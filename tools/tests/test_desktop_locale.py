"""Plasma cannot set its own language here, so the session carries the app's over."""
import os
import re
import subprocess
import unittest
from pathlib import Path

BIN = Path(__file__).resolve().parents[1] / 'linuxfs/overlay/usr/local/bin'
SESSION = BIN / 'droiddeck-session'
BASH = os.environ.get('DROIDDECK_TEST_BASH', 'bash')


def locale_function():
    text = SESSION.read_text()
    match = re.search(r'^desktop_locale\(\) \{.*?^\}$', text, re.M | re.S)
    assert match, 'droiddeck-session lost desktop_locale'
    return match.group(0)


class DesktopLocaleTest(unittest.TestCase):
    def run_locale(self, **env):
        script = '%s\ndesktop_locale\necho "LANG=${LANG:-unset} LANGUAGE=${LANGUAGE:-unset}"\n' % locale_function()
        return subprocess.run([BASH, '-c', script], text=True, capture_output=True,
                              env=dict(env, HOME='/nonexistent-home'))

    def test_the_desktop_branch_applies_it(self):
        self.assertIn('desktop_locale', SESSION.read_text())

    def test_the_steam_language_becomes_a_posix_locale(self):
        result = self.run_locale(BL_STEAM_LANGUAGE='schinese')
        self.assertIn('steam=schinese -> zh_CN', result.stdout)
        self.assertIn('LANGUAGE=zh_CN', result.stdout)

    def test_it_keeps_the_environment_when_nothing_was_asked_for(self):
        result = self.run_locale()
        self.assertIn('-> unset', result.stdout)
        self.assertIn('LANGUAGE=unset', result.stdout)

    def test_it_reports_what_the_runtime_can_actually_do(self):
        """Whether Plasma ends up translated depends on files the rootfs may not ship."""
        result = self.run_locale(BL_STEAM_LANGUAGE='schinese')
        self.assertIn('generated=', result.stdout)
        self.assertIn('zh_CN=', result.stdout)


if __name__ == '__main__':
    unittest.main()
