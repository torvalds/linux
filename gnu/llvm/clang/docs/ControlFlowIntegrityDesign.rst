===========================================
Control Flow Integrity Design Documentation
===========================================

This page documents the design of the :doc:`ControlFlowIntegrity` schemes
supported by Clang.

Forward-Edge CFI for Virtual Calls
==================================

This scheme works by allocating, for each static type used to make a virtual
call, a region of read-only storage in the object file holding a bit vector
that maps onto to the region of storage used for those virtual tables. Each
set bit in the bit vector corresponds to the `address point`_ for a virtual
table compatible with the static type for which the bit vector is being built.

For example, consider the following three C++ classes:

.. code-block:: c++

  struct A {
    virtual void f1();
    virtual void f2();
    virtual void f3();
  };

  struct B : A {
    virtual void f1();
    virtual void f2();
    virtual void f3();
  };

  struct C : A {
    virtual void f1();
    virtual void f2();
    virtual void f3();
  };

The scheme will cause the virtual tables for A, B and C to be laid out
consecutively:

.. csv-table:: Virtual Table Layout for A, B, C
  :header: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14

  A::offset-to-top, &A::rtti, &A::f1, &A::f2, &A::f3, B::offset-to-top, &B::rtti, &B::f1, &B::f2, &B::f3, C::offset-to-top, &C::rtti, &C::f1, &C::f2, &C::f3

The bit vector for static types A, B and C will look like this:

.. csv-table:: Bit Vectors for A, B, C
  :header: Class, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14

  A, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0
  B, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
  C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0

Bit vectors are represented in the object file as byte arrays. By loading
from indexed offsets into the byte array and applying a mask, a program can
test bits from the bit set with a relatively short instruction sequence. Bit
vectors may overlap so long as they use different bits. For the full details,
see the `ByteArrayBuilder`_ class.

In this case, assuming A is laid out at offset 0 in bit 0, B at offset 0 in
bit 1 and C at offset 0 in bit 2, the byte array would look like this:

.. code-block:: c++

  char bits[] = { 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0, 5, 0, 0 };

To emit a virtual call, the compiler will assemble code that checks that
the object's virtual table pointer is in-bounds and aligned and that the
relevant bit is set in the bit vector.

For example on x86 a typical virtual call may look like this:

.. code-block:: none

  ca7fbb:       48 8b 0f                mov    (%rdi),%rcx
  ca7fbe:       48 8d 15 c3 42 fb 07    lea    0x7fb42c3(%rip),%rdx
  ca7fc5:       48 89 c8                mov    %rcx,%rax
  ca7fc8:       48 29 d0                sub    %rdx,%rax
  ca7fcb:       48 c1 c0 3d             rol    $0x3d,%rax
  ca7fcf:       48 3d 7f 01 00 00       cmp    $0x17f,%rax
  ca7fd5:       0f 87 36 05 00 00       ja     ca8511
  ca7fdb:       48 8d 15 c0 0b f7 06    lea    0x6f70bc0(%rip),%rdx
  ca7fe2:       f6 04 10 10             testb  $0x10,(%rax,%rdx,1)
  ca7fe6:       0f 84 25 05 00 00       je     ca8511
  ca7fec:       ff 91 98 00 00 00       callq  *0x98(%rcx)
    [...]
  ca8511:       0f 0b                   ud2

The compiler relies on co-operation from the linker in order to assemble
the bit vectors for the whole program. It currently does this using LLVM's
`type metadata`_ mechanism together with link-time optimization.

.. _address point: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable-general
.. _type metadata: https://llvm.org/docs/TypeMetadata.html
.. _ByteArrayBuilder: https://llvm.org/docs/doxygen/html/structllvm_1_1ByteArrayBuilder.html

Optimizations
-------------

The scheme as described above is the fully general variant of the scheme.
Most of the time we are able to apply one or more of the following
optimizations to improve binary size or performance.

In fact, if you try the above example with the current version of the
compiler, you will probably find that it will not use the described virtual
table layout or machine instructions. Some of the optimizations we are about
to introduce cause the compiler to use a different layout or a different
sequence of machine instructions.

Stripping Leading/Trailing Zeros in Bit Vectors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If a bit vector contains leading or trailing zeros, we can strip them from
the vector. The compiler will emit code to check if the pointer is in range
of the region covered by ones, and perform the bit vector check using a
truncated version of the bit vector. For example, the bit vectors for our
example class hierarchy will be emitted like this:

.. csv-table:: Bit Vectors for A, B, C
  :header: Class, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14

  A,  ,  , 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,  ,
  B,  ,  ,  ,  ,  ,  ,  , 1,  ,  ,  ,  ,  ,  ,
  C,  ,  ,  ,  ,  ,  ,  ,  ,  ,  ,  ,  , 1,  ,

Short Inline Bit Vectors
~~~~~~~~~~~~~~~~~~~~~~~~

If the vector is sufficiently short, we can represent it as an inline constant
on x86. This saves us a few instructions when reading the correct element
of the bit vector.

If the bit vector fits in 32 bits, the code looks like this:

.. code-block:: none

     dc2:       48 8b 03                mov    (%rbx),%rax
     dc5:       48 8d 15 14 1e 00 00    lea    0x1e14(%rip),%rdx
     dcc:       48 89 c1                mov    %rax,%rcx
     dcf:       48 29 d1                sub    %rdx,%rcx
     dd2:       48 c1 c1 3d             rol    $0x3d,%rcx
     dd6:       48 83 f9 03             cmp    $0x3,%rcx
     dda:       77 2f                   ja     e0b <main+0x9b>
     ddc:       ba 09 00 00 00          mov    $0x9,%edx
     de1:       0f a3 ca                bt     %ecx,%edx
     de4:       73 25                   jae    e0b <main+0x9b>
     de6:       48 89 df                mov    %rbx,%rdi
     de9:       ff 10                   callq  *(%rax)
    [...]
     e0b:       0f 0b                   ud2

Or if the bit vector fits in 64 bits:

.. code-block:: none

    11a6:       48 8b 03                mov    (%rbx),%rax
    11a9:       48 8d 15 d0 28 00 00    lea    0x28d0(%rip),%rdx
    11b0:       48 89 c1                mov    %rax,%rcx
    11b3:       48 29 d1                sub    %rdx,%rcx
    11b6:       48 c1 c1 3d             rol    $0x3d,%rcx
    11ba:       48 83 f9 2a             cmp    $0x2a,%rcx
    11be:       77 35                   ja     11f5 <main+0xb5>
    11c0:       48 ba 09 00 00 00 00    movabs $0x40000000009,%rdx
    11c7:       04 00 00
    11ca:       48 0f a3 ca             bt     %rcx,%rdx
    11ce:       73 25                   jae    11f5 <main+0xb5>
    11d0:       48 89 df                mov    %rbx,%rdi
    11d3:       ff 10                   callq  *(%rax)
    [...]
    11f5:       0f 0b                   ud2

If the bit vector consists of a single bit, there is only one possible
virtual table, and the check can consist of a single equality comparison:

.. code-block:: none

     9a2:   48 8b 03                mov    (%rbx),%rax
     9a5:   48 8d 0d a4 13 00 00    lea    0x13a4(%rip),%rcx
     9ac:   48 39 c8                cmp    %rcx,%rax
     9af:   75 25                   jne    9d6 <main+0x86>
     9b1:   48 89 df                mov    %rbx,%rdi
     9b4:   ff 10                   callq  *(%rax)
     [...]
     9d6:   0f 0b                   ud2

Virtual Table Layout
~~~~~~~~~~~~~~~~~~~~

The compiler lays out classes of disjoint hierarchies in separate regions
of the object file. At worst, bit vectors in disjoint hierarchies only
need to cover their disjoint hierarchy. But the closer that classes in
sub-hierarchies are laid out to each other, the smaller the bit vectors for
those sub-hierarchies need to be (see "Stripping Leading/Trailing Zeros in Bit
Vectors" above). The `GlobalLayoutBuilder`_ class is responsible for laying
out the globals efficiently to minimize the sizes of the underlying bitsets.

.. _GlobalLayoutBuilder: https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/Transforms/IPO/LowerTypeTests.h

Alignment
~~~~~~~~~

If all gaps between address points in a particular bit vector are multiples
of powers of 2, the compiler can compress the bit vector by strengthening
the alignment requirements of the virtual table pointer. For example, given
this class hierarchy:

.. code-block:: c++

  struct A {
    virtual void f1();
    virtual void f2();
  };

  struct B : A {
    virtual void f1();
    virtual void f2();
    virtual void f3();
    virtual void f4();
    virtual void f5();
    virtual void f6();
  };

  struct C : A {
    virtual void f1();
    virtual void f2();
  };

The virtual tables will be laid out like this:

.. csv-table:: Virtual Table Layout for A, B, C
  :header: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

  A::offset-to-top, &A::rtti, &A::f1, &A::f2, B::offset-to-top, &B::rtti, &B::f1, &B::f2, &B::f3, &B::f4, &B::f5, &B::f6, C::offset-to-top, &C::rtti, &C::f1, &C::f2

Notice that each address point for A is separated by 4 words. This lets us
emit a compressed bit vector for A that looks like this:

.. csv-table::
  :header: 2, 6, 10, 14

  1, 1, 0, 1

At call sites, the compiler will strengthen the alignment requirements by
using a different rotate count. For example, on a 64-bit machine where the
address points are 4-word aligned (as in A from our example), the ``rol``
instruction may look like this:

.. code-block:: none

     dd2:       48 c1 c1 3b             rol    $0x3b,%rcx

Padding to Powers of 2
~~~~~~~~~~~~~~~~~~~~~~

Of course, this alignment scheme works best if the address points are
in fact aligned correctly. To make this more likely to happen, we insert
padding between virtual tables that in many cases aligns address points to
a power of 2. Specifically, our padding aligns virtual tables to the next
highest power of 2 bytes; because address points for specific base classes
normally appear at fixed offsets within the virtual table, this normally
has the effect of aligning the address points as well.

This scheme introduces tradeoffs between decreased space overhead for
instructions and bit vectors and increased overhead in the form of padding. We
therefore limit the amount of padding so that we align to no more than 128
bytes. This number was found experimentally to provide a good tradeoff.

Eliminating Bit Vector Checks for All-Ones Bit Vectors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the bit vector is all ones, the bit vector check is redundant; we simply
need to check that the address is in range and well aligned. This is more
likely to occur if the virtual tables are padded.

Forward-Edge CFI for Virtual Calls by Interleaving Virtual Tables
-----------------------------------------------------------------

Dimitar et. al. proposed a novel approach that interleaves virtual tables in [1]_.
This approach is more efficient in terms of space because padding and bit vectors are no longer needed.
At the same time, it is also more efficient in terms of performance because in the interleaved layout
address points of the virtual tables are consecutive, thus the validity check of a virtual
vtable pointer is always a range check.

At a high level, the interleaving scheme consists of three steps: 1) split virtual table groups into
separate virtual tables, 2) order virtual tables by a pre-order traversal of the class hierarchy
and 3) interleave virtual tables.

The interleaving scheme implemented in LLVM is inspired by [1]_ but has its own
enhancements (more in `Interleave virtual tables`_).

.. [1] `Protecting C++ Dynamic Dispatch Through VTable Interleaving <https://cseweb.ucsd.edu/~lerner/papers/ivtbl-ndss16.pdf>`_. Dimitar Bounov, Rami Gökhan Kıcı, Sorin Lerner.

Split virtual table groups into separate virtual tables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Itanium C++ ABI glues multiple individual virtual tables for a class into a combined virtual table (virtual table group).
The interleaving scheme, however, can only work with individual virtual tables so it must split the combined virtual tables first.
In comparison, the old scheme does not require the splitting but it is more efficient when the combined virtual tables have been split.
The `GlobalSplit`_ pass is responsible for splitting combined virtual tables into individual ones.

.. _GlobalSplit: https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/IPO/GlobalSplit.cpp

Order virtual tables by a pre-order traversal of the class hierarchy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This step is common to both the old scheme described above and the interleaving scheme.
For the interleaving scheme, since the combined virtual tables have been split in the previous step,
this step ensures that for any class all the compatible virtual tables will appear consecutively.
For the old scheme, the same property may not hold since it may work on combined virtual tables.

For example, consider the following four C++ classes:

.. code-block:: c++

  struct A {
    virtual void f1();
  };

  struct B : A {
    virtual void f1();
    virtual void f2();
  };

  struct C : A {
    virtual void f1();
    virtual void f3();
  };

  struct D : B {
    virtual void f1();
    virtual void f2();
  };

This step will arrange the virtual tables for A, B, C, and D in the order of *vtable-of-A, vtable-of-B, vtable-of-D, vtable-of-C*.

Interleave virtual tables
~~~~~~~~~~~~~~~~~~~~~~~~~

This step is where the interleaving scheme deviates from the old scheme. Instead of laying out
whole virtual tables in the previously computed order, the interleaving scheme lays out table
entries of the virtual tables strategically to ensure the following properties:

(1) offset-to-top and RTTI fields layout property

The Itanium C++ ABI specifies that offset-to-top and RTTI fields appear at the offsets behind the
address point. Note that libraries like libcxxabi do assume this property.

(2) virtual function entry layout property

For each virtual function the distance between a virtual table entry for this function and the corresponding
address point is always the same. This property ensures that dynamic dispatch still works with the interleaving layout.

Note that the interleaving scheme in the CFI implementation guarantees both properties above whereas the original scheme proposed
in [1]_ only guarantees the second property.

To illustrate how the interleaving algorithm works, let us continue with the running example.
The algorithm first separates all the virtual table entries into two work lists. To do so,
it starts by allocating two work lists, one initialized with all the offset-to-top entries of virtual tables in the order
computed in the last step, one initialized with all the RTTI entries in the same order.

.. csv-table:: Work list 1 Layout
  :header: 0, 1, 2, 3

  A::offset-to-top, B::offset-to-top, D::offset-to-top, C::offset-to-top


.. csv-table:: Work list 2 layout
  :header: 0, 1, 2, 3,

  &A::rtti, &B::rtti, &D::rtti, &C::rtti

Then for each virtual function the algorithm goes through all the virtual tables in the previously computed order
to collect all the related entries into a virtual function list.
After this step, there are the following virtual function lists:

.. csv-table:: f1 list
  :header: 0, 1, 2, 3

  &A::f1, &B::f1, &D::f1, &C::f1


.. csv-table:: f2 list
  :header: 0, 1

  &B::f2, &D::f2


.. csv-table:: f3 list
  :header: 0

  &C::f3

Next, the algorithm picks the longest remaining virtual function list and appends the whole list to the shortest work list
until no function lists are left, and pads the shorter work list so that they are of the same length.
In the example, f1 list will be first added to work list 1, then f2 list will be added
to work list 2, and finally f3 list will be added to the work list 2. Since work list 1 now has one more entry than
work list 2, a padding entry is added to the latter. After this step, the two work lists look like:

.. csv-table:: Work list 1 Layout
  :header: 0, 1, 2, 3, 4, 5, 6, 7

  A::offset-to-top, B::offset-to-top, D::offset-to-top, C::offset-to-top, &A::f1, &B::f1, &D::f1, &C::f1


.. csv-table:: Work list 2 layout
  :header: 0, 1, 2, 3, 4, 5, 6, 7

  &A::rtti, &B::rtti, &D::rtti, &C::rtti, &B::f2, &D::f2, &C::f3, padding

Finally, the algorithm merges the two work lists into the interleaved layout by alternatingly
moving the head of each list to the final layout. After this step, the final interleaved layout looks like:

.. csv-table:: Interleaved layout
  :header: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

  A::offset-to-top, &A::rtti, B::offset-to-top, &B::rtti, D::offset-to-top, &D::rtti, C::offset-to-top, &C::rtti, &A::f1, &B::f2, &B::f1, &D::f2, &D::f1, &C::f3, &C::f1, padding

In the above interleaved layout, each virtual table's offset-to-top and RTTI are always adjacent, which shows that the layout has the first property.
For the second property, let us look at f2 as an example. In the interleaved layout,
there are two entries for f2: B::f2 and D::f2. The distance between &B::f2
and its address point D::offset-to-top (the entry immediately after &B::rtti) is 5 entry-length, so is the distance between &D::f2 and C::offset-to-top (the entry immediately after &D::rtti).

Forward-Edge CFI for Indirect Function Calls
============================================

Under forward-edge CFI for indirect function calls, each unique function
type has its own bit vector, and at each call site we need to check that the
function pointer is a member of the function type's bit vector. This scheme
works in a similar way to forward-edge CFI for virtual calls, the distinction
being that we need to build bit vectors of function entry points rather than
of virtual tables.

Unlike when re-arranging global variables, we cannot re-arrange functions
in a particular order and base our calculations on the layout of the
functions' entry points, as we have no idea how large a particular function
will end up being (the function sizes could even depend on how we arrange
the functions). Instead, we build a jump table, which is a block of code
consisting of one branch instruction for each of the functions in the bit
set that branches to the target function, and redirect any taken function
addresses to the corresponding jump table entry. In this way, the distance
between function entry points is predictable and controllable. In the object
file's symbol table, the symbols for the target functions also refer to the
jump table entries, so that addresses taken outside the module will pass
any verification done inside the module.

In more concrete terms, suppose we have three functions ``f``, ``g``,
``h`` which are all of the same type, and a function foo that returns their
addresses:

.. code-block:: none

  f:
  mov 0, %eax
  ret

  g:
  mov 1, %eax
  ret

  h:
  mov 2, %eax
  ret

  foo:
  mov f, %eax
  mov g, %edx
  mov h, %ecx
  ret

Our jump table will (conceptually) look like this:

.. code-block:: none

  f:
  jmp .Ltmp0 ; 5 bytes
  int3       ; 1 byte
  int3       ; 1 byte
  int3       ; 1 byte

  g:
  jmp .Ltmp1 ; 5 bytes
  int3       ; 1 byte
  int3       ; 1 byte
  int3       ; 1 byte

  h:
  jmp .Ltmp2 ; 5 bytes
  int3       ; 1 byte
  int3       ; 1 byte
  int3       ; 1 byte

  .Ltmp0:
  mov 0, %eax
  ret

  .Ltmp1:
  mov 1, %eax
  ret

  .Ltmp2:
  mov 2, %eax
  ret

  foo:
  mov f, %eax
  mov g, %edx
  mov h, %ecx
  ret

Because the addresses of ``f``, ``g``, ``h`` are evenly spaced at a power of
2, and function types do not overlap (unlike class types with base classes),
we can normally apply the `Alignment`_ and `Eliminating Bit Vector Checks
for All-Ones Bit Vectors`_ optimizations thus simplifying the check at each
call site to a range and alignment check.

Shared library support
======================

**EXPERIMENTAL**

The basic CFI mode described above assumes that the application is a
monolithic binary; at least that all possible virtual/indirect call
targets and the entire class hierarchy are known at link time. The
cross-DSO mode, enabled with **-f[no-]sanitize-cfi-cross-dso** relaxes
this requirement by allowing virtual and indirect calls to cross the
DSO boundary.

Assuming the following setup: the binary consists of several
instrumented and several uninstrumented DSOs. Some of them may be
dlopen-ed/dlclose-d periodically, even frequently.

  - Calls made from uninstrumented DSOs are not checked and just work.
  - Calls inside any instrumented DSO are fully protected.
  - Calls between different instrumented DSOs are also protected, with
     a performance penalty (in addition to the monolithic CFI
     overhead).
  - Calls from an instrumented DSO to an uninstrumented one are
     unchecked and just work, with performance penalty.
  - Calls from an instrumented DSO outside of any known DSO are
     detected as CFI violations.

In the monolithic scheme a call site is instrumented as

.. code-block:: none

   if (!InlinedFastCheck(f))
     abort();
   call *f

In the cross-DSO scheme it becomes

.. code-block:: none

   if (!InlinedFastCheck(f))
     __cfi_slowpath(CallSiteTypeId, f);
   call *f

CallSiteTypeId
--------------

``CallSiteTypeId`` is a stable process-wide identifier of the
call-site type. For a virtual call site, the type in question is the class
type; for an indirect function call it is the function signature. The
mapping from a type to an identifier is an ABI detail. In the current,
experimental, implementation the identifier of type T is calculated as
follows:

  -  Obtain the mangled name for "typeinfo name for T".
  -  Calculate MD5 hash of the name as a string.
  -  Reinterpret the first 8 bytes of the hash as a little-endian
     64-bit integer.

It is possible, but unlikely, that collisions in the
``CallSiteTypeId`` hashing will result in weaker CFI checks that would
still be conservatively correct.

CFI_Check
---------

In the general case, only the target DSO knows whether the call to
function ``f`` with type ``CallSiteTypeId`` is valid or not.  To
export this information, every DSO implements

.. code-block:: none

   void __cfi_check(uint64 CallSiteTypeId, void *TargetAddr, void *DiagData)

This function provides external modules with access to CFI checks for
the targets inside this DSO.  For each known ``CallSiteTypeId``, this
function performs an ``llvm.type.test`` with the corresponding type
identifier. It reports an error if the type is unknown, or if the
check fails. Depending on the values of compiler flags
``-fsanitize-trap`` and ``-fsanitize-recover``, this function may
print an error, abort and/or return to the caller. ``DiagData`` is an
opaque pointer to the diagnostic information about the error, or
``null`` if the caller does not provide this information.

The basic implementation is a large switch statement over all values
of CallSiteTypeId supported by this DSO, and each case is similar to
the InlinedFastCheck() in the basic CFI mode.

CFI Shadow
----------

To route CFI checks to the target DSO's __cfi_check function, a
mapping from possible virtual / indirect call targets to the
corresponding __cfi_check functions is maintained. This mapping is
implemented as a sparse array of 2 bytes for every possible page (4096
bytes) of memory. The table is kept readonly most of the time.

There are 3 types of shadow values:

  -  Address in a CFI-instrumented DSO.
  -  Unchecked address (a “trusted” non-instrumented DSO). Encoded as
     value 0xFFFF.
  -  Invalid address (everything else). Encoded as value 0.

For a CFI-instrumented DSO, a shadow value encodes the address of the
__cfi_check function for all call targets in the corresponding memory
page. If Addr is the target address, and V is the shadow value, then
the address of __cfi_check is calculated as

.. code-block:: none

  __cfi_check = AlignUpTo(Addr, 4096) - (V + 1) * 4096

This works as long as __cfi_check is aligned by 4096 bytes and located
below any call targets in its DSO, but not more than 256MB apart from
them.

CFI_SlowPath
------------

The slow path check is implemented in a runtime support library as

.. code-block:: none

  void __cfi_slowpath(uint64 CallSiteTypeId, void *TargetAddr)
  void __cfi_slowpath_diag(uint64 CallSiteTypeId, void *TargetAddr, void *DiagData)

These functions loads a shadow value for ``TargetAddr``, finds the
address of ``__cfi_check`` as described above and calls
that. ``DiagData`` is an opaque pointer to diagnostic data which is
passed verbatim to ``__cfi_check``, and ``__cfi_slowpath`` passes
``nullptr`` instead.

Compiler-RT library contains reference implementations of slowpath
functions, but they have unresolvable issues with correctness and
performance in the handling of dlopen(). It is recommended that
platforms provide their own implementations, usually as part of libc
or libdl.

Position-independent executable requirement
-------------------------------------------

Cross-DSO CFI mode requires that the main executable is built as PIE.
In non-PIE executables the address of an external function (taken from
the main executable) is the address of that function’s PLT record in
the main executable. This would break the CFI checks.

Backward-edge CFI for return statements (RCFI)
==============================================

This section is a proposal. As of March 2017 it is not implemented.

Backward-edge control flow (`RET` instructions) can be hijacked
via overwriting the return address (`RA`) on stack.
Various mitigation techniques (e.g. `SafeStack`_, `RFG`_, `Intel CET`_)
try to detect or prevent `RA` corruption on stack.

RCFI enforces the expected control flow in several different ways described below.
RCFI heavily relies on LTO.

Leaf Functions
--------------
If `f()` is a leaf function (i.e. it has no calls
except maybe no-return calls) it can be called using a special calling convention
that stores `RA` in a dedicated register `R` before the `CALL` instruction.
`f()` does not spill `R` and does not use the `RET` instruction,
instead it uses the value in `R` to `JMP` to `RA`.

This flavour of CFI is *precise*, i.e. the function is guaranteed to return
to the point exactly following the call.

An alternative approach is to
copy `RA` from stack to `R` in the first instruction of `f()`,
then `JMP` to `R`.
This approach is simpler to implement (does not require changing the caller)
but weaker (there is a small window when `RA` is actually stored on stack).


Functions called once
---------------------
Suppose `f()` is called in just one place in the program
(assuming we can verify this in LTO mode).
In this case we can replace the `RET` instruction with a `JMP` instruction
with the immediate constant for `RA`.
This will *precisely* enforce the return control flow no matter what is stored on stack.

Another variant is to compare `RA` on stack with the known constant and abort
if they don't match; then `JMP` to the known constant address.

Functions called in a small number of call sites
------------------------------------------------
We may extend the above approach to cases where `f()`
is called more than once (but still a small number of times).
With LTO we know all possible values of `RA` and we check them
one-by-one (or using binary search) against the value on stack.
If the match is found, we `JMP` to the known constant address, otherwise abort.

This protection is *near-precise*, i.e. it guarantees that the control flow will
be transferred to one of the valid return addresses for this function,
but not necessary to the point of the most recent `CALL`.

General case
------------
For functions called multiple times a *return jump table* is constructed
in the same manner as jump tables for indirect function calls (see above).
The correct jump table entry (or its index) is passed by `CALL` to `f()`
(as an extra argument) and then spilled to stack.
The `RET` instruction is replaced with a load of the jump table entry,
jump table range check, and `JMP` to the jump table entry.

This protection is also *near-precise*.

Returns from functions called indirectly
----------------------------------------

If a function is called indirectly, the return jump table is constructed for the
equivalence class of functions instead of a single function.

Cross-DSO calls
---------------
Consider two instrumented DSOs, `A` and `B`. `A` defines `f()` and `B` calls it.

This case will be handled similarly to the cross-DSO scheme using the slow path callback.

Non-goals
---------

RCFI does not protect `RET` instructions:
  * in non-instrumented DSOs,
  * in instrumented DSOs for functions that are called from non-instrumented DSOs,
  * embedded into other instructions (e.g. `0f4fc3 cmovg %ebx,%eax`).

.. _SafeStack: https://clang.llvm.org/docs/SafeStack.html
.. _RFG: https://xlab.tencent.com/en/2016/11/02/return-flow-guard
.. _Intel CET: https://software.intel.com/en-us/blogs/2016/06/09/intel-release-new-technology-specifications-protect-rop-attacks

Hardware support
================

We believe that the above design can be efficiently implemented in hardware.
A single new instruction added to an ISA would allow to perform the forward-edge CFI check
with fewer bytes per check (smaller code size overhead) and potentially more
efficiently. The current software-only instrumentation requires at least
32-bytes per check (on x86_64).
A hardware instruction may probably be less than ~ 12 bytes.
Such instruction would check that the argument pointer is in-bounds,
and is properly aligned, and if the checks fail it will either trap (in monolithic scheme)
or call the slow path function (cross-DSO scheme).
The bit vector lookup is probably too complex for a hardware implementation.

.. code-block:: none

  //  This instruction checks that 'Ptr'
  //   * is aligned by (1 << kAlignment) and
  //   * is inside [kRangeBeg, kRangeBeg+(kRangeSize<<kAlignment))
  //  and if the check fails it jumps to the given target (slow path).
  //
  // 'Ptr' is a register, pointing to the virtual function table
  //    or to the function which we need to check. We may require an explicit
  //    fixed register to be used.
  // 'kAlignment' is a 4-bit constant.
  // 'kRangeSize' is a ~20-bit constant.
  // 'kRangeBeg' is a PC-relative constant (~28 bits)
  //    pointing to the beginning of the allowed range for 'Ptr'.
  // 'kFailedCheckTarget': is a PC-relative constant (~28 bits)
  //    representing the target to branch to when the check fails.
  //    If kFailedCheckTarget==0, the process will trap
  //    (monolithic binary scheme).
  //    Otherwise it will jump to a handler that implements `CFI_SlowPath`
  //    (cross-DSO scheme).
  CFI_Check(Ptr, kAlignment, kRangeSize, kRangeBeg, kFailedCheckTarget) {
     if (Ptr < kRangeBeg ||
         Ptr >= kRangeBeg + (kRangeSize << kAlignment) ||
         Ptr & ((1 << kAlignment) - 1))
           Jump(kFailedCheckTarget);
  }

An alternative and more compact encoding would not use `kFailedCheckTarget`,
and will trap on check failure instead.
This will allow us to fit the instruction into **8-9 bytes**.
The cross-DSO checks will be performed by a trap handler and
performance-critical ones will have to be black-listed and checked using the
software-only scheme.

Note that such hardware extension would be complementary to checks
at the callee side, such as e.g. **Intel ENDBRANCH**.
Moreover, CFI would have two benefits over ENDBRANCH: a) precision and b)
ability to protect against invalid casts between polymorphic types.
