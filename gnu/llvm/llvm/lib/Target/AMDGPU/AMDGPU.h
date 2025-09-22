//===-- AMDGPU.h - MachineFunction passes hw codegen --------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPU_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPU_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/AMDGPUAddrSpace.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

class AMDGPUTargetMachine;
class TargetMachine;

// GlobalISel passes
void initializeAMDGPUPreLegalizerCombinerPass(PassRegistry &);
FunctionPass *createAMDGPUPreLegalizeCombiner(bool IsOptNone);
void initializeAMDGPUPostLegalizerCombinerPass(PassRegistry &);
FunctionPass *createAMDGPUPostLegalizeCombiner(bool IsOptNone);
FunctionPass *createAMDGPURegBankCombiner(bool IsOptNone);
void initializeAMDGPURegBankCombinerPass(PassRegistry &);

void initializeAMDGPURegBankSelectPass(PassRegistry &);

// SI Passes
FunctionPass *createGCNDPPCombinePass();
FunctionPass *createSIAnnotateControlFlowPass();
FunctionPass *createSIFoldOperandsPass();
FunctionPass *createSIPeepholeSDWAPass();
FunctionPass *createSILowerI1CopiesPass();
FunctionPass *createAMDGPUGlobalISelDivergenceLoweringPass();
FunctionPass *createSIShrinkInstructionsPass();
FunctionPass *createSILoadStoreOptimizerPass();
FunctionPass *createSIWholeQuadModePass();
FunctionPass *createSIFixControlFlowLiveIntervalsPass();
FunctionPass *createSIOptimizeExecMaskingPreRAPass();
FunctionPass *createSIOptimizeVGPRLiveRangePass();
FunctionPass *createSIFixSGPRCopiesPass();
FunctionPass *createLowerWWMCopiesPass();
FunctionPass *createSIMemoryLegalizerPass();
FunctionPass *createSIInsertWaitcntsPass();
FunctionPass *createSIPreAllocateWWMRegsPass();
FunctionPass *createSIFormMemoryClausesPass();

FunctionPass *createSIPostRABundlerPass();
FunctionPass *createAMDGPUImageIntrinsicOptimizerPass(const TargetMachine *);
ModulePass *createAMDGPURemoveIncompatibleFunctionsPass(const TargetMachine *);
FunctionPass *createAMDGPUCodeGenPreparePass();
FunctionPass *createAMDGPULateCodeGenPreparePass();
FunctionPass *createAMDGPUMachineCFGStructurizerPass();
FunctionPass *createAMDGPURewriteOutArgumentsPass();
ModulePass *
createAMDGPULowerModuleLDSLegacyPass(const AMDGPUTargetMachine *TM = nullptr);
ModulePass *createAMDGPULowerBufferFatPointersPass();
FunctionPass *createSIModeRegisterPass();
FunctionPass *createGCNPreRAOptimizationsPass();

struct AMDGPUSimplifyLibCallsPass : PassInfoMixin<AMDGPUSimplifyLibCallsPass> {
  AMDGPUSimplifyLibCallsPass() {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct AMDGPUImageIntrinsicOptimizerPass
    : PassInfoMixin<AMDGPUImageIntrinsicOptimizerPass> {
  AMDGPUImageIntrinsicOptimizerPass(TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  TargetMachine &TM;
};

struct AMDGPUUseNativeCallsPass : PassInfoMixin<AMDGPUUseNativeCallsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

void initializeAMDGPUDAGToDAGISelLegacyPass(PassRegistry &);

void initializeAMDGPUMachineCFGStructurizerPass(PassRegistry&);
extern char &AMDGPUMachineCFGStructurizerID;

void initializeAMDGPUAlwaysInlinePass(PassRegistry&);

Pass *createAMDGPUAnnotateKernelFeaturesPass();
Pass *createAMDGPUAttributorLegacyPass();
void initializeAMDGPUAttributorLegacyPass(PassRegistry &);
void initializeAMDGPUAnnotateKernelFeaturesPass(PassRegistry &);
extern char &AMDGPUAnnotateKernelFeaturesID;

// DPP/Iterative option enables the atomic optimizer with given strategy
// whereas None disables the atomic optimizer.
enum class ScanOptions { DPP, Iterative, None };
FunctionPass *createAMDGPUAtomicOptimizerPass(ScanOptions ScanStrategy);
void initializeAMDGPUAtomicOptimizerPass(PassRegistry &);
extern char &AMDGPUAtomicOptimizerID;

ModulePass *createAMDGPUCtorDtorLoweringLegacyPass();
void initializeAMDGPUCtorDtorLoweringLegacyPass(PassRegistry &);
extern char &AMDGPUCtorDtorLoweringLegacyPassID;

FunctionPass *createAMDGPULowerKernelArgumentsPass();
void initializeAMDGPULowerKernelArgumentsPass(PassRegistry &);
extern char &AMDGPULowerKernelArgumentsID;

FunctionPass *createAMDGPUPromoteKernelArgumentsPass();
void initializeAMDGPUPromoteKernelArgumentsPass(PassRegistry &);
extern char &AMDGPUPromoteKernelArgumentsID;

struct AMDGPUPromoteKernelArgumentsPass
    : PassInfoMixin<AMDGPUPromoteKernelArgumentsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

ModulePass *createAMDGPULowerKernelAttributesPass();
void initializeAMDGPULowerKernelAttributesPass(PassRegistry &);
extern char &AMDGPULowerKernelAttributesID;

struct AMDGPULowerKernelAttributesPass
    : PassInfoMixin<AMDGPULowerKernelAttributesPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

void initializeAMDGPULowerModuleLDSLegacyPass(PassRegistry &);
extern char &AMDGPULowerModuleLDSLegacyPassID;

struct AMDGPULowerModuleLDSPass : PassInfoMixin<AMDGPULowerModuleLDSPass> {
  const AMDGPUTargetMachine &TM;
  AMDGPULowerModuleLDSPass(const AMDGPUTargetMachine &TM_) : TM(TM_) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

void initializeAMDGPULowerBufferFatPointersPass(PassRegistry &);
extern char &AMDGPULowerBufferFatPointersID;

struct AMDGPULowerBufferFatPointersPass
    : PassInfoMixin<AMDGPULowerBufferFatPointersPass> {
  AMDGPULowerBufferFatPointersPass(const TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  const TargetMachine &TM;
};

void initializeAMDGPURewriteOutArgumentsPass(PassRegistry &);
extern char &AMDGPURewriteOutArgumentsID;

void initializeGCNDPPCombinePass(PassRegistry &);
extern char &GCNDPPCombineID;

void initializeSIFoldOperandsPass(PassRegistry &);
extern char &SIFoldOperandsID;

void initializeSIPeepholeSDWAPass(PassRegistry &);
extern char &SIPeepholeSDWAID;

void initializeSIShrinkInstructionsPass(PassRegistry&);
extern char &SIShrinkInstructionsID;

void initializeSIFixSGPRCopiesPass(PassRegistry &);
extern char &SIFixSGPRCopiesID;

void initializeSIFixVGPRCopiesPass(PassRegistry &);
extern char &SIFixVGPRCopiesID;

void initializeSILowerWWMCopiesPass(PassRegistry &);
extern char &SILowerWWMCopiesID;

void initializeSILowerI1CopiesPass(PassRegistry &);
extern char &SILowerI1CopiesID;

void initializeAMDGPUGlobalISelDivergenceLoweringPass(PassRegistry &);
extern char &AMDGPUGlobalISelDivergenceLoweringID;

void initializeAMDGPUMarkLastScratchLoadPass(PassRegistry &);
extern char &AMDGPUMarkLastScratchLoadID;

void initializeSILowerSGPRSpillsPass(PassRegistry &);
extern char &SILowerSGPRSpillsID;

void initializeSILoadStoreOptimizerPass(PassRegistry &);
extern char &SILoadStoreOptimizerID;

void initializeSIWholeQuadModePass(PassRegistry &);
extern char &SIWholeQuadModeID;

void initializeSILowerControlFlowPass(PassRegistry &);
extern char &SILowerControlFlowID;

void initializeSIPreEmitPeepholePass(PassRegistry &);
extern char &SIPreEmitPeepholeID;

void initializeSILateBranchLoweringPass(PassRegistry &);
extern char &SILateBranchLoweringPassID;

void initializeSIOptimizeExecMaskingPass(PassRegistry &);
extern char &SIOptimizeExecMaskingID;

void initializeSIPreAllocateWWMRegsPass(PassRegistry &);
extern char &SIPreAllocateWWMRegsID;

void initializeAMDGPUImageIntrinsicOptimizerPass(PassRegistry &);
extern char &AMDGPUImageIntrinsicOptimizerID;

void initializeAMDGPUPerfHintAnalysisPass(PassRegistry &);
extern char &AMDGPUPerfHintAnalysisID;

void initializeGCNRegPressurePrinterPass(PassRegistry &);
extern char &GCNRegPressurePrinterID;

// Passes common to R600 and SI
FunctionPass *createAMDGPUPromoteAlloca();
void initializeAMDGPUPromoteAllocaPass(PassRegistry&);
extern char &AMDGPUPromoteAllocaID;

FunctionPass *createAMDGPUPromoteAllocaToVector();
void initializeAMDGPUPromoteAllocaToVectorPass(PassRegistry&);
extern char &AMDGPUPromoteAllocaToVectorID;

struct AMDGPUPromoteAllocaPass : PassInfoMixin<AMDGPUPromoteAllocaPass> {
  AMDGPUPromoteAllocaPass(TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  TargetMachine &TM;
};

struct AMDGPUPromoteAllocaToVectorPass
    : PassInfoMixin<AMDGPUPromoteAllocaToVectorPass> {
  AMDGPUPromoteAllocaToVectorPass(TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  TargetMachine &TM;
};

struct AMDGPUAtomicOptimizerPass : PassInfoMixin<AMDGPUAtomicOptimizerPass> {
  AMDGPUAtomicOptimizerPass(TargetMachine &TM, ScanOptions ScanImpl)
      : TM(TM), ScanImpl(ScanImpl) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  TargetMachine &TM;
  ScanOptions ScanImpl;
};

Pass *createAMDGPUStructurizeCFGPass();
FunctionPass *createAMDGPUISelDag(TargetMachine &TM, CodeGenOptLevel OptLevel);
ModulePass *createAMDGPUAlwaysInlinePass(bool GlobalOpt = true);

struct AMDGPUAlwaysInlinePass : PassInfoMixin<AMDGPUAlwaysInlinePass> {
  AMDGPUAlwaysInlinePass(bool GlobalOpt = true) : GlobalOpt(GlobalOpt) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  bool GlobalOpt;
};

class AMDGPUCodeGenPreparePass
    : public PassInfoMixin<AMDGPUCodeGenPreparePass> {
private:
  TargetMachine &TM;

public:
  AMDGPUCodeGenPreparePass(TargetMachine &TM) : TM(TM){};
  PreservedAnalyses run(Function &, FunctionAnalysisManager &);
};

class AMDGPULowerKernelArgumentsPass
    : public PassInfoMixin<AMDGPULowerKernelArgumentsPass> {
private:
  TargetMachine &TM;

public:
  AMDGPULowerKernelArgumentsPass(TargetMachine &TM) : TM(TM){};
  PreservedAnalyses run(Function &, FunctionAnalysisManager &);
};

class AMDGPUAttributorPass : public PassInfoMixin<AMDGPUAttributorPass> {
private:
  TargetMachine &TM;

public:
  AMDGPUAttributorPass(TargetMachine &TM) : TM(TM){};
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

FunctionPass *createAMDGPUAnnotateUniformValues();

ModulePass *createAMDGPUPrintfRuntimeBinding();
void initializeAMDGPUPrintfRuntimeBindingPass(PassRegistry&);
extern char &AMDGPUPrintfRuntimeBindingID;

void initializeAMDGPUResourceUsageAnalysisPass(PassRegistry &);
extern char &AMDGPUResourceUsageAnalysisID;

struct AMDGPUPrintfRuntimeBindingPass
    : PassInfoMixin<AMDGPUPrintfRuntimeBindingPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

ModulePass* createAMDGPUUnifyMetadataPass();
void initializeAMDGPUUnifyMetadataPass(PassRegistry&);
extern char &AMDGPUUnifyMetadataID;

struct AMDGPUUnifyMetadataPass : PassInfoMixin<AMDGPUUnifyMetadataPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

void initializeSIOptimizeExecMaskingPreRAPass(PassRegistry&);
extern char &SIOptimizeExecMaskingPreRAID;

void initializeSIOptimizeVGPRLiveRangePass(PassRegistry &);
extern char &SIOptimizeVGPRLiveRangeID;

void initializeAMDGPUAnnotateUniformValuesPass(PassRegistry&);
extern char &AMDGPUAnnotateUniformValuesPassID;

void initializeAMDGPUCodeGenPreparePass(PassRegistry&);
extern char &AMDGPUCodeGenPrepareID;

void initializeAMDGPURemoveIncompatibleFunctionsPass(PassRegistry &);
extern char &AMDGPURemoveIncompatibleFunctionsID;

void initializeAMDGPULateCodeGenPreparePass(PassRegistry &);
extern char &AMDGPULateCodeGenPrepareID;

FunctionPass *createAMDGPURewriteUndefForPHILegacyPass();
void initializeAMDGPURewriteUndefForPHILegacyPass(PassRegistry &);
extern char &AMDGPURewriteUndefForPHILegacyPassID;

class AMDGPURewriteUndefForPHIPass
    : public PassInfoMixin<AMDGPURewriteUndefForPHIPass> {
public:
  AMDGPURewriteUndefForPHIPass() = default;
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

void initializeSIAnnotateControlFlowPass(PassRegistry&);
extern char &SIAnnotateControlFlowPassID;

void initializeSIMemoryLegalizerPass(PassRegistry&);
extern char &SIMemoryLegalizerID;

void initializeSIModeRegisterPass(PassRegistry&);
extern char &SIModeRegisterID;

void initializeAMDGPUInsertDelayAluPass(PassRegistry &);
extern char &AMDGPUInsertDelayAluID;

void initializeAMDGPUInsertSingleUseVDSTPass(PassRegistry &);
extern char &AMDGPUInsertSingleUseVDSTID;

void initializeSIInsertHardClausesPass(PassRegistry &);
extern char &SIInsertHardClausesID;

void initializeSIInsertWaitcntsPass(PassRegistry&);
extern char &SIInsertWaitcntsID;

void initializeSIFormMemoryClausesPass(PassRegistry&);
extern char &SIFormMemoryClausesID;

void initializeSIPostRABundlerPass(PassRegistry&);
extern char &SIPostRABundlerID;

void initializeGCNCreateVOPDPass(PassRegistry &);
extern char &GCNCreateVOPDID;

void initializeAMDGPUUnifyDivergentExitNodesPass(PassRegistry&);
extern char &AMDGPUUnifyDivergentExitNodesID;

ImmutablePass *createAMDGPUAAWrapperPass();
void initializeAMDGPUAAWrapperPassPass(PassRegistry&);
ImmutablePass *createAMDGPUExternalAAWrapperPass();
void initializeAMDGPUExternalAAWrapperPass(PassRegistry&);

void initializeAMDGPUArgumentUsageInfoPass(PassRegistry &);

ModulePass *createAMDGPUOpenCLEnqueuedBlockLoweringPass();
void initializeAMDGPUOpenCLEnqueuedBlockLoweringPass(PassRegistry &);
extern char &AMDGPUOpenCLEnqueuedBlockLoweringID;

void initializeGCNNSAReassignPass(PassRegistry &);
extern char &GCNNSAReassignID;

void initializeGCNPreRALongBranchRegPass(PassRegistry &);
extern char &GCNPreRALongBranchRegID;

void initializeGCNPreRAOptimizationsPass(PassRegistry &);
extern char &GCNPreRAOptimizationsID;

FunctionPass *createAMDGPUSetWavePriorityPass();
void initializeAMDGPUSetWavePriorityPass(PassRegistry &);

void initializeGCNRewritePartialRegUsesPass(llvm::PassRegistry &);
extern char &GCNRewritePartialRegUsesID;

namespace AMDGPU {
enum TargetIndex {
  TI_CONSTDATA_START,
  TI_SCRATCH_RSRC_DWORD0,
  TI_SCRATCH_RSRC_DWORD1,
  TI_SCRATCH_RSRC_DWORD2,
  TI_SCRATCH_RSRC_DWORD3
};

// FIXME: Missing constant_32bit
inline bool isFlatGlobalAddrSpace(unsigned AS) {
  return AS == AMDGPUAS::GLOBAL_ADDRESS ||
         AS == AMDGPUAS::FLAT_ADDRESS ||
         AS == AMDGPUAS::CONSTANT_ADDRESS ||
         AS > AMDGPUAS::MAX_AMDGPU_ADDRESS;
}

inline bool isExtendedGlobalAddrSpace(unsigned AS) {
  return AS == AMDGPUAS::GLOBAL_ADDRESS || AS == AMDGPUAS::CONSTANT_ADDRESS ||
         AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
         AS > AMDGPUAS::MAX_AMDGPU_ADDRESS;
}

static inline bool addrspacesMayAlias(unsigned AS1, unsigned AS2) {
  static_assert(AMDGPUAS::MAX_AMDGPU_ADDRESS <= 9, "Addr space out of range");

  if (AS1 > AMDGPUAS::MAX_AMDGPU_ADDRESS || AS2 > AMDGPUAS::MAX_AMDGPU_ADDRESS)
    return true;

  // This array is indexed by address space value enum elements 0 ... to 9
  // clang-format off
  static const bool ASAliasRules[10][10] = {
    /*                       Flat   Global Region  Group Constant Private Const32 BufFatPtr BufRsrc BufStrdPtr */
    /* Flat     */            {true,  true,  false, true,  true,  true,  true,  true,  true,  true},
    /* Global   */            {true,  true,  false, false, true,  false, true,  true,  true,  true},
    /* Region   */            {false, false, true,  false, false, false, false, false, false, false},
    /* Group    */            {true,  false, false, true,  false, false, false, false, false, false},
    /* Constant */            {true,  true,  false, false, false, false, true,  true,  true,  true},
    /* Private  */            {true,  false, false, false, false, true,  false, false, false, false},
    /* Constant 32-bit */     {true,  true,  false, false, true,  false, false, true,  true,  true},
    /* Buffer Fat Ptr  */     {true,  true,  false, false, true,  false, true,  true,  true,  true},
    /* Buffer Resource */     {true,  true,  false, false, true,  false, true,  true,  true,  true},
    /* Buffer Strided Ptr  */ {true,  true,  false, false, true,  false, true,  true,  true,  true},
  };
  // clang-format on

  return ASAliasRules[AS1][AS2];
}

}

} // End namespace llvm

#endif
