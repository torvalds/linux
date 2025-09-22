===============================
Building a Distribution of LLVM
===============================

.. contents::
   :local:

Introduction
============

This document is geared toward people who want to build and package LLVM and any
combination of LLVM sub-project tools for distribution. This document covers
useful features of the LLVM build system as well as best practices and general
information about packaging LLVM.

If you are new to CMake you may find the :doc:`CMake` or :doc:`CMakePrimer`
documentation useful. Some of the things covered in this document are the inner
workings of the builds described in the :doc:`AdvancedBuilds` document.

General Distribution Guidance
=============================

When building a distribution of a compiler it is generally advised to perform a
bootstrap build of the compiler. That means building a "stage 1" compiler with
your host toolchain, then building the "stage 2" compiler using the "stage 1"
compiler. This is done so that the compiler you distribute benefits from all the
bug fixes, performance optimizations and general improvements provided by the
new compiler.

In deciding how to build your distribution there are a few trade-offs that you
will need to evaluate. The big two are:

#. Compile time of the distribution against performance of the built compiler

#. Binary size of the distribution against performance of the built compiler

The guidance for maximizing performance of the generated compiler is to use LTO,
PGO, and statically link everything. This will result in an overall larger
distribution, and it will take longer to generate, but it provides the most
opportunity for the compiler to optimize.

The guidance for minimizing distribution size is to dynamically link LLVM and
Clang libraries into the tools to reduce code duplication. This will come at a
substantial performance penalty to the generated binary both because it reduces
optimization opportunity, and because dynamic linking requires resolving symbols
at process launch time, which can be very slow for C++ code.

.. _shared_libs:

.. warning::
  One very important note: Distributions should never be built using the
  *BUILD_SHARED_LIBS* CMake option. That option exists for optimizing developer
  workflow only. Due to design and implementation decisions, LLVM relies on
  global data which can end up being duplicated across shared libraries
  resulting in bugs. As such this is not a safe way to distribute LLVM or
  LLVM-based tools.

The simplest example of building a distribution with reasonable performance is
captured in the DistributionExample CMake cache file located at
clang/cmake/caches/DistributionExample.cmake. The following command will perform
and install the distribution build:

.. code-block:: console

  $ cmake -G Ninja -C <path to clang>/cmake/caches/DistributionExample.cmake <path to LLVM source>
  $ ninja stage2-distribution
  $ ninja stage2-install-distribution

Difference between ``install`` and ``install-distribution``
-----------------------------------------------------------

One subtle but important thing to note is the difference between the ``install``
and ``install-distribution`` targets. The ``install`` target is expected to
install every part of LLVM that your build is configured to generate except the
LLVM testing tools. Alternatively the ``install-distribution`` target, which is
recommended for building distributions, only installs specific parts of LLVM as
specified at configuration time by *LLVM_DISTRIBUTION_COMPONENTS*.

Additionally by default the ``install`` target will install the LLVM testing
tools as the public tools. This can be changed well by setting
*LLVM_INSTALL_TOOLCHAIN_ONLY* to ``On``. The LLVM tools are intended for
development and testing of LLVM, and should only be included in distributions
that support LLVM development.

When building with *LLVM_DISTRIBUTION_COMPONENTS* the build system also
generates a ``distribution`` target which builds all the components specified in
the list. This is a convenience build target to allow building just the
distributed pieces without needing to build all configured targets.

.. _Multi-distribution configurations:

Multi-distribution configurations
---------------------------------

The ``install-distribution`` target described above is for building a single
distribution. LLVM's build system also supports building multiple distributions,
which can be used to e.g. have one distribution containing just tools and
another for libraries (to enable development). These are configured by setting
the *LLVM_DISTRIBUTIONS* variable to hold a list of all distribution names
(which conventionally start with an uppercase letter, e.g. "Development"), and
then setting the *LLVM_<distribution>_DISTRIBUTION_COMPONENTS* variable to the
list of targets for that distribution. For each distribution, the build system
generates an ``install-${distribution}-distribution`` target, where
``${distribution}`` is the name of the distribution in lowercase, to install
that distribution.

Each distribution creates its own set of CMake exports, and the target to
install the CMake exports for a particular distribution for a project is named
``${project}-${distribution}-cmake-exports``, where ``${project}`` is the name
of the project in lowercase and ``${distribution}`` is the name of the
distribution in lowercase, unless the project is LLVM, in which case the target
is just named ``${distribution}-cmake-exports``. These targets need to be
explicitly included in the *LLVM_<distribution>_DISTRIBUTION_COMPONENTS*
variable in order to be included as part of the distribution.

Unlike with the single distribution setup, when building multiple distributions,
any components specified in *LLVM_RUNTIME_DISTRIBUTION_COMPONENTS* are not
automatically added to any distribution. Instead, you must include the targets
explicitly in some *LLVM_<distribution>_DISTRIBUTION_COMPONENTS* list.

By default, each target can appear in multiple distributions; a target will be
installed as part of all distributions it appears in, and it'll be exported by
the last distribution it appears in (the order of distributions is the order
they appear in *LLVM_DISTRIBUTIONS*). We also define some umbrella targets (e.g.
``llvm-libraries`` to install all LLVM libraries); a target can appear in a
different distribution than its umbrella, in which case the target will be
exported by the distribution it appears in (and not the distribution its
umbrella appears in). Set *LLVM_STRICT_DISTRIBUTIONS* to ``On`` if you want to
enforce a target appearing in only one distribution and umbrella distributions
being consistent with target distributions.

We strongly encourage looking at ``clang/cmake/caches/MultiDistributionExample.cmake``
as an example of configuring multiple distributions.

Special Notes for Library-only Distributions
--------------------------------------------

One of the most powerful features of LLVM is its library-first design mentality
and the way you can compose a wide variety of tools using different portions of
LLVM. Even in this situation using *BUILD_SHARED_LIBS* is not supported. If you
want to distribute LLVM as a shared library for use in a tool, the recommended
method is using *LLVM_BUILD_LLVM_DYLIB*, and you can use *LLVM_DYLIB_COMPONENTS*
to configure which LLVM components are part of libLLVM.
Note: *LLVM_BUILD_LLVM_DYLIB* is not available on Windows.

Options for Optimizing LLVM
===========================

There are four main build optimizations that our CMake build system supports.
When performing a bootstrap build it is not beneficial to do anything other than
setting *CMAKE_BUILD_TYPE* to ``Release`` for the stage-1 compiler. This is
because the more intensive optimizations are expensive to perform and the
stage-1 compiler is thrown away. All of the further options described should be
set on the stage-2 compiler either using a CMake cache file, or by prefixing the
option with *BOOTSTRAP_*.

The first and simplest to use is the compiler optimization level by setting the
*CMAKE_BUILD_TYPE* option. The main values of interest are ``Release`` or
``RelWithDebInfo``. By default the ``Release`` option uses the ``-O3``
optimization level, and ``RelWithDebInfo`` uses ``-O2``. If you want to generate
debug information and use ``-O3`` you can override the
*CMAKE_<LANG>_FLAGS_RELWITHDEBINFO* option for C and CXX.
DistributionExample.cmake does this.

Another easy to use option is Link-Time-Optimization. You can set the
*LLVM_ENABLE_LTO* option on your stage-2 build to ``Thin`` or ``Full`` to enable
building LLVM with LTO. These options will significantly increase link time of
the binaries in the distribution, but it will create much faster binaries. This
option should not be used if your distribution includes static archives, as the
objects inside the archive will be LLVM bitcode, which is not portable.

The :doc:`AdvancedBuilds` documentation describes the built-in tooling for
generating LLVM profiling information to drive Profile-Guided-Optimization. The
in-tree profiling tests are very limited, and generating the profile takes a
significant amount of time, but it can result in a significant improvement in
the performance of the generated binaries.

In addition to PGO profiling we also have limited support in-tree for generating
linker order files. These files provide the linker with a suggested ordering for
functions in the final binary layout. This can measurably speed up clang by
physically grouping functions that are called temporally close to each other.
The current tooling is only available on Darwin systems with ``dtrace(1)``. It
is worth noting that dtrace is non-deterministic, and so the order file
generation using dtrace is also non-deterministic.

Options for Reducing Size
=========================

.. warning::
  Any steps taken to reduce the binary size will come at a cost of runtime
  performance in the generated binaries.

The simplest and least significant way to reduce binary size is to set the
*CMAKE_BUILD_TYPE* variable to ``MinSizeRel``, which will set the compiler
optimization level to ``-Os`` which optimizes for binary size. This will have
both the least benefit to size and the least impact on performance.

The most impactful way to reduce binary size is to dynamically link LLVM into
all the tools. This reduces code size by decreasing duplication of common code
between the LLVM-based tools. This can be done by setting the following two
CMake options to ``On``: *LLVM_BUILD_LLVM_DYLIB* and *LLVM_LINK_LLVM_DYLIB*.

.. warning::
  Distributions should never be built using the *BUILD_SHARED_LIBS* CMake
  option. (:ref:`See the warning above for more explanation <shared_libs>`.).

Relevant CMake Options
======================

This section provides documentation of the CMake options that are intended to
help construct distributions. This is not an exhaustive list, and many
additional options are documented in the :doc:`CMake` page. Some key options
that are already documented include: *LLVM_TARGETS_TO_BUILD*, *LLVM_ENABLE_PROJECTS*,
*LLVM_ENABLE_RUNTIMES*, *LLVM_BUILD_LLVM_DYLIB*, and *LLVM_LINK_LLVM_DYLIB*.

**LLVM_ENABLE_RUNTIMES**:STRING
  When building a distribution that includes LLVM runtime projects (i.e. libcxx,
  compiler-rt, libcxxabi, libunwind...), it is important to build those projects
  with the just-built compiler.

**LLVM_DISTRIBUTION_COMPONENTS**:STRING
  This variable can be set to a semi-colon separated list of LLVM build system
  components to install. All LLVM-based tools are components, as well as most
  of the libraries and runtimes. Component names match the names of the build
  system targets.

**LLVM_DISTRIBUTIONS**:STRING
  This variable can be set to a semi-colon separated list of distributions. See
  the :ref:`Multi-distribution configurations` section above for details on this
  and other CMake variables to configure multiple distributions.

**LLVM_RUNTIME_DISTRIBUTION_COMPONENTS**:STRING
  This variable can be set to a semi-colon separated list of runtime library
  components. This is used in conjunction with *LLVM_ENABLE_RUNTIMES* to specify
  components of runtime libraries that you want to include in your distribution.
  Just like with *LLVM_DISTRIBUTION_COMPONENTS*, component names match the names
  of the build system targets.

**LLVM_DYLIB_COMPONENTS**:STRING
  This variable can be set to a semi-colon separated name of LLVM library
  components. LLVM library components are either library names with the LLVM
  prefix removed (i.e. Support, Demangle...), LLVM target names, or special
  purpose component names. The special purpose component names are:

  #. ``all`` - All LLVM available component libraries
  #. ``Native`` - The LLVM target for the Native system
  #. ``AllTargetsAsmParsers`` - All the included target ASM parsers libraries
  #. ``AllTargetsDescs`` - All the included target descriptions libraries
  #. ``AllTargetsDisassemblers`` - All the included target dissassemblers libraries
  #. ``AllTargetsInfos`` - All the included target info libraries

**LLVM_INSTALL_TOOLCHAIN_ONLY**:BOOL
  This option defaults to ``Off``: when set to ``On`` it removes many of the
  LLVM development and testing tools as well as component libraries from the
  default ``install`` target. Including the development tools is not recommended
  for distributions as many of the LLVM tools are only intended for development
  and testing use.
