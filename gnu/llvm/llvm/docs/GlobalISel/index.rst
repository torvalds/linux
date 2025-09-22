============================
Global Instruction Selection
============================

.. warning::
   This document is a work in progress.  It reflects the current state of the
   implementation, as well as open design and implementation issues.

.. contents::
   :local:
   :depth: 1

Introduction
============

GlobalISel is a framework that provides a set of reusable passes and utilities
for instruction selection --- translation from LLVM IR to target-specific
Machine IR (MIR).

GlobalISel is intended to be a replacement for SelectionDAG and FastISel, to
solve three major problems:

* **Performance** --- SelectionDAG introduces a dedicated intermediate
  representation, which has a compile-time cost.

  GlobalISel directly operates on the post-isel representation used by the
  rest of the code generator, MIR.
  It does require extensions to that representation to support arbitrary
  incoming IR: :ref:`gmir`.

* **Granularity** --- SelectionDAG and FastISel operate on individual basic
  blocks, losing some global optimization opportunities.

  GlobalISel operates on the whole function.

* **Modularity** --- SelectionDAG and FastISel are radically different and share
  very little code.

  GlobalISel is built in a way that enables code reuse. For instance, both the
  optimized and fast selectors share the :ref:`pipeline`, and targets can
  configure that pipeline to better suit their needs.

Design and Implementation Reference
===================================

More information on the design and implementation of GlobalISel can be found in
the following sections.

.. toctree::
  :maxdepth: 1

  GMIR
  GenericOpcode
  MIRPatterns
  Pipeline
  Porting
  Resources

More information on specific passes can be found in the following sections:

.. toctree::
  :maxdepth: 1

  IRTranslator
  Legalizer
  RegBankSelect
  InstructionSelect
  KnownBits

.. _progress:

Progress and Future Work
========================

The initial goal is to replace FastISel on AArch64.  The next step will be to
replace SelectionDAG as the optimized ISel.

``NOTE``:
While we iterate on GlobalISel, we strive to avoid affecting the performance of
SelectionDAG, FastISel, or the other MIR passes.  For instance, the types of
:ref:`gmir-gvregs` are stored in a separate table in ``MachineRegisterInfo``,
that is destroyed after :ref:`instructionselect`.

.. _progress-fastisel:

FastISel Replacement
--------------------

For the initial FastISel replacement, we intend to fallback to SelectionDAG on
selection failures.

Currently, compile-time of the fast pipeline is within 1.5x of FastISel.
We're optimistic we can get to within 1.1/1.2x, but beating FastISel will be
challenging given the multi-pass approach.
Still, supporting all IR (via a complete legalizer) and avoiding the fallback
to SelectionDAG in the worst case should enable better amortized performance
than SelectionDAG+FastISel.

``NOTE``:
We considered never having a fallback to SelectionDAG, instead deciding early
whether a given function is supported by GlobalISel or not.  The decision would
be based on :ref:`milegalizer` queries.
We abandoned that for two reasons:
a) on IR inputs, we'd need to basically simulate the :ref:`irtranslator`;
b) to be robust against unforeseen failures and to enable iterative
improvements.
