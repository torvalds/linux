=======================================================================
Building a JIT: Extreme Laziness - Using LazyReexports to JIT from ASTs
=======================================================================

.. contents::
   :local:

**This tutorial is under active development. It is incomplete and details may
change frequently.** Nonetheless we invite you to try it out as it stands, and
we welcome any feedback.

Chapter 4 Introduction
======================

Welcome to Chapter 4 of the "Building an ORC-based JIT in LLVM" tutorial. This
chapter introduces custom MaterializationUnits and Layers, and the lazy
reexports API. Together these will be used to replace the CompileOnDemandLayer
from `Chapter 3 <BuildingAJIT3.html>`_ with a custom lazy-JITing scheme that JITs
directly from Kaleidoscope ASTs.

**To be done:**

**(1) Describe the drawbacks of JITing from IR (have to compile to IR first,
which reduces the benefits of laziness).**

**(2) Describe CompileCallbackManagers and IndirectStubManagers in detail.**

**(3) Run through the implementation of addFunctionAST.**

Full Code Listing
=================

Here is the complete code listing for our running example that JITs lazily from
Kaleidoscope ASTS. To build this example, use:

.. code-block:: bash

    # Compile
    clang++ -g toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o toy
    # Run
    ./toy

Here is the code:

.. literalinclude:: ../../examples/Kaleidoscope/BuildingAJIT/Chapter4/KaleidoscopeJIT.h
   :language: c++

`Next: Remote-JITing -- Process-isolation and laziness-at-a-distance <BuildingAJIT5.html>`_
