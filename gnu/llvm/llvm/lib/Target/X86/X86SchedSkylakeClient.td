//=- X86SchedSkylake.td - X86 Skylake Client Scheduling ------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Skylake Client to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def SkylakeClientModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and SKylake can
  // decode 6 instructions per cycle.
  let IssueWidth = 6;
  let MicroOpBufferSize = 224; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 14;

  // Based on the LSD (loop-stream detector) queue size and benchmarking data.
  let LoopMicroOpBufferSize = 50;

  // This flag is set to allow the scheduler to assign a default model to
  // unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = SkylakeClientModel in {

// Skylake Client can issue micro-ops to 8 different ports in one cycle.

// Ports 0, 1, 5, and 6 handle all computation.
// Port 4 gets the data half of stores. Store data can be available later than
// the store address, but since we don't model the latency of stores, we can
// ignore that.
// Ports 2 and 3 are identical. They handle loads and the address half of
// stores. Port 7 can handle address calculations.
def SKLPort0 : ProcResource<1>;
def SKLPort1 : ProcResource<1>;
def SKLPort2 : ProcResource<1>;
def SKLPort3 : ProcResource<1>;
def SKLPort4 : ProcResource<1>;
def SKLPort5 : ProcResource<1>;
def SKLPort6 : ProcResource<1>;
def SKLPort7 : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def SKLPort01  : ProcResGroup<[SKLPort0, SKLPort1]>;
def SKLPort23  : ProcResGroup<[SKLPort2, SKLPort3]>;
def SKLPort237 : ProcResGroup<[SKLPort2, SKLPort3, SKLPort7]>;
def SKLPort04  : ProcResGroup<[SKLPort0, SKLPort4]>;
def SKLPort05  : ProcResGroup<[SKLPort0, SKLPort5]>;
def SKLPort06  : ProcResGroup<[SKLPort0, SKLPort6]>;
def SKLPort15  : ProcResGroup<[SKLPort1, SKLPort5]>;
def SKLPort16  : ProcResGroup<[SKLPort1, SKLPort6]>;
def SKLPort56  : ProcResGroup<[SKLPort5, SKLPort6]>;
def SKLPort015 : ProcResGroup<[SKLPort0, SKLPort1, SKLPort5]>;
def SKLPort056 : ProcResGroup<[SKLPort0, SKLPort5, SKLPort6]>;
def SKLPort0156: ProcResGroup<[SKLPort0, SKLPort1, SKLPort5, SKLPort6]>;

def SKLDivider : ProcResource<1>; // Integer division issued on port 0.
// FP division and sqrt on port 0.
def SKLFPDivider : ProcResource<1>;

// 60 Entry Unified Scheduler
def SKLPortAny : ProcResGroup<[SKLPort0, SKLPort1, SKLPort2, SKLPort3, SKLPort4,
                              SKLPort5, SKLPort6, SKLPort7]> {
  let BufferSize=60;
}

// Integer loads are 5 cycles, so ReadAfterLd registers needn't be available until 5
// cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 5>;

// Vector loads are 5/6/7 cycles, so ReadAfterVec*Ld registers needn't be available
// until 5/6/7 cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 5>;
def : ReadAdvance<ReadAfterVecXLd, 6>;
def : ReadAdvance<ReadAfterVecYLd, 7>;

def : ReadAdvance<ReadInt2Fpu, 0>;

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when queued in the reservation station.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass SKLWriteResPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [1], int UOps = 1,
                          int LoadLat = 5, int LoadUOps = 1> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on port 2/3 and adds LoadLat cycles to
  // the latency (default = 5).
  def : WriteRes<SchedRW.Folded, !listconcat([SKLPort23], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ReleaseAtCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// A folded store needs a cycle on port 4 for the store data, and an extra port
// 2/3/7 cycle to recompute the address.
def : WriteRes<WriteRMW, [SKLPort237,SKLPort4]>;

// Arithmetic.
defm : SKLWriteResPair<WriteALU,    [SKLPort0156], 1>; // Simple integer ALU op.
defm : SKLWriteResPair<WriteADC,    [SKLPort06],   1>; // Integer ALU + flags op.

// Integer multiplication.
defm : SKLWriteResPair<WriteIMul8,     [SKLPort1],   3>;
defm : SKLWriteResPair<WriteIMul16,    [SKLPort1,SKLPort06,SKLPort0156], 4, [1,1,2], 4>;
defm : X86WriteRes<WriteIMul16Imm,     [SKLPort1,SKLPort0156], 4, [1,1], 2>;
defm : X86WriteRes<WriteIMul16ImmLd,   [SKLPort1,SKLPort0156,SKLPort23], 8, [1,1,1], 3>;
defm : SKLWriteResPair<WriteIMul16Reg, [SKLPort1],   3>;
defm : SKLWriteResPair<WriteIMul32,    [SKLPort1,SKLPort06,SKLPort0156], 4, [1,1,1], 3>;
defm : SKLWriteResPair<WriteMULX32,    [SKLPort1,SKLPort06,SKLPort0156], 3, [1,1,1], 3>;
defm : SKLWriteResPair<WriteIMul32Imm, [SKLPort1],   3>;
defm : SKLWriteResPair<WriteIMul32Reg, [SKLPort1],   3>;
defm : SKLWriteResPair<WriteIMul64,    [SKLPort1,SKLPort5], 4, [1,1], 2>;
defm : SKLWriteResPair<WriteMULX64,    [SKLPort1,SKLPort5], 3, [1,1], 2>;
defm : SKLWriteResPair<WriteIMul64Imm, [SKLPort1],   3>;
defm : SKLWriteResPair<WriteIMul64Reg, [SKLPort1],   3>;
def SKLWriteIMulH : WriteRes<WriteIMulH, []> { let Latency = 4; }
def  : WriteRes<WriteIMulHLd, []> {
  let Latency = !add(SKLWriteIMulH.Latency, SkylakeClientModel.LoadLatency);
}

defm : X86WriteRes<WriteBSWAP32,    [SKLPort15], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64,    [SKLPort06, SKLPort15], 2, [1,1], 2>;
defm : X86WriteRes<WriteCMPXCHG,[SKLPort06, SKLPort0156], 5, [2,3], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW,[SKLPort23,SKLPort06,SKLPort0156,SKLPort237,SKLPort4], 8, [1,2,1,1,1], 6>;
defm : X86WriteRes<WriteXCHG, [SKLPort0156], 2, [3], 3>;

// TODO: Why isn't the SKLDivider used?
defm : SKLWriteResPair<WriteDiv8,   [SKLPort0,SKLDivider], 25, [1,10], 1, 4>;
defm : X86WriteRes<WriteDiv16,      [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort05,SKLPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv32,      [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort05,SKLPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv64,      [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort05,SKLPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv16Ld,    [SKLPort0,SKLPort23,SKLDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv32Ld,    [SKLPort0,SKLPort23,SKLDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv64Ld,    [SKLPort0,SKLPort23,SKLDivider], 29, [1,1,10], 2>;

defm : X86WriteRes<WriteIDiv8,    [SKLPort0,SKLDivider], 25, [1,10], 1>;
defm : X86WriteRes<WriteIDiv16,   [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort06,SKLPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv32,   [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort06,SKLPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv64,   [SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort06,SKLPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv8Ld,  [SKLPort0,SKLPort5,SKLPort23,SKLPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv16Ld, [SKLPort0,SKLPort5,SKLPort23,SKLPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv32Ld, [SKLPort0,SKLPort5,SKLPort23,SKLPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv64Ld, [SKLPort0,SKLPort5,SKLPort23,SKLPort0156], 28, [2,4,1,1], 8>;

defm : SKLWriteResPair<WriteCRC32, [SKLPort1], 3>;

def : WriteRes<WriteLEA, [SKLPort15]>; // LEA instructions can't fold loads.

defm : SKLWriteResPair<WriteCMOV,  [SKLPort06], 1, [1], 1>; // Conditional move.
defm : X86WriteRes<WriteFCMOV, [SKLPort1], 3, [1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [SKLPort06]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [SKLPort06,SKLPort4,SKLPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

defm : X86WriteRes<WriteLAHFSAHF,        [SKLPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [SKLPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [SKLPort06,SKLPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [SKLPort0156,SKLPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [SKLPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [SKLPort06,SKLPort23], 5, [1,1], 3>;
defm : X86WriteRes<WriteBitTestSetRegLd, [SKLPort0156,SKLPort23], 5, [1,1], 2>;

// Bit counts.
defm : SKLWriteResPair<WriteBSF, [SKLPort1], 3>;
defm : SKLWriteResPair<WriteBSR, [SKLPort1], 3>;
defm : SKLWriteResPair<WriteLZCNT,          [SKLPort1], 3>;
defm : SKLWriteResPair<WriteTZCNT,          [SKLPort1], 3>;
defm : SKLWriteResPair<WritePOPCNT,         [SKLPort1], 3>;

// Integer shifts and rotates.
defm : SKLWriteResPair<WriteShift,    [SKLPort06],  1>;
defm : SKLWriteResPair<WriteShiftCL,  [SKLPort06],  3, [3], 3>;
defm : SKLWriteResPair<WriteRotate,   [SKLPort06],  1, [1], 1>;
defm : SKLWriteResPair<WriteRotateCL, [SKLPort06],  3, [3], 3>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [SKLPort1], 3, [1], 1>;
defm : X86WriteRes<WriteSHDrrcl,[SKLPort1,SKLPort06,SKLPort0156], 6, [1, 2, 1], 4>;
defm : X86WriteRes<WriteSHDmri, [SKLPort1,SKLPort23,SKLPort237,SKLPort0156], 9, [1, 1, 1, 1], 4>;
defm : X86WriteRes<WriteSHDmrcl,[SKLPort1,SKLPort23,SKLPort237,SKLPort06,SKLPort0156], 11, [1, 1, 1, 2, 1], 6>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : SKLWriteResPair<WriteBEXTR, [SKLPort06,SKLPort15], 2, [1,1], 2>;
defm : SKLWriteResPair<WriteBLS,   [SKLPort15], 1>;
defm : SKLWriteResPair<WriteBZHI,  [SKLPort15], 1>;

// Loads, stores, and moves, not folded with other operations.
defm : X86WriteRes<WriteLoad,    [SKLPort23], 5, [1], 1>;
defm : X86WriteRes<WriteStore,   [SKLPort237, SKLPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteStoreNT, [SKLPort237, SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteMove,    [SKLPort0156], 1, [1], 1>;

// Model the effect of clobbering the read-write mask operand of the GATHER operation.
// Does not cost anything by itself, only has latency, matching that of the WriteLoad,
defm : X86WriteRes<WriteVecMaskedGatherWriteback, [], 5, [], 0>;

// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
def : WriteRes<WriteZero,  []>;

// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
defm : SKLWriteResPair<WriteJump,  [SKLPort06],   1>;

// Floating point. This covers both scalar and vector operations.
defm : X86WriteRes<WriteFLD0,          [SKLPort05], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,          [SKLPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLDC,          [SKLPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLoad,         [SKLPort23], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [SKLPort23], 6, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [SKLPort23], 7, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [SKLPort23,SKLPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedLoadY,  [SKLPort23,SKLPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteFStore,        [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreX,       [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreY,       [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNT,      [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTX,     [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTY,     [SKLPort237,SKLPort4], 1, [1,1], 2>;

defm : X86WriteRes<WriteFMaskedStore32,  [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore32Y, [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore64,  [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore64Y, [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;

defm : X86WriteRes<WriteFMove,         [SKLPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [SKLPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [SKLPort015], 1, [1], 1>;
defm : X86WriteResUnsupported<WriteFMoveZ>;
defm : X86WriteRes<WriteEMMS,          [SKLPort05,SKLPort0156], 10, [9,1], 10>;

defm : SKLWriteResPair<WriteFAdd,     [SKLPort01],  4, [1], 1, 5>; // Floating point add/sub.
defm : SKLWriteResPair<WriteFAddX,    [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFAddY,    [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : SKLWriteResPair<WriteFAdd64,   [SKLPort01],  4, [1], 1, 5>; // Floating point double add/sub.
defm : SKLWriteResPair<WriteFAdd64X,  [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFAdd64Y,  [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;

defm : SKLWriteResPair<WriteFCmp,     [SKLPort01],  4, [1], 1, 5>; // Floating point compare.
defm : SKLWriteResPair<WriteFCmpX,    [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFCmpY,    [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : SKLWriteResPair<WriteFCmp64,   [SKLPort01],  4, [1], 1, 5>; // Floating point double compare.
defm : SKLWriteResPair<WriteFCmp64X,  [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFCmp64Y,  [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;

defm : SKLWriteResPair<WriteFCom,      [SKLPort0],  2>; // Floating point compare to flags (X87).
defm : SKLWriteResPair<WriteFComX,     [SKLPort0],  2>; // Floating point compare to flags (SSE).

defm : SKLWriteResPair<WriteFMul,     [SKLPort01],  4, [1], 1, 5>; // Floating point multiplication.
defm : SKLWriteResPair<WriteFMulX,    [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFMulY,    [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : SKLWriteResPair<WriteFMul64,   [SKLPort01],  4, [1], 1, 5>; // Floating point double multiplication.
defm : SKLWriteResPair<WriteFMul64X,  [SKLPort01],  4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFMul64Y,  [SKLPort01],  4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;

defm : SKLWriteResPair<WriteFDiv,     [SKLPort0,SKLFPDivider], 11, [1,3], 1, 5>; // Floating point division.
defm : SKLWriteResPair<WriteFDivX,    [SKLPort0,SKLFPDivider], 11, [1,3], 1, 6>;
defm : SKLWriteResPair<WriteFDivY,    [SKLPort0,SKLFPDivider], 11, [1,5], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : SKLWriteResPair<WriteFDiv64,   [SKLPort0,SKLFPDivider], 14, [1,4], 1, 5>; // Floating point double division.
defm : SKLWriteResPair<WriteFDiv64X,  [SKLPort0,SKLFPDivider], 14, [1,4], 1, 6>;
defm : SKLWriteResPair<WriteFDiv64Y,  [SKLPort0,SKLFPDivider], 14, [1,8], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;

defm : SKLWriteResPair<WriteFSqrt,    [SKLPort0,SKLFPDivider], 12, [1,3], 1, 5>; // Floating point square root.
defm : SKLWriteResPair<WriteFSqrtX,   [SKLPort0,SKLFPDivider], 12, [1,3], 1, 6>;
defm : SKLWriteResPair<WriteFSqrtY,   [SKLPort0,SKLFPDivider], 12, [1,6], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : SKLWriteResPair<WriteFSqrt64,  [SKLPort0,SKLFPDivider], 18, [1,6], 1, 5>; // Floating point double square root.
defm : SKLWriteResPair<WriteFSqrt64X, [SKLPort0,SKLFPDivider], 18, [1,6], 1, 6>;
defm : SKLWriteResPair<WriteFSqrt64Y, [SKLPort0,SKLFPDivider], 18, [1,12],1, 7>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : SKLWriteResPair<WriteFSqrt80,  [SKLPort0,SKLFPDivider], 21, [1,7]>; // Floating point long double square root.

defm : SKLWriteResPair<WriteFRcp,   [SKLPort0], 4, [1], 1, 5>; // Floating point reciprocal estimate.
defm : SKLWriteResPair<WriteFRcpX,  [SKLPort0], 4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFRcpY,  [SKLPort0], 4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;

defm : SKLWriteResPair<WriteFRsqrt, [SKLPort0], 4, [1], 1, 5>; // Floating point reciprocal square root estimate.
defm : SKLWriteResPair<WriteFRsqrtX,[SKLPort0], 4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFRsqrtY,[SKLPort0], 4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;

defm : SKLWriteResPair<WriteFMA,    [SKLPort01], 4, [1], 1, 5>; // Fused Multiply Add.
defm : SKLWriteResPair<WriteFMAX,   [SKLPort01], 4, [1], 1, 6>;
defm : SKLWriteResPair<WriteFMAY,   [SKLPort01], 4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;
defm : SKLWriteResPair<WriteDPPD,   [SKLPort5,SKLPort01],  9, [1,2], 3, 6>; // Floating point double dot product.
defm : X86WriteRes<WriteDPPS,       [SKLPort5,SKLPort01], 13, [1,3], 4>;
defm : X86WriteRes<WriteDPPSY,      [SKLPort5,SKLPort01], 13, [1,3], 4>;
defm : X86WriteRes<WriteDPPSLd,     [SKLPort5,SKLPort01,SKLPort06,SKLPort23], 19, [1,3,1,1], 6>;
defm : X86WriteRes<WriteDPPSYLd,    [SKLPort5,SKLPort01,SKLPort06,SKLPort23], 20, [1,3,1,1], 6>;
defm : SKLWriteResPair<WriteFSign,   [SKLPort0], 1>; // Floating point fabs/fchs.
defm : SKLWriteResPair<WriteFRnd,     [SKLPort01], 8, [2], 2, 6>; // Floating point rounding.
defm : SKLWriteResPair<WriteFRndY,    [SKLPort01], 8, [2], 2, 7>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : SKLWriteResPair<WriteFLogic,  [SKLPort015], 1, [1], 1, 6>; // Floating point and/or/xor logicals.
defm : SKLWriteResPair<WriteFLogicY, [SKLPort015], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : SKLWriteResPair<WriteFTest,   [SKLPort0], 2, [1], 1, 6>; // Floating point TEST instructions.
defm : SKLWriteResPair<WriteFTestY,  [SKLPort0], 2, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : SKLWriteResPair<WriteFShuffle,  [SKLPort5], 1, [1], 1, 6>; // Floating point vector shuffles.
defm : SKLWriteResPair<WriteFShuffleY, [SKLPort5], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : SKLWriteResPair<WriteFVarShuffle,  [SKLPort5], 1, [1], 1, 6>; // Floating point vector shuffles.
defm : SKLWriteResPair<WriteFVarShuffleY, [SKLPort5], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : SKLWriteResPair<WriteFBlend,  [SKLPort015], 1, [1], 1, 6>; // Floating point vector blends.
defm : SKLWriteResPair<WriteFBlendY, [SKLPort015], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : SKLWriteResPair<WriteFVarBlend, [SKLPort015], 2, [2], 2, 6>; // Fp vector variable blends.
defm : SKLWriteResPair<WriteFVarBlendY,[SKLPort015], 2, [2], 2, 7>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;

// FMA Scheduling helper class.
// class FMASC { X86FoldableSchedWrite Sched = WriteFAdd; }

// Vector integer operations.
defm : X86WriteRes<WriteVecLoad,         [SKLPort23], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [SKLPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [SKLPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [SKLPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [SKLPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [SKLPort23,SKLPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [SKLPort23,SKLPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecStore,        [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreX,       [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreY,       [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNT,      [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNTY,     [SKLPort237,SKLPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore32,  [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore32Y, [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore64,  [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore64Y, [SKLPort23,SKLPort0,SKLPort4], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMove,         [SKLPort05],  1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [SKLPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [SKLPort015], 1, [1], 1>;
defm : X86WriteResUnsupported<WriteVecMoveZ>;
defm : X86WriteRes<WriteVecMoveToGpr,    [SKLPort0], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [SKLPort5], 1, [1], 1>;

defm : SKLWriteResPair<WriteVecALU,   [SKLPort05], 1, [1], 1, 5>; // Vector integer ALU op, no logicals.
defm : SKLWriteResPair<WriteVecALUX,  [SKLPort01], 1, [1], 1, 6>;
defm : SKLWriteResPair<WriteVecALUY,  [SKLPort01], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : SKLWriteResPair<WriteVecLogic, [SKLPort05],  1, [1], 1, 5>; // Vector integer and/or/xor.
defm : SKLWriteResPair<WriteVecLogicX,[SKLPort015], 1, [1], 1, 6>;
defm : SKLWriteResPair<WriteVecLogicY,[SKLPort015], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : SKLWriteResPair<WriteVecTest,  [SKLPort0,SKLPort5], 3, [1,1], 2, 6>; // Vector integer TEST instructions.
defm : SKLWriteResPair<WriteVecTestY, [SKLPort0,SKLPort5], 3, [1,1], 2, 7>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : SKLWriteResPair<WriteVecIMul,  [SKLPort0] ,  5, [1], 1, 5>; // Vector integer multiply.
defm : SKLWriteResPair<WriteVecIMulX, [SKLPort01],  5, [1], 1, 6>;
defm : SKLWriteResPair<WriteVecIMulY, [SKLPort01],  5, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
defm : SKLWriteResPair<WritePMULLD,   [SKLPort01], 10, [2], 2, 6>; // Vector PMULLD.
defm : SKLWriteResPair<WritePMULLDY,  [SKLPort01], 10, [2], 2, 7>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : SKLWriteResPair<WriteShuffle,  [SKLPort5], 1, [1], 1, 5>; // Vector shuffles.
defm : SKLWriteResPair<WriteShuffleX, [SKLPort5], 1, [1], 1, 6>;
defm : SKLWriteResPair<WriteShuffleY, [SKLPort5], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : SKLWriteResPair<WriteVarShuffle,  [SKLPort5], 1, [1], 1, 5>; // Vector shuffles.
defm : SKLWriteResPair<WriteVarShuffleX, [SKLPort5], 1, [1], 1, 6>;
defm : SKLWriteResPair<WriteVarShuffleY, [SKLPort5], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : SKLWriteResPair<WriteBlend,  [SKLPort5], 1, [1], 1, 6>; // Vector blends.
defm : SKLWriteResPair<WriteBlendY, [SKLPort5], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : SKLWriteResPair<WriteVarBlend,  [SKLPort015], 2, [2], 2, 6>; // Vector variable blends.
defm : SKLWriteResPair<WriteVarBlendY, [SKLPort015], 2, [2], 2, 6>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : SKLWriteResPair<WriteMPSAD,  [SKLPort5], 4, [2], 2, 6>; // Vector MPSAD.
defm : SKLWriteResPair<WriteMPSADY, [SKLPort5], 4, [2], 2, 7>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;
defm : SKLWriteResPair<WritePSADBW,  [SKLPort5], 3, [1], 1, 5>; // Vector PSADBW.
defm : SKLWriteResPair<WritePSADBWX, [SKLPort5], 3, [1], 1, 6>;
defm : SKLWriteResPair<WritePSADBWY, [SKLPort5], 3, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : SKLWriteResPair<WritePHMINPOS, [SKLPort01], 4, [1], 1, 6>; // Vector PHMINPOS.

// Vector integer shifts.
defm : SKLWriteResPair<WriteVecShift,     [SKLPort0], 1, [1], 1, 5>;
defm : X86WriteRes<WriteVecShiftX,        [SKLPort5,SKLPort01],  2, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftY,        [SKLPort5,SKLPort01],  4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftXLd,      [SKLPort01,SKLPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftYLd,      [SKLPort01,SKLPort23], 8, [1,1], 2>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;

defm : SKLWriteResPair<WriteVecShiftImm,  [SKLPort0],  1, [1], 1, 5>; // Vector integer immediate shifts.
defm : SKLWriteResPair<WriteVecShiftImmX, [SKLPort01], 1, [1], 1, 6>;
defm : SKLWriteResPair<WriteVecShiftImmY, [SKLPort01], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : SKLWriteResPair<WriteVarVecShift,  [SKLPort01], 1, [1], 1, 6>; // Variable vector shifts.
defm : SKLWriteResPair<WriteVarVecShiftY, [SKLPort01], 1, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;

// Vector insert/extract operations.
def : WriteRes<WriteVecInsert, [SKLPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : WriteRes<WriteVecInsertLd, [SKLPort5,SKLPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def: InstRW<[WriteVecInsertLd], (instregex "(V?)MOV(H|L)(PD|PS)rm")>;

def : WriteRes<WriteVecExtract, [SKLPort0,SKLPort5]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecExtractSt, [SKLPort4,SKLPort5,SKLPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// Conversion between integer and float.
defm : SKLWriteResPair<WriteCvtSS2I,   [SKLPort0,SKLPort01], 6, [1,1], 2, 5>;
defm : SKLWriteResPair<WriteCvtPS2I,   [SKLPort01], 4, [1], 1, 6>;
defm : SKLWriteResPair<WriteCvtPS2IY,  [SKLPort01], 4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : SKLWriteResPair<WriteCvtSD2I,   [SKLPort0,SKLPort01], 6, [1,1], 2, 5>;
defm : SKLWriteResPair<WriteCvtPD2I,   [SKLPort5,SKLPort01], 5, [1,1], 2, 6>;
defm : SKLWriteResPair<WriteCvtPD2IY,  [SKLPort5,SKLPort01], 7, [1,1], 2, 6>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

defm : X86WriteRes<WriteCvtI2SS,      [SKLPort5,SKLPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2SSLd,   [SKLPort23,SKLPort01], 10, [1,1], 2>;
defm : SKLWriteResPair<WriteCvtI2PS,   [SKLPort01], 4, [1], 1, 6>;
defm : SKLWriteResPair<WriteCvtI2PSY,  [SKLPort01], 4, [1], 1, 7>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : X86WriteRes<WriteCvtI2SD,      [SKLPort5,SKLPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2SDLd,   [SKLPort23,SKLPort01], 10, [1,1], 2>;
defm : SKLWriteResPair<WriteCvtI2PD,   [SKLPort0,SKLPort5],  5, [1,1], 2, 6>;
defm : SKLWriteResPair<WriteCvtI2PDY,  [SKLPort0,SKLPort5],  7, [1,1], 2, 6>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : X86WriteRes<WriteCvtSS2SD,     [SKLPort5,SKLPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtSS2SDLd,  [SKLPort23,SKLPort01], 10, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PD,     [SKLPort5,SKLPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDLd,  [SKLPort23,SKLPort01],  9, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDY,    [SKLPort5,SKLPort01],  7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDYLd, [SKLPort23,SKLPort01], 11, [1,1], 2>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;
defm : SKLWriteResPair<WriteCvtSD2SS,  [SKLPort5,SKLPort01], 5, [1,1], 2, 5>;
defm : SKLWriteResPair<WriteCvtPD2PS,  [SKLPort5,SKLPort01], 5, [1,1], 2, 6>;
defm : SKLWriteResPair<WriteCvtPD2PSY, [SKLPort5,SKLPort01], 7, [1,1], 2, 6>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

defm : X86WriteRes<WriteCvtPH2PS,     [SKLPort5,SKLPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSY,    [SKLPort5,SKLPort01],  7, [1,1], 2>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZ>;
defm : X86WriteRes<WriteCvtPH2PSLd,  [SKLPort23,SKLPort01],  9, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSYLd, [SKLPort23,SKLPort01], 10, [1,1], 2>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZLd>;

defm : X86WriteRes<WriteCvtPS2PH,                        [SKLPort5,SKLPort01], 5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHY,                       [SKLPort5,SKLPort01], 7, [1,1], 2>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
defm : X86WriteRes<WriteCvtPS2PHSt,  [SKLPort4,SKLPort5,SKLPort237,SKLPort01], 6, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [SKLPort4,SKLPort5,SKLPort237,SKLPort01], 8, [1,1,1,1], 4>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

// Strings instructions.

// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [SKLPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : WriteRes<WritePCmpIStrMLd, [SKLPort0, SKLPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [SKLPort0, SKLPort5, SKLPort015, SKLPort0156]> {
  let Latency = 19;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [4,3,1,1];
}
def : WriteRes<WritePCmpEStrMLd, [SKLPort0, SKLPort5,SKLPort23, SKLPort015, SKLPort0156]> {
  let Latency = 25;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [4,3,1,1,1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [SKLPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : WriteRes<WritePCmpIStrILd, [SKLPort0, SKLPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [SKLPort0, SKLPort5, SKLPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [4,3,1];
}
def : WriteRes<WritePCmpEStrILd, [SKLPort0, SKLPort5, SKLPort23, SKLPort0156]> {
  let Latency = 24;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [4,3,1,1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [SKLPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSK,  [SKLPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSKY, [SKLPort0]> { let Latency = 2; }
def : WriteRes<WriteMMXMOVMSK,  [SKLPort0]> { let Latency = 2; }

// AES instructions.
def : WriteRes<WriteAESDecEnc, [SKLPort0]> { // Decryption, encryption.
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def : WriteRes<WriteAESDecEncLd, [SKLPort0, SKLPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}

def : WriteRes<WriteAESIMC, [SKLPort0]> { // InvMixColumn.
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : WriteRes<WriteAESIMCLd, [SKLPort0, SKLPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}

def : WriteRes<WriteAESKeyGen, [SKLPort0, SKLPort5, SKLPort015]> { // Key Generation.
  let Latency = 20;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [3,6,2];
}
def : WriteRes<WriteAESKeyGenLd, [SKLPort0, SKLPort5, SKLPort23, SKLPort015]> {
  let Latency = 25;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [3,6,1,1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [SKLPort5]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def : WriteRes<WriteCLMulLd, [SKLPort5, SKLPort23]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}

// Catch-all for expensive system instructions.
def : WriteRes<WriteSystem,     [SKLPort0156]> { let Latency = 100; } // def WriteSystem : SchedWrite;

// AVX2.
defm : SKLWriteResPair<WriteFShuffle256, [SKLPort5], 3, [1], 1, 7>; // Fp 256-bit width vector shuffles.
defm : SKLWriteResPair<WriteFVarShuffle256, [SKLPort5], 3, [1], 1, 7>; // Fp 256-bit width vector variable shuffles.
defm : SKLWriteResPair<WriteShuffle256, [SKLPort5], 3, [1], 1, 7>;  // 256-bit width vector shuffles.
defm : SKLWriteResPair<WriteVPMOV256, [SKLPort5], 3, [1], 1, 7>;  // 256-bit width packed vector width-changing move.
defm : SKLWriteResPair<WriteVarShuffle256, [SKLPort5], 3, [1], 1, 7>;  // 256-bit width vector variable shuffles.

// Old microcoded instructions that nobody use.
def : WriteRes<WriteMicrocoded, [SKLPort0156]> { let Latency = 100; } // def WriteMicrocoded : SchedWrite;

// Fence instructions.
def : WriteRes<WriteFence,  [SKLPort23, SKLPort4]>;

// Load/store MXCSR.
def : WriteRes<WriteLDMXCSR, [SKLPort0,SKLPort23,SKLPort0156]> { let Latency = 7; let NumMicroOps = 3; let ReleaseAtCycles = [1,1,1]; }
def : WriteRes<WriteSTMXCSR, [SKLPort4,SKLPort5,SKLPort237]> { let Latency = 2; let NumMicroOps = 3; let ReleaseAtCycles = [1,1,1]; }

// Nop, not very useful expect it provides a model for nops!
def : WriteRes<WriteNop, []>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : SKLWriteResPair<WriteFHAdd,  [SKLPort5,SKLPort01], 6, [2,1], 3, 6>;
defm : SKLWriteResPair<WriteFHAddY, [SKLPort5,SKLPort01], 6, [2,1], 3, 7>;
defm : SKLWriteResPair<WritePHAdd,  [SKLPort5,SKLPort05],  3, [2,1], 3, 5>;
defm : SKLWriteResPair<WritePHAddX, [SKLPort5,SKLPort015], 3, [2,1], 3, 6>;
defm : SKLWriteResPair<WritePHAddY, [SKLPort5,SKLPort015], 3, [2,1], 3, 7>;

// Remaining instrs.

def SKLWriteResGroup1 : SchedWriteRes<[SKLPort0]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup1], (instregex "MMX_PADDS(B|W)rr",
                                            "MMX_PADDUS(B|W)rr",
                                            "MMX_PAVG(B|W)rr",
                                            "MMX_PCMPEQ(B|D|W)rr",
                                            "MMX_PCMPGT(B|D|W)rr",
                                            "MMX_P(MAX|MIN)SWrr",
                                            "MMX_P(MAX|MIN)UBrr",
                                            "MMX_PSUBS(B|W)rr",
                                            "MMX_PSUBUS(B|W)rr")>;

def SKLWriteResGroup3 : SchedWriteRes<[SKLPort5]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup3], (instregex "COM(P?)_FST0r",
                                            "UCOM_F(P?)r")>;

def SKLWriteResGroup4 : SchedWriteRes<[SKLPort6]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup4], (instregex "JMP(16|32|64)r")>;

def SKLWriteResGroup6 : SchedWriteRes<[SKLPort05]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup6], (instrs FINCSTP, FNOP)>;

def SKLWriteResGroup7 : SchedWriteRes<[SKLPort06]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup7], (instrs CDQ, CQO, CLAC, STAC)>;

def SKLWriteResGroup8 : SchedWriteRes<[SKLPort15]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup8], (instregex "ANDN(32|64)rr")>;

def SKLWriteResGroup9 : SchedWriteRes<[SKLPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup9], (instregex "(V?)PADD(B|D|Q|W)(Y?)rr",
                                            "VPBLENDD(Y?)rri")>;

def SKLWriteResGroup10 : SchedWriteRes<[SKLPort0156]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup10], (instrs SGDT64m,
                                          SIDT64m,
                                          SMSW16m,
                                          STRm,
                                          SYSCALL)>;

def SKLWriteResGroup11 : SchedWriteRes<[SKLPort4,SKLPort237]> {
  let Latency = 1;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup11], (instrs FBSTPm, VMPTRSTm)>;
def: InstRW<[SKLWriteResGroup11], (instregex "ST_FP(32|64|80)m")>;

def SKLWriteResGroup13 : SchedWriteRes<[SKLPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[SKLWriteResGroup13], (instrs MMX_MOVQ2DQrr)>;

def SKLWriteResGroup14 : SchedWriteRes<[SKLPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[SKLWriteResGroup14], (instrs FDECSTP,
                                          MMX_MOVDQ2Qrr)>;

def SKLWriteResGroup17 : SchedWriteRes<[SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[SKLWriteResGroup17], (instrs LFENCE,
                                          WAIT,
                                          XGETBV)>;

def SKLWriteResGroup20 : SchedWriteRes<[SKLPort6,SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup20], (instregex "CLFLUSH")>;

def SKLWriteResGroup21 : SchedWriteRes<[SKLPort237,SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup21], (instrs SFENCE)>;

def SKLWriteResGroup23 : SchedWriteRes<[SKLPort06,SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup23], (instrs CWD,
                                          JCXZ, JECXZ, JRCXZ,
                                          ADC8i8, SBB8i8,
                                          ADC16i16, SBB16i16,
                                          ADC32i32, SBB32i32,
                                          ADC64i32, SBB64i32)>;

def SKLWriteResGroup25 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup25], (instrs FNSTCW16m)>;

def SKLWriteResGroup27 : SchedWriteRes<[SKLPort4,SKLPort237,SKLPort15]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup27], (instregex "MOVBE(16|32|64)mr")>;

def SKLWriteResGroup28 : SchedWriteRes<[SKLPort4,SKLPort237,SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup28], (instrs PUSH16r, PUSH32r, PUSH64r, PUSH64i8,
                                          STOSB, STOSL, STOSQ, STOSW)>;
def: InstRW<[SKLWriteResGroup28], (instregex "PUSH(16|32|64)rmr")>;

def SKLWriteResGroup29 : SchedWriteRes<[SKLPort1]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup29], (instregex "PDEP(32|64)rr",
                                             "PEXT(32|64)rr")>;

def SKLWriteResGroup30 : SchedWriteRes<[SKLPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup30], (instregex "(ADD|SUB|SUBR)_(FPrST0|FST0r|FrST0)",
                                             "VPBROADCAST(B|W)rr")>;

def SKLWriteResGroup32 : SchedWriteRes<[SKLPort0,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup32], (instrs FNSTSW16r)>;

def SKLWriteResGroup35 : SchedWriteRes<[SKLPort0,SKLPort5]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[SKLWriteResGroup35], (instregex "MMX_PH(ADD|SUB)SWrr")>;

def SKLWriteResGroup36 : SchedWriteRes<[SKLPort5,SKLPort01]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup36], (instregex "(V?)PHADDSW(Y?)rr",
                                             "(V?)PHSUBSW(Y?)rr")>;

def SKLWriteResGroup39 : SchedWriteRes<[SKLPort5,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup39], (instrs MMX_PACKSSDWrr,
                                          MMX_PACKSSWBrr,
                                          MMX_PACKUSWBrr)>;

def SKLWriteResGroup40 : SchedWriteRes<[SKLPort6,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[SKLWriteResGroup40], (instregex "CLD")>;

def SKLWriteResGroup41 : SchedWriteRes<[SKLPort237,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[SKLWriteResGroup41], (instrs MFENCE)>;

def SKLWriteResGroup42 : SchedWriteRes<[SKLPort06,SKLPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[SKLWriteResGroup42], (instrs RCL8r1, RCL16r1, RCL32r1, RCL64r1,
                                          RCR8r1, RCR16r1, RCR32r1, RCR64r1)>;

def SKLWriteResGroup42b : SchedWriteRes<[SKLPort1,SKLPort06,SKLPort0156]> {
  let Latency = 5;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [2,4,2];
}
def: InstRW<[SKLWriteResGroup42b], (instrs RCR8ri, RCR16ri, RCR32ri, RCR64ri)>;

def SKLWriteResGroup42c : SchedWriteRes<[SKLPort1,SKLPort06,SKLPort0156]> {
  let Latency = 6;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [2,4,2];
}
def: InstRW<[SKLWriteResGroup42c], (instrs RCL8ri, RCL16ri, RCL32ri, RCL64ri)>;

def SKLWriteResGroup43 : SchedWriteRes<[SKLPort0,SKLPort4,SKLPort237]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup43], (instrs FNSTSWm)>;

def SKLWriteResGroup45 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort237,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[SKLWriteResGroup45], (instregex "CALL(16|32|64)r")>;

def SKLWriteResGroup46 : SchedWriteRes<[SKLPort4,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[SKLWriteResGroup46], (instrs CALL64pcrel32)>;

def SKLWriteResGroup47 : SchedWriteRes<[SKLPort0]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup47], (instregex "MUL_(FPrST0|FST0r|FrST0)")>;

def SKLWriteResGroup53 : SchedWriteRes<[SKLPort4,SKLPort5,SKLPort237]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup53], (instregex "IST(T?)_FP(16|32|64)m",
                                             "IST_F(16|32)m")>;

def SKLWriteResGroup54 : SchedWriteRes<[SKLPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}
def: InstRW<[SKLWriteResGroup54], (instrs FNCLEX)>;

def SKLWriteResGroup55 : SchedWriteRes<[SKLPort6,SKLPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,3];
}
def: InstRW<[SKLWriteResGroup55], (instrs PAUSE)>;

def SKLWriteResGroup56 : SchedWriteRes<[]> {
  let Latency = 0;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [];
}
def: InstRW<[SKLWriteResGroup56], (instrs VZEROUPPER)>;

def SKLWriteResGroup57 : SchedWriteRes<[SKLPort1,SKLPort6,SKLPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,2];
}
def: InstRW<[SKLWriteResGroup57], (instregex "LAR(16|32|64)rr")>;

def SKLWriteResGroup60 : SchedWriteRes<[SKLPort5,SKLPort01]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup60], (instregex "MMX_CVT(T?)PS2PIrr")>;

def SKLWriteResGroup61 : SchedWriteRes<[SKLPort1,SKLPort6,SKLPort06]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup61], (instregex "STR(16|32|64)r")>;

def SKLWriteResGroup63 : SchedWriteRes<[SKLPort06,SKLPort0156]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,4];
}
def: InstRW<[SKLWriteResGroup63], (instrs XSETBV)>;

def SKLWriteResGroup65 : SchedWriteRes<[SKLPort4,SKLPort237,SKLPort0156]> {
  let Latency = 5;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,4];
}
def: InstRW<[SKLWriteResGroup65], (instregex "PUSHF(16|64)")>;

def SKLWriteResGroup67 : SchedWriteRes<[SKLPort23]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup67], (instrs VBROADCASTSSrm,
                                          VPBROADCASTDrm,
                                          VPBROADCASTQrm)>;
def: InstRW<[SKLWriteResGroup67], (instregex "(V?)MOVSHDUPrm",
                                             "(V?)MOVSLDUPrm",
                                             "(V?)MOVDDUPrm")>;

def SKLWriteResGroup68 : SchedWriteRes<[SKLPort0]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[SKLWriteResGroup68], (instrs MMX_CVTPI2PSrr)>;

def SKLWriteResGroup69 : SchedWriteRes<[SKLPort0,SKLPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup69], (instrs MMX_PADDSBrm,
                                          MMX_PADDSWrm,
                                          MMX_PADDUSBrm,
                                          MMX_PADDUSWrm,
                                          MMX_PAVGBrm,
                                          MMX_PAVGWrm,
                                          MMX_PCMPEQBrm,
                                          MMX_PCMPEQDrm,
                                          MMX_PCMPEQWrm,
                                          MMX_PCMPGTBrm,
                                          MMX_PCMPGTDrm,
                                          MMX_PCMPGTWrm,
                                          MMX_PMAXSWrm,
                                          MMX_PMAXUBrm,
                                          MMX_PMINSWrm,
                                          MMX_PMINUBrm,
                                          MMX_PSUBSBrm,
                                          MMX_PSUBSWrm,
                                          MMX_PSUBUSBrm,
                                          MMX_PSUBUSWrm)>;

def SKLWriteResGroup72 : SchedWriteRes<[SKLPort6,SKLPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup72], (instrs FARJMP64m)>;
def: InstRW<[SKLWriteResGroup72], (instregex "JMP(16|32|64)m")>;

def SKLWriteResGroup75 : SchedWriteRes<[SKLPort23,SKLPort15]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup75], (instregex "ANDN(32|64)rm",
                                             "MOVBE(16|32|64)rm")>;

def SKLWriteResGroup76 : SchedWriteRes<[SKLPort23,SKLPort0156]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup76], (instrs POP16r, POP32r, POP64r)>;
def: InstRW<[SKLWriteResGroup76], (instregex "POP(16|32|64)rmr")>;

def SKLWriteResGroup78 : SchedWriteRes<[SKLPort5,SKLPort01]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup78], (instregex "(V?)CVTSI642SSrr")>;

def SKLWriteResGroup80 : SchedWriteRes<[SKLPort1,SKLPort6,SKLPort06,SKLPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[SKLWriteResGroup80], (instregex "SLDT(16|32|64)r")>;

def SKLWriteResGroup82 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort06]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[SKLWriteResGroup82], (instregex "SAR(8|16|32|64)m(1|i)",
                                             "SHL(8|16|32|64)m(1|i)",
                                             "SHR(8|16|32|64)m(1|i)")>;

def SKLWriteResGroup83 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[SKLWriteResGroup83], (instregex "POP(16|32|64)rmm",
                                             "PUSH(16|32|64)rmm")>;

def SKLWriteResGroup84 : SchedWriteRes<[SKLPort6,SKLPort0156]> {
  let Latency = 6;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,5];
}
def: InstRW<[SKLWriteResGroup84], (instrs STD)>;

def SKLWriteResGroup85 : SchedWriteRes<[SKLPort23]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup85], (instregex "LD_F(32|64|80)m")>;
def: InstRW<[SKLWriteResGroup85], (instrs VBROADCASTF128rm,
                                          VBROADCASTI128rm,
                                          VBROADCASTSDYrm,
                                          VBROADCASTSSYrm,
                                          VMOVDDUPYrm,
                                          VMOVSHDUPYrm,
                                          VMOVSLDUPYrm,
                                          VPBROADCASTDYrm,
                                          VPBROADCASTQYrm)>;

def SKLWriteResGroup88 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup88], (instregex "(V?)PMOV(SX|ZX)BDrm",
                                             "(V?)PMOV(SX|ZX)BQrm",
                                             "(V?)PMOV(SX|ZX)BWrm",
                                             "(V?)PMOV(SX|ZX)DQrm",
                                             "(V?)PMOV(SX|ZX)WDrm",
                                             "(V?)PMOV(SX|ZX)WQrm")>;

def SKLWriteResGroup91 : SchedWriteRes<[SKLPort23,SKLPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup91], (instrs VINSERTF128rm,
                                          VINSERTI128rm,
                                          VPBLENDDrmi)>;
def: InstRW<[SKLWriteResGroup91, ReadAfterVecXLd],
                                  (instregex "(V?)PADD(B|D|Q|W)rm",
                                             "(V?)PSUB(B|D|Q|W)rm")>;

def SKLWriteResGroup92 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup92], (instrs MMX_PACKSSDWrm,
                                          MMX_PACKSSWBrm,
                                          MMX_PACKUSWBrm)>;

def SKLWriteResGroup94 : SchedWriteRes<[SKLPort23,SKLPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[SKLWriteResGroup94], (instrs LEAVE, LEAVE64,
                                          SCASB, SCASL, SCASQ, SCASW)>;

def SKLWriteResGroup95 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort01]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup95], (instregex "(V?)CVT(T?)SS2SI64rr")>;

def SKLWriteResGroup96 : SchedWriteRes<[SKLPort0,SKLPort23,SKLPort05]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup96], (instrs FLDCW16m)>;

def SKLWriteResGroup98 : SchedWriteRes<[SKLPort6,SKLPort23,SKLPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup98], (instrs LRET64, RET64)>;

def SKLWriteResGroup100 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort06]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[SKLWriteResGroup100], (instregex "ROL(8|16|32|64)m(1|i)",
                                              "ROR(8|16|32|64)m(1|i)")>;

def SKLWriteResGroup100_1 : SchedWriteRes<[SKLPort06]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[SKLWriteResGroup100_1], (instrs ROL8r1, ROL16r1, ROL32r1, ROL64r1,
                                             ROR8r1, ROR16r1, ROR32r1, ROR64r1)>;

def SKLWriteResGroup101 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[SKLWriteResGroup101], (instregex "XADD(8|16|32|64)rm")>;

def SKLWriteResGroup102 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,1,1];
}
def: InstRW<[SKLWriteResGroup102], (instregex "CALL(16|32|64)m")>;
def: InstRW<[SKLWriteResGroup102], (instrs FARCALL64m)>;

def SKLWriteResGroup103 : SchedWriteRes<[SKLPort6,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [1,3,1,2];
}
def: InstRW<[SKLWriteResGroup103], (instrs LOOP)>;

def SKLWriteResGroup107 : SchedWriteRes<[SKLPort1,SKLPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup107], (instregex "PDEP(32|64)rm",
                                              "PEXT(32|64)rm")>;

def SKLWriteResGroup108 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup108], (instregex "FCOM(P?)(32|64)m")>;
def: InstRW<[SKLWriteResGroup108], (instrs VPBROADCASTBYrm,
                                           VPBROADCASTWYrm,
                                           VPMOVSXBDYrm,
                                           VPMOVSXBQYrm,
                                           VPMOVSXWQYrm)>;

def SKLWriteResGroup110 : SchedWriteRes<[SKLPort23,SKLPort015]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup110], (instrs VPBLENDDYrmi)>;
def: InstRW<[SKLWriteResGroup110, ReadAfterVecYLd],
                                   (instregex "VPADD(B|D|Q|W)Yrm",
                                              "VPSUB(B|D|Q|W)Yrm")>;

def SKLWriteResGroup112 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort23]> {
  let Latency = 8;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,2,1];
}
def: InstRW<[SKLWriteResGroup112], (instregex "MMX_PH(ADD|SUB)SWrm")>;

def SKLWriteResGroup116 : SchedWriteRes<[SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[SKLWriteResGroup116], (instregex "RCL(8|16|32|64)m(1|i)",
                                              "RCR(8|16|32|64)m(1|i)")>;

def SKLWriteResGroup117 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort06]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,1,3];
}
def: InstRW<[SKLWriteResGroup117], (instregex "ROL(8|16|32|64)mCL",
                                              "ROR(8|16|32|64)mCL",
                                              "SAR(8|16|32|64)mCL",
                                              "SHL(8|16|32|64)mCL",
                                              "SHR(8|16|32|64)mCL")>;

def SKLWriteResGroup119 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,1,2,1];
}
def: SchedAlias<WriteADCRMW, SKLWriteResGroup119>;

def SKLWriteResGroup120 : SchedWriteRes<[SKLPort0,SKLPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup120], (instrs MMX_CVTPI2PSrm)>;

def SKLWriteResGroup121 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup121], (instrs PCMPGTQrm,
                                           VPCMPGTQrm,
                                           VPMOVSXBWYrm,
                                           VPMOVSXDQYrm,
                                           VPMOVSXWDYrm,
                                           VPMOVZXWDYrm)>;

def SKLWriteResGroup123 : SchedWriteRes<[SKLPort23,SKLPort01]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup123], (instregex "MMX_CVT(T?)PS2PIrm")>;

def SKLWriteResGroup128 : SchedWriteRes<[SKLPort5,SKLPort01,SKLPort23]> {
  let Latency = 9;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[SKLWriteResGroup128], (instregex "(V?)PHADDSWrm",
                                              "(V?)PHSUBSWrm")>;

def SKLWriteResGroup131 : SchedWriteRes<[SKLPort1,SKLPort6,SKLPort23,SKLPort0156]> {
  let Latency = 9;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,2,1,1];
}
def: InstRW<[SKLWriteResGroup131], (instregex "LAR(16|32|64)rm",
                                              "LSL(16|32|64)rm")>;

def SKLWriteResGroup133 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup133], (instregex "(ADD|SUB|SUBR)_F(32|64)m",
                                              "ILD_F(16|32|64)m")>;
def: InstRW<[SKLWriteResGroup133], (instrs VPCMPGTQYrm)>;

def SKLWriteResGroup138 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort23]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup138], (instrs MMX_CVTPI2PDrm)>;

def SKLWriteResGroup140 : SchedWriteRes<[SKLPort5,SKLPort01,SKLPort23]> {
  let Latency = 10;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[SKLWriteResGroup140], (instrs VPHADDSWYrm,
                                           VPHSUBSWYrm)>;

def SKLWriteResGroup143 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 10;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,1,1,3];
}
def: InstRW<[SKLWriteResGroup143], (instregex "XCHG(8|16|32|64)rm")>;

def SKLWriteResGroup146 : SchedWriteRes<[SKLPort0,SKLPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup146], (instregex "MUL_F(32|64)m")>;

def SKLWriteResGroup149 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup149], (instregex "FICOM(P?)(16|32)m")>;

def SKLWriteResGroup154 : SchedWriteRes<[SKLPort1,SKLPort06,SKLPort0156]> {
  let Latency = 11;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [2,3,2];
}
def: InstRW<[SKLWriteResGroup154], (instregex "RCL(16|32|64)rCL",
                                              "RCR(16|32|64)rCL")>;

def SKLWriteResGroup155 : SchedWriteRes<[SKLPort1,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 11;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [1,5,1,2];
}
def: InstRW<[SKLWriteResGroup155], (instrs RCL8rCL)>;

def SKLWriteResGroup156 : SchedWriteRes<[SKLPort06,SKLPort0156]> {
  let Latency = 11;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [2,9];
}
def: InstRW<[SKLWriteResGroup156], (instrs LOOPE, LOOPNE)>;

def SKLWriteResGroup162 : SchedWriteRes<[SKLPort5,SKLPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[SKLWriteResGroup162], (instregex "(ADD|SUB|SUBR)_FI(16|32)m")>;

def SKLWriteResGroup169 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup169], (instregex "MUL_FI(16|32)m")>;

def SKLWriteResGroup170 : SchedWriteRes<[SKLPort1,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 14;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [2,4,1,3];
}
def: InstRW<[SKLWriteResGroup170], (instrs RCR8rCL)>;

def SKLWriteResGroup171 : SchedWriteRes<[SKLPort0]> {
  let Latency = 15;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup171], (instregex "DIVR_(FPrST0|FST0r|FrST0)")>;

def SKLWriteResGroup174 : SchedWriteRes<[SKLPort1,SKLPort23,SKLPort237,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 15;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [1,1,1,5,1,1];
}
def: InstRW<[SKLWriteResGroup174], (instregex "RCL(8|16|32|64)mCL")>;

def SKLWriteResGroup177 : SchedWriteRes<[SKLPort4,SKLPort23,SKLPort237,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 16;
  let NumMicroOps = 14;
  let ReleaseAtCycles = [1,1,1,4,2,5];
}
def: InstRW<[SKLWriteResGroup177], (instrs CMPXCHG8B)>;

def SKLWriteResGroup178 : SchedWriteRes<[SKLPort0156]> {
  let Latency = 16;
  let NumMicroOps = 16;
  let ReleaseAtCycles = [16];
}
def: InstRW<[SKLWriteResGroup178], (instrs VZEROALL)>;

def SKLWriteResGroup180 : SchedWriteRes<[SKLPort0,SKLPort1,SKLPort5,SKLPort6,SKLPort05,SKLPort0156]> {
  let Latency = 17;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [2,1,2,4,2,4];
}
def: InstRW<[SKLWriteResGroup180], (instrs XCH_F)>;

def SKLWriteResGroup184 : SchedWriteRes<[SKLPort5,SKLPort6,SKLPort06,SKLPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,5];
}
def: InstRW<[SKLWriteResGroup184], (instrs CPUID, RDTSC)>;

def SKLWriteResGroup185 : SchedWriteRes<[SKLPort1,SKLPort23,SKLPort237,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 18;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [2,1,1,4,1,2];
}
def: InstRW<[SKLWriteResGroup185], (instregex "RCR(8|16|32|64)mCL")>;

def SKLWriteResGroup189 : SchedWriteRes<[SKLPort0]> {
  let Latency = 20;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[SKLWriteResGroup189], (instregex "DIV_(FPrST0|FST0r|FrST0)")>;

def SKLWriteResGroup192 : SchedWriteRes<[SKLPort4,SKLPort5,SKLPort6,SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 20;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,1,1,1,2];
}
def: InstRW<[SKLWriteResGroup192], (instrs INSB, INSL, INSW)>;

def SKLWriteResGroup193 : SchedWriteRes<[SKLPort5,SKLPort6,SKLPort0156]> {
  let Latency = 20;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [1,2,7];
}
def: InstRW<[SKLWriteResGroup193], (instrs MWAITrr)>;

def SKLWriteResGroup196 : SchedWriteRes<[SKLPort0,SKLPort23]> {
  let Latency = 22;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup196], (instregex "DIV_F(32|64)m")>;

def SKLWriteResGroupVEX2 : SchedWriteRes<[SKLPort0, SKLPort23, SKLPort5, SKLPort015]> {
  let Latency = 18;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,2,1,1];
}
def: InstRW<[SKLWriteResGroupVEX2], (instrs VGATHERDPDrm, VPGATHERDQrm,
                                            VGATHERQPDrm, VPGATHERQQrm,
                                            VGATHERQPSrm, VPGATHERQDrm)>;

def SKLWriteResGroupVEX4 : SchedWriteRes<[SKLPort0, SKLPort23, SKLPort5, SKLPort015]> {
  let Latency = 20;
  let NumMicroOps = 5; // 2 uops peform multiple loads
  let ReleaseAtCycles = [1,4,1,1];
}
def: InstRW<[SKLWriteResGroupVEX4], (instrs VGATHERDPDYrm, VPGATHERDQYrm,
                                            VGATHERDPSrm,  VPGATHERDDrm,
                                            VGATHERQPDYrm, VPGATHERQQYrm,
                                            VGATHERQPSYrm,  VPGATHERQDYrm)>;

def SKLWriteResGroupVEX8 : SchedWriteRes<[SKLPort0, SKLPort23, SKLPort5, SKLPort015]> {
  let Latency = 22;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,8,1,1];
}
def: InstRW<[SKLWriteResGroupVEX8], (instrs VGATHERDPSYrm,  VPGATHERDDYrm)>;

def SKLWriteResGroup198 : SchedWriteRes<[SKLPort0,SKLPort4,SKLPort5,SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 23;
  let NumMicroOps = 19;
  let ReleaseAtCycles = [2,1,4,1,1,4,6];
}
def: InstRW<[SKLWriteResGroup198], (instrs CMPXCHG16B)>;

def SKLWriteResGroup202 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort23]> {
  let Latency = 25;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup202], (instregex "DIV_FI(16|32)m")>;

def SKLWriteResGroup206 : SchedWriteRes<[SKLPort0,SKLPort23]> {
  let Latency = 27;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[SKLWriteResGroup206], (instregex "DIVR_F(32|64)m")>;

def SKLWriteResGroup208 : SchedWriteRes<[SKLPort0,SKLPort5,SKLPort23]> {
  let Latency = 30;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[SKLWriteResGroup208], (instregex "DIVR_FI(16|32)m")>;

def SKLWriteResGroup209 : SchedWriteRes<[SKLPort5,SKLPort6,SKLPort23,SKLPort06,SKLPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ReleaseAtCycles = [1,5,3,4,10];
}
def: InstRW<[SKLWriteResGroup209], (instregex "IN(8|16|32)ri",
                                              "IN(8|16|32)rr")>;

def SKLWriteResGroup210 : SchedWriteRes<[SKLPort5,SKLPort6,SKLPort23,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ReleaseAtCycles = [1,5,2,1,4,10];
}
def: InstRW<[SKLWriteResGroup210], (instregex "OUT(8|16|32)ir",
                                              "OUT(8|16|32)rr")>;

def SKLWriteResGroup211 : SchedWriteRes<[SKLPort1,SKLPort6,SKLPort23,SKLPort0156]> {
  let Latency = 37;
  let NumMicroOps = 31;
  let ReleaseAtCycles = [1,8,1,21];
}
def: InstRW<[SKLWriteResGroup211], (instregex "XRSTOR(64)?")>;

def SKLWriteResGroup212 : SchedWriteRes<[SKLPort1,SKLPort4,SKLPort5,SKLPort6,SKLPort23,SKLPort237,SKLPort15,SKLPort0156]> {
  let Latency = 40;
  let NumMicroOps = 18;
  let ReleaseAtCycles = [1,1,2,3,1,1,1,8];
}
def: InstRW<[SKLWriteResGroup212], (instrs VMCLEARm)>;

def SKLWriteResGroup213 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 41;
  let NumMicroOps = 39;
  let ReleaseAtCycles = [1,10,1,1,26];
}
def: InstRW<[SKLWriteResGroup213], (instrs XSAVE64)>;

def SKLWriteResGroup214 : SchedWriteRes<[SKLPort5,SKLPort0156]> {
  let Latency = 42;
  let NumMicroOps = 22;
  let ReleaseAtCycles = [2,20];
}
def: InstRW<[SKLWriteResGroup214], (instrs RDTSCP)>;

def SKLWriteResGroup215 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 42;
  let NumMicroOps = 40;
  let ReleaseAtCycles = [1,11,1,1,26];
}
def: InstRW<[SKLWriteResGroup215], (instrs XSAVE)>;
def: InstRW<[SKLWriteResGroup215], (instregex "XSAVEC", "XSAVES")>;

def SKLWriteResGroup216 : SchedWriteRes<[SKLPort4,SKLPort6,SKLPort23,SKLPort237,SKLPort0156]> {
  let Latency = 46;
  let NumMicroOps = 44;
  let ReleaseAtCycles = [1,11,1,1,30];
}
def: InstRW<[SKLWriteResGroup216], (instregex "XSAVEOPT")>;

def SKLWriteResGroup217 : SchedWriteRes<[SKLPort0,SKLPort23,SKLPort05,SKLPort06,SKLPort0156]> {
  let Latency = 62;
  let NumMicroOps = 64;
  let ReleaseAtCycles = [2,8,5,10,39];
}
def: InstRW<[SKLWriteResGroup217], (instrs FLDENVm)>;

def SKLWriteResGroup218 : SchedWriteRes<[SKLPort0,SKLPort6,SKLPort23,SKLPort05,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 63;
  let NumMicroOps = 88;
  let ReleaseAtCycles = [4,4,31,1,2,1,45];
}
def: InstRW<[SKLWriteResGroup218], (instrs FXRSTOR64)>;

def SKLWriteResGroup219 : SchedWriteRes<[SKLPort0,SKLPort6,SKLPort23,SKLPort05,SKLPort06,SKLPort15,SKLPort0156]> {
  let Latency = 63;
  let NumMicroOps = 90;
  let ReleaseAtCycles = [4,2,33,1,2,1,47];
}
def: InstRW<[SKLWriteResGroup219], (instrs FXRSTOR)>;

def SKLWriteResGroup220 : SchedWriteRes<[SKLPort5,SKLPort05,SKLPort0156]> {
  let Latency = 75;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [6,3,6];
}
def: InstRW<[SKLWriteResGroup220], (instrs FNINIT)>;

def SKLWriteResGroup223 : SchedWriteRes<[SKLPort0,SKLPort1,SKLPort4,SKLPort5,SKLPort6,SKLPort237,SKLPort06,SKLPort0156]> {
  let Latency = 106;
  let NumMicroOps = 100;
  let ReleaseAtCycles = [9,1,11,16,1,11,21,30];
}
def: InstRW<[SKLWriteResGroup223], (instrs FSTENVm)>;

def: InstRW<[WriteZero], (instrs CLC)>;


// Instruction variants handled by the renamer. These might not need execution
// ports in certain conditions.
// See Agner's Fog "The microarchitecture of Intel, AMD and VIA CPUs",
// section "Skylake Pipeline" > "Register allocation and renaming".
// These can be investigated with llvm-exegesis, e.g.
// echo 'pxor %mm0, %mm0' | /tmp/llvm-exegesis -mode=uops -snippets-file=-
// echo 'vxorpd %xmm0, %xmm0, %xmm1' | /tmp/llvm-exegesis -mode=uops -snippets-file=-

def SKLWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def SKLWriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[SKLWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                          XOR32rr, XOR64rr)>;

def SKLWriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
def : InstRW<[SKLWriteFZeroIdiom], (instrs XORPSrr, VXORPSrr, XORPDrr,
                                           VXORPDrr)>;

def SKLWriteFZeroIdiomY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogicY]>
]>;
def : InstRW<[SKLWriteFZeroIdiomY], (instrs VXORPSYrr, VXORPDYrr)>;

def SKLWriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
def : InstRW<[SKLWriteVZeroIdiomLogicX], (instrs PXORrr, VPXORrr)>;

def SKLWriteVZeroIdiomLogicY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicY]>
]>;
def : InstRW<[SKLWriteVZeroIdiomLogicY], (instrs VPXORYrr)>;

def SKLWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
def : InstRW<[SKLWriteVZeroIdiomALUX], (instrs PCMPGTBrr, VPCMPGTBrr,
                                               PCMPGTDrr, VPCMPGTDrr,
                                               PCMPGTWrr, VPCMPGTWrr)>;

def SKLWriteVZeroIdiomALUY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUY]>
]>;
def : InstRW<[SKLWriteVZeroIdiomALUY], (instrs VPCMPGTBYrr,
                                               VPCMPGTDYrr,
                                               VPCMPGTWYrr)>;

def SKLWritePSUB : SchedWriteRes<[SKLPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}

def SKLWriteVZeroIdiomPSUB : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [SKLWritePSUB]>
]>;
def : InstRW<[SKLWriteVZeroIdiomPSUB], (instrs PSUBBrr, VPSUBBrr,
                                               PSUBDrr, VPSUBDrr,
                                               PSUBQrr, VPSUBQrr,
                                               PSUBWrr, VPSUBWrr,
                                               VPSUBBYrr,
                                               VPSUBDYrr,
                                               VPSUBQYrr,
                                               VPSUBWYrr)>;

def SKLWritePCMPGTQ : SchedWriteRes<[SKLPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}

def SKLWriteVZeroIdiomPCMPGTQ : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SKLWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [SKLWritePCMPGTQ]>
]>;
def : InstRW<[SKLWriteVZeroIdiomPCMPGTQ], (instrs PCMPGTQrr, VPCMPGTQrr,
                                                  VPCMPGTQYrr)>;


// CMOVs that use both Z and C flag require an extra uop.
def SKLWriteCMOVA_CMOVBErr : SchedWriteRes<[SKLPort06]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def SKLWriteCMOVA_CMOVBErm : SchedWriteRes<[SKLPort23,SKLPort06]> {
  let Latency = 7;
  let ReleaseAtCycles = [1,2];
  let NumMicroOps = 3;
}

def SKLCMOVA_CMOVBErr :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsCMOVArr_Or_CMOVBErr>, [SKLWriteCMOVA_CMOVBErr]>,
  SchedVar<NoSchedPred,                             [WriteCMOV]>
]>;

def SKLCMOVA_CMOVBErm :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsCMOVArm_Or_CMOVBErm>, [SKLWriteCMOVA_CMOVBErm]>,
  SchedVar<NoSchedPred,                             [WriteCMOV.Folded]>
]>;

def : InstRW<[SKLCMOVA_CMOVBErr], (instrs CMOV16rr, CMOV32rr, CMOV64rr)>;
def : InstRW<[SKLCMOVA_CMOVBErm], (instrs CMOV16rm, CMOV32rm, CMOV64rm)>;

// SETCCs that use both Z and C flag require an extra uop.
def SKLWriteSETA_SETBEr : SchedWriteRes<[SKLPort06]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def SKLWriteSETA_SETBEm : SchedWriteRes<[SKLPort4,SKLPort237,SKLPort06]> {
  let Latency = 3;
  let ReleaseAtCycles = [1,1,2];
  let NumMicroOps = 4;
}

def SKLSETA_SETBErr :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsSETAr_Or_SETBEr>, [SKLWriteSETA_SETBEr]>,
  SchedVar<NoSchedPred,                         [WriteSETCC]>
]>;

def SKLSETA_SETBErm :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsSETAm_Or_SETBEm>, [SKLWriteSETA_SETBEm]>,
  SchedVar<NoSchedPred,                         [WriteSETCCStore]>
]>;

def : InstRW<[SKLSETA_SETBErr], (instrs SETCCr)>;
def : InstRW<[SKLSETA_SETBErm], (instrs SETCCm)>;

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

def : IsZeroIdiomFunction<[
  // GPR Zero-idioms.
  DepBreakingClass<[ SUB32rr, SUB64rr, XOR32rr, XOR64rr ], ZeroIdiomPredicate>,

  // SSE Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    XORPSrr, XORPDrr,

    // int variants.
    PXORrr,
    PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
    PCMPGTBrr, PCMPGTDrr, PCMPGTQrr, PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // AVX Zero-idioms.
  DepBreakingClass<[
    // xmm fp variants.
    VXORPSrr, VXORPDrr,

    // xmm int variants.
    VPXORrr,
    VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
    VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr,

    // ymm variants.
    VXORPSYrr, VXORPDYrr, VPXORYrr,
    VPSUBBYrr, VPSUBWYrr, VPSUBDYrr, VPSUBQYrr,
    VPCMPGTBYrr, VPCMPGTWYrr, VPCMPGTDYrr, VPCMPGTQYrr
  ], ZeroIdiomPredicate>,
]>;

} // SchedModel
