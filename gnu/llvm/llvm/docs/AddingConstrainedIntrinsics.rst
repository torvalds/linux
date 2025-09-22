==================================================
How To Add A Constrained Floating-Point Intrinsic
==================================================

.. contents::
   :local:

.. warning::
  This is a work in progress.

Add the intrinsic
=================

Multiple files need to be updated when adding a new constrained intrinsic.

Add the new intrinsic to the table of intrinsics::

  include/llvm/IR/Intrinsics.td

Add SelectionDAG node types
===========================

Add the new STRICT version of the node type to the ISD::NodeType enum::

  include/llvm/CodeGen/ISDOpcodes.h

Strict version name must be a concatenation of prefix ``STRICT_`` and the name
of corresponding non-strict node name. For instance, strict version of the
node FADD must be STRICT_FADD.

Update mappings
===============

Add new record to the mapping of instructions to constrained intrinsic and
DAG nodes::

  include/llvm/IR/ConstrainedOps.def

Follow instructions provided in this file.

Update IR components
====================

Update the IR verifier::

  lib/IR/Verifier.cpp

Update Selector components
==========================

Building the SelectionDAG
-------------------------

The function SelectionDAGBuilder::visitConstrainedFPIntrinsic builds DAG nodes
using mappings specified in ConstrainedOps.def. If however this default build is
not sufficient, the build can be modified, see how it is implemented for
STRICT_FP_ROUND. The new STRICT node will eventually be converted
to the matching non-STRICT node. For this reason it should have the same
operands and values as the non-STRICT version but should also use the chain.
This makes subsequent sharing of code for STRICT and non-STRICT code paths
easier::

  lib/CodeGen/SelectionDAG/SelectionDAGBuilder.cpp

Most of the STRICT nodes get legalized the same as their matching non-STRICT
counterparts. A new STRICT node with this property must get added to the
switch in SelectionDAGLegalize::LegalizeOp().::

  lib/CodeGen/SelectionDAG/LegalizeDAG.cpp

Other parts of the legalizer may need to be updated as well. Look for
places where the non-STRICT counterpart is legalized and update as needed.
Be careful of the chain since STRICT nodes use it but their counterparts
often don't.

The code to do the conversion or mutation of the STRICT node to a non-STRICT
version of the node happens in SelectionDAG::mutateStrictFPToFP(). In most cases
the function can do the conversion using information from ConstrainedOps.def. Be
careful updating this function since some nodes have the same return type
as their input operand, but some are different. Both of these cases must
be properly handled::

  lib/CodeGen/SelectionDAG/SelectionDAG.cpp

Whether the mutation may happens or not, depends on how the new node has been
registered in TargetLoweringBase::initActions(). By default all strict nodes are
registered with Expand action::

  lib/CodeGen/TargetLoweringBase.cpp

To make debug logs readable it is helpful to update the SelectionDAG's
debug logger:::

  lib/CodeGen/SelectionDAG/SelectionDAGDumper.cpp

Add documentation and tests
===========================

::

  docs/LangRef.rst
