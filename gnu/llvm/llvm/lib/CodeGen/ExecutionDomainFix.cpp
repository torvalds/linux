//===- ExecutionDomainFix.cpp - Fix execution domain issues ----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ExecutionDomainFix.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "execution-deps-fix"

iterator_range<SmallVectorImpl<int>::const_iterator>
ExecutionDomainFix::regIndices(unsigned Reg) const {
  assert(Reg < AliasMap.size() && "Invalid register");
  const auto &Entry = AliasMap[Reg];
  return make_range(Entry.begin(), Entry.end());
}

DomainValue *ExecutionDomainFix::alloc(int domain) {
  DomainValue *dv = Avail.empty() ? new (Allocator.Allocate()) DomainValue
                                  : Avail.pop_back_val();
  if (domain >= 0)
    dv->addDomain(domain);
  assert(dv->Refs == 0 && "Reference count wasn't cleared");
  assert(!dv->Next && "Chained DomainValue shouldn't have been recycled");
  return dv;
}

void ExecutionDomainFix::release(DomainValue *DV) {
  while (DV) {
    assert(DV->Refs && "Bad DomainValue");
    if (--DV->Refs)
      return;

    // There are no more DV references. Collapse any contained instructions.
    if (DV->AvailableDomains && !DV->isCollapsed())
      collapse(DV, DV->getFirstDomain());

    DomainValue *Next = DV->Next;
    DV->clear();
    Avail.push_back(DV);
    // Also release the next DomainValue in the chain.
    DV = Next;
  }
}

DomainValue *ExecutionDomainFix::resolve(DomainValue *&DVRef) {
  DomainValue *DV = DVRef;
  if (!DV || !DV->Next)
    return DV;

  // DV has a chain. Find the end.
  do
    DV = DV->Next;
  while (DV->Next);

  // Update DVRef to point to DV.
  retain(DV);
  release(DVRef);
  DVRef = DV;
  return DV;
}

void ExecutionDomainFix::setLiveReg(int rx, DomainValue *dv) {
  assert(unsigned(rx) < NumRegs && "Invalid index");
  assert(!LiveRegs.empty() && "Must enter basic block first.");

  if (LiveRegs[rx] == dv)
    return;
  if (LiveRegs[rx])
    release(LiveRegs[rx]);
  LiveRegs[rx] = retain(dv);
}

void ExecutionDomainFix::kill(int rx) {
  assert(unsigned(rx) < NumRegs && "Invalid index");
  assert(!LiveRegs.empty() && "Must enter basic block first.");
  if (!LiveRegs[rx])
    return;

  release(LiveRegs[rx]);
  LiveRegs[rx] = nullptr;
}

void ExecutionDomainFix::force(int rx, unsigned domain) {
  assert(unsigned(rx) < NumRegs && "Invalid index");
  assert(!LiveRegs.empty() && "Must enter basic block first.");
  if (DomainValue *dv = LiveRegs[rx]) {
    if (dv->isCollapsed())
      dv->addDomain(domain);
    else if (dv->hasDomain(domain))
      collapse(dv, domain);
    else {
      // This is an incompatible open DomainValue. Collapse it to whatever and
      // force the new value into domain. This costs a domain crossing.
      collapse(dv, dv->getFirstDomain());
      assert(LiveRegs[rx] && "Not live after collapse?");
      LiveRegs[rx]->addDomain(domain);
    }
  } else {
    // Set up basic collapsed DomainValue.
    setLiveReg(rx, alloc(domain));
  }
}

void ExecutionDomainFix::collapse(DomainValue *dv, unsigned domain) {
  assert(dv->hasDomain(domain) && "Cannot collapse");

  // Collapse all the instructions.
  while (!dv->Instrs.empty())
    TII->setExecutionDomain(*dv->Instrs.pop_back_val(), domain);
  dv->setSingleDomain(domain);

  // If there are multiple users, give them new, unique DomainValues.
  if (!LiveRegs.empty() && dv->Refs > 1)
    for (unsigned rx = 0; rx != NumRegs; ++rx)
      if (LiveRegs[rx] == dv)
        setLiveReg(rx, alloc(domain));
}

bool ExecutionDomainFix::merge(DomainValue *A, DomainValue *B) {
  assert(!A->isCollapsed() && "Cannot merge into collapsed");
  assert(!B->isCollapsed() && "Cannot merge from collapsed");
  if (A == B)
    return true;
  // Restrict to the domains that A and B have in common.
  unsigned common = A->getCommonDomains(B->AvailableDomains);
  if (!common)
    return false;
  A->AvailableDomains = common;
  A->Instrs.append(B->Instrs.begin(), B->Instrs.end());

  // Clear the old DomainValue so we won't try to swizzle instructions twice.
  B->clear();
  // All uses of B are referred to A.
  B->Next = retain(A);

  for (unsigned rx = 0; rx != NumRegs; ++rx) {
    assert(!LiveRegs.empty() && "no space allocated for live registers");
    if (LiveRegs[rx] == B)
      setLiveReg(rx, A);
  }
  return true;
}

void ExecutionDomainFix::enterBasicBlock(
    const LoopTraversal::TraversedMBBInfo &TraversedMBB) {

  MachineBasicBlock *MBB = TraversedMBB.MBB;

  // Set up LiveRegs to represent registers entering MBB.
  // Set default domain values to 'no domain' (nullptr)
  if (LiveRegs.empty())
    LiveRegs.assign(NumRegs, nullptr);

  // This is the entry block.
  if (MBB->pred_empty()) {
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << ": entry\n");
    return;
  }

  // Try to coalesce live-out registers from predecessors.
  for (MachineBasicBlock *pred : MBB->predecessors()) {
    assert(unsigned(pred->getNumber()) < MBBOutRegsInfos.size() &&
           "Should have pre-allocated MBBInfos for all MBBs");
    LiveRegsDVInfo &Incoming = MBBOutRegsInfos[pred->getNumber()];
    // Incoming is null if this is a backedge from a BB
    // we haven't processed yet
    if (Incoming.empty())
      continue;

    for (unsigned rx = 0; rx != NumRegs; ++rx) {
      DomainValue *pdv = resolve(Incoming[rx]);
      if (!pdv)
        continue;
      if (!LiveRegs[rx]) {
        setLiveReg(rx, pdv);
        continue;
      }

      // We have a live DomainValue from more than one predecessor.
      if (LiveRegs[rx]->isCollapsed()) {
        // We are already collapsed, but predecessor is not. Force it.
        unsigned Domain = LiveRegs[rx]->getFirstDomain();
        if (!pdv->isCollapsed() && pdv->hasDomain(Domain))
          collapse(pdv, Domain);
        continue;
      }

      // Currently open, merge in predecessor.
      if (!pdv->isCollapsed())
        merge(LiveRegs[rx], pdv);
      else
        force(rx, pdv->getFirstDomain());
    }
  }
  LLVM_DEBUG(dbgs() << printMBBReference(*MBB)
                    << (!TraversedMBB.IsDone ? ": incomplete\n"
                                             : ": all preds known\n"));
}

void ExecutionDomainFix::leaveBasicBlock(
    const LoopTraversal::TraversedMBBInfo &TraversedMBB) {
  assert(!LiveRegs.empty() && "Must enter basic block first.");
  unsigned MBBNumber = TraversedMBB.MBB->getNumber();
  assert(MBBNumber < MBBOutRegsInfos.size() &&
         "Unexpected basic block number.");
  // Save register clearances at end of MBB - used by enterBasicBlock().
  for (DomainValue *OldLiveReg : MBBOutRegsInfos[MBBNumber]) {
    release(OldLiveReg);
  }
  MBBOutRegsInfos[MBBNumber] = LiveRegs;
  LiveRegs.clear();
}

bool ExecutionDomainFix::visitInstr(MachineInstr *MI) {
  // Update instructions with explicit execution domains.
  std::pair<uint16_t, uint16_t> DomP = TII->getExecutionDomain(*MI);
  if (DomP.first) {
    if (DomP.second)
      visitSoftInstr(MI, DomP.second);
    else
      visitHardInstr(MI, DomP.first);
  }

  return !DomP.first;
}

void ExecutionDomainFix::processDefs(MachineInstr *MI, bool Kill) {
  assert(!MI->isDebugInstr() && "Won't process debug values");
  const MCInstrDesc &MCID = MI->getDesc();
  for (unsigned i = 0,
                e = MI->isVariadic() ? MI->getNumOperands() : MCID.getNumDefs();
       i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    if (MO.isUse())
      continue;
    for (int rx : regIndices(MO.getReg())) {
      // This instruction explicitly defines rx.
      LLVM_DEBUG(dbgs() << printReg(RC->getRegister(rx), TRI) << ":\t" << *MI);

      // Kill off domains redefined by generic instructions.
      if (Kill)
        kill(rx);
    }
  }
}

void ExecutionDomainFix::visitHardInstr(MachineInstr *mi, unsigned domain) {
  // Collapse all uses.
  for (unsigned i = mi->getDesc().getNumDefs(),
                e = mi->getDesc().getNumOperands();
       i != e; ++i) {
    MachineOperand &mo = mi->getOperand(i);
    if (!mo.isReg())
      continue;
    for (int rx : regIndices(mo.getReg())) {
      force(rx, domain);
    }
  }

  // Kill all defs and force them.
  for (unsigned i = 0, e = mi->getDesc().getNumDefs(); i != e; ++i) {
    MachineOperand &mo = mi->getOperand(i);
    if (!mo.isReg())
      continue;
    for (int rx : regIndices(mo.getReg())) {
      kill(rx);
      force(rx, domain);
    }
  }
}

void ExecutionDomainFix::visitSoftInstr(MachineInstr *mi, unsigned mask) {
  // Bitmask of available domains for this instruction after taking collapsed
  // operands into account.
  unsigned available = mask;

  // Scan the explicit use operands for incoming domains.
  SmallVector<int, 4> used;
  if (!LiveRegs.empty())
    for (unsigned i = mi->getDesc().getNumDefs(),
                  e = mi->getDesc().getNumOperands();
         i != e; ++i) {
      MachineOperand &mo = mi->getOperand(i);
      if (!mo.isReg())
        continue;
      for (int rx : regIndices(mo.getReg())) {
        DomainValue *dv = LiveRegs[rx];
        if (dv == nullptr)
          continue;
        // Bitmask of domains that dv and available have in common.
        unsigned common = dv->getCommonDomains(available);
        // Is it possible to use this collapsed register for free?
        if (dv->isCollapsed()) {
          // Restrict available domains to the ones in common with the operand.
          // If there are no common domains, we must pay the cross-domain
          // penalty for this operand.
          if (common)
            available = common;
        } else if (common)
          // Open DomainValue is compatible, save it for merging.
          used.push_back(rx);
        else
          // Open DomainValue is not compatible with instruction. It is useless
          // now.
          kill(rx);
      }
    }

  // If the collapsed operands force a single domain, propagate the collapse.
  if (isPowerOf2_32(available)) {
    unsigned domain = llvm::countr_zero(available);
    TII->setExecutionDomain(*mi, domain);
    visitHardInstr(mi, domain);
    return;
  }

  // Kill off any remaining uses that don't match available, and build a list of
  // incoming DomainValues that we want to merge.
  SmallVector<int, 4> Regs;
  for (int rx : used) {
    assert(!LiveRegs.empty() && "no space allocated for live registers");
    DomainValue *&LR = LiveRegs[rx];
    // This useless DomainValue could have been missed above.
    if (!LR->getCommonDomains(available)) {
      kill(rx);
      continue;
    }
    // Sorted insertion.
    // Enables giving priority to the latest domains during merging.
    const int Def = RDA->getReachingDef(mi, RC->getRegister(rx));
    auto I = partition_point(Regs, [&](int I) {
      return RDA->getReachingDef(mi, RC->getRegister(I)) <= Def;
    });
    Regs.insert(I, rx);
  }

  // doms are now sorted in order of appearance. Try to merge them all, giving
  // priority to the latest ones.
  DomainValue *dv = nullptr;
  while (!Regs.empty()) {
    if (!dv) {
      dv = LiveRegs[Regs.pop_back_val()];
      // Force the first dv to match the current instruction.
      dv->AvailableDomains = dv->getCommonDomains(available);
      assert(dv->AvailableDomains && "Domain should have been filtered");
      continue;
    }

    DomainValue *Latest = LiveRegs[Regs.pop_back_val()];
    // Skip already merged values.
    if (Latest == dv || Latest->Next)
      continue;
    if (merge(dv, Latest))
      continue;

    // If latest didn't merge, it is useless now. Kill all registers using it.
    for (int i : used) {
      assert(!LiveRegs.empty() && "no space allocated for live registers");
      if (LiveRegs[i] == Latest)
        kill(i);
    }
  }

  // dv is the DomainValue we are going to use for this instruction.
  if (!dv) {
    dv = alloc();
    dv->AvailableDomains = available;
  }
  dv->Instrs.push_back(mi);

  // Finally set all defs and non-collapsed uses to dv. We must iterate through
  // all the operators, including imp-def ones.
  for (const MachineOperand &mo : mi->operands()) {
    if (!mo.isReg())
      continue;
    for (int rx : regIndices(mo.getReg())) {
      if (!LiveRegs[rx] || (mo.isDef() && LiveRegs[rx] != dv)) {
        kill(rx);
        setLiveReg(rx, dv);
      }
    }
  }
}

void ExecutionDomainFix::processBasicBlock(
    const LoopTraversal::TraversedMBBInfo &TraversedMBB) {
  enterBasicBlock(TraversedMBB);
  // If this block is not done, it makes little sense to make any decisions
  // based on clearance information. We need to make a second pass anyway,
  // and by then we'll have better information, so we can avoid doing the work
  // to try and break dependencies now.
  for (MachineInstr &MI : *TraversedMBB.MBB) {
    if (!MI.isDebugInstr()) {
      bool Kill = false;
      if (TraversedMBB.PrimaryPass)
        Kill = visitInstr(&MI);
      processDefs(&MI, Kill);
    }
  }
  leaveBasicBlock(TraversedMBB);
}

bool ExecutionDomainFix::runOnMachineFunction(MachineFunction &mf) {
  if (skipFunction(mf.getFunction()))
    return false;
  MF = &mf;
  TII = MF->getSubtarget().getInstrInfo();
  TRI = MF->getSubtarget().getRegisterInfo();
  LiveRegs.clear();
  assert(NumRegs == RC->getNumRegs() && "Bad regclass");

  LLVM_DEBUG(dbgs() << "********** FIX EXECUTION DOMAIN: "
                    << TRI->getRegClassName(RC) << " **********\n");

  // If no relevant registers are used in the function, we can skip it
  // completely.
  bool anyregs = false;
  const MachineRegisterInfo &MRI = mf.getRegInfo();
  for (unsigned Reg : *RC) {
    if (MRI.isPhysRegUsed(Reg)) {
      anyregs = true;
      break;
    }
  }
  if (!anyregs)
    return false;

  RDA = &getAnalysis<ReachingDefAnalysis>();

  // Initialize the AliasMap on the first use.
  if (AliasMap.empty()) {
    // Given a PhysReg, AliasMap[PhysReg] returns a list of indices into RC and
    // therefore the LiveRegs array.
    AliasMap.resize(TRI->getNumRegs());
    for (unsigned i = 0, e = RC->getNumRegs(); i != e; ++i)
      for (MCRegAliasIterator AI(RC->getRegister(i), TRI, true); AI.isValid();
           ++AI)
        AliasMap[*AI].push_back(i);
  }

  // Initialize the MBBOutRegsInfos
  MBBOutRegsInfos.resize(mf.getNumBlockIDs());

  // Traverse the basic blocks.
  LoopTraversal Traversal;
  LoopTraversal::TraversalOrder TraversedMBBOrder = Traversal.traverse(mf);
  for (const LoopTraversal::TraversedMBBInfo &TraversedMBB : TraversedMBBOrder)
    processBasicBlock(TraversedMBB);

  for (const LiveRegsDVInfo &OutLiveRegs : MBBOutRegsInfos)
    for (DomainValue *OutLiveReg : OutLiveRegs)
      if (OutLiveReg)
        release(OutLiveReg);

  MBBOutRegsInfos.clear();
  Avail.clear();
  Allocator.DestroyAll();

  return false;
}
