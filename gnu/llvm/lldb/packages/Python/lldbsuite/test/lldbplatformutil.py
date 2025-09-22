""" This module contains functions used by the test cases to hide the
architecture and/or the platform dependent nature of the tests. """

# System modules
import itertools
import json
import re
import subprocess
import sys
import os
from packaging import version

# LLDB modules
import lldb
from . import configuration
from . import lldbtest_config
import lldbsuite.test.lldbplatform as lldbplatform
from lldbsuite.test.builders import get_builder
from lldbsuite.test.lldbutil import is_exe


def check_first_register_readable(test_case):
    arch = test_case.getArchitecture()

    if arch in ["x86_64", "i386"]:
        test_case.expect("register read eax", substrs=["eax = 0x"])
    elif arch in ["arm", "armv7", "armv7k", "armv8l", "armv7l"]:
        test_case.expect("register read r0", substrs=["r0 = 0x"])
    elif arch in ["aarch64", "arm64", "arm64e", "arm64_32"]:
        test_case.expect("register read x0", substrs=["x0 = 0x"])
    elif re.match("mips", arch):
        test_case.expect("register read zero", substrs=["zero = 0x"])
    elif arch in ["s390x"]:
        test_case.expect("register read r0", substrs=["r0 = 0x"])
    elif arch in ["powerpc64le"]:
        test_case.expect("register read r0", substrs=["r0 = 0x"])
    elif re.match("^rv(32|64)", arch):
        test_case.expect("register read zero", substrs=["zero = 0x"])
    else:
        # TODO: Add check for other architectures
        test_case.fail(
            "Unsupported architecture for test case (arch: %s)"
            % test_case.getArchitecture()
        )


def _run_adb_command(cmd, device_id):
    device_id_args = []
    if device_id:
        device_id_args = ["-s", device_id]
    full_cmd = ["adb"] + device_id_args + cmd
    p = subprocess.Popen(full_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    return p.returncode, stdout, stderr


def target_is_android():
    return configuration.lldb_platform_name == "remote-android"


def android_device_api():
    if not hasattr(android_device_api, "result"):
        assert configuration.lldb_platform_url is not None
        device_id = None
        parsed_url = urlparse(configuration.lldb_platform_url)
        host_name = parsed_url.netloc.split(":")[0]
        if host_name != "localhost":
            device_id = host_name
            if device_id.startswith("[") and device_id.endswith("]"):
                device_id = device_id[1:-1]
        retcode, stdout, stderr = _run_adb_command(
            ["shell", "getprop", "ro.build.version.sdk"], device_id
        )
        if retcode == 0:
            android_device_api.result = int(stdout)
        else:
            raise LookupError(
                ">>> Unable to determine the API level of the Android device.\n"
                ">>> stdout:\n%s\n"
                ">>> stderr:\n%s\n" % (stdout, stderr)
            )
    return android_device_api.result


def match_android_device(device_arch, valid_archs=None, valid_api_levels=None):
    if not target_is_android():
        return False
    if valid_archs is not None and device_arch not in valid_archs:
        return False
    if valid_api_levels is not None and android_device_api() not in valid_api_levels:
        return False

    return True


def finalize_build_dictionary(dictionary):
    # Provide uname-like platform name
    platform_name_to_uname = {
        "linux": "Linux",
        "netbsd": "NetBSD",
        "freebsd": "FreeBSD",
        "windows": "Windows_NT",
        "macosx": "Darwin",
        "darwin": "Darwin",
    }

    if dictionary is None:
        dictionary = {}
    if target_is_android():
        dictionary["OS"] = "Android"
        dictionary["PIE"] = 1
    elif platformIsDarwin():
        dictionary["OS"] = "Darwin"
    else:
        dictionary["OS"] = platform_name_to_uname[getPlatform()]

    dictionary["HOST_OS"] = platform_name_to_uname[getHostPlatform()]

    return dictionary


def _get_platform_os(p):
    # Use the triple to determine the platform if set.
    triple = p.GetTriple()
    if triple:
        platform = triple.split("-")[2]
        if platform.startswith("freebsd"):
            platform = "freebsd"
        elif platform.startswith("netbsd"):
            platform = "netbsd"
        elif platform.startswith("openbsd"):
            platform = "openbsd"
        return platform

    return ""


def getHostPlatform():
    """Returns the host platform running the test suite."""
    return _get_platform_os(lldb.SBPlatform("host"))


def getDarwinOSTriples():
    return lldbplatform.translate(lldbplatform.darwin_all)


def getPlatform():
    """Returns the target platform which the tests are running on."""
    # Use the Apple SDK to determine the platform if set.
    if configuration.apple_sdk:
        platform = configuration.apple_sdk
        dot = platform.find(".")
        if dot != -1:
            platform = platform[:dot]
        if platform == "iphoneos":
            platform = "ios"
        return platform

    return _get_platform_os(lldb.selected_platform)


def platformIsDarwin():
    """Returns true if the OS triple for the selected platform is any valid apple OS"""
    return getPlatform() in getDarwinOSTriples()


def findMainThreadCheckerDylib():
    if not platformIsDarwin():
        return ""

    if getPlatform() in lldbplatform.translate(lldbplatform.darwin_embedded):
        return "/Developer/usr/lib/libMainThreadChecker.dylib"

    with os.popen("xcode-select -p") as output:
        xcode_developer_path = output.read().strip()
        mtc_dylib_path = "%s/usr/lib/libMainThreadChecker.dylib" % xcode_developer_path
        if os.path.isfile(mtc_dylib_path):
            return mtc_dylib_path

    return ""


class _PlatformContext(object):
    """Value object class which contains platform-specific options."""

    def __init__(
        self, shlib_environment_var, shlib_path_separator, shlib_prefix, shlib_extension
    ):
        self.shlib_environment_var = shlib_environment_var
        self.shlib_path_separator = shlib_path_separator
        self.shlib_prefix = shlib_prefix
        self.shlib_extension = shlib_extension


def createPlatformContext():
    if platformIsDarwin():
        return _PlatformContext("DYLD_LIBRARY_PATH", ":", "lib", "dylib")
    elif getPlatform() in ("linux", "freebsd", "netbsd", "openbsd"):
        return _PlatformContext("LD_LIBRARY_PATH", ":", "lib", "so")
    else:
        return _PlatformContext("PATH", ";", "", "dll")


def hasChattyStderr(test_case):
    """Some targets produce garbage on the standard error output. This utility function
    determines whether the tests can be strict about the expected stderr contents."""
    if match_android_device(
        test_case.getArchitecture(), ["aarch64"], range(22, 25 + 1)
    ):
        return True  # The dynamic linker on the device will complain about unknown DT entries
    return False


def builder_module():
    return get_builder(sys.platform)


def getArchitecture():
    """Returns the architecture in effect the test suite is running with."""
    module = builder_module()
    arch = module.getArchitecture()
    if arch == "amd64":
        arch = "x86_64"
    if arch in ["armv7l", "armv8l"]:
        arch = "arm"
    return arch


lldbArchitecture = None


def getLLDBArchitecture():
    """Returns the architecture of the lldb binary."""
    global lldbArchitecture
    if not lldbArchitecture:
        # These two target settings prevent lldb from doing setup that does
        # nothing but slow down the end goal of printing the architecture.
        command = [
            lldbtest_config.lldbExec,
            "-x",
            "-b",
            "-o",
            "settings set target.preload-symbols false",
            "-o",
            "settings set target.load-script-from-symbol-file false",
            "-o",
            "file " + lldbtest_config.lldbExec,
        ]

        output = subprocess.check_output(command)
        str = output.decode()

        for line in str.splitlines():
            m = re.search(r"Current executable set to '.*' \((.*)\)\.", line)
            if m:
                lldbArchitecture = m.group(1)
                break

    return lldbArchitecture


def getCompiler():
    """Returns the compiler in effect the test suite is running with."""
    module = builder_module()
    return module.getCompiler()


def getCompilerBinary():
    """Returns the compiler binary the test suite is running with."""
    return getCompiler().split()[0]


def getCompilerVersion():
    """Returns a string that represents the compiler version.
    Supports: llvm, clang.
    """
    compiler = getCompilerBinary()
    version_output = subprocess.check_output([compiler, "--version"], errors="replace")
    m = re.search("version ([0-9.]+)", version_output)
    if m:
        return m.group(1)
    return "unknown"


def getDwarfVersion():
    """Returns the dwarf version generated by clang or '0'."""
    if configuration.dwarf_version:
        return str(configuration.dwarf_version)
    if "clang" in getCompiler():
        try:
            triple = builder_module().getTriple(getArchitecture())
            target = ["-target", triple] if triple else []
            driver_output = subprocess.check_output(
                [getCompiler()] + target + "-g -c -x c - -o - -###".split(),
                stderr=subprocess.STDOUT,
            )
            driver_output = driver_output.decode("utf-8")
            for line in driver_output.split(os.linesep):
                m = re.search("dwarf-version=([0-9])", line)
                if m:
                    return m.group(1)
        except subprocess.CalledProcessError:
            pass
    return "0"


def expectedCompilerVersion(compiler_version):
    """Returns True iff compiler_version[1] matches the current compiler version.
    Use compiler_version[0] to specify the operator used to determine if a match has occurred.
    Any operator other than the following defaults to an equality test:
        '>', '>=', "=>", '<', '<=', '=<', '!=', "!" or 'not'

    If the current compiler version cannot be determined, we assume it is close to the top
    of trunk, so any less-than or equal-to comparisons will return False, and any
    greater-than or not-equal-to comparisons will return True.
    """
    if compiler_version is None:
        return True
    operator = str(compiler_version[0])
    version_str = str(compiler_version[1])

    if not version_str:
        return True

    test_compiler_version_str = getCompilerVersion()
    if test_compiler_version_str == "unknown":
        # Assume the compiler version is at or near the top of trunk.
        return operator in [">", ">=", "!", "!=", "not"]

    actual_version = version.parse(version_str)
    test_compiler_version = version.parse(test_compiler_version_str)

    if operator == ">":
        return test_compiler_version > actual_version
    if operator == ">=" or operator == "=>":
        return test_compiler_version >= actual_version
    if operator == "<":
        return test_compiler_version < actual_version
    if operator == "<=" or operator == "=<":
        return test_compiler_version <= actual_version
    if operator == "!=" or operator == "!" or operator == "not":
        return version_str not in test_compiler_version_str
    return version_str in test_compiler_version_str


def expectedCompiler(compilers):
    """Returns True iff any element of compilers is a sub-string of the current compiler."""
    if compilers is None:
        return True

    for compiler in compilers:
        if compiler in getCompiler():
            return True

    return False


# This is a helper function to determine if a specific version of Xcode's linker
# contains a TLS bug. We want to skip TLS tests if they contain this bug, but
# adding a linker/linker_version conditions to a decorator is challenging due to
# the number of ways linkers can enter the build process.
def xcode15LinkerBug():
    """Returns true iff a test is running on a darwin platform and the host linker is between versions 1000 and 1109."""
    darwin_platforms = lldbplatform.translate(lldbplatform.darwin_all)
    if getPlatform() not in darwin_platforms:
        return False

    try:
        raw_version_details = subprocess.check_output(
            ("xcrun", "ld", "-version_details")
        )
        version_details = json.loads(raw_version_details)
        version = version_details.get("version", "0")
        version_tuple = tuple(int(x) for x in version.split("."))
        if (1000,) <= version_tuple <= (1109,):
            return True
    except:
        pass

    return False
