=============================================================
How To Build Clang and LLVM with Profile-Guided Optimizations
=============================================================

Introduction
============

PGO (Profile-Guided Optimization) allows your compiler to better optimize code
for how it actually runs. Users report that applying this to Clang and LLVM can
decrease overall compile time by 20%.

This guide walks you through how to build Clang with PGO, though it also applies
to other subprojects, such as LLD.

If you want to build other software with PGO, see the `end-user documentation
for PGO <https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization>`_.


Using preconfigured CMake caches
================================

See https://llvm.org/docs/AdvancedBuilds.html#multi-stage-pgo

Using the script
================

We have a script at ``utils/collect_and_build_with_pgo.py``. This script is
tested on a few Linux flavors, and requires a checkout of LLVM, Clang, and
compiler-rt. Despite the name, it performs four clean builds of Clang, so it
can take a while to run to completion. Please see the script's ``--help`` for
more information on how to run it, and the different options available to you.
If you want to get the most out of PGO for a particular use-case (e.g. compiling
a specific large piece of software), please do read the section below on
'benchmark' selection.

Please note that this script is only tested on a few Linux distros. Patches to
add support for other platforms, as always, are highly appreciated. :)

This script also supports a ``--dry-run`` option, which causes it to print
important commands instead of running them.


Selecting 'benchmarks'
======================

PGO does best when the profiles gathered represent how the user plans to use the
compiler. Notably, highly accurate profiles of llc building x86_64 code aren't
incredibly helpful if you're going to be targeting ARM.

By default, the script above does two things to get solid coverage. It:

- runs all of Clang and LLVM's lit tests, and
- uses the instrumented Clang to build Clang, LLVM, and all of the other
  LLVM subprojects available to it.

Together, these should give you:

- solid coverage of building C++,
- good coverage of building C,
- great coverage of running optimizations,
- great coverage of the backend for your host's architecture, and
- some coverage of other architectures (if other arches are supported backends).

Altogether, this should cover a diverse set of uses for Clang and LLVM. If you
have very specific needs (e.g. your compiler is meant to compile a large browser
for four different platforms, or similar), you may want to do something else.
This is configurable in the script itself.


Building Clang with PGO
=======================

If you prefer to not use the script or the cmake cache, this briefly goes over
how to build Clang/LLVM with PGO.

First, you should have at least LLVM, Clang, and compiler-rt checked out
locally.

Next, at a high level, you're going to need to do the following:

1. Build a standard Release Clang and the relevant libclang_rt.profile library
2. Build Clang using the Clang you built above, but with instrumentation
3. Use the instrumented Clang to generate profiles, which consists of two steps:

  - Running the instrumented Clang/LLVM/lld/etc. on tasks that represent how
    users will use said tools.
  - Using a tool to convert the "raw" profiles generated above into a single,
    final PGO profile.

4. Build a final release Clang (along with whatever other binaries you need)
   using the profile collected from your benchmark

In more detailed steps:

1. Configure a Clang build as you normally would. It's highly recommended that
   you use the Release configuration for this, since it will be used to build
   another Clang. Because you need Clang and supporting libraries, you'll want
   to build the ``all`` target (e.g. ``ninja all`` or ``make -j4 all``).

2. Configure a Clang build as above, but add the following CMake args:

   - ``-DLLVM_BUILD_INSTRUMENTED=IR`` -- This causes us to build everything
     with instrumentation.
   - ``-DLLVM_BUILD_RUNTIME=No`` -- A few projects have bad interactions when
     built with profiling, and aren't necessary to build. This flag turns them
     off.
   - ``-DCMAKE_C_COMPILER=/path/to/stage1/clang`` - Use the Clang we built in
     step 1.
   - ``-DCMAKE_CXX_COMPILER=/path/to/stage1/clang++`` - Same as above.

 In this build directory, you simply need to build the ``clang`` target (and
 whatever supporting tooling your benchmark requires).

3. As mentioned above, this has two steps: gathering profile data, and then
   massaging it into a useful form:

   a. Build your benchmark using the Clang generated in step 2. The 'standard'
      benchmark recommended is to run ``check-clang`` and ``check-llvm`` in your
      instrumented Clang's build directory, and to do a full build of Clang/LLVM
      using your instrumented Clang. So, create yet another build directory,
      with the following CMake arguments:

      - ``-DCMAKE_C_COMPILER=/path/to/stage2/clang`` - Use the Clang we built in
        step 2.
      - ``-DCMAKE_CXX_COMPILER=/path/to/stage2/clang++`` - Same as above.

      If your users are fans of debug info, you may want to consider using
      ``-DCMAKE_BUILD_TYPE=RelWithDebInfo`` instead of
      ``-DCMAKE_BUILD_TYPE=Release``. This will grant better coverage of
      debug info pieces of clang, but will take longer to complete and will
      result in a much larger build directory.

      It's recommended to build the ``all`` target with your instrumented Clang,
      since more coverage is often better.

  b. You should now have a few ``*.profraw`` files in
     ``path/to/stage2/profiles/``. You need to merge these using
     ``llvm-profdata`` (even if you only have one! The profile merge transforms
     profraw into actual profile data, as well). This can be done with
     ``/path/to/stage1/llvm-profdata merge
     -output=/path/to/output/profdata.prof path/to/stage2/profiles/*.profraw``.

4. Now, build your final, PGO-optimized Clang. To do this, you'll want to pass
   the following additional arguments to CMake.

   - ``-DLLVM_PROFDATA_FILE=/path/to/output/profdata.prof`` - Use the PGO
     profile from the previous step.
   - ``-DCMAKE_C_COMPILER=/path/to/stage1/clang`` - Use the Clang we built in
     step 1.
   - ``-DCMAKE_CXX_COMPILER=/path/to/stage1/clang++`` - Same as above.

   From here, you can build whatever targets you need.

   .. note::
     You may see warnings about a mismatched profile in the build output. These
     are generally harmless. To silence them, you can add
     ``-DCMAKE_C_FLAGS='-Wno-backend-plugin'
     -DCMAKE_CXX_FLAGS='-Wno-backend-plugin'`` to your CMake invocation.


Congrats! You now have a Clang built with profile-guided optimizations, and you
can delete all but the final build directory if you'd like.

If this worked well for you and you plan on doing it often, there's a slight
optimization that can be made: LLVM and Clang have a tool called tblgen that's
built and run during the build process. While it's potentially nice to build
this for coverage as part of step 3, none of your other builds should benefit
from building it. You can pass the CMake option
``-DLLVM_NATIVE_TOOL_DIR=/path/to/stage1/bin``
to steps 2 and onward to avoid these useless rebuilds.
