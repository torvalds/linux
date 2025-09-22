==================
Matrix Types
==================

.. contents::
   :local:

.. _matrixtypes:

Clang provides a C/C++ language extension that allows users to directly express
fixed-size 2-dimensional matrices as language values and perform arithmetic on
them.

This feature is currently experimental, and both its design and its
implementation are in flux.

Draft Specification
===================

Matrix Type
-----------

A matrix type is a scalar type with an underlying *element type*, a constant
number of *rows*, and a constant number of *columns*. Matrix types with the same
element type, rows, and columns are the same type. A value of a matrix type
includes storage for ``rows * columns`` values of the *element type*. The
internal layout, overall size and alignment are implementation-defined.

The maximum of the product of the number of rows and columns is
implementation-defined. If that implementation-defined limit is exceeded, the
program is ill-formed.

Currently, the element type of a matrix is only permitted to be one of the
following types:

* an integer type (as in C23 6.2.5p22), but excluding enumerated types and ``bool``
* the standard floating types ``float`` or ``double``
* a half-precision floating point type, if one is supported on the target

Other types may be supported in the future.

Matrix Type Attribute
---------------------

Matrix types can be declared by adding the ``matrix_type`` attribute to the
declaration of a *typedef* (or a C++ alias declaration). The underlying type
of the *typedef* must be a valid matrix element type. The
attribute takes two arguments, both of which must be integer constant
expressions that evaluate to a value greater than zero. The first specifies the
number of rows, and the second specifies the number of columns. The underlying
type of the *typedef* becomes a matrix type with the given dimensions and an
element type of the former underlying type.

If a declaration of a *typedef-name* has a ``matrix_type`` attribute, then all
declaration of that *typedef-name* shall have a matrix_type attribute with the
same element type, number of rows, and number of columns.

Standard Conversions
--------------------

The standard conversions are extended as follows. Note that these conversions
are intentionally not listed as satisfying the constraints for assignment,
which is to say, they are only permitted as explicit casts, not as implicit
conversions.

A value of matrix type can be converted to another matrix type if the number of
rows and columns are the same and the value's elements can be converted to the
element type of the result type. The result is a matrix where each element is
the converted corresponding element.

A value of any real type (as in C23 6.2.5p14) can be converted to a matrix type
if it can be converted to the element type of the matrix. The result is a
matrix where all elements are the converted original value.

If the number of rows or columns differ between the original and resulting
type, the program is ill-formed.


Arithmetic Conversions
----------------------

The usual arithmetic conversions are extended as follows.

Insert at the start:

* If both operands are of matrix type, no arithmetic conversion is performed.
* If one operand is of matrix type and the other operand is of a real type,
  convert the real type operand to the matrix type
  according to the standard conversion rules.

Matrix Type Element Access Operator
-----------------------------------

An expression of the form ``E1 [E2] [E3]``, where ``E1`` has matrix type ``cv
M``, is a matrix element access expression.  Let ``T`` be the element type
of ``M``, and let ``R`` and ``C`` be the number of rows and columns in ``M``
respectively.  The index expressions shall have integral or unscoped
enumeration type and shall not be uses of the comma operator unless
parenthesized.  The first index expression shall evaluate to a
non-negative value less than ``R``, and the second index expression shall
evaluate to a non-negative value less than ``C``, or else the expression has
undefined behavior.  If ``E1`` is a prvalue, the result is a prvalue with type
``T`` and is the value of the element at the given row and column in the matrix.
Otherwise, the result is a glvalue with type ``cv T`` and with the same value
category as ``E1`` which refers to the element at the given row and column in
the matrix.

Programs containing a single subscript expression into a matrix are ill-formed.

**Note**: We considered providing an expression of the form
``postfix-expression [expression]`` to access columns of a matrix. We think
that such an expression would be problematic once both column and row major
matrixes are supported: depending on the memory layout, either accessing columns
or rows can be done efficiently, but not both. Instead, we propose to provide
builtins to extract rows and columns from a matrix. This makes the operations
more explicit.

Matrix Type Binary Operators
----------------------------

Given two matrixes, the ``+`` and ``-`` operators perform element-wise addition
and subtraction, while the ``*`` operator performs matrix multiplication.
``+``, ``-``, ``*``, and ``/`` can also be used with a matrix and a scalar
value, applying the operation to each element of the matrix.

Earlier versions of this extension did not support division by a scalar.
You can test for the availability of this feature with
``__has_extension(matrix_types_scalar_division)``.

For the expression ``M1 BIN_OP M2`` where

* ``BIN_OP`` is one of ``+`` or ``-``, one of ``M1`` and ``M2`` is of matrix
  type, and the other is of matrix type or real type; or
* ``BIN_OP`` is ``*``, one of ``M1`` and ``M2`` is of matrix type, and the
   other is of a real type; or
* ``BIN_OP`` is ``/``, ``M1`` is of matrix type, and ``M2`` is of a real type:

* The usual arithmetic conversions are applied to ``M1`` and ``M2``. [ Note: if ``M1`` or
  ``M2`` are of a real type, they are broadcast to matrices here. — end note ]
* ``M1`` and ``M2`` shall be of the same matrix type.
* The result is equivalent to Res in the following where col is the number of
  columns and row is the number of rows in the matrix type:

.. code-block:: c++

  decltype(M1) Res;
  for (int C = 0; C < col; ++C)
    for (int R = 0; R < row; ++R)
      Res[R][C] = M1[R][C] BIN_OP M2[R][C];

Given the expression ``M1 * M2`` where ``M1`` and ``M2`` are of matrix type:

* The usual arithmetic conversions are applied to ``M1`` and ``M2``.
* The type of ``M1`` shall have the same number of columns as the type of ``M2`` has
  rows. The element types of ``M1`` and ``M2`` shall be the same type.
* The resulting type, ``MTy``, is a matrix type with the common element type,
  the number of rows of ``M1`` and the number of columns of ``M2``.
* The result is equivalent to ``Res`` in the following where ``EltTy`` is the
  element type of ``MTy``, ``col`` is the number of columns, ``row`` is the
  number of rows in ``MTy`` and ``inner`` is the number of columns of ``M1``:

.. code-block:: c++

  MTy Res;
  for (int C = 0; C < col; ++C) {
    for (int R = 0; R < row; ++R) {
      EltTy Elt = 0;
      for (int K = 0; K < inner; ++K) {
        Elt += M1[R][K] * M2[K][C];
    }
    Res[R][C] = Elt;
  }

All operations on matrix types match the behavior of the element type with
respect to signed overflows.

With respect to floating-point contraction, rounding and environment rules,
operations on matrix types match the behavior of the elementwise operations
in the corresponding expansions provided above.

Operations on floating-point matrices have the same rounding and floating-point
environment behavior as ordinary floating-point operations in the expression's
context. For the purposes of floating-point contraction, all calculations done
as part of a matrix operation are considered intermediate operations, and their
results need not be rounded to the format of the element type until the final
result in the containing expression. This is subject to the normal restrictions
on contraction, such as ``#pragma STDC FP_CONTRACT``.

For the ``+=``, ``-=`` and ``*=`` operators the semantics match their expanded
variants.

Matrix Type Builtin Operations
------------------------------

Each matrix type supports a collection of builtin expressions that look like
function calls but do not form an overload set. Here they are described as
function declarations with rules for how to construct the argument list types
and return type and the library description elements from
[library.description.structure.specifications]/3 in the C++ standard.

Definitions:

* *M*, *M1*, *M2*, *M3* - Matrix types
* *T* - Element type
* *row*, *col* - Row and column arguments respectively.


``M2 __builtin_matrix_transpose(M1 matrix)``

**Remarks**: The return type is a cv-unqualified matrix type that has the same
element type as ``M1`` and has the same number of rows as ``M1`` has columns and
the same number of columns as ``M1`` has rows.

**Returns**: A matrix ``Res`` equivalent to the code below, where ``col`` refers to the
number of columns of ``M``, and ``row`` to the number of rows of ``M``.

**Effects**: Equivalent to:

.. code-block:: c++

  M Res;
  for (int C = 0; C < col; ++C)
    for (int R = 0; R < row; ++R)
      Res[C][R] = matrix[R][C];


``M __builtin_matrix_column_major_load(T *ptr, size_t row, size_t col, size_t columnStride)``

**Mandates**: ``row`` and ``col`` shall be integral constants greater than 0.

**Preconditions**: ``columnStride`` is greater than or equal to ``row``.

**Remarks**: The return type is a cv-unqualified matrix type with an element
type of the cv-unqualified version of ``T`` and a number of rows and columns equal
to ``row`` and ``col`` respectively. The parameter ``columnStride`` is optional
and if omitted ``row`` is used as ``columnStride``.

**Returns**: A matrix ``Res`` equivalent to:

.. code-block:: c++

  M Res;
  for (size_t C = 0; C < col; ++C) {
    for (size_t R = 0; R < row; ++K)
      Res[R][C] = ptr[R];
    ptr += columnStride
  }


``void __builtin_matrix_column_major_store(M matrix, T *ptr, size_t columnStride)``

**Preconditions**: ``columnStride`` is greater than or equal to the number of rows in ``M``.

**Remarks**: The type ``T`` is the const-unqualified version of the matrix
argument’s element type. The parameter ``columnStride`` is optional and if
omitted, the number of rows of ``M`` is used as ``columnStride``.

**Effects**: Equivalent to:

.. code-block:: c++

  for (size_t C = 0; C < columns in M; ++C) {
    for (size_t R = 0; R < rows in M; ++K)
      ptr[R] = matrix[R][C];
    ptr += columnStride
  }


TODOs
-----

TODO: Does it make sense to allow M::element_type, M::rows, and M::columns
where M is a matrix type? We don’t support this anywhere else, but it’s
convenient. The alternative is using template deduction to extract this
information. Also add spelling for C.

Future Work: Initialization syntax.


Decisions for the Implementation in Clang
=========================================

This section details decisions taken for the implementation in Clang and is not
part of the draft specification.

The elements of a  value of a matrix type are laid out in column-major order
without padding.

We propose to provide a Clang option to override this behavior and allow
contraction of those operations (e.g. *-ffp-contract=matrix*).

TODO: Specify how matrix values are passed to functions.
