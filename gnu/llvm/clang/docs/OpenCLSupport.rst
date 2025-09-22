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

==================
OpenCL Support
==================

Clang has complete support of OpenCL C versions from 1.0 to 3.0.
Support for OpenCL 3.0 is in experimental phase (:ref:`OpenCL 3.0 <opencl_300>`).

Clang also supports :ref:`the C++ for OpenCL kernel language <cxx_for_opencl_impl>`.

There are also other :ref:`new and experimental features <opencl_experimenal>`
available.

Details about usage of clang for OpenCL can be found in :doc:`UsersManual`.

Missing features or with limited support
========================================

- For general issues and bugs with OpenCL in clang refer to `the GitHub issue
  list
  <https://github.com/llvm/llvm-project/issues?q=is%3Aopen+is%3Aissue+label%3Aopencl>`__.

- Command-line flag :option:`-cl-ext` (used to override extensions/
  features supported by a target) is missing support of some functionality i.e. that is
  implemented fully through libraries (see :ref:`library-based features and
  extensions <opencl_ext_libs>`).

Internals Manual
================

This section acts as internal documentation for OpenCL features design
as well as some important implementation aspects. It is primarily targeted
at the advanced users and the toolchain developers integrating frontend
functionality as a component.

OpenCL Metadata
---------------

Clang uses metadata to provide additional OpenCL semantics in IR needed for
backends and OpenCL runtime.

Each kernel will have function metadata attached to it, specifying the arguments.
Kernel argument metadata is used to provide source level information for querying
at runtime, for example using the `clGetKernelArgInfo
<https://www.khronos.org/registry/OpenCL/specs/opencl-1.2.pdf#167>`_
call.

Note that ``-cl-kernel-arg-info`` enables more information about the original
kernel code to be added e.g. kernel parameter names will appear in the OpenCL
metadata along with other information.

The IDs used to encode the OpenCL's logical address spaces in the argument info
metadata follows the SPIR address space mapping as defined in the SPIR
specification `section 2.2
<https://www.khronos.org/registry/spir/specs/spir_spec-2.0.pdf#18>`_

OpenCL Specific Options
-----------------------

In addition to the options described in :doc:`UsersManual` there are the
following options specific to the OpenCL frontend.

All the options in this section are frontend-only and therefore if used
with regular clang driver they require frontend forwarding, e.g. ``-cc1``
or ``-Xclang``.

.. _opencl_finclude_default_header:

.. option:: -finclude-default-header

Adds most of builtin types and function declarations during compilations. By
default the OpenCL headers are not loaded by the frontend and therefore certain
builtin types and most of builtin functions are not declared. To load them
automatically this flag can be passed to the frontend (see also :ref:`the
section on the OpenCL Header <opencl_header>`):

   .. code-block:: console

     $ clang -Xclang -finclude-default-header test.cl

Alternatively the internal header `opencl-c.h` containing the declarations
can be included manually using ``-include`` or ``-I`` followed by the path
to the header location. The header can be found in the clang source tree or
installation directory.

   .. code-block:: console

     $ clang -I<path to clang sources>/lib/Headers/opencl-c.h test.cl
     $ clang -I<path to clang installation>/lib/clang/<llvm version>/include/opencl-c.h/opencl-c.h test.cl

In this example it is assumed that the kernel code contains
``#include <opencl-c.h>`` just as a regular C include.

Because the header is very large and long to parse, PCH (:doc:`PCHInternals`)
and modules (:doc:`Modules`) can be used internally to improve the compilation
speed.

To enable modules for OpenCL:

   .. code-block:: console

     $ clang --target=spir-unknown-unknown -c -emit-llvm -Xclang -finclude-default-header -fmodules -fimplicit-module-maps -fmodules-cache-path=<path to the generated module> test.cl

Another way to circumvent long parsing latency for the OpenCL builtin
declarations is to use mechanism enabled by :ref:`-fdeclare-opencl-builtins
<opencl_fdeclare_opencl_builtins>` flag that is available as an alternative
feature.

.. _opencl_fdeclare_opencl_builtins:

.. option:: -fdeclare-opencl-builtins

In addition to regular header includes with builtin types and functions using
:ref:`-finclude-default-header <opencl_finclude_default_header>`, clang
supports a fast mechanism to declare builtin functions with
``-fdeclare-opencl-builtins``. This does not declare the builtin types and
therefore it has to be used in combination with ``-finclude-default-header``
if full functionality is required.

**Example of Use**:

    .. code-block:: console

      $ clang -Xclang -fdeclare-opencl-builtins test.cl

.. _opencl_fake_address_space_map:

.. option:: -ffake-address-space-map

Overrides the target address space map with a fake map.
This allows adding explicit address space IDs to the bitcode for non-segmented
memory architectures that do not have separate IDs for each of the OpenCL
logical address spaces by default. Passing ``-ffake-address-space-map`` will
add/override address spaces of the target compiled for with the following values:
``1-global``, ``2-constant``, ``3-local``, ``4-generic``. The private address
space is represented by the absence of an address space attribute in the IR (see
also :ref:`the section on the address space attribute <opencl_addrsp>`).

   .. code-block:: console

     $ clang -cc1 -ffake-address-space-map test.cl

.. _opencl_builtins:

OpenCL builtins
---------------

**Clang builtins**

There are some standard OpenCL functions that are implemented as Clang builtins:

- All pipe functions from `section 6.13.16.2/6.13.16.3
  <https://www.khronos.org/registry/cl/specs/opencl-2.0-openclc.pdf#160>`_ of
  the OpenCL v2.0 kernel language specification.

- Address space qualifier conversion functions ``to_global``/``to_local``/``to_private``
  from `section 6.13.9
  <https://www.khronos.org/registry/cl/specs/opencl-2.0-openclc.pdf#101>`_.

- All the ``enqueue_kernel`` functions from `section 6.13.17.1
  <https://www.khronos.org/registry/cl/specs/opencl-2.0-openclc.pdf#164>`_ and
  enqueue query functions from `section 6.13.17.5
  <https://www.khronos.org/registry/cl/specs/opencl-2.0-openclc.pdf#171>`_.

**Fast builtin function declarations**

The implementation of the fast builtin function declarations (available via the
:ref:`-fdeclare-opencl-builtins option <opencl_fdeclare_opencl_builtins>`) consists
of the following main components:

- A TableGen definitions file ``OpenCLBuiltins.td``.  This contains a compact
  representation of the supported builtin functions.  When adding new builtin
  function declarations, this is normally the only file that needs modifying.

- A Clang TableGen emitter defined in ``ClangOpenCLBuiltinEmitter.cpp``.  During
  Clang build time, the emitter reads the TableGen definition file and
  generates ``OpenCLBuiltins.inc``.  This generated file contains various tables
  and functions that capture the builtin function data from the TableGen
  definitions in a compact manner.

- OpenCL specific code in ``SemaLookup.cpp``.  When ``Sema::LookupBuiltin``
  encounters a potential builtin function, it will check if the name corresponds
  to a valid OpenCL builtin function.  If so, all overloads of the function are
  inserted using ``InsertOCLBuiltinDeclarationsFromTable`` and overload
  resolution takes place.

OpenCL Extensions and Features
------------------------------

Clang implements various extensions to OpenCL kernel languages.

New functionality is accepted as soon as the documentation is detailed to the
level sufficient to be implemented. There should be an evidence that the
extension is designed with implementation feasibility in consideration and
assessment of complexity for C/C++ based compilers. Alternatively, the
documentation can be accepted in a format of a draft that can be further
refined during the implementation.

Implementation guidelines
^^^^^^^^^^^^^^^^^^^^^^^^^

This section explains how to extend clang with the new functionality.

**Parsing functionality**

If an extension modifies the standard parsing it needs to be added to
the clang frontend source code. This also means that the associated macro
indicating the presence of the extension should be added to clang.

The default flow for adding a new extension into the frontend is to
modify `OpenCLExtensions.def
<https://github.com/llvm/llvm-project/blob/main/clang/include/clang/Basic/OpenCLExtensions.def>`__,
containing the list of all extensions and optional features supported by
the frontend.

This will add the macro automatically and also add a field in the target
options ``clang::TargetOptions::OpenCLFeaturesMap`` to control the exposure
of the new extension during the compilation.

Note that by default targets like `SPIR-V`, `SPIR` or `X86` expose all the OpenCL
extensions. For all other targets the configuration has to be made explicitly.

Note that the target extension support performed by clang can be overridden
with :option:`-cl-ext` command-line flags.

.. _opencl_ext_libs:

**Library functionality**

If an extension adds functionality that does not modify standard language
parsing it should not require modifying anything other than header files and
``OpenCLBuiltins.td`` detailed in :ref:`OpenCL builtins <opencl_builtins>`.
Most commonly such extensions add functionality via libraries (by adding
non-native types or functions) parsed regularly. Similar to other languages this
is the most common way to add new functionality.

Clang has standard headers where new types and functions are being added,
for more details refer to
:ref:`the section on the OpenCL Header <opencl_header>`. The macros indicating
the presence of such extensions can be added in the standard header files
conditioned on target specific predefined macros or/and language version
predefined macros (see `feature/extension preprocessor macros defined in
opencl-c-base.h
<https://github.com/llvm/llvm-project/blob/main/clang/lib/Headers/opencl-c-base.h>`__).

**Pragmas**

Some extensions alter standard parsing dynamically via pragmas.

Clang provides a mechanism to add the standard extension pragma
``OPENCL EXTENSION`` by setting a dedicated flag in the extension list entry of
``OpenCLExtensions.def``. Note that there is no default behavior for the
standard extension pragmas as it is not specified (for the standards up to and
including version 3.0) in a sufficient level of detail and, therefore,
there is no default functionality provided by clang.

Pragmas without detailed information of their behavior (e.g. an explanation of
changes it triggers in the parsing) should not be added to clang. Moreover, the
pragmas should provide useful functionality to the user. For example, such
functionality should address a practical use case and not be redundant i.e.
cannot be achieved using existing features.

Note that some legacy extensions (published prior to OpenCL 3.0) still
provide some non-conformant functionality for pragmas e.g. add diagnostics on
the use of types or functions. This functionality is not guaranteed to remain in
future releases. However, any future changes should not affect backward
compatibility.

.. _opencl_addrsp:

Address spaces attribute
------------------------

Clang has arbitrary address space support using the ``address_space(N)``
attribute, where ``N`` is an integer number in the range specified in the
Clang source code. This addresses spaces can be used along with the OpenCL
address spaces however when such addresses spaces converted to/from OpenCL
address spaces the behavior is not governed by OpenCL specification.

An OpenCL implementation provides a list of standard address spaces using
keywords: ``private``, ``local``, ``global``, and ``generic``. In the AST and
in the IR each of the address spaces will be represented by unique number
provided in the Clang source code. The specific IDs for an address space do not
have to match between the AST and the IR. Typically in the AST address space
numbers represent logical segments while in the IR they represent physical
segments.
Therefore, machines with flat memory segments can map all AST address space
numbers to the same physical segment ID or skip address space attribute
completely while generating the IR. However, if the address space information
is needed by the IR passes e.g. to improve alias analysis, it is recommended
to keep it and only lower to reflect physical memory segments in the late
machine passes. The mapping between logical and target address spaces is
specified in the Clang's source code.

.. _cxx_for_opencl_impl:

C++ for OpenCL Implementation Status
====================================

Clang implements language versions 1.0 and 2021 published in `the official
release of C++ for OpenCL Documentation
<https://github.com/KhronosGroup/OpenCL-Docs/releases/tag/cxxforopencl-docrev2021.12>`_.

Limited support of experimental C++ libraries is described in the :ref:`experimental features <opencl_experimenal>`.

GitHub issues for this functionality are typically prefixed
with '[C++4OpenCL]' - click `here
<https://github.com/llvm/llvm-project/issues?q=is%3Aissue+is%3Aopen+%5BC%2B%2B4OpenCL%5D>`__
to view the full bug list.


Missing features or with limited support
----------------------------------------

- Support of C++ for OpenCL 2021 is currently in experimental phase. Refer to
  :ref:`OpenCL 3.0 status <opencl_300>` for details of common missing
  functionality from OpenCL 3.0.

- IR generation for non-trivial global destructors is incomplete (See:
  `PR48047 <https://llvm.org/PR48047>`_).

- Support of `destructors with non-default address spaces
  <https://www.khronos.org/opencl/assets/CXX_for_OpenCL.html#_construction_initialization_and_destruction>`_
  is incomplete (See: `D109609 <https://reviews.llvm.org/D109609>`_).

.. _opencl_300:

OpenCL C 3.0 Usage
==================

OpenCL C 3.0 language standard makes most OpenCL C 2.0 features optional. Optional
functionality in OpenCL C 3.0 is indicated with the presence of feature-test macros
(list of feature-test macros is `here <https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_C.html#features>`__).
Command-line flag :option:`-cl-ext` can be used to override features supported by a target.

For cases when there is an associated extension for a specific feature (fp64 and 3d image writes)
user should specify both (extension and feature) in command-line flag:

   .. code-block:: console

     $ clang -cl-std=CL3.0 -cl-ext=+cl_khr_fp64,+__opencl_c_fp64 ...
     $ clang -cl-std=CL3.0 -cl-ext=-cl_khr_fp64,-__opencl_c_fp64 ...



OpenCL C 3.0 Implementation Status
----------------------------------

The following table provides an overview of features in OpenCL C 3.0 and their
implementation status.

+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Category                     | Feature                                                           | Status               | Reviews                                                                                                                        |
+==============================+=========================+=========================================+======================+================================================================================================================================+
| Command line interface       | New value for ``-cl-std`` flag                                    | :good:`done`         | https://reviews.llvm.org/D88300                                                                                                |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Predefined macros            | New version macro                                                 | :good:`done`         | https://reviews.llvm.org/D88300                                                                                                |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Predefined macros            | Feature macros                                                    | :good:`done`         | https://reviews.llvm.org/D95776                                                                                                |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Generic address space                                             | :good:`done`         | https://reviews.llvm.org/D95778 and https://reviews.llvm.org/D103401                                                           |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Builtin function overloads with generic address space             | :good:`done`         | https://reviews.llvm.org/D105526, https://reviews.llvm.org/D107769                                                             |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Program scope variables in global memory                          | :good:`done`         | https://reviews.llvm.org/D103191                                                                                               |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | 3D image writes including builtin functions                       | :good:`done`         | https://reviews.llvm.org/D106260 (frontend)                                                                                    |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | read_write images including builtin functions                     | :good:`done`         | https://reviews.llvm.org/D104915 (frontend) and https://reviews.llvm.org/D107539, https://reviews.llvm.org/D117899 (functions) |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | C11 atomics memory scopes, ordering and builtin function          | :good:`done`         | https://reviews.llvm.org/D106111, https://reviews.llvm.org/D119420                                                             |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Blocks and Device-side kernel enqueue including builtin functions | :good:`done`         | https://reviews.llvm.org/D115640, https://reviews.llvm.org/D118605                                                             |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Pipes including builtin functions                                 | :good:`done`         | https://reviews.llvm.org/D107154 (frontend) and https://reviews.llvm.org/D105858 (functions)                                   |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Work group collective builtin functions                           | :good:`done`         | https://reviews.llvm.org/D105858                                                                                               |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Image types and builtin functions                                 | :good:`done`         | https://reviews.llvm.org/D103911 (frontend) and https://reviews.llvm.org/D107539 (functions)                                   |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| Feature optionality          | Double precision floating point type                              | :good:`done`         | https://reviews.llvm.org/D96524                                                                                                |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| New functionality            | RGBA vector components                                            | :good:`done`         | https://reviews.llvm.org/D99969                                                                                                |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| New functionality            | Subgroup functions                                                | :good:`done`         | https://reviews.llvm.org/D105858, https://reviews.llvm.org/D118999                                                             |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+
| New functionality            | Atomic mem scopes: subgroup, all devices including functions      | :good:`done`         | https://reviews.llvm.org/D103241                                                                                               |
+------------------------------+-------------------------+-----------------------------------------+----------------------+--------------------------------------------------------------------------------------------------------------------------------+

.. _opencl_experimenal:

Experimental features
=====================

Clang provides the following new WIP features for the developers to experiment
and provide early feedback or contribute with further improvements.
Feel free to contact us on `the Discourse forums (Clang Frontend category)
<https://discourse.llvm.org/c/clang/6>`_ or file `a GitHub issue
<https://github.com/llvm/llvm-project/issues/new>`_.

.. _opencl_experimental_cxxlibs:

C++ libraries for OpenCL
------------------------

There is ongoing work to support C++ standard libraries from `LLVM's libcxx
<https://libcxx.llvm.org/>`_ in OpenCL kernel code using C++ for OpenCL mode.

It is currently possible to include `type_traits` from C++17 in the kernel
sources when the following clang extensions are enabled
``__cl_clang_function_pointers`` and ``__cl_clang_variadic_functions``,
see :doc:`LanguageExtensions` for more details. The use of non-conformant
features enabled by the extensions does not expose non-conformant behavior
beyond the compilation i.e. does not get generated in IR or binary.
The extension only appear in metaprogramming
mechanism to identify or verify the properties of types. This allows to provide
the full C++ functionality without a loss of portability. To avoid unsafe use
of the extensions it is recommended that the extensions are disabled directly
after the header include.

**Example of Use**:

The example of kernel code with `type_traits` is illustrated here.

.. code-block:: c++

  #pragma OPENCL EXTENSION __cl_clang_function_pointers : enable
  #pragma OPENCL EXTENSION __cl_clang_variadic_functions : enable
  #include <type_traits>
  #pragma OPENCL EXTENSION __cl_clang_function_pointers : disable
  #pragma OPENCL EXTENSION __cl_clang_variadic_functions : disable

  using sint_type = std::make_signed<unsigned int>::type;

  __kernel void foo() {
    static_assert(!std::is_same<sint_type, unsigned int>::value);
  }

The possible clang invocation to compile the example is as follows:

   .. code-block:: console

     $ clang -I<path to libcxx checkout or installation>/include test.clcpp

Note that `type_traits` is a header only library and therefore no extra
linking step against the standard libraries is required. See full example
in `Compiler Explorer <https://godbolt.org/z/5WbnTfb65>`_.

More OpenCL specific C++ library implementations built on top of libcxx
are available in `libclcxx <https://github.com/KhronosGroup/libclcxx>`_
project.
