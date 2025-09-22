============================================
Implementation plans for ``-fbounds-safety``
============================================

.. contents::
   :local:

Gradual updates with experimental flag
======================================

The feature will be implemented as a series of smaller PRs and we will guard our
implementation with an experimental flag ``-fexperimental-bounds-safety`` until
the usable model is fully available. Once the model is ready for use, we will
expose the flag ``-fbounds-safety``.

Possible patch sets
-------------------

* External bounds annotations and the (late) parsing logic.
* Internal bounds annotations (wide pointers) and their parsing logic.
* Clang code generation for wide pointers with debug information.
* Pointer cast semantics involving bounds annotations (this could be divided
  into multiple sub-PRs).
* CFG analysis for pairs of related pointer and count assignments and the likes.
* Bounds check expressions in AST and the Clang code generation (this could also
  be divided into multiple sub-PRs).

Proposed implementation
=======================

External bounds annotations
---------------------------

The bounds annotations are C type attributes appertaining to pointer types. If
an attribute is added to the position of a declaration attribute, e.g., ``int
*ptr __counted_by(size)``, the attribute appertains to the outermost pointer
type of the declaration (``int *``).

New sugar types
---------------

An external bounds annotation creates a type sugar of the underlying pointer
types. We will introduce a new sugar type, ``DynamicBoundsPointerType`` to
represent ``__counted_by`` or ``__sized_by``. Using ``AttributedType`` would not
be sufficient because the type needs to hold the count or size expression as
well as some metadata necessary for analysis, while this type may be implemented
through inheritance from ``AttributedType``. Treating the annotations as type
sugars means two types with incompatible external bounds annotations may be
considered canonically the same types. This is sometimes necessary, for example,
to make the ``__counted_by`` and friends not participate in function
overloading. However, this design requires a separate logic to walk through the
entire type hierarchy to check type compatibility of bounds annotations.

Late parsing for C
------------------

A bounds annotation such as ``__counted_by(count)`` can be added to type of a
struct field declaration where count is another field of the same struct
declared later. Similarly, the annotation may apply to type of a function
parameter declaration which precedes the parameter count in the same function.
This means parsing the argument of bounds annotations must be done after the
parser has the whole context of a struct or a function declaration. Clang has
late parsing logic for C++ declaration attributes that require late parsing,
while the C declaration attributes and C/C++ type attributes do not have the
same logic. This requires introducing late parsing logic for C/C++ type
attributes.

Internal bounds annotations
---------------------------

``__indexable`` and ``__bidi_indexable`` alter pointer representations to be
equivalent to a struct with the pointer and the corresponding bounds fields.
Despite this difference in their representations, they are still pointers in
terms of types of operations that are allowed and their semantics. For instance,
a pointer dereference on a ``__bidi_indexable`` pointer will return the
dereferenced value same as plain C pointers, modulo the extra bounds checks
being performed before dereferencing the wide pointer. This means mapping the
wide pointers to struct types with equivalent layout won’t be sufficient. To
represent the wide pointers in Clang AST, we add an extra field in the
PointerType class to indicate the internal bounds of the pointer. This ensures
pointers of different representations are mapped to different canonical types
while they are still treated as pointers.

In LLVM IR, wide pointers will be emitted as structs of equivalent
representations. Clang CodeGen will handle them as Aggregate in
``TypeEvaluationKind (TEK)``. ``AggExprEmitter`` was extended to handle pointer
operations returning wide pointers. Alternatively, a new ``TEK`` and an
expression emitter dedicated to wide pointers could be introduced.

Default bounds annotations
--------------------------

The model may implicitly add ``__bidi_indexable`` or ``__single`` depending on
the context of the declaration that has the pointer type. ``__bidi_indexable``
implicitly adds to local variables, while ``__single`` implicitly adds to
pointer types specifying struct fields, function parameters, or global
variables. This means the parser may first create the pointer type without any
default pointer attribute and then recreate the type once the parser has the
declaration context and determined the default attribute accordingly.

This also requires the parser to reset the type of the declaration with the
newly created type with the right default attribute.

Promotion expression
--------------------

A new expression will be introduced to represent the conversion from a pointer
with an external bounds annotation, such as ``__counted_by``, to
``__bidi_indexable``. This type of conversion cannot be handled by normal
CastExprs because it requires an extra subexpression(s) to provide the bounds
information necessary to create a wide pointer.

Bounds check expression
-----------------------

Bounds checks are part of semantics defined in the ``-fbounds-safety`` language
model. Hence, exposing the bounds checks and other semantic actions in the AST
is desirable. A new expression for bounds checks has been added to the AST. The
bounds check expression has a ``BoundsCheckKind`` to indicate the kind of checks
and has the additional sub-expressions that are necessary to perform the check
according to the kind.

Paired assignment check
-----------------------

``-fbounds-safety`` enforces that variables or fields related with the same
external bounds annotation (e.g., ``buf`` and ``count`` related with
``__counted_by`` in the example below) must be updated side by side within the
same basic block and without side effect in between.

.. code-block:: c

   typedef struct {
      int *__counted_by(count) buf; size_t count;
   } sized_buf_t;

   void alloc_buf(sized_buf_t *sbuf, sized_t nelems) {
      sbuf->buf = (int *)malloc(sizeof(int) * nelems);
      sbuf->count = nelems;
   }

To implement this rule, the compiler requires a linear representation of
statements to understand the ordering and the adjacency between the two or more
assignments. The Clang CFG is used to implement this analysis as Clang CFG
provides a linear view of statements within each ``CFGBlock`` (Clang
``CFGBlock`` represents a single basic block in a source-level CFG).

Bounds check optimizations
--------------------------

In ``-fbounds-safety``, the Clang frontend emits run-time checks for every
memory dereference if the type system or analyses in the frontend couldn’t
verify its bounds safety. The implementation relies on LLVM optimizations to
remove redundant run-time checks. Using this optimization strategy, if the
original source code already has bounds checks, the fewer additional checks
``-fbounds-safety`` will introduce. The LLVM ``ConstraintElimination`` pass is
design to remove provable redundant checks (please check Florian Hahn’s
presentation in 2021 LLVM Dev Meeting and the implementation to learn more). In
the following example, ``-fbounds-safety`` implicitly adds the redundant bounds
checks that the optimizer can remove:

.. code-block:: c

   void fill_array_with_indices(int *__counted_by(count) p, size_t count) {
      for (size_t i = 0; i < count; ++i) {
         // implicit bounds checks:
         //   if (p + i < p || p + i + 1 > p + count) trap();
         p[i] = i;
      }
   }

``ConstraintElimination`` collects the following facts and determines if the
bounds checks can be safely removed:

* Inside the for-loop, ``0 <= i < count``, hence ``1 <= i + 1 <= count``.
* Pointer arithmetic ``p + count`` in the if-condition doesn’t wrap.
* ``-fbounds-safety`` treats pointer arithmetic overflow as deterministically
  two’s complement computation, not an undefined behavior. Therefore,
  getelementptr does not typically have inbounds keyword. However, the compiler
  does emit inbounds for ``p + count`` in this case because
  ``__counted_by(count)`` has the invariant that p has at least as many as
  elements as count. Using this information, ``ConstraintElimination`` is able
  to determine ``p + count`` doesn’t wrap.
* Accordingly, ``p + i`` and ``p + i + 1`` also don’t wrap.
* Therefore, ``p <= p + i`` and ``p + i + 1 <= p + count``.
* The if-condition simplifies to false and becomes dead code that the subsequent
  optimization passes can remove.

``OptRemarks`` can be utilized to provide insights into performance tuning. It
has the capability to report on checks that it cannot eliminate, possibly with
reasons, allowing programmers to adjust their code to unlock further
optimizations.

Debugging
=========

Internal bounds annotations
---------------------------

Internal bounds annotations change a pointer into a wide pointer. The debugger
needs to understand that wide pointers are essentially pointers with a struct
layout. To handle this, a wide pointer is described as a record type in the
debug info. The type name has a special name prefix (e.g.,
``__bounds_safety$bidi_indexable``) which can be recognized by a debug info
consumer to provide support that goes beyond showing the internal structure of
the wide pointer. There are no DWARF extensions needed to support wide pointers.
In our implementation, LLDB recognizes wide pointer types by name and
reconstructs them as wide pointer Clang AST types for use in the expression
evaluator.

External bounds annotations
---------------------------

Similar to internal bounds annotations, external bound annotations are described
as a typedef to their underlying pointer type in the debug info, and the bounds
are encoded as strings in the typedef’s name (e.g.,
``__bounds_safety$counted_by:N``).

Recognizing ``-fbounds-safety`` traps
-------------------------------------

Clang emits debug info for ``-fbounds-safety`` traps as inlined functions, where
the function name encodes the error message. LLDB implements a frame recognizer
to surface a human-readable error cause to the end user. A debug info consumer
that is unaware of this sees an inlined function whose name encodes an error
message (e.g., : ``__bounds_safety$Bounds check failed``).

Expression Parsing
------------------

In our implementation, LLDB’s expression evaluator does not enable the
``-fbounds-safety`` language option because it’s currently unable to fully
reconstruct the pointers with external bounds annotations, and also because the
evaluator operates in C++ mode, utilizing C++ reference types, while
``-fbounds-safety`` does not currently support C++. This means LLDB’s expression
evaluator can only evaluate a subset of the ``-fbounds-safety`` language model.
Specifically, it’s capable of evaluating the wide pointers that already exist in
the source code. All other expressions are evaluated according to C/C++
semantics.

C++ support
===========

C++ has multiple options to write code in a bounds-safe manner, such as
following the bounds-safety core guidelines and/or using hardened libc++ along
with the `C++ Safe Buffer model
<https://discourse.llvm.org/t/rfc-c-buffer-hardening/65734>`_. However, these
techniques may require ABI changes and may not be applicable to code
interoperating with C. When the ABI of an existing program needs to be preserved
and for headers shared between C and C++, ``-fbounds-safety`` offers a potential
solution.

``-fbounds-safety`` is not currently supported in C++, but we believe the
general approach would be applicable for future efforts.
