=========================
Clang Language Extensions
=========================

.. contents::
   :local:
   :depth: 1

.. toctree::
   :hidden:

   ObjectiveCLiterals
   BlockLanguageSpec
   Block-ABI-Apple
   AutomaticReferenceCounting
   PointerAuthentication
   MatrixTypes

Introduction
============

This document describes the language extensions provided by Clang.  In addition
to the language extensions listed here, Clang aims to support a broad range of
GCC extensions.  Please see the `GCC manual
<https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html>`_ for more information on
these extensions.

.. _langext-feature_check:

Feature Checking Macros
=======================

Language extensions can be very useful, but only if you know you can depend on
them.  In order to allow fine-grain features checks, we support three builtin
function-like macros.  This allows you to directly test for a feature in your
code without having to resort to something like autoconf or fragile "compiler
version checks".

``__has_builtin``
-----------------

This function-like macro takes a single identifier argument that is the name of
a builtin function, a builtin pseudo-function (taking one or more type
arguments), or a builtin template.
It evaluates to 1 if the builtin is supported or 0 if not.
It can be used like this:

.. code-block:: c++

  #ifndef __has_builtin         // Optional of course.
    #define __has_builtin(x) 0  // Compatibility with non-clang compilers.
  #endif

  ...
  #if __has_builtin(__builtin_trap)
    __builtin_trap();
  #else
    abort();
  #endif
  ...

.. note::

  Prior to Clang 10, ``__has_builtin`` could not be used to detect most builtin
  pseudo-functions.

  ``__has_builtin`` should not be used to detect support for a builtin macro;
  use ``#ifdef`` instead.

``__has_constexpr_builtin``
---------------------------

This function-like macro takes a single identifier argument that is the name of
a builtin function, a builtin pseudo-function (taking one or more type
arguments), or a builtin template.
It evaluates to 1 if the builtin is supported and can be constant evaluated or
0 if not. It can be used for writing conditionally constexpr code like this:

.. code-block:: c++

  #ifndef __has_constexpr_builtin         // Optional of course.
    #define __has_constexpr_builtin(x) 0  // Compatibility with non-clang compilers.
  #endif

  ...
  #if __has_constexpr_builtin(__builtin_fmax)
    constexpr
  #endif
    double money_fee(double amount) {
        return __builtin_fmax(amount * 0.03, 10.0);
    }
  ...

For example, ``__has_constexpr_builtin`` is used in libcxx's implementation of
the ``<cmath>`` header file to conditionally make a function constexpr whenever
the constant evaluation of the corresponding builtin (for example,
``std::fmax`` calls ``__builtin_fmax``) is supported in Clang.

.. _langext-__has_feature-__has_extension:

``__has_feature`` and ``__has_extension``
-----------------------------------------

These function-like macros take a single identifier argument that is the name
of a feature.  ``__has_feature`` evaluates to 1 if the feature is both
supported by Clang and standardized in the current language standard or 0 if
not (but see :ref:`below <langext-has-feature-back-compat>`), while
``__has_extension`` evaluates to 1 if the feature is supported by Clang in the
current language (either as a language extension or a standard language
feature) or 0 if not.  They can be used like this:

.. code-block:: c++

  #ifndef __has_feature         // Optional of course.
    #define __has_feature(x) 0  // Compatibility with non-clang compilers.
  #endif
  #ifndef __has_extension
    #define __has_extension __has_feature // Compatibility with pre-3.0 compilers.
  #endif

  ...
  #if __has_feature(cxx_rvalue_references)
  // This code will only be compiled with the -std=c++11 and -std=gnu++11
  // options, because rvalue references are only standardized in C++11.
  #endif

  #if __has_extension(cxx_rvalue_references)
  // This code will be compiled with the -std=c++11, -std=gnu++11, -std=c++98
  // and -std=gnu++98 options, because rvalue references are supported as a
  // language extension in C++98.
  #endif

.. _langext-has-feature-back-compat:

For backward compatibility, ``__has_feature`` can also be used to test
for support for non-standardized features, i.e. features not prefixed ``c_``,
``cxx_`` or ``objc_``.

Another use of ``__has_feature`` is to check for compiler features not related
to the language standard, such as e.g. :doc:`AddressSanitizer
<AddressSanitizer>`.

If the ``-pedantic-errors`` option is given, ``__has_extension`` is equivalent
to ``__has_feature``.

The feature tag is described along with the language feature below.

The feature name or extension name can also be specified with a preceding and
following ``__`` (double underscore) to avoid interference from a macro with
the same name.  For instance, ``__cxx_rvalue_references__`` can be used instead
of ``cxx_rvalue_references``.

``__has_cpp_attribute``
-----------------------

This function-like macro is available in C++20 by default, and is provided as an
extension in earlier language standards. It takes a single argument that is the
name of a double-square-bracket-style attribute. The argument can either be a
single identifier or a scoped identifier. If the attribute is supported, a
nonzero value is returned. If the attribute is a standards-based attribute, this
macro returns a nonzero value based on the year and month in which the attribute
was voted into the working draft. See `WG21 SD-6
<https://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations>`_
for the list of values returned for standards-based attributes. If the attribute
is not supported by the current compilation target, this macro evaluates to 0.
It can be used like this:

.. code-block:: c++

  #ifndef __has_cpp_attribute         // For backwards compatibility
    #define __has_cpp_attribute(x) 0
  #endif

  ...
  #if __has_cpp_attribute(clang::fallthrough)
  #define FALLTHROUGH [[clang::fallthrough]]
  #else
  #define FALLTHROUGH
  #endif
  ...

The attribute scope tokens ``clang`` and ``_Clang`` are interchangeable, as are
the attribute scope tokens ``gnu`` and ``__gnu__``. Attribute tokens in either
of these namespaces can be specified with a preceding and following ``__``
(double underscore) to avoid interference from a macro with the same name. For
instance, ``gnu::__const__`` can be used instead of ``gnu::const``.

``__has_c_attribute``
---------------------

This function-like macro takes a single argument that is the name of an
attribute exposed with the double square-bracket syntax in C mode. The argument
can either be a single identifier or a scoped identifier. If the attribute is
supported, a nonzero value is returned. If the attribute is not supported by the
current compilation target, this macro evaluates to 0. It can be used like this:

.. code-block:: c

  #ifndef __has_c_attribute         // Optional of course.
    #define __has_c_attribute(x) 0  // Compatibility with non-clang compilers.
  #endif

  ...
  #if __has_c_attribute(fallthrough)
    #define FALLTHROUGH [[fallthrough]]
  #else
    #define FALLTHROUGH
  #endif
  ...

The attribute scope tokens ``clang`` and ``_Clang`` are interchangeable, as are
the attribute scope tokens ``gnu`` and ``__gnu__``. Attribute tokens in either
of these namespaces can be specified with a preceding and following ``__``
(double underscore) to avoid interference from a macro with the same name. For
instance, ``gnu::__const__`` can be used instead of ``gnu::const``.

``__has_attribute``
-------------------

This function-like macro takes a single identifier argument that is the name of
a GNU-style attribute.  It evaluates to 1 if the attribute is supported by the
current compilation target, or 0 if not.  It can be used like this:

.. code-block:: c++

  #ifndef __has_attribute         // Optional of course.
    #define __has_attribute(x) 0  // Compatibility with non-clang compilers.
  #endif

  ...
  #if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline))
  #else
  #define ALWAYS_INLINE
  #endif
  ...

The attribute name can also be specified with a preceding and following ``__``
(double underscore) to avoid interference from a macro with the same name.  For
instance, ``__always_inline__`` can be used instead of ``always_inline``.


``__has_declspec_attribute``
----------------------------

This function-like macro takes a single identifier argument that is the name of
an attribute implemented as a Microsoft-style ``__declspec`` attribute.  It
evaluates to 1 if the attribute is supported by the current compilation target,
or 0 if not.  It can be used like this:

.. code-block:: c++

  #ifndef __has_declspec_attribute         // Optional of course.
    #define __has_declspec_attribute(x) 0  // Compatibility with non-clang compilers.
  #endif

  ...
  #if __has_declspec_attribute(dllexport)
  #define DLLEXPORT __declspec(dllexport)
  #else
  #define DLLEXPORT
  #endif
  ...

The attribute name can also be specified with a preceding and following ``__``
(double underscore) to avoid interference from a macro with the same name.  For
instance, ``__dllexport__`` can be used instead of ``dllexport``.

``__is_identifier``
-------------------

This function-like macro takes a single identifier argument that might be either
a reserved word or a regular identifier. It evaluates to 1 if the argument is just
a regular identifier and not a reserved word, in the sense that it can then be
used as the name of a user-defined function or variable. Otherwise it evaluates
to 0.  It can be used like this:

.. code-block:: c++

  ...
  #ifdef __is_identifier          // Compatibility with non-clang compilers.
    #if __is_identifier(__wchar_t)
      typedef wchar_t __wchar_t;
    #endif
  #endif

  __wchar_t WideCharacter;
  ...

Include File Checking Macros
============================

Not all developments systems have the same include files.  The
:ref:`langext-__has_include` and :ref:`langext-__has_include_next` macros allow
you to check for the existence of an include file before doing a possibly
failing ``#include`` directive.  Include file checking macros must be used
as expressions in ``#if`` or ``#elif`` preprocessing directives.

.. _langext-__has_include:

``__has_include``
-----------------

This function-like macro takes a single file name string argument that is the
name of an include file.  It evaluates to 1 if the file can be found using the
include paths, or 0 otherwise:

.. code-block:: c++

  // Note the two possible file name string formats.
  #if __has_include("myinclude.h") && __has_include(<stdint.h>)
  # include "myinclude.h"
  #endif

To test for this feature, use ``#if defined(__has_include)``:

.. code-block:: c++

  // To avoid problem with non-clang compilers not having this macro.
  #if defined(__has_include)
  #if __has_include("myinclude.h")
  # include "myinclude.h"
  #endif
  #endif

.. _langext-__has_include_next:

``__has_include_next``
----------------------

This function-like macro takes a single file name string argument that is the
name of an include file.  It is like ``__has_include`` except that it looks for
the second instance of the given file found in the include paths.  It evaluates
to 1 if the second instance of the file can be found using the include paths,
or 0 otherwise:

.. code-block:: c++

  // Note the two possible file name string formats.
  #if __has_include_next("myinclude.h") && __has_include_next(<stdint.h>)
  # include_next "myinclude.h"
  #endif

  // To avoid problem with non-clang compilers not having this macro.
  #if defined(__has_include_next)
  #if __has_include_next("myinclude.h")
  # include_next "myinclude.h"
  #endif
  #endif

Note that ``__has_include_next``, like the GNU extension ``#include_next``
directive, is intended for use in headers only, and will issue a warning if
used in the top-level compilation file.  A warning will also be issued if an
absolute path is used in the file argument.

``__has_warning``
-----------------

This function-like macro takes a string literal that represents a command line
option for a warning and returns true if that is a valid warning option.

.. code-block:: c++

  #if __has_warning("-Wformat")
  ...
  #endif

.. _languageextensions-builtin-macros:

Builtin Macros
==============

``__BASE_FILE__``
  Defined to a string that contains the name of the main input file passed to
  Clang.

``__FILE_NAME__``
  Clang-specific extension that functions similar to ``__FILE__`` but only
  renders the last path component (the filename) instead of an invocation
  dependent full path to that file.

``__COUNTER__``
  Defined to an integer value that starts at zero and is incremented each time
  the ``__COUNTER__`` macro is expanded.

``__INCLUDE_LEVEL__``
  Defined to an integral value that is the include depth of the file currently
  being translated.  For the main file, this value is zero.

``__TIMESTAMP__``
  Defined to the date and time of the last modification of the current source
  file.

``__clang__``
  Defined when compiling with Clang

``__clang_major__``
  Defined to the major marketing version number of Clang (e.g., the 2 in
  2.0.1).  Note that marketing version numbers should not be used to check for
  language features, as different vendors use different numbering schemes.
  Instead, use the :ref:`langext-feature_check`.

``__clang_minor__``
  Defined to the minor version number of Clang (e.g., the 0 in 2.0.1).  Note
  that marketing version numbers should not be used to check for language
  features, as different vendors use different numbering schemes.  Instead, use
  the :ref:`langext-feature_check`.

``__clang_patchlevel__``
  Defined to the marketing patch level of Clang (e.g., the 1 in 2.0.1).

``__clang_version__``
  Defined to a string that captures the Clang marketing version, including the
  Subversion tag or revision number, e.g., "``1.5 (trunk 102332)``".

``__clang_literal_encoding__``
  Defined to a narrow string literal that represents the current encoding of
  narrow string literals, e.g., ``"hello"``. This macro typically expands to
  "UTF-8" (but may change in the future if the
  ``-fexec-charset="Encoding-Name"`` option is implemented.)

``__clang_wide_literal_encoding__``
  Defined to a narrow string literal that represents the current encoding of
  wide string literals, e.g., ``L"hello"``. This macro typically expands to
  "UTF-16" or "UTF-32" (but may change in the future if the
  ``-fwide-exec-charset="Encoding-Name"`` option is implemented.)

Implementation-defined keywords
===============================

__datasizeof
------------

``__datasizeof`` behaves like ``sizeof``, except that it returns the size of the
type ignoring tail padding.

..
  FIXME: This should list all the keyword extensions

.. _langext-vectors:

Vectors and Extended Vectors
============================

Supports the GCC, OpenCL, AltiVec, NEON and SVE vector extensions.

OpenCL vector types are created using the ``ext_vector_type`` attribute.  It
supports the ``V.xyzw`` syntax and other tidbits as seen in OpenCL.  An example
is:

.. code-block:: c++

  typedef float float4 __attribute__((ext_vector_type(4)));
  typedef float float2 __attribute__((ext_vector_type(2)));

  float4 foo(float2 a, float2 b) {
    float4 c;
    c.xz = a;
    c.yw = b;
    return c;
  }

Query for this feature with ``__has_attribute(ext_vector_type)``.

Giving ``-maltivec`` option to clang enables support for AltiVec vector syntax
and functions.  For example:

.. code-block:: c++

  vector float foo(vector int a) {
    vector int b;
    b = vec_add(a, a) + a;
    return (vector float)b;
  }

NEON vector types are created using ``neon_vector_type`` and
``neon_polyvector_type`` attributes.  For example:

.. code-block:: c++

  typedef __attribute__((neon_vector_type(8))) int8_t int8x8_t;
  typedef __attribute__((neon_polyvector_type(16))) poly8_t poly8x16_t;

  int8x8_t foo(int8x8_t a) {
    int8x8_t v;
    v = a;
    return v;
  }

GCC vector types are created using the ``vector_size(N)`` attribute.  The
argument ``N`` specifies the number of bytes that will be allocated for an
object of this type.  The size has to be multiple of the size of the vector
element type. For example:

.. code-block:: c++

  // OK: This declares a vector type with four 'int' elements
  typedef int int4 __attribute__((vector_size(4 * sizeof(int))));

  // ERROR: '11' is not a multiple of sizeof(int)
  typedef int int_impossible __attribute__((vector_size(11)));

  int4 foo(int4 a) {
    int4 v;
    v = a;
    return v;
  }


Boolean Vectors
---------------

Clang also supports the ext_vector_type attribute with boolean element types in
C and C++.  For example:

.. code-block:: c++

  // legal for Clang, error for GCC:
  typedef bool bool4 __attribute__((ext_vector_type(4)));
  // Objects of bool4 type hold 8 bits, sizeof(bool4) == 1

  bool4 foo(bool4 a) {
    bool4 v;
    v = a;
    return v;
  }

Boolean vectors are a Clang extension of the ext vector type.  Boolean vectors
are intended, though not guaranteed, to map to vector mask registers.  The size
parameter of a boolean vector type is the number of bits in the vector.  The
boolean vector is dense and each bit in the boolean vector is one vector
element.

The semantics of boolean vectors borrows from C bit-fields with the following
differences:

* Distinct boolean vectors are always distinct memory objects (there is no
  packing).
* Only the operators `?:`, `!`, `~`, `|`, `&`, `^` and comparison are allowed on
  boolean vectors.
* Casting a scalar bool value to a boolean vector type means broadcasting the
  scalar value onto all lanes (same as general ext_vector_type).
* It is not possible to access or swizzle elements of a boolean vector
  (different than general ext_vector_type).

The size and alignment are both the number of bits rounded up to the next power
of two, but the alignment is at most the maximum vector alignment of the
target.


Vector Literals
---------------

Vector literals can be used to create vectors from a set of scalars, or
vectors.  Either parentheses or braces form can be used.  In the parentheses
form the number of literal values specified must be one, i.e. referring to a
scalar value, or must match the size of the vector type being created.  If a
single scalar literal value is specified, the scalar literal value will be
replicated to all the components of the vector type.  In the brackets form any
number of literals can be specified.  For example:

.. code-block:: c++

  typedef int v4si __attribute__((__vector_size__(16)));
  typedef float float4 __attribute__((ext_vector_type(4)));
  typedef float float2 __attribute__((ext_vector_type(2)));

  v4si vsi = (v4si){1, 2, 3, 4};
  float4 vf = (float4)(1.0f, 2.0f, 3.0f, 4.0f);
  vector int vi1 = (vector int)(1);    // vi1 will be (1, 1, 1, 1).
  vector int vi2 = (vector int){1};    // vi2 will be (1, 0, 0, 0).
  vector int vi3 = (vector int)(1, 2); // error
  vector int vi4 = (vector int){1, 2}; // vi4 will be (1, 2, 0, 0).
  vector int vi5 = (vector int)(1, 2, 3, 4);
  float4 vf = (float4)((float2)(1.0f, 2.0f), (float2)(3.0f, 4.0f));

Vector Operations
-----------------

The table below shows the support for each operation by vector extension.  A
dash indicates that an operation is not accepted according to a corresponding
specification.

============================== ======= ======= ============= ======= =====
         Operator              OpenCL  AltiVec     GCC        NEON    SVE
============================== ======= ======= ============= ======= =====
[]                               yes     yes       yes         yes    yes
unary operators +, --            yes     yes       yes         yes    yes
++, -- --                        yes     yes       yes         no     no
+,--,*,/,%                       yes     yes       yes         yes    yes
bitwise operators &,|,^,~        yes     yes       yes         yes    yes
>>,<<                            yes     yes       yes         yes    yes
!, &&, ||                        yes     --        yes         yes    yes
==, !=, >, <, >=, <=             yes     yes       yes         yes    yes
=                                yes     yes       yes         yes    yes
?: [#]_                          yes     --        yes         yes    yes
sizeof                           yes     yes       yes         yes    yes [#]_
C-style cast                     yes     yes       yes         no     no
reinterpret_cast                 yes     no        yes         no     no
static_cast                      yes     no        yes         no     no
const_cast                       no      no        no          no     no
address &v[i]                    no      no        no [#]_     no     no
============================== ======= ======= ============= ======= =====

See also :ref:`langext-__builtin_shufflevector`, :ref:`langext-__builtin_convertvector`.

.. [#] ternary operator(?:) has different behaviors depending on condition
  operand's vector type. If the condition is a GNU vector (i.e. __vector_size__),
  a NEON vector or an SVE vector, it's only available in C++ and uses normal bool
  conversions (that is, != 0).
  If it's an extension (OpenCL) vector, it's only available in C and OpenCL C.
  And it selects base on signedness of the condition operands (OpenCL v1.1 s6.3.9).
.. [#] sizeof can only be used on vector length specific SVE types.
.. [#] Clang does not allow the address of an element to be taken while GCC
   allows this. This is intentional for vectors with a boolean element type and
   not implemented otherwise.

Vector Builtins
---------------

**Note: The implementation of vector builtins is work-in-progress and incomplete.**

In addition to the operators mentioned above, Clang provides a set of builtins
to perform additional operations on certain scalar and vector types.

Let ``T`` be one of the following types:

* an integer type (as in C23 6.2.5p22), but excluding enumerated types and ``bool``
* the standard floating types float or double
* a half-precision floating point type, if one is supported on the target
* a vector type.

For scalar types, consider the operation applied to a vector with a single element.

*Vector Size*
To determine the number of elements in a vector, use ``__builtin_vectorelements()``.
For fixed-sized vectors, e.g., defined via ``__attribute__((vector_size(N)))`` or ARM
NEON's vector types (e.g., ``uint16x8_t``), this returns the constant number of
elements at compile-time. For scalable vectors, e.g., SVE or RISC-V V, the number of
elements is not known at compile-time and is determined at runtime. This builtin can
be used, e.g., to increment the loop-counter in vector-type agnostic loops.

*Elementwise Builtins*

Each builtin returns a vector equivalent to applying the specified operation
elementwise to the input.

Unless specified otherwise operation(±0) = ±0 and operation(±infinity) = ±infinity

=========================================== ================================================================ =========================================
         Name                                Operation                                                        Supported element types
=========================================== ================================================================ =========================================
 T __builtin_elementwise_abs(T x)            return the absolute value of a number x; the absolute value of   signed integer and floating point types
                                             the most negative integer remains the most negative integer
 T __builtin_elementwise_fma(T x, T y, T z)  fused multiply add, (x * y) +  z.                                floating point types
 T __builtin_elementwise_ceil(T x)           return the smallest integral value greater than or equal to x    floating point types
 T __builtin_elementwise_sin(T x)            return the sine of x interpreted as an angle in radians          floating point types
 T __builtin_elementwise_cos(T x)            return the cosine of x interpreted as an angle in radians        floating point types
 T __builtin_elementwise_tan(T x)            return the tangent of x interpreted as an angle in radians       floating point types
 T __builtin_elementwise_asin(T x)           return the arcsine of x interpreted as an angle in radians       floating point types
 T __builtin_elementwise_acos(T x)           return the arccosine of x interpreted as an angle in radians     floating point types
 T __builtin_elementwise_atan(T x)           return the arctangent of x interpreted as an angle in radians    floating point types
 T __builtin_elementwise_sinh(T x)           return the hyperbolic sine of angle x in radians                 floating point types
 T __builtin_elementwise_cosh(T x)           return the hyperbolic cosine of angle x in radians               floating point types
 T __builtin_elementwise_tanh(T x)           return the hyperbolic tangent of angle x in radians              floating point types
 T __builtin_elementwise_floor(T x)          return the largest integral value less than or equal to x        floating point types
 T __builtin_elementwise_log(T x)            return the natural logarithm of x                                floating point types
 T __builtin_elementwise_log2(T x)           return the base 2 logarithm of x                                 floating point types
 T __builtin_elementwise_log10(T x)          return the base 10 logarithm of x                                floating point types
 T __builtin_elementwise_pow(T x, T y)       return x raised to the power of y                                floating point types
 T __builtin_elementwise_bitreverse(T x)     return the integer represented after reversing the bits of x     integer types
 T __builtin_elementwise_exp(T x)            returns the base-e exponential, e^x, of the specified value      floating point types
 T __builtin_elementwise_exp2(T x)           returns the base-2 exponential, 2^x, of the specified value      floating point types

 T __builtin_elementwise_sqrt(T x)           return the square root of a floating-point number                floating point types
 T __builtin_elementwise_roundeven(T x)      round x to the nearest integer value in floating point format,   floating point types
                                             rounding halfway cases to even (that is, to the nearest value
                                             that is an even integer), regardless of the current rounding
                                             direction.
 T __builtin_elementwise_round(T x)          round x to the nearest  integer value in floating point format,      floating point types
                                             rounding halfway cases away from zero, regardless of the
                                             current rounding direction. May raise floating-point
                                             exceptions.
 T __builtin_elementwise_trunc(T x)          return the integral value nearest to but no larger in            floating point types
                                             magnitude than x

  T __builtin_elementwise_nearbyint(T x)     round x to the nearest  integer value in floating point format,      floating point types
                                             rounding according to the current rounding direction.
                                             May not raise the inexact floating-point exception. This is
                                             treated the same as ``__builtin_elementwise_rint`` unless
                                             :ref:`FENV_ACCESS is enabled <floating-point-environment>`.

 T __builtin_elementwise_rint(T x)           round x to the nearest  integer value in floating point format,      floating point types
                                             rounding according to the current rounding
                                             direction. May raise floating-point exceptions. This is treated
                                             the same as ``__builtin_elementwise_nearbyint`` unless
                                             :ref:`FENV_ACCESS is enabled <floating-point-environment>`.

 T __builtin_elementwise_canonicalize(T x)   return the platform specific canonical encoding                  floating point types
                                             of a floating-point number
 T __builtin_elementwise_copysign(T x, T y)  return the magnitude of x with the sign of y.                    floating point types
 T __builtin_elementwise_max(T x, T y)       return x or y, whichever is larger                               integer and floating point types
 T __builtin_elementwise_min(T x, T y)       return x or y, whichever is smaller                              integer and floating point types
 T __builtin_elementwise_add_sat(T x, T y)   return the sum of x and y, clamped to the range of               integer types
                                             representable values for the signed/unsigned integer type.
 T __builtin_elementwise_sub_sat(T x, T y)   return the difference of x and y, clamped to the range of        integer types
                                             representable values for the signed/unsigned integer type.
=========================================== ================================================================ =========================================


*Reduction Builtins*

Each builtin returns a scalar equivalent to applying the specified
operation(x, y) as recursive even-odd pairwise reduction to all vector
elements. ``operation(x, y)`` is repeatedly applied to each non-overlapping
even-odd element pair with indices ``i * 2`` and ``i * 2 + 1`` with
``i in [0, Number of elements / 2)``. If the numbers of elements is not a
power of 2, the vector is widened with neutral elements for the reduction
at the end to the next power of 2.

These reductions support both fixed-sized and scalable vector types.

Example:

.. code-block:: c++

    __builtin_reduce_add([e3, e2, e1, e0]) = __builtin_reduced_add([e3 + e2, e1 + e0])
                                           = (e3 + e2) + (e1 + e0)


Let ``VT`` be a vector type and ``ET`` the element type of ``VT``.

======================================= ================================================================ ==================================
         Name                            Operation                                                        Supported element types
======================================= ================================================================ ==================================
 ET __builtin_reduce_max(VT a)           return x or y, whichever is larger; If exactly one argument is   integer and floating point types
                                         a NaN, return the other argument. If both arguments are NaNs,
                                         fmax() return a NaN.
 ET __builtin_reduce_min(VT a)           return x or y, whichever is smaller; If exactly one argument     integer and floating point types
                                         is a NaN, return the other argument. If both arguments are
                                         NaNs, fmax() return a NaN.
 ET __builtin_reduce_add(VT a)           \+                                                               integer types
 ET __builtin_reduce_mul(VT a)           \*                                                               integer types
 ET __builtin_reduce_and(VT a)           &                                                                integer types
 ET __builtin_reduce_or(VT a)            \|                                                               integer types
 ET __builtin_reduce_xor(VT a)           ^                                                                integer types
======================================= ================================================================ ==================================

Matrix Types
============

Clang provides an extension for matrix types, which is currently being
implemented. See :ref:`the draft specification <matrixtypes>` for more details.

For example, the code below uses the matrix types extension to multiply two 4x4
float matrices and add the result to a third 4x4 matrix.

.. code-block:: c++

  typedef float m4x4_t __attribute__((matrix_type(4, 4)));

  m4x4_t f(m4x4_t a, m4x4_t b, m4x4_t c) {
    return a + b * c;
  }

The matrix type extension also supports operations on a matrix and a scalar.

.. code-block:: c++

  typedef float m4x4_t __attribute__((matrix_type(4, 4)));

  m4x4_t f(m4x4_t a) {
    return (a + 23) * 12;
  }

The matrix type extension supports division on a matrix and a scalar but not on a matrix and a matrix.

.. code-block:: c++

  typedef float m4x4_t __attribute__((matrix_type(4, 4)));

  m4x4_t f(m4x4_t a) {
    a = a / 3.0;
    return a;
  }

The matrix type extension supports compound assignments for addition, subtraction, and multiplication on matrices
and on a matrix and a scalar, provided their types are consistent.

.. code-block:: c++

  typedef float m4x4_t __attribute__((matrix_type(4, 4)));

  m4x4_t f(m4x4_t a, m4x4_t b) {
    a += b;
    a -= b;
    a *= b;
    a += 23;
    a -= 12;
    return a;
  }

The matrix type extension supports explicit casts. Implicit type conversion between matrix types is not allowed.

.. code-block:: c++

  typedef int ix5x5 __attribute__((matrix_type(5, 5)));
  typedef float fx5x5 __attribute__((matrix_type(5, 5)));

  fx5x5 f1(ix5x5 i, fx5x5 f) {
    return (fx5x5) i;
  }


  template <typename X>
  using matrix_4_4 = X __attribute__((matrix_type(4, 4)));

  void f2() {
    matrix_5_5<double> d;
    matrix_5_5<int> i;
    i = (matrix_5_5<int>)d;
    i = static_cast<matrix_5_5<int>>(d);
  }

Half-Precision Floating Point
=============================

Clang supports three half-precision (16-bit) floating point types:
``__fp16``, ``_Float16`` and ``__bf16``. These types are supported
in all language modes, but their support differs between targets.
A target is said to have "native support" for a type if the target
processor offers instructions for directly performing basic arithmetic
on that type.  In the absence of native support, a type can still be
supported if the compiler can emulate arithmetic on the type by promoting
to ``float``; see below for more information on this emulation.

* ``__fp16`` is supported on all targets. The special semantics of this
  type mean that no arithmetic is ever performed directly on ``__fp16`` values;
  see below.

* ``_Float16`` is supported on the following targets:

  * 32-bit ARM (natively on some architecture versions)
  * 64-bit ARM (AArch64) (natively on ARMv8.2a and above)
  * AMDGPU (natively)
  * NVPTX (natively)
  * SPIR (natively)
  * X86 (if SSE2 is available; natively if AVX512-FP16 is also available)
  * RISC-V (natively if Zfh or Zhinx is available)

* ``__bf16`` is supported on the following targets (currently never natively):

  * 32-bit ARM
  * 64-bit ARM (AArch64)
  * RISC-V
  * X86 (when SSE2 is available)

(For X86, SSE2 is available on 64-bit and all recent 32-bit processors.)

``__fp16`` and ``_Float16`` both use the binary16 format from IEEE
754-2008, which provides a 5-bit exponent and an 11-bit significand
(counting the implicit leading 1). ``__bf16`` uses the `bfloat16
<https://en.wikipedia.org/wiki/Bfloat16_floating-point_format>`_ format,
which provides an 8-bit exponent and an 8-bit significand; this is the same
exponent range as `float`, just with greatly reduced precision.

``_Float16`` and ``__bf16`` follow the usual rules for arithmetic
floating-point types. Most importantly, this means that arithmetic operations
on operands of these types are formally performed in the type and produce
values of the type. ``__fp16`` does not follow those rules: most operations
immediately promote operands of type ``__fp16`` to ``float``, and so
arithmetic operations are defined to be performed in ``float`` and so result in
a value of type ``float`` (unless further promoted because of other operands).
See below for more information on the exact specifications of these types.

When compiling arithmetic on ``_Float16`` and ``__bf16`` for a target without
native support, Clang will perform the arithmetic in ``float``, inserting
extensions and truncations as necessary. This can be done in a way that
exactly matches the operation-by-operation behavior of native support,
but that can require many extra truncations and extensions. By default,
when emulating ``_Float16`` and ``__bf16`` arithmetic using ``float``, Clang
does not truncate intermediate operands back to their true type unless the
operand is the result of an explicit cast or assignment. This is generally
much faster but can generate different results from strict operation-by-operation
emulation. Usually the results are more precise. This is permitted by the
C and C++ standards under the rules for excess precision in intermediate operands;
see the discussion of evaluation formats in the C standard and [expr.pre] in
the C++ standard.

The use of excess precision can be independently controlled for these two
types with the ``-ffloat16-excess-precision=`` and
``-fbfloat16-excess-precision=`` options. Valid values include:

* ``none``: meaning to perform strict operation-by-operation emulation
* ``standard``: meaning that excess precision is permitted under the rules
  described in the standard, i.e. never across explicit casts or statements
* ``fast``: meaning that excess precision is permitted whenever the
  optimizer sees an opportunity to avoid truncations; currently this has no
  effect beyond ``standard``

The ``_Float16`` type is an interchange floating type specified in
ISO/IEC TS 18661-3:2015 ("Floating-point extensions for C"). It will
be supported on more targets as they define ABIs for it.

The ``__bf16`` type is a non-standard extension, but it generally follows
the rules for arithmetic interchange floating types from ISO/IEC TS
18661-3:2015. In previous versions of Clang, it was a storage-only type
that forbade arithmetic operations. It will be supported on more targets
as they define ABIs for it.

The ``__fp16`` type was originally an ARM extension and is specified
by the `ARM C Language Extensions <https://github.com/ARM-software/acle/releases>`_.
Clang uses the ``binary16`` format from IEEE 754-2008 for ``__fp16``,
not the ARM alternative format. Operators that expect arithmetic operands
immediately promote ``__fp16`` operands to ``float``.

It is recommended that portable code use ``_Float16`` instead of ``__fp16``,
as it has been defined by the C standards committee and has behavior that is
more familiar to most programmers.

Because ``__fp16`` operands are always immediately promoted to ``float``, the
common real type of ``__fp16`` and ``_Float16`` for the purposes of the usual
arithmetic conversions is ``float``.

A literal can be given ``_Float16`` type using the suffix ``f16``. For example,
``3.14f16``.

Because default argument promotion only applies to the standard floating-point
types, ``_Float16`` values are not promoted to ``double`` when passed as variadic
or untyped arguments. As a consequence, some caution must be taken when using
certain library facilities with ``_Float16``; for example, there is no ``printf`` format
specifier for ``_Float16``, and (unlike ``float``) it will not be implicitly promoted to
``double`` when passed to ``printf``, so the programmer must explicitly cast it to
``double`` before using it with an ``%f`` or similar specifier.

Messages on ``deprecated`` and ``unavailable`` Attributes
=========================================================

An optional string message can be added to the ``deprecated`` and
``unavailable`` attributes.  For example:

.. code-block:: c++

  void explode(void) __attribute__((deprecated("extremely unsafe, use 'combust' instead!!!")));

If the deprecated or unavailable declaration is used, the message will be
incorporated into the appropriate diagnostic:

.. code-block:: none

  harmless.c:4:3: warning: 'explode' is deprecated: extremely unsafe, use 'combust' instead!!!
        [-Wdeprecated-declarations]
    explode();
    ^

Query for this feature with
``__has_extension(attribute_deprecated_with_message)`` and
``__has_extension(attribute_unavailable_with_message)``.

Attributes on Enumerators
=========================

Clang allows attributes to be written on individual enumerators.  This allows
enumerators to be deprecated, made unavailable, etc.  The attribute must appear
after the enumerator name and before any initializer, like so:

.. code-block:: c++

  enum OperationMode {
    OM_Invalid,
    OM_Normal,
    OM_Terrified __attribute__((deprecated)),
    OM_AbortOnError __attribute__((deprecated)) = 4
  };

Attributes on the ``enum`` declaration do not apply to individual enumerators.

Query for this feature with ``__has_extension(enumerator_attributes)``.

C++11 Attributes on using-declarations
======================================

Clang allows C++-style ``[[]]`` attributes to be written on using-declarations.
For instance:

.. code-block:: c++

  [[clang::using_if_exists]] using foo::bar;
  using foo::baz [[clang::using_if_exists]];

You can test for support for this extension with
``__has_extension(cxx_attributes_on_using_declarations)``.

'User-Specified' System Frameworks
==================================

Clang provides a mechanism by which frameworks can be built in such a way that
they will always be treated as being "system frameworks", even if they are not
present in a system framework directory.  This can be useful to system
framework developers who want to be able to test building other applications
with development builds of their framework, including the manner in which the
compiler changes warning behavior for system headers.

Framework developers can opt-in to this mechanism by creating a
"``.system_framework``" file at the top-level of their framework.  That is, the
framework should have contents like:

.. code-block:: none

  .../TestFramework.framework
  .../TestFramework.framework/.system_framework
  .../TestFramework.framework/Headers
  .../TestFramework.framework/Headers/TestFramework.h
  ...

Clang will treat the presence of this file as an indicator that the framework
should be treated as a system framework, regardless of how it was found in the
framework search path.  For consistency, we recommend that such files never be
included in installed versions of the framework.

Checks for Standard Language Features
=====================================

The ``__has_feature`` macro can be used to query if certain standard language
features are enabled.  The ``__has_extension`` macro can be used to query if
language features are available as an extension when compiling for a standard
which does not provide them.  The features which can be tested are listed here.

Since Clang 3.4, the C++ SD-6 feature test macros are also supported.
These are macros with names of the form ``__cpp_<feature_name>``, and are
intended to be a portable way to query the supported features of the compiler.
See `the C++ status page <https://clang.llvm.org/cxx_status.html#ts>`_ for
information on the version of SD-6 supported by each Clang release, and the
macros provided by that revision of the recommendations.

C++98
-----

The features listed below are part of the C++98 standard.  These features are
enabled by default when compiling C++ code.

C++ exceptions
^^^^^^^^^^^^^^

Use ``__has_feature(cxx_exceptions)`` to determine if C++ exceptions have been
enabled.  For example, compiling code with ``-fno-exceptions`` disables C++
exceptions.

C++ RTTI
^^^^^^^^

Use ``__has_feature(cxx_rtti)`` to determine if C++ RTTI has been enabled.  For
example, compiling code with ``-fno-rtti`` disables the use of RTTI.

C++11
-----

The features listed below are part of the C++11 standard.  As a result, all
these features are enabled with the ``-std=c++11`` or ``-std=gnu++11`` option
when compiling C++ code.

C++11 SFINAE includes access control
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_access_control_sfinae)`` or
``__has_extension(cxx_access_control_sfinae)`` to determine whether
access-control errors (e.g., calling a private constructor) are considered to
be template argument deduction errors (aka SFINAE errors), per `C++ DR1170
<http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1170>`_.

C++11 alias templates
^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_alias_templates)`` or
``__has_extension(cxx_alias_templates)`` to determine if support for C++11's
alias declarations and alias templates is enabled.

C++11 alignment specifiers
^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_alignas)`` or ``__has_extension(cxx_alignas)`` to
determine if support for alignment specifiers using ``alignas`` is enabled.

Use ``__has_feature(cxx_alignof)`` or ``__has_extension(cxx_alignof)`` to
determine if support for the ``alignof`` keyword is enabled.

C++11 attributes
^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_attributes)`` or ``__has_extension(cxx_attributes)`` to
determine if support for attribute parsing with C++11's square bracket notation
is enabled.

C++11 generalized constant expressions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_constexpr)`` to determine if support for generalized
constant expressions (e.g., ``constexpr``) is enabled.

C++11 ``decltype()``
^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_decltype)`` or ``__has_extension(cxx_decltype)`` to
determine if support for the ``decltype()`` specifier is enabled.  C++11's
``decltype`` does not require type-completeness of a function call expression.
Use ``__has_feature(cxx_decltype_incomplete_return_types)`` or
``__has_extension(cxx_decltype_incomplete_return_types)`` to determine if
support for this feature is enabled.

C++11 default template arguments in function templates
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_default_function_template_args)`` or
``__has_extension(cxx_default_function_template_args)`` to determine if support
for default template arguments in function templates is enabled.

C++11 ``default``\ ed functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_defaulted_functions)`` or
``__has_extension(cxx_defaulted_functions)`` to determine if support for
defaulted function definitions (with ``= default``) is enabled.

C++11 delegating constructors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_delegating_constructors)`` to determine if support for
delegating constructors is enabled.

C++11 ``deleted`` functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_deleted_functions)`` or
``__has_extension(cxx_deleted_functions)`` to determine if support for deleted
function definitions (with ``= delete``) is enabled.

C++11 explicit conversion functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_explicit_conversions)`` to determine if support for
``explicit`` conversion functions is enabled.

C++11 generalized initializers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_generalized_initializers)`` to determine if support for
generalized initializers (using braced lists and ``std::initializer_list``) is
enabled.

C++11 implicit move constructors/assignment operators
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_implicit_moves)`` to determine if Clang will implicitly
generate move constructors and move assignment operators where needed.

C++11 inheriting constructors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_inheriting_constructors)`` to determine if support for
inheriting constructors is enabled.

C++11 inline namespaces
^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_inline_namespaces)`` or
``__has_extension(cxx_inline_namespaces)`` to determine if support for inline
namespaces is enabled.

C++11 lambdas
^^^^^^^^^^^^^

Use ``__has_feature(cxx_lambdas)`` or ``__has_extension(cxx_lambdas)`` to
determine if support for lambdas is enabled.

C++11 local and unnamed types as template arguments
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_local_type_template_args)`` or
``__has_extension(cxx_local_type_template_args)`` to determine if support for
local and unnamed types as template arguments is enabled.

C++11 noexcept
^^^^^^^^^^^^^^

Use ``__has_feature(cxx_noexcept)`` or ``__has_extension(cxx_noexcept)`` to
determine if support for noexcept exception specifications is enabled.

C++11 in-class non-static data member initialization
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_nonstatic_member_init)`` to determine whether in-class
initialization of non-static data members is enabled.

C++11 ``nullptr``
^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_nullptr)`` or ``__has_extension(cxx_nullptr)`` to
determine if support for ``nullptr`` is enabled.

C++11 ``override control``
^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_override_control)`` or
``__has_extension(cxx_override_control)`` to determine if support for the
override control keywords is enabled.

C++11 reference-qualified functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_reference_qualified_functions)`` or
``__has_extension(cxx_reference_qualified_functions)`` to determine if support
for reference-qualified functions (e.g., member functions with ``&`` or ``&&``
applied to ``*this``) is enabled.

C++11 range-based ``for`` loop
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_range_for)`` or ``__has_extension(cxx_range_for)`` to
determine if support for the range-based for loop is enabled.

C++11 raw string literals
^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_raw_string_literals)`` to determine if support for raw
string literals (e.g., ``R"x(foo\bar)x"``) is enabled.

C++11 rvalue references
^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_rvalue_references)`` or
``__has_extension(cxx_rvalue_references)`` to determine if support for rvalue
references is enabled.

C++11 ``static_assert()``
^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_static_assert)`` or
``__has_extension(cxx_static_assert)`` to determine if support for compile-time
assertions using ``static_assert`` is enabled.

C++11 ``thread_local``
^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_thread_local)`` to determine if support for
``thread_local`` variables is enabled.

C++11 type inference
^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_auto_type)`` or ``__has_extension(cxx_auto_type)`` to
determine C++11 type inference is supported using the ``auto`` specifier.  If
this is disabled, ``auto`` will instead be a storage class specifier, as in C
or C++98.

C++11 strongly typed enumerations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_strong_enums)`` or
``__has_extension(cxx_strong_enums)`` to determine if support for strongly
typed, scoped enumerations is enabled.

C++11 trailing return type
^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_trailing_return)`` or
``__has_extension(cxx_trailing_return)`` to determine if support for the
alternate function declaration syntax with trailing return type is enabled.

C++11 Unicode string literals
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_unicode_literals)`` to determine if support for Unicode
string literals is enabled.

C++11 unrestricted unions
^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_unrestricted_unions)`` to determine if support for
unrestricted unions is enabled.

C++11 user-defined literals
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_user_literals)`` to determine if support for
user-defined literals is enabled.

C++11 variadic templates
^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_variadic_templates)`` or
``__has_extension(cxx_variadic_templates)`` to determine if support for
variadic templates is enabled.

C++14
-----

The features listed below are part of the C++14 standard.  As a result, all
these features are enabled with the ``-std=C++14`` or ``-std=gnu++14`` option
when compiling C++ code.

C++14 binary literals
^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_binary_literals)`` or
``__has_extension(cxx_binary_literals)`` to determine whether
binary literals (for instance, ``0b10010``) are recognized. Clang supports this
feature as an extension in all language modes.

C++14 contextual conversions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_contextual_conversions)`` or
``__has_extension(cxx_contextual_conversions)`` to determine if the C++14 rules
are used when performing an implicit conversion for an array bound in a
*new-expression*, the operand of a *delete-expression*, an integral constant
expression, or a condition in a ``switch`` statement.

C++14 decltype(auto)
^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_decltype_auto)`` or
``__has_extension(cxx_decltype_auto)`` to determine if support
for the ``decltype(auto)`` placeholder type is enabled.

C++14 default initializers for aggregates
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_aggregate_nsdmi)`` or
``__has_extension(cxx_aggregate_nsdmi)`` to determine if support
for default initializers in aggregate members is enabled.

C++14 digit separators
^^^^^^^^^^^^^^^^^^^^^^

Use ``__cpp_digit_separators`` to determine if support for digit separators
using single quotes (for instance, ``10'000``) is enabled. At this time, there
is no corresponding ``__has_feature`` name

C++14 generalized lambda capture
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_init_captures)`` or
``__has_extension(cxx_init_captures)`` to determine if support for
lambda captures with explicit initializers is enabled
(for instance, ``[n(0)] { return ++n; }``).

C++14 generic lambdas
^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_generic_lambdas)`` or
``__has_extension(cxx_generic_lambdas)`` to determine if support for generic
(polymorphic) lambdas is enabled
(for instance, ``[] (auto x) { return x + 1; }``).

C++14 relaxed constexpr
^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_relaxed_constexpr)`` or
``__has_extension(cxx_relaxed_constexpr)`` to determine if variable
declarations, local variable modification, and control flow constructs
are permitted in ``constexpr`` functions.

C++14 return type deduction
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_return_type_deduction)`` or
``__has_extension(cxx_return_type_deduction)`` to determine if support
for return type deduction for functions (using ``auto`` as a return type)
is enabled.

C++14 runtime-sized arrays
^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_runtime_array)`` or
``__has_extension(cxx_runtime_array)`` to determine if support
for arrays of runtime bound (a restricted form of variable-length arrays)
is enabled.
Clang's implementation of this feature is incomplete.

C++14 variable templates
^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(cxx_variable_templates)`` or
``__has_extension(cxx_variable_templates)`` to determine if support for
templated variable declarations is enabled.

C11
---

The features listed below are part of the C11 standard.  As a result, all these
features are enabled with the ``-std=c11`` or ``-std=gnu11`` option when
compiling C code.  Additionally, because these features are all
backward-compatible, they are available as extensions in all language modes.

C11 alignment specifiers
^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(c_alignas)`` or ``__has_extension(c_alignas)`` to determine
if support for alignment specifiers using ``_Alignas`` is enabled.

Use ``__has_feature(c_alignof)`` or ``__has_extension(c_alignof)`` to determine
if support for the ``_Alignof`` keyword is enabled.

C11 atomic operations
^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(c_atomic)`` or ``__has_extension(c_atomic)`` to determine
if support for atomic types using ``_Atomic`` is enabled.  Clang also provides
:ref:`a set of builtins <langext-__c11_atomic>` which can be used to implement
the ``<stdatomic.h>`` operations on ``_Atomic`` types. Use
``__has_include(<stdatomic.h>)`` to determine if C11's ``<stdatomic.h>`` header
is available.

Clang will use the system's ``<stdatomic.h>`` header when one is available, and
will otherwise use its own. When using its own, implementations of the atomic
operations are provided as macros. In the cases where C11 also requires a real
function, this header provides only the declaration of that function (along
with a shadowing macro implementation), and you must link to a library which
provides a definition of the function if you use it instead of the macro.

C11 generic selections
^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(c_generic_selections)`` or
``__has_extension(c_generic_selections)`` to determine if support for generic
selections is enabled.

As an extension, the C11 generic selection expression is available in all
languages supported by Clang.  The syntax is the same as that given in the C11
standard.

In C, type compatibility is decided according to the rules given in the
appropriate standard, but in C++, which lacks the type compatibility rules used
in C, types are considered compatible only if they are equivalent.

Clang also supports an extended form of ``_Generic`` with a controlling type
rather than a controlling expression. Unlike with a controlling expression, a
controlling type argument does not undergo any conversions and thus is suitable
for use when trying to match qualified types, incomplete types, or function
types. Variable-length array types lack the necessary compile-time information
to resolve which association they match with and thus are not allowed as a
controlling type argument.

Use ``__has_extension(c_generic_selection_with_controlling_type)`` to determine
if support for this extension is enabled.

C11 ``_Static_assert()``
^^^^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(c_static_assert)`` or ``__has_extension(c_static_assert)``
to determine if support for compile-time assertions using ``_Static_assert`` is
enabled.

C11 ``_Thread_local``
^^^^^^^^^^^^^^^^^^^^^

Use ``__has_feature(c_thread_local)`` or ``__has_extension(c_thread_local)``
to determine if support for ``_Thread_local`` variables is enabled.

Modules
-------

Use ``__has_feature(modules)`` to determine if Modules have been enabled.
For example, compiling code with ``-fmodules`` enables the use of Modules.

More information could be found `here <https://clang.llvm.org/docs/Modules.html>`_.

Language Extensions Back-ported to Previous Standards
=====================================================

============================================ ================================ ============= =============
Feature                                      Feature Test Macro               Introduced In Backported To
============================================ ================================ ============= =============
variadic templates                           __cpp_variadic_templates         C++11         C++03
Alias templates                              __cpp_alias_templates            C++11         C++03
Non-static data member initializers          __cpp_nsdmi                      C++11         C++03
Range-based ``for`` loop                     __cpp_range_based_for            C++11         C++03
RValue references                            __cpp_rvalue_references          C++11         C++03
Attributes                                   __cpp_attributes                 C++11         C++03
Lambdas                                      __cpp_lambdas                    C++11         C++03
Generalized lambda captures                  __cpp_init_captures              C++14         C++03
Generic lambda expressions                   __cpp_generic_lambdas            C++14         C++03
variable templates                           __cpp_variable_templates         C++14         C++03
Binary literals                              __cpp_binary_literals            C++14         C++03
Relaxed constexpr                            __cpp_constexpr                  C++14         C++11
Pack expansion in generalized lambda-capture __cpp_init_captures              C++17         C++03
``if constexpr``                             __cpp_if_constexpr               C++17         C++11
fold expressions                             __cpp_fold_expressions           C++17         C++03
Lambda capture of \*this by value            __cpp_capture_star_this          C++17         C++03
Attributes on enums                          __cpp_enumerator_attributes      C++17         C++03
Guaranteed copy elision                      __cpp_guaranteed_copy_elision    C++17         C++03
Hexadecimal floating literals                __cpp_hex_float                  C++17         C++03
``inline`` variables                         __cpp_inline_variables           C++17         C++03
Attributes on namespaces                     __cpp_namespace_attributes       C++17         C++11
Structured bindings                          __cpp_structured_bindings        C++17         C++03
template template arguments                  __cpp_template_template_args     C++17         C++03
Familiar template syntax for generic lambdas __cpp_generic_lambdas            C++20         C++03
``static operator[]``                        __cpp_multidimensional_subscript C++20         C++03
Designated initializers                      __cpp_designated_initializers    C++20         C++03
Conditional ``explicit``                     __cpp_conditional_explicit       C++20         C++03
``using enum``                               __cpp_using_enum                 C++20         C++03
``if consteval``                             __cpp_if_consteval               C++23         C++20
``static operator()``                        __cpp_static_call_operator       C++23         C++03
Attributes on Lambda-Expressions                                              C++23         C++11
Attributes on Structured Bindings            __cpp_structured_bindings        C++26         C++03
Pack Indexing                                __cpp_pack_indexing              C++26         C++03
``= delete ("should have a reason");``       __cpp_deleted_function           C++26         C++03
-------------------------------------------- -------------------------------- ------------- -------------
Designated initializers (N494)                                                C99           C89
Array & element qualification (N2607)                                         C23           C89
Attributes (N2335)                                                            C23           C89
``#embed`` (N3017)                                                            C23           C89, C++
============================================ ================================ ============= =============

Type Trait Primitives
=====================

Type trait primitives are special builtin constant expressions that can be used
by the standard C++ library to facilitate or simplify the implementation of
user-facing type traits in the <type_traits> header.

They are not intended to be used directly by user code because they are
implementation-defined and subject to change -- as such they're tied closely to
the supported set of system headers, currently:

* LLVM's own libc++
* GNU libstdc++
* The Microsoft standard C++ library

Clang supports the `GNU C++ type traits
<https://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html>`_ and a subset of the
`Microsoft Visual C++ type traits
<https://msdn.microsoft.com/en-us/library/ms177194(v=VS.100).aspx>`_,
as well as nearly all of the
`Embarcadero C++ type traits
<http://docwiki.embarcadero.com/RADStudio/Rio/en/Type_Trait_Functions_(C%2B%2B11)_Index>`_.

The following type trait primitives are supported by Clang. Those traits marked
(C++) provide implementations for type traits specified by the C++ standard;
``__X(...)`` has the same semantics and constraints as the corresponding
``std::X_t<...>`` or ``std::X_v<...>`` type trait.

* ``__array_rank(type)`` (Embarcadero):
  Returns the number of levels of array in the type ``type``:
  ``0`` if ``type`` is not an array type, and
  ``__array_rank(element) + 1`` if ``type`` is an array of ``element``.
* ``__array_extent(type, dim)`` (Embarcadero):
  The ``dim``'th array bound in the type ``type``, or ``0`` if
  ``dim >= __array_rank(type)``.
* ``__can_pass_in_regs`` (C++)
  Returns whether a class can be passed in registers under the current
  ABI. This type can only be applied to unqualified class types.
  This is not a portable type trait.
* ``__has_nothrow_assign`` (GNU, Microsoft, Embarcadero):
  Deprecated, use ``__is_nothrow_assignable`` instead.
* ``__has_nothrow_move_assign`` (GNU, Microsoft):
  Deprecated, use ``__is_nothrow_assignable`` instead.
* ``__has_nothrow_copy`` (GNU, Microsoft):
  Deprecated, use ``__is_nothrow_constructible`` instead.
* ``__has_nothrow_constructor`` (GNU, Microsoft):
  Deprecated, use ``__is_nothrow_constructible`` instead.
* ``__has_trivial_assign`` (GNU, Microsoft, Embarcadero):
  Deprecated, use ``__is_trivially_assignable`` instead.
* ``__has_trivial_move_assign`` (GNU, Microsoft):
  Deprecated, use ``__is_trivially_assignable`` instead.
* ``__has_trivial_copy`` (GNU, Microsoft):
  Deprecated, use ``__is_trivially_copyable`` instead.
* ``__has_trivial_constructor`` (GNU, Microsoft):
  Deprecated, use ``__is_trivially_constructible`` instead.
* ``__has_trivial_move_constructor`` (GNU, Microsoft):
  Deprecated, use ``__is_trivially_constructible`` instead.
* ``__has_trivial_destructor`` (GNU, Microsoft, Embarcadero):
  Deprecated, use ``__is_trivially_destructible`` instead.
* ``__has_unique_object_representations`` (C++, GNU)
* ``__has_virtual_destructor`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_abstract`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_aggregate`` (C++, GNU, Microsoft)
* ``__is_arithmetic`` (C++, Embarcadero)
* ``__is_array`` (C++, Embarcadero)
* ``__is_assignable`` (C++, MSVC 2015)
* ``__is_base_of`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_bounded_array`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_class`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_complete_type(type)`` (Embarcadero):
  Return ``true`` if ``type`` is a complete type.
  Warning: this trait is dangerous because it can return different values at
  different points in the same program.
* ``__is_compound`` (C++, Embarcadero)
* ``__is_const`` (C++, Embarcadero)
* ``__is_constructible`` (C++, MSVC 2013)
* ``__is_convertible`` (C++, Embarcadero)
* ``__is_nothrow_convertible`` (C++, GNU)
* ``__is_convertible_to`` (Microsoft):
  Synonym for ``__is_convertible``.
* ``__is_destructible`` (C++, MSVC 2013)
* ``__is_empty`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_enum`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_final`` (C++, GNU, Microsoft)
* ``__is_floating_point`` (C++, Embarcadero)
* ``__is_function`` (C++, Embarcadero)
* ``__is_fundamental`` (C++, Embarcadero)
* ``__is_integral`` (C++, Embarcadero)
* ``__is_interface_class`` (Microsoft):
  Returns ``false``, even for types defined with ``__interface``.
* ``__is_layout_compatible`` (C++, GNU, Microsoft)
* ``__is_literal`` (Clang):
  Synonym for ``__is_literal_type``.
* ``__is_literal_type`` (C++, GNU, Microsoft):
  Note, the corresponding standard trait was deprecated in C++17
  and removed in C++20.
* ``__is_lvalue_reference`` (C++, Embarcadero)
* ``__is_member_object_pointer`` (C++, Embarcadero)
* ``__is_member_function_pointer`` (C++, Embarcadero)
* ``__is_member_pointer`` (C++, Embarcadero)
* ``__is_nothrow_assignable`` (C++, MSVC 2013)
* ``__is_nothrow_constructible`` (C++, MSVC 2013)
* ``__is_nothrow_destructible`` (C++, MSVC 2013)
* ``__is_nullptr`` (C++, GNU, Microsoft, Embarcadero):
  Returns true for ``std::nullptr_t`` and false for everything else. The
  corresponding standard library feature is ``std::is_null_pointer``, but
  ``__is_null_pointer`` is already in use by some implementations.
* ``__is_object`` (C++, Embarcadero)
* ``__is_pod`` (C++, GNU, Microsoft, Embarcadero):
  Note, the corresponding standard trait was deprecated in C++20.
* ``__is_pointer`` (C++, Embarcadero)
* ``__is_pointer_interconvertible_base_of`` (C++, GNU, Microsoft)
* ``__is_polymorphic`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_reference`` (C++, Embarcadero)
* ``__is_referenceable`` (C++, GNU, Microsoft, Embarcadero):
  Returns true if a type is referenceable, and false otherwise. A referenceable
  type is a type that's either an object type, a reference type, or an unqualified
  function type.
* ``__is_rvalue_reference`` (C++, Embarcadero)
* ``__is_same`` (C++, Embarcadero)
* ``__is_same_as`` (GCC): Synonym for ``__is_same``.
* ``__is_scalar`` (C++, Embarcadero)
* ``__is_scoped_enum`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_sealed`` (Microsoft):
  Synonym for ``__is_final``.
* ``__is_signed`` (C++, Embarcadero):
  Returns false for enumeration types, and returns true for floating-point
  types. Note, before Clang 10, returned true for enumeration types if the
  underlying type was signed, and returned false for floating-point types.
* ``__is_standard_layout`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_trivial`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_trivially_assignable`` (C++, GNU, Microsoft)
* ``__is_trivially_constructible`` (C++, GNU, Microsoft)
* ``__is_trivially_copyable`` (C++, GNU, Microsoft)
* ``__is_trivially_destructible`` (C++, MSVC 2013)
* ``__is_trivially_relocatable`` (Clang): Returns true if moving an object
  of the given type, and then destroying the source object, is known to be
  functionally equivalent to copying the underlying bytes and then dropping the
  source object on the floor. This is true of trivial types and types which
  were made trivially relocatable via the ``clang::trivial_abi`` attribute.
* ``__is_trivially_equality_comparable`` (Clang): Returns true if comparing two
  objects of the provided type is known to be equivalent to comparing their
  object representations. Note that types containing padding bytes are never
  trivially equality comparable.
* ``__is_unbounded_array`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_union`` (C++, GNU, Microsoft, Embarcadero)
* ``__is_unsigned`` (C++, Embarcadero):
  Returns false for enumeration types. Note, before Clang 13, returned true for
  enumeration types if the underlying type was unsigned.
* ``__is_void`` (C++, Embarcadero)
* ``__is_volatile`` (C++, Embarcadero)
* ``__reference_binds_to_temporary(T, U)`` (Clang):  Determines whether a
  reference of type ``T`` bound to an expression of type ``U`` would bind to a
  materialized temporary object. If ``T`` is not a reference type the result
  is false. Note this trait will also return false when the initialization of
  ``T`` from ``U`` is ill-formed.
  Deprecated, use ``__reference_constructs_from_temporary``.
* ``__reference_constructs_from_temporary(T, U)`` (C++)
  Returns true if a reference ``T`` can be direct-initialized from a temporary of type
  a non-cv-qualified ``U``.
* ``__reference_converts_from_temporary(T, U)`` (C++)
    Returns true if a reference ``T`` can be copy-initialized from a temporary of type
    a non-cv-qualified ``U``.
* ``__underlying_type`` (C++, GNU, Microsoft)

In addition, the following expression traits are supported:

* ``__is_lvalue_expr(e)`` (Embarcadero):
  Returns true if ``e`` is an lvalue expression.
  Deprecated, use ``__is_lvalue_reference(decltype((e)))`` instead.
* ``__is_rvalue_expr(e)`` (Embarcadero):
  Returns true if ``e`` is a prvalue expression.
  Deprecated, use ``!__is_reference(decltype((e)))`` instead.

There are multiple ways to detect support for a type trait ``__X`` in the
compiler, depending on the oldest version of Clang you wish to support.

* From Clang 10 onwards, ``__has_builtin(__X)`` can be used.
* From Clang 6 onwards, ``!__is_identifier(__X)`` can be used.
* From Clang 3 onwards, ``__has_feature(X)`` can be used, but only supports
  the following traits:

  * ``__has_nothrow_assign``
  * ``__has_nothrow_copy``
  * ``__has_nothrow_constructor``
  * ``__has_trivial_assign``
  * ``__has_trivial_copy``
  * ``__has_trivial_constructor``
  * ``__has_trivial_destructor``
  * ``__has_virtual_destructor``
  * ``__is_abstract``
  * ``__is_base_of``
  * ``__is_class``
  * ``__is_constructible``
  * ``__is_convertible_to``
  * ``__is_empty``
  * ``__is_enum``
  * ``__is_final``
  * ``__is_literal``
  * ``__is_standard_layout``
  * ``__is_pod``
  * ``__is_polymorphic``
  * ``__is_sealed``
  * ``__is_trivial``
  * ``__is_trivially_assignable``
  * ``__is_trivially_constructible``
  * ``__is_trivially_copyable``
  * ``__is_union``
  * ``__underlying_type``

A simplistic usage example as might be seen in standard C++ headers follows:

.. code-block:: c++

  #if __has_builtin(__is_convertible_to)
  template<typename From, typename To>
  struct is_convertible_to {
    static const bool value = __is_convertible_to(From, To);
  };
  #else
  // Emulate type trait for compatibility with other compilers.
  #endif

Blocks
======

The syntax and high level language feature description is in
:doc:`BlockLanguageSpec<BlockLanguageSpec>`. Implementation and ABI details for
the clang implementation are in :doc:`Block-ABI-Apple<Block-ABI-Apple>`.

Query for this feature with ``__has_extension(blocks)``.

ASM Goto with Output Constraints
================================

Outputs may be used along any branches from the ``asm goto`` whether the
branches are taken or not.

Query for this feature with ``__has_extension(gnu_asm_goto_with_outputs)``.

Prior to clang-16, the output may only be used safely when the indirect
branches are not taken.  Query for this difference with
``__has_extension(gnu_asm_goto_with_outputs_full)``.

When using tied-outputs (i.e. outputs that are inputs and outputs, not just
outputs) with the `+r` constraint, there is a hidden input that's created
before the label, so numeric references to operands must account for that.

.. code-block:: c++

  int foo(int x) {
      // %0 and %1 both refer to x
      // %l2 refers to err
      asm goto("# %0 %1 %l2" : "+r"(x) : : : err);
      return x;
    err:
      return -1;
  }

This was changed to match GCC in clang-13; for better portability, symbolic
references can be used instead of numeric references.

.. code-block:: c++

  int foo(int x) {
      asm goto("# %[x] %l[err]" : [x]"+r"(x) : : : err);
      return x;
    err:
      return -1;
  }

Objective-C Features
====================

Related result types
--------------------

According to Cocoa conventions, Objective-C methods with certain names
("``init``", "``alloc``", etc.) always return objects that are an instance of
the receiving class's type.  Such methods are said to have a "related result
type", meaning that a message send to one of these methods will have the same
static type as an instance of the receiver class.  For example, given the
following classes:

.. code-block:: objc

  @interface NSObject
  + (id)alloc;
  - (id)init;
  @end

  @interface NSArray : NSObject
  @end

and this common initialization pattern

.. code-block:: objc

  NSArray *array = [[NSArray alloc] init];

the type of the expression ``[NSArray alloc]`` is ``NSArray*`` because
``alloc`` implicitly has a related result type.  Similarly, the type of the
expression ``[[NSArray alloc] init]`` is ``NSArray*``, since ``init`` has a
related result type and its receiver is known to have the type ``NSArray *``.
If neither ``alloc`` nor ``init`` had a related result type, the expressions
would have had type ``id``, as declared in the method signature.

A method with a related result type can be declared by using the type
``instancetype`` as its result type.  ``instancetype`` is a contextual keyword
that is only permitted in the result type of an Objective-C method, e.g.

.. code-block:: objc

  @interface A
  + (instancetype)constructAnA;
  @end

The related result type can also be inferred for some methods.  To determine
whether a method has an inferred related result type, the first word in the
camel-case selector (e.g., "``init``" in "``initWithObjects``") is considered,
and the method will have a related result type if its return type is compatible
with the type of its class and if:

* the first word is "``alloc``" or "``new``", and the method is a class method,
  or

* the first word is "``autorelease``", "``init``", "``retain``", or "``self``",
  and the method is an instance method.

If a method with a related result type is overridden by a subclass method, the
subclass method must also return a type that is compatible with the subclass
type.  For example:

.. code-block:: objc

  @interface NSString : NSObject
  - (NSUnrelated *)init; // incorrect usage: NSUnrelated is not NSString or a superclass of NSString
  @end

Related result types only affect the type of a message send or property access
via the given method.  In all other respects, a method with a related result
type is treated the same way as method that returns ``id``.

Use ``__has_feature(objc_instancetype)`` to determine whether the
``instancetype`` contextual keyword is available.

Automatic reference counting
----------------------------

Clang provides support for :doc:`automated reference counting
<AutomaticReferenceCounting>` in Objective-C, which eliminates the need
for manual ``retain``/``release``/``autorelease`` message sends.  There are three
feature macros associated with automatic reference counting:
``__has_feature(objc_arc)`` indicates the availability of automated reference
counting in general, while ``__has_feature(objc_arc_weak)`` indicates that
automated reference counting also includes support for ``__weak`` pointers to
Objective-C objects. ``__has_feature(objc_arc_fields)`` indicates that C structs
are allowed to have fields that are pointers to Objective-C objects managed by
automatic reference counting.

.. _objc-weak:

Weak references
---------------

Clang supports ARC-style weak and unsafe references in Objective-C even
outside of ARC mode.  Weak references must be explicitly enabled with
the ``-fobjc-weak`` option; use ``__has_feature((objc_arc_weak))``
to test whether they are enabled.  Unsafe references are enabled
unconditionally.  ARC-style weak and unsafe references cannot be used
when Objective-C garbage collection is enabled.

Except as noted below, the language rules for the ``__weak`` and
``__unsafe_unretained`` qualifiers (and the ``weak`` and
``unsafe_unretained`` property attributes) are just as laid out
in the :doc:`ARC specification <AutomaticReferenceCounting>`.
In particular, note that some classes do not support forming weak
references to their instances, and note that special care must be
taken when storing weak references in memory where initialization
and deinitialization are outside the responsibility of the compiler
(such as in ``malloc``-ed memory).

Loading from a ``__weak`` variable always implicitly retains the
loaded value.  In non-ARC modes, this retain is normally balanced
by an implicit autorelease.  This autorelease can be suppressed
by performing the load in the receiver position of a ``-retain``
message send (e.g. ``[weakReference retain]``); note that this performs
only a single retain (the retain done when primitively loading from
the weak reference).

For the most part, ``__unsafe_unretained`` in non-ARC modes is just the
default behavior of variables and therefore is not needed.  However,
it does have an effect on the semantics of block captures: normally,
copying a block which captures an Objective-C object or block pointer
causes the captured pointer to be retained or copied, respectively,
but that behavior is suppressed when the captured variable is qualified
with ``__unsafe_unretained``.

Note that the ``__weak`` qualifier formerly meant the GC qualifier in
all non-ARC modes and was silently ignored outside of GC modes.  It now
means the ARC-style qualifier in all non-GC modes and is no longer
allowed if not enabled by either ``-fobjc-arc`` or ``-fobjc-weak``.
It is expected that ``-fobjc-weak`` will eventually be enabled by default
in all non-GC Objective-C modes.

.. _objc-fixed-enum:

Enumerations with a fixed underlying type
-----------------------------------------

Clang provides support for C++11 enumerations with a fixed underlying type
within Objective-C.  For example, one can write an enumeration type as:

.. code-block:: c++

  typedef enum : unsigned char { Red, Green, Blue } Color;

This specifies that the underlying type, which is used to store the enumeration
value, is ``unsigned char``.

Use ``__has_feature(objc_fixed_enum)`` to determine whether support for fixed
underlying types is available in Objective-C.

Interoperability with C++11 lambdas
-----------------------------------

Clang provides interoperability between C++11 lambdas and blocks-based APIs, by
permitting a lambda to be implicitly converted to a block pointer with the
corresponding signature.  For example, consider an API such as ``NSArray``'s
array-sorting method:

.. code-block:: objc

  - (NSArray *)sortedArrayUsingComparator:(NSComparator)cmptr;

``NSComparator`` is simply a typedef for the block pointer ``NSComparisonResult
(^)(id, id)``, and parameters of this type are generally provided with block
literals as arguments.  However, one can also use a C++11 lambda so long as it
provides the same signature (in this case, accepting two parameters of type
``id`` and returning an ``NSComparisonResult``):

.. code-block:: objc

  NSArray *array = @[@"string 1", @"string 21", @"string 12", @"String 11",
                     @"String 02"];
  const NSStringCompareOptions comparisonOptions
    = NSCaseInsensitiveSearch | NSNumericSearch |
      NSWidthInsensitiveSearch | NSForcedOrderingSearch;
  NSLocale *currentLocale = [NSLocale currentLocale];
  NSArray *sorted
    = [array sortedArrayUsingComparator:[=](id s1, id s2) -> NSComparisonResult {
               NSRange string1Range = NSMakeRange(0, [s1 length]);
               return [s1 compare:s2 options:comparisonOptions
               range:string1Range locale:currentLocale];
       }];
  NSLog(@"sorted: %@", sorted);

This code relies on an implicit conversion from the type of the lambda
expression (an unnamed, local class type called the *closure type*) to the
corresponding block pointer type.  The conversion itself is expressed by a
conversion operator in that closure type that produces a block pointer with the
same signature as the lambda itself, e.g.,

.. code-block:: objc

  operator NSComparisonResult (^)(id, id)() const;

This conversion function returns a new block that simply forwards the two
parameters to the lambda object (which it captures by copy), then returns the
result.  The returned block is first copied (with ``Block_copy``) and then
autoreleased.  As an optimization, if a lambda expression is immediately
converted to a block pointer (as in the first example, above), then the block
is not copied and autoreleased: rather, it is given the same lifetime as a
block literal written at that point in the program, which avoids the overhead
of copying a block to the heap in the common case.

The conversion from a lambda to a block pointer is only available in
Objective-C++, and not in C++ with blocks, due to its use of Objective-C memory
management (autorelease).

Object Literals and Subscripting
--------------------------------

Clang provides support for :doc:`Object Literals and Subscripting
<ObjectiveCLiterals>` in Objective-C, which simplifies common Objective-C
programming patterns, makes programs more concise, and improves the safety of
container creation.  There are several feature macros associated with object
literals and subscripting: ``__has_feature(objc_array_literals)`` tests the
availability of array literals; ``__has_feature(objc_dictionary_literals)``
tests the availability of dictionary literals;
``__has_feature(objc_subscripting)`` tests the availability of object
subscripting.

Objective-C Autosynthesis of Properties
---------------------------------------

Clang provides support for autosynthesis of declared properties.  Using this
feature, clang provides default synthesis of those properties not declared
@dynamic and not having user provided backing getter and setter methods.
``__has_feature(objc_default_synthesize_properties)`` checks for availability
of this feature in version of clang being used.

.. _langext-objc-retain-release:

Objective-C retaining behavior attributes
-----------------------------------------

In Objective-C, functions and methods are generally assumed to follow the
`Cocoa Memory Management
<https://developer.apple.com/library/mac/#documentation/Cocoa/Conceptual/MemoryMgmt/Articles/mmRules.html>`_
conventions for ownership of object arguments and
return values. However, there are exceptions, and so Clang provides attributes
to allow these exceptions to be documented. This are used by ARC and the
`static analyzer <https://clang-analyzer.llvm.org>`_ Some exceptions may be
better described using the ``objc_method_family`` attribute instead.

**Usage**: The ``ns_returns_retained``, ``ns_returns_not_retained``,
``ns_returns_autoreleased``, ``cf_returns_retained``, and
``cf_returns_not_retained`` attributes can be placed on methods and functions
that return Objective-C or CoreFoundation objects. They are commonly placed at
the end of a function prototype or method declaration:

.. code-block:: objc

  id foo() __attribute__((ns_returns_retained));

  - (NSString *)bar:(int)x __attribute__((ns_returns_retained));

The ``*_returns_retained`` attributes specify that the returned object has a +1
retain count.  The ``*_returns_not_retained`` attributes specify that the return
object has a +0 retain count, even if the normal convention for its selector
would be +1.  ``ns_returns_autoreleased`` specifies that the returned object is
+0, but is guaranteed to live at least as long as the next flush of an
autorelease pool.

**Usage**: The ``ns_consumed`` and ``cf_consumed`` attributes can be placed on
a parameter declaration; they specify that the argument is expected to have a
+1 retain count, which will be balanced in some way by the function or method.
The ``ns_consumes_self`` attribute can only be placed on an Objective-C
method; it specifies that the method expects its ``self`` parameter to have a
+1 retain count, which it will balance in some way.

.. code-block:: objc

  void foo(__attribute__((ns_consumed)) NSString *string);

  - (void) bar __attribute__((ns_consumes_self));
  - (void) baz:(id) __attribute__((ns_consumed)) x;

Further examples of these attributes are available in the static analyzer's `list of annotations for analysis
<https://clang-analyzer.llvm.org/annotations.html#cocoa_mem>`_.

Query for these features with ``__has_attribute(ns_consumed)``,
``__has_attribute(ns_returns_retained)``, etc.

Objective-C @available
----------------------

It is possible to use the newest SDK but still build a program that can run on
older versions of macOS and iOS by passing ``-mmacos-version-min=`` /
``-miphoneos-version-min=``.

Before LLVM 5.0, when calling a function that exists only in the OS that's
newer than the target OS (as determined by the minimum deployment version),
programmers had to carefully check if the function exists at runtime, using
null checks for weakly-linked C functions, ``+class`` for Objective-C classes,
and ``-respondsToSelector:`` or ``+instancesRespondToSelector:`` for
Objective-C methods.  If such a check was missed, the program would compile
fine, run fine on newer systems, but crash on older systems.

As of LLVM 5.0, ``-Wunguarded-availability`` uses the `availability attributes
<https://clang.llvm.org/docs/AttributeReference.html#availability>`_ together
with the new ``@available()`` keyword to assist with this issue.
When a method that's introduced in the OS newer than the target OS is called, a
-Wunguarded-availability warning is emitted if that call is not guarded:

.. code-block:: objc

  void my_fun(NSSomeClass* var) {
    // If fancyNewMethod was added in e.g. macOS 10.12, but the code is
    // built with -mmacos-version-min=10.11, then this unconditional call
    // will emit a -Wunguarded-availability warning:
    [var fancyNewMethod];
  }

To fix the warning and to avoid the crash on macOS 10.11, wrap it in
``if(@available())``:

.. code-block:: objc

  void my_fun(NSSomeClass* var) {
    if (@available(macOS 10.12, *)) {
      [var fancyNewMethod];
    } else {
      // Put fallback behavior for old macOS versions (and for non-mac
      // platforms) here.
    }
  }

The ``*`` is required and means that platforms not explicitly listed will take
the true branch, and the compiler will emit ``-Wunguarded-availability``
warnings for unlisted platforms based on those platform's deployment target.
More than one platform can be listed in ``@available()``:

.. code-block:: objc

  void my_fun(NSSomeClass* var) {
    if (@available(macOS 10.12, iOS 10, *)) {
      [var fancyNewMethod];
    }
  }

If the caller of ``my_fun()`` already checks that ``my_fun()`` is only called
on 10.12, then add an `availability attribute
<https://clang.llvm.org/docs/AttributeReference.html#availability>`_ to it,
which will also suppress the warning and require that calls to my_fun() are
checked:

.. code-block:: objc

  API_AVAILABLE(macos(10.12)) void my_fun(NSSomeClass* var) {
    [var fancyNewMethod];  // Now ok.
  }

``@available()`` is only available in Objective-C code.  To use the feature
in C and C++ code, use the ``__builtin_available()`` spelling instead.

If existing code uses null checks or ``-respondsToSelector:``, it should
be changed to use ``@available()`` (or ``__builtin_available``) instead.

``-Wunguarded-availability`` is disabled by default, but
``-Wunguarded-availability-new``, which only emits this warning for APIs
that have been introduced in macOS >= 10.13, iOS >= 11, watchOS >= 4 and
tvOS >= 11, is enabled by default.

.. _langext-overloading:

Objective-C++ ABI: protocol-qualifier mangling of parameters
------------------------------------------------------------

Starting with LLVM 3.4, Clang produces a new mangling for parameters whose
type is a qualified-``id`` (e.g., ``id<Foo>``).  This mangling allows such
parameters to be differentiated from those with the regular unqualified ``id``
type.

This was a non-backward compatible mangling change to the ABI.  This change
allows proper overloading, and also prevents mangling conflicts with template
parameters of protocol-qualified type.

Query the presence of this new mangling with
``__has_feature(objc_protocol_qualifier_mangling)``.

Initializer lists for complex numbers in C
==========================================

clang supports an extension which allows the following in C:

.. code-block:: c++

  #include <math.h>
  #include <complex.h>
  complex float x = { 1.0f, INFINITY }; // Init to (1, Inf)

This construct is useful because there is no way to separately initialize the
real and imaginary parts of a complex variable in standard C, given that clang
does not support ``_Imaginary``.  (Clang also supports the ``__real__`` and
``__imag__`` extensions from gcc, which help in some cases, but are not usable
in static initializers.)

Note that this extension does not allow eliding the braces; the meaning of the
following two lines is different:

.. code-block:: c++

  complex float x[] = { { 1.0f, 1.0f } }; // [0] = (1, 1)
  complex float x[] = { 1.0f, 1.0f }; // [0] = (1, 0), [1] = (1, 0)

This extension also works in C++ mode, as far as that goes, but does not apply
to the C++ ``std::complex``.  (In C++11, list initialization allows the same
syntax to be used with ``std::complex`` with the same meaning.)

For GCC compatibility, ``__builtin_complex(re, im)`` can also be used to
construct a complex number from the given real and imaginary components.

OpenCL Features
===============

Clang supports internal OpenCL extensions documented below.

``__cl_clang_bitfields``
--------------------------------

With this extension it is possible to enable bitfields in structs
or unions using the OpenCL extension pragma mechanism detailed in
`the OpenCL Extension Specification, section 1.2
<https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_Ext.html#extensions-overview>`_.

Use of bitfields in OpenCL kernels can result in reduced portability as struct
layout is not guaranteed to be consistent when compiled by different compilers.
If structs with bitfields are used as kernel function parameters, it can result
in incorrect functionality when the layout is different between the host and
device code.

**Example of Use**:

.. code-block:: c++

  #pragma OPENCL EXTENSION __cl_clang_bitfields : enable
  struct with_bitfield {
    unsigned int i : 5; // compiled - no diagnostic generated
  };

  #pragma OPENCL EXTENSION __cl_clang_bitfields : disable
  struct without_bitfield {
    unsigned int i : 5; // error - bitfields are not supported
  };

``__cl_clang_function_pointers``
--------------------------------

With this extension it is possible to enable various language features that
are relying on function pointers using regular OpenCL extension pragma
mechanism detailed in `the OpenCL Extension Specification,
section 1.2
<https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_Ext.html#extensions-overview>`_.

In C++ for OpenCL this also enables:

- Use of member function pointers;

- Unrestricted use of references to functions;

- Virtual member functions.

Such functionality is not conformant and does not guarantee to compile
correctly in any circumstances. It can be used if:

- the kernel source does not contain call expressions to (member-) function
  pointers, or virtual functions. For example this extension can be used in
  metaprogramming algorithms to be able to specify/detect types generically.

- the generated kernel binary does not contain indirect calls because they
  are eliminated using compiler optimizations e.g. devirtualization.

- the selected target supports the function pointer like functionality e.g.
  most CPU targets.

**Example of Use**:

.. code-block:: c++

  #pragma OPENCL EXTENSION __cl_clang_function_pointers : enable
  void foo()
  {
    void (*fp)(); // compiled - no diagnostic generated
  }

  #pragma OPENCL EXTENSION __cl_clang_function_pointers : disable
  void bar()
  {
    void (*fp)(); // error - pointers to function are not allowed
  }

``__cl_clang_variadic_functions``
---------------------------------

With this extension it is possible to enable variadic arguments in functions
using regular OpenCL extension pragma mechanism detailed in `the OpenCL
Extension Specification, section 1.2
<https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_Ext.html#extensions-overview>`_.

This is not conformant behavior and it can only be used portably when the
functions with variadic prototypes do not get generated in binary e.g. the
variadic prototype is used to specify a function type with any number of
arguments in metaprogramming algorithms in C++ for OpenCL.

This extensions can also be used when the kernel code is intended for targets
supporting the variadic arguments e.g. majority of CPU targets.

**Example of Use**:

.. code-block:: c++

  #pragma OPENCL EXTENSION __cl_clang_variadic_functions : enable
  void foo(int a, ...); // compiled - no diagnostic generated

  #pragma OPENCL EXTENSION __cl_clang_variadic_functions : disable
  void bar(int a, ...); // error - variadic prototype is not allowed

``__cl_clang_non_portable_kernel_param_types``
----------------------------------------------

With this extension it is possible to enable the use of some restricted types
in kernel parameters specified in `C++ for OpenCL v1.0 s2.4
<https://www.khronos.org/opencl/assets/CXX_for_OpenCL.html#kernel_function>`_.
The restrictions can be relaxed using regular OpenCL extension pragma mechanism
detailed in `the OpenCL Extension Specification, section 1.2
<https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_Ext.html#extensions-overview>`_.

This is not a conformant behavior and it can only be used when the
kernel arguments are not accessed on the host side or the data layout/size
between the host and device is known to be compatible.

**Example of Use**:

.. code-block:: c++

  // Plain Old Data type.
  struct Pod {
    int a;
    int b;
  };

  // Not POD type because of the constructor.
  // Standard layout type because there is only one access control.
  struct OnlySL {
    int a;
    int b;
    OnlySL() : a(0), b(0) {}
  };

  // Not standard layout type because of two different access controls.
  struct NotSL {
    int a;
  private:
    int b;
  };

  #pragma OPENCL EXTENSION __cl_clang_non_portable_kernel_param_types : enable
  kernel void kernel_main(
    Pod a,

    OnlySL b,
    global NotSL *c,
    global OnlySL *d
  );
  #pragma OPENCL EXTENSION __cl_clang_non_portable_kernel_param_types : disable

Remove address space builtin function
-------------------------------------

``__remove_address_space`` allows to derive types in C++ for OpenCL
that have address space qualifiers removed. This utility only affects
address space qualifiers, therefore, other type qualifiers such as
``const`` or ``volatile`` remain unchanged.

**Example of Use**:

.. code-block:: c++

  template<typename T>
  void foo(T *par){
    T var1; // error - local function variable with global address space
    __private T var2; // error - conflicting address space qualifiers
    __private __remove_address_space<T>::type var3; // var3 is __private int
  }

  void bar(){
    __global int* ptr;
    foo(ptr);
  }

Legacy 1.x atomics with generic address space
---------------------------------------------

Clang allows use of atomic functions from the OpenCL 1.x standards
with the generic address space pointer in C++ for OpenCL mode.

This is a non-portable feature and might not be supported by all
targets.

**Example of Use**:

.. code-block:: c++

  void foo(__generic volatile unsigned int* a) {
    atomic_add(a, 1);
  }

WebAssembly Features
====================

Clang supports the WebAssembly features documented below. For further
information related to the semantics of the builtins, please refer to the `WebAssembly Specification <https://webassembly.github.io/spec/core/>`_.
In this section, when we refer to reference types, we are referring to
WebAssembly reference types, not C++ reference types unless stated
otherwise.

``__builtin_wasm_table_set``
----------------------------

This builtin function stores a value in a WebAssembly table.
It takes three arguments.
The first argument is the table to store a value into, the second
argument is the index to which to store the value into, and the
third argument is a value of reference type to store in the table.
It returns nothing.

.. code-block:: c++

  static __externref_t table[0];
  extern __externref_t JSObj;

  void store(int index) {
    __builtin_wasm_table_set(table, index, JSObj);
  }

``__builtin_wasm_table_get``
----------------------------

This builtin function is the counterpart to ``__builtin_wasm_table_set``
and loads a value from a WebAssembly table of reference typed values.
It takes 2 arguments.
The first argument is a table of reference typed values and the
second argument is an index from which to load the value. It returns
the loaded reference typed value.

.. code-block:: c++

  static __externref_t table[0];

  __externref_t load(int index) {
    __externref_t Obj = __builtin_wasm_table_get(table, index);
    return Obj;
  }

``__builtin_wasm_table_size``
-----------------------------

This builtin function returns the size of the WebAssembly table.
Takes the table as an argument and returns an unsigned integer (``size_t``)
with the current table size.

.. code-block:: c++

  typedef void (*__funcref funcref_t)();
  static __funcref table[0];

  size_t getSize() {
    return __builtin_wasm_table_size(table);
  }

``__builtin_wasm_table_grow``
-----------------------------

This builtin function grows the WebAssembly table by a certain amount.
Currently, as all WebAssembly tables created in C/C++ are zero-sized,
this always needs to be called to grow the table.

It takes three arguments. The first argument is the WebAssembly table
to grow. The second argument is the reference typed value to store in
the new table entries (the initialization value), and the third argument
is the amount to grow the table by. It returns the previous table size
or -1. It will return -1 if not enough space could be allocated.

.. code-block:: c++

  typedef void (*__funcref funcref_t)();
  static __funcref table[0];

  // grow returns the new table size or -1 on error.
  int grow(__funcref fn, int delta) {
    int prevSize = __builtin_wasm_table_grow(table, fn, delta);
    if (prevSize == -1)
      return -1;
    return prevSize + delta;
  }

``__builtin_wasm_table_fill``
-----------------------------

This builtin function sets all the entries of a WebAssembly table to a given
reference typed value. It takes four arguments. The first argument is
the WebAssembly table, the second argument is the index that starts the
range, the third argument is the value to set in the new entries, and
the fourth and the last argument is the size of the range. It returns
nothing.

.. code-block:: c++

  static __externref_t table[0];

  // resets a table by setting all of its entries to a given value.
  void reset(__externref_t Obj) {
    int Size = __builtin_wasm_table_size(table);
    __builtin_wasm_table_fill(table, 0, Obj, Size);
  }

``__builtin_wasm_table_copy``
-----------------------------

This builtin function copies elements from a source WebAssembly table
to a possibly overlapping destination region. It takes five arguments.
The first argument is the destination WebAssembly table, and the second
argument is the source WebAssembly table. The third argument is the
destination index from where the copy starts, the fourth argument is the
source index from there the copy starts, and the fifth and last argument
is the number of elements to copy. It returns nothing.

.. code-block:: c++

  static __externref_t tableSrc[0];
  static __externref_t tableDst[0];

  // Copy nelem elements from [src, src + nelem - 1] in tableSrc to
  // [dst, dst + nelem - 1] in tableDst
  void copy(int dst, int src, int nelem) {
    __builtin_wasm_table_copy(tableDst, tableSrc, dst, src, nelem);
  }


Builtin Functions
=================

Clang supports a number of builtin library functions with the same syntax as
GCC, including things like ``__builtin_nan``, ``__builtin_constant_p``,
``__builtin_choose_expr``, ``__builtin_types_compatible_p``,
``__builtin_assume_aligned``, ``__sync_fetch_and_add``, etc.  In addition to
the GCC builtins, Clang supports a number of builtins that GCC does not, which
are listed here.

Please note that Clang does not and will not support all of the GCC builtins
for vector operations.  Instead of using builtins, you should use the functions
defined in target-specific header files like ``<xmmintrin.h>``, which define
portable wrappers for these.  Many of the Clang versions of these functions are
implemented directly in terms of :ref:`extended vector support
<langext-vectors>` instead of builtins, in order to reduce the number of
builtins that we need to implement.

``__builtin_alloca``
--------------------

``__builtin_alloca`` is used to dynamically allocate memory on the stack. Memory
is automatically freed upon function termination.

**Syntax**:

.. code-block:: c++

  __builtin_alloca(size_t n)

**Example of Use**:

.. code-block:: c++

  void init(float* data, size_t nbelems);
  void process(float* data, size_t nbelems);
  int foo(size_t n) {
    auto mem = (float*)__builtin_alloca(n * sizeof(float));
    init(mem, n);
    process(mem, n);
    /* mem is automatically freed at this point */
  }

**Description**:

``__builtin_alloca`` is meant to be used to allocate a dynamic amount of memory
on the stack. This amount is subject to stack allocation limits.

Query for this feature with ``__has_builtin(__builtin_alloca)``.

``__builtin_alloca_with_align``
-------------------------------

``__builtin_alloca_with_align`` is used to dynamically allocate memory on the
stack while controlling its alignment. Memory is automatically freed upon
function termination.


**Syntax**:

.. code-block:: c++

  __builtin_alloca_with_align(size_t n, size_t align)

**Example of Use**:

.. code-block:: c++

  void init(float* data, size_t nbelems);
  void process(float* data, size_t nbelems);
  int foo(size_t n) {
    auto mem = (float*)__builtin_alloca_with_align(
                        n * sizeof(float),
                        CHAR_BIT * alignof(float));
    init(mem, n);
    process(mem, n);
    /* mem is automatically freed at this point */
  }

**Description**:

``__builtin_alloca_with_align`` is meant to be used to allocate a dynamic amount of memory
on the stack. It is similar to ``__builtin_alloca`` but accepts a second
argument whose value is the alignment constraint, as a power of 2 in *bits*.

Query for this feature with ``__has_builtin(__builtin_alloca_with_align)``.

.. _langext-__builtin_assume:

``__builtin_assume``
--------------------

``__builtin_assume`` is used to provide the optimizer with a boolean
invariant that is defined to be true.

**Syntax**:

.. code-block:: c++

    __builtin_assume(bool)

**Example of Use**:

.. code-block:: c++

  int foo(int x) {
      __builtin_assume(x != 0);
      // The optimizer may short-circuit this check using the invariant.
      if (x == 0)
            return do_something();
      return do_something_else();
  }

**Description**:

The boolean argument to this function is defined to be true. The optimizer may
analyze the form of the expression provided as the argument and deduce from
that information used to optimize the program. If the condition is violated
during execution, the behavior is undefined. The argument itself is never
evaluated, so any side effects of the expression will be discarded.

Query for this feature with ``__has_builtin(__builtin_assume)``.

.. _langext-__builtin_assume_separate_storage:

``__builtin_assume_separate_storage``
-------------------------------------

``__builtin_assume_separate_storage`` is used to provide the optimizer with the
knowledge that its two arguments point to separately allocated objects.

**Syntax**:

.. code-block:: c++

    __builtin_assume_separate_storage(const volatile void *, const volatile void *)

**Example of Use**:

.. code-block:: c++

  int foo(int *x, int *y) {
      __builtin_assume_separate_storage(x, y);
      *x = 0;
      *y = 1;
      // The optimizer may optimize this to return 0 without reloading from *x.
      return *x;
  }

**Description**:

The arguments to this function are assumed to point into separately allocated
storage (either different variable definitions or different dynamic storage
allocations). The optimizer may use this fact to aid in alias analysis. If the
arguments point into the same storage, the behavior is undefined. Note that the
definition of "storage" here refers to the outermost enclosing allocation of any
particular object (so for example, it's never correct to call this function
passing the addresses of fields in the same struct, elements of the same array,
etc.).

Query for this feature with ``__has_builtin(__builtin_assume_separate_storage)``.


``__builtin_offsetof``
----------------------

``__builtin_offsetof`` is used to implement the ``offsetof`` macro, which
calculates the offset (in bytes) to a given member of the given type.

**Syntax**:

.. code-block:: c++

    __builtin_offsetof(type-name, member-designator)

**Example of Use**:

.. code-block:: c++

  struct S {
    char c;
    int i;
    struct T {
      float f[2];
    } t;
  };

  const int offset_to_i = __builtin_offsetof(struct S, i);
  const int ext1 = __builtin_offsetof(struct U { int i; }, i); // C extension
  const int offset_to_subobject = __builtin_offsetof(struct S, t.f[1]);

**Description**:

This builtin is usable in an integer constant expression which returns a value
of type ``size_t``. The value returned is the offset in bytes to the subobject
designated by the member-designator from the beginning of an object of type
``type-name``. Clang extends the required standard functionality in the
following way:

* In C language modes, the first argument may be the definition of a new type.
  Any type declared this way is scoped to the nearest scope containing the call
  to the builtin.

Query for this feature with ``__has_builtin(__builtin_offsetof)``.

``__builtin_call_with_static_chain``
------------------------------------

``__builtin_call_with_static_chain`` is used to perform a static call while
setting updating the static chain register.

**Syntax**:

.. code-block:: c++

  T __builtin_call_with_static_chain(T expr, void* ptr)

**Example of Use**:

.. code-block:: c++

  auto v = __builtin_call_with_static_chain(foo(3), foo);

**Description**:

This builtin returns ``expr`` after checking that ``expr`` is a non-member
static call expression. The call to that expression is made while using ``ptr``
as a function pointer stored in a dedicated register to implement *static chain*
calling convention, as used by some language to implement closures or nested
functions.

Query for this feature with ``__has_builtin(__builtin_call_with_static_chain)``.

``__builtin_readcyclecounter``
------------------------------

``__builtin_readcyclecounter`` is used to access the cycle counter register (or
a similar low-latency, high-accuracy clock) on those targets that support it.

**Syntax**:

.. code-block:: c++

  __builtin_readcyclecounter()

**Example of Use**:

.. code-block:: c++

  unsigned long long t0 = __builtin_readcyclecounter();
  do_something();
  unsigned long long t1 = __builtin_readcyclecounter();
  unsigned long long cycles_to_do_something = t1 - t0; // assuming no overflow

**Description**:

The ``__builtin_readcyclecounter()`` builtin returns the cycle counter value,
which may be either global or process/thread-specific depending on the target.
As the backing counters often overflow quickly (on the order of seconds) this
should only be used for timing small intervals.  When not supported by the
target, the return value is always zero.  This builtin takes no arguments and
produces an unsigned long long result.

Query for this feature with ``__has_builtin(__builtin_readcyclecounter)``. Note
that even if present, its use may depend on run-time privilege or other OS
controlled state.

``__builtin_readsteadycounter``
-------------------------------

``__builtin_readsteadycounter`` is used to access the fixed frequency counter
register (or a similar steady-rate clock) on those targets that support it.
The function is similar to ``__builtin_readcyclecounter`` above except that the
frequency is fixed, making it suitable for measuring elapsed time.

**Syntax**:

.. code-block:: c++

  __builtin_readsteadycounter()

**Example of Use**:

.. code-block:: c++

  unsigned long long t0 = __builtin_readsteadycounter();
  do_something();
  unsigned long long t1 = __builtin_readsteadycounter();
  unsigned long long secs_to_do_something = (t1 - t0) / tick_rate;

**Description**:

The ``__builtin_readsteadycounter()`` builtin returns the frequency counter value.
When not supported by the target, the return value is always zero. This builtin
takes no arguments and produces an unsigned long long result. The builtin does
not guarantee any particular frequency, only that it is stable. Knowledge of the
counter's true frequency will need to be provided by the user.

Query for this feature with ``__has_builtin(__builtin_readsteadycounter)``.

``__builtin_cpu_supports``
--------------------------

**Syntax**:

.. code-block:: c++

  int __builtin_cpu_supports(const char *features);

**Example of Use:**:

.. code-block:: c++

  if (__builtin_cpu_supports("sve"))
    sve_code();

**Description**:

The ``__builtin_cpu_supports`` function detects if the run-time CPU supports
features specified in string argument. It returns a positive integer if all
features are supported and 0 otherwise. Feature names are target specific. On
AArch64 features are combined using ``+`` like this
``__builtin_cpu_supports("flagm+sha3+lse+rcpc2+fcma+memtag+bti+sme2")``.
If a feature name is not supported, Clang will issue a warning and replace
builtin by the constant 0.

Query for this feature with ``__has_builtin(__builtin_cpu_supports)``.

``__builtin_dump_struct``
-------------------------

**Syntax**:

.. code-block:: c++

    __builtin_dump_struct(&some_struct, some_printf_func, args...);

**Examples**:

.. code-block:: c++

    struct S {
      int x, y;
      float f;
      struct T {
        int i;
      } t;
    };

    void func(struct S *s) {
      __builtin_dump_struct(s, printf);
    }

Example output:

.. code-block:: none

    struct S {
      int x = 100
      int y = 42
      float f = 3.141593
      struct T t = {
        int i = 1997
      }
    }

.. code-block:: c++

    #include <string>
    struct T { int a, b; };
    constexpr void constexpr_sprintf(std::string &out, const char *format,
                                     auto ...args) {
      // ...
    }
    constexpr std::string dump_struct(auto &x) {
      std::string s;
      __builtin_dump_struct(&x, constexpr_sprintf, s);
      return s;
    }
    static_assert(dump_struct(T{1, 2}) == R"(struct T {
      int a = 1
      int b = 2
    }
    )");

**Description**:

The ``__builtin_dump_struct`` function is used to print the fields of a simple
structure and their values for debugging purposes. The first argument of the
builtin should be a pointer to a complete record type to dump. The second argument ``f``
should be some callable expression, and can be a function object or an overload
set. The builtin calls ``f``, passing any further arguments ``args...``
followed by a ``printf``-compatible format string and the corresponding
arguments. ``f`` may be called more than once, and ``f`` and ``args`` will be
evaluated once per call. In C++, ``f`` may be a template or overload set and
resolve to different functions for each call.

In the format string, a suitable format specifier will be used for builtin
types that Clang knows how to format. This includes standard builtin types, as
well as aggregate structures, ``void*`` (printed with ``%p``), and ``const
char*`` (printed with ``%s``). A ``*%p`` specifier will be used for a field
that Clang doesn't know how to format, and the corresponding argument will be a
pointer to the field. This allows a C++ templated formatting function to detect
this case and implement custom formatting. A ``*`` will otherwise not precede a
format specifier.

This builtin does not return a value.

This builtin can be used in constant expressions.

Query for this feature with ``__has_builtin(__builtin_dump_struct)``

.. _langext-__builtin_shufflevector:

``__builtin_shufflevector``
---------------------------

``__builtin_shufflevector`` is used to express generic vector
permutation/shuffle/swizzle operations.  This builtin is also very important
for the implementation of various target-specific header files like
``<xmmintrin.h>``. This builtin can be used within constant expressions.

**Syntax**:

.. code-block:: c++

  __builtin_shufflevector(vec1, vec2, index1, index2, ...)

**Examples**:

.. code-block:: c++

  // identity operation - return 4-element vector v1.
  __builtin_shufflevector(v1, v1, 0, 1, 2, 3)

  // "Splat" element 0 of V1 into a 4-element result.
  __builtin_shufflevector(V1, V1, 0, 0, 0, 0)

  // Reverse 4-element vector V1.
  __builtin_shufflevector(V1, V1, 3, 2, 1, 0)

  // Concatenate every other element of 4-element vectors V1 and V2.
  __builtin_shufflevector(V1, V2, 0, 2, 4, 6)

  // Concatenate every other element of 8-element vectors V1 and V2.
  __builtin_shufflevector(V1, V2, 0, 2, 4, 6, 8, 10, 12, 14)

  // Shuffle v1 with some elements being undefined. Not allowed in constexpr.
  __builtin_shufflevector(v1, v1, 3, -1, 1, -1)

**Description**:

The first two arguments to ``__builtin_shufflevector`` are vectors that have
the same element type.  The remaining arguments are a list of integers that
specify the elements indices of the first two vectors that should be extracted
and returned in a new vector.  These element indices are numbered sequentially
starting with the first vector, continuing into the second vector.  Thus, if
``vec1`` is a 4-element vector, index 5 would refer to the second element of
``vec2``. An index of -1 can be used to indicate that the corresponding element
in the returned vector is a don't care and can be optimized by the backend.
Values of -1 are not supported in constant expressions.

The result of ``__builtin_shufflevector`` is a vector with the same element
type as ``vec1``/``vec2`` but that has an element count equal to the number of
indices specified.

Query for this feature with ``__has_builtin(__builtin_shufflevector)``.

.. _langext-__builtin_convertvector:

``__builtin_convertvector``
---------------------------

``__builtin_convertvector`` is used to express generic vector
type-conversion operations. The input vector and the output vector
type must have the same number of elements. This builtin can be used within
constant expressions.

**Syntax**:

.. code-block:: c++

  __builtin_convertvector(src_vec, dst_vec_type)

**Examples**:

.. code-block:: c++

  typedef double vector4double __attribute__((__vector_size__(32)));
  typedef float  vector4float  __attribute__((__vector_size__(16)));
  typedef short  vector4short  __attribute__((__vector_size__(8)));
  vector4float vf; vector4short vs;

  // convert from a vector of 4 floats to a vector of 4 doubles.
  __builtin_convertvector(vf, vector4double)
  // equivalent to:
  (vector4double) { (double) vf[0], (double) vf[1], (double) vf[2], (double) vf[3] }

  // convert from a vector of 4 shorts to a vector of 4 floats.
  __builtin_convertvector(vs, vector4float)
  // equivalent to:
  (vector4float) { (float) vs[0], (float) vs[1], (float) vs[2], (float) vs[3] }

**Description**:

The first argument to ``__builtin_convertvector`` is a vector, and the second
argument is a vector type with the same number of elements as the first
argument.

The result of ``__builtin_convertvector`` is a vector with the same element
type as the second argument, with a value defined in terms of the action of a
C-style cast applied to each element of the first argument.

Query for this feature with ``__has_builtin(__builtin_convertvector)``.

``__builtin_bitreverse``
------------------------

* ``__builtin_bitreverse8``
* ``__builtin_bitreverse16``
* ``__builtin_bitreverse32``
* ``__builtin_bitreverse64``

**Syntax**:

.. code-block:: c++

     __builtin_bitreverse32(x)

**Examples**:

.. code-block:: c++

      uint8_t rev_x = __builtin_bitreverse8(x);
      uint16_t rev_x = __builtin_bitreverse16(x);
      uint32_t rev_y = __builtin_bitreverse32(y);
      uint64_t rev_z = __builtin_bitreverse64(z);

**Description**:

The '``__builtin_bitreverse``' family of builtins is used to reverse
the bitpattern of an integer value; for example ``0b10110110`` becomes
``0b01101101``. These builtins can be used within constant expressions.

``__builtin_rotateleft``
------------------------

* ``__builtin_rotateleft8``
* ``__builtin_rotateleft16``
* ``__builtin_rotateleft32``
* ``__builtin_rotateleft64``

**Syntax**:

.. code-block:: c++

     __builtin_rotateleft32(x, y)

**Examples**:

.. code-block:: c++

      uint8_t rot_x = __builtin_rotateleft8(x, y);
      uint16_t rot_x = __builtin_rotateleft16(x, y);
      uint32_t rot_x = __builtin_rotateleft32(x, y);
      uint64_t rot_x = __builtin_rotateleft64(x, y);

**Description**:

The '``__builtin_rotateleft``' family of builtins is used to rotate
the bits in the first argument by the amount in the second argument.
For example, ``0b10000110`` rotated left by 11 becomes ``0b00110100``.
The shift value is treated as an unsigned amount modulo the size of
the arguments. Both arguments and the result have the bitwidth specified
by the name of the builtin. These builtins can be used within constant
expressions.

``__builtin_rotateright``
-------------------------

* ``__builtin_rotateright8``
* ``__builtin_rotateright16``
* ``__builtin_rotateright32``
* ``__builtin_rotateright64``

**Syntax**:

.. code-block:: c++

     __builtin_rotateright32(x, y)

**Examples**:

.. code-block:: c++

      uint8_t rot_x = __builtin_rotateright8(x, y);
      uint16_t rot_x = __builtin_rotateright16(x, y);
      uint32_t rot_x = __builtin_rotateright32(x, y);
      uint64_t rot_x = __builtin_rotateright64(x, y);

**Description**:

The '``__builtin_rotateright``' family of builtins is used to rotate
the bits in the first argument by the amount in the second argument.
For example, ``0b10000110`` rotated right by 3 becomes ``0b11010000``.
The shift value is treated as an unsigned amount modulo the size of
the arguments. Both arguments and the result have the bitwidth specified
by the name of the builtin. These builtins can be used within constant
expressions.

``__builtin_unreachable``
-------------------------

``__builtin_unreachable`` is used to indicate that a specific point in the
program cannot be reached, even if the compiler might otherwise think it can.
This is useful to improve optimization and eliminates certain warnings.  For
example, without the ``__builtin_unreachable`` in the example below, the
compiler assumes that the inline asm can fall through and prints a "function
declared '``noreturn``' should not return" warning.

**Syntax**:

.. code-block:: c++

    __builtin_unreachable()

**Example of use**:

.. code-block:: c++

  void myabort(void) __attribute__((noreturn));
  void myabort(void) {
    asm("int3");
    __builtin_unreachable();
  }

**Description**:

The ``__builtin_unreachable()`` builtin has completely undefined behavior.
Since it has undefined behavior, it is a statement that it is never reached and
the optimizer can take advantage of this to produce better code.  This builtin
takes no arguments and produces a void result.

Query for this feature with ``__has_builtin(__builtin_unreachable)``.

``__builtin_unpredictable``
---------------------------

``__builtin_unpredictable`` is used to indicate that a branch condition is
unpredictable by hardware mechanisms such as branch prediction logic.

**Syntax**:

.. code-block:: c++

    __builtin_unpredictable(long long)

**Example of use**:

.. code-block:: c++

  if (__builtin_unpredictable(x > 0)) {
     foo();
  }

**Description**:

The ``__builtin_unpredictable()`` builtin is expected to be used with control
flow conditions such as in ``if`` and ``switch`` statements.

Query for this feature with ``__has_builtin(__builtin_unpredictable)``.


``__builtin_expect``
--------------------

``__builtin_expect`` is used to indicate that the value of an expression is
anticipated to be the same as a statically known result.

**Syntax**:

.. code-block:: c++

    long __builtin_expect(long expr, long val)

**Example of use**:

.. code-block:: c++

  if (__builtin_expect(x, 0)) {
     bar();
  }

**Description**:

The ``__builtin_expect()`` builtin is typically used with control flow
conditions such as in ``if`` and ``switch`` statements to help branch
prediction. It means that its first argument ``expr`` is expected to take the
value of its second argument ``val``. It always returns ``expr``.

Query for this feature with ``__has_builtin(__builtin_expect)``.

``__builtin_expect_with_probability``
-------------------------------------

``__builtin_expect_with_probability`` is similar to ``__builtin_expect`` but it
takes a probability as third argument.

**Syntax**:

.. code-block:: c++

    long __builtin_expect_with_probability(long expr, long val, double p)

**Example of use**:

.. code-block:: c++

  if (__builtin_expect_with_probability(x, 0, .3)) {
     bar();
  }

**Description**:

The ``__builtin_expect_with_probability()`` builtin is typically used with
control flow conditions such as in ``if`` and ``switch`` statements to help
branch prediction. It means that its first argument ``expr`` is expected to take
the value of its second argument ``val`` with probability ``p``. ``p`` must be
within ``[0.0 ; 1.0]`` bounds. This builtin always returns the value of ``expr``.

Query for this feature with ``__has_builtin(__builtin_expect_with_probability)``.

``__builtin_prefetch``
----------------------

``__builtin_prefetch`` is used to communicate with the cache handler to bring
data into the cache before it gets used.

**Syntax**:

.. code-block:: c++

    void __builtin_prefetch(const void *addr, int rw=0, int locality=3)

**Example of use**:

.. code-block:: c++

    __builtin_prefetch(a + i);

**Description**:

The ``__builtin_prefetch(addr, rw, locality)`` builtin is expected to be used to
avoid cache misses when the developer has a good understanding of which data
are going to be used next. ``addr`` is the address that needs to be brought into
the cache. ``rw`` indicates the expected access mode: ``0`` for *read* and ``1``
for *write*. In case of *read write* access, ``1`` is to be used. ``locality``
indicates the expected persistence of data in cache, from ``0`` which means that
data can be discarded from cache after its next use to ``3`` which means that
data is going to be reused a lot once in cache. ``1`` and ``2`` provide
intermediate behavior between these two extremes.

Query for this feature with ``__has_builtin(__builtin_prefetch)``.

``__sync_swap``
---------------

``__sync_swap`` is used to atomically swap integers or pointers in memory.

**Syntax**:

.. code-block:: c++

  type __sync_swap(type *ptr, type value, ...)

**Example of Use**:

.. code-block:: c++

  int old_value = __sync_swap(&value, new_value);

**Description**:

The ``__sync_swap()`` builtin extends the existing ``__sync_*()`` family of
atomic intrinsics to allow code to atomically swap the current value with the
new value.  More importantly, it helps developers write more efficient and
correct code by avoiding expensive loops around
``__sync_bool_compare_and_swap()`` or relying on the platform specific
implementation details of ``__sync_lock_test_and_set()``.  The
``__sync_swap()`` builtin is a full barrier.

``__builtin_addressof``
-----------------------

``__builtin_addressof`` performs the functionality of the built-in ``&``
operator, ignoring any ``operator&`` overload.  This is useful in constant
expressions in C++11, where there is no other way to take the address of an
object that overloads ``operator&``. Clang automatically adds
``[[clang::lifetimebound]]`` to the parameter of ``__builtin_addressof``.

**Example of use**:

.. code-block:: c++

  template<typename T> constexpr T *addressof(T &value) {
    return __builtin_addressof(value);
  }

``__builtin_function_start``
-----------------------------

``__builtin_function_start`` returns the address of a function body.

**Syntax**:

.. code-block:: c++

  void *__builtin_function_start(function)

**Example of use**:

.. code-block:: c++

  void a() {}
  void *p = __builtin_function_start(a);

  class A {
  public:
    void a(int n);
    void a();
  };

  void A::a(int n) {}
  void A::a() {}

  void *pa1 = __builtin_function_start((void(A::*)(int)) &A::a);
  void *pa2 = __builtin_function_start((void(A::*)()) &A::a);

**Description**:

The ``__builtin_function_start`` builtin accepts an argument that can be
constant-evaluated to a function, and returns the address of the function
body.  This builtin is not supported on all targets.

The returned pointer may differ from the normally taken function address
and is not safe to call.  For example, with ``-fsanitize=cfi``, taking a
function address produces a callable pointer to a CFI jump table, while
``__builtin_function_start`` returns an address that fails
:doc:`cfi-icall<ControlFlowIntegrity>` checks.

``__builtin_operator_new`` and ``__builtin_operator_delete``
------------------------------------------------------------

A call to ``__builtin_operator_new(args)`` is exactly the same as a call to
``::operator new(args)``, except that it allows certain optimizations
that the C++ standard does not permit for a direct function call to
``::operator new`` (in particular, removing ``new`` / ``delete`` pairs and
merging allocations), and that the call is required to resolve to a
`replaceable global allocation function
<https://en.cppreference.com/w/cpp/memory/new/operator_new>`_.

Likewise, ``__builtin_operator_delete`` is exactly the same as a call to
``::operator delete(args)``, except that it permits optimizations
and that the call is required to resolve to a
`replaceable global deallocation function
<https://en.cppreference.com/w/cpp/memory/new/operator_delete>`_.

These builtins are intended for use in the implementation of ``std::allocator``
and other similar allocation libraries, and are only available in C++.

Query for this feature with ``__has_builtin(__builtin_operator_new)`` or
``__has_builtin(__builtin_operator_delete)``:

  * If the value is at least ``201802L``, the builtins behave as described above.

  * If the value is non-zero, the builtins may not support calling arbitrary
    replaceable global (de)allocation functions, but do support calling at least
    ``::operator new(size_t)`` and ``::operator delete(void*)``.

``__builtin_preserve_access_index``
-----------------------------------

``__builtin_preserve_access_index`` specifies a code section where
array subscript access and structure/union member access are relocatable
under bpf compile-once run-everywhere framework. Debuginfo (typically
with ``-g``) is needed, otherwise, the compiler will exit with an error.
The return type for the intrinsic is the same as the type of the
argument.

**Syntax**:

.. code-block:: c

  type __builtin_preserve_access_index(type arg)

**Example of Use**:

.. code-block:: c

  struct t {
    int i;
    int j;
    union {
      int a;
      int b;
    } c[4];
  };
  struct t *v = ...;
  int *pb =__builtin_preserve_access_index(&v->c[3].b);
  __builtin_preserve_access_index(v->j);

``__builtin_debugtrap``
-----------------------

``__builtin_debugtrap`` causes the program to stop its execution in such a way that a debugger can catch it.

**Syntax**:

.. code-block:: c++

    __builtin_debugtrap()

**Description**

``__builtin_debugtrap`` is lowered to the ` ``llvm.debugtrap`` <https://llvm.org/docs/LangRef.html#llvm-debugtrap-intrinsic>`_ builtin. It should have the same effect as setting a breakpoint on the line where the builtin is called.

Query for this feature with ``__has_builtin(__builtin_debugtrap)``.


``__builtin_trap``
------------------

``__builtin_trap`` causes the program to stop its execution abnormally.

**Syntax**:

.. code-block:: c++

    __builtin_trap()

**Description**

``__builtin_trap`` is lowered to the ` ``llvm.trap`` <https://llvm.org/docs/LangRef.html#llvm-trap-intrinsic>`_ builtin.

Query for this feature with ``__has_builtin(__builtin_trap)``.

``__builtin_arm_trap``
----------------------

``__builtin_arm_trap`` is an AArch64 extension to ``__builtin_trap`` which also accepts a compile-time constant value, encoded directly into the trap instruction for later inspection.

**Syntax**:

.. code-block:: c++

    __builtin_arm_trap(const unsigned short payload)

**Description**

``__builtin_arm_trap`` is lowered to the ``llvm.aarch64.break`` builtin, and then to ``brk #payload``.

``__builtin_verbose_trap``
--------------------------

``__builtin_verbose_trap`` causes the program to stop its execution abnormally
and shows a human-readable description of the reason for the termination when a
debugger is attached or in a symbolicated crash log.

**Syntax**:

.. code-block:: c++

    __builtin_verbose_trap(const char *category, const char *reason)

**Description**

``__builtin_verbose_trap`` is lowered to the ` ``llvm.trap`` <https://llvm.org/docs/LangRef.html#llvm-trap-intrinsic>`_ builtin.
Additionally, clang emits debugging information that represents an artificial
inline frame whose name encodes the category and reason strings passed to the builtin,
prefixed by a "magic" prefix.

For example, consider the following code:

.. code-block:: c++

    void foo(int* p) {
      if (p == nullptr)
        __builtin_verbose_trap("check null", "Argument must not be null!");
    }

The debugging information would look as if it were produced for the following code:

.. code-block:: c++

    __attribute__((always_inline))
    inline void "__clang_trap_msg$check null$Argument must not be null!"() {
      __builtin_trap();
    }

    void foo(int* p) {
      if (p == nullptr)
        "__clang_trap_msg$check null$Argument must not be null!"();
    }

However, the generated code would not actually contain a call to the artificial
function — it only exists in the debugging information.

Query for this feature with ``__has_builtin(__builtin_verbose_trap)``. Note that
users need to enable debug information to enable this feature. A call to this
builtin is equivalent to a call to ``__builtin_trap`` if debug information isn't
enabled.

The optimizer can merge calls to trap with different messages, which degrades
the debugging experience.

``__builtin_allow_runtime_check``
---------------------------------

``__builtin_allow_runtime_check`` return true if the check at the current
program location should be executed. It is expected to be used to implement
``assert`` like checks which can be safely removed by optimizer.

**Syntax**:

.. code-block:: c++

    bool __builtin_allow_runtime_check(const char* kind)

**Example of use**:

.. code-block:: c++

  if (__builtin_allow_runtime_check("mycheck") && !ExpensiveCheck()) {
     abort();
  }

**Description**

``__builtin_allow_runtime_check`` is lowered to ` ``llvm.allow.runtime.check``
<https://llvm.org/docs/LangRef.html#llvm-allow-runtime-check-intrinsic>`_
builtin.

The ``__builtin_allow_runtime_check()`` is expected to be used with control
flow conditions such as in ``if`` to guard expensive runtime checks. The
specific rules for selecting permitted checks can differ and are controlled by
the compiler options.

Flags to control checks:
* ``-mllvm -lower-allow-check-percentile-cutoff-hot=N`` where N is PGO hotness
cutoff in range ``[0, 999999]`` to disallow checks in hot code.
* ``-mllvm -lower-allow-check-random-rate=P`` where P is number in range
``[0.0, 1.0]`` representation probability of keeping a check.
* If both flags are specified, ``-lower-allow-check-random-rate`` takes
precedence.
* If none is specified, ``__builtin_allow_runtime_check`` is lowered as
``true``, allowing all checks.

Parameter ``kind`` is a string literal representing a user selected kind for
guarded check. It's unused now. It will enable kind-specific lowering in future.
E.g. a higher hotness cutoff can be used for more expensive kind of check.

Query for this feature with ``__has_builtin(__builtin_allow_runtime_check)``.

``__builtin_nondeterministic_value``
------------------------------------

``__builtin_nondeterministic_value`` returns a valid nondeterministic value of the same type as the provided argument.

**Syntax**:

.. code-block:: c++

    type __builtin_nondeterministic_value(type x)

**Examples**:

.. code-block:: c++

    int x = __builtin_nondeterministic_value(x);
    float y = __builtin_nondeterministic_value(y);
    __m256i a = __builtin_nondeterministic_value(a);

**Description**

Each call to ``__builtin_nondeterministic_value`` returns a valid value of the type given by the argument.

The types currently supported are: integer types, floating-point types, vector types.

Query for this feature with ``__has_builtin(__builtin_nondeterministic_value)``.

``__builtin_sycl_unique_stable_name``
-------------------------------------

``__builtin_sycl_unique_stable_name()`` is a builtin that takes a type and
produces a string literal containing a unique name for the type that is stable
across split compilations, mainly to support SYCL/Data Parallel C++ language.

In cases where the split compilation needs to share a unique token for a type
across the boundary (such as in an offloading situation), this name can be used
for lookup purposes, such as in the SYCL Integration Header.

The value of this builtin is computed entirely at compile time, so it can be
used in constant expressions. This value encodes lambda functions based on a
stable numbering order in which they appear in their local declaration contexts.
Once this builtin is evaluated in a constexpr context, it is erroneous to use
it in an instantiation which changes its value.

In order to produce the unique name, the current implementation of the builtin
uses Itanium mangling even if the host compilation uses a different name
mangling scheme at runtime. The mangler marks all the lambdas required to name
the SYCL kernel and emits a stable local ordering of the respective lambdas.
The resulting pattern is demanglable.  When non-lambda types are passed to the
builtin, the mangler emits their usual pattern without any special treatment.

**Syntax**:

.. code-block:: c

  // Computes a unique stable name for the given type.
  constexpr const char * __builtin_sycl_unique_stable_name( type-id );

``__builtin_popcountg``
-----------------------

``__builtin_popcountg`` returns the number of 1 bits in the argument. The
argument can be of any unsigned integer type.

**Syntax**:

.. code-block:: c++

  int __builtin_popcountg(type x)

**Examples**:

.. code-block:: c++

  unsigned int x = 1;
  int x_pop = __builtin_popcountg(x);

  unsigned long y = 3;
  int y_pop = __builtin_popcountg(y);

  unsigned _BitInt(128) z = 7;
  int z_pop = __builtin_popcountg(z);

**Description**:

``__builtin_popcountg`` is meant to be a type-generic alternative to the
``__builtin_popcount{,l,ll}`` builtins, with support for other integer types,
such as ``unsigned __int128`` and C23 ``unsigned _BitInt(N)``.

``__builtin_clzg`` and ``__builtin_ctzg``
-----------------------------------------

``__builtin_clzg`` (respectively ``__builtin_ctzg``) returns the number of
leading (respectively trailing) 0 bits in the first argument. The first argument
can be of any unsigned integer type.

If the first argument is 0 and an optional second argument of ``int`` type is
provided, then the second argument is returned. If the first argument is 0, but
only one argument is provided, then the behavior is undefined.

**Syntax**:

.. code-block:: c++

  int __builtin_clzg(type x[, int fallback])
  int __builtin_ctzg(type x[, int fallback])

**Examples**:

.. code-block:: c++

  unsigned int x = 1;
  int x_lz = __builtin_clzg(x);
  int x_tz = __builtin_ctzg(x);

  unsigned long y = 2;
  int y_lz = __builtin_clzg(y);
  int y_tz = __builtin_ctzg(y);

  unsigned _BitInt(128) z = 4;
  int z_lz = __builtin_clzg(z);
  int z_tz = __builtin_ctzg(z);

**Description**:

``__builtin_clzg`` (respectively ``__builtin_ctzg``) is meant to be a
type-generic alternative to the ``__builtin_clz{,l,ll}`` (respectively
``__builtin_ctz{,l,ll}``) builtins, with support for other integer types, such
as ``unsigned __int128`` and C23 ``unsigned _BitInt(N)``.

Multiprecision Arithmetic Builtins
----------------------------------

Clang provides a set of builtins which expose multiprecision arithmetic in a
manner amenable to C. They all have the following form:

.. code-block:: c

  unsigned x = ..., y = ..., carryin = ..., carryout;
  unsigned sum = __builtin_addc(x, y, carryin, &carryout);

Thus one can form a multiprecision addition chain in the following manner:

.. code-block:: c

  unsigned *x, *y, *z, carryin=0, carryout;
  z[0] = __builtin_addc(x[0], y[0], carryin, &carryout);
  carryin = carryout;
  z[1] = __builtin_addc(x[1], y[1], carryin, &carryout);
  carryin = carryout;
  z[2] = __builtin_addc(x[2], y[2], carryin, &carryout);
  carryin = carryout;
  z[3] = __builtin_addc(x[3], y[3], carryin, &carryout);

The complete list of builtins are:

.. code-block:: c

  unsigned char      __builtin_addcb (unsigned char x, unsigned char y, unsigned char carryin, unsigned char *carryout);
  unsigned short     __builtin_addcs (unsigned short x, unsigned short y, unsigned short carryin, unsigned short *carryout);
  unsigned           __builtin_addc  (unsigned x, unsigned y, unsigned carryin, unsigned *carryout);
  unsigned long      __builtin_addcl (unsigned long x, unsigned long y, unsigned long carryin, unsigned long *carryout);
  unsigned long long __builtin_addcll(unsigned long long x, unsigned long long y, unsigned long long carryin, unsigned long long *carryout);
  unsigned char      __builtin_subcb (unsigned char x, unsigned char y, unsigned char carryin, unsigned char *carryout);
  unsigned short     __builtin_subcs (unsigned short x, unsigned short y, unsigned short carryin, unsigned short *carryout);
  unsigned           __builtin_subc  (unsigned x, unsigned y, unsigned carryin, unsigned *carryout);
  unsigned long      __builtin_subcl (unsigned long x, unsigned long y, unsigned long carryin, unsigned long *carryout);
  unsigned long long __builtin_subcll(unsigned long long x, unsigned long long y, unsigned long long carryin, unsigned long long *carryout);

Checked Arithmetic Builtins
---------------------------

Clang provides a set of builtins that implement checked arithmetic for security
critical applications in a manner that is fast and easily expressible in C. As
an example of their usage:

.. code-block:: c

  errorcode_t security_critical_application(...) {
    unsigned x, y, result;
    ...
    if (__builtin_mul_overflow(x, y, &result))
      return kErrorCodeHackers;
    ...
    use_multiply(result);
    ...
  }

Clang provides the following checked arithmetic builtins:

.. code-block:: c

  bool __builtin_add_overflow   (type1 x, type2 y, type3 *sum);
  bool __builtin_sub_overflow   (type1 x, type2 y, type3 *diff);
  bool __builtin_mul_overflow   (type1 x, type2 y, type3 *prod);
  bool __builtin_uadd_overflow  (unsigned x, unsigned y, unsigned *sum);
  bool __builtin_uaddl_overflow (unsigned long x, unsigned long y, unsigned long *sum);
  bool __builtin_uaddll_overflow(unsigned long long x, unsigned long long y, unsigned long long *sum);
  bool __builtin_usub_overflow  (unsigned x, unsigned y, unsigned *diff);
  bool __builtin_usubl_overflow (unsigned long x, unsigned long y, unsigned long *diff);
  bool __builtin_usubll_overflow(unsigned long long x, unsigned long long y, unsigned long long *diff);
  bool __builtin_umul_overflow  (unsigned x, unsigned y, unsigned *prod);
  bool __builtin_umull_overflow (unsigned long x, unsigned long y, unsigned long *prod);
  bool __builtin_umulll_overflow(unsigned long long x, unsigned long long y, unsigned long long *prod);
  bool __builtin_sadd_overflow  (int x, int y, int *sum);
  bool __builtin_saddl_overflow (long x, long y, long *sum);
  bool __builtin_saddll_overflow(long long x, long long y, long long *sum);
  bool __builtin_ssub_overflow  (int x, int y, int *diff);
  bool __builtin_ssubl_overflow (long x, long y, long *diff);
  bool __builtin_ssubll_overflow(long long x, long long y, long long *diff);
  bool __builtin_smul_overflow  (int x, int y, int *prod);
  bool __builtin_smull_overflow (long x, long y, long *prod);
  bool __builtin_smulll_overflow(long long x, long long y, long long *prod);

Each builtin performs the specified mathematical operation on the
first two arguments and stores the result in the third argument.  If
possible, the result will be equal to mathematically-correct result
and the builtin will return 0.  Otherwise, the builtin will return
1 and the result will be equal to the unique value that is equivalent
to the mathematically-correct result modulo two raised to the *k*
power, where *k* is the number of bits in the result type.  The
behavior of these builtins is well-defined for all argument values.

The first three builtins work generically for operands of any integer type,
including boolean types.  The operands need not have the same type as each
other, or as the result.  The other builtins may implicitly promote or
convert their operands before performing the operation.

Query for this feature with ``__has_builtin(__builtin_add_overflow)``, etc.

Floating point builtins
---------------------------------------

``__builtin_isfpclass``
-----------------------

``__builtin_isfpclass`` is used to test if the specified floating-point values
fall into one of the specified floating-point classes.

**Syntax**:

.. code-block:: c++

    int __builtin_isfpclass(fp_type expr, int mask)
    int_vector __builtin_isfpclass(fp_vector expr, int mask)

**Example of use**:

.. code-block:: c++

  if (__builtin_isfpclass(x, 448)) {
     // `x` is positive finite value
	 ...
  }

**Description**:

The ``__builtin_isfpclass()`` builtin is a generalization of functions ``isnan``,
``isinf``, ``isfinite`` and some others defined by the C standard. It tests if
the floating-point value, specified by the first argument, falls into any of data
classes, specified by the second argument. The latter is an integer constant
bitmask expression, in which each data class is represented by a bit
using the encoding:

========== =================== ======================
Mask value Data class          Macro
========== =================== ======================
0x0001     Signaling NaN       __FPCLASS_SNAN
0x0002     Quiet NaN           __FPCLASS_QNAN
0x0004     Negative infinity   __FPCLASS_NEGINF
0x0008     Negative normal     __FPCLASS_NEGNORMAL
0x0010     Negative subnormal  __FPCLASS_NEGSUBNORMAL
0x0020     Negative zero       __FPCLASS_NEGZERO
0x0040     Positive zero       __FPCLASS_POSZERO
0x0080     Positive subnormal  __FPCLASS_POSSUBNORMAL
0x0100     Positive normal     __FPCLASS_POSNORMAL
0x0200     Positive infinity   __FPCLASS_POSINF
========== =================== ======================

For convenience preprocessor defines macros for these values. The function
returns 1 if ``expr`` falls into one of the specified data classes, 0 otherwise.

In the example above the mask value 448 (0x1C0) contains the bits selecting
positive zero, positive subnormal and positive normal classes.
``__builtin_isfpclass(x, 448)`` would return true only if ``x`` if of any of
these data classes. Using suitable mask value, the function can implement any of
the standard classification functions, for example, ``__builtin_isfpclass(x, 3)``
is identical to ``isnan``,``__builtin_isfpclass(x, 504)`` - to ``isfinite``
and so on.

If the first argument is a vector, the function is equivalent to the set of
scalar calls of ``__builtin_isfpclass`` applied to the input elementwise.

The result of ``__builtin_isfpclass`` is a boolean value, if the first argument
is a scalar, or an integer vector with the same element count as the first
argument. The element type in this vector has the same bit length as the
element of the first argument type.

This function never raises floating-point exceptions and does not canonicalize
its input. The floating-point argument is not promoted, its data class is
determined based on its representation in its actual semantic type.

``__builtin_canonicalize``
--------------------------

.. code-block:: c

   double __builtin_canonicalize(double);
   float __builtin_canonicalizef(float);
   long double __builtin_canonicalizel(long double);

Returns the platform specific canonical encoding of a floating point
number. This canonicalization is useful for implementing certain
numeric primitives such as frexp. See `LLVM canonicalize intrinsic
<https://llvm.org/docs/LangRef.html#llvm-canonicalize-intrinsic>`_ for
more information on the semantics.

``__builtin_flt_rounds`` and ``__builtin_set_flt_rounds``
---------------------------------------------------------

.. code-block:: c

   int __builtin_flt_rounds();
   void __builtin_set_flt_rounds(int);

Returns and sets current floating point rounding mode. The encoding of returned
values and input parameters is same as the result of FLT_ROUNDS, specified by C
standard:
- ``0``  - toward zero
- ``1``  - to nearest, ties to even
- ``2``  - toward positive infinity
- ``3``  - toward negative infinity
- ``4``  - to nearest, ties away from zero
The effect of passing some other value to ``__builtin_flt_rounds`` is
implementation-defined. ``__builtin_set_flt_rounds`` is currently only supported
to work on x86, x86_64, Arm and AArch64 targets. These builtins read and modify
the floating-point environment, which is not always allowed and may have unexpected
behavior. Please see the section on `Accessing the floating point environment <https://clang.llvm.org/docs/UsersManual.html#accessing-the-floating-point-environment>`_ for more information.

String builtins
---------------

Clang provides constant expression evaluation support for builtins forms of
the following functions from the C standard library headers
``<string.h>`` and ``<wchar.h>``:

* ``memchr``
* ``memcmp`` (and its deprecated BSD / POSIX alias ``bcmp``)
* ``strchr``
* ``strcmp``
* ``strlen``
* ``strncmp``
* ``wcschr``
* ``wcscmp``
* ``wcslen``
* ``wcsncmp``
* ``wmemchr``
* ``wmemcmp``

In each case, the builtin form has the name of the C library function prefixed
by ``__builtin_``. Example:

.. code-block:: c

  void *p = __builtin_memchr("foobar", 'b', 5);

In addition to the above, one further builtin is provided:

.. code-block:: c

  char *__builtin_char_memchr(const char *haystack, int needle, size_t size);

``__builtin_char_memchr(a, b, c)`` is identical to
``(char*)__builtin_memchr(a, b, c)`` except that its use is permitted within
constant expressions in C++11 onwards (where a cast from ``void*`` to ``char*``
is disallowed in general).

Constant evaluation support for the ``__builtin_mem*`` functions is provided
only for arrays of ``char``, ``signed char``, ``unsigned char``, or ``char8_t``,
despite these functions accepting an argument of type ``const void*``.

Support for constant expression evaluation for the above builtins can be detected
with ``__has_feature(cxx_constexpr_string_builtins)``.

Variadic function builtins
--------------------------

Clang provides several builtins for working with variadic functions from the C
standard library ``<stdarg.h>`` header:

* ``__builtin_va_list``

A predefined typedef for the target-specific ``va_list`` type. It is undefined
behavior to use a byte-wise copy of this type produced by calling ``memcpy``,
``memmove``, or similar. Valid explicit copies are only produced by calling
``va_copy`` or ``__builtin_va_copy``.

* ``void __builtin_va_start(__builtin_va_list list, <parameter-name>)``

A builtin function for the target-specific ``va_start`` function-like macro.
The ``parameter-name`` argument is the name of the parameter preceding the
ellipsis (``...``) in the function signature. Alternatively, in C23 mode or
later, it may be the integer literal ``0`` if there is no parameter preceding
the ellipsis. This function initializes the given ``__builtin_va_list`` object.
It is undefined behavior to call this function on an already initialized
``__builin_va_list`` object.

* ``void __builtin_va_end(__builtin_va_list list)``

A builtin function for the target-specific ``va_end`` function-like macro. This
function finalizes the given ``__builtin_va_list`` object such that it is no
longer usable unless re-initialized with a call to ``__builtin_va_start`` or
``__builtin_va_copy``. It is undefined behavior to call this function with a
``list`` that has not been initialized by either ``__builtin_va_start`` or
``__builtin_va_copy``.

* ``<type-name> __builtin_va_arg(__builtin_va_list list, <type-name>)``

A builtin function for the target-specific ``va_arg`` function-like macro. This
function returns the value of the next variadic argument to the call. It is
undefined behavior to call this builtin when there is no next variadic argument
to retrieve or if the next variadic argument does not have a type compatible
with the given ``type-name``. The return type of the function is the
``type-name`` given as the second argument. It is undefined behavior to call
this function with a ``list`` that has not been initialized by either
``__builtin_va_start`` or ``__builtin_va_copy``.

* ``void __builtin_va_copy(__builtin_va_list dest, __builtin_va_list src)``

A builtin function for the target-specific ``va_copy`` function-like macro.
This function initializes ``dest`` as a copy of ``src``. It is undefined
behavior to call this function with an already initialized ``dest`` argument.

Memory builtins
---------------

Clang provides constant expression evaluation support for builtin forms of the
following functions from the C standard library headers
``<string.h>`` and ``<wchar.h>``:

* ``memcpy``
* ``memmove``
* ``wmemcpy``
* ``wmemmove``

In each case, the builtin form has the name of the C library function prefixed
by ``__builtin_``.

Constant evaluation support is only provided when the source and destination
are pointers to arrays with the same trivially copyable element type, and the
given size is an exact multiple of the element size that is no greater than
the number of elements accessible through the source and destination operands.

Guaranteed inlined copy
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  void __builtin_memcpy_inline(void *dst, const void *src, size_t size);


``__builtin_memcpy_inline`` has been designed as a building block for efficient
``memcpy`` implementations. It is identical to ``__builtin_memcpy`` but also
guarantees not to call any external functions. See LLVM IR `llvm.memcpy.inline
<https://llvm.org/docs/LangRef.html#llvm-memcpy-inline-intrinsic>`_ intrinsic
for more information.

This is useful to implement a custom version of ``memcpy``, implement a
``libc`` memcpy or work around the absence of a ``libc``.

Note that the `size` argument must be a compile time constant.

Note that this intrinsic cannot yet be called in a ``constexpr`` context.

Guaranteed inlined memset
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  void __builtin_memset_inline(void *dst, int value, size_t size);


``__builtin_memset_inline`` has been designed as a building block for efficient
``memset`` implementations. It is identical to ``__builtin_memset`` but also
guarantees not to call any external functions. See LLVM IR `llvm.memset.inline
<https://llvm.org/docs/LangRef.html#llvm-memset-inline-intrinsic>`_ intrinsic
for more information.

This is useful to implement a custom version of ``memset``, implement a
``libc`` memset or work around the absence of a ``libc``.

Note that the `size` argument must be a compile time constant.

Note that this intrinsic cannot yet be called in a ``constexpr`` context.

``__is_bitwise_cloneable``
--------------------------

A type trait is used to check whether a type can be safely copied by memcpy.

**Syntax**:

.. code-block:: c++

  bool __is_bitwise_cloneable(Type)

**Description**:

Objects of bitwise cloneable types can be bitwise copied by memcpy/memmove. The
Clang compiler warrants that this behavior is well defined, and won't be
broken by compiler optimizations and sanitizers.

For implicit-lifetime types, the lifetime of the new object is implicitly
started after the copy. For other types (e.g., classes with virtual methods),
the lifetime isn't started, and using the object results in undefined behavior
according to the C++ Standard.

This builtin can be used in constant expressions.

Atomic Min/Max builtins with memory ordering
--------------------------------------------

There are two atomic builtins with min/max in-memory comparison and swap.
The syntax and semantics are similar to GCC-compatible __atomic_* builtins.

* ``__atomic_fetch_min``
* ``__atomic_fetch_max``

The builtins work with signed and unsigned integers and require to specify memory ordering.
The return value is the original value that was stored in memory before comparison.

Example:

.. code-block:: c

  unsigned int val = __atomic_fetch_min(unsigned int *pi, unsigned int ui, __ATOMIC_RELAXED);

The third argument is one of the memory ordering specifiers ``__ATOMIC_RELAXED``,
``__ATOMIC_CONSUME``, ``__ATOMIC_ACQUIRE``, ``__ATOMIC_RELEASE``,
``__ATOMIC_ACQ_REL``, or ``__ATOMIC_SEQ_CST`` following C++11 memory model semantics.

In terms of acquire-release ordering barriers these two operations are always
considered as operations with *load-store* semantics, even when the original value
is not actually modified after comparison.

.. _langext-__c11_atomic:

__c11_atomic builtins
---------------------

Clang provides a set of builtins which are intended to be used to implement
C11's ``<stdatomic.h>`` header.  These builtins provide the semantics of the
``_explicit`` form of the corresponding C11 operation, and are named with a
``__c11_`` prefix.  The supported operations, and the differences from
the corresponding C11 operations, are:

* ``__c11_atomic_init``
* ``__c11_atomic_thread_fence``
* ``__c11_atomic_signal_fence``
* ``__c11_atomic_is_lock_free`` (The argument is the size of the
  ``_Atomic(...)`` object, instead of its address)
* ``__c11_atomic_store``
* ``__c11_atomic_load``
* ``__c11_atomic_exchange``
* ``__c11_atomic_compare_exchange_strong``
* ``__c11_atomic_compare_exchange_weak``
* ``__c11_atomic_fetch_add``
* ``__c11_atomic_fetch_sub``
* ``__c11_atomic_fetch_and``
* ``__c11_atomic_fetch_or``
* ``__c11_atomic_fetch_xor``
* ``__c11_atomic_fetch_nand`` (Nand is not presented in ``<stdatomic.h>``)
* ``__c11_atomic_fetch_max``
* ``__c11_atomic_fetch_min``

The macros ``__ATOMIC_RELAXED``, ``__ATOMIC_CONSUME``, ``__ATOMIC_ACQUIRE``,
``__ATOMIC_RELEASE``, ``__ATOMIC_ACQ_REL``, and ``__ATOMIC_SEQ_CST`` are
provided, with values corresponding to the enumerators of C11's
``memory_order`` enumeration.

(Note that Clang additionally provides GCC-compatible ``__atomic_*``
builtins and OpenCL 2.0 ``__opencl_atomic_*`` builtins. The OpenCL 2.0
atomic builtins are an explicit form of the corresponding OpenCL 2.0
builtin function, and are named with a ``__opencl_`` prefix. The macros
``__OPENCL_MEMORY_SCOPE_WORK_ITEM``, ``__OPENCL_MEMORY_SCOPE_WORK_GROUP``,
``__OPENCL_MEMORY_SCOPE_DEVICE``, ``__OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES``,
and ``__OPENCL_MEMORY_SCOPE_SUB_GROUP`` are provided, with values
corresponding to the enumerators of OpenCL's ``memory_scope`` enumeration.)

__scoped_atomic builtins
------------------------

Clang provides a set of atomics taking a memory scope argument. These atomics
are identical to the standard GNU / GCC atomic builtins but taking an extra
memory scope argument. These are designed to be a generic alternative to the
``__opencl_atomic_*`` builtin functions for targets that support atomic memory
scopes.

Atomic memory scopes are designed to assist optimizations for systems with
several levels of memory hierarchy like GPUs. The following memory scopes are
currently supported:

* ``__MEMORY_SCOPE_SYSTEM``
* ``__MEMORY_SCOPE_DEVICE``
* ``__MEMORY_SCOPE_WRKGRP``
* ``__MEMORY_SCOPE_WVFRNT``
* ``__MEMORY_SCOPE_SINGLE``

This controls whether or not the atomic operation is ordered with respect to the
whole system, the current device, an OpenCL workgroup, wavefront, or just a
single thread. If these are used on a target that does not support atomic
scopes, then they will behave exactly as the standard GNU atomic builtins.

Low-level ARM exclusive memory builtins
---------------------------------------

Clang provides overloaded builtins giving direct access to the three key ARM
instructions for implementing atomic operations.

.. code-block:: c

  T __builtin_arm_ldrex(const volatile T *addr);
  T __builtin_arm_ldaex(const volatile T *addr);
  int __builtin_arm_strex(T val, volatile T *addr);
  int __builtin_arm_stlex(T val, volatile T *addr);
  void __builtin_arm_clrex(void);

The types ``T`` currently supported are:

* Integer types with width at most 64 bits (or 128 bits on AArch64).
* Floating-point types
* Pointer types.

Note that the compiler does not guarantee it will not insert stores which clear
the exclusive monitor in between an ``ldrex`` type operation and its paired
``strex``. In practice this is only usually a risk when the extra store is on
the same cache line as the variable being modified and Clang will only insert
stack stores on its own, so it is best not to use these operations on variables
with automatic storage duration.

Also, loads and stores may be implicit in code written between the ``ldrex`` and
``strex``. Clang will not necessarily mitigate the effects of these either, so
care should be exercised.

For these reasons the higher level atomic primitives should be preferred where
possible.

Non-temporal load/store builtins
--------------------------------

Clang provides overloaded builtins allowing generation of non-temporal memory
accesses.

.. code-block:: c

  T __builtin_nontemporal_load(T *addr);
  void __builtin_nontemporal_store(T value, T *addr);

The types ``T`` currently supported are:

* Integer types.
* Floating-point types.
* Vector types.

Note that the compiler does not guarantee that non-temporal loads or stores
will be used.

C++ Coroutines support builtins
--------------------------------

.. warning::
  This is a work in progress. Compatibility across Clang/LLVM releases is not
  guaranteed.

Clang provides experimental builtins to support C++ Coroutines as defined by
https://wg21.link/P0057. The following four are intended to be used by the
standard library to implement the ``std::coroutine_handle`` type.

**Syntax**:

.. code-block:: c

  void  __builtin_coro_resume(void *addr);
  void  __builtin_coro_destroy(void *addr);
  bool  __builtin_coro_done(void *addr);
  void *__builtin_coro_promise(void *addr, int alignment, bool from_promise)

**Example of use**:

.. code-block:: c++

  template <> struct coroutine_handle<void> {
    void resume() const { __builtin_coro_resume(ptr); }
    void destroy() const { __builtin_coro_destroy(ptr); }
    bool done() const { return __builtin_coro_done(ptr); }
    // ...
  protected:
    void *ptr;
  };

  template <typename Promise> struct coroutine_handle : coroutine_handle<> {
    // ...
    Promise &promise() const {
      return *reinterpret_cast<Promise *>(
        __builtin_coro_promise(ptr, alignof(Promise), /*from-promise=*/false));
    }
    static coroutine_handle from_promise(Promise &promise) {
      coroutine_handle p;
      p.ptr = __builtin_coro_promise(&promise, alignof(Promise),
                                                      /*from-promise=*/true);
      return p;
    }
  };


Other coroutine builtins are either for internal clang use or for use during
development of the coroutine feature. See `Coroutines in LLVM
<https://llvm.org/docs/Coroutines.html#intrinsics>`_ for
more information on their semantics. Note that builtins matching the intrinsics
that take token as the first parameter (llvm.coro.begin, llvm.coro.alloc,
llvm.coro.free and llvm.coro.suspend) omit the token parameter and fill it to
an appropriate value during the emission.

**Syntax**:

.. code-block:: c

  size_t __builtin_coro_size()
  void  *__builtin_coro_frame()
  void  *__builtin_coro_free(void *coro_frame)

  void  *__builtin_coro_id(int align, void *promise, void *fnaddr, void *parts)
  bool   __builtin_coro_alloc()
  void  *__builtin_coro_begin(void *memory)
  void   __builtin_coro_end(void *coro_frame, bool unwind)
  char   __builtin_coro_suspend(bool final)

Note that there is no builtin matching the `llvm.coro.save` intrinsic. LLVM
automatically will insert one if the first argument to `llvm.coro.suspend` is
token `none`. If a user calls `__builin_suspend`, clang will insert `token none`
as the first argument to the intrinsic.

Source location builtins
------------------------

Clang provides builtins to support C++ standard library implementation
of ``std::source_location`` as specified in C++20.  With the exception
of ``__builtin_COLUMN``, ``__builtin_FILE_NAME`` and ``__builtin_FUNCSIG``,
these builtins are also implemented by GCC.

**Syntax**:

.. code-block:: c

  const char *__builtin_FILE();
  const char *__builtin_FILE_NAME(); // Clang only
  const char *__builtin_FUNCTION();
  const char *__builtin_FUNCSIG(); // Microsoft
  unsigned    __builtin_LINE();
  unsigned    __builtin_COLUMN(); // Clang only
  const std::source_location::__impl *__builtin_source_location();

**Example of use**:

.. code-block:: c++

  void my_assert(bool pred, int line = __builtin_LINE(), // Captures line of caller
                 const char* file = __builtin_FILE(),
                 const char* function = __builtin_FUNCTION()) {
    if (pred) return;
    printf("%s:%d assertion failed in function %s\n", file, line, function);
    std::abort();
  }

  struct MyAggregateType {
    int x;
    int line = __builtin_LINE(); // captures line where aggregate initialization occurs
  };
  static_assert(MyAggregateType{42}.line == __LINE__);

  struct MyClassType {
    int line = __builtin_LINE(); // captures line of the constructor used during initialization
    constexpr MyClassType(int) { assert(line == __LINE__); }
  };

**Description**:

The builtins ``__builtin_LINE``, ``__builtin_FUNCTION``, ``__builtin_FUNCSIG``,
``__builtin_FILE`` and ``__builtin_FILE_NAME`` return the values, at the
"invocation point", for ``__LINE__``, ``__FUNCTION__``, ``__FUNCSIG__``,
``__FILE__`` and ``__FILE_NAME__`` respectively. ``__builtin_COLUMN`` similarly
returns the column, though there is no corresponding macro. These builtins are
constant expressions.

When the builtins appear as part of a default function argument the invocation
point is the location of the caller. When the builtins appear as part of a
default member initializer, the invocation point is the location of the
constructor or aggregate initialization used to create the object. Otherwise
the invocation point is the same as the location of the builtin.

When the invocation point of ``__builtin_FUNCTION`` is not a function scope the
empty string is returned.

The builtin ``__builtin_source_location`` returns a pointer to constant static
data of type ``std::source_location::__impl``. This type must have already been
defined, and must contain exactly four fields: ``const char *_M_file_name``,
``const char *_M_function_name``, ``<any-integral-type> _M_line``, and
``<any-integral-type> _M_column``. The fields will be populated in the same
manner as the above four builtins, except that ``_M_function_name`` is populated
with ``__PRETTY_FUNCTION__`` rather than ``__FUNCTION__``.


Alignment builtins
------------------
Clang provides builtins to support checking and adjusting alignment of
pointers and integers.
These builtins can be used to avoid relying on implementation-defined behavior
of arithmetic on integers derived from pointers.
Additionally, these builtins retain type information and, unlike bitwise
arithmetic, they can perform semantic checking on the alignment value.

**Syntax**:

.. code-block:: c

  Type __builtin_align_up(Type value, size_t alignment);
  Type __builtin_align_down(Type value, size_t alignment);
  bool __builtin_is_aligned(Type value, size_t alignment);


**Example of use**:

.. code-block:: c++

  char* global_alloc_buffer;
  void* my_aligned_allocator(size_t alloc_size, size_t alignment) {
    char* result = __builtin_align_up(global_alloc_buffer, alignment);
    // result now contains the value of global_alloc_buffer rounded up to the
    // next multiple of alignment.
    global_alloc_buffer = result + alloc_size;
    return result;
  }

  void* get_start_of_page(void* ptr) {
    return __builtin_align_down(ptr, PAGE_SIZE);
  }

  void example(char* buffer) {
     if (__builtin_is_aligned(buffer, 64)) {
       do_fast_aligned_copy(buffer);
     } else {
       do_unaligned_copy(buffer);
     }
  }

  // In addition to pointers, the builtins can also be used on integer types
  // and are evaluatable inside constant expressions.
  static_assert(__builtin_align_up(123, 64) == 128, "");
  static_assert(__builtin_align_down(123u, 64) == 64u, "");
  static_assert(!__builtin_is_aligned(123, 64), "");


**Description**:

The builtins ``__builtin_align_up``, ``__builtin_align_down``, return their
first argument aligned up/down to the next multiple of the second argument.
If the value is already sufficiently aligned, it is returned unchanged.
The builtin ``__builtin_is_aligned`` returns whether the first argument is
aligned to a multiple of the second argument.
All of these builtins expect the alignment to be expressed as a number of bytes.

These builtins can be used for all integer types as well as (non-function)
pointer types. For pointer types, these builtins operate in terms of the integer
address of the pointer and return a new pointer of the same type (including
qualifiers such as ``const``) with an adjusted address.
When aligning pointers up or down, the resulting value must be within the same
underlying allocation or one past the end (see C17 6.5.6p8, C++ [expr.add]).
This means that arbitrary integer values stored in pointer-type variables must
not be passed to these builtins. For those use cases, the builtins can still be
used, but the operation must be performed on the pointer cast to ``uintptr_t``.

If Clang can determine that the alignment is not a power of two at compile time,
it will result in a compilation failure. If the alignment argument is not a
power of two at run time, the behavior of these builtins is undefined.

Non-standard C++11 Attributes
=============================

Clang's non-standard C++11 attributes live in the ``clang`` attribute
namespace.

Clang supports GCC's ``gnu`` attribute namespace. All GCC attributes which
are accepted with the ``__attribute__((foo))`` syntax are also accepted as
``[[gnu::foo]]``. This only extends to attributes which are specified by GCC
(see the list of `GCC function attributes
<https://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html>`_, `GCC variable
attributes <https://gcc.gnu.org/onlinedocs/gcc/Variable-Attributes.html>`_, and
`GCC type attributes
<https://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html>`_). As with the GCC
implementation, these attributes must appertain to the *declarator-id* in a
declaration, which means they must go either at the start of the declaration or
immediately after the name being declared.

For example, this applies the GNU ``unused`` attribute to ``a`` and ``f``, and
also applies the GNU ``noreturn`` attribute to ``f``.

Examples:
.. code-block:: c++

  [[gnu::unused]] int a, f [[gnu::noreturn]] ();

Target-Specific Extensions
==========================

Clang supports some language features conditionally on some targets.

AMDGPU Language Extensions
--------------------------

__builtin_amdgcn_fence
^^^^^^^^^^^^^^^^^^^^^^

``__builtin_amdgcn_fence`` emits a fence.

* ``unsigned`` atomic ordering, e.g. ``__ATOMIC_ACQUIRE``
* ``const char *`` synchronization scope, e.g. ``workgroup``
* Zero or more ``const char *`` address spaces names.

The address spaces arguments must be one of the following string literals:

* ``"local"``
* ``"global"``

If one or more address space name are provided, the code generator will attempt
to emit potentially faster instructions that order access to at least those
address spaces.
Emitting such instructions may not always be possible and the compiler is free
to fence more aggressively.

If no address spaces names are provided, all address spaces are fenced.

.. code-block:: c++

  // Fence all address spaces.
  __builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup");
  __builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "agent");

  // Fence only requested address spaces.
  __builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup", "local")
  __builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup", "local", "global")


ARM/AArch64 Language Extensions
-------------------------------

Memory Barrier Intrinsics
^^^^^^^^^^^^^^^^^^^^^^^^^
Clang implements the ``__dmb``, ``__dsb`` and ``__isb`` intrinsics as defined
in the `Arm C Language Extensions
<https://github.com/ARM-software/acle/releases>`_.
Note that these intrinsics are implemented as motion barriers that block
reordering of memory accesses and side effect instructions. Other instructions
like simple arithmetic may be reordered around the intrinsic. If you expect to
have no reordering at all, use inline assembly instead.

Pointer Authentication
^^^^^^^^^^^^^^^^^^^^^^
See :doc:`PointerAuthentication`.

X86/X86-64 Language Extensions
------------------------------

The X86 backend has these language extensions:

Memory references to specified segments
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Annotating a pointer with address space #256 causes it to be code generated
relative to the X86 GS segment register, address space #257 causes it to be
relative to the X86 FS segment, and address space #258 causes it to be
relative to the X86 SS segment.  Note that this is a very very low-level
feature that should only be used if you know what you're doing (for example in
an OS kernel).

Here is an example:

.. code-block:: c++

  #define GS_RELATIVE __attribute__((address_space(256)))
  int foo(int GS_RELATIVE *P) {
    return *P;
  }

Which compiles to (on X86-32):

.. code-block:: gas

  _foo:
          movl    4(%esp), %eax
          movl    %gs:(%eax), %eax
          ret

You can also use the GCC compatibility macros ``__seg_fs`` and ``__seg_gs`` for
the same purpose. The preprocessor symbols ``__SEG_FS`` and ``__SEG_GS``
indicate their support.

PowerPC Language Extensions
---------------------------

Set the Floating Point Rounding Mode
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
PowerPC64/PowerPC64le supports the builtin function ``__builtin_setrnd`` to set
the floating point rounding mode. This function will use the least significant
two bits of integer argument to set the floating point rounding mode.

.. code-block:: c++

  double __builtin_setrnd(int mode);

The effective values for mode are:

    - 0 - round to nearest
    - 1 - round to zero
    - 2 - round to +infinity
    - 3 - round to -infinity

Note that the mode argument will modulo 4, so if the integer argument is greater
than 3, it will only use the least significant two bits of the mode.
Namely, ``__builtin_setrnd(102))`` is equal to ``__builtin_setrnd(2)``.

PowerPC cache builtins
^^^^^^^^^^^^^^^^^^^^^^

The PowerPC architecture specifies instructions implementing cache operations.
Clang provides builtins that give direct programmer access to these cache
instructions.

Currently the following builtins are implemented in clang:

``__builtin_dcbf`` copies the contents of a modified block from the data cache
to main memory and flushes the copy from the data cache.

**Syntax**:

.. code-block:: c

  void __dcbf(const void* addr); /* Data Cache Block Flush */

**Example of Use**:

.. code-block:: c

  int a = 1;
  __builtin_dcbf (&a);

Extensions for Static Analysis
==============================

Clang supports additional attributes that are useful for documenting program
invariants and rules for static analysis tools, such as the `Clang Static
Analyzer <https://clang-analyzer.llvm.org/>`_. These attributes are documented
in the analyzer's `list of source-level annotations
<https://clang-analyzer.llvm.org/annotations.html>`_.


Extensions for Dynamic Analysis
===============================

Use ``__has_feature(address_sanitizer)`` to check if the code is being built
with :doc:`AddressSanitizer`.

Use ``__has_feature(thread_sanitizer)`` to check if the code is being built
with :doc:`ThreadSanitizer`.

Use ``__has_feature(memory_sanitizer)`` to check if the code is being built
with :doc:`MemorySanitizer`.

Use ``__has_feature(dataflow_sanitizer)`` to check if the code is being built
with :doc:`DataFlowSanitizer`.

Use ``__has_feature(safe_stack)`` to check if the code is being built
with :doc:`SafeStack`.


Extensions for selectively disabling optimization
=================================================

Clang provides a mechanism for selectively disabling optimizations in functions
and methods.

To disable optimizations in a single function definition, the GNU-style or C++11
non-standard attribute ``optnone`` can be used.

.. code-block:: c++

  // The following functions will not be optimized.
  // GNU-style attribute
  __attribute__((optnone)) int foo() {
    // ... code
  }
  // C++11 attribute
  [[clang::optnone]] int bar() {
    // ... code
  }

To facilitate disabling optimization for a range of function definitions, a
range-based pragma is provided. Its syntax is ``#pragma clang optimize``
followed by ``off`` or ``on``.

All function definitions in the region between an ``off`` and the following
``on`` will be decorated with the ``optnone`` attribute unless doing so would
conflict with explicit attributes already present on the function (e.g. the
ones that control inlining).

.. code-block:: c++

  #pragma clang optimize off
  // This function will be decorated with optnone.
  int foo() {
    // ... code
  }

  // optnone conflicts with always_inline, so bar() will not be decorated.
  __attribute__((always_inline)) int bar() {
    // ... code
  }
  #pragma clang optimize on

If no ``on`` is found to close an ``off`` region, the end of the region is the
end of the compilation unit.

Note that a stray ``#pragma clang optimize on`` does not selectively enable
additional optimizations when compiling at low optimization levels. This feature
can only be used to selectively disable optimizations.

The pragma has an effect on functions only at the point of their definition; for
function templates, this means that the state of the pragma at the point of an
instantiation is not necessarily relevant. Consider the following example:

.. code-block:: c++

  template<typename T> T twice(T t) {
    return 2 * t;
  }

  #pragma clang optimize off
  template<typename T> T thrice(T t) {
    return 3 * t;
  }

  int container(int a, int b) {
    return twice(a) + thrice(b);
  }
  #pragma clang optimize on

In this example, the definition of the template function ``twice`` is outside
the pragma region, whereas the definition of ``thrice`` is inside the region.
The ``container`` function is also in the region and will not be optimized, but
it causes the instantiation of ``twice`` and ``thrice`` with an ``int`` type; of
these two instantiations, ``twice`` will be optimized (because its definition
was outside the region) and ``thrice`` will not be optimized.

Clang also implements MSVC's range-based pragma,
``#pragma optimize("[optimization-list]", on | off)``. At the moment, Clang only
supports an empty optimization list, whereas MSVC supports the arguments, ``s``,
``g``, ``t``, and ``y``. Currently, the implementation of ``pragma optimize`` behaves
the same as ``#pragma clang optimize``. All functions
between ``off`` and ``on`` will be decorated with the ``optnone`` attribute.

.. code-block:: c++

  #pragma optimize("", off)
  // This function will be decorated with optnone.
  void f1() {}

  #pragma optimize("", on)
  // This function will be optimized with whatever was specified on
  // the commandline.
  void f2() {}

  // This will warn with Clang's current implementation.
  #pragma optimize("g", on)
  void f3() {}

For MSVC, an empty optimization list and ``off`` parameter will turn off
all optimizations, ``s``, ``g``, ``t``, and ``y``. An empty optimization and
``on`` parameter will reset the optimizations to the ones specified on the
commandline.

.. list-table:: Parameters (unsupported by Clang)

   * - Parameter
     - Type of optimization
   * - g
     - Deprecated
   * - s or t
     - Short or fast sequences of machine code
   * - y
     - Enable frame pointers

Extensions for loop hint optimizations
======================================

The ``#pragma clang loop`` directive is used to specify hints for optimizing the
subsequent for, while, do-while, or c++11 range-based for loop. The directive
provides options for vectorization, interleaving, predication, unrolling and
distribution. Loop hints can be specified before any loop and will be ignored if
the optimization is not safe to apply.

There are loop hints that control transformations (e.g. vectorization, loop
unrolling) and there are loop hints that set transformation options (e.g.
``vectorize_width``, ``unroll_count``).  Pragmas setting transformation options
imply the transformation is enabled, as if it was enabled via the corresponding
transformation pragma (e.g. ``vectorize(enable)``). If the transformation is
disabled  (e.g. ``vectorize(disable)``), that takes precedence over
transformations option pragmas implying that transformation.

Vectorization, Interleaving, and Predication
--------------------------------------------

A vectorized loop performs multiple iterations of the original loop
in parallel using vector instructions. The instruction set of the target
processor determines which vector instructions are available and their vector
widths. This restricts the types of loops that can be vectorized. The vectorizer
automatically determines if the loop is safe and profitable to vectorize. A
vector instruction cost model is used to select the vector width.

Interleaving multiple loop iterations allows modern processors to further
improve instruction-level parallelism (ILP) using advanced hardware features,
such as multiple execution units and out-of-order execution. The vectorizer uses
a cost model that depends on the register pressure and generated code size to
select the interleaving count.

Vectorization is enabled by ``vectorize(enable)`` and interleaving is enabled
by ``interleave(enable)``. This is useful when compiling with ``-Os`` to
manually enable vectorization or interleaving.

.. code-block:: c++

  #pragma clang loop vectorize(enable)
  #pragma clang loop interleave(enable)
  for(...) {
    ...
  }

The vector width is specified by
``vectorize_width(_value_[, fixed|scalable])``, where _value_ is a positive
integer and the type of vectorization can be specified with an optional
second parameter. The default for the second parameter is 'fixed' and
refers to fixed width vectorization, whereas 'scalable' indicates the
compiler should use scalable vectors instead. Another use of vectorize_width
is ``vectorize_width(fixed|scalable)`` where the user can hint at the type
of vectorization to use without specifying the exact width. In both variants
of the pragma the vectorizer may decide to fall back on fixed width
vectorization if the target does not support scalable vectors.

The interleave count is specified by ``interleave_count(_value_)``, where
_value_ is a positive integer. This is useful for specifying the optimal
width/count of the set of target architectures supported by your application.

.. code-block:: c++

  #pragma clang loop vectorize_width(2)
  #pragma clang loop interleave_count(2)
  for(...) {
    ...
  }

Specifying a width/count of 1 disables the optimization, and is equivalent to
``vectorize(disable)`` or ``interleave(disable)``.

Vector predication is enabled by ``vectorize_predicate(enable)``, for example:

.. code-block:: c++

  #pragma clang loop vectorize(enable)
  #pragma clang loop vectorize_predicate(enable)
  for(...) {
    ...
  }

This predicates (masks) all instructions in the loop, which allows the scalar
remainder loop (the tail) to be folded into the main vectorized loop. This
might be more efficient when vector predication is efficiently supported by the
target platform.

Loop Unrolling
--------------

Unrolling a loop reduces the loop control overhead and exposes more
opportunities for ILP. Loops can be fully or partially unrolled. Full unrolling
eliminates the loop and replaces it with an enumerated sequence of loop
iterations. Full unrolling is only possible if the loop trip count is known at
compile time. Partial unrolling replicates the loop body within the loop and
reduces the trip count.

If ``unroll(enable)`` is specified the unroller will attempt to fully unroll the
loop if the trip count is known at compile time. If the fully unrolled code size
is greater than an internal limit the loop will be partially unrolled up to this
limit. If the trip count is not known at compile time the loop will be partially
unrolled with a heuristically chosen unroll factor.

.. code-block:: c++

  #pragma clang loop unroll(enable)
  for(...) {
    ...
  }

If ``unroll(full)`` is specified the unroller will attempt to fully unroll the
loop if the trip count is known at compile time identically to
``unroll(enable)``. However, with ``unroll(full)`` the loop will not be unrolled
if the loop count is not known at compile time.

.. code-block:: c++

  #pragma clang loop unroll(full)
  for(...) {
    ...
  }

The unroll count can be specified explicitly with ``unroll_count(_value_)`` where
_value_ is a positive integer. If this value is greater than the trip count the
loop will be fully unrolled. Otherwise the loop is partially unrolled subject
to the same code size limit as with ``unroll(enable)``.

.. code-block:: c++

  #pragma clang loop unroll_count(8)
  for(...) {
    ...
  }

Unrolling of a loop can be prevented by specifying ``unroll(disable)``.

Loop unroll parameters can be controlled by options
`-mllvm -unroll-count=n` and `-mllvm -pragma-unroll-threshold=n`.

Loop Distribution
-----------------

Loop Distribution allows splitting a loop into multiple loops.  This is
beneficial for example when the entire loop cannot be vectorized but some of the
resulting loops can.

If ``distribute(enable))`` is specified and the loop has memory dependencies
that inhibit vectorization, the compiler will attempt to isolate the offending
operations into a new loop.  This optimization is not enabled by default, only
loops marked with the pragma are considered.

.. code-block:: c++

  #pragma clang loop distribute(enable)
  for (i = 0; i < N; ++i) {
    S1: A[i + 1] = A[i] + B[i];
    S2: C[i] = D[i] * E[i];
  }

This loop will be split into two loops between statements S1 and S2.  The
second loop containing S2 will be vectorized.

Loop Distribution is currently not enabled by default in the optimizer because
it can hurt performance in some cases.  For example, instruction-level
parallelism could be reduced by sequentializing the execution of the
statements S1 and S2 above.

If Loop Distribution is turned on globally with
``-mllvm -enable-loop-distribution``, specifying ``distribute(disable)`` can
be used the disable it on a per-loop basis.

Additional Information
----------------------

For convenience multiple loop hints can be specified on a single line.

.. code-block:: c++

  #pragma clang loop vectorize_width(4) interleave_count(8)
  for(...) {
    ...
  }

If an optimization cannot be applied any hints that apply to it will be ignored.
For example, the hint ``vectorize_width(4)`` is ignored if the loop is not
proven safe to vectorize. To identify and diagnose optimization issues use
`-Rpass`, `-Rpass-missed`, and `-Rpass-analysis` command line options. See the
user guide for details.

Extensions to specify floating-point flags
====================================================

The ``#pragma clang fp`` pragma allows floating-point options to be specified
for a section of the source code. This pragma can only appear at file scope or
at the start of a compound statement (excluding comments). When using within a
compound statement, the pragma is active within the scope of the compound
statement.

Currently, the following settings can be controlled with this pragma:

``#pragma clang fp reassociate`` allows control over the reassociation
of floating point expressions. When enabled, this pragma allows the expression
``x + (y + z)`` to be reassociated as ``(x + y) + z``.
Reassociation can also occur across multiple statements.
This pragma can be used to disable reassociation when it is otherwise
enabled for the translation unit with the ``-fassociative-math`` flag.
The pragma can take two values: ``on`` and ``off``.

.. code-block:: c++

  float f(float x, float y, float z)
  {
    // Enable floating point reassociation across statements
    #pragma clang fp reassociate(on)
    float t = x + y;
    float v = t + z;
  }

``#pragma clang fp reciprocal`` allows control over using reciprocal
approximations in floating point expressions. When enabled, this
pragma allows the expression ``x / y`` to be approximated as ``x *
(1.0 / y)``.  This pragma can be used to disable reciprocal
approximation when it is otherwise enabled for the translation unit
with the ``-freciprocal-math`` flag or other fast-math options. The
pragma can take two values: ``on`` and ``off``.

.. code-block:: c++

  float f(float x, float y)
  {
    // Enable floating point reciprocal approximation
    #pragma clang fp reciprocal(on)
    return x / y;
  }

``#pragma clang fp contract`` specifies whether the compiler should
contract a multiply and an addition (or subtraction) into a fused FMA
operation when supported by the target.

The pragma can take three values: ``on``, ``fast`` and ``off``.  The ``on``
option is identical to using ``#pragma STDC FP_CONTRACT(ON)`` and it allows
fusion as specified the language standard.  The ``fast`` option allows fusion
in cases when the language standard does not make this possible (e.g. across
statements in C).

.. code-block:: c++

  for(...) {
    #pragma clang fp contract(fast)
    a = b[i] * c[i];
    d[i] += a;
  }


The pragma can also be used with ``off`` which turns FP contraction off for a
section of the code. This can be useful when fast contraction is otherwise
enabled for the translation unit with the ``-ffp-contract=fast-honor-pragmas`` flag.
Note that ``-ffp-contract=fast`` will override pragmas to fuse multiply and
addition across statements regardless of any controlling pragmas.

``#pragma clang fp exceptions`` specifies floating point exception behavior. It
may take one of the values: ``ignore``, ``maytrap`` or ``strict``. Meaning of
these values is same as for `constrained floating point intrinsics <http://llvm.org/docs/LangRef.html#constrained-floating-point-intrinsics>`_.

.. code-block:: c++

  {
    // Preserve floating point exceptions
    #pragma clang fp exceptions(strict)
    z = x + y;
    if (fetestexcept(FE_OVERFLOW))
      ...
  }

A ``#pragma clang fp`` pragma may contain any number of options:

.. code-block:: c++

  void func(float *dest, float a, float b) {
    #pragma clang fp exceptions(maytrap) contract(fast) reassociate(on)
    ...
  }

``#pragma clang fp eval_method`` allows floating-point behavior to be specified
for a section of the source code. This pragma can appear at file or namespace
scope, or at the start of a compound statement (excluding comments).
The pragma is active within the scope of the compound statement.

When ``pragma clang fp eval_method(source)`` is enabled, the section of code
governed by the pragma behaves as though the command-line option
``-ffp-eval-method=source`` is enabled. Rounds intermediate results to
source-defined precision.

When ``pragma clang fp eval_method(double)`` is enabled, the section of code
governed by the pragma behaves as though the command-line option
``-ffp-eval-method=double`` is enabled. Rounds intermediate results to
``double`` precision.

When ``pragma clang fp eval_method(extended)`` is enabled, the section of code
governed by the pragma behaves as though the command-line option
``-ffp-eval-method=extended`` is enabled. Rounds intermediate results to
target-dependent ``long double`` precision. In Win32 programming, for instance,
the long double data type maps to the double, 64-bit precision data type.

The full syntax this pragma supports is
``#pragma clang fp eval_method(source|double|extended)``.

.. code-block:: c++

  for(...) {
    // The compiler will use long double as the floating-point evaluation
    // method.
    #pragma clang fp eval_method(extended)
    a = b[i] * c[i] + e;
  }

Note: ``math.h`` defines the typedefs ``float_t`` and ``double_t`` based on the active
evaluation method at the point where the header is included, not where the
typedefs are used.  Because of this, it is unwise to combine these typedefs with
``#pragma clang fp eval_method``.  To catch obvious bugs, Clang will emit an
error for any references to these typedefs within the scope of this pragma;
however, this is not a fool-proof protection, and programmers must take care.

The ``#pragma float_control`` pragma allows precise floating-point
semantics and floating-point exception behavior to be specified
for a section of the source code. This pragma can only appear at file or
namespace scope, within a language linkage specification or at the start of a
compound statement (excluding comments). When used within a compound statement,
the pragma is active within the scope of the compound statement.  This pragma
is modeled after a Microsoft pragma with the same spelling and syntax.  For
pragmas specified at file or namespace scope, or within a language linkage
specification, a stack is supported so that the ``pragma float_control``
settings can be pushed or popped.

When ``pragma float_control(precise, on)`` is enabled, the section of code
governed by the pragma uses precise floating point semantics, effectively
``-ffast-math`` is disabled and ``-ffp-contract=on``
(fused multiply add) is enabled. This pragma enables ``-fmath-errno``.

When ``pragma float_control(precise, off)`` is enabled, unsafe-floating point
optimizations are enabled in the section of code governed by the pragma.
Effectively ``-ffast-math`` is enabled and ``-ffp-contract=fast``. This pragma
disables ``-fmath-errno``.

When ``pragma float_control(except, on)`` is enabled, the section of code
governed by the pragma behaves as though the command-line option
``-ffp-exception-behavior=strict`` is enabled,
when ``pragma float_control(except, off)`` is enabled, the section of code
governed by the pragma behaves as though the command-line option
``-ffp-exception-behavior=ignore`` is enabled.

The full syntax this pragma supports is
``float_control(except|precise, on|off [, push])`` and
``float_control(push|pop)``.
The ``push`` and ``pop`` forms, including using ``push`` as the optional
third argument, can only occur at file scope.

.. code-block:: c++

  for(...) {
    // This block will be compiled with -fno-fast-math and -ffp-contract=on
    #pragma float_control(precise, on)
    a = b[i] * c[i] + e;
  }

Specifying an attribute for multiple declarations (#pragma clang attribute)
===========================================================================

The ``#pragma clang attribute`` directive can be used to apply an attribute to
multiple declarations. The ``#pragma clang attribute push`` variation of the
directive pushes a new "scope" of ``#pragma clang attribute`` that attributes
can be added to. The ``#pragma clang attribute (...)`` variation adds an
attribute to that scope, and the ``#pragma clang attribute pop`` variation pops
the scope. You can also use ``#pragma clang attribute push (...)``, which is a
shorthand for when you want to add one attribute to a new scope. Multiple push
directives can be nested inside each other.

The attributes that are used in the ``#pragma clang attribute`` directives
can be written using the GNU-style syntax:

.. code-block:: c++

  #pragma clang attribute push (__attribute__((annotate("custom"))), apply_to = function)

  void function(); // The function now has the annotate("custom") attribute

  #pragma clang attribute pop

The attributes can also be written using the C++11 style syntax:

.. code-block:: c++

  #pragma clang attribute push ([[noreturn]], apply_to = function)

  void function(); // The function now has the [[noreturn]] attribute

  #pragma clang attribute pop

The ``__declspec`` style syntax is also supported:

.. code-block:: c++

  #pragma clang attribute push (__declspec(dllexport), apply_to = function)

  void function(); // The function now has the __declspec(dllexport) attribute

  #pragma clang attribute pop

A single push directive can contain multiple attributes, however,
only one syntax style can be used within a single directive:

.. code-block:: c++

  #pragma clang attribute push ([[noreturn, noinline]], apply_to = function)

  void function1(); // The function now has the [[noreturn]] and [[noinline]] attributes

  #pragma clang attribute pop

  #pragma clang attribute push (__attribute((noreturn, noinline)), apply_to = function)

  void function2(); // The function now has the __attribute((noreturn)) and __attribute((noinline)) attributes

  #pragma clang attribute pop

Because multiple push directives can be nested, if you're writing a macro that
expands to ``_Pragma("clang attribute")`` it's good hygiene (though not
required) to add a namespace to your push/pop directives. A pop directive with a
namespace will pop the innermost push that has that same namespace. This will
ensure that another macro's ``pop`` won't inadvertently pop your attribute. Note
that an ``pop`` without a namespace will pop the innermost ``push`` without a
namespace. ``push``es with a namespace can only be popped by ``pop`` with the
same namespace. For instance:

.. code-block:: c++

   #define ASSUME_NORETURN_BEGIN _Pragma("clang attribute AssumeNoreturn.push ([[noreturn]], apply_to = function)")
   #define ASSUME_NORETURN_END   _Pragma("clang attribute AssumeNoreturn.pop")

   #define ASSUME_UNAVAILABLE_BEGIN _Pragma("clang attribute Unavailable.push (__attribute__((unavailable)), apply_to=function)")
   #define ASSUME_UNAVAILABLE_END   _Pragma("clang attribute Unavailable.pop")


   ASSUME_NORETURN_BEGIN
   ASSUME_UNAVAILABLE_BEGIN
   void function(); // function has [[noreturn]] and __attribute__((unavailable))
   ASSUME_NORETURN_END
   void other_function(); // function has __attribute__((unavailable))
   ASSUME_UNAVAILABLE_END

Without the namespaces on the macros, ``other_function`` will be annotated with
``[[noreturn]]`` instead of ``__attribute__((unavailable))``. This may seem like
a contrived example, but its very possible for this kind of situation to appear
in real code if the pragmas are spread out across a large file. You can test if
your version of clang supports namespaces on ``#pragma clang attribute`` with
``__has_extension(pragma_clang_attribute_namespaces)``.

Subject Match Rules
-------------------

The set of declarations that receive a single attribute from the attribute stack
depends on the subject match rules that were specified in the pragma. Subject
match rules are specified after the attribute. The compiler expects an
identifier that corresponds to the subject set specifier. The ``apply_to``
specifier is currently the only supported subject set specifier. It allows you
to specify match rules that form a subset of the attribute's allowed subject
set, i.e. the compiler doesn't require all of the attribute's subjects. For
example, an attribute like ``[[nodiscard]]`` whose subject set includes
``enum``, ``record`` and ``hasType(functionType)``, requires the presence of at
least one of these rules after ``apply_to``:

.. code-block:: c++

  #pragma clang attribute push([[nodiscard]], apply_to = enum)

  enum Enum1 { A1, B1 }; // The enum will receive [[nodiscard]]

  struct Record1 { }; // The struct will *not* receive [[nodiscard]]

  #pragma clang attribute pop

  #pragma clang attribute push([[nodiscard]], apply_to = any(record, enum))

  enum Enum2 { A2, B2 }; // The enum will receive [[nodiscard]]

  struct Record2 { }; // The struct *will* receive [[nodiscard]]

  #pragma clang attribute pop

  // This is an error, since [[nodiscard]] can't be applied to namespaces:
  #pragma clang attribute push([[nodiscard]], apply_to = any(record, namespace))

  #pragma clang attribute pop

Multiple match rules can be specified using the ``any`` match rule, as shown
in the example above. The ``any`` rule applies attributes to all declarations
that are matched by at least one of the rules in the ``any``. It doesn't nest
and can't be used inside the other match rules. Redundant match rules or rules
that conflict with one another should not be used inside of ``any``. Failing to
specify a rule within the ``any`` rule results in an error.

Clang supports the following match rules:

- ``function``: Can be used to apply attributes to functions. This includes C++
  member functions, static functions, operators, and constructors/destructors.

- ``function(is_member)``: Can be used to apply attributes to C++ member
  functions. This includes members like static functions, operators, and
  constructors/destructors.

- ``hasType(functionType)``: Can be used to apply attributes to functions, C++
  member functions, and variables/fields whose type is a function pointer. It
  does not apply attributes to Objective-C methods or blocks.

- ``type_alias``: Can be used to apply attributes to ``typedef`` declarations
  and C++11 type aliases.

- ``record``: Can be used to apply attributes to ``struct``, ``class``, and
  ``union`` declarations.

- ``record(unless(is_union))``: Can be used to apply attributes only to
  ``struct`` and ``class`` declarations.

- ``enum``: Can be used to apply attributes to enumeration declarations.

- ``enum_constant``: Can be used to apply attributes to enumerators.

- ``variable``: Can be used to apply attributes to variables, including
  local variables, parameters, global variables, and static member variables.
  It does not apply attributes to instance member variables or Objective-C
  ivars.

- ``variable(is_thread_local)``: Can be used to apply attributes to thread-local
  variables only.

- ``variable(is_global)``: Can be used to apply attributes to global variables
  only.

- ``variable(is_local)``: Can be used to apply attributes to local variables
  only.

- ``variable(is_parameter)``: Can be used to apply attributes to parameters
  only.

- ``variable(unless(is_parameter))``: Can be used to apply attributes to all
  the variables that are not parameters.

- ``field``: Can be used to apply attributes to non-static member variables
  in a record. This includes Objective-C ivars.

- ``namespace``: Can be used to apply attributes to ``namespace`` declarations.

- ``objc_interface``: Can be used to apply attributes to ``@interface``
  declarations.

- ``objc_protocol``: Can be used to apply attributes to ``@protocol``
  declarations.

- ``objc_category``: Can be used to apply attributes to category declarations,
  including class extensions.

- ``objc_method``: Can be used to apply attributes to Objective-C methods,
  including instance and class methods. Implicit methods like implicit property
  getters and setters do not receive the attribute.

- ``objc_method(is_instance)``: Can be used to apply attributes to Objective-C
  instance methods.

- ``objc_property``: Can be used to apply attributes to ``@property``
  declarations.

- ``block``: Can be used to apply attributes to block declarations. This does
  not include variables/fields of block pointer type.

The use of ``unless`` in match rules is currently restricted to a strict set of
sub-rules that are used by the supported attributes. That means that even though
``variable(unless(is_parameter))`` is a valid match rule,
``variable(unless(is_thread_local))`` is not.

Supported Attributes
--------------------

Not all attributes can be used with the ``#pragma clang attribute`` directive.
Notably, statement attributes like ``[[fallthrough]]`` or type attributes
like ``address_space`` aren't supported by this directive. You can determine
whether or not an attribute is supported by the pragma by referring to the
:doc:`individual documentation for that attribute <AttributeReference>`.

The attributes are applied to all matching declarations individually, even when
the attribute is semantically incorrect. The attributes that aren't applied to
any declaration are not verified semantically.

Specifying section names for global objects (#pragma clang section)
===================================================================

The ``#pragma clang section`` directive provides a means to assign section-names
to global variables, functions and static variables.

The section names can be specified as:

.. code-block:: c++

  #pragma clang section bss="myBSS" data="myData" rodata="myRodata" relro="myRelro" text="myText"

The section names can be reverted back to default name by supplying an empty
string to the section kind, for example:

.. code-block:: c++

  #pragma clang section bss="" data="" text="" rodata="" relro=""

The ``#pragma clang section`` directive obeys the following rules:

* The pragma applies to all global variable, statics and function declarations
  from the pragma to the end of the translation unit.

* The pragma clang section is enabled automatically, without need of any flags.

* This feature is only defined to work sensibly for ELF and Mach-O targets.

* If section name is specified through _attribute_((section("myname"))), then
  the attribute name gains precedence.

* Global variables that are initialized to zero will be placed in the named
  bss section, if one is present.

* The ``#pragma clang section`` directive does not does try to infer section-kind
  from the name. For example, naming a section "``.bss.mySec``" does NOT mean
  it will be a bss section name.

* The decision about which section-kind applies to each global is taken in the back-end.
  Once the section-kind is known, appropriate section name, as specified by the user using
  ``#pragma clang section`` directive, is applied to that global.

Specifying Linker Options on ELF Targets
========================================

The ``#pragma comment(lib, ...)`` directive is supported on all ELF targets.
The second parameter is the library name (without the traditional Unix prefix of
``lib``).  This allows you to provide an implicit link of dependent libraries.

Evaluating Object Size
======================

Clang supports the builtins ``__builtin_object_size`` and
``__builtin_dynamic_object_size``. The semantics are compatible with GCC's
builtins of the same names, but the details are slightly different.

.. code-block:: c

  size_t __builtin_[dynamic_]object_size(const void *ptr, int type)

Returns the number of accessible bytes ``n`` past ``ptr``. The value returned
depends on ``type``, which is required to be an integer constant between 0 and
3:

* If ``type & 2 == 0``, the least ``n`` is returned such that accesses to
  ``(const char*)ptr + n`` and beyond are known to be out of bounds. This is
  ``(size_t)-1`` if no better bound is known.
* If ``type & 2 == 2``, the greatest ``n`` is returned such that accesses to
  ``(const char*)ptr + i`` are known to be in bounds, for 0 <= ``i`` < ``n``.
  This is ``(size_t)0`` if no better bound is known.

.. code-block:: c

  char small[10], large[100];
  bool cond;
  // Returns 100: writes of more than 100 bytes are known to be out of bounds.
  int n100 = __builtin_object_size(cond ? small : large, 0);
  // Returns 10: writes of 10 or fewer bytes are known to be in bounds.
  int n10 = __builtin_object_size(cond ? small : large, 2);

* If ``type & 1 == 0``, pointers are considered to be in bounds if they point
  into the same storage as ``ptr`` -- that is, the same stack object, global
  variable, or heap allocation.
* If ``type & 1 == 1``, pointers are considered to be in bounds if they point
  to the same subobject that ``ptr`` points to. If ``ptr`` points to an array
  element, other elements of the same array, but not of enclosing arrays, are
  considered in bounds.

.. code-block:: c

  struct X { char a, b, c; } x;
  static_assert(__builtin_object_size(&x, 0) == 3);
  static_assert(__builtin_object_size(&x.b, 0) == 2);
  static_assert(__builtin_object_size(&x.b, 1) == 1);

.. code-block:: c

  char a[10][10][10];
  static_assert(__builtin_object_size(&a, 1) == 1000);
  static_assert(__builtin_object_size(&a[1], 1) == 900);
  static_assert(__builtin_object_size(&a[1][1], 1) == 90);
  static_assert(__builtin_object_size(&a[1][1][1], 1) == 9);

The values returned by this builtin are a best effort conservative approximation
of the correct answers. When ``type & 2 == 0``, the true value is less than or
equal to the value returned by the builtin, and when ``type & 2 == 1``, the true
value is greater than or equal to the value returned by the builtin.

For ``__builtin_object_size``, the value is determined entirely at compile time.
With optimization enabled, better results will be produced, especially when the
call to ``__builtin_object_size`` is in a different function from the formation
of the pointer. Unlike in GCC, enabling optimization in Clang does not allow
more information about subobjects to be determined, so the ``type & 1 == 1``
case will often give imprecise results when used across a function call boundary
even when optimization is enabled.

`The pass_object_size and pass_dynamic_object_size attributes <https://clang.llvm.org/docs/AttributeReference.html#pass-object-size-pass-dynamic-object-size>`_
can be used to invisibly pass the object size for a pointer parameter alongside
the pointer in a function call. This allows more precise object sizes to be
determined both when building without optimizations and in the ``type & 1 == 1``
case.

For ``__builtin_dynamic_object_size``, the result is not limited to being a
compile time constant. Instead, a small amount of runtime evaluation is
permitted to determine the size of the object, in order to give a more precise
result. ``__builtin_dynamic_object_size`` is meant to be used as a drop-in
replacement for ``__builtin_object_size`` in libraries that support it. For
instance, here is a program that ``__builtin_dynamic_object_size`` will make
safer:

.. code-block:: c

  void copy_into_buffer(size_t size) {
    char* buffer = malloc(size);
    strlcpy(buffer, "some string", strlen("some string"));
    // Previous line preprocesses to:
    // __builtin___strlcpy_chk(buffer, "some string", strlen("some string"), __builtin_object_size(buffer, 0))
  }

Since the size of ``buffer`` can't be known at compile time, Clang will fold
``__builtin_object_size(buffer, 0)`` into ``-1``. However, if this was written
as ``__builtin_dynamic_object_size(buffer, 0)``, Clang will fold it into
``size``, providing some extra runtime safety.

Deprecating Macros
==================

Clang supports the pragma ``#pragma clang deprecated``, which can be used to
provide deprecation warnings for macro uses. For example:

.. code-block:: c

   #define MIN(x, y) x < y ? x : y
   #pragma clang deprecated(MIN, "use std::min instead")

   int min(int a, int b) {
     return MIN(a, b); // warning: MIN is deprecated: use std::min instead
   }

``#pragma clang deprecated`` should be preferred for this purpose over
``#pragma GCC warning`` because the warning can be controlled with
``-Wdeprecated``.

Restricted Expansion Macros
===========================

Clang supports the pragma ``#pragma clang restrict_expansion``, which can be
used restrict macro expansion in headers. This can be valuable when providing
headers with ABI stability requirements. Any expansion of the annotated macro
processed by the preprocessor after the ``#pragma`` annotation will log a
warning. Redefining the macro or undefining the macro will not be diagnosed, nor
will expansion of the macro within the main source file. For example:

.. code-block:: c

   #define TARGET_ARM 1
   #pragma clang restrict_expansion(TARGET_ARM, "<reason>")

   /// Foo.h
   struct Foo {
   #if TARGET_ARM // warning: TARGET_ARM is marked unsafe in headers: <reason>
     uint32_t X;
   #else
     uint64_t X;
   #endif
   };

   /// main.c
   #include "foo.h"
   #if TARGET_ARM // No warning in main source file
   X_TYPE uint32_t
   #else
   X_TYPE uint64_t
   #endif

This warning is controlled by ``-Wpedantic-macros``.

Final Macros
============

Clang supports the pragma ``#pragma clang final``, which can be used to
mark macros as final, meaning they cannot be undef'd or re-defined. For example:

.. code-block:: c

   #define FINAL_MACRO 1
   #pragma clang final(FINAL_MACRO)

   #define FINAL_MACRO // warning: FINAL_MACRO is marked final and should not be redefined
   #undef FINAL_MACRO  // warning: FINAL_MACRO is marked final and should not be undefined

This is useful for enforcing system-provided macros that should not be altered
in user headers or code. This is controlled by ``-Wpedantic-macros``. Final
macros will always warn on redefinition, including situations with identical
bodies and in system headers.

Line Control
============

Clang supports an extension for source line control, which takes the
form of a preprocessor directive starting with an unsigned integral
constant. In addition to the standard ``#line`` directive, this form
allows control of an include stack and header file type, which is used
in issuing diagnostics. These lines are emitted in preprocessed
output.

.. code-block:: c

   # <line:number> <filename:string> <header-type:numbers>

The filename is optional, and if unspecified indicates no change in
source filename. The header-type is an optional, whitespace-delimited,
sequence of magic numbers as follows.

* ``1:`` Push the current source file name onto the include stack and
  enter a new file.

* ``2``: Pop the include stack and return to the specified file. If
  the filename is ``""``, the name popped from the include stack is
  used. Otherwise there is no requirement that the specified filename
  matches the current source when originally pushed.

* ``3``: Enter a system-header region. System headers often contain
  implementation-specific source that would normally emit a diagnostic.

* ``4``: Enter an implicit ``extern "C"`` region. This is not required on
  modern systems where system headers are C++-aware.

At most a single ``1`` or ``2`` can be present, and values must be in
ascending order.

Examples are:

.. code-block:: c

   # 57 // Advance (or return) to line 57 of the current source file
   # 57 "frob" // Set to line 57 of "frob"
   # 1 "foo.h" 1 // Enter "foo.h" at line 1
   # 59 "main.c" 2 // Leave current include and return to "main.c"
   # 1 "/usr/include/stdio.h" 1 3 // Enter a system header
   # 60 "" 2 // return to "main.c"
   # 1 "/usr/ancient/header.h" 1 4 // Enter an implicit extern "C" header

Extended Integer Types
======================

Clang supports the C23 ``_BitInt(N)`` feature as an extension in older C modes
and in C++. This type was previously implemented in Clang with the same
semantics, but spelled ``_ExtInt(N)``. This spelling has been deprecated in
favor of the standard type.

Note: the ABI for ``_BitInt(N)`` is still in the process of being stabilized,
so this type should not yet be used in interfaces that require ABI stability.

Intrinsics Support within Constant Expressions
==============================================

The following builtin intrinsics can be used in constant expressions:

* ``__builtin_addcb``
* ``__builtin_addcs``
* ``__builtin_addc``
* ``__builtin_addcl``
* ``__builtin_addcll``
* ``__builtin_bitreverse8``
* ``__builtin_bitreverse16``
* ``__builtin_bitreverse32``
* ``__builtin_bitreverse64``
* ``__builtin_bswap16``
* ``__builtin_bswap32``
* ``__builtin_bswap64``
* ``__builtin_clrsb``
* ``__builtin_clrsbl``
* ``__builtin_clrsbll``
* ``__builtin_clz``
* ``__builtin_clzl``
* ``__builtin_clzll``
* ``__builtin_clzs``
* ``__builtin_clzg``
* ``__builtin_ctz``
* ``__builtin_ctzl``
* ``__builtin_ctzll``
* ``__builtin_ctzs``
* ``__builtin_ctzg``
* ``__builtin_ffs``
* ``__builtin_ffsl``
* ``__builtin_ffsll``
* ``__builtin_fmax``
* ``__builtin_fmin``
* ``__builtin_fpclassify``
* ``__builtin_inf``
* ``__builtin_isinf``
* ``__builtin_isinf_sign``
* ``__builtin_isfinite``
* ``__builtin_isnan``
* ``__builtin_isnormal``
* ``__builtin_nan``
* ``__builtin_nans``
* ``__builtin_parity``
* ``__builtin_parityl``
* ``__builtin_parityll``
* ``__builtin_popcount``
* ``__builtin_popcountl``
* ``__builtin_popcountll``
* ``__builtin_popcountg``
* ``__builtin_rotateleft8``
* ``__builtin_rotateleft16``
* ``__builtin_rotateleft32``
* ``__builtin_rotateleft64``
* ``__builtin_rotateright8``
* ``__builtin_rotateright16``
* ``__builtin_rotateright32``
* ``__builtin_rotateright64``
* ``__builtin_subcb``
* ``__builtin_subcs``
* ``__builtin_subc``
* ``__builtin_subcl``
* ``__builtin_subcll``

The following x86-specific intrinsics can be used in constant expressions:

* ``_bit_scan_forward``
* ``_bit_scan_reverse``
* ``__bsfd``
* ``__bsfq``
* ``__bsrd``
* ``__bsrq``
* ``__bswap``
* ``__bswapd``
* ``__bswap64``
* ``__bswapq``
* ``_castf32_u32``
* ``_castf64_u64``
* ``_castu32_f32``
* ``_castu64_f64``
* ``__lzcnt16``
* ``__lzcnt``
* ``__lzcnt64``
* ``_mm_popcnt_u32``
* ``_mm_popcnt_u64``
* ``_popcnt32``
* ``_popcnt64``
* ``__popcntd``
* ``__popcntq``
* ``__popcnt16``
* ``__popcnt``
* ``__popcnt64``
* ``__rolb``
* ``__rolw``
* ``__rold``
* ``__rolq``
* ``__rorb``
* ``__rorw``
* ``__rord``
* ``__rorq``
* ``_rotl``
* ``_rotr``
* ``_rotwl``
* ``_rotwr``
* ``_lrotl``
* ``_lrotr``

Debugging the Compiler
======================

Clang supports a number of pragma directives that help debugging the compiler itself.
Syntax is the following: `#pragma clang __debug <command> <arguments>`.
Note, all of debugging pragmas are subject to change.

`dump`
------
Accepts either a single identifier or an expression. When a single identifier is passed,
the lookup results for the identifier are printed to `stderr`. When an expression is passed,
the AST for the expression is printed to `stderr`. The expression is an unevaluated operand,
so things like overload resolution and template instantiations are performed,
but the expression has no runtime effects.
Type- and value-dependent expressions are not supported yet.

This facility is designed to aid with testing name lookup machinery.

Predefined Macros
=================

`__GCC_DESTRUCTIVE_SIZE` and `__GCC_CONSTRUCTIVE_SIZE`
------------------------------------------------------
Specify the mimum offset between two objects to avoid false sharing and the
maximum size of contiguous memory to promote true sharing, respectively. These
macros are predefined in all C and C++ language modes, but can be redefined on
the command line with ``-D`` to specify different values as needed or can be
undefined on the command line with ``-U`` to disable support for the feature.

**Note: the values the macros expand to are not guaranteed to be stable. They
are are affected by architectures and CPU tuning flags, can change between
releases of Clang and will not match the values defined by other compilers such
as GCC.**

Compiling different TUs depending on these flags (including use of
``std::hardware_constructive_interference`` or
``std::hardware_destructive_interference``)  with different compilers, macro
definitions, or architecture flags will lead to ODR violations and should be
avoided.

``#embed`` Parameters
=====================

``clang::offset``
-----------------
The ``clang::offset`` embed parameter may appear zero or one time in the
embed parameter sequence. Its preprocessor argument clause shall be present and
have the form:

..code-block: text

  ( constant-expression )

and shall be an integer constant expression. The integer constant expression
shall not evaluate to a value less than 0. The token ``defined`` shall not
appear within the constant expression.

The offset will be used when reading the contents of the embedded resource to
specify the starting offset to begin embedding from. The resources is treated
as being empty if the specified offset is larger than the number of bytes in
the resource. The offset will be applied *before* any ``limit`` parameters are
applied.
