"""A Mali board has no DRM node, so the Steam tile lands on the Plasma desktop instead."""
import os
import re
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path

BIN = Path(__file__).resolve().parents[1] / 'linuxfs/overlay/usr/local/bin'
SESSION = BIN / 'droiddeck-session'

PROBE_LINE = 'probe=$(command -v xprop || command -v xdpyinfo || command -v xset)'
# On Windows a bare "bash" resolves to the WSL shim, so let a runner point at a real one.
BASH = os.environ.get('DROIDDECK_TEST_BASH', 'bash')


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
        self.x11 = self.root / 'x11'
        self.x11.mkdir()
        # On CI there is no Plasma to ask, so stand in for it: a pgrep that names a
        # process whose environ is unreadable, which is the same as "not running here".
        pgrep = self.root / 'pgrep'
        pgrep.write_text('#!/bin/bash\necho 999999\n')
        pgrep.chmod(0o755)
        self.path = str(self.root) + os.pathsep + os.environ.get('PATH', '')

    def run_queue(self):
        script = 'HOME=%s\nmkdir -p "$HOME"\nexport HOME XDG_RUNTIME_DIR=%s\n%s\nqueue_steam_on_desktop\n' % (
            self.home, self.runtime, queue_function())
        return subprocess.run([BASH, '-c', script], text=True, capture_output=True)

    def queued(self, x11=None):
        """The script as the desktop would run it, pointing at a scratch X socket dir."""
        body = self.queued_text()
        return body.replace('/tmp/.X11-unix', str(x11 or self.x11))

    def queued_text(self):
        return (self.home / '.cache/droiddeck/run/steam.sh').read_text()

    def run_queued(self, body, **env):
        return subprocess.run([BASH, '-c', body], text=True, capture_output=True,
                              env=dict(env, HOME=str(self.home), PATH=self.path))

    def launch_stub(self, fields):
        seen = self.root / 'seen'
        fake = self.root / 'launch'
        fake.write_text('#!/bin/bash\nprintf "%s\\n" '
                        + ' '.join('"$%s"' % f for f in fields)
                        + ' > "' + str(seen) + '"\nexit 0\n')
        fake.chmod(0o755)
        return seen, fake

    def test_the_desktop_branch_only_queues_when_it_is_asked_to(self):
        text = SESSION.read_text()
        self.assertIn('BL_STEAM_AFTER_DESKTOP', text)
        guard = re.search(r'^ +\[ "\$\{BL_STEAM_AFTER_DESKTOP:-?\}" = 1 \] && queue_steam_on_desktop$',
                          text, re.M)
        self.assertTrue(guard, 'the desktop branch must gate the Steam queue on BL_STEAM_AFTER_DESKTOP')

    def test_the_queue_arms_the_desktop_runner(self):
        self.run_queue()
        self.assertTrue((self.home / '.droiddeck-remote-run').is_file())
        self.assertTrue((self.home / '.cache/droiddeck/run/steam.sh').is_file())
        body = self.queued_text()
        self.assertIn('/usr/local/bin/droiddeck-steam-launch desktop', body)
        self.assertEqual(subprocess.run([BASH, '-n', str(self.home / '.cache/droiddeck/run/steam.sh')]).returncode, 0)

    def test_it_reads_the_compositor_off_the_filesystem(self):
        """gamescope is not there, so wayland-1/:1 are not where Plasma and Xwayland land."""
        self.run_queue()
        body = self.queued_text()
        self.assertIn('wayland-*', body)
        self.assertIn('/tmp/.X11-unix/X*', body)
        self.assertIn('w=${wayland##*/}', body)
        self.assertIn('d=:${x11##*X}', body)
        self.assertIn('sleep 1', body)

    def test_it_prefers_the_display_settings_of_the_running_session(self):
        """The runner's environment predates Plasma, so it has no X cookie and no session bus."""
        self.run_queue()
        body = self.queued_text()
        self.assertIn('kwin_wayland|startplasma-wayland|plasmashell', body)
        self.assertIn('/proc/$pid/environ', body)
        self.assertIn('XAUTHORITY', body)
        self.assertIn('DBUS_SESSION_BUS_ADDRESS', body)

    def test_it_overrides_the_runner_pair_and_starts_the_client(self):
        self.run_queue()
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(self.runtime / 'wayland-2'))
        self.addCleanup(listener.close)
        (self.x11 / 'X3').write_text('')
        seen, fake = self.launch_stub(['BL_DESKTOP', 'WAYLAND_DISPLAY', 'DISPLAY', '*'])
        body = self.queued().replace('/usr/local/bin/droiddeck-steam-launch', str(fake))
        body = re.sub(r'sleep [0-9]+', 'sleep 0', body)
        body = body.replace(PROBE_LINE, 'probe=')
        # the desktop runner hands over the gamescope pair; it has to be corrected
        result = self.run_queued(body, WAYLAND_DISPLAY='wayland-1', DISPLAY=':1',
                                 XDG_RUNTIME_DIR=str(self.runtime))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(seen.read_text().split(), ['1', 'wayland-2', ':3', 'desktop'])

    def test_it_picks_up_the_x_cookie_the_runner_does_not_have(self):
        """Without one the client dies in XOpenDisplay before it ever shows a window."""
        self.run_queue()
        cookie = self.runtime / 'xauth-Ab12'
        cookie.write_text('')
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(self.runtime / 'wayland-2'))
        self.addCleanup(listener.close)
        (self.x11 / 'X3').write_text('')
        seen, fake = self.launch_stub(['BL_DESKTOP', 'WAYLAND_DISPLAY', 'DISPLAY', 'XAUTHORITY'])
        probe = self.root / 'probe'
        probe.write_text('#!/bin/bash\n[ "${XAUTHORITY:-}" = "%s" ]\n' % cookie)
        probe.chmod(0o755)
        body = self.queued().replace('/usr/local/bin/droiddeck-steam-launch', str(fake))
        body = re.sub(r'sleep [0-9]+', 'sleep 0', body)
        body = body.replace(PROBE_LINE, 'probe=%s' % probe)
        result = self.run_queued(body, WAYLAND_DISPLAY='wayland-1', DISPLAY=':1',
                                 XDG_RUNTIME_DIR=str(self.runtime))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(seen.read_text().split(), ['1', 'wayland-2', ':3', str(cookie)])

    def test_it_gives_up_loudly_when_the_client_never_starts(self):
        self.run_queue()
        body = re.sub(r'sleep [0-9]+', 'sleep 0', self.queued())
        body = body.replace(PROBE_LINE, 'probe=')
        result = self.run_queued(body, XDG_RUNTIME_DIR=str(self.runtime))
        self.assertEqual(result.returncode, 69, result.stdout)
        self.assertIn('never came up', result.stderr)


if __name__ == '__main__':
    unittest.main()
