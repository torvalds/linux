#!/usr/bin/env python3
"""
This script:
- Builds clang with user-defined flags
- Uses that clang to build an instrumented clang, which can be used to collect
  PGO samples
- Builds a user-defined set of sources (default: clang) to act as a
  "benchmark" to generate a PGO profile
- Builds clang once more with the PGO profile generated above

This is a total of four clean builds of clang (by default). This may take a
while. :)

This scripts duplicates https://llvm.org/docs/AdvancedBuilds.html#multi-stage-pgo
Eventually, it will be updated to instead call the cmake cache mentioned there.
"""

import argparse
import collections
import multiprocessing
import os
import shlex
import shutil
import subprocess
import sys

### User configuration


# If you want to use a different 'benchmark' than building clang, make this
# function do what you want. out_dir is the build directory for clang, so all
# of the clang binaries will live under "${out_dir}/bin/". Using clang in
# ${out_dir} will magically have the profiles go to the right place.
#
# You may assume that out_dir is a freshly-built directory that you can reach
# in to build more things, if you'd like.
def _run_benchmark(env, out_dir, include_debug_info):
    """The 'benchmark' we run to generate profile data."""
    target_dir = env.output_subdir("instrumentation_run")

    # `check-llvm` and `check-clang` are cheap ways to increase coverage. The
    # former lets us touch on the non-x86 backends a bit if configured, and the
    # latter gives us more C to chew on (and will send us through diagnostic
    # paths a fair amount, though the `if (stuff_is_broken) { diag() ... }`
    # branches should still heavily be weighted in the not-taken direction,
    # since we built all of LLVM/etc).
    _build_things_in(env, out_dir, what=["check-llvm", "check-clang"])

    # Building tblgen gets us coverage; don't skip it. (out_dir may also not
    # have them anyway, but that's less of an issue)
    cmake = _get_cmake_invocation_for_bootstrap_from(env, out_dir, skip_tablegens=False)

    if include_debug_info:
        cmake.add_flag("CMAKE_BUILD_TYPE", "RelWithDebInfo")

    _run_fresh_cmake(env, cmake, target_dir)

    # Just build all the things. The more data we have, the better.
    _build_things_in(env, target_dir, what=["all"])


### Script


class CmakeInvocation:
    _cflags = ["CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS"]
    _ldflags = [
        "CMAKE_EXE_LINKER_FLAGS",
        "CMAKE_MODULE_LINKER_FLAGS",
        "CMAKE_SHARED_LINKER_FLAGS",
    ]

    def __init__(self, cmake, maker, cmake_dir):
        self._prefix = [cmake, "-G", maker, cmake_dir]

        # Map of str -> (list|str).
        self._flags = {}
        for flag in CmakeInvocation._cflags + CmakeInvocation._ldflags:
            self._flags[flag] = []

    def add_new_flag(self, key, value):
        self.add_flag(key, value, allow_overwrites=False)

    def add_flag(self, key, value, allow_overwrites=True):
        if key not in self._flags:
            self._flags[key] = value
            return

        existing_value = self._flags[key]
        if isinstance(existing_value, list):
            existing_value.append(value)
            return

        if not allow_overwrites:
            raise ValueError("Invalid overwrite of %s requested" % key)

        self._flags[key] = value

    def add_cflags(self, flags):
        # No, I didn't intend to append ['-', 'O', '2'] to my flags, thanks :)
        assert not isinstance(flags, str)
        for f in CmakeInvocation._cflags:
            self._flags[f].extend(flags)

    def add_ldflags(self, flags):
        assert not isinstance(flags, str)
        for f in CmakeInvocation._ldflags:
            self._flags[f].extend(flags)

    def to_args(self):
        args = self._prefix.copy()
        for key, value in sorted(self._flags.items()):
            if isinstance(value, list):
                # We preload all of the list-y values (cflags, ...). If we've
                # nothing to add, don't.
                if not value:
                    continue
                value = " ".join(value)

            arg = "-D" + key
            if value != "":
                arg += "=" + value
            args.append(arg)
        return args


class Env:
    def __init__(self, llvm_dir, use_make, output_dir, default_cmake_args, dry_run):
        self.llvm_dir = llvm_dir
        self.use_make = use_make
        self.output_dir = output_dir
        self.default_cmake_args = default_cmake_args.copy()
        self.dry_run = dry_run

    def get_default_cmake_args_kv(self):
        return self.default_cmake_args.items()

    def get_cmake_maker(self):
        return "Ninja" if not self.use_make else "Unix Makefiles"

    def get_make_command(self):
        if self.use_make:
            return ["make", "-j{}".format(multiprocessing.cpu_count())]
        return ["ninja"]

    def output_subdir(self, name):
        return os.path.join(self.output_dir, name)

    def has_llvm_subproject(self, name):
        if name == "compiler-rt":
            subdir = "../compiler-rt"
        elif name == "clang":
            subdir = "../clang"
        else:
            raise ValueError("Unknown subproject: %s" % name)

        return os.path.isdir(os.path.join(self.llvm_dir, subdir))

    # Note that we don't allow capturing stdout/stderr. This works quite nicely
    # with dry_run.
    def run_command(self, cmd, cwd=None, check=False, silent_unless_error=False):
        print("Running `%s` in %s" % (cmd, shlex.quote(cwd or os.getcwd())))

        if self.dry_run:
            return

        if silent_unless_error:
            stdout, stderr = subprocess.PIPE, subprocess.STDOUT
        else:
            stdout, stderr = None, None

        # Don't use subprocess.run because it's >= py3.5 only, and it's not too
        # much extra effort to get what it gives us anyway.
        popen = subprocess.Popen(
            cmd, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, cwd=cwd
        )
        stdout, _ = popen.communicate()
        return_code = popen.wait(timeout=0)

        if not return_code:
            return

        if silent_unless_error:
            print(stdout.decode("utf-8", "ignore"))

        if check:
            raise subprocess.CalledProcessError(
                returncode=return_code, cmd=cmd, output=stdout, stderr=None
            )


def _get_default_cmake_invocation(env):
    inv = CmakeInvocation(
        cmake="cmake", maker=env.get_cmake_maker(), cmake_dir=env.llvm_dir
    )
    for key, value in env.get_default_cmake_args_kv():
        inv.add_new_flag(key, value)
    return inv


def _get_cmake_invocation_for_bootstrap_from(env, out_dir, skip_tablegens=True):
    clang = os.path.join(out_dir, "bin", "clang")
    cmake = _get_default_cmake_invocation(env)
    cmake.add_new_flag("CMAKE_C_COMPILER", clang)
    cmake.add_new_flag("CMAKE_CXX_COMPILER", clang + "++")

    # We often get no value out of building new tblgens; the previous build
    # should have them. It's still correct to build them, just slower.
    def add_tablegen(key, binary):
        path = os.path.join(out_dir, "bin", binary)

        # Check that this exists, since the user's allowed to specify their own
        # stage1 directory (which is generally where we'll source everything
        # from). Dry runs should hope for the best from our user, as well.
        if env.dry_run or os.path.exists(path):
            cmake.add_new_flag(key, path)

    if skip_tablegens:
        add_tablegen("LLVM_TABLEGEN", "llvm-tblgen")
        add_tablegen("CLANG_TABLEGEN", "clang-tblgen")

    return cmake


def _build_things_in(env, target_dir, what):
    cmd = env.get_make_command() + what
    env.run_command(cmd, cwd=target_dir, check=True)


def _run_fresh_cmake(env, cmake, target_dir):
    if not env.dry_run:
        try:
            shutil.rmtree(target_dir)
        except FileNotFoundError:
            pass

        os.makedirs(target_dir, mode=0o755)

    cmake_args = cmake.to_args()
    env.run_command(cmake_args, cwd=target_dir, check=True, silent_unless_error=True)


def _build_stage1_clang(env):
    target_dir = env.output_subdir("stage1")
    cmake = _get_default_cmake_invocation(env)
    _run_fresh_cmake(env, cmake, target_dir)
    _build_things_in(env, target_dir, what=["clang", "llvm-profdata", "profile"])
    return target_dir


def _generate_instrumented_clang_profile(env, stage1_dir, profile_dir, output_file):
    llvm_profdata = os.path.join(stage1_dir, "bin", "llvm-profdata")
    if env.dry_run:
        profiles = [os.path.join(profile_dir, "*.profraw")]
    else:
        profiles = [
            os.path.join(profile_dir, f)
            for f in os.listdir(profile_dir)
            if f.endswith(".profraw")
        ]
    cmd = [llvm_profdata, "merge", "-output=" + output_file] + profiles
    env.run_command(cmd, check=True)


def _build_instrumented_clang(env, stage1_dir):
    assert os.path.isabs(stage1_dir)

    target_dir = os.path.join(env.output_dir, "instrumented")
    cmake = _get_cmake_invocation_for_bootstrap_from(env, stage1_dir)
    cmake.add_new_flag("LLVM_BUILD_INSTRUMENTED", "IR")

    # libcxx's configure step messes with our link order: we'll link
    # libclang_rt.profile after libgcc, and the former requires atexit from the
    # latter. So, configure checks fail.
    #
    # Since we don't need libcxx or compiler-rt anyway, just disable them.
    cmake.add_new_flag("LLVM_BUILD_RUNTIME", "No")

    _run_fresh_cmake(env, cmake, target_dir)
    _build_things_in(env, target_dir, what=["clang", "lld"])

    profiles_dir = os.path.join(target_dir, "profiles")
    return target_dir, profiles_dir


def _build_optimized_clang(env, stage1_dir, profdata_file):
    if not env.dry_run and not os.path.exists(profdata_file):
        raise ValueError(
            "Looks like the profdata file at %s doesn't exist" % profdata_file
        )

    target_dir = os.path.join(env.output_dir, "optimized")
    cmake = _get_cmake_invocation_for_bootstrap_from(env, stage1_dir)
    cmake.add_new_flag("LLVM_PROFDATA_FILE", os.path.abspath(profdata_file))

    # We'll get complaints about hash mismatches in `main` in tools/etc. Ignore
    # it.
    cmake.add_cflags(["-Wno-backend-plugin"])
    _run_fresh_cmake(env, cmake, target_dir)
    _build_things_in(env, target_dir, what=["clang"])
    return target_dir


Args = collections.namedtuple(
    "Args",
    [
        "do_optimized_build",
        "include_debug_info",
        "profile_location",
        "stage1_dir",
    ],
)


def _parse_args():
    parser = argparse.ArgumentParser(
        description="Builds LLVM and Clang with instrumentation, collects "
        "instrumentation profiles for them, and (optionally) builds things "
        "with these PGO profiles. By default, it's assumed that you're "
        "running this from your LLVM root, and all build artifacts will be "
        "saved to $PWD/out."
    )
    parser.add_argument(
        "--cmake-extra-arg",
        action="append",
        default=[],
        help="an extra arg to pass to all cmake invocations. Note that this "
        "is interpreted as a -D argument, e.g. --cmake-extra-arg FOO=BAR will "
        "be passed as -DFOO=BAR. This may be specified multiple times.",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print commands instead of running them"
    )
    parser.add_argument(
        "--llvm-dir",
        default=".",
        help="directory containing an LLVM checkout (default: $PWD)",
    )
    parser.add_argument(
        "--no-optimized-build",
        action="store_true",
        help="disable the final, PGO-optimized build",
    )
    parser.add_argument(
        "--out-dir", help="directory to write artifacts to (default: $llvm_dir/out)"
    )
    parser.add_argument(
        "--profile-output",
        help="where to output the profile (default is $out/pgo_profile.prof)",
    )
    parser.add_argument(
        "--stage1-dir",
        help="instead of having an initial build of everything, use the given "
        "directory. It is expected that this directory will have clang, "
        "llvm-profdata, and the appropriate libclang_rt.profile already built",
    )
    parser.add_argument(
        "--use-debug-info-in-benchmark",
        action="store_true",
        help="use a regular build instead of RelWithDebInfo in the benchmark. "
        "This increases benchmark execution time and disk space requirements, "
        "but gives more coverage over debuginfo bits in LLVM and clang.",
    )
    parser.add_argument(
        "--use-make",
        action="store_true",
        default=shutil.which("ninja") is None,
        help="use Makefiles instead of ninja",
    )

    args = parser.parse_args()

    llvm_dir = os.path.abspath(args.llvm_dir)
    if args.out_dir is None:
        output_dir = os.path.join(llvm_dir, "out")
    else:
        output_dir = os.path.abspath(args.out_dir)

    extra_args = {
        "CMAKE_BUILD_TYPE": "Release",
        "LLVM_ENABLE_PROJECTS": "clang;compiler-rt;lld",
    }
    for arg in args.cmake_extra_arg:
        if arg.startswith("-D"):
            arg = arg[2:]
        elif arg.startswith("-"):
            raise ValueError(
                "Unknown not- -D arg encountered; you may need "
                "to tweak the source..."
            )
        split = arg.split("=", 1)
        if len(split) == 1:
            key, val = split[0], ""
        else:
            key, val = split
        extra_args[key] = val

    env = Env(
        default_cmake_args=extra_args,
        dry_run=args.dry_run,
        llvm_dir=llvm_dir,
        output_dir=output_dir,
        use_make=args.use_make,
    )

    if args.profile_output is not None:
        profile_location = args.profile_output
    else:
        profile_location = os.path.join(env.output_dir, "pgo_profile.prof")

    result_args = Args(
        do_optimized_build=not args.no_optimized_build,
        include_debug_info=args.use_debug_info_in_benchmark,
        profile_location=profile_location,
        stage1_dir=args.stage1_dir,
    )

    return env, result_args


def _looks_like_llvm_dir(directory):
    """Arbitrary set of heuristics to determine if `directory` is an llvm dir.

    Errs on the side of false-positives."""

    contents = set(os.listdir(directory))
    expected_contents = [
        "CODE_OWNERS.TXT",
        "cmake",
        "docs",
        "include",
        "utils",
    ]

    if not all(c in contents for c in expected_contents):
        return False

    try:
        include_listing = os.listdir(os.path.join(directory, "include"))
    except NotADirectoryError:
        return False

    return "llvm" in include_listing


def _die(*args, **kwargs):
    kwargs["file"] = sys.stderr
    print(*args, **kwargs)
    sys.exit(1)


def _main():
    env, args = _parse_args()

    if not _looks_like_llvm_dir(env.llvm_dir):
        _die("Looks like %s isn't an LLVM directory; please see --help" % env.llvm_dir)
    if not env.has_llvm_subproject("clang"):
        _die("Need a clang checkout at tools/clang")
    if not env.has_llvm_subproject("compiler-rt"):
        _die("Need a compiler-rt checkout at projects/compiler-rt")

    def status(*args):
        print(*args, file=sys.stderr)

    if args.stage1_dir is None:
        status("*** Building stage1 clang...")
        stage1_out = _build_stage1_clang(env)
    else:
        stage1_out = args.stage1_dir

    status("*** Building instrumented clang...")
    instrumented_out, profile_dir = _build_instrumented_clang(env, stage1_out)
    status("*** Running profdata benchmarks...")
    _run_benchmark(env, instrumented_out, args.include_debug_info)
    status("*** Generating profile...")
    _generate_instrumented_clang_profile(
        env, stage1_out, profile_dir, args.profile_location
    )

    print("Final profile:", args.profile_location)
    if args.do_optimized_build:
        status("*** Building PGO-optimized binaries...")
        optimized_out = _build_optimized_clang(env, stage1_out, args.profile_location)
        print("Final build directory:", optimized_out)


if __name__ == "__main__":
    _main()
