//=- X86SchedIceLake.td - X86 Ice Lake Scheduling ------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Ice Lake to support
// instruction scheduling and other instruction cost heuristics.
//
// TODO: This is mainly a copy X86SchedSkylakeServer.td, but allows us to
// iteratively improve scheduling handling toward better modelling the 
// Ice Lake (Sunny/Cypress Cove) microarchitecture.
//
//===----------------------------------------------------------------------===//

def IceLakeModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and Ice Lake can
  // decode 6 instructions per cycle.
  let IssueWidth = 6;
  let MicroOpBufferSize = 352; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 14;

  // Based on the LSD (loop-stream detector) queue size and benchmarking data.
  let LoopMicroOpBufferSize = 50;

  // This flag is set to allow the scheduler to assign a default model to
  // unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = IceLakeModel in {

// Ice Lake can issue micro-ops to 8 different ports in one cycle.

// Ports 0, 1, 5, and 6 handle all computation.
// Ports 4 and 9 gets the data half of stores. Store data can be available later
// than the store address, but since we don't model the latency of stores, we
// can ignore that.
// Ports 2 and 3 are identical. They handle loads and address calculations.
// Ports 7 and 8 are identical. They handle stores address calculations.
def ICXPort0 : ProcResource<1>;
def ICXPort1 : ProcResource<1>;
def ICXPort2 : ProcResource<1>;
def ICXPort3 : ProcResource<1>;
def ICXPort4 : ProcResource<1>;
def ICXPort5 : ProcResource<1>;
def ICXPort6 : ProcResource<1>;
def ICXPort7 : ProcResource<1>;
def ICXPort8 : ProcResource<1>;
def ICXPort9 : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def ICXPort01  : ProcResGroup<[ICXPort0, ICXPort1]>;
def ICXPort23  : ProcResGroup<[ICXPort2, ICXPort3]>;
def ICXPort04  : ProcResGroup<[ICXPort0, ICXPort4]>;
def ICXPort05  : ProcResGroup<[ICXPort0, ICXPort5]>;
def ICXPort06  : ProcResGroup<[ICXPort0, ICXPort6]>;
def ICXPort15  : ProcResGroup<[ICXPort1, ICXPort5]>;
def ICXPort16  : ProcResGroup<[ICXPort1, ICXPort6]>;
def ICXPort49  : ProcResGroup<[ICXPort4, ICXPort9]>;
def ICXPort56  : ProcResGroup<[ICXPort5, ICXPort6]>;
def ICXPort78  : ProcResGroup<[ICXPort7, ICXPort8]>;
def ICXPort015 : ProcResGroup<[ICXPort0, ICXPort1, ICXPort5]>;
def ICXPort056 : ProcResGroup<[ICXPort0, ICXPort5, ICXPort6]>;
def ICXPort0156: ProcResGroup<[ICXPort0, ICXPort1, ICXPort5, ICXPort6]>;

def ICXDivider : ProcResource<1>; // Integer division issued on port 0.
// FP division and sqrt on port 0.
def ICXFPDivider : ProcResource<1>;

// 60 Entry Unified Scheduler
def ICXPortAny : ProcResGroup<[ICXPort0, ICXPort1, ICXPort2, ICXPort3, ICXPort4,
                               ICXPort5, ICXPort6, ICXPort7, ICXPort8, ICXPort9]> {
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
multiclass ICXWriteResPair<X86FoldableSchedWrite SchedRW,
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
  def : WriteRes<SchedRW.Folded, !listconcat([ICXPort23], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ReleaseAtCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// A folded store needs a cycle on port 4 for the store data, and an extra port
// 2/3/7 cycle to recompute the address.
def : WriteRes<WriteRMW, [ICXPort78,ICXPort49]>;

// Arithmetic.
defm : ICXWriteResPair<WriteALU,    [ICXPort0156], 1>; // Simple integer ALU op.
defm : ICXWriteResPair<WriteADC,    [ICXPort06],   1>; // Integer ALU + flags op.

// Integer multiplication.
defm : ICXWriteResPair<WriteIMul8,     [ICXPort1],   3>;
defm : ICXWriteResPair<WriteIMul16,    [ICXPort1,ICXPort06,ICXPort0156], 4, [1,1,2], 4>;
defm : X86WriteRes<WriteIMul16Imm,     [ICXPort1,ICXPort0156], 4, [1,1], 2>;
defm : X86WriteRes<WriteIMul16ImmLd,   [ICXPort1,ICXPort0156,ICXPort23], 8, [1,1,1], 3>;
defm : X86WriteRes<WriteIMul16Reg,     [ICXPort1],   3, [1], 1>;
defm : X86WriteRes<WriteIMul16RegLd,   [ICXPort1,ICXPort0156,ICXPort23], 8, [1,1,1], 3>;
defm : ICXWriteResPair<WriteIMul32,    [ICXPort1,ICXPort06,ICXPort0156], 4, [1,1,1], 3>;
defm : ICXWriteResPair<WriteMULX32,    [ICXPort1,ICXPort06,ICXPort0156], 3, [1,1,1], 3>;
defm : ICXWriteResPair<WriteIMul32Imm, [ICXPort1],   3>;
defm : ICXWriteResPair<WriteIMul32Reg, [ICXPort1],   3>;
defm : ICXWriteResPair<WriteIMul64,    [ICXPort1,ICXPort5], 4, [1,1], 2>;
defm : ICXWriteResPair<WriteMULX64,    [ICXPort1,ICXPort5], 3, [1,1], 2>;
defm : ICXWriteResPair<WriteIMul64Imm, [ICXPort1],   3>;
defm : ICXWriteResPair<WriteIMul64Reg, [ICXPort1],   3>;
def ICXWriteIMulH : WriteRes<WriteIMulH, []> { let Latency = 4; }
def  : WriteRes<WriteIMulHLd, []> {
  let Latency = !add(ICXWriteIMulH.Latency, SkylakeServerModel.LoadLatency);
}

defm : X86WriteRes<WriteBSWAP32, [ICXPort15], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64, [ICXPort06, ICXPort15], 2, [1,1], 2>;
defm : X86WriteRes<WriteCMPXCHG,[ICXPort06, ICXPort0156], 5, [2,3], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW,[ICXPort23,ICXPort06,ICXPort0156,ICXPort78,ICXPort49], 8, [1,2,1,1,1], 6>;
defm : X86WriteRes<WriteXCHG,       [ICXPort0156], 2, [3], 3>;

// TODO: Why isn't the ICXDivider used?
defm : ICXWriteResPair<WriteDiv8,  [ICXPort0, ICXDivider], 25, [1,10], 1, 4>;
defm : X86WriteRes<WriteDiv16,     [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort05,ICXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv32,     [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort05,ICXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv64,     [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort05,ICXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv16Ld,   [ICXPort0,ICXPort23,ICXDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv32Ld,   [ICXPort0,ICXPort23,ICXDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv64Ld,   [ICXPort0,ICXPort23,ICXDivider], 29, [1,1,10], 2>;

defm : X86WriteRes<WriteIDiv8,     [ICXPort0, ICXDivider], 25, [1,10], 1>;
defm : X86WriteRes<WriteIDiv16,    [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort06,ICXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv32,    [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort06,ICXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv64,    [ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort06,ICXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv8Ld,   [ICXPort0,ICXPort5,ICXPort23,ICXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv16Ld,  [ICXPort0,ICXPort5,ICXPort23,ICXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv32Ld,  [ICXPort0,ICXPort5,ICXPort23,ICXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv64Ld,  [ICXPort0,ICXPort5,ICXPort23,ICXPort0156], 28, [2,4,1,1], 8>;

defm : ICXWriteResPair<WriteCRC32, [ICXPort1], 3>;

def : WriteRes<WriteLEA, [ICXPort15]>; // LEA instructions can't fold loads.

defm : ICXWriteResPair<WriteCMOV,  [ICXPort06], 1, [1], 1>; // Conditional move.
defm : X86WriteRes<WriteFCMOV, [ICXPort1], 3, [1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [ICXPort06]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [ICXPort06,ICXPort49,ICXPort78]> {
  let Latency = 2;
  let NumMicroOps = 3;
}
defm : X86WriteRes<WriteLAHFSAHF,        [ICXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [ICXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [ICXPort06,ICXPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [ICXPort0156,ICXPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [ICXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [ICXPort06,ICXPort23], 5, [1,1], 3>;
defm : X86WriteRes<WriteBitTestSetRegLd, [ICXPort0156,ICXPort23], 5, [1,1], 2>;

// Integer shifts and rotates.
defm : ICXWriteResPair<WriteShift,    [ICXPort06],  1>;
defm : ICXWriteResPair<WriteShiftCL,  [ICXPort06],  3, [3], 3>;
defm : ICXWriteResPair<WriteRotate,   [ICXPort06],  1, [1], 1>;
defm : ICXWriteResPair<WriteRotateCL, [ICXPort06],  3, [3], 3>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [ICXPort1], 3, [1], 1>;
defm : X86WriteRes<WriteSHDrrcl,[ICXPort1,ICXPort06,ICXPort0156], 6, [1, 2, 1], 4>;
defm : X86WriteRes<WriteSHDmri, [ICXPort1,ICXPort23,ICXPort78,ICXPort0156], 9, [1, 1, 1, 1], 4>;
defm : X86WriteRes<WriteSHDmrcl,[ICXPort1,ICXPort23,ICXPort78,ICXPort06,ICXPort0156], 11, [1, 1, 1, 2, 1], 6>;

// Bit counts.
defm : ICXWriteResPair<WriteBSF, [ICXPort1], 3>;
defm : ICXWriteResPair<WriteBSR, [ICXPort1], 3>;
defm : ICXWriteResPair<WriteLZCNT,          [ICXPort1], 3>;
defm : ICXWriteResPair<WriteTZCNT,          [ICXPort1], 3>;
defm : ICXWriteResPair<WritePOPCNT,         [ICXPort1], 3>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : ICXWriteResPair<WriteBEXTR, [ICXPort06,ICXPort15], 2, [1,1], 2>;
defm : ICXWriteResPair<WriteBLS,   [ICXPort15], 1>;
defm : ICXWriteResPair<WriteBZHI,  [ICXPort15], 1>;

// Loads, stores, and moves, not folded with other operations.
defm : X86WriteRes<WriteLoad,    [ICXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteStore,   [ICXPort78, ICXPort49], 1, [1,1], 1>;
defm : X86WriteRes<WriteStoreNT, [ICXPort78, ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteMove,    [ICXPort0156], 1, [1], 1>;

// Model the effect of clobbering the read-write mask operand of the GATHER operation.
// Does not cost anything by itself, only has latency, matching that of the WriteLoad,
defm : X86WriteRes<WriteVecMaskedGatherWriteback, [], 5, [], 0>;

// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
def : WriteRes<WriteZero,  []>;

// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
defm : ICXWriteResPair<WriteJump,  [ICXPort06],   1>;

// Floating point. This covers both scalar and vector operations.
defm : X86WriteRes<WriteFLD0,          [ICXPort05], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,          [ICXPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLDC,          [ICXPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLoad,         [ICXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [ICXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [ICXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [ICXPort23,ICXPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedLoadY,  [ICXPort23,ICXPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteFStore,        [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreX,       [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreY,       [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNT,      [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTX,     [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTY,     [ICXPort78,ICXPort49], 1, [1,1], 2>;

defm : X86WriteRes<WriteFMaskedStore32,  [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore32Y, [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore64,  [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore64Y, [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;

defm : X86WriteRes<WriteFMove,         [ICXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [ICXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [ICXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveZ,        [ICXPort05],  1, [1], 1>;
defm : X86WriteRes<WriteEMMS,          [ICXPort05,ICXPort0156], 10, [9,1], 10>;

defm : ICXWriteResPair<WriteFAdd,      [ICXPort01],  4, [1], 1, 5>; // Floating point add/sub.
defm : ICXWriteResPair<WriteFAddX,     [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFAddY,     [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFAddZ,     [ICXPort0],   4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFAdd64,    [ICXPort01],  4, [1], 1, 5>; // Floating point double add/sub.
defm : ICXWriteResPair<WriteFAdd64X,   [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFAdd64Y,   [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFAdd64Z,   [ICXPort0],   4, [1], 1, 7>;

defm : ICXWriteResPair<WriteFCmp,      [ICXPort01],  4, [1], 1, 5>; // Floating point compare.
defm : ICXWriteResPair<WriteFCmpX,     [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFCmpY,     [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFCmpZ,     [ICXPort05],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFCmp64,    [ICXPort01],  4, [1], 1, 5>; // Floating point double compare.
defm : ICXWriteResPair<WriteFCmp64X,   [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFCmp64Y,   [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFCmp64Z,   [ICXPort05],  4, [1], 1, 7>;

defm : ICXWriteResPair<WriteFCom,       [ICXPort0],  2>; // Floating point compare to flags (X87).
defm : ICXWriteResPair<WriteFComX,      [ICXPort0],  2>; // Floating point compare to flags (SSE).

defm : ICXWriteResPair<WriteFMul,      [ICXPort01],  4, [1], 1, 5>; // Floating point multiplication.
defm : ICXWriteResPair<WriteFMulX,     [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFMulY,     [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFMulZ,     [ICXPort0],   4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFMul64,    [ICXPort01],  4, [1], 1, 5>; // Floating point double multiplication.
defm : ICXWriteResPair<WriteFMul64X,   [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFMul64Y,   [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFMul64Z,   [ICXPort0],   4, [1], 1, 7>;

defm : ICXWriteResPair<WriteFDiv,     [ICXPort0,ICXFPDivider], 11, [1,3], 1, 5>; // 10-14 cycles. // Floating point division.
defm : ICXWriteResPair<WriteFDivX,    [ICXPort0,ICXFPDivider], 11, [1,3], 1, 6>; // 10-14 cycles.
defm : ICXWriteResPair<WriteFDivY,    [ICXPort0,ICXFPDivider], 11, [1,5], 1, 7>; // 10-14 cycles.
defm : ICXWriteResPair<WriteFDivZ,    [ICXPort0,ICXPort5,ICXFPDivider], 18, [2,1,10], 3, 7>; // 10-14 cycles.
defm : ICXWriteResPair<WriteFDiv64,   [ICXPort0,ICXFPDivider], 14, [1,4], 1, 5>; // 10-14 cycles. // Floating point division.
defm : ICXWriteResPair<WriteFDiv64X,  [ICXPort0,ICXFPDivider], 14, [1,4], 1, 6>; // 10-14 cycles.
defm : ICXWriteResPair<WriteFDiv64Y,  [ICXPort0,ICXFPDivider], 14, [1,8], 1, 7>; // 10-14 cycles.
defm : ICXWriteResPair<WriteFDiv64Z,  [ICXPort0,ICXPort5,ICXFPDivider], 23, [2,1,16], 3, 7>; // 10-14 cycles.

defm : ICXWriteResPair<WriteFSqrt,    [ICXPort0,ICXFPDivider], 12, [1,3], 1, 5>; // Floating point square root.
defm : ICXWriteResPair<WriteFSqrtX,   [ICXPort0,ICXFPDivider], 12, [1,3], 1, 6>;
defm : ICXWriteResPair<WriteFSqrtY,   [ICXPort0,ICXFPDivider], 12, [1,6], 1, 7>;
defm : ICXWriteResPair<WriteFSqrtZ,   [ICXPort0,ICXPort5,ICXFPDivider], 20, [2,1,12], 3, 7>;
defm : ICXWriteResPair<WriteFSqrt64,  [ICXPort0,ICXFPDivider], 18, [1,6], 1, 5>; // Floating point double square root.
defm : ICXWriteResPair<WriteFSqrt64X, [ICXPort0,ICXFPDivider], 18, [1,6], 1, 6>;
defm : ICXWriteResPair<WriteFSqrt64Y, [ICXPort0,ICXFPDivider], 18, [1,12],1, 7>;
defm : ICXWriteResPair<WriteFSqrt64Z, [ICXPort0,ICXPort5,ICXFPDivider], 32, [2,1,24], 3, 7>;
defm : ICXWriteResPair<WriteFSqrt80,  [ICXPort0,ICXFPDivider], 21, [1,7]>; // Floating point long double square root.

defm : ICXWriteResPair<WriteFRcp,   [ICXPort0],  4, [1], 1, 5>; // Floating point reciprocal estimate.
defm : ICXWriteResPair<WriteFRcpX,  [ICXPort0],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFRcpY,  [ICXPort0],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFRcpZ,  [ICXPort0,ICXPort5],  4, [2,1], 3, 7>;

defm : ICXWriteResPair<WriteFRsqrt, [ICXPort0],  4, [1], 1, 5>; // Floating point reciprocal square root estimate.
defm : ICXWriteResPair<WriteFRsqrtX,[ICXPort0],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFRsqrtY,[ICXPort0],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFRsqrtZ,[ICXPort0,ICXPort5],  9, [2,1], 3, 7>;

defm : ICXWriteResPair<WriteFMA,  [ICXPort01],  4, [1], 1, 5>; // Fused Multiply Add.
defm : ICXWriteResPair<WriteFMAX, [ICXPort01],  4, [1], 1, 6>;
defm : ICXWriteResPair<WriteFMAY, [ICXPort01],  4, [1], 1, 7>;
defm : ICXWriteResPair<WriteFMAZ, [ICXPort0],   4, [1], 1, 7>;
defm : ICXWriteResPair<WriteDPPD, [ICXPort5,ICXPort015],  9, [1,2], 3, 6>; // Floating point double dot product.
defm : ICXWriteResPair<WriteDPPS, [ICXPort5,ICXPort015], 13, [1,3], 4, 6>;
defm : ICXWriteResPair<WriteDPPSY,[ICXPort5,ICXPort015], 13, [1,3], 4, 7>;
defm : ICXWriteResPair<WriteFSign,  [ICXPort0],  1>; // Floating point fabs/fchs.
defm : ICXWriteResPair<WriteFRnd,   [ICXPort01], 8, [2], 2, 6>; // Floating point rounding.
defm : ICXWriteResPair<WriteFRndY,  [ICXPort01], 8, [2], 2, 7>;
defm : ICXWriteResPair<WriteFRndZ,  [ICXPort05], 8, [2], 2, 7>;
defm : ICXWriteResPair<WriteFLogic, [ICXPort015], 1, [1], 1, 6>; // Floating point and/or/xor logicals.
defm : ICXWriteResPair<WriteFLogicY, [ICXPort015], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFLogicZ, [ICXPort05], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFTest,  [ICXPort0], 2, [1], 1, 6>; // Floating point TEST instructions.
defm : ICXWriteResPair<WriteFTestY, [ICXPort0], 2, [1], 1, 7>;
defm : ICXWriteResPair<WriteFTestZ, [ICXPort0], 2, [1], 1, 7>;
defm : ICXWriteResPair<WriteFShuffle,  [ICXPort15], 1, [1], 1, 6>; // Floating point vector shuffles.
defm : ICXWriteResPair<WriteFShuffleY, [ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFShuffleZ, [ICXPort5],  1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFVarShuffle,  [ICXPort15], 1, [1], 1, 6>; // Floating point vector variable shuffles.
defm : ICXWriteResPair<WriteFVarShuffleY, [ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFVarShuffleZ, [ICXPort5],  1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFBlend, [ICXPort015], 1, [1], 1, 6>; // Floating point vector blends.
defm : ICXWriteResPair<WriteFBlendY,[ICXPort015], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFBlendZ,[ICXPort015], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteFVarBlend, [ICXPort015], 2, [2], 2, 6>; // Fp vector variable blends.
defm : ICXWriteResPair<WriteFVarBlendY,[ICXPort015], 2, [2], 2, 7>;
defm : ICXWriteResPair<WriteFVarBlendZ,[ICXPort015], 2, [2], 2, 7>;

// FMA Scheduling helper class.
// class FMASC { X86FoldableSchedWrite Sched = WriteFAdd; }

// Vector integer operations.
defm : X86WriteRes<WriteVecLoad,         [ICXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [ICXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [ICXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [ICXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [ICXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [ICXPort23,ICXPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [ICXPort23,ICXPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecStore,        [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreX,       [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreY,       [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNT,      [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNTY,     [ICXPort78,ICXPort49], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore32,  [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore32Y, [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore64,  [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore64Y, [ICXPort78,ICXPort49,ICXPort0], 2, [1,1,1], 2>;
defm : X86WriteRes<WriteVecMove,         [ICXPort05],  1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [ICXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [ICXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveZ,        [ICXPort05],  1, [1], 1>;
defm : X86WriteRes<WriteVecMoveToGpr,    [ICXPort0], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [ICXPort5], 1, [1], 1>;

defm : ICXWriteResPair<WriteVecALU,   [ICXPort05], 1, [1], 1, 5>; // Vector integer ALU op, no logicals.
defm : ICXWriteResPair<WriteVecALUX,  [ICXPort01], 1, [1], 1, 6>;
defm : ICXWriteResPair<WriteVecALUY,  [ICXPort01], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecALUZ,  [ICXPort0], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecLogic, [ICXPort05],  1, [1], 1, 5>; // Vector integer and/or/xor.
defm : ICXWriteResPair<WriteVecLogicX,[ICXPort015], 1, [1], 1, 6>;
defm : ICXWriteResPair<WriteVecLogicY,[ICXPort015], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecLogicZ,[ICXPort05], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecTest,  [ICXPort0,ICXPort5], 3, [1,1], 2, 6>; // Vector integer TEST instructions.
defm : ICXWriteResPair<WriteVecTestY, [ICXPort0,ICXPort5], 3, [1,1], 2, 7>;
defm : ICXWriteResPair<WriteVecTestZ, [ICXPort0,ICXPort5], 3, [1,1], 2, 7>;
defm : ICXWriteResPair<WriteVecIMul,  [ICXPort0],   5, [1], 1, 5>; // Vector integer multiply.
defm : ICXWriteResPair<WriteVecIMulX, [ICXPort01],  5, [1], 1, 6>;
defm : ICXWriteResPair<WriteVecIMulY, [ICXPort01],  5, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecIMulZ, [ICXPort05],  5, [1], 1, 7>;
defm : ICXWriteResPair<WritePMULLD,   [ICXPort01], 10, [2], 2, 6>; // Vector PMULLD.
defm : ICXWriteResPair<WritePMULLDY,  [ICXPort01], 10, [2], 2, 7>;
defm : ICXWriteResPair<WritePMULLDZ,  [ICXPort05], 10, [2], 2, 7>;
defm : ICXWriteResPair<WriteShuffle,  [ICXPort5],  1, [1], 1, 5>; // Vector shuffles.
defm : ICXWriteResPair<WriteShuffleX, [ICXPort15], 1, [1], 1, 6>;
defm : ICXWriteResPair<WriteShuffleY, [ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteShuffleZ, [ICXPort5],  1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVarShuffle,  [ICXPort5],  1, [1], 1, 5>; // Vector variable shuffles.
defm : ICXWriteResPair<WriteVarShuffleX, [ICXPort15], 1, [1], 1, 6>;
defm : ICXWriteResPair<WriteVarShuffleY, [ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVarShuffleZ, [ICXPort5],  1, [1], 1, 7>;
defm : ICXWriteResPair<WriteBlend, [ICXPort15], 1, [1], 1, 6>; // Vector blends.
defm : ICXWriteResPair<WriteBlendY,[ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteBlendZ,[ICXPort15], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVarBlend, [ICXPort015], 2, [2], 2, 6>; // Vector variable blends.
defm : ICXWriteResPair<WriteVarBlendY,[ICXPort015], 2, [2], 2, 6>;
defm : ICXWriteResPair<WriteVarBlendZ,[ICXPort05],  2, [1], 1, 6>;
defm : ICXWriteResPair<WriteMPSAD,   [ICXPort15,ICXPort5], 4, [1,1], 2, 6>; // Vector MPSAD.
defm : ICXWriteResPair<WriteMPSADY,  [ICXPort15,ICXPort5], 4, [1,1], 2, 7>;
defm : ICXWriteResPair<WriteMPSADZ,  [ICXPort15,ICXPort5], 4, [1,1], 2, 7>;
defm : ICXWriteResPair<WritePSADBW,  [ICXPort5], 3, [1], 1, 5>; // Vector PSADBW.
defm : ICXWriteResPair<WritePSADBWX, [ICXPort5], 3, [1], 1, 6>;
defm : ICXWriteResPair<WritePSADBWY, [ICXPort5], 3, [1], 1, 7>;
defm : ICXWriteResPair<WritePSADBWZ, [ICXPort5], 3, [1], 1, 7>; // TODO: 512-bit ops require ports 0/1 to be joined.
defm : ICXWriteResPair<WritePHMINPOS, [ICXPort0], 4, [1], 1, 6>; // Vector PHMINPOS.

// Vector integer shifts.
defm : ICXWriteResPair<WriteVecShift, [ICXPort0], 1, [1], 1, 5>;
defm : X86WriteRes<WriteVecShiftX,    [ICXPort01,ICXPort15], 2, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftY,    [ICXPort01,ICXPort5],  4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZ,    [ICXPort0,ICXPort5],   4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftXLd,  [ICXPort01,ICXPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftYLd,  [ICXPort01,ICXPort23], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZLd,  [ICXPort0,ICXPort23],  8, [1,1], 2>;

defm : ICXWriteResPair<WriteVecShiftImm,  [ICXPort0],  1, [1], 1, 5>;
defm : ICXWriteResPair<WriteVecShiftImmX, [ICXPort01], 1, [1], 1, 6>; // Vector integer immediate shifts.
defm : ICXWriteResPair<WriteVecShiftImmY, [ICXPort01], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVecShiftImmZ, [ICXPort0], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVarVecShift,  [ICXPort01], 1, [1], 1, 6>; // Variable vector shifts.
defm : ICXWriteResPair<WriteVarVecShiftY, [ICXPort01], 1, [1], 1, 7>;
defm : ICXWriteResPair<WriteVarVecShiftZ, [ICXPort0], 1, [1], 1, 7>;

// Vector insert/extract operations.
def : WriteRes<WriteVecInsert, [ICXPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : WriteRes<WriteVecInsertLd, [ICXPort5,ICXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def: InstRW<[WriteVecInsertLd], (instregex "(V?)MOV(H|L)(PD|PS)rm")>;

def : WriteRes<WriteVecExtract, [ICXPort0,ICXPort5]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecExtractSt, [ICXPort49,ICXPort5,ICXPort78]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// Conversion between integer and float.
defm : ICXWriteResPair<WriteCvtSS2I,   [ICXPort01], 6, [2], 2>; // Needs more work: DD vs DQ.
defm : ICXWriteResPair<WriteCvtPS2I,   [ICXPort01], 3>;
defm : ICXWriteResPair<WriteCvtPS2IY,  [ICXPort01], 3>;
defm : ICXWriteResPair<WriteCvtPS2IZ,  [ICXPort05], 3>;
defm : ICXWriteResPair<WriteCvtSD2I,   [ICXPort01], 6, [2], 2>;
defm : ICXWriteResPair<WriteCvtPD2I,   [ICXPort01], 3>;
defm : ICXWriteResPair<WriteCvtPD2IY,  [ICXPort01], 3>;
defm : ICXWriteResPair<WriteCvtPD2IZ,  [ICXPort05], 3>;

defm : ICXWriteResPair<WriteCvtI2SS,   [ICXPort1], 4>;
defm : ICXWriteResPair<WriteCvtI2PS,   [ICXPort01], 4>;
defm : ICXWriteResPair<WriteCvtI2PSY,  [ICXPort01], 4>;
defm : ICXWriteResPair<WriteCvtI2PSZ,  [ICXPort05], 4>;  // Needs more work: DD vs DQ.
defm : ICXWriteResPair<WriteCvtI2SD,   [ICXPort1], 4>;
defm : ICXWriteResPair<WriteCvtI2PD,   [ICXPort01], 4>;
defm : ICXWriteResPair<WriteCvtI2PDY,  [ICXPort01], 4>;
defm : ICXWriteResPair<WriteCvtI2PDZ,  [ICXPort05], 4>;

defm : ICXWriteResPair<WriteCvtSS2SD,  [ICXPort1], 3>;
defm : ICXWriteResPair<WriteCvtPS2PD,  [ICXPort1], 3>;
defm : ICXWriteResPair<WriteCvtPS2PDY, [ICXPort5,ICXPort01], 3, [1,1], 2>;
defm : ICXWriteResPair<WriteCvtPS2PDZ, [ICXPort05], 3, [2], 2>;
defm : ICXWriteResPair<WriteCvtSD2SS,  [ICXPort5,ICXPort01], 5, [1,1], 2, 5>;
defm : ICXWriteResPair<WriteCvtPD2PS,  [ICXPort5,ICXPort01], 5, [1,1], 2, 6>;
defm : ICXWriteResPair<WriteCvtPD2PSY, [ICXPort5,ICXPort01], 7, [1,1], 2, 7>;
defm : ICXWriteResPair<WriteCvtPD2PSZ, [ICXPort5,ICXPort0],  7, [1,1], 2, 7>;

defm : X86WriteRes<WriteCvtPH2PS,     [ICXPort5,ICXPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSY,    [ICXPort5,ICXPort01],  7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZ,    [ICXPort5,ICXPort0],   7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSLd,  [ICXPort23,ICXPort01],  9, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSYLd, [ICXPort23,ICXPort01], 10, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZLd, [ICXPort23,ICXPort05], 10, [1,1], 2>;

defm : X86WriteRes<WriteCvtPS2PH,    [ICXPort5,ICXPort01], 5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHY,   [ICXPort5,ICXPort01], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHZ,   [ICXPort5,ICXPort05], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHSt,  [ICXPort49,ICXPort5,ICXPort78,ICXPort01], 6, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [ICXPort49,ICXPort5,ICXPort78,ICXPort01], 8, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHZSt, [ICXPort49,ICXPort5,ICXPort78,ICXPort05], 8, [1,1,1,1], 4>;

// Strings instructions.

// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [ICXPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : WriteRes<WritePCmpIStrMLd, [ICXPort0, ICXPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [ICXPort0, ICXPort5, ICXPort015, ICXPort0156]> {
  let Latency = 19;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [4,3,1,1];
}
def : WriteRes<WritePCmpEStrMLd, [ICXPort0, ICXPort5, ICXPort23, ICXPort015, ICXPort0156]> {
  let Latency = 25;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [4,3,1,1,1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [ICXPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : WriteRes<WritePCmpIStrILd, [ICXPort0, ICXPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [ICXPort0,ICXPort5,ICXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [4,3,1];
}
def : WriteRes<WritePCmpEStrILd, [ICXPort0, ICXPort5, ICXPort23, ICXPort0156]> {
  let Latency = 24;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [4,3,1,1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [ICXPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSK,  [ICXPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSKY, [ICXPort0]> { let Latency = 2; }
def : WriteRes<WriteMMXMOVMSK,  [ICXPort0]> { let Latency = 2; }

// AES instructions.
def : WriteRes<WriteAESDecEnc, [ICXPort0]> { // Decryption, encryption.
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def : WriteRes<WriteAESDecEncLd, [ICXPort0, ICXPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}

def : WriteRes<WriteAESIMC, [ICXPort0]> { // InvMixColumn.
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : WriteRes<WriteAESIMCLd, [ICXPort0, ICXPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}

def : WriteRes<WriteAESKeyGen, [ICXPort0,ICXPort5,ICXPort015]> { // Key Generation.
  let Latency = 20;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [3,6,2];
}
def : WriteRes<WriteAESKeyGenLd, [ICXPort0,ICXPort5,ICXPort23,ICXPort015]> {
  let Latency = 25;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [3,6,1,1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [ICXPort5]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def : WriteRes<WriteCLMulLd, [ICXPort5, ICXPort23]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}

// Catch-all for expensive system instructions.
def : WriteRes<WriteSystem,     [ICXPort0156]> { let Latency = 100; } // def WriteSystem : SchedWrite;

// AVX2.
defm : ICXWriteResPair<WriteFShuffle256, [ICXPort5], 3, [1], 1, 7>; // Fp 256-bit width vector shuffles.
defm : ICXWriteResPair<WriteFVarShuffle256, [ICXPort5], 3, [1], 1, 7>; // Fp 256-bit width vector variable shuffles.
defm : ICXWriteResPair<WriteShuffle256, [ICXPort5], 3, [1], 1, 7>;  // 256-bit width vector shuffles.
defm : ICXWriteResPair<WriteVPMOV256, [ICXPort5], 3, [1], 1, 7>;  // 256-bit width packed vector width-changing move.
defm : ICXWriteResPair<WriteVarShuffle256, [ICXPort5], 3, [1], 1, 7>;  // 256-bit width vector variable shuffles.

// Old microcoded instructions that nobody use.
def : WriteRes<WriteMicrocoded, [ICXPort0156]> { let Latency = 100; } // def WriteMicrocoded : SchedWrite;

// Fence instructions.
def : WriteRes<WriteFence,  [ICXPort78, ICXPort49]>;

// Load/store MXCSR.
def : WriteRes<WriteLDMXCSR, [ICXPort0,ICXPort23,ICXPort0156]> { let Latency = 7; let NumMicroOps = 3; let ReleaseAtCycles = [1,1,1]; }
def : WriteRes<WriteSTMXCSR, [ICXPort49,ICXPort5,ICXPort78]> { let Latency = 2; let NumMicroOps = 3; let ReleaseAtCycles = [1,1,1]; }

// Nop, not very useful expect it provides a model for nops!
def : WriteRes<WriteNop, []>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : ICXWriteResPair<WriteFHAdd,  [ICXPort5,ICXPort01], 6, [2,1], 3, 6>;
defm : ICXWriteResPair<WriteFHAddY, [ICXPort5,ICXPort01], 6, [2,1], 3, 7>;
defm : ICXWriteResPair<WritePHAdd,  [ICXPort5,ICXPort05],  3, [2,1], 3, 5>;
defm : ICXWriteResPair<WritePHAddX, [ICXPort15,ICXPort015], 3, [2,1], 3, 6>;
defm : ICXWriteResPair<WritePHAddY, [ICXPort15,ICXPort015], 3, [2,1], 3, 7>;

// Remaining instrs.

def ICXWriteResGroup1 : SchedWriteRes<[ICXPort0]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup1], (instregex "KAND(B|D|Q|W)rr",
                                            "KANDN(B|D|Q|W)rr",
                                            "KMOV(B|D|Q|W)kk",
                                            "KNOT(B|D|Q|W)rr",
                                            "KOR(B|D|Q|W)rr",
                                            "KXNOR(B|D|Q|W)rr",
                                            "KXOR(B|D|Q|W)rr",
                                            "KSET0(B|D|Q|W)", // Same as KXOR
                                            "KSET1(B|D|Q|W)", // Same as KXNOR
                                            "MMX_PADDS(B|W)rr",
                                            "MMX_PADDUS(B|W)rr",
                                            "MMX_PAVG(B|W)rr",
                                            "MMX_PCMPEQ(B|D|W)rr",
                                            "MMX_PCMPGT(B|D|W)rr",
                                            "MMX_P(MAX|MIN)SWrr",
                                            "MMX_P(MAX|MIN)UBrr",
                                            "MMX_PSUBS(B|W)rr",
                                            "MMX_PSUBUS(B|W)rr",
                                            "VPMOVB2M(Z|Z128|Z256)rr",
                                            "VPMOVD2M(Z|Z128|Z256)rr",
                                            "VPMOVQ2M(Z|Z128|Z256)rr",
                                            "VPMOVW2M(Z|Z128|Z256)rr")>;

def ICXWriteResGroup3 : SchedWriteRes<[ICXPort5]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup3], (instregex "COM(P?)_FST0r",
                                            "KMOV(B|D|Q|W)kr",
                                            "UCOM_F(P?)r",
                                            "VPBROADCAST(D|Q)rr",
                                            "(V?)INSERTPS(Z?)rr",
                                            "(V?)MOV(HL|LH)PS(Z?)rr",
                                            "(V?)MOVDDUP(Y|Z128|Z256)?rr",
                                            "(V?)PALIGNR(Y|Z128|Z256)?rri",
                                            "(V?)PERMIL(PD|PS)(Y|Z128|Z256)?ri",
                                            "(V?)PERMIL(PD|PS)(Y|Z128|Z256)?rr",
                                            "(V?)UNPCK(L|H)(PD|PS)(Y|Z128|Z256)?rr")>;

def ICXWriteResGroup4 : SchedWriteRes<[ICXPort6]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup4], (instregex "JMP(16|32|64)r")>;

def ICXWriteResGroup6 : SchedWriteRes<[ICXPort05]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup6], (instrs FINCSTP, FNOP)>;

def ICXWriteResGroup7 : SchedWriteRes<[ICXPort06]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup7], (instrs CDQ, CQO, CLAC, STAC)>;

def ICXWriteResGroup8 : SchedWriteRes<[ICXPort15]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup8], (instregex "ANDN(32|64)rr")>;

def ICXWriteResGroup9 : SchedWriteRes<[ICXPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup9], (instregex "VBLENDMPD(Z128|Z256)rr",
                                            "VBLENDMPS(Z128|Z256)rr",
                                            "VPADD(B|D|Q|W)(Y|Z|Z128|Z256)rr",
                                            "(V?)PADD(B|D|Q|W)rr",
                                            "(V?)MOV(SD|SS)(Z?)rr",
                                            "VPBLENDD(Y?)rri",
                                            "VPBLENDMB(Z128|Z256)rr",
                                            "VPBLENDMD(Z128|Z256)rr",
                                            "VPBLENDMQ(Z128|Z256)rr",
                                            "VPBLENDMW(Z128|Z256)rr",
                                            "VPSUB(B|D|Q|W)(Y|Z|Z128|Z256)rrk",
                                            "VPTERNLOGD(Z|Z128|Z256)rri",
                                            "VPTERNLOGQ(Z|Z128|Z256)rri")>;

def ICXWriteResGroup10 : SchedWriteRes<[ICXPort0156]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup10], (instrs SGDT64m,
                                          SIDT64m,
                                          SMSW16m,
                                          STRm,
                                          SYSCALL)>;

def ICXWriteResGroup11 : SchedWriteRes<[ICXPort49,ICXPort78]> {
  let Latency = 1;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup11], (instrs FBSTPm, VMPTRSTm)>;
def: InstRW<[ICXWriteResGroup11], (instregex "KMOV(B|D|Q|W)mk",
                                             "ST_FP(32|64|80)m")>;

def ICXWriteResGroup13 : SchedWriteRes<[ICXPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup13], (instrs MMX_MOVQ2DQrr)>;

def ICXWriteResGroup14 : SchedWriteRes<[ICXPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup14], (instrs FDECSTP,
                                          MMX_MOVDQ2Qrr)>;

def ICXWriteResGroup17 : SchedWriteRes<[ICXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup17], (instrs LFENCE,
                                          WAIT,
                                          XGETBV)>;

def ICXWriteResGroup20 : SchedWriteRes<[ICXPort6,ICXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup20], (instregex "CLFLUSH")>;

def ICXWriteResGroup21 : SchedWriteRes<[ICXPort49,ICXPort78]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup21], (instrs SFENCE)>;

def ICXWriteResGroup23 : SchedWriteRes<[ICXPort06,ICXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup23], (instrs CWD,
                                          JCXZ, JECXZ, JRCXZ,
                                          ADC8i8, SBB8i8,
                                          ADC16i16, SBB16i16,
                                          ADC32i32, SBB32i32,
                                          ADC64i32, SBB64i32)>;

def ICXWriteResGroup25 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort78]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup25], (instrs FNSTCW16m)>;

def ICXWriteResGroup27 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort15]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup27], (instregex "MOVBE(16|32|64)mr")>;

def ICXWriteResGroup28 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup28], (instrs PUSH16r, PUSH32r, PUSH64r, PUSH64i8,
                                          STOSB, STOSL, STOSQ, STOSW)>;
def: InstRW<[ICXWriteResGroup28], (instregex "PUSH(16|32|64)rmr")>;

def ICXWriteResGroup29 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort15]> {
  let Latency = 2;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [2,2,1];
}
def: InstRW<[ICXWriteResGroup29], (instregex "VMOVDQU8Zmr(b?)")>;

def ICXWriteResGroup30 : SchedWriteRes<[ICXPort0]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup30], (instregex "KMOV(B|D|Q|W)rk",
                                             "KORTEST(B|D|Q|W)rr",
                                             "KTEST(B|D|Q|W)rr")>;

def ICXWriteResGroup31 : SchedWriteRes<[ICXPort1]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup31], (instregex "PDEP(32|64)rr",
                                             "PEXT(32|64)rr")>;

def ICXWriteResGroup32 : SchedWriteRes<[ICXPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup32], (instregex "(ADD|SUB|SUBR)_(FPrST0|FST0r|FrST0)",
                                             "VALIGND(Z|Z128|Z256)rri",
                                             "VALIGNQ(Z|Z128|Z256)rri",
                                             "VPBROADCAST(B|W)rr",
                                             "(V?)PACK(U|S)S(DW|WB)(Y|Z|Z128|Z256)?rr",
                                             "VP(MAX|MIN)(S|U)Q(Z|Z128|Z256)rr")>;

def ICXWriteResGroup33 : SchedWriteRes<[ICXPort5]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup33], (instregex "KADD(B|D|Q|W)rr",
                                             "KSHIFTL(B|D|Q|W)ri",
                                             "KSHIFTR(B|D|Q|W)ri",
                                             "KUNPCK(BW|DQ|WD)rr",
                                             "VCMPPD(Z|Z128|Z256)rri",
                                             "VCMPPS(Z|Z128|Z256)rri",
                                             "VCMP(SD|SS)Zrr",
                                             "VFPCLASS(PD|PS)(Z|Z128|Z256)rr",
                                             "VFPCLASS(SD|SS)Zrr",
                                             "VPCMPB(Z|Z128|Z256)rri",
                                             "VPCMPD(Z|Z128|Z256)rri",
                                             "VPCMPEQ(B|D|Q|W)(Z|Z128|Z256)rr",
                                             "VPCMPGT(B|D|Q|W)(Z|Z128|Z256)rr",
                                             "VPCMPQ(Z|Z128|Z256)rri",
                                             "VPCMPU(B|D|Q|W)(Z|Z128|Z256)rri",
                                             "VPCMPW(Z|Z128|Z256)rri",
                                             "VPTEST(N?)M(B|D|Q|W)(Z|Z128|Z256)rr")>;

def ICXWriteResGroup34 : SchedWriteRes<[ICXPort0,ICXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup34], (instrs FNSTSW16r)>;

def ICXWriteResGroup37 : SchedWriteRes<[ICXPort0,ICXPort5]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[ICXWriteResGroup37], (instregex "MMX_PH(ADD|SUB)SWrr")>;

def ICXWriteResGroup38 : SchedWriteRes<[ICXPort15,ICXPort01]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup38], (instregex "(V?)PH(ADD|SUB)SW(Y?)rr")>;

def ICXWriteResGroup41 : SchedWriteRes<[ICXPort5,ICXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup41], (instrs MMX_PACKSSDWrr,
                                          MMX_PACKSSWBrr,
                                          MMX_PACKUSWBrr)>;

def ICXWriteResGroup42 : SchedWriteRes<[ICXPort6,ICXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[ICXWriteResGroup42], (instregex "CLD")>;

def ICXWriteResGroup43 : SchedWriteRes<[ICXPort49,ICXPort78]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[ICXWriteResGroup43], (instrs MFENCE)>;

def ICXWriteResGroup44 : SchedWriteRes<[ICXPort06,ICXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[ICXWriteResGroup44], (instrs RCL8r1, RCL16r1, RCL32r1, RCL64r1,
                                          RCR8r1, RCR16r1, RCR32r1, RCR64r1)>;

def ICXWriteResGroup44b : SchedWriteRes<[ICXPort1,ICXPort06,ICXPort0156]> {
  let Latency = 5;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [2,3,2];
}
def: InstRW<[ICXWriteResGroup44b], (instrs RCR8ri, RCR16ri, RCR32ri, RCR64ri)>;

def ICXWriteResGroup44c : SchedWriteRes<[ICXPort1,ICXPort06,ICXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [2,3,2];
}
def: InstRW<[ICXWriteResGroup44c], (instrs RCL8ri, RCL16ri, RCL32ri, RCL64ri)>;

def ICXWriteResGroup45 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort78]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup45], (instrs FNSTSWm)>;

def ICXWriteResGroup47 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort78,ICXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[ICXWriteResGroup47], (instregex "CALL(16|32|64)r")>;

def ICXWriteResGroup48 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[ICXWriteResGroup48], (instrs CALL64pcrel32)>;

def ICXWriteResGroup49 : SchedWriteRes<[ICXPort0]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup49], (instregex "MUL_(FPrST0|FST0r|FrST0)")>;

def ICXWriteResGroup50 : SchedWriteRes<[ICXPort01]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup50], (instregex "VCVTPD2QQ(Z128|Z256)rr",
                                             "VCVTPD2UQQ(Z128|Z256)rr",
                                             "VCVTPS2DQ(Y|Z128|Z256)rr",
                                             "(V?)CVTPS2DQrr",
                                             "VCVTPS2UDQ(Z128|Z256)rr",
                                             "VCVTTPD2QQ(Z128|Z256)rr",
                                             "VCVTTPD2UQQ(Z128|Z256)rr",
                                             "VCVTTPS2DQ(Z128|Z256)rr",
                                             "(V?)CVTTPS2DQrr",
                                             "VCVTTPS2UDQ(Z128|Z256)rr")>;

def ICXWriteResGroup50z : SchedWriteRes<[ICXPort05]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup50z], (instrs VCVTPD2QQZrr,
                                           VCVTPD2UQQZrr,
                                           VCVTPS2DQZrr,
                                           VCVTPS2UDQZrr,
                                           VCVTTPD2QQZrr,
                                           VCVTTPD2UQQZrr,
                                           VCVTTPS2DQZrr,
                                           VCVTTPS2UDQZrr)>;

def ICXWriteResGroup51 : SchedWriteRes<[ICXPort5]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup51], (instregex "VEXPANDPD(Z|Z128|Z256)rr",
                                             "VEXPANDPS(Z|Z128|Z256)rr",
                                             "VPEXPANDD(Z|Z128|Z256)rr",
                                             "VPEXPANDQ(Z|Z128|Z256)rr",
                                             "VPMOVDB(Z|Z128|Z256)rr",
                                             "VPMOVDW(Z|Z128|Z256)rr",
                                             "VPMOVQB(Z|Z128|Z256)rr",
                                             "VPMOVQW(Z|Z128|Z256)rr",
                                             "VPMOVSDB(Z|Z128|Z256)rr",
                                             "VPMOVSDW(Z|Z128|Z256)rr",
                                             "VPMOVSQB(Z|Z128|Z256)rr",
                                             "VPMOVSQD(Z|Z128|Z256)rr",
                                             "VPMOVSQW(Z|Z128|Z256)rr",
                                             "VPMOVSWB(Z|Z128|Z256)rr",
                                             "VPMOVUSDB(Z|Z128|Z256)rr",
                                             "VPMOVUSDW(Z|Z128|Z256)rr",
                                             "VPMOVUSQB(Z|Z128|Z256)rr",
                                             "VPMOVUSQD(Z|Z128|Z256)rr",
                                             "VPMOVUSWB(Z|Z128|Z256)rr",
                                             "VPMOVWB(Z|Z128|Z256)rr")>;

def ICXWriteResGroup54 : SchedWriteRes<[ICXPort49,ICXPort5,ICXPort78]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup54], (instregex "IST(T?)_FP(16|32|64)m",
                                             "IST_F(16|32)m",
                                             "VPMOVQD(Z|Z128|Z256)mr(b?)")>;

def ICXWriteResGroup55 : SchedWriteRes<[ICXPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}
def: InstRW<[ICXWriteResGroup55], (instrs FNCLEX)>;

def ICXWriteResGroup56 : SchedWriteRes<[]> {
  let Latency = 0;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [];
}
def: InstRW<[ICXWriteResGroup56], (instrs VZEROUPPER)>;

def ICXWriteResGroup57 : SchedWriteRes<[ICXPort1,ICXPort6,ICXPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,2];
}
def: InstRW<[ICXWriteResGroup57], (instregex "LAR(16|32|64)rr")>;

def ICXWriteResGroup61 : SchedWriteRes<[ICXPort5,ICXPort01]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup61], (instregex "MMX_CVT(T?)PD2PIrr",
                                             "MMX_CVT(T?)PS2PIrr",
                                             "VCVTDQ2PDZ128rr",
                                             "VCVTPD2DQZ128rr",
                                             "(V?)CVT(T?)PD2DQrr",
                                             "VCVTPD2UDQZ128rr",
                                             "VCVTPS2PDZ128rr",
                                             "(V?)CVTPS2PDrr",
                                             "VCVTPS2QQZ128rr",
                                             "VCVTPS2UQQZ128rr",
                                             "VCVTQQ2PSZ128rr",
                                             "(V?)CVTSI(64)?2SDrr",
                                             "VCVTSI2SSZrr",
                                             "(V?)CVTSI2SSrr",
                                             "VCVTSI(64)?2SDZrr",
                                             "VCVTSS2SDZrr",
                                             "(V?)CVTSS2SDrr",
                                             "VCVTTPD2DQZ128rr",
                                             "VCVTTPD2UDQZ128rr",
                                             "VCVTTPS2QQZ128rr",
                                             "VCVTTPS2UQQZ128rr",
                                             "VCVTUDQ2PDZ128rr",
                                             "VCVTUQQ2PSZ128rr",
                                             "VCVTUSI2SSZrr",
                                             "VCVTUSI(64)?2SDZrr")>;

def ICXWriteResGroup62 : SchedWriteRes<[ICXPort5,ICXPort015]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup62], (instregex "VPCONFLICTQZ128rr")>;

def ICXWriteResGroup63 : SchedWriteRes<[ICXPort1,ICXPort6,ICXPort06]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup63], (instregex "STR(16|32|64)r")>;

def ICXWriteResGroup65 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort01]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup65], (instregex "VCVTPS2PHZ128mr(b?)",
                                             "VCVTPS2PHZ256mr(b?)",
                                             "VCVTPS2PHZmr(b?)")>;

def ICXWriteResGroup66 : SchedWriteRes<[ICXPort49,ICXPort5,ICXPort78]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,2,1];
}
def: InstRW<[ICXWriteResGroup66], (instregex "VPMOVDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQD(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSWB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQD(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSWB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVWB(Z|Z128|Z256)mr(b?)")>;

def ICXWriteResGroup67 : SchedWriteRes<[ICXPort06,ICXPort0156]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,4];
}
def: InstRW<[ICXWriteResGroup67], (instrs XSETBV)>;

def ICXWriteResGroup69 : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort0156]> {
  let Latency = 5;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,4];
}
def: InstRW<[ICXWriteResGroup69], (instregex "PUSHF(16|64)")>;

def ICXWriteResGroup71 : SchedWriteRes<[ICXPort23]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup71], (instrs VBROADCASTSSrm,
                                          VPBROADCASTDrm,
                                          VPBROADCASTQrm,
                                          VMOVSHDUPrm,
                                          VMOVSLDUPrm,
                                          VMOVDDUPrm,
                                          MOVSHDUPrm,
                                          MOVSLDUPrm,
                                          MOVDDUPrm)>;

def ICXWriteResGroup72 : SchedWriteRes<[ICXPort5]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup72], (instrs MMX_CVTPI2PSrr)>;
def: InstRW<[ICXWriteResGroup72], (instregex "VCOMPRESSPD(Z|Z128|Z256)rr",
                                             "VCOMPRESSPS(Z|Z128|Z256)rr",
                                             "VPCOMPRESSD(Z|Z128|Z256)rr",
                                             "VPCOMPRESSQ(Z|Z128|Z256)rr",
                                             "VPERMW(Z|Z128|Z256)rr")>;

def ICXWriteResGroup73 : SchedWriteRes<[ICXPort0,ICXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup73], (instrs MMX_PADDSBrm,
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

def ICXWriteResGroup76 : SchedWriteRes<[ICXPort6,ICXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup76], (instrs FARJMP64m)>;
def: InstRW<[ICXWriteResGroup76], (instregex "JMP(16|32|64)m")>;

def ICXWriteResGroup79 : SchedWriteRes<[ICXPort23,ICXPort15]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup79], (instregex "ANDN(32|64)rm",
                                             "MOVBE(16|32|64)rm")>;

def ICXWriteResGroup80 : SchedWriteRes<[ICXPort23,ICXPort015]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup80], (instregex "VMOV(64to|QI2)PQIZrm(b?)")>;
def: InstRW<[ICXWriteResGroup80], (instrs VMOVDI2PDIZrm)>;

def ICXWriteResGroup81 : SchedWriteRes<[ICXPort23,ICXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup81], (instrs POP16r, POP32r, POP64r)>;
def: InstRW<[ICXWriteResGroup81], (instregex "POP(16|32|64)rmr")>;

def ICXWriteResGroup82 : SchedWriteRes<[ICXPort5,ICXPort01]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup82], (instregex "(V?)CVTSI642SSrr",
                                             "VCVTSI642SSZrr",
                                             "VCVTUSI642SSZrr")>;

def ICXWriteResGroup84 : SchedWriteRes<[ICXPort1,ICXPort6,ICXPort06,ICXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[ICXWriteResGroup84], (instregex "SLDT(16|32|64)r")>;

def ICXWriteResGroup86 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort06]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[ICXWriteResGroup86], (instregex "SAR(8|16|32|64)m(1|i)",
                                             "SHL(8|16|32|64)m(1|i)",
                                             "SHR(8|16|32|64)m(1|i)")>;

def ICXWriteResGroup87 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,1,1,1];
}
def: InstRW<[ICXWriteResGroup87], (instregex "POP(16|32|64)rmm",
                                             "PUSH(16|32|64)rmm")>;

def ICXWriteResGroup88 : SchedWriteRes<[ICXPort6,ICXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,5];
}
def: InstRW<[ICXWriteResGroup88], (instrs STD)>;

def ICXWriteResGroup89 : SchedWriteRes<[ICXPort23]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup89], (instregex "LD_F(32|64|80)m")>;
def: InstRW<[ICXWriteResGroup89], (instrs VBROADCASTF128rm,
                                          VBROADCASTI128rm,
                                          VBROADCASTSDYrm,
                                          VBROADCASTSSYrm,
                                          VMOVDDUPYrm,
                                          VMOVSHDUPYrm,
                                          VMOVSLDUPYrm,
                                          VPBROADCASTDYrm,
                                          VPBROADCASTQYrm)>;

def ICXWriteResGroup90 : SchedWriteRes<[ICXPort01,ICXPort5]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup90], (instrs VCVTDQ2PDYrr)>;

def ICXWriteResGroup92 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup92], (instregex "VMOV(SD|SS)Zrm(b?)",
                                              "VPBROADCAST(B|W)(Z128)?rm",
                                             "(V?)INSERTPS(Z?)rm",
                                             "(V?)PALIGNR(Z128)?rmi",
                                             "(V?)PERMIL(PD|PS)(Z128)?m(b?)i",
                                             "(V?)PERMIL(PD|PS)(Z128)?rm",
                                             "(V?)UNPCK(L|H)(PD|PS)(Z128)?rm")>;

def ICXWriteResGroup93 : SchedWriteRes<[ICXPort5,ICXPort01]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup93], (instregex "VCVTDQ2PDZ256rr",
                                             "VCVTPD2DQ(Y|Z256)rr",
                                             "VCVTPD2UDQZ256rr",
                                             "VCVTPS2PD(Y|Z256)rr",
                                             "VCVTPS2QQZ256rr",
                                             "VCVTPS2UQQZ256rr",
                                             "VCVTQQ2PSZ256rr",
                                             "VCVTTPD2DQ(Y|Z256)rr",
                                             "VCVTTPD2UDQZ256rr",
                                             "VCVTTPS2QQZ256rr",
                                             "VCVTTPS2UQQZ256rr",
                                             "VCVTUDQ2PDZ256rr",
                                             "VCVTUQQ2PSZ256rr")>;

def ICXWriteResGroup93z : SchedWriteRes<[ICXPort5,ICXPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup93z], (instrs VCVTDQ2PDZrr,
                                           VCVTPD2DQZrr,
                                           VCVTPD2UDQZrr,
                                           VCVTPS2PDZrr,
                                           VCVTPS2QQZrr,
                                           VCVTPS2UQQZrr,
                                           VCVTQQ2PSZrr,
                                           VCVTTPD2DQZrr,
                                           VCVTTPD2UDQZrr,
                                           VCVTTPS2QQZrr,
                                           VCVTTPS2UQQZrr,
                                           VCVTUDQ2PDZrr,
                                           VCVTUQQ2PSZrr)>;

def ICXWriteResGroup95 : SchedWriteRes<[ICXPort23,ICXPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup95], (instrs VMOVNTDQAZ128rm,
                                          VPBLENDDrmi)>;
def: InstRW<[ICXWriteResGroup95, ReadAfterVecXLd],
                                  (instregex "VBLENDMPDZ128rm(b?)",
                                             "VBLENDMPSZ128rm(b?)",
                                             "VBROADCASTI32X2Z128rm(b?)",
                                             "VBROADCASTSSZ128rm(b?)",
                                             "VINSERT(F|I)128rm",
                                             "VMOVAPDZ128rm(b?)",
                                             "VMOVAPSZ128rm(b?)",
                                             "VMOVDDUPZ128rm(b?)",
                                             "VMOVDQA32Z128rm(b?)",
                                             "VMOVDQA64Z128rm(b?)",
                                             "VMOVDQU16Z128rm(b?)",
                                             "VMOVDQU32Z128rm(b?)",
                                             "VMOVDQU64Z128rm(b?)",
                                             "VMOVDQU8Z128rm(b?)",
                                             "VMOVSHDUPZ128rm(b?)",
                                             "VMOVSLDUPZ128rm(b?)",
                                             "VMOVUPDZ128rm(b?)",
                                             "VMOVUPSZ128rm(b?)",
                                             "VPADD(B|D|Q|W)Z128rm(b?)",
                                             "(V?)PADD(B|D|Q|W)rm",
                                             "VPBLENDM(B|D|Q|W)Z128rm(b?)",
                                             "VPBROADCASTDZ128rm(b?)",
                                             "VPBROADCASTQZ128rm(b?)",
                                             "VPSUB(B|D|Q|W)Z128rm(b?)",
                                             "(V?)PSUB(B|D|Q|W)rm",
                                             "VPTERNLOGDZ128rm(b?)i",
                                             "VPTERNLOGQZ128rm(b?)i")>;

def ICXWriteResGroup96 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup96], (instrs MMX_PACKSSDWrm,
                                          MMX_PACKSSWBrm,
                                          MMX_PACKUSWBrm)>;

def ICXWriteResGroup97 : SchedWriteRes<[ICXPort5,ICXPort015]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup97], (instregex "VPERMI2WZ128rr",
                                             "VPERMI2WZ256rr",
                                             "VPERMI2WZrr",
                                             "VPERMT2WZ128rr",
                                             "VPERMT2WZ256rr",
                                             "VPERMT2WZrr")>;

def ICXWriteResGroup99 : SchedWriteRes<[ICXPort23,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,2];
}
def: InstRW<[ICXWriteResGroup99], (instrs LEAVE, LEAVE64,
                                          SCASB, SCASL, SCASQ, SCASW)>;

def ICXWriteResGroup100 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort01]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup100], (instregex "(V?)CVT(T?)SS2SI64(Z?)rr",
                                              "VCVT(T?)SS2USI64Zrr")>;

def ICXWriteResGroup101 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort05]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup101], (instrs FLDCW16m)>;

def ICXWriteResGroup103 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup103], (instregex "KMOV(B|D|Q|W)km")>;

def ICXWriteResGroup104 : SchedWriteRes<[ICXPort6,ICXPort23,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup104], (instrs LRET64, RET64)>;

def ICXWriteResGroup106 : SchedWriteRes<[ICXPort49,ICXPort5,ICXPort78]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,2,1];
}
def: InstRW<[ICXWriteResGroup106], (instregex "VCOMPRESSPD(Z|Z128|Z256)mr(b?)",
                                              "VCOMPRESSPS(Z|Z128|Z256)mr(b?)",
                                              "VPCOMPRESSD(Z|Z128|Z256)mr(b?)",
                                              "VPCOMPRESSQ(Z|Z128|Z256)mr(b?)")>;

def ICXWriteResGroup107 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort06]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[ICXWriteResGroup107], (instregex "ROL(8|16|32|64)m(1|i)",
                                              "ROR(8|16|32|64)m(1|i)")>;

def ICXWriteResGroup107_1 : SchedWriteRes<[ICXPort06]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def: InstRW<[ICXWriteResGroup107_1], (instrs ROL8r1, ROL16r1, ROL32r1, ROL64r1,
                                             ROR8r1, ROR16r1, ROR32r1, ROR64r1)>;

def ICXWriteResGroup108 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[ICXWriteResGroup108], (instregex "XADD(8|16|32|64)rm")>;

def ICXWriteResGroup109 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort23,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,1,1];
}
def: InstRW<[ICXWriteResGroup109], (instregex "CALL(16|32|64)m")>;
def: InstRW<[ICXWriteResGroup109], (instrs FARCALL64m)>;

def ICXWriteResGroup110 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [1,2,2,2];
}
def: InstRW<[ICXWriteResGroup110], (instrs VPSCATTERDQZ128mr,
                                           VPSCATTERQQZ128mr,
                                           VSCATTERDPDZ128mr,
                                           VSCATTERQPDZ128mr)>;

def ICXWriteResGroup111 : SchedWriteRes<[ICXPort6,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [1,3,1,2];
}
def: InstRW<[ICXWriteResGroup111], (instrs LOOP)>;

def ICXWriteResGroup112 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [1,4,4,2];
}
def: InstRW<[ICXWriteResGroup112], (instrs VPSCATTERDQZ256mr,
                                           VPSCATTERQQZ256mr,
                                           VSCATTERDPDZ256mr,
                                           VSCATTERQPDZ256mr)>;

def ICXWriteResGroup113 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 19;
  let ReleaseAtCycles = [1,8,8,2];
}
def: InstRW<[ICXWriteResGroup113], (instrs VPSCATTERDQZmr,
                                           VPSCATTERQDZmr,
                                           VPSCATTERQQZmr,
                                           VSCATTERDPDZmr,
                                           VSCATTERQPSZmr,
                                           VSCATTERQPDZmr)>;

def ICXWriteResGroup114 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort78,ICXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 36;
  let ReleaseAtCycles = [1,16,1,16,2];
}
def: InstRW<[ICXWriteResGroup114], (instrs VSCATTERDPSZmr)>;

def ICXWriteResGroup118 : SchedWriteRes<[ICXPort1,ICXPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup118], (instregex "PDEP(32|64)rm",
                                              "PEXT(32|64)rm")>;

def ICXWriteResGroup119 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup119], (instregex "FCOM(P?)(32|64)m",
                                              "VPBROADCASTB(Z|Z256)rm(b?)",
                                              "VPBROADCASTW(Z|Z256)rm(b?)",
                                              "(V?)PALIGNR(Y|Z256)rmi",
                                              "(V?)PERMIL(PD|PS)(Y|Z256)m(b?)i",
                                              "(V?)PERMIL(PD|PS)(Y|Z256)rm",
                                              "(V?)UNPCK(L|H)(PD|PS)(Y|Z256)rm")>;
def: InstRW<[ICXWriteResGroup119], (instrs VPBROADCASTBYrm,
                                           VPBROADCASTWYrm,
                                           VPMOVSXBDYrm,
                                           VPMOVSXBQYrm,
                                           VPMOVSXWQYrm)>;

def ICXWriteResGroup121 : SchedWriteRes<[ICXPort23,ICXPort015]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup121], (instrs VMOVNTDQAZ256rm,
                                           VPBLENDDYrmi)>;
def: InstRW<[ICXWriteResGroup121, ReadAfterVecYLd],
                                   (instregex "VBLENDMPD(Z|Z256)rm(b?)",
                                              "VBLENDMPS(Z|Z256)rm(b?)",
                                              "VBROADCASTF32X2Z256rm(b?)",
                                              "VBROADCASTF32X2Zrm(b?)",
                                              "VBROADCASTF32X4Z256rm(b?)",
                                              "VBROADCASTF32X4rm(b?)",
                                              "VBROADCASTF32X8rm(b?)",
                                              "VBROADCASTF64X2Z128rm(b?)",
                                              "VBROADCASTF64X2rm(b?)",
                                              "VBROADCASTF64X4rm(b?)",
                                              "VBROADCASTI32X2Z256rm(b?)",
                                              "VBROADCASTI32X2Zrm(b?)",
                                              "VBROADCASTI32X4Z256rm(b?)",
                                              "VBROADCASTI32X4rm(b?)",
                                              "VBROADCASTI32X8rm(b?)",
                                              "VBROADCASTI64X2Z128rm(b?)",
                                              "VBROADCASTI64X2rm(b?)",
                                              "VBROADCASTI64X4rm(b?)",
                                              "VBROADCASTSD(Z|Z256)rm(b?)",
                                              "VBROADCASTSS(Z|Z256)rm(b?)",
                                              "VINSERTF32x4(Z|Z256)rm(b?)",
                                              "VINSERTF32x8Zrm(b?)",
                                              "VINSERTF64x2(Z|Z256)rm(b?)",
                                              "VINSERTF64x4Zrm(b?)",
                                              "VINSERTI32x4(Z|Z256)rm(b?)",
                                              "VINSERTI32x8Zrm(b?)",
                                              "VINSERTI64x2(Z|Z256)rm(b?)",
                                              "VINSERTI64x4Zrm(b?)",
                                              "VMOVAPD(Z|Z256)rm(b?)",
                                              "VMOVAPS(Z|Z256)rm(b?)",
                                              "VMOVDDUP(Z|Z256)rm(b?)",
                                              "VMOVDQA32(Z|Z256)rm(b?)",
                                              "VMOVDQA64(Z|Z256)rm(b?)",
                                              "VMOVDQU16(Z|Z256)rm(b?)",
                                              "VMOVDQU32(Z|Z256)rm(b?)",
                                              "VMOVDQU64(Z|Z256)rm(b?)",
                                              "VMOVDQU8(Z|Z256)rm(b?)",
                                              "VMOVSHDUP(Z|Z256)rm(b?)",
                                              "VMOVSLDUP(Z|Z256)rm(b?)",
                                              "VMOVUPD(Z|Z256)rm(b?)",
                                              "VMOVUPS(Z|Z256)rm(b?)",
                                              "VPADD(B|D|Q|W)Yrm",
                                              "VPADD(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPBLENDM(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPBROADCASTD(Z|Z256)rm(b?)",
                                              "VPBROADCASTQ(Z|Z256)rm(b?)",
                                              "VPSUB(B|D|Q|W)Yrm",
                                              "VPSUB(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPTERNLOGD(Z|Z256)rm(b?)i",
                                              "VPTERNLOGQ(Z|Z256)rm(b?)i")>;

def ICXWriteResGroup123 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 8;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,2,1];
}
def: InstRW<[ICXWriteResGroup123], (instregex "MMX_PH(ADD|SUB)SWrm")>;

def ICXWriteResGroup127 : SchedWriteRes<[ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,1,1,2];
}
def: InstRW<[ICXWriteResGroup127], (instregex "RCL(8|16|32|64)m(1|i)",
                                              "RCR(8|16|32|64)m(1|i)")>;

def ICXWriteResGroup128 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort06]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,1,3];
}
def: InstRW<[ICXWriteResGroup128], (instregex "ROL(8|16|32|64)mCL",
                                              "ROR(8|16|32|64)mCL",
                                              "SAR(8|16|32|64)mCL",
                                              "SHL(8|16|32|64)mCL",
                                              "SHR(8|16|32|64)mCL")>;

def ICXWriteResGroup130 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [1,1,1,2,1];
}
def: SchedAlias<WriteADCRMW, ICXWriteResGroup130>;

def ICXWriteResGroup131 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort78,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,2,1,2,2];
}
def: InstRW<[ICXWriteResGroup131], (instrs VPSCATTERQDZ128mr,
                                           VPSCATTERQDZ256mr,
                                           VSCATTERQPSZ128mr,
                                           VSCATTERQPSZ256mr)>;

def ICXWriteResGroup132 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort78,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 12;
  let ReleaseAtCycles = [1,4,1,4,2];
}
def: InstRW<[ICXWriteResGroup132], (instrs VPSCATTERDDZ128mr,
                                           VSCATTERDPSZ128mr)>;

def ICXWriteResGroup133 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort78,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 20;
  let ReleaseAtCycles = [1,8,1,8,2];
}
def: InstRW<[ICXWriteResGroup133], (instrs VPSCATTERDDZ256mr,
                                           VSCATTERDPSZ256mr)>;

def ICXWriteResGroup134 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort78,ICXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 36;
  let ReleaseAtCycles = [1,16,1,16,2];
}
def: InstRW<[ICXWriteResGroup134], (instrs VPSCATTERDDZmr)>;

def ICXWriteResGroup135 : SchedWriteRes<[ICXPort0,ICXPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup135], (instrs MMX_CVTPI2PSrm)>;

def ICXWriteResGroup136 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup136], (instrs VPMOVSXBWYrm,
                                           VPMOVSXDQYrm,
                                           VPMOVSXWDYrm,
                                           VPMOVZXWDYrm)>;
def: InstRW<[ICXWriteResGroup136], (instregex "VALIGN(D|Q)Z128rm(b?)i",
                                              "VFPCLASSSDZrm(b?)",
                                              "VFPCLASSSSZrm(b?)",
                                              "(V?)PCMPGTQrm",
                                              "VPERMI2DZ128rm(b?)",
                                              "VPERMI2PDZ128rm(b?)",
                                              "VPERMI2PSZ128rm(b?)",
                                              "VPERMI2QZ128rm(b?)",
                                              "VPERMT2DZ128rm(b?)",
                                              "VPERMT2PDZ128rm(b?)",
                                              "VPERMT2PSZ128rm(b?)",
                                              "VPERMT2QZ128rm(b?)",
                                              "VPMAXSQZ128rm(b?)",
                                              "VPMAXUQZ128rm(b?)",
                                              "VPMINSQZ128rm(b?)",
                                              "VPMINUQZ128rm(b?)")>;

def ICXWriteResGroup136_2 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup136_2], (instregex "VCMP(PD|PS)Z128rm(b?)i",
                                                "VCMP(SD|SS)Zrm",
                                                "VFPCLASSPDZ128rm(b?)",
                                                "VFPCLASSPSZ128rm(b?)",
                                                "VPCMPBZ128rmi(b?)",
                                                "VPCMPDZ128rmi(b?)",
                                                "VPCMPEQ(B|D|Q|W)Z128rm(b?)",
                                                "VPCMPGT(B|D|Q|W)Z128rm(b?)",
                                                "VPCMPQZ128rmi(b?)",
                                                "VPCMPU(B|D|Q|W)Z128rmi(b?)",
                                                "VPCMPWZ128rmi(b?)",
                                                "(V?)PACK(U|S)S(DW|WB)(Z128)?rm",
                                                "VPTESTMBZ128rm(b?)",
                                                "VPTESTMDZ128rm(b?)",
                                                "VPTESTMQZ128rm(b?)",
                                                "VPTESTMWZ128rm(b?)",
                                                "VPTESTNMBZ128rm(b?)",
                                                "VPTESTNMDZ128rm(b?)",
                                                "VPTESTNMQZ128rm(b?)",
                                                "VPTESTNMWZ128rm(b?)")>;

def ICXWriteResGroup137 : SchedWriteRes<[ICXPort23,ICXPort01]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup137], (instregex "MMX_CVT(T?)PS2PIrm",
                                              "(V?)CVTPS2PDrm")>;

def ICXWriteResGroup143 : SchedWriteRes<[ICXPort15,ICXPort01,ICXPort23]> {
  let Latency = 9;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[ICXWriteResGroup143], (instrs PHADDSWrm, VPHADDSWrm,
                                           PHSUBSWrm, VPHSUBSWrm)>;

def ICXWriteResGroup146 : SchedWriteRes<[ICXPort1,ICXPort6,ICXPort23,ICXPort0156]> {
  let Latency = 9;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [1,2,1,1];
}
def: InstRW<[ICXWriteResGroup146], (instregex "LAR(16|32|64)rm",
                                              "LSL(16|32|64)rm")>;

def ICXWriteResGroup148 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup148], (instrs VPCMPGTQYrm)>;
def: InstRW<[ICXWriteResGroup148], (instregex "(ADD|SUB|SUBR)_F(32|64)m",
                                              "ILD_F(16|32|64)m",
                                              "VALIGND(Z|Z256)rm(b?)i",
                                              "VALIGNQ(Z|Z256)rm(b?)i",
                                              "VPMAXSQ(Z|Z256)rm(b?)",
                                              "VPMAXUQ(Z|Z256)rm(b?)",
                                              "VPMINSQ(Z|Z256)rm(b?)",
                                              "VPMINUQ(Z|Z256)rm(b?)")>;

def ICXWriteResGroup148_2 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup148_2], (instregex "VCMPPD(Z|Z256)rm(b?)i",
                                                "VCMPPS(Z|Z256)rm(b?)i",
                                                "VFPCLASSPD(Z|Z256)rm(b?)",
                                                "VFPCLASSPS(Z|Z256)rm(b?)",
                                                "VPCMPB(Z|Z256)rmi(b?)",
                                                "VPCMPD(Z|Z256)rmi(b?)",
                                                "VPCMPEQB(Z|Z256)rm(b?)",
                                                "VPCMPEQD(Z|Z256)rm(b?)",
                                                "VPCMPEQQ(Z|Z256)rm(b?)",
                                                "VPCMPEQW(Z|Z256)rm(b?)",
                                                "VPCMPGTB(Z|Z256)rm(b?)",
                                                "VPCMPGTD(Z|Z256)rm(b?)",
                                                "VPCMPGTQ(Z|Z256)rm(b?)",
                                                "VPCMPGTW(Z|Z256)rm(b?)",
                                                "VPCMPQ(Z|Z256)rmi(b?)",
                                                "VPCMPU(B|D|Q|W)Z256rmi(b?)",
                                                "VPCMPU(B|D|Q|W)Zrmi(b?)",
                                                "VPCMPW(Z|Z256)rmi(b?)",
                                                "(V?)PACK(U|S)S(DW|WB)(Y|Z|Z256)rm",
                                                "VPTESTM(B|D|Q|W)Z256rm(b?)",
                                                "VPTESTM(B|D|Q|W)Zrm(b?)",
                                                "VPTESTNM(B|D|Q|W)Z256rm(b?)",
                                                "VPTESTNM(B|D|Q|W)Zrm(b?)")>;

def ICXWriteResGroup149 : SchedWriteRes<[ICXPort23,ICXPort01]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup149], (instregex "VCVTDQ2PDZ128rm(b?)",
                                              "VCVTDQ2PSZ128rm(b?)",
                                              "(V?)CVTDQ2PSrm",
                                              "VCVTPD2QQZ128rm(b?)",
                                              "VCVTPD2UQQZ128rm(b?)",
                                              "VCVTPH2PSZ128rm(b?)",
                                              "VCVTPS2DQZ128rm(b?)",
                                              "(V?)CVTPS2DQrm",
                                              "VCVTPS2PDZ128rm(b?)",
                                              "VCVTPS2QQZ128rm(b?)",
                                              "VCVTPS2UDQZ128rm(b?)",
                                              "VCVTPS2UQQZ128rm(b?)",
                                              "VCVTQQ2PDZ128rm(b?)",
                                              "VCVTQQ2PSZ128rm(b?)",
                                              "VCVTSS2SDZrm",
                                              "(V?)CVTSS2SDrm",
                                              "VCVTTPD2QQZ128rm(b?)",
                                              "VCVTTPD2UQQZ128rm(b?)",
                                              "VCVTTPS2DQZ128rm(b?)",
                                              "(V?)CVTTPS2DQrm",
                                              "VCVTTPS2QQZ128rm(b?)",
                                              "VCVTTPS2UDQZ128rm(b?)",
                                              "VCVTTPS2UQQZ128rm(b?)",
                                              "VCVTUDQ2PDZ128rm(b?)",
                                              "VCVTUDQ2PSZ128rm(b?)",
                                              "VCVTUQQ2PDZ128rm(b?)",
                                              "VCVTUQQ2PSZ128rm(b?)")>;

def ICXWriteResGroup151 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup151], (instregex "VEXPANDPDZ128rm(b?)",
                                              "VEXPANDPSZ128rm(b?)",
                                              "VPEXPANDDZ128rm(b?)",
                                              "VPEXPANDQZ128rm(b?)")>;

def ICXWriteResGroup154 : SchedWriteRes<[ICXPort15,ICXPort01,ICXPort23]> {
  let Latency = 10;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[ICXWriteResGroup154], (instrs VPHADDSWYrm,
                                           VPHSUBSWYrm)>;

def ICXWriteResGroup157 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 10;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,1,1,3];
}
def: InstRW<[ICXWriteResGroup157], (instregex "XCHG(8|16|32|64)rm")>;

def ICXWriteResGroup160 : SchedWriteRes<[ICXPort0,ICXPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup160], (instregex "MUL_F(32|64)m")>;

def ICXWriteResGroup161 : SchedWriteRes<[ICXPort23,ICXPort01]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup161], (instrs VCVTDQ2PSYrm,
                                           VCVTPS2PDYrm)>;
def: InstRW<[ICXWriteResGroup161], (instregex "VCVTDQ2(PD|PS)(Z|Z256)rm(b?)",
                                              "VCVTPH2PS(Z|Z256)rm(b?)",
                                              "VCVTPS2PD(Z|Z256)rm(b?)",
                                              "VCVTQQ2PD(Z|Z256)rm(b?)",
                                              "VCVTQQ2PSZ256rm(b?)",
                                              "VCVT(T?)PD2QQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PD2UQQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2DQYrm",
                                              "VCVT(T?)PS2DQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2QQZ256rm(b?)",
                                              "VCVT(T?)PS2UDQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2UQQZ256rm(b?)",
                                              "VCVTUDQ2(PD|PS)(Z|Z256)rm(b?)",
                                              "VCVTUQQ2PD(Z|Z256)rm(b?)",
                                              "VCVTUQQ2PSZ256rm(b?)")>;

def ICXWriteResGroup162 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup162], (instregex "FICOM(P?)(16|32)m",
                                              "VEXPANDPD(Z|Z256)rm(b?)",
                                              "VEXPANDPS(Z|Z256)rm(b?)",
                                              "VPEXPANDD(Z|Z256)rm(b?)",
                                              "VPEXPANDQ(Z|Z256)rm(b?)")>;

def ICXWriteResGroup164 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup164], (instregex "(V?)CVTDQ2PDrm")>;

def ICXWriteResGroup166 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort01]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup166], (instrs CVTPD2DQrm,
                                           CVTTPD2DQrm,
                                           MMX_CVTPD2PIrm,
                                           MMX_CVTTPD2PIrm)>;

def ICXWriteResGroup167 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort015]> {
  let Latency = 11;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[ICXWriteResGroup167], (instregex "VPCONFLICTQZ128rm(b?)")>;

def ICXWriteResGroup169 : SchedWriteRes<[ICXPort1,ICXPort06,ICXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [2,3,2];
}
def: InstRW<[ICXWriteResGroup169], (instregex "RCL(16|32|64)rCL",
                                              "RCR(16|32|64)rCL")>;

def ICXWriteResGroup170 : SchedWriteRes<[ICXPort1,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [1,5,1,2];
}
def: InstRW<[ICXWriteResGroup170], (instrs RCL8rCL)>;

def ICXWriteResGroup171 : SchedWriteRes<[ICXPort06,ICXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [2,9];
}
def: InstRW<[ICXWriteResGroup171], (instrs LOOPE, LOOPNE)>;

def ICXWriteResGroup174 : SchedWriteRes<[ICXPort01]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def: InstRW<[ICXWriteResGroup174], (instregex "VPMULLQ(Z128|Z256)rr")>;

def ICXWriteResGroup174z : SchedWriteRes<[ICXPort0]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def: InstRW<[ICXWriteResGroup174z], (instregex "VPMULLQZrr")>;

def ICXWriteResGroup175 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup175], (instregex "VPERMWZ128rm(b?)")>;

def ICXWriteResGroup176 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort01]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup176], (instregex "VCVT(T?)SD2USIZrm(b?)",
                                              "VCVT(T?)SS2USI64Zrm(b?)")>;

def ICXWriteResGroup177 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort01]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup177], (instregex "VCVT(T?)PS2QQZrm(b?)",
                                              "VCVT(T?)PS2UQQZrm(b?)")>;

def ICXWriteResGroup180 : SchedWriteRes<[ICXPort5,ICXPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2,1];
}
def: InstRW<[ICXWriteResGroup180], (instregex "(ADD|SUB|SUBR)_FI(16|32)m",
                                              "VPERMWZ256rm(b?)",
                                              "VPERMWZrm(b?)")>;

def ICXWriteResGroup181 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup181], (instrs VCVTDQ2PDYrm)>;

def ICXWriteResGroup183 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort015]> {
  let Latency = 13;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[ICXWriteResGroup183], (instregex "VPERMI2WZ128rm(b?)",
                                              "VPERMT2WZ128rm(b?)")>;

def ICXWriteResGroup187 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup187], (instregex "MUL_FI(16|32)m")>;

def ICXWriteResGroup188 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort01]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup188], (instregex "VCVTPD2DQZrm(b?)",
                                              "VCVTPD2UDQZrm(b?)",
                                              "VCVTQQ2PSZrm(b?)",
                                              "VCVTTPD2DQZrm(b?)",
                                              "VCVTTPD2UDQZrm(b?)",
                                              "VCVTUQQ2PSZrm(b?)")>;

def ICXWriteResGroup189 : SchedWriteRes<[ICXPort5,ICXPort23,ICXPort015]> {
  let Latency = 14;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2,1,1];
}
def: InstRW<[ICXWriteResGroup189], (instregex "VPERMI2WZ256rm(b?)",
                                              "VPERMI2WZrm(b?)",
                                              "VPERMT2WZ256rm(b?)",
                                              "VPERMT2WZrm(b?)")>;

def ICXWriteResGroup190 : SchedWriteRes<[ICXPort1,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 14;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [2,4,1,3];
}
def: InstRW<[ICXWriteResGroup190], (instrs RCR8rCL)>;

def ICXWriteResGroup191 : SchedWriteRes<[ICXPort0]> {
  let Latency = 15;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup191], (instregex "DIVR_(FPrST0|FST0r|FrST0)")>;

def ICXWriteResGroup194 : SchedWriteRes<[ICXPort1,ICXPort5,ICXPort01,ICXPort23,ICXPort015]> {
  let Latency = 15;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,2,2,1,2];
}
def: InstRW<[ICXWriteResGroup194], (instregex "VPCONFLICTDZ128rm(b?)")>;

def ICXWriteResGroup195 : SchedWriteRes<[ICXPort1,ICXPort23,ICXPort78,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 15;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [1,1,1,5,1,1];
}
def: InstRW<[ICXWriteResGroup195], (instregex "RCL(8|16|32|64)mCL")>;

def ICXWriteResGroup199 : SchedWriteRes<[ICXPort49,ICXPort23,ICXPort78,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 16;
  let NumMicroOps = 14;
  let ReleaseAtCycles = [1,1,1,4,2,5];
}
def: InstRW<[ICXWriteResGroup199], (instrs CMPXCHG8B)>;

def ICXWriteResGroup200 : SchedWriteRes<[ICXPort1, ICXPort05, ICXPort6]> {
  let Latency = 12;
  let NumMicroOps = 34;
  let ReleaseAtCycles = [1, 4, 5];
}
def: InstRW<[ICXWriteResGroup200], (instrs VZEROALL)>;

def ICXWriteResGroup202 : SchedWriteRes<[ICXPort0,ICXPort1,ICXPort5,ICXPort6,ICXPort05,ICXPort0156]> {
  let Latency = 17;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [2,1,2,4,2,4];
}
def: InstRW<[ICXWriteResGroup202], (instrs XCH_F)>;

def ICXWriteResGroup205 : SchedWriteRes<[ICXPort23,ICXPort01]> {
  let Latency = 21;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,3];
}
def: InstRW<[ICXWriteResGroup205], (instregex "VPMULLQZ128rm(b?)")>;

def ICXWriteResGroup207 : SchedWriteRes<[ICXPort5,ICXPort6,ICXPort06,ICXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,5];
}
def: InstRW<[ICXWriteResGroup207], (instrs CPUID, RDTSC)>;

def ICXWriteResGroup208 : SchedWriteRes<[ICXPort1,ICXPort23,ICXPort78,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [2,1,1,4,1,2];
}
def: InstRW<[ICXWriteResGroup208], (instregex "RCR(8|16|32|64)mCL")>;

def ICXWriteResGroup211 : SchedWriteRes<[ICXPort23,ICXPort01]> {
  let Latency = 22;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,3];
}
def: InstRW<[ICXWriteResGroup211], (instregex "VPMULLQZ256rm(b?)")>;

def ICXWriteResGroup211_1 : SchedWriteRes<[ICXPort23,ICXPort0]> {
  let Latency = 22;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,3];
}
def: InstRW<[ICXWriteResGroup211_1], (instregex "VPMULLQZrm(b?)")>;

def ICXWriteResGroup215 : SchedWriteRes<[ICXPort0]> {
  let Latency = 20;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def: InstRW<[ICXWriteResGroup215], (instregex "DIV_(FPrST0|FST0r|FrST0)")>;

def ICXWriteGatherEVEX2 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort015,ICXPort0156]> {
  let Latency = 17;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,2,1,1];
}
def: InstRW<[ICXWriteGatherEVEX2], (instrs VGATHERQPSZ128rm, VPGATHERQDZ128rm,
                                           VGATHERDPDZ128rm, VPGATHERDQZ128rm,
                                           VGATHERQPDZ128rm, VPGATHERQQZ128rm)>;

def ICXWriteGatherEVEX4 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort015,ICXPort0156]> {
  let Latency = 19;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,4,1,1];
}
def: InstRW<[ICXWriteGatherEVEX4], (instrs VGATHERQPSZ256rm, VPGATHERQDZ256rm,
                                           VGATHERQPDZ256rm, VPGATHERQQZ256rm,
                                           VGATHERDPSZ128rm, VPGATHERDDZ128rm,
                                           VGATHERDPDZ256rm, VPGATHERDQZ256rm)>;

def ICXWriteGatherEVEX8 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort015,ICXPort0156]> {
  let Latency = 21;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,8,1,1];
}
def: InstRW<[ICXWriteGatherEVEX8], (instrs VGATHERDPSZ256rm, VPGATHERDDZ256rm,
                                           VGATHERDPDZrm,    VPGATHERDQZrm,
                                           VGATHERQPDZrm,    VPGATHERQQZrm,
                                           VGATHERQPSZrm,    VPGATHERQDZrm)>;

def ICXWriteGatherEVEX16 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort015,ICXPort0156]> {
  let Latency = 25;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,16,1,1];
}
def: InstRW<[ICXWriteGatherEVEX16], (instrs VGATHERDPSZrm, VPGATHERDDZrm)>;

def ICXWriteResGroup219 : SchedWriteRes<[ICXPort49,ICXPort5,ICXPort6,ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 20;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [1,1,1,1,1,1,2];
}
def: InstRW<[ICXWriteResGroup219], (instrs INSB, INSL, INSW)>;

def ICXWriteResGroup220 : SchedWriteRes<[ICXPort5,ICXPort6,ICXPort0156]> {
  let Latency = 20;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [1,2,7];
}
def: InstRW<[ICXWriteResGroup220], (instrs MWAITrr)>;

def ICXWriteResGroup223 : SchedWriteRes<[ICXPort0,ICXPort23]> {
  let Latency = 22;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup223], (instregex "DIV_F(32|64)m")>;

def ICXWriteResGroupVEX2 : SchedWriteRes<[ICXPort0, ICXPort23, ICXPort5, ICXPort015]> {
  let Latency = 18;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,2,1,1];
}
def: InstRW<[ICXWriteResGroupVEX2], (instrs VGATHERDPDrm, VPGATHERDQrm,
                                            VGATHERQPDrm, VPGATHERQQrm,
                                            VGATHERQPSrm, VPGATHERQDrm)>;

def ICXWriteResGroupVEX4 : SchedWriteRes<[ICXPort0, ICXPort23, ICXPort5, ICXPort015]> {
  let Latency = 20;
  let NumMicroOps = 5; // 2 uops peform multiple loads
  let ReleaseAtCycles = [1,4,1,1];
}
def: InstRW<[ICXWriteResGroupVEX4], (instrs VGATHERDPDYrm, VPGATHERDQYrm,
                                            VGATHERDPSrm,  VPGATHERDDrm,
                                            VGATHERQPDYrm, VPGATHERQQYrm,
                                            VGATHERQPSYrm,  VPGATHERQDYrm)>;

def ICXWriteResGroupVEX8 : SchedWriteRes<[ICXPort0, ICXPort23, ICXPort5, ICXPort015]> {
  let Latency = 22;
  let NumMicroOps = 5; // 2 uops perform multiple loads
  let ReleaseAtCycles = [1,8,1,1];
}
def: InstRW<[ICXWriteResGroupVEX8], (instrs VGATHERDPSYrm, VPGATHERDDYrm)>;

def ICXWriteResGroup225 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort015]> {
  let Latency = 22;
  let NumMicroOps = 14;
  let ReleaseAtCycles = [5,5,4];
}
def: InstRW<[ICXWriteResGroup225], (instregex "VPCONFLICTDZ128rr",
                                              "VPCONFLICTQZ256rr")>;

def ICXWriteResGroup228 : SchedWriteRes<[ICXPort0,ICXPort49,ICXPort5,ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 23;
  let NumMicroOps = 19;
  let ReleaseAtCycles = [2,1,4,1,1,4,6];
}
def: InstRW<[ICXWriteResGroup228], (instrs CMPXCHG16B)>;

def ICXWriteResGroup233 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 25;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup233], (instregex "DIV_FI(16|32)m")>;

def ICXWriteResGroup239 : SchedWriteRes<[ICXPort0,ICXPort23]> {
  let Latency = 27;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1,1];
}
def: InstRW<[ICXWriteResGroup239], (instregex "DIVR_F(32|64)m")>;

def ICXWriteResGroup242 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort23,ICXPort015]> {
  let Latency = 29;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [5,5,1,4];
}
def: InstRW<[ICXWriteResGroup242], (instregex "VPCONFLICTQZ256rm(b?)")>;

def ICXWriteResGroup243 : SchedWriteRes<[ICXPort0,ICXPort5,ICXPort23]> {
  let Latency = 30;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [1,1,1];
}
def: InstRW<[ICXWriteResGroup243], (instregex "DIVR_FI(16|32)m")>;

def ICXWriteResGroup247 : SchedWriteRes<[ICXPort5,ICXPort6,ICXPort23,ICXPort06,ICXPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ReleaseAtCycles = [1,5,3,4,10];
}
def: InstRW<[ICXWriteResGroup247], (instregex "IN(8|16|32)ri",
                                              "IN(8|16|32)rr")>;

def ICXWriteResGroup248 : SchedWriteRes<[ICXPort5,ICXPort6,ICXPort23,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ReleaseAtCycles = [1,5,2,1,4,10];
}
def: InstRW<[ICXWriteResGroup248], (instregex "OUT(8|16|32)ir",
                                              "OUT(8|16|32)rr")>;

def ICXWriteResGroup249 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort015]> {
  let Latency = 37;
  let NumMicroOps = 21;
  let ReleaseAtCycles = [9,7,5];
}
def: InstRW<[ICXWriteResGroup249], (instregex "VPCONFLICTDZ256rr",
                                              "VPCONFLICTQZrr")>;

def ICXWriteResGroup250 : SchedWriteRes<[ICXPort1,ICXPort6,ICXPort23,ICXPort0156]> {
  let Latency = 37;
  let NumMicroOps = 31;
  let ReleaseAtCycles = [1,8,1,21];
}
def: InstRW<[ICXWriteResGroup250], (instregex "XRSTOR(64)?")>;

def ICXWriteResGroup252 : SchedWriteRes<[ICXPort1,ICXPort49,ICXPort5,ICXPort6,ICXPort23,ICXPort78,ICXPort15,ICXPort0156]> {
  let Latency = 40;
  let NumMicroOps = 18;
  let ReleaseAtCycles = [1,1,2,3,1,1,1,8];
}
def: InstRW<[ICXWriteResGroup252], (instrs VMCLEARm)>;

def ICXWriteResGroup253 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort23,ICXPort78,ICXPort0156]> {
  let Latency = 41;
  let NumMicroOps = 39;
  let ReleaseAtCycles = [1,10,1,1,26];
}
def: InstRW<[ICXWriteResGroup253], (instrs XSAVE64)>;

def ICXWriteResGroup254 : SchedWriteRes<[ICXPort5,ICXPort0156]> {
  let Latency = 42;
  let NumMicroOps = 22;
  let ReleaseAtCycles = [2,20];
}
def: InstRW<[ICXWriteResGroup254], (instrs RDTSCP)>;

def ICXWriteResGroup255 : SchedWriteRes<[ICXPort49,ICXPort6,ICXPort23,ICXPort78,ICXPort0156]> {
  let Latency = 42;
  let NumMicroOps = 40;
  let ReleaseAtCycles = [1,11,1,1,26];
}
def: InstRW<[ICXWriteResGroup255], (instrs XSAVE)>;
def: InstRW<[ICXWriteResGroup255], (instregex "XSAVEC", "XSAVES", "XSAVEOPT")>;

def ICXWriteResGroup256 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort23,ICXPort015]> {
  let Latency = 44;
  let NumMicroOps = 22;
  let ReleaseAtCycles = [9,7,1,5];
}
def: InstRW<[ICXWriteResGroup256], (instregex "VPCONFLICTDZ256rm(b?)",
                                              "VPCONFLICTQZrm(b?)")>;

def ICXWriteResGroup258 : SchedWriteRes<[ICXPort0,ICXPort23,ICXPort05,ICXPort06,ICXPort0156]> {
  let Latency = 62;
  let NumMicroOps = 64;
  let ReleaseAtCycles = [2,8,5,10,39];
}
def: InstRW<[ICXWriteResGroup258], (instrs FLDENVm)>;

def ICXWriteResGroup259 : SchedWriteRes<[ICXPort0,ICXPort6,ICXPort23,ICXPort05,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 63;
  let NumMicroOps = 88;
  let ReleaseAtCycles = [4,4,31,1,2,1,45];
}
def: InstRW<[ICXWriteResGroup259], (instrs FXRSTOR64)>;

def ICXWriteResGroup260 : SchedWriteRes<[ICXPort0,ICXPort6,ICXPort23,ICXPort05,ICXPort06,ICXPort15,ICXPort0156]> {
  let Latency = 63;
  let NumMicroOps = 90;
  let ReleaseAtCycles = [4,2,33,1,2,1,47];
}
def: InstRW<[ICXWriteResGroup260], (instrs FXRSTOR)>;

def ICXWriteResGroup261 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort015]> {
  let Latency = 67;
  let NumMicroOps = 35;
  let ReleaseAtCycles = [17,11,7];
}
def: InstRW<[ICXWriteResGroup261], (instregex "VPCONFLICTDZrr")>;

def ICXWriteResGroup262 : SchedWriteRes<[ICXPort5,ICXPort01,ICXPort23,ICXPort015]> {
  let Latency = 74;
  let NumMicroOps = 36;
  let ReleaseAtCycles = [17,11,1,7];
}
def: InstRW<[ICXWriteResGroup262], (instregex "VPCONFLICTDZrm(b?)")>;

def ICXWriteResGroup263 : SchedWriteRes<[ICXPort5,ICXPort05,ICXPort0156]> {
  let Latency = 75;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [6,3,6];
}
def: InstRW<[ICXWriteResGroup263], (instrs FNINIT)>;

def ICXWriteResGroup266 : SchedWriteRes<[ICXPort0,ICXPort1,ICXPort49,ICXPort5,ICXPort6,ICXPort78,ICXPort06,ICXPort0156]> {
  let Latency = 106;
  let NumMicroOps = 100;
  let ReleaseAtCycles = [9,1,11,16,1,11,21,30];
}
def: InstRW<[ICXWriteResGroup266], (instrs FSTENVm)>;

def ICXWriteResGroup267 : SchedWriteRes<[ICXPort6,ICXPort0156]> {
  let Latency = 140;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [1,3];
}
def: InstRW<[ICXWriteResGroup267], (instrs PAUSE)>;

def: InstRW<[WriteZero], (instrs CLC)>;


// Instruction variants handled by the renamer. These might not need execution
// ports in certain conditions.
// See Agner's Fog "The microarchitecture of Intel, AMD and VIA CPUs",
// section "Skylake Pipeline" > "Register allocation and renaming".
// These can be investigated with llvm-exegesis, e.g.
// echo 'pxor %mm0, %mm0' | /tmp/llvm-exegesis -mode=uops -snippets-file=-
// echo 'vxorpd %xmm0, %xmm0, %xmm1' | /tmp/llvm-exegesis -mode=uops -snippets-file=-

def ICXWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def ICXWriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[ICXWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                          XOR32rr, XOR64rr)>;

def ICXWriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
def : InstRW<[ICXWriteFZeroIdiom], (instrs XORPSrr, VXORPSrr,
                                           XORPDrr, VXORPDrr,
                                           VXORPSZ128rr,
                                           VXORPDZ128rr)>;

def ICXWriteFZeroIdiomY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogicY]>
]>;
def : InstRW<[ICXWriteFZeroIdiomY], (instrs VXORPSYrr, VXORPDYrr,
                                            VXORPSZ256rr, VXORPDZ256rr)>;

def ICXWriteFZeroIdiomZ : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogicZ]>
]>;
def : InstRW<[ICXWriteFZeroIdiomZ], (instrs VXORPSZrr, VXORPDZrr)>;

def ICXWriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
def : InstRW<[ICXWriteVZeroIdiomLogicX], (instrs PXORrr, VPXORrr,
                                                 VPXORDZ128rr, VPXORQZ128rr)>;

def ICXWriteVZeroIdiomLogicY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicY]>
]>;
def : InstRW<[ICXWriteVZeroIdiomLogicY], (instrs VPXORYrr,
                                                 VPXORDZ256rr, VPXORQZ256rr)>;

def ICXWriteVZeroIdiomLogicZ : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicZ]>
]>;
def : InstRW<[ICXWriteVZeroIdiomLogicZ], (instrs VPXORDZrr, VPXORQZrr)>;

def ICXWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
def : InstRW<[ICXWriteVZeroIdiomALUX], (instrs PCMPGTBrr, VPCMPGTBrr,
                                               PCMPGTDrr, VPCMPGTDrr,
                                               PCMPGTWrr, VPCMPGTWrr)>;

def ICXWriteVZeroIdiomALUY : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUY]>
]>;
def : InstRW<[ICXWriteVZeroIdiomALUY], (instrs VPCMPGTBYrr,
                                               VPCMPGTDYrr,
                                               VPCMPGTWYrr)>;

def ICXWritePSUB : SchedWriteRes<[ICXPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}

def ICXWriteVZeroIdiomPSUB : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [ICXWritePSUB]>
]>;

def : InstRW<[ICXWriteVZeroIdiomPSUB], (instrs PSUBBrr, VPSUBBrr, VPSUBBZ128rr,
                                               PSUBDrr, VPSUBDrr, VPSUBDZ128rr,
                                               PSUBQrr, VPSUBQrr, VPSUBQZ128rr,
                                               PSUBWrr, VPSUBWrr, VPSUBWZ128rr,
                                               VPSUBBYrr, VPSUBBZ256rr,
                                               VPSUBDYrr, VPSUBDZ256rr,
                                               VPSUBQYrr, VPSUBQZ256rr,
                                               VPSUBWYrr, VPSUBWZ256rr,
                                               VPSUBBZrr,
                                               VPSUBDZrr,
                                               VPSUBQZrr,
                                               VPSUBWZrr)>;
def ICXWritePCMPGTQ : SchedWriteRes<[ICXPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}

def ICXWriteVZeroIdiomPCMPGTQ : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [ICXWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [ICXWritePCMPGTQ]>
]>;
def : InstRW<[ICXWriteVZeroIdiomPCMPGTQ], (instrs PCMPGTQrr, VPCMPGTQrr,
                                                  VPCMPGTQYrr)>;


// CMOVs that use both Z and C flag require an extra uop.
def ICXWriteCMOVA_CMOVBErr : SchedWriteRes<[ICXPort06]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def ICXWriteCMOVA_CMOVBErm : SchedWriteRes<[ICXPort23,ICXPort06]> {
  let Latency = 7;
  let ReleaseAtCycles = [1,2];
  let NumMicroOps = 3;
}

def ICXCMOVA_CMOVBErr :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsCMOVArr_Or_CMOVBErr>, [ICXWriteCMOVA_CMOVBErr]>,
  SchedVar<NoSchedPred,                             [WriteCMOV]>
]>;

def ICXCMOVA_CMOVBErm :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsCMOVArm_Or_CMOVBErm>, [ICXWriteCMOVA_CMOVBErm]>,
  SchedVar<NoSchedPred,                             [WriteCMOV.Folded]>
]>;

def : InstRW<[ICXCMOVA_CMOVBErr], (instrs CMOV16rr, CMOV32rr, CMOV64rr)>;
def : InstRW<[ICXCMOVA_CMOVBErm], (instrs CMOV16rm, CMOV32rm, CMOV64rm)>;

// SETCCs that use both Z and C flag require an extra uop.
def ICXWriteSETA_SETBEr : SchedWriteRes<[ICXPort06]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}

def ICXWriteSETA_SETBEm : SchedWriteRes<[ICXPort49,ICXPort78,ICXPort06]> {
  let Latency = 3;
  let ReleaseAtCycles = [1,1,2];
  let NumMicroOps = 4;
}

def ICXSETA_SETBErr :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsSETAr_Or_SETBEr>, [ICXWriteSETA_SETBEr]>,
  SchedVar<NoSchedPred,                         [WriteSETCC]>
]>;

def ICXSETA_SETBErm :  SchedWriteVariant<[
  SchedVar<MCSchedPredicate<IsSETAm_Or_SETBEm>, [ICXWriteSETA_SETBEm]>,
  SchedVar<NoSchedPred,                         [WriteSETCCStore]>
]>;

def : InstRW<[ICXSETA_SETBErr], (instrs SETCCr)>;
def : InstRW<[ICXSETA_SETBErm], (instrs SETCCm)>;

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
    VPCMPGTBYrr, VPCMPGTWYrr, VPCMPGTDYrr, VPCMPGTQYrr,

    // zmm variants.
    VXORPSZrr, VXORPDZrr, VPXORDZrr, VPXORQZrr,
    VXORPSZ128rr, VXORPDZ128rr, VPXORDZ128rr, VPXORQZ128rr,
    VXORPSZ256rr, VXORPDZ256rr, VPXORDZ256rr, VPXORQZ256rr,
    VPSUBBZrr, VPSUBWZrr, VPSUBDZrr, VPSUBQZrr,
    VPSUBBZ128rr, VPSUBWZ128rr, VPSUBDZ128rr, VPSUBQZ128rr,
    VPSUBBZ256rr, VPSUBWZ256rr, VPSUBDZ256rr, VPSUBQZ256rr,
  ], ZeroIdiomPredicate>,
]>;

} // SchedModel
