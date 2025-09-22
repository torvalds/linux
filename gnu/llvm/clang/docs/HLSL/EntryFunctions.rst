====================
HLSL Entry Functions
====================

.. contents::
   :local:

Usage
=====

In HLSL, entry functions denote the starting point for shader execution. They
must be known at compile time. For all non-library shaders, the compiler assumes
the default entry function name ``main``, unless the DXC ``/E`` option is
provided to specify an alternate entry point. For library shaders entry points
are denoted using the ``[shader(...)]`` attribute.

All scalar parameters to entry functions must have semantic annotations, and all
struct parameters must have semantic annotations on every field in the struct
declaration. Additionally if the entry function has a return type, a semantic
annotation must be provided for the return type as well.

HLSL entry functions can be called from other parts of the shader, which has
implications on code generation.

Implementation Details
======================

In Clang, the DXC ``/E`` option is translated to the cc1 flag ``-hlsl-entry``,
which in turn applies the ``HLSLShader`` attribute to the function with the
specified name. This allows code generation for entry functions to always key
off the presence of the ``HLSLShader`` attribute, regardless of what shader
profile you are compiling.

In code generation, two functions are generated. One is the user defined
function, which is code generated as a mangled C++ function with internal
linkage following normal function code generation.

The actual exported entry function which can be called by the GPU driver is a
``void(void)`` function that isn't name mangled. In code generation we generate
the unmangled entry function to serve as the actual shader entry. The shader
entry function is annotated with the ``hlsl.shader`` function attribute
identifying the entry's pipeline stage.

The body of the unmangled entry function contains first a call to execute global
constructors, then instantiations of the user-defined entry parameters with
their semantic values populated, and a call to the user-defined function.
After the call instruction the return value (if any) is saved using a
target-appropriate intrinsic for storing outputs (for DirectX, the
``llvm.dx.store.output``). Lastly, any present global destructors will be called
immediately before the return. HLSL does not support C++ ``atexit``
registrations, instead calls to global destructors are compile-time generated.

.. note::

   HLSL support in Clang is currently focused on compute shaders, which do not
   support output semantics. Support for output semantics will not be
   implemented until other shader profiles are supported.

Below is example IR that represents the planned implementation, subject to
change as the ``llvm.dx.store.output`` and ``llvm.dx.load.input`` intrinsics are
not yet implemented.

.. code-block:: none

   ; Function Attrs: norecurse
   define void @main() #1 {
      entry:
      %0 = call i32 @llvm.dx.load.input.i32(...)
      %1 = call i32 @"?main@@YAXII@Z"(i32 %0)
      call @llvm.dx.store.output.i32(%1, ...)
      ret void
   }

