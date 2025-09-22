================================
LLVM Block Frequency Terminology
================================

.. contents::
   :local:

Introduction
============

Block Frequency is a metric for estimating the relative frequency of different
basic blocks.  This document describes the terminology that the
``BlockFrequencyInfo`` and ``MachineBlockFrequencyInfo`` analysis passes use.

Branch Probability
==================

Blocks with multiple successors have probabilities associated with each
outgoing edge.  These are called branch probabilities.  For a given block, the
sum of its outgoing branch probabilities should be 1.0.

Branch Weight
=============

Rather than storing fractions on each edge, we store an integer weight.
Weights are relative to the other edges of a given predecessor block.  The
branch probability associated with a given edge is its own weight divided by
the sum of the weights on the predecessor's outgoing edges.

For example, consider this IR:

.. code-block:: llvm

   define void @foo() {
       ; ...
       A:
           br i1 %cond, label %B, label %C, !prof !0
       ; ...
   }
   !0 = !{!"branch_weights", i32 7, i32 8}

and this simple graph representation::

   A -> B  (edge-weight: 7)
   A -> C  (edge-weight: 8)

The probability of branching from block A to block B is 7/15, and the
probability of branching from block A to block C is 8/15.

See :doc:`BranchWeightMetadata` for details about the branch weight IR
representation.

Block Frequency
===============

Block frequency is a relative metric that represents the number of times a
block executes.  The ratio of a block frequency to the entry block frequency is
the expected number of times the block will execute per entry to the function.

Block frequency is the main output of the ``BlockFrequencyInfo`` and
``MachineBlockFrequencyInfo`` analysis passes.

Implementation: a series of DAGs
================================

The implementation of the block frequency calculation analyses each loop,
bottom-up, ignoring backedges; i.e., as a DAG.  After each loop is processed,
it's packaged up to act as a pseudo-node in its parent loop's (or the
function's) DAG analysis.

Block Mass
==========

For each DAG, the entry node is assigned a mass of ``UINT64_MAX`` and mass is
distributed to successors according to branch weights.  Block Mass uses a
fixed-point representation where ``UINT64_MAX`` represents ``1.0`` and ``0``
represents a number just above ``0.0``.

After mass is fully distributed, in any cut of the DAG that separates the exit
nodes from the entry node, the sum of the block masses of the nodes succeeded
by a cut edge should equal ``UINT64_MAX``.  In other words, mass is conserved
as it "falls" through the DAG.

If a function's basic block graph is a DAG, then block masses are valid block
frequencies.  This works poorly in practice though, since downstream users rely
on adding block frequencies together without hitting the maximum.

Loop Scale
==========

Loop scale is a metric that indicates how many times a loop iterates per entry.
As mass is distributed through the loop's DAG, the (otherwise ignored) backedge
mass is collected.  This backedge mass is used to compute the exit frequency,
and thus the loop scale.

Implementation: Getting from mass and scale to frequency
========================================================

After analysing the complete series of DAGs, each block has a mass (local to
its containing loop, if any), and each loop pseudo-node has a loop scale and
its own mass (from its parent's DAG).

We can get an initial frequency assignment (with entry frequency of 1.0) by
multiplying these masses and loop scales together.  A given block's frequency
is the product of its mass, the mass of containing loops' pseudo nodes, and the
containing loops' loop scales.

Since downstream users need integers (not floating point), this initial
frequency assignment is shifted as necessary into the range of ``uint64_t``.

Block Bias
==========

Block bias is a proposed *absolute* metric to indicate a bias toward or away
from a given block during a function's execution.  The idea is that bias can be
used in isolation to indicate whether a block is relatively hot or cold, or to
compare two blocks to indicate whether one is hotter or colder than the other.

The proposed calculation involves calculating a *reference* block frequency,
where:

* every branch weight is assumed to be 1 (i.e., every branch probability
  distribution is even) and

* loop scales are ignored.

This reference frequency represents what the block frequency would be in an
unbiased graph.

The bias is the ratio of the block frequency to this reference block frequency.
