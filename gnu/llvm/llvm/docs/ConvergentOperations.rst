==============================
Convergent Operation Semantics
==============================

.. contents::
   :local:
   :depth: 4

Overview
========

Some parallel execution environments execute threads in groups that allow
efficient communication within the group using special primitives called
*convergent* operations. The outcome of a convergent operation is sensitive to
the set of threads that executes it "together", i.e., convergently. When control
flow :ref:`diverges <convergence-and-uniformity>`, i.e. threads of the same
group follow different
paths through the CFG, not all threads of the group may be available to
participate in this communication. This is the defining characteristic that
distinguishes convergent operations from other inter-thread communication:

  A convergent operation involves inter-thread communication or synchronization
  that occurs outside of the memory model, where the set of threads which
  participate in communication is implicitly affected by control flow.

For example, in the following GPU compute kernel, communication during the
convergent operation is expected to occur precisely among those threads of an
implementation-defined execution scope (such as workgroup or subgroup) for
which ``condition`` is true:

.. code-block:: c++

  void example_kernel() {
      ...
      if (condition)
          convergent_operation();
      ...
  }

In structured programming languages, there is often an intuitive and
unambiguous way of determining the threads that are expected to communicate.
However, this is not always the case even in structured programming languages,
and the intuition breaks down entirely in unstructured control flow. This
document describes the formal semantics in LLVM, i.e. how to determine the set
of communicating threads for convergent operations.

The definitions in this document leave many details open, such as how groups of
threads are formed in the first place. It focuses on the questions that are
relevant for deciding the correctness of generic program transforms and
convergence-related analyses such as :ref:`uniformity analysis
<convergence-and-uniformity>`.

.. _convergent_operations:

Convergent Operations
=====================

In LLVM IR, the only way to communicate between threads as described
above is by calling target-defined convergent intrinsics. Hence, only
a call-site in LLVM IR (a :ref:`call <i_call>`, :ref:`invoke
<i_invoke>`, or :ref:`callbr <i_callbr>` instruction) can result in a
convergent operation.

A function in LLVM IR is said to be *convergent* if it has the
:ref:`convergent <attr_convergent>` attribute.

A call-site in LLVM IR is said to be *convergent* if it is a direct
call to a convergent function or it has the :ref:`convergent
<attr_convergent>` attribute or a :ref:`convergencectrl operand bundle
<convergencectrl>`.

Informational notes:

  A function may have to be treated as convergent if that function, or
  transitively, any function called from it, contains a convergent call-site. A
  frontend generating the ``convergent`` attribute should take this into account
  when emitting functions and function calls. But this is not always the case:

  A non-convergent function may contain convergent operations; such operations
  do not directly depend on the set of threads that enter the function as a
  single communicating group. Instead, these operations depend on an
  implementation-defined subset of threads within the body of the function, as
  shown in :ref:`opportunistic_convergence`.

Examples of Convergent Operations
========================================

(This section is informative.)

Texture sampling in a pixel shader
----------------------------------

The following stylized pixel shader samples a texture at a given set of
coordinates, using the builtin function `textureSample`. Texture sampling
requires screen-space derivatives of the coordinates to determine the level of
detail (mipmap level) of the sample. They are commonly approximated by taking
the difference between neighboring pixels, which are computed by different
threads in the same group:

.. code-block:: c++

  void example_shader() {
    ...
    color = textureSample(texture, coordinates);
    if (condition) {
      use(color);
    }
    ...
  }

From a purely single-threaded perspective, sinking the `textureSample` into
the if-statement appears legal. However, if the condition is false for some
neighboring pixels, then their corresponding threads will not execute together
in the group, making it impossible to take the difference of coordinates as an
approximation of the screen-space derivative. In practice, the outcome will be
an undefined value.

That is, the `textureSample` operation fits our definition of a convergent
operation:

 1. It communicates with a set of threads that implicitly depends on control
    flow.
 2. Correctness depends on this set of threads.

The compiler frontend can emit IR that expresses the convergence constraints as
follows:

.. code-block:: llvm

  define void @example_shader() convergent {
    %entry = call token @llvm.experimental.convergence.entry()
    ...
    %color = call T @textureSample(U %texture, V %coordinates) [ "convergencectrl"(token %entry) ]
    br i1 %condition, label %then, label %end

  then:
    call void @use(T %color)
    br label %end

  end:
    ret void
  }

The :ref:`llvm.experimental.convergence.entry <llvm.experimental.convergence.entry>`
intrinsic is itself ``convergent``, and we expect it to communicate at least
among all threads of the same "quad" -- a group of 2x2 pixels that are
evaluated together for the purpose of approximating screen-space derivatives.
This fact is not part of the generic LLVM IR semantics; it would have to be
defined somewhere else, for example as part of target-specific ABI definitions
and/or in reference to some relevant API specs.

Since the ``@textureSample`` call then uses the token produced by the entry
intrinsic in its ``convergencectrl`` bundle, and has no additional control
dependencies, it must communicate among the same set of threads. This indicates
to generic program transforms that sinking the ``@textureSample`` call is
forbidden. (A program transform can still sink the call if it can prove somehow,
e.g. by leaning on target-specific callbacks that can analyze the program with
additional knowledge, that ``%condition`` is always uniform across the threads
referenced by the *convergence token* ``%entry``.)

.. _convergence_example_reductions:

Reductions inside divergent control flow
----------------------------------------

The following example shows that merging common code of branches can be
incorrect in the face of convergent operations:

.. code-block:: c++

  void example_kernel() {
    delta = ...
    if (delta > 0) {
      total_gains = subgroupAdd(delta);
      ...
    } else {
      total_losses = subgroupAdd(delta);
      ...
    }
  }

The ``subgroupAdd`` computing the ``total_gains`` will be executed by the
subset of threads with positive ``delta`` in a subgroup (wave), and so will sum
up all the ``delta`` values of those threads; and similarly for the
``subgroupAdd`` that computes the ``total_losses``.

If we were to hoist and merge the ``subgroupAdd`` above the if-statement, it
would sum up the ``delta`` across *all* threads instead.

The compiler frontend can emit IR that expresses the convergence constraints
as follows:

.. code-block:: llvm

  define void @example_kernel() convergent {
    %entry = call token @llvm.experimental.convergence.entry()
    %delta = ...
    %cc = icmp sgt i32 %delta, 0
    br i1 %cc, label %then, label %else

  then:
    %total_gains = call i32 @subgroupAdd(i32 %delta) [ "convergencectrl"(token %entry) ]
    ...
    br label %end

  else:
    %total_losses = call i32 @subgroupAdd(i32 %delta) [ "convergencectrl"(token %entry) ]
    ...
    br label %end

  end:
    ...
  }

The entry intrinsic behaves like in the previous example: assuming that
``@example_kernel`` is an OpenCL kernel (as hinted at by the "subgroup"
terminology), we expect it to communicate among all threads within the
"subgroup". This typically maps to a SIMD vector on GPU hardware.

The calls to ``@subgroupAdd`` use the token produced by the entry intrinsic,
but they also have an additional control dependency. According to the rules
defined in this document, they only communicate among the subset of threads
that actually end up executing the respective (static) call site.

Hoisting them would remove the control dependency and cause them to communicate
among the full set of threads that the entry intrinsic communicated with.
Again, hoisting is allowed if it can be proven that ``%cc`` is always uniform
among the relevant set of threads: in that case, the ``@subgroupAdd`` already
communicates among the full set of threads in the original program.

Motivating Examples of Convergence Control
==========================================

(This section is informative.)

Unstructured control flow
-------------------------

Consider an example of how jump threading removes structure in a way that can
make semantics non-obvious without the convergence intrinsics described in this
document:

.. code-block:: llvm

  void example_original() {
  entry:
      ...
      br i1 %cond1, label %then1, label %mid

  then1:
      ...
      %cond2 = ...
      br label %mid

  mid:
      %flag = phi i1 [ true, %entry ], [ %cond2, %then1 ]
      br i1 %flag, label %then2, label %end

  then2:
      ...
      call void @subgroupControlBarrier()
      ...
      br label %end

  end:
  }

  void example_jumpthreaded() {
  entry:
      ...
      br i1 %cond1, label %then1, label %then2

  then1:
      ...
      %cond2 = ...
      br i1 %cond2, label %then2, label %end

  then2:
      ...
      call void @subgroupControlBarrier()
      ...
      br label %end

  end:
  }

Is the control barrier guaranteed to synchronize among the same set of threads
in both cases? Different implementations in the literature may give different
answers to this question:

* In an implementation that reconverges at post-dominators, threads reconverge
  at ``mid`` in the first version, so that all threads (within a subgroup/wave)
  that execute the control barrier do so together. In the second version,
  threads that reach the control barrier via different paths synchronize
  separately: the first (and only) post-dominator is ``end``, so threads do not
  reconverge before then.

* An implementation that sorts basic blocks topologically and ensures maximal
  reconvergence for each basic block would behave the same way in both
  versions.

We generally take the stance that reconvergence in acyclic control flow must
be maximal. The compiler frontend could augment the original code as follows:

.. code-block:: llvm

  define void @example_original() convergent {
  entry:
    %entry = call token @llvm.experimental.convergence.entry()
    ...
    br i1 %cond1, label %then1, label %mid

  then1:
    ...
    %cond2 = ...
    br label %mid

  mid:
    %flag = phi i1 [ true, %entry ], [ %cond2, %then1 ]
    br i1 %flag, label %then2, label %end

  then2:
    ...
    call void @subgroupControlBarrier() [ "convergencectrl"(token %entry) ]
    ...
    br label %end

  end:
  }

If S is the set of threads that the entry intrinsic communicated with, then
the ``@subgroupControlBarrier`` call communicates with the subset of S that
actually reaches the call site. This set of threads doesn't change after
jump-threading, so the answer to the question posed above remains the same.

.. _opportunistic_convergence:

Opportunistic convergent operations
-----------------------------------

Some programs have local regions of code that contain a sequence of convergent
operations where the code does not care about the exact set of threads with
which it is executed, but only that the set of threads is the same for all the
operations within the sequence. (If a subset of the convergent operations in the
sequence have additional, non-uniform control dependencies, then this is not
possible. However, the code may still require that the sets of threads are
logically consistent with the conditions of those control dependencies.) In this
case, :ref:`llvm.experimental.convergence.anchor
<llvm.experimental.convergence.anchor>` can be used to express the desired
semantics.

The following example function could be part of a hypothetical "append buffer"
implementation, where threads conditionally write fixed-sized records
contiguously into a global buffer. The function ``@reserveSpaceInBuffer``
returns the index into the buffer at which the calling thread should store its
data.

This could be achieved by using a simple atomic operation in every thread to
bump an allocation counter.

However, the following implementation can be more performant on some hardware,
because it uses only a single atomic operation for an entire group of threads.
To do this, it first determines the total size of the group, which will be the
operand to the atomic operation, and then later broadcasts the result of the
atomic operation to all threads of the group, so that each thread can compute
its individual position in the buffer:

.. code-block:: llvm

  define i32 @reserveSpaceInBuffer() {    ; NOTE: _not_ a convergent function!
  entry:
    %anchor = call token @llvm.experimental.convergence.anchor()

    %ballot = call i64 @subgroupBallot(i1 true) [ "convergencectrl"(token %anchor) ]
    %numThreads.p = call i64 @llvm.ctpop.i64(i64 %ballot)
    %numThreads = trunc i64 %numThreads.p to i32

    %absoluteThreadIdx = call i32 @getSubgroupLocalInvocationId()
    %absoluteThreadIdx.ext = zext i32 %absoluteThreadIdx to i64
    %mask.p = shl i64 1, %absoluteThreadIdx.ext
    %mask = sub i64 %mask.p, 1

    %maskedBallot = and i64 %ballot, %mask
    %relativeThreadIdx.p = call i64 @llvm.ctpop.i64(i64 %maskedBallot)
    %relativeThreadIdx = trunc i64 %relativeThreadIdx.p to i32

    %isFirstThread = icmp eq i32 %relativeThreadIdx, 0
    br i1 %isFirstThread, label %then, label %end

  then:
    %baseOffset.1 = atomicrmw add ptr @bufferAllocationCount, i32 %numThreads monotonic
    br label %end

  end:
    %baseOffset.2 = phi i32 [ undef, %entry ], [ %baseOffset.1, %then ]
    %baseOffset = call i32 @subgroupBroadcastFirst(i32 %baseOffset.2) [ "convergencectrl"(token %anchor) ]
    %offset = add i32 %baseOffset, %relativeThreadIdx
    ret i32 %offset
  }

The key here is that the function really doesn't care which set of threads it
is being called with. It takes whatever set of threads it can get. What the
implementation of the function cares about is that the initial
``@subgroupBallot`` -- which is used to retrieve the bitmask of threads that
executed the anchor together -- executes with the same set of threads as the
final ``@subgroupBroadcastFirst``. Nothing else is required for correctness as
far as convergence is concerned.

The function ``@reserveSpaceInBuffer`` itself is _not_ ``convergent``: callers
are free to move call sites of the function as they see fit. This can change
the behavior in practice, by changing the sets of threads that are grouped
together for the atomic operation. This can be visible in the output of the
program, since the order in which outputs appear in the buffer is changed.
However, this does not break the overall contract that ``@reserveSpaceInBuffer``
has with its caller -- which makes sense: the order of outputs is
non-deterministic anyway because of the atomic operation that is involved.

If the function is inlined, the use of the anchor intrinsic similarly indicates
that certain transforms which are usually forbidden by the presence of
convergent operations are in fact allowed, as long as they don't break up the
region of code that is controlled by the anchor.

.. _convergence_high-level_break:

Extended Cycles: Divergent Exit from a Loop
-------------------------------------------

High-level languages typically provide a ``break`` statement that transfers
control out of a loop statement. In most cases, the loop is structured and hence
there is no ambiguity about convergence inside the loop. But an ambiguity arises
when a ``break`` is control dependent on a divergent condition inside the loop.
Consider the following example:

.. code-block:: c++

  void example() {
    // A
    ...
    for (...) {
      // B
      if (condition) { // divergent condition
        // C
        convergent_op();
        break;
      }
      // D
      ...
    }
    // E
  }

In this program, the call to convergent_op() is lexically "inside" the ``for``
loop. But when translated to LLVM IR, the basic block B is an exiting block
ending in a divergent branch, and the basic block C is an exit of the loop.
Thus, the call to convergent_op() is outside the loop. This causes a mismatch
between the programmer's expectation and the compiled program. The call should
be executed convergently on every iteration of the loop, by threads that
together take the branch to exit the loop. But when compiled, all threads that
take the divergent exit on different iterations first converge at the beginning
of basic block C and then together execute the call to convergent_op().

In this case, :ref:`llvm.experimental.convergence.loop
<llvm.experimental.convergence.loop>` can be used to express the desired
semantics. A call to this intrinsic is placed in the loop header, which tracks
each iteration of the loop. The token produced by this is used as a
``convergencectrl`` operand to the convergent call. The semantics of the
``loop`` intrinsic ensures that the convergent call is performed convergently
only by those threads that convergently exited the loop in a given iteration.

.. code-block:: llvm

  define void @example() convergent {
    %entry = call token @llvm.experimental.convergence.entry()
    br label %for

  for:
    %inner = call token @llvm.experimental.convergence.loop() ["convergencectrl"(token %entry)]
    %for.cond = i1 ...
    br i1 %for.cond, label %B, label %E

  B:
    ...
    %condition = i1 ...
    br i1 %condition, label %C, label %D

  C:
    call void @convergent_op() ["convergencectrl"(token %inner)]
    br label %E

  D:
    ...
    br label %for

  E:
    ...
    ret void
  }

The LLVM IR version of the same program shows a cycle consisting of the basic
blocks ``%for``, ``%B`` and ``%D``, while ``%C`` is an exit reached by the
divergent branch at the end of the exiting block ``%B``. But the use of
convergence control tokens makes it clear that block ``%C`` must be executed
convergently only by those threads that convergently take the exit edge from %B
to ``%C``. In other words, the convergent execution of ``%C`` is governed by the
call to the :ref:`llvm.experimental.convergence.loop
<llvm.experimental.convergence.loop>` intrinsic inside the cycle. The cycle is
effectively extended to include all uses of this token that lie outside the
cycle.

.. _dynamic_instances_and_convergence_tokens:

Dynamic Instances and Convergence Tokens
========================================

Every execution of an LLVM IR instruction occurs in a :ref:`dynamic instance
<convergence-dynamic-instances>` of the instruction. Dynamic instances are the
formal objects by which we talk about communicating threads in convergent
operations. Dynamic instances are defined for *all* operations in an LLVM
program, whether convergent or not. Convergence control is primarily about the
dynamic instances of convergent operations since they affect execution of the
program through inter-thread communication. The dynamic instances for
non-convergent operations are relevant for determining :ref:`uniformity
<convergence-and-uniformity>` of values.

Dynamic instances produced by the execution of the same *convergent operation*
by different threads may be :ref:`converged <convergence-definition>`. When
executing a convergent operation, the set of threads that execute converged
dynamic instances is the set of threads that communicate with each other.
*Convergence tokens* capture this convergence as described below.

*Convergence tokens* are values of ``token`` type, i.e. they cannot be used in
``phi`` or ``select`` instructions. A convergence token value represents the
dynamic instance of the instruction that produced it.

Convergent operations may have an optional ``convergencectrl`` operand bundle with
a convergence token operand to define the set of communicating threads relative
to the operation that defined the token.

   Let ``U`` be a convergent operation other than a call to a convergence
   control intrinsic, and ``D`` be the convergent operation that defines
   the token value used as the ``convergencectrl`` operand to ``U``. Two
   threads execute converged dynamic instances of ``U`` if and only if the
   token value in both threads was returned by converged dynamic
   instances of ``D``.

.. note::

   The text defines convergence token values as representing dynamic instances.
   But if we were to assume that converged dynamic instances produce the same
   token value, then we could almost think of the token value as representing a
   set of threads instead -- specifically, the set ``S`` of threads that
   executed converged dynamic instances of the defining instruction ``D``.

   In this intuitive picture, when a convergence token value ``T`` is used by a
   ``convergencectrl`` bundle on an instruction ``I``, then the set of threads that
   communicates in ``I`` is a subset of the set ``S`` represented by the token value.
   Specifically, it is the subset of threads that ends up executing ``I`` while
   using the token value.

   This by itself wouldn't quite work as a definition: what if ``I`` is executed
   multiple times by the same threads? Which execution of ``I`` in thread 1
   communicates with which execution of ``I`` in thread 2? Leaning on the notion
   of dynamic instances gives a robust answer to this question as long as ``D``
   and ``I`` are at the same loop (or cycle) nesting level.

   The case where ``D`` and ``I`` are at different loop nesting levels is
   forbidden by the :ref:`static rules <convergence_static_rules>` -- handling
   that case is the purpose of :ref:`llvm.experimental.convergence.loop
   <llvm.experimental.convergence.loop>`.

.. _convergence_control_intrinsics:

Convergence Control Intrinsics
==============================

This section describes target-independent intrinsics that can be used to
produce convergence tokens.

Behaviour is undefined if a convergence control intrinsic is called
indirectly.

.. _llvm.experimental.convergence.entry:

``llvm.experimental.convergence.entry``
----------------------------------------

.. code-block:: llvm

  token @llvm.experimental.convergence.entry() convergent readnone

This intrinsic is used to tie the dynamic instances inside of a function to
those in the caller.

1. If the function is called from outside the scope of LLVM, the convergence of
   dynamic instances of this intrinsic are environment-defined. For example:

   a. In an OpenCL *kernel launch*, the maximal set of threads that
      can communicate outside the memory model is a *workgroup*.
      Hence, a suitable choice is to specify that all the threads from
      a single workgroup in OpenCL execute converged dynamic instances
      of this intrinsic.
   b. In a C/C++ program, threads are launched independently and they can
      communicate only through the memory model. Hence the dynamic instances of
      this intrinsic in a C/C++ program are never converged.
2. If the function is called from a call-site in LLVM IR, then two
   threads execute converged dynamic instances of this intrinsic if and
   only if both threads entered the function by executing converged
   dynamic instances of the call-site.

This intrinsic can occur at most once in a function, and only in the entry
block of the function. If this intrinsic occurs in a basic block, then it must
precede any other convergent operation in the same basic block.

It is an error if this intrinsic appears in a non-convergent function.

It is an error to specify a ``convergencectrl`` operand bundle at a
call to this intrinsic.

Function inlining substitutes this intrinsic with the token from the operand
bundle. For example:

.. code-block:: c++

  // Before inlining:

  void callee() convergent {
    %tok = call token @llvm.experimental.convergence.entry()
    convergent_operation(...) [ "convergencectrl"(token %tok) ]
  }

  void main() {
    %outer = call token @llvm.experimental.convergence.anchor()
    for (...) {
      %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
      callee() [ "convergencectrl"(token %inner) ]
    }
  }

  // After inlining:

  void main() {
    %outer = call token @llvm.experimental.convergence.anchor()
    for (...) {
      %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
      convergent_operation(...) [ "convergencectrl"(token %inner) ]
    }
  }

.. _llvm.experimental.convergence.loop:

``llvm.experimental.convergence.loop``
--------------------------------------

.. code-block:: llvm

  token @llvm.experimental.convergence.loop() [ "convergencectrl"(token) ] convergent readnone

This intrinsic represents the place where an imaginary counter is incremented
for determining convergence inside a control flow cycle.

Let ``U`` be a call to this intrinsic and ``D`` be the convergent operation that
defines the token value used as the ``convergencectrl`` operand to ``U``. Two
threads execute converged dynamic instances of ``U`` if and only if:

1. The token value in both threads was returned by converged dynamic
   instances of ``D``, and,
2. There is an integer *n* such that both threads execute ``U`` for the *n*'th time
   with that token value.

It is an error to omit the ``convergencectrl`` operand bundle on a
call to this intrinsic.

If this intrinsic occurs in a basic block, then it must precede any other
convergent operation in the same basic block.

.. _convergence_cycle_heart:

**Heart of a Cycle:**

  If a :ref:`cycle <cycle-terminology>` ``C`` contains an occurrence ``H`` of
  this intrinsic whose token operand is defined outside ``C``, then ``H`` is
  called the heart of ``C``.

  .. note::

     The static rules for cycles imply that a heart can occur only in the header
     of a natural loop. This ensures that the heart closely represents the
     intuitive notion of a loop iteration. If this restriction is relaxed, the
     resulting semantics provides a new notion of "cycle iteration" even for
     irreducible cycles. But this allows a natural loop to have a heart in a
     node other than its header, which has interesting consequences on the
     meaning of a loop iteration in terms of convergence. For now, we disallow
     this situation since its practical application is very rare.

.. _llvm.experimental.convergence.anchor:

``llvm.experimental.convergence.anchor``
----------------------------------------

.. code-block:: llvm

  token @llvm.experimental.convergence.anchor() convergent readnone

This intrinsic produces an initial convergence token that is independent from
any "outer scope". The set of threads executing converged dynamic instances of
this intrinsic is implementation-defined.

It is an error to pass a ``convergencectrl`` operand bundle at a
call to this intrinsic.

.. note::

   The expectation is that all threads within a group that "happen to be active
   at the same time" will execute converged dynamic instances, so that programs
   can detect the maximal set of threads that can communicate efficiently within
   some local region of the program.

.. _convergence_uncontrolled:

Uncontrolled Convergent Operations
==================================

Convergent operations with an explicit ``convergencectrl`` operand bundle are
called *controlled convergent operations*. All other convergent operations are
said to be *uncontrolled*.

An uncontrolled convergent operation is said to have *implicit convergence
control* determined by the ``convergent`` attribute alone. The semantics of the
``convergent`` attribute as implemented in LLVM differs from the documented
semantics. The implementation tries to follow common intuition about convergent
operations, which remains under-specified. As such, it is not possible to fully
translate implicit convergence control into explicit convergence control tokens,
and these two modes cannot be mixed in the same function.

  If a function contains a controlled convergent operation, then all convergent
  operations in that function must either be controlled operations or calls to
  the convergence control intrinsics.

Inferring Tokens
----------------

(This section is informational)

Sometimes, it may be necessary to reinterpret the implicit convergence control
in terms of explicit convergence control tokens. For example, this may happen
when a function call is inlined, and either the caller or the callee contains
uncontrolled convergent operations.

Some uses of uncontrolled convergent operations may need to satisfy the
following property:

  For an environment-defined group of threads (such as an OpenCL workgroup or
  subgroup), if one thread in the group executes a convergent operation, then
  all threads in the group do so convergently with that thread.

In terms of explicit convergence control, this means that the
``convergencectrl`` operand on each convergent operation ``X`` must ultimately
originate from a call to the :ref:`llvm.experimental.convergence.entry
<llvm.experimental.convergence.entry>` intrinsic. This preserves the possibility
that the group of threads that converge on reaching ``X`` is the same group that
originally started executing the program in convergence. In comparison, the
:ref:`llvm.experimental.convergence.anchor
<llvm.experimental.convergence.anchor>` intrinsic captures an
implementation-defined group of threads, which is insufficient to support the
above property.

One way to approximate implicit convergence control in terms of explicit
convergence control tokens is the following procedure, which preserves the above
mentioned property:

1. Convert every irreducible cycle into a reducible cycle.
2. Insert a call to :ref:`llvm.experimental.convergence.entry
   <llvm.experimental.convergence.entry>` at the start of the entry block of the
   function.
3. Insert a call to :ref:`llvm.experimental.convergence.loop
   <llvm.experimental.convergence.loop>` at the start of every loop header. If
   this loop is an outermost loop, the ``convergencectrl`` operand is the call
   to :ref:`llvm.experimental.convergence.entry
   <llvm.experimental.convergence.entry>` in the entry block of the function.
   Otherwise, the ``convergencectrl`` operand is the call to
   :ref:`llvm.experimental.convergence.loop
   <llvm.experimental.convergence.loop>` in the parent loop's header.
4. For each uncontrolled convergent operation ``X``, add a ``convergencectrl``
   operand bundle using the token defined by a definition ``D`` that is a
   :ref:`sibling <cycle-sibling>` to this operation. ``D`` always dominates
   ``X`` --- if ``X`` is not in any cycle, then ``D`` is a call to
   :ref:`llvm.experimental.convergence.entry
   <llvm.experimental.convergence.entry>`; otherwise ``D`` is the heart of the
   parent cycle of ``X``.

.. _convergence_static_rules:

Static Rules
============

A *well-formed* program in LLVM IR must satisfy the following static
rules about cycles and convergence regions.

Closed Paths
------------

A :ref:`closed path <cycle-closed-path>` in a CFG is a connected sequence of
nodes and edges in the CFG whose start and end points are the same.

1. Every closed path in the CFG that contains a use of a convergence token T other
   than a use by
   :ref:`llvm.experimental.convergence.loop <llvm.experimental.convergence.loop>`
   must also contain the definition of T.

2. Every closed path in the CFG that contains two different uses of a convergence
   token T must also contain the definition of T.

3. Every closed path in the CFG that contains uses of two different convergence tokens
   T1 and T2 must also contain the definition of at least one of them.

Taken together, these rules imply that for every closed path C, there can be at most
one convergence token T which is used in C but defined outside of it, and that
T can be used only once in C, and only by ``llvm.experimental.convergence.loop``.

4. In every closed path that contains a use U of a token T but not the
   definition of T, U must dominate all nodes in the closed path.

This implies that ``llvm.experimental.convergence.loop`` can appear as a heart
only in the header of a natural loop.

**Sufficient Conditions:** From the :ref:`properties of cycles
<cycle-closed-path>`, it is sufficient to prove the above properties
for cycles instead of closed paths. Briefly, any closed path that violates
one or more of the above static rules is contained in a cycle that also
violates the same rule(s).

.. _convergence_region:

Convergence Regions
-------------------

The *convergence region* of a convergence token T is the minimal region in
which T is live and used, i.e., the set of program points dominated by the
definition D of T from which a use of T can be reached.

The following static rule about convergence regions must be satisfied by
valid programs:

   If a convergence region R for a token T1 contains a use of a convergence
   token T2, then R must also contain the definition of T2. (In other words,
   convergence regions must be reasonably nested.)

.. note::

   For brevity, this document uses the term "convergence region of a token
   definition ``D``" to actually refer to the convergence region of the token
   ``T`` defined by ``D``.

.. _inferring_noconvergent:

Inferring non-convergence
=========================

When the target or the environment guarantees that threads do not
communicate using convergent operations or that threads never diverge,
the dynamic instances in the program are irrelevant and an optimizer
may remove any occurrence of the ``convergent`` attribute on a
call-site or a function and any explicit ``convergencectrl`` operand
bundle at a call-site.

An optimizer may remove the ``convergent`` attribute and any explicit
``convergencectrl`` operand bundle from a call-site if it can prove
that the execution of this call-site always results in a call to a
non-convergent function.

An optimizer may remove the ``convergent`` attribute on a function if it can
prove that the function does not contain a call to
:ref:`llvm.experimental.convergence.entry
<llvm.experimental.convergence.entry>`, or any uncontrolled convergent
operations.

Memory Model Non-Interaction
============================

The fact that an operation is convergent has no effect on how it is treated for
memory model purposes. In particular, an operation that is ``convergent`` and
``readnone`` does not introduce additional ordering constraints as far as the
memory model is concerned. There is no implied barrier, neither in the memory
barrier sense nor in the control barrier sense of synchronizing the execution
of threads.

Informational note: Threads that execute converged dynamic instances do not
necessarily do so at the same time.


Other Interactions
==================

A function can be both ``convergent`` and
``speculatable``, indicating that the function does not have undefined
behavior and has no effects besides calculating its result, but is still
affected by the set of threads executing this function. This typically
prevents speculation of calls to the function unless the constraint imposed
by ``convergent`` is further relaxed by some other means.

Controlled Maximal Convergence
==============================

The :ref:`converged-with relation <convergence-definition>` over dynamic
instances of each controlled convergent operation is completely defined by the
semantics of convergence tokens. But the implementation-defined convergence at a
call to :ref:`llvm.experimental.convergence.anchor
<llvm.experimental.convergence.anchor>` also depends on the cycle hierarchy
chosen if it occurs inside an irreducible cycle.

When the token defined by a convergent operation ``D`` is used at another
convergent operation ``U``, the implementation must ensure that the threads that
converge at ``U`` are all the threads that reached ``U`` after converging at
``D``. On most implementations, it is reasonable to assume that only these
threads are converged at every node they reach on any path from ``D`` to ``U``.
In other words, the converged-with relation at ``D`` produces groups of threads
that can converge only within each group, while inside the convergence region of
``D``.

All this affects the :ref:`maximal converged-with relation
<convergence-maximal>` over dynamic instances and in turn the :ref:`m-converged
property <uniformity-analysis>` of static instances in the convergence region of
``D``.

.. _controlled_maximal_converged_with:

  **Controlled Maximal converged-with Relation**

  1. Dynamic instances of a *convergent operation* are related in the controlled
     maximal converged-with relation according to the semantics of the convergence
     control tokens.
  2. Dynamic instances ``X1`` and ``X2`` produced by different threads for the
     same *non-convergent operation* ``X`` are related in the controlled maximal
     converged-with relation if and only if:

     1. Both threads executed converged dynamic instances of every token
        definition ``D`` such that ``X`` is in the convergence region of ``D``,
        and,
     2. Either ``X`` is not contained in any cycle, or, for every cycle ``C``
        with header ``H`` that contains ``X``:

        - every dynamic instance ``H1`` of ``H`` that precedes ``X1`` in the
          respective thread is convergence-before ``X2``, and,
        - every dynamic instance ``H2`` of ``H`` that precedes ``X2`` in the
          respective thread is convergence-before ``X1``,
        - without assuming that ``X1`` is converged with ``X2``.

.. _controlled_m_converged:

  **Controlled m-converged Static Instances**

  A node ``X`` in a given CFG is reported to be m-converged if and only if:

  1. For any token definition ``D`` such that ``X`` is inside the convergence region
     of ``D``, ``D`` itself is m-converged, and,
  2. Every cycle that contains ``X`` satisfies the following necessary
     conditions:

     a. Every divergent branch inside the cycle satisfies the :ref:`diverged
        entry criterion<convergence-diverged-entry>`, and,
     b. There are no :ref:`diverged paths reaching the
        cycle<convergence-diverged-outside>` from a divergent branch outside it.

Temporal Divergence at Cycle Exit
---------------------------------

When a cycle has a divergent exit, maximal convergence assumes that all threads
converge at the exit block. But if a controlled convergent operation outside the
cycle uses a token defined by an operation ``D`` inside the cycle, the
convergence region of ``D`` now extends outside the cycle. If two threads
executed converged dynamic instances of ``D`` before exiting the cycle, then
they continue to execute converged dynamic instances of nodes in the convergence
region of ``D`` outside the cycle. Thus, for a value ``V`` defined inside the
cycle, any use ``U`` of ``V`` within the convergence region of ``T`` uses the
output of converged dynamic instances of ``V``. If ``V`` is uniform, then its
use at such a ``U`` is also uniform. In other words, temporal divergence applies
only to a use of ``V`` that is outside the convergence region of ``D``.

Rationales for Static rules about cycles
========================================

(This section is informative.)

.. note::

   For convenience, we use the operator ``==`` to represent the relation
   ``converged-with`` and the operator ``!=`` to represent its negation.

Consider a loop with (incorrect!) convergence control as in the following
pseudocode:

.. code-block:: llvm

  ; WARNING: Example of incorrect convergence control!

  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    ...
    call void @convergent.op() [ "convergencectrl"(token %anchor) ]
    ...
  }

This code is forbidden by the first static rule about cycles.

A first formal argument why we have to do this is that the dynamic rule for
deciding whether two threads execute converged dynamic instances of
``@convergent.op`` leads to a logical contradiction in this code.
Assume two threads execute converged dynamic instances of the anchor
followed by two iterations of the loop. Thread 1 executes dynamic instances
I1 and I2 of ``@convergent.op``, thread 2 executes dynamic instances J1 and J2.
Using all the rules, we can deduce:

1. ``I1 != I2`` and ``J1 != J2`` by the basic rules of dynamic instances.

2. ``I1 == J1`` by the first dynamic rule about controlled convergent
   operations: both threads execute the same static instruction while using
   a convergence token value produced by converged dynamic instances of an
   instruction (the anchor).

3. ``I1 == J2`` by the same argument. Also, ``I2 == J1`` and ``I2 == J2``.

   The fact that one may be *intuitively* tempted to think of ``I1`` and ``J2``
   as being executed in different loop iterations is completely irrelevant for
   the *formal* argument. There is no mechanism in LLVM IR semantics for
   forming associations between loop iterations in different threads, *except*
   for the rules defined in this document -- and the rules in this document
   require a loop heart intrinsic for talking about loop iterations.

4. By transitivity, we have ``I1 == I2`` and ``J1 == J2``. That is a
   contradiction.

This problem goes away by inserting a loop heart intrinsic as follows, which
establishes a relationship between loop iterations across threads.

.. code-block:: llvm

  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    %loop = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %anchor) ]
    ...
    call void @convergent.op() [ "convergencectrl"(token %loop) ]
    ...
  }

In the same scenario of two threads executing converged dynamic instances of the
anchor and then two iterations of the loop, the dynamic rule about loop heart
intrinsics implies that both threads execute the converged dynamic instances of
the loop heart intrinsic in their respective first iterations and then again in
their respective second iterations of the loop.

This then implies that they execute converged dynamic instances ``I1 == J1`` of
the ``@convergent.op`` in their first iterations and then
``I2 == J2`` in their second iterations. The rule is an "if and only if" rule,
so it also implies that ``I1 != J2`` and ``I2 != J1``, because those executions
see token values of ``%loop`` originating from non-converged dynamic
instances of the loop intrinsic.

One may ask whether we could change the dynamic rule instead of adding the
static rule about cycles. That is impractical due to deeper difficulties.
Consider the following loop, again with incorrect convergence control:

.. code-block:: llvm

  ; WARNING: Example of incorrect convergence control!

  ; (A)
  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    ; (B)
    if (condition1) {
      ; (C)
      call void @convergent.op.1() [ "convergencectrl"(token %anchor) ]
    }
    ; (D)
    if (condition2) {
      ; (E)
      call void @convergent.op.2() [ "convergencectrl"(token %anchor) ]
    }
    ; (F)
  }
  ; (G)

Assume two threads execute converged dynamic instances of the anchor followed
by this sequence of basic blocks:

.. code-block:: text

  Thread 1: A B C D F B D E F G
  Thread 2: A B D E F B C D F G

That is, both threads execute two iterations of the loop, but they execute
the different convergent operations in different iterations. Without forming a
relation between loop iterations across the threads, there is no reasonable way
of defining which dynamic instances of the convergent operations should be the
same across the threads, if any.

Again, this can be addressed by adding a loop heart intrinsic, most naturally
as:

.. code-block:: llvm

  ; (A)
  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    ; (B)
    %loop = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %anchor) ]
    if (condition1) {
      ; (C)
      call void @convergent.op.1() [ "convergencectrl"(token %loop) ]
    }
    ; (D)
    if (condition2) {
      ; (E)
      call void @convergent.op.2() [ "convergencectrl"(token %loop) ]
    }
    ; (F)
  }
  ; (G)

Let ``%loop(i;j)`` be the dynamic instance of ``j``-th execution of the loop
heart intrinsic by thread ``i``, and analogously ``@op.k(i)`` and ``@op.k(i)``
the dynamic instances of the execution of ``@convergent.op.k`` by thread ``i``.
Then we have:

1. ``%loop(1;j) == %loop(2;j)`` for ``j = 1, 2`` because of the dynamic rule
   about loop heart intrinsics.

2. ``%loop(i;1) != %loop(i;2)`` for ``i = 1, 2`` because of the basic rule that
   different executions by the same thread happen in different dynamic
   instances.

3. ``@op.1(1) != @op.1(2)``, since ``@op.1(1)`` uses the token value of ``%loop``
   referring to ``%loop(1;1)`` and ``@op.1(2)`` uses that
   referring to ``%loop(2;2) == %loop(1;2)``, which is different from
   ``%loop(1;1)``.

4. Similarly, ``@op.2(1) != @op.2(2)``.

However, loop heart intrinsics could be inserted differently, at the cost
of also inserting a free-standing anchor:

.. code-block:: llvm

  ; (A)
  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    ; (B)
    if (condition1) {
      ; (C)
      %loop = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %anchor) ]
      call void @convergent.op.1() [ "convergencectrl"(token %loop) ]
    }
    ; (D)
    if (condition2) {
      ; (E)
      %free = call token @llvm.experimental.convergence.anchor()
      call void @convergent.op.2() [ "convergencectrl"(token %free) ]
    }
    ; (F)
  }
  ; (G)

This leads to the "unnatural counting of loop iterations" that is also mentioned
elsewhere. Let ``%loop(i)`` be the dynamic instance of the execution of the
loop heart intrinsic by thread ``i`` (each thread executes it only once), and
let ``@op.k(i)`` be as before. Then:

1. ``%loop(1) == %loop(2)`` because of the dynamic rule about loop heart
   intrinsics.

2. ``@op.1(1) == @op.1(2)`` because ``@op.1(i)`` uses the value of ``%loop``
   referring to ``%loop(i)``, and ``%loop(1) == %loop(2)``.

3. Whether ``@op.2(1) == @op.2(2)`` is implementation-defined because of the
   use of the ``%free`` anchor intrinsic.

   In practice, they almost certainly have to be non-converged dynamic
   instances. Consider that if an implementation strictly follows the order of
   instructions given in the program, the executions of the threads can be
   "aligned" as follows:

   .. code-block:: text

     Thread 1: A B         C D F B D E F G
     Thread 2: A B D E F B C D F         G

   So then ``@op.2(1)`` physically executes later than ``@op.2(2)`` and there
   can be no communication between the threads, which means they execute
   non-converged dynamic instances.

   That said, it is conceivable that there aren't actually any data or other
   dependencies that would enforce this execution order. In that case, a highly
   out-of-order implementation could potentially allow communication. That's
   why the rules defined in this document are silent about whether
   ``@op.2(1) == @op.2(2)`` or not.

This type of convergence control seems relatively unlikely to appear in real
programs. Its possibility is simply a logical consequence of the model.

An equivalent issue arises if the convergent operations are replaced by nested
loops with loop heart intrinsics that directly refer to ``%anchor``, hence
the variants of the static rules about cycles that apply to them:

.. code-block:: llvm

  ; WARNING: Example of incorrect convergence control!

  %anchor = call token @llvm.experimental.convergence.anchor()
  for (;;) {
    if (condition1) {
      for (;;) {
        %loop1 = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %anchor) ]
      }
    }
    if (condition2) {
      for (;;) {
        %loop2 = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %anchor) ]
      }
    }
  }

There is a cycle (closed walk in the CFG) that goes through both loop heart
intrinsics using ``%anchor`` but not through the definition of ``%anchor``,
so this code is invalid.


Examples for the Correctness of Program Transforms
==================================================

(This section is informative.)

As implied by the rules in the previous sections, program transforms are correct
with respect to convergent operations if they preserve or refine their
semantics. This means that the set of communicating threads in the transformed
program must have been possible in the original program.

Program transforms with a single-threaded focus are generally conservatively
correct if they do not sink or hoist convergent operations across a branch.
This applies even to program transforms that change the control flow graph.

For example, unrolling a loop that does not contain convergent operations
cannot break any of the guarantees required for convergent operations outside
of the loop.


Loop unrolling examples
-----------------------

We consider three kinds of loop unrolling here:

* Partial unrolling with no known trip multiple, so a "tail" is required to
  collect the remaining elements.
* Partial unrolling by a trip multiple, so no "tail" is required.
* Full unrolling, which eliminates the loop.

The first kind is forbidden when ``@llvm.experimental.convergence.loop`` is
used. We illustrate the reasoning with some examples.

First, an arbitrary loop that contains convergent operations *can* be unrolled
in all of these ways, even with "tail", if all convergent operations refer back
to an anchor inside the loop. For example (in pseudo-code):

.. code-block:: llvm

  while (counter > 0) {
    %tok = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
    counter--;
  }

This can be unrolled to:

.. code-block:: llvm

  while (counter >= 2) {
    %tok = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
    %tok = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
    counter -= 2;
  }
  while (counter > 0) {
    %tok = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
    counter--;
  }

This is likely to change the behavior of the convergent operation if there
are threads whose initial counter value is not a multiple of 2. In particular,
all threads with an odd trip count are now likely to execute the convergent
operation in their respective final iterations together because the
underlying implementation is likely to try to group as many threads together
as possible for the execution of the "tail".

This change is allowed because the anchor intrinsic has implementation-defined
convergence behavior and the loop unrolling transform is considered to be part
of the implementation. Another way of reasoning is that while the *likely*
behavior of the code has changed, the *guarantees* about its behavior have
remained the same.

If the loop contains uncontrolled convergent operations, this kind of unrolling
is forbidden.

Unrolling a loop with convergent operations that refer to tokens produced
outside the loop is forbidden when a "tail" or "remainder" would have to
be introduced. Consider:

.. code-block:: llvm

  ; (A)
  %outer = call token @llvm.experimental.convergence.anchor()
  while (counter > 0) {
    %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
    ; (B)
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    counter--;
  }
  ; (C)

To understand why unrolling is forbidden, consider two threads that execute
converged dynamic instances of the anchor and then proceed with 3 and 4 loop
iterations, respectively:

.. code-block:: text

  Thread 1: A B B B C
  Thread 2: A B B B B C

By the dynamic rule on loop heart intrinsics, these threads execute converged
dynamic instances of the loop intrinsic for the first 3 iterations, and then
thread 2 executes another dynamic instance by itself.

By the dynamic rule on general convergent operations, the threads execute
converged dynamic instances of the ``@convergent.operation`` in the first 3
iterations (that is, the dynamic instance executed by thread 1 in iteration
*n* is the same as that executed by thread 2 in iteration *n*, for *n = 1,2,3*;
the dynamic instance executed in iteration 1 is different from that in
iteration 2, etc.).

Now assume that the loop is unrolled by a factor of 2, which requires a
remainder as follows:

.. code-block:: llvm

  ; (A)
  %outer = call token @llvm.experimental.convergence.anchor()
  while (counter >= 2) {
    %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
    ; (B)
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    counter -= 2;
  }
  ; (C)
  if (counter > 0) {
    %remainder = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
    ; (D)
    call void @convergent.operation() [ "convergencectrl"(token %remainder) ]
  }
  ; (E)

First of all, note some interesting problems surrounding the loop intrinsic:

1. It is *not* duplicated inside the unrolled loop. This is to comply with
   the :ref:`convergence_static_rules`.

2. It is unclear whether the loop intrinsic ought to be duplicated in the
   remainder, or whether the final ``@convergent.operation`` in D should just
   refer to either ``%inner`` (which is possible in SSA form) or directly to
   ``%outer``. The decision made here is arbitrary and doesn't change the
   argument that follows. Ultimately, it simply doesn't matter because the
   transform is incorrect either way.

The threads now execute the following sequences of blocks:

.. code-block:: text

  Thread 1: A B C D E
  Thread 2: A B B C D E

Analogous to the argument above, they execute converged dynamic instances of the
``%inner`` intrinsic and the ``@convergent.operation`` in the first iteration
of the unrolled loop, which corresponds to the first 2 iterations of the
original loop.

However, they execute different static calls to ``@convergent.operation`` for
the 3rd iteration of the original loop. In thread 1, that iteration corresponds
to the call in the remainder, while in thread 2 it corresponds to the first
call to ``@convergent.operation`` in the unrolled loop. Therefore, they execute
non-converged dynamic instances, which means that the set of communicating threads
for the 3rd iteration of the original loop is different. This is why the
unrolling is incorrect.

On the other hand, unrolling without "tail" is allowed. For example, assuming
that the trip counter is known to be a multiple of 2, we can unroll the loop
as follows:

.. code-block:: llvm

  %outer = call token @llvm.experimental.convergence.anchor()
  while (counter > 0) {
    %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    counter -= 2;
  }

Note again that the loop intrinsic is not duplicated.

The
:ref:`llvm.experimental.convergence.loop <llvm.experimental.convergence.loop>`
intrinsic is typically expected to appear in the header of a natural loop.
However, it can also appear in non-header blocks of a loop. In that case, the
loop can generally not be unrolled.


Hoisting and sinking
--------------------

In general, hoisting and sinking of convergent operations is forbidden. This is
because moving the operation to a different point in control flow generally
changes the set of threads that reach the operation and therefore, the set of
threads that execute converged dynamic instances of the operation. By
definition, this changes the set of threads that participate in the
communication of the convergent operation, which will typically change its
result.

There are a number of exceptions, though most of them require additional
knowledge.

For example, hoisting and sinking across *uniform* conditional branches -- i.e.,
conditional branches where within every possible relevant set of threads, all
threads will always take the same direction -- is generally allowed. See the end
of the :ref:`example of reductions inside control flow
<convergence_example_reductions>` for a brief discussion.

Some convergent operations can be hoisted but not sunk, or vice versa. A simple
example is the ``subgroupShuffle(data, id)`` operation. It returns the ``data``
operand of the thread identified by ``id``, where thread IDs are fixed and
assigned to each thread at launch. The result is undefined (or perhaps there is
UB, depending on the language and environment) if thread ``id`` is not in the
communicating set of threads. So hoisting is allowed in the following
pseudo-code example:

.. code-block:: llvm

  define void @example(...) convergent {
    %entry = call token @llvm.experimental.convergence.entry()
    %data = ...
    %id = ...
    if (condition) {
      %shuffled = call i32 @subgroupShuffle(i32 %data, i32 %id) [ "convergencectrl"(token %entry) ]
      ...
    } else {
      %shuffled = call i32 @subgroupShuffle(i32 %data, i32 %id) [ "convergencectrl"(token %entry) ]
      ...
    }
  }

After hoisting the calls to ``@subgroupShuffle``, the communicating set of
threads is the union of the two sets of threads in the original program, so
``%id`` can only go "out of range" after hoisting if it did so in the original
program.

However, speculative execution of ``@subgroupShuffle`` in the following program
may be forbidden:

.. code-block:: llvm

  define void @example(...) convergent {
    %entry = call token @llvm.experimental.convergence.entry()
    %data = ...
    %id = ...
    if (condition) {
      %shuffled = call i32 @subgroupShuffle(i32 %data, i32 %id) [ "convergencectrl"(token %entry) ]
      ...
    }
  }

There is no guarantee about the value of ``%id`` in the threads where
``condition`` is false. If ``@subgroupShuffle`` is defined to have UB when
``%id`` is outside of the set of communicating threads, then speculating and
hoisting ``@subgroupShuffle`` might introduce UB.

On the other hand, if ``@subgroupShuffle`` is defined such that it merely
produces an undefined value or poison as result when ``%id`` is "out of range",
then speculating is okay.

Even though
:ref:`llvm.experimental.convergence.anchor <llvm.experimental.convergence.anchor>`
is marked as ``convergent``, it can be sunk in some cases. For example, in
pseudo-code:

.. code-block:: llvm

  %tok = call token @llvm.experimental.convergence.anchor()
  if (condition) {
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
  }

Assuming that ``%tok`` is only used inside the conditional block, the anchor can
be sunk. The rationale is two-fold. First, the anchor has implementation-defined
behavior, and the sinking is part of the implementation. Second, already in the
original program, the set of threads that communicates in the
``@convergent.operation`` is automatically subset to the threads for which
``condition`` is true.

Anchors can be hoisted in acyclic control flow. For example:

.. code-block:: llvm

  if (condition) {
    %tok1 = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok1) ]
  } else {
    %tok2 = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok2) ]
  }

The anchors can be hoisted, resulting in:

.. code-block:: llvm

  %tok = call token @llvm.experimental.convergence.anchor()
  if (condition) {
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
  } else {
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
  }

The behavior is unchanged, since each of the static convergent operations only
ever communicates with threads that have the same ``condition`` value.
By contrast, hoisting the convergent operations themselves is forbidden.

Hoisting and sinking anchors out of and into loops is forbidden. For example:

.. code-block:: llvm

  for (;;) {
    %tok = call token @llvm.experimental.convergence.anchor()
    call void @convergent.operation() [ "convergencectrl"(token %tok) ]
  }

Hoisting the anchor would make the program invalid according to the static
validity rules. Conversely:

.. code-block:: llvm

  %outer = call token @llvm.experimental.convergence.anchor()
  while (counter > 0) {
    %inner = call token @llvm.experimental.convergence.loop() [ "convergencectrl"(token %outer) ]
    call void @convergent.operation() [ "convergencectrl"(token %inner) ]
    counter--;
  }

The program would stay valid if the anchor was sunk into the loop, but its
behavior could end up being different. If the anchor is inside the loop, then
each loop iteration has a new dynamic instance of the anchor, and the set of
threads participating in those dynamic instances of the anchor could be
different in arbitrary implementation-defined ways. Via the dynamic rules about
dynamic instances of convergent operations, this then implies that the set of
threads executing ``@convergent.operation`` could be different in each loop
iteration in arbitrary implementation-defined ways.

Convergent operations can be sunk together with their anchor. Again in
pseudo-code:

.. code-block:: llvm

  %tok = call token @llvm.experimental.convergence.anchor()
  %a = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
  %b = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
  if (condition) {
    use(%a, %b)
  }

Assuming that ``%tok``, ``%a``, and ``%b`` are only used inside the conditional
block, all can be sunk together:

.. code-block:: llvm

  if (condition) {
    %tok = call token @llvm.experimental.convergence.anchor()
    %a = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
    %b = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
    use(%a, %b)
  }

The rationale is that the anchor intrinsic has implementation-defined behavior,
and the sinking transform is considered to be part of the implementation:
the sinking will restrict the set of communicating threads to those for which
``condition`` is true, but that could have happened in the original program
anyway for some arbitrary other reason.

However, sinking *only* the convergent operation producing ``%b`` would be
incorrect. That would allow threads for which ``condition`` is false to
communicate at ``%a``, but not at ``%b``, which the original program doesn't
allow.

Note that the entry intrinsic behaves differently. Sinking the convergent
operations is forbidden in the following snippet:

.. code-block:: llvm

  %tok = call token @llvm.experimental.convergence.entry()
  %a = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
  %b = call T @pure.convergent.operation(...) [ "convergencectrl"(token %tok) ]
  if (condition) {
    use(%a, %b)
  }
