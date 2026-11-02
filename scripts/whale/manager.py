"""Local-only whale panel with guarded USB resource-only updates."""
import argparse
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
from pathlib import Path
import secrets
import sys

from device_resources import DeviceResources, config as device_config
import subprocess
import threading
import time
import urllib.parse

from resource_audit import inspect_resources
from doctor import inspect
from resource_manager import ResourceStore, TRIGGERS
from workflow_core import BusyError, Lease, TaskRunner, command_for, read_json, sha, write_json

ROOT=Path(__file__).resolve().parents[2]
PANEL=Path(__file__).with_name('panel')


class Application:
    def __init__(self,root):
        self.root=Path(root).resolve()
        self.token=secrets.token_urlsafe(32)
        self.store=ResourceStore(root)
        self.runner=TaskRunner(root)
        self.cached=None
        self.cached_at=0
        self.cache_lock=threading.Lock()

    def state(self):
        with self.cache_lock:
            if self.cached is None or time.monotonic()-self.cached_at>5:
                self.cached=inspect(self.root)
                blob=self.store.base_blob()
                files=self.store.base()
                self.cached['resource']=dict(bytes=len(blob),sha256=sha(blob),capacity=int(self.cached['profile']['allowed_write_partitions']['assets']['size'],0),
                                             action_count=sum(name.endswith('.gif') for name in files))
                self.cached['resource']['audit']=inspect_resources(blob,self.cached['resource']['capacity'],self.cached['profile']['flash_bytes'])
                self.cached_at=time.monotonic()
            result=dict(self.cached)
        result['latest_package']=read_json(self.store.workspace/'latest-package.json')
        result['draft_revision']=self.store.draft()['revision']
        if result['latest_package']:
            result['latest_package']['stale']=result['latest_package'].get('draft_revision')!=result['draft_revision']
        result['staged_count']=len(self.store.draft()['actions'])
        result['triggers']=TRIGGERS
        result['device']=dict(settings=read_json(self.store.workspace/'device.json',{}),
                              reference=read_json(self.store.workspace/'device-reference.json'),
                              latest=read_json(self.store.workspace/'latest-device-operation.json'),
                              last_write=read_json(self.store.workspace/'last-device-write.json'))
        result['physical_device_checked']=bool(result['device']['reference'])
        result['capabilities']=dict(device_install=True,incremental_device_install=False,
                                   source_dynamic_registry=False,physical_device_checked=result['physical_device_checked'])
        try:
            command_for(self.root,'build')
            result['build_block_reason']=None
        except ValueError as error:
            result['build_block_reason']=str(error)
        return result


def handler_for(app):
    class Handler(BaseHTTPRequestHandler):
        server_version='WhaleManager/0.1'
        def log_message(self,*_): pass  # No token/filename request logging.

        def reply(self,status,content,kind='application/json; charset=utf-8',cookie=False,extra=None):
            data=json.dumps(content,ensure_ascii=False).encode() if kind.startswith('application/json') else content
            self.send_response(status)
            self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Referrer-Policy','no-referrer')
            self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'")
            if cookie: self.send_header('Set-Cookie','whale_session='+app.token+'; HttpOnly; SameSite=Strict; Path=/')
            for key,value in (extra or {}).items(): self.send_header(key,value)
            self.end_headers()
            self.wfile.write(data)

        def authorized(self):
            return hmac.compare_digest(self.headers.get('X-Whale-Token','').encode(),app.token.encode())

        def host_ok(self):
            host=self.headers.get('Host','')
            return host in ('127.0.0.1:'+str(self.server.server_port),'localhost:'+str(self.server.server_port))

        def do_GET(self):
            try:
                if not self.host_ok(): return self.reply(403,dict(error='只允许本机访问'))
                path=urllib.parse.urlsplit(self.path).path
                if path in ('/','/index.html'):
                    return self.reply(200,(PANEL/'index.html').read_bytes(),'text/html; charset=utf-8',cookie=True)
                if path=='/bootstrap.js':
                    if self.headers.get('Sec-Fetch-Site')=='cross-site': return self.reply(403,dict(error='拒绝跨站加载控制脚本'))
                    return self.reply(200,('window.WHALE_TOKEN='+json.dumps(app.token)+';').encode(),'text/javascript; charset=utf-8')
                if path in ('/app.js','/styles.css'):
                    return self.reply(200,(PANEL/path[1:]).read_bytes(), 'text/javascript; charset=utf-8' if path.endswith('.js') else 'text/css; charset=utf-8')
                if path.startswith('/media/'):
                    cookie='whale_session='+app.token
                    if cookie not in self.headers.get('Cookie','').split('; '): return self.reply(403,dict(error='请从本机面板打开'))
                    parts=path.split('/')
                    if len(parts)!=4: return self.reply(404,dict(error='素材不存在'))
                    if parts[2]=='base':
                        item=app.store.base().get(parts[3])
                        if not item or not parts[3].endswith('.gif'): return self.reply(404,dict(error='素材不存在'))
                        data=item[0]
                    elif parts[2]=='staged' and len(parts[3])==68 and parts[3].endswith('.gif') and all(c in '0123456789abcdef' for c in parts[3][:-4]):
                        data=(app.store.workspace/'media'/parts[3]).read_bytes()
                    else: return self.reply(404,dict(error='素材不存在'))
                    return self.reply(200,data,'image/gif')
                if not self.authorized(): return self.reply(403,dict(error='请求认证失败，请刷新面板'))
                if path=='/api/state': return self.reply(200,app.state())
                if path=='/api/actions': return self.reply(200,app.store.actions())
                if path=='/api/tasks': return self.reply(200,app.runner.list())
                if path.startswith('/api/logs/'):
                    identifier=path.rsplit('/',1)[1]
                    record=app.runner.get(identifier)
                    if not record: return self.reply(404,dict(error='任务不存在'))
                    log=app.runner.jobs/identifier/'full.log'
                    data=b''
                    if log.exists():
                        with log.open('rb') as stream:
                            stream.seek(max(0,log.stat().st_size-48000));data=stream.read()
                    return self.reply(200,dict(text=data.decode('utf-8',errors='replace'),log=record['log']))
                if path.startswith('/api/tasks/'):
                    value=app.runner.get(path.rsplit('/',1)[1])
                    return self.reply(200,value) if value else self.reply(404,dict(error='任务不存在'))
                if path=='/api/package/download':
                    manifest=read_json(app.store.workspace/'latest-package.json')
                    if not manifest or not manifest.get('artifact'): return self.reply(404,dict(error='没有候选包'))
                    if manifest.get('draft_revision')!=app.store.draft()['revision']: return self.reply(409,dict(error='草稿已变化，请重新生成候选包'))
                    candidate=(app.root/manifest['artifact']).resolve()
                    if not candidate.is_relative_to(app.store.workspace/'packages'): return self.reply(403,dict(error='候选路径不合法'))
                    return self.reply(200,candidate.read_bytes(),'application/octet-stream',extra={'Content-Disposition':'attachment; filename=whale-assets-candidate.bin'})
                return self.reply(404,dict(error='页面或接口不存在'))
            except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error: self.reply(400,dict(error=str(error)[:500]))

        def do_POST(self):
            try:
                if not self.host_ok() or not self.authorized(): return self.reply(403,dict(error='请求认证失败'))
                origin=self.headers.get('Origin')
                if origin and origin not in ('http://127.0.0.1:'+str(self.server.server_port),'http://localhost:'+str(self.server.server_port)):
                    return self.reply(403,dict(error='拒绝跨站控制请求'))
                size=int(self.headers.get('Content-Length','0'))
                if not 0<size<=20*1048576: return self.reply(413,dict(error='请求需在20MiB以内'))
                self.connection.settimeout(120)
                body=self.rfile.read(size)
                if len(body)!=size: return self.reply(400,dict(error='上传未完成'))
                path=urllib.parse.urlsplit(self.path).path
                if path=='/api/actions/import':
                    name=urllib.parse.unquote(self.headers.get('X-File-Name','animation.gif'))
                    target=self.headers.get('X-Replace-Id') or None
                    return self.reply(200,app.store.import_data(body,name,target))
                if size>1048576: return self.reply(413,dict(error='配置请求过大'))
                value=json.loads(body)
                if not isinstance(value,dict): raise ValueError('配置必须是JSON对象')
                if path=='/api/actions/edit': return self.reply(200,app.store.edit(value['id'],value))
                if path=='/api/draft/clear':
                    app.store.clear_draft()
                    return self.reply(200,dict(ok=True))
                if path=='/api/device/settings':
                    if app.runner.active: raise BusyError('任务运行中不能修改设备配置')
                    # Do not accept arbitrary commands, Python executables or network endpoints.
                    settings={k:str(value.get(k,'')).strip() for k in ('port','identity','tools_directory')}
                    import re
                    if not re.fullmatch(r'COM[1-9][0-9]*|/dev/(?:tty|cu\.)[A-Za-z0-9._/-]+',settings['port']): raise ValueError('串口格式无效')
                    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}',settings['identity']): raise ValueError('设备MAC格式无效')
                    write_json(app.store.workspace/'device.json',settings)
                    return self.reply(200,dict(ok=True))
                if path=='/api/device/start':
                    operation=value.get('operation')
                    if operation not in ('sync','install','rollback'): raise ValueError('设备操作无效')
                    device_config(app.root)
                    command=[sys.executable,str(app.root/'scripts/whale/device_resources.py'),operation]
                    if operation=='install':
                        digest=value.get('sha256','')
                        import re
                        if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest): raise ValueError('请核对候选SHA256')
                        command+=['--sha256',digest]
                    return self.reply(202,app.runner.start('device-'+operation,command,timeout=1800))
                if path=='/api/tasks/start':
                    kind=value['kind']
                    command=command_for(app.root,kind)
                    return self.reply(202,app.runner.start(kind,command,timeout=1800 if kind=='build' else 300))
                if path=='/api/tasks/cancel': return self.reply(200,dict(cancelled=app.runner.cancel(value['id'])))
                return self.reply(404,dict(error='接口不存在；此版本未开放设备写入'))
            except BusyError as error: self.reply(409,dict(error=str(error)))
            except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error: self.reply(400,dict(error=str(error)[:500]))
    return Handler


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    args=parser.parse_args()
    if not 1024<=args.port<=65535: parser.error('端口需在1024..65535')
    with Lease(ROOT/'local-only/manager/locks','manager-server'):
        app=Application(ROOT)
        server=ThreadingHTTPServer(('127.0.0.1',args.port),handler_for(app))
        server.daemon_threads=True
        print('Whale manager: http://127.0.0.1:'+str(args.port),flush=True)
        print('USB resource updates require device binding and verified backup; no application writes.',flush=True)
        try: server.serve_forever()
        except KeyboardInterrupt: pass
        finally: server.server_close()


if __name__=='__main__': main()
