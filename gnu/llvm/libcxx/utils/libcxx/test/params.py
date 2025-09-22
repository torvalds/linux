# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
import sys
import re
import shlex
from pathlib import Path

from libcxx.test.dsl import *
from libcxx.test.features import _isClang, _isAppleClang, _isGCC, _isMSVC


_warningFlags = [
    "-Werror",
    "-Wall",
    "-Wctad-maybe-unsupported",
    "-Wextra",
    "-Wshadow",
    "-Wundef",
    "-Wunused-template",
    "-Wno-unused-command-line-argument",
    "-Wno-attributes",
    "-Wno-pessimizing-move",
    "-Wno-noexcept-type",
    "-Wno-aligned-allocation-unavailable",
    "-Wno-atomic-alignment",
    "-Wno-reserved-module-identifier",
    '-Wdeprecated-copy',
    '-Wdeprecated-copy-dtor',
    # GCC warns about places where we might want to add sized allocation/deallocation
    # functions, but we know better what we're doing/testing in the test suite.
    "-Wno-sized-deallocation",
    # Turn off warnings about user-defined literals with reserved suffixes. Those are
    # just noise since we are testing the Standard Library itself.
    "-Wno-literal-suffix",  # GCC
    "-Wno-user-defined-literals",  # Clang
    # GCC warns about this when TEST_IS_CONSTANT_EVALUATED is used on a non-constexpr
    # function. (This mostly happens in C++11 mode.)
    # TODO(mordante) investigate a solution for this issue.
    "-Wno-tautological-compare",
    # -Wstringop-overread and -Wstringop-overflow seem to be a bit buggy currently
    "-Wno-stringop-overread",
    "-Wno-stringop-overflow",
    # These warnings should be enabled in order to support the MSVC
    # team using the test suite; They enable the warnings below and
    # expect the test suite to be clean.
    "-Wsign-compare",
    "-Wunused-variable",
    "-Wunused-parameter",
    "-Wunreachable-code",
    "-Wno-unused-local-typedef",

    # Disable warnings for extensions used in C++03
    "-Wno-local-type-template-args",
    "-Wno-c++11-extensions",

    # TODO(philnik) This fails with the PSTL.
    "-Wno-unknown-pragmas",
    # Don't fail compilation in case the compiler fails to perform the requested
    # loop vectorization.
    "-Wno-pass-failed",

    # TODO: Find out why GCC warns in lots of places (is this a problem with always_inline?)
    "-Wno-dangling-reference",
    "-Wno-mismatched-new-delete",
    "-Wno-redundant-move",

    # This doesn't make sense in real code, but we have to test it because the standard requires us to not break
    "-Wno-self-move",
]

_allStandards = ["c++03", "c++11", "c++14", "c++17", "c++20", "c++23", "c++26"]


def getStdFlag(cfg, std):
    if hasCompileFlag(cfg, "-std=" + std):
        return "-std=" + std
    # TODO(LLVM-19) Remove the fallbacks needed for Clang 16.
    fallbacks = {
        "c++23": "c++2b",
    }
    if std in fallbacks and hasCompileFlag(cfg, "-std=" + fallbacks[std]):
        return "-std=" + fallbacks[std]
    return None


def getDefaultStdValue(cfg):
    viable = [s for s in reversed(_allStandards) if getStdFlag(cfg, s)]

    if not viable:
        raise RuntimeError(
            "Unable to successfully detect the presence of any -std=c++NN flag. This likely indicates an issue with your compiler."
        )

    return viable[0]


def getSpeedOptimizationFlag(cfg):
    if _isClang(cfg) or _isAppleClang(cfg) or _isGCC(cfg):
        return "-O3"
    elif _isMSVC(cfg):
        return "/O2"
    else:
        raise RuntimeError(
            "Can't figure out what compiler is used in the configuration"
        )


def getSizeOptimizationFlag(cfg):
    if _isClang(cfg) or _isAppleClang(cfg) or _isGCC(cfg):
        return "-Os"
    elif _isMSVC(cfg):
        return "/O1"
    else:
        raise RuntimeError(
            "Can't figure out what compiler is used in the configuration"
        )


def testClangTidy(cfg, version, executable):
    try:
        if version in commandOutput(cfg, [f"{executable} --version"]):
            return executable
    except ConfigurationRuntimeError:
        return None


def getSuitableClangTidy(cfg):
    # If we didn't build the libcxx-tidy plugin via CMake, we can't run the clang-tidy tests.
    if (
        runScriptExitCode(
            cfg, ["stat %{test-tools-dir}/clang_tidy_checks/libcxx-tidy.plugin"]
        )
        != 0
    ):
        return None

    version = "{__clang_major__}.{__clang_minor__}.{__clang_patchlevel__}".format(
        **compilerMacros(cfg)
    )
    exe = testClangTidy(
        cfg, version, "clang-tidy-{__clang_major__}".format(**compilerMacros(cfg))
    )

    if not exe:
        exe = testClangTidy(cfg, version, "clang-tidy")

    return exe


# fmt: off
DEFAULT_PARAMETERS = [
    Parameter(
        name="compiler",
        type=str,
        help="The path of the compiler to use for testing.",
        actions=lambda cxx: [
            AddSubstitution("%{cxx}", shlex.quote(cxx)),
        ],
    ),
    Parameter(
        name="target_triple",
        type=str,
        help="The target triple to compile the test suite for. This must be "
        "compatible with the target that the tests will be run on.",
        actions=lambda triple: filter(
            None,
            [
                AddFeature("target={}".format(triple)),
                AddFlagIfSupported("--target={}".format(triple)),
                AddSubstitution("%{triple}", triple),
            ],
        ),
    ),
    Parameter(
        name="std",
        choices=_allStandards,
        type=str,
        help="The version of the standard to compile the test suite with.",
        default=lambda cfg: getDefaultStdValue(cfg),
        actions=lambda std: [
            AddFeature(std),
            AddSubstitution("%{cxx_std}", re.sub(r"\+", "x", std)),
            AddCompileFlag(lambda cfg: getStdFlag(cfg, std)),
        ],
    ),
    Parameter(
        name="optimization",
        choices=["none", "speed", "size"],
        type=str,
        help="The optimization level to use when compiling the test suite.",
        default="none",
        actions=lambda opt: filter(None, [
            AddCompileFlag(lambda cfg: getSpeedOptimizationFlag(cfg)) if opt == "speed" else None,
            AddCompileFlag(lambda cfg: getSizeOptimizationFlag(cfg)) if opt == "size" else None,
            AddFeature(f'optimization={opt}'),
        ]),
    ),
    Parameter(
        name="enable_modules",
        choices=["none", "clang", "clang-lsv"],
        type=str,
        help="Whether to build the test suite with modules enabled. "
             "Select `clang` for Clang modules, and 'clang-lsv' for Clang modules with Local Submodule Visibility.",
        default="none",
        actions=lambda modules: filter(None, [
            AddFeature("clang-modules-build")           if modules in ("clang", "clang-lsv") else None,

            # Note: AppleClang disregards -fmodules entirely when compiling C++, so we also pass -fcxx-modules
            #       to enable modules for C++.
            AddCompileFlag("-fmodules -fcxx-modules")   if modules in ("clang", "clang-lsv") else None,

            # Note: We use a custom modules cache path to make sure that we don't reuse
            #       the default one, which can be shared across CI builds with different
            #       configurations.
            AddCompileFlag(lambda cfg: f"-fmodules-cache-path={cfg.test_exec_root}/ModuleCache") if modules in ("clang", "clang-lsv") else None,

            AddCompileFlag("-Xclang -fmodules-local-submodule-visibility") if modules == "clang-lsv" else None,
        ])
    ),
    Parameter(
        name="enable_exceptions",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable exceptions when compiling the test suite.",
        actions=lambda exceptions: [] if exceptions else [
            AddFeature("no-exceptions"),
            AddCompileFlag("-fno-exceptions")
        ],
    ),
    Parameter(
        name="enable_rtti",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable RTTI when compiling the test suite.",
        actions=lambda rtti: [] if rtti else [
            AddFeature("no-rtti"),
            AddCompileFlag("-fno-rtti")
        ],
    ),
    Parameter(
        name="stdlib",
        choices=["llvm-libc++", "apple-libc++", "libstdc++", "msvc"],
        type=str,
        default="llvm-libc++",
        help="""The C++ Standard Library implementation being tested.

                 Note that this parameter can also be used to encode different 'flavors' of the same
                 standard library, such as libc++ as shipped by a different vendor, if it has different
                 properties worth testing.

                 The Standard libraries currently supported are:
                 - llvm-libc++: The 'upstream' libc++ as shipped with LLVM.
                 - apple-libc++: libc++ as shipped by Apple. This is basically like the LLVM one, but
                                 there are a few differences like installation paths, the use of
                                 universal dylibs and the existence of availability markup.
                 - libstdc++: The GNU C++ library typically shipped with GCC.
                 - msvc: The Microsoft implementation of the C++ Standard Library.
                """,
        actions=lambda stdlib: filter(
            None,
            [
                AddFeature("stdlib={}".format(stdlib)),
                # Also add an umbrella feature 'stdlib=libc++' for all flavors of libc++, to simplify
                # the test suite.
                AddFeature("stdlib=libc++") if re.match(r".+-libc\+\+", stdlib) else None,
            ],
        ),
    ),
    Parameter(
        name="using_system_stdlib",
        choices=[True, False],
        type=bool,
        default=False,
        help="""Whether the Standard Library being tested is the one that shipped with the system by default.

                This is different from the 'stdlib' parameter, which describes the flavor of libc++ being
                tested. 'using_system_stdlib' describes whether the target system passed with 'target_triple'
                also corresponds to the version of the library being tested.
             """,
        actions=lambda is_system: [AddFeature("stdlib=system")] if is_system else [],
    ),
    Parameter(
        name="enable_warnings",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable warnings when compiling the test suite.",
        actions=lambda warnings: [] if not warnings else
            [AddOptionalWarningFlag(w) for w in _warningFlags] +
            [AddCompileFlag("-D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER")],
    ),
    Parameter(
        name="use_sanitizer",
        choices=[
            "",
            "Address",
            "HWAddress",
            "Undefined",
            "Memory",
            "MemoryWithOrigins",
            "Thread",
            "DataFlow",
            "Leaks",
        ],
        type=str,
        default="",
        help="An optional sanitizer to enable when building and running the test suite.",
        actions=lambda sanitizer: filter(
            None,
            [
                AddFlag("-g -fno-omit-frame-pointer") if sanitizer else None,

                AddFlag("-fsanitize=undefined -fno-sanitize=float-divide-by-zero -fno-sanitize-recover=all") if sanitizer == "Undefined" else None,
                AddFeature("ubsan")                                                                          if sanitizer == "Undefined" else None,

                AddFlag("-fsanitize=address") if sanitizer == "Address" else None,
                AddFeature("asan")            if sanitizer == "Address" else None,

                AddFlag("-fsanitize=hwaddress") if sanitizer == "HWAddress" else None,
                AddFeature("hwasan")            if sanitizer == "HWAddress" else None,

                AddFlag("-fsanitize=memory")               if sanitizer in ["Memory", "MemoryWithOrigins"] else None,
                AddFeature("msan")                         if sanitizer in ["Memory", "MemoryWithOrigins"] else None,
                AddFlag("-fsanitize-memory-track-origins") if sanitizer == "MemoryWithOrigins" else None,

                AddFlag("-fsanitize=thread") if sanitizer == "Thread" else None,
                AddFeature("tsan")           if sanitizer == "Thread" else None,

                AddFlag("-fsanitize=dataflow") if sanitizer == "DataFlow" else None,
                AddFlag("-fsanitize=leaks")    if sanitizer == "Leaks" else None,

                AddFeature("sanitizer-new-delete") if sanitizer in ["Address", "HWAddress", "Memory", "MemoryWithOrigins", "Thread"] else None,
                AddFeature("lsan") if sanitizer in ["Address", "HWAddress", "Leaks"] else None,
            ]
        )
    ),
    Parameter(
        name="enable_experimental",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable tests for experimental C++ Library features.",
        actions=lambda experimental: [
            # When linking in MSVC mode via the Clang driver, a -l<foo>
            # maps to <foo>.lib, so we need to use -llibc++experimental here
            # to make it link against the static libc++experimental.lib.
            # We can't check for the feature 'msvc' in available_features
            # as those features are added after processing parameters.
            AddFeature("c++experimental"),
            PrependLinkFlag(lambda cfg: "-llibc++experimental" if _isMSVC(cfg) else "-lc++experimental"),
            AddCompileFlag("-D_LIBCPP_ENABLE_EXPERIMENTAL"),
        ]
        if experimental
        else [
            AddFeature("libcpp-has-no-incomplete-pstl"),
            AddFeature("libcpp-has-no-experimental-stop_token"),
            AddFeature("libcpp-has-no-experimental-tzdb"),
            AddFeature("libcpp-has-no-experimental-syncstream"),
        ],
    ),
    Parameter(
        name="long_tests",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable tests that take longer to run. This can be useful when running on a very slow device.",
        actions=lambda enabled: [] if not enabled else [AddFeature("long_tests")],
    ),
    Parameter(
        name="large_tests",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable tests that use a lot of memory. This can be useful when running on a device with limited amounts of memory.",
        actions=lambda enabled: [] if not enabled else [AddFeature("large_tests")],
    ),
    Parameter(
        name="hardening_mode",
        choices=["none", "fast", "extensive", "debug", "undefined"],
        type=str,
        default="undefined",
        help="Whether to enable one of the hardening modes when compiling the test suite. This is only "
        "meaningful when running the tests against libc++. By default, no hardening mode is specified "
        "so the default hardening mode of the standard library will be used (if any).",
        actions=lambda hardening_mode: filter(
            None,
            [
                AddCompileFlag("-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE")      if hardening_mode == "none" else None,
                AddCompileFlag("-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST")      if hardening_mode == "fast" else None,
                AddCompileFlag("-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE") if hardening_mode == "extensive" else None,
                AddCompileFlag("-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")     if hardening_mode == "debug" else None,
                AddFeature("libcpp-hardening-mode={}".format(hardening_mode))               if hardening_mode != "undefined" else None,
            ],
        ),
    ),
    Parameter(
        name="additional_features",
        type=list,
        default=[],
        help="A comma-delimited list of additional features that will be enabled when running the tests. "
        "This should be used sparingly since specifying ad-hoc features manually is error-prone and "
        "brittle in the long run as changes are made to the test suite.",
        actions=lambda features: [AddFeature(f) for f in features],
    ),
    Parameter(
        name="enable_transitive_includes",
        choices=[True, False],
        type=bool,
        default=True,
        help="Whether to enable backwards-compatibility transitive includes when running the tests. This "
        "is provided to ensure that the trimmed-down version of libc++ does not bit-rot in between "
        "points at which we bulk-remove transitive includes.",
        actions=lambda enabled: [] if enabled else [
            AddFeature("transitive-includes-disabled"),
            AddCompileFlag("-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES"),
        ],
    ),
    Parameter(
        name="executor",
        type=str,
        default=f"{shlex.quote(sys.executable)} {shlex.quote(str(Path(__file__).resolve().parent.parent.parent / 'run.py'))}",
        help="Custom executor to use instead of the configured default.",
        actions=lambda executor: [AddSubstitution("%{executor}", executor)],
    ),
    Parameter(
        name='clang-tidy-executable',
        type=str,
        default=lambda cfg: getSuitableClangTidy(cfg),
        help="Selects the clang-tidy executable to use.",
        actions=lambda exe: [] if exe is None else [
            AddFeature('has-clang-tidy'),
            AddSubstitution('%{clang-tidy}', exe),
        ]
    ),
]
# fmt: on
