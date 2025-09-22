//==- AArch64SchedCortexA55.td - ARM Cortex-A55 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the ARM Cortex-A55 processors. Note
// that this schedule is currently used as the default for -mcpu=generic. As a
// result, some of the modelling decision made do not precisely model the
// Cortex-A55, instead aiming to be a good compromise between different cpus.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the per-operand machine model.
// This works with MachineScheduler. See MCSchedModel.h for details.

// Cortex-A55 machine model for scheduling and other instruction cost heuristics.
def CortexA55Model : SchedMachineModel {
  let MicroOpBufferSize = 0;  // The Cortex-A55 is an in-order processor
  let IssueWidth = 2;         // It dual-issues under most circumstances
  let LoadLatency = 4;        // Cycles for loads to access the cache. The
                              // optimisation guide shows that most loads have
                              // a latency of 3, but some have a latency of 4
                              // or 5. Setting it 4 looked to be good trade-off.
  let MispredictPenalty = 8;  // A branch direction mispredict.
  let PostRAScheduler = 1;    // Enable PostRA scheduler pass.
  let CompleteModel = 0;      // Covers instructions applicable to Cortex-A55.

  list<Predicate> UnsupportedFeatures = [HasSVE, HasMTE];

  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.

// Modeling each pipeline as a ProcResource using the BufferSize = 0 since the
// Cortex-A55 is in-order.

def CortexA55UnitALU    : ProcResource<2> { let BufferSize = 0; } // Int ALU
def CortexA55UnitMAC    : ProcResource<1> { let BufferSize = 0; } // Int MAC, 64-bi wide
def CortexA55UnitDiv    : ProcResource<1> { let BufferSize = 0; } // Int Division, not pipelined
def CortexA55UnitLd     : ProcResource<1> { let BufferSize = 0; } // Load pipe
def CortexA55UnitSt     : ProcResource<1> { let BufferSize = 0; } // Store pipe
def CortexA55UnitB      : ProcResource<1> { let BufferSize = 0; } // Branch

// The FP DIV/SQRT instructions execute totally differently from the FP ALU
// instructions, which can mostly be dual-issued; that's why for now we model
// them with 2 resources.
def CortexA55UnitFPALU  : ProcResource<2> { let BufferSize = 0; } // FP ALU
def CortexA55UnitFPMAC  : ProcResource<2> { let BufferSize = 0; } // FP MAC
def CortexA55UnitFPDIV  : ProcResource<1> { let BufferSize = 0; } // FP Div/SQRT, 64/128

//===----------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types

let SchedModel = CortexA55Model in {

// These latencies are modeled without taking into account forwarding paths
// (the software optimisation guide lists latencies taking into account
// typical forwarding paths).
def : WriteRes<WriteImm, [CortexA55UnitALU]> { let Latency = 3; }    // MOVN, MOVZ
def : WriteRes<WriteI, [CortexA55UnitALU]> { let Latency = 3; }      // ALU
def : WriteRes<WriteISReg, [CortexA55UnitALU]> { let Latency = 3; }  // ALU of Shifted-Reg
def : WriteRes<WriteIEReg, [CortexA55UnitALU]> { let Latency = 3; }  // ALU of Extended-Reg
def : WriteRes<WriteExtr, [CortexA55UnitALU]> { let Latency = 3; }   // EXTR from a reg pair
def : WriteRes<WriteIS, [CortexA55UnitALU]> { let Latency = 3; }     // Shift/Scale

// MAC
def : WriteRes<WriteIM32, [CortexA55UnitMAC]> { let Latency = 4; }   // 32-bit Multiply
def : WriteRes<WriteIM64, [CortexA55UnitMAC]> { let Latency = 4; }   // 64-bit Multiply

// Div
def : WriteRes<WriteID32, [CortexA55UnitDiv]> {
  let Latency = 8; let ReleaseAtCycles = [8];
}
def : WriteRes<WriteID64, [CortexA55UnitDiv]> {
  let Latency = 8; let ReleaseAtCycles = [8];
}

// Load
def : WriteRes<WriteLD, [CortexA55UnitLd]> { let Latency = 3; }
def : WriteRes<WriteLDIdx, [CortexA55UnitLd]> { let Latency = 4; }
def : WriteRes<WriteLDHi, [CortexA55UnitLd]> { let Latency = 5; }

// Vector Load - Vector loads take 1-5 cycles to issue. For the WriteVecLd
//               below, choosing the median of 3 which makes the latency 6.
// An extra cycle is needed to get the swizzling right.
def : WriteRes<WriteVLD, [CortexA55UnitLd]> { let Latency = 6;
                                           let ReleaseAtCycles = [3]; }
def CortexA55WriteVLD1 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 4; }
def CortexA55WriteVLD1SI : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 4; let SingleIssue = 1; }
def CortexA55WriteVLD2 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 5;
                                                  let ReleaseAtCycles = [2]; }
def CortexA55WriteVLD3 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 6;
                                                  let ReleaseAtCycles = [3]; }
def CortexA55WriteVLD4 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 7;
                                                  let ReleaseAtCycles = [4]; }
def CortexA55WriteVLD5 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 8;
                                                  let ReleaseAtCycles = [5]; }
def CortexA55WriteVLD6 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 9;
                                                  let ReleaseAtCycles = [6]; }
def CortexA55WriteVLD7 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 10;
                                                  let ReleaseAtCycles = [7]; }
def CortexA55WriteVLD8 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 11;
                                                  let ReleaseAtCycles = [8]; }

def CortexA55WriteLDP1 : SchedWriteRes<[]> { let Latency = 4; }
def CortexA55WriteLDP2 : SchedWriteRes<[CortexA55UnitLd]> { let Latency = 5; }
def CortexA55WriteLDP4 : SchedWriteRes<[CortexA55UnitLd, CortexA55UnitLd, CortexA55UnitLd, CortexA55UnitLd, CortexA55UnitLd]> { let Latency = 6; }

// Pre/Post Indexing - Performed as part of address generation
def : WriteRes<WriteAdr, []> { let Latency = 0; }

// Store
let RetireOOO = 1 in {
def : WriteRes<WriteST, [CortexA55UnitSt]> { let Latency = 1; }
def : WriteRes<WriteSTP, [CortexA55UnitSt]> { let Latency = 1; }
def : WriteRes<WriteSTIdx, [CortexA55UnitSt]> { let Latency = 1; }
}
def : WriteRes<WriteSTX, [CortexA55UnitSt]> { let Latency = 4; }

// Vector Store - Similar to vector loads, can take 1-3 cycles to issue.
def : WriteRes<WriteVST, [CortexA55UnitSt]> { let Latency = 5;
                                          let ReleaseAtCycles = [2];}
def CortexA55WriteVST1 : SchedWriteRes<[CortexA55UnitSt]> { let Latency = 4; }
def CortexA55WriteVST2 : SchedWriteRes<[CortexA55UnitSt]> { let Latency = 5;
                                                  let ReleaseAtCycles = [2]; }
def CortexA55WriteVST3 : SchedWriteRes<[CortexA55UnitSt]> { let Latency = 6;
                                                  let ReleaseAtCycles = [3]; }
def CortexA55WriteVST4 : SchedWriteRes<[CortexA55UnitSt]> { let Latency = 5;
                                                  let ReleaseAtCycles = [4]; }

def : WriteRes<WriteAtomic, []> { let Unsupported = 1; }

// Branch
def : WriteRes<WriteBr, [CortexA55UnitB]>;
def : WriteRes<WriteBrReg, [CortexA55UnitB]>;
def : WriteRes<WriteSys, [CortexA55UnitB]>;
def : WriteRes<WriteBarrier, [CortexA55UnitB]>;
def : WriteRes<WriteHint, [CortexA55UnitB]>;

// FP ALU
//   As WriteF result is produced in F5 and it can be mostly forwarded
//   to consumer at F1, the effectively latency is set as 4.
def : WriteRes<WriteF, [CortexA55UnitFPALU]> { let Latency = 4; }
def : WriteRes<WriteFCmp, [CortexA55UnitFPALU]> { let Latency = 3; }
def : WriteRes<WriteFCvt, [CortexA55UnitFPALU]> { let Latency = 4; }
def : WriteRes<WriteFCopy, [CortexA55UnitFPALU]> { let Latency = 3; }
def : WriteRes<WriteFImm, [CortexA55UnitFPALU]> { let Latency = 3; }

// NEON
class CortexA55WriteVd<int n, ProcResourceKind res> : SchedWriteRes<[res]> {
  let Latency = n;
}
class CortexA55WriteVq<int n, ProcResourceKind res> : SchedWriteRes<[res, res]> {
  let Latency = n;
  let BeginGroup = 1;
}
def CortexA55WriteDotScVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteDotVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteDotVd_4 : CortexA55WriteVd<4, CortexA55UnitFPALU>;
def CortexA55WriteMlaLVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteMlaIxVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteMlaVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteMlaVd_4 : CortexA55WriteVd<4, CortexA55UnitFPALU>;
def CortexA55WriteAluVq_4 : CortexA55WriteVq<4, CortexA55UnitFPALU>;
def CortexA55WriteAluVd_3 : CortexA55WriteVd<3, CortexA55UnitFPALU>;
def CortexA55WriteAluVq_3 : CortexA55WriteVq<3, CortexA55UnitFPALU>;
def CortexA55WriteAluVd_2 : CortexA55WriteVd<2, CortexA55UnitFPALU>;
def CortexA55WriteAluVq_2 : CortexA55WriteVq<2, CortexA55UnitFPALU>;
def CortexA55WriteAluVd_1 : CortexA55WriteVd<1, CortexA55UnitFPALU>;
def CortexA55WriteAluVq_1 : CortexA55WriteVq<1, CortexA55UnitFPALU>;
def : SchedAlias<WriteVd, CortexA55WriteVd<4, CortexA55UnitFPALU>>;
def : SchedAlias<WriteVq, CortexA55WriteVq<4, CortexA55UnitFPALU>>;

// FP ALU specific new schedwrite definitions
def CortexA55WriteFPALU_F2 : SchedWriteRes<[CortexA55UnitFPALU]> { let Latency = 2;}
def CortexA55WriteFPALU_F3 : SchedWriteRes<[CortexA55UnitFPALU]> { let Latency = 3;}
def CortexA55WriteFPALU_F4 : SchedWriteRes<[CortexA55UnitFPALU]> { let Latency = 4;}
def CortexA55WriteFPALU_F5 : SchedWriteRes<[CortexA55UnitFPALU]> { let Latency = 5;}

// FP Mul, Div, Sqrt. Div/Sqrt are not pipelined
def : WriteRes<WriteFMul, [CortexA55UnitFPMAC]> { let Latency = 4; }

let RetireOOO = 1 in {
def : WriteRes<WriteFDiv, [CortexA55UnitFPDIV]> { let Latency = 22;
                                            let ReleaseAtCycles = [29]; }
def CortexA55WriteFMAC : SchedWriteRes<[CortexA55UnitFPMAC]> { let Latency = 4; }
def CortexA55WriteFDivHP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 8;
                                                     let ReleaseAtCycles = [5]; }
def CortexA55WriteFDivSP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 13;
                                                     let ReleaseAtCycles = [10]; }
def CortexA55WriteFDivDP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 22;
                                                     let ReleaseAtCycles = [19]; }
def CortexA55WriteFSqrtHP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 8;
                                                      let ReleaseAtCycles = [5]; }
def CortexA55WriteFSqrtSP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 12;
                                                      let ReleaseAtCycles = [9]; }
def CortexA55WriteFSqrtDP : SchedWriteRes<[CortexA55UnitFPDIV]> { let Latency = 22;
                                                      let ReleaseAtCycles = [19]; }
}
//===----------------------------------------------------------------------===//
// Subtarget-specific SchedRead types.

def : ReadAdvance<ReadVLD, 0>;
def : ReadAdvance<ReadExtrHi, 1>;
def : ReadAdvance<ReadAdrBase, 1>;
def : ReadAdvance<ReadST, 1>;

// ALU - ALU input operands are generally needed in EX1. An operand produced in
//       in say EX2 can be forwarded for consumption to ALU in EX1, thereby
//       allowing back-to-back ALU operations such as add. If an operand requires
//       a shift, it will, however, be required in ISS stage.
def : ReadAdvance<ReadI, 2, [WriteImm,WriteI,
                             WriteISReg, WriteIEReg,WriteIS,
                             WriteID32,WriteID64,
                             WriteIM32,WriteIM64]>;
// Shifted operand
def CortexA55ReadShifted : SchedReadAdvance<1, [WriteImm,WriteI,
                                          WriteISReg, WriteIEReg,WriteIS,
                                          WriteID32,WriteID64,
                                          WriteIM32,WriteIM64]>;
def CortexA55ReadNotShifted : SchedReadAdvance<2, [WriteImm,WriteI,
                                             WriteISReg, WriteIEReg,WriteIS,
                                             WriteID32,WriteID64,
                                             WriteIM32,WriteIM64]>;
def CortexA55ReadISReg : SchedReadVariant<[
        SchedVar<RegShiftedPred, [CortexA55ReadShifted]>,
        SchedVar<NoSchedPred, [CortexA55ReadNotShifted]>]>;
def : SchedAlias<ReadISReg, CortexA55ReadISReg>;

def CortexA55ReadIEReg : SchedReadVariant<[
        SchedVar<RegExtendedPred, [CortexA55ReadShifted]>,
        SchedVar<NoSchedPred, [CortexA55ReadNotShifted]>]>;
def : SchedAlias<ReadIEReg, CortexA55ReadIEReg>;

// MUL
def : ReadAdvance<ReadIM, 1, [WriteImm,WriteI,
                              WriteISReg, WriteIEReg,WriteIS,
                              WriteID32,WriteID64,
                              WriteIM32,WriteIM64]>;
def : ReadAdvance<ReadIMA, 2, [WriteImm,WriteI,
                               WriteISReg, WriteIEReg,WriteIS,
                               WriteID32,WriteID64,
                               WriteIM32,WriteIM64]>;

// Div
def : ReadAdvance<ReadID, 1, [WriteImm,WriteI,
                              WriteISReg, WriteIEReg,WriteIS,
                              WriteID32,WriteID64,
                              WriteIM32,WriteIM64]>;

//===----------------------------------------------------------------------===//
// Subtarget-specific InstRWs.

//---
// Miscellaneous
//---
def : InstRW<[CortexA55WriteVLD1SI,CortexA55WriteLDP1], (instregex "LDPS?Wi")>;
def : InstRW<[CortexA55WriteVLD1,CortexA55WriteLDP1], (instregex "LDPSi")>;
def : InstRW<[CortexA55WriteVLD1,CortexA55WriteLDP2], (instregex "LDP(X|D)i")>;
def : InstRW<[CortexA55WriteVLD1,CortexA55WriteLDP4], (instregex "LDPQi")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1SI,CortexA55WriteLDP1], (instregex "LDPS?W(pre|post)")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1,CortexA55WriteLDP1], (instregex "LDPS(pre|post)")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1,CortexA55WriteLDP2], (instregex "LDP(X|D)(pre|post)")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1,CortexA55WriteLDP4], (instregex "LDPQ(pre|post)")>;
def : InstRW<[WriteI], (instrs COPY)>;
//---
// Vector Loads - 64-bit per cycle
//---
//   1-element structures
def : InstRW<[CortexA55WriteVLD1], (instregex "LD1i(8|16|32|64)$")>;                // single element
def : InstRW<[CortexA55WriteVLD1], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>; // replicate
def : InstRW<[CortexA55WriteVLD1], (instregex "LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[CortexA55WriteVLD2], (instregex "LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVLD2], (instregex "LD1Twov(8b|4h|2s|1d)$")>; // multiple structures
def : InstRW<[CortexA55WriteVLD4], (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVLD3], (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[CortexA55WriteVLD6], (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVLD4], (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[CortexA55WriteVLD8], (instregex "LD1Fourv(16b|8h|4s|2d)$")>;

def : InstRW<[WriteAdr, CortexA55WriteVLD1], (instregex "LD1i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD1], (instregex "LD1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD1Onev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD4], (instregex "LD1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD3], (instregex "LD1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD6], (instregex "LD1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD4], (instregex "LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD8], (instregex "LD1Fourv(16b|8h|4s|2d)_POST$")>;

//    2-element structures
def : InstRW<[CortexA55WriteVLD2], (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVLD2], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVLD2], (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[CortexA55WriteVLD4], (instregex "LD2Twov(16b|8h|4s|2d)$")>;

def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD2i(8|16|32|64)(_POST)?$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)(_POST)?$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD2Twov(8b|4h|2s)(_POST)?$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD4], (instregex "LD2Twov(16b|8h|4s|2d)(_POST)?$")>;

//    3-element structures
def : InstRW<[CortexA55WriteVLD2], (instregex "LD3i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVLD2], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVLD3], (instregex "LD3Threev(8b|4h|2s|1d)$")>;
def : InstRW<[CortexA55WriteVLD6], (instregex "LD3Threev(16b|8h|4s|2d)$")>;

def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD3i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD3], (instregex "LD3Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD6], (instregex "LD3Threev(16b|8h|4s|2d)_POST$")>;

//    4-element structures
def : InstRW<[CortexA55WriteVLD2], (instregex "LD4i(8|16|32|64)$")>;                // load single 4-el structure to one lane of 4 regs.
def : InstRW<[CortexA55WriteVLD2], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>; // load single 4-el structure, replicate to all lanes of 4 regs.
def : InstRW<[CortexA55WriteVLD4], (instregex "LD4Fourv(8b|4h|2s|1d)$")>;           // load multiple 4-el structures to 4 regs.
def : InstRW<[CortexA55WriteVLD8], (instregex "LD4Fourv(16b|8h|4s|2d)$")>;

def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD4i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD2], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD4], (instregex "LD4Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVLD8], (instregex "LD4Fourv(16b|8h|4s|2d)_POST$")>;

//---
// Vector Stores
//---
def : InstRW<[CortexA55WriteVST1], (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVST1], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVST1], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVST2], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[CortexA55WriteVST4], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST1], (instregex "ST1i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST1], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST1], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST2], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST4], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[CortexA55WriteVST2], (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVST2], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[CortexA55WriteVST4], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST2], (instregex "ST2i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST2], (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST4], (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[CortexA55WriteVST2], (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVST4], (instregex "ST3Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST2], (instregex "ST3i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST4], (instregex "ST3Threev(8b|4h|2s|1d|2d|16b|8h|4s|4d)_POST$")>;

def : InstRW<[CortexA55WriteVST2], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[CortexA55WriteVST4], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST2], (instregex "ST4i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, CortexA55WriteVST4], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

//---
// Floating Point Conversions, MAC, DIV, SQRT
//---
def : InstRW<[CortexA55WriteFPALU_F2], (instregex "^DUP(v2i64|v4i32|v8i16|v16i8)")>;
def : InstRW<[CortexA55WriteFPALU_F2], (instregex "^XTN")>;
def : InstRW<[CortexA55WriteFPALU_F3], (instregex "^FCVT[ALMNPZ][SU](S|U)?(W|X)")>;
def : InstRW<[CortexA55WriteFPALU_F4], (instregex "^FCVT(X)?[ALMNPXZ](S|U|N)?v")>;

def : InstRW<[CortexA55WriteFPALU_F4], (instregex "^(S|U)CVTF(S|U)(W|X)(H|S|D)")>;
def : InstRW<[CortexA55WriteFPALU_F4], (instregex "^(S|U)CVTF(h|s|d)")>;
def : InstRW<[CortexA55WriteFPALU_F4], (instregex "^(S|U)CVTFv")>;

def : InstRW<[CortexA55WriteFMAC], (instregex "^FN?M(ADD|SUB).*")>;
def : InstRW<[CortexA55WriteFMAC], (instregex "^FML(A|S).*")>;
def : InstRW<[CortexA55WriteFDivHP], (instrs FDIVHrr)>;
def : InstRW<[CortexA55WriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[CortexA55WriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[CortexA55WriteFDivHP], (instregex "^FDIVv.*16$")>;
def : InstRW<[CortexA55WriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[CortexA55WriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[CortexA55WriteFSqrtHP], (instregex "^.*SQRT.*16$")>;
def : InstRW<[CortexA55WriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[CortexA55WriteFSqrtDP], (instregex "^.*SQRT.*64$")>;

// 4.15. Advanced SIMD integer instructions
// ASIMD absolute diff
def : InstRW<[CortexA55WriteAluVd_3], (instregex "[SU]ABDv(2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "[SU]ABDv(16i8|4i32|8i16)")>;
// ASIMD absolute diff accum
def : InstRW<[CortexA55WriteAluVq_4], (instregex "[SU]ABAL?v")>;
// ASIMD absolute diff long
def : InstRW<[CortexA55WriteAluVq_3], (instregex "[SU]ABDLv")>;
// ASIMD arith #1
def : InstRW<[CortexA55WriteAluVd_2], (instregex "(ADD|SUB|NEG)v(1i64|2i32|4i16|8i8)",
  "[SU]R?HADDv(2i32|4i16|8i8)", "[SU]HSUBv(2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_2], (instregex "(ADD|SUB|NEG)v(2i64|4i32|8i16|16i8)",
  "[SU]R?HADDv(8i16|4i32|16i8)", "[SU]HSUBv(8i16|4i32|16i8)")>;
// ASIMD arith #2
def : InstRW<[CortexA55WriteAluVd_3], (instregex "ABSv(1i64|2i32|4i16|8i8)$",
  "[SU]ADDLPv(2i32_v1i64|4i16_v2i32|8i8_v4i16)$", 
  "([SU]QADD|[SU]QSUB|SQNEG|SUQADD|USQADD)v(1i16|1i32|1i64|1i8|2i32|4i16|8i8)$",
  "ADDPv(2i32|4i16|8i8)$")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "ABSv(2i64|4i32|8i16|16i8)$",
  "[SU]ADDLPv(16i8_v8i16|4i32_v2i64|8i16_v4i32)$", 
  "([SU]QADD|[SU]QSUB|SQNEG|SUQADD|USQADD)v(16i8|2i64|4i32|8i16)$",
  "ADDPv(16i8|2i64|4i32|8i16)$")>;
// ASIMD arith #3
def : InstRW<[CortexA55WriteAluVq_3], (instregex  "SADDLv", "UADDLv", "SADDWv",
  "UADDWv", "SSUBLv", "USUBLv", "SSUBWv", "USUBWv", "ADDHNv", "SUBHNv")>;
// ASIMD arith #5
def : InstRW<[CortexA55WriteAluVq_4], (instregex "RADDHNv", "RSUBHNv")>;
// ASIMD arith, reduce
def : InstRW<[CortexA55WriteAluVq_3], (instregex  "ADDVv", "SADDLVv", "UADDLVv")>;
// ASIMD compare #1
def : InstRW<[CortexA55WriteAluVd_2], (instregex "CM(EQ|GE|GT|HI|HS|LE|LT)v(1i64|2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_2], (instregex "CM(EQ|GE|GT|HI|HS|LE|LT)v(2i64|4i32|8i16|16i8)")>;
// ASIMD compare #2
def : InstRW<[CortexA55WriteAluVd_3], (instregex "CMTSTv(1i64|2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "CMTSTv(2i64|4i32|8i16|16i8)")>;
// ASIMD logical $1
def : InstRW<[CortexA55WriteAluVd_1], (instregex "(AND|EOR|NOT|ORN)v8i8",
  "(ORR|BIC)v(2i32|4i16|8i8)$", "MVNIv(2i|2s|4i16)")>;
def : InstRW<[CortexA55WriteAluVq_1], (instregex "(AND|EOR|NOT|ORN)v16i8",
  "(ORR|BIC)v(16i8|4i32|8i16)$", "MVNIv(4i32|4s|8i16)")>;
// ASIMD max/min, basic
def : InstRW<[CortexA55WriteAluVd_2], (instregex "[SU](MIN|MAX)P?v(2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_2], (instregex "[SU](MIN|MAX)P?v(16i8|4i132|8i16)")>;
// SIMD max/min, reduce
def : InstRW<[CortexA55WriteAluVq_4], (instregex "[SU](MAX|MIN)Vv")>;
// ASIMD multiply, by element
def : InstRW<[CortexA55WriteAluVq_4], (instregex "MULv(2i32|4i16|4i32|8i16)_indexed$",
  "SQR?DMULHv(1i16|1i32|2i32|4i16|4i32|8i16)_indexed$")>;
// ASIMD multiply
def : InstRW<[CortexA55WriteAluVd_3], (instrs PMULv8i8)>;
def : InstRW<[CortexA55WriteAluVq_3], (instrs PMULv16i8)>;
// ASIMD multiply accumulate
def : InstRW<[CortexA55WriteMlaVd_4], (instregex "ML[AS]v(2i32|4i16|8i8)$")>;
def : InstRW<[CortexA55WriteMlaVq_4], (instregex "ML[AS]v(16i8|4i32|8i16)$")>;
def : InstRW<[CortexA55WriteMlaIxVq_4], (instregex "ML[AS]v(2i32|4i16|4i32|8i16)_indexed$")>;
// ASIMD multiply accumulate half
def : InstRW<[CortexA55WriteAluVq_4], (instregex "SQRDML[AS]H[vi]")>;
// ASIMD multiply accumulate long
def : InstRW<[CortexA55WriteMlaLVq_4], (instregex "[SU]ML[AS]Lv")>;
// ASIMD multiply accumulate long #2
def : InstRW<[CortexA55WriteAluVq_4], (instregex "SQDML[AS]L[iv]")>;
// ASIMD dot product
def : InstRW<[CortexA55WriteDotVd_4], (instregex "[SU]DOTv8i8")>;
def : InstRW<[CortexA55WriteDotVq_4], (instregex "[SU]DOTv16i8")>;
// ASIMD dot product, by scalar
def : InstRW<[CortexA55WriteDotScVq_4], (instregex "[SU]DOTlanev")>;
// ASIMD multiply long
def : InstRW<[CortexA55WriteAluVq_4], (instregex "[SU]MULLv", "SQDMULL[iv]")>;
// ASIMD polynomial (8x8) multiply long
def : InstRW<[CortexA55WriteAluVq_3], (instrs PMULLv8i8, PMULLv16i8)>;
// ASIMD pairwise add and accumulate
def : InstRW<[CortexA55WriteAluVq_4], (instregex "[SU]ADALPv")>;
// ASIMD shift accumulate
def : InstRW<[CortexA55WriteAluVd_3], (instregex "[SU]SRA(d|v2i32|v4i16|v8i8)")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "[SU]SRAv(16i8|2i64|4i32|8i16)")>;
// ASIMD shift accumulate #2
def : InstRW<[CortexA55WriteAluVq_4], (instregex "[SU]RSRA[vd]")>;
// ASIMD shift by immed
def : InstRW<[CortexA55WriteAluVd_2], (instregex "SHLd$", "SHLv",
  "SLId$", "SRId$", "[SU]SHR[vd]", "SHRNv")>;
// ASIMD shift by immed
// SXTL and UXTL are aliases for SHLL
def : InstRW<[CortexA55WriteAluVq_2], (instregex "[US]?SHLLv")>;
// ASIMD shift by immed #2
def : InstRW<[CortexA55WriteAluVd_3], (instregex "[SU]RSHR(d|v2i32|v4i16|v8i8)",
  "RSHRNv(2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "[SU]RSHRv(16i8|2i64|4i32|8i16)",
  "RSHRNv(16i8|4i32|8i16)")>;
// ASIMD shift by register
def : InstRW<[CortexA55WriteAluVd_2], (instregex "[SU]SHLv(1i64|2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_2], (instregex "[SU]SHLv(2i64|4i32|8i16|16i8)")>;
// ASIMD shift by register #2
def : InstRW<[CortexA55WriteAluVd_3], (instregex "[SU]RSHLv(1i64|2i32|4i16|8i8)")>;
def : InstRW<[CortexA55WriteAluVq_3], (instregex "[SU]RSHLv(2i64|4i32|8i16|16i8)")>;

}
