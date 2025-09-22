//===-- AArch64StackTaggingPreRA.cpp --- Stack Tagging for AArch64 -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#include "AArch64.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64InstrInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-stack-tagging-pre-ra"

enum UncheckedLdStMode { UncheckedNever, UncheckedSafe, UncheckedAlways };

cl::opt<UncheckedLdStMode> ClUncheckedLdSt(
    "stack-tagging-unchecked-ld-st", cl::Hidden,
    cl::init(UncheckedSafe),
    cl::desc(
        "Unconditionally apply unchecked-ld-st optimization (even for large "
        "stack frames, or in the presence of variable sized allocas)."),
    cl::values(
        clEnumValN(UncheckedNever, "never", "never apply unchecked-ld-st"),
        clEnumValN(
            UncheckedSafe, "safe",
            "apply unchecked-ld-st when the target is definitely within range"),
        clEnumValN(UncheckedAlways, "always", "always apply unchecked-ld-st")));

static cl::opt<bool>
    ClFirstSlot("stack-tagging-first-slot-opt", cl::Hidden, cl::init(true),
                cl::desc("Apply first slot optimization for stack tagging "
                         "(eliminate ADDG Rt, Rn, 0, 0)."));

namespace {

class AArch64StackTaggingPreRA : public MachineFunctionPass {
  MachineFunction *MF;
  AArch64FunctionInfo *AFI;
  MachineFrameInfo *MFI;
  MachineRegisterInfo *MRI;
  const AArch64RegisterInfo *TRI;
  const AArch64InstrInfo *TII;

  SmallVector<MachineInstr*, 16> ReTags;

public:
  static char ID;
  AArch64StackTaggingPreRA() : MachineFunctionPass(ID) {
    initializeAArch64StackTaggingPreRAPass(*PassRegistry::getPassRegistry());
  }

  bool mayUseUncheckedLoadStore();
  void uncheckUsesOf(unsigned TaggedReg, int FI);
  void uncheckLoadsAndStores();
  std::optional<int> findFirstSlotCandidate();

  bool runOnMachineFunction(MachineFunction &Func) override;
  StringRef getPassName() const override {
    return "AArch64 Stack Tagging PreRA";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // end anonymous namespace

char AArch64StackTaggingPreRA::ID = 0;

INITIALIZE_PASS_BEGIN(AArch64StackTaggingPreRA, "aarch64-stack-tagging-pre-ra",
                      "AArch64 Stack Tagging PreRA Pass", false, false)
INITIALIZE_PASS_END(AArch64StackTaggingPreRA, "aarch64-stack-tagging-pre-ra",
                    "AArch64 Stack Tagging PreRA Pass", false, false)

FunctionPass *llvm::createAArch64StackTaggingPreRAPass() {
  return new AArch64StackTaggingPreRA();
}

static bool isUncheckedLoadOrStoreOpcode(unsigned Opcode) {
  switch (Opcode) {
  case AArch64::LDRBBui:
  case AArch64::LDRHHui:
  case AArch64::LDRWui:
  case AArch64::LDRXui:

  case AArch64::LDRBui:
  case AArch64::LDRHui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:

  case AArch64::LDRSHWui:
  case AArch64::LDRSHXui:

  case AArch64::LDRSBWui:
  case AArch64::LDRSBXui:

  case AArch64::LDRSWui:

  case AArch64::STRBBui:
  case AArch64::STRHHui:
  case AArch64::STRWui:
  case AArch64::STRXui:

  case AArch64::STRBui:
  case AArch64::STRHui:
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STRQui:

  case AArch64::LDPWi:
  case AArch64::LDPXi:
  case AArch64::LDPSi:
  case AArch64::LDPDi:
  case AArch64::LDPQi:

  case AArch64::LDPSWi:

  case AArch64::STPWi:
  case AArch64::STPXi:
  case AArch64::STPSi:
  case AArch64::STPDi:
  case AArch64::STPQi:
    return true;
  default:
    return false;
  }
}

bool AArch64StackTaggingPreRA::mayUseUncheckedLoadStore() {
  if (ClUncheckedLdSt == UncheckedNever)
    return false;
  else if (ClUncheckedLdSt == UncheckedAlways)
    return true;

  // This estimate can be improved if we had harder guarantees about stack frame
  // layout. With LocalStackAllocation we can estimate SP offset to any
  // preallocated slot. AArch64FrameLowering::orderFrameObjects could put tagged
  // objects ahead of non-tagged ones, but that's not always desirable.
  //
  // Underestimating SP offset here may require the use of LDG to materialize
  // the tagged address of the stack slot, along with a scratch register
  // allocation (post-regalloc!).
  //
  // For now we do the safe thing here and require that the entire stack frame
  // is within range of the shortest of the unchecked instructions.
  unsigned FrameSize = 0;
  for (unsigned i = 0, e = MFI->getObjectIndexEnd(); i != e; ++i)
    FrameSize += MFI->getObjectSize(i);
  bool EntireFrameReachableFromSP = FrameSize < 0xf00;
  return !MFI->hasVarSizedObjects() && EntireFrameReachableFromSP;
}

void AArch64StackTaggingPreRA::uncheckUsesOf(unsigned TaggedReg, int FI) {
  for (MachineInstr &UseI :
       llvm::make_early_inc_range(MRI->use_instructions(TaggedReg))) {
    if (isUncheckedLoadOrStoreOpcode(UseI.getOpcode())) {
      // FI operand is always the one before the immediate offset.
      unsigned OpIdx = TII->getLoadStoreImmIdx(UseI.getOpcode()) - 1;
      if (UseI.getOperand(OpIdx).isReg() &&
          UseI.getOperand(OpIdx).getReg() == TaggedReg) {
        UseI.getOperand(OpIdx).ChangeToFrameIndex(FI);
        UseI.getOperand(OpIdx).setTargetFlags(AArch64II::MO_TAGGED);
      }
    } else if (UseI.isCopy() && UseI.getOperand(0).getReg().isVirtual()) {
      uncheckUsesOf(UseI.getOperand(0).getReg(), FI);
    }
  }
}

void AArch64StackTaggingPreRA::uncheckLoadsAndStores() {
  for (auto *I : ReTags) {
    Register TaggedReg = I->getOperand(0).getReg();
    int FI = I->getOperand(1).getIndex();
    uncheckUsesOf(TaggedReg, FI);
  }
}

namespace {
struct SlotWithTag {
  int FI;
  int Tag;
  SlotWithTag(int FI, int Tag) : FI(FI), Tag(Tag) {}
  explicit SlotWithTag(const MachineInstr &MI)
      : FI(MI.getOperand(1).getIndex()), Tag(MI.getOperand(4).getImm()) {}
  bool operator==(const SlotWithTag &Other) const {
    return FI == Other.FI && Tag == Other.Tag;
  }
};
} // namespace

namespace llvm {
template <> struct DenseMapInfo<SlotWithTag> {
  static inline SlotWithTag getEmptyKey() { return {-2, -2}; }
  static inline SlotWithTag getTombstoneKey() { return {-3, -3}; }
  static unsigned getHashValue(const SlotWithTag &V) {
    return hash_combine(DenseMapInfo<int>::getHashValue(V.FI),
                        DenseMapInfo<int>::getHashValue(V.Tag));
  }
  static bool isEqual(const SlotWithTag &A, const SlotWithTag &B) {
    return A == B;
  }
};
} // namespace llvm

static bool isSlotPreAllocated(MachineFrameInfo *MFI, int FI) {
  return MFI->getUseLocalStackAllocationBlock() &&
         MFI->isObjectPreAllocated(FI);
}

// Pin one of the tagged slots to offset 0 from the tagged base pointer.
// This would make its address available in a virtual register (IRG's def), as
// opposed to requiring an ADDG instruction to materialize. This effectively
// eliminates a vreg (by replacing it with direct uses of IRG, which is usually
// live almost everywhere anyway), and therefore needs to happen before
// regalloc.
std::optional<int> AArch64StackTaggingPreRA::findFirstSlotCandidate() {
  // Find the best (FI, Tag) pair to pin to offset 0.
  // Looking at the possible uses of a tagged address, the advantage of pinning
  // is:
  // - COPY to physical register.
  //   Does not matter, this would trade a MOV instruction for an ADDG.
  // - ST*G matter, but those mostly appear near the function prologue where all
  //   the tagged addresses need to be materialized anyway; also, counting ST*G
  //   uses would overweight large allocas that require more than one ST*G
  //   instruction.
  // - Load/Store instructions in the address operand do not require a tagged
  //   pointer, so they also do not benefit. These operands have already been
  //   eliminated (see uncheckLoadsAndStores) so all remaining load/store
  //   instructions count.
  // - Any other instruction may benefit from being pinned to offset 0.
  LLVM_DEBUG(dbgs() << "AArch64StackTaggingPreRA::findFirstSlotCandidate\n");
  if (!ClFirstSlot)
    return std::nullopt;

  DenseMap<SlotWithTag, int> RetagScore;
  SlotWithTag MaxScoreST{-1, -1};
  int MaxScore = -1;
  for (auto *I : ReTags) {
    SlotWithTag ST{*I};
    if (isSlotPreAllocated(MFI, ST.FI))
      continue;

    Register RetagReg = I->getOperand(0).getReg();
    if (!RetagReg.isVirtual())
      continue;

    int Score = 0;
    SmallVector<Register, 8> WorkList;
    WorkList.push_back(RetagReg);

    while (!WorkList.empty()) {
      Register UseReg = WorkList.pop_back_val();
      for (auto &UseI : MRI->use_instructions(UseReg)) {
        unsigned Opcode = UseI.getOpcode();
        if (Opcode == AArch64::STGi || Opcode == AArch64::ST2Gi ||
            Opcode == AArch64::STZGi || Opcode == AArch64::STZ2Gi ||
            Opcode == AArch64::STGPi || Opcode == AArch64::STGloop ||
            Opcode == AArch64::STZGloop || Opcode == AArch64::STGloop_wback ||
            Opcode == AArch64::STZGloop_wback)
          continue;
        if (UseI.isCopy()) {
          Register DstReg = UseI.getOperand(0).getReg();
          if (DstReg.isVirtual())
            WorkList.push_back(DstReg);
          continue;
        }
        LLVM_DEBUG(dbgs() << "[" << ST.FI << ":" << ST.Tag << "] use of %"
                          << Register::virtReg2Index(UseReg) << " in " << UseI
                          << "\n");
        Score++;
      }
    }

    int TotalScore = RetagScore[ST] += Score;
    if (TotalScore > MaxScore ||
        (TotalScore == MaxScore && ST.FI > MaxScoreST.FI)) {
      MaxScore = TotalScore;
      MaxScoreST = ST;
    }
  }

  if (MaxScoreST.FI < 0)
    return std::nullopt;

  // If FI's tag is already 0, we are done.
  if (MaxScoreST.Tag == 0)
    return MaxScoreST.FI;

  // Otherwise, find a random victim pair (FI, Tag) where Tag == 0.
  SlotWithTag SwapST{-1, -1};
  for (auto *I : ReTags) {
    SlotWithTag ST{*I};
    if (ST.Tag == 0) {
      SwapST = ST;
      break;
    }
  }

  // Swap tags between the victim and the highest scoring pair.
  // If SwapWith is still (-1, -1), that's fine, too - we'll simply take tag for
  // the highest score slot without changing anything else.
  for (auto *&I : ReTags) {
    SlotWithTag ST{*I};
    MachineOperand &TagOp = I->getOperand(4);
    if (ST == MaxScoreST) {
      TagOp.setImm(0);
    } else if (ST == SwapST) {
      TagOp.setImm(MaxScoreST.Tag);
    }
  }
  return MaxScoreST.FI;
}

bool AArch64StackTaggingPreRA::runOnMachineFunction(MachineFunction &Func) {
  MF = &Func;
  MRI = &MF->getRegInfo();
  AFI = MF->getInfo<AArch64FunctionInfo>();
  TII = static_cast<const AArch64InstrInfo *>(MF->getSubtarget().getInstrInfo());
  TRI = static_cast<const AArch64RegisterInfo *>(
      MF->getSubtarget().getRegisterInfo());
  MFI = &MF->getFrameInfo();
  ReTags.clear();

  assert(MRI->isSSA());

  LLVM_DEBUG(dbgs() << "********** AArch64 Stack Tagging PreRA **********\n"
                    << "********** Function: " << MF->getName() << '\n');

  SmallSetVector<int, 8> TaggedSlots;
  for (auto &BB : *MF) {
    for (auto &I : BB) {
      if (I.getOpcode() == AArch64::TAGPstack) {
        ReTags.push_back(&I);
        int FI = I.getOperand(1).getIndex();
        TaggedSlots.insert(FI);
        // There should be no offsets in TAGP yet.
        assert(I.getOperand(2).getImm() == 0);
      }
    }
  }

  // Take over from SSP. It does nothing for tagged slots, and should not really
  // have been enabled in the first place.
  for (int FI : TaggedSlots)
    MFI->setObjectSSPLayout(FI, MachineFrameInfo::SSPLK_None);

  if (ReTags.empty())
    return false;

  if (mayUseUncheckedLoadStore())
    uncheckLoadsAndStores();

  // Find a slot that is used with zero tag offset, like ADDG #fi, 0.
  // If the base tagged pointer is set up to the address of this slot,
  // the ADDG instruction can be eliminated.
  std::optional<int> BaseSlot = findFirstSlotCandidate();
  if (BaseSlot)
    AFI->setTaggedBasePointerIndex(*BaseSlot);

  for (auto *I : ReTags) {
    int FI = I->getOperand(1).getIndex();
    int Tag = I->getOperand(4).getImm();
    Register Base = I->getOperand(3).getReg();
    if (Tag == 0 && FI == BaseSlot) {
      BuildMI(*I->getParent(), I, {}, TII->get(AArch64::COPY),
              I->getOperand(0).getReg())
          .addReg(Base);
      I->eraseFromParent();
    }
  }

  return true;
}
