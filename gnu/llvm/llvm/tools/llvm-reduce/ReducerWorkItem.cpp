//===- ReducerWorkItem.cpp - Wrapper for Module and MachineFunction -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReducerWorkItem.h"
#include "TestRunner.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <optional>

using namespace llvm;

ReducerWorkItem::ReducerWorkItem() = default;
ReducerWorkItem::~ReducerWorkItem() = default;

extern cl::OptionCategory LLVMReduceOptions;
static cl::opt<std::string> TargetTriple("mtriple",
                                         cl::desc("Set the target triple"),
                                         cl::cat(LLVMReduceOptions));

static cl::opt<bool> TmpFilesAsBitcode(
    "write-tmp-files-as-bitcode",
    cl::desc("Always write temporary files as bitcode instead of textual IR"),
    cl::init(false), cl::cat(LLVMReduceOptions));

static void cloneFrameInfo(
    MachineFrameInfo &DstMFI, const MachineFrameInfo &SrcMFI,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB) {
  DstMFI.setFrameAddressIsTaken(SrcMFI.isFrameAddressTaken());
  DstMFI.setReturnAddressIsTaken(SrcMFI.isReturnAddressTaken());
  DstMFI.setHasStackMap(SrcMFI.hasStackMap());
  DstMFI.setHasPatchPoint(SrcMFI.hasPatchPoint());
  DstMFI.setUseLocalStackAllocationBlock(
      SrcMFI.getUseLocalStackAllocationBlock());
  DstMFI.setOffsetAdjustment(SrcMFI.getOffsetAdjustment());

  DstMFI.ensureMaxAlignment(SrcMFI.getMaxAlign());
  assert(DstMFI.getMaxAlign() == SrcMFI.getMaxAlign() &&
         "we need to set exact alignment");

  DstMFI.setAdjustsStack(SrcMFI.adjustsStack());
  DstMFI.setHasCalls(SrcMFI.hasCalls());
  DstMFI.setHasOpaqueSPAdjustment(SrcMFI.hasOpaqueSPAdjustment());
  DstMFI.setHasCopyImplyingStackAdjustment(
      SrcMFI.hasCopyImplyingStackAdjustment());
  DstMFI.setHasVAStart(SrcMFI.hasVAStart());
  DstMFI.setHasMustTailInVarArgFunc(SrcMFI.hasMustTailInVarArgFunc());
  DstMFI.setHasTailCall(SrcMFI.hasTailCall());

  if (SrcMFI.isMaxCallFrameSizeComputed())
    DstMFI.setMaxCallFrameSize(SrcMFI.getMaxCallFrameSize());

  DstMFI.setCVBytesOfCalleeSavedRegisters(
      SrcMFI.getCVBytesOfCalleeSavedRegisters());

  if (MachineBasicBlock *SavePt = SrcMFI.getSavePoint())
    DstMFI.setSavePoint(Src2DstMBB.find(SavePt)->second);
  if (MachineBasicBlock *RestorePt = SrcMFI.getRestorePoint())
    DstMFI.setRestorePoint(Src2DstMBB.find(RestorePt)->second);


  auto CopyObjectProperties = [](MachineFrameInfo &DstMFI,
                                 const MachineFrameInfo &SrcMFI, int FI) {
    if (SrcMFI.isStatepointSpillSlotObjectIndex(FI))
      DstMFI.markAsStatepointSpillSlotObjectIndex(FI);
    DstMFI.setObjectSSPLayout(FI, SrcMFI.getObjectSSPLayout(FI));
    DstMFI.setObjectZExt(FI, SrcMFI.isObjectZExt(FI));
    DstMFI.setObjectSExt(FI, SrcMFI.isObjectSExt(FI));
  };

  for (int i = 0, e = SrcMFI.getNumObjects() - SrcMFI.getNumFixedObjects();
       i != e; ++i) {
    int NewFI;

    assert(!SrcMFI.isFixedObjectIndex(i));
    if (SrcMFI.isVariableSizedObjectIndex(i)) {
      NewFI = DstMFI.CreateVariableSizedObject(SrcMFI.getObjectAlign(i),
                                               SrcMFI.getObjectAllocation(i));
    } else {
      NewFI = DstMFI.CreateStackObject(
          SrcMFI.getObjectSize(i), SrcMFI.getObjectAlign(i),
          SrcMFI.isSpillSlotObjectIndex(i), SrcMFI.getObjectAllocation(i),
          SrcMFI.getStackID(i));
      DstMFI.setObjectOffset(NewFI, SrcMFI.getObjectOffset(i));
    }

    CopyObjectProperties(DstMFI, SrcMFI, i);

    (void)NewFI;
    assert(i == NewFI && "expected to keep stable frame index numbering");
  }

  // Copy the fixed frame objects backwards to preserve frame index numbers,
  // since CreateFixedObject uses front insertion.
  for (int i = -1; i >= (int)-SrcMFI.getNumFixedObjects(); --i) {
    assert(SrcMFI.isFixedObjectIndex(i));
    int NewFI = DstMFI.CreateFixedObject(
      SrcMFI.getObjectSize(i), SrcMFI.getObjectOffset(i),
      SrcMFI.isImmutableObjectIndex(i), SrcMFI.isAliasedObjectIndex(i));
    CopyObjectProperties(DstMFI, SrcMFI, i);

    (void)NewFI;
    assert(i == NewFI && "expected to keep stable frame index numbering");
  }

  for (unsigned I = 0, E = SrcMFI.getLocalFrameObjectCount(); I < E; ++I) {
    auto LocalObject = SrcMFI.getLocalFrameObjectMap(I);
    DstMFI.mapLocalFrameObject(LocalObject.first, LocalObject.second);
  }

  DstMFI.setCalleeSavedInfo(SrcMFI.getCalleeSavedInfo());

  if (SrcMFI.hasStackProtectorIndex()) {
    DstMFI.setStackProtectorIndex(SrcMFI.getStackProtectorIndex());
  }

  // FIXME: Needs test, missing MIR serialization.
  if (SrcMFI.hasFunctionContextIndex()) {
    DstMFI.setFunctionContextIndex(SrcMFI.getFunctionContextIndex());
  }
}

static void cloneJumpTableInfo(
    MachineFunction &DstMF, const MachineJumpTableInfo &SrcJTI,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB) {

  auto *DstJTI = DstMF.getOrCreateJumpTableInfo(SrcJTI.getEntryKind());

  std::vector<MachineBasicBlock *> DstBBs;

  for (const MachineJumpTableEntry &Entry : SrcJTI.getJumpTables()) {
    for (MachineBasicBlock *X : Entry.MBBs)
      DstBBs.push_back(Src2DstMBB.find(X)->second);

    DstJTI->createJumpTableIndex(DstBBs);
    DstBBs.clear();
  }
}

static void cloneMemOperands(MachineInstr &DstMI, MachineInstr &SrcMI,
                             MachineFunction &SrcMF, MachineFunction &DstMF) {
  // The new MachineMemOperands should be owned by the new function's
  // Allocator.
  PseudoSourceValueManager &PSVMgr = DstMF.getPSVManager();

  // We also need to remap the PseudoSourceValues from the new function's
  // PseudoSourceValueManager.
  SmallVector<MachineMemOperand *, 2> NewMMOs;
  for (MachineMemOperand *OldMMO : SrcMI.memoperands()) {
    MachinePointerInfo NewPtrInfo(OldMMO->getPointerInfo());
    if (const PseudoSourceValue *PSV =
            dyn_cast_if_present<const PseudoSourceValue *>(NewPtrInfo.V)) {
      switch (PSV->kind()) {
      case PseudoSourceValue::Stack:
        NewPtrInfo.V = PSVMgr.getStack();
        break;
      case PseudoSourceValue::GOT:
        NewPtrInfo.V = PSVMgr.getGOT();
        break;
      case PseudoSourceValue::JumpTable:
        NewPtrInfo.V = PSVMgr.getJumpTable();
        break;
      case PseudoSourceValue::ConstantPool:
        NewPtrInfo.V = PSVMgr.getConstantPool();
        break;
      case PseudoSourceValue::FixedStack:
        NewPtrInfo.V = PSVMgr.getFixedStack(
            cast<FixedStackPseudoSourceValue>(PSV)->getFrameIndex());
        break;
      case PseudoSourceValue::GlobalValueCallEntry:
        NewPtrInfo.V = PSVMgr.getGlobalValueCallEntry(
            cast<GlobalValuePseudoSourceValue>(PSV)->getValue());
        break;
      case PseudoSourceValue::ExternalSymbolCallEntry:
        NewPtrInfo.V = PSVMgr.getExternalSymbolCallEntry(
            cast<ExternalSymbolPseudoSourceValue>(PSV)->getSymbol());
        break;
      case PseudoSourceValue::TargetCustom:
      default:
        // FIXME: We have no generic interface for allocating custom PSVs.
        report_fatal_error("Cloning TargetCustom PSV not handled");
      }
    }

    MachineMemOperand *NewMMO = DstMF.getMachineMemOperand(
        NewPtrInfo, OldMMO->getFlags(), OldMMO->getMemoryType(),
        OldMMO->getBaseAlign(), OldMMO->getAAInfo(), OldMMO->getRanges(),
        OldMMO->getSyncScopeID(), OldMMO->getSuccessOrdering(),
        OldMMO->getFailureOrdering());
    NewMMOs.push_back(NewMMO);
  }

  DstMI.setMemRefs(DstMF, NewMMOs);
}

static std::unique_ptr<MachineFunction> cloneMF(MachineFunction *SrcMF,
                                                MachineModuleInfo &DestMMI) {
  auto DstMF = std::make_unique<MachineFunction>(
      SrcMF->getFunction(), SrcMF->getTarget(), SrcMF->getSubtarget(),
      SrcMF->getFunctionNumber(), DestMMI);
  DenseMap<MachineBasicBlock *, MachineBasicBlock *> Src2DstMBB;

  auto *SrcMRI = &SrcMF->getRegInfo();
  auto *DstMRI = &DstMF->getRegInfo();

  // Clone blocks.
  for (MachineBasicBlock &SrcMBB : *SrcMF) {
    MachineBasicBlock *DstMBB =
        DstMF->CreateMachineBasicBlock(SrcMBB.getBasicBlock());
    Src2DstMBB[&SrcMBB] = DstMBB;

    DstMBB->setCallFrameSize(SrcMBB.getCallFrameSize());

    if (SrcMBB.isIRBlockAddressTaken())
      DstMBB->setAddressTakenIRBlock(SrcMBB.getAddressTakenIRBlock());
    if (SrcMBB.isMachineBlockAddressTaken())
      DstMBB->setMachineBlockAddressTaken();

    // FIXME: This is not serialized
    if (SrcMBB.hasLabelMustBeEmitted())
      DstMBB->setLabelMustBeEmitted();

    DstMBB->setAlignment(SrcMBB.getAlignment());

    // FIXME: This is not serialized
    DstMBB->setMaxBytesForAlignment(SrcMBB.getMaxBytesForAlignment());

    DstMBB->setIsEHPad(SrcMBB.isEHPad());
    DstMBB->setIsEHScopeEntry(SrcMBB.isEHScopeEntry());
    DstMBB->setIsEHCatchretTarget(SrcMBB.isEHCatchretTarget());
    DstMBB->setIsEHFuncletEntry(SrcMBB.isEHFuncletEntry());

    // FIXME: These are not serialized
    DstMBB->setIsCleanupFuncletEntry(SrcMBB.isCleanupFuncletEntry());
    DstMBB->setIsBeginSection(SrcMBB.isBeginSection());
    DstMBB->setIsEndSection(SrcMBB.isEndSection());

    DstMBB->setSectionID(SrcMBB.getSectionID());
    DstMBB->setIsInlineAsmBrIndirectTarget(
        SrcMBB.isInlineAsmBrIndirectTarget());

    // FIXME: This is not serialized
    if (std::optional<uint64_t> Weight = SrcMBB.getIrrLoopHeaderWeight())
      DstMBB->setIrrLoopHeaderWeight(*Weight);
  }

  const MachineFrameInfo &SrcMFI = SrcMF->getFrameInfo();
  MachineFrameInfo &DstMFI = DstMF->getFrameInfo();

  // Copy stack objects and other info
  cloneFrameInfo(DstMFI, SrcMFI, Src2DstMBB);

  if (MachineJumpTableInfo *SrcJTI = SrcMF->getJumpTableInfo()) {
    cloneJumpTableInfo(*DstMF, *SrcJTI, Src2DstMBB);
  }

  // Remap the debug info frame index references.
  DstMF->VariableDbgInfos = SrcMF->VariableDbgInfos;

  // Clone virtual registers
  for (unsigned I = 0, E = SrcMRI->getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    Register NewReg = DstMRI->createIncompleteVirtualRegister(
      SrcMRI->getVRegName(Reg));
    assert(NewReg == Reg && "expected to preserve virtreg number");

    DstMRI->setRegClassOrRegBank(NewReg, SrcMRI->getRegClassOrRegBank(Reg));

    LLT RegTy = SrcMRI->getType(Reg);
    if (RegTy.isValid())
      DstMRI->setType(NewReg, RegTy);

    // Copy register allocation hints.
    const auto &Hints = SrcMRI->getRegAllocationHints(Reg);
    for (Register PrefReg : Hints.second)
      DstMRI->addRegAllocationHint(NewReg, PrefReg);
  }

  const TargetSubtargetInfo &STI = DstMF->getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  // Link blocks.
  for (auto &SrcMBB : *SrcMF) {
    auto *DstMBB = Src2DstMBB[&SrcMBB];
    DstMF->push_back(DstMBB);

    for (auto It = SrcMBB.succ_begin(), IterEnd = SrcMBB.succ_end();
         It != IterEnd; ++It) {
      auto *SrcSuccMBB = *It;
      auto *DstSuccMBB = Src2DstMBB[SrcSuccMBB];
      DstMBB->addSuccessor(DstSuccMBB, SrcMBB.getSuccProbability(It));
    }

    for (auto &LI : SrcMBB.liveins_dbg())
      DstMBB->addLiveIn(LI);

    // Make sure MRI knows about registers clobbered by unwinder.
    if (DstMBB->isEHPad()) {
      if (auto *RegMask = TRI->getCustomEHPadPreservedMask(*DstMF))
        DstMRI->addPhysRegsUsedFromRegMask(RegMask);
    }
  }

  DenseSet<const uint32_t *> ConstRegisterMasks;

  // Track predefined/named regmasks which we ignore.
  for (const uint32_t *Mask : TRI->getRegMasks())
    ConstRegisterMasks.insert(Mask);

  // Clone instructions.
  for (auto &SrcMBB : *SrcMF) {
    auto *DstMBB = Src2DstMBB[&SrcMBB];
    for (auto &SrcMI : SrcMBB) {
      const auto &MCID = TII->get(SrcMI.getOpcode());
      auto *DstMI = DstMF->CreateMachineInstr(MCID, SrcMI.getDebugLoc(),
                                              /*NoImplicit=*/true);
      DstMI->setFlags(SrcMI.getFlags());
      DstMI->setAsmPrinterFlag(SrcMI.getAsmPrinterFlags());

      DstMBB->push_back(DstMI);
      for (auto &SrcMO : SrcMI.operands()) {
        MachineOperand DstMO(SrcMO);
        DstMO.clearParent();

        // Update MBB.
        if (DstMO.isMBB())
          DstMO.setMBB(Src2DstMBB[DstMO.getMBB()]);
        else if (DstMO.isRegMask()) {
          DstMRI->addPhysRegsUsedFromRegMask(DstMO.getRegMask());

          if (!ConstRegisterMasks.count(DstMO.getRegMask())) {
            uint32_t *DstMask = DstMF->allocateRegMask();
            std::memcpy(DstMask, SrcMO.getRegMask(),
                        sizeof(*DstMask) *
                            MachineOperand::getRegMaskSize(TRI->getNumRegs()));
            DstMO.setRegMask(DstMask);
          }
        }

        DstMI->addOperand(DstMO);
      }

      cloneMemOperands(*DstMI, SrcMI, *SrcMF, *DstMF);
    }
  }

  DstMF->setAlignment(SrcMF->getAlignment());
  DstMF->setExposesReturnsTwice(SrcMF->exposesReturnsTwice());
  DstMF->setHasInlineAsm(SrcMF->hasInlineAsm());
  DstMF->setHasWinCFI(SrcMF->hasWinCFI());

  DstMF->getProperties().reset().set(SrcMF->getProperties());

  if (!SrcMF->getFrameInstructions().empty() ||
      !SrcMF->getLongjmpTargets().empty() ||
      !SrcMF->getCatchretTargets().empty())
    report_fatal_error("cloning not implemented for machine function property");

  DstMF->setCallsEHReturn(SrcMF->callsEHReturn());
  DstMF->setCallsUnwindInit(SrcMF->callsUnwindInit());
  DstMF->setHasEHCatchret(SrcMF->hasEHCatchret());
  DstMF->setHasEHScopes(SrcMF->hasEHScopes());
  DstMF->setHasEHFunclets(SrcMF->hasEHFunclets());
  DstMF->setIsOutlined(SrcMF->isOutlined());

  if (!SrcMF->getLandingPads().empty() ||
      !SrcMF->getCodeViewAnnotations().empty() ||
      !SrcMF->getTypeInfos().empty() ||
      !SrcMF->getFilterIds().empty() ||
      SrcMF->hasAnyWasmLandingPadIndex() ||
      SrcMF->hasAnyCallSiteLandingPad() ||
      SrcMF->hasAnyCallSiteLabel() ||
      !SrcMF->getCallSitesInfo().empty())
    report_fatal_error("cloning not implemented for machine function property");

  DstMF->setDebugInstrNumberingCount(SrcMF->DebugInstrNumberingCount);

  if (!DstMF->cloneInfoFrom(*SrcMF, Src2DstMBB))
    report_fatal_error("target does not implement MachineFunctionInfo cloning");

  DstMRI->freezeReservedRegs();

  DstMF->verify(nullptr, "", /*AbortOnError=*/true);
  return DstMF;
}

static void initializeTargetInfo() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

void ReducerWorkItem::print(raw_ostream &ROS, void *p) const {
  if (MMI) {
    printMIR(ROS, *M);
    for (Function &F : *M) {
      if (auto *MF = MMI->getMachineFunction(F))
        printMIR(ROS, *MF);
    }
  } else {
    M->print(ROS, /*AssemblyAnnotationWriter=*/nullptr,
             /*ShouldPreserveUseListOrder=*/true);
  }
}

bool ReducerWorkItem::verify(raw_fd_ostream *OS) const {
  if (verifyModule(*M, OS))
    return true;

  if (!MMI)
    return false;

  for (const Function &F : getModule()) {
    if (const MachineFunction *MF = MMI->getMachineFunction(F)) {
      if (!MF->verify(nullptr, "", /*AbortOnError=*/false))
        return true;
    }
  }

  return false;
}

bool ReducerWorkItem::isReduced(const TestRunner &Test) const {
  const bool UseBitcode = Test.inputIsBitcode() || TmpFilesAsBitcode;

  SmallString<128> CurrentFilepath;

  // Write ReducerWorkItem to tmp file
  int FD;
  std::error_code EC = sys::fs::createTemporaryFile(
      "llvm-reduce", isMIR() ? "mir" : (UseBitcode ? "bc" : "ll"), FD,
      CurrentFilepath,
      UseBitcode && !isMIR() ? sys::fs::OF_None : sys::fs::OF_Text);
  if (EC) {
    WithColor::error(errs(), Test.getToolName())
        << "error making unique filename: " << EC.message() << '\n';
    exit(1);
  }

  ToolOutputFile Out(CurrentFilepath, FD);

  writeOutput(Out.os(), UseBitcode);

  Out.os().close();
  if (Out.os().has_error()) {
    WithColor::error(errs(), Test.getToolName())
        << "error emitting bitcode to file '" << CurrentFilepath
        << "': " << Out.os().error().message() << '\n';
    exit(1);
  }

  // Current Chunks aren't interesting
  return Test.run(CurrentFilepath);
}

std::unique_ptr<ReducerWorkItem>
ReducerWorkItem::clone(const TargetMachine *TM) const {
  auto CloneMMM = std::make_unique<ReducerWorkItem>();
  if (TM) {
    // We're assuming the Module IR contents are always unchanged by MIR
    // reductions, and can share it as a constant.
    CloneMMM->M = M;

    // MachineModuleInfo contains a lot of other state used during codegen which
    // we won't be using here, but we should be able to ignore it (although this
    // is pretty ugly).
    const LLVMTargetMachine *LLVMTM =
        static_cast<const LLVMTargetMachine *>(TM);
    CloneMMM->MMI = std::make_unique<MachineModuleInfo>(LLVMTM);

    for (const Function &F : getModule()) {
      if (auto *MF = MMI->getMachineFunction(F))
        CloneMMM->MMI->insertFunction(F, cloneMF(MF, *CloneMMM->MMI));
    }
  } else {
    CloneMMM->M = CloneModule(*M);
  }
  return CloneMMM;
}

/// Try to produce some number that indicates a function is getting smaller /
/// simpler.
static uint64_t computeMIRComplexityScoreImpl(const MachineFunction &MF) {
  uint64_t Score = 0;
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Add for stack objects
  Score += MFI.getNumObjects();

  // Add in the block count.
  Score += 2 * MF.size();

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    Score += MRI.getRegAllocationHints(Reg).second.size();
  }

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      const unsigned Opc = MI.getOpcode();

      // Reductions may want or need to introduce implicit_defs, so don't count
      // them.
      // TODO: These probably should count in some way.
      if (Opc == TargetOpcode::IMPLICIT_DEF ||
          Opc == TargetOpcode::G_IMPLICIT_DEF)
        continue;

      // Each instruction adds to the score
      Score += 4;

      if (Opc == TargetOpcode::PHI || Opc == TargetOpcode::G_PHI ||
          Opc == TargetOpcode::INLINEASM || Opc == TargetOpcode::INLINEASM_BR)
        ++Score;

      if (MI.getFlags() != 0)
        ++Score;

      // Increase weight for more operands.
      for (const MachineOperand &MO : MI.operands()) {
        ++Score;

        // Treat registers as more complex.
        if (MO.isReg()) {
          ++Score;

          // And subregisters as even more complex.
          if (MO.getSubReg()) {
            ++Score;
            if (MO.isDef())
              ++Score;
          }
        } else if (MO.isRegMask())
          ++Score;
      }
    }
  }

  return Score;
}

uint64_t ReducerWorkItem::computeMIRComplexityScore() const {
  uint64_t Score = 0;

  for (const Function &F : getModule()) {
    if (auto *MF = MMI->getMachineFunction(F))
      Score += computeMIRComplexityScoreImpl(*MF);
  }

  return Score;
}

// FIXME: ReduceOperandsSkip has similar function, except it uses larger numbers
// for more reduced.
static unsigned classifyReductivePower(const Value *V) {
  if (auto *C = dyn_cast<ConstantData>(V)) {
    if (C->isNullValue())
      return 0;
    if (C->isOneValue())
      return 1;
    if (isa<UndefValue>(V))
      return 2;
    return 3;
  }

  if (isa<GlobalValue>(V))
    return 4;

  // TODO: Account for expression size
  if (isa<ConstantExpr>(V))
    return 5;

  if (isa<Constant>(V))
    return 1;

  if (isa<Argument>(V))
    return 6;

  if (isa<Instruction>(V))
    return 7;

  return 0;
}

// TODO: Additional flags and attributes may be complexity reducing. If we start
// adding flags and attributes, they could have negative cost.
static uint64_t computeIRComplexityScoreImpl(const Function &F) {
  uint64_t Score = 1; // Count the function itself
  SmallVector<std::pair<unsigned, MDNode *>> MDs;

  AttributeList Attrs = F.getAttributes();
  for (AttributeSet AttrSet : Attrs)
    Score += AttrSet.getNumAttributes();

  for (const BasicBlock &BB : F) {
    ++Score;

    for (const Instruction &I : BB) {
      ++Score;

      if (const auto *OverflowOp = dyn_cast<OverflowingBinaryOperator>(&I)) {
        if (OverflowOp->hasNoUnsignedWrap())
          ++Score;
        if (OverflowOp->hasNoSignedWrap())
          ++Score;
      } else if (const auto *GEP = dyn_cast<GEPOperator>(&I)) {
        if (GEP->isInBounds())
          ++Score;
      } else if (const auto *ExactOp = dyn_cast<PossiblyExactOperator>(&I)) {
        if (ExactOp->isExact())
          ++Score;
      } else if (const auto *FPOp = dyn_cast<FPMathOperator>(&I)) {
        FastMathFlags FMF = FPOp->getFastMathFlags();
        if (FMF.allowReassoc())
          ++Score;
        if (FMF.noNaNs())
          ++Score;
        if (FMF.noInfs())
          ++Score;
        if (FMF.noSignedZeros())
          ++Score;
        if (FMF.allowReciprocal())
          ++Score;
        if (FMF.allowContract())
          ++Score;
        if (FMF.approxFunc())
          ++Score;
      }

      for (const Value *Operand : I.operands()) {
        ++Score;
        Score += classifyReductivePower(Operand);
      }

      I.getAllMetadata(MDs);
      Score += MDs.size();
      MDs.clear();
    }
  }

  return Score;
}

uint64_t ReducerWorkItem::computeIRComplexityScore() const {
  uint64_t Score = 0;

  const Module &M = getModule();
  Score += M.named_metadata_size();

  SmallVector<std::pair<unsigned, MDNode *>, 32> GlobalMetadata;
  for (const GlobalVariable &GV : M.globals()) {
    ++Score;

    if (GV.hasInitializer())
      Score += classifyReductivePower(GV.getInitializer());

    // TODO: Account for linkage?

    GV.getAllMetadata(GlobalMetadata);
    Score += GlobalMetadata.size();
    GlobalMetadata.clear();
  }

  for (const GlobalAlias &GA : M.aliases())
    Score += classifyReductivePower(GA.getAliasee());

  for (const GlobalIFunc &GI : M.ifuncs())
    Score += classifyReductivePower(GI.getResolver());

  for (const Function &F : M)
    Score += computeIRComplexityScoreImpl(F);

  return Score;
}

void ReducerWorkItem::writeOutput(raw_ostream &OS, bool EmitBitcode) const {
  // Requesting bitcode emission with mir is nonsense, so just ignore it.
  if (EmitBitcode && !isMIR())
    writeBitcode(OS);
  else
    print(OS, /*AnnotationWriter=*/nullptr);
}

void ReducerWorkItem::readBitcode(MemoryBufferRef Data, LLVMContext &Ctx,
                                  StringRef ToolName) {
  Expected<BitcodeFileContents> IF = llvm::getBitcodeFileContents(Data);
  if (!IF) {
    WithColor::error(errs(), ToolName) << IF.takeError();
    exit(1);
  }
  BitcodeModule BM = IF->Mods[0];
  Expected<BitcodeLTOInfo> LI = BM.getLTOInfo();
  Expected<std::unique_ptr<Module>> MOrErr = BM.parseModule(Ctx);
  if (!LI || !MOrErr) {
    WithColor::error(errs(), ToolName) << IF.takeError();
    exit(1);
  }
  LTOInfo = std::make_unique<BitcodeLTOInfo>(*LI);
  M = std::move(MOrErr.get());
}

void ReducerWorkItem::writeBitcode(raw_ostream &OutStream) const {
  if (LTOInfo && LTOInfo->IsThinLTO && LTOInfo->EnableSplitLTOUnit) {
    PassBuilder PB;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    ModulePassManager MPM;
    MPM.addPass(ThinLTOBitcodeWriterPass(OutStream, nullptr));
    MPM.run(*M, MAM);
  } else {
    std::unique_ptr<ModuleSummaryIndex> Index;
    if (LTOInfo && LTOInfo->HasSummary) {
      ProfileSummaryInfo PSI(*M);
      Index = std::make_unique<ModuleSummaryIndex>(
          buildModuleSummaryIndex(*M, nullptr, &PSI));
    }
    WriteBitcodeToFile(getModule(), OutStream,
                       /*ShouldPreserveUseListOrder=*/true, Index.get());
  }
}

std::pair<std::unique_ptr<ReducerWorkItem>, bool>
llvm::parseReducerWorkItem(StringRef ToolName, StringRef Filename,
                           LLVMContext &Ctxt,
                           std::unique_ptr<TargetMachine> &TM, bool IsMIR) {
  bool IsBitcode = false;
  Triple TheTriple;

  auto MMM = std::make_unique<ReducerWorkItem>();

  if (IsMIR) {
    initializeTargetInfo();

    auto FileOrErr = MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/true);
    if (std::error_code EC = FileOrErr.getError()) {
      WithColor::error(errs(), ToolName) << EC.message() << '\n';
      return {nullptr, false};
    }

    std::unique_ptr<MIRParser> MParser =
        createMIRParser(std::move(FileOrErr.get()), Ctxt);

    auto SetDataLayout = [&](StringRef DataLayoutTargetTriple,
                             StringRef OldDLStr) -> std::optional<std::string> {
      // NB: We always call createTargetMachineForTriple() even if an explicit
      // DataLayout is already set in the module since we want to use this
      // callback to setup the TargetMachine rather than doing it later.
      std::string IRTargetTriple = DataLayoutTargetTriple.str();
      if (!TargetTriple.empty())
        IRTargetTriple = Triple::normalize(TargetTriple);
      TheTriple = Triple(IRTargetTriple);
      if (TheTriple.getTriple().empty())
        TheTriple.setTriple(sys::getDefaultTargetTriple());
      ExitOnError ExitOnErr(std::string(ToolName) + ": error: ");
      TM = ExitOnErr(codegen::createTargetMachineForTriple(TheTriple.str()));

      return TM->createDataLayout().getStringRepresentation();
    };

    std::unique_ptr<Module> M = MParser->parseIRModule(SetDataLayout);
    LLVMTargetMachine *LLVMTM = static_cast<LLVMTargetMachine *>(TM.get());

    MMM->MMI = std::make_unique<MachineModuleInfo>(LLVMTM);
    MParser->parseMachineFunctions(*M, *MMM->MMI);
    MMM->M = std::move(M);
  } else {
    SMDiagnostic Err;
    ErrorOr<std::unique_ptr<MemoryBuffer>> MB =
        MemoryBuffer::getFileOrSTDIN(Filename);
    if (std::error_code EC = MB.getError()) {
      WithColor::error(errs(), ToolName)
          << Filename << ": " << EC.message() << "\n";
      return {nullptr, false};
    }

    if (!isBitcode((const unsigned char *)(*MB)->getBufferStart(),
                   (const unsigned char *)(*MB)->getBufferEnd())) {
      std::unique_ptr<Module> Result = parseIR(**MB, Err, Ctxt);
      if (!Result) {
        Err.print(ToolName.data(), errs());
        return {nullptr, false};
      }
      MMM->M = std::move(Result);
    } else {
      IsBitcode = true;
      MMM->readBitcode(MemoryBufferRef(**MB), Ctxt, ToolName);

      if (MMM->LTOInfo->IsThinLTO && MMM->LTOInfo->EnableSplitLTOUnit)
        initializeTargetInfo();
    }
  }
  if (MMM->verify(&errs())) {
    WithColor::error(errs(), ToolName)
        << Filename << " - input module is broken!\n";
    return {nullptr, false};
  }
  return {std::move(MMM), IsBitcode};
}
