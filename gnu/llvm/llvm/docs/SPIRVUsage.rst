=============================
User Guide for SPIR-V Target
=============================

.. contents::
   :local:

.. toctree::
   :hidden:

Introduction
============

The SPIR-V target provides code generation for the SPIR-V binary format described
in  `the official SPIR-V specification <https://www.khronos.org/registry/SPIR-V/>`_.

Usage
=====

The SPIR-V backend can be invoked either from LLVM's Static Compiler (llc) or Clang,
allowing developers to compile LLVM intermediate language (IL) files or OpenCL kernel
sources directly to SPIR-V. This section outlines the usage of various commands to
leverage the SPIR-V backend for different purposes.

Static Compiler Commands
------------------------

1. **Basic SPIR-V Compilation**
   Command: `llc -mtriple=spirv32-unknown-unknown input.ll -o output.spvt`
   Description: This command compiles an LLVM IL file (`input.ll`) to a SPIR-V binary (`output.spvt`) for a 32-bit architecture.

2. **Compilation with Extensions and Optimization**
   Command: `llc -O1 -mtriple=spirv64-unknown-unknown --spirv-ext=+SPV_INTEL_arbitrary_precision_integers input.ll -o output.spvt`
   Description: Compiles an LLVM IL file to SPIR-V with (`-O1`) optimizations, targeting a 64-bit architecture. It enables the SPV_INTEL_arbitrary_precision_integers extension.

3. **SPIR-V Binary Generation**
   Command: `llc -O0 -mtriple=spirv64-unknown-unknown -filetype=obj input.ll -o output.spvt`
   Description: Generates a SPIR-V object file (`output.spvt`) from an LLVM module, targeting a 64-bit SPIR-V architecture with no optimizations.

Clang Commands
--------------

1. **SPIR-V Generation**
   Command: `clang –target=spirv64 input.cl`
   Description: Generates a SPIR-V file directly from an OpenCL kernel source file (`input.cl`).

Compiler Options
================

.. _spirv-target-triples:

Target Triples
--------------

For cross-compilation into SPIR-V use option

``-target <Architecture><Subarchitecture>-<Vendor>-<OS>-<Environment>``

to specify the target triple:

  .. table:: SPIR-V Architectures

     ============ ==============================================================
     Architecture Description
     ============ ==============================================================
     ``spirv32``   SPIR-V with 32-bit pointer width.
     ``spirv64``   SPIR-V with 64-bit pointer width.
     ``spirv``     SPIR-V with logical memory layout.
     ============ ==============================================================

  .. table:: SPIR-V Subarchitectures

     =============== ==============================================================
     Subarchitecture Description
     =============== ==============================================================
     *<empty>*        SPIR-V version deduced by backend based on the input.
     ``v1.0``         SPIR-V version 1.0.
     ``v1.1``         SPIR-V version 1.1.
     ``v1.2``         SPIR-V version 1.2.
     ``v1.3``         SPIR-V version 1.3.
     ``v1.4``         SPIR-V version 1.4.
     ``v1.5``         SPIR-V version 1.5.
     ``v1.6``         SPIR-V version 1.6.
     =============== ==============================================================

  .. table:: SPIR-V Vendors

     ===================== ==============================================================
     Vendor                Description
     ===================== ==============================================================
     *<empty>*/``unknown``  Generic SPIR-V target without any vendor-specific settings.
     ``amd``                AMDGCN SPIR-V target, with support for target specific
                            builtins and ASM, meant to be consumed by AMDGCN toolchains.
     ===================== ==============================================================

  .. table:: Operating Systems

     ===================== ==============================================================
     OS                    Description
     ===================== ==============================================================
     *<empty>*/``unknown``  Defaults to the OpenCL runtime.
     ``vulkan``             Vulkan shader runtime.
     ``vulkan1.2``          Vulkan 1.2 runtime, corresponding to SPIR-V 1.5.
     ``vulkan1.3``          Vulkan 1.3 runtime, corresponding to SPIR-V 1.6.
     ``amdhsa``             AMDHSA runtime, meant to be used on HSA compatible runtimes,
                            corresponding to SPIR-V 1.6.
     ===================== ==============================================================

  .. table:: SPIR-V Environments

     ===================== ==============================================================
     Environment           Description
     ===================== ==============================================================
     *<empty>*/``unknown``  OpenCL environment or deduced by backend based on the input.
     ===================== ==============================================================

Example:

``-target spirv64v1.0`` can be used to compile for SPIR-V version 1.0 with 64-bit pointer width.

``-target spirv64-amd-amdhsa`` can be used to compile for AMDGCN flavoured SPIR-V with 64-bit pointer width.

.. _spirv-extensions:

Extensions
----------

The SPIR-V backend supports a variety of `extensions <https://github.com/KhronosGroup/SPIRV-Registry/tree/main/extensions>`_
that enable or enhance features beyond the core SPIR-V specification.
These extensions can be enabled using the ``-spirv-extensions`` option
followed by the name of the extension(s) you wish to enable. Below is a
list of supported SPIR-V extensions, sorted alphabetically by their extension names:

.. list-table:: Supported SPIR-V Extensions
   :widths: 50 150
   :header-rows: 1

   * - Extension Name
     - Description
   * - ``SPV_EXT_shader_atomic_float16_add``
     - Extends the SPV_EXT_shader_atomic_float_add extension to support atomically adding to 16-bit floating-point numbers in memory.
   * - ``SPV_EXT_shader_atomic_float_add``
     - Adds atomic add instruction on floating-point numbers.
   * - ``SPV_EXT_shader_atomic_float_min_max``
     - Adds atomic min and max instruction on floating-point numbers.
   * - ``SPV_INTEL_arbitrary_precision_integers``
     - Allows generating arbitrary width integer types.
   * - ``SPV_INTEL_bfloat16_conversion``
     - Adds instructions to convert between single-precision 32-bit floating-point values and 16-bit bfloat16 values.
   * - ``SPV_INTEL_cache_controls``
     - Allows cache control information to be applied to memory access instructions.
   * - ``SPV_INTEL_function_pointers``
     - Allows translation of function pointers.
   * - ``SPV_INTEL_inline_assembly``
     - Allows to use inline assembly.
   * - ``SPV_INTEL_global_variable_host_access``
     - Adds decorations that can be applied to global (module scope) variables.
   * - ``SPV_INTEL_global_variable_fpga_decorations``
     - Adds decorations that can be applied to global (module scope) variables to help code generation for FPGA devices.
   * - ``SPV_INTEL_optnone``
     - Adds OptNoneINTEL value for Function Control mask that indicates a request to not optimize the function.
   * - ``SPV_INTEL_subgroups``
     - Allows work items in a subgroup to share data without the use of local memory and work group barriers, and to utilize specialized hardware to load and store blocks of data from images or buffers.
   * - ``SPV_INTEL_usm_storage_classes``
     - Introduces two new storage classes that are subclasses of the CrossWorkgroup storage class that provides additional information that can enable optimization.
   * - ``SPV_INTEL_variable_length_array``
     - Allows to allocate local arrays whose number of elements is unknown at compile time.
   * - ``SPV_KHR_bit_instructions``
     - Enables bit instructions to be used by SPIR-V modules without requiring the Shader capability.
   * - ``SPV_KHR_expect_assume``
     - Provides additional information to a compiler, similar to the llvm.assume and llvm.expect intrinsics.
   * - ``SPV_KHR_float_controls``
     - Provides new execution modes to control floating-point computations by overriding an implementation’s default behavior for rounding modes, denormals, signed zero, and infinities.
   * - ``SPV_KHR_linkonce_odr``
     - Allows to use the LinkOnceODR linkage type that lets a function or global variable to be merged with other functions or global variables of the same name when linkage occurs.
   * - ``SPV_KHR_no_integer_wrap_decoration``
     - Adds decorations to indicate that a given instruction does not cause integer wrapping.
   * - ``SPV_KHR_shader_clock``
     - Adds the extension cl_khr_kernel_clock that adds the ability for a kernel to sample the value from clocks provided by compute units.
   * - ``SPV_KHR_subgroup_rotate``
     - Adds a new instruction that enables rotating values across invocations within a subgroup.
   * - ``SPV_KHR_uniform_group_instructions``
     - Allows support for additional group operations within uniform control flow.

To enable multiple extensions, list them separated by spaces. For example, to enable support for atomic operations on floating-point numbers and arbitrary precision integers, use:

``-spirv-ext=+SPV_EXT_shader_atomic_float_add,+SPV_INTEL_arbitrary_precision_integers``

To enable all extensions, use the following option:
``-spirv-ext=all``

To enable all extensions except specified, specify ``all`` followed by a list of disallowed extensions. For example:
``-spirv-ext=all,-SPV_INTEL_arbitrary_precision_integers``

SPIR-V representation in LLVM IR
================================

SPIR-V is intentionally designed for seamless integration with various Intermediate
Representations (IRs), including LLVM IR, facilitating straightforward mappings for
most of its entities. The development of the SPIR-V backend has been guided by a
principle of compatibility with the `Khronos Group SPIR-V LLVM Translator <https://github.com/KhronosGroup/SPIRV-LLVM-Translator>`_.
Consequently, the input representation accepted by the SPIR-V backend aligns closely
with that detailed in `the SPIR-V Representation in LLVM document <https://github.com/KhronosGroup/SPIRV-LLVM-Translator/blob/main/docs/SPIRVRepresentationInLLVM.rst>`_.
This document, along with the sections that follow, delineate the main points and focus
on any differences between the LLVM IR that this backend processes and the conventions
used by other tools.

.. _spirv-special-types:

Special types
-------------

SPIR-V specifies several kinds of opaque types. These types are represented
using target extension types and are represented as follows:

  .. table:: SPIR-V Opaque Types

     ================== ====================== =========================================================================================
     SPIR-V Type        LLVM type name         LLVM type arguments
     ================== ====================== =========================================================================================
     OpTypeImage        ``spirv.Image``        sampled type, dimensionality, depth, arrayed, MS, sampled, image format, access qualifier
     OpTypeSampler      ``spirv.Sampler``      (none)
     OpTypeSampledImage ``spirv.SampledImage`` sampled type, dimensionality, depth, arrayed, MS, sampled, image format, access qualifier
     OpTypeEvent        ``spirv.Event``        (none)
     OpTypeDeviceEvent  ``spirv.DeviceEvent``  (none)
     OpTypeReserveId    ``spirv.ReserveId``    (none)
     OpTypeQueue        ``spirv.Queue``        (none)
     OpTypePipe         ``spirv.Pipe``         access qualifier
     OpTypePipeStorage  ``spirv.PipeStorage``  (none)
     ================== ====================== =========================================================================================

All integer arguments take the same value as they do in their `corresponding
SPIR-V instruction <https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_type_declaration_instructions>`_.
For example, the OpenCL type ``image2d_depth_ro_t`` would be represented in
SPIR-V IR as ``target("spirv.Image", void, 1, 1, 0, 0, 0, 0, 0)``, with its
dimensionality parameter as ``1`` meaning 2D. Sampled image types include the
parameters of its underlying image type, so that a sampled image for the
previous type has the representation
``target("spirv.SampledImage, void, 1, 1, 0, 0, 0, 0, 0)``.

.. _spirv-intrinsics:

Target Intrinsics
-----------------

The SPIR-V backend employs several LLVM IR intrinsics that facilitate various low-level
operations essential for generating correct and efficient SPIR-V code. These intrinsics
cover a range of functionalities from type assignment and memory management to control
flow and atomic operations. Below is a detailed table of selected intrinsics used in the
SPIR-V backend, along with their descriptions and argument details.

.. list-table:: LLVM IR Intrinsics for SPIR-V
   :widths: 25 15 20 40
   :header-rows: 1

   * - Intrinsic ID
     - Return Type
     - Argument Types
     - Description
   * - `int_spv_assign_type`
     - None
     - `[Type, Metadata]`
     - Associates a type with metadata, crucial for maintaining type information in SPIR-V structures. Not emitted directly but supports the type system internally.
   * - `int_spv_assign_ptr_type`
     - None
     - `[Type, Metadata, Integer]`
     - Similar to `int_spv_assign_type`, but for pointer types with an additional integer specifying the storage class. Supports SPIR-V's detailed pointer type system. Not emitted directly.
   * - `int_spv_assign_name`
     - None
     - `[Type, Vararg]`
     - Assigns names to types or values, enhancing readability and debuggability of SPIR-V code. Not emitted directly but used for metadata enrichment.
   * - `int_spv_assign_decoration`
     - None
     - `[Type, Metadata]`
     - Assigns decoration to values by associating them with metadatas. Not emitted directly but used to support SPIR-V representation in LLVM IR.
   * - `int_spv_track_constant`
     - Type
     - `[Type, Metadata]`
     - Tracks constants in the SPIR-V module. Essential for optimizing and reducing redundancy. Emitted for internal use only.
   * - `int_spv_init_global`
     - None
     - `[Type, Type]`
     - Initializes global variables, a necessary step for ensuring correct global state management in SPIR-V. Emitted for internal use only.
   * - `int_spv_unref_global`
     - None
     - `[Type]`
     - Manages the lifetime of global variables by marking them as unreferenced, thus enabling optimizations related to global variable usage. Emitted for internal use only.
   * - `int_spv_gep`
     - Pointer
     - `[Boolean, Type, Vararg]`
     - Computes the address of a sub-element of an aggregate type. Critical for accessing array elements and structure fields. Supports conditionally addressing elements in a generic way.
   * - `int_spv_load`
     - 32-bit Integer
     - `[Pointer, 16-bit Integer, 8-bit Integer]`
     - Loads a value from a memory location. The additional integers specify memory access and alignment details, vital for ensuring correct and efficient memory operations.
   * - `int_spv_store`
     - None
     - `[Type, Pointer, 16-bit Integer, 8-bit Integer]`
     - Stores a value to a memory location. Like `int_spv_load`, it includes specifications for memory access and alignment, essential for memory operations.
   * - `int_spv_extractv`
     - Type
     - `[32-bit Integer, Vararg]`
     - Extracts a value from a vector, allowing for vector operations within SPIR-V. Enables manipulation of vector components.
   * - `int_spv_insertv`
     - 32-bit Integer
     - `[32-bit Integer, Type, Vararg]`
     - Inserts a value into a vector. Complementary to `int_spv_extractv`, it facilitates the construction and manipulation of vectors.
   * - `int_spv_extractelt`
     - Type
     - `[Type, Any Integer]`
     - Extracts an element from an aggregate type based on an index. Essential for operations on arrays and vectors.
   * - `int_spv_insertelt`
     - Type
     - `[Type, Type, Any Integer]`
     - Inserts an element into an aggregate type at a specified index. Allows for building and modifying arrays and vectors.
   * - `int_spv_const_composite`
     - Type
     - `[Vararg]`
     - Constructs a composite type from given elements. Key for creating arrays, structs, and vectors from individual components.
   * - `int_spv_bitcast`
     - Type
     - `[Type]`
     - Performs a bit-wise cast between types. Critical for type conversions that do not change the bit representation.
   * - `int_spv_ptrcast`
     - Type
     - `[Type, Metadata, Integer]`
     - Casts pointers between different types. Similar to `int_spv_bitcast` but specifically for pointers, taking into account SPIR-V's strict type system.
   * - `int_spv_switch`
     - None
     - `[Type, Vararg]`
     - Implements a multi-way branch based on a value. Enables complex control flow structures, similar to the switch statement in high-level languages.
   * - `int_spv_cmpxchg`
     - 32-bit Integer
     - `[Type, Vararg]`
     - Performs an atomic compare-and-exchange operation. Crucial for synchronization and concurrency control in compute shaders.
   * - `int_spv_unreachable`
     - None
     - `[]`
     - Marks a point in the code that should never be reached, enabling optimizations by indicating unreachable code paths.
   * - `int_spv_alloca`
     - Type
     - `[]`
     - Allocates memory on the stack. Fundamental for local variable storage in functions.
   * - `int_spv_alloca_array`
     - Type
     - `[Any Integer]`
     - Allocates an array on the stack. Extends `int_spv_alloca` to support array allocations, essential for temporary arrays.
   * - `int_spv_undef`
     - 32-bit Integer
     - `[]`
     - Generates an undefined value. Useful for optimizations and indicating uninitialized variables.
   * - `int_spv_inline_asm`
     - None
     - `[Metadata, Metadata, Vararg]`
     - Associates inline assembly features to inline assembly call instances by creating metadatas and preserving original arguments. Not emitted directly but used to support SPIR-V representation in LLVM IR.
   * - `int_spv_assume`
     - None
     - `[1-bit Integer]`
     - Provides hints to the optimizer about assumptions that can be made about program state. Improves optimization potential.
   * - `int_spv_expect`
     - Any Integer Type
     - `[Type, Type]`
     - Guides branch prediction by indicating expected branch paths. Enhances performance by optimizing common code paths.
   * - `int_spv_thread_id`
     - 32-bit Integer
     - `[32-bit Integer]`
     - Retrieves the thread ID within a workgroup. Essential for identifying execution context in parallel compute operations.
   * - `int_spv_create_handle`
     - Pointer
     - `[8-bit Integer]`
     - Creates a resource handle for graphics or compute resources. Facilitates the management and use of resources in shaders.

.. _spirv-builtin-functions:

Builtin Functions
-----------------

The following section highlights the representation of SPIR-V builtins in LLVM IR,
emphasizing builtins that do not have direct counterparts in LLVM.

Instructions as Function Calls
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

SPIR-V builtins without direct LLVM counterparts are represented as LLVM function calls.
These functions, termed SPIR-V builtin functions, follow an IA64 mangling scheme with
SPIR-V-specific extensions. Parsing non-mangled calls to builtins is supported in some cases,
but not tested extensively. The general format is:

.. code-block:: c

  __spirv_{OpCodeName}{_OptionalPostfixes}

Where `{OpCodeName}` is the SPIR-V opcode name sans the "Op" prefix, and
`{OptionalPostfixes}` are decoration-specific postfixes, if any. The mangling and
postfixes allow for the representation of SPIR-V's rich instruction set within LLVM's
framework.

Extended Instruction Sets
~~~~~~~~~~~~~~~~~~~~~~~~~

SPIR-V defines several extended instruction sets for additional functionalities, such as
OpenCL-specific operations. In LLVM IR, these are represented by function calls to
mangled builtins and selected based on the environment. For example:

.. code-block:: c

  acos_f32

represents the `acos` function from the OpenCL extended instruction set for a float32
input.

Builtin Variables
~~~~~~~~~~~~~~~~~

SPIR-V builtin variables, which provide access to special hardware or execution model
properties, are mapped to either LLVM function calls or LLVM global variables. The
representation follows the naming convention:

.. code-block:: c

  __spirv_BuiltIn{VariableName}

For instance, the SPIR-V builtin `GlobalInvocationId` is accessible in LLVM IR as
`__spirv_BuiltInGlobalInvocationId`.

Vector Load and Store Builtins
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

SPIR-V's capabilities for loading and storing vectors are represented in LLVM IR using
functions that mimic the SPIR-V instructions. These builtins handle cases that LLVM's
native instructions do not directly support, enabling fine-grained control over memory
operations.

Atomic Operations
~~~~~~~~~~~~~~~~~

SPIR-V's atomic operations, especially those operating on floating-point data, are
represented in LLVM IR with corresponding function calls. These builtins ensure
atomicity in operations where LLVM might not have direct support, essential for parallel
execution and synchronization.

Image Operations
~~~~~~~~~~~~~~~~

SPIR-V provides extensive support for image and sampler operations, which LLVM
represents through function calls to builtins. These include image reads, writes, and
queries, allowing detailed manipulation of image data and parameters.

Group and Subgroup Operations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For workgroup and subgroup operations, LLVM uses function calls to represent SPIR-V's
group-based instructions. These builtins facilitate group synchronization, data sharing,
and collective operations essential for efficient parallel computation.
