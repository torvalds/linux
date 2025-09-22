=======================================
The Often Misunderstood GEP Instruction
=======================================

.. contents::
   :local:

Introduction
============

This document seeks to dispel the mystery and confusion surrounding LLVM's
`GetElementPtr <LangRef.html#getelementptr-instruction>`_ (GEP) instruction.
Questions about the wily GEP instruction are probably the most frequently
occurring questions once a developer gets down to coding with LLVM. Here we lay
out the sources of confusion and show that the GEP instruction is really quite
simple.

Address Computation
===================

When people are first confronted with the GEP instruction, they tend to relate
it to known concepts from other programming paradigms, most notably C array
indexing and field selection. GEP closely resembles C array indexing and field
selection, however it is a little different and this leads to the following
questions.

What is the first index of the GEP instruction?
-----------------------------------------------

Quick answer: The index stepping through the second operand.

The confusion with the first index usually arises from thinking about the
GetElementPtr instruction as if it was a C index operator. They aren't the
same. For example, when we write, in "C":

.. code-block:: c++

  AType *Foo;
  ...
  X = &Foo->F;

it is natural to think that there is only one index, the selection of the field
``F``.  However, in this example, ``Foo`` is a pointer. That pointer
must be indexed explicitly in LLVM. C, on the other hand, indices through it
transparently.  To arrive at the same address location as the C code, you would
provide the GEP instruction with two index operands. The first operand indexes
through the pointer; the second operand indexes the field ``F`` of the
structure, just as if you wrote:

.. code-block:: c++

  X = &Foo[0].F;

Sometimes this question gets rephrased as:

.. _GEP index through first pointer:

  *Why is it okay to index through the first pointer, but subsequent pointers
  won't be dereferenced?*

The answer is simply because memory does not have to be accessed to perform the
computation. The second operand to the GEP instruction must be a value of a
pointer type. The value of the pointer is provided directly to the GEP
instruction as an operand without any need for accessing memory. It must,
therefore be indexed and requires an index operand. Consider this example:

.. code-block:: c++

  struct munger_struct {
    int f1;
    int f2;
  };
  void munge(struct munger_struct *P) {
    P[0].f1 = P[1].f1 + P[2].f2;
  }
  ...
  struct munger_struct Array[3];
  ...
  munge(Array);

In this "C" example, the front end compiler (Clang) will generate three GEP
instructions for the three indices through "P" in the assignment statement.  The
function argument ``P`` will be the second operand of each of these GEP
instructions.  The third operand indexes through that pointer.  The fourth
operand will be the field offset into the ``struct munger_struct`` type, for
either the ``f1`` or ``f2`` field. So, in LLVM assembly the ``munge`` function
looks like:

.. code-block:: llvm

  define void @munge(ptr %P) {
  entry:
    %tmp = getelementptr %struct.munger_struct, ptr %P, i32 1, i32 0
    %tmp1 = load i32, ptr %tmp
    %tmp2 = getelementptr %struct.munger_struct, ptr %P, i32 2, i32 1
    %tmp3 = load i32, ptr %tmp2
    %tmp4 = add i32 %tmp3, %tmp1
    %tmp5 = getelementptr %struct.munger_struct, ptr %P, i32 0, i32 0
    store i32 %tmp4, ptr %tmp5
    ret void
  }

In each case the second operand is the pointer through which the GEP instruction
starts. The same is true whether the second operand is an argument, allocated
memory, or a global variable.

To make this clear, let's consider a more obtuse example:

.. code-block:: text

  @MyVar = external global i32
  ...
  %idx1 = getelementptr i32, ptr @MyVar, i64 0
  %idx2 = getelementptr i32, ptr @MyVar, i64 1
  %idx3 = getelementptr i32, ptr @MyVar, i64 2

These GEP instructions are simply making address computations from the base
address of ``MyVar``.  They compute, as follows (using C syntax):

.. code-block:: c++

  idx1 = (char*) &MyVar + 0
  idx2 = (char*) &MyVar + 4
  idx3 = (char*) &MyVar + 8

Since the type ``i32`` is known to be four bytes long, the indices 0, 1 and 2
translate into memory offsets of 0, 4, and 8, respectively. No memory is
accessed to make these computations because the address of ``@MyVar`` is passed
directly to the GEP instructions.

The obtuse part of this example is in the cases of ``%idx2`` and ``%idx3``. They
result in the computation of addresses that point to memory past the end of the
``@MyVar`` global, which is only one ``i32`` long, not three ``i32``\s long.
While this is legal in LLVM, it is inadvisable because any load or store with
the pointer that results from these GEP instructions would trigger undefined
behavior (UB).

Why is the extra 0 index required?
----------------------------------

Quick answer: there are no superfluous indices.

This question arises most often when the GEP instruction is applied to a global
variable which is always a pointer type. For example, consider this:

.. code-block:: text

  %MyStruct = external global { ptr, i32 }
  ...
  %idx = getelementptr { ptr, i32 }, ptr %MyStruct, i64 0, i32 1

The GEP above yields a ``ptr`` by indexing the ``i32`` typed field of the
structure ``%MyStruct``. When people first look at it, they wonder why the ``i64
0`` index is needed. However, a closer inspection of how globals and GEPs work
reveals the need. Becoming aware of the following facts will dispel the
confusion:

#. The type of ``%MyStruct`` is *not* ``{ ptr, i32 }`` but rather ``ptr``.
   That is, ``%MyStruct`` is a pointer (to a structure), not a structure itself.

#. Point #1 is evidenced by noticing the type of the second operand of the GEP
   instruction (``%MyStruct``) which is ``ptr``.

#. The first index, ``i64 0`` is required to step over the global variable
   ``%MyStruct``.  Since the second argument to the GEP instruction must always
   be a value of pointer type, the first index steps through that pointer. A
   value of 0 means 0 elements offset from that pointer.

#. The second index, ``i32 1`` selects the second field of the structure (the
   ``i32``).

What is dereferenced by GEP?
----------------------------

Quick answer: nothing.

The GetElementPtr instruction dereferences nothing. That is, it doesn't access
memory in any way. That's what the Load and Store instructions are for.  GEP is
only involved in the computation of addresses. For example, consider this:

.. code-block:: text

  @MyVar = external global { i32, ptr }
  ...
  %idx = getelementptr { i32, ptr }, ptr @MyVar, i64 0, i32 1
  %arr = load ptr, ptr %idx
  %idx = getelementptr [40 x i32], ptr %arr, i64 0, i64 17

In this example, we have a global variable, ``@MyVar``, which is a pointer to
a structure containing a pointer. Let's assume that this inner pointer points
to an array of type ``[40 x i32]``. The above IR will first compute the address
of the inner pointer, then load the pointer, and then compute the address of
the 18th array element.

This cannot be expressed in a single GEP instruction, because it requires
a memory dereference in between. However, the following example would work
fine:

.. code-block:: text

  @MyVar = external global { i32, [40 x i32 ] }
  ...
  %idx = getelementptr { i32, [40 x i32] }, ptr @MyVar, i64 0, i32 1, i64 17

In this case, the structure does not contain a pointer and the GEP instruction
can index through the global variable, into the second field of the structure
and access the 18th ``i32`` in the array there.

Why don't GEP x,0,0,1 and GEP x,1 alias?
----------------------------------------

Quick Answer: They compute different address locations.

If you look at the first indices in these GEP instructions you find that they
are different (0 and 1), therefore the address computation diverges with that
index. Consider this example:

.. code-block:: llvm

  @MyVar = external global { [10 x i32] }
  %idx1 = getelementptr { [10 x i32] }, ptr @MyVar, i64 0, i32 0, i64 1
  %idx2 = getelementptr { [10 x i32] }, ptr @MyVar, i64 1

In this example, ``idx1`` computes the address of the second integer in the
array that is in the structure in ``@MyVar``, that is ``MyVar+4``.  However,
``idx2`` computes the address of *the next* structure after ``@MyVar``, that is
``MyVar+40``, because it indexes past the ten 4-byte integers in ``MyVar``.
Obviously, in such a situation, the pointers don't alias.

Why do GEP x,1,0,0 and GEP x,1 alias?
-------------------------------------

Quick Answer: They compute the same address location.

These two GEP instructions will compute the same address because indexing
through the 0th element does not change the address. Consider this example:

.. code-block:: llvm

  @MyVar = global { [10 x i32] }
  %idx1 = getelementptr { [10 x i32] }, ptr @MyVar, i64 1, i32 0, i64 0
  %idx2 = getelementptr { [10 x i32] }, ptr @MyVar, i64 1

In this example, the value of ``%idx1`` is ``MyVar+40``, and the value of
``%idx2`` is also ``MyVar+40``.

Can GEP index into vector elements?
-----------------------------------

This hasn't always been forcefully disallowed, though it's not recommended.  It
leads to awkward special cases in the optimizers, and fundamental inconsistency
in the IR. In the future, it will probably be outright disallowed.

What effect do address spaces have on GEPs?
-------------------------------------------

None, except that the address space qualifier on the second operand pointer type
always matches the address space qualifier on the result type.

How is GEP different from ``ptrtoint``, arithmetic, and ``inttoptr``?
---------------------------------------------------------------------

It's very similar; there are only subtle differences.

With ptrtoint, you have to pick an integer type. One approach is to pick i64;
this is safe on everything LLVM supports (LLVM internally assumes pointers are
never wider than 64 bits in many places), and the optimizer will actually narrow
the i64 arithmetic down to the actual pointer size on targets which don't
support 64-bit arithmetic in most cases. However, there are some cases where it
doesn't do this. With GEP you can avoid this problem.

Also, GEP carries additional pointer aliasing rules. It's invalid to take a GEP
from one object, address into a different separately allocated object, and
dereference it. IR producers (front-ends) must follow this rule, and consumers
(optimizers, specifically alias analysis) benefit from being able to rely on
it. See the `Rules`_ section for more information.

And, GEP is more concise in common cases.

However, for the underlying integer computation implied, there is no
difference.


I'm writing a backend for a target which needs custom lowering for GEP. How do I do this?
-----------------------------------------------------------------------------------------

You don't. The integer computation implied by a GEP is target-independent.
Typically what you'll need to do is make your backend pattern-match expressions
trees involving ADD, MUL, etc., which are what GEP is lowered into. This has the
advantage of letting your code work correctly in more cases.

GEP does use target-dependent parameters for the size and layout of data types,
which targets can customize.

If you require support for addressing units which are not 8 bits, you'll need to
fix a lot of code in the backend, with GEP lowering being only a small piece of
the overall picture.

How does VLA addressing work with GEPs?
---------------------------------------

GEPs don't natively support VLAs. LLVM's type system is entirely static, and GEP
address computations are guided by an LLVM type.

VLA indices can be implemented as linearized indices. For example, an expression
like ``X[a][b][c]``, must be effectively lowered into a form like
``X[a*m+b*n+c]``, so that it appears to the GEP as a single-dimensional array
reference.

This means if you want to write an analysis which understands array indices and
you want to support VLAs, your code will have to be prepared to reverse-engineer
the linearization. One way to solve this problem is to use the ScalarEvolution
library, which always presents VLA and non-VLA indexing in the same manner.

.. _Rules:

Rules
=====

What happens if an array index is out of bounds?
------------------------------------------------

There are two senses in which an array index can be out of bounds.

First, there's the array type which comes from the (static) type of the first
operand to the GEP. Indices greater than the number of elements in the
corresponding static array type are valid. There is no problem with out of
bounds indices in this sense. Indexing into an array only depends on the size of
the array element, not the number of elements.

A common example of how this is used is arrays where the size is not known.
It's common to use array types with zero length to represent these. The fact
that the static type says there are zero elements is irrelevant; it's perfectly
valid to compute arbitrary element indices, as the computation only depends on
the size of the array element, not the number of elements. Note that zero-sized
arrays are not a special case here.

This sense is unconnected with ``inbounds`` keyword. The ``inbounds`` keyword is
designed to describe low-level pointer arithmetic overflow conditions, rather
than high-level array indexing rules.

Analysis passes which wish to understand array indexing should not assume that
the static array type bounds are respected.

The second sense of being out of bounds is computing an address that's beyond
the actual underlying allocated object.

With the ``inbounds`` keyword, the result value of the GEP is ``poison`` if the
address is outside the actual underlying allocated object and not the address
one-past-the-end.

Without the ``inbounds`` keyword, there are no restrictions on computing
out-of-bounds addresses. Obviously, performing a load or a store requires an
address of allocated and sufficiently aligned memory. But the GEP itself is only
concerned with computing addresses.

Can array indices be negative?
------------------------------

Yes. This is basically a special case of array indices being out of bounds.

Can I compare two values computed with GEPs?
--------------------------------------------

Yes. If both addresses are within the same allocated object, or
one-past-the-end, you'll get the comparison result you expect. If either is
outside of it, integer arithmetic wrapping may occur, so the comparison may not
be meaningful.

Can I do GEP with a different pointer type than the type of the underlying object?
----------------------------------------------------------------------------------

Yes. There are no restrictions on bitcasting a pointer value to an arbitrary
pointer type. The types in a GEP serve only to define the parameters for the
underlying integer computation. They need not correspond with the actual type of
the underlying object.

Furthermore, loads and stores don't have to use the same types as the type of
the underlying object. Types in this context serve only to specify memory size
and alignment. Beyond that there are merely a hint to the optimizer indicating
how the value will likely be used.

Can I cast an object's address to integer and add it to null?
-------------------------------------------------------------

You can compute an address that way, but if you use GEP to do the add, you can't
use that pointer to actually access the object, unless the object is managed
outside of LLVM.

The underlying integer computation is sufficiently defined; null has a defined
value --- zero --- and you can add whatever value you want to it.

However, it's invalid to access (load from or store to) an LLVM-aware object
with such a pointer. This includes ``GlobalVariables``, ``Allocas``, and objects
pointed to by noalias pointers.

If you really need this functionality, you can do the arithmetic with explicit
integer instructions, and use inttoptr to convert the result to an address. Most
of GEP's special aliasing rules do not apply to pointers computed from ptrtoint,
arithmetic, and inttoptr sequences.

Can I compute the distance between two objects, and add that value to one address to compute the other address?
---------------------------------------------------------------------------------------------------------------

As with arithmetic on null, you can use GEP to compute an address that way, but
you can't use that pointer to actually access the object if you do, unless the
object is managed outside of LLVM.

Also as above, ptrtoint and inttoptr provide an alternative way to do this which
do not have this restriction.

Can I do type-based alias analysis on LLVM IR?
----------------------------------------------

You can't do type-based alias analysis using LLVM's built-in type system,
because LLVM has no restrictions on mixing types in addressing, loads or stores.

LLVM's type-based alias analysis pass uses metadata to describe a different type
system (such as the C type system), and performs type-based aliasing on top of
that.  Further details are in the
`language reference <LangRef.html#tbaa-metadata>`_.

What happens if a GEP computation overflows?
--------------------------------------------

If the GEP lacks the ``inbounds`` keyword, the value is the result from
evaluating the implied two's complement integer computation. However, since
there's no guarantee of where an object will be allocated in the address space,
such values have limited meaning.

If the GEP has the ``inbounds`` keyword, the result value is ``poison``
if the GEP overflows (i.e. wraps around the end of the address space).

As such, there are some ramifications of this for inbounds GEPs: scales implied
by array/vector/pointer indices are always known to be "nsw" since they are
signed values that are scaled by the element size.  These values are also
allowed to be negative (e.g. "``gep i32, ptr %P, i32 -1``") but the pointer
itself is logically treated as an unsigned value.  This means that GEPs have an
asymmetric relation between the pointer base (which is treated as unsigned) and
the offset applied to it (which is treated as signed). The result of the
additions within the offset calculation cannot have signed overflow, but when
applied to the base pointer, there can be signed overflow.

How can I tell if my front-end is following the rules?
------------------------------------------------------

There is currently no checker for the getelementptr rules. Currently, the only
way to do this is to manually check each place in your front-end where
GetElementPtr operators are created.

It's not possible to write a checker which could find all rule violations
statically. It would be possible to write a checker which works by instrumenting
the code with dynamic checks though. Alternatively, it would be possible to
write a static checker which catches a subset of possible problems. However, no
such checker exists today.

Rationale
=========

Why is GEP designed this way?
-----------------------------

The design of GEP has the following goals, in rough unofficial order of
priority:

* Support C, C-like languages, and languages which can be conceptually lowered
  into C (this covers a lot).

* Support optimizations such as those that are common in C compilers. In
  particular, GEP is a cornerstone of LLVM's `pointer aliasing
  model <LangRef.html#pointeraliasing>`_.

* Provide a consistent method for computing addresses so that address
  computations don't need to be a part of load and store instructions in the IR.

* Support non-C-like languages, to the extent that it doesn't interfere with
  other goals.

* Minimize target-specific information in the IR.

Why do struct member indices always use ``i32``?
------------------------------------------------

The specific type i32 is probably just a historical artifact, however it's wide
enough for all practical purposes, so there's been no need to change it.  It
doesn't necessarily imply i32 address arithmetic; it's just an identifier which
identifies a field in a struct. Requiring that all struct indices be the same
reduces the range of possibilities for cases where two GEPs are effectively the
same but have distinct operand types.

What's an uglygep?
------------------

Some LLVM optimizers operate on GEPs by internally lowering them into more
primitive integer expressions, which allows them to be combined with other
integer expressions and/or split into multiple separate integer expressions. If
they've made non-trivial changes, translating back into LLVM IR can involve
reverse-engineering the structure of the addressing in order to fit it into the
static type of the original first operand. It isn't always possibly to fully
reconstruct this structure; sometimes the underlying addressing doesn't
correspond with the static type at all. In such cases the optimizer instead will
emit a GEP with the base pointer casted to a simple address-unit pointer, using
the name "uglygep". This isn't pretty, but it's just as valid, and it's
sufficient to preserve the pointer aliasing guarantees that GEP provides.

Summary
=======

In summary, here's some things to always remember about the GetElementPtr
instruction:


#. The GEP instruction never accesses memory, it only provides pointer
   computations.

#. The second operand to the GEP instruction is always a pointer and it must be
   indexed.

#. There are no superfluous indices for the GEP instruction.

#. Trailing zero indices are superfluous for pointer aliasing, but not for the
   types of the pointers.

#. Leading zero indices are not superfluous for pointer aliasing nor the types
   of the pointers.
