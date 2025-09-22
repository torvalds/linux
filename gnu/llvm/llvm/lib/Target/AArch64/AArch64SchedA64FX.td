//=- AArch64SchedA64FX.td - Fujitsu A64FX Scheduling Defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for the Fujitsu A64FX processors.
//
//===----------------------------------------------------------------------===//

def A64FXModel : SchedMachineModel {
  let IssueWidth            =   6; // 6 micro-ops dispatched at a time.
  let MicroOpBufferSize     = 180; // 180 entries in micro-op re-order buffer.
  let LoadLatency           =   5; // Optimistic load latency.
  let MispredictPenalty     =  12; // Extra cycles for mispredicted branch.
  // Determined via a mix of micro-arch details and experimentation.
  let LoopMicroOpBufferSize = 128;
  let PostRAScheduler       =   1; // Using PostRA sched.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SMEUnsupported.F, SVEUnsupported.F,
                                                    [HasMTE, HasMatMulInt8, HasBF16,
                                                    HasPAuth, HasPAuthLR, HasCPA,
                                                    HasCSSC]);
  let FullInstRWOverlapCheck = 0;
}

let SchedModel = A64FXModel in {

// Define the issue ports.

// A64FXIP*

// Port 0
def A64FXIPFLA : ProcResource<1>;

// Port 1
def A64FXIPPR : ProcResource<1>;

// Port 2
def A64FXIPEXA : ProcResource<1>;

// Port 3
def A64FXIPFLB : ProcResource<1>;

// Port 4
def A64FXIPEXB : ProcResource<1>;

// Port 5
def A64FXIPEAGA : ProcResource<1>;

// Port 6
def A64FXIPEAGB : ProcResource<1>;

// Port 7
def A64FXIPBR : ProcResource<1>;

// Define groups for the functional units on each issue port.  Each group
// created will be used by a WriteRes later on.

def A64FXGI7 : ProcResGroup<[A64FXIPBR]>;

def A64FXGI0 : ProcResGroup<[A64FXIPFLA]>;

def A64FXGI1 : ProcResGroup<[A64FXIPPR]>;

def A64FXGI2 : ProcResGroup<[A64FXIPEXA]>;

def A64FXGI3 : ProcResGroup<[A64FXIPFLB]>;

def A64FXGI4 : ProcResGroup<[A64FXIPEXB]>;

def A64FXGI5 : ProcResGroup<[A64FXIPEAGA]>;

def A64FXGI6 : ProcResGroup<[A64FXIPEAGB]>;

def A64FXGI03 : ProcResGroup<[A64FXIPFLA, A64FXIPFLB]>;

def A64FXGI01 : ProcResGroup<[A64FXIPFLA, A64FXIPPR]>;

def A64FXGI24 : ProcResGroup<[A64FXIPEXA, A64FXIPEXB]>;

def A64FXGI56 : ProcResGroup<[A64FXIPEAGA, A64FXIPEAGB]>;

def A64FXGI056 : ProcResGroup<[A64FXIPFLA, A64FXIPEAGA, A64FXIPEAGB]>;

def A64FXGI2456 : ProcResGroup<[A64FXIPEXA, A64FXIPEXB, A64FXIPEAGA, A64FXIPEAGB]>;

def A64FXAny : ProcResGroup<[A64FXIPFLA, A64FXIPPR, A64FXIPEXA, A64FXIPFLB,
                             A64FXIPEXB, A64FXIPEAGA, A64FXIPEAGB, A64FXIPBR]>;

def A64FXWrite_1Cyc_GI7 : SchedWriteRes<[A64FXGI7]> {
  let Latency = 1;
}

def A64FXWrite_2Cyc_GI0 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 2;
}

def A64FXWrite_4Cyc_GI0 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 4;
}

def A64FXWrite_6Cyc_GI0 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 6;
}

def A64FXWrite_8Cyc_GI0 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 8;
}

def A64FXWrite_9Cyc_GI0 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 9;
}

def A64FXWrite_3Cyc_GI1 : SchedWriteRes<[A64FXGI1]> {
  let Latency = 3;
}

def A64FXWrite_5Cyc_GI2 : SchedWriteRes<[A64FXGI2]> {
  let Latency = 5;
}

def A64FXWrite_4Cyc_GI3 : SchedWriteRes<[A64FXGI3]> {
  let Latency = 4;
}

def A64FXWrite_6Cyc_GI3 : SchedWriteRes<[A64FXGI3]> {
  let Latency = 6;
}

def A64FXWrite_4Cyc_GI03 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 4;
}

def A64FXWrite_8Cyc_GI03 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 8;
}

def A64FXWrite_9Cyc_GI03 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 9;
}

def A64FXWrite_10Cyc_GI4 : SchedWriteRes<[A64FXGI4]> {
  let Latency = 10;
}

def A64FXWrite_12Cyc_GI4 : SchedWriteRes<[A64FXGI4]> {
  let Latency = 12;
}

def A64FXWrite_20Cyc_GI4 : SchedWriteRes<[A64FXGI4]> {
  let Latency = 20;
}

def A64FXWrite_5Cyc_GI5 : SchedWriteRes<[A64FXGI5]> {
  let Latency = 5;
}

def A64FXWrite_11Cyc_GI5 : SchedWriteRes<[A64FXGI5]> {
  let Latency = 11;
}

def A64FXWrite_5Cyc_GI6 : SchedWriteRes<[A64FXGI6]> {
  let Latency = 5;
}

def A64FXWrite_1Cyc_GI24 : SchedWriteRes<[A64FXGI24]> {
  let Latency = 1;
}

def A64FXWrite_2Cyc_GI24 : SchedWriteRes<[A64FXGI24]> {
  let Latency = 2;
}

def A64FXWrite_4Cyc_NGI24 : SchedWriteRes<[A64FXGI24]> {
  let Latency = 4;
  let NumMicroOps = 4;
}

def A64FXWrite_1Cyc_GI56 : SchedWriteRes<[A64FXGI56]> {
  let Latency = 1;
}

def A64FXWrite_5Cyc_GI56 : SchedWriteRes<[A64FXGI56]> {
  let Latency = 5;
}

def A64FXWrite_8Cyc_GI56 : SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
}

def A64FXWrite_11Cyc_GI56 : SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
}

def A64FXWrite_LDNP: SchedWriteRes<[A64FXGI56]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def A64FXWrite_LDP01: SchedWriteRes<[A64FXGI2456]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

def A64FXWrite_LDR01: SchedWriteRes<[A64FXGI2456]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def A64FXWrite_LD102: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def A64FXWrite_LD103: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 2;

}

def A64FXWrite_LD104: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

def A64FXWrite_LD105: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 3;
}

def A64FXWrite_LD106: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def A64FXWrite_LD107: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 4;
}

def A64FXWrite_LD108: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def A64FXWrite_LD109: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 2;
}

def A64FXWrite_LD110: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

def A64FXWrite_LD111: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 3;
}

def A64FXWrite_LD112: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def A64FXWrite_LD113: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 4;
}

def A64FXWrite_LD114: SchedWriteRes<[A64FXGI56]> {
  let Latency = 8;
  let NumMicroOps = 5;
}

def A64FXWrite_LD115: SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 5;
}

def A64FXWrite_LD1I0: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def A64FXWrite_LD1I1: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

def A64FXWrite_LD2I0: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def A64FXWrite_LD2I1: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 5;
}

def A64FXWrite_LD3I0: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 6;
}

def A64FXWrite_LD3I1: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 7;
}

def A64FXWrite_LD4I0: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 8;
}

def A64FXWrite_LD4I1: SchedWriteRes<[A64FXGI056]> {
  let Latency = 8;
  let NumMicroOps = 9;
}

def A64FXWrite_1Cyc_GI2456 : SchedWriteRes<[A64FXGI2456]> {
  let Latency = 1;
}

def A64FXWrite_FMOV_GV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
}

def A64FXWrite_FMOV_VG14 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 14;
}

def A64FXWrite_ADDLV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 12;
}

def A64FXWrite_MULLE : SchedWriteRes<[A64FXGI03]> {
  let Latency = 14;
}

def A64FXWrite_MULLV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 14;
}

def A64FXWrite_MADDL : SchedWriteRes<[A64FXGI03]> {
  let Latency = 6;
}

def A64FXWrite_ABA : SchedWriteRes<[A64FXGI03]> {
  let Latency = 8;
}

def A64FXWrite_ABAL : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
}

def A64FXWrite_ADDLV1 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 12;
  let NumMicroOps = 6;
}

def A64FXWrite_MINMAXV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 14;
  let NumMicroOps = 6;
}

def A64FXWrite_SQRDMULH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 9;
}

def A64FXWrite_PMUL : SchedWriteRes<[A64FXGI03]> {
  let Latency = 8;
}


def A64FXWrite_SRSRAV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

def A64FXWrite_SSRAV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def A64FXWrite_RSHRN : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def A64FXWrite_SHRN : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 2;
}


def A64FXWrite_ADDP : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def A64FXWrite_FMULXE : SchedWriteRes<[A64FXGI03]> {
  let Latency = 15;
  let NumMicroOps = 2;
}

def A64FXWrite_FADDPV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 15;
  let NumMicroOps = 3;
}

def A64FXWrite_SADALP : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def A64FXWrite_SADDLP : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 2;
}

def A64FXWrite_FCVTXNV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 15;
  let NumMicroOps = 2;
}

def A64FXWrite_FMAXVVH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 14;
  let NumMicroOps = 7;
}

def A64FXWrite_BIF : SchedWriteRes<[A64FXGI03]> {
  let Latency = 5;
}

def A64FXWrite_DUPGENERAL : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
}

def A64FXWrite_SHA00 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 9;
}

def A64FXWrite_SHA01 : SchedWriteRes<[A64FXGI0]> {
  let Latency = 12;
}

def A64FXWrite_SMOV : SchedWriteRes<[A64FXGI03]> {
  let Latency = 25;
}

def A64FXWrite_TBX1 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def A64FXWrite_TBX2 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 5;
}

def A64FXWrite_TBX3 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 7;
}

def A64FXWrite_TBX4 : SchedWriteRes<[A64FXGI03]> {
  let Latency = 10;
  let NumMicroOps = 9;
}

def A64FXWrite_PREF0: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_PREF1: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_SWP: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_STUR: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_STNP: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_STP01: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_ST10: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_ST11: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_ST12: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_ST13: SchedWriteRes<[A64FXGI56]> {
  let Latency = 0;
}

def A64FXWrite_ST14: SchedWriteRes<[A64FXGI56]> {
  let Latency = 1;
}

def A64FXWrite_ST15: SchedWriteRes<[A64FXGI56]> {
  let Latency = 1;
}

def A64FXWrite_ST16: SchedWriteRes<[A64FXGI56]> {
  let Latency = 1;
}

def A64FXWrite_ST17: SchedWriteRes<[A64FXGI56]> {
  let Latency = 1;
}

def A64FXWrite_CAS: SchedWriteRes<[A64FXGI56]> {
  let Latency = 7;
}

// Define commonly used read types.

// No forwarding is provided for these types.
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

//===----------------------------------------------------------------------===//
// 3. Instruction Tables.

//---
// 3.1 Branch Instructions
//---

// Branch, immed
// Branch and link, immed
// Compare and branch
def : WriteRes<WriteBr,      [A64FXGI7]> {
  let Latency = 1;
}

// Branch, register
// Branch and link, register != LR
// Branch and link, register = LR
def : WriteRes<WriteBrReg,   [A64FXGI7]> {
  let Latency = 1;
}

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteAtomic,  []> {
  let Latency = 4;
}

//---
// Branch
//---
def : InstRW<[A64FXWrite_1Cyc_GI7], (instrs B, BL, BR, BLR)>;
def : InstRW<[A64FXWrite_1Cyc_GI7], (instrs RET)>;
def : InstRW<[A64FXWrite_1Cyc_GI7], (instregex "^B..$")>;
def : InstRW<[A64FXWrite_1Cyc_GI7],
            (instregex "^CBZ", "^CBNZ", "^TBZ", "^TBNZ")>;

//---
// 3.2 Arithmetic and Logical Instructions
// 3.3 Move and Shift Instructions
//---

// ALU, basic
// Conditional compare
// Conditional select
// Address generation
def : WriteRes<WriteI,       [A64FXGI2456]> {
  let Latency = 1;
}

def : InstRW<[WriteI],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

def : InstRW<[WriteI], (instrs COPY)>;

// ALU, extend and/or shift
def : WriteRes<WriteISReg,   [A64FXGI2456]> {
  let Latency = 2;
}

def : InstRW<[WriteISReg],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

def : WriteRes<WriteIEReg,   [A64FXGI2456]> {
  let Latency = 1;
}

def : InstRW<[WriteIEReg],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

// Move immed
def : WriteRes<WriteImm,     [A64FXGI2456]> {
  let Latency = 1;
}

def : InstRW<[A64FXWrite_1Cyc_GI2456],
            (instrs MOVKWi, MOVKXi, MOVNWi, MOVNXi, MOVZWi, MOVZXi)>;

def : InstRW<[A64FXWrite_2Cyc_GI24],
            (instrs ASRVWr, ASRVXr, LSLVWr, LSLVXr, RORVWr, RORVXr)>;

// Variable shift
def : WriteRes<WriteIS,      [A64FXGI2456]> {
  let Latency = 1;
}

//---
// 3.4 Divide and Multiply Instructions
//---

// Divide, W-form
def : WriteRes<WriteID32,    [A64FXGI4]> {
  let Latency = 39;
  let ReleaseAtCycles = [39];
}

// Divide, X-form
def : WriteRes<WriteID64,    [A64FXGI4]> {
  let Latency = 23;
  let ReleaseAtCycles = [23];
}

// Multiply accumulate, W-form
def : WriteRes<WriteIM32,    [A64FXGI2456]> {
  let Latency = 5;
}

// Multiply accumulate, X-form
def : WriteRes<WriteIM64,    [A64FXGI2456]> {
  let Latency = 5;
}

def : InstRW<[WriteIM32], (instrs MADDWrrr, MSUBWrrr)>;
def : InstRW<[WriteIM32], (instrs MADDXrrr, MSUBXrrr)>;
def : InstRW<[A64FXWrite_MADDL],
            (instregex "(S|U)(MADDL|MSUBL)rrr")>;

def : InstRW<[WriteID32], (instrs SDIVWr, UDIVWr)>;
def : InstRW<[WriteID64], (instrs SDIVXr, UDIVXr)>;

// Bitfield extract, two reg
def : WriteRes<WriteExtr,    [A64FXGI2456]> {
  let Latency = 1;
}

// Multiply high
def : InstRW<[A64FXWrite_5Cyc_GI2], (instrs SMULHrr, UMULHrr)>;

// Miscellaneous Data-Processing Instructions
// Bitfield extract
def : InstRW<[A64FXWrite_2Cyc_GI24], (instrs EXTRWrri, EXTRXrri)>;

// Bitifield move - basic
def : InstRW<[A64FXWrite_1Cyc_GI24],
            (instrs SBFMWri, SBFMXri, UBFMWri, UBFMXri)>;

// Bitfield move, insert
def : InstRW<[A64FXWrite_4Cyc_NGI24], (instregex "^BFM")>;
def : InstRW<[A64FXWrite_1Cyc_GI24], (instregex "(S|U)?BFM.*")>;

// Count leading
def : InstRW<[A64FXWrite_2Cyc_GI0], (instregex "^CLS(W|X)r$",
                                               "^CLZ(W|X)r$")>;

// Reverse bits
def : InstRW<[A64FXWrite_4Cyc_GI03], (instrs RBITWr, RBITXr)>;

// Cryptography Extensions
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^AES[DE]")>;
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^AESI?MC")>;
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^PMULL")>;
def : InstRW<[A64FXWrite_SHA00], (instregex "^SHA1SU0")>;
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^SHA1(H|SU1)")>;
def : InstRW<[A64FXWrite_SHA01], (instregex "^SHA1[CMP]")>;
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^SHA256SU0")>;
def : InstRW<[A64FXWrite_8Cyc_GI0], (instregex "^SHA256SU1")>;
def : InstRW<[A64FXWrite_SHA01], (instregex "^SHA256(H|H2)")>;

// CRC Instructions
def : InstRW<[A64FXWrite_10Cyc_GI4], (instrs CRC32Brr, CRC32Hrr)>;
def : InstRW<[A64FXWrite_12Cyc_GI4], (instrs CRC32Wrr)>;
def : InstRW<[A64FXWrite_20Cyc_GI4], (instrs CRC32Xrr)>;

def : InstRW<[A64FXWrite_10Cyc_GI4], (instrs CRC32CBrr, CRC32CHrr)>;
def : InstRW<[A64FXWrite_12Cyc_GI4], (instrs CRC32CWrr)>;
def : InstRW<[A64FXWrite_20Cyc_GI4], (instrs CRC32CXrr)>;

// Reverse bits/bytes
// NOTE: Handled by WriteI.

//---
// 3.6 Load Instructions
// 3.10 FP Load Instructions
//---

// Load register, literal
// Load register, unscaled immed
// Load register, immed unprivileged
// Load register, unsigned immed
def : WriteRes<WriteLD,      [A64FXGI56]> {
  let Latency = 4;
}

// Load register, immed post-index
// NOTE: Handled by WriteLD, WriteI.
// Load register, immed pre-index
// NOTE: Handled by WriteLD, WriteAdr.
def : WriteRes<WriteAdr,     [A64FXGI2456]> {
  let Latency = 1;
}

// Load pair, immed offset, normal
// Load pair, immed offset, signed words, base != SP
// Load pair, immed offset signed words, base = SP
// LDP only breaks into *one* LS micro-op.  Thus
// the resources are handled by WriteLD.
def : WriteRes<WriteLDHi,    []> {
  let Latency = 5;
}

// Load register offset, basic
// Load register, register offset, scale by 4/8
// Load register, register offset, scale by 2
// Load register offset, extend
// Load register, register offset, extend, scale by 4/8
// Load register, register offset, extend, scale by 2
def A64FXWriteLDIdx : SchedWriteVariant<[
  SchedVar<ScaledIdxPred, [A64FXWrite_1Cyc_GI56]>,
  SchedVar<NoSchedPred,   [A64FXWrite_1Cyc_GI56]>]>;
def : SchedAlias<WriteLDIdx, A64FXWriteLDIdx>;

def A64FXReadAdrBase : SchedReadVariant<[
  SchedVar<ScaledIdxPred, [ReadDefault]>,
  SchedVar<NoSchedPred,   [ReadDefault]>]>;
def : SchedAlias<ReadAdrBase, A64FXReadAdrBase>;

// Load pair, immed pre-index, normal
// Load pair, immed pre-index, signed words
// Load pair, immed post-index, normal
// Load pair, immed post-index, signed words
// NOTE: Handled by WriteLD, WriteLDHi, WriteAdr.

def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDNPDi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDNPQi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDNPSi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDNPWi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDNPXi)>;

def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPDi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPQi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPSi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPSWi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPWi)>;
def : InstRW<[A64FXWrite_LDNP, WriteLDHi], (instrs LDPXi)>;

def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDRBui)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDRDui)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDRHui)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDRQui)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDRSui)>;

def : InstRW<[A64FXWrite_5Cyc_GI6], (instrs LDRDl)>;
def : InstRW<[A64FXWrite_5Cyc_GI6], (instrs LDRQl)>;
def : InstRW<[A64FXWrite_5Cyc_GI6], (instrs LDRWl)>;
def : InstRW<[A64FXWrite_5Cyc_GI6], (instrs LDRXl)>;

def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRBi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRHi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRWi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRXi)>;

def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRSBWi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRSBXi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRSHWi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRSHXi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDTRSWi)>;

def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPDpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPQpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPSpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRBpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRDpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRHpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRQpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRWpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRXpre)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSBWpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSBXpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSBWpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSBXpost)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSHWpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSHXpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSHWpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSHXpost)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRBBpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRBBpost)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRHHpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRHHpost)>;

def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPDpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPQpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPSpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPWpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPXpost)>;

def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRBpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRDpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRHpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRQpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRSpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRWpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRXpost)>;

def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPDpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPQpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPSpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPXpre)>;

def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRBpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRDpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRHpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRQpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRSpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRWpre)>;
def : InstRW<[A64FXWrite_LDR01, WriteAdr], (instrs LDRXpre)>;

def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPDpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPQpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPSpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPWpost)>;
def : InstRW<[A64FXWrite_LDP01, WriteLDHi, WriteAdr],
            (instrs LDPXpost)>;

def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRBpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRDpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRHpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRQpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRSpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRWpost)>;
def : InstRW<[A64FXWrite_LDR01, WriteI], (instrs LDRXpost)>;

def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRBroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRDroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRHroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRHHroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRQroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSHWroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSHXroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRWroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRXroW)>;

def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRBroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRDroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRHHroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRHroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSHWroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRSHXroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRWroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase], (instrs LDRXroX)>;

def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRBroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRBroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
             (instrs LDRDroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRHroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRHHroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRQroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSHWroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSHXroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRWroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRXroW)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRBroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRDroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRHroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRHHroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRQroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSHWroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRSHXroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRWroX)>;
def : InstRW<[A64FXWrite_5Cyc_GI56, ReadAdrBase],
            (instrs LDRXroX)>;

def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURBi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURBBi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURDi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURHi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURHHi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURQi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURXi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSBWi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSBXi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSHWi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSHXi)>;
def : InstRW<[A64FXWrite_5Cyc_GI56], (instrs LDURSWi)>;

//---
// Prefetch
//---
def : InstRW<[A64FXWrite_PREF0], (instrs PRFMl)>;
def : InstRW<[A64FXWrite_PREF1], (instrs PRFUMi)>;
def : InstRW<[A64FXWrite_PREF1], (instrs PRFMui)>;
def : InstRW<[A64FXWrite_PREF1], (instrs PRFMroW)>;
def : InstRW<[A64FXWrite_PREF1], (instrs PRFMroX)>;

//--
// 3.7 Store Instructions
// 3.11 FP Store Instructions
//--

// Store register, unscaled immed
// Store register, immed unprivileged
// Store register, unsigned immed
def : WriteRes<WriteST,      [A64FXGI56]> {
  let Latency = 1;
}

// Store register, immed post-index
// NOTE: Handled by WriteAdr, WriteST, ReadAdrBase

// Store register, immed pre-index
// NOTE: Handled by WriteAdr, WriteST

// Store register, register offset, basic
// Store register, register offset, scaled by 4/8
// Store register, register offset, scaled by 2
// Store register, register offset, extend
// Store register, register offset, extend, scale by 4/8
// Store register, register offset, extend, scale by 1
def : WriteRes<WriteSTIdx, [A64FXGI56, A64FXGI2456]> {
  let Latency = 1;
}

// Store pair, immed offset, W-form
// Store pair, immed offset, X-form
def : WriteRes<WriteSTP,     [A64FXGI56]> {
  let Latency = 1;
}

// Store pair, immed post-index, W-form
// Store pair, immed post-index, X-form
// Store pair, immed pre-index, W-form
// Store pair, immed pre-index, X-form
// NOTE: Handled by WriteAdr, WriteSTP.

def : InstRW<[A64FXWrite_STUR], (instrs STURBi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURBBi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURDi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURHi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURHHi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURQi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURSi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURWi)>;
def : InstRW<[A64FXWrite_STUR], (instrs STURXi)>;

def : InstRW<[WriteAdr, A64FXWrite_STUR], (instrs STTRBi)>;
def : InstRW<[WriteAdr, A64FXWrite_STUR], (instrs STTRHi)>;
def : InstRW<[WriteAdr, A64FXWrite_STUR], (instrs STTRWi)>;
def : InstRW<[WriteAdr, A64FXWrite_STUR], (instrs STTRXi)>;

def : InstRW<[A64FXWrite_STNP], (instrs STNPDi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STNPQi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STNPXi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STNPWi)>;

def : InstRW<[A64FXWrite_STNP], (instrs STPDi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STPQi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STPXi)>;
def : InstRW<[A64FXWrite_STNP], (instrs STPWi)>;

def : InstRW<[A64FXWrite_STUR], (instrs STRBui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRBui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRDui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRDui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRHui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRHui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRQui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRQui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRXui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRXui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRWui)>;
def : InstRW<[A64FXWrite_STUR], (instrs STRWui)>;

def : InstRW<[A64FXWrite_STP01],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[A64FXWrite_STP01],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[A64FXWrite_STP01, ReadAdrBase],
            (instrs STPXpre, STPXpost)>;

def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, A64FXWrite_STP01, ReadAdrBase],
            (instrs STRXpre, STRXpost)>;

def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRBBroW, STRBBroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRBBroW, STRBBroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRXroW, STRXroX)>;
def : InstRW<[A64FXWrite_STUR, ReadAdrBase],
            (instrs STRXroW, STRXroX)>;

//---
// 3.8 FP Data Processing Instructions
//---

// FP absolute value
// FP min/max
// FP negate
def : WriteRes<WriteF,       [A64FXGI03]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
}

// FP arithmetic

def : InstRW<[A64FXWrite_4Cyc_GI03], (instrs FADDDrr, FADDHrr)>;
def : InstRW<[A64FXWrite_4Cyc_GI03], (instrs FSUBDrr, FSUBHrr)>;

// FP compare
def : WriteRes<WriteFCmp,    [A64FXGI03]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
}

// FP Div, Sqrt
def : WriteRes<WriteFDiv, [A64FXGI0]> {
  let Latency = 43;
}

def A64FXXWriteFDiv : SchedWriteRes<[A64FXGI0]> {
  let Latency = 38;
}

def A64FXXWriteFDivSP : SchedWriteRes<[A64FXGI0]> {
  let Latency = 29;
}

def A64FXXWriteFDivDP : SchedWriteRes<[A64FXGI0]> {
  let Latency = 43;
}

def A64FXXWriteFSqrtSP : SchedWriteRes<[A64FXGI0]> {
  let Latency = 29;
}

def A64FXXWriteFSqrtDP : SchedWriteRes<[A64FXGI0]> {
  let Latency = 43;
}

// FP divide, S-form
// FP square root, S-form
def : InstRW<[A64FXXWriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[A64FXXWriteFSqrtSP], (instrs FSQRTSr)>;
def : InstRW<[A64FXXWriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[A64FXXWriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[A64FXXWriteFDivSP], (instregex "^FDIVSrr")>;
def : InstRW<[A64FXXWriteFSqrtSP], (instregex "^FSQRTSr")>;

// FP divide, D-form
// FP square root, D-form
def : InstRW<[A64FXXWriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[A64FXXWriteFSqrtDP], (instrs FSQRTDr)>;
def : InstRW<[A64FXXWriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[A64FXXWriteFSqrtDP], (instregex "^.*SQRT.*64$")>;
def : InstRW<[A64FXXWriteFDivDP], (instregex "^FDIVDrr")>;
def : InstRW<[A64FXXWriteFSqrtDP], (instregex "^FSQRTDr")>;

// FP round to integral
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^FRINT(A|I|M|N|P|X|Z)(Sr|Dr)")>;

// FP select
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^FCSEL")>;

//---
// 3.9 FP Miscellaneous Instructions
//---

// FP convert, from vec to vec reg
// FP convert, from gen to vec reg
// FP convert, from vec to gen reg
def : WriteRes<WriteFCvt, [A64FXGI03]> {
  let Latency = 9;
  let ReleaseAtCycles = [2];
}

// FP move, immed
// FP move, register
def : WriteRes<WriteFImm, [A64FXGI0]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
}

// FP transfer, from gen to vec reg
// FP transfer, from vec to gen reg
def : WriteRes<WriteFCopy, [A64FXGI0]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
}

def : InstRW<[A64FXWrite_FMOV_GV], (instrs FMOVXDHighr)>;
def : InstRW<[A64FXWrite_FMOV_VG14], (instrs FMOVDXHighr)>;

//---
// 3.12 ASIMD Integer Instructions
//---

// ASIMD absolute diff, D-form
// ASIMD absolute diff, Q-form
// ASIMD absolute diff accum, D-form
// ASIMD absolute diff accum, Q-form
// ASIMD absolute diff accum long
// ASIMD absolute diff long
// ASIMD arith, basic
// ASIMD arith, complex
// ASIMD compare
// ASIMD logical (AND, BIC, EOR)
// ASIMD max/min, basic
// ASIMD max/min, reduce, 4H/4S
// ASIMD max/min, reduce, 8B/8H
// ASIMD max/min, reduce, 16B
// ASIMD multiply, D-form
// ASIMD multiply, Q-form
// ASIMD multiply accumulate long
// ASIMD multiply accumulate saturating long
// ASIMD multiply long
// ASIMD pairwise add and accumulate
// ASIMD shift accumulate
// ASIMD shift by immed, basic
// ASIMD shift by immed and insert, basic, D-form
// ASIMD shift by immed and insert, basic, Q-form
// ASIMD shift by immed, complex
// ASIMD shift by register, basic, D-form
// ASIMD shift by register, basic, Q-form
// ASIMD shift by register, complex, D-form
// ASIMD shift by register, complex, Q-form
def : WriteRes<WriteVd, [A64FXGI03]> {
  let Latency = 4;
}
def : WriteRes<WriteVq, [A64FXGI03]> {
  let Latency = 4;
}

// ASIMD arith, reduce, 4H/4S
// ASIMD arith, reduce, 8B/8H
// ASIMD arith, reduce, 16B

// ASIMD logical (MVN (alias for NOT), ORN, ORR)
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^ANDv", "^BICv", "^EORv", "^ORRv", "^ORNv", "^NOTv")>;

// ASIMD arith, reduce
def : InstRW<[A64FXWrite_ADDLV],
            (instregex "^ADDVv", "^SADDLVv", "^UADDLVv")>;

// ASIMD polynomial (8x8) multiply long
def : InstRW<[A64FXWrite_MULLE], (instregex "^(S|U|SQD)MULL")>;
def : InstRW<[A64FXWrite_MULLV],
            (instregex "(S|U|SQD)(MLAL|MLSL|MULL)v.*")>;
def : InstRW<[A64FXWrite_8Cyc_GI03], (instregex "^PMULL(v8i8|v16i8)")>;
def : InstRW<[A64FXWrite_8Cyc_GI03], (instregex "^PMULL(v1i64|v2i64)")>;

// ASIMD absolute diff accum, D-form
def : InstRW<[A64FXWrite_ABA],
            (instregex "^[SU]ABA(v8i8|v4i16|v2i32)$")>;
// ASIMD absolute diff accum, Q-form
def : InstRW<[A64FXWrite_ABA],
            (instregex "^[SU]ABA(v16i8|v8i16|v4i32)$")>;
// ASIMD absolute diff accum long
def : InstRW<[A64FXWrite_ABAL],
            (instregex "^[SU]ABAL")>;
// ASIMD arith, reduce, 4H/4S
def : InstRW<[A64FXWrite_ADDLV1],
            (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v$")>;
// ASIMD arith, reduce, 8B
def : InstRW<[A64FXWrite_ADDLV1],
            (instregex "^[SU]?ADDL?V(v8i16|v4i32)v$")>;
// ASIMD arith, reduce, 16B/16H
def : InstRW<[A64FXWrite_ADDLV1],
            (instregex "^[SU]?ADDL?Vv16i8v$")>;
// ASIMD max/min, reduce, 4H/4S
def : InstRW<[A64FXWrite_MINMAXV],
            (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v$")>;
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[A64FXWrite_MINMAXV],
            (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v$")>;
// ASIMD max/min, reduce, 16B/16H
def : InstRW<[A64FXWrite_MINMAXV],
            (instregex "^[SU](MIN|MAX)Vv16i8v$")>;
// ASIMD multiply, D-form
def : InstRW<[A64FXWrite_PMUL],
            (instregex "^(P?MUL|SQR?DMUL)" #
                       "(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)" #
                       "(_indexed)?$")>;

// ASIMD multiply, Q-form
def : InstRW<[A64FXWrite_PMUL],
            (instregex "^(P?MUL)(v16i8|v8i16|v4i32)(_indexed)?$")>;

// ASIMD multiply, Q-form
def : InstRW<[A64FXWrite_SQRDMULH],
            (instregex "^(SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?$")>;

// ASIMD multiply accumulate, D-form
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^ML[AS](v8i8|v4i16|v2i32)(_indexed)?$")>;
// ASIMD multiply accumulate, Q-form
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^ML[AS](v16i8|v8i16|v4i32)(_indexed)?$")>;
// ASIMD shift accumulate
def : InstRW<[A64FXWrite_SRSRAV],
            (instregex "SRSRAv", "URSRAv")>;
def : InstRW<[A64FXWrite_SSRAV],
            (instregex "SSRAv", "USRAv")>;

// ASIMD shift by immed, basic
def : InstRW<[A64FXWrite_RSHRN],
            (instregex "RSHRNv", "SQRSHRNv", "SQRSHRUNv", "UQRSHRNv")>;
def : InstRW<[A64FXWrite_SHRN],
            (instregex "SHRNv", "SQSHRNv", "SQSHRUNv", "UQSHRNv")>;

def : InstRW<[A64FXWrite_6Cyc_GI3],
            (instregex "SQXTNv", "SQXTUNv", "UQXTNv")>;

// ASIMD shift by immed, complex
def : InstRW<[A64FXWrite_ABA], (instregex "^[SU]?(Q|R){1,2}SHR")>;
def : InstRW<[A64FXWrite_6Cyc_GI3], (instregex "^SQSHLU")>;
// ASIMD shift by register, basic, Q-form
def : InstRW<[A64FXWrite_6Cyc_GI3],
            (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// ASIMD shift by register, complex, D-form
def : InstRW<[A64FXWrite_6Cyc_GI3],
            (instregex "^[SU][QR]{1,2}SHL" #
                       "(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32|b|d|h|s)")>;
// ASIMD shift by register, complex, Q-form
def : InstRW<[A64FXWrite_6Cyc_GI3],
            (instregex "^[SU][QR]{1,2}SHL(v16i8|v8i16|v4i32|v2i64)")>;

// ASIMD Arithmetic
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "(ADD|SUB)(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "(ADD|SUB)(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[A64FXWrite_SHRN], (instregex "(ADD|SUB)HNv.*")>;
def : InstRW<[A64FXWrite_RSHRN], (instregex "(RADD|RSUB)HNv.*")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^SQADD", "^SQNEG", "^SQSUB", "^SRHADD",
                       "^SUQADD", "^UQADD", "^UQSUB", "^URHADD", "^USQADD")>;
def : InstRW<[A64FXWrite_ADDP],
            (instregex "ADDP(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "((AND|ORN|EOR|EON)S?(Xr[rsi]|v16i8|v8i16|v4i32)|" #
                       "(ORR|BIC)S?(Xr[rs]|v16i8|v8i16|v4i32))")>;
def : InstRW<[A64FXWrite_4Cyc_GI0],
            (instregex "(CLS|CLZ|CNT)(v4i32|v8i16|v16i8)")>;
def : InstRW<[A64FXWrite_SADALP], (instregex "^SADALP", "^UADALP")>;
def : InstRW<[A64FXWrite_SADDLP], (instregex "^SADDLPv", "^UADDLPv")>;
def : InstRW<[A64FXWrite_ADDLV1], (instregex "^SADDLV", "^UADDLV")>;
def : InstRW<[A64FXWrite_MINMAXV],
             (instregex "^ADDVv", "^SMAXVv", "^UMAXVv", "^SMINVv", "^UMINVv")>;
def : InstRW<[A64FXWrite_ABA],
             (instregex "^SABAv", "^UABAv", "^SABALv", "^UABALv")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^SQADDv", "^SQSUBv", "^UQADDv", "^UQSUBv")>;
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^SUQADDv", "^USQADDv")>;
def : InstRW<[A64FXWrite_SHRN],
            (instregex "^ADDHNv", "^SUBHNv")>;
def : InstRW<[A64FXWrite_RSHRN],
            (instregex "^RADDHNv", "^RSUBHNv")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^SQABS", "^SQADD", "^SQNEG", "^SQSUB",
                       "^SRHADD", "^SUQADD", "^UQADD", "^UQSUB",
                      "^URHADD", "^USQADD")>;

def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^CMEQv", "^CMGEv", "^CMGTv",
                       "^CMLEv", "^CMLTv", "^CMHIv", "^CMHSv")>;
def : InstRW<[A64FXWrite_MINMAXV],
            (instregex "^SMAXv", "^SMINv", "^UMAXv", "^UMINv")>;
def : InstRW<[A64FXWrite_ADDP],
            (instregex "^SMAXPv", "^SMINPv", "^UMAXPv", "^UMINPv")>;
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^SABDv", "^UABDv")>;
def : InstRW<[A64FXWrite_TBX1],
            (instregex "^SABDLv", "^UABDLv")>;

//---
// 3.13 ASIMD Floating-point Instructions
//---

def : WriteRes<WriteFMul, [A64FXGI03]> {
  let Latency = 9;
}

// ASIMD FP absolute value
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^FABSv")>;

// ASIMD FP arith, normal, D-form
// ASIMD FP arith, normal, Q-form
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^FABDv", "^FADDv", "^FSUBv")>;

// ASIMD FP arith, pairwise, D-form
// ASIMD FP arith, pairwise, Q-form
def : InstRW<[A64FXWrite_FADDPV], (instregex "^FADDPv")>;

// ASIMD FP compare, D-form
// ASIMD FP compare, Q-form
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^FACGEv", "^FACGTv")>;
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^FCMEQv", "^FCMGEv",
                                                 "^FCMGTv", "^FCMLEv",
                                                 "^FCMLTv")>;
// ASIMD FP round, D-form
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^FRINT[AIMNPXZ](v2f32)")>;
// ASIMD FP round, Q-form
def : InstRW<[A64FXWrite_9Cyc_GI03],
            (instregex "^FRINT[AIMNPXZ](v4f32|v2f64)")>;

// ASIMD FP convert, long
// ASIMD FP convert, narrow
// ASIMD FP convert, other, D-form
// ASIMD FP convert, other, Q-form

// ASIMD FP convert, long and narrow
def : InstRW<[A64FXWrite_FCVTXNV], (instregex "^FCVT(L|N|XN)v")>;
// ASIMD FP convert, other, D-form
def : InstRW<[A64FXWrite_FCVTXNV],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v2f32|v1i32|v2i32|v1i64)")>;
// ASIMD FP convert, other, Q-form
def : InstRW<[A64FXWrite_FCVTXNV],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP divide, D-form, F32
def : InstRW<[A64FXXWriteFDivSP], (instrs FDIVv2f32)>;
def : InstRW<[A64FXXWriteFDivSP], (instregex "FDIVv2f32")>;

// ASIMD FP divide, Q-form, F32
def : InstRW<[A64FXXWriteFDiv], (instrs FDIVv4f32)>;
def : InstRW<[A64FXXWriteFDiv], (instregex "FDIVv4f32")>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[A64FXXWriteFDivDP], (instrs FDIVv2f64)>;
def : InstRW<[A64FXXWriteFDivDP], (instregex "FDIVv2f64")>;

// ASIMD FP max/min, normal, D-form
// ASIMD FP max/min, normal, Q-form
def : InstRW<[A64FXWrite_4Cyc_GI0], (instregex "^FMAXv", "^FMAXNMv",
                                               "^FMINv", "^FMINNMv")>;

// ASIMD FP max/min, pairwise, D-form
// ASIMD FP max/min, pairwise, Q-form
def : InstRW<[A64FXWrite_ADDP], (instregex "^FMAXPv", "^FMAXNMPv",
                                           "^FMINPv", "^FMINNMPv")>;

// ASIMD FP max/min, reduce
def : InstRW<[A64FXWrite_FMAXVVH], (instregex "^FMAXVv", "^FMAXNMVv",
                                              "^FMINVv", "^FMINNMVv")>;

// ASIMD FP multiply, D-form, FZ
// ASIMD FP multiply, D-form, no FZ
// ASIMD FP multiply, Q-form, FZ
// ASIMD FP multiply, Q-form, no FZ
def : InstRW<[A64FXWrite_9Cyc_GI03], (instregex "^FMULv", "^FMULXv")>;
def : InstRW<[A64FXWrite_FMULXE],
            (instregex "^FMULX?(v2f32|v1i32|v2i32|v1i64|32|64)")>;
def : InstRW<[A64FXWrite_FMULXE],
            (instregex "^FMULX?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP multiply accumulate, Dform, FZ
// ASIMD FP multiply accumulate, Dform, no FZ
// ASIMD FP multiply accumulate, Qform, FZ
// ASIMD FP multiply accumulate, Qform, no FZ
def : InstRW<[A64FXWrite_9Cyc_GI03], (instregex "^FMLAv", "^FMLSv")>;
def : InstRW<[A64FXWrite_FMULXE],
            (instregex "^FML[AS](v2f32|v1i32|v2i32|v1i64)")>;
def : InstRW<[A64FXWrite_FMULXE],
            (instregex "^FML[AS](v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP negate
def : InstRW<[A64FXWrite_4Cyc_GI03], (instregex "^FNEGv")>;

//--
// 3.14 ASIMD Miscellaneous Instructions
//--

// ASIMD bit reverse
def : InstRW<[A64FXWrite_1Cyc_GI2456], (instregex "^RBITv")>;

// ASIMD bitwise insert, D-form
// ASIMD bitwise insert, Q-form
def : InstRW<[A64FXWrite_BIF],
            (instregex "^BIFv", "^BITv", "^BSLv")>;

// ASIMD count, D-form
// ASIMD count, Q-form
def : InstRW<[A64FXWrite_4Cyc_GI0],
            (instregex "^CLSv", "^CLZv", "^CNTv")>;

// ASIMD duplicate, gen reg
// ASIMD duplicate, element
def : InstRW<[A64FXWrite_DUPGENERAL], (instregex "^DUPv")>;
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^DUP(i8|i16|i32|i64)$")>;
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^DUPv.+gpr")>;

// ASIMD extract
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^EXTv")>;

// ASIMD extract narrow
def : InstRW<[A64FXWrite_6Cyc_GI3], (instregex "^XTNv")>;

// ASIMD extract narrow, saturating
def : InstRW<[A64FXWrite_6Cyc_GI3],
            (instregex "^SQXTNv", "^SQXTUNv", "^UQXTNv")>;

// ASIMD insert, element to element
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^INSv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[A64FXWrite_SMOV], (instregex "^[SU]MOVv")>;

// ASIMD move, integer immed
def : InstRW<[A64FXWrite_4Cyc_GI0], (instregex "^MOVIv")>;

// ASIMD move, FP immed
def : InstRW<[A64FXWrite_4Cyc_GI0], (instregex "^FMOVv")>;

// ASIMD table lookup, D-form
def : InstRW<[A64FXWrite_6Cyc_GI3], (instregex "^TBLv8i8One")>;
def : InstRW<[A64FXWrite_TBX1], (instregex "^TBLv8i8Two")>;
def : InstRW<[A64FXWrite_TBX2], (instregex "^TBLv8i8Three")>;
def : InstRW<[A64FXWrite_TBX3], (instregex "^TBLv8i8Four")>;
def : InstRW<[A64FXWrite_TBX1], (instregex "^TBXv8i8One")>;
def : InstRW<[A64FXWrite_TBX2], (instregex "^TBXv8i8Two")>;
def : InstRW<[A64FXWrite_TBX3], (instregex "^TBXv8i8Three")>;
def : InstRW<[A64FXWrite_TBX4], (instregex "^TBXv8i8Four")>;

// ASIMD table lookup, Q-form
def : InstRW<[A64FXWrite_6Cyc_GI3], (instregex "^TBLv16i8One")>;
def : InstRW<[A64FXWrite_TBX1], (instregex "^TBLv16i8Two")>;
def : InstRW<[A64FXWrite_TBX2], (instregex "^TBLv16i8Three")>;
def : InstRW<[A64FXWrite_TBX3], (instregex "^TBLv16i8Four")>;
def : InstRW<[A64FXWrite_TBX1], (instregex "^TBXv16i8One")>;
def : InstRW<[A64FXWrite_TBX2], (instregex "^TBXv16i8Two")>;
def : InstRW<[A64FXWrite_TBX3], (instregex "^TBXv16i8Three")>;
def : InstRW<[A64FXWrite_TBX4], (instregex "^TBXv16i8Four")>;

// ASIMD unzip/zip
def : InstRW<[A64FXWrite_6Cyc_GI0],
            (instregex "^UZP1", "^UZP2", "^ZIP1", "^ZIP2")>;

// ASIMD reciprocal estimate, D-form
// ASIMD reciprocal estimate, Q-form
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^FRECPEv", "^FRECPXv", "^URECPEv",
                       "^FRSQRTEv", "^URSQRTEv")>;

// ASIMD reciprocal step, D-form, FZ
// ASIMD reciprocal step, D-form, no FZ
// ASIMD reciprocal step, Q-form, FZ
// ASIMD reciprocal step, Q-form, no FZ
def : InstRW<[A64FXWrite_9Cyc_GI0], (instregex "^FRECPSv", "^FRSQRTSv")>;

// ASIMD reverse
def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^REV16v", "^REV32v", "^REV64v")>;

// ASIMD table lookup, D-form
// ASIMD table lookup, Q-form
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^TBLv", "^TBXv")>;

// ASIMD transfer, element to word or word
def : InstRW<[A64FXWrite_SMOV], (instregex "^[SU]MOVv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[A64FXWrite_SMOV], (instregex "(S|U)MOVv.*")>;

// ASIMD transfer gen reg to element
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^INSv")>;

// ASIMD transpose
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^TRN1v", "^TRN2v",
                                                 "^UZP1v", "^UZP2v")>;

// ASIMD unzip/zip
def : InstRW<[A64FXWrite_6Cyc_GI0], (instregex "^ZIP1v", "^ZIP2v")>;

//--
// 3.15 ASIMD Load Instructions
//--

// ASIMD load, 1 element, multiple, 1 reg, D-form
// ASIMD load, 1 element, multiple, 1 reg, Q-form
def : InstRW<[A64FXWrite_8Cyc_GI56],
            (instregex "^LD1Onev(8b|4h|2s|1d|2d)$")>;
def : InstRW<[A64FXWrite_11Cyc_GI56],
            (instregex "^LD1Onev(16b|8h|4s)$")>;
def : InstRW<[A64FXWrite_LD108, WriteAdr],
            (instregex "^LD1Onev(8b|4h|2s|1d|2d)_POST$")>;
def : InstRW<[A64FXWrite_LD109, WriteAdr],
            (instregex "^LD1Onev(16b|8h|4s)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, D-form
// ASIMD load, 1 element, multiple, 2 reg, Q-form
def : InstRW<[A64FXWrite_LD102],
            (instregex "^LD1Twov(8b|4h|2s|1d|2d)$")>;
def : InstRW<[A64FXWrite_LD103],
            (instregex "^LD1Twov(16b|8h|4s)$")>;
def : InstRW<[A64FXWrite_LD110, WriteAdr],
            (instregex "^LD1Twov(8b|4h|2s|1d|2d)_POST$")>;
def : InstRW<[A64FXWrite_LD111, WriteAdr],
            (instregex "^LD1Twov(16b|8h|4s)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, D-form
// ASIMD load, 1 element, multiple, 3 reg, Q-form
def : InstRW<[A64FXWrite_LD104],
            (instregex "^LD1Threev(8b|4h|2s|1d|2d)$")>;
def : InstRW<[A64FXWrite_LD105],
            (instregex "^LD1Threev(16b|8h|4s)$")>;
def : InstRW<[A64FXWrite_LD112, WriteAdr],
            (instregex "^LD1Threev(8b|4h|2s|1d|2d)_POST$")>;
def : InstRW<[A64FXWrite_LD113, WriteAdr],
            (instregex "^LD1Threev(16b|8h|4s)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form
// ASIMD load, 1 element, multiple, 4 reg, Q-form
def : InstRW<[A64FXWrite_LD106],
            (instregex "^LD1Fourv(8b|4h|2s|1d|2d)$")>;
def : InstRW<[A64FXWrite_LD107],
            (instregex "^LD1Fourv(16b|8h|4s)$")>;
def : InstRW<[A64FXWrite_LD114, WriteAdr],
            (instregex "^LD1Fourv(8b|4h|2s|1d|2d)_POST$")>;
def : InstRW<[A64FXWrite_LD115, WriteAdr],
            (instregex "^LD1Fourv(16b|8h|4s)_POST$")>;

// ASIMD load, 1 element, one lane, B/H/S
// ASIMD load, 1 element, one lane, D
def : InstRW<[A64FXWrite_LD1I0], (instregex "^LD1i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_LD1I1, WriteAdr],
            (instregex "^LD1i(8|16|32|64)_POST$")>;

// ASIMD load, 1 element, all lanes, D-form, B/H/S
// ASIMD load, 1 element, all lanes, D-form, D
// ASIMD load, 1 element, all lanes, Q-form
def : InstRW<[A64FXWrite_8Cyc_GI03],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD108, WriteAdr],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form, B/H/S
// ASIMD load, 2 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_LD103],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD111, WriteAdr],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, one lane, B/H
// ASIMD load, 2 element, one lane, S
// ASIMD load, 2 element, one lane, D
def : InstRW<[A64FXWrite_LD2I0], (instregex "^LD2i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_LD2I1, WriteAdr],
            (instregex "^LD2i(8|16|32|64)_POST$")>;

// ASIMD load, 2 element, all lanes, D-form, B/H/S
// ASIMD load, 2 element, all lanes, D-form, D
// ASIMD load, 2 element, all lanes, Q-form
def : InstRW<[A64FXWrite_LD102],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD110, WriteAdr],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, multiple, D-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_LD105],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD113, WriteAdr],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, one lone, B/H
// ASIMD load, 3 element, one lane, S
// ASIMD load, 3 element, one lane, D
def : InstRW<[A64FXWrite_LD3I0], (instregex "^LD3i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_LD3I1, WriteAdr],
            (instregex "^LD3i(8|16|32|64)_POST$")>;

// ASIMD load, 3 element, all lanes, D-form, B/H/S
// ASIMD load, 3 element, all lanes, D-form, D
// ASIMD load, 3 element, all lanes, Q-form, B/H/S
// ASIMD load, 3 element, all lanes, Q-form, D
def : InstRW<[A64FXWrite_LD104],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD112, WriteAdr],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_LD107],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD115, WriteAdr],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, one lane, B/H
// ASIMD load, 4 element, one lane, S
// ASIMD load, 4 element, one lane, D
def : InstRW<[A64FXWrite_LD4I0], (instregex "^LD4i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_LD4I1, WriteAdr],
            (instregex "^LD4i(8|16|32|64)_POST$")>;

// ASIMD load, 4 element, all lanes, D-form, B/H/S
// ASIMD load, 4 element, all lanes, D-form, D
// ASIMD load, 4 element, all lanes, Q-form, B/H/S
// ASIMD load, 4 element, all lanes, Q-form, D
def : InstRW<[A64FXWrite_LD106],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_LD114, WriteAdr],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

//--
// 3.16 ASIMD Store Instructions
//--

// ASIMD store, 1 element, multiple, 1 reg, D-form
// ASIMD store, 1 element, multiple, 1 reg, Q-form
def : InstRW<[A64FXWrite_ST10],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST14, WriteAdr],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form
// ASIMD store, 1 element, multiple, 2 reg, Q-form
def : InstRW<[A64FXWrite_ST11],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST15, WriteAdr],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form
// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[A64FXWrite_ST12],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST16, WriteAdr],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form
// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[A64FXWrite_ST13],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST17, WriteAdr],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane, B/H/S
// ASIMD store, 1 element, one lane, D
def : InstRW<[A64FXWrite_ST10],
            (instregex "^ST1i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_ST14, WriteAdr],
            (instregex "^ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_ST11],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST15, WriteAdr],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane, B/H/S
// ASIMD store, 2 element, one lane, D
def : InstRW<[A64FXWrite_ST11],
            (instregex "^ST2i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_ST15, WriteAdr],
            (instregex "^ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_ST12],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST16, WriteAdr],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H
// ASIMD store, 3 element, one lane, S
// ASIMD store, 3 element, one lane, D
def : InstRW<[A64FXWrite_ST12], (instregex "^ST3i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_ST16, WriteAdr],
            (instregex "^ST3i(8|16|32|64)_POST$")>;

// ASIMD store, 4 element, multiple, D-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[A64FXWrite_ST13],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[A64FXWrite_ST17, WriteAdr],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 4 element, one lane, B/H
// ASIMD store, 4 element, one lane, S
// ASIMD store, 4 element, one lane, D
def : InstRW<[A64FXWrite_ST13], (instregex "^ST4i(8|16|32|64)$")>;
def : InstRW<[A64FXWrite_ST17, WriteAdr],
            (instregex "^ST4i(8|16|32|64)_POST$")>;

// V8.1a Atomics (LSE)
def : InstRW<[A64FXWrite_CAS, WriteAtomic],
            (instrs CASB, CASH, CASW, CASX)>;

def : InstRW<[A64FXWrite_CAS, WriteAtomic],
            (instrs CASAB, CASAH, CASAW, CASAX)>;

def : InstRW<[A64FXWrite_CAS, WriteAtomic],
            (instrs CASLB, CASLH, CASLW, CASLX)>;

def : InstRW<[A64FXWrite_CAS, WriteAtomic],
            (instrs CASALB, CASALH, CASALW, CASALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDLARB, LDLARH, LDLARW, LDLARX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDADDB, LDADDH, LDADDW, LDADDX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDADDAB, LDADDAH, LDADDAW, LDADDAX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDADDLB, LDADDLH, LDADDLW, LDADDLX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDADDALB, LDADDALH, LDADDALW, LDADDALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDCLRB, LDCLRH, LDCLRW, LDCLRX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDCLRAB, LDCLRAH, LDCLRAW, LDCLRAX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDCLRLB, LDCLRLH, LDCLRLW, LDCLRLX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDCLRALB, LDCLRALH, LDCLRALW, LDCLRALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDEORB, LDEORH, LDEORW, LDEORX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDEORAB, LDEORAH, LDEORAW, LDEORAX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDEORLB, LDEORLH, LDEORLW, LDEORLX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDEORALB, LDEORALH, LDEORALW, LDEORALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSETB, LDSETH, LDSETW, LDSETX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSETAB, LDSETAH, LDSETAW, LDSETAX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSETLB, LDSETLH, LDSETLW, LDSETLX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSETALB, LDSETALH, LDSETALW, LDSETALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSMAXB, LDSMAXH, LDSMAXW, LDSMAXX,
             LDSMAXAB, LDSMAXAH, LDSMAXAW, LDSMAXAX,
             LDSMAXLB, LDSMAXLH, LDSMAXLW, LDSMAXLX,
             LDSMAXALB, LDSMAXALH, LDSMAXALW, LDSMAXALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDSMINB, LDSMINH, LDSMINW, LDSMINX,
             LDSMINAB, LDSMINAH, LDSMINAW, LDSMINAX,
             LDSMINLB, LDSMINLH, LDSMINLW, LDSMINLX,
             LDSMINALB, LDSMINALH, LDSMINALW, LDSMINALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDUMAXB, LDUMAXH, LDUMAXW, LDUMAXX,
             LDUMAXAB, LDUMAXAH, LDUMAXAW, LDUMAXAX,
             LDUMAXLB, LDUMAXLH, LDUMAXLW, LDUMAXLX,
             LDUMAXALB, LDUMAXALH, LDUMAXALW, LDUMAXALX)>;

def : InstRW<[A64FXWrite_5Cyc_GI5, WriteAtomic],
            (instrs LDUMINB, LDUMINH, LDUMINW, LDUMINX,
             LDUMINAB, LDUMINAH, LDUMINAW, LDUMINAX,
             LDUMINLB, LDUMINLH, LDUMINLW, LDUMINLX,
             LDUMINALB, LDUMINALH, LDUMINALW, LDUMINALX)>;

def : InstRW<[A64FXWrite_SWP, WriteAtomic],
            (instrs SWPB, SWPH, SWPW, SWPX)>;

def : InstRW<[A64FXWrite_SWP, WriteAtomic],
            (instrs SWPAB, SWPAH, SWPAW, SWPAX)>;

def : InstRW<[A64FXWrite_SWP, WriteAtomic],
            (instrs SWPLB, SWPLH, SWPLW, SWPLX)>;

def : InstRW<[A64FXWrite_SWP, WriteAtomic],
            (instrs SWPALB, SWPALH, SWPALW, SWPALX)>;

def : InstRW<[A64FXWrite_STUR, WriteAtomic],
            (instrs STLLRB, STLLRH, STLLRW, STLLRX)>;

// SVE instructions

// The modeling method for SVE instructions is more accurate than others.
// TODO: modify the model of other instructions similarly.

def : InstRW<[A64FXWrite_4Cyc_GI0],
            (instregex "^AND_ZI", "^CL[SZ]_Z", "^CPY_ZP[mz]I", "^DUP_ZZ?I", "^DUPM_Z",
                       "^EOR_ZI", "^ORR_ZI", "^FCM(EQ|GT|GE|LT|LE|NE|UO)_P",
                       "^FCPY_Z", "^F(MAX|MIN).*I_", "^NEG_Z", "^[SU](MAX|MIN)_ZI",
                       "^SUBR?_ZI")>;

def : InstRW<[A64FXWrite_6Cyc_GI0],
            (instregex "^CLAST[AB]_[VZ]", "^COMPACT_Z", "^CPY_ZPmV", "^DUP_ZR",
                       "^EXT_Z", "^FDUP_Z", "^INSR_ZV", "^LAST[AB]_V", "^REV_Z",
                       "^SPLICE_Z", "^[SU]UNPK(HI|LO)_Z", "^TBL_Z", "^TRN[12]_Z")>;

def : InstRW<[A64FXWrite_9Cyc_GI0],
            (instregex "^F(ADD|SUBR?)_.*I_", "^FRECPS_Z", "^FRSQRTS_Z",
                       "^INDEX_II_[SD]", "^MUL_ZI")>;

def : InstRW<[A64FXWrite_4Cyc_GI3],
            (instregex "^CNT_Z")>;

def : InstRW<[A64FXWrite_4Cyc_GI03],
            (instregex "^ABS_Z", "^ADD_Z", "^AND_Z[^I]", "^ASRR?_(WIDE_)?Z",
                       "^BIC_Z", "^ADR_[SU]XTW_Z", "^CNOT_Z", "^DEC[BHWD]_Z",
                       "^EOR_Z[^I]", "^INC[BHWD]_Z", "^ORR_Z[^I]", "^FABS_Z",
                       "^FACG[ET]_P", "^FEXPA_Z", "^F(MAX|MIN)[^V]*Z_",
                       "^FNEG_Z", "^FRECP[EX]_Z", "^FRSQRTE_Z", "^FTSSEL_Z",
                       "^LS[LR]R?(_WIDE)?_Z", "^NOT_Z", "^RBIT_Z", "^REV[BHW]_Z", "^SABD_Z",
                       "^SEL_Z", "^[SU](MAX|MIN)_ZP", "^[SU]Q(INC|DEC)[^P]_Z",
                       "^SUBR?_Z[^I]", "^[SU]XT._Z", "^UABD_Z")>;

def : InstRW<[A64FXWrite_9Cyc_GI03      ],
            (instregex "^FABD_Z", "^F(ADD|SUBR?)_.*Z_", "^FN?(MAD|MLA|MLS|MSB)_ZP",
                       "^FMUL_(ZP|ZZZ_)", "^FMULX_Z", "^FCVT(ZS|ZU)?_Z",
                       "^FRINT._Z", "^FSCALE_Z", "^FTMAD_Z", "^FTSMUL_Z",
                       "^MAD_Z", "^MLA_Z", "^MLS_Z", "^MSB_Z", "^MUL_ZP",
                       "^[SU]CVTF_Z", "^[SU]DOT_ZZZ_", "^[SU]MULH_Z")>;

def : InstRW<[A64FXWrite_3Cyc_GI1],
            (instregex "^ANDS?_P", "^BICS?_P", "^BRK.*_P", "^EORS?_P", "^ORRS?_P",
                       "^NANDS?_P", "^NORS?_P", "^ORNS?_P", "^PFALSE", "^PNEXT",
                       "^PFIRST", "^PTEST", "^PTRUES?", "^PUNPK(HI|LO)",
                       "^RDFFRS?", "^REV_P", "^SEL_P", "^TRN[12]_P")>;

def : InstRW<[A64FXWrite_1Cyc_GI24],
            (instregex "^ADD[PV]L", "^CNT[BHWD]_X", "^DEC[BHWD]_X", "^INC[BHWD]_X",
                       "^RDVLI")>;

def : InstRW<[A64FXWrite_11Cyc_GI5],
            (instregex "^LDR_[PZ]XI")>;

def : InstRW<[A64FXWrite_11Cyc_GI56],
            (instregex "^LD(NF|FF|NT)?1R?S?[BHSWDQ]")>;

def A64FXWrite_None : SchedWriteRes<[]> {
}
def : InstRW<[A64FXWrite_None], (instregex "^SETFFR", "^MOVPRFX")>;

def A64FXWrite_FMAIndexed : SchedWriteRes<[A64FXGI03]> {
  let Latency = 15;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_FMAIndexed], (instregex "^F(MLA|MLS|MUL)_ZZZI")>;

def A64FXWrite_ADR_LSL_Z : SchedWriteRes<[A64FXGI0]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_ADR_LSL_Z], (instregex "^ADR_LSL_Z")>;

def A64FXWrite_ASRD : SchedWriteRes<[A64FXGI0, A64FXGI01]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[A64FXWrite_ASRD], (instregex "^ASRD_Z")>;

def A64FXWrite_Reduction4CycB : SchedWriteRes<[A64FXGI03]> {
  let Latency = 46;
  let NumMicroOps = 10;
  let ReleaseAtCycles = [10];
}
def : InstRW<[A64FXWrite_Reduction4CycB],
      (instregex "^(AND|EOR|OR|SADD|SMAX|SMIN|UADD|UMAX|UMIN)V_VPZ_B")>;

def A64FXWrite_Reduction4CycH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 42;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [9];
}
def : InstRW<[A64FXWrite_Reduction4CycH],
      (instregex "^(AND|EOR|OR|SADD|SMAX|SMIN|UADD|UMAX|UMIN)V_VPZ_H")>;

def A64FXWrite_Reduction4CycS : SchedWriteRes<[A64FXGI03]> {
  let Latency = 38;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [8];
}
def : InstRW<[A64FXWrite_Reduction4CycS],
      (instregex "^(AND|EOR|OR|SADD|SMAX|SMIN|UADD|UMAX|UMIN)V_VPZ_S")>;

def A64FXWrite_Reduction4CycD : SchedWriteRes<[A64FXGI03]> {
  let Latency = 34;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [7];
}
def : InstRW<[A64FXWrite_Reduction4CycD],
      (instregex "^(AND|EOR|OR|SADD|SMAX|SMIN|UADD|UMAX|UMIN)V_VPZ_D")>;

def A64FXWrite_CLAST_R : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 29;
}
def : InstRW<[A64FXWrite_CLAST_R], (instregex "^CLAST[AB]_R")>;

def A64FXWrite_CMP : SchedWriteRes<[A64FXGI0, A64FXGI1]> {
  let Latency = 4;
}
def : InstRW<[A64FXWrite_CMP], (instregex "^CMP.*_P")>;

def A64FXWrite_CNTP : SchedWriteRes<[A64FXGI1, A64FXGI2]> {
  let Latency = 6;
}
def : InstRW<[A64FXWrite_CNTP], (instregex "^CNTP_X")>;

def A64FXWrite_CPYScalar : SchedWriteRes<[A64FXGI0, A64FXGI2]> {
  let Latency = 8;
}
def : InstRW<[A64FXWrite_CPYScalar], (instregex "^CPY_ZPmR")>;

def A64FXWrite_CTERM : SchedWriteRes<[A64FXGI24]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_CTERM], (instregex "^CTERM")>;

def A64FXWrite_INCPScalar : SchedWriteRes<[A64FXGI1, A64FXGI2, A64FXGI4]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[A64FXWrite_INCPScalar], (instregex "^DECP_X", "^INCP_X")>;

def A64FXWrite_INCPVector : SchedWriteRes<[A64FXGI0, A64FXGI1]> {
  let Latency = 12;
}
def : InstRW<[A64FXWrite_INCPVector], (instregex "^DECP_Z", "^INCP_Z")>;

def A64FXWrite_FADDVH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 75;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [11];
}
def : InstRW<[A64FXWrite_FADDVH], (instrs FADDV_VPZ_H)>;

def A64FXWrite_FADDVS : SchedWriteRes<[A64FXGI03]> {
  let Latency = 60;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [9];
}
def : InstRW<[A64FXWrite_FADDVS], (instrs FADDV_VPZ_S)>;

def A64FXWrite_FADDVD : SchedWriteRes<[A64FXGI03]> {
  let Latency = 45;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [7];
}
def : InstRW<[A64FXWrite_FADDVD], (instrs FADDV_VPZ_D)>;

def A64FXWrite_FADDAH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 468;
  let NumMicroOps = 63;
  let ReleaseAtCycles = [63];
}
def : InstRW<[A64FXWrite_FADDAH], (instrs FADDA_VPZ_H)>;

def A64FXWrite_FADDAS : SchedWriteRes<[A64FXGI03]> {
  let Latency = 228;
  let NumMicroOps = 31;
  let ReleaseAtCycles = [31];
}
def : InstRW<[A64FXWrite_FADDAS], (instrs FADDA_VPZ_S)>;

def A64FXWrite_FADDAD : SchedWriteRes<[A64FXGI03]> {
  let Latency = 108;
  let NumMicroOps = 15;
  let ReleaseAtCycles = [15];
}
def : InstRW<[A64FXWrite_FADDAD], (instrs FADDA_VPZ_D)>;

def A64FXWrite_FCADDZ : SchedWriteRes<[A64FXGI0, A64FXGI3]> {
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[A64FXWrite_FCADDZ], (instregex "^FCADD_Z")>;

def A64FXWrite_FCMLAZ : SchedWriteRes<[A64FXGI03]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : InstRW<[A64FXWrite_FCMLAZ], (instregex "^FCMLA_Z")>;

def A64FXWrite_FDIVH : SchedWriteRes<[A64FXGI0]> {
  let Latency = 134;
  let ReleaseAtCycles = [134];
}
def : InstRW<[A64FXWrite_FDIVH], (instregex "^F(DIVR?|SQRT)_Z.*_H")>;

def A64FXWrite_FDIVS : SchedWriteRes<[A64FXGI0]> {
  let Latency = 98;
  let ReleaseAtCycles = [98];
}
def : InstRW<[A64FXWrite_FDIVS], (instregex "^F(DIVR?|SQRT)_Z.*_S")>;

def A64FXWrite_FDIVD : SchedWriteRes<[A64FXGI0]> {
  let Latency = 154;
  let ReleaseAtCycles = [154];
}
def : InstRW<[A64FXWrite_FDIVD], (instregex "^F(DIVR?|SQRT)_Z.*_D")>;

def A64FXWrite_FMAXVH : SchedWriteRes<[A64FXGI03]> {
  let Latency = 54;
  let NumMicroOps = 11;
  let ReleaseAtCycles = [11];
}
def : InstRW<[A64FXWrite_FMAXVH], (instregex "^F(MAX|MIN)(NM)?V_VPZ_H")>;

def A64FXWrite_FMAXVS : SchedWriteRes<[A64FXGI03]> {
  let Latency = 44;
  let NumMicroOps = 9;
  let ReleaseAtCycles = [9];
}
def : InstRW<[A64FXWrite_FMAXVS], (instregex "^F(MAX|MIN)(NM)?V_VPZ_S")>;

def A64FXWrite_FMAXVD : SchedWriteRes<[A64FXGI03]> {
  let Latency = 34;
  let NumMicroOps = 7;
  let ReleaseAtCycles = [7];
}
def : InstRW<[A64FXWrite_FMAXVH], (instregex "^F(MAX|MIN)(NM)?V_VPZ_D")>;

def A64FXWrite_INDEX_RI_BH : SchedWriteRes<[A64FXGI0, A64FXGI2]> {
  let Latency = 17;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2, 2];
}
def : InstRW<[A64FXWrite_INDEX_RI_BH], (instregex "^INDEX_(RI|IR)_[BH]")>;

def A64FXWrite_INDEX_RI_SD : SchedWriteRes<[A64FXGI0, A64FXGI2]> {
  let Latency = 13;
  let NumMicroOps = 1;
}
def : InstRW<[A64FXWrite_INDEX_RI_SD], (instregex "^INDEX_(RI|IR)_[SD]")>;

def A64FXWrite_INDEX_II_BH : SchedWriteRes<[A64FXGI0]> {
  let Latency = 13;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_INDEX_II_BH], (instregex "^INDEX_II_[BH]")>;

def A64FXWrite_INDEX_RR_BH : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI3]> {
  let Latency = 17;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2, 2, 1];
}
def : InstRW<[A64FXWrite_INDEX_RR_BH], (instregex "^INDEX_RR_[BH]")>;

def A64FXWrite_INDEX_RR_SD : SchedWriteRes<[A64FXGI0, A64FXGI2]> {
  let Latency = 17;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2, 1];
}
def : InstRW<[A64FXWrite_INDEX_RR_SD], (instregex "^INDEX_RR_[SD]")>;

def A64FXWrite_INSR_ZR : SchedWriteRes<[A64FXGI0, A64FXGI2]> {
  let Latency = 10;
}
def : InstRW<[A64FXWrite_INSR_ZR], (instregex "^INSR_ZR")>;

def A64FXWrite_LAST_R : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 25;
}
def : InstRW<[A64FXWrite_CLAST_R], (instregex "^LAST[AB]_R")>;

def A64FXWrite_GLD_S_ZI : SchedWriteRes<[A64FXGI0, A64FXGI5, A64FXGI6]> {
  let Latency = 19;
  let ReleaseAtCycles = [2, 4, 4];
}
def : InstRW<[A64FXWrite_GLD_S_ZI],
      (instregex "^GLD(FF)?1W_IMM", "^GLD(FF)?1S?[BHW]_S_IMM")>;

def A64FXWrite_GLD_D_ZI : SchedWriteRes<[A64FXGI0, A64FXGI5, A64FXGI6]> {
  let Latency = 16;
  let ReleaseAtCycles = [1, 2, 2];
}
def : InstRW<[A64FXWrite_GLD_D_ZI],
      (instregex "^GLD(FF)?1D_IMM", "^GLD(FF)?1S?[BHW]_D_IMM")>;

def A64FXWrite_GLD_S_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI5, A64FXGI6]> {
  let Latency = 23;
  let ReleaseAtCycles = [2, 1, 4, 4];
}
def : InstRW<[A64FXWrite_GLD_S_RZ],
      (instregex "^GLD(FF)?1W_[^DI]", "^GLD(FF)?1S?[BHW]_S_[^I]")>;

def A64FXWrite_GLD_D_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI5, A64FXGI6]> {
  let Latency = 20;
  let ReleaseAtCycles = [1, 1, 2, 2];
}
def : InstRW<[A64FXWrite_GLD_D_RZ],
      (instregex "^GLD(FF)?1D_[^I]", "^GLD(FF)?1D$", "^GLD(FF)?1S?[BHW]_D_[^I]",
                 "^GLD(FF)?1S?[BHW]_D$")>;

def A64FXWrite_LD2_BH : SchedWriteRes<[A64FXGI56]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [9];
}
def : InstRW<[A64FXWrite_LD2_BH], (instregex "^LD2[BH]")>;

def A64FXWrite_LD2_WD_IMM : SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_LD2_WD_IMM], (instregex "^LD2[WD]_IMM")>;

def A64FXWrite_LD2_WD : SchedWriteRes<[A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : InstRW<[A64FXWrite_LD2_WD], (instregex "^LD2[WD]$")>;

def A64FXWrite_LD3_BH : SchedWriteRes<[A64FXGI56]> {
  let Latency = 15;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [13];
}
def : InstRW<[A64FXWrite_LD3_BH], (instregex "^LD3[BH]")>;

def A64FXWrite_LD3_WD_IMM : SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3];
}
def : InstRW<[A64FXWrite_LD3_WD_IMM], (instregex "^LD3[WD]_IMM")>;

def A64FXWrite_LD3_WD : SchedWriteRes<[A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}
def : InstRW<[A64FXWrite_LD3_WD], (instregex "^LD3[WD]$")>;

def A64FXWrite_LD4_BH : SchedWriteRes<[A64FXGI56]> {
  let Latency = 15;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [17];
}
def : InstRW<[A64FXWrite_LD4_BH], (instregex "^LD4[BH]")>;

def A64FXWrite_LD4_WD_IMM : SchedWriteRes<[A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}
def : InstRW<[A64FXWrite_LD4_WD_IMM], (instregex "^LD4[WD]_IMM")>;

def A64FXWrite_LD4_WD : SchedWriteRes<[A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [5];
}
def : InstRW<[A64FXWrite_LD4_WD], (instregex "^LD4[WD]$")>;

def A64FXWrite_PRF : SchedWriteRes<[A64FXGI56]> {
}
def : InstRW<[A64FXWrite_PRF], (instregex "^PRF._PR")>;

def A64FXWrite_PRF_W_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI56]> {
  let ReleaseAtCycles = [2, 1, 4];
}
def : InstRW<[A64FXWrite_PRF_W_RZ], (instregex "^PRF._S_[^P]")>;

def A64FXWrite_PRF_W_ZI : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let ReleaseAtCycles = [2, 4];
}
def : InstRW<[A64FXWrite_PRF_W_ZI], (instregex "^PRF._S_PZI")>;

def A64FXWrite_PRF_D_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI56]> {
  let ReleaseAtCycles = [1, 1, 2];
}
def : InstRW<[A64FXWrite_PRF_D_RZ], (instregex "^PRF._D_[^P]")>;

def A64FXWrite_PRF_D_ZI : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let ReleaseAtCycles = [1, 2];
}
def : InstRW<[A64FXWrite_PRF_D_ZI], (instregex "^PRF._D_PZI")>;

def A64FXWrite_SDIV_S : SchedWriteRes<[A64FXGI0]> {
  let Latency = 114;
  let ReleaseAtCycles = [114];
}
def : InstRW<[A64FXWrite_SDIV_S], (instregex "^[SU]DIVR?.*_S")>;

def A64FXWrite_SDIV_D : SchedWriteRes<[A64FXGI0]> {
  let Latency = 178;
  let ReleaseAtCycles = [178];
}
def : InstRW<[A64FXWrite_SDIV_D], (instregex "^[SU]DIVR?.*_D")>;

def A64FXWrite_SDOT_I : SchedWriteRes<[A64FXGI0, A64FXGI3]> {
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[A64FXWrite_SDOT_I], (instregex "^[SU]DOT_ZZZI")>;

def A64FXWrite_SQINC_Scalar : SchedWriteRes<[A64FXGI24]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}
def : InstRW<[A64FXWrite_SQINC_Scalar], (instregex "^[SU]Q(INC|DEC)[BHWD]_[WX]")>;

def A64FXWrite_SQINCP_X : SchedWriteRes<[A64FXGI24, A64FXGI3]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [3, 1];
}
def : InstRW<[A64FXWrite_SQINCP_X], (instregex "^[SU]Q(INC|DEC)P_[WX]")>;

def A64FXWrite_SQINCP_Z : SchedWriteRes<[A64FXGI24, A64FXGI3]> {
  let Latency = 12;
}
def : InstRW<[A64FXWrite_SQINCP_Z], (instregex "^[SU]Q(INC|DEC)P_Z")>;

def A64FXWrite_ST1 : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 11;
}
def : InstRW<[A64FXWrite_ST1], (instregex "^ST(NT)?1[BHWD]")>;

def A64FXWrite_SST1_W_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI5, A64FXGI6]> {
  let Latency = 20;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [8, 8, 8, 8];
}
def : InstRW<[A64FXWrite_SST1_W_RZ],
      (instregex "^SST1[BH]_S(_[^I]|$)", "^SST1W(_[^ID]|$)")>;

def A64FXWrite_SST1_D_RZ : SchedWriteRes<[A64FXGI0, A64FXGI2, A64FXGI5, A64FXGI6]> {
  let Latency = 20;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4, 4, 4, 4];
}
def : InstRW<[A64FXWrite_SST1_D_RZ],
      (instregex "^SST1[BHW]_D(_[^I]|$)", "^SST1D(_[^I]|$)")>;

def A64FXWrite_SST1_W_ZI : SchedWriteRes<[A64FXGI0, A64FXGI5, A64FXGI6]> {
  let Latency = 16;
  let NumMicroOps = 8;
  let ReleaseAtCycles = [12, 8, 8];
}
def : InstRW<[A64FXWrite_SST1_W_ZI],
      (instregex "^SST1[BH]_S_I", "^SST1W_I")>;

def A64FXWrite_SST1_D_ZI : SchedWriteRes<[A64FXGI0, A64FXGI5, A64FXGI6]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4, 4, 4];
}
def : InstRW<[A64FXWrite_SST1_D_ZI],
      (instregex "^SST1[BHW]_D_I", "^SST1D_I")>;

def A64FXWrite_ST2_BH : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [8, 9];
}
def : InstRW<[A64FXWrite_ST2_BH], (instregex "^ST2[BH]")>;

def A64FXWrite_ST2_WD_RI : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2, 2];
}
def : InstRW<[A64FXWrite_ST2_WD_RI], (instregex "^ST2[WD]$")>;

def A64FXWrite_ST2_WD_RR : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [2, 3];
}
def : InstRW<[A64FXWrite_ST2_WD_RR], (instregex "^ST2[WD]_I")>;

def A64FXWrite_ST3_BH : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 15;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [12, 13];
}
def : InstRW<[A64FXWrite_ST3_BH], (instregex "^ST3[BH]")>;

def A64FXWrite_ST3_WD_RI : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3, 3];
}
def : InstRW<[A64FXWrite_ST3_WD_RI], (instregex "^ST3[WD]$")>;

def A64FXWrite_ST3_WD_RR : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [3, 4];
}
def : InstRW<[A64FXWrite_ST3_WD_RR], (instregex "^ST3[WD]_I")>;

def A64FXWrite_ST4_BH : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 15;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [16, 17];
}
def : InstRW<[A64FXWrite_ST4_BH], (instregex "^ST4[BH]")>;

def A64FXWrite_ST4_WD_RI : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 11;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4, 4];
}
def : InstRW<[A64FXWrite_ST4_WD_RI], (instregex "^ST4[WD]$")>;

def A64FXWrite_ST4_WD_RR : SchedWriteRes<[A64FXGI0, A64FXGI56]> {
  let Latency = 12;
  let NumMicroOps = 5;
  let ReleaseAtCycles = [4, 5];
}
def : InstRW<[A64FXWrite_ST4_WD_RR], (instregex "^ST4[WD]_I")>;

def A64FXWrite_STR_P : SchedWriteRes<[A64FXGI3, A64FXGI5]> {
  let Latency = 11;
}
def : InstRW<[A64FXWrite_STR_P], (instrs STR_PXI)>;

def A64FXWrite_STR_Z : SchedWriteRes<[A64FXGI0, A64FXGI5]> {
  let Latency = 11;
}
def : InstRW<[A64FXWrite_STR_Z], (instrs STR_ZXI)>;

def A64FXWrite_WHILE : SchedWriteRes<[A64FXGI3, A64FXGI5]> {
  let Latency = 4;
}
def : InstRW<[A64FXWrite_WHILE], (instregex "^WHILEL._P")>;

def A64FXWrite_WRFFR : SchedWriteRes<[A64FXGI3, A64FXGI5]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : InstRW<[A64FXWrite_WRFFR], (instrs WRFFR)>;

} // SchedModel = A64FXModel
