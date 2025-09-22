//==- AArch64SchedA53.td - Cortex-A53 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the ARM Cortex A53 processors.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the simpler per-operand machine model.
// This works with MachineScheduler. See MCSchedule.h for details.

// Cortex-A53 machine model for scheduling and other instruction cost heuristics.
def CortexA53Model : SchedMachineModel {
  let MicroOpBufferSize = 0; // Explicitly set to zero since A53 is in-order.
  let IssueWidth = 2;        // 2 micro-ops are dispatched per cycle.
  let LoadLatency = 3;       // Optimistic load latency assuming bypass.
                             // This is overriden by OperandCycles if the
                             // Itineraries are queried instead.
  let MispredictPenalty = 9; // Based on "Cortex-A53 Software Optimisation
                             // Specification - Instruction Timings"
                             // v 1.0 Spreadsheet
  let CompleteModel = 1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}


//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.

// Modeling each pipeline as a ProcResource using the BufferSize = 0 since
// Cortex-A53 is in-order.

def A53UnitALU    : ProcResource<2> { let BufferSize = 0; } // Int ALU
def A53UnitMAC    : ProcResource<1> { let BufferSize = 0; } // Int MAC
def A53UnitDiv    : ProcResource<1> { let BufferSize = 0; } // Int Division
def A53UnitLdSt   : ProcResource<1> { let BufferSize = 0; } // Load/Store
def A53UnitB      : ProcResource<1> { let BufferSize = 0; } // Branch
def A53UnitFPALU  : ProcResource<1> { let BufferSize = 0; } // FP ALU
def A53UnitFPMDS  : ProcResource<1> { let BufferSize = 0; } // FP Mult/Div/Sqrt


//===----------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types which both map the ProcResources and
// set the latency.

let SchedModel = CortexA53Model in {

// ALU - Despite having a full latency of 4, most of the ALU instructions can
//       forward a cycle earlier and then two cycles earlier in the case of a
//       shift-only instruction. These latencies will be incorrect when the
//       result cannot be forwarded, but modeling isn't rocket surgery.
def : WriteRes<WriteImm, [A53UnitALU]> { let Latency = 3; }
def : WriteRes<WriteI, [A53UnitALU]> { let Latency = 3; }
def : WriteRes<WriteISReg, [A53UnitALU]> { let Latency = 3; }
def : WriteRes<WriteIEReg, [A53UnitALU]> { let Latency = 3; }
def : WriteRes<WriteIS, [A53UnitALU]> { let Latency = 2; }
def : WriteRes<WriteExtr, [A53UnitALU]> { let Latency = 3; }

// MAC
def : WriteRes<WriteIM32, [A53UnitMAC]> { let Latency = 4; }
def : WriteRes<WriteIM64, [A53UnitMAC]> { let Latency = 4; }

// Div
def : WriteRes<WriteID32, [A53UnitDiv]> { let Latency = 4; }
def : WriteRes<WriteID64, [A53UnitDiv]> { let Latency = 4; }

// Load
def : WriteRes<WriteLD, [A53UnitLdSt]> { let Latency = 4; }
def : WriteRes<WriteLDIdx, [A53UnitLdSt]> { let Latency = 4; }
def : WriteRes<WriteLDHi, [A53UnitLdSt]> { let Latency = 4; }

// Vector Load - Vector loads take 1-5 cycles to issue. For the WriteVecLd
//               below, choosing the median of 3 which makes the latency 6.
//               May model this more carefully in the future. The remaining
//               A53WriteVLD# types represent the 1-5 cycle issues explicitly.
def : WriteRes<WriteVLD, [A53UnitLdSt]> { let Latency = 6;
                                          let ReleaseAtCycles = [3]; }
def A53WriteVLD1 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 4; }
def A53WriteVLD2 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 5;
                                                  let ReleaseAtCycles = [2]; }
def A53WriteVLD3 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 6;
                                                  let ReleaseAtCycles = [3]; }
def A53WriteVLD4 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 7;
                                                  let ReleaseAtCycles = [4]; }
def A53WriteVLD5 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 8;
                                                  let ReleaseAtCycles = [5]; }

// Pre/Post Indexing - Performed as part of address generation which is already
//                     accounted for in the WriteST* latencies below
def : WriteRes<WriteAdr, []> { let Latency = 0; }

// Store
def : WriteRes<WriteST, [A53UnitLdSt]> { let Latency = 4; }
def : WriteRes<WriteSTP, [A53UnitLdSt]> { let Latency = 4; }
def : WriteRes<WriteSTIdx, [A53UnitLdSt]> { let Latency = 4; }
def : WriteRes<WriteSTX, [A53UnitLdSt]> { let Latency = 4; }

// Vector Store - Similar to vector loads, can take 1-3 cycles to issue.
def : WriteRes<WriteVST, [A53UnitLdSt]> { let Latency = 5;
                                          let ReleaseAtCycles = [2];}
def A53WriteVST1 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 4; }
def A53WriteVST2 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 5;
                                                  let ReleaseAtCycles = [2]; }
def A53WriteVST3 : SchedWriteRes<[A53UnitLdSt]> { let Latency = 6;
                                                  let ReleaseAtCycles = [3]; }

def : WriteRes<WriteAtomic, []> { let Unsupported = 1; }

// Branch
def : WriteRes<WriteBr, [A53UnitB]>;
def : WriteRes<WriteBrReg, [A53UnitB]>;
def : WriteRes<WriteSys, [A53UnitB]>;
def : WriteRes<WriteBarrier, [A53UnitB]>;
def : WriteRes<WriteHint, [A53UnitB]>;

// FP ALU
def : WriteRes<WriteF, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCmp, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCvt, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCopy, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFImm, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteVd, [A53UnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteVq, [A53UnitFPALU]> { let Latency = 6; }

// FP Mul, Div, Sqrt
def : WriteRes<WriteFMul, [A53UnitFPMDS]> { let Latency = 6; }
def : WriteRes<WriteFDiv, [A53UnitFPMDS]> { let Latency = 33;
                                            let ReleaseAtCycles = [29]; }
def A53WriteFMAC : SchedWriteRes<[A53UnitFPMDS]> { let Latency = 10; }
def A53WriteFDivSP : SchedWriteRes<[A53UnitFPMDS]> { let Latency = 18;
                                                     let ReleaseAtCycles = [14]; }
def A53WriteFDivDP : SchedWriteRes<[A53UnitFPMDS]> { let Latency = 33;
                                                     let ReleaseAtCycles = [29]; }
def A53WriteFSqrtSP : SchedWriteRes<[A53UnitFPMDS]> { let Latency = 17;
                                                      let ReleaseAtCycles = [13]; }
def A53WriteFSqrtDP : SchedWriteRes<[A53UnitFPMDS]> { let Latency = 32;
                                                      let ReleaseAtCycles = [28]; }

//===----------------------------------------------------------------------===//
// Subtarget-specific SchedRead types.

// No forwarding for these reads.
def : ReadAdvance<ReadExtrHi, 0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadST, 0>;
def : ReadAdvance<ReadVLD, 0>;

// ALU - Most operands in the ALU pipes are not needed for two cycles. Shiftable
//       operands are needed one cycle later if and only if they are to be
//       shifted. Otherwise, they too are needed two cycles later. This same
//       ReadAdvance applies to Extended registers as well, even though there is
//       a separate SchedPredicate for them.
def : ReadAdvance<ReadI, 2, [WriteImm,WriteI,
                             WriteISReg, WriteIEReg,WriteIS,
                             WriteID32,WriteID64,
                             WriteIM32,WriteIM64]>;
def A53ReadShifted : SchedReadAdvance<1, [WriteImm,WriteI,
                                          WriteISReg, WriteIEReg,WriteIS,
                                          WriteID32,WriteID64,
                                          WriteIM32,WriteIM64]>;
def A53ReadNotShifted : SchedReadAdvance<2, [WriteImm,WriteI,
                                             WriteISReg, WriteIEReg,WriteIS,
                                             WriteID32,WriteID64,
                                             WriteIM32,WriteIM64]>;
def A53ReadISReg : SchedReadVariant<[
	SchedVar<RegShiftedPred, [A53ReadShifted]>,
	SchedVar<NoSchedPred, [A53ReadNotShifted]>]>;
def : SchedAlias<ReadISReg, A53ReadISReg>;

def A53ReadIEReg : SchedReadVariant<[
	SchedVar<RegExtendedPred, [A53ReadShifted]>,
	SchedVar<NoSchedPred, [A53ReadNotShifted]>]>;
def : SchedAlias<ReadIEReg, A53ReadIEReg>;

// MAC - Operands are generally needed one cycle later in the MAC pipe.
//       Accumulator operands are needed two cycles later.
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
def : InstRW<[WriteI], (instrs COPY)>;

//---
// Vector Loads
//---
def : InstRW<[A53WriteVLD1], (instregex "LD1i(8|16|32|64)$")>;
def : InstRW<[A53WriteVLD1], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD1], (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD2], (instregex "LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD3], (instregex "LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD4], (instregex "LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A53WriteVLD1], (instregex "LD1i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD1], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD1], (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD3], (instregex "LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD4], (instregex "LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[A53WriteVLD1], (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[A53WriteVLD1], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD2], (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[A53WriteVLD4], (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A53WriteVLD1], (instregex "LD2i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD1], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD4], (instregex "LD2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A53WriteVLD2], (instregex "LD3i(8|16|32|64)$")>;
def : InstRW<[A53WriteVLD2], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD4], (instregex "LD3Threev(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[A53WriteVLD3], (instregex "LD3Threev2d$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD3i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD4], (instregex "LD3Threev(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD3], (instregex "LD3Threev2d_POST$")>;

def : InstRW<[A53WriteVLD2], (instregex "LD4i(8|16|32|64)$")>;
def : InstRW<[A53WriteVLD2], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVLD5], (instregex "LD4Fourv(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[A53WriteVLD4], (instregex "LD4Fourv(2d)$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD4i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD2], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD5], (instregex "LD4Fourv(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVLD4], (instregex "LD4Fourv(2d)_POST$")>;

//---
// Vector Stores
//---
def : InstRW<[A53WriteVST1], (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[A53WriteVST1], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVST1], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVST2], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A53WriteVST2], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A53WriteVST1], (instregex "ST1i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST1], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST1], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[A53WriteVST1], (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[A53WriteVST1], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[A53WriteVST2], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, A53WriteVST1], (instregex "ST2i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST1], (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[A53WriteVST2], (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[A53WriteVST3], (instregex "ST3Threev(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[A53WriteVST2], (instregex "ST3Threev(2d)$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST3i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST3], (instregex "ST3Threev(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST3Threev(2d)_POST$")>;

def : InstRW<[A53WriteVST2], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[A53WriteVST3], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[A53WriteVST2], (instregex "ST4Fourv(2d)$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST4i(8|16|32|64)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST3], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[WriteAdr, A53WriteVST2], (instregex "ST4Fourv(2d)_POST$")>;

//---
// Floating Point MAC, DIV, SQRT
//---
def : InstRW<[A53WriteFMAC], (instregex "^FN?M(ADD|SUB).*")>;
def : InstRW<[A53WriteFMAC], (instregex "^FML(A|S).*")>;
def : InstRW<[A53WriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[A53WriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[A53WriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[A53WriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[A53WriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[A53WriteFSqrtDP], (instregex "^.*SQRT.*64$")>;

}
