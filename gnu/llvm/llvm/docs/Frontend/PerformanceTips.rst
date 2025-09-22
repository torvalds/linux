=====================================
Performance Tips for Frontend Authors
=====================================

.. contents::
   :local:
   :depth: 2

Abstract
========

The intended audience of this document is developers of language frontends
targeting LLVM IR. This document is home to a collection of tips on how to
generate IR that optimizes well.

IR Best Practices
=================

As with any optimizer, LLVM has its strengths and weaknesses.  In some cases,
surprisingly small changes in the source IR can have a large effect on the
generated code.

Beyond the specific items on the list below, it's worth noting that the most
mature frontend for LLVM is Clang.  As a result, the further your IR gets from
what Clang might emit, the less likely it is to be effectively optimized. It
can often be useful to write a quick C program with the semantics you're trying
to model and see what decisions Clang's IRGen makes about what IR to emit.
Studying Clang's CodeGen directory can also be a good source of ideas.  Note
that Clang and LLVM are explicitly version locked so you'll need to make sure
you're using a Clang built from the same git revision or release as the LLVM
library you're using.  As always, it's *strongly* recommended that you track
tip of tree development, particularly during bring up of a new project.

The Basics
^^^^^^^^^^^

#. Make sure that your Modules contain both a data layout specification and
   target triple. Without these pieces, non of the target specific optimization
   will be enabled.  This can have a major effect on the generated code quality.

#. For each function or global emitted, use the most private linkage type
   possible (private, internal or linkonce_odr preferably).  Doing so will
   make LLVM's inter-procedural optimizations much more effective.

#. Avoid high in-degree basic blocks (e.g. basic blocks with dozens or hundreds
   of predecessors).  Among other issues, the register allocator is known to
   perform badly with confronted with such structures.  The only exception to
   this guidance is that a unified return block with high in-degree is fine.

Use of allocas
^^^^^^^^^^^^^^

An alloca instruction can be used to represent a function scoped stack slot,
but can also represent dynamic frame expansion.  When representing function
scoped variables or locations, placing alloca instructions at the beginning of
the entry block should be preferred.   In particular, place them before any
call instructions. Call instructions might get inlined and replaced with
multiple basic blocks. The end result is that a following alloca instruction
would no longer be in the entry basic block afterward.

The SROA (Scalar Replacement Of Aggregates) and Mem2Reg passes only attempt
to eliminate alloca instructions that are in the entry basic block.  Given
SSA is the canonical form expected by much of the optimizer; if allocas can
not be eliminated by Mem2Reg or SROA, the optimizer is likely to be less
effective than it could be.

Avoid creating values of aggregate type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Avoid creating values of :ref:`aggregate types <t_aggregate>` (i.e. structs and
arrays). In particular, avoid loading and storing them, or manipulating them
with insertvalue and extractvalue instructions. Instead, only load and store
individual fields of the aggregate.

There are some exceptions to this rule:

* It is fine to use values of aggregate type in global variable initializers.
* It is fine to return structs, if this is done to represent the return of
  multiple values in registers.
* It is fine to work with structs returned by LLVM intrinsics, such as the
  ``with.overflow`` family of intrinsics.
* It is fine to use aggregate *types* without creating values. For example,
  they are commonly used in ``getelementptr`` instructions or attributes like
  ``sret``.

Avoid loads and stores of non-byte-sized types
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Avoid loading or storing non-byte-sized types like ``i1``. Instead,
appropriately extend them to the next byte-sized type.

For example, when working with boolean values, store them by zero-extending
``i1`` to ``i8`` and load them by loading ``i8`` and truncating to ``i1``.

If you do use loads/stores on non-byte-sized types, make sure that you *always*
use those types. For example, do not first store ``i8`` and then load ``i1``.

Prefer zext over sext when legal
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

On some architectures (X86_64 is one), sign extension can involve an extra
instruction whereas zero extension can be folded into a load.  LLVM will try to
replace a sext with a zext when it can be proven safe, but if you have
information in your source language about the range of an integer value, it can
be profitable to use a zext rather than a sext.

Alternatively, you can :ref:`specify the range of the value using metadata
<range-metadata>` and LLVM can do the sext to zext conversion for you.

Zext GEP indices to machine register width
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Internally, LLVM often promotes the width of GEP indices to machine register
width.  When it does so, it will default to using sign extension (sext)
operations for safety.  If your source language provides information about
the range of the index, you may wish to manually extend indices to machine
register width using a zext instruction.

When to specify alignment
^^^^^^^^^^^^^^^^^^^^^^^^^^
LLVM will always generate correct code if you don’t specify alignment, but may
generate inefficient code.  For example, if you are targeting MIPS (or older
ARM ISAs) then the hardware does not handle unaligned loads and stores, and
so you will enter a trap-and-emulate path if you do a load or store with
lower-than-natural alignment.  To avoid this, LLVM will emit a slower
sequence of loads, shifts and masks (or load-right + load-left on MIPS) for
all cases where the load / store does not have a sufficiently high alignment
in the IR.

The alignment is used to guarantee the alignment on allocas and globals,
though in most cases this is unnecessary (most targets have a sufficiently
high default alignment that they’ll be fine).  It is also used to provide a
contract to the back end saying ‘either this load/store has this alignment, or
it is undefined behavior’.  This means that the back end is free to emit
instructions that rely on that alignment (and mid-level optimizers are free to
perform transforms that require that alignment).  For x86, it doesn’t make
much difference, as almost all instructions are alignment-independent.  For
MIPS, it can make a big difference.

Note that if your loads and stores are atomic, the backend will be unable to
lower an under aligned access into a sequence of natively aligned accesses.
As a result, alignment is mandatory for atomic loads and stores.

Other Things to Consider
^^^^^^^^^^^^^^^^^^^^^^^^

#. Use ptrtoint/inttoptr sparingly (they interfere with pointer aliasing
   analysis), prefer GEPs

#. Prefer globals over inttoptr of a constant address - this gives you
   dereferencability information.  In MCJIT, use getSymbolAddress to provide
   actual address.

#. Be wary of ordered and atomic memory operations.  They are hard to optimize
   and may not be well optimized by the current optimizer.  Depending on your
   source language, you may consider using fences instead.

#. If calling a function which is known to throw an exception (unwind), use
   an invoke with a normal destination which contains an unreachable
   instruction.  This form conveys to the optimizer that the call returns
   abnormally.  For an invoke which neither returns normally or requires unwind
   code in the current function, you can use a noreturn call instruction if
   desired.  This is generally not required because the optimizer will convert
   an invoke with an unreachable unwind destination to a call instruction.

#. Use profile metadata to indicate statically known cold paths, even if
   dynamic profiling information is not available.  This can make a large
   difference in code placement and thus the performance of tight loops.

#. When generating code for loops, try to avoid terminating the header block of
   the loop earlier than necessary.  If the terminator of the loop header
   block is a loop exiting conditional branch, the effectiveness of LICM will
   be limited for loads not in the header.  (This is due to the fact that LLVM
   may not know such a load is safe to speculatively execute and thus can't
   lift an otherwise loop invariant load unless it can prove the exiting
   condition is not taken.)  It can be profitable, in some cases, to emit such
   instructions into the header even if they are not used along a rarely
   executed path that exits the loop.  This guidance specifically does not
   apply if the condition which terminates the loop header is itself invariant,
   or can be easily discharged by inspecting the loop index variables.

#. In hot loops, consider duplicating instructions from small basic blocks
   which end in highly predictable terminators into their successor blocks.
   If a hot successor block contains instructions which can be vectorized
   with the duplicated ones, this can provide a noticeable throughput
   improvement.  Note that this is not always profitable and does involve a
   potentially large increase in code size.

#. When checking a value against a constant, emit the check using a consistent
   comparison type.  The GVN pass *will* optimize redundant equalities even if
   the type of comparison is inverted, but GVN only runs late in the pipeline.
   As a result, you may miss the opportunity to run other important
   optimizations.

#. Avoid using arithmetic intrinsics unless you are *required* by your source
   language specification to emit a particular code sequence.  The optimizer
   is quite good at reasoning about general control flow and arithmetic, it is
   not anywhere near as strong at reasoning about the various intrinsics.  If
   profitable for code generation purposes, the optimizer will likely form the
   intrinsics itself late in the optimization pipeline.  It is *very* rarely
   profitable to emit these directly in the language frontend.  This item
   explicitly includes the use of the :ref:`overflow intrinsics <int_overflow>`.

#. Avoid using the :ref:`assume intrinsic <int_assume>` until you've
   established that a) there's no other way to express the given fact and b)
   that fact is critical for optimization purposes.  Assumes are a great
   prototyping mechanism, but they can have negative effects on both compile
   time and optimization effectiveness.  The former is fixable with enough
   effort, but the later is fairly fundamental to their designed purpose.  If
   you are creating a non-terminator unreachable instruction or passing a false
   value, use the ``store i1 true, ptr poison, align 1`` canonical form.


Describing Language Specific Properties
=======================================

When translating a source language to LLVM, finding ways to express concepts
and guarantees available in your source language which are not natively
provided by LLVM IR will greatly improve LLVM's ability to optimize your code.
As an example, C/C++'s ability to mark every add as "no signed wrap (nsw)" goes
a long way to assisting the optimizer in reasoning about loop induction
variables and thus generating more optimal code for loops.

The LLVM LangRef includes a number of mechanisms for annotating the IR with
additional semantic information.  It is *strongly* recommended that you become
highly familiar with this document.  The list below is intended to highlight a
couple of items of particular interest, but is by no means exhaustive.

Restricted Operation Semantics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#. Add nsw/nuw flags as appropriate.  Reasoning about overflow is
   generally hard for an optimizer so providing these facts from the frontend
   can be very impactful.

#. Use fast-math flags on floating point operations if legal.  If you don't
   need strict IEEE floating point semantics, there are a number of additional
   optimizations that can be performed.  This can be highly impactful for
   floating point intensive computations.

Describing Aliasing Properties
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#. Add noalias/align/dereferenceable/nonnull to function arguments and return
   values as appropriate

#. Use pointer aliasing metadata, especially tbaa metadata, to communicate
   otherwise-non-deducible pointer aliasing facts

#. Use inbounds on geps.  This can help to disambiguate some aliasing queries.

Undefined Values
^^^^^^^^^^^^^^^^

#. Use poison values instead of undef values whenever possible.

#. Tag function parameters with the noundef attribute whenever possible.

Modeling Memory Effects
^^^^^^^^^^^^^^^^^^^^^^^^

#. Mark functions as readnone/readonly/argmemonly or noreturn/nounwind when
   known.  The optimizer will try to infer these flags, but may not always be
   able to.  Manual annotations are particularly important for external
   functions that the optimizer can not analyze.

#. Use the lifetime.start/lifetime.end and invariant.start/invariant.end
   intrinsics where possible.  Common profitable uses are for stack like data
   structures (thus allowing dead store elimination) and for describing
   life times of allocas (thus allowing smaller stack sizes).

#. Mark invariant locations using !invariant.load and TBAA's constant flags

Pass Ordering
^^^^^^^^^^^^^

One of the most common mistakes made by new language frontend projects is to
use the existing -O2 or -O3 pass pipelines as is.  These pass pipelines make a
good starting point for an optimizing compiler for any language, but they have
been carefully tuned for C and C++, not your target language.  You will almost
certainly need to use a custom pass order to achieve optimal performance.  A
couple specific suggestions:

#. For languages with numerous rarely executed guard conditions (e.g. null
   checks, type checks, range checks) consider adding an extra execution or
   two of LoopUnswitch and LICM to your pass order.  The standard pass order,
   which is tuned for C and C++ applications, may not be sufficient to remove
   all dischargeable checks from loops.

#. If your language uses range checks, consider using the IRCE pass.  It is not
   currently part of the standard pass order.

#. A useful sanity check to run is to run your optimized IR back through the
   -O2 pipeline again.  If you see noticeable improvement in the resulting IR,
   you likely need to adjust your pass order.


I Still Can't Find What I'm Looking For
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you didn't find what you were looking for above, consider proposing a piece
of metadata which provides the optimization hint you need.  Such extensions are
relatively common and are generally well received by the community.  You will
need to ensure that your proposal is sufficiently general so that it benefits
others if you wish to contribute it upstream.

You should also consider describing the problem you're facing on `Discourse
<https://discourse.llvm.org>`_ and asking for advice.
It's entirely possible someone has encountered your problem before and can
give good advice.  If there are multiple interested parties, that also
increases the chances that a metadata extension would be well received by the
community as a whole.

Adding to this document
=======================

If you run across a case that you feel deserves to be covered here, please send
a patch to `llvm-commits
<http://lists.llvm.org/mailman/listinfo/llvm-commits>`_ for review.

If you have questions on these items, please ask them on `Discourse
<https://discourse.llvm.org>`_.  The more relevant
context you are able to give to your question, the more likely it is to be
answered.
