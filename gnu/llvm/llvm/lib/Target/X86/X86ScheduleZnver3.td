//=- X86ScheduleZnver3.td - X86 Znver3 Scheduling ------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Znver3 to support instruction
// scheduling and other instruction cost heuristics.
// Based on:
//  * AMD Software Optimization Guide for AMD Family 19h Processors.
//    https://www.amd.com/system/files/TechDocs/56665.zip
//  * The microarchitecture of Intel, AMD and VIA CPUs, By Agner Fog
//    http://www.agner.org/optimize/microarchitecture.pdf
//  * AMD Zen 3 Ryzen Deep Dive Review
//    https://www.anandtech.com/show/16214/
//===----------------------------------------------------------------------===//

def Znver3Model : SchedMachineModel {
  // AMD SOG 19h, 2.9.6 Dispatch
  // The processor may dispatch up to 6 macro ops per cycle
  // into the execution engine.
  let IssueWidth = 6;
  // AMD SOG 19h, 2.10.3
  // The retire control unit (RCU) tracks the completion status of all
  // outstanding operations (integer, load/store, and floating-point) and is
  // the final arbiter for exception processing and recovery.
  // The unit can receive up to 6 macro ops dispatched per cycle and track up
  // to 256 macro ops in-flight in non-SMT mode or 128 per thread in SMT mode.
  let MicroOpBufferSize = 256;
  // AMD SOG 19h, 2.9.1 Op Cache
  // The op cache is organized as an associative cache with 64 sets and 8 ways.
  // At each set-way intersection is an entry containing up to 8 macro ops.
  // The maximum capacity of the op cache is 4K ops.
  // Assuming a maximum dispatch of 8 ops/cy and a mispredict cost of 12cy from
  // the op-cache, we limit the loop buffer to 8*12 = 96 to avoid loop unrolling
  // leading to excessive filling of the op-cache from frontend.
  let LoopMicroOpBufferSize = 96;
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

let SchedModel = Znver3Model in {


//===----------------------------------------------------------------------===//
// RCU
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.3 Retire Control Unit
// The unit can receive up to 6 macro ops dispatched per cycle and track up to
// 256 macro ops in-flight in non-SMT mode or 128 per thread in SMT mode. <...>
// The retire unit handles in-order commit of up to eight macro ops per cycle.
def Zn3RCU : RetireControlUnit<Znver3Model.MicroOpBufferSize, 8>;

//===----------------------------------------------------------------------===//
// Units
//===----------------------------------------------------------------------===//

// There are total of three Units, each one with it's own schedulers.

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
def Zn3ALU0 : ProcResource<1>;
def Zn3ALU1 : ProcResource<1>;
def Zn3ALU2 : ProcResource<1>;
def Zn3ALU3 : ProcResource<1>;

// AMD SOG 19h, 2.10.2 Execution Units
// There is also a separate branch execution unit.
def Zn3BRU1 : ProcResource<1>;

// AMD SOG 19h, 2.10.2 Execution Units
// There are three Address Generation Units (AGUs) for all load and store
// address generation. There are also 3 store data movement units
// associated with the same schedulers as the AGUs.
def Zn3AGU0 : ProcResource<1>;
def Zn3AGU1 : ProcResource<1>;
def Zn3AGU2 : ProcResource<1>;

//
// Execution Units
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.2 Execution Units
// ALU0 additionally has divide <...> execution capability.
defvar Zn3Divider = Zn3ALU0;

// AMD SOG 19h, 2.10.2 Execution Units
// ALU0 additionally has <...> branch execution capability.
defvar Zn3BRU0 = Zn3ALU0;

// Integer Multiplication issued on ALU1.
defvar Zn3Multiplier = Zn3ALU1;

// Execution pipeline grouping
//===----------------------------------------------------------------------===//

// General ALU operations
def Zn3ALU0123 : ProcResGroup<[Zn3ALU0, Zn3ALU1, Zn3ALU2, Zn3ALU3]>;

// General AGU operations
def Zn3AGU012 : ProcResGroup<[Zn3AGU0, Zn3AGU1, Zn3AGU2]>;

// Control flow: jumps, calls
def Zn3BRU01 : ProcResGroup<[Zn3BRU0, Zn3BRU1]>;

// Everything that isn't control flow, but still needs to access CC register,
// namely: conditional moves, SETcc.
def Zn3ALU03 : ProcResGroup<[Zn3ALU0, Zn3ALU3]>;

// Zn3ALU1 handles complex bit twiddling: CRC/PDEP/PEXT

// Simple bit twiddling: bit test, shift/rotate, bit extraction
def Zn3ALU12 : ProcResGroup<[Zn3ALU1, Zn3ALU2]>;


//
// Scheduling
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.10.3 Retire Control Unit
// The integer physical register file (PRF) consists of 192 registers.
def Zn3IntegerPRF : RegisterFile<192, [GR64, CCR], [1, 1], [1, 0],
                              6,  // Max moves that can be eliminated per cycle.
                              0>; // Restrict move elimination to zero regs.

// anandtech, The integer scheduler has a 4*24 entry macro op capacity.
// AMD SOG 19h, 2.10.1 Schedulers
// The schedulers can receive up to six macro ops per cycle, with a limit of
// two per scheduler. Each scheduler can issue one micro op per cycle into
// each of its associated pipelines
// FIXME: these are 4 separate schedulers, not a single big one.
def Zn3Int : ProcResGroup<[Zn3ALU0, Zn3AGU0, Zn3BRU0, // scheduler 0
                           Zn3ALU1, Zn3AGU1,          // scheduler 1
                           Zn3ALU2, Zn3AGU2,          // scheduler 2
                           Zn3ALU3,          Zn3BRU1  // scheduler 3
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
def Zn3FP0  : ProcResource<1>;
def Zn3FP1  : ProcResource<1>;
def Zn3FP2  : ProcResource<1>;
def Zn3FP3  : ProcResource<1>;
def Zn3FP45 : ProcResource<2>;

//
// Execution Units
//===----------------------------------------------------------------------===//
// AMD SOG 19h, 2.11.1 Floating Point Execution Resources

// (v)FMUL*, (v)FMA*, Floating Point Compares, Blendv(DQ)
defvar Zn3FPFMul0 = Zn3FP0;
defvar Zn3FPFMul1 = Zn3FP1;

// (v)FADD*
defvar Zn3FPFAdd0 = Zn3FP2;
defvar Zn3FPFAdd1 = Zn3FP3;

// All convert operations except pack/unpack
defvar Zn3FPFCvt0 = Zn3FP2;
defvar Zn3FPFCvt1 = Zn3FP3;

// All Divide and Square Root except Reciprocal Approximation
// AMD SOG 19h, 2.11.1 Floating Point Execution Resources
// FDIV unit can support 2 simultaneous operations in flight
// even though it occupies a single pipe.
// FIXME: BufferSize=2 ?
defvar Zn3FPFDiv = Zn3FP1;

// Moves and Logical operations on Floating Point Data Types
defvar Zn3FPFMisc0 = Zn3FP0;
defvar Zn3FPFMisc1 = Zn3FP1;
defvar Zn3FPFMisc2 = Zn3FP2;
defvar Zn3FPFMisc3 = Zn3FP3;

// Integer Adds, Subtracts, and Compares
// Some complex VADD operations are not available in all pipes.
defvar Zn3FPVAdd0 = Zn3FP0;
defvar Zn3FPVAdd1 = Zn3FP1;
defvar Zn3FPVAdd2 = Zn3FP2;
defvar Zn3FPVAdd3 = Zn3FP3;

// Integer Multiplies, SAD, Blendvb
defvar Zn3FPVMul0 = Zn3FP0;
defvar Zn3FPVMul1 = Zn3FP3;

// Data Shuffles, Packs, Unpacks, Permute
// Some complex shuffle operations are only available in pipe1.
defvar Zn3FPVShuf = Zn3FP1;
defvar Zn3FPVShufAux = Zn3FP2;

// Bit Shift Left/Right operations
defvar Zn3FPVShift0 = Zn3FP1;
defvar Zn3FPVShift1 = Zn3FP2;

// Moves and Logical operations on Packed Integer Data Types
defvar Zn3FPVMisc0 = Zn3FP0;
defvar Zn3FPVMisc1 = Zn3FP1;
defvar Zn3FPVMisc2 = Zn3FP2;
defvar Zn3FPVMisc3 = Zn3FP3;

// *AES*
defvar Zn3FPAES0 = Zn3FP0;
defvar Zn3FPAES1 = Zn3FP1;

// *CLM*
defvar Zn3FPCLM0 = Zn3FP0;
defvar Zn3FPCLM1 = Zn3FP1;

// Execution pipeline grouping
//===----------------------------------------------------------------------===//

// AMD SOG 19h, 2.11 Floating-Point Unit
// Stores and floating point to general purpose register transfer
// have 2 dedicated pipelines (pipe 5 and 6).
def Zn3FPU0123 : ProcResGroup<[Zn3FP0, Zn3FP1, Zn3FP2, Zn3FP3]>;

// (v)FMUL*, (v)FMA*, Floating Point Compares, Blendv(DQ)
def Zn3FPFMul01 : ProcResGroup<[Zn3FPFMul0, Zn3FPFMul1]>;

// (v)FADD*
// Some complex VADD operations are not available in all pipes.
def Zn3FPFAdd01 : ProcResGroup<[Zn3FPFAdd0, Zn3FPFAdd1]>;

// All convert operations except pack/unpack
def Zn3FPFCvt01 : ProcResGroup<[Zn3FPFCvt0, Zn3FPFCvt1]>;

// All Divide and Square Root except Reciprocal Approximation
// def Zn3FPFDiv : ProcResGroup<[Zn3FPFDiv]>;

// Moves and Logical operations on Floating Point Data Types
def Zn3FPFMisc0123 : ProcResGroup<[Zn3FPFMisc0, Zn3FPFMisc1, Zn3FPFMisc2, Zn3FPFMisc3]>;

def Zn3FPFMisc12 : ProcResGroup<[Zn3FPFMisc1, Zn3FPFMisc2]>;

// Loads, Stores and Move to General Register (EX) Operations
// AMD SOG 19h, 2.11 Floating-Point Unit
// Stores and floating point to general purpose register transfer
// have 2 dedicated pipelines (pipe 5 and 6).
defvar Zn3FPLd01 = Zn3FP45;

// AMD SOG 19h, 2.11 Floating-Point Unit
// Note that FP stores are supported on two pipelines,
// but throughput is limited to one per cycle.
let Super = Zn3FP45 in
def Zn3FPSt : ProcResource<1>;

// Integer Adds, Subtracts, and Compares
// Some complex VADD operations are not available in all pipes.
def Zn3FPVAdd0123 : ProcResGroup<[Zn3FPVAdd0, Zn3FPVAdd1, Zn3FPVAdd2, Zn3FPVAdd3]>;

def Zn3FPVAdd01: ProcResGroup<[Zn3FPVAdd0, Zn3FPVAdd1]>;
def Zn3FPVAdd12: ProcResGroup<[Zn3FPVAdd1, Zn3FPVAdd2]>;

// Integer Multiplies, SAD, Blendvb
def Zn3FPVMul01 : ProcResGroup<[Zn3FPVMul0, Zn3FPVMul1]>;

// Data Shuffles, Packs, Unpacks, Permute
// Some complex shuffle operations are only available in pipe1.
def Zn3FPVShuf01 : ProcResGroup<[Zn3FPVShuf, Zn3FPVShufAux]>;

// Bit Shift Left/Right operations
def Zn3FPVShift01 : ProcResGroup<[Zn3FPVShift0, Zn3FPVShift1]>;

// Moves and Logical operations on Packed Integer Data Types
def Zn3FPVMisc0123 : ProcResGroup<[Zn3FPVMisc0, Zn3FPVMisc1, Zn3FPVMisc2, Zn3FPVMisc3]>;

// *AES*
def Zn3FPAES01 : ProcResGroup<[Zn3FPAES0, Zn3FPAES1]>;

// *CLM*
def Zn3FPCLM01 : ProcResGroup<[Zn3FPCLM0, Zn3FPCLM1]>;


//
// Scheduling
//===----------------------------------------------------------------------===//

// Agner, 21.8 Register renaming and out-of-order schedulers
// The floating point register file has 160 vector registers
// of 128 bits each in Zen 1 and 256 bits each in Zen 2.
// anandtech also confirms this.
def Zn3FpPRF : RegisterFile<160, [VR64, VR128, VR256], [1, 1, 1], [0, 1, 1],
                            6,  // Max moves that can be eliminated per cycle.
                            0>; // Restrict move elimination to zero regs.

// AMD SOG 19h, 2.11 Floating-Point Unit
// The floating-point scheduler has a 2*32 entry macro op capacity.
// AMD SOG 19h, 2.11 Floating-Point Unit
// <...> the scheduler can issue 1 micro op per cycle for each pipe.
// FIXME: those are two separate schedulers, not a single big one.
def Zn3FP : ProcResGroup<[Zn3FP0, Zn3FP2,          /*Zn3FP4,*/ // scheduler 0
                          Zn3FP1, Zn3FP3, Zn3FP45 /*Zn3FP5*/  // scheduler 1
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
def Zn3LSU : ProcResource<3>;

// AMD SOG 19h, 2.12 Load-Store Unit
// All three memory operations can be loads.
let Super = Zn3LSU in
def Zn3Load : ProcResource<3> {
  // AMD SOG 19h, 2.12 Load-Store Unit
  // The LS unit can process up to 72 out-of-order loads.
  let BufferSize = 72;
}

def Zn3LoadQueue : LoadQueue<Zn3Load>;

// AMD SOG 19h, 2.12 Load-Store Unit
// A maximum of two of the memory operations can be stores.
let Super = Zn3LSU in
def Zn3Store : ProcResource<2> {
  // AMD SOG 19h, 2.12 Load-Store Unit
  // The LS unit utilizes a 64-entry store queue (STQ).
  let BufferSize = 64;
}

def Zn3StoreQueue : StoreQueue<Zn3Store>;

//===----------------------------------------------------------------------===//
// Basic helper classes.
//===----------------------------------------------------------------------===//

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when dispatched by the schedulers.
// This multiclass defines the resource usage for variants with and without
// folded loads.

multiclass __zn3WriteRes<SchedWrite SchedRW, list<ProcResourceKind> ExePorts,
                         int Lat = 1, list<int> Res = [], int UOps = 1> {
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }
}

multiclass __zn3WriteResPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat,
                             list<int> Res, int UOps, int LoadLat, int LoadUOps,
                             ProcResourceKind AGU, int LoadRes> {
  defm : __zn3WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;

  defm : __zn3WriteRes<SchedRW.Folded,
                       !listconcat([AGU, Zn3Load], ExePorts),
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
multiclass Zn3WriteResInt<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __zn3WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

multiclass Zn3WriteResXMM<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __zn3WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

multiclass Zn3WriteResYMM<SchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts, int Lat = 1,
                          list<int> Res = [], int UOps = 1> {
  defm : __zn3WriteRes<SchedRW, ExePorts, Lat, Res, UOps>;
}

// For classes with folded loads.
multiclass Zn3WriteResIntPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __zn3WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver3Model.LoadLatency,
                           LoadUOps, Zn3AGU012, LoadRes>;
}

multiclass Zn3WriteResXMMPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __zn3WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver3Model.VecLoadLatency,
                           LoadUOps, Zn3FPLd01, LoadRes>;
}

multiclass Zn3WriteResYMMPair<X86FoldableSchedWrite SchedRW,
                              list<ProcResourceKind> ExePorts, int Lat = 1,
                              list<int> Res = [], int UOps = 1,
                              int LoadUOps = 0, int LoadRes = 1> {
  defm : __zn3WriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           Znver3Model.VecLoadLatency,
                           LoadUOps, Zn3FPLd01, LoadRes>;
}


//===----------------------------------------------------------------------===//
// Here be dragons.
//===----------------------------------------------------------------------===//

def : ReadAdvance<ReadAfterLd, Znver3Model.LoadLatency>;

def : ReadAdvance<ReadAfterVecLd, Znver3Model.VecLoadLatency>;
def : ReadAdvance<ReadAfterVecXLd, Znver3Model.VecLoadLatency>;
def : ReadAdvance<ReadAfterVecYLd, Znver3Model.VecLoadLatency>;

// AMD SOG 19h, 2.11 Floating-Point Unit
// There is 1 cycle of added latency for a result to cross
// from F to I or I to F domain.
def : ReadAdvance<ReadInt2Fpu, -1>;

// Instructions with both a load and a store folded are modeled as a folded
// load + WriteRMW.
defm : Zn3WriteResInt<WriteRMW, [Zn3AGU012, Zn3Store], Znver3Model.StoreLatency, [1, 1], 0>;

// Loads, stores, and moves, not folded with other operations.
defm : Zn3WriteResInt<WriteLoad, [Zn3AGU012, Zn3Load], !add(Znver3Model.LoadLatency, 1), [1, 1], 1>;

// Model the effect of clobbering the read-write mask operand of the GATHER operation.
// Does not cost anything by itself, only has latency, matching that of the WriteLoad,
defm : Zn3WriteResInt<WriteVecMaskedGatherWriteback, [], !add(Znver3Model.LoadLatency, 1), [], 0>;

def Zn3WriteMOVSlow : SchedWriteRes<[Zn3AGU012, Zn3Load]> {
  let Latency = !add(Znver3Model.LoadLatency, 1);
  let ReleaseAtCycles = [3, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteMOVSlow], (instrs MOV8rm, MOV8rm_NOREX, MOV16rm, MOVSX16rm16, MOVSX16rm32, MOVZX16rm16, MOVSX16rm8, MOVZX16rm8)>;

defm : Zn3WriteResInt<WriteStore, [Zn3AGU012, Zn3Store], Znver3Model.StoreLatency, [1, 2], 1>;
defm : Zn3WriteResInt<WriteStoreNT, [Zn3AGU012, Zn3Store], Znver3Model.StoreLatency, [1, 2], 1>;
defm : Zn3WriteResInt<WriteMove, [Zn3ALU0123], 1, [4], 1>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

def Zn3WriteMOVBE16rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU0123]> {
  let Latency = Znver3Model.LoadLatency;
  let ReleaseAtCycles = [1, 1, 4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteMOVBE16rm], (instrs MOVBE16rm)>;

def Zn3WriteMOVBEmr : SchedWriteRes<[Zn3ALU0123, Zn3AGU012, Zn3Store]> {
  let Latency = Znver3Model.StoreLatency;
  let ReleaseAtCycles = [4, 1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteMOVBEmr], (instrs MOVBE16mr, MOVBE32mr, MOVBE64mr)>;

// Arithmetic.
defm : Zn3WriteResIntPair<WriteALU, [Zn3ALU0123], 1, [1], 1>; // Simple integer ALU op.

def Zn3WriteALUSlow : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteALUSlow], (instrs ADD8i8, ADD16i16, ADD32i32, ADD64i32,
                                        AND8i8, AND16i16, AND32i32, AND64i32,
                                         OR8i8,  OR16i16,  OR32i32,  OR64i32,
                                        SUB8i8, SUB16i16, SUB32i32, SUB64i32,
                                        XOR8i8, XOR16i16, XOR32i32, XOR64i32)>;

def Zn3WriteMoveExtend : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteMoveExtend], (instrs MOVSX16rr16, MOVSX16rr32, MOVZX16rr16, MOVSX16rr8, MOVZX16rr8)>;

def Zn3WriteMaterialize32bitImm: SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteMaterialize32bitImm], (instrs MOV32ri, MOV32ri_alt, MOV64ri32)>;

def Zn3WritePDEP_PEXT : SchedWriteRes<[Zn3ALU1]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WritePDEP_PEXT], (instrs PDEP32rr, PDEP64rr,
                                          PEXT32rr, PEXT64rr)>;

defm : Zn3WriteResIntPair<WriteADC, [Zn3ALU0123], 1, [4], 1>; // Integer ALU + flags op.

def Zn3WriteADC8mr_SBB8mr : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU0123, Zn3Store]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 1, 7, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteADC8mr_SBB8mr], (instrs ADC8mr, SBB8mr)>;

// This is for simple LEAs with one or two input operands.
defm : Zn3WriteResInt<WriteLEA, [Zn3AGU012], 1, [1], 1>;     // LEA instructions can't fold loads.

// This write is used for slow LEA instructions.
def Zn3Write3OpsLEA : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}

// On Znver3, a slow LEA is either a 3Ops LEA (base, index, offset),
// or an LEA with a `Scale` value different than 1.
def Zn3SlowLEAPredicate : MCSchedPredicate<
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

def Zn3WriteLEA : SchedWriteVariant<[
    SchedVar<Zn3SlowLEAPredicate, [Zn3Write3OpsLEA]>,
    SchedVar<NoSchedPred,         [WriteLEA]>
]>;

def : InstRW<[Zn3WriteLEA], (instrs LEA32r, LEA64r, LEA64_32r)>;

def Zn3SlowLEA16r : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 2; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [4];
  let NumMicroOps = 2;
}

def : InstRW<[Zn3SlowLEA16r], (instrs LEA16r)>;

// Integer multiplication
defm : Zn3WriteResIntPair<WriteIMul8, [Zn3Multiplier], 3, [3], 1>; // Integer 8-bit multiplication.
defm : Zn3WriteResIntPair<WriteIMul16, [Zn3Multiplier], 3, [3], 3, /*LoadUOps=*/1>; // Integer 16-bit multiplication.
defm : Zn3WriteResIntPair<WriteIMul16Imm, [Zn3Multiplier], 4, [4], 2>; // Integer 16-bit multiplication by immediate.
defm : Zn3WriteResIntPair<WriteIMul16Reg, [Zn3Multiplier], 3, [1], 1>; // Integer 16-bit multiplication by register.
defm : Zn3WriteResIntPair<WriteIMul32, [Zn3Multiplier], 3, [3], 2>;    // Integer 32-bit multiplication.
defm : Zn3WriteResIntPair<WriteMULX32, [Zn3Multiplier], 3, [1], 2>;    // Integer 32-bit Unsigned Multiply Without Affecting Flags.
defm : Zn3WriteResIntPair<WriteIMul32Imm, [Zn3Multiplier], 3, [1], 1>; // Integer 32-bit multiplication by immediate.
defm : Zn3WriteResIntPair<WriteIMul32Reg, [Zn3Multiplier], 3, [1], 1>; // Integer 32-bit multiplication by register.
defm : Zn3WriteResIntPair<WriteIMul64, [Zn3Multiplier], 3, [3], 2>;    // Integer 64-bit multiplication.
defm : Zn3WriteResIntPair<WriteMULX64, [Zn3Multiplier], 3, [1], 2>;    // Integer 32-bit Unsigned Multiply Without Affecting Flags.
defm : Zn3WriteResIntPair<WriteIMul64Imm, [Zn3Multiplier], 3, [1], 1>; // Integer 64-bit multiplication by immediate.
defm : Zn3WriteResIntPair<WriteIMul64Reg, [Zn3Multiplier], 3, [1], 1>; // Integer 64-bit multiplication by register.
defm : Zn3WriteResInt<WriteIMulHLd, [], !add(4, Znver3Model.LoadLatency), [], 0>;  // Integer multiplication, high part.
defm : Zn3WriteResInt<WriteIMulH, [], 4, [], 0>;  // Integer multiplication, high part.

defm : Zn3WriteResInt<WriteBSWAP32, [Zn3ALU0123], 1, [1], 1>; // Byte Order (Endianness) 32-bit Swap.
defm : Zn3WriteResInt<WriteBSWAP64, [Zn3ALU0123], 1, [1], 1>; // Byte Order (Endianness) 64-bit Swap.

defm : Zn3WriteResIntPair<WriteCMPXCHG, [Zn3ALU0123], 3, [12], 5>; // Compare and set, compare and swap.

def Zn3WriteCMPXCHG8rr : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 3;
  let ReleaseAtCycles = [12];
  let NumMicroOps = 3;
}
def : InstRW<[Zn3WriteCMPXCHG8rr], (instrs CMPXCHG8rr)>;

defm : Zn3WriteResInt<WriteCMPXCHGRMW, [Zn3ALU0123], 3, [12], 6>;     // Compare and set, compare and swap.

def Zn3WriteCMPXCHG8rm_LCMPXCHG8 : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteCMPXCHG8rr.Latency);
  let ReleaseAtCycles = [1, 1, 12];
  let NumMicroOps = !add(Zn3WriteCMPXCHG8rr.NumMicroOps, 2);
}
def : InstRW<[Zn3WriteCMPXCHG8rm_LCMPXCHG8], (instrs CMPXCHG8rm, LCMPXCHG8)>;

def Zn3WriteCMPXCHG8B : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 3; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [24];
  let NumMicroOps = 19;
}
def : InstRW<[Zn3WriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def Zn3WriteCMPXCHG16B_LCMPXCHG16B : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 4; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [59];
  let NumMicroOps = 28;
}
def : InstRW<[Zn3WriteCMPXCHG16B_LCMPXCHG16B], (instrs CMPXCHG16B, LCMPXCHG16B)>;

def Zn3WriteWriteXCHGUnrenameable : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteWriteXCHGUnrenameable], (instrs XCHG8rr, XCHG16rr, XCHG16ar)>;

def Zn3WriteXCHG8rm_XCHG16rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, 3); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 5;
}
def : InstRW<[Zn3WriteXCHG8rm_XCHG16rm], (instrs XCHG8rm, XCHG16rm)>;

def Zn3WriteXCHG32rm_XCHG64rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, 2); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteXCHG32rm_XCHG64rm], (instrs XCHG32rm, XCHG64rm)>;

// Integer division.
// FIXME: uops for 8-bit division measures as 2. for others it's a guess.
// FIXME: latency for 8-bit division measures as 10. for others it's a guess.
defm : Zn3WriteResIntPair<WriteDiv8, [Zn3Divider], 10, [10], 2>;
defm : Zn3WriteResIntPair<WriteDiv16, [Zn3Divider], 11, [11], 2>;
defm : Zn3WriteResIntPair<WriteDiv32, [Zn3Divider], 13, [13], 2>;
defm : Zn3WriteResIntPair<WriteDiv64, [Zn3Divider], 17, [17], 2>;
defm : Zn3WriteResIntPair<WriteIDiv8, [Zn3Divider], 10, [10], 2>;
defm : Zn3WriteResIntPair<WriteIDiv16, [Zn3Divider], 11, [11], 2>;
defm : Zn3WriteResIntPair<WriteIDiv32, [Zn3Divider], 13, [13], 2>;
defm : Zn3WriteResIntPair<WriteIDiv64, [Zn3Divider], 17, [17], 2>;

defm : Zn3WriteResIntPair<WriteBSF, [Zn3ALU1], 3, [3], 6, /*LoadUOps=*/2>; // Bit scan forward.
defm : Zn3WriteResIntPair<WriteBSR, [Zn3ALU1], 4, [4], 6, /*LoadUOps=*/2>; // Bit scan reverse.

defm : Zn3WriteResIntPair<WritePOPCNT, [Zn3ALU0123], 1, [1], 1>; // Bit population count.

def Zn3WritePOPCNT16rr : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WritePOPCNT16rr], (instrs POPCNT16rr)>;

defm : Zn3WriteResIntPair<WriteLZCNT, [Zn3ALU0123], 1, [1], 1>; // Leading zero count.

def Zn3WriteLZCNT16rr : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteLZCNT16rr], (instrs LZCNT16rr)>;

defm : Zn3WriteResIntPair<WriteTZCNT, [Zn3ALU12], 2, [1], 2>; // Trailing zero count.

def Zn3WriteTZCNT16rr : SchedWriteRes<[Zn3ALU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [4];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteTZCNT16rr], (instrs TZCNT16rr)>;

defm : Zn3WriteResIntPair<WriteCMOV, [Zn3ALU03], 1, [1], 1>; // Conditional move.
defm : Zn3WriteResInt<WriteFCMOV, [Zn3ALU0123], 7, [28], 7>; // FIXME: not from llvm-exegesis // X87 conditional move.
defm : Zn3WriteResInt<WriteSETCC, [Zn3ALU03], 1, [2], 1>; // Set register based on condition code.
defm : Zn3WriteResInt<WriteSETCCStore, [Zn3ALU03, Zn3AGU012, Zn3Store], 2, [2, 1, 1], 2>; // FIXME: latency not from llvm-exegesis
defm : Zn3WriteResInt<WriteLAHFSAHF, [Zn3ALU3], 1, [1], 1>; // Load/Store flags in AH.

defm : Zn3WriteResInt<WriteBitTest, [Zn3ALU12], 1, [1], 1>; // Bit Test
defm : Zn3WriteResInt<WriteBitTestImmLd, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 1), [1, 1, 1], 2>;
defm : Zn3WriteResInt<WriteBitTestRegLd, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 1), [1, 1, 1], 7>;

defm : Zn3WriteResInt<WriteBitTestSet, [Zn3ALU12], 2, [2], 2>; // Bit Test + Set
defm : Zn3WriteResInt<WriteBitTestSetImmLd, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 2), [1, 1, 1], 4>;
defm : Zn3WriteResInt<WriteBitTestSetRegLd, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 2), [1, 1, 1], 9>;

// Integer shifts and rotates.
defm : Zn3WriteResIntPair<WriteShift, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn3WriteResIntPair<WriteShiftCL, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn3WriteResIntPair<WriteRotate, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;

def Zn3WriteRotateR1 : SchedWriteRes<[Zn3ALU12]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteRotateR1], (instrs RCL8r1, RCL16r1, RCL32r1, RCL64r1,
                                         RCR8r1, RCR16r1, RCR32r1, RCR64r1)>;

def Zn3WriteRotateM1 : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU12]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteRotateR1.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn3WriteRotateR1.NumMicroOps, 1);
}
def : InstRW<[Zn3WriteRotateM1], (instrs RCL8m1, RCL16m1, RCL32m1, RCL64m1,
                                         RCR8m1, RCR16m1, RCR32m1, RCR64m1)>;

def Zn3WriteRotateRightRI : SchedWriteRes<[Zn3ALU12]> {
  let Latency = 3;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 7;
}
def : InstRW<[Zn3WriteRotateRightRI], (instrs RCR8ri, RCR16ri, RCR32ri, RCR64ri)>;

def Zn3WriteRotateRightMI : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU12]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteRotateRightRI.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn3WriteRotateRightRI.NumMicroOps, 3);
}
def : InstRW<[Zn3WriteRotateRightMI], (instrs RCR8mi, RCR16mi, RCR32mi, RCR64mi)>;

def Zn3WriteRotateLeftRI : SchedWriteRes<[Zn3ALU12]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 9;
}
def : InstRW<[Zn3WriteRotateLeftRI], (instrs RCL8ri, RCL16ri, RCL32ri, RCL64ri)>;

def Zn3WriteRotateLeftMI : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU12]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteRotateLeftRI.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn3WriteRotateLeftRI.NumMicroOps, 2);
}
def : InstRW<[Zn3WriteRotateLeftMI], (instrs RCL8mi, RCL16mi, RCL32mi, RCL64mi)>;

defm : Zn3WriteResIntPair<WriteRotateCL, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;

def Zn3WriteRotateRightRCL : SchedWriteRes<[Zn3ALU12]> {
  let Latency = 3;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 7;
}
def : InstRW<[Zn3WriteRotateRightRCL], (instrs RCR8rCL, RCR16rCL, RCR32rCL, RCR64rCL)>;

def Zn3WriteRotateRightMCL : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU12]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteRotateRightRCL.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn3WriteRotateRightRCL.NumMicroOps, 2);
}
def : InstRW<[Zn3WriteRotateRightMCL], (instrs RCR8mCL, RCR16mCL, RCR32mCL, RCR64mCL)>;

def Zn3WriteRotateLeftRCL : SchedWriteRes<[Zn3ALU12]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 9;
}
def : InstRW<[Zn3WriteRotateLeftRCL], (instrs RCL8rCL, RCL16rCL, RCL32rCL, RCL64rCL)>;

def Zn3WriteRotateLeftMCL : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3ALU12]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteRotateLeftRCL.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn3WriteRotateLeftRCL.NumMicroOps, 2);
}
def : InstRW<[Zn3WriteRotateLeftMCL], (instrs RCL8mCL, RCL16mCL, RCL32mCL, RCL64mCL)>;

// Double shift instructions.
defm : Zn3WriteResInt<WriteSHDrri, [Zn3ALU12], 2, [3], 4>;
defm : Zn3WriteResInt<WriteSHDrrcl, [Zn3ALU12], 2, [3], 5>;
defm : Zn3WriteResInt<WriteSHDmri, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 2), [1, 1, 4], 6>;
defm : Zn3WriteResInt<WriteSHDmrcl, [Zn3AGU012, Zn3Load, Zn3ALU12], !add(Znver3Model.LoadLatency, 2), [1, 1, 4], 6>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : Zn3WriteResIntPair<WriteBEXTR, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;
defm : Zn3WriteResIntPair<WriteBLS, [Zn3ALU0123], 2, [2], 2, /*LoadUOps=*/1>;
defm : Zn3WriteResIntPair<WriteBZHI, [Zn3ALU12], 1, [1], 1, /*LoadUOps=*/1>;

// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
defm : Zn3WriteResInt<WriteZero, [Zn3ALU0123], 0, [0], 1>;

// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
defm : Zn3WriteResIntPair<WriteJump, [Zn3BRU01], 1, [1], 1>; // FIXME: not from llvm-exegesis

// Floating point. This covers both scalar and vector operations.
defm : Zn3WriteResInt<WriteFLD0, [Zn3FPLd01, Zn3Load, Zn3FP1], !add(Znver3Model.LoadLatency, 4), [1, 1, 1], 1>;
defm : Zn3WriteResInt<WriteFLD1, [Zn3FPLd01, Zn3Load, Zn3FP1], !add(Znver3Model.LoadLatency, 7), [1, 1, 1], 1>;
defm : Zn3WriteResInt<WriteFLDC, [Zn3FPLd01, Zn3Load, Zn3FP1], !add(Znver3Model.LoadLatency, 7), [1, 1, 1], 1>;
defm : Zn3WriteResXMM<WriteFLoad, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteFLoadX, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResYMM<WriteFLoadY, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteFMaskedLoad, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResYMM<WriteFMaskedLoadY, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteFStore, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;

def Zn3WriteWriteFStoreMMX : SchedWriteRes<[Zn3FPSt, Zn3Store]> {
  let Latency = 2; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteWriteFStoreMMX], (instrs MOVHPDmr,  MOVHPSmr,
                                               VMOVHPDmr, VMOVHPSmr)>;

defm : Zn3WriteResXMM<WriteFStoreX, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResYMM<WriteFStoreY, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResXMM<WriteFStoreNT, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResXMM<WriteFStoreNTX, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResYMM<WriteFStoreNTY, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;

defm : Zn3WriteResXMM<WriteFMaskedStore32, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [6, 1], 18>;
defm : Zn3WriteResXMM<WriteFMaskedStore64, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [4, 1], 10>;
defm : Zn3WriteResYMM<WriteFMaskedStore32Y, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [12, 1], 42>;
defm : Zn3WriteResYMM<WriteFMaskedStore64Y, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [6, 1], 18>;

defm : Zn3WriteResXMMPair<WriteFAdd, [Zn3FPFAdd01], 3, [1], 1>;  // Floating point add/sub.

def Zn3WriteX87Arith : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, 1); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 24];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteX87Arith], (instrs ADD_FI16m, ADD_FI32m,
                                         SUB_FI16m, SUB_FI32m,
                                         SUBR_FI16m, SUBR_FI32m,
                                         MUL_FI16m, MUL_FI32m)>;

def Zn3WriteX87Div : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, 1); // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1, 1, 62];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteX87Div], (instrs DIV_FI16m, DIV_FI32m,
                                       DIVR_FI16m, DIVR_FI32m)>;

defm : Zn3WriteResXMMPair<WriteFAddX, [Zn3FPFAdd01], 3, [1], 1>; // Floating point add/sub (XMM).
defm : Zn3WriteResYMMPair<WriteFAddY, [Zn3FPFAdd01], 3, [1], 1>; // Floating point add/sub (YMM).
defm : X86WriteResPairUnsupported<WriteFAddZ>; // Floating point add/sub (ZMM).
defm : Zn3WriteResXMMPair<WriteFAdd64, [Zn3FPFAdd01], 3, [1], 1>;  // Floating point double add/sub.
defm : Zn3WriteResXMMPair<WriteFAdd64X, [Zn3FPFAdd01], 3, [1], 1>; // Floating point double add/sub (XMM).
defm : Zn3WriteResYMMPair<WriteFAdd64Y, [Zn3FPFAdd01], 3, [1], 1>; // Floating point double add/sub (YMM).
defm : X86WriteResPairUnsupported<WriteFAdd64Z>; // Floating point double add/sub (ZMM).
defm : Zn3WriteResXMMPair<WriteFCmp, [Zn3FPFMul01], 1, [1], 1>;  // Floating point compare.
defm : Zn3WriteResXMMPair<WriteFCmpX, [Zn3FPFMul01], 1, [1], 1>; // Floating point compare (XMM).
defm : Zn3WriteResYMMPair<WriteFCmpY, [Zn3FPFMul01], 1, [1], 1>; // Floating point compare (YMM).
defm : X86WriteResPairUnsupported<WriteFCmpZ>; // Floating point compare (ZMM).
defm : Zn3WriteResXMMPair<WriteFCmp64, [Zn3FPFMul01], 1, [1], 1>;  // Floating point double compare.
defm : Zn3WriteResXMMPair<WriteFCmp64X, [Zn3FPFMul01], 1, [1], 1>; // Floating point double compare (XMM).
defm : Zn3WriteResYMMPair<WriteFCmp64Y, [Zn3FPFMul01], 1, [1], 1>; // Floating point double compare (YMM).
defm : X86WriteResPairUnsupported<WriteFCmp64Z>; // Floating point double compare (ZMM).
defm : Zn3WriteResXMMPair<WriteFCom, [Zn3FPFMul01], 3, [2], 1>; // FIXME: latency not from llvm-exegesis  // Floating point compare to flags (X87).
defm : Zn3WriteResXMMPair<WriteFComX, [Zn3FPFMul01], 4, [2], 2>;  // FIXME: latency not from llvm-exegesis // Floating point compare to flags (SSE).
defm : Zn3WriteResXMMPair<WriteFMul, [Zn3FPFMul01], 3, [1], 1>;  // Floating point multiplication.
defm : Zn3WriteResXMMPair<WriteFMulX, [Zn3FPFMul01], 3, [1], 1>; // Floating point multiplication (XMM).
defm : Zn3WriteResYMMPair<WriteFMulY, [Zn3FPFMul01], 3, [1], 1>; // Floating point multiplication (YMM).
defm : X86WriteResPairUnsupported<WriteFMulZ>; // Floating point multiplication (YMM).
defm : Zn3WriteResXMMPair<WriteFMul64, [Zn3FPFMul01], 3, [1], 1>;  // Floating point double multiplication.
defm : Zn3WriteResXMMPair<WriteFMul64X, [Zn3FPFMul01], 3, [1], 1>; // Floating point double multiplication (XMM).
defm : Zn3WriteResYMMPair<WriteFMul64Y, [Zn3FPFMul01], 3, [1], 1>; // Floating point double multiplication (YMM).
defm : X86WriteResPairUnsupported<WriteFMul64Z>; // Floating point double multiplication (ZMM).
defm : Zn3WriteResXMMPair<WriteFDiv, [Zn3FPFDiv], 11, [3], 1>;  // Floating point division.
defm : Zn3WriteResXMMPair<WriteFDivX, [Zn3FPFDiv], 11, [3], 1>; // Floating point division (XMM).
defm : Zn3WriteResYMMPair<WriteFDivY, [Zn3FPFDiv], 11, [3], 1>; // Floating point division (YMM).
defm : X86WriteResPairUnsupported<WriteFDivZ>; // Floating point division (ZMM).
defm : Zn3WriteResXMMPair<WriteFDiv64, [Zn3FPFDiv], 13, [5], 1>;  // Floating point double division.
defm : Zn3WriteResXMMPair<WriteFDiv64X, [Zn3FPFDiv], 13, [5], 1>; // Floating point double division (XMM).
defm : Zn3WriteResYMMPair<WriteFDiv64Y, [Zn3FPFDiv], 13, [5], 1>; // Floating point double division (YMM).
defm : X86WriteResPairUnsupported<WriteFDiv64Z>; // Floating point double division (ZMM).
defm : Zn3WriteResXMMPair<WriteFSqrt, [Zn3FPFDiv], 15, [5], 1>;   // Floating point square root.
defm : Zn3WriteResXMMPair<WriteFSqrtX, [Zn3FPFDiv], 15, [5], 1>;  // Floating point square root (XMM).
defm : Zn3WriteResYMMPair<WriteFSqrtY, [Zn3FPFDiv], 15, [5], 1>;  // Floating point square root (YMM).
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;  // Floating point square root (ZMM).
defm : Zn3WriteResXMMPair<WriteFSqrt64, [Zn3FPFDiv], 21, [9], 1>;  // Floating point double square root.
defm : Zn3WriteResXMMPair<WriteFSqrt64X, [Zn3FPFDiv], 21, [9], 1>; // Floating point double square root (XMM).
defm : Zn3WriteResYMMPair<WriteFSqrt64Y, [Zn3FPFDiv], 21, [9], 1>; // Floating point double square root (YMM).
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>; // Floating point double square root (ZMM).
defm : Zn3WriteResXMMPair<WriteFSqrt80, [Zn3FPFDiv], 22, [23], 1>; // FIXME: latency not from llvm-exegesis  // Floating point long double square root.
defm : Zn3WriteResXMMPair<WriteFRcp, [Zn3FPFMul01], 3, [1], 1>;  // Floating point reciprocal estimate.
defm : Zn3WriteResXMMPair<WriteFRcpX, [Zn3FPFMul01], 3, [1], 1>; // Floating point reciprocal estimate (XMM).
defm : Zn3WriteResYMMPair<WriteFRcpY, [Zn3FPFMul01], 3, [1], 1>; // Floating point reciprocal estimate (YMM).
defm : X86WriteResPairUnsupported<WriteFRcpZ>; // Floating point reciprocal estimate (ZMM).
defm : Zn3WriteResXMMPair<WriteFRsqrt, [Zn3FPFDiv], 3, [1], 1>;  // Floating point reciprocal square root estimate.
defm : Zn3WriteResXMMPair<WriteFRsqrtX, [Zn3FPFDiv], 3, [1], 1>; // Floating point reciprocal square root estimate (XMM).
defm : Zn3WriteResYMMPair<WriteFRsqrtY, [Zn3FPFDiv], 3, [1], 1>; // Floating point reciprocal square root estimate (YMM).
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>; // Floating point reciprocal square root estimate (ZMM).
defm : Zn3WriteResXMMPair<WriteFMA, [Zn3FPFMul01], 4, [1], 1>;  // Fused Multiply Add.
defm : Zn3WriteResXMMPair<WriteFMAX, [Zn3FPFMul01], 4, [1], 1>; // Fused Multiply Add (XMM).
defm : Zn3WriteResYMMPair<WriteFMAY, [Zn3FPFMul01], 4, [1], 1>; // Fused Multiply Add (YMM).
defm : X86WriteResPairUnsupported<WriteFMAZ>; // Fused Multiply Add (ZMM).
defm : Zn3WriteResXMMPair<WriteDPPD, [Zn3FPFMul01], 9, [6], 3, /*LoadUOps=*/2>; // Floating point double dot product.
defm : Zn3WriteResXMMPair<WriteDPPS, [Zn3FPFMul01], 15, [8], 8, /*LoadUOps=*/2>; // Floating point single dot product.
defm : Zn3WriteResYMMPair<WriteDPPSY, [Zn3FPFMul01], 15, [8], 7, /*LoadUOps=*/1>; // Floating point single dot product (YMM).
defm : Zn3WriteResXMMPair<WriteFSign, [Zn3FPFMul01], 1, [2], 1>; // FIXME: latency not from llvm-exegesis  // Floating point fabs/fchs.
defm : Zn3WriteResXMMPair<WriteFRnd, [Zn3FPFCvt01], 3, [1], 1>; // Floating point rounding.
defm : Zn3WriteResYMMPair<WriteFRndY, [Zn3FPFCvt01], 3, [1], 1>; // Floating point rounding (YMM).
defm : X86WriteResPairUnsupported<WriteFRndZ>; // Floating point rounding (ZMM).
defm : Zn3WriteResXMMPair<WriteFLogic, [Zn3FPVMisc0123], 1, [1], 1>; // Floating point and/or/xor logicals.
defm : Zn3WriteResYMMPair<WriteFLogicY, [Zn3FPVMisc0123], 1, [1], 1>; // Floating point and/or/xor logicals (YMM).
defm : X86WriteResPairUnsupported<WriteFLogicZ>; // Floating point and/or/xor logicals (ZMM).
defm : Zn3WriteResXMMPair<WriteFTest, [Zn3FPFMisc12], 1, [2], 2>; // FIXME: latency not from llvm-exegesis // Floating point TEST instructions.
defm : Zn3WriteResYMMPair<WriteFTestY, [Zn3FPFMisc12], 1, [2], 2>; // FIXME: latency not from llvm-exegesis // Floating point TEST instructions (YMM).
defm : X86WriteResPairUnsupported<WriteFTestZ>; // Floating point TEST instructions (ZMM).
defm : Zn3WriteResXMMPair<WriteFShuffle, [Zn3FPVShuf01], 1, [1], 1>; // Floating point vector shuffles.
defm : Zn3WriteResYMMPair<WriteFShuffleY, [Zn3FPVShuf01], 1, [1], 1>; // Floating point vector shuffles (YMM).
defm : X86WriteResPairUnsupported<WriteFShuffleZ>; // Floating point vector shuffles (ZMM).
defm : Zn3WriteResXMMPair<WriteFVarShuffle, [Zn3FPVShuf01], 3, [1], 1>; // Floating point vector variable shuffles.
defm : Zn3WriteResYMMPair<WriteFVarShuffleY, [Zn3FPVShuf01], 3, [1], 1>; // Floating point vector variable shuffles (YMM).
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>; // Floating point vector variable shuffles (ZMM).
defm : Zn3WriteResXMMPair<WriteFBlend, [Zn3FPFMul01], 1, [1], 1>; // Floating point vector blends.
defm : Zn3WriteResYMMPair<WriteFBlendY, [Zn3FPFMul01], 1, [1], 1>; // Floating point vector blends (YMM).
defm : X86WriteResPairUnsupported<WriteFBlendZ>; // Floating point vector blends (ZMM).
defm : Zn3WriteResXMMPair<WriteFVarBlend, [Zn3FPFMul01], 1, [1], 1>; // Fp vector variable blends.
defm : Zn3WriteResYMMPair<WriteFVarBlendY, [Zn3FPFMul01], 1, [1], 1>; // Fp vector variable blends (YMM).
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>; // Fp vector variable blends (ZMM).

// Horizontal Add/Sub (float and integer)
defm : Zn3WriteResXMMPair<WriteFHAdd, [Zn3FPFAdd0], 6, [2], 4>;
defm : Zn3WriteResYMMPair<WriteFHAddY, [Zn3FPFAdd0], 6, [2], 3, /*LoadUOps=*/1>;
defm : X86WriteResPairUnsupported<WriteFHAddZ>;
defm : Zn3WriteResXMMPair<WritePHAdd, [Zn3FPVAdd0], 2, [2], 3, /*LoadUOps=*/1>;
defm : Zn3WriteResXMMPair<WritePHAddX, [Zn3FPVAdd0], 2, [2], 4>;
defm : Zn3WriteResYMMPair<WritePHAddY, [Zn3FPVAdd0], 2, [2], 3, /*LoadUOps=*/1>;
defm : X86WriteResPairUnsupported<WritePHAddZ>;

// Vector integer operations.
defm : Zn3WriteResXMM<WriteVecLoad, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecLoadX, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResYMM<WriteVecLoadY, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecLoadNT, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResYMM<WriteVecLoadNTY, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecMaskedLoad, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResYMM<WriteVecMaskedLoadY, [Zn3FPLd01, Zn3Load], !add(Znver3Model.VecLoadLatency, 1), [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecStore, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecStoreX, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;

def Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr : SchedWriteRes<[Zn3FPFMisc0]> {
  let Latency = 4;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr], (instrs VEXTRACTF128rr, VEXTRACTI128rr)>;

def Zn3WriteVEXTRACTI128mr : SchedWriteRes<[Zn3FPFMisc0, Zn3FPSt, Zn3Store]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr.NumMicroOps, 1);
}
def : InstRW<[Zn3WriteVEXTRACTI128mr], (instrs VEXTRACTI128mr, VEXTRACTF128mr)>;

def Zn3WriteVINSERTF128rmr : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPFMisc0]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn3WriteVEXTRACTF128rr_VEXTRACTI128rr.NumMicroOps, 0);
}
def : InstRW<[Zn3WriteVINSERTF128rmr], (instrs VINSERTF128rm)>;

defm : Zn3WriteResYMM<WriteVecStoreY, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecStoreNT, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResYMM<WriteVecStoreNTY, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [1, 1], 1>;
defm : Zn3WriteResXMM<WriteVecMaskedStore32, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [6, 1], 18>;
defm : Zn3WriteResXMM<WriteVecMaskedStore64, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [4, 1], 10>;
defm : Zn3WriteResYMM<WriteVecMaskedStore32Y, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [12, 1], 42>;
defm : Zn3WriteResYMM<WriteVecMaskedStore64Y, [Zn3FPSt, Zn3Store], Znver3Model.StoreLatency, [6, 1], 18>;

defm : Zn3WriteResXMM<WriteVecMoveToGpr, [Zn3FPLd01], 1, [2], 1>;
defm : Zn3WriteResXMM<WriteVecMoveFromGpr, [Zn3FPLd01], 1, [2], 1>;

def Zn3WriteMOVMMX : SchedWriteRes<[Zn3FPLd01, Zn3FPFMisc0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteMOVMMX], (instrs MMX_MOVQ2FR64rr, MMX_MOVQ2DQrr)>;

def Zn3WriteMOVMMXSlow : SchedWriteRes<[Zn3FPLd01, Zn3FPFMisc0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 4];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteMOVMMXSlow], (instrs MMX_MOVD64rr, MMX_MOVD64to64rr)>;

defm : Zn3WriteResXMMPair<WriteVecALU, [Zn3FPVAdd0123], 1, [1], 1>;  // Vector integer ALU op, no logicals.

def Zn3WriteEXTRQ_INSERTQ : SchedWriteRes<[Zn3FPVShuf01, Zn3FPLd01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteEXTRQ_INSERTQ], (instrs EXTRQ, INSERTQ)>;

def Zn3WriteEXTRQI_INSERTQI : SchedWriteRes<[Zn3FPVShuf01, Zn3FPLd01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1, 1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteEXTRQI_INSERTQI], (instrs EXTRQI, INSERTQI)>;

defm : Zn3WriteResXMMPair<WriteVecALUX, [Zn3FPVAdd0123], 1, [1], 1>; // Vector integer ALU op, no logicals (XMM).

def Zn3WriteVecALUXSlow : SchedWriteRes<[Zn3FPVAdd01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVecALUXSlow], (instrs PABSBrr, PABSDrr, PABSWrr,
                                            PADDSBrr, PADDSWrr, PADDUSBrr, PADDUSWrr,
                                            PAVGBrr, PAVGWrr,
                                            PSIGNBrr, PSIGNDrr, PSIGNWrr,
                                            VPABSBrr, VPABSDrr, VPABSWrr,
                                            VPADDSBrr, VPADDSWrr, VPADDUSBrr, VPADDUSWrr,
                                            VPAVGBrr, VPAVGWrr,
                                            VPCMPEQQrr,
                                            VPSIGNBrr, VPSIGNDrr, VPSIGNWrr,
                                            PSUBSBrr, PSUBSWrr, PSUBUSBrr, PSUBUSWrr, VPSUBSBrr, VPSUBSWrr, VPSUBUSBrr, VPSUBUSWrr)>;

def Zn3WriteVecALUXMMX : SchedWriteRes<[Zn3FPVAdd01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVecALUXMMX], (instrs MMX_PABSBrr, MMX_PABSDrr, MMX_PABSWrr,
                                           MMX_PSIGNBrr, MMX_PSIGNDrr, MMX_PSIGNWrr,
                                           MMX_PADDSBrr, MMX_PADDSWrr, MMX_PADDUSBrr, MMX_PADDUSWrr,
                                           MMX_PAVGBrr, MMX_PAVGWrr,
                                           MMX_PSUBSBrr, MMX_PSUBSWrr, MMX_PSUBUSBrr, MMX_PSUBUSWrr)>;

defm : Zn3WriteResYMMPair<WriteVecALUY, [Zn3FPVAdd0123], 1, [1], 1>; // Vector integer ALU op, no logicals (YMM).

def Zn3WriteVecALUYSlow : SchedWriteRes<[Zn3FPVAdd01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVecALUYSlow], (instrs VPABSBYrr, VPABSDYrr, VPABSWYrr,
                                            VPADDSBYrr, VPADDSWYrr, VPADDUSBYrr, VPADDUSWYrr,
                                            VPSUBSBYrr, VPSUBSWYrr, VPSUBUSBYrr, VPSUBUSWYrr,
                                            VPAVGBYrr, VPAVGWYrr,
                                            VPCMPEQQYrr,
                                            VPSIGNBYrr, VPSIGNDYrr, VPSIGNWYrr)>;

defm : X86WriteResPairUnsupported<WriteVecALUZ>; // Vector integer ALU op, no logicals (ZMM).
defm : Zn3WriteResXMMPair<WriteVecLogic, [Zn3FPVMisc0123], 1, [1], 1>;  // Vector integer and/or/xor logicals.
defm : Zn3WriteResXMMPair<WriteVecLogicX, [Zn3FPVMisc0123], 1, [1], 1>; // Vector integer and/or/xor logicals (XMM).
defm : Zn3WriteResYMMPair<WriteVecLogicY, [Zn3FPVMisc0123], 1, [1], 1>; // Vector integer and/or/xor logicals (YMM).
defm : X86WriteResPairUnsupported<WriteVecLogicZ>; // Vector integer and/or/xor logicals (ZMM).
defm : Zn3WriteResXMMPair<WriteVecTest, [Zn3FPVAdd12, Zn3FPSt], 1, [1, 1], 2>;  // FIXME: latency not from llvm-exegesis // Vector integer TEST instructions.
defm : Zn3WriteResYMMPair<WriteVecTestY, [Zn3FPVAdd12, Zn3FPSt], 1, [1, 1], 2>; // FIXME: latency not from llvm-exegesis  // Vector integer TEST instructions (YMM).
defm : X86WriteResPairUnsupported<WriteVecTestZ>;  // Vector integer TEST instructions (ZMM).
defm : Zn3WriteResXMMPair<WriteVecShift, [Zn3FPVShift01], 1, [1], 1>;  // Vector integer shifts (default).
defm : Zn3WriteResXMMPair<WriteVecShiftX, [Zn3FPVShift01], 1, [1], 1>; // Vector integer shifts (XMM).
defm : Zn3WriteResYMMPair<WriteVecShiftY, [Zn3FPVShift01], 1, [1], 1>; // Vector integer shifts (YMM).
defm : X86WriteResPairUnsupported<WriteVecShiftZ>; // Vector integer shifts (ZMM).
defm : Zn3WriteResXMMPair<WriteVecShiftImm, [Zn3FPVShift01], 1, [1], 1>;  // Vector integer immediate shifts (default).
defm : Zn3WriteResXMMPair<WriteVecShiftImmX, [Zn3FPVShift01], 1, [1], 1>; // Vector integer immediate shifts (XMM).
defm : Zn3WriteResYMMPair<WriteVecShiftImmY, [Zn3FPVShift01], 1, [1], 1>; // Vector integer immediate shifts (YMM).
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>; // Vector integer immediate shifts (ZMM).
defm : Zn3WriteResXMMPair<WriteVecIMul, [Zn3FPVMul01], 3, [1], 1>;  // Vector integer multiply (default).
defm : Zn3WriteResXMMPair<WriteVecIMulX, [Zn3FPVMul01], 3, [1], 1>; // Vector integer multiply (XMM).
defm : Zn3WriteResYMMPair<WriteVecIMulY, [Zn3FPVMul01], 3, [1], 1>; // Vector integer multiply (YMM).
defm : X86WriteResPairUnsupported<WriteVecIMulZ>; // Vector integer multiply (ZMM).
defm : Zn3WriteResXMMPair<WritePMULLD, [Zn3FPVMul01], 3, [1], 1>; // Vector PMULLD.
defm : Zn3WriteResYMMPair<WritePMULLDY, [Zn3FPVMul01], 3, [1], 1>; // Vector PMULLD (YMM).
defm : X86WriteResPairUnsupported<WritePMULLDZ>; // Vector PMULLD (ZMM).
defm : Zn3WriteResXMMPair<WriteShuffle, [Zn3FPVShuf01], 1, [1], 1>;  // Vector shuffles.
defm : Zn3WriteResXMMPair<WriteShuffleX, [Zn3FPVShuf01], 1, [1], 1>; // Vector shuffles (XMM).
defm : Zn3WriteResYMMPair<WriteShuffleY, [Zn3FPVShuf01], 1, [1], 1>; // Vector shuffles (YMM).
defm : X86WriteResPairUnsupported<WriteShuffleZ>; // Vector shuffles (ZMM).
defm : Zn3WriteResXMMPair<WriteVarShuffle, [Zn3FPVShuf01], 1, [1], 1>;  // Vector variable shuffles.
defm : Zn3WriteResXMMPair<WriteVarShuffleX, [Zn3FPVShuf01], 1, [1], 1>; // Vector variable shuffles (XMM).
defm : Zn3WriteResYMMPair<WriteVarShuffleY, [Zn3FPVShuf01], 1, [1], 1>; // Vector variable shuffles (YMM).
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>; // Vector variable shuffles (ZMM).
defm : Zn3WriteResXMMPair<WriteBlend, [Zn3FPVMisc0123], 1, [1], 1>; // Vector blends.
defm : Zn3WriteResYMMPair<WriteBlendY, [Zn3FPVMisc0123], 1, [1], 1>; // Vector blends (YMM).
defm : X86WriteResPairUnsupported<WriteBlendZ>; // Vector blends (ZMM).
defm : Zn3WriteResXMMPair<WriteVarBlend, [Zn3FPVMul01], 1, [1], 1>; // Vector variable blends.
defm : Zn3WriteResYMMPair<WriteVarBlendY, [Zn3FPVMul01], 1, [1], 1>; // Vector variable blends (YMM).
defm : X86WriteResPairUnsupported<WriteVarBlendZ>; // Vector variable blends (ZMM).
defm : Zn3WriteResXMMPair<WritePSADBW, [Zn3FPVAdd0123], 3, [2], 1>;  // Vector PSADBW.
defm : Zn3WriteResXMMPair<WritePSADBWX, [Zn3FPVAdd0123], 3, [2], 1>; // Vector PSADBW (XMM).
defm : Zn3WriteResYMMPair<WritePSADBWY, [Zn3FPVAdd0123], 3, [2], 1>; // Vector PSADBW (YMM).
defm : X86WriteResPairUnsupported<WritePSADBWZ>; // Vector PSADBW (ZMM).
defm : Zn3WriteResXMMPair<WriteMPSAD, [Zn3FPVAdd0123], 4, [8], 4, /*LoadUOps=*/2>; // Vector MPSAD.
defm : Zn3WriteResYMMPair<WriteMPSADY, [Zn3FPVAdd0123], 4, [8], 3, /*LoadUOps=*/1>; // Vector MPSAD (YMM).
defm : X86WriteResPairUnsupported<WriteMPSADZ>; // Vector MPSAD (ZMM).
defm : Zn3WriteResXMMPair<WritePHMINPOS, [Zn3FPVAdd01], 3, [1], 1>;  // Vector PHMINPOS.

// Vector insert/extract operations.
defm : Zn3WriteResXMMPair<WriteVecInsert, [Zn3FPLd01], 1, [2], 2, /*LoadUOps=*/-1>; // Insert gpr to vector element.
defm : Zn3WriteResXMM<WriteVecExtract, [Zn3FPLd01], 1, [2], 2>; // Extract vector element to gpr.
defm : Zn3WriteResXMM<WriteVecExtractSt, [Zn3FPSt, Zn3Store], !add(1, Znver3Model.StoreLatency), [1, 1], 2>; // Extract vector element and store.

// MOVMSK operations.
defm : Zn3WriteResXMM<WriteFMOVMSK, [Zn3FPVMisc2], 1, [1], 1>;
defm : Zn3WriteResXMM<WriteVecMOVMSK, [Zn3FPVMisc2], 1, [1], 1>;
defm : Zn3WriteResYMM<WriteVecMOVMSKY, [Zn3FPVMisc2], 1, [1], 1>;
defm : Zn3WriteResXMM<WriteMMXMOVMSK, [Zn3FPVMisc2], 1, [1], 1>;

// Conversion between integer and float.
defm : Zn3WriteResXMMPair<WriteCvtSD2I, [Zn3FPFCvt01], 2, [2], 2>;  // Double -> Integer.
defm : Zn3WriteResXMMPair<WriteCvtPD2I, [Zn3FPFCvt01], 3, [1], 1>; // Double -> Integer (XMM).
defm : Zn3WriteResYMMPair<WriteCvtPD2IY, [Zn3FPFCvt01], 6, [2], 2>; // Double -> Integer (YMM).
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>; // Double -> Integer (ZMM).

def Zn3WriteCvtPD2IMMX : SchedWriteRes<[Zn3FPFCvt01]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteCvtPD2IMMX], (instrs MMX_CVTPD2PIrm, MMX_CVTTPD2PIrm, MMX_CVTPD2PIrr, MMX_CVTTPD2PIrr)>;

defm : Zn3WriteResXMMPair<WriteCvtSS2I, [Zn3FPFCvt01], 2, [2], 2>;  // Float -> Integer.

defm : Zn3WriteResXMMPair<WriteCvtPS2I, [Zn3FPFCvt01], 3, [1], 1>; // Float -> Integer (XMM).
defm : Zn3WriteResYMMPair<WriteCvtPS2IY, [Zn3FPFCvt01], 3, [1], 1>; // Float -> Integer (YMM).
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>; // Float -> Integer (ZMM).

defm : Zn3WriteResXMMPair<WriteCvtI2SD, [Zn3FPFCvt01], 3, [2], 2, /*LoadUOps=*/-1>;  // Integer -> Double.
defm : Zn3WriteResXMMPair<WriteCvtI2PD, [Zn3FPFCvt01], 3, [1], 1>; // Integer -> Double (XMM).
defm : Zn3WriteResYMMPair<WriteCvtI2PDY, [Zn3FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>; // Integer -> Double (YMM).
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>; // Integer -> Double (ZMM).

def Zn3WriteCvtI2PDMMX : SchedWriteRes<[Zn3FPFCvt01]> {
  let Latency = 2;
  let ReleaseAtCycles = [6];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteCvtI2PDMMX], (instrs MMX_CVTPI2PDrm, MMX_CVTPI2PDrr)>;

defm : Zn3WriteResXMMPair<WriteCvtI2SS, [Zn3FPFCvt01], 3, [2], 2, /*LoadUOps=*/-1>;  // Integer -> Float.
defm : Zn3WriteResXMMPair<WriteCvtI2PS, [Zn3FPFCvt01], 3, [1], 1>; // Integer -> Float (XMM).
defm : Zn3WriteResYMMPair<WriteCvtI2PSY, [Zn3FPFCvt01], 3, [1], 1>; // Integer -> Float (YMM).
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>; // Integer -> Float (ZMM).

def Zn3WriteCvtI2PSMMX : SchedWriteRes<[Zn3FPFCvt01]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteCvtI2PSMMX], (instrs MMX_CVTPI2PSrr)>;

defm : Zn3WriteResXMMPair<WriteCvtSS2SD, [Zn3FPFCvt01], 3, [1], 1>;  // Float -> Double size conversion.
defm : Zn3WriteResXMMPair<WriteCvtPS2PD, [Zn3FPFCvt01], 3, [1], 1>; // Float -> Double size conversion (XMM).
defm : Zn3WriteResYMMPair<WriteCvtPS2PDY, [Zn3FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>; // Float -> Double size conversion (YMM).
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>; // Float -> Double size conversion (ZMM).

defm : Zn3WriteResXMMPair<WriteCvtSD2SS, [Zn3FPFCvt01], 3, [1], 1>;  // Double -> Float size conversion.
defm : Zn3WriteResXMMPair<WriteCvtPD2PS, [Zn3FPFCvt01], 3, [1], 1>; // Double -> Float size conversion (XMM).
defm : Zn3WriteResYMMPair<WriteCvtPD2PSY, [Zn3FPFCvt01], 6, [2], 2>; // Double -> Float size conversion (YMM).
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>; // Double -> Float size conversion (ZMM).

defm : Zn3WriteResXMMPair<WriteCvtPH2PS, [Zn3FPFCvt01], 3, [1], 1>; // Half -> Float size conversion.
defm : Zn3WriteResYMMPair<WriteCvtPH2PSY, [Zn3FPFCvt01], 4, [2], 2, /*LoadUOps=*/-1>; // Half -> Float size conversion (YMM).
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>; // Half -> Float size conversion (ZMM).

defm : Zn3WriteResXMM<WriteCvtPS2PH, [Zn3FPFCvt01], 3, [2], 1>; // Float -> Half size conversion.
defm : Zn3WriteResYMM<WriteCvtPS2PHY, [Zn3FPFCvt01], 6, [2], 2>; // Float -> Half size conversion (YMM).
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>; // Float -> Half size conversion (ZMM).
defm : Zn3WriteResXMM<WriteCvtPS2PHSt, [Zn3FPFCvt01, Zn3FPSt, Zn3Store], !add(3, Znver3Model.StoreLatency), [1, 1, 1], 2>; // Float -> Half + store size conversion.
defm : Zn3WriteResYMM<WriteCvtPS2PHYSt, [Zn3FPFCvt01, Zn3FPSt, Zn3Store], !add(6, Znver3Model.StoreLatency), [2, 1, 1], 3>; // Float -> Half + store size conversion (YMM).
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>; // Float -> Half + store size conversion (ZMM).

// CRC32 instruction.
defm : Zn3WriteResIntPair<WriteCRC32, [Zn3ALU1], 3, [1], 1>;

def Zn3WriteSHA1MSG1rr : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteSHA1MSG1rr], (instrs SHA1MSG1rr)>;

def Zn3WriteSHA1MSG1rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteSHA1MSG1rr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn3WriteSHA1MSG1rr.NumMicroOps, 0);
}
def : InstRW<[Zn3WriteSHA1MSG1rm], (instrs SHA1MSG1rm)>;

def Zn3WriteSHA1MSG2rr_SHA1NEXTErr : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteSHA1MSG2rr_SHA1NEXTErr], (instrs SHA1MSG2rr, SHA1NEXTErr)>;

def Zn3Writerm_SHA1MSG2rm_SHA1NEXTErm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteSHA1MSG2rr_SHA1NEXTErr.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn3WriteSHA1MSG2rr_SHA1NEXTErr.NumMicroOps, 0);
}
def : InstRW<[Zn3Writerm_SHA1MSG2rm_SHA1NEXTErm], (instrs SHA1MSG2rm, SHA1NEXTErm)>;

def Zn3WriteSHA256MSG1rr : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [3];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteSHA256MSG1rr], (instrs SHA256MSG1rr)>;

def Zn3Writerm_SHA256MSG1rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteSHA256MSG1rr.Latency);
  let ReleaseAtCycles = [1, 1, 3];
  let NumMicroOps = !add(Zn3WriteSHA256MSG1rr.NumMicroOps, 0);
}
def : InstRW<[Zn3Writerm_SHA256MSG1rm], (instrs SHA256MSG1rm)>;

def Zn3WriteSHA256MSG2rr : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 3;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 4;
}
def : InstRW<[Zn3WriteSHA256MSG2rr], (instrs SHA256MSG2rr)>;

def Zn3WriteSHA256MSG2rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPU0123]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteSHA256MSG2rr.Latency);
  let ReleaseAtCycles = [1, 1, 8];
  let NumMicroOps = !add(Zn3WriteSHA256MSG2rr.NumMicroOps, 1);
}
def : InstRW<[Zn3WriteSHA256MSG2rm], (instrs SHA256MSG2rm)>;

def Zn3WriteSHA1RNDS4rri : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 6;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteSHA1RNDS4rri], (instrs SHA1RNDS4rri)>;

def Zn3WriteSHA256RNDS2rr : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 4;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteSHA256RNDS2rr], (instrs SHA256RNDS2rr)>;

// Strings instructions.
// Packed Compare Implicit Length Strings, Return Mask
defm : Zn3WriteResXMMPair<WritePCmpIStrM, [Zn3FPVAdd0123], 6, [8], 3, /*LoadUOps=*/1>;
// Packed Compare Explicit Length Strings, Return Mask
defm : Zn3WriteResXMMPair<WritePCmpEStrM, [Zn3FPVAdd0123], 6, [12], 7, /*LoadUOps=*/5>;
// Packed Compare Implicit Length Strings, Return Index
defm : Zn3WriteResXMMPair<WritePCmpIStrI, [Zn3FPVAdd0123], 2, [8], 4>;
// Packed Compare Explicit Length Strings, Return Index
defm : Zn3WriteResXMMPair<WritePCmpEStrI, [Zn3FPVAdd0123], 6, [12], 8, /*LoadUOps=*/4>;

// AES instructions.
defm : Zn3WriteResXMMPair<WriteAESDecEnc, [Zn3FPAES01], 4, [1], 1>; // Decryption, encryption.
defm : Zn3WriteResXMMPair<WriteAESIMC, [Zn3FPAES01], 4, [1], 1>; // InvMixColumn.
defm : Zn3WriteResXMMPair<WriteAESKeyGen, [Zn3FPAES01], 4, [1], 1>; // Key Generation.

// Carry-less multiplication instructions.
defm : Zn3WriteResXMMPair<WriteCLMul, [Zn3FPCLM01], 4, [4], 4>;

// EMMS/FEMMS
defm : Zn3WriteResInt<WriteEMMS, [Zn3ALU0123], 2, [1], 1>; // FIXME: latency not from llvm-exegesis

// Load/store MXCSR
defm : Zn3WriteResInt<WriteLDMXCSR, [Zn3AGU012, Zn3Load, Zn3ALU0123], !add(Znver3Model.LoadLatency, 1), [1, 1, 6], 1>; // FIXME: latency not from llvm-exegesis
defm : Zn3WriteResInt<WriteSTMXCSR, [Zn3ALU0123, Zn3AGU012, Zn3Store], !add(1, Znver3Model.StoreLatency), [60, 1, 1], 2>; // FIXME: latency not from llvm-exegesis

// Catch-all for expensive system instructions.
defm : Zn3WriteResInt<WriteSystem, [Zn3ALU0123], 100, [100], 100>;

def Zn3WriteVZEROUPPER : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 0; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVZEROUPPER], (instrs VZEROUPPER)>;

def Zn3WriteVZEROALL : SchedWriteRes<[Zn3FPU0123]> {
  let Latency = 10; // FIXME: not from llvm-exegesis
  let ReleaseAtCycles = [24];
  let NumMicroOps = 18;
}
def : InstRW<[Zn3WriteVZEROALL], (instrs VZEROALL)>;

// AVX2.
defm : Zn3WriteResYMMPair<WriteFShuffle256, [Zn3FPVShuf], 2, [1], 1, /*LoadUOps=*/2>; // Fp 256-bit width vector shuffles.
defm : Zn3WriteResYMMPair<WriteFVarShuffle256, [Zn3FPVShuf], 7, [1], 2, /*LoadUOps=*/1>; // Fp 256-bit width variable shuffles.
defm : Zn3WriteResYMMPair<WriteShuffle256, [Zn3FPVShuf], 2, [1], 1>; // 256-bit width vector shuffles.

def Zn3WriteVPERM2I128rr_VPERM2F128rr : SchedWriteRes<[Zn3FPVShuf]> {
  let Latency = 3;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteVPERM2I128rr_VPERM2F128rr], (instrs VPERM2I128rr, VPERM2F128rr)>;

def Zn3WriteVPERM2F128rm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPVShuf]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteVPERM2I128rr_VPERM2F128rr.Latency);
  let ReleaseAtCycles = [1, 1, 1];
  let NumMicroOps = !add(Zn3WriteVPERM2I128rr_VPERM2F128rr.NumMicroOps, 0);
}
def : InstRW<[Zn3WriteVPERM2F128rm], (instrs VPERM2F128rm)>;

def Zn3WriteVPERMPSYrm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPVShuf]> {
  let Latency = !add(Znver3Model.LoadLatency, 7);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 3;
}
def : InstRW<[Zn3WriteVPERMPSYrm], (instrs VPERMPSYrm)>;

def Zn3WriteVPERMYri : SchedWriteRes<[Zn3FPVShuf]> {
  let Latency = 6;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteVPERMYri], (instrs VPERMPDYri, VPERMQYri)>;

def Zn3WriteVPERMPDYmi : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPVShuf]> {
  let Latency = !add(Znver3Model.LoadLatency, Zn3WriteVPERMYri.Latency);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = !add(Zn3WriteVPERMYri.NumMicroOps, 1);
}
def : InstRW<[Zn3WriteVPERMPDYmi], (instrs VPERMPDYmi)>;

def Zn3WriteVPERMDYm : SchedWriteRes<[Zn3AGU012, Zn3Load, Zn3FPVShuf]> {
  let Latency = !add(Znver3Model.LoadLatency, 5);
  let ReleaseAtCycles = [1, 1, 2];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteVPERMDYm], (instrs VPERMQYmi, VPERMDYrm)>;

defm : Zn3WriteResYMMPair<WriteVPMOV256, [Zn3FPVShuf01], 4, [3], 2, /*LoadUOps=*/-1>; // 256-bit width packed vector width-changing move.
defm : Zn3WriteResYMMPair<WriteVarShuffle256, [Zn3FPVShuf], 5, [1], 2, /*LoadUOps=*/1>; // 256-bit width vector variable shuffles.
defm : Zn3WriteResXMMPair<WriteVarVecShift, [Zn3FPVShift01], 1, [1], 1>; // Variable vector shifts.
defm : Zn3WriteResYMMPair<WriteVarVecShiftY, [Zn3FPVShift01], 1, [1], 1>; // Variable vector shifts (YMM).
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>; // Variable vector shifts (ZMM).

// Old microcoded instructions that nobody use.
defm : Zn3WriteResInt<WriteMicrocoded, [Zn3ALU0123], 100, [100], 100>;

// Fence instructions.
defm : Zn3WriteResInt<WriteFence, [Zn3ALU0123], 1, [100], 1>;

def Zn3WriteLFENCE : SchedWriteRes<[Zn3LSU]> {
  let Latency = 1;
  let ReleaseAtCycles = [30];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteLFENCE], (instrs LFENCE)>;

def Zn3WriteSFENCE : SchedWriteRes<[Zn3LSU]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteSFENCE], (instrs SFENCE)>;

// Nop, not very useful expect it provides a model for nops!
defm : Zn3WriteResInt<WriteNop, [Zn3ALU0123], 0, [1], 1>; // FIXME: latency not from llvm-exegesis


///////////////////////////////////////////////////////////////////////////////
// Zero Cycle Move
///////////////////////////////////////////////////////////////////////////////

def Zn3WriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
  let ReleaseAtCycles = [];
  let NumMicroOps = 1;
}
def : InstRW<[Zn3WriteZeroLatency], (instrs MOV32rr, MOV32rr_REV,
                                               MOV64rr, MOV64rr_REV,
                                               MOVSX32rr32)>;

def Zn3WriteSwapRenameable : SchedWriteRes<[]> {
  let Latency = 0;
  let ReleaseAtCycles = [];
  let NumMicroOps = 2;
}
def : InstRW<[Zn3WriteSwapRenameable], (instrs XCHG32rr, XCHG32ar,
                                               XCHG64rr, XCHG64ar)>;

defm : Zn3WriteResInt<WriteXCHG, [Zn3ALU0123], 0, [8], 2>;        // Compare+Exchange - TODO RMW support.

defm : Zn3WriteResXMM<WriteFMove, [Zn3FPVMisc0123], 1, [1], 1>; // Empty sched class
defm : Zn3WriteResXMM<WriteFMoveX, [], 0, [], 1>;
defm : Zn3WriteResYMM<WriteFMoveY, [], 0, [], 1>;
defm : X86WriteResUnsupported<WriteFMoveZ>;

defm : Zn3WriteResXMM<WriteVecMove, [Zn3FPFMisc0123], 1, [1], 1>; // MMX
defm : Zn3WriteResXMM<WriteVecMoveX, [], 0, [], 1>;
defm : Zn3WriteResYMM<WriteVecMoveY, [], 0, [], 1>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;

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

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

def Zn3WriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[Zn3WriteZeroIdiom], (instrs XOR32rr, XOR32rr_REV,
                                          XOR64rr, XOR64rr_REV,
                                          SUB32rr, SUB32rr_REV,
                                          SUB64rr, SUB64rr_REV)>;

def Zn3WriteZeroIdiomEFLAGS : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<CheckSameRegOperand<0, 1>>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                                 [WriteALU]>
]>;
def : InstRW<[Zn3WriteZeroIdiomEFLAGS], (instrs CMP8rr,  CMP8rr_REV,
                                                CMP16rr, CMP16rr_REV,
                                                CMP32rr, CMP32rr_REV,
                                                CMP64rr, CMP64rr_REV)>;

def Zn3WriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
// NOTE: XORPSrr, XORPDrr are not zero-cycle!
def : InstRW<[Zn3WriteFZeroIdiom], (instrs VXORPSrr, VXORPDrr,
                                           VANDNPSrr, VANDNPDrr)>;

def Zn3WriteFZeroIdiomY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogicY]>
]>;
def : InstRW<[Zn3WriteFZeroIdiomY], (instrs VXORPSYrr, VXORPDYrr,
                                            VANDNPSYrr, VANDNPDYrr)>;

def Zn3WriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
// NOTE: PXORrr,PANDNrr are not zero-cycle!
def : InstRW<[Zn3WriteVZeroIdiomLogicX], (instrs VPXORrr, VPANDNrr)>;

def Zn3WriteVZeroIdiomLogicY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicY]>
]>;
def : InstRW<[Zn3WriteVZeroIdiomLogicY], (instrs VPXORYrr, VPANDNYrr)>;

def Zn3WriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
// NOTE: PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
//       PCMPGTBrr, PCMPGTWrr, PCMPGTDrr, PCMPGTQrr are not zero-cycle!
def : InstRW<[Zn3WriteVZeroIdiomALUX],
             (instrs VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
                     VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr)>;

def Zn3WriteVZeroIdiomALUY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [Zn3WriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUY]>
]>;
def : InstRW<[Zn3WriteVZeroIdiomALUY],
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

  // MMX
  DepBreakingClass<[
    MMX_PCMPEQBrr, MMX_PCMPEQWrr, MMX_PCMPEQDrr
  ], ZeroIdiomPredicate>,

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
