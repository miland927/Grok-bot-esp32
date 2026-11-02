"""Run upstream tests with Windows-safe temporary-directory cleanup.

Assertions are unchanged. Windows cannot delete its process working directory,
whereas a few upstream tests leave chdir() cleanup until after TemporaryDirectory.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

root = pathlib.Path(__file__).resolve().parents[2]
os.chdir(root)
original = tempfile.TemporaryDirectory

class WindowsTemporaryDirectory(original):
    def __init__(self, *args, **kwargs):
        self.return_dir = os.getcwd()
        super().__init__(*args, **kwargs)

    def cleanup(self):
        if pathlib.Path.cwd().is_relative_to(pathlib.Path(self.name)):
            os.chdir(self.return_dir)
        super().cleanup()

tempfile.TemporaryDirectory = WindowsTemporaryDirectory
original_run = subprocess.run

def windows_bash_run(command, *args, **kwargs):
    # Git Bash expects POSIX paths in shell variables. Keep all test assertions
    # and the upstream workflow script unchanged.
    if isinstance(command, list) and command and command[0] == 'bash' and 'env' in kwargs:
        env = dict(kwargs['env'])
        for key in ('GITHUB_WORKSPACE', 'GITHUB_OUTPUT', 'RUNNER_TEMP', 'GIT_CONFIG_GLOBAL'):
            if key in env:
                drive, tail = os.path.splitdrive(env[key])
                if drive:
                    env[key] = '/' + drive[0].lower() + tail.replace('\\', '/')
        kwargs['env'] = env
    try:
        return original_run(command, *args, **kwargs)
    except subprocess.CalledProcessError as exc:
        if exc.stderr:
            print(exc.stderr, file=sys.stderr)
        raise

subprocess.run = windows_bash_run
suite = unittest.defaultTestLoader.discover(str(root/'scripts/tests'))
if os.name == 'nt':
    import test_ci_selection
    unittest.skip('Upstream Linux CI shell integration; native CI-selection unit tests still run')(
        test_ci_selection.WorkflowDiffTests
    )
if shutil.which(os.environ.get('CXX', 'c++')) is None:
    import test_ogg_demuxer
    test_ogg_demuxer.OggDemuxerTest.test_zero_length_lace_is_processed_without_more_input = unittest.skip(
        'No host C++ compiler; this unchanged Ogg-demuxer test is not part of the board-only UI change'
    )(test_ogg_demuxer.OggDemuxerTest.test_zero_length_lace_is_processed_without_more_input)
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(not result.wasSuccessful())
