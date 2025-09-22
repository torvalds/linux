Reference
=========

LLVM and API reference documentation.

.. contents::
   :local:

.. toctree::
   :hidden:

   Atomics
   BitCodeFormat
   BlockFrequencyTerminology
   BranchWeightMetadata
   Bugpoint
   CommandGuide/index
   ConvergenceAndUniformity
   ConvergentOperations
   Coroutines
   DependenceGraphs/index
   ExceptionHandling
   Extensions
   FaultMaps
   FuzzingLLVM
   GarbageCollection
   GetElementPtr
   GlobalISel/index
   GwpAsan
   HowToSetUpLLVMStyleRTTI
   HowToUseAttributes
   InAlloca
   LangRef
   LibFuzzer
   MarkedUpDisassembly
   MIRLangRef
   OptBisect
   PCSectionsMetadata
   PDB/index
   PointerAuth
   ScudoHardenedAllocator
   MemoryModelRelaxationAnnotations
   MemTagSanitizer
   Security
   SecurityTransparencyReports
   SegmentedStacks
   StackMaps
   SpeculativeLoadHardening
   Statepoints
   SymbolizerMarkupFormat
   SystemLibrary
   TestingGuide
   TransformMetadata
   TypeMetadata
   XRay
   XRayExample
   XRayFDRFormat
   YamlIO

API Reference
-------------

`Doxygen generated documentation <https://llvm.org/doxygen/>`_
  (`classes <https://llvm.org/doxygen/inherits.html>`_)

:doc:`HowToUseAttributes`
  Answers some questions about the new Attributes infrastructure.

LLVM Reference
--------------

======================
Command Line Utilities
======================

:doc:`LLVM Command Guide <CommandGuide/index>`
   A reference manual for the LLVM command line utilities ("man" pages for LLVM
   tools).

:doc:`Bugpoint`
   Automatic bug finder and test-case reducer description and usage
   information.

:doc:`OptBisect`
  A command line option for debugging optimization-induced failures.

:doc:`SymbolizerMarkupFormat`
  A reference for the log symbolizer markup accepted by ``llvm-symbolizer``.

:doc:`The Microsoft PDB File Format <PDB/index>`
  A detailed description of the Microsoft PDB (Program Database) file format.

==================
Garbage Collection
==================

:doc:`GarbageCollection`
   The interfaces source-language compilers should use for compiling GC'd
   programs.

:doc:`Statepoints`
  This describes a set of experimental extensions for garbage
  collection support.

=========
LibFuzzer
=========

:doc:`LibFuzzer`
  A library for writing in-process guided fuzzers.

:doc:`FuzzingLLVM`
  Information on writing and using Fuzzers to find bugs in LLVM.

========
LLVM IR
========

:doc:`LLVM Language Reference Manual <LangRef>`
  Defines the LLVM intermediate representation and the assembly form of the
  different nodes.

:doc:`InAlloca`
  Description of the ``inalloca`` argument attribute.

:doc:`BitCodeFormat`
   This describes the file format and encoding used for LLVM "bc" files.

:doc:`Machine IR (MIR) Format Reference Manual <MIRLangRef>`
   A reference manual for the MIR serialization format, which is used to test
   LLVM's code generation passes.

:doc:`GlobalISel/index`
  This describes the prototype instruction selection replacement, GlobalISel.

:doc:`ConvergentOperations`
  Description of ``convergent`` operation semantics and related intrinsics.

=====================
Testing and Debugging
=====================

:doc:`LLVM Testing Infrastructure Guide <TestingGuide>`
   A reference manual for using the LLVM testing infrastructure.

:doc:`TestSuiteGuide`
  Describes how to compile and run the test-suite benchmarks.


:doc:`GwpAsan`
  A sampled heap memory error detection toolkit designed for production use.

====
XRay
====

:doc:`XRay`
  High-level documentation of how to use XRay in LLVM.

:doc:`XRayExample`
  An example of how to debug an application with XRay.

=================
Additional Topics
=================

:doc:`FaultMaps`
  LLVM support for folding control flow into faulting machine instructions.

:doc:`Atomics`
  Information about LLVM's concurrency model.

:doc:`ExceptionHandling`
   This document describes the design and implementation of exception handling
   in LLVM.

:doc:`Extensions`
  LLVM-specific extensions to tools and formats LLVM seeks compatibility with.

:doc:`HowToSetUpLLVMStyleRTTI`
  How to make ``isa<>``, ``dyn_cast<>``, etc. available for clients of your
  class hierarchy.

:doc:`BlockFrequencyTerminology`
   Provides information about terminology used in the ``BlockFrequencyInfo``
   analysis pass.

:doc:`BranchWeightMetadata`
   Provides information about Branch Prediction Information.

:doc:`GetElementPtr`
  Answers to some very frequent questions about LLVM's most frequently
  misunderstood instruction.

:doc:`ScudoHardenedAllocator`
  A library that implements a security-hardened `malloc()`.

:doc:`MemoryModelRelaxationAnnotations`
  Target-defined relaxation to LLVM's concurrency model.

:doc:`MemTagSanitizer`
  Security hardening for production code aiming to mitigate memory
  related vulnerabilities. Based on the Armv8.5-A Memory Tagging Extension.

:doc:`Dependence Graphs <DependenceGraphs/index>`
  A description of the design of the various dependence graphs such as
  the DDG (Data Dependence Graph).

:doc:`SpeculativeLoadHardening`
  A description of the Speculative Load Hardening mitigation for Spectre v1.

:doc:`SegmentedStacks`
   This document describes segmented stacks and how they are used in LLVM.

:doc:`MarkedUpDisassembly`
   This document describes the optional rich disassembly output syntax.

:doc:`StackMaps`
  LLVM support for mapping instruction addresses to the location of
  values and allowing code to be patched.

:doc:`Coroutines`
  LLVM support for coroutines.

:doc:`PointerAuth`
  A description of pointer authentication, its LLVM IR representation, and its
  support in the backend.

:doc:`YamlIO`
   A reference guide for using LLVM's YAML I/O library.

:doc:`ConvergenceAndUniformity`
   A description of uniformity analysis in the presence of irreducible
   control flow, and its implementation.
