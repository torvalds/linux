//===-------------- MIRCanonicalizer.cpp - MIR Canonicalizer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The purpose of this pass is to employ a canonical code transformation so
// that code compiled with slightly different IR passes can be diffed more
// effectively than otherwise. This is done by renaming vregs in a given
// LiveRange in a canonical way. This pass also does a pseudo-scheduling to
// move defs closer to their use inorder to reduce diffs caused by slightly
// different schedules.
//
// Basic Usage:
//
// llc -o - -run-pass mir-canonicalizer example.mir
//
// Reorders instructions canonically.
// Renames virtual register operands canonically.
// Strips certain MIR artifacts (optionally).
//
//===----------------------------------------------------------------------===//

#include "MIRVRegNamerUtils.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mir-canonicalizer"

static cl::opt<unsigned>
    CanonicalizeFunctionNumber("canon-nth-function", cl::Hidden, cl::init(~0u),
                               cl::value_desc("N"),
                               cl::desc("Function number to canonicalize."));

namespace {

class MIRCanonicalizer : public MachineFunctionPass {
public:
  static char ID;
  MIRCanonicalizer() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Rename register operands in a canonical ordering.";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char MIRCanonicalizer::ID;

char &llvm::MIRCanonicalizerID = MIRCanonicalizer::ID;

INITIALIZE_PASS_BEGIN(MIRCanonicalizer, "mir-canonicalizer",
                      "Rename Register Operands Canonically", false, false)

INITIALIZE_PASS_END(MIRCanonicalizer, "mir-canonicalizer",
                    "Rename Register Operands Canonically", false, false)

static std::vector<MachineBasicBlock *> GetRPOList(MachineFunction &MF) {
  if (MF.empty())
    return {};
  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&*MF.begin());
  std::vector<MachineBasicBlock *> RPOList;
  append_range(RPOList, RPOT);

  return RPOList;
}

static bool
rescheduleLexographically(std::vector<MachineInstr *> instructions,
                          MachineBasicBlock *MBB,
                          std::function<MachineBasicBlock::iterator()> getPos) {

  bool Changed = false;
  using StringInstrPair = std::pair<std::string, MachineInstr *>;
  std::vector<StringInstrPair> StringInstrMap;

  for (auto *II : instructions) {
    std::string S;
    raw_string_ostream OS(S);
    II->print(OS);
    OS.flush();

    // Trim the assignment, or start from the beginning in the case of a store.
    const size_t i = S.find('=');
    StringInstrMap.push_back({(i == std::string::npos) ? S : S.substr(i), II});
  }

  llvm::sort(StringInstrMap, llvm::less_first());

  for (auto &II : StringInstrMap) {

    LLVM_DEBUG({
      dbgs() << "Splicing ";
      II.second->dump();
      dbgs() << " right before: ";
      getPos()->dump();
    });

    Changed = true;
    MBB->splice(getPos(), MBB, II.second);
  }

  return Changed;
}

static bool rescheduleCanonically(unsigned &PseudoIdempotentInstCount,
                                  MachineBasicBlock *MBB) {

  bool Changed = false;

  // Calculates the distance of MI from the beginning of its parent BB.
  auto getInstrIdx = [](const MachineInstr &MI) {
    unsigned i = 0;
    for (const auto &CurMI : *MI.getParent()) {
      if (&CurMI == &MI)
        return i;
      i++;
    }
    return ~0U;
  };

  // Pre-Populate vector of instructions to reschedule so that we don't
  // clobber the iterator.
  std::vector<MachineInstr *> Instructions;
  for (auto &MI : *MBB) {
    Instructions.push_back(&MI);
  }

  std::map<MachineInstr *, std::vector<MachineInstr *>> MultiUsers;
  std::map<unsigned, MachineInstr *> MultiUserLookup;
  unsigned UseToBringDefCloserToCount = 0;
  std::vector<MachineInstr *> PseudoIdempotentInstructions;
  std::vector<unsigned> PhysRegDefs;
  for (auto *II : Instructions) {
    for (unsigned i = 1; i < II->getNumOperands(); i++) {
      MachineOperand &MO = II->getOperand(i);
      if (!MO.isReg())
        continue;

      if (MO.getReg().isVirtual())
        continue;

      if (!MO.isDef())
        continue;

      PhysRegDefs.push_back(MO.getReg());
    }
  }

  for (auto *II : Instructions) {
    if (II->getNumOperands() == 0)
      continue;
    if (II->mayLoadOrStore())
      continue;

    MachineOperand &MO = II->getOperand(0);
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    if (!MO.isDef())
      continue;

    bool IsPseudoIdempotent = true;
    for (unsigned i = 1; i < II->getNumOperands(); i++) {

      if (II->getOperand(i).isImm()) {
        continue;
      }

      if (II->getOperand(i).isReg()) {
        if (!II->getOperand(i).getReg().isVirtual())
          if (!llvm::is_contained(PhysRegDefs, II->getOperand(i).getReg())) {
            continue;
          }
      }

      IsPseudoIdempotent = false;
      break;
    }

    if (IsPseudoIdempotent) {
      PseudoIdempotentInstructions.push_back(II);
      continue;
    }

    LLVM_DEBUG(dbgs() << "Operand " << 0 << " of "; II->dump(); MO.dump(););

    MachineInstr *Def = II;
    unsigned Distance = ~0U;
    MachineInstr *UseToBringDefCloserTo = nullptr;
    MachineRegisterInfo *MRI = &MBB->getParent()->getRegInfo();
    for (auto &UO : MRI->use_nodbg_operands(MO.getReg())) {
      MachineInstr *UseInst = UO.getParent();

      const unsigned DefLoc = getInstrIdx(*Def);
      const unsigned UseLoc = getInstrIdx(*UseInst);
      const unsigned Delta = (UseLoc - DefLoc);

      if (UseInst->getParent() != Def->getParent())
        continue;
      if (DefLoc >= UseLoc)
        continue;

      if (Delta < Distance) {
        Distance = Delta;
        UseToBringDefCloserTo = UseInst;
        MultiUserLookup[UseToBringDefCloserToCount++] = UseToBringDefCloserTo;
      }
    }

    const auto BBE = MBB->instr_end();
    MachineBasicBlock::iterator DefI = BBE;
    MachineBasicBlock::iterator UseI = BBE;

    for (auto BBI = MBB->instr_begin(); BBI != BBE; ++BBI) {

      if (DefI != BBE && UseI != BBE)
        break;

      if (&*BBI == Def) {
        DefI = BBI;
        continue;
      }

      if (&*BBI == UseToBringDefCloserTo) {
        UseI = BBI;
        continue;
      }
    }

    if (DefI == BBE || UseI == BBE)
      continue;

    LLVM_DEBUG({
      dbgs() << "Splicing ";
      DefI->dump();
      dbgs() << " right before: ";
      UseI->dump();
    });

    MultiUsers[UseToBringDefCloserTo].push_back(Def);
    Changed = true;
    MBB->splice(UseI, MBB, DefI);
  }

  // Sort the defs for users of multiple defs lexographically.
  for (const auto &E : MultiUserLookup) {

    auto UseI = llvm::find_if(MBB->instrs(), [&](MachineInstr &MI) -> bool {
      return &MI == E.second;
    });

    if (UseI == MBB->instr_end())
      continue;

    LLVM_DEBUG(
        dbgs() << "Rescheduling Multi-Use Instructions Lexographically.";);
    Changed |= rescheduleLexographically(
        MultiUsers[E.second], MBB,
        [&]() -> MachineBasicBlock::iterator { return UseI; });
  }

  PseudoIdempotentInstCount = PseudoIdempotentInstructions.size();
  LLVM_DEBUG(
      dbgs() << "Rescheduling Idempotent Instructions Lexographically.";);
  Changed |= rescheduleLexographically(
      PseudoIdempotentInstructions, MBB,
      [&]() -> MachineBasicBlock::iterator { return MBB->begin(); });

  return Changed;
}

static bool propagateLocalCopies(MachineBasicBlock *MBB) {
  bool Changed = false;
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  std::vector<MachineInstr *> Copies;
  for (MachineInstr &MI : MBB->instrs()) {
    if (MI.isCopy())
      Copies.push_back(&MI);
  }

  for (MachineInstr *MI : Copies) {

    if (!MI->getOperand(0).isReg())
      continue;
    if (!MI->getOperand(1).isReg())
      continue;

    const Register Dst = MI->getOperand(0).getReg();
    const Register Src = MI->getOperand(1).getReg();

    if (!Dst.isVirtual())
      continue;
    if (!Src.isVirtual())
      continue;
    // Not folding COPY instructions if regbankselect has not set the RCs.
    // Why are we only considering Register Classes? Because the verifier
    // sometimes gets upset if the register classes don't match even if the
    // types do. A future patch might add COPY folding for matching types in
    // pre-registerbankselect code.
    if (!MRI.getRegClassOrNull(Dst))
      continue;
    if (MRI.getRegClass(Dst) != MRI.getRegClass(Src))
      continue;

    std::vector<MachineOperand *> Uses;
    for (MachineOperand &MO : MRI.use_operands(Dst))
      Uses.push_back(&MO);
    for (auto *MO : Uses)
      MO->setReg(Src);

    Changed = true;
    MI->eraseFromParent();
  }

  return Changed;
}

static bool doDefKillClear(MachineBasicBlock *MBB) {
  bool Changed = false;

  for (auto &MI : *MBB) {
    for (auto &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      if (!MO.isDef() && MO.isKill()) {
        Changed = true;
        MO.setIsKill(false);
      }

      if (MO.isDef() && MO.isDead()) {
        Changed = true;
        MO.setIsDead(false);
      }
    }
  }

  return Changed;
}

static bool runOnBasicBlock(MachineBasicBlock *MBB,
                            unsigned BasicBlockNum, VRegRenamer &Renamer) {
  LLVM_DEBUG({
    dbgs() << "\n\n  NEW BASIC BLOCK: " << MBB->getName() << "  \n\n";
    dbgs() << "\n\n================================================\n\n";
  });

  bool Changed = false;

  LLVM_DEBUG(dbgs() << "\n\n NEW BASIC BLOCK: " << MBB->getName() << "\n\n";);

  LLVM_DEBUG(dbgs() << "MBB Before Canonical Copy Propagation:\n";
             MBB->dump(););
  Changed |= propagateLocalCopies(MBB);
  LLVM_DEBUG(dbgs() << "MBB After Canonical Copy Propagation:\n"; MBB->dump(););

  LLVM_DEBUG(dbgs() << "MBB Before Scheduling:\n"; MBB->dump(););
  unsigned IdempotentInstCount = 0;
  Changed |= rescheduleCanonically(IdempotentInstCount, MBB);
  LLVM_DEBUG(dbgs() << "MBB After Scheduling:\n"; MBB->dump(););

  Changed |= Renamer.renameVRegs(MBB, BasicBlockNum);

  // TODO: Consider dropping this. Dropping kill defs is probably not
  // semantically sound.
  Changed |= doDefKillClear(MBB);

  LLVM_DEBUG(dbgs() << "Updated MachineBasicBlock:\n"; MBB->dump();
             dbgs() << "\n";);
  LLVM_DEBUG(
      dbgs() << "\n\n================================================\n\n");
  return Changed;
}

bool MIRCanonicalizer::runOnMachineFunction(MachineFunction &MF) {

  static unsigned functionNum = 0;
  if (CanonicalizeFunctionNumber != ~0U) {
    if (CanonicalizeFunctionNumber != functionNum++)
      return false;
    LLVM_DEBUG(dbgs() << "\n Canonicalizing Function " << MF.getName()
                      << "\n";);
  }

  // we need a valid vreg to create a vreg type for skipping all those
  // stray vreg numbers so reach alignment/canonical vreg values.
  std::vector<MachineBasicBlock *> RPOList = GetRPOList(MF);

  LLVM_DEBUG(
      dbgs() << "\n\n  NEW MACHINE FUNCTION: " << MF.getName() << "  \n\n";
      dbgs() << "\n\n================================================\n\n";
      dbgs() << "Total Basic Blocks: " << RPOList.size() << "\n";
      for (auto MBB
           : RPOList) { dbgs() << MBB->getName() << "\n"; } dbgs()
      << "\n\n================================================\n\n";);

  unsigned BBNum = 0;
  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  VRegRenamer Renamer(MRI);
  for (auto *MBB : RPOList)
    Changed |= runOnBasicBlock(MBB, BBNum++, Renamer);

  return Changed;
}
