//===-- X86.h - Top-level interface for X86 representation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the x86
// target library, as used by the LLVM JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86_H
#define LLVM_LIB_TARGET_X86_X86_H

#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class InstructionSelector;
class PassRegistry;
class X86RegisterBankInfo;
class X86Subtarget;
class X86TargetMachine;

/// This pass converts a legalized DAG into a X86-specific DAG, ready for
/// instruction scheduling.
FunctionPass *createX86ISelDag(X86TargetMachine &TM, CodeGenOptLevel OptLevel);

/// This pass initializes a global base register for PIC on x86-32.
FunctionPass *createX86GlobalBaseRegPass();

/// This pass combines multiple accesses to local-dynamic TLS variables so that
/// the TLS base address for the module is only fetched once per execution path
/// through the function.
FunctionPass *createCleanupLocalDynamicTLSPass();

/// This function returns a pass which converts floating-point register
/// references and pseudo instructions into floating-point stack references and
/// physical instructions.
FunctionPass *createX86FloatingPointStackifierPass();

/// This pass inserts AVX vzeroupper instructions before each call to avoid
/// transition penalty between functions encoded with AVX and SSE.
FunctionPass *createX86IssueVZeroUpperPass();

/// This pass inserts ENDBR instructions before indirect jump/call
/// destinations as part of CET IBT mechanism.
FunctionPass *createX86IndirectBranchTrackingPass();

/// Return a pass that pads short functions with NOOPs.
/// This will prevent a stall when returning on the Atom.
FunctionPass *createX86PadShortFunctions();

/// Return a pass that selectively replaces certain instructions (like add,
/// sub, inc, dec, some shifts, and some multiplies) by equivalent LEA
/// instructions, in order to eliminate execution delays in some processors.
FunctionPass *createX86FixupLEAs();

/// Return a pass that replaces equivalent slower instructions with faster
/// ones.
FunctionPass *createX86FixupInstTuning();

/// Return a pass that reduces the size of vector constant pool loads.
FunctionPass *createX86FixupVectorConstants();

/// Return a pass that removes redundant LEA instructions and redundant address
/// recalculations.
FunctionPass *createX86OptimizeLEAs();

/// Return a pass that transforms setcc + movzx pairs into xor + setcc.
FunctionPass *createX86FixupSetCC();

/// Return a pass that transform inline buffer security check into seperate bb
FunctionPass *createX86WinFixupBufferSecurityCheckPass();

/// Return a pass that avoids creating store forward block issues in the hardware.
FunctionPass *createX86AvoidStoreForwardingBlocks();

/// Return a pass that lowers EFLAGS copy pseudo instructions.
FunctionPass *createX86FlagsCopyLoweringPass();

/// Return a pass that expands DynAlloca pseudo-instructions.
FunctionPass *createX86DynAllocaExpander();

/// Return a pass that config the tile registers.
FunctionPass *createX86TileConfigPass();

/// Return a pass that preconfig the tile registers before fast reg allocation.
FunctionPass *createX86FastPreTileConfigPass();

/// Return a pass that config the tile registers after fast reg allocation.
FunctionPass *createX86FastTileConfigPass();

/// Return a pass that insert pseudo tile config instruction.
FunctionPass *createX86PreTileConfigPass();

/// Return a pass that lower the tile copy instruction.
FunctionPass *createX86LowerTileCopyPass();

/// Return a pass that inserts int3 at the end of the function if it ends with a
/// CALL instruction. The pass does the same for each funclet as well. This
/// ensures that the open interval of function start and end PCs contains all
/// return addresses for the benefit of the Windows x64 unwinder.
FunctionPass *createX86AvoidTrailingCallPass();

/// Return a pass that optimizes the code-size of x86 call sequences. This is
/// done by replacing esp-relative movs with pushes.
FunctionPass *createX86CallFrameOptimization();

/// Return an IR pass that inserts EH registration stack objects and explicit
/// EH state updates. This pass must run after EH preparation, which does
/// Windows-specific but architecture-neutral preparation.
FunctionPass *createX86WinEHStatePass();

/// Return a Machine IR pass that expands X86-specific pseudo
/// instructions into a sequence of actual instructions. This pass
/// must run after prologue/epilogue insertion and before lowering
/// the MachineInstr to MC.
FunctionPass *createX86ExpandPseudoPass();

/// This pass converts X86 cmov instructions into branch when profitable.
FunctionPass *createX86CmovConverterPass();

/// Return a Machine Function pass that attempts to replace
/// ROP friendly instructions with alternatives.
FunctionPass *createX86FixupGadgetsPass();

/// Return a Machine Function pass that attempts to replace
/// RET instructions with a cleaning sequence
FunctionPass *createX86RetCleanPass();

/// Return a Machine IR pass that selectively replaces
/// certain byte and word instructions by equivalent 32 bit instructions,
/// in order to eliminate partial register usage, false dependences on
/// the upper portions of registers, and to save code size.
FunctionPass *createX86FixupBWInsts();

/// Return a Machine IR pass that reassigns instruction chains from one domain
/// to another, when profitable.
FunctionPass *createX86DomainReassignmentPass();

/// This pass compress instructions from EVEX space to legacy/VEX/EVEX space when
/// possible in order to reduce code size or facilitate HW decoding.
FunctionPass *createX86CompressEVEXPass();

/// This pass creates the thunks for the retpoline feature.
FunctionPass *createX86IndirectThunksPass();

/// This pass replaces ret instructions with jmp's to __x86_return thunk.
FunctionPass *createX86ReturnThunksPass();

/// This pass ensures instructions featuring a memory operand
/// have distinctive <LineNumber, Discriminator> (with respect to eachother)
FunctionPass *createX86DiscriminateMemOpsPass();

/// This pass applies profiling information to insert cache prefetches.
FunctionPass *createX86InsertPrefetchPass();

/// This pass insert wait instruction after X87 instructions which could raise
/// fp exceptions when strict-fp enabled.
FunctionPass *createX86InsertX87waitPass();

/// This pass optimizes arithmetic based on knowledge that is only used by
/// a reduction sequence and is therefore safe to reassociate in interesting
/// ways.
FunctionPass *createX86PartialReductionPass();

InstructionSelector *createX86InstructionSelector(const X86TargetMachine &TM,
                                                  const X86Subtarget &,
                                                  const X86RegisterBankInfo &);

FunctionPass *createX86LoadValueInjectionLoadHardeningPass();
FunctionPass *createX86LoadValueInjectionRetHardeningPass();
FunctionPass *createX86SpeculativeLoadHardeningPass();
FunctionPass *createX86SpeculativeExecutionSideEffectSuppression();
FunctionPass *createX86ArgumentStackSlotPass();

void initializeCompressEVEXPassPass(PassRegistry &);
void initializeFPSPass(PassRegistry &);
void initializeFixupBWInstPassPass(PassRegistry &);
void initializeFixupLEAPassPass(PassRegistry &);
void initializeX86ArgumentStackSlotPassPass(PassRegistry &);
void initializeX86FixupInstTuningPassPass(PassRegistry &);
void initializeX86FixupVectorConstantsPassPass(PassRegistry &);
void initializeWinEHStatePassPass(PassRegistry &);
void initializeX86AvoidSFBPassPass(PassRegistry &);
void initializeX86AvoidTrailingCallPassPass(PassRegistry &);
void initializeX86CallFrameOptimizationPass(PassRegistry &);
void initializeX86CmovConverterPassPass(PassRegistry &);
void initializeX86DAGToDAGISelLegacyPass(PassRegistry &);
void initializeX86DomainReassignmentPass(PassRegistry &);
void initializeX86ExecutionDomainFixPass(PassRegistry &);
void initializeX86ExpandPseudoPass(PassRegistry &);
void initializeX86FastPreTileConfigPass(PassRegistry &);
void initializeX86FastTileConfigPass(PassRegistry &);
void initializeX86FixupSetCCPassPass(PassRegistry &);
void initializeX86WinFixupBufferSecurityCheckPassPass(PassRegistry &);
void initializeX86FlagsCopyLoweringPassPass(PassRegistry &);
void initializeX86LoadValueInjectionLoadHardeningPassPass(PassRegistry &);
void initializeX86LoadValueInjectionRetHardeningPassPass(PassRegistry &);
void initializeX86LowerAMXIntrinsicsLegacyPassPass(PassRegistry &);
void initializeX86LowerAMXTypeLegacyPassPass(PassRegistry &);
void initializeX86LowerTileCopyPass(PassRegistry &);
void initializeX86OptimizeLEAPassPass(PassRegistry &);
void initializeX86PartialReductionPass(PassRegistry &);
void initializeX86PreTileConfigPass(PassRegistry &);
void initializeX86ReturnThunksPass(PassRegistry &);
void initializeX86SpeculativeExecutionSideEffectSuppressionPass(PassRegistry &);
void initializeX86SpeculativeLoadHardeningPassPass(PassRegistry &);
void initializeX86TileConfigPass(PassRegistry &);

namespace X86AS {
enum : unsigned {
  GS = 256,
  FS = 257,
  SS = 258,
  PTR32_SPTR = 270,
  PTR32_UPTR = 271,
  PTR64 = 272
};
} // End X86AS namespace

} // End llvm namespace

#endif
