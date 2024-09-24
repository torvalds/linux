#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Tests the `rust_is_available.sh` script.

Some of the tests require the real programs to be available in `$PATH`
under their canonical name (and with the expected versions).
"""

import enum
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

class TestRustIsAvailable(unittest.TestCase):
    @enum.unique
    class Expected(enum.Enum):
        SUCCESS = enum.auto()
        SUCCESS_WITH_WARNINGS = enum.auto()
        SUCCESS_WITH_EXTRA_OUTPUT = enum.auto()
        FAILURE = enum.auto()

    @classmethod
    def generate_executable(cls, content):
        path = pathlib.Path(cls.tempdir.name)
        name = str(len(tuple(path.iterdir())))
        path = path / name
        with open(path, "w") as file_:
            file_.write(content)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path

    @classmethod
    def generate_clang(cls, stdout):
        return cls.generate_executable(f"""#!/usr/bin/env python3
import sys
if "-E" in " ".join(sys.argv):
    print({repr("Clang " + " ".join(cls.llvm_default_version.split(" ")))})
else:
    print({repr(stdout)})
""")

    @classmethod
    def generate_rustc(cls, stdout):
        return cls.generate_executable(f"""#!/usr/bin/env python3
import sys
if "--print sysroot" in " ".join(sys.argv):
    print({repr(cls.rust_default_sysroot)})
else:
    print({repr(stdout)})
""")

    @classmethod
    def generate_bindgen(cls, version_stdout, libclang_stderr, version_0_66_patched=False):
        if libclang_stderr is None:
            libclang_case = f"raise SystemExit({cls.bindgen_default_bindgen_libclang_failure_exit_code})"
        else:
            libclang_case = f"print({repr(libclang_stderr)}, file=sys.stderr)"

        if version_0_66_patched:
            version_0_66_case = "pass"
        else:
            version_0_66_case = "raise SystemExit(1)"

        return cls.generate_executable(f"""#!/usr/bin/env python3
import sys
if "rust_is_available_bindgen_libclang.h" in " ".join(sys.argv):
    {libclang_case}
elif "rust_is_available_bindgen_0_66.h" in " ".join(sys.argv):
    {version_0_66_case}
else:
    print({repr(version_stdout)})
""")

    @classmethod
    def generate_bindgen_version(cls, stdout, version_0_66_patched=False):
        return cls.generate_bindgen(stdout, cls.bindgen_default_bindgen_libclang_stderr, version_0_66_patched)

    @classmethod
    def generate_bindgen_libclang_failure(cls):
        return cls.generate_bindgen(cls.bindgen_default_bindgen_version_stdout, None)

    @classmethod
    def generate_bindgen_libclang(cls, stderr):
        return cls.generate_bindgen(cls.bindgen_default_bindgen_version_stdout, stderr)

    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()

        cls.missing = pathlib.Path(cls.tempdir.name) / "missing"

        cls.nonexecutable = pathlib.Path(cls.tempdir.name) / "nonexecutable"
        with open(cls.nonexecutable, "w") as file_:
            file_.write("nonexecutable")

        cls.unexpected_binary = "true"

        cls.rustc_default_version = subprocess.check_output(("scripts/min-tool-version.sh", "rustc")).decode().strip()
        cls.bindgen_default_version = subprocess.check_output(("scripts/min-tool-version.sh", "bindgen")).decode().strip()
        cls.llvm_default_version = subprocess.check_output(("scripts/min-tool-version.sh", "llvm")).decode().strip()
        cls.rust_default_sysroot = subprocess.check_output(("rustc", "--print", "sysroot")).decode().strip()

        cls.bindgen_default_bindgen_version_stdout = f"bindgen {cls.bindgen_default_version}"
        cls.bindgen_default_bindgen_libclang_failure_exit_code = 42
        cls.bindgen_default_bindgen_libclang_stderr = f"scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {cls.llvm_default_version} [-W#pragma-messages], err: false"

        cls.default_rustc = cls.generate_rustc(f"rustc {cls.rustc_default_version}")
        cls.default_bindgen =  cls.generate_bindgen(cls.bindgen_default_bindgen_version_stdout, cls.bindgen_default_bindgen_libclang_stderr)
        cls.default_cc = cls.generate_clang(f"clang version {cls.llvm_default_version}")

    def run_script(self, expected, override_env):
        env = {
            "RUSTC": self.default_rustc,
            "BINDGEN": self.default_bindgen,
            "CC": self.default_cc,
        }

        for key, value in override_env.items():
            if value is None:
                del env[key]
                continue
            env[key] = value

        result = subprocess.run("scripts/rust_is_available.sh", env=env, capture_output=True)

        # The script should never output anything to `stdout`.
        self.assertEqual(result.stdout, b"")

        if expected == self.Expected.SUCCESS:
            # When expecting a success, the script should return 0
            # and it should not output anything to `stderr`.
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stderr, b"")
        elif expected == self.Expected.SUCCESS_WITH_EXTRA_OUTPUT:
            # When expecting a success with extra output (that is not warnings,
            # which is the common case), the script should return 0 and it
            # should output at least something to `stderr` (the output should
            # be checked further by the test).
            self.assertEqual(result.returncode, 0)
            self.assertNotEqual(result.stderr, b"")
        elif expected == self.Expected.SUCCESS_WITH_WARNINGS:
            # When expecting a success with warnings, the script should return 0
            # and it should output at least the instructions to `stderr`.
            self.assertEqual(result.returncode, 0)
            self.assertIn(b"Please see Documentation/rust/quick-start.rst for details", result.stderr)
        else:
            # When expecting a failure, the script should return non-0
            # and it should output at least the instructions to `stderr`.
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"Please see Documentation/rust/quick-start.rst for details", result.stderr)

        # The output will generally be UTF-8 (i.e. unless the user has
        # put strange values in the environment).
        result.stderr = result.stderr.decode()

        return result

    def test_rustc_unset(self):
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": None })
        self.assertIn("Environment variable 'RUSTC' is not set.", result.stderr)
        self.assertIn("This script is intended to be called from Kbuild.", result.stderr)

    def test_bindgen_unset(self):
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": None })
        self.assertIn("Environment variable 'BINDGEN' is not set.", result.stderr)
        self.assertIn("This script is intended to be called from Kbuild.", result.stderr)

    def test_cc_unset(self):
        result = self.run_script(self.Expected.FAILURE, { "CC": None })
        self.assertIn("Environment variable 'CC' is not set.", result.stderr)
        self.assertIn("This script is intended to be called from Kbuild.", result.stderr)

    def test_rustc_missing(self):
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": self.missing })
        self.assertIn(f"Rust compiler '{self.missing}' could not be found.", result.stderr)

    def test_bindgen_missing(self):
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": self.missing })
        self.assertIn(f"Rust bindings generator '{self.missing}' could not be found.", result.stderr)

    def test_rustc_nonexecutable(self):
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": self.nonexecutable })
        self.assertIn(f"Running '{self.nonexecutable}' to check the Rust compiler version failed with", result.stderr)

    def test_rustc_unexpected_binary(self):
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": self.unexpected_binary })
        self.assertIn(f"Running '{self.unexpected_binary}' to check the Rust compiler version did not return", result.stderr)

    def test_rustc_unexpected_name(self):
        rustc = self.generate_rustc(f"unexpected {self.rustc_default_version} (a8314ef7d 2022-06-27)")
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": rustc })
        self.assertIn(f"Running '{rustc}' to check the Rust compiler version did not return", result.stderr)

    def test_rustc_unexpected_version(self):
        rustc = self.generate_rustc("rustc unexpected (a8314ef7d 2022-06-27)")
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": rustc })
        self.assertIn(f"Running '{rustc}' to check the Rust compiler version did not return", result.stderr)

    def test_rustc_no_minor(self):
        rustc = self.generate_rustc(f"rustc {'.'.join(self.rustc_default_version.split('.')[:2])} (a8314ef7d 2022-06-27)")
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": rustc })
        self.assertIn(f"Running '{rustc}' to check the Rust compiler version did not return", result.stderr)

    def test_rustc_old_version(self):
        rustc = self.generate_rustc("rustc 1.60.0 (a8314ef7d 2022-06-27)")
        result = self.run_script(self.Expected.FAILURE, { "RUSTC": rustc })
        self.assertIn(f"Rust compiler '{rustc}' is too old.", result.stderr)

    def test_bindgen_nonexecutable(self):
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": self.nonexecutable })
        self.assertIn(f"Running '{self.nonexecutable}' to check the Rust bindings generator version failed with", result.stderr)

    def test_bindgen_unexpected_binary(self):
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": self.unexpected_binary })
        self.assertIn(f"Running '{self.unexpected_binary}' to check the bindings generator version did not return", result.stderr)

    def test_bindgen_unexpected_name(self):
        bindgen = self.generate_bindgen_version(f"unexpected {self.bindgen_default_version}")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Running '{bindgen}' to check the bindings generator version did not return", result.stderr)

    def test_bindgen_unexpected_version(self):
        bindgen = self.generate_bindgen_version("bindgen unexpected")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Running '{bindgen}' to check the bindings generator version did not return", result.stderr)

    def test_bindgen_no_minor(self):
        bindgen = self.generate_bindgen_version(f"bindgen {'.'.join(self.bindgen_default_version.split('.')[:2])}")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Running '{bindgen}' to check the bindings generator version did not return", result.stderr)

    def test_bindgen_old_version(self):
        bindgen = self.generate_bindgen_version("bindgen 0.50.0")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Rust bindings generator '{bindgen}' is too old.", result.stderr)

    def test_bindgen_bad_version_0_66_0_and_0_66_1(self):
        for version in ("0.66.0", "0.66.1"):
            with self.subTest(version=version):
                bindgen = self.generate_bindgen_version(f"bindgen {version}")
                result = self.run_script(self.Expected.SUCCESS_WITH_WARNINGS, { "BINDGEN": bindgen })
                self.assertIn(f"Rust bindings generator '{bindgen}' versions 0.66.0 and 0.66.1 may not", result.stderr)

    def test_bindgen_bad_version_0_66_0_and_0_66_1_patched(self):
        for version in ("0.66.0", "0.66.1"):
            with self.subTest(version=version):
                bindgen = self.generate_bindgen_version(f"bindgen {version}", True)
                result = self.run_script(self.Expected.SUCCESS, { "BINDGEN": bindgen })

    def test_bindgen_libclang_failure(self):
        bindgen = self.generate_bindgen_libclang_failure()
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Running '{bindgen}' to check the libclang version (used by the Rust", result.stderr)
        self.assertIn(f"bindings generator) failed with code {self.bindgen_default_bindgen_libclang_failure_exit_code}. This may be caused by", result.stderr)

    def test_bindgen_libclang_unexpected_version(self):
        bindgen = self.generate_bindgen_libclang("scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version unexpected [-W#pragma-messages], err: false")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"Running '{bindgen}' to check the libclang version (used by the Rust", result.stderr)
        self.assertIn("bindings generator) did not return an expected output. See output", result.stderr)

    def test_bindgen_libclang_old_version(self):
        bindgen = self.generate_bindgen_libclang("scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version 10.0.0 [-W#pragma-messages], err: false")
        result = self.run_script(self.Expected.FAILURE, { "BINDGEN": bindgen })
        self.assertIn(f"libclang (used by the Rust bindings generator '{bindgen}') is too old.", result.stderr)

    def test_clang_matches_bindgen_libclang_different_bindgen(self):
        bindgen = self.generate_bindgen_libclang("scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version 999.0.0 [-W#pragma-messages], err: false")
        result = self.run_script(self.Expected.SUCCESS_WITH_WARNINGS, { "BINDGEN": bindgen })
        self.assertIn("version does not match Clang's. This may be a problem.", result.stderr)

    def test_clang_matches_bindgen_libclang_different_clang(self):
        cc = self.generate_clang("clang version 999.0.0")
        result = self.run_script(self.Expected.SUCCESS_WITH_WARNINGS, { "CC": cc })
        self.assertIn("version does not match Clang's. This may be a problem.", result.stderr)

    def test_rustc_src_core_krustflags(self):
        result = self.run_script(self.Expected.FAILURE, { "PATH": os.environ["PATH"], "RUSTC": "rustc", "KRUSTFLAGS": f"--sysroot={self.missing}" })
        self.assertIn("Source code for the 'core' standard library could not be found", result.stderr)

    def test_rustc_src_core_rustlibsrc(self):
        result = self.run_script(self.Expected.FAILURE, { "RUST_LIB_SRC": self.missing })
        self.assertIn("Source code for the 'core' standard library could not be found", result.stderr)

    def test_success_cc_unknown(self):
        result = self.run_script(self.Expected.SUCCESS_WITH_EXTRA_OUTPUT, { "CC": self.missing })
        self.assertIn("unknown C compiler", result.stderr)

    def test_success_cc_multiple_arguments_ccache(self):
        clang = self.generate_clang(f"""Ubuntu clang version {self.llvm_default_version}-1ubuntu1
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
""")
        result = self.run_script(self.Expected.SUCCESS, { "CC": f"{clang} clang" })

    def test_success_rustc_version(self):
        for rustc_stdout in (
            f"rustc {self.rustc_default_version} (a8314ef7d 2022-06-27)",
            f"rustc {self.rustc_default_version}-dev (a8314ef7d 2022-06-27)",
            f"rustc {self.rustc_default_version}-1.60.0 (a8314ef7d 2022-06-27)",
        ):
            with self.subTest(rustc_stdout=rustc_stdout):
                rustc = self.generate_rustc(rustc_stdout)
                result = self.run_script(self.Expected.SUCCESS, { "RUSTC": rustc })

    def test_success_bindgen_version(self):
        for bindgen_stdout in (
            f"bindgen {self.bindgen_default_version}",
            f"bindgen {self.bindgen_default_version}-dev",
            f"bindgen {self.bindgen_default_version}-0.999.0",
        ):
            with self.subTest(bindgen_stdout=bindgen_stdout):
                bindgen = self.generate_bindgen_version(bindgen_stdout)
                result = self.run_script(self.Expected.SUCCESS, { "BINDGEN": bindgen })

    def test_success_bindgen_libclang(self):
        for stderr in (
            f"scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {self.llvm_default_version} (https://github.com/llvm/llvm-project.git 4a2c05b05ed07f1f620e94f6524a8b4b2760a0b1) [-W#pragma-messages], err: false",
            f"/home/jd/Documents/dev/kernel-module-flake/linux-6.1/outputs/dev/lib/modules/6.1.0-development/source/scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {self.llvm_default_version}  [-W#pragma-messages], err: false",
            f"scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {self.llvm_default_version} (Fedora 13.0.0-3.fc35) [-W#pragma-messages], err: false",
            f"""
/nix/store/dsd5gz46hdbdk2rfdimqddhq6m8m8fqs-bash-5.1-p16/bin/bash: warning: setlocale: LC_ALL: cannot change locale (c)
scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {self.llvm_default_version}  [-W#pragma-messages], err: false
""",
            f"""
/nix/store/dsd5gz46hdbdk2rfdimqddhq6m8m8fqs-bash-5.1.0-p16/bin/bash: warning: setlocale: LC_ALL: cannot change locale (c)
/home/jd/Documents/dev/kernel-module-flake/linux-6.1/outputs/dev/lib/modules/6.1.0-development/source/scripts/rust_is_available_bindgen_libclang.h:2:9: warning: clang version {self.llvm_default_version} (Fedora 13.0.0-3.fc35) [-W#pragma-messages], err: false
"""
        ):
            with self.subTest(stderr=stderr):
                bindgen = self.generate_bindgen_libclang(stderr)
                result = self.run_script(self.Expected.SUCCESS, { "BINDGEN": bindgen })

    def test_success_clang_version(self):
        for clang_stdout in (
            f"clang version {self.llvm_default_version} (https://github.com/llvm/llvm-project.git 4a2c05b05ed07f1f620e94f6524a8b4b2760a0b1)",
            f"clang version {self.llvm_default_version}-dev",
            f"clang version {self.llvm_default_version}-2~ubuntu20.04.1",
            f"Ubuntu clang version {self.llvm_default_version}-2~ubuntu20.04.1",
        ):
            with self.subTest(clang_stdout=clang_stdout):
                clang = self.generate_clang(clang_stdout)
                result = self.run_script(self.Expected.SUCCESS, { "CC": clang })

    def test_success_real_programs(self):
        for cc in ["gcc", "clang"]:
            with self.subTest(cc=cc):
                result = self.run_script(self.Expected.SUCCESS, {
                    "PATH": os.environ["PATH"],
                    "RUSTC": "rustc",
                    "BINDGEN": "bindgen",
                    "CC": cc,
                })

if __name__ == "__main__":
    unittest.main()
