//===- AMDGPUResourceUsageAnalysis.h ---- analysis of resources -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Analyzes how many registers and other resources are used by
/// functions.
///
/// The results of this analysis are used to fill the register usage, flat
/// usage, etc. into hardware registers.
///
/// The analysis takes callees into account. E.g. if a function A that needs 10
/// VGPRs calls a function B that needs 20 VGPRs, querying the VGPR usage of A
/// will return 20.
/// It is assumed that an indirect call can go into any function except
/// hardware-entrypoints. Therefore the register usage of functions with
/// indirect calls is estimated as the maximum of all non-entrypoint functions
/// in the module.
///
//===----------------------------------------------------------------------===//

#include "AMDGPUResourceUsageAnalysis.h"
#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;
using namespace llvm::AMDGPU;

#define DEBUG_TYPE "amdgpu-resource-usage"

char llvm::AMDGPUResourceUsageAnalysis::ID = 0;
char &llvm::AMDGPUResourceUsageAnalysisID = AMDGPUResourceUsageAnalysis::ID;

// In code object v4 and older, we need to tell the runtime some amount ahead of
// time if we don't know the true stack size. Assume a smaller number if this is
// only due to dynamic / non-entry block allocas.
static cl::opt<uint32_t> clAssumedStackSizeForExternalCall(
    "amdgpu-assume-external-call-stack-size",
    cl::desc("Assumed stack use of any external call (in bytes)"), cl::Hidden,
    cl::init(16384));

static cl::opt<uint32_t> clAssumedStackSizeForDynamicSizeObjects(
    "amdgpu-assume-dynamic-stack-object-size",
    cl::desc("Assumed extra stack use if there are any "
             "variable sized objects (in bytes)"),
    cl::Hidden, cl::init(4096));

INITIALIZE_PASS(AMDGPUResourceUsageAnalysis, DEBUG_TYPE,
                "Function register usage analysis", true, true)

static const Function *getCalleeFunction(const MachineOperand &Op) {
  if (Op.isImm()) {
    assert(Op.getImm() == 0);
    return nullptr;
  }
  return cast<Function>(Op.getGlobal()->stripPointerCastsAndAliases());
}

static bool hasAnyNonFlatUseOfReg(const MachineRegisterInfo &MRI,
                                  const SIInstrInfo &TII, unsigned Reg) {
  for (const MachineOperand &UseOp : MRI.reg_operands(Reg)) {
    if (!UseOp.isImplicit() || !TII.isFLAT(*UseOp.getParent()))
      return true;
  }

  return false;
}

int32_t AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo::getTotalNumSGPRs(
    const GCNSubtarget &ST) const {
  return NumExplicitSGPR +
         IsaInfo::getNumExtraSGPRs(&ST, UsesVCC, UsesFlatScratch,
                                   ST.getTargetID().isXnackOnOrAny());
}

int32_t AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo::getTotalNumVGPRs(
    const GCNSubtarget &ST, int32_t ArgNumAGPR, int32_t ArgNumVGPR) const {
  return AMDGPU::getTotalNumVGPRs(ST.hasGFX90AInsts(), ArgNumAGPR, ArgNumVGPR);
}

int32_t AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo::getTotalNumVGPRs(
    const GCNSubtarget &ST) const {
  return getTotalNumVGPRs(ST, NumAGPR, NumVGPR);
}

bool AMDGPUResourceUsageAnalysis::runOnModule(Module &M) {
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  const TargetMachine &TM = TPC->getTM<TargetMachine>();
  const MCSubtargetInfo &STI = *TM.getMCSubtargetInfo();
  bool HasIndirectCall = false;

  CallGraph CG = CallGraph(M);
  auto End = po_end(&CG);

  // By default, for code object v5 and later, track only the minimum scratch
  // size
  uint32_t AssumedStackSizeForDynamicSizeObjects =
      clAssumedStackSizeForDynamicSizeObjects;
  uint32_t AssumedStackSizeForExternalCall = clAssumedStackSizeForExternalCall;
  if (AMDGPU::getAMDHSACodeObjectVersion(M) >= AMDGPU::AMDHSA_COV5 ||
      STI.getTargetTriple().getOS() == Triple::AMDPAL) {
    if (clAssumedStackSizeForDynamicSizeObjects.getNumOccurrences() == 0)
      AssumedStackSizeForDynamicSizeObjects = 0;
    if (clAssumedStackSizeForExternalCall.getNumOccurrences() == 0)
      AssumedStackSizeForExternalCall = 0;
  }

  for (auto IT = po_begin(&CG); IT != End; ++IT) {
    Function *F = IT->getFunction();
    if (!F || F->isDeclaration())
      continue;

    MachineFunction *MF = MMI.getMachineFunction(*F);
    assert(MF && "function must have been generated already");

    auto CI =
        CallGraphResourceInfo.insert(std::pair(F, SIFunctionResourceInfo()));
    SIFunctionResourceInfo &Info = CI.first->second;
    assert(CI.second && "should only be called once per function");
    Info = analyzeResourceUsage(*MF, TM, AssumedStackSizeForDynamicSizeObjects,
                                AssumedStackSizeForExternalCall);
    HasIndirectCall |= Info.HasIndirectCall;
  }

  // It's possible we have unreachable functions in the module which weren't
  // visited by the PO traversal. Make sure we have some resource counts to
  // report.
  for (const auto &IT : CG) {
    const Function *F = IT.first;
    if (!F || F->isDeclaration())
      continue;

    auto CI =
        CallGraphResourceInfo.insert(std::pair(F, SIFunctionResourceInfo()));
    if (!CI.second) // Skip already visited functions
      continue;

    SIFunctionResourceInfo &Info = CI.first->second;
    MachineFunction *MF = MMI.getMachineFunction(*F);
    assert(MF && "function must have been generated already");
    Info = analyzeResourceUsage(*MF, TM, AssumedStackSizeForDynamicSizeObjects,
                                AssumedStackSizeForExternalCall);
    HasIndirectCall |= Info.HasIndirectCall;
  }

  if (HasIndirectCall)
    propagateIndirectCallRegisterUsage();

  return false;
}

AMDGPUResourceUsageAnalysis::SIFunctionResourceInfo
AMDGPUResourceUsageAnalysis::analyzeResourceUsage(
    const MachineFunction &MF, const TargetMachine &TM,
    uint32_t AssumedStackSizeForDynamicSizeObjects,
    uint32_t AssumedStackSizeForExternalCall) const {
  SIFunctionResourceInfo Info;

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();

  Info.UsesFlatScratch = MRI.isPhysRegUsed(AMDGPU::FLAT_SCR_LO) ||
                         MRI.isPhysRegUsed(AMDGPU::FLAT_SCR_HI) ||
                         MRI.isLiveIn(MFI->getPreloadedReg(
                             AMDGPUFunctionArgInfo::FLAT_SCRATCH_INIT));

  // Even if FLAT_SCRATCH is implicitly used, it has no effect if flat
  // instructions aren't used to access the scratch buffer. Inline assembly may
  // need it though.
  //
  // If we only have implicit uses of flat_scr on flat instructions, it is not
  // really needed.
  if (Info.UsesFlatScratch && !MFI->getUserSGPRInfo().hasFlatScratchInit() &&
      (!hasAnyNonFlatUseOfReg(MRI, *TII, AMDGPU::FLAT_SCR) &&
       !hasAnyNonFlatUseOfReg(MRI, *TII, AMDGPU::FLAT_SCR_LO) &&
       !hasAnyNonFlatUseOfReg(MRI, *TII, AMDGPU::FLAT_SCR_HI))) {
    Info.UsesFlatScratch = false;
  }

  Info.PrivateSegmentSize = FrameInfo.getStackSize();

  // Assume a big number if there are any unknown sized objects.
  Info.HasDynamicallySizedStack = FrameInfo.hasVarSizedObjects();
  if (Info.HasDynamicallySizedStack)
    Info.PrivateSegmentSize += AssumedStackSizeForDynamicSizeObjects;

  if (MFI->isStackRealigned())
    Info.PrivateSegmentSize += FrameInfo.getMaxAlign().value();

  Info.UsesVCC =
      MRI.isPhysRegUsed(AMDGPU::VCC_LO) || MRI.isPhysRegUsed(AMDGPU::VCC_HI);

  // If there are no calls, MachineRegisterInfo can tell us the used register
  // count easily.
  // A tail call isn't considered a call for MachineFrameInfo's purposes.
  if (!FrameInfo.hasCalls() && !FrameInfo.hasTailCall()) {
    MCPhysReg HighestVGPRReg = AMDGPU::NoRegister;
    for (MCPhysReg Reg : reverse(AMDGPU::VGPR_32RegClass.getRegisters())) {
      if (MRI.isPhysRegUsed(Reg)) {
        HighestVGPRReg = Reg;
        break;
      }
    }

    if (ST.hasMAIInsts()) {
      MCPhysReg HighestAGPRReg = AMDGPU::NoRegister;
      for (MCPhysReg Reg : reverse(AMDGPU::AGPR_32RegClass.getRegisters())) {
        if (MRI.isPhysRegUsed(Reg)) {
          HighestAGPRReg = Reg;
          break;
        }
      }
      Info.NumAGPR = HighestAGPRReg == AMDGPU::NoRegister
                         ? 0
                         : TRI.getHWRegIndex(HighestAGPRReg) + 1;
    }

    MCPhysReg HighestSGPRReg = AMDGPU::NoRegister;
    for (MCPhysReg Reg : reverse(AMDGPU::SGPR_32RegClass.getRegisters())) {
      if (MRI.isPhysRegUsed(Reg)) {
        HighestSGPRReg = Reg;
        break;
      }
    }

    // We found the maximum register index. They start at 0, so add one to get
    // the number of registers.
    Info.NumVGPR = HighestVGPRReg == AMDGPU::NoRegister
                       ? 0
                       : TRI.getHWRegIndex(HighestVGPRReg) + 1;
    Info.NumExplicitSGPR = HighestSGPRReg == AMDGPU::NoRegister
                               ? 0
                               : TRI.getHWRegIndex(HighestSGPRReg) + 1;

    return Info;
  }

  int32_t MaxVGPR = -1;
  int32_t MaxAGPR = -1;
  int32_t MaxSGPR = -1;
  uint64_t CalleeFrameSize = 0;

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      // TODO: Check regmasks? Do they occur anywhere except calls?
      for (const MachineOperand &MO : MI.operands()) {
        unsigned Width = 0;
        bool IsSGPR = false;
        bool IsAGPR = false;

        if (!MO.isReg())
          continue;

        Register Reg = MO.getReg();
        switch (Reg) {
        case AMDGPU::EXEC:
        case AMDGPU::EXEC_LO:
        case AMDGPU::EXEC_HI:
        case AMDGPU::SCC:
        case AMDGPU::M0:
        case AMDGPU::M0_LO16:
        case AMDGPU::M0_HI16:
        case AMDGPU::SRC_SHARED_BASE_LO:
        case AMDGPU::SRC_SHARED_BASE:
        case AMDGPU::SRC_SHARED_LIMIT_LO:
        case AMDGPU::SRC_SHARED_LIMIT:
        case AMDGPU::SRC_PRIVATE_BASE_LO:
        case AMDGPU::SRC_PRIVATE_BASE:
        case AMDGPU::SRC_PRIVATE_LIMIT_LO:
        case AMDGPU::SRC_PRIVATE_LIMIT:
        case AMDGPU::SRC_POPS_EXITING_WAVE_ID:
        case AMDGPU::SGPR_NULL:
        case AMDGPU::SGPR_NULL64:
        case AMDGPU::MODE:
          continue;

        case AMDGPU::NoRegister:
          assert(MI.isDebugInstr() &&
                 "Instruction uses invalid noreg register");
          continue;

        case AMDGPU::VCC:
        case AMDGPU::VCC_LO:
        case AMDGPU::VCC_HI:
        case AMDGPU::VCC_LO_LO16:
        case AMDGPU::VCC_LO_HI16:
        case AMDGPU::VCC_HI_LO16:
        case AMDGPU::VCC_HI_HI16:
          Info.UsesVCC = true;
          continue;

        case AMDGPU::FLAT_SCR:
        case AMDGPU::FLAT_SCR_LO:
        case AMDGPU::FLAT_SCR_HI:
          continue;

        case AMDGPU::XNACK_MASK:
        case AMDGPU::XNACK_MASK_LO:
        case AMDGPU::XNACK_MASK_HI:
          llvm_unreachable("xnack_mask registers should not be used");

        case AMDGPU::LDS_DIRECT:
          llvm_unreachable("lds_direct register should not be used");

        case AMDGPU::TBA:
        case AMDGPU::TBA_LO:
        case AMDGPU::TBA_HI:
        case AMDGPU::TMA:
        case AMDGPU::TMA_LO:
        case AMDGPU::TMA_HI:
          llvm_unreachable("trap handler registers should not be used");

        case AMDGPU::SRC_VCCZ:
          llvm_unreachable("src_vccz register should not be used");

        case AMDGPU::SRC_EXECZ:
          llvm_unreachable("src_execz register should not be used");

        case AMDGPU::SRC_SCC:
          llvm_unreachable("src_scc register should not be used");

        default:
          break;
        }

        if (AMDGPU::SGPR_32RegClass.contains(Reg) ||
            AMDGPU::SGPR_LO16RegClass.contains(Reg) ||
            AMDGPU::SGPR_HI16RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 1;
        } else if (AMDGPU::VGPR_32RegClass.contains(Reg) ||
                   AMDGPU::VGPR_16RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 1;
        } else if (AMDGPU::AGPR_32RegClass.contains(Reg) ||
                   AMDGPU::AGPR_LO16RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 1;
        } else if (AMDGPU::SGPR_64RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 2;
        } else if (AMDGPU::VReg_64RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 2;
        } else if (AMDGPU::AReg_64RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 2;
        } else if (AMDGPU::VReg_96RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 3;
        } else if (AMDGPU::SReg_96RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 3;
        } else if (AMDGPU::AReg_96RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 3;
        } else if (AMDGPU::SGPR_128RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 4;
        } else if (AMDGPU::VReg_128RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 4;
        } else if (AMDGPU::AReg_128RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 4;
        } else if (AMDGPU::VReg_160RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 5;
        } else if (AMDGPU::SReg_160RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 5;
        } else if (AMDGPU::AReg_160RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 5;
        } else if (AMDGPU::VReg_192RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 6;
        } else if (AMDGPU::SReg_192RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 6;
        } else if (AMDGPU::AReg_192RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 6;
        } else if (AMDGPU::VReg_224RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 7;
        } else if (AMDGPU::SReg_224RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 7;
        } else if (AMDGPU::AReg_224RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 7;
        } else if (AMDGPU::SReg_256RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 8;
        } else if (AMDGPU::VReg_256RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 8;
        } else if (AMDGPU::AReg_256RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 8;
        } else if (AMDGPU::VReg_288RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 9;
        } else if (AMDGPU::SReg_288RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 9;
        } else if (AMDGPU::AReg_288RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 9;
        } else if (AMDGPU::VReg_320RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 10;
        } else if (AMDGPU::SReg_320RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 10;
        } else if (AMDGPU::AReg_320RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 10;
        } else if (AMDGPU::VReg_352RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 11;
        } else if (AMDGPU::SReg_352RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 11;
        } else if (AMDGPU::AReg_352RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 11;
        } else if (AMDGPU::VReg_384RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 12;
        } else if (AMDGPU::SReg_384RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 12;
        } else if (AMDGPU::AReg_384RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 12;
        } else if (AMDGPU::SReg_512RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 16;
        } else if (AMDGPU::VReg_512RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 16;
        } else if (AMDGPU::AReg_512RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 16;
        } else if (AMDGPU::SReg_1024RegClass.contains(Reg)) {
          IsSGPR = true;
          Width = 32;
        } else if (AMDGPU::VReg_1024RegClass.contains(Reg)) {
          IsSGPR = false;
          Width = 32;
        } else if (AMDGPU::AReg_1024RegClass.contains(Reg)) {
          IsSGPR = false;
          IsAGPR = true;
          Width = 32;
        } else {
          // We only expect TTMP registers or registers that do not belong to
          // any RC.
          assert((AMDGPU::TTMP_32RegClass.contains(Reg) ||
                  AMDGPU::TTMP_64RegClass.contains(Reg) ||
                  AMDGPU::TTMP_128RegClass.contains(Reg) ||
                  AMDGPU::TTMP_256RegClass.contains(Reg) ||
                  AMDGPU::TTMP_512RegClass.contains(Reg) ||
                  !TRI.getPhysRegBaseClass(Reg)) &&
                 "Unknown register class");
        }
        unsigned HWReg = TRI.getHWRegIndex(Reg);
        int MaxUsed = HWReg + Width - 1;
        if (IsSGPR) {
          MaxSGPR = MaxUsed > MaxSGPR ? MaxUsed : MaxSGPR;
        } else if (IsAGPR) {
          MaxAGPR = MaxUsed > MaxAGPR ? MaxUsed : MaxAGPR;
        } else {
          MaxVGPR = MaxUsed > MaxVGPR ? MaxUsed : MaxVGPR;
        }
      }

      if (MI.isCall()) {
        // Pseudo used just to encode the underlying global. Is there a better
        // way to track this?

        const MachineOperand *CalleeOp =
            TII->getNamedOperand(MI, AMDGPU::OpName::callee);

        const Function *Callee = getCalleeFunction(*CalleeOp);
        DenseMap<const Function *, SIFunctionResourceInfo>::const_iterator I =
            CallGraphResourceInfo.end();

        // Avoid crashing on undefined behavior with an illegal call to a
        // kernel. If a callsite's calling convention doesn't match the
        // function's, it's undefined behavior. If the callsite calling
        // convention does match, that would have errored earlier.
        if (Callee && AMDGPU::isEntryFunctionCC(Callee->getCallingConv()))
          report_fatal_error("invalid call to entry function");

        bool IsIndirect = !Callee || Callee->isDeclaration();
        if (!IsIndirect)
          I = CallGraphResourceInfo.find(Callee);

        // FIXME: Call site could have norecurse on it
        if (!Callee || !Callee->doesNotRecurse()) {
          Info.HasRecursion = true;

          // TODO: If we happen to know there is no stack usage in the
          // callgraph, we don't need to assume an infinitely growing stack.
          if (!MI.isReturn()) {
            // We don't need to assume an unknown stack size for tail calls.

            // FIXME: This only benefits in the case where the kernel does not
            // directly call the tail called function. If a kernel directly
            // calls a tail recursive function, we'll assume maximum stack size
            // based on the regular call instruction.
            CalleeFrameSize = std::max(
                CalleeFrameSize,
                static_cast<uint64_t>(AssumedStackSizeForExternalCall));
          }
        }

        if (IsIndirect || I == CallGraphResourceInfo.end()) {
          CalleeFrameSize =
              std::max(CalleeFrameSize,
                       static_cast<uint64_t>(AssumedStackSizeForExternalCall));

          // Register usage of indirect calls gets handled later
          Info.UsesVCC = true;
          Info.UsesFlatScratch = ST.hasFlatAddressSpace();
          Info.HasDynamicallySizedStack = true;
          Info.HasIndirectCall = true;
        } else {
          // We force CodeGen to run in SCC order, so the callee's register
          // usage etc. should be the cumulative usage of all callees.
          MaxSGPR = std::max(I->second.NumExplicitSGPR - 1, MaxSGPR);
          MaxVGPR = std::max(I->second.NumVGPR - 1, MaxVGPR);
          MaxAGPR = std::max(I->second.NumAGPR - 1, MaxAGPR);
          CalleeFrameSize =
              std::max(I->second.PrivateSegmentSize, CalleeFrameSize);
          Info.UsesVCC |= I->second.UsesVCC;
          Info.UsesFlatScratch |= I->second.UsesFlatScratch;
          Info.HasDynamicallySizedStack |= I->second.HasDynamicallySizedStack;
          Info.HasRecursion |= I->second.HasRecursion;
          Info.HasIndirectCall |= I->second.HasIndirectCall;
        }
      }
    }
  }

  Info.NumExplicitSGPR = MaxSGPR + 1;
  Info.NumVGPR = MaxVGPR + 1;
  Info.NumAGPR = MaxAGPR + 1;
  Info.PrivateSegmentSize += CalleeFrameSize;

  return Info;
}

void AMDGPUResourceUsageAnalysis::propagateIndirectCallRegisterUsage() {
  // Collect the maximum number of registers from non-hardware-entrypoints.
  // All these functions are potential targets for indirect calls.
  int32_t NonKernelMaxSGPRs = 0;
  int32_t NonKernelMaxVGPRs = 0;
  int32_t NonKernelMaxAGPRs = 0;

  for (const auto &I : CallGraphResourceInfo) {
    if (!AMDGPU::isEntryFunctionCC(I.getFirst()->getCallingConv())) {
      auto &Info = I.getSecond();
      NonKernelMaxSGPRs = std::max(NonKernelMaxSGPRs, Info.NumExplicitSGPR);
      NonKernelMaxVGPRs = std::max(NonKernelMaxVGPRs, Info.NumVGPR);
      NonKernelMaxAGPRs = std::max(NonKernelMaxAGPRs, Info.NumAGPR);
    }
  }

  // Add register usage for functions with indirect calls.
  // For calls to unknown functions, we assume the maximum register usage of
  // all non-hardware-entrypoints in the current module.
  for (auto &I : CallGraphResourceInfo) {
    auto &Info = I.getSecond();
    if (Info.HasIndirectCall) {
      Info.NumExplicitSGPR = std::max(Info.NumExplicitSGPR, NonKernelMaxSGPRs);
      Info.NumVGPR = std::max(Info.NumVGPR, NonKernelMaxVGPRs);
      Info.NumAGPR = std::max(Info.NumAGPR, NonKernelMaxAGPRs);
    }
  }
}
