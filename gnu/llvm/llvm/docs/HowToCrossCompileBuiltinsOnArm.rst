===================================================================
How to Cross Compile Compiler-rt Builtins For Arm
===================================================================

Introduction
============

This document contains information about building and testing the builtins part
of compiler-rt for an Arm target, from an x86_64 Linux machine.

While this document concentrates on Arm and Linux the general principles should
apply to other targets supported by compiler-rt. Further contributions for other
targets are welcome.

The instructions in this document depend on libraries and programs external to
LLVM, there are many ways to install and configure these dependencies so you
may need to adapt the instructions here to fit your own local situation.

Prerequisites
=============

In this use case we'll be using cmake on a Debian-based Linux system,
cross-compiling from an x86_64 host to a hard-float Armv7-A target. We'll be
using as many of the LLVM tools as we can, but it is possible to use GNU
equivalents.

 * ``A build of LLVM/clang for the llvm-tools and llvm-config``
 * ``A clang executable with support for the ARM target``
 * ``compiler-rt sources``
 * ``The qemu-arm user mode emulator``
 * ``An arm-linux-gnueabihf sysroot``

In this example we will be using ninja.

See https://compiler-rt.llvm.org/ for more information about the dependencies
on clang and LLVM.

See https://llvm.org/docs/GettingStarted.html for information about obtaining
the source for LLVM and compiler-rt. Note that the getting started guide
places compiler-rt in the projects subdirectory, but this is not essential and
if you are using the BaremetalARM.cmake cache for v6-M, v7-M and v7-EM then
compiler-rt must be placed in the runtimes directory.

``qemu-arm`` should be available as a package for your Linux distribution.

The most complicated of the prerequisites to satisfy is the arm-linux-gnueabihf
sysroot. In theory it is possible to use the Linux distributions multiarch
support to fulfill the dependencies for building but unfortunately due to
/usr/local/include being added some host includes are selected. The easiest way
to supply a sysroot is to download the arm-linux-gnueabihf toolchain. This can
be found at:
* https://developer.arm.com/open-source/gnu-toolchain/gnu-a/downloads for gcc 8 and above
* https://releases.linaro.org/components/toolchain/binaries/ for gcc 4.9 to 7.3

Building compiler-rt builtins for Arm
=====================================
We will be doing a standalone build of compiler-rt using the following cmake
options.

* ``path/to/compiler-rt``
* ``-G Ninja``
* ``-DCMAKE_AR=/path/to/llvm-ar``
* ``-DCMAKE_ASM_COMPILER_TARGET="arm-linux-gnueabihf"``
* ``-DCMAKE_ASM_FLAGS="build-c-flags"``
* ``-DCMAKE_C_COMPILER=/path/to/clang``
* ``-DCMAKE_C_COMPILER_TARGET="arm-linux-gnueabihf"``
* ``-DCMAKE_C_FLAGS="build-c-flags"``
* ``-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"``
* ``-DCMAKE_NM=/path/to/llvm-nm``
* ``-DCMAKE_RANLIB=/path/to/llvm-ranlib``
* ``-DCOMPILER_RT_BUILD_BUILTINS=ON``
* ``-DCOMPILER_RT_BUILD_LIBFUZZER=OFF``
* ``-DCOMPILER_RT_BUILD_MEMPROF=OFF``
* ``-DCOMPILER_RT_BUILD_PROFILE=OFF``
* ``-DCOMPILER_RT_BUILD_SANITIZERS=OFF``
* ``-DCOMPILER_RT_BUILD_XRAY=OFF``
* ``-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON``
* ``-DLLVM_CONFIG_PATH=/path/to/llvm-config``

The ``build-c-flags`` need to be sufficient to pass the C-make compiler check,
compile compiler-rt, and if you are running the tests, compile and link the
tests. When cross-compiling with clang we will need to pass sufficient
information to generate code for the Arm architecture we are targeting. We will
need to select the Arm target, select the Armv7-A architecture and choose
between using Arm or Thumb.
instructions. For example:

* ``--target=arm-linux-gnueabihf``
* ``-march=armv7a``
* ``-mthumb``

When using a GCC arm-linux-gnueabihf toolchain the following flags are
needed to pick up the includes and libraries:

* ``--gcc-toolchain=/path/to/dir/toolchain``
* ``--sysroot=/path/to/toolchain/arm-linux-gnueabihf/libc``

In this example we will be adding all of the command line options to both
``CMAKE_C_FLAGS`` and ``CMAKE_ASM_FLAGS``. There are cmake flags to pass some of
these options individually which can be used to simplify the ``build-c-flags``:

* ``-DCMAKE_C_COMPILER_TARGET="arm-linux-gnueabihf"``
* ``-DCMAKE_ASM_COMPILER_TARGET="arm-linux-gnueabihf"``
* ``-DCMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN=/path/to/dir/toolchain``
* ``-DCMAKE_SYSROOT=/path/to/dir/toolchain/arm-linux-gnueabihf/libc``

Once cmake has completed the builtins can be built with ``ninja builtins``

Testing compiler-rt builtins using qemu-arm
===========================================
To test the builtins library we need to add a few more cmake flags to enable
testing and set up the compiler and flags for test case. We must also tell
cmake that we wish to run the tests on ``qemu-arm``.

* ``-DCOMPILER_RT_EMULATOR="qemu-arm -L /path/to/armhf/sysroot``
* ``-DCOMPILER_RT_INCLUDE_TESTS=ON``
* ``-DCOMPILER_RT_TEST_COMPILER="/path/to/clang"``
* ``-DCOMPILER_RT_TEST_COMPILER_CFLAGS="test-c-flags"``

The ``/path/to/armhf/sysroot`` should be the same as the one passed to
``--sysroot`` in the "build-c-flags".

The "test-c-flags" need to include the target, architecture, gcc-toolchain,
sysroot and arm/thumb state. The additional cmake defines such as
``CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN`` do not apply when building the tests. If
you have put all of these in "build-c-flags" then these can be repeated. If you
wish to use lld to link the tests then add ``"-fuse-ld=lld``.

Once cmake has completed the tests can be built and run using
``ninja check-builtins``

Troubleshooting
===============

The cmake try compile stage fails
---------------------------------
At an early stage cmake will attempt to compile and link a simple C program to
test if the toolchain is working.

This stage can often fail at link time if the ``--sysroot=`` and
``--gcc-toolchain=`` options are not passed to the compiler. Check the
``CMAKE_C_FLAGS`` and ``CMAKE_C_COMPILER_TARGET`` flags.

It can be useful to build a simple example outside of cmake with your toolchain
to make sure it is working. For example: ``clang --target=arm-linux-gnueabi -march=armv7a --gcc-toolchain=/path/to/gcc-toolchain --sysroot=/path/to/gcc-toolchain/arm-linux-gnueabihf/libc helloworld.c``

Clang uses the host header files
--------------------------------
On debian based systems it is possible to install multiarch support for
arm-linux-gnueabi and arm-linux-gnueabihf. In many cases clang can successfully
use this multiarch support when ``--gcc-toolchain=`` and ``--sysroot=`` are not supplied.
Unfortunately clang adds ``/usr/local/include`` before
``/usr/include/arm-linux-gnueabihf`` leading to errors when compiling the hosts
header files.

The multiarch support is not sufficient to build the builtins you will need to
use a separate arm-linux-gnueabihf toolchain.

No target passed to clang
-------------------------
If clang is not given a target it will typically use the host target, this will
not understand the Arm assembly language files resulting in error messages such
as ``error: unknown directive .syntax unified``.

You can check the clang invocation in the error message to see if there is no
``--target`` or if it is set incorrectly. The cause is usually
``CMAKE_ASM_FLAGS`` not containing ``--target`` or ``CMAKE_ASM_COMPILER_TARGET`` not being present.

Arm architecture not given
--------------------------
The ``--target=arm-linux-gnueabihf`` will default to arm architecture v4t which
cannot assemble the barrier instructions used in the synch_and_fetch source
files.

The cause is usually a missing ``-march=armv7a`` from the ``CMAKE_ASM_FLAGS``.

Compiler-rt builds but the tests fail to build
----------------------------------------------
The flags used to build the tests are not the same as those used to build the
builtins. The c flags are provided by ``COMPILER_RT_TEST_COMPILE_CFLAGS`` and
the ``CMAKE_C_COMPILER_TARGET``, ``CMAKE_ASM_COMPILER_TARGET``,
``CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN`` and ``CMAKE_SYSROOT`` flags are not
applied.

Make sure that ``COMPILER_RT_TEST_COMPILE_CFLAGS`` contains all the necessary
information.


Modifications for other Targets
===============================

Arm Soft-Float Target
---------------------
The instructions for the Arm hard-float target can be used for the soft-float
target by substituting soft-float equivalents for the sysroot and target. The
target to use is:

* ``-DCMAKE_C_COMPILER_TARGET=arm-linux-gnueabi``

Depending on whether you want to use floating point instructions or not you
may need extra c-flags such as ``-mfloat-abi=softfp`` for use of floating-point
instructions, and ``-mfloat-abi=soft -mfpu=none`` for software floating-point
emulation.

You will need to use an arm-linux-gnueabi GNU toolchain for soft-float.

AArch64 Target
--------------
The instructions for Arm can be used for AArch64 by substituting AArch64
equivalents for the sysroot, emulator and target.

* ``-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu``
* ``-DCOMPILER_RT_EMULATOR="qemu-aarch64 -L /path/to/aarch64/sysroot``

The CMAKE_C_FLAGS and COMPILER_RT_TEST_COMPILER_CFLAGS may also need:
``"--sysroot=/path/to/aarch64/sysroot --gcc-toolchain=/path/to/gcc-toolchain"``

Armv6-m, Armv7-m and Armv7E-M targets
-------------------------------------
To build and test the libraries using a similar method to Armv7-A is possible
but more difficult. The main problems are:

* There isn't a ``qemu-arm`` user-mode emulator for bare-metal systems. The ``qemu-system-arm`` can be used but this is significantly more difficult to setup.
* The targets to compile compiler-rt have the suffix -none-eabi. This uses the BareMetal driver in clang and by default won't find the libraries needed to pass the cmake compiler check.

As the Armv6-M, Armv7-M and Armv7E-M builds of compiler-rt only use instructions
that are supported on Armv7-A we can still get most of the value of running the
tests using the same ``qemu-arm`` that we used for Armv7-A by building and
running the test cases for Armv7-A but using the builtins compiled for
Armv6-M, Armv7-M or Armv7E-M. This will test that the builtins can be linked
into a binary and execute the tests correctly but it will not catch if the
builtins use instructions that are supported on Armv7-A but not Armv6-M,
Armv7-M and Armv7E-M.

To get the cmake compile test to pass you will need to pass the libraries
needed to successfully link the cmake test via ``CMAKE_CFLAGS``. It is
strongly recommended that you use version 3.6 or above of cmake so you can use
``CMAKE_TRY_COMPILE_TARGET=STATIC_LIBRARY`` to skip the link step.

* ``-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY``
* ``-DCOMPILER_RT_OS_DIR="baremetal"``
* ``-DCOMPILER_RT_BUILD_BUILTINS=ON``
* ``-DCOMPILER_RT_BUILD_SANITIZERS=OFF``
* ``-DCOMPILER_RT_BUILD_XRAY=OFF``
* ``-DCOMPILER_RT_BUILD_LIBFUZZER=OFF``
* ``-DCOMPILER_RT_BUILD_PROFILE=OFF``
* ``-DCMAKE_C_COMPILER=${host_install_dir}/bin/clang``
* ``-DCMAKE_C_COMPILER_TARGET="your *-none-eabi target"``
* ``-DCMAKE_ASM_COMPILER_TARGET="your *-none-eabi target"``
* ``-DCMAKE_AR=/path/to/llvm-ar``
* ``-DCMAKE_NM=/path/to/llvm-nm``
* ``-DCMAKE_RANLIB=/path/to/llvm-ranlib``
* ``-DCOMPILER_RT_BAREMETAL_BUILD=ON``
* ``-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON``
* ``-DLLVM_CONFIG_PATH=/path/to/llvm-config``
* ``-DCMAKE_C_FLAGS="build-c-flags"``
* ``-DCMAKE_ASM_FLAGS="build-c-flags"``
* ``-DCOMPILER_RT_EMULATOR="qemu-arm -L /path/to/armv7-A/sysroot"``
* ``-DCOMPILER_RT_INCLUDE_TESTS=ON``
* ``-DCOMPILER_RT_TEST_COMPILER="/path/to/clang"``
* ``-DCOMPILER_RT_TEST_COMPILER_CFLAGS="test-c-flags"``

The Armv6-M builtins will use the soft-float ABI. When compiling the tests for
Armv7-A we must include ``"-mthumb -mfloat-abi=soft -mfpu=none"`` in the
test-c-flags. We must use an Armv7-A soft-float abi sysroot for ``qemu-arm``.

Depending on the linker used for the test cases you may encounter BuildAttribute
mismatches between the M-profile objects from compiler-rt and the A-profile
objects from the test. The lld linker does not check the profile
BuildAttribute so it can be used to link the tests by adding -fuse-ld=lld to the
``COMPILER_RT_TEST_COMPILER_CFLAGS``.

Alternative using a cmake cache
-------------------------------
If you wish to build, but not test compiler-rt for Armv6-M, Armv7-M or Armv7E-M
the easiest way is to use the BaremetalARM.cmake recipe in clang/cmake/caches.

You will need a bare metal sysroot such as that provided by the GNU ARM
Embedded toolchain.

The libraries can be built with the cmake options:

* ``-DBAREMETAL_ARMV6M_SYSROOT=/path/to/bare/metal/toolchain/arm-none-eabi``
* ``-DBAREMETAL_ARMV7M_SYSROOT=/path/to/bare/metal/toolchain/arm-none-eabi``
* ``-DBAREMETAL_ARMV7EM_SYSROOT=/path/to/bare/metal/toolchain/arm-none-eabi``
* ``-C /path/to/llvm/source/tools/clang/cmake/caches/BaremetalARM.cmake``
* ``/path/to/llvm``

**Note** that for the recipe to work the compiler-rt source must be checked out
into the directory llvm/runtimes. You will also need clang and lld checked out.

