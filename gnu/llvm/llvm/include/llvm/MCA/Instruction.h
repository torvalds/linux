//===--------------------- Instruction.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines abstractions used by the Pipeline to model register reads,
/// register writes and instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INSTRUCTION_H
#define LLVM_MCA_INSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCRegister.h" // definition of MCPhysReg.
#include "llvm/Support/MathExtras.h"

#ifndef NDEBUG
#include "llvm/Support/raw_ostream.h"
#endif

#include <memory>

namespace llvm {

namespace mca {

constexpr int UNKNOWN_CYCLES = -512;

/// A representation of an mca::Instruction operand
/// for use in mca::CustomBehaviour.
class MCAOperand {
  // This class is mostly copied from MCOperand within
  // MCInst.h except that we don't keep track of
  // expressions or sub-instructions.
  enum MCAOperandType : unsigned char {
    kInvalid,   ///< Uninitialized, Relocatable immediate, or Sub-instruction.
    kRegister,  ///< Register operand.
    kImmediate, ///< Immediate operand.
    kSFPImmediate, ///< Single-floating-point immediate operand.
    kDFPImmediate, ///< Double-Floating-point immediate operand.
  };
  MCAOperandType Kind;

  union {
    unsigned RegVal;
    int64_t ImmVal;
    uint32_t SFPImmVal;
    uint64_t FPImmVal;
  };

  // We only store specific operands for specific instructions
  // so an instruction's operand 3 may be stored within the list
  // of MCAOperand as element 0. This Index attribute keeps track
  // of the original index (3 for this example).
  unsigned Index;

public:
  MCAOperand() : Kind(kInvalid), FPImmVal(), Index() {}

  bool isValid() const { return Kind != kInvalid; }
  bool isReg() const { return Kind == kRegister; }
  bool isImm() const { return Kind == kImmediate; }
  bool isSFPImm() const { return Kind == kSFPImmediate; }
  bool isDFPImm() const { return Kind == kDFPImmediate; }

  /// Returns the register number.
  unsigned getReg() const {
    assert(isReg() && "This is not a register operand!");
    return RegVal;
  }

  int64_t getImm() const {
    assert(isImm() && "This is not an immediate");
    return ImmVal;
  }

  uint32_t getSFPImm() const {
    assert(isSFPImm() && "This is not an SFP immediate");
    return SFPImmVal;
  }

  uint64_t getDFPImm() const {
    assert(isDFPImm() && "This is not an FP immediate");
    return FPImmVal;
  }

  void setIndex(const unsigned Idx) { Index = Idx; }

  unsigned getIndex() const { return Index; }

  static MCAOperand createReg(unsigned Reg) {
    MCAOperand Op;
    Op.Kind = kRegister;
    Op.RegVal = Reg;
    return Op;
  }

  static MCAOperand createImm(int64_t Val) {
    MCAOperand Op;
    Op.Kind = kImmediate;
    Op.ImmVal = Val;
    return Op;
  }

  static MCAOperand createSFPImm(uint32_t Val) {
    MCAOperand Op;
    Op.Kind = kSFPImmediate;
    Op.SFPImmVal = Val;
    return Op;
  }

  static MCAOperand createDFPImm(uint64_t Val) {
    MCAOperand Op;
    Op.Kind = kDFPImmediate;
    Op.FPImmVal = Val;
    return Op;
  }

  static MCAOperand createInvalid() {
    MCAOperand Op;
    Op.Kind = kInvalid;
    Op.FPImmVal = 0;
    return Op;
  }
};

/// A register write descriptor.
struct WriteDescriptor {
  // Operand index. The index is negative for implicit writes only.
  // For implicit writes, the actual operand index is computed performing
  // a bitwise not of the OpIndex.
  int OpIndex;
  // Write latency. Number of cycles before write-back stage.
  unsigned Latency;
  // This field is set to a value different than zero only if this
  // is an implicit definition.
  MCPhysReg RegisterID;
  // Instruction itineraries would set this field to the SchedClass ID.
  // Otherwise, it defaults to the WriteResourceID from the MCWriteLatencyEntry
  // element associated to this write.
  // When computing read latencies, this value is matched against the
  // "ReadAdvance" information. The hardware backend may implement
  // dedicated forwarding paths to quickly propagate write results to dependent
  // instructions waiting in the reservation station (effectively bypassing the
  // write-back stage).
  unsigned SClassOrWriteResourceID;
  // True only if this is a write obtained from an optional definition.
  // Optional definitions are allowed to reference regID zero (i.e. "no
  // register").
  bool IsOptionalDef;

  bool isImplicitWrite() const { return OpIndex < 0; };
};

/// A register read descriptor.
struct ReadDescriptor {
  // A MCOperand index. This is used by the Dispatch logic to identify register
  // reads. Implicit reads have negative indices. The actual operand index of an
  // implicit read is the bitwise not of field OpIndex.
  int OpIndex;
  // The actual "UseIdx". This is used to query the ReadAdvance table. Explicit
  // uses always come first in the sequence of uses.
  unsigned UseIndex;
  // This field is only set if this is an implicit read.
  MCPhysReg RegisterID;
  // Scheduling Class Index. It is used to query the scheduling model for the
  // MCSchedClassDesc object.
  unsigned SchedClassID;

  bool isImplicitRead() const { return OpIndex < 0; };
};

class ReadState;

/// A critical data dependency descriptor.
///
/// Field RegID is set to the invalid register for memory dependencies.
struct CriticalDependency {
  unsigned IID;
  MCPhysReg RegID;
  unsigned Cycles;
};

/// Tracks uses of a register definition (e.g. register write).
///
/// Each implicit/explicit register write is associated with an instance of
/// this class. A WriteState object tracks the dependent users of a
/// register write. It also tracks how many cycles are left before the write
/// back stage.
class WriteState {
  const WriteDescriptor *WD;
  // On instruction issue, this field is set equal to the write latency.
  // Before instruction issue, this field defaults to -512, a special
  // value that represents an "unknown" number of cycles.
  int CyclesLeft;

  // Actual register defined by this write. This field is only used
  // to speedup queries on the register file.
  // For implicit writes, this field always matches the value of
  // field RegisterID from WD.
  MCPhysReg RegisterID;

  // Physical register file that serves register RegisterID.
  unsigned PRFID;

  // True if this write implicitly clears the upper portion of RegisterID's
  // super-registers.
  bool ClearsSuperRegs;

  // True if this write is from a dependency breaking zero-idiom instruction.
  bool WritesZero;

  // True if this write has been eliminated at register renaming stage.
  // Example: a register move doesn't consume scheduler/pipleline resources if
  // it is eliminated at register renaming stage. It still consumes
  // decode bandwidth, and ROB entries.
  bool IsEliminated;

  // This field is set if this is a partial register write, and it has a false
  // dependency on any previous write of the same register (or a portion of it).
  // DependentWrite must be able to complete before this write completes, so
  // that we don't break the WAW, and the two writes can be merged together.
  const WriteState *DependentWrite;

  // A partial write that is in a false dependency with this write.
  WriteState *PartialWrite;
  unsigned DependentWriteCyclesLeft;

  // Critical register dependency for this write.
  CriticalDependency CRD;

  // A list of dependent reads. Users is a set of dependent
  // reads. A dependent read is added to the set only if CyclesLeft
  // is "unknown". As soon as CyclesLeft is 'known', each user in the set
  // gets notified with the actual CyclesLeft.

  // The 'second' element of a pair is a "ReadAdvance" number of cycles.
  SmallVector<std::pair<ReadState *, int>, 4> Users;

public:
  WriteState(const WriteDescriptor &Desc, MCPhysReg RegID,
             bool clearsSuperRegs = false, bool writesZero = false)
      : WD(&Desc), CyclesLeft(UNKNOWN_CYCLES), RegisterID(RegID), PRFID(0),
        ClearsSuperRegs(clearsSuperRegs), WritesZero(writesZero),
        IsEliminated(false), DependentWrite(nullptr), PartialWrite(nullptr),
        DependentWriteCyclesLeft(0), CRD() {}

  WriteState(const WriteState &Other) = default;
  WriteState &operator=(const WriteState &Other) = default;

  int getCyclesLeft() const { return CyclesLeft; }
  unsigned getWriteResourceID() const { return WD->SClassOrWriteResourceID; }
  MCPhysReg getRegisterID() const { return RegisterID; }
  void setRegisterID(const MCPhysReg RegID) { RegisterID = RegID; }
  unsigned getRegisterFileID() const { return PRFID; }
  unsigned getLatency() const { return WD->Latency; }
  unsigned getDependentWriteCyclesLeft() const {
    return DependentWriteCyclesLeft;
  }
  const WriteState *getDependentWrite() const { return DependentWrite; }
  const CriticalDependency &getCriticalRegDep() const { return CRD; }

  // This method adds Use to the set of data dependent reads. IID is the
  // instruction identifier associated with this write. ReadAdvance is the
  // number of cycles to subtract from the latency of this data dependency.
  // Use is in a RAW dependency with this write.
  void addUser(unsigned IID, ReadState *Use, int ReadAdvance);

  // Use is a younger register write that is in a false dependency with this
  // write. IID is the instruction identifier associated with this write.
  void addUser(unsigned IID, WriteState *Use);

  unsigned getNumUsers() const {
    unsigned NumUsers = Users.size();
    if (PartialWrite)
      ++NumUsers;
    return NumUsers;
  }

  bool clearsSuperRegisters() const { return ClearsSuperRegs; }
  bool isWriteZero() const { return WritesZero; }
  bool isEliminated() const { return IsEliminated; }

  bool isReady() const {
    if (DependentWrite)
      return false;
    unsigned CyclesLeft = getDependentWriteCyclesLeft();
    return !CyclesLeft || CyclesLeft < getLatency();
  }

  bool isExecuted() const {
    return CyclesLeft != UNKNOWN_CYCLES && CyclesLeft <= 0;
  }

  void setDependentWrite(const WriteState *Other) { DependentWrite = Other; }
  void writeStartEvent(unsigned IID, MCPhysReg RegID, unsigned Cycles);
  void setWriteZero() { WritesZero = true; }
  void setEliminated() {
    assert(Users.empty() && "Write is in an inconsistent state.");
    CyclesLeft = 0;
    IsEliminated = true;
  }

  void setPRF(unsigned PRF) { PRFID = PRF; }

  // On every cycle, update CyclesLeft and notify dependent users.
  void cycleEvent();
  void onInstructionIssued(unsigned IID);

#ifndef NDEBUG
  void dump() const;
#endif
};

/// Tracks register operand latency in cycles.
///
/// A read may be dependent on more than one write. This occurs when some
/// writes only partially update the register associated to this read.
class ReadState {
  const ReadDescriptor *RD;
  // Physical register identified associated to this read.
  MCPhysReg RegisterID;
  // Physical register file that serves register RegisterID.
  unsigned PRFID;
  // Number of writes that contribute to the definition of RegisterID.
  // In the absence of partial register updates, the number of DependentWrites
  // cannot be more than one.
  unsigned DependentWrites;
  // Number of cycles left before RegisterID can be read. This value depends on
  // the latency of all the dependent writes. It defaults to UNKNOWN_CYCLES.
  // It gets set to the value of field TotalCycles only when the 'CyclesLeft' of
  // every dependent write is known.
  int CyclesLeft;
  // This field is updated on every writeStartEvent(). When the number of
  // dependent writes (i.e. field DependentWrite) is zero, this value is
  // propagated to field CyclesLeft.
  unsigned TotalCycles;
  // Longest register dependency.
  CriticalDependency CRD;
  // This field is set to true only if there are no dependent writes, and
  // there are no `CyclesLeft' to wait.
  bool IsReady;
  // True if this is a read from a known zero register.
  bool IsZero;
  // True if this register read is from a dependency-breaking instruction.
  bool IndependentFromDef;

public:
  ReadState(const ReadDescriptor &Desc, MCPhysReg RegID)
      : RD(&Desc), RegisterID(RegID), PRFID(0), DependentWrites(0),
        CyclesLeft(UNKNOWN_CYCLES), TotalCycles(0), CRD(), IsReady(true),
        IsZero(false), IndependentFromDef(false) {}

  const ReadDescriptor &getDescriptor() const { return *RD; }
  unsigned getSchedClass() const { return RD->SchedClassID; }
  MCPhysReg getRegisterID() const { return RegisterID; }
  unsigned getRegisterFileID() const { return PRFID; }
  const CriticalDependency &getCriticalRegDep() const { return CRD; }

  bool isPending() const { return !IndependentFromDef && CyclesLeft > 0; }
  bool isReady() const { return IsReady; }
  bool isImplicitRead() const { return RD->isImplicitRead(); }

  bool isIndependentFromDef() const { return IndependentFromDef; }
  void setIndependentFromDef() { IndependentFromDef = true; }

  void cycleEvent();
  void writeStartEvent(unsigned IID, MCPhysReg RegID, unsigned Cycles);
  void setDependentWrites(unsigned Writes) {
    DependentWrites = Writes;
    IsReady = !Writes;
  }

  bool isReadZero() const { return IsZero; }
  void setReadZero() { IsZero = true; }
  void setPRF(unsigned ID) { PRFID = ID; }
};

/// A sequence of cycles.
///
/// This class can be used as a building block to construct ranges of cycles.
class CycleSegment {
  unsigned Begin; // Inclusive.
  unsigned End;   // Exclusive.
  bool Reserved;  // Resources associated to this segment must be reserved.

public:
  CycleSegment(unsigned StartCycle, unsigned EndCycle, bool IsReserved = false)
      : Begin(StartCycle), End(EndCycle), Reserved(IsReserved) {}

  bool contains(unsigned Cycle) const { return Cycle >= Begin && Cycle < End; }
  bool startsAfter(const CycleSegment &CS) const { return End <= CS.Begin; }
  bool endsBefore(const CycleSegment &CS) const { return Begin >= CS.End; }
  bool overlaps(const CycleSegment &CS) const {
    return !startsAfter(CS) && !endsBefore(CS);
  }
  bool isExecuting() const { return Begin == 0 && End != 0; }
  bool isExecuted() const { return End == 0; }
  bool operator<(const CycleSegment &Other) const {
    return Begin < Other.Begin;
  }
  CycleSegment &operator--() {
    if (Begin)
      Begin--;
    if (End)
      End--;
    return *this;
  }

  bool isValid() const { return Begin <= End; }
  unsigned size() const { return End - Begin; };
  void subtract(unsigned Cycles) {
    assert(End >= Cycles);
    End -= Cycles;
  }

  unsigned begin() const { return Begin; }
  unsigned end() const { return End; }
  void setEnd(unsigned NewEnd) { End = NewEnd; }
  bool isReserved() const { return Reserved; }
  void setReserved() { Reserved = true; }
};

/// Helper used by class InstrDesc to describe how hardware resources
/// are used.
///
/// This class describes how many resource units of a specific resource kind
/// (and how many cycles) are "used" by an instruction.
struct ResourceUsage {
  CycleSegment CS;
  unsigned NumUnits;
  ResourceUsage(CycleSegment Cycles, unsigned Units = 1)
      : CS(Cycles), NumUnits(Units) {}
  unsigned size() const { return CS.size(); }
  bool isReserved() const { return CS.isReserved(); }
  void setReserved() { CS.setReserved(); }
};

/// An instruction descriptor
struct InstrDesc {
  SmallVector<WriteDescriptor, 2> Writes; // Implicit writes are at the end.
  SmallVector<ReadDescriptor, 4> Reads;   // Implicit reads are at the end.

  // For every resource used by an instruction of this kind, this vector
  // reports the number of "consumed cycles".
  SmallVector<std::pair<uint64_t, ResourceUsage>, 4> Resources;

  // A bitmask of used hardware buffers.
  uint64_t UsedBuffers;

  // A bitmask of used processor resource units.
  uint64_t UsedProcResUnits;

  // A bitmask of used processor resource groups.
  uint64_t UsedProcResGroups;

  unsigned MaxLatency;
  // Number of MicroOps for this instruction.
  unsigned NumMicroOps;
  // SchedClassID used to construct this InstrDesc.
  // This information is currently used by views to do fast queries on the
  // subtarget when computing the reciprocal throughput.
  unsigned SchedClassID;

  // True if all buffered resources are in-order, and there is at least one
  // buffer which is a dispatch hazard (BufferSize = 0).
  unsigned MustIssueImmediately : 1;

  // True if the corresponding mca::Instruction can be recycled. Currently only
  // instructions that are neither variadic nor have any variant can be
  // recycled.
  unsigned IsRecyclable : 1;

  // True if some of the consumed group resources are partially overlapping.
  unsigned HasPartiallyOverlappingGroups : 1;

  // A zero latency instruction doesn't consume any scheduler resources.
  bool isZeroLatency() const { return !MaxLatency && Resources.empty(); }

  InstrDesc() = default;
  InstrDesc(const InstrDesc &Other) = delete;
  InstrDesc &operator=(const InstrDesc &Other) = delete;
};

/// Base class for instructions consumed by the simulation pipeline.
///
/// This class tracks data dependencies as well as generic properties
/// of the instruction.
class InstructionBase {
  const InstrDesc &Desc;

  // This field is set for instructions that are candidates for move
  // elimination. For more information about move elimination, see the
  // definition of RegisterMappingTracker in RegisterFile.h
  bool IsOptimizableMove;

  // Output dependencies.
  // One entry per each implicit and explicit register definition.
  SmallVector<WriteState, 2> Defs;

  // Input dependencies.
  // One entry per each implicit and explicit register use.
  SmallVector<ReadState, 4> Uses;

  // List of operands which can be used by mca::CustomBehaviour
  std::vector<MCAOperand> Operands;

  // Instruction opcode which can be used by mca::CustomBehaviour
  unsigned Opcode;

  // Flags used by the LSUnit.
  bool IsALoadBarrier : 1;
  bool IsAStoreBarrier : 1;
  // Flags copied from the InstrDesc and potentially modified by
  // CustomBehaviour or (more likely) InstrPostProcess.
  bool MayLoad : 1;
  bool MayStore : 1;
  bool HasSideEffects : 1;
  bool BeginGroup : 1;
  bool EndGroup : 1;
  bool RetireOOO : 1;

public:
  InstructionBase(const InstrDesc &D, const unsigned Opcode)
      : Desc(D), IsOptimizableMove(false), Operands(0), Opcode(Opcode),
        IsALoadBarrier(false), IsAStoreBarrier(false) {}

  SmallVectorImpl<WriteState> &getDefs() { return Defs; }
  ArrayRef<WriteState> getDefs() const { return Defs; }
  SmallVectorImpl<ReadState> &getUses() { return Uses; }
  ArrayRef<ReadState> getUses() const { return Uses; }
  const InstrDesc &getDesc() const { return Desc; }

  unsigned getLatency() const { return Desc.MaxLatency; }
  unsigned getNumMicroOps() const { return Desc.NumMicroOps; }
  unsigned getOpcode() const { return Opcode; }
  bool isALoadBarrier() const { return IsALoadBarrier; }
  bool isAStoreBarrier() const { return IsAStoreBarrier; }
  void setLoadBarrier(bool IsBarrier) { IsALoadBarrier = IsBarrier; }
  void setStoreBarrier(bool IsBarrier) { IsAStoreBarrier = IsBarrier; }

  /// Return the MCAOperand which corresponds to index Idx within the original
  /// MCInst.
  const MCAOperand *getOperand(const unsigned Idx) const {
    auto It = llvm::find_if(Operands, [&Idx](const MCAOperand &Op) {
      return Op.getIndex() == Idx;
    });
    if (It == Operands.end())
      return nullptr;
    return &(*It);
  }
  unsigned getNumOperands() const { return Operands.size(); }
  void addOperand(const MCAOperand Op) { Operands.push_back(Op); }

  bool hasDependentUsers() const {
    return any_of(Defs,
                  [](const WriteState &Def) { return Def.getNumUsers() > 0; });
  }

  unsigned getNumUsers() const {
    unsigned NumUsers = 0;
    for (const WriteState &Def : Defs)
      NumUsers += Def.getNumUsers();
    return NumUsers;
  }

  // Returns true if this instruction is a candidate for move elimination.
  bool isOptimizableMove() const { return IsOptimizableMove; }
  void setOptimizableMove() { IsOptimizableMove = true; }
  void clearOptimizableMove() { IsOptimizableMove = false; }
  bool isMemOp() const { return MayLoad || MayStore; }

  // Getters and setters for general instruction flags.
  void setMayLoad(bool newVal) { MayLoad = newVal; }
  void setMayStore(bool newVal) { MayStore = newVal; }
  void setHasSideEffects(bool newVal) { HasSideEffects = newVal; }
  void setBeginGroup(bool newVal) { BeginGroup = newVal; }
  void setEndGroup(bool newVal) { EndGroup = newVal; }
  void setRetireOOO(bool newVal) { RetireOOO = newVal; }

  bool getMayLoad() const { return MayLoad; }
  bool getMayStore() const { return MayStore; }
  bool getHasSideEffects() const { return HasSideEffects; }
  bool getBeginGroup() const { return BeginGroup; }
  bool getEndGroup() const { return EndGroup; }
  bool getRetireOOO() const { return RetireOOO; }
};

/// An instruction propagated through the simulated instruction pipeline.
///
/// This class is used to monitor changes to the internal state of instructions
/// that are sent to the various components of the simulated hardware pipeline.
class Instruction : public InstructionBase {
  enum InstrStage {
    IS_INVALID,    // Instruction in an invalid state.
    IS_DISPATCHED, // Instruction dispatched but operands are not ready.
    IS_PENDING,    // Instruction is not ready, but operand latency is known.
    IS_READY,      // Instruction dispatched and operands ready.
    IS_EXECUTING,  // Instruction issued.
    IS_EXECUTED,   // Instruction executed. Values are written back.
    IS_RETIRED     // Instruction retired.
  };

  // The current instruction stage.
  enum InstrStage Stage;

  // This value defaults to the instruction latency. This instruction is
  // considered executed when field CyclesLeft goes to zero.
  int CyclesLeft;

  // Retire Unit token ID for this instruction.
  unsigned RCUTokenID;

  // LS token ID for this instruction.
  // This field is set to the invalid null token if this is not a memory
  // operation.
  unsigned LSUTokenID;

  // A resource mask which identifies buffered resources consumed by this
  // instruction at dispatch stage. In the absence of macro-fusion, this value
  // should always match the value of field `UsedBuffers` from the instruction
  // descriptor (see field InstrBase::Desc).
  uint64_t UsedBuffers;

  // Critical register dependency.
  CriticalDependency CriticalRegDep;

  // Critical memory dependency.
  CriticalDependency CriticalMemDep;

  // A bitmask of busy processor resource units.
  // This field is set to zero only if execution is not delayed during this
  // cycle because of unavailable pipeline resources.
  uint64_t CriticalResourceMask;

  // True if this instruction has been optimized at register renaming stage.
  bool IsEliminated;

public:
  Instruction(const InstrDesc &D, const unsigned Opcode)
      : InstructionBase(D, Opcode), Stage(IS_INVALID),
        CyclesLeft(UNKNOWN_CYCLES), RCUTokenID(0), LSUTokenID(0),
        UsedBuffers(D.UsedBuffers), CriticalRegDep(), CriticalMemDep(),
        CriticalResourceMask(0), IsEliminated(false) {}

  void reset();

  unsigned getRCUTokenID() const { return RCUTokenID; }
  unsigned getLSUTokenID() const { return LSUTokenID; }
  void setLSUTokenID(unsigned LSUTok) { LSUTokenID = LSUTok; }

  uint64_t getUsedBuffers() const { return UsedBuffers; }
  void setUsedBuffers(uint64_t Mask) { UsedBuffers = Mask; }
  void clearUsedBuffers() { UsedBuffers = 0ULL; }

  int getCyclesLeft() const { return CyclesLeft; }

  // Transition to the dispatch stage, and assign a RCUToken to this
  // instruction. The RCUToken is used to track the completion of every
  // register write performed by this instruction.
  void dispatch(unsigned RCUTokenID);

  // Instruction issued. Transition to the IS_EXECUTING state, and update
  // all the register definitions.
  void execute(unsigned IID);

  // Force a transition from the IS_DISPATCHED state to the IS_READY or
  // IS_PENDING state. State transitions normally occur either at the beginning
  // of a new cycle (see method cycleEvent()), or as a result of another issue
  // event. This method is called every time the instruction might have changed
  // in state. It internally delegates to method updateDispatched() and
  // updateWaiting().
  void update();
  bool updateDispatched();
  bool updatePending();

  bool isInvalid() const { return Stage == IS_INVALID; }
  bool isDispatched() const { return Stage == IS_DISPATCHED; }
  bool isPending() const { return Stage == IS_PENDING; }
  bool isReady() const { return Stage == IS_READY; }
  bool isExecuting() const { return Stage == IS_EXECUTING; }
  bool isExecuted() const { return Stage == IS_EXECUTED; }
  bool isRetired() const { return Stage == IS_RETIRED; }
  bool isEliminated() const { return IsEliminated; }

  // Forces a transition from state IS_DISPATCHED to state IS_EXECUTED.
  void forceExecuted();
  void setEliminated() { IsEliminated = true; }

  void retire() {
    assert(isExecuted() && "Instruction is in an invalid state!");
    Stage = IS_RETIRED;
  }

  const CriticalDependency &getCriticalRegDep() const { return CriticalRegDep; }
  const CriticalDependency &getCriticalMemDep() const { return CriticalMemDep; }
  const CriticalDependency &computeCriticalRegDep();
  void setCriticalMemDep(const CriticalDependency &MemDep) {
    CriticalMemDep = MemDep;
  }

  uint64_t getCriticalResourceMask() const { return CriticalResourceMask; }
  void setCriticalResourceMask(uint64_t ResourceMask) {
    CriticalResourceMask = ResourceMask;
  }

  void cycleEvent();
};

/// An InstRef contains both a SourceMgr index and Instruction pair.  The index
/// is used as a unique identifier for the instruction.  MCA will make use of
/// this index as a key throughout MCA.
class InstRef {
  std::pair<unsigned, Instruction *> Data;

public:
  InstRef() : Data(std::make_pair(0, nullptr)) {}
  InstRef(unsigned Index, Instruction *I) : Data(std::make_pair(Index, I)) {}

  bool operator==(const InstRef &Other) const { return Data == Other.Data; }
  bool operator!=(const InstRef &Other) const { return Data != Other.Data; }
  bool operator<(const InstRef &Other) const {
    return Data.first < Other.Data.first;
  }

  unsigned getSourceIndex() const { return Data.first; }
  Instruction *getInstruction() { return Data.second; }
  const Instruction *getInstruction() const { return Data.second; }

  /// Returns true if this references a valid instruction.
  explicit operator bool() const { return Data.second != nullptr; }

  /// Invalidate this reference.
  void invalidate() { Data.second = nullptr; }

#ifndef NDEBUG
  void print(raw_ostream &OS) const { OS << getSourceIndex(); }
#endif
};

#ifndef NDEBUG
inline raw_ostream &operator<<(raw_ostream &OS, const InstRef &IR) {
  IR.print(OS);
  return OS;
}
#endif

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_INSTRUCTION_H
