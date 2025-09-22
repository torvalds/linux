//==- ARMScheduleR52.td - Cortex-R52 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SchedRead/Write data for the ARM Cortex-R52 processor.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The Cortex-R52 is an in-order pipelined superscalar microprocessor with
// a 8 stage pipeline. It can issue maximum two instructions in each cycle.
// There are two ALUs, one LDST, one MUL  and a non-pipelined integer DIV.
// A number of forwarding paths enable results of computations to be input
// to subsequent operations before they are written to registers.
// This scheduler is a MachineScheduler. See TargetSchedule.td for details.

def CortexR52Model : SchedMachineModel {
  let MicroOpBufferSize = 0;  // R52 is in-order processor
  let IssueWidth = 2;         // 2 micro-ops dispatched per cycle
  let LoadLatency = 1;        // Optimistic, assuming no misses
  let MispredictPenalty = 8;  // A branch direction mispredict, including PFU
  let CompleteModel = 0;      // Covers instructions applicable to cortex-r52.
}


//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.

// Modeling each pipeline as a ProcResource using the BufferSize = 0 since
// Cortex-R52 is an in-order processor.

def R52UnitALU    : ProcResource<2> { let BufferSize = 0; } // Int ALU
def R52UnitMAC    : ProcResource<1> { let BufferSize = 0; } // Int MAC
def R52UnitDiv    : ProcResource<1> { let BufferSize = 0; } // Int Division
def R52UnitLd     : ProcResource<1> { let BufferSize = 0; } // Load/Store
def R52UnitB      : ProcResource<1> { let BufferSize = 0; } // Branch
def R52UnitFPALU  : ProcResource<2> { let BufferSize = 0; } // FP ALU
def R52UnitFPMUL  : ProcResource<2> { let BufferSize = 0; } // FP MUL
def R52UnitFPDIV  : ProcResource<1> { let BufferSize = 0; } // FP DIV

// Cortex-R52 specific SchedReads
def R52Read_ISS   : SchedRead;
def R52Read_EX1   : SchedRead;
def R52Read_EX2   : SchedRead;
def R52Read_WRI   : SchedRead;
def R52Read_F0    : SchedRead; // F0 maps to ISS stage of integer pipe
def R52Read_F1    : SchedRead;
def R52Read_F2    : SchedRead;


//===----------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types which map ProcResources and set latency.

let SchedModel = CortexR52Model in {

// ALU - Write occurs in Late EX2 (independent of whether shift was required)
def : WriteRes<WriteALU, [R52UnitALU]> { let Latency = 3; }
def : WriteRes<WriteALUsi, [R52UnitALU]> { let Latency = 3; }
def : WriteRes<WriteALUsr, [R52UnitALU]> { let Latency = 3; }
def : WriteRes<WriteALUSsr, [R52UnitALU]> { let Latency = 3; }

// Compares
def : WriteRes<WriteCMP, [R52UnitALU]> { let Latency = 0; }
def : WriteRes<WriteCMPsi, [R52UnitALU]> { let Latency = 0; }
def : WriteRes<WriteCMPsr, [R52UnitALU]> { let Latency = 0; }

// Multiply - aliased to sub-target specific later

// Div - may stall 0-9 cycles depending on input (i.e. WRI+(0-9)/2)
def : WriteRes<WriteDIV, [R52UnitDiv]> {
  let Latency = 8; let ReleaseAtCycles = [8]; // non-pipelined
}

// Branches  - LR written in Late EX2
def : WriteRes<WriteBr, [R52UnitB]> { let Latency = 0; }
def : WriteRes<WriteBrL, [R52UnitB]> { let Latency = 0; }
def : WriteRes<WriteBrTbl, [R52UnitALU]> { let Latency = 0; }

// Misc
def : WriteRes<WriteNoop, []> { let Latency = 0; let NumMicroOps = 0; }

// Integer pipeline by-passes
def : ReadAdvance<ReadALU, 1>;   // Operand needed in EX1 stage
def : ReadAdvance<ReadALUsr, 0>; // Shift operands needed in ISS
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 0>;

// Floating-point. Map target-defined SchedReadWrites to subtarget
def : WriteRes<WriteFPMUL32, [R52UnitFPMUL]> { let Latency = 6; }

def : WriteRes<WriteFPMUL64, [R52UnitFPMUL, R52UnitFPMUL]> {
  let Latency = 6;
}

def : WriteRes<WriteFPMAC32, [R52UnitFPMUL, R52UnitFPALU]> {
  let Latency = 11;     // as it is internally two insns (MUL then ADD)
}

def : WriteRes<WriteFPMAC64, [R52UnitFPMUL, R52UnitFPMUL,
                              R52UnitFPALU, R52UnitFPALU]> {
  let Latency = 11;
}

def : WriteRes<WriteFPDIV32, [R52UnitDiv]> {
  let Latency = 7;          // FP div takes fixed #cycles
  let ReleaseAtCycles = [7]; // is not pipelined
}

def : WriteRes<WriteFPDIV64, [R52UnitDiv]> {
  let Latency = 17;
  let ReleaseAtCycles = [17];
}

def : WriteRes<WriteFPSQRT32, [R52UnitDiv]> { let Latency = 7; }
def : WriteRes<WriteFPSQRT64, [R52UnitDiv]> { let Latency = 17; }

// Overriden via InstRW for this processor.
def : WriteRes<WriteVST1, []>;
def : WriteRes<WriteVST2, []>;
def : WriteRes<WriteVST3, []>;
def : WriteRes<WriteVST4, []>;

def : ReadAdvance<ReadFPMUL, 1>; // mul operand read in F1
def : ReadAdvance<ReadFPMAC, 1>; // fp-mac operand read in F1

//===----------------------------------------------------------------------===//
// Subtarget-specific SchedReadWrites.

// Forwarding information - based on when an operand is read
def : ReadAdvance<R52Read_ISS, 0>;
def : ReadAdvance<R52Read_EX1, 1>;
def : ReadAdvance<R52Read_EX2, 2>;
def : ReadAdvance<R52Read_F0, 0>;
def : ReadAdvance<R52Read_F1, 1>;
def : ReadAdvance<R52Read_F2, 2>;


// Cortex-R52 specific SchedWrites for use with InstRW
def R52WriteMAC        : SchedWriteRes<[R52UnitMAC]> { let Latency = 4; }
def R52WriteMACHi      : SchedWriteRes<[R52UnitMAC]> {
  let Latency = 4; let NumMicroOps = 0;
}
def R52WriteDIV        : SchedWriteRes<[R52UnitDiv]> {
  let Latency = 8; let ReleaseAtCycles = [8]; // not pipelined
}
def R52WriteLd         : SchedWriteRes<[R52UnitLd]> { let Latency = 4; }
def R52WriteST         : SchedWriteRes<[R52UnitLd]> { let Latency = 4; }
def R52WriteAdr        : SchedWriteRes<[]> { let Latency = 0; }
def R52WriteCC         : SchedWriteRes<[]> { let Latency = 0; }
def R52WriteALU_EX1    : SchedWriteRes<[R52UnitALU]> { let Latency = 2; }
def R52WriteALU_EX2    : SchedWriteRes<[R52UnitALU]> { let Latency = 3; }
def R52WriteALU_WRI    : SchedWriteRes<[R52UnitALU]> { let Latency = 4; }

def R52WriteNoRSRC_EX2 : SchedWriteRes<[]> { let Latency = 3; }
def R52WriteNoRSRC_WRI : SchedWriteRes<[]> { let Latency = 4; }

// Alias generics to sub-target specific
def : SchedAlias<WriteMUL16, R52WriteMAC>;
def : SchedAlias<WriteMUL32, R52WriteMAC>;
def : SchedAlias<WriteMUL64Lo, R52WriteMAC>;
def : SchedAlias<WriteMUL64Hi, R52WriteMACHi>;
def : SchedAlias<WriteMAC16, R52WriteMAC>;
def : SchedAlias<WriteMAC32, R52WriteMAC>;
def : SchedAlias<WriteMAC64Lo, R52WriteMAC>;
def : SchedAlias<WriteMAC64Hi, R52WriteMACHi>;
def : SchedAlias<WritePreLd, R52WriteLd>;
def : SchedAlias<WriteLd, R52WriteLd>;
def : SchedAlias<WriteST, R52WriteST>;

def R52WriteFPALU_F3   : SchedWriteRes<[R52UnitFPALU]> { let Latency = 4; }
def R52Write2FPALU_F3  : SchedWriteRes<[R52UnitFPALU, R52UnitFPALU]> {
  let Latency = 4;
}
def R52WriteFPALU_F4   : SchedWriteRes<[R52UnitFPALU]> { let Latency = 5; }
def R52Write2FPALU_F4  : SchedWriteRes<[R52UnitFPALU, R52UnitFPALU]> {
  let Latency = 5;
}
def R52WriteFPALU_F5   : SchedWriteRes<[R52UnitFPALU]> { let Latency = 6; }
def R52Write2FPALU_F5  : SchedWriteRes<[R52UnitFPALU, R52UnitFPALU]> {
  let Latency = 6;
}
def R52WriteFPMUL_F5   : SchedWriteRes<[R52UnitFPMUL]> { let Latency = 6; }
def R52Write2FPMUL_F5  : SchedWriteRes<[R52UnitFPMUL, R52UnitFPMUL]> {
  let Latency = 6;
}
def R52WriteFPMAC_F5   : SchedWriteRes<[R52UnitFPMUL, R52UnitFPALU]> {
  let Latency = 11;     // as it is internally two insns (MUL then ADD)
}
def R52Write2FPMAC_F5  : SchedWriteRes<[R52UnitFPMUL, R52UnitFPMUL,
                                         R52UnitFPALU, R52UnitFPALU]> {
  let Latency = 11;
}

def R52WriteFPLd_F4    : SchedWriteRes<[R52UnitLd]> { let Latency = 5; }
def R52WriteFPST_F4    : SchedWriteRes<[R52UnitLd]> { let Latency = 5; }

//===----------------------------------------------------------------------===//
// Floating-point. Map target defined SchedReadWrites to processor specific ones
//
def : SchedAlias<WriteFPCVT,   R52WriteFPALU_F5>;
def : SchedAlias<WriteFPMOV, R52WriteFPALU_F3>;
def : SchedAlias<WriteFPALU32, R52WriteFPALU_F5>;
def : SchedAlias<WriteFPALU64, R52WriteFPALU_F5>;

//===----------------------------------------------------------------------===//
// Subtarget-specific overrides. Map opcodes to list of SchedReadWrites types.
//
def : InstRW<[WriteALU], (instrs COPY)>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1, R52Read_ISS],
      (instregex "SXTB", "SXTH", "SXTB16", "UXTB", "UXTH", "UXTB16",
      "t2SXTB", "t2SXTH", "t2SXTB16", "t2UXTB", "t2UXTH", "t2UXTB16")>;

def : InstRW<[R52WriteALU_EX1, R52Read_ISS],
      (instregex "MOVCCi32imm", "MOVi32imm", "t2MOVCCi", "t2MOVi")>;
def : InstRW<[R52WriteALU_EX2, R52Read_EX1],
      (instregex "MOV_ga_pcrel$")>;
def : InstRW<[R52WriteLd,R52Read_ISS],
      (instregex "MOV_ga_pcrel_ldr")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1, R52Read_EX1], (instregex "SEL", "t2SEL")>;

def : InstRW< [R52WriteALU_EX2, R52Read_ISS, R52Read_ISS],
      (instregex "BFC", "BFI", "UBFX", "SBFX", "(t|t2)BFC", "(t|t2)BFI",
      "(t|t2)UBFX", "(t|t2)SBFX")>;

// Saturating arithmetic
def : InstRW< [R52WriteALU_WRI, R52Read_EX1, R52Read_EX1],
      (instregex "QADD", "QSUB", "QDADD", "QDSUB", "SSAT", "SSAT16", "USAT",
      "QADD8", "QADD16", "QSUB8", "QSUB16", "QASX", "QSAX",
      "UQADD8", "UQADD16","UQSUB8","UQSUB16","UQASX","UQSAX", "t2QADD",
      "t2QSUB", "t2QDADD", "t2QDSUB", "t2SSAT", "t2SSAT16", "t2USAT",
      "t2QADD8", "t2QADD16", "t2QSUB8", "t2QSUB16", "t2QASX", "t2QSAX",
      "t2UQADD8", "t2UQADD16","t2UQSUB8","t2UQSUB16","t2UQASX","t2UQSAX","t2ABS")>;

// Parallel arithmetic
def : InstRW< [R52WriteALU_EX2, R52Read_EX1, R52Read_EX1],
      (instregex "SADD8", "SADD16", "SSUB8", "SSUB16", "SASX", "SSAX",
      "UADD8", "UADD16", "USUB8", "USUB16", "UASX", "USAX", "t2SADD8",
      "t2SADD16", "t2SSUB8", "t2SSUB16", "t2SASX", "t2SSAX", "t2UADD8",
      "t2UADD16", "t2USUB8", "t2USUB16", "t2UASX", "t2USAX")>;

// Flag setting.
def : InstRW< [R52WriteALU_EX2, R52Read_EX1, R52Read_EX1],
      (instregex "SHADD8", "SHADD16", "SHSUB8", "SHSUB16", "SHASX", "SHSAX",
      "SXTAB", "SXTAB16", "SXTAH", "UHADD8", "UHADD16", "UHSUB8", "UHSUB16",
      "UHASX", "UHSAX", "UXTAB", "UXTAB16", "UXTAH", "t2SHADD8", "t2SHADD16",
      "t2SHSUB8", "t2SHSUB16", "t2SHASX", "t2SHSAX", "t2SXTAB", "t2SXTAB16",
      "t2SXTAH", "t2UHADD8", "t2UHADD16", "t2UHSUB8", "t2UHSUB16", "t2UHASX",
      "t2UHSAX", "t2UXTAB", "t2UXTAB16", "t2UXTAH")>;

// Sum of Absolute Difference
def : InstRW< [R52WriteALU_WRI, R52Read_ISS, R52Read_ISS, R52Read_ISS],
      (instregex "USAD8", "t2USAD8", "USADA8", "t2USADA8") >;

// Integer Multiply
def : InstRW<[R52WriteMAC, R52Read_ISS, R52Read_ISS],
      (instregex "MUL", "SMMUL", "SMMULR", "SMULBB", "SMULBT",
      "SMULTB", "SMULTT", "SMULWB", "SMULWT", "SMUSD", "SMUSDX", "t2MUL",
      "t2SMMUL", "t2SMMULR", "t2SMULBB", "t2SMULBT", "t2SMULTB", "t2SMULTT",
      "t2SMULWB", "t2SMULWT", "t2SMUSD")>;

// Multiply Accumulate
// Even for 64-bit accumulation (or Long), the single MAC is used (not ALUs).
// The store pipeline is used partly for 64-bit operations.
def : InstRW<[R52WriteMAC, R52Read_ISS, R52Read_ISS, R52Read_ISS],
      (instregex "MLA", "MLS", "SMMLA", "SMMLAR", "SMMLS", "SMMLSR",
      "t2MLA", "t2MLS", "t2SMMLA", "t2SMMLAR", "t2SMMLS", "t2SMMLSR",
      "SMUAD", "SMUADX", "t2SMUAD", "t2SMUADX",
      "SMLABB", "SMLABT", "SMLATB", "SMLATT", "SMLSD", "SMLSDX",
      "SMLAWB", "SMLAWT", "t2SMLABB", "t2SMLABT", "t2SMLATB", "t2SMLATT",
      "t2SMLSD", "t2SMLSDX", "t2SMLAWB", "t2SMLAWT",
      "SMLAD", "SMLADX", "t2SMLAD", "t2SMLADX",
      "SMULL$", "UMULL$", "t2SMULL$", "t2UMULL$",
      "SMLAL", "UMLAL", "SMLALBT",
      "SMLALTB", "SMLALTT", "SMLALD", "SMLALDX", "SMLSLD", "SMLSLDX",
      "UMAAL", "t2SMLAL", "t2UMLAL",
      "t2SMLALBT", "t2SMLALTB", "t2SMLALTT", "t2SMLALD", "t2SMLALDX",
      "t2SMLSLD", "t2SMLSLDX", "t2UMAAL")>;

def : InstRW <[R52WriteDIV, R52Read_ISS, R52Read_ISS],
      (instregex "t2SDIV", "t2UDIV")>;

// Loads (except POST) with SHL > 2, or ror, require 2 extra cycles.
// However, that's non-trivial to specify, so we keep it uniform
def : InstRW<[R52WriteLd, R52Read_ISS, R52Read_ISS],
      (instregex "LDR(i12|rs)$", "LDRB(i12|rs)$", "t2LDR(i8|i12|s|pci)",
      "t2LDR(H|B)(i8|i12|s|pci)", "LDREX", "t2LDREX",
      "tLDR[BH](r|i|spi|pci|pciASM)", "tLDR(r|i|spi|pci|pciASM)",
      "LDRH$",  "PICLDR$", "PICLDR(H|B)$", "LDRcp$",
      "PICLDRS(H|B)$", "t2LDRS(H|B)(i|r|p|s)", "LDRS(H|B)$",
      "t2LDRpci_pic", "tLDRS(B|H)", "t2LDRDi8", "LDRD$", "LDA", "t2LDA")>;
def : InstRW<[R52WriteLd, R52WriteAdr, R52Read_ISS, R52Read_ISS],
      (instregex "LD(RB|R)(_|T_)(POST|PRE)_(IMM|REG)", "LDRH(_PRE|_POST)",
      "LDRBT_POST$", "LDR(T|BT)_POST_(REG|IMM)", "LDRHT(i|r)",
      "t2LD(R|RB|RH)_(PRE|POST)", "t2LD(R|RB|RH)T",
      "LDR(SH|SB)(_POST|_PRE)", "t2LDR(SH|SB)(_POST|_PRE)",
      "LDRS(B|H)T(i|r)", "t2LDRS(B|H)T(i|r)?",
      "LDRD_(POST|PRE)", "t2LDRD_(POST|PRE)")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1], (instregex "MOVS?sr", "t2MOVS?sr")>;
def : InstRW<[R52WriteALU_WRI, R52Read_EX2], (instregex "MOVT", "t2MOVT")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1], (instregex "AD(C|D)S?ri", "ANDS?ri",
      "BICS?ri", "CLZ", "EORri", "MVNS?r", "ORRri", "RSBS?ri", "RSCri", "SBCri",
      "t2AD(C|D)S?ri", "t2ANDS?ri", "t2BICS?ri","t2CLZ", "t2EORri", "t2MVN",
      "t2ORRri", "t2RSBS?ri", "t2SBCri")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1, R52Read_EX1], (instregex "AD(C|D)S?rr",
      "ANDS?rr", "BICS?rr", "CRC", "EORrr", "ORRrr", "RSBrr", "RSCrr", "SBCrr",
      "t2AD(C|D)S?rr", "t2ANDS?rr", "t2BICS?rr", "t2CRC", "t2EORrr", "t2SBCrr")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1, R52Read_ISS], (instregex "AD(C|D)S?rsi",
      "ANDS?rsi", "BICS?rsi", "EORrsi", "ORRrsi", "RSBrsi", "RSCrsi", "SBCrsi",
      "t2AD(C|D)S?rs", "t2ANDS?rs", "t2BICS?rs", "t2EORrs", "t2ORRrs", "t2RSBrs", "t2SBCrs")>;

def : InstRW<[R52WriteALU_EX2, R52Read_EX1, R52Read_ISS, R52Read_ISS],
      (instregex "AD(C|D)S?rsr", "ANDS?rsr", "BICS?rsr", "EORrsr", "MVNS?sr",
      "ORRrsr", "RSBrsr", "RSCrsr", "SBCrsr")>;

def : InstRW<[R52WriteALU_EX1],
    (instregex "ADR", "MOVsi", "MVNS?s?i", "t2MOVS?si")>;

def : InstRW<[R52WriteALU_EX1, R52Read_ISS], (instregex "ASRi", "RORS?i")>;
def : InstRW<[R52WriteALU_EX1, R52Read_ISS, R52Read_ISS],
      (instregex "ASRr", "RORS?r", "LSR", "LSL")>;

def : InstRW<[R52WriteCC, R52Read_EX1], (instregex "CMPri", "CMNri")>;
def : InstRW<[R52WriteCC, R52Read_EX1, R52Read_EX1], (instregex "CMPrr", "CMNzrr")>;
def : InstRW<[R52WriteCC, R52Read_EX1, R52Read_ISS], (instregex "CMPrsi", "CMNzrsi")>;
def : InstRW<[R52WriteCC, R52Read_EX1, R52Read_ISS, R52Read_ISS], (instregex "CMPrsr", "CMNzrsr")>;

def : InstRW<[R52WriteALU_EX2, R52Read_ISS],
      (instregex "t2LDC", "RBIT", "REV", "REV16", "REVSH", "RRX")>;

def : InstRW<[R52WriteCC, R52Read_ISS], (instregex "TST")>;

def : InstRW<[R52WriteLd], (instregex "MRS", "MRSbanked")>;
def : InstRW<[R52WriteLd, R52Read_EX1], (instregex "MSR", "MSRbanked")>;

// Integer Load, Multiple.
foreach Lat = 3-25 in {
  def R52WriteILDM#Lat#Cy : SchedWriteRes<[R52UnitLd]> {
    let Latency = Lat;
  }
  def R52WriteILDM#Lat#CyNo : SchedWriteRes<[]> {
    let Latency = Lat;
    let NumMicroOps = 0;
  }
}
foreach NAddr = 1-16 in {
  def R52ILDMAddr#NAddr#Pred : SchedPredicate<"TII->getNumLDMAddresses(*MI) == "#NAddr>;
}
def R52WriteILDMAddrNoWB : SchedWriteRes<[R52UnitLd]> { let Latency = 0; }
def R52WriteILDMAddrWB : SchedWriteRes<[R52UnitLd]>;
def R52WriteILDM : SchedWriteVariant<[
    SchedVar<R52ILDMAddr2Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy]>,

    SchedVar<R52ILDMAddr3Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy]>,
    SchedVar<R52ILDMAddr4Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy]>,

    SchedVar<R52ILDMAddr5Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy]>,
    SchedVar<R52ILDMAddr6Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy]>,

    SchedVar<R52ILDMAddr7Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy]>,
    SchedVar<R52ILDMAddr8Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy]>,

    SchedVar<R52ILDMAddr9Pred, [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy]>,
    SchedVar<R52ILDMAddr10Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy]>,

    SchedVar<R52ILDMAddr11Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy]>,
    SchedVar<R52ILDMAddr12Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy, R52WriteILDM15Cy]>,

    SchedVar<R52ILDMAddr13Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy, R52WriteILDM15Cy,
                                 R52WriteILDM16Cy]>,
    SchedVar<R52ILDMAddr14Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy, R52WriteILDM15Cy,
                                 R52WriteILDM16Cy, R52WriteILDM17Cy]>,

    SchedVar<R52ILDMAddr15Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy, R52WriteILDM15Cy,
                                 R52WriteILDM16Cy, R52WriteILDM17Cy,
                                 R52WriteILDM18Cy]>,
    SchedVar<R52ILDMAddr15Pred,[R52WriteILDM4Cy, R52WriteILDM5Cy,
                                 R52WriteILDM6Cy, R52WriteILDM7Cy,
                                 R52WriteILDM8Cy, R52WriteILDM9Cy,
                                 R52WriteILDM10Cy, R52WriteILDM11Cy,
                                 R52WriteILDM12Cy, R52WriteILDM13Cy,
                                 R52WriteILDM14Cy, R52WriteILDM15Cy,
                                 R52WriteILDM16Cy, R52WriteILDM17Cy,
                                 R52WriteILDM18Cy, R52WriteILDM19Cy]>,

// Unknown number of registers, just use resources for two registers.
    SchedVar<NoSchedPred,      [R52WriteILDM4Cy, R52WriteILDM5Cy,
                                R52WriteILDM6CyNo, R52WriteILDM7CyNo,
                                R52WriteILDM8CyNo, R52WriteILDM9CyNo,
                                R52WriteILDM10CyNo, R52WriteILDM11CyNo,
                                R52WriteILDM12CyNo, R52WriteILDM13CyNo,
                                R52WriteILDM14CyNo, R52WriteILDM15CyNo,
                                R52WriteILDM16CyNo, R52WriteILDM17CyNo,
                                R52WriteILDM18Cy, R52WriteILDM19Cy]>
]> { let Variadic=1; }

// Integer Store, Multiple
def R52WriteIStIncAddr : SchedWriteRes<[R52UnitLd]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
foreach NumAddr = 1-16 in {
  def R52WriteISTM#NumAddr : WriteSequence<[R52WriteIStIncAddr], NumAddr>;
}
def R52WriteISTM : SchedWriteVariant<[
    SchedVar<R52ILDMAddr2Pred, [R52WriteISTM2]>,
    SchedVar<R52ILDMAddr3Pred, [R52WriteISTM3]>,
    SchedVar<R52ILDMAddr4Pred, [R52WriteISTM4]>,
    SchedVar<R52ILDMAddr5Pred, [R52WriteISTM5]>,
    SchedVar<R52ILDMAddr6Pred, [R52WriteISTM6]>,
    SchedVar<R52ILDMAddr7Pred, [R52WriteISTM7]>,
    SchedVar<R52ILDMAddr8Pred, [R52WriteISTM8]>,
    SchedVar<R52ILDMAddr9Pred, [R52WriteISTM9]>,
    SchedVar<R52ILDMAddr10Pred,[R52WriteISTM10]>,
    SchedVar<R52ILDMAddr11Pred,[R52WriteISTM11]>,
    SchedVar<R52ILDMAddr12Pred,[R52WriteISTM12]>,
    SchedVar<R52ILDMAddr13Pred,[R52WriteISTM13]>,
    SchedVar<R52ILDMAddr14Pred,[R52WriteISTM14]>,
    SchedVar<R52ILDMAddr15Pred,[R52WriteISTM15]>,
    SchedVar<R52ILDMAddr16Pred,[R52WriteISTM16]>,
    // Unknow number of registers, just use resources for two registers.
    SchedVar<NoSchedPred,      [R52WriteISTM2]>
]>;

def : InstRW<[R52WriteILDM, R52Read_ISS],
      (instregex "LDM(IA|DA|DB|IB)$", "t2LDM(IA|DA|DB|IB)$",
      "(t|sys)LDM(IA|DA|DB|IB)$")>;
def : InstRW<[R52WriteILDM, R52WriteAdr, R52Read_ISS],
      (instregex "LDM(IA|DA|DB|IB)_UPD", "(t2|sys|t)LDM(IA|DA|DB|IB)_UPD")>;
def : InstRW<[R52WriteILDM, R52WriteAdr, R52Read_ISS],
        (instregex "LDMIA_RET", "(t|t2)LDMIA_RET", "tPOP")>;

// Integer Store, Single Element
def : InstRW<[R52WriteLd, R52Read_ISS, R52Read_EX2],
      (instregex "PICSTR", "STR(i12|rs)", "STRB(i12|rs)", "STRH$", "STREX", "SRS", "t2SRS",
      "t2SRSDB", "t2STREX", "t2STREXB", "t2STREXD", "t2STREXH", "t2STR(i12|i8|s)$",
      "RFE", "t2RFE", "t2STR[BH](i12|i8|s)$", "tSTR[BH](i|r)", "tSTR(i|r)", "tSTRspi")>;

def : InstRW<[R52WriteLd, R52WriteAdr, R52Read_ISS, R52Read_EX2],
      (instregex "STR(B_|_|BT_|T_)(PRE_IMM|PRE_REG|POST_REG|POST_IMM)",
      "STR(i|r)_preidx", "STRB(i|r)_preidx", "STRH_preidx", "STR(H_|HT_)(PRE|POST)",
      "STR(BT|HT|T)", "t2STR_(PRE|POST)", "t2STR[BH]_(PRE|POST)",
      "t2STR_preidx", "t2STR[BH]_preidx", "t2ST(RB|RH|R)T")>;

// Integer Store, Dual
def : InstRW<[R52WriteLd, R52Read_ISS, R52Read_EX2],
    (instregex "STRD$", "t2STRDi8", "STL", "t2STL")>;
def : InstRW<[R52WriteLd, R52WriteAdr, R52Read_ISS, R52Read_EX2],
    (instregex "(t2|t)STRD_(POST|PRE)", "STRD_(POST|PRE)")>;

def : InstRW<[R52WriteISTM, R52Read_ISS, R52Read_EX2],
    (instregex "STM(IB|IA|DB|DA)$", "(t2|sys|t)STM(IB|IA|DB|DA)$")>;
def : InstRW<[R52WriteISTM, R52WriteAdr, R52Read_ISS, R52Read_EX2],
    (instregex "STM(IB|IA|DB|DA)_UPD", "(t2|sys|t)STM(IB|IA|DB|DA)_UPD",
    "tPUSH")>;

// LDRLIT pseudo instructions, they expand to LDR + PICADD
def : InstRW<[R52WriteLd],
      (instregex "t?LDRLIT_ga_abs", "t?LDRLIT_ga_pcrel$")>;
// LDRLIT_ga_pcrel_ldr expands to LDR + PICLDR
def : InstRW<[R52WriteLd], (instregex "LDRLIT_ga_pcrel_ldr")>;



//===----------------------------------------------------------------------===//
// VFP, Floating Point Support
def : InstRW<[R52WriteFPALU_F5, R52Read_F1, R52Read_F1], (instregex "VABD(fd|hd)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1, R52Read_F1], (instregex "VABD(fq|hq)")>;

def : InstRW<[R52WriteFPALU_F5, R52Read_F1], (instregex "VABS(D|S|H)")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F1], (instregex "VABS(fd|hd)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1], (instregex "VABS(fq|hq)")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F1], (instregex "(VACGE|VACGT)(fd|hd)")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F1, R52Read_F1], (instregex "(VACGE|VACGT)(fq|hq)")>;

def : InstRW<[R52WriteFPALU_F5, R52Read_F1, R52Read_F1], (instregex "(VADD|VSUB)(D|S|H|fd|hd)$")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1, R52Read_F1], (instregex "(VADD|VSUB)(fq|hq)")>;

def : InstRW<[R52WriteFPLd_F4, R52Read_ISS, R52Read_F1], (instregex "VLDR")>;
def : InstRW<[R52WriteFPST_F4, R52Read_ISS, R52Read_F1], (instregex "VSTR")>;


//===----------------------------------------------------------------------===//
// Neon Support

// vector multiple load stores
foreach NumAddr = 1-16 in {
  def R52LMAddrPred#NumAddr :
    SchedPredicate<"MI->getNumOperands() == "#NumAddr>;
}
foreach Lat = 1-32 in {
  def R52WriteLM#Lat#Cy : SchedWriteRes<[]> {
    let Latency = Lat;
  }
}
foreach Num = 1-32 in { // reserve LdSt resource, no dual-issue
  def R52ReserveLd#Num#Cy : SchedWriteRes<[R52UnitLd]> {
    let Latency = 0;
    let NumMicroOps = Num;
    let ReleaseAtCycles = [Num];
  }
}
def R52WriteVLDM : SchedWriteVariant<[
  // 1 D reg
  SchedVar<R52LMAddrPred1,  [R52WriteLM5Cy,
                              R52ReserveLd5Cy]>,
  SchedVar<R52LMAddrPred2,  [R52WriteLM5Cy,
                              R52ReserveLd5Cy]>,

  // 2 D reg
  SchedVar<R52LMAddrPred3,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52ReserveLd6Cy]>,
  SchedVar<R52LMAddrPred4,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52ReserveLd6Cy]>,

  // 3 D reg
  SchedVar<R52LMAddrPred5,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy,
                              R52ReserveLd4Cy]>,
  SchedVar<R52LMAddrPred6,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy,
                              R52ReserveLd7Cy]>,

  // 4 D reg
  SchedVar<R52LMAddrPred7,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52ReserveLd8Cy]>,
  SchedVar<R52LMAddrPred8,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52ReserveLd8Cy]>,

  // 5 D reg
  SchedVar<R52LMAddrPred9,  [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy,
                              R52ReserveLd9Cy]>,
  SchedVar<R52LMAddrPred10, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy,
                              R52ReserveLd9Cy]>,

  // 6 D reg
  SchedVar<R52LMAddrPred11, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52ReserveLd10Cy]>,
  SchedVar<R52LMAddrPred12, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52ReserveLd10Cy]>,

  // 7 D reg
  SchedVar<R52LMAddrPred13, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52WriteLM11Cy,
                              R52ReserveLd11Cy]>,
  SchedVar<R52LMAddrPred14, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52WriteLM11Cy,
                              R52ReserveLd11Cy]>,

  // 8 D reg
  SchedVar<R52LMAddrPred14, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52WriteLM11Cy, R52WriteLM12Cy,
                              R52ReserveLd12Cy]>,
  SchedVar<R52LMAddrPred15, [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52WriteLM11Cy, R52WriteLM12Cy,
                              R52ReserveLd12Cy]>,
  // unknown number of reg.
  SchedVar<NoSchedPred,      [R52WriteLM5Cy, R52WriteLM6Cy,
                              R52WriteLM7Cy, R52WriteLM8Cy,
                              R52WriteLM9Cy, R52WriteLM10Cy,
                              R52WriteLM11Cy, R52WriteLM12Cy,
                              R52ReserveLd5Cy]>
]> { let Variadic=1;}

// variable stores. Cannot dual-issue
def R52WriteSTM5  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [1];
}
def R52WriteSTM6  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [2];
}
def R52WriteSTM7  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 7;
  let NumMicroOps = 6;
  let ReleaseAtCycles = [3];
}
def R52WriteSTM8  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 8;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [4];
}
def R52WriteSTM9  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 9;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [5];
}
def R52WriteSTM10 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 10;
  let NumMicroOps = 12;
  let ReleaseAtCycles = [6];
}
def R52WriteSTM11 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 11;
  let NumMicroOps = 14;
  let ReleaseAtCycles = [7];
}
def R52WriteSTM12 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 12;
  let NumMicroOps = 16;
  let ReleaseAtCycles = [8];
}
def R52WriteSTM13 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 13;
  let NumMicroOps = 18;
  let ReleaseAtCycles = [9];
}
def R52WriteSTM14 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 14;
  let NumMicroOps = 20;
  let ReleaseAtCycles = [10];
}
def R52WriteSTM15 : SchedWriteRes<[R52UnitLd]> {
  let Latency = 15;
  let NumMicroOps = 22;
  let ReleaseAtCycles = [11];
}

def R52WriteSTM : SchedWriteVariant<[
  SchedVar<R52LMAddrPred1, [R52WriteSTM5]>,
  SchedVar<R52LMAddrPred2, [R52WriteSTM5]>,
  SchedVar<R52LMAddrPred3, [R52WriteSTM6]>,
  SchedVar<R52LMAddrPred4, [R52WriteSTM6]>,
  SchedVar<R52LMAddrPred5, [R52WriteSTM7]>,
  SchedVar<R52LMAddrPred6, [R52WriteSTM7]>,
  SchedVar<R52LMAddrPred7, [R52WriteSTM8]>,
  SchedVar<R52LMAddrPred8, [R52WriteSTM8]>,
  SchedVar<R52LMAddrPred9,  [R52WriteSTM9]>,
  SchedVar<R52LMAddrPred10, [R52WriteSTM9]>,
  SchedVar<R52LMAddrPred11, [R52WriteSTM10]>,
  SchedVar<R52LMAddrPred12, [R52WriteSTM10]>,
  SchedVar<R52LMAddrPred13, [R52WriteSTM11]>,
  SchedVar<R52LMAddrPred14, [R52WriteSTM11]>,
  SchedVar<R52LMAddrPred15, [R52WriteSTM12]>,
  SchedVar<R52LMAddrPred16, [R52WriteSTM12]>,
  // unknown number of registers, just use resources for two
  SchedVar<NoSchedPred,      [R52WriteSTM6]>
]>;

// Vector Load/Stores. Can issue only in slot-0. Can dual-issue with
// another instruction in slot-1, but only in the last issue.
def : WriteRes<WriteVLD1, [R52UnitLd]> { let Latency = 5;}
def : WriteRes<WriteVLD2, [R52UnitLd]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2];
  let SingleIssue = 1;
}
def : WriteRes<WriteVLD3, [R52UnitLd]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [3];
  let SingleIssue = 1;
}
def : WriteRes<WriteVLD4, [R52UnitLd]> {
  let Latency = 8;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [4];
  let SingleIssue = 1;
}
def R52WriteVST1Mem  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ReleaseAtCycles = [1];
}
def R52WriteVST2Mem  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2];
}
def R52WriteVST3Mem  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [3];
}
def R52WriteVST4Mem  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 8;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [4];
}
def R52WriteVST5Mem  : SchedWriteRes<[R52UnitLd]> {
  let Latency = 9;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [5];
}


def : InstRW<[R52WriteFPALU_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "VABA(u|s)(v8i8|v4i16|v2i32)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "VABA(u|s)(v16i8|v8i16|v4i32)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "VABAL(u|s)(v8i16|v4i32|v2i64)")>;

def : InstRW<[R52WriteFPALU_F4, R52Read_F1, R52Read_F1], (instregex "VABD(u|s)(v8i8|v4i16|v2i32)")>;
def : InstRW<[R52Write2FPALU_F4, R52Read_F1, R52Read_F1], (instregex "VABD(u|s)(v16i8|v8i16|v4i32)")>;
def : InstRW<[R52Write2FPALU_F4, R52Read_F1, R52Read_F1], (instregex "VABDL(u|s)(v16i8|v8i16|v4i32)")>;

def : InstRW<[R52Write2FPALU_F4, R52Read_F1], (instregex "VABS(v16i8|v8i16|v4i32)")>;

def : InstRW<[R52WriteFPALU_F4, R52Read_F2, R52Read_F2],
                               (instregex "(VADD|VSUB)(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[R52Write2FPALU_F4, R52Read_F2, R52Read_F2],
                                (instregex "(VADD|VSUB)(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F2, R52Read_F2],
                               (instregex "(VADDHN|VRADDHN|VSUBHN|VRSUBHN)(v8i8|v4i16|v2i32)")>;

def : InstRW<[R52Write2FPALU_F4, R52Read_F1, R52Read_F1],
                                            (instregex "VADDL", "VADDW", "VSUBL", "VSUBW")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F2, R52Read_F2], (instregex "(VAND|VBIC|VEOR)d")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F2, R52Read_F2], (instregex "(VAND|VBIC|VEOR)q")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F2], (instregex "VBICi(v4i16|v2i32)")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F2], (instregex "VBICi(v8i16|v4i32)")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F2, R52Read_F2], (instregex "(VBIF|VBIT|VBSL|VBSP)d")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F1, R52Read_F2, R52Read_F2], (instregex "(VBIF|VBIT|VBSL|VBSP)q")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F1],
      (instregex "(VCEQ|VCGE|VCGT|VCLE|VCLT|VCLZ|VCMP|VCMPE|VCNT)")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F1, R52Read_F1],
      (instregex "VCVT", "VSITO", "VUITO", "VTO")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_ISS], (instregex "VDUP(8|16|32)d")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_ISS], (instregex "VDUP(8|16|32)q")>;
def : InstRW<[R52WriteFPALU_F3, R52Read_F1], (instregex "VDUPLN(8|16|32)d")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F1], (instregex "VDUPLN(8|16|32)q")>;

def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F1], (instregex "VEXTd(8|16|32)", "VSEL")>;
def : InstRW<[R52Write2FPALU_F3, R52Read_F1, R52Read_F1], (instregex "VEXTq(8|16|32|64)")>;

def : InstRW<[R52WriteFPMAC_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "(VFMA|VFMS)(f|h)d")>;
def : InstRW<[R52Write2FPMAC_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "(VFMA|VFMS)(f|h)q")>;

def : InstRW<[R52WriteFPALU_F4, R52Read_F2, R52Read_F2], (instregex "(VHADD|VHSUB)(u|s)(v8i8|v4i16|v2i32)")>;
def : InstRW<[R52Write2FPALU_F4, R52Read_F2, R52Read_F2], (instregex "(VHADD|VHSUB)(u|s)(v16i8|v8i16|v4i32)")>;

def : InstRW<[R52WriteVLDM], (instregex "VLDM[SD](IA|DB)$")>;
def : InstRW<[R52WriteFPALU_F4, R52Read_F1, R52Read_F1], (instregex "VMAX", "VMIN", "VPMAX", "VPMIN")>;
def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F1], (instregex "VORR", "VORN", "VREV")>;
def : InstRW<[R52WriteNoRSRC_WRI], (instregex "VMRS")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F1], (instregex "VNEG")>;
def : InstRW<[R52WriteFPALU_F4, R52Read_F1, R52Read_F1], (instregex "VPADDi")>;
def : InstRW<[R52Write2FPALU_F4, R52Read_F1, R52Read_F1], (instregex "VPADAL", "VPADDL")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F1], (instregex "VQABS(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F1], (instregex "VQABS(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F2, R52Read_F2],
                  (instregex "(VQADD|VQSUB)(u|s)(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[R52Write2FPALU_F5, R52Read_F2, R52Read_F2],
                  (instregex "(VQADD|VQSUB)(u|s)(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[R52Write2FPMAC_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "VQDMLAL", "VQDMLSL")>;
def : InstRW<[R52WriteFPMUL_F5, R52Read_F1, R52Read_F1, R52Read_F1], (instregex "VQDMUL","VQRDMUL")>;
def : InstRW<[R52WriteFPALU_F5, R52Read_F1, R52Read_F1],
                 (instregex "VQMOVN", "VQNEG", "VQSHL", "VQSHRN")>;
def : InstRW<[R52WriteFPALU_F4, R52Read_F1, R52Read_F1], (instregex "VRSHL", "VRSHR", "VRSHRN", "VTB")>;
def : InstRW<[R52WriteFPALU_F3, R52Read_F1, R52Read_F1], (instregex "VSWP", "VTRN", "VUZP", "VZIP")>;

//---
// VSTx. Vector Stores
//---
// 1-element structure store
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST1q(8|16|32|64)$")>;
def : InstRW<[R52WriteVST3Mem, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)T$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)Q$")>;
def : InstRW<[R52WriteVST3Mem, R52Read_ISS, R52Read_F2], (instregex "VST1d64TPseudo$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST1d64QPseudo$")>;

def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST1LNd(8|16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST1LNdAsm_(8|16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST1LNq(8|16|32)Pseudo$")>;

def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)wb")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1q(8|16|32|64)wb")>;
def : InstRW<[R52WriteVST3Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)Twb")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1d(8|16|32|64)Qwb")>;
def : InstRW<[R52WriteVST3Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1d64TPseudoWB")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1d64QPseudoWB")>;

def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1LNd(8|16|32)_UPD")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1LNdWB_(fixed|register)_Asm_(8|16|32)")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST1LNq(8|16|32)Pseudo_UPD")>;

// 2-element structure store
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST2(d|b)(8|16|32)$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST2q(8|16|32)$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST2q(8|16|32)Pseudo$")>;

def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNd(8|16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNdAsm_(8|16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNd(8|16|32)Pseudo$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNq(16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNqAsm_(16|32)$")>;
def : InstRW<[R52WriteVST1Mem, R52Read_ISS, R52Read_F2], (instregex "VST2LNq(16|32)Pseudo$")>;

def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2(d|b)(8|16|32)wb")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2q(8|16|32)wb")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2q(8|16|32)PseudoWB")>;

def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNd(8|16|32)_UPD")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNdWB_(fixed|register)_Asm_(8|16|32)")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNd(8|16|32)Pseudo_UPD")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNq(16|32)_UPD")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNqWB_(fixed|register)_Asm_(16|32)")>;
def : InstRW<[R52WriteVST1Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST2LNq(16|32)Pseudo_UPD")>;

// 3-element structure store
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST3(d|q)(8|16|32)$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST3(d|q)Asm_(8|16|32)$")>;
def : InstRW<[R52WriteVST4Mem, R52Read_ISS, R52Read_F2], (instregex "VST3d(8|16|32)(oddP|P)seudo$")>;

def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNd(8|16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNdAsm_(8|16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNd(8|16|32)Pseudo$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNq(16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNqAsm_(16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST3LNq(16|32)Pseudo$")>;

def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3(d|q)(8|16|32)_UPD$")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3(d|q)WB_(fixed|register)_Asm_(8|16|32)$")>;
def : InstRW<[R52WriteVST4Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3(d|q)(8|16|32)(oddP|P)seudo_UPD$")>;

def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNd(8|16|32)_UPD$")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNdWB_(fixed|register)_Asm_(8|16|32)")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNd(8|16|32)Pseudo_UPD$")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNq(16|32)_UPD$")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNqWB_(fixed|register)_Asm_(16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST3LNq(16|32)Pseudo_UPD$")>;

// 4-element structure store
def : InstRW<[R52WriteVST5Mem, R52Read_ISS, R52Read_F2], (instregex "VST4(d|q)(8|16|32)$")>;
def : InstRW<[R52WriteVST5Mem, R52Read_ISS, R52Read_F2], (instregex "VST4(d|q)Asm_(8|16|32)$")>;
def : InstRW<[R52WriteVST5Mem, R52Read_ISS, R52Read_F2], (instregex "VST4d(8|16|32)Pseudo$")>;

def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNd(8|16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNdAsm_(8|16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNd(8|16|32)Pseudo$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNq(16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNqAsm_(16|32)$")>;
def : InstRW<[R52WriteVST2Mem, R52Read_ISS, R52Read_F2], (instregex "VST4LNq(16|32)Pseudo$")>;

def : InstRW<[R52WriteVST5Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4(d|q)(8|16|32)_UPD")>;
def : InstRW<[R52WriteVST5Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4(d|q)WB_(fixed|register)_Asm_(8|16|32)")>;
def : InstRW<[R52WriteVST5Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4(d|q)(8|16|32)(oddP|P)seudo_UPD")>;

def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNd(8|16|32)_UPD")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNdWB_(fixed|register)_Asm_(8|16|32)")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNd(8|16|32)Pseudo_UPD")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNq(16|32)_UPD")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNqWB_(fixed|register)_Asm_(16|32)")>;
def : InstRW<[R52WriteVST2Mem, R52WriteAdr, R52Read_ISS, R52Read_F2], (instregex "VST4LNq(16|32)Pseudo_UPD")>;

} // R52 SchedModel
