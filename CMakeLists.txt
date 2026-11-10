# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.5.2")
project(xiaozhi)

# The SDK has an exFAT implementation but does not expose its switch in Kconfig.
# FatFs and callers that include ff.h must agree on FATFS and FIL layouts.
if(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_2_06
   AND NOT CONFIG_WAVESHARE_WHALE_VALIDATION)
    if(CONFIG_FATFS_LFN_NONE)
        message(FATAL_ERROR "GrokBot exFAT requires FatFs long filename support")
    endif()
    idf_component_get_property(grok_fatfs_lib fatfs COMPONENT_LIB)
    idf_component_get_property(grok_fatfs_dir fatfs COMPONENT_DIR)
    set(grok_fatfs_source "${grok_fatfs_dir}/src/ffconf.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${grok_fatfs_source}")
    file(READ "${grok_fatfs_source}" grok_fatfs_config)
    string(REGEX REPLACE "#define[ \t]+FF_FS_EXFAT[ \t]+0"
           "#define FF_FS_EXFAT 1" grok_fatfs_exfat "${grok_fatfs_config}")
    if(grok_fatfs_exfat STREQUAL grok_fatfs_config)
        message(FATAL_ERROR "GrokBot exFAT configuration no longer matches this ESP-IDF FatFs")
    endif()
    # The exFAT directory path uses FF_USE_LABEL in a C expression as well.
    if(NOT CONFIG_FATFS_USE_LABEL)
        string(APPEND grok_fatfs_exfat "\n#undef FF_USE_LABEL\n#define FF_USE_LABEL 0\n")
    endif()
    set(grok_fatfs_header "${CMAKE_BINARY_DIR}/grok_ffconf.h")
    file(GENERATE OUTPUT "${grok_fatfs_header}" CONTENT "${grok_fatfs_exfat}")
    target_compile_options(${grok_fatfs_lib} PRIVATE "-include${grok_fatfs_header}")
    set(grok_fatfs_include "${CMAKE_BINARY_DIR}/grok_fatfs")
    file(GENERATE OUTPUT "${grok_fatfs_include}/ff.h"
         CONTENT "#include \"${grok_fatfs_header}\"\n#include \"${grok_fatfs_dir}/src/ff.h\"\n")
    target_include_directories(${grok_fatfs_lib} BEFORE INTERFACE "${grok_fatfs_include}")
endif()
