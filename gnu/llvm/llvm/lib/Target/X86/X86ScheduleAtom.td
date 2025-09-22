//===- X86ScheduleAtom.td - X86 Atom Scheduling Definitions -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the schedule class data for the Intel Atom
// in order (Saltwell-32nm/Bonnell-45nm) processors.
//
//===----------------------------------------------------------------------===//

//
// Scheduling information derived from the "Intel 64 and IA32 Architectures
// Optimization Reference Manual", Chapter 13, Section 4.

// Atom machine model.
def AtomModel : SchedMachineModel {
  let IssueWidth = 2;  // Allows 2 instructions per scheduling group.
  let MicroOpBufferSize = 0; // In-order execution, always hide latency.
  let LoadLatency = 3; // Expected cycles, may be overriden.
  let HighLatency = 30;// Expected, may be overriden.

  // On the Atom, the throughput for taken branches is 2 cycles. For small
  // simple loops, expand by a small factor to hide the backedge cost.
  let LoopMicroOpBufferSize = 10;
  let PostRAScheduler = 1;
  let CompleteModel = 0;
}

let SchedModel = AtomModel in {

// Functional Units
def AtomPort0 : ProcResource<1>; // ALU: ALU0, shift/rotate, load/store
                                 // SIMD/FP: SIMD ALU, Shuffle,SIMD/FP multiply, divide
def AtomPort1 : ProcResource<1>; // ALU: ALU1, bit processing, jump, and LEA
                                 // SIMD/FP: SIMD ALU, FP Adder

// NOTE: This is for ops that can use EITHER port, not for ops that require BOTH ports.
def AtomPort01 : ProcResGroup<[AtomPort0, AtomPort1]>;

// Loads are 3 cycles, so ReadAfterLd registers needn't be available until 3
// cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 3>;
def : ReadAdvance<ReadAfterVecLd, 3>;
def : ReadAdvance<ReadAfterVecXLd, 3>;
def : ReadAdvance<ReadAfterVecYLd, 3>;

def : ReadAdvance<ReadInt2Fpu, 0>;

// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass AtomWriteResPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> RRPorts,
                            list<ProcResourceKind> RMPorts,
                            int RRLat = 1, int RMLat = 1,
                            list<int> RRRes = [1],
                            list<int> RMRes = [1],
                            int RRUOps = 1,
                            int RMUOps = 1> {
  // Register variant.
  def : WriteRes<SchedRW, RRPorts> {
    let Latency = RRLat;
    let ReleaseAtCycles = RRRes;
    let NumMicroOps = RRUOps;
  }

  // Memory variant.
  def : WriteRes<SchedRW.Folded, RMPorts> {
    let Latency = RMLat;
    let ReleaseAtCycles = RMRes;
    let NumMicroOps = RMUOps;
  }
}

// A folded store needs a cycle on Port0 for the store data.
def : WriteRes<WriteRMW, [AtomPort0]>;

////////////////////////////////////////////////////////////////////////////////
// Arithmetic.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteALU,    [AtomPort01], [AtomPort0]>;
defm : AtomWriteResPair<WriteADC,    [AtomPort01], [AtomPort0]>;

defm : AtomWriteResPair<WriteIMul8,     [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  7,  7,  [7,7],  [7,7], 3, 3>;
defm : AtomWriteResPair<WriteIMul16,    [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  7,  8,  [7,7],  [8,8], 4, 5>;
defm : AtomWriteResPair<WriteIMul16Imm, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 2, 3>;
defm : AtomWriteResPair<WriteIMul16Reg, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 2, 3>;
defm : AtomWriteResPair<WriteIMul32,    [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 3, 4>;
defm : AtomWriteResPair<WriteIMul32Imm, [AtomPort0],  [AtomPort0],   5,  5,  [5],  [5]>;
defm : AtomWriteResPair<WriteIMul32Reg, [AtomPort0],  [AtomPort0],   5,  5,  [5],  [5]>;
defm : AtomWriteResPair<WriteIMul64,    [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 12, 12, [12,12], [12,12], 8, 8>;
defm : AtomWriteResPair<WriteIMul64Imm, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 14, 14, [14,14], [14,14], 7, 7>;
defm : AtomWriteResPair<WriteIMul64Reg, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 12, 12, [12,12], [12,12], 6, 6>;
defm : X86WriteResUnsupported<WriteIMulH>;
defm : X86WriteResUnsupported<WriteIMulHLd>;
defm : X86WriteResPairUnsupported<WriteMULX32>;
defm : X86WriteResPairUnsupported<WriteMULX64>;

defm : X86WriteRes<WriteXCHG,        [AtomPort01], 2, [2], 1>;
defm : X86WriteRes<WriteBSWAP32,     [AtomPort0], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64,     [AtomPort0], 1, [1], 1>;
defm : AtomWriteResPair<WriteCMPXCHG, [AtomPort01], [AtomPort01], 15, 15, [15]>;
defm : X86WriteRes<WriteCMPXCHGRMW,   [AtomPort01, AtomPort0], 1, [1, 1], 1>;

defm : AtomWriteResPair<WriteDiv8,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 50, 68, [50,50], [68,68],  9,  9>;
defm : AtomWriteResPair<WriteDiv16,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 50, 50, [50,50], [50,50], 12, 12>;
defm : AtomWriteResPair<WriteDiv32,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 50, 50, [50,50], [50,50], 12, 12>;
defm : AtomWriteResPair<WriteDiv64,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],130,130,[130,130],[130,130], 38, 38>;
defm : AtomWriteResPair<WriteIDiv8,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 62, 62, [62,62], [62,62], 26, 26>;
defm : AtomWriteResPair<WriteIDiv16, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 62, 62, [62,62], [62,62], 29, 29>;
defm : AtomWriteResPair<WriteIDiv32, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 62, 62, [62,62], [62,62], 29, 29>;
defm : AtomWriteResPair<WriteIDiv64, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],130,130,[130,130],[130,130], 60, 60>;

defm : X86WriteResPairUnsupported<WriteCRC32>;

defm : AtomWriteResPair<WriteCMOV,  [AtomPort01], [AtomPort0]>;
defm : X86WriteRes<WriteFCMOV, [AtomPort01], 9, [9], 1>; // x87 conditional move.

def  : WriteRes<WriteSETCC, [AtomPort01]>;
def  : WriteRes<WriteSETCCStore, [AtomPort01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}
def  : WriteRes<WriteLAHFSAHF, [AtomPort01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}
defm : X86WriteRes<WriteBitTest,         [AtomPort1],  1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [AtomPort0],  1, [1], 1>;
defm : X86WriteRes<WriteBitTestRegLd,    [AtomPort01], 9, [9], 1>;
defm : X86WriteRes<WriteBitTestSet,      [AtomPort1],  1, [1], 1>;
//defm : X86WriteRes<WriteBitTestSetImmLd, [AtomPort1],  1, [1], 1>;
//defm : X86WriteRes<WriteBitTestSetRegLd, [AtomPort1],  1, [1], 1>;

// This is for simple LEAs with one or two input operands.
def : WriteRes<WriteLEA, [AtomPort1]>;

// Bit counts.
defm : AtomWriteResPair<WriteBSF, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 16, 16, [16,16], [16,16], 10, 10>;
defm : AtomWriteResPair<WriteBSR, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 16, 16, [16,16], [16,16], 10, 10>;
defm : X86WriteResPairUnsupported<WritePOPCNT>;
defm : X86WriteResPairUnsupported<WriteLZCNT>;
defm : X86WriteResPairUnsupported<WriteTZCNT>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : X86WriteResPairUnsupported<WriteBEXTR>;
defm : X86WriteResPairUnsupported<WriteBLS>;
defm : X86WriteResPairUnsupported<WriteBZHI>;

////////////////////////////////////////////////////////////////////////////////
// Integer shifts and rotates.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteShift,    [AtomPort0], [AtomPort0]>;
defm : AtomWriteResPair<WriteShiftCL,  [AtomPort0], [AtomPort0]>;
defm : AtomWriteResPair<WriteRotate,   [AtomPort0], [AtomPort0]>;
defm : AtomWriteResPair<WriteRotateCL, [AtomPort0], [AtomPort0]>;

defm : X86WriteRes<WriteSHDrri, [AtomPort01], 2, [2], 1>;
defm : X86WriteRes<WriteSHDrrcl,[AtomPort01], 2, [2], 1>;
defm : X86WriteRes<WriteSHDmri, [AtomPort01], 4, [4], 1>;
defm : X86WriteRes<WriteSHDmrcl,[AtomPort01], 4, [4], 1>;

////////////////////////////////////////////////////////////////////////////////
// Loads, stores, and moves, not folded with other operations.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteLoad,    [AtomPort0]>;
def : WriteRes<WriteStore,   [AtomPort0]>;
def : WriteRes<WriteStoreNT, [AtomPort0]>;
def : WriteRes<WriteMove,    [AtomPort01]>;
defm : X86WriteResUnsupported<WriteVecMaskedGatherWriteback>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

////////////////////////////////////////////////////////////////////////////////
// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteZero,  []>;

////////////////////////////////////////////////////////////////////////////////
// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteJump, [AtomPort1], [AtomPort1]>;

////////////////////////////////////////////////////////////////////////////////
// Special case scheduling classes.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteSystem,     [AtomPort01]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [AtomPort01]> { let Latency = 100; }
def : WriteRes<WriteFence,      [AtomPort0]>;

// Nops don't have dependencies, so there's no actual latency, but we set this
// to '1' to tell the scheduler that the nop uses an ALU slot for a cycle.
def : WriteRes<WriteNop, [AtomPort01]>;

////////////////////////////////////////////////////////////////////////////////
// Floating point. This covers both scalar and vector operations.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteRes<WriteFLD0,       [AtomPort01], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,       [AtomPort01], 6, [6], 1>;
def  : WriteRes<WriteFLoad,         [AtomPort0]>;
def  : WriteRes<WriteFLoadX,        [AtomPort0]>;
defm : X86WriteResUnsupported<WriteFLoadY>;
defm : X86WriteResUnsupported<WriteFMaskedLoad>;
defm : X86WriteResUnsupported<WriteFMaskedLoadY>;

def  : WriteRes<WriteFStore,        [AtomPort0]>;
def  : WriteRes<WriteFStoreX,       [AtomPort0]>;
defm : X86WriteResUnsupported<WriteFStoreY>;
def  : WriteRes<WriteFStoreNT,      [AtomPort0]>;
def  : WriteRes<WriteFStoreNTX,     [AtomPort0]>;
defm : X86WriteResUnsupported<WriteFStoreNTY>;
defm : X86WriteResUnsupported<WriteFMaskedStore32>;
defm : X86WriteResUnsupported<WriteFMaskedStore32Y>;
defm : X86WriteResUnsupported<WriteFMaskedStore64>;
defm : X86WriteResUnsupported<WriteFMaskedStore64Y>;

def  : WriteRes<WriteFMove,         [AtomPort01]>;
def  : WriteRes<WriteFMoveX,        [AtomPort01]>;
defm : X86WriteResUnsupported<WriteFMoveY>;
defm : X86WriteResUnsupported<WriteFMoveZ>;

defm : X86WriteRes<WriteEMMS,       [AtomPort01], 5, [5], 1>;

defm : AtomWriteResPair<WriteFAdd,           [AtomPort1], [AtomPort0,AtomPort1],  5,  5,  [1],  [1,1]>;
defm : AtomWriteResPair<WriteFAddX,          [AtomPort1], [AtomPort0,AtomPort1],  5,  5,  [1],  [1,1]>;
defm : X86WriteResPairUnsupported<WriteFAddY>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : AtomWriteResPair<WriteFAdd64,         [AtomPort1], [AtomPort0,AtomPort1],  5,  5,  [1],  [1,1]>;
defm : AtomWriteResPair<WriteFAdd64X,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [5,5],  [6,6], 3, 4>;
defm : X86WriteResPairUnsupported<WriteFAdd64Y>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;
defm : AtomWriteResPair<WriteFCmp,           [AtomPort1], [AtomPort0,AtomPort1],  5,  5,  [1],  [1,1]>;
defm : AtomWriteResPair<WriteFCmpX,          [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [5,5],  [6,6], 3, 4>;
defm : X86WriteResPairUnsupported<WriteFCmpY>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : AtomWriteResPair<WriteFCmp64,         [AtomPort1], [AtomPort0,AtomPort1],  5,  5,  [1],  [1,1]>;
defm : AtomWriteResPair<WriteFCmp64X,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [5,5],  [6,6], 3, 4>;
defm : X86WriteResPairUnsupported<WriteFCmp64Y>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;
defm : AtomWriteResPair<WriteFCom,           [AtomPort0],  [AtomPort0],  5,  5,  [5],  [5]>;
defm : AtomWriteResPair<WriteFComX,          [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  9, 10,  [9,9],[10,10], 4, 5>;
defm : AtomWriteResPair<WriteFMul,           [AtomPort0],  [AtomPort0],  4,  4,  [2],  [2]>;
defm : AtomWriteResPair<WriteFMulX,          [AtomPort0],  [AtomPort0],  5,  5,  [2],  [2]>;
defm : X86WriteResPairUnsupported<WriteFMulY>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : AtomWriteResPair<WriteFMul64,         [AtomPort0],  [AtomPort0],  5,  5,  [2],  [2]>;
defm : AtomWriteResPair<WriteFMul64X,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  9, 10,  [9,9],[10,10], 6, 7>;
defm : X86WriteResPairUnsupported<WriteFMul64Y>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;
defm : AtomWriteResPair<WriteFRcp,           [AtomPort0],  [AtomPort0],  4,  4,  [4],  [4]>;
defm : AtomWriteResPair<WriteFRcpX,          [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  9, 10,  [9,9], [10,10], 5, 6>;
defm : X86WriteResPairUnsupported<WriteFRcpY>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;
defm : AtomWriteResPair<WriteFRsqrt,         [AtomPort0],  [AtomPort0],  4,  4,  [4],  [4]>;
defm : AtomWriteResPair<WriteFRsqrtX,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  9, 10,  [9,9], [10,10], 5, 6>;
defm : X86WriteResPairUnsupported<WriteFRsqrtY>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;
defm : AtomWriteResPair<WriteFDiv,          [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 34, 34, [34,34], [34,34], 3, 4>;
defm : AtomWriteResPair<WriteFDivX,         [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 70, 70, [70,70], [70,70], 6, 7>;
defm : X86WriteResPairUnsupported<WriteFDivY>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : AtomWriteResPair<WriteFDiv64,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 62, 62,  [62,62],  [62,62], 3, 4>;
defm : AtomWriteResPair<WriteFDiv64X,       [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],125,125,[125,125],[125,125], 6, 7>;
defm : X86WriteResPairUnsupported<WriteFDiv64Y>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;
defm : AtomWriteResPair<WriteFSqrt,         [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 34, 34, [34,34], [34,34], 3, 4>;
defm : AtomWriteResPair<WriteFSqrtX,        [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 70, 70, [70,70], [70,70], 5, 6>;
defm : X86WriteResPairUnsupported<WriteFSqrtY>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : AtomWriteResPair<WriteFSqrt64,       [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 62, 62,  [62,62],  [62,62], 3, 4>;
defm : AtomWriteResPair<WriteFSqrt64X,      [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],125,125,[125,125],[125,125], 5, 6>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Y>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : AtomWriteResPair<WriteFSqrt80,        [AtomPort0],  [AtomPort0], 71, 71, [71], [71]>;
defm : AtomWriteResPair<WriteFSign,          [AtomPort1],  [AtomPort1]>;
defm : AtomWriteResPair<WriteFRnd,           [AtomPort0],  [AtomPort0],  5,  5,  [5],  [5]>;
defm : X86WriteResPairUnsupported<WriteFRndY>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : AtomWriteResPair<WriteFLogic,        [AtomPort01],  [AtomPort0]>;
defm : X86WriteResPairUnsupported<WriteFLogicY>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : AtomWriteResPair<WriteFTest,         [AtomPort01],  [AtomPort0]>;
defm : X86WriteResPairUnsupported<WriteFTestY>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : AtomWriteResPair<WriteFShuffle,       [AtomPort0],  [AtomPort0]>;
defm : X86WriteResPairUnsupported<WriteFShuffleY>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleY>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : X86WriteResPairUnsupported<WriteFMA>;
defm : X86WriteResPairUnsupported<WriteFMAX>;
defm : X86WriteResPairUnsupported<WriteFMAY>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;
defm : X86WriteResPairUnsupported<WriteDPPD>;
defm : X86WriteResPairUnsupported<WriteDPPS>;
defm : X86WriteResPairUnsupported<WriteDPPSY>;
defm : X86WriteResPairUnsupported<WriteFBlend>;
defm : X86WriteResPairUnsupported<WriteFBlendY>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : X86WriteResPairUnsupported<WriteFVarBlend>;
defm : X86WriteResPairUnsupported<WriteFVarBlendY>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;
defm : X86WriteResPairUnsupported<WriteFShuffle256>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle256>;

////////////////////////////////////////////////////////////////////////////////
// Conversions.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteCvtSS2I,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  8,  9,  [8,8],  [9,9], 3, 4>;
defm : AtomWriteResPair<WriteCvtPS2I,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 3, 4>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IY>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : AtomWriteResPair<WriteCvtSD2I,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  8,  9,  [8,8],[10,10], 3, 4>;
defm : AtomWriteResPair<WriteCvtPD2I,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  7,  8,  [7,7],  [8,8], 4, 5>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IY>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

defm : AtomWriteResPair<WriteCvtI2SS,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [6,6], 3, 1>;
defm : AtomWriteResPair<WriteCvtI2PS,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 3, 4>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSY>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : AtomWriteResPair<WriteCvtI2SD,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 3, 3>;
defm : AtomWriteResPair<WriteCvtI2PD,   [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  7,  8,  [6,6],  [7,7], 3, 4>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDY>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : AtomWriteResPair<WriteCvtSS2SD,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  6,  7,  [6,6],  [7,7], 3, 4>;
defm : AtomWriteResPair<WriteCvtPS2PD,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1],  7,  8,  [6,6],  [7,7], 4, 5>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDY>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;
defm : AtomWriteResPair<WriteCvtSD2SS,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 10, 11,[10,10],[12,12], 3, 4>;
defm : AtomWriteResPair<WriteCvtPD2PS,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 11, 12,[11,11],[12,12], 4, 5>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSY>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

defm : X86WriteResPairUnsupported<WriteCvtPH2PS>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSY>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>;
defm : X86WriteResUnsupported<WriteCvtPS2PH>;
defm : X86WriteResUnsupported<WriteCvtPS2PHSt>;
defm : X86WriteResUnsupported<WriteCvtPS2PHY>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
defm : X86WriteResUnsupported<WriteCvtPS2PHYSt>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

////////////////////////////////////////////////////////////////////////////////
// Vector integer operations.
////////////////////////////////////////////////////////////////////////////////

def  : WriteRes<WriteVecLoad,         [AtomPort0]>;
def  : WriteRes<WriteVecLoadX,        [AtomPort0]>;
defm : X86WriteResUnsupported<WriteVecLoadY>;
def  : WriteRes<WriteVecLoadNT,       [AtomPort0]>;
defm : X86WriteResUnsupported<WriteVecLoadNTY>;
defm : X86WriteResUnsupported<WriteVecMaskedLoad>;
defm : X86WriteResUnsupported<WriteVecMaskedLoadY>;

def  : WriteRes<WriteVecStore,        [AtomPort0]>;
def  : WriteRes<WriteVecStoreX,       [AtomPort0]>;
defm : X86WriteResUnsupported<WriteVecStoreY>;
def  : WriteRes<WriteVecStoreNT,      [AtomPort0]>;
defm : X86WriteResUnsupported<WriteVecStoreNTY>;
defm : X86WriteResUnsupported<WriteVecMaskedStore32>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64>;
defm : X86WriteResUnsupported<WriteVecMaskedStore32Y>;
defm : X86WriteResUnsupported<WriteVecMaskedStore64Y>;

def  : WriteRes<WriteVecMove,          [AtomPort0]>;
def  : WriteRes<WriteVecMoveX,        [AtomPort01]>;
defm : X86WriteResUnsupported<WriteVecMoveY>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;
defm : X86WriteRes<WriteVecMoveToGpr,   [AtomPort0], 3, [3], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr, [AtomPort0], 1, [1], 1>;

defm : AtomWriteResPair<WriteVecALU,       [AtomPort01],  [AtomPort0], 1, 1>;
defm : AtomWriteResPair<WriteVecALUX,      [AtomPort01],  [AtomPort0], 1, 1>;
defm : X86WriteResPairUnsupported<WriteVecALUY>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : AtomWriteResPair<WriteVecLogic,     [AtomPort01],  [AtomPort0], 1, 1>;
defm : AtomWriteResPair<WriteVecLogicX,    [AtomPort01],  [AtomPort0], 1, 1>;
defm : X86WriteResPairUnsupported<WriteVecLogicY>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : X86WriteResPairUnsupported<WriteVecTest>;
defm : X86WriteResPairUnsupported<WriteVecTestY>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : AtomWriteResPair<WriteVecShift,     [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 2, 3, [1,1], [2,2], 2, 3>;
defm : AtomWriteResPair<WriteVecShiftX,    [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 2, 3, [1,1], [2,2], 2, 3>;
defm : X86WriteResPairUnsupported<WriteVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;
defm : AtomWriteResPair<WriteVecShiftImm,   [AtomPort0],  [AtomPort0], 1, 1>;
defm : AtomWriteResPair<WriteVecShiftImmX,  [AtomPort0],  [AtomPort0], 1, 1>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmY>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : AtomWriteResPair<WriteVecIMul,       [AtomPort0],  [AtomPort0], 4, 4, [1], [1]>;
defm : AtomWriteResPair<WriteVecIMulX,      [AtomPort0],  [AtomPort0], 5, 5, [2], [2]>;
defm : X86WriteResPairUnsupported<WriteVecIMulY>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
defm : X86WriteResPairUnsupported<WritePMULLD>;
defm : X86WriteResPairUnsupported<WritePMULLDY>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : X86WriteResPairUnsupported<WritePHMINPOS>;
defm : X86WriteResPairUnsupported<WriteMPSAD>;
defm : X86WriteResPairUnsupported<WriteMPSADY>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;
defm : AtomWriteResPair<WritePSADBW,        [AtomPort0],  [AtomPort0], 4, 4, [1], [1]>;
defm : AtomWriteResPair<WritePSADBWX,       [AtomPort0],  [AtomPort0], 5, 5, [2], [2]>;
defm : X86WriteResPairUnsupported<WritePSADBWY>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : AtomWriteResPair<WriteShuffle,       [AtomPort0],  [AtomPort0], 1, 1>;
defm : AtomWriteResPair<WriteShuffleX,      [AtomPort0],  [AtomPort0], 1, 1>;
defm : X86WriteResPairUnsupported<WriteShuffleY>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : AtomWriteResPair<WriteVarShuffle,    [AtomPort0],  [AtomPort0], 1, 1>;
defm : AtomWriteResPair<WriteVarShuffleX,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 4, 5, [3,3], [4,4], 4, 5>;
defm : X86WriteResPairUnsupported<WriteVarShuffleY>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : X86WriteResPairUnsupported<WriteBlend>;
defm : X86WriteResPairUnsupported<WriteBlendY>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : X86WriteResPairUnsupported<WriteVarBlend>;
defm : X86WriteResPairUnsupported<WriteVarBlendY>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : X86WriteResPairUnsupported<WriteShuffle256>;
defm : X86WriteResPairUnsupported<WriteVPMOV256>;
defm : X86WriteResPairUnsupported<WriteVarShuffle256>;
defm : X86WriteResPairUnsupported<WriteVarVecShift>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;

////////////////////////////////////////////////////////////////////////////////
// Vector insert/extract operations.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteVecInsert,     [AtomPort0],  [AtomPort0], 1, 1>;
def  : WriteRes<WriteVecExtract,   [AtomPort0]>;
def  : WriteRes<WriteVecExtractSt, [AtomPort0]>;

////////////////////////////////////////////////////////////////////////////////
// SSE42 String instructions.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteResPairUnsupported<WritePCmpIStrI>;
defm : X86WriteResPairUnsupported<WritePCmpIStrM>;
defm : X86WriteResPairUnsupported<WritePCmpEStrI>;
defm : X86WriteResPairUnsupported<WritePCmpEStrM>;

////////////////////////////////////////////////////////////////////////////////
// MOVMSK Instructions.
////////////////////////////////////////////////////////////////////////////////

def  : WriteRes<WriteFMOVMSK,    [AtomPort0]> { let Latency = 3; let ReleaseAtCycles = [3]; }
def  : WriteRes<WriteVecMOVMSK,  [AtomPort0]> { let Latency = 3; let ReleaseAtCycles = [3]; }
defm : X86WriteResUnsupported<WriteVecMOVMSKY>;
def  : WriteRes<WriteMMXMOVMSK,  [AtomPort0]> { let Latency = 3; let ReleaseAtCycles = [3]; }

////////////////////////////////////////////////////////////////////////////////
// AES instructions.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteResPairUnsupported<WriteAESIMC>;
defm : X86WriteResPairUnsupported<WriteAESKeyGen>;
defm : X86WriteResPairUnsupported<WriteAESDecEnc>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : AtomWriteResPair<WriteFHAdd,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 8, 9, [8,8], [9,9], 5, 6>;
defm : X86WriteResPairUnsupported<WriteFHAddY>;
defm : AtomWriteResPair<WritePHAdd,  [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 3, 4, [3,3], [4,4], 3, 4>;
defm : AtomWriteResPair<WritePHAddX, [AtomPort0,AtomPort1], [AtomPort0,AtomPort1], 7, 8, [7,7], [8,8], 3, 4>;
defm : X86WriteResPairUnsupported<WritePHAddY>;

////////////////////////////////////////////////////////////////////////////////
// Carry-less multiplication instructions.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteResPairUnsupported<WriteCLMul>;

////////////////////////////////////////////////////////////////////////////////
// Load/store MXCSR.
////////////////////////////////////////////////////////////////////////////////

defm : X86WriteRes<WriteLDMXCSR, [AtomPort0,AtomPort1],  5,   [5,5], 4>;
defm : X86WriteRes<WriteSTMXCSR, [AtomPort0,AtomPort1], 15, [15,15], 4>;

////////////////////////////////////////////////////////////////////////////////
// Special Cases.
////////////////////////////////////////////////////////////////////////////////

// Port0
def AtomWrite0_1 : SchedWriteRes<[AtomPort0]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
}
def : InstRW<[AtomWrite0_1], (instrs XAM_F, LD_Frr,
                                     MOVSX64rr32)>;
def : SchedAlias<WriteALURMW, AtomWrite0_1>;
def : SchedAlias<WriteADCRMW, AtomWrite0_1>;
def : InstRW<[AtomWrite0_1], (instregex "(RCL|RCR|ROL|ROR|SAR|SHL|SHR)(8|16|32|64)m",
                                        "MOV(S|Z)X(32|64)rr(8|8_NOREX|16)")>;

// Port1
def AtomWrite1_1 : SchedWriteRes<[AtomPort1]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
}
def : InstRW<[AtomWrite1_1], (instrs FCOMPP)>;
def : InstRW<[AtomWrite1_1], (instregex "UCOM_F(P|PP)?r")>;

def AtomWrite1_5 : SchedWriteRes<[AtomPort1]> {
  let Latency = 5;
  let ReleaseAtCycles = [5];
}
def : InstRW<[AtomWrite1_5], (instrs MMX_CVTPI2PSrr, MMX_CVTPI2PSrm,
                                     MMX_CVTPS2PIrr, MMX_CVTTPS2PIrr)>;

// Port0 and Port1
def AtomWrite0_1_1 : SchedWriteRes<[AtomPort0, AtomPort1]> {
  let Latency = 1;
  let ReleaseAtCycles = [1, 1];
}
def : InstRW<[AtomWrite0_1_1], (instrs POP32r, POP64r,
                                       POP16rmr, POP32rmr, POP64rmr,
                                       PUSH16r, PUSH32r, PUSH64r,
                                       PUSH16i, PUSH32i,
                                       PUSH16rmr, PUSH32rmr, PUSH64rmr,
                                       PUSH16i8, PUSH32i8, PUSH64i8, PUSH64i32,
                                       XCH_F)>;
def : InstRW<[AtomWrite0_1_1], (instregex "RETI(16|32|64)$",
                                          "IRET(16|32|64)?")>;

def AtomWrite0_1_5 : SchedWriteRes<[AtomPort0, AtomPort1]> {
  let Latency = 5;
  let ReleaseAtCycles = [5, 5];
}
def : InstRW<[AtomWrite0_1_5], (instrs MMX_CVTPS2PIrm, MMX_CVTTPS2PIrm)>;
def : InstRW<[AtomWrite0_1_5], (instregex "ILD_F(16|32|64)")>;

def AtomWrite0_1_7 : SchedWriteRes<[AtomPort0,AtomPort1]> {
  let Latency = 7;
  let ReleaseAtCycles = [6,6];
}
def : InstRW<[AtomWrite0_1_7], (instregex "CVTSI642SDrm(_Int)?")>;

def AtomWrite0_1_7_4 : SchedWriteRes<[AtomPort0,AtomPort1]> {
  let Latency = 7;
  let ReleaseAtCycles = [8,8];
  let NumMicroOps = 4;
}
def : InstRW<[AtomWrite0_1_7_4], (instregex "CVTSI642SSrr(_Int)?")>;

def AtomWrite0_1_8_4 : SchedWriteRes<[AtomPort0,AtomPort1]> {
  let Latency = 8;
  let ReleaseAtCycles = [8,8];
  let NumMicroOps = 4;
}
def : InstRW<[AtomWrite0_1_7_4], (instregex "CVTSI642SSrm(_Int)?")>;

def AtomWrite0_1_9 : SchedWriteRes<[AtomPort0,AtomPort1]> {
  let Latency = 9;
  let ReleaseAtCycles = [9,9];
  let NumMicroOps = 4;
}
def : InstRW<[AtomWrite0_1_9], (instregex "CVT(T)?SS2SI64rr(_Int)?")>;

def AtomWrite0_1_10 : SchedWriteRes<[AtomPort0,AtomPort1]> {
  let Latency = 10;
  let ReleaseAtCycles = [11,11];
  let NumMicroOps = 5;
}
def : InstRW<[AtomWrite0_1_10], (instregex "CVT(T)?SS2SI64rm(_Int)?")>;

// Port0 or Port1
def AtomWrite01_1 : SchedWriteRes<[AtomPort01]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
}
def : InstRW<[AtomWrite01_1], (instrs FDECSTP, FFREE, FFREEP, FINCSTP, WAIT,
                                      LFENCE,
                                      STOSB, STOSL, STOSQ, STOSW,
                                      MOVSSrr, MOVSSrr_REV)>;

def AtomWrite01_2 : SchedWriteRes<[AtomPort01]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[AtomWrite01_2], (instrs LEAVE, LEAVE64, POP16r,
                                      PUSH16rmm, PUSH32rmm, PUSH64rmm,
                                      LODSB, LODSL, LODSQ, LODSW,
                                      SCASB, SCASL, SCASQ, SCASW)>;
def : InstRW<[AtomWrite01_2], (instregex "PUSH(CS|DS|ES|FS|GS|SS)(16|32|64)",
                                         "(ST|ISTT)_F(P)?(16|32|64)?(m|rr)",
                                         "MMX_P(ADD|SUB)Qrr",
                                         "MOV(S|Z)X16rr8",
                                         "MOV(UPS|UPD|DQU)mr",
                                         "MASKMOVDQU(64)?",
                                         "P(ADD|SUB)Qrr")>;
def : SchedAlias<WriteBitTestSetImmRMW, AtomWrite01_2>;

def AtomWrite01_3 : SchedWriteRes<[AtomPort01]> {
  let Latency = 3;
  let ReleaseAtCycles = [3];
}
def : InstRW<[AtomWrite01_3], (instrs CLD, LDDQUrm,
                                      CMPSB, CMPSL, CMPSQ, CMPSW,
                                      MOVSB, MOVSL, MOVSQ, MOVSW,
                                      POP16rmm, POP32rmm, POP64rmm)>;
def : InstRW<[AtomWrite01_3], (instregex "XADD(8|16|32|64)rm",
                                         "XCHG(8|16|32|64)rm",
                                         "PH(ADD|SUB)Drr",
                                         "MOV(S|Z)X16rm8",
                                         "MMX_P(ADD|SUB)Qrm",
                                         "MOV(UPS|UPD|DQU)rm",
                                         "P(ADD|SUB)Qrm")>;

def AtomWrite01_4 : SchedWriteRes<[AtomPort01]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
}
def : InstRW<[AtomWrite01_4], (instrs CBW, CWD, CWDE, CDQ, CDQE, CQO,
                                      JCXZ, JECXZ, JRCXZ,
                                      LD_F80m)>;
def : InstRW<[AtomWrite01_4], (instregex "PH(ADD|SUB)Drm",
                                         "(MMX_)?PEXTRWrr(_REV)?")>;

def AtomWrite01_5 : SchedWriteRes<[AtomPort01]> {
  let Latency = 5;
  let ReleaseAtCycles = [5];
}
def : InstRW<[AtomWrite01_5], (instrs FLDCW16m, ST_FP80m)>;
def : InstRW<[AtomWrite01_5], (instregex "MMX_PH(ADD|SUB)S?Wrr")>;

def AtomWrite01_6 : SchedWriteRes<[AtomPort01]> {
  let Latency = 6;
  let ReleaseAtCycles = [6];
}
def : InstRW<[AtomWrite01_6], (instrs CMPXCHG8rm, INTO, XLAT,
                                      SHLD16rrCL, SHRD16rrCL,
                                      SHLD16rri8, SHRD16rri8,
                                      SHLD16mrCL, SHRD16mrCL,
                                      SHLD16mri8, SHRD16mri8)>;
def : InstRW<[AtomWrite01_6], (instregex "IST_F(P)?(16|32|64)?m",
                                         "MMX_PH(ADD|SUB)S?Wrm")>;

def AtomWrite01_7 : SchedWriteRes<[AtomPort01]> {
  let Latency = 7;
  let ReleaseAtCycles = [7];
}
def : InstRW<[AtomWrite01_7], (instrs AAD8i8)>;

def AtomWrite01_8 : SchedWriteRes<[AtomPort01]> {
  let Latency = 8;
  let ReleaseAtCycles = [8];
}
def : InstRW<[AtomWrite01_8], (instrs LOOPE,
                                      PUSHA16, PUSHA32,
                                      SHLD64rrCL, SHRD64rrCL,
                                      FNSTCW16m)>;

def AtomWrite01_9 : SchedWriteRes<[AtomPort01]> {
  let Latency = 9;
  let ReleaseAtCycles = [9];
}
def : InstRW<[AtomWrite01_9], (instrs POPA16, POPA32,
                                      PUSHF16, PUSHF32, PUSHF64,
                                      SHLD64mrCL, SHRD64mrCL,
                                      SHLD64mri8, SHRD64mri8,
                                      SHLD64rri8, SHRD64rri8,
                                      CMPXCHG8rr)>;
def : InstRW<[AtomWrite01_9], (instregex "(U)?COM_FI", "TST_F")>;

def AtomWrite01_10 : SchedWriteRes<[AtomPort01]> {
  let Latency = 10;
  let ReleaseAtCycles = [10];
}
def : SchedAlias<WriteFLDC, AtomWrite01_10>;

def AtomWrite01_11 : SchedWriteRes<[AtomPort01]> {
  let Latency = 11;
  let ReleaseAtCycles = [11];
}
def : InstRW<[AtomWrite01_11], (instrs BOUNDS16rm, BOUNDS32rm)>;
def : SchedAlias<WriteBitTestSetRegRMW, AtomWrite01_11>;

def AtomWrite01_13 : SchedWriteRes<[AtomPort01]> {
  let Latency = 13;
  let ReleaseAtCycles = [13];
}
def : InstRW<[AtomWrite01_13], (instrs AAA, AAS)>;

def AtomWrite01_14 : SchedWriteRes<[AtomPort01]> {
  let Latency = 14;
  let ReleaseAtCycles = [14];
}
def : InstRW<[AtomWrite01_14], (instrs CMPXCHG16rm, CMPXCHG32rm, CMPXCHG64rm)>;

def AtomWrite01_17 : SchedWriteRes<[AtomPort01]> {
  let Latency = 17;
  let ReleaseAtCycles = [17];
}
def : InstRW<[AtomWrite01_17], (instrs LOOPNE, PAUSE)>;

def AtomWrite01_18 : SchedWriteRes<[AtomPort01]> {
  let Latency = 18;
  let ReleaseAtCycles = [18];
}
def : InstRW<[AtomWrite01_18], (instrs CMPXCHG8B, DAA, LOOP)>;

def AtomWrite01_20 : SchedWriteRes<[AtomPort01]> {
  let Latency = 20;
  let ReleaseAtCycles = [20];
}
def : InstRW<[AtomWrite01_20], (instrs DAS)>;

def AtomWrite01_21 : SchedWriteRes<[AtomPort01]> {
  let Latency = 21;
  let ReleaseAtCycles = [21];
}
def : InstRW<[AtomWrite01_21], (instrs AAM8i8, STD)>;

def AtomWrite01_22 : SchedWriteRes<[AtomPort01]> {
  let Latency = 22;
  let ReleaseAtCycles = [22];
}
def : InstRW<[AtomWrite01_22], (instrs CMPXCHG16B)>;

def AtomWrite01_23 : SchedWriteRes<[AtomPort01]> {
  let Latency = 23;
  let ReleaseAtCycles = [23];
}
def : InstRW<[AtomWrite01_23], (instrs ARPL16mr, ARPL16rr)>;

def AtomWrite01_25 : SchedWriteRes<[AtomPort01]> {
  let Latency = 25;
  let ReleaseAtCycles = [25];
}
def : InstRW<[AtomWrite01_25], (instrs FNCLEX, FXTRACT)>;

def AtomWrite01_26 : SchedWriteRes<[AtomPort01]> {
  let Latency = 26;
  let ReleaseAtCycles = [26];
}
def : InstRW<[AtomWrite01_26], (instrs POPF32, POPF64)>;

def AtomWrite01_29 : SchedWriteRes<[AtomPort01]> {
  let Latency = 29;
  let ReleaseAtCycles = [29];
}
def : InstRW<[AtomWrite01_29], (instregex "POP(DS|ES|FS|GS)(16|32|64)")>;

def AtomWrite01_30 : SchedWriteRes<[AtomPort01]> {
  let Latency = 30;
  let ReleaseAtCycles = [30];
}
def : InstRW<[AtomWrite01_30], (instrs RDTSC, RDTSCP)>;

def AtomWrite01_32 : SchedWriteRes<[AtomPort01]> {
  let Latency = 32;
  let ReleaseAtCycles = [32];
}
def : InstRW<[AtomWrite01_32], (instrs ENTER, POPF16)>;

def AtomWrite01_45 : SchedWriteRes<[AtomPort01]> {
  let Latency = 45;
  let ReleaseAtCycles = [45];
}
def : InstRW<[AtomWrite01_45], (instrs MONITOR32rrr, MONITOR64rrr)>;

def AtomWrite01_46 : SchedWriteRes<[AtomPort01]> {
  let Latency = 46;
  let ReleaseAtCycles = [46];
}
def : InstRW<[AtomWrite01_46], (instrs FRNDINT, MWAITrr, RDPMC)>;

def AtomWrite01_48 : SchedWriteRes<[AtomPort01]> {
  let Latency = 48;
  let ReleaseAtCycles = [48];
}
def : InstRW<[AtomWrite01_48], (instrs POPSS16, POPSS32)>;

def AtomWrite01_55 : SchedWriteRes<[AtomPort01]> {
  let Latency = 55;
  let ReleaseAtCycles = [55];
}
def : InstRW<[AtomWrite01_55], (instrs FPREM)>;

def AtomWrite01_59 : SchedWriteRes<[AtomPort01]> {
  let Latency = 59;
  let ReleaseAtCycles = [59];
}
def : InstRW<[AtomWrite01_59], (instrs INSB, INSL, INSW)>;

def AtomWrite01_63 : SchedWriteRes<[AtomPort01]> {
  let Latency = 63;
  let ReleaseAtCycles = [63];
}
def : InstRW<[AtomWrite01_63], (instrs FNINIT)>;

def AtomWrite01_68 : SchedWriteRes<[AtomPort01]> {
  let Latency = 68;
  let ReleaseAtCycles = [68];
}
def : InstRW<[AtomWrite01_68], (instrs OUT8rr, OUT16rr, OUT32rr)>;

def AtomWrite01_71 : SchedWriteRes<[AtomPort01]> {
  let Latency = 71;
  let ReleaseAtCycles = [71];
}
def : InstRW<[AtomWrite01_71], (instrs FPREM1,
                                       INVLPG, INVLPGA32, INVLPGA64)>;

def AtomWrite01_72 : SchedWriteRes<[AtomPort01]> {
  let Latency = 72;
  let ReleaseAtCycles = [72];
}
def : InstRW<[AtomWrite01_72], (instrs OUT8ir, OUT16ir, OUT32ir)>;

def AtomWrite01_74 : SchedWriteRes<[AtomPort01]> {
  let Latency = 74;
  let ReleaseAtCycles = [74];
}
def : InstRW<[AtomWrite01_74], (instrs OUTSB, OUTSL, OUTSW)>;

def AtomWrite01_77 : SchedWriteRes<[AtomPort01]> {
  let Latency = 77;
  let ReleaseAtCycles = [77];
}
def : InstRW<[AtomWrite01_77], (instrs FSCALE)>;

def AtomWrite01_78 : SchedWriteRes<[AtomPort01]> {
  let Latency = 78;
  let ReleaseAtCycles = [78];
}
def : InstRW<[AtomWrite01_78], (instrs RDMSR)>;

def AtomWrite01_79 : SchedWriteRes<[AtomPort01]> {
  let Latency = 79;
  let ReleaseAtCycles = [79];
}
def : InstRW<[AtomWrite01_79], (instregex "RET(16|32|64)?$",
                                          "LRETI?(16|32|64)")>;

def AtomWrite01_92 : SchedWriteRes<[AtomPort01]> {
  let Latency = 92;
  let ReleaseAtCycles = [92];
}
def : InstRW<[AtomWrite01_92], (instrs IN8ri, IN16ri, IN32ri)>;

def AtomWrite01_94 : SchedWriteRes<[AtomPort01]> {
  let Latency = 94;
  let ReleaseAtCycles = [94];
}
def : InstRW<[AtomWrite01_94], (instrs IN8rr, IN16rr, IN32rr)>;

def AtomWrite01_99 : SchedWriteRes<[AtomPort01]> {
  let Latency = 99;
  let ReleaseAtCycles = [99];
}
def : InstRW<[AtomWrite01_99], (instrs F2XM1)>;

def AtomWrite01_121 : SchedWriteRes<[AtomPort01]> {
  let Latency = 121;
  let ReleaseAtCycles = [121];
}
def : InstRW<[AtomWrite01_121], (instrs CPUID)>;

def AtomWrite01_127 : SchedWriteRes<[AtomPort01]> {
  let Latency = 127;
  let ReleaseAtCycles = [127];
}
def : InstRW<[AtomWrite01_127], (instrs INT)>;

def AtomWrite01_130 : SchedWriteRes<[AtomPort01]> {
  let Latency = 130;
  let ReleaseAtCycles = [130];
}
def : InstRW<[AtomWrite01_130], (instrs INT3)>;

def AtomWrite01_140 : SchedWriteRes<[AtomPort01]> {
  let Latency = 140;
  let ReleaseAtCycles = [140];
}
def : InstRW<[AtomWrite01_140], (instrs FXSAVE, FXSAVE64)>;

def AtomWrite01_141 : SchedWriteRes<[AtomPort01]> {
  let Latency = 141;
  let ReleaseAtCycles = [141];
}
def : InstRW<[AtomWrite01_141], (instrs FXRSTOR, FXRSTOR64)>;

def AtomWrite01_146 : SchedWriteRes<[AtomPort01]> {
  let Latency = 146;
  let ReleaseAtCycles = [146];
}
def : InstRW<[AtomWrite01_146], (instrs FYL2X)>;

def AtomWrite01_147 : SchedWriteRes<[AtomPort01]> {
  let Latency = 147;
  let ReleaseAtCycles = [147];
}
def : InstRW<[AtomWrite01_147], (instrs FYL2XP1)>;

def AtomWrite01_168 : SchedWriteRes<[AtomPort01]> {
  let Latency = 168;
  let ReleaseAtCycles = [168];
}
def : InstRW<[AtomWrite01_168], (instrs FPTAN)>;

def AtomWrite01_174 : SchedWriteRes<[AtomPort01]> {
  let Latency = 174;
  let ReleaseAtCycles = [174];
}
def : InstRW<[AtomWrite01_174], (instrs FSINCOS, FSIN, FCOS)>;

def AtomWrite01_183 : SchedWriteRes<[AtomPort01]> {
  let Latency = 183;
  let ReleaseAtCycles = [183];
}
def : InstRW<[AtomWrite01_183], (instrs FPATAN)>;

def AtomWrite01_202 : SchedWriteRes<[AtomPort01]> {
  let Latency = 202;
  let ReleaseAtCycles = [202];
}
def : InstRW<[AtomWrite01_202], (instrs WRMSR)>;

} // SchedModel
