===============================================
Architecture and Design of DXIL Support in LLVM
===============================================

.. contents::
   :local:

.. toctree::
   :hidden:

Introduction
============

LLVM supports reading and writing the `DirectX Intermediate Language.
<https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst>`_,
or DXIL. DXIL is essentially LLVM 3.7 era bitcode with some
restrictions and various semantically important operations and
metadata.

LLVM's implementation philosophy for DXIL support is to treat DXIL as
merely a representation format as much as possible. When reading DXIL,
we should translate everything to generic LLVM constructs when
possible. Similarly, we should introduce DXIL-specific constructs as
late as possible in the process of lowering to the format.

There are three places to look for DXIL related code in LLVM: The
`DirectX` backend, for writing DXIL; The `DXILUpgrade` pass, for
reading; and in library code that is shared between writing and
reading. We'll describe these in reverse order.

Common Code for Reading and Writing
===================================

There's quite a bit of logic that needs to be shared between reading
and writing DXIL in order to avoid code duplication. While we don't
have a hard and fast rule about where such code should live, there are
generally three sensible places. Simple definitions of enums and
values that must stay fixed to match DXIL's ABI can be found in
`Support/DXILABI.h`, utilities to translate bidirectionally between
DXIL and modern LLVM constructs live in `lib/Transforms/Utils`, and
more analyses that are needed to derive or preserve information are
implemented as typical `lib/Analysis` passes.

The DXILUpgrade Pass
====================

Translating DXIL to LLVM IR takes advantage of the fact that DXIL is
compatible with LLVM 3.7 bitcode, and that modern LLVM is capable of
"upgrading" older bitcode into modern IR. Simply relying on the
bitcode upgrade process isn't sufficient though, since that leaves a
number of DXIL specific constructs around. Thus, we have the
`DXILUpgrade` pass to transform DXIL operations to LLVM operations and
smooth over differences in metadata representation. We call this pass
"upgrade" to reflect that it follows LLVM's standard bitcode upgrade
process and simply finishes the job for DXIL constructs - while
"reader" or "lifting" might also be reasonable names, they could be a
bit misleading.

The `DXILUpgrade` pass itself is fairly lightweight. It mostly relies
on the utilities described in "Common Code" above in order to share
logic with both the DirectX backend and with Clang's codegen of HLSL
support as much as possible.

The DirectX Intrinsic Expansion Pass
====================================
There are intrinsics that don't map directly to DXIL Ops. In some cases
an intrinsic needs to be expanded to a set of LLVM IR instructions. In
other cases an intrinsic needs modifications to the arguments or return
values of a DXIL Op. The `DXILIntrinsicExpansion` pass handles all 
the cases where our intrinsics don't have a one to one mapping. This 
pass may also be used when the expansion is specific to DXIL to keep 
implementation details out of CodeGen. Finally, there is an expectation 
that we maintain vector types through this pass. Therefore, best 
practice would be to avoid scalarization in this pass.


The DirectX Backend
===================

The DirectX backend lowers LLVM IR into DXIL. As we're transforming to
an intermediate format rather than a specific ISA, this backend does
not follow the instruction selection patterns you might be familiar
with from other backends. There are two parts to lowering DXIL - a set
of passes that mutate various constructs into a form that matches how
DXIL represents those constructs, followed by a limited bitcode
"downgrader pass".

Before emitting DXIL, the DirectX backend needs to modify the LLVM IR
such that external operations, types, and metadata is represented in
the way that DXIL expects. For example, `DXILOpLowering` translates
intrinsics into `dx.op` calls. These passes are essentially the
inverse of the `DXILUpgrade` pass. It's best to do this downgrading
process as IR to IR passes when possible, as that means that they can
be easily tested with `opt` and `FileCheck` without the need for
external tooling.

The second part of DXIL emission is more or less an LLVM bitcode
downgrader. We need to emit bitcode that matches the LLVM 3.7
representation. For this, we have `DXILWriter`, which is an alternate
version of LLVM's `BitcodeWriter`. At present, this is able to
leverage LLVM's current bitcode libraries to do a lot of the work, but
it's possible that at some point in the future it will need to be
completely separate as modern LLVM bitcode evolves.

DirectX Backend Flow
--------------------

The code generation flow for DXIL is broken into a series of passes. The passes
are grouped into two flows:

#. Generating DXIL IR.
#. Generating DXIL Binary.

The passes to generate DXIL IR follow the flow:

  DXILOpLowering -> DXILPrepare -> DXILTranslateMetadata

Each of these passes has a defined responsibility:

#. DXILOpLowering translates LLVM intrinsic calls to dx.op calls.
#. DXILPrepare transforms the DXIL IR to be compatible with LLVM 3.7, and
   inserts bitcasts to allow typed pointers to be inserted.
#. DXILTranslateMetadata emits the DXIL Metadata structures.

The passes to encode DXIL to binary in the DX Container follow the flow:

  DXILEmbedder -> DXContainerGlobals -> AsmPrinter

Each of these passes have the following defined responsibilities:

#. DXILEmbedder runs the DXIL bitcode writer to generate a bitcode stream and
   embeds the binary data inside a global in the original module.
#. DXContainerGlobals generates binary data globals for the other DX Container
   parts based on computed analysis passes.
#. AsmPrinter is the standard LLVM infrastructure for emitting object files.

When emitting DXIL into a DX Container file the MC layer is used in a similar
way to how the Clang ``-fembed-bitcode`` option operates. The DX Container
object writer knows how to construct the headers and structural fields of the
container, and reads global variables from the module to fill in the remaining
part data.

DirectX Container
-----------------

The DirectX container format is treated in LLVM as an object file format.
Reading is implemented between the BinaryFormat and Object libraries, and
writing is implemented in the MC layer. Additional testing and inspection
support are implemented in the ObjectYAML library and tools.

Testing
=======

A lot of DXIL testing can be done with typical IR to IR tests using
`opt` and `FileCheck`, since a lot of the support is implemented in
terms of IR level passes as described in the previous sections. You
can see examples of this in `llvm/test/CodeGen/DirectX` as well as
`llvm/test/Transforms/DXILUpgrade`, and this type of testing should be
leveraged as much as possible.

However, when it comes to testing the DXIL format itself, IR passes
are insufficient for testing. For now, the best option we have
available is using the DXC project's tools in order to round trip.
These tests are currently found in `test/tools/dxil-dis` and are only
available if the `LLVM_INCLUDE_DXIL_TESTS` cmake option is set. Note
that we do not currently have the equivalent testing set up for the
DXIL reading path.

As soon as we are able, we will also want to round trip using the DXIL
writing and reading paths in order to ensure self consistency and to
get test coverage when `dxil-dis` isn't available.
