=============================
HLSL Availability Diagnostics
=============================

.. contents::
   :local:

Introduction
============

HLSL availability diagnostics emits errors or warning when unavailable shader APIs are used. Unavailable shader APIs are APIs that are exposed in HLSL code but are not available in the target shader stage or shader model version.

There are three modes of HLSL availability diagnostic:

#. **Default mode** - compiler emits an error when an unavailable API is found in a code that is reachable from the shader entry point function or from an exported library function (when compiling a shader library)

#. **Relaxed mode** - same as default mode except the compiler emits a warning. This mode is enabled by ``-Wno-error=hlsl-availability``.

#. **Strict mode** - compiler emits an error when an unavailable API is found in parsed code regardless of whether it can be reached from the shader entry point or exported functions, or not. This mode is enabled by ``-fhlsl-strict-availability``.

Implementation Details
======================

Environment Parameter
---------------------

In order to encode API availability based on the shader model version and shader model stage a new ``environment`` parameter was added to the existing Clang ``availability`` attribute.

The values allowed for this parameter are a subset of values allowed as the ``llvm::Triple`` environment component. If the environment parameters is present, the declared availability attribute applies only to targets with the same platform and environment.

Default and Relaxed Diagnostic Modes
------------------------------------

This mode is implemented in ``DiagnoseHLSLAvailability`` class in ``SemaHLSL.cpp`` and it is invoked after the whole translation unit is parsed (from ``Sema::ActOnEndOfTranslationUnit``). The implementation iterates over all shader entry points and exported library functions in the translation unit and performs an AST traversal of each function body.

When a reference to another function or member method is found (``DeclRefExpr`` or ``MemberExpr``) and it has a body, the AST of the referenced function is also scanned. This chain of AST traversals will reach all of the code that is reachable from the initial shader entry point or exported library function and avoids the need to generate a call graph.

All shader APIs have an availability attribute that specifies the shader model version (and environment, if applicable) when this API was first introduced.When a reference to a function without a definition is found and it has an availability attribute, the version of the attribute is checked against the target shader model version and shader stage (if shader stage context is known), and an appropriate diagnostic is generated as needed.

All shader entry functions have ``HLSLShaderAttr`` attribute that specifies what type of shader this function represents. However, for exported library functions the target shader stage is unknown, so in this case the HLSL API availability will be only checked against the shader model version. It means that for exported library functions the diagnostic of APIs with availability specific to shader stage will be deferred until DXIL linking time.

A list of functions that were already scanned is kept in order to avoid duplicate scans and diagnostics (see ``DiagnoseHLSLAvailability::ScannedDecls``). It might happen that a shader library has multiple shader entry points for different shader stages that all call into the same shared function. It is therefore important to record not just that a function has been scanned, but also in which shader stage context. This is done by using ``llvm::DenseMap`` that maps ``FunctionDecl *`` to a ``unsigned`` bitmap that represents a set of shader stages (or environments) the function has been scanned for. The ``N``'th bit in the set is set if the function has been scanned in shader environment whose ``HLSLShaderAttr::ShaderType`` integer value equals ``N``.

The emitted diagnostic messages belong to ``hlsl-availability`` diagnostic group and are reported as errors by default. With ``-Wno-error=hlsl-availability`` flag they become warning, making it relaxed HLSL diagnostics mode.

Strict Diagnostic Mode
----------------------

When strict HLSL availability diagnostic mode is enabled the compiler must report all HLSL API availability issues regardless of code reachability. The implementation of this mode takes advantage of an existing diagnostic scan in ``DiagnoseUnguardedAvailability`` class which is already traversing AST of each function as soon as the function body has been parsed. For HLSL, this pass was only slightly modified, such as making sure diagnostic messages are in the ``hlsl-availability`` group and that availability checks based on shader stage are not included if the shader stage context is unknown.

If the compilation target is a shader library, only availability based on shader model version can be diagnosed during this scan. To diagnose availability based on shader stage, the compiler needs to run the AST traversals implementated in ``DiagnoseHLSLAvailability`` at the end of the translation unit as described above.

As a result, availability based on specific shader stage will only be diagnosed in code that is reachable from a shader entry point or library export function. It also means that function bodies might be scanned multiple time. When that happens, care should be taken not to produce duplicated diagnostics.

Examples
========

**Note**
For the example below, the ``WaveActiveCountBits`` API function became available in shader model 6.0 and ``WaveMultiPrefixSum`` in shader model 6.5.

The availability of ``ddx`` function depends on a shader stage. It is available for pixel shaders in shader model 2.1 and higher, for compute, mesh and amplification shaders in shader model 6.6 and higher. For any other shader stages it is not available.

Compute shader example
----------------------

.. code-block:: c++

   float unusedFunction(float f) {
     return ddx(f);
   }

   [numthreads(4, 4, 1)]
   void main(uint3 threadId : SV_DispatchThreadId) {
     float f1 = ddx(threadId.x);
     float f2 = WaveActiveCountBits(threadId.y == 1.0);
   }

When compiled as compute shader for shader model version 5.0, Clang will emit the following error by default:

.. code-block:: console

   <>:7:13: error: 'ddx' is only available in compute shader environment on Shader Model 6.6 or newer
   <>:8:13: error: 'WaveActiveCountBits' is only available on Shader Model 6.5 or newer

With relaxed diagnostic mode this errors will become warnings.

With strict diagnostic mode, in addition to the 2 errors above Clang will also emit error for the ``ddx`` call in ``unusedFunction``.:

.. code-block:: console

   <>:2:9: error: 'ddx' is only available in compute shader environment on Shader Model 6.5 or newer
   <>:7:13: error: 'ddx' is only available in compute shader environment on Shader Model 6.5 or newer
   <>:7:13: error: 'WaveActiveCountBits' is only available on Shader Model 6.5 or newer

Shader library example
----------------------

.. code-block:: c++

   float myFunction(float f) {
     return ddx(f);
   }

   float unusedFunction(float f) {
     return WaveMultiPrefixSum(f, 1.0);
   }

   [shader("compute")]
   [numthreads(4, 4, 1)]
   void main(uint3 threadId : SV_DispatchThreadId) {
      float f = 3;
      float e = myFunction(f);
   }

   [shader("pixel")]
   void main() {
      float f = 3;
      float e = myFunction(f);
   }

When compiled as shader library vshader model version 6.4, Clang will emit the following error by default:

.. code-block:: console

   <>:2:9: error: 'ddx' is only available in compute shader environment on Shader Model 6.5 or newer

With relaxed diagnostic mode this errors will become warnings.

With strict diagnostic mode Clang will also emit errors for availability issues in code that is not used by any of the entry points:

.. code-block:: console

   <>2:9: error: 'ddx' is only available in compute shader environment on Shader Model 6.6 or newer
   <>:6:9: error: 'WaveActiveCountBits' is only available on Shader Model 6.5 or newer

Note that ``myFunction`` is reachable from both pixel and compute shader entry points is therefore scanned twice - once for each context. The diagnostic is emitted only for the compute shader context.
