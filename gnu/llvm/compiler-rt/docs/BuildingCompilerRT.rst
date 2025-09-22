.. _BuildingCompilerRT:

===============
Building Compiler-RT
===============

.. contents::
  :local:

.. _build instructions:

The instructions on this page are aimed at vendors who ship Compiler-RT as part of an
operating system distribution, a toolchain or similar shipping vehicles. If you
are a user merely trying to use Compiler-RT in your program, you most likely want to
refer to your vendor's documentation, or to the general documentation for using
LLVM, Clang, the various santizers, etc.

CMake Options
=============

Here are some of the CMake variables that are used often, along with a
brief explanation and LLVM-specific notes. For full documentation, check the
CMake docs or execute ``cmake --help-variable VARIABLE_NAME``.

**CMAKE_BUILD_TYPE**:STRING
  Sets the build type for ``make`` based generators. Possible values are
  Release, Debug, RelWithDebInfo and MinSizeRel. On systems like Visual Studio
  the user sets the build type with the IDE settings.

**CMAKE_INSTALL_PREFIX**:PATH
  Path where LLVM will be installed if "make install" is invoked or the
  "INSTALL" target is built.

**CMAKE_CXX_COMPILER**:STRING
  The C++ compiler to use when building and testing Compiler-RT.


.. _compiler-rt-specific options:

Compiler-RT specific options
-----------------------

.. option:: COMPILER_RT_INSTALL_PATH:PATH

  **Default**: ```` (empty relative path)

  Prefix for directories where built Compiler-RT artifacts should be installed.
  Can be an absolute path, like the default empty string, in which case it is
  relative ``CMAKE_INSTALL_PREFIX``. If setting a relative path, make sure to
  include the ``:PATH`` with your ``-D``, i.e. use
  ``-DCOMPILER_RT_INSTALL_PATH:PATH=...`` not
  ``-DCOMPILER_RT_INSTALL_PATH=...``, otherwise CMake will convert the
  path to an absolute path.

.. option:: COMPILER_RT_INSTALL_LIBRARY_DIR:PATH

  **Default**: ``lib``

  Path where built Compiler-RT libraries should be installed. If a relative
  path, relative to ``COMPILER_RT_INSTALL_PATH``.

.. option:: COMPILER_RT_INSTALL_BINARY_DIR:PATH

  **Default**: ``bin``

  Path where built Compiler-RT executables should be installed. If a relative
  path, relative to ``COMPILER_RT_INSTALL_PATH``.

.. option:: COMPILER_RT_INSTALL_INCLUDE_DIR:PATH

  **Default**: ``include``

  Path where Compiler-RT headers should be installed. If a relative
  path, relative to ``COMPILER_RT_INSTALL_PATH``.

.. option:: COMPILER_RT_INSTALL_DATA_DIR:PATH

  **Default**: ``share``

  Path where Compiler-RT data should be installed. If a relative
  path, relative to ``COMPILER_RT_INSTALL_PATH``.

.. _LLVM-specific variables:

LLVM-specific options
---------------------

.. option:: LLVM_LIBDIR_SUFFIX:STRING

  Extra suffix to append to the directory where libraries are to be
  installed. On a 64-bit architecture, one could use ``-DLLVM_LIBDIR_SUFFIX=64``
  to install libraries to ``/usr/lib64``.
