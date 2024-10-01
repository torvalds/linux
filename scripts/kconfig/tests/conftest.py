# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2018 Masahiro Yamada <yamada.masahiro@socionext.com>
#

"""
Kconfig unit testing framework.

This provides fixture functions commonly used from test files.
"""

import os
import pytest
import shutil
import subprocess
import tempfile

CONF_PATH = os.path.abspath(os.path.join('scripts', 'kconfig', 'conf'))


class Conf:
    """Kconfig runner and result checker.

    This class provides methods to run text-based interface of Kconfig
    (scripts/kconfig/conf) and retrieve the resulted configuration,
    stdout, and stderr.  It also provides methods to compare those
    results with expectations.
    """

    def __init__(self, request):
        """Create a new Conf instance.

        request: object to introspect the requesting test module
        """
        # the directory of the test being run
        self._test_dir = os.path.dirname(str(request.fspath))

    # runners
    def _run_conf(self, mode, dot_config=None, out_file='.config',
                  interactive=False, in_keys=None, extra_env={}):
        """Run text-based Kconfig executable and save the result.

        mode: input mode option (--oldaskconfig, --defconfig=<file> etc.)
        dot_config: .config file to use for configuration base
        out_file: file name to contain the output config data
        interactive: flag to specify the interactive mode
        in_keys: key inputs for interactive modes
        extra_env: additional environments
        returncode: exit status of the Kconfig executable
        """
        command = [CONF_PATH, mode, 'Kconfig']

        # Override 'srctree' environment to make the test as the top directory
        extra_env['srctree'] = self._test_dir

        # Clear KCONFIG_DEFCONFIG_LIST to keep unit tests from being affected
        # by the user's environment.
        extra_env['KCONFIG_DEFCONFIG_LIST'] = ''

        # Run Kconfig in a temporary directory.
        # This directory is automatically removed when done.
        with tempfile.TemporaryDirectory() as temp_dir:

            # if .config is given, copy it to the working directory
            if dot_config:
                shutil.copyfile(os.path.join(self._test_dir, dot_config),
                                os.path.join(temp_dir, '.config'))

            ps = subprocess.Popen(command,
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE,
                                  cwd=temp_dir,
                                  env=dict(os.environ, **extra_env))

            # If input key sequence is given, feed it to stdin.
            if in_keys:
                ps.stdin.write(in_keys.encode('utf-8'))

            while ps.poll() is None:
                # For interactive modes such as oldaskconfig, oldconfig,
                # send 'Enter' key until the program finishes.
                if interactive:
                    ps.stdin.write(b'\n')

            self.retcode = ps.returncode
            self.stdout = ps.stdout.read().decode()
            self.stderr = ps.stderr.read().decode()

            # Retrieve the resulted config data only when .config is supposed
            # to exist.  If the command fails, the .config does not exist.
            # 'listnewconfig' does not produce .config in the first place.
            if self.retcode == 0 and out_file:
                with open(os.path.join(temp_dir, out_file)) as f:
                    self.config = f.read()
            else:
                self.config = None

        # Logging:
        # Pytest captures the following information by default.  In failure
        # of tests, the captured log will be displayed.  This will be useful to
        # figure out what has happened.

        print("[command]\n{}\n".format(' '.join(command)))

        print("[retcode]\n{}\n".format(self.retcode))

        print("[stdout]")
        print(self.stdout)

        print("[stderr]")
        print(self.stderr)

        if self.config is not None:
            print("[output for '{}']".format(out_file))
            print(self.config)

        return self.retcode

    def oldaskconfig(self, dot_config=None, in_keys=None):
        """Run oldaskconfig.

        dot_config: .config file to use for configuration base (optional)
        in_key: key inputs (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._run_conf('--oldaskconfig', dot_config=dot_config,
                              interactive=True, in_keys=in_keys)

    def oldconfig(self, dot_config=None, in_keys=None):
        """Run oldconfig.

        dot_config: .config file to use for configuration base (optional)
        in_key: key inputs (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._run_conf('--oldconfig', dot_config=dot_config,
                              interactive=True, in_keys=in_keys)

    def olddefconfig(self, dot_config=None):
        """Run olddefconfig.

        dot_config: .config file to use for configuration base (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._run_conf('--olddefconfig', dot_config=dot_config)

    def defconfig(self, defconfig):
        """Run defconfig.

        defconfig: defconfig file for input
        returncode: exit status of the Kconfig executable
        """
        defconfig_path = os.path.join(self._test_dir, defconfig)
        return self._run_conf('--defconfig={}'.format(defconfig_path))

    def _allconfig(self, mode, all_config, extra_env={}):
        if all_config:
            all_config_path = os.path.join(self._test_dir, all_config)
            extra_env['KCONFIG_ALLCONFIG'] = all_config_path

        return self._run_conf('--{}config'.format(mode), extra_env=extra_env)

    def allyesconfig(self, all_config=None):
        """Run allyesconfig.

        all_config: fragment config file for KCONFIG_ALLCONFIG (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._allconfig('allyes', all_config)

    def allmodconfig(self, all_config=None):
        """Run allmodconfig.

        all_config: fragment config file for KCONFIG_ALLCONFIG (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._allconfig('allmod', all_config)

    def allnoconfig(self, all_config=None):
        """Run allnoconfig.

        all_config: fragment config file for KCONFIG_ALLCONFIG (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._allconfig('allno', all_config)

    def alldefconfig(self, all_config=None):
        """Run alldefconfig.

        all_config: fragment config file for KCONFIG_ALLCONFIG (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._allconfig('alldef', all_config)

    def randconfig(self, all_config=None, seed=None):
        """Run randconfig.

        all_config: fragment config file for KCONFIG_ALLCONFIG (optional)
        seed: the seed for randconfig (optional)
        returncode: exit status of the Kconfig executable
        """
        if seed is not None:
            extra_env = {'KCONFIG_SEED': hex(seed)}
        else:
            extra_env = {}

        return self._allconfig('rand', all_config, extra_env=extra_env)

    def savedefconfig(self, dot_config):
        """Run savedefconfig.

        dot_config: .config file for input
        returncode: exit status of the Kconfig executable
        """
        return self._run_conf('--savedefconfig', out_file='defconfig')

    def listnewconfig(self, dot_config=None):
        """Run listnewconfig.

        dot_config: .config file to use for configuration base (optional)
        returncode: exit status of the Kconfig executable
        """
        return self._run_conf('--listnewconfig', dot_config=dot_config,
                              out_file=None)

    # checkers
    def _read_and_compare(self, compare, expected):
        """Compare the result with expectation.

        compare: function to compare the result with expectation
        expected: file that contains the expected data
        """
        with open(os.path.join(self._test_dir, expected)) as f:
            expected_data = f.read()
        return compare(self, expected_data)

    def _contains(self, attr, expected):
        return self._read_and_compare(
                                    lambda s, e: getattr(s, attr).find(e) >= 0,
                                    expected)

    def _matches(self, attr, expected):
        return self._read_and_compare(lambda s, e: getattr(s, attr) == e,
                                      expected)

    def config_contains(self, expected):
        """Check if resulted configuration contains expected data.

        expected: file that contains the expected data
        returncode: True if result contains the expected data, False otherwise
        """
        return self._contains('config', expected)

    def config_matches(self, expected):
        """Check if resulted configuration exactly matches expected data.

        expected: file that contains the expected data
        returncode: True if result matches the expected data, False otherwise
        """
        return self._matches('config', expected)

    def stdout_contains(self, expected):
        """Check if resulted stdout contains expected data.

        expected: file that contains the expected data
        returncode: True if result contains the expected data, False otherwise
        """
        return self._contains('stdout', expected)

    def stdout_matches(self, expected):
        """Check if resulted stdout exactly matches expected data.

        expected: file that contains the expected data
        returncode: True if result matches the expected data, False otherwise
        """
        return self._matches('stdout', expected)

    def stderr_contains(self, expected):
        """Check if resulted stderr contains expected data.

        expected: file that contains the expected data
        returncode: True if result contains the expected data, False otherwise
        """
        return self._contains('stderr', expected)

    def stderr_matches(self, expected):
        """Check if resulted stderr exactly matches expected data.

        expected: file that contains the expected data
        returncode: True if result matches the expected data, False otherwise
        """
        return self._matches('stderr', expected)


@pytest.fixture(scope="module")
def conf(request):
    """Create a Conf instance and provide it to test functions."""
    return Conf(request)
