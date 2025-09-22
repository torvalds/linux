========================
Building LLVM with CMake
========================

.. contents::
   :local:

Introduction
============

`CMake <http://www.cmake.org/>`_ is a cross-platform build-generator tool. CMake
does not build the project, it generates the files needed by your build tool
(GNU make, Visual Studio, etc.) for building LLVM.

If **you are a new contributor**, please start with the :doc:`GettingStarted`
page.  This page is geared for existing contributors moving from the
legacy configure/make system.

If you are really anxious about getting a functional LLVM build, go to the
`Quick start`_ section. If you are a CMake novice, start with `Basic CMake usage`_
and then go back to the `Quick start`_ section once you know what you are doing. The
`Options and variables`_ section is a reference for customizing your build. If
you already have experience with CMake, this is the recommended starting point.

This page is geared towards users of the LLVM CMake build. If you're looking for
information about modifying the LLVM CMake build system you may want to see the
:doc:`CMakePrimer` page. It has a basic overview of the CMake language.

.. _Quick start:

Quick start
===========

We use here the command-line, non-interactive CMake interface.

#. `Download <http://www.cmake.org/cmake/resources/software.html>`_ and install
   CMake. Version 3.20.0 is the minimum required.

#. Open a shell. Your development tools must be reachable from this shell
   through the PATH environment variable.

#. Create a build directory. Building LLVM in the source
   directory is not supported. cd to this directory:

   .. code-block:: console

     $ mkdir mybuilddir
     $ cd mybuilddir

#. Execute this command in the shell replacing `path/to/llvm/source/root` with
   the path to the root of your LLVM source tree:

   .. code-block:: console

     $ cmake path/to/llvm/source/root

   CMake will detect your development environment, perform a series of tests, and
   generate the files required for building LLVM. CMake will use default values
   for all build parameters. See the `Options and variables`_ section for
   a list of build parameters that you can modify.

   This can fail if CMake can't detect your toolset, or if it thinks that the
   environment is not sane enough. In this case, make sure that the toolset that
   you intend to use is the only one reachable from the shell, and that the shell
   itself is the correct one for your development environment. CMake will refuse
   to build MinGW makefiles if you have a POSIX shell reachable through the PATH
   environment variable, for instance. You can force CMake to use a given build
   tool; for instructions, see the `Usage`_ section, below.  You may
   also wish to control which targets LLVM enables, or which LLVM
   components are built; see the `Frequently Used LLVM-related
   variables`_ below.

#. After CMake has finished running, proceed to use IDE project files, or start
   the build from the build directory:

   .. code-block:: console

     $ cmake --build .

   The ``--build`` option tells ``cmake`` to invoke the underlying build
   tool (``make``, ``ninja``, ``xcodebuild``, ``msbuild``, etc.)

   The underlying build tool can be invoked directly, of course, but
   the ``--build`` option is portable.

#. After LLVM has finished building, install it from the build directory:

   .. code-block:: console

     $ cmake --build . --target install

   The ``--target`` option with ``install`` parameter in addition to
   the ``--build`` option tells ``cmake`` to build the ``install`` target.

   It is possible to set a different install prefix at installation time
   by invoking the ``cmake_install.cmake`` script generated in the
   build directory:

   .. code-block:: console

     $ cmake -DCMAKE_INSTALL_PREFIX=/tmp/llvm -P cmake_install.cmake

.. _Basic CMake usage:
.. _Usage:

Basic CMake usage
=================

This section explains basic aspects of CMake
which you may need in your day-to-day usage.

CMake comes with extensive documentation, in the form of html files, and as
online help accessible via the ``cmake`` executable itself. Execute ``cmake
--help`` for further help options.

CMake allows you to specify a build tool (e.g., GNU make, Visual Studio,
or Xcode). If not specified on the command line, CMake tries to guess which
build tool to use, based on your environment. Once it has identified your
build tool, CMake uses the corresponding *Generator* to create files for your
build tool (e.g., Makefiles or Visual Studio or Xcode project files). You can
explicitly specify the generator with the command line option ``-G "Name of the
generator"``. To see a list of the available generators on your system, execute

.. code-block:: console

  $ cmake --help

This will list the generator names at the end of the help text.

Generators' names are case-sensitive, and may contain spaces. For this reason,
you should enter them exactly as they are listed in the ``cmake --help``
output, in quotes. For example, to generate project files specifically for
Visual Studio 12, you can execute:

.. code-block:: console

  $ cmake -G "Visual Studio 12" path/to/llvm/source/root

For a given development platform there can be more than one adequate
generator. If you use Visual Studio, "NMake Makefiles" is a generator you can use
for building with NMake. By default, CMake chooses the most specific generator
supported by your development environment. If you want an alternative generator,
you must tell this to CMake with the ``-G`` option.

.. todo::

  Explain variables and cache. Move explanation here from #options section.

.. _Options and variables:

Options and variables
=====================

Variables customize how the build will be generated. Options are boolean
variables, with possible values ON/OFF. Options and variables are defined on the
CMake command line like this:

.. code-block:: console

  $ cmake -DVARIABLE=value path/to/llvm/source

You can set a variable after the initial CMake invocation to change its
value. You can also undefine a variable:

.. code-block:: console

  $ cmake -UVARIABLE path/to/llvm/source

Variables are stored in the CMake cache. This is a file named ``CMakeCache.txt``
stored at the root of your build directory that is generated by ``cmake``.
Editing it yourself is not recommended.

Variables are listed in the CMake cache and later in this document with
the variable name and type separated by a colon. You can also specify the
variable and type on the CMake command line:

.. code-block:: console

  $ cmake -DVARIABLE:TYPE=value path/to/llvm/source

.. _cmake_frequently_used_variables:

Frequently-used CMake variables
-------------------------------

Here are some of the CMake variables that are used often, along with a
brief explanation. For full documentation, consult the CMake manual,
or execute ``cmake --help-variable VARIABLE_NAME``.  See `Frequently
Used LLVM-related Variables`_ below for information about commonly
used variables that control features of LLVM and enabled subprojects.

.. _cmake_build_type:

**CMAKE_BUILD_TYPE**:STRING
  This configures the optimization level for ``make`` or ``ninja`` builds.

  Possible values:

  =========================== ============= ========== ========== ==========================
  Build Type                  Optimizations Debug Info Assertions Best suited for
  =========================== ============= ========== ========== ==========================
  **Release**                 For Speed     No         No         Users of LLVM and Clang
  **Debug**                   None          Yes        Yes        Developers of LLVM
  **RelWithDebInfo**          For Speed     Yes        No         Users that also need Debug
  **MinSizeRel**              For Size      No         No         When disk space matters
  =========================== ============= ========== ========== ==========================

  * Optimizations make LLVM/Clang run faster, but can be an impediment for
    step-by-step debugging.
  * Builds with debug information can use a lot of RAM and disk space and is
    usually slower to run. You can improve RAM usage by using ``lld``, see
    the :ref:`LLVM_USE_LINKER <llvm_use_linker>` option.
  * Assertions are internal checks to help you find bugs. They typically slow
    down LLVM and Clang when enabled, but can be useful during development.
    You can manually set :ref:`LLVM_ENABLE_ASSERTIONS <llvm_enable_assertions>`
    to override the default from `CMAKE_BUILD_TYPE`.

  If you are using an IDE such as Visual Studio or Xcode, you should use
  the IDE settings to set the build type.

**CMAKE_INSTALL_PREFIX**:PATH
  Path where LLVM will be installed when the "install" target is built.

**CMAKE_{C,CXX}_FLAGS**:STRING
  Extra flags to use when compiling C and C++ source files respectively.

**CMAKE_{C,CXX}_COMPILER**:STRING
  Specify the C and C++ compilers to use. If you have multiple
  compilers installed, CMake might not default to the one you wish to
  use.

.. _Frequently Used LLVM-related variables:

Frequently Used LLVM-related variables
--------------------------------------

The default configuration may not match your requirements. Here are
LLVM variables that are frequently used to control that. The full
description is in `LLVM-related variables`_ below.

**LLVM_ENABLE_PROJECTS**:STRING
  Control which projects are enabled. For example you may want to work on clang
  or lldb by specifying ``-DLLVM_ENABLE_PROJECTS="clang;lldb"``.

**LLVM_ENABLE_RUNTIMES**:STRING
  Control which runtimes are enabled. For example you may want to work on
  libc++ or libc++abi by specifying ``-DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi"``.

**LLVM_LIBDIR_SUFFIX**:STRING
  Extra suffix to append to the directory where libraries are to be
  installed. On a 64-bit architecture, one could use ``-DLLVM_LIBDIR_SUFFIX=64``
  to install libraries to ``/usr/lib64``.

**LLVM_PARALLEL_{COMPILE,LINK}_JOBS**:STRING
  Building the llvm toolchain can use a lot of resources, particularly
  linking. These options, when you use the Ninja generator, allow you
  to restrict the parallelism. For example, to avoid OOMs or going
  into swap, permit only one link job per 15GB of RAM available on a
  32GB machine, specify ``-G Ninja -DLLVM_PARALLEL_LINK_JOBS=2``.

**LLVM_TARGETS_TO_BUILD**:STRING
  Control which targets are enabled. For example you may only need to enable
  your native target with, for example, ``-DLLVM_TARGETS_TO_BUILD=X86``.

.. _llvm_use_linker:

**LLVM_USE_LINKER**:STRING
  Override the system's default linker. For instance use ``lld`` with
  ``-DLLVM_USE_LINKER=lld``.

Rarely-used CMake variables
---------------------------

Here are some of the CMake variables that are rarely used, along with a brief
explanation and LLVM-related notes.  For full documentation, consult the CMake
manual, or execute ``cmake --help-variable VARIABLE_NAME``.

**CMAKE_CXX_STANDARD**:STRING
  Sets the C++ standard to conform to when building LLVM.  Possible values are
  17 and 20.  LLVM Requires C++17 or higher.  This defaults to 17.

**CMAKE_INSTALL_BINDIR**:PATH
  The path to install executables, relative to the *CMAKE_INSTALL_PREFIX*.
  Defaults to "bin".

**CMAKE_INSTALL_DOCDIR**:PATH
  The path to install documentation, relative to the *CMAKE_INSTALL_PREFIX*.
  Defaults to "share/doc".

**CMAKE_INSTALL_INCLUDEDIR**:PATH
  The path to install header files, relative to the *CMAKE_INSTALL_PREFIX*.
  Defaults to "include".

**CMAKE_INSTALL_MANDIR**:PATH
  The path to install manpage files, relative to the *CMAKE_INSTALL_PREFIX*.
  Defaults to "share/man".

.. _LLVM-related variables:

LLVM-related variables
-----------------------

These variables provide fine control over the build of LLVM and
enabled sub-projects. Nearly all of these variable names begin with
``LLVM_``.

**BUILD_SHARED_LIBS**:BOOL
  Flag indicating if each LLVM component (e.g. Support) is built as a shared
  library (ON) or as a static library (OFF). Its default value is OFF. On
  Windows, shared libraries may be used when building with MinGW, including
  mingw-w64, but not when building with the Microsoft toolchain.

  .. note:: BUILD_SHARED_LIBS is only recommended for use by LLVM developers.
            If you want to build LLVM as a shared library, you should use the
            ``LLVM_BUILD_LLVM_DYLIB`` option.

**LLVM_ABI_BREAKING_CHECKS**:STRING
  Used to decide if LLVM should be built with ABI breaking checks or
  not.  Allowed values are `WITH_ASSERTS` (default), `FORCE_ON` and
  `FORCE_OFF`.  `WITH_ASSERTS` turns on ABI breaking checks in an
  assertion enabled build.  `FORCE_ON` (`FORCE_OFF`) turns them on
  (off) irrespective of whether normal (`NDEBUG`-based) assertions are
  enabled or not.  A version of LLVM built with ABI breaking checks
  is not ABI compatible with a version built without it.

**LLVM_ADDITIONAL_BUILD_TYPES**:LIST
  Adding a semicolon separated list of additional build types to this flag
  allows for them to be specified as values in CMAKE_BUILD_TYPE without
  encountering a fatal error during the configuration process.

**LLVM_APPEND_VC_REV**:BOOL
  Embed version control revision info (Git revision id).
  The version info is provided by the ``LLVM_REVISION`` macro in
  ``llvm/include/llvm/Support/VCSRevision.h``. Developers using git who don't
  need revision info can disable this option to avoid re-linking most binaries
  after a branch switch. Defaults to ON.

**LLVM_FORCE_VC_REPOSITORY**:STRING
  Set the git repository to include in version info rather than calling git to
  determine it.

**LLVM_FORCE_VC_REVISION**:STRING
  Force a specific Git revision id rather than calling to git to determine it.
  This is useful in environments where git is not available or non-functional
  but the VC revision is available through other means.

**LLVM_BUILD_32_BITS**:BOOL
  Build 32-bit executables and libraries on 64-bit systems. This option is
  available only on some 64-bit Unix systems. Defaults to OFF.

**LLVM_BUILD_BENCHMARKS**:BOOL
  Adds benchmarks to the list of default targets. Defaults to OFF.

**LLVM_BUILD_DOCS**:BOOL
  Adds all *enabled* documentation targets (i.e. Doxgyen and Sphinx targets) as
  dependencies of the default build targets.  This results in all of the (enabled)
  documentation targets being as part of a normal build.  If the ``install``
  target is run then this also enables all built documentation targets to be
  installed. Defaults to OFF.  To enable a particular documentation target, see
  LLVM_ENABLE_SPHINX and LLVM_ENABLE_DOXYGEN.

**LLVM_BUILD_EXAMPLES**:BOOL
  Build LLVM examples. Defaults to OFF. Targets for building each example are
  generated in any case. See documentation for *LLVM_BUILD_TOOLS* above for more
  details.

**LLVM_BUILD_INSTRUMENTED_COVERAGE**:BOOL
  If enabled, `source-based code coverage
  <https://clang.llvm.org/docs/SourceBasedCodeCoverage.html>`_ instrumentation
  is enabled while building llvm. If CMake can locate the code coverage
  scripts and the llvm-cov and llvm-profdata tools that pair to your compiler,
  the build will also generate the `generate-coverage-report` target to generate
  the code coverage report for LLVM, and the `clear-profile-data` utility target
  to delete captured profile data. See documentation for
  *LLVM_CODE_COVERAGE_TARGETS* and *LLVM_COVERAGE_SOURCE_DIRS* for more
  information on configuring code coverage reports.

**LLVM_BUILD_LLVM_DYLIB**:BOOL
  If enabled, the target for building the libLLVM shared library is added.
  This library contains all of LLVM's components in a single shared library.
  Defaults to OFF. This cannot be used in conjunction with BUILD_SHARED_LIBS.
  Tools will only be linked to the libLLVM shared library if LLVM_LINK_LLVM_DYLIB
  is also ON.
  The components in the library can be customised by setting LLVM_DYLIB_COMPONENTS
  to a list of the desired components.
  This option is not available on Windows.

**LLVM_BUILD_TESTS**:BOOL
  Include LLVM unit tests in the 'all' build target. Defaults to OFF. Targets
  for building each unit test are generated in any case. You can build a
  specific unit test using the targets defined under *unittests*, such as
  ADTTests, IRTests, SupportTests, etc. (Search for ``add_llvm_unittest`` in
  the subdirectories of *unittests* for a complete list of unit tests.) It is
  possible to build all unit tests with the target *UnitTests*.

**LLVM_BUILD_TOOLS**:BOOL
  Build LLVM tools. Defaults to ON. Targets for building each tool are generated
  in any case. You can build a tool separately by invoking its target. For
  example, you can build *llvm-as* with a Makefile-based system by executing *make
  llvm-as* at the root of your build directory.

**LLVM_CCACHE_BUILD**:BOOL
  If enabled and the ``ccache`` program is available, then LLVM will be
  built using ``ccache`` to speed up rebuilds of LLVM and its components.
  Defaults to OFF.  The size and location of the cache maintained
  by ``ccache`` can be adjusted via the LLVM_CCACHE_MAXSIZE and LLVM_CCACHE_DIR
  options, which are passed to the CCACHE_MAXSIZE and CCACHE_DIR environment
  variables, respectively.

**LLVM_CODE_COVERAGE_TARGETS**:STRING
  If set to a semicolon separated list of targets, those targets will be used
  to drive the code coverage reports. If unset, the target list will be
  constructed using the LLVM build's CMake export list.

**LLVM_COVERAGE_SOURCE_DIRS**:STRING
  If set to a semicolon separated list of directories, the coverage reports
  will limit code coverage summaries to just the listed directories. If unset,
  coverage reports will include all sources identified by the tooling.

**LLVM_CREATE_XCODE_TOOLCHAIN**:BOOL
  macOS Only: If enabled CMake will generate a target named
  'install-xcode-toolchain'. This target will create a directory at
  $CMAKE_INSTALL_PREFIX/Toolchains containing an xctoolchain directory which can
  be used to override the default system tools.

**LLVM_DEFAULT_TARGET_TRIPLE**:STRING
  LLVM target to use for code generation when no target is explicitly specified.
  It defaults to "host", meaning that it shall pick the architecture
  of the machine where LLVM is being built. If you are building a cross-compiler,
  set it to the target triple of your desired architecture.

**LLVM_DOXYGEN_QCH_FILENAME**:STRING
  The filename of the Qt Compressed Help file that will be generated when
  ``-DLLVM_ENABLE_DOXYGEN=ON`` and
  ``-DLLVM_ENABLE_DOXYGEN_QT_HELP=ON`` are given. Defaults to
  ``org.llvm.qch``.
  This option is only useful in combination with
  ``-DLLVM_ENABLE_DOXYGEN_QT_HELP=ON``;
  otherwise it has no effect.

**LLVM_DOXYGEN_QHELPGENERATOR_PATH**:STRING
  The path to the ``qhelpgenerator`` executable. Defaults to whatever CMake's
  ``find_program()`` can find. This option is only useful in combination with
  ``-DLLVM_ENABLE_DOXYGEN_QT_HELP=ON``; otherwise it has no
  effect.

**LLVM_DOXYGEN_QHP_CUST_FILTER_NAME**:STRING
  See `Qt Help Project`_ for
  more information. Defaults to the CMake variable ``${PACKAGE_STRING}`` which
  is a combination of the package name and version string. This filter can then
  be used in Qt Creator to select only documentation from LLVM when browsing
  through all the help files that you might have loaded. This option is only
  useful in combination with ``-DLLVM_ENABLE_DOXYGEN_QT_HELP=ON``;
  otherwise it has no effect.

.. _Qt Help Project: http://qt-project.org/doc/qt-4.8/qthelpproject.html#custom-filters

**LLVM_DOXYGEN_QHP_NAMESPACE**:STRING
  Namespace under which the intermediate Qt Help Project file lives. See `Qt
  Help Project`_
  for more information. Defaults to "org.llvm". This option is only useful in
  combination with ``-DLLVM_ENABLE_DOXYGEN_QT_HELP=ON``; otherwise
  it has no effect.

**LLVM_DOXYGEN_SVG**:BOOL
  Uses .svg files instead of .png files for graphs in the Doxygen output.
  Defaults to OFF.

.. _llvm_enable_assertions:

**LLVM_ENABLE_ASSERTIONS**:BOOL
  Enables code assertions. Defaults to ON if and only if ``CMAKE_BUILD_TYPE``
  is *Debug*.

**LLVM_ENABLE_BINDINGS**:BOOL
  If disabled, do not try to build the OCaml bindings.

**LLVM_ENABLE_DIA_SDK**:BOOL
  Enable building with MSVC DIA SDK for PDB debugging support. Available
  only with MSVC. Defaults to ON.

**LLVM_ENABLE_DOXYGEN**:BOOL
  Enables the generation of browsable HTML documentation using doxygen.
  Defaults to OFF.

**LLVM_ENABLE_DOXYGEN_QT_HELP**:BOOL
  Enables the generation of a Qt Compressed Help file. Defaults to OFF.
  This affects the make target ``doxygen-llvm``. When enabled, apart from
  the normal HTML output generated by doxygen, this will produce a QCH file
  named ``org.llvm.qch``. You can then load this file into Qt Creator.
  This option is only useful in combination with ``-DLLVM_ENABLE_DOXYGEN=ON``;
  otherwise this has no effect.

**LLVM_ENABLE_EH**:BOOL
  Build LLVM with exception-handling support. This is necessary if you wish to
  link against LLVM libraries and make use of C++ exceptions in your own code
  that need to propagate through LLVM code. Defaults to OFF.

**LLVM_ENABLE_EXPENSIVE_CHECKS**:BOOL
  Enable additional time/memory expensive checking. Defaults to OFF.

**LLVM_ENABLE_FFI**:BOOL
  Indicates whether the LLVM Interpreter will be linked with the Foreign Function
  Interface library (libffi) in order to enable calling external functions.
  If the library or its headers are installed in a custom
  location, you can also set the variables FFI_INCLUDE_DIR and
  FFI_LIBRARY_DIR to the directories where ffi.h and libffi.so can be found,
  respectively. Defaults to OFF.

**LLVM_ENABLE_HTTPLIB**:BOOL
  Enables the optional cpp-httplib dependency which is used by llvm-debuginfod
  to serve debug info over HTTP. `cpp-httplib <https://github.com/yhirose/cpp-httplib>`_
  must be installed, or `httplib_ROOT` must be set. Defaults to OFF.

**LLVM_ENABLE_IDE**:BOOL
  Tell the build system that an IDE is being used. This in turn disables the
  creation of certain convenience build system targets, such as the various
  ``install-*`` and ``check-*`` targets, since IDEs don't always deal well with
  a large number of targets. This is usually autodetected, but it can be
  configured manually to explicitly control the generation of those targets.

**LLVM_ENABLE_LIBCXX**:BOOL
  If the host compiler and linker supports the stdlib flag, -stdlib=libc++ is
  passed to invocations of both so that the project is built using libc++
  instead of stdlibc++. Defaults to OFF.

**LLVM_ENABLE_LIBPFM**:BOOL
  Enable building with libpfm to support hardware counter measurements in LLVM
  tools.
  Defaults to ON.

**LLVM_ENABLE_LLD**:BOOL
  This option is equivalent to `-DLLVM_USE_LINKER=lld`, except during a 2-stage
  build where a dependency is added from the first stage to the second ensuring
  that lld is built before stage2 begins.

**LLVM_ENABLE_LLVM_LIBC**: BOOL
  If the LLVM libc overlay is installed in a location where the host linker
  can access it, all built executables will be linked against the LLVM libc
  overlay before linking against the system libc. Defaults to OFF.

**LLVM_ENABLE_LTO**:STRING
  Add ``-flto`` or ``-flto=`` flags to the compile and link command
  lines, enabling link-time optimization. Possible values are ``Off``,
  ``On``, ``Thin`` and ``Full``. Defaults to OFF.

**LLVM_ENABLE_MODULES**:BOOL
  Compile with `Clang Header Modules
  <https://clang.llvm.org/docs/Modules.html>`_.

**LLVM_ENABLE_PEDANTIC**:BOOL
  Enable pedantic mode. This disables compiler-specific extensions, if
  possible. Defaults to ON.

**LLVM_ENABLE_PIC**:BOOL
  Add the ``-fPIC`` flag to the compiler command-line, if the compiler supports
  this flag. Some systems, like Windows, do not need this flag. Defaults to ON.

**LLVM_ENABLE_PROJECTS**:STRING
  Semicolon-separated list of projects to build, or *all* for building all
  (clang, lldb, lld, polly, etc) projects. This flag assumes that projects
  are checked out side-by-side and not nested, i.e. clang needs to be in
  parallel of llvm instead of nested in `llvm/tools`. This feature allows
  to have one build for only LLVM and another for clang+llvm using the same
  source checkout.
  The full list is:
  ``clang;clang-tools-extra;cross-project-tests;libc;libclc;lld;lldb;openmp;polly;pstl``

**LLVM_ENABLE_RTTI**:BOOL
  Build LLVM with run-time type information. Defaults to OFF.

**LLVM_ENABLE_RUNTIMES**:STRING
  Build libc++, libc++abi, libunwind or compiler-rt using the just-built compiler.
  This is the correct way to build runtimes when putting together a toolchain.
  It will build the builtins separately from the other runtimes to preserve
  correct dependency ordering. If you want to build the runtimes using a system
  compiler, see the `libc++ documentation <https://libcxx.llvm.org/BuildingLibcxx.html>`_.
  Note: the list should not have duplicates with `LLVM_ENABLE_PROJECTS`.
  The full list is:
  ``compiler-rt;libc;libcxx;libcxxabi;libunwind;openmp``
  To enable all of them, use:
  ``LLVM_ENABLE_RUNTIMES=all``

**LLVM_ENABLE_SPHINX**:BOOL
  If specified, CMake will search for the ``sphinx-build`` executable and will make
  the ``SPHINX_OUTPUT_HTML`` and ``SPHINX_OUTPUT_MAN`` CMake options available.
  Defaults to OFF.

**LLVM_ENABLE_THREADS**:BOOL
  Build with threads support, if available. Defaults to ON.

**LLVM_ENABLE_UNWIND_TABLES**:BOOL
  Enable unwind tables in the binary.  Disabling unwind tables can reduce the
  size of the libraries.  Defaults to ON.

**LLVM_ENABLE_WARNINGS**:BOOL
  Enable all compiler warnings. Defaults to ON.

**LLVM_ENABLE_WERROR**:BOOL
  Stop and fail the build, if a compiler warning is triggered. Defaults to OFF.

**LLVM_ENABLE_Z3_SOLVER**:BOOL
  If enabled, the Z3 constraint solver is activated for the Clang static analyzer.
  A recent version of the z3 library needs to be available on the system.

**LLVM_ENABLE_ZLIB**:STRING
  Used to decide if LLVM tools should support compression/decompression with
  zlib. Allowed values are ``OFF``, ``ON`` (default, enable if zlib is found),
  and ``FORCE_ON`` (error if zlib is not found).

**LLVM_ENABLE_ZSTD**:STRING
  Used to decide if LLVM tools should support compression/decompression with
  zstd. Allowed values are ``OFF``, ``ON`` (default, enable if zstd is found),
  and ``FORCE_ON`` (error if zstd is not found).

**LLVM_EXPERIMENTAL_TARGETS_TO_BUILD**:STRING
  Semicolon-separated list of experimental targets to build and linked into
  llvm. This will build the experimental target without needing it to add to the
  list of all the targets available in the LLVM's main CMakeLists.txt.

**LLVM_EXTERNAL_PROJECTS**:STRING
  Semicolon-separated list of additional external projects to build as part of
  llvm. For each project LLVM_EXTERNAL_<NAME>_SOURCE_DIR have to be specified
  with the path for the source code of the project. Example:
  ``-DLLVM_EXTERNAL_PROJECTS="Foo;Bar"
  -DLLVM_EXTERNAL_FOO_SOURCE_DIR=/src/foo
  -DLLVM_EXTERNAL_BAR_SOURCE_DIR=/src/bar``.

**LLVM_EXTERNAL_{CLANG,LLD,POLLY}_SOURCE_DIR**:PATH
  These variables specify the path to the source directory for the external
  LLVM projects Clang, lld, and Polly, respectively, relative to the top-level
  source directory.  If the in-tree subdirectory for an external project
  exists (e.g., llvm/tools/clang for Clang), then the corresponding variable
  will not be used.  If the variable for an external project does not point
  to a valid path, then that project will not be built.

**LLVM_EXTERNALIZE_DEBUGINFO**:BOOL
  Generate dSYM files and strip executables and libraries (Darwin Only).
  Defaults to OFF.

**LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES**:BOOL
  When building executables, preserve symbol exports. Defaults to ON. 
  You can use this option to disable exported symbols from all 
  executables (Darwin Only).

**LLVM_FORCE_USE_OLD_TOOLCHAIN**:BOOL
  If enabled, the compiler and standard library versions won't be checked. LLVM
  may not compile at all, or might fail at runtime due to known bugs in these
  toolchains.

**LLVM_INCLUDE_BENCHMARKS**:BOOL
  Generate build targets for the LLVM benchmarks. Defaults to ON.

**LLVM_INCLUDE_EXAMPLES**:BOOL
  Generate build targets for the LLVM examples. Defaults to ON. You can use this
  option to disable the generation of build targets for the LLVM examples.

**LLVM_INCLUDE_TESTS**:BOOL
  Generate build targets for the LLVM unit tests. Defaults to ON. You can use
  this option to disable the generation of build targets for the LLVM unit
  tests.

**LLVM_INCLUDE_TOOLS**:BOOL
  Generate build targets for the LLVM tools. Defaults to ON. You can use this
  option to disable the generation of build targets for the LLVM tools.

**LLVM_INDIVIDUAL_TEST_COVERAGE**:BOOL
  Enable individual test case coverage. When set to ON, code coverage data for
  each test case will be generated and stored in a separate directory under the
  config.test_exec_root path. This feature allows code coverage analysis of each
  individual test case. Defaults to OFF.

**LLVM_INSTALL_BINUTILS_SYMLINKS**:BOOL
  Install symlinks from the binutils tool names to the corresponding LLVM tools.
  For example, ar will be symlinked to llvm-ar.

**LLVM_INSTALL_CCTOOLS_SYMLINKS**:BOOL
  Install symliks from the cctools tool names to the corresponding LLVM tools.
  For example, lipo will be symlinked to llvm-lipo.

**LLVM_INSTALL_OCAMLDOC_HTML_DIR**:STRING
  The path to install OCamldoc-generated HTML documentation to. This path can
  either be absolute or relative to the CMAKE_INSTALL_PREFIX. Defaults to
  ``${CMAKE_INSTALL_DOCDIR}/llvm/ocaml-html``.

**LLVM_INSTALL_SPHINX_HTML_DIR**:STRING
  The path to install Sphinx-generated HTML documentation to. This path can
  either be absolute or relative to the CMAKE_INSTALL_PREFIX. Defaults to
  ``${CMAKE_INSTALL_DOCDIR}/llvm/html``.

**LLVM_INSTALL_UTILS**:BOOL
  If enabled, utility binaries like ``FileCheck`` and ``not`` will be installed
  to CMAKE_INSTALL_PREFIX.

**LLVM_INSTALL_DOXYGEN_HTML_DIR**:STRING
  The path to install Doxygen-generated HTML documentation to. This path can
  either be absolute or relative to the *CMAKE_INSTALL_PREFIX*. Defaults to
  ``${CMAKE_INSTALL_DOCDIR}/llvm/doxygen-html``.

**LLVM_INTEGRATED_CRT_ALLOC**:PATH
  On Windows, allows embedding a different C runtime allocator into the LLVM
  tools and libraries. Using a lock-free allocator such as the ones listed below
  greatly decreases ThinLTO link time by about an order of magnitude. It also
  midly improves Clang build times, by about 5-10%. At the moment, rpmalloc,
  snmalloc and mimalloc are supported. Use the path to `git clone` to select
  the respective allocator, for example:

  .. code-block:: console

    $ D:\git> git clone https://github.com/mjansson/rpmalloc
    $ D:\llvm-project> cmake ... -DLLVM_INTEGRATED_CRT_ALLOC=D:\git\rpmalloc

  This option needs to be used along with the static CRT, ie. if building the
  Release target, add -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded.
  Note that rpmalloc is also supported natively in-tree, see option below.

**LLVM_ENABLE_RPMALLOC**:BOOL
  Similar to LLVM_INTEGRATED_CRT_ALLOC, embeds the in-tree rpmalloc into the
  host toolchain as a C runtime allocator. The version currently used is
  rpmalloc 1.4.5. This option also implies linking with the static CRT, there's
  no need to provide CMAKE_MSVC_RUNTIME_LIBRARY.

**LLVM_LINK_LLVM_DYLIB**:BOOL
  If enabled, tools will be linked with the libLLVM shared library. Defaults
  to OFF. Setting LLVM_LINK_LLVM_DYLIB to ON also sets LLVM_BUILD_LLVM_DYLIB
  to ON.
  This option is not available on Windows.

**LLVM_<target>_LINKER_FLAGS**:STRING
  Defines the set of linker flags that should be applied to a <target>.

**LLVM_LIT_ARGS**:STRING
  Arguments given to lit.  ``make check`` and ``make clang-test`` are affected.
  By default, ``'-sv --no-progress-bar'`` on Visual C++ and Xcode, ``'-sv'`` on
  others.

**LLVM_LIT_TOOLS_DIR**:PATH
  The path to GnuWin32 tools for tests. Valid on Windows host.  Defaults to
  the empty string, in which case lit will look for tools needed for tests
  (e.g. ``grep``, ``sort``, etc.) in your %PATH%. If GnuWin32 is not in your
  %PATH%, then you can set this variable to the GnuWin32 directory so that
  lit can find tools needed for tests in that directory.

**LLVM_NATIVE_TOOL_DIR**:STRING
  Full path to a directory containing executables for the build host
  (containing binaries such as ``llvm-tblgen`` and ``clang-tblgen``). This is
  intended for cross-compiling: if the user sets this variable and the
  directory contains executables with the expected names, no separate
  native versions of those executables will be built.

**LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE**:BOOL
  Defaults to ``OFF``. If set to ``ON``, CMake's default logic for library IDs
  on Darwin in the build tree will be used. Otherwise the install-time library
  IDs will be used in the build tree as well. Mainly useful when other CMake
  library ID control variables (e.g., ``CMAKE_INSTALL_NAME_DIR``) are being
  set to non-standard values.

**LLVM_OPTIMIZED_TABLEGEN**:BOOL
  If enabled and building a debug or asserts build the CMake build system will
  generate a Release build tree to build a fully optimized tablegen for use
  during the build. Enabling this option can significantly speed up build times
  especially when building LLVM in Debug configurations.

**LLVM_PARALLEL_{COMPILE,LINK,TABLEGEN}_JOBS**:STRING
  Limit the maximum number of concurrent compilation, link or
  tablegen jobs respectively. The default total number of parallel jobs is
  determined by the number of logical CPUs.

**LLVM_PROFDATA_FILE**:PATH
  Path to a profdata file to pass into clang's -fprofile-instr-use flag. This
  can only be specified if you're building with clang.

**LLVM_RAM_PER_{COMPILE,LINK,TABLEGEN}_JOB**:STRING
  Limit the number of concurrent compile, link or tablegen jobs
  respectively, depending on available physical memory. The value
  specified is in MB. The respective
  ``LLVM_PARALLEL_{COMPILE,LINK,TABLEGEN}_JOBS`` variable is
  overwritten by computing the memory size divided by the
  specified value. The largest memory user is linking, but remember
  that jobs in the other categories might run in parallel to the link
  jobs, and you need to consider their memory requirements when
  in a memory-limited environment. Using a
  ``-DLLVM_RAM_PER_LINK_JOB=10000`` is a good approximation. On ELF
  platforms debug builds can reduce link-time memory pressure by also
  using ``LLVM_USE_SPLIT_DWARF``.

**LLVM_REVERSE_ITERATION**:BOOL
  If enabled, all supported unordered llvm containers would be iterated in
  reverse order. This is useful for uncovering non-determinism caused by
  iteration of unordered containers.

**LLVM_STATIC_LINK_CXX_STDLIB**:BOOL
  Statically link to the C++ standard library if possible. This uses the flag
  "-static-libstdc++", but a Clang host compiler will statically link to libc++
  if used in conjunction with the **LLVM_ENABLE_LIBCXX** flag. Defaults to OFF.

**LLVM_TABLEGEN**:STRING
  Full path to a native TableGen executable (usually named ``llvm-tblgen``). This is
  intended for cross-compiling: if the user sets this variable, no native
  TableGen will be created.

**LLVM_TARGET_ARCH**:STRING
  LLVM target to use for native code generation. This is required for JIT
  generation. It defaults to "host", meaning that it shall pick the architecture
  of the machine where LLVM is being built. If you are cross-compiling, set it
  to the target architecture name.

**LLVM_TARGETS_TO_BUILD**:STRING
  Semicolon-separated list of targets to build, or *all* for building all
  targets. Case-sensitive. Defaults to *all*. Example:
  ``-DLLVM_TARGETS_TO_BUILD="X86;PowerPC"``.
  The full list, as of March 2023, is:
  ``AArch64;AMDGPU;ARM;AVR;BPF;Hexagon;Lanai;LoongArch;Mips;MSP430;NVPTX;PowerPC;RISCV;Sparc;SystemZ;VE;WebAssembly;X86;XCore``

**LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN**:BOOL
  If enabled, the compiler version check will only warn when using a toolchain
  which is about to be deprecated, instead of emitting an error.

**LLVM_UBSAN_FLAGS**:STRING
  Defines the set of compile flags used to enable UBSan. Only used if
  ``LLVM_USE_SANITIZER`` contains ``Undefined``. This can be used to override
  the default set of UBSan flags.

**LLVM_UNREACHABLE_OPTIMIZE**:BOOL
  This flag controls the behavior of `llvm_unreachable()` in release build
  (when assertions are disabled in general). When ON (default) then
  `llvm_unreachable()` is considered "undefined behavior" and optimized as
  such. When OFF it is instead replaced with a guaranteed "trap".

**LLVM_USE_INTEL_JITEVENTS**:BOOL
  Enable building support for Intel JIT Events API. Defaults to OFF.

**LLVM_USE_LINKER**:STRING
  Add ``-fuse-ld={name}`` to the link invocation. The possible value depend on
  your compiler, for clang the value can be an absolute path to your custom
  linker, otherwise clang will prefix the name with ``ld.`` and apply its usual
  search. For example to link LLVM with the Gold linker, cmake can be invoked
  with ``-DLLVM_USE_LINKER=gold``.

**LLVM_USE_OPROFILE**:BOOL
  Enable building OProfile JIT support. Defaults to OFF.

**LLVM_USE_PERF**:BOOL
  Enable building support for Perf (linux profiling tool) JIT support. Defaults to OFF.

**LLVM_USE_RELATIVE_PATHS_IN_FILES**:BOOL
  Rewrite absolute source paths in sources and debug info to relative ones. The
  source prefix can be adjusted via the LLVM_SOURCE_PREFIX variable.

**LLVM_USE_RELATIVE_PATHS_IN_DEBUG_INFO**:BOOL
  Rewrite absolute source paths in debug info to relative ones. The source prefix
  can be adjusted via the LLVM_SOURCE_PREFIX variable.

**LLVM_USE_SANITIZER**:STRING
  Define the sanitizer used to build LLVM binaries and tests. Possible values
  are ``Address``, ``Memory``, ``MemoryWithOrigins``, ``Undefined``, ``Thread``,
  ``DataFlow``, and ``Address;Undefined``. Defaults to empty string.

**LLVM_USE_SPLIT_DWARF**:BOOL
  If enabled CMake will pass ``-gsplit-dwarf`` to the compiler. This option
  reduces link-time memory usage by reducing the amount of debug information that
  the linker needs to resolve. It is recommended for platforms using the ELF object
  format, like Linux systems when linker memory usage is too high.

**SPHINX_EXECUTABLE**:STRING
  The path to the ``sphinx-build`` executable detected by CMake.
  For installation instructions, see
  https://www.sphinx-doc.org/en/master/usage/installation.html

**SPHINX_OUTPUT_HTML**:BOOL
  If enabled (and ``LLVM_ENABLE_SPHINX`` is enabled) then the targets for
  building the documentation as html are added (but not built by default unless
  ``LLVM_BUILD_DOCS`` is enabled). There is a target for each project in the
  source tree that uses sphinx (e.g.  ``docs-llvm-html``, ``docs-clang-html``
  and ``docs-lld-html``). Defaults to ON.

**SPHINX_OUTPUT_MAN**:BOOL
  If enabled (and ``LLVM_ENABLE_SPHINX`` is enabled) the targets for building
  the man pages are added (but not built by default unless ``LLVM_BUILD_DOCS``
  is enabled). Currently the only target added is ``docs-llvm-man``. Defaults
  to ON.

**SPHINX_WARNINGS_AS_ERRORS**:BOOL
  If enabled then sphinx documentation warnings will be treated as
  errors. Defaults to ON.

Advanced variables
~~~~~~~~~~~~~~~~~~

These are niche, and changing them from their defaults is more likely to cause
things to go wrong.  They are also unstable across LLVM versions.

**LLVM_EXAMPLES_INSTALL_DIR**:STRING
  The path for examples of using LLVM, relative to the *CMAKE_INSTALL_PREFIX*.
  Only matters if *LLVM_BUILD_EXAMPLES* is enabled.
  Defaults to "examples".

**LLVM_TOOLS_INSTALL_DIR**:STRING
  The path to install the main LLVM tools, relative to the *CMAKE_INSTALL_PREFIX*.
  Defaults to *CMAKE_INSTALL_BINDIR*.

**LLVM_UTILS_INSTALL_DIR**:STRING
  The path to install auxiliary LLVM utilities, relative to the *CMAKE_INSTALL_PREFIX*.
  Only matters if *LLVM_INSTALL_UTILS* is enabled.
  Defaults to *LLVM_TOOLS_INSTALL_DIR*.

CMake Caches
============

Recently LLVM and Clang have been adding some more complicated build system
features. Utilizing these new features often involves a complicated chain of
CMake variables passed on the command line. Clang provides a collection of CMake
cache scripts to make these features more approachable.

CMake cache files are utilized using CMake's -C flag:

.. code-block:: console

  $ cmake -C <path to cache file> <path to sources>

CMake cache scripts are processed in an isolated scope, only cached variables
remain set when the main configuration runs. CMake cached variables do not reset
variables that are already set unless the FORCE option is specified.

A few notes about CMake Caches:

- Order of command line arguments is important

  - -D arguments specified before -C are set before the cache is processed and
    can be read inside the cache file
  - -D arguments specified after -C are set after the cache is processed and
    are unset inside the cache file

- All -D arguments will override cache file settings
- CMAKE_TOOLCHAIN_FILE is evaluated after both the cache file and the command
  line arguments
- It is recommended that all -D options should be specified *before* -C

For more information about some of the advanced build configurations supported
via Cache files see :doc:`AdvancedBuilds`.

Executing the Tests
===================

Testing is performed when the *check-all* target is built. For instance, if you are
using Makefiles, execute this command in the root of your build directory:

.. code-block:: console

  $ make check-all

On Visual Studio, you may run tests by building the project "check-all".
For more information about testing, see the :doc:`TestingGuide`.

Cross compiling
===============

See `this wiki page <https://gitlab.kitware.com/cmake/community/wikis/doc/cmake/CrossCompiling>`_ for
generic instructions on how to cross-compile with CMake. It goes into detailed
explanations and may seem daunting, but it is not. On the wiki page there are
several examples including toolchain files. Go directly to the
``Information how to set up various cross compiling toolchains`` section
for a quick solution.

Also see the `LLVM-related variables`_ section for variables used when
cross-compiling.

Embedding LLVM in your project
==============================

From LLVM 3.5 onwards the CMake build system exports LLVM libraries as
importable CMake targets. This means that clients of LLVM can now reliably use
CMake to develop their own LLVM-based projects against an installed version of
LLVM regardless of how it was built.

Here is a simple example of a CMakeLists.txt file that imports the LLVM libraries
and uses them to build a simple application ``simple-tool``.

.. code-block:: cmake

  cmake_minimum_required(VERSION 3.20.0)
  project(SimpleProject)

  find_package(LLVM REQUIRED CONFIG)

  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  # Set your project compile flags.
  # E.g. if using the C++ header files
  # you will need to enable C++11 support
  # for your compiler.

  include_directories(${LLVM_INCLUDE_DIRS})
  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})

  # Now build our tools
  add_executable(simple-tool tool.cpp)

  # Find the libraries that correspond to the LLVM components
  # that we wish to use
  llvm_map_components_to_libnames(llvm_libs support core irreader)

  # Link against LLVM libraries
  target_link_libraries(simple-tool ${llvm_libs})

The ``find_package(...)`` directive when used in CONFIG mode (as in the above
example) will look for the ``LLVMConfig.cmake`` file in various locations (see
cmake manual for details).  It creates a ``LLVM_DIR`` cache entry to save the
directory where ``LLVMConfig.cmake`` is found or allows the user to specify the
directory (e.g. by passing ``-DLLVM_DIR=/usr/lib/cmake/llvm`` to
the ``cmake`` command or by setting it directly in ``ccmake`` or ``cmake-gui``).

This file is available in two different locations.

* ``<LLVM_INSTALL_PACKAGE_DIR>/LLVMConfig.cmake`` where
  ``<LLVM_INSTALL_PACKAGE_DIR>`` is the location where LLVM CMake modules are
  installed as part of an installed version of LLVM. This is typically
  ``cmake/llvm/`` within the lib directory. On Linux, this is typically
  ``/usr/lib/cmake/llvm/LLVMConfig.cmake``.

* ``<LLVM_BUILD_ROOT>/lib/cmake/llvm/LLVMConfig.cmake`` where
  ``<LLVM_BUILD_ROOT>`` is the root of the LLVM build tree. **Note: this is only
  available when building LLVM with CMake.**

If LLVM is installed in your operating system's normal installation prefix (e.g.
on Linux this is usually ``/usr/``) ``find_package(LLVM ...)`` will
automatically find LLVM if it is installed correctly. If LLVM is not installed
or you wish to build directly against the LLVM build tree you can use
``LLVM_DIR`` as previously mentioned.

The ``LLVMConfig.cmake`` file sets various useful variables. Notable variables
include

``LLVM_CMAKE_DIR``
  The path to the LLVM CMake directory (i.e. the directory containing
  LLVMConfig.cmake).

``LLVM_DEFINITIONS``
  A list of preprocessor defines that should be used when building against LLVM.

``LLVM_ENABLE_ASSERTIONS``
  This is set to ON if LLVM was built with assertions, otherwise OFF.

``LLVM_ENABLE_EH``
  This is set to ON if LLVM was built with exception handling (EH) enabled,
  otherwise OFF.

``LLVM_ENABLE_RTTI``
  This is set to ON if LLVM was built with run time type information (RTTI),
  otherwise OFF.

``LLVM_INCLUDE_DIRS``
  A list of include paths to directories containing LLVM header files.

``LLVM_PACKAGE_VERSION``
  The LLVM version. This string can be used with CMake conditionals, e.g., ``if
  (${LLVM_PACKAGE_VERSION} VERSION_LESS "3.5")``.

``LLVM_TOOLS_BINARY_DIR``
  The path to the directory containing the LLVM tools (e.g. ``llvm-as``).

Notice that in the above example we link ``simple-tool`` against several LLVM
libraries. The list of libraries is determined by using the
``llvm_map_components_to_libnames()`` CMake function. For a list of available
components look at the output of running ``llvm-config --components``.

Note that for LLVM < 3.5 ``llvm_map_components_to_libraries()`` was
used instead of ``llvm_map_components_to_libnames()``. This is now deprecated
and will be removed in a future version of LLVM.

.. _cmake-out-of-source-pass:

Developing LLVM passes out of source
------------------------------------

It is possible to develop LLVM passes out of LLVM's source tree (i.e. against an
installed or built LLVM). An example of a project layout is provided below.

.. code-block:: none

  <project dir>/
      |
      CMakeLists.txt
      <pass name>/
          |
          CMakeLists.txt
          Pass.cpp
          ...

Contents of ``<project dir>/CMakeLists.txt``:

.. code-block:: cmake

  find_package(LLVM REQUIRED CONFIG)

  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})
  include_directories(${LLVM_INCLUDE_DIRS})

  add_subdirectory(<pass name>)

Contents of ``<project dir>/<pass name>/CMakeLists.txt``:

.. code-block:: cmake

  add_library(LLVMPassname MODULE Pass.cpp)

Note if you intend for this pass to be merged into the LLVM source tree at some
point in the future it might make more sense to use LLVM's internal
``add_llvm_library`` function with the MODULE argument instead by...


Adding the following to ``<project dir>/CMakeLists.txt`` (after
``find_package(LLVM ...)``)

.. code-block:: cmake

  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
  include(AddLLVM)

And then changing ``<project dir>/<pass name>/CMakeLists.txt`` to

.. code-block:: cmake

  add_llvm_library(LLVMPassname MODULE
    Pass.cpp
    )

When you are done developing your pass, you may wish to integrate it
into the LLVM source tree. You can achieve it in two easy steps:

#. Copying ``<pass name>`` folder into ``<LLVM root>/lib/Transforms`` directory.

#. Adding ``add_subdirectory(<pass name>)`` line into
   ``<LLVM root>/lib/Transforms/CMakeLists.txt``.

Compiler/Platform-specific topics
=================================

Notes for specific compilers and/or platforms.

Windows
-------

**LLVM_COMPILER_JOBS**:STRING
  Specifies the maximum number of parallel compiler jobs to use per project
  when building with msbuild or Visual Studio. Only supported for the Visual
  Studio 2010 CMake generator. 0 means use all processors. Default is 0.

**CMAKE_MT**:STRING
  When compiling with clang-cl, recent CMake versions will default to selecting
  `llvm-mt` as the Manifest Tool instead of Microsoft's `mt.exe`. This will
  often cause errors like:

  .. code-block:: console

    -- Check for working C compiler: [...]clang-cl.exe - broken
    [...]
        MT: command [...] failed (exit code 0x1) with the following output:
        llvm-mt: error: no libxml2
        ninja: build stopped: subcommand failed.

  To work around this error, set `CMAKE_MT=mt`.
