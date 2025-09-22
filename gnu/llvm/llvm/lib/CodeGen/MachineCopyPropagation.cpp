//===- MachineCopyPropagation.cpp - Machine Copy Propagation Pass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an extremely simple MachineInstr-level copy propagation pass.
//
// This pass forwards the source of COPYs to the users of their destinations
// when doing so is legal.  For example:
//
//   %reg1 = COPY %reg0
//   ...
//   ... = OP %reg1
//
// If
//   - %reg0 has not been clobbered by the time of the use of %reg1
//   - the register class constraints are satisfied
//   - the COPY def is the only value that reaches OP
// then this pass replaces the above with:
//
//   %reg1 = COPY %reg0
//   ...
//   ... = OP %reg0
//
// This pass also removes some redundant COPYs.  For example:
//
//    %R1 = COPY %R0
//    ... // No clobber of %R1
//    %R0 = COPY %R1 <<< Removed
//
// or
//
//    %R1 = COPY %R0
//    ... // No clobber of %R0
//    %R1 = COPY %R0 <<< Removed
//
// or
//
//    $R0 = OP ...
//    ... // No read/clobber of $R0 and $R1
//    $R1 = COPY $R0 // $R0 is killed
// Replace $R0 with $R1 and remove the COPY
//    $R1 = OP ...
//    ...
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "machine-cp"

STATISTIC(NumDeletes, "Number of dead copies deleted");
STATISTIC(NumCopyForwards, "Number of copy uses forwarded");
STATISTIC(NumCopyBackwardPropagated, "Number of copy defs backward propagated");
STATISTIC(SpillageChainsLength, "Length of spillage chains");
STATISTIC(NumSpillageChains, "Number of spillage chains");
DEBUG_COUNTER(FwdCounter, "machine-cp-fwd",
              "Controls which register COPYs are forwarded");

static cl::opt<bool> MCPUseCopyInstr("mcp-use-is-copy-instr", cl::init(false),
                                     cl::Hidden);
static cl::opt<cl::boolOrDefault>
    EnableSpillageCopyElimination("enable-spill-copy-elim", cl::Hidden);

namespace {

static std::optional<DestSourcePair> isCopyInstr(const MachineInstr &MI,
                                                 const TargetInstrInfo &TII,
                                                 bool UseCopyInstr) {
  if (UseCopyInstr)
    return TII.isCopyInstr(MI);

  if (MI.isCopy())
    return std::optional<DestSourcePair>(
        DestSourcePair{MI.getOperand(0), MI.getOperand(1)});

  return std::nullopt;
}

class CopyTracker {
  struct CopyInfo {
    MachineInstr *MI, *LastSeenUseInCopy;
    SmallVector<MCRegister, 4> DefRegs;
    bool Avail;
  };

  DenseMap<MCRegUnit, CopyInfo> Copies;

public:
  /// Mark all of the given registers and their subregisters as unavailable for
  /// copying.
  void markRegsUnavailable(ArrayRef<MCRegister> Regs,
                           const TargetRegisterInfo &TRI) {
    for (MCRegister Reg : Regs) {
      // Source of copy is no longer available for propagation.
      for (MCRegUnit Unit : TRI.regunits(Reg)) {
        auto CI = Copies.find(Unit);
        if (CI != Copies.end())
          CI->second.Avail = false;
      }
    }
  }

  /// Remove register from copy maps.
  void invalidateRegister(MCRegister Reg, const TargetRegisterInfo &TRI,
                          const TargetInstrInfo &TII, bool UseCopyInstr) {
    // Since Reg might be a subreg of some registers, only invalidate Reg is not
    // enough. We have to find the COPY defines Reg or registers defined by Reg
    // and invalidate all of them. Similarly, we must invalidate all of the
    // the subregisters used in the source of the COPY.
    SmallSet<MCRegUnit, 8> RegUnitsToInvalidate;
    auto InvalidateCopy = [&](MachineInstr *MI) {
      std::optional<DestSourcePair> CopyOperands =
          isCopyInstr(*MI, TII, UseCopyInstr);
      assert(CopyOperands && "Expect copy");

      auto Dest = TRI.regunits(CopyOperands->Destination->getReg().asMCReg());
      auto Src = TRI.regunits(CopyOperands->Source->getReg().asMCReg());
      RegUnitsToInvalidate.insert(Dest.begin(), Dest.end());
      RegUnitsToInvalidate.insert(Src.begin(), Src.end());
    };

    for (MCRegUnit Unit : TRI.regunits(Reg)) {
      auto I = Copies.find(Unit);
      if (I != Copies.end()) {
        if (MachineInstr *MI = I->second.MI)
          InvalidateCopy(MI);
        if (MachineInstr *MI = I->second.LastSeenUseInCopy)
          InvalidateCopy(MI);
      }
    }
    for (MCRegUnit Unit : RegUnitsToInvalidate)
      Copies.erase(Unit);
  }

  /// Clobber a single register, removing it from the tracker's copy maps.
  void clobberRegister(MCRegister Reg, const TargetRegisterInfo &TRI,
                       const TargetInstrInfo &TII, bool UseCopyInstr) {
    for (MCRegUnit Unit : TRI.regunits(Reg)) {
      auto I = Copies.find(Unit);
      if (I != Copies.end()) {
        // When we clobber the source of a copy, we need to clobber everything
        // it defined.
        markRegsUnavailable(I->second.DefRegs, TRI);
        // When we clobber the destination of a copy, we need to clobber the
        // whole register it defined.
        if (MachineInstr *MI = I->second.MI) {
          std::optional<DestSourcePair> CopyOperands =
              isCopyInstr(*MI, TII, UseCopyInstr);

          MCRegister Def = CopyOperands->Destination->getReg().asMCReg();
          MCRegister Src = CopyOperands->Source->getReg().asMCReg();

          markRegsUnavailable(Def, TRI);

          // Since we clobber the destination of a copy, the semantic of Src's
          // "DefRegs" to contain Def is no longer effectual. We will also need
          // to remove the record from the copy maps that indicates Src defined
          // Def. Failing to do so might cause the target to miss some
          // opportunities to further eliminate redundant copy instructions.
          // Consider the following sequence during the
          // ForwardCopyPropagateBlock procedure:
          // L1: r0 = COPY r9     <- TrackMI
          // L2: r0 = COPY r8     <- TrackMI (Remove r9 defined r0 from tracker)
          // L3: use r0           <- Remove L2 from MaybeDeadCopies
          // L4: early-clobber r9 <- Clobber r9 (L2 is still valid in tracker)
          // L5: r0 = COPY r8     <- Remove NopCopy
          for (MCRegUnit SrcUnit : TRI.regunits(Src)) {
            auto SrcCopy = Copies.find(SrcUnit);
            if (SrcCopy != Copies.end() && SrcCopy->second.LastSeenUseInCopy) {
              // If SrcCopy defines multiple values, we only need
              // to erase the record for Def in DefRegs.
              for (auto itr = SrcCopy->second.DefRegs.begin();
                   itr != SrcCopy->second.DefRegs.end(); itr++) {
                if (*itr == Def) {
                  SrcCopy->second.DefRegs.erase(itr);
                  // If DefReg becomes empty after removal, we can remove the
                  // SrcCopy from the tracker's copy maps. We only remove those
                  // entries solely record the Def is defined by Src. If an
                  // entry also contains the definition record of other Def'
                  // registers, it cannot be cleared.
                  if (SrcCopy->second.DefRegs.empty() && !SrcCopy->second.MI) {
                    Copies.erase(SrcCopy);
                  }
                  break;
                }
              }
            }
          }
        }
        // Now we can erase the copy.
        Copies.erase(I);
      }
    }
  }

  /// Add this copy's registers into the tracker's copy maps.
  void trackCopy(MachineInstr *MI, const TargetRegisterInfo &TRI,
                 const TargetInstrInfo &TII, bool UseCopyInstr) {
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*MI, TII, UseCopyInstr);
    assert(CopyOperands && "Tracking non-copy?");

    MCRegister Src = CopyOperands->Source->getReg().asMCReg();
    MCRegister Def = CopyOperands->Destination->getReg().asMCReg();

    // Remember Def is defined by the copy.
    for (MCRegUnit Unit : TRI.regunits(Def))
      Copies[Unit] = {MI, nullptr, {}, true};

    // Remember source that's copied to Def. Once it's clobbered, then
    // it's no longer available for copy propagation.
    for (MCRegUnit Unit : TRI.regunits(Src)) {
      auto I = Copies.insert({Unit, {nullptr, nullptr, {}, false}});
      auto &Copy = I.first->second;
      if (!is_contained(Copy.DefRegs, Def))
        Copy.DefRegs.push_back(Def);
      Copy.LastSeenUseInCopy = MI;
    }
  }

  bool hasAnyCopies() {
    return !Copies.empty();
  }

  MachineInstr *findCopyForUnit(MCRegUnit RegUnit,
                                const TargetRegisterInfo &TRI,
                                bool MustBeAvailable = false) {
    auto CI = Copies.find(RegUnit);
    if (CI == Copies.end())
      return nullptr;
    if (MustBeAvailable && !CI->second.Avail)
      return nullptr;
    return CI->second.MI;
  }

  MachineInstr *findCopyDefViaUnit(MCRegUnit RegUnit,
                                   const TargetRegisterInfo &TRI) {
    auto CI = Copies.find(RegUnit);
    if (CI == Copies.end())
      return nullptr;
    if (CI->second.DefRegs.size() != 1)
      return nullptr;
    MCRegUnit RU = *TRI.regunits(CI->second.DefRegs[0]).begin();
    return findCopyForUnit(RU, TRI, true);
  }

  MachineInstr *findAvailBackwardCopy(MachineInstr &I, MCRegister Reg,
                                      const TargetRegisterInfo &TRI,
                                      const TargetInstrInfo &TII,
                                      bool UseCopyInstr) {
    MCRegUnit RU = *TRI.regunits(Reg).begin();
    MachineInstr *AvailCopy = findCopyDefViaUnit(RU, TRI);

    if (!AvailCopy)
      return nullptr;

    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*AvailCopy, TII, UseCopyInstr);
    Register AvailSrc = CopyOperands->Source->getReg();
    Register AvailDef = CopyOperands->Destination->getReg();
    if (!TRI.isSubRegisterEq(AvailSrc, Reg))
      return nullptr;

    for (const MachineInstr &MI :
         make_range(AvailCopy->getReverseIterator(), I.getReverseIterator()))
      for (const MachineOperand &MO : MI.operands())
        if (MO.isRegMask())
          // FIXME: Shall we simultaneously invalidate AvailSrc or AvailDef?
          if (MO.clobbersPhysReg(AvailSrc) || MO.clobbersPhysReg(AvailDef))
            return nullptr;

    return AvailCopy;
  }

  MachineInstr *findAvailCopy(MachineInstr &DestCopy, MCRegister Reg,
                              const TargetRegisterInfo &TRI,
                              const TargetInstrInfo &TII, bool UseCopyInstr) {
    // We check the first RegUnit here, since we'll only be interested in the
    // copy if it copies the entire register anyway.
    MCRegUnit RU = *TRI.regunits(Reg).begin();
    MachineInstr *AvailCopy =
        findCopyForUnit(RU, TRI, /*MustBeAvailable=*/true);

    if (!AvailCopy)
      return nullptr;

    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*AvailCopy, TII, UseCopyInstr);
    Register AvailSrc = CopyOperands->Source->getReg();
    Register AvailDef = CopyOperands->Destination->getReg();
    if (!TRI.isSubRegisterEq(AvailDef, Reg))
      return nullptr;

    // Check that the available copy isn't clobbered by any regmasks between
    // itself and the destination.
    for (const MachineInstr &MI :
         make_range(AvailCopy->getIterator(), DestCopy.getIterator()))
      for (const MachineOperand &MO : MI.operands())
        if (MO.isRegMask())
          if (MO.clobbersPhysReg(AvailSrc) || MO.clobbersPhysReg(AvailDef))
            return nullptr;

    return AvailCopy;
  }

  // Find last COPY that defines Reg before Current MachineInstr.
  MachineInstr *findLastSeenDefInCopy(const MachineInstr &Current,
                                      MCRegister Reg,
                                      const TargetRegisterInfo &TRI,
                                      const TargetInstrInfo &TII,
                                      bool UseCopyInstr) {
    MCRegUnit RU = *TRI.regunits(Reg).begin();
    auto CI = Copies.find(RU);
    if (CI == Copies.end() || !CI->second.Avail)
      return nullptr;

    MachineInstr *DefCopy = CI->second.MI;
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*DefCopy, TII, UseCopyInstr);
    Register Def = CopyOperands->Destination->getReg();
    if (!TRI.isSubRegisterEq(Def, Reg))
      return nullptr;

    for (const MachineInstr &MI :
         make_range(static_cast<const MachineInstr *>(DefCopy)->getIterator(),
                    Current.getIterator()))
      for (const MachineOperand &MO : MI.operands())
        if (MO.isRegMask())
          if (MO.clobbersPhysReg(Def)) {
            LLVM_DEBUG(dbgs() << "MCP: Removed tracking of "
                              << printReg(Def, &TRI) << "\n");
            return nullptr;
          }

    return DefCopy;
  }

  // Find last COPY that uses Reg.
  MachineInstr *findLastSeenUseInCopy(MCRegister Reg,
                                      const TargetRegisterInfo &TRI) {
    MCRegUnit RU = *TRI.regunits(Reg).begin();
    auto CI = Copies.find(RU);
    if (CI == Copies.end())
      return nullptr;
    return CI->second.LastSeenUseInCopy;
  }

  void clear() {
    Copies.clear();
  }
};

class MachineCopyPropagation : public MachineFunctionPass {
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const MachineRegisterInfo *MRI = nullptr;

  // Return true if this is a copy instruction and false otherwise.
  bool UseCopyInstr;

public:
  static char ID; // Pass identification, replacement for typeid

  MachineCopyPropagation(bool CopyInstr = false)
      : MachineFunctionPass(ID), UseCopyInstr(CopyInstr || MCPUseCopyInstr) {
    initializeMachineCopyPropagationPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  typedef enum { DebugUse = false, RegularUse = true } DebugType;

  void ReadRegister(MCRegister Reg, MachineInstr &Reader, DebugType DT);
  void readSuccessorLiveIns(const MachineBasicBlock &MBB);
  void ForwardCopyPropagateBlock(MachineBasicBlock &MBB);
  void BackwardCopyPropagateBlock(MachineBasicBlock &MBB);
  void EliminateSpillageCopies(MachineBasicBlock &MBB);
  bool eraseIfRedundant(MachineInstr &Copy, MCRegister Src, MCRegister Def);
  void forwardUses(MachineInstr &MI);
  void propagateDefs(MachineInstr &MI);
  bool isForwardableRegClassCopy(const MachineInstr &Copy,
                                 const MachineInstr &UseI, unsigned UseIdx);
  bool isBackwardPropagatableRegClassCopy(const MachineInstr &Copy,
                                          const MachineInstr &UseI,
                                          unsigned UseIdx);
  bool hasImplicitOverlap(const MachineInstr &MI, const MachineOperand &Use);
  bool hasOverlappingMultipleDef(const MachineInstr &MI,
                                 const MachineOperand &MODef, Register Def);

  /// Candidates for deletion.
  SmallSetVector<MachineInstr *, 8> MaybeDeadCopies;

  /// Multimap tracking debug users in current BB
  DenseMap<MachineInstr *, SmallSet<MachineInstr *, 2>> CopyDbgUsers;

  CopyTracker Tracker;

  bool Changed = false;
};

} // end anonymous namespace

char MachineCopyPropagation::ID = 0;

char &llvm::MachineCopyPropagationID = MachineCopyPropagation::ID;

INITIALIZE_PASS(MachineCopyPropagation, DEBUG_TYPE,
                "Machine Copy Propagation Pass", false, false)

void MachineCopyPropagation::ReadRegister(MCRegister Reg, MachineInstr &Reader,
                                          DebugType DT) {
  // If 'Reg' is defined by a copy, the copy is no longer a candidate
  // for elimination. If a copy is "read" by a debug user, record the user
  // for propagation.
  for (MCRegUnit Unit : TRI->regunits(Reg)) {
    if (MachineInstr *Copy = Tracker.findCopyForUnit(Unit, *TRI)) {
      if (DT == RegularUse) {
        LLVM_DEBUG(dbgs() << "MCP: Copy is used - not dead: "; Copy->dump());
        MaybeDeadCopies.remove(Copy);
      } else {
        CopyDbgUsers[Copy].insert(&Reader);
      }
    }
  }
}

void MachineCopyPropagation::readSuccessorLiveIns(
    const MachineBasicBlock &MBB) {
  if (MaybeDeadCopies.empty())
    return;

  // If a copy result is livein to a successor, it is not dead.
  for (const MachineBasicBlock *Succ : MBB.successors()) {
    for (const auto &LI : Succ->liveins()) {
      for (MCRegUnit Unit : TRI->regunits(LI.PhysReg)) {
        if (MachineInstr *Copy = Tracker.findCopyForUnit(Unit, *TRI))
          MaybeDeadCopies.remove(Copy);
      }
    }
  }
}

/// Return true if \p PreviousCopy did copy register \p Src to register \p Def.
/// This fact may have been obscured by sub register usage or may not be true at
/// all even though Src and Def are subregisters of the registers used in
/// PreviousCopy. e.g.
/// isNopCopy("ecx = COPY eax", AX, CX) == true
/// isNopCopy("ecx = COPY eax", AH, CL) == false
static bool isNopCopy(const MachineInstr &PreviousCopy, MCRegister Src,
                      MCRegister Def, const TargetRegisterInfo *TRI,
                      const TargetInstrInfo *TII, bool UseCopyInstr) {

  std::optional<DestSourcePair> CopyOperands =
      isCopyInstr(PreviousCopy, *TII, UseCopyInstr);
  MCRegister PreviousSrc = CopyOperands->Source->getReg().asMCReg();
  MCRegister PreviousDef = CopyOperands->Destination->getReg().asMCReg();
  if (Src == PreviousSrc && Def == PreviousDef)
    return true;
  if (!TRI->isSubRegister(PreviousSrc, Src))
    return false;
  unsigned SubIdx = TRI->getSubRegIndex(PreviousSrc, Src);
  return SubIdx == TRI->getSubRegIndex(PreviousDef, Def);
}

/// Remove instruction \p Copy if there exists a previous copy that copies the
/// register \p Src to the register \p Def; This may happen indirectly by
/// copying the super registers.
bool MachineCopyPropagation::eraseIfRedundant(MachineInstr &Copy,
                                              MCRegister Src, MCRegister Def) {
  // Avoid eliminating a copy from/to a reserved registers as we cannot predict
  // the value (Example: The sparc zero register is writable but stays zero).
  if (MRI->isReserved(Src) || MRI->isReserved(Def))
    return false;

  // Search for an existing copy.
  MachineInstr *PrevCopy =
      Tracker.findAvailCopy(Copy, Def, *TRI, *TII, UseCopyInstr);
  if (!PrevCopy)
    return false;

  auto PrevCopyOperands = isCopyInstr(*PrevCopy, *TII, UseCopyInstr);
  // Check that the existing copy uses the correct sub registers.
  if (PrevCopyOperands->Destination->isDead())
    return false;
  if (!isNopCopy(*PrevCopy, Src, Def, TRI, TII, UseCopyInstr))
    return false;

  LLVM_DEBUG(dbgs() << "MCP: copy is a NOP, removing: "; Copy.dump());

  // Copy was redundantly redefining either Src or Def. Remove earlier kill
  // flags between Copy and PrevCopy because the value will be reused now.
  std::optional<DestSourcePair> CopyOperands =
      isCopyInstr(Copy, *TII, UseCopyInstr);
  assert(CopyOperands);

  Register CopyDef = CopyOperands->Destination->getReg();
  assert(CopyDef == Src || CopyDef == Def);
  for (MachineInstr &MI :
       make_range(PrevCopy->getIterator(), Copy.getIterator()))
    MI.clearRegisterKills(CopyDef, TRI);

  // Clear undef flag from remaining copy if needed.
  if (!CopyOperands->Source->isUndef()) {
    PrevCopy->getOperand(PrevCopyOperands->Source->getOperandNo())
        .setIsUndef(false);
  }

  Copy.eraseFromParent();
  Changed = true;
  ++NumDeletes;
  return true;
}

bool MachineCopyPropagation::isBackwardPropagatableRegClassCopy(
    const MachineInstr &Copy, const MachineInstr &UseI, unsigned UseIdx) {
  std::optional<DestSourcePair> CopyOperands =
      isCopyInstr(Copy, *TII, UseCopyInstr);
  Register Def = CopyOperands->Destination->getReg();

  if (const TargetRegisterClass *URC =
          UseI.getRegClassConstraint(UseIdx, TII, TRI))
    return URC->contains(Def);

  // We don't process further if UseI is a COPY, since forward copy propagation
  // should handle that.
  return false;
}

/// Decide whether we should forward the source of \param Copy to its use in
/// \param UseI based on the physical register class constraints of the opcode
/// and avoiding introducing more cross-class COPYs.
bool MachineCopyPropagation::isForwardableRegClassCopy(const MachineInstr &Copy,
                                                       const MachineInstr &UseI,
                                                       unsigned UseIdx) {
  std::optional<DestSourcePair> CopyOperands =
      isCopyInstr(Copy, *TII, UseCopyInstr);
  Register CopySrcReg = CopyOperands->Source->getReg();

  // If the new register meets the opcode register constraints, then allow
  // forwarding.
  if (const TargetRegisterClass *URC =
          UseI.getRegClassConstraint(UseIdx, TII, TRI))
    return URC->contains(CopySrcReg);

  auto UseICopyOperands = isCopyInstr(UseI, *TII, UseCopyInstr);
  if (!UseICopyOperands)
    return false;

  /// COPYs don't have register class constraints, so if the user instruction
  /// is a COPY, we just try to avoid introducing additional cross-class
  /// COPYs.  For example:
  ///
  ///   RegClassA = COPY RegClassB  // Copy parameter
  ///   ...
  ///   RegClassB = COPY RegClassA  // UseI parameter
  ///
  /// which after forwarding becomes
  ///
  ///   RegClassA = COPY RegClassB
  ///   ...
  ///   RegClassB = COPY RegClassB
  ///
  /// so we have reduced the number of cross-class COPYs and potentially
  /// introduced a nop COPY that can be removed.

  // Allow forwarding if src and dst belong to any common class, so long as they
  // don't belong to any (possibly smaller) common class that requires copies to
  // go via a different class.
  Register UseDstReg = UseICopyOperands->Destination->getReg();
  bool Found = false;
  bool IsCrossClass = false;
  for (const TargetRegisterClass *RC : TRI->regclasses()) {
    if (RC->contains(CopySrcReg) && RC->contains(UseDstReg)) {
      Found = true;
      if (TRI->getCrossCopyRegClass(RC) != RC) {
        IsCrossClass = true;
        break;
      }
    }
  }
  if (!Found)
    return false;
  if (!IsCrossClass)
    return true;
  // The forwarded copy would be cross-class. Only do this if the original copy
  // was also cross-class.
  Register CopyDstReg = CopyOperands->Destination->getReg();
  for (const TargetRegisterClass *RC : TRI->regclasses()) {
    if (RC->contains(CopySrcReg) && RC->contains(CopyDstReg) &&
        TRI->getCrossCopyRegClass(RC) != RC)
      return true;
  }
  return false;
}

/// Check that \p MI does not have implicit uses that overlap with it's \p Use
/// operand (the register being replaced), since these can sometimes be
/// implicitly tied to other operands.  For example, on AMDGPU:
///
/// V_MOVRELS_B32_e32 %VGPR2, %M0<imp-use>, %EXEC<imp-use>, %VGPR2_VGPR3_VGPR4_VGPR5<imp-use>
///
/// the %VGPR2 is implicitly tied to the larger reg operand, but we have no
/// way of knowing we need to update the latter when updating the former.
bool MachineCopyPropagation::hasImplicitOverlap(const MachineInstr &MI,
                                                const MachineOperand &Use) {
  for (const MachineOperand &MIUse : MI.uses())
    if (&MIUse != &Use && MIUse.isReg() && MIUse.isImplicit() &&
        MIUse.isUse() && TRI->regsOverlap(Use.getReg(), MIUse.getReg()))
      return true;

  return false;
}

/// For an MI that has multiple definitions, check whether \p MI has
/// a definition that overlaps with another of its definitions.
/// For example, on ARM: umull   r9, r9, lr, r0
/// The umull instruction is unpredictable unless RdHi and RdLo are different.
bool MachineCopyPropagation::hasOverlappingMultipleDef(
    const MachineInstr &MI, const MachineOperand &MODef, Register Def) {
  for (const MachineOperand &MIDef : MI.all_defs()) {
    if ((&MIDef != &MODef) && MIDef.isReg() &&
        TRI->regsOverlap(Def, MIDef.getReg()))
      return true;
  }

  return false;
}

/// Look for available copies whose destination register is used by \p MI and
/// replace the use in \p MI with the copy's source register.
void MachineCopyPropagation::forwardUses(MachineInstr &MI) {
  if (!Tracker.hasAnyCopies())
    return;

  // Look for non-tied explicit vreg uses that have an active COPY
  // instruction that defines the physical register allocated to them.
  // Replace the vreg with the source of the active COPY.
  for (unsigned OpIdx = 0, OpEnd = MI.getNumOperands(); OpIdx < OpEnd;
       ++OpIdx) {
    MachineOperand &MOUse = MI.getOperand(OpIdx);
    // Don't forward into undef use operands since doing so can cause problems
    // with the machine verifier, since it doesn't treat undef reads as reads,
    // so we can end up with a live range that ends on an undef read, leading to
    // an error that the live range doesn't end on a read of the live range
    // register.
    if (!MOUse.isReg() || MOUse.isTied() || MOUse.isUndef() || MOUse.isDef() ||
        MOUse.isImplicit())
      continue;

    if (!MOUse.getReg())
      continue;

    // Check that the register is marked 'renamable' so we know it is safe to
    // rename it without violating any constraints that aren't expressed in the
    // IR (e.g. ABI or opcode requirements).
    if (!MOUse.isRenamable())
      continue;

    MachineInstr *Copy = Tracker.findAvailCopy(MI, MOUse.getReg().asMCReg(),
                                               *TRI, *TII, UseCopyInstr);
    if (!Copy)
      continue;

    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*Copy, *TII, UseCopyInstr);
    Register CopyDstReg = CopyOperands->Destination->getReg();
    const MachineOperand &CopySrc = *CopyOperands->Source;
    Register CopySrcReg = CopySrc.getReg();

    Register ForwardedReg = CopySrcReg;
    // MI might use a sub-register of the Copy destination, in which case the
    // forwarded register is the matching sub-register of the Copy source.
    if (MOUse.getReg() != CopyDstReg) {
      unsigned SubRegIdx = TRI->getSubRegIndex(CopyDstReg, MOUse.getReg());
      assert(SubRegIdx &&
             "MI source is not a sub-register of Copy destination");
      ForwardedReg = TRI->getSubReg(CopySrcReg, SubRegIdx);
      if (!ForwardedReg) {
        LLVM_DEBUG(dbgs() << "MCP: Copy source does not have sub-register "
                          << TRI->getSubRegIndexName(SubRegIdx) << '\n');
        continue;
      }
    }

    // Don't forward COPYs of reserved regs unless they are constant.
    if (MRI->isReserved(CopySrcReg) && !MRI->isConstantPhysReg(CopySrcReg))
      continue;

    if (!isForwardableRegClassCopy(*Copy, MI, OpIdx))
      continue;

    if (hasImplicitOverlap(MI, MOUse))
      continue;

    // Check that the instruction is not a copy that partially overwrites the
    // original copy source that we are about to use. The tracker mechanism
    // cannot cope with that.
    if (isCopyInstr(MI, *TII, UseCopyInstr) &&
        MI.modifiesRegister(CopySrcReg, TRI) &&
        !MI.definesRegister(CopySrcReg, /*TRI=*/nullptr)) {
      LLVM_DEBUG(dbgs() << "MCP: Copy source overlap with dest in " << MI);
      continue;
    }

    if (!DebugCounter::shouldExecute(FwdCounter)) {
      LLVM_DEBUG(dbgs() << "MCP: Skipping forwarding due to debug counter:\n  "
                        << MI);
      continue;
    }

    LLVM_DEBUG(dbgs() << "MCP: Replacing " << printReg(MOUse.getReg(), TRI)
                      << "\n     with " << printReg(ForwardedReg, TRI)
                      << "\n     in " << MI << "     from " << *Copy);

    MOUse.setReg(ForwardedReg);

    if (!CopySrc.isRenamable())
      MOUse.setIsRenamable(false);
    MOUse.setIsUndef(CopySrc.isUndef());

    LLVM_DEBUG(dbgs() << "MCP: After replacement: " << MI << "\n");

    // Clear kill markers that may have been invalidated.
    for (MachineInstr &KMI :
         make_range(Copy->getIterator(), std::next(MI.getIterator())))
      KMI.clearRegisterKills(CopySrcReg, TRI);

    ++NumCopyForwards;
    Changed = true;
  }
}

void MachineCopyPropagation::ForwardCopyPropagateBlock(MachineBasicBlock &MBB) {
  LLVM_DEBUG(dbgs() << "MCP: ForwardCopyPropagateBlock " << MBB.getName()
                    << "\n");

  for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
    // Analyze copies (which don't overlap themselves).
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(MI, *TII, UseCopyInstr);
    if (CopyOperands) {

      Register RegSrc = CopyOperands->Source->getReg();
      Register RegDef = CopyOperands->Destination->getReg();

      if (!TRI->regsOverlap(RegDef, RegSrc)) {
        assert(RegDef.isPhysical() && RegSrc.isPhysical() &&
              "MachineCopyPropagation should be run after register allocation!");

        MCRegister Def = RegDef.asMCReg();
        MCRegister Src = RegSrc.asMCReg();

        // The two copies cancel out and the source of the first copy
        // hasn't been overridden, eliminate the second one. e.g.
        //  %ecx = COPY %eax
        //  ... nothing clobbered eax.
        //  %eax = COPY %ecx
        // =>
        //  %ecx = COPY %eax
        //
        // or
        //
        //  %ecx = COPY %eax
        //  ... nothing clobbered eax.
        //  %ecx = COPY %eax
        // =>
        //  %ecx = COPY %eax
        if (eraseIfRedundant(MI, Def, Src) || eraseIfRedundant(MI, Src, Def))
          continue;

        forwardUses(MI);

        // Src may have been changed by forwardUses()
        CopyOperands = isCopyInstr(MI, *TII, UseCopyInstr);
        Src = CopyOperands->Source->getReg().asMCReg();

        // If Src is defined by a previous copy, the previous copy cannot be
        // eliminated.
        ReadRegister(Src, MI, RegularUse);
        for (const MachineOperand &MO : MI.implicit_operands()) {
          if (!MO.isReg() || !MO.readsReg())
            continue;
          MCRegister Reg = MO.getReg().asMCReg();
          if (!Reg)
            continue;
          ReadRegister(Reg, MI, RegularUse);
        }

        LLVM_DEBUG(dbgs() << "MCP: Copy is a deletion candidate: "; MI.dump());

        // Copy is now a candidate for deletion.
        if (!MRI->isReserved(Def))
          MaybeDeadCopies.insert(&MI);

        // If 'Def' is previously source of another copy, then this earlier copy's
        // source is no longer available. e.g.
        // %xmm9 = copy %xmm2
        // ...
        // %xmm2 = copy %xmm0
        // ...
        // %xmm2 = copy %xmm9
        Tracker.clobberRegister(Def, *TRI, *TII, UseCopyInstr);
        for (const MachineOperand &MO : MI.implicit_operands()) {
          if (!MO.isReg() || !MO.isDef())
            continue;
          MCRegister Reg = MO.getReg().asMCReg();
          if (!Reg)
            continue;
          Tracker.clobberRegister(Reg, *TRI, *TII, UseCopyInstr);
        }

        Tracker.trackCopy(&MI, *TRI, *TII, UseCopyInstr);

        continue;
      }
    }

    // Clobber any earlyclobber regs first.
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isEarlyClobber()) {
        MCRegister Reg = MO.getReg().asMCReg();
        // If we have a tied earlyclobber, that means it is also read by this
        // instruction, so we need to make sure we don't remove it as dead
        // later.
        if (MO.isTied())
          ReadRegister(Reg, MI, RegularUse);
        Tracker.clobberRegister(Reg, *TRI, *TII, UseCopyInstr);
      }

    forwardUses(MI);

    // Not a copy.
    SmallVector<Register, 4> Defs;
    const MachineOperand *RegMask = nullptr;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isRegMask())
        RegMask = &MO;
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;

      assert(!Reg.isVirtual() &&
             "MachineCopyPropagation should be run after register allocation!");

      if (MO.isDef() && !MO.isEarlyClobber()) {
        Defs.push_back(Reg.asMCReg());
        continue;
      } else if (MO.readsReg())
        ReadRegister(Reg.asMCReg(), MI, MO.isDebug() ? DebugUse : RegularUse);
    }

    // The instruction has a register mask operand which means that it clobbers
    // a large set of registers.  Treat clobbered registers the same way as
    // defined registers.
    if (RegMask) {
      // Erase any MaybeDeadCopies whose destination register is clobbered.
      for (SmallSetVector<MachineInstr *, 8>::iterator DI =
               MaybeDeadCopies.begin();
           DI != MaybeDeadCopies.end();) {
        MachineInstr *MaybeDead = *DI;
        std::optional<DestSourcePair> CopyOperands =
            isCopyInstr(*MaybeDead, *TII, UseCopyInstr);
        MCRegister Reg = CopyOperands->Destination->getReg().asMCReg();
        assert(!MRI->isReserved(Reg));

        if (!RegMask->clobbersPhysReg(Reg)) {
          ++DI;
          continue;
        }

        LLVM_DEBUG(dbgs() << "MCP: Removing copy due to regmask clobbering: ";
                   MaybeDead->dump());

        // Make sure we invalidate any entries in the copy maps before erasing
        // the instruction.
        Tracker.clobberRegister(Reg, *TRI, *TII, UseCopyInstr);

        // erase() will return the next valid iterator pointing to the next
        // element after the erased one.
        DI = MaybeDeadCopies.erase(DI);
        MaybeDead->eraseFromParent();
        Changed = true;
        ++NumDeletes;
      }
    }

    // Any previous copy definition or reading the Defs is no longer available.
    for (MCRegister Reg : Defs)
      Tracker.clobberRegister(Reg, *TRI, *TII, UseCopyInstr);
  }

  bool TracksLiveness = MRI->tracksLiveness();

  // If liveness is tracked, we can use the live-in lists to know which
  // copies aren't dead.
  if (TracksLiveness)
    readSuccessorLiveIns(MBB);

  // If MBB doesn't have succesor, delete copies whose defs are not used.
  // If MBB does have successors, we can only delete copies if we are able to
  // use liveness information from successors to confirm they are really dead.
  if (MBB.succ_empty() || TracksLiveness) {
    for (MachineInstr *MaybeDead : MaybeDeadCopies) {
      LLVM_DEBUG(dbgs() << "MCP: Removing copy due to no live-out succ: ";
                 MaybeDead->dump());

      std::optional<DestSourcePair> CopyOperands =
          isCopyInstr(*MaybeDead, *TII, UseCopyInstr);
      assert(CopyOperands);

      Register SrcReg = CopyOperands->Source->getReg();
      Register DestReg = CopyOperands->Destination->getReg();
      assert(!MRI->isReserved(DestReg));

      // Update matching debug values, if any.
      SmallVector<MachineInstr *> MaybeDeadDbgUsers(
          CopyDbgUsers[MaybeDead].begin(), CopyDbgUsers[MaybeDead].end());
      MRI->updateDbgUsersToReg(DestReg.asMCReg(), SrcReg.asMCReg(),
                               MaybeDeadDbgUsers);

      MaybeDead->eraseFromParent();
      Changed = true;
      ++NumDeletes;
    }
  }

  MaybeDeadCopies.clear();
  CopyDbgUsers.clear();
  Tracker.clear();
}

static bool isBackwardPropagatableCopy(const DestSourcePair &CopyOperands,
                                       const MachineRegisterInfo &MRI) {
  Register Def = CopyOperands.Destination->getReg();
  Register Src = CopyOperands.Source->getReg();

  if (!Def || !Src)
    return false;

  if (MRI.isReserved(Def) || MRI.isReserved(Src))
    return false;

  return CopyOperands.Source->isRenamable() && CopyOperands.Source->isKill();
}

void MachineCopyPropagation::propagateDefs(MachineInstr &MI) {
  if (!Tracker.hasAnyCopies())
    return;

  for (unsigned OpIdx = 0, OpEnd = MI.getNumOperands(); OpIdx != OpEnd;
       ++OpIdx) {
    MachineOperand &MODef = MI.getOperand(OpIdx);

    if (!MODef.isReg() || MODef.isUse())
      continue;

    // Ignore non-trivial cases.
    if (MODef.isTied() || MODef.isUndef() || MODef.isImplicit())
      continue;

    if (!MODef.getReg())
      continue;

    // We only handle if the register comes from a vreg.
    if (!MODef.isRenamable())
      continue;

    MachineInstr *Copy = Tracker.findAvailBackwardCopy(
        MI, MODef.getReg().asMCReg(), *TRI, *TII, UseCopyInstr);
    if (!Copy)
      continue;

    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*Copy, *TII, UseCopyInstr);
    Register Def = CopyOperands->Destination->getReg();
    Register Src = CopyOperands->Source->getReg();

    if (MODef.getReg() != Src)
      continue;

    if (!isBackwardPropagatableRegClassCopy(*Copy, MI, OpIdx))
      continue;

    if (hasImplicitOverlap(MI, MODef))
      continue;

    if (hasOverlappingMultipleDef(MI, MODef, Def))
      continue;

    LLVM_DEBUG(dbgs() << "MCP: Replacing " << printReg(MODef.getReg(), TRI)
                      << "\n     with " << printReg(Def, TRI) << "\n     in "
                      << MI << "     from " << *Copy);

    MODef.setReg(Def);
    MODef.setIsRenamable(CopyOperands->Destination->isRenamable());

    LLVM_DEBUG(dbgs() << "MCP: After replacement: " << MI << "\n");
    MaybeDeadCopies.insert(Copy);
    Changed = true;
    ++NumCopyBackwardPropagated;
  }
}

void MachineCopyPropagation::BackwardCopyPropagateBlock(
    MachineBasicBlock &MBB) {
  LLVM_DEBUG(dbgs() << "MCP: BackwardCopyPropagateBlock " << MBB.getName()
                    << "\n");

  for (MachineInstr &MI : llvm::make_early_inc_range(llvm::reverse(MBB))) {
    // Ignore non-trivial COPYs.
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(MI, *TII, UseCopyInstr);
    if (CopyOperands && MI.getNumOperands() == 2) {
      Register DefReg = CopyOperands->Destination->getReg();
      Register SrcReg = CopyOperands->Source->getReg();

      if (!TRI->regsOverlap(DefReg, SrcReg)) {
        // Unlike forward cp, we don't invoke propagateDefs here,
        // just let forward cp do COPY-to-COPY propagation.
        if (isBackwardPropagatableCopy(*CopyOperands, *MRI)) {
          Tracker.invalidateRegister(SrcReg.asMCReg(), *TRI, *TII,
                                     UseCopyInstr);
          Tracker.invalidateRegister(DefReg.asMCReg(), *TRI, *TII,
                                     UseCopyInstr);
          Tracker.trackCopy(&MI, *TRI, *TII, UseCopyInstr);
          continue;
        }
      }
    }

    // Invalidate any earlyclobber regs first.
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isEarlyClobber()) {
        MCRegister Reg = MO.getReg().asMCReg();
        if (!Reg)
          continue;
        Tracker.invalidateRegister(Reg, *TRI, *TII, UseCopyInstr);
      }

    propagateDefs(MI);
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;

      if (!MO.getReg())
        continue;

      if (MO.isDef())
        Tracker.invalidateRegister(MO.getReg().asMCReg(), *TRI, *TII,
                                   UseCopyInstr);

      if (MO.readsReg()) {
        if (MO.isDebug()) {
          //  Check if the register in the debug instruction is utilized
          // in a copy instruction, so we can update the debug info if the
          // register is changed.
          for (MCRegUnit Unit : TRI->regunits(MO.getReg().asMCReg())) {
            if (auto *Copy = Tracker.findCopyDefViaUnit(Unit, *TRI)) {
              CopyDbgUsers[Copy].insert(&MI);
            }
          }
        } else {
          Tracker.invalidateRegister(MO.getReg().asMCReg(), *TRI, *TII,
                                     UseCopyInstr);
        }
      }
    }
  }

  for (auto *Copy : MaybeDeadCopies) {
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(*Copy, *TII, UseCopyInstr);
    Register Src = CopyOperands->Source->getReg();
    Register Def = CopyOperands->Destination->getReg();
    SmallVector<MachineInstr *> MaybeDeadDbgUsers(CopyDbgUsers[Copy].begin(),
                                                  CopyDbgUsers[Copy].end());

    MRI->updateDbgUsersToReg(Src.asMCReg(), Def.asMCReg(), MaybeDeadDbgUsers);
    Copy->eraseFromParent();
    ++NumDeletes;
  }

  MaybeDeadCopies.clear();
  CopyDbgUsers.clear();
  Tracker.clear();
}

static void LLVM_ATTRIBUTE_UNUSED printSpillReloadChain(
    DenseMap<MachineInstr *, SmallVector<MachineInstr *>> &SpillChain,
    DenseMap<MachineInstr *, SmallVector<MachineInstr *>> &ReloadChain,
    MachineInstr *Leader) {
  auto &SC = SpillChain[Leader];
  auto &RC = ReloadChain[Leader];
  for (auto I = SC.rbegin(), E = SC.rend(); I != E; ++I)
    (*I)->dump();
  for (MachineInstr *MI : RC)
    MI->dump();
}

// Remove spill-reload like copy chains. For example
// r0 = COPY r1
// r1 = COPY r2
// r2 = COPY r3
// r3 = COPY r4
// <def-use r4>
// r4 = COPY r3
// r3 = COPY r2
// r2 = COPY r1
// r1 = COPY r0
// will be folded into
// r0 = COPY r1
// r1 = COPY r4
// <def-use r4>
// r4 = COPY r1
// r1 = COPY r0
// TODO: Currently we don't track usage of r0 outside the chain, so we
// conservatively keep its value as it was before the rewrite.
//
// The algorithm is trying to keep
// property#1: No Def of spill COPY in the chain is used or defined until the
// paired reload COPY in the chain uses the Def.
//
// property#2: NO Source of COPY in the chain is used or defined until the next
// COPY in the chain defines the Source, except the innermost spill-reload
// pair.
//
// The algorithm is conducted by checking every COPY inside the MBB, assuming
// the COPY is a reload COPY, then try to find paired spill COPY by searching
// the COPY defines the Src of the reload COPY backward. If such pair is found,
// it either belongs to an existing chain or a new chain depends on
// last available COPY uses the Def of the reload COPY.
// Implementation notes, we use CopyTracker::findLastDefCopy(Reg, ...) to find
// out last COPY that defines Reg; we use CopyTracker::findLastUseCopy(Reg, ...)
// to find out last COPY that uses Reg. When we are encountered with a Non-COPY
// instruction, we check registers in the operands of this instruction. If this
// Reg is defined by a COPY, we untrack this Reg via
// CopyTracker::clobberRegister(Reg, ...).
void MachineCopyPropagation::EliminateSpillageCopies(MachineBasicBlock &MBB) {
  // ChainLeader maps MI inside a spill-reload chain to its innermost reload COPY.
  // Thus we can track if a MI belongs to an existing spill-reload chain.
  DenseMap<MachineInstr *, MachineInstr *> ChainLeader;
  // SpillChain maps innermost reload COPY of a spill-reload chain to a sequence
  // of COPYs that forms spills of a spill-reload chain.
  // ReloadChain maps innermost reload COPY of a spill-reload chain to a
  // sequence of COPYs that forms reloads of a spill-reload chain.
  DenseMap<MachineInstr *, SmallVector<MachineInstr *>> SpillChain, ReloadChain;
  // If a COPY's Source has use or def until next COPY defines the Source,
  // we put the COPY in this set to keep property#2.
  DenseSet<const MachineInstr *> CopySourceInvalid;

  auto TryFoldSpillageCopies =
      [&, this](const SmallVectorImpl<MachineInstr *> &SC,
                const SmallVectorImpl<MachineInstr *> &RC) {
        assert(SC.size() == RC.size() && "Spill-reload should be paired");

        // We need at least 3 pairs of copies for the transformation to apply,
        // because the first outermost pair cannot be removed since we don't
        // recolor outside of the chain and that we need at least one temporary
        // spill slot to shorten the chain. If we only have a chain of two
        // pairs, we already have the shortest sequence this code can handle:
        // the outermost pair for the temporary spill slot, and the pair that
        // use that temporary spill slot for the other end of the chain.
        // TODO: We might be able to simplify to one spill-reload pair if collecting
        // more infomation about the outermost COPY.
        if (SC.size() <= 2)
          return;

        // If violate property#2, we don't fold the chain.
        for (const MachineInstr *Spill : drop_begin(SC))
          if (CopySourceInvalid.count(Spill))
            return;

        for (const MachineInstr *Reload : drop_end(RC))
          if (CopySourceInvalid.count(Reload))
            return;

        auto CheckCopyConstraint = [this](Register Def, Register Src) {
          for (const TargetRegisterClass *RC : TRI->regclasses()) {
            if (RC->contains(Def) && RC->contains(Src))
              return true;
          }
          return false;
        };

        auto UpdateReg = [](MachineInstr *MI, const MachineOperand *Old,
                            const MachineOperand *New) {
          for (MachineOperand &MO : MI->operands()) {
            if (&MO == Old)
              MO.setReg(New->getReg());
          }
        };

        std::optional<DestSourcePair> InnerMostSpillCopy =
            isCopyInstr(*SC[0], *TII, UseCopyInstr);
        std::optional<DestSourcePair> OuterMostSpillCopy =
            isCopyInstr(*SC.back(), *TII, UseCopyInstr);
        std::optional<DestSourcePair> InnerMostReloadCopy =
            isCopyInstr(*RC[0], *TII, UseCopyInstr);
        std::optional<DestSourcePair> OuterMostReloadCopy =
            isCopyInstr(*RC.back(), *TII, UseCopyInstr);
        if (!CheckCopyConstraint(OuterMostSpillCopy->Source->getReg(),
                                 InnerMostSpillCopy->Source->getReg()) ||
            !CheckCopyConstraint(InnerMostReloadCopy->Destination->getReg(),
                                 OuterMostReloadCopy->Destination->getReg()))
          return;

        SpillageChainsLength += SC.size() + RC.size();
        NumSpillageChains += 1;
        UpdateReg(SC[0], InnerMostSpillCopy->Destination,
                  OuterMostSpillCopy->Source);
        UpdateReg(RC[0], InnerMostReloadCopy->Source,
                  OuterMostReloadCopy->Destination);

        for (size_t I = 1; I < SC.size() - 1; ++I) {
          SC[I]->eraseFromParent();
          RC[I]->eraseFromParent();
          NumDeletes += 2;
        }
      };

  auto IsFoldableCopy = [this](const MachineInstr &MaybeCopy) {
    if (MaybeCopy.getNumImplicitOperands() > 0)
      return false;
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(MaybeCopy, *TII, UseCopyInstr);
    if (!CopyOperands)
      return false;
    Register Src = CopyOperands->Source->getReg();
    Register Def = CopyOperands->Destination->getReg();
    return Src && Def && !TRI->regsOverlap(Src, Def) &&
           CopyOperands->Source->isRenamable() &&
           CopyOperands->Destination->isRenamable();
  };

  auto IsSpillReloadPair = [&, this](const MachineInstr &Spill,
                                     const MachineInstr &Reload) {
    if (!IsFoldableCopy(Spill) || !IsFoldableCopy(Reload))
      return false;
    std::optional<DestSourcePair> SpillCopy =
        isCopyInstr(Spill, *TII, UseCopyInstr);
    std::optional<DestSourcePair> ReloadCopy =
        isCopyInstr(Reload, *TII, UseCopyInstr);
    if (!SpillCopy || !ReloadCopy)
      return false;
    return SpillCopy->Source->getReg() == ReloadCopy->Destination->getReg() &&
           SpillCopy->Destination->getReg() == ReloadCopy->Source->getReg();
  };

  auto IsChainedCopy = [&, this](const MachineInstr &Prev,
                                 const MachineInstr &Current) {
    if (!IsFoldableCopy(Prev) || !IsFoldableCopy(Current))
      return false;
    std::optional<DestSourcePair> PrevCopy =
        isCopyInstr(Prev, *TII, UseCopyInstr);
    std::optional<DestSourcePair> CurrentCopy =
        isCopyInstr(Current, *TII, UseCopyInstr);
    if (!PrevCopy || !CurrentCopy)
      return false;
    return PrevCopy->Source->getReg() == CurrentCopy->Destination->getReg();
  };

  for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
    std::optional<DestSourcePair> CopyOperands =
        isCopyInstr(MI, *TII, UseCopyInstr);

    // Update track information via non-copy instruction.
    SmallSet<Register, 8> RegsToClobber;
    if (!CopyOperands) {
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (!Reg)
          continue;
        MachineInstr *LastUseCopy =
            Tracker.findLastSeenUseInCopy(Reg.asMCReg(), *TRI);
        if (LastUseCopy) {
          LLVM_DEBUG(dbgs() << "MCP: Copy source of\n");
          LLVM_DEBUG(LastUseCopy->dump());
          LLVM_DEBUG(dbgs() << "might be invalidated by\n");
          LLVM_DEBUG(MI.dump());
          CopySourceInvalid.insert(LastUseCopy);
        }
        // Must be noted Tracker.clobberRegister(Reg, ...) removes tracking of
        // Reg, i.e, COPY that defines Reg is removed from the mapping as well
        // as marking COPYs that uses Reg unavailable.
        // We don't invoke CopyTracker::clobberRegister(Reg, ...) if Reg is not
        // defined by a previous COPY, since we don't want to make COPYs uses
        // Reg unavailable.
        if (Tracker.findLastSeenDefInCopy(MI, Reg.asMCReg(), *TRI, *TII,
                                    UseCopyInstr))
          // Thus we can keep the property#1.
          RegsToClobber.insert(Reg);
      }
      for (Register Reg : RegsToClobber) {
        Tracker.clobberRegister(Reg, *TRI, *TII, UseCopyInstr);
        LLVM_DEBUG(dbgs() << "MCP: Removed tracking of " << printReg(Reg, TRI)
                          << "\n");
      }
      continue;
    }

    Register Src = CopyOperands->Source->getReg();
    Register Def = CopyOperands->Destination->getReg();
    // Check if we can find a pair spill-reload copy.
    LLVM_DEBUG(dbgs() << "MCP: Searching paired spill for reload: ");
    LLVM_DEBUG(MI.dump());
    MachineInstr *MaybeSpill =
        Tracker.findLastSeenDefInCopy(MI, Src.asMCReg(), *TRI, *TII, UseCopyInstr);
    bool MaybeSpillIsChained = ChainLeader.count(MaybeSpill);
    if (!MaybeSpillIsChained && MaybeSpill &&
        IsSpillReloadPair(*MaybeSpill, MI)) {
      // Check if we already have an existing chain. Now we have a
      // spill-reload pair.
      // L2: r2 = COPY r3
      // L5: r3 = COPY r2
      // Looking for a valid COPY before L5 which uses r3.
      // This can be serverial cases.
      // Case #1:
      // No COPY is found, which can be r3 is def-use between (L2, L5), we
      // create a new chain for L2 and L5.
      // Case #2:
      // L2: r2 = COPY r3
      // L5: r3 = COPY r2
      // Such COPY is found and is L2, we create a new chain for L2 and L5.
      // Case #3:
      // L2: r2 = COPY r3
      // L3: r1 = COPY r3
      // L5: r3 = COPY r2
      // we create a new chain for L2 and L5.
      // Case #4:
      // L2: r2 = COPY r3
      // L3: r1 = COPY r3
      // L4: r3 = COPY r1
      // L5: r3 = COPY r2
      // Such COPY won't be found since L4 defines r3. we create a new chain
      // for L2 and L5.
      // Case #5:
      // L2: r2 = COPY r3
      // L3: r3 = COPY r1
      // L4: r1 = COPY r3
      // L5: r3 = COPY r2
      // COPY is found and is L4 which belongs to an existing chain, we add
      // L2 and L5 to this chain.
      LLVM_DEBUG(dbgs() << "MCP: Found spill: ");
      LLVM_DEBUG(MaybeSpill->dump());
      MachineInstr *MaybePrevReload =
          Tracker.findLastSeenUseInCopy(Def.asMCReg(), *TRI);
      auto Leader = ChainLeader.find(MaybePrevReload);
      MachineInstr *L = nullptr;
      if (Leader == ChainLeader.end() ||
          (MaybePrevReload && !IsChainedCopy(*MaybePrevReload, MI))) {
        L = &MI;
        assert(!SpillChain.count(L) &&
               "SpillChain should not have contained newly found chain");
      } else {
        assert(MaybePrevReload &&
               "Found a valid leader through nullptr should not happend");
        L = Leader->second;
        assert(SpillChain[L].size() > 0 &&
               "Existing chain's length should be larger than zero");
      }
      assert(!ChainLeader.count(&MI) && !ChainLeader.count(MaybeSpill) &&
             "Newly found paired spill-reload should not belong to any chain "
             "at this point");
      ChainLeader.insert({MaybeSpill, L});
      ChainLeader.insert({&MI, L});
      SpillChain[L].push_back(MaybeSpill);
      ReloadChain[L].push_back(&MI);
      LLVM_DEBUG(dbgs() << "MCP: Chain " << L << " now is:\n");
      LLVM_DEBUG(printSpillReloadChain(SpillChain, ReloadChain, L));
    } else if (MaybeSpill && !MaybeSpillIsChained) {
      // MaybeSpill is unable to pair with MI. That's to say adding MI makes
      // the chain invalid.
      // The COPY defines Src is no longer considered as a candidate of a
      // valid chain. Since we expect the Def of a spill copy isn't used by
      // any COPY instruction until a reload copy. For example:
      // L1: r1 = COPY r2
      // L2: r3 = COPY r1
      // If we later have
      // L1: r1 = COPY r2
      // L2: r3 = COPY r1
      // L3: r2 = COPY r1
      // L1 and L3 can't be a valid spill-reload pair.
      // Thus we keep the property#1.
      LLVM_DEBUG(dbgs() << "MCP: Not paired spill-reload:\n");
      LLVM_DEBUG(MaybeSpill->dump());
      LLVM_DEBUG(MI.dump());
      Tracker.clobberRegister(Src.asMCReg(), *TRI, *TII, UseCopyInstr);
      LLVM_DEBUG(dbgs() << "MCP: Removed tracking of " << printReg(Src, TRI)
                        << "\n");
    }
    Tracker.trackCopy(&MI, *TRI, *TII, UseCopyInstr);
  }

  for (auto I = SpillChain.begin(), E = SpillChain.end(); I != E; ++I) {
    auto &SC = I->second;
    assert(ReloadChain.count(I->first) &&
           "Reload chain of the same leader should exist");
    auto &RC = ReloadChain[I->first];
    TryFoldSpillageCopies(SC, RC);
  }

  MaybeDeadCopies.clear();
  CopyDbgUsers.clear();
  Tracker.clear();
}

bool MachineCopyPropagation::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool isSpillageCopyElimEnabled = false;
  switch (EnableSpillageCopyElimination) {
  case cl::BOU_UNSET:
    isSpillageCopyElimEnabled =
        MF.getSubtarget().enableSpillageCopyElimination();
    break;
  case cl::BOU_TRUE:
    isSpillageCopyElimEnabled = true;
    break;
  case cl::BOU_FALSE:
    isSpillageCopyElimEnabled = false;
    break;
  }

  Changed = false;

  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();

  for (MachineBasicBlock &MBB : MF) {
    if (isSpillageCopyElimEnabled)
      EliminateSpillageCopies(MBB);
    BackwardCopyPropagateBlock(MBB);
    ForwardCopyPropagateBlock(MBB);
  }

  return Changed;
}

MachineFunctionPass *
llvm::createMachineCopyPropagationPass(bool UseCopyInstr = false) {
  return new MachineCopyPropagation(UseCopyInstr);
}
