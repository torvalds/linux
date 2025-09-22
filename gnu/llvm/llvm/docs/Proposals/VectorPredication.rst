==========================
Vector Predication Roadmap
==========================

.. contents:: Table of Contents
  :depth: 3
  :local:

Motivation
==========

This proposal defines a roadmap towards native vector predication in LLVM,
specifically for vector instructions with a mask and/or an explicit vector
length.  LLVM currently has no target-independent means to model predicated
vector instructions for modern SIMD ISAs such as AVX512, ARM SVE, the RISC-V V
extension and NEC SX-Aurora.  Only some predicated vector operations, such as
masked loads and stores, are available through intrinsics [MaskedIR]_.

The Vector Predication (VP) extensions is a concrete RFC and prototype
implementation to achieve native vector predication in LLVM.  The VP prototype
and all related discussions can be found in the VP patch on Phabricator
[VPRFC]_.

Roadmap
=======

1. IR-level VP intrinsics
-------------------------

- There is a consensus on the semantics/instruction set of VP.
- VP intrinsics and attributes are available on IR level.
- TTI has capability flags for VP (``supportsVP()``?,
  ``haveActiveVectorLength()``?).

Result: VP usable for IR-level vectorizers (LV, VPlan, RegionVectorizer),
potential integration in Clang with builtins.

2. CodeGen support
------------------

- VP intrinsics translate to first-class SDNodes
  (eg  ``llvm.vp.fdiv.* -> vp_fdiv``).
- VP legalization (legalize explicit vector length to mask (AVX512), legalize VP
  SDNodes to pre-existing ones (SSE, NEON)).

Result: Backend development based on VP SDNodes.

3. Lift InstSimplify/InstCombine/DAGCombiner to VP
--------------------------------------------------

- Introduce PredicatedInstruction, PredicatedBinaryOperator, .. helper classes
  that match standard vector IR and VP intrinsics.
- Add a matcher context to PatternMatch and context-aware IR Builder APIs.
- Incrementally lift DAGCombiner to work on VP SDNodes as well as on regular
  vector instructions.
- Incrementally lift InstCombine/InstSimplify to operate on VP as well as
  regular IR instructions.

Result: Optimization of VP intrinsics on par with standard vector instructions.

4. Deprecate llvm.masked.* / llvm.experimental.reduce.*
-------------------------------------------------------

- Modernize llvm.masked.* / llvm.experimental.reduce* by translating to VP.
- DCE transitional APIs.

Result: VP has superseded earlier vector intrinsics.

5. Predicated IR Instructions
-----------------------------

- Vector instructions have an optional mask and vector length parameter. These
  lower to VP SDNodes (from Stage 2).
- Phase out VP intrinsics, only keeping those that are not equivalent to
  vectorized scalar instructions (reduce,  shuffles, ..)
- InstCombine/InstSimplify expect predication in regular Instructions (Stage (3)
  has laid the groundwork).

Result: Native vector predication in IR.

References
==========

.. [MaskedIR] `llvm.masked.*` intrinsics,
   https://llvm.org/docs/LangRef.html#masked-vector-load-and-store-intrinsics

.. [VPRFC] RFC: Prototype & Roadmap for vector predication in LLVM,
   https://reviews.llvm.org/D57504
