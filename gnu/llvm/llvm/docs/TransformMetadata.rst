.. _transformation-metadata:

============================
Code Transformation Metadata
============================

.. contents::
   :local:

Overview
========

LLVM transformation passes can be controlled by attaching metadata to
the code to transform. By default, transformation passes use heuristics
to determine whether or not to perform transformations, and when doing
so, other details of how the transformations are applied (e.g., which
vectorization factor to select).
Unless the optimizer is otherwise directed, transformations are applied
conservatively. This conservatism generally allows the optimizer to
avoid unprofitable transformations, but in practice, this results in the
optimizer not applying transformations that would be highly profitable.

Frontends can give additional hints to LLVM passes on which
transformations they should apply. This can be additional knowledge that
cannot be derived from the emitted IR, or directives passed from the
user/programmer. OpenMP pragmas are an example of the latter.

If any such metadata is dropped from the program, the code's semantics
must not change.

Metadata on Loops
=================

Attributes can be attached to loops as described in :ref:`llvm.loop`.
Attributes can describe properties of the loop, disable transformations,
force specific transformations and set transformation options.

Because metadata nodes are immutable (with the exception of
``MDNode::replaceOperandWith`` which is dangerous to use on uniqued
metadata), in order to add or remove a loop attributes, a new ``MDNode``
must be created and assigned as the new ``llvm.loop`` metadata. Any
connection between the old ``MDNode`` and the loop is lost. The
``llvm.loop`` node is also used as LoopID (``Loop::getLoopID()``), i.e.
the loop effectively gets a new identifier. For instance,
``llvm.mem.parallel_loop_access`` references the LoopID. Therefore, if
the parallel access property is to be preserved after adding/removing
loop attributes, any ``llvm.mem.parallel_loop_access`` reference must be
updated to the new LoopID.

Transformation Metadata Structure
=================================

Some attributes describe code transformations (unrolling, vectorizing,
loop distribution, etc.). They can either be a hint to the optimizer
that a transformation might be beneficial, instruction to use a specific
option, , or convey a specific request from the user (such as
``#pragma clang loop`` or ``#pragma omp simd``).

If a transformation is forced but cannot be carried-out for any reason,
an optimization-missed warning must be emitted. Semantic information
such as a transformation being safe (e.g.
``llvm.mem.parallel_loop_access``) can be unused by the optimizer
without generating a warning.

Unless explicitly disabled, any optimization pass may heuristically
determine whether a transformation is beneficial and apply it. If
metadata for another transformation was specified, applying a different
transformation before it might be inadvertent due to being applied on a
different loop or the loop not existing anymore. To avoid having to
explicitly disable an unknown number of passes, the attribute
``llvm.loop.disable_nonforced`` disables all optional, high-level,
restructuring transformations.

The following example avoids the loop being altered before being
vectorized, for instance being unrolled.

.. code-block:: llvm

      br i1 %exitcond, label %for.exit, label %for.header, !llvm.loop !0
    ...
    !0 = distinct !{!0, !1, !2}
    !1 = !{!"llvm.loop.vectorize.enable", i1 true}
    !2 = !{!"llvm.loop.disable_nonforced"}

After a transformation is applied, follow-up attributes are set on the
transformed and/or new loop(s). This allows additional attributes
including followup-transformations to be specified. Specifying multiple
transformations in the same metadata node is possible for compatibility
reasons, but their execution order is undefined. For instance, when
``llvm.loop.vectorize.enable`` and ``llvm.loop.unroll.enable`` are
specified at the same time, unrolling may occur either before or after
vectorization.

As an example, the following instructs a loop to be vectorized and only
then unrolled.

.. code-block:: llvm

    !0 = distinct !{!0, !1, !2, !3}
    !1 = !{!"llvm.loop.vectorize.enable", i1 true}
    !2 = !{!"llvm.loop.disable_nonforced"}
    !3 = !{!"llvm.loop.vectorize.followup_vectorized", !{"llvm.loop.unroll.enable"}}

If, and only if, no followup is specified, the pass may add attributes itself.
For instance, the vectorizer adds a ``llvm.loop.isvectorized`` attribute and
all attributes from the original loop excluding its loop vectorizer
attributes. To avoid this, an empty followup attribute can be used, e.g.

.. code-block:: llvm

    !3 = !{!"llvm.loop.vectorize.followup_vectorized"}

The followup attributes of a transformation that cannot be applied will
never be added to a loop and are therefore effectively ignored. This means
that any followup-transformation in such attributes requires that its
prior transformations are applied before the followup-transformation.
The user should receive a warning about the first transformation in the
transformation chain that could not be applied if it a forced
transformation. All following transformations are skipped.

Pass-Specific Transformation Metadata
=====================================

Transformation options are specific to each transformation. In the
following, we present the model for each LLVM loop optimization pass and
the metadata to influence them.

Loop Vectorization and Interleaving
-----------------------------------

Loop vectorization and interleaving is interpreted as a single
transformation. It is interpreted as forced if
``!{"llvm.loop.vectorize.enable", i1 true}`` is set.

Assuming the pre-vectorization loop is

.. code-block:: c

    for (int i = 0; i < n; i+=1) // original loop
      Stmt(i);

then the code after vectorization will be approximately (assuming an
SIMD width of 4):

.. code-block:: c

    int i = 0;
    if (rtc) {
      for (; i + 3 < n; i+=4) // vectorized/interleaved loop
        Stmt(i:i+3);
    }
    for (; i < n; i+=1) // epilogue loop
      Stmt(i);

where ``rtc`` is a generated runtime check.

``llvm.loop.vectorize.followup_vectorized`` will set the attributes for
the vectorized loop. If not specified, ``llvm.loop.isvectorized`` is
combined with the original loop's attributes to avoid it being
vectorized multiple times.

``llvm.loop.vectorize.followup_epilogue`` will set the attributes for
the remainder loop. If not specified, it will have the original loop's
attributes combined with ``llvm.loop.isvectorized`` and
``llvm.loop.unroll.runtime.disable`` (unless the original loop already
has unroll metadata).

The attributes specified by ``llvm.loop.vectorize.followup_all`` are
added to both loops.

When using a follow-up attribute, it replaces any automatically deduced
attributes for the generated loop in question. Therefore it is
recommended to add ``llvm.loop.isvectorized`` to
``llvm.loop.vectorize.followup_all`` which avoids that the loop
vectorizer tries to optimize the loops again.

Loop Unrolling
--------------

Unrolling is interpreted as forced any ``!{!"llvm.loop.unroll.enable"}``
metadata or option (``llvm.loop.unroll.count``, ``llvm.loop.unroll.full``)
is present. Unrolling can be full unrolling, partial unrolling of a loop
with constant trip count or runtime unrolling of a loop with a trip
count unknown at compile-time.

If the loop has been unrolled fully, there is no followup-loop. For
partial/runtime unrolling, the original loop of

.. code-block:: c

    for (int i = 0; i < n; i+=1) // original loop
      Stmt(i);

is transformed into (using an unroll factor of 4):

.. code-block:: c

    int i = 0;
    for (; i + 3 < n; i+=4) { // unrolled loop
      Stmt(i);
      Stmt(i+1);
      Stmt(i+2);
      Stmt(i+3);
    }
    for (; i < n; i+=1) // remainder loop
      Stmt(i);

``llvm.loop.unroll.followup_unrolled`` will set the loop attributes of
the unrolled loop. If not specified, the attributes of the original loop
without the ``llvm.loop.unroll.*`` attributes are copied and
``llvm.loop.unroll.disable`` added to it.

``llvm.loop.unroll.followup_remainder`` defines the attributes of the
remainder loop. If not specified the remainder loop will have no
attributes. The remainder loop might not be present due to being fully
unrolled in which case this attribute has no effect.

Attributes defined in ``llvm.loop.unroll.followup_all`` are added to the
unrolled and remainder loops.

To avoid that the partially unrolled loop is unrolled again, it is
recommended to add ``llvm.loop.unroll.disable`` to
``llvm.loop.unroll.followup_all``. If no follow-up attribute specified
for a generated loop, it is added automatically.

Unroll-And-Jam
--------------

Unroll-and-jam uses the following transformation model (here with an
unroll factor if 2). Currently, it does not support a fallback version
when the transformation is unsafe.

.. code-block:: c

    for (int i = 0; i < n; i+=1) { // original outer loop
      Fore(i);
      for (int j = 0; j < m; j+=1) // original inner loop
        SubLoop(i, j);
      Aft(i);
    }

.. code-block:: c

    int i = 0;
    for (; i + 1 < n; i+=2) { // unrolled outer loop
      Fore(i);
      Fore(i+1);
      for (int j = 0; j < m; j+=1) { // unrolled inner loop
        SubLoop(i, j);
        SubLoop(i+1, j);
      }
      Aft(i);
      Aft(i+1);
    }
    for (; i < n; i+=1) { // remainder outer loop
      Fore(i);
      for (int j = 0; j < m; j+=1) // remainder inner loop
        SubLoop(i, j);
      Aft(i);
    }

``llvm.loop.unroll_and_jam.followup_outer`` will set the loop attributes
of the unrolled outer loop. If not specified, the attributes of the
original outer loop without the ``llvm.loop.unroll.*`` attributes are
copied and ``llvm.loop.unroll.disable`` added to it.

``llvm.loop.unroll_and_jam.followup_inner`` will set the loop attributes
of the unrolled inner loop. If not specified, the attributes of the
original inner loop are used unchanged.

``llvm.loop.unroll_and_jam.followup_remainder_outer`` sets the loop
attributes of the outer remainder loop. If not specified it will not
have any attributes. The remainder loop might not be present due to
being fully unrolled.

``llvm.loop.unroll_and_jam.followup_remainder_inner`` sets the loop
attributes of the inner remainder loop. If not specified it will have
the attributes of the original inner loop. It the outer remainder loop
is unrolled, the inner remainder loop might be present multiple times.

Attributes defined in ``llvm.loop.unroll_and_jam.followup_all`` are
added to all of the aforementioned output loops.

To avoid that the unrolled loop is unrolled again, it is
recommended to add ``llvm.loop.unroll.disable`` to
``llvm.loop.unroll_and_jam.followup_all``. It suppresses unroll-and-jam
as well as an additional inner loop unrolling. If no follow-up
attribute specified for a generated loop, it is added automatically.

Loop Distribution
-----------------

The LoopDistribution pass tries to separate vectorizable parts of a loop
from the non-vectorizable part (which otherwise would make the entire
loop non-vectorizable). Conceptually, it transforms a loop such as

.. code-block:: c

    for (int i = 1; i < n; i+=1) { // original loop
      A[i] = i;
      B[i] = 2 + B[i];
      C[i] = 3 + C[i - 1];
    }

into the following code:

.. code-block:: c

    if (rtc) {
      for (int i = 1; i < n; i+=1) // coincident loop
        A[i] = i;
      for (int i = 1; i < n; i+=1) // coincident loop
        B[i] = 2 + B[i];
      for (int i = 1; i < n; i+=1) // sequential loop
        C[i] = 3 + C[i - 1];
    } else {
      for (int i = 1; i < n; i+=1) { // fallback loop
        A[i] = i;
        B[i] = 2 + B[i];
        C[i] = 3 + C[i - 1];
      }
    }

where ``rtc`` is a generated runtime check.

``llvm.loop.distribute.followup_coincident`` sets the loop attributes of
all loops without loop-carried dependencies (i.e. vectorizable loops).
There might be more than one such loops. If not defined, the loops will
inherit the original loop's attributes.

``llvm.loop.distribute.followup_sequential`` sets the loop attributes of the
loop with potentially unsafe dependencies. There should be at most one
such loop. If not defined, the loop will inherit the original loop's
attributes.

``llvm.loop.distribute.followup_fallback`` defines the loop attributes
for the fallback loop, which is a copy of the original loop for when
loop versioning is required. If undefined, the fallback loop inherits
all attributes from the original loop.

Attributes defined in ``llvm.loop.distribute.followup_all`` are added to
all of the aforementioned output loops.

It is recommended to add ``llvm.loop.disable_nonforced`` to
``llvm.loop.distribute.followup_fallback``. This avoids that the
fallback version (which is likely never executed) is further optimized
which would increase the code size.

Versioning LICM
---------------

The pass hoists code out of loops that are only loop-invariant when
dynamic conditions apply. For instance, it transforms the loop

.. code-block:: c

    for (int i = 0; i < n; i+=1) // original loop
      A[i] = B[0];

into:

.. code-block:: c

    if (rtc) {
      auto b = B[0];
      for (int i = 0; i < n; i+=1) // versioned loop
        A[i] = b;
    } else {
      for (int i = 0; i < n; i+=1) // unversioned loop
        A[i] = B[0];
    }

The runtime condition (``rtc``) checks that the array ``A`` and the
element `B[0]` do not alias.

Currently, this transformation does not support followup-attributes.

Loop Interchange
----------------

Currently, the ``LoopInterchange`` pass does not use any metadata.

Ambiguous Transformation Order
==============================

If there multiple transformations defined, the order in which they are
executed depends on the order in LLVM's pass pipeline, which is subject
to change. The default optimization pipeline (anything higher than
``-O0``) has the following order.

When using the legacy pass manager:

 - LoopInterchange (if enabled)
 - SimpleLoopUnroll/LoopFullUnroll (only performs full unrolling)
 - VersioningLICM (if enabled)
 - LoopDistribute
 - LoopVectorizer
 - LoopUnrollAndJam (if enabled)
 - LoopUnroll (partial and runtime unrolling)

When using the legacy pass manager with LTO:

 - LoopInterchange (if enabled)
 - SimpleLoopUnroll/LoopFullUnroll (only performs full unrolling)
 - LoopVectorizer
 - LoopUnroll (partial and runtime unrolling)

When using the new pass manager:

 - SimpleLoopUnroll/LoopFullUnroll (only performs full unrolling)
 - LoopDistribute
 - LoopVectorizer
 - LoopUnrollAndJam (if enabled)
 - LoopUnroll (partial and runtime unrolling)

Leftover Transformations
========================

Forced transformations that have not been applied after the last
transformation pass should be reported to the user. The transformation
passes themselves cannot be responsible for this reporting because they
might not be in the pipeline, there might be multiple passes able to
apply a transformation (e.g. ``LoopInterchange`` and Polly) or a
transformation attribute may be 'hidden' inside another passes' followup
attribute.

The pass ``-transform-warning`` (``WarnMissedTransformationsPass``)
emits such warnings. It should be placed after the last transformation
pass.

The current pass pipeline has a fixed order in which transformations
passes are executed. A transformation can be in the followup of a pass
that is executed later and thus leftover. For instance, a loop nest
cannot be distributed and then interchanged with the current pass
pipeline. The loop distribution will execute, but there is no loop
interchange pass following such that any loop interchange metadata will
be ignored. The ``-transform-warning`` should emit a warning in this
case.

Future versions of LLVM may fix this by executing transformations using
a dynamic ordering.
