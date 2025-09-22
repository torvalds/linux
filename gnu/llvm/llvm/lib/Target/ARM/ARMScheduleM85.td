//=- ARMScheduleM85.td - ARM Cortex-M85 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the ARM Cortex-M85 processor.
//
// All timing is referred to EX2.  Thus, operands which are needed at EX1 are
// stated to have a ReadAdvance of -1.  The FP/MVE pipe actually begins at EX3
// but is described as if it were in EX2 to avoid having unnaturally long latencies
// with delayed inputs on every instruction.  Instead, whenever an FP instruction
// must access a GP register or a non-FP instruction (which includes loads/stores)
// must access an FP register, the operand timing is adjusted:
//     FP accessing GPR:     read one cycle later, write one cycle later
//                           NOTE: absolute spec timing already includes this if
//                                 referenced to EX2
//     non-FP accessing FPR: read one cycle earlier, write one cycle earlier
//===----------------------------------------------------------------------===//

def CortexM85Model : SchedMachineModel {
  let IssueWidth = 2;        // Dual issue for most instructions.
  let MicroOpBufferSize = 0; // M85 is in-order.
  let LoadLatency = 2;       // Best case for load-use case.
  let MispredictPenalty = 4; // Mispredict cost for forward branches is 7,
                             // but 4 works better
  let CompleteModel = 0;
}

let SchedModel = CortexM85Model in {

//===--------------------------------------------------------------------===//
// CortexM85 has two ALU, two LOAD, two STORE, a MAC, a BRANCH and two VFP
// pipes (with three units).  There are three shifters available: one per
// stage.

def M85UnitLoadL  : ProcResource<1> { let BufferSize = 0; }
def M85UnitLoadH  : ProcResource<1> { let BufferSize = 0; }
def M85UnitLoad   : ProcResGroup<[M85UnitLoadL,M85UnitLoadH]> { let BufferSize = 0; }
def M85UnitStoreL : ProcResource<1> { let BufferSize = 0; }
def M85UnitStoreH : ProcResource<1> { let BufferSize = 0; }
def M85UnitStore  : ProcResGroup<[M85UnitStoreL,M85UnitStoreH]> { let BufferSize = 0; }
def M85UnitALU    : ProcResource<2> { let BufferSize = 0; }
def M85UnitShift1 : ProcResource<1> { let BufferSize = 0; }
def M85UnitShift2 : ProcResource<1> { let BufferSize = 0; }
def M85UnitMAC    : ProcResource<1> { let BufferSize = 0; }
def M85UnitBranch : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPAL  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPAH  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPA   : ProcResGroup<[M85UnitVFPAL,M85UnitVFPAH]> { let BufferSize = 0; }
def M85UnitVFPBL  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPBH  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPB   : ProcResGroup<[M85UnitVFPBL,M85UnitVFPBH]> { let BufferSize = 0; }
def M85UnitVFPCL  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPCH  : ProcResource<1> { let BufferSize = 0; }
def M85UnitVFPC   : ProcResGroup<[M85UnitVFPCL,M85UnitVFPCH]> { let BufferSize = 0; }
def M85UnitVFPD   : ProcResource<1> { let BufferSize = 0; }
def M85UnitVPortL : ProcResource<1> { let BufferSize = 0; }
def M85UnitVPortH : ProcResource<1> { let BufferSize = 0; }
def M85UnitVPort  : ProcResGroup<[M85UnitVPortL,M85UnitVPortH]> { let BufferSize = 0; }
def M85UnitSIMD   : ProcResource<1> { let BufferSize = 0; }
def M85UnitLShift : ProcResource<1> { let BufferSize = 0; }
def M85UnitDiv    : ProcResource<1> { let BufferSize = 0; }

def M85UnitSlot0 : ProcResource<1> { let BufferSize = 0; }

//===---------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types with map ProcResources and set latency.

def : WriteRes<WriteALU, [M85UnitALU]> { let Latency = 1; }

// Basic ALU with shifts.
let Latency = 1 in {
  def : WriteRes<WriteALUsi,  [M85UnitALU, M85UnitShift1]>;
  def : WriteRes<WriteALUsr,  [M85UnitALU, M85UnitShift1]>;
  def : WriteRes<WriteALUSsr, [M85UnitALU, M85UnitShift1]>;
}

// Compares.
def : WriteRes<WriteCMP,   [M85UnitALU]> { let Latency = 1; }
def : WriteRes<WriteCMPsi, [M85UnitALU, M85UnitShift1]> { let Latency = 2; }
def : WriteRes<WriteCMPsr, [M85UnitALU, M85UnitShift1]> { let Latency = 2; }

// Multiplies.
let Latency = 2 in {
  def : WriteRes<WriteMUL16,   [M85UnitMAC]>;
  def : WriteRes<WriteMUL32,   [M85UnitMAC]>;
  def : WriteRes<WriteMUL64Lo, [M85UnitMAC]>;
  def : WriteRes<WriteMUL64Hi, []> { let NumMicroOps = 0; }
}

// Multiply-accumulates.
let Latency = 2 in {
def : WriteRes<WriteMAC16,   [M85UnitMAC]>;
def : WriteRes<WriteMAC32,   [M85UnitMAC]>;
def : WriteRes<WriteMAC64Lo, [M85UnitMAC]>;
def : WriteRes<WriteMAC64Hi, []> { let NumMicroOps = 0; }
}

// Divisions.
def : WriteRes<WriteDIV, [M85UnitDiv]> {
  let Latency = 7;
}

// Loads/Stores.
def : WriteRes<WriteLd,    [M85UnitLoad]> { let Latency = 1; }
def : WriteRes<WritePreLd, [M85UnitLoad]> { let Latency = 2; }
def : WriteRes<WriteST,    [M85UnitStore]> { let Latency = 2; }
def M85WriteLdWide : SchedWriteRes<[M85UnitLoadL, M85UnitLoadH]> { let Latency = 1; }
def M85WriteStWide : SchedWriteRes<[M85UnitStoreL, M85UnitStoreH]> { let Latency = 2; }

// Branches.
def : WriteRes<WriteBr,    [M85UnitBranch]> { let Latency = 2; }
def : WriteRes<WriteBrL,   [M85UnitBranch]> { let Latency = 2; }
def : WriteRes<WriteBrTbl, [M85UnitBranch]> { let Latency = 2; }

// Noop.
def : WriteRes<WriteNoop, []> { let Latency = 0; let NumMicroOps = 0; }

//===---------------------------------------------------------------------===//
// Sched definitions for floating-point instructions
//
// Floating point conversions.
def : WriteRes<WriteFPCVT, [M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 2;
}
def : WriteRes<WriteFPMOV, [M85UnitVPort, M85UnitSlot0]> { let Latency = 1; }
def M85WriteFPMOV64 : SchedWriteRes<[M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> { let Latency = 1; }

// ALU operations (32/64-bit).  These go down the FP pipeline.
def : WriteRes<WriteFPALU32, [M85UnitVFPA, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 2;
}
def : WriteRes<WriteFPALU64, [M85UnitVFPAL, M85UnitVFPAH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
  let Latency = 6;
}

// Multiplication
def : WriteRes<WriteFPMUL32, [M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 3;
}
def : WriteRes<WriteFPMUL64, [M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
  let Latency = 8;
}

// Multiply-accumulate.  FPMAC goes down the FP Pipeline.
def : WriteRes<WriteFPMAC32, [M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 5;
}
def : WriteRes<WriteFPMAC64, [M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
  let Latency = 14;
}

// Division.   Effective scheduling latency is 3, though real latency is larger
def : WriteRes<WriteFPDIV32, [M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 14;
}
def : WriteRes<WriteFPDIV64, [M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
  let Latency = 29;
}

// Square-root.  Effective scheduling latency is 3, though real latency is larger
def : WriteRes<WriteFPSQRT32, [M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 14;
}
def : WriteRes<WriteFPSQRT64, [M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
  let Latency = 29;
}

let NumMicroOps = 0 in {
  def M85SingleIssue : SchedWriteRes<[]> { let SingleIssue = 1; }
  def M85Slot0Only   : SchedWriteRes<[M85UnitSlot0]> { }
}

// What pipeline stage operands need to be ready for depending on
// where they come from.
def : ReadAdvance<ReadALUsr, 0>;
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 1>;
def : ReadAdvance<ReadALU, 0>;
def : ReadAdvance<ReadFPMUL, 0>;
def : ReadAdvance<ReadFPMAC, 3>;
def M85Read_ISSm1 : SchedReadAdvance<-2>;    // operands needed at ISS
def M85Read_ISS : SchedReadAdvance<-1>;    // operands needed at EX1
def M85Read_EX1 : SchedReadAdvance<0>;     // operands needed at EX2
def M85Read_EX2 : SchedReadAdvance<1>;    // operands needed at EX3
def M85Read_EX3 : SchedReadAdvance<2>;    // operands needed at EX4
def M85Read_EX4 : SchedReadAdvance<3>;    // operands needed at EX5
def M85Write1   : SchedWriteRes<[]> {
  let Latency = 1;
  let NumMicroOps = 0;
}
def M85Write2   : SchedWriteRes<[]> {
  let Latency = 2;
  let NumMicroOps = 0;
}
def M85WriteShift2   : SchedWriteRes<[M85UnitALU, M85UnitShift2]> {}

// Non general purpose instructions may not be dual issued. These
// use both issue units.
def M85NonGeneralPurpose : SchedWriteRes<[]> {
  // Assume that these will go down the main ALU pipeline.
  // In reality, many look likely to stall the whole pipeline.
  let Latency = 3;
  let SingleIssue = 1;
}

// List the non general purpose instructions.
def : InstRW<[M85NonGeneralPurpose],
                (instregex "t2MRS", "tSVC", "tBKPT", "t2MSR", "t2DMB", "t2DSB",
                           "t2ISB", "t2HVC", "t2SMC", "t2UDF", "ERET", "tHINT",
                           "t2HINT", "t2CLREX", "t2CLRM", "BUNDLE")>;

//===---------------------------------------------------------------------===//
// Sched definitions for load/store
//
// Mark whether the loads/stores must be single-issue
// Address operands are needed earlier
// Data operands are needed later

let NumMicroOps = 0 in {
  def M85BaseUpdate : SchedWriteRes<[]> {
    // Update is bypassable out of EX1
    let Latency = 0;
  }
  def M85MVERBaseUpdate : SchedWriteRes<[]> { let Latency = 1; }
  // Q register base update is available in EX3 to bypass into EX2/ISS.
  //  Latency=2 matches what we want for ISS, Latency=1 for EX2.  Going
  //  with 2, as base update into another load/store is most likely.  Could
  //  change later in an override.
  def M85MVEQBaseUpdate : SchedWriteRes<[]> { let Latency = 2; }
  def M85LoadLatency1 : SchedWriteRes<[]> { let Latency = 1; }
}
def M85SlowLoad : SchedWriteRes<[M85UnitLoad]> { let Latency = 2; }

// Byte and half-word loads should have greater latency than other loads.
// So should load exclusive?

def : InstRW<[M85SlowLoad],
               (instregex "t2LDR(B|H|SB|SH)pc")>;
def : InstRW<[M85SlowLoad, M85Read_ISS],
               (instregex "t2LDR(B|H|SB|SH)T", "t2LDR(B|H|SB|SH)i",
                          "tLDRspi", "tLDR(B|H)i")>;
def : InstRW<[M85SlowLoad, M85Read_ISS, M85Read_ISS],
               (instregex "t2LDR(B|H|SB|SH)s")>;
def : InstRW<[M85SlowLoad, M85Read_ISS, M85Read_ISS],
               (instregex "tLDR(B|H)r", "tLDR(SB|SH)")>;
def : InstRW<[M85SlowLoad, M85BaseUpdate, M85Read_ISS],
               (instregex "t2LDR(B|H|SB|SH)_(POST|PRE)")>;

// Exclusive/acquire/release loads/stores cannot be dual-issued
def : InstRW<[WriteLd, M85SingleIssue, M85Read_ISS],
               (instregex "t2LDREX$", "t2LDA(EX)?$")>;
def : InstRW<[M85WriteLdWide, M85LoadLatency1, M85SingleIssue, M85Read_ISS],
               (instregex "t2LDAEXD$")>;
def : InstRW<[M85SlowLoad, M85SingleIssue, M85Read_ISS],
               (instregex "t2LDREX(B|H)", "t2LDA(EX)?(B|H)$")>;
def : InstRW<[WriteST, M85SingleIssue, M85Read_EX2, M85Read_ISS],
               (instregex "t2STREX(B|H)?$", "t2STL(EX)?(B|H)?$")>;
def : InstRW<[M85WriteStWide, M85SingleIssue, M85Read_EX2, M85Read_EX2, M85Read_ISS],
               (instregex "t2STLEXD$")>;

// Load/store multiples end issue groups.

def : InstRW<[M85WriteLdWide, M85SingleIssue, M85Read_ISS],
               (instregex "(t|t2)LDM(DB|IA)$")>;
def : InstRW<[M85WriteStWide, M85SingleIssue, M85Read_ISS],
               (instregex "(t|t2)STM(DB|IA)$")>;
def : InstRW<[M85BaseUpdate, M85WriteLdWide, M85SingleIssue, M85Read_ISS],
               (instregex "(t|t2)LDM(DB|IA)_UPD$", "tPOP")>;
def : InstRW<[M85BaseUpdate, M85WriteStWide, M85SingleIssue, M85Read_ISS],
               (instregex "(t|t2)STM(DB|IA)_UPD$", "tPUSH")>;

// Load/store doubles

def : InstRW<[M85BaseUpdate, M85WriteStWide,
              M85Read_EX2, M85Read_EX2, M85Read_ISS],
               (instregex "t2STRD_(PRE|POST)")>;
def : InstRW<[M85WriteStWide, M85Read_EX2, M85Read_EX2, M85Read_ISS],
               (instregex "t2STRDi")>;
def : InstRW<[M85WriteLdWide, M85LoadLatency1, M85BaseUpdate, M85Read_ISS],
               (instregex "t2LDRD_(PRE|POST)")>;
def : InstRW<[M85WriteLdWide, M85LoadLatency1, M85Read_ISS],
               (instregex "t2LDRDi")>;

// Word load / preload
def : InstRW<[WriteLd],
               (instregex "t2LDRpc", "t2PL[DI]pci", "tLDRpci")>;
def : InstRW<[WriteLd, M85Read_ISS],
               (instregex "t2LDR(i|T)", "t2PL[DI](W)?i", "tLDRi")>;
def : InstRW<[WriteLd, M85Read_ISS, M85Read_ISS],
               (instregex "t2LDRs", "t2PL[DI](w)?s", "tLDRr")>;
def : InstRW<[WriteLd, M85BaseUpdate, M85Read_ISS],
               (instregex "t2LDR_(POST|PRE)")>;

// Stores
def : InstRW<[M85BaseUpdate, WriteST, M85Read_EX2, M85Read_ISS],
               (instregex "t2STR(B|H)?_(POST|PRE)")>;
def : InstRW<[WriteST, M85Read_EX2, M85Read_ISS, M85Read_ISS],
               (instregex "t2STR(B|H)?s$", "tSTR(B|H)?r$")>;
def : InstRW<[WriteST, M85Read_EX2, M85Read_ISS],
               (instregex "t2STR(B|H)?(i|T)", "tSTR(B|H)?i$", "tSTRspi")>;

// TBB/TBH - single-issue only

def M85TableLoad : SchedWriteRes<[M85UnitLoad]> { let SingleIssue = 1; }

def : InstRW<[M85TableLoad, M85Read_ISS, M85Read_ISS],
                (instregex "t2TB")>;

// VFP/MVE loads and stores
//   Note: timing for VLDR/VSTR special has not been broken out
//   Note 2: see notes at top of file for the reason load latency is 1 and
//           store data is in EX3.

def M85LoadSP  : SchedWriteRes<[M85UnitLoad, M85UnitVPort]>;
def M85LoadDP  : SchedWriteRes<[M85UnitLoadL, M85UnitLoadH,
                                M85UnitVPortL, M85UnitVPortH]>;
def M85LoadSys  : SchedWriteRes<[M85UnitLoad, M85UnitVPort,
                                 M85UnitVFPA, M85UnitVFPB, M85UnitVFPC, M85UnitVFPD]> {
  let Latency = 4;
}
def M85StoreSP : SchedWriteRes<[M85UnitStore, M85UnitVPort]>;
def M85StoreDP : SchedWriteRes<[M85UnitStoreL, M85UnitStoreH,
                                M85UnitVPortL, M85UnitVPortH]>;
def M85StoreSys : SchedWriteRes<[M85UnitStore, M85UnitVPort,
                                 M85UnitVFPA, M85UnitVFPB, M85UnitVFPC, M85UnitVFPD]>;
let ReleaseAtCycles = [2,2,1,1], EndGroup = 1 in {
  def M85LoadMVE  : SchedWriteRes<[M85UnitLoadL, M85UnitLoadH,
                                   M85UnitVPortL, M85UnitVPortH]>;
  def M85LoadMVELate  : SchedWriteRes<[M85UnitLoadL, M85UnitLoadH,
                                       M85UnitVPortL, M85UnitVPortH]> {
    let Latency = 4; // 3 cycles later
  }
  def M85StoreMVE : SchedWriteRes<[M85UnitStoreL, M85UnitStoreH,
                                   M85UnitVPortL, M85UnitVPortH]>;
}

def : InstRW<[M85LoadSP, M85Read_ISS], (instregex "VLDR(S|H)$")>;
def : InstRW<[M85LoadSys, M85Read_ISS], (instregex "VLDR_")>;
def : InstRW<[M85LoadDP, M85Read_ISS], (instregex "VLDRD$")>;
def : InstRW<[M85StoreSP, M85Read_EX3, M85Read_ISS], (instregex "VSTR(S|H)$")>;
def : InstRW<[M85StoreSys, M85Read_EX1, M85Read_ISS], (instregex "VSTR_")>;
def : InstRW<[M85StoreDP, M85Read_EX3, M85Read_ISS], (instregex "VSTRD$")>;

def : InstRW<[M85LoadMVELate, M85Read_ISS],
               (instregex "MVE_VLD[24]._[0-9]+$")>;
def : InstRW<[M85LoadMVELate, M85MVERBaseUpdate, M85Read_ISS],
               (instregex "MVE_VLD[24].*wb")>;
def : InstRW<[M85LoadMVE, M85Read_ISS],
               (instregex "MVE_VLDR.*(8|16|32|64)$")>;
def : InstRW<[M85LoadMVE, M85SingleIssue, M85Read_ISS, M85Read_ISS],
               (instregex "MVE_VLDR.*(_rq|_rq|_rq_u)$")>;
def : InstRW<[M85LoadMVE, M85SingleIssue, M85Read_ISS],
               (instregex "MVE_VLDR.*_qi$")>;
def : InstRW<[M85MVERBaseUpdate, M85LoadMVE, M85Read_ISS],
               (instregex "MVE_VLDR.*(_post|[^i]_pre)$")>;
def : InstRW<[M85MVEQBaseUpdate, M85SingleIssue, M85LoadMVE, M85Read_ISS],
               (instregex "MVE_VLDR.*(qi_pre)$")>;

def : InstRW<[M85StoreMVE, M85Read_EX3, M85Read_ISS],
               (instregex "MVE_VST[24]._[0-9]+$")>;
def : InstRW<[M85StoreMVE, M85Read_EX3, M85MVERBaseUpdate, M85Read_ISS],
               (instregex "MVE_VST[24].*wb")>;
def : InstRW<[M85StoreMVE, M85Read_EX3, M85Read_ISS],
               (instregex "MVE_VSTR.*(8|16|32|64)$")>;
def : InstRW<[M85StoreMVE, M85SingleIssue, M85Read_EX3, M85Read_ISS, M85Read_ISS],
               (instregex "MVE_VSTR.*(_rq|_rq|_rq_u)$")>;
def : InstRW<[M85StoreMVE, M85SingleIssue, M85Read_EX3, M85Read_ISS],
               (instregex "MVE_VSTR.*_qi$")>;
def : InstRW<[M85MVERBaseUpdate, M85StoreMVE, M85Read_EX3, M85Read_ISS],
               (instregex "MVE_VSTR.*(_post|[^i]_pre)$")>;
def : InstRW<[M85MVEQBaseUpdate, M85SingleIssue, M85StoreMVE,
              M85Read_EX3, M85Read_ISS],
               (instregex "MVE_VSTR.*(qi_pre)$")>;

// Load/store multiples end issue groups.

def : InstRW<[M85WriteLdWide, M85SingleIssue, M85Read_ISS],
               (instregex "VLDM(S|D|Q)(DB|IA)$")>;
def : InstRW<[M85WriteStWide, M85SingleIssue, M85Read_ISS, M85Read_EX3],
               (instregex "VSTM(S|D|Q)(DB|IA)$")>;
def : InstRW<[M85BaseUpdate, M85WriteLdWide, M85SingleIssue, M85Read_ISS],
               (instregex "VLDM(S|D|Q)(DB|IA)_UPD$", "VLLDM")>;
def : InstRW<[M85BaseUpdate, M85WriteStWide, M85SingleIssue,
              M85Read_ISS, M85Read_EX3],
               (instregex "VSTM(S|D|Q)(DB|IA)_UPD$", "VLSTM")>;

//===---------------------------------------------------------------------===//
// Sched definitions for ALU
//

// Non-small shifted ALU operands are read a cycle early; small LSLs
// aren't, as they don't require the shifter.

def M85NonsmallShiftWrite : SchedWriteRes<[M85UnitALU,M85UnitShift1]> {
  let Latency = 1;
}

def M85WriteALUsi : SchedWriteVariant<[
  SchedVar<NoSchedPred, [M85NonsmallShiftWrite]>
]>;
def M85Ex1ReadNoFastBypass : SchedReadAdvance<-1,
                                   [WriteLd, M85WriteLdWide, M85LoadLatency1]>;
def M85ReadALUsi : SchedReadVariant<[
  SchedVar<NoSchedPred, [M85Read_ISS]>
]>;

def : InstRW<[M85WriteALUsi, M85Read_EX1, M85ReadALUsi],
               (instregex "t2(ADC|ADDS|BIC|EOR|ORN|ORR|RSBS|RSB|SBC|"
                          "SUBS|CMP|CMNz|TEQ|TST)rs$")>;
def : InstRW<[M85WriteALUsi, M85ReadALUsi],
               (instregex "t2MVNs")>;

// CortexM85 treats LSL #0 as needing a shifter. In practice the throughput
// seems to reliably be 2 when run on a cyclemodel, so we don't require a
// shift resource.
def : InstRW<[M85WriteALUsi, M85Read_EX1, M85ReadALUsi],
               (instregex "t2(ADC|ADDS|BIC|EOR|ORN|ORR|RSBS|RSB|SBC|"
                          "SUBS|CMP|CMNz|TEQ|TST)rr$")>;
def : InstRW<[M85WriteALUsi, M85ReadALUsi],
               (instregex "t2MVNr")>;

// Shift instructions: most pure shifts (i.e. MOV w/ shift) will use whichever
// shifter is free, thus it is possible to dual-issue them freely with anything
// else.  As a result, they are not modeled as needing a shifter.
// RRX is odd because it must use the EX2 shifter, so it cannot dual-issue with
// itself.
//
// Note that pure shifts which use the EX1 shifter would need their operands
// a cycle earlier.  However, they are only forced to use the EX1 shifter
// when issuing against an RRX instructions, which should be rare.

def : InstRW<[M85WriteShift2],
               (instregex "t2RRX$")>;
def : InstRW<[WriteALU],
               (instregex "(t|t2)(LSL|LSR|ASR|ROR|SBFX|UBFX)", "t2MOVsr(a|l)")>;

// Instructions that use the shifter, but have normal timing

def : InstRW<[WriteALUsi,M85Slot0Only], (instregex "t2(BFC|BFI)$")>;

// Stack pointer add/sub happens in EX1 with checks in EX2

def M85WritesToSPPred : MCSchedPredicate<CheckRegOperand<0, SP>>;

def M85ReadForSP : SchedReadVariant<[
  SchedVar<M85WritesToSPPred, [M85Read_ISS]>,
  SchedVar<NoSchedPred, [M85Read_EX1]>
]>;
def M85ReadForSPShift : SchedReadVariant<[
  SchedVar<M85WritesToSPPred, [M85Read_ISS]>,
  SchedVar<NoSchedPred, [M85Read_ISS]>
]>;

def : InstRW<[WriteALU, M85Read_ISS],
               (instregex "tADDspi", "tSUBspi")>;
def : InstRW<[WriteALU, M85ReadForSP],
               (instregex "t2(ADD|SUB)ri", "t2MOVr", "tMOVr")>;
def : InstRW<[WriteALU, M85ReadForSP, M85ReadForSP],
               (instregex "tADDrSP", "tADDspr", "tADDhirr")>;
def : InstRW<[M85WriteALUsi, M85ReadForSP, M85ReadForSPShift],
               (instregex "t2(ADD|SUB)rs")>;

def : InstRW<[WriteALU, M85Slot0Only], (instregex "t2CLZ")>;

// MAC operations that don't have SchedRW set

def : InstRW<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC], (instregex "t2SML[AS]D")>;

// Divides are special because they stall for their latency, and so look like
// two cycles as far as scheduling opportunities go.  By putting M85Write2
// first, we make the operand latency 2, but keep the instruction latency 7.
// Divide operands are read early.

def : InstRW<[M85Write2, WriteDIV, M85Read_ISS, M85Read_ISS, WriteALU],
               (instregex "t2(S|U)DIV")>;

// DSP extension operations

def M85WriteSIMD1   : SchedWriteRes<[M85UnitSIMD, M85UnitALU, M85UnitSlot0]> {
  let Latency = 1;
}
def M85WriteSIMD2   : SchedWriteRes<[M85UnitSIMD, M85UnitALU, M85UnitSlot0]> {
  let Latency = 2;
}
def M85WriteShSIMD0 : SchedWriteRes<[M85UnitSIMD, M85UnitALU,
                                       M85UnitShift1, M85UnitSlot0]> {
  let Latency = 0; // Finishes at EX1
}
def M85WriteShSIMD1 : SchedWriteRes<[M85UnitSIMD, M85UnitALU,
                                       M85UnitShift1, M85UnitSlot0]> {
  let Latency = 1;
}
def M85WriteShSIMD2 : SchedWriteRes<[M85UnitSIMD, M85UnitALU,
                                     M85UnitShift1, M85UnitSlot0]> {
  let Latency = 2;
}

def : InstRW<[M85WriteShSIMD2, M85Read_ISS],
               (instregex "t2(S|U)SAT")>;
def : InstRW<[M85WriteSIMD1, ReadALU],
               (instregex "(t|t2)(S|U)XT(B|H)")>;
def : InstRW<[M85WriteSIMD1, ReadALU, ReadALU],
               (instregex "t2(S|SH|U|UH)(ADD16|ADD8|ASX|SAX|SUB16|SUB8)",
                          "t2SEL")>;
def : InstRW<[M85WriteSIMD2, ReadALU, ReadALU],
               (instregex "t2(Q|UQ)(ADD|ASX|SAX|SUB)", "t2USAD8")>;
def : InstRW<[M85WriteShSIMD2, M85Read_ISS, M85Read_ISS],
               (instregex "t2QD(ADD|SUB)")>;
def : InstRW<[M85WriteShSIMD0, M85Read_ISS],
               (instregex "t2(RBIT|REV)", "tREV")>;
def : InstRW<[M85WriteShSIMD1, ReadALU, M85Read_ISS],
               (instregex "t2PKH(BT|TB)", "t2(S|U)XTA")>;
def : InstRW<[M85WriteSIMD2, ReadALU, ReadALU, M85Read_EX2],
               (instregex "t2USADA8")>;

// MSR/MRS
def : InstRW<[M85NonGeneralPurpose], (instregex "MSR", "MRS")>;

// 64-bit shift operations in EX3

def M85WriteLShift : SchedWriteRes<[M85UnitLShift, M85UnitALU]> {
  let Latency = 2;
}
def M85WriteLat2  : SchedWriteRes<[]>  { let Latency = 2; let NumMicroOps = 0; }

def : InstRW<[M85WriteLShift, M85WriteLat2, M85Read_EX2, M85Read_EX2],
               (instregex "MVE_(ASRLi|LSLLi|LSRL|SQSHLL|SRSHRL|UQSHLL|URSHRL)$")>;
def : InstRW<[M85WriteLShift, M85WriteLat2,
              M85Read_EX2, M85Read_EX2, M85Read_EX2],
               (instregex "MVE_(ASRLr|LSLLr|SQRSHRL|UQRSHLL)$")>;
def : InstRW<[M85WriteLShift, M85Read_EX2, M85Read_EX2],
               (instregex "MVE_(SQRSHR|UQRSHL)$")>;
def : InstRW<[M85WriteLShift, M85Read_EX2],
               (instregex "MVE_(SQSHL|SRSHR|UQSHL|URSHR)$")>;

// Loop control/branch future instructions

def M85LE   : SchedWriteRes<[]> { let NumMicroOps = 0; let Latency = -2; }

def : InstRW<[WriteALU], (instregex "t2BF(_|Lr|i|Li|r)")>;

def : InstRW<[WriteALU], (instregex "MVE_LCTP")>;
def : InstRW<[WriteALU],
               (instregex "t2DLS", "t2WLS", "MVE_DLSTP", "MVE_WLSTP")>;
def : InstRW<[M85LE], (instregex "t2LE$")>;
def : InstRW<[M85LE, M85Read_ISSm1],
               (instregex "t2LEUpdate", "MVE_LETP")>;  // LE is executed at ISS

// Conditional selects

def : InstRW<[M85WriteLShift, M85Read_EX2, M85Read_EX2, M85Read_EX2],
              (instregex "t2(CSEL|CSINC|CSINV|CSNEG)")>;

//===---------------------------------------------------------------------===//
// Sched definitions for FP and MVE operations

let NumMicroOps = 0 in {
  def M85OverrideVFPLat5 : SchedWriteRes<[]> { let Latency = 5; }
  def M85OverrideVFPLat4 : SchedWriteRes<[]> { let Latency = 4; }
  def M85OverrideVFPLat3 : SchedWriteRes<[]> { let Latency = 3; }
  def M85OverrideVFPLat2 : SchedWriteRes<[]> { let Latency = 2; }
}

let Latency = 1 in {
   def M85GroupALat1S : SchedWriteRes<[M85UnitVFPA, M85UnitVPort, M85UnitSlot0]>;
   def M85GroupBLat1S : SchedWriteRes<[M85UnitVFPB, M85UnitVPort, M85UnitSlot0]>;
   def M85GroupCLat1S : SchedWriteRes<[M85UnitVFPC, M85UnitVPort, M85UnitSlot0]>;
   def M85GroupALat1D : SchedWriteRes<[M85UnitVFPAL, M85UnitVFPAH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GroupBLat1D : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GroupCLat1D : SchedWriteRes<[M85UnitVFPCL, M85UnitVFPCH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GroupABLat1S : SchedWriteRes<[M85UnitVPort, M85UnitSlot0]>;
}
let Latency = 2 in {
   def M85GroupBLat2S : SchedWriteRes<[M85UnitVFPB, M85UnitVPort, M85UnitSlot0]>;
   def M85GroupBLat2D : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GroupABLat2S : SchedWriteRes<[M85UnitVPort, M85UnitSlot0]>;
   def M85GroupABLat2D : SchedWriteRes<[M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
}

// Instructions which are missing default schedules
def : InstRW<[M85GroupALat1S],  (instregex "V(FP_VMAXNM|FP_VMINNM)(H|S)$")>;
def : InstRW<[M85GroupALat1D],  (instregex "V(FP_VMAXNM|FP_VMINNM)D$")>;
def : InstRW<[M85GroupCLat1S],  (instregex "VCMPE?Z?(H|S)$")>;
def : InstRW<[M85GroupCLat1D],  (instregex "VCMPE?Z?D$")>;
def : InstRW<[M85GroupBLat2S],
               (instregex "VCVT(A|M|N|P|R|X|Z)(S|U)(H|S)",
                          "VRINT(A|M|N|P|R|X|Z)(H|S)")>;
def : InstRW<[M85GroupBLat2D],
               (instregex "VCVT(B|T)(DH|HD)", "VCVT(A|M|N|P|R|X|Z)(S|U)D",
                          "V.*TOD", "VTO.*D", "VCVTDS", "VCVTSD",
                          "VRINT(A|M|N|P|R|X|Z)D")>;
def : InstRW<[M85GroupABLat1S], (instregex "VINSH")>;
def : InstRW<[M85GroupBLat1S],  (instregex "V(ABS|NEG)(H|S)$")>;
def : InstRW<[M85GroupBLat1D],  (instregex "V(ABS|NEG)D$")>;

// VMRS/VMSR
let SingleIssue = 1 in {
  def M85VMRSEarly : SchedWriteRes<[M85UnitVPort]> { let Latency = 2;}
  def M85VMRSLate  : SchedWriteRes<[M85UnitVPort]> { let Latency = 4; }
  def M85VMSREarly : SchedWriteRes<[M85UnitVPort]> { let Latency = 1; }
  def M85VMSRLate  : SchedWriteRes<[M85UnitVPort]> { let Latency = 3; }
}

def M85FPSCRFlagPred : MCSchedPredicate<
                           CheckAll<[CheckIsRegOperand<0>,
                                     CheckRegOperand<0, PC>]>>;

def M85VMRSFPSCR : SchedWriteVariant<[
  SchedVar<M85FPSCRFlagPred, [M85VMRSEarly]>,
  SchedVar<NoSchedPred, [M85VMRSLate]>
]>;

def : InstRW<[M85VMSREarly, M85Read_EX2],
               (instregex "VMSR$", "VMSR_FPSCR_NZCVQC", "VMSR_P0", "VMSR_VPR")>;
def : InstRW<[M85VMRSEarly], (instregex "VMRS_P0", "VMRS_VPR", "FMSTAT")>;
def : InstRW<[M85VMRSLate], (instregex "VMRS_FPSCR_NZCVQC")>;
def : InstRW<[M85VMRSFPSCR], (instregex "VMRS$")>;
// Not matching properly
//def : InstRW<[M85VMSRLate, M85Read_EX2], (instregex "VMSR_FPCTX(NS|S)")>;
//def : InstRW<[M85VMRSLate], (instregex "VMRS_FPCTX(NS|S)")>;

// VSEL cannot bypass in its implied $cpsr operand; model as earlier read
def : InstRW<[M85GroupBLat1S, ReadALU, ReadALU, M85Read_ISS],
               (instregex "VSEL.*(S|H)$")>;
def : InstRW<[M85GroupBLat1D, ReadALU, ReadALU, M85Read_ISS],
               (instregex "VSEL.*D$")>;

// VMOV
def : InstRW<[WriteFPMOV],
               (instregex "VMOV(H|S)$", "FCONST(H|S)")>;
def : InstRW<[WriteFPMOV, M85Read_EX2],
               (instregex "VMOVHR$", "VMOVSR$")>;
def : InstRW<[M85GroupABLat2S],
               (instregex "VMOVRH$", "VMOVRS$")>;
def : InstRW<[M85WriteFPMOV64],
               (instregex "VMOVD$")>;
def : InstRW<[M85WriteFPMOV64],
               (instregex "FCONSTD")>;
def : InstRW<[M85WriteFPMOV64, M85Read_EX2, M85Read_EX2],
               (instregex "VMOVDRR")>;
def : InstRW<[M85WriteFPMOV64, M85Write1, M85Read_EX2, M85Read_EX2],
               (instregex "VMOVSRR")>;
def : InstRW<[M85GroupABLat2D, M85Write2],
               (instregex "VMOV(RRD|RRS)")>;

// These shouldn't even exist, but Cortex-m55 defines them, so here they are.
def : InstRW<[WriteFPMOV, M85Read_EX2],
               (instregex "VGETLNi32$")>;
def : InstRW<[M85GroupABLat2S],
               (instregex "VSETLNi32")>;

// Larger-latency overrides

def M85FPDIV16 : SchedWriteRes<[M85UnitVFPB, M85UnitVPort, M85UnitSlot0]> {
  let Latency = 8;
}
def : InstRW<[M85OverrideVFPLat2, M85FPDIV16], (instregex "VDIVH")>;
def : InstRW<[M85OverrideVFPLat2, WriteFPDIV32],   (instregex "VDIVS")>;
def : InstRW<[M85OverrideVFPLat2, WriteFPDIV64],   (instregex "VDIVD")>;
def : InstRW<[M85OverrideVFPLat2, M85FPDIV16], (instregex "VSQRTH")>;
def : InstRW<[M85OverrideVFPLat2, WriteFPSQRT32],  (instregex "VSQRTS")>;
def : InstRW<[M85OverrideVFPLat2, WriteFPSQRT64],  (instregex "VSQRTD")>;
def : InstRW<[M85OverrideVFPLat3, WriteFPMUL64],   (instregex "V(MUL|NMUL)D")>;
def : InstRW<[M85OverrideVFPLat2, WriteFPALU64],   (instregex "V(ADD|SUB)D")>;

// Multiply-accumulate.  Chained SP timing is correct; rest need overrides
// Double-precision chained MAC should also be seen as having latency of 5,
// as stalls stall everything.

def : InstRW<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL],
               (instregex "VN?ML(A|S)H")>;

def : InstRW<[M85OverrideVFPLat5, WriteFPMAC64,
              ReadFPMUL, ReadFPMUL, ReadFPMUL],
               (instregex "VN?ML(A|S)D$")>;

// Single-precision fused MACs look like latency 4 with advance of 2.

def M85ReadFPMAC2   : SchedReadAdvance<2>;

def : InstRW<[M85OverrideVFPLat4, WriteFPMAC32,
              M85ReadFPMAC2, ReadFPMUL, ReadFPMUL],
               (instregex "VF(N)?M(A|S)(H|S)$")>;

// Double-precision fused MAC looks like latency 4.

def : InstRW<[M85OverrideVFPLat4, WriteFPMAC64,
              ReadFPMUL, ReadFPMUL, ReadFPMUL],
               (instregex "VF(N)?M(A|S)D$")>;

// MVE beatwise instructions
// NOTE: Q-register timing for the 2nd beat is off by a cycle and needs
//       DAG overrides to correctly set latencies.
// NOTE2: MVE integer MAC->MAC accumulate latencies are set as if the
//        accumulate value arrives from an unmatching MAC instruction;
//        matching ones are handled via DAG mutation.  These are marked as
//        "limited accumulate bypass"

let Latency = 4, EndGroup = 1 in {
   def M85GrpALat2MveR : SchedWriteRes<[M85UnitVFPAL, M85UnitVFPAH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpABLat2MveR : SchedWriteRes<[M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GrpBLat2MveR : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85Lat2MveR : SchedWriteRes<[]> { let NumMicroOps = 0; }
   def M85GrpBLat4Mve : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
}
let Latency = 3, EndGroup = 1 in {
   def M85GrpBLat3Mve : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpBLat1MveR : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85Lat1MveR : SchedWriteRes<[]> { let NumMicroOps = 0; }
}
let Latency = 2, EndGroup = 1 in {
   def M85GrpALat2Mve : SchedWriteRes<[M85UnitVFPAL, M85UnitVFPAH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpABLat2Mve : SchedWriteRes<[M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GrpBLat2Mve : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85Lat2Mve : SchedWriteRes<[]> { let NumMicroOps = 0; }
}
let Latency = 1, EndGroup = 1 in {
   def M85GrpALat1Mve : SchedWriteRes<[M85UnitVFPAL, M85UnitVFPAH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpABLat1Mve : SchedWriteRes<[M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]>;
   def M85GrpBLat1Mve : SchedWriteRes<[M85UnitVFPBL, M85UnitVFPBH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpCLat1Mve : SchedWriteRes<[M85UnitVFPCL, M85UnitVFPCH, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,2,1,1,1];
   }
   def M85GrpDLat1Mve : SchedWriteRes<[M85UnitVFPD, M85UnitVPortL, M85UnitVPortH, M85UnitSlot0]> {
     let ReleaseAtCycles = [2,1,1,1];
   }
}

def : InstRW<[M85GrpABLat1Mve, M85Read_EX1, M85Read_EX2, M85Read_EX2],
                (instregex "MVE_VMOV_q_rr")>;

def : InstRW<[M85GrpABLat1Mve, M85Read_EX2],
                (instregex "MVE_VMOV_to_lane_(8|16|32)")>;

def : InstRW<[M85GrpABLat1Mve],
                (instregex "MVE_VAND$",
                           "MVE_VBIC$", "MVE_VBICimm",
                           "MVE_VCLSs(8|16|32)",
                           "MVE_VCLZs(8|16|32)",
                           "MVE_VEOR",
                           "MVE_VMOVimmf32", "MVE_VMOVimmi(8|16|32|64)",
                           "MVE_VMVN$", "MVE_VMVNimmi(16|32)",
                           "MVE_VORN$",
                           "MVE_VORR$", "MVE_VORRimm", "MQPRCopy",
                           "MVE_VPSEL",
                           "MVE_VREV(16|32|64)_(8|16|32)"
                           )>;

def : InstRW<[M85GrpABLat2MveR, M85Lat2MveR],
                (instregex "MVE_VMOV_rr_q")>;

def : InstRW<[M85GrpABLat2MveR],
                (instregex "MVE_VMOV_from_lane_(32|u8|s8|u16|s16)")>;

def : InstRW<[M85GrpALat1Mve, M85Lat1MveR,
              M85Read_EX1, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VADC$")>;

def : InstRW<[M85GrpALat1Mve, M85Lat1MveR],
                (instregex "MVE_VADCI")>;

def : InstRW<[M85GrpALat1Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VADD_qr_i(8|16|32)",
                           "MVE_VBRSR(16|32|8)",
                           "MVE_VHADD_qr_[su](8|16|32)",
                           "MVE_VHSUB_qr_[su](8|16|32)",
                           "MVE_VQADD_qr_[su](8|16|32)",
                           "MVE_VQSUB_qr_[su](8|16|32)",
                           "MVE_VSHL_qr[su](8|16|32)",
                           "MVE_VSUB_qr_i(8|16|32)"
                )>;

def : InstRW<[M85GrpALat1Mve],
                (instregex "MVE_VABD(s|u)(8|16|32)",
                           "MVE_VABS(s|u)(8|16|32)",
                           "MVE_V(MAX|MIN)A?[us](8|16|32)",
                           "MVE_VADDi(8|16|32)",
                           "MVE_VCADDi(8|16|32)",
                           "MVE_VHCADDs(8|16|32)",
                           "MVE_VHSUB[su](8|16|32)",
                           "MVE_VMOVL[su](8|16)[tb]h",
                           "MVE_VMOVNi(16|32)[tb]h",
                           "MVE_VMULL[BT]?[p](8|16|32)(bh|th)?",
                           "MVE_VNEGs(8|16|32)",
                           "MVE_VQABSs(8|16|32)",
                           "MVE_VQADD[su](8|16|32)",
                           "MVE_VQNEGs(8|16|32)",
                           "MVE_VQSUB[su](8|16|32)",
                           "MVE_VR?HADD[su](8|16|32)",
                           "MVE_VSBC$", "MVE_VSBCI",
                           "MVE_VSHL_by_vec[su](8|16|32)",
                           "MVE_VSHL_immi(8|16|32)",
                           "MVE_VSHLL_imm[su](8|16)[bt]h",
                           "MVE_VSHLL_lw[su](8|16)[bt]h",
                           "MVE_VSHRNi(16|32)[bt]h",
                           "MVE_VSHR_imm[su](8|16|32)",
                           "MVE_VSLIimm[su]?(8|16|32)",
                           "MVE_VSRIimm[su]?(8|16|32)",
                           "MVE_VSUBi(8|16|32)"
                 )>;

def : InstRW<[M85GrpALat2Mve, M85Lat2MveR, M85Read_EX2, M85Read_EX2],
                (instregex "MVE_V(D|I)WDUPu(8|16|32)")>;

def : InstRW<[M85GrpALat2Mve, M85Lat2MveR, M85Read_EX2],
                (instregex "MVE_V(D|I)DUPu(8|16|32)")>;

def : InstRW<[M85GrpALat2Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_V(Q|R|QR)SHL_qr[su](8|16|32)",
                           "MVE_VADD_qr_f(16|32)",
                           "MVE_VSUB_qr_f(16|32)"
                )>;

def : InstRW<[M85GrpALat1Mve, M85Read_EX2],
                (instregex "MVE_VDUP(8|16|32)")>;

def : InstRW<[M85GrpBLat1Mve],
                (instregex "MVE_VABSf(16|32)",
                           "MVE_V(MAX|MIN)NMA?f(16|32)",
                           "MVE_VNEGf(16|32)"
                )>;

def : InstRW<[M85GrpBLat2MveR, M85Lat2MveR, M85Read_EX3, M85Read_EX3],
                (instregex "MVE_VADDLV[us]32acc")>;

def : InstRW<[M85GrpBLat2MveR, M85Lat2MveR],
                (instregex "MVE_VADDLV[us]32no_acc")>;

def : InstRW<[M85GrpBLat2MveR, M85Read_EX3],
                (instregex "MVE_VADDV[us](8|16|32)acc"
                )>;

def : InstRW<[M85GrpALat2MveR, M85Read_EX3],
                (instregex "MVE_V(MAX|MIN)A?V[us](8|16|32)",
                           "MVE_VABAV(s|u)(8|16|32)"
                )>;

def : InstRW<[M85GrpALat2MveR],
                (instregex "MVE_VADDV[us](8|16|32)no_acc")>;

def : InstRW<[M85GrpALat2Mve],
                (instregex "MVE_V(Q|R|QR)SHL_by_vec[su](8|16|32)",
                           "MVE_VABDf(16|32)",
                           "MVE_VADDf(16|32)",
                           "MVE_VCADDf(16|32)",
                           "MVE_VQMOVU?N[su](8|16|32)[tb]h",
                           "MVE_VQR?SHL(U_)?imm[su](8|16|32)",
                           "MVE_VQR?SHRN[bt]h[su](16|32)",
                           "MVE_VQR?SHRUNs(16|32)[bt]h",
                           "MVE_VRSHR_imm[su](8|16|32)",
                           "MVE_VRSHRNi(16|32)[bt]h",
                           "MVE_VSUBf(16|32)"
                 )>;

def : InstRW<[M85GrpBLat2MveR, M85Read_EX2],
                (instregex "MVE_V(MAX|MIN)NMA?Vf(16|32)")>;

def : InstRW<[M85GrpBLat2Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VMUL_qr_i(8|16|32)")>;

def : InstRW<[M85GrpBLat2Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VQDMULL_qr_s(16|32)[tb]h")>;

def : InstRW<[M85GrpBLat2Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VQR?DMULH_qr_s(8|16|32)")>;

def : InstRW<[M85GrpBLat2Mve, M85Read_EX1, M85Read_EX1, M85Read_EX3],
                // limited accumulate bypass
                (instregex "MVE_VMLAS?_qr_i(8|16|32)")>;

def : InstRW<[M85GrpBLat2Mve, M85Read_EX1, M85Read_EX1, M85Read_EX2],
                // limited accumulate bypass
                (instregex "MVE_VQR?DMLAS?H_qrs(8|16|32)")>;

def : InstRW<[M85GrpBLat2Mve],
                // limited accumulate bypass
                (instregex "MVE_VQR?DML[AS]DHX?s(8|16|32)")>;

def : InstRW<[M85GrpBLat2MveR, M85Lat2MveR, M85Read_EX3, M85Read_EX3],
                (instregex "MVE_VR?ML[AS]LDAVH?ax?[su](8|16|32)")>;

def : InstRW<[M85GrpBLat2MveR, M85Lat2MveR],
                (instregex "MVE_VR?ML[AS]LDAVH?x?[su](8|16|32)")>;

def : InstRW<[M85GrpBLat2MveR, M85Read_EX3],
                (instregex "MVE_VML[AS]DAVax?[su](8|16|32)")>;

def : InstRW<[M85GrpBLat2MveR],
                (instregex "MVE_VML[AS]DAVx?[su](8|16|32)")>;

def : InstRW<[M85GrpBLat2Mve],
                (instregex "MVE_VCVTf16(u|s)16", "MVE_VCVTf32(u|s)32",
                           "MVE_VCVT(u|s)16f16", "MVE_VCVT(u|s)32f32",
                           "MVE_VCVTf16f32", "MVE_VCVTf32f16",
                           "MVE_VMULL[BT]?[su](8|16|32)(bh|th)?",
                           "MVE_VMUL(t1)*i(8|16|32)",
                           "MVE_VQDMULLs(16|32)[tb]h",
                           "MVE_VQR?DMULHi(8|16|32)",
                           "MVE_VR?MULH[su](8|16|32)",
                           "MVE_VRINTf(16|32)"
                )>;

def : InstRW<[M85GrpBLat3Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VMUL_qr_f(16|32)")>;

def : InstRW<[M85GrpBLat3Mve],
                (instregex "MVE_VCMULf(16|32)",
                           "MVE_VMULf(16|32)"
                )>;

def : InstRW<[M85GrpBLat4Mve, M85Read_EX3, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VFMA_qr_Sf(16|32)", // VFMAS
                           "MVE_VFMA_qr_f(16|32)" // VFMA
                )>;

def : InstRW<[M85GrpBLat4Mve, M85Read_EX3],
                (instregex "MVE_VCMLAf(16|32)")>;

def : InstRW<[M85GrpBLat4Mve, M85Read_EX3],
                (instregex "MVE_VFM(A|S)f(16|32)")>;

def : InstRW<[M85GrpCLat1Mve, M85Read_EX1, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VPTv(4|8)f(16|32)r")>;

def : InstRW<[M85GrpCLat1Mve, M85Read_EX1, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VPTv(4|8|16)(i|s|u)(8|16|32)r")>;

def : InstRW<[M85GrpCLat1Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VCMP[isu](8|16|32)r$", "MVE_VCMPf(16|32)r$")>;

def : InstRW<[M85GrpDLat1Mve, M85Read_EX2],
                (instregex "MVE_VCTP(8|16|32|64)")>;

def : InstRW<[M85GrpCLat1Mve],
                (instregex "MVE_VCMPf(16|32)$", "MVE_VCMP[isu](8|16|32)$",
                           "MVE_VPTv(4|8)f(16|32)$",
                           "MVE_VPTv(4|8|16)(i|s|u)(8|16|32)$"
                )>;

def : InstRW<[M85GrpDLat1Mve],
                (instregex "MVE_VPNOT",
                           "MVE_VPST"
                )>;

def : InstRW<[M85Lat2MveR, M85GrpALat2Mve, M85Read_EX1, M85Read_EX2],
                (instregex "MVE_VSHLC")>;

// VFP instructions

def : WriteRes<WriteVLD1, []>;
def : WriteRes<WriteVLD2, []>;
def : WriteRes<WriteVLD3, []>;
def : WriteRes<WriteVLD4, []>;
def : WriteRes<WriteVST1, []>;
def : WriteRes<WriteVST2, []>;
def : WriteRes<WriteVST3, []>;
def : WriteRes<WriteVST4, []>;

}  // SchedModel = CortexCortexM85Model
