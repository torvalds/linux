//=- X86ScheduleZnver4.td - X86 Znver4 Scheduling ------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Znver4 to support instruction
// scheduling and other instruction cost heuristics.
// Based on:
//  * AMD Software Optimization Guide for AMD Family 19h Processors.
//    https://www.amd.com/system/files/TechDocs/56665.zip
//===----------------------------------------------------------------------===//

def Znver4Model : SchedMachineModel {
  // AMD SOG 19h, 2.9.6 Dispatch
  // The processor may dispatch up to 6 macro ops per cycle
  // into the execution engine.
  let IssueWidth = 6;
  // AMD SOG 19h, 2.10.3
  // The retire control unit (RCU) tracks the completion status of all
  // outstanding operations (integer, load/store, and floating-point) and is
  // the final arbiter for exception processing and recovery.
  // The unit can receive up to 6 macro ops dispatched per cycle and track up
  // to 320 macro ops in-flight in non-SMT mode or 160 per thread in SMT mode.
  let MicroOpBufferSize = 320;
  // AMD SOG 19h, 2.9.1 Op Cache
  // The op cache is organized as an associative cache with 64 sets and 8 ways.
  // At each set-way intersection is an entry containing up to 8 macro ops.
  // The maximum capacity of the op cache is 6.75K ops.
  // Assuming a maximum dispatch of 9 ops/cy and a mispredict cost of 12cy from
  // the op-cache, we limit the loop buffer to 9*12 = 108 to avoid loop
  // unrolling leading to excessive filling of the op-cache from frontend.
  let LoopMicroOpBufferSize = 108;
  // AMD SOG 19h, 2.6.2 L1 Data Cache
  // The L1 data cache has a 4- or 5- cycle integer load-to-use latency.
  // AMD SOG 19h, 2.12 L1 Data Cache
  // The AGU and LS pipelines are optimized for simple address generation modes.
  // <...> and can achieve 4-cycle load-to-use integer load latency.
  let LoadLatency = 4;
  // AMD SOG 19h, 2.12 L1 Data Cache
  // The AGU and LS pipelines are optimized for simple address generation modes.
  // <...> and can achieve <...> 7-cycle load-to-use FP load latency.
  int VecLoadLatency = 7;
  // Latency of a simple store operation.
  int StoreLatency = 1;
  // FIXME:
  let HighLatency = 25; // FIXME: any better choice?
  // AMD SOG 19h, 2.8 Optimizing Branching
  // The branch misprediction penalty is in the range from 11 to 18 cycles,
  // <...>. The common case penalty is 13 cycles.
  let MispredictPenalty = 13;

  let PostRAScheduler = 1; // Enable Post RegAlloc Scheduler pass.

  let CompleteModel = 1;
}

let SchedModel = Znver4Model in {


//===----------------------------------------------------------------------===//
// RCU
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.3 Retire Control Unit
// The unit can receive up to 6 macro ops dispatched per cycle and track up to
// 320 macro ops in-flight in non-SMT mode or 128 per thread in SMT mode. <...>
// The retire unit handles in-order commit of up to nine macro ops per cycle.
def Zn4RCU : RetireControlUnit<Znver4Model.MicroOpBufferSize, 9>;

//===----------------------------------------------------------------------===//
// Integer Execution Unit
//

// AMD SOG 19h, 2.4 Superscalar Organization
// The processor uses four decoupled independent integer scheduler queues,
// each one servicing one ALU pipeline and one or two other pipelines

//
// Execution pipes
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.2 Execution Units
// The processor contains 4 general purpose integer execution pipes.
// Each pipe has an ALU capable of general purpose integer operations.
def Zn4ALU0 : ProcResource<1>;
def Zn4ALU1 : ProcResource<1>;
def Zn4ALU2 : ProcResource<1>;
def Zn4ALU3 : ProcResource<1>;

// AMD SOG 19h, 2.10.2 Execution Units
// There is also a separate branch execution unit.
def Zn4BRU1 : ProcResource<1>;

// AMD SOG 19h, 2.10.2 Execution Units
// There are three Address Generation Units (AGUs) for all load and store
// address generation. There are also 3 store data movement units
// associated with the same schedulers as the AGUs.
def Zn4AGU0 : ProcResource<1>;
def Zn4AGU1 : ProcResource<1>;
def Zn4AGU2 : ProcResource<1>;

//
// Execution Units
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.2 Execution Units
// ALU0 additionally has divide <...> execution capability.
defvar Zn4Divider = Zn4ALU0;

// AMD SOG 19h, 2.10.2 Execution Units
// ALU0 additionally has <...> branch execution capability.
defvar Zn4BRU0 = Zn4ALU0;

// Integer Multiplication issued on ALU1.
defvar Zn4Multiplier = Zn4ALU1;

// Execution pipeline grouping
//===----------------------------------------------------------------------===//

// General ALU operations
def Zn4ALU0123 : ProcResGroup<[Zn4ALU0, Zn4ALU1, Zn4ALU2, Zn4ALU3]>;

// General AGU operations
def Zn4AGU012 : ProcResGroup<[Zn4AGU0, Zn4AGU1, Zn4AGU2]>;

// Control flow: jumps, calls
def Zn4BRU01 : ProcResGroup<[Zn4BRU0, Zn4BRU1]>;

// Everything that isn't control flow, but still needs to access CC register,
// namely: conditional moves, SETcc.
def Zn4ALU03 : ProcResGroup<[Zn4ALU0, Zn4ALU3]>;

// Zn4ALU1 handles complex bit twiddling: CRC/PDEP/PEXT

// Simple bit twiddling: bit test, shift/rotate, bit extraction
def Zn4ALU12 : ProcResGroup<[Zn4ALU1, Zn4ALU2]>;


//
// Scheduling
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.3 Retire Control Unit
// The integer physical register file (PRF) consists of 224 registers.
def Zn4IntegerPRF : RegisterFile<224, [GR64, CCR], [1, 1], [1, 0],
                              6,  // Max moves that can be eliminated per cycle.
                              0>; // Restrict move elimination to zero regs.

// anandtech, The integer scheduler has a 4*24 entry macro op capacity.
// AMD SOG 19h, 2.10.1 Schedulers
// The schedulers can receive up to six macro ops per cycle, with a limit of
// two per scheduler. Each scheduler can issue one micro op per cycle into
// each of its associated pipelines
def Zn4Int : ProcResGroup<[Zn4ALU0, Zn4AGU0, Zn4BRU0, // scheduler 0
                           Zn4ALU1, Zn4AGU1,          // scheduler 1
                           Zn4ALU2, Zn4AGU2,          // scheduler 2
                           Zn4ALU3,          Zn4BRU1  // scheduler 3
                          ]> {
  let BufferSize = !mul(4, 24);
}


//===----------------------------------------------------------------------===//
// Floating-Point Unit
//

// AMD SOG 19h, 2.4 Superscalar Organization
// The processor uses <...> two decoupled independent floating point schedulers
// each servicing two FP pipelines and one store or FP-to-integer pipeline.

//
// Execution pipes
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.1 Schedulers
// <...>, and six FPU pipes.
// Agner, 22.10 Floating point execution pipes
// There are six floating point/vector execution pipes,
def Zn4FP0  : ProcResource<1>;
def Zn4FP1  : ProcResource<1>;
def Zn4FP2  : ProcResource<1>;
def Zn4FP3  : ProcResource<1>;
def Zn4FP45 : ProcResource<2>;

//
// Execution Units
//===----------------------------------------------------------------------===//
// AMD SOG 19h, 2.11.1 Floating Point Execution Resources

// (v)FMUL*, (v)FMA*, Floating Point Compares, Blendv(DQ)
defvar Zn4FPFMul0 = Zn4FP0;
defvar Zn4FPFMul1 = Zn4FP1;

// (v)FADD*
defvar Zn4FPFAdd0 = Zn4FP2;
defvar Zn4FPFAdd1 = Zn4FP3;

// All convert operations except pack/unpack
defvar Zn4FPFCvt0 = Zn4FP2;
defvar Zn4FPFCvt1 = Zn4FP3;

// All Divide and Square Root except Reciprocal Approximation
// AMD SOG 19h, 2.11.1 Floating Point Execution Resources
// FDIV unit can support 2 simultaneous operations in flight
// even though it occupies a single pipe.
// FIXME: BufferSize=2 ?
defvar Zn4FPFDiv = Zn4FP1;

// Moves and Logical operations on Floating Point Data Types
defvar Zn4FPFMisc0 = Zn4FP0;
defvar Zn4FPFMisc1 = Zn4FP1;
defvar Zn4FPFMisc2 = Zn4FP2;
defvar Zn4FPFMisc3 = Zn4FP3;

// Integer Adds, Subtracts, and Compares
// Some complex VADD operations are not available in all pipes.
defvar Zn4FPVAdd0 = Zn4FP0;
defvar Zn4FPVAdd1 = Zn4FP1;
defvar Zn4FPVAdd2 = Zn4FP2;
defvar Zn4FPVAdd3 = Zn4FP3;

// Integer Multiplies, SAD, Blendvb
defvar Zn4FPVMul0 = Zn4FP0;
defvar Zn4FPVMul1 = Zn4FP3;

// Data Shuffles, Packs, Unpacks, Permute
// Some complex shuffle operations are only available in pipe1.
defvar Zn4FPVShuf = Zn4FP1;
defvar Zn4FPVShufAux = Zn4FP2;

// Bit Shift Left/Right operations
defvar Zn4FPVShift0 = Zn4FP1;
defvar Zn4FPVShift1 = Zn4FP2;

// Moves and Logical operations on Packed Integer Data Types
defvar Zn4FPVMisc0 = Zn4FP0;
defvar Zn4FPVMisc1 = Zn4FP1;
defvar Zn4FPVMisc2 = Zn4FP2;
defvar Zn4FPVMisc3 = Zn4FP3;

// *AES*
defvar Zn4FPAES0 = Zn4FP0;
defvar Zn4FPAES1 = Zn4FP1;

// *CLM*
defvar Zn4FPCLM0 = Zn4FP0;
defvar Zn4FPCLM1 = Zn4FP1;

// Execution pipeline grouping
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.11 Floating-Point Unit
// Stores and floating point to general purpose register transfer
// have 2 dedicated pipelines (pipe 5 and 6).
def Zn4FPU0123 : ProcResGroup<[Zn4FP0, Zn4FP1, Zn4FP2, Zn4FP3]>;

// (v)FMUL*, (v)FMA*, Floating Point Compares, Blendv(DQ)
def Zn4FPFMul01 : ProcResGroup<[Zn4FPFMul0, Zn4FPFMul1]>;

// (v)FADD*
// Some complex VADD operations are not available in all pipes.
def Zn4FPFAdd01 : ProcResGroup<[Zn4FPFAdd0, Zn4FPFAdd1]>;

// All convert operations except pack/unpack
def Zn4FPFCvt01 : ProcResGroup<[Zn4FPFCvt0, Zn4FPFCvt1]>;

// All Divide and Square Root except Reciprocal Approximation
// def Zn4FPFDiv : ProcResGroup<[Zn4FPFDiv]>;

// Moves and Logical operations on Floating Point Data Types
def Zn4FPFMisc0123 : ProcResGroup<[Zn4FPFMisc0, Zn4FPFMisc1, Zn4FPFMisc2, Zn4FPFMisc3]>;

// FIXUP and RANGE use FP01 pipelines
def Zn4FPFMisc01 : ProcResGroup<[Zn4FPFMisc0, Zn4FPFMisc1]>;
def Zn4FPFMisc12 : ProcResGroup<[Zn4FPFMisc1, Zn4FPFMisc2]>;
// SCALE instructions use FP23 pipelines
def Zn4FPFMisc23 : ProcResGroup<[Zn4FPFMisc2, Zn4FPFMisc3]>;
def Zn4FPFMisc123 : ProcResGroup<[Zn4FPFMisc1,Zn4FPFMisc2, Zn4FPFMisc3]>;

// Loads, Stores and Move to General Register (EX) Operations
// AMD SOG 19h, 2.11 Floating-Point Unit
// Stores and floating point to general purpose register transfer
// have 2 dedicated pipelines (pipe 5 and 6).
defvar Zn4FPLd01 = Zn4FP45;

// AMD SOG 19h, 2.11 Floating-Point Unit
// Note that FP stores are supported on two pipelines,
// but throughput is limited to one per cycle.
let Super = Zn4FP45 in
def Zn4FPSt : ProcResource<1>;

// Integer Adds, Subtracts, and Compares
// Some complex VADD operations are not available in all pipes.
def Zn4FPVAdd0123 : ProcResGroup<[Zn4FPVAdd0, Zn4FPVAdd1, Zn4FPVAdd2, Zn4FPVAdd3]>;

def Zn4FPVAdd01: ProcResGroup<[Zn4FPVAdd0, Zn4FPVAdd1]>;
def Zn4FPVAdd12: ProcResGroup<[Zn4FPVAdd1, Zn4FPVAdd2]>;

// AVX512 Opmask pipelines
def Zn4FPOpMask01: ProcResGroup<[Zn4FP2, Zn4FP3]>;
def Zn4FPOpMask4: ProcResGroup<[Zn4FP45]>;

// Integer Multiplies, SAD, Blendvb
def Zn4FPVMul01 : ProcResGroup<[Zn4FPVMul0, Zn4FPVMul1]>;

// Data Shuffles, Packs, Unpacks, Permute
// Some complex shuffle operations are only available in pipe1.
def Zn4FPVShuf01 : ProcResGroup<[Zn4FPVShuf, Zn4FPVShufAux]>;

// Bit Shift Left/Right operations
def Zn4FPVShift01 : ProcResGroup<[Zn4FPVShift0, Zn4FPVShift1]>;

// Moves and Logical operations on Packed Integer Data Types
def Zn4FPVMisc0123 : ProcResGroup<[Zn4FPVMisc0, Zn4FPVMisc1, Zn4FPVMisc2, Zn4FPVMisc3]>;

// *AES*
def Zn4FPAES01 : ProcResGroup<[Zn4FPAES0, Zn4FPAES1]>;

// *CLM*
def Zn4FPCLM01 : ProcResGroup<[Zn4FPCLM0, Zn4FPCLM1]>;


//
// Scheduling
//===----------------------------------------------------------------------===//

// Agner, 21.8 Register renaming and out-of-order schedulers
// The floating point register file has 192 vector registers
// of 512b each in zen4.
def Zn4FpPRF : RegisterFile<192, [VR64, VR128, VR256, VR512], [1, 1, 1, 1], [0, 1, 1],
                            6,  // Max moves that can be eliminated per cycle.
                            0>; // Restrict move elimination to zero regs.

// AMD SOG 19h, 2.11 Floating-Point Unit
// The floating-point scheduler has a 2*32 entry macro op capacity.
// AMD SOG 19h, 2.11 Floating-Point Unit
// <...> the scheduler can issue 1 micro op per cycle for each pipe.
// FIXME: those are two separate schedulers, not a single big one.
def Zn4FP : ProcResGroup<[Zn4FP0, Zn4FP2,          /*Zn4FP4,*/ // scheduler 0
                          Zn4FP1, Zn4FP3, Zn4FP45 /*Zn4FP5*/  // scheduler 1
                         ]> {
  let BufferSize = !mul(2, 32);
}

// AMD SOG 19h, 2.11 Floating-Point Unit
// Macro ops can be dispatched to the 64 entry Non Scheduling Queue (NSQ)
// even if floating-point scheduler is full.
// FIXME: how to model this properly?


//===----------------------------------------------------------------------===//
// Load-Store Unit
//

// AMD SOG 19h, 2.12 Load-Store Unit
// The LS unit contains three largely independent pipe-lines
// enabling the execution of three 256-bit memory operations per cycle.
def Zn4LSU : ProcResource<3>;

// AMD SOG 19h, 2.12 Load-Store Unit
// All three memory operations can be loads.
let Super = Zn4LSU in
def Zn4Load : ProcResource<3> {
  // AMD SOG 19h, 2.12 Load-Store Unit
  // The LS unit can process up to 72 out-of-order loads.
  let BufferSize = 72;
}

def Zn4LoadQueue : LoadQueue<Zn4Load>;

// AMD SOG 19h, 2.12 Load-Store Unit
// A maximum of two of the memory operations can be stores.
let Super = Zn4LSU in
def Zn4Store : ProcResource<2> {
  // AMD SOG 19h, 2.12 Load-Store Unit
  // The LS unit utilizes a 64-entry store queue (STQ).
  let BufferSize = 64;
}

def Zn4StoreQueue : StoreQueue<Zn4Store>;

//===----------------------------------------------------------------------===//
// Basic helper classes.
//===----------------------------------------------------------------------===//

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when dispatched by the schedulers.
// This multiclass defines the resource usage for variants with and without
// folded loads.

multiclass __Zn4WriteRes<SchedWrite SchedRW, list<ProcResourceKind> ExePorts,
                         int Lat = 1, list<int> Res = [], int UOps = 1> {
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }
}

multiclass __Zn4WriteResPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat,
                             list<int> Res, int UOps, int LoadLat, int LoadUOps,
                             ProcResourceKind AGU, int LoadRes> {
  defm : __Zn4WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;

  defm : __Zn4WriteRes<SchedRW.Folded,
                       !listconcat([AGU, Zn4Load], ExePorts),
                       !add(Lat, LoadLat),
                       !if(!and(!empty(Res), !eq(LoadRes, 1)),
                         [],
                         !listconcat([1, LoadRes],
                           !if(!empty(Res),
                             !listsplat(1, !size(ExePorts)),
                             Res))),
                       !add(UOps, LoadUOps)>;
}

// For classes without folded loads.
multiclass Zn4WriteResInt<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __Zn4WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

multiclass Zn4WriteResXMM<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __Zn4WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

multiclass Zn4WriteResYMM<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __Zn4WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

multiclass Zn4WriteResZMM<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __Zn4WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

// For classes with folded loads.
multiclass Zn4WriteResIntPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __Zn4WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver4Model.LoadLatency,
                           LoadUOps, Zn4AGU012, LoadRes>;
}

multiclass Zn4WriteResXMMPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __Zn4WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver4Model.VecLoadLatency,
                           LoadUOps, Zn4FPLd01, LoadRes>;
}

multiclass Zn4WriteResYMMPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __Zn4WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver4Model.VecLoadLatency,
                           LoadUOps, Zn4FPLd01, LoadRes>;
}

multiclass Zn4WriteResZMMPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 2,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __Zn4WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver4Model.VecLoadLatency,
                           LoadUOps, Zn4FPLd01, LoadRes>;
}

//===----------------------------------------------------------------------===//
// Here be dragons.
//===----------------------------------------------------------------------===//

def : ReadAdvance<ReadAfterLd, Znver4Model.LoadLatency>;

def : ReadAdvance<ReadAfterVecLd, Znver4Model.VecLoadLatency>;
def : ReadAdvance<ReadAfterVecXLd, Znver4Model.VecLoadLatency>;
def : ReadAdvance<ReadAfterVecYLd, Znver4Model.VecLoadLatency>;

// AMD SOG 19h, 2.11 Floating-Point Unit
// There is 1 cycle of added latency for a result to cross
// from F to I or I to F domain.
def : ReadAdvance<ReadInt2Fpu, -1>;

// Instructions with both a load and a store folded are modeled as a folded
// load + WriteRMW.
defm : Zn4WriteResInt<WriteRMW, [Zn4AGU012, Zn4Store], Znver4Model.StoreLatency, [1, 1], 0>;

// Loads, stores, and moves, not folded with other operations.
defm : Zn4WriteResInt<WriteLoad, [Zn4AGU012, Zn4Load], !add(Znver4Model.LoadLatency, 1), [1, 1], 1>;

// Model the effect of clobbering the read-write mask operand of the GATHER operation.
// Does not cost anything by itself, only has latency, matching that of the WriteLoad,
defm : Zn4WriteResInt<WriteVecMaskedGatherWriteback, [], !add(Znver4Model.LoadLatency, 1), [], 0>;

def Zn4WriteMOVSlow : SchedWriteRes<[Zn4AGU012, Zn4Load]> {
  let Latency = !add(Znver4Model.LoadLatency, 1);
  let ReleaseAtCycles = [3, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteMOVSlow], (instrs MOV8rm, MOV8rm_NOREX, MOV16rm, MOVSX16rm16, MOVSX16rm32, MOVZX16rm16, MOVSX16rm8, MOVZX16rm8)>;

defm : Zn4WriteResInt<WriteStore, [Zn4AGU012, Zn4Store], Znver4Model.StoreLatency, [1, 2], 1>;
defm : Zn4WriteResInt<WriteStoreNT, [Zn4AGU012, Zn4Store], Znver4Model.StoreLatency, [1, 2], 1>;
defm : Zn4WriteResInt<WriteMove, [Zn4ALU0123], 1, [4], 1>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

def Zn4WriteMOVBE16rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU0123]> {
  let Latency = Znver4Model.LoadLatency;
  let ReleaseAtCycles = [1, 1, 4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteMOVBE16rm], (instrs MOVBE16rm)>;

def Zn4WriteMOVBEmr : SchedWriteRes<[Zn4ALU0123, Zn4AGU012, Zn4Store]> {
  let Latency = Znver4Model.StoreLatency;
  let ReleaseAtCycles = [4, 1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteMOVBEmr], (instrs MOVBE16mr, MOVBE32mr, MOVBE64mr)>;

// Arithmetic.
defm : Zn4WriteResIntPair<WriteALU, [Zn4ALU0123], 1, [1], 1>; // Simple integer ALU op.

def Zn4WriteALUSlow : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteALUSlow], (instrs ADD8i8, ADD16i16, ADD32i32, ADD64i32,
                                        AND8i8, AND16i16, AND32i32, AND64i32,
                                         OR8i8,  OR16i16,  OR32i32,  OR64i32,
                                        SUB8i8, SUB16i16, SUB32i32, SUB64i32,
                                        XOR8i8, XOR16i16, XOR32i32, XOR64i32)>;

def Zn4WriteMoveExtend : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteMoveExtend], (instrs MOVSX16rr16, MOVSX16rr32, MOVZX16rr16, MOVSX16rr8, MOVZX16rr8)>;

def Zn4WriteMaterialize32bitImm: SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteMaterialize32bitImm], (instrs MOV32ri, MOV32ri_alt, MOV64ri32)>;

def Zn4WritePDEP_PEXT : SchedWriteRes<[Zn4ALU1]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WritePDEP_PEXT], (instrs PDEP32rr, PDEP64rr,
                                          PEXT32rr, PEXT64rr)>;

defm : Zn4WriteResIntPair<WriteADC, [Zn4ALU0123], 1, [4], 1>; // Integer ALU + flags op.

def Zn4WriteADC8mr_SBB8mr : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU0123, Zn4Store]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 1, 7, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteADC8mr_SBB8mr], (instrs ADC8mr, SBB8mr)>;

// This is for simple LEAs with one or two input operands.
defm : Zn4WriteResInt<WriteLEA, [Zn4AGU012], 1, [1], 1>;     // LEA instructions can't fold loads.

// This write is used for slow LEA instructions.
def Zn4Write3OpsLEA : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}

// On Znver4, a slow LEA is either a 3Ops LEA (base, index, offset),
// or an LEA with a `Scale` value different than 1.
def Zn4SlowLEAPredicate : MCSchedPredicate<
  CheckAny<[
    // A 3-operand LEA (base, index, offset).
    IsThreeOperandsLEAFn,
    // An LEA with a "Scale" different than 1.
    CheckAll<[
      CheckIsImmOperand<2>,
      CheckNot<CheckImmOperand<2, 1>>
    ]>
  ]>
>;

def Zn4WriteLEA : SchedWriteVariant<[
    SchedVar<Zn4SlowLEAPredicate, [Zn4Write3OpsLEA]>,
    SchedVar<NoSchedPred,         [WriteLEA]>
]>;

def : InstRW<[Zn4WriteLEA], (instrs LEA32r, LEA64r, LEA64_32r)>;

def Zn4SlowLEA16r : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 2; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [4];
  let NumMicroOps = 2;
}

def : InstRW<[Zn4SlowLEA16r], (instrs LEA16r)>;

// Integer multiplication
defm : Zn4WriteResIntPair<WriteIMul8, [Zn4Multiplier], 3, [3], 1>; // Integer 8-bit multiplication.
defm : Zn4WriteResIntPair<WriteIMul16, [Zn4Multiplier], 3, [3], 3, /*LoadUOps=*/1>; // Integer 16-bit multiplication.
defm : Zn4WriteResIntPair<WriteIMul16Imm, [Zn4Multiplier], 4, [4], 2>; // Integer 16-bit multiplication by immediate.
defm : Zn4WriteResIntPair<WriteIMul16Reg, [Zn4Multiplier], 3, [1], 1>; // Integer 16-bit multiplication by register.
defm : Zn4WriteResIntPair<WriteIMul32, [Zn4Multiplier], 3, [3], 2>;    // Integer 32-bit multiplication.
defm : Zn4WriteResIntPair<WriteMULX32, [Zn4Multiplier], 3, [1], 2>;    // Integer 32-bit Unsigned Multiply Without Affecting Flags.
defm : Zn4WriteResIntPair<WriteIMul32Imm, [Zn4Multiplier], 3, [1], 1>; // Integer 32-bit multiplication by immediate.
defm : Zn4WriteResIntPair<WriteIMul32Reg, [Zn4Multiplier], 3, [1], 1>; // Integer 32-bit multiplication by register.
defm : Zn4WriteResIntPair<WriteIMul64, [Zn4Multiplier], 3, [3], 2>;    // Integer 64-bit multiplication.
defm : Zn4WriteResIntPair<WriteMULX64, [Zn4Multiplier], 3, [1], 2>;    // Integer 32-bit Unsigned Multiply Without Affecting Flags.
defm : Zn4WriteResIntPair<WriteIMul64Imm, [Zn4Multiplier], 3, [1], 1>; // Integer 64-bit multiplication by immediate.
defm : Zn4WriteResIntPair<WriteIMul64Reg, [Zn4Multiplier], 3, [1], 1>; // Integer 64-bit multiplication by register.
defm : Zn4WriteResInt<WriteIMulHLd, [], !add(4, Znver4Model.LoadLatency), [], 0>;  // Integer multiplication, high part.
defm : Zn4WriteResInt<WriteIMulH, [], 4, [], 0>;  // Integer multiplication, high part.

defm : Zn4WriteResInt<WriteBSWAP32, [Zn4ALU0123], 1, [1], 1>; // Byte Order (Endianness) 32-bit Swap.
defm : Zn4WriteResInt<WriteBSWAP64, [Zn4ALU0123], 1, [1], 1>; // Byte Order (Endianness) 64-bit Swap.

defm : Zn4WriteResIntPair<WriteCMPXCHG, [Zn4ALU0123], 3, [12], 5>; // Compare and set, compare and swap.

def Zn4WriteCMPXCHG8rr : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 3;
  let ReleaseAtCycles = [12];
  let NumMicroOps = 3;
}
def : InstRW<[Zn4WriteCMPXCHG8rr], (instrs CMPXCHG8rr)>;

defm : Zn4WriteResInt<WriteCMPXCHGRMW, [Zn4ALU0123], 3, [12], 6>;     // Compare and set, compare and swap.

def Zn4WriteCMPXCHG8rm_LCMPXCHG8 : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteCMPXCHG8rr.Latency);
  let ReleaseAtCycles = [1, 1, 12];
  let NumMicroOps = !add(Zn4WriteCMPXCHG8rr.NumMicroOps, 2);
}
def : InstRW<[Zn4WriteCMPXCHG8rm_LCMPXCHG8], (instrs CMPXCHG8rm, LCMPXCHG8)>;

def Zn4WriteCMPXCHG8B : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 3; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [24];
  let NumMicroOps = 19;
}
def : InstRW<[Zn4WriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def Zn4WriteCMPXCHG16B_LCMPXCHG16B : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 4; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [59];
  let NumMicroOps = 28;
}
def : InstRW<[Zn4WriteCMPXCHG16B_LCMPXCHG16B], (instrs CMPXCHG16B, LCMPXCHG16B)>;

def Zn4WriteWriteXCHGUnrenameable : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteWriteXCHGUnrenameable], (instrs XCHG8rr, XCHG16rr, XCHG16ar)>;

def Zn4WriteXCHG8rm_XCHG16rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, 3); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 5;
}
def : InstRW<[Zn4WriteXCHG8rm_XCHG16rm], (instrs XCHG8rm, XCHG16rm)>;

def Zn4WriteXCHG32rm_XCHG64rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, 2); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteXCHG32rm_XCHG64rm], (instrs XCHG32rm, XCHG64rm)>;

// Integer division.
// FIXME: uops for 8-bit division measures as 2. for others it's a guess.
// FIXME: latency for 8-bit division measures as 10. for others it's a guess.
defm : Zn4WriteResIntPair<WriteDiv8, [Zn4Divider], 10, [10], 2>;
defm : Zn4WriteResIntPair<WriteDiv16, [Zn4Divider], 11, [11], 2>;
defm : Zn4WriteResIntPair<WriteDiv32, [Zn4Divider], 13, [13], 2>;
defm : Zn4WriteResIntPair<WriteDiv64, [Zn4Divider], 17, [17], 2>;
defm : Zn4WriteResIntPair<WriteIDiv8, [Zn4Divider], 10, [10], 2>;
defm : Zn4WriteResIntPair<WriteIDiv16, [Zn4Divider], 11, [11], 2>;
defm : Zn4WriteResIntPair<WriteIDiv32, [Zn4Divider], 13, [13], 2>;
defm : Zn4WriteResIntPair<WriteIDiv64, [Zn4Divider], 17, [17], 2>;

defm : Zn4WriteResIntPair<WriteBSF, [Zn4ALU1], 1, [1], 6, /*LoadUOps=*/1>; // Bit scan forward.
defm : Zn4WriteResIntPair<WriteBSR, [Zn4ALU1], 1, [1], 6, /*LoadUOps=*/1>; // Bit scan reverse.

defm : Zn4WriteResIntPair<WritePOPCNT, [Zn4ALU0123], 1, [1], 1>; // Bit population count.

def Zn4WritePOPCNT16rr : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WritePOPCNT16rr], (instrs POPCNT16rr)>;

defm : Zn4WriteResIntPair<WriteLZCNT, [Zn4ALU0123], 1, [1], 1>; // Leading zero count.

def Zn4WriteLZCNT16rr : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteLZCNT16rr], (instrs LZCNT16rr)>;

defm : Zn4WriteResIntPair<WriteTZCNT, [Zn4ALU12], 2, [1], 2>; // Trailing zero count.

def Zn4WriteTZCNT16rr : SchedWriteRes<[Zn4ALU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteTZCNT16rr], (instrs TZCNT16rr)>;

defm : Zn4WriteResIntPair<WriteCMOV, [Zn4ALU03], 1, [1], 1>; // Conditional move.
defm : Zn4WriteResInt<WriteFCMOV, [Zn4ALU0123], 7, [28], 7>; // FIXME: not from llvm-exegesis // X87 conditional move.
defm : Zn4WriteResInt<WriteSETCC, [Zn4ALU03], 1, [2], 1>; // Set register based on condition code.
defm : Zn4WriteResInt<WriteSETCCStore, [Zn4ALU03, Zn4AGU012, Zn4Store], 2, [2, 1, 1], 2>; // FIXME: latency not from llvm-exegesis
defm : Zn4WriteResInt<WriteLAHFSAHF, [Zn4ALU3], 1, [1], 1>; // Load/Store flags in AH.

defm : Zn4WriteResInt<WriteBitTest, [Zn4ALU12], 1, [1], 1>; // Bit Test
defm : Zn4WriteResInt<WriteBitTestImmLd, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 1), [1, 1, 1], 2>;
defm : Zn4WriteResInt<WriteBitTestRegLd, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 1), [1, 1, 1], 7>;

defm : Zn4WriteResInt<WriteBitTestSet, [Zn4ALU12], 2, [2], 2>; // Bit Test + Set
defm : Zn4WriteResInt<WriteBitTestSetImmLd, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 2), [1, 1, 1], 4>;
defm : Zn4WriteResInt<WriteBitTestSetRegLd, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 2), [1, 1, 1], 9>;

// Integer shifts and rotates.
defm : Zn4WriteResIntPair<WriteShift, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn4WriteResIntPair<WriteShiftCL, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn4WriteResIntPair<WriteRotate, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;

def Zn4WriteRotateR1 : SchedWriteRes<[Zn4ALU12]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteRotateR1], (instrs RCL8r1, RCL16r1, RCL32r1, RCL64r1,
                                         RCR8r1, RCR16r1, RCR32r1, RCR64r1)>;

def Zn4WriteRotateM1 : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU12]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteRotateR1.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteRotateR1.NumMicroOps, 1);
}
def : InstRW<[Zn4WriteRotateM1], (instrs RCL8m1, RCL16m1, RCL32m1, RCL64m1,
                                         RCR8m1, RCR16m1, RCR32m1, RCR64m1)>;

def Zn4WriteRotateRightRI : SchedWriteRes<[Zn4ALU12]> {
  let Latency = 3;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 7;
}
def : InstRW<[Zn4WriteRotateRightRI], (instrs RCR8ri, RCR16ri, RCR32ri, RCR64ri)>;

def Zn4WriteRotateRightMI : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU12]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteRotateRightRI.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn4WriteRotateRightRI.NumMicroOps, 3);
}
def : InstRW<[Zn4WriteRotateRightMI], (instrs RCR8mi, RCR16mi, RCR32mi, RCR64mi)>;

def Zn4WriteRotateLeftRI : SchedWriteRes<[Zn4ALU12]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 9;
}
def : InstRW<[Zn4WriteRotateLeftRI], (instrs RCL8ri, RCL16ri, RCL32ri, RCL64ri)>;

def Zn4WriteRotateLeftMI : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU12]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteRotateLeftRI.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn4WriteRotateLeftRI.NumMicroOps, 2);
}
def : InstRW<[Zn4WriteRotateLeftMI], (instrs RCL8mi, RCL16mi, RCL32mi, RCL64mi)>;

defm : Zn4WriteResIntPair<WriteRotateCL, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;

def Zn4WriteRotateRightRCL : SchedWriteRes<[Zn4ALU12]> {
  let Latency = 3;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 7;
}
def : InstRW<[Zn4WriteRotateRightRCL], (instrs RCR8rCL, RCR16rCL, RCR32rCL, RCR64rCL)>;

def Zn4WriteRotateRightMCL : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU12]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteRotateRightRCL.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn4WriteRotateRightRCL.NumMicroOps, 2);
}
def : InstRW<[Zn4WriteRotateRightMCL], (instrs RCR8mCL, RCR16mCL, RCR32mCL, RCR64mCL)>;

def Zn4WriteRotateLeftRCL : SchedWriteRes<[Zn4ALU12]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 9;
}
def : InstRW<[Zn4WriteRotateLeftRCL], (instrs RCL8rCL, RCL16rCL, RCL32rCL, RCL64rCL)>;

def Zn4WriteRotateLeftMCL : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4ALU12]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteRotateLeftRCL.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn4WriteRotateLeftRCL.NumMicroOps, 2);
}
def : InstRW<[Zn4WriteRotateLeftMCL], (instrs RCL8mCL, RCL16mCL, RCL32mCL, RCL64mCL)>;

// Double shift instructions.
defm : Zn4WriteResInt<WriteSHDrri, [Zn4ALU12], 2, [3], 4>;
defm : Zn4WriteResInt<WriteSHDrrcl, [Zn4ALU12], 2, [3], 5>;
defm : Zn4WriteResInt<WriteSHDmri, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 2), [1, 1, 4], 6>;
defm : Zn4WriteResInt<WriteSHDmrcl, [Zn4AGU012, Zn4Load, Zn4ALU12], !add(Znver4Model.LoadLatency, 2), [1, 1, 4], 6>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : Zn4WriteResIntPair<WriteBEXTR, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn4WriteResIntPair<WriteBLS, [Zn4ALU0123], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn4WriteResIntPair<WriteBZHI, [Zn4ALU12], 1, [1], 1, /*LoadUOps=*/1>;

// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
defm : Zn4WriteResInt<WriteZero, [Zn4ALU0123], 0, [0], 1>;

// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
defm : Zn4WriteResIntPair<WriteJump, [Zn4BRU01], 1, [1], 1>; // FIXME: not from llvm-exegesis

// Floating point. This covers both scalar and vector operations.
defm : Zn4WriteResInt<WriteFLD0, [Zn4FPLd01, Zn4Load, Zn4FP1], !add(Znver4Model.LoadLatency, 4), [1, 1, 1], 1>;
defm : Zn4WriteResInt<WriteFLD1, [Zn4FPLd01, Zn4Load, Zn4FP1], !add(Znver4Model.LoadLatency, 7), [1, 1, 1], 1>;
defm : Zn4WriteResInt<WriteFLDC, [Zn4FPLd01, Zn4Load, Zn4FP1], !add(Znver4Model.LoadLatency, 7), [1, 1, 1], 1>;
defm : Zn4WriteResXMM<WriteFLoad, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteFLoadX, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResYMM<WriteFLoadY, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteFMaskedLoad, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResYMM<WriteFMaskedLoadY, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteFStore, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;

def Zn4WriteWriteFStoreMMX : SchedWriteRes<[Zn4FPSt, Zn4Store]> {
  let Latency = 2; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteWriteFStoreMMX], (instrs MOVHPDmr,  MOVHPSmr,
                                               VMOVHPDmr, VMOVHPSmr)>;

defm : Zn4WriteResXMM<WriteFStoreX, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResYMM<WriteFStoreY, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResXMM<WriteFStoreNT, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResXMM<WriteFStoreNTX, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResYMM<WriteFStoreNTY, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;

defm : Zn4WriteResXMM<WriteFMaskedStore32, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [6, 1], 18>;
defm : Zn4WriteResXMM<WriteFMaskedStore64, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [4, 1], 10>;
defm : Zn4WriteResYMM<WriteFMaskedStore32Y, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [12, 1], 42>;
defm : Zn4WriteResYMM<WriteFMaskedStore64Y, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [6, 1], 18>;

defm : Zn4WriteResXMMPair<WriteFAdd, [Zn4FPFAdd01], 3, [1], 1>;  // Floating point add/sub.

def Zn4WriteX87Arith : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, 1); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 24];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteX87Arith], (instrs ADD_FI16m, ADD_FI32m,
                                         SUB_FI16m, SUB_FI32m,
                                         SUBR_FI16m, SUBR_FI32m,
                                         MUL_FI16m, MUL_FI32m)>;

def Zn4WriteX87Div : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, 1); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 62];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteX87Div], (instrs DIV_FI16m, DIV_FI32m,
                                       DIVR_FI16m, DIVR_FI32m)>;

defm : Zn4WriteResXMMPair<WriteFAddX, [Zn4FPFAdd01], 3, [1], 1>; // Floating point add/sub (XMM).
defm : Zn4WriteResYMMPair<WriteFAddY, [Zn4FPFAdd01], 3, [1], 1>; // Floating point add/sub (YMM).
defm : Zn4WriteResZMMPair<WriteFAddZ, [Zn4FPFAdd01], 3, [2], 1>; // Floating point add/sub (ZMM).
defm : Zn4WriteResXMMPair<WriteFAdd64, [Zn4FPFAdd01], 3, [1], 1>;  // Floating point double add/sub.
defm : Zn4WriteResXMMPair<WriteFAdd64X, [Zn4FPFAdd01], 3, [1], 1>; // Floating point double add/sub (XMM).
defm : Zn4WriteResYMMPair<WriteFAdd64Y, [Zn4FPFAdd01], 3, [1], 1>; // Floating point double add/sub (YMM).
defm : Zn4WriteResZMMPair<WriteFAdd64Z, [Zn4FPFAdd01], 3, [2], 1>; // Floating point double add/sub (ZMM).
defm : Zn4WriteResXMMPair<WriteFCmp, [Zn4FPFMul01], 2, [2], 1>;  // Floating point compare.
defm : Zn4WriteResXMMPair<WriteFCmpX, [Zn4FPFMul01], 2, [1], 1>; // Floating point compare (XMM).
defm : Zn4WriteResYMMPair<WriteFCmpY, [Zn4FPFMul01], 2, [1], 1>; // Floating point compare (YMM).
defm : Zn4WriteResZMMPair<WriteFCmpZ, [Zn4FPFMul01], 2, [2], 1>; // Floating point compare (ZMM).
defm : Zn4WriteResXMMPair<WriteFCmp64, [Zn4FPFMul01], 1, [1], 1>;  // Floating point double compare.
defm : Zn4WriteResXMMPair<WriteFCmp64X, [Zn4FPFMul01], 2, [1], 1>; // Floating point double compare (XMM).
defm : Zn4WriteResYMMPair<WriteFCmp64Y, [Zn4FPFMul01], 2, [1], 1>; // Floating point double compare (YMM).
defm : Zn4WriteResZMMPair<WriteFCmp64Z, [Zn4FPFMul01], 2, [2], 1>; // Floating point double compare (ZMM).
defm : Zn4WriteResXMMPair<WriteFCom, [Zn4FPFMul01], 3, [2], 1>; // FIXME: latency not from llvm-exegesis  // Floating point compare to flags (X87).
defm : Zn4WriteResXMMPair<WriteFComX, [Zn4FPFMul01], 4, [2], 2>;  // FIXME: latency not from llvm-exegesis // Floating point compare to flags (SSE).
defm : Zn4WriteResXMMPair<WriteFMul, [Zn4FPFMul01], 3, [1], 1>;  // Floating point multiplication.
defm : Zn4WriteResXMMPair<WriteFMulX, [Zn4FPFMul01], 3, [1], 1>; // Floating point multiplication (XMM).
defm : Zn4WriteResYMMPair<WriteFMulY, [Zn4FPFMul01], 3, [1], 1>; // Floating point multiplication (YMM).
defm : Zn4WriteResZMMPair<WriteFMulZ, [Zn4FPFMul01], 3, [2], 1>; // Floating point multiplication (ZMM).
defm : Zn4WriteResXMMPair<WriteFMul64, [Zn4FPFMul01], 3, [1], 1>;  // Floating point double multiplication.
defm : Zn4WriteResXMMPair<WriteFMul64X, [Zn4FPFMul01], 3, [1], 1>; // Floating point double multiplication (XMM).
defm : Zn4WriteResYMMPair<WriteFMul64Y, [Zn4FPFMul01], 3, [1], 1>; // Floating point double multiplication (YMM).
defm : Zn4WriteResZMMPair<WriteFMul64Z, [Zn4FPFMul01], 3, [2], 1>; // Floating point double multiplication (ZMM).
defm : Zn4WriteResXMMPair<WriteFDiv, [Zn4FPFDiv], 11, [3], 1>;  // Floating point division.
defm : Zn4WriteResXMMPair<WriteFDivX, [Zn4FPFDiv], 11, [3], 1>; // Floating point division (XMM).
defm : Zn4WriteResYMMPair<WriteFDivY, [Zn4FPFDiv], 11, [3], 1>; // Floating point division (YMM).
defm : Zn4WriteResZMMPair<WriteFDivZ, [Zn4FPFDiv], 11, [6], 1>; // Floating point division (ZMM).
defm : Zn4WriteResXMMPair<WriteFDiv64, [Zn4FPFDiv], 13, [5], 1>;  // Floating point double division.
defm : Zn4WriteResXMMPair<WriteFDiv64X, [Zn4FPFDiv], 13, [5], 1>; // Floating point double division (XMM).
defm : Zn4WriteResYMMPair<WriteFDiv64Y, [Zn4FPFDiv], 13, [5], 1>; // Floating point double division (YMM).
defm : Zn4WriteResZMMPair<WriteFDiv64Z, [Zn4FPFDiv], 13, [10], 1>; // Floating point double division (ZMM).
defm : Zn4WriteResXMMPair<WriteFSqrt, [Zn4FPFDiv], 15, [5], 1>;   // Floating point square root.
defm : Zn4WriteResXMMPair<WriteFSqrtX, [Zn4FPFDiv], 15, [5], 1>;  // Floating point square root (XMM).
defm : Zn4WriteResYMMPair<WriteFSqrtY, [Zn4FPFDiv], 15, [5], 1>;  // Floating point square root (YMM).
defm : Zn4WriteResZMMPair<WriteFSqrtZ, [Zn4FPFDiv], 15, [10], 1>;  // Floating point square root (ZMM).
defm : Zn4WriteResXMMPair<WriteFSqrt64, [Zn4FPFDiv], 21, [9], 1>;  // Floating point double square root.
defm : Zn4WriteResXMMPair<WriteFSqrt64X, [Zn4FPFDiv], 21, [9], 1>; // Floating point double square root (XMM).
defm : Zn4WriteResYMMPair<WriteFSqrt64Y, [Zn4FPFDiv], 21, [9], 1>; // Floating point double square root (YMM).
defm : Zn4WriteResZMMPair<WriteFSqrt64Z, [Zn4FPFDiv], 21, [18], 1>; // Floating point double square root (ZMM).
defm : Zn4WriteResXMMPair<WriteFSqrt80, [Zn4FPFDiv], 22, [23], 1>; // FIXME: latency not from llvm-exegesis  // Floating point long double square root.
defm : Zn4WriteResXMMPair<WriteFRcp, [Zn4FPFMul01], 4, [1], 1>;  // Floating point reciprocal estimate.
defm : Zn4WriteResXMMPair<WriteFRcpX, [Zn4FPFMul01], 4, [1], 1>; // Floating point reciprocal estimate (XMM).
defm : Zn4WriteResYMMPair<WriteFRcpY, [Zn4FPFMul01], 5, [1], 1>; // Floating point reciprocal estimate (YMM).
defm : Zn4WriteResZMMPair<WriteFRcpZ, [Zn4FPFMul01], 5, [2], 1>; // Floating point reciprocal estimate (ZMM).
defm : Zn4WriteResXMMPair<WriteFRsqrt, [Zn4FPFDiv], 4, [1], 1>;  // Floating point reciprocal square root estimate.
defm : Zn4WriteResXMMPair<WriteFRsqrtX, [Zn4FPFDiv], 4, [1], 1>; // Floating point reciprocal square root estimate (XMM).
defm : Zn4WriteResYMMPair<WriteFRsqrtY, [Zn4FPFDiv], 4, [1], 1>; // Floating point reciprocal square root estimate (YMM).
defm : Zn4WriteResZMMPair<WriteFRsqrtZ, [Zn4FPFDiv], 5, [2], 1>; // Floating point reciprocal square root estimate (ZMM).
defm : Zn4WriteResXMMPair<WriteFMA, [Zn4FPFMul01], 4, [2], 1>;  // Fused Multiply Add.
defm : Zn4WriteResXMMPair<WriteFMAX, [Zn4FPFMul01], 4, [1], 1>; // Fused Multiply Add (XMM).
defm : Zn4WriteResYMMPair<WriteFMAY, [Zn4FPFMul01], 4, [1], 1>; // Fused Multiply Add (YMM).
defm : Zn4WriteResZMMPair<WriteFMAZ, [Zn4FPFMul01], 4, [2], 1>; // Fused Multiply Add (ZMM).
defm : Zn4WriteResXMMPair<WriteDPPD, [Zn4FPFMul01], 7, [6], 3, /*LoadUOps=*/2>; // Floating point double dot product.
defm : Zn4WriteResXMMPair<WriteDPPS, [Zn4FPFMul01], 11, [8], 8, /*LoadUOps=*/2>; // Floating point single dot product.
defm : Zn4WriteResYMMPair<WriteDPPSY, [Zn4FPFMul01], 11, [8], 7, /*LoadUOps=*/1>; // Floating point single dot product (YMM).
defm : Zn4WriteResXMMPair<WriteFSign, [Zn4FPFMul01], 1, [2], 1>; // FIXME: latency not from llvm-exegesis  // Floating point fabs/fchs.
defm : Zn4WriteResXMMPair<WriteFRnd, [Zn4FPFCvt01], 3, [1], 1>; // Floating point rounding.
defm : Zn4WriteResYMMPair<WriteFRndY, [Zn4FPFCvt01], 3, [1], 1>; // Floating point rounding (YMM).
defm : Zn4WriteResZMMPair<WriteFRndZ, [Zn4FPFCvt01], 3, [2], 1>; // Floating point rounding (ZMM).

defm : Zn4WriteResXMMPair<WriteFLogic, [Zn4FPVMisc0123], 1, [1], 1>; // Floating point and/or/xor logicals.
defm : Zn4WriteResYMMPair<WriteFLogicY, [Zn4FPVMisc0123], 1, [1], 1>; // Floating point and/or/xor logicals (YMM).
defm : Zn4WriteResZMMPair<WriteFLogicZ, [Zn4FPVMisc0123], 1, [2], 1>; // Floating point and/or/xor logicals (ZMM).
defm : Zn4WriteResXMMPair<WriteFTest, [Zn4FPFMisc12], 1, [2], 2>; // FIXME: latency not from llvm-exegesis // Floating point TEST instructions.
defm : Zn4WriteResYMMPair<WriteFTestY, [Zn4FPFMisc12], 1, [2], 2>; // FIXME: latency not from llvm-exegesis // Floating point TEST instructions (YMM).
defm : Zn4WriteResZMMPair<WriteFTestZ, [Zn4FPFMisc12], 1, [4], 1>; // FIXME: latency not from llvm-exegesis // Floating point TEST instructions (ZMM).
defm : Zn4WriteResXMMPair<WriteFShuffle, [Zn4FPVShuf01], 1, [1], 1>; // Floating point vector shuffles.
defm : Zn4WriteResYMMPair<WriteFShuffleY, [Zn4FPVShuf01], 1, [1], 1>; // Floating point vector shuffles (YMM).
defm : Zn4WriteResZMMPair<WriteFShuffleZ, [Zn4FPVShuf01], 1, [2], 1>; // Floating point vector shuffles (ZMM).
defm : Zn4WriteResXMMPair<WriteFVarShuffle, [Zn4FPVShuf01], 3, [1], 1>; // Floating point vector variable shuffles.
defm : Zn4WriteResYMMPair<WriteFVarShuffleY, [Zn4FPVShuf01], 3, [1], 1>; // Floating point vector variable shuffles (YMM).
defm : Zn4WriteResZMMPair<WriteFVarShuffleZ, [Zn4FPVShuf01], 3, [2], 1>; // Floating point vector variable shuffles (ZMM).
defm : Zn4WriteResXMMPair<WriteFBlend, [Zn4FPFMul01], 1, [1], 1>; // Floating point vector blends.
defm : Zn4WriteResYMMPair<WriteFBlendY, [Zn4FPFMul01], 1, [1], 1>; // Floating point vector blends (YMM).
defm : Zn4WriteResZMMPair<WriteFBlendZ, [Zn4FPFMul01], 1, [2], 1>; // Floating point vector blends (ZMM).
defm : Zn4WriteResXMMPair<WriteFVarBlend, [Zn4FPFMul01], 1, [1], 1>; // Fp vector variable blends.
defm : Zn4WriteResYMMPair<WriteFVarBlendY, [Zn4FPFMul01], 1, [1], 1>; // Fp vector variable blends (YMM).
defm : Zn4WriteResZMMPair<WriteFVarBlendZ, [Zn4FPFMul01], 1, [2], 1>; // Fp vector variable blends (ZMM).

// Horizontal Add/Sub (float and integer)
defm : Zn4WriteResXMMPair<WriteFHAdd, [Zn4FPFAdd0], 4, [2], 3>;
defm : Zn4WriteResYMMPair<WriteFHAddY, [Zn4FPFAdd0], 4, [2], 3, /*LoadUOps=*/1>;
defm : Zn4WriteResZMMPair<WriteFHAddZ, [Zn4FPFAdd0], 6, [4], 3, /*LoadUOps=*/1>;
defm : Zn4WriteResXMMPair<WritePHAdd, [Zn4FPVAdd0], 2, [2], 3, /*LoadUOps=*/1>;
defm : Zn4WriteResXMMPair<WritePHAddX, [Zn4FPVAdd0], 2, [2], 3>;
defm : Zn4WriteResYMMPair<WritePHAddY, [Zn4FPVAdd0], 3, [3], 3, /*LoadUOps=*/1>;
defm : Zn4WriteResZMMPair<WritePHAddZ, [Zn4FPVAdd0], 2, [4], 3, /*LoadUOps=*/1>;

// Vector integer operations.
defm : Zn4WriteResXMM<WriteVecLoad, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecLoadX, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResYMM<WriteVecLoadY, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecLoadNT, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResYMM<WriteVecLoadNTY, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecMaskedLoad, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResYMM<WriteVecMaskedLoadY, [Zn4FPLd01, Zn4Load], !add(Znver4Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecStore, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecStoreX, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;

def Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr : SchedWriteRes<[Zn4FPFMisc0]> {
  let Latency = 4;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr], (instrs VEXTRACTF128rr, VEXTRACTI128rr)>;

def Zn4WriteVEXTRACTI128mr : SchedWriteRes<[Zn4FPFMisc0, Zn4FPSt, Zn4Store]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr.NumMicroOps, 1);
}
def : InstRW<[Zn4WriteVEXTRACTI128mr], (instrs VEXTRACTI128mr, VEXTRACTF128mr)>;

def Zn4WriteVINSERTF128rmr : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPFMisc0]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn4WriteVEXTRACTF128rr_VEXTRACTI128rr.NumMicroOps, 0);
}
def : InstRW<[Zn4WriteVINSERTF128rmr], (instrs VINSERTF128rm)>;

defm : Zn4WriteResYMM<WriteVecStoreY, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecStoreNT, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResYMM<WriteVecStoreNTY, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [1, 1], 1>;
defm : Zn4WriteResXMM<WriteVecMaskedStore32, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [6, 1], 18>;
defm : Zn4WriteResXMM<WriteVecMaskedStore64, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [4, 1], 10>;
defm : Zn4WriteResYMM<WriteVecMaskedStore32Y, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [12, 1], 42>;
defm : Zn4WriteResYMM<WriteVecMaskedStore64Y, [Zn4FPSt, Zn4Store], Znver4Model.StoreLatency, [6, 1], 18>;

defm : Zn4WriteResXMM<WriteVecMoveToGpr, [Zn4FPLd01], 1, [2], 1>;
defm : Zn4WriteResXMM<WriteVecMoveFromGpr, [Zn4FPLd01], 1, [2], 1>;

def Zn4WriteMOVMMX : SchedWriteRes<[Zn4FPLd01, Zn4FPFMisc0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteMOVMMX], (instrs MMX_MOVQ2FR64rr, MMX_MOVQ2DQrr)>;

def Zn4WriteMOVMMXSlow : SchedWriteRes<[Zn4FPLd01, Zn4FPFMisc0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 4];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteMOVMMXSlow], (instrs MMX_MOVD64rr, MMX_MOVD64to64rr)>;

defm : Zn4WriteResXMMPair<WriteVecALU, [Zn4FPVAdd0123], 1, [1], 1>;  // Vector integer ALU op, no logicals.

def Zn4WriteEXTRQ_INSERTQ : SchedWriteRes<[Zn4FPVShuf01, Zn4FPLd01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteEXTRQ_INSERTQ], (instrs EXTRQ, INSERTQ)>;

def Zn4WriteEXTRQI_INSERTQI : SchedWriteRes<[Zn4FPVShuf01, Zn4FPLd01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteEXTRQI_INSERTQI], (instrs EXTRQI, INSERTQI)>;

defm : Zn4WriteResXMMPair<WriteVecALUX, [Zn4FPVAdd0123], 1, [1], 1>; // Vector integer ALU op, no logicals (XMM).

def Zn4WriteVecALUXSlow : SchedWriteRes<[Zn4FPVAdd01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecALUXSlow], (instrs PABSBrr, PABSDrr, PABSWrr,
                                            PADDSBrr, PADDSWrr, PADDUSBrr, PADDUSWrr,
                                            PAVGBrr, PAVGWrr,
                                            PSIGNBrr, PSIGNDrr, PSIGNWrr,
                                            VPABSBrr, VPABSDrr, VPABSWrr,
                                            VPADDSBrr, VPADDSWrr, VPADDUSBrr, VPADDUSWrr,
                                            VPAVGBrr, VPAVGWrr,
                                            VPCMPEQQrr,
                                            VPSIGNBrr, VPSIGNDrr, VPSIGNWrr,
                                            PSUBSBrr, PSUBSWrr, PSUBUSBrr, PSUBUSWrr, VPSUBSBrr, VPSUBSWrr, VPSUBUSBrr, VPSUBUSWrr)>;

def Zn4WriteVecOpMask : SchedWriteRes<[Zn4FPOpMask01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecOpMask], (instrs   KADDBrr, KADDDrr, KADDQrr, KADDWrr,
                                            KANDBrr, KANDDrr, KANDQrr, KANDWrr,
                                            KANDNBrr, KANDNDrr, KANDNQrr, KANDNWrr,
                                            KMOVBkk, KMOVDkk, KMOVQkk, KMOVWkk,
                                            KMOVBrk, KMOVDrk, KMOVQrk, KMOVWrk,
                                            KNOTBrr, KNOTDrr, KNOTQrr, KNOTWrr,
                                            KORBrr, KORDrr, KORQrr, KORWrr,
                                            KORTESTBrr, KORTESTDrr, KORTESTQrr, KORTESTWrr,
                                            KTESTBrr, KTESTDrr, KTESTQrr, KTESTWrr,
                                            KUNPCKBWrr, KUNPCKDQrr, KUNPCKWDrr,
                                            KXNORBrr, KXNORDrr, KXNORQrr, KXNORWrr,
                                            KXORBrr, KXORDrr, KXORQrr, KXORWrr)>;

def Zn4WriteVecOpMaskMemMov : SchedWriteRes<[Zn4FPOpMask4]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecOpMaskMemMov], (instrs KMOVBmk, KMOVDmk, KMOVQmk, KMOVWmk)>;

def Zn4WriteVecOpMaskKRMov : SchedWriteRes<[Zn4FPOpMask4]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecOpMaskKRMov], (instrs KMOVBkr, KMOVDkr, KMOVQkr, KMOVWkr)>;

def Zn4WriteVecALU2Slow : SchedWriteRes<[Zn4FPVAdd12]> {
  // TODO: All align instructions are expected to be of 4 cycle latency
  let Latency = 4;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecALU2Slow], (instrs VALIGNDZrri, VALIGNDZ128rri, VALIGNDZ256rri,
                                            VALIGNQZrri, VALIGNQZ128rri, VALIGNQZ256rri)
                                            >;
defm : Zn4WriteResYMMPair<WriteVecALUY, [Zn4FPVAdd0123], 1, [1], 1>; // Vector integer ALU op, no logicals (YMM).

def Zn4WriteVecALUYSlow : SchedWriteRes<[Zn4FPVAdd01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVecALUYSlow], (instrs VPABSBYrr, VPABSDYrr, VPABSWYrr,
                                            VPADDSBYrr, VPADDSWYrr, VPADDUSBYrr, VPADDUSWYrr,
                                            VPSUBSBYrr, VPSUBSWYrr, VPSUBUSBYrr, VPSUBUSWYrr,
                                            VPAVGBYrr, VPAVGWYrr,
                                            VPCMPEQQYrr,
                                            VPSIGNBYrr, VPSIGNDYrr, VPSIGNWYrr)>;

defm : Zn4WriteResZMMPair<WriteVecALUZ, [Zn4FPVAdd0123], 1, [2], 1>; // Vector integer ALU op, no logicals (ZMM).

defm : Zn4WriteResXMMPair<WriteVecLogic, [Zn4FPVMisc0123], 1, [1], 1>;  // Vector integer and/or/xor logicals.
defm : Zn4WriteResXMMPair<WriteVecLogicX, [Zn4FPVMisc0123], 1, [1], 1>; // Vector integer and/or/xor logicals (XMM).
defm : Zn4WriteResYMMPair<WriteVecLogicY, [Zn4FPVMisc0123], 1, [1], 1>; // Vector integer and/or/xor logicals (YMM).
defm : Zn4WriteResZMMPair<WriteVecLogicZ, [Zn4FPVMisc0123], 1, [2], 1>; // Vector integer and/or/xor logicals (ZMM).
defm : Zn4WriteResXMMPair<WriteVecTest, [Zn4FPVAdd12, Zn4FPSt], 1, [1, 1], 2>;  // FIXME: latency not from llvm-exegesis // Vector integer TEST instructions.
defm : Zn4WriteResYMMPair<WriteVecTestY, [Zn4FPVAdd12, Zn4FPSt], 1, [1, 1], 2>; // FIXME: latency not from llvm-exegesis  // Vector integer TEST instructions (YMM).
defm : Zn4WriteResZMMPair<WriteVecTestZ, [Zn4FPVAdd12, Zn4FPSt], 1, [2, 2], 2>; // FIXME: latency not from llvm-exegesis  // Vector integer TEST instructions (ZMM).
defm : Zn4WriteResXMMPair<WriteVecShift, [Zn4FPVShift01], 1, [1], 1>;  // Vector integer shifts (default).
defm : Zn4WriteResXMMPair<WriteVecShiftX, [Zn4FPVShift01], 2, [2], 1>; // Vector integer shifts (XMM).
defm : Zn4WriteResYMMPair<WriteVecShiftY, [Zn4FPVShift01], 1, [1], 1>; // Vector integer shifts (YMM).
defm : Zn4WriteResZMMPair<WriteVecShiftZ, [Zn4FPVShift01], 1, [2], 1>; // Vector integer shifts (ZMM).
defm : Zn4WriteResXMMPair<WriteVecShiftImm, [Zn4FPVShift01], 1, [1], 1>;  // Vector integer immediate shifts (default).
defm : Zn4WriteResXMMPair<WriteVecShiftImmX, [Zn4FPVShift01], 1, [1], 1>; // Vector integer immediate shifts (XMM).
defm : Zn4WriteResYMMPair<WriteVecShiftImmY, [Zn4FPVShift01], 1, [1], 1>; // Vector integer immediate shifts (YMM).
defm : Zn4WriteResZMMPair<WriteVecShiftImmZ, [Zn4FPVShift01], 1, [2], 1>; // Vector integer immediate shifts (ZMM).
defm : Zn4WriteResXMMPair<WriteVecIMul, [Zn4FPVMul01], 3, [1], 1>;  // Vector integer multiply (default).
defm : Zn4WriteResXMMPair<WriteVecIMulX, [Zn4FPVMul01], 3, [1], 1>; // Vector integer multiply (XMM).
defm : Zn4WriteResYMMPair<WriteVecIMulY, [Zn4FPVMul01], 3, [1], 1>; // Vector integer multiply (YMM).
defm : Zn4WriteResZMMPair<WriteVecIMulZ, [Zn4FPVMul01], 3, [2], 1>; // Vector integer multiply (ZMM).
defm : Zn4WriteResXMMPair<WritePMULLD, [Zn4FPVMul01], 3, [1], 1>; // Vector PMULLD.
defm : Zn4WriteResYMMPair<WritePMULLDY, [Zn4FPVMul01], 3, [1], 1>; // Vector PMULLD (YMM).
defm : Zn4WriteResZMMPair<WritePMULLDZ, [Zn4FPVMul01], 3, [2], 1>; // Vector PMULLD (ZMM).
defm : Zn4WriteResXMMPair<WriteShuffle, [Zn4FPVShuf01], 1, [1], 1>;  // Vector shuffles.
defm : Zn4WriteResXMMPair<WriteShuffleX, [Zn4FPVShuf01], 1, [1], 1>; // Vector shuffles (XMM).
defm : Zn4WriteResYMMPair<WriteShuffleY, [Zn4FPVShuf01], 1, [1], 1>; // Vector shuffles (YMM).
defm : Zn4WriteResZMMPair<WriteShuffleZ, [Zn4FPVShuf01], 1, [2], 1>; // Vector shuffles (ZMM).
defm : Zn4WriteResXMMPair<WriteVarShuffle, [Zn4FPVShuf01], 1, [1], 1>;  // Vector variable shuffles.
defm : Zn4WriteResXMMPair<WriteVarShuffleX, [Zn4FPVShuf01], 1, [1], 1>; // Vector variable shuffles (XMM).
defm : Zn4WriteResYMMPair<WriteVarShuffleY, [Zn4FPVShuf01], 1, [1], 1>; // Vector variable shuffles (YMM).
defm : Zn4WriteResZMMPair<WriteVarShuffleZ, [Zn4FPVShuf01], 1, [2], 1>; // Vector variable shuffles (ZMM).
defm : Zn4WriteResXMMPair<WriteBlend, [Zn4FPVMisc0123], 1, [1], 1>; // Vector blends.
defm : Zn4WriteResYMMPair<WriteBlendY, [Zn4FPVMisc0123], 1, [1], 1>; // Vector blends (YMM).
defm : Zn4WriteResZMMPair<WriteBlendZ, [Zn4FPVMisc0123], 1, [2], 1>; // Vector blends (ZMM).
defm : Zn4WriteResXMMPair<WriteVarBlend, [Zn4FPVMul01], 1, [1], 1>; // Vector variable blends.
defm : Zn4WriteResYMMPair<WriteVarBlendY, [Zn4FPVMul01], 1, [1], 1>; // Vector variable blends (YMM).
defm : Zn4WriteResZMMPair<WriteVarBlendZ, [Zn4FPVMul01], 1, [2], 1>; // Vector variable blends (ZMM).
defm : Zn4WriteResXMMPair<WritePSADBW, [Zn4FPVAdd0123], 3, [2], 1>;  // Vector PSADBW.
defm : Zn4WriteResXMMPair<WritePSADBWX, [Zn4FPVAdd0123], 3, [2], 1>; // Vector PSADBW (XMM).
defm : Zn4WriteResYMMPair<WritePSADBWY, [Zn4FPVAdd0123], 3, [2], 1>; // Vector PSADBW (YMM).
defm : Zn4WriteResZMMPair<WritePSADBWZ, [Zn4FPVAdd0123], 4, [4], 1>; // Vector PSADBW (ZMM).
defm : Zn4WriteResXMMPair<WriteMPSAD, [Zn4FPVAdd0123], 4, [8], 4, /*LoadUOps=*/2>; // Vector MPSAD.
defm : Zn4WriteResYMMPair<WriteMPSADY, [Zn4FPVAdd0123], 4, [8], 3, /*LoadUOps=*/1>; // Vector MPSAD (YMM).
defm : Zn4WriteResZMMPair<WriteMPSADZ, [Zn4FPVAdd0123], 4, [16], 3, /*LoadUOps=*/1>; // Vector MPSAD (ZMM).
defm : Zn4WriteResXMMPair<WritePHMINPOS, [Zn4FPVAdd01], 3, [1], 1>;  // Vector PHMINPOS.

// Vector insert/extract operations.
defm : Zn4WriteResXMMPair<WriteVecInsert, [Zn4FPLd01], 1, [2], 2, /*LoadUOps=*/-1>; // Insert gpr to vector element.
defm : Zn4WriteResXMM<WriteVecExtract, [Zn4FPLd01], 1, [2], 2>; // Extract vector element to gpr.
defm : Zn4WriteResXMM<WriteVecExtractSt, [Zn4FPSt, Zn4Store], !add(1, Znver4Model.StoreLatency), [1, 1], 2>; // Extract vector element and store.

// MOVMSK operations.
defm : Zn4WriteResXMM<WriteFMOVMSK, [Zn4FPVMisc2], 1, [1], 1>;
defm : Zn4WriteResXMM<WriteVecMOVMSK, [Zn4FPVMisc2], 1, [1], 1>;
defm : Zn4WriteResYMM<WriteVecMOVMSKY, [Zn4FPVMisc2], 1, [1], 1>;
defm : Zn4WriteResXMM<WriteMMXMOVMSK, [Zn4FPVMisc2], 1, [1], 1>;

// Conversion between integer and float.
defm : Zn4WriteResXMMPair<WriteCvtSD2I, [Zn4FPFCvt01], 1, [1], 1>;  // Double -> Integer.
defm : Zn4WriteResXMMPair<WriteCvtPD2I, [Zn4FPFCvt01], 3, [2], 1>; // Double -> Integer (XMM).
defm : Zn4WriteResYMMPair<WriteCvtPD2IY, [Zn4FPFCvt01], 3, [2], 2>; // Double -> Integer (YMM).
defm : Zn4WriteResZMMPair<WriteCvtPD2IZ, [Zn4FPFCvt01], 3, [4], 2>; // Double -> Integer (ZMM).

def Zn4WriteCvtPD2IMMX : SchedWriteRes<[Zn4FPFCvt01]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
defm : Zn4WriteResXMMPair<WriteCvtSS2I, [Zn4FPFCvt01], 5, [5], 2>;  // Float -> Integer.

defm : Zn4WriteResXMMPair<WriteCvtPS2I, [Zn4FPFCvt01], 3, [1], 1>; // Float -> Integer (XMM).
defm : Zn4WriteResYMMPair<WriteCvtPS2IY, [Zn4FPFCvt01], 4, [1], 1>; // Float -> Integer (YMM).
defm : Zn4WriteResZMMPair<WriteCvtPS2IZ, [Zn4FPFCvt01], 4, [2], 2>; // Float -> Integer (ZMM).

defm : Zn4WriteResXMMPair<WriteCvtI2SD, [Zn4FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>;  // Integer -> Double.
defm : Zn4WriteResXMMPair<WriteCvtI2PD, [Zn4FPFCvt01], 3, [1], 1>; // Integer -> Double (XMM).
defm : Zn4WriteResYMMPair<WriteCvtI2PDY, [Zn4FPFCvt01], 3, [2], 2, /*LoadUOps=*/-1>; // Integer -> Double (YMM).
defm : Zn4WriteResZMMPair<WriteCvtI2PDZ, [Zn4FPFCvt01], 4, [4], 4, /*LoadUOps=*/-1>; // Integer -> Double (ZMM).

def Zn4WriteCvtI2PDMMX : SchedWriteRes<[Zn4FPFCvt01]> {
  let Latency = 2;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 2;
}

defm : Zn4WriteResXMMPair<WriteCvtI2SS, [Zn4FPFCvt01], 3, [2], 2, /*LoadUOps=*/-1>;  // Integer -> Float.
defm : Zn4WriteResXMMPair<WriteCvtI2PS, [Zn4FPFCvt01], 3, [1], 1>; // Integer -> Float (XMM).
defm : Zn4WriteResYMMPair<WriteCvtI2PSY, [Zn4FPFCvt01], 3, [1], 1>; // Integer -> Float (YMM).
defm : Zn4WriteResZMMPair<WriteCvtI2PSZ, [Zn4FPFCvt01], 3, [2], 2>; // Integer -> Float (ZMM).

def Zn4WriteCvtI2PSMMX : SchedWriteRes<[Zn4FPFCvt01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}

defm : Zn4WriteResXMMPair<WriteCvtSS2SD, [Zn4FPFCvt01], 3, [1], 1>;  // Float -> Double size conversion.
defm : Zn4WriteResXMMPair<WriteCvtPS2PD, [Zn4FPFCvt01], 3, [1], 1>; // Float -> Double size conversion (XMM).
defm : Zn4WriteResYMMPair<WriteCvtPS2PDY, [Zn4FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>; // Float -> Double size conversion (YMM).
defm : Zn4WriteResZMMPair<WriteCvtPS2PDZ, [Zn4FPFCvt01], 6, [4], 4, /*LoadUOps=*/-1>; // Float -> Double size conversion (ZMM).

defm : Zn4WriteResXMMPair<WriteCvtSD2SS, [Zn4FPFCvt01], 3, [1], 1>;  // Double -> Float size conversion.
defm : Zn4WriteResXMMPair<WriteCvtPD2PS, [Zn4FPFCvt01], 3, [1], 1>; // Double -> Float size conversion (XMM).
defm : Zn4WriteResYMMPair<WriteCvtPD2PSY, [Zn4FPFCvt01], 6, [2], 2>; // Double -> Float size conversion (YMM).
defm : Zn4WriteResZMMPair<WriteCvtPD2PSZ, [Zn4FPFCvt01], 6, [4], 4>; // Double -> Float size conversion (ZMM).

defm : Zn4WriteResXMMPair<WriteCvtPH2PS, [Zn4FPFCvt01], 3, [1], 1>; // Half -> Float size conversion.
defm : Zn4WriteResYMMPair<WriteCvtPH2PSY, [Zn4FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>; // Half -> Float size conversion (YMM).
defm : Zn4WriteResZMMPair<WriteCvtPH2PSZ, [Zn4FPFCvt01], 4, [4], 4, /*LoadUOps=*/-1>; // Half -> Float size conversion (ZMM).

defm : Zn4WriteResXMM<WriteCvtPS2PH, [Zn4FPFCvt01], 3, [2], 1>; // Float -> Half size conversion.
defm : Zn4WriteResYMM<WriteCvtPS2PHY, [Zn4FPFCvt01], 6, [2], 2>; // Float -> Half size conversion (YMM).
defm : Zn4WriteResZMM<WriteCvtPS2PHZ, [Zn4FPFCvt01], 6, [2], 2>; // Float -> Half size conversion (ZMM).

defm : Zn4WriteResXMM<WriteCvtPS2PHSt, [Zn4FPFCvt01, Zn4FPSt, Zn4Store], !add(3, Znver4Model.StoreLatency), [1, 1, 1], 2>; // Float -> Half + store size conversion.
defm : Zn4WriteResYMM<WriteCvtPS2PHYSt, [Zn4FPFCvt01, Zn4FPSt, Zn4Store], !add(6, Znver4Model.StoreLatency), [2, 1, 1], 3>; // Float -> Half + store size conversion (YMM).
defm : Zn4WriteResYMM<WriteCvtPS2PHZSt, [Zn4FPFCvt01, Zn4FPSt, Zn4Store], !add(6, Znver4Model.StoreLatency), [2, 1, 1], 3>; // Float -> Half + store size conversion (ZMM).

// CRC32 instruction.
defm : Zn4WriteResIntPair<WriteCRC32, [Zn4ALU1], 3, [1], 1>;

def Zn4WriteSHA1MSG1rr : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteSHA1MSG1rr], (instrs SHA1MSG1rr)>;

def Zn4WriteSHA1MSG1rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteSHA1MSG1rr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteSHA1MSG1rr.NumMicroOps, 0);
}
def : InstRW<[Zn4WriteSHA1MSG1rm], (instrs SHA1MSG1rm)>;

def Zn4WriteSHA1MSG2rr_SHA1NEXTErr : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSHA1MSG2rr_SHA1NEXTErr], (instrs SHA1MSG2rr, SHA1NEXTErr)>;

def Zn4Writerm_SHA1MSG2rm_SHA1NEXTErm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteSHA1MSG2rr_SHA1NEXTErr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteSHA1MSG2rr_SHA1NEXTErr.NumMicroOps, 0);
}
def : InstRW<[Zn4Writerm_SHA1MSG2rm_SHA1NEXTErm], (instrs SHA1MSG2rm, SHA1NEXTErm)>;

def Zn4WriteSHA256MSG1rr : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteSHA256MSG1rr], (instrs SHA256MSG1rr)>;

def Zn4Writerm_SHA256MSG1rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteSHA256MSG1rr.Latency);
  let ReleaseAtCycles = [1, 1, 3];
  let NumMicroOps = !add(Zn4WriteSHA256MSG1rr.NumMicroOps, 0);
}
def : InstRW<[Zn4Writerm_SHA256MSG1rm], (instrs SHA256MSG1rm)>;

def Zn4WriteSHA256MSG2rr : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 3;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 4;
}
def : InstRW<[Zn4WriteSHA256MSG2rr], (instrs SHA256MSG2rr)>;

def Zn4WriteSHA256MSG2rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPU0123]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteSHA256MSG2rr.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn4WriteSHA256MSG2rr.NumMicroOps, 1);
}
def : InstRW<[Zn4WriteSHA256MSG2rm], (instrs SHA256MSG2rm)>;

def Zn4WriteSHA1RNDS4rri : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 6;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSHA1RNDS4rri], (instrs SHA1RNDS4rri)>;

def Zn4WriteSHA256RNDS2rr : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSHA256RNDS2rr], (instrs SHA256RNDS2rr)>;

// Strings instructions.
// Packed Compare Implicit Length Strings, Return Mask
defm : Zn4WriteResXMMPair<WritePCmpIStrM, [Zn4FPVAdd0123], 6, [8], 3, /*LoadUOps=*/1>;
// Packed Compare Explicit Length Strings, Return Mask
defm : Zn4WriteResXMMPair<WritePCmpEStrM, [Zn4FPVAdd0123], 6, [12], 7, /*LoadUOps=*/5>;
// Packed Compare Implicit Length Strings, Return Index
defm : Zn4WriteResXMMPair<WritePCmpIStrI, [Zn4FPVAdd0123], 2, [8], 4>;
// Packed Compare Explicit Length Strings, Return Index
defm : Zn4WriteResXMMPair<WritePCmpEStrI, [Zn4FPVAdd0123], 6, [12], 8, /*LoadUOps=*/4>;

// AES instructions.
defm : Zn4WriteResXMMPair<WriteAESDecEnc, [Zn4FPAES01], 4, [1], 1>; // Decryption, encryption.
defm : Zn4WriteResXMMPair<WriteAESIMC, [Zn4FPAES01], 4, [1], 1>; // InvMixColumn.
defm : Zn4WriteResXMMPair<WriteAESKeyGen, [Zn4FPAES01], 4, [1], 1>; // Key Generation.

// Carry-less multiplication instructions.
defm : Zn4WriteResXMMPair<WriteCLMul, [Zn4FPCLM01], 4, [4], 4>;

// EMMS/FEMMS
defm : Zn4WriteResInt<WriteEMMS, [Zn4ALU0123], 2, [1], 1>; // FIXME: latency not from llvm-exegesis

// Load/store MXCSR
defm : Zn4WriteResInt<WriteLDMXCSR, [Zn4AGU012, Zn4Load, Zn4ALU0123], !add(Znver4Model.LoadLatency, 1), [1, 1, 6], 1>; // FIXME: latency not from llvm-exegesis
defm : Zn4WriteResInt<WriteSTMXCSR, [Zn4ALU0123, Zn4AGU012, Zn4Store], !add(1, Znver4Model.StoreLatency), [60, 1, 1], 2>; // FIXME: latency not from llvm-exegesis

// Catch-all for expensive system instructions.
defm : Zn4WriteResInt<WriteSystem, [Zn4ALU0123], 100, [100], 100>;

def Zn4WriteVZEROUPPER : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 0; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVZEROUPPER], (instrs VZEROUPPER)>;

def Zn4WriteVZEROALL : SchedWriteRes<[Zn4FPU0123]> {
  let Latency = 10; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [24];
  let NumMicroOps = 18;
}
def : InstRW<[Zn4WriteVZEROALL], (instrs VZEROALL)>;

// AVX2.
defm : Zn4WriteResYMMPair<WriteFShuffle256, [Zn4FPVShuf], 2, [1], 1, /*LoadUOps=*/2>; // Fp 256-bit width vector shuffles.
defm : Zn4WriteResYMMPair<WriteFVarShuffle256, [Zn4FPVShuf], 7, [1], 2, /*LoadUOps=*/1>; // Fp 256-bit width variable shuffles.
defm : Zn4WriteResYMMPair<WriteShuffle256, [Zn4FPVShuf], 1, [1], 1>; // 256-bit width vector shuffles.

def Zn4WriteVPERM2I128rr_VPERM2F128rr : SchedWriteRes<[Zn4FPVShuf]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVPERM2I128rr_VPERM2F128rr], (instrs VPERM2I128rr, VPERM2F128rr)>;

def Zn4WriteVPERM2F128rm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPVShuf]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVPERM2I128rr_VPERM2F128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn4WriteVPERM2I128rr_VPERM2F128rr.NumMicroOps, 0);
}
def : InstRW<[Zn4WriteVPERM2F128rm], (instrs VPERM2F128rm)>;

def Zn4WriteVPERMPSYrr : SchedWriteRes<[Zn4FPVShuf]> {
  let Latency = 7;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteVPERMPSYrr], (instrs VPERMPSYrr)>;

def Zn4WriteVPERMPSYrm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPVShuf]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVPERMPSYrr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteVPERMPSYrr.NumMicroOps, 1);
}
def : InstRW<[Zn4WriteVPERMPSYrm], (instrs VPERMPSYrm)>;

def Zn4WriteVPERMYri : SchedWriteRes<[Zn4FPVShuf]> {
  let Latency = 6;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteVPERMYri], (instrs VPERMPDYri, VPERMQYri)>;

def Zn4WriteVPERMPDYmi : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPVShuf]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVPERMYri.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteVPERMYri.NumMicroOps, 1);
}
def : InstRW<[Zn4WriteVPERMPDYmi], (instrs VPERMPDYmi)>;

def Zn4WriteVPERMDYrr : SchedWriteRes<[Zn4FPVShuf]> {
  let Latency = 5;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteVPERMDYrr], (instrs VPERMDYrr)>;

def Zn4WriteVPERMYm : SchedWriteRes<[Zn4AGU012, Zn4Load, Zn4FPVShuf]> {
  let Latency = !add(Znver4Model.LoadLatency, Zn4WriteVPERMDYrr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn4WriteVPERMDYrr.NumMicroOps, 0);
}
def : InstRW<[Zn4WriteVPERMYm], (instrs VPERMQYmi, VPERMDYrm)>;

defm : Zn4WriteResYMMPair<WriteVPMOV256, [Zn4FPVShuf01], 4, [3], 2, /*LoadUOps=*/-1>; // 256-bit width packed vector width-changing move.
defm : Zn4WriteResYMMPair<WriteVarShuffle256, [Zn4FPVShuf01], 1, [1], 2>; // 256-bit width vector variable shuffles.
defm : Zn4WriteResXMMPair<WriteVarVecShift, [Zn4FPVShift01], 1, [1], 1>; // Variable vector shifts.
defm : Zn4WriteResYMMPair<WriteVarVecShiftY, [Zn4FPVShift01], 1, [1], 1>; // Variable vector shifts (YMM).
defm : Zn4WriteResZMMPair<WriteVarVecShiftZ, [Zn4FPVShift01], 1, [2], 2>; // Variable vector shifts (ZMM).

// Old microcoded instructions that nobody use.
defm : Zn4WriteResInt<WriteMicrocoded, [Zn4ALU0123], 100, [100], 100>;

// Fence instructions.
defm : Zn4WriteResInt<WriteFence, [Zn4ALU0123], 1, [100], 1>;

def Zn4WriteLFENCE : SchedWriteRes<[Zn4LSU]> {
  let Latency = 1;
  let ReleaseAtCycles = [30];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteLFENCE], (instrs LFENCE)>;

def Zn4WriteSFENCE : SchedWriteRes<[Zn4LSU]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSFENCE], (instrs SFENCE)>;

// Nop, not very useful expect it provides a model for nops!
defm : Zn4WriteResInt<WriteNop, [Zn4ALU0123], 0, [1], 1>; // FIXME: latency not from llvm-exegesis


///////////////////////////////////////////////////////////////////////////////
// Zero Cycle Move
///////////////////////////////////////////////////////////////////////////////

def Zn4WriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
  let ReleaseAtCycles = [];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteZeroLatency], (instrs MOV32rr, MOV32rr_REV,
                                               MOV64rr, MOV64rr_REV,
                                               MOVSX32rr32)>;

def Zn4WriteSwapRenameable : SchedWriteRes<[]> {
  let Latency = 0;
  let ReleaseAtCycles = [];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteSwapRenameable], (instrs XCHG32rr, XCHG32ar,
                                               XCHG64rr, XCHG64ar)>;

defm : Zn4WriteResInt<WriteXCHG, [Zn4ALU0123], 0, [8], 2>;        // Compare+Exchange - TODO RMW support.

defm : Zn4WriteResXMM<WriteFMoveX, [], 0, [], 1>;
defm : Zn4WriteResYMM<WriteFMoveY, [], 0, [], 1>;
defm : Zn4WriteResYMM<WriteFMoveZ, [], 0, [], 1>;

defm : Zn4WriteResXMM<WriteVecMove, [Zn4FPFMisc0123], 1, [1], 1>; // MMX
defm : Zn4WriteResXMM<WriteVecMoveX, [], 0, [], 1>;
defm : Zn4WriteResYMM<WriteVecMoveY, [], 0, [], 1>;
defm : Zn4WriteResYMM<WriteVecMoveZ, [], 0, [], 1>;

def : IsOptimizableRegisterMove<[
  InstructionEquivalenceClass<[
    // GPR variants.
    MOV32rr, MOV32rr_REV,
    MOV64rr, MOV64rr_REV,
    MOVSX32rr32,
    XCHG32rr, XCHG32ar,
    XCHG64rr, XCHG64ar,

    // MMX variants.
    // MMX moves are *NOT* eliminated.

    // SSE variants.
    MOVAPSrr, MOVAPSrr_REV,
    MOVUPSrr, MOVUPSrr_REV,
    MOVAPDrr, MOVAPDrr_REV,
    MOVUPDrr, MOVUPDrr_REV,
    MOVDQArr, MOVDQArr_REV,
    MOVDQUrr, MOVDQUrr_REV,

    // AVX variants.
    VMOVAPSrr, VMOVAPSrr_REV,
    VMOVUPSrr, VMOVUPSrr_REV,
    VMOVAPDrr, VMOVAPDrr_REV,
    VMOVUPDrr, VMOVUPDrr_REV,
    VMOVDQArr, VMOVDQArr_REV,
    VMOVDQUrr, VMOVDQUrr_REV,

    // AVX YMM variants.
    VMOVAPSYrr, VMOVAPSYrr_REV,
    VMOVUPSYrr, VMOVUPSYrr_REV,
    VMOVAPDYrr, VMOVAPDYrr_REV,
    VMOVUPDYrr, VMOVUPDYrr_REV,
    VMOVDQAYrr, VMOVDQAYrr_REV,
    VMOVDQUYrr, VMOVDQUYrr_REV,
  ], TruePred >
]>;

// FIXUP and RANGE Instructions
def Zn4WriteVFIXUPIMMPDZrr_VRANGESDrr : SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteVFIXUPIMMPDZrr_VRANGESDrr], (instregex
	"VFIXUPIMM(S|P)(S|D)(Z|Z128|Z256?)rrik", "VFIXUPIMM(S|P)(S|D)(Z?|Z128?|Z256?)rrikz", 
        "VFIXUPIMM(S|P)(S|D)(Z128|Z256?)rri",  "VRANGE(S|P)(S|D)(Z?|Z128?|Z256?)rri(b?)",
	"VRANGE(S|P)(S|D)(Z|Z128|Z256?)rri(b?)k","VRANGE(S|P)(S|D)(Z?|Z128?|Z256?)rri(b?)kz"
	)>;

// SCALE & REDUCE instructions
def Zn4WriteSCALErr: SchedWriteRes<[Zn4FPFMisc23]> {
  let Latency = 6;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteSCALErr], (instregex
        "V(SCALEF|REDUCE)(S|P)(S|D)(Z?|Z128?|Z256?)(rr|rrb|rrkz|rrik|rrikz|rri)(_Int?|_Intkz?)",
        "(V?)REDUCE(PD|PS|SD|SS)(Z?|Z128?)(rri|rrikz|rrib)"
	)>;

//BF16PS Instructions
def Zn4WriteBF16: SchedWriteRes<[Zn4FPFMisc23]> {
  let Latency = 6;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 2;
}
def : InstRW<[Zn4WriteBF16], (instregex
        "(V?)DPBF16PS(Z?|Z128?|Z256?)(r|rk|rkz)"
	)>;

// BUSD and VPMADD Instructions
def Zn4WriteBUSDr_VPMADDr: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteBUSDr_VPMADDr], (instregex
	"VPDP(BU|WS)(S|P)(S|D|DS)(Z|Z128|Z256)(r|rk|rkz)",
        "VPMADD52(H|L)UQ(Z|Z128|Z256)(r|rk|rkz)"
	)>;

// SHIFT instructions
def Zn4WriteSHIFTrr: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSHIFTrr], (instregex
        "VP(LZCNT|SHLD|SHRD?)(D|Q|W|VD|VQ|VW?)(Z?|Z128?|Z256?)(rr|rk|rrk|rrkz|rri|rrik|rrikz)",
        "(V?)P(SLL|SRL|SRA)(D|Q|W|DQ)(Y?|Z?|Z128?|Z256?)(rr|rrk|rrkz)",
        "(V?)P(SLL|SRL|SRA)DQYri",
        "(V?)P(SLL|SRL)DQ(Z?|Z256?)ri",
        "(V?)P(SHUFB)(Y|Z|Z128|Z256?)(rr|rrk|rrkz)",
        "(V?)P(ROL|ROR)(D|Q|VD|VQ)(Z?|Z128?|Z256?)(rr|rrk|rrkz)",
        "(V?)P(ROL|ROR)(D|Q|VD|VQ)(Z256?)(ri|rik|rikz)",
        "(V?)P(ROL|ROR)(D|Q)(Z?|Z128?)(ri|rik|rikz)",
	"VPSHUFBITQMBZ128rr", "VFMSUB231SSZr_Intkz"
	)>;

def Zn4WriteSHIFTri: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteSHIFTri], (instregex
        "VP(SLL|SRL|SRA)(D|Q|W)(Z|Z128|Z256?)(ri|rik|rikz)"
	)>;

// ALIGN Instructions
def Zn4WriteALIGN: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteALIGN], (instregex
        "(V?)PALIGNR(Z?|Z128?|Z256?)(rri|rrik|rrikz)"
	)>;

//PACK Instructions
def Zn4WritePACK: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WritePACK], (instregex
        "(V?)PACK(SS|US)(DW|WB)(Z?|Z128?|Z256?)(rr|rrk|rrkz)"
	)>;

// MAX and MIN Instructions
def Zn4WriteFCmp64: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4WriteFCmp64], (instregex
        "(V?)CMP(S|P)(S|D)(rr|rri|rr_Int)",
        "(V?|VP?)(MAX|MIN|MINC|MAXC)(S|P|U)(S|D|Q)(Z?|Z128?|Z256?)(rr|rri|rrk|rrkz)(_Int?)",
        "VP(MAX|MIN)(SQ|UQ)(Z|Z128|Z256)(rr|rrk|rrkz)",
        "(V?)(MAX|MAXC|MIN|MINC)PD(Z|Z128|Z256?)(rr|rrk|rrkz)"
	)>;

// MOV Instructions
def Zn4MOVDUPZ: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4MOVDUPZ], (instregex
        "(V?)VMOVDDUP(Z|Z128|Z256)(rr|rrk|rrkz)"
	)>;

def Zn4MOVS: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 2;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4MOVS], (instregex
        "(V?)PMOV(SX|ZX)(BD|BQ|BW|WD|WQ|DQ)(Z128?|Z256?)(rr|rrk|rrkz)",
        "(V?)PMOV(SX|QD|UZ|ZX)(BD|BQ|BW?)(Y|Z128?)(rr|rrk|rrkz)",
        "(V?)PMOV(SX|US|ZX)(DQ|WD|QW|WQ?)(Y|Z128?)(rr|rrk|rrkz)",
        "VPMOV(DB|DW|QB|QD|QW|SDB|SDW|SQB|SQD|SQW|SWB|USDB|USDW|USQB|USQD|USWB|WB)(Z128?|Z256?)(rr|rrk|rrkz)"
	)>;

def Zn4MOVSZ: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4MOVSZ], (instregex
        "(V?)PMOV(SX|ZX)(BD|BQ|BW|WD|WQ|DQ)(Z?)(rr|rrk|rrkz)"
	)>;

def Zn4MOVSrr: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 5;
  let ReleaseAtCycles = [5];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4MOVSrr], (instregex
        "(V?)PMOV(DB|QB|QW|SDB|SQB|SQW|USDB|USQB|USQW)(Z?)(rr|rrk|rrkz)"
	)>;


//VPTEST Instructions
def Zn4VPTESTZ128: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 3;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4VPTESTZ128], (instregex
        "(V?)PTEST(N?)(MB|MD|MQ|MW)(Z128?)(rrk)"
	)>;

def Zn4VPTESTZ256: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4VPTESTZ256], (instregex
        "(V?)PTEST(N?)(MB|MD|MQ|MW)(Z256?)(rr|rrk)"
	)>;

def Zn4VPTESTZ: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 5;
  let ReleaseAtCycles = [5];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4VPTESTZ], (instregex
        "(V?)PTEST(N?)(MB|MD|MQ|MW)(Z?)(rrk)"
	)>;

// CONFLICT Instructions
def Zn4CONFLICTZ128: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4CONFLICTZ128], (instregex
        "VPCONFLICT(D|Q)(Z128)(rr|rrk|rrkz)"
	)>;

def Zn4CONFLICTrr: SchedWriteRes<[Zn4FPFMisc01,Zn4FPFMisc12,Zn4FPFMisc23]> {
  let Latency = 6;
  let ReleaseAtCycles = [2,2,2];
  let NumMicroOps = 4;
}
def : InstRW<[Zn4CONFLICTrr], (instregex
        "VPCONFLICT(D|Q)(Z|Z256)(rr|rrkz)"
	)>;

// RSQRT Instructions
def Zn4VRSQRT14PDZ256: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 5;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4VRSQRT14PDZ256], (instregex
        "VRSQRT14(PD|PS)(Z?|Z128?|Z256?)(r|rr|rk|rrk|rkz|rrkz)"
	)>;


// PERM Instructions
def Zn4PERMILP: SchedWriteRes<[Zn4FPFMisc123]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4PERMILP], (instregex
        "VPERMILP(S|D)(Y|Z|Z128|Z256)(rr|rrk|rrkz)"
	)>;

def Zn4PERMIT2_128: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 3;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4PERMIT2_128], (instregex
	"VPERM(I2|T2)(PS|PD|W)Z128(rr|rrk|rrkz)",
	"VPERM(I2|T2)(B|D|Q)Z128(rr|rrk|rrkz)"
	)>;

def Zn4PERMIT2_128rr:SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4PERMIT2_128rr], (instregex
	"V(P?)COMPRESS(B|W|D|Q|PD|PS|SD|SQ)Z128(rr|rrk|rrkz)",
	"VPERM(B|D|Q|W)(Z128?)(rr|rrk|rrkz)"
	)>;

def Zn4PERMIT2_256: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4PERMIT2_256], (instregex
	"VPERM(I2|T2)(PS|PD|W)Z256(rr|rrk|rrkz)",
	"VPERMP(S|D)Z256(rr|rrk|rrkz)",
	"V(P?)COMPRESS(B|W|D|Q|PD|PS|SD|SQ)Z256(rr|rrk|rrkz)",
	"VPERM(B|D|Q|W)Z256(rr|rrk|rrkz)",
	"VPERM(I2|Q|T2)(B|D|Q)Z256(rr|rrk|rrkz)",
	"VPEXPAND(B|W)Z256(rr|rrk|rrkz)"
	)>;

def Zn4PERMIT2Z: SchedWriteRes<[Zn4FPFMisc12]> {
  let Latency = 5;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4PERMIT2Z], (instregex
	"VPERM(I2|T2)(PS|PD|W)Z(rr|rrk|rrkz)",
	"VPERM(B|D|W)Z(rr|rrk|rrkz)",
	"VPERM(I2|Q|T2)(B|D|Q)Z(rr|rrk|rrkz)",
	"V(P?)COMPRESS(B|W|D|Q|PD|PS|SD|SQ)Z(rr|rrk|rrkz)",
	"VPEXPAND(B|W)Z(rr|rrk|rrkz)",
	"VPERMP(S|D)Z(rr|rrk|rrkz)"
	)>;

// ALU SLOW Misc Instructions
def Zn4VecALUZSlow: SchedWriteRes<[Zn4FPFMisc01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn4VecALUZSlow], (instrs 
	VPABSBZ128rr,      VPABSBZ128rrk,  VPABSBZ128rrkz,   VPABSDZ128rr, 
	VPABSDZ128rrk,     VPABSDZ128rrkz, VPABSQZ128rr,     VPABSQZ128rrk, 
	VPABSQZ128rrkz,    VPABSWZ128rr,   VPABSWZ128rrk,    VPABSWZ128rrkz, 
	VPADDSBZ128rr,     VPADDSBZ128rrk, VPADDSBZ128rrkz,  VPADDSWZ128rr, 
	VPADDSWZ128rrk,    VPADDSWZ128rrkz,VPADDUSBZ128rr,   VPADDUSBZ128rrk, 
	VPADDUSBZ128rrkz,  VPADDUSWZ128rr, VPADDUSWZ128rrk,  VPADDUSWZ128rrkz, 
	VPAVGBZ128rr,      VPAVGBZ128rrk,  VPAVGBZ128rrkz,   VPAVGWZ128rr, 
	VPAVGWZ128rrk,     VPAVGWZ128rrkz, VPOPCNTBZ128rr,   VPOPCNTBZ128rrk, 
	VPOPCNTBZ128rrkz,  VPOPCNTDZ128rr, VPOPCNTDZ128rrk,  VPOPCNTDZ128rrkz, 
	VPOPCNTQZ128rr,    VPOPCNTQZ128rrk,VPOPCNTQZ128rrkz, VPOPCNTWZ128rr, 
	VPOPCNTWZ128rrk,   VPOPCNTWZ128rrkz,VPSUBSBZ128rr,   VPSUBSBZ128rrk, 
	VPSUBSBZ128rrkz,   VPSUBSWZ128rr,   VPSUBSWZ128rrk,  VPSUBSWZ128rrkz, 
	VPSUBUSBZ128rr,    VPSUBUSBZ128rrk, VPSUBUSBZ128rrkz,VPSUBUSWZ128rr, 
	VPSUBUSWZ128rrk,   VPSUBUSWZ128rrkz
	)>;


///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

def Zn4WriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[Zn4WriteZeroIdiom], (instrs XOR32rr, XOR32rr_REV,
                                          XOR64rr, XOR64rr_REV,
                                          SUB32rr, SUB32rr_REV,
                                          SUB64rr, SUB64rr_REV)>;

def Zn4WriteZeroIdiomEFLAGS : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<CheckSameRegOperand<0, 1>>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                                 [WriteALU]>
]>;
def : InstRW<[Zn4WriteZeroIdiomEFLAGS], (instrs CMP8rr,  CMP8rr_REV,
                                                CMP16rr, CMP16rr_REV,
                                                CMP32rr, CMP32rr_REV,
                                                CMP64rr, CMP64rr_REV)>;

def Zn4WriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
// NOTE: XORPSrr, XORPDrr are not zero-cycle!
def : InstRW<[Zn4WriteFZeroIdiom], (instrs VXORPSrr, VXORPDrr,
                                           VANDNPSrr, VANDNPDrr)>;

def Zn4WriteFZeroIdiomY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogicY]>
]>;
def : InstRW<[Zn4WriteFZeroIdiomY], (instrs VXORPSYrr, VXORPDYrr,
                                            VANDNPSYrr, VANDNPDYrr)>;

def Zn4WriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
// NOTE: PXORrr,PANDNrr are not zero-cycle!
def : InstRW<[Zn4WriteVZeroIdiomLogicX], (instrs VPXORrr, VPANDNrr)>;

def Zn4WriteVZeroIdiomLogicY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicY]>
]>;
def : InstRW<[Zn4WriteVZeroIdiomLogicY], (instrs VPXORYrr, VPANDNYrr)>;

def Zn4WriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
// NOTE: PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
//       PCMPGTBrr, PCMPGTWrr, PCMPGTDrr, PCMPGTQrr are not zero-cycle!
def : InstRW<[Zn4WriteVZeroIdiomALUX],
             (instrs VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
                     VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr)>;

def Zn4WriteVZeroIdiomALUY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn4WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUY]>
]>;
def : InstRW<[Zn4WriteVZeroIdiomALUY],
             (instrs VPSUBBYrr, VPSUBWYrr, VPSUBDYrr, VPSUBQYrr,
                     VPCMPGTBYrr, VPCMPGTWYrr, VPCMPGTDYrr, VPCMPGTQYrr)>;

def : IsZeroIdiomFunction<[
  // GPR Zero-idioms.
  DepBreakingClass<[ XOR32rr, XOR32rr_REV,
                     XOR64rr, XOR64rr_REV,
                     SUB32rr, SUB32rr_REV,
                     SUB64rr, SUB64rr_REV ], ZeroIdiomPredicate>,

  // SSE XMM Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    XORPSrr, XORPDrr,
    ANDNPSrr, ANDNPDrr,

    // int variants.
    PXORrr,
    PANDNrr,
    PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
    PSUBSBrr, PSUBSWrr,
    PSUBUSBrr, PSUBUSWrr,
    PCMPGTBrr, PCMPGTWrr, PCMPGTDrr, PCMPGTQrr
  ], ZeroIdiomPredicate>,

  // AVX XMM Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    VXORPSrr, VXORPDrr,
    VANDNPSrr, VANDNPDrr,

    // int variants.
    VPXORrr,
    VPANDNrr,
    VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
    VPSUBSBrr, VPSUBSWrr,
    VPSUBUSBrr, VPSUBUSWrr,
    VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr,
  ], ZeroIdiomPredicate>,

  // AVX YMM Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    VXORPSYrr, VXORPDYrr,
    VANDNPSYrr, VANDNPDYrr,

    // int variants.
    VPXORYrr,
    VPANDNYrr,
    VPSUBBYrr, VPSUBWYrr, VPSUBDYrr, VPSUBQYrr,
    VPSUBSBYrr, VPSUBSWYrr,
    VPSUBUSBYrr, VPSUBUSWYrr,
    VPCMPGTBYrr, VPCMPGTWYrr, VPCMPGTDYrr, VPCMPGTQYrr
  ], ZeroIdiomPredicate>,
]>;

def : IsDepBreakingFunction<[
  // GPR
  DepBreakingClass<[ SBB32rr, SBB32rr_REV,
                     SBB64rr, SBB64rr_REV ], ZeroIdiomPredicate>,
  DepBreakingClass<[ CMP8rr,  CMP8rr_REV,
                     CMP16rr, CMP16rr_REV,
                     CMP32rr, CMP32rr_REV,
                     CMP64rr, CMP64rr_REV ], CheckSameRegOperand<0, 1> >,
  // SSE
  DepBreakingClass<[
    PCMPEQBrr, PCMPEQWrr, PCMPEQDrr, PCMPEQQrr
  ], ZeroIdiomPredicate>,

  // AVX XMM
  DepBreakingClass<[
    VPCMPEQBrr, VPCMPEQWrr, VPCMPEQDrr, VPCMPEQQrr
  ], ZeroIdiomPredicate>,

  // AVX YMM
  DepBreakingClass<[
    VPCMPEQBYrr, VPCMPEQWYrr, VPCMPEQDYrr, VPCMPEQQYrr
  ], ZeroIdiomPredicate>,
]>;

} // SchedModel

