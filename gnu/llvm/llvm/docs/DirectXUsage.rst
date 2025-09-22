=================================
User Guide for the DirectX Target
=================================

.. warning::
   Disclaimer: The DirectX backend is experimental and under active development.
   It is not yet feature complete or ready to be used outside of experimental or
   demonstration contexts.

.. contents::
   :local:

.. toctree::
   :hidden:

   DirectX/DXContainer
   DirectX/DXILArchitecture
   DirectX/DXILOpTableGenDesign
   DirectX/DXILResources

Introduction
============

The DirectX target implements the DirectX programmability interfaces. These
interfaces are documented in the `DirectX Specifications. <https://github.com/Microsoft/DirectX-Specs>`_

Initially the backend is aimed at supporting DirectX 12, and support for DirectX
11 is planned at a later date.

The DirectX backend is currently experimental and is not shipped with any
release builds of LLVM tools. To enable building the DirectX backend locally add
``DirectX`` to the ``LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`` CMake option. For more
information on building LLVM see the :doc:`CMake` documentation.

.. _dx-target-triples:

Target Triples
==============

At present the DirectX target only supports the ``dxil`` architecture, which
generates code for the
`DirectX Intermediate Language. <https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst>`_

In addition to target architecture, the DirectX backend also needs to know the
target runtime version and pipeline stage. These are expressed using the OS and
Environment triple component.

Presently the DirectX backend requires targeting the ``shadermodel`` OS, and
supports versions 6.0+ (at time of writing the latest announced version is 6.7).

.. table:: DirectX Environments

     ================== ========================================================
     Environment         Description
     ================== ========================================================
     ``pixel``           Pixel shader
     ``vertex``          Vertex shader
     ``geometry``        Geometry shader
     ``hull``            Hull shader (tesselation)
     ``domain``          Domain shader (tesselation)
     ``compute``         Compute kernel
     ``library``         Linkable ``dxil`` library
     ``raygeneration``   Ray generation (ray tracing)
     ``intersection``    Ray intersection (ray tracing)
     ``anyhit``          Ray any collision (ray tracing)
     ``closesthit``      Ray closest collision (ray tracing)
     ``miss``            Ray miss (ray tracing)
     ``callable``        Callable shader (ray tracing)
     ``mesh``            Mesh shader
     ``amplification``   Amplification shader
     ================== ========================================================

Output Binaries
===============

The DirectX runtime APIs read a file format based on the
`DirectX Specification. <https://github.com/Microsoft/DirectX-Specs>`_. In
different codebases the file format is referred to by different names
(specifically ``DXBC`` and ``DXILContainer``). Since the format is used to store
both ``DXBC`` and ``DXIL`` outputs, and the ultimate goal is to support both as
code generation targets in LLVM, the LLVM codebase uses a more neutral name,
``DXContainer``.

The ``DXContainer`` format is sparsely documented in the functional
specification, but a reference implementation exists in the
`DirectXShaderCompiler. <https://github.com/microsoft/DirectXShaderCompiler>`_.
The format is documented in the LLVM project docs as well (see
:doc:`DirectX/DXContainer`).

Support for generating ``DXContainer`` files in LLVM, is being added to the LLVM
MC layer for object streamers and writers, and to the Object and ObjectYAML
libraries for testing and object file tooling.

For ``dxil`` targeting, bitcode emission into ``DXContainer`` files follows a
similar model to the ``-fembed-bitcode`` flag supported by clang for other
targets.
