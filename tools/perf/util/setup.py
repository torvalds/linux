# SPDX-License-Identifier: GPL-2.0
"""Setup script for perf python extension.

This script is used to build and install the perf python binding.
It handles compiler-specific flags, especially for clang, and configures
the setuptools Extension.
"""

import os
import shlex
import shutil
import subprocess
import sysconfig

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext as _build_ext
from setuptools.command.install_lib import install_lib as _install_lib


def clang_has_option(cc: str, cc_args: list[str], src_feature_tests: str, option: str) -> bool:
    """Check if clang supports a specific option.

    Args:
        cc: The compiler executable.
        cc_args: Compiler arguments from CC environment variable.
        src_feature_tests: Path to the feature tests directory.
        option: The compiler option to check (e.g., "-mcet").

    Returns:
        True if the option is supported, False otherwise.
    """
    error_substrings = (
        b"unknown argument",
        b"is not supported",
        b"unknown warning option"
    )
    cmd = [cc] + cc_args + [
        option,
        "-o", "/dev/null",
        os.path.join(src_feature_tests, "test-hello.c")
    ]
    try:
        res = subprocess.run(cmd, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, check=False)
        cc_output = res.stderr.splitlines()
    except OSError:
        return False
    return not any(any(error in line for error in error_substrings) for line in cc_output)


def filter_clang_options(cc: str, cc_args: list[str], src_feature_tests: str) -> None:
    """Filter out unsupported clang options from sysconfig CFLAGS and OPT.

    Args:
        cc: The compiler executable.
        cc_args: Compiler arguments from CC environment variable.
        src_feature_tests: Path to the feature tests directory.
    """
    config_vars = sysconfig.get_config_vars()
    for var in ('CFLAGS', 'OPT'):
        if var not in config_vars:
            continue

        # Split into individual flags using shlex to preserve quoted arguments
        flags = shlex.split(config_vars[var])

        # Remove -specs=...
        flags = [f for f in flags if not f.startswith("-specs=")]

        options = (
            "-mcet",
            "-fcf-protection",
            "-fstack-clash-protection",
            "-fstack-protector-strong",
            "-fno-semantic-interposition",
            "-ffat-lto-objects",
            "-ftree-loop-distribute-patterns",
            "-gno-variable-location-views"
        )
        for option in options:
            if not clang_has_option(cc, cc_args, src_feature_tests, option):
                # Remove the option and any variant (e.g. -option=...)
                flags = [f for f in flags if not f.startswith(option)]

        # Re-join flags preserving quoting
        config_vars[var] = shlex.join(flags)


class BuildExt(_build_ext):
    """Custom build_ext command to set output directories."""

    def __init__(self, *args, **kwargs):
        self.build_lib = None
        self.build_temp = None
        super().__init__(*args, **kwargs)

    def finalize_options(self) -> None:
        _build_ext.finalize_options(self)
        build_lib = os.getenv('PYTHON_EXTBUILD_LIB')
        build_tmp = os.getenv('PYTHON_EXTBUILD_TMP')
        if build_lib:
            self.build_lib = build_lib
        if build_tmp:
            self.build_temp = build_tmp


class InstallLib(_install_lib):
    """Custom install_lib command to set output directory."""

    def __init__(self, *args, **kwargs):
        self.build_dir = None
        super().__init__(*args, **kwargs)

    def finalize_options(self) -> None:
        _install_lib.finalize_options(self)
        build_lib = os.getenv('PYTHON_EXTBUILD_LIB')
        if build_lib:
            self.build_dir = build_lib

    def run(self):
        _install_lib.run(self)
        srctree = os.getenv('srctree', '.')
        src_perf = os.path.join(srctree, 'tools/perf')
        shutil.copy2(os.path.join(src_perf, 'python/perf.pyi'), self.install_dir)


def main() -> None:
    """Main entry point for the setup script."""
    cc_env = os.getenv("CC")
    assert cc_env, "Environment variable CC not set"

    # Safe parsing of CC environment variable which might contain options/quotes
    cc_tokens = shlex.split(cc_env)
    cc = cc_tokens[0]
    cc_args = cc_tokens[1:]

    # Run CC -v to check if it is clang.
    try:
        cc_info = subprocess.run(
            [cc, "-v"], stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, check=False
        )
        cc_is_clang = b"clang version" in cc_info.stderr
    except OSError as e:
        raise RuntimeError(f"Failed to execute compiler '{cc}': {e}") from e

    srctree = os.getenv('srctree')
    assert srctree, "Environment variable srctree, for the Linux sources, not set"
    src_feature_tests = f'{srctree}/tools/build/feature'

    if cc_is_clang:
        filter_clang_options(cc, cc_args, src_feature_tests)

    # switch off several checks (need to be at the end of cflags list)
    cflags = [
        '-fno-strict-aliasing',
        '-Wno-write-strings',
        '-Wno-unused-parameter',
        '-Wno-redundant-decls'
    ]
    if cc_is_clang:
        cflags += ["-Wno-unused-command-line-argument"]
        if clang_has_option(
            cc, cc_args, src_feature_tests, "-Wno-cast-function-type-mismatch"
        ):
            cflags += ["-Wno-cast-function-type-mismatch"]
    else:
        cflags += ['-Wno-cast-function-type']

    # The python headers have mixed code with declarations (decls after asserts, for instance)
    cflags += ["-Wno-declaration-after-statement"]

    src_perf = f'{srctree}/tools/perf'

    perf = Extension(
        'perf',
        sources=[os.path.join(src_perf, 'util/python.c')],
        include_dirs=['util/include'],
        extra_compile_args=cflags,
    )

    setup(
        name='perf',
        version='0.1',
        description='Interface with the Linux profiling infrastructure',
        author='Arnaldo Carvalho de Melo',
        author_email='acme@redhat.com',
        license='GPLv2',
        url='http://perf.wiki.kernel.org',
        ext_modules=[perf],
        cmdclass={'build_ext': BuildExt, 'install_lib': InstallLib},
    )


if __name__ == '__main__':
    main()
