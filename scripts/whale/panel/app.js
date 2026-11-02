'use strict';
const $ = id => document.getElementById(id);
const escapeHTML = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const prettyBytes = n => n == null ? '—' : n >= 1048576 ? `${(n / 1048576).toFixed(2)} MiB` : `${(n / 1024).toFixed(0)} KiB`;
const names = {check:'检查项目', assets:'校验资源',test:'主机测试',build:'构建固件',package:'生成资源候选','device-sync':'核验并备份设备','device-install':'安装独立资源','device-rollback':'恢复更新前资源'};
const statuses = {queued:'等待中',running:'运行中',succeeded:'已完成',failed:'失败',blocked:'条件不满足',cancelled:'已取消',timed_out:'已超时',unconfirmed:'结果待复核',interrupted:'已中断'};
const triggers = {manual:'手动选择',neutral:'默认待机',happy:'开心',thinking:'思考',angry:'生气',embarrassed:'害羞',sad:'难过',surprised:'惊讶',sleepy:'困倦',idle:'空闲事件',wake:'唤醒事件'};
let actions = [], state = null, previousTasks = new Map(), toastTimer;

async function api(path, options = {}) {
  const response = await fetch(path, {...options, headers:{'X-Whale-Token':window.WHALE_TOKEN, ...options.headers}});
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || `请求失败 ${response.status}`);
  return body;
}
const post = (path,value) => api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});
function toast(message) {
  $('toast').textContent = message; $('toast').hidden = false;
  clearTimeout(toastTimer); toastTimer = setTimeout(() => $('toast').hidden = true, 6000);
}
function page() {
  const target = location.hash.slice(1) || 'overview';
  const allowed = ['overview','library','updates','tasks','metrics'];
  const selected = allowed.includes(target) ? target : 'overview';
  document.querySelectorAll('.page').forEach(el => el.hidden = el.id !== 'page-' + selected);
  document.querySelectorAll('[data-page]').forEach(el => el.classList.toggle('active',el.dataset.page === selected));
}
function renderActions() {
  const term = $('search').value.trim().toLowerCase();
  const visible = actions.filter(a => (a.id + a.label).toLowerCase().includes(term));
  $('actionGrid').innerHTML = visible.map(a => `<article class="action-card"><div class="action-top"><span class="mono">${escapeHTML(a.id.toUpperCase())}</span><span class="${a.source === 'staged' ? 'staged' : ''}">${a.source === 'staged' ? '本机草稿' : '工程包'}</span></div><div class="action-picture"><img src="${escapeHTML(a.preview)}" alt="${escapeHTML(a.label)}" loading="lazy"></div><h3 title="${escapeHTML(a.label)}">${escapeHTML(a.label)}</h3><div class="action-data"><span>${a.width} × ${a.height} · ${a.frames} 帧</span><span>${prettyBytes(a.bytes)}</span></div><button class="button secondary" data-edit="${escapeHTML(a.id)}">名称与触发规则</button></article>`).join('') || '<div class="empty">没有匹配的动作</div>';
  $('navCount').textContent = actions.length;
  $('libraryCount').textContent = actions.length;
  const staged = actions.filter(a => a.source === 'staged').length;
  $('draftCount').textContent = staged ? `${staged} 项本机草稿` : '没有待更新草稿';
  $('clearDraft').disabled = !staged;
}
async function loadActions() {
  actions = await api('/api/actions'); renderActions();
  const hero = actions.find(a => a.id === 'w024') || actions[0];
  if (hero) $('heroWhale').src = hero.preview;
  $('replaceTarget').innerHTML = '<option value="">新增动作（需要动态引擎）</option>' + actions.map(a => `<option value="${escapeHTML(a.id)}">替换 ${escapeHTML(a.label)} · ${escapeHTML(a.id)}</option>`).join('');
}
function renderPackage(value) {
  if (!value) return;
  if (value.stale) {
    $('packageInfo').className='empty';
    $('packageInfo').innerHTML='草稿已变更，旧候选已过期。<span>重新生成候选后再核对更新内容。</span>';return;
  }
  if (value.status === 'no_changes') {
    $('packageInfo').className = 'empty';
    $('packageInfo').innerHTML = '没有资源变化。<span>本次无需传输，也没有编译或写入固件。</span>'; return;
  }
  $('packageInfo').className = '';
  const conditions = value.engine_upgrade_required_for_registry ? '此候选含新增菜单或规则。当前固件仍需升级为数据化引擎，不能直接安装。' : '候选只替换现有动作媒体。先核验并完整备份，再安装资源；不会重刷应用。';
  $('packageInfo').innerHTML = `<div class="package-badge">${conditions}</div><div class="package-facts"><div><span>候选大小</span><strong>${prettyBytes(value.bytes)}</strong></div><div><span>分区剩余</span><strong>${value.free_percent}%</strong></div><div><span>变化媒体</span><strong>${value.changed_media.length} 个</strong></div><div><span>传输方式</span><strong>整个资源包</strong></div><div><span>应用编译 / 写入</span><strong>本轮均未执行</strong></div><div><span>SHA256</span><code>${escapeHTML(value.sha256)}</code></div></div><a href="/api/package/download" id="downloadPackage" class="button secondary">下载本机候选包</a><p class="fine">下载通过本页认证；不是设备安装按钮。</p>`;
  $('downloadPackage').addEventListener('click',async event => {
    event.preventDefault();
    try {
      const response = await fetch('/api/package/download',{headers:{'X-Whale-Token':window.WHALE_TOKEN}});
      if (!response.ok) throw new Error('候选下载失败');
      const url = URL.createObjectURL(await response.blob());
      const link = document.createElement('a');link.href=url;link.download='whale-assets-candidate.bin';link.click();
      setTimeout(() => URL.revokeObjectURL(url),1000);
    } catch(error) { toast(error.message); }
  });
}
async function loadState() {
  state = await api('/api/state');
  $('boardName').textContent = state.board;
  $('branch').textContent = state.git.branch || '未知';
  $('commit').textContent = state.git.commit || '未知';
  $('projectStatus').textContent = state.errors.length || state.warnings.length ? '有待处理项' : '工程已核对';
  $('usedSize').textContent = prettyBytes(state.resource.bytes);
  const ratio = 100 * state.resource.bytes / state.resource.capacity;
  $('usedBar').style.width = `${Math.min(100,ratio)}%`;
  $('usedPercent').textContent = `${ratio.toFixed(1)}%`;
  const audit=state.resource.audit;
  if(audit){
    const labels={animations:'动画 GIF',fonts:'中文字体',speech_models:'语音模型',other_images:'其他图片',metadata:'清单',other:'其他资源'};
    $('resourceBreakdown').innerHTML=Object.entries(audit.categories).filter(([,v])=>v.bytes).map(([k,v])=>`<div><span>${escapeHTML(labels[k])} · ${v.count} 项</span><b>${prettyBytes(v.bytes)}</b></div>`).join('');
    const rates=[...new Set(audit.files.filter(f=>f.category==='animations').map(f=>f.encoded_fps))];
    $('encodingNote').textContent=`资源包占整块 Flash 的 ${audit.resource_share_of_physical_flash_percent}%；其余空间按分区预留，不能直接借用。动画编码帧率 ${rates.join(' / ')} fps；实际播放 FPS、CPU 和运行内存尚未测量。`;
  }
  $('buildButton').disabled = !!state.build_block_reason;
  $('buildButton').title = state.build_block_reason || '';
  $('buildReason').textContent = state.build_block_reason || '构建使用现有ESP-IDF环境，不自动安装或清理缓存。';
  renderPackage(state.latest_package);
  const device=state.device || {}, ref=device.reference, last=device.last_write;
  $('connection').textContent=ref ? (device.latest?.status==='installed_verified' ? '资源已校验 · 播放待确认' : device.latest?.status==='rolled_back_verified' ? '回退资源已校验' : '设备资源已核验') : '实机未核验';
  $('sourceNotice').textContent=ref ? '当前素材来自已核验的设备资源。仓库固件源码仍为旧 10 动作版本；13 动作 / v7 完整源码待同步。' : '工程资源与实机分别核对；13 动作 / v7 完整源码仍待同步。';
  $('resourceSource').textContent=ref ? '已核验设备资源基线' : '当前工程包';
  $('deviceRuntime').textContent=ref ? '资源已核验；运行状态未实时读取' : '未读取';
  $('writeAcceptance').textContent=device.latest?.status==='installed_verified' ? '资源与保护区校验通过；已请求重启' : device.latest?.status==='rolled_back_verified' ? '回退资源与保护区校验通过；已请求重启' : '尚无安装验收记录';
  if (!$('deviceForm').dataset.loaded) {
    $('devicePort').value=device.settings?.port || '';
    $('deviceIdentity').value=device.settings?.identity || '';
    $('deviceTools').value=device.settings?.tools_directory || '';
    $('deviceForm').dataset.loaded='true';
  }
  $('deviceStatus').textContent=ref ? `设备资源基线：${ref.resource_sha256.slice(0,16)}… · 核验时间 ${ref.updated_at}。${device.latest?.status==='installed_verified' ? '写入与保护区校验通过，屏幕播放仍待确认。' : '可生成基于该设备的替换候选。'}` : '尚未核验设备；先保存配置，再完整备份。';
  const candidate=state.latest_package;
  $('installResource').disabled=!(ref && candidate && !candidate.stale && candidate.changed_media?.length && !candidate.engine_upgrade_required_for_registry && candidate.base_sha256===ref.resource_sha256);
  $('rollbackResource').disabled=!(last && ['installed_verified','write_failed','writing'].includes(last.status));
}
async function loadTasks() {
  const tasks = await api('/api/tasks');
  const open = new Set([...document.querySelectorAll('.task details[open]')].map(el=>el.dataset.id+(el.classList.contains('live-log') ? '-log' : '')));
  $('taskList').innerHTML = tasks.map(task => `<article class="task"><div class="task-head"><strong>${escapeHTML(names[task.kind] || task.kind)}</strong><span class="pill soft">${escapeHTML(statuses[task.status] || task.status)}</span><span class="elapsed">${task.elapsed_seconds == null ? '记录中' : task.elapsed_seconds.toFixed(2)+' s'}</span>${['queued','running'].includes(task.status) && !task.kind.startsWith('device-') ? `<button class="button quiet" data-cancel="${task.id}">取消</button>` : ''}</div><p class="fine">${escapeHTML(task.created_at)}${task.reason ? ' · '+escapeHTML(task.reason) : ''}</p><details data-id="${task.id}" ${open.has(task.id) ? 'open' : ''}><summary>查看限量摘要 · 完整日志保存在本机</summary><pre>${escapeHTML((task.summary || []).join('\n') || '任务运行中，完成后显示摘要。')}</pre></details><details class="live-log" data-id="${task.id}" ${open.has(task.id+'-log') ? 'open' : ''}><summary>运行日志末尾（最多 48KB）</summary><pre data-log="${task.id}">展开后读取本机日志</pre><p class="fine">${escapeHTML(task.log)}</p></details></article>`).join('') || '<div class="card empty">还没有任务。<span>可以先检查项目或校验资源。</span></div>';
  for (const task of tasks) {
    if ((task.kind === 'package' || task.kind.startsWith('device-')) && !['queued','running'].includes(task.status) && previousTasks.get(task.id) !== task.status) { await loadState();await loadActions(); }
    const log=document.querySelector(`[data-log="${task.id}"]`);
    if (log?.parentElement.open) { try { log.textContent=(await api('/api/logs/'+task.id)).text || '等待任务输出…'; } catch(error) { log.textContent=error.message; } }
    previousTasks.set(task.id,task.status);
  }
}
document.addEventListener('click',async event => {
  const run = event.target.closest('[data-run]');
  if (run) {
    run.disabled = true;
    try { await post('/api/tasks/start',{kind:run.dataset.run});toast('任务已开始，工具会等待完成。');await loadTasks(); }
    catch(error) { toast(error.message); }
    finally { if (run.id !== 'buildButton' || !state?.build_block_reason) run.disabled=false; }
  }
  const edit = event.target.closest('[data-edit]');
  if (edit) {
    const action = actions.find(a=>a.id===edit.dataset.edit);
    $('editId').value=action.id;$('editLabel').value=action.label;
    $('editTrigger').innerHTML=Object.entries(triggers).map(([value,label])=>`<option value="${value}">${label}</option>`).join('');
    $('editTrigger').value=action.trigger;$('editCooldown').value=action.cooldown_ms;
    $('editDialog').showModal();
  }
  const close=event.target.closest('[data-close]');if(close) $(close.dataset.close).close();
  const cancel=event.target.closest('[data-cancel]');if(cancel) { await post('/api/tasks/cancel',{id:cancel.dataset.cancel});toast('已请求取消，正在回收进程。'); }
});
document.addEventListener('click',async event=>{
  const button=event.target.closest('[data-device]');if(!button)return;
  const operation=button.dataset.device;
  const message=operation==='sync' ? '核验并读取完整 32MB 备份会重启设备，期间暂停语音。继续？' : operation==='install' ? '将只更新独立资源分区并重启。已核对候选和备份；保持供电，期间不能取消。继续？' : '恢复最近完整备份里的资源，应用保持不变。期间保持供电。继续？';
  if (!window.confirm(message))return;
  button.disabled=true;
  try { await post('/api/device/start',{operation,sha256:state?.latest_package?.sha256});toast('设备任务已开始，请在任务与日志中查看。');location.hash='tasks';await loadTasks(); }
  catch(error){toast(error.message);}
});
$('deviceForm').addEventListener('submit',async event=>{
 event.preventDefault();
 try {await post('/api/device/settings',{port:$('devicePort').value,identity:$('deviceIdentity').value,tools_directory:$('deviceTools').value});toast('本机设备配置已保存。');await loadState();}catch(error){toast(error.message);}
});
$('openImport').addEventListener('click',() => { $('importStatus').textContent='';$('importDialog').showModal(); });
$('search').addEventListener('input',renderActions);
$('clearDraft').addEventListener('click',async () => {try {await post('/api/draft/clear',{});await loadActions();await loadState();toast('草稿已撤销，原工程资源未改变。');} catch(error){toast(error.message);}});
$('importForm').addEventListener('submit',async event => {
  event.preventDefault(); const file=$('fileInput').files[0];if(!file)return;
  $('importSubmit').disabled=true;$('importStatus').textContent='正在检查素材；视频将按需要转换并缓存…';
  try {
    await api('/api/actions/import',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-File-Name':encodeURIComponent(file.name),'X-Replace-Id':$('replaceTarget').value},body:await file.arrayBuffer()});
    await loadActions();await loadState();$('importDialog').close();$('importForm').reset();toast('已导入本机草稿，尚未更新设备。');
  } catch(error) { $('importStatus').textContent=error.message; }
  finally {$('importSubmit').disabled=false;}
});
$('editForm').addEventListener('submit',async event => {
  event.preventDefault();
  try {await post('/api/actions/edit',{id:$('editId').value,label:$('editLabel').value,trigger:$('editTrigger').value,cooldown_ms:Number($('editCooldown').value)});await loadActions();$('editDialog').close();toast('规则草稿已保存，需要动态引擎才能在设备生效。');}
  catch(error){toast(error.message);}
});
window.addEventListener('hashchange',page);page();
Promise.all([loadState(),loadActions(),loadTasks()]).catch(error=>toast(error.message));
setInterval(()=>loadTasks().catch(()=>{}),2500);
setInterval(()=>loadState().catch(()=>{}),15000);
