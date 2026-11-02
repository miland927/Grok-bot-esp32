"""Portable host workflow primitives; never opens a serial port or writes Flash."""
from __future__ import annotations

import contextlib
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid


def now():
    return dt.datetime.now(dt.timezone(dt.timedelta(hours=9))).isoformat(timespec='seconds')


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    try:
        for attempt in range(8):
            try:
                os.replace(temp,path)
                break
            except PermissionError:
                if attempt==7: raise
                time.sleep(min(.01*2**attempt,.4))
    finally:
        if temp.exists(): temp.unlink()


def read_json(path, default=None):
    path = Path(path)
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else default


def sha(data):
    return hashlib.sha256(data).hexdigest()


class BusyError(RuntimeError):
    pass


class Lease:
    """OS-owned lock: released on process death; the lock file is never unlinked."""
    def __init__(self, directory, identity):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        self.path = directory / (sha(identity.encode()) + '.lock')
        self.file = None

    def __enter__(self):
        stream = open(self.path, 'a+b')
        stream.seek(0, 2)
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        stream.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            stream.close()
            raise BusyError('另一个任务正在使用该资源') from error
        self.file = stream
        return self

    def __exit__(self, *_):
        if self.file:
            if os.name == 'nt':
                import msvcrt
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_UN)
            self.file.close()
            self.file = None


class ExperimentLedger:
    def __init__(self, root):
        self.root = Path(root)

    def reserve_write(self, experiment, limit=2):
        if not experiment or len(experiment) > 100:
            raise ValueError('需要稳定的实验编号')
        with Lease(self.root/'locks', 'experiment-budget'):
            path = self.root/'experiment-budget.json'
            ledger = read_json(path, {})
            record = ledger.setdefault(experiment, dict(attempts=0, limit=limit))
            if record['attempts'] >= min(record['limit'], limit):
                raise ValueError('同一实验已达到自动写入预算，必须先复核证据')
            record['attempts'] += 1
            record['updated_at'] = now()
            write_json(path, ledger)
            return record['attempts']


def device_lease(verified_identity):
    if not verified_identity or verified_identity.lower().startswith(('com','/dev/')):
        raise ValueError('设备锁需要已核验身份，不能用串口名替代')
    return Lease(Path(tempfile.gettempdir())/'whale-verified-device-locks',verified_identity.lower())


class TaskRunner:
    """Wait in a worker, retain complete local logs, return bounded summaries."""
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.jobs = self.root/'local-only/manager/jobs'
        self.jobs.mkdir(parents=True, exist_ok=True)
        self.lock_dir = self.root/'local-only/manager/locks'
        self.active = {}
        self.completed = {}
        self.mutex = threading.Lock()

    def list(self):
        values = [read_json(path) for path in self.jobs.glob('*/task.json')]
        merged={value['id']:value for value in values}
        merged.update(self.completed)
        values=list(merged.values())
        for value in values:
            if value['status'] in ('queued', 'running') and value['id'] not in self.active:
                value.update(recorded_status=value['status'], status='unconfirmed',
                             reason='其他记录器留下的运行记录，结果待复核')
        return sorted(values, key=lambda x:x['created_at'], reverse=True)[:40]

    def get(self, identifier):
        if len(identifier) != 32 or any(c not in '0123456789abcdef' for c in identifier):
            raise ValueError('无效任务编号')
        return self.completed.get(identifier) or read_json(self.jobs/identifier/'task.json')

    def start(self, kind, command, *, timeout=300, cwd=None):
        identifier = uuid.uuid4().hex
        cancel = threading.Event()
        with self.mutex:
            if self.active:
                raise BusyError('已有任务运行，请等待完成或取消')
            self.active[identifier] = cancel
        directory = self.jobs/identifier
        directory.mkdir()
        record = dict(id=identifier, kind=kind, status='queued', created_at=now(),
                      started_at=None, ended_at=None, elapsed_seconds=None,
                      returncode=None, summary=[], owner_pid=os.getpid(),physical_device_checked=False,
                      firmware_written=False, log='local-only/manager/jobs/'+identifier+'/full.log')
        write_json(directory/'task.json', record)
        thread = threading.Thread(target=self._run, args=(record, command, timeout, cwd or self.root, cancel), daemon=True)
        thread.start()
        return record

    def cancel(self, identifier):
        record=self.get(identifier)
        if record is None or record['status'] not in ('queued','running'): return False
        if record['kind'].startswith('device-'): raise ValueError('资源写入任务不可中途取消，请等待校验和重启')
        with self.mutex:
            event = self.active.get(identifier)
            if event: event.set()
        (self.jobs/identifier/'cancel.request').touch()
        return True

    @staticmethod
    def _terminate(process):
        if process.poll() is not None: return
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        else:
            with contextlib.suppress(ProcessLookupError): os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    def _run(self, record, command, timeout, cwd, cancel):
        directory = self.jobs/record['id']
        started = time.monotonic()
        status = 'failed'
        process = None
        try:
            with Lease(self.lock_dir, str(self.root)):
                record.update(status='running', started_at=now())
                write_json(directory/'task.json', record)
                env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
                options = dict(creationflags=subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW) if os.name=='nt' else dict(start_new_session=True)
                with (directory/'full.log').open('wb') as output:
                    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                                               stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, **options)
                    while process.poll() is None:
                        if (cancel.wait(0.1) or (directory/'cancel.request').exists()) and not record['kind'].startswith('device-'):
                            status = 'cancelled'
                            self._terminate(process)
                            break
                        if time.monotonic()-started >= timeout:
                            status = 'timed_out'
                            self._terminate(process)
                            break
                    else:
                        status = 'succeeded' if process.returncode==0 else 'failed'
                record['returncode'] = process.returncode
        except BusyError as error:
            status = 'blocked'
            record['reason'] = str(error)
        except Exception as error:
            record['reason'] = (type(error).__name__+': '+str(error))[:500]
            if process is not None: self._terminate(process)
        finally:
            log = directory/'full.log'
            # Keep bounded memory even for a compiler emitting gigabytes of output.
            if log.exists():
                with log.open('rb') as stream:
                    stream.seek(max(0, log.stat().st_size-48000))
                    lines = stream.read().decode('utf-8', errors='replace').splitlines()
            else:
                lines=[]
            if record['kind'].startswith('device-'):
                operation=read_json(self.root/'local-only/manager/latest-device-operation.json',{})
                if operation.get('started_at','')>=record['started_at'] and 'device-'+operation.get('operation','')==record['kind']:
                    record['physical_device_checked']=operation.get('identity_partition_verified',False)
                    record['assets_written']=operation.get('assets_written',False)
                    record['device_operation_status']=operation.get('status')
                    record['physical_playback_confirmed']=False
            count = 20 if status=='succeeded' else 80
            important=[line for line in lines if re.match(r'^(Ran \d+ tests|OK\b|FAILED\b|ERROR:|FAIL:)',line)]
            if important:
                chosen=important[-8:]+lines[-max(0,count-min(8,len(important))):]
            else: chosen=lines[-count:]
            if record['kind']=='test':
                runs=re.findall(r'^Ran (\d+) tests? in ', '\n'.join(lines), re.M)
                outcomes=re.findall(r'^OK(?: \(skipped=(\d+)\))?$', '\n'.join(lines), re.M)
                if runs and outcomes:
                    total=int(runs[-1]);skipped=int(outcomes[-1] or 0)
                    record['test_counts']=dict(total=total,skipped=skipped,passed=total-skipped)
            record.update(status=status, ended_at=now(), elapsed_seconds=round(time.monotonic()-started,3),
                          summary=[line[:500] for line in chosen[-count:]])
            try:
                write_json(directory/'task.json', record)
            except OSError as error:
                record.update(status='failed',persistence_error=str(error)[:300])
            finally:
                with self.mutex:
                    self.completed[record['id']]=record
                    self.active.pop(record['id'], None)


def command_for(root, kind):
    root = Path(root).resolve()
    allowed = {
        'check': [sys.executable, str(root/'scripts/whale/doctor.py'), '--json'],
        'test': [sys.executable, str(root/'scripts/whale/run_host_tests.py')],
        'assets': [sys.executable, str(root/'scripts/whale/build_assets.py'), '--check'],
        'package': [sys.executable, str(root/'scripts/whale/resource_manager.py'), 'package'],
        'device-sync': [sys.executable, str(root/'scripts/whale/device_resources.py'), 'sync'],
        'device-rollback': [sys.executable, str(root/'scripts/whale/device_resources.py'), 'rollback'],
    }
    if kind in allowed: return allowed[kind]
    if kind=='build':
        machine = read_json(root/'local-only/machine.json', {})
        sdk = machine.get('esp_idf_path') or os.environ.get('IDF_PATH')
        if not sdk or not (Path(sdk)/'tools/idf.py').is_file():
            raise ValueError('先在ESP-IDF终端激活既有环境，或填写本机SDK配置')
        if any(ord(c)>127 for c in str(root)) and os.name=='nt':
            raise ValueError('当前Windows工程路径含非ASCII字符；请使用已对齐的英文路径工作树')
        return [sys.executable, str(root/'scripts/whale/build.py')]
    raise ValueError('未允许的任务类型；面板不接受任意命令或自动刷写')
