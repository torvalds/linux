=============================
Advanced Build Configurations
=============================

.. contents::
   :local:

Introduction
============

`CMake <http://www.cmake.org/>`_ is a cross-platform build-generator tool. CMake
does not build the project, it generates the files needed by your build tool
(GNU make, Visual Studio, etc.) for building LLVM.

If **you are a new contributor**, please start with the :doc:`GettingStarted` or
:doc:`CMake` pages. This page is intended for users doing more complex builds.

Many of the examples below are written assuming specific CMake Generators.
Unless otherwise explicitly called out these commands should work with any CMake
generator.

Many of the build configurations mentioned on this documentation page can be
utilized by using a CMake cache. A CMake cache is essentially a configuration
file that sets the necessary flags for a specific build configuration. The caches
for Clang are located in :code:`/clang/cmake/caches` within the monorepo. They
can be passed to CMake using the :code:`-C` flag as demonstrated in the examples
below along with additional configuration flags.

Bootstrap Builds
================

The Clang CMake build system supports bootstrap (aka multi-stage) builds. At a
high level a multi-stage build is a chain of builds that pass data from one
stage into the next. The most common and simple version of this is a traditional
bootstrap build.

In a simple two-stage bootstrap build, we build clang using the system compiler,
then use that just-built clang to build clang again. In CMake this simplest form
of a bootstrap build can be configured with a single option,
CLANG_ENABLE_BOOTSTRAP.

.. code-block:: console

  $ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCLANG_ENABLE_BOOTSTRAP=On \
      -DLLVM_ENABLE_PROJECTS="clang" \
      <path to source>/llvm
  $ ninja stage2

This command itself isn't terribly useful because it assumes default
configurations for each stage. The next series of examples utilize CMake cache
scripts to provide more complex options.

By default, only a few CMake options will be passed between stages.
The list, called _BOOTSTRAP_DEFAULT_PASSTHROUGH, is defined in clang/CMakeLists.txt.
To force the passing of the variables between stages, use the -DCLANG_BOOTSTRAP_PASSTHROUGH
CMake option, each variable separated by a ";". As example:

.. code-block:: console

  $ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCLANG_ENABLE_BOOTSTRAP=On \
      -DCLANG_BOOTSTRAP_PASSTHROUGH="CMAKE_INSTALL_PREFIX;CMAKE_VERBOSE_MAKEFILE" \
      -DLLVM_ENABLE_PROJECTS="clang" \
      <path to source>/llvm
  $ ninja stage2

CMake options starting by ``BOOTSTRAP_`` will be passed only to the stage2 build.
This gives the opportunity to use Clang specific build flags.
For example, the following CMake call will enabled '-fno-addrsig' only during
the stage2 build for C and C++.

.. code-block:: console

  $ cmake [..]  -DBOOTSTRAP_CMAKE_CXX_FLAGS='-fno-addrsig' -DBOOTSTRAP_CMAKE_C_FLAGS='-fno-addrsig' [..]

The clang build system refers to builds as stages. A stage1 build is a standard
build using the compiler installed on the host, and a stage2 build is built
using the stage1 compiler. This nomenclature holds up to more stages too. In
general a stage*n* build is built using the output from stage*n-1*.

Apple Clang Builds (A More Complex Bootstrap)
=============================================

Apple's Clang builds are a slightly more complicated example of the simple
bootstrapping scenario. Apple Clang is built using a 2-stage build.

The stage1 compiler is a host-only compiler with some options set. The stage1
compiler is a balance of optimization vs build time because it is a throwaway.
The stage2 compiler is the fully optimized compiler intended to ship to users.

Setting up these compilers requires a lot of options. To simplify the
configuration the Apple Clang build settings are contained in CMake Cache files.
You can build an Apple Clang compiler using the following commands:

.. code-block:: console

  $ cmake -G Ninja -C <path to source>/clang/cmake/caches/Apple-stage1.cmake <path to source>/llvm
  $ ninja stage2-distribution

This CMake invocation configures the stage1 host compiler, and sets
CLANG_BOOTSTRAP_CMAKE_ARGS to pass the Apple-stage2.cmake cache script to the
stage2 configuration step.

When you build the stage2-distribution target it builds the minimal stage1
compiler and required tools, then configures and builds the stage2 compiler
based on the settings in Apple-stage2.cmake.

This pattern of using cache scripts to set complex settings, and specifically to
make later stage builds include cache scripts is common in our more advanced
build configurations.

Multi-stage PGO
===============

Profile-Guided Optimizations (PGO) is a really great way to optimize the code
clang generates. Our multi-stage PGO builds are a workflow for generating PGO
profiles that can be used to optimize clang.

At a high level, the way PGO works is that you build an instrumented compiler,
then you run the instrumented compiler against sample source files. While the
instrumented compiler runs it will output a bunch of files containing
performance counters (.profraw files). After generating all the profraw files
you use llvm-profdata to merge the files into a single profdata file that you
can feed into the LLVM_PROFDATA_FILE option.

Our PGO.cmake cache automates that whole process. You can use it for
configuration with CMake with the following command:

.. code-block:: console

  $ cmake -G Ninja -C <path to source>/clang/cmake/caches/PGO.cmake \
      <path to source>/llvm

There are several additional options that the cache file also accepts to modify
the build, particularly the PGO_INSTRUMENT_LTO option. Setting this option to
Thin or Full will enable ThinLTO or full LTO respectively, further enhancing
the performance gains from a PGO build by enabling interprocedural
optimizations. For example, to run a CMake configuration for a PGO build
that also enables ThinTLO, use the following command:

.. code-block:: console

  $ cmake -G Ninja -C <path to source>/clang/cmake/caches/PGO.cmake \
      -DPGO_INSTRUMENT_LTO=Thin \
      <path to source>/llvm

By default, clang will generate profile data by compiling a simple
hello world program.  You can also tell clang use an external
project for generating profile data that may be a better fit for your
use case.  The project you specify must either be a lit test suite
(use the CLANG_PGO_TRAINING_DATA option) or a CMake project (use the
CLANG_PERF_TRAINING_DATA_SOURCE_DIR option).

For example, If you wanted to use the
`LLVM Test Suite <https://github.com/llvm/llvm-test-suite/>`_ to generate
profile data you would use the following command:

.. code-block:: console

  $ cmake -G Ninja -C <path to source>/clang/cmake/caches/PGO.cmake \
       -DBOOTSTRAP_CLANG_PGO_TRAINING_DATA_SOURCE_DIR=<path to llvm-test-suite> \
       -DBOOTSTRAP_CLANG_PGO_TRAINING_DEPS=runtimes

The BOOTSTRAP\_ prefixes tells CMake to pass the variables on to the instrumented
stage two build.  And the CLANG_PGO_TRAINING_DEPS option let's you specify
additional build targets to build before building the external project.  The
LLVM Test Suite requires compiler-rt to build, so we need to add the
`runtimes` target as a dependency.

After configuration, building the stage2-instrumented-generate-profdata target
will automatically build the stage1 compiler, build the instrumented compiler
with the stage1 compiler, and then run the instrumented compiler against the
perf training data:

.. code-block:: console

  $ ninja stage2-instrumented-generate-profdata

If you let that run for a few hours or so, it will place a profdata file in your
build directory. This takes a really long time because it builds clang twice,
and you *must* have compiler-rt in your build tree.

This process uses any source files under the perf-training directory as training
data as long as the source files are marked up with LIT-style RUN lines.

After it finishes you can use :code:`find . -name clang.profdata` to find it, but it
should be at a path something like:

.. code-block:: console

  <build dir>/tools/clang/stage2-instrumented-bins/utils/perf-training/clang.profdata

You can feed that file into the LLVM_PROFDATA_FILE option when you build your
optimized compiler.

It may be necessary to build additional targets before running perf training, such as
builtins and runtime libraries. You can use the :code:`CLANG_PGO_TRAINING_DEPS` CMake
variable for that purpose:

.. code-block:: cmake

  set(CLANG_PGO_TRAINING_DEPS builtins runtimes CACHE STRING "")

The PGO cache has a slightly different stage naming scheme than other
multi-stage builds. It generates three stages: stage1, stage2-instrumented, and
stage2. Both of the stage2 builds are built using the stage1 compiler.

The PGO cache generates the following additional targets:

**stage2-instrumented**
  Builds a stage1 compiler, runtime, and required tools (llvm-config,
  llvm-profdata) then uses that compiler to build an instrumented stage2 compiler.

**stage2-instrumented-generate-profdata**
  Depends on stage2-instrumented and will use the instrumented compiler to
  generate profdata based on the training files in clang/utils/perf-training

**stage2**
  Depends on stage2-instrumented-generate-profdata and will use the stage1
  compiler with the stage2 profdata to build a PGO-optimized compiler.

**stage2-check-llvm**
  Depends on stage2 and runs check-llvm using the stage2 compiler.

**stage2-check-clang**
  Depends on stage2 and runs check-clang using the stage2 compiler.

**stage2-check-all**
  Depends on stage2 and runs check-all using the stage2 compiler.

**stage2-test-suite**
  Depends on stage2 and runs the test-suite using the stage2 compiler (requires
  in-tree test-suite).

BOLT
====

`BOLT <https://github.com/llvm/llvm-project/blob/main/bolt/README.md>`_
(Binary Optimization and Layout Tool) is a tool that optimizes binaries
post-link by profiling them at runtime and then using that information to
optimize the layout of the final binary among other optimizations performed
at the binary level. There are also CMake caches available to build
LLVM/Clang with BOLT.

To configure a single-stage build that builds LLVM/Clang and then optimizes
it with BOLT, use the following CMake configuration:

.. code-block:: console

  $ cmake <path to source>/llvm -C <path to source>/clang/cmake/caches/BOLT.cmake

Then, build the BOLT-optimized binary by running the following ninja command:

.. code-block:: console

  $ ninja clang-bolt

If you're seeing errors in the build process, try building with a recent
version of Clang/LLVM by setting the CMAKE_C_COMPILER and
CMAKE_CXX_COMPILER flags to the appropriate values.

It is also possible to use BOLT on top of PGO and (Thin)LTO for an even more
significant runtime speedup. To configure a three stage PGO build with ThinLTO
that optimizes the resulting binary with BOLT, use the following CMake
configuration command:

.. code-block:: console

  $ cmake -G Ninja <path to source>/llvm \
      -C <path to source>/clang/cmake/caches/BOLT-PGO.cmake \
      -DBOOTSTRAP_LLVM_ENABLE_LLD=ON \
      -DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_LLD=ON \
      -DPGO_INSTRUMENT_LTO=Thin

Then, to build the final optimized binary, build the stage2-clang-bolt target:

.. code-block:: console

  $ ninja stage2-clang-bolt

3-Stage Non-Determinism
=======================

In the ancient lore of compilers non-determinism is like the multi-headed hydra.
Whenever its head pops up, terror and chaos ensue.

Historically one of the tests to verify that a compiler was deterministic would
be a three stage build. The idea of a three stage build is you take your sources
and build a compiler (stage1), then use that compiler to rebuild the sources
(stage2), then you use that compiler to rebuild the sources a third time
(stage3) with an identical configuration to the stage2 build. At the end of
this, you have a stage2 and stage3 compiler that should be bit-for-bit
identical.

You can perform one of these 3-stage builds with LLVM and clang using the
following commands:

.. code-block:: console

  $ cmake -G Ninja -C <path to source>/clang/cmake/caches/3-stage.cmake <path to source>/llvm
  $ ninja stage3

After the build you can compare the stage2 and stage3 compilers.
