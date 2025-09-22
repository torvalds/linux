==================================================================
Getting Started with the LLVM System using Microsoft Visual Studio
==================================================================


.. contents::
   :local:


Overview
========
Welcome to LLVM on Windows! This document only covers LLVM on Windows using
Visual Studio, not WSL, mingw or cygwin. In order to get started, you first need
to know some basic information.

There are many different projects that compose LLVM. The first piece is the
LLVM suite. This contains all of the tools, libraries, and header files needed
to use LLVM. It contains an assembler, disassembler, bitcode analyzer and
bitcode optimizer. It also contains basic regression tests that can be used to
test the LLVM tools and the Clang front end.

The second piece is the `Clang <https://clang.llvm.org/>`_ front end.  This
component compiles C, C++, Objective C, and Objective C++ code into LLVM
bitcode. Clang typically uses LLVM libraries to optimize the bitcode and emit
machine code. LLVM fully supports the COFF object file format, which is
compatible with all other existing Windows toolchains.

There are more LLVM projects which this document does not discuss.


Requirements
============
Before you begin to use the LLVM system, review the requirements given
below.  This may save you some trouble by knowing ahead of time what hardware
and software you will need.

Hardware
--------
Any system that can adequately run Visual Studio 2019 is fine. The LLVM
source tree including the git index consumes approximately 3GB.
Object files, libraries and executables consume approximately 5GB in
Release mode and much more in Debug mode. SSD drive and >16GB RAM are
recommended.


Software
--------
You will need `Visual Studio <https://visualstudio.microsoft.com/>`_ 2019 or
later, with the latest Update installed. Visual Studio Community Edition
suffices.

You will also need the `CMake <http://www.cmake.org/>`_ build system since it
generates the project files you will use to build with. CMake is bundled with
Visual Studio 2019 so separate installation is not required. If you do install
CMake separately, Visual Studio 2022 will require CMake Version 3.21 or later.

If you would like to run the LLVM tests you will need `Python
<http://www.python.org/>`_. Version 3.8 and newer are known to work. You can
install Python with Visual Studio 2019, from the Microsoft store or from
the `Python web site <http://www.python.org/>`_. We recommend the latter since it
allows you to adjust installation options.

You will need `Git for Windows <https://git-scm.com/>`_ with bash tools, too.
Git for Windows is also bundled with Visual Studio 2019.


Getting Started
===============
Here's the short story for getting up and running quickly with LLVM.
These instruction were tested with Visual Studio 2019 and Python 3.9.6:

1. Download and install `Visual Studio <https://visualstudio.microsoft.com/>`_.
2. In the Visual Studio installer, Workloads tab, select the
   **Desktop development with C++** workload. Under Individual components tab,
   select **Git for Windows**.
3. Complete the Visual Studio installation.
4. Download and install the latest `Python 3 release <http://www.python.org/>`_.
5. In the first install screen, select both **Install launcher for all users**
   and **Add Python to the PATH**. This will allow installing psutil for all
   users for the regression tests and make Python available from the command
   line.
6. In the second install screen, select (again) **Install for all users** and
   if you want to develop `lldb <https://lldb.llvm.org/>`_, selecting
   **Download debug binaries** is useful.
7. Complete the Python installation.
8. Run a "Developer Command Prompt for VS 2019" **as administrator**. This command
    prompt provides correct path and environment variables to Visual Studio and
    the installed tools.
9. In the terminal window, type the commands:

   .. code-block:: bat

     c:
     cd \

  You may install the llvm sources in other location than ``c:\llvm`` but do not
  install into a path containing spaces (e.g. ``c:\Documents and Settings\...``)
  as it will fail.

10. Register the Microsoft Debug Interface Access (DIA) DLLs

    .. code-block:: bat

     regsvr32 "%VSINSTALLDIR%\DIA SDK\bin\msdia140.dll"
     regsvr32 "%VSINSTALLDIR%\DIA SDK\bin\amd64\msdia140.dll"

 The DIA library is required for LLVM PDB tests and
 `LLDB development <https://lldb.llvm.org/resources/build.html>`_.

11. Install psutil and obtain LLVM source code:

    .. code-block:: bat

     pip install psutil
     git clone https://github.com/llvm/llvm-project.git llvm

 Instead of ``git clone`` you may download a compressed source distribution
 from the `releases page <https://github.com/llvm/llvm-project/releases>`_.
 Select the last link: ``Source code (zip)`` and unpack the downloaded file using
 Windows Explorer built-in zip support or any other unzip tool.

12. Finally, configure LLVM using CMake:

    .. code-block:: bat

       cmake -S llvm\llvm -B build -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 -Thost=x64
       exit

   ``LLVM_ENABLE_PROJECTS`` specifies any additional LLVM projects you want to
   build while ``LLVM_TARGETS_TO_BUILD`` selects the compiler targets. If
   ``LLVM_TARGETS_TO_BUILD`` is omitted by default all targets are built
   slowing compilation and using more disk space.
   See the :doc:`LLVM CMake guide <CMake>` for detailed information about
   how to configure the LLVM build.

   The ``cmake`` command line tool is bundled with Visual Studio but its GUI is
   not. You may install `CMake <http://www.cmake.org/>`_ to use its GUI to change
   CMake variables or modify the above command line.

   * Once CMake is installed then the simplest way is to just start the
     CMake GUI, select the directory where you have LLVM extracted to, and
     the default options should all be fine.  One option you may really
     want to change, regardless of anything else, might be the
     ``CMAKE_INSTALL_PREFIX`` setting to select a directory to INSTALL to
     once compiling is complete, although installation is not mandatory for
     using LLVM.  Another important option is ``LLVM_TARGETS_TO_BUILD``,
     which controls the LLVM target architectures that are included on the
     build.
   * CMake generates project files for all build types. To select a specific
     build type, use the Configuration manager from the VS IDE or the
     ``/property:Configuration`` command line option when using MSBuild.
   * By default, the Visual Studio project files generated by CMake use the
     32-bit toolset. If you are developing on a 64-bit version of Windows and
     want to use the 64-bit toolset, pass the ``-Thost=x64`` flag when
     generating the Visual Studio solution. This requires CMake 3.8.0 or later.

13. Start Visual Studio and select configuration:

   In the directory you created the project files will have an ``llvm.sln``
   file, just double-click on that to open Visual Studio. The default Visual
   Studio configuration is **Debug** which is slow and generates a huge amount
   of debug information on disk. For now, we recommend selecting **Release**
   configuration for the LLVM project which will build the fastest or
   **RelWithDebInfo** which is also several time larger than Release.
   Another technique is to build all of LLVM in Release mode and change
   compiler flags, disabling optimization and enabling debug information, only
   for specific libraries or source files you actually need to debug.

14. Test LLVM in Visual Studio:

   You can run LLVM tests by merely building the project "check-all". The test
   results will be shown in the VS output window. Once the build succeeds, you
   have verified a working LLVM development environment!

   You should not see any unexpected failures, but will see many unsupported
   tests and expected failures:

   ::

    114>Testing Time: 1124.66s
    114>  Skipped          :    39
    114>  Unsupported      : 21649
    114>  Passed           : 51615
    114>  Expectedly Failed:    93
    ========== Build: 114 succeeded, 0 failed, 321 up-to-date, 0 skipped ==========``

Alternatives to manual installation
===================================
Instead of the steps above, to simplify the installation procedure you can use
`Chocolatey <https://chocolatey.org/>`_ as package manager.
After the `installation <https://chocolatey.org/install>`_ of Chocolatey,
run these commands in an admin shell to install the required tools:

.. code-block:: bat

   choco install -y git cmake python3
   pip3 install psutil

There is also a Windows
`Dockerfile <https://github.com/llvm/llvm-zorg/blob/main/buildbot/google/docker/windows-base-vscode2019/Dockerfile>`_
with the entire build tool chain. This can be used to test the build with a
tool chain different from your host installation or to create build servers.

Next steps
==========
1. Read the documentation.
2. Seriously, read the documentation.
3. Remember that you were warned twice about reading the documentation.

Test LLVM on the command line:
------------------------------
The LLVM tests can be run by changing directory to the llvm source
directory and running:

.. code-block:: bat

  c:\llvm> python ..\build\Release\bin\llvm-lit.py llvm\test

This example assumes that Python is in your PATH variable, which would be
after **Add Python to the PATH** was selected during Python installation.
If you had opened a command window prior to Python installation, you would
have to close and reopen it to get the updated PATH.

A specific test or test directory can be run with:

.. code-block:: bat

  c:\llvm> python ..\build\Release\bin\llvm-lit.py llvm\test\Transforms\Util

Build the LLVM Suite:
---------------------
* The projects may still be built individually, but to build them all do
  not just select all of them in batch build (as some are meant as
  configuration projects), but rather select and build just the
  ``ALL_BUILD`` project to build everything, or the ``INSTALL`` project,
  which first builds the ``ALL_BUILD`` project, then installs the LLVM
  headers, libs, and other useful things to the directory set by the
  ``CMAKE_INSTALL_PREFIX`` setting when you first configured CMake.
* The Fibonacci project is a sample program that uses the JIT. Modify the
  project's debugging properties to provide a numeric command line argument
  or run it from the command line.  The program will print the
  corresponding fibonacci value.


Links
=====
This document is just an **introduction** to how to use LLVM to do some simple
things... there are many more interesting and complicated things that you can
do that aren't documented here (but we'll gladly accept a patch if you want to
write something up!).  For more information about LLVM, check out:

* `LLVM homepage <https://llvm.org/>`_
* `LLVM doxygen tree <https://llvm.org/doxygen/>`_
* Additional information about the LLVM directory structure and tool chain
  can be found on the main :doc:`GettingStarted` page.
* If you are having problems building or using LLVM, or if you have any other
  general questions about LLVM, please consult the
  :doc:`Frequently Asked Questions <FAQ>` page.
