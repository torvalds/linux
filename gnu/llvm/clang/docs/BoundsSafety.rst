==================================================
``-fbounds-safety``: Enforcing bounds safety for C
==================================================

.. contents::
   :local:

Overview
========

**NOTE:** This is a design document and the feature is not available for users yet.
Please see :doc:`BoundsSafetyImplPlans` for more details.

``-fbounds-safety`` is a C extension to enforce bounds safety to prevent
out-of-bounds (OOB) memory accesses, which remain a major source of security
vulnerabilities in C. ``-fbounds-safety`` aims to eliminate this class of bugs
by turning OOB accesses into deterministic traps.

The ``-fbounds-safety`` extension offers bounds annotations that programmers can
use to attach bounds to pointers. For example, programmers can add the
``__counted_by(N)`` annotation to parameter ``ptr``, indicating that the pointer
has ``N`` valid elements:

.. code-block:: c

   void foo(int *__counted_by(N) ptr, size_t N);

Using this bounds information, the compiler inserts bounds checks on every
pointer dereference, ensuring that the program does not access memory outside
the specified bounds. The compiler requires programmers to provide enough bounds
information so that the accesses can be checked at either run time or compile
time — and it rejects code if it cannot.

The most important contribution of ``-fbounds-safety`` is how it reduces the
programmer's annotation burden by reconciling bounds annotations at ABI
boundaries with the use of implicit wide pointers (a.k.a. "fat" pointers) that
carry bounds information on local variables without the need for annotations. We
designed this model so that it preserves ABI compatibility with C while
minimizing adoption effort.

The ``-fbounds-safety`` extension has been adopted on millions of lines of
production C code and proven to work in a consumer operating system setting. The
extension was designed to enable incremental adoption — a key requirement in
real-world settings where modifying an entire project and its dependencies all
at once is often not possible. It also addresses multiple of other practical
challenges that have made existing approaches to safer C dialects difficult to
adopt, offering these properties that make it widely adoptable in practice:

* It is designed to preserve the Application Binary Interface (ABI).
* It interoperates well with plain C code.
* It can be adopted partially and incrementally while still providing safety
  benefits.
* It is a conforming extension to C.
* Consequently, source code that adopts the extension can continue to be
  compiled by toolchains that do not support the extension (CAVEAT: this still
  requires inclusion of a header file macro-defining bounds annotations to
  empty).
* It has a relatively low adoption cost.

This document discusses the key designs of ``-fbounds-safety``. The document is
subject to be actively updated with a more detailed specification.

Programming Model
=================

Overview
--------

``-fbounds-safety`` ensures that pointers are not used to access memory beyond
their bounds by performing bounds checking. If a bounds check fails, the program
will deterministically trap before out-of-bounds memory is accessed.

In our model, every pointer has an explicit or implicit bounds attribute that
determines its bounds and ensures guaranteed bounds checking. Consider the
example below where the ``__counted_by(count)`` annotation indicates that
parameter ``p`` points to a buffer of integers containing ``count`` elements. An
off-by-one error is present in the loop condition, leading to ``p[i]`` being
out-of-bounds access during the loop's final iteration. The compiler inserts a
bounds check before ``p`` is dereferenced to ensure that the access remains
within the specified bounds.

.. code-block:: c

   void fill_array_with_indices(int *__counted_by(count) p, unsigned count) {
      // off-by-one error (i < count)
      for (unsigned i = 0; i <= count; ++i) {
         // bounds check inserted:
         //   if (i >= count) trap();
         p[i] = i;
      }
   }

A bounds annotation defines an invariant for the pointer type, and the model
ensures that this invariant remains true. In the example below, pointer ``p``
annotated with ``__counted_by(count)`` must always point to a memory buffer
containing at least ``count`` elements of the pointee type. Changing the value
of ``count``, like in the example below, may violate this invariant and permit
out-of-bounds access to the pointer. To avoid this, the compiler employs
compile-time restrictions and emits run-time checks as necessary to ensure the
new count value doesn't exceed the actual length of the buffer. Section
`Maintaining correctness of bounds annotations`_ provides more details about
this programming model.

.. code-block:: c

   int g;

   void foo(int *__counted_by(count) p, size_t count) {
      count++; // may violate the invariant of __counted_by
      count--; // may violate the invariant of __counted_by if count was 0.
      count = g; // may violate the invariant of __counted_by
                 // depending on the value of `g`.
   }

The requirement to annotate all pointers with explicit bounds information could
present a significant adoption burden. To tackle this issue, the model
incorporates the concept of a "wide pointer" (a.k.a. fat pointer) – a larger
pointer that carries bounds information alongside the pointer value. Utilizing
wide pointers can potentially reduce the adoption burden, as it contains bounds
information internally and eliminates the need for explicit bounds annotations.
However, wide pointers differ from standard C pointers in their data layout,
which may result in incompatibilities with the application binary interface
(ABI). Breaking the ABI complicates interoperability with external code that has
not adopted the same programming model.

``-fbounds-safety`` harmonizes the wide pointer and the bounds annotation
approaches to reduce the adoption burden while maintaining the ABI. In this
model, local variables of pointer type are implicitly treated as wide pointers,
allowing them to carry bounds information without requiring explicit bounds
annotations. Please note that this approach doesn't apply to function parameters
which are considered ABI-visible. As local variables are typically hidden from
the ABI, this approach has a marginal impact on it. In addition,
``-fbounds-safety`` employs compile-time restrictions to prevent implicit wide
pointers from silently breaking the ABI (see `ABI implications of default bounds
annotations`_). Pointers associated with any other variables, including function
parameters, are treated as single object pointers (i.e., ``__single``), ensuring
that they always have the tightest bounds by default and offering a strong
bounds safety guarantee.

By implementing default bounds annotations based on ABI visibility, a
considerable portion of C code can operate without modifications within this
programming model, reducing the adoption burden.

The rest of the section will discuss individual bounds annotations and the
programming model in more detail.

Bounds annotations
------------------

Annotation for pointers to a single object
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The C language allows pointer arithmetic on arbitrary pointers and this has been
a source of many bounds safety issues. In practice, many pointers are merely
pointing to a single object and incrementing or decrementing such a pointer
immediately makes the pointer go out-of-bounds. To prevent this unsafety,
``-fbounds-safety`` provides the annotation ``__single`` that causes pointer
arithmetic on annotated pointers to be a compile time error.

* ``__single`` : indicates that the pointer is either pointing to a single
  object or null. Hence, pointers with ``__single`` do not permit pointer
  arithmetic nor being subscripted with a non-zero index. Dereferencing a
  ``__single`` pointer is allowed but it requires a null check. Upper and lower
  bounds checks are not required because the ``__single`` pointer should point
  to a valid object unless it's null.

``__single`` is the default annotation for ABI-visible pointers. This
gives strong security guarantees in that these pointers cannot be incremented or
decremented unless they have an explicit, overriding bounds annotation that can
be used to verify the safety of the operation. The compiler issues an error when
a ``__single`` pointer is utilized for pointer arithmetic or array access, as
these operations would immediately cause the pointer to exceed its bounds.
Consequently, this prompts programmers to provide sufficient bounds information
to pointers. In the following example, the pointer on parameter p is
single-by-default, and is employed for array access. As a result, the compiler
generates an error suggesting to add ``__counted_by`` to the pointer.

.. code-block:: c

   void fill_array_with_indices(int *p, unsigned count) {
      for (unsigned i = 0; i < count; ++i) {
         p[i] = i; // error
      }
   }


External bounds annotations
^^^^^^^^^^^^^^^^^^^^^^^^^^^

"External" bounds annotations provide a way to express a relationship between a
pointer variable and another variable (or expression) containing the bounds
information of the pointer. In the following example, ``__counted_by(count)``
annotation expresses the bounds of parameter p using another parameter count.
This model works naturally with many C interfaces and structs because the bounds
of a pointer is often available adjacent to the pointer itself, e.g., at another
parameter of the same function prototype, or at another field of the same struct
declaration.

.. code-block:: c

   void fill_array_with_indices(int *__counted_by(count) p, size_t count) {
      // off-by-one error
      for (size_t i = 0; i <= count; ++i)
         p[i] = i;
   }

External bounds annotations include ``__counted_by``, ``__sized_by``, and
``__ended_by``. These annotations do not change the pointer representation,
meaning they do not have ABI implications.

* ``__counted_by(N)`` : The pointer points to memory that contains ``N``
  elements of pointee type. ``N`` is an expression of integer type which can be
  a simple reference to declaration, a constant including calls to constant
  functions, or an arithmetic expression that does not have side effect. The
  ``__counted_by`` annotation cannot apply to pointers to incomplete types or
  types without size such as ``void *``. Instead, ``__sized_by`` can be used to
  describe the byte count.
* ``__sized_by(N)`` : The pointer points to memory that contains ``N`` bytes.
  Just like the argument of ``__counted_by``, ``N`` is an expression of integer
  type which can be a constant, a simple reference to a declaration, or an
  arithmetic expression that does not have side effects. This is mainly used for
  pointers to incomplete types or types without size such as ``void *``.
* ``__ended_by(P)`` : The pointer has the upper bound of value ``P``, which is
  one past the last element of the pointer. In other words, this annotation
  describes a range that starts with the pointer that has this annotation and
  ends with ``P`` which is the argument of the annotation. ``P`` itself may be
  annotated with ``__ended_by(Q)``. In this case, the end of the range extends
  to the pointer ``Q``. This is used for "iterator" support in C where you're
  iterating from one pointer value to another until a final pointer value is
  reached (and the final pointer value is not dereferencable).

Accessing a pointer outside the specified bounds causes a run-time trap or a
compile-time error. Also, the model maintains correctness of bounds annotations
when the pointer and/or the related value containing the bounds information are
updated or passed as arguments. This is done by compile-time restrictions or
run-time checks (see `Maintaining correctness of bounds annotations`_
for more detail). For instance, initializing ``buf`` with ``null`` while
assigning non-zero value to ``count``, as shown in the following example, would
violate the ``__counted_by`` annotation because a null pointer does not point to
any valid memory location. To avoid this, the compiler produces either a
compile-time error or run-time trap.

.. code-block:: c

   void null_with_count_10(int *__counted_by(count) buf, unsigned count) {
      buf = 0;
      // This is not allowed as it creates a null pointer with non-zero length
      count = 10;
   }

However, there are use cases where a pointer is either a null pointer or is
pointing to memory of the specified size. To support this idiom,
``-fbounds-safety`` provides ``*_or_null`` variants,
``__counted_by_or_null(N)``, ``__sized_by_or_null(N)``, and
``__ended_by_or_null(P)``. Accessing a pointer with any of these bounds
annotations will require an extra null check to avoid a null pointer
dereference.

Internal bounds annotations
^^^^^^^^^^^^^^^^^^^^^^^^^^^

A wide pointer (sometimes known as a "fat" pointer) is a pointer that carries
additional bounds information internally (as part of its data). The bounds
require additional storage space making wide pointers larger than normal
pointers, hence the name "wide pointer". The memory layout of a wide pointer is
equivalent to a struct with the pointer, upper bound, and (optionally) lower
bound as its fields as shown below.

.. code-block:: c

   struct wide_pointer_datalayout {
      void* pointer; // Address used for dereferences and pointer arithmetic
      void* upper_bound; // Points one past the highest address that can be
                         // accessed
      void* lower_bound; // (Optional) Points to lowest address that can be
                         // accessed
   };

Even with this representational change, wide pointers act syntactically as
normal pointers to allow standard pointer operations, such as pointer
dereference (``*p``), array subscript (``p[i]``), member access (``p->``), and
pointer arithmetic, with some restrictions on bounds-unsafe uses.

``-fbounds-safety`` has a set of "internal" bounds annotations to turn pointers
into wide pointers. These are ``__bidi_indexable`` and ``__indexable``. When a
pointer has either of these annotations, the compiler changes the pointer to the
corresponding wide pointer. This means these annotations will break the ABI and
will not be compatible with plain C, and thus they should generally not be used
in ABI surfaces.

* ``__bidi_indexable`` : A pointer with this annotation becomes a wide pointer
  to carry the upper bound and the lower bound, the layout of which is
  equivalent to ``struct { T *ptr; T *upper_bound; T *lower_bound; };``. As the
  name indicates, pointers with this annotation are "bidirectionally indexable",
  meaning that they can be indexed with either a negative or a positive offset
  and the pointers can be incremented or decremented using pointer arithmetic. A
  ``__bidi_indexable`` pointer is allowed to hold an out-of-bounds pointer
  value. While creating an OOB pointer is undefined behavior in C,
  ``-fbounds-safety`` makes it well-defined behavior. That is, pointer
  arithmetic overflow with ``__bidi_indexable`` is defined as equivalent of
  two's complement integer computation, and at the LLVM IR level this means
  ``getelementptr`` won't get ``inbounds`` keyword. Accessing memory using the
  OOB pointer is prevented via a run-time bounds check.

* ``__indexable`` : A pointer with this annotation becomes a wide pointer
  carrying the upper bound (but no explicit lower bound), the layout of which is
  equivalent to ``struct { T *ptr; T *upper_bound; };``. Since ``__indexable``
  pointers do not have a separate lower bound, the pointer value itself acts as
  the lower bound. An ``__indexable`` pointer can only be incremented or indexed
  in the positive direction. Indexing it in the negative direction will trigger
  a compile-time error. Otherwise, the compiler inserts a run-time
  check to ensure pointer arithmetic doesn't make the pointer smaller than the
  original ``__indexable`` pointer (Note that ``__indexable`` doesn't have a
  lower bound so the pointer value is effectively the lower bound). As pointer
  arithmetic overflow will make the pointer smaller than the original pointer,
  it will cause a trap at runtime. Similar to ``__bidi_indexable``, an
  ``__indexable`` pointer is allowed to have a pointer value above the upper
  bound and creating such a pointer is well-defined behavior. Dereferencing such
  a pointer, however, will cause a run-time trap.

* ``__bidi_indexable`` offers the best flexibility out of all the pointer
  annotations in this model, as ``__bidi_indexable`` pointers can be used for
  any pointer operation. However, this comes with the largest code size and
  memory cost out of the available pointer annotations in this model. In some
  cases, use of the ``__bidi_indexable`` annotation may be duplicating bounds
  information that exists elsewhere in the program. In such cases, using
  external bounds annotations may be a better choice.

``__bidi_indexable`` is the default annotation for non-ABI visible pointers,
such as local pointer variables — that is, if the programmer does not specify
another bounds annotation, a local pointer variable is implicitly
``__bidi_indexable``. Since ``__bidi_indexable`` pointers automatically carry
bounds information and have no restrictions on kinds of pointer operations that
can be used with these pointers, most code inside a function works as is without
modification. In the example below, ``int *buf`` doesn't require manual
annotation as it's implicitly ``int *__bidi_indexable buf``, carrying the bounds
information passed from the return value of malloc, which is necessary to insert
bounds checking for ``buf[i]``.

.. code-block:: c

   void *__sized_by(size) malloc(size_t size);

   int *__counted_by(n) get_array_with_0_to_n_1(size_t n) {
      int *buf = malloc(sizeof(int) * n);
      for (size_t i = 0; i < n; ++i)
         buf[i] = i;
      return buf;
   }

Annotations for sentinel-delimited arrays
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A C string is an array of characters. The null terminator — the first null
character ('\0') element in the array — marks the end of the string.
``-fbounds-safety`` provides ``__null_terminated`` to annotate C strings and the
generalized form ``__terminated_by(T)`` to annotate pointers and arrays with an
end marked by a sentinel value. The model prevents dereferencing a
``__terminated_by`` pointer beyond its end. Calculating the location of the end
(i.e., the address of the sentinel value), requires reading the entire array in
memory and would have some performance costs. To avoid an unintended performance
hit, the model puts some restrictions on how these pointers can be used.
``__terminated_by`` pointers cannot be indexed and can only be incremented one
element at a time. To allow these operations, the pointers must be explicitly
converted to ``__indexable`` pointers using the intrinsic function
``__unsafe_terminated_by_to_indexable(P, T)`` (or
``__unsafe_null_terminated_to_indexable(P)``) which converts the
``__terminated_by`` pointer ``P`` to an ``__indexable`` pointer.

* ``__null_terminated`` : The pointer or array is terminated by ``NULL`` or
  ``0``. Modifying the terminator or incrementing the pointer beyond it is
  prevented at run time.

* ``__terminated_by(T)`` : The pointer or array is terminated by ``T`` which is
  a constant expression. Accessing or incrementing the pointer beyond the
  terminator is not allowed. This is a generalization of ``__null_terminated``
  which is defined as ``__terminated_by(0)``.

Annotation for interoperating with bounds-unsafe code
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A pointer with the ``__unsafe_indexable`` annotation behaves the same as a plain
C pointer. That is, the pointer does not have any bounds information and pointer
operations are not checked.

``__unsafe_indexable`` can be used to mark pointers from system headers or
pointers from code that has not adopted -fbounds safety. This enables
interoperation between code using ``-fbounds-safety`` and code that does not.

Default pointer types
---------------------

ABI visibility and default annotations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Requiring ``-fbounds-safety`` adopters to add bounds annotations to all pointers
in the codebase would be a significant adoption burden. To avoid this and to
secure all pointers by default, ``-fbounds-safety`` applies default bounds
annotations to pointer types.
Default annotations apply to pointer types of declarations

``-fbounds-safety`` applies default bounds annotations to pointer types used in
declarations. The default annotations are determined by the ABI visibility of
the pointer. A pointer type is ABI-visible if changing its size or
representation affects the ABI. For instance, changing the size of a type used
in a function parameter will affect the ABI and thus pointers used in function
parameters are ABI-visible pointers. On the other hand, changing the types of
local variables won't have such ABI implications. Hence, ``-fbounds-safety``
considers the outermost pointer types of local variables as non-ABI visible. The
rest of the pointers such as nested pointer types, pointer types of global
variables, struct fields, and function prototypes are considered ABI-visible.

All ABI-visible pointers are treated as ``__single`` by default unless annotated
otherwise. This default both preserves ABI and makes these pointers safe by
default. This behavior can be controlled with macros, i.e.,
``__ptrcheck_abi_assume_*ATTR*()``, to set the default annotation for
ABI-visible pointers to be either ``__single``, ``__bidi_indexable``,
``__indexable``, or ``__unsafe_indexable``. For instance,
``__ptrcheck_abi_assume_unsafe_indexable()`` will make all ABI-visible pointers
be ``__unsafe_indexable``. Non-ABI visible pointers — the outermost pointer
types of local variables — are ``__bidi_indexable`` by default, so that these
pointers have the bounds information necessary to perform bounds checks without
the need for a manual annotation. All ``const char`` pointers or any typedefs
equivalent to ``const char`` pointers are ``__null_terminated`` by default. This
means that ``char8_t`` is ``unsigned char`` so ``const char8_t *`` won't be
``__null_terminated`` by default. Similarly, ``const wchar_t *`` won't be
``__null_terminated`` by default unless the platform defines it as ``typedef
char wchar_t``. Please note, however, that the programmers can still explicitly
use ``__null_terminated`` in any other pointers, e.g., ``char8_t
*__null_terminated``, ``wchar_t *__null_terminated``, ``int
*__null_terminated``, etc. if they should be treated as ``__null_terminated``.
The same applies to other annotations.
In system headers, the default pointer attribute for ABI-visible pointers is set
to ``__unsafe_indexable`` by default.

The ``__ptrcheck_abi_assume_*ATTR*()`` macros are defined as pragmas in the
toolchain header (See `Portability with toolchains that do not support the
extension`_ for more details about the toolchain header):

.. code-block:: C

#define __ptrcheck_abi_assume_single() \
   _Pragma("clang abi_ptr_attr set(single)")

#define __ptrcheck_abi_assume_indexable() \
  _Pragma("clang abi_ptr_attr set(indexable)")

#define __ptrcheck_abi_assume_bidi_indexable() \
  _Pragma("clang abi_ptr_attr set(bidi_indexable)")

#define __ptrcheck_abi_assume_unsafe_indexable() \
  _Pragma("clang abi_ptr_attr set(unsafe_indexable)")


ABI implications of default bounds annotations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Although simply modifying types of a local variable doesn't normally impact the
ABI, taking the address of such a modified type could create a pointer type that
has an ABI mismatch. Looking at the following example, ``int *local`` is
implicitly ``int *__bidi_indexable`` and thus the type of ``&local`` is a
pointer to ``int *__bidi_indexable``. On the other hand, in ``void foo(int
**)``, the parameter type is a pointer to ``int *__single`` (i.e., ``void
foo(int *__single *__single)``) (or a pointer to ``int *__unsafe_indexable`` if
it's from a system header). The compiler reports an error for casts between
pointers whose elements have incompatible pointer attributes. This way,
``-fbounds-safety`` prevents pointers that are implicitly ``__bidi_indexable``
from silently escaping thereby breaking the ABI.

.. code-block:: c

   void foo(int **);

   void bar(void) {
      int *local = 0;
      // error: passing 'int *__bidi_indexable*__bidi_indexable' to parameter of
      // incompatible nested pointer type 'int *__single*__single'
      foo(&local);
   }

A local variable may still be exposed to the ABI if ``typeof()`` takes the type
of local variable to define an interface as shown in the following example.

.. code-block:: C

   // bar.c
   void bar(int *) { ... }

   // foo.c
   void foo(void) {
      int *p; // implicitly `int *__bidi_indexable p`
      extern void bar(typeof(p)); // creates an interface of type
                                  // `void bar(int *__bidi_indexable)`
   }

Doing this may break the ABI if the parameter is not ``__bidi_indexable`` at the
definition of function ``bar()`` which is likely the case because parameters are
``__single`` by default without an explicit annotation.

In order to avoid an implicitly wide pointer from silently breaking the ABI, the
compiler reports a warning when ``typeof()`` is used on an implicit wide pointer
at any ABI visible context (e.g., function prototype, struct definition, etc.).

.. _Default pointer types in typeof:

Default pointer types in ``typeof()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When ``typeof()`` takes an expression, it respects the bounds annotation on
the expression type, including the bounds annotation is implcit. For example,
the global variable ``g`` in the following code is implicitly ``__single`` so
``typeof(g)`` gets ``char *__single``. The similar is true for the parameter
``p``, so ``typeof(p)`` returns ``void *__single``. The local variable ``l`` is
implicitly ``__bidi_indexable``, so ``typeof(l)`` becomes
``int *__bidi_indexable``.

.. code-block:: C

   char *g; // typeof(g) == char *__single

   void foo(void *p) {
      // typeof(p) == void *__single

      int *l; // typeof(l) == int *__bidi_indexable
   }

When the type of expression has an "external" bounds annotation, e.g.,
``__sized_by``, ``__counted_by``, etc., the compiler may report an error on
``typeof`` if the annotation creates a dependency with another declaration or
variable. For example, the compiler reports an error on ``typeof(p1)`` shown in
the following code because allowing it can potentially create another type
dependent on the parameter ``size`` in a different context (Please note that an
external bounds annotation on a parameter may only refer to another parameter of
the same function). On the other hand, ``typeof(p2)`` works resulting in ``int
*__counted_by(10)``, since it doesn't depend on any other declaration.

.. TODO: add a section describing constraints on external bounds annotations

.. code-block:: C

   void foo(int *__counted_by(size) p1, size_t size) {
      // typeof(p1) == int *__counted_by(size)
      // -> a compiler error as it tries to create another type
      // dependent on `size`.

      int *__counted_by(10) p2; // typeof(p2) == int *__counted_by(10)
                                // -> no error

   }

When ``typeof()`` takes a type name, the compiler doesn't apply an implicit
bounds annotation on the named pointer types. For example, ``typeof(int*)``
returns ``int *`` without any bounds annotation. A bounds annotation may be
added after the fact depending on the context. In the following example,
``typeof(int *)`` returns ``int *`` so it's equivalent as the local variable is
declared as ``int *l``, so it eventually becomes implicitly
``__bidi_indexable``.

.. code-block:: c

   void foo(void) {
      typeof(int *) l; // `int *__bidi_indexable` (same as `int *l`)
   }

The programmers can still explicitly add a bounds annotation on the types named
inside ``typeof``, e.g., ``typeof(int *__bidi_indexable)``, which evaluates to
``int *__bidi_indexable``.


Default pointer types in ``sizeof()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When ``sizeof()`` takes a type name, the compiler doesn't apply an implicit
bounds annotation on the named pointer types. This means if a bounds annotation
is not specified, the evaluated pointer type is treated identically to a plain C
pointer type. Therefore, ``sizeof(int*)`` remains the same with or without
``-fbounds-safety``. That said, programmers can explicitly add attribute to the
types, e.g., ``sizeof(int *__bidi_indexable)``, in which case the sizeof
evaluates to the size of type ``int *__bidi_indexable`` (the value equivalent to
``3 * sizeof(int*)``).

When ``sizeof()`` takes an expression, i.e., ``sizeof(expr``, it behaves as
``sizeof(typeof(expr))``, except that ``sizeof(expr)`` does not report an error
with ``expr`` that has a type with an external bounds annotation dependent on
another declaration, whereas ``typeof()`` on the same expression would be an
error as described in :ref:`Default pointer types in typeof`.
The following example describes this behavior.

.. code-block:: c

   void foo(int *__counted_by(size) p, size_t size) {
      // sizeof(p) == sizeof(int *__counted_by(size)) == sizeof(int *)
      // typeof(p): error
   };

Default pointer types in ``alignof()``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``alignof()`` only takes a type name as the argument and it doesn't take an
expression. Similar to ``sizeof()`` and ``typeof``, the compiler doesn't apply
an implicit bounds annotation on the pointer types named inside ``alignof()``.
Therefore, ``alignof(T *)`` remains the same with or without
``-fbounds-safety``, evaluating into the alignment of the raw pointer ``T *``.
The programmers can explicitly add a bounds annotation to the types, e.g.,
``alignof(int *__bidi_indexable)``, which returns the alignment of ``int
*__bidi_indexable``. A bounds annotation including an internal bounds annotation
(i.e., ``__indexable`` and ``__bidi_indexable``) doesn't affect the alignment of
the original pointer. Therefore, ``alignof(int *__bidi_indexable)`` is equal to
``alignof(int *)``.


Default pointer types used in C-style casts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A pointer type used in a C-style cast (e.g., ``(int *)src``) inherits the same
pointer attribute in the type of src. For instance, if the type of src is ``T
*__single`` (with ``T`` being an arbitrary C type), ``(int *)src`` will be ``int
*__single``. The reasoning behind this behavior is so that a C-style cast
doesn't introduce any unexpected side effects caused by an implicit cast of
bounds attribute.

Pointer casts can have explicit bounds annotations. For instance, ``(int
*__bidi_indexable)src`` casts to ``int *__bidi_indexable`` as long as src has a
bounds annotation that can implicitly convert to ``__bidi_indexable``. If
``src`` has type ``int *__single``, it can implicitly convert to ``int
*__bidi_indexable`` which then will have the upper bound pointing to one past
the first element. However, if src has type ``int *__unsafe_indexable``, the
explicit cast ``(int *__bidi_indexable)src`` will cause an error because
``__unsafe_indexable`` cannot cast to ``__bidi_indexable`` as
``__unsafe_indexable`` doesn't have bounds information. `Cast rules`_ describes
in more detail what kinds of casts are allowed between pointers with different
bounds annotations.

Default pointer types in typedef
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Pointer types in ``typedef``\s do not have implicit default bounds annotations.
Instead, the bounds annotation is determined when the ``typedef`` is used. The
following example shows that no pointer annotation is specified in the ``typedef
pint_t`` while each instance of ``typedef``'ed pointer gets its bounds
annotation based on the context in which the type is used.

.. code-block:: c

   typedef int * pint_t; // int *

   pint_t glob; // int *__single glob;

   void foo(void) {
      pint_t local; // int *__bidi_indexable local;
   }

Pointer types in a ``typedef`` can still have explicit annotations, e.g.,
``typedef int *__single``, in which case the bounds annotation ``__single`` will
apply to every use of the ``typedef``.

Array to pointer promotion to secure arrays (including VLAs)
------------------------------------------------------------

Arrays on function prototypes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In C, arrays on function prototypes are promoted (or "decayed") to a pointer to
its first element (e.g., ``&arr[0]``). In ``-fbounds-safety``, arrays are also
decayed to pointers, but with the addition of an implicit bounds annotation,
which includes variable-length arrays (VLAs). As shown in the following example,
arrays on function prototypes are decalyed to corresponding ``__counted_by``
pointers.

.. code-block:: c

   // Function prototype: void foo(int n, int *__counted_by(n) arr);
   void foo(int n, int arr[n]);

   // Function prototype: void bar(int *__counted_by(10) arr);
   void bar(int arr[10]);

This means the array parameters are treated as `__counted_by` pointers within
the function and callers of the function also see them as the corresponding
`__counted_by` pointers.

Incomplete arrays on function prototypes will cause a compiler error unless it
has ``__counted_by`` annotation in its bracket.

.. code-block:: c

   void f1(int n, int arr[]); // error

   void f3(int n, int arr[__counted_by(n)]); // ok

   void f2(int n, int arr[n]); // ok, decays to int *__counted_by(n)

   void f4(int n, int *__counted_by(n) arr); // ok

   void f5(int n, int *arr); // ok, but decays to int *__single,
                             // and cannot be used for pointer arithmetic

Array references
^^^^^^^^^^^^^^^^

In C, similar to arrays on the function prototypes, a reference to array is
automatically promoted (or "decayed") to a pointer to its first element (e.g.,
``&arr[0]``).

In `-fbounds-safety`, array references are promoted to ``__bidi_indexable``
pointers which contain the upper and lower bounds of the array, with the
equivalent of ``&arr[0]`` serving as the lower bound and ``&arr[array_size]``
(or one past the last element) serving as the upper bound. This applies to all
types of arrays including constant-length arrays, variable-length arrays (VLAs),
and flexible array members annotated with `__counted_by`.

In the following example, reference to ``vla`` promotes to ``int
*__bidi_indexable``, with ``&vla[n]`` as the upper bound and ``&vla[0]`` as the
lower bound. Then, it's copied to ``int *p``, which is implicitly ``int
*__bidi_indexable p``. Please note that value of ``n`` used to create the upper
bound is ``10``, not ``100``, in this case because ``10`` is the actual length
of ``vla``, the value of ``n`` at the time when the array is being allocated.

.. code-block:: c

   void foo(void) {
      int n = 10;
      int vla[n];
      n = 100;
      int *p = vla; // { .ptr: &vla[0], .upper: &vla[10], .lower: &vla[0] }
                    // it's `&vla[10]` because the value of `n` was 10 at the
                    // time when the array is actually allocated.
      // ...
   }

By promoting array references to ``__bidi_indexable``, all array accesses are
bounds checked in ``-fbounds-safety``, just as ``__bidi_indexable`` pointers
are.

Maintaining correctness of bounds annotations
---------------------------------------------

``-fbounds-safety`` maintains correctness of bounds annotations by performing
additional checks when a pointer object and/or its related value containing the
bounds information is updated.

For example, ``__single`` expresses an invariant that the pointer must either
point to a single valid object or be a null pointer. To maintain this invariant,
the compiler inserts checks when initializing a ``__single`` pointer, as shown
in the following example:

.. code-block:: c

   void foo(void *__sized_by(size) vp, size_t size) {
      // Inserted check:
      // if ((int*)upper_bound(vp) - (int*)vp < sizeof(int) && !!vp) trap();
      int *__single ip = (int *)vp;
   }

Additionally, an explicit bounds annotation such as ``int *__counted_by(count)
buf`` defines a relationship between two variables, ``buf`` and ``count``:
namely, that ``buf`` has ``count`` number of elements available. This
relationship must hold even after any of these related variables are updated. To
this end, the model requires that assignments to ``buf`` and ``count`` must be
side by side, with no side effects between them. This prevents ``buf`` and
``count`` from temporarily falling out of sync due to updates happening at a
distance.

The example below shows a function ``alloc_buf`` that initializes a struct that
members that use the ``__counted_by`` annotation. The compiler allows these
assignments because ``sbuf->buf`` and ``sbuf->count`` are updated side by side
without any side effects in between the assignments.

Furthermore, the compiler inserts additional run-time checks to ensure the new
``buf`` has at least as many elements as the new ``count`` indicates as shown in
the transformed pseudo code of function ``alloc_buf()`` in the example below.

.. code-block:: c

   typedef struct {
      int *__counted_by(count) buf;
      size_t count;
   } sized_buf_t;

   void alloc_buf(sized_buf_t *sbuf, sized_t nelems) {
      sbuf->buf = (int *)malloc(sizeof(int) * nelems);
      sbuf->count = nelems;
   }

   // Transformed pseudo code:
   void alloc_buf(sized_buf_t *sbuf, sized_t nelems) {
      // Materialize RHS values:
      int *tmp_ptr = (int *)malloc(sizeof(int) * nelems);
      int tmp_count = nelems;
      // Inserted check:
      //   - checks to ensure that `lower <= tmp_ptr <= upper`
      //   - if (upper(tmp_ptr) - tmp_ptr < tmp_count) trap();
      sbuf->buf = tmp_ptr;
      sbuf->count = tmp_count;
   }

Whether the compiler can optimize such run-time checks depends on how the upper
bound of the pointer is derived. If the source pointer has ``__sized_by``,
``__counted_by``, or a variant of such, the compiler assumes that the upper
bound calculation doesn't overflow, e.g., ``ptr + size`` (where the type of
``ptr`` is ``void *__sized_by(size)``), because when the ``__sized_by`` pointer
is initialized, ``-fbounds-safety`` inserts run-time checks to ensure that ``ptr
+ size`` doesn't overflow and that ``size >= 0``.

Assuming the upper bound calculation doesn't overflow, the compiler can simplify
the trap condition ``upper(tmp_ptr) - tmp_ptr < tmp_count`` to ``size <
tmp_count`` so if both ``size`` and ``tmp_count`` values are known at compile
time such that ``0 <= tmp_count <= size``, the optimizer can remove the check.

``ptr + size`` may still overflow if the ``__sized_by`` pointer is created from
code that doesn't enable ``-fbounds-safety``, which is undefined behavior.

In the previous code example with the transformed ``alloc_buf()``, the upper
bound of ``tmp_ptr`` is derived from ``void *__sized_by_or_null(size)``, which
is the return type of ``malloc()``. Hence, the pointer arithmetic doesn't
overflow or ``tmp_ptr`` is null. Therefore, if ``nelems`` was given as a
compile-time constant, the compiler could remove the checks.

Cast rules
----------

``-fbounds-safety`` does not enforce overall type safety and bounds invariants
can still be violated by incorrect casts in some cases. That said,
``-fbounds-safety`` prevents type conversions that change bounds attributes in a
way to violate the bounds invariant of the destination's pointer annotation.
Type conversions that change bounds attributes may be allowed if it does not
violate the invariant of the destination or that can be verified at run time.
Here are some of the important cast rules.

Two pointers that have different bounds annotations on their nested pointer
types are incompatible and cannot implicitly cast to each other. For example,
``T *__single *__single`` cannot be converted to ``T *__bidi_indexable
*__single``. Such a conversion between incompatible nested bounds annotations
can be allowed using an explicit cast (e.g., C-style cast). Hereafter, the rules
only apply to the top pointer types. ``__unsafe_indexable`` cannot be converted
to any other safe pointer types (``__single``, ``__bidi_indexable``,
``__counted_by``, etc) using a cast. The extension provides builtins to force
this conversion, ``__unsafe_forge_bidi_indexable(type, pointer, char_count)`` to
convert pointer to a ``__bidi_indexable`` pointer of type with ``char_count``
bytes available and ``__unsafe_forge_single(type, pointer)`` to convert pointer
to a single pointer of type type. The following examples show the usage of these
functions. Function ``example_forge_bidi()`` gets an external buffer from an
unsafe library by calling ``get_buf()`` which returns ``void
*__unsafe_indexable.`` Under the type rules, this cannot be directly assigned to
``void *buf`` (implicitly ``void *__bidi_indexable``). Thus,
``__unsafe_forge_bidi_indexable`` is used to manually create a
``__bidi_indexable`` from the unsafe buffer.

.. code-block:: c

   // unsafe_library.h
   void *__unsafe_indexable get_buf(void);
   size_t get_buf_size(void);

   // my_source1.c (enables -fbounds-safety)
   #include "unsafe_library.h"
   void example_forge_bidi(void) {
      void *buf =
        __unsafe_forge_bidi_indexable(void *, get_buf(), get_buf_size());
      // ...
   }

   // my_source2.c (enables -fbounds-safety)
   #include <stdio.h>
   void example_forge_single(void) {
      FILE *fp = __unsafe_forge_single(FILE *, fopen("mypath", "rb"));
      // ...
   }

* Function ``example_forge_single`` takes a file handle by calling fopen defined
  in system header ``stdio.h``. Assuming ``stdio.h`` did not adopt
  ``-fbounds-safety``, the return type of ``fopen`` would implicitly be ``FILE
  *__unsafe_indexable`` and thus it cannot be directly assigned to ``FILE *fp``
  in the bounds-safe source. To allow this operation, ``__unsafe_forge_single``
  is used to create a ``__single`` from the return value of ``fopen``.

* Similar to ``__unsafe_indexable``, any non-pointer type (including ``int``,
  ``intptr_t``, ``uintptr_t``, etc.) cannot be converted to any safe pointer
  type because these don't have bounds information. ``__unsafe_forge_single`` or
  ``__unsafe_forge_bidi_indexable`` must be used to force the conversion.

* Any safe pointer types can cast to ``__unsafe_indexable`` because it doesn't
  have any invariant to maintain.

* ``__single`` casts to ``__bidi_indexable`` if the pointee type has a known
  size. After the conversion, the resulting ``__bidi_indexable`` has the size of
  a single object of the pointee type of ``__single``. ``__single`` cannot cast
  to ``__bidi_indexable`` if the pointee type is incomplete or sizeless. For
  example, ``void *__single`` cannot convert to ``void *__bidi_indexable``
  because void is an incomplete type and thus the compiler cannot correctly
  determine the upper bound of a single void pointer.

* Similarly, ``__single`` can cast to ``__indexable`` if the pointee type has a
  known size. The resulting ``__indexable`` has the size of a single object of
  the pointee type.

* ``__single`` casts to ``__counted_by(E)`` only if ``E`` is 0 or 1.

* ``__single`` can cast to ``__single`` including when they have different
  pointee types as long as it is allowed in the underlying C standard.
  ``-fbounds-safety`` doesn't guarantee type safety.

* ``__bidi_indexable`` and ``__indexable`` can cast to ``__single``. The
  compiler may insert run-time checks to ensure the pointer has at least a
  single element or is a null pointer.

* ``__bidi_indexable`` casts to ``__indexable`` if the pointer does not have an
  underflow. The compiler may insert run-time checks to ensure the pointer is
  not below the lower bound.

* ``__indexable`` casts to ``__bidi_indexable``. The resulting
  ``__bidi_indexable`` gets the lower bound same as the pointer value.

* A type conversion may involve both a bitcast and a bounds annotation cast. For
  example, casting from ``int *__bidi_indexable`` to ``char *__single`` involve
  a bitcast (``int *`` to ``char *``) and a bounds annotation cast
  (``__bidi_indexable`` to ``__single``). In this case, the compiler performs
  the bitcast and then converts the bounds annotation. This means, ``int
  *__bidi_indexable`` will be converted to ``char *__bidi_indexable`` and then
  to ``char *__single``.

* ``__terminated_by(T)`` cannot cast to any safe pointer type without the same
  ``__terminated_by(T)`` attribute. To perform the cast, programmers can use an
  intrinsic function such as ``__unsafe_terminated_by_to_indexable(P)`` to force
  the conversion.

* ``__terminated_by(T)`` can cast to ``__unsafe_indexable``.

* Any type without ``__terminated_by(T)`` cannot cast to ``__terminated_by(T)``
  without explicitly using an intrinsic function to allow it.

  + ``__unsafe_terminated_by_from_indexable(T, PTR [, PTR_TO_TERM])`` casts any
    safe pointer PTR to a ``__terminated_by(T)`` pointer. ``PTR_TO_TERM`` is an
    optional argument where the programmer can provide the exact location of the
    terminator. With this argument, the function can skip reading the entire
    array in order to locate the end of the pointer (or the upper bound).
    Providing an incorrect ``PTR_TO_TERM`` causes a run-time trap.

  + ``__unsafe_forge_terminated_by(T, P, E)`` creates ``T __terminated_by(E)``
    pointer given any pointer ``P``. Tmust be a pointer type.

Portability with toolchains that do not support the extension
-------------------------------------------------------------

The language model is designed so that it doesn't alter the semantics of the
original C program, other than introducing deterministic traps where otherwise
the behavior is undefined and/or unsafe. Clang provides a toolchain header
(``ptrcheck.h``) that macro-defines the annotations as type attributes when
``-fbounds-safety`` is enabled and defines them to empty when the extension is
disabled. Thus, the code adopting ``-fbounds-safety`` can compile with
toolchains that do not support this extension, by including the header or adding
macros to define the annotations to empty. For example, the toolchain not
supporting this extension may not have a header defining ``__counted_by``, so
the code using ``__counted_by`` must define it as nothing or include a header
that has the define.

.. code-block:: c

   #if defined(__has_feature) && __has_feature(bounds_safety)
   #define __counted_by(T) __attribute__((__counted_by__(T)))
   // ... other bounds annotations
   #else #define __counted_by(T) // defined as nothing
   // ... other bounds annotations
   #endif

   // expands to `void foo(int * ptr, size_t count);`
   // when extension is not enabled or not available
   void foo(int *__counted_by(count) ptr, size_t count);

Other potential applications of bounds annotations
==================================================

The bounds annotations provided by the ``-fbounds-safety`` programming model
have potential use cases beyond the language extension itself. For example,
static and dynamic analysis tools could use the bounds information to improve
diagnostics for out-of-bounds accesses, even if ``-fbounds-safety`` is not used.
The bounds annotations could be used to improve C interoperability with
bounds-safe languages, providing a better mapping to bounds-safe types in the
safe language interface. The bounds annotations can also serve as documentation
specifying the relationship between declarations.

Limitations
===========

``-fbounds-safety`` aims to bring the bounds safety guarantee to the C language,
and it does not guarantee other types of memory safety properties. Consequently,
it may not prevent some of the secondary bounds safety violations caused by
other types of safety violations such as type confusion. For instance,
``-fbounds-safety`` does not perform type-safety checks on conversions between
`__single`` pointers of different pointee types (e.g., ``char *__single`` →
``void *__single`` → ``int *__single``) beyond what the foundation languages
(C/C++) already offer.

``-fbounds-safety`` heavily relies on run-time checks to keep the bounds safety
and the soundness of the type system. This may incur significant code size
overhead in unoptimized builds and leaving some of the adoption mistakes to be
caught only at run time. This is not a fundamental limitation, however, because
incrementally adding necessary static analysis will allow us to catch issues
early on and remove unnecessary bounds checks in unoptimized builds.