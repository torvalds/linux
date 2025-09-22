==============================================================
Specification of DXIL Operations using TableGen Representation
==============================================================
.. contents::
   :local:

.. toctree
   :hidden

Introduction
============

`DirectXShaderCompiler <https://github.com/microsoft/DirectXShaderCompiler>`_
encapsulates, among other information, various DXIL Operations in
`hctdb.py <https://github.com/microsoft/DirectXShaderCompiler/blob/main/utils/hct/hctdb.py>`_.
DXIL Operations are represented in one of the following `two ways
<https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#operations>`_:

#. Using LLVM instructions.
#. Using LLVM External functions. These are represented in LLVM IR as follows:

   * "Standard" LLVM intrinsics (e.g., ``llvm.sin.*``) and
   * HLSL intrinsics (defined as LLVM intrinsics in ``llvm/include/llvm/IR/IntrinsicsDirectX.td``, e.g., ``llvm.dx.*``)

   These are  collectively referred to as `LLVM Intrinsics` in this note.

Following is the complete list of properties of DXIL Ops with the corresponding field name
as used in ``hctdb.py``. A DXIL Op is represented by a set of associated properties. These
are consumed in DXIL backend passes as well as in other usage scenarios such as validation,
DXIL reader, etc.

A. Properties consumed in DXIL backend passes

   1. Name of operation (``dxil_op``)
   2. A string that documents the operation (``doc``) - This is not strictly necessary but is included
      for readability and documentation of the operation.
   3. The generic or HLSL-specific intrinsic that maps to the operation (``llvm_name``).
   4. Unique Integer ID (``dxil_opid``)
   5. Operation Class signifying the name and function signature of the operation (``dxil_class``).
      This string is an integral part of the DXIL Op function name and is constructed in
      the format ``dx.op.<class-name>.<overload-type>``. Each DXIL Op call target function name
      is required to conform to this format per existing contract with the driver.
   6. List of valid overload types for the operation (``oload_types``).
   7. Required DXIL Version with support for the operation.
   8. Required minimum Shader Model (``shader_model``).
   9. Minimum shader model required with translation by linker (``shader_model_translated``)
   10.  List of shader stages applicable to (``shader_stages``), empty, if applicable to all stages.
   11.  Memory access attributes of the operation (``fn_attr``).
   12.  Boolean attributes of operation to indicate if it

        * is some kind of a derivative (``is_derivative``)
        * requires gradient calculation (``is_gradient``)
        * is a sampler feedback (``is_feedback``)
        * requires in-wave, cross-lane functionality (``is_wave``)
        * requires that all of its inputs are uniform across the wave (``requires_uniform_inputs``).
        * is a barrier operation (``is_barrier``).

Motivation
==========

DXIL backend passes depend on various properties of DXIL Operations. For example, ``DXILOpLowering``
pass will need information such as the DXIL operation an LLVM intrinsic is to be lowered to,
along with valid overload and argument types etc. The TableGen file -
``llvm/lib/Target/DirectX/DXIL.td`` - is used to represent DXIL Operations
by specifying their properties listed above. ``DXIL.td`` is designed to be the single source
of reference of DXIL Operations primarily for the implementation of passes in DXIL backend in
``llvm-project`` repo - analogous to ``hctdb.py`` for ``DirectXShadeCompiler`` repo. However,
the current design does not intend to encapsulate various validation rules, present in ``hctdb.py``,
but do not pertain to DXIL Operations. It needs to have a rich representation capabilities that
TableGen backends (such as ``DXILEmitter``) can rely on. Additionally, the DXIL Op specification
should be easy to read and comprehend.

This note provides the design of the specification DXIL Ops as TableGen class ``DXILOp``
by specifying its properties identified above.

DXIL Operation Specification
============================

The DXIL Operation is represented using the TableGen class ``DXILOp``. The DXIL operation
properties are specified as fields of the ``DXILOp`` class as described below.

1. Each DXIL Operation is represented as a TableGen record. The name of each of the records
   signifies operation name.
2. A documentation string for the operation.
3. The LLVM Intrinsic that maps to the operation is represented as ``Intrinsic`` defined in
   `Intrinsics.td <https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/IR/Intrinsics.td>`_.
4. The unique operation id is represented by an integer.
5. DXIL Operation Class is represented as follows

   .. code-block::

        // Abstraction of DXIL Operation class.
        class DXILOpClass;

   Concrete operation records, such as ``unary`` are defined by inheriting from ``DXILOpClass``.
6. Return type of the operation is represented as ``LLVMType``.
7. Operation arguments are represented as a list of ``LLVMType`` with each type
   corresponding to the argument position. An overload type, if supported by the operation, is
   denoted as the positional type ``overloadTy`` in the argument or in the result, where
   ``overloadTy`` is defined to be synonymous to ``llvm_any_ty``.

   .. code-block::

      defvar overloadTy = llvm_any_ty

   Empty list, ``[]`` represents an operation with no arguments.

8. Valid operation overload types predicated on DXIL version are specified as
   a list of ``Overloads`` records. Representation of ``Overloads``
   class is described in a later section.
9.  Valid shader stages predicated on DXIL version are specified as a list of
    ``Stages`` records. Representation of ``Stages`` class is
    described in a later section.
10. Various attributes of the DXIL Operation are represented as a ``list`` of
    ``Attributes`` class records. Representation of ``Attributes``
    class is described in a later section.

Types specific to DXIL
----------------------

Type notation used in this document viz., ``<size>Ty`` corresponds to TableGen records for
LLVM types ``llvm_<size>_ty``. Apart from ``overloadTy`` described above, ``resRetF32Ty`` is
used to denote resource return type and ``handleTy`` is used to denote handle type.

Specification of DXIL Operation
================================

A DXIL Operation is represented by the following TableGen class that encapsulates the various
TableGen representations of its properties described above.

.. code-block::

   // Abstraction DXIL Operation
   class DXILOp<int opcode, DXILOpClass opclass> {
     // A short description of the operation
     string Doc = "";

     // Opcode of DXIL Operation
     int OpCode = opcode;

     // Class of DXIL Operation.
     DXILOpClass OpClass = opclass;

     // LLVM Intrinsic DXIL Operation maps to
     Intrinsic LLVMIntrinsic = ?;

     // Result type of the op.
     LLVMType result;

     // List of argument types of the op. Default to 0 arguments.
     list<LLVMType> arguments = [];

     // List of valid overload types predicated by DXIL version
     list<Overloads> overloads;

     // List of valid shader stages predicated by DXIL version
    list<Stages> stages;

     // List of valid attributes predicated by DXIL version
     list<Attributes> attributes = [];
   }

Version Specification
=====================

DXIL version is used to specify various version-dependent operation properties in
place of Shader Model version.

A ``Version`` class encapsulating ``Major`` and ``Minor`` version number is defined
as follows:

.. code-block::

   // Abstract class to represent major and minor version values
   class Version<int major, int minor> {
     int Major = major;
     int Minor = minor;
   }


Concrete representations of valid DXIL versions are defined as follows:

.. code-block::

   // Definition of DXIL Version 1.0 - 1.8
   foreach i = 0...8 in {
     def DXIL1_#i : Version<1, i>;
   }

Shader Stage Specification
==========================

Various shader stages such as ``compute``, ``pixel``, ``vertex``, etc., are represented
as follows

.. code-block::

   // Shader stages
   class DXILShaderStage;

   def compute : DXILShaderStage;
   def pixel : DXILShaderStage;
   def vertex : DXILShaderStage;
   ...

Shader Attribute Specification
==============================

Various operation memory access and boolean attributes such as ``ReadNone``,
``IsWave`` etc., are represented as follows

.. code-block::

  class DXILAttribute;

  def ReadOnly : DXILOpAttributes;
  def ReadNone : DXILOpAttributes;
  def IsWave : DXILOpAttributes;
  ...

Versioned Property Specification
================================

DXIL Operation properties such as valid overload types, shader stages and
attributes are predicated on DXIL version. These are represented as list of
versioned properties.

Overload Type Specification
---------------------------

``overloads`` field of ``class DXILOp`` is used to represent valid operation
overloads predicated on DXIL version as list of records of the following class

.. code-block::

   class Overloads<Version minver, list<LLVMType> ols> {
     Version dxil_version = minver;
     list<LLVMType> overload_types = ols;
   }

Following is an example specification of valid overload types for ``DXIL1_0`` and
``DXIL1_2``.

.. code-block::

   overloads = [
                 Overloads<DXIL1_0, [halfTy, floatTy]>,
                 Overloads<DXIL1_2, [halfTy, floatTy, doubleTy]>
               ];

An empty list signifies that the operation supports no overload types.


Stages Specification
--------------------

``stages`` field of ``class DXILOp`` is used to represent valid operation
stages predicated on DXIL version as list of records of the following class

.. code-block::

   class Stages<Version minver, list<DXILShaderStage> sts> {
     Version dxil_version = minver;
     list<DXILShaderStage> shader_stages = sts;
   }

Following is an example specification of valid stages for ``DXIL1_0``,
``DXIL1_2``, ``DXIL1_4`` and ``DXIL1_6``.

.. code-block::

   stages = [
             Stages<DXIL1_0, [compute, pixel]>,
             Stages<DXIL1_2, [compute, pixel, mesh]>,
             Stages<DXIL1_4, [all_stages]>,
             Stages<DXIL1_6, [removed]>
            ];

The following two pseudo stage records in addition to standard shader stages
are defined.

1. ``all_stages`` signifies that the operation is valid for all stages in the
   specified DXIL version and later.
2. ``removed`` signifies removal of support for the operation in the specified
   DXIL version and later.

A non-empty list of supported stages is required to be specified. If an operation
is supported in all DXIL versions and all stages it is required to be specified as

.. code-block::

   stages = [Stages<DXIL1_0, [all_stages]>];


Attribute Specification
-----------------------

``attributes`` field of ``class DXILOp`` is used to represent valid operation
attributes predicated on DXIL version as list of records of the following class

.. code-block::

  class Attributes<MinVersion minver, list<DXILAttribute> attrs> {
    MinVersion dxil_version = ver;
    list<DXILAttribute> attributes = attrs;
  }

Following is an example specification of valid attributes for ``DXIL1_0``.

.. code-block::

   attributes = [Attributes<DXIL1_0, [ReadNone]];

A null list of ``attributes`` signifies no operation attributes.

Interpretation of Multiple Versioned Properties
-----------------------------------------------

Each of the versioned properties states that the specified overload type, stage or
attribute records are valid for the predicated DXIL version. Only
the properties corresponding to latest minimal DXIL version are applicable.
Note as in the above example, any overload types, stages or attributes,
that remain valid in a later DXIL version need to be specified in full.
For example, consider the following specification of valid overload types:

.. code-block::

   overloads = [
                Overloads<DXIL1_0, [halfTy, floatTy]>,
                Overloads<DXIL1_2, [halfTy, floatTy, doubleTy]>
               ];

It specifies that the overload types ``halfTy`` and ``floatTy`` are valid for DXIL
version 1.0 and later. It also specifies that  ``doubleTy`` is additionally supported
in DXIL version 1.2 and later.

This provides the flexibility to specify properties independent of other
versioned specifications in the list.


DXIL Operation Specification Examples
=====================================

Following examples illustrate the specification of some of the DXIL Ops.

``Sin`` operation - an operation valid in all DXIL versions and all stages
and has valid overload types predicated on DXIL version.

.. code-block::

  def Sin : DXILOp<13, unary> {
    let Doc = "Returns sine(theta) for theta in radians.";
    let LLVMIntrinsic = int_sin;
    let result = overloadTy;
    let arguments = [overloadTy];
    let overloads = [Overloads<DXIL1_0, [halfTy, floatTy]>];
    let stages = [Stages<DXIL1_0, [all_stages]>];
    let attributes = [Attributes<DXIL1_0, [ReadNone]>];
  }

``FlattenedThreadIdInGroup`` - an operation with no arguments, no
overload types, and valid stages and attributes predicated by DXIL Version.

.. code-block::

   def FlattenedThreadIdInGroup :  DXILOp<96, flattenedThreadIdInGroup> {
    let Doc = "Provides a flattened index for a given thread within a given "
              "group (SV_GroupIndex)";
    let LLVMIntrinsic = int_dx_flattened_thread_id_in_group;
    let result = i32Ty;
    let stages = [Stages<DXIL1_0, [compute, mesh, amplification, node]>];
    let attributes = [Attributes<DXIL1_0, [ReadNone]>];
   }

``RawBufferStore`` - an operation with ``void`` return type, valid overload types
predicated by DXIL Version and valid in all DXIL versions and stages.

.. code-block::

   def RawBufferStore : DXILOp<140, rawBufferStore> {
     let Doc = "Writes to a RWByteAddressBuffer or RWStructuredBuffer.";
     let result = voidTy;
     let arguments = [dxil_resource_ty, i32Ty, i32Ty, overloadTy,
                      overloadTy, overloadTy, overloadTy, i8Ty, i32Ty];
     let overloads = [
                      Overloads<DXIL1_2, [halfTy, floatTy, i16Ty, i32Ty]>,
                      Overloads<DXIL1_3>,[halfTy, floatTy, doubleTy,
                                                   i16Ty, i32Ty, i64Ty]>
                     ];
      let stages = [Stages<DXIL1_2, all_stages>];
      let attributes = [Attributes<DXIL1_0, [ReadOnly]>];
   }

``DerivCoarseX`` - an operation with no overload types and stages predicated
by DXIL Version.

.. code-block::

   def DerivCoarseX : DXILOp<83, unary> {
    let doc = "Computes the rate of change per stamp in x direction.";
    let LLVMIntrinsic = int_dx_ddx;
    let result = overloadTy;
    let arguments = [overloadTy];
    let stages = [
                   Stages<DXIL1_0, [library, pixel]>,
                   Stages<DXIL1_6, [library, pixel, amplification, compute, mesh]>
                 ];
    let attributes = [Attributes<DXIL1_0, [ReadNone]>];
   }

``CreateHandle`` - an operation with no overload types, no associated ``LLVMIntrinsic``
and stages predicated  by DXIL Version.

.. code-block::

   def CreateHandle : DXILOp<57, createHandle> {
     let doc = "Creates the handle to a resource";
     let result = i32Ty;
     let arguments = [i8Ty, i32Ty, i32Ty, i1Ty];
     let stages = [
                   Stages<DXIL1_0, [all_stages]>,
                   Stages<DXIL1_6, [removed]
                  ];
     let attributes = [Attributes<DXIL1_0, [ReadOnly]>];
   }

``Sample`` - an operation with valid overload types, stages and attributes
predicated by DXIL version.

.. code-block::

   def Sample : DXILOp<60, sample> {
     let Doc = "Samples a texture";
     let LLVMIntrinsic = int_dx_sample;
     let result = resRetF32Ty;
     let arguments = [handleTy, handleTy, floatTy, floatTy, floatTy, floatTy,
                      i32Ty, i32Ty, i32Ty, floatTy];
     let overloads = [Overloads<DXIL1_0, [halfTy, floatTy, i16Ty, i32Ty]>];
     let stages = [
                   Stages<DXIL1_0, [library, pixel]>,
                   Stages<DXIL1_6, [library, pixel, amplification, compute, mesh]>
                  ];
     let attributes = [Attributes<DXIL1_0, [ReadOnly]>];
   }

Summary
=======

This note sketches the design of a readable and maintainable TableGen specification of
DXIL Ops in ``DXIL.td`` intended to serve as a single source of reference for TableGen
backends (such as ``DXILEmitter``) that generate C++ representations used in DXIL
backend passes.
