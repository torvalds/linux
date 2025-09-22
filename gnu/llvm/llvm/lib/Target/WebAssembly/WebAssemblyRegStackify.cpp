//===-- WebAssemblyRegStackify.cpp - Register Stackification --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a register stacking pass.
///
/// This pass reorders instructions to put register uses and defs in an order
/// such that they form single-use expression trees. Registers fitting this form
/// are then marked as "stackified", meaning references to them are replaced by
/// "push" and "pop" from the value stack.
///
/// This is primarily a code size optimization, since temporary values on the
/// value stack don't need to be named.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h" // for WebAssembly::ARGUMENT_*
#include "WebAssembly.h"
#include "WebAssemblyDebugValueManager.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-stackify"

namespace {
class WebAssemblyRegStackify final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Register Stackify";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyRegStackify() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyRegStackify::ID = 0;
INITIALIZE_PASS(WebAssemblyRegStackify, DEBUG_TYPE,
                "Reorder instructions to use the WebAssembly value stack",
                false, false)

FunctionPass *llvm::createWebAssemblyRegStackify() {
  return new WebAssemblyRegStackify();
}

// Decorate the given instruction with implicit operands that enforce the
// expression stack ordering constraints for an instruction which is on
// the expression stack.
static void imposeStackOrdering(MachineInstr *MI) {
  // Write the opaque VALUE_STACK register.
  if (!MI->definesRegister(WebAssembly::VALUE_STACK, /*TRI=*/nullptr))
    MI->addOperand(MachineOperand::CreateReg(WebAssembly::VALUE_STACK,
                                             /*isDef=*/true,
                                             /*isImp=*/true));

  // Also read the opaque VALUE_STACK register.
  if (!MI->readsRegister(WebAssembly::VALUE_STACK, /*TRI=*/nullptr))
    MI->addOperand(MachineOperand::CreateReg(WebAssembly::VALUE_STACK,
                                             /*isDef=*/false,
                                             /*isImp=*/true));
}

// Convert an IMPLICIT_DEF instruction into an instruction which defines
// a constant zero value.
static void convertImplicitDefToConstZero(MachineInstr *MI,
                                          MachineRegisterInfo &MRI,
                                          const TargetInstrInfo *TII,
                                          MachineFunction &MF,
                                          LiveIntervals &LIS) {
  assert(MI->getOpcode() == TargetOpcode::IMPLICIT_DEF);

  const auto *RegClass = MRI.getRegClass(MI->getOperand(0).getReg());
  if (RegClass == &WebAssembly::I32RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_I32));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else if (RegClass == &WebAssembly::I64RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_I64));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else if (RegClass == &WebAssembly::F32RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_F32));
    auto *Val = cast<ConstantFP>(Constant::getNullValue(
        Type::getFloatTy(MF.getFunction().getContext())));
    MI->addOperand(MachineOperand::CreateFPImm(Val));
  } else if (RegClass == &WebAssembly::F64RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_F64));
    auto *Val = cast<ConstantFP>(Constant::getNullValue(
        Type::getDoubleTy(MF.getFunction().getContext())));
    MI->addOperand(MachineOperand::CreateFPImm(Val));
  } else if (RegClass == &WebAssembly::V128RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_V128_I64x2));
    MI->addOperand(MachineOperand::CreateImm(0));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else {
    llvm_unreachable("Unexpected reg class");
  }
}

// Determine whether a call to the callee referenced by
// MI->getOperand(CalleeOpNo) reads memory, writes memory, and/or has side
// effects.
static void queryCallee(const MachineInstr &MI, bool &Read, bool &Write,
                        bool &Effects, bool &StackPointer) {
  // All calls can use the stack pointer.
  StackPointer = true;

  const MachineOperand &MO = WebAssembly::getCalleeOp(MI);
  if (MO.isGlobal()) {
    const Constant *GV = MO.getGlobal();
    if (const auto *GA = dyn_cast<GlobalAlias>(GV))
      if (!GA->isInterposable())
        GV = GA->getAliasee();

    if (const auto *F = dyn_cast<Function>(GV)) {
      if (!F->doesNotThrow())
        Effects = true;
      if (F->doesNotAccessMemory())
        return;
      if (F->onlyReadsMemory()) {
        Read = true;
        return;
      }
    }
  }

  // Assume the worst.
  Write = true;
  Read = true;
  Effects = true;
}

// Determine whether MI reads memory, writes memory, has side effects,
// and/or uses the stack pointer value.
static void query(const MachineInstr &MI, bool &Read, bool &Write,
                  bool &Effects, bool &StackPointer) {
  assert(!MI.isTerminator());

  if (MI.isDebugInstr() || MI.isPosition())
    return;

  // Check for loads.
  if (MI.mayLoad() && !MI.isDereferenceableInvariantLoad())
    Read = true;

  // Check for stores.
  if (MI.mayStore()) {
    Write = true;
  } else if (MI.hasOrderedMemoryRef()) {
    switch (MI.getOpcode()) {
    case WebAssembly::DIV_S_I32:
    case WebAssembly::DIV_S_I64:
    case WebAssembly::REM_S_I32:
    case WebAssembly::REM_S_I64:
    case WebAssembly::DIV_U_I32:
    case WebAssembly::DIV_U_I64:
    case WebAssembly::REM_U_I32:
    case WebAssembly::REM_U_I64:
    case WebAssembly::I32_TRUNC_S_F32:
    case WebAssembly::I64_TRUNC_S_F32:
    case WebAssembly::I32_TRUNC_S_F64:
    case WebAssembly::I64_TRUNC_S_F64:
    case WebAssembly::I32_TRUNC_U_F32:
    case WebAssembly::I64_TRUNC_U_F32:
    case WebAssembly::I32_TRUNC_U_F64:
    case WebAssembly::I64_TRUNC_U_F64:
      // These instruction have hasUnmodeledSideEffects() returning true
      // because they trap on overflow and invalid so they can't be arbitrarily
      // moved, however hasOrderedMemoryRef() interprets this plus their lack
      // of memoperands as having a potential unknown memory reference.
      break;
    default:
      // Record volatile accesses, unless it's a call, as calls are handled
      // specially below.
      if (!MI.isCall()) {
        Write = true;
        Effects = true;
      }
      break;
    }
  }

  // Check for side effects.
  if (MI.hasUnmodeledSideEffects()) {
    switch (MI.getOpcode()) {
    case WebAssembly::DIV_S_I32:
    case WebAssembly::DIV_S_I64:
    case WebAssembly::REM_S_I32:
    case WebAssembly::REM_S_I64:
    case WebAssembly::DIV_U_I32:
    case WebAssembly::DIV_U_I64:
    case WebAssembly::REM_U_I32:
    case WebAssembly::REM_U_I64:
    case WebAssembly::I32_TRUNC_S_F32:
    case WebAssembly::I64_TRUNC_S_F32:
    case WebAssembly::I32_TRUNC_S_F64:
    case WebAssembly::I64_TRUNC_S_F64:
    case WebAssembly::I32_TRUNC_U_F32:
    case WebAssembly::I64_TRUNC_U_F32:
    case WebAssembly::I32_TRUNC_U_F64:
    case WebAssembly::I64_TRUNC_U_F64:
      // These instructions have hasUnmodeledSideEffects() returning true
      // because they trap on overflow and invalid so they can't be arbitrarily
      // moved, however in the specific case of register stackifying, it is safe
      // to move them because overflow and invalid are Undefined Behavior.
      break;
    default:
      Effects = true;
      break;
    }
  }

  // Check for writes to __stack_pointer global.
  if ((MI.getOpcode() == WebAssembly::GLOBAL_SET_I32 ||
       MI.getOpcode() == WebAssembly::GLOBAL_SET_I64) &&
      strcmp(MI.getOperand(0).getSymbolName(), "__stack_pointer") == 0)
    StackPointer = true;

  // Analyze calls.
  if (MI.isCall()) {
    queryCallee(MI, Read, Write, Effects, StackPointer);
  }
}

// Test whether Def is safe and profitable to rematerialize.
static bool shouldRematerialize(const MachineInstr &Def,
                                const WebAssemblyInstrInfo *TII) {
  return Def.isAsCheapAsAMove() && TII->isTriviallyReMaterializable(Def);
}

// Identify the definition for this register at this point. This is a
// generalization of MachineRegisterInfo::getUniqueVRegDef that uses
// LiveIntervals to handle complex cases.
static MachineInstr *getVRegDef(unsigned Reg, const MachineInstr *Insert,
                                const MachineRegisterInfo &MRI,
                                const LiveIntervals &LIS) {
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MachineInstr *Def = MRI.getUniqueVRegDef(Reg))
    return Def;

  // MRI doesn't know what the Def is. Try asking LIS.
  if (const VNInfo *ValNo = LIS.getInterval(Reg).getVNInfoBefore(
          LIS.getInstructionIndex(*Insert)))
    return LIS.getInstructionFromIndex(ValNo->def);

  return nullptr;
}

// Test whether Reg, as defined at Def, has exactly one use. This is a
// generalization of MachineRegisterInfo::hasOneNonDBGUse that uses
// LiveIntervals to handle complex cases.
static bool hasOneNonDBGUse(unsigned Reg, MachineInstr *Def,
                            MachineRegisterInfo &MRI, MachineDominatorTree &MDT,
                            LiveIntervals &LIS) {
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MRI.hasOneNonDBGUse(Reg))
    return true;

  bool HasOne = false;
  const LiveInterval &LI = LIS.getInterval(Reg);
  const VNInfo *DefVNI =
      LI.getVNInfoAt(LIS.getInstructionIndex(*Def).getRegSlot());
  assert(DefVNI);
  for (auto &I : MRI.use_nodbg_operands(Reg)) {
    const auto &Result = LI.Query(LIS.getInstructionIndex(*I.getParent()));
    if (Result.valueIn() == DefVNI) {
      if (!Result.isKill())
        return false;
      if (HasOne)
        return false;
      HasOne = true;
    }
  }
  return HasOne;
}

// Test whether it's safe to move Def to just before Insert.
// TODO: Compute memory dependencies in a way that doesn't require always
// walking the block.
// TODO: Compute memory dependencies in a way that uses AliasAnalysis to be
// more precise.
static bool isSafeToMove(const MachineOperand *Def, const MachineOperand *Use,
                         const MachineInstr *Insert,
                         const WebAssemblyFunctionInfo &MFI,
                         const MachineRegisterInfo &MRI) {
  const MachineInstr *DefI = Def->getParent();
  const MachineInstr *UseI = Use->getParent();
  assert(DefI->getParent() == Insert->getParent());
  assert(UseI->getParent() == Insert->getParent());

  // The first def of a multivalue instruction can be stackified by moving,
  // since the later defs can always be placed into locals if necessary. Later
  // defs can only be stackified if all previous defs are already stackified
  // since ExplicitLocals will not know how to place a def in a local if a
  // subsequent def is stackified. But only one def can be stackified by moving
  // the instruction, so it must be the first one.
  //
  // TODO: This could be loosened to be the first *live* def, but care would
  // have to be taken to ensure the drops of the initial dead defs can be
  // placed. This would require checking that no previous defs are used in the
  // same instruction as subsequent defs.
  if (Def != DefI->defs().begin())
    return false;

  // If any subsequent def is used prior to the current value by the same
  // instruction in which the current value is used, we cannot
  // stackify. Stackifying in this case would require that def moving below the
  // current def in the stack, which cannot be achieved, even with locals.
  // Also ensure we don't sink the def past any other prior uses.
  for (const auto &SubsequentDef : drop_begin(DefI->defs())) {
    auto I = std::next(MachineBasicBlock::const_iterator(DefI));
    auto E = std::next(MachineBasicBlock::const_iterator(UseI));
    for (; I != E; ++I) {
      for (const auto &PriorUse : I->uses()) {
        if (&PriorUse == Use)
          break;
        if (PriorUse.isReg() && SubsequentDef.getReg() == PriorUse.getReg())
          return false;
      }
    }
  }

  // If moving is a semantic nop, it is always allowed
  const MachineBasicBlock *MBB = DefI->getParent();
  auto NextI = std::next(MachineBasicBlock::const_iterator(DefI));
  for (auto E = MBB->end(); NextI != E && NextI->isDebugInstr(); ++NextI)
    ;
  if (NextI == Insert)
    return true;

  // 'catch' and 'catch_all' should be the first instruction of a BB and cannot
  // move.
  if (WebAssembly::isCatch(DefI->getOpcode()))
    return false;

  // Check for register dependencies.
  SmallVector<unsigned, 4> MutableRegisters;
  for (const MachineOperand &MO : DefI->operands()) {
    if (!MO.isReg() || MO.isUndef())
      continue;
    Register Reg = MO.getReg();

    // If the register is dead here and at Insert, ignore it.
    if (MO.isDead() && Insert->definesRegister(Reg, /*TRI=*/nullptr) &&
        !Insert->readsRegister(Reg, /*TRI=*/nullptr))
      continue;

    if (Reg.isPhysical()) {
      // Ignore ARGUMENTS; it's just used to keep the ARGUMENT_* instructions
      // from moving down, and we've already checked for that.
      if (Reg == WebAssembly::ARGUMENTS)
        continue;
      // If the physical register is never modified, ignore it.
      if (!MRI.isPhysRegModified(Reg))
        continue;
      // Otherwise, it's a physical register with unknown liveness.
      return false;
    }

    // If one of the operands isn't in SSA form, it has different values at
    // different times, and we need to make sure we don't move our use across
    // a different def.
    if (!MO.isDef() && !MRI.hasOneDef(Reg))
      MutableRegisters.push_back(Reg);
  }

  bool Read = false, Write = false, Effects = false, StackPointer = false;
  query(*DefI, Read, Write, Effects, StackPointer);

  // If the instruction does not access memory and has no side effects, it has
  // no additional dependencies.
  bool HasMutableRegisters = !MutableRegisters.empty();
  if (!Read && !Write && !Effects && !StackPointer && !HasMutableRegisters)
    return true;

  // Scan through the intervening instructions between DefI and Insert.
  MachineBasicBlock::const_iterator D(DefI), I(Insert);
  for (--I; I != D; --I) {
    bool InterveningRead = false;
    bool InterveningWrite = false;
    bool InterveningEffects = false;
    bool InterveningStackPointer = false;
    query(*I, InterveningRead, InterveningWrite, InterveningEffects,
          InterveningStackPointer);
    if (Effects && InterveningEffects)
      return false;
    if (Read && InterveningWrite)
      return false;
    if (Write && (InterveningRead || InterveningWrite))
      return false;
    if (StackPointer && InterveningStackPointer)
      return false;

    for (unsigned Reg : MutableRegisters)
      for (const MachineOperand &MO : I->operands())
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
          return false;
  }

  return true;
}

/// Test whether OneUse, a use of Reg, dominates all of Reg's other uses.
static bool oneUseDominatesOtherUses(unsigned Reg, const MachineOperand &OneUse,
                                     const MachineBasicBlock &MBB,
                                     const MachineRegisterInfo &MRI,
                                     const MachineDominatorTree &MDT,
                                     LiveIntervals &LIS,
                                     WebAssemblyFunctionInfo &MFI) {
  const LiveInterval &LI = LIS.getInterval(Reg);

  const MachineInstr *OneUseInst = OneUse.getParent();
  VNInfo *OneUseVNI = LI.getVNInfoBefore(LIS.getInstructionIndex(*OneUseInst));

  for (const MachineOperand &Use : MRI.use_nodbg_operands(Reg)) {
    if (&Use == &OneUse)
      continue;

    const MachineInstr *UseInst = Use.getParent();
    VNInfo *UseVNI = LI.getVNInfoBefore(LIS.getInstructionIndex(*UseInst));

    if (UseVNI != OneUseVNI)
      continue;

    if (UseInst == OneUseInst) {
      // Another use in the same instruction. We need to ensure that the one
      // selected use happens "before" it.
      if (&OneUse > &Use)
        return false;
    } else {
      // Test that the use is dominated by the one selected use.
      while (!MDT.dominates(OneUseInst, UseInst)) {
        // Actually, dominating is over-conservative. Test that the use would
        // happen after the one selected use in the stack evaluation order.
        //
        // This is needed as a consequence of using implicit local.gets for
        // uses and implicit local.sets for defs.
        if (UseInst->getDesc().getNumDefs() == 0)
          return false;
        const MachineOperand &MO = UseInst->getOperand(0);
        if (!MO.isReg())
          return false;
        Register DefReg = MO.getReg();
        if (!DefReg.isVirtual() || !MFI.isVRegStackified(DefReg))
          return false;
        assert(MRI.hasOneNonDBGUse(DefReg));
        const MachineOperand &NewUse = *MRI.use_nodbg_begin(DefReg);
        const MachineInstr *NewUseInst = NewUse.getParent();
        if (NewUseInst == OneUseInst) {
          if (&OneUse > &NewUse)
            return false;
          break;
        }
        UseInst = NewUseInst;
      }
    }
  }
  return true;
}

/// Get the appropriate tee opcode for the given register class.
static unsigned getTeeOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::TEE_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::TEE_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::TEE_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::TEE_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::TEE_V128;
  if (RC == &WebAssembly::EXTERNREFRegClass)
    return WebAssembly::TEE_EXTERNREF;
  if (RC == &WebAssembly::FUNCREFRegClass)
    return WebAssembly::TEE_FUNCREF;
  if (RC == &WebAssembly::EXNREFRegClass)
    return WebAssembly::TEE_EXNREF;
  llvm_unreachable("Unexpected register class");
}

// Shrink LI to its uses, cleaning up LI.
static void shrinkToUses(LiveInterval &LI, LiveIntervals &LIS) {
  if (LIS.shrinkToUses(&LI)) {
    SmallVector<LiveInterval *, 4> SplitLIs;
    LIS.splitSeparateComponents(LI, SplitLIs);
  }
}

/// A single-use def in the same block with no intervening memory or register
/// dependencies; move the def down and nest it with the current instruction.
static MachineInstr *moveForSingleUse(unsigned Reg, MachineOperand &Op,
                                      MachineInstr *Def, MachineBasicBlock &MBB,
                                      MachineInstr *Insert, LiveIntervals &LIS,
                                      WebAssemblyFunctionInfo &MFI,
                                      MachineRegisterInfo &MRI) {
  LLVM_DEBUG(dbgs() << "Move for single use: "; Def->dump());

  WebAssemblyDebugValueManager DefDIs(Def);
  DefDIs.sink(Insert);
  LIS.handleMove(*Def);

  if (MRI.hasOneDef(Reg) && MRI.hasOneNonDBGUse(Reg)) {
    // No one else is using this register for anything so we can just stackify
    // it in place.
    MFI.stackifyVReg(MRI, Reg);
  } else {
    // The register may have unrelated uses or defs; create a new register for
    // just our one def and use so that we can stackify it.
    Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(Reg));
    Op.setReg(NewReg);
    DefDIs.updateReg(NewReg);

    // Tell LiveIntervals about the new register.
    LIS.createAndComputeVirtRegInterval(NewReg);

    // Tell LiveIntervals about the changes to the old register.
    LiveInterval &LI = LIS.getInterval(Reg);
    LI.removeSegment(LIS.getInstructionIndex(*Def).getRegSlot(),
                     LIS.getInstructionIndex(*Op.getParent()).getRegSlot(),
                     /*RemoveDeadValNo=*/true);

    MFI.stackifyVReg(MRI, NewReg);

    LLVM_DEBUG(dbgs() << " - Replaced register: "; Def->dump());
  }

  imposeStackOrdering(Def);
  return Def;
}

static MachineInstr *getPrevNonDebugInst(MachineInstr *MI) {
  for (auto *I = MI->getPrevNode(); I; I = I->getPrevNode())
    if (!I->isDebugInstr())
      return I;
  return nullptr;
}

/// A trivially cloneable instruction; clone it and nest the new copy with the
/// current instruction.
static MachineInstr *rematerializeCheapDef(
    unsigned Reg, MachineOperand &Op, MachineInstr &Def, MachineBasicBlock &MBB,
    MachineBasicBlock::instr_iterator Insert, LiveIntervals &LIS,
    WebAssemblyFunctionInfo &MFI, MachineRegisterInfo &MRI,
    const WebAssemblyInstrInfo *TII, const WebAssemblyRegisterInfo *TRI) {
  LLVM_DEBUG(dbgs() << "Rematerializing cheap def: "; Def.dump());
  LLVM_DEBUG(dbgs() << " - for use in "; Op.getParent()->dump());

  WebAssemblyDebugValueManager DefDIs(&Def);

  Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(Reg));
  DefDIs.cloneSink(&*Insert, NewReg);
  Op.setReg(NewReg);
  MachineInstr *Clone = getPrevNonDebugInst(&*Insert);
  assert(Clone);
  LIS.InsertMachineInstrInMaps(*Clone);
  LIS.createAndComputeVirtRegInterval(NewReg);
  MFI.stackifyVReg(MRI, NewReg);
  imposeStackOrdering(Clone);

  LLVM_DEBUG(dbgs() << " - Cloned to "; Clone->dump());

  // Shrink the interval.
  bool IsDead = MRI.use_empty(Reg);
  if (!IsDead) {
    LiveInterval &LI = LIS.getInterval(Reg);
    shrinkToUses(LI, LIS);
    IsDead = !LI.liveAt(LIS.getInstructionIndex(Def).getDeadSlot());
  }

  // If that was the last use of the original, delete the original.
  if (IsDead) {
    LLVM_DEBUG(dbgs() << " - Deleting original\n");
    SlotIndex Idx = LIS.getInstructionIndex(Def).getRegSlot();
    LIS.removePhysRegDefAt(MCRegister::from(WebAssembly::ARGUMENTS), Idx);
    LIS.removeInterval(Reg);
    LIS.RemoveMachineInstrFromMaps(Def);
    DefDIs.removeDef();
  }

  return Clone;
}

/// A multiple-use def in the same block with no intervening memory or register
/// dependencies; move the def down, nest it with the current instruction, and
/// insert a tee to satisfy the rest of the uses. As an illustration, rewrite
/// this:
///
///    Reg = INST ...        // Def
///    INST ..., Reg, ...    // Insert
///    INST ..., Reg, ...
///    INST ..., Reg, ...
///
/// to this:
///
///    DefReg = INST ...     // Def (to become the new Insert)
///    TeeReg, Reg = TEE_... DefReg
///    INST ..., TeeReg, ... // Insert
///    INST ..., Reg, ...
///    INST ..., Reg, ...
///
/// with DefReg and TeeReg stackified. This eliminates a local.get from the
/// resulting code.
static MachineInstr *moveAndTeeForMultiUse(
    unsigned Reg, MachineOperand &Op, MachineInstr *Def, MachineBasicBlock &MBB,
    MachineInstr *Insert, LiveIntervals &LIS, WebAssemblyFunctionInfo &MFI,
    MachineRegisterInfo &MRI, const WebAssemblyInstrInfo *TII) {
  LLVM_DEBUG(dbgs() << "Move and tee for multi-use:"; Def->dump());

  const auto *RegClass = MRI.getRegClass(Reg);
  Register TeeReg = MRI.createVirtualRegister(RegClass);
  Register DefReg = MRI.createVirtualRegister(RegClass);

  // Move Def into place.
  WebAssemblyDebugValueManager DefDIs(Def);
  DefDIs.sink(Insert);
  LIS.handleMove(*Def);

  // Create the Tee and attach the registers.
  MachineOperand &DefMO = Def->getOperand(0);
  MachineInstr *Tee = BuildMI(MBB, Insert, Insert->getDebugLoc(),
                              TII->get(getTeeOpcode(RegClass)), TeeReg)
                          .addReg(Reg, RegState::Define)
                          .addReg(DefReg, getUndefRegState(DefMO.isDead()));
  Op.setReg(TeeReg);
  DefDIs.updateReg(DefReg);
  SlotIndex TeeIdx = LIS.InsertMachineInstrInMaps(*Tee).getRegSlot();
  SlotIndex DefIdx = LIS.getInstructionIndex(*Def).getRegSlot();

  // Tell LiveIntervals we moved the original vreg def from Def to Tee.
  LiveInterval &LI = LIS.getInterval(Reg);
  LiveInterval::iterator I = LI.FindSegmentContaining(DefIdx);
  VNInfo *ValNo = LI.getVNInfoAt(DefIdx);
  I->start = TeeIdx;
  ValNo->def = TeeIdx;
  shrinkToUses(LI, LIS);

  // Finish stackifying the new regs.
  LIS.createAndComputeVirtRegInterval(TeeReg);
  LIS.createAndComputeVirtRegInterval(DefReg);
  MFI.stackifyVReg(MRI, DefReg);
  MFI.stackifyVReg(MRI, TeeReg);
  imposeStackOrdering(Def);
  imposeStackOrdering(Tee);

  // Even though 'TeeReg, Reg = TEE ...', has two defs, we don't need to clone
  // DBG_VALUEs for both of them, given that the latter will cancel the former
  // anyway. Here we only clone DBG_VALUEs for TeeReg, which will be converted
  // to a local index in ExplicitLocals pass.
  DefDIs.cloneSink(Insert, TeeReg, /* CloneDef */ false);

  LLVM_DEBUG(dbgs() << " - Replaced register: "; Def->dump());
  LLVM_DEBUG(dbgs() << " - Tee instruction: "; Tee->dump());
  return Def;
}

namespace {
/// A stack for walking the tree of instructions being built, visiting the
/// MachineOperands in DFS order.
class TreeWalkerState {
  using mop_iterator = MachineInstr::mop_iterator;
  using mop_reverse_iterator = std::reverse_iterator<mop_iterator>;
  using RangeTy = iterator_range<mop_reverse_iterator>;
  SmallVector<RangeTy, 4> Worklist;

public:
  explicit TreeWalkerState(MachineInstr *Insert) {
    const iterator_range<mop_iterator> &Range = Insert->explicit_uses();
    if (!Range.empty())
      Worklist.push_back(reverse(Range));
  }

  bool done() const { return Worklist.empty(); }

  MachineOperand &pop() {
    RangeTy &Range = Worklist.back();
    MachineOperand &Op = *Range.begin();
    Range = drop_begin(Range);
    if (Range.empty())
      Worklist.pop_back();
    assert((Worklist.empty() || !Worklist.back().empty()) &&
           "Empty ranges shouldn't remain in the worklist");
    return Op;
  }

  /// Push Instr's operands onto the stack to be visited.
  void pushOperands(MachineInstr *Instr) {
    const iterator_range<mop_iterator> &Range(Instr->explicit_uses());
    if (!Range.empty())
      Worklist.push_back(reverse(Range));
  }

  /// Some of Instr's operands are on the top of the stack; remove them and
  /// re-insert them starting from the beginning (because we've commuted them).
  void resetTopOperands(MachineInstr *Instr) {
    assert(hasRemainingOperands(Instr) &&
           "Reseting operands should only be done when the instruction has "
           "an operand still on the stack");
    Worklist.back() = reverse(Instr->explicit_uses());
  }

  /// Test whether Instr has operands remaining to be visited at the top of
  /// the stack.
  bool hasRemainingOperands(const MachineInstr *Instr) const {
    if (Worklist.empty())
      return false;
    const RangeTy &Range = Worklist.back();
    return !Range.empty() && Range.begin()->getParent() == Instr;
  }

  /// Test whether the given register is present on the stack, indicating an
  /// operand in the tree that we haven't visited yet. Moving a definition of
  /// Reg to a point in the tree after that would change its value.
  ///
  /// This is needed as a consequence of using implicit local.gets for
  /// uses and implicit local.sets for defs.
  bool isOnStack(unsigned Reg) const {
    for (const RangeTy &Range : Worklist)
      for (const MachineOperand &MO : Range)
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
    return false;
  }
};

/// State to keep track of whether commuting is in flight or whether it's been
/// tried for the current instruction and didn't work.
class CommutingState {
  /// There are effectively three states: the initial state where we haven't
  /// started commuting anything and we don't know anything yet, the tentative
  /// state where we've commuted the operands of the current instruction and are
  /// revisiting it, and the declined state where we've reverted the operands
  /// back to their original order and will no longer commute it further.
  bool TentativelyCommuting = false;
  bool Declined = false;

  /// During the tentative state, these hold the operand indices of the commuted
  /// operands.
  unsigned Operand0, Operand1;

public:
  /// Stackification for an operand was not successful due to ordering
  /// constraints. If possible, and if we haven't already tried it and declined
  /// it, commute Insert's operands and prepare to revisit it.
  void maybeCommute(MachineInstr *Insert, TreeWalkerState &TreeWalker,
                    const WebAssemblyInstrInfo *TII) {
    if (TentativelyCommuting) {
      assert(!Declined &&
             "Don't decline commuting until you've finished trying it");
      // Commuting didn't help. Revert it.
      TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
      TentativelyCommuting = false;
      Declined = true;
    } else if (!Declined && TreeWalker.hasRemainingOperands(Insert)) {
      Operand0 = TargetInstrInfo::CommuteAnyOperandIndex;
      Operand1 = TargetInstrInfo::CommuteAnyOperandIndex;
      if (TII->findCommutedOpIndices(*Insert, Operand0, Operand1)) {
        // Tentatively commute the operands and try again.
        TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
        TreeWalker.resetTopOperands(Insert);
        TentativelyCommuting = true;
        Declined = false;
      }
    }
  }

  /// Stackification for some operand was successful. Reset to the default
  /// state.
  void reset() {
    TentativelyCommuting = false;
    Declined = false;
  }
};
} // end anonymous namespace

bool WebAssemblyRegStackify::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Register Stackifying **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto *TII = MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  const auto *TRI = MF.getSubtarget<WebAssemblySubtarget>().getRegisterInfo();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  auto &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();

  // Walk the instructions from the bottom up. Currently we don't look past
  // block boundaries, and the blocks aren't ordered so the block visitation
  // order isn't significant, but we may want to change this in the future.
  for (MachineBasicBlock &MBB : MF) {
    // Don't use a range-based for loop, because we modify the list as we're
    // iterating over it and the end iterator may change.
    for (auto MII = MBB.rbegin(); MII != MBB.rend(); ++MII) {
      MachineInstr *Insert = &*MII;
      // Don't nest anything inside an inline asm, because we don't have
      // constraints for $push inputs.
      if (Insert->isInlineAsm())
        continue;

      // Ignore debugging intrinsics.
      if (Insert->isDebugValue())
        continue;

      // Iterate through the inputs in reverse order, since we'll be pulling
      // operands off the stack in LIFO order.
      CommutingState Commuting;
      TreeWalkerState TreeWalker(Insert);
      while (!TreeWalker.done()) {
        MachineOperand &Use = TreeWalker.pop();

        // We're only interested in explicit virtual register operands.
        if (!Use.isReg())
          continue;

        Register Reg = Use.getReg();
        assert(Use.isUse() && "explicit_uses() should only iterate over uses");
        assert(!Use.isImplicit() &&
               "explicit_uses() should only iterate over explicit operands");
        if (Reg.isPhysical())
          continue;

        // Identify the definition for this register at this point.
        MachineInstr *DefI = getVRegDef(Reg, Insert, MRI, LIS);
        if (!DefI)
          continue;

        // Don't nest an INLINE_ASM def into anything, because we don't have
        // constraints for $pop outputs.
        if (DefI->isInlineAsm())
          continue;

        // Argument instructions represent live-in registers and not real
        // instructions.
        if (WebAssembly::isArgument(DefI->getOpcode()))
          continue;

        MachineOperand *Def =
            DefI->findRegisterDefOperand(Reg, /*TRI=*/nullptr);
        assert(Def != nullptr);

        // Decide which strategy to take. Prefer to move a single-use value
        // over cloning it, and prefer cloning over introducing a tee.
        // For moving, we require the def to be in the same block as the use;
        // this makes things simpler (LiveIntervals' handleMove function only
        // supports intra-block moves) and it's MachineSink's job to catch all
        // the sinking opportunities anyway.
        bool SameBlock = DefI->getParent() == &MBB;
        bool CanMove = SameBlock && isSafeToMove(Def, &Use, Insert, MFI, MRI) &&
                       !TreeWalker.isOnStack(Reg);
        if (CanMove && hasOneNonDBGUse(Reg, DefI, MRI, MDT, LIS)) {
          Insert = moveForSingleUse(Reg, Use, DefI, MBB, Insert, LIS, MFI, MRI);

          // If we are removing the frame base reg completely, remove the debug
          // info as well.
          // TODO: Encode this properly as a stackified value.
          if (MFI.isFrameBaseVirtual() && MFI.getFrameBaseVreg() == Reg)
            MFI.clearFrameBaseVreg();
        } else if (shouldRematerialize(*DefI, TII)) {
          Insert =
              rematerializeCheapDef(Reg, Use, *DefI, MBB, Insert->getIterator(),
                                    LIS, MFI, MRI, TII, TRI);
        } else if (CanMove && oneUseDominatesOtherUses(Reg, Use, MBB, MRI, MDT,
                                                       LIS, MFI)) {
          Insert = moveAndTeeForMultiUse(Reg, Use, DefI, MBB, Insert, LIS, MFI,
                                         MRI, TII);
        } else {
          // We failed to stackify the operand. If the problem was ordering
          // constraints, Commuting may be able to help.
          if (!CanMove && SameBlock)
            Commuting.maybeCommute(Insert, TreeWalker, TII);
          // Proceed to the next operand.
          continue;
        }

        // Stackifying a multivalue def may unlock in-place stackification of
        // subsequent defs. TODO: Handle the case where the consecutive uses are
        // not all in the same instruction.
        auto *SubsequentDef = Insert->defs().begin();
        auto *SubsequentUse = &Use;
        while (SubsequentDef != Insert->defs().end() &&
               SubsequentUse != Use.getParent()->uses().end()) {
          if (!SubsequentDef->isReg() || !SubsequentUse->isReg())
            break;
          Register DefReg = SubsequentDef->getReg();
          Register UseReg = SubsequentUse->getReg();
          // TODO: This single-use restriction could be relaxed by using tees
          if (DefReg != UseReg || !MRI.hasOneNonDBGUse(DefReg))
            break;
          MFI.stackifyVReg(MRI, DefReg);
          ++SubsequentDef;
          ++SubsequentUse;
        }

        // If the instruction we just stackified is an IMPLICIT_DEF, convert it
        // to a constant 0 so that the def is explicit, and the push/pop
        // correspondence is maintained.
        if (Insert->getOpcode() == TargetOpcode::IMPLICIT_DEF)
          convertImplicitDefToConstZero(Insert, MRI, TII, MF, LIS);

        // We stackified an operand. Add the defining instruction's operands to
        // the worklist stack now to continue to build an ever deeper tree.
        Commuting.reset();
        TreeWalker.pushOperands(Insert);
      }

      // If we stackified any operands, skip over the tree to start looking for
      // the next instruction we can build a tree on.
      if (Insert != &*MII) {
        imposeStackOrdering(&*MII);
        MII = MachineBasicBlock::iterator(Insert).getReverse();
        Changed = true;
      }
    }
  }

  // If we used VALUE_STACK anywhere, add it to the live-in sets everywhere so
  // that it never looks like a use-before-def.
  if (Changed) {
    MF.getRegInfo().addLiveIn(WebAssembly::VALUE_STACK);
    for (MachineBasicBlock &MBB : MF)
      MBB.addLiveIn(WebAssembly::VALUE_STACK);
  }

#ifndef NDEBUG
  // Verify that pushes and pops are performed in LIFO order.
  SmallVector<unsigned, 0> Stack;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (MFI.isVRegStackified(Reg))
          assert(Stack.pop_back_val() == Reg &&
                 "Register stack pop should be paired with a push");
      }
      for (MachineOperand &MO : MI.defs()) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (MFI.isVRegStackified(Reg))
          Stack.push_back(MO.getReg());
      }
    }
    // TODO: Generalize this code to support keeping values on the stack across
    // basic block boundaries.
    assert(Stack.empty() &&
           "Register stack pushes and pops should be balanced");
  }
#endif

  return Changed;
}
