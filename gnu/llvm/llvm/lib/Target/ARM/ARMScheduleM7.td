//=- ARMScheduleM7.td - ARM Cortex-M7 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SchedRead/Write data for the ARM Cortex-M7 processor.
//
//===----------------------------------------------------------------------===//

def CortexM7Model : SchedMachineModel {
  let IssueWidth = 2;        // Dual issue for most instructions.
  let MicroOpBufferSize = 0; // The Cortex-M7 is in-order.
  let LoadLatency = 2;       // Best case for load-use case.
  let MispredictPenalty = 4; // Mispredict cost for forward branches is 6,
                             // but 4 works better
  let CompleteModel = 0;
}

let SchedModel = CortexM7Model in {

//===--------------------------------------------------------------------===//
// The Cortex-M7 has two ALU, two LOAD, a STORE, a MAC, a BRANCH and a VFP
// pipe. The stages relevant to scheduling are as follows:
//
//   EX1: address generation  shifts
//   EX2: fast load data      ALUs                  FP operation
//   EX3: slow load data      integer writeback     FP operation
//   EX4: store data                                FP writeback
//
// There are shifters in both EX1 and EX2, and some instructions can be
// flexibly allocated between them.  EX2 is used as the "zero" point
// for scheduling, so simple ALU operations executing in EX2 will have
// ReadAdvance<0> (the default) for their source operands and Latency = 1.

def M7UnitLoadL  : ProcResource<1> { let BufferSize = 0; }
def M7UnitLoadH  : ProcResource<1> { let BufferSize = 0; }
def M7UnitLoad   : ProcResGroup<[M7UnitLoadL,M7UnitLoadH]> { let BufferSize = 0; }
def M7UnitStore  : ProcResource<1> { let BufferSize = 0; }
def M7UnitALU    : ProcResource<2>;
def M7UnitShift1 : ProcResource<1> { let BufferSize = 0; }
def M7UnitShift2 : ProcResource<1> { let BufferSize = 0; }
def M7UnitMAC    : ProcResource<1> { let BufferSize = 0; }
def M7UnitBranch : ProcResource<1> { let BufferSize = 0; }
def M7UnitVFP    : ProcResource<1> { let BufferSize = 0; }
def M7UnitVPortL : ProcResource<1> { let BufferSize = 0; }
def M7UnitVPortH : ProcResource<1> { let BufferSize = 0; }
def M7UnitVPort  : ProcResGroup<[M7UnitVPortL,M7UnitVPortH]> { let BufferSize = 0; }
def M7UnitSIMD   : ProcResource<1> { let BufferSize = 0; }

//===---------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types with map ProcResources and set latency.

def : WriteRes<WriteALU, [M7UnitALU]> { let Latency = 1; }

// Basic ALU with shifts.
let Latency = 1 in {
  def : WriteRes<WriteALUsi,  [M7UnitALU, M7UnitShift1]>;
  def : WriteRes<WriteALUsr,  [M7UnitALU, M7UnitShift1]>;
  def : WriteRes<WriteALUSsr, [M7UnitALU, M7UnitShift1]>;
}

// Compares.
def : WriteRes<WriteCMP,   [M7UnitALU]> { let Latency = 1; }
def : WriteRes<WriteCMPsi, [M7UnitALU, M7UnitShift1]> { let Latency = 2; }
def : WriteRes<WriteCMPsr, [M7UnitALU, M7UnitShift1]> { let Latency = 2; }

// Multiplies.
let Latency = 2 in {
  def : WriteRes<WriteMUL16,   [M7UnitMAC]>;
  def : WriteRes<WriteMUL32,   [M7UnitMAC]>;
  def : WriteRes<WriteMUL64Lo, [M7UnitMAC]>;
  def : WriteRes<WriteMUL64Hi, []> { let NumMicroOps = 0; }
}

// Multiply-accumulates.
let Latency = 2 in {
  def : WriteRes<WriteMAC16,   [M7UnitMAC]>;
  def : WriteRes<WriteMAC32,   [M7UnitMAC]>;
  def : WriteRes<WriteMAC64Lo, [M7UnitMAC]> { let Latency = 2; }
  def : WriteRes<WriteMAC64Hi, []> { let NumMicroOps = 0; }
}

// Divisions.
// These cannot be dual-issued with any instructions.
def : WriteRes<WriteDIV, [M7UnitALU]> {
  let Latency = 7;
  let SingleIssue = 1;
}

// Loads/Stores.
def : WriteRes<WriteLd,    [M7UnitLoad]> { let Latency = 1; }
def : WriteRes<WritePreLd, [M7UnitLoad]> { let Latency = 2; }
def : WriteRes<WriteST,    [M7UnitStore]> { let Latency = 2; }

// Branches.
def : WriteRes<WriteBr,    [M7UnitBranch]> { let Latency = 2; }
def : WriteRes<WriteBrL,   [M7UnitBranch]> { let Latency = 2; }
def : WriteRes<WriteBrTbl, [M7UnitBranch]> { let Latency = 2; }

// Noop.
def : WriteRes<WriteNoop, []> { let Latency = 0; }

//===---------------------------------------------------------------------===//
// Sched definitions for floating-point instructions
//
// Floating point conversions.
def : WriteRes<WriteFPCVT, [M7UnitVFP, M7UnitVPort]> { let Latency = 3; }
def : WriteRes<WriteFPMOV, [M7UnitVPort]>            { let Latency = 3; }
def M7WriteFPMOV64 : SchedWriteRes<[M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 3;
}

// The FP pipeline has a latency of 3 cycles.
// ALU operations (32/64-bit).  These go down the FP pipeline.
def : WriteRes<WriteFPALU32, [M7UnitVFP, M7UnitVPort]>  { let Latency = 3; }
def : WriteRes<WriteFPALU64, [M7UnitVFP, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 4;
  let BeginGroup = 1;
}

// Multiplication
def : WriteRes<WriteFPMUL32, [M7UnitVFP, M7UnitVPort]> { let Latency = 3; }
def : WriteRes<WriteFPMUL64, [M7UnitVFP, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 7;
  let BeginGroup = 1;
}

// Multiply-accumulate.  FPMAC goes down the FP Pipeline.
def : WriteRes<WriteFPMAC32, [M7UnitVFP, M7UnitVPort]> { let Latency = 6; }
def : WriteRes<WriteFPMAC64, [M7UnitVFP, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 11;
  let BeginGroup = 1;
}

// Division.   Effective scheduling latency is 3, though real latency is larger
def : WriteRes<WriteFPDIV32, [M7UnitVFP, M7UnitVPort]>  { let Latency = 16; }
def : WriteRes<WriteFPDIV64, [M7UnitVFP, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 30;
  let BeginGroup = 1;
}

// Square-root.  Effective scheduling latency is 3; real latency is larger
def : WriteRes<WriteFPSQRT32, [M7UnitVFP, M7UnitVPort]> { let Latency = 16; }
def : WriteRes<WriteFPSQRT64, [M7UnitVFP, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 30;
  let BeginGroup = 1;
}

def M7WriteShift2   : SchedWriteRes<[M7UnitALU, M7UnitShift2]> {}

// Not used for M7, but needing definitions anyway
def : WriteRes<WriteVLD1, []>;
def : WriteRes<WriteVLD2, []>;
def : WriteRes<WriteVLD3, []>;
def : WriteRes<WriteVLD4, []>;
def : WriteRes<WriteVST1, []>;
def : WriteRes<WriteVST2, []>;
def : WriteRes<WriteVST3, []>;
def : WriteRes<WriteVST4, []>;

def M7SingleIssue : SchedWriteRes<[]> {
  let SingleIssue = 1;
  let NumMicroOps = 0;
}
def M7Slot0Only   : SchedWriteRes<[]> {
  let BeginGroup = 1;
  let NumMicroOps = 0;
}

// What pipeline stage operands need to be ready for depending on
// where they come from.
def : ReadAdvance<ReadALUsr, 0>;
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 1>;
def : ReadAdvance<ReadALU, 0>;
def : ReadAdvance<ReadFPMUL, 0>;
def : ReadAdvance<ReadFPMAC, 3>;
def M7Read_ISS : SchedReadAdvance<-1>;     // operands needed at EX1
def M7Read_EX2   : SchedReadAdvance<1>;    // operands needed at EX3
def M7Read_EX3   : SchedReadAdvance<2>;    // operands needed at EX4

// Non general purpose instructions may not be dual issued. These
// use both issue units.
def M7NonGeneralPurpose : SchedWriteRes<[]> {
  // Assume that these will go down the main ALU pipeline.
  // In reality, many look likely to stall the whole pipeline.
  let Latency = 3;
  let SingleIssue = 1;
}

// List the non general purpose instructions.
def : InstRW<[M7NonGeneralPurpose], (instregex "t2MRS", "tSVC", "tBKPT",
                                     "t2MSR", "t2DMB", "t2DSB", "t2ISB",
                                     "t2HVC", "t2SMC", "t2UDF", "ERET",
                                     "tHINT", "t2HINT", "t2CLREX", "BUNDLE")>;

//===---------------------------------------------------------------------===//
// Sched definitions for load/store
//
// Mark whether the loads/stores must be single-issue
// Address operands are needed earlier
// Data operands are needed later

def M7BaseUpdate : SchedWriteRes<[]> {
    let Latency = 0; // Update is bypassable out of EX1
    let NumMicroOps = 0;
}
def M7LoadLatency1 : SchedWriteRes<[]> {
    let Latency = 1;
    let NumMicroOps = 0;
}
def M7SlowLoad : SchedWriteRes<[M7UnitLoad]>            { let Latency = 2; }

// Byte and half-word loads should have greater latency than other loads.
// So should load exclusive.

def : InstRW<[M7SlowLoad],
      (instregex "t2LDR(B|H|SB|SH)pc")>;
def : InstRW<[M7SlowLoad, M7Read_ISS],
      (instregex "t2LDR(B|H|SB|SH)T", "t2LDR(B|H|SB|SH)i",
                 "tLDR(B|H)i")>;
def : InstRW<[M7SlowLoad, M7Read_ISS, M7Read_ISS],
      (instregex "t2LDR(B|H|SB|SH)s", "tLDR(B|H)r", "tLDR(SB|SH)")>;
def : InstRW<[M7SlowLoad, M7BaseUpdate, M7Read_ISS],
      (instregex "t2LDR(B|H|SB|SH)_(POST|PRE)")>;

// Exclusive loads/stores cannot be dual-issued
def : InstRW<[WriteLd, M7Slot0Only, M7Read_ISS],
      (instregex "t2LDREX$")>;
def : InstRW<[M7SlowLoad, M7Slot0Only, M7Read_ISS],
      (instregex "t2LDREX(B|H)")>;
def : InstRW<[WriteST, M7SingleIssue, M7Read_EX2, M7Read_ISS],
      (instregex "t2STREX(B|H)?$")>;

// Load/store multiples cannot be dual-issued.  Note that default scheduling
// occurs around read/write times of individual registers in the list; read
// time for STM cannot be overridden because it is a variadic source operand.

def : InstRW<[WriteLd, M7SingleIssue, M7Read_ISS],
      (instregex "(t|t2)LDM(DB|IA)$")>;
def : InstRW<[WriteST, M7SingleIssue, M7Read_ISS],
      (instregex "(t|t2)STM(DB|IA)$")>;
def : InstRW<[M7BaseUpdate, WriteLd, M7SingleIssue, M7Read_ISS],
      (instregex "(t|t2)LDM(DB|IA)_UPD$", "tPOP")>;
def : InstRW<[M7BaseUpdate, WriteST, M7SingleIssue, M7Read_ISS],
      (instregex "(t|t2)STM(DB|IA)_UPD$", "tPUSH")>;

// Load/store doubles cannot be dual-issued.

def : InstRW<[M7BaseUpdate, WriteST, M7SingleIssue,
              M7Read_EX2, M7Read_EX2, M7Read_ISS],
      (instregex "t2STRD_(PRE|POST)")>;
def : InstRW<[WriteST, M7SingleIssue, M7Read_EX2, M7Read_EX2, M7Read_ISS],
      (instregex "t2STRDi")>;
def : InstRW<[WriteLd, M7LoadLatency1, M7SingleIssue, M7BaseUpdate, M7Read_ISS],
      (instregex "t2LDRD_(PRE|POST)")>;
def : InstRW<[WriteLd, M7LoadLatency1, M7SingleIssue, M7Read_ISS],
      (instregex "t2LDRDi")>;

// Word load / preload
def : InstRW<[WriteLd],
      (instregex "t2LDRpc", "t2PL[DI]pci", "tLDRpci")>;
def : InstRW<[WriteLd, M7Read_ISS],
      (instregex "t2LDR(i|T)", "t2PL[DI](W)?i", "tLDRi", "tLDRspi")>;
def : InstRW<[WriteLd, M7Read_ISS, M7Read_ISS],
      (instregex "t2LDRs", "t2PL[DI](w)?s", "tLDRr")>;
def : InstRW<[WriteLd, M7BaseUpdate, M7Read_ISS],
      (instregex "t2LDR_(POST|PRE)")>;

// Stores
def : InstRW<[M7BaseUpdate, WriteST, M7Read_EX2, M7Read_ISS],
      (instregex "t2STR(B|H)?_(POST|PRE)")>;
def : InstRW<[WriteST, M7Read_EX2, M7Read_ISS, M7Read_ISS],
      (instregex "t2STR(B|H)?s$", "tSTR(B|H)?r$")>;
def : InstRW<[WriteST, M7Read_EX2, M7Read_ISS],
      (instregex "t2STR(B|H)?(i|T)", "tSTR(B|H)?i$", "tSTRspi")>;

// TBB/TBH - single-issue only; takes two cycles to issue

def M7TableLoad : SchedWriteRes<[M7UnitLoad]> {
  let NumMicroOps = 2;
  let SingleIssue = 1;
}

def : InstRW<[M7TableLoad, M7Read_ISS, M7Read_ISS], (instregex "t2TB")>;

// VFP loads and stores

def M7LoadSP  : SchedWriteRes<[M7UnitLoad, M7UnitVPort]> { let Latency = 1; }
def M7LoadDP  : SchedWriteRes<[M7UnitLoadL, M7UnitLoadH, M7UnitVPortL, M7UnitVPortH]> {
  let Latency = 2;
  let SingleIssue = 1;
}
def M7StoreSP : SchedWriteRes<[M7UnitStore, M7UnitVPort]>;
def M7StoreDP : SchedWriteRes<[M7UnitStore, M7UnitVPortL, M7UnitVPortH]> {
  let SingleIssue = 1;
}

def : InstRW<[M7LoadSP, M7Read_ISS],                 (instregex "VLDR(S|H)$")>;
def : InstRW<[M7LoadDP, M7Read_ISS],                 (instregex "VLDRD$")>;
def : InstRW<[M7StoreSP, M7Read_EX3, M7Read_ISS],    (instregex "VSTR(S|H)$")>;
def : InstRW<[M7StoreDP, M7Read_EX3, M7Read_ISS],    (instregex "VSTRD$")>;

// Load/store multiples cannot be dual-issued.

def : InstRW<[WriteLd, M7SingleIssue, M7Read_ISS],
      (instregex "VLDM(S|D|Q)(DB|IA)$")>;
def : InstRW<[WriteST, M7SingleIssue, M7Read_ISS],
      (instregex "VSTM(S|D|Q)(DB|IA)$")>;
def : InstRW<[M7BaseUpdate, WriteLd, M7SingleIssue, M7Read_ISS],
      (instregex "VLDM(S|D|Q)(DB|IA)_UPD$")>;
def : InstRW<[M7BaseUpdate, WriteST, M7SingleIssue, M7Read_ISS],
      (instregex "VSTM(S|D|Q)(DB|IA)_UPD$")>;

//===---------------------------------------------------------------------===//
// Sched definitions for ALU
//

// Shifted ALU operands are read a cycle early.
def M7Ex1ReadNoFastBypass : SchedReadAdvance<-1, [WriteLd, M7LoadLatency1]>;

def : InstRW<[WriteALUsi, M7Ex1ReadNoFastBypass, M7Read_ISS],
             (instregex "t2(ADC|ADDS|ADD|BIC|EOR|ORN|ORR|RSBS|RSB|SBC|SUBS)rs$",
                        "t2(SUB|CMP|CMNz|TEQ|TST)rs$",
                        "t2MOVsr(a|l)")>;
def : InstRW<[WriteALUsi, M7Read_ISS],
             (instregex "t2MVNs")>;

// Treat pure shift operations (except for RRX) as if they used the EX1
// shifter but have timing as if they used the EX2 shifter as they usually
// can choose the EX2 shifter when needed.  Will miss a few dual-issue cases,
// but the results prove to be better than trying to get them exact.

def : InstRW<[M7WriteShift2, M7Read_ISS], (instregex "t2RRX$")>;
def : InstRW<[WriteALUsi], (instregex "(t|t2)(LSL|LSR|ASR|ROR)")>;

// Instructions that use the shifter, but have normal timing.

def : InstRW<[WriteALUsi,M7Slot0Only], (instregex "t2(BFC|BFI)$")>;

// Instructions which are slot zero only but otherwise normal.

def : InstRW<[WriteALU, M7Slot0Only], (instregex "t2CLZ")>;

// MAC operations that don't have SchedRW set.

def : InstRW<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC], (instregex "t2SML[AS]D")>;

// Divides are special because they stall for their latency, and so look like a
// single-cycle as far as scheduling opportunities go.  By putting WriteALU
// first, we make the operand latency 1, but keep the instruction latency 7.

def : InstRW<[WriteALU, WriteDIV], (instregex "t2(S|U)DIV")>;

// DSP extension operations

def M7WriteSIMD1   : SchedWriteRes<[M7UnitSIMD, M7UnitALU]> {
  let Latency = 1;
  let BeginGroup = 1;
}
def M7WriteSIMD2   : SchedWriteRes<[M7UnitSIMD, M7UnitALU]> {
  let Latency = 2;
  let BeginGroup = 1;
}
def M7WriteShSIMD1 : SchedWriteRes<[M7UnitSIMD, M7UnitALU, M7UnitShift1]> {
  let Latency = 1;
  let BeginGroup = 1;
}
def M7WriteShSIMD0 : SchedWriteRes<[M7UnitSIMD, M7UnitALU, M7UnitShift1]> {
  let Latency = 0;      // Bypassable out of EX1
  let BeginGroup = 1;
}
def M7WriteShSIMD2 : SchedWriteRes<[M7UnitSIMD, M7UnitALU, M7UnitShift1]> {
  let Latency = 2;
  let BeginGroup = 1;
}

def : InstRW<[M7WriteShSIMD2, M7Read_ISS],
             (instregex "t2(S|U)SAT")>;
def : InstRW<[M7WriteSIMD1, ReadALU],
             (instregex "(t|t2)(S|U)XT(B|H)")>;
def : InstRW<[M7WriteSIMD1, ReadALU, ReadALU],
             (instregex "t2(S|SH|U|UH)(ADD16|ADD8|ASX|SAX|SUB16|SUB8)",
                        "t2SEL")>;
def : InstRW<[M7WriteSIMD2, ReadALU, ReadALU],
             (instregex "t2(Q|UQ)(ADD|ASX|SAX|SUB)", "t2USAD8")>;
def : InstRW<[M7WriteShSIMD2, M7Read_ISS, M7Read_ISS],
             (instregex "t2QD(ADD|SUB)")>;
def : InstRW<[M7WriteShSIMD0, M7Read_ISS],
             (instregex "t2(RBIT|REV)", "tREV")>;
def : InstRW<[M7WriteShSIMD1, M7Read_ISS],
             (instregex "t2(SBFX|UBFX)")>;
def : InstRW<[M7WriteShSIMD1, ReadALU, M7Read_ISS],
             (instregex "t2PKH(BT|TB)", "t2(S|U)XTA")>;
def : InstRW<[M7WriteSIMD2, ReadALU, ReadALU, M7Read_EX2],
             (instregex "t2USADA8")>;

// MSR/MRS
def : InstRW<[M7NonGeneralPurpose], (instregex "MSR", "MRS")>;

//===---------------------------------------------------------------------===//
// Sched definitions for FP operations
//

// Effective scheduling latency is really 3 for nearly all FP operations,
// even if their true latency is higher.
def M7WriteVFPLatOverride : SchedWriteRes<[]> {
  let Latency = 3;
  let NumMicroOps = 0;
}
def M7WriteVFPExtraVPort  : SchedWriteRes<[M7UnitVPort]> {
  let Latency = 3;
  let NumMicroOps = 0;
}

// Instructions which are missing default schedules.
def : InstRW<[WriteFPALU32],
             (instregex "V(ABS|CVT.*|NEG|FP_VMAX.*|FP_VMIN.*|RINT.*)S$")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPALU64],
             (instregex "V(ABS|CVT.*|NEG|FP_VMAX.*|FP_VMIN.*|RINT.*)D$")>;

// VCMP
def M7WriteVCMPS : SchedWriteRes<[M7UnitVFP, M7UnitVPort]> { let Latency = 0; }
def M7WriteVCMPD : SchedWriteRes<[M7UnitVFP, M7UnitVPort, M7UnitVPort]> {
  let Latency = 0;
  let BeginGroup = 1;
}
def : InstRW<[M7WriteVCMPS], (instregex "VCMPS$")>;
def : InstRW<[M7WriteVCMPD], (instregex "VCMPD$")>;

    // VMRS/VMSR
def M7VMRS : SchedWriteRes<[M7UnitVFP, M7UnitVPort]> { let SingleIssue = 1; }
def M7VMSR : SchedWriteRes<[M7UnitVFP, M7UnitVPort]> { let SingleIssue = 1; }
def : InstRW<[M7VMRS], (instregex "FMSTAT")>;
def : InstRW<[M7VMSR], (instregex "VMSR")>;

// VSEL cannot bypass in its implied $cpsr operand; model as earlier read
def : InstRW<[WriteFPALU32, M7Slot0Only, ReadALU, ReadALU, M7Read_ISS],
             (instregex "VSEL.*S$")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPALU64, M7Slot0Only,
              ReadALU, ReadALU, M7Read_ISS],
             (instregex "VSEL.*D$")>;

// VMOV
def : InstRW<[WriteFPMOV],
             (instregex "VMOV(H|S)$", "FCONST(H|S)")>;
def : InstRW<[WriteFPMOV, M7WriteVFPExtraVPort, M7Slot0Only],
             (instregex "VMOVD$")>;
def : InstRW<[WriteFPMOV, M7WriteVFPExtraVPort, M7Slot0Only],
             (instregex "FCONSTD")>;
def : InstRW<[WriteFPMOV, M7WriteVFPExtraVPort, M7SingleIssue],
             (instregex "VMOV(DRR|RRD|RRS|SRR)")>;

// Larger-latency overrides.

def : InstRW<[M7WriteVFPLatOverride, WriteFPDIV32],  (instregex "VDIVS")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPDIV64],  (instregex "VDIVD")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPSQRT32], (instregex "VSQRTS")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPSQRT64], (instregex "VSQRTD")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPMUL64],
             (instregex "V(MUL|NMUL)D")>;
def : InstRW<[M7WriteVFPLatOverride, WriteFPALU64],
             (instregex "V(ADD|SUB)D")>;

// Multiply-accumulate.  Chained SP timing is correct; rest need overrides
// Double-precision chained MAC stalls the pipeline behind it for 3 cycles,
// making it appear to have 3 cycle latency for scheduling.

def : InstRW<[M7WriteVFPLatOverride, WriteFPMAC64,
              ReadFPMAC, ReadFPMUL, ReadFPMUL],
             (instregex "V(N)?ML(A|S)D$")>;

// Single-precision fused MACs look like latency 5 with advance of 2.

def M7WriteVFPLatOverride5 : SchedWriteRes<[]> {
  let Latency = 5;
  let NumMicroOps = 0;
}
def M7ReadFPMAC2   : SchedReadAdvance<2>;

def : InstRW<[M7WriteVFPLatOverride5, WriteFPMAC32,
              M7ReadFPMAC2, ReadFPMUL, ReadFPMUL],
             (instregex "VF(N)?M(A|S)S$")>;

// Double-precision fused MAC stalls the pipeline behind it for 2 cycles, making
// it appear to have 3 cycle latency for scheduling.

def : InstRW<[M7WriteVFPLatOverride, WriteFPMAC64,
              ReadFPMAC, ReadFPMUL, ReadFPMUL],
             (instregex "VF(N)?M(A|S)D$")>;

}  // SchedModel = CortexM7Model
