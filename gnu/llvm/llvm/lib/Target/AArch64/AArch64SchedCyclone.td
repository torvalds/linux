//=- AArch64SchedCyclone.td - Cyclone Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for AArch64 Cyclone to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def CycloneModel : SchedMachineModel {
  let IssueWidth = 6; // 6 micro-ops are dispatched per cycle.
  let MicroOpBufferSize = 192; // Based on the reorder buffer.
  let LoadLatency = 4; // Optimistic load latency.
  let MispredictPenalty = 16; // 14-19 cycles are typical.
  let CompleteModel = 1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Cyclone.

// 4 integer pipes
def CyUnitI : ProcResource<4> {
  let BufferSize = 48;
}

// 2 branch units: I[0..1]
def CyUnitB : ProcResource<2> {
  let Super  = CyUnitI;
  let BufferSize = 24;
}

// 1 indirect-branch unit: I[0]
def CyUnitBR : ProcResource<1> {
  let Super  = CyUnitB;
}

// 2 shifter pipes: I[2..3]
// When an instruction consumes a CyUnitIS, it also consumes a CyUnitI
def CyUnitIS : ProcResource<2> {
  let Super = CyUnitI;
  let BufferSize = 24;
}

// 1 mul pipe: I[0]
def CyUnitIM : ProcResource<1> {
  let Super = CyUnitBR;
  let BufferSize = 32;
}

// 1 div pipe: I[1]
def CyUnitID : ProcResource<1> {
  let Super = CyUnitB;
  let BufferSize = 16;
}

// 1 integer division unit. This is driven by the ID pipe, but only
// consumes the pipe for one cycle at issue and another cycle at writeback.
def CyUnitIntDiv : ProcResource<1>;

// 2 ld/st pipes.
def CyUnitLS : ProcResource<2> {
  let BufferSize = 28;
}

// 3 fp/vector pipes.
def CyUnitV : ProcResource<3> {
  let BufferSize = 48;
}
// 2 fp/vector arithmetic and multiply pipes: V[0-1]
def CyUnitVM : ProcResource<2> {
  let Super = CyUnitV;
  let BufferSize = 32;
}
// 1 fp/vector division/sqrt pipe: V[2]
def CyUnitVD : ProcResource<1> {
  let Super = CyUnitV;
  let BufferSize = 16;
}
// 1 fp compare pipe: V[0]
def CyUnitVC : ProcResource<1> {
  let Super = CyUnitVM;
  let BufferSize = 16;
}

// 2 fp division/square-root units.  These are driven by the VD pipe,
// but only consume the pipe for one cycle at issue and a cycle at writeback.
def CyUnitFloatDiv : ProcResource<2>;

//===----------------------------------------------------------------------===//
// Define scheduler read/write resources and latency on Cyclone.
// This mirrors sections 7.7-7.9 of the Tuning Guide v1.0.1.

let SchedModel = CycloneModel in {

//---
// 7.8.1. Moves
//---

// A single nop micro-op (uX).
def WriteX : SchedWriteRes<[]> { let Latency = 0; }

// Move zero is a register rename (to machine register zero).
// The move is replaced by a single nop micro-op.
// MOVZ Rd, #0
// AND Rd, Rzr, #imm
def WriteZPred : SchedPredicate<[{TII->isGPRZero(*MI)}]>;
def WriteImmZ  : SchedWriteVariant<[
                   SchedVar<WriteZPred, [WriteX]>,
                   SchedVar<NoSchedPred, [WriteImm]>]>;
def : InstRW<[WriteImmZ], (instrs MOVZWi,MOVZXi,ANDWri,ANDXri)>;

// Move GPR is a register rename and single nop micro-op.
// ORR Xd, XZR, Xm
// ADD Xd, Xn, #0
def WriteIMovPred : SchedPredicate<[{TII->isGPRCopy(*MI)}]>;
def WriteVMovPred : SchedPredicate<[{TII->isFPRCopy(*MI)}]>;
def WriteMov      : SchedWriteVariant<[
                      SchedVar<WriteIMovPred, [WriteX]>,
                      SchedVar<WriteVMovPred, [WriteX]>,
                      SchedVar<NoSchedPred,   [WriteI]>]>;
def : InstRW<[WriteMov], (instrs COPY,ORRXrr,ADDXrr)>;

// Move non-zero immediate is an integer ALU op.
// MOVN,MOVZ,MOVK
def : WriteRes<WriteImm, [CyUnitI]>;

//---
// 7.8.2-7.8.5. Arithmetic and Logical, Comparison, Conditional,
//              Shifts and Bitfield Operations
//---

// ADR,ADRP
// ADD(S)ri,SUB(S)ri,AND(S)ri,EORri,ORRri
// ADD(S)rr,SUB(S)rr,AND(S)rr,BIC(S)rr,EONrr,EORrr,ORNrr,ORRrr
// ADC(S),SBC(S)
// Aliases: CMN, CMP, TST
//
// Conditional operations.
// CCMNi,CCMPi,CCMNr,CCMPr,
// CSEL,CSINC,CSINV,CSNEG
//
// Bit counting and reversal operations.
// CLS,CLZ,RBIT,REV,REV16,REV32
def : WriteRes<WriteI, [CyUnitI]>;

// ADD with shifted register operand is a single micro-op that
// consumes a shift pipeline for two cycles.
// ADD(S)rs,SUB(S)rs,AND(S)rs,BIC(S)rs,EONrs,EORrs,ORNrs,ORRrs
// EXAMPLE: ADDrs Xn, Xm LSL #imm
def : WriteRes<WriteISReg, [CyUnitIS]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}

// ADD with extended register operand is the same as shifted reg operand.
// ADD(S)re,SUB(S)re
// EXAMPLE: ADDXre Xn, Xm, UXTB #1
def : WriteRes<WriteIEReg, [CyUnitIS]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}

// Variable shift and bitfield operations.
// ASRV,LSLV,LSRV,RORV,BFM,SBFM,UBFM
def : WriteRes<WriteIS, [CyUnitIS]>;

// EXTR Shifts a pair of registers and requires two micro-ops.
// The second micro-op is delayed, as modeled by ReadExtrHi.
// EXTR Xn, Xm, #imm
def : WriteRes<WriteExtr, [CyUnitIS, CyUnitIS]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// EXTR's first register read is delayed by one cycle, effectively
// shortening its writer's latency.
// EXTR Xn, Xm, #imm
def : ReadAdvance<ReadExtrHi, 1>;

//---
// 7.8.6. Multiplies
//---

// MUL/MNEG are aliases for MADD/MSUB.
// MADDW,MSUBW,SMADDL,SMSUBL,UMADDL,UMSUBL
def : WriteRes<WriteIM32, [CyUnitIM]> {
  let Latency = 4;
}
// MADDX,MSUBX,SMULH,UMULH
def : WriteRes<WriteIM64, [CyUnitIM]> {
  let Latency = 5;
}

//---
// 7.8.7. Divide
//---

// 32-bit divide takes 7-13 cycles. 10 cycles covers a 20-bit quotient.
// The ID pipe is consumed for 2 cycles: issue and writeback.
// SDIVW,UDIVW
def : WriteRes<WriteID32, [CyUnitID, CyUnitIntDiv]> {
  let Latency = 10;
  let ReleaseAtCycles = [2, 10];
}
// 64-bit divide takes 7-21 cycles. 13 cycles covers a 32-bit quotient.
// The ID pipe is consumed for 2 cycles: issue and writeback.
// SDIVX,UDIVX
def : WriteRes<WriteID64, [CyUnitID, CyUnitIntDiv]> {
  let Latency = 13;
  let ReleaseAtCycles = [2, 13];
}

//---
// 7.8.8,7.8.10. Load/Store, single element
//---

// Integer loads take 4 cycles and use one LS unit for one cycle.
def : WriteRes<WriteLD, [CyUnitLS]> {
  let Latency = 4;
}

// Store-load forwarding is 4 cycles.
//
// Note: The store-exclusive sequence incorporates this
// latency. However, general heuristics should not model the
// dependence between a store and subsequent may-alias load because
// hardware speculation works.
def : WriteRes<WriteST, [CyUnitLS]> {
  let Latency = 4;
}

// Load from base address plus an optionally scaled register offset.
// Rt latency is latency WriteIS + WriteLD.
// EXAMPLE: LDR Xn, Xm [, lsl 3]
def CyWriteLDIdx : SchedWriteVariant<[
  SchedVar<ScaledIdxPred, [WriteIS, WriteLD]>, // Load from scaled register.
  SchedVar<NoSchedPred,   [WriteLD]>]>;        // Load from register offset.
def : SchedAlias<WriteLDIdx, CyWriteLDIdx>;    // Map AArch64->Cyclone type.

// EXAMPLE: STR Xn, Xm [, lsl 3]
def CyWriteSTIdx : SchedWriteVariant<[
  SchedVar<ScaledIdxPred, [WriteIS, WriteST]>, // Store to scaled register.
  SchedVar<NoSchedPred,   [WriteST]>]>;        // Store to register offset.
def : SchedAlias<WriteSTIdx, CyWriteSTIdx>;    // Map AArch64->Cyclone type.

// Read the (unshifted) base register Xn in the second micro-op one cycle later.
// EXAMPLE: LDR Xn, Xm [, lsl 3]
def ReadBaseRS : SchedReadAdvance<1>;
def CyReadAdrBase : SchedReadVariant<[
  SchedVar<ScaledIdxPred, [ReadBaseRS]>, // Read base reg after shifting offset.
  SchedVar<NoSchedPred,   [ReadDefault]>]>;   // Read base reg with no shift.
def : SchedAlias<ReadAdrBase, CyReadAdrBase>; // Map AArch64->Cyclone type.
def : ReadAdvance<ReadST, 0>;

//---
// 7.8.9,7.8.11. Load/Store, paired
//---

// Address pre/post increment is a simple ALU op with one cycle latency.
def : WriteRes<WriteAdr, [CyUnitI]>;

// LDP high register write is fused with the load, but a nop micro-op remains.
def : WriteRes<WriteLDHi, []> {
  let Latency = 4;
}

// STP is a vector op and store, except for QQ, which is just two stores.
def : SchedAlias<WriteSTP, WriteVSTShuffle>;
def : InstRW<[WriteST, WriteST], (instrs STPQi)>;

//---
// 7.8.13. Branches
//---

// Branches take a single micro-op.
// The misprediction penalty is defined as a SchedMachineModel property.
def : WriteRes<WriteBr,    [CyUnitB]>  {let Latency = 0;}
def : WriteRes<WriteBrReg, [CyUnitBR]> {let Latency = 0;}

//---
// 7.8.14. Never-issued Instructions, Barrier and Hint Operations
//---

// NOP,SEV,SEVL,WFE,WFI,YIELD
def : WriteRes<WriteHint, []> {let Latency = 0;}
// ISB
def : InstRW<[WriteI], (instrs ISB)>;
// SLREX,DMB,DSB
def : WriteRes<WriteBarrier, [CyUnitLS]>;

// System instructions get an invalid latency because the latency of
// other operations across them is meaningless.
def : WriteRes<WriteSys, []> {let Latency = -1;}

//===----------------------------------------------------------------------===//
// 7.9 Vector Unit Instructions

// Simple vector operations take 2 cycles.
def : WriteRes<WriteVd, [CyUnitV]> {let Latency = 2;}
def : WriteRes<WriteVq, [CyUnitV]> {let Latency = 2;}

// Define some longer latency vector op types for Cyclone.
def CyWriteV3 : SchedWriteRes<[CyUnitV]> {let Latency = 3;}
def CyWriteV4 : SchedWriteRes<[CyUnitV]> {let Latency = 4;}
def CyWriteV5 : SchedWriteRes<[CyUnitV]> {let Latency = 5;}
def CyWriteV6 : SchedWriteRes<[CyUnitV]> {let Latency = 6;}

// Simple floating-point operations take 2 cycles.
def : WriteRes<WriteF, [CyUnitV]> {let Latency = 2;}

//---
// 7.9.1 Vector Moves
//---

// TODO: Add Cyclone-specific zero-cycle zeros. LLVM currently
// generates expensive int-float conversion instead:
// FMOVDi Dd, #0.0
// FMOVv2f64ns Vd.2d, #0.0

// FMOVSi,FMOVDi
def : WriteRes<WriteFImm, [CyUnitV]> {let Latency = 2;}

// MOVI,MVNI are WriteV
// FMOVv2f32ns,FMOVv2f64ns,FMOVv4f32ns are WriteV

// Move FPR is a register rename and single nop micro-op.
// ORR.16b Vd,Vn,Vn
// COPY is handled above in the WriteMov Variant.
def WriteVMov    : SchedWriteVariant<[
                     SchedVar<WriteVMovPred, [WriteX]>,
                     SchedVar<NoSchedPred,   [WriteVq]>]>;
def : InstRW<[WriteVMov], (instrs ORRv16i8)>;

// FMOVSr,FMOVDr are WriteF.

// MOV V,V is a WriteV.

// CPY D,V[x] is a WriteV

// INS V[x],V[y] is a WriteV.

// FMOVWSr,FMOVXDr,FMOVXDHighr
def : WriteRes<WriteFCopy, [CyUnitLS]> {
  let Latency = 5;
}

// FMOVSWr,FMOVDXr
def : InstRW<[WriteLD], (instrs FMOVSWr,FMOVDXr,FMOVDXHighr)>;

// INS V[x],R
def CyWriteCopyToFPR : WriteSequence<[WriteVLD, WriteVq]>;
def : InstRW<[CyWriteCopyToFPR], (instregex "INSv")>;

// SMOV,UMOV R,V[x]
def CyWriteCopyToGPR : WriteSequence<[WriteLD, WriteI]>;
def : InstRW<[CyWriteCopyToGPR], (instregex "SMOVv","UMOVv")>;

// DUP V,R
def : InstRW<[CyWriteCopyToFPR], (instregex "DUPv")>;

// DUP V,V[x] is a WriteV.

//---
// 7.9.2 Integer Arithmetic, Logical, and Comparisons
//---

// BIC,ORR V,#imm are WriteV

def : InstRW<[CyWriteV3], (instregex "ABSv")>;

// MVN,NEG,NOT are WriteV

def : InstRW<[CyWriteV3], (instregex "SQABSv","SQNEGv")>;

// ADDP is a WriteV.
def CyWriteVADDLP : SchedWriteRes<[CyUnitV]> {let Latency = 2;}
def : InstRW<[CyWriteVADDLP], (instregex "SADDLPv","UADDLPv")>;

def : InstRW<[CyWriteV3],
             (instregex "ADDVv","SMAXVv","UMAXVv","SMINVv","UMINVv")>;

def : InstRW<[CyWriteV3], (instregex "SADDLV","UADDLV")>;

// ADD,SUB are WriteV

// Forward declare.
def CyWriteVABD : SchedWriteRes<[CyUnitV]> {let Latency = 3;}

// Add/Diff and accumulate uses the vector multiply unit.
def CyWriteVAccum : SchedWriteRes<[CyUnitVM]> {let Latency = 3;}
def CyReadVAccum  : SchedReadAdvance<1,
                    [CyWriteVAccum, CyWriteVADDLP, CyWriteVABD]>;

def : InstRW<[CyWriteVAccum, CyReadVAccum],
             (instregex "SADALP","UADALP")>;

def : InstRW<[CyWriteVAccum, CyReadVAccum],
             (instregex "SABAv","UABAv","SABALv","UABALv")>;

def : InstRW<[CyWriteV3], (instregex "SQADDv","SQSUBv","UQADDv","UQSUBv")>;

def : InstRW<[CyWriteV3], (instregex "SUQADDv","USQADDv")>;

def : InstRW<[CyWriteV4], (instregex "ADDHNv","RADDHNv", "RSUBHNv", "SUBHNv")>;

// WriteV includes:
// AND,BIC,CMTST,EOR,ORN,ORR
// ADDP
// SHADD,SHSUB,SRHADD,UHADD,UHSUB,URHADD
// SADDL,SSUBL,UADDL,USUBL
// SADDW,SSUBW,UADDW,USUBW

def : InstRW<[CyWriteV3], (instregex "CMEQv","CMGEv","CMGTv",
                                     "CMLEv","CMLTv",
                                     "CMHIv","CMHSv")>;

def : InstRW<[CyWriteV3], (instregex "SMAXv","SMINv","UMAXv","UMINv",
                                     "SMAXPv","SMINPv","UMAXPv","UMINPv")>;

def : InstRW<[CyWriteVABD], (instregex "SABDv","UABDv",
                                       "SABDLv","UABDLv")>;

//---
// 7.9.3 Floating Point Arithmetic and Comparisons
//---

// FABS,FNEG are WriteF

def : InstRW<[CyWriteV4], (instrs FADDPv2i32p)>;
def : InstRW<[CyWriteV5], (instrs FADDPv2i64p)>;

def : InstRW<[CyWriteV3], (instregex "FMAXPv2i","FMAXNMPv2i",
                                     "FMINPv2i","FMINNMPv2i")>;

def : InstRW<[CyWriteV4], (instregex "FMAXVv","FMAXNMVv","FMINVv","FMINNMVv")>;

def : InstRW<[CyWriteV4], (instrs FADDSrr,FADDv2f32,FADDv4f32,
                                  FSUBSrr,FSUBv2f32,FSUBv4f32,
                                  FADDPv2f32,FADDPv4f32,
                                  FABD32,FABDv2f32,FABDv4f32)>;
def : InstRW<[CyWriteV5], (instrs FADDDrr,FADDv2f64,
                                  FSUBDrr,FSUBv2f64,
                                  FADDPv2f64,
                                  FABD64,FABDv2f64)>;

def : InstRW<[CyWriteV3], (instregex "FCMEQ","FCMGT","FCMLE","FCMLT")>;

def : InstRW<[CyWriteV3], (instregex "FACGE","FACGT",
                                     "FMAXS","FMAXD","FMAXv",
                                     "FMINS","FMIND","FMINv",
                                     "FMAXNMS","FMAXNMD","FMAXNMv",
                                     "FMINNMS","FMINNMD","FMINNMv",
                                     "FMAXPv2f","FMAXPv4f",
                                     "FMINPv2f","FMINPv4f",
                                     "FMAXNMPv2f","FMAXNMPv4f",
                                     "FMINNMPv2f","FMINNMPv4f")>;

// FCMP,FCMPE,FCCMP,FCCMPE
def : WriteRes<WriteFCmp, [CyUnitVC]> {let Latency = 4;}

// FCSEL is a WriteF.

//---
// 7.9.4 Shifts and Bitfield Operations
//---

// SHL is a WriteV

def CyWriteVSHR : SchedWriteRes<[CyUnitV]> {let Latency = 2;}
def : InstRW<[CyWriteVSHR], (instregex "SSHRv","USHRv")>;

def CyWriteVSRSHR : SchedWriteRes<[CyUnitV]> {let Latency = 3;}
def : InstRW<[CyWriteVSRSHR], (instregex "SRSHRv","URSHRv")>;

// Shift and accumulate uses the vector multiply unit.
def CyWriteVShiftAcc : SchedWriteRes<[CyUnitVM]> {let Latency = 3;}
def CyReadVShiftAcc  : SchedReadAdvance<1,
                        [CyWriteVShiftAcc, CyWriteVSHR, CyWriteVSRSHR]>;
def : InstRW<[CyWriteVShiftAcc, CyReadVShiftAcc],
             (instregex "SRSRAv","SSRAv","URSRAv","USRAv")>;

// SSHL,USHL are WriteV.

def : InstRW<[CyWriteV3], (instregex "SRSHLv","URSHLv")>;

// SQSHL,SQSHLU,UQSHL are WriteV.

def : InstRW<[CyWriteV3], (instregex "SQRSHLv","UQRSHLv")>;

// WriteV includes:
// SHLL,SSHLL,USHLL
// SLI,SRI
// BIF,BIT,BSL,BSP
// EXT
// CLS,CLZ,CNT,RBIT,REV16,REV32,REV64,XTN
// XTN2

def : InstRW<[CyWriteV4],
             (instregex "RSHRNv","SHRNv",
                        "SQRSHRNv","SQRSHRUNv","SQSHRNv","SQSHRUNv",
                        "UQRSHRNv","UQSHRNv","SQXTNv","SQXTUNv","UQXTNv")>;

//---
// 7.9.5 Multiplication
//---

def CyWriteVMul : SchedWriteRes<[CyUnitVM]> { let Latency = 4;}
def : InstRW<[CyWriteVMul], (instregex "MULv","SMULLv","UMULLv",
                             "SQDMULLv","SQDMULHv","SQRDMULHv")>;

// FMUL,FMULX,FNMUL default to WriteFMul.
def : WriteRes<WriteFMul, [CyUnitVM]> { let Latency = 4;}

def CyWriteV64Mul : SchedWriteRes<[CyUnitVM]> { let Latency = 5;}
def : InstRW<[CyWriteV64Mul], (instrs FMULDrr,FMULv2f64,FMULv2i64_indexed,
                               FNMULDrr,FMULX64,FMULXv2f64,FMULXv2i64_indexed)>;

def CyReadVMulAcc : SchedReadAdvance<1, [CyWriteVMul, CyWriteV64Mul]>;
def : InstRW<[CyWriteVMul, CyReadVMulAcc],
             (instregex "MLA","MLS","SMLAL","SMLSL","UMLAL","UMLSL",
              "SQDMLAL","SQDMLSL")>;

def CyWriteSMul : SchedWriteRes<[CyUnitVM]> { let Latency = 8;}
def CyWriteDMul : SchedWriteRes<[CyUnitVM]> { let Latency = 10;}
def CyReadSMul : SchedReadAdvance<4, [CyWriteSMul]>;
def CyReadDMul : SchedReadAdvance<5, [CyWriteDMul]>;

def : InstRW<[CyWriteSMul, CyReadSMul],
             (instrs FMADDSrrr,FMSUBSrrr,FNMADDSrrr,FNMSUBSrrr,
              FMLAv2f32,FMLAv4f32,
              FMLAv1i32_indexed,FMLAv1i64_indexed,FMLAv2i32_indexed)>;
def : InstRW<[CyWriteDMul, CyReadDMul],
             (instrs FMADDDrrr,FMSUBDrrr,FNMADDDrrr,FNMSUBDrrr,
              FMLAv2f64,FMLAv2i64_indexed,
              FMLSv2f64,FMLSv2i64_indexed)>;

def CyWritePMUL : SchedWriteRes<[CyUnitVD]> { let Latency = 3; }
def : InstRW<[CyWritePMUL], (instregex "PMULv", "PMULLv")>;

//---
// 7.9.6 Divide and Square Root
//---

// FDIV,FSQRT
// TODO: Add 64-bit variant with 19 cycle latency.
// TODO: Specialize FSQRT for longer latency.
def : WriteRes<WriteFDiv, [CyUnitVD, CyUnitFloatDiv]> {
  let Latency = 17;
  let ReleaseAtCycles = [2, 17];
}

def : InstRW<[CyWriteV4], (instregex "FRECPEv","FRECPXv","URECPEv","URSQRTEv")>;

def WriteFRSQRTE : SchedWriteRes<[CyUnitVM]> { let Latency = 4; }
def : InstRW<[WriteFRSQRTE], (instregex "FRSQRTEv")>;

def WriteFRECPS : SchedWriteRes<[CyUnitVM]> { let Latency = 8; }
def WriteFRSQRTS : SchedWriteRes<[CyUnitVM]> { let Latency = 10; }
def : InstRW<[WriteFRECPS],  (instregex "FRECPSv")>;
def : InstRW<[WriteFRSQRTS], (instregex "FRSQRTSv")>;

//---
// 7.9.7 Integer-FP Conversions
//---

// FCVT lengthen f16/s32
def : InstRW<[WriteVq], (instrs FCVTSHr,FCVTDHr,FCVTDSr)>;

// FCVT,FCVTN,FCVTXN
// SCVTF,UCVTF V,V
// FRINT(AIMNPXZ) V,V
def : WriteRes<WriteFCvt, [CyUnitV]> {let Latency = 4;}

// SCVT/UCVT S/D, Rd = VLD5+V4: 9 cycles.
def CyWriteCvtToFPR : WriteSequence<[WriteVLD, CyWriteV4]>;
def : InstRW<[CyWriteCopyToFPR], (instregex "FCVT[AMNPZ][SU][SU][WX][SD]r")>;

// FCVT Rd, S/D = V6+LD4: 10 cycles
def CyWriteCvtToGPR : WriteSequence<[CyWriteV6, WriteLD]>;
def : InstRW<[CyWriteCvtToGPR], (instregex "[SU]CVTF[SU][WX][SD]r")>;

// FCVTL is a WriteV

//---
// 7.9.8-7.9.10 Cryptography, Data Transposition, Table Lookup
//---

def CyWriteCrypto2 : SchedWriteRes<[CyUnitVD]> {let Latency = 2;}
def : InstRW<[CyWriteCrypto2], (instrs AESIMCrr, AESMCrr, SHA1Hrr,
                                       AESDrr, AESErr, SHA1SU1rr, SHA256SU0rr,
                                       SHA1SU0rrr)>;

def CyWriteCrypto3 : SchedWriteRes<[CyUnitVD]> {let Latency = 3;}
def : InstRW<[CyWriteCrypto3], (instrs SHA256SU1rrr)>;

def CyWriteCrypto6 : SchedWriteRes<[CyUnitVD]> {let Latency = 6;}
def : InstRW<[CyWriteCrypto6], (instrs SHA1Crrr, SHA1Mrrr, SHA1Prrr,
                                       SHA256Hrrr,SHA256H2rrr)>;

// TRN,UZP,ZUP are WriteV.

// TBL,TBX are WriteV.

//---
// 7.9.11-7.9.14 Load/Store, single element and paired
//---

// Loading into the vector unit takes 5 cycles vs 4 for integer loads.
def : WriteRes<WriteVLD, [CyUnitLS]> {
  let Latency = 5;
}

// Store-load forwarding is 4 cycles.
def : WriteRes<WriteVST, [CyUnitLS]> {
  let Latency = 4;
}

// WriteVLDPair/VSTPair sequences are expanded by the target description.

//---
// 7.9.15 Load, element operations
//---

// Only the first WriteVLD and WriteAdr for writeback matches def operands.
// Subsequent WriteVLDs consume resources. Since all loaded values have the
// same latency, this is acceptable.

// Vd is read 5 cycles after issuing the vector load.
def : ReadAdvance<ReadVLD, 5>;

def : InstRW<[WriteVLD],
             (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD, WriteAdr],
             (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST")>;

// Register writes from the load's high half are fused micro-ops.
def : InstRW<[WriteVLD],
             (instregex "LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD, WriteAdr],
             (instregex "LD1Twov(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVLD, WriteVLD],
             (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD, WriteAdr, WriteVLD],
             (instregex "LD1Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLD, WriteVLD],
             (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD, WriteAdr, WriteVLD],
             (instregex "LD1Threev(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVLD, WriteVLD, WriteVLD],
             (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD, WriteAdr, WriteVLD, WriteVLD],
             (instregex "LD1Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLD, WriteVLD],
             (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD, WriteAdr, WriteVLD],
             (instregex "LD1Fourv(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVLD, WriteVLD, WriteVLD, WriteVLD],
             (instregex "LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD, WriteAdr, WriteVLD, WriteVLD, WriteVLD],
             (instregex "LD1Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD],
             (instregex "LD1i(8|16|32)$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr],
             (instregex "LD1i(8|16|32)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD],          (instrs LD1i64)>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr],(instrs LD1i64_POST)>;

def : InstRW<[WriteVLDShuffle],
             (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr],
             (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[WriteVLDShuffle, WriteVq],
             (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVq],
             (instregex "LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[WriteVLDShuffle, WriteVLDShuffle],
             (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle],
             (instregex "LD2Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVq],
             (instregex "LD2i(8|16|32)$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVq],
             (instregex "LD2i(8|16|32)_POST")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVq],
             (instregex "LD2i64$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVq],
             (instregex "LD2i64_POST")>;

def : InstRW<[WriteVLDShuffle, WriteVq],
             (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVq],
             (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLDShuffle, WriteVLDShuffle, WriteVq],
             (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle, WriteVq],
             (instregex "LD3Threev(8b|4h|2s)_POST")>;
def : InstRW<[WriteVLDShuffle, WriteVLDShuffle, WriteVLDShuffle],
             (instregex "LD3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle, WriteVLDShuffle],
             (instregex "LD3Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVq, WriteVq],
             (instregex "LD3i(8|16|32)$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVq, WriteVq],
             (instregex "LD3i(8|16|32)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVLDShuffle, WriteVq],
             (instregex "LD3i64$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVLDShuffle, WriteVq],
             (instregex "LD3i64_POST")>;

def : InstRW<[WriteVLDShuffle, WriteVq, WriteVq],
             (instregex "LD3Rv(8b|4h|2s|16b|8h|4s)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVq, WriteVq],
             (instregex "LD3Rv(8b|4h|2s|16b|8h|4s)_POST")>;

def : InstRW<[WriteVLDShuffle, WriteVLDShuffle, WriteVq],
             (instrs LD3Rv1d,LD3Rv2d)>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle, WriteVq],
             (instrs LD3Rv1d_POST,LD3Rv2d_POST)>;

def : InstRW<[WriteVLDShuffle, WriteVLDShuffle, WriteVq, WriteVq],
             (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle, WriteVq, WriteVq],
             (instregex "LD4Fourv(8b|4h|2s)_POST")>;
def : InstRW<[WriteVLDPairShuffle, WriteVLDPairShuffle,
              WriteVLDPairShuffle, WriteVLDPairShuffle],
             (instregex "LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLDPairShuffle, WriteAdr, WriteVLDPairShuffle,
              WriteVLDPairShuffle, WriteVLDPairShuffle],
             (instregex "LD4Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVq, WriteVq, WriteVq],
             (instregex "LD4i(8|16|32)$")>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVq, WriteVq, WriteVq],
             (instregex "LD4i(8|16|32)_POST")>;


def : InstRW<[WriteVLDShuffle, ReadVLD, WriteVLDShuffle, WriteVq, WriteVq],
             (instrs LD4i64)>;
def : InstRW<[WriteVLDShuffle, ReadVLD, WriteAdr, WriteVLDShuffle, WriteVq],
             (instrs LD4i64_POST)>;

def : InstRW<[WriteVLDShuffle, WriteVq, WriteVq, WriteVq],
             (instregex "LD4Rv(8b|4h|2s|16b|8h|4s)$")>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVq, WriteVq, WriteVq],
             (instregex "LD4Rv(8b|4h|2s|16b|8h|4s)_POST")>;

def : InstRW<[WriteVLDShuffle, WriteVLDShuffle, WriteVq, WriteVq],
             (instrs LD4Rv1d,LD4Rv2d)>;
def : InstRW<[WriteVLDShuffle, WriteAdr, WriteVLDShuffle, WriteVq, WriteVq],
             (instrs LD4Rv1d_POST,LD4Rv2d_POST)>;

//---
// 7.9.16 Store, element operations
//---

// Only the WriteAdr for writeback matches a def operands.
// Subsequent WriteVLDs only consume resources.

def : InstRW<[WriteVST],
             (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVST],
             (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle],
             (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle],
             (instregex "ST1Twov(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVST, WriteVST],
             (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVST, WriteVST],
             (instregex "ST1Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle, WriteVST],
             (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVST],
             (instregex "ST1Threev(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVST, WriteVST, WriteVST],
             (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVST, WriteVST, WriteVST],
             (instregex "ST1Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST1Fourv(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVST, WriteVST, WriteVST, WriteVST],
             (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVST, WriteVST, WriteVST, WriteVST],
             (instregex "ST1Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle],           (instregex "ST1i(8|16|32)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle], (instregex "ST1i(8|16|32)_POST")>;

def : InstRW<[WriteVSTShuffle],           (instrs ST1i64)>;
def : InstRW<[WriteAdr, WriteVSTShuffle], (instrs ST1i64_POST)>;

def : InstRW<[WriteVSTShuffle],
             (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle],
             (instregex "ST2Twov(8b|4h|2s)_POST")>;
def : InstRW<[WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST2Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle],           (instregex "ST2i(8|16|32)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle], (instregex "ST2i(8|16|32)_POST")>;
def : InstRW<[WriteVSTShuffle],           (instrs ST2i64)>;
def : InstRW<[WriteAdr, WriteVSTShuffle], (instrs ST2i64_POST)>;

def : InstRW<[WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST3Threev(8b|4h|2s)_POST")>;
def : InstRW<[WriteVSTShuffle, WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle, WriteVSTShuffle],
             (instregex "ST3Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTShuffle],           (instregex "ST3i(8|16|32)$")>;
def : InstRW<[WriteAdr, WriteVSTShuffle], (instregex "ST3i(8|16|32)_POST")>;

def :InstRW<[WriteVSTShuffle, WriteVSTShuffle],           (instrs ST3i64)>;
def :InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle], (instrs ST3i64_POST)>;

def : InstRW<[WriteVSTPairShuffle, WriteVSTPairShuffle],
            (instregex "ST4Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, WriteVSTPairShuffle, WriteVSTPairShuffle],
            (instregex "ST4Fourv(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVSTPairShuffle, WriteVSTPairShuffle,
              WriteVSTPairShuffle, WriteVSTPairShuffle],
             (instregex "ST4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, WriteVSTPairShuffle, WriteVSTPairShuffle,
              WriteVSTPairShuffle, WriteVSTPairShuffle],
             (instregex "ST4Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[WriteVSTPairShuffle],           (instregex "ST4i(8|16|32)$")>;
def : InstRW<[WriteAdr, WriteVSTPairShuffle], (instregex "ST4i(8|16|32)_POST")>;

def : InstRW<[WriteVSTShuffle, WriteVSTShuffle],          (instrs ST4i64)>;
def : InstRW<[WriteAdr, WriteVSTShuffle, WriteVSTShuffle],(instrs ST4i64_POST)>;

// Atomic operations are not supported.
def : WriteRes<WriteAtomic, []> { let Unsupported = 1; }

//---
// Unused SchedRead types
//---

def : ReadAdvance<ReadI, 0>;
def : ReadAdvance<ReadISReg, 0>;
def : ReadAdvance<ReadIEReg, 0>;
def : ReadAdvance<ReadIM, 0>;
def : ReadAdvance<ReadIMA, 0>;
def : ReadAdvance<ReadID, 0>;

} // SchedModel = CycloneModel
