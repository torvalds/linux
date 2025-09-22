=========
MemorySSA
=========

.. contents::
   :local:

Introduction
============

``MemorySSA`` is an analysis that allows us to cheaply reason about the
interactions between various memory operations. Its goal is to replace
``MemoryDependenceAnalysis`` for most (if not all) use-cases. This is because,
unless you're very careful, use of ``MemoryDependenceAnalysis`` can easily
result in quadratic-time algorithms in LLVM. Additionally, ``MemorySSA`` doesn't
have as many arbitrary limits as ``MemoryDependenceAnalysis``, so you should get
better results, too. One common use of ``MemorySSA`` is to quickly find out
that something definitely cannot happen (for example, reason that a hoist
out of a loop can't happen).

At a high level, one of the goals of ``MemorySSA`` is to provide an SSA based
form for memory, complete with def-use and use-def chains, which
enables users to quickly find may-def and may-uses of memory operations.
It can also be thought of as a way to cheaply give versions to the complete
state of memory, and associate memory operations with those versions.

This document goes over how ``MemorySSA`` is structured, and some basic
intuition on how ``MemorySSA`` works.

A paper on MemorySSA (with notes about how it's implemented in GCC) `can be
found here <http://www.airs.com/dnovillo/Papers/mem-ssa.pdf>`_. Though, it's
relatively out-of-date; the paper references multiple memory partitions, but GCC
eventually swapped to just using one, like we now have in LLVM.  Like
GCC's, LLVM's MemorySSA is intraprocedural.


MemorySSA Structure
===================

MemorySSA is a virtual IR. After it's built, ``MemorySSA`` will contain a
structure that maps ``Instruction``\ s to ``MemoryAccess``\ es, which are
``MemorySSA``'s parallel to LLVM ``Instruction``\ s.

Each ``MemoryAccess`` can be one of three types:

- ``MemoryDef``
- ``MemoryPhi``
- ``MemoryUse``

``MemoryDef``\ s are operations which may either modify memory, or which
introduce some kind of ordering constraints. Examples of ``MemoryDef``\ s
include ``store``\ s, function calls, ``load``\ s with ``acquire`` (or higher)
ordering, volatile operations, memory fences, etc. A ``MemoryDef``
always introduces a new version of the entire memory and is linked with a single
``MemoryDef/MemoryPhi`` which is the version of memory that the new
version is based on. This implies that there is a *single*
``Def`` chain that connects all the ``Def``\ s, either directly
or indirectly. For example in:

.. code-block:: llvm

  b = MemoryDef(a)
  c = MemoryDef(b)
  d = MemoryDef(c)

``d`` is connected directly with ``c`` and indirectly with ``b``.
This means that ``d`` potentially clobbers (see below) ``c`` *or*
``b`` *or* both. This in turn implies that without the use of `The walker`_,
initially every ``MemoryDef`` clobbers every other ``MemoryDef``.

``MemoryPhi``\ s are ``PhiNode``\ s, but for memory operations. If at any
point we have two (or more) ``MemoryDef``\ s that could flow into a
``BasicBlock``, the block's top ``MemoryAccess`` will be a
``MemoryPhi``. As in LLVM IR, ``MemoryPhi``\ s don't correspond to any
concrete operation. As such, ``BasicBlock``\ s are mapped to ``MemoryPhi``\ s
inside ``MemorySSA``, whereas ``Instruction``\ s are mapped to ``MemoryUse``\ s
and ``MemoryDef``\ s.

Note also that in SSA, Phi nodes merge must-reach definitions (that is,
definitions that *must* be new versions of variables). In MemorySSA, PHI nodes
merge may-reach definitions (that is, until disambiguated, the versions that
reach a phi node may or may not clobber a given variable).

``MemoryUse``\ s are operations which use but don't modify memory. An example of
a ``MemoryUse`` is a ``load``, or a ``readonly`` function call.

Every function that exists has a special ``MemoryDef`` called ``liveOnEntry``.
It dominates every ``MemoryAccess`` in the function that ``MemorySSA`` is being
run on, and implies that we've hit the top of the function. It's the only
``MemoryDef`` that maps to no ``Instruction`` in LLVM IR. Use of
``liveOnEntry`` implies that the memory being used is either undefined or
defined before the function begins.

An example of all of this overlaid on LLVM IR (obtained by running ``opt
-passes='print<memoryssa>' -disable-output`` on an ``.ll`` file) is below. When
viewing this example, it may be helpful to view it in terms of clobbers.
The operands of a given ``MemoryAccess`` are all (potential) clobbers of said
``MemoryAccess``, and the value produced by a ``MemoryAccess`` can act as a clobber
for other ``MemoryAccess``\ es.

If a ``MemoryAccess`` is a *clobber* of another, it means that these two
``MemoryAccess``\ es may access the same memory. For example, ``x = MemoryDef(y)``
means that ``x`` potentially modifies memory that ``y`` modifies/constrains
(or has modified / constrained).
In the same manner, ``a = MemoryPhi({BB1,b},{BB2,c})`` means that
anyone that uses ``a`` is accessing memory potentially modified / constrained
by either ``b`` or ``c`` (or both).  And finally, ``MemoryUse(x)`` means
that this use accesses memory that ``x`` has modified / constrained
(as an example, think that if ``x = MemoryDef(...)``
and ``MemoryUse(x)`` are in the same loop, the use can't
be hoisted outside alone).

Another useful way of looking at it is in terms of memory versions.
In that view, operands of a given ``MemoryAccess`` are the version
of the entire memory before the operation, and if the access produces
a value (i.e. ``MemoryDef/MemoryPhi``),
the value is the new version of the memory after the operation.

.. code-block:: llvm

  define void @foo() {
  entry:
    %p1 = alloca i8
    %p2 = alloca i8
    %p3 = alloca i8
    ; 1 = MemoryDef(liveOnEntry)
    store i8 0, ptr %p3
    br label %while.cond

  while.cond:
    ; 6 = MemoryPhi({entry,1},{if.end,4})
    br i1 undef, label %if.then, label %if.else

  if.then:
    ; 2 = MemoryDef(6)
    store i8 0, ptr %p1
    br label %if.end

  if.else:
    ; 3 = MemoryDef(6)
    store i8 1, ptr %p2
    br label %if.end

  if.end:
    ; 5 = MemoryPhi({if.then,2},{if.else,3})
    ; MemoryUse(5)
    %1 = load i8, ptr %p1
    ; 4 = MemoryDef(5)
    store i8 2, ptr %p2
    ; MemoryUse(1)
    %2 = load i8, ptr %p3
    br label %while.cond
  }

The ``MemorySSA`` IR is shown in comments that precede the instructions they map
to (if such an instruction exists). For example, ``1 = MemoryDef(liveOnEntry)``
is a ``MemoryAccess`` (specifically, a ``MemoryDef``), and it describes the LLVM
instruction ``store i8 0, ptr %p3``. Other places in ``MemorySSA`` refer to this
particular ``MemoryDef`` as ``1`` (much like how one can refer to ``load i8, ptr
%p1`` in LLVM with ``%1``). Again, ``MemoryPhi``\ s don't correspond to any LLVM
Instruction, so the line directly below a ``MemoryPhi`` isn't special.

Going from the top down:

- ``6 = MemoryPhi({entry,1},{if.end,4})`` notes that, when entering
  ``while.cond``, the reaching definition for it is either ``1`` or ``4``. This
  ``MemoryPhi`` is referred to in the textual IR by the number ``6``.
- ``2 = MemoryDef(6)`` notes that ``store i8 0, ptr %p1`` is a definition,
  and its reaching definition before it is ``6``, or the ``MemoryPhi`` after
  ``while.cond``. (See the `Use and Def optimization`_ and `Precision`_
  sections below for why this ``MemoryDef`` isn't linked to a separate,
  disambiguated ``MemoryPhi``.)
- ``3 = MemoryDef(6)`` notes that ``store i8 0, ptr %p2`` is a definition; its
  reaching definition is also ``6``.
- ``5 = MemoryPhi({if.then,2},{if.else,3})`` notes that the clobber before
  this block could either be ``2`` or ``3``.
- ``MemoryUse(5)`` notes that ``load i8, ptr %p1`` is a use of memory, and that
  it's clobbered by ``5``.
- ``4 = MemoryDef(5)`` notes that ``store i8 2, ptr %p2`` is a definition; its
  reaching definition is ``5``.
- ``MemoryUse(1)`` notes that ``load i8, ptr %p3`` is just a user of memory,
  and the last thing that could clobber this use is above ``while.cond`` (e.g.
  the store to ``%p3``). In memory versioning parlance, it really only depends on
  the memory version 1, and is unaffected by the new memory versions generated since
  then.

As an aside, ``MemoryAccess`` is a ``Value`` mostly for convenience; it's not
meant to interact with LLVM IR.

Design of MemorySSA
===================

``MemorySSA`` is an analysis that can be built for any arbitrary function. When
it's built, it does a pass over the function's IR in order to build up its
mapping of ``MemoryAccess``\ es. You can then query ``MemorySSA`` for things
like the dominance relation between ``MemoryAccess``\ es, and get the
``MemoryAccess`` for any given ``Instruction`` .

When ``MemorySSA`` is done building, it also hands you a ``MemorySSAWalker``
that you can use (see below).


The walker
----------

A structure that helps ``MemorySSA`` do its job is the ``MemorySSAWalker``, or
the walker, for short. The goal of the walker is to provide answers to clobber
queries beyond what's represented directly by ``MemoryAccess``\ es. For example,
given:

.. code-block:: llvm

  define void @foo() {
    %a = alloca i8
    %b = alloca i8

    ; 1 = MemoryDef(liveOnEntry)
    store i8 0, ptr %a
    ; 2 = MemoryDef(1)
    store i8 0, ptr %b
  }

The store to ``%a`` is clearly not a clobber for the store to ``%b``. It would
be the walker's goal to figure this out, and return ``liveOnEntry`` when queried
for the clobber of ``MemoryAccess`` ``2``.

By default, ``MemorySSA`` provides a walker that can optimize ``MemoryDef``\ s
and ``MemoryUse``\ s by consulting whatever alias analysis stack you happen to
be using. Walkers were built to be flexible, though, so it's entirely reasonable
(and expected) to create more specialized walkers (e.g. one that specifically
queries ``GlobalsAA``, one that always stops at ``MemoryPhi`` nodes, etc).

Default walker APIs
^^^^^^^^^^^^^^^^^^^

There are two main APIs used to retrieve the clobbering access using the walker:

-  ``MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA);`` return the
   clobbering memory access for ``MA``, caching all intermediate results
   computed along the way as part of each access queried.

-  ``MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA, const MemoryLocation &Loc);``
   returns the access clobbering memory location ``Loc``, starting at ``MA``.
   Because this API does not request the clobbering access of a specific memory
   access, there are no results that can be cached.

Locating clobbers yourself
^^^^^^^^^^^^^^^^^^^^^^^^^^

If you choose to make your own walker, you can find the clobber for a
``MemoryAccess`` by walking every ``MemoryDef`` that dominates said
``MemoryAccess``. The structure of ``MemoryDef``\ s makes this relatively simple;
they ultimately form a linked list of every clobber that dominates the
``MemoryAccess`` that you're trying to optimize. In other words, the
``definingAccess`` of a ``MemoryDef`` is always the nearest dominating
``MemoryDef`` or ``MemoryPhi`` of said ``MemoryDef``.


Use and Def optimization
------------------------

``MemoryUse``\ s keep a single operand, which is their defining or optimized
access.
Traditionally ``MemorySSA`` optimized ``MemoryUse``\ s at build-time, up to a
given threshold.
Specifically, the operand of every ``MemoryUse`` was optimized to point to the
actual clobber of said ``MemoryUse``. This can be seen in the above example; the
second ``MemoryUse`` in ``if.end`` has an operand of ``1``, which is a
``MemoryDef`` from the entry block.  This is done to make walking,
value numbering, etc, faster and easier.
As of `this revision <https://reviews.llvm.org/D121381>`_, the default was
changed to not optimize uses at build time, in order to provide the option to
reduce compile-time if the walking is not necessary in a pass. Most users call
the new API ``ensureOptimizedUses()`` to keep the previous behavior and do a
one-time optimization of ``MemoryUse``\ s, if this was not done before.
New pass users are recommended to call ``ensureOptimizedUses()``.

Initially it was not possible to optimize ``MemoryDef``\ s in the same way, as we
restricted ``MemorySSA`` to one operand per access.
This was changed and ``MemoryDef``\ s now keep two operands.
The first one, the defining access, is
always the previous ``MemoryDef`` or ``MemoryPhi`` in the same basic block, or
the last one in a dominating predecessor if the current block doesn't have any
other accesses writing to memory. This is needed for walking Def chains.
The second operand is the optimized access, if there was a previous call on the
walker's ``getClobberingMemoryAccess(MA)``. This API will cache information
as part of ``MA``.
Optimizing all ``MemoryDef``\ s has quadratic time complexity and is not done
by default.

A walk of the uses for any MemoryDef can find the accesses that were optimized
to it.
A code snippet for such a walk looks like this:

.. code-block:: c++

  MemoryDef *Def;  // find who's optimized or defining for this MemoryDef
  for (auto &U : Def->uses()) {
    MemoryAccess *MA = cast<MemoryAccess>(U.getUser());
    if (auto *DefUser = dyn_cast<MemoryDef>(MA))
      if (DefUser->isOptimized() && DefUser->getOptimized() == Def) {
        // User who is optimized to Def
      } else {
        // User who's defining access is Def; optimized to something else or not optimized.
      }
  }

When ``MemoryUse``\ s are optimized, for a given store,  you can find all loads
clobbered by that store by walking the immediate and transitive uses of
the store.

.. code-block:: c++

  checkUses(MemoryAccess *Def) { // Def can be a MemoryDef or a MemoryPhi.
    for (auto &U : Def->uses()) {
      MemoryAccess *MA = cast<MemoryAccess>(U.getUser());
      if (auto *MU = dyn_cast<MemoryUse>(MA)) {
        // Process MemoryUse as needed.
      } else {
        // Process MemoryDef or MemoryPhi as needed.

        // As a user can come up twice, as an optimized access and defining
        // access, keep a visited list.

        // Check transitive uses as needed
        checkUses(MA); // use a worklist for an iterative algorithm
      }
    }
  }

An example of similar traversals can be found in the DeadStoreElimination pass.

Invalidation and updating
-------------------------

Because ``MemorySSA`` keeps track of LLVM IR, it needs to be updated whenever
the IR is updated. "Update", in this case, includes the addition, deletion, and
motion of ``Instructions``. The update API is being made on an as-needed basis.
If you'd like examples, ``GVNHoist`` and ``LICM`` are users of ``MemorySSA``\ s
update API.
Note that adding new ``MemoryDef``\ s (by calling ``insertDef``) can be a
time-consuming update, if the new access triggers many ``MemoryPhi`` insertions and
renaming (optimization invalidation) of many ``MemoryAccesses``\ es.


Phi placement
^^^^^^^^^^^^^

``MemorySSA`` only places ``MemoryPhi``\ s where they're actually
needed. That is, it is a pruned SSA form, like LLVM's SSA form.  For
example, consider:

.. code-block:: llvm

  define void @foo() {
  entry:
    %p1 = alloca i8
    %p2 = alloca i8
    %p3 = alloca i8
    ; 1 = MemoryDef(liveOnEntry)
    store i8 0, ptr %p3
    br label %while.cond

  while.cond:
    ; 3 = MemoryPhi({%0,1},{if.end,2})
    br i1 undef, label %if.then, label %if.else

  if.then:
    br label %if.end

  if.else:
    br label %if.end

  if.end:
    ; MemoryUse(1)
    %1 = load i8, ptr %p1
    ; 2 = MemoryDef(3)
    store i8 2, ptr %p2
    ; MemoryUse(1)
    %2 = load i8, ptr %p3
    br label %while.cond
  }

Because we removed the stores from ``if.then`` and ``if.else``, a ``MemoryPhi``
for ``if.end`` would be pointless, so we don't place one. So, if you need to
place a ``MemoryDef`` in ``if.then`` or ``if.else``, you'll need to also create
a ``MemoryPhi`` for ``if.end``.

If it turns out that this is a large burden, we can just place ``MemoryPhi``\ s
everywhere. Because we have Walkers that are capable of optimizing above said
phis, doing so shouldn't prohibit optimizations.


Non-Goals
---------

``MemorySSA`` is meant to reason about the relation between memory
operations, and enable quicker querying.
It isn't meant to be the single source of truth for all potential memory-related
optimizations. Specifically, care must be taken when trying to use ``MemorySSA``
to reason about atomic or volatile operations, as in:

.. code-block:: llvm

  define i8 @foo(ptr %a) {
  entry:
    br i1 undef, label %if.then, label %if.end

  if.then:
    ; 1 = MemoryDef(liveOnEntry)
    %0 = load volatile i8, ptr %a
    br label %if.end

  if.end:
    %av = phi i8 [0, %entry], [%0, %if.then]
    ret i8 %av
  }

Going solely by ``MemorySSA``'s analysis, hoisting the ``load`` to ``entry`` may
seem legal. Because it's a volatile load, though, it's not.


Design tradeoffs
----------------

Precision
^^^^^^^^^

``MemorySSA`` in LLVM deliberately trades off precision for speed.
Let us think about memory variables as if they were disjoint partitions of the
memory (that is, if you have one variable, as above, it represents the entire
memory, and if you have multiple variables, each one represents some
disjoint portion of the memory)

First, because alias analysis results conflict with each other, and
each result may be what an analysis wants (IE
TBAA may say no-alias, and something else may say must-alias), it is
not possible to partition the memory the way every optimization wants.
Second, some alias analysis results are not transitive (IE A noalias B,
and B noalias C, does not mean A noalias C), so it is not possible to
come up with a precise partitioning in all cases without variables to
represent every pair of possible aliases.  Thus, partitioning
precisely may require introducing at least N^2 new virtual variables,
phi nodes, etc.

Each of these variables may be clobbered at multiple def sites.

To give an example, if you were to split up struct fields into
individual variables, all aliasing operations that may-def multiple struct
fields, will may-def more than one of them.  This is pretty common (calls,
copies, field stores, etc).

Experience with SSA forms for memory in other compilers has shown that
it is simply not possible to do this precisely, and in fact, doing it
precisely is not worth it, because now all the optimizations have to
walk tons and tons of virtual variables and phi nodes.

So we partition.  At the point at which you partition, again,
experience has shown us there is no point in partitioning to more than
one variable.  It simply generates more IR, and optimizations still
have to query something to disambiguate further anyway.

As a result, LLVM partitions to one variable.

Precision in practice
^^^^^^^^^^^^^^^^^^^^^

In practice, there are implementation details in LLVM that also affect the
results' precision provided by ``MemorySSA``. For example, AliasAnalysis has various
caps, or restrictions on looking through phis which can affect what ``MemorySSA``
can infer. Changes made by different passes may make MemorySSA either "overly
optimized" (it can provide a more accurate result than if it were recomputed
from scratch), or "under optimized" (it could infer more if it were recomputed).
This can lead to challenges to reproduced results in isolation with a single pass
when the result relies on the state acquired by ``MemorySSA`` due to being updated by
multiple subsequent passes.
Passes that use and update ``MemorySSA`` should do so through the APIs provided by the
``MemorySSAUpdater``, or through calls on the Walker.
Direct optimizations to ``MemorySSA`` are not permitted.
There is currently a single, narrowly scoped exception where DSE (DeadStoreElimination)
updates an optimized access of a store, after a traversal that guarantees the
optimization is correct. This is solely allowed due to the traversals and inferences
being beyond what ``MemorySSA`` does and them being "free" (i.e. DSE does them anyway).
This exception is set under a flag ("-dse-optimize-memoryssa") and can be disabled to
help reproduce optimizations in isolation.


LLVM Developers Meeting presentations
-------------------------------------

- `2016 LLVM Developers' Meeting: G. Burgess - MemorySSA in Five Minutes <https://www.youtube.com/watch?v=bdxWmryoHak>`_.
- `2020 LLVM Developers' Meeting: S. Baziotis & S. Moll - Finding Your Way Around the LLVM Dependence Analysis Zoo <https://www.youtube.com/watch?v=1e5y6WDbXCQ>`_
