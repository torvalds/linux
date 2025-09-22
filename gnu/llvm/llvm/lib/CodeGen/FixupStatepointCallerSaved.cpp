//===-- FixupStatepointCallerSaved.cpp - Fixup caller saved registers  ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Statepoint instruction in deopt parameters contains values which are
/// meaningful to the runtime and should be able to be read at the moment the
/// call returns. So we can say that we need to encode the fact that these
/// values are "late read" by runtime. If we could express this notion for
/// register allocator it would produce the right form for us.
/// The need to fixup (i.e this pass) is specifically handling the fact that
/// we cannot describe such a late read for the register allocator.
/// Register allocator may put the value on a register clobbered by the call.
/// This pass forces the spill of such registers and replaces corresponding
/// statepoint operands to added spill slots.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "fixup-statepoint-caller-saved"
STATISTIC(NumSpilledRegisters, "Number of spilled register");
STATISTIC(NumSpillSlotsAllocated, "Number of spill slots allocated");
STATISTIC(NumSpillSlotsExtended, "Number of spill slots extended");

static cl::opt<bool> FixupSCSExtendSlotSize(
    "fixup-scs-extend-slot-size", cl::Hidden, cl::init(false),
    cl::desc("Allow spill in spill slot of greater size than register size"),
    cl::Hidden);

static cl::opt<bool> PassGCPtrInCSR(
    "fixup-allow-gcptr-in-csr", cl::Hidden, cl::init(false),
    cl::desc("Allow passing GC Pointer arguments in callee saved registers"));

static cl::opt<bool> EnableCopyProp(
    "fixup-scs-enable-copy-propagation", cl::Hidden, cl::init(true),
    cl::desc("Enable simple copy propagation during register reloading"));

// This is purely debugging option.
// It may be handy for investigating statepoint spilling issues.
static cl::opt<unsigned> MaxStatepointsWithRegs(
    "fixup-max-csr-statepoints", cl::Hidden,
    cl::desc("Max number of statepoints allowed to pass GC Ptrs in registers"));

namespace {

class FixupStatepointCallerSaved : public MachineFunctionPass {
public:
  static char ID;

  FixupStatepointCallerSaved() : MachineFunctionPass(ID) {
    initializeFixupStatepointCallerSavedPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return "Fixup Statepoint Caller Saved";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // End anonymous namespace.

char FixupStatepointCallerSaved::ID = 0;
char &llvm::FixupStatepointCallerSavedID = FixupStatepointCallerSaved::ID;

INITIALIZE_PASS_BEGIN(FixupStatepointCallerSaved, DEBUG_TYPE,
                      "Fixup Statepoint Caller Saved", false, false)
INITIALIZE_PASS_END(FixupStatepointCallerSaved, DEBUG_TYPE,
                    "Fixup Statepoint Caller Saved", false, false)

// Utility function to get size of the register.
static unsigned getRegisterSize(const TargetRegisterInfo &TRI, Register Reg) {
  const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg);
  return TRI.getSpillSize(*RC);
}

// Try to eliminate redundant copy to register which we're going to
// spill, i.e. try to change:
//    X = COPY Y
//    SPILL X
//  to
//    SPILL Y
//  If there are no uses of X between copy and STATEPOINT, that COPY
//  may be eliminated.
//  Reg - register we're about to spill
//  RI - On entry points to statepoint.
//       On successful copy propagation set to new spill point.
//  IsKill - set to true if COPY is Kill (there are no uses of Y)
//  Returns either found source copy register or original one.
static Register performCopyPropagation(Register Reg,
                                       MachineBasicBlock::iterator &RI,
                                       bool &IsKill, const TargetInstrInfo &TII,
                                       const TargetRegisterInfo &TRI) {
  // First check if statepoint itself uses Reg in non-meta operands.
  int Idx = RI->findRegisterUseOperandIdx(Reg, &TRI, false);
  if (Idx >= 0 && (unsigned)Idx < StatepointOpers(&*RI).getNumDeoptArgsIdx()) {
    IsKill = false;
    return Reg;
  }

  if (!EnableCopyProp)
    return Reg;

  MachineBasicBlock *MBB = RI->getParent();
  MachineBasicBlock::reverse_iterator E = MBB->rend();
  MachineInstr *Def = nullptr, *Use = nullptr;
  for (auto It = ++(RI.getReverse()); It != E; ++It) {
    if (It->readsRegister(Reg, &TRI) && !Use)
      Use = &*It;
    if (It->modifiesRegister(Reg, &TRI)) {
      Def = &*It;
      break;
    }
  }

  if (!Def)
    return Reg;

  auto DestSrc = TII.isCopyInstr(*Def);
  if (!DestSrc || DestSrc->Destination->getReg() != Reg)
    return Reg;

  Register SrcReg = DestSrc->Source->getReg();

  if (getRegisterSize(TRI, Reg) != getRegisterSize(TRI, SrcReg))
    return Reg;

  LLVM_DEBUG(dbgs() << "spillRegisters: perform copy propagation "
                    << printReg(Reg, &TRI) << " -> " << printReg(SrcReg, &TRI)
                    << "\n");

  // Insert spill immediately after Def
  RI = ++MachineBasicBlock::iterator(Def);
  IsKill = DestSrc->Source->isKill();

  if (!Use) {
    // There are no uses of original register between COPY and STATEPOINT.
    // There can't be any after STATEPOINT, so we can eliminate Def.
    LLVM_DEBUG(dbgs() << "spillRegisters: removing dead copy " << *Def);
    Def->eraseFromParent();
  } else if (IsKill) {
    // COPY will remain in place, spill will be inserted *after* it, so it is
    // not a kill of source anymore.
    const_cast<MachineOperand *>(DestSrc->Source)->setIsKill(false);
  }

  return SrcReg;
}

namespace {
// Pair {Register, FrameIndex}
using RegSlotPair = std::pair<Register, int>;

// Keeps track of what reloads were inserted in MBB.
class RegReloadCache {
  using ReloadSet = SmallSet<RegSlotPair, 8>;
  DenseMap<const MachineBasicBlock *, ReloadSet> Reloads;

public:
  RegReloadCache() = default;

  // Record reload of Reg from FI in block MBB
  void recordReload(Register Reg, int FI, const MachineBasicBlock *MBB) {
    RegSlotPair RSP(Reg, FI);
    auto Res = Reloads[MBB].insert(RSP);
    (void)Res;
    assert(Res.second && "reload already exists");
  }

  // Does basic block MBB contains reload of Reg from FI?
  bool hasReload(Register Reg, int FI, const MachineBasicBlock *MBB) {
    RegSlotPair RSP(Reg, FI);
    return Reloads.count(MBB) && Reloads[MBB].count(RSP);
  }
};

// Cache used frame indexes during statepoint re-write to re-use them in
// processing next statepoint instruction.
// Two strategies. One is to preserve the size of spill slot while another one
// extends the size of spill slots to reduce the number of them, causing
// the less total frame size. But unspill will have "implicit" any extend.
class FrameIndexesCache {
private:
  struct FrameIndexesPerSize {
    // List of used frame indexes during processing previous statepoints.
    SmallVector<int, 8> Slots;
    // Current index of un-used yet frame index.
    unsigned Index = 0;
  };
  MachineFrameInfo &MFI;
  const TargetRegisterInfo &TRI;
  // Map size to list of frame indexes of this size. If the mode is
  // FixupSCSExtendSlotSize then the key 0 is used to keep all frame indexes.
  // If the size of required spill slot is greater than in a cache then the
  // size will be increased.
  DenseMap<unsigned, FrameIndexesPerSize> Cache;

  // Keeps track of slots reserved for the shared landing pad processing.
  // Initialized from GlobalIndices for the current EHPad.
  SmallSet<int, 8> ReservedSlots;

  // Landing pad can be destination of several statepoints. Every register
  // defined by such statepoints must be spilled to the same stack slot.
  // This map keeps that information.
  DenseMap<const MachineBasicBlock *, SmallVector<RegSlotPair, 8>>
      GlobalIndices;

  FrameIndexesPerSize &getCacheBucket(unsigned Size) {
    // In FixupSCSExtendSlotSize mode the bucket with 0 index is used
    // for all sizes.
    return Cache[FixupSCSExtendSlotSize ? 0 : Size];
  }

public:
  FrameIndexesCache(MachineFrameInfo &MFI, const TargetRegisterInfo &TRI)
      : MFI(MFI), TRI(TRI) {}
  // Reset the current state of used frame indexes. After invocation of
  // this function all frame indexes are available for allocation with
  // the exception of slots reserved for landing pad processing (if any).
  void reset(const MachineBasicBlock *EHPad) {
    for (auto &It : Cache)
      It.second.Index = 0;

    ReservedSlots.clear();
    if (EHPad && GlobalIndices.count(EHPad))
      for (auto &RSP : GlobalIndices[EHPad])
        ReservedSlots.insert(RSP.second);
  }

  // Get frame index to spill the register.
  int getFrameIndex(Register Reg, MachineBasicBlock *EHPad) {
    // Check if slot for Reg is already reserved at EHPad.
    auto It = GlobalIndices.find(EHPad);
    if (It != GlobalIndices.end()) {
      auto &Vec = It->second;
      auto Idx = llvm::find_if(
          Vec, [Reg](RegSlotPair &RSP) { return Reg == RSP.first; });
      if (Idx != Vec.end()) {
        int FI = Idx->second;
        LLVM_DEBUG(dbgs() << "Found global FI " << FI << " for register "
                          << printReg(Reg, &TRI) << " at "
                          << printMBBReference(*EHPad) << "\n");
        assert(ReservedSlots.count(FI) && "using unreserved slot");
        return FI;
      }
    }

    unsigned Size = getRegisterSize(TRI, Reg);
    FrameIndexesPerSize &Line = getCacheBucket(Size);
    while (Line.Index < Line.Slots.size()) {
      int FI = Line.Slots[Line.Index++];
      if (ReservedSlots.count(FI))
        continue;
      // If all sizes are kept together we probably need to extend the
      // spill slot size.
      if (MFI.getObjectSize(FI) < Size) {
        MFI.setObjectSize(FI, Size);
        MFI.setObjectAlignment(FI, Align(Size));
        NumSpillSlotsExtended++;
      }
      return FI;
    }
    int FI = MFI.CreateSpillStackObject(Size, Align(Size));
    NumSpillSlotsAllocated++;
    Line.Slots.push_back(FI);
    ++Line.Index;

    // Remember assignment {Reg, FI} for EHPad
    if (EHPad) {
      GlobalIndices[EHPad].push_back(std::make_pair(Reg, FI));
      LLVM_DEBUG(dbgs() << "Reserved FI " << FI << " for spilling reg "
                        << printReg(Reg, &TRI) << " at landing pad "
                        << printMBBReference(*EHPad) << "\n");
    }

    return FI;
  }

  // Sort all registers to spill in descendent order. In the
  // FixupSCSExtendSlotSize mode it will minimize the total frame size.
  // In non FixupSCSExtendSlotSize mode we can skip this step.
  void sortRegisters(SmallVectorImpl<Register> &Regs) {
    if (!FixupSCSExtendSlotSize)
      return;
    llvm::sort(Regs, [&](Register &A, Register &B) {
      return getRegisterSize(TRI, A) > getRegisterSize(TRI, B);
    });
  }
};

// Describes the state of the current processing statepoint instruction.
class StatepointState {
private:
  // statepoint instruction.
  MachineInstr &MI;
  MachineFunction &MF;
  // If non-null then statepoint is invoke, and this points to the landing pad.
  MachineBasicBlock *EHPad;
  const TargetRegisterInfo &TRI;
  const TargetInstrInfo &TII;
  MachineFrameInfo &MFI;
  // Mask with callee saved registers.
  const uint32_t *Mask;
  // Cache of frame indexes used on previous instruction processing.
  FrameIndexesCache &CacheFI;
  bool AllowGCPtrInCSR;
  // Operands with physical registers requiring spilling.
  SmallVector<unsigned, 8> OpsToSpill;
  // Set of register to spill.
  SmallVector<Register, 8> RegsToSpill;
  // Set of registers to reload after statepoint.
  SmallVector<Register, 8> RegsToReload;
  // Map Register to Frame Slot index.
  DenseMap<Register, int> RegToSlotIdx;

public:
  StatepointState(MachineInstr &MI, const uint32_t *Mask,
                  FrameIndexesCache &CacheFI, bool AllowGCPtrInCSR)
      : MI(MI), MF(*MI.getMF()), TRI(*MF.getSubtarget().getRegisterInfo()),
        TII(*MF.getSubtarget().getInstrInfo()), MFI(MF.getFrameInfo()),
        Mask(Mask), CacheFI(CacheFI), AllowGCPtrInCSR(AllowGCPtrInCSR) {

    // Find statepoint's landing pad, if any.
    EHPad = nullptr;
    MachineBasicBlock *MBB = MI.getParent();
    // Invoke statepoint must be last one in block.
    bool Last = std::none_of(++MI.getIterator(), MBB->end().getInstrIterator(),
                             [](MachineInstr &I) {
                               return I.getOpcode() == TargetOpcode::STATEPOINT;
                             });

    if (!Last)
      return;

    auto IsEHPad = [](MachineBasicBlock *B) { return B->isEHPad(); };

    assert(llvm::count_if(MBB->successors(), IsEHPad) < 2 && "multiple EHPads");

    auto It = llvm::find_if(MBB->successors(), IsEHPad);
    if (It != MBB->succ_end())
      EHPad = *It;
  }

  MachineBasicBlock *getEHPad() const { return EHPad; }

  // Return true if register is callee saved.
  bool isCalleeSaved(Register Reg) { return (Mask[Reg / 32] >> Reg % 32) & 1; }

  // Iterates over statepoint meta args to find caller saver registers.
  // Also cache the size of found registers.
  // Returns true if caller save registers found.
  bool findRegistersToSpill() {
    SmallSet<Register, 8> GCRegs;
    // All GC pointer operands assigned to registers produce new value.
    // Since they're tied to their defs, it is enough to collect def registers.
    for (const auto &Def : MI.defs())
      GCRegs.insert(Def.getReg());

    SmallSet<Register, 8> VisitedRegs;
    for (unsigned Idx = StatepointOpers(&MI).getVarIdx(),
                  EndIdx = MI.getNumOperands();
         Idx < EndIdx; ++Idx) {
      MachineOperand &MO = MI.getOperand(Idx);
      // Leave `undef` operands as is, StackMaps will rewrite them
      // into a constant.
      if (!MO.isReg() || MO.isImplicit() || MO.isUndef())
        continue;
      Register Reg = MO.getReg();
      assert(Reg.isPhysical() && "Only physical regs are expected");

      if (isCalleeSaved(Reg) && (AllowGCPtrInCSR || !GCRegs.contains(Reg)))
        continue;

      LLVM_DEBUG(dbgs() << "Will spill " << printReg(Reg, &TRI) << " at index "
                        << Idx << "\n");

      if (VisitedRegs.insert(Reg).second)
        RegsToSpill.push_back(Reg);
      OpsToSpill.push_back(Idx);
    }
    CacheFI.sortRegisters(RegsToSpill);
    return !RegsToSpill.empty();
  }

  // Spill all caller saved registers right before statepoint instruction.
  // Remember frame index where register is spilled.
  void spillRegisters() {
    for (Register Reg : RegsToSpill) {
      int FI = CacheFI.getFrameIndex(Reg, EHPad);

      NumSpilledRegisters++;
      RegToSlotIdx[Reg] = FI;

      LLVM_DEBUG(dbgs() << "Spilling " << printReg(Reg, &TRI) << " to FI " << FI
                        << "\n");

      // Perform trivial copy propagation
      bool IsKill = true;
      MachineBasicBlock::iterator InsertBefore(MI);
      Reg = performCopyPropagation(Reg, InsertBefore, IsKill, TII, TRI);
      const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg);

      LLVM_DEBUG(dbgs() << "Insert spill before " << *InsertBefore);
      TII.storeRegToStackSlot(*MI.getParent(), InsertBefore, Reg, IsKill, FI,
                              RC, &TRI, Register());
    }
  }

  void insertReloadBefore(unsigned Reg, MachineBasicBlock::iterator It,
                          MachineBasicBlock *MBB) {
    const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg);
    int FI = RegToSlotIdx[Reg];
    if (It != MBB->end()) {
      TII.loadRegFromStackSlot(*MBB, It, Reg, FI, RC, &TRI, Register());
      return;
    }

    // To insert reload at the end of MBB, insert it before last instruction
    // and then swap them.
    assert(!MBB->empty() && "Empty block");
    --It;
    TII.loadRegFromStackSlot(*MBB, It, Reg, FI, RC, &TRI, Register());
    MachineInstr *Reload = It->getPrevNode();
    int Dummy = 0;
    (void)Dummy;
    assert(TII.isLoadFromStackSlot(*Reload, Dummy) == Reg);
    assert(Dummy == FI);
    MBB->remove(Reload);
    MBB->insertAfter(It, Reload);
  }

  // Insert reloads of (relocated) registers spilled in statepoint.
  void insertReloads(MachineInstr *NewStatepoint, RegReloadCache &RC) {
    MachineBasicBlock *MBB = NewStatepoint->getParent();
    auto InsertPoint = std::next(NewStatepoint->getIterator());

    for (auto Reg : RegsToReload) {
      insertReloadBefore(Reg, InsertPoint, MBB);
      LLVM_DEBUG(dbgs() << "Reloading " << printReg(Reg, &TRI) << " from FI "
                        << RegToSlotIdx[Reg] << " after statepoint\n");

      if (EHPad && !RC.hasReload(Reg, RegToSlotIdx[Reg], EHPad)) {
        RC.recordReload(Reg, RegToSlotIdx[Reg], EHPad);
        auto EHPadInsertPoint =
            EHPad->SkipPHIsLabelsAndDebug(EHPad->begin(), Reg);
        insertReloadBefore(Reg, EHPadInsertPoint, EHPad);
        LLVM_DEBUG(dbgs() << "...also reload at EHPad "
                          << printMBBReference(*EHPad) << "\n");
      }
    }
  }

  // Re-write statepoint machine instruction to replace caller saved operands
  // with indirect memory location (frame index).
  MachineInstr *rewriteStatepoint() {
    MachineInstr *NewMI =
        MF.CreateMachineInstr(TII.get(MI.getOpcode()), MI.getDebugLoc(), true);
    MachineInstrBuilder MIB(MF, NewMI);

    unsigned NumOps = MI.getNumOperands();

    // New indices for the remaining defs.
    SmallVector<unsigned, 8> NewIndices;
    unsigned NumDefs = MI.getNumDefs();
    for (unsigned I = 0; I < NumDefs; ++I) {
      MachineOperand &DefMO = MI.getOperand(I);
      assert(DefMO.isReg() && DefMO.isDef() && "Expected Reg Def operand");
      Register Reg = DefMO.getReg();
      assert(DefMO.isTied() && "Def is expected to be tied");
      // We skipped undef uses and did not spill them, so we should not
      // proceed with defs here.
      if (MI.getOperand(MI.findTiedOperandIdx(I)).isUndef()) {
        if (AllowGCPtrInCSR) {
          NewIndices.push_back(NewMI->getNumOperands());
          MIB.addReg(Reg, RegState::Define);
        }
        continue;
      }
      if (!AllowGCPtrInCSR) {
        assert(is_contained(RegsToSpill, Reg));
        RegsToReload.push_back(Reg);
      } else {
        if (isCalleeSaved(Reg)) {
          NewIndices.push_back(NewMI->getNumOperands());
          MIB.addReg(Reg, RegState::Define);
        } else {
          NewIndices.push_back(NumOps);
          RegsToReload.push_back(Reg);
        }
      }
    }

    // Add End marker.
    OpsToSpill.push_back(MI.getNumOperands());
    unsigned CurOpIdx = 0;

    for (unsigned I = NumDefs; I < MI.getNumOperands(); ++I) {
      MachineOperand &MO = MI.getOperand(I);
      if (I == OpsToSpill[CurOpIdx]) {
        int FI = RegToSlotIdx[MO.getReg()];
        MIB.addImm(StackMaps::IndirectMemRefOp);
        MIB.addImm(getRegisterSize(TRI, MO.getReg()));
        assert(MO.isReg() && "Should be register");
        assert(MO.getReg().isPhysical() && "Should be physical register");
        MIB.addFrameIndex(FI);
        MIB.addImm(0);
        ++CurOpIdx;
      } else {
        MIB.add(MO);
        unsigned OldDef;
        if (AllowGCPtrInCSR && MI.isRegTiedToDefOperand(I, &OldDef)) {
          assert(OldDef < NumDefs);
          assert(NewIndices[OldDef] < NumOps);
          MIB->tieOperands(NewIndices[OldDef], MIB->getNumOperands() - 1);
        }
      }
    }
    assert(CurOpIdx == (OpsToSpill.size() - 1) && "Not all operands processed");
    // Add mem operands.
    NewMI->setMemRefs(MF, MI.memoperands());
    for (auto It : RegToSlotIdx) {
      Register R = It.first;
      int FrameIndex = It.second;
      auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);
      MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad;
      if (is_contained(RegsToReload, R))
        Flags |= MachineMemOperand::MOStore;
      auto *MMO =
          MF.getMachineMemOperand(PtrInfo, Flags, getRegisterSize(TRI, R),
                                  MFI.getObjectAlign(FrameIndex));
      NewMI->addMemOperand(MF, MMO);
    }

    // Insert new statepoint and erase old one.
    MI.getParent()->insert(MI, NewMI);

    LLVM_DEBUG(dbgs() << "rewritten statepoint to : " << *NewMI << "\n");
    MI.eraseFromParent();
    return NewMI;
  }
};

class StatepointProcessor {
private:
  MachineFunction &MF;
  const TargetRegisterInfo &TRI;
  FrameIndexesCache CacheFI;
  RegReloadCache ReloadCache;

public:
  StatepointProcessor(MachineFunction &MF)
      : MF(MF), TRI(*MF.getSubtarget().getRegisterInfo()),
        CacheFI(MF.getFrameInfo(), TRI) {}

  bool process(MachineInstr &MI, bool AllowGCPtrInCSR) {
    StatepointOpers SO(&MI);
    uint64_t Flags = SO.getFlags();
    // Do nothing for LiveIn, it supports all registers.
    if (Flags & (uint64_t)StatepointFlags::DeoptLiveIn)
      return false;
    LLVM_DEBUG(dbgs() << "\nMBB " << MI.getParent()->getNumber() << " "
                      << MI.getParent()->getName() << " : process statepoint "
                      << MI);
    CallingConv::ID CC = SO.getCallingConv();
    const uint32_t *Mask = TRI.getCallPreservedMask(MF, CC);
    StatepointState SS(MI, Mask, CacheFI, AllowGCPtrInCSR);
    CacheFI.reset(SS.getEHPad());

    if (!SS.findRegistersToSpill())
      return false;

    SS.spillRegisters();
    auto *NewStatepoint = SS.rewriteStatepoint();
    SS.insertReloads(NewStatepoint, ReloadCache);
    return true;
  }
};
} // namespace

bool FixupStatepointCallerSaved::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const Function &F = MF.getFunction();
  if (!F.hasGC())
    return false;

  SmallVector<MachineInstr *, 16> Statepoints;
  for (MachineBasicBlock &BB : MF)
    for (MachineInstr &I : BB)
      if (I.getOpcode() == TargetOpcode::STATEPOINT)
        Statepoints.push_back(&I);

  if (Statepoints.empty())
    return false;

  bool Changed = false;
  StatepointProcessor SPP(MF);
  unsigned NumStatepoints = 0;
  bool AllowGCPtrInCSR = PassGCPtrInCSR;
  for (MachineInstr *I : Statepoints) {
    ++NumStatepoints;
    if (MaxStatepointsWithRegs.getNumOccurrences() &&
        NumStatepoints >= MaxStatepointsWithRegs)
      AllowGCPtrInCSR = false;
    Changed |= SPP.process(*I, AllowGCPtrInCSR);
  }
  return Changed;
}
