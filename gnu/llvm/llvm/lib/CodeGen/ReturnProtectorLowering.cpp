//===- ReturnProtectorLowering.cpp - ---------------------------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements common routines for return protector support.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ReturnProtectorLowering.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

static void markUsedRegsInSuccessors(MachineBasicBlock &MBB,
                                     SmallSet<unsigned, 16> &Used,
                                     SmallSet<int, 24> &Visited) {
  int BBNum = MBB.getNumber();
  if (Visited.count(BBNum))
    return;

  // Mark all the registers used
  for (auto &MBBI : MBB.instrs()) {
    for (auto &MBBIOp : MBBI.operands()) {
      if (MBBIOp.isReg())
        Used.insert(MBBIOp.getReg());
    }
  }

  // Mark this MBB as visited
  Visited.insert(BBNum);
  // Recurse over all successors
  for (auto &SuccMBB : MBB.successors())
    markUsedRegsInSuccessors(*SuccMBB, Used, Visited);
}

static bool containsProtectableData(Type *Ty) {
  if (!Ty)
    return false;

  if (ArrayType *AT = dyn_cast<ArrayType>(Ty))
    return true;

  if (StructType *ST = dyn_cast<StructType>(Ty)) {
    for (StructType::element_iterator I = ST->element_begin(),
                                      E = ST->element_end();
         I != E; ++I) {
      if (containsProtectableData(*I))
        return true;
    }
  }
  return false;
}

// Mostly the same as StackProtector::HasAddressTaken
static bool hasAddressTaken(const Instruction *AI,
                            SmallPtrSet<const PHINode *, 16> &visitedPHI) {
  for (const User *U : AI->users()) {
    const auto *I = cast<Instruction>(U);
    switch (I->getOpcode()) {
    case Instruction::Store:
      if (AI == cast<StoreInst>(I)->getValueOperand())
        return true;
      break;
    case Instruction::AtomicCmpXchg:
      if (AI == cast<AtomicCmpXchgInst>(I)->getNewValOperand())
        return true;
      break;
    case Instruction::PtrToInt:
      if (AI == cast<PtrToIntInst>(I)->getOperand(0))
        return true;
      break;
    case Instruction::BitCast:
    case Instruction::GetElementPtr:
    case Instruction::Select:
    case Instruction::AddrSpaceCast:
      if (hasAddressTaken(I, visitedPHI))
        return true;
      break;
    case Instruction::PHI: {
      const auto *PN = cast<PHINode>(I);
      if (visitedPHI.insert(PN).second)
        if (hasAddressTaken(PN, visitedPHI))
          return true;
      break;
    }
    case Instruction::Load:
    case Instruction::AtomicRMW:
    case Instruction::Ret:
      return false;
      break;
    default:
      // Conservatively return true for any instruction that takes an address
      // operand, but is not handled above.
      return true;
    }
  }
  return false;
}

/// setupReturnProtector - Checks the function for ROP friendly return
/// instructions and sets ReturnProtectorNeeded if found.
void ReturnProtectorLowering::setupReturnProtector(MachineFunction &MF) const {
  if (MF.getFunction().hasFnAttribute("ret-protector")) {
    for (auto &MBB : MF) {
      for (auto &T : MBB.terminators()) {
        if (opcodeIsReturn(T.getOpcode())) {
          MF.getFrameInfo().setReturnProtectorNeeded(true);
          return;
        }
      }
    }
  }
}

/// saveReturnProtectorRegister - Allows the target to save the
/// ReturnProtectorRegister in the CalleeSavedInfo vector if needed.
void ReturnProtectorLowering::saveReturnProtectorRegister(
    MachineFunction &MF, std::vector<CalleeSavedInfo> &CSI) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.getReturnProtectorNeeded())
    return;

  if (!MFI.hasReturnProtectorRegister())
    llvm_unreachable("Saving unset return protector register");

  unsigned Reg = MFI.getReturnProtectorRegister();
  if (MFI.getReturnProtectorNeedsStore())
    CSI.push_back(CalleeSavedInfo(Reg));
  else {
    for (auto &MBB : MF) {
      if (!MBB.isLiveIn(Reg))
        MBB.addLiveIn(Reg);
    }
  }
}

/// determineReturnProtectorTempRegister - Find a register that can be used
/// during function prologue / epilogue to store the return protector cookie.
/// Returns false if a register is needed but could not be found,
/// otherwise returns true.
bool ReturnProtectorLowering::determineReturnProtectorRegister(
    MachineFunction &MF, const SmallVector<MachineBasicBlock *, 4> &SaveBlocks,
    const SmallVector<MachineBasicBlock *, 4> &RestoreBlocks) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.getReturnProtectorNeeded())
    return true;

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  std::vector<unsigned> TempRegs;
  fillTempRegisters(MF, TempRegs);

  // For leaf functions, try to find a free register that is available
  // in every BB, so we do not need to store it in the frame at all.
  // We walk the entire function here because MFI.hasCalls() is unreliable.
  bool hasCalls = false;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.isCall() && !MI.isReturn()) {
        hasCalls = true;
        break;
      }
    }
    if (hasCalls)
      break;
  }

  // If the return address is always on the stack, then we
  // want to try to keep the return protector cookie unspilled.
  // This prevents a single stack smash from corrupting both the
  // return protector cookie and the return address.
  llvm::Triple::ArchType arch = MF.getTarget().getTargetTriple().getArch();
  bool returnAddrOnStack = arch == llvm::Triple::ArchType::x86
                        || arch == llvm::Triple::ArchType::x86_64;

  // For architectures which do not spill a return address
  // to the stack by default, it is possible that in a leaf
  // function that neither the return address or the retguard cookie
  // will be spilled, and stack corruption may be missed.
  // Here, we check leaf functions on these kinds of architectures
  // to see if they have any variable sized local allocations,
  // array type allocations, allocations which contain array
  // types, or elements that have their address taken. If any of
  // these conditions are met, then we skip leaf function
  // optimization and spill the retguard cookie to the stack.
  bool hasLocals = MFI.hasVarSizedObjects();
  if (!hasCalls && !hasLocals && !returnAddrOnStack) {
    for (const BasicBlock &BB : MF.getFunction()) {
      for (const Instruction &I : BB) {
        if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
          // Check for array allocations
          Type *Ty = AI->getAllocatedType();
          if (AI->isArrayAllocation() || containsProtectableData(Ty)) {
            hasLocals = true;
            break;
          }
          // Check for address taken
          SmallPtrSet<const PHINode *, 16> visitedPHIs;
          if (hasAddressTaken(AI, visitedPHIs)) {
            hasLocals = true;
            break;
          }
        }
      }
      if (hasLocals)
        break;
    }
  }

  bool tryLeafOptimize = !hasCalls && (returnAddrOnStack || !hasLocals);

  if (tryLeafOptimize) {
    SmallSet<unsigned, 16> LeafUsed;
    SmallSet<int, 24> LeafVisited;
    markUsedRegsInSuccessors(MF.front(), LeafUsed, LeafVisited);
    for (unsigned Reg : TempRegs) {
      bool canUse = true;
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI) {
        if (LeafUsed.count(*AI)) {
          canUse = false;
          break;
        }
      }
      if (canUse) {
        MFI.setReturnProtectorRegister(Reg);
        MFI.setReturnProtectorNeedsStore(false);
        return true;
      }
    }
  }

  // For non-leaf functions, we only need to search save / restore blocks
  SmallSet<unsigned, 16> Used;
  SmallSet<int, 24> Visited;

  // CSR spills happen at the beginning of this block
  // so we can mark it as visited because anything past it is safe
  for (auto &SB : SaveBlocks)
    Visited.insert(SB->getNumber());

  // CSR Restores happen at the end of restore blocks, before any terminators,
  // so we need to search restores for MBB terminators, and any successor BBs.
  for (auto &RB : RestoreBlocks) {
    for (auto &RBI : RB->terminators()) {
      for (auto &RBIOp : RBI.operands()) {
        if (RBIOp.isReg())
          Used.insert(RBIOp.getReg());
      }
    }
    for (auto &SuccMBB : RB->successors())
      markUsedRegsInSuccessors(*SuccMBB, Used, Visited);
  }

  // Now we iterate from the front to find code paths that
  // bypass save blocks and land on return blocks
  markUsedRegsInSuccessors(MF.front(), Used, Visited);

  // Now we have gathered all the regs used outside the frame save / restore,
  // so we can see if we have a free reg to use for the retguard cookie.
  for (unsigned Reg : TempRegs) {
    bool canUse = true;
    for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI) {
      if (Used.count(*AI)) {
        // Reg is used somewhere, so we cannot use it
        canUse = false;
        break;
      }
    }
    if (canUse) {
      MFI.setReturnProtectorRegister(Reg);
      break;
    }
  }

  return MFI.hasReturnProtectorRegister();
}

/// insertReturnProtectors - insert return protector instrumentation.
void ReturnProtectorLowering::insertReturnProtectors(
    MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (!MFI.getReturnProtectorNeeded())
    return;

  if (!MFI.hasReturnProtectorRegister())
    llvm_unreachable("Inconsistent return protector state.");

  const Function &Fn = MF.getFunction();
  const Module *M = Fn.getParent();
  GlobalVariable *cookie =
      dyn_cast_or_null<GlobalVariable>(M->getGlobalVariable(
          Fn.getFnAttribute("ret-protector-cookie").getValueAsString(),
          PointerType::getUnqual(M->getContext())));

  if (!cookie)
    llvm_unreachable("Function needs return protector but no cookie assigned");

  unsigned Reg = MFI.getReturnProtectorRegister();

  std::vector<MachineInstr *> returns;
  for (auto &MBB : MF) {
    if (MBB.isReturnBlock()) {
      for (auto &MI : MBB.terminators()) {
        if (opcodeIsReturn(MI.getOpcode())) {
          returns.push_back(&MI);
          if (!MBB.isLiveIn(Reg))
            MBB.addLiveIn(Reg);
        }
      }
    }
  }

  if (returns.empty())
    return;

  for (auto &MI : returns)
    insertReturnProtectorEpilogue(MF, *MI, cookie);

  insertReturnProtectorPrologue(MF, MF.front(), cookie);

  if (!MF.front().isLiveIn(Reg))
    MF.front().addLiveIn(Reg);
}
