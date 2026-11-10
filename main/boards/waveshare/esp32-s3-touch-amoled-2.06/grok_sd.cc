#ifdef GROK_FIREWORKS
#include "display/pet_media.h"

#include <cstdio>
#include <new>

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

#include "diskio_impl.h"

namespace PetMedia {
namespace {
constexpr char kTag[] = "GrokSD";
constexpr auto kHost = SPI3_HOST;
constexpr unsigned kTaskStack = 8192;

// All FatFs operations run in one worker at a time. No VFS path is registered.
std::atomic<bool> busy{false};
sdmmc_card_t* active_card = nullptr;

const ff_diskio_impl_t kReadOnlyDisk = {
    .init = [](BYTE) -> DSTATUS { return STA_PROTECT; },
    .status = [](BYTE) -> DSTATUS { return STA_PROTECT; },
    .read = [](BYTE, BYTE* buffer, uint32_t sector, UINT count) -> DRESULT {
        return sdmmc_read_sectors(active_card, buffer, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
    },
    .write = [](BYTE, const BYTE*, uint32_t, UINT) -> DRESULT { return RES_WRPRT; },
    .ioctl = [](BYTE, BYTE command, void* buffer) -> DRESULT {
        switch (command) {
            case CTRL_SYNC:
                return RES_OK;
            case GET_SECTOR_COUNT:
                *static_cast<DWORD*>(buffer) = active_card->csd.capacity;
                return RES_OK;
            case GET_SECTOR_SIZE:
                *static_cast<WORD*>(buffer) = active_card->csd.sector_size;
                return RES_OK;
            case CTRL_TRIM:
                return RES_WRPRT;
            default:
                return RES_PARERR;
        }
    },
};

class Card {
public:
    ~Card() {
        if (registered_) {
            f_mount(nullptr, drive_, 0);
            ff_diskio_unregister(drive_number_);
            active_card = nullptr;
        }
        if (device_ >= 0) {
            sdspi_host_remove_device(device_);
        }
        if (bus_) {
            spi_bus_free(kHost);
        }
    }

    bool Mount() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = GPIO_NUM_1;
        bus.miso_io_num = GPIO_NUM_3;
        bus.sclk_io_num = GPIO_NUM_2;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.data4_io_num = -1;
        bus.data5_io_num = -1;
        bus.data6_io_num = -1;
        bus.data7_io_num = -1;
        auto err = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            return Report("SPI bus", err);
        }
        bus_ = true;
        auto slot = sdspi_device_config_t SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = kHost;
        slot.gpio_cs = GPIO_NUM_17;
        err = sdspi_host_init_device(&slot, &device_);
        if (err != ESP_OK) {
            return Report("SDSPI device", err);
        }
        auto host = sdmmc_host_t SDSPI_HOST_DEFAULT();
        host.slot = device_;
        err = sdmmc_card_init(&host, &card_);
        if (err != ESP_OK) {
            return Report("card probe", err);
        }
        if (ff_diskio_get_drive(&drive_number_) != ESP_OK || drive_number_ > 9) {
            ESP_LOGW(kTag, "No free FatFs volume");
            return false;
        }
        drive_[0] = '0' + drive_number_;
        active_card = &card_;
        ff_diskio_register(drive_number_, &kReadOnlyDisk);
        registered_ = true;
        const FRESULT result = f_mount(&fs_, drive_, 1);
        if (result != FR_OK) {
            ESP_LOGW(kTag, "Read-only FAT mount failed (%d), card capacity %llu bytes",
                     result, static_cast<unsigned long long>(Capacity()));
            return false;
        }
        return true;
    }

    std::shared_ptr<Clip> Load(unsigned effect) {
        char path[64];
        std::snprintf(path, sizeof(path), "%s/grokbot/fireworks/%s.gra", drive_, kFireworkEffects[effect]);
        return LoadPack<Clip>(path);
    }

    FireworkSelection ReadFireworkSelection() {
        FireworkSelection selection;
        char path[48];
        std::snprintf(path, sizeof(path), "%s/grokbot/fireworks/selection.json", drive_);
        FIL file = {};
        if (f_open(&file, path, FA_READ) != FR_OK) {
            return selection;
        }
        char data[FireworkSelection::kMaxBytes];
        const auto size = f_size(&file);
        UINT read = 0;
        const bool complete = size > 0 && size <= sizeof(data) &&
                              f_read(&file, data, size, &read) == FR_OK && read == size;
        f_close(&file);
        if (!complete || !FireworkSelection::Parse({data, read}, selection)) {
            ESP_LOGW(kTag, "Invalid firework selection; using default weights");
        }
        return selection;
    }

    std::shared_ptr<TntClip> LoadTnt() {
        char path[40];
        std::snprintf(path, sizeof(path), "%s/grokbot/tnt/show.gtn", drive_);
        return LoadPack<TntClip>(path);
    }

    template <typename Pack>
    std::shared_ptr<Pack> LoadPack(const char* path) {
        FIL file = {};
        const FRESULT opened = f_open(&file, path, FA_READ);
        if (opened != FR_OK) {
            ESP_LOGI(kTag, "%s unavailable (%d)", path, opened);
            return {};
        }

        const auto size = f_size(&file);
        std::shared_ptr<Pack> clip(new (std::nothrow) Pack);
        auto buffer = clip ? clip->Allocate(size) : std::span<uint8_t>();
        UINT read = 0;
        const bool complete = !buffer.empty() &&
                              f_read(&file, buffer.data(), buffer.size(), &read) == FR_OK &&
                              read == buffer.size();
        f_close(&file);
        if (!complete || !clip->Validate(buffer)) {
            ESP_LOGW(kTag, "Unreadable or invalid pack %s", path);
            return {};
        }
        ESP_LOGI(kTag, "Validated %s (%u bytes)", path, static_cast<unsigned>(size));
        return clip;
    }

    void LoadFace(FaceRequest& request) {
        char path[32];
        std::snprintf(path, sizeof(path), "%s/grokbot/face/profile.json", drive_);
        FIL file = {};
        const auto result = f_open(&file, path, FA_READ);
        if (result != FR_OK) {
            ESP_LOGI(kTag, "Face profile unavailable (%d)", result);
            return;
        }
        char data[PetFaceProfile::kMaxBytes + 1];
        const auto size = f_size(&file);
        UINT read = 0;
        const bool complete = size > 0 && size <= PetFaceProfile::kMaxBytes &&
                              f_read(&file, data, size, &read) == FR_OK && read == size;
        f_close(&file);
        if (complete) {
            data[read] = '\0';
            request.valid = PetFaceProfile::Parse({data, read}, request.profile);
        }
        if (!request.valid) {
            ESP_LOGW(kTag, "Unreadable or invalid face profile");
        }
    }

    std::shared_ptr<TotemPack> LoadTotem() {
        char path[48];
        std::snprintf(path, sizeof(path), "%s/grokbot/totem/totem.gra", drive_);
        return LoadPack<TotemPack>(path);
    }

private:
    bool Report(const char* stage, esp_err_t err) {
        ESP_LOGW(kTag, "%s failed: %s", stage, esp_err_to_name(err));
        return false;
    }

    uint64_t Capacity() const {
        return uint64_t(card_.csd.capacity) * card_.csd.sector_size;
    }

    sdmmc_card_t card_{};
    FATFS fs_{};
    sdspi_dev_handle_t device_ = -1;
    BYTE drive_number_ = 0;
    char drive_[3] = "0:";
    bool registered_ = false;
    bool bus_ = false;
};

template <typename RequestType, void (*Load)(Card&, RequestType&)>
bool StartWorker(std::shared_ptr<RequestType> request) {
    if (busy.exchange(true)) {
        return false;
    }
    auto* context = new (std::nothrow) std::shared_ptr<RequestType>(std::move(request));
    if (context && xTaskCreate([](void* arg) {
            {
                std::unique_ptr<std::shared_ptr<RequestType>> context(
                    static_cast<std::shared_ptr<RequestType>*>(arg));
                auto& request = *context;
                {
                    Card card;
                    if (card.Mount()) {
                        Load(card, *request);
                    }
                }
                request->done.store(true, std::memory_order_release);
            }
            busy.store(false);
            vTaskDelete(nullptr);
        }, "grok_sd", kTaskStack, context, 2, nullptr) == pdPASS) {
        return true;
    }
    delete context;
    busy.store(false);
    ESP_LOGW(kTag, "Could not start SD worker");
    return false;
}
}  // namespace

std::shared_ptr<Request> LoadFirework(unsigned roll) {
    if (roll >= 100 || busy.load()) {
        return {};
    }
    std::shared_ptr<Request> request(new (std::nothrow) Request(roll));
    return request && StartWorker<Request, [](Card& card, Request& result) {
        result.effect = card.ReadFireworkSelection().Pick(result.roll);
        result.clip = card.Load(result.effect);
    }>(request) ? request : nullptr;
}
std::shared_ptr<FaceRequest> LoadFaceProfile() {
    if (busy.load()) {
        return {};
    }
    std::shared_ptr<FaceRequest> request(new (std::nothrow) FaceRequest);
    return request && StartWorker<FaceRequest, [](Card& card, FaceRequest& result) {
        card.LoadFace(result);
    }>(request) ? request : nullptr;
}
std::shared_ptr<TntRequest> LoadTnt() {
    if (busy.load()) {
        return {};
    }
    std::shared_ptr<TntRequest> request(new (std::nothrow) TntRequest);
    return request && StartWorker<TntRequest, [](Card& card, TntRequest& result) {
        result.clip = card.LoadTnt();
    }>(request) ? request : nullptr;
}
std::shared_ptr<TotemRequest> LoadTotem() {
    if (busy.load()) {
        return {};
    }
    std::shared_ptr<TotemRequest> request(new (std::nothrow) TotemRequest);
    return request && StartWorker<TotemRequest, [](Card& card, TotemRequest& result) {
        result.pack = card.LoadTotem();
    }>(request) ? request : nullptr;
}
}  // namespace PetMedia
#endif
