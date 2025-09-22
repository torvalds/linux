====================
Clang Linker Wrapper
====================

.. contents::
   :local:

.. _clang-linker-wrapper:

Introduction
============

This tool works as a wrapper of the normal host linking job. This tool is used
to create linked device images for offloading and the necessary runtime calls to
register them. It works by first scanning the linker's input for embedded device
offloading data stored at the ``.llvm.offloading`` section. This section
contains binary data created by the :doc:`ClangOffloadPackager`. The extracted
device files will then be linked. The linked modules will then be wrapped into a
new object file containing the code necessary to register it with the offloading
runtime.

Usage
=====

This tool can be used with the following options. Any arguments not intended
only for the linker wrapper will be forwarded to the wrapped linker job.

.. code-block:: console

  USAGE: clang-linker-wrapper [options] -- <options to passed to the linker>

  OPTIONS:
    --bitcode-library=<kind>-<triple>-<arch>=<path>
                           Extra bitcode library to link
    --cuda-path=<dir>      Set the system CUDA path
    --device-debug         Use debugging
    --device-linker=<value> or <triple>=<value>
                           Arguments to pass to the device linker invocation
    --dry-run              Print program arguments without running
    --embed-bitcode        Embed linked bitcode in the module
    --help-hidden          Display all available options
    --help                 Display available options (--help-hidden for more)
    --host-triple=<triple> Triple to use for the host compilation
    --linker-path=<path>   The linker executable to invoke
    -L <dir>               Add <dir> to the library search path
    -l <libname>           Search for library <libname>
    --opt-level=<O0, O1, O2, or O3>
                           Optimization level for LTO
    --override-image=<kind=file>
                            Uses the provided file as if it were the output of the device link step
    -o <path>              Path to file to write output
    --pass-remarks-analysis=<value>
                           Pass remarks for LTO
    --pass-remarks-missed=<value>
                           Pass remarks for LTO
    --pass-remarks=<value> Pass remarks for LTO
    --print-wrapped-module Print the wrapped module's IR for testing
    --ptxas-arg=<value>    Argument to pass to the 'ptxas' invocation
    --relocatable           Link device code to create a relocatable offloading application
    --save-temps           Save intermediate results
    --sysroot<value>       Set the system root
    --verbose              Verbose output from tools
    --v                    Display the version number and exit
    --                     The separator for the wrapped linker arguments

Relocatable Linking
===================

The ``clang-linker-wrapper`` handles linking embedded device code and then
registering it with the appropriate runtime. Normally, this is only done when
the executable is created so other files containing device code can be linked
together. This can be somewhat problematic for users who wish to ship static
libraries that contain offloading code to users without a compatible offloading
toolchain.

When using a relocatable link with ``-r``, the ``clang-linker-wrapper`` will
perform the device linking and registration eagerly. This will remove the
embedded device code and register it correctly with the runtime. Semantically,
this is similar to creating a shared library object. If standard relocatable
linking is desired, simply do not run the binaries through the
``clang-linker-wrapper``. This will simply append the embedded device code so
that it can be linked later.

Matching
========

The linker wrapper will link extracted device code that is compatible with each
other. Generally, this requires that the target triple and architecture match.
An exception is made when the architecture is listed as ``generic``, which will
cause it be linked with any other device code with the same target triple.

Debugging
=========

The linker wrapper performs a lot of steps internally, such as input matching,
symbol resolution, and image registration. This makes it difficult to debug in
some scenarios. The behavior of the linker-wrapper is controlled mostly through
metadata, described in `clang documentation
<https://clang.llvm.org/docs/OffloadingDesign.html>`_. Intermediate output can
be obtained from the linker-wrapper using the ``--save-temps`` flag. These files
can then be modified.

.. code-block:: sh

  $> clang openmp.c -fopenmp --offload-arch=gfx90a -c
  $> clang openmp.o -fopenmp --offload-arch=gfx90a -Wl,--save-temps
  $> ; Modify temp files.
  $> llvm-objcopy --update-section=.llvm.offloading=out.bc openmp.o

Doing this will allow you to override one of the input files by replacing its
embedded offloading metadata with a user-modified version. However, this will be
more difficult when there are multiple input files. For a very large hammer, the
``--override-image=<kind>=<file>`` flag can be used.

In the following example, we use the ``--save-temps`` to obtain the LLVM-IR just
before running the backend. We then modify it to test altered behavior, and then
compile it to a binary. This can then be passed to the linker-wrapper which will
then ignore all embedded metadata and use the provided image as if it were the
result of the device linking phase.

.. code-block:: sh

  $> clang openmp.c -fopenmp --offload-arch=gfx90a -Wl,--save-temps
  $> ; Modify temp files.
  $> clang --target=amdgcn-amd-amdhsa -mcpu=gfx90a -nogpulib out.bc -o a.out
  $> clang openmp.c -fopenmp --offload-arch=gfx90a -Wl,--override-image=openmp=a.out

Example
=======

This tool links object files with offloading images embedded within it using the
``-fembed-offload-object`` flag in Clang. Given an input file containing the
magic section we can pass it to this tool to extract the data contained at that
section and run a device linking job on it.

.. code-block:: console

  clang-linker-wrapper --host-triple=x86_64 --linker-path=/usr/bin/ld -- <Args>
