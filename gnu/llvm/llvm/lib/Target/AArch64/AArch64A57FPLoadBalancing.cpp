//===-- AArch64A57FPLoadBalancing.cpp - Balance FP ops statically on A57---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// For best-case performance on Cortex-A57, we should try to use a balanced
// mix of odd and even D-registers when performing a critical sequence of
// independent, non-quadword FP/ASIMD floating-point multiply or
// multiply-accumulate operations.
//
// This pass attempts to detect situations where the register allocation may
// adversely affect this load balancing and to change the registers used so as
// to better utilize the CPU.
//
// Ideally we'd just take each multiply or multiply-accumulate in turn and
// allocate it alternating even or odd registers. However, multiply-accumulates
// are most efficiently performed in the same functional unit as their
// accumulation operand. Therefore this pass tries to find maximal sequences
// ("Chains") of multiply-accumulates linked via their accumulation operand,
// and assign them all the same "color" (oddness/evenness).
//
// This optimization affects S-register and D-register floating point
// multiplies and FMADD/FMAs, as well as vector (floating point only) muls and
// FMADD/FMA. Q register instructions (and 128-bit vector instructions) are
// not affected.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-a57-fp-load-balancing"

// Enforce the algorithm to use the scavenged register even when the original
// destination register is the correct color. Used for testing.
static cl::opt<bool>
TransformAll("aarch64-a57-fp-load-balancing-force-all",
             cl::desc("Always modify dest registers regardless of color"),
             cl::init(false), cl::Hidden);

// Never use the balance information obtained from chains - return a specific
// color always. Used for testing.
static cl::opt<unsigned>
OverrideBalance("aarch64-a57-fp-load-balancing-override",
              cl::desc("Ignore balance information, always return "
                       "(1: Even, 2: Odd)."),
              cl::init(0), cl::Hidden);

//===----------------------------------------------------------------------===//
// Helper functions

// Is the instruction a type of multiply on 64-bit (or 32-bit) FPRs?
static bool isMul(MachineInstr *MI) {
  switch (MI->getOpcode()) {
  case AArch64::FMULSrr:
  case AArch64::FNMULSrr:
  case AArch64::FMULDrr:
  case AArch64::FNMULDrr:
    return true;
  default:
    return false;
  }
}

// Is the instruction a type of FP multiply-accumulate on 64-bit (or 32-bit) FPRs?
static bool isMla(MachineInstr *MI) {
  switch (MI->getOpcode()) {
  case AArch64::FMSUBSrrr:
  case AArch64::FMADDSrrr:
  case AArch64::FNMSUBSrrr:
  case AArch64::FNMADDSrrr:
  case AArch64::FMSUBDrrr:
  case AArch64::FMADDDrrr:
  case AArch64::FNMSUBDrrr:
  case AArch64::FNMADDDrrr:
    return true;
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//

namespace {
/// A "color", which is either even or odd. Yes, these aren't really colors
/// but the algorithm is conceptually doing two-color graph coloring.
enum class Color { Even, Odd };
#ifndef NDEBUG
static const char *ColorNames[2] = { "Even", "Odd" };
#endif

class Chain;

class AArch64A57FPLoadBalancing : public MachineFunctionPass {
  MachineRegisterInfo *MRI;
  const TargetRegisterInfo *TRI;
  RegisterClassInfo RCI;

public:
  static char ID;
  explicit AArch64A57FPLoadBalancing() : MachineFunctionPass(ID) {
    initializeAArch64A57FPLoadBalancingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return "A57 FP Anti-dependency breaker";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool runOnBasicBlock(MachineBasicBlock &MBB);
  bool colorChainSet(std::vector<Chain*> GV, MachineBasicBlock &MBB,
                     int &Balance);
  bool colorChain(Chain *G, Color C, MachineBasicBlock &MBB);
  int scavengeRegister(Chain *G, Color C, MachineBasicBlock &MBB);
  void scanInstruction(MachineInstr *MI, unsigned Idx,
                       std::map<unsigned, Chain*> &Active,
                       std::vector<std::unique_ptr<Chain>> &AllChains);
  void maybeKillChain(MachineOperand &MO, unsigned Idx,
                      std::map<unsigned, Chain*> &RegChains);
  Color getColor(unsigned Register);
  Chain *getAndEraseNext(Color PreferredColor, std::vector<Chain*> &L);
};
}

char AArch64A57FPLoadBalancing::ID = 0;

INITIALIZE_PASS_BEGIN(AArch64A57FPLoadBalancing, DEBUG_TYPE,
                      "AArch64 A57 FP Load-Balancing", false, false)
INITIALIZE_PASS_END(AArch64A57FPLoadBalancing, DEBUG_TYPE,
                    "AArch64 A57 FP Load-Balancing", false, false)

namespace {
/// A Chain is a sequence of instructions that are linked together by
/// an accumulation operand. For example:
///
///   fmul def d0, ?
///   fmla def d1, ?, ?, killed d0
///   fmla def d2, ?, ?, killed d1
///
/// There may be other instructions interleaved in the sequence that
/// do not belong to the chain. These other instructions must not use
/// the "chain" register at any point.
///
/// We currently only support chains where the "chain" operand is killed
/// at each link in the chain for simplicity.
/// A chain has three important instructions - Start, Last and Kill.
///   * The start instruction is the first instruction in the chain.
///   * Last is the final instruction in the chain.
///   * Kill may or may not be defined. If defined, Kill is the instruction
///     where the outgoing value of the Last instruction is killed.
///     This information is important as if we know the outgoing value is
///     killed with no intervening uses, we can safely change its register.
///
/// Without a kill instruction, we must assume the outgoing value escapes
/// beyond our model and either must not change its register or must
/// create a fixup FMOV to keep the old register value consistent.
///
class Chain {
public:
  /// The important (marker) instructions.
  MachineInstr *StartInst, *LastInst, *KillInst;
  /// The index, from the start of the basic block, that each marker
  /// appears. These are stored so we can do quick interval tests.
  unsigned StartInstIdx, LastInstIdx, KillInstIdx;
  /// All instructions in the chain.
  std::set<MachineInstr*> Insts;
  /// True if KillInst cannot be modified. If this is true,
  /// we cannot change LastInst's outgoing register.
  /// This will be true for tied values and regmasks.
  bool KillIsImmutable;
  /// The "color" of LastInst. This will be the preferred chain color,
  /// as changing intermediate nodes is easy but changing the last
  /// instruction can be more tricky.
  Color LastColor;

  Chain(MachineInstr *MI, unsigned Idx, Color C)
      : StartInst(MI), LastInst(MI), KillInst(nullptr),
        StartInstIdx(Idx), LastInstIdx(Idx), KillInstIdx(0),
        LastColor(C) {
    Insts.insert(MI);
  }

  /// Add a new instruction into the chain. The instruction's dest operand
  /// has the given color.
  void add(MachineInstr *MI, unsigned Idx, Color C) {
    LastInst = MI;
    LastInstIdx = Idx;
    LastColor = C;
    assert((KillInstIdx == 0 || LastInstIdx < KillInstIdx) &&
           "Chain: broken invariant. A Chain can only be killed after its last "
           "def");

    Insts.insert(MI);
  }

  /// Return true if MI is a member of the chain.
  bool contains(MachineInstr &MI) { return Insts.count(&MI) > 0; }

  /// Return the number of instructions in the chain.
  unsigned size() const {
    return Insts.size();
  }

  /// Inform the chain that its last active register (the dest register of
  /// LastInst) is killed by MI with no intervening uses or defs.
  void setKill(MachineInstr *MI, unsigned Idx, bool Immutable) {
    KillInst = MI;
    KillInstIdx = Idx;
    KillIsImmutable = Immutable;
    assert((KillInstIdx == 0 || LastInstIdx < KillInstIdx) &&
           "Chain: broken invariant. A Chain can only be killed after its last "
           "def");
  }

  /// Return the first instruction in the chain.
  MachineInstr *getStart() const { return StartInst; }
  /// Return the last instruction in the chain.
  MachineInstr *getLast() const { return LastInst; }
  /// Return the "kill" instruction (as set with setKill()) or NULL.
  MachineInstr *getKill() const { return KillInst; }
  /// Return an instruction that can be used as an iterator for the end
  /// of the chain. This is the maximum of KillInst (if set) and LastInst.
  MachineBasicBlock::iterator end() const {
    return ++MachineBasicBlock::iterator(KillInst ? KillInst : LastInst);
  }
  MachineBasicBlock::iterator begin() const { return getStart(); }

  /// Can the Kill instruction (assuming one exists) be modified?
  bool isKillImmutable() const { return KillIsImmutable; }

  /// Return the preferred color of this chain.
  Color getPreferredColor() {
    if (OverrideBalance != 0)
      return OverrideBalance == 1 ? Color::Even : Color::Odd;
    return LastColor;
  }

  /// Return true if this chain (StartInst..KillInst) overlaps with Other.
  bool rangeOverlapsWith(const Chain &Other) const {
    unsigned End = KillInst ? KillInstIdx : LastInstIdx;
    unsigned OtherEnd = Other.KillInst ?
      Other.KillInstIdx : Other.LastInstIdx;

    return StartInstIdx <= OtherEnd && Other.StartInstIdx <= End;
  }

  /// Return true if this chain starts before Other.
  bool startsBefore(const Chain *Other) const {
    return StartInstIdx < Other->StartInstIdx;
  }

  /// Return true if the group will require a fixup MOV at the end.
  bool requiresFixup() const {
    return (getKill() && isKillImmutable()) || !getKill();
  }

  /// Return a simple string representation of the chain.
  std::string str() const {
    std::string S;
    raw_string_ostream OS(S);

    OS << "{";
    StartInst->print(OS, /* SkipOpers= */true);
    OS << " -> ";
    LastInst->print(OS, /* SkipOpers= */true);
    if (KillInst) {
      OS << " (kill @ ";
      KillInst->print(OS, /* SkipOpers= */true);
      OS << ")";
    }
    OS << "}";

    return OS.str();
  }

};

} // end anonymous namespace

//===----------------------------------------------------------------------===//

bool AArch64A57FPLoadBalancing::runOnMachineFunction(MachineFunction &F) {
  if (skipFunction(F.getFunction()))
    return false;

  if (!F.getSubtarget<AArch64Subtarget>().balanceFPOps())
    return false;

  bool Changed = false;
  LLVM_DEBUG(dbgs() << "***** AArch64A57FPLoadBalancing *****\n");

  MRI = &F.getRegInfo();
  TRI = F.getRegInfo().getTargetRegisterInfo();
  RCI.runOnMachineFunction(F);

  for (auto &MBB : F) {
    Changed |= runOnBasicBlock(MBB);
  }

  return Changed;
}

bool AArch64A57FPLoadBalancing::runOnBasicBlock(MachineBasicBlock &MBB) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Running on MBB: " << MBB
                    << " - scanning instructions...\n");

  // First, scan the basic block producing a set of chains.

  // The currently "active" chains - chains that can be added to and haven't
  // been killed yet. This is keyed by register - all chains can only have one
  // "link" register between each inst in the chain.
  std::map<unsigned, Chain*> ActiveChains;
  std::vector<std::unique_ptr<Chain>> AllChains;
  unsigned Idx = 0;
  for (auto &MI : MBB)
    scanInstruction(&MI, Idx++, ActiveChains, AllChains);

  LLVM_DEBUG(dbgs() << "Scan complete, " << AllChains.size()
                    << " chains created.\n");

  // Group the chains into disjoint sets based on their liveness range. This is
  // a poor-man's version of graph coloring. Ideally we'd create an interference
  // graph and perform full-on graph coloring on that, but;
  //   (a) That's rather heavyweight for only two colors.
  //   (b) We expect multiple disjoint interference regions - in practice the live
  //       range of chains is quite small and they are clustered between loads
  //       and stores.
  EquivalenceClasses<Chain*> EC;
  for (auto &I : AllChains)
    EC.insert(I.get());

  for (auto &I : AllChains)
    for (auto &J : AllChains)
      if (I != J && I->rangeOverlapsWith(*J))
        EC.unionSets(I.get(), J.get());
  LLVM_DEBUG(dbgs() << "Created " << EC.getNumClasses() << " disjoint sets.\n");

  // Now we assume that every member of an equivalence class interferes
  // with every other member of that class, and with no members of other classes.

  // Convert the EquivalenceClasses to a simpler set of sets.
  std::vector<std::vector<Chain*> > V;
  for (auto I = EC.begin(), E = EC.end(); I != E; ++I) {
    std::vector<Chain*> Cs(EC.member_begin(I), EC.member_end());
    if (Cs.empty()) continue;
    V.push_back(std::move(Cs));
  }

  // Now we have a set of sets, order them by start address so
  // we can iterate over them sequentially.
  llvm::sort(V,
             [](const std::vector<Chain *> &A, const std::vector<Chain *> &B) {
               return A.front()->startsBefore(B.front());
             });

  // As we only have two colors, we can track the global (BB-level) balance of
  // odds versus evens. We aim to keep this near zero to keep both execution
  // units fed.
  // Positive means we're even-heavy, negative we're odd-heavy.
  //
  // FIXME: If chains have interdependencies, for example:
  //   mul r0, r1, r2
  //   mul r3, r0, r1
  // We do not model this and may color each one differently, assuming we'll
  // get ILP when we obviously can't. This hasn't been seen to be a problem
  // in practice so far, so we simplify the algorithm by ignoring it.
  int Parity = 0;

  for (auto &I : V)
    Changed |= colorChainSet(std::move(I), MBB, Parity);

  return Changed;
}

Chain *AArch64A57FPLoadBalancing::getAndEraseNext(Color PreferredColor,
                                                  std::vector<Chain*> &L) {
  if (L.empty())
    return nullptr;

  // We try and get the best candidate from L to color next, given that our
  // preferred color is "PreferredColor". L is ordered from larger to smaller
  // chains. It is beneficial to color the large chains before the small chains,
  // but if we can't find a chain of the maximum length with the preferred color,
  // we fuzz the size and look for slightly smaller chains before giving up and
  // returning a chain that must be recolored.

  // FIXME: Does this need to be configurable?
  const unsigned SizeFuzz = 1;
  unsigned MinSize = L.front()->size() - SizeFuzz;
  for (auto I = L.begin(), E = L.end(); I != E; ++I) {
    if ((*I)->size() <= MinSize) {
      // We've gone past the size limit. Return the previous item.
      Chain *Ch = *--I;
      L.erase(I);
      return Ch;
    }

    if ((*I)->getPreferredColor() == PreferredColor) {
      Chain *Ch = *I;
      L.erase(I);
      return Ch;
    }
  }

  // Bailout case - just return the first item.
  Chain *Ch = L.front();
  L.erase(L.begin());
  return Ch;
}

bool AArch64A57FPLoadBalancing::colorChainSet(std::vector<Chain*> GV,
                                              MachineBasicBlock &MBB,
                                              int &Parity) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "colorChainSet(): #sets=" << GV.size() << "\n");

  // Sort by descending size order so that we allocate the most important
  // sets first.
  // Tie-break equivalent sizes by sorting chains requiring fixups before
  // those without fixups. The logic here is that we should look at the
  // chains that we cannot change before we look at those we can,
  // so the parity counter is updated and we know what color we should
  // change them to!
  // Final tie-break with instruction order so pass output is stable (i.e. not
  // dependent on malloc'd pointer values).
  llvm::sort(GV, [](const Chain *G1, const Chain *G2) {
    if (G1->size() != G2->size())
      return G1->size() > G2->size();
    if (G1->requiresFixup() != G2->requiresFixup())
      return G1->requiresFixup() > G2->requiresFixup();
    // Make sure startsBefore() produces a stable final order.
    assert((G1 == G2 || (G1->startsBefore(G2) ^ G2->startsBefore(G1))) &&
           "Starts before not total order!");
    return G1->startsBefore(G2);
  });

  Color PreferredColor = Parity < 0 ? Color::Even : Color::Odd;
  while (Chain *G = getAndEraseNext(PreferredColor, GV)) {
    // Start off by assuming we'll color to our own preferred color.
    Color C = PreferredColor;
    if (Parity == 0)
      // But if we really don't care, use the chain's preferred color.
      C = G->getPreferredColor();

    LLVM_DEBUG(dbgs() << " - Parity=" << Parity
                      << ", Color=" << ColorNames[(int)C] << "\n");

    // If we'll need a fixup FMOV, don't bother. Testing has shown that this
    // happens infrequently and when it does it has at least a 50% chance of
    // slowing code down instead of speeding it up.
    if (G->requiresFixup() && C != G->getPreferredColor()) {
      C = G->getPreferredColor();
      LLVM_DEBUG(dbgs() << " - " << G->str()
                        << " - not worthwhile changing; "
                           "color remains "
                        << ColorNames[(int)C] << "\n");
    }

    Changed |= colorChain(G, C, MBB);

    Parity += (C == Color::Even) ? G->size() : -G->size();
    PreferredColor = Parity < 0 ? Color::Even : Color::Odd;
  }

  return Changed;
}

int AArch64A57FPLoadBalancing::scavengeRegister(Chain *G, Color C,
                                                MachineBasicBlock &MBB) {
  // Can we find an appropriate register that is available throughout the life
  // of the chain? Simulate liveness backwards until the end of the chain.
  LiveRegUnits Units(*TRI);
  Units.addLiveOuts(MBB);
  MachineBasicBlock::iterator I = MBB.end();
  MachineBasicBlock::iterator ChainEnd = G->end();
  while (I != ChainEnd) {
    --I;
    Units.stepBackward(*I);
  }

  // Check which register units are alive throughout the chain.
  MachineBasicBlock::iterator ChainBegin = G->begin();
  assert(ChainBegin != ChainEnd && "Chain should contain instructions");
  do {
    --I;
    Units.accumulate(*I);
  } while (I != ChainBegin);

  // Make sure we allocate in-order, to get the cheapest registers first.
  unsigned RegClassID = ChainBegin->getDesc().operands()[0].RegClass;
  auto Ord = RCI.getOrder(TRI->getRegClass(RegClassID));
  for (auto Reg : Ord) {
    if (!Units.available(Reg))
      continue;
    if (C == getColor(Reg))
      return Reg;
  }

  return -1;
}

bool AArch64A57FPLoadBalancing::colorChain(Chain *G, Color C,
                                           MachineBasicBlock &MBB) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << " - colorChain(" << G->str() << ", "
                    << ColorNames[(int)C] << ")\n");

  // Try and obtain a free register of the right class. Without a register
  // to play with we cannot continue.
  int Reg = scavengeRegister(G, C, MBB);
  if (Reg == -1) {
    LLVM_DEBUG(dbgs() << "Scavenging (thus coloring) failed!\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << " - Scavenged register: " << printReg(Reg, TRI) << "\n");

  std::map<unsigned, unsigned> Substs;
  for (MachineInstr &I : *G) {
    if (!G->contains(I) && (&I != G->getKill() || G->isKillImmutable()))
      continue;

    // I is a member of G, or I is a mutable instruction that kills G.

    std::vector<unsigned> ToErase;
    for (auto &U : I.operands()) {
      if (U.isReg() && U.isUse() && Substs.find(U.getReg()) != Substs.end()) {
        Register OrigReg = U.getReg();
        U.setReg(Substs[OrigReg]);
        if (U.isKill())
          // Don't erase straight away, because there may be other operands
          // that also reference this substitution!
          ToErase.push_back(OrigReg);
      } else if (U.isRegMask()) {
        for (auto J : Substs) {
          if (U.clobbersPhysReg(J.first))
            ToErase.push_back(J.first);
        }
      }
    }
    // Now it's safe to remove the substs identified earlier.
    for (auto J : ToErase)
      Substs.erase(J);

    // Only change the def if this isn't the last instruction.
    if (&I != G->getKill()) {
      MachineOperand &MO = I.getOperand(0);

      bool Change = TransformAll || getColor(MO.getReg()) != C;
      if (G->requiresFixup() && &I == G->getLast())
        Change = false;

      if (Change) {
        Substs[MO.getReg()] = Reg;
        MO.setReg(Reg);

        Changed = true;
      }
    }
  }
  assert(Substs.size() == 0 && "No substitutions should be left active!");

  if (G->getKill()) {
    LLVM_DEBUG(dbgs() << " - Kill instruction seen.\n");
  } else {
    // We didn't have a kill instruction, but we didn't seem to need to change
    // the destination register anyway.
    LLVM_DEBUG(dbgs() << " - Destination register not changed.\n");
  }
  return Changed;
}

void AArch64A57FPLoadBalancing::scanInstruction(
    MachineInstr *MI, unsigned Idx, std::map<unsigned, Chain *> &ActiveChains,
    std::vector<std::unique_ptr<Chain>> &AllChains) {
  // Inspect "MI", updating ActiveChains and AllChains.

  if (isMul(MI)) {

    for (auto &I : MI->uses())
      maybeKillChain(I, Idx, ActiveChains);
    for (auto &I : MI->defs())
      maybeKillChain(I, Idx, ActiveChains);

    // Create a new chain. Multiplies don't require forwarding so can go on any
    // unit.
    Register DestReg = MI->getOperand(0).getReg();

    LLVM_DEBUG(dbgs() << "New chain started for register "
                      << printReg(DestReg, TRI) << " at " << *MI);

    auto G = std::make_unique<Chain>(MI, Idx, getColor(DestReg));
    ActiveChains[DestReg] = G.get();
    AllChains.push_back(std::move(G));

  } else if (isMla(MI)) {

    // It is beneficial to keep MLAs on the same functional unit as their
    // accumulator operand.
    Register DestReg = MI->getOperand(0).getReg();
    Register AccumReg = MI->getOperand(3).getReg();

    maybeKillChain(MI->getOperand(1), Idx, ActiveChains);
    maybeKillChain(MI->getOperand(2), Idx, ActiveChains);
    if (DestReg != AccumReg)
      maybeKillChain(MI->getOperand(0), Idx, ActiveChains);

    if (ActiveChains.find(AccumReg) != ActiveChains.end()) {
      LLVM_DEBUG(dbgs() << "Chain found for accumulator register "
                        << printReg(AccumReg, TRI) << " in MI " << *MI);

      // For simplicity we only chain together sequences of MULs/MLAs where the
      // accumulator register is killed on each instruction. This means we don't
      // need to track other uses of the registers we want to rewrite.
      //
      // FIXME: We could extend to handle the non-kill cases for more coverage.
      if (MI->getOperand(3).isKill()) {
        // Add to chain.
        LLVM_DEBUG(dbgs() << "Instruction was successfully added to chain.\n");
        ActiveChains[AccumReg]->add(MI, Idx, getColor(DestReg));
        // Handle cases where the destination is not the same as the accumulator.
        if (DestReg != AccumReg) {
          ActiveChains[DestReg] = ActiveChains[AccumReg];
          ActiveChains.erase(AccumReg);
        }
        return;
      }

      LLVM_DEBUG(
          dbgs() << "Cannot add to chain because accumulator operand wasn't "
                 << "marked <kill>!\n");
      maybeKillChain(MI->getOperand(3), Idx, ActiveChains);
    }

    LLVM_DEBUG(dbgs() << "Creating new chain for dest register "
                      << printReg(DestReg, TRI) << "\n");
    auto G = std::make_unique<Chain>(MI, Idx, getColor(DestReg));
    ActiveChains[DestReg] = G.get();
    AllChains.push_back(std::move(G));

  } else {

    // Non-MUL or MLA instruction. Invalidate any chain in the uses or defs
    // lists.
    for (auto &I : MI->uses())
      maybeKillChain(I, Idx, ActiveChains);
    for (auto &I : MI->defs())
      maybeKillChain(I, Idx, ActiveChains);

  }
}

void AArch64A57FPLoadBalancing::
maybeKillChain(MachineOperand &MO, unsigned Idx,
               std::map<unsigned, Chain*> &ActiveChains) {
  // Given an operand and the set of active chains (keyed by register),
  // determine if a chain should be ended and remove from ActiveChains.
  MachineInstr *MI = MO.getParent();

  if (MO.isReg()) {

    // If this is a KILL of a current chain, record it.
    if (MO.isKill() && ActiveChains.find(MO.getReg()) != ActiveChains.end()) {
      LLVM_DEBUG(dbgs() << "Kill seen for chain " << printReg(MO.getReg(), TRI)
                        << "\n");
      ActiveChains[MO.getReg()]->setKill(MI, Idx, /*Immutable=*/MO.isTied());
    }
    ActiveChains.erase(MO.getReg());

  } else if (MO.isRegMask()) {

    for (auto I = ActiveChains.begin(), E = ActiveChains.end();
         I != E;) {
      if (MO.clobbersPhysReg(I->first)) {
        LLVM_DEBUG(dbgs() << "Kill (regmask) seen for chain "
                          << printReg(I->first, TRI) << "\n");
        I->second->setKill(MI, Idx, /*Immutable=*/true);
        ActiveChains.erase(I++);
      } else
        ++I;
    }

  }
}

Color AArch64A57FPLoadBalancing::getColor(unsigned Reg) {
  if ((TRI->getEncodingValue(Reg) % 2) == 0)
    return Color::Even;
  else
    return Color::Odd;
}

// Factory function used by AArch64TargetMachine to add the pass to the passmanager.
FunctionPass *llvm::createAArch64A57FPLoadBalancing() {
  return new AArch64A57FPLoadBalancing();
}
