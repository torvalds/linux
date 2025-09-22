========================
Creating an LLVM Project
========================

.. contents::
   :local:

Overview
========

The LLVM build system is designed to facilitate the building of third party
projects that use LLVM header files, libraries, and tools.  In order to use
these facilities, a ``Makefile`` from a project must do the following things:

* Set ``make`` variables. There are several variables that a ``Makefile`` needs
  to set to use the LLVM build system:

  * ``PROJECT_NAME`` - The name by which your project is known.
  * ``LLVM_SRC_ROOT`` - The root of the LLVM source tree.
  * ``LLVM_OBJ_ROOT`` - The root of the LLVM object tree.
  * ``PROJ_SRC_ROOT`` - The root of the project's source tree.
  * ``PROJ_OBJ_ROOT`` - The root of the project's object tree.
  * ``PROJ_INSTALL_ROOT`` - The root installation directory.
  * ``LEVEL`` - The relative path from the current directory to the
    project's root ``($PROJ_OBJ_ROOT)``.

* Include ``Makefile.config`` from ``$(LLVM_OBJ_ROOT)``.

* Include ``Makefile.rules`` from ``$(LLVM_SRC_ROOT)``.

There are two ways that you can set all of these variables:

* You can write your own ``Makefiles`` which hard-code these values.

* You can use the pre-made LLVM sample project. This sample project includes
  ``Makefiles``, a configure script that can be used to configure the location
  of LLVM, and the ability to support multiple object directories from a single
  source directory.

If you want to devise your own build system, studying other projects and LLVM
``Makefiles`` will probably provide enough information on how to write your own
``Makefiles``.

Source Tree Layout
==================

In order to use the LLVM build system, you will want to organize your source
code so that it can benefit from the build system's features.  Mainly, you want
your source tree layout to look similar to the LLVM source tree layout.

Underneath your top level directory, you should have the following directories:

**lib**

    This subdirectory should contain all of your library source code.  For each
    library that you build, you will have one directory in **lib** that will
    contain that library's source code.

    Libraries can be object files, archives, or dynamic libraries.  The **lib**
    directory is just a convenient place for libraries as it places them all in
    a directory from which they can be linked later.

**include**

    This subdirectory should contain any header files that are global to your
    project. By global, we mean that they are used by more than one library or
    executable of your project.

    By placing your header files in **include**, they will be found
    automatically by the LLVM build system.  For example, if you have a file
    **include/jazz/note.h**, then your source files can include it simply with
    **#include "jazz/note.h"**.

**tools**

    This subdirectory should contain all of your source code for executables.
    For each program that you build, you will have one directory in **tools**
    that will contain that program's source code.

**test**

    This subdirectory should contain tests that verify that your code works
    correctly.  Automated tests are especially useful.

    Currently, the LLVM build system provides basic support for tests. The LLVM
    system provides the following:

* LLVM contains regression tests in ``llvm/test``.  These tests are run by the
  :doc:`Lit <CommandGuide/lit>` testing tool.  This test procedure uses ``RUN``
  lines in the actual test case to determine how to run the test.  See the
  :doc:`TestingGuide` for more details.

* LLVM contains an optional package called ``llvm-test``, which provides
  benchmarks and programs that are known to compile with the Clang front
  end. You can use these programs to test your code, gather statistical
  information, and compare it to the current LLVM performance statistics.

  Currently, there is no way to hook your tests directly into the ``llvm/test``
  testing harness. You will simply need to find a way to use the source
  provided within that directory on your own.

Typically, you will want to build your **lib** directory first followed by your
**tools** directory.

Writing LLVM Style Makefiles
============================

The LLVM build system provides a convenient way to build libraries and
executables.  Most of your project Makefiles will only need to define a few
variables.  Below is a list of the variables one can set and what they can
do:

Required Variables
------------------

``LEVEL``

    This variable is the relative path from this ``Makefile`` to the top
    directory of your project's source code.  For example, if your source code
    is in ``/tmp/src``, then the ``Makefile`` in ``/tmp/src/jump/high``
    would set ``LEVEL`` to ``"../.."``.

Variables for Building Subdirectories
-------------------------------------

``DIRS``

    This is a space separated list of subdirectories that should be built.  They
    will be built, one at a time, in the order specified.

``PARALLEL_DIRS``

    This is a list of directories that can be built in parallel. These will be
    built after the directories in DIRS have been built.

``OPTIONAL_DIRS``

    This is a list of directories that can be built if they exist, but will not
    cause an error if they do not exist.  They are built serially in the order
    in which they are listed.

Variables for Building Libraries
--------------------------------

``LIBRARYNAME``

    This variable contains the base name of the library that will be built.  For
    example, to build a library named ``libsample.a``, ``LIBRARYNAME`` should
    be set to ``sample``.

``BUILD_ARCHIVE``

    By default, a library is a ``.o`` file that is linked directly into a
    program.  To build an archive (also known as a static library), set the
    ``BUILD_ARCHIVE`` variable.

``SHARED_LIBRARY``

    If ``SHARED_LIBRARY`` is defined in your Makefile, a shared (or dynamic)
    library will be built.

Variables for Building Programs
-------------------------------

``TOOLNAME``

    This variable contains the name of the program that will be built.  For
    example, to build an executable named ``sample``, ``TOOLNAME`` should be set
    to ``sample``.

``USEDLIBS``

    This variable holds a space separated list of libraries that should be
    linked into the program.  These libraries must be libraries that come from
    your **lib** directory.  The libraries must be specified without their
    ``lib`` prefix.  For example, to link ``libsample.a``, you would set
    ``USEDLIBS`` to ``sample.a``.

    Note that this works only for statically linked libraries.

``LLVMLIBS``

    This variable holds a space separated list of libraries that should be
    linked into the program.  These libraries must be LLVM libraries.  The
    libraries must be specified without their ``lib`` prefix.  For example, to
    link with a driver that performs an IR transformation you might set
    ``LLVMLIBS`` to this minimal set of libraries ``LLVMSupport.a LLVMCore.a
    LLVMBitReader.a LLVMAsmParser.a LLVMAnalysis.a LLVMTransformUtils.a
    LLVMScalarOpts.a LLVMTarget.a``.

    Note that this works only for statically linked libraries. LLVM is split
    into a large number of static libraries, and the list of libraries you
    require may be much longer than the list above. To see a full list of
    libraries use: ``llvm-config --libs all``.  Using ``LINK_COMPONENTS`` as
    described below, obviates the need to set ``LLVMLIBS``.

``LINK_COMPONENTS``

    This variable holds a space separated list of components that the LLVM
    ``Makefiles`` pass to the ``llvm-config`` tool to generate a link line for
    the program. For example, to link with all LLVM libraries use
    ``LINK_COMPONENTS = all``.

``LIBS``

    To link dynamic libraries, add ``-l<library base name>`` to the ``LIBS``
    variable.  The LLVM build system will look in the same places for dynamic
    libraries as it does for static libraries.

    For example, to link ``libsample.so``, you would have the following line in
    your ``Makefile``:

        .. code-block:: makefile

          LIBS += -lsample

Note that ``LIBS`` must occur in the Makefile after the inclusion of
``Makefile.common``.

Miscellaneous Variables
-----------------------

``CFLAGS`` & ``CPPFLAGS``

    This variable can be used to add options to the C and C++ compiler,
    respectively.  It is typically used to add options that tell the compiler
    the location of additional directories to search for header files.

    It is highly suggested that you append to ``CFLAGS`` and ``CPPFLAGS`` as
    opposed to overwriting them.  The LLVM ``Makefiles`` may already have
    useful options in them that you may not want to overwrite.

Placement of Object Code
========================

The final location of built libraries and executables will depend upon whether
you do a ``Debug``, ``Release``, or ``Profile`` build.

Libraries

    All libraries (static and dynamic) will be stored in
    ``PROJ_OBJ_ROOT/<type>/lib``, where *type* is ``Debug``, ``Release``, or
    ``Profile`` for a debug, optimized, or profiled build, respectively.

Executables

    All executables will be stored in ``PROJ_OBJ_ROOT/<type>/bin``, where *type*
    is ``Debug``, ``Release``, or ``Profile`` for a debug, optimized, or
    profiled build, respectively.

Further Help
============

If you have any questions or need any help creating an LLVM project, the LLVM
team would be more than happy to help.  You can always post your questions to
the `Discourse forums
<https://discourse.llvm.org>`_.
