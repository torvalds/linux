//===-- llvm/MC/MCSchedule.h - Scheduling -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to describe a subtarget's machine model
// for scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSCHEDULE_H
#define LLVM_MC_MCSCHEDULE_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/DataTypes.h"
#include <cassert>

namespace llvm {

template <typename T> class ArrayRef;
struct InstrItinerary;
class MCSubtargetInfo;
class MCInstrInfo;
class MCInst;
class InstrItineraryData;

/// Define a kind of processor resource that will be modeled by the scheduler.
struct MCProcResourceDesc {
  const char *Name;
  unsigned NumUnits; // Number of resource of this kind
  unsigned SuperIdx; // Index of the resources kind that contains this kind.

  // Number of resources that may be buffered.
  //
  // Buffered resources (BufferSize != 0) may be consumed at some indeterminate
  // cycle after dispatch. This should be used for out-of-order cpus when
  // instructions that use this resource can be buffered in a reservaton
  // station.
  //
  // Unbuffered resources (BufferSize == 0) always consume their resource some
  // fixed number of cycles after dispatch. If a resource is unbuffered, then
  // the scheduler will avoid scheduling instructions with conflicting resources
  // in the same cycle. This is for in-order cpus, or the in-order portion of
  // an out-of-order cpus.
  int BufferSize;

  // If the resource has sub-units, a pointer to the first element of an array
  // of `NumUnits` elements containing the ProcResourceIdx of the sub units.
  // nullptr if the resource does not have sub-units.
  const unsigned *SubUnitsIdxBegin;

  bool operator==(const MCProcResourceDesc &Other) const {
    return NumUnits == Other.NumUnits && SuperIdx == Other.SuperIdx
      && BufferSize == Other.BufferSize;
  }
};

/// Identify one of the processor resource kinds consumed by a
/// particular scheduling class for the specified number of cycles.
struct MCWriteProcResEntry {
  uint16_t ProcResourceIdx;
  /// Cycle at which the resource will be released by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  uint16_t ReleaseAtCycle;
  /// Cycle at which the resource will be aquired by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  uint16_t AcquireAtCycle;

  bool operator==(const MCWriteProcResEntry &Other) const {
    return ProcResourceIdx == Other.ProcResourceIdx &&
           ReleaseAtCycle == Other.ReleaseAtCycle &&
           AcquireAtCycle == Other.AcquireAtCycle;
  }
};

/// Specify the latency in cpu cycles for a particular scheduling class and def
/// index. -1 indicates an invalid latency. Heuristics would typically consider
/// an instruction with invalid latency to have infinite latency.  Also identify
/// the WriteResources of this def. When the operand expands to a sequence of
/// writes, this ID is the last write in the sequence.
struct MCWriteLatencyEntry {
  int16_t Cycles;
  uint16_t WriteResourceID;

  bool operator==(const MCWriteLatencyEntry &Other) const {
    return Cycles == Other.Cycles && WriteResourceID == Other.WriteResourceID;
  }
};

/// Specify the number of cycles allowed after instruction issue before a
/// particular use operand reads its registers. This effectively reduces the
/// write's latency. Here we allow negative cycles for corner cases where
/// latency increases. This rule only applies when the entry's WriteResource
/// matches the write's WriteResource.
///
/// MCReadAdvanceEntries are sorted first by operand index (UseIdx), then by
/// WriteResourceIdx.
struct MCReadAdvanceEntry {
  unsigned UseIdx;
  unsigned WriteResourceID;
  int Cycles;

  bool operator==(const MCReadAdvanceEntry &Other) const {
    return UseIdx == Other.UseIdx && WriteResourceID == Other.WriteResourceID
      && Cycles == Other.Cycles;
  }
};

/// Summarize the scheduling resources required for an instruction of a
/// particular scheduling class.
///
/// Defined as an aggregate struct for creating tables with initializer lists.
struct MCSchedClassDesc {
  static const unsigned short InvalidNumMicroOps = (1U << 13) - 1;
  static const unsigned short VariantNumMicroOps = InvalidNumMicroOps - 1;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  const char* Name;
#endif
  uint16_t NumMicroOps : 13;
  uint16_t BeginGroup : 1;
  uint16_t EndGroup : 1;
  uint16_t RetireOOO : 1;
  uint16_t WriteProcResIdx; // First index into WriteProcResTable.
  uint16_t NumWriteProcResEntries;
  uint16_t WriteLatencyIdx; // First index into WriteLatencyTable.
  uint16_t NumWriteLatencyEntries;
  uint16_t ReadAdvanceIdx; // First index into ReadAdvanceTable.
  uint16_t NumReadAdvanceEntries;

  bool isValid() const {
    return NumMicroOps != InvalidNumMicroOps;
  }
  bool isVariant() const {
    return NumMicroOps == VariantNumMicroOps;
  }
};

/// Specify the cost of a register definition in terms of number of physical
/// register allocated at register renaming stage. For example, AMD Jaguar.
/// natively supports 128-bit data types, and operations on 256-bit registers
/// (i.e. YMM registers) are internally split into two COPs (complex operations)
/// and each COP updates a physical register. Basically, on Jaguar, a YMM
/// register write effectively consumes two physical registers. That means,
/// the cost of a YMM write in the BtVer2 model is 2.
struct MCRegisterCostEntry {
  unsigned RegisterClassID;
  unsigned Cost;
  bool AllowMoveElimination;
};

/// A register file descriptor.
///
/// This struct allows to describe processor register files. In particular, it
/// helps describing the size of the register file, as well as the cost of
/// allocating a register file at register renaming stage.
/// FIXME: this struct can be extended to provide information about the number
/// of read/write ports to the register file.  A value of zero for field
/// 'NumPhysRegs' means: this register file has an unbounded number of physical
/// registers.
struct MCRegisterFileDesc {
  const char *Name;
  uint16_t NumPhysRegs;
  uint16_t NumRegisterCostEntries;
  // Index of the first cost entry in MCExtraProcessorInfo::RegisterCostTable.
  uint16_t RegisterCostEntryIdx;
  // A value of zero means: there is no limit in the number of moves that can be
  // eliminated every cycle.
  uint16_t MaxMovesEliminatedPerCycle;
  // Ture if this register file only knows how to optimize register moves from
  // known zero registers.
  bool AllowZeroMoveEliminationOnly;
};

/// Provide extra details about the machine processor.
///
/// This is a collection of "optional" processor information that is not
/// normally used by the LLVM machine schedulers, but that can be consumed by
/// external tools like llvm-mca to improve the quality of the peformance
/// analysis.
struct MCExtraProcessorInfo {
  // Actual size of the reorder buffer in hardware.
  unsigned ReorderBufferSize;
  // Number of instructions retired per cycle.
  unsigned MaxRetirePerCycle;
  const MCRegisterFileDesc *RegisterFiles;
  unsigned NumRegisterFiles;
  const MCRegisterCostEntry *RegisterCostTable;
  unsigned NumRegisterCostEntries;
  unsigned LoadQueueID;
  unsigned StoreQueueID;
};

/// Machine model for scheduling, bundling, and heuristics.
///
/// The machine model directly provides basic information about the
/// microarchitecture to the scheduler in the form of properties. It also
/// optionally refers to scheduler resource tables and itinerary
/// tables. Scheduler resource tables model the latency and cost for each
/// instruction type. Itinerary tables are an independent mechanism that
/// provides a detailed reservation table describing each cycle of instruction
/// execution. Subtargets may define any or all of the above categories of data
/// depending on the type of CPU and selected scheduler.
///
/// The machine independent properties defined here are used by the scheduler as
/// an abstract machine model. A real micro-architecture has a number of
/// buffers, queues, and stages. Declaring that a given machine-independent
/// abstract property corresponds to a specific physical property across all
/// subtargets can't be done. Nonetheless, the abstract model is
/// useful. Futhermore, subtargets typically extend this model with processor
/// specific resources to model any hardware features that can be exploited by
/// scheduling heuristics and aren't sufficiently represented in the abstract.
///
/// The abstract pipeline is built around the notion of an "issue point". This
/// is merely a reference point for counting machine cycles. The physical
/// machine will have pipeline stages that delay execution. The scheduler does
/// not model those delays because they are irrelevant as long as they are
/// consistent. Inaccuracies arise when instructions have different execution
/// delays relative to each other, in addition to their intrinsic latency. Those
/// special cases can be handled by TableGen constructs such as, ReadAdvance,
/// which reduces latency when reading data, and ReleaseAtCycles, which consumes
/// a processor resource when writing data for a number of abstract
/// cycles.
///
/// TODO: One tool currently missing is the ability to add a delay to
/// ReleaseAtCycles. That would be easy to add and would likely cover all cases
/// currently handled by the legacy itinerary tables.
///
/// A note on out-of-order execution and, more generally, instruction
/// buffers. Part of the CPU pipeline is always in-order. The issue point, which
/// is the point of reference for counting cycles, only makes sense as an
/// in-order part of the pipeline. Other parts of the pipeline are sometimes
/// falling behind and sometimes catching up. It's only interesting to model
/// those other, decoupled parts of the pipeline if they may be predictably
/// resource constrained in a way that the scheduler can exploit.
///
/// The LLVM machine model distinguishes between in-order constraints and
/// out-of-order constraints so that the target's scheduling strategy can apply
/// appropriate heuristics. For a well-balanced CPU pipeline, out-of-order
/// resources would not typically be treated as a hard scheduling
/// constraint. For example, in the GenericScheduler, a delay caused by limited
/// out-of-order resources is not directly reflected in the number of cycles
/// that the scheduler sees between issuing an instruction and its dependent
/// instructions. In other words, out-of-order resources don't directly increase
/// the latency between pairs of instructions. However, they can still be used
/// to detect potential bottlenecks across a sequence of instructions and bias
/// the scheduling heuristics appropriately.
struct MCSchedModel {
  // IssueWidth is the maximum number of instructions that may be scheduled in
  // the same per-cycle group. This is meant to be a hard in-order constraint
  // (a.k.a. "hazard"). In the GenericScheduler strategy, no more than
  // IssueWidth micro-ops can ever be scheduled in a particular cycle.
  //
  // In practice, IssueWidth is useful to model any bottleneck between the
  // decoder (after micro-op expansion) and the out-of-order reservation
  // stations or the decoder bandwidth itself. If the total number of
  // reservation stations is also a bottleneck, or if any other pipeline stage
  // has a bandwidth limitation, then that can be naturally modeled by adding an
  // out-of-order processor resource.
  unsigned IssueWidth;
  static const unsigned DefaultIssueWidth = 1;

  // MicroOpBufferSize is the number of micro-ops that the processor may buffer
  // for out-of-order execution.
  //
  // "0" means operations that are not ready in this cycle are not considered
  // for scheduling (they go in the pending queue). Latency is paramount. This
  // may be more efficient if many instructions are pending in a schedule.
  //
  // "1" means all instructions are considered for scheduling regardless of
  // whether they are ready in this cycle. Latency still causes issue stalls,
  // but we balance those stalls against other heuristics.
  //
  // "> 1" means the processor is out-of-order. This is a machine independent
  // estimate of highly machine specific characteristics such as the register
  // renaming pool and reorder buffer.
  unsigned MicroOpBufferSize;
  static const unsigned DefaultMicroOpBufferSize = 0;

  // LoopMicroOpBufferSize is the number of micro-ops that the processor may
  // buffer for optimized loop execution. More generally, this represents the
  // optimal number of micro-ops in a loop body. A loop may be partially
  // unrolled to bring the count of micro-ops in the loop body closer to this
  // number.
  unsigned LoopMicroOpBufferSize;
  static const unsigned DefaultLoopMicroOpBufferSize = 0;

  // LoadLatency is the expected latency of load instructions.
  unsigned LoadLatency;
  static const unsigned DefaultLoadLatency = 4;

  // HighLatency is the expected latency of "very high latency" operations.
  // See TargetInstrInfo::isHighLatencyDef().
  // By default, this is set to an arbitrarily high number of cycles
  // likely to have some impact on scheduling heuristics.
  unsigned HighLatency;
  static const unsigned DefaultHighLatency = 10;

  // MispredictPenalty is the typical number of extra cycles the processor
  // takes to recover from a branch misprediction.
  unsigned MispredictPenalty;
  static const unsigned DefaultMispredictPenalty = 10;

  bool PostRAScheduler; // default value is false

  bool CompleteModel;

  // Tells the MachineScheduler whether or not to track resource usage
  // using intervals via ResourceSegments (see
  // llvm/include/llvm/CodeGen/MachineScheduler.h).
  bool EnableIntervals;

  unsigned ProcID;
  const MCProcResourceDesc *ProcResourceTable;
  const MCSchedClassDesc *SchedClassTable;
  unsigned NumProcResourceKinds;
  unsigned NumSchedClasses;
  // Instruction itinerary tables used by InstrItineraryData.
  friend class InstrItineraryData;
  const InstrItinerary *InstrItineraries;

  const MCExtraProcessorInfo *ExtraProcessorInfo;

  bool hasExtraProcessorInfo() const { return ExtraProcessorInfo; }

  unsigned getProcessorID() const { return ProcID; }

  /// Does this machine model include instruction-level scheduling.
  bool hasInstrSchedModel() const { return SchedClassTable; }

  const MCExtraProcessorInfo &getExtraProcessorInfo() const {
    assert(hasExtraProcessorInfo() &&
           "No extra information available for this model");
    return *ExtraProcessorInfo;
  }

  /// Return true if this machine model data for all instructions with a
  /// scheduling class (itinerary class or SchedRW list).
  bool isComplete() const { return CompleteModel; }

  /// Return true if machine supports out of order execution.
  bool isOutOfOrder() const { return MicroOpBufferSize > 1; }

  unsigned getNumProcResourceKinds() const {
    return NumProcResourceKinds;
  }

  const MCProcResourceDesc *getProcResource(unsigned ProcResourceIdx) const {
    assert(hasInstrSchedModel() && "No scheduling machine model");

    assert(ProcResourceIdx < NumProcResourceKinds && "bad proc resource idx");
    return &ProcResourceTable[ProcResourceIdx];
  }

  const MCSchedClassDesc *getSchedClassDesc(unsigned SchedClassIdx) const {
    assert(hasInstrSchedModel() && "No scheduling machine model");

    assert(SchedClassIdx < NumSchedClasses && "bad scheduling class idx");
    return &SchedClassTable[SchedClassIdx];
  }

  /// Returns the latency value for the scheduling class.
  static int computeInstrLatency(const MCSubtargetInfo &STI,
                                 const MCSchedClassDesc &SCDesc);

  int computeInstrLatency(const MCSubtargetInfo &STI, unsigned SClass) const;
  int computeInstrLatency(const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
                          const MCInst &Inst) const;

  // Returns the reciprocal throughput information from a MCSchedClassDesc.
  static double
  getReciprocalThroughput(const MCSubtargetInfo &STI,
                          const MCSchedClassDesc &SCDesc);

  static double
  getReciprocalThroughput(unsigned SchedClass, const InstrItineraryData &IID);

  double
  getReciprocalThroughput(const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
                          const MCInst &Inst) const;

  /// Returns the maximum forwarding delay for register reads dependent on
  /// writes of scheduling class WriteResourceIdx.
  static unsigned getForwardingDelayCycles(ArrayRef<MCReadAdvanceEntry> Entries,
                                           unsigned WriteResourceIdx = 0);

  /// Returns the default initialized model.
  static const MCSchedModel Default;
};

} // namespace llvm

#endif
