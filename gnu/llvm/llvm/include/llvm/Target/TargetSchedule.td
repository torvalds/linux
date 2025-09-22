//===- TargetSchedule.td - Target Independent Scheduling ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent scheduling interfaces which should
// be implemented by each target which is using TableGen based scheduling.
//
// The SchedMachineModel is defined by subtargets for three categories of data:
// 1. Basic properties for coarse grained instruction cost model.
// 2. Scheduler Read/Write resources for simple per-opcode cost model.
// 3. Instruction itineraries for detailed reservation tables.
//
// (1) Basic properties are defined by the SchedMachineModel
// class. Target hooks allow subtargets to associate opcodes with
// those properties.
//
// (2) A per-operand machine model can be implemented in any
// combination of the following ways:
//
// A. Associate per-operand SchedReadWrite types with Instructions by
// modifying the Instruction definition to inherit from Sched. For
// each subtarget, define WriteRes and ReadAdvance to associate
// processor resources and latency with each SchedReadWrite type.
//
// B. In each instruction definition, name an ItineraryClass. For each
// subtarget, define ItinRW entries to map ItineraryClass to
// per-operand SchedReadWrite types. Unlike method A, these types may
// be subtarget specific and can be directly associated with resources
// by defining SchedWriteRes and SchedReadAdvance.
//
// C. In the subtarget, map SchedReadWrite types to specific
// opcodes. This overrides any SchedReadWrite types or
// ItineraryClasses defined by the Instruction. As in method B, the
// subtarget can directly associate resources with SchedReadWrite
// types by defining SchedWriteRes and SchedReadAdvance.
//
// D. In either the target or subtarget, define SchedWriteVariant or
// SchedReadVariant to map one SchedReadWrite type onto another
// sequence of SchedReadWrite types. This allows dynamic selection of
// an instruction's machine model via custom C++ code. It also allows
// a machine-independent SchedReadWrite type to map to a sequence of
// machine-dependent types.
//
// (3) A per-pipeline-stage machine model can be implemented by providing
// Itineraries in addition to mapping instructions to ItineraryClasses.
//===----------------------------------------------------------------------===//

// Include legacy support for instruction itineraries.
include "llvm/Target/TargetItinerary.td"

class Predicate; // Forward def

// DAG operator that interprets the DAG args as Instruction defs.
def instrs;

// DAG operator that interprets each DAG arg as a regex pattern for
// matching Instruction opcode names.
// The regex must match the beginning of the opcode (as in Python re.match).
// To avoid matching prefixes, append '$' to the pattern.
def instregex;

// Define the SchedMachineModel and provide basic properties for
// coarse grained instruction cost model. Default values for the
// properties are defined in MCSchedModel. A value of "-1" in the
// target description's SchedMachineModel indicates that the property
// is not overriden by the target.
//
// Target hooks allow subtargets to associate LoadLatency and
// HighLatency with groups of opcodes.
//
// See MCSchedule.h for detailed comments.
class SchedMachineModel {
  int IssueWidth = -1; // Max micro-ops that may be scheduled per cycle.
  int MicroOpBufferSize = -1; // Max micro-ops that can be buffered.
  int LoopMicroOpBufferSize = -1; // Max micro-ops that can be buffered for
                                  // optimized loop dispatch/execution.
  int LoadLatency = -1; // Cycles for loads to access the cache.
  int HighLatency = -1; // Approximation of cycles for "high latency" ops.
  int MispredictPenalty = -1; // Extra cycles for a mispredicted branch.

  // Per-cycle resources tables.
  ProcessorItineraries Itineraries = NoItineraries;

  bit PostRAScheduler = false; // Enable Post RegAlloc Scheduler pass.

  // Subtargets that define a model for only a subset of instructions
  // that have a scheduling class (itinerary class or SchedRW list)
  // and may actually be generated for that subtarget must clear this
  // bit. Otherwise, the scheduler considers an unmodelled opcode to
  // be an error. This should only be set during initial bringup,
  // or there will be no way to catch simple errors in the model
  // resulting from changes to the instruction definitions.
  bit CompleteModel = true;

  // Indicates that we should do full overlap checking for multiple InstrRWs
  // defining the same instructions within the same SchedMachineModel.
  // FIXME: Remove when all in tree targets are clean with the full check
  // enabled.
  bit FullInstRWOverlapCheck = true;

  // A processor may only implement part of published ISA, due to either new ISA
  // extensions, (e.g. Pentium 4 doesn't have AVX) or implementation
  // (ARM/MIPS/PowerPC/SPARC soft float cores).
  //
  // For a processor which doesn't support some feature(s), the schedule model
  // can use:
  //
  // let<Predicate> UnsupportedFeatures = [HaveA,..,HaveY];
  //
  // to skip the checks for scheduling information when building LLVM for
  // instructions which have any of the listed predicates in their Predicates
  // field.
  list<Predicate> UnsupportedFeatures = [];

  bit NoModel = false; // Special tag to indicate missing machine model.

  // Tells the MachineScheduler whether or not to track resource usage
  // using intervals via ResourceSegments (see
  // llvm/include/llvm/CodeGen/MachineScheduler.h).
  bit EnableIntervals = false;
}

def NoSchedModel : SchedMachineModel {
  let NoModel = true;
  let CompleteModel = false;
}

// Define a kind of processor resource that may be common across
// similar subtargets.
class ProcResourceKind;

// Define a number of interchangeable processor resources. NumUnits
// determines the throughput of instructions that require the resource.
//
// An optional Super resource may be given to model these resources as
// a subset of the more general super resources. Using one of these
// resources implies using one of the super resources.
//
// ProcResourceUnits normally model a few buffered resources within an
// out-of-order engine. Buffered resources may be held for multiple
// clock cycles, but the scheduler does not pin them to a particular
// clock cycle relative to instruction dispatch. Setting BufferSize=0
// changes this to an in-order issue/dispatch resource. In this case,
// the scheduler counts down from the cycle that the instruction
// issues in-order, forcing a stall whenever a subsequent instruction
// requires the same resource until the number of ReleaseAtCycles
// specified in WriteRes expire. Setting BufferSize=1 changes this to
// an in-order latency resource. In this case, the scheduler models
// producer/consumer stalls between instructions that use the
// resource.
//
// Examples (all assume an out-of-order engine):
//
// Use BufferSize = -1 for "issue ports" fed by a unified reservation
// station. Here the size of the reservation station is modeled by
// MicroOpBufferSize, which should be the minimum size of either the
// register rename pool, unified reservation station, or reorder
// buffer.
//
// Use BufferSize = 0 for resources that force "dispatch/issue
// groups". (Different processors define dispath/issue
// differently. Here we refer to stage between decoding into micro-ops
// and moving them into a reservation station.) Normally NumMicroOps
// is sufficient to limit dispatch/issue groups. However, some
// processors can form groups of with only certain combinations of
// instruction types. e.g. POWER7.
//
// Use BufferSize = 1 for in-order execution units. This is used for
// an in-order pipeline within an out-of-order core where scheduling
// dependent operations back-to-back is guaranteed to cause a
// bubble. e.g. Cortex-a9 floating-point.
//
// Use BufferSize > 1 for out-of-order executions units with a
// separate reservation station. This simply models the size of the
// reservation station.
//
// To model both dispatch/issue groups and in-order execution units,
// create two types of units, one with BufferSize=0 and one with
// BufferSize=1.
//
// SchedModel ties these units to a processor for any stand-alone defs
// of this class.
class ProcResourceUnits<ProcResourceKind kind, int num> {
  ProcResourceKind Kind = kind;
  int NumUnits = num;
  ProcResourceKind Super = ?;
  int BufferSize = -1;
  SchedMachineModel SchedModel = ?;
}

// Subtargets typically define processor resource kind and number of
// units in one place.
class ProcResource<int num> : ProcResourceKind,
  ProcResourceUnits<!cast<ProcResourceKind>(NAME), num>;

class ProcResGroup<list<ProcResource> resources> : ProcResourceKind {
  list<ProcResource> Resources = resources;
  SchedMachineModel SchedModel = ?;
  int BufferSize = -1;
}

// A target architecture may define SchedReadWrite types and associate
// them with instruction operands.
class SchedReadWrite;

// List the per-operand types that map to the machine model of an
// instruction. One SchedWrite type must be listed for each explicit
// def operand in order. Additional SchedWrite types may optionally be
// listed for implicit def operands.  SchedRead types may optionally
// be listed for use operands in order. The order of defs relative to
// uses is insignificant. This way, the same SchedReadWrite list may
// be used for multiple forms of an operation. For example, a
// two-address instruction could have two tied operands or single
// operand that both reads and writes a reg. In both cases we have a
// single SchedWrite and single SchedRead in any order.
class Sched<list<SchedReadWrite> schedrw> {
  list<SchedReadWrite> SchedRW = schedrw;
}

// Define a scheduler resource associated with a def operand.
class SchedWrite : SchedReadWrite;
def NoWrite : SchedWrite;

// Define a scheduler resource associated with a use operand.
class SchedRead  : SchedReadWrite;

// Define a SchedWrite that is modeled as a sequence of other
// SchedWrites with additive latency. This allows a single operand to
// be mapped the resources composed from a set of previously defined
// SchedWrites.
//
// If the final write in this sequence is a SchedWriteVariant marked
// Variadic, then the list of prior writes are distributed across all
// operands after resolving the predicate for the final write.
//
// SchedModel silences warnings but is ignored.
class WriteSequence<list<SchedWrite> writes, int rep = 1> : SchedWrite {
  list<SchedWrite> Writes = writes;
  int Repeat = rep;
  SchedMachineModel SchedModel = ?;
}

// Define values common to WriteRes and SchedWriteRes.
//
// SchedModel ties these resources to a processor.
class ProcWriteResources<list<ProcResourceKind> resources> {
  list<ProcResourceKind> ProcResources = resources;
  /// Cycle at which the resource will be released by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  list<int> ReleaseAtCycles = [];
  /// Cycle at which the resource will be aquired by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  list<int> AcquireAtCycles = [];
  int Latency = 1;
  int NumMicroOps = 1;
  bit BeginGroup = false;
  bit EndGroup = false;
  // Allow a processor to mark some scheduling classes as unsupported
  // for stronger verification.
  bit Unsupported = false;
  // Allow a processor to mark some scheduling classes as single-issue.
  // SingleIssue is an alias for Begin/End Group.
  bit SingleIssue = false;
  // An instruction is allowed to retire out-of-order if RetireOOO is
  // true for at least one of its writes. This field is only used by
  // MCA for in-order subtargets, and is ignored for other targets.
  bit RetireOOO = false;
  SchedMachineModel SchedModel = ?;
}

// Define the resources and latency of a SchedWrite. This will be used
// directly by targets that have no itinerary classes. In this case,
// SchedWrite is defined by the target, while WriteResources is
// defined by the subtarget, and maps the SchedWrite to processor
// resources.
//
// If a target already has itinerary classes, SchedWriteResources can
// be used instead to define subtarget specific SchedWrites and map
// them to processor resources in one place. Then ItinRW can map
// itinerary classes to the subtarget's SchedWrites.
//
// ProcResources indicates the set of resources consumed by the write.
// Optionally, ReleaseAtCycles indicates the number of cycles the
// resource is consumed. Each ReleaseAtCycles item is paired with the
// ProcResource item at the same position in its list. ReleaseAtCycles
// can be `[]`: in that case, all resources are consumed for a single
// cycle, regardless of latency, which models a fully pipelined processing
// unit. A value of 0 for ReleaseAtCycles means that the resource must
// be available but is not consumed, which is only relevant for
// unbuffered resources.
//
// By default, each SchedWrite takes one micro-op, which is counted
// against the processor's IssueWidth limit. If an instruction can
// write multiple registers with a single micro-op, the subtarget
// should define one of the writes to be zero micro-ops. If a
// subtarget requires multiple micro-ops to write a single result, it
// should either override the write's NumMicroOps to be greater than 1
// or require additional writes. Extra writes can be required either
// by defining a WriteSequence, or simply listing extra writes in the
// instruction's list of writers beyond the number of "def"
// operands. The scheduler assumes that all micro-ops must be
// dispatched in the same cycle. These micro-ops may be required to
// begin or end the current dispatch group.
class WriteRes<SchedWrite write, list<ProcResourceKind> resources>
  : ProcWriteResources<resources> {
  SchedWrite WriteType = write;
}

// Directly name a set of WriteResources defining a new SchedWrite
// type at the same time. This class is unaware of its SchedModel so
// must be referenced by InstRW or ItinRW.
class SchedWriteRes<list<ProcResourceKind> resources> : SchedWrite,
  ProcWriteResources<resources>;

// Define values common to ReadAdvance and SchedReadAdvance.
//
// SchedModel ties these resources to a processor.
class ProcReadAdvance<int cycles, list<SchedWrite> writes = []> {
  int Cycles = cycles;
  list<SchedWrite> ValidWrites = writes;
  // Allow a processor to mark some scheduling classes as unsupported
  // for stronger verification.
  bit Unsupported = false;
  SchedMachineModel SchedModel = ?;
}

// A processor may define a ReadAdvance associated with a SchedRead
// to reduce latency of a prior write by N cycles. A negative advance
// effectively increases latency, which may be used for cross-domain
// stalls.
//
// A ReadAdvance may be associated with a list of SchedWrites
// to implement pipeline bypass. The Writes list may be empty to
// indicate operands that are always read this number of Cycles later
// than a normal register read, allowing the read's parent instruction
// to issue earlier relative to the writer.
class ReadAdvance<SchedRead read, int cycles, list<SchedWrite> writes = []>
  : ProcReadAdvance<cycles, writes> {
  SchedRead ReadType = read;
}

// Directly associate a new SchedRead type with a delay and optional
// pipeline bypass. For use with InstRW or ItinRW.
class SchedReadAdvance<int cycles, list<SchedWrite> writes = []> : SchedRead,
  ProcReadAdvance<cycles, writes>;

// Define SchedRead defaults. Reads seldom need special treatment.
def ReadDefault : SchedRead;
def NoReadAdvance : SchedReadAdvance<0>;

// Define shared code that will be in the same scope as all
// SchedPredicates. Available variables are:
// (const MachineInstr *MI, const TargetSchedModel *SchedModel)
class PredicateProlog<code c> {
  code Code = c;
}

// Base class for scheduling predicates.
class SchedPredicateBase;

// A scheduling predicate whose logic is defined by a MCInstPredicate.
// This can directly be used by SchedWriteVariant definitions.
class MCSchedPredicate<MCInstPredicate P> : SchedPredicateBase {
  MCInstPredicate Pred = P;
  SchedMachineModel SchedModel = ?;
}

// Define a predicate to determine which SchedVariant applies to a
// particular MachineInstr. The code snippet is used as an
// if-statement's expression. Available variables are MI, SchedModel,
// and anything defined in a PredicateProlog.
//
// SchedModel silences warnings but is ignored.
class SchedPredicate<code pred> : SchedPredicateBase {
  SchedMachineModel SchedModel = ?;
  code Predicate = pred;
}

// Define a predicate to be typically used as the default case in a
// SchedVariant.  It the SchedVariant does not use any other predicate based on
// MCSchedPredicate, this is the default scheduling case used by llvm-mca.
def NoSchedPred : MCSchedPredicate<TruePred>;

// Associate a predicate with a list of SchedReadWrites. By default,
// the selected SchedReadWrites are still associated with a single
// operand and assumed to execute sequentially with additive
// latency. However, if the parent SchedWriteVariant or
// SchedReadVariant is marked "Variadic", then each Selected
// SchedReadWrite is mapped in place to the instruction's variadic
// operands. In this case, latency is not additive. If the current Variant
// is already part of a Sequence, then that entire chain leading up to
// the Variant is distributed over the variadic operands.
class SchedVar<SchedPredicateBase pred, list<SchedReadWrite> selected> {
  SchedPredicateBase Predicate = pred;
  list<SchedReadWrite> Selected = selected;
  // SchedModel silences warnings but is ignored.
  SchedMachineModel SchedModel = ?;
}

// SchedModel silences warnings but is ignored.
class SchedVariant<list<SchedVar> variants> {
  list<SchedVar> Variants = variants;
  bit Variadic = false;
  SchedMachineModel SchedModel = ?;
}

// A SchedWriteVariant is a single SchedWrite type that maps to a list
// of SchedWrite types under the conditions defined by its predicates.
//
// A Variadic write is expanded to cover multiple "def" operands. The
// SchedVariant's Expansion list is then interpreted as one write
// per-operand instead of the usual sequential writes feeding a single
// operand.
class SchedWriteVariant<list<SchedVar> variants> : SchedWrite,
  SchedVariant<variants> {
}

// A SchedReadVariant is a single SchedRead type that maps to a list
// of SchedRead types under the conditions defined by its predicates.
//
// A Variadic write is expanded to cover multiple "readsReg" operands as
// explained above.
class SchedReadVariant<list<SchedVar> variants> : SchedRead,
  SchedVariant<variants> {
}

// Map a set of opcodes to a list of SchedReadWrite types. This allows
// the subtarget to easily override specific operations.
//
// SchedModel ties this opcode mapping to a processor.
class InstRW<list<SchedReadWrite> rw, dag instrlist> {
  list<SchedReadWrite> OperandReadWrites = rw;
  dag Instrs = instrlist;
  SchedMachineModel SchedModel = ?;
  // Allow a subtarget to mark some instructions as unsupported.
  bit Unsupported = false;
}

// Map a set of itinerary classes to SchedReadWrite resources. This is
// used to bootstrap a target (e.g. ARM) when itineraries already
// exist and changing InstrInfo is undesirable.
//
// SchedModel ties this ItineraryClass mapping to a processor.
class ItinRW<list<SchedReadWrite> rw, list<InstrItinClass> iic> {
  list<InstrItinClass> MatchedItinClasses = iic;
  list<SchedReadWrite> OperandReadWrites = rw;
  SchedMachineModel SchedModel = ?;
}

// Alias a target-defined SchedReadWrite to a processor specific
// SchedReadWrite. This allows a subtarget to easily map a
// SchedReadWrite type onto a WriteSequence, SchedWriteVariant, or
// SchedReadVariant.
//
// SchedModel will usually be provided by surrounding let statement
// and ties this SchedAlias mapping to a processor.
class SchedAlias<SchedReadWrite match, SchedReadWrite alias> {
  SchedReadWrite MatchRW = match;
  SchedReadWrite AliasRW = alias;
  SchedMachineModel SchedModel = ?;
}

// Allow the definition of processor register files for register renaming
// purposes.
//
// Each processor register file declares:
//  - The set of registers that can be renamed.
//  - The number of physical registers which can be used for register renaming
//    purpose.
//  - The cost of a register rename.
//  - The set of registers that allow move elimination.
//  - The maximum number of moves that can be eliminated every cycle.
//  - Whether move elimination is limited to register moves whose input
//    is known to be zero.
//
// The cost of a rename is the number of physical registers allocated by the
// register alias table to map the new definition. By default, register can be
// renamed at the cost of a single physical register.  Note that register costs
// are defined at register class granularity (see field `Costs`).
//
// The set of registers that are subject to register renaming is declared using
// a list of register classes (see field `RegClasses`). An empty list of
// register classes means: all the logical registers defined by the target can
// be fully renamed.
//
// A register R can be renamed if its register class appears in the `RegClasses`
// set. When R is written, a new alias is allocated at the cost of one or more
// physical registers; as a result, false dependencies on R are removed.
//
// A sub-register V of register R is implicitly part of the same register file.
// However, V is only renamed if its register class is part of `RegClasses`.
// Otherwise, the processor keeps it (as well as any other different part
// of R) together with R, and a write of V always causes a compulsory read of R.
//
// This is what happens for example on AMD processors (at least from Bulldozer
// onwards), where AL and AH are not treated as independent from AX, and AX is
// not treated as independent from EAX. A write to AL has an implicity false
// dependency on the last write to EAX (or a portion of EAX).  As a consequence,
// a write to AL cannot go in parallel with a write to AH.
//
// There is no false dependency if the partial register write belongs to a
// register class that is in `RegClasses`.
// There is also no penalty for writes that "clear the content a super-register"
// (see MC/MCInstrAnalysis.h - method MCInstrAnalysis::clearsSuperRegisters()).
// On x86-64, 32-bit GPR writes implicitly zero the upper half of the underlying
// physical register, effectively removing any false dependencies with the
// previous register definition.
//
// TODO: This implementation assumes that there is no limit in the number of
// renames per cycle, which might not be true for all hardware or register
// classes. Also, there is no limit to how many times the same logical register
// can be renamed during the same cycle.
//
// TODO: we don't currently model merge penalties for the case where a write to
// a part of a register is followed by a read from a larger part of the same
// register. On some Intel chips, different parts of a GPR can be stored in
// different physical registers. However, there is a cost to pay for when the
// partial write is combined with the previous super-register definition.  We
// should add support for these cases, and correctly model merge problems with
// partial register accesses.
//
// Field MaxMovesEliminatedPerCycle specifies how many moves can be eliminated
// every cycle. A default value of zero for that field means: there is no limit
// to the number of moves that can be eliminated by this register file.
//
// An instruction MI is a candidate for move elimination if a call to
// method TargetSubtargetInfo::isOptimizableRegisterMove(MI) returns true (see
// llvm/CodeGen/TargetSubtargetInfo.h, and llvm/MC/MCInstrAnalysis.h).
//
// Subtargets can instantiate tablegen class IsOptimizableRegisterMove (see
// llvm/Target/TargetInstrPredicate.td) to customize the set of move elimination
// candidates. By default, no instruction is a valid move elimination candidate.
//
// A register move MI is eliminated only if:
//  - MI is a move elimination candidate.
//  - The destination register is from a register class that allows move
//    elimination (see field `AllowMoveElimination` below).
//  - Constraints on the move kind, and the maximum number of moves that can be
//    eliminated per cycle are all met.

class RegisterFile<int numPhysRegs, list<RegisterClass> Classes = [],
                   list<int> Costs = [], list<bit> AllowMoveElim = [],
                   int MaxMoveElimPerCy = 0, bit AllowZeroMoveElimOnly = false> {
  list<RegisterClass> RegClasses = Classes;
  list<int> RegCosts = Costs;
  list<bit> AllowMoveElimination = AllowMoveElim;
  int NumPhysRegs = numPhysRegs;
  int MaxMovesEliminatedPerCycle = MaxMoveElimPerCy;
  bit AllowZeroMoveEliminationOnly = AllowZeroMoveElimOnly;
  SchedMachineModel SchedModel = ?;
}

// Describe the retire control unit.
// A retire control unit specifies the size of the reorder buffer, as well as
// the maximum number of opcodes that can be retired every cycle.
// A value less-than-or-equal-to zero for field 'ReorderBufferSize' means: "the
// size is unknown". The idea is that external tools can fall-back to using
// field MicroOpBufferSize in SchedModel if the reorder buffer size is unknown.
// A zero or negative value for field 'MaxRetirePerCycle' means "no
// restrictions on the number of instructions retired per cycle".
// Models can optionally specify up to one instance of RetireControlUnit per
// scheduling model.
class RetireControlUnit<int bufferSize, int retirePerCycle> {
  int ReorderBufferSize = bufferSize;
  int MaxRetirePerCycle = retirePerCycle;
  SchedMachineModel SchedModel = ?;
}

// Base class for Load/StoreQueue.  It is used to identify processor resources
// which describe load/store queues in the LS unit.
class MemoryQueue<ProcResourceKind PR> {
  ProcResourceKind QueueDescriptor = PR;
  SchedMachineModel SchedModel = ?;
}

class LoadQueue<ProcResourceKind LDQueue> : MemoryQueue<LDQueue>;
class StoreQueue<ProcResourceKind STQueue> : MemoryQueue<STQueue>;
