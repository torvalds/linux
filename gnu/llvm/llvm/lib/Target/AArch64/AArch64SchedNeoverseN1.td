//=- AArch64SchedNeoverseN1.td - NeoverseN1 Scheduling Model -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for the Arm Neoverse N1 processors.
//
// References:
// - "Arm Neoverse N1 Software Optimization Guide"
// - https://en.wikichip.org/wiki/arm_holdings/microarchitectures/neoverse_n1
//
//===----------------------------------------------------------------------===//

def NeoverseN1Model : SchedMachineModel {
  let IssueWidth            =   8; // Maximum micro-ops dispatch rate.
  let MicroOpBufferSize     = 128; // NOTE: Copied from Cortex-A76.
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  11; // Cycles cost of branch mispredicted.
  let LoopMicroOpBufferSize =  16; // NOTE: Copied from Cortex-A57.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    SVEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Neoverse N1.
// Instructions are first fetched and then decoded into internal macro-ops
// (MOPs).  From there, the MOPs proceed through register renaming and dispatch
// stages.  A MOP can be split into one or more micro-ops further down the
// pipeline, after the decode stage.  Once dispatched, micro-ops wait for their
// operands and issue out-of-order to one of the issue pipelines.  Each issue
// pipeline can accept one micro-op per cycle.

let SchedModel = NeoverseN1Model in {

// Define the issue ports.
def N1UnitB  : ProcResource<1>;  // Branch
def N1UnitS  : ProcResource<2>;  // Integer single cycle 0/1
def N1UnitM  : ProcResource<1>;  // Integer multicycle
def N1UnitL  : ProcResource<2>;  // Load/Store 0/1
def N1UnitD  : ProcResource<2>;  // Store data 0/1
def N1UnitV0 : ProcResource<1>;  // FP/ASIMD 0
def N1UnitV1 : ProcResource<1>;  // FP/ASIMD 1

def N1UnitI : ProcResGroup<[N1UnitS, N1UnitM]>;    // Integer units
def N1UnitV : ProcResGroup<[N1UnitV0, N1UnitV1]>;  // FP/ASIMD units

// Define commonly used read types.

// No generic forwarding is provided for these types.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     0>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadST,      0>;
def : ReadAdvance<ReadVLD,     0>;

def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }


//===----------------------------------------------------------------------===//
// Define generic 0 micro-op types

let Latency = 0, NumMicroOps = 0 in
def N1Write_0c_0Z : SchedWriteRes<[]>;

//===----------------------------------------------------------------------===//
// Define generic 1 micro-op types

def N1Write_1c_1B     : SchedWriteRes<[N1UnitB]>  { let Latency = 1; }
def N1Write_1c_1I     : SchedWriteRes<[N1UnitI]>  { let Latency = 1; }
def N1Write_2c_1M     : SchedWriteRes<[N1UnitM]>  { let Latency = 2; }
def N1Write_3c_1M     : SchedWriteRes<[N1UnitM]>  { let Latency = 3; }
def N1Write_4c3_1M    : SchedWriteRes<[N1UnitM]>  { let Latency = 4;
                                                    let ReleaseAtCycles = [3]; }
def N1Write_5c3_1M    : SchedWriteRes<[N1UnitM]>  { let Latency = 5;
                                                    let ReleaseAtCycles = [3]; }
def N1Write_12c5_1M   : SchedWriteRes<[N1UnitM]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [5]; }
def N1Write_20c5_1M   : SchedWriteRes<[N1UnitM]>  { let Latency = 20;
                                                    let ReleaseAtCycles = [5]; }
def N1Write_4c_1L     : SchedWriteRes<[N1UnitL]>  { let Latency = 4; }
def N1Write_5c_1L     : SchedWriteRes<[N1UnitL]>  { let Latency = 5; }
def N1Write_7c_1L     : SchedWriteRes<[N1UnitL]>  { let Latency = 7; }
def N1Write_2c_1V     : SchedWriteRes<[N1UnitV]>  { let Latency = 2; }
def N1Write_3c_1V     : SchedWriteRes<[N1UnitV]>  { let Latency = 3; }
def N1Write_4c_1V     : SchedWriteRes<[N1UnitV]>  { let Latency = 4; }
def N1Write_5c_1V     : SchedWriteRes<[N1UnitV]>  { let Latency = 5; }
def N1Write_2c_1V0    : SchedWriteRes<[N1UnitV0]> { let Latency = 2; }
def N1Write_3c_1V0    : SchedWriteRes<[N1UnitV0]> { let Latency = 3; }
def N1Write_4c_1V0    : SchedWriteRes<[N1UnitV0]> { let Latency = 4; }
def N1Write_7c7_1V0   : SchedWriteRes<[N1UnitV0]> { let Latency = 7;
                                                    let ReleaseAtCycles = [7]; }
def N1Write_10c7_1V0  : SchedWriteRes<[N1UnitV0]> { let Latency = 10;
                                                    let ReleaseAtCycles = [7]; }
def N1Write_13c10_1V0 : SchedWriteRes<[N1UnitV0]> { let Latency = 13;
                                                    let ReleaseAtCycles = [10]; }
def N1Write_15c7_1V0  : SchedWriteRes<[N1UnitV0]> { let Latency = 15;
                                                    let ReleaseAtCycles = [7]; }
def N1Write_17c7_1V0  : SchedWriteRes<[N1UnitV0]> { let Latency = 17;
                                                    let ReleaseAtCycles = [7]; }
def N1Write_2c_1V1    : SchedWriteRes<[N1UnitV1]> { let Latency = 2; }
def N1Write_3c_1V1    : SchedWriteRes<[N1UnitV1]> { let Latency = 3; }
def N1Write_4c_1V1    : SchedWriteRes<[N1UnitV1]> { let Latency = 4; }

//===----------------------------------------------------------------------===//
// Define generic 2 micro-op types

let Latency = 1, NumMicroOps = 2 in
def N1Write_1c_1B_1I   : SchedWriteRes<[N1UnitB, N1UnitI]>;
let Latency = 3, NumMicroOps = 2 in
def N1Write_3c_1I_1M   : SchedWriteRes<[N1UnitI, N1UnitM]>;
let Latency = 2, NumMicroOps = 2 in
def N1Write_2c_1I_1L   : SchedWriteRes<[N1UnitI, N1UnitL]>;
let Latency = 5, NumMicroOps = 2 in
def N1Write_5c_1I_1L   : SchedWriteRes<[N1UnitI, N1UnitL]>;
let Latency = 6, NumMicroOps = 2 in
def N1Write_6c_1I_1L   : SchedWriteRes<[N1UnitI, N1UnitL]>;
let Latency = 7, NumMicroOps = 2 in
def N1Write_7c_1I_1L   : SchedWriteRes<[N1UnitI, N1UnitL]>;
let Latency = 5, NumMicroOps = 2 in
def N1Write_5c_1M_1V   : SchedWriteRes<[N1UnitM, N1UnitV]>;
let Latency = 6, NumMicroOps = 2 in
def N1Write_6c_1M_1V0  : SchedWriteRes<[N1UnitM, N1UnitV0]>;
let Latency = 5, NumMicroOps = 2 in
def N1Write_5c_2L      : SchedWriteRes<[N1UnitL, N1UnitL]>;
let Latency = 1, NumMicroOps = 2 in
def N1Write_1c_1L_1D   : SchedWriteRes<[N1UnitL, N1UnitD]>;
let Latency = 2, NumMicroOps = 2 in
def N1Write_2c_1L_1V   : SchedWriteRes<[N1UnitL, N1UnitV]>;
let Latency = 4, NumMicroOps = 2 in
def N1Write_4c_1L_1V   : SchedWriteRes<[N1UnitL, N1UnitV]>;
let Latency = 7, NumMicroOps = 2 in
def N1Write_7c_1L_1V   : SchedWriteRes<[N1UnitL, N1UnitV]>;
let Latency = 4, NumMicroOps = 2 in
def N1Write_4c_1V0_1V1 : SchedWriteRes<[N1UnitV0, N1UnitV1]>;
let Latency = 4, NumMicroOps = 2 in
def N1Write_4c_2V0     : SchedWriteRes<[N1UnitV0, N1UnitV0]>;
let Latency = 5, NumMicroOps = 2 in
def N1Write_5c_2V0     : SchedWriteRes<[N1UnitV0, N1UnitV0]>;
let Latency = 6, NumMicroOps = 2 in
def N1Write_6c_2V1     : SchedWriteRes<[N1UnitV1, N1UnitV1]>;
let Latency = 5, NumMicroOps = 2 in
def N1Write_5c_1V1_1V  : SchedWriteRes<[N1UnitV1, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 3 micro-op types

let Latency = 7, NumMicroOps = 3 in
def N1Write_2c_1I_1L_1V : SchedWriteRes<[N1UnitI, N1UnitL, N1UnitV]>;
let Latency = 1, NumMicroOps = 3 in
def N1Write_1c_2L_1D    : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitD]>;
let Latency = 2, NumMicroOps = 3 in
def N1Write_2c_1L_2V    : SchedWriteRes<[N1UnitL, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 3 in
def N1Write_6c_3L       : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL]>;
let Latency = 4, NumMicroOps = 3 in
def N1Write_4c_3V       : SchedWriteRes<[N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 3 in
def N1Write_6c_3V       : SchedWriteRes<[N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 8, NumMicroOps = 3 in
def N1Write_8c_3V       : SchedWriteRes<[N1UnitV, N1UnitV, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 4 micro-op types

let Latency = 2, NumMicroOps = 4 in
def N1Write_2c_2I_2L : SchedWriteRes<[N1UnitI, N1UnitI, N1UnitL, N1UnitL]>;
let Latency = 6, NumMicroOps = 4 in
def N1Write_6c_4L    : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL]>;
let Latency = 2, NumMicroOps = 4 in
def N1Write_2c_2L_2V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitV, N1UnitV]>;
let Latency = 2, NumMicroOps = 4 in
def N1Write_3c_2L_2V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitV, N1UnitV]>;
let Latency = 5, NumMicroOps = 4 in
def N1Write_5c_2L_2V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitV, N1UnitV]>;
let Latency = 7, NumMicroOps = 4 in
def N1Write_7c_2L_2V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitV, N1UnitV]>;
let Latency = 4, NumMicroOps = 4 in
def N1Write_4c_4V    : SchedWriteRes<[N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 4 in
def N1Write_6c_4V0   : SchedWriteRes<[N1UnitV0, N1UnitV0, N1UnitV0, N1UnitV0]>;

//===----------------------------------------------------------------------===//
// Define generic 5 micro-op types

let Latency = 3, NumMicroOps = 5 in
def N1Write_3c_2L_3V : SchedWriteRes<[N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 7, NumMicroOps = 5 in
def N1Write_7c_2L_3V : SchedWriteRes<[N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 5 in
def N1Write_6c_5V    : SchedWriteRes<[N1UnitV, N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 6 micro-op types

let Latency = 3, NumMicroOps = 6 in
def N1Write_3c_4L_2V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV]>;
let Latency = 4, NumMicroOps = 6 in
def N1Write_4c_3L_3V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 5, NumMicroOps = 6 in
def N1Write_5c_3L_3V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 6 in
def N1Write_6c_3L_3V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 7, NumMicroOps = 6 in
def N1Write_7c_3L_3V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 8, NumMicroOps = 6 in
def N1Write_8c_3L_3V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 7 micro-op types

let Latency = 8, NumMicroOps = 7 in
def N1Write_8c_3L_4V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 8 micro-op types

let Latency = 5, NumMicroOps = 8 in
def N1Write_5c_4L_4V  : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL, 
                                       N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 6, NumMicroOps = 8 in
def N1Write_6c_4L_4V  : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL, 
                                       N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 8, NumMicroOps = 8 in
def N1Write_8c_4L_4V  : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL,
                                       N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;
let Latency = 10, NumMicroOps = 8 in
def N1Write_10c_4L_4V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL, N1UnitL,
                                       N1UnitV, N1UnitV, N1UnitV, N1UnitV]>;

//===----------------------------------------------------------------------===//
// Define generic 12 micro-op types

let Latency = 9, NumMicroOps = 12 in
def N1Write_9c_6L_6V : SchedWriteRes<[N1UnitL, N1UnitL, N1UnitL,
                                      N1UnitL, N1UnitL, N1UnitL, 
                                      N1UnitV, N1UnitV, N1UnitV,
                                      N1UnitV, N1UnitV, N1UnitV]>;


// Miscellaneous Instructions
// -----------------------------------------------------------------------------

def : InstRW<[WriteI], (instrs COPY)>;

// Convert floating-point condition flags
// Flag manipulation instructions
def : WriteRes<WriteSys, []> { let Latency = 1; }


// Branch Instructions
// -----------------------------------------------------------------------------

// Branch, immed
// Compare and branch
def : SchedAlias<WriteBr, N1Write_1c_1B>;

// Branch, register
def : SchedAlias<WriteBrReg, N1Write_1c_1B>;

// Branch and link, immed
// Branch and link, register
def : InstRW<[N1Write_1c_1B_1I], (instrs BL, BLR)>;

// Compare and branch
def : InstRW<[N1Write_1c_1B], (instregex "^[CT]BN?Z[XW]$")>;


// Arithmetic and Logical Instructions
// -----------------------------------------------------------------------------

// ALU, basic
// ALU, basic, flagset
// Conditional compare
// Conditional select
// Logical, basic
// Address generation
// Count leading
// Reverse bits/bytes
// Move immediate
def : SchedAlias<WriteI, N1Write_1c_1I>;

// ALU, extend and shift
def : SchedAlias<WriteIEReg, N1Write_2c_1M>;

// Arithmetic, LSL shift, shift <= 4
// Arithmetic, flagset, LSL shift, shift <= 4
// Arithmetic, LSR/ASR/ROR shift or LSL shift > 4
def N1WriteISReg : SchedWriteVariant<[
                     SchedVar<IsCheapLSL,  [N1Write_1c_1I]>,
                     SchedVar<NoSchedPred, [N1Write_2c_1M]>]>;
def              : SchedAlias<WriteISReg, N1WriteISReg>;

// Logical, shift, no flagset
def : InstRW<[N1Write_1c_1I],
             (instregex "^(AND|BIC|EON|EOR|ORN|ORR)[WX]rs$")>;

// Logical, shift, flagset
def : InstRW<[N1Write_2c_1M], (instregex "^(AND|BIC)S[WX]rs$")>;


// Divide and multiply instructions
// -----------------------------------------------------------------------------

// Divide
def : SchedAlias<WriteID32, N1Write_12c5_1M>;
def : SchedAlias<WriteID64, N1Write_20c5_1M>;

// Multiply accumulate
// Multiply accumulate, long
def : SchedAlias<WriteIM32, N1Write_2c_1M>;
def : SchedAlias<WriteIM64, N1Write_4c3_1M>;

// Multiply high
def : InstRW<[N1Write_5c3_1M, ReadIM, ReadIM], (instrs SMULHrr, UMULHrr)>;


// Miscellaneous data-processing instructions
// -----------------------------------------------------------------------------

// Bitfield extract, one reg
// Bitfield extract, two regs
def N1WriteExtr : SchedWriteVariant<[
                    SchedVar<IsRORImmIdiomPred, [N1Write_1c_1I]>,
                    SchedVar<NoSchedPred,       [N1Write_3c_1I_1M]>]>;
def : SchedAlias<WriteExtr, N1WriteExtr>;

// Bitfield move, basic
// Variable shift
def : SchedAlias<WriteIS, N1Write_1c_1I>;

// Bitfield move, insert
def : InstRW<[N1Write_2c_1M], (instregex "^BFM[WX]ri$")>;

// Move immediate
def : SchedAlias<WriteImm, N1Write_1c_1I>;

// Load instructions
// -----------------------------------------------------------------------------

// Load register, immed offset
def : SchedAlias<WriteLD, N1Write_4c_1L>;

// Load register, immed offset, index
def : SchedAlias<WriteLDIdx, N1Write_4c_1L>;
def : SchedAlias<WriteAdr,   N1Write_1c_1I>;

// Load pair, immed offset
def : SchedAlias<WriteLDHi, N1Write_4c_1L>;

// Load pair, immed offset, W-form
def : InstRW<[N1Write_4c_1L, N1Write_0c_0Z], (instrs LDPWi, LDNPWi)>;

// Load pair, signed immed offset, signed words
def : InstRW<[N1Write_5c_1I_1L, N1Write_0c_0Z], (instrs LDPSWi)>;

// Load pair, immed post or pre-index, signed words
def : InstRW<[WriteAdr, N1Write_5c_1I_1L, N1Write_0c_0Z],
             (instrs LDPSWpost, LDPSWpre)>;


// Store instructions
// -----------------------------------------------------------------------------

// Store register, immed offset
def : SchedAlias<WriteST, N1Write_1c_1L_1D>;

// Store register, immed offset, index
def : SchedAlias<WriteSTIdx, N1Write_1c_1L_1D>;

// Store pair, immed offset
def : SchedAlias<WriteSTP, N1Write_1c_2L_1D>;

// Store pair, immed offset, W-form
def : InstRW<[N1Write_1c_1L_1D], (instrs STPWi)>;


// FP data processing instructions
// -----------------------------------------------------------------------------

// FP absolute value
// FP arithmetic
// FP min/max
// FP negate
// FP select
def : SchedAlias<WriteF, N1Write_2c_1V>;

// FP compare
def : SchedAlias<WriteFCmp, N1Write_2c_1V0>;

// FP divide
// FP square root
def : SchedAlias<WriteFDiv, N1Write_10c7_1V0>;

// FP divide, H-form
// FP square root, H-form
def : InstRW<[N1Write_7c7_1V0], (instrs FDIVHrr, FSQRTHr)>;

// FP divide, S-form
// FP square root, S-form
def : InstRW<[N1Write_10c7_1V0], (instrs FDIVSrr, FSQRTSr)>;

// FP divide, D-form
def : InstRW<[N1Write_15c7_1V0], (instrs FDIVDrr)>;

// FP square root, D-form
def : InstRW<[N1Write_17c7_1V0], (instrs FSQRTDr)>;

// FP multiply
def : SchedAlias<WriteFMul, N1Write_3c_1V>;

// FP multiply accumulate
def : InstRW<[N1Write_4c_1V], (instregex "^FN?M(ADD|SUB)[HSD]rrr$")>;

// FP round to integral
def : InstRW<[N1Write_3c_1V0], (instregex "^FRINT[AIMNPXZ][HSD]r$",
                                          "^FRINT(32|64)[XZ][SD]r$")>;


// FP miscellaneous instructions
// -----------------------------------------------------------------------------

// FP convert, from vec to vec reg
// FP convert, Javascript from vec to gen reg
def : SchedAlias<WriteFCvt, N1Write_3c_1V>;

// FP convert, from gen to vec reg
def : InstRW<[N1Write_6c_1M_1V0], (instregex "^[SU]CVTF[SU][WX][HSD]ri$")>;

// FP convert, from vec to gen reg
def : InstRW<[N1Write_4c_1V0_1V1], (instregex "^FCVT[AMNPZ][SU][SU][WX][HSD]r$")>;

// FP move, immed
def : SchedAlias<WriteFImm, N1Write_2c_1V>;

// FP move, register
def : InstRW<[N1Write_2c_1V], (instrs FMOVHr, FMOVSr, FMOVDr)>;

// FP transfer, from gen to low half of vec reg
// FP transfer, from gen to high half of vec reg
def : InstRW<[N1Write_3c_1M], (instrs FMOVWHr, FMOVXHr, FMOVWSr, FMOVXDr,
                                      FMOVXDHighr)>;

// FP transfer, from vec to gen reg
def : SchedAlias<WriteFCopy, N1Write_2c_1V1>;


// FP load instructions
// -----------------------------------------------------------------------------

// Load vector reg, literal, S/D/Q forms
// Load vector reg, unscaled immed
def : InstRW<[N1Write_5c_1L, ReadAdrBase], (instregex "^LDR[SDQ]l$",
                                                      "^LDUR[BHSDQ]i$")>;

// Load vector reg, immed post-index
// Load vector reg, immed pre-index
def : InstRW<[WriteAdr, N1Write_5c_1L],
             (instregex "^LDR[BHSDQ](post|pre)$")>;

// Load vector reg, unsigned immed
def : InstRW<[N1Write_5c_1I_1L], (instregex "^LDR[BHSDQ]ui$")>;

// Load vector reg, register offset, basic
// Load vector reg, register offset, scale, S/D-form
// Load vector reg, register offset, extend
// Load vector reg, register offset, extend, scale, S/D-form
def : InstRW<[N1Write_5c_1I_1L, ReadAdrBase], (instregex "^LDR[BSD]ro[WX]$")>;

// Load vector reg, register offset, scale, H/Q-form
// Load vector reg, register offset, extend, scale, H/Q-form
def : InstRW<[N1Write_6c_1I_1L, ReadAdrBase], (instregex "^LDR[HQ]ro[WX]$")>;

// Load vector pair, immed offset, S/D-form
def : InstRW<[N1Write_5c_1I_1L, WriteLDHi], (instregex "^LDN?P[SD]i$")>;

// Load vector pair, immed offset, H/Q-form
def : InstRW<[N1Write_7c_1I_1L, WriteLDHi], (instregex "^LDPN?[HQ]i$")>;

// Load vector pair, immed post-index, S/D-form
// Load vector pair, immed pre-index, S/D-form
def : InstRW<[WriteAdr, N1Write_5c_1L, WriteLDHi],
             (instregex "^LDP[SD](pre|post)$")>;

// Load vector pair, immed post-index, Q-form
// Load vector pair, immed pre-index, Q-form
def : InstRW<[WriteAdr, N1Write_7c_1L, WriteLDHi],
             (instrs LDPQpost, LDPQpre)>;


// FP store instructions
// -----------------------------------------------------------------------------

// Store vector reg, unscaled immed, B/H/S/D-form
def : InstRW<[N1Write_2c_1I_1L], (instregex "^STUR[BHSD]i$")>;

// Store vector reg, unscaled immed, Q-form
def : InstRW<[N1Write_2c_2I_2L], (instrs STURQi)>;

// Store vector reg, immed post-index, B/H/S/D-form
// Store vector reg, immed pre-index, B/H/S/D-form
def : InstRW<[WriteAdr, N1Write_2c_1L_1V], (instregex "^STR[BHSD](pre|post)$")>;

// Store vector reg, immed pre-index, Q-form
// Store vector reg, immed post-index, Q-form
def : InstRW<[WriteAdr, N1Write_2c_2L_2V], (instrs STRQpre, STRQpost)>;

// Store vector reg, unsigned immed, B/H/S/D-form
def : InstRW<[N1Write_2c_1L_1V], (instregex "^STR[BHSD]ui$")>;

// Store vector reg, unsigned immed, Q-form
def : InstRW<[N1Write_2c_2L_2V], (instrs STRQui)>;

// Store vector reg, register offset, basic, B/S/D-form
// Store vector reg, register offset, scale, B/S/D-form
// Store vector reg, register offset, extend, B/S/D-form
// Store vector reg, register offset, extend, scale, B/S/D-form
def : InstRW<[N1Write_2c_1L_1V, ReadAdrBase], (instregex "^STR[BSD]ro[WX]$")>;

// Store vector reg, register offset, basic, H-form
// Store vector reg, register offset, scale, H-form
// Store vector reg, register offset, extend, H-form
// Store vector reg, register offset, extend, scale, H-form
def : InstRW<[N1Write_2c_1I_1L_1V, ReadAdrBase], (instregex "^STRHro[WX]$")>;

// Store vector reg, register offset, basic, Q-form
// Store vector reg, register offset, scale, Q-form
// Store vector reg, register offset, extend, Q-form
// Store vector reg, register offset, extend, scale, Q-form
def : InstRW<[N1Write_2c_2L_2V, ReadAdrBase], (instregex "^STRQro[WX]$")>;

// Store vector pair, immed offset, S-form
def : InstRW<[N1Write_2c_1L_1V], (instrs STPSi, STNPSi)>;

// Store vector pair, immed offset, D-form
def : InstRW<[N1Write_2c_2L_2V], (instrs STPDi, STNPDi)>;

// Store vector pair, immed offset, Q-form
def : InstRW<[N1Write_3c_4L_2V], (instrs STPQi, STNPQi)>;

// Store vector pair, immed post-index, S-form
// Store vector pair, immed pre-index, S-form
def : InstRW<[WriteAdr, N1Write_2c_1L_1V], (instrs STPSpre, STPSpost)>;

// Store vector pair, immed post-index, D-form
// Store vector pair, immed pre-index, D-form
def : InstRW<[WriteAdr, N1Write_2c_2L_2V], (instrs STPDpre, STPDpost)>;

// Store vector pair, immed post-index, Q-form
// Store vector pair, immed pre-index, Q-form
def : InstRW<[WriteAdr, N1Write_3c_4L_2V], (instrs STPQpre, STPQpost)>;


// ASIMD integer instructions
// -----------------------------------------------------------------------------

// ASIMD absolute diff
// ASIMD absolute diff long
// ASIMD arith, basic
// ASIMD arith, complex
// ASIMD arith, pair-wise
// ASIMD compare
// ASIMD logical
// ASIMD max/min, basic and pair-wise
def : SchedAlias<WriteVd, N1Write_2c_1V>;
def : SchedAlias<WriteVq, N1Write_2c_1V>;

// ASIMD absolute diff accum
// ASIMD absolute diff accum long
def : InstRW<[N1Write_4c_1V1], (instregex "^[SU]ABAL?v")>;

// ASIMD arith, reduce, 4H/4S
def : InstRW<[N1Write_3c_1V1], (instregex "^(ADDV|[SU]ADDLV)v4(i16|i32)v$")>;

// ASIMD arith, reduce, 8B/8H
def : InstRW<[N1Write_5c_1V1_1V], (instregex "^(ADDV|[SU]ADDLV)v8(i8|i16)v$")>;

// ASIMD arith, reduce, 16B
def : InstRW<[N1Write_6c_2V1], (instregex "^(ADDV|[SU]ADDLV)v16i8v$")>;

// ASIMD max/min, reduce, 4H/4S
def : InstRW<[N1Write_3c_1V1], (instregex "^[SU](MAX|MIN)Vv4(i16|i32)v$")>;

// ASIMD max/min, reduce, 8B/8H
def : InstRW<[N1Write_5c_1V1_1V], (instregex "^[SU](MAX|MIN)Vv8(i8|i16)v$")>;

// ASIMD max/min, reduce, 16B
def : InstRW<[N1Write_6c_2V1], (instregex "[SU](MAX|MIN)Vv16i8v$")>;

// ASIMD multiply, D-form
// ASIMD multiply accumulate, D-form
// ASIMD multiply accumulate high, D-form
// ASIMD multiply accumulate saturating long
// ASIMD multiply long
// ASIMD multiply accumulate long
def : InstRW<[N1Write_4c_1V0], (instregex "^MUL(v[14]i16|v[12]i32)$",
                                          "^ML[AS](v[14]i16|v[12]i32)$",
                                          "^SQ(R)?DMULH(v[14]i16|v[12]i32)$",
                                          "^SQRDML[AS]H(v[14]i16|v[12]i32)$",
                                          "^SQDML[AS]Lv",
                                          "^([SU]|SQD)MULLv",
                                          "^[SU]ML[AS]Lv")>;

// ASIMD multiply, Q-form
// ASIMD multiply accumulate, Q-form
// ASIMD multiply accumulate high, Q-form
def : InstRW<[N1Write_5c_2V0], (instregex "^MUL(v8i16|v4i32)$",
                                          "^ML[AS](v8i16|v4i32)$",
                                          "^SQ(R)?DMULH(v8i16|v4i32)$",
                                          "^SQRDML[AS]H(v8i16|v4i32)$")>;

// ASIMD multiply/multiply long (8x8) polynomial, D-form
def : InstRW<[N1Write_3c_1V0], (instrs PMULv8i8, PMULLv8i8)>;

// ASIMD multiply/multiply long (8x8) polynomial, Q-form
def : InstRW<[N1Write_4c_2V0], (instrs PMULv16i8, PMULLv16i8)>;

// ASIMD pairwise add and accumulate long
def : InstRW<[N1Write_4c_1V1], (instregex "^[SU]ADALPv")>;

// ASIMD shift accumulate
def : InstRW<[N1Write_4c_1V1], (instregex "^[SU]R?SRAv")>;

// ASIMD shift by immed, basic
// ASIMD shift by immed and insert, basic
// ASIMD shift by register, basic
def : InstRW<[N1Write_2c_1V1], (instregex "^SHLL?v", "^SHRNv", "^[SU]SHLLv",
                                          "^[SU]SHRv", "^S[LR]Iv", "^[SU]SHLv")>;

// ASIMD shift by immed, complex
// ASIMD shift by register, complex
def : InstRW<[N1Write_4c_1V1],
             (instregex "^RSHRNv", "^SQRSHRU?Nv", "^(SQSHLU?|UQSHL)[bhsd]$",
                        "^(SQSHLU?|UQSHL)(v8i8|v16i8|v4i16|v8i16|v2i32|v4i32|v2i64)_shift$",
                        "^SQSHU?RNv", "^[SU]RSHRv", "^UQR?SHRNv", 
                        "^[SU]Q?RSHLv", "^[SU]QSHLv")>;


// ASIMD FP instructions
// -----------------------------------------------------------------------------

// ASIMD FP absolute value/difference
// ASIMD FP arith, normal
// ASIMD FP compare
// ASIMD FP max/min, normal
// ASIMD FP max/min, pairwise
// ASIMD FP negate
// Covered by "SchedAlias (WriteV[dq]...)" above

// ASIMD FP convert, long (F16 to F32)
def : InstRW<[N1Write_4c_2V0], (instregex "^FCVTL(v4|v8)i16$")>;

// ASIMD FP convert, long (F32 to F64)
def : InstRW<[N1Write_3c_1V0], (instregex "^FCVTL(v2|v4)i32$")>;

// ASIMD FP convert, narrow (F32 to F16)
def : InstRW<[N1Write_4c_2V0], (instregex "^FCVTN(v4|v8)i16$")>;

// ASIMD FP convert, narrow (F64 to F32)
def : InstRW<[N1Write_3c_1V0], (instregex "^FCVTN(v2|v4)i32$",
                                          "^FCVTXN(v2|v4)f32$")>;

// ASIMD FP convert, other, D-form F32 and Q-form F64
def : InstRW<[N1Write_3c_1V0], (instregex "^[FSU]CVT[AMNPZ][SU]v2f(32|64)$",
                                          "^[SU]CVTFv2f(32|64)$")>;

// ASIMD FP convert, other, D-form F16 and Q-form F32
def : InstRW<[N1Write_4c_2V0], (instregex "^[FSU]CVT[AMNPZ][SU]v4f(16|32)$",
                                          "^[SU]CVTFv4f(16|32)$")>;

// ASIMD FP convert, other, Q-form F16
def : InstRW<[N1Write_6c_4V0], (instregex "^[FSU]CVT[AMNPZ][SU]v8f16$",
                                          "^[SU]CVTFv8f16$")>;

// ASIMD FP divide, D-form, F16
// ASIMD FP square root, D-form, F16
def : InstRW<[N1Write_7c7_1V0], (instrs FDIVv4f16, FSQRTv4f16)>;

// ASIMD FP divide, D-form, F32
// ASIMD FP square root, D-form, F32
def : InstRW<[N1Write_10c7_1V0], (instrs FDIVv2f32, FSQRTv2f32)>;

// ASIMD FP divide, Q-form, F16
// ASIMD FP square root, Q-form, F16
def : InstRW<[N1Write_13c10_1V0], (instrs FDIVv8f16, FSQRTv8f16)>;

// ASIMD FP divide, Q-form, F32
// ASIMD FP square root, Q-form, F32
def : InstRW<[N1Write_10c7_1V0], (instrs FDIVv4f32, FSQRTv4f32)>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[N1Write_15c7_1V0], (instrs FDIVv2f64)>;

// ASIMD FP square root, Q-form, F64
def : InstRW<[N1Write_17c7_1V0], (instrs FSQRTv2f64)>;

// ASIMD FP max/min, reduce, F32 and D-form F16
def : InstRW<[N1Write_5c_1V], (instregex "^F(MAX|MIN)(NM)?Vv4(i16|i32)v$")>;

// ASIMD FP max/min, reduce, Q-form F16
def : InstRW<[N1Write_8c_3V], (instregex "^F(MAX|MIN)(NM)?Vv8i16v$")>;

// ASIMD FP multiply
def : InstRW<[N1Write_3c_1V], (instregex "^FMULX?v")>;

// ASIMD FP multiply accumulate
def : InstRW<[N1Write_4c_1V], (instregex "^FML[AS]v")>;

// ASIMD FP multiply accumulate long
def : InstRW<[N1Write_5c_1V], (instregex "^FML[AS]L2?v")>;

// ASIMD FP round, D-form F32 and Q-form F64
def : InstRW<[N1Write_3c_1V0], (instregex "^FRINT[AIMNPXZ]v2f(32|64)$")>;

// ASIMD FP round, D-form F16 and Q-form F32
def : InstRW<[N1Write_4c_2V0], (instregex "^FRINT[AIMNPXZ]v4f(16|32)$")>;

// ASIMD FP round, Q-form F16
def : InstRW<[N1Write_6c_4V0], (instregex "^FRINT[AIMNPXZ]v8f16$")>;


// ASIMD miscellaneous instructions
// -----------------------------------------------------------------------------

// ASIMD bit reverse
// ASIMD bitwise insert
// ASIMD count
// ASIMD duplicate, element
// ASIMD extract
// ASIMD extract narrow
// ASIMD insert, element to element
// ASIMD move, FP immed
// ASIMD move, integer immed
// ASIMD reverse
// ASIMD table lookup, 1 or 2 table regs
// ASIMD table lookup extension, 1 table reg
// ASIMD transfer, element to gen reg
// ASIMD transpose
// ASIMD unzip/zip
// Covered by "SchedAlias (WriteV[dq]...)" above

// ASIMD duplicate, gen reg
def : InstRW<[N1Write_3c_1M],
             (instregex "^DUP((v16|v8)i8|(v8|v4)i16|(v4|v2)i32|v2i64)gpr$")>;

// ASIMD extract narrow, saturating
def : InstRW<[N1Write_4c_1V1], (instregex "^[SU]QXTNv", "^SQXTUNv")>;

// ASIMD reciprocal and square root estimate, D-form F32 and F64
def : InstRW<[N1Write_3c_1V0], (instrs FRECPEv1i32, FRECPEv2f32, FRECPEv1i64,
                                       FRECPXv1i32, FRECPXv1i64,
                                       URECPEv2i32,
                                       FRSQRTEv1i32, FRSQRTEv2f32, FRSQRTEv1i64,
                                       URSQRTEv2i32)>;

// ASIMD reciprocal and square root estimate, D-form F16 and Q-form F32
def : InstRW<[N1Write_4c_2V0], (instrs FRECPEv1f16, FRECPEv4f16, FRECPEv4f32,
                                       FRECPXv1f16,
                                       URECPEv4i32,
                                       FRSQRTEv1f16, FRSQRTEv4f16, FRSQRTEv4f32,
                                       URSQRTEv4i32)>;

// ASIMD reciprocal and square root estimate, Q-form F16
def : InstRW<[N1Write_6c_4V0], (instrs FRECPEv8f16,
                                       FRSQRTEv8f16)>;

// ASIMD reciprocal step
def : InstRW<[N1Write_4c_1V], (instregex "^FRECPS(16|32|64)$", "^FRECPSv",
                                         "^FRSQRTS(16|32|64)$", "^FRSQRTSv")>;

// ASIMD table lookup, 3 table regs
// ASIMD table lookup extension, 2 table reg
def : InstRW<[N1Write_4c_4V], (instrs TBLv8i8Three, TBLv16i8Three,
                                      TBXv8i8Two, TBXv16i8Two)>;

// ASIMD table lookup, 4 table regs
def : InstRW<[N1Write_4c_3V], (instrs TBLv8i8Four, TBLv16i8Four)>;

// ASIMD table lookup extension, 3 table reg
def : InstRW<[N1Write_6c_3V], (instrs TBXv8i8Three, TBXv16i8Three)>;

// ASIMD table lookup extension, 4 table reg
def : InstRW<[N1Write_6c_5V], (instrs TBXv8i8Four, TBXv16i8Four)>;

// ASIMD transfer, element to gen reg
def : InstRW<[N1Write_2c_1V1], (instregex "^SMOVvi(((8|16)to(32|64))|32to64)$",
                                          "^UMOVvi(8|16|32|64)$")>;

// ASIMD transfer, gen reg to element
def : InstRW<[N1Write_5c_1M_1V], (instregex "^INSvi(8|16|32|64)gpr$")>;


// ASIMD load instructions
// -----------------------------------------------------------------------------

// ASIMD load, 1 element, multiple, 1 reg
def : InstRW<[N1Write_5c_1L],
             (instregex "^LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_5c_1L],
             (instregex "^LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg
def : InstRW<[N1Write_5c_2L],
             (instregex "^LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_5c_2L],
             (instregex "^LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg
def : InstRW<[N1Write_6c_3L],
             (instregex "^LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_6c_3L],
             (instregex "^LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg
def : InstRW<[N1Write_6c_4L],
             (instregex "^LD1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_6c_4L],
             (instregex "^LD1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, one lane
// ASIMD load, 1 element, all lanes
def : InstRW<[N1Write_7c_1L_1V],
             (instregex "LD1(i|Rv)(8|16|32|64)$",
                        "LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_7c_1L_1V],
             (instregex "LD1i(8|16|32|64)_POST$",
                        "LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 2 element, multiple
// ASIMD load, 2 element, one lane
// ASIMD load, 2 element, all lanes
def : InstRW<[N1Write_7c_2L_2V],
             (instregex "LD2Twov(8b|16b|4h|8h|2s|4s|2d)$",
                        "LD2i(8|16|32|64)$",
                        "LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_7c_2L_2V],
             (instregex "LD2Twov(8b|16b|4h|8h|2s|4s|2d)_POST$",
                        "LD2i(8|16|32|64)_POST$",
                        "LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 3 element, multiple
def : InstRW<[N1Write_8c_3L_3V],
             (instregex "LD3Threev(8b|16b|4h|8h|2s|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_8c_3L_3V],
             (instregex "LD3Threev(8b|16b|4h|8h|2s|4s|2d)_POST$")>;

// ASIMD load, 3 element, one lane
// ASIMD load, 3 element, all lanes
def : InstRW<[N1Write_7c_2L_3V],
             (instregex "LD3i(8|16|32|64)$",
                        "LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_7c_2L_3V],
             (instregex "LD3i(8|16|32|64)_POST$",
                        "LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form
def : InstRW<[N1Write_8c_3L_4V],
             (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N1Write_8c_3L_4V],
             (instregex "LD4Fourv(8b|4h|2s)_POST$")>;

// ASIMD load, 4 element, multiple, Q-form
def : InstRW<[N1Write_10c_4L_4V],
             (instregex "LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_10c_4L_4V],
             (instregex "LD4Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, one lane
// ASIMD load, 4 element, all lanes
def : InstRW<[N1Write_8c_4L_4V],
             (instregex "LD4i(8|16|32|64)$",
                        "LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, N1Write_8c_4L_4V],
             (instregex "LD4i(8|16|32|64)_POST$",
                        "LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;


// ASIMD store instructions
// -----------------------------------------------------------------------------

// ASIMD store, 1 element, multiple, 1 reg, D-form
def : InstRW<[N1Write_2c_1L_1V],
             (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N1Write_2c_1L_1V],
             (instregex "ST1Onev(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 1 reg, Q-form
def : InstRW<[N1Write_2c_1L_1V],
             (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_2c_1L_1V],
             (instregex "ST1Onev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form
def : InstRW<[N1Write_2c_1L_2V],
             (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N1Write_2c_1L_2V],
             (instregex "ST1Twov(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, Q-form
def : InstRW<[N1Write_3c_2L_2V],
             (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_3c_2L_2V],
             (instregex "ST1Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form
def : InstRW<[N1Write_3c_2L_3V],           
             (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N1Write_3c_2L_3V],
             (instregex "ST1Threev(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[N1Write_4c_3L_3V],
             (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_4c_3L_3V],
             (instregex "ST1Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form
def : InstRW<[N1Write_3c_2L_2V],
             (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N1Write_3c_2L_2V],
             (instregex "ST1Fourv(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[N1Write_5c_4L_4V],
             (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_5c_4L_4V],
             (instregex "ST1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane
def : InstRW<[N1Write_4c_1L_1V],
             (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N1Write_4c_1L_1V],
             (instregex "ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S
def : InstRW<[N1Write_4c_1L_1V],
             (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N1Write_4c_1L_1V],
             (instregex "ST2Twov(8b|4h|2s)_POST$")>;

// ASIMD store, 2 element, multiple, Q-form
def : InstRW<[N1Write_5c_2L_2V],
             (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_5c_2L_2V],
             (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane
def : InstRW<[N1Write_4c_1L_1V],
             (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N1Write_4c_1L_1V],
             (instregex "ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S
def : InstRW<[N1Write_5c_2L_2V],
             (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N1Write_5c_2L_2V],
             (instregex "ST3Threev(8b|4h|2s)_POST$")>;

// ASIMD store, 3 element, multiple, Q-form
def : InstRW<[N1Write_6c_3L_3V],
             (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N1Write_6c_3L_3V],
             (instregex "ST3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H/S
def : InstRW<[N1Write_4c_3L_3V],
             (instregex "ST3i(8|16|32)$")>;
def : InstRW<[WriteAdr, N1Write_4c_3L_3V],
             (instregex "ST3i(8|16|32)_POST$")>;

// ASIMD store, 3 element, one lane, D
def : InstRW<[N1Write_5c_3L_3V],
             (instrs ST3i64)>;
def : InstRW<[WriteAdr, N1Write_5c_3L_3V],
             (instrs ST3i64_POST)>;

// ASIMD store, 4 element, multiple, D-form, B/H/S
def : InstRW<[N1Write_7c_3L_3V],
             (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N1Write_7c_3L_3V],
             (instregex "ST4Fourv(8b|4h|2s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, B/H/S
def : InstRW<[N1Write_9c_6L_6V],
             (instregex "ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, N1Write_9c_6L_6V],
             (instregex "ST4Fourv(16b|8h|4s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[N1Write_6c_4L_4V],
             (instrs ST4Fourv2d)>;
def : InstRW<[WriteAdr, N1Write_6c_4L_4V],
             (instrs ST4Fourv2d_POST)>;

// ASIMD store, 4 element, one lane, B/H/S
def : InstRW<[N1Write_5c_3L_3V],
             (instregex "ST4i(8|16|32)$")>;
def : InstRW<[WriteAdr, N1Write_5c_3L_3V],
             (instregex "ST4i(8|16|32)_POST$")>;

// ASIMD store, 4 element, one lane, D
def : InstRW<[N1Write_4c_3L_3V],
             (instrs ST4i64)>;
def : InstRW<[WriteAdr, N1Write_4c_3L_3V],
             (instrs ST4i64_POST)>;


// Cryptography extensions
// -----------------------------------------------------------------------------

// Crypto AES ops
def N1WriteVC : WriteSequence<[N1Write_2c_1V0]>;
def N1ReadVC  : SchedReadAdvance<2, [N1WriteVC]>;
def           : InstRW<[N1WriteVC], (instrs AESDrr, AESErr)>;
def           : InstRW<[N1Write_2c_1V0, N1ReadVC], (instrs AESMCrr, AESIMCrr)>;

// Crypto polynomial (64x64) multiply long
// Crypto SHA1 hash acceleration op
// Crypto SHA1 schedule acceleration ops
// Crypto SHA256 schedule acceleration ops
def : InstRW<[N1Write_2c_1V0], (instregex "^PMULLv[12]i64$",
                                          "^SHA1(H|SU0|SU1)rr",
                                          "^SHA256SU[01]rr")>;

// Crypto SHA1 hash acceleration ops
// Crypto SHA256 hash acceleration ops
def : InstRW<[N1Write_4c_1V0], (instregex "^SHA1[CMP]rrr$",
                                          "^SHA256H2?rrr$")>;


// CRC
// -----------------------------------------------------------------------------

// CRC checksum ops
def : InstRW<[N1Write_2c_1M], (instregex "^CRC32C?[BHWX]rr$")>;


}
