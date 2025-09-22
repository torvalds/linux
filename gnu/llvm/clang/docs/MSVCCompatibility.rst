.. raw:: html

  <style type="text/css">
    .none { background-color: #FFCCCC }
    .partial { background-color: #FFFF99 }
    .good { background-color: #CCFF99 }
  </style>

.. role:: none
.. role:: partial
.. role:: good

==================
MSVC compatibility
==================

When Clang compiles C++ code for Windows, it attempts to be compatible with
MSVC.  There are multiple dimensions to compatibility.

First, Clang attempts to be ABI-compatible, meaning that Clang-compiled code
should be able to link against MSVC-compiled code successfully.  However, C++
ABIs are particularly large and complicated, and Clang's support for MSVC's C++
ABI is a work in progress.  If you don't require MSVC ABI compatibility or don't
want to use Microsoft's C and C++ runtimes, the mingw32 toolchain might be a
better fit for your project.

Second, Clang implements many MSVC language extensions, such as
``__declspec(dllexport)`` and a handful of pragmas.  These are typically
controlled by ``-fms-extensions``.

Third, MSVC accepts some C++ code that Clang will typically diagnose as
invalid.  When these constructs are present in widely included system headers,
Clang attempts to recover and continue compiling the user's program.  Most
parsing and semantic compatibility tweaks are controlled by
``-fms-compatibility`` and ``-fdelayed-template-parsing``, and they are a work
in progress.

Finally, there is :ref:`clang-cl`, a driver program for clang that attempts to
be compatible with MSVC's cl.exe.

ABI features
============

The status of major ABI-impacting C++ features:

* Record layout: :good:`Complete`.  We've tested this with a fuzzer and have
  fixed all known bugs.

* Class inheritance: :good:`Mostly complete`.  This covers all of the standard
  OO features you would expect: virtual method inheritance, multiple
  inheritance, and virtual inheritance.  Every so often we uncover a bug where
  our tables are incompatible, but this is pretty well in hand.  This feature
  has also been fuzz tested.

* Name mangling: :good:`Ongoing`.  Every new C++ feature generally needs its own
  mangling.  For example, member pointer template arguments have an interesting
  and distinct mangling.  Fortunately, incorrect manglings usually do not result
  in runtime errors.  Non-inline functions with incorrect manglings usually
  result in link errors, which are relatively easy to diagnose.  Incorrect
  manglings for inline functions and templates result in multiple copies in the
  final image.  The C++ standard requires that those addresses be equal, but few
  programs rely on this.

* Member pointers: :good:`Mostly complete`.  Standard C++ member pointers are
  fully implemented and should be ABI compatible.  Both `#pragma
  pointers_to_members`_ and the `/vm`_ flags are supported. However, MSVC
  supports an extension to allow creating a `pointer to a member of a virtual
  base class`_.  Clang does not yet support this.

.. _#pragma pointers_to_members:
  https://msdn.microsoft.com/en-us/library/83cch5a6.aspx
.. _/vm: https://msdn.microsoft.com/en-us/library/yad46a6z.aspx
.. _pointer to a member of a virtual base class: https://llvm.org/PR15713

* Debug info: :good:`Mostly complete`.  Clang emits relatively complete CodeView
  debug information if ``/Z7`` or ``/Zi`` is passed. Microsoft's link.exe will
  transform the CodeView debug information into a PDB that works in Windows
  debuggers and other tools that consume PDB files like ETW. Work to teach lld
  about CodeView and PDBs is ongoing.

* RTTI: :good:`Complete`.  Generation of RTTI data structures has been
  finished, along with support for the ``/GR`` flag.

* C++ Exceptions: :good:`Mostly complete`.  Support for
  C++ exceptions (``try`` / ``catch`` / ``throw``) have been implemented for
  x86 and x64.  Our implementation has been well tested but we still get the
  odd bug report now and again.
  C++ exception specifications are ignored, but this is `consistent with Visual
  C++`_.

.. _consistent with Visual C++:
  https://msdn.microsoft.com/en-us/library/wfa0edys.aspx

* Asynchronous Exceptions (SEH): :partial:`Partial`.
  Structured exceptions (``__try`` / ``__except`` / ``__finally``) mostly
  work on x86 and x64.
  LLVM does not model asynchronous exceptions, so it is currently impossible to
  catch an asynchronous exception generated in the same frame as the catching
  ``__try``.

* Thread-safe initialization of local statics: :good:`Complete`.  MSVC 2015
  added support for thread-safe initialization of such variables by taking an
  ABI break.
  We are ABI compatible with both the MSVC 2013 and 2015 ABI for static local
  variables.

* Lambdas: :good:`Mostly complete`.  Clang is compatible with Microsoft's
  implementation of lambdas except for providing overloads for conversion to
  function pointer for different calling conventions.  However, Microsoft's
  extension is non-conforming.

Template instantiation and name lookup
======================================

MSVC allows many invalid constructs in class templates that Clang has
historically rejected.  In order to parse widely distributed headers for
libraries such as the Active Template Library (ATL) and Windows Runtime Library
(WRL), some template rules have been relaxed or extended in Clang on Windows.

The first major semantic difference is that MSVC appears to defer all parsing
an analysis of inline method bodies in class templates until instantiation
time.  By default on Windows, Clang attempts to follow suit.  This behavior is
controlled by the ``-fdelayed-template-parsing`` flag.  While Clang delays
parsing of method bodies, it still parses the bodies *before* template argument
substitution, which is not what MSVC does.  The following compatibility tweaks
are necessary to parse the template in those cases.

MSVC allows some name lookup into dependent base classes.  Even on other
platforms, this has been a `frequently asked question`_ for Clang users.  A
dependent base class is a base class that depends on the value of a template
parameter.  Clang cannot see any of the names inside dependent bases while it
is parsing your template, so the user is sometimes required to use the
``typename`` keyword to assist the parser.  On Windows, Clang attempts to
follow the normal lookup rules, but if lookup fails, it will assume that the
user intended to find the name in a dependent base.  While parsing the
following program, Clang will recover as if the user had written the
commented-out code:

.. _frequently asked question:
  https://clang.llvm.org/compatibility.html#dep_lookup

.. code-block:: c++

  template <typename T>
  struct Foo : T {
    void f() {
      /*typename*/ T::UnknownType x =  /*this->*/unknownMember;
    }
  };

After recovery, Clang warns the user that this code is non-standard and issues
a hint suggesting how to fix the problem.

As of this writing, Clang is able to compile a simple ATL hello world
application.  There are still issues parsing WRL headers for modern Windows 8
apps, but they should be addressed soon.

__forceinline behavior
======================

``__forceinline`` behaves like ``[[clang::always_inline]]``.
Inlining is always attempted regardless of optimization level.

This differs from MSVC where ``__forceinline`` is only respected once inline expansion is enabled
which allows any function marked implicitly or explicitly ``inline`` or ``__forceinline`` to be expanded.
Therefore functions marked ``__forceinline`` will be expanded when the optimization level is ``/Od`` unlike
MSVC where ``__forceinline`` will not be expanded under ``/Od``.

SIMD and instruction set intrinsic behavior
===========================================

Clang follows the GCC model for intrinsics and not the MSVC model.
There are currently no plans to support the MSVC model.

MSVC intrinsics always emit the machine instruction the intrinsic models regardless of the compile time options specified.
For example ``__popcnt`` always emits the x86 popcnt instruction even if the compiler does not have the option enabled to emit popcnt on its own volition.

There are two common cases where code that compiles with MSVC will need reworking to build on clang.
Assume the examples are only built with `-msse2` so we do not have the intrinsics at compile time.

.. code-block:: c++

  unsigned PopCnt(unsigned v) {
    if (HavePopCnt)
      return __popcnt(v);
    else
      return GenericPopCnt(v);
  }

.. code-block:: c++

  __m128 dot4_sse3(__m128 v0, __m128 v1) {
    __m128 r = _mm_mul_ps(v0, v1);
    r = _mm_hadd_ps(r, r);
    r = _mm_hadd_ps(r, r);
    return r;
  }

Clang expects that either you have compile time support for the target features, `-msse3` and `-mpopcnt`, you mark the function with the expected target feature or use runtime detection with an indirect call.

.. code-block:: c++

  __attribute__((__target__("sse3"))) __m128 dot4_sse3(__m128 v0, __m128 v1) {
    __m128 r = _mm_mul_ps(v0, v1);
    r = _mm_hadd_ps(r, r);
    r = _mm_hadd_ps(r, r);
    return r;
  }

The SSE3 dot product can be easily fixed by either building the translation unit with SSE3 support or using `__target__` to compile that specific function with SSE3 support.

.. code-block:: c++

  unsigned PopCnt(unsigned v) {
    if (HavePopCnt)
      return __popcnt(v);
    else
      return GenericPopCnt(v);
  }

The above ``PopCnt`` example must be changed to work with clang. If we mark the function with `__target__("popcnt")` then the compiler is free to emit popcnt at will which we do not want. While this isn't a concern in our small example it is a concern in larger functions with surrounding code around the intrinsics. Similar reasoning for compiling the translation unit with `-mpopcnt`.
We must split each branch into its own function that can be called indirectly instead of using the intrinsic directly.

.. code-block:: c++

  __attribute__((__target__("popcnt"))) unsigned hwPopCnt(unsigned v) { return __popcnt(v); }
  unsigned (*PopCnt)(unsigned) = HavePopCnt ? hwPopCnt : GenericPopCnt;

.. code-block:: c++

  __attribute__((__target__("popcnt"))) unsigned hwPopCnt(unsigned v) { return __popcnt(v); }
  unsigned PopCnt(unsigned v) {
    if (HavePopCnt)
      return hwPopCnt(v);
    else
      return GenericPopCnt(v);
  }

In the above example ``hwPopCnt`` will not be inlined into ``PopCnt`` since ``PopCnt`` doesn't have the popcnt target feature.
With a larger function that does real work the function call overhead is negligible. However in our popcnt example there is the function call
overhead. There is no analog for this specific MSVC behavior in clang.

For clang we effectively have to create the dispatch function ourselves to each specfic implementation.

SIMD vector types
=================

Clang's simd vector types are builtin types and not user defined types as in MSVC. This does have some observable behavior changes.
We will look at the x86 `__m128` type for the examples below but the statements apply to all vector types including ARM's `float32x4_t`.

There are no members that can be accessed on the vector types. Vector types are not structs in clang.
You cannot use ``__m128.m128_f32[0]`` to access the first element of the `__m128`.
This also means struct initialization like ``__m128{ { 0.0f, 0.0f, 0.0f, 0.0f } }`` will not compile with clang.

Since vector types are builtin types, clang implements operators on them natively.

.. code-block:: c++

  #ifdef _MSC_VER
  __m128 operator+(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
  #endif

The above code will fail to compile since overloaded 'operator+' must have at least one parameter of class or enumeration type.
You will need to fix such code to have the check ``#if defined(_MSC_VER) && !defined(__clang__)``.

Since `__m128` is not a class type in clang any overloads after a template definition will not be considered.

.. code-block:: c++

  template<class T>
  void foo(T) {}

  template<class T>
  void bar(T t) {
    foo(t);
  }

  void foo(__m128) {}

  int main() {
    bar(_mm_setzero_ps());
  }

With MSVC ``foo(__m128)`` will be selected but with clang ``foo<__m128>()`` will be selected since on clang `__m128` is a builtin type.

In general the takeaway is `__m128` is a builtin type on clang while a class type on MSVC.
