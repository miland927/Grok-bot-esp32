import http.client
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/whale'))
import doctor
import manager
import resource_manager as resources
from workflow_core import BusyError, ExperimentLedger, Lease, TaskRunner, command_for, device_lease, sha, write_json
import workflow_core


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        for name in ('whale/assets.bin','whale/library.json','whale/hardware-profile.json',
                     'partitions/whale-32m.csv','dependencies.lock'):
            dest=self.root/name;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(ROOT/name,dest)
        profile=json.loads((self.root/'whale/hardware-profile.json').read_text(encoding='utf-8'))
        for name in ('config.h','config.json'):
            dest=self.root/profile['board_directory']/name;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(ROOT/profile['board_directory']/name,dest)
        (self.root/'PROJECT_STATE.md').write_text('Fixture, no hardware',encoding='utf-8')
        scripts=self.root/'scripts/whale';scripts.mkdir(parents=True,exist_ok=True)
        for name in ('doctor.py','workflow_core.py','resource_manager.py'):
            shutil.copy2(ROOT/'scripts/whale'/name,scripts/name)
        self.store=resources.ResourceStore(self.root)
        self.original=(self.root/'whale/assets.bin').read_bytes()
        self.base=resources.unpack(self.original)
    def tearDown(self): self.temp.cleanup()
    def stage_replacement(self): return self.store.import_data(self.base['w031.gif'][0],'replacement.gif','w024')


class ResourceTests(Fixture):
    def test_pack_roundtrip_preserves_every_resource(self):
        self.assertEqual(resources.unpack(resources.pack(self.base)),self.base)
    def test_reject_corrupt_checksum(self):
        damaged=bytearray(self.original);damaged[-1]^=1
        with self.assertRaises(ValueError):resources.unpack(damaged)
    def test_actual_gif_structure_and_duration(self):
        info=resources.gif_info(self.base['w024.gif'][0])
        self.assertEqual((info['width'],info['height'],info['frames']),(160,160,80))
        self.assertGreaterEqual(info['duration_ms'],9900)
    def test_reject_truncated_gif(self):
        with self.assertRaises(ValueError):resources.gif_info(self.base['w024.gif'][0][:-8])
    def test_no_changes_transfer_zero(self):
        report=self.store.package()
        self.assertEqual(report['bytes_to_transfer'],0)
        self.assertEqual(report['sha256'],sha(self.original))
    def test_replacement_preserves_font_model_and_original(self):
        self.stage_replacement();report=self.store.package()
        self.assertFalse(report['engine_upgrade_required_for_registry'])
        output=resources.unpack((self.root/report['artifact']).read_bytes())
        self.assertEqual(output['w024.gif'],self.base['w031.gif'])
        self.assertEqual(output['srmodels.bin'],self.base['srmodels.bin'])
        self.assertEqual(output['font_noto_sans_common_30_4.bin'],self.base['font_noto_sans_common_30_4.bin'])
        self.assertEqual((self.root/'whale/assets.bin').read_bytes(),self.original)
        self.assertFalse(report['firmware_compiled'])
    def test_new_action_requires_engine_and_is_reproducible(self):
        self.store.import_data(self.base['w031.gif'][0],'new.gif')
        first=self.store.package();second=self.store.package()
        self.assertTrue(first['engine_upgrade_required_for_registry'])
        self.assertEqual(first['sha256'],second['sha256'])
        self.assertEqual(len(first['new_action_ids']),1)
    def test_rule_edit_requires_engine(self):
        self.store.edit('w024',dict(label='新的名称',trigger='happy',cooldown_ms=3000))
        self.assertTrue(self.store.package()['engine_upgrade_required_for_registry'])
    def test_unknown_rule_rejected(self):
        with self.assertRaises(ValueError):self.store.edit('w024',dict(trigger='eval'))
        with self.assertRaises(ValueError):self.store.edit('../secret',{})
    def test_bad_staged_hash_rejected(self):
        entry=self.stage_replacement()
        (self.store.workspace/'media'/(entry['sha256']+'.gif')).write_bytes(b'corrupt')
        with self.assertRaises(ValueError):self.store.package()
    def test_capacity_rejected_without_changing_original(self):
        self.stage_replacement()
        profile=json.loads((self.root/'whale/hardware-profile.json').read_text(encoding='utf-8'))
        profile['allowed_write_partitions']['assets']['size']='0x1000'
        write_json(self.root/'whale/hardware-profile.json',profile)
        with self.assertRaises(ValueError):self.store.package()
        self.assertEqual((self.root/'whale/assets.bin').read_bytes(),self.original)
    def test_clear_draft_restores_base_view(self):
        self.store.import_data(self.base['w031.gif'][0],'new.gif')
        self.store.clear_draft()
        self.assertEqual(len(self.store.actions()),10)
    @unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'),'FFmpeg unavailable')
    def test_webm_conversion_reuses_cache(self):
        source=next(p for p in (ROOT/'whale/source').glob('*.webm') if p.stem=='待机呼吸休闲').read_bytes()
        original_run=subprocess.run
        with mock.patch.object(resources.subprocess,'run',wraps=original_run) as run:
            first=self.store.import_data(source,'test.webm','w024')
            second=self.store.import_data(source,'test.webm','w024')
            conversions=[call for call in run.call_args_list if '-filter_complex' in call.args[0]]
        self.assertEqual(len(conversions),1)
        self.assertEqual(first['sha256'],second['sha256'])
        self.assertFalse(first['conversion_cache_hit'])
        self.assertTrue(second['conversion_cache_hit'])


class RunnerTests(Fixture):
    def await_job(self,runner,job):
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            value=runner.get(job['id'])
            if value['status'] not in ('queued','running'):return value
            time.sleep(.05)
        self.fail('Worker did not finish')
    def test_success_summary_is_bounded(self):
        runner=TaskRunner(self.root)
        job=runner.start('test',[sys.executable,'-c','print("line\\n"*100)'])
        result=self.await_job(runner,job)
        self.assertEqual(result['status'],'succeeded')
        self.assertLessEqual(len(result['summary']),20)
        self.assertGreater(len((self.root/result['log']).read_text()),100)
    def test_failure_summary_is_bounded(self):
        runner=TaskRunner(self.root)
        job=runner.start('test',[sys.executable,'-c','print("failure\\n"*150);raise SystemExit(3)'])
        result=self.await_job(runner,job)
        self.assertEqual(result['status'],'failed')
        self.assertLessEqual(len(result['summary']),80)
    def test_test_outcome_survives_noisy_tail(self):
        runner=TaskRunner(self.root)
        job=runner.start('test',[sys.executable,'-c','print("Ran 3 tests in 0.1s\\nOK (skipped=1)");print("fixture output\\n"*100)'])
        result=self.await_job(runner,job)
        self.assertEqual(result['test_counts'],dict(total=3,skipped=1,passed=2))
        self.assertTrue(any(line.startswith('Ran 3') for line in result['summary']))
    def test_timeout_is_not_success(self):
        runner=TaskRunner(self.root)
        job=runner.start('test',[sys.executable,'-c','import time;time.sleep(20)'],timeout=.3)
        self.assertEqual(self.await_job(runner,job)['status'],'timed_out')
    def test_final_result_write_failure_does_not_hang_waiter(self):
        runner=TaskRunner(self.root)
        original=workflow_core.write_json
        def failing_final(path,value):
            if value.get('status')=='succeeded':raise PermissionError('simulated final-result contention')
            return original(path,value)
        with mock.patch.object(workflow_core,'write_json',side_effect=failing_final):
            job=runner.start('test',[sys.executable,'-c','print("finished")'])
            result=self.await_job(runner,job)
        self.assertEqual(result['status'],'failed')
        self.assertIn('persistence_error',result)
        self.assertEqual(runner.list()[0]['status'],'failed')
    def test_cancel_does_not_pass(self):
        runner=TaskRunner(self.root)
        job=runner.start('test',[sys.executable,'-c','import time;time.sleep(20)'])
        runner.cancel(job['id'])
        self.assertEqual(self.await_job(runner,job)['status'],'cancelled')
    def test_root_mutex_blocks_second_process(self):
        module=str(ROOT/'scripts/whale')
        command=[sys.executable,'-c',f'import sys;sys.path.insert(0,{module!r});from workflow_core import Lease;import time;\nwith Lease({str(self.root)!r},"device"): print("ready",flush=True);time.sleep(1)']
        child=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(),'ready')
            with self.assertRaises(BusyError):
                with Lease(self.root,'device'):pass
        finally:
            child.communicate(timeout=5)
        with Lease(self.root,'device'):pass
    def test_budget_persists_across_instances(self):
        self.assertEqual(ExperimentLedger(self.root).reserve_write('same-fault'),1)
        self.assertEqual(ExperimentLedger(self.root).reserve_write('same-fault'),2)
        with self.assertRaises(ValueError):ExperimentLedger(self.root).reserve_write('same-fault')
    def test_port_is_not_device_identity(self):
        with self.assertRaises(ValueError):device_lease('COM3')
    def test_arbitrary_commands_rejected(self):
        with self.assertRaises(ValueError):command_for(self.root,'flash')
        with self.assertRaises(ValueError):command_for(self.root,'test;whoami')
    def test_optional_wake_config_not_required(self):
        result=doctor.inspect(self.root)
        self.assertEqual(result['errors'],[])
        self.assertFalse(result['physical_device_checked'])


class HttpTests(Fixture):
    def setUp(self):
        super().setUp()
        self.app=manager.Application(self.root)
        self.server=manager.ThreadingHTTPServer(('127.0.0.1',0),manager.handler_for(self.app))
        self.server.daemon_threads=True
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();super().tearDown()
    def request(self,path,method='GET',body=None,auth=True,extra=None):
        connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=10)
        headers={'X-Whale-Token':self.app.token} if auth else {}
        headers.update(extra or {})
        connection.request(method,path,body=body,headers=headers)
        response=connection.getresponse();result=(response.status,response.read(),dict(response.getheaders()))
        connection.close();return result
    def test_auth_and_cross_origin(self):
        self.assertEqual(self.request('/api/state',auth=False)[0],403)
        self.assertEqual(self.request('/api/tasks/start','POST',json.dumps({'kind':'check'}),extra={'Origin':'https://example.invalid'})[0],403)
        self.assertEqual(self.request('/api/state',extra={'Host':'evil.example'})[0],403)
        self.assertEqual(self.request('/bootstrap.js',extra={'Sec-Fetch-Site':'cross-site'})[0],403)
        self.assertEqual(self.request('/api/tasks/start','POST','[]')[0],400)
    def test_page_and_state_show_unknown_hardware(self):
        status,body,headers=self.request('/')
        self.assertEqual(status,200);self.assertIn('SameSite=Strict',headers['Set-Cookie'])
        result=json.loads(self.request('/api/state')[1])
        self.assertFalse(result['physical_device_checked'])
        self.assertIsNone(result['metrics']['cpu'])
    def test_traversal_does_not_read_file(self):
        self.assertEqual(self.request('/../PROJECT_STATE.md')[0],404)
        self.assertEqual(self.request('/api/tasks/../secret')[0],400)
    def test_import_and_media_cookie(self):
        status,body,_=self.request('/api/actions/import','POST',self.base['w031.gif'][0],extra={'X-File-Name':'new.gif'})
        self.assertEqual(status,200)
        entry=json.loads(body)
        path='/media/staged/'+entry['sha256']+'.gif'
        self.assertEqual(self.request(path)[0],403)
        self.assertEqual(self.request(path,extra={'Cookie':'whale_session='+self.app.token})[0],200)
    def test_device_settings_and_operation_input_are_validated(self):
        self.assertEqual(self.request('/api/device/start','POST',json.dumps({'operation':'sync'}))[0],400)
        self.assertEqual(self.request('/api/device/settings','POST',json.dumps({'port':'COM3','identity':'bad'}))[0],400)
        value=dict(port='COM3',identity='aa:bb:cc:dd:ee:ff',tools_directory='',command='do not execute')
        self.assertEqual(self.request('/api/device/settings','POST',json.dumps(value))[0],200)
        saved=json.loads((self.store.workspace/'device.json').read_text(encoding='utf-8'))
        self.assertNotIn('command',saved)
        self.assertEqual(self.request('/api/device/start','POST',json.dumps({'operation':'erase'}))[0],400)
        self.assertEqual(self.request('/api/device/start','POST',json.dumps({'operation':'install','sha256':'bad'}))[0],400)

    def test_authenticated_device_dispatch_has_fixed_command(self):
        write_json(self.store.workspace/'device.json',dict(port='COM3',identity='aa:bb:cc:dd:ee:ff'))
        with mock.patch.object(self.app.runner,'start',return_value={'id':'test'}) as start:
            self.assertEqual(self.request('/api/device/start','POST',json.dumps({'operation':'install','sha256':'a'*64}))[0],202)
            args=start.call_args.args
            self.assertEqual(args[0],'device-install')
            self.assertEqual(args[1][-3:],['install','--sha256','a'*64])

    def test_log_endpoint_is_authenticated_bounded_and_safe(self):
        identifier='a'*32
        directory=self.app.runner.jobs/identifier;directory.mkdir()
        write_json(directory/'task.json',dict(id=identifier,kind='check',status='succeeded',log='local-only/log'))
        (directory/'full.log').write_bytes(b'x'*60000+b'END')
        self.assertEqual(self.request('/api/logs/'+identifier,auth=False)[0],403)
        status,body,_=self.request('/api/logs/'+identifier)
        self.assertEqual(status,200)
        result=json.loads(body)
        self.assertEqual(len(result['text']),48000)
        self.assertTrue(result['text'].endswith('END'))
        self.assertEqual(self.request('/api/logs/../secret')[0],400)

    def test_device_tasks_cannot_be_cancelled_mid_flash(self):
        identifier='b'*32
        write_json(self.app.runner.jobs/identifier/'task.json',dict(id=identifier,kind='device-install',status='running'))
        self.assertEqual(self.request('/api/tasks/cancel','POST',json.dumps({'id':identifier}))[0],400)

    def test_flash_endpoint_not_available(self):
        self.assertEqual(self.request('/api/tasks/start','POST',json.dumps({'kind':'flash'}))[0],400)
    def test_stale_candidate_download_blocked(self):
        self.stage_replacement();self.store.package();self.store.clear_draft()
        self.assertEqual(self.request('/api/package/download')[0],409)


if __name__=='__main__':unittest.main()
