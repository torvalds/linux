//===-- SwiftErrorValueTracking.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements a limited mem2reg-like analysis to promote uses of function
// arguments and allocas marked with swiftalloc from memory into virtual
// registers tracked by this class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SwiftErrorValueTracking.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Value.h"

using namespace llvm;

Register SwiftErrorValueTracking::getOrCreateVReg(const MachineBasicBlock *MBB,
                                                  const Value *Val) {
  auto Key = std::make_pair(MBB, Val);
  auto It = VRegDefMap.find(Key);
  // If this is the first use of this swifterror value in this basic block,
  // create a new virtual register.
  // After we processed all basic blocks we will satisfy this "upwards exposed
  // use" by inserting a copy or phi at the beginning of this block.
  if (It == VRegDefMap.end()) {
    auto &DL = MF->getDataLayout();
    const TargetRegisterClass *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
    auto VReg = MF->getRegInfo().createVirtualRegister(RC);
    VRegDefMap[Key] = VReg;
    VRegUpwardsUse[Key] = VReg;
    return VReg;
  } else
    return It->second;
}

void SwiftErrorValueTracking::setCurrentVReg(const MachineBasicBlock *MBB,
                                             const Value *Val, Register VReg) {
  VRegDefMap[std::make_pair(MBB, Val)] = VReg;
}

Register SwiftErrorValueTracking::getOrCreateVRegDefAt(
    const Instruction *I, const MachineBasicBlock *MBB, const Value *Val) {
  auto Key = PointerIntPair<const Instruction *, 1, bool>(I, true);
  auto It = VRegDefUses.find(Key);
  if (It != VRegDefUses.end())
    return It->second;

  auto &DL = MF->getDataLayout();
  const TargetRegisterClass *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
  Register VReg = MF->getRegInfo().createVirtualRegister(RC);
  VRegDefUses[Key] = VReg;
  setCurrentVReg(MBB, Val, VReg);
  return VReg;
}

Register SwiftErrorValueTracking::getOrCreateVRegUseAt(
    const Instruction *I, const MachineBasicBlock *MBB, const Value *Val) {
  auto Key = PointerIntPair<const Instruction *, 1, bool>(I, false);
  auto It = VRegDefUses.find(Key);
  if (It != VRegDefUses.end())
    return It->second;

  Register VReg = getOrCreateVReg(MBB, Val);
  VRegDefUses[Key] = VReg;
  return VReg;
}

/// Set up SwiftErrorVals by going through the function. If the function has
/// swifterror argument, it will be the first entry.
void SwiftErrorValueTracking::setFunction(MachineFunction &mf) {
  MF = &mf;
  Fn = &MF->getFunction();
  TLI = MF->getSubtarget().getTargetLowering();
  TII = MF->getSubtarget().getInstrInfo();

  if (!TLI->supportSwiftError())
    return;

  SwiftErrorVals.clear();
  VRegDefMap.clear();
  VRegUpwardsUse.clear();
  VRegDefUses.clear();
  SwiftErrorArg = nullptr;

  // Check if function has a swifterror argument.
  bool HaveSeenSwiftErrorArg = false;
  for (Function::const_arg_iterator AI = Fn->arg_begin(), AE = Fn->arg_end();
       AI != AE; ++AI)
    if (AI->hasSwiftErrorAttr()) {
      assert(!HaveSeenSwiftErrorArg &&
             "Must have only one swifterror parameter");
      (void)HaveSeenSwiftErrorArg; // silence warning.
      HaveSeenSwiftErrorArg = true;
      SwiftErrorArg = &*AI;
      SwiftErrorVals.push_back(&*AI);
    }

  for (const auto &LLVMBB : *Fn)
    for (const auto &Inst : LLVMBB) {
      if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst))
        if (Alloca->isSwiftError())
          SwiftErrorVals.push_back(Alloca);
    }
}

bool SwiftErrorValueTracking::createEntriesInEntryBlock(DebugLoc DbgLoc) {
  if (!TLI->supportSwiftError())
    return false;

  // We only need to do this when we have swifterror parameter or swifterror
  // alloc.
  if (SwiftErrorVals.empty())
    return false;

  MachineBasicBlock *MBB = &*MF->begin();
  auto &DL = MF->getDataLayout();
  auto const *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
  bool Inserted = false;
  for (const auto *SwiftErrorVal : SwiftErrorVals) {
    // We will always generate a copy from the argument. It is always used at
    // least by the 'return' of the swifterror.
    if (SwiftErrorArg && SwiftErrorArg == SwiftErrorVal)
      continue;
    Register VReg = MF->getRegInfo().createVirtualRegister(RC);
    // Assign Undef to Vreg. We construct MI directly to make sure it works
    // with FastISel.
    BuildMI(*MBB, MBB->getFirstNonPHI(), DbgLoc,
            TII->get(TargetOpcode::IMPLICIT_DEF), VReg);

    setCurrentVReg(MBB, SwiftErrorVal, VReg);
    Inserted = true;
  }

  return Inserted;
}

/// Propagate swifterror values through the machine function CFG.
void SwiftErrorValueTracking::propagateVRegs() {
  if (!TLI->supportSwiftError())
    return;

  // We only need to do this when we have swifterror parameter or swifterror
  // alloc.
  if (SwiftErrorVals.empty())
    return;

  // For each machine basic block in reverse post order.
  ReversePostOrderTraversal<MachineFunction *> RPOT(MF);
  for (MachineBasicBlock *MBB : RPOT) {
    // For each swifterror value in the function.
    for (const auto *SwiftErrorVal : SwiftErrorVals) {
      auto Key = std::make_pair(MBB, SwiftErrorVal);
      auto UUseIt = VRegUpwardsUse.find(Key);
      auto VRegDefIt = VRegDefMap.find(Key);
      bool UpwardsUse = UUseIt != VRegUpwardsUse.end();
      Register UUseVReg = UpwardsUse ? UUseIt->second : Register();
      bool DownwardDef = VRegDefIt != VRegDefMap.end();
      assert(!(UpwardsUse && !DownwardDef) &&
             "We can't have an upwards use but no downwards def");

      // If there is no upwards exposed use and an entry for the swifterror in
      // the def map for this value we don't need to do anything: We already
      // have a downward def for this basic block.
      if (!UpwardsUse && DownwardDef)
        continue;

      // Otherwise we either have an upwards exposed use vreg that we need to
      // materialize or need to forward the downward def from predecessors.

      // Check whether we have a single vreg def from all predecessors.
      // Otherwise we need a phi.
      SmallVector<std::pair<MachineBasicBlock *, Register>, 4> VRegs;
      SmallSet<const MachineBasicBlock *, 8> Visited;
      for (auto *Pred : MBB->predecessors()) {
        if (!Visited.insert(Pred).second)
          continue;
        VRegs.push_back(std::make_pair(
            Pred, getOrCreateVReg(Pred, SwiftErrorVal)));
        if (Pred != MBB)
          continue;
        // We have a self-edge.
        // If there was no upwards use in this basic block there is now one: the
        // phi needs to use it self.
        if (!UpwardsUse) {
          UpwardsUse = true;
          UUseIt = VRegUpwardsUse.find(Key);
          assert(UUseIt != VRegUpwardsUse.end());
          UUseVReg = UUseIt->second;
        }
      }

      // We need a phi node if we have more than one predecessor with different
      // downward defs.
      bool needPHI =
          VRegs.size() >= 1 &&
          llvm::any_of(
              VRegs,
              [&](const std::pair<const MachineBasicBlock *, Register> &V)
                  -> bool { return V.second != VRegs[0].second; });

      // If there is no upwards exposed used and we don't need a phi just
      // forward the swifterror vreg from the predecessor(s).
      if (!UpwardsUse && !needPHI) {
        assert(!VRegs.empty() &&
               "No predecessors? The entry block should bail out earlier");
        // Just forward the swifterror vreg from the predecessor(s).
        setCurrentVReg(MBB, SwiftErrorVal, VRegs[0].second);
        continue;
      }

      auto DLoc = isa<Instruction>(SwiftErrorVal)
                      ? cast<Instruction>(SwiftErrorVal)->getDebugLoc()
                      : DebugLoc();
      const auto *TII = MF->getSubtarget().getInstrInfo();

      // If we don't need a phi create a copy to the upward exposed vreg.
      if (!needPHI) {
        assert(UpwardsUse);
        assert(!VRegs.empty() &&
               "No predecessors?  Is the Calling Convention correct?");
        Register DestReg = UUseVReg;
        BuildMI(*MBB, MBB->getFirstNonPHI(), DLoc, TII->get(TargetOpcode::COPY),
                DestReg)
            .addReg(VRegs[0].second);
        continue;
      }

      // We need a phi: if there is an upwards exposed use we already have a
      // destination virtual register number otherwise we generate a new one.
      auto &DL = MF->getDataLayout();
      auto const *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
      Register PHIVReg =
          UpwardsUse ? UUseVReg : MF->getRegInfo().createVirtualRegister(RC);
      MachineInstrBuilder PHI =
          BuildMI(*MBB, MBB->getFirstNonPHI(), DLoc,
                  TII->get(TargetOpcode::PHI), PHIVReg);
      for (auto BBRegPair : VRegs) {
        PHI.addReg(BBRegPair.second).addMBB(BBRegPair.first);
      }

      // We did not have a definition in this block before: store the phi's vreg
      // as this block downward exposed def.
      if (!UpwardsUse)
        setCurrentVReg(MBB, SwiftErrorVal, PHIVReg);
    }
  }

  // Create implicit defs for upward uses from unreachable blocks
  MachineRegisterInfo &MRI = MF->getRegInfo();
  for (const auto &Use : VRegUpwardsUse) {
    const MachineBasicBlock *UseBB = Use.first.first;
    Register VReg = Use.second;
    if (!MRI.def_begin(VReg).atEnd())
      continue;

#ifdef EXPENSIVE_CHECKS
    assert(std::find(RPOT.begin(), RPOT.end(), UseBB) == RPOT.end() &&
           "Reachable block has VReg upward use without definition.");
#endif

    MachineBasicBlock *UseBBMut = MF->getBlockNumbered(UseBB->getNumber());

    BuildMI(*UseBBMut, UseBBMut->getFirstNonPHI(), DebugLoc(),
            TII->get(TargetOpcode::IMPLICIT_DEF), VReg);
  }
}

void SwiftErrorValueTracking::preassignVRegs(
    MachineBasicBlock *MBB, BasicBlock::const_iterator Begin,
    BasicBlock::const_iterator End) {
  if (!TLI->supportSwiftError() || SwiftErrorVals.empty())
    return;

  // Iterator over instructions and assign vregs to swifterror defs and uses.
  for (auto It = Begin; It != End; ++It) {
    if (auto *CB = dyn_cast<CallBase>(&*It)) {
      // A call-site with a swifterror argument is both use and def.
      const Value *SwiftErrorAddr = nullptr;
      for (const auto &Arg : CB->args()) {
        if (!Arg->isSwiftError())
          continue;
        // Use of swifterror.
        assert(!SwiftErrorAddr && "Cannot have multiple swifterror arguments");
        SwiftErrorAddr = &*Arg;
        assert(SwiftErrorAddr->isSwiftError() &&
               "Must have a swifterror value argument");
        getOrCreateVRegUseAt(&*It, MBB, SwiftErrorAddr);
      }
      if (!SwiftErrorAddr)
        continue;

      // Def of swifterror.
      getOrCreateVRegDefAt(&*It, MBB, SwiftErrorAddr);

      // A load is a use.
    } else if (const LoadInst *LI = dyn_cast<const LoadInst>(&*It)) {
      const Value *V = LI->getOperand(0);
      if (!V->isSwiftError())
        continue;

      getOrCreateVRegUseAt(LI, MBB, V);

      // A store is a def.
    } else if (const StoreInst *SI = dyn_cast<const StoreInst>(&*It)) {
      const Value *SwiftErrorAddr = SI->getOperand(1);
      if (!SwiftErrorAddr->isSwiftError())
        continue;

      // Def of swifterror.
      getOrCreateVRegDefAt(&*It, MBB, SwiftErrorAddr);

      // A return in a swiferror returning function is a use.
    } else if (const ReturnInst *R = dyn_cast<const ReturnInst>(&*It)) {
      const Function *F = R->getParent()->getParent();
      if (!F->getAttributes().hasAttrSomewhere(Attribute::SwiftError))
        continue;

      getOrCreateVRegUseAt(R, MBB, SwiftErrorArg);
    }
  }
}
