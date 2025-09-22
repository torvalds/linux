//=- X86SchedSapphireRapids.td - X86 SapphireRapids Scheduling *- tablegen -*=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for SapphireRapids to support instruction
// scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def SapphireRapidsModel : SchedMachineModel {
  // SapphireRapids can allocate 6 uops per cycle.
  let IssueWidth = 6; // Based on allocator width.
  let MicroOpBufferSize = 512; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 14;

  // Latency for microcoded instructions or instructions without latency info.
  int MaxLatency = 100;

  // Based on the LSD (loop-stream detector) queue size (ST).
  let LoopMicroOpBufferSize = 72;

  // This flag is set to allow the scheduler to assign a default model to
  // unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = SapphireRapidsModel in {

// SapphireRapids can issue micro-ops to 12 different ports in one cycle.
def SPRPort00 : ProcResource<1>;
def SPRPort01 : ProcResource<1>;
def SPRPort02 : ProcResource<1>;
def SPRPort03 : ProcResource<1>;
def SPRPort04 : ProcResource<1>;
def SPRPort05 : ProcResource<1>;
def SPRPort06 : ProcResource<1>;
def SPRPort07 : ProcResource<1>;
def SPRPort08 : ProcResource<1>;
def SPRPort09 : ProcResource<1>;
def SPRPort10 : ProcResource<1>;
def SPRPort11 : ProcResource<1>;

// Workaround to represent invalid ports. WriteRes shouldn't use this resource.
def SPRPortInvalid :ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def SPRPort00_01          : ProcResGroup<[SPRPort00, SPRPort01]>;
def SPRPort00_01_05       : ProcResGroup<[SPRPort00, SPRPort01, SPRPort05]>;
def SPRPort00_01_05_06    : ProcResGroup<[SPRPort00, SPRPort01, SPRPort05, SPRPort06]>;
def SPRPort00_05          : ProcResGroup<[SPRPort00, SPRPort05]>;
def SPRPort00_05_06       : ProcResGroup<[SPRPort00, SPRPort05, SPRPort06]>;
def SPRPort00_06          : ProcResGroup<[SPRPort00, SPRPort06]>;
def SPRPort01_05          : ProcResGroup<[SPRPort01, SPRPort05]>;
def SPRPort01_05_10       : ProcResGroup<[SPRPort01, SPRPort05, SPRPort10]>;
def SPRPort02_03          : ProcResGroup<[SPRPort02, SPRPort03]>;
def SPRPort02_03_11       : ProcResGroup<[SPRPort02, SPRPort03, SPRPort11]>;
def SPRPort07_08          : ProcResGroup<[SPRPort07, SPRPort08]>;

// EU has 112 reservation stations.
def SPRPort00_01_05_06_10 : ProcResGroup<[SPRPort00, SPRPort01, SPRPort05,
                                          SPRPort06, SPRPort10]> {
  let BufferSize = 112;
}

// STD has 48 reservation stations.
def SPRPort04_09          : ProcResGroup<[SPRPort04, SPRPort09]> {
  let BufferSize = 48;
}

// MEM has 72 reservation stations.
def SPRPort02_03_07_08_11 : ProcResGroup<[SPRPort02, SPRPort03, SPRPort07,
                                          SPRPort08, SPRPort11]> {
  let BufferSize = 72;
}

// Integer loads are 5 cycles, so ReadAfterLd registers needn't be available
// until 5 cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 5>;

// Vector loads are 6 cycles, so ReadAfterVec*Ld registers needn't be available
// until 6 cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 6>;
def : ReadAdvance<ReadAfterVecXLd, 6>;
def : ReadAdvance<ReadAfterVecYLd, 6>;

def : ReadAdvance<ReadInt2Fpu, 0>;

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when queued in the reservation station.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass SPRWriteResPair<X86FoldableSchedWrite SchedRW,
                           list<ProcResourceKind> ExePorts,
                           int Lat, list<int> Res = [1], int UOps = 1,
                           int LoadLat = 5, int LoadUOps = 1> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ReleaseAtCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on port 2/3/11 and adds LoadLat cycles to
  // the latency (default = 5).
  def : WriteRes<SchedRW.Folded, !listconcat([SPRPort02_03_11], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ReleaseAtCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

//===----------------------------------------------------------------------===//
// The following definitons are infered by smg.
//===----------------------------------------------------------------------===//

// Infered SchedWrite definition.
def : WriteRes<WriteADC, [SPRPort00_06]>;
defm : X86WriteRes<WriteADCLd, [SPRPort00_01_05_06_10, SPRPort00_06], 11, [1, 1], 2>;
defm : SPRWriteResPair<WriteAESDecEnc, [SPRPort00_01], 5, [1], 1, 7>;
defm : SPRWriteResPair<WriteAESIMC, [SPRPort00_01], 8, [2], 2, 7>;
defm : X86WriteRes<WriteAESKeyGen, [SPRPort00, SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort01_05, SPRPort05], 7, [4, 1, 1, 2, 3, 3], 14>;
defm : X86WriteRes<WriteAESKeyGenLd, [SPRPort00, SPRPort00_01, SPRPort00_06, SPRPort01_05, SPRPort02_03_11, SPRPort05], 12, [4, 1, 2, 3, 1, 3], 14>;
def : WriteRes<WriteALU, [SPRPort00_01_05_06_10]>;
def : WriteRes<WriteALULd, [SPRPort00_01_05_06_10]> {
  let Latency = 11;
}
defm : SPRWriteResPair<WriteBEXTR, [SPRPort00_06, SPRPort01], 6, [1, 1], 2>;
defm : SPRWriteResPair<WriteBLS, [SPRPort01_05_10], 2, [1]>;
defm : SPRWriteResPair<WriteBSF, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteBSR, [SPRPort01], 3, [1]>;
def : WriteRes<WriteBSWAP32, [SPRPort01]>;
defm : X86WriteRes<WriteBSWAP64, [SPRPort00_06, SPRPort01], 2, [1, 1], 2>;
defm : SPRWriteResPair<WriteBZHI, [SPRPort01], 3, [1]>;
def : WriteRes<WriteBitTest, [SPRPort01]>;
defm : X86WriteRes<WriteBitTestImmLd, [SPRPort01, SPRPort02_03_11], 6, [1, 1], 2>;
defm : X86WriteRes<WriteBitTestRegLd, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11], 11, [4, 2, 1, 2, 1], 10>;
def : WriteRes<WriteBitTestSet, [SPRPort01]>;
def : WriteRes<WriteBitTestSetImmLd, [SPRPort01]> {
  let Latency = 11;
}
defm : X86WriteRes<WriteBitTestSetRegLd, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10], 17, [3, 2, 1, 2], 8>;
defm : SPRWriteResPair<WriteBlend, [SPRPort01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteBlendY, [SPRPort00_01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteCLMul, [SPRPort05], 3, [1], 1, 7>;
defm : SPRWriteResPair<WriteCMOV, [SPRPort00_06], 1, [1], 1, 6>;
defm : X86WriteRes<WriteCMPXCHG, [SPRPort00_01_05_06_10, SPRPort00_06], 3, [3, 2], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08], 12, [1, 2, 1, 1, 1], 6>;
defm : SPRWriteResPair<WriteCRC32, [SPRPort01], 3, [1]>;
defm : X86WriteRes<WriteCvtI2PD, [SPRPort00_01, SPRPort05], 5, [1, 1], 2>;
defm : X86WriteRes<WriteCvtI2PDLd, [SPRPort00_01, SPRPort02_03_11], 11, [1, 1], 2>;
defm : X86WriteRes<WriteCvtI2PDY, [SPRPort00_01, SPRPort05], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtI2PDYLd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtI2PDZ, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteCvtI2PS, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteCvtI2PSY, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteCvtI2PSZ, [SPRPort00], 4, [1], 1, 8>;
defm : X86WriteRes<WriteCvtI2SD, [SPRPort00_01, SPRPort05], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtI2SDLd, [SPRPort00_01, SPRPort02_03_11], 11, [1, 1], 2>;
defm : X86WriteRes<WriteCvtI2SS, [SPRPort00_01, SPRPort00_01_05, SPRPort05], 9, [1, 1, 1], 3>;
defm : X86WriteRes<WriteCvtI2SSLd, [SPRPort00_01, SPRPort02_03_11], 11, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2I, [SPRPort00_01, SPRPort05], 5, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2ILd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2IY, [SPRPort00_01, SPRPort05], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2IYLd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2IZ, [SPRPort00, SPRPort05], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPD2IZLd, [SPRPort00, SPRPort02_03_11], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtPD2PS, [SPRPort00_01, SPRPort05], 5, [1, 1], 2, 7>;
defm : SPRWriteResPair<WriteCvtPD2PSY, [SPRPort00_01, SPRPort05], 7, [1, 1], 2, 8>;
defm : SPRWriteResPair<WriteCvtPD2PSZ, [SPRPort00, SPRPort05], 7, [1, 1], 2, 8>;
defm : X86WriteRes<WriteCvtPH2PS, [SPRPort00_01, SPRPort05], 6, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPH2PSLd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPH2PSY, [SPRPort00_01, SPRPort05], 8, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPH2PSYLd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtPH2PSZ, [SPRPort00, SPRPort05], 11, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtPS2I, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteCvtPS2IY, [SPRPort00_01], 4, [1], 1, 8>;
defm : X86WriteRes<WriteCvtPS2IZ, [SPRPort00, SPRPort00_05, SPRPort05], 10, [1, 2, 1], 4>;
defm : X86WriteRes<WriteCvtPS2IZLd, [SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03_11, SPRPort05], 18, [1, 2, 1, 1, 1], 6>;
defm : X86WriteRes<WriteCvtPS2PD, [SPRPort00_01, SPRPort05], 5, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PDLd, [SPRPort00_01, SPRPort02_03_11], 11, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PDY, [SPRPort00_01, SPRPort05], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PDYLd, [SPRPort00_01, SPRPort02_03_11], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtPS2PDZ, [SPRPort00, SPRPort05], 7, [1, 1], 2, 6>;
defm : X86WriteRes<WriteCvtPS2PH, [SPRPort00_01, SPRPort05], 6, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PHSt, [SPRPort00_01, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1], 3>;
defm : X86WriteRes<WriteCvtPS2PHY, [SPRPort00_01, SPRPort05], 8, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [SPRPort00_01, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1], 3>;
defm : X86WriteRes<WriteCvtPS2PHZ, [SPRPort00, SPRPort05], 11, [1, 1], 2>;
defm : X86WriteRes<WriteCvtPS2PHZSt, [SPRPort00, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1], 3>;
defm : SPRWriteResPair<WriteCvtSD2I, [SPRPort00, SPRPort00_01], 7, [1, 1], 2>;
defm : SPRWriteResPair<WriteCvtSD2SS, [SPRPort00_01, SPRPort05], 5, [1, 1], 2, 7>;
defm : SPRWriteResPair<WriteCvtSS2I, [SPRPort00, SPRPort00_01], 7, [1, 1], 2>;
defm : X86WriteRes<WriteCvtSS2SD, [SPRPort00_01, SPRPort05], 5, [1, 1], 2>;
defm : X86WriteRes<WriteCvtSS2SDLd, [SPRPort00_01, SPRPort02_03_11], 11, [1, 1], 2>;
defm : SPRWriteResPair<WriteDPPD, [SPRPort00_01, SPRPort01_05], 9, [2, 1], 3, 7>;
defm : SPRWriteResPair<WriteDPPS, [SPRPort00_01, SPRPort00_06, SPRPort01_05, SPRPort05], 14, [2, 1, 2, 1], 6, 7>;
defm : SPRWriteResPair<WriteDPPSY, [SPRPort00_01, SPRPort00_06, SPRPort01_05, SPRPort05], 14, [2, 1, 2, 1], 6, 8>;
defm : SPRWriteResPair<WriteDiv16, [SPRPort00_01_05_06_10, SPRPort01], 16, [1, 3], 4, 4>;
defm : SPRWriteResPair<WriteDiv32, [SPRPort00_01_05_06_10, SPRPort01], 15, [1, 3], 4, 4>;
defm : SPRWriteResPair<WriteDiv64, [SPRPort01], 18, [3], 3>;
defm : X86WriteRes<WriteDiv8, [SPRPort01], 17, [3], 3>;
defm : X86WriteRes<WriteDiv8Ld, [SPRPort01], 22, [3], 3>;
defm : X86WriteRes<WriteEMMS, [SPRPort00, SPRPort00_05, SPRPort00_06], 10, [1, 8, 1], 10>;
defm : SPRWriteResPair<WriteFAdd, [SPRPort01_05], 3, [1], 1, 7>;
defm : SPRWriteResPair<WriteFAdd64, [SPRPort01_05], 3, [1], 1, 7>;
defm : SPRWriteResPair<WriteFAdd64X, [SPRPort01_05], 3, [1], 1, 7>;
defm : SPRWriteResPair<WriteFAdd64Y, [SPRPort01_05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteFAdd64Z, [SPRPort00_05], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFAddX, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFAddY, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFAddZ, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFBlend, [SPRPort00_01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFBlendY, [SPRPort00_01_05], 1, [1], 1, 8>;
def : WriteRes<WriteFCMOV, [SPRPort01]> {
  let Latency = 3;
}
defm : SPRWriteResPair<WriteFCmp, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFCmp64, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFCmp64X, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFCmp64Y, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFCmp64Z, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFCmpX, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFCmpY, [SPRPort00_01], 4, [1], 1, 8>;
def : WriteRes<WriteFCmpZ, [SPRPort05]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteFCmpZLd, [SPRPort00, SPRPort02_03_11], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteFCom, [SPRPort05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFComX, [SPRPort00], 3, [1]>;
defm : SPRWriteResPair<WriteFDiv, [SPRPort00], 11, [1], 1, 7>;
defm : SPRWriteResPair<WriteFDiv64, [SPRPort00], 14, [1], 1, 6>;
defm : SPRWriteResPair<WriteFDiv64X, [SPRPort00], 14, [1], 1, 6>;
defm : SPRWriteResPair<WriteFDiv64Y, [SPRPort00], 14, [1], 1, 7>;
defm : SPRWriteResPair<WriteFDiv64Z, [SPRPort00, SPRPort00_05], 23, [2, 1], 3, 7>;
defm : SPRWriteResPair<WriteFDivX, [SPRPort00], 11, [1], 1, 7>;
defm : SPRWriteResPair<WriteFDivY, [SPRPort00], 11, [1], 1, 8>;
defm : SPRWriteResPair<WriteFDivZ, [SPRPort00, SPRPort00_05], 18, [2, 1], 3, 7>;
defm : SPRWriteResPair<WriteFHAdd, [SPRPort01_05, SPRPort05], 6, [1, 2], 3, 6>;
defm : SPRWriteResPair<WriteFHAddY, [SPRPort01_05, SPRPort05], 5, [1, 2], 3, 8>;
def : WriteRes<WriteFLD0, [SPRPort00_05]>;
defm : X86WriteRes<WriteFLD1, [SPRPort00_05], 1, [2], 2>;
defm : X86WriteRes<WriteFLDC, [SPRPort00_05], 1, [2], 2>;
def : WriteRes<WriteFLoad, [SPRPort02_03_11]> {
  let Latency = 7;
}
def : WriteRes<WriteFLoadX, [SPRPort02_03_11]> {
  let Latency = 7;
}
def : WriteRes<WriteFLoadY, [SPRPort02_03_11]> {
  let Latency = 8;
}
defm : SPRWriteResPair<WriteFLogic, [SPRPort00_01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFLogicY, [SPRPort00_01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFLogicZ, [SPRPort00_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFMA, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMAX, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMAY, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFMAZ, [SPRPort00], 4, [1], 1, 8>;
def : WriteRes<WriteFMOVMSK, [SPRPort00]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteFMaskedLoad, [SPRPort00_01_05, SPRPort02_03_11], 8, [1, 1], 2>;
defm : X86WriteRes<WriteFMaskedLoadY, [SPRPort00_01_05, SPRPort02_03_11], 9, [1, 1], 2>;
defm : X86WriteRes<WriteFMaskedStore32, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteFMaskedStore32Y, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteFMaskedStore64, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteFMaskedStore64Y, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteFMoveX, [], 1, [], 0>;
defm : X86WriteRes<WriteFMoveY, [], 1, [], 0>;
def : WriteRes<WriteFMoveZ, [SPRPort00_05]>;
defm : SPRWriteResPair<WriteFMul, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMul64, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMul64X, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMul64Y, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFMul64Z, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFMulX, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFMulY, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFMulZ, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFRcp, [SPRPort00], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFRcpX, [SPRPort00], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFRcpY, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFRcpZ, [SPRPort00, SPRPort00_05], 7, [2, 1], 3, 7>;
defm : SPRWriteResPair<WriteFRnd, [SPRPort00_01], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFRndY, [SPRPort00_01], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFRndZ, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFRsqrt, [SPRPort00], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFRsqrtX, [SPRPort00], 4, [1], 1, 7>;
defm : SPRWriteResPair<WriteFRsqrtY, [SPRPort00], 4, [1], 1, 8>;
defm : SPRWriteResPair<WriteFRsqrtZ, [SPRPort00, SPRPort00_05], 9, [2, 1], 3>;
defm : SPRWriteResPair<WriteFShuffle, [SPRPort05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFShuffle256, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteFShuffleY, [SPRPort05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFShuffleZ, [SPRPort05], 1, [1], 1, 8>;
def : WriteRes<WriteFSign, [SPRPort00]>;
defm : SPRWriteResPair<WriteFSqrt, [SPRPort00], 12, [1], 1, 7>;
defm : SPRWriteResPair<WriteFSqrt64, [SPRPort00], 18, [1]>;
defm : SPRWriteResPair<WriteFSqrt64X, [SPRPort00], 18, [1], 1, 6>;
defm : SPRWriteResPair<WriteFSqrt64Y, [SPRPort00], 18, [1], 1, 3>;
// Warning: negtive load latency.
defm : SPRWriteResPair<WriteFSqrt64Z, [SPRPort00, SPRPort00_05], 32, [2, 1], 3, -1>;
def : WriteRes<WriteFSqrt80, [SPRPortInvalid, SPRPort00]> {
  let ReleaseAtCycles = [7, 1];
  let Latency = 21;
}
defm : SPRWriteResPair<WriteFSqrtX, [SPRPort00], 12, [1], 1, 7>;
defm : SPRWriteResPair<WriteFSqrtY, [SPRPort00], 12, [1], 1, 8>;
defm : SPRWriteResPair<WriteFSqrtZ, [SPRPort00, SPRPort00_05], 20, [2, 1], 3, 7>;
defm : X86WriteRes<WriteFStore, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : X86WriteResUnsupported<WriteFStoreNT>;
defm : X86WriteRes<WriteFStoreNTX, [SPRPort04_09, SPRPort07_08], 518, [1, 1], 2>;
defm : X86WriteRes<WriteFStoreNTY, [SPRPort04_09, SPRPort07_08], 542, [1, 1], 2>;
defm : X86WriteRes<WriteFStoreX, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : X86WriteRes<WriteFStoreY, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteFTest, [SPRPort00], 3, [1]>;
defm : SPRWriteResPair<WriteFTestY, [SPRPort00], 5, [1], 1, 6>;
defm : SPRWriteResPair<WriteFVarBlend, [SPRPort00_01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFVarBlendY, [SPRPort00_01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFVarBlendZ, [SPRPort00_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFVarShuffle, [SPRPort05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteFVarShuffle256, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteFVarShuffleY, [SPRPort05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteFVarShuffleZ, [SPRPort05], 1, [1], 1, 8>;
def : WriteRes<WriteFence, [SPRPort00_06]> {
  let Latency = 2;
}
defm : SPRWriteResPair<WriteIDiv16, [SPRPort00_01_05_06_10, SPRPort01], 16, [1, 3], 4, 4>;
defm : SPRWriteResPair<WriteIDiv32, [SPRPort00_01_05_06_10, SPRPort01], 15, [1, 3], 4, 4>;
defm : SPRWriteResPair<WriteIDiv64, [SPRPort01], 18, [3], 3>;
defm : X86WriteRes<WriteIDiv8, [SPRPort01], 17, [3], 3>;
defm : X86WriteRes<WriteIDiv8Ld, [SPRPort01], 22, [3], 3>;
defm : SPRWriteResPair<WriteIMul16, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01], 5, [2, 1, 1], 4>;
defm : SPRWriteResPair<WriteIMul16Imm, [SPRPort00_01_05_06_10, SPRPort01], 4, [1, 1], 2>;
defm : SPRWriteResPair<WriteIMul16Reg, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteIMul32, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01], 4, [1, 1, 1], 3>;
defm : SPRWriteResPair<WriteIMul32Imm, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteIMul32Reg, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteIMul64, [SPRPort01, SPRPort05], 4, [1, 1], 2>;
defm : SPRWriteResPair<WriteIMul64Imm, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteIMul64Reg, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteIMul8, [SPRPort01], 3, [1]>;
def : WriteRes<WriteIMulH, []> {
  let Latency = 3;
}
def : WriteRes<WriteIMulHLd, []> {
  let Latency = 3;
}
defm : SPRWriteResPair<WriteJump, [SPRPort00_06], 1, [1]>;
def : WriteRes<WriteLAHFSAHF, [SPRPort00_06]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteLDMXCSR, [SPRPort00, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11], 7, [1, 1, 1, 1], 4>;
def : WriteRes<WriteLEA, [SPRPort01]>;
defm : SPRWriteResPair<WriteLZCNT, [SPRPort01], 3, [1]>;
def : WriteRes<WriteLoad, [SPRPort02_03_11]> {
  let Latency = 5;
}
def : WriteRes<WriteMMXMOVMSK, [SPRPort00]> {
  let Latency = 3;
}
defm : SPRWriteResPair<WriteMPSAD, [SPRPort01_05, SPRPort05], 4, [1, 1], 2, 7>;
defm : SPRWriteResPair<WriteMPSADY, [SPRPort01_05, SPRPort05], 4, [1, 1], 2, 8>;
defm : SPRWriteResPair<WriteMULX32, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01], 4, [1, 1, 1], 2>;
defm : SPRWriteResPair<WriteMULX64, [SPRPort01, SPRPort05], 4, [1, 1]>;
def : WriteRes<WriteMicrocoded, [SPRPort00_01_05_06]> {
  let Latency = SapphireRapidsModel.MaxLatency;
}
def : WriteRes<WriteMove, [SPRPort00]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteNop, [], 1, [], 0>;
defm : X86WriteRes<WritePCmpEStrI, [SPRPort00, SPRPort00_01_05, SPRPort00_06, SPRPort01, SPRPort05], 16, [3, 2, 1, 1, 1], 8>;
defm : X86WriteRes<WritePCmpEStrILd, [SPRPort00, SPRPort00_01_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05], 31, [3, 1, 1, 1, 1, 1], 8>;
defm : X86WriteRes<WritePCmpEStrM, [SPRPort00, SPRPort00_01_05, SPRPort00_06, SPRPort01, SPRPort05], 16, [3, 3, 1, 1, 1], 9>;
defm : X86WriteRes<WritePCmpEStrMLd, [SPRPort00, SPRPort00_01_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05], 17, [3, 2, 1, 1, 1, 1], 9>;
defm : SPRWriteResPair<WritePCmpIStrI, [SPRPort00], 11, [3], 3, 20>;
defm : SPRWriteResPair<WritePCmpIStrM, [SPRPort00], 11, [3], 3>;
defm : SPRWriteResPair<WritePHAdd, [SPRPort00_05, SPRPort05], 3, [1, 2], 3, 8>;
defm : SPRWriteResPair<WritePHAddX, [SPRPort00_01_05, SPRPort01_05], 2, [1, 2], 3, 7>;
defm : SPRWriteResPair<WritePHAddY, [SPRPort00_01_05, SPRPort01_05], 2, [1, 2], 3, 8>;
defm : SPRWriteResPair<WritePHMINPOS, [SPRPort00], 4, [1], 1, 7>;
defm : SPRWriteResPair<WritePMULLD, [SPRPort00_01], 10, [2], 2, 8>;
defm : SPRWriteResPair<WritePMULLDY, [SPRPort00_01], 10, [2], 2, 8>;
defm : SPRWriteResPair<WritePMULLDZ, [SPRPort00], 10, [2], 2, 8>;
defm : SPRWriteResPair<WritePOPCNT, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WritePSADBW, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WritePSADBWX, [SPRPort05], 3, [1], 1, 7>;
defm : SPRWriteResPair<WritePSADBWY, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WritePSADBWZ, [SPRPort05], 3, [1], 1, 8>;
defm : X86WriteRes<WriteRMW, [SPRPort02_03_11, SPRPort04_09, SPRPort07_08], 1, [1, 1, 1], 3>;
defm : X86WriteRes<WriteRotate, [SPRPort00_01_05_06_10, SPRPort00_06], 2, [1, 2], 3>;
defm : X86WriteRes<WriteRotateLd, [SPRPort00_01_05_06_10, SPRPort00_06], 12, [1, 2], 3>;
defm : X86WriteRes<WriteRotateCL, [SPRPort00_06], 2, [2], 2>;
defm : X86WriteRes<WriteRotateCLLd, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01], 19, [2, 3, 2], 7>;
defm : X86WriteRes<WriteSETCC, [SPRPort00_06], 2, [2], 2>;
defm : X86WriteRes<WriteSETCCStore, [SPRPort00_06, SPRPort04_09, SPRPort07_08], 13, [2, 1, 1], 4>;
defm : X86WriteRes<WriteSHDmrcl, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1, 1, 1, 1], 6>;
defm : X86WriteRes<WriteSHDmri, [SPRPort00_01_05_06_10, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1, 1, 1], 5>;
defm : X86WriteRes<WriteSHDrrcl, [SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01], 5, [1, 1, 1], 3>;
def : WriteRes<WriteSHDrri, [SPRPort01]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteSTMXCSR, [SPRPort00, SPRPort00_06, SPRPort04_09, SPRPort07_08], 12, [1, 1, 1, 1], 4>;
def : WriteRes<WriteShift, [SPRPort00_06]>;
def : WriteRes<WriteShiftLd, [SPRPort00_06]> {
  let Latency = 12;
}
defm : X86WriteRes<WriteShiftCL, [SPRPort00_06], 2, [2], 2>;
defm : X86WriteRes<WriteShiftCLLd, [SPRPort00_06], 12, [2], 2>;
defm : SPRWriteResPair<WriteShuffle, [SPRPort05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteShuffle256, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteShuffleX, [SPRPort01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteShuffleY, [SPRPort01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteShuffleZ, [SPRPort05], 3, [1], 1, 6>;
defm : X86WriteRes<WriteStore, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : X86WriteRes<WriteStoreNT, [SPRPort04_09, SPRPort07_08], 512, [1, 1], 2>;
def : WriteRes<WriteSystem, [SPRPort00_01_05_06]> {
  let Latency = SapphireRapidsModel.MaxLatency;
}
defm : SPRWriteResPair<WriteTZCNT, [SPRPort01], 3, [1]>;
defm : SPRWriteResPair<WriteVPMOV256, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarBlend, [SPRPort00_01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVarBlendY, [SPRPort00_01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarBlendZ, [SPRPort00_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarShuffle, [SPRPort00, SPRPort05], 3, [1, 1], 2, 8>;
defm : X86WriteRes<WriteVarShuffle256, [SPRPort05], 6, [2], 2>;
defm : X86WriteRes<WriteVarShuffle256Ld, [SPRPort02_03_11, SPRPort05], 11, [1, 1], 2>;
defm : SPRWriteResPair<WriteVarShuffleX, [SPRPort01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVarShuffleY, [SPRPort01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarShuffleZ, [SPRPort05], 3, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarVecShift, [SPRPort00_01], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVarVecShiftY, [SPRPort00_01], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVarVecShiftZ, [SPRPort00], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecALU, [SPRPort00], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecALUX, [SPRPort00_01], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVecALUY, [SPRPort00_01], 1, [1], 1, 8>;
def : WriteRes<WriteVecALUZ, [SPRPort05]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteVecALUZLd, [SPRPort00, SPRPort02_03_11], 9, [1, 1], 2>;
defm : X86WriteRes<WriteVecExtract, [SPRPort00, SPRPort01_05], 4, [1, 1], 2>;
defm : X86WriteRes<WriteVecExtractSt, [SPRPort01_05, SPRPort04_09, SPRPort07_08], 19, [1, 1, 1], 3>;
defm : SPRWriteResPair<WriteVecIMul, [SPRPort00], 5, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecIMulX, [SPRPort00_01], 5, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecIMulY, [SPRPort00_01], 5, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecIMulZ, [SPRPort00], 5, [1], 1, 8>;
defm : X86WriteRes<WriteVecInsert, [SPRPort01_05, SPRPort05], 4, [1, 1], 2>;
defm : X86WriteRes<WriteVecInsertLd, [SPRPort01_05, SPRPort02_03_11], 8, [1, 1], 2>;
def : WriteRes<WriteVecLoad, [SPRPort02_03_11]> {
  let Latency = 7;
}
def : WriteRes<WriteVecLoadNT, [SPRPort02_03_11]> {
  let Latency = 7;
}
def : WriteRes<WriteVecLoadNTY, [SPRPort02_03_11]> {
  let Latency = 8;
}
def : WriteRes<WriteVecLoadX, [SPRPort02_03_11]> {
  let Latency = 7;
}
def : WriteRes<WriteVecLoadY, [SPRPort02_03_11]> {
  let Latency = 8;
}
defm : SPRWriteResPair<WriteVecLogic, [SPRPort00_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecLogicX, [SPRPort00_01_05], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVecLogicY, [SPRPort00_01_05], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecLogicZ, [SPRPort00_05], 1, [1], 1, 8>;
def : WriteRes<WriteVecMOVMSK, [SPRPort00]> {
  let Latency = 3;
}
def : WriteRes<WriteVecMOVMSKY, [SPRPort00]> {
  let Latency = 4;
}
defm : X86WriteRes<WriteVecMaskedGatherWriteback, [], 5, [], 0>;
defm : X86WriteRes<WriteVecMaskedLoad, [SPRPort00_01_05, SPRPort02_03_11], 8, [1, 1], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY, [SPRPort00_01_05, SPRPort02_03_11], 9, [1, 1], 2>;
defm : X86WriteRes<WriteVecMaskedStore32, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteVecMaskedStore32Y, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteVecMaskedStore64, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
defm : X86WriteRes<WriteVecMaskedStore64Y, [SPRPort00, SPRPort04_09, SPRPort07_08], 14, [1, 1, 1], 3>;
def : WriteRes<WriteVecMove, [SPRPort00_05]>;
def : WriteRes<WriteVecMoveFromGpr, [SPRPort05]> {
  let Latency = 3;
}
def : WriteRes<WriteVecMoveToGpr, [SPRPort00]> {
  let Latency = 3;
}
defm : X86WriteRes<WriteVecMoveX, [], 1, [], 0>;
def : WriteRes<WriteVecMoveY, [SPRPort00_01_05]>;
def : WriteRes<WriteVecMoveZ, [SPRPort00_05]>;
defm : SPRWriteResPair<WriteVecShift, [SPRPort00], 1, [1], 1, 8>;
def : WriteRes<WriteVecShiftImm, [SPRPort00]>;
defm : SPRWriteResPair<WriteVecShiftImmX, [SPRPort00_01], 1, [1], 1, 7>;
defm : SPRWriteResPair<WriteVecShiftImmY, [SPRPort00_01], 1, [1], 1, 8>;
defm : SPRWriteResPair<WriteVecShiftImmZ, [SPRPort00], 1, [1], 1, 8>;
defm : X86WriteRes<WriteVecShiftX, [SPRPort00_01, SPRPort01_05], 2, [1, 1], 2>;
defm : X86WriteRes<WriteVecShiftXLd, [SPRPort00_01, SPRPort02_03_11], 8, [1, 1], 2>;
defm : X86WriteRes<WriteVecShiftY, [SPRPort00_01, SPRPort05], 4, [1, 1], 2>;
defm : X86WriteRes<WriteVecShiftYLd, [SPRPort00_01, SPRPort02_03_11], 9, [1, 1], 2>;
defm : X86WriteRes<WriteVecShiftZ, [SPRPort00, SPRPort05], 4, [1, 1], 2>;
defm : X86WriteRes<WriteVecShiftZLd, [SPRPort00, SPRPort02_03_11], 9, [1, 1], 2>;
defm : X86WriteRes<WriteVecStore, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : X86WriteRes<WriteVecStoreNT, [SPRPort04_09, SPRPort07_08], 511, [1, 1], 2>;
defm : X86WriteRes<WriteVecStoreNTY, [SPRPort04_09, SPRPort07_08], 507, [1, 1], 2>;
defm : X86WriteRes<WriteVecStoreX, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : X86WriteRes<WriteVecStoreY, [SPRPort04_09, SPRPort07_08], 12, [1, 1], 2>;
defm : SPRWriteResPair<WriteVecTest, [SPRPort00, SPRPort05], 4, [1, 1], 2>;
defm : SPRWriteResPair<WriteVecTestY, [SPRPort00, SPRPort05], 6, [1, 1], 2, 6>;
defm : X86WriteRes<WriteXCHG, [SPRPort00_01_05_06_10], 2, [3], 3>;
def : WriteRes<WriteZero, []>;

// Infered SchedWriteRes and InstRW definition.

def SPRWriteResGroup0 : SchedWriteRes<[SPRPort02_03, SPRPort02_03_11, SPRPort04, SPRPort04_09]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup0], (instregex "^AA(D|N)D64mr$",
                                             "^A(X?)OR64mr$")>;

def SPRWriteResGroup1 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 1];
  let Latency = 12;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup1, ReadAfterLd, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^(ADC|SBB)(16|32|64)mr$")>;

def SPRWriteResGroup2 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup2], (instregex "^RORX(32|64)mi$")>;
def : InstRW<[SPRWriteResGroup2, ReadAfterLd, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^(ADC|SBB)(8|16|32|64)rm$",
                                                                                                                                        "^AD(C|O)X(32|64)rm$")>;

def SPRWriteResGroup3 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 13;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup3], (instregex "^(ADC|SBB)8mi(8?)$")>;

def SPRWriteResGroup4 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 1];
  let Latency = 13;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup4, ReadAfterLd, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^(ADC|SBB)8mr$")>;

def SPRWriteResGroup5 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup5], (instregex "^CMP(8|16|32)mi$",
                                             "^CMP(8|16|32|64)mi8$",
                                             "^MOV(8|16)rm$",
                                             "^POP(16|32)r((mr)?)$")>;
def : InstRW<[SPRWriteResGroup5], (instrs CMP64mi32,
                                          MOV8rm_NOREX,
                                          MOVZX16rm8)>;
def : InstRW<[SPRWriteResGroup5, ReadAfterLd], (instregex "^(ADD|CMP|SUB)(8|16|32|64)rm$",
                                                          "^AND(8|16|32)rm$",
                                                          "^(X?)OR(8|16|32)rm$")>;
def : InstRW<[SPRWriteResGroup5, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^CMP(8|16|32|64)mr$")>;

def SPRWriteResGroup6 : SchedWriteRes<[]> {
  let NumMicroOps = 0;
}
def : InstRW<[SPRWriteResGroup6], (instregex "^(ADD|SUB)64ri8$",
                                             "^(DE|IN)C64r$",
                                             "^MOV64rr((_REV)?)$",
                                             "^VMOV(A|U)P(D|S)Zrr((_REV)?)$",
                                             "^VMOVDQA(32|64)Z((256)?)rr((_REV)?)$",
                                             "^VMOVDQ(A|U)Yrr((_REV)?)$",
                                             "^VMOVDQU(8|16|32|64)Z((256)?)rr((_REV)?)$")>;
def : InstRW<[SPRWriteResGroup6], (instrs CLC,
                                          JMP_2)>;

def SPRWriteResGroup7 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 13;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup7], (instregex "^A(D|N)D8mi(8?)$",
                                             "^(DE|IN)C8m$",
                                             "^N(EG|OT)8m$",
                                             "^(X?)OR8mi(8?)$",
                                             "^SUB8mi(8?)$")>;
def : InstRW<[SPRWriteResGroup7, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^A(D|N)D8mr$",
                                                                                                                           "^(X?)OR8mr$")>;
def : InstRW<[SPRWriteResGroup7, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instrs SUB8mr)>;

def SPRWriteResGroup8 : SchedWriteRes<[SPRPort01_05, SPRPort02_03_11]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup8, ReadAfterVecXLd], (instregex "^(V?)(ADD|SUB)PSrm$",
                                                              "^(V?)ADDSUBPSrm$",
                                                              "^V(ADD|SUB)PSZ128rm((b|k|bk|kz)?)$",
                                                              "^V(ADD|SUB)PSZ128rmbkz$")>;

def SPRWriteResGroup9 : SchedWriteRes<[SPRPort01_05]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup9], (instregex "^(V?)(ADD|SUB)PSrr$",
                                             "^(V?)ADDSUBPSrr$",
                                             "^V(ADD|SUB)PSYrr$",
                                             "^V(ADD|SUB)PSZ(128|256)rr(k?)$",
                                             "^VPMOV(S|Z)XBWZ128rrk(z?)$",
                                             "^VPSHUFBZ(128|256)rrk(z?)$",
                                             "^VPSHUF(H|L)WZ(128|256)rik(z?)$",
                                             "^VPUNPCK(H|L)(BW|WD)Z(128|256)rrk(z?)$")>;
def : InstRW<[SPRWriteResGroup9], (instrs VADDSUBPSYrr)>;

def SPRWriteResGroup10 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup10], (instregex "^ADD_F(32|64)m$",
                                              "^ILD_F(16|32|64)m$",
                                              "^SUB(R?)_F(32|64)m$",
                                              "^VPOPCNT(B|D|Q|W)Z128rm$",
                                              "^VPOPCNT(D|Q)Z128rm(b|k|kz)$",
                                              "^VPOPCNT(D|Q)Z128rmbk(z?)$")>;
def : InstRW<[SPRWriteResGroup10, ReadAfterVecXLd], (instregex "^(V?)PACK(S|U)S(DW|WB)rm$",
                                                               "^(V?)PCMPGTQrm$",
                                                               "^VFPCLASSP(D|H|S)Z128rmb$",
                                                               "^VPACK(S|U)S(DW|WB)Z128rm$",
                                                               "^VPACK(S|U)SDWZ128rmb$",
                                                               "^VPM(AX|IN)(S|U)QZ128rm((b|k|bk|kz)?)$",
                                                               "^VPM(AX|IN)(S|U)QZ128rmbkz$",
                                                               "^VPMULTISHIFTQBZ128rm(b?)$")>;
def : InstRW<[SPRWriteResGroup10, ReadAfterVecXLd], (instrs VFPCLASSPHZ128rm)>;
def : InstRW<[SPRWriteResGroup10, ReadAfterVecYLd], (instregex "^VFPCLASSP(D|H|S)Z((256)?)rm$",
                                                               "^VPERM(I|T)2(D|Q|PS)Z128rm((b|k|bk|kz)?)$",
                                                               "^VPERM(I|T)2(D|Q|PS)Z128rmbkz$",
                                                               "^VPERM(I|T)2PDZ128rm((b|k|bk|kz)?)$",
                                                               "^VPERM(I|T)2PDZ128rmbkz$")>;
def : InstRW<[SPRWriteResGroup10, ReadAfterVecYLd], (instrs VPERMBZ128rm)>;

def SPRWriteResGroup11 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup11], (instregex "^ADD_FI(16|32)m$",
                                              "^SUB(R?)_FI(16|32)m$")>;
def : InstRW<[SPRWriteResGroup11, ReadAfterVecXLd], (instrs SHA256MSG2rm)>;
def : InstRW<[SPRWriteResGroup11, ReadAfterVecYLd], (instregex "^VPEXPAND(B|W)Z(128|256)rmk(z?)$",
                                                               "^VPEXPAND(B|W)Zrmk(z?)$")>;

def SPRWriteResGroup12 : SchedWriteRes<[SPRPort05]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup12], (instregex "^ADD_F(P?)rST0$",
                                              "^KMOV(B|D|W)kr$",
                                              "^(V?)PACK(S|U)S(DW|WB)rr$",
                                              "^(V?)PCMPGTQrr$",
                                              "^SUB(R?)_F(P?)rST0$",
                                              "^SUB(R?)_FST0r$",
                                              "^VALIGN(D|Q)Z256rri((k|kz)?)$",
                                              "^VCMPP(D|H|S)Z(128|256)rri(k?)$",
                                              "^VCMPS(D|H|S)Zrri$",
                                              "^VCMPS(D|H|S)Zrr(b?)i_Int(k?)$",
                                              "^VFPCLASSP(D|H|S)Z(128|256)rr(k?)$",
                                              "^VFPCLASSS(D|H|S)Zrr(k?)$",
                                              "^VPACK(S|U)S(DW|WB)Yrr$",
                                              "^VPACK(S|U)S(DW|WB)Z(128|256)rr$",
                                              "^VPALIGNRZ(128|256)rrik(z?)$",
                                              "^VPBROADCAST(B|W)Z128rrk(z?)$",
                                              "^VPCMP(B|D|Q|W|UD|UQ|UW)Z(128|256)rri(k?)$",
                                              "^VPCMP(EQ|GT)(B|D|Q|W)Z(128|256)rr(k?)$",
                                              "^VPCMPUBZ(128|256)rri(k?)$",
                                              "^VPERMBZ(128|256)rr$",
                                              "^VPERM(B|D|Q)Zrr$",
                                              "^VPERM(D|Q)Z256rr((k|kz)?)$",
                                              "^VPERM(D|Q)Zrrk(z?)$",
                                              "^VPERM(I|T)2(D|Q)Z(128|256)rr((k|kz)?)$",
                                              "^VPERM(I|T)2(D|Q)Zrr((k|kz)?)$",
                                              "^VPM(AX|IN)(S|U)QZ(128|256)rr((k|kz)?)$",
                                              "^VPMULTISHIFTQBZ(128|256)rr$",
                                              "^VPOPCNT(B|D|Q|W)Z(128|256)rr$",
                                              "^VPOPCNT(D|Q)Z(128|256)rrk(z?)$",
                                              "^VPTEST(N?)M(B|D|Q|W)Z(128|256)rr(k?)$",
                                              "^VPTEST(N?)M(B|D|Q|W)Zrr(k?)$")>;
def : InstRW<[SPRWriteResGroup12], (instrs ADD_FST0r,
                                           VPCMPGTQYrr,
                                           VPERMDYrr)>;

def SPRWriteResGroup13 : SchedWriteRes<[SPRPort00_01_05_06_10]> {
  let Latency = 2;
}
def : InstRW<[SPRWriteResGroup13], (instregex "^AND(8|16|32|64)r(r|i8)$",
                                              "^AND(8|16|32|64)rr_REV$",
                                              "^(AND|TEST)(32|64)i32$",
                                              "^(AND|TEST)(8|32)ri$",
                                              "^(AND|TEST)64ri32$",
                                              "^(AND|TEST)8i8$",
                                              "^(X?)OR(8|16|32|64)r(r|i8)$",
                                              "^(X?)OR(8|16|32|64)rr_REV$",
                                              "^(X?)OR(32|64)i32$",
                                              "^(X?)OR(8|32)ri$",
                                              "^(X?)OR64ri32$",
                                              "^(X?)OR8i8$",
                                              "^TEST(8|16|32|64)rr$")>;
def : InstRW<[SPRWriteResGroup13], (instrs XOR8rr_NOREX)>;

def SPRWriteResGroup14 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup14], (instregex "^TEST(8|16|32)mi$")>;
def : InstRW<[SPRWriteResGroup14], (instrs TEST64mi32)>;
def : InstRW<[SPRWriteResGroup14, ReadAfterLd], (instregex "^(X?)OR64rm$")>;
def : InstRW<[SPRWriteResGroup14, ReadAfterLd], (instrs AND64rm)>;
def : InstRW<[SPRWriteResGroup14, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^TEST(8|16|32|64)mr$")>;

def SPRWriteResGroup15 : SchedWriteRes<[SPRPort01_05_10, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup15, ReadAfterLd], (instregex "^ANDN(32|64)rm$")>;

def SPRWriteResGroup16 : SchedWriteRes<[SPRPort01_05_10]> {
  let Latency = 2;
}
def : InstRW<[SPRWriteResGroup16], (instregex "^ANDN(32|64)rr$")>;

def SPRWriteResGroup17 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [5, 2, 1, 1];
  let Latency = 10;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup17], (instrs BT64mr)>;

def SPRWriteResGroup18 : SchedWriteRes<[SPRPort01]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup18], (instregex "^BT((C|R|S)?)64rr$",
                                              "^P(DEP|EXT)(32|64)rr$")>;

def SPRWriteResGroup19 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [4, 2, 1, 1, 1, 1];
  let Latency = 17;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup19], (instregex "^BT(C|R|S)64mr$")>;

def SPRWriteResGroup20 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 7;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup20], (instregex "^CALL(16|32|64)m((_NT)?)$")>;

def SPRWriteResGroup21 : SchedWriteRes<[SPRPort00_06, SPRPort04_09, SPRPort07_08]> {
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup21], (instregex "^CALL(16|32|64)r((_NT)?)$")>;

def SPRWriteResGroup22 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup22], (instrs CALL64pcrel32,
                                           MFENCE)>;

def SPRWriteResGroup23 : SchedWriteRes<[SPRPort01_05]>;
def : InstRW<[SPRWriteResGroup23], (instregex "^C(DQ|WD)E$",
                                              "^(V?)MOVS(H|L)DUPrr$",
                                              "^(V?)SHUFP(D|S)rri$",
                                              "^VMOVS(H|L)DUPYrr$",
                                              "^VMOVS(H|L)DUPZ(128|256)rr((k|kz)?)$",
                                              "^VPMOVQDZ128rr((k|kz)?)$",
                                              "^VSHUFP(D|S)Yrri$",
                                              "^VSHUFP(D|S)Z(128|256)rri((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup23], (instrs CBW,
                                           VPBLENDWYrri)>;

def SPRWriteResGroup24 : SchedWriteRes<[SPRPort00_06]>;
def : InstRW<[SPRWriteResGroup24], (instregex "^C(DQ|QO)$",
                                              "^(CL|ST)AC$")>;

def SPRWriteResGroup25 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup25], (instrs CLD)>;

def SPRWriteResGroup26 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup26], (instrs CLDEMOTE)>;

def SPRWriteResGroup27 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort04_09, SPRPort07_08]> {
  let Latency = 2;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup27], (instrs CLFLUSH)>;

def SPRWriteResGroup28 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let Latency = 2;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup28], (instrs CLFLUSHOPT)>;

def SPRWriteResGroup29 : SchedWriteRes<[SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup29], (instrs CLI)>;

def SPRWriteResGroup30 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort05]> {
  let ReleaseAtCycles = [6, 1, 3];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup30], (instrs CLTS)>;

def SPRWriteResGroup31 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let Latency = 5;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup31], (instregex "^MOV16o(16|32|64)a$")>;
def : InstRW<[SPRWriteResGroup31], (instrs CLWB)>;

def SPRWriteResGroup32 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let ReleaseAtCycles = [5, 2];
  let Latency = 6;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup32], (instregex "^CMPS(B|L|Q|W)$")>;

def SPRWriteResGroup33 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01_05, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 7, 6, 2, 1, 1, 2, 1];
  let Latency = 32;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup33], (instrs CMPXCHG16B)>;

def SPRWriteResGroup34 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [4, 7, 2, 1, 1, 1];
  let Latency = 25;
  let NumMicroOps = 16;
}
def : InstRW<[SPRWriteResGroup34], (instrs CMPXCHG8B)>;

def SPRWriteResGroup35 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 1, 1, 1];
  let Latency = 13;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup35], (instrs CMPXCHG8rm)>;

def SPRWriteResGroup36 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_06, SPRPort01, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 10, 6, 1, 5, 1];
  let Latency = 18;
  let NumMicroOps = 26;
}
def : InstRW<[SPRWriteResGroup36], (instrs CPUID)>;

def SPRWriteResGroup37 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup37], (instregex "^(V?)CVT(T?)PD2DQrm$",
                                              "^VCVT(T?)PD2(U?)DQZ128rm((b|k|bk|kz)?)$",
                                              "^VCVT(T?)PD2(U?)DQZ128rmbkz$",
                                              "^VCVTPH2PSXZ128rm(b?)$",
                                              "^VCVT(U?)QQ2PSZ128rm((b|k|bk|kz)?)$",
                                              "^VCVT(U?)QQ2PSZ128rmbkz$")>;
def : InstRW<[SPRWriteResGroup37], (instrs CVTSI642SSrm)>;
def : InstRW<[SPRWriteResGroup37, ReadAfterVecLd], (instregex "^(V?)CVTSI642SSrm_Int$",
                                                              "^VCVT(U?)SI642SSZrm((_Int)?)$")>;
def : InstRW<[SPRWriteResGroup37, ReadAfterVecLd], (instrs VCVTSI642SSrm)>;

def SPRWriteResGroup38 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 26;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup38], (instregex "^(V?)CVT(T?)SD2SIrm((_Int)?)$")>;
def : InstRW<[SPRWriteResGroup38, ReadAfterVecLd], (instregex "^VCVT(T?)SD2SIZrm$",
                                                              "^VCVT(T?)SD2(U?)SIZrm_Int$")>;
def : InstRW<[SPRWriteResGroup38, ReadAfterVecLd], (instrs VCVTTSD2USIZrm)>;

def SPRWriteResGroup39 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup39], (instregex "^VCVT(T?)PS2(U?)QQZ256rr((k|kz)?)$",
                                              "^VCVT(U?)QQ2PSZ256rr((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup39, ReadInt2Fpu], (instrs CVTSI2SSrr)>;
def : InstRW<[SPRWriteResGroup39, ReadDefault, ReadInt2Fpu], (instregex "^(V?)CVTSI2SSrr_Int$",
                                                                        "^VCVT(U?)SI2SSZrr$",
                                                                        "^VCVT(U?)SI2SSZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup39, ReadDefault, ReadInt2Fpu], (instrs VCVTSI2SSrr)>;

def SPRWriteResGroup40 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup40, ReadInt2Fpu], (instrs CVTSI642SSrr)>;
def : InstRW<[SPRWriteResGroup40, ReadDefault, ReadInt2Fpu], (instregex "^(V?)CVTSI642SSrr_Int$",
                                                                        "^VCVT(U?)SI642SSZrr$",
                                                                        "^VCVT(U?)SI642SSZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup40, ReadDefault, ReadInt2Fpu], (instrs VCVTSI642SSrr)>;

def SPRWriteResGroup41 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup41], (instregex "^(V?)CVT(T?)SS2SI64rr_Int$",
                                              "^VCVT(T?)SS2SI64Zrr$",
                                              "^VCVT(T?)SS2(U?)SI64Zrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup41], (instrs VCVTTSS2USI64Zrr)>;
def : InstRW<[SPRWriteResGroup41, ReadDefault], (instregex "^(V?)CVT(T?)SS2SI64rr$")>;

def SPRWriteResGroup42 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup42], (instregex "^J(E|R)CXZ$")>;
def : InstRW<[SPRWriteResGroup42], (instrs CWD)>;

def SPRWriteResGroup43 : SchedWriteRes<[SPRPort00_01_05_06]>;
def : InstRW<[SPRWriteResGroup43], (instregex "^(LD|ST)_Frr$",
                                              "^MOV16s(m|r)$",
                                              "^MOV(32|64)sr$")>;
def : InstRW<[SPRWriteResGroup43], (instrs DEC16r_alt,
                                           SALC,
                                           ST_FPrr,
                                           SYSCALL)>;

def SPRWriteResGroup44 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 7;
}
def : InstRW<[SPRWriteResGroup44], (instrs DEC32r_alt)>;

def SPRWriteResGroup45 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 27;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup45], (instregex "^DIVR_F(32|64)m$")>;

def SPRWriteResGroup46 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 30;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup46], (instregex "^DIVR_FI(16|32)m$")>;

def SPRWriteResGroup47 : SchedWriteRes<[SPRPort00]> {
  let Latency = 15;
}
def : InstRW<[SPRWriteResGroup47], (instregex "^DIVR_F(P?)rST0$")>;
def : InstRW<[SPRWriteResGroup47], (instrs DIVR_FST0r)>;

def SPRWriteResGroup48 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 19;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup48, ReadAfterVecLd], (instregex "^(V?)DIVSDrm$")>;
def : InstRW<[SPRWriteResGroup48, ReadAfterVecLd], (instrs VDIVSDZrm)>;

def SPRWriteResGroup49 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 22;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup49], (instregex "^DIV_F(32|64)m$")>;
def : InstRW<[SPRWriteResGroup49, ReadAfterVecLd], (instregex "^VSQRTSHZm_Int((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup49, ReadAfterVecLd], (instrs VSQRTSHZm)>;

def SPRWriteResGroup50 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 25;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup50], (instregex "^DIV_FI(16|32)m$")>;

def SPRWriteResGroup51 : SchedWriteRes<[SPRPort00]> {
  let Latency = 20;
}
def : InstRW<[SPRWriteResGroup51], (instregex "^DIV_F(P?)rST0$")>;
def : InstRW<[SPRWriteResGroup51], (instrs DIV_FST0r)>;

def SPRWriteResGroup52 : SchedWriteRes<[SPRPort04, SPRPort04_09]>;
def : InstRW<[SPRWriteResGroup52], (instregex "^ENQCMD(S?)(16|32|64)$",
                                              "^PUSHA(16|32)$",
                                              "^ST_F(32|64)m$")>;
def : InstRW<[SPRWriteResGroup52], (instrs PUSHF32)>;

def SPRWriteResGroup53 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 21, 2, 14, 4, 9, 5];
  let Latency = 126;
  let NumMicroOps = 57;
}
def : InstRW<[SPRWriteResGroup53], (instrs ENTER)>;

def SPRWriteResGroup54 : SchedWriteRes<[SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup54], (instregex "^(V?)EXTRACTPSmr$",
                                              "^VPMOVQDZ((256)?)mr$")>;
def : InstRW<[SPRWriteResGroup54], (instrs SMSW16m,
                                           VEXTRACTPSZmr)>;

def SPRWriteResGroup55 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup55], (instregex "^(V?)EXTRACTPSrr$")>;
def : InstRW<[SPRWriteResGroup55], (instrs MMX_PEXTRWrr,
                                           VEXTRACTPSZrr,
                                           VPERMWZrr)>;

def SPRWriteResGroup56 : SchedWriteRes<[SPRPort02_03, SPRPort02_03_11, SPRPort04, SPRPort04_09, SPRPort06]> {
  let Latency = 7;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup56], (instrs FARCALL64m)>;

def SPRWriteResGroup57 : SchedWriteRes<[SPRPort02_03_11, SPRPort06]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup57], (instrs FARJMP64m,
                                           JMP64m_REX)>;

def SPRWriteResGroup58 : SchedWriteRes<[SPRPort04, SPRPort04_09]> {
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup58], (instregex "^(V?)MASKMOVDQU((64)?)$",
                                              "^ST_FP(32|64|80)m$")>;
def : InstRW<[SPRWriteResGroup58], (instrs FBSTPm,
                                           VMPTRSTm)>;

def SPRWriteResGroup59 : SchedWriteRes<[SPRPort00_05]> {
  let ReleaseAtCycles = [2];
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup59], (instrs FDECSTP)>;

def SPRWriteResGroup60 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 11;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup60], (instregex "^FICOM(P?)(16|32)m$")>;
def : InstRW<[SPRWriteResGroup60, ReadAfterVecYLd], (instregex "^VEXPANDP(D|S)Z((256)?)rm((k|kz)?)$",
                                                               "^VPEXPAND(B|D|Q|W)Z((256)?)rm$",
                                                               "^VPEXPAND(D|Q)Z((256)?)rmk(z?)$")>;

def SPRWriteResGroup61 : SchedWriteRes<[SPRPort00_05]>;
def : InstRW<[SPRWriteResGroup61], (instregex "^MMX_P(ADD|SUB)(B|D|Q|W)rr$",
                                              "^VP(ADD|SUB)(B|D|Q|W)Zrr$",
                                              "^VP(ADD|SUB)(D|Q)Zrrk(z?)$",
                                              "^VPTERNLOG(D|Q)Zrri((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup61], (instrs FINCSTP,
                                           FNOP)>;

def SPRWriteResGroup62 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup62], (instrs FLDCW16m)>;

def SPRWriteResGroup63 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 5, 10, 39, 8];
  let Latency = 62;
  let NumMicroOps = 64;
}
def : InstRW<[SPRWriteResGroup63], (instrs FLDENVm)>;

def SPRWriteResGroup64 : SchedWriteRes<[SPRPort00_01_05_06]> {
  let ReleaseAtCycles = [4];
  let Latency = 4;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup64], (instrs FNCLEX)>;

def SPRWriteResGroup65 : SchedWriteRes<[SPRPort00_01_05_06, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [6, 3, 6];
  let Latency = 75;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup65], (instrs FNINIT)>;

def SPRWriteResGroup66 : SchedWriteRes<[SPRPort04, SPRPort04_09, SPRPort06]> {
  let Latency = 2;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup66], (instrs FNSTCW16m)>;

def SPRWriteResGroup67 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup67], (instrs FNSTSW16r)>;

def SPRWriteResGroup68 : SchedWriteRes<[SPRPort00, SPRPort04, SPRPort04_09]> {
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup68], (instrs FNSTSWm)>;

def SPRWriteResGroup69 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06, SPRPort00_06, SPRPort01, SPRPort04, SPRPort04_09, SPRPort05, SPRPort06]> {
  let ReleaseAtCycles = [9, 11, 21, 1, 30, 11, 16, 1];
  let Latency = 106;
  let NumMicroOps = 100;
}
def : InstRW<[SPRWriteResGroup69], (instrs FSTENVm)>;

def SPRWriteResGroup70 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort01_05, SPRPort02_03, SPRPort02_03_11, SPRPort06]> {
  let ReleaseAtCycles = [4, 1, 2, 1, 47, 33, 2];
  let Latency = 63;
  let NumMicroOps = 90;
}
def : InstRW<[SPRWriteResGroup70], (instrs FXRSTOR)>;

def SPRWriteResGroup71 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort01_05, SPRPort02_03, SPRPort02_03_11, SPRPort06]> {
  let ReleaseAtCycles = [4, 1, 2, 1, 45, 31, 4];
  let Latency = 63;
  let NumMicroOps = 88;
}
def : InstRW<[SPRWriteResGroup71], (instrs FXRSTOR64)>;

def SPRWriteResGroup72 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 5, 10, 10, 2, 38, 5, 38];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 110;
}
def : InstRW<[SPRWriteResGroup72], (instregex "^FXSAVE((64)?)$")>;

def SPRWriteResGroup73 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 12;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup73], (instregex "^VPLZCNT(D|Q)Z256rm((b|k|bk|kz)?)$",
                                              "^VPLZCNT(D|Q)Z256rmbkz$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecXLd], (instregex "^(V?)GF2P8AFFINE((INV)?)QBrmi$",
                                                               "^(V?)GF2P8MULBrm$",
                                                               "^V(ADD|SUB)PHZ128rm((b|k|bk|kz)?)$",
                                                               "^V(ADD|SUB)PHZ128rmbkz$",
                                                               "^VGETEXPPHZ128m((b|k|bk|kz)?)$",
                                                               "^VGETEXPSHZm((k|kz)?)$",
                                                               "^VGETMANTPHZ128rm(bi|ik)$",
                                                               "^VGETMANTPHZ128rmbik(z?)$",
                                                               "^VGETMANTPHZ128rmi((kz)?)$",
                                                               "^VGETMANTSHZrmi((k|kz)?)$",
                                                               "^VGF2P8AFFINE((INV)?)QBZ128rm(b?)i$",
                                                               "^VM(AX|IN)CPHZ128rm((b|k|bk|kz)?)$",
                                                               "^VM(AX|IN)CPHZ128rmbkz$",
                                                               "^VM(AX|IN|UL)PHZ128rm((b|k|bk|kz)?)$",
                                                               "^VM(AX|IN|UL)PHZ128rmbkz$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecXLd], (instrs VGETEXPPHZ128mbkz,
                                                            VGF2P8MULBZ128rm)>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecLd], (instregex "^V(ADD|SUB)SHZrm$",
                                                              "^V(ADD|SUB)SHZrm_Int((k|kz)?)$",
                                                              "^VCVTSH2SSZrm((_Int)?)$",
                                                              "^VM(AX|IN)CSHZrm$",
                                                              "^VM(AX|IN|UL)SHZrm$",
                                                              "^VM(AX|IN|UL)SHZrm_Int((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecYLd], (instregex "^VGF2P8AFFINE((INV)?)QBYrmi$",
                                                               "^VGF2P8AFFINE((INV)?)QBZ256rm(b?)i$",
                                                               "^VGF2P8MULB(Y|Z256)rm$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecXLd, ReadAfterVecXLd], (instregex "^VF(N?)M(ADD|SUB)(132|213|231)PHZ128m((b|k|bk|kz)?)$",
                                                                                "^VF(N?)M(ADD|SUB)(132|213|231)PHZ128mbkz$",
                                                                                "^VFMADDSUB(132|213|231)PHZ128m((b|k|bk|kz)?)$",
                                                                                "^VFMADDSUB(132|213|231)PHZ128mbkz$",
                                                                                "^VFMSUBADD(132|213|231)PHZ128m((b|k|bk|kz)?)$",
                                                                                "^VFMSUBADD(132|213|231)PHZ128mbkz$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecLd, ReadAfterVecLd], (instregex "^VF(N?)M(ADD|SUB)(132|213|231)SHZm$",
                                                                              "^VF(N?)M(ADD|SUB)(132|213|231)SHZm_Int((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup73, ReadAfterVecYLd, ReadAfterVecYLd], (instregex "^VPMADD52(H|L)UQZ256m((b|k|bk|kz)?)$",
                                                                                "^VPMADD52(H|L)UQZ256mbkz$")>;

def SPRWriteResGroup74 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 5;
}
def : InstRW<[SPRWriteResGroup74], (instregex "^(V?)GF2P8MULBrr$",
                                              "^V(ADD|SUB)PHZ(128|256)rr$",
                                              "^V(ADD|SUB)SHZrr$",
                                              "^V(ADD|SUB)SHZrr(b?)_Int$",
                                              "^VCVT(T?)PH2(U?)WZ(128|256)rr$",
                                              "^VCVTSH2SSZrr(b?)_Int$",
                                              "^VCVT(U?)W2PHZ(128|256)rr$",
                                              "^VF(N?)M(ADD|SUB)(132|213|231)PHZ(128|256)r$",
                                              "^VF(N?)M(ADD|SUB)(132|213|231)SHZr(b?)((_Int)?)$",
                                              "^VFMADDSUB(132|213|231)PHZ(128|256)r$",
                                              "^VFMSUBADD(132|213|231)PHZ(128|256)r$",
                                              "^VGETEXPPHZ(128|256)r$",
                                              "^VGETEXPSHZr(b?)$",
                                              "^VGETMANTPHZ(128|256)rri$",
                                              "^VGETMANTSHZrri(b?)$",
                                              "^VGF2P8MULBZ(128|256)rr$",
                                              "^VM(AX|IN)CPHZ(128|256)rr$",
                                              "^VM(AX|IN)CSHZrr$",
                                              "^VM(AX|IN|UL)PHZ(128|256)rr$",
                                              "^VM(AX|IN|UL)SHZrr$",
                                              "^VM(AX|IN|UL)SHZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup74], (instrs VCVTSH2SSZrr,
                                           VGF2P8MULBYrr)>;

def SPRWriteResGroup75 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 5, 26, 19, 2, 7, 21];
  let Latency = 35;
  let NumMicroOps = 87;
}
def : InstRW<[SPRWriteResGroup75], (instrs IN16ri)>;

def SPRWriteResGroup76 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 1, 4, 26, 19, 3, 7, 20];
  let Latency = 35;
  let NumMicroOps = 87;
}
def : InstRW<[SPRWriteResGroup76], (instrs IN16rr)>;

def SPRWriteResGroup77 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 28, 21, 2, 10, 20];
  let Latency = 35;
  let NumMicroOps = 94;
}
def : InstRW<[SPRWriteResGroup77], (instrs IN32ri)>;

def SPRWriteResGroup78 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 9, 28, 21, 2, 11, 21];
  let NumMicroOps = 99;
}
def : InstRW<[SPRWriteResGroup78], (instrs IN32rr)>;

def SPRWriteResGroup79 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 25, 19, 2, 8, 20];
  let Latency = 35;
  let NumMicroOps = 87;
}
def : InstRW<[SPRWriteResGroup79], (instrs IN8ri)>;

def SPRWriteResGroup80 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 25, 19, 2, 7, 20];
  let Latency = 35;
  let NumMicroOps = 86;
}
def : InstRW<[SPRWriteResGroup80], (instrs IN8rr)>;

def SPRWriteResGroup81 : SchedWriteRes<[SPRPort00_06]> {
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup81], (instrs INC16r_alt)>;

def SPRWriteResGroup82 : SchedWriteRes<[SPRPort02_03_11]> {
  let Latency = 7;
}
def : InstRW<[SPRWriteResGroup82], (instregex "^LD_F(32|64|80)m$",
                                              "^(V?)MOV(D|SH|SL)DUPrm$",
                                              "^VBROADCASTSS((Z128)?)rm$",
                                              "^VMOV(D|SH|SL)DUPZ128rm$",
                                              "^VPBROADCAST(D|Q)((Z128)?)rm$")>;
def : InstRW<[SPRWriteResGroup82], (instrs INC32r_alt,
                                           VBROADCASTI32X2Z128rm)>;

def SPRWriteResGroup83 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [7, 6, 24, 17, 8, 1, 19, 1];
  let Latency = 20;
  let NumMicroOps = 83;
}
def : InstRW<[SPRWriteResGroup83], (instrs INSB)>;

def SPRWriteResGroup84 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [7, 1, 5, 1, 27, 17, 11, 1, 21, 1];
  let Latency = 20;
  let NumMicroOps = 92;
}
def : InstRW<[SPRWriteResGroup84], (instrs INSL)>;

def SPRWriteResGroup85 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [7, 1, 4, 1, 25, 17, 1, 9, 1, 19, 1];
  let Latency = 20;
  let NumMicroOps = 86;
}
def : InstRW<[SPRWriteResGroup85], (instrs INSW)>;

def SPRWriteResGroup86 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [5, 4, 8, 6, 2, 5, 7, 5];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 42;
}
def : InstRW<[SPRWriteResGroup86], (instrs INVLPG)>;

def SPRWriteResGroup87 : SchedWriteRes<[SPRPort04, SPRPort04_09, SPRPort05]> {
  let Latency = 4;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup87], (instregex "^IST(T?)_FP(16|32|64)m$",
                                              "^IST_F(16|32)m$")>;

def SPRWriteResGroup88 : SchedWriteRes<[SPRPort00_01_05_06, SPRPort00_06]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup88], (instrs JCXZ)>;

def SPRWriteResGroup89 : SchedWriteRes<[SPRPort06]>;
def : InstRW<[SPRWriteResGroup89], (instrs JMP64r_REX)>;

def SPRWriteResGroup90 : SchedWriteRes<[]> {
  let Latency = 0;
  let NumMicroOps = 0;
}
def : InstRW<[SPRWriteResGroup90], (instregex "^JMP_(1|4)$")>;
def : InstRW<[SPRWriteResGroup90], (instrs VZEROUPPER)>;

def SPRWriteResGroup91 : SchedWriteRes<[SPRPort05]> {
  let Latency = 4;
}
def : InstRW<[SPRWriteResGroup91], (instregex "^KADD(B|D|Q|W)rr$",
                                              "^KSHIFT(LB|RD|RQ|RW)ri$",
                                              "^KSHIFT(LD|RB)ri$",
                                              "^KSHIFTL(Q|W)ri$",
                                              "^KUNPCK(BW|DQ|WD)rr$")>;

def SPRWriteResGroup92 : SchedWriteRes<[SPRPort00]>;
def : InstRW<[SPRWriteResGroup92], (instregex "^KAND(B|D|Q|W|ND|NQ|NW)rr$",
                                              "^KMOV(B|D|Q|W)kk$",
                                              "^KNOT(B|D|Q|W)rr$",
                                              "^K((X|XN)?)OR(B|D|Q|W)rr$",
                                              "^VP(A|SU)BSBZrr$",
                                              "^VPABS(D|Q|W)Zrr$",
                                              "^VPABS(D|Q)Zrrk(z?)$",
                                              "^VPADD(U?)S(B|W)Zrr$",
                                              "^VPAVG(B|W)Zrr$",
                                              "^VPM(AX|IN)(SB|UD|UW)Zrr$",
                                              "^VPM(AX|IN)(SD|UB)Zrr$",
                                              "^VPM(AX|IN)(S|U)DZrrk(z?)$",
                                              "^VPM(AX|IN)SWZrr$",
                                              "^VPSH(L|R)D(D|Q|W)Zrri$",
                                              "^VPSH(L|R)DV(D|Q|W)Zr$",
                                              "^VPSH(L|R)DV(D|Q)Zrk(z?)$",
                                              "^VPSUB(U?)SWZrr$")>;
def : InstRW<[SPRWriteResGroup92], (instrs KANDNBrr,
                                           VPSUBUSBZrr)>;

def SPRWriteResGroup93 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup93], (instregex "^KMOV(B|D|Q|W)km$")>;

def SPRWriteResGroup94 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup94], (instregex "^MOV8m(i|r)$")>;
def : InstRW<[SPRWriteResGroup94], (instrs KMOVBmk,
                                           MOV8mr_NOREX)>;

def SPRWriteResGroup95 : SchedWriteRes<[SPRPort05]>;
def : InstRW<[SPRWriteResGroup95], (instregex "^(V?)PALIGNRrri$",
                                              "^VALIGN(D|Q)Z128rri((k|kz)?)$",
                                              "^VBROADCASTSSZ128rr((k|kz)?)$",
                                              "^VPALIGNR(Y|Z)rri$",
                                              "^VPALIGNRZ(128|256)rri$",
                                              "^VPBROADCAST(B|D|Q|W)rr$",
                                              "^VPSHUF(D|HW|LW)Zri$",
                                              "^VPSHUFDZrik(z?)$",
                                              "^VPS(L|R)LDQZri$",
                                              "^VPUNPCK(H|L)(BW|WD)Zrr$",
                                              "^VPUNPCK(H|L|LQ)DQZrr((k|kz)?)$",
                                              "^VPUNPCKHQDQZrr((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup95], (instrs KMOVQkr,
                                           VPSHUFBZrr)>;

def SPRWriteResGroup96 : SchedWriteRes<[SPRPort00]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup96], (instregex "^K((OR)?)TEST(B|D|Q|W)rr$",
                                              "^VP(A|SU)BS(B|W)Zrrk(z?)$",
                                              "^VPADD(U?)S(B|W)Zrrk(z?)$",
                                              "^VPAVG(B|W)Zrrk(z?)$",
                                              "^VPM(AX|IN)(SB|UW)Zrrk(z?)$",
                                              "^VPM(AX|IN)(SW|UB)Zrrk(z?)$",
                                              "^VPSH(L|R)DVWZrk(z?)$",
                                              "^VPS(L|R)LVWZrrk(z?)$",
                                              "^VPS(L|R)LWZrik(z?)$",
                                              "^VPSRAVWZrrk(z?)$",
                                              "^VPSRAWZrik(z?)$",
                                              "^VPSUBUS(B|W)Zrrk(z?)$")>;
def : InstRW<[SPRWriteResGroup96], (instrs VMOVSDto64Zrr)>;

def SPRWriteResGroup97 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [8, 2, 14, 3, 1];
  let Latency = 198;
  let NumMicroOps = 81;
}
def : InstRW<[SPRWriteResGroup97], (instrs LAR16rm)>;

def SPRWriteResGroup98 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 3, 1, 8, 5, 1, 2, 1];
  let Latency = 66;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup98], (instrs LAR16rr)>;

def SPRWriteResGroup99 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2, 2, 9, 5, 3, 1];
  let Latency = 71;
  let NumMicroOps = 85;
}
def : InstRW<[SPRWriteResGroup99], (instrs LAR32rm)>;

def SPRWriteResGroup100 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 3, 1, 8, 5, 1, 2, 1];
  let Latency = 65;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup100], (instregex "^LAR(32|64)rr$")>;

def SPRWriteResGroup101 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2, 2, 9, 5, 3, 1];
  let Latency = 71;
  let NumMicroOps = 87;
}
def : InstRW<[SPRWriteResGroup101], (instrs LAR64rm)>;

def SPRWriteResGroup102 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup102], (instrs LEA16r)>;

def SPRWriteResGroup103 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 6;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup103], (instregex "^LODS(B|W)$",
                                               "^SCAS(B|L|Q|W)$")>;
def : InstRW<[SPRWriteResGroup103], (instrs LEAVE)>;

def SPRWriteResGroup104 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 6;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup104], (instrs LEAVE64)>;

def SPRWriteResGroup105 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 4, 3, 2, 1, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 14;
}
def : InstRW<[SPRWriteResGroup105], (instrs LGDT64m)>;

def SPRWriteResGroup106 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 5, 3, 2, 1, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 14;
}
def : InstRW<[SPRWriteResGroup106], (instrs LIDT64m)>;

def SPRWriteResGroup107 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [5, 3, 2, 1, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 12;
}
def : InstRW<[SPRWriteResGroup107], (instrs LLDT16m)>;

def SPRWriteResGroup108 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 4, 3, 1, 1, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup108], (instrs LLDT16r)>;

def SPRWriteResGroup109 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 2, 8, 3, 1, 2, 7, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 27;
}
def : InstRW<[SPRWriteResGroup109], (instrs LMSW16m)>;

def SPRWriteResGroup110 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [5, 7, 1, 2, 5, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup110], (instrs LMSW16r)>;

def SPRWriteResGroup111 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 5;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup111], (instregex "^LODS(L|Q)$")>;

def SPRWriteResGroup112 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [2, 4, 1];
  let Latency = 3;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup112], (instrs LOOP)>;

def SPRWriteResGroup113 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [4, 6, 1];
  let Latency = 3;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup113], (instrs LOOPE)>;

def SPRWriteResGroup114 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [4, 6, 1];
  let Latency = 2;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup114], (instrs LOOPNE)>;

def SPRWriteResGroup115 : SchedWriteRes<[SPRPort02_03, SPRPort02_03_11, SPRPort06]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup115], (instrs LRET64)>;

def SPRWriteResGroup116 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 5, 3, 3, 1];
  let Latency = 70;
  let NumMicroOps = 13;
}
def : InstRW<[SPRWriteResGroup116], (instregex "^LSL(16|32|64)rm$")>;

def SPRWriteResGroup117 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 4, 4, 3, 2, 1];
  let Latency = 63;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup117], (instregex "^LSL(16|32|64)rr$")>;

def SPRWriteResGroup118 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 24;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup118], (instregex "^MMX_CVT(T?)PD2PIrm$")>;

def SPRWriteResGroup119 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup119], (instregex "^MMX_CVT(T?)PD2PIrr$",
                                               "^VCVT(T?)PH2(U?)DQZ(128|256)rr$",
                                               "^VCVTP(H2PS|S2PH)XZ256rr$")>;

def SPRWriteResGroup120 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup120], (instregex "^VCVTP(H2PS|S2PH)XZ128rr$",
                                               "^VPERMWZ(128|256)rrk(z?)$",
                                               "^VPS(L|R)LWZ256rrk(z?)$",
                                               "^VPSRAWZ256rrk(z?)$")>;
def : InstRW<[SPRWriteResGroup120], (instrs MMX_CVTPI2PDrr)>;

def SPRWriteResGroup121 : SchedWriteRes<[SPRPort00, SPRPort00_01]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup121], (instrs MMX_CVTPI2PSrr)>;

def SPRWriteResGroup122 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup122], (instregex "^MMX_CVT(T?)PS2PIrm$")>;

def SPRWriteResGroup123 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup123], (instregex "^MMX_CVT(T?)PS2PIrr$")>;

def SPRWriteResGroup124 : SchedWriteRes<[SPRPort00, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup124], (instregex "^MMX_MASKMOVQ((64)?)$")>;

def SPRWriteResGroup125 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 18;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup125], (instregex "^VMOV(W|SHZ)mr$")>;
def : InstRW<[SPRWriteResGroup125], (instrs MMX_MOVD64mr)>;

def SPRWriteResGroup126 : SchedWriteRes<[SPRPort02_03_11]> {
  let Latency = 8;
}
def : InstRW<[SPRWriteResGroup126], (instregex "^MMX_MOV(D|Q)64rm$",
                                               "^VBROADCAST(F|I)128rm$",
                                               "^VBROADCAST(F|I)32X(2|4)Z256rm$",
                                               "^VBROADCAST(F|I)32X(8|2Z)rm$",
                                               "^VBROADCAST(F|I)(32|64)X4rm$",
                                               "^VBROADCAST(F|I)64X2((Z128)?)rm$",
                                               "^VBROADCASTS(DY|SZ)rm$",
                                               "^VBROADCASTS(D|S)Z256rm$",
                                               "^VBROADCASTS(DZ|SY)rm$",
                                               "^VMOV(D|SH|SL)DUP(Y|Z)rm$",
                                               "^VMOV(D|SH|SL)DUPZ256rm$",
                                               "^VPBROADCAST(DY|QZ)rm$",
                                               "^VPBROADCAST(D|Q)Z256rm$",
                                               "^VPBROADCAST(DZ|QY)rm$")>;
def : InstRW<[SPRWriteResGroup126], (instrs MMX_MOVD64to64rm)>;

def SPRWriteResGroup127 : SchedWriteRes<[SPRPort00_01_05, SPRPort00_05]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup127], (instregex "^MMX_MOV(DQ|FR64)2Qrr$")>;

def SPRWriteResGroup128 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup128], (instregex "^MMX_MOVQ2(DQ|FR64)rr$")>;

def SPRWriteResGroup129 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup129, ReadAfterVecLd], (instregex "^MMX_PACKSS(DW|WB)rm$")>;
def : InstRW<[SPRWriteResGroup129, ReadAfterVecLd], (instrs MMX_PACKUSWBrm)>;

def SPRWriteResGroup130 : SchedWriteRes<[SPRPort05]> {
  let ReleaseAtCycles = [2];
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup130], (instregex "^MMX_PACKSS(DW|WB)rr$",
                                               "^VPMOV(D|Q|W|SQ|SW)BZrr$",
                                               "^VPMOV((S|US)?)(D|Q)WZrr$",
                                               "^VPMOV(U?)S(DB|QD)Zrr$",
                                               "^VPMOV(U?)SQDZrrk(z?)$",
                                               "^VPMOVUS(Q|W)BZrr$")>;
def : InstRW<[SPRWriteResGroup130], (instrs MMX_PACKUSWBrr)>;
def : InstRW<[SPRWriteResGroup130, ReadDefault, ReadInt2Fpu], (instrs MMX_PINSRWrr)>;

def SPRWriteResGroup131 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup131], (instregex "^VBROADCAST(F|I)32X(8|2Z)rmk(z?)$",
                                               "^VBROADCAST(F|I)(32|64)X4rmk(z?)$",
                                               "^VBROADCAST(F|I)64X2rmk(z?)$",
                                               "^VBROADCASTS(D|S)Zrmk(z?)$",
                                               "^VMOV(A|U)P(D|S)Zrmk(z?)$",
                                               "^VMOV(D|SH|SL)DUPZrmk(z?)$",
                                               "^VMOVDQ(A|U)(32|64)Zrmk(z?)$",
                                               "^VPBROADCAST(D|Q)Zrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup131, ReadAfterVecLd], (instregex "^MMX_P(ADD|SUB)(B|D|Q|W)rm$")>;
def : InstRW<[SPRWriteResGroup131, ReadAfterVecYLd], (instregex "^VINSERT(F|I)(32|64)x4Zrm((k|kz)?)$",
                                                                "^VINSERT(F|I)(32x8|64x2)Zrm((k|kz)?)$",
                                                                "^VP(ADD|SUB)(B|D|Q|W)Zrm$",
                                                                "^VP(ADD|SUB)(D|Q)Zrm(b|k|kz)$",
                                                                "^VP(ADD|SUB)(D|Q)Zrmbk(z?)$",
                                                                "^VPTERNLOG(D|Q)Zrm(bi|ik)$",
                                                                "^VPTERNLOG(D|Q)Zrmbik(z?)$",
                                                                "^VPTERNLOG(D|Q)Zrmi((kz)?)$")>;

def SPRWriteResGroup132 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 11;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup132, ReadAfterVecLd], (instregex "^MMX_PH(ADD|SUB)SWrm$")>;

def SPRWriteResGroup133 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup133], (instregex "^MMX_PH(ADD|SUB)SWrr$")>;

def SPRWriteResGroup134 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup134], (instregex "^VPBROADCAST(BY|WZ)rm$",
                                               "^VPBROADCAST(B|W)Z256rm$",
                                               "^VPBROADCAST(BZ|WY)rm$")>;
def : InstRW<[SPRWriteResGroup134, ReadAfterLd], (instrs MMX_PINSRWrm)>;
def : InstRW<[SPRWriteResGroup134, ReadAfterVecXLd], (instregex "^VFPCLASSP(D|S)Z128rm$")>;
def : InstRW<[SPRWriteResGroup134, ReadAfterVecLd], (instregex "^VFPCLASSS(D|H|S)Zrm$")>;
def : InstRW<[SPRWriteResGroup134, ReadAfterVecYLd], (instregex "^VPALIGNR(Y|Z256)rmi$")>;
def : InstRW<[SPRWriteResGroup134, ReadAfterVecYLd], (instrs VPSHUFBZrm)>;

def SPRWriteResGroup135 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup135], (instregex "^MOV16ao(16|32|64)$")>;

def SPRWriteResGroup136 : SchedWriteRes<[SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup136], (instregex "^PUSH(F|G)S(16|32)$")>;
def : InstRW<[SPRWriteResGroup136], (instrs MOV16ms,
                                            MOVBE32mr)>;

def SPRWriteResGroup137 : SchedWriteRes<[SPRPort00_01_05_06_10]>;
def : InstRW<[SPRWriteResGroup137], (instregex "^MOV(8|16|32|64)ri$",
                                               "^MOV(8|16|32)ri_alt$",
                                               "^MOV(8|16)rr((_REV)?)$")>;
def : InstRW<[SPRWriteResGroup137], (instrs MOV64ri32,
                                            MOV8rr_NOREX)>;

def SPRWriteResGroup138 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01]> {
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup138], (instregex "^MOV(16|32|64)rs$",
                                               "^S(TR|LDT)16r$")>;

def SPRWriteResGroup139 : SchedWriteRes<[SPRPort02_03_11]>;
def : InstRW<[SPRWriteResGroup139], (instregex "^MOV32ao(16|32|64)$")>;
def : InstRW<[SPRWriteResGroup139], (instrs MOV64ao64)>;

def SPRWriteResGroup140 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup140], (instregex "^MOV(8|32)o(16|32)a$",
                                               "^MOV(8|32|64)o64a$")>;

def SPRWriteResGroup141 : SchedWriteRes<[SPRPort00_01_05_06_10]> {
  let Latency = 0;
}
def : InstRW<[SPRWriteResGroup141], (instregex "^MOV32rr((_REV)?)$",
                                               "^MOVZX(32|64)rr8$")>;
def : InstRW<[SPRWriteResGroup141], (instrs MOVZX32rr8_NOREX)>;

def SPRWriteResGroup142 : SchedWriteRes<[SPRPort02_03_11]> {
  let Latency = 5;
}
def : InstRW<[SPRWriteResGroup142], (instrs MOV64ao32)>;

def SPRWriteResGroup143 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 4, 16, 7, 2, 2, 12, 2];
  let Latency = 217;
  let NumMicroOps = 48;
}
def : InstRW<[SPRWriteResGroup143], (instrs MOV64dr)>;

def SPRWriteResGroup144 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup144], (instrs MOV64o32a)>;

def SPRWriteResGroup145 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort05]> {
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup145], (instrs MOV64rc)>;

def SPRWriteResGroup146 : SchedWriteRes<[SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort05]> {
  let ReleaseAtCycles = [3, 4, 8, 4, 2, 3];
  let Latency = 181;
  let NumMicroOps = 24;
}
def : InstRW<[SPRWriteResGroup146], (instrs MOV64rd)>;

def SPRWriteResGroup147 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup147], (instregex "^MOV8ao(16|32|64)$")>;

def SPRWriteResGroup148 : SchedWriteRes<[SPRPort00_06, SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup148], (instrs MOVBE16mr)>;

def SPRWriteResGroup149 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup149], (instrs MOVBE16rm)>;

def SPRWriteResGroup150 : SchedWriteRes<[SPRPort01, SPRPort02_03_11]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup150], (instrs MOVBE32rm)>;

def SPRWriteResGroup151 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup151], (instrs MOVBE64mr,
                                            PUSHF16,
                                            SLDT16m,
                                            STRm)>;

def SPRWriteResGroup152 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup152], (instrs MOVBE64rm)>;

def SPRWriteResGroup153 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup153], (instregex "^MOVDIR64B(16|32|64)$")>;

def SPRWriteResGroup154 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 511;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup154], (instrs MOVDIRI32)>;

def SPRWriteResGroup155 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 514;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup155], (instrs MOVDIRI64)>;

def SPRWriteResGroup156 : SchedWriteRes<[SPRPort01_05, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup156, ReadAfterVecXLd], (instregex "^(V?)MOVLP(D|S)rm$",
                                                                "^(V?)SHUFP(D|S)rmi$",
                                                                "^VMOVLP(D|S)Z128rm$",
                                                                "^VSHUFP(D|S)Z128rm(bi|ik)$",
                                                                "^VSHUFP(D|S)Z128rmbik(z?)$",
                                                                "^VSHUFP(D|S)Z128rmi((kz)?)$")>;

def SPRWriteResGroup157 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 512;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup157], (instrs MOVNTDQmr)>;

def SPRWriteResGroup158 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 518;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup158], (instrs MOVNTImr)>;

def SPRWriteResGroup159 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [4, 1, 1, 1];
  let Latency = 8;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup159], (instrs MOVSB)>;

def SPRWriteResGroup160 : SchedWriteRes<[SPRPort00_01_05]>;
def : InstRW<[SPRWriteResGroup160], (instregex "^(V?)MOVS(D|S)rr((_REV)?)$",
                                               "^(V?)P(ADD|SUB)(B|D|Q|W)rr$",
                                               "^VMOV(A|U)P(D|S)Z(128|256)rrk(z?)((_REV)?)$",
                                               "^VMOVDQ(A|U)(32|64)Z128rrk(z?)((_REV)?)$",
                                               "^VMOVS(D|H|S)Zrr((_REV)?)$",
                                               "^VMOVS(D|S)Zrrk(z?)((_REV)?)$",
                                               "^VP(ADD|SUB)(B|D|Q|W)Yrr$",
                                               "^VP(ADD|SUB)(B|D|Q|W)Z(128|256)rr$",
                                               "^VP(ADD|SUB)(D|Q)Z(128|256)rrk(z?)$",
                                               "^VPMOVM2(D|Q)Z128rr$",
                                               "^VPTERNLOG(D|Q)Z(128|256)rri((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup160], (instrs VPBLENDDrri)>;

def SPRWriteResGroup161 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [4, 1, 1, 1];
  let Latency = 7;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup161], (instregex "^MOVS(L|Q|W)$")>;

def SPRWriteResGroup162 : SchedWriteRes<[SPRPort02_03_11]> {
  let Latency = 6;
}
def : InstRW<[SPRWriteResGroup162], (instregex "^MOVSX(16|32|64)rm(16|32)$",
                                               "^MOVSX(32|64)rm8$")>;
def : InstRW<[SPRWriteResGroup162], (instrs MOVSX32rm8_NOREX)>;

def SPRWriteResGroup163 : SchedWriteRes<[SPRPort01_05_10, SPRPort02_03_11]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup163], (instrs MOVSX16rm8)>;

def SPRWriteResGroup164 : SchedWriteRes<[SPRPort01_05_10]>;
def : InstRW<[SPRWriteResGroup164], (instregex "^MOVSX(16|32|64)rr(8|16|32)$")>;
def : InstRW<[SPRWriteResGroup164], (instrs MOVSX32rr8_NOREX)>;

def SPRWriteResGroup165 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup165], (instregex "^MUL_F(32|64)m$",
                                               "^VPABS(B|W)Zrmk(z?)$",
                                               "^VPS(L|R)LWZmik(z?)$",
                                               "^VPSRAWZmik(z?)$")>;
def : InstRW<[SPRWriteResGroup165, ReadAfterVecYLd], (instregex "^VP(ADD|SUB)(U?)S(B|W)Zrmk(z?)$",
                                                                "^VPAVG(B|W)Zrmk(z?)$",
                                                                "^VPM(AX|IN)(SB|UW)Zrmk(z?)$",
                                                                "^VPM(AX|IN)(SW|UB)Zrmk(z?)$",
                                                                "^VPSH(L|R)DVWZmk(z?)$",
                                                                "^VPS(L|R)L(V?)WZrmk(z?)$",
                                                                "^VPSRA(V?)WZrmk(z?)$")>;

def SPRWriteResGroup166 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup166], (instregex "^MUL_FI(16|32)m$")>;

def SPRWriteResGroup167 : SchedWriteRes<[SPRPort00]> {
  let Latency = 4;
}
def : InstRW<[SPRWriteResGroup167], (instregex "^MUL_F(P?)rST0$",
                                               "^V(U?)COMISHZrr(b?)$",
                                               "^V(U?)COMISHZrr_Int$",
                                               "^VCVT(T?)PD2(U?)QQZrr((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PD2(U?)QQZrrbkz$",
                                               "^VCVT(T?)PS2(U?)DQZrr((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PS2(U?)DQZrrbkz$",
                                               "^VM(AX|IN)(C?)PSZrr((k|kz)?)$",
                                               "^VM(AX|IN)PSZrrb((k|kz)?)$",
                                               "^VPLZCNT(D|Q)Zrr((k|kz)?)$",
                                               "^VPMADD52(H|L)UQZr((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup167], (instrs MUL_FST0r)>;

def SPRWriteResGroup168 : SchedWriteRes<[SPRPort00_01_05_06, SPRPort05, SPRPort06]> {
  let ReleaseAtCycles = [7, 1, 2];
  let Latency = 20;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup168], (instrs MWAITrr)>;

def SPRWriteResGroup169 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [6, 4, 1, 28, 15, 7, 1, 16, 1];
  let Latency = 35;
  let NumMicroOps = 79;
}
def : InstRW<[SPRWriteResGroup169], (instrs OUT16ir)>;

def SPRWriteResGroup170 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [6, 6, 27, 15, 7, 1, 16, 1];
  let Latency = 35;
  let NumMicroOps = 79;
}
def : InstRW<[SPRWriteResGroup170], (instrs OUT16rr)>;

def SPRWriteResGroup171 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [6, 4, 1, 30, 15, 9, 1, 18, 1];
  let Latency = 35;
  let NumMicroOps = 85;
}
def : InstRW<[SPRWriteResGroup171], (instrs OUT32ir)>;

def SPRWriteResGroup172 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [6, 6, 29, 15, 9, 1, 18, 1];
  let Latency = 35;
  let NumMicroOps = 85;
}
def : InstRW<[SPRWriteResGroup172], (instrs OUT32rr)>;

def SPRWriteResGroup173 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [5, 5, 1, 25, 15, 5, 1, 15, 1];
  let Latency = 35;
  let NumMicroOps = 73;
}
def : InstRW<[SPRWriteResGroup173], (instrs OUT8ir)>;

def SPRWriteResGroup174 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [5, 5, 26, 15, 5, 1, 15, 1];
  let Latency = 35;
  let NumMicroOps = 73;
}
def : InstRW<[SPRWriteResGroup174], (instrs OUT8rr)>;

def SPRWriteResGroup175 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [7, 6, 25, 16, 7, 1, 17, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 80;
}
def : InstRW<[SPRWriteResGroup175], (instrs OUTSB)>;

def SPRWriteResGroup176 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [7, 6, 28, 16, 10, 1, 20, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 89;
}
def : InstRW<[SPRWriteResGroup176], (instrs OUTSL)>;

def SPRWriteResGroup177 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [6, 1, 5, 27, 16, 8, 1, 18, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 83;
}
def : InstRW<[SPRWriteResGroup177], (instrs OUTSW)>;

def SPRWriteResGroup178 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup178], (instregex "^VBROADCASTI32X2Z128rmk(z?)$",
                                               "^VBROADCASTSSZ128rmk(z?)$",
                                               "^VMOV(A|U)P(D|S)Z128rmk(z?)$",
                                               "^VMOV(D|SH|SL)DUPZ128rmk(z?)$",
                                               "^VMOVDQ(A|U)(32|64)Z128rmk(z?)$",
                                               "^VMOVS(D|S)Zrmk(z?)$",
                                               "^VPBROADCAST(D|Q)Z128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup178, ReadAfterVecXLd], (instregex "^(V?)P(ADD|SUB)(B|D|Q|W)rm$",
                                                                "^VP(ADD|SUB)(B|D|Q|W)Z128rm$",
                                                                "^VP(ADD|SUB)(D|Q)Z128rm(b|k|kz)$",
                                                                "^VP(ADD|SUB)(D|Q)Z128rmbk(z?)$",
                                                                "^VPTERNLOG(D|Q)Z128rm(bi|ik)$",
                                                                "^VPTERNLOG(D|Q)Z128rmbik(z?)$",
                                                                "^VPTERNLOG(D|Q)Z128rmi((kz)?)$")>;
def : InstRW<[SPRWriteResGroup178, ReadAfterVecXLd], (instrs VPBLENDDrmi)>;

def SPRWriteResGroup179 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup179], (instregex "^VPBROADCAST(B|W)((Z128)?)rm$")>;
def : InstRW<[SPRWriteResGroup179, ReadAfterVecXLd], (instregex "^(V?)PALIGNRrmi$",
                                                                "^VALIGN(D|Q)Z128rm(bi|ik)$",
                                                                "^VALIGN(D|Q)Z128rmbik(z?)$",
                                                                "^VALIGN(D|Q)Z128rmi((kz)?)$")>;
def : InstRW<[SPRWriteResGroup179, ReadAfterVecXLd], (instrs VPALIGNRZ128rmi)>;

def SPRWriteResGroup180 : SchedWriteRes<[SPRPort00_06, SPRPort05]> {
  let Latency = 140;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup180], (instrs PAUSE)>;

def SPRWriteResGroup181 : SchedWriteRes<[SPRPort01, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup181, ReadAfterLd], (instregex "^P(DEP|EXT)(32|64)rm$")>;

def SPRWriteResGroup182 : SchedWriteRes<[SPRPort01_05, SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup182], (instregex "^(V?)PEXTR(D|Q)mr$",
                                               "^VPEXTR(D|Q)Zmr$",
                                               "^VPMOVQDZ128mr(k?)$")>;

def SPRWriteResGroup183 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 2, 1];
  let Latency = 9;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup183, ReadAfterVecXLd], (instregex "^(V?)PH(ADD|SUB)SWrm$")>;

def SPRWriteResGroup184 : SchedWriteRes<[SPRPort00_01, SPRPort01_05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 2;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup184], (instregex "^(V?)PH(ADD|SUB)SWrr$",
                                               "^VPH(ADD|SUB)SWYrr$")>;

def SPRWriteResGroup185 : SchedWriteRes<[SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup185], (instregex "^POP(16|32|64)rmm$",
                                               "^PUSH(16|32)rmm$")>;

def SPRWriteResGroup186 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [6, 2, 1, 1];
  let Latency = 5;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup186], (instrs POPF16)>;

def SPRWriteResGroup187 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 5;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup187], (instrs POPF64)>;

def SPRWriteResGroup188 : SchedWriteRes<[SPRPort02_03_11]> {
  let Latency = 0;
}
def : InstRW<[SPRWriteResGroup188], (instregex "^PREFETCHT(0|1|2)$")>;
def : InstRW<[SPRWriteResGroup188], (instrs PREFETCHNTA)>;

def SPRWriteResGroup189 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11, SPRPort06]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup189], (instregex "^PTWRITE((64)?)m$")>;

def SPRWriteResGroup190 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort06]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup190], (instrs PTWRITE64r)>;

def SPRWriteResGroup191 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort06]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup191], (instrs PTWRITEr)>;

def SPRWriteResGroup192 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup192], (instregex "^PUSH64r((mr)?)$")>;

def SPRWriteResGroup193 : SchedWriteRes<[SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup193], (instrs PUSH64rmm)>;

def SPRWriteResGroup194 : SchedWriteRes<[SPRPort00_06, SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let Latency = 4;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup194], (instrs PUSHF64)>;

def SPRWriteResGroup195 : SchedWriteRes<[SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup195], (instregex "^PUSH(F|G)S64$")>;

def SPRWriteResGroup196 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [2, 3, 2];
  let Latency = 8;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup196], (instregex "^RC(L|R)(16|32|64)rCL$")>;

def SPRWriteResGroup197 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup197, WriteRMW], (instregex "^RC(L|R)8m(1|i)$")>;

def SPRWriteResGroup198 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [1, 5, 2];
  let Latency = 20;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup198, WriteRMW], (instrs RCL8mCL)>;

def SPRWriteResGroup199 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [2, 5, 2];
  let Latency = 7;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup199], (instrs RCL8rCL)>;

def SPRWriteResGroup200 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [2, 4, 3];
  let Latency = 20;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup200, WriteRMW], (instrs RCR8mCL)>;

def SPRWriteResGroup201 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [3, 4, 3];
  let Latency = 9;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup201], (instrs RCR8rCL)>;

def SPRWriteResGroup202 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_05, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort01_05_10, SPRPort05]> {
  let ReleaseAtCycles = [1, 6, 1, 10, 20, 8, 5, 1, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 54;
}
def : InstRW<[SPRWriteResGroup202], (instrs RDMSR)>;

def SPRWriteResGroup203 : SchedWriteRes<[SPRPort01]> {
  let Latency = SapphireRapidsModel.MaxLatency;
}
def : InstRW<[SPRWriteResGroup203], (instrs RDPID64)>;

def SPRWriteResGroup204 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup204], (instrs RDPKRUr)>;

def SPRWriteResGroup205 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort05]> {
  let ReleaseAtCycles = [9, 6, 2, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 18;
}
def : InstRW<[SPRWriteResGroup205], (instrs RDPMC)>;

def SPRWriteResGroup206 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 3, 2, 5, 7, 3, 1, 2];
  let Latency = 1386;
  let NumMicroOps = 25;
}
def : InstRW<[SPRWriteResGroup206], (instrs RDRAND16r)>;

def SPRWriteResGroup207 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 3, 2, 5, 7, 3, 1, 2];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 25;
}
def : InstRW<[SPRWriteResGroup207], (instregex "^RDRAND(32|64)r$")>;

def SPRWriteResGroup208 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 3, 3, 5, 7, 1, 4];
  let Latency = 1381;
  let NumMicroOps = 25;
}
def : InstRW<[SPRWriteResGroup208], (instrs RDSEED16r)>;

def SPRWriteResGroup209 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 3, 3, 5, 7, 1, 4];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 25;
}
def : InstRW<[SPRWriteResGroup209], (instregex "^RDSEED(32|64)r$")>;

def SPRWriteResGroup210 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort05]> {
  let ReleaseAtCycles = [5, 6, 3, 1];
  let Latency = 18;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup210], (instrs RDTSC)>;

def SPRWriteResGroup211 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2, 1, 2, 7, 4, 3];
  let Latency = 42;
  let NumMicroOps = 21;
}
def : InstRW<[SPRWriteResGroup211], (instrs RDTSCP)>;

def SPRWriteResGroup212 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup212], (instrs RET64)>;

def SPRWriteResGroup213 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 6;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup213], (instregex "^RETI(16|32|64)$")>;

def SPRWriteResGroup214 : SchedWriteRes<[]>;
def : InstRW<[SPRWriteResGroup214], (instrs REX64_PREFIX)>;

def SPRWriteResGroup215 : SchedWriteRes<[SPRPort00_06]> {
  let ReleaseAtCycles = [2];
  let Latency = 12;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup215, WriteRMW], (instregex "^RO(L|R)(16|32|64)m(1|i|CL)$")>;

def SPRWriteResGroup216 : SchedWriteRes<[SPRPort00_06]> {
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup216], (instregex "^RO(L|R)(8|16|32|64)r(1|i)$")>;

def SPRWriteResGroup217 : SchedWriteRes<[SPRPort00_06]> {
  let ReleaseAtCycles = [2];
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup217, WriteRMW], (instregex "^RO(L|R)8m(1|i)$",
                                                         "^(RO|SH)L8mCL$",
                                                         "^(RO|SA|SH)R8mCL$")>;

def SPRWriteResGroup218 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 15;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup218], (instregex "^(V?)ROUNDP(D|S)mi$")>;
def : InstRW<[SPRWriteResGroup218, ReadAfterVecXLd], (instregex "^(V?)ROUNDS(D|S)mi((_Int)?)$",
                                                                "^VRNDSCALEP(D|S)Z128rm(bi|ik)$",
                                                                "^VRNDSCALEP(D|S)Z128rmbik(z?)$",
                                                                "^VRNDSCALEP(D|S)Z128rmi((kz)?)$",
                                                                "^VRNDSCALES(D|S)Zm$",
                                                                "^VRNDSCALES(D|S)Zm_Int((k|kz)?)$")>;

def SPRWriteResGroup219 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [2];
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup219], (instregex "^(V?)ROUND(PD|SS)ri$",
                                               "^(V?)ROUND(PS|SD)ri$",
                                               "^(V?)ROUNDS(D|S)ri_Int$",
                                               "^VRNDSCALEP(D|S)Z(128|256)rri((k|kz)?)$",
                                               "^VRNDSCALES(D|S)Zr$",
                                               "^VRNDSCALES(D|S)Zr(b?)_Int((k|kz)?)$",
                                               "^VROUNDP(D|S)Yri$")>;

def SPRWriteResGroup220 : SchedWriteRes<[SPRPort00_06]> {
  let ReleaseAtCycles = [2];
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup220], (instrs SAHF)>;

def SPRWriteResGroup221 : SchedWriteRes<[SPRPort00_06]> {
  let Latency = 13;
}
def : InstRW<[SPRWriteResGroup221, WriteRMW], (instregex "^S(A|H)R8m(1|i)$",
                                                         "^SHL8m(1|i)$")>;

def SPRWriteResGroup222 : SchedWriteRes<[SPRPort00_06, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup222, ReadAfterLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^S(A|H)RX(32|64)rm$",
                                                                                                                             "^SHLX(32|64)rm$")>;

def SPRWriteResGroup223 : SchedWriteRes<[SPRPort00_06]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup223], (instregex "^S(A|H)RX(32|64)rr$",
                                               "^SHLX(32|64)rr$")>;

def SPRWriteResGroup224 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 2, 1, 1, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup224], (instrs SERIALIZE)>;

def SPRWriteResGroup225 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup225], (instrs SFENCE)>;

def SPRWriteResGroup226 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 2, 2];
  let Latency = 21;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup226], (instregex "^S(G|I)DT64m$")>;

def SPRWriteResGroup227 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup227, ReadAfterVecXLd], (instrs SHA1MSG1rm)>;

def SPRWriteResGroup228 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup228], (instrs SHA1MSG1rr)>;

def SPRWriteResGroup229 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 2, 1, 2, 1];
  let Latency = 13;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup229, ReadAfterVecXLd], (instrs SHA1MSG2rm)>;

def SPRWriteResGroup230 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort01_05]> {
  let ReleaseAtCycles = [2, 2, 1, 2];
  let Latency = 6;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup230], (instrs SHA1MSG2rr)>;

def SPRWriteResGroup231 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup231, ReadAfterVecXLd], (instrs SHA1NEXTErm)>;

def SPRWriteResGroup232 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05]> {
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup232], (instrs SHA1NEXTErr)>;

def SPRWriteResGroup233 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup233], (instregex "^VPMOV(S|Z)XBWZ((256)?)rmk(z?)$",
                                               "^VPOPCNT(B|W)Z(128|256)rmk(z?)$",
                                               "^VPOPCNT(B|W)Zrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup233, ReadAfterVecXLd], (instregex "^VDBPSADBWZ128rmik(z?)$",
                                                                "^VPACK(S|U)SDWZ128rm(bk|kz)$",
                                                                "^VPACK(S|U)SDWZ128rmbkz$",
                                                                "^VPACK(S|U)S(DW|WB)Z128rmk$",
                                                                "^VPACK(S|U)SWBZ128rmkz$",
                                                                "^VPMULTISHIFTQBZ128rm(bk|kz)$",
                                                                "^VPMULTISHIFTQBZ128rm(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup233, ReadAfterVecXLd], (instrs SHA1RNDS4rmi,
                                                             SHA256RNDS2rm)>;
def : InstRW<[SPRWriteResGroup233, ReadAfterVecYLd], (instregex "^VDBPSADBWZ((256)?)rmik(z?)$",
                                                                "^VPACK(S|U)SDWZ((256)?)rm(bk|kz)$",
                                                                "^VPACK(S|U)SDWZ((256)?)rmbkz$",
                                                                "^VPACK(S|U)S(DW|WB)Z((256)?)rmk$",
                                                                "^VPACK(S|U)SWBZ((256)?)rmkz$",
                                                                "^VPERMBZ(128|256)rmk(z?)$",
                                                                "^VPERMBZrmk(z?)$",
                                                                "^VPMULTISHIFTQBZ((256)?)rm(bk|kz)$",
                                                                "^VPMULTISHIFTQBZ((256)?)rm(k|bkz)$")>;

def SPRWriteResGroup234 : SchedWriteRes<[SPRPort05]> {
  let Latency = 6;
}
def : InstRW<[SPRWriteResGroup234], (instrs SHA1RNDS4rri,
                                            SHA256RNDS2rr)>;

def SPRWriteResGroup235 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [3, 2, 1, 1, 1];
  let Latency = 12;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup235, ReadAfterVecXLd], (instrs SHA256MSG1rm)>;

def SPRWriteResGroup236 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [3, 2, 1, 1];
  let Latency = 5;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup236], (instrs SHA256MSG1rr)>;

def SPRWriteResGroup237 : SchedWriteRes<[SPRPort05]> {
  let ReleaseAtCycles = [2];
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup237], (instregex "^VPMOV(D|Q|W|SQ|SW)BZrrk(z?)$",
                                               "^VPMOV((S|US)?)(D|Q)WZrrk(z?)$",
                                               "^VPMOV(U?)SDBZrrk(z?)$",
                                               "^VPMOVUS(Q|W)BZrrk(z?)$")>;
def : InstRW<[SPRWriteResGroup237], (instrs SHA256MSG2rr)>;

def SPRWriteResGroup238 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort07_08]> {
  let Latency = 13;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup238], (instrs SHRD16mri8)>;

def SPRWriteResGroup239 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup239], (instregex "^SLDT(32|64)r$")>;

def SPRWriteResGroup240 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort05]> {
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup240], (instrs SMSW16r)>;

def SPRWriteResGroup241 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort05]> {
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup241], (instregex "^SMSW(32|64)r$")>;

def SPRWriteResGroup242 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 24;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup242, ReadAfterVecLd], (instregex "^(V?)SQRTSDm_Int$")>;
def : InstRW<[SPRWriteResGroup242, ReadAfterVecLd], (instrs VSQRTSDZm_Int)>;

def SPRWriteResGroup243 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup243], (instrs STD)>;

def SPRWriteResGroup244 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [1, 4, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup244], (instrs STI)>;

def SPRWriteResGroup245 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup245], (instrs STOSB)>;

def SPRWriteResGroup246 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 7;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup246], (instregex "^STOS(L|Q|W)$")>;

def SPRWriteResGroup247 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort01]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup247], (instregex "^STR(32|64)r$")>;

def SPRWriteResGroup248 : SchedWriteRes<[SPRPort00]> {
  let Latency = 2;
}
def : InstRW<[SPRWriteResGroup248], (instregex "^(TST|XAM)_F$")>;
def : InstRW<[SPRWriteResGroup248], (instrs UCOM_FPPr)>;

def SPRWriteResGroup249 : SchedWriteRes<[SPRPort01_05]> {
  let Latency = 4;
}
def : InstRW<[SPRWriteResGroup249], (instregex "^V(ADD|SUB)P(D|S)Z(128|256)rrkz$",
                                               "^V(ADD|SUB)S(D|S)Zrr(b?)_Intkz$")>;

def SPRWriteResGroup250 : SchedWriteRes<[SPRPort00_05]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup250], (instregex "^V(ADD|SUB)P(D|S)Zrr(b?)$",
                                               "^VMOVDQU(8|16)Zrrk(z?)((_REV)?)$",
                                               "^VP(ADD|SUB)(B|W)Zrrk(z?)$",
                                               "^VPBLENDM(B|W)Zrrk(z?)$",
                                               "^VPMOVM2(B|W)Zrr$")>;

def SPRWriteResGroup251 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 6;
}
def : InstRW<[SPRWriteResGroup251], (instregex "^V(ADD|SUB)PHZ(128|256)rrk(z?)$",
                                               "^V(ADD|SUB)SHZrr(b?)_Intk(z?)$",
                                               "^VCVT(T?)PH2(U?)WZ(128|256)rrk(z?)$",
                                               "^VCVT(U?)W2PHZ(128|256)rrk(z?)$",
                                               "^VF(N?)M(ADD|SUB)(132|213|231)PHZ(128|256)rk(z?)$",
                                               "^VF(N?)M(ADD|SUB)(132|213|231)SHZr(b?)_Intk(z?)$",
                                               "^VFMADDSUB(132|213|231)PHZ(128|256)rk(z?)$",
                                               "^VFMSUBADD(132|213|231)PHZ(128|256)rk(z?)$",
                                               "^VGETEXPPHZ(128|256)rk(z?)$",
                                               "^VGETEXPSHZr(bk|kz)$",
                                               "^VGETEXPSHZr(k|bkz)$",
                                               "^VGETMANTPHZ(128|256)rrik(z?)$",
                                               "^VGETMANTSHZrri(bk|kz)$",
                                               "^VGETMANTSHZrri(k|bkz)$",
                                               "^VM(AX|IN)CPHZ(128|256)rrk(z?)$",
                                               "^VM(AX|IN|UL)PHZ(128|256)rrk(z?)$",
                                               "^VM(AX|IN|UL)SHZrr(b?)_Intk(z?)$")>;

def SPRWriteResGroup252 : SchedWriteRes<[SPRPort00]> {
  let Latency = 5;
}
def : InstRW<[SPRWriteResGroup252], (instregex "^V(ADD|SUB)PHZrr(b?)$",
                                               "^VAES(DE|EN)C((LAST)?)Zrr$",
                                               "^VCVT(T?)PH2(U?)WZrr(b?)$",
                                               "^VCVT(U?)W2PHZrr(b?)$",
                                               "^VF(N?)M(ADD|SUB)(132|213|231)PHZr(b?)$",
                                               "^VFMADDSUB(132|213|231)PHZr(b?)$",
                                               "^VFMSUBADD(132|213|231)PHZr(b?)$",
                                               "^VGETEXPPHZr(b?)$",
                                               "^VGETMANTPHZrri(b?)$",
                                               "^VM(AX|IN)CPHZrr$",
                                               "^VM(AX|IN|UL)PHZrr(b?)$",
                                               "^VMOVMSKP(D|S)Yrr$")>;
def : InstRW<[SPRWriteResGroup252], (instrs VGF2P8MULBZrr)>;

def SPRWriteResGroup253 : SchedWriteRes<[SPRPort00]> {
  let Latency = 6;
}
def : InstRW<[SPRWriteResGroup253], (instregex "^V(ADD|SUB)PHZrr(bk|kz)$",
                                               "^V(ADD|SUB)PHZrr(k|bkz)$",
                                               "^VCVT(T?)PH2(U?)WZrr(bk|kz)$",
                                               "^VCVT(T?)PH2(U?)WZrr(k|bkz)$",
                                               "^VCVT(U?)W2PHZrr(bk|kz)$",
                                               "^VCVT(U?)W2PHZrr(k|bkz)$",
                                               "^VF(N?)M(ADD|SUB)(132|213|231)PHZr(bk|kz)$",
                                               "^VF(N?)M(ADD|SUB)(132|213|231)PHZr(k|bkz)$",
                                               "^VFMADDSUB(132|213|231)PHZr(bk|kz)$",
                                               "^VFMADDSUB(132|213|231)PHZr(k|bkz)$",
                                               "^VFMSUBADD(132|213|231)PHZr(bk|kz)$",
                                               "^VFMSUBADD(132|213|231)PHZr(k|bkz)$",
                                               "^VGETEXPPHZr(bk|kz)$",
                                               "^VGETEXPPHZr(k|bkz)$",
                                               "^VGETMANTPHZrri(bk|kz)$",
                                               "^VGETMANTPHZrri(k|bkz)$",
                                               "^VM(AX|IN)CPHZrrk(z?)$",
                                               "^VM(AX|IN|UL)PHZrr(bk|kz)$",
                                               "^VM(AX|IN|UL)PHZrr(k|bkz)$")>;

def SPRWriteResGroup254 : SchedWriteRes<[SPRPort01_05, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup254], (instregex "^VPMOV(S|Z)XBWZ128rmk(z?)$",
                                               "^VPSHUF(H|L)WZ(128|256)mik(z?)$")>;
def : InstRW<[SPRWriteResGroup254, ReadAfterVecYLd], (instregex "^V(ADD|SUB)PSYrm$",
                                                                "^V(ADD|SUB)PSZ256rm((b|k|bk|kz)?)$",
                                                                "^V(ADD|SUB)PSZ256rmbkz$",
                                                                "^VPSHUFBZ256rmk(z?)$",
                                                                "^VPUNPCK(H|L)(BW|WD)Z256rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup254, ReadAfterVecYLd], (instrs VADDSUBPSYrm)>;
def : InstRW<[SPRWriteResGroup254, ReadAfterVecXLd], (instregex "^VPSHUFBZ128rmk(z?)$",
                                                                "^VPUNPCK(H|L)(BW|WD)Z128rmk(z?)$")>;

def SPRWriteResGroup255 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup255], (instregex "^VMOVDQU(8|16)Zrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup255, ReadAfterVecYLd], (instregex "^V(ADD|SUB)PSZrm((b|k|bk|kz)?)$",
                                                                "^V(ADD|SUB)PSZrmbkz$",
                                                                "^VP(ADD|SUB)(B|W)Zrmk(z?)$",
                                                                "^VPBLENDM(B|W)Zrmk(z?)$")>;

def SPRWriteResGroup256 : SchedWriteRes<[SPRPort00_05]> {
  let Latency = 4;
}
def : InstRW<[SPRWriteResGroup256], (instregex "^V(ADD|SUB)PSZrr(bk|kz)$",
                                               "^V(ADD|SUB)PSZrr(k|bkz)$")>;

def SPRWriteResGroup257 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 12;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup257], (instregex "^VCVT(T?)PS2(U?)DQZrm((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PS2(U?)DQZrmbkz$",
                                               "^VPLZCNT(D|Q)Zrm((b|k|bk|kz)?)$",
                                               "^VPLZCNT(D|Q)Zrmbkz$")>;
def : InstRW<[SPRWriteResGroup257, ReadAfterVecXLd], (instregex "^VAES(DE|EN)C((LAST)?)Zrm$")>;
def : InstRW<[SPRWriteResGroup257, ReadAfterVecYLd], (instregex "^VGF2P8AFFINE((INV)?)QBZrm(b?)i$")>;
def : InstRW<[SPRWriteResGroup257, ReadAfterVecYLd], (instrs VGF2P8MULBZrm)>;
def : InstRW<[SPRWriteResGroup257, ReadAfterVecYLd, ReadAfterVecYLd], (instregex "^VPMADD52(H|L)UQZm((b|k|bk|kz)?)$",
                                                                                 "^VPMADD52(H|L)UQZmbkz$")>;

def SPRWriteResGroup258 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup258], (instregex "^VPBROADCAST(B|W)Z128rmk(z?)$",
                                               "^VPOPCNT(B|D|Q|W)Z((256)?)rm$",
                                               "^VPOPCNT(D|Q)Z((256)?)rm(b|k|kz)$",
                                               "^VPOPCNT(D|Q)Z((256)?)rmbk(z?)$",
                                               "^VPSHUF(H|L)WZmik(z?)$")>;
def : InstRW<[SPRWriteResGroup258, ReadAfterVecYLd], (instregex "^VALIGN(D|Q)Z((256)?)rm(bi|ik)$",
                                                                "^VALIGN(D|Q)Z((256)?)rmbik(z?)$",
                                                                "^VALIGN(D|Q)Z((256)?)rmi((kz)?)$",
                                                                "^VFPCLASSP(D|H|S)Z((256)?)rmb$",
                                                                "^VPACK(S|U)S(DW|WB)(Y|Z)rm$",
                                                                "^VPACK(S|U)S(DW|WB)Z256rm$",
                                                                "^VPACK(S|U)SDWZ((256)?)rmb$",
                                                                "^VPALIGNRZ((256)?)rmik(z?)$",
                                                                "^VPM(AX|IN)(S|U)QZ((256)?)rm((b|k|bk|kz)?)$",
                                                                "^VPM(AX|IN)(S|U)QZ((256)?)rmbkz$",
                                                                "^VPMULTISHIFTQBZ((256)?)rm(b?)$",
                                                                "^VPUNPCK(H|L)(BW|WD)Zrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup258, ReadAfterVecYLd], (instrs VPCMPGTQYrm)>;
def : InstRW<[SPRWriteResGroup258, ReadAfterVecXLd], (instregex "^VPALIGNRZ128rmik(z?)$",
                                                                "^VPCLMULQDQ(Y|Z)rmi$")>;
def : InstRW<[SPRWriteResGroup258, ReadAfterVecXLd], (instrs VPCLMULQDQZ256rmi)>;

def SPRWriteResGroup259 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup259, ReadAfterVecYLd, ReadAfterVecYLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^VBLENDVP(D|S)Yrmr$")>;
def : InstRW<[SPRWriteResGroup259, ReadAfterVecYLd, ReadAfterVecYLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instrs VPBLENDVBYrmr)>;

def SPRWriteResGroup260 : SchedWriteRes<[SPRPort00_01_05]> {
  let ReleaseAtCycles = [3];
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup260], (instregex "^VBLENDVP(S|DY)rrr$",
                                               "^VBLENDVP(D|SY)rrr$",
                                               "^VPBLENDVB(Y?)rrr$")>;

def SPRWriteResGroup261 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 9;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup261, ReadAfterVecXLd, ReadAfterVecXLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instregex "^VBLENDVP(D|S)rmr$")>;
def : InstRW<[SPRWriteResGroup261, ReadAfterVecXLd, ReadAfterVecXLd, ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault], (instrs VPBLENDVBrmr)>;

def SPRWriteResGroup262 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup262], (instregex "^VBROADCAST(F|I)32X(2|4)Z256rmk(z?)$",
                                               "^VBROADCAST(F|I)64X2Z128rmk(z?)$",
                                               "^VBROADCASTS(D|S)Z256rmk(z?)$",
                                               "^VMOV(A|U)P(D|S)Z256rmk(z?)$",
                                               "^VMOV(D|SH|SL)DUPZ256rmk(z?)$",
                                               "^VMOVDQ(A|U)(32|64)Z256rmk(z?)$",
                                               "^VPBROADCAST(D|Q)Z256rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup262, ReadAfterVecYLd], (instregex "^VINSERT(F|I)128rm$",
                                                                "^VINSERT(F|I)(32x4|64x2)Z256rm((k|kz)?)$",
                                                                "^VP(ADD|SUB)(B|D|Q|W)(Y|Z256)rm$",
                                                                "^VP(ADD|SUB)(D|Q)Z256rm(b|k|kz)$",
                                                                "^VP(ADD|SUB)(D|Q)Z256rmbk(z?)$",
                                                                "^VPTERNLOG(D|Q)Z256rm(bi|ik)$",
                                                                "^VPTERNLOG(D|Q)Z256rmbik(z?)$",
                                                                "^VPTERNLOG(D|Q)Z256rmi((kz)?)$")>;

def SPRWriteResGroup263 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup263, ReadAfterVecXLd], (instregex "^VCMPP(D|H|S)Z128rm(bi|ik)$",
                                                                "^VCMPP(D|H|S)Z128rm(i|bik)$",
                                                                "^VFPCLASSP(D|H|S)Z128rm(b?)k$",
                                                                "^VPCMP(B|D|Q|W|UD|UQ|UW)Z128rmi(k?)$",
                                                                "^VPCMP(D|Q|UQ)Z128rmib(k?)$",
                                                                "^VPCMP(EQ|GT)(B|D|Q|W)Z128rm(k?)$",
                                                                "^VPCMP(EQ|GT)(D|Q)Z128rmb(k?)$",
                                                                "^VPCMPUBZ128rmi(k?)$",
                                                                "^VPCMPUDZ128rmib(k?)$",
                                                                "^VPTEST(N?)M(B|D|Q|W)Z128rm(k?)$",
                                                                "^VPTEST(N?)M(D|Q)Z128rmb(k?)$")>;
def : InstRW<[SPRWriteResGroup263, ReadAfterVecYLd], (instregex "^VCMPP(D|H|S)Z((256)?)rm(bi|ik)$",
                                                                "^VCMPP(D|H|S)Z((256)?)rm(i|bik)$",
                                                                "^VFPCLASSP(D|H|S)Z((256)?)rm(b?)k$",
                                                                "^VPCMP(B|D|Q|W|UD|UQ|UW)Z((256)?)rmi(k?)$",
                                                                "^VPCMP(D|Q|UQ)Z((256)?)rmib(k?)$",
                                                                "^VPCMP(EQ|GT)(B|D|Q|W)Z((256)?)rm(k?)$",
                                                                "^VPCMP(EQ|GT)(D|Q)Z((256)?)rmb(k?)$",
                                                                "^VPCMPUBZ((256)?)rmi(k?)$",
                                                                "^VPCMPUDZ((256)?)rmib(k?)$",
                                                                "^VPTEST(N?)M(B|D|Q|W)Z((256)?)rm(k?)$",
                                                                "^VPTEST(N?)M(D|Q)Z((256)?)rmb(k?)$")>;
def : InstRW<[SPRWriteResGroup263, ReadAfterVecLd], (instregex "^VCMPS(D|H|S)Zrmi$",
                                                               "^VCMPS(D|H|S)Zrmi_Int(k?)$",
                                                               "^VFPCLASSS(D|H|S)Zrmk$")>;

def SPRWriteResGroup264 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup264, ReadAfterVecLd], (instregex "^V(U?)COMISHZrm((_Int)?)$")>;

def SPRWriteResGroup265 : SchedWriteRes<[SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 1];
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup265], (instregex "^VCOMPRESSP(D|S)Z(128|256)mr$",
                                               "^VCOMPRESSP(D|S)Zmr$",
                                               "^VPCOMPRESS(D|Q)Z(128|256)mr$",
                                               "^VPCOMPRESS(D|Q)Zmr$",
                                               "^VPMOV(D|Q|W|SQ|SW)BZmr$",
                                               "^VPMOV((S|US)?)(D|Q)WZmr$",
                                               "^VPMOV(U?)S(DB|QD)Zmr$",
                                               "^VPMOVUS(Q|W)BZmr$")>;

def SPRWriteResGroup266 : SchedWriteRes<[SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 1];
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup266], (instregex "^VCOMPRESSP(D|S)Z(128|256)mrk$",
                                               "^VCOMPRESSP(D|S)Zmrk$",
                                               "^VPCOMPRESS(D|Q)Z(128|256)mrk$",
                                               "^VPCOMPRESS(D|Q)Zmrk$",
                                               "^VPMOV(D|Q|W|SQ|SW)BZmrk$",
                                               "^VPMOV((S|US)?)(D|Q)WZmrk$",
                                               "^VPMOV(U?)S(DB|QD)Zmrk$",
                                               "^VPMOVUS(Q|W)BZmrk$")>;

def SPRWriteResGroup267 : SchedWriteRes<[SPRPort05]> {
  let ReleaseAtCycles = [2];
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup267], (instregex "^VCOMPRESSP(D|S)Z(128|256)rr$",
                                               "^VCOMPRESSP(D|S)Zrr$",
                                               "^VEXPANDP(D|S)Z(128|256)rr$",
                                               "^VEXPANDP(D|S)Zrr$",
                                               "^VPCOMPRESS(B|D|Q|W)Z(128|256)rr$",
                                               "^VPCOMPRESS(B|D|Q|W)Zrr$",
                                               "^VPEXPAND(B|D|Q|W)Z(128|256)rr$",
                                               "^VPEXPAND(B|D|Q|W)Zrr$")>;

def SPRWriteResGroup268 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup268], (instregex "^VCVT(U?)DQ2PDZrr((k|kz)?)$",
                                               "^VCVT(T?)PS2(U?)QQZrr((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PS2(U?)QQZrrbkz$",
                                               "^VCVT(U?)QQ2PSZrr((b|k|bk|kz)?)$",
                                               "^VCVT(U?)QQ2PSZrrbkz$")>;

def SPRWriteResGroup269 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup269], (instregex "^VCVT(U?)DQ2PHZ128rm(b?)$",
                                               "^VCVTNEPS2BF16Z128rm(b?)$")>;

def SPRWriteResGroup270 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 19;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup270], (instregex "^VCVT(U?)DQ2PHZ128rm(bk|kz)$",
                                               "^VCVT(U?)DQ2PHZ128rm(k|bkz)$")>;

def SPRWriteResGroup271 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup271], (instregex "^VCVT(U?)DQ2PHZ128rr$")>;

def SPRWriteResGroup272 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup272], (instregex "^VCVT(U?)DQ2PHZ128rrk(z?)$")>;

def SPRWriteResGroup273 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 17;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup273], (instregex "^VCVT(U?)DQ2PHZ256rm(b?)$",
                                               "^VCVTNEPS2BF16Z128rm(bk|kz)$",
                                               "^VCVTNEPS2BF16Z128rm(k|bkz)$")>;

def SPRWriteResGroup274 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 21;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup274], (instregex "^VCVT(U?)DQ2PHZ256rm(bk|kz)$",
                                               "^VCVT(U?)DQ2PHZ256rm(k|bkz)$")>;

def SPRWriteResGroup275 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup275], (instregex "^VCVT(U?)DQ2PHZ256rr$")>;

def SPRWriteResGroup276 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup276], (instregex "^VCVT(U?)DQ2PHZ256rrk(z?)$")>;

def SPRWriteResGroup277 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 17;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup277], (instregex "^VCVT(U?)DQ2PHZrm(b?)$")>;

def SPRWriteResGroup278 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 21;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup278], (instregex "^VCVT(U?)DQ2PHZrm(bk|kz)$",
                                               "^VCVT(U?)DQ2PHZrm(k|bkz)$")>;

def SPRWriteResGroup279 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup279], (instregex "^VCVT(U?)DQ2PHZrr(b?)$")>;

def SPRWriteResGroup280 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup280], (instregex "^VCVT(U?)DQ2PHZrr(bk|kz)$",
                                               "^VCVT(U?)DQ2PHZrr(k|bkz)$")>;

def SPRWriteResGroup281 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1];
  let Latency = 15;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup281, ReadAfterVecXLd], (instregex "^VCVTNE2PS2BF16Z128rm(b?)$")>;

def SPRWriteResGroup282 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1];
  let Latency = 17;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup282, ReadAfterVecXLd], (instregex "^VCVTNE2PS2BF16Z128rm(bk|kz)$",
                                                                "^VCVTNE2PS2BF16Z128rm(k|bkz)$")>;

def SPRWriteResGroup283 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup283], (instregex "^VCVTNE2PS2BF16Z(128|256)rr$")>;

def SPRWriteResGroup284 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup284], (instregex "^VCVTNE2PS2BF16Z(128|256)rrk(z?)$")>;

def SPRWriteResGroup285 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1];
  let Latency = 16;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup285, ReadAfterVecYLd], (instregex "^VCVTNE2PS2BF16Z256rm(b?)$")>;

def SPRWriteResGroup286 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1];
  let Latency = 18;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup286, ReadAfterVecYLd], (instregex "^VCVTNE2PS2BF16Z256rm(bk|kz)$",
                                                                "^VCVTNE2PS2BF16Z256rm(k|bkz)$")>;

def SPRWriteResGroup287 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 16;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup287, ReadAfterVecYLd], (instregex "^VCVTNE2PS2BF16Zrm(b?)$",
                                                                "^VDPBF16PSZm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup287, ReadAfterVecYLd], (instrs VDPBF16PSZmbkz)>;

def SPRWriteResGroup288 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 18;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup288, ReadAfterVecYLd], (instregex "^VCVTNE2PS2BF16Zrm(bk|kz)$",
                                                                "^VCVTNE2PS2BF16Zrm(k|bkz)$")>;

def SPRWriteResGroup289 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup289], (instregex "^VDPBF16PSZr((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup289], (instrs VCVTNE2PS2BF16Zrr)>;

def SPRWriteResGroup290 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup290], (instregex "^VCVTNE2PS2BF16Zrrk(z?)$")>;

def SPRWriteResGroup291 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup291], (instregex "^VCVTNEPS2BF16Z(128|256)rr$")>;

def SPRWriteResGroup292 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort05]> {
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup292], (instregex "^VCVTNEPS2BF16Z(128|256)rrk(z?)$")>;

def SPRWriteResGroup293 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup293], (instregex "^VCVTNEPS2BF16Z256rm(b?)$")>;

def SPRWriteResGroup294 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 18;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup294], (instregex "^VCVTNEPS2BF16Z256rm(bk|kz)$",
                                               "^VCVTNEPS2BF16Z256rm(k|bkz)$")>;

def SPRWriteResGroup295 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup295], (instregex "^VCVTNEPS2BF16Zrm(b?)$")>;

def SPRWriteResGroup296 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 18;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup296], (instregex "^VCVTNEPS2BF16Zrm(bk|kz)$",
                                               "^VCVTNEPS2BF16Zrm(k|bkz)$")>;

def SPRWriteResGroup297 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup297], (instrs VCVTNEPS2BF16Zrr)>;

def SPRWriteResGroup298 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup298], (instregex "^VCVTNEPS2BF16Zrrk(z?)$")>;

def SPRWriteResGroup299 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 15;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup299], (instregex "^VCVT(T?)PD2DQYrm$",
                                               "^VCVT(T?)P(D|H)2(U?)DQZ256rm(b?)$",
                                               "^VCVT(T?)PD2(U?)DQZ256rm(bk|kz)$",
                                               "^VCVT(T?)PD2(U?)DQZ256rm(k|bkz)$",
                                               "^VCVTPH2PSXZ128rm(bk|kz)$",
                                               "^VCVTPH2PSXZ128rm(k|bkz)$",
                                               "^VCVTPH2PSXZ256rm(b?)$",
                                               "^VCVT(U?)QQ2PSZ256rm((b|k|bk|kz)?)$",
                                               "^VCVT(U?)QQ2PSZ256rmbkz$")>;

def SPRWriteResGroup300 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 15;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup300], (instregex "^VCVT(T?)P(D|H)2(U?)DQZrm(b?)$",
                                               "^VCVT(T?)PD2(U?)DQZrm(bk|kz)$",
                                               "^VCVT(T?)PD2(U?)DQZrm(k|bkz)$",
                                               "^VCVTPH2PSXZrm(b?)$",
                                               "^VCVT(U?)QQ2PSZrm((b|k|bk|kz)?)$",
                                               "^VCVT(U?)QQ2PSZrmbkz$")>;

def SPRWriteResGroup301 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 2];
  let Latency = 19;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup301], (instregex "^VCVTPD2PHZ128rm(b?)$")>;

def SPRWriteResGroup302 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 2];
  let Latency = 22;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup302], (instregex "^VCVTPD2PHZ128rm(bk|kz)$",
                                               "^VCVTPD2PHZ128rm(k|bkz)$")>;

def SPRWriteResGroup303 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 12;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup303], (instrs VCVTPD2PHZ128rr)>;

def SPRWriteResGroup304 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 15;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup304], (instregex "^VCVTPD2PHZ128rrk(z?)$")>;

def SPRWriteResGroup305 : SchedWriteRes<[SPRPort00_01, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 21;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup305], (instregex "^VCVTPD2PHZ256rm(b?)$")>;

def SPRWriteResGroup306 : SchedWriteRes<[SPRPort00_01, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 24;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup306], (instregex "^VCVTPD2PHZ256rm(bk|kz)$",
                                               "^VCVTPD2PHZ256rm(k|bkz)$")>;

def SPRWriteResGroup307 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 13;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup307], (instrs VCVTPD2PHZ256rr)>;

def SPRWriteResGroup308 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup308], (instregex "^VCVTPD2PHZ256rrk(z?)$")>;

def SPRWriteResGroup309 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 23;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup309], (instregex "^VCVTP(D2PH|H2PD)Zrm(b?)$")>;

def SPRWriteResGroup310 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 26;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup310], (instregex "^VCVTP(D2PH|H2PD)Zrm(bk|kz)$",
                                               "^VCVTP(D2PH|H2PD)Zrm(k|bkz)$")>;

def SPRWriteResGroup311 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup311], (instregex "^VCVTP(D2PH|H2PD)Zrr(b?)$")>;

def SPRWriteResGroup312 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 18;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup312], (instregex "^VCVTP(D2PH|H2PD)Zrr(bk|kz)$",
                                               "^VCVTP(D2PH|H2PD)Zrr(k|bkz)$")>;

def SPRWriteResGroup313 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup313], (instregex "^VCVT(T?)PD2(U?)QQZ128rm((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PD2(U?)QQZ128rmbkz$",
                                               "^VPABS(B|W)Z(128|256)rmk(z?)$",
                                               "^VPLZCNT(D|Q)Z128rm((b|k|bk|kz)?)$",
                                               "^VPLZCNT(D|Q)Z128rmbkz$",
                                               "^VPS(L|R)LWZ(128|256)mik(z?)$",
                                               "^VPSRAWZ(128|256)mik(z?)$")>;
def : InstRW<[SPRWriteResGroup313, ReadAfterVecLd], (instregex "^VFIXUPIMMS(D|S)Zrmi((k|kz)?)$",
                                                               "^VSCALEFS(D|S)Zrm((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup313, ReadAfterVecXLd], (instregex "^VP(ADD|SUB)(U?)S(B|W)Z128rmk(z?)$",
                                                                "^VPAVG(B|W)Z128rmk(z?)$",
                                                                "^VPM(AX|IN)(SB|UW)Z128rmk(z?)$",
                                                                "^VPM(AX|IN)(SW|UB)Z128rmk(z?)$",
                                                                "^VPSH(L|R)DVWZ128mk(z?)$",
                                                                "^VPS(L|R)L(V?)WZ128rmk(z?)$",
                                                                "^VPSRA(V?)WZ128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup313, ReadAfterVecYLd], (instregex "^VP(ADD|SUB)(U?)S(B|W)Z256rmk(z?)$",
                                                                "^VPAVG(B|W)Z256rmk(z?)$",
                                                                "^VPM(AX|IN)(SB|UW)Z256rmk(z?)$",
                                                                "^VPM(AX|IN)(SW|UB)Z256rmk(z?)$",
                                                                "^VPSH(L|R)DVWZ256mk(z?)$",
                                                                "^VPS(L|R)L(V?)WZ256rmk(z?)$",
                                                                "^VPSRA(V?)WZ256rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup313, ReadAfterVecXLd, ReadAfterVecXLd], (instregex "^VPMADD52(H|L)UQZ128m((b|k|bk|kz)?)$",
                                                                                 "^VPMADD52(H|L)UQZ128mbkz$")>;

def SPRWriteResGroup314 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 4;
}
def : InstRW<[SPRWriteResGroup314], (instregex "^VCVT(T?)PD2(U?)QQZ(128|256)rr((k|kz)?)$",
                                               "^VCVT(U?)QQ2PDZ(128|256)rr((k|kz)?)$",
                                               "^VFIXUPIMMS(D|S)Zrri((k|kz)?)$",
                                               "^VPLZCNT(D|Q)Z(128|256)rr((k|kz)?)$",
                                               "^VPMADD52(H|L)UQZ(128|256)r((k|kz)?)$",
                                               "^VSCALEFS(D|S)Zrr((k|kz)?)$",
                                               "^VSCALEFS(D|S)Zrrb_Int((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup314, ReadAfterVecLd], (instregex "^VFIXUPIMMS(D|S)Zrrib((k|kz)?)$")>;

def SPRWriteResGroup315 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup315], (instregex "^VCVT(T?)PH2(U?)DQZ128rm(b?)$",
                                               "^VCVTPS2PHXZ128rm(b?)$")>;

def SPRWriteResGroup316 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 17;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup316], (instregex "^VCVT(T?)PH2(U?)DQZ128rm(bk|kz)$",
                                               "^VCVT(T?)PH2(U?)DQZ128rm(k|bkz)$")>;

def SPRWriteResGroup317 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup317], (instregex "^VCVT(T?)PH2(U?)DQZ(128|256)rrk(z?)$",
                                               "^VCVTP(H2PS|S2PH)(X?)Z256rrk(z?)$")>;

def SPRWriteResGroup318 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 18;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup318], (instregex "^VCVT(T?)PH2(U?)DQZ256rm(bk|kz)$",
                                               "^VCVT(T?)PH2(U?)DQZ256rm(k|bkz)$",
                                               "^VCVTP(H2PS|S2PH)XZ256rm(bk|kz)$",
                                               "^VCVTP(H2PS|S2PH)XZ256rm(k|bkz)$")>;

def SPRWriteResGroup319 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 18;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup319], (instregex "^VCVT(T?)PH2(U?)DQZrm(bk|kz)$",
                                               "^VCVT(T?)PH2(U?)DQZrm(k|bkz)$",
                                               "^VCVTP(H2PS|S2PH)XZrm(bk|kz)$",
                                               "^VCVTP(H2PS|S2PH)XZrm(k|bkz)$")>;

def SPRWriteResGroup320 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup320], (instregex "^VCVT(T?)PH2(U?)DQZrr(b?)$",
                                               "^VCVTP(H2PS|S2PH)(X?)Zrr(b?)$",
                                               "^VPSHUFBITQMBZ(128|256)rrk$")>;
def : InstRW<[SPRWriteResGroup320], (instrs VPSHUFBITQMBZrrk)>;

def SPRWriteResGroup321 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup321], (instregex "^VCVT(T?)PH2(U?)DQZrr(bk|kz)$",
                                               "^VCVT(T?)PH2(U?)DQZrr(k|bkz)$",
                                               "^VCVTP(H2PS|S2PH)XZrr(bk|kz)$",
                                               "^VCVTP(H2PS|S2PH)XZrr(k|bkz)$")>;

def SPRWriteResGroup322 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 2];
  let Latency = 23;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup322], (instregex "^VCVTPH2PDZ128rm(b?)$")>;

def SPRWriteResGroup323 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 1, 2];
  let Latency = 26;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup323], (instregex "^VCVTPH2PDZ128rm(bk|kz)$",
                                               "^VCVTPH2PDZ128rm(k|bkz)$")>;

def SPRWriteResGroup324 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 16;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup324], (instrs VCVTPH2PDZ128rr)>;

def SPRWriteResGroup325 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1, 2];
  let Latency = 19;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup325], (instregex "^VCVTPH2PDZ128rrk(z?)$")>;

def SPRWriteResGroup326 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 22;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup326], (instregex "^VCVTPH2PDZ256rm(b?)$")>;

def SPRWriteResGroup327 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 25;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup327], (instregex "^VCVTPH2PDZ256rm(bk|kz)$",
                                               "^VCVTPH2PDZ256rm(k|bkz)$")>;

def SPRWriteResGroup328 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup328], (instrs VCVTPH2PDZ256rr)>;

def SPRWriteResGroup329 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 18;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup329], (instregex "^VCVTPH2PDZ256rrk(z?)$")>;

def SPRWriteResGroup330 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup330], (instregex "^VCVTP(H2PS|S2PH)(X?)Z128rrk(z?)$")>;

def SPRWriteResGroup331 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 14;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup331], (instregex "^VCVTPH2PSZ(128|256)rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup331, ReadAfterVecLd], (instregex "^VCVTSH2SSZrm_Intk(z?)$")>;
def : InstRW<[SPRWriteResGroup331, ReadAfterVecXLd], (instregex "^VPMADDUBSWZ128rmk(z?)$",
                                                                "^VPMULH((U|RS)?)WZ128rmk(z?)$",
                                                                "^VPMULLWZ128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup331, ReadAfterVecYLd], (instregex "^VPMADDUBSWZ256rmk(z?)$",
                                                                "^VPMULH((U|RS)?)WZ256rmk(z?)$",
                                                                "^VPMULLWZ256rmk(z?)$")>;

def SPRWriteResGroup332 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup332], (instregex "^VCVT(T?)PS2(U?)QQZrm((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PS2(U?)QQZrmbkz$")>;
def : InstRW<[SPRWriteResGroup332], (instrs VCVTPH2PSZrm)>;
def : InstRW<[SPRWriteResGroup332, ReadAfterVecYLd], (instregex "^VPERMWZrmk(z?)$")>;

def SPRWriteResGroup333 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2, 1, 1, 1];
  let Latency = 17;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup333], (instregex "^VCVT(T?)PH2(U?)QQZ128rm((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PH2(U?)QQZ128rmbkz$")>;

def SPRWriteResGroup334 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2, 1];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup334], (instregex "^VCVT(T?)PH2(U?)QQZ(128|256)rr((k|kz)?)$")>;

def SPRWriteResGroup335 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2, 1, 1, 1];
  let Latency = 18;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup335], (instregex "^VCVT(T?)PH2(U?)QQZ256rm((b|k|bk|kz)?)$",
                                               "^VCVT(T?)PH2(U?)QQZ256rmbkz$")>;

def SPRWriteResGroup336 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup336], (instregex "^VCVTPS2PHXZ128rm(bk|kz)$",
                                               "^VCVTPS2PHXZ128rm(k|bkz)$",
                                               "^VCVTPS2PHXZ256rm(b?)$")>;

def SPRWriteResGroup337 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup337], (instregex "^VCVTPS2PHXZrm(b?)$")>;

def SPRWriteResGroup338 : SchedWriteRes<[SPRPort00_01, SPRPort04_09, SPRPort07_08]> {
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup338], (instregex "^VCVTPS2PHZ(128|256)mrk$")>;

def SPRWriteResGroup339 : SchedWriteRes<[SPRPort00, SPRPort04_09, SPRPort07_08]> {
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup339], (instrs VCVTPS2PHZmrk)>;

def SPRWriteResGroup340 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup340], (instregex "^VCVT(T?)PS2(U?)QQZ128rr((k|kz)?)$",
                                               "^VCVT(U?)QQ2PSZ128rr((k|kz)?)$")>;

def SPRWriteResGroup341 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 15;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup341], (instregex "^VCVT(U?)QQ2PHZ128rm(b?)$")>;

def SPRWriteResGroup342 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 17;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup342], (instregex "^VCVT(U?)QQ2PHZ128rm(bk|kz)$",
                                               "^VCVT(U?)QQ2PHZ128rm(k|bkz)$")>;

def SPRWriteResGroup343 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup343], (instregex "^VCVT(U?)QQ2PHZ128rr$")>;

def SPRWriteResGroup344 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort05]> {
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup344], (instregex "^VCVT(U?)QQ2PHZ128rrk(z?)$",
                                               "^VCVT(U?)QQ2PHZ256rr$")>;

def SPRWriteResGroup345 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 18;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup345], (instregex "^VCVT(U?)QQ2PHZ256rm(b?)$")>;

def SPRWriteResGroup346 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let Latency = 20;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup346], (instregex "^VCVT(U?)QQ2PHZ256rm(bk|kz)$",
                                               "^VCVT(U?)QQ2PHZ256rm(k|bkz)$")>;

def SPRWriteResGroup347 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort05]> {
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup347], (instregex "^VCVT(U?)QQ2PHZ256rrk(z?)$")>;

def SPRWriteResGroup348 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 1, 2];
  let Latency = 18;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup348], (instregex "^VCVT(U?)QQ2PHZrm(b?)$")>;

def SPRWriteResGroup349 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 1, 2];
  let Latency = 20;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup349], (instregex "^VCVT(U?)QQ2PHZrm(bk|kz)$",
                                               "^VCVT(U?)QQ2PHZrm(k|bkz)$")>;

def SPRWriteResGroup350 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup350], (instregex "^VCVT(U?)QQ2PHZrr(b?)$")>;

def SPRWriteResGroup351 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup351], (instregex "^VCVT(U?)QQ2PHZrr(bk|kz)$",
                                               "^VCVT(U?)QQ2PHZrr(k|bkz)$")>;

def SPRWriteResGroup352 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 2, 1, 1, 1];
  let Latency = 18;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup352, ReadAfterVecLd], (instregex "^VCVTSD2SHZrm((_Int)?)$")>;

def SPRWriteResGroup353 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 2, 1, 1, 1];
  let Latency = 21;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup353, ReadAfterVecLd], (instregex "^VCVTSD2SHZrm_Intk(z?)$")>;

def SPRWriteResGroup354 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 11;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup354], (instregex "^VCVTSD2SHZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup354], (instrs VCVTSD2SHZrr)>;

def SPRWriteResGroup355 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 14;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup355], (instregex "^VCVTSD2SHZrr(b?)_Intk(z?)$")>;

def SPRWriteResGroup356 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 18;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup356, ReadAfterVecLd], (instregex "^VCVTSH2SDZrm((_Int)?)$")>;

def SPRWriteResGroup357 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 20;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup357, ReadAfterVecLd], (instregex "^VCVTSH2SDZrm_Intk(z?)$")>;

def SPRWriteResGroup358 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup358], (instregex "^VCVTSH2SDZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup358], (instrs VCVTSH2SDZrr)>;

def SPRWriteResGroup359 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup359], (instregex "^VCVTSH2SDZrr(b?)_Intk(z?)$")>;

def SPRWriteResGroup360 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup360, ReadAfterVecLd], (instregex "^VCVT(T?)SH2(U?)SI((64)?)Zrm_Int$",
                                                               "^VCVTTSH2(U?)SI((64)?)Zrm$")>;

def SPRWriteResGroup361 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05]> {
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup361], (instregex "^VCVT(T?)SH2(U?)SI((64)?)Zrr(b?)_Int$",
                                               "^VCVTTSH2(U?)SI((64)?)Zrr$")>;

def SPRWriteResGroup362 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 8;
}
def : InstRW<[SPRWriteResGroup362], (instregex "^VCVTSH2SSZrr(b?)_Intk(z?)$")>;

def SPRWriteResGroup363 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup363, ReadAfterVecLd], (instregex "^VCVT(U?)SI((64)?)2SHZrm((_Int)?)$",
                                                               "^VCVTSS2SHZrm((_Int)?)$")>;

def SPRWriteResGroup364 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup364, ReadAfterVecLd], (instregex "^VCVTSS2SHZrm_Intk(z?)$")>;

def SPRWriteResGroup365 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup365], (instregex "^VCVTSS2SHZrr(b?)_Int$")>;
def : InstRW<[SPRWriteResGroup365], (instrs VCVTSS2SHZrr)>;

def SPRWriteResGroup366 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup366], (instregex "^VCVTSS2SHZrr(b?)_Intk(z?)$")>;

def SPRWriteResGroup367 : SchedWriteRes<[SPRPort05]> {
  let Latency = 5;
}
def : InstRW<[SPRWriteResGroup367], (instregex "^VDBPSADBWZ(128|256)rrik(z?)$",
                                               "^VDBPSADBWZrrik(z?)$",
                                               "^VPACK(S|U)S(DW|WB)Z(128|256)rrk(z?)$",
                                               "^VPACK(S|U)S(DW|WB)Zrrk(z?)$",
                                               "^VPBROADCAST(B|W|Dr|Qr|Wr)Z((256)?)rrk(z?)$",
                                               "^VPBROADCAST(B|D|Q|W)rZ(128|256)rr$",
                                               "^VPBROADCASTBrZ(128|256)rrk(z?)$",
                                               "^VPBROADCAST(B|D|Q|W)rZrr$",
                                               "^VPBROADCASTBrZrrk(z?)$",
                                               "^VPBROADCAST(D|Q|W)rZ128rrk(z?)$",
                                               "^VPERMBZ(128|256)rrk(z?)$",
                                               "^VPERMBZrrk(z?)$",
                                               "^VPMOV(S|Z)XBWZ((256)?)rrk(z?)$",
                                               "^VPMULTISHIFTQBZ(128|256)rrk(z?)$",
                                               "^VPMULTISHIFTQBZrrk(z?)$",
                                               "^VPOPCNT(B|W)Z(128|256)rrk(z?)$",
                                               "^VPOPCNT(B|W)Zrrk(z?)$")>;

def SPRWriteResGroup368 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 36;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup368, ReadAfterVecXLd], (instregex "^VDIVPHZ128rm(b?)$")>;

def SPRWriteResGroup369 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 38;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup369, ReadAfterVecXLd], (instregex "^VDIVPHZ128rm(bk|kz)$",
                                                                "^VDIVPHZ128rm(k|bkz)$")>;

def SPRWriteResGroup370 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 31;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup370], (instregex "^VDIVPHZ(128|256)rr$")>;

def SPRWriteResGroup371 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 33;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup371], (instregex "^VDIVPHZ(128|256)rrk$",
                                               "^VSQRTPHZ(128|256)r$")>;
def : InstRW<[SPRWriteResGroup371], (instrs VDIVPHZ128rrkz)>;

def SPRWriteResGroup372 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 37;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup372, ReadAfterVecYLd], (instregex "^VDIVPHZ256rm(b?)$")>;

def SPRWriteResGroup373 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 39;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup373, ReadAfterVecYLd], (instregex "^VDIVPHZ256rm(bk|kz)$",
                                                                "^VDIVPHZ256rm(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup373, ReadAfterVecXLd], (instregex "^VSQRTPHZ128m(b?)$")>;

def SPRWriteResGroup374 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 11;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup374], (instrs VDIVPHZ256rrkz)>;

def SPRWriteResGroup375 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [4, 2, 1, 1, 1];
  let Latency = 49;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup375, ReadAfterVecYLd], (instregex "^VDIVPHZrm(b?)$")>;

def SPRWriteResGroup376 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [4, 2, 1, 1, 1];
  let Latency = 51;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup376, ReadAfterVecYLd], (instregex "^VDIVPHZrm(bk|kz)$",
                                                                "^VDIVPHZrm(k|bkz)$")>;

def SPRWriteResGroup377 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [4, 1, 1];
  let Latency = 41;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup377], (instregex "^VDIVPHZrr(b?)$")>;

def SPRWriteResGroup378 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [4, 1, 1];
  let Latency = 43;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup378], (instregex "^VDIVPHZrr(bk|kz)$",
                                               "^VDIVPHZrr(k|bkz)$")>;

def SPRWriteResGroup379 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 17;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup379], (instrs VDIVPSZrr)>;

def SPRWriteResGroup380 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 21;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup380, ReadAfterVecLd], (instregex "^VDIVSHZrm_Int((k|kz)?)$")>;
def : InstRW<[SPRWriteResGroup380, ReadAfterVecLd], (instrs VDIVSHZrm)>;

def SPRWriteResGroup381 : SchedWriteRes<[SPRPort00]> {
  let Latency = 14;
}
def : InstRW<[SPRWriteResGroup381], (instrs VDIVSHZrr_Int,
                                            VSQRTSHZr_Int)>;

def SPRWriteResGroup382 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 15;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup382, ReadAfterVecXLd], (instregex "^VDPBF16PSZ128m((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup382, ReadAfterVecXLd], (instrs VDPBF16PSZ128mbkz)>;

def SPRWriteResGroup383 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let ReleaseAtCycles = [2, 2];
  let Latency = 8;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup383], (instregex "^VDPBF16PSZ(128|256)r((k|kz)?)$")>;

def SPRWriteResGroup384 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 2];
  let Latency = 16;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup384, ReadAfterVecYLd], (instregex "^VDPBF16PSZ256m((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup384, ReadAfterVecYLd], (instrs VDPBF16PSZ256mbkz)>;

def SPRWriteResGroup385 : SchedWriteRes<[SPRPort00, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [6, 7, 18];
  let Latency = 81;
  let NumMicroOps = 31;
}
def : InstRW<[SPRWriteResGroup385], (instrs VERRm)>;

def SPRWriteResGroup386 : SchedWriteRes<[SPRPort00, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [6, 7, 17];
  let Latency = 74;
  let NumMicroOps = 30;
}
def : InstRW<[SPRWriteResGroup386], (instrs VERRr)>;

def SPRWriteResGroup387 : SchedWriteRes<[SPRPort00, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [5, 8, 21];
  let Latency = 81;
  let NumMicroOps = 34;
}
def : InstRW<[SPRWriteResGroup387], (instrs VERWm)>;

def SPRWriteResGroup388 : SchedWriteRes<[SPRPort00, SPRPort01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [5, 8, 20];
  let Latency = 74;
  let NumMicroOps = 33;
}
def : InstRW<[SPRWriteResGroup388], (instrs VERWr)>;

def SPRWriteResGroup389 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup389, ReadAfterVecYLd], (instregex "^VEXPANDP(D|S)Z128rm((k|kz)?)$",
                                                                "^VPEXPAND(B|D|Q|W)Z128rm$",
                                                                "^VPEXPAND(D|Q)Z128rmk(z?)$")>;

def SPRWriteResGroup390 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup390], (instregex "^VF(C?)MADDCPHZ(128|256)m(b?)$",
                                               "^VROUNDP(D|S)Ymi$")>;
def : InstRW<[SPRWriteResGroup390, ReadAfterVecXLd], (instregex "^VF(C?)MADDCSHZm$",
                                                                "^VF(C?)MULCPHZ128rm(b?)$",
                                                                "^VF(C?)MULCSHZrm$",
                                                                "^VRNDSCALEPHZ128rm(b?)i$",
                                                                "^VRNDSCALESHZm((_Int)?)$",
                                                                "^VSCALEFPHZ128rm(b?)$")>;
def : InstRW<[SPRWriteResGroup390, ReadAfterVecYLd], (instregex "^VF(C?)MULCPHZ256rm(b?)$",
                                                                "^VRNDSCALEP(D|H|S)Z256rm(b?)i$",
                                                                "^VRNDSCALEP(D|S)Z256rm(b?)ik(z?)$",
                                                                "^VSCALEFPHZ256rm(b?)$")>;
def : InstRW<[SPRWriteResGroup390, ReadAfterVecLd], (instrs VSCALEFSHZrm)>;

def SPRWriteResGroup391 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 21;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup391], (instregex "^VF(C?)MADDCPHZ(128|256)m(bk|kz)$",
                                               "^VF(C?)MADDCPHZ(128|256)m(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup391, ReadAfterVecXLd], (instregex "^VF(C?)MADDCSHZmk(z?)$",
                                                                "^VF(C?)MULCPHZ128rm(bk|kz)$",
                                                                "^VF(C?)MULCPHZ128rm(k|bkz)$",
                                                                "^VF(C?)MULCSHZrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup391, ReadAfterVecYLd], (instregex "^VF(C?)MULCPHZ256rm(bk|kz)$",
                                                                "^VF(C?)MULCPHZ256rm(k|bkz)$")>;

def SPRWriteResGroup392 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [2];
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup392], (instregex "^VF(C?)MADDCPHZ(128|256)r$",
                                               "^VF(C?)MADDCSHZr(b?)$",
                                               "^VF(C?)MULCPHZ(128|256)rr$",
                                               "^VF(C?)MULCSHZrr(b?)$",
                                               "^VRNDSCALEPHZ(128|256)rri$",
                                               "^VRNDSCALESHZr(b?)_Int$",
                                               "^VSCALEFPHZ(128|256)rr$")>;
def : InstRW<[SPRWriteResGroup392], (instrs VRNDSCALESHZr,
                                            VSCALEFSHZrr,
                                            VSCALEFSHZrrb_Int)>;

def SPRWriteResGroup393 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [2];
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup393], (instregex "^VF(C?)MADDCPHZ(128|256)rk(z?)$",
                                               "^VF(C?)MADDCSHZr(bk|kz)$",
                                               "^VF(C?)MADDCSHZr(k|bkz)$",
                                               "^VF(C?)MULCPHZ(128|256)rrk(z?)$",
                                               "^VF(C?)MULCSHZrr(bk|kz)$",
                                               "^VF(C?)MULCSHZrr(k|bkz)$")>;

def SPRWriteResGroup394 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup394], (instregex "^VF(C?)MADDCPHZm(b?)$")>;
def : InstRW<[SPRWriteResGroup394, ReadAfterVecYLd], (instregex "^VF(C?)MULCPHZrm(b?)$",
                                                                "^VRNDSCALEP(D|H|S)Zrm(b?)i$",
                                                                "^VRNDSCALEP(D|S)Zrm(b?)ik(z?)$",
                                                                "^VSCALEFPHZrm(b?)$")>;

def SPRWriteResGroup395 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 21;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup395], (instregex "^VF(C?)MADDCPHZm(bk|kz)$",
                                               "^VF(C?)MADDCPHZm(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup395, ReadAfterVecYLd], (instregex "^VF(C?)MULCPHZrm(bk|kz)$",
                                                                "^VF(C?)MULCPHZrm(k|bkz)$")>;

def SPRWriteResGroup396 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [2];
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup396], (instregex "^VF(C?)MADDCPHZr(b?)$",
                                               "^VF(C?)MULCPHZrr(b?)$",
                                               "^VRNDSCALEPHZrri(b?)$",
                                               "^VSCALEFPHZrr(b?)$")>;

def SPRWriteResGroup397 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [2];
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup397], (instregex "^VF(C?)MADDCPHZr(bk|kz)$",
                                               "^VF(C?)MADDCPHZr(k|bkz)$",
                                               "^VF(C?)MULCPHZrr(bk|kz)$",
                                               "^VF(C?)MULCPHZrr(k|bkz)$")>;

def SPRWriteResGroup398 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 1, 2, 4];
  let Latency = 29;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup398, WriteVecMaskedGatherWriteback], (instregex "^VGATHER(D|Q)PDYrm$",
                                                                              "^VPGATHER(D|Q)QYrm$")>;
def : InstRW<[SPRWriteResGroup398, WriteVecMaskedGatherWriteback], (instrs VGATHERQPSYrm,
                                                                           VPGATHERQDYrm)>;

def SPRWriteResGroup399 : SchedWriteRes<[SPRPort00, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 20;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup399, WriteVecMaskedGatherWriteback], (instregex "^VGATHER(D|Q)PDZ128rm$",
                                                                              "^VPGATHER(D|Q)QZ128rm$")>;
def : InstRW<[SPRWriteResGroup399, WriteVecMaskedGatherWriteback], (instrs VGATHERQPSZ128rm,
                                                                           VPGATHERQDZ128rm)>;

def SPRWriteResGroup400 : SchedWriteRes<[SPRPort00, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 2, 4];
  let Latency = 28;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup400, WriteVecMaskedGatherWriteback], (instregex "^VGATHER(D|Q)PDZ256rm$",
                                                                              "^VPGATHER(D|Q)QZ256rm$")>;
def : InstRW<[SPRWriteResGroup400, WriteVecMaskedGatherWriteback], (instrs VGATHERQPSZ256rm,
                                                                           VPGATHERQDZ256rm)>;

def SPRWriteResGroup401 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 8, 2];
  let Latency = 28;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup401, WriteVecMaskedGatherWriteback], (instregex "^VGATHER(D|Q)PDZrm$",
                                                                              "^VPGATHER(D|Q)QZrm$")>;
def : InstRW<[SPRWriteResGroup401, WriteVecMaskedGatherWriteback], (instrs VGATHERQPSZrm,
                                                                           VPGATHERQDZrm)>;

def SPRWriteResGroup402 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 1, 1, 2];
  let Latency = 20;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup402, WriteVecMaskedGatherWriteback], (instregex "^VGATHER(D|Q)PDrm$",
                                                                              "^VPGATHER(D|Q)Qrm$")>;
def : InstRW<[SPRWriteResGroup402, WriteVecMaskedGatherWriteback], (instrs VGATHERQPSrm,
                                                                           VPGATHERQDrm)>;

def SPRWriteResGroup403 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 1, 2, 8];
  let Latency = 30;
  let NumMicroOps = 12;
}
def : InstRW<[SPRWriteResGroup403, WriteVecMaskedGatherWriteback], (instrs VGATHERDPSYrm,
                                                                           VPGATHERDDYrm)>;

def SPRWriteResGroup404 : SchedWriteRes<[SPRPort00, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 2, 4];
  let Latency = 27;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup404, WriteVecMaskedGatherWriteback], (instrs VGATHERDPSZ128rm,
                                                                           VPGATHERDDZ128rm)>;

def SPRWriteResGroup405 : SchedWriteRes<[SPRPort00, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 2, 8];
  let Latency = 29;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup405, WriteVecMaskedGatherWriteback], (instrs VGATHERDPSZ256rm,
                                                                           VPGATHERDDZ256rm)>;

def SPRWriteResGroup406 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 16, 2];
  let Latency = 30;
  let NumMicroOps = 19;
}
def : InstRW<[SPRWriteResGroup406, WriteVecMaskedGatherWriteback], (instrs VGATHERDPSZrm,
                                                                           VPGATHERDDZrm)>;

def SPRWriteResGroup407 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 1, 2, 4];
  let Latency = 28;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup407, WriteVecMaskedGatherWriteback], (instrs VGATHERDPSrm,
                                                                           VPGATHERDDrm)>;

def SPRWriteResGroup408 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup408, ReadAfterVecXLd], (instregex "^VGF2P8AFFINE((INV)?)QBZ128rm(b?)ik(z?)$",
                                                                "^VGF2P8MULBZ128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup408, ReadAfterVecYLd], (instregex "^VGF2P8AFFINE((INV)?)QBZ256rm(b?)ik(z?)$",
                                                                "^VGF2P8MULBZ256rmk(z?)$")>;

def SPRWriteResGroup409 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 9;
}
def : InstRW<[SPRWriteResGroup409], (instregex "^VGF2P8AFFINE((INV)?)QBZ(128|256)rrik$",
                                               "^VGF2P8MULBZ(128|256)rrk$")>;

def SPRWriteResGroup410 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 10;
}
def : InstRW<[SPRWriteResGroup410], (instregex "^VGF2P8AFFINE((INV)?)QBZ(128|256)rrikz$",
                                               "^VGF2P8MULBZ(128|256)rrkz$")>;

def SPRWriteResGroup411 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup411, ReadAfterVecYLd], (instregex "^VGF2P8AFFINE((INV)?)QBZrm(b?)ik(z?)$",
                                                                "^VGF2P8MULBZrmk(z?)$")>;

def SPRWriteResGroup412 : SchedWriteRes<[SPRPort00]> {
  let Latency = 9;
}
def : InstRW<[SPRWriteResGroup412], (instregex "^VGF2P8AFFINE((INV)?)QBZrrik$")>;
def : InstRW<[SPRWriteResGroup412], (instrs VGF2P8MULBZrrk)>;

def SPRWriteResGroup413 : SchedWriteRes<[SPRPort00]> {
  let Latency = 10;
}
def : InstRW<[SPRWriteResGroup413], (instregex "^VGF2P8AFFINE((INV)?)QBZrrikz$")>;
def : InstRW<[SPRWriteResGroup413], (instrs VGF2P8MULBZrrkz)>;

def SPRWriteResGroup414 : SchedWriteRes<[SPRPort01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 5;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup414], (instregex "^VH(ADD|SUB)P(D|S)rr$")>;

def SPRWriteResGroup415 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort02_03_11]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup415], (instrs VLDMXCSR)>;

def SPRWriteResGroup416 : SchedWriteRes<[SPRPort01, SPRPort01_05, SPRPort02_03, SPRPort02_03_11, SPRPort04, SPRPort04_09, SPRPort05, SPRPort06]> {
  let ReleaseAtCycles = [1, 1, 1, 8, 1, 1, 2, 3];
  let Latency = 40;
  let NumMicroOps = 18;
}
def : InstRW<[SPRWriteResGroup416], (instrs VMCLEARm)>;

def SPRWriteResGroup417 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup417], (instregex "^VMOVDQU(8|16)Z(128|256)rmk(z?)$",
                                               "^VMOVSHZrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup417, ReadAfterVecXLd], (instregex "^VP(ADD|SUB)(B|W)Z128rmk(z?)$",
                                                                "^VPBLENDM(B|W)Z128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup417, ReadAfterVecYLd], (instregex "^VP(ADD|SUB)(B|W)Z256rmk(z?)$",
                                                                "^VPBLENDM(B|W)Z256rmk(z?)$")>;

def SPRWriteResGroup418 : SchedWriteRes<[SPRPort00_01_05]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup418], (instregex "^VMOVDQU(8|16)Z(128|256)rrk(z?)((_REV)?)$",
                                               "^VMOVSHZrrk(z?)((_REV)?)$",
                                               "^VP(ADD|SUB)(B|W)Z(128|256)rrk(z?)$",
                                               "^VPBLENDM(B|W)Z(128|256)rrk(z?)$",
                                               "^VPMOVM2(B|W)Z(128|256)rr$")>;

def SPRWriteResGroup419 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 2, 2];
  let Latency = 12;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup419], (instrs VMOVDQU8Zmrk)>;

def SPRWriteResGroup420 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 477;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup420], (instrs VMOVNTDQZ128mr)>;

def SPRWriteResGroup421 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 470;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup421], (instrs VMOVNTDQZ256mr,
                                            VMOVNTPSmr)>;

def SPRWriteResGroup422 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 473;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup422], (instregex "^VMOVNT(PD|DQZ)mr$")>;

def SPRWriteResGroup423 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 521;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup423], (instrs VMOVNTDQmr)>;

def SPRWriteResGroup424 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 550;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup424], (instrs VMOVNTPDZ128mr)>;

def SPRWriteResGroup425 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 474;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup425], (instrs VMOVNTPDZ256mr)>;

def SPRWriteResGroup426 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 464;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup426], (instrs VMOVNTPDZmr)>;

def SPRWriteResGroup427 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 494;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup427], (instrs VMOVNTPSYmr)>;

def SPRWriteResGroup428 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 475;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup428], (instrs VMOVNTPSZ128mr)>;

def SPRWriteResGroup429 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 476;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup429], (instrs VMOVNTPSZ256mr)>;

def SPRWriteResGroup430 : SchedWriteRes<[SPRPort04_09, SPRPort07_08]> {
  let Latency = 471;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup430], (instrs VMOVNTPSZmr)>;

def SPRWriteResGroup431 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [3, 1, 8];
  let Latency = 10;
  let NumMicroOps = 12;
}
def : InstRW<[SPRWriteResGroup431, ReadAfterVecXLd], (instregex "^VP2INTERSECTDZ128rm(b?)$")>;
def : InstRW<[SPRWriteResGroup431, ReadAfterVecYLd], (instregex "^VP2INTERSECTQZ256rm(b?)$")>;

def SPRWriteResGroup432 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [4, 8];
  let Latency = 10;
  let NumMicroOps = 12;
}
def : InstRW<[SPRWriteResGroup432], (instrs VP2INTERSECTDZ128rr,
                                            VP2INTERSECTQZ256rr)>;

def SPRWriteResGroup433 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 8, 7, 2, 1, 11];
  let Latency = 27;
  let NumMicroOps = 30;
}
def : InstRW<[SPRWriteResGroup433, ReadAfterVecYLd], (instregex "^VP2INTERSECTDZ256rm(b?)$")>;

def SPRWriteResGroup434 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05, SPRPort01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 8, 8, 2, 11];
  let Latency = 27;
  let NumMicroOps = 30;
}
def : InstRW<[SPRWriteResGroup434], (instrs VP2INTERSECTDZ256rr)>;

def SPRWriteResGroup435 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [13, 9, 1, 23];
  let Latency = 40;
  let NumMicroOps = 46;
}
def : InstRW<[SPRWriteResGroup435, ReadAfterVecYLd], (instregex "^VP2INTERSECTDZrm(b?)$")>;

def SPRWriteResGroup436 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [13, 10, 23];
  let Latency = 40;
  let NumMicroOps = 46;
}
def : InstRW<[SPRWriteResGroup436], (instrs VP2INTERSECTDZrr)>;

def SPRWriteResGroup437 : SchedWriteRes<[SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 4];
  let Latency = 6;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup437, ReadAfterVecXLd], (instregex "^VP2INTERSECTQZ128rm(b?)$")>;

def SPRWriteResGroup438 : SchedWriteRes<[SPRPort05]> {
  let ReleaseAtCycles = [4];
  let Latency = 6;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup438], (instrs VP2INTERSECTQZ128rr)>;

def SPRWriteResGroup439 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [8, 7, 1, 14];
  let Latency = 29;
  let NumMicroOps = 30;
}
def : InstRW<[SPRWriteResGroup439, ReadAfterVecYLd], (instregex "^VP2INTERSECTQZrm(b?)$")>;

def SPRWriteResGroup440 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [8, 8, 14];
  let Latency = 30;
  let NumMicroOps = 30;
}
def : InstRW<[SPRWriteResGroup440], (instrs VP2INTERSECTQZrr)>;

def SPRWriteResGroup441 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 3;
}
def : InstRW<[SPRWriteResGroup441], (instregex "^VP(A|SU)BS(B|W)Z(128|256)rrk(z?)$",
                                               "^VPADD(U?)S(B|W)Z(128|256)rrk(z?)$",
                                               "^VPAVG(B|W)Z(128|256)rrk(z?)$",
                                               "^VPM(AX|IN)(SB|UW)Z(128|256)rrk(z?)$",
                                               "^VPM(AX|IN)(SW|UB)Z(128|256)rrk(z?)$",
                                               "^VPSH(L|R)DVWZ(128|256)rk(z?)$",
                                               "^VPS(L|R)LVWZ(128|256)rrk(z?)$",
                                               "^VPS(L|R)LWZ(128|256)rik(z?)$",
                                               "^VPSRAVWZ(128|256)rrk(z?)$",
                                               "^VPSRAWZ(128|256)rik(z?)$",
                                               "^VPSUBUS(B|W)Z(128|256)rrk(z?)$")>;

def SPRWriteResGroup442 : SchedWriteRes<[SPRPort01_05, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup442, ReadAfterVecYLd], (instregex "^VSHUFP(D|S)Yrmi$",
                                                                "^VSHUFP(D|S)Z256rm(bi|ik)$",
                                                                "^VSHUFP(D|S)Z256rmbik(z?)$",
                                                                "^VSHUFP(D|S)Z256rmi((kz)?)$")>;
def : InstRW<[SPRWriteResGroup442, ReadAfterVecYLd], (instrs VPBLENDWYrmi)>;

def SPRWriteResGroup443 : SchedWriteRes<[SPRPort00, SPRPort05]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup443], (instregex "^VPBROADCASTM(B2Q|W2D)Z(128|256)rr$",
                                               "^VPBROADCASTM(B2Q|W2D)Zrr$",
                                               "^VP(ERM|SRA)WZrrk(z?)$",
                                               "^VPSHUFBITQMBZ(128|256)rr$",
                                               "^VPS(L|R)LWZrrk(z?)$")>;
def : InstRW<[SPRWriteResGroup443], (instrs VPSHUFBITQMBZrr)>;

def SPRWriteResGroup444 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 1, 2, 1];
  let Latency = 12;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup444], (instregex "^VPCOMPRESS(B|W)Z(128|256)mr$")>;
def : InstRW<[SPRWriteResGroup444], (instrs VPCOMPRESSWZmr)>;

def SPRWriteResGroup445 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 1, 2, 1];
  let Latency = 14;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup445], (instregex "^VPCOMPRESS(B|W)Z(128|256)mrk$")>;
def : InstRW<[SPRWriteResGroup445], (instrs VPCOMPRESSWZmrk)>;

def SPRWriteResGroup446 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 2, 2, 2];
  let Latency = 12;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup446], (instrs VPCOMPRESSBZmr)>;

def SPRWriteResGroup447 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 2, 2, 2];
  let Latency = 14;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup447], (instrs VPCOMPRESSBZmrk)>;

def SPRWriteResGroup448 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [5, 4, 1, 5];
  let Latency = 17;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup448], (instregex "^VPCONFLICTDZ128rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup448], (instrs VPCONFLICTDZ128rmbkz)>;

def SPRWriteResGroup449 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [5, 5, 5];
  let Latency = 12;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup449], (instregex "^VPCONFLICTDZ128rr((k|kz)?)$")>;

def SPRWriteResGroup450 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 5, 1, 1, 9];
  let Latency = 24;
  let NumMicroOps = 23;
}
def : InstRW<[SPRWriteResGroup450], (instregex "^VPCONFLICTDZ256rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup450], (instrs VPCONFLICTDZ256rmbkz)>;

def SPRWriteResGroup451 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 1, 9];
  let Latency = 17;
  let NumMicroOps = 23;
}
def : InstRW<[SPRWriteResGroup451], (instregex "^VPCONFLICTDZ256rr((k|kz)?)$")>;

def SPRWriteResGroup452 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [11, 8, 1, 17];
  let Latency = 33;
  let NumMicroOps = 37;
}
def : InstRW<[SPRWriteResGroup452], (instregex "^VPCONFLICTDZrm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup452], (instrs VPCONFLICTDZrmbkz)>;

def SPRWriteResGroup453 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [11, 9, 17];
  let Latency = 26;
  let NumMicroOps = 37;
}
def : InstRW<[SPRWriteResGroup453], (instregex "^VPCONFLICTDZrr((kz)?)$")>;

def SPRWriteResGroup454 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [11, 9, 17];
  let Latency = 25;
  let NumMicroOps = 37;
}
def : InstRW<[SPRWriteResGroup454], (instrs VPCONFLICTDZrrk)>;

def SPRWriteResGroup455 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 11;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup455], (instregex "^VPCONFLICTQZ128rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup455], (instrs VPCONFLICTQZ128rmbkz)>;
def : InstRW<[SPRWriteResGroup455, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZ128rm$")>;

def SPRWriteResGroup456 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 4;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup456], (instregex "^VPCONFLICTQZ128rr((k|kz)?)$")>;

def SPRWriteResGroup457 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [5, 4, 1, 5];
  let Latency = 20;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup457], (instregex "^VPCONFLICTQZ256rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup457], (instrs VPCONFLICTQZ256rmbkz)>;

def SPRWriteResGroup458 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [5, 5, 5];
  let Latency = 13;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup458], (instregex "^VPCONFLICTQZ256rr((k|kz)?)$")>;

def SPRWriteResGroup459 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [7, 5, 1, 9];
  let Latency = 23;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup459], (instregex "^VPCONFLICTQZrm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup459], (instrs VPCONFLICTQZrmbkz)>;

def SPRWriteResGroup460 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 9];
  let Latency = 17;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup460], (instregex "^VPCONFLICTQZrr((kz)?)$")>;

def SPRWriteResGroup461 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [7, 6, 9];
  let Latency = 16;
  let NumMicroOps = 22;
}
def : InstRW<[SPRWriteResGroup461], (instrs VPCONFLICTQZrrk)>;

def SPRWriteResGroup462 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 13;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup462, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZ128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup462, ReadAfterVecYLd], (instrs VPERMT2WZ128rm)>;

def SPRWriteResGroup463 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 5;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup463], (instregex "^VPERM(I|T)2BZ(128|256)rr$")>;

def SPRWriteResGroup464 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup464], (instregex "^VPERM(I|T)2BZ(128|256)rrk(z?)$",
                                               "^VPERM(I|T)2WZ(128|256)rr$")>;

def SPRWriteResGroup465 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup465, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZ256rm$")>;

def SPRWriteResGroup466 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 14;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup466, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZ256rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup466, ReadAfterVecYLd], (instrs VPERMI2WZ128rm,
                                                             VPERMT2WZ256rm)>;

def SPRWriteResGroup467 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup467, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZrm$")>;

def SPRWriteResGroup468 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 14;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup468, ReadAfterVecYLd], (instregex "^VPERM(I|T)2BZrmk(z?)$")>;
def : InstRW<[SPRWriteResGroup468, ReadAfterVecYLd], (instrs VPERMT2WZrm)>;

def SPRWriteResGroup469 : SchedWriteRes<[SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 5;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup469], (instregex "^VPERM(I|T)2BZrr$")>;

def SPRWriteResGroup470 : SchedWriteRes<[SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup470], (instregex "^VPERM(I|T)2BZrrk(z?)$",
                                               "^VPERM(I|T)2WZrr$")>;

def SPRWriteResGroup471 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup471, ReadAfterVecYLd], (instregex "^VPERMI2WZ128rmk(z?)$",
                                                                "^VPERMT2WZ256rmk(z?)$")>;

def SPRWriteResGroup472 : SchedWriteRes<[SPRPort00_01_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup472], (instregex "^VPERM(I|T)2WZ(128|256)rrk(z?)$")>;

def SPRWriteResGroup473 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup473, ReadAfterVecYLd], (instregex "^VPERMT2WZ128rmk(z?)$")>;
def : InstRW<[SPRWriteResGroup473, ReadAfterVecYLd], (instrs VPERMI2WZ256rm)>;

def SPRWriteResGroup474 : SchedWriteRes<[SPRPort00_01_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 17;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup474, ReadAfterVecYLd], (instregex "^VPERMI2WZ256rmk(z?)$")>;

def SPRWriteResGroup475 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup475, ReadAfterVecYLd], (instrs VPERMI2WZrm)>;

def SPRWriteResGroup476 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 17;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup476, ReadAfterVecYLd], (instregex "^VPERMI2WZrmk(z?)$")>;

def SPRWriteResGroup477 : SchedWriteRes<[SPRPort00_05, SPRPort05]> {
  let ReleaseAtCycles = [1, 2];
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup477], (instregex "^VPERM(I|T)2WZrrk(z?)$")>;

def SPRWriteResGroup478 : SchedWriteRes<[SPRPort00_05, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [1, 1, 2];
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup478, ReadAfterVecYLd], (instregex "^VPERMT2WZrmk(z?)$")>;

def SPRWriteResGroup479 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup479, ReadAfterVecYLd], (instrs VPERMWZ128rm)>;

def SPRWriteResGroup480 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup480, ReadAfterVecYLd], (instregex "^VPERMWZ(128|256)rmk(z?)$")>;

def SPRWriteResGroup481 : SchedWriteRes<[SPRPort00_01, SPRPort05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup481], (instregex "^VPERMWZ(128|256)rr$")>;

def SPRWriteResGroup482 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11, SPRPort05]> {
  let Latency = 11;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup482, ReadAfterVecYLd], (instrs VPERMWZ256rm)>;

def SPRWriteResGroup483 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 11;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup483, ReadAfterVecYLd], (instrs VPERMWZrm)>;

def SPRWriteResGroup484 : SchedWriteRes<[SPRPort05]> {
  let ReleaseAtCycles = [2];
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup484], (instregex "^VPEXPAND(B|W)Z(128|256)rrk(z?)$",
                                               "^VPEXPAND(B|W)Zrrk(z?)$")>;

def SPRWriteResGroup485 : SchedWriteRes<[SPRPort00_01, SPRPort01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [1, 2, 1];
  let Latency = 10;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup485, ReadAfterVecYLd], (instregex "^VPH(ADD|SUB)SWYrm$")>;

def SPRWriteResGroup486 : SchedWriteRes<[SPRPort00_01]> {
  let Latency = 7;
}
def : InstRW<[SPRWriteResGroup486], (instregex "^VPMADDUBSWZ(128|256)rrk(z?)$",
                                               "^VPMULH((U|RS)?)WZ(128|256)rrk(z?)$",
                                               "^VPMULLWZ(128|256)rrk(z?)$")>;

def SPRWriteResGroup487 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 14;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup487, ReadAfterVecYLd], (instregex "^VPMADDUBSWZrmk(z?)$",
                                                                "^VPMULH((U|RS)?)WZrmk(z?)$",
                                                                "^VPMULLWZrmk(z?)$")>;

def SPRWriteResGroup488 : SchedWriteRes<[SPRPort00]> {
  let Latency = 7;
}
def : InstRW<[SPRWriteResGroup488], (instregex "^VPMADDUBSWZrrk(z?)$",
                                               "^VPMULH((U|RS)?)WZrrk(z?)$",
                                               "^VPMULLWZrrk(z?)$")>;

def SPRWriteResGroup489 : SchedWriteRes<[SPRPort01_05, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 12;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup489], (instregex "^VPMOV((US)?)DBZ(128|256)mr$",
                                               "^VPMOV((S|US)?)(D|Q)WZ(128|256)mr$",
                                               "^VPMOV(Q|W|SD|SW)BZ256mr$",
                                               "^VPMOV(W|SD)BZ128mr$",
                                               "^VPMOV(U?)SQBZ256mr$",
                                               "^VPMOV(U?)SQDZ(128|256)mr$",
                                               "^VPMOV(U?)SWBZ128mr$")>;
def : InstRW<[SPRWriteResGroup489], (instrs VPMOVUSWBZ256mr)>;

def SPRWriteResGroup490 : SchedWriteRes<[SPRPort01_05, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 13;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup490], (instregex "^VPMOV(D|Q|W|SQ|SW)BZ128mrk$",
                                               "^VPMOV((S|US)?)(D|Q)WZ128mrk$",
                                               "^VPMOV(U?)S(DB|QD)Z128mrk$",
                                               "^VPMOVUS(Q|W)BZ128mrk$")>;

def SPRWriteResGroup491 : SchedWriteRes<[SPRPort01_05, SPRPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup491], (instregex "^VPMOV(D|Q|W|SQ|SW)BZ128rr$",
                                               "^VPMOV((S|US)?)(D|Q)WZ128rr$",
                                               "^VPMOV(U?)S(DB|QD)Z128rr$",
                                               "^VPMOV(U?)SQDZ128rrk(z?)$",
                                               "^VPMOVUS(Q|W)BZ128rr$")>;

def SPRWriteResGroup492 : SchedWriteRes<[SPRPort01_05, SPRPort05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup492], (instregex "^VPMOV(D|Q|W|SQ|SW)BZ128rrk(z?)$",
                                               "^VPMOV(D|Q|W|SQ|SW)BZ256rr$",
                                               "^VPMOV((S|US)?)(D|Q)WZ128rrk(z?)$",
                                               "^VPMOV((S|US)?)(D|Q)WZ256rr$",
                                               "^VPMOV(U?)SDBZ128rrk(z?)$",
                                               "^VPMOV(U?)S(DB|QD)Z256rr$",
                                               "^VPMOV(U?)SQDZ256rrk(z?)$",
                                               "^VPMOVUS(Q|W)BZ128rrk(z?)$",
                                               "^VPMOVUS(Q|W)BZ256rr$")>;

def SPRWriteResGroup493 : SchedWriteRes<[SPRPort01_05, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 15;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup493], (instregex "^VPMOV(D|Q|W|SQ|SW)BZ256mrk$",
                                               "^VPMOV((S|US)?)(D|Q)WZ256mrk$",
                                               "^VPMOV(U?)S(DB|QD)Z256mrk$",
                                               "^VPMOVUS(Q|W)BZ256mrk$")>;

def SPRWriteResGroup494 : SchedWriteRes<[SPRPort01_05, SPRPort05]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup494], (instregex "^VPMOV(D|Q|W|SQ|SW)BZ256rrk(z?)$",
                                               "^VPMOV((S|US)?)(D|Q)WZ256rrk(z?)$",
                                               "^VPMOV(U?)SDBZ256rrk(z?)$",
                                               "^VPMOVUS(Q|W)BZ256rrk(z?)$")>;

def SPRWriteResGroup495 : SchedWriteRes<[SPRPort01_05, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 20;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup495], (instregex "^VPMOV((S|US)?)QBZ128mr$")>;

def SPRWriteResGroup496 : SchedWriteRes<[SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let Latency = 14;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup496], (instregex "^VPMOVQDZ((256)?)mrk$")>;

def SPRWriteResGroup497 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 23;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup497, ReadAfterVecXLd], (instregex "^VPMULLQZ128rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup497, ReadAfterVecXLd], (instrs VPMULLQZ128rmbkz)>;
def : InstRW<[SPRWriteResGroup497, ReadAfterVecYLd], (instregex "^VPMULLQZ256rm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup497, ReadAfterVecYLd], (instrs VPMULLQZ256rmbkz)>;

def SPRWriteResGroup498 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [3];
  let Latency = 15;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup498], (instregex "^VPMULLQZ(128|256)rr((k|kz)?)$")>;

def SPRWriteResGroup499 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 23;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup499, ReadAfterVecYLd], (instregex "^VPMULLQZrm((b|k|bk|kz)?)$")>;
def : InstRW<[SPRWriteResGroup499, ReadAfterVecYLd], (instrs VPMULLQZrmbkz)>;

def SPRWriteResGroup500 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [3];
  let Latency = 15;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup500], (instregex "^VPMULLQZrr((k|kz)?)$")>;

def SPRWriteResGroup501 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 1, 4, 4];
  let Latency = 12;
  let NumMicroOps = 11;
}
def : InstRW<[SPRWriteResGroup501], (instregex "^VPSCATTER(D|Q)QZ256mr$",
                                               "^VSCATTER(D|Q)PDZ256mr$")>;
def : InstRW<[SPRWriteResGroup501], (instrs VPSCATTERDDZ128mr,
                                            VPSCATTERQDZ256mr,
                                            VSCATTERDPSZ128mr,
                                            VSCATTERQPSZ256mr)>;

def SPRWriteResGroup502 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 1, 8, 8];
  let Latency = 12;
  let NumMicroOps = 19;
}
def : InstRW<[SPRWriteResGroup502], (instrs VPSCATTERDDZ256mr,
                                            VSCATTERDPSZ256mr)>;

def SPRWriteResGroup503 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 16, 16];
  let Latency = 19;
  let NumMicroOps = 35;
}
def : InstRW<[SPRWriteResGroup503], (instrs VPSCATTERDDZmr,
                                            VSCATTERDPSZmr)>;

def SPRWriteResGroup504 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 1, 1, 2, 2];
  let Latency = 12;
  let NumMicroOps = 7;
}
def : InstRW<[SPRWriteResGroup504], (instregex "^VPSCATTER(D|Q)QZ128mr$",
                                               "^VSCATTER(D|Q)PDZ128mr$")>;
def : InstRW<[SPRWriteResGroup504], (instrs VPSCATTERQDZ128mr,
                                            VSCATTERQPSZ128mr)>;

def SPRWriteResGroup505 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06_10, SPRPort04_09, SPRPort07_08]> {
  let ReleaseAtCycles = [2, 1, 8, 8];
  let Latency = 12;
  let NumMicroOps = 19;
}
def : InstRW<[SPRWriteResGroup505], (instregex "^VPSCATTER(D|Q)QZmr$",
                                               "^VSCATTER(D|Q)PDZmr$")>;
def : InstRW<[SPRWriteResGroup505], (instrs VPSCATTERQDZmr,
                                            VSCATTERQPSZmr)>;

def SPRWriteResGroup506 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup506, ReadAfterVecXLd], (instregex "^VPSH(L|R)D(D|Q)Z128rmbi$",
                                                                "^VPSH(L|R)D(D|Q|W)Z128rmi$",
                                                                "^VPSH(L|R)DV(D|Q|W)Z128m$",
                                                                "^VPSH(L|R)DV(D|Q)Z128m(b|k|kz)$",
                                                                "^VPSH(L|R)DV(D|Q)Z128mbk(z?)$")>;

def SPRWriteResGroup507 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup507, ReadAfterVecXLd], (instregex "^VPSH(L|R)D(D|Q)Z128rm(b?)ik(z?)$")>;

def SPRWriteResGroup508 : SchedWriteRes<[SPRPort00_01]>;
def : InstRW<[SPRWriteResGroup508], (instregex "^VPSH(L|R)D(D|Q|W)Z(128|256)rri$",
                                               "^VPSH(L|R)DV(D|Q|W)Z(128|256)r$",
                                               "^VPSH(L|R)DV(D|Q)Z(128|256)rk(z?)$")>;

def SPRWriteResGroup509 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup509], (instregex "^VPSH(L|R)D(D|Q)Z(128|256)rrik(z?)$")>;

def SPRWriteResGroup510 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup510, ReadAfterVecYLd], (instregex "^VPSH(L|R)D(D|Q)Z256rmbi$",
                                                                "^VPSH(L|R)D(D|Q|W)Z256rmi$",
                                                                "^VPSH(L|R)DV(D|Q|W)Z256m$",
                                                                "^VPSH(L|R)DV(D|Q)Z256m(b|k|kz)$",
                                                                "^VPSH(L|R)DV(D|Q)Z256mbk(z?)$")>;

def SPRWriteResGroup511 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup511, ReadAfterVecYLd], (instregex "^VPSH(L|R)D(D|Q)Z256rm(b?)ik(z?)$")>;

def SPRWriteResGroup512 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup512, ReadAfterVecYLd], (instregex "^VPSH(L|R)D(D|Q)Zrmbi$",
                                                                "^VPSH(L|R)D(D|Q|W)Zrmi$",
                                                                "^VPSH(L|R)DV(D|Q|W)Zm$",
                                                                "^VPSH(L|R)DV(D|Q)Zm(b|k|kz)$",
                                                                "^VPSH(L|R)DV(D|Q)Zmbk(z?)$")>;

def SPRWriteResGroup513 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let Latency = 10;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup513, ReadAfterVecYLd], (instregex "^VPSH(L|R)D(D|Q)Zrm(b?)ik(z?)$")>;

def SPRWriteResGroup514 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup514], (instregex "^VPSH(L|R)D(D|Q)Zrrik(z?)$")>;

def SPRWriteResGroup515 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 11;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup515, ReadAfterVecXLd], (instregex "^VPSH(L|R)DWZ128rmik(z?)$")>;

def SPRWriteResGroup516 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup516], (instregex "^VPSH(L|R)DWZ(128|256)rrik(z?)$")>;

def SPRWriteResGroup517 : SchedWriteRes<[SPRPort00_01, SPRPort00_01_05, SPRPort02_03_11]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup517, ReadAfterVecYLd], (instregex "^VPSH(L|R)DWZ256rmik(z?)$")>;

def SPRWriteResGroup518 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup518, ReadAfterVecYLd], (instregex "^VPSH(L|R)DWZrmik(z?)$")>;

def SPRWriteResGroup519 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup519], (instregex "^VPSH(L|R)DWZrrik(z?)$")>;

def SPRWriteResGroup520 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 6;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup520, ReadAfterVecXLd], (instrs VPSHUFBITQMBZ128rm)>;
def : InstRW<[SPRWriteResGroup520, ReadAfterVecYLd], (instregex "^VPSHUFBITQMBZ((256)?)rm$")>;

def SPRWriteResGroup521 : SchedWriteRes<[SPRPort00, SPRPort02_03_11, SPRPort05]> {
  let Latency = 8;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup521, ReadAfterVecXLd], (instrs VPSHUFBITQMBZ128rmk)>;
def : InstRW<[SPRWriteResGroup521, ReadAfterVecYLd], (instregex "^VPSHUFBITQMBZ((256)?)rmk$")>;

def SPRWriteResGroup522 : SchedWriteRes<[SPRPort00_01, SPRPort01_05]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup522], (instregex "^VPS(L|R)LWZ128rrk(z?)$",
                                               "^VPSRAWZ128rrk(z?)$")>;

def SPRWriteResGroup523 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 16;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup523, ReadAfterVecYLd], (instregex "^VR(CP|SQRT)PHZm(bk|kz)$",
                                                                "^VR(CP|SQRT)PHZm(k|bkz)$")>;

def SPRWriteResGroup524 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 9;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup524], (instregex "^VRCPPHZrk(z?)$")>;

def SPRWriteResGroup525 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 20;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup525, ReadAfterVecXLd], (instregex "^VREDUCEPHZ128rm(b?)i$")>;
def : InstRW<[SPRWriteResGroup525, ReadAfterVecXLd], (instrs VREDUCESHZrmi)>;
def : InstRW<[SPRWriteResGroup525, ReadAfterVecYLd], (instregex "^VREDUCEPHZ256rm(b?)i$")>;

def SPRWriteResGroup526 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 22;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup526, ReadAfterVecXLd], (instregex "^VREDUCEPHZ128rm(b?)ik(z?)$",
                                                                "^VREDUCESHZrmik(z?)$")>;
def : InstRW<[SPRWriteResGroup526, ReadAfterVecYLd], (instregex "^VREDUCEPHZ256rm(b?)ik(z?)$")>;

def SPRWriteResGroup527 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [3];
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup527], (instregex "^VREDUCEPHZ(128|256)rri$",
                                               "^VREDUCESHZrri(b?)$")>;

def SPRWriteResGroup528 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [3];
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup528], (instregex "^VREDUCEPHZ(128|256)rrik(z?)$",
                                               "^VREDUCESHZrri(bk|kz)$",
                                               "^VREDUCESHZrri(k|bkz)$")>;

def SPRWriteResGroup529 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 20;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup529, ReadAfterVecYLd], (instregex "^VREDUCEPHZrm(b?)i$")>;

def SPRWriteResGroup530 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [3, 1];
  let Latency = 22;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup530, ReadAfterVecYLd], (instregex "^VREDUCEPHZrm(b?)ik(z?)$")>;

def SPRWriteResGroup531 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [3];
  let Latency = 13;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup531], (instregex "^VREDUCEPHZrri(b?)$")>;

def SPRWriteResGroup532 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [3];
  let Latency = 16;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup532], (instregex "^VREDUCEPHZrri(bk|kz)$",
                                               "^VREDUCEPHZrri(k|bkz)$")>;

def SPRWriteResGroup533 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [2];
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup533], (instregex "^VRNDSCALEP(D|S)Zrri((b|k|bk|kz)?)$",
                                               "^VRNDSCALEP(D|S)Zrribkz$")>;

def SPRWriteResGroup534 : SchedWriteRes<[SPRPort00_01, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 17;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup534, ReadAfterVecXLd], (instregex "^VRNDSCALEPHZ128rm(b?)ik(z?)$",
                                                                "^VRNDSCALESHZm_Intk(z?)$",
                                                                "^VSCALEFPHZ128rm(bk|kz)$",
                                                                "^VSCALEFPHZ128rm(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup534, ReadAfterVecYLd], (instregex "^VRNDSCALEPHZ256rm(b?)ik(z?)$",
                                                                "^VSCALEFPHZ256rm(bk|kz)$",
                                                                "^VSCALEFPHZ256rm(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup534, ReadAfterVecLd], (instregex "^VSCALEFSHZrmk(z?)$")>;

def SPRWriteResGroup535 : SchedWriteRes<[SPRPort00_01]> {
  let ReleaseAtCycles = [2];
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup535], (instregex "^VRNDSCALEPHZ(128|256)rrik(z?)$",
                                               "^VRNDSCALESHZr(b?)_Intk(z?)$",
                                               "^VSCALEFPHZ(128|256)rrk(z?)$",
                                               "^VSCALEFSHZrrb_Intk(z?)$",
                                               "^VSCALEFSHZrrk(z?)$")>;

def SPRWriteResGroup536 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 17;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup536, ReadAfterVecYLd], (instregex "^VRNDSCALEPHZrm(b?)ik(z?)$",
                                                                "^VSCALEFPHZrm(bk|kz)$",
                                                                "^VSCALEFPHZrm(k|bkz)$")>;

def SPRWriteResGroup537 : SchedWriteRes<[SPRPort00]> {
  let ReleaseAtCycles = [2];
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup537], (instregex "^VRNDSCALEPHZrri(bk|kz)$",
                                               "^VRNDSCALEPHZrri(k|bkz)$",
                                               "^VSCALEFPHZrr(bk|kz)$",
                                               "^VSCALEFPHZrr(k|bkz)$")>;

def SPRWriteResGroup538 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 6;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup538], (instregex "^VRSQRT14P(D|S)Zr$")>;
def : InstRW<[SPRWriteResGroup538], (instrs VRSQRT14PSZrk,
                                            VRSQRTPHZr)>;

def SPRWriteResGroup539 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 25;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup539], (instrs VSQRTPDYm)>;
def : InstRW<[SPRWriteResGroup539, ReadAfterVecYLd], (instregex "^VSQRTPDZ256m(b?)$")>;

def SPRWriteResGroup540 : SchedWriteRes<[SPRPort00, SPRPort02_03_11]> {
  let Latency = 20;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup540, ReadAfterVecXLd], (instregex "^VSQRTPDZ128m(bk|kz)$",
                                                                "^VSQRTPDZ128m(k|bkz)$")>;
def : InstRW<[SPRWriteResGroup540, ReadAfterVecLd], (instregex "^VSQRTSDZm_Intk(z?)$")>;

def SPRWriteResGroup541 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 38;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup541, ReadAfterVecYLd], (instrs VSQRTPDZm)>;

def SPRWriteResGroup542 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 39;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup542, ReadAfterVecYLd], (instrs VSQRTPDZmb)>;

def SPRWriteResGroup543 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 31;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup543], (instrs VSQRTPDZr)>;

def SPRWriteResGroup544 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 41;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup544, ReadAfterVecXLd], (instregex "^VSQRTPHZ128m(bk|kz)$",
                                                                "^VSQRTPHZ128m(k|bkz)$")>;

def SPRWriteResGroup545 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 35;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup545], (instregex "^VSQRTPHZ(128|256)rk$")>;
def : InstRW<[SPRWriteResGroup545], (instrs VSQRTPHZ256rkz)>;

def SPRWriteResGroup546 : SchedWriteRes<[SPRPort00, SPRPort00_01_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 12;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup546], (instrs VSQRTPHZ128rkz)>;

def SPRWriteResGroup547 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 40;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup547, ReadAfterVecYLd], (instregex "^VSQRTPHZ256m(b?)$")>;

def SPRWriteResGroup548 : SchedWriteRes<[SPRPort00, SPRPort00_01_05, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1, 1];
  let Latency = 42;
  let NumMicroOps = 4;
}
def : InstRW<[SPRWriteResGroup548, ReadAfterVecYLd], (instregex "^VSQRTPHZ256m(bk|kz)$",
                                                                "^VSQRTPHZ256m(k|bkz)$")>;

def SPRWriteResGroup549 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [4, 2, 1, 1, 1];
  let Latency = 53;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup549, ReadAfterVecYLd], (instregex "^VSQRTPHZm(b?)$")>;

def SPRWriteResGroup550 : SchedWriteRes<[SPRPort00, SPRPort00_05, SPRPort00_06, SPRPort02_03_11, SPRPort05]> {
  let ReleaseAtCycles = [4, 2, 1, 1, 1];
  let Latency = 55;
  let NumMicroOps = 9;
}
def : InstRW<[SPRWriteResGroup550, ReadAfterVecYLd], (instregex "^VSQRTPHZm(bk|kz)$",
                                                                "^VSQRTPHZm(k|bkz)$")>;

def SPRWriteResGroup551 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [4, 1, 1];
  let Latency = 45;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup551], (instregex "^VSQRTPHZr(b?)$")>;

def SPRWriteResGroup552 : SchedWriteRes<[SPRPort00, SPRPort00_06, SPRPort05]> {
  let ReleaseAtCycles = [4, 1, 1];
  let Latency = 47;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup552], (instregex "^VSQRTPHZr(bk|kz)$",
                                               "^VSQRTPHZr(k|bkz)$")>;

def SPRWriteResGroup553 : SchedWriteRes<[SPRPort00, SPRPort00_05]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 19;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup553], (instrs VSQRTPSZr)>;

def SPRWriteResGroup554 : SchedWriteRes<[SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort01_05_10]> {
  let ReleaseAtCycles = [1, 2, 3, 3, 1];
  let Latency = 12;
  let NumMicroOps = 10;
}
def : InstRW<[SPRWriteResGroup554], (instrs VZEROALL)>;

def SPRWriteResGroup555 : SchedWriteRes<[SPRPort00_01_05_06]> {
  let ReleaseAtCycles = [2];
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup555], (instrs WAIT)>;

def SPRWriteResGroup556 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [8, 6, 19, 63, 21, 15, 1, 10, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 144;
}
def : InstRW<[SPRWriteResGroup556], (instrs WRMSR)>;

def SPRWriteResGroup557 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06, SPRPort01, SPRPort05]> {
  let ReleaseAtCycles = [2, 1, 4, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 8;
}
def : InstRW<[SPRWriteResGroup557], (instrs WRPKRUr)>;

def SPRWriteResGroup558 : SchedWriteRes<[SPRPort00_01_05_06_10]> {
  let ReleaseAtCycles = [2];
  let Latency = 12;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup558, WriteRMW], (instregex "^XADD(16|32|64)rm$")>;

def SPRWriteResGroup559 : SchedWriteRes<[SPRPort00_01_05_06_10]> {
  let ReleaseAtCycles = [2];
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[SPRWriteResGroup559, WriteRMW], (instrs XADD8rm)>;

def SPRWriteResGroup560 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let ReleaseAtCycles = [4, 1];
  let Latency = 39;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup560, WriteRMW], (instregex "^XCHG(16|32)rm$")>;

def SPRWriteResGroup561 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let ReleaseAtCycles = [5, 1];
  let Latency = 39;
  let NumMicroOps = 6;
}
def : InstRW<[SPRWriteResGroup561, WriteRMW], (instrs XCHG64rm)>;

def SPRWriteResGroup562 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let ReleaseAtCycles = [4, 1];
  let Latency = 40;
  let NumMicroOps = 5;
}
def : InstRW<[SPRWriteResGroup562, WriteRMW], (instrs XCHG8rm)>;

def SPRWriteResGroup563 : SchedWriteRes<[SPRPort00, SPRPort00_01_05_06, SPRPort00_05, SPRPort01, SPRPort05, SPRPort06]> {
  let ReleaseAtCycles = [2, 4, 2, 1, 2, 4];
  let Latency = 17;
  let NumMicroOps = 15;
}
def : InstRW<[SPRWriteResGroup563], (instrs XCH_F)>;

def SPRWriteResGroup564 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01]> {
  let ReleaseAtCycles = [7, 3, 8, 5];
  let Latency = 4;
  let NumMicroOps = 23;
}
def : InstRW<[SPRWriteResGroup564], (instrs XGETBV)>;

def SPRWriteResGroup565 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort02_03_11]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup565], (instrs XLAT)>;

def SPRWriteResGroup566 : SchedWriteRes<[SPRPort01, SPRPort02_03, SPRPort02_03_11, SPRPort06]> {
  let ReleaseAtCycles = [1, 21, 1, 8];
  let Latency = 37;
  let NumMicroOps = 31;
}
def : InstRW<[SPRWriteResGroup566], (instregex "^XRSTOR((S|64)?)$")>;
def : InstRW<[SPRWriteResGroup566], (instrs XRSTORS64)>;

def SPRWriteResGroup567 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [14, 25, 44, 21, 21, 4, 1, 9, 1];
  let Latency = 42;
  let NumMicroOps = 140;
}
def : InstRW<[SPRWriteResGroup567], (instrs XSAVE)>;

def SPRWriteResGroup568 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [14, 25, 44, 21, 21, 4, 1, 9, 1];
  let Latency = 41;
  let NumMicroOps = 140;
}
def : InstRW<[SPRWriteResGroup568], (instrs XSAVE64)>;

def SPRWriteResGroup569 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 19, 36, 52, 23, 4, 2, 12, 2];
  let Latency = 42;
  let NumMicroOps = 151;
}
def : InstRW<[SPRWriteResGroup569], (instrs XSAVEC)>;

def SPRWriteResGroup570 : SchedWriteRes<[SPRPort00, SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [1, 19, 36, 53, 23, 4, 2, 12, 2];
  let Latency = 42;
  let NumMicroOps = 152;
}
def : InstRW<[SPRWriteResGroup570], (instrs XSAVEC64)>;

def SPRWriteResGroup571 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [25, 35, 52, 27, 4, 1, 10, 1];
  let Latency = 42;
  let NumMicroOps = 155;
}
def : InstRW<[SPRWriteResGroup571], (instrs XSAVEOPT)>;

def SPRWriteResGroup572 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [25, 35, 53, 27, 4, 1, 10, 1];
  let Latency = 42;
  let NumMicroOps = 156;
}
def : InstRW<[SPRWriteResGroup572], (instrs XSAVEOPT64)>;

def SPRWriteResGroup573 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [23, 32, 53, 29, 30, 4, 2, 9, 2];
  let Latency = 42;
  let NumMicroOps = 184;
}
def : InstRW<[SPRWriteResGroup573], (instrs XSAVES)>;

def SPRWriteResGroup574 : SchedWriteRes<[SPRPort00_01, SPRPort00_05, SPRPort00_06, SPRPort01, SPRPort01_05, SPRPort02_03_11, SPRPort04_09, SPRPort05, SPRPort07_08]> {
  let ReleaseAtCycles = [23, 33, 53, 29, 32, 4, 2, 8, 2];
  let Latency = 42;
  let NumMicroOps = 186;
}
def : InstRW<[SPRWriteResGroup574], (instrs XSAVES64)>;

def SPRWriteResGroup575 : SchedWriteRes<[SPRPort00_01_05, SPRPort00_01_05_06_10, SPRPort00_05_06, SPRPort00_06, SPRPort01, SPRPort01_05_10, SPRPort05]> {
  let ReleaseAtCycles = [4, 23, 2, 14, 8, 1, 2];
  let Latency = 5;
  let NumMicroOps = 54;
}
def : InstRW<[SPRWriteResGroup575], (instrs XSETBV)>;

def SPRWriteResGroup576 : SchedWriteRes<[SPRPort00_01_05_06_10, SPRPort00_06]> {
  let ReleaseAtCycles = [2, 1];
  let Latency = SapphireRapidsModel.MaxLatency;
  let NumMicroOps = 3;
}
def : InstRW<[SPRWriteResGroup576], (instrs XTEST)>;

}
