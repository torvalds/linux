# -*- Python -*-

# Configuration file for 'lit' test runner.
# This file contains common config setup rules for unit tests in various
# compiler-rt testsuites.

import os

import lit.formats

# Copied from libcxx's config.py
def get_lit_conf(name, default=None):
    # Allow overriding on the command line using --param=<name>=<val>
    val = lit_config.params.get(name, None)
    if val is None:
        val = getattr(config, name, None)
        if val is None:
            val = default
    return val


emulator = get_lit_conf("emulator", None)

# Setup test format
llvm_build_mode = getattr(config, "llvm_build_mode", "Debug")
config.test_format = lit.formats.GoogleTest(llvm_build_mode, "Test", emulator)

# Setup test suffixes.
config.suffixes = []

# Tweak PATH to include llvm tools dir.
llvm_tools_dir = config.llvm_tools_dir
if (not llvm_tools_dir) or (not os.path.exists(llvm_tools_dir)):
    lit_config.fatal("Invalid llvm_tools_dir config attribute: %r" % llvm_tools_dir)
path = os.path.pathsep.join((llvm_tools_dir, config.environment["PATH"]))
config.environment["PATH"] = path

# Propagate the temp directory. Windows requires this because it uses \Windows\
# if none of these are present.
if "TMP" in os.environ:
    config.environment["TMP"] = os.environ["TMP"]
if "TEMP" in os.environ:
    config.environment["TEMP"] = os.environ["TEMP"]

if config.host_os == "Darwin":
    # Only run up to 3 processes that require shadow memory simultaneously on
    # 64-bit Darwin. Using more scales badly and hogs the system due to
    # inefficient handling of large mmap'd regions (terabytes) by the kernel.
    lit_config.parallelism_groups["shadow-memory"] = 3

    # Disable libmalloc nano allocator due to crashes running on macOS 12.0.
    # rdar://80086125
    config.environment["MallocNanoZone"] = "0"

    # We crash when we set DYLD_INSERT_LIBRARIES for unit tests, so interceptors
    # don't work.
    config.environment["ASAN_OPTIONS"] = "verify_interceptors=0"
    config.environment["TSAN_OPTIONS"] = "verify_interceptors=0"
