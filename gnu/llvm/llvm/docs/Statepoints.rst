=====================================
Garbage Collection Safepoints in LLVM
=====================================

.. contents::
   :local:
   :depth: 2

Status
=======

This document describes a set of extensions to LLVM to support garbage
collection.  By now, these mechanisms are well proven with commercial java
implementation with a fully relocating collector having shipped using them.
There are a couple places where bugs might still linger; these are called out
below.

They are still listed as "experimental" to indicate that no forward or backward
compatibility guarantees are offered across versions.  If your use case is such
that you need some form of forward compatibility guarantee, please raise the
issue on the llvm-dev mailing list.

LLVM still supports an alternate mechanism for conservative garbage collection
support using the ``gcroot`` intrinsic.  The ``gcroot`` mechanism is mostly of
historical interest at this point with one exception - its implementation of
shadow stacks has been used successfully by a number of language frontends and
is still supported.

Overview & Core Concepts
========================

To collect dead objects, garbage collectors must be able to identify
any references to objects contained within executing code, and,
depending on the collector, potentially update them.  The collector
does not need this information at all points in code - that would make
the problem much harder - but only at well-defined points in the
execution known as 'safepoints' For most collectors, it is sufficient
to track at least one copy of each unique pointer value.  However, for
a collector which wishes to relocate objects directly reachable from
running code, a higher standard is required.

One additional challenge is that the compiler may compute intermediate
results ("derived pointers") which point outside of the allocation or
even into the middle of another allocation.  The eventual use of this
intermediate value must yield an address within the bounds of the
allocation, but such "exterior derived pointers" may be visible to the
collector.  Given this, a garbage collector can not safely rely on the
runtime value of an address to indicate the object it is associated
with.  If the garbage collector wishes to move any object, the
compiler must provide a mapping, for each pointer, to an indication of
its allocation.

To simplify the interaction between a collector and the compiled code,
most garbage collectors are organized in terms of three abstractions:
load barriers, store barriers, and safepoints.

#. A load barrier is a bit of code executed immediately after the
   machine load instruction, but before any use of the value loaded.
   Depending on the collector, such a barrier may be needed for all
   loads, merely loads of a particular type (in the original source
   language), or none at all.

#. Analogously, a store barrier is a code fragment that runs
   immediately before the machine store instruction, but after the
   computation of the value stored.  The most common use of a store
   barrier is to update a 'card table' in a generational garbage
   collector.

#. A safepoint is a location at which pointers visible to the compiled
   code (i.e. currently in registers or on the stack) are allowed to
   change.  After the safepoint completes, the actual pointer value
   may differ, but the 'object' (as seen by the source language)
   pointed to will not.

  Note that the term 'safepoint' is somewhat overloaded.  It refers to
  both the location at which the machine state is parsable and the
  coordination protocol involved in bring application threads to a
  point at which the collector can safely use that information.  The
  term "statepoint" as used in this document refers exclusively to the
  former.

This document focuses on the last item - compiler support for
safepoints in generated code.  We will assume that an outside
mechanism has decided where to place safepoints.  From our
perspective, all safepoints will be function calls.  To support
relocation of objects directly reachable from values in compiled code,
the collector must be able to:

#. identify every copy of a pointer (including copies introduced by
   the compiler itself) at the safepoint,
#. identify which object each pointer relates to, and
#. potentially update each of those copies.

This document describes the mechanism by which an LLVM based compiler
can provide this information to a language runtime/collector, and
ensure that all pointers can be read and updated if desired.

Abstract Machine Model
^^^^^^^^^^^^^^^^^^^^^^^

At a high level, LLVM has been extended to support compiling to an abstract
machine which extends the actual target with a non-integral pointer type
suitable for representing a garbage collected reference to an object.  In
particular, such non-integral pointer type have no defined mapping to an
integer representation.  This semantic quirk allows the runtime to pick a
integer mapping for each point in the program allowing relocations of objects
without visible effects.

This high level abstract machine model is used for most of the optimizer.  As
a result, transform passes do not need to be extended to look through explicit
relocation sequence.  Before starting code generation, we switch
representations to an explicit form.  The exact location chosen for lowering
is an implementation detail.

Note that most of the value of the abstract machine model comes for collectors
which need to model potentially relocatable objects.  For a compiler which
supports only a non-relocating collector, you may wish to consider starting
with the fully explicit form.

Warning: There is one currently known semantic hole in the definition of
non-integral pointers which has not been addressed upstream.  To work around
this, you need to disable speculation of loads unless the memory type
(non-integral pointer vs anything else) is known to unchanged.  That is, it is
not safe to speculate a load if doing causes a non-integral pointer value to
be loaded as any other type or vice versa.  In practice, this restriction is
well isolated to isSafeToSpeculate in ValueTracking.cpp.

Explicit Representation
^^^^^^^^^^^^^^^^^^^^^^^

A frontend could directly generate this low level explicit form, but
doing so may inhibit optimization.  Instead, it is recommended that
compilers with relocating collectors target the abstract machine model just
described.

The heart of the explicit approach is to construct (or rewrite) the IR in a
manner where the possible updates performed by the garbage collector are
explicitly visible in the IR.  Doing so requires that we:

#. create a new SSA value for each potentially relocated pointer, and
   ensure that no uses of the original (non relocated) value is
   reachable after the safepoint,
#. specify the relocation in a way which is opaque to the compiler to
   ensure that the optimizer can not introduce new uses of an
   unrelocated value after a statepoint. This prevents the optimizer
   from performing unsound optimizations.
#. recording a mapping of live pointers (and the allocation they're
   associated with) for each statepoint.

At the most abstract level, inserting a safepoint can be thought of as
replacing a call instruction with a call to a multiple return value
function which both calls the original target of the call, returns
its result, and returns updated values for any live pointers to
garbage collected objects.

  Note that the task of identifying all live pointers to garbage
  collected values, transforming the IR to expose a pointer giving the
  base object for every such live pointer, and inserting all the
  intrinsics correctly is explicitly out of scope for this document.
  The recommended approach is to use the :ref:`utility passes
  <statepoint-utilities>` described below.

This abstract function call is concretely represented by a sequence of
intrinsic calls known collectively as a "statepoint relocation sequence".

Let's consider a simple call in LLVM IR:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    call void ()* @foo()
    ret i8 addrspace(1)* %obj
  }

Depending on our language we may need to allow a safepoint during the execution
of ``foo``. If so, we need to let the collector update local values in the
current frame.  If we don't, we'll be accessing a potential invalid reference
once we eventually return from the call.

In this example, we need to relocate the SSA value ``%obj``.  Since we can't
actually change the value in the SSA value ``%obj``, we need to introduce a new
SSA value ``%obj.relocated`` which represents the potentially changed value of
``%obj`` after the safepoint and update any following uses appropriately.  The
resulting relocation sequence is:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    %0 = call token (i64, i32, void ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @foo, i32 0, i32 0, i32 0, i32 0, i8 addrspace(1)* %obj)
    %obj.relocated = call coldcc i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %0, i32 7, i32 7)
    ret i8 addrspace(1)* %obj.relocated
  }

Ideally, this sequence would have been represented as a M argument, N
return value function (where M is the number of values being
relocated + the original call arguments and N is the original return
value + each relocated value), but LLVM does not easily support such a
representation.

Instead, the statepoint intrinsic marks the actual site of the
safepoint or statepoint.  The statepoint returns a token value (which
exists only at compile time).  To get back the original return value
of the call, we use the ``gc.result`` intrinsic.  To get the relocation
of each pointer in turn, we use the ``gc.relocate`` intrinsic with the
appropriate index.  Note that both the ``gc.relocate`` and ``gc.result`` are
tied to the statepoint.  The combination forms a "statepoint relocation
sequence" and represents the entirety of a parseable call or 'statepoint'.

When lowered, this example would generate the following x86 assembly:

.. code-block:: gas

	  .globl	test1
	  .align	16, 0x90
	  pushq	%rax
	  callq	foo
  .Ltmp1:
	  movq	(%rsp), %rax  # This load is redundant (oops!)
	  popq	%rdx
	  retq

Each of the potentially relocated values has been spilled to the
stack, and a record of that location has been recorded to the
:ref:`Stack Map section <stackmap-section>`.  If the garbage collector
needs to update any of these pointers during the call, it knows
exactly what to change.

The relevant parts of the StackMap section for our example are:

.. code-block:: gas

  # This describes the call site
  # Stack Maps: callsite 2882400000
	  .quad	2882400000
	  .long	.Ltmp1-test1
	  .short	0
  # .. 8 entries skipped ..
  # This entry describes the spill slot which is directly addressable
  # off RSP with offset 0.  Given the value was spilled with a pushq,
  # that makes sense.
  # Stack Maps:   Loc 8: Direct RSP     [encoding: .byte 2, .byte 8, .short 7, .int 0]
	  .byte	2
	  .byte	8
	  .short	7
	  .long	0

This example was taken from the tests for the :ref:`RewriteStatepointsForGC`
utility pass.  As such, its full StackMap can be easily examined with the
following command.

.. code-block:: bash

  opt -rewrite-statepoints-for-gc test/Transforms/RewriteStatepointsForGC/basics.ll -S | llc -debug-only=stackmaps

Simplifications for Non-Relocating GCs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Some of the complexity in the previous example is unnecessary for a
non-relocating collector.  While a non-relocating collector still needs the
information about which location contain live references, it doesn't need to
represent explicit relocations.  As such, the previously described explicit
lowering can be simplified to remove all of the ``gc.relocate`` intrinsic
calls and leave uses in terms of the original reference value.

Here's the explicit lowering for the previous example for a non-relocating
collector:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    call token (i64, i32, void ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @foo, i32 0, i32 0, i32 0, i32 0, i8 addrspace(1)* %obj)
    ret i8 addrspace(1)* %obj
  }

Recording On Stack Regions
^^^^^^^^^^^^^^^^^^^^^^^^^^

In addition to the explicit relocation form previously described, the
statepoint infrastructure also allows the listing of allocas within the gc
pointer list.  Allocas can be listed with or without additional explicit gc
pointer values and relocations.

An alloca in the gc region of the statepoint operand list will cause the
address of the stack region to be listed in the stackmap for the statepoint.

This mechanism can be used to describe explicit spill slots if desired.  It
then becomes the generator's responsibility to ensure that values are
spill/filled to/from the alloca as needed on either side of the safepoint.
Note that there is no way to indicate a corresponding base pointer for such
an explicitly specified spill slot, so usage is restricted to values for
which the associated collector can derive the object base from the pointer
itself.

This mechanism can be used to describe on stack objects containing
references provided that the collector can map from the location on the
stack to a heap map describing the internal layout of the references the
collector needs to process.

WARNING: At the moment, this alternate form is not well exercised.  It is
recommended to use this with caution and expect to have to fix a few bugs.
In particular, the RewriteStatepointsForGC utility pass does not do
anything for allocas today.

Base & Derived Pointers
^^^^^^^^^^^^^^^^^^^^^^^

A "base pointer" is one which points to the starting address of an allocation
(object).  A "derived pointer" is one which is offset from a base pointer by
some amount.  When relocating objects, a garbage collector needs to be able
to relocate each derived pointer associated with an allocation to the same
offset from the new address.

"Interior derived pointers" remain within the bounds of the allocation
they're associated with.  As a result, the base object can be found at
runtime provided the bounds of allocations are known to the runtime system.

"Exterior derived pointers" are outside the bounds of the associated object;
they may even fall within *another* allocations address range.  As a result,
there is no way for a garbage collector to determine which allocation they
are associated with at runtime and compiler support is needed.

The ``gc.relocate`` intrinsic supports an explicit operand for describing the
allocation associated with a derived pointer.  This operand is frequently
referred to as the base operand, but does not strictly speaking have to be
a base pointer, but it does need to lie within the bounds of the associated
allocation.  Some collectors may require that the operand be an actual base
pointer rather than merely an internal derived pointer. Note that during
lowering both the base and derived pointer operands are required to be live
over the associated call safepoint even if the base is otherwise unused
afterwards.

If we extend our previous example to include a pointless derived pointer,
we get:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    %gep = getelementptr i8, i8 addrspace(1)* %obj, i64 20000
    %token = call token (i64, i32, void ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @foo, i32 0, i32 0, i32 0, i32 0, i8 addrspace(1)* %obj, i8 addrspace(1)* %gep)
    %obj.relocated = call i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %token, i32 7, i32 7)
    %gep.relocated = call i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %token, i32 7, i32 8)
    %p = getelementptr i8, i8 addrspace(1)* %gep, i64 -20000
    ret i8 addrspace(1)* %p
  }

Note that in this example %p and %obj.relocate are the same address and we
could replace one with the other, potentially removing the derived pointer
from the live set at the safepoint entirely.

.. _gc_transition_args:

GC Transitions
^^^^^^^^^^^^^^^^^^

As a practical consideration, many garbage-collected systems allow code that is
collector-aware ("managed code") to call code that is not collector-aware
("unmanaged code"). It is common that such calls must also be safepoints, since
it is desirable to allow the collector to run during the execution of
unmanaged code. Furthermore, it is common that coordinating the transition from
managed to unmanaged code requires extra code generation at the call site to
inform the collector of the transition. In order to support these needs, a
statepoint may be marked as a GC transition, and data that is necessary to
perform the transition (if any) may be provided as additional arguments to the
statepoint.

  Note that although in many cases statepoints may be inferred to be GC
  transitions based on the function symbols involved (e.g. a call from a
  function with GC strategy "foo" to a function with GC strategy "bar"),
  indirect calls that are also GC transitions must also be supported. This
  requirement is the driving force behind the decision to require that GC
  transitions are explicitly marked.

Let's revisit the sample given above, this time treating the call to ``@foo``
as a GC transition. Depending on our target, the transition code may need to
access some extra state in order to inform the collector of the transition.
Let's assume a hypothetical GC--somewhat unimaginatively named "hypothetical-gc"
--that requires that a TLS variable must be written to before and after a call
to unmanaged code. The resulting relocation sequence is:

.. code-block:: llvm

  @flag = thread_local global i32 0, align 4

  define i8 addrspace(1)* @test1(i8 addrspace(1) *%obj)
         gc "hypothetical-gc" {

    %0 = call token (i64, i32, void ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @foo, i32 0, i32 1, i32* @Flag, i32 0, i8 addrspace(1)* %obj)
    %obj.relocated = call coldcc i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %0, i32 7, i32 7)
    ret i8 addrspace(1)* %obj.relocated
  }

During lowering, this will result in an instruction selection DAG that looks
something like:

::

  CALLSEQ_START
  ...
  GC_TRANSITION_START (lowered i32 *@Flag), SRCVALUE i32* Flag
  STATEPOINT
  GC_TRANSITION_END (lowered i32 *@Flag), SRCVALUE i32 *Flag
  ...
  CALLSEQ_END

In order to generate the necessary transition code, the backend for each target
supported by "hypothetical-gc" must be modified to lower ``GC_TRANSITION_START``
and ``GC_TRANSITION_END`` nodes appropriately when the "hypothetical-gc"
strategy is in use for a particular function. Assuming that such lowering has
been added for X86, the generated assembly would be:

.. code-block:: gas

	  .globl	test1
	  .align	16, 0x90
	  pushq	%rax
	  movl $1, %fs:Flag@TPOFF
	  callq	foo
	  movl $0, %fs:Flag@TPOFF
  .Ltmp1:
	  movq	(%rsp), %rax  # This load is redundant (oops!)
	  popq	%rdx
	  retq

Note that the design as presented above is not fully implemented: in particular,
strategy-specific lowering is not present, and all GC transitions are emitted as
as single no-op before and after the call instruction. These no-ops are often
removed by the backend during dead machine instruction elimination.

Before the abstract machine model is lowered to the explicit statepoint model
of relocations by the :ref:`RewriteStatepointsForGC` pass it is possible for
any derived pointer to get its base pointer and offset from the base pointer
by using the ``gc.get.pointer.base`` and the ``gc.get.pointer.offset``
intrinsics respectively. These intrinsics are inlined by the
:ref:`RewriteStatepointsForGC` pass and must not be used after this pass.


.. _statepoint-stackmap-format:

Stack Map Format
================

Locations for each pointer value which may need read and/or updated by
the runtime or collector are provided in a separate section of the
generated object file as specified in the PatchPoint documentation.
This special section is encoded per the
:ref:`Stack Map format <stackmap-format>`.

The general expectation is that a JIT compiler will parse and discard this
format; it is not particularly memory efficient.  If you need an alternate
format (e.g. for an ahead of time compiler), see discussion under
:ref: `open work items <OpenWork>` below.

Each statepoint generates the following Locations:

* Constant which describes the calling convention of the call target. This
  constant is a valid :ref:`calling convention identifier <callingconv>` for
  the version of LLVM used to generate the stackmap. No additional compatibility
  guarantees are made for this constant over what LLVM provides elsewhere w.r.t.
  these identifiers.
* Constant which describes the flags passed to the statepoint intrinsic
* Constant which describes number of following deopt *Locations* (not
  operands).  Will be 0 if no "deopt" bundle is provided.
* Variable number of Locations, one for each deopt parameter listed in the
  "deopt" operand bundle.  At the moment, only deopt parameters with a bitwidth
  of 64 bits or less are supported.  Values of a type larger than 64 bits can be
  specified and reported only if a) the value is constant at the call site, and
  b) the constant can be represented with less than 64 bits (assuming zero
  extension to the original bitwidth).
* Variable number of relocation records, each of which consists of
  exactly two Locations.  Relocation records are described in detail
  below.

Each relocation record provides sufficient information for a collector to
relocate one or more derived pointers.  Each record consists of a pair of
Locations.  The second element in the record represents the pointer (or
pointers) which need updated.  The first element in the record provides a
pointer to the base of the object with which the pointer(s) being relocated is
associated.  This information is required for handling generalized derived
pointers since a pointer may be outside the bounds of the original allocation,
but still needs to be relocated with the allocation.  Additionally:

* It is guaranteed that the base pointer must also appear explicitly as a
  relocation pair if used after the statepoint.
* There may be fewer relocation records then gc parameters in the IR
  statepoint. Each *unique* pair will occur at least once; duplicates
  are possible.
* The Locations within each record may either be of pointer size or a
  multiple of pointer size.  In the later case, the record must be
  interpreted as describing a sequence of pointers and their corresponding
  base pointers. If the Location is of size N x sizeof(pointer), then
  there will be N records of one pointer each contained within the Location.
  Both Locations in a pair can be assumed to be of the same size.

Note that the Locations used in each section may describe the same
physical location.  e.g. A stack slot may appear as a deopt location,
a gc base pointer, and a gc derived pointer.

The LiveOut section of the StkMapRecord will be empty for a statepoint
record.

Safepoint Semantics & Verification
==================================

The fundamental correctness property for the compiled code's
correctness w.r.t. the garbage collector is a dynamic one.  It must be
the case that there is no dynamic trace such that an operation
involving a potentially relocated pointer is observably-after a
safepoint which could relocate it.  'observably-after' is this usage
means that an outside observer could observe this sequence of events
in a way which precludes the operation being performed before the
safepoint.

To understand why this 'observable-after' property is required,
consider a null comparison performed on the original copy of a
relocated pointer.  Assuming that control flow follows the safepoint,
there is no way to observe externally whether the null comparison is
performed before or after the safepoint.  (Remember, the original
Value is unmodified by the safepoint.)  The compiler is free to make
either scheduling choice.

The actual correctness property implemented is slightly stronger than
this.  We require that there be no *static path* on which a
potentially relocated pointer is 'observably-after' it may have been
relocated.  This is slightly stronger than is strictly necessary (and
thus may disallow some otherwise valid programs), but greatly
simplifies reasoning about correctness of the compiled code.

By construction, this property will be upheld by the optimizer if
correctly established in the source IR.  This is a key invariant of
the design.

The existing IR Verifier pass has been extended to check most of the
local restrictions on the intrinsics mentioned in their respective
documentation.  The current implementation in LLVM does not check the
key relocation invariant, but this is ongoing work on developing such
a verifier.  Please ask on llvm-dev if you're interested in
experimenting with the current version.

.. _statepoint-utilities:

Utility Passes for Safepoint Insertion
======================================

.. _RewriteStatepointsForGC:

RewriteStatepointsForGC
^^^^^^^^^^^^^^^^^^^^^^^^

The pass RewriteStatepointsForGC transforms a function's IR to lower from the
abstract machine model described above to the explicit statepoint model of
relocations.  To do this, it replaces all calls or invokes of functions which
might contain a safepoint poll with a ``gc.statepoint`` and associated full
relocation sequence, including all required ``gc.relocates``.

This pass only applies to GCStrategy instances where the ``UseRS4GC`` flag
is set. The two builtin GC strategies with this set are the
"statepoint-example" and "coreclr" strategies.

As an example, given this code:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    call void @foo()
    ret i8 addrspace(1)* %obj
  }

The pass would produce this IR:

.. code-block:: llvm

  define i8 addrspace(1)* @test1(i8 addrspace(1)* %obj)
         gc "statepoint-example" {
    %0 = call token (i64, i32, void ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 2882400000, i32 0, void ()* @foo, i32 0, i32 0, i32 0, i32 5, i32 0, i32 -1, i32 0, i32 0, i32 0, i8 addrspace(1)* %obj)
    %obj.relocated = call coldcc i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %0, i32 12, i32 12)
    ret i8 addrspace(1)* %obj.relocated
  }

In the above examples, the addrspace(1) marker on the pointers is the mechanism
that the ``statepoint-example`` GC strategy uses to distinguish references from
non references.  This is controlled via GCStrategy::isGCManagedPointer. The
``statepoint-example`` and ``coreclr`` strategies (the only two default
strategies that support statepoints) both use addrspace(1) to determine which
pointers are references, however custom strategies don't have to follow this
convention.

This pass can be used an utility function by a language frontend that doesn't
want to manually reason about liveness, base pointers, or relocation when
constructing IR.  As currently implemented, RewriteStatepointsForGC must be
run after SSA construction (i.e. mem2ref).

RewriteStatepointsForGC will ensure that appropriate base pointers are listed
for every relocation created.  It will do so by duplicating code as needed to
propagate the base pointer associated with each pointer being relocated to
the appropriate safepoints.  The implementation assumes that the following
IR constructs produce base pointers: loads from the heap, addresses of global
variables, function arguments, function return values. Constant pointers (such
as null) are also assumed to be base pointers.  In practice, this constraint
can be relaxed to producing interior derived pointers provided the target
collector can find the associated allocation from an arbitrary interior
derived pointer.

By default RewriteStatepointsForGC passes in ``0xABCDEF00`` as the statepoint
ID and ``0`` as the number of patchable bytes to the newly constructed
``gc.statepoint``.  These values can be configured on a per-callsite
basis using the attributes ``"statepoint-id"`` and
``"statepoint-num-patch-bytes"``.  If a call site is marked with a
``"statepoint-id"`` function attribute and its value is a positive
integer (represented as a string), then that value is used as the ID
of the newly constructed ``gc.statepoint``.  If a call site is marked
with a ``"statepoint-num-patch-bytes"`` function attribute and its
value is a positive integer, then that value is used as the 'num patch
bytes' parameter of the newly constructed ``gc.statepoint``.  The
``"statepoint-id"`` and ``"statepoint-num-patch-bytes"`` attributes
are not propagated to the ``gc.statepoint`` call or invoke if they
could be successfully parsed.

In practice, RewriteStatepointsForGC should be run much later in the pass
pipeline, after most optimization is already done.  This helps to improve
the quality of the generated code when compiled with garbage collection support.

.. _RewriteStatepointsForGC_intrinsic_lowering:

RewriteStatepointsForGC intrinsic lowering
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As a part of lowering to the explicit model of relocations
RewriteStatepointsForGC performs GC specific lowering for the following
intrinsics:

* ``gc.get.pointer.base``
* ``gc.get.pointer.offset``
* ``llvm.memcpy.element.unordered.atomic.*``
* ``llvm.memmove.element.unordered.atomic.*``

There are two possible lowerings for the memcpy and memmove operations:
GC leaf lowering and GC parseable lowering. If a call is explicitly marked with
"gc-leaf-function" attribute the call is lowered to a GC leaf call to
'``__llvm_memcpy_element_unordered_atomic_*``' or
'``__llvm_memmove_element_unordered_atomic_*``' symbol. Such a call can not
take a safepoint. Otherwise, the call is made GC parseable by wrapping the
call into a statepoint. This makes it possible to take a safepoint during
copy operation. Note that a GC parseable copy operation is not required to
take a safepoint. For example, a short copy operation may be performed without
taking a safepoint.

GC parseable calls to '``llvm.memcpy.element.unordered.atomic.*``',
'``llvm.memmove.element.unordered.atomic.*``' intrinsics are lowered to calls
to '``__llvm_memcpy_element_unordered_atomic_safepoint_*``',
'``__llvm_memmove_element_unordered_atomic_safepoint_*``' symbols respectively.
This way the runtime can provide implementations of copy operations with and
without safepoints.

GC parseable lowering also involves adjusting the arguments for the call.
Memcpy and memmove intrinsics take derived pointers as source and destination
arguments. If a copy operation takes a safepoint it might need to relocate the
underlying source and destination objects. This requires the corresponding base
pointers to be available in the copy operation. In order to make the base
pointers available RewriteStatepointsForGC replaces derived pointers with base
pointer and offset pairs. For example:

.. code-block:: llvm

  declare void @__llvm_memcpy_element_unordered_atomic_safepoint_1(
    i8 addrspace(1)*  %dest_base, i64 %dest_offset,
    i8 addrspace(1)*  %src_base, i64 %src_offset,
    i64 %length)


.. _PlaceSafepoints:

PlaceSafepoints
^^^^^^^^^^^^^^^^

The pass PlaceSafepoints inserts safepoint polls sufficient to ensure running
code checks for a safepoint request on a timely manner. This pass is expected
to be run before RewriteStatepointsForGC and thus does not produce full
relocation sequences.

As an example, given input IR of the following:

.. code-block:: llvm

  define void @test() gc "statepoint-example" {
    call void @foo()
    ret void
  }

  declare void @do_safepoint()
  define void @gc.safepoint_poll() {
    call void @do_safepoint()
    ret void
  }


This pass would produce the following IR:

.. code-block:: llvm

  define void @test() gc "statepoint-example" {
    call void @do_safepoint()
    call void @foo()
    ret void
  }

In this case, we've added an (unconditional) entry safepoint poll.  Note that
despite appearances, the entry poll is not necessarily redundant.  We'd have to
know that ``foo`` and ``test`` were not mutually recursive for the poll to be
redundant.  In practice, you'd probably want to your poll definition to contain
a conditional branch of some form.

At the moment, PlaceSafepoints can insert safepoint polls at method entry and
loop backedges locations.  Extending this to work with return polls would be
straight forward if desired.

PlaceSafepoints includes a number of optimizations to avoid placing safepoint
polls at particular sites unless needed to ensure timely execution of a poll
under normal conditions.  PlaceSafepoints does not attempt to ensure timely
execution of a poll under worst case conditions such as heavy system paging.

The implementation of a safepoint poll action is specified by looking up a
function of the name ``gc.safepoint_poll`` in the containing Module.  The body
of this function is inserted at each poll site desired.  While calls or invokes
inside this method are transformed to a ``gc.statepoints``, recursive poll
insertion is not performed.

This pass is useful for any language frontend which only has to support
garbage collection semantics at safepoints.  If you need other abstract
frame information at safepoints (e.g. for deoptimization or introspection),
you can insert safepoint polls in the frontend.  If you have the later case,
please ask on llvm-dev for suggestions.  There's been a good amount of work
done on making such a scheme work well in practice which is not yet documented
here.


Supported Architectures
=======================

Support for statepoint generation requires some code for each backend.
Today, only Aarch64 and X86_64 are supported.

.. _OpenWork:

Limitations and Half Baked Ideas
================================

Mixing References and Raw Pointers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Support for languages which allow unmanaged pointers to garbage collected
objects (i.e. pass a pointer to an object to a C routine) in the abstract
machine model.  At the moment, the best idea on how to approach this
involves an intrinsic or opaque function which hides the connection between
the reference value and the raw pointer.  The problem is that having a
ptrtoint or inttoptr cast (which is common for such use cases) breaks the
rules used for inferring base pointers for arbitrary references when
lowering out of the abstract model to the explicit physical model.  Note
that a frontend which lowers directly to the physical model doesn't have
any problems here.

Objects on the Stack
^^^^^^^^^^^^^^^^^^^^

As noted above, the explicit lowering supports objects allocated on the
stack provided the collector can find a heap map given the stack address.

The missing pieces are a) integration with rewriting (RS4GC) from the
abstract machine model and b) support for optionally decomposing on stack
objects so as not to require heap maps for them.  The later is required
for ease of integration with some collectors.

Lowering Quality and Representation Overhead
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The current statepoint lowering is known to be somewhat poor.  In the very
long term, we'd like to integrate statepoints with the register allocator;
in the near term this is unlikely to happen.  We've found the quality of
lowering to be relatively unimportant as hot-statepoints are almost always
inliner bugs.

Concerns have been raised that the statepoint representation results in a
large amount of IR being produced for some examples and that this
contributes to higher than expected memory usage and compile times.  There's
no immediate plans to make changes due to this, but alternate models may be
explored in the future.

Relocations Along Exceptional Edges
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Relocations along exceptional paths are currently broken in ToT.  In
particular, there is current no way to represent a rethrow on a path which
also has relocations.  See `this llvm-dev discussion
<https://groups.google.com/forum/#!topic/llvm-dev/AE417XjgxvI>`_ for more
detail.

Bugs and Enhancements
=====================

Currently known bugs and enhancements under consideration can be
tracked by performing a `bugzilla search
<https://bugs.llvm.org/buglist.cgi?cmdtype=runnamed&namedcmd=Statepoint%20Bugs&list_id=64342>`_
for [Statepoint] in the summary field. When filing new bugs, please
use this tag so that interested parties see the newly filed bug.  As
with most LLVM features, design discussions take place on the `Discourse forums <https://discourse.llvm.org>`_ and patches
should be sent to `llvm-commits
<http://lists.llvm.org/mailman/listinfo/llvm-commits>`_ for review.
