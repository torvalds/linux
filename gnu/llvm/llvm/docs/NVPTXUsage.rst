=============================
User Guide for NVPTX Back-end
=============================

.. contents::
   :local:
   :depth: 3


Introduction
============

To support GPU programming, the NVPTX back-end supports a subset of LLVM IR
along with a defined set of conventions used to represent GPU programming
concepts. This document provides an overview of the general usage of the back-
end, including a description of the conventions used and the set of accepted
LLVM IR.

.. note::

   This document assumes a basic familiarity with CUDA and the PTX
   assembly language. Information about the CUDA Driver API and the PTX assembly
   language can be found in the `CUDA documentation
   <http://docs.nvidia.com/cuda/index.html>`_.



Conventions
===========

Marking Functions as Kernels
----------------------------

In PTX, there are two types of functions: *device functions*, which are only
callable by device code, and *kernel functions*, which are callable by host
code. By default, the back-end will emit device functions. Metadata is used to
declare a function as a kernel function. This metadata is attached to the
``nvvm.annotations`` named metadata object, and has the following format:

.. code-block:: text

   !0 = !{<function-ref>, metadata !"kernel", i32 1}

The first parameter is a reference to the kernel function. The following
example shows a kernel function calling a device function in LLVM IR. The
function ``@my_kernel`` is callable from host code, but ``@my_fmad`` is not.

.. code-block:: llvm

    define float @my_fmad(float %x, float %y, float %z) {
      %mul = fmul float %x, %y
      %add = fadd float %mul, %z
      ret float %add
    }

    define void @my_kernel(ptr %ptr) {
      %val = load float, ptr %ptr
      %ret = call float @my_fmad(float %val, float %val, float %val)
      store float %ret, ptr %ptr
      ret void
    }

    !nvvm.annotations = !{!1}
    !1 = !{ptr @my_kernel, !"kernel", i32 1}

When compiled, the PTX kernel functions are callable by host-side code.


.. _address_spaces:

Address Spaces
--------------

The NVPTX back-end uses the following address space mapping:

   ============= ======================
   Address Space Memory Space
   ============= ======================
   0             Generic
   1             Global
   2             Internal Use
   3             Shared
   4             Constant
   5             Local
   ============= ======================

Every global variable and pointer type is assigned to one of these address
spaces, with 0 being the default address space. Intrinsics are provided which
can be used to convert pointers between the generic and non-generic address
spaces.

As an example, the following IR will define an array ``@g`` that resides in
global device memory.

.. code-block:: llvm

    @g = internal addrspace(1) global [4 x i32] [ i32 0, i32 1, i32 2, i32 3 ]

LLVM IR functions can read and write to this array, and host-side code can
copy data to it by name with the CUDA Driver API.

Note that since address space 0 is the generic space, it is illegal to have
global variables in address space 0.  Address space 0 is the default address
space in LLVM, so the ``addrspace(N)`` annotation is *required* for global
variables.


Triples
-------

The NVPTX target uses the module triple to select between 32/64-bit code
generation and the driver-compiler interface to use. The triple architecture
can be one of ``nvptx`` (32-bit PTX) or ``nvptx64`` (64-bit PTX). The
operating system should be one of ``cuda`` or ``nvcl``, which determines the
interface used by the generated code to communicate with the driver.  Most
users will want to use ``cuda`` as the operating system, which makes the
generated PTX compatible with the CUDA Driver API.

Example: 32-bit PTX for CUDA Driver API: ``nvptx-nvidia-cuda``

Example: 64-bit PTX for CUDA Driver API: ``nvptx64-nvidia-cuda``



.. _nvptx_intrinsics:

NVPTX Intrinsics
================

Address Space Conversion
------------------------

'``llvm.nvvm.ptr.*.to.gen``' Intrinsics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

These are overloaded intrinsics.  You can use these on any pointer types.

.. code-block:: llvm

    declare ptr @llvm.nvvm.ptr.global.to.gen.p0.p1(ptr addrspace(1))
    declare ptr @llvm.nvvm.ptr.shared.to.gen.p0.p3(ptr addrspace(3))
    declare ptr @llvm.nvvm.ptr.constant.to.gen.p0.p4(ptr addrspace(4))
    declare ptr @llvm.nvvm.ptr.local.to.gen.p0.p5(ptr addrspace(5))

Overview:
"""""""""

The '``llvm.nvvm.ptr.*.to.gen``' intrinsics convert a pointer in a non-generic
address space to a generic address space pointer.

Semantics:
""""""""""

These intrinsics modify the pointer value to be a valid generic address space
pointer.


'``llvm.nvvm.ptr.gen.to.*``' Intrinsics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

These are overloaded intrinsics.  You can use these on any pointer types.

.. code-block:: llvm

    declare ptr addrspace(1) @llvm.nvvm.ptr.gen.to.global.p1.p0(ptr)
    declare ptr addrspace(3) @llvm.nvvm.ptr.gen.to.shared.p3.p0(ptr)
    declare ptr addrspace(4) @llvm.nvvm.ptr.gen.to.constant.p4.p0(ptr)
    declare ptr addrspace(5) @llvm.nvvm.ptr.gen.to.local.p5.p0(ptr)

Overview:
"""""""""

The '``llvm.nvvm.ptr.gen.to.*``' intrinsics convert a pointer in the generic
address space to a pointer in the target address space.  Note that these
intrinsics are only useful if the address space of the target address space of
the pointer is known.  It is not legal to use address space conversion
intrinsics to convert a pointer from one non-generic address space to another
non-generic address space.

Semantics:
""""""""""

These intrinsics modify the pointer value to be a valid pointer in the target
non-generic address space.


Reading PTX Special Registers
-----------------------------

'``llvm.nvvm.read.ptx.sreg.*``'
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

.. code-block:: llvm

    declare i32 @llvm.nvvm.read.ptx.sreg.tid.x()
    declare i32 @llvm.nvvm.read.ptx.sreg.tid.y()
    declare i32 @llvm.nvvm.read.ptx.sreg.tid.z()
    declare i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
    declare i32 @llvm.nvvm.read.ptx.sreg.ntid.y()
    declare i32 @llvm.nvvm.read.ptx.sreg.ntid.z()
    declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
    declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.y()
    declare i32 @llvm.nvvm.read.ptx.sreg.ctaid.z()
    declare i32 @llvm.nvvm.read.ptx.sreg.nctaid.x()
    declare i32 @llvm.nvvm.read.ptx.sreg.nctaid.y()
    declare i32 @llvm.nvvm.read.ptx.sreg.nctaid.z()
    declare i32 @llvm.nvvm.read.ptx.sreg.warpsize()

Overview:
"""""""""

The '``@llvm.nvvm.read.ptx.sreg.*``' intrinsics provide access to the PTX
special registers, in particular the kernel launch bounds.  These registers
map in the following way to CUDA builtins:

   ============ =====================================
   CUDA Builtin PTX Special Register Intrinsic
   ============ =====================================
   ``threadId`` ``@llvm.nvvm.read.ptx.sreg.tid.*``
   ``blockIdx`` ``@llvm.nvvm.read.ptx.sreg.ctaid.*``
   ``blockDim`` ``@llvm.nvvm.read.ptx.sreg.ntid.*``
   ``gridDim``  ``@llvm.nvvm.read.ptx.sreg.nctaid.*``
   ============ =====================================


Barriers
--------

'``llvm.nvvm.barrier0``'
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:
"""""""

.. code-block:: llvm

  declare void @llvm.nvvm.barrier0()

Overview:
"""""""""

The '``@llvm.nvvm.barrier0()``' intrinsic emits a PTX ``bar.sync 0``
instruction, equivalent to the ``__syncthreads()`` call in CUDA.


Other Intrinsics
----------------

For the full set of NVPTX intrinsics, please see the
``include/llvm/IR/IntrinsicsNVVM.td`` file in the LLVM source tree.


.. _libdevice:

Linking with Libdevice
======================

The CUDA Toolkit comes with an LLVM bitcode library called ``libdevice`` that
implements many common mathematical functions. This library can be used as a
high-performance math library for any compilers using the LLVM NVPTX target.
The library can be found under ``nvvm/libdevice/`` in the CUDA Toolkit and
there is a separate version for each compute architecture.

For a list of all math functions implemented in libdevice, see
`libdevice Users Guide <http://docs.nvidia.com/cuda/libdevice-users-guide/index.html>`_.

To accommodate various math-related compiler flags that can affect code
generation of libdevice code, the library code depends on a special LLVM IR
pass (``NVVMReflect``) to handle conditional compilation within LLVM IR. This
pass looks for calls to the ``@__nvvm_reflect`` function and replaces them
with constants based on the defined reflection parameters. Such conditional
code often follows a pattern:

.. code-block:: c++

  float my_function(float a) {
    if (__nvvm_reflect("FASTMATH"))
      return my_function_fast(a);
    else
      return my_function_precise(a);
  }

The default value for all unspecified reflection parameters is zero.

The ``NVVMReflect`` pass should be executed early in the optimization
pipeline, immediately after the link stage. The ``internalize`` pass is also
recommended to remove unused math functions from the resulting PTX. For an
input IR module ``module.bc``, the following compilation flow is recommended:

The ``NVVMReflect`` pass will attempt to remove dead code even without
optimizations. This allows potentially incompatible instructions to be avoided
at all optimizations levels by using the ``__CUDA_ARCH`` argument.

1. Save list of external functions in ``module.bc``
2. Link ``module.bc`` with ``libdevice.compute_XX.YY.bc``
3. Internalize all functions not in list from (1)
4. Eliminate all unused internal functions
5. Run ``NVVMReflect`` pass
6. Run standard optimization pipeline

.. note::

  ``linkonce`` and ``linkonce_odr`` linkage types are not suitable for the
  libdevice functions. It is possible to link two IR modules that have been
  linked against libdevice using different reflection variables.

Since the ``NVVMReflect`` pass replaces conditionals with constants, it will
often leave behind dead code of the form:

.. code-block:: llvm

  entry:
    ..
    br i1 true, label %foo, label %bar
  foo:
    ..
  bar:
    ; Dead code
    ..

Therefore, it is recommended that ``NVVMReflect`` is executed early in the
optimization pipeline before dead-code elimination.

The NVPTX TargetMachine knows how to schedule ``NVVMReflect`` at the beginning
of your pass manager; just use the following code when setting up your pass
manager and the PassBuilder will use ``registerPassBuilderCallbacks`` to let
NVPTXTargetMachine::registerPassBuilderCallbacks add the pass to the
pass manager:

.. code-block:: c++

    std::unique_ptr<TargetMachine> TM = ...;
    PassBuilder PB(TM);
    ModulePassManager MPM;
    PB.parsePassPipeline(MPM, ...);

Reflection Parameters
---------------------

The libdevice library currently uses the following reflection parameters to
control code generation:

==================== ======================================================
Flag                 Description
==================== ======================================================
``__CUDA_FTZ=[0,1]`` Use optimized code paths that flush subnormals to zero
==================== ======================================================

The value of this flag is determined by the "nvvm-reflect-ftz" module flag.
The following sets the ftz flag to 1.

.. code-block:: llvm

    !llvm.module.flag = !{!0}
    !0 = !{i32 4, !"nvvm-reflect-ftz", i32 1}

(``i32 4`` indicates that the value set here overrides the value in another
module we link with.  See the `LangRef <LangRef.html#module-flags-metadata>`
for details.)

Executing PTX
=============

The most common way to execute PTX assembly on a GPU device is to use the CUDA
Driver API. This API is a low-level interface to the GPU driver and allows for
JIT compilation of PTX code to native GPU machine code.

Initializing the Driver API:

.. code-block:: c++

    CUdevice device;
    CUcontext context;

    // Initialize the driver API
    cuInit(0);
    // Get a handle to the first compute device
    cuDeviceGet(&device, 0);
    // Create a compute device context
    cuCtxCreate(&context, 0, device);

JIT compiling a PTX string to a device binary:

.. code-block:: c++

    CUmodule module;
    CUfunction function;

    // JIT compile a null-terminated PTX string
    cuModuleLoadData(&module, (void*)PTXString);

    // Get a handle to the "myfunction" kernel function
    cuModuleGetFunction(&function, module, "myfunction");

For full examples of executing PTX assembly, please see the `CUDA Samples
<https://developer.nvidia.com/cuda-downloads>`_ distribution.


Common Issues
=============

ptxas complains of undefined function: __nvvm_reflect
-----------------------------------------------------

When linking with libdevice, the ``NVVMReflect`` pass must be used. See
:ref:`libdevice` for more information.


Tutorial: A Simple Compute Kernel
=================================

To start, let us take a look at a simple compute kernel written directly in
LLVM IR. The kernel implements vector addition, where each thread computes one
element of the output vector C from the input vectors A and B.  To make this
easier, we also assume that only a single CTA (thread block) will be launched,
and that it will be one dimensional.


The Kernel
----------

.. code-block:: llvm

  target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v32:32:32-v64:64:64-v128:128:128-n16:32:64"
  target triple = "nvptx64-nvidia-cuda"

  ; Intrinsic to read X component of thread ID
  declare i32 @llvm.nvvm.read.ptx.sreg.tid.x() readnone nounwind

  define void @kernel(ptr addrspace(1) %A,
                      ptr addrspace(1) %B,
                      ptr addrspace(1) %C) {
  entry:
    ; What is my ID?
    %id = tail call i32 @llvm.nvvm.read.ptx.sreg.tid.x() readnone nounwind

    ; Compute pointers into A, B, and C
    %ptrA = getelementptr float, ptr addrspace(1) %A, i32 %id
    %ptrB = getelementptr float, ptr addrspace(1) %B, i32 %id
    %ptrC = getelementptr float, ptr addrspace(1) %C, i32 %id

    ; Read A, B
    %valA = load float, ptr addrspace(1) %ptrA, align 4
    %valB = load float, ptr addrspace(1) %ptrB, align 4

    ; Compute C = A + B
    %valC = fadd float %valA, %valB

    ; Store back to C
    store float %valC, ptr addrspace(1) %ptrC, align 4

    ret void
  }

  !nvvm.annotations = !{!0}
  !0 = !{ptr @kernel, !"kernel", i32 1}


We can use the LLVM ``llc`` tool to directly run the NVPTX code generator:

.. code-block:: text

  # llc -mcpu=sm_20 kernel.ll -o kernel.ptx


.. note::

  If you want to generate 32-bit code, change ``p:64:64:64`` to ``p:32:32:32``
  in the module data layout string and use ``nvptx-nvidia-cuda`` as the
  target triple.


The output we get from ``llc`` (as of LLVM 3.4):

.. code-block:: text

  //
  // Generated by LLVM NVPTX Back-End
  //

  .version 3.1
  .target sm_20
  .address_size 64

    // .globl kernel
                                          // @kernel
  .visible .entry kernel(
    .param .u64 kernel_param_0,
    .param .u64 kernel_param_1,
    .param .u64 kernel_param_2
  )
  {
    .reg .f32   %f<4>;
    .reg .s32   %r<2>;
    .reg .s64   %rl<8>;

  // %bb.0:                                // %entry
    ld.param.u64    %rl1, [kernel_param_0];
    mov.u32         %r1, %tid.x;
    mul.wide.s32    %rl2, %r1, 4;
    add.s64         %rl3, %rl1, %rl2;
    ld.param.u64    %rl4, [kernel_param_1];
    add.s64         %rl5, %rl4, %rl2;
    ld.param.u64    %rl6, [kernel_param_2];
    add.s64         %rl7, %rl6, %rl2;
    ld.global.f32   %f1, [%rl3];
    ld.global.f32   %f2, [%rl5];
    add.f32         %f3, %f1, %f2;
    st.global.f32   [%rl7], %f3;
    ret;
  }


Dissecting the Kernel
---------------------

Now let us dissect the LLVM IR that makes up this kernel.

Data Layout
^^^^^^^^^^^

The data layout string determines the size in bits of common data types, their
ABI alignment, and their storage size.  For NVPTX, you should use one of the
following:

32-bit PTX:

.. code-block:: llvm

  target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v32:32:32-v64:64:64-v128:128:128-n16:32:64"

64-bit PTX:

.. code-block:: llvm

  target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v32:32:32-v64:64:64-v128:128:128-n16:32:64"


Target Intrinsics
^^^^^^^^^^^^^^^^^

In this example, we use the ``@llvm.nvvm.read.ptx.sreg.tid.x`` intrinsic to
read the X component of the current thread's ID, which corresponds to a read
of register ``%tid.x`` in PTX. The NVPTX back-end supports a large set of
intrinsics.  A short list is shown below; please see
``include/llvm/IR/IntrinsicsNVVM.td`` for the full list.


================================================ ====================
Intrinsic                                        CUDA Equivalent
================================================ ====================
``i32 @llvm.nvvm.read.ptx.sreg.tid.{x,y,z}``     threadIdx.{x,y,z}
``i32 @llvm.nvvm.read.ptx.sreg.ctaid.{x,y,z}``   blockIdx.{x,y,z}
``i32 @llvm.nvvm.read.ptx.sreg.ntid.{x,y,z}``    blockDim.{x,y,z}
``i32 @llvm.nvvm.read.ptx.sreg.nctaid.{x,y,z}``  gridDim.{x,y,z}
``void @llvm.nvvm.barrier0()``                   __syncthreads()
================================================ ====================


Address Spaces
^^^^^^^^^^^^^^

You may have noticed that all of the pointer types in the LLVM IR example had
an explicit address space specifier. What is address space 1? NVIDIA GPU
devices (generally) have four types of memory:

- Global: Large, off-chip memory
- Shared: Small, on-chip memory shared among all threads in a CTA
- Local: Per-thread, private memory
- Constant: Read-only memory shared across all threads

These different types of memory are represented in LLVM IR as address spaces.
There is also a fifth address space used by the NVPTX code generator that
corresponds to the "generic" address space.  This address space can represent
addresses in any other address space (with a few exceptions).  This allows
users to write IR functions that can load/store memory using the same
instructions. Intrinsics are provided to convert pointers between the generic
and non-generic address spaces.

See :ref:`address_spaces` and :ref:`nvptx_intrinsics` for more information.


Kernel Metadata
^^^^^^^^^^^^^^^

In PTX, a function can be either a `kernel` function (callable from the host
program), or a `device` function (callable only from GPU code). You can think
of `kernel` functions as entry-points in the GPU program. To mark an LLVM IR
function as a `kernel` function, we make use of special LLVM metadata. The
NVPTX back-end will look for a named metadata node called
``nvvm.annotations``. This named metadata must contain a list of metadata that
describe the IR. For our purposes, we need to declare a metadata node that
assigns the "kernel" attribute to the LLVM IR function that should be emitted
as a PTX `kernel` function. These metadata nodes take the form:

.. code-block:: text

  !{<function ref>, metadata !"kernel", i32 1}

For the previous example, we have:

.. code-block:: llvm

  !nvvm.annotations = !{!0}
  !0 = !{ptr @kernel, !"kernel", i32 1}

Here, we have a single metadata declaration in ``nvvm.annotations``. This
metadata annotates our ``@kernel`` function with the ``kernel`` attribute.


Running the Kernel
------------------

Generating PTX from LLVM IR is all well and good, but how do we execute it on
a real GPU device? The CUDA Driver API provides a convenient mechanism for
loading and JIT compiling PTX to a native GPU device, and launching a kernel.
The API is similar to OpenCL.  A simple example showing how to load and
execute our vector addition code is shown below. Note that for brevity this
code does not perform much error checking!

.. note::

  You can also use the ``ptxas`` tool provided by the CUDA Toolkit to offline
  compile PTX to machine code (SASS) for a specific GPU architecture. Such
  binaries can be loaded by the CUDA Driver API in the same way as PTX. This
  can be useful for reducing startup time by precompiling the PTX kernels.


.. code-block:: c++

  #include <iostream>
  #include <fstream>
  #include <cassert>
  #include "cuda.h"


  void checkCudaErrors(CUresult err) {
    assert(err == CUDA_SUCCESS);
  }

  /// main - Program entry point
  int main(int argc, char **argv) {
    CUdevice    device;
    CUmodule    cudaModule;
    CUcontext   context;
    CUfunction  function;
    CUlinkState linker;
    int         devCount;

    // CUDA initialization
    checkCudaErrors(cuInit(0));
    checkCudaErrors(cuDeviceGetCount(&devCount));
    checkCudaErrors(cuDeviceGet(&device, 0));

    char name[128];
    checkCudaErrors(cuDeviceGetName(name, 128, device));
    std::cout << "Using CUDA Device [0]: " << name << "\n";

    int devMajor, devMinor;
    checkCudaErrors(cuDeviceComputeCapability(&devMajor, &devMinor, device));
    std::cout << "Device Compute Capability: "
              << devMajor << "." << devMinor << "\n";
    if (devMajor < 2) {
      std::cerr << "ERROR: Device 0 is not SM 2.0 or greater\n";
      return 1;
    }

    std::ifstream t("kernel.ptx");
    if (!t.is_open()) {
      std::cerr << "kernel.ptx not found\n";
      return 1;
    }
    std::string str((std::istreambuf_iterator<char>(t)),
                      std::istreambuf_iterator<char>());

    // Create driver context
    checkCudaErrors(cuCtxCreate(&context, 0, device));

    // Create module for object
    checkCudaErrors(cuModuleLoadDataEx(&cudaModule, str.c_str(), 0, 0, 0));

    // Get kernel function
    checkCudaErrors(cuModuleGetFunction(&function, cudaModule, "kernel"));

    // Device data
    CUdeviceptr devBufferA;
    CUdeviceptr devBufferB;
    CUdeviceptr devBufferC;

    checkCudaErrors(cuMemAlloc(&devBufferA, sizeof(float)*16));
    checkCudaErrors(cuMemAlloc(&devBufferB, sizeof(float)*16));
    checkCudaErrors(cuMemAlloc(&devBufferC, sizeof(float)*16));

    float* hostA = new float[16];
    float* hostB = new float[16];
    float* hostC = new float[16];

    // Populate input
    for (unsigned i = 0; i != 16; ++i) {
      hostA[i] = (float)i;
      hostB[i] = (float)(2*i);
      hostC[i] = 0.0f;
    }

    checkCudaErrors(cuMemcpyHtoD(devBufferA, &hostA[0], sizeof(float)*16));
    checkCudaErrors(cuMemcpyHtoD(devBufferB, &hostB[0], sizeof(float)*16));


    unsigned blockSizeX = 16;
    unsigned blockSizeY = 1;
    unsigned blockSizeZ = 1;
    unsigned gridSizeX  = 1;
    unsigned gridSizeY  = 1;
    unsigned gridSizeZ  = 1;

    // Kernel parameters
    void *KernelParams[] = { &devBufferA, &devBufferB, &devBufferC };

    std::cout << "Launching kernel\n";

    // Kernel launch
    checkCudaErrors(cuLaunchKernel(function, gridSizeX, gridSizeY, gridSizeZ,
                                   blockSizeX, blockSizeY, blockSizeZ,
                                   0, NULL, KernelParams, NULL));

    // Retrieve device data
    checkCudaErrors(cuMemcpyDtoH(&hostC[0], devBufferC, sizeof(float)*16));


    std::cout << "Results:\n";
    for (unsigned i = 0; i != 16; ++i) {
      std::cout << hostA[i] << " + " << hostB[i] << " = " << hostC[i] << "\n";
    }


    // Clean up after ourselves
    delete [] hostA;
    delete [] hostB;
    delete [] hostC;

    // Clean-up
    checkCudaErrors(cuMemFree(devBufferA));
    checkCudaErrors(cuMemFree(devBufferB));
    checkCudaErrors(cuMemFree(devBufferC));
    checkCudaErrors(cuModuleUnload(cudaModule));
    checkCudaErrors(cuCtxDestroy(context));

    return 0;
  }


You will need to link with the CUDA driver and specify the path to cuda.h.

.. code-block:: text

  # clang++ sample.cpp -o sample -O2 -g -I/usr/local/cuda-5.5/include -lcuda

We don't need to specify a path to ``libcuda.so`` since this is installed in a
system location by the driver, not the CUDA toolkit.

If everything goes as planned, you should see the following output when
running the compiled program:

.. code-block:: text

  Using CUDA Device [0]: GeForce GTX 680
  Device Compute Capability: 3.0
  Launching kernel
  Results:
  0 + 0 = 0
  1 + 2 = 3
  2 + 4 = 6
  3 + 6 = 9
  4 + 8 = 12
  5 + 10 = 15
  6 + 12 = 18
  7 + 14 = 21
  8 + 16 = 24
  9 + 18 = 27
  10 + 20 = 30
  11 + 22 = 33
  12 + 24 = 36
  13 + 26 = 39
  14 + 28 = 42
  15 + 30 = 45

.. note::

  You will likely see a different device identifier based on your hardware


Tutorial: Linking with Libdevice
================================

In this tutorial, we show a simple example of linking LLVM IR with the
libdevice library. We will use the same kernel as the previous tutorial,
except that we will compute ``C = pow(A, B)`` instead of ``C = A + B``.
Libdevice provides an ``__nv_powf`` function that we will use.

.. code-block:: llvm

  target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v32:32:32-v64:64:64-v128:128:128-n16:32:64"
  target triple = "nvptx64-nvidia-cuda"

  ; Intrinsic to read X component of thread ID
  declare i32 @llvm.nvvm.read.ptx.sreg.tid.x() readnone nounwind
  ; libdevice function
  declare float @__nv_powf(float, float)

  define void @kernel(ptr addrspace(1) %A,
                      ptr addrspace(1) %B,
                      ptr addrspace(1) %C) {
  entry:
    ; What is my ID?
    %id = tail call i32 @llvm.nvvm.read.ptx.sreg.tid.x() readnone nounwind

    ; Compute pointers into A, B, and C
    %ptrA = getelementptr float, ptr addrspace(1) %A, i32 %id
    %ptrB = getelementptr float, ptr addrspace(1) %B, i32 %id
    %ptrC = getelementptr float, ptr addrspace(1) %C, i32 %id

    ; Read A, B
    %valA = load float, ptr addrspace(1) %ptrA, align 4
    %valB = load float, ptr addrspace(1) %ptrB, align 4

    ; Compute C = pow(A, B)
    %valC = call float @__nv_powf(float %valA, float %valB)

    ; Store back to C
    store float %valC, ptr addrspace(1) %ptrC, align 4

    ret void
  }

  !nvvm.annotations = !{!0}
  !0 = !{ptr @kernel, !"kernel", i32 1}


To compile this kernel, we perform the following steps:

1. Link with libdevice
2. Internalize all but the public kernel function
3. Run ``NVVMReflect`` and set ``__CUDA_FTZ`` to 0
4. Optimize the linked module
5. Codegen the module


These steps can be performed by the LLVM ``llvm-link``, ``opt``, and ``llc``
tools. In a complete compiler, these steps can also be performed entirely
programmatically by setting up an appropriate pass configuration (see
:ref:`libdevice`).

.. code-block:: text

  # llvm-link t2.bc libdevice.compute_20.10.bc -o t2.linked.bc
  # opt -internalize -internalize-public-api-list=kernel -nvvm-reflect-list=__CUDA_FTZ=0 -nvvm-reflect -O3 t2.linked.bc -o t2.opt.bc
  # llc -mcpu=sm_20 t2.opt.bc -o t2.ptx

.. note::

  The ``-nvvm-reflect-list=_CUDA_FTZ=0`` is not strictly required, as any
  undefined variables will default to zero. It is shown here for evaluation
  purposes.


This gives us the following PTX (excerpt):

.. code-block:: text

  //
  // Generated by LLVM NVPTX Back-End
  //

  .version 3.1
  .target sm_20
  .address_size 64

    // .globl kernel
                                          // @kernel
  .visible .entry kernel(
    .param .u64 kernel_param_0,
    .param .u64 kernel_param_1,
    .param .u64 kernel_param_2
  )
  {
    .reg .pred  %p<30>;
    .reg .f32   %f<111>;
    .reg .s32   %r<21>;
    .reg .s64   %rl<8>;

  // %bb.0:                                // %entry
    ld.param.u64  %rl2, [kernel_param_0];
    mov.u32   %r3, %tid.x;
    ld.param.u64  %rl3, [kernel_param_1];
    mul.wide.s32  %rl4, %r3, 4;
    add.s64   %rl5, %rl2, %rl4;
    ld.param.u64  %rl6, [kernel_param_2];
    add.s64   %rl7, %rl3, %rl4;
    add.s64   %rl1, %rl6, %rl4;
    ld.global.f32   %f1, [%rl5];
    ld.global.f32   %f2, [%rl7];
    setp.eq.f32 %p1, %f1, 0f3F800000;
    setp.eq.f32 %p2, %f2, 0f00000000;
    or.pred   %p3, %p1, %p2;
    @%p3 bra  BB0_1;
    bra.uni   BB0_2;
  BB0_1:
    mov.f32   %f110, 0f3F800000;
    st.global.f32   [%rl1], %f110;
    ret;
  BB0_2:                                  // %__nv_isnanf.exit.i
    abs.f32   %f4, %f1;
    setp.gtu.f32  %p4, %f4, 0f7F800000;
    @%p4 bra  BB0_4;
  // %bb.3:                                // %__nv_isnanf.exit5.i
    abs.f32   %f5, %f2;
    setp.le.f32 %p5, %f5, 0f7F800000;
    @%p5 bra  BB0_5;
  BB0_4:                                  // %.critedge1.i
    add.f32   %f110, %f1, %f2;
    st.global.f32   [%rl1], %f110;
    ret;
  BB0_5:                                  // %__nv_isinff.exit.i

    ...

  BB0_26:                                 // %__nv_truncf.exit.i.i.i.i.i
    mul.f32   %f90, %f107, 0f3FB8AA3B;
    cvt.rzi.f32.f32 %f91, %f90;
    mov.f32   %f92, 0fBF317200;
    fma.rn.f32  %f93, %f91, %f92, %f107;
    mov.f32   %f94, 0fB5BFBE8E;
    fma.rn.f32  %f95, %f91, %f94, %f93;
    mul.f32   %f89, %f95, 0f3FB8AA3B;
    // inline asm
    ex2.approx.ftz.f32 %f88,%f89;
    // inline asm
    add.f32   %f96, %f91, 0f00000000;
    ex2.approx.f32  %f97, %f96;
    mul.f32   %f98, %f88, %f97;
    setp.lt.f32 %p15, %f107, 0fC2D20000;
    selp.f32  %f99, 0f00000000, %f98, %p15;
    setp.gt.f32 %p16, %f107, 0f42D20000;
    selp.f32  %f110, 0f7F800000, %f99, %p16;
    setp.eq.f32 %p17, %f110, 0f7F800000;
    @%p17 bra   BB0_28;
  // %bb.27:
    fma.rn.f32  %f110, %f110, %f108, %f110;
  BB0_28:                                 // %__internal_accurate_powf.exit.i
    setp.lt.f32 %p18, %f1, 0f00000000;
    setp.eq.f32 %p19, %f3, 0f3F800000;
    and.pred    %p20, %p18, %p19;
    @!%p20 bra  BB0_30;
    bra.uni   BB0_29;
  BB0_29:
    mov.b32    %r9, %f110;
    xor.b32   %r10, %r9, -2147483648;
    mov.b32    %f110, %r10;
  BB0_30:                                 // %__nv_powf.exit
    st.global.f32   [%rl1], %f110;
    ret;
  }
