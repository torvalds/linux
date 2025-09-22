//=- X86ScheduleZnver2.td - X86 Znver2 Scheduling -------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Znver2 to support instruction
// scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def Znver2Model : SchedMachineModel {
  // Zen can decode 4 instructions per cycle.
  let IssueWidth = 4;
  // Based on the reorder buffer we define MicroOpBufferSize
  let MicroOpBufferSize = 224;
  let LoadLatency = 4;
  let MispredictPenalty = 17;
  let HighLatency = 25;
  let PostRAScheduler = 1;

  // FIXME: This variable is required for incomplete model.
  // We haven't catered all instructions.
  // So, we reset the value of this variable so as to
  // say that the model is incomplete.
  let CompleteModel = 0;
}

let SchedModel = Znver2Model in {

// Zen can issue micro-ops to 10 different units in one cycle.
// These are
//  * Four integer ALU units (ZALU0, ZALU1, ZALU2, ZALU3)
//  * Three AGU units (ZAGU0, ZAGU1, ZAGU2)
//  * Four FPU units (ZFPU0, ZFPU1, ZFPU2, ZFPU3)
// AGUs feed load store queues @two loads and 1 store per cycle.

// Four ALU units are defined below
def Zn2ALU0 : ProcResource<1>;
def Zn2ALU1 : ProcResource<1>;
def Zn2ALU2 : ProcResource<1>;
def Zn2ALU3 : ProcResource<1>;

// Three AGU units are defined below
def Zn2AGU0 : ProcResource<1>;
def Zn2AGU1 : ProcResource<1>;
def Zn2AGU2 : ProcResource<1>;

// Four FPU units are defined below
def Zn2FPU0 : ProcResource<1>;
def Zn2FPU1 : ProcResource<1>;
def Zn2FPU2 : ProcResource<1>;
def Zn2FPU3 : ProcResource<1>;

// FPU grouping
def Zn2FPU013  : ProcResGroup<[Zn2FPU0, Zn2FPU1, Zn2FPU3]>;
def Zn2FPU01   : ProcResGroup<[Zn2FPU0, Zn2FPU1]>;
def Zn2FPU12   : ProcResGroup<[Zn2FPU1, Zn2FPU2]>;
def Zn2FPU13   : ProcResGroup<[Zn2FPU1, Zn2FPU3]>;
def Zn2FPU23   : ProcResGroup<[Zn2FPU2, Zn2FPU3]>;
def Zn2FPU02   : ProcResGroup<[Zn2FPU0, Zn2FPU2]>;
def Zn2FPU03   : ProcResGroup<[Zn2FPU0, Zn2FPU3]>;

// Below are the grouping of the units.
// Micro-ops to be issued to multiple units are tackled this way.

// ALU grouping
// Zn2ALU03 - 0,3 grouping
def Zn2ALU03: ProcResGroup<[Zn2ALU0, Zn2ALU3]>;

// 64 Entry (16x4 entries) Int Scheduler
def Zn2ALU : ProcResGroup<[Zn2ALU0, Zn2ALU1, Zn2ALU2, Zn2ALU3]> {
  let BufferSize=64;
}

// 28 Entry (14x2) AGU group. AGUs can't be used for all ALU operations
// but are relevant for some instructions
def Zn2AGU : ProcResGroup<[Zn2AGU0, Zn2AGU1, Zn2AGU2]> {
  let BufferSize=28;
}

// Integer Multiplication issued on ALU1.
def Zn2Multiplier : ProcResource<1>;

// Integer division issued on ALU2.
def Zn2Divider : ProcResource<1>;

// 4 Cycles load-to use Latency is captured
def : ReadAdvance<ReadAfterLd, 4>;

// 7 Cycles vector load-to use Latency is captured
def : ReadAdvance<ReadAfterVecLd, 7>;
def : ReadAdvance<ReadAfterVecXLd, 7>;
def : ReadAdvance<ReadAfterVecYLd, 7>;

def : ReadAdvance<ReadInt2Fpu, 0>;

// The Integer PRF for Zen is 168 entries, and it holds the architectural and
// speculative version of the 64-bit integer registers.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
def Zn2IntegerPRF : RegisterFile<168, [GR64, CCR]>;

// 36 Entry (9x4 entries) floating-point Scheduler
def Zn2FPU     : ProcResGroup<[Zn2FPU0, Zn2FPU1, Zn2FPU2, Zn2FPU3]> {
  let BufferSize=36;
}

// The Zen FP Retire Queue renames SIMD and FP uOps onto a pool of 160 128-bit
// registers. Operations on 256-bit data types are cracked into two COPs.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
def Zn2FpuPRF: RegisterFile<160, [VR64, VR128, VR256], [1, 1, 2]>;

// The unit can track up to 192 macro ops in-flight.
// The retire unit handles in-order commit of up to 8 macro ops per cycle.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
// To be noted, the retire unit is shared between integer and FP ops.
// In SMT mode it is 96 entry per thread. But, we do not use the conservative
// value here because there is currently no way to fully mode the SMT mode,
// so there is no point in trying.
def Zn2RCU : RetireControlUnit<192, 8>;

// (a folded load is an instruction that loads and does some operation)
// Ex: ADDPD xmm,[mem]-> This instruction has two micro-ops
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops.
//      a. load and
//      b. addpd
// This multiclass is for folded loads for integer units.
multiclass Zn2WriteResPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [], int UOps = 1,
                          int LoadLat = 4, int LoadUOps = 1> {
  // Register variant takes 1-cycle on Execution Port.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on Zn2AGU
  // adds LoadLat cycles to the latency (default = 4).
  def : WriteRes<SchedRW.Folded, !listconcat([Zn2AGU], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ReleaseAtCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// This multiclass is for folded loads for floating point units.
multiclass Zn2WriteResFpuPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [], int UOps = 1,
                          int LoadLat = 7, int LoadUOps = 0> {
  // Register variant takes 1-cycle on Execution Port.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on Zn2AGU
  // adds LoadLat cycles to the latency (default = 7).
  def : WriteRes<SchedRW.Folded, !listconcat([Zn2AGU], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ReleaseAtCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// WriteRMW is set for instructions with Memory write
// operation in codegen
def : WriteRes<WriteRMW, [Zn2AGU]>;

def : WriteRes<WriteStore,   [Zn2AGU]>;
def : WriteRes<WriteStoreNT, [Zn2AGU]>;
def : WriteRes<WriteMove,    [Zn2ALU]>;
def : WriteRes<WriteLoad,    [Zn2AGU]> { let Latency = 4; }

// Model the effect of clobbering the read-write mask operand of the GATHER operation.
// Does not cost anything by itself, only has latency, matching that of the WriteLoad,
def : WriteRes<WriteVecMaskedGatherWriteback, []> { let Latency = 8; let NumMicroOps = 0; }

def : WriteRes<WriteZero,  []>;
def : WriteRes<WriteLEA, [Zn2ALU]>;
defm : Zn2WriteResPair<WriteALU,   [Zn2ALU], 1>;
defm : Zn2WriteResPair<WriteADC,   [Zn2ALU], 1>;

defm : Zn2WriteResPair<WriteIMul8,     [Zn2ALU1, Zn2Multiplier], 4>;

defm : X86WriteRes<WriteBSWAP32, [Zn2ALU], 1, [4], 1>;
defm : X86WriteRes<WriteBSWAP64, [Zn2ALU], 1, [4], 1>;
defm : X86WriteRes<WriteCMPXCHG, [Zn2ALU], 3, [1], 1>;
defm : X86WriteRes<WriteCMPXCHGRMW,[Zn2ALU,Zn2AGU], 8, [1,1], 5>;
defm : X86WriteRes<WriteXCHG, [Zn2ALU], 1, [2], 2>;

defm : Zn2WriteResPair<WriteShift,    [Zn2ALU], 1>;
defm : Zn2WriteResPair<WriteShiftCL,  [Zn2ALU], 1>;
defm : Zn2WriteResPair<WriteRotate,   [Zn2ALU], 1>;
defm : Zn2WriteResPair<WriteRotateCL, [Zn2ALU], 1>;

defm : X86WriteRes<WriteSHDrri, [Zn2ALU], 1, [1], 1>;
defm : X86WriteResUnsupported<WriteSHDrrcl>;
defm : X86WriteResUnsupported<WriteSHDmri>;
defm : X86WriteResUnsupported<WriteSHDmrcl>;

defm : Zn2WriteResPair<WriteJump,  [Zn2ALU], 1>;
defm : Zn2WriteResFpuPair<WriteCRC32, [Zn2FPU0], 3>;

defm : Zn2WriteResPair<WriteCMOV,   [Zn2ALU], 1>;
def  : WriteRes<WriteSETCC,  [Zn2ALU]>;
def  : WriteRes<WriteSETCCStore,  [Zn2ALU, Zn2AGU]>;
defm : X86WriteRes<WriteLAHFSAHF, [Zn2ALU], 2, [1], 2>;

defm : X86WriteRes<WriteBitTest,         [Zn2ALU], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [Zn2ALU,Zn2AGU], 5, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [Zn2ALU,Zn2AGU], 5, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [Zn2ALU], 2, [1], 2>;

// Bit counts.
defm : Zn2WriteResPair<WriteBSF, [Zn2ALU], 3, [12], 6, 4, 2>;
defm : Zn2WriteResPair<WriteBSR, [Zn2ALU], 4, [16], 6, 4, 2>;
defm : Zn2WriteResPair<WriteLZCNT,          [Zn2ALU], 1>;
defm : Zn2WriteResPair<WriteTZCNT,          [Zn2ALU], 2, [2], 2, 4, 0>;
defm : Zn2WriteResPair<WritePOPCNT,         [Zn2ALU], 1>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

// BMI1 BEXTR, BMI2 BZHI
defm : Zn2WriteResPair<WriteBEXTR, [Zn2ALU], 1, [1], 1, 4, 1>;
defm : Zn2WriteResPair<WriteBLS,   [Zn2ALU], 2, [2], 2, 4, 1>;
defm : Zn2WriteResPair<WriteBZHI,  [Zn2ALU], 1>;

// IDIV
defm : Zn2WriteResPair<WriteDiv8,   [Zn2ALU2, Zn2Divider], 15, [1,15], 1>;
defm : Zn2WriteResPair<WriteDiv16,  [Zn2ALU2, Zn2Divider], 17, [1,17], 2>;
defm : Zn2WriteResPair<WriteDiv32,  [Zn2ALU2, Zn2Divider], 25, [1,25], 2>;
defm : Zn2WriteResPair<WriteDiv64,  [Zn2ALU2, Zn2Divider], 41, [1,41], 2>;
defm : Zn2WriteResPair<WriteIDiv8,  [Zn2ALU2, Zn2Divider], 15, [1,15], 1>;
defm : Zn2WriteResPair<WriteIDiv16, [Zn2ALU2, Zn2Divider], 17, [1,17], 2>;
defm : Zn2WriteResPair<WriteIDiv32, [Zn2ALU2, Zn2Divider], 25, [1,25], 2>;
defm : Zn2WriteResPair<WriteIDiv64, [Zn2ALU2, Zn2Divider], 41, [1,41], 2>;

// IMULH
def Zn2WriteIMulH : WriteRes<WriteIMulH, [Zn2Multiplier]>{
  let Latency = 3;
  let NumMicroOps = 0;
}
def  : WriteRes<WriteIMulHLd, [Zn2Multiplier]>{
  let Latency = !add(Zn2WriteIMulH.Latency, Znver2Model.LoadLatency);
  let NumMicroOps = Zn2WriteIMulH.NumMicroOps;
}

// Floating point operations
defm : X86WriteRes<WriteFLoad,         [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [Zn2AGU,Zn2FPU01], 8, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedLoadY,  [Zn2AGU,Zn2FPU01], 8, [1,1], 2>;

defm : X86WriteRes<WriteFStore,        [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreX,       [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreY,       [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreNT,      [Zn2AGU,Zn2FPU2], 8, [1,1], 1>;
defm : X86WriteRes<WriteFStoreNTX,     [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreNTY,     [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteFMaskedStore32,  [Zn2AGU,Zn2FPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedStore32Y, [Zn2AGU,Zn2FPU01], 5, [1,2], 2>;
defm : X86WriteRes<WriteFMaskedStore64,  [Zn2AGU,Zn2FPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedStore64Y, [Zn2AGU,Zn2FPU01], 5, [1,2], 2>;

defm : X86WriteRes<WriteFMove,         [Zn2FPU], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [Zn2FPU], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [Zn2FPU], 1, [1], 1>;
defm : X86WriteResUnsupported<WriteFMoveZ>;

defm : Zn2WriteResFpuPair<WriteFAdd,      [Zn2FPU23], 3>;
defm : Zn2WriteResFpuPair<WriteFAddX,     [Zn2FPU23], 3>;
defm : Zn2WriteResFpuPair<WriteFAddY,     [Zn2FPU23], 3>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : Zn2WriteResFpuPair<WriteFAdd64,    [Zn2FPU23], 3>;
defm : Zn2WriteResFpuPair<WriteFAdd64X,   [Zn2FPU23], 3>;
defm : Zn2WriteResFpuPair<WriteFAdd64Y,   [Zn2FPU23], 3>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;
defm : Zn2WriteResFpuPair<WriteFCmp,      [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFCmpX,     [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFCmpY,     [Zn2FPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : Zn2WriteResFpuPair<WriteFCmp64,    [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFCmp64X,   [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFCmp64Y,   [Zn2FPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;
defm : Zn2WriteResFpuPair<WriteFCom,      [Zn2FPU01,Zn2FPU2], 3, [1,1], 2>;
defm : Zn2WriteResFpuPair<WriteFComX,     [Zn2FPU01,Zn2FPU2], 3, [1,1], 2>;
defm : Zn2WriteResFpuPair<WriteFBlend,    [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFBlendY,   [Zn2FPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : Zn2WriteResFpuPair<WriteFVarBlend, [Zn2FPU01], 1>;
defm : Zn2WriteResFpuPair<WriteFVarBlendY,[Zn2FPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;
defm : Zn2WriteResFpuPair<WriteCvtSS2I,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtPS2I,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtPS2IY,  [Zn2FPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : Zn2WriteResFpuPair<WriteCvtSD2I,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtPD2I,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtPD2IY,  [Zn2FPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;
defm : Zn2WriteResFpuPair<WriteCvtI2SS,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtI2PS,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtI2PSY,  [Zn2FPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : Zn2WriteResFpuPair<WriteCvtI2SD,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtI2PD,   [Zn2FPU3],  5>;
defm : Zn2WriteResFpuPair<WriteCvtI2PDY,  [Zn2FPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;
defm : Zn2WriteResFpuPair<WriteFDiv,      [Zn2FPU3], 10, [5]>;
defm : Zn2WriteResFpuPair<WriteFDivX,     [Zn2FPU3], 10, [5]>;
defm : Zn2WriteResFpuPair<WriteFDivY,     [Zn2FPU3], 10, [5]>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : Zn2WriteResFpuPair<WriteFDiv64,    [Zn2FPU3], 13, [6]>;
defm : Zn2WriteResFpuPair<WriteFDiv64X,   [Zn2FPU3], 13, [6]>;
defm : Zn2WriteResFpuPair<WriteFDiv64Y,   [Zn2FPU3], 13, [6]>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;
defm : Zn2WriteResFpuPair<WriteFSign,     [Zn2FPU3],  2>;
defm : Zn2WriteResFpuPair<WriteFRnd,      [Zn2FPU3],  3, [1], 1, 7, 0>;
defm : Zn2WriteResFpuPair<WriteFRndY,     [Zn2FPU3],  3, [1], 1, 7, 0>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : Zn2WriteResFpuPair<WriteFLogic,    [Zn2FPU],   1>;
defm : Zn2WriteResFpuPair<WriteFLogicY,   [Zn2FPU],   1>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : Zn2WriteResFpuPair<WriteFTest,     [Zn2FPU12], 3, [2], 1, 7, 1>;
defm : Zn2WriteResFpuPair<WriteFTestY,    [Zn2FPU12], 3, [2], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : Zn2WriteResFpuPair<WriteFShuffle,  [Zn2FPU12], 1>;
defm : Zn2WriteResFpuPair<WriteFShuffleY, [Zn2FPU12], 1>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : Zn2WriteResFpuPair<WriteFVarShuffle, [Zn2FPU12], 3>;
defm : Zn2WriteResFpuPair<WriteFVarShuffleY,[Zn2FPU12], 3>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : Zn2WriteResFpuPair<WriteFMul,      [Zn2FPU01], 3>;
defm : Zn2WriteResFpuPair<WriteFMulX,     [Zn2FPU01], 3>;
defm : Zn2WriteResFpuPair<WriteFMulY,     [Zn2FPU01], 3>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : Zn2WriteResFpuPair<WriteFMul64,    [Zn2FPU01], 3>;
defm : Zn2WriteResFpuPair<WriteFMul64X,   [Zn2FPU01], 3>;
defm : Zn2WriteResFpuPair<WriteFMul64Y,   [Zn2FPU01], 3>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;
defm : Zn2WriteResFpuPair<WriteFMA,       [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFMAX,      [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFMAY,      [Zn2FPU01], 5>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;
defm : Zn2WriteResFpuPair<WriteFRcp,      [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFRcpX,     [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFRcpY,     [Zn2FPU01], 5>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;
defm : Zn2WriteResFpuPair<WriteFRsqrt,    [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFRsqrtX,   [Zn2FPU01], 5>;
defm : Zn2WriteResFpuPair<WriteFRsqrtY,   [Zn2FPU01], 5>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;
defm : Zn2WriteResFpuPair<WriteFSqrt,     [Zn2FPU3], 14, [7]>;
defm : Zn2WriteResFpuPair<WriteFSqrtX,    [Zn2FPU3], 14, [7]>;
defm : Zn2WriteResFpuPair<WriteFSqrtY,    [Zn2FPU3], 14, [7]>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : Zn2WriteResFpuPair<WriteFSqrt64,   [Zn2FPU3], 20, [10]>;
defm : Zn2WriteResFpuPair<WriteFSqrt64X,  [Zn2FPU3], 20, [10]>;
defm : Zn2WriteResFpuPair<WriteFSqrt64Y,  [Zn2FPU3], 20, [10]>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : Zn2WriteResFpuPair<WriteFSqrt80,   [Zn2FPU3], 20, [20]>;
defm : Zn2WriteResFpuPair<WriteFShuffle256, [Zn2FPU12], 2>;
defm : Zn2WriteResFpuPair<WriteFVarShuffle256, [Zn2FPU12], 2>;

// Vector integer operations which uses FPU units
defm : X86WriteRes<WriteVecLoad,         [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [Zn2AGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [Zn2AGU,Zn2FPU01], 8, [1,2], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [Zn2AGU,Zn2FPU01], 8, [1,2], 2>;
defm : X86WriteRes<WriteVecStore,        [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreX,       [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreY,       [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreNT,      [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreNTY,     [Zn2AGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMaskedStore32,  [Zn2AGU,Zn2FPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteVecMaskedStore32Y, [Zn2AGU,Zn2FPU01], 5, [1,2], 2>;
defm : X86WriteRes<WriteVecMaskedStore64,  [Zn2AGU,Zn2FPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteVecMaskedStore64Y, [Zn2AGU,Zn2FPU01], 5, [1,2], 2>;
defm : X86WriteRes<WriteVecMove,         [Zn2FPU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [Zn2FPU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [Zn2FPU], 2, [1], 2>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;
defm : X86WriteRes<WriteVecMoveToGpr,    [Zn2FPU2], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [Zn2FPU2], 3, [1], 1>;
defm : X86WriteRes<WriteEMMS,            [Zn2FPU], 2, [1], 1>;

defm : Zn2WriteResFpuPair<WriteVecShift,   [Zn2FPU2],  1>;
defm : Zn2WriteResFpuPair<WriteVecShiftX,  [Zn2FPU2],  1>;
defm : Zn2WriteResFpuPair<WriteVecShiftY,  [Zn2FPU2],  1>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;
defm : Zn2WriteResFpuPair<WriteVecShiftImm,  [Zn2FPU2], 1>;
defm : Zn2WriteResFpuPair<WriteVecShiftImmX, [Zn2FPU2], 1>;
defm : Zn2WriteResFpuPair<WriteVecShiftImmY, [Zn2FPU2], 1>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : Zn2WriteResFpuPair<WriteVarVecShift,  [Zn2FPU1], 3, [2], 1>;
defm : Zn2WriteResFpuPair<WriteVarVecShiftY, [Zn2FPU1], 3, [2], 1>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;
defm : Zn2WriteResFpuPair<WriteVecLogic,   [Zn2FPU],   1>;
defm : Zn2WriteResFpuPair<WriteVecLogicX,  [Zn2FPU],   1>;
defm : Zn2WriteResFpuPair<WriteVecLogicY,  [Zn2FPU],   1>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : Zn2WriteResFpuPair<WriteVecTest,    [Zn2FPU12], 3, [2], 1, 7, 1>;
defm : Zn2WriteResFpuPair<WriteVecTestY,   [Zn2FPU12], 3, [2], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : Zn2WriteResFpuPair<WriteVecALU,     [Zn2FPU013],   1>;
defm : Zn2WriteResFpuPair<WriteVecALUX,    [Zn2FPU013],   1>;
defm : Zn2WriteResFpuPair<WriteVecALUY,    [Zn2FPU013],   1>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : Zn2WriteResFpuPair<WriteVecIMul,    [Zn2FPU0],  4>;
defm : Zn2WriteResFpuPair<WriteVecIMulX,   [Zn2FPU0],  4>;
defm : Zn2WriteResFpuPair<WriteVecIMulY,   [Zn2FPU0],  4>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
defm : Zn2WriteResFpuPair<WritePMULLD,     [Zn2FPU0],  4, [2]>;
defm : Zn2WriteResFpuPair<WritePMULLDY,    [Zn2FPU0],  4, [2]>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : Zn2WriteResFpuPair<WriteShuffle,    [Zn2FPU12],   1>;
defm : Zn2WriteResFpuPair<WriteShuffleX,   [Zn2FPU12],   1>;
defm : Zn2WriteResFpuPair<WriteShuffleY,   [Zn2FPU12],   1>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : Zn2WriteResFpuPair<WriteVarShuffle, [Zn2FPU12],   1>;
defm : Zn2WriteResFpuPair<WriteVarShuffleX,[Zn2FPU12],   1>;
defm : Zn2WriteResFpuPair<WriteVarShuffleY,[Zn2FPU12],   1>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : Zn2WriteResFpuPair<WriteBlend,      [Zn2FPU013], 1>;
defm : Zn2WriteResFpuPair<WriteBlendY,     [Zn2FPU013], 1>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : Zn2WriteResFpuPair<WriteVarBlend,   [Zn2FPU0],  1>;
defm : Zn2WriteResFpuPair<WriteVarBlendY,  [Zn2FPU0],  1>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : Zn2WriteResFpuPair<WriteShuffle256, [Zn2FPU12],   2>;
defm : Zn2WriteResFpuPair<WriteVPMOV256,   [Zn2FPU12],  4, [1], 2, 4>;
defm : Zn2WriteResFpuPair<WriteVarShuffle256, [Zn2FPU12],   2>;
defm : Zn2WriteResFpuPair<WritePSADBW,     [Zn2FPU0],  3>;
defm : Zn2WriteResFpuPair<WritePSADBWX,    [Zn2FPU0],  3>;
defm : Zn2WriteResFpuPair<WritePSADBWY,    [Zn2FPU0],  3>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : Zn2WriteResFpuPair<WritePHMINPOS,   [Zn2FPU0],  4>;

// Vector insert/extract operations.
defm : Zn2WriteResFpuPair<WriteVecInsert,   [Zn2FPU],   1>;

def : WriteRes<WriteVecExtract, [Zn2FPU12, Zn2FPU2]> {
  let Latency = 2;
  let ReleaseAtCycles = [1, 2];
}
def : WriteRes<WriteVecExtractSt, [Zn2AGU, Zn2FPU12, Zn2FPU2]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1, 2, 3];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK, [Zn2FPU2]>;
def : WriteRes<WriteMMXMOVMSK, [Zn2FPU2]>;
def : WriteRes<WriteVecMOVMSK, [Zn2FPU2]>;

def : WriteRes<WriteVecMOVMSKY, [Zn2FPU2]> {
  let NumMicroOps = 2;
  let Latency = 2;
  let ReleaseAtCycles = [2];
}

// AES Instructions.
defm : Zn2WriteResFpuPair<WriteAESDecEnc, [Zn2FPU01], 4>;
defm : Zn2WriteResFpuPair<WriteAESIMC,    [Zn2FPU01], 4>;
defm : Zn2WriteResFpuPair<WriteAESKeyGen, [Zn2FPU01], 4>;

def : WriteRes<WriteFence,  [Zn2AGU]>;
def : WriteRes<WriteNop, []>;

// Microcoded Instructions
def Zn2WriteMicrocoded : SchedWriteRes<[]> {
  let Latency = 100;
}

def : SchedAlias<WriteMicrocoded, Zn2WriteMicrocoded>;
def : SchedAlias<WriteFCMOV, Zn2WriteMicrocoded>;
def : SchedAlias<WriteSystem, Zn2WriteMicrocoded>;
def : SchedAlias<WriteMPSAD, Zn2WriteMicrocoded>;
def : SchedAlias<WriteMPSADY, Zn2WriteMicrocoded>;
def : SchedAlias<WriteMPSADLd, Zn2WriteMicrocoded>;
def : SchedAlias<WriteMPSADYLd, Zn2WriteMicrocoded>;
def : SchedAlias<WriteCLMul, Zn2WriteMicrocoded>;
def : SchedAlias<WriteCLMulLd, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpIStrM, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpIStrMLd, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpEStrI, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpEStrILd, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpEStrM, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpEStrMLd, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpIStrI, Zn2WriteMicrocoded>;
def : SchedAlias<WritePCmpIStrILd, Zn2WriteMicrocoded>;
def : SchedAlias<WriteLDMXCSR, Zn2WriteMicrocoded>;
def : SchedAlias<WriteSTMXCSR, Zn2WriteMicrocoded>;

//=== Regex based InstRW ===//
// Notation:
// - r: register.
// - m = memory.
// - i = immediate
// - mm: 64 bit mmx register.
// - x = 128 bit xmm register.
// - (x)mm = mmx or xmm register.
// - y = 256 bit ymm register.
// - v = any vector register.

//=== Integer Instructions ===//
//-- Move instructions --//
// MOV.
// r16,m.
def : InstRW<[WriteALULd, ReadAfterLd], (instrs MOV16rm)>;

// XCHG.
// r,r.
def Zn2WriteXCHG : SchedWriteRes<[Zn2ALU]> {
  let NumMicroOps = 2;
}

def : InstRW<[Zn2WriteXCHG], (instregex "^XCHG(8|16|32|64)rr", "^XCHG(16|32|64)ar")>;

// r,m.
def Zn2WriteXCHGrm : SchedWriteRes<[Zn2AGU, Zn2ALU]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WriteXCHGrm, ReadAfterLd], (instregex "^XCHG(8|16|32|64)rm")>;

def : InstRW<[WriteMicrocoded], (instrs XLAT)>;

// POP16.
// r.
def Zn2WritePop16r : SchedWriteRes<[Zn2AGU]>{
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WritePop16r], (instrs POP16rmm)>;
def : InstRW<[WriteMicrocoded], (instregex "POPF(16|32)")>;
def : InstRW<[WriteMicrocoded], (instregex "POPA(16|32)")>;


// PUSH.
// r. Has default values.
// m.
def Zn2WritePUSH : SchedWriteRes<[Zn2AGU]>{
  let Latency = 4;
}
def : InstRW<[Zn2WritePUSH], (instregex "PUSH(16|32)rmm")>;

// PUSHF
def : InstRW<[WriteMicrocoded], (instregex "PUSHF(16|32)")>;

// PUSHA.
def Zn2WritePushA : SchedWriteRes<[Zn2AGU]> {
  let Latency = 8;
}
def : InstRW<[Zn2WritePushA], (instregex "PUSHA(16|32)")>;

//LAHF
def : InstRW<[WriteMicrocoded], (instrs LAHF)>;

// MOVBE.
// r,m.
def Zn2WriteMOVBE : SchedWriteRes<[Zn2AGU, Zn2ALU]> {
  let Latency = 5;
}
def : InstRW<[Zn2WriteMOVBE, ReadAfterLd], (instregex "MOVBE(16|32|64)rm")>;

// m16,r16.
def : InstRW<[Zn2WriteMOVBE], (instregex "MOVBE(16|32|64)mr")>;

//-- Arithmetic instructions --//

// ADD SUB.
// m,r/i.
def : InstRW<[WriteALULd], (instregex "(ADD|SUB)(8|16|32|64)m(r|i)",
                          "(ADD|SUB)(8|16|32|64)mi8",
                          "(ADD|SUB)64mi32")>;

// ADC SBB.
// m,r/i.
def : InstRW<[WriteALULd],
             (instregex "(ADC|SBB)(8|16|32|64)m(r|i)",
              "(ADC|SBB)(16|32|64)mi8",
              "(ADC|SBB)64mi32")>;

// INC DEC NOT NEG.
// m.
def : InstRW<[WriteALULd],
             (instregex "(INC|DEC|NOT|NEG)(8|16|32|64)m")>;

// MUL IMUL.
// r16.
def Zn2WriteMul16 : SchedWriteRes<[Zn2ALU1, Zn2Multiplier]> {
  let Latency = 3;
}
def Zn2WriteMul16Imm : SchedWriteRes<[Zn2ALU1, Zn2Multiplier]> {
  let Latency = 4;
}
def : SchedAlias<WriteIMul16, Zn2WriteMul16>;
def : SchedAlias<WriteIMul16Imm, Zn2WriteMul16Imm>;
def : SchedAlias<WriteIMul16Reg, Zn2WriteMul16>;

// m16.
def Zn2WriteMul16Ld : SchedWriteRes<[Zn2AGU, Zn2ALU1, Zn2Multiplier]> {
  let Latency = 7;
}
def : SchedAlias<WriteIMul16Ld, Zn2WriteMul16Ld>;
def : SchedAlias<WriteIMul16ImmLd, Zn2WriteMul16Ld>;
def : SchedAlias<WriteIMul16RegLd, Zn2WriteMul16Ld>;

// r32.
def Zn2WriteMul32 : SchedWriteRes<[Zn2ALU1, Zn2Multiplier]> {
  let Latency = 3;
}
def : SchedAlias<WriteIMul32, Zn2WriteMul32>;
def : SchedAlias<WriteIMul32Imm, Zn2WriteMul32>;
def : SchedAlias<WriteIMul32Reg, Zn2WriteMul32>;

// m32.
def Zn2WriteMul32Ld : SchedWriteRes<[Zn2AGU, Zn2ALU1, Zn2Multiplier]> {
  let Latency = 7;
}
def : SchedAlias<WriteIMul32Ld, Zn2WriteMul32Ld>;
def : SchedAlias<WriteIMul32ImmLd, Zn2WriteMul32Ld>;
def : SchedAlias<WriteIMul32RegLd, Zn2WriteMul32Ld>;

// r64.
def Zn2WriteMul64 : SchedWriteRes<[Zn2ALU1, Zn2Multiplier]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteIMul64, Zn2WriteMul64>;
def : SchedAlias<WriteIMul64Imm, Zn2WriteMul64>;
def : SchedAlias<WriteIMul64Reg, Zn2WriteMul64>;

// m64.
def Zn2WriteMul64Ld : SchedWriteRes<[Zn2AGU, Zn2ALU1, Zn2Multiplier]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteIMul64Ld, Zn2WriteMul64Ld>;
def : SchedAlias<WriteIMul64ImmLd, Zn2WriteMul64Ld>;
def : SchedAlias<WriteIMul64RegLd, Zn2WriteMul64Ld>;

// MULX.
// Numbers are based on the AMD SOG for Family 17h - Instruction Latencies.
defm : Zn2WriteResPair<WriteMULX32, [Zn2ALU1, Zn2Multiplier], 3, [1, 1], 1, 4, 0>;
defm : Zn2WriteResPair<WriteMULX64, [Zn2ALU1, Zn2Multiplier], 3, [1, 1], 1, 4, 0>;

//-- Control transfer instructions --//

// J(E|R)CXZ.
def Zn2WriteJCXZ : SchedWriteRes<[Zn2ALU03]>;
def : InstRW<[Zn2WriteJCXZ], (instrs JCXZ, JECXZ, JRCXZ)>;

// LOOP.
def Zn2WriteLOOP : SchedWriteRes<[Zn2ALU03]>;
def : InstRW<[Zn2WriteLOOP], (instrs LOOP)>;

// LOOP(N)E, LOOP(N)Z
def Zn2WriteLOOPE : SchedWriteRes<[Zn2ALU03]>;
def : InstRW<[Zn2WriteLOOPE], (instrs LOOPE, LOOPNE)>;

// CALL.
// r.
def Zn2WriteCALLr : SchedWriteRes<[Zn2AGU, Zn2ALU03]>;
def : InstRW<[Zn2WriteCALLr], (instregex "CALL(16|32)r")>;

def : InstRW<[WriteMicrocoded], (instregex "CALL(16|32)m")>;

// RET.
def Zn2WriteRET : SchedWriteRes<[Zn2ALU03]> {
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WriteRET], (instregex "RET(16|32|64)", "LRET(16|32|64)",
                            "IRET(16|32|64)")>;

//-- Logic instructions --//

// AND OR XOR.
// m,r/i.
def : InstRW<[WriteALULd],
             (instregex "(AND|OR|XOR)(8|16|32|64)m(r|i)",
              "(AND|OR|XOR)(8|16|32|64)mi8", "(AND|OR|XOR)64mi32")>;

// Define ALU latency variants
def Zn2WriteALULat2 : SchedWriteRes<[Zn2ALU]> {
  let Latency = 2;
}
def Zn2WriteALULat2Ld : SchedWriteRes<[Zn2AGU, Zn2ALU]> {
  let Latency = 6;
}

// BTR BTS BTC.
// m,r,i.
def Zn2WriteBTRSCm : SchedWriteRes<[Zn2AGU, Zn2ALU]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
// m,r,i.
def : SchedAlias<WriteBitTestSetImmRMW, Zn2WriteBTRSCm>;
def : SchedAlias<WriteBitTestSetRegRMW, Zn2WriteBTRSCm>;

// PDEP PEXT.
// r,r,r.
def : InstRW<[WriteMicrocoded], (instregex "PDEP(32|64)rr", "PEXT(32|64)rr")>;
// r,r,m.
def : InstRW<[WriteMicrocoded], (instregex "PDEP(32|64)rm", "PEXT(32|64)rm")>;

// RCR RCL.
// m,i.
def : InstRW<[WriteMicrocoded], (instregex "RC(R|L)(8|16|32|64)m(1|i|CL)")>;

// SHR SHL SAR.
// m,i.
def : InstRW<[WriteShiftLd], (instregex "S(A|H)(R|L)(8|16|32|64)m(i|1)")>;

// SHRD SHLD.
// m,r
def : InstRW<[WriteShiftLd], (instregex "SH(R|L)D(16|32|64)mri8")>;

// r,r,cl.
def : InstRW<[WriteMicrocoded], (instregex "SH(R|L)D(16|32|64)rrCL")>;

// m,r,cl.
def : InstRW<[WriteMicrocoded], (instregex "SH(R|L)D(16|32|64)mrCL")>;

//-- Misc instructions --//
// CMPXCHG8B.
def Zn2WriteCMPXCHG8B : SchedWriteRes<[Zn2AGU, Zn2ALU]> {
  let NumMicroOps = 18;
}
def : InstRW<[Zn2WriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def : InstRW<[WriteMicrocoded], (instrs CMPXCHG16B)>;

// LEAVE
def Zn2WriteLEAVE : SchedWriteRes<[Zn2ALU, Zn2AGU]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WriteLEAVE], (instregex "LEAVE")>;

// PAUSE.
def : InstRW<[WriteMicrocoded], (instrs PAUSE)>;

// XADD.
def Zn2XADD : SchedWriteRes<[Zn2ALU]>;
def : InstRW<[Zn2XADD], (instregex "XADD(8|16|32|64)rr")>;
def : InstRW<[WriteMicrocoded], (instregex "XADD(8|16|32|64)rm")>;

//=== Floating Point x87 Instructions ===//
//-- Move instructions --//

def Zn2WriteFLDr : SchedWriteRes<[Zn2FPU13]> ;

def Zn2WriteSTr: SchedWriteRes<[Zn2FPU23]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// LD_F.
// r.
def : InstRW<[Zn2WriteFLDr], (instrs LD_Frr)>;

// m.
def Zn2WriteLD_F80m : SchedWriteRes<[Zn2AGU, Zn2FPU13]> {
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WriteLD_F80m], (instrs LD_F80m)>;

// FST(P).
// r.
def : InstRW<[Zn2WriteSTr], (instregex "ST_(F|FP)rr")>;

// m80.
def Zn2WriteST_FP80m : SchedWriteRes<[Zn2AGU, Zn2FPU23]> {
  let Latency = 5;
}
def : InstRW<[Zn2WriteST_FP80m], (instrs ST_FP80m)>;

def Zn2WriteFXCH : SchedWriteRes<[Zn2FPU]>;

// FXCHG.
def : InstRW<[Zn2WriteFXCH], (instrs XCH_F)>;

// FILD.
def Zn2WriteFILD : SchedWriteRes<[Zn2AGU, Zn2FPU3]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[Zn2WriteFILD], (instregex "ILD_F(16|32|64)m")>;

// FIST(P) FISTTP.
def Zn2WriteFIST : SchedWriteRes<[Zn2AGU, Zn2FPU23]> {
  let Latency = 12;
}
def : InstRW<[Zn2WriteFIST], (instregex "IS(T|TT)_(F|FP)(16|32|64)m")>;

def Zn2WriteFPU13 : SchedWriteRes<[Zn2AGU, Zn2FPU13]> {
  let Latency = 8;
}

def Zn2WriteFPU3 : SchedWriteRes<[Zn2AGU, Zn2FPU3]> {
  let Latency = 11;
}

// FLDZ.
def : SchedAlias<WriteFLD0, Zn2WriteFPU13>;

// FLD1.
def : SchedAlias<WriteFLD1, Zn2WriteFPU3>;

// FLDPI FLDL2E etc.
def : SchedAlias<WriteFLDC, Zn2WriteFPU3>;

// FNSTSW.
// AX.
def : InstRW<[WriteMicrocoded], (instrs FNSTSW16r)>;

// FLDCW.
def : InstRW<[WriteMicrocoded], (instrs FLDCW16m)>;

// FNSTCW.
def : InstRW<[WriteMicrocoded], (instrs FNSTCW16m)>;

// FINCSTP FDECSTP.
def : InstRW<[Zn2WriteFPU3], (instrs FINCSTP, FDECSTP)>;

// FFREE.
def : InstRW<[Zn2WriteFPU3], (instregex "FFREE")>;

//-- Arithmetic instructions --//

def Zn2WriteFPU3Lat1 : SchedWriteRes<[Zn2FPU3]> ;

def Zn2WriteFPU0Lat1 : SchedWriteRes<[Zn2FPU0]> ;

def Zn2WriteFPU0Lat1Ld : SchedWriteRes<[Zn2AGU, Zn2FPU0]> {
  let Latency = 8;
}

// FCHS.
def : InstRW<[Zn2WriteFPU3Lat1], (instregex "CHS_F")>;

// FCOM(P) FUCOM(P).
// r.
def : InstRW<[Zn2WriteFPU0Lat1], (instregex "COM(P?)_FST0r", "UCOM_F(P?)r")>;
// m.
def : InstRW<[Zn2WriteFPU0Lat1Ld], (instregex "FCOM(P?)(32|64)m")>;

// FCOMPP FUCOMPP.
// r.
def : InstRW<[Zn2WriteFPU0Lat1], (instrs FCOMPP, UCOM_FPPr)>;

def Zn2WriteFPU02 : SchedWriteRes<[Zn2AGU, Zn2FPU02]>
{
  let Latency = 9;
}

// FCOMI(P) FUCOMI(P).
// m.
def : InstRW<[Zn2WriteFPU02], (instrs COM_FIPr, COM_FIr, UCOM_FIPr, UCOM_FIr)>;

def Zn2WriteFPU03 : SchedWriteRes<[Zn2AGU, Zn2FPU03]>
{
  let Latency = 12;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,3];
}

// FICOM(P).
def : InstRW<[Zn2WriteFPU03], (instregex "FICOM(P?)(16|32)m")>;

// FTST.
def : InstRW<[Zn2WriteFPU0Lat1], (instregex "TST_F")>;

// FXAM.
def : InstRW<[Zn2WriteFPU3Lat1], (instrs XAM_F)>;

// FNOP.
def : InstRW<[Zn2WriteFPU0Lat1], (instrs FNOP)>;

// WAIT.
def : InstRW<[Zn2WriteFPU0Lat1], (instrs WAIT)>;

//=== Integer MMX and XMM Instructions ===//

def Zn2WriteFPU013 : SchedWriteRes<[Zn2FPU013]> ;
def Zn2WriteFPU013m : SchedWriteRes<[Zn2AGU, Zn2FPU013]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Zn2WriteFPU01 : SchedWriteRes<[Zn2FPU01]> ;
def Zn2WriteFPU01Y : SchedWriteRes<[Zn2FPU01]> {
  let NumMicroOps = 2;
}

// VPBLENDD.
// v,v,v,i.
def : InstRW<[Zn2WriteFPU01], (instrs VPBLENDDrri)>;
// ymm
def : InstRW<[Zn2WriteFPU01Y], (instrs VPBLENDDYrri)>;

// v,v,m,i
def Zn2WriteFPU01Op2 : SchedWriteRes<[Zn2AGU, Zn2FPU01]> {
  let NumMicroOps = 2;
  let Latency = 8;
  let ReleaseAtCycles = [1, 2];
}
def Zn2WriteFPU01Op2Y : SchedWriteRes<[Zn2AGU, Zn2FPU01]> {
  let NumMicroOps = 2;
  let Latency = 9;
  let ReleaseAtCycles = [1, 3];
}
def : InstRW<[Zn2WriteFPU01Op2], (instrs VPBLENDDrmi)>;
def : InstRW<[Zn2WriteFPU01Op2Y], (instrs VPBLENDDYrmi)>;

// MASKMOVQ.
def : InstRW<[WriteMicrocoded], (instregex "MMX_MASKMOVQ(64)?")>;

// MASKMOVDQU.
def : InstRW<[WriteMicrocoded], (instregex "(V?)MASKMOVDQU(64)?")>;

// VPMASKMOVD.
// ymm
def : InstRW<[WriteMicrocoded],
                               (instregex "VPMASKMOVD(Y?)rm")>;
// m, v,v.
def : InstRW<[WriteMicrocoded], (instregex "VPMASKMOV(D|Q)(Y?)mr")>;

// VPBROADCAST B/W.
// x, m8/16.
def Zn2WriteVPBROADCAST128Ld : SchedWriteRes<[Zn2AGU, Zn2FPU12]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[Zn2WriteVPBROADCAST128Ld],
                                     (instregex "VPBROADCAST(B|W)rm")>;

// y, m8/16
def Zn2WriteVPBROADCAST256Ld : SchedWriteRes<[Zn2AGU, Zn2FPU1]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[Zn2WriteVPBROADCAST256Ld],
                                     (instregex "VPBROADCAST(B|W)Yrm")>;

// VPGATHER.
def : InstRW<[WriteMicrocoded], (instregex "VPGATHER(Q|D)(Q|D)(Y?)rm")>;

//-- Arithmetic instructions --//

// HADD, HSUB PS/PD
// PHADD|PHSUB (S) W/D.
defm : Zn2WriteResFpuPair<WriteFHAdd, [], 7>;
defm : Zn2WriteResFpuPair<WriteFHAddY, [], 7>;
defm : Zn2WriteResFpuPair<WritePHAdd, [], 3>;
defm : Zn2WriteResFpuPair<WritePHAddX, [], 3>;
defm : Zn2WriteResFpuPair<WritePHAddY, [], 3>;

// PCMPGTQ.
def Zn2WritePCMPGTQr : SchedWriteRes<[Zn2FPU03]>;
def : InstRW<[Zn2WritePCMPGTQr], (instregex "(V?)PCMPGTQ(Y?)rr")>;

// x <- x,m.
def Zn2WritePCMPGTQm : SchedWriteRes<[Zn2AGU, Zn2FPU03]> {
  let Latency = 8;
}
// ymm.
def Zn2WritePCMPGTQYm : SchedWriteRes<[Zn2AGU, Zn2FPU03]> {
  let Latency = 8;
}
def : InstRW<[Zn2WritePCMPGTQm], (instregex "(V?)PCMPGTQrm")>;
def : InstRW<[Zn2WritePCMPGTQYm], (instrs VPCMPGTQYrm)>;

//=== Floating Point XMM and YMM Instructions ===//
//-- Move instructions --//

// VPERM2F128 / VPERM2I128.
def : InstRW<[WriteMicrocoded], (instrs VPERM2F128rr,
                                        VPERM2I128rr)>;
def : InstRW<[WriteMicrocoded], (instrs VPERM2F128rm,
                                        VPERM2I128rm)>;

def Zn2WriteBROADCAST : SchedWriteRes<[Zn2AGU, Zn2FPU13]> {
  let NumMicroOps = 2;
  let Latency = 8;
}
// VBROADCASTF128 / VBROADCASTI128.
def : InstRW<[Zn2WriteBROADCAST], (instrs VBROADCASTF128rm,
                                          VBROADCASTI128rm)>;

// EXTRACTPS.
// r32,x,i.
def Zn2WriteEXTRACTPSr : SchedWriteRes<[Zn2FPU12, Zn2FPU2]> {
  let Latency = 2;
  let NumMicroOps = 2; 
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[Zn2WriteEXTRACTPSr], (instregex "(V?)EXTRACTPSrr")>;

def Zn2WriteEXTRACTPSm : SchedWriteRes<[Zn2AGU,Zn2FPU12, Zn2FPU2]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [5, 1, 2];
}
// m32,x,i.
def : InstRW<[Zn2WriteEXTRACTPSm], (instregex "(V?)EXTRACTPSmr")>;

// VEXTRACTF128 / VEXTRACTI128.
// x,y,i.
def : InstRW<[Zn2WriteFPU013], (instrs VEXTRACTF128rr,
                                       VEXTRACTI128rr)>;

// m128,y,i.
def : InstRW<[Zn2WriteFPU013m], (instrs VEXTRACTF128mr,
                                        VEXTRACTI128mr)>;

def Zn2WriteVINSERT128r: SchedWriteRes<[Zn2FPU013]> {
  let Latency = 2;
//  let ReleaseAtCycles = [2];
}
def Zn2WriteVINSERT128Ld: SchedWriteRes<[Zn2AGU,Zn2FPU013]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
// VINSERTF128 / VINSERTI128.
// y,y,x,i.
def : InstRW<[Zn2WriteVINSERT128r], (instrs VINSERTF128rr,
                                            VINSERTI128rr)>;
def : InstRW<[Zn2WriteVINSERT128Ld], (instrs VINSERTF128rm,
                                             VINSERTI128rm)>;

// VGATHER.
def : InstRW<[WriteMicrocoded], (instregex "VGATHER(Q|D)(PD|PS)(Y?)rm")>;

//-- Conversion instructions --//
def Zn2WriteCVTPD2PSr: SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}
def Zn2WriteCVTPD2PSYr: SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}

// CVTPD2PS.
// x,x.
def : SchedAlias<WriteCvtPD2PS,  Zn2WriteCVTPD2PSr>;
// y,y.
def : SchedAlias<WriteCvtPD2PSY, Zn2WriteCVTPD2PSYr>;
// z,z.
defm : X86WriteResUnsupported<WriteCvtPD2PSZ>;

def Zn2WriteCVTPD2PSLd: SchedWriteRes<[Zn2AGU,Zn2FPU3]> {
  let Latency = 10;
}
// x,m128.
def : SchedAlias<WriteCvtPD2PSLd, Zn2WriteCVTPD2PSLd>;

// x,m256.
def Zn2WriteCVTPD2PSYLd : SchedWriteRes<[Zn2AGU, Zn2FPU3]> {
  let Latency = 10;
}
def : SchedAlias<WriteCvtPD2PSYLd, Zn2WriteCVTPD2PSYLd>;
// z,m512
defm : X86WriteResUnsupported<WriteCvtPD2PSZLd>;

// CVTSD2SS.
// x,x.
// Same as WriteCVTPD2PSr
def : SchedAlias<WriteCvtSD2SS, Zn2WriteCVTPD2PSr>;

// x,m64.
def : SchedAlias<WriteCvtSD2SSLd, Zn2WriteCVTPD2PSLd>;

// CVTPS2PD.
// x,x.
def Zn2WriteCVTPS2PDr : SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}
def : SchedAlias<WriteCvtPS2PD, Zn2WriteCVTPS2PDr>;

// x,m64.
// y,m128.
def Zn2WriteCVTPS2PDLd : SchedWriteRes<[Zn2AGU, Zn2FPU3]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteCvtPS2PDLd, Zn2WriteCVTPS2PDLd>;
def : SchedAlias<WriteCvtPS2PDYLd, Zn2WriteCVTPS2PDLd>;
defm : X86WriteResUnsupported<WriteCvtPS2PDZLd>;

// y,x.
def Zn2WriteVCVTPS2PDY : SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}
def : SchedAlias<WriteCvtPS2PDY, Zn2WriteVCVTPS2PDY>;
defm : X86WriteResUnsupported<WriteCvtPS2PDZ>;

// CVTSS2SD.
// x,x.
def Zn2WriteCVTSS2SDr : SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}
def : SchedAlias<WriteCvtSS2SD, Zn2WriteCVTSS2SDr>;

// x,m32.
def Zn2WriteCVTSS2SDLd : SchedWriteRes<[Zn2AGU, Zn2FPU3]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1, 2];
}
def : SchedAlias<WriteCvtSS2SDLd, Zn2WriteCVTSS2SDLd>;

def Zn2WriteCVTDQ2PDr: SchedWriteRes<[Zn2FPU12,Zn2FPU3]> {
  let Latency = 3;
}
// CVTDQ2PD.
// x,x.
def : InstRW<[Zn2WriteCVTDQ2PDr], (instregex "(V)?CVTDQ2P(D|S)rr")>;

// Same as xmm
// y,x.
def : InstRW<[Zn2WriteCVTDQ2PDr], (instrs VCVTDQ2PDYrr)>;
def : InstRW<[Zn2WriteCVTDQ2PDr], (instrs VCVTDQ2PSYrr)>;

def Zn2WriteCVTPD2DQr: SchedWriteRes<[Zn2FPU12, Zn2FPU3]> {
  let Latency = 3;
}
// CVT(T)P(D|S)2DQ.
// x,x.
def : InstRW<[Zn2WriteCVTPD2DQr], (instregex "(V?)CVT(T?)P(D|S)2DQrr")>;

def Zn2WriteCVTPD2DQLd: SchedWriteRes<[Zn2AGU,Zn2FPU12,Zn2FPU3]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
// x,m128.
def : InstRW<[Zn2WriteCVTPD2DQLd], (instregex "(V?)CVT(T?)PD2DQrm")>;
// same as xmm handling
// x,y.
def : InstRW<[Zn2WriteCVTPD2DQr], (instregex "VCVT(T?)PD2DQYrr")>;
// x,m256.
def : InstRW<[Zn2WriteCVTPD2DQLd], (instregex "VCVT(T?)PD2DQYrm")>;

def Zn2WriteCVTPS2PIr: SchedWriteRes<[Zn2FPU3]> {
  let Latency = 4;
}
// CVT(T)PS2PI.
// mm,x.
def : InstRW<[Zn2WriteCVTPS2PIr], (instregex "MMX_CVT(T?)PS2PIrr")>;

// CVTPI2PD.
// x,mm.
def : InstRW<[Zn2WriteCVTPS2PDr], (instrs MMX_CVTPI2PDrr)>;

// CVT(T)PD2PI.
// mm,x.
def : InstRW<[Zn2WriteCVTPS2PIr], (instregex "MMX_CVT(T?)PD2PIrr")>;

def Zn2WriteCVSTSI2SSr: SchedWriteRes<[Zn2FPU3]> {
  let Latency = 3;
}

// same as CVTPD2DQr
// CVT(T)SS2SI.
// r32,x.
def : InstRW<[Zn2WriteCVTPD2DQr], (instregex "(V?)CVT(T?)SS2SI(64)?rr")>;
// same as CVTPD2DQm
// r32,m32.
def : InstRW<[Zn2WriteCVTPD2DQLd], (instregex "(V?)CVT(T?)SS2SI(64)?rm")>;

def Zn2WriteCVSTSI2SDr: SchedWriteRes<[Zn2FPU013, Zn2FPU3]> {
  let Latency = 3;
}
// CVTSI2SD.
// x,r32/64.
def : InstRW<[Zn2WriteCVSTSI2SDr], (instregex "(V?)CVTSI(64)?2SDrr")>;


def Zn2WriteCVSTSI2SIr: SchedWriteRes<[Zn2FPU3, Zn2FPU2]> {
  let Latency = 4;
}
def Zn2WriteCVSTSI2SILd: SchedWriteRes<[Zn2AGU, Zn2FPU3, Zn2FPU2]> {
  let Latency = 11;
}
// CVTSD2SI.
// r32/64
def : InstRW<[Zn2WriteCVSTSI2SIr], (instregex "(V?)CVT(T?)SD2SI(64)?rr")>;
// r32,m32.
def : InstRW<[Zn2WriteCVSTSI2SILd], (instregex "(V?)CVT(T?)SD2SI(64)?rm")>;

// VCVTPS2PH.
// x,v,i.
def : SchedAlias<WriteCvtPS2PH,    Zn2WriteMicrocoded>;
def : SchedAlias<WriteCvtPS2PHY,   Zn2WriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
// m,v,i.
def : SchedAlias<WriteCvtPS2PHSt,  Zn2WriteMicrocoded>;
def : SchedAlias<WriteCvtPS2PHYSt, Zn2WriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

// VCVTPH2PS.
// v,x.
def : SchedAlias<WriteCvtPH2PS,    Zn2WriteMicrocoded>;
def : SchedAlias<WriteCvtPH2PSY,   Zn2WriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZ>;
// v,m.
def : SchedAlias<WriteCvtPH2PSLd,  Zn2WriteMicrocoded>;
def : SchedAlias<WriteCvtPH2PSYLd, Zn2WriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZLd>;

//-- SSE4A instructions --//
// EXTRQ
def Zn2WriteEXTRQ: SchedWriteRes<[Zn2FPU12, Zn2FPU2]> {
  let Latency = 3;
}
def : InstRW<[Zn2WriteEXTRQ], (instregex "EXTRQ")>;

// INSERTQ
def Zn2WriteINSERTQ: SchedWriteRes<[Zn2FPU03,Zn2FPU1]> {
  let Latency = 4;
}
def : InstRW<[Zn2WriteINSERTQ], (instregex "INSERTQ")>;

//-- SHA instructions --//
// SHA256MSG2
def : InstRW<[WriteMicrocoded], (instregex "SHA256MSG2(Y?)r(r|m)")>;

// SHA1MSG1, SHA256MSG1
// x,x.
def Zn2WriteSHA1MSG1r : SchedWriteRes<[Zn2FPU12]> {
  let Latency = 2;
}
def : InstRW<[Zn2WriteSHA1MSG1r], (instregex "SHA(1|256)MSG1rr")>;
// x,m.
def Zn2WriteSHA1MSG1Ld : SchedWriteRes<[Zn2AGU, Zn2FPU12]> {
  let Latency = 9;
}
def : InstRW<[Zn2WriteSHA1MSG1Ld], (instregex "SHA(1|256)MSG1rm")>;

// SHA1MSG2
// x,x.
def Zn2WriteSHA1MSG2r : SchedWriteRes<[Zn2FPU12]> ;
def : InstRW<[Zn2WriteSHA1MSG2r], (instrs SHA1MSG2rr)>;
// x,m.
def Zn2WriteSHA1MSG2Ld : SchedWriteRes<[Zn2AGU, Zn2FPU12]> {
  let Latency = 8;
}
def : InstRW<[Zn2WriteSHA1MSG2Ld], (instrs SHA1MSG2rm)>;

// SHA1NEXTE
// x,x.
def Zn2WriteSHA1NEXTEr : SchedWriteRes<[Zn2FPU1]> ;
def : InstRW<[Zn2WriteSHA1NEXTEr], (instrs SHA1NEXTErr)>;
// x,m.
def Zn2WriteSHA1NEXTELd : SchedWriteRes<[Zn2AGU, Zn2FPU1]> {
  let Latency = 8;
}
def : InstRW<[Zn2WriteSHA1NEXTELd], (instrs SHA1NEXTErm)>;

// SHA1RNDS4
// x,x.
def Zn2WriteSHA1RNDS4r : SchedWriteRes<[Zn2FPU1]> {
  let Latency = 6;
}
def : InstRW<[Zn2WriteSHA1RNDS4r], (instrs SHA1RNDS4rri)>;
// x,m.
def Zn2WriteSHA1RNDS4Ld : SchedWriteRes<[Zn2AGU, Zn2FPU1]> {
  let Latency = 13;
}
def : InstRW<[Zn2WriteSHA1RNDS4Ld], (instrs SHA1RNDS4rmi)>;

// SHA256RNDS2
// x,x.
def Zn2WriteSHA256RNDS2r : SchedWriteRes<[Zn2FPU1]> {
  let Latency = 4;
}
def : InstRW<[Zn2WriteSHA256RNDS2r], (instrs SHA256RNDS2rr)>;
// x,m.
def Zn2WriteSHA256RNDS2Ld : SchedWriteRes<[Zn2AGU, Zn2FPU1]> {
  let Latency = 11;
}
def : InstRW<[Zn2WriteSHA256RNDS2Ld], (instrs SHA256RNDS2rm)>;

//-- Arithmetic instructions --//

// DPPS.
// x,x,i / v,v,v,i.
defm : Zn2WriteResPair<WriteDPPS, [], 15>;
def : SchedAlias<WriteDPPSY,  Zn2WriteMicrocoded>;

// x,m,i / v,v,m,i.
def : SchedAlias<WriteDPPSYLd,Zn2WriteMicrocoded>;

// DPPD.
// x,x,i.
def : SchedAlias<WriteDPPD,   Zn2WriteMicrocoded>;

// x,m,i.
def : SchedAlias<WriteDPPDLd, Zn2WriteMicrocoded>;

//-- Other instructions --//

// VZEROUPPER.
def : InstRW<[WriteALU], (instrs VZEROUPPER)>;

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

def : IsZeroIdiomFunction<[
  // GPR Zero-idioms.
  DepBreakingClass<[
    SUB32rr, SUB64rr,
    XOR32rr, XOR64rr
  ], ZeroIdiomPredicate>,

  // MMX Zero-idioms.
  DepBreakingClass<[
    MMX_PXORrr, MMX_PANDNrr, MMX_PSUBBrr,
    MMX_PSUBDrr, MMX_PSUBQrr, MMX_PSUBWrr,
    MMX_PSUBSBrr, MMX_PSUBSWrr, MMX_PSUBUSBrr, MMX_PSUBUSWrr,
    MMX_PCMPGTBrr, MMX_PCMPGTDrr, MMX_PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // SSE Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    XORPSrr, XORPDrr, ANDNPSrr, ANDNPDrr,

    // int variants.
    PXORrr, PANDNrr,
    PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
    PCMPGTBrr, PCMPGTDrr, PCMPGTQrr, PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // AVX XMM Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    VXORPSrr, VXORPDrr, VANDNPSrr, VANDNPDrr,

    // int variants.
    VPXORrr, VPANDNrr,
    VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
    VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr
  ], ZeroIdiomPredicate>,

  // AVX YMM Zero-idioms.
  DepBreakingClass<[
    // fp variants
    VXORPSYrr, VXORPDYrr, VANDNPSYrr, VANDNPDYrr,

    // int variants
    VPXORYrr, VPANDNYrr,
    VPSUBBYrr, VPSUBWYrr, VPSUBDYrr, VPSUBQYrr,
    VPCMPGTBYrr, VPCMPGTWYrr, VPCMPGTDYrr, VPCMPGTQYrr
  ], ZeroIdiomPredicate>
]>;

def : IsDepBreakingFunction<[
  // GPR
  DepBreakingClass<[ SBB32rr, SBB64rr ], ZeroIdiomPredicate>,
  DepBreakingClass<[ CMP32rr, CMP64rr ], CheckSameRegOperand<0, 1> >,

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
