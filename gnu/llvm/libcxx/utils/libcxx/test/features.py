# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

from libcxx.test.dsl import *
from lit.BooleanExpression import BooleanExpression
import re
import shutil
import subprocess
import sys

_isAnyClang = lambda cfg: "__clang__" in compilerMacros(cfg)
_isAppleClang = lambda cfg: "__apple_build_version__" in compilerMacros(cfg)
_isAnyGCC = lambda cfg: "__GNUC__" in compilerMacros(cfg)
_isClang = lambda cfg: _isAnyClang(cfg) and not _isAppleClang(cfg)
_isGCC = lambda cfg: _isAnyGCC(cfg) and not _isAnyClang(cfg)
_isAnyClangOrGCC = lambda cfg: _isAnyClang(cfg) or _isAnyGCC(cfg)
_isClExe = lambda cfg: not _isAnyClangOrGCC(cfg)
_isMSVC = lambda cfg: "_MSC_VER" in compilerMacros(cfg)
_msvcVersion = lambda cfg: (int(compilerMacros(cfg)["_MSC_VER"]) // 100, int(compilerMacros(cfg)["_MSC_VER"]) % 100)

def _getAndroidDeviceApi(cfg):
    return int(
        programOutput(
            cfg,
            r"""
                #include <android/api-level.h>
                #include <stdio.h>
                int main() {
                    printf("%d\n", android_get_device_api_level());
                    return 0;
                }
            """,
        )
    )


def _mingwSupportsModules(cfg):
    # Only mingw headers are known to work with libc++ built as a module,
    # at the moment.
    if not "__MINGW32__" in compilerMacros(cfg):
        return False
    # For mingw headers, check for a version known to support being built
    # as a module.
    return sourceBuilds(
        cfg,
        """
        #include <_mingw_mac.h>
        #if __MINGW64_VERSION_MAJOR < 12
        #error Headers known to be incompatible
        #elif __MINGW64_VERSION_MAJOR == 12
        // The headers were fixed to work with libc++ modules during
        // __MINGW64_VERSION_MAJOR == 12. The headers became compatible
        // with libc++ built as a module in
        // 1652e9241b5d8a5a779c6582b1c3c4f4a7cc66e5 (Apr 2024), but the
        // following commit 8c13b28ace68f2c0094d45121d59a4b951b533ed
        // removed the now unused __mingw_static_ovr define. Use this
        // as indicator for whether we've got new enough headers.
        #ifdef __mingw_static_ovr
        #error Headers too old
        #endif
        #else
        // __MINGW64_VERSION_MAJOR > 12 should be ok.
        #endif
        int main() { return 0; }
        """,
    )


# Lit features are evaluated in order. Some checks may require the compiler detection to have
# run first in order to work properly.
DEFAULT_FEATURES = [
    # gcc-style-warnings detects compilers that understand -Wno-meow flags, unlike MSVC's compiler driver cl.exe.
    Feature(name="gcc-style-warnings", when=_isAnyClangOrGCC),
    Feature(name="cl-style-warnings", when=_isClExe),
    Feature(name="apple-clang", when=_isAppleClang),
    Feature(
        name=lambda cfg: "apple-clang-{__clang_major__}".format(**compilerMacros(cfg)),
        when=_isAppleClang,
    ),
    Feature(
        name=lambda cfg: "apple-clang-{__clang_major__}.{__clang_minor__}".format(**compilerMacros(cfg)),
        when=_isAppleClang,
    ),
    Feature(
        name=lambda cfg: "apple-clang-{__clang_major__}.{__clang_minor__}.{__clang_patchlevel__}".format(**compilerMacros(cfg)),
        when=_isAppleClang,
    ),
    Feature(name="clang", when=_isClang),
    Feature(
        name=lambda cfg: "clang-{__clang_major__}".format(**compilerMacros(cfg)),
        when=_isClang,
    ),
    Feature(
        name=lambda cfg: "clang-{__clang_major__}.{__clang_minor__}".format(**compilerMacros(cfg)),
        when=_isClang,
    ),
    Feature(
        name=lambda cfg: "clang-{__clang_major__}.{__clang_minor__}.{__clang_patchlevel__}".format(**compilerMacros(cfg)),
        when=_isClang,
    ),
    # Note: Due to a GCC bug (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104760), we must disable deprecation warnings
    #       on GCC or spurious diagnostics are issued.
    #
    # TODO:
    # - Enable -Wplacement-new with GCC.
    # - Enable -Wclass-memaccess with GCC.
    Feature(
        name="gcc",
        when=_isGCC,
        actions=[
            AddCompileFlag("-D_LIBCPP_DISABLE_DEPRECATION_WARNINGS"),
            AddCompileFlag("-Wno-placement-new"),
            AddCompileFlag("-Wno-class-memaccess"),
            AddFeature("GCC-ALWAYS_INLINE-FIXME"),
        ],
    ),
    Feature(
        name=lambda cfg: "gcc-{__GNUC__}".format(**compilerMacros(cfg)), when=_isGCC
    ),
    Feature(
        name=lambda cfg: "gcc-{__GNUC__}.{__GNUC_MINOR__}".format(**compilerMacros(cfg)),
        when=_isGCC,
    ),
    Feature(
        name=lambda cfg: "gcc-{__GNUC__}.{__GNUC_MINOR__}.{__GNUC_PATCHLEVEL__}".format(**compilerMacros(cfg)),
        when=_isGCC,
    ),
    Feature(name="msvc", when=_isMSVC),
    Feature(name=lambda cfg: "msvc-{}".format(*_msvcVersion(cfg)), when=_isMSVC),
    Feature(name=lambda cfg: "msvc-{}.{}".format(*_msvcVersion(cfg)), when=_isMSVC),

    Feature(
        name="thread-safety",
        when=lambda cfg: hasCompileFlag(cfg, "-Werror=thread-safety"),
        actions=[AddCompileFlag("-Werror=thread-safety")],
    ),
    Feature(
        name="diagnose-if-support",
        when=lambda cfg: hasCompileFlag(cfg, "-Wuser-defined-warnings"),
        actions=[AddCompileFlag("-Wuser-defined-warnings")],
    ),
    # Tests to validate whether the compiler has a way to set the maximum number
    # of steps during constant evaluation. Since the flag differs per compiler
    # store the "valid" flag as a feature. This allows passing the proper compile
    # flag to the compiler:
    # // ADDITIONAL_COMPILE_FLAGS(has-fconstexpr-steps): -fconstexpr-steps=12345678
    # // ADDITIONAL_COMPILE_FLAGS(has-fconstexpr-ops-limit): -fconstexpr-ops-limit=12345678
    Feature(
        name="has-fconstexpr-steps",
        when=lambda cfg: hasCompileFlag(cfg, "-fconstexpr-steps=1"),
    ),
    Feature(
        name="has-fconstexpr-ops-limit",
        when=lambda cfg: hasCompileFlag(cfg, "-fconstexpr-ops-limit=1"),
    ),
    Feature(name="has-fblocks", when=lambda cfg: hasCompileFlag(cfg, "-fblocks")),
    Feature(
        name="-fsized-deallocation",
        when=lambda cfg: hasCompileFlag(cfg, "-fsized-deallocation"),
    ),
    Feature(
        name="-faligned-allocation",
        when=lambda cfg: hasCompileFlag(cfg, "-faligned-allocation"),
    ),
    Feature(
        name="fdelayed-template-parsing",
        when=lambda cfg: hasCompileFlag(cfg, "-fdelayed-template-parsing"),
    ),
    Feature(
        name="has-fobjc-arc",
        when=lambda cfg: hasCompileFlag(cfg, "-xobjective-c++ -fobjc-arc")
        and sys.platform.lower().strip() == "darwin",
    ),  # TODO: this doesn't handle cross-compiling to Apple platforms.
    Feature(
        name="objective-c++",
        when=lambda cfg: hasCompileFlag(cfg, "-xobjective-c++ -fobjc-arc"),
    ),
    Feature(
        name="verify-support",
        when=lambda cfg: hasCompileFlag(cfg, "-Xclang -verify-ignore-unexpected"),
    ),
    Feature(
        name="add-latomic-workaround",  # https://github.com/llvm/llvm-project/issues/73361
        when=lambda cfg: sourceBuilds(
            cfg, "int main(int, char**) { return 0; }", ["-latomic"]
        ),
        actions=[AddLinkFlag("-latomic")],
    ),
    Feature(
        name="has-64-bit-atomics",
        when=lambda cfg: sourceBuilds(
            cfg,
            """
            #include <atomic>
            struct Large { char storage[64/8]; };
            std::atomic<Large> x;
            int main(int, char**) { (void)x.load(); (void)x.is_lock_free(); return 0; }
          """,
        ),
    ),
    Feature(
        name="has-1024-bit-atomics",
        when=lambda cfg: sourceBuilds(
            cfg,
            """
            #include <atomic>
            struct Large { char storage[1024/8]; };
            std::atomic<Large> x;
            int main(int, char**) { (void)x.load(); (void)x.is_lock_free(); return 0; }
          """,
        ),
    ),
    # Tests that require 64-bit architecture
    Feature(
        name="32-bit-pointer",
        when=lambda cfg: sourceBuilds(
            cfg,
            """
            int main(int, char**) {
              static_assert(sizeof(void *) == 4);
            }
          """,
        ),
    ),
    # Check for a Windows UCRT bug (fixed in UCRT/Windows 10.0.20348.0):
    # https://developercommunity.visualstudio.com/t/utf-8-locales-break-ctype-functions-for-wchar-type/1653678
    Feature(
        name="win32-broken-utf8-wchar-ctype",
        when=lambda cfg: not "_LIBCPP_HAS_NO_LOCALIZATION" in compilerMacros(cfg)
        and "_WIN32" in compilerMacros(cfg)
        and not programSucceeds(
            cfg,
            """
            #include <locale.h>
            #include <wctype.h>
            int main(int, char**) {
              setlocale(LC_ALL, "en_US.UTF-8");
              return towlower(L'\\xDA') != L'\\xFA';
            }
          """,
        ),
    ),
    # Check for a Windows UCRT bug (fixed in UCRT/Windows 10.0.19041.0).
    # https://developercommunity.visualstudio.com/t/printf-formatting-with-g-outputs-too/1660837
    Feature(
        name="win32-broken-printf-g-precision",
        when=lambda cfg: "_WIN32" in compilerMacros(cfg)
        and not programSucceeds(
            cfg,
            """
            #include <stdio.h>
            #include <string.h>
            int main(int, char**) {
              char buf[100];
              snprintf(buf, sizeof(buf), "%#.*g", 0, 0.0);
              return strcmp(buf, "0.");
            }
          """,
        ),
    ),
    # Check for Glibc < 2.27, where the ru_RU.UTF-8 locale had
    # mon_decimal_point == ".", which our tests don't handle.
    Feature(
        name="glibc-old-ru_RU-decimal-point",
        when=lambda cfg: not "_LIBCPP_HAS_NO_LOCALIZATION" in compilerMacros(cfg)
        and not programSucceeds(
            cfg,
            """
            #include <locale.h>
            #include <string.h>
            int main(int, char**) {
              setlocale(LC_ALL, "ru_RU.UTF-8");
              return strcmp(localeconv()->mon_decimal_point, ",");
            }
          """,
        ),
    ),
    Feature(
        name="has-unix-headers",
        when=lambda cfg: sourceBuilds(
            cfg,
            """
            #include <unistd.h>
            #include <sys/wait.h>
            int main(int, char**) {
              int fd[2];
              return pipe(fd);
            }
          """,
        ),
    ),
    # Whether Bash can run on the executor.
    # This is not always the case, for example when running on embedded systems.
    #
    # For the corner case of bash existing, but it being missing in the path
    # set in %{exec} as "--env PATH=one-single-dir", the executor does find
    # and executes bash, but bash then can't find any other common shell
    # utilities. Test executing "bash -c 'bash --version'" to see if bash
    # manages to find binaries to execute.
    Feature(
        name="executor-has-no-bash",
        when=lambda cfg: runScriptExitCode(cfg, ["%{exec} bash -c 'bash --version'"]) != 0,
    ),
    # Whether module support for the platform is available.
    Feature(
        name="has-no-cxx-module-support",
        # The libc of these platforms have functions with internal linkage.
        # This is not allowed per C11 7.1.2 Standard headers/6
        #  Any declaration of a library function shall have external linkage.
        when=lambda cfg: "__ANDROID__" in compilerMacros(cfg)
        or "__FreeBSD__" in compilerMacros(cfg)
        or ("_WIN32" in compilerMacros(cfg) and not _mingwSupportsModules(cfg))
        or platform.system().lower().startswith("aix")
        # Avoid building on platforms that don't support modules properly.
        or not hasCompileFlag(cfg, "-Wno-reserved-module-identifier")
        or not sourceBuilds(
            cfg,
            """
            export module test;
            int main(int, char**) { return 0; }
          """,
        ),
    ),
    # The time zone validation tests compare the output of zdump against the
    # output generated by <chrono>'s time zone support.
    Feature(
        name="has-no-zdump",
        when=lambda cfg: runScriptExitCode(cfg, ["zdump --version"]) != 0,
    ),
]

# Deduce and add the test features that that are implied by the #defines in
# the <__config> header.
#
# For each macro of the form `_LIBCPP_XXX_YYY_ZZZ` defined below that
# is defined after including <__config>, add a Lit feature called
# `libcpp-xxx-yyy-zzz`. When a macro is defined to a specific value
# (e.g. `_LIBCPP_ABI_VERSION=2`), the feature is `libcpp-xxx-yyy-zzz=<value>`.
#
# Note that features that are more strongly tied to libc++ are named libcpp-foo,
# while features that are more general in nature are not prefixed with 'libcpp-'.
macros = {
    "_LIBCPP_HAS_NO_MONOTONIC_CLOCK": "no-monotonic-clock",
    "_LIBCPP_HAS_NO_THREADS": "no-threads",
    "_LIBCPP_HAS_THREAD_API_EXTERNAL": "libcpp-has-thread-api-external",
    "_LIBCPP_HAS_THREAD_API_PTHREAD": "libcpp-has-thread-api-pthread",
    "_LIBCPP_NO_VCRUNTIME": "libcpp-no-vcruntime",
    "_LIBCPP_ABI_VERSION": "libcpp-abi-version",
    "_LIBCPP_ABI_BOUNDED_ITERATORS": "libcpp-has-abi-bounded-iterators",
    "_LIBCPP_ABI_BOUNDED_ITERATORS_IN_STRING": "libcpp-has-abi-bounded-iterators-in-string",
    "_LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR": "libcpp-has-abi-bounded-iterators-in-vector",
    "_LIBCPP_DEPRECATED_ABI_DISABLE_PAIR_TRIVIAL_COPY_CTOR": "libcpp-deprecated-abi-disable-pair-trivial-copy-ctor",
    "_LIBCPP_HAS_NO_FILESYSTEM": "no-filesystem",
    "_LIBCPP_HAS_NO_RANDOM_DEVICE": "no-random-device",
    "_LIBCPP_HAS_NO_LOCALIZATION": "no-localization",
    "_LIBCPP_HAS_NO_WIDE_CHARACTERS": "no-wide-characters",
    "_LIBCPP_HAS_NO_TIME_ZONE_DATABASE": "no-tzdb",
    "_LIBCPP_HAS_NO_UNICODE": "libcpp-has-no-unicode",
    "_LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS": "libcpp-has-no-availability-markup",
    "_LIBCPP_PSTL_BACKEND_LIBDISPATCH": "libcpp-pstl-backend-libdispatch",
}
for macro, feature in macros.items():
    DEFAULT_FEATURES.append(
        Feature(
            name=lambda cfg, m=macro, f=feature: f + ("={}".format(compilerMacros(cfg)[m]) if compilerMacros(cfg)[m] else ""),
            when=lambda cfg, m=macro: m in compilerMacros(cfg),
        )
    )


# Mapping from canonical locale names (used in the tests) to possible locale
# names on various systems. Each locale is considered supported if any of the
# alternative names is supported.
locales = {
    "en_US.UTF-8": ["en_US.UTF-8", "en_US.utf8", "English_United States.1252"],
    "fr_FR.UTF-8": ["fr_FR.UTF-8", "fr_FR.utf8", "French_France.1252"],
    "ja_JP.UTF-8": ["ja_JP.UTF-8", "ja_JP.utf8", "Japanese_Japan.923"],
    "ru_RU.UTF-8": ["ru_RU.UTF-8", "ru_RU.utf8", "Russian_Russia.1251"],
    "zh_CN.UTF-8": ["zh_CN.UTF-8", "zh_CN.utf8", "Chinese_China.936"],
    "fr_CA.ISO8859-1": ["fr_CA.ISO8859-1", "French_Canada.1252"],
    "cs_CZ.ISO8859-2": ["cs_CZ.ISO8859-2", "Czech_Czech Republic.1250"],
}
for locale, alts in locales.items():
    # Note: Using alts directly in the lambda body here will bind it to the value at the
    # end of the loop. Assigning it to a default argument works around this issue.
    DEFAULT_FEATURES.append(
        Feature(
            name="locale.{}".format(locale),
            when=lambda cfg, alts=alts: hasAnyLocale(cfg, alts),
        )
    )


# Add features representing the target platform name: darwin, linux, windows, etc...
DEFAULT_FEATURES += [
    Feature(name="darwin", when=lambda cfg: "__APPLE__" in compilerMacros(cfg)),
    Feature(name="windows", when=lambda cfg: "_WIN32" in compilerMacros(cfg)),
    Feature(
        name="windows-dll",
        when=lambda cfg: "_WIN32" in compilerMacros(cfg)
        and sourceBuilds(
            cfg,
            """
            #include <iostream>
            int main(int, char**) { return 0; }
          """,
        )
        and programSucceeds(
            cfg,
            """
            #include <iostream>
            #include <windows.h>
            #include <winnt.h>
            int main(int, char**) {
              // Get a pointer to a data member that gets linked from the C++
              // library. This must be a data member (functions can get
              // thunk inside the calling executable), and must not be
              // something that is defined inline in headers.
              void *ptr = &std::cout;
              // Get a handle to the current main executable.
              void *exe = GetModuleHandle(NULL);
              // The handle points at the PE image header. Navigate through
              // the header structure to find the size of the PE image (the
              // executable).
              PIMAGE_DOS_HEADER dosheader = (PIMAGE_DOS_HEADER)exe;
              PIMAGE_NT_HEADERS ntheader = (PIMAGE_NT_HEADERS)((BYTE *)dosheader + dosheader->e_lfanew);
              PIMAGE_OPTIONAL_HEADER peheader = &ntheader->OptionalHeader;
              void *exeend = (BYTE*)exe + peheader->SizeOfImage;
              // Check if the tested pointer - the data symbol from the
              // C++ library - is located within the exe.
              if (ptr >= exe && ptr <= exeend)
                return 1;
              // Return success if it was outside of the executable, i.e.
              // loaded from a DLL.
              return 0;
            }
          """,
        ),
        actions=[AddCompileFlag("-DTEST_WINDOWS_DLL")],
    ),
    Feature(name="linux", when=lambda cfg: "__linux__" in compilerMacros(cfg)),
    Feature(name="android", when=lambda cfg: "__ANDROID__" in compilerMacros(cfg)),
    Feature(
        name=lambda cfg: "android-device-api={}".format(_getAndroidDeviceApi(cfg)),
        when=lambda cfg: "__ANDROID__" in compilerMacros(cfg),
    ),
    Feature(
        name="LIBCXX-ANDROID-FIXME",
        when=lambda cfg: "__ANDROID__" in compilerMacros(cfg),
    ),
    Feature(name="netbsd", when=lambda cfg: "__NetBSD__" in compilerMacros(cfg)),
    Feature(name="freebsd", when=lambda cfg: "__FreeBSD__" in compilerMacros(cfg)),
    Feature(
        name="LIBCXX-FREEBSD-FIXME",
        when=lambda cfg: "__FreeBSD__" in compilerMacros(cfg),
    ),
    Feature(
        name="LIBCXX-PICOLIBC-FIXME",
        when=lambda cfg: sourceBuilds(
            cfg,
            """
            #include <string.h>
            #ifndef __PICOLIBC__
            #error not picolibc
            #endif
            int main(int, char**) { return 0; }
          """,
        ),
    ),
    Feature(
        name="can-create-symlinks",
        when=lambda cfg: "_WIN32" not in compilerMacros(cfg)
        or programSucceeds(
            cfg,
            # Creation of symlinks require elevated privileges on Windows unless
            # Windows developer mode is enabled.
            """
            #include <stdio.h>
            #include <windows.h>
            int main() {
              CHAR tempDirPath[MAX_PATH];
              DWORD tempPathRet = GetTempPathA(MAX_PATH, tempDirPath);
              if (tempPathRet == 0 || tempPathRet > MAX_PATH) {
                return 1;
              }

              CHAR tempFilePath[MAX_PATH];
              UINT uRetVal = GetTempFileNameA(
                tempDirPath,
                "cxx", // Prefix
                0, // Unique=0 also implies file creation.
                tempFilePath);
              if (uRetVal == 0) {
                return 1;
              }

              CHAR symlinkFilePath[MAX_PATH];
              int ret = sprintf_s(symlinkFilePath, MAX_PATH, "%s_symlink", tempFilePath);
              if (ret == -1) {
                DeleteFileA(tempFilePath);
                return 1;
              }

              // Requires either administrator, or developer mode enabled.
              BOOL bCreatedSymlink = CreateSymbolicLinkA(symlinkFilePath,
                tempFilePath,
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
              if (!bCreatedSymlink) {
                DeleteFileA(tempFilePath);
                return 1;
              }

              DeleteFileA(tempFilePath);
              DeleteFileA(symlinkFilePath);
              return 0;
            }
            """,
        ),
    ),
]

# Add features representing the build host platform name.
# The build host could differ from the target platform for cross-compilation.
DEFAULT_FEATURES += [
    Feature(name="buildhost={}".format(sys.platform.lower().strip())),
    # sys.platform can often be represented by a "sub-system", such as 'win32', 'cygwin', 'mingw', freebsd13 & etc.
    # We define a consolidated feature on a few platforms.
    Feature(
        name="buildhost=windows",
        when=lambda cfg: platform.system().lower().startswith("windows"),
    ),
    Feature(
        name="buildhost=freebsd",
        when=lambda cfg: platform.system().lower().startswith("freebsd"),
    ),
    Feature(
        name="buildhost=aix",
        when=lambda cfg: platform.system().lower().startswith("aix"),
    ),
]

# Detect whether GDB is on the system, has Python scripting and supports
# adding breakpoint commands. If so add a substitution to access it.
def check_gdb(cfg):
    gdb_path = shutil.which("gdb")
    if gdb_path is None:
        return False

    # Check that we can set breakpoint commands, which was added in 8.3.
    # Using the quit command here means that gdb itself exits, not just
    # the "python <...>" command.
    test_src = """\
try:
  gdb.Breakpoint(\"main\").commands=\"foo\"
except AttributeError:
  gdb.execute(\"quit 1\")
gdb.execute(\"quit\")"""

    try:
        stdout = subprocess.check_output(
            [gdb_path, "-ex", "python " + test_src, "--batch"],
            stderr=subprocess.DEVNULL,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        # We can't set breakpoint commands
        return False

    # Check we actually ran the Python
    return not "Python scripting is not supported" in stdout


DEFAULT_FEATURES += [
    Feature(
        name="host-has-gdb-with-python",
        when=check_gdb,
        actions=[AddSubstitution("%{gdb}", lambda cfg: shutil.which("gdb"))],
    )
]

# Helpers to define correspondances between LLVM versions and vendor system versions.
# Those are used for backdeployment features below, do not use directly in tests.
DEFAULT_FEATURES += [
    Feature(
        name="_target-has-llvm-17",
        when=lambda cfg: BooleanExpression.evaluate(
            "target={{.+}}-apple-macosx{{14.[4-9](.0)?}} || target={{.+}}-apple-macosx{{1[5-9]([.].+)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-16",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-17 || target={{.+}}-apple-macosx{{14.[0-3](.0)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-15",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-16 || target={{.+}}-apple-macosx{{13.[4-9](.0)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-14",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-15",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-13",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-14 || target={{.+}}-apple-macosx{{13.[0-3](.0)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-12",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-13 || target={{.+}}-apple-macosx{{12.[3-9](.0)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-11",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-12 || target={{.+}}-apple-macosx{{(11.[0-9]|12.[0-2])(.0)?}}",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-10",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-11",
            cfg.available_features,
        ),
    ),
    Feature(
        name="_target-has-llvm-9",
        when=lambda cfg: BooleanExpression.evaluate(
            "_target-has-llvm-10 || target={{.+}}-apple-macosx{{10.15(.0)?}}",
            cfg.available_features,
        ),
    ),
]

# Define features for back-deployment testing.
#
# These features can be used to XFAIL tests that fail when deployed on (or compiled
# for) an older system. For example, if a test exhibits a bug in the libc++ on a
# particular system version, or if it uses a symbol that is not available on an
# older version of the dylib, it can be marked as XFAIL with these features.
#
# We have two families of Lit features:
#
# The first one is `using-built-library-before-llvm-XYZ`. These features encode the
# fact that the test suite is being *run* against a version of the shared/static library
# that predates LLVM version XYZ. This is useful to represent the use case of compiling
# a program against the latest libc++ but then deploying it and running it on an older
# system with an older version of the (usually shared) library.
#
# This feature is built up using the target triple passed to the compiler and the
# `stdlib=system` Lit feature, which encodes that we're running against the same library
# as described by the target triple.
#
# The second set of features is `availability-<FEATURE>-missing`. This family of Lit
# features encodes the presence of availability markup in the libc++ headers. This is
# useful to check that a test fails specifically when compiled for a given deployment
# target, such as when testing availability markup where we want to make sure that
# using the annotated facility on a deployment target that doesn't support it will fail
# at compile time. This can be achieved by creating a `.verify.cpp` test that checks for
# the right errors and marking the test as `REQUIRES: availability-<FEATURE>-missing`.
#
# This feature is built up using the presence of availability markup detected inside
# __config, the flavor of the library being tested and the target triple passed to the
# compiler.
#
# Note that both families of Lit features are similar but different in important ways.
# For example, tests for availability markup should be expected to produce diagnostics
# regardless of whether we're running against a system library, as long as we're using
# a libc++ flavor that enables availability markup. Similarly, a test could fail when
# run against the system library of an older version of FreeBSD, even though FreeBSD
# doesn't provide availability markup at the time of writing this.
for version in ("9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19"):
    DEFAULT_FEATURES.append(
        Feature(
            name="using-built-library-before-llvm-{}".format(version),
            when=lambda cfg, v=version: BooleanExpression.evaluate(
                "stdlib=system && !_target-has-llvm-{}".format(v),
                cfg.available_features,
            ),
        )
    )

DEFAULT_FEATURES += [
    # Tests that require std::filesystem support in the built library
    Feature(
        name="availability-filesystem-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-9)",
            cfg.available_features,
        ),
    ),
    # Tests that require the C++20 synchronization library (P1135R6 implemented by https://llvm.org/D68480) in the built library
    Feature(
        name="availability-synchronization_library-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-11)",
            cfg.available_features,
        ),
    ),
    # Tests that require https://wg21.link/P0482 support in the built library
    Feature(
        name="availability-char8_t_support-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-12)",
            cfg.available_features,
        ),
    ),
    # Tests that require __libcpp_verbose_abort support in the built library
    Feature(
        name="availability-verbose_abort-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-13)",
            cfg.available_features,
        ),
    ),
    # Tests that require std::pmr support in the built library
    Feature(
        name="availability-pmr-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-13)",
            cfg.available_features,
        ),
    ),
    # Tests that require std::to_chars(floating-point) in the built library
    Feature(
        name="availability-fp_to_chars-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-14)",
            cfg.available_features,
        ),
    ),
    # Tests that require support for <print> and std::print in <ostream> in the built library.
    Feature(
        name="availability-print-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-18)",
            cfg.available_features,
        ),
    ),
    # Tests that require time zone database support in the built library
    Feature(
        name="availability-tzdb-missing",
        when=lambda cfg: BooleanExpression.evaluate(
            "!libcpp-has-no-availability-markup && (stdlib=apple-libc++ && !_target-has-llvm-19)",
            cfg.available_features,
        ),
    ),
]
