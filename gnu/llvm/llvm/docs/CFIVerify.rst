==============================================
Control Flow Verification Tool Design Document
==============================================

.. contents::
   :local:

Objective
=========

This document provides an overview of an external tool to verify the protection
mechanisms implemented by Clang's *Control Flow Integrity* (CFI) schemes
(``-fsanitize=cfi``). This tool, provided a binary or DSO, should infer whether
indirect control flow operations are protected by CFI, and should output these
results in a human-readable form.

This tool should also be added as part of Clang's continuous integration testing
framework, where modifications to the compiler ensure that CFI protection
schemes are still present in the final binary.

Location
========

This tool will be present as a part of the LLVM toolchain, and will reside in
the "/llvm/tools/llvm-cfi-verify" directory, relative to the LLVM trunk. It will
be tested in two methods:

- Unit tests to validate code sections, present in
  "/llvm/unittests/tools/llvm-cfi-verify".
- Integration tests, present in "/llvm/tools/clang/test/LLVMCFIVerify". These
  integration tests are part of clang as part of a continuous integration
  framework, ensuring updates to the compiler that reduce CFI coverage on
  indirect control flow instructions are identified.

Background
==========

This tool will continuously validate that CFI directives are properly
implemented around all indirect control flows by analysing the output machine
code. The analysis of machine code is important as it ensures that any bugs
present in linker or compiler do not subvert CFI protections in the final
shipped binary.

Unprotected indirect control flow instructions will be flagged for manual
review. These unexpected control flows may simply have not been accounted for in
the compiler implementation of CFI (e.g. indirect jumps to facilitate switch
statements may not be fully protected).

It may be possible in the future to extend this tool to flag unnecessary CFI
directives (e.g. CFI directives around a static call to a non-polymorphic base
type). This type of directive has no security implications, but may present
performance impacts.

Design Ideas
============

This tool will disassemble binaries and DSO's from their machine code format and
analyse the disassembled machine code. The tool will inspect virtual calls and
indirect function calls. This tool will also inspect indirect jumps, as inlined
functions and jump tables should also be subject to CFI protections. Non-virtual
calls (``-fsanitize=cfi-nvcall``) and cast checks (``-fsanitize=cfi-*cast*``)
are not implemented due to a lack of information provided by the bytecode.

The tool would operate by searching for indirect control flow instructions in
the disassembly. A control flow graph would be generated from a small buffer of
the instructions surrounding the 'target' control flow instruction. If the
target instruction is branched-to, the fallthrough of the branch should be the
CFI trap (on x86, this is a ``ud2`` instruction). If the target instruction is
the fallthrough (i.e. immediately succeeds) of a conditional jump, the
conditional jump target should be the CFI trap. If an indirect control flow
instruction does not conform to one of these formats, the target will be noted
as being CFI-unprotected.

Note that in the second case outlined above (where the target instruction is the
fallthrough of a conditional jump), if the target represents a vcall that takes
arguments, these arguments may be pushed to the stack after the branch but
before the target instruction. In these cases, a secondary 'spill graph' in
constructed, to ensure the register argument used by the indirect jump/call is
not spilled from the stack at any point in the interim period. If there are no
spills that affect the target register, the target is marked as CFI-protected.

Other Design Notes
~~~~~~~~~~~~~~~~~~

Only machine code sections that are marked as executable will be subject to this
analysis. Non-executable sections do not require analysis as any execution
present in these sections has already violated the control flow integrity.

Suitable extensions may be made at a later date to include analysis for indirect
control flow operations across DSO boundaries. Currently, these CFI features are
only experimental with an unstable ABI, making them unsuitable for analysis.

The tool currently only supports the x86, x86_64, and AArch64 architectures.
