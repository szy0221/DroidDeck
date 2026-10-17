"""A Mali board has no DRM node, so the Steam tile lands on the Plasma desktop instead."""
import re
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

BIN = Path(__file__).resolve().parents[1] / 'linuxfs/overlay/usr/local/bin'
SESSION = BIN / 'droiddeck-session'


def queue_function():
    text = SESSION.read_text()
    match = re.search(r'^queue_steam_on_desktop\(\) \{.*?^\}$', text, re.M | re.S)
    assert match, 'droiddeck-session lost queue_steam_on_desktop'
    return match.group(0)


class MaliSteamRouteTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home'
        self.home.mkdir()
        self.runtime = self.root / 'xdg'
        self.runtime.mkdir()

    def run_queue(self):
        script = 'HOME=%s\nmkdir -p "$HOME"\nexport HOME XDG_RUNTIME_DIR=%s\n%s\nqueue_steam_on_desktop\n' % (
            self.home, self.runtime, queue_function())
        return subprocess.run(['bash', '-c', script], text=True, capture_output=True)

    def queued(self):
        return self.home / '.cache/droiddeck/run/steam.sh'

    def test_the_desktop_branch_only_queues_when_it_is_asked_to(self):
        text = SESSION.read_text()
        self.assertIn('BL_STEAM_AFTER_DESKTOP', text)
        guard = re.search(r'^ +\[ "\$\{BL_STEAM_AFTER_DESKTOP:-?\}" = 1 \] && queue_steam_on_desktop$',
                          text, re.M)
        self.assertTrue(guard, 'the desktop branch must gate the Steam queue on BL_STEAM_AFTER_DESKTOP')

    def test_the_queue_arms_the_desktop_runner(self):
        self.run_queue()
        self.assertTrue((self.home / '.droiddeck-remote-run').is_file())
        self.assertTrue(self.queued().is_file())
        body = self.queued().read_text()
        self.assertIn('/usr/local/bin/droiddeck-steam-launch desktop', body)
        self.assertIn('wayland-1', body)
        self.assertEqual(subprocess.run(['bash', '-n', str(self.queued())]).returncode, 0)

    def test_the_queued_client_waits_for_the_desktop_and_then_starts_it(self):
        self.run_queue()
        # droiddeck-desktop runs these with WAYLAND_DISPLAY=wayland-1 DISPLAY=:1
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(self.runtime / 'wayland-1'))
        self.addCleanup(listener.close)
        seen = self.root / 'seen'
        fake = self.root / 'launch'
        fake.write_text('#!/bin/bash\nprintf "%s\\n" "$BL_DESKTOP" "$@" > "' + str(seen) + '"\nexit 0\n')
        fake.chmod(0o755)
        body = self.queued().read_text().replace('/usr/local/bin/droiddeck-steam-launch', str(fake))
        body = re.sub(r'sleep [0-9]+', 'sleep 0', body)
        result = subprocess.run(['bash', '-c', body], text=True, capture_output=True,
                                env={'XDG_RUNTIME_DIR': str(self.runtime), 'HOME': str(self.home)})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(seen.read_text().split(), ['1', 'desktop'])

    def test_the_queued_client_gives_up_loudly_when_it_never_starts(self):
        self.run_queue()
        body = re.sub(r'sleep [0-9]+', 'sleep 0', self.queued().read_text())
        result = subprocess.run(['bash', '-c', body], text=True, capture_output=True,
                                env={'XDG_RUNTIME_DIR': str(self.runtime), 'HOME': str(self.home)})
        self.assertEqual(result.returncode, 69, result.stdout)
        self.assertIn('never came up', result.stderr)


if __name__ == '__main__':
    unittest.main()
