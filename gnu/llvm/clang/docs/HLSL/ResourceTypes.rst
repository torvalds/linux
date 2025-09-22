===================
HLSL Resource Types
===================

.. contents::
   :local:

Introduction
============

HLSL Resources are runtime-bound data that is provided as input, output or both
to shader programs written in HLSL. Resource Types in HLSL provide key user
abstractions for reading and writing resource data.

Implementation Details
======================

In Clang resource types are forward declared by the ``HLSLExternalSemaSource``
on initialization. They are then lazily completed when ``requiresCompleteType``
is called later in Sema.

Resource types are classes that have the "intangible" resource handle type,
`__hlsl_resource_t`, as a member. These are generally templated class
declarations that specify the type of data that can be loaded from or stored
into the resource. The handle is annotated with hlsl-specific attributes
describing properties of the resource. Member functions of a resource type are
generally fairly simple wrappers around builtins that operate on the handle
member.

During code generation resource types are lowered to target extension types in
IR. These types are target specific and differ between DXIL and SPIR-V
generation, providing the necessary information for the targets to generate
binding metadata for their respective target runtimes.
