//===--------------------- RegisterFile.cpp ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a register mapping file class.  This class is responsible
/// for managing hardware register files and the tracking of data dependencies
/// between registers.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

const unsigned WriteRef::INVALID_IID = std::numeric_limits<unsigned>::max();

WriteRef::WriteRef(unsigned SourceIndex, WriteState *WS)
    : IID(SourceIndex), WriteBackCycle(), WriteResID(), RegisterID(),
      Write(WS) {}

void WriteRef::commit() {
  assert(Write && Write->isExecuted() && "Cannot commit before write back!");
  RegisterID = Write->getRegisterID();
  WriteResID = Write->getWriteResourceID();
  Write = nullptr;
}

void WriteRef::notifyExecuted(unsigned Cycle) {
  assert(Write && Write->isExecuted() && "Not executed!");
  WriteBackCycle = Cycle;
}

bool WriteRef::hasKnownWriteBackCycle() const {
  return isValid() && (!Write || Write->isExecuted());
}

bool WriteRef::isWriteZero() const {
  assert(isValid() && "Invalid null WriteState found!");
  return getWriteState()->isWriteZero();
}

unsigned WriteRef::getWriteResourceID() const {
  if (Write)
    return Write->getWriteResourceID();
  return WriteResID;
}

MCPhysReg WriteRef::getRegisterID() const {
  if (Write)
    return Write->getRegisterID();
  return RegisterID;
}

RegisterFile::RegisterFile(const MCSchedModel &SM, const MCRegisterInfo &mri,
                           unsigned NumRegs)
    : MRI(mri),
      RegisterMappings(mri.getNumRegs(), {WriteRef(), RegisterRenamingInfo()}),
      ZeroRegisters(mri.getNumRegs(), false), CurrentCycle() {
  initialize(SM, NumRegs);
}

void RegisterFile::initialize(const MCSchedModel &SM, unsigned NumRegs) {
  // Create a default register file that "sees" all the machine registers
  // declared by the target. The number of physical registers in the default
  // register file is set equal to `NumRegs`. A value of zero for `NumRegs`
  // means: this register file has an unbounded number of physical registers.
  RegisterFiles.emplace_back(NumRegs);
  if (!SM.hasExtraProcessorInfo())
    return;

  // For each user defined register file, allocate a RegisterMappingTracker
  // object. The size of every register file, as well as the mapping between
  // register files and register classes is specified via tablegen.
  const MCExtraProcessorInfo &Info = SM.getExtraProcessorInfo();

  // Skip invalid register file at index 0.
  for (unsigned I = 1, E = Info.NumRegisterFiles; I < E; ++I) {
    const MCRegisterFileDesc &RF = Info.RegisterFiles[I];
    assert(RF.NumPhysRegs && "Invalid PRF with zero physical registers!");

    // The cost of a register definition is equivalent to the number of
    // physical registers that are allocated at register renaming stage.
    unsigned Length = RF.NumRegisterCostEntries;
    const MCRegisterCostEntry *FirstElt =
        &Info.RegisterCostTable[RF.RegisterCostEntryIdx];
    addRegisterFile(RF, ArrayRef<MCRegisterCostEntry>(FirstElt, Length));
  }
}

void RegisterFile::cycleStart() {
  for (RegisterMappingTracker &RMT : RegisterFiles)
    RMT.NumMoveEliminated = 0;
}

void RegisterFile::onInstructionExecuted(Instruction *IS) {
  assert(IS && IS->isExecuted() && "Unexpected internal state found!");
  for (WriteState &WS : IS->getDefs()) {
    if (WS.isEliminated())
      return;

    MCPhysReg RegID = WS.getRegisterID();

    // This allows InstrPostProcess to remove register Defs
    // by setting their RegisterID to 0.
    if (!RegID)
      continue;

    assert(WS.getCyclesLeft() != UNKNOWN_CYCLES &&
           "The number of cycles should be known at this point!");
    assert(WS.getCyclesLeft() <= 0 && "Invalid cycles left for this write!");

    MCPhysReg RenameAs = RegisterMappings[RegID].second.RenameAs;
    if (RenameAs && RenameAs != RegID)
      RegID = RenameAs;

    WriteRef &WR = RegisterMappings[RegID].first;
    if (WR.getWriteState() == &WS)
      WR.notifyExecuted(CurrentCycle);

    for (MCPhysReg I : MRI.subregs(RegID)) {
      WriteRef &OtherWR = RegisterMappings[I].first;
      if (OtherWR.getWriteState() == &WS)
        OtherWR.notifyExecuted(CurrentCycle);
    }

    if (!WS.clearsSuperRegisters())
      continue;

    for (MCPhysReg I : MRI.superregs(RegID)) {
      WriteRef &OtherWR = RegisterMappings[I].first;
      if (OtherWR.getWriteState() == &WS)
        OtherWR.notifyExecuted(CurrentCycle);
    }
  }
}

void RegisterFile::addRegisterFile(const MCRegisterFileDesc &RF,
                                   ArrayRef<MCRegisterCostEntry> Entries) {
  // A default register file is always allocated at index #0. That register file
  // is mainly used to count the total number of mappings created by all
  // register files at runtime. Users can limit the number of available physical
  // registers in register file #0 through the command line flag
  // `-register-file-size`.
  unsigned RegisterFileIndex = RegisterFiles.size();
  RegisterFiles.emplace_back(RF.NumPhysRegs, RF.MaxMovesEliminatedPerCycle,
                             RF.AllowZeroMoveEliminationOnly);

  // Special case where there is no register class identifier in the set.
  // An empty set of register classes means: this register file contains all
  // the physical registers specified by the target.
  // We optimistically assume that a register can be renamed at the cost of a
  // single physical register. The constructor of RegisterFile ensures that
  // a RegisterMapping exists for each logical register defined by the Target.
  if (Entries.empty())
    return;

  // Now update the cost of individual registers.
  for (const MCRegisterCostEntry &RCE : Entries) {
    const MCRegisterClass &RC = MRI.getRegClass(RCE.RegisterClassID);
    for (const MCPhysReg Reg : RC) {
      RegisterRenamingInfo &Entry = RegisterMappings[Reg].second;
      IndexPlusCostPairTy &IPC = Entry.IndexPlusCost;
      if (IPC.first && IPC.first != RegisterFileIndex) {
        // The only register file that is allowed to overlap is the default
        // register file at index #0. The analysis is inaccurate if register
        // files overlap.
        errs() << "warning: register " << MRI.getName(Reg)
               << " defined in multiple register files.";
      }
      IPC = std::make_pair(RegisterFileIndex, RCE.Cost);
      Entry.RenameAs = Reg;
      Entry.AllowMoveElimination = RCE.AllowMoveElimination;

      // Assume the same cost for each sub-register.
      for (MCPhysReg I : MRI.subregs(Reg)) {
        RegisterRenamingInfo &OtherEntry = RegisterMappings[I].second;
        if (!OtherEntry.IndexPlusCost.first &&
            (!OtherEntry.RenameAs ||
             MRI.isSuperRegister(I, OtherEntry.RenameAs))) {
          OtherEntry.IndexPlusCost = IPC;
          OtherEntry.RenameAs = Reg;
        }
      }
    }
  }
}

void RegisterFile::allocatePhysRegs(const RegisterRenamingInfo &Entry,
                                    MutableArrayRef<unsigned> UsedPhysRegs) {
  unsigned RegisterFileIndex = Entry.IndexPlusCost.first;
  unsigned Cost = Entry.IndexPlusCost.second;
  if (RegisterFileIndex) {
    RegisterMappingTracker &RMT = RegisterFiles[RegisterFileIndex];
    RMT.NumUsedPhysRegs += Cost;
    UsedPhysRegs[RegisterFileIndex] += Cost;
  }

  // Now update the default register mapping tracker.
  RegisterFiles[0].NumUsedPhysRegs += Cost;
  UsedPhysRegs[0] += Cost;
}

void RegisterFile::freePhysRegs(const RegisterRenamingInfo &Entry,
                                MutableArrayRef<unsigned> FreedPhysRegs) {
  unsigned RegisterFileIndex = Entry.IndexPlusCost.first;
  unsigned Cost = Entry.IndexPlusCost.second;
  if (RegisterFileIndex) {
    RegisterMappingTracker &RMT = RegisterFiles[RegisterFileIndex];
    RMT.NumUsedPhysRegs -= Cost;
    FreedPhysRegs[RegisterFileIndex] += Cost;
  }

  // Now update the default register mapping tracker.
  RegisterFiles[0].NumUsedPhysRegs -= Cost;
  FreedPhysRegs[0] += Cost;
}

void RegisterFile::addRegisterWrite(WriteRef Write,
                                    MutableArrayRef<unsigned> UsedPhysRegs) {
  WriteState &WS = *Write.getWriteState();
  MCPhysReg RegID = WS.getRegisterID();

  // This allows InstrPostProcess to remove register Defs
  // by setting their RegisterID to 0.
  if (!RegID)
    return;

  LLVM_DEBUG({
    dbgs() << "[PRF] addRegisterWrite [ " << Write.getSourceIndex() << ", "
           << MRI.getName(RegID) << "]\n";
  });

  // If RenameAs is equal to RegID, then RegID is subject to register renaming
  // and false dependencies on RegID are all eliminated.

  // If RenameAs references the invalid register, then we optimistically assume
  // that it can be renamed. In the absence of tablegen descriptors for register
  // files, RenameAs is always set to the invalid register ID.  In all other
  // cases, RenameAs must be either equal to RegID, or it must reference a
  // super-register of RegID.

  // If RenameAs is a super-register of RegID, then a write to RegID has always
  // a false dependency on RenameAs. The only exception is for when the write
  // implicitly clears the upper portion of the underlying register.
  // If a write clears its super-registers, then it is renamed as `RenameAs`.
  bool IsWriteZero = WS.isWriteZero();
  bool IsEliminated = WS.isEliminated();
  bool ShouldAllocatePhysRegs = !IsWriteZero && !IsEliminated;
  const RegisterRenamingInfo &RRI = RegisterMappings[RegID].second;
  WS.setPRF(RRI.IndexPlusCost.first);

  if (RRI.RenameAs && RRI.RenameAs != RegID) {
    RegID = RRI.RenameAs;
    WriteRef &OtherWrite = RegisterMappings[RegID].first;

    if (!WS.clearsSuperRegisters()) {
      // The processor keeps the definition of `RegID` together with register
      // `RenameAs`. Since this partial write is not renamed, no physical
      // register is allocated.
      ShouldAllocatePhysRegs = false;

      WriteState *OtherWS = OtherWrite.getWriteState();
      if (OtherWS && (OtherWrite.getSourceIndex() != Write.getSourceIndex())) {
        // This partial write has a false dependency on RenameAs.
        assert(!IsEliminated && "Unexpected partial update!");
        OtherWS->addUser(OtherWrite.getSourceIndex(), &WS);
      }
    }
  }

  // Update zero registers.
  MCPhysReg ZeroRegisterID =
      WS.clearsSuperRegisters() ? RegID : WS.getRegisterID();
  ZeroRegisters.setBitVal(ZeroRegisterID, IsWriteZero);
  for (MCPhysReg I : MRI.subregs(ZeroRegisterID))
    ZeroRegisters.setBitVal(I, IsWriteZero);

  // If this move has been eliminated, then method tryEliminateMoveOrSwap should
  // have already updated all the register mappings.
  if (!IsEliminated) {
    // Check if this is one of multiple writes performed by this
    // instruction to register RegID.
    const WriteRef &OtherWrite = RegisterMappings[RegID].first;
    const WriteState *OtherWS = OtherWrite.getWriteState();
    if (OtherWS && OtherWrite.getSourceIndex() == Write.getSourceIndex()) {
      if (OtherWS->getLatency() > WS.getLatency()) {
        // Conservatively keep the slowest write on RegID.
        if (ShouldAllocatePhysRegs)
          allocatePhysRegs(RegisterMappings[RegID].second, UsedPhysRegs);
        return;
      }
    }

    // Update the mapping for register RegID including its sub-registers.
    RegisterMappings[RegID].first = Write;
    RegisterMappings[RegID].second.AliasRegID = 0U;
    for (MCPhysReg I : MRI.subregs(RegID)) {
      RegisterMappings[I].first = Write;
      RegisterMappings[I].second.AliasRegID = 0U;
    }

    // No physical registers are allocated for instructions that are optimized
    // in hardware. For example, zero-latency data-dependency breaking
    // instructions don't consume physical registers.
    if (ShouldAllocatePhysRegs)
      allocatePhysRegs(RegisterMappings[RegID].second, UsedPhysRegs);
  }

  if (!WS.clearsSuperRegisters())
    return;

  for (MCPhysReg I : MRI.superregs(RegID)) {
    if (!IsEliminated) {
      RegisterMappings[I].first = Write;
      RegisterMappings[I].second.AliasRegID = 0U;
    }

    ZeroRegisters.setBitVal(I, IsWriteZero);
  }
}

void RegisterFile::removeRegisterWrite(
    const WriteState &WS, MutableArrayRef<unsigned> FreedPhysRegs) {
  // Early exit if this write was eliminated. A write eliminated at register
  // renaming stage generates an alias, and it is not added to the PRF.
  if (WS.isEliminated())
    return;

  MCPhysReg RegID = WS.getRegisterID();

  // This allows InstrPostProcess to remove register Defs
  // by setting their RegisterID to 0.
  if (!RegID)
    return;

  assert(WS.getCyclesLeft() != UNKNOWN_CYCLES &&
         "Invalidating a write of unknown cycles!");
  assert(WS.getCyclesLeft() <= 0 && "Invalid cycles left for this write!");

  bool ShouldFreePhysRegs = !WS.isWriteZero();
  MCPhysReg RenameAs = RegisterMappings[RegID].second.RenameAs;
  if (RenameAs && RenameAs != RegID) {
    RegID = RenameAs;

    if (!WS.clearsSuperRegisters()) {
      // Keep the definition of `RegID` together with register `RenameAs`.
      ShouldFreePhysRegs = false;
    }
  }

  if (ShouldFreePhysRegs)
    freePhysRegs(RegisterMappings[RegID].second, FreedPhysRegs);

  WriteRef &WR = RegisterMappings[RegID].first;
  if (WR.getWriteState() == &WS)
    WR.commit();

  for (MCPhysReg I : MRI.subregs(RegID)) {
    WriteRef &OtherWR = RegisterMappings[I].first;
    if (OtherWR.getWriteState() == &WS)
      OtherWR.commit();
  }

  if (!WS.clearsSuperRegisters())
    return;

  for (MCPhysReg I : MRI.superregs(RegID)) {
    WriteRef &OtherWR = RegisterMappings[I].first;
    if (OtherWR.getWriteState() == &WS)
      OtherWR.commit();
  }
}

bool RegisterFile::canEliminateMove(const WriteState &WS, const ReadState &RS,
                                    unsigned RegisterFileIndex) const {
  const RegisterMapping &RMFrom = RegisterMappings[RS.getRegisterID()];
  const RegisterMapping &RMTo = RegisterMappings[WS.getRegisterID()];
  const RegisterMappingTracker &RMT = RegisterFiles[RegisterFileIndex];

  // From and To must be owned by the PRF at index `RegisterFileIndex`.
  const RegisterRenamingInfo &RRIFrom = RMFrom.second;
  if (RRIFrom.IndexPlusCost.first != RegisterFileIndex)
    return false;

  const RegisterRenamingInfo &RRITo = RMTo.second;
  if (RRITo.IndexPlusCost.first != RegisterFileIndex)
    return false;

  // Early exit if the destination register is from a register class that
  // doesn't allow move elimination.
  if (!RegisterMappings[RRITo.RenameAs].second.AllowMoveElimination)
    return false;

  // We only allow move elimination for writes that update a full physical
  // register. On X86, move elimination is possible with 32-bit general purpose
  // registers because writes to those registers are not partial writes.  If a
  // register move is a partial write, then we conservatively assume that move
  // elimination fails, since it would either trigger a partial update, or the
  // issue of a merge opcode.
  //
  // Note that this constraint may be lifted in future.  For example, we could
  // make this model more flexible, and let users customize the set of registers
  // (i.e. register classes) that allow move elimination.
  //
  // For now, we assume that there is a strong correlation between registers
  // that allow move elimination, and how those same registers are renamed in
  // hardware.
  if (RRITo.RenameAs && RRITo.RenameAs != WS.getRegisterID())
    if (!WS.clearsSuperRegisters())
      return false;

  bool IsZeroMove = ZeroRegisters[RS.getRegisterID()];
  return (!RMT.AllowZeroMoveEliminationOnly || IsZeroMove);
}

bool RegisterFile::tryEliminateMoveOrSwap(MutableArrayRef<WriteState> Writes,
                                          MutableArrayRef<ReadState> Reads) {
  if (Writes.size() != Reads.size())
    return false;

  // This logic assumes that writes and reads are contributed by a register move
  // or a register swap operation. In particular, it assumes a simple register
  // move if there is only one write.  It assumes a swap operation if there are
  // exactly two writes.
  if (Writes.empty() || Writes.size() > 2)
    return false;

  // All registers must be owned by the same PRF.
  const RegisterRenamingInfo &RRInfo =
      RegisterMappings[Writes[0].getRegisterID()].second;
  unsigned RegisterFileIndex = RRInfo.IndexPlusCost.first;
  RegisterMappingTracker &RMT = RegisterFiles[RegisterFileIndex];

  // Early exit if the PRF cannot eliminate more moves/xchg in this cycle.
  if (RMT.MaxMoveEliminatedPerCycle &&
      (RMT.NumMoveEliminated + Writes.size()) > RMT.MaxMoveEliminatedPerCycle)
    return false;

  for (size_t I = 0, E = Writes.size(); I < E; ++I) {
    const ReadState &RS = Reads[I];
    const WriteState &WS = Writes[E - (I + 1)];
    if (!canEliminateMove(WS, RS, RegisterFileIndex))
      return false;
  }

  for (size_t I = 0, E = Writes.size(); I < E; ++I) {
    ReadState &RS = Reads[I];
    WriteState &WS = Writes[E - (I + 1)];

    const RegisterMapping &RMFrom = RegisterMappings[RS.getRegisterID()];
    const RegisterMapping &RMTo = RegisterMappings[WS.getRegisterID()];
    const RegisterRenamingInfo &RRIFrom = RMFrom.second;
    const RegisterRenamingInfo &RRITo = RMTo.second;

    // Construct an alias.
    MCPhysReg AliasedReg =
        RRIFrom.RenameAs ? RRIFrom.RenameAs : RS.getRegisterID();
    MCPhysReg AliasReg = RRITo.RenameAs ? RRITo.RenameAs : WS.getRegisterID();

    const RegisterRenamingInfo &RMAlias = RegisterMappings[AliasedReg].second;
    if (RMAlias.AliasRegID)
      AliasedReg = RMAlias.AliasRegID;

    RegisterMappings[AliasReg].second.AliasRegID = AliasedReg;
    for (MCPhysReg I : MRI.subregs(AliasReg))
      RegisterMappings[I].second.AliasRegID = AliasedReg;

    if (ZeroRegisters[RS.getRegisterID()]) {
      WS.setWriteZero();
      RS.setReadZero();
    }

    WS.setEliminated();
    RMT.NumMoveEliminated++;
  }

  return true;
}

unsigned WriteRef::getWriteBackCycle() const {
  assert(hasKnownWriteBackCycle() && "Instruction not executed!");
  assert((!Write || Write->getCyclesLeft() <= 0) &&
         "Inconsistent state found!");
  return WriteBackCycle;
}

unsigned RegisterFile::getElapsedCyclesFromWriteBack(const WriteRef &WR) const {
  assert(WR.hasKnownWriteBackCycle() && "Write hasn't been committed yet!");
  return CurrentCycle - WR.getWriteBackCycle();
}

void RegisterFile::collectWrites(
    const MCSubtargetInfo &STI, const ReadState &RS,
    SmallVectorImpl<WriteRef> &Writes,
    SmallVectorImpl<WriteRef> &CommittedWrites) const {
  const ReadDescriptor &RD = RS.getDescriptor();
  const MCSchedModel &SM = STI.getSchedModel();
  const MCSchedClassDesc *SC = SM.getSchedClassDesc(RD.SchedClassID);
  MCPhysReg RegID = RS.getRegisterID();
  assert(RegID && RegID < RegisterMappings.size());
  LLVM_DEBUG(dbgs() << "[PRF] collecting writes for register "
                    << MRI.getName(RegID) << '\n');

  // Check if this is an alias.
  const RegisterRenamingInfo &RRI = RegisterMappings[RegID].second;
  if (RRI.AliasRegID)
    RegID = RRI.AliasRegID;

  const WriteRef &WR = RegisterMappings[RegID].first;
  if (WR.getWriteState()) {
    Writes.push_back(WR);
  } else if (WR.hasKnownWriteBackCycle()) {
    unsigned WriteResID = WR.getWriteResourceID();
    int ReadAdvance = STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);
    if (ReadAdvance < 0) {
      unsigned Elapsed = getElapsedCyclesFromWriteBack(WR);
      if (Elapsed < static_cast<unsigned>(-ReadAdvance))
        CommittedWrites.push_back(WR);
    }
  }

  // Handle potential partial register updates.
  for (MCPhysReg I : MRI.subregs(RegID)) {
    const WriteRef &WR = RegisterMappings[I].first;
    if (WR.getWriteState()) {
      Writes.push_back(WR);
    } else if (WR.hasKnownWriteBackCycle()) {
      unsigned WriteResID = WR.getWriteResourceID();
      int ReadAdvance = STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);
      if (ReadAdvance < 0) {
        unsigned Elapsed = getElapsedCyclesFromWriteBack(WR);
        if (Elapsed < static_cast<unsigned>(-ReadAdvance))
          CommittedWrites.push_back(WR);
      }
    }
  }

  // Remove duplicate entries and resize the input vector.
  if (Writes.size() > 1) {
    sort(Writes, [](const WriteRef &Lhs, const WriteRef &Rhs) {
      return Lhs.getWriteState() < Rhs.getWriteState();
    });
    auto It = llvm::unique(Writes);
    Writes.resize(std::distance(Writes.begin(), It));
  }

  LLVM_DEBUG({
    for (const WriteRef &WR : Writes) {
      const WriteState &WS = *WR.getWriteState();
      dbgs() << "[PRF] Found a dependent use of Register "
             << MRI.getName(WS.getRegisterID()) << " (defined by instruction #"
             << WR.getSourceIndex() << ")\n";
    }
  });
}

RegisterFile::RAWHazard
RegisterFile::checkRAWHazards(const MCSubtargetInfo &STI,
                              const ReadState &RS) const {
  RAWHazard Hazard;
  SmallVector<WriteRef, 4> Writes;
  SmallVector<WriteRef, 4> CommittedWrites;

  const MCSchedModel &SM = STI.getSchedModel();
  const ReadDescriptor &RD = RS.getDescriptor();
  const MCSchedClassDesc *SC = SM.getSchedClassDesc(RD.SchedClassID);

  collectWrites(STI, RS, Writes, CommittedWrites);
  for (const WriteRef &WR : Writes) {
    const WriteState *WS = WR.getWriteState();
    unsigned WriteResID = WS->getWriteResourceID();
    int ReadAdvance = STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);

    if (WS->getCyclesLeft() == UNKNOWN_CYCLES) {
      if (Hazard.isValid())
        continue;

      Hazard.RegisterID = WR.getRegisterID();
      Hazard.CyclesLeft = UNKNOWN_CYCLES;
      continue;
    }

    int CyclesLeft = WS->getCyclesLeft() - ReadAdvance;
    if (CyclesLeft > 0) {
      if (Hazard.CyclesLeft < CyclesLeft) {
        Hazard.RegisterID = WR.getRegisterID();
        Hazard.CyclesLeft = CyclesLeft;
      }
    }
  }
  Writes.clear();

  for (const WriteRef &WR : CommittedWrites) {
    unsigned WriteResID = WR.getWriteResourceID();
    int NegReadAdvance = -STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);
    int Elapsed = static_cast<int>(getElapsedCyclesFromWriteBack(WR));
    int CyclesLeft = NegReadAdvance - Elapsed;
    assert(CyclesLeft > 0 && "Write should not be in the CommottedWrites set!");
    if (Hazard.CyclesLeft < CyclesLeft) {
      Hazard.RegisterID = WR.getRegisterID();
      Hazard.CyclesLeft = CyclesLeft;
    }
  }

  return Hazard;
}

void RegisterFile::addRegisterRead(ReadState &RS,
                                   const MCSubtargetInfo &STI) const {
  MCPhysReg RegID = RS.getRegisterID();
  const RegisterRenamingInfo &RRI = RegisterMappings[RegID].second;
  RS.setPRF(RRI.IndexPlusCost.first);
  if (RS.isIndependentFromDef())
    return;

  if (ZeroRegisters[RS.getRegisterID()])
    RS.setReadZero();

  SmallVector<WriteRef, 4> DependentWrites;
  SmallVector<WriteRef, 4> CompletedWrites;
  collectWrites(STI, RS, DependentWrites, CompletedWrites);
  RS.setDependentWrites(DependentWrites.size() + CompletedWrites.size());

  // We know that this read depends on all the writes in DependentWrites.
  // For each write, check if we have ReadAdvance information, and use it
  // to figure out in how many cycles this read will be available.
  const ReadDescriptor &RD = RS.getDescriptor();
  const MCSchedModel &SM = STI.getSchedModel();
  const MCSchedClassDesc *SC = SM.getSchedClassDesc(RD.SchedClassID);
  for (WriteRef &WR : DependentWrites) {
    unsigned WriteResID = WR.getWriteResourceID();
    WriteState &WS = *WR.getWriteState();
    int ReadAdvance = STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID);
    WS.addUser(WR.getSourceIndex(), &RS, ReadAdvance);
  }

  for (WriteRef &WR : CompletedWrites) {
    unsigned WriteResID = WR.getWriteResourceID();
    assert(WR.hasKnownWriteBackCycle() && "Invalid write!");
    assert(STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID) < 0);
    unsigned ReadAdvance = static_cast<unsigned>(
        -STI.getReadAdvanceCycles(SC, RD.UseIndex, WriteResID));
    unsigned Elapsed = getElapsedCyclesFromWriteBack(WR);
    assert(Elapsed < ReadAdvance && "Should not have been added to the set!");
    RS.writeStartEvent(WR.getSourceIndex(), WR.getRegisterID(),
                       ReadAdvance - Elapsed);
  }
}

unsigned RegisterFile::isAvailable(ArrayRef<MCPhysReg> Regs) const {
  SmallVector<unsigned, 4> NumPhysRegs(getNumRegisterFiles());

  // Find how many new mappings must be created for each register file.
  for (const MCPhysReg RegID : Regs) {
    const RegisterRenamingInfo &RRI = RegisterMappings[RegID].second;
    const IndexPlusCostPairTy &Entry = RRI.IndexPlusCost;
    if (Entry.first)
      NumPhysRegs[Entry.first] += Entry.second;
    NumPhysRegs[0] += Entry.second;
  }

  unsigned Response = 0;
  for (unsigned I = 0, E = getNumRegisterFiles(); I < E; ++I) {
    unsigned NumRegs = NumPhysRegs[I];
    if (!NumRegs)
      continue;

    const RegisterMappingTracker &RMT = RegisterFiles[I];
    if (!RMT.NumPhysRegs) {
      // The register file has an unbounded number of microarchitectural
      // registers.
      continue;
    }

    if (RMT.NumPhysRegs < NumRegs) {
      // The current register file is too small. This may occur if the number of
      // microarchitectural registers in register file #0 was changed by the
      // users via flag -reg-file-size. Alternatively, the scheduling model
      // specified a too small number of registers for this register file.
      LLVM_DEBUG(
          dbgs() << "[PRF] Not enough registers in the register file.\n");

      // FIXME: Normalize the instruction register count to match the
      // NumPhysRegs value.  This is a highly unusual case, and is not expected
      // to occur.  This normalization is hiding an inconsistency in either the
      // scheduling model or in the value that the user might have specified
      // for NumPhysRegs.
      NumRegs = RMT.NumPhysRegs;
    }

    if (RMT.NumPhysRegs < (RMT.NumUsedPhysRegs + NumRegs))
      Response |= (1U << I);
  }

  return Response;
}

#ifndef NDEBUG
void WriteRef::dump() const {
  dbgs() << "IID=" << getSourceIndex() << ' ';
  if (isValid())
    getWriteState()->dump();
  else
    dbgs() << "(null)";
}

void RegisterFile::dump() const {
  for (unsigned I = 0, E = MRI.getNumRegs(); I < E; ++I) {
    const RegisterMapping &RM = RegisterMappings[I];
    const RegisterRenamingInfo &RRI = RM.second;
    if (ZeroRegisters[I]) {
      dbgs() << MRI.getName(I) << ", " << I
             << ", PRF=" << RRI.IndexPlusCost.first
             << ", Cost=" << RRI.IndexPlusCost.second
             << ", RenameAs=" << RRI.RenameAs << ", IsZero=" << ZeroRegisters[I]
             << ",";
      RM.first.dump();
      dbgs() << '\n';
    }
  }

  for (unsigned I = 0, E = getNumRegisterFiles(); I < E; ++I) {
    dbgs() << "Register File #" << I;
    const RegisterMappingTracker &RMT = RegisterFiles[I];
    dbgs() << "\n  TotalMappings:        " << RMT.NumPhysRegs
           << "\n  NumUsedMappings:      " << RMT.NumUsedPhysRegs << '\n';
  }
}
#endif

} // namespace mca
} // namespace llvm
