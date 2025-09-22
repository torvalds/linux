============
HLSL Support
============

.. contents::
   :local:

Introduction
============

HLSL Support is under active development in the Clang codebase. This document
describes the high level goals of the project, the guiding principles, as well
as some idiosyncrasies of the HLSL language and how we intend to support them in
Clang.

Project Goals
=============

The long term goal of this project is to enable Clang to function as a
replacement for the `DirectXShaderCompiler (DXC)
<https://github.com/microsoft/DirectXShaderCompiler/>`_ in all its supported
use cases. Accomplishing that goal will require Clang to be able to process most
existing HLSL programs with a high degree of source compatibility.

Non-Goals
---------

HLSL ASTs do not need to be compatible between DXC and Clang. We do not expect
identical code generation or that features will resemble DXC's implementation or
architecture. In fact, we explicitly expect to deviate from DXC's implementation
in key ways.

Guiding Principles
==================

This document lacks details for architectural decisions that are not yet
finalized. Our top priorities are quality, maintainability, and flexibility. In
accordance with community standards we are expecting a high level of test
coverage, and we will engineer our solutions with long term maintenance in mind.
We are also working to limit modifications to the Clang C++ code paths and
share as much functionality as possible.

Architectural Direction
=======================

HLSL support in Clang is expressed as C++ minus unsupported C and C++ features.
This is different from how other Clang languages are implemented. Most languages
in Clang are additive on top of C.

HLSL is not a formally or fully specified language, and while our goals require
a high level of source compatibility, implementations can vary and we have some
flexibility to be more or less permissive in some cases. For modern HLSL DXC is
the reference implementation.

The HLSL effort prioritizes following similar patterns for other languages,
drivers, runtimes and targets. Specifically, We will maintain separation between
HSLS-specific code and the rest of Clang as much as possible following patterns
in use in Clang code today (i.e. ParseHLSL.cpp, SemaHLSL.cpp, CGHLSL*.cpp...).
We will use inline checks on language options where the code is simple and
isolated, and prefer HLSL-specific implementation files for any code of
reasonable complexity.

In places where the HLSL language is in conflict with C and C++, we will seek to
make minimally invasive changes guarded under the HLSL language options. We will
seek to make HLSL language support as minimal a maintenance burden as possible.

DXC Driver
----------

A DXC driver mode will provide command-line compatibility with DXC, supporting
DXC's options and flags. The DXC driver is HLSL-specific and will create an
HLSLToolchain which will provide the basis to support targeting both DirectX and
Vulkan.

Parser
------

Following the examples of other parser extensions HLSL will add a ParseHLSL.cpp
file to contain the implementations of HLSL-specific extensions to the Clang
parser. The HLSL grammar shares most of its structure with C and C++, so we will
use the existing C/C++ parsing code paths.

Sema
----

HLSL's Sema implementation will also provide an ``ExternalSemaSource``. In DXC,
an ``ExternalSemaSource`` is used to provide definitions for HLSL built-in data
types and built-in templates. Clang is already designed to allow an attached
``ExternalSemaSource`` to lazily complete data types, which is a **huge**
performance win for HLSL.

If precompiled headers are used when compiling HLSL, the ``ExternalSemaSource``
will be a ``MultiplexExternalSemaSource`` which includes both the ``ASTReader``
and -. For Built-in declarations that are already
completed in the serialized AST, the ``HLSLExternalSemaSource`` will reuse the
existing declarations and not introduce new declarations. If the built-in types
are not completed in the serialized AST, the ``HLSLExternalSemaSource`` will
create new declarations and connect the de-serialized decls as the previous
declaration.

CodeGen
-------

Like OpenCL, HLSL relies on capturing a lot of information into IR metadata.
*hand wave* *hand wave* *hand wave* As a design principle here we want our IR to
be idiomatic Clang IR as much as possible. We will use IR attributes wherever we
can, and use metadata as sparingly as possible. One example of a difference from
DXC already implemented in Clang is the use of target triples to communicate
shader model versions and shader stages.

Our HLSL CodeGen implementation should also have an eye toward generating IR
that will map directly to targets other than DXIL. While IR itself is generally
not re-targetable, we want to share the Clang CodeGen implementation for HLSL
with other GPU graphics targets like SPIR-V and possibly other GPU and even CPU
targets.

hlsl.h
------

HLSL has a library of standalone functions. This is similar to OpenCL and CUDA,
and is analogous to C's standard library. The implementation approach for the
HLSL library functionality draws from patterns in use by OpenCL and other Clang
resource headers. All of the clang resource headers are part of the
``ClangHeaders`` component found in the source tree under
`clang/lib/Headers <https://github.com/llvm/llvm-project/tree/main/clang/lib/Headers>`_.

.. note::

   HLSL's complex data types are not defined in HLSL's header because many of
   the semantics of those data types cannot be expressed in HLSL due to missing
   language features. Data types that can't be expressed in HLSL are defined in
   code in the ``HLSLExternalSemaSource``.

Similar to OpenCL, the HLSL library functionality is implicitly declared in
translation units without needing to include a header to provide declarations.
In Clang this is handled by making ``hlsl.h`` an implicitly included header
distributed as part of the Clang resource directory.

Similar to OpenCL, HLSL's implicit header will explicitly declare all overloads,
and each overload will map to a corresponding ``__builtin*`` compiler intrinsic
that is handled in ClangCodeGen. CUDA uses a similar pattern although many CUDA
functions have full definitions in the included headers which in turn call
corresponding ``__builtin*`` compiler intrinsics. By not having bodies HLSL
avoids the need for the inliner to clean up and inline large numbers of small
library functions.

HLSL's implicit headers also define some of HLSL's typedefs. This is consistent
with how the AVX vector header is implemented.

Concerns have been expressed that this approach may result in slower compile
times than the approach DXC uses where library functions are treated more like
Clang ``__builtin*`` intrinsics. No real world use cases have been identified
where parsing is a significant compile-time overhead, but the HLSL implicit
headers can be compiled into a module for performance if needed.

Further, by treating these as functions rather than ``__builtin*`` compiler
intrinsics, the language behaviors are more consistent and aligned with user
expectation because normal overload resolution rules and implicit conversions
apply as expected.

It is a feature of this design that clangd-powered "go to declaration" for
library functions will jump to a valid header declaration and all overloads will
be user readable.

HLSL Language
=============

The HLSL language is insufficiently documented, and not formally specified.
Documentation is available on `Microsoft's website
<https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl>`_.
The language syntax is similar enough to C and C++ that carefully written C and
C++ code is valid HLSL. HLSL has some key differences from C & C++ which we will
need to handle in Clang.

HLSL is not a conforming or valid extension or superset of C or C++. The
language has key incompatibilities with C and C++, both syntactically and
semantically.

An Aside on GPU Languages
-------------------------

Due to HLSL being a GPU targeted language HLSL is a Single Program Multiple Data
(SPMD) language relying on the implicit parallelism provided by GPU hardware.
Some language features in HLSL enable programmers to take advantage of the
parallel nature of GPUs in a hardware abstracted language.

HLSL also prohibits some features of C and C++ which can have catastrophic
performance or are not widely supportable on GPU hardware or drivers. As an
example, register spilling is often excessively expensive on GPUs, so HLSL
requires all functions to be inlined during code generation, and does not
support a runtime calling convention.

Pointers & References
---------------------

HLSL does not support referring to values by address. Semantically all variables
are value-types and behave as such. HLSL disallows the pointer dereference
operators (unary ``*``, and ``->``), as well as the address of operator (unary
&). While HLSL disallows pointers and references in the syntax, HLSL does use
reference types in the AST, and we intend to use pointer decay in the AST in
the Clang implementation.

HLSL ``this`` Keyword
---------------------

HLSL does support member functions, and (in HLSL 2021) limited operator
overloading. With member function support, HLSL also has a ``this`` keyword. The
``this`` keyword is an example of one of the places where HLSL relies on
references in the AST, because ``this`` is a reference.

Bitshifts
---------

In deviation from C, HLSL bitshifts are defined to mask the shift count by the
size of the type. In DXC, the semantics of LLVM IR were altered to accommodate
this, in Clang we intend to generate the mask explicitly in the IR. In cases
where the shift value is constant, this will be constant folded appropriately,
in other cases we can clean it up in the DXIL target.

Non-short Circuiting Logical Operators
--------------------------------------

In HLSL 2018 and earlier, HLSL supported logical operators (and the ternary
operator) on vector types. This behavior required that operators not short
circuit. The non-short circuiting behavior applies to all data types until HLSL
2021. In HLSL 2021, logical and ternary operators do not support vector types
instead builtin functions ``and``, ``or`` and ``select`` are available, and
operators short circuit matching C behavior.

Precise Qualifier
-----------------

HLSL has a ``precise`` qualifier that behaves unlike anything else in the C
language. The support for this qualifier in DXC is buggy, so our bar for
compatibility is low.

The ``precise`` qualifier applies in the inverse direction from normal
qualifiers. Rather than signifying that the declaration containing ``precise``
qualifier be precise, it signifies that the operations contributing to the
declaration's value be ``precise``. Additionally, ``precise`` is a misnomer:
values attributed as ``precise`` comply with IEEE-754 floating point semantics,
and are prevented from optimizations which could decrease *or increase*
precision.

Differences in Templates
------------------------

HLSL uses templates to define builtin types and methods, but disallowed
user-defined templates until HLSL 2021. HLSL also allows omitting empty template
parameter lists when all template parameters are defaulted. This is an ambiguous
syntax in C++, but Clang detects the case and issues a diagnostic. This makes
supporting the case in Clang minimally invasive.

Vector Extensions
-----------------

HLSL uses the OpenCL vector extensions, and also provides C++-style constructors
for vectors that are not supported by Clang.

Standard Library
----------------

HLSL does not support the C or C++ standard libraries. Like OpenCL, HLSL
describes its own library of built in types, complex data types, and functions.

Unsupported C & C++ Features
----------------------------

HLSL does not support all features of C and C++. In implementing HLSL in Clang
use of some C and C++ features will produce diagnostics under HLSL, and others
will be supported as language extensions. In general, any C or C++ feature that
can be supported by the DXIL and SPIR-V code generation targets could be treated
as a clang HLSL extension. Features that cannot be lowered to DXIL or SPIR-V,
must be diagnosed as errors.

HLSL does not support the following C features:

* Pointers
* References
* ``goto`` or labels
* Variable Length Arrays
* ``_Complex`` and ``_Imaginary``
* C Threads or Atomics (or Obj-C blocks)
* ``union`` types `(in progress for HLSL 202x) <https://github.com/microsoft/DirectXShaderCompiler/pull/4132>`_
* Most features C11 and later

HLSL does not support the following C++ features:

* RTTI
* Exceptions
* Multiple inheritance
* Access specifiers
* Anonymous or inline namespaces
* ``new`` & ``delete`` operators in all of their forms (array, placement, etc)
* Constructors and destructors
* Any use of the ``virtual`` keyword
* Most features C++11 and later
