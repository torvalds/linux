# System modules
from functools import wraps
from packaging import version
import ctypes
import locale
import os
import platform
import re
import sys
import tempfile
import subprocess

# Third-party modules
import unittest

# LLDB modules
import lldb
from . import configuration
from . import test_categories
from . import lldbtest_config
from lldbsuite.support import funcutils
from lldbsuite.test import lldbplatform
from lldbsuite.test import lldbplatformutil


class DecorateMode:
    Skip, Xfail = range(2)


# You can use no_match to reverse the test of the conditional that is used to match keyword
# arguments in the skip / xfail decorators.  If oslist=["windows", "linux"] skips windows
# and linux, oslist=no_match(["windows", "linux"]) skips *unless* windows
# or linux.
class no_match:
    def __init__(self, item):
        self.item = item


def _check_expected_version(comparison, expected, actual):
    def fn_leq(x, y):
        return x <= y

    def fn_less(x, y):
        return x < y

    def fn_geq(x, y):
        return x >= y

    def fn_greater(x, y):
        return x > y

    def fn_eq(x, y):
        return x == y

    def fn_neq(x, y):
        return x != y

    op_lookup = {
        "==": fn_eq,
        "=": fn_eq,
        "!=": fn_neq,
        "<>": fn_neq,
        ">": fn_greater,
        "<": fn_less,
        ">=": fn_geq,
        "<=": fn_leq,
    }

    return op_lookup[comparison](version.parse(actual), version.parse(expected))


def _match_decorator_property(expected, actual):
    if expected is None:
        return True

    if actual is None:
        return False

    if isinstance(expected, no_match):
        return not _match_decorator_property(expected.item, actual)

    # Python 3.6 doesn't declare a `re.Pattern` type, get the dynamic type.
    pattern_type = type(re.compile(""))
    if isinstance(expected, (pattern_type, str)):
        return re.search(expected, actual) is not None

    if hasattr(expected, "__iter__"):
        return any(
            [x is not None and _match_decorator_property(x, actual) for x in expected]
        )

    return expected == actual


def _compiler_supports(
    compiler, flag, source="int main() {}", output_file=tempfile.NamedTemporaryFile()
):
    """Test whether the compiler supports the given flag."""
    if platform.system() == "Darwin":
        compiler = "xcrun " + compiler
    try:
        cmd = "echo '%s' | %s %s -x c -o %s -" % (
            source,
            compiler,
            flag,
            output_file.name,
        )
        subprocess.check_call(cmd, shell=True)
    except subprocess.CalledProcessError:
        return False
    return True


def expectedFailureIf(condition, bugnumber=None):
    def expectedFailure_impl(func):
        if isinstance(func, type) and issubclass(func, unittest.TestCase):
            raise Exception("Decorator can only be used to decorate a test method")

        if condition:
            return unittest.expectedFailure(func)
        return func

    if callable(bugnumber):
        return expectedFailure_impl(bugnumber)
    else:
        return expectedFailure_impl


def skipTestIfFn(expected_fn, bugnumber=None):
    def skipTestIfFn_impl(func):
        if isinstance(func, type) and issubclass(func, unittest.TestCase):
            reason = expected_fn()
            # The return value is the reason (or None if we don't skip), so
            # reason is used for both args.
            return unittest.skipIf(condition=reason, reason=reason)(func)

        @wraps(func)
        def wrapper(*args, **kwargs):
            self = args[0]
            if funcutils.requires_self(expected_fn):
                reason = expected_fn(self)
            else:
                reason = expected_fn()

            if reason is not None:
                self.skipTest(reason)
            else:
                return func(*args, **kwargs)

        return wrapper

    # Some decorators can be called both with no arguments (e.g. @expectedFailureWindows)
    # or with arguments (e.g. @expectedFailureWindows(compilers=['gcc'])).  When called
    # the first way, the first argument will be the actual function because decorators are
    # weird like that.  So this is basically a check that says "how was the
    # decorator used"
    if callable(bugnumber):
        return skipTestIfFn_impl(bugnumber)
    else:
        return skipTestIfFn_impl


def _xfailForDebugInfo(expected_fn, bugnumber=None):
    def expectedFailure_impl(func):
        if isinstance(func, type) and issubclass(func, unittest.TestCase):
            raise Exception("Decorator can only be used to decorate a test method")

        func.__xfail_for_debug_info_cat_fn__ = expected_fn
        return func

    if callable(bugnumber):
        return expectedFailure_impl(bugnumber)
    else:
        return expectedFailure_impl


def _skipForDebugInfo(expected_fn, bugnumber=None):
    def skipImpl(func):
        if isinstance(func, type) and issubclass(func, unittest.TestCase):
            raise Exception("Decorator can only be used to decorate a test method")

        func.__skip_for_debug_info_cat_fn__ = expected_fn
        return func

    if callable(bugnumber):
        return skipImpl(bugnumber)
    else:
        return skipImpl


def _decorateTest(
    mode,
    bugnumber=None,
    oslist=None,
    hostoslist=None,
    compiler=None,
    compiler_version=None,
    archs=None,
    triple=None,
    debug_info=None,
    swig_version=None,
    py_version=None,
    macos_version=None,
    remote=None,
    dwarf_version=None,
    setting=None,
    asan=None,
):
    def fn(actual_debug_info=None):
        skip_for_os = _match_decorator_property(
            lldbplatform.translate(oslist), lldbplatformutil.getPlatform()
        )
        skip_for_hostos = _match_decorator_property(
            lldbplatform.translate(hostoslist), lldbplatformutil.getHostPlatform()
        )
        skip_for_compiler = _match_decorator_property(
            compiler, lldbplatformutil.getCompiler()
        ) and lldbplatformutil.expectedCompilerVersion(compiler_version)
        skip_for_arch = _match_decorator_property(
            archs, lldbplatformutil.getArchitecture()
        )
        skip_for_debug_info = _match_decorator_property(debug_info, actual_debug_info)
        skip_for_triple = _match_decorator_property(
            triple, lldb.selected_platform.GetTriple()
        )
        skip_for_remote = _match_decorator_property(
            remote, lldb.remote_platform is not None
        )

        skip_for_swig_version = (
            (swig_version is None)
            or (not hasattr(lldb, "swig_version"))
            or (
                _check_expected_version(
                    swig_version[0], swig_version[1], lldb.swig_version
                )
            )
        )
        skip_for_py_version = (py_version is None) or _check_expected_version(
            py_version[0],
            py_version[1],
            "{}.{}".format(sys.version_info.major, sys.version_info.minor),
        )
        skip_for_macos_version = (macos_version is None) or (
            (platform.mac_ver()[0] != "")
            and (
                _check_expected_version(
                    macos_version[0], macos_version[1], platform.mac_ver()[0]
                )
            )
        )
        skip_for_dwarf_version = (dwarf_version is None) or (
            _check_expected_version(
                dwarf_version[0], dwarf_version[1], lldbplatformutil.getDwarfVersion()
            )
        )
        skip_for_setting = (setting is None) or (setting in configuration.settings)
        skip_for_asan = (asan is None) or is_running_under_asan()

        # For the test to be skipped, all specified (e.g. not None) parameters must be True.
        # An unspecified parameter means "any", so those are marked skip by default.  And we skip
        # the final test if all conditions are True.
        conditions = [
            (oslist, skip_for_os, "target o/s"),
            (hostoslist, skip_for_hostos, "host o/s"),
            (compiler, skip_for_compiler, "compiler or version"),
            (archs, skip_for_arch, "architecture"),
            (debug_info, skip_for_debug_info, "debug info format"),
            (triple, skip_for_triple, "target triple"),
            (swig_version, skip_for_swig_version, "swig version"),
            (py_version, skip_for_py_version, "python version"),
            (macos_version, skip_for_macos_version, "macOS version"),
            (remote, skip_for_remote, "platform locality (remote/local)"),
            (dwarf_version, skip_for_dwarf_version, "dwarf version"),
            (setting, skip_for_setting, "setting"),
            (asan, skip_for_asan, "running under asan"),
        ]
        reasons = []
        final_skip_result = True
        for this_condition in conditions:
            final_skip_result = final_skip_result and this_condition[1]
            if this_condition[0] is not None and this_condition[1]:
                reasons.append(this_condition[2])
        reason_str = None
        if final_skip_result:
            mode_str = {DecorateMode.Skip: "skipping", DecorateMode.Xfail: "xfailing"}[
                mode
            ]
            if len(reasons) > 0:
                reason_str = ",".join(reasons)
                reason_str = "{} due to the following parameter(s): {}".format(
                    mode_str, reason_str
                )
            else:
                reason_str = "{} unconditionally".format(mode_str)
            if bugnumber is not None and not callable(bugnumber):
                reason_str = reason_str + " [" + str(bugnumber) + "]"
        return reason_str

    if mode == DecorateMode.Skip:
        if debug_info:
            return _skipForDebugInfo(fn, bugnumber)
        return skipTestIfFn(fn, bugnumber)
    elif mode == DecorateMode.Xfail:
        if debug_info:
            return _xfailForDebugInfo(fn, bugnumber)
        return expectedFailureIf(fn(), bugnumber)
    else:
        return None


# provide a function to xfail on defined oslist, compiler version, and archs
# if none is specified for any argument, that argument won't be checked and thus means for all
# for example,
# @expectedFailureAll, xfail for all platform/compiler/arch,
# @expectedFailureAll(compiler='gcc'), xfail for gcc on all platform/architecture
# @expectedFailureAll(bugnumber, ["linux"], "gcc", ['>=', '4.9'], ['i386']), xfail for gcc>=4.9 on linux with i386


def expectedFailureAll(
    bugnumber=None,
    oslist=None,
    hostoslist=None,
    compiler=None,
    compiler_version=None,
    archs=None,
    triple=None,
    debug_info=None,
    swig_version=None,
    py_version=None,
    macos_version=None,
    remote=None,
    dwarf_version=None,
    setting=None,
    asan=None,
):
    return _decorateTest(
        DecorateMode.Xfail,
        bugnumber=bugnumber,
        oslist=oslist,
        hostoslist=hostoslist,
        compiler=compiler,
        compiler_version=compiler_version,
        archs=archs,
        triple=triple,
        debug_info=debug_info,
        swig_version=swig_version,
        py_version=py_version,
        macos_version=macos_version,
        remote=remote,
        dwarf_version=dwarf_version,
        setting=setting,
        asan=asan,
    )


# provide a function to skip on defined oslist, compiler version, and archs
# if none is specified for any argument, that argument won't be checked and thus means for all
# for example,
# @skipIf, skip for all platform/compiler/arch,
# @skipIf(compiler='gcc'), skip for gcc on all platform/architecture
# @skipIf(bugnumber, ["linux"], "gcc", ['>=', '4.9'], ['i386']), skip for gcc>=4.9 on linux with i386 (all conditions must be true)
def skipIf(
    bugnumber=None,
    oslist=None,
    hostoslist=None,
    compiler=None,
    compiler_version=None,
    archs=None,
    triple=None,
    debug_info=None,
    swig_version=None,
    py_version=None,
    macos_version=None,
    remote=None,
    dwarf_version=None,
    setting=None,
    asan=None,
):
    return _decorateTest(
        DecorateMode.Skip,
        bugnumber=bugnumber,
        oslist=oslist,
        hostoslist=hostoslist,
        compiler=compiler,
        compiler_version=compiler_version,
        archs=archs,
        triple=triple,
        debug_info=debug_info,
        swig_version=swig_version,
        py_version=py_version,
        macos_version=macos_version,
        remote=remote,
        dwarf_version=dwarf_version,
        setting=setting,
        asan=asan,
    )


def _skip_fn_for_android(reason, api_levels, archs):
    def impl():
        result = lldbplatformutil.match_android_device(
            lldbplatformutil.getArchitecture(),
            valid_archs=archs,
            valid_api_levels=api_levels,
        )
        return reason if result else None

    return impl


def add_test_categories(cat):
    """Add test categories to a TestCase method"""
    cat = test_categories.validate(cat, True)

    def impl(func):
        try:
            if hasattr(func, "categories"):
                cat.extend(func.categories)
            setattr(func, "categories", cat)
        except AttributeError:
            raise Exception("Cannot assign categories to inline tests.")

        return func

    return impl


def benchmarks_test(func):
    """Decorate the item as a benchmarks test."""

    def should_skip_benchmarks_test():
        return "benchmarks test"

    # Mark this function as such to separate them from the regular tests.
    result = skipTestIfFn(should_skip_benchmarks_test)(func)
    result.__benchmarks_test__ = True
    return result


def no_debug_info_test(func):
    """Decorate the item as a test what don't use any debug info. If this annotation is specified
    then the test runner won't generate a separate test for each debug info format."""
    if isinstance(func, type) and issubclass(func, unittest.TestCase):
        raise Exception(
            "@no_debug_info_test can only be used to decorate a test method"
        )

    @wraps(func)
    def wrapper(self, *args, **kwargs):
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__no_debug_info_test__ = True
    return wrapper


def apple_simulator_test(platform):
    """
    Decorate the test as a test requiring a simulator for a specific platform.

    Consider that a simulator is available if you have the corresponding SDK installed.
    The SDK identifiers for simulators are iphonesimulator, appletvsimulator, watchsimulator
    """

    def should_skip_simulator_test():
        if lldbplatformutil.getHostPlatform() not in ["darwin", "macosx"]:
            return "simulator tests are run only on darwin hosts."
        try:
            DEVNULL = open(os.devnull, "w")
            output = subprocess.check_output(
                ["xcodebuild", "-showsdks"], stderr=DEVNULL
            ).decode("utf-8")
            if re.search("%ssimulator" % platform, output):
                return None
            else:
                return "%s simulator is not supported on this system." % platform
        except subprocess.CalledProcessError:
            return "Simulators are unsupported on this system (xcodebuild failed)"

    return skipTestIfFn(should_skip_simulator_test)


def debugserver_test(func):
    """Decorate the item as a debugserver test."""
    return add_test_categories(["debugserver"])(func)


def llgs_test(func):
    """Decorate the item as a lldb-server test."""
    return add_test_categories(["llgs"])(func)


def expectedFailureOS(
    oslist, bugnumber=None, compilers=None, debug_info=None, archs=None
):
    return expectedFailureAll(
        oslist=oslist,
        bugnumber=bugnumber,
        compiler=compilers,
        archs=archs,
        debug_info=debug_info,
    )


def expectedFailureDarwin(bugnumber=None, compilers=None, debug_info=None, archs=None):
    # For legacy reasons, we support both "darwin" and "macosx" as OS X
    # triples.
    return expectedFailureOS(
        lldbplatform.darwin_all,
        bugnumber,
        compilers,
        debug_info=debug_info,
        archs=archs,
    )


def expectedFailureAndroid(bugnumber=None, api_levels=None, archs=None):
    """Mark a test as xfail for Android.

    Arguments:
        bugnumber - The LLVM pr associated with the problem.
        api_levels - A sequence of numbers specifying the Android API levels
            for which a test is expected to fail. None means all API level.
        arch - A sequence of architecture names specifying the architectures
            for which a test is expected to fail. None means all architectures.
    """
    return expectedFailureIf(
        _skip_fn_for_android("xfailing on android", api_levels, archs)(), bugnumber
    )


def expectedFailureNetBSD(bugnumber=None):
    return expectedFailureOS(["netbsd"], bugnumber)


def expectedFailureWindows(bugnumber=None):
    return expectedFailureOS(["windows"], bugnumber)


# TODO: This decorator does not do anything. Remove it.
def expectedFlakey(expected_fn, bugnumber=None):
    def expectedFailure_impl(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            func(*args, **kwargs)

        return wrapper

    # Some decorators can be called both with no arguments (e.g. @expectedFailureWindows)
    # or with arguments (e.g. @expectedFailureWindows(compilers=['gcc'])).  When called
    # the first way, the first argument will be the actual function because decorators are
    # weird like that.  So this is basically a check that says "which syntax was the original
    # function decorated with?"
    if callable(bugnumber):
        return expectedFailure_impl(bugnumber)
    else:
        return expectedFailure_impl


def expectedFlakeyOS(oslist, bugnumber=None, compilers=None):
    def fn(self):
        return (
            lldbplatformutil.getPlatform() in oslist
            and lldbplatformutil.expectedCompiler(compilers)
        )

    return expectedFlakey(fn, bugnumber)


def expectedFlakeyDarwin(bugnumber=None, compilers=None):
    # For legacy reasons, we support both "darwin" and "macosx" as OS X
    # triples.
    return expectedFlakeyOS(lldbplatformutil.getDarwinOSTriples(), bugnumber, compilers)


def expectedFlakeyFreeBSD(bugnumber=None, compilers=None):
    return expectedFlakeyOS(["freebsd"], bugnumber, compilers)


def expectedFlakeyLinux(bugnumber=None, compilers=None):
    return expectedFlakeyOS(["linux"], bugnumber, compilers)


def expectedFlakeyNetBSD(bugnumber=None, compilers=None):
    return expectedFlakeyOS(["netbsd"], bugnumber, compilers)


def expectedFlakeyAndroid(bugnumber=None, api_levels=None, archs=None):
    return expectedFlakey(
        _skip_fn_for_android("flakey on android", api_levels, archs), bugnumber
    )


def skipIfOutOfTreeDebugserver(func):
    """Decorate the item to skip tests if using an out-of-tree debugserver."""

    def is_out_of_tree_debugserver():
        return (
            "out-of-tree debugserver"
            if lldbtest_config.out_of_tree_debugserver
            else None
        )

    return skipTestIfFn(is_out_of_tree_debugserver)(func)


def skipIfRemote(func):
    """Decorate the item to skip tests if testing remotely."""
    return unittest.skipIf(lldb.remote_platform, "skip on remote platform")(func)


def skipIfNoSBHeaders(func):
    """Decorate the item to mark tests that should be skipped when LLDB is built with no SB API headers."""

    def are_sb_headers_missing():
        if lldb.remote_platform:
            return "skip because SBHeaders tests make no sense remotely"

        if (
            lldbplatformutil.getHostPlatform() == "darwin"
            and configuration.lldb_framework_path
        ):
            header = os.path.join(
                configuration.lldb_framework_path,
                "Versions",
                "Current",
                "Headers",
                "LLDB.h",
            )
            if os.path.exists(header):
                return None

        header = os.path.join(
            os.environ["LLDB_SRC"], "include", "lldb", "API", "LLDB.h"
        )
        if not os.path.exists(header):
            return "skip because LLDB.h header not found"
        return None

    return skipTestIfFn(are_sb_headers_missing)(func)


def skipIfRosetta(bugnumber):
    """Skip a test when running the testsuite on macOS under the Rosetta translation layer."""

    def is_running_rosetta():
        if lldbplatformutil.getPlatform() in ["darwin", "macosx"]:
            if (platform.uname()[5] == "arm") and (
                lldbplatformutil.getArchitecture() == "x86_64"
            ):
                return "skipped under Rosetta"
        return None

    return skipTestIfFn(is_running_rosetta)


def skipIfiOSSimulator(func):
    """Decorate the item to skip tests that should be skipped on the iOS Simulator."""

    def is_ios_simulator():
        return (
            "skip on the iOS Simulator"
            if configuration.lldb_platform_name == "ios-simulator"
            else None
        )

    return skipTestIfFn(is_ios_simulator)(func)


def skipIfiOS(func):
    return skipIfPlatform(lldbplatform.translate(lldbplatform.ios))(func)


def skipIftvOS(func):
    return skipIfPlatform(lldbplatform.translate(lldbplatform.tvos))(func)


def skipIfwatchOS(func):
    return skipIfPlatform(lldbplatform.translate(lldbplatform.watchos))(func)


def skipIfbridgeOS(func):
    return skipIfPlatform(lldbplatform.translate(lldbplatform.bridgeos))(func)


def skipIfDarwinEmbedded(func):
    """Decorate the item to skip tests that should be skipped on Darwin armv7/arm64 targets."""
    return skipIfPlatform(lldbplatform.translate(lldbplatform.darwin_embedded))(func)


def skipIfDarwinSimulator(func):
    """Decorate the item to skip tests that should be skipped on Darwin simulator targets."""
    return skipIfPlatform(lldbplatform.translate(lldbplatform.darwin_simulator))(func)


def skipIfFreeBSD(func):
    """Decorate the item to skip tests that should be skipped on FreeBSD."""
    return skipIfPlatform(["freebsd"])(func)


def skipIfNetBSD(func):
    """Decorate the item to skip tests that should be skipped on NetBSD."""
    return skipIfPlatform(["netbsd"])(func)


def skipIfDarwin(func):
    """Decorate the item to skip tests that should be skipped on Darwin."""
    return skipIfPlatform(lldbplatform.translate(lldbplatform.darwin_all))(func)


def skipIfLinux(func):
    """Decorate the item to skip tests that should be skipped on Linux."""
    return skipIfPlatform(["linux"])(func)


def skipIfWindows(func):
    """Decorate the item to skip tests that should be skipped on Windows."""
    return skipIfPlatform(["windows"])(func)


def skipIfWindowsAndNonEnglish(func):
    """Decorate the item to skip tests that should be skipped on non-English locales on Windows."""

    def is_Windows_NonEnglish():
        if sys.platform != "win32":
            return None
        kernel = ctypes.windll.kernel32
        if locale.windows_locale[kernel.GetUserDefaultUILanguage()] == "en_US":
            return None
        return "skipping non-English Windows locale"

    return skipTestIfFn(is_Windows_NonEnglish)(func)


def skipUnlessWindows(func):
    """Decorate the item to skip tests that should be skipped on any non-Windows platform."""
    return skipUnlessPlatform(["windows"])(func)


def skipUnlessDarwin(func):
    """Decorate the item to skip tests that should be skipped on any non Darwin platform."""
    return skipUnlessPlatform(lldbplatformutil.getDarwinOSTriples())(func)


def skipUnlessTargetAndroid(func):
    return unittest.skipUnless(
        lldbplatformutil.target_is_android(), "requires target to be Android"
    )(func)


def skipIfHostIncompatibleWithTarget(func):
    """Decorate the item to skip tests when the host and target are incompatible."""

    def is_host_incompatible_with_remote():
        host_arch = lldbplatformutil.getLLDBArchitecture()
        host_platform = lldbplatformutil.getHostPlatform()
        target_arch = lldbplatformutil.getArchitecture()
        target_platform = lldbplatformutil.getPlatform()
        if (
            not (target_arch == "x86_64" and host_arch == "i386")
            and host_arch != target_arch
        ):
            return (
                "skipping because target %s is not compatible with host architecture %s"
                % (target_arch, host_arch)
            )
        if target_platform != host_platform:
            return "skipping because target is %s but host is %s" % (
                target_platform,
                host_platform,
            )
        if lldbplatformutil.match_android_device(target_arch):
            return "skipping because target is android"
        return None

    return skipTestIfFn(is_host_incompatible_with_remote)(func)


def skipIfPlatform(oslist):
    """Decorate the item to skip tests if running on one of the listed platforms."""
    # This decorator cannot be ported to `skipIf` yet because it is used on entire
    # classes, which `skipIf` explicitly forbids.
    return unittest.skipIf(
        lldbplatformutil.getPlatform() in oslist, "skip on %s" % (", ".join(oslist))
    )


def skipUnlessPlatform(oslist):
    """Decorate the item to skip tests unless running on one of the listed platforms."""
    # This decorator cannot be ported to `skipIf` yet because it is used on entire
    # classes, which `skipIf` explicitly forbids.
    return unittest.skipUnless(
        lldbplatformutil.getPlatform() in oslist,
        "requires one of %s" % (", ".join(oslist)),
    )


def skipUnlessArch(arch):
    """Decorate the item to skip tests unless running on the specified architecture."""

    def arch_doesnt_match():
        target_arch = lldbplatformutil.getArchitecture()
        if arch != target_arch:
            return "Test only runs on " + arch + ", but target arch is " + target_arch
        return None

    return skipTestIfFn(arch_doesnt_match)


def skipIfTargetAndroid(bugnumber=None, api_levels=None, archs=None):
    """Decorator to skip tests when the target is Android.

    Arguments:
        api_levels - The API levels for which the test should be skipped. If
            it is None, then the test will be skipped for all API levels.
        arch - A sequence of architecture names specifying the architectures
            for which a test is skipped. None means all architectures.
    """
    return skipTestIfFn(
        _skip_fn_for_android("skipping for android", api_levels, archs), bugnumber
    )


def skipUnlessAppleSilicon(func):
    """Decorate the item to skip tests unless running on Apple Silicon."""

    def not_apple_silicon():
        if platform.system() != "Darwin" or lldbplatformutil.getArchitecture() not in [
            "arm64",
            "arm64e",
        ]:
            return "Test only runs on Apple Silicon"
        return None

    return skipTestIfFn(not_apple_silicon)(func)


def skipUnlessSupportedTypeAttribute(attr):
    """Decorate the item to skip test unless Clang supports type __attribute__(attr)."""

    def compiler_doesnt_support_struct_attribute():
        compiler_path = lldbplatformutil.getCompiler()
        f = tempfile.NamedTemporaryFile()
        cmd = [lldbplatformutil.getCompiler(), "-x", "c++", "-c", "-o", f.name, "-"]
        p = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        stdout, stderr = p.communicate("struct __attribute__((%s)) Test {};" % attr)
        if attr in stderr:
            return "Compiler does not support attribute %s" % (attr)
        return None

    return skipTestIfFn(compiler_doesnt_support_struct_attribute)


def skipUnlessHasCallSiteInfo(func):
    """Decorate the function to skip testing unless call site info from clang is available."""

    def is_compiler_clang_with_call_site_info():
        compiler_path = lldbplatformutil.getCompiler()
        compiler = os.path.basename(compiler_path)
        if not compiler.startswith("clang"):
            return "Test requires clang as compiler"

        f = tempfile.NamedTemporaryFile()
        cmd = (
            "echo 'int main() {}' | "
            "%s -g -glldb -O1 -S -emit-llvm -x c -o %s -" % (compiler_path, f.name)
        )
        if os.popen(cmd).close() is not None:
            return "Compiler can't compile with call site info enabled"

        with open(f.name, "r") as ir_output_file:
            buf = ir_output_file.read()

        if "DIFlagAllCallsDescribed" not in buf:
            return "Compiler did not introduce DIFlagAllCallsDescribed IR flag"

        return None

    return skipTestIfFn(is_compiler_clang_with_call_site_info)(func)


def skipUnlessThreadSanitizer(func):
    """Decorate the item to skip test unless Clang -fsanitize=thread is supported."""

    def is_compiler_clang_with_thread_sanitizer():
        if is_running_under_asan():
            return "Thread sanitizer tests are disabled when runing under ASAN"

        compiler_path = lldbplatformutil.getCompiler()
        compiler = os.path.basename(compiler_path)
        if not compiler.startswith("clang"):
            return "Test requires clang as compiler"
        if lldbplatformutil.getPlatform() == "windows":
            return "TSAN tests not compatible with 'windows'"
        # rdar://28659145 - TSAN tests don't look like they're supported on i386
        if (
            lldbplatformutil.getArchitecture() == "i386"
            and platform.system() == "Darwin"
        ):
            return "TSAN tests not compatible with i386 targets"
        if not _compiler_supports(compiler_path, "-fsanitize=thread"):
            return "Compiler cannot compile with -fsanitize=thread"
        return None

    return skipTestIfFn(is_compiler_clang_with_thread_sanitizer)(func)


def skipUnlessUndefinedBehaviorSanitizer(func):
    """Decorate the item to skip test unless -fsanitize=undefined is supported."""

    def is_compiler_clang_with_ubsan():
        if is_running_under_asan():
            return (
                "Undefined behavior sanitizer tests are disabled when runing under ASAN"
            )

        # We need to write out the object into a named temp file for inspection.
        outputf = tempfile.NamedTemporaryFile()

        # Try to compile with ubsan turned on.
        if not _compiler_supports(
            lldbplatformutil.getCompiler(),
            "-fsanitize=undefined",
            "int main() { int x = 0; return x / x; }",
            outputf,
        ):
            return "Compiler cannot compile with -fsanitize=undefined"

        # Check that we actually see ubsan instrumentation in the binary.
        cmd = "nm %s" % outputf.name
        with os.popen(cmd) as nm_output:
            if "___ubsan_handle_divrem_overflow" not in nm_output.read():
                return "Division by zero instrumentation is missing"

        # Find the ubsan dylib.
        # FIXME: This check should go away once compiler-rt gains support for __ubsan_on_report.
        cmd = (
            "%s -fsanitize=undefined -x c - -o - -### 2>&1"
            % lldbplatformutil.getCompiler()
        )
        with os.popen(cmd) as cc_output:
            driver_jobs = cc_output.read()
            m = re.search(r'"([^"]+libclang_rt.ubsan_osx_dynamic.dylib)"', driver_jobs)
            if not m:
                return "Could not find the ubsan dylib used by the driver"
            ubsan_dylib = m.group(1)

        # Check that the ubsan dylib has special monitor hooks.
        cmd = "nm -gU %s" % ubsan_dylib
        with os.popen(cmd) as nm_output:
            syms = nm_output.read()
            if "___ubsan_on_report" not in syms:
                return "Missing ___ubsan_on_report"
            if "___ubsan_get_current_report_data" not in syms:
                return "Missing ___ubsan_get_current_report_data"

        # OK, this dylib + compiler works for us.
        return None

    return skipTestIfFn(is_compiler_clang_with_ubsan)(func)


def is_running_under_asan():
    if "ASAN_OPTIONS" in os.environ:
        return "ASAN unsupported"
    return None


def skipUnlessAddressSanitizer(func):
    """Decorate the item to skip test unless Clang -fsanitize=thread is supported."""

    def is_compiler_with_address_sanitizer():
        # Also don't run tests that use address sanitizer inside an
        # address-sanitized LLDB. The tests don't support that
        # configuration.
        if is_running_under_asan():
            return "Address sanitizer tests are disabled when runing under ASAN"

        if lldbplatformutil.getPlatform() == "windows":
            return "ASAN tests not compatible with 'windows'"
        if not _compiler_supports(lldbplatformutil.getCompiler(), "-fsanitize=address"):
            return "Compiler cannot compile with -fsanitize=address"
        return None

    return skipTestIfFn(is_compiler_with_address_sanitizer)(func)


def skipIfAsan(func):
    """Skip this test if the environment is set up to run LLDB *itself* under ASAN."""
    return skipTestIfFn(is_running_under_asan)(func)


def skipUnlessAArch64MTELinuxCompiler(func):
    """Decorate the item to skip test unless MTE is supported by the test compiler."""

    def is_toolchain_with_mte():
        compiler_path = lldbplatformutil.getCompiler()
        compiler = os.path.basename(compiler_path)
        f = tempfile.NamedTemporaryFile()
        if lldbplatformutil.getPlatform() == "windows":
            return "MTE tests are not compatible with 'windows'"

        cmd = "echo 'int main() {}' | %s -x c -o %s -" % (compiler_path, f.name)
        if os.popen(cmd).close() is not None:
            # Cannot compile at all, don't skip the test
            # so that we report the broken compiler normally.
            return None

        # We need the Linux headers and ACLE MTE intrinsics
        test_src = """
            #include <asm/hwcap.h>
            #include <arm_acle.h>
            #ifndef HWCAP2_MTE
            #error
            #endif
            int main() {
                void* ptr = __arm_mte_create_random_tag((void*)(0), 0);
            }"""
        cmd = "echo '%s' | %s -march=armv8.5-a+memtag -x c -o %s -" % (
            test_src,
            compiler_path,
            f.name,
        )
        if os.popen(cmd).close() is not None:
            return "Toolchain does not support MTE"
        return None

    return skipTestIfFn(is_toolchain_with_mte)(func)


def _get_bool_config(key, fail_value=True):
    """
    Returns the current LLDB's build config value.
    :param key The key to lookup in LLDB's build configuration.
    :param fail_value The error value to return when the key can't be found.
           Defaults to true so that if an unknown key is lookup up we rather
           enable more tests (that then fail) than silently skipping them.
    """
    config = lldb.SBDebugger.GetBuildConfiguration()
    value_node = config.GetValueForKey(key)
    return value_node.GetValueForKey("value").GetBooleanValue(fail_value)


def _get_bool_config_skip_if_decorator(key):
    have = _get_bool_config(key)
    return unittest.skipIf(not have, "requires " + key)


def skipIfCursesSupportMissing(func):
    return _get_bool_config_skip_if_decorator("curses")(func)


def skipIfXmlSupportMissing(func):
    return _get_bool_config_skip_if_decorator("xml")(func)


def skipIfEditlineSupportMissing(func):
    return _get_bool_config_skip_if_decorator("editline")(func)


def skipIfEditlineWideCharSupportMissing(func):
    return _get_bool_config_skip_if_decorator("editline_wchar")(func)


def skipIfFBSDVMCoreSupportMissing(func):
    return _get_bool_config_skip_if_decorator("fbsdvmcore")(func)


def skipIfLLVMTargetMissing(target):
    config = lldb.SBDebugger.GetBuildConfiguration()
    targets = config.GetValueForKey("targets").GetValueForKey("value")
    found = False
    for i in range(targets.GetSize()):
        if targets.GetItemAtIndex(i).GetStringValue(99) == target:
            found = True
            break

    return unittest.skipIf(not found, "requires " + target)


# Call sysctl on darwin to see if a specified hardware feature is available on this machine.
def skipUnlessFeature(feature):
    def is_feature_enabled():
        if platform.system() == "Darwin":
            try:
                DEVNULL = open(os.devnull, "w")
                output = subprocess.check_output(
                    ["/usr/sbin/sysctl", feature], stderr=DEVNULL
                ).decode("utf-8")
                # If 'feature: 1' was output, then this feature is available and
                # the test should not be skipped.
                if re.match(r"%s: 1\s*" % feature, output):
                    return None
                else:
                    return "%s is not supported on this system." % feature
            except subprocess.CalledProcessError:
                return "%s is not supported on this system." % feature

    return skipTestIfFn(is_feature_enabled)
