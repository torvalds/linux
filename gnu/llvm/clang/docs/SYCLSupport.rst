=============================================
SYCL Compiler and Runtime architecture design
=============================================

.. contents::
   :local:

Introduction
============

This document describes the architecture of the SYCL compiler and runtime
library. More details are provided in
`external document <https://github.com/intel/llvm/blob/sycl/sycl/doc/design/CompilerAndRuntimeDesign.md>`_\ ,
which are going to be added to clang documentation in the future.

Address space handling
======================

The SYCL specification represents pointers to disjoint memory regions using C++
wrapper classes on an accelerator to enable compilation with a standard C++
toolchain and a SYCL compiler toolchain. Section 3.8.2 of SYCL 2020
specification defines
`memory model <https://www.khronos.org/registry/SYCL/specs/sycl-2020/html/sycl-2020.html#_sycl_device_memory_model>`_\ ,
section 4.7.7 - `address space classes <https://www.khronos.org/registry/SYCL/specs/sycl-2020/html/sycl-2020.html#_address_space_classes>`_
and section 5.9 covers `address space deduction <https://www.khronos.org/registry/SYCL/specs/sycl-2020/html/sycl-2020.html#_address_space_deduction>`_.
The SYCL specification allows two modes of address space deduction: "generic as
default address space" (see section 5.9.3) and "inferred address space" (see
section 5.9.4). Current implementation supports only "generic as default address
space" mode.

SYCL borrows its memory model from OpenCL however SYCL doesn't perform
the address space qualifier inference as detailed in
`OpenCL C v3.0 6.7.8 <https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_C.html#addr-spaces-inference>`_.

The default address space is "generic-memory", which is a virtual address space
that overlaps the global, local, and private address spaces. SYCL mode enables
following conversions:

- explicit conversions to/from the default address space from/to the address
  space-attributed type
- implicit conversions from the address space-attributed type to the default
  address space
- explicit conversions to/from the global address space from/to the
  ``__attribute__((opencl_global_device))`` or
  ``__attribute__((opencl_global_host))`` address space-attributed type
- implicit conversions from the ``__attribute__((opencl_global_device))`` or
  ``__attribute__((opencl_global_host))`` address space-attributed type to the
  global address space

All named address spaces are disjoint and sub-sets of default address space.

The SPIR target allocates SYCL namespace scope variables in the global address
space.

Pointers to default address space should get lowered into a pointer to a generic
address space (or flat to reuse more general terminology). But depending on the
allocation context, the default address space of a non-pointer type is assigned
to a specific address space. This is described in
`common address space deduction rules <https://www.khronos.org/registry/SYCL/specs/sycl-2020/html/sycl-2020.html#subsec:commonAddressSpace>`_
section.

This is also in line with the behaviour of CUDA (`small example
<https://godbolt.org/z/veqTfo9PK>`_).

``multi_ptr`` class implementation example:

.. code-block:: C++

   // check that SYCL mode is ON and we can use non-standard decorations
   #if defined(__SYCL_DEVICE_ONLY__)
   // GPU/accelerator implementation
   template <typename T, address_space AS> class multi_ptr {
     // DecoratedType applies corresponding address space attribute to the type T
     // DecoratedType<T, global_space>::type == "__attribute__((opencl_global)) T"
     // See sycl/include/CL/sycl/access/access.hpp for more details
     using pointer_t = typename DecoratedType<T, AS>::type *;

     pointer_t m_Pointer;
     public:
     pointer_t get() { return m_Pointer; }
     T& operator* () { return *reinterpret_cast<T*>(m_Pointer); }
   }
   #else
   // CPU/host implementation
   template <typename T, address_space AS> class multi_ptr {
     T *m_Pointer; // regular undecorated pointer
     public:
     T *get() { return m_Pointer; }
     T& operator* () { return *m_Pointer; }
   }
   #endif

Depending on the compiler mode, ``multi_ptr`` will either decorate its internal
data with the address space attribute or not.

To utilize clang's existing functionality, we reuse the following OpenCL address
space attributes for pointers:

.. list-table::
   :header-rows: 1

   * - Address space attribute
     - SYCL address_space enumeration
   * - ``__attribute__((opencl_global))``
     - global_space, constant_space
   * - ``__attribute__((opencl_global_device))``
     - global_space
   * - ``__attribute__((opencl_global_host))``
     - global_space
   * - ``__attribute__((opencl_local))``
     - local_space
   * - ``__attribute__((opencl_private))``
     - private_space


.. code-block:: C++

    //TODO: add support for __attribute__((opencl_global_host)) and __attribute__((opencl_global_device)).

