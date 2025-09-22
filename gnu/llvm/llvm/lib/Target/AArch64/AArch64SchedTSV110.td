//==- AArch64SchedTSV110.td - Huawei TSV110 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Huawei TSV110 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the simpler per-operand machine model.
// This works with MachineScheduler. See llvm/MC/MCSchedule.h for details.

// Huawei TSV110 scheduling machine model.
def TSV110Model : SchedMachineModel {
  let IssueWidth            =   4; // 4 micro-ops dispatched  per cycle. 
  let MicroOpBufferSize     = 128; // 128 micro-op re-order buffer
  let LoopMicroOpBufferSize =  16; 
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  14; // Fetch + Decode/Rename/Dispatch + Branch
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

// Define each kind of processor resource and number available on the TSV110,
// which has 8 pipelines, each with its own queue where micro-ops wait for
// their operands and issue out-of-order to one of eight execution pipelines.
let SchedModel = TSV110Model in {
  def TSV110UnitALU   : ProcResource<1>; // Int ALU
  def TSV110UnitAB    : ProcResource<2>; // Int ALU/BRU
  def TSV110UnitMDU   : ProcResource<1>; // Multi-Cycle
  def TSV110UnitFSU1  : ProcResource<1>; // FP/ASIMD
  def TSV110UnitFSU2  : ProcResource<1>; // FP/ASIMD
  def TSV110UnitLd0St : ProcResource<1>; // Load/Store
  def TSV110UnitLd1   : ProcResource<1>; // Load

  def TSV110UnitLd    : ProcResGroup<[TSV110UnitLd0St, TSV110UnitLd1]>;  // Any load
  def TSV110UnitF     : ProcResGroup<[TSV110UnitFSU1, TSV110UnitFSU2]>;
  def TSV110UnitALUAB : ProcResGroup<[TSV110UnitALU, TSV110UnitAB]>;
}

let SchedModel = TSV110Model in {

//===----------------------------------------------------------------------===//
// Map the target-defined scheduler read/write resources and latency for TSV110

// Integer ALU
// TODO: Use SchedVariant to select BRU for ALU ops that sets NZCV flags
// (including ops that shift and/or extend):
// 1cyc_1BRU: ADDS, ADCS, ANDS, BICS, SUBS, SBCS, CCMN, CCMP
// 2cyc_1BRU: ADDSshfr, SUBSshfr, ANDSshfr, ADDSextr, SUBSextr
def : WriteRes<WriteImm,   [TSV110UnitALUAB]> { let Latency = 1; }
def : WriteRes<WriteI,     [TSV110UnitALUAB]> { let Latency = 1; }
def : WriteRes<WriteISReg, [TSV110UnitMDU]>   { let Latency = 2; } 
def : WriteRes<WriteIEReg, [TSV110UnitMDU]>   { let Latency = 2; } 
def : WriteRes<WriteExtr,  [TSV110UnitALUAB]> { let Latency = 1; }
def : WriteRes<WriteIS,    [TSV110UnitALUAB]> { let Latency = 1; }

// Integer Mul/MAC/Div
def : WriteRes<WriteID32,  [TSV110UnitMDU]> { let Latency = 12;
                                              let ReleaseAtCycles = [12]; } 
def : WriteRes<WriteID64,  [TSV110UnitMDU]> { let Latency = 20;
                                              let ReleaseAtCycles = [20]; }
def : WriteRes<WriteIM32,  [TSV110UnitMDU]> { let Latency = 3; }
def : WriteRes<WriteIM64,  [TSV110UnitMDU]> { let Latency = 4; }

// Load
def : WriteRes<WriteLD,    [TSV110UnitLd]>  { let Latency = 4; }
def : WriteRes<WriteLDIdx, [TSV110UnitLd]>  { let Latency = 4; }
def : WriteRes<WriteLDHi,  []> { let Latency = 4; }

// Pre/Post Indexing
def : WriteRes<WriteAdr,   [TSV110UnitALUAB]> { let Latency = 1; } 

// Store
def : WriteRes<WriteST,    [TSV110UnitLd0St]> { let Latency = 1; }
def : WriteRes<WriteSTP,   [TSV110UnitLd0St]> { let Latency = 1; }
def : WriteRes<WriteSTIdx, [TSV110UnitLd0St]> { let Latency = 1; }

// FP
def : WriteRes<WriteF,     [TSV110UnitF]> { let Latency = 2; }
def : WriteRes<WriteFCmp,  [TSV110UnitF]> { let Latency = 3; }
def : WriteRes<WriteFCvt,  [TSV110UnitF]> { let Latency = 3; } 
def : WriteRes<WriteFCopy, [TSV110UnitF]> { let Latency = 2; }
def : WriteRes<WriteFImm,  [TSV110UnitF]> { let Latency = 2; } 
def : WriteRes<WriteFMul,  [TSV110UnitF]> { let Latency = 5; }

// FP Div, Sqrt
def : WriteRes<WriteFDiv,  [TSV110UnitFSU1]> { let Latency = 18; let ReleaseAtCycles = [18]; }

def : WriteRes<WriteVd,    [TSV110UnitF]>     { let Latency = 4; }
def : WriteRes<WriteVq,    [TSV110UnitF]>     { let Latency = 4; }
def : WriteRes<WriteVST,   [TSV110UnitF]>     { let Latency = 1; }

// Branch
def : WriteRes<WriteBr,    [TSV110UnitAB]> { let Latency = 1; }
def : WriteRes<WriteBrReg, [TSV110UnitAB]> { let Latency = 1; }
def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteAtomic, []> { let Unsupported = 1; } 

// Forwarding logic is modeled only for multiply and accumulate.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     2, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;
def : ReadAdvance<ReadST,      0>;

def : InstRW<[WriteI], (instrs COPY)>;

// Detailed Refinements
//===----------------------------------------------------------------------===//

// Contains all of the TSV110 specific SchedWriteRes types. The approach
// below is to define a generic SchedWriteRes for every combination of
// latency and microOps. The naming conventions is to use a prefix, one field
// for latency, and one or more microOp count/type designators.
//   Prefix: TSV110Wr
//       Latency: #cyc
//   MicroOp Count/Types: #(ALU|AB|MDU|FSU1|FSU2|LdSt|ALUAB|F|FLdSt)
//
// e.g. TSV110Wr_6cyc_1ALU_6MDU_4LdSt means the total latency is 6 and there are
//      1 micro-ops to be issued down one ALU pipe, six MDU pipes and four LdSt pipes.
//

//===----------------------------------------------------------------------===//
// Define Generic 1 micro-op types

def TSV110Wr_1cyc_1AB    : SchedWriteRes<[TSV110UnitAB]>    { let Latency = 1; }
def TSV110Wr_1cyc_1ALU   : SchedWriteRes<[TSV110UnitALU]>   { let Latency = 1; }
def TSV110Wr_1cyc_1ALUAB : SchedWriteRes<[TSV110UnitALUAB]> { let Latency = 1; }
def TSV110Wr_1cyc_1LdSt  : SchedWriteRes<[TSV110UnitLd0St]> { let Latency = 1; }

def TSV110Wr_2cyc_1AB    : SchedWriteRes<[TSV110UnitAB]>    { let Latency = 2; let ReleaseAtCycles = [2]; }
def TSV110Wr_2cyc_1ALU   : SchedWriteRes<[TSV110UnitALU]>   { let Latency = 2; }
def TSV110Wr_2cyc_1MDU   : SchedWriteRes<[TSV110UnitMDU]>   { let Latency = 2; }
def TSV110Wr_2cyc_1FSU1  : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 2; }
def TSV110Wr_2cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 2; }

def TSV110Wr_3cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 3; }
def TSV110Wr_3cyc_1FSU1  : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 3; }
def TSV110Wr_3cyc_1MDU   : SchedWriteRes<[TSV110UnitMDU]>   { let Latency = 3; }

def TSV110Wr_4cyc_1FSU1  : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 4; }
def TSV110Wr_4cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 4; }
def TSV110Wr_4cyc_1LdSt  : SchedWriteRes<[TSV110UnitLd]>    { let Latency = 4; }
def TSV110Wr_4cyc_1MDU   : SchedWriteRes<[TSV110UnitMDU]>   { let Latency = 4; }

def TSV110Wr_5cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 5; }
def TSV110Wr_5cyc_1FSU1  : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 5; }
def TSV110Wr_5cyc_1FSU2  : SchedWriteRes<[TSV110UnitFSU2]>  { let Latency = 5; }
def TSV110Wr_5cyc_1LdSt  : SchedWriteRes<[TSV110UnitLd]>    { let Latency = 5; }

def TSV110Wr_6cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 6; }

def TSV110Wr_7cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 7; }

def TSV110Wr_8cyc_1F     : SchedWriteRes<[TSV110UnitF]>     { let Latency = 8; }

def TSV110Wr_11cyc_1FSU1 : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 11; let ReleaseAtCycles = [11]; }

def TSV110Wr_12cyc_1MDU  : SchedWriteRes<[TSV110UnitMDU]>   { let Latency = 12; let ReleaseAtCycles = [12]; }

def TSV110Wr_17cyc_1FSU2 : SchedWriteRes<[TSV110UnitFSU2]>  { let Latency = 17; let ReleaseAtCycles = [17]; }

def TSV110Wr_18cyc_1FSU1 : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 18; let ReleaseAtCycles = [18]; }

def TSV110Wr_20cyc_1MDU  : SchedWriteRes<[TSV110UnitMDU]>   { let Latency = 20; let ReleaseAtCycles = [20]; }

def TSV110Wr_24cyc_1FSU1 : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 24; let ReleaseAtCycles = [24]; }

def TSV110Wr_31cyc_1FSU2 : SchedWriteRes<[TSV110UnitFSU2]>  { let Latency = 31; let ReleaseAtCycles = [31]; }

def TSV110Wr_36cyc_1FSU2 : SchedWriteRes<[TSV110UnitFSU2]>  { let Latency = 36; let ReleaseAtCycles = [36]; }

def TSV110Wr_38cyc_1FSU1 : SchedWriteRes<[TSV110UnitFSU1]>  { let Latency = 38; let ReleaseAtCycles = [38]; }

def TSV110Wr_64cyc_1FSU2 : SchedWriteRes<[TSV110UnitFSU2]>  { let Latency = 64; let ReleaseAtCycles = [64]; }

//===----------------------------------------------------------------------===//
// Define Generic 2 micro-op types

def TSV110Wr_1cyc_1LdSt_1ALUAB : SchedWriteRes<[TSV110UnitLd0St,
                                                TSV110UnitALUAB]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def TSV110Wr_2cyc_1LdSt_1ALUAB : SchedWriteRes<[TSV110UnitLd0St,
                                                TSV110UnitALUAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def TSV110Wr_2cyc_2LdSt        : SchedWriteRes<[TSV110UnitLd0St,
                                                TSV110UnitLd0St]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def TSV110Wr_2cyc_2F           : SchedWriteRes<[TSV110UnitF,
                                                TSV110UnitF]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def TSV110Wr_2cyc_1FSU1_1FSU2  : SchedWriteRes<[TSV110UnitFSU1,
                                                TSV110UnitFSU2]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def TSV110Wr_4cyc_2F           : SchedWriteRes<[TSV110UnitF,
                                                TSV110UnitF]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def TSV110Wr_4cyc_1FSU1_1FSU2  : SchedWriteRes<[TSV110UnitFSU1,
                                                TSV110UnitFSU2]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def TSV110Wr_4cyc_1LdSt_1ALUAB : SchedWriteRes<[TSV110UnitLd,
                                                TSV110UnitALUAB]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def TSV110Wr_5cyc_1ALU_1F      : SchedWriteRes<[TSV110UnitALU,
                                                TSV110UnitF]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}

def TSV110Wr_6cyc_2LdSt        : SchedWriteRes<[TSV110UnitLd,
                                                TSV110UnitLd]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def TSV110Wr_6cyc_1LdSt_1ALUAB : SchedWriteRes<[TSV110UnitLd,
                                                TSV110UnitALUAB]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def TSV110Wr_7cyc_1F_1LdSt     : SchedWriteRes<[TSV110UnitF,
                                                TSV110UnitLd]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def TSV110Wr_8cyc_2FSU1        : SchedWriteRes<[TSV110UnitFSU1,
                                                TSV110UnitFSU1]> {
  let Latency = 8;
  let NumMicroOps = 2;
}


def TSV110Wr_8cyc_1FSU1_1FSU2  : SchedWriteRes<[TSV110UnitFSU1,
                                                TSV110UnitFSU2]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

//===----------------------------------------------------------------------===//
// Define Generic 3 micro-op types

def TSV110Wr_6cyc_3F       : SchedWriteRes<[TSV110UnitF, TSV110UnitF,
                                            TSV110UnitF]> {
  let Latency     = 6;
  let NumMicroOps = 3;
}

def TSV110Wr_6cyc_3LdSt    : SchedWriteRes<[TSV110UnitLd, TSV110UnitLd,
                                            TSV110UnitLd]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def TSV110Wr_7cyc_2F_1LdSt : SchedWriteRes<[TSV110UnitF, TSV110UnitF,
                                                         TSV110UnitLd]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

//===----------------------------------------------------------------------===//
// Define Generic 4 micro-op types

def TSV110Wr_8cyc_4F          : SchedWriteRes<[TSV110UnitF, TSV110UnitF,
                                               TSV110UnitF, TSV110UnitF]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def TSV110Wr_8cyc_3F_1LdSt    : SchedWriteRes<[TSV110UnitF, TSV110UnitF,
                                               TSV110UnitF, TSV110UnitLd]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

//===----------------------------------------------------------------------===//
// Define Generic 5 micro-op types

def TSV110Wr_8cyc_3F_2LdSt : SchedWriteRes<[TSV110UnitF, TSV110UnitF, TSV110UnitF,
                                            TSV110UnitLd, TSV110UnitLd]> {
  let Latency = 8;
  let NumMicroOps = 5;
}

//===----------------------------------------------------------------------===//
// Define Generic 8 micro-op types

def TSV110Wr_10cyc_4F_4LdSt : SchedWriteRes<[TSV110UnitF, TSV110UnitF,
                                             TSV110UnitF, TSV110UnitF,
                                             TSV110UnitLd, TSV110UnitLd,
                                             TSV110UnitLd, TSV110UnitLd]> {
  let Latency = 10;
  let NumMicroOps = 8;
}


// Branch Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1AB], (instrs B)>;
def : InstRW<[TSV110Wr_1cyc_1AB], (instrs BL)>;
def : InstRW<[TSV110Wr_1cyc_1AB], (instrs BLR)>;
def : InstRW<[TSV110Wr_1cyc_1AB], (instregex "^(BR|RET|(CBZ|CBNZ|TBZ|TBNZ))$")>;


// Cryptography Extensions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_3cyc_1FSU1], (instregex "^AES[DE]")>;
def : InstRW<[TSV110Wr_3cyc_1FSU1], (instregex "^AESI?MC")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1], (instregex "^SHA1SU1")>;
def : InstRW<[TSV110Wr_2cyc_2F],    (instregex "^SHA1(H|SU0)")>;
def : InstRW<[TSV110Wr_5cyc_1FSU1], (instregex "^SHA1[CMP]")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1], (instregex "^SHA256SU0")>;
def : InstRW<[TSV110Wr_3cyc_1FSU1], (instregex "^SHA256SU1")>;
def : InstRW<[TSV110Wr_5cyc_1FSU1], (instregex "^SHA256(H|H2)")>;
def TSV110ReadCRC: SchedReadAdvance<1, [TSV110Wr_2cyc_1MDU]>;
def : InstRW<[TSV110Wr_2cyc_1MDU, TSV110ReadCRC],  (instregex "^CRC32.*$")>;


// Arithmetic and Logical Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "(BIC|EON|ORN)[WX]rr")>;
def : InstRW<[TSV110Wr_1cyc_1AB],    (instregex "(BIC)S[WX]rr")>;

def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "(ADD|AND|EOR|ORR|SUB)[WX]r(r|i)")>;
def : InstRW<[TSV110Wr_1cyc_1AB],    (instregex "(ADD|AND|EOR|ORR|SUB)S[WX]r(r|i)")>;

def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "^(ADC|SBC|BIC)[WX]r$")>;
def : InstRW<[TSV110Wr_1cyc_1AB],    (instregex "^(ADC|SBC)S[WX]r$")>;


// Shifted Register with Shift == 0
// ----------------------------------------------------------------------------

def TSV110WrISReg : SchedWriteVariant<[
       SchedVar<RegShiftedPred, [WriteISReg]>,
       SchedVar<NoSchedPred, [WriteI]>]>;
def : InstRW<[TSV110WrISReg], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)[WX]rs$")>;

def TSV110WrISRegBr : SchedWriteVariant<[
       SchedVar<RegShiftedPred, [TSV110Wr_2cyc_1AB]>,
       SchedVar<NoSchedPred, [WriteBr]>]>;
def : InstRW<[TSV110WrISRegBr], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)S[WX]rs$")>;

// Extended Register with Extend == 0
// ----------------------------------------------------------------------------

def TSV110WrIEReg : SchedWriteVariant<[
       SchedVar<RegExtendedPred, [WriteISReg]>,
       SchedVar<NoSchedPred, [WriteI]>]>;
def : InstRW<[TSV110WrIEReg], (instregex "^(ADD|SUB)[WX]r(x|x64)$")>;

def TSV110WrIERegBr : SchedWriteVariant<[
       SchedVar<RegExtendedPred, [TSV110Wr_2cyc_1AB]>,
       SchedVar<NoSchedPred, [WriteBr]>]>;
def : InstRW<[TSV110WrIERegBr], (instregex "^(ADD|SUB)S[WX]r(x|x64)$")>;

def : InstRW<[TSV110Wr_1cyc_1AB],    (instregex "^(CCMN|CCMP)(W|X)(r|i)$")>;
def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "^(CSEL|CSINC|CSINV|CSNEG)(W|X)r$")>;


// Move and Shift Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instrs ADR, ADRP)>;
def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "^MOV[NZK][WX]i")>;
def : InstRW<[TSV110Wr_1cyc_1ALUAB], (instregex "(LSLV|LSRV|ASRV|RORV)(W|X)r")>;


// Divide and Multiply Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_12cyc_1MDU],  (instregex "^(S|U)DIVWr$")>;
def : InstRW<[TSV110Wr_20cyc_1MDU],  (instregex "^(S|U)DIVXr$")>;

def TSV110ReadMAW : SchedReadAdvance<2, [TSV110Wr_3cyc_1MDU]>;
def : InstRW<[TSV110Wr_3cyc_1MDU, ReadIM, ReadIM, TSV110ReadMAW], (instrs MADDWrrr, MSUBWrrr)>;
def TSV110ReadMAQ : SchedReadAdvance<3, [TSV110Wr_4cyc_1MDU]>;
def : InstRW<[TSV110Wr_4cyc_1MDU, ReadIM, ReadIM, TSV110ReadMAQ], (instrs MADDXrrr, MSUBXrrr)>;
def : InstRW<[TSV110Wr_3cyc_1MDU, ReadIM, ReadIM, TSV110ReadMAW], (instregex "(S|U)(MADDL|MSUBL)rrr")>;
def : InstRW<[TSV110Wr_4cyc_1MDU], (instregex "^(S|U)MULHrr$")>;


// Miscellaneous Data-Processing Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1ALUAB],    (instregex "^EXTR(W|X)rri$")>;
def : InstRW<[TSV110Wr_1cyc_1ALUAB],    (instregex "^(S|U)?BFM(W|X)ri$")>;
def : InstRW<[TSV110Wr_1cyc_1ALUAB],    (instregex "^(CLS|CLZ|RBIT|REV(16|32)?)(W|X)r$")>;


// Load Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDR(W|X)l$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instrs LDRSWl)>;

def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDR(BB|HH|W|X)ui$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDRS(BW|BX|HW|HX|W)ui$")>;

def : InstRW<[WriteAdr, TSV110Wr_4cyc_1LdSt],     (instregex "^LDR(BB|HH|W|X)(post|pre)$")>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1LdSt],     (instregex "^LDRS(BW|BX|HW|HX|W)(post|pre)$")>;

def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDTR(B|H|W|X)i$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDUR(BB|HH|W|X)i$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDTRS(BW|BX|HW|HX|W)i$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^LDURS(BW|BX|HW|HX|W)i$")>;

def : InstRW<[TSV110Wr_4cyc_1LdSt, WriteLDHi],     (instregex "^LDNP(W|X)i$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt, WriteLDHi],     (instregex "^LDP(W|X)i$")>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1LdSt_1ALUAB, WriteLDHi],(instregex "^LDP(W|X)(post|pre)$")>;

def : InstRW<[TSV110Wr_4cyc_1LdSt, WriteLDHi],           (instrs LDPSWi)>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1LdSt, WriteLDHi], (instrs LDPSWpost)>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1LdSt, WriteLDHi], (instrs LDPSWpre)>;

def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instrs PRFMl)>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instrs PRFUMi)>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^PRFMui$")>;
def : InstRW<[TSV110Wr_4cyc_1LdSt],     (instregex "^PRFMro(W|X)$")>;


// Store Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1LdSt],            (instregex "^STN?P(W|X)i$")>;
def : InstRW<[WriteAdr, TSV110Wr_1cyc_1LdSt],  (instregex "^STP(W|X)(post|pre)$")>;
def : InstRW<[TSV110Wr_1cyc_1LdSt],            (instregex "^STUR(BB|HH|W|X)i$")>;
def : InstRW<[TSV110Wr_1cyc_1LdSt],            (instregex "^STTR(B|H|W|X)i$")>;
def : InstRW<[TSV110Wr_1cyc_1LdSt],            (instregex "^STR(BB|HH|W|X)ui$")>;

def : InstRW<[WriteAdr, TSV110Wr_1cyc_1LdSt],  (instregex "^STR(BB|HH|W|X)(post|pre)$")>;
def : InstRW<[WriteAdr, TSV110Wr_1cyc_1LdSt],  (instregex "^STR(BB|HH|W|X)ro(W|X)$")>;


// FP Data Processing Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_2cyc_1F], (instregex "F(ABS|NEG)(D|S)r")>;
def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^FCCMP(E)?(S|D)rr$")>;
def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^FCMP(E)?(S|D)r(r|i)$")>;
def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^FCSEL(S|D)rrr$")>;

def : InstRW<[TSV110Wr_11cyc_1FSU1], (instrs FDIVSrr)>;
def : InstRW<[TSV110Wr_18cyc_1FSU1], (instrs FDIVDrr)>;
def : InstRW<[TSV110Wr_17cyc_1FSU2], (instrs FSQRTSr)>;
def : InstRW<[TSV110Wr_31cyc_1FSU2], (instrs FSQRTDr)>;

def : InstRW<[TSV110Wr_2cyc_1F], (instregex "^F(MAX|MIN).+rr")>;

def : InstRW<[TSV110Wr_4cyc_1F], (instregex "^FN?M(ADD|SUB)Hrrr")>;
def : InstRW<[TSV110Wr_5cyc_1F], (instregex "^FN?M(ADD|SUB)Srrr")>;
def : InstRW<[TSV110Wr_7cyc_1F], (instregex "^FN?M(ADD|SUB)Drrr")>;

def : InstRW<[TSV110Wr_4cyc_1F], (instregex "^F(ADD|SUB)Hrr")>;
def : InstRW<[TSV110Wr_5cyc_1F], (instregex "^F(ADD|SUB)Srr")>;
def : InstRW<[TSV110Wr_4cyc_1F], (instregex "^F(ADD|SUB)Drr")>;

def : InstRW<[TSV110Wr_4cyc_1F], (instregex "^F(N)?MULHrr$")>;
def : InstRW<[TSV110Wr_5cyc_1F], (instregex "^F(N)?MULSrr$")>;
def : InstRW<[TSV110Wr_5cyc_1F], (instregex "^F(N)?MULDrr$")>;

def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^FRINT.+r")>;


// FP Miscellaneous Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_5cyc_1ALU_1F], (instregex "^[SU]CVTF[SU][WX][SD]ri")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1],   (instregex "^FCVT(A|M|N|P|Z)(S|U)U(W|X)(S|D)r$")>;
def : InstRW<[TSV110Wr_3cyc_1F],      (instregex "^FCVT[HSD][HSD]r")>;

def : InstRW<[TSV110Wr_2cyc_1FSU1],   (instregex "^FMOV(DX|WS|XD|SW|DXHigh|XDHigh)r$")>;
def : InstRW<[TSV110Wr_2cyc_1F],      (instregex "^FMOV[SD][ir]$")>;


// FP Load Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_5cyc_1LdSt],                      (instregex "^LDR[DSQ]l")>;
def : InstRW<[TSV110Wr_5cyc_1LdSt],                      (instregex "^LDUR[BDHSQ]i")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1LdSt],            (instregex "^LDR[BDHSQ](post|pre)")>;
def : InstRW<[TSV110Wr_5cyc_1LdSt],                      (instregex "^LDR[BDHSQ]ui")>;
def : InstRW<[TSV110Wr_6cyc_1LdSt_1ALUAB, ReadAdrBase],  (instregex "^LDR(Q|D|H|S|B)ro(W|X)$")>;
def : InstRW<[TSV110Wr_5cyc_1LdSt, WriteLDHi],           (instregex "^LDN?P[DQS]i")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1LdSt, WriteLDHi], (instregex "^LDP[DQS](post|pre)")>;


// FP Store Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_1cyc_1LdSt],                     (instregex "^STUR[BHSDQ]i")>;
def : InstRW<[TSV110Wr_1cyc_1LdSt_1ALUAB, ReadAdrBase], (instregex "^STR[BHSDQ](post|pre)")>;
def : InstRW<[TSV110Wr_1cyc_1LdSt],                     (instregex "^STR[BHSDQ]ui")>;
def : InstRW<[TSV110Wr_2cyc_1LdSt_1ALUAB, ReadAdrBase], (instregex "^STR[BHSDQ]ro[WX]")>;
def : InstRW<[TSV110Wr_2cyc_2LdSt],                     (instregex "^STN?P[SDQ]i")>;
def : InstRW<[WriteAdr, TSV110Wr_2cyc_2LdSt],           (instregex "^STP[SDQ](post|pre)")>;


// ASIMD Integer Instructions
// -----------------------------------------------------------------------------

// Reference for forms in this group
//   D form - v8i8, v4i16, v2i32
//   Q form - v16i8, v8i16, v4i32
//   D form - v1i8, v1i16, v1i32, v1i64
//   Q form - v16i8, v8i16, v4i32, v2i64
//   D form - v8i8_v8i16, v4i16_v4i32, v2i32_v2i64
//   Q form - v16i8_v8i16, v8i16_v4i32, v4i32_v2i64

// ASIMD simple arithmetic
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^(ABS|ADD(P)?|NEG|SUB)v")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^[SU](ADD(L|LP|W)|SUB(L|W))v")>;

// ASIMD complex arithmetic
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]H(ADD|SUB)v")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^R?(ADD|SUB)HN2?v")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]Q(ADD|SUB)v")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^(SU|US)QADDv")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]RHADDv")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]ABAL?v")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]ABDL?v")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]ADALPv")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^((SQ)(ABS|NEG))v")>;

// ASIMD compare
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^CM(EQ|GE|GT|HI|HS|LE|LT|TST)v")>;

// ASIMD max/min
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^[SU](MIN|MAX)P?v")>;

// ASIMD logical
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^(AND|BIC|BIF|BIT|BSL|EOR|MVN|NOT|ORN|ORR)v")>;

// ASIMD multiply accumulate, D-form
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^(MUL|ML[AS]|SQR?D(MULH))(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)")>;
// ASIMD multiply accumulate, Q-form
def : InstRW<[TSV110Wr_8cyc_2FSU1], (instregex "^(MUL|ML[AS]|SQR?D(MULH))(v16i8|v8i16|v4i32)")>;

// ASIMD multiply accumulate long
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "(S|U|SQD)(MLAL|MLSL|MULL)v.*")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1], (instregex "^PMULL(v8i8|v16i8)")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1], (instregex "^PMULL(v1i64|v2i64)")>;

// ASIMD shift
// ASIMD shift accumulate
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^(S|SR|U|UR)SRA")>;
// ASIMD shift by immed, basic
def : InstRW<[TSV110Wr_4cyc_1FSU1],
            (instregex "SHLv","SLIv","SRIv","SHRNv","SQXTNv","SQXTUNv","UQXTNv")>;
// ASIMD shift by immed, complex
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^[SU]?(Q|R){1,2}SHR")>;
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^SQSHLU")>;
// ASIMD shift by register, basic, Q-form
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// ASIMD shift by register, complex, D-form
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^[SU][QR]{1,2}SHL(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32|b|d|h|s)")>;
// ASIMD shift by register, complex, Q-form
def : InstRW<[TSV110Wr_4cyc_1FSU1], (instregex "^[SU][QR]{1,2}SHL(v16i8|v8i16|v4i32|v2i64)")>;

// ASIMD reduction
// ASIMD arith, reduce, 4H/4S
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v$")>;
// ASIMD arith, reduce, 8B/8H
def : InstRW<[TSV110Wr_8cyc_1FSU1_1FSU2], (instregex "^[SU]?ADDL?V(v8i16|v4i32)v$")>;
// ASIMD arith, reduce, 16B
def : InstRW<[TSV110Wr_8cyc_1FSU1_1FSU2], (instregex "^[SU]?ADDL?Vv16i8v$")>;

// ASIMD max/min, reduce, 4H/4S
def : InstRW<[TSV110Wr_4cyc_1FSU1_1FSU2], (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v$")>;
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[TSV110Wr_8cyc_1FSU1_1FSU2], (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v$")>;
// ASIMD max/min, reduce, 16B
def : InstRW<[TSV110Wr_8cyc_1FSU1_1FSU2], (instregex "^[SU](MIN|MAX)Vv16i8v$")>;


// Vector - Floating Point
// -----------------------------------------------------------------------------

// Reference for forms in this group
//   D form - v2f32
//   Q form - v4f32, v2f64
//   D form - 32, 64
//   D form - v1i32, v1i64
//   D form - v2i32
//   Q form - v4i32, v2i64

// ASIMD FP sign manipulation
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^FABSv")>;
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^FNEGv")>;

// ASIMD FP compare
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v")>;

// ASIMD FP convert
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^FCVT[AMNPZ][SU]v")>;
def : InstRW<[TSV110Wr_3cyc_1F],  (instregex "^FCVT(L)v")>;
def : InstRW<[TSV110Wr_5cyc_1F],  (instregex "^FCVT(N|XN)v")>;

// ASIMD FP divide, D-form, F32
def : InstRW<[TSV110Wr_11cyc_1FSU1], (instregex "FDIVv2f32")>;
// ASIMD FP divide, Q-form, F32
def : InstRW<[TSV110Wr_24cyc_1FSU1], (instregex "FDIVv4f32")>;
// ASIMD FP divide, Q-form, F64
def : InstRW<[TSV110Wr_38cyc_1FSU1], (instregex "FDIVv2f64")>;

// ASIMD FP SQRT
def : InstRW<[TSV110Wr_17cyc_1FSU2], (instrs FSQRTv2f32)>;
def : InstRW<[TSV110Wr_36cyc_1FSU2], (instrs FSQRTv4f32)>;
def : InstRW<[TSV110Wr_64cyc_1FSU2], (instrs FSQRTv2f64)>;

// ASIMD FP max,min
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^F(MAX|MIN)(NM)?v")>;
def : InstRW<[TSV110Wr_2cyc_1F],  (instregex "^F(MAX|MIN)(NM)?Pv")>;
def : InstRW<[TSV110Wr_4cyc_1F],  (instregex "^F(MAX|MIN)(NM)?Vv")>;

// ASIMD FP add
def : InstRW<[TSV110Wr_5cyc_1F],  (instregex "^F(ADD|ADDP|SUB)v")>;

// ASIMD FP multiply
def : InstRW<[TSV110Wr_5cyc_1F],  (instregex "^FMULX?v")>;


// ASIMD Miscellaneous Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^(CLS|CLZ|CNT)v")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^(DUP|INS)v.+lane")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^REV(16|32|64)v")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^(UZP|ZIP)[12]v")>;

def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^EXTv")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^XTNv")>;
def : InstRW<[TSV110Wr_2cyc_1FSU1_1FSU2], (instregex "^RBITv")>;

def : InstRW<[TSV110Wr_4cyc_1F], (instregex "^(INS|DUP)v.+gpr")>;

def : InstRW<[TSV110Wr_3cyc_1FSU1], (instregex "^[SU]MOVv")>;

// ASIMD table lookup, D-form
def : InstRW<[TSV110Wr_2cyc_1F], (instregex "^TB[LX]v8i8One")>;
def : InstRW<[TSV110Wr_4cyc_2F], (instregex "^TB[LX]v8i8Two")>;
def : InstRW<[TSV110Wr_6cyc_3F], (instregex "^TB[LX]v8i8Three")>;
def : InstRW<[TSV110Wr_8cyc_4F], (instregex "^TB[LX]v8i8Four")>;
// ASIMD table lookup, Q-form
def : InstRW<[TSV110Wr_2cyc_1F], (instregex "^TB[LX]v16i8One")>;
def : InstRW<[TSV110Wr_4cyc_2F], (instregex "^TB[LX]v16i8Two")>;
def : InstRW<[TSV110Wr_6cyc_3F], (instregex "^TB[LX]v16i8Three")>;
def : InstRW<[TSV110Wr_8cyc_4F], (instregex "^TB[LX]v16i8Four")>;

def : InstRW<[TSV110Wr_2cyc_1F], (instregex "^FMOVv")>;

def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^FRINT[AIMNPXZ]v")>;
def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^[SU]CVTFv")>;
def : InstRW<[TSV110Wr_3cyc_1F], (instregex "^[FU](RECP|RSQRT)(E|X)v")>;


// ASIMD Load Instructions
// -----------------------------------------------------------------------------

def : InstRW<[TSV110Wr_7cyc_1F_1LdSt],            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_7cyc_1F_1LdSt],  (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_7cyc_2F_1LdSt],            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_7cyc_2F_1LdSt],  (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_8cyc_3F_1LdSt],            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_8cyc_3F_1LdSt],  (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_8cyc_3F_2LdSt],            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_8cyc_3F_2LdSt],  (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def  : InstRW<[TSV110Wr_7cyc_1F_1LdSt],           (instregex "LD1i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_7cyc_1F_1LdSt], (instregex "LD1i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_7cyc_2F_1LdSt],           (instregex "LD2i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_7cyc_2F_1LdSt], (instregex "LD2i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_8cyc_3F_1LdSt],           (instregex "LD3i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_8cyc_3F_1LdSt], (instregex "LD3i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_8cyc_3F_2LdSt],           (instregex "LD4i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_8cyc_3F_2LdSt], (instregex "LD4i(8|16|32|64)_POST$")>;

def : InstRW<[TSV110Wr_5cyc_1LdSt],               (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1LdSt],     (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_5cyc_1LdSt],               (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1LdSt],     (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_6cyc_3LdSt],               (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_6cyc_3LdSt],     (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_6cyc_2LdSt],               (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_6cyc_2LdSt],     (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_7cyc_2F_1LdSt],            (instregex "^LD2Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_7cyc_2F_1LdSt],  (instregex "^LD2Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_8cyc_3F_1LdSt],            (instregex "^LD3Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_8cyc_3F_1LdSt],  (instregex "^LD3Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_10cyc_4F_4LdSt],           (instregex "^LD4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_10cyc_4F_4LdSt], (instregex "^LD4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;


// ASIMD Store Instructions
// -----------------------------------------------------------------------------

def  : InstRW<[TSV110Wr_3cyc_1F],             (instregex "ST1i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_3cyc_1F],   (instregex "ST1i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_4cyc_1F],             (instregex "ST2i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_4cyc_1F],   (instregex "ST2i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_5cyc_1F],             (instregex "ST3i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_5cyc_1F],   (instregex "ST3i(8|16|32|64)_POST$")>;
def  : InstRW<[TSV110Wr_6cyc_1F],             (instregex "ST4i(8|16|32|64)$")>;
def  : InstRW<[WriteAdr, TSV110Wr_6cyc_1F],   (instregex "ST4i(8|16|32|64)_POST$")>;

def : InstRW<[TSV110Wr_3cyc_1F],              (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_3cyc_1F],    (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_4cyc_1F],              (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1F],    (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_5cyc_1F],              (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1F],    (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[TSV110Wr_6cyc_1F],              (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_6cyc_1F],    (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_4cyc_1F],              (instregex "^ST2Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_4cyc_1F],    (instregex "^ST2Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_5cyc_1F],              (instregex "^ST3Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_5cyc_1F],    (instregex "^ST3Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[TSV110Wr_8cyc_1F],              (instregex "^ST4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, TSV110Wr_8cyc_1F],    (instregex "^ST4Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

} // SchedModel = TSV110Model
