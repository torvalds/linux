.. raw:: html

  <style type="text/css">
    .none { background-color: #FFCCCC }
    .part { background-color: #FFFF99 }
    .good { background-color: #CCFF99 }
  </style>

.. role:: none
.. role:: part
.. role:: good

.. contents::
   :local:

=============
HIP Support
=============

HIP (Heterogeneous-Compute Interface for Portability) `<https://github.com/ROCm-Developer-Tools/HIP>`_ is
a C++ Runtime API and Kernel Language. It enables developers to create portable applications for
offloading computation to different hardware platforms from a single source code.

AMD GPU Support
===============

Clang provides HIP support on AMD GPUs via the ROCm platform `<https://rocm.docs.amd.com/en/latest/#>`_.
The ROCm runtime forms the base for HIP host APIs, while HIP device APIs are realized through HIP header
files and the ROCm device library. The Clang driver uses the HIPAMD toolchain to compile HIP device code
to AMDGPU ISA via the AMDGPU backend. The compiled code is then bundled and embedded in the host executables.

Intel GPU Support
=================

Clang provides partial HIP support on Intel GPUs using the CHIP-Star project `<https://github.com/CHIP-SPV/chipStar>`_.
CHIP-Star implements the HIP runtime over oneAPI Level Zero or OpenCL runtime. The Clang driver uses the HIPSPV
toolchain to compile HIP device code into LLVM IR, which is subsequently translated to SPIR-V via the SPIR-V
backend or the out-of-tree LLVM-SPIRV translator. The SPIR-V is then bundled and embedded into the host executables.

.. note::
   While Clang does not directly provide HIP support for NVIDIA GPUs and CPUs, these platforms are supported via other means:

   - NVIDIA GPUs: HIP support is offered through the HIP project `<https://github.com/ROCm-Developer-Tools/HIP>`_, which provides a header-only library for translating HIP runtime APIs into CUDA runtime APIs. The code is subsequently compiled using NVIDIA's `nvcc`.

   - CPUs: HIP support is available through the HIP-CPU runtime library `<https://github.com/ROCm-Developer-Tools/HIP-CPU>`_. This header-only library enables CPUs to execute unmodified HIP code.


Example Usage
=============

To compile a HIP program, use the following command:

.. code-block:: shell

   clang++ -c --offload-arch=gfx906 -xhip sample.cpp -o sample.o

The ``-xhip`` option indicates that the source is a HIP program. If the file has a ``.hip`` extension,
Clang will automatically recognize it as a HIP program:

.. code-block:: shell

   clang++ -c --offload-arch=gfx906 sample.hip -o sample.o

To link a HIP program, use this command:

.. code-block:: shell

   clang++ --hip-link --offload-arch=gfx906 sample.o -o sample

In the above command, the ``--hip-link`` flag instructs Clang to link the HIP runtime library. However,
the use of this flag is unnecessary if a HIP input file is already present in your program.

For convenience, Clang also supports compiling and linking in a single step:

.. code-block:: shell

   clang++ --offload-arch=gfx906 -xhip sample.cpp -o sample

In the above commands, ``gfx906`` is the GPU architecture that the code is being compiled for. The supported GPU
architectures can be found in the `AMDGPU Processor Table <https://llvm.org/docs/AMDGPUUsage.html#processors>`_.
Alternatively, you can use the ``amdgpu-arch`` tool that comes with Clang to list the GPU architecture on your system:

.. code-block:: shell

   amdgpu-arch

You can use ``--offload-arch=native`` to automatically detect the GPU architectures on your system:

.. code-block:: shell

   clang++ --offload-arch=native -xhip sample.cpp -o sample


Path Setting for Dependencies
=============================

Compiling a HIP program depends on the HIP runtime and device library. The paths to the HIP runtime and device libraries
can be specified either using compiler options or environment variables. The paths can also be set through the ROCm path
if they follow the ROCm installation directory structure.

Order of Precedence for HIP Path
--------------------------------

1. ``--hip-path`` compiler option
2. ``HIP_PATH`` environment variable *(use with caution)*
3. ``--rocm-path`` compiler option
4. ``ROCM_PATH`` environment variable *(use with caution)*
5. Default automatic detection (relative to Clang or at the default ROCm installation location)

Order of Precedence for Device Library Path
-------------------------------------------

1. ``--hip-device-lib-path`` compiler option
2. ``HIP_DEVICE_LIB_PATH`` environment variable *(use with caution)*
3. ``--rocm-path`` compiler option
4. ``ROCM_PATH`` environment variable *(use with caution)*
5. Default automatic detection (relative to Clang or at the default ROCm installation location)

.. list-table::
   :header-rows: 1

   * - Compiler Option
     - Environment Variable
     - Description
     - Default Value
   * - ``--rocm-path=<path>``
     - ``ROCM_PATH``
     - Specifies the ROCm installation path.
     - Automatic detection
   * - ``--hip-path=<path>``
     - ``HIP_PATH``
     - Specifies the HIP runtime installation path.
     - Determined by ROCm directory structure
   * - ``--hip-device-lib-path=<path>``
     - ``HIP_DEVICE_LIB_PATH``
     - Specifies the HIP device library installation path.
     - Determined by ROCm directory structure

.. note::

   We recommend using the compiler options as the primary method for specifying these paths. While the environment variables ``ROCM_PATH``, ``HIP_PATH``, and ``HIP_DEVICE_LIB_PATH`` are supported, their use can lead to implicit dependencies that might cause issues in the long run. Use them with caution.


Predefined Macros
=================

.. list-table::
   :header-rows: 1

   * - Macro
     - Description
   * - ``__CLANG_RDC__``
     - Defined when Clang is compiling code in Relocatable Device Code (RDC) mode. RDC, enabled with the ``-fgpu-rdc`` compiler option, is necessary for linking device codes across translation units.
   * - ``__HIP__``
     - Defined when compiling with HIP language support, indicating that the code targets the HIP environment.
   * - ``__HIPCC__``
     - Alias to ``__HIP__``.
   * - ``__HIP_DEVICE_COMPILE__``
     - Defined during device code compilation in Clang's separate compilation process for the host and each offloading GPU architecture.
   * - ``__HIP_MEMORY_SCOPE_SINGLETHREAD``
     - Represents single-thread memory scope in HIP (value is 1).
   * - ``__HIP_MEMORY_SCOPE_WAVEFRONT``
     - Represents wavefront memory scope in HIP (value is 2).
   * - ``__HIP_MEMORY_SCOPE_WORKGROUP``
     - Represents workgroup memory scope in HIP (value is 3).
   * - ``__HIP_MEMORY_SCOPE_AGENT``
     - Represents agent memory scope in HIP (value is 4).
   * - ``__HIP_MEMORY_SCOPE_SYSTEM``
     - Represents system-wide memory scope in HIP (value is 5).
   * - ``__HIP_NO_IMAGE_SUPPORT__``
     - Defined with a value of 1 when the target device lacks support for HIP image functions.
   * - ``__HIP_NO_IMAGE_SUPPORT``
     - Alias to ``__HIP_NO_IMAGE_SUPPORT__``. Deprecated.
   * - ``__HIP_API_PER_THREAD_DEFAULT_STREAM__``
     - Defined when the GPU default stream is set to per-thread mode.
   * - ``HIP_API_PER_THREAD_DEFAULT_STREAM``
     - Alias to ``__HIP_API_PER_THREAD_DEFAULT_STREAM__``. Deprecated.

Note that some architecture specific AMDGPU macros will have default values when
used from the HIP host compilation. Other :doc:`AMDGPU macros <AMDGPUSupport>`
like ``__AMDGCN_WAVEFRONT_SIZE__`` will default to 64 for example.

Compilation Modes
=================

Each HIP source file contains intertwined device and host code. Depending on the chosen compilation mode by the compiler options ``-fno-gpu-rdc`` and ``-fgpu-rdc``, these portions of code are compiled differently.

Device Code Compilation
-----------------------

**``-fno-gpu-rdc`` Mode (default)**:

- Compiles to a self-contained, fully linked offloading device binary for each offloading device architecture.
- Device code within a Translation Unit (TU) cannot call functions located in another TU.

**``-fgpu-rdc`` Mode**:

- Compiles to a bitcode for each GPU architecture.
- For each offloading device architecture, the bitcode from different TUs are linked together to create a single offloading device binary.
- Device code in one TU can call functions located in another TU.

Host Code Compilation
---------------------

**Both Modes**:

- Compiles to a relocatable object for each TU.
- These relocatable objects are then linked together.
- Host code within a TU can call host functions and launch kernels from another TU.

Syntax Difference with CUDA
===========================

Clang's front end, used for both CUDA and HIP programming models, shares the same parsing and semantic analysis mechanisms. This includes the resolution of overloads concerning device and host functions. While there exists a comprehensive documentation on the syntax differences between Clang and NVCC for CUDA at `Dialect Differences Between Clang and NVCC <https://llvm.org/docs/CompileCudaWithLLVM.html#dialect-differences-between-clang-and-nvcc>`_, it is important to note that these differences also apply to HIP code compilation.

Predefined Macros for Differentiation
-------------------------------------

To facilitate differentiation between HIP and CUDA code, as well as between device and host compilations within HIP, Clang defines specific macros:

- ``__HIP__`` : This macro is defined only when compiling HIP code. It can be used to conditionally compile code specific to HIP, enabling developers to write portable code that can be compiled for both CUDA and HIP.

- ``__HIP_DEVICE_COMPILE__`` : Defined exclusively during HIP device compilation, this macro allows for conditional compilation of device-specific code. It provides a mechanism to segregate device and host code, ensuring that each can be optimized for their respective execution environments.

Function Pointers Support
=========================

Function pointers' support varies with the usage mode in Clang with HIP. The following table provides an overview of the support status across different use-cases and modes.

.. list-table:: Function Pointers Support Overview
   :widths: 25 25 25
   :header-rows: 1

   * - Use Case
     - ``-fno-gpu-rdc`` Mode (default)
     - ``-fgpu-rdc`` Mode
   * - Defined and used in the same TU
     - Supported
     - Supported
   * - Defined in one TU and used in another TU
     - Not Supported
     - Supported

In the ``-fno-gpu-rdc`` mode, the compiler calculates the resource usage of kernels based only on functions present within the same TU. This mode does not support the use of function pointers defined in a different TU due to the possibility of incorrect resource usage calculations, leading to undefined behavior.

On the other hand, the ``-fgpu-rdc`` mode allows the definition and use of function pointers across different TUs, as resource usage calculations can accommodate functions from disparate TUs.

Virtual Function Support
========================

In Clang with HIP, support for calling virtual functions of an object in device or host code is contingent on where the object is constructed.

- **Constructed in Device Code**: Virtual functions of an object can be called in device code on a specific offloading device if the object is constructed in device code on an offloading device with the same architecture.
- **Constructed in Host Code**: Virtual functions of an object can be called in host code if the object is constructed in host code.

In other scenarios, calling virtual functions is not allowed.

Explanation
-----------

An object constructed on the device side contains a pointer to the virtual function table on the device side, which is not accessible in host code, and vice versa. Thus, trying to invoke virtual functions from a context different from where the object was constructed will be disallowed because the appropriate virtual table cannot be accessed. The virtual function tables for offloading devices with different architecures are different, therefore trying to invoke virtual functions from an offloading device with a different architecture than where the object is constructed is also disallowed.

Example Usage
-------------

.. code-block:: c++

   class Base {
   public:
      __device__ virtual void virtualFunction() {
         // Base virtual function implementation
      }
   };

   class Derived : public Base {
   public:
      __device__ void virtualFunction() override {
         // Derived virtual function implementation
      }
   };

   __global__ void kernel() {
      Derived obj;
      Base* basePtr = &obj;
      basePtr->virtualFunction(); // Allowed since obj is constructed in device code
   }

SPIR-V Support on HIPAMD ToolChain
==================================

The HIPAMD ToolChain supports targetting
`AMDGCN Flavoured SPIR-V <https://llvm.org/docs/SPIRVUsage.html#target-triples>`_.
The support for SPIR-V in the ROCm and HIPAMD ToolChain is under active
development.

Compilation Process
-------------------

When compiling HIP programs with the intent of utilizing SPIR-V, the process
diverges from the traditional compilation flow:

Using ``--offload-arch=amdgcnspirv``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Target Triple**: The ``--offload-arch=amdgcnspirv`` flag instructs the
  compiler to use the target triple ``spirv64-amd-amdhsa``. This approach does
  generates generic AMDGCN SPIR-V which retains architecture specific elements
  without hardcoding them, thus allowing for optimal target specific code to be
  generated at run time, when the concrete target is known.

- **LLVM IR Translation**: The program is compiled to LLVM Intermediate
  Representation (IR), which is subsequently translated into SPIR-V. In the
  future, this translation step will be replaced by direct SPIR-V emission via
  the SPIR-V Back-end.

- **Clang Offload Bundler**: The resulting SPIR-V is embedded in the Clang
  offload bundler with the bundle ID ``hip-spirv64-amd-amdhsa--amdgcnspirv``.

Mixed with Normal ``--offload-arch``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Mixing ``amdgcnspirv`` and concrete ``gfx###`` targets via ``--offload-arch``
is not currently supported; this limitation is temporary and will be removed in
a future release**

Architecture Specific Macros
----------------------------

None of the architecture specific :doc:`AMDGPU macros <AMDGPUSupport>` are
defined when targeting SPIR-V. An alternative, more flexible mechanism to enable
doing per target / per feature code selection will be added in the future.
