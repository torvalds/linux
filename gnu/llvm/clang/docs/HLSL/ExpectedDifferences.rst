===================================
Expected Differences vs DXC and FXC
===================================

.. contents::
   :local:

Introduction
============

HLSL currently has two reference compilers, the `DirectX Shader Compiler (DXC)
<https://github.com/microsoft/DirectXShaderCompiler/>`_ and the
`Effect-Compiler (FXC) <https://learn.microsoft.com/en-us/windows/win32/direct3dtools/fxc>`_.
The two reference compilers do not fully agree. Some known disagreements in the
references are tracked on
`DXC's GitHub
<https://github.com/microsoft/DirectXShaderCompiler/issues?q=is%3Aopen+is%3Aissue+label%3Afxc-disagrees>`_,
but many more are known to exist.

HLSL as implemented by Clang will also not fully match either of the reference
implementations, it is instead being written to match the `draft language
specification <https://microsoft.github.io/hlsl-specs/specs/hlsl.pdf>`_.

This document is a non-exhaustive collection the known differences between
Clang's implementation of HLSL and the existing reference compilers.

General Principles
------------------

Most of the intended differences between Clang and the earlier reference
compilers are focused on increased consistency and correctness. Both reference
compilers do not always apply language rules the same in all contexts.

Clang also deviates from the reference compilers by providing different
diagnostics, both in terms of the textual messages and the contexts in which
diagnostics are produced. While striving for a high level of source
compatibility with conforming HLSL code, Clang may produce earlier and more
robust diagnostics for incorrect code or reject code that a reference compiler
incorrectly accepted.

Language Version
================

Clang targets language compatibility for HLSL 2021 as implemented by DXC.
Language features that were removed in earlier versions of HLSL may be added on
a case-by-case basis, but are not planned for the initial implementation.

Overload Resolution
===================

Clang's HLSL implementation adopts C++ overload resolution rules as proposed for
HLSL 202x based on proposal
`0007 <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0007-const-instance-methods.md>`_
and
`0008 <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0008-non-member-operator-overloading.md>`_.

Clang's implementation extends standard overload resolution rules to HLSL
library functionality. This causes subtle changes in overload resolution
behavior between Clang and DXC. Some examples include:

.. code-block:: c++

  void halfOrInt16(half H);
  void halfOrInt16(uint16_t U);
  void halfOrInt16(int16_t I);

  void takesDoubles(double, double, double);

  cbuffer CB {
    bool B;
    uint U;
    int I;
    float X, Y, Z;
    double3 A, B;
  }

  void twoParams(int, int);
  void twoParams(float, float);

  export void call() {
    halfOrInt16(U); // DXC: Fails with call ambiguous between int16_t and uint16_t overloads
                    // Clang: Resolves to halfOrInt16(uint16_t).
    halfOrInt16(I); // All: Resolves to halfOrInt16(int16_t).
    half H;
  #ifndef IGNORE_ERRORS
    // asfloat16 is a builtin with overloads for half, int16_t, and uint16_t.
    H = asfloat16(I); // DXC: Fails to resolve overload for int.
                      // Clang: Resolves to asfloat16(int16_t).
    H = asfloat16(U); // DXC: Fails to resolve overload for int.
                      // Clang: Resolves to asfloat16(uint16_t).
  #endif
    H = asfloat16(0x01); // DXC: Resolves to asfloat16(half).
                         // Clang: Resolves to asfloat16(uint16_t).

    takesDoubles(X, Y, Z); // Works on all compilers
  #ifndef IGNORE_ERRORS
    fma(X, Y, Z); // DXC: Fails to resolve no known conversion from float to double.
                  // Clang: Resolves to fma(double,double,double).
  #endif

    double D = dot(A, B); // DXC: Resolves to dot(double3, double3), fails DXIL Validation.
                          // FXC: Expands to compute double dot product with fmul/fadd
                          // Clang: Resolves to dot(float3, float3), emits conversion warnings.

  #ifndef IGNORE_ERRORS
    tan(B); // DXC: resolves to tan(float).
            // Clang: Fails to resolve, ambiguous between integer types.

    twoParams(I, X); // DXC: resolves twoParams(int, int).
                     // Clang: Fails to resolve ambiguous conversions.
  #endif
  }

.. note::

  In Clang, a conscious decision was made to exclude the ``dot(vector<double,N>, vector<double,N>)``
  overload and allow overload resolution to resolve the
  ``vector<float,N>`` overload. This approach provides ``-Wconversion``
  diagnostic notifying the user of the conversion rather than silently altering
  precision relative to the other overloads (as FXC does) or generating code
  that will fail validation (as DXC does).
