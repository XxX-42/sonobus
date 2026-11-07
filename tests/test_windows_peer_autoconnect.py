import importlib.util
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / 'scripts/windows-peer-autoconnect.py'
spec = importlib.util.spec_from_file_location('peer', script)
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)

class PeerRepairTests(unittest.TestCase):
    def test_new_port_and_address_reload(self):
        with tempfile.TemporaryDirectory() as folder:
            agent = Path(folder) / 'agent.plist'
            config = {'ProgramArguments': ['/Applications/SonoBusHeadless.app/Contents/MacOS/SonoBus', '-q', '-d', '192.168.1.6:51440']}
            agent.write_bytes(plistlib.dumps(config))
            success = subprocess.CompletedProcess([], 0)
            with patch.object(peer, 'AGENT', agent), patch.object(peer, 'discover', return_value='192.168.1.8'), \
                 patch.object(peer, 'windows_port_and_watchdog', return_value='51443'), \
                 patch.object(peer.subprocess, 'run', return_value=success) as run:
                peer.run()
            self.assertEqual(plistlib.loads(agent.read_bytes())['ProgramArguments'][3], '192.168.1.8:51443')
            self.assertEqual(run.call_count, 2)

    def test_unverified_port_does_not_modify(self):
        with tempfile.TemporaryDirectory() as folder:
            agent = Path(folder) / 'agent.plist'
            config = {'ProgramArguments': ['/Applications/SonoBusHeadless.app/Contents/MacOS/SonoBus', '-q', '-d', '192.168.1.8:51441']}
            agent.write_bytes(plistlib.dumps(config))
            with patch.object(peer, 'AGENT', agent), patch.object(peer, 'discover', return_value='192.168.1.8'), \
                 patch.object(peer, 'windows_port_and_watchdog', return_value=None), \
                 patch.object(peer.subprocess, 'run') as run:
                peer.run()
            self.assertEqual(plistlib.loads(agent.read_bytes()), config)
            run.assert_not_called()

    def test_retry_bootstrap_after_transient_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            agent = Path(folder) / 'agent.plist'
            config = {'ProgramArguments': ['/Applications/SonoBusHeadless.app/Contents/MacOS/SonoBus', '-q', '-d', '192.168.1.8:51441']}
            agent.write_bytes(plistlib.dumps(config))
            with patch.object(peer, 'AGENT', agent), patch.object(peer, 'discover', return_value='192.168.1.8'), \
                 patch.object(peer, 'windows_port_and_watchdog', return_value='51442'), patch.object(peer.time, 'sleep'), \
                 patch.object(peer.subprocess, 'run', side_effect=[subprocess.CompletedProcess([], 0), subprocess.CompletedProcess([], 5), subprocess.CompletedProcess([], 0)]) as run:
                peer.run()
            self.assertEqual(plistlib.loads(agent.read_bytes())['ProgramArguments'][3], '192.168.1.8:51442')
            self.assertEqual(run.call_count, 3)

if __name__ == '__main__':
    unittest.main()
