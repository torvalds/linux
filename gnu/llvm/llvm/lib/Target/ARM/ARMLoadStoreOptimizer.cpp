//===- ARMLoadStoreOptimizer.cpp - ARM load / store opt. pass -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains a pass that performs load / store related peephole
/// optimizations. This pass should be run after register allocation.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMISelLowering.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "arm-ldst-opt"

STATISTIC(NumLDMGened , "Number of ldm instructions generated");
STATISTIC(NumSTMGened , "Number of stm instructions generated");
STATISTIC(NumVLDMGened, "Number of vldm instructions generated");
STATISTIC(NumVSTMGened, "Number of vstm instructions generated");
STATISTIC(NumLdStMoved, "Number of load / store instructions moved");
STATISTIC(NumLDRDFormed,"Number of ldrd created before allocation");
STATISTIC(NumSTRDFormed,"Number of strd created before allocation");
STATISTIC(NumLDRD2LDM,  "Number of ldrd instructions turned back into ldm");
STATISTIC(NumSTRD2STM,  "Number of strd instructions turned back into stm");
STATISTIC(NumLDRD2LDR,  "Number of ldrd instructions turned back into ldr's");
STATISTIC(NumSTRD2STR,  "Number of strd instructions turned back into str's");

/// This switch disables formation of double/multi instructions that could
/// potentially lead to (new) alignment traps even with CCR.UNALIGN_TRP
/// disabled. This can be used to create libraries that are robust even when
/// users provoke undefined behaviour by supplying misaligned pointers.
/// \see mayCombineMisaligned()
static cl::opt<bool>
AssumeMisalignedLoadStores("arm-assume-misaligned-load-store", cl::Hidden,
    cl::init(false), cl::desc("Be more conservative in ARM load/store opt"));

#define ARM_LOAD_STORE_OPT_NAME "ARM load / store optimization pass"

namespace {

  /// Post- register allocation pass the combine load / store instructions to
  /// form ldm / stm instructions.
  struct ARMLoadStoreOpt : public MachineFunctionPass {
    static char ID;

    const MachineFunction *MF;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    const TargetLowering *TL;
    ARMFunctionInfo *AFI;
    LiveRegUnits LiveRegs;
    RegisterClassInfo RegClassInfo;
    MachineBasicBlock::const_iterator LiveRegPos;
    bool LiveRegsValid;
    bool RegClassInfoValid;
    bool isThumb1, isThumb2;

    ARMLoadStoreOpt() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override { return ARM_LOAD_STORE_OPT_NAME; }

  private:
    /// A set of load/store MachineInstrs with same base register sorted by
    /// offset.
    struct MemOpQueueEntry {
      MachineInstr *MI;
      int Offset;        ///< Load/Store offset.
      unsigned Position; ///< Position as counted from end of basic block.

      MemOpQueueEntry(MachineInstr &MI, int Offset, unsigned Position)
          : MI(&MI), Offset(Offset), Position(Position) {}
    };
    using MemOpQueue = SmallVector<MemOpQueueEntry, 8>;

    /// A set of MachineInstrs that fulfill (nearly all) conditions to get
    /// merged into a LDM/STM.
    struct MergeCandidate {
      /// List of instructions ordered by load/store offset.
      SmallVector<MachineInstr*, 4> Instrs;

      /// Index in Instrs of the instruction being latest in the schedule.
      unsigned LatestMIIdx;

      /// Index in Instrs of the instruction being earliest in the schedule.
      unsigned EarliestMIIdx;

      /// Index into the basic block where the merged instruction will be
      /// inserted. (See MemOpQueueEntry.Position)
      unsigned InsertPos;

      /// Whether the instructions can be merged into a ldm/stm instruction.
      bool CanMergeToLSMulti;

      /// Whether the instructions can be merged into a ldrd/strd instruction.
      bool CanMergeToLSDouble;
    };
    SpecificBumpPtrAllocator<MergeCandidate> Allocator;
    SmallVector<const MergeCandidate*,4> Candidates;
    SmallVector<MachineInstr*,4> MergeBaseCandidates;

    void moveLiveRegsBefore(const MachineBasicBlock &MBB,
                            MachineBasicBlock::const_iterator Before);
    unsigned findFreeReg(const TargetRegisterClass &RegClass);
    void UpdateBaseRegUses(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                           unsigned Base, unsigned WordOffset,
                           ARMCC::CondCodes Pred, unsigned PredReg);
    MachineInstr *CreateLoadStoreMulti(
        MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
        int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
        ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
        ArrayRef<std::pair<unsigned, bool>> Regs,
        ArrayRef<MachineInstr*> Instrs);
    MachineInstr *CreateLoadStoreDouble(
        MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
        int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
        ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
        ArrayRef<std::pair<unsigned, bool>> Regs,
        ArrayRef<MachineInstr*> Instrs) const;
    void FormCandidates(const MemOpQueue &MemOps);
    MachineInstr *MergeOpsUpdate(const MergeCandidate &Cand);
    bool FixInvalidRegPairOp(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI);
    bool MergeBaseUpdateLoadStore(MachineInstr *MI);
    bool MergeBaseUpdateLSMultiple(MachineInstr *MI);
    bool MergeBaseUpdateLSDouble(MachineInstr &MI) const;
    bool LoadStoreMultipleOpti(MachineBasicBlock &MBB);
    bool MergeReturnIntoLDM(MachineBasicBlock &MBB);
    bool CombineMovBx(MachineBasicBlock &MBB);
  };

} // end anonymous namespace

char ARMLoadStoreOpt::ID = 0;

INITIALIZE_PASS(ARMLoadStoreOpt, "arm-ldst-opt", ARM_LOAD_STORE_OPT_NAME, false,
                false)

static bool definesCPSR(const MachineInstr &MI) {
  for (const auto &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (MO.isDef() && MO.getReg() == ARM::CPSR && !MO.isDead())
      // If the instruction has live CPSR def, then it's not safe to fold it
      // into load / store.
      return true;
  }

  return false;
}

static int getMemoryOpOffset(const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();
  bool isAM3 = Opcode == ARM::LDRD || Opcode == ARM::STRD;
  unsigned NumOperands = MI.getDesc().getNumOperands();
  unsigned OffField = MI.getOperand(NumOperands - 3).getImm();

  if (Opcode == ARM::t2LDRi12 || Opcode == ARM::t2LDRi8 ||
      Opcode == ARM::t2STRi12 || Opcode == ARM::t2STRi8 ||
      Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8 ||
      Opcode == ARM::LDRi12   || Opcode == ARM::STRi12)
    return OffField;

  // Thumb1 immediate offsets are scaled by 4
  if (Opcode == ARM::tLDRi || Opcode == ARM::tSTRi ||
      Opcode == ARM::tLDRspi || Opcode == ARM::tSTRspi)
    return OffField * 4;

  int Offset = isAM3 ? ARM_AM::getAM3Offset(OffField)
    : ARM_AM::getAM5Offset(OffField) * 4;
  ARM_AM::AddrOpc Op = isAM3 ? ARM_AM::getAM3Op(OffField)
    : ARM_AM::getAM5Op(OffField);

  if (Op == ARM_AM::sub)
    return -Offset;

  return Offset;
}

static const MachineOperand &getLoadStoreBaseOp(const MachineInstr &MI) {
  return MI.getOperand(1);
}

static const MachineOperand &getLoadStoreRegOp(const MachineInstr &MI) {
  return MI.getOperand(0);
}

static int getLoadStoreMultipleOpcode(unsigned Opcode, ARM_AM::AMSubMode Mode) {
  switch (Opcode) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDRi12:
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::LDMIA;
    case ARM_AM::da: return ARM::LDMDA;
    case ARM_AM::db: return ARM::LDMDB;
    case ARM_AM::ib: return ARM::LDMIB;
    }
  case ARM::STRi12:
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::STMIA;
    case ARM_AM::da: return ARM::STMDA;
    case ARM_AM::db: return ARM::STMDB;
    case ARM_AM::ib: return ARM::STMIB;
    }
  case ARM::tLDRi:
  case ARM::tLDRspi:
    // tLDMIA is writeback-only - unless the base register is in the input
    // reglist.
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::tLDMIA;
    }
  case ARM::tSTRi:
  case ARM::tSTRspi:
    // There is no non-writeback tSTMIA either.
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::tSTMIA_UPD;
    }
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2LDMIA;
    case ARM_AM::db: return ARM::t2LDMDB;
    }
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2STMIA;
    case ARM_AM::db: return ARM::t2STMDB;
    }
  case ARM::VLDRS:
    ++NumVLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMSIA;
    case ARM_AM::db: return 0; // Only VLDMSDB_UPD exists.
    }
  case ARM::VSTRS:
    ++NumVSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMSIA;
    case ARM_AM::db: return 0; // Only VSTMSDB_UPD exists.
    }
  case ARM::VLDRD:
    ++NumVLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMDIA;
    case ARM_AM::db: return 0; // Only VLDMDDB_UPD exists.
    }
  case ARM::VSTRD:
    ++NumVSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMDIA;
    case ARM_AM::db: return 0; // Only VSTMDDB_UPD exists.
    }
  }
}

static ARM_AM::AMSubMode getLoadStoreMultipleSubMode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMIA_UPD:
  case ARM::STMIA:
  case ARM::STMIA_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMIA_UPD:
  case ARM::t2STMIA:
  case ARM::t2STMIA_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
    return ARM_AM::ia;

  case ARM::LDMDA:
  case ARM::LDMDA_UPD:
  case ARM::STMDA:
  case ARM::STMDA_UPD:
    return ARM_AM::da;

  case ARM::LDMDB:
  case ARM::LDMDB_UPD:
  case ARM::STMDB:
  case ARM::STMDB_UPD:
  case ARM::t2LDMDB:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMDB:
  case ARM::t2STMDB_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMSDB_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VSTMDDB_UPD:
    return ARM_AM::db;

  case ARM::LDMIB:
  case ARM::LDMIB_UPD:
  case ARM::STMIB:
  case ARM::STMIB_UPD:
    return ARM_AM::ib;
  }
}

static bool isT1i32Load(unsigned Opc) {
  return Opc == ARM::tLDRi || Opc == ARM::tLDRspi;
}

static bool isT2i32Load(unsigned Opc) {
  return Opc == ARM::t2LDRi12 || Opc == ARM::t2LDRi8;
}

static bool isi32Load(unsigned Opc) {
  return Opc == ARM::LDRi12 || isT1i32Load(Opc) || isT2i32Load(Opc) ;
}

static bool isT1i32Store(unsigned Opc) {
  return Opc == ARM::tSTRi || Opc == ARM::tSTRspi;
}

static bool isT2i32Store(unsigned Opc) {
  return Opc == ARM::t2STRi12 || Opc == ARM::t2STRi8;
}

static bool isi32Store(unsigned Opc) {
  return Opc == ARM::STRi12 || isT1i32Store(Opc) || isT2i32Store(Opc);
}

static bool isLoadSingle(unsigned Opc) {
  return isi32Load(Opc) || Opc == ARM::VLDRS || Opc == ARM::VLDRD;
}

static unsigned getImmScale(unsigned Opc) {
  switch (Opc) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
    return 1;
  case ARM::tLDRHi:
  case ARM::tSTRHi:
    return 2;
  case ARM::tLDRBi:
  case ARM::tSTRBi:
    return 4;
  }
}

static unsigned getLSMultipleTransferSize(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default: return 0;
  case ARM::LDRi12:
  case ARM::STRi12:
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
  case ARM::t2STRi8:
  case ARM::t2STRi12:
  case ARM::VLDRS:
  case ARM::VSTRS:
    return 4;
  case ARM::VLDRD:
  case ARM::VSTRD:
    return 8;
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::VLDMSIA:
  case ARM::VSTMSIA:
    return (MI->getNumOperands() - MI->getDesc().getNumOperands() + 1) * 4;
  case ARM::VLDMDIA:
  case ARM::VSTMDIA:
    return (MI->getNumOperands() - MI->getDesc().getNumOperands() + 1) * 8;
  }
}

/// Update future uses of the base register with the offset introduced
/// due to writeback. This function only works on Thumb1.
void ARMLoadStoreOpt::UpdateBaseRegUses(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, unsigned Base,
                                        unsigned WordOffset,
                                        ARMCC::CondCodes Pred,
                                        unsigned PredReg) {
  assert(isThumb1 && "Can only update base register uses for Thumb1!");
  // Start updating any instructions with immediate offsets. Insert a SUB before
  // the first non-updateable instruction (if any).
  for (; MBBI != MBB.end(); ++MBBI) {
    bool InsertSub = false;
    unsigned Opc = MBBI->getOpcode();

    if (MBBI->readsRegister(Base, /*TRI=*/nullptr)) {
      int Offset;
      bool IsLoad =
        Opc == ARM::tLDRi || Opc == ARM::tLDRHi || Opc == ARM::tLDRBi;
      bool IsStore =
        Opc == ARM::tSTRi || Opc == ARM::tSTRHi || Opc == ARM::tSTRBi;

      if (IsLoad || IsStore) {
        // Loads and stores with immediate offsets can be updated, but only if
        // the new offset isn't negative.
        // The MachineOperand containing the offset immediate is the last one
        // before predicates.
        MachineOperand &MO =
          MBBI->getOperand(MBBI->getDesc().getNumOperands() - 3);
        // The offsets are scaled by 1, 2 or 4 depending on the Opcode.
        Offset = MO.getImm() - WordOffset * getImmScale(Opc);

        // If storing the base register, it needs to be reset first.
        Register InstrSrcReg = getLoadStoreRegOp(*MBBI).getReg();

        if (Offset >= 0 && !(IsStore && InstrSrcReg == Base))
          MO.setImm(Offset);
        else
          InsertSub = true;
      } else if ((Opc == ARM::tSUBi8 || Opc == ARM::tADDi8) &&
                 !definesCPSR(*MBBI)) {
        // SUBS/ADDS using this register, with a dead def of the CPSR.
        // Merge it with the update; if the merged offset is too large,
        // insert a new sub instead.
        MachineOperand &MO =
          MBBI->getOperand(MBBI->getDesc().getNumOperands() - 3);
        Offset = (Opc == ARM::tSUBi8) ?
          MO.getImm() + WordOffset * 4 :
          MO.getImm() - WordOffset * 4 ;
        if (Offset >= 0 && TL->isLegalAddImmediate(Offset)) {
          // FIXME: Swap ADDS<->SUBS if Offset < 0, erase instruction if
          // Offset == 0.
          MO.setImm(Offset);
          // The base register has now been reset, so exit early.
          return;
        } else {
          InsertSub = true;
        }
      } else {
        // Can't update the instruction.
        InsertSub = true;
      }
    } else if (definesCPSR(*MBBI) || MBBI->isCall() || MBBI->isBranch()) {
      // Since SUBS sets the condition flags, we can't place the base reset
      // after an instruction that has a live CPSR def.
      // The base register might also contain an argument for a function call.
      InsertSub = true;
    }

    if (InsertSub) {
      // An instruction above couldn't be updated, so insert a sub.
      BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBi8), Base)
          .add(t1CondCodeOp(true))
          .addReg(Base)
          .addImm(WordOffset * 4)
          .addImm(Pred)
          .addReg(PredReg);
      return;
    }

    if (MBBI->killsRegister(Base, /*TRI=*/nullptr) ||
        MBBI->definesRegister(Base, /*TRI=*/nullptr))
      // Register got killed. Stop updating.
      return;
  }

  // End of block was reached.
  if (!MBB.succ_empty()) {
    // FIXME: Because of a bug, live registers are sometimes missing from
    // the successor blocks' live-in sets. This means we can't trust that
    // information and *always* have to reset at the end of a block.
    // See PR21029.
    if (MBBI != MBB.end()) --MBBI;
    BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBi8), Base)
        .add(t1CondCodeOp(true))
        .addReg(Base)
        .addImm(WordOffset * 4)
        .addImm(Pred)
        .addReg(PredReg);
  }
}

/// Return the first register of class \p RegClass that is not in \p Regs.
unsigned ARMLoadStoreOpt::findFreeReg(const TargetRegisterClass &RegClass) {
  if (!RegClassInfoValid) {
    RegClassInfo.runOnMachineFunction(*MF);
    RegClassInfoValid = true;
  }

  for (unsigned Reg : RegClassInfo.getOrder(&RegClass))
    if (LiveRegs.available(Reg) && !MF->getRegInfo().isReserved(Reg))
      return Reg;
  return 0;
}

/// Compute live registers just before instruction \p Before (in normal schedule
/// direction). Computes backwards so multiple queries in the same block must
/// come in reverse order.
void ARMLoadStoreOpt::moveLiveRegsBefore(const MachineBasicBlock &MBB,
    MachineBasicBlock::const_iterator Before) {
  // Initialize if we never queried in this block.
  if (!LiveRegsValid) {
    LiveRegs.init(*TRI);
    LiveRegs.addLiveOuts(MBB);
    LiveRegPos = MBB.end();
    LiveRegsValid = true;
  }
  // Move backward just before the "Before" position.
  while (LiveRegPos != Before) {
    --LiveRegPos;
    LiveRegs.stepBackward(*LiveRegPos);
  }
}

static bool ContainsReg(const ArrayRef<std::pair<unsigned, bool>> &Regs,
                        unsigned Reg) {
  for (const std::pair<unsigned, bool> &R : Regs)
    if (R.first == Reg)
      return true;
  return false;
}

/// Create and insert a LDM or STM with Base as base register and registers in
/// Regs as the register operands that would be loaded / stored.  It returns
/// true if the transformation is done.
MachineInstr *ARMLoadStoreOpt::CreateLoadStoreMulti(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
    ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
    ArrayRef<std::pair<unsigned, bool>> Regs,
    ArrayRef<MachineInstr*> Instrs) {
  unsigned NumRegs = Regs.size();
  assert(NumRegs > 1);

  // For Thumb1 targets, it might be necessary to clobber the CPSR to merge.
  // Compute liveness information for that register to make the decision.
  bool SafeToClobberCPSR = !isThumb1 ||
    (MBB.computeRegisterLiveness(TRI, ARM::CPSR, InsertBefore, 20) ==
     MachineBasicBlock::LQR_Dead);

  bool Writeback = isThumb1; // Thumb1 LDM/STM have base reg writeback.

  // Exception: If the base register is in the input reglist, Thumb1 LDM is
  // non-writeback.
  // It's also not possible to merge an STR of the base register in Thumb1.
  if (isThumb1 && ContainsReg(Regs, Base)) {
    assert(Base != ARM::SP && "Thumb1 does not allow SP in register list");
    if (Opcode == ARM::tLDRi)
      Writeback = false;
    else if (Opcode == ARM::tSTRi)
      return nullptr;
  }

  ARM_AM::AMSubMode Mode = ARM_AM::ia;
  // VFP and Thumb2 do not support IB or DA modes. Thumb1 only supports IA.
  bool isNotVFP = isi32Load(Opcode) || isi32Store(Opcode);
  bool haveIBAndDA = isNotVFP && !isThumb2 && !isThumb1;

  if (Offset == 4 && haveIBAndDA) {
    Mode = ARM_AM::ib;
  } else if (Offset == -4 * (int)NumRegs + 4 && haveIBAndDA) {
    Mode = ARM_AM::da;
  } else if (Offset == -4 * (int)NumRegs && isNotVFP && !isThumb1) {
    // VLDM/VSTM do not support DB mode without also updating the base reg.
    Mode = ARM_AM::db;
  } else if (Offset != 0 || Opcode == ARM::tLDRspi || Opcode == ARM::tSTRspi) {
    // Check if this is a supported opcode before inserting instructions to
    // calculate a new base register.
    if (!getLoadStoreMultipleOpcode(Opcode, Mode)) return nullptr;

    // If starting offset isn't zero, insert a MI to materialize a new base.
    // But only do so if it is cost effective, i.e. merging more than two
    // loads / stores.
    if (NumRegs <= 2)
      return nullptr;

    // On Thumb1, it's not worth materializing a new base register without
    // clobbering the CPSR (i.e. not using ADDS/SUBS).
    if (!SafeToClobberCPSR)
      return nullptr;

    unsigned NewBase;
    if (isi32Load(Opcode)) {
      // If it is a load, then just use one of the destination registers
      // as the new base. Will no longer be writeback in Thumb1.
      NewBase = Regs[NumRegs-1].first;
      Writeback = false;
    } else {
      // Find a free register that we can use as scratch register.
      moveLiveRegsBefore(MBB, InsertBefore);
      // The merged instruction does not exist yet but will use several Regs if
      // it is a Store.
      if (!isLoadSingle(Opcode))
        for (const std::pair<unsigned, bool> &R : Regs)
          LiveRegs.addReg(R.first);

      NewBase = findFreeReg(isThumb1 ? ARM::tGPRRegClass : ARM::GPRRegClass);
      if (NewBase == 0)
        return nullptr;
    }

    int BaseOpc = isThumb2 ? (BaseKill && Base == ARM::SP ? ARM::t2ADDspImm
                                                          : ARM::t2ADDri)
                           : (isThumb1 && Base == ARM::SP)
                                 ? ARM::tADDrSPi
                                 : (isThumb1 && Offset < 8)
                                       ? ARM::tADDi3
                                       : isThumb1 ? ARM::tADDi8 : ARM::ADDri;

    if (Offset < 0) {
      // FIXME: There are no Thumb1 load/store instructions with negative
      // offsets. So the Base != ARM::SP might be unnecessary.
      Offset = -Offset;
      BaseOpc = isThumb2 ? (BaseKill && Base == ARM::SP ? ARM::t2SUBspImm
                                                        : ARM::t2SUBri)
                         : (isThumb1 && Offset < 8 && Base != ARM::SP)
                               ? ARM::tSUBi3
                               : isThumb1 ? ARM::tSUBi8 : ARM::SUBri;
    }

    if (!TL->isLegalAddImmediate(Offset))
      // FIXME: Try add with register operand?
      return nullptr; // Probably not worth it then.

    // We can only append a kill flag to the add/sub input if the value is not
    // used in the register list of the stm as well.
    bool KillOldBase = BaseKill &&
      (!isi32Store(Opcode) || !ContainsReg(Regs, Base));

    if (isThumb1) {
      // Thumb1: depending on immediate size, use either
      //   ADDS NewBase, Base, #imm3
      // or
      //   MOV  NewBase, Base
      //   ADDS NewBase, #imm8.
      if (Base != NewBase &&
          (BaseOpc == ARM::tADDi8 || BaseOpc == ARM::tSUBi8)) {
        // Need to insert a MOV to the new base first.
        if (isARMLowRegister(NewBase) && isARMLowRegister(Base) &&
            !STI->hasV6Ops()) {
          // thumbv4t doesn't have lo->lo copies, and we can't predicate tMOVSr
          if (Pred != ARMCC::AL)
            return nullptr;
          BuildMI(MBB, InsertBefore, DL, TII->get(ARM::tMOVSr), NewBase)
            .addReg(Base, getKillRegState(KillOldBase));
        } else
          BuildMI(MBB, InsertBefore, DL, TII->get(ARM::tMOVr), NewBase)
              .addReg(Base, getKillRegState(KillOldBase))
              .add(predOps(Pred, PredReg));

        // The following ADDS/SUBS becomes an update.
        Base = NewBase;
        KillOldBase = true;
      }
      if (BaseOpc == ARM::tADDrSPi) {
        assert(Offset % 4 == 0 && "tADDrSPi offset is scaled by 4");
        BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
            .addReg(Base, getKillRegState(KillOldBase))
            .addImm(Offset / 4)
            .add(predOps(Pred, PredReg));
      } else
        BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
            .add(t1CondCodeOp(true))
            .addReg(Base, getKillRegState(KillOldBase))
            .addImm(Offset)
            .add(predOps(Pred, PredReg));
    } else {
      BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
          .addReg(Base, getKillRegState(KillOldBase))
          .addImm(Offset)
          .add(predOps(Pred, PredReg))
          .add(condCodeOp());
    }
    Base = NewBase;
    BaseKill = true; // New base is always killed straight away.
  }

  bool isDef = isLoadSingle(Opcode);

  // Get LS multiple opcode. Note that for Thumb1 this might be an opcode with
  // base register writeback.
  Opcode = getLoadStoreMultipleOpcode(Opcode, Mode);
  if (!Opcode)
    return nullptr;

  // Check if a Thumb1 LDM/STM merge is safe. This is the case if:
  // - There is no writeback (LDM of base register),
  // - the base register is killed by the merged instruction,
  // - or it's safe to overwrite the condition flags, i.e. to insert a SUBS
  //   to reset the base register.
  // Otherwise, don't merge.
  // It's safe to return here since the code to materialize a new base register
  // above is also conditional on SafeToClobberCPSR.
  if (isThumb1 && !SafeToClobberCPSR && Writeback && !BaseKill)
    return nullptr;

  MachineInstrBuilder MIB;

  if (Writeback) {
    assert(isThumb1 && "expected Writeback only inThumb1");
    if (Opcode == ARM::tLDMIA) {
      assert(!(ContainsReg(Regs, Base)) && "Thumb1 can't LDM ! with Base in Regs");
      // Update tLDMIA with writeback if necessary.
      Opcode = ARM::tLDMIA_UPD;
    }

    MIB = BuildMI(MBB, InsertBefore, DL, TII->get(Opcode));

    // Thumb1: we might need to set base writeback when building the MI.
    MIB.addReg(Base, getDefRegState(true))
       .addReg(Base, getKillRegState(BaseKill));

    // The base isn't dead after a merged instruction with writeback.
    // Insert a sub instruction after the newly formed instruction to reset.
    if (!BaseKill)
      UpdateBaseRegUses(MBB, InsertBefore, DL, Base, NumRegs, Pred, PredReg);
  } else {
    // No writeback, simply build the MachineInstr.
    MIB = BuildMI(MBB, InsertBefore, DL, TII->get(Opcode));
    MIB.addReg(Base, getKillRegState(BaseKill));
  }

  MIB.addImm(Pred).addReg(PredReg);

  for (const std::pair<unsigned, bool> &R : Regs)
    MIB.addReg(R.first, getDefRegState(isDef) | getKillRegState(R.second));

  MIB.cloneMergedMemRefs(Instrs);

  return MIB.getInstr();
}

MachineInstr *ARMLoadStoreOpt::CreateLoadStoreDouble(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
    ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
    ArrayRef<std::pair<unsigned, bool>> Regs,
    ArrayRef<MachineInstr*> Instrs) const {
  bool IsLoad = isi32Load(Opcode);
  assert((IsLoad || isi32Store(Opcode)) && "Must have integer load or store");
  unsigned LoadStoreOpcode = IsLoad ? ARM::t2LDRDi8 : ARM::t2STRDi8;

  assert(Regs.size() == 2);
  MachineInstrBuilder MIB = BuildMI(MBB, InsertBefore, DL,
                                    TII->get(LoadStoreOpcode));
  if (IsLoad) {
    MIB.addReg(Regs[0].first, RegState::Define)
       .addReg(Regs[1].first, RegState::Define);
  } else {
    MIB.addReg(Regs[0].first, getKillRegState(Regs[0].second))
       .addReg(Regs[1].first, getKillRegState(Regs[1].second));
  }
  MIB.addReg(Base).addImm(Offset).addImm(Pred).addReg(PredReg);
  MIB.cloneMergedMemRefs(Instrs);
  return MIB.getInstr();
}

/// Call MergeOps and update MemOps and merges accordingly on success.
MachineInstr *ARMLoadStoreOpt::MergeOpsUpdate(const MergeCandidate &Cand) {
  const MachineInstr *First = Cand.Instrs.front();
  unsigned Opcode = First->getOpcode();
  bool IsLoad = isLoadSingle(Opcode);
  SmallVector<std::pair<unsigned, bool>, 8> Regs;
  SmallVector<unsigned, 4> ImpDefs;
  DenseSet<unsigned> KilledRegs;
  DenseSet<unsigned> UsedRegs;
  // Determine list of registers and list of implicit super-register defs.
  for (const MachineInstr *MI : Cand.Instrs) {
    const MachineOperand &MO = getLoadStoreRegOp(*MI);
    Register Reg = MO.getReg();
    bool IsKill = MO.isKill();
    if (IsKill)
      KilledRegs.insert(Reg);
    Regs.push_back(std::make_pair(Reg, IsKill));
    UsedRegs.insert(Reg);

    if (IsLoad) {
      // Collect any implicit defs of super-registers, after merging we can't
      // be sure anymore that we properly preserved these live ranges and must
      // removed these implicit operands.
      for (const MachineOperand &MO : MI->implicit_operands()) {
        if (!MO.isReg() || !MO.isDef() || MO.isDead())
          continue;
        assert(MO.isImplicit());
        Register DefReg = MO.getReg();

        if (is_contained(ImpDefs, DefReg))
          continue;
        // We can ignore cases where the super-reg is read and written.
        if (MI->readsRegister(DefReg, /*TRI=*/nullptr))
          continue;
        ImpDefs.push_back(DefReg);
      }
    }
  }

  // Attempt the merge.
  using iterator = MachineBasicBlock::iterator;

  MachineInstr *LatestMI = Cand.Instrs[Cand.LatestMIIdx];
  iterator InsertBefore = std::next(iterator(LatestMI));
  MachineBasicBlock &MBB = *LatestMI->getParent();
  unsigned Offset = getMemoryOpOffset(*First);
  Register Base = getLoadStoreBaseOp(*First).getReg();
  bool BaseKill = LatestMI->killsRegister(Base, /*TRI=*/nullptr);
  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(*First, PredReg);
  DebugLoc DL = First->getDebugLoc();
  MachineInstr *Merged = nullptr;
  if (Cand.CanMergeToLSDouble)
    Merged = CreateLoadStoreDouble(MBB, InsertBefore, Offset, Base, BaseKill,
                                   Opcode, Pred, PredReg, DL, Regs,
                                   Cand.Instrs);
  if (!Merged && Cand.CanMergeToLSMulti)
    Merged = CreateLoadStoreMulti(MBB, InsertBefore, Offset, Base, BaseKill,
                                  Opcode, Pred, PredReg, DL, Regs, Cand.Instrs);
  if (!Merged)
    return nullptr;

  // Determine earliest instruction that will get removed. We then keep an
  // iterator just above it so the following erases don't invalidated it.
  iterator EarliestI(Cand.Instrs[Cand.EarliestMIIdx]);
  bool EarliestAtBegin = false;
  if (EarliestI == MBB.begin()) {
    EarliestAtBegin = true;
  } else {
    EarliestI = std::prev(EarliestI);
  }

  // Remove instructions which have been merged.
  for (MachineInstr *MI : Cand.Instrs)
    MBB.erase(MI);

  // Determine range between the earliest removed instruction and the new one.
  if (EarliestAtBegin)
    EarliestI = MBB.begin();
  else
    EarliestI = std::next(EarliestI);
  auto FixupRange = make_range(EarliestI, iterator(Merged));

  if (isLoadSingle(Opcode)) {
    // If the previous loads defined a super-reg, then we have to mark earlier
    // operands undef; Replicate the super-reg def on the merged instruction.
    for (MachineInstr &MI : FixupRange) {
      for (unsigned &ImpDefReg : ImpDefs) {
        for (MachineOperand &MO : MI.implicit_operands()) {
          if (!MO.isReg() || MO.getReg() != ImpDefReg)
            continue;
          if (MO.readsReg())
            MO.setIsUndef();
          else if (MO.isDef())
            ImpDefReg = 0;
        }
      }
    }

    MachineInstrBuilder MIB(*Merged->getParent()->getParent(), Merged);
    for (unsigned ImpDef : ImpDefs)
      MIB.addReg(ImpDef, RegState::ImplicitDefine);
  } else {
    // Remove kill flags: We are possibly storing the values later now.
    assert(isi32Store(Opcode) || Opcode == ARM::VSTRS || Opcode == ARM::VSTRD);
    for (MachineInstr &MI : FixupRange) {
      for (MachineOperand &MO : MI.uses()) {
        if (!MO.isReg() || !MO.isKill())
          continue;
        if (UsedRegs.count(MO.getReg()))
          MO.setIsKill(false);
      }
    }
    assert(ImpDefs.empty());
  }

  return Merged;
}

static bool isValidLSDoubleOffset(int Offset) {
  unsigned Value = abs(Offset);
  // t2LDRDi8/t2STRDi8 supports an 8 bit immediate which is internally
  // multiplied by 4.
  return (Value % 4) == 0 && Value < 1024;
}

/// Return true for loads/stores that can be combined to a double/multi
/// operation without increasing the requirements for alignment.
static bool mayCombineMisaligned(const TargetSubtargetInfo &STI,
                                 const MachineInstr &MI) {
  // vldr/vstr trap on misaligned pointers anyway, forming vldm makes no
  // difference.
  unsigned Opcode = MI.getOpcode();
  if (!isi32Load(Opcode) && !isi32Store(Opcode))
    return true;

  // Stack pointer alignment is out of the programmers control so we can trust
  // SP-relative loads/stores.
  if (getLoadStoreBaseOp(MI).getReg() == ARM::SP &&
      STI.getFrameLowering()->getTransientStackAlign() >= Align(4))
    return true;
  return false;
}

/// Find candidates for load/store multiple merge in list of MemOpQueueEntries.
void ARMLoadStoreOpt::FormCandidates(const MemOpQueue &MemOps) {
  const MachineInstr *FirstMI = MemOps[0].MI;
  unsigned Opcode = FirstMI->getOpcode();
  bool isNotVFP = isi32Load(Opcode) || isi32Store(Opcode);
  unsigned Size = getLSMultipleTransferSize(FirstMI);

  unsigned SIndex = 0;
  unsigned EIndex = MemOps.size();
  do {
    // Look at the first instruction.
    const MachineInstr *MI = MemOps[SIndex].MI;
    int Offset = MemOps[SIndex].Offset;
    const MachineOperand &PMO = getLoadStoreRegOp(*MI);
    Register PReg = PMO.getReg();
    unsigned PRegNum = PMO.isUndef() ? std::numeric_limits<unsigned>::max()
                                     : TRI->getEncodingValue(PReg);
    unsigned Latest = SIndex;
    unsigned Earliest = SIndex;
    unsigned Count = 1;
    bool CanMergeToLSDouble =
      STI->isThumb2() && isNotVFP && isValidLSDoubleOffset(Offset);
    // ARM errata 602117: LDRD with base in list may result in incorrect base
    // register when interrupted or faulted.
    if (STI->isCortexM3() && isi32Load(Opcode) &&
        PReg == getLoadStoreBaseOp(*MI).getReg())
      CanMergeToLSDouble = false;

    bool CanMergeToLSMulti = true;
    // On swift vldm/vstm starting with an odd register number as that needs
    // more uops than single vldrs.
    if (STI->hasSlowOddRegister() && !isNotVFP && (PRegNum % 2) == 1)
      CanMergeToLSMulti = false;

    // LDRD/STRD do not allow SP/PC. LDM/STM do not support it or have it
    // deprecated; LDM to PC is fine but cannot happen here.
    if (PReg == ARM::SP || PReg == ARM::PC)
      CanMergeToLSMulti = CanMergeToLSDouble = false;

    // Should we be conservative?
    if (AssumeMisalignedLoadStores && !mayCombineMisaligned(*STI, *MI))
      CanMergeToLSMulti = CanMergeToLSDouble = false;

    // vldm / vstm limit are 32 for S variants, 16 for D variants.
    unsigned Limit;
    switch (Opcode) {
    default:
      Limit = UINT_MAX;
      break;
    case ARM::VLDRD:
    case ARM::VSTRD:
      Limit = 16;
      break;
    }

    // Merge following instructions where possible.
    for (unsigned I = SIndex+1; I < EIndex; ++I, ++Count) {
      int NewOffset = MemOps[I].Offset;
      if (NewOffset != Offset + (int)Size)
        break;
      const MachineOperand &MO = getLoadStoreRegOp(*MemOps[I].MI);
      Register Reg = MO.getReg();
      if (Reg == ARM::SP || Reg == ARM::PC)
        break;
      if (Count == Limit)
        break;

      // See if the current load/store may be part of a multi load/store.
      unsigned RegNum = MO.isUndef() ? std::numeric_limits<unsigned>::max()
                                     : TRI->getEncodingValue(Reg);
      bool PartOfLSMulti = CanMergeToLSMulti;
      if (PartOfLSMulti) {
        // Register numbers must be in ascending order.
        if (RegNum <= PRegNum)
          PartOfLSMulti = false;
        // For VFP / NEON load/store multiples, the registers must be
        // consecutive and within the limit on the number of registers per
        // instruction.
        else if (!isNotVFP && RegNum != PRegNum+1)
          PartOfLSMulti = false;
      }
      // See if the current load/store may be part of a double load/store.
      bool PartOfLSDouble = CanMergeToLSDouble && Count <= 1;

      if (!PartOfLSMulti && !PartOfLSDouble)
        break;
      CanMergeToLSMulti &= PartOfLSMulti;
      CanMergeToLSDouble &= PartOfLSDouble;
      // Track MemOp with latest and earliest position (Positions are
      // counted in reverse).
      unsigned Position = MemOps[I].Position;
      if (Position < MemOps[Latest].Position)
        Latest = I;
      else if (Position > MemOps[Earliest].Position)
        Earliest = I;
      // Prepare for next MemOp.
      Offset += Size;
      PRegNum = RegNum;
    }

    // Form a candidate from the Ops collected so far.
    MergeCandidate *Candidate = new(Allocator.Allocate()) MergeCandidate;
    for (unsigned C = SIndex, CE = SIndex + Count; C < CE; ++C)
      Candidate->Instrs.push_back(MemOps[C].MI);
    Candidate->LatestMIIdx = Latest - SIndex;
    Candidate->EarliestMIIdx = Earliest - SIndex;
    Candidate->InsertPos = MemOps[Latest].Position;
    if (Count == 1)
      CanMergeToLSMulti = CanMergeToLSDouble = false;
    Candidate->CanMergeToLSMulti = CanMergeToLSMulti;
    Candidate->CanMergeToLSDouble = CanMergeToLSDouble;
    Candidates.push_back(Candidate);
    // Continue after the chain.
    SIndex += Count;
  } while (SIndex < EIndex);
}

static unsigned getUpdatingLSMultipleOpcode(unsigned Opc,
                                            ARM_AM::AMSubMode Mode) {
  switch (Opc) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::LDMIA_UPD;
    case ARM_AM::ib: return ARM::LDMIB_UPD;
    case ARM_AM::da: return ARM::LDMDA_UPD;
    case ARM_AM::db: return ARM::LDMDB_UPD;
    }
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::STMIA_UPD;
    case ARM_AM::ib: return ARM::STMIB_UPD;
    case ARM_AM::da: return ARM::STMDA_UPD;
    case ARM_AM::db: return ARM::STMDB_UPD;
    }
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2LDMIA_UPD;
    case ARM_AM::db: return ARM::t2LDMDB_UPD;
    }
  case ARM::t2STMIA:
  case ARM::t2STMDB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2STMIA_UPD;
    case ARM_AM::db: return ARM::t2STMDB_UPD;
    }
  case ARM::VLDMSIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMSIA_UPD;
    case ARM_AM::db: return ARM::VLDMSDB_UPD;
    }
  case ARM::VLDMDIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMDIA_UPD;
    case ARM_AM::db: return ARM::VLDMDDB_UPD;
    }
  case ARM::VSTMSIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMSIA_UPD;
    case ARM_AM::db: return ARM::VSTMSDB_UPD;
    }
  case ARM::VSTMDIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMDIA_UPD;
    case ARM_AM::db: return ARM::VSTMDDB_UPD;
    }
  }
}

/// Check if the given instruction increments or decrements a register and
/// return the amount it is incremented/decremented. Returns 0 if the CPSR flags
/// generated by the instruction are possibly read as well.
static int isIncrementOrDecrement(const MachineInstr &MI, Register Reg,
                                  ARMCC::CondCodes Pred, Register PredReg) {
  bool CheckCPSRDef;
  int Scale;
  switch (MI.getOpcode()) {
  case ARM::tADDi8:  Scale =  4; CheckCPSRDef = true; break;
  case ARM::tSUBi8:  Scale = -4; CheckCPSRDef = true; break;
  case ARM::t2SUBri:
  case ARM::t2SUBspImm:
  case ARM::SUBri:   Scale = -1; CheckCPSRDef = true; break;
  case ARM::t2ADDri:
  case ARM::t2ADDspImm:
  case ARM::ADDri:   Scale =  1; CheckCPSRDef = true; break;
  case ARM::tADDspi: Scale =  4; CheckCPSRDef = false; break;
  case ARM::tSUBspi: Scale = -4; CheckCPSRDef = false; break;
  default: return 0;
  }

  Register MIPredReg;
  if (MI.getOperand(0).getReg() != Reg ||
      MI.getOperand(1).getReg() != Reg ||
      getInstrPredicate(MI, MIPredReg) != Pred ||
      MIPredReg != PredReg)
    return 0;

  if (CheckCPSRDef && definesCPSR(MI))
    return 0;
  return MI.getOperand(2).getImm() * Scale;
}

/// Searches for an increment or decrement of \p Reg before \p MBBI.
static MachineBasicBlock::iterator
findIncDecBefore(MachineBasicBlock::iterator MBBI, Register Reg,
                 ARMCC::CondCodes Pred, Register PredReg, int &Offset) {
  Offset = 0;
  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineBasicBlock::iterator BeginMBBI = MBB.begin();
  MachineBasicBlock::iterator EndMBBI = MBB.end();
  if (MBBI == BeginMBBI)
    return EndMBBI;

  // Skip debug values.
  MachineBasicBlock::iterator PrevMBBI = std::prev(MBBI);
  while (PrevMBBI->isDebugInstr() && PrevMBBI != BeginMBBI)
    --PrevMBBI;

  Offset = isIncrementOrDecrement(*PrevMBBI, Reg, Pred, PredReg);
  return Offset == 0 ? EndMBBI : PrevMBBI;
}

/// Searches for a increment or decrement of \p Reg after \p MBBI.
static MachineBasicBlock::iterator
findIncDecAfter(MachineBasicBlock::iterator MBBI, Register Reg,
                ARMCC::CondCodes Pred, Register PredReg, int &Offset,
                const TargetRegisterInfo *TRI) {
  Offset = 0;
  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineBasicBlock::iterator EndMBBI = MBB.end();
  MachineBasicBlock::iterator NextMBBI = std::next(MBBI);
  while (NextMBBI != EndMBBI) {
    // Skip debug values.
    while (NextMBBI != EndMBBI && NextMBBI->isDebugInstr())
      ++NextMBBI;
    if (NextMBBI == EndMBBI)
      return EndMBBI;

    unsigned Off = isIncrementOrDecrement(*NextMBBI, Reg, Pred, PredReg);
    if (Off) {
      Offset = Off;
      return NextMBBI;
    }

    // SP can only be combined if it is the next instruction after the original
    // MBBI, otherwise we may be incrementing the stack pointer (invalidating
    // anything below the new pointer) when its frame elements are still in
    // use. Other registers can attempt to look further, until a different use
    // or def of the register is found.
    if (Reg == ARM::SP || NextMBBI->readsRegister(Reg, TRI) ||
        NextMBBI->definesRegister(Reg, TRI))
      return EndMBBI;

    ++NextMBBI;
  }
  return EndMBBI;
}

/// Fold proceeding/trailing inc/dec of base register into the
/// LDM/STM/VLDM{D|S}/VSTM{D|S} op when possible:
///
/// stmia rn, <ra, rb, rc>
/// rn := rn + 4 * 3;
/// =>
/// stmia rn!, <ra, rb, rc>
///
/// rn := rn - 4 * 3;
/// ldmia rn, <ra, rb, rc>
/// =>
/// ldmdb rn!, <ra, rb, rc>
bool ARMLoadStoreOpt::MergeBaseUpdateLSMultiple(MachineInstr *MI) {
  // Thumb1 is already using updating loads/stores.
  if (isThumb1) return false;
  LLVM_DEBUG(dbgs() << "Attempting to merge update of: " << *MI);

  const MachineOperand &BaseOP = MI->getOperand(0);
  Register Base = BaseOP.getReg();
  bool BaseKill = BaseOP.isKill();
  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  unsigned Opcode = MI->getOpcode();
  DebugLoc DL = MI->getDebugLoc();

  // Can't use an updating ld/st if the base register is also a dest
  // register. e.g. ldmdb r0!, {r0, r1, r2}. The behavior is undefined.
  for (const MachineOperand &MO : llvm::drop_begin(MI->operands(), 2))
    if (MO.getReg() == Base)
      return false;

  int Bytes = getLSMultipleTransferSize(MI);
  MachineBasicBlock &MBB = *MI->getParent();
  MachineBasicBlock::iterator MBBI(MI);
  int Offset;
  MachineBasicBlock::iterator MergeInstr
    = findIncDecBefore(MBBI, Base, Pred, PredReg, Offset);
  ARM_AM::AMSubMode Mode = getLoadStoreMultipleSubMode(Opcode);
  if (Mode == ARM_AM::ia && Offset == -Bytes) {
    Mode = ARM_AM::db;
  } else if (Mode == ARM_AM::ib && Offset == -Bytes) {
    Mode = ARM_AM::da;
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset, TRI);
    if (((Mode != ARM_AM::ia && Mode != ARM_AM::ib) || Offset != Bytes) &&
        ((Mode != ARM_AM::da && Mode != ARM_AM::db) || Offset != -Bytes)) {

      // We couldn't find an inc/dec to merge. But if the base is dead, we
      // can still change to a writeback form as that will save us 2 bytes
      // of code size. It can create WAW hazards though, so only do it if
      // we're minimizing code size.
      if (!STI->hasMinSize() || !BaseKill)
        return false;

      bool HighRegsUsed = false;
      for (const MachineOperand &MO : llvm::drop_begin(MI->operands(), 2))
        if (MO.getReg() >= ARM::R8) {
          HighRegsUsed = true;
          break;
        }

      if (!HighRegsUsed)
        MergeInstr = MBB.end();
      else
        return false;
    }
  }
  if (MergeInstr != MBB.end()) {
    LLVM_DEBUG(dbgs() << "  Erasing old increment: " << *MergeInstr);
    MBB.erase(MergeInstr);
  }

  unsigned NewOpc = getUpdatingLSMultipleOpcode(Opcode, Mode);
  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc))
    .addReg(Base, getDefRegState(true)) // WB base register
    .addReg(Base, getKillRegState(BaseKill))
    .addImm(Pred).addReg(PredReg);

  // Transfer the rest of operands.
  for (const MachineOperand &MO : llvm::drop_begin(MI->operands(), 3))
    MIB.add(MO);

  // Transfer memoperands.
  MIB.setMemRefs(MI->memoperands());

  LLVM_DEBUG(dbgs() << "  Added new load/store: " << *MIB);
  MBB.erase(MBBI);
  return true;
}

static unsigned getPreIndexedLoadStoreOpcode(unsigned Opc,
                                             ARM_AM::AddrOpc Mode) {
  switch (Opc) {
  case ARM::LDRi12:
    return ARM::LDR_PRE_IMM;
  case ARM::STRi12:
    return ARM::STR_PRE_IMM;
  case ARM::VLDRS:
    return Mode == ARM_AM::add ? ARM::VLDMSIA_UPD : ARM::VLDMSDB_UPD;
  case ARM::VLDRD:
    return Mode == ARM_AM::add ? ARM::VLDMDIA_UPD : ARM::VLDMDDB_UPD;
  case ARM::VSTRS:
    return Mode == ARM_AM::add ? ARM::VSTMSIA_UPD : ARM::VSTMSDB_UPD;
  case ARM::VSTRD:
    return Mode == ARM_AM::add ? ARM::VSTMDIA_UPD : ARM::VSTMDDB_UPD;
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    return ARM::t2LDR_PRE;
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    return ARM::t2STR_PRE;
  default: llvm_unreachable("Unhandled opcode!");
  }
}

static unsigned getPostIndexedLoadStoreOpcode(unsigned Opc,
                                              ARM_AM::AddrOpc Mode) {
  switch (Opc) {
  case ARM::LDRi12:
    return ARM::LDR_POST_IMM;
  case ARM::STRi12:
    return ARM::STR_POST_IMM;
  case ARM::VLDRS:
    return Mode == ARM_AM::add ? ARM::VLDMSIA_UPD : ARM::VLDMSDB_UPD;
  case ARM::VLDRD:
    return Mode == ARM_AM::add ? ARM::VLDMDIA_UPD : ARM::VLDMDDB_UPD;
  case ARM::VSTRS:
    return Mode == ARM_AM::add ? ARM::VSTMSIA_UPD : ARM::VSTMSDB_UPD;
  case ARM::VSTRD:
    return Mode == ARM_AM::add ? ARM::VSTMDIA_UPD : ARM::VSTMDDB_UPD;
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    return ARM::t2LDR_POST;
  case ARM::t2LDRBi8:
  case ARM::t2LDRBi12:
    return ARM::t2LDRB_POST;
  case ARM::t2LDRSBi8:
  case ARM::t2LDRSBi12:
    return ARM::t2LDRSB_POST;
  case ARM::t2LDRHi8:
  case ARM::t2LDRHi12:
    return ARM::t2LDRH_POST;
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSHi12:
    return ARM::t2LDRSH_POST;
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    return ARM::t2STR_POST;
  case ARM::t2STRBi8:
  case ARM::t2STRBi12:
    return ARM::t2STRB_POST;
  case ARM::t2STRHi8:
  case ARM::t2STRHi12:
    return ARM::t2STRH_POST;

  case ARM::MVE_VLDRBS16:
    return ARM::MVE_VLDRBS16_post;
  case ARM::MVE_VLDRBS32:
    return ARM::MVE_VLDRBS32_post;
  case ARM::MVE_VLDRBU16:
    return ARM::MVE_VLDRBU16_post;
  case ARM::MVE_VLDRBU32:
    return ARM::MVE_VLDRBU32_post;
  case ARM::MVE_VLDRHS32:
    return ARM::MVE_VLDRHS32_post;
  case ARM::MVE_VLDRHU32:
    return ARM::MVE_VLDRHU32_post;
  case ARM::MVE_VLDRBU8:
    return ARM::MVE_VLDRBU8_post;
  case ARM::MVE_VLDRHU16:
    return ARM::MVE_VLDRHU16_post;
  case ARM::MVE_VLDRWU32:
    return ARM::MVE_VLDRWU32_post;
  case ARM::MVE_VSTRB16:
    return ARM::MVE_VSTRB16_post;
  case ARM::MVE_VSTRB32:
    return ARM::MVE_VSTRB32_post;
  case ARM::MVE_VSTRH32:
    return ARM::MVE_VSTRH32_post;
  case ARM::MVE_VSTRBU8:
    return ARM::MVE_VSTRBU8_post;
  case ARM::MVE_VSTRHU16:
    return ARM::MVE_VSTRHU16_post;
  case ARM::MVE_VSTRWU32:
    return ARM::MVE_VSTRWU32_post;

  default: llvm_unreachable("Unhandled opcode!");
  }
}

/// Fold proceeding/trailing inc/dec of base register into the
/// LDR/STR/FLD{D|S}/FST{D|S} op when possible:
bool ARMLoadStoreOpt::MergeBaseUpdateLoadStore(MachineInstr *MI) {
  // Thumb1 doesn't have updating LDR/STR.
  // FIXME: Use LDM/STM with single register instead.
  if (isThumb1) return false;
  LLVM_DEBUG(dbgs() << "Attempting to merge update of: " << *MI);

  Register Base = getLoadStoreBaseOp(*MI).getReg();
  bool BaseKill = getLoadStoreBaseOp(*MI).isKill();
  unsigned Opcode = MI->getOpcode();
  DebugLoc DL = MI->getDebugLoc();
  bool isAM5 = (Opcode == ARM::VLDRD || Opcode == ARM::VLDRS ||
                Opcode == ARM::VSTRD || Opcode == ARM::VSTRS);
  bool isAM2 = (Opcode == ARM::LDRi12 || Opcode == ARM::STRi12);
  if (isi32Load(Opcode) || isi32Store(Opcode))
    if (MI->getOperand(2).getImm() != 0)
      return false;
  if (isAM5 && ARM_AM::getAM5Offset(MI->getOperand(2).getImm()) != 0)
    return false;

  // Can't do the merge if the destination register is the same as the would-be
  // writeback register.
  if (MI->getOperand(0).getReg() == Base)
    return false;

  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  int Bytes = getLSMultipleTransferSize(MI);
  MachineBasicBlock &MBB = *MI->getParent();
  MachineBasicBlock::iterator MBBI(MI);
  int Offset;
  MachineBasicBlock::iterator MergeInstr
    = findIncDecBefore(MBBI, Base, Pred, PredReg, Offset);
  unsigned NewOpc;
  if (!isAM5 && Offset == Bytes) {
    NewOpc = getPreIndexedLoadStoreOpcode(Opcode, ARM_AM::add);
  } else if (Offset == -Bytes) {
    NewOpc = getPreIndexedLoadStoreOpcode(Opcode, ARM_AM::sub);
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset, TRI);
    if (MergeInstr == MBB.end())
      return false;

    NewOpc = getPostIndexedLoadStoreOpcode(Opcode, ARM_AM::add);
    if ((isAM5 && Offset != Bytes) ||
        (!isAM5 && !isLegalAddressImm(NewOpc, Offset, TII))) {
      NewOpc = getPostIndexedLoadStoreOpcode(Opcode, ARM_AM::sub);
      if (isAM5 || !isLegalAddressImm(NewOpc, Offset, TII))
        return false;
    }
  }
  LLVM_DEBUG(dbgs() << "  Erasing old increment: " << *MergeInstr);
  MBB.erase(MergeInstr);

  ARM_AM::AddrOpc AddSub = Offset < 0 ? ARM_AM::sub : ARM_AM::add;

  bool isLd = isLoadSingle(Opcode);
  if (isAM5) {
    // VLDM[SD]_UPD, VSTM[SD]_UPD
    // (There are no base-updating versions of VLDR/VSTR instructions, but the
    // updating load/store-multiple instructions can be used with only one
    // register.)
    MachineOperand &MO = MI->getOperand(0);
    auto MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc))
                   .addReg(Base, getDefRegState(true)) // WB base register
                   .addReg(Base, getKillRegState(isLd ? BaseKill : false))
                   .addImm(Pred)
                   .addReg(PredReg)
                   .addReg(MO.getReg(), (isLd ? getDefRegState(true)
                                              : getKillRegState(MO.isKill())))
                   .cloneMemRefs(*MI);
    (void)MIB;
    LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
  } else if (isLd) {
    if (isAM2) {
      // LDR_PRE, LDR_POST
      if (NewOpc == ARM::LDR_PRE_IMM || NewOpc == ARM::LDRB_PRE_IMM) {
        auto MIB =
            BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
                .addReg(Base, RegState::Define)
                .addReg(Base)
                .addImm(Offset)
                .addImm(Pred)
                .addReg(PredReg)
                .cloneMemRefs(*MI);
        (void)MIB;
        LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
      } else {
        int Imm = ARM_AM::getAM2Opc(AddSub, abs(Offset), ARM_AM::no_shift);
        auto MIB =
            BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
                .addReg(Base, RegState::Define)
                .addReg(Base)
                .addReg(0)
                .addImm(Imm)
                .add(predOps(Pred, PredReg))
                .cloneMemRefs(*MI);
        (void)MIB;
        LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
      }
    } else {
      // t2LDR_PRE, t2LDR_POST
      auto MIB =
          BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
              .addReg(Base, RegState::Define)
              .addReg(Base)
              .addImm(Offset)
              .add(predOps(Pred, PredReg))
              .cloneMemRefs(*MI);
      (void)MIB;
      LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
    }
  } else {
    MachineOperand &MO = MI->getOperand(0);
    // FIXME: post-indexed stores use am2offset_imm, which still encodes
    // the vestigal zero-reg offset register. When that's fixed, this clause
    // can be removed entirely.
    if (isAM2 && NewOpc == ARM::STR_POST_IMM) {
      int Imm = ARM_AM::getAM2Opc(AddSub, abs(Offset), ARM_AM::no_shift);
      // STR_PRE, STR_POST
      auto MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc), Base)
                     .addReg(MO.getReg(), getKillRegState(MO.isKill()))
                     .addReg(Base)
                     .addReg(0)
                     .addImm(Imm)
                     .add(predOps(Pred, PredReg))
                     .cloneMemRefs(*MI);
      (void)MIB;
      LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
    } else {
      // t2STR_PRE, t2STR_POST
      auto MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc), Base)
                     .addReg(MO.getReg(), getKillRegState(MO.isKill()))
                     .addReg(Base)
                     .addImm(Offset)
                     .add(predOps(Pred, PredReg))
                     .cloneMemRefs(*MI);
      (void)MIB;
      LLVM_DEBUG(dbgs() << "  Added new instruction: " << *MIB);
    }
  }
  MBB.erase(MBBI);

  return true;
}

bool ARMLoadStoreOpt::MergeBaseUpdateLSDouble(MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  assert((Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8) &&
         "Must have t2STRDi8 or t2LDRDi8");
  if (MI.getOperand(3).getImm() != 0)
    return false;
  LLVM_DEBUG(dbgs() << "Attempting to merge update of: " << MI);

  // Behaviour for writeback is undefined if base register is the same as one
  // of the others.
  const MachineOperand &BaseOp = MI.getOperand(2);
  Register Base = BaseOp.getReg();
  const MachineOperand &Reg0Op = MI.getOperand(0);
  const MachineOperand &Reg1Op = MI.getOperand(1);
  if (Reg0Op.getReg() == Base || Reg1Op.getReg() == Base)
    return false;

  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(MI, PredReg);
  MachineBasicBlock::iterator MBBI(MI);
  MachineBasicBlock &MBB = *MI.getParent();
  int Offset;
  MachineBasicBlock::iterator MergeInstr = findIncDecBefore(MBBI, Base, Pred,
                                                            PredReg, Offset);
  unsigned NewOpc;
  if (Offset == 8 || Offset == -8) {
    NewOpc = Opcode == ARM::t2LDRDi8 ? ARM::t2LDRD_PRE : ARM::t2STRD_PRE;
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset, TRI);
    if (MergeInstr == MBB.end())
      return false;
    NewOpc = Opcode == ARM::t2LDRDi8 ? ARM::t2LDRD_POST : ARM::t2STRD_POST;
    if (!isLegalAddressImm(NewOpc, Offset, TII))
      return false;
  }
  LLVM_DEBUG(dbgs() << "  Erasing old increment: " << *MergeInstr);
  MBB.erase(MergeInstr);

  DebugLoc DL = MI.getDebugLoc();
  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc));
  if (NewOpc == ARM::t2LDRD_PRE || NewOpc == ARM::t2LDRD_POST) {
    MIB.add(Reg0Op).add(Reg1Op).addReg(BaseOp.getReg(), RegState::Define);
  } else {
    assert(NewOpc == ARM::t2STRD_PRE || NewOpc == ARM::t2STRD_POST);
    MIB.addReg(BaseOp.getReg(), RegState::Define).add(Reg0Op).add(Reg1Op);
  }
  MIB.addReg(BaseOp.getReg(), RegState::Kill)
     .addImm(Offset).addImm(Pred).addReg(PredReg);
  assert(TII->get(Opcode).getNumOperands() == 6 &&
         TII->get(NewOpc).getNumOperands() == 7 &&
         "Unexpected number of operands in Opcode specification.");

  // Transfer implicit operands.
  for (const MachineOperand &MO : MI.implicit_operands())
    MIB.add(MO);
  MIB.cloneMemRefs(MI);

  LLVM_DEBUG(dbgs() << "  Added new load/store: " << *MIB);
  MBB.erase(MBBI);
  return true;
}

/// Returns true if instruction is a memory operation that this pass is capable
/// of operating on.
static bool isMemoryOp(const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  case ARM::VLDRS:
  case ARM::VSTRS:
  case ARM::VLDRD:
  case ARM::VSTRD:
  case ARM::LDRi12:
  case ARM::STRi12:
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    break;
  default:
    return false;
  }
  if (!MI.getOperand(1).isReg())
    return false;

  // When no memory operands are present, conservatively assume unaligned,
  // volatile, unfoldable.
  if (!MI.hasOneMemOperand())
    return false;

  const MachineMemOperand &MMO = **MI.memoperands_begin();

  // Don't touch volatile memory accesses - we may be changing their order.
  // TODO: We could allow unordered and monotonic atomics here, but we need to
  // make sure the resulting ldm/stm is correctly marked as atomic.
  if (MMO.isVolatile() || MMO.isAtomic())
    return false;

  // Unaligned ldr/str is emulated by some kernels, but unaligned ldm/stm is
  // not.
  if (MMO.getAlign() < Align(4))
    return false;

  // str <undef> could probably be eliminated entirely, but for now we just want
  // to avoid making a mess of it.
  // FIXME: Use str <undef> as a wildcard to enable better stm folding.
  if (MI.getOperand(0).isReg() && MI.getOperand(0).isUndef())
    return false;

  // Likewise don't mess with references to undefined addresses.
  if (MI.getOperand(1).isUndef())
    return false;

  return true;
}

static void InsertLDR_STR(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI, int Offset,
                          bool isDef, unsigned NewOpc, unsigned Reg,
                          bool RegDeadKill, bool RegUndef, unsigned BaseReg,
                          bool BaseKill, bool BaseUndef, ARMCC::CondCodes Pred,
                          unsigned PredReg, const TargetInstrInfo *TII,
                          MachineInstr *MI) {
  if (isDef) {
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MBBI->getDebugLoc(),
                                      TII->get(NewOpc))
      .addReg(Reg, getDefRegState(true) | getDeadRegState(RegDeadKill))
      .addReg(BaseReg, getKillRegState(BaseKill)|getUndefRegState(BaseUndef));
    MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
    // FIXME: This is overly conservative; the new instruction accesses 4
    // bytes, not 8.
    MIB.cloneMemRefs(*MI);
  } else {
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MBBI->getDebugLoc(),
                                      TII->get(NewOpc))
      .addReg(Reg, getKillRegState(RegDeadKill) | getUndefRegState(RegUndef))
      .addReg(BaseReg, getKillRegState(BaseKill)|getUndefRegState(BaseUndef));
    MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
    // FIXME: This is overly conservative; the new instruction accesses 4
    // bytes, not 8.
    MIB.cloneMemRefs(*MI);
  }
}

bool ARMLoadStoreOpt::FixInvalidRegPairOp(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator &MBBI) {
  MachineInstr *MI = &*MBBI;
  unsigned Opcode = MI->getOpcode();
  // FIXME: Code/comments below check Opcode == t2STRDi8, but this check returns
  // if we see this opcode.
  if (Opcode != ARM::LDRD && Opcode != ARM::STRD && Opcode != ARM::t2LDRDi8)
    return false;

  const MachineOperand &BaseOp = MI->getOperand(2);
  Register BaseReg = BaseOp.getReg();
  Register EvenReg = MI->getOperand(0).getReg();
  Register OddReg = MI->getOperand(1).getReg();
  unsigned EvenRegNum = TRI->getDwarfRegNum(EvenReg, false);
  unsigned OddRegNum  = TRI->getDwarfRegNum(OddReg, false);

  // ARM errata 602117: LDRD with base in list may result in incorrect base
  // register when interrupted or faulted.
  bool Errata602117 = EvenReg == BaseReg &&
    (Opcode == ARM::LDRD || Opcode == ARM::t2LDRDi8) && STI->isCortexM3();
  // ARM LDRD/STRD needs consecutive registers.
  bool NonConsecutiveRegs = (Opcode == ARM::LDRD || Opcode == ARM::STRD) &&
    (EvenRegNum % 2 != 0 || EvenRegNum + 1 != OddRegNum);

  if (!Errata602117 && !NonConsecutiveRegs)
    return false;

  bool isT2 = Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8;
  bool isLd = Opcode == ARM::LDRD || Opcode == ARM::t2LDRDi8;
  bool EvenDeadKill = isLd ?
    MI->getOperand(0).isDead() : MI->getOperand(0).isKill();
  bool EvenUndef = MI->getOperand(0).isUndef();
  bool OddDeadKill  = isLd ?
    MI->getOperand(1).isDead() : MI->getOperand(1).isKill();
  bool OddUndef = MI->getOperand(1).isUndef();
  bool BaseKill = BaseOp.isKill();
  bool BaseUndef = BaseOp.isUndef();
  assert((isT2 || MI->getOperand(3).getReg() == ARM::NoRegister) &&
         "register offset not handled below");
  int OffImm = getMemoryOpOffset(*MI);
  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);

  if (OddRegNum > EvenRegNum && OffImm == 0) {
    // Ascending register numbers and no offset. It's safe to change it to a
    // ldm or stm.
    unsigned NewOpc = (isLd)
      ? (isT2 ? ARM::t2LDMIA : ARM::LDMIA)
      : (isT2 ? ARM::t2STMIA : ARM::STMIA);
    if (isLd) {
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(NewOpc))
        .addReg(BaseReg, getKillRegState(BaseKill))
        .addImm(Pred).addReg(PredReg)
        .addReg(EvenReg, getDefRegState(isLd) | getDeadRegState(EvenDeadKill))
        .addReg(OddReg,  getDefRegState(isLd) | getDeadRegState(OddDeadKill))
        .cloneMemRefs(*MI);
      ++NumLDRD2LDM;
    } else {
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(NewOpc))
        .addReg(BaseReg, getKillRegState(BaseKill))
        .addImm(Pred).addReg(PredReg)
        .addReg(EvenReg,
                getKillRegState(EvenDeadKill) | getUndefRegState(EvenUndef))
        .addReg(OddReg,
                getKillRegState(OddDeadKill)  | getUndefRegState(OddUndef))
        .cloneMemRefs(*MI);
      ++NumSTRD2STM;
    }
  } else {
    // Split into two instructions.
    unsigned NewOpc = (isLd)
      ? (isT2 ? (OffImm < 0 ? ARM::t2LDRi8 : ARM::t2LDRi12) : ARM::LDRi12)
      : (isT2 ? (OffImm < 0 ? ARM::t2STRi8 : ARM::t2STRi12) : ARM::STRi12);
    // Be extra careful for thumb2. t2LDRi8 can't reference a zero offset,
    // so adjust and use t2LDRi12 here for that.
    unsigned NewOpc2 = (isLd)
      ? (isT2 ? (OffImm+4 < 0 ? ARM::t2LDRi8 : ARM::t2LDRi12) : ARM::LDRi12)
      : (isT2 ? (OffImm+4 < 0 ? ARM::t2STRi8 : ARM::t2STRi12) : ARM::STRi12);
    // If this is a load, make sure the first load does not clobber the base
    // register before the second load reads it.
    if (isLd && TRI->regsOverlap(EvenReg, BaseReg)) {
      assert(!TRI->regsOverlap(OddReg, BaseReg));
      InsertLDR_STR(MBB, MBBI, OffImm + 4, isLd, NewOpc2, OddReg, OddDeadKill,
                    false, BaseReg, false, BaseUndef, Pred, PredReg, TII, MI);
      InsertLDR_STR(MBB, MBBI, OffImm, isLd, NewOpc, EvenReg, EvenDeadKill,
                    false, BaseReg, BaseKill, BaseUndef, Pred, PredReg, TII,
                    MI);
    } else {
      if (OddReg == EvenReg && EvenDeadKill) {
        // If the two source operands are the same, the kill marker is
        // probably on the first one. e.g.
        // t2STRDi8 killed %r5, %r5, killed %r9, 0, 14, %reg0
        EvenDeadKill = false;
        OddDeadKill = true;
      }
      // Never kill the base register in the first instruction.
      if (EvenReg == BaseReg)
        EvenDeadKill = false;
      InsertLDR_STR(MBB, MBBI, OffImm, isLd, NewOpc, EvenReg, EvenDeadKill,
                    EvenUndef, BaseReg, false, BaseUndef, Pred, PredReg, TII,
                    MI);
      InsertLDR_STR(MBB, MBBI, OffImm + 4, isLd, NewOpc2, OddReg, OddDeadKill,
                    OddUndef, BaseReg, BaseKill, BaseUndef, Pred, PredReg, TII,
                    MI);
    }
    if (isLd)
      ++NumLDRD2LDR;
    else
      ++NumSTRD2STR;
  }

  MBBI = MBB.erase(MBBI);
  return true;
}

/// An optimization pass to turn multiple LDR / STR ops of the same base and
/// incrementing offset into LDM / STM ops.
bool ARMLoadStoreOpt::LoadStoreMultipleOpti(MachineBasicBlock &MBB) {
  MemOpQueue MemOps;
  unsigned CurrBase = 0;
  unsigned CurrOpc = ~0u;
  ARMCC::CondCodes CurrPred = ARMCC::AL;
  unsigned Position = 0;
  assert(Candidates.size() == 0);
  assert(MergeBaseCandidates.size() == 0);
  LiveRegsValid = false;

  for (MachineBasicBlock::iterator I = MBB.end(), MBBI; I != MBB.begin();
       I = MBBI) {
    // The instruction in front of the iterator is the one we look at.
    MBBI = std::prev(I);
    if (FixInvalidRegPairOp(MBB, MBBI))
      continue;
    ++Position;

    if (isMemoryOp(*MBBI)) {
      unsigned Opcode = MBBI->getOpcode();
      const MachineOperand &MO = MBBI->getOperand(0);
      Register Reg = MO.getReg();
      Register Base = getLoadStoreBaseOp(*MBBI).getReg();
      Register PredReg;
      ARMCC::CondCodes Pred = getInstrPredicate(*MBBI, PredReg);
      int Offset = getMemoryOpOffset(*MBBI);
      if (CurrBase == 0) {
        // Start of a new chain.
        CurrBase = Base;
        CurrOpc  = Opcode;
        CurrPred = Pred;
        MemOps.push_back(MemOpQueueEntry(*MBBI, Offset, Position));
        continue;
      }
      // Note: No need to match PredReg in the next if.
      if (CurrOpc == Opcode && CurrBase == Base && CurrPred == Pred) {
        // Watch out for:
        //   r4 := ldr [r0, #8]
        //   r4 := ldr [r0, #4]
        // or
        //   r0 := ldr [r0]
        // If a load overrides the base register or a register loaded by
        // another load in our chain, we cannot take this instruction.
        bool Overlap = false;
        if (isLoadSingle(Opcode)) {
          Overlap = (Base == Reg);
          if (!Overlap) {
            for (const MemOpQueueEntry &E : MemOps) {
              if (TRI->regsOverlap(Reg, E.MI->getOperand(0).getReg())) {
                Overlap = true;
                break;
              }
            }
          }
        }

        if (!Overlap) {
          // Check offset and sort memory operation into the current chain.
          if (Offset > MemOps.back().Offset) {
            MemOps.push_back(MemOpQueueEntry(*MBBI, Offset, Position));
            continue;
          } else {
            MemOpQueue::iterator MI, ME;
            for (MI = MemOps.begin(), ME = MemOps.end(); MI != ME; ++MI) {
              if (Offset < MI->Offset) {
                // Found a place to insert.
                break;
              }
              if (Offset == MI->Offset) {
                // Collision, abort.
                MI = ME;
                break;
              }
            }
            if (MI != MemOps.end()) {
              MemOps.insert(MI, MemOpQueueEntry(*MBBI, Offset, Position));
              continue;
            }
          }
        }
      }

      // Don't advance the iterator; The op will start a new chain next.
      MBBI = I;
      --Position;
      // Fallthrough to look into existing chain.
    } else if (MBBI->isDebugInstr()) {
      continue;
    } else if (MBBI->getOpcode() == ARM::t2LDRDi8 ||
               MBBI->getOpcode() == ARM::t2STRDi8) {
      // ARMPreAllocLoadStoreOpt has already formed some LDRD/STRD instructions
      // remember them because we may still be able to merge add/sub into them.
      MergeBaseCandidates.push_back(&*MBBI);
    }

    // If we are here then the chain is broken; Extract candidates for a merge.
    if (MemOps.size() > 0) {
      FormCandidates(MemOps);
      // Reset for the next chain.
      CurrBase = 0;
      CurrOpc = ~0u;
      CurrPred = ARMCC::AL;
      MemOps.clear();
    }
  }
  if (MemOps.size() > 0)
    FormCandidates(MemOps);

  // Sort candidates so they get processed from end to begin of the basic
  // block later; This is necessary for liveness calculation.
  auto LessThan = [](const MergeCandidate* M0, const MergeCandidate *M1) {
    return M0->InsertPos < M1->InsertPos;
  };
  llvm::sort(Candidates, LessThan);

  // Go through list of candidates and merge.
  bool Changed = false;
  for (const MergeCandidate *Candidate : Candidates) {
    if (Candidate->CanMergeToLSMulti || Candidate->CanMergeToLSDouble) {
      MachineInstr *Merged = MergeOpsUpdate(*Candidate);
      // Merge preceding/trailing base inc/dec into the merged op.
      if (Merged) {
        Changed = true;
        unsigned Opcode = Merged->getOpcode();
        if (Opcode == ARM::t2STRDi8 || Opcode == ARM::t2LDRDi8)
          MergeBaseUpdateLSDouble(*Merged);
        else
          MergeBaseUpdateLSMultiple(Merged);
      } else {
        for (MachineInstr *MI : Candidate->Instrs) {
          if (MergeBaseUpdateLoadStore(MI))
            Changed = true;
        }
      }
    } else {
      assert(Candidate->Instrs.size() == 1);
      if (MergeBaseUpdateLoadStore(Candidate->Instrs.front()))
        Changed = true;
    }
  }
  Candidates.clear();
  // Try to fold add/sub into the LDRD/STRD formed by ARMPreAllocLoadStoreOpt.
  for (MachineInstr *MI : MergeBaseCandidates)
    MergeBaseUpdateLSDouble(*MI);
  MergeBaseCandidates.clear();

  return Changed;
}

/// If this is a exit BB, try merging the return ops ("bx lr" and "mov pc, lr")
/// into the preceding stack restore so it directly restore the value of LR
/// into pc.
///   ldmfd sp!, {..., lr}
///   bx lr
/// or
///   ldmfd sp!, {..., lr}
///   mov pc, lr
/// =>
///   ldmfd sp!, {..., pc}
bool ARMLoadStoreOpt::MergeReturnIntoLDM(MachineBasicBlock &MBB) {
  // Thumb1 LDM doesn't allow high registers.
  if (isThumb1) return false;
  if (MBB.empty()) return false;

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  if (MBBI != MBB.begin() && MBBI != MBB.end() &&
      (MBBI->getOpcode() == ARM::BX_RET ||
       MBBI->getOpcode() == ARM::tBX_RET ||
       MBBI->getOpcode() == ARM::MOVPCLR)) {
    MachineBasicBlock::iterator PrevI = std::prev(MBBI);
    // Ignore any debug instructions.
    while (PrevI->isDebugInstr() && PrevI != MBB.begin())
      --PrevI;
    MachineInstr &PrevMI = *PrevI;
    unsigned Opcode = PrevMI.getOpcode();
    if (Opcode == ARM::LDMIA_UPD || Opcode == ARM::LDMDA_UPD ||
        Opcode == ARM::LDMDB_UPD || Opcode == ARM::LDMIB_UPD ||
        Opcode == ARM::t2LDMIA_UPD || Opcode == ARM::t2LDMDB_UPD) {
      MachineOperand &MO = PrevMI.getOperand(PrevMI.getNumOperands() - 1);
      if (MO.getReg() != ARM::LR)
        return false;
      unsigned NewOpc = (isThumb2 ? ARM::t2LDMIA_RET : ARM::LDMIA_RET);
      assert(((isThumb2 && Opcode == ARM::t2LDMIA_UPD) ||
              Opcode == ARM::LDMIA_UPD) && "Unsupported multiple load-return!");
      PrevMI.setDesc(TII->get(NewOpc));
      MO.setReg(ARM::PC);
      PrevMI.copyImplicitOps(*MBB.getParent(), *MBBI);
      MBB.erase(MBBI);
      return true;
    }
  }
  return false;
}

bool ARMLoadStoreOpt::CombineMovBx(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  if (MBBI == MBB.begin() || MBBI == MBB.end() ||
      MBBI->getOpcode() != ARM::tBX_RET)
    return false;

  MachineBasicBlock::iterator Prev = MBBI;
  --Prev;
  if (Prev->getOpcode() != ARM::tMOVr ||
      !Prev->definesRegister(ARM::LR, /*TRI=*/nullptr))
    return false;

  for (auto Use : Prev->uses())
    if (Use.isKill()) {
      assert(STI->hasV4TOps());
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(ARM::tBX))
          .addReg(Use.getReg(), RegState::Kill)
          .add(predOps(ARMCC::AL))
          .copyImplicitOps(*MBBI);
      MBB.erase(MBBI);
      MBB.erase(Prev);
      return true;
    }

  llvm_unreachable("tMOVr doesn't kill a reg before tBX_RET?");
}

bool ARMLoadStoreOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  MF = &Fn;
  STI = &Fn.getSubtarget<ARMSubtarget>();
  TL = STI->getTargetLowering();
  AFI = Fn.getInfo<ARMFunctionInfo>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();

  RegClassInfoValid = false;
  isThumb2 = AFI->isThumb2Function();
  isThumb1 = AFI->isThumbFunction() && !isThumb2;

  bool Modified = false, ModifiedLDMReturn = false;
  for (MachineBasicBlock &MBB : Fn) {
    Modified |= LoadStoreMultipleOpti(MBB);
    if (STI->hasV5TOps() && !AFI->shouldSignReturnAddress())
      ModifiedLDMReturn |= MergeReturnIntoLDM(MBB);
    if (isThumb1)
      Modified |= CombineMovBx(MBB);
  }
  Modified |= ModifiedLDMReturn;

  // If we merged a BX instruction into an LDM, we need to re-calculate whether
  // LR is restored. This check needs to consider the whole function, not just
  // the instruction(s) we changed, because there may be other BX returns which
  // still need LR to be restored.
  if (ModifiedLDMReturn)
    ARMFrameLowering::updateLRRestored(Fn);

  Allocator.DestroyAll();
  return Modified;
}

#define ARM_PREALLOC_LOAD_STORE_OPT_NAME                                       \
  "ARM pre- register allocation load / store optimization pass"

namespace {

  /// Pre- register allocation pass that move load / stores from consecutive
  /// locations close to make it more likely they will be combined later.
  struct ARMPreAllocLoadStoreOpt : public MachineFunctionPass{
    static char ID;

    AliasAnalysis *AA;
    const DataLayout *TD;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    MachineRegisterInfo *MRI;
    MachineDominatorTree *DT;
    MachineFunction *MF;

    ARMPreAllocLoadStoreOpt() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    StringRef getPassName() const override {
      return ARM_PREALLOC_LOAD_STORE_OPT_NAME;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

  private:
    bool CanFormLdStDWord(MachineInstr *Op0, MachineInstr *Op1, DebugLoc &dl,
                          unsigned &NewOpc, Register &EvenReg, Register &OddReg,
                          Register &BaseReg, int &Offset, Register &PredReg,
                          ARMCC::CondCodes &Pred, bool &isT2);
    bool RescheduleOps(
        MachineBasicBlock *MBB, SmallVectorImpl<MachineInstr *> &Ops,
        unsigned Base, bool isLd, DenseMap<MachineInstr *, unsigned> &MI2LocMap,
        SmallDenseMap<Register, SmallVector<MachineInstr *>, 8> &RegisterMap);
    bool RescheduleLoadStoreInstrs(MachineBasicBlock *MBB);
    bool DistributeIncrements();
    bool DistributeIncrements(Register Base);
  };

} // end anonymous namespace

char ARMPreAllocLoadStoreOpt::ID = 0;

INITIALIZE_PASS_BEGIN(ARMPreAllocLoadStoreOpt, "arm-prera-ldst-opt",
                      ARM_PREALLOC_LOAD_STORE_OPT_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(ARMPreAllocLoadStoreOpt, "arm-prera-ldst-opt",
                    ARM_PREALLOC_LOAD_STORE_OPT_NAME, false, false)

// Limit the number of instructions to be rescheduled.
// FIXME: tune this limit, and/or come up with some better heuristics.
static cl::opt<unsigned> InstReorderLimit("arm-prera-ldst-opt-reorder-limit",
                                          cl::init(8), cl::Hidden);

bool ARMPreAllocLoadStoreOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (AssumeMisalignedLoadStores || skipFunction(Fn.getFunction()))
    return false;

  TD = &Fn.getDataLayout();
  STI = &Fn.getSubtarget<ARMSubtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  MRI = &Fn.getRegInfo();
  DT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MF  = &Fn;
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  bool Modified = DistributeIncrements();
  for (MachineBasicBlock &MFI : Fn)
    Modified |= RescheduleLoadStoreInstrs(&MFI);

  return Modified;
}

static bool IsSafeAndProfitableToMove(bool isLd, unsigned Base,
                                      MachineBasicBlock::iterator I,
                                      MachineBasicBlock::iterator E,
                                      SmallPtrSetImpl<MachineInstr*> &MemOps,
                                      SmallSet<unsigned, 4> &MemRegs,
                                      const TargetRegisterInfo *TRI,
                                      AliasAnalysis *AA) {
  // Are there stores / loads / calls between them?
  SmallSet<unsigned, 4> AddedRegPressure;
  while (++I != E) {
    if (I->isDebugInstr() || MemOps.count(&*I))
      continue;
    if (I->isCall() || I->isTerminator() || I->hasUnmodeledSideEffects())
      return false;
    if (I->mayStore() || (!isLd && I->mayLoad()))
      for (MachineInstr *MemOp : MemOps)
        if (I->mayAlias(AA, *MemOp, /*UseTBAA*/ false))
          return false;
    for (unsigned j = 0, NumOps = I->getNumOperands(); j != NumOps; ++j) {
      MachineOperand &MO = I->getOperand(j);
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (MO.isDef() && TRI->regsOverlap(Reg, Base))
        return false;
      if (Reg != Base && !MemRegs.count(Reg))
        AddedRegPressure.insert(Reg);
    }
  }

  // Estimate register pressure increase due to the transformation.
  if (MemRegs.size() <= 4)
    // Ok if we are moving small number of instructions.
    return true;
  return AddedRegPressure.size() <= MemRegs.size() * 2;
}

bool ARMPreAllocLoadStoreOpt::CanFormLdStDWord(
    MachineInstr *Op0, MachineInstr *Op1, DebugLoc &dl, unsigned &NewOpc,
    Register &FirstReg, Register &SecondReg, Register &BaseReg, int &Offset,
    Register &PredReg, ARMCC::CondCodes &Pred, bool &isT2) {
  // Make sure we're allowed to generate LDRD/STRD.
  if (!STI->hasV5TEOps())
    return false;

  // FIXME: VLDRS / VSTRS -> VLDRD / VSTRD
  unsigned Scale = 1;
  unsigned Opcode = Op0->getOpcode();
  if (Opcode == ARM::LDRi12) {
    NewOpc = ARM::LDRD;
  } else if (Opcode == ARM::STRi12) {
    NewOpc = ARM::STRD;
  } else if (Opcode == ARM::t2LDRi8 || Opcode == ARM::t2LDRi12) {
    NewOpc = ARM::t2LDRDi8;
    Scale = 4;
    isT2 = true;
  } else if (Opcode == ARM::t2STRi8 || Opcode == ARM::t2STRi12) {
    NewOpc = ARM::t2STRDi8;
    Scale = 4;
    isT2 = true;
  } else {
    return false;
  }

  // Make sure the base address satisfies i64 ld / st alignment requirement.
  // At the moment, we ignore the memoryoperand's value.
  // If we want to use AliasAnalysis, we should check it accordingly.
  if (!Op0->hasOneMemOperand() ||
      (*Op0->memoperands_begin())->isVolatile() ||
      (*Op0->memoperands_begin())->isAtomic())
    return false;

  Align Alignment = (*Op0->memoperands_begin())->getAlign();
  Align ReqAlign = STI->getDualLoadStoreAlignment();
  if (Alignment < ReqAlign)
    return false;

  // Then make sure the immediate offset fits.
  int OffImm = getMemoryOpOffset(*Op0);
  if (isT2) {
    int Limit = (1 << 8) * Scale;
    if (OffImm >= Limit || (OffImm <= -Limit) || (OffImm & (Scale-1)))
      return false;
    Offset = OffImm;
  } else {
    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (OffImm < 0) {
      AddSub = ARM_AM::sub;
      OffImm = - OffImm;
    }
    int Limit = (1 << 8) * Scale;
    if (OffImm >= Limit || (OffImm & (Scale-1)))
      return false;
    Offset = ARM_AM::getAM3Opc(AddSub, OffImm);
  }
  FirstReg = Op0->getOperand(0).getReg();
  SecondReg = Op1->getOperand(0).getReg();
  if (FirstReg == SecondReg)
    return false;
  BaseReg = Op0->getOperand(1).getReg();
  Pred = getInstrPredicate(*Op0, PredReg);
  dl = Op0->getDebugLoc();
  return true;
}

bool ARMPreAllocLoadStoreOpt::RescheduleOps(
    MachineBasicBlock *MBB, SmallVectorImpl<MachineInstr *> &Ops, unsigned Base,
    bool isLd, DenseMap<MachineInstr *, unsigned> &MI2LocMap,
    SmallDenseMap<Register, SmallVector<MachineInstr *>, 8> &RegisterMap) {
  bool RetVal = false;

  // Sort by offset (in reverse order).
  llvm::sort(Ops, [](const MachineInstr *LHS, const MachineInstr *RHS) {
    int LOffset = getMemoryOpOffset(*LHS);
    int ROffset = getMemoryOpOffset(*RHS);
    assert(LHS == RHS || LOffset != ROffset);
    return LOffset > ROffset;
  });

  // The loads / stores of the same base are in order. Scan them from first to
  // last and check for the following:
  // 1. Any def of base.
  // 2. Any gaps.
  while (Ops.size() > 1) {
    unsigned FirstLoc = ~0U;
    unsigned LastLoc = 0;
    MachineInstr *FirstOp = nullptr;
    MachineInstr *LastOp = nullptr;
    int LastOffset = 0;
    unsigned LastOpcode = 0;
    unsigned LastBytes = 0;
    unsigned NumMove = 0;
    for (MachineInstr *Op : llvm::reverse(Ops)) {
      // Make sure each operation has the same kind.
      unsigned LSMOpcode
        = getLoadStoreMultipleOpcode(Op->getOpcode(), ARM_AM::ia);
      if (LastOpcode && LSMOpcode != LastOpcode)
        break;

      // Check that we have a continuous set of offsets.
      int Offset = getMemoryOpOffset(*Op);
      unsigned Bytes = getLSMultipleTransferSize(Op);
      if (LastBytes) {
        if (Bytes != LastBytes || Offset != (LastOffset + (int)Bytes))
          break;
      }

      // Don't try to reschedule too many instructions.
      if (NumMove == InstReorderLimit)
        break;

      // Found a mergable instruction; save information about it.
      ++NumMove;
      LastOffset = Offset;
      LastBytes = Bytes;
      LastOpcode = LSMOpcode;

      unsigned Loc = MI2LocMap[Op];
      if (Loc <= FirstLoc) {
        FirstLoc = Loc;
        FirstOp = Op;
      }
      if (Loc >= LastLoc) {
        LastLoc = Loc;
        LastOp = Op;
      }
    }

    if (NumMove <= 1)
      Ops.pop_back();
    else {
      SmallPtrSet<MachineInstr*, 4> MemOps;
      SmallSet<unsigned, 4> MemRegs;
      for (size_t i = Ops.size() - NumMove, e = Ops.size(); i != e; ++i) {
        MemOps.insert(Ops[i]);
        MemRegs.insert(Ops[i]->getOperand(0).getReg());
      }

      // Be conservative, if the instructions are too far apart, don't
      // move them. We want to limit the increase of register pressure.
      bool DoMove = (LastLoc - FirstLoc) <= NumMove*4; // FIXME: Tune this.
      if (DoMove)
        DoMove = IsSafeAndProfitableToMove(isLd, Base, FirstOp, LastOp,
                                           MemOps, MemRegs, TRI, AA);
      if (!DoMove) {
        for (unsigned i = 0; i != NumMove; ++i)
          Ops.pop_back();
      } else {
        // This is the new location for the loads / stores.
        MachineBasicBlock::iterator InsertPos = isLd ? FirstOp : LastOp;
        while (InsertPos != MBB->end() &&
               (MemOps.count(&*InsertPos) || InsertPos->isDebugInstr()))
          ++InsertPos;

        // If we are moving a pair of loads / stores, see if it makes sense
        // to try to allocate a pair of registers that can form register pairs.
        MachineInstr *Op0 = Ops.back();
        MachineInstr *Op1 = Ops[Ops.size()-2];
        Register FirstReg, SecondReg;
        Register BaseReg, PredReg;
        ARMCC::CondCodes Pred = ARMCC::AL;
        bool isT2 = false;
        unsigned NewOpc = 0;
        int Offset = 0;
        DebugLoc dl;
        if (NumMove == 2 && CanFormLdStDWord(Op0, Op1, dl, NewOpc,
                                             FirstReg, SecondReg, BaseReg,
                                             Offset, PredReg, Pred, isT2)) {
          Ops.pop_back();
          Ops.pop_back();

          const MCInstrDesc &MCID = TII->get(NewOpc);
          const TargetRegisterClass *TRC = TII->getRegClass(MCID, 0, TRI, *MF);
          MRI->constrainRegClass(FirstReg, TRC);
          MRI->constrainRegClass(SecondReg, TRC);

          // Form the pair instruction.
          if (isLd) {
            MachineInstrBuilder MIB = BuildMI(*MBB, InsertPos, dl, MCID)
              .addReg(FirstReg, RegState::Define)
              .addReg(SecondReg, RegState::Define)
              .addReg(BaseReg);
            // FIXME: We're converting from LDRi12 to an insn that still
            // uses addrmode2, so we need an explicit offset reg. It should
            // always by reg0 since we're transforming LDRi12s.
            if (!isT2)
              MIB.addReg(0);
            MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
            MIB.cloneMergedMemRefs({Op0, Op1});
            LLVM_DEBUG(dbgs() << "Formed " << *MIB << "\n");
            ++NumLDRDFormed;
          } else {
            MachineInstrBuilder MIB = BuildMI(*MBB, InsertPos, dl, MCID)
              .addReg(FirstReg)
              .addReg(SecondReg)
              .addReg(BaseReg);
            // FIXME: We're converting from LDRi12 to an insn that still
            // uses addrmode2, so we need an explicit offset reg. It should
            // always by reg0 since we're transforming STRi12s.
            if (!isT2)
              MIB.addReg(0);
            MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
            MIB.cloneMergedMemRefs({Op0, Op1});
            LLVM_DEBUG(dbgs() << "Formed " << *MIB << "\n");
            ++NumSTRDFormed;
          }
          MBB->erase(Op0);
          MBB->erase(Op1);

          if (!isT2) {
            // Add register allocation hints to form register pairs.
            MRI->setRegAllocationHint(FirstReg, ARMRI::RegPairEven, SecondReg);
            MRI->setRegAllocationHint(SecondReg,  ARMRI::RegPairOdd, FirstReg);
          }
        } else {
          for (unsigned i = 0; i != NumMove; ++i) {
            MachineInstr *Op = Ops.pop_back_val();
            if (isLd) {
              // Populate RegisterMap with all Registers defined by loads.
              Register Reg = Op->getOperand(0).getReg();
              RegisterMap[Reg];
            }

            MBB->splice(InsertPos, MBB, Op);
          }
        }

        NumLdStMoved += NumMove;
        RetVal = true;
      }
    }
  }

  return RetVal;
}

static void forEachDbgRegOperand(MachineInstr *MI,
                                 std::function<void(MachineOperand &)> Fn) {
  if (MI->isNonListDebugValue()) {
    auto &Op = MI->getOperand(0);
    if (Op.isReg())
      Fn(Op);
  } else {
    for (unsigned I = 2; I < MI->getNumOperands(); I++) {
      auto &Op = MI->getOperand(I);
      if (Op.isReg())
        Fn(Op);
    }
  }
}

// Update the RegisterMap with the instruction that was moved because a
// DBG_VALUE_LIST may need to be moved again.
static void updateRegisterMapForDbgValueListAfterMove(
    SmallDenseMap<Register, SmallVector<MachineInstr *>, 8> &RegisterMap,
    MachineInstr *DbgValueListInstr, MachineInstr *InstrToReplace) {

  forEachDbgRegOperand(DbgValueListInstr, [&](MachineOperand &Op) {
    auto RegIt = RegisterMap.find(Op.getReg());
    if (RegIt == RegisterMap.end())
      return;
    auto &InstrVec = RegIt->getSecond();
    for (unsigned I = 0; I < InstrVec.size(); I++)
      if (InstrVec[I] == InstrToReplace)
        InstrVec[I] = DbgValueListInstr;
  });
}

static DebugVariable createDebugVariableFromMachineInstr(MachineInstr *MI) {
  auto DbgVar = DebugVariable(MI->getDebugVariable(), MI->getDebugExpression(),
                              MI->getDebugLoc()->getInlinedAt());
  return DbgVar;
}

bool
ARMPreAllocLoadStoreOpt::RescheduleLoadStoreInstrs(MachineBasicBlock *MBB) {
  bool RetVal = false;

  DenseMap<MachineInstr*, unsigned> MI2LocMap;
  using MapIt = DenseMap<unsigned, SmallVector<MachineInstr *, 4>>::iterator;
  using Base2InstMap = DenseMap<unsigned, SmallVector<MachineInstr *, 4>>;
  using BaseVec = SmallVector<unsigned, 4>;
  Base2InstMap Base2LdsMap;
  Base2InstMap Base2StsMap;
  BaseVec LdBases;
  BaseVec StBases;
  // This map is used to track the relationship between the virtual
  // register that is the result of a load that is moved and the DBG_VALUE
  // MachineInstr pointer that uses that virtual register.
  SmallDenseMap<Register, SmallVector<MachineInstr *>, 8> RegisterMap;

  unsigned Loc = 0;
  MachineBasicBlock::iterator MBBI = MBB->begin();
  MachineBasicBlock::iterator E = MBB->end();
  while (MBBI != E) {
    for (; MBBI != E; ++MBBI) {
      MachineInstr &MI = *MBBI;
      if (MI.isCall() || MI.isTerminator()) {
        // Stop at barriers.
        ++MBBI;
        break;
      }

      if (!MI.isDebugInstr())
        MI2LocMap[&MI] = ++Loc;

      if (!isMemoryOp(MI))
        continue;
      Register PredReg;
      if (getInstrPredicate(MI, PredReg) != ARMCC::AL)
        continue;

      int Opc = MI.getOpcode();
      bool isLd = isLoadSingle(Opc);
      Register Base = MI.getOperand(1).getReg();
      int Offset = getMemoryOpOffset(MI);
      bool StopHere = false;
      auto FindBases = [&] (Base2InstMap &Base2Ops, BaseVec &Bases) {
        MapIt BI = Base2Ops.find(Base);
        if (BI == Base2Ops.end()) {
          Base2Ops[Base].push_back(&MI);
          Bases.push_back(Base);
          return;
        }
        for (const MachineInstr *MI : BI->second) {
          if (Offset == getMemoryOpOffset(*MI)) {
            StopHere = true;
            break;
          }
        }
        if (!StopHere)
          BI->second.push_back(&MI);
      };

      if (isLd)
        FindBases(Base2LdsMap, LdBases);
      else
        FindBases(Base2StsMap, StBases);

      if (StopHere) {
        // Found a duplicate (a base+offset combination that's seen earlier).
        // Backtrack.
        --Loc;
        break;
      }
    }

    // Re-schedule loads.
    for (unsigned Base : LdBases) {
      SmallVectorImpl<MachineInstr *> &Lds = Base2LdsMap[Base];
      if (Lds.size() > 1)
        RetVal |= RescheduleOps(MBB, Lds, Base, true, MI2LocMap, RegisterMap);
    }

    // Re-schedule stores.
    for (unsigned Base : StBases) {
      SmallVectorImpl<MachineInstr *> &Sts = Base2StsMap[Base];
      if (Sts.size() > 1)
        RetVal |= RescheduleOps(MBB, Sts, Base, false, MI2LocMap, RegisterMap);
    }

    if (MBBI != E) {
      Base2LdsMap.clear();
      Base2StsMap.clear();
      LdBases.clear();
      StBases.clear();
    }
  }

  // Reschedule DBG_VALUEs to match any loads that were moved. When a load is
  // sunk beyond a DBG_VALUE that is referring to it, the DBG_VALUE becomes a
  // use-before-def, resulting in a loss of debug info.

  // Example:
  // Before the Pre Register Allocation Load Store Pass
  // inst_a
  // %2 = ld ...
  // inst_b
  // DBG_VALUE %2, "x", ...
  // %3 = ld ...

  // After the Pass:
  // inst_a
  // inst_b
  // DBG_VALUE %2, "x", ...
  // %2 = ld ...
  // %3 = ld ...

  // The code below addresses this by moving the DBG_VALUE to the position
  // immediately after the load.

  // Example:
  // After the code below:
  // inst_a
  // inst_b
  // %2 = ld ...
  // DBG_VALUE %2, "x", ...
  // %3 = ld ...

  // The algorithm works in two phases: First RescheduleOps() populates the
  // RegisterMap with registers that were moved as keys, there is no value
  // inserted. In the next phase, every MachineInstr in a basic block is
  // iterated over. If it is a valid DBG_VALUE or DBG_VALUE_LIST and it uses one
  // or more registers in the RegisterMap, the RegisterMap and InstrMap are
  // populated with the MachineInstr. If the DBG_VALUE or DBG_VALUE_LIST
  // describes debug information for a variable that already exists in the
  // DbgValueSinkCandidates, the MachineInstr in the DbgValueSinkCandidates must
  // be set to undef. If the current MachineInstr is a load that was moved,
  // undef the corresponding DBG_VALUE or DBG_VALUE_LIST and clone it to below
  // the load.

  // To illustrate the above algorithm visually let's take this example.

  // Before the Pre Register Allocation Load Store Pass:
  // %2 = ld ...
  // DBG_VALUE %2, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // %3 = ld ...
  // DBG_VALUE %3, A, ..., # Z
  // %4 = ld ...

  // After Pre Register Allocation Load Store Pass:
  // DBG_VALUE %2, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE %3, A, ..., # Z
  // %2 = ld ...
  // %3 = ld ...
  // %4 = ld ...

  // The algorithm below does the following:

  // In the beginning, the RegisterMap will have been populated with the virtual
  // registers %2, and %3, the DbgValueSinkCandidates and the InstrMap will be
  // empty. DbgValueSinkCandidates = {}, RegisterMap = {2 -> {}, 3 -> {}},
  // InstrMap {}
  // -> DBG_VALUE %2, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE %3, A, ..., # Z
  // %2 = ld ...
  // %3 = ld ...
  // %4 = ld ...

  // After the first DBG_VALUE (denoted with an X) is processed, the
  // DbgValueSinkCandidates and InstrMap will be populated and the RegisterMap
  // entry for %2 will be populated as well. DbgValueSinkCandidates = {A -> X},
  // RegisterMap = {2 -> {X}, 3 -> {}}, InstrMap {X -> 2}
  // DBG_VALUE %2, A, .... # X
  // -> DBG_VALUE 0, A, ... # Y
  // DBG_VALUE %3, A, ..., # Z
  // %2 = ld ...
  // %3 = ld ...
  // %4 = ld ...

  // After the DBG_VALUE Y is processed, the DbgValueSinkCandidates is updated
  // to now hold Y for A and the RegisterMap is also updated to remove X from
  // %2, this is because both X and Y describe the same debug variable A. X is
  // also updated to have a $noreg as the first operand.
  // DbgValueSinkCandidates = {A -> {Y}}, RegisterMap = {2 -> {}, 3 -> {}},
  // InstrMap = {X-> 2}
  // DBG_VALUE $noreg, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // -> DBG_VALUE %3, A, ..., # Z
  // %2 = ld ...
  // %3 = ld ...
  // %4 = ld ...

  // After DBG_VALUE Z is processed, the DbgValueSinkCandidates is updated to
  // hold Z fr A, the RegisterMap is updated to hold Z for %3, and the InstrMap
  // is updated to have Z mapped to %3. This is again because Z describes the
  // debug variable A, Y is not updated to have $noreg as first operand because
  // its first operand is an immediate, not a register.
  // DbgValueSinkCandidates = {A -> {Z}}, RegisterMap = {2 -> {}, 3 -> {Z}},
  // InstrMap = {X -> 2, Z -> 3}
  // DBG_VALUE $noreg, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE %3, A, ..., # Z
  // -> %2 = ld ...
  // %3 = ld ...
  // %4 = ld ...

  // Nothing happens here since the RegisterMap for %2 contains no value.
  // DbgValueSinkCandidates = {A -> {Z}}, RegisterMap = {2 -> {}, 3 -> {Z}},
  // InstrMap = {X -> 2, Z -> 3}
  // DBG_VALUE $noreg, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE %3, A, ..., # Z
  // %2 = ld ...
  // -> %3 = ld ...
  // %4 = ld ...

  // Since the RegisterMap contains Z as a value for %3, the MachineInstr
  // pointer Z is copied to come after the load for %3 and the old Z's first
  // operand is changed to $noreg the Basic Block iterator is moved to after the
  // DBG_VALUE Z's new position.
  // DbgValueSinkCandidates = {A -> {Z}}, RegisterMap = {2 -> {}, 3 -> {Z}},
  // InstrMap = {X -> 2, Z -> 3}
  // DBG_VALUE $noreg, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE $noreg, A, ..., # Old Z
  // %2 = ld ...
  // %3 = ld ...
  // DBG_VALUE %3, A, ..., # Z
  // -> %4 = ld ...

  // Nothing happens for %4 and the algorithm exits having processed the entire
  // Basic Block.
  // DbgValueSinkCandidates = {A -> {Z}}, RegisterMap = {2 -> {}, 3 -> {Z}},
  // InstrMap = {X -> 2, Z -> 3}
  // DBG_VALUE $noreg, A, .... # X
  // DBG_VALUE 0, A, ... # Y
  // DBG_VALUE $noreg, A, ..., # Old Z
  // %2 = ld ...
  // %3 = ld ...
  // DBG_VALUE %3, A, ..., # Z
  // %4 = ld ...

  // This map is used to track the relationship between
  // a Debug Variable and the DBG_VALUE MachineInstr pointer that describes the
  // debug information for that Debug Variable.
  SmallDenseMap<DebugVariable, MachineInstr *, 8> DbgValueSinkCandidates;
  // This map is used to track the relationship between a DBG_VALUE or
  // DBG_VALUE_LIST MachineInstr pointer and Registers that it uses.
  SmallDenseMap<MachineInstr *, SmallVector<Register>, 8> InstrMap;
  for (MBBI = MBB->begin(), E = MBB->end(); MBBI != E; ++MBBI) {
    MachineInstr &MI = *MBBI;

    auto PopulateRegisterAndInstrMapForDebugInstr = [&](Register Reg) {
      auto RegIt = RegisterMap.find(Reg);
      if (RegIt == RegisterMap.end())
        return;
      auto &InstrVec = RegIt->getSecond();
      InstrVec.push_back(&MI);
      InstrMap[&MI].push_back(Reg);
    };

    if (MI.isDebugValue()) {
      assert(MI.getDebugVariable() &&
             "DBG_VALUE or DBG_VALUE_LIST must contain a DILocalVariable");

      auto DbgVar = createDebugVariableFromMachineInstr(&MI);
      // If the first operand is a register and it exists in the RegisterMap, we
      // know this is a DBG_VALUE that uses the result of a load that was moved,
      // and is therefore a candidate to also be moved, add it to the
      // RegisterMap and InstrMap.
      forEachDbgRegOperand(&MI, [&](MachineOperand &Op) {
        PopulateRegisterAndInstrMapForDebugInstr(Op.getReg());
      });

      // If the current DBG_VALUE describes the same variable as one of the
      // in-flight DBG_VALUEs, remove the candidate from the list and set it to
      // undef. Moving one DBG_VALUE past another would result in the variable's
      // value going back in time when stepping through the block in the
      // debugger.
      auto InstrIt = DbgValueSinkCandidates.find(DbgVar);
      if (InstrIt != DbgValueSinkCandidates.end()) {
        auto *Instr = InstrIt->getSecond();
        auto RegIt = InstrMap.find(Instr);
        if (RegIt != InstrMap.end()) {
          const auto &RegVec = RegIt->getSecond();
          // For every Register in the RegVec, remove the MachineInstr in the
          // RegisterMap that describes the DbgVar.
          for (auto &Reg : RegVec) {
            auto RegIt = RegisterMap.find(Reg);
            if (RegIt == RegisterMap.end())
              continue;
            auto &InstrVec = RegIt->getSecond();
            auto IsDbgVar = [&](MachineInstr *I) -> bool {
              auto Var = createDebugVariableFromMachineInstr(I);
              return Var == DbgVar;
            };

            llvm::erase_if(InstrVec, IsDbgVar);
          }
          forEachDbgRegOperand(Instr,
                               [&](MachineOperand &Op) { Op.setReg(0); });
        }
      }
      DbgValueSinkCandidates[DbgVar] = &MI;
    } else {
      // If the first operand of a load matches with a DBG_VALUE in RegisterMap,
      // then move that DBG_VALUE to below the load.
      auto Opc = MI.getOpcode();
      if (!isLoadSingle(Opc))
        continue;
      auto Reg = MI.getOperand(0).getReg();
      auto RegIt = RegisterMap.find(Reg);
      if (RegIt == RegisterMap.end())
        continue;
      auto &DbgInstrVec = RegIt->getSecond();
      if (!DbgInstrVec.size())
        continue;
      for (auto *DbgInstr : DbgInstrVec) {
        MachineBasicBlock::iterator InsertPos = std::next(MBBI);
        auto *ClonedMI = MI.getMF()->CloneMachineInstr(DbgInstr);
        MBB->insert(InsertPos, ClonedMI);
        MBBI++;
        //  Erase the entry into the DbgValueSinkCandidates for the DBG_VALUE
        //  that was moved.
        auto DbgVar = createDebugVariableFromMachineInstr(DbgInstr);
        auto DbgIt = DbgValueSinkCandidates.find(DbgVar);
        // If the instruction is a DBG_VALUE_LIST, it may have already been
        // erased from the DbgValueSinkCandidates. Only erase if it exists in
        // the DbgValueSinkCandidates.
        if (DbgIt != DbgValueSinkCandidates.end())
          DbgValueSinkCandidates.erase(DbgIt);
        // Zero out original dbg instr
        forEachDbgRegOperand(DbgInstr,
                             [&](MachineOperand &Op) { Op.setReg(0); });
        // Update RegisterMap with ClonedMI because it might have to be moved
        // again.
        if (DbgInstr->isDebugValueList())
          updateRegisterMapForDbgValueListAfterMove(RegisterMap, ClonedMI,
                                                    DbgInstr);
      }
    }
  }
  return RetVal;
}

// Get the Base register operand index from the memory access MachineInst if we
// should attempt to distribute postinc on it. Return -1 if not of a valid
// instruction type. If it returns an index, it is assumed that instruction is a
// r+i indexing mode, and getBaseOperandIndex() + 1 is the Offset index.
static int getBaseOperandIndex(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case ARM::MVE_VLDRBS16:
  case ARM::MVE_VLDRBS32:
  case ARM::MVE_VLDRBU16:
  case ARM::MVE_VLDRBU32:
  case ARM::MVE_VLDRHS32:
  case ARM::MVE_VLDRHU32:
  case ARM::MVE_VLDRBU8:
  case ARM::MVE_VLDRHU16:
  case ARM::MVE_VLDRWU32:
  case ARM::MVE_VSTRB16:
  case ARM::MVE_VSTRB32:
  case ARM::MVE_VSTRH32:
  case ARM::MVE_VSTRBU8:
  case ARM::MVE_VSTRHU16:
  case ARM::MVE_VSTRWU32:
  case ARM::t2LDRHi8:
  case ARM::t2LDRHi12:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSHi12:
  case ARM::t2LDRBi8:
  case ARM::t2LDRBi12:
  case ARM::t2LDRSBi8:
  case ARM::t2LDRSBi12:
  case ARM::t2STRBi8:
  case ARM::t2STRBi12:
  case ARM::t2STRHi8:
  case ARM::t2STRHi12:
    return 1;
  case ARM::MVE_VLDRBS16_post:
  case ARM::MVE_VLDRBS32_post:
  case ARM::MVE_VLDRBU16_post:
  case ARM::MVE_VLDRBU32_post:
  case ARM::MVE_VLDRHS32_post:
  case ARM::MVE_VLDRHU32_post:
  case ARM::MVE_VLDRBU8_post:
  case ARM::MVE_VLDRHU16_post:
  case ARM::MVE_VLDRWU32_post:
  case ARM::MVE_VSTRB16_post:
  case ARM::MVE_VSTRB32_post:
  case ARM::MVE_VSTRH32_post:
  case ARM::MVE_VSTRBU8_post:
  case ARM::MVE_VSTRHU16_post:
  case ARM::MVE_VSTRWU32_post:
  case ARM::MVE_VLDRBS16_pre:
  case ARM::MVE_VLDRBS32_pre:
  case ARM::MVE_VLDRBU16_pre:
  case ARM::MVE_VLDRBU32_pre:
  case ARM::MVE_VLDRHS32_pre:
  case ARM::MVE_VLDRHU32_pre:
  case ARM::MVE_VLDRBU8_pre:
  case ARM::MVE_VLDRHU16_pre:
  case ARM::MVE_VLDRWU32_pre:
  case ARM::MVE_VSTRB16_pre:
  case ARM::MVE_VSTRB32_pre:
  case ARM::MVE_VSTRH32_pre:
  case ARM::MVE_VSTRBU8_pre:
  case ARM::MVE_VSTRHU16_pre:
  case ARM::MVE_VSTRWU32_pre:
    return 2;
  }
  return -1;
}

static bool isPostIndex(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case ARM::MVE_VLDRBS16_post:
  case ARM::MVE_VLDRBS32_post:
  case ARM::MVE_VLDRBU16_post:
  case ARM::MVE_VLDRBU32_post:
  case ARM::MVE_VLDRHS32_post:
  case ARM::MVE_VLDRHU32_post:
  case ARM::MVE_VLDRBU8_post:
  case ARM::MVE_VLDRHU16_post:
  case ARM::MVE_VLDRWU32_post:
  case ARM::MVE_VSTRB16_post:
  case ARM::MVE_VSTRB32_post:
  case ARM::MVE_VSTRH32_post:
  case ARM::MVE_VSTRBU8_post:
  case ARM::MVE_VSTRHU16_post:
  case ARM::MVE_VSTRWU32_post:
    return true;
  }
  return false;
}

static bool isPreIndex(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case ARM::MVE_VLDRBS16_pre:
  case ARM::MVE_VLDRBS32_pre:
  case ARM::MVE_VLDRBU16_pre:
  case ARM::MVE_VLDRBU32_pre:
  case ARM::MVE_VLDRHS32_pre:
  case ARM::MVE_VLDRHU32_pre:
  case ARM::MVE_VLDRBU8_pre:
  case ARM::MVE_VLDRHU16_pre:
  case ARM::MVE_VLDRWU32_pre:
  case ARM::MVE_VSTRB16_pre:
  case ARM::MVE_VSTRB32_pre:
  case ARM::MVE_VSTRH32_pre:
  case ARM::MVE_VSTRBU8_pre:
  case ARM::MVE_VSTRHU16_pre:
  case ARM::MVE_VSTRWU32_pre:
    return true;
  }
  return false;
}

// Given a memory access Opcode, check that the give Imm would be a valid Offset
// for this instruction (same as isLegalAddressImm), Or if the instruction
// could be easily converted to one where that was valid. For example converting
// t2LDRi12 to t2LDRi8 for negative offsets. Works in conjunction with
// AdjustBaseAndOffset below.
static bool isLegalOrConvertableAddressImm(unsigned Opcode, int Imm,
                                           const TargetInstrInfo *TII,
                                           int &CodesizeEstimate) {
  if (isLegalAddressImm(Opcode, Imm, TII))
    return true;

  // We can convert AddrModeT2_i12 to AddrModeT2_i8neg.
  const MCInstrDesc &Desc = TII->get(Opcode);
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  switch (AddrMode) {
  case ARMII::AddrModeT2_i12:
    CodesizeEstimate += 1;
    return Imm < 0 && -Imm < ((1 << 8) * 1);
  }
  return false;
}

// Given an MI adjust its address BaseReg to use NewBaseReg and address offset
// by -Offset. This can either happen in-place or be a replacement as MI is
// converted to another instruction type.
static void AdjustBaseAndOffset(MachineInstr *MI, Register NewBaseReg,
                                int Offset, const TargetInstrInfo *TII,
                                const TargetRegisterInfo *TRI) {
  // Set the Base reg
  unsigned BaseOp = getBaseOperandIndex(*MI);
  MI->getOperand(BaseOp).setReg(NewBaseReg);
  // and constrain the reg class to that required by the instruction.
  MachineFunction *MF = MI->getMF();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const MCInstrDesc &MCID = TII->get(MI->getOpcode());
  const TargetRegisterClass *TRC = TII->getRegClass(MCID, BaseOp, TRI, *MF);
  MRI.constrainRegClass(NewBaseReg, TRC);

  int OldOffset = MI->getOperand(BaseOp + 1).getImm();
  if (isLegalAddressImm(MI->getOpcode(), OldOffset - Offset, TII))
    MI->getOperand(BaseOp + 1).setImm(OldOffset - Offset);
  else {
    unsigned ConvOpcode;
    switch (MI->getOpcode()) {
    case ARM::t2LDRHi12:
      ConvOpcode = ARM::t2LDRHi8;
      break;
    case ARM::t2LDRSHi12:
      ConvOpcode = ARM::t2LDRSHi8;
      break;
    case ARM::t2LDRBi12:
      ConvOpcode = ARM::t2LDRBi8;
      break;
    case ARM::t2LDRSBi12:
      ConvOpcode = ARM::t2LDRSBi8;
      break;
    case ARM::t2STRHi12:
      ConvOpcode = ARM::t2STRHi8;
      break;
    case ARM::t2STRBi12:
      ConvOpcode = ARM::t2STRBi8;
      break;
    default:
      llvm_unreachable("Unhandled convertable opcode");
    }
    assert(isLegalAddressImm(ConvOpcode, OldOffset - Offset, TII) &&
           "Illegal Address Immediate after convert!");

    const MCInstrDesc &MCID = TII->get(ConvOpcode);
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), MCID)
        .add(MI->getOperand(0))
        .add(MI->getOperand(1))
        .addImm(OldOffset - Offset)
        .add(MI->getOperand(3))
        .add(MI->getOperand(4))
        .cloneMemRefs(*MI);
    MI->eraseFromParent();
  }
}

static MachineInstr *createPostIncLoadStore(MachineInstr *MI, int Offset,
                                            Register NewReg,
                                            const TargetInstrInfo *TII,
                                            const TargetRegisterInfo *TRI) {
  MachineFunction *MF = MI->getMF();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  unsigned NewOpcode = getPostIndexedLoadStoreOpcode(
      MI->getOpcode(), Offset > 0 ? ARM_AM::add : ARM_AM::sub);

  const MCInstrDesc &MCID = TII->get(NewOpcode);
  // Constrain the def register class
  const TargetRegisterClass *TRC = TII->getRegClass(MCID, 0, TRI, *MF);
  MRI.constrainRegClass(NewReg, TRC);
  // And do the same for the base operand
  TRC = TII->getRegClass(MCID, 2, TRI, *MF);
  MRI.constrainRegClass(MI->getOperand(1).getReg(), TRC);

  unsigned AddrMode = (MCID.TSFlags & ARMII::AddrModeMask);
  switch (AddrMode) {
  case ARMII::AddrModeT2_i7:
  case ARMII::AddrModeT2_i7s2:
  case ARMII::AddrModeT2_i7s4:
    // Any MVE load/store
    return BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), MCID)
        .addReg(NewReg, RegState::Define)
        .add(MI->getOperand(0))
        .add(MI->getOperand(1))
        .addImm(Offset)
        .add(MI->getOperand(3))
        .add(MI->getOperand(4))
        .add(MI->getOperand(5))
        .cloneMemRefs(*MI);
  case ARMII::AddrModeT2_i8:
    if (MI->mayLoad()) {
      return BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), MCID)
          .add(MI->getOperand(0))
          .addReg(NewReg, RegState::Define)
          .add(MI->getOperand(1))
          .addImm(Offset)
          .add(MI->getOperand(3))
          .add(MI->getOperand(4))
          .cloneMemRefs(*MI);
    } else {
      return BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), MCID)
          .addReg(NewReg, RegState::Define)
          .add(MI->getOperand(0))
          .add(MI->getOperand(1))
          .addImm(Offset)
          .add(MI->getOperand(3))
          .add(MI->getOperand(4))
          .cloneMemRefs(*MI);
    }
  default:
    llvm_unreachable("Unhandled createPostIncLoadStore");
  }
}

// Given a Base Register, optimise the load/store uses to attempt to create more
// post-inc accesses and less register moves. We do this by taking zero offset
// loads/stores with an add, and convert them to a postinc load/store of the
// same type. Any subsequent accesses will be adjusted to use and account for
// the post-inc value.
// For example:
// LDR #0            LDR_POSTINC #16
// LDR #4            LDR #-12
// LDR #8            LDR #-8
// LDR #12           LDR #-4
// ADD #16
//
// At the same time if we do not find an increment but do find an existing
// pre/post inc instruction, we can still adjust the offsets of subsequent
// instructions to save the register move that would otherwise be needed for the
// in-place increment.
bool ARMPreAllocLoadStoreOpt::DistributeIncrements(Register Base) {
  // We are looking for:
  // One zero offset load/store that can become postinc
  MachineInstr *BaseAccess = nullptr;
  MachineInstr *PrePostInc = nullptr;
  // An increment that can be folded in
  MachineInstr *Increment = nullptr;
  // Other accesses after BaseAccess that will need to be updated to use the
  // postinc value.
  SmallPtrSet<MachineInstr *, 8> OtherAccesses;
  for (auto &Use : MRI->use_nodbg_instructions(Base)) {
    if (!Increment && getAddSubImmediate(Use) != 0) {
      Increment = &Use;
      continue;
    }

    int BaseOp = getBaseOperandIndex(Use);
    if (BaseOp == -1)
      return false;

    if (!Use.getOperand(BaseOp).isReg() ||
        Use.getOperand(BaseOp).getReg() != Base)
      return false;
    if (isPreIndex(Use) || isPostIndex(Use))
      PrePostInc = &Use;
    else if (Use.getOperand(BaseOp + 1).getImm() == 0)
      BaseAccess = &Use;
    else
      OtherAccesses.insert(&Use);
  }

  int IncrementOffset;
  Register NewBaseReg;
  if (BaseAccess && Increment) {
    if (PrePostInc || BaseAccess->getParent() != Increment->getParent())
      return false;
    Register PredReg;
    if (Increment->definesRegister(ARM::CPSR, /*TRI=*/nullptr) ||
        getInstrPredicate(*Increment, PredReg) != ARMCC::AL)
      return false;

    LLVM_DEBUG(dbgs() << "\nAttempting to distribute increments on VirtualReg "
                      << Base.virtRegIndex() << "\n");

    // Make sure that Increment has no uses before BaseAccess that are not PHI
    // uses.
    for (MachineInstr &Use :
        MRI->use_nodbg_instructions(Increment->getOperand(0).getReg())) {
      if (&Use == BaseAccess || (Use.getOpcode() != TargetOpcode::PHI &&
                                 !DT->dominates(BaseAccess, &Use))) {
        LLVM_DEBUG(dbgs() << "  BaseAccess doesn't dominate use of increment\n");
        return false;
      }
    }

    // Make sure that Increment can be folded into Base
    IncrementOffset = getAddSubImmediate(*Increment);
    unsigned NewPostIncOpcode = getPostIndexedLoadStoreOpcode(
        BaseAccess->getOpcode(), IncrementOffset > 0 ? ARM_AM::add : ARM_AM::sub);
    if (!isLegalAddressImm(NewPostIncOpcode, IncrementOffset, TII)) {
      LLVM_DEBUG(dbgs() << "  Illegal addressing mode immediate on postinc\n");
      return false;
    }
  }
  else if (PrePostInc) {
    // If we already have a pre/post index load/store then set BaseAccess,
    // IncrementOffset and NewBaseReg to the values it already produces,
    // allowing us to update and subsequent uses of BaseOp reg with the
    // incremented value.
    if (Increment)
      return false;

    LLVM_DEBUG(dbgs() << "\nAttempting to distribute increments on already "
                      << "indexed VirtualReg " << Base.virtRegIndex() << "\n");
    int BaseOp = getBaseOperandIndex(*PrePostInc);
    IncrementOffset = PrePostInc->getOperand(BaseOp+1).getImm();
    BaseAccess = PrePostInc;
    NewBaseReg = PrePostInc->getOperand(0).getReg();
  }
  else
    return false;

  // And make sure that the negative value of increment can be added to all
  // other offsets after the BaseAccess. We rely on either
  // dominates(BaseAccess, OtherAccess) or dominates(OtherAccess, BaseAccess)
  // to keep things simple.
  // This also adds a simple codesize metric, to detect if an instruction (like
  // t2LDRBi12) which can often be shrunk to a thumb1 instruction (tLDRBi)
  // cannot because it is converted to something else (t2LDRBi8). We start this
  // at -1 for the gain from removing the increment.
  SmallPtrSet<MachineInstr *, 4> SuccessorAccesses;
  int CodesizeEstimate = -1;
  for (auto *Use : OtherAccesses) {
    if (DT->dominates(BaseAccess, Use)) {
      SuccessorAccesses.insert(Use);
      unsigned BaseOp = getBaseOperandIndex(*Use);
      if (!isLegalOrConvertableAddressImm(Use->getOpcode(),
                                          Use->getOperand(BaseOp + 1).getImm() -
                                              IncrementOffset,
                                          TII, CodesizeEstimate)) {
        LLVM_DEBUG(dbgs() << "  Illegal addressing mode immediate on use\n");
        return false;
      }
    } else if (!DT->dominates(Use, BaseAccess)) {
      LLVM_DEBUG(
          dbgs() << "  Unknown dominance relation between Base and Use\n");
      return false;
    }
  }
  if (STI->hasMinSize() && CodesizeEstimate > 0) {
    LLVM_DEBUG(dbgs() << "  Expected to grow instructions under minsize\n");
    return false;
  }

  if (!PrePostInc) {
    // Replace BaseAccess with a post inc
    LLVM_DEBUG(dbgs() << "Changing: "; BaseAccess->dump());
    LLVM_DEBUG(dbgs() << "  And   : "; Increment->dump());
    NewBaseReg = Increment->getOperand(0).getReg();
    MachineInstr *BaseAccessPost =
        createPostIncLoadStore(BaseAccess, IncrementOffset, NewBaseReg, TII, TRI);
    BaseAccess->eraseFromParent();
    Increment->eraseFromParent();
    (void)BaseAccessPost;
    LLVM_DEBUG(dbgs() << "  To    : "; BaseAccessPost->dump());
  }

  for (auto *Use : SuccessorAccesses) {
    LLVM_DEBUG(dbgs() << "Changing: "; Use->dump());
    AdjustBaseAndOffset(Use, NewBaseReg, IncrementOffset, TII, TRI);
    LLVM_DEBUG(dbgs() << "  To    : "; Use->dump());
  }

  // Remove the kill flag from all uses of NewBaseReg, in case any old uses
  // remain.
  for (MachineOperand &Op : MRI->use_nodbg_operands(NewBaseReg))
    Op.setIsKill(false);
  return true;
}

bool ARMPreAllocLoadStoreOpt::DistributeIncrements() {
  bool Changed = false;
  SmallSetVector<Register, 4> Visited;
  for (auto &MBB : *MF) {
    for (auto &MI : MBB) {
      int BaseOp = getBaseOperandIndex(MI);
      if (BaseOp == -1 || !MI.getOperand(BaseOp).isReg())
        continue;

      Register Base = MI.getOperand(BaseOp).getReg();
      if (!Base.isVirtual() || Visited.count(Base))
        continue;

      Visited.insert(Base);
    }
  }

  for (auto Base : Visited)
    Changed |= DistributeIncrements(Base);

  return Changed;
}

/// Returns an instance of the load / store optimization pass.
FunctionPass *llvm::createARMLoadStoreOptimizationPass(bool PreAlloc) {
  if (PreAlloc)
    return new ARMPreAllocLoadStoreOpt();
  return new ARMLoadStoreOpt();
}
