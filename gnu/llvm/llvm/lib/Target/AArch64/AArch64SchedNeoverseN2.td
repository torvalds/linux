//=- AArch64SchedNeoverseN2.td - NeoverseN2 Scheduling Defs --*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for the Arm Neoverse N2 processors.
//
//===----------------------------------------------------------------------===//

def NeoverseN2Model : SchedMachineModel {
  let IssueWidth            =  10; // Micro-ops dispatched at a time.
  let MicroOpBufferSize     = 160; // Entries in micro-op re-order buffer.
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  10; // Extra cycles for mispredicted branch.
  let LoopMicroOpBufferSize =  16; // NOTE: Copied from Cortex-A57.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SMEUnsupported.F,
    [HasSVE2p1, HasPAuthLR, HasCPA, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Neoverse N2.
// Instructions are first fetched and then decoded into internal macro-ops
// (MOPs). From there, the MOPs proceed through register renaming and dispatch
// stages. A MOP can be split into two micro-ops further down the pipeline
// after the decode stage. Once dispatched, micro-ops wait for their operands
// and issue out-of-order to one of thirteen issue pipelines. Each issue
// pipeline can accept one micro-op per cycle.

let SchedModel = NeoverseN2Model in {

// Define the (13) issue ports.
def N2UnitB   : ProcResource<2>;  // Branch 0/1
def N2UnitS   : ProcResource<2>;  // Integer single Cycle 0/1
def N2UnitM0  : ProcResource<1>;  // Integer multicycle 0
def N2UnitM1  : ProcResource<1>;  // Integer multicycle 1
def N2UnitL01 : ProcResource<2>;  // Load/Store 0/1
def N2UnitL2  : ProcResource<1>;  // Load 2
def N2UnitD   : ProcResource<2>;  // Store data 0/1
def N2UnitV0  : ProcResource<1>;  // FP/ASIMD 0
def N2UnitV1  : ProcResource<1>;  // FP/ASIMD 1

def N2UnitV : ProcResGroup<[N2UnitV0, N2UnitV1]>;  // FP/ASIMD 0/1
def N2UnitM : ProcResGroup<[N2UnitM0, N2UnitM1]>;  // Integer single/multicycle 0/1
def N2UnitL : ProcResGroup<[N2UnitL01, N2UnitL2]>; // Load/Store 0/1 and Load 2
def N2UnitI : ProcResGroup<[N2UnitS, N2UnitM0, N2UnitM1]>; // Integer single cycle 0/1 and single/multicycle 0/1

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

def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }
def : WriteRes<WriteLDHi,    []> { let Latency = 4; }

//===----------------------------------------------------------------------===//
// Define customized scheduler read/write types specific to the Neoverse N2.

//===----------------------------------------------------------------------===//
// Define generic 1 micro-op types

def N2Write_1cyc_1B   : SchedWriteRes<[N2UnitB]>   { let Latency = 1; }
def N2Write_1cyc_1I   : SchedWriteRes<[N2UnitI]>   { let Latency = 1; }
def N2Write_1cyc_1M   : SchedWriteRes<[N2UnitM]>   { let Latency = 1; }
def N2Write_1cyc_1M0  : SchedWriteRes<[N2UnitM0]>  { let Latency = 1; }
def N2Write_1cyc_1L01 : SchedWriteRes<[N2UnitL01]> { let Latency = 1; }
def N2Write_2cyc_1M   : SchedWriteRes<[N2UnitM]>   { let Latency = 2; }
def N2Write_3cyc_1M   : SchedWriteRes<[N2UnitM]>   { let Latency = 3; }
def N2Write_2cyc_1M0  : SchedWriteRes<[N2UnitM0]>  { let Latency = 2;
                                                     let ReleaseAtCycles = [2]; }
def N2Write_3cyc_1M0  : SchedWriteRes<[N2UnitM0]>  { let Latency = 3;
                                                     let ReleaseAtCycles = [3]; }
def N2Write_5cyc_1M0  : SchedWriteRes<[N2UnitM0]>  { let Latency = 5;
                                                     let ReleaseAtCycles = [5]; }
def N2Write_12cyc_1M0 : SchedWriteRes<[N2UnitM0]>  { let Latency = 12;
                                                     let ReleaseAtCycles = [12]; }
def N2Write_20cyc_1M0 : SchedWriteRes<[N2UnitM0]>  { let Latency = 20;
                                                     let ReleaseAtCycles = [20]; }
def N2Write_4cyc_1L   : SchedWriteRes<[N2UnitL]>   { let Latency = 4; }
def N2Write_6cyc_1L   : SchedWriteRes<[N2UnitL]>   { let Latency = 6; }
def N2Write_2cyc_1V   : SchedWriteRes<[N2UnitV]>   { let Latency = 2; }
def N2Write_3cyc_1V   : SchedWriteRes<[N2UnitV]>   { let Latency = 3; }
def N2Write_4cyc_1V   : SchedWriteRes<[N2UnitV]>   { let Latency = 4; }
def N2Write_5cyc_1V   : SchedWriteRes<[N2UnitV]>   { let Latency = 5; }
def N2Write_12cyc_1V  : SchedWriteRes<[N2UnitV]>   { let Latency = 12; }
def N2Write_2cyc_1V0  : SchedWriteRes<[N2UnitV0]>  { let Latency = 2; }
def N2Write_3cyc_1V0  : SchedWriteRes<[N2UnitV0]>  { let Latency = 3; }
def N2Write_4cyc_1V0  : SchedWriteRes<[N2UnitV0]>  { let Latency = 4; }
def N2Write_7cyc_1V0  : SchedWriteRes<[N2UnitV0]>  { let Latency = 7;
                                                     let ReleaseAtCycles = [7]; }
def N2Write_9cyc_1V0  : SchedWriteRes<[N2UnitV0]>  { let Latency = 9; }
def N2Write_10cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 10; }
def N2Write_12cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 12; }
def N2Write_13cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 13; }
def N2Write_15cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 15; }
def N2Write_16cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 16; }
def N2Write_20cyc_1V0 : SchedWriteRes<[N2UnitV0]>  { let Latency = 20; }
def N2Write_2cyc_1V1  : SchedWriteRes<[N2UnitV1]>  { let Latency = 2; }
def N2Write_3cyc_1V1  : SchedWriteRes<[N2UnitV1]>  { let Latency = 3; }
def N2Write_4cyc_1V1  : SchedWriteRes<[N2UnitV1]>  { let Latency = 4; }
def N2Write_6cyc_1V1  : SchedWriteRes<[N2UnitV1]>  { let Latency = 6; }
def N2Write_10cyc_1V1 : SchedWriteRes<[N2UnitV1]>  { let Latency = 10; }
def N2Write_6cyc_1L01 : SchedWriteRes<[N2UnitL01]> { let Latency = 6; }

//===----------------------------------------------------------------------===//
// Define generic 2 micro-op types

def N2Write_1cyc_1B_1S : SchedWriteRes<[N2UnitB, N2UnitS]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}

def N2Write_6cyc_1M0_1B : SchedWriteRes<[N2UnitM0, N2UnitB]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_9cyc_1M0_1L : SchedWriteRes<[N2UnitM0, N2UnitL]> {
  let Latency     = 9;
  let NumMicroOps = 2;
}

def N2Write_3cyc_1I_1M : SchedWriteRes<[N2UnitI, N2UnitM]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}

def N2Write_4cyc_1I_1L : SchedWriteRes<[N2UnitI, N2UnitL]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_5cyc_1I_1L : SchedWriteRes<[N2UnitI, N2UnitL]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}

def N2Write_6cyc_1I_1L : SchedWriteRes<[N2UnitI, N2UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_7cyc_1I_1L : SchedWriteRes<[N2UnitI, N2UnitL]> {
  let Latency     = 7;
  let NumMicroOps = 2;
}

def N2Write_1cyc_1L01_1D : SchedWriteRes<[N2UnitL01, N2UnitD]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}

def N2Write_5cyc_1M0_1V : SchedWriteRes<[N2UnitM0, N2UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}

def N2Write_2cyc_1L01_1V : SchedWriteRes<[N2UnitL01, N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}

def N2Write_4cyc_1V1_1V : SchedWriteRes<[N2UnitV1, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_4cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_10cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [5, 5];
}

def N2Write_13cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency = 13;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [6, 7];
}

def N2Write_15cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency = 15;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [7, 8];
}

def N2Write_16cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency = 16;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [8, 8];
}

def N2Write_4cyc_2V : SchedWriteRes<[N2UnitV, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_6cyc_2V : SchedWriteRes<[N2UnitV, N2UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_6cyc_2L : SchedWriteRes<[N2UnitL, N2UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_8cyc_1L_1V : SchedWriteRes<[N2UnitL, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 2;
}

def N2Write_4cyc_1L01_1V : SchedWriteRes<[N2UnitL01, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_3cyc_1M0_1M  : SchedWriteRes<[N2UnitM0, N2UnitM]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}

def N2Write_2cyc_1M0_1M  : SchedWriteRes<[N2UnitM0, N2UnitM]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}

def N2Write_6cyc_2V1 : SchedWriteRes<[N2UnitV1, N2UnitV1]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_4cyc_1V0_1M : SchedWriteRes<[N2UnitV0, N2UnitM]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

def N2Write_5cyc_2V0 : SchedWriteRes<[N2UnitV0, N2UnitV0]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}

def N2Write_5cyc_1V1_1M0 : SchedWriteRes<[N2UnitV1, N2UnitM0]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}

def N2Write_7cyc_1M0_1V0 : SchedWriteRes<[N2UnitM0, N2UnitV0]> {
  let Latency     = 7;
  let NumMicroOps = 2;
}

def N2Write_2cyc_1V0_1M : SchedWriteRes<[N2UnitV0, N2UnitM]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}

def N2Write_6cyc_1V_1V1 : SchedWriteRes<[N2UnitV, N2UnitV1]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_6cyc_1L_1M : SchedWriteRes<[N2UnitL, N2UnitM]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_6cyc_1L_1S : SchedWriteRes<[N2UnitL, N2UnitS]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}

def N2Write_9cyc_1L_1V : SchedWriteRes<[N2UnitL, N2UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 2;
}

def N2Write_4cyc_2V1 : SchedWriteRes<[N2UnitV1, N2UnitV1]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

//===----------------------------------------------------------------------===//
// Define generic 3 micro-op types

def N2Write_1cyc_1L01_1D_1I : SchedWriteRes<[N2UnitL01, N2UnitD, N2UnitI]> {
  let Latency     = 1;
  let NumMicroOps = 3;
}

def N2Write_2cyc_1L01_1V_1I : SchedWriteRes<[N2UnitL01, N2UnitV, N2UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 3;
}

def N2Write_2cyc_1L01_2V : SchedWriteRes<[N2UnitL01, N2UnitV, N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 3;
}

def N2Write_7cyc_1M_1M0_1V : SchedWriteRes<[N2UnitM, N2UnitM0, N2UnitV]> {
  let Latency     = 7;
  let NumMicroOps = 3;
}

def N2Write_8cyc_1M0_1V1_1V : SchedWriteRes<[N2UnitM0, N2UnitV1, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}

def N2Write_10cyc_1V_1L_1S : SchedWriteRes<[N2UnitV, N2UnitL, N2UnitL]> {
  let Latency     = 10;
  let NumMicroOps = 3;
}

def N2Write_2cyc_1L01_1S_1V : SchedWriteRes<[N2UnitL01, N2UnitS, N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 3;
}

def N2Write_4cyc_1L01_1S_1V : SchedWriteRes<[N2UnitL01, N2UnitS, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 3;
}

def N2Write_6cyc_3L : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 3;
}

def N2Write_8cyc_1L_2V : SchedWriteRes<[N2UnitL, N2UnitV, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}

//===----------------------------------------------------------------------===//
// Define generic 4 micro-op types

def N2Write_2cyc_1L01_2V_1I : SchedWriteRes<[N2UnitL01, N2UnitV, N2UnitV,
                                             N2UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 4;
}

def N2Write_6cyc_4V0 : SchedWriteRes<[N2UnitV0, N2UnitV0, N2UnitV0, N2UnitV0]> {
  let Latency     = 6;
  let NumMicroOps = 4;
}

def N2Write_4cyc_4V : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 4;
}

def N2Write_6cyc_4V : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 4;
}

def N2Write_8cyc_2L_2V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitV, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 4;
}

def N2Write_9cyc_2L_2V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitV, N2UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 4;
}

def N2Write_2cyc_2L01_2V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitV,
                                          N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 4;
}

def N2Write_4cyc_2L01_2V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitV,
                                          N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 4;
}

def N2Write_5cyc_2L01_2V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitV,
                                          N2UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 4;
}

def N2Write_8cyc_2M0_2V0 : SchedWriteRes<[N2UnitM0, N2UnitM0, N2UnitV0,
                                          N2UnitV0]> {
  let Latency     = 8;
  let NumMicroOps = 4;
}

def N2Write_11cyc_2V_2V1 : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV1,
                                          N2UnitV1]> {
  let Latency     = 11;
  let NumMicroOps = 4;
}

def N2Write_9cyc_2V_2V1 : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV1,
                                         N2UnitV1]> {
  let Latency     = 9;
  let NumMicroOps = 4;
}

def N2Write_8cyc_2V_2V1 : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV1,
                                         N2UnitV1]> {
  let Latency     = 8;
  let NumMicroOps = 4;
}

def N2Write_10cyc_2L_2V1 : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV1,
                                          N2UnitV1]> {
  let Latency     = 10;
  let NumMicroOps = 4;
}

def N2Write_10cyc_2L_2V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitV, N2UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 4;
}

def N2Write_4cyc_2M0_2M : SchedWriteRes<[N2UnitM0, N2UnitM0, N2UnitM,
                                         N2UnitM]> {
  let Latency     = 4;
  let NumMicroOps = 4;
}

def N2Write_6cyc_2I_2L : SchedWriteRes<[N2UnitI, N2UnitI, N2UnitL, N2UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 4;
}

def N2Write_7cyc_4L : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL, N2UnitL]> {
  let Latency     = 7;
  let NumMicroOps = 4;
}

//===----------------------------------------------------------------------===//
// Define generic 5 micro-op types

def N2Write_2cyc_1L01_2V_2I : SchedWriteRes<[N2UnitL01, N2UnitV, N2UnitV,
                                             N2UnitI, N2UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 5;
}

def N2Write_8cyc_2L_3V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitV, N2UnitV,
                                        N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 5;
}

//===----------------------------------------------------------------------===//
// Define generic 6 micro-op types

def N2Write_8cyc_3L_3V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL,
                                        N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 6;
}

def N2Write_2cyc_3L01_3V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 6;
}

def N2Write_6cyc_3L01_3V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 6;
}

def N2Write_4cyc_3L01_3V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 6;
}

def N2Write_10cyc_2L_2V_2S : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitV, N2UnitV,
                                            N2UnitS, N2UnitS]> {
  let Latency     = 10;
  let NumMicroOps = 6;
}

//===----------------------------------------------------------------------===//
// Define generic 7 micro-op types

def N2Write_8cyc_3L_4V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL,
                                        N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 7;
}

//===----------------------------------------------------------------------===//
// Define generic 8 micro-op types

def N2Write_6cyc_8V : SchedWriteRes<[N2UnitV, N2UnitV, N2UnitV, N2UnitV,
                                     N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 8;
}

def N2Write_2cyc_4L01_4V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitL01, N2UnitV, N2UnitV, N2UnitV,
                                          N2UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 8;
}

def N2Write_5cyc_4L01_4V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitL01, N2UnitV, N2UnitV, N2UnitV,
                                          N2UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 8;
}

def N2Write_8cyc_4L_4V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL, N2UnitL,
                                        N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 8;
}

def N2Write_9cyc_4L_4V : SchedWriteRes<[N2UnitL, N2UnitL, N2UnitL, N2UnitL,
                                        N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 8;
}

//===----------------------------------------------------------------------===//
// Define generic 10 micro-op types

def N2Write_7cyc_5L01_5V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitL01, N2UnitL01, N2UnitV,
                                          N2UnitV, N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 7;
  let NumMicroOps = 10;
}

//===----------------------------------------------------------------------===//
// Define generic 12 micro-op types

def N2Write_7cyc_6L01_6V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitL01, N2UnitL01, N2UnitL01,
                                          N2UnitV, N2UnitV, N2UnitV, N2UnitV,
                                          N2UnitV, N2UnitV]> {
  let Latency     = 7;
  let NumMicroOps = 12;
}

//===----------------------------------------------------------------------===//
// Define generic 15 micro-op types

def N2Write_7cyc_5L01_5S_5V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                             N2UnitL01, N2UnitL01, N2UnitS,
                                             N2UnitS, N2UnitS, N2UnitS,
                                             N2UnitS, N2UnitV, N2UnitV,
                                             N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 7;
  let NumMicroOps = 15;
}

//===----------------------------------------------------------------------===//
// Define generic 18 micro-op types

def N2Write_11cyc_9L01_9V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                           N2UnitL01, N2UnitL01, N2UnitL01,
                                           N2UnitL01, N2UnitL01, N2UnitL01,
                                           N2UnitV, N2UnitV, N2UnitV,
                                           N2UnitV, N2UnitV, N2UnitV,
                                           N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 11;
  let NumMicroOps = 18;
}

//===----------------------------------------------------------------------===//
// Define generic 27 micro-op types

def N2Write_11cyc_9L01_9S_9V : SchedWriteRes<[N2UnitL01, N2UnitL01, N2UnitL01,
                                              N2UnitL01, N2UnitL01, N2UnitL01,
                                              N2UnitL01, N2UnitL01, N2UnitL01,
                                              N2UnitS, N2UnitS, N2UnitS,
                                              N2UnitS, N2UnitS, N2UnitS,
                                              N2UnitS, N2UnitS, N2UnitS,
                                              N2UnitV, N2UnitV, N2UnitV,
                                              N2UnitV, N2UnitV, N2UnitV,
                                              N2UnitV, N2UnitV, N2UnitV]> {
  let Latency     = 11;
  let NumMicroOps = 27;
}

//===----------------------------------------------------------------------===//
// Define types for arithmetic and logical ops with short shifts
def N2Write_Arith : SchedWriteVariant<[
                      SchedVar<IsCheapLSL,  [N2Write_1cyc_1I]>,
                      SchedVar<NoSchedPred, [N2Write_2cyc_1M]>]>;

def N2Write_Logical: SchedWriteVariant<[
                       SchedVar<NeoverseNoLSL, [N2Write_1cyc_1I]>,
                       SchedVar<NoSchedPred,   [N2Write_2cyc_1M]>]>;

// Miscellaneous
// -----------------------------------------------------------------------------

def : InstRW<[WriteI], (instrs COPY)>;

// Branch Instructions
// -----------------------------------------------------------------------------

// Branch, immed
// Compare and branch
def : SchedAlias<WriteBr,    N2Write_1cyc_1B>;

// Branch, register
def : SchedAlias<WriteBrReg, N2Write_1cyc_1B>;

// Branch and link, immed
// Branch and link, register
def : InstRW<[N2Write_1cyc_1B_1S], (instrs BL, BLR)>;

// Arithmetic and Logical Instructions
// -----------------------------------------------------------------------------

// ALU, basic
// ALU, basic, flagset
def : SchedAlias<WriteI,     N2Write_1cyc_1I>;

// ALU, extend and shift
def : SchedAlias<WriteIEReg, N2Write_2cyc_1M>;

// Arithmetic, LSL shift, shift <= 4
// Arithmetic, flagset, LSL shift, shift <= 4
// Arithmetic, LSR/ASR/ROR shift or LSL shift > 4
def : SchedAlias<WriteISReg, N2Write_Arith>;

// Logical, shift, no flagset
def : InstRW<[N2Write_1cyc_1I],
             (instregex "^(AND|BIC|EON|EOR|ORN|ORR)[WX]rs$")>;

// Logical, shift, flagset
def : InstRW<[N2Write_Logical], (instregex "^(AND|BIC)S[WX]rs$")>;

// Arithmetic, immediate to logical address tag
def : InstRW<[N2Write_2cyc_1M], (instrs ADDG, SUBG)>;

// Convert floating-point condition flags
// Flag manipulation instructions
def : WriteRes<WriteSys, []> { let Latency = 1; }

// Insert Random Tags
def : InstRW<[N2Write_2cyc_1M], (instrs IRG, IRGstack)>;

// Insert Tag Mask
// Subtract Pointer
// Subtract Pointer, flagset
def : InstRW<[N2Write_1cyc_1I], (instrs GMI, SUBP, SUBPS)>;

// Move and shift instructions
// -----------------------------------------------------------------------------

def : SchedAlias<WriteImm, N2Write_1cyc_1I>;

// Divide and Multiply Instructions
// -----------------------------------------------------------------------------

// SDIV, UDIV
def : SchedAlias<WriteID32,  N2Write_12cyc_1M0>;
def : SchedAlias<WriteID64,  N2Write_20cyc_1M0>;

def : WriteRes<WriteIM32, [N2UnitM]> { let Latency = 2; }
def : WriteRes<WriteIM64, [N2UnitM]> { let Latency = 2; }

// Multiply high
def : InstRW<[N2Write_3cyc_1M], (instrs SMULHrr, UMULHrr)>;

// Pointer Authentication Instructions (v8.3 PAC)
// -----------------------------------------------------------------------------

// Authenticate data address
// Authenticate instruction address
// Compute pointer authentication code for data address
// Compute pointer authentication code, using generic key
// Compute pointer authentication code for instruction address
def : InstRW<[N2Write_5cyc_1M0], (instregex "^AUT", "^PAC")>;

// Branch and link, register, with pointer authentication
// Branch, register, with pointer authentication
// Branch, return, with pointer authentication
def : InstRW<[N2Write_6cyc_1M0_1B], (instrs BLRAA, BLRAAZ, BLRAB, BLRABZ, BRAA,
                                            BRAAZ, BRAB, BRABZ, RETAA, RETAB,
                                            ERETAA, ERETAB)>;


// Load register, with pointer authentication
def : InstRW<[N2Write_9cyc_1M0_1L], (instregex "^LDRA[AB](indexed|writeback)")>;

// Strip pointer authentication code
def : InstRW<[N2Write_2cyc_1M0], (instrs XPACD, XPACI, XPACLRI)>;

// Miscellaneous data-processing instructions
// -----------------------------------------------------------------------------

// Bitfield extract, one reg
// Bitfield extract, two regs
// NOTE: We don't model the difference between EXTR where both operands are the
// same (one reg).
def : SchedAlias<WriteExtr, N2Write_3cyc_1I_1M>;
def : InstRW<[N2Write_3cyc_1I_1M], (instrs EXTRWrri, EXTRXrri)>;

// Bitfield move, basic
def : SchedAlias<WriteIS, N2Write_1cyc_1I>;

// Bitfield move, insert
def : InstRW<[N2Write_2cyc_1M], (instregex "^BFM[WX]ri$")>;

// Load instructions
// -----------------------------------------------------------------------------

def : SchedAlias<WriteLD,    N2Write_4cyc_1L>;
def : SchedAlias<WriteLDIdx, N2Write_4cyc_1I_1L>;

// Load pair, signed immed offset, signed words
def : InstRW<[N2Write_5cyc_1M0, WriteLDHi], (instrs LDPSWi)>;
// Load pair, immed post-index or immed pre-index, signed words
def : InstRW<[WriteAdr, N2Write_5cyc_1M0, WriteLDHi],
             (instregex "^LDPSW(post|pre)$")>;

// Store instructions
// -----------------------------------------------------------------------------

def : SchedAlias<WriteST,    N2Write_1cyc_1L01_1D>;
def : SchedAlias<WriteSTIdx, N2Write_1cyc_1L01_1D_1I>;
def : SchedAlias<WriteSTP,   N2Write_1cyc_1L01_1D>;
def : SchedAlias<WriteAdr,   N2Write_1cyc_1I>; // copied from A57.

// Tag load instructions
// -----------------------------------------------------------------------------

// Load allocation tag
// Load multiple allocation tags
def : InstRW<[N2Write_4cyc_1L], (instrs LDG, LDGM)>;

// Tag store instructions
// -----------------------------------------------------------------------------

// Store allocation tags to one or two granules, post-index
// Store allocation tags to one or two granules, pre-index
// Store allocation tag to one or two granules, zeroing, post-index
// Store Allocation Tag to one or two granules, zeroing, pre-index
// Store allocation tag and reg pair to memory, post-Index
// Store allocation tag and reg pair to memory, pre-Index
def : InstRW<[N2Write_1cyc_1L01_1D_1I], (instrs STGPreIndex, STGPostIndex,
                                                ST2GPreIndex, ST2GPostIndex,
                                                STZGPreIndex, STZGPostIndex,
                                                STZ2GPreIndex, STZ2GPostIndex,
                                                STGPpre, STGPpost)>;

// Store allocation tags to one or two granules, signed offset
// Store allocation tag to two granules, zeroing, signed offset
// Store allocation tag and reg pair to memory, signed offset
// Store multiple allocation tags
def : InstRW<[N2Write_1cyc_1L01_1D], (instrs STGi, ST2Gi, STZGi,
                                             STZ2Gi, STGPi, STGM, STZGM)>;

// FP data processing instructions
// -----------------------------------------------------------------------------

// FP absolute value
// FP arithmetic
// FP min/max
// FP negate
// FP select
def : SchedAlias<WriteF,     N2Write_2cyc_1V>;

// FP compare
def : SchedAlias<WriteFCmp,  N2Write_2cyc_1V0>;

// FP divide, square root
def : SchedAlias<WriteFDiv,  N2Write_7cyc_1V0>;

// FP divide, H-form
def : InstRW<[N2Write_7cyc_1V0],  (instrs FDIVHrr)>;
// FP divide, S-form
def : InstRW<[N2Write_10cyc_1V0], (instrs FDIVSrr)>;
// FP divide, D-form
def : InstRW<[N2Write_15cyc_1V0], (instrs FDIVDrr)>;

// FP square root, H-form
def : InstRW<[N2Write_7cyc_1V0],  (instrs FSQRTHr)>;
// FP square root, S-form
def : InstRW<[N2Write_9cyc_1V0],  (instrs FSQRTSr)>;
// FP square root, D-form
def : InstRW<[N2Write_16cyc_1V0], (instrs FSQRTDr)>;

// FP multiply
def : WriteRes<WriteFMul, [N2UnitV]> { let Latency = 3; }

// FP multiply accumulate
def : InstRW<[N2Write_4cyc_1V], (instregex "^FN?M(ADD|SUB)[HSD]rrr$")>;

// FP round to integral
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FRINT[AIMNPXZ][HSD]r$",
                                            "^FRINT(32|64)[XZ][SD]r$")>;

// FP miscellaneous instructions
// -----------------------------------------------------------------------------

// FP convert, from gen to vec reg
def : InstRW<[N2Write_3cyc_1M0], (instregex "^[SU]CVTF[SU][WX][HSD]ri$")>;

// FP convert, from vec to gen reg
def : InstRW<[N2Write_3cyc_1V], (instregex "^FCVT[AMNPZ][SU][SU][WX][HSD]r$")>;

// FP convert, Javascript from vec to gen reg
// FP convert, from vec to vec reg
def : SchedAlias<WriteFCvt, N2Write_3cyc_1V0>;

// FP move, immed
// FP move, register
def : SchedAlias<WriteFImm, N2Write_2cyc_1V>;

// FP transfer, from gen to low half of vec reg
def : InstRW<[N2Write_3cyc_1M0], (instrs FMOVWHr, FMOVXHr, FMOVWSr, FMOVXDr,
                                         FMOVHWr, FMOVHXr, FMOVSWr, FMOVDXr)>;

// FP transfer, from gen to high half of vec reg
def : InstRW<[N2Write_5cyc_1M0_1V], (instrs FMOVXDHighr)>;

// FP transfer, from vec to gen reg
def : SchedAlias<WriteFCopy, N2Write_2cyc_1V>;

// FP load instructions
// -----------------------------------------------------------------------------

// Load vector reg, literal, S/D/Q forms
// Load vector reg, unscaled immed
def : InstRW<[N2Write_6cyc_1L], (instregex "^LDR[SDQ]l$",
                                           "^LDUR[BHSDQ]i$")>;

// Load vector reg, immed post-index
def : InstRW<[N2Write_6cyc_1I_1L, WriteI], (instregex "^LDR[BHSDQ]post$")>;
// Load vector reg, immed pre-index
def : InstRW<[WriteAdr, N2Write_6cyc_1I_1L], (instregex "^LDR[BHSDQ]pre$")>;

// Load vector reg, unsigned immed
def : InstRW<[N2Write_6cyc_1L], (instregex "^LDR[BHSDQ]ui$")>;

// Load vector reg, register offset, basic
// Load vector reg, register offset, scale, S/D-form
// Load vector reg, register offset, extend
// Load vector reg, register offset, extend, scale, S/D-form
def : InstRW<[N2Write_6cyc_1L, ReadAdrBase], (instregex "^LDR[BSD]ro[WX]$")>;

// Load vector reg, register offset, scale, H/Q-form
// Load vector reg, register offset, extend, scale, H/Q-form
def : InstRW<[N2Write_7cyc_1I_1L, ReadAdrBase], (instregex "^LDR[HQ]ro[WX]$")>;

// Load vector pair, immed offset, S/D-form
def : InstRW<[N2Write_6cyc_1L, WriteLDHi], (instregex "^LDN?P[SD]i$")>;

// Load vector pair, immed offset, Q-form
def : InstRW<[N2Write_6cyc_2L, WriteLDHi], (instrs LDPQi, LDNPQi)>;

// Load vector pair, immed post-index, S/D-form
// Load vector pair, immed pre-index, S/D-form
def : InstRW<[WriteAdr, N2Write_6cyc_1I_1L, WriteLDHi],
             (instregex "^LDP[SD](pre|post)$")>;

// Load vector pair, immed post-index, Q-form
// Load vector pair, immed pre-index, Q-form
def : InstRW<[WriteAdr, N2Write_6cyc_2I_2L, WriteLDHi], (instrs LDPQpost,
                                                                LDPQpre)>;

// FP store instructions
// -----------------------------------------------------------------------------

// Store vector reg, unscaled immed, B/H/S/D-form
// Store vector reg, unscaled immed, Q-form
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STUR[BHSDQ]i$")>;

// Store vector reg, immed post-index, B/H/S/D-form
// Store vector reg, immed post-index, Q-form
// Store vector reg, immed pre-index, B/H/S/D-form
// Store vector reg, immed pre-index, Q-form
def : InstRW<[WriteAdr, N2Write_2cyc_1L01_1V_1I, ReadAdrBase],
             (instregex "^STR[BHSDQ](pre|post)$")>;

// Store vector reg, unsigned immed, B/H/S/D-form
// Store vector reg, unsigned immed, Q-form
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STR[BHSDQ]ui$")>;

// Store vector reg, register offset, basic, B/H/S/D-form
// Store vector reg, register offset, basic, Q-form
// Store vector reg, register offset, scale, S/D-form
// Store vector reg, register offset, extend, B/H/S/D-form
// Store vector reg, register offset, extend, Q-form
// Store vector reg, register offset, extend, scale, S/D-form
def : InstRW<[N2Write_2cyc_1L01_1V, ReadAdrBase],
             (instregex "^STR[BSD]ro[WX]$")>;

// Store vector reg, register offset, scale, H-form
// Store vector reg, register offset, scale, Q-form
// Store vector reg, register offset, extend, scale, H-form
// Store vector reg, register offset, extend, scale, Q-form
def : InstRW<[N2Write_2cyc_1L01_1V, ReadAdrBase],
             (instregex "^STR[HQ]ro[WX]$")>;

// Store vector pair, immed offset, S-form
// Store vector pair, immed offset, D-form
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STN?P[SD]i$")>;

// Store vector pair, immed offset, Q-form
def : InstRW<[N2Write_2cyc_1L01_2V], (instrs STPQi, STNPQi)>;

// Store vector pair, immed post-index, S-form
// Store vector pair, immed post-index, D-form
// Store vector pair, immed pre-index, S-form
// Store vector pair, immed pre-index, D-form
def : InstRW<[WriteAdr, N2Write_2cyc_1L01_1V_1I],
             (instregex "^STP[SD](pre|post)$")>;

// Store vector pair, immed post-index, Q-form
def : InstRW<[N2Write_2cyc_1L01_2V_1I], (instrs STPQpost)>;

// Store vector pair, immed pre-index, Q-form
def : InstRW<[N2Write_2cyc_1L01_2V_2I], (instrs STPQpre)>;

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
def : SchedAlias<WriteVd, N2Write_2cyc_1V>;
def : SchedAlias<WriteVq, N2Write_2cyc_1V>;

// ASIMD absolute diff accum
// ASIMD absolute diff accum long
def : InstRW<[N2Write_4cyc_1V1],
             (instregex "^SABAv", "^UABAv", "^SABALv", "^UABALv")>;

// ASIMD arith, reduce, 4H/4S
def : InstRW<[N2Write_2cyc_1V1], (instregex "^(ADDV|[SU]ADDLV)v4(i16|i32)v$")>;

// ASIMD arith, reduce, 8B/8H
def : InstRW<[N2Write_4cyc_1V1_1V],
             (instregex "^(ADDV|[SU]ADDLV)v8(i8|i16)v$")>;

// ASIMD arith, reduce, 16B
def : InstRW<[N2Write_4cyc_1V1], (instrs ADDVv16i8v, SADDLVv16i8v,
                                         UADDLVv16i8v)>;

// ASIMD dot product
// ASIMD dot product using signed and unsigned integers
def : InstRW<[N2Write_3cyc_1V],
             (instregex "^([SU]|SU|US)DOT(lane)?(v8|v16)i8$")>;

// ASIMD matrix multiply-accumulate
def : InstRW<[N2Write_3cyc_1V], (instrs SMMLA, UMMLA, USMMLA)>;

// ASIMD max/min, reduce, 4H/4S
def : InstRW<[N2Write_2cyc_1V1], (instregex "^[SU](MAX|MIN)Vv4i16v$",
                                            "^[SU](MAX|MIN)Vv4i32v$")>;

// ASIMD max/min, reduce, 8B/8H
def : InstRW<[N2Write_4cyc_1V1_1V], (instregex "^[SU](MAX|MIN)Vv8i8v$",
                                               "^[SU](MAX|MIN)Vv8i16v$")>;

// ASIMD max/min, reduce, 16B
def : InstRW<[N2Write_4cyc_2V1], (instregex "[SU](MAX|MIN)Vv16i8v$")>;

// ASIMD multiply
def : InstRW<[N2Write_4cyc_1V0], (instregex "^MULv", "^SQ(R)?DMULHv")>;

// ASIMD multiply accumulate
def : InstRW<[N2Write_4cyc_1V0], (instregex "^MLAv", "^MLSv")>;

// ASIMD multiply accumulate high
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQRDMLAHv", "^SQRDMLSHv")>;

// ASIMD multiply accumulate long
def : InstRW<[N2Write_4cyc_1V0], (instregex "^[SU]MLALv", "^[SU]MLSLv")>;

// ASIMD multiply accumulate saturating long
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQDMLALv", "^SQDMLSLv")>;

// ASIMD multiply/multiply long (8x8) polynomial, D-form
// ASIMD multiply/multiply long (8x8) polynomial, Q-form
def : InstRW<[N2Write_3cyc_1V0], (instregex "^PMULL?(v8i8|v16i8)$")>;

// ASIMD multiply long
def : InstRW<[N2Write_3cyc_1V], (instregex "^[SU]MULLv", "^SQDMULLv")>;

// ASIMD pairwise add and accumulate long
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]ADALPv")>;

// ASIMD shift accumulate
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]SRAv", "^[SU]RSRAv")>;

// ASIMD shift by immed, basic
def : InstRW<[N2Write_2cyc_1V1], (instregex "^SHLv", "^SHLLv", "^SHRNv",
                                            "^SSHLLv", "^SSHRv", "^USHLLv",
                                            "^USHRv")>;

// ASIMD shift by immed and insert, basic
def : InstRW<[N2Write_2cyc_1V1], (instregex "^SLIv", "^SRIv")>;

// ASIMD shift by immed, complex
def : InstRW<[N2Write_4cyc_1V1],
             (instregex "^RSHRNv", "^SQRSHRNv", "^SQRSHRUNv",
                        "^(SQSHLU?|UQSHL)[bhsd]$",
                        "^(SQSHLU?|UQSHL)(v8i8|v16i8|v4i16|v8i16|v2i32|v4i32|v2i64)_shift$",
                        "^SQSHRNv", "^SQSHRUNv", "^SRSHRv", "^UQRSHRNv",
                        "^UQSHRNv", "^URSHRv")>;

// ASIMD shift by register, basic
def : InstRW<[N2Write_2cyc_1V1], (instregex "^[SU]SHLv")>;

// ASIMD shift by register, complex
def : InstRW<[N2Write_4cyc_1V1],
             (instregex "^[SU]RSHLv", "^[SU]QRSHLv",
                        "^[SU]QSHL(v1i8|v1i16|v1i32|v1i64|v8i8|v16i8|v4i16|v8i16|v2i32|v4i32|v2i64)$")>;

// ASIMD floating-point instructions
// -----------------------------------------------------------------------------

// ASIMD FP absolute value/difference
// ASIMD FP arith, normal
// ASIMD FP compare
// ASIMD FP complex add
// ASIMD FP max/min, normal
// ASIMD FP max/min, pairwise
// ASIMD FP negate
// Handled by SchedAlias<WriteV[dq], ...>

// ASIMD FP complex multiply add
def : InstRW<[N2Write_4cyc_1V], (instregex "^FCMLAv")>;

// ASIMD FP convert, long (F16 to F32)
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FCVTL(v4|v8)i16")>;

// ASIMD FP convert, long (F32 to F64)
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FCVTL(v2|v4)i32")>;

// ASIMD FP convert, narrow (F32 to F16)
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FCVTN(v4|v8)i16")>;

// ASIMD FP convert, narrow (F64 to F32)
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FCVTN(v2|v4)i32",
                                            "^FCVTXN(v2|v4)f32")>;

// ASIMD FP convert, other, D-form F32 and Q-form F64
def : InstRW<[N2Write_3cyc_1V0], (instregex "^[FSU]CVT[AMNPZ][SU]v2f(32|64)$",
                                            "^[SU]CVTFv2f(32|64)$")>;

// ASIMD FP convert, other, D-form F16 and Q-form F32
def : InstRW<[N2Write_4cyc_2V0], (instregex "^[FSU]CVT[AMNPZ][SU]v4f(16|32)$",
                                            "^[SU]CVTFv4f(16|32)$")>;

// ASIMD FP convert, other, Q-form F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^[FSU]CVT[AMNPZ][SU]v8f16$",
                                            "^[SU]CVTFv8f16$")>;

// ASIMD FP divide, D-form, F16
def : InstRW<[N2Write_7cyc_1V0], (instrs FDIVv4f16)>;

// ASIMD FP divide, D-form, F32
def : InstRW<[N2Write_10cyc_2V0], (instrs FDIVv2f32)>;

// ASIMD FP divide, Q-form, F16
def : InstRW<[N2Write_13cyc_2V0], (instrs FDIVv8f16)>;

// ASIMD FP divide, Q-form, F32
def : InstRW<[N2Write_10cyc_2V0], (instrs FDIVv4f32)>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[N2Write_15cyc_2V0], (instrs FDIVv2f64)>;

// ASIMD FP max/min, reduce, F32 and D-form F16
def : InstRW<[N2Write_4cyc_1V], (instregex "^(FMAX|FMIN)(NM)?Vv4(i16|i32)v$")>;

// ASIMD FP max/min, reduce, Q-form F16
def : InstRW<[N2Write_6cyc_2V], (instregex "^(FMAX|FMIN)(NM)?Vv8i16v$")>;

// ASIMD FP multiply
def : InstRW<[N2Write_3cyc_1V], (instregex "^FMULv", "^FMULXv")>;

// ASIMD FP multiply accumulate
def : InstRW<[N2Write_4cyc_1V], (instregex "^FMLAv", "^FMLSv")>;

// ASIMD FP multiply accumulate long
def : InstRW<[N2Write_5cyc_1V], (instregex "^FMLALv", "^FMLSLv")>;

// ASIMD FP round, D-form F32 and Q-form F64
def : InstRW<[N2Write_3cyc_1V0],
             (instregex "^FRINT[AIMNPXZ]v2f(32|64)$",
                        "^FRINT[32|64)[XZ]v2f(32|64)$")>;

// ASIMD FP round, D-form F16 and Q-form F32
def : InstRW<[N2Write_4cyc_2V0],
             (instregex "^FRINT[AIMNPXZ]v4f(16|32)$",
                        "^FRINT(32|64)[XZ]v4f32$")>;


// ASIMD FP round, Q-form F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^FRINT[AIMNPXZ]v8f16$")>;

// ASIMD FP square root, D-form, F16
def : InstRW<[N2Write_7cyc_1V0], (instrs FSQRTv4f16)>;

// ASIMD FP square root, D-form, F32
def : InstRW<[N2Write_10cyc_2V0], (instrs FSQRTv2f32)>;

// ASIMD FP square root, Q-form, F16
def : InstRW<[N2Write_13cyc_2V0], (instrs FSQRTv8f16)>;

// ASIMD FP square root, Q-form, F32
def : InstRW<[N2Write_10cyc_2V0], (instrs FSQRTv4f32)>;

// ASIMD FP square root, Q-form, F64
def : InstRW<[N2Write_16cyc_2V0], (instrs FSQRTv2f64)>;

// ASIMD BFloat16 (BF16) instructions
// -----------------------------------------------------------------------------

// ASIMD convert, F32 to BF16
def : InstRW<[N2Write_4cyc_1V0], (instrs BFCVTN, BFCVTN2)>;

// ASIMD dot product
def : InstRW<[N2Write_4cyc_1V], (instrs BFDOTv4bf16, BFDOTv8bf16)>;

// ASIMD matrix multiply accumulate
def : InstRW<[N2Write_5cyc_1V], (instrs BFMMLA)>;

// ASIMD multiply accumulate long
def : InstRW<[N2Write_4cyc_1V], (instrs BFMLALB, BFMLALBIdx, BFMLALT,
                                        BFMLALTIdx)>;

// Scalar convert, F32 to BF16
def : InstRW<[N2Write_3cyc_1V0], (instrs BFCVT)>;

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
// Handled by SchedAlias<WriteV[dq], ...>

// ASIMD duplicate, gen reg
def : InstRW<[N2Write_3cyc_1M0], (instregex "^DUPv.+gpr")>;

// ASIMD extract narrow, saturating
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]QXTNv", "^SQXTUNv")>;

// ASIMD reciprocal and square root estimate, D-form U32
def : InstRW<[N2Write_3cyc_1V0], (instrs URECPEv2i32, URSQRTEv2i32)>;

// ASIMD reciprocal and square root estimate, Q-form U32
def : InstRW<[N2Write_4cyc_2V0], (instrs URECPEv4i32, URSQRTEv4i32)>;

// ASIMD reciprocal and square root estimate, D-form F32 and scalar forms
def : InstRW<[N2Write_3cyc_1V0], (instrs FRECPEv1f16, FRECPEv1i32,
                                         FRECPEv1i64, FRECPEv2f32,
                                         FRSQRTEv1f16, FRSQRTEv1i32,
                                         FRSQRTEv1i64, FRSQRTEv2f32)>;

// ASIMD reciprocal and square root estimate, D-form F16 and Q-form F32
def : InstRW<[N2Write_4cyc_2V0], (instrs FRECPEv4f16, FRECPEv4f32,
                                         FRSQRTEv4f16, FRSQRTEv4f32)>;

// ASIMD reciprocal and square root estimate, Q-form F16
def : InstRW<[N2Write_6cyc_4V0], (instrs FRECPEv8f16, FRSQRTEv8f16)>;

// ASIMD reciprocal exponent
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FRECPXv")>;

// ASIMD reciprocal step
def : InstRW<[N2Write_4cyc_1V], (instregex "^FRECPSv", "^FRSQRTSv")>;

// ASIMD table lookup, 3 table regs
def : InstRW<[N2Write_4cyc_2V], (instrs TBLv8i8Three, TBLv16i8Three)>;

// ASIMD table lookup, 4 table regs
def : InstRW<[N2Write_4cyc_4V], (instrs TBLv8i8Four, TBLv16i8Four)>;

// ASIMD table lookup extension, 2 table reg
def : InstRW<[N2Write_4cyc_2V], (instrs TBXv8i8Two, TBXv16i8Two)>;

// ASIMD table lookup extension, 3 table reg
def : InstRW<[N2Write_6cyc_4V], (instrs TBXv8i8Three, TBXv16i8Three)>;

// ASIMD table lookup extension, 4 table reg
def : InstRW<[N2Write_6cyc_8V], (instrs TBXv8i8Four, TBXv16i8Four)>;

// ASIMD transfer, gen reg to element
def : InstRW<[N2Write_5cyc_1M0_1V], (instregex "^INSvi(8|16|32|64)gpr$")>;

// ASIMD load instructions
// -----------------------------------------------------------------------------

// ASIMD load, 1 element, multiple, 1 reg, D-form
def : InstRW<[N2Write_6cyc_1L], (instregex "^LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_1L],
             (instregex "^LD1Onev(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, multiple, 1 reg, Q-form
def : InstRW<[N2Write_6cyc_1L], (instregex "^LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_1L],
             (instregex "^LD1Onev(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, D-form
def : InstRW<[N2Write_6cyc_2L], (instregex "^LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_2L],
             (instregex "^LD1Twov(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, Q-form
def : InstRW<[N2Write_6cyc_2L], (instregex "^LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_2L],
             (instregex "^LD1Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, D-form
def : InstRW<[N2Write_6cyc_3L], (instregex "^LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L],
             (instregex "^LD1Threev(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, Q-form
def : InstRW<[N2Write_6cyc_3L], (instregex "^LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L],
             (instregex "^LD1Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form
def : InstRW<[N2Write_7cyc_4L], (instregex "^LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_7cyc_4L],
             (instregex "^LD1Fourv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, Q-form
def : InstRW<[N2Write_7cyc_4L], (instregex "^LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_7cyc_4L],
             (instregex "^LD1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, one lane, B/H/S
// ASIMD load, 1 element, one lane, D
def : InstRW<[N2Write_8cyc_1L_1V],           (instregex "LD1i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_1V], (instregex "LD1i(8|16|32|64)_POST$")>;

// ASIMD load, 1 element, all lanes, D-form, B/H/S
// ASIMD load, 1 element, all lanes, D-form, D
def : InstRW<[N2Write_8cyc_1L_1V],           (instregex "LD1Rv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_1V], (instregex "LD1Rv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, all lanes, Q-form
def : InstRW<[N2Write_8cyc_1L_1V],           (instregex "LD1Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_1V], (instregex "LD1Rv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_8cyc_1L_2V],           (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_2V], (instregex "LD2Twov(8b|4h|2s)_POST$")>;

// ASIMD load, 2 element, multiple, Q-form, B/H/S
// ASIMD load, 2 element, multiple, Q-form, D
def : InstRW<[N2Write_8cyc_2L_2V],           (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_2L_2V], (instregex "LD2Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, one lane, B/H
// ASIMD load, 2 element, one lane, S
// ASIMD load, 2 element, one lane, D
def : InstRW<[N2Write_8cyc_1L_2V],           (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_2V], (instregex "LD2i(8|16|32|64)_POST$")>;

// ASIMD load, 2 element, all lanes, D-form, B/H/S
// ASIMD load, 2 element, all lanes, D-form, D
def : InstRW<[N2Write_8cyc_1L_2V],            (instregex "LD2Rv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_2V],  (instregex "LD2Rv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 2 element, all lanes, Q-form
def : InstRW<[N2Write_8cyc_1L_2V],           (instregex "LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_1L_2V], (instregex "LD2Rv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_8cyc_2L_3V],           (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_2L_3V], (instregex "LD3Threev(8b|4h|2s)_POST$")>;

// ASIMD load, 3 element, multiple, Q-form, B/H/S
def : InstRW<[N2Write_8cyc_3L_3V],           (instregex "LD3Threev(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_3V], (instregex "LD3Threev(16b|8h|4s)_POST$")>;

// ASIMD load, 3 element, multiple, Q-form, D
def : InstRW<[N2Write_8cyc_3L_3V],           (instregex "LD3Threev(2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_3V], (instregex "LD3Threev(2d)_POST$")>;

// ASIMD load, 3 element, one lane, B/H
// ASIMD load, 3 element, one lane, S
// ASIMD load, 3 element, one lane, D
def : InstRW<[N2Write_8cyc_2L_3V],           (instregex "LD3i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_2L_3V], (instregex "LD3i(8|16|32|64)_POST$")>;

// ASIMD load, 3 element, all lanes, D-form, B/H/S
// ASIMD load, 3 element, all lanes, D-form, D
def : InstRW<[N2Write_8cyc_2L_3V],           (instregex "LD3Rv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_2L_3V], (instregex "LD3Rv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 3 element, all lanes, Q-form, B/H/S
// ASIMD load, 3 element, all lanes, Q-form, D
def : InstRW<[N2Write_8cyc_3L_3V],           (instregex "LD3Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_3V], (instregex "LD3Rv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_8cyc_3L_4V],           (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_4V], (instregex "LD4Fourv(8b|4h|2s)_POST$")>;

// ASIMD load, 4 element, multiple, Q-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, D
def : InstRW<[N2Write_9cyc_4L_4V],           (instregex "LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_9cyc_4L_4V], (instregex "LD4Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, one lane, B/H
// ASIMD load, 4 element, one lane, S
// ASIMD load, 4 element, one lane, D
def : InstRW<[N2Write_8cyc_3L_4V],           (instregex "LD4i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_4V], (instregex "LD4i(8|16|32|64)_POST$")>;

// ASIMD load, 4 element, all lanes, D-form, B/H/S
// ASIMD load, 4 element, all lanes, D-form, D
def : InstRW<[N2Write_8cyc_3L_4V],              (instregex "LD4Rv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_3L_4V],    (instregex "LD4Rv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 4 element, all lanes, Q-form, B/H/S
// ASIMD load, 4 element, all lanes, Q-form, D
def : InstRW<[N2Write_8cyc_4L_4V],            (instregex "LD4Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_8cyc_4L_4V],  (instregex "LD4Rv(16b|8h|4s|2d)_POST$")>;

// ASIMD store instructions
// -----------------------------------------------------------------------------

// ASIMD store, 1 element, multiple, 1 reg, D-form
def : InstRW<[N2Write_2cyc_1L01_1V],           (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_1L01_1V], (instregex "ST1Onev(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 1 reg, Q-form
def : InstRW<[N2Write_2cyc_1L01_1V],           (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_1L01_1V], (instregex "ST1Onev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form
def : InstRW<[N2Write_2cyc_1L01_1V],           (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_1L01_1V], (instregex "ST1Twov(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, Q-form
def : InstRW<[N2Write_2cyc_2L01_2V],           (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_2L01_2V], (instregex "ST1Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form
def : InstRW<[N2Write_2cyc_2L01_2V],           (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_2L01_2V], (instregex "ST1Threev(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[N2Write_2cyc_3L01_3V],           (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_3L01_3V], (instregex "ST1Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form
def : InstRW<[N2Write_2cyc_2L01_2V],           (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_2L01_2V], (instregex "ST1Fourv(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[N2Write_2cyc_4L01_4V],           (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_2cyc_4L01_4V], (instregex "ST1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane, B/H/S
// ASIMD store, 1 element, one lane, D
def : InstRW<[N2Write_4cyc_1L01_1V],           (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_4cyc_1L01_1V], (instregex "ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_4cyc_1L01_1V],           (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_4cyc_1L01_1V], (instregex "ST2Twov(8b|4h|2s)_POST$")>;

// ASIMD store, 2 element, multiple, Q-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, D
def : InstRW<[N2Write_4cyc_2L01_2V],           (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_4cyc_2L01_2V], (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane, B/H/S
// ASIMD store, 2 element, one lane, D
def : InstRW<[N2Write_4cyc_1L01_1V],           (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_4cyc_1L01_1V], (instregex "ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_5cyc_2L01_2V],           (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_5cyc_2L01_2V], (instregex "ST3Threev(8b|4h|2s)_POST$")>;

// ASIMD store, 3 element, multiple, Q-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, D
def : InstRW<[N2Write_6cyc_3L01_3V],           (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L01_3V], (instregex "ST3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H
// ASIMD store, 3 element, one lane, S
// ASIMD store, 3 element, one lane, D
def : InstRW<[N2Write_6cyc_3L01_3V],           (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L01_3V], (instregex "ST3i(8|16|32|64)_POST$")>;

// ASIMD store, 4 element, multiple, D-form, B/H/S
def : InstRW<[N2Write_6cyc_3L01_3V],           (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L01_3V], (instregex "ST4Fourv(8b|4h|2s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, B/H/S
def : InstRW<[N2Write_7cyc_6L01_6V],           (instregex "ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, N2Write_7cyc_6L01_6V], (instregex "ST4Fourv(16b|8h|4s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[N2Write_5cyc_4L01_4V],           (instregex "ST4Fourv(2d)$")>;
def : InstRW<[WriteAdr, N2Write_5cyc_4L01_4V], (instregex "ST4Fourv(2d)_POST$")>;

// ASIMD store, 4 element, one lane, B/H/S
def : InstRW<[N2Write_6cyc_3L01_3V],           (instregex "ST4i(8|16|32)$")>;
def : InstRW<[WriteAdr, N2Write_6cyc_3L01_3V], (instregex "ST4i(8|16|32)_POST$")>;

// ASIMD store, 4 element, one lane, D
def : InstRW<[N2Write_4cyc_3L01_3V],            (instregex "ST4i(64)$")>;
def : InstRW<[WriteAdr, N2Write_4cyc_3L01_3V],  (instregex "ST4i(64)_POST$")>;

// Cryptography extensions
// -----------------------------------------------------------------------------

// Crypto AES ops
def : InstRW<[N2Write_2cyc_1V], (instregex "^AES[DE]rr$", "^AESI?MCrr")>;

// Crypto polynomial (64x64) multiply long
def : InstRW<[N2Write_2cyc_1V0], (instrs PMULLv1i64, PMULLv2i64)>;

// Crypto SHA1 hash acceleration op
// Crypto SHA1 schedule acceleration ops
def : InstRW<[N2Write_2cyc_1V0], (instregex "^SHA1(H|SU0|SU1)")>;

// Crypto SHA1 hash acceleration ops
// Crypto SHA256 hash acceleration ops
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SHA1[CMP]", "^SHA256H2?")>;

// Crypto SHA256 schedule acceleration ops
def : InstRW<[N2Write_2cyc_1V0], (instregex "^SHA256SU[01]")>;

// Crypto SHA512 hash acceleration ops
def : InstRW<[N2Write_2cyc_1V0], (instregex "^SHA512(H|H2|SU0|SU1)")>;

// Crypto SHA3 ops
def : InstRW<[N2Write_2cyc_1V0], (instrs BCAX, EOR3, RAX1, XAR)>;

// Crypto SM3 ops
def : InstRW<[N2Write_2cyc_1V0], (instregex "^SM3PARTW[12]$", "^SM3SS1$",
                                            "^SM3TT[12][AB]$")>;

// Crypto SM4 ops
def : InstRW<[N2Write_4cyc_1V0], (instrs SM4E, SM4ENCKEY)>;

// CRC
// -----------------------------------------------------------------------------

def : InstRW<[N2Write_2cyc_1M0], (instregex "^CRC32")>;

// SVE Predicate instructions
// -----------------------------------------------------------------------------

// Loop control, based on predicate
def : InstRW<[N2Write_2cyc_1M], (instrs BRKA_PPmP, BRKA_PPzP,
                                        BRKB_PPmP, BRKB_PPzP)>;

// Loop control, based on predicate and flag setting
def : InstRW<[N2Write_3cyc_1M], (instrs BRKAS_PPzP, BRKBS_PPzP)>;

// Loop control, propagating
def : InstRW<[N2Write_2cyc_1M0], (instrs BRKN_PPzP, BRKPA_PPzPP, BRKPB_PPzPP)>;

// Loop control, propagating and flag setting
def : InstRW<[N2Write_3cyc_1M0_1M], (instrs BRKNS_PPzP, BRKPAS_PPzPP,
                                            BRKPBS_PPzPP)>;

// Loop control, based on GPR
def : InstRW<[N2Write_3cyc_1M],
             (instregex "^WHILE(GE|GT|HI|HS|LE|LO|LS|LT)_P(WW|XX)_[BHSD]$")>;

def : InstRW<[N2Write_3cyc_1M], (instregex "^WHILE(RW|WR)_PXX_[BHSD]$")>;

// Loop terminate
def : InstRW<[N2Write_1cyc_1M], (instregex "^CTERM(EQ|NE)_(WW|XX)$")>;

// Predicate counting scalar
def : InstRW<[N2Write_2cyc_1M], (instrs ADDPL_XXI, ADDVL_XXI, RDVLI_XI)>;
def : InstRW<[N2Write_2cyc_1M],
             (instregex "^(CNT|DEC|INC|SQDEC|SQINC|UQDEC|UQINC)[BHWD]_XPiI$",
                        "^SQ(DEC|INC)[BHWD]_XPiWdI$",
                        "^(UQDEC|UQINC)[BHWD]_WPiI$")>;

// Predicate counting scalar, active predicate
def : InstRW<[N2Write_2cyc_1M],
             (instregex "^CNTP_XPP_[BHSD]$",
                        "^(DEC|INC|SQDEC|SQINC|UQDEC|UQINC)P_XP_[BHSD]$",
                        "^(UQDEC|UQINC)P_WP_[BHSD]$",
                        "^(SQDEC|SQINC|UQDEC|UQINC)P_XPWd_[BHSD]$")>;

// Predicate counting vector, active predicate
def : InstRW<[N2Write_7cyc_1M_1M0_1V],
             (instregex "^(DEC|INC|SQDEC|SQINC|UQDEC|UQINC)P_ZP_[HSD]$")>;

// Predicate logical
def : InstRW<[N2Write_1cyc_1M0],
             (instregex "^(AND|BIC|EOR|NAND|NOR|ORN|ORR)_PPzPP$")>;

// Predicate logical, flag setting
def : InstRW<[N2Write_2cyc_1M0_1M],
             (instregex "^(ANDS|BICS|EORS|NANDS|NORS|ORNS|ORRS)_PPzPP$")>;

// Predicate reverse
def : InstRW<[N2Write_2cyc_1M], (instregex "^REV_PP_[BHSD]$")>;

// Predicate select
def : InstRW<[N2Write_1cyc_1M0], (instrs SEL_PPPP)>;

// Predicate set
def : InstRW<[N2Write_2cyc_1M], (instregex "^PFALSE$", "^PTRUE_[BHSD]$")>;

// Predicate set/initialize, set flags
def : InstRW<[N2Write_3cyc_1M], (instregex "^PTRUES_[BHSD]$")>;

// Predicate find first/next
def : InstRW<[N2Write_3cyc_1M], (instregex "^PFIRST_B$", "^PNEXT_[BHSD]$")>;

// Predicate test
def : InstRW<[N2Write_1cyc_1M], (instrs PTEST_PP)>;

// Predicate transpose
def : InstRW<[N2Write_2cyc_1M], (instregex "^TRN[12]_PPP_[BHSDQ]$")>;

// Predicate unpack and widen
def : InstRW<[N2Write_2cyc_1M], (instrs PUNPKHI_PP, PUNPKLO_PP)>;

// Predicate zip/unzip
def : InstRW<[N2Write_2cyc_1M], (instregex "^(ZIP|UZP)[12]_PPP_[BHSDQ]$")>;

// SVE integer instructions
// -----------------------------------------------------------------------------

// Arithmetic, absolute diff
def : InstRW<[N2Write_2cyc_1V], (instregex "^[SU]ABD_ZPmZ_[BHSD]",
                                           "^[SU]ABD_ZPZZ_[BHSD]")>;

// Arithmetic, absolute diff accum
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]ABA_ZZZ_[BHSD]$")>;

// Arithmetic, absolute diff accum long
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]ABAL[TB]_ZZZ_[HSD]$")>;

// Arithmetic, absolute diff long
def : InstRW<[N2Write_2cyc_1V], (instregex "^[SU]ABDL[TB]_ZZZ_[HSD]$")>;

// Arithmetic, basic
def : InstRW<[N2Write_2cyc_1V],
             (instregex "^(ABS|ADD|CNOT|NEG|SUB|SUBR)_ZPmZ_[BHSD]",
                        "^(ADD|SUB)_ZZZ_[BHSD]",
                        "^(ADD|SUB|SUBR)_ZPZZ_[BHSD]",
                        "^(ADD|SUB|SUBR)_ZI_[BHSD]",
                        "^ADR_[SU]XTW_ZZZ_D_[0123]",
                        "^ADR_LSL_ZZZ_[SD]_[0123]",
                        "^[SU](ADD|SUB)[LW][BT]_ZZZ_[HSD]",
                        "^SADDLBT_ZZZ_[HSD]",
                        "^[SU]H(ADD|SUB|SUBR)_ZPmZ_[BHSD]",
                        "^SSUBL(BT|TB)_ZZZ_[HSD]")>;

// Arithmetic, complex
def : InstRW<[N2Write_2cyc_1V],
             (instregex "^R?(ADD|SUB)HN[BT]_ZZZ_[BHS]",
                        "^SQ(ABS|ADD|NEG|SUB|SUBR)_ZPmZ_[BHSD]",
                        "^[SU]Q(ADD|SUB)_ZZZ_[BHSD]",
                        "^[SU]Q(ADD|SUB)_ZI_[BHSD]",
                        "^(SRH|SUQ|UQ|USQ|URH)ADD_ZPmZ_[BHSD]",
                        "^(UQSUB|UQSUBR)_ZPmZ_[BHSD]")>;

// Arithmetic, large integer
def : InstRW<[N2Write_2cyc_1V], (instregex "^(AD|SB)CL[BT]_ZZZ_[SD]$")>;

// Arithmetic, pairwise add
def : InstRW<[N2Write_2cyc_1V], (instregex "^ADDP_ZPmZ_[BHSD]$")>;

// Arithmetic, pairwise add and accum long
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]ADALP_ZPmZ_[HSD]$")>;

// Arithmetic, shift
def : InstRW<[N2Write_2cyc_1V1],
             (instregex "^(ASR|LSL|LSR)_WIDE_ZPmZ_[BHS]",
                        "^(ASR|LSL|LSR)_WIDE_ZZZ_[BHS]",
                        "^(ASR|LSL|LSR)_ZPmI_[BHSD]",
                        "^(ASR|LSL|LSR)_ZPmZ_[BHSD]",
                        "^(ASR|LSL|LSR)_ZZI_[BHSD]",
                        "^(ASR|LSL|LSR)_ZPZ[IZ]_[BHSD]",
                        "^(ASRR|LSLR|LSRR)_ZPmZ_[BHSD]")>;

// Arithmetic, shift and accumulate
def : InstRW<[N2Write_4cyc_1V1],
             (instregex "^(SRSRA|SSRA|URSRA|USRA)_ZZI_[BHSD]$")>;

// Arithmetic, shift by immediate
// Arithmetic, shift by immediate and insert
def : InstRW<[N2Write_2cyc_1V1],
             (instregex "^(SHRNB|SHRNT|SSHLLB|SSHLLT|USHLLB|USHLLT|SLI|SRI)_ZZI_[BHSD]$")>;

// Arithmetic, shift complex
def : InstRW<[N2Write_4cyc_1V1],
             (instregex "^(SQ)?RSHRU?N[BT]_ZZI_[BHS]",
                        "^(SQRSHL|SQRSHLR|SQSHL|SQSHLR|UQRSHL|UQRSHLR|UQSHL|UQSHLR)_ZPmZ_[BHSD]",
                        "^[SU]QR?SHL_ZPZZ_[BHSD]",
                        "^(SQSHL|SQSHLU|UQSHL)_(ZPmI|ZPZI)_[BHSD]",
                        "^SQSHRU?N[BT]_ZZI_[BHS]",
                        "^UQR?SHRN[BT]_ZZI_[BHS]")>;

// Arithmetic, shift right for divide
def : InstRW<[N2Write_4cyc_1V1], (instregex "^ASRD_(ZPmI|ZPZI)_[BHSD]")>;

// Arithmetic, shift rounding
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]RSHLR?_ZPmZ_[BHSD]",
                                             "^[SU]RSHL_ZPZZ_[BHSD]",
                                             "^[SU]RSHR_(ZPmI|ZPZI)_[BHSD]")>;

// Bit manipulation
def : InstRW<[N2Write_6cyc_2V1], (instregex "^(BDEP|BEXT|BGRP)_ZZZ_[BHSD]")>;

// Bitwise select
def : InstRW<[N2Write_2cyc_1V], (instregex "^(BSL|BSL1N|BSL2N|NBSL)_ZZZZ$")>;

// Count/reverse bits
def : InstRW<[N2Write_2cyc_1V], (instregex "^(CLS|CLZ|CNT|RBIT)_ZPmZ_[BHSD]")>;

// Broadcast logical bitmask immediate to vector
def : InstRW<[N2Write_2cyc_1V], (instrs DUPM_ZI)>;

// Compare and set flags
def : InstRW<[N2Write_4cyc_1V0_1M],
             (instregex "^CMP(EQ|GE|GT|HI|HS|LE|LO|LS|LT|NE)_PPzZ[IZ]_[BHSD]$",
                        "^CMP(EQ|GE|GT|HI|HS|LE|LO|LS|LT|NE)_WIDE_PPzZZ_[BHS]$")>;

// Complex add
def : InstRW<[N2Write_2cyc_1V], (instregex "^(SQ)?CADD_ZZI_[BHSD]$")>;

// Complex dot product 8-bit element
def : InstRW<[N2Write_3cyc_1V], (instrs CDOT_ZZZ_S, CDOT_ZZZI_S)>;

// Complex dot product 16-bit element
def : InstRW<[N2Write_4cyc_1V0], (instrs CDOT_ZZZ_D, CDOT_ZZZI_D)>;

// Complex multiply-add B, H, S element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^CMLA_ZZZ_[BHS]$",
                                            "^CMLA_ZZZI_[HS]$")>;

// Complex multiply-add D element size
def : InstRW<[N2Write_5cyc_2V0], (instrs CMLA_ZZZ_D)>;

// Conditional extract operations, scalar form
def : InstRW<[N2Write_8cyc_1M0_1V1_1V], (instregex "^CLAST[AB]_RPZ_[BHSD]$")>;

// Conditional extract operations, SIMD&FP scalar and vector forms
def : InstRW<[N2Write_3cyc_1V1], (instregex "^CLAST[AB]_[VZ]PZ_[BHSD]$",
                                            "^COMPACT_ZPZ_[SD]$",
                                            "^SPLICE_ZPZZ?_[BHSD]$")>;

// Convert to floating point, 64b to float or convert to double
def : InstRW<[N2Write_3cyc_1V0], (instregex "^[SU]CVTF_ZPmZ_Dto[HSD]",
                                            "^[SU]CVTF_ZPmZ_StoD")>;

// Convert to floating point, 32b to single or half
def : InstRW<[N2Write_4cyc_2V0], (instregex "^[SU]CVTF_ZPmZ_Sto[HS]")>;

// Convert to floating point, 16b to half
def : InstRW<[N2Write_6cyc_4V0], (instregex "^[SU]CVTF_ZPmZ_HtoH")>;

// Copy, scalar
def : InstRW<[N2Write_5cyc_1M0_1V], (instregex "^CPY_ZPmR_[BHSD]$")>;

// Copy, scalar SIMD&FP or imm
def : InstRW<[N2Write_2cyc_1V], (instregex "^CPY_ZPm[IV]_[BHSD]$",
                                           "^CPY_ZPzI_[BHSD]$")>;

// Divides, 32 bit
def : InstRW<[N2Write_12cyc_1V0], (instregex "^[SU]DIVR?_ZPmZ_S",
                                             "^[SU]DIV_ZPZZ_S")>;

// Divides, 64 bit
def : InstRW<[N2Write_20cyc_1V0], (instregex "^[SU]DIVR?_ZPmZ_D",
                                             "^[SU]DIV_ZPZZ_D")>;

// Dot product, 8 bit
def : InstRW<[N2Write_3cyc_1V], (instregex "^[SU]DOT_ZZZI?_S$")>;

// Dot product, 8 bit, using signed and unsigned integers
def : InstRW<[N2Write_3cyc_1V], (instrs SUDOT_ZZZI, USDOT_ZZZI, USDOT_ZZZ)>;

// Dot product, 16 bit
def : InstRW<[N2Write_4cyc_1V0], (instregex "^[SU]DOT_ZZZI?_D$")>;

// Duplicate, immediate and indexed form
def : InstRW<[N2Write_2cyc_1V], (instregex "^DUP_ZI_[BHSD]$",
                                           "^DUP_ZZI_[BHSDQ]$")>;

// Duplicate, scalar form
def : InstRW<[N2Write_3cyc_1M0], (instregex "^DUP_ZR_[BHSD]$")>;

// Extend, sign or zero
def : InstRW<[N2Write_2cyc_1V1], (instregex "^[SU]XTB_ZPmZ_[HSD]",
                                            "^[SU]XTH_ZPmZ_[SD]",
                                            "^[SU]XTW_ZPmZ_[D]")>;

// Extract
def : InstRW<[N2Write_2cyc_1V], (instrs EXT_ZZI, EXT_ZZI_B)>;

// Extract narrow saturating
def : InstRW<[N2Write_4cyc_1V1], (instregex "^[SU]QXTN[BT]_ZZ_[BHS]$",
                                            "^SQXTUN[BT]_ZZ_[BHS]$")>;

// Extract/insert operation, SIMD and FP scalar form
def : InstRW<[N2Write_3cyc_1V1], (instregex "^LAST[AB]_VPZ_[BHSD]$",
                                            "^INSR_ZV_[BHSD]$")>;

// Extract/insert operation, scalar
def : InstRW<[N2Write_5cyc_1V1_1M0], (instregex "^LAST[AB]_RPZ_[BHSD]$",
                                                "^INSR_ZR_[BHSD]$")>;

// Histogram operations
def : InstRW<[N2Write_2cyc_1V], (instregex "^HISTCNT_ZPzZZ_[SD]$",
                                           "^HISTSEG_ZZZ$")>;

// Horizontal operations, B, H, S form, immediate operands only
def : InstRW<[N2Write_4cyc_1V0], (instregex "^INDEX_II_[BHS]$")>;

// Horizontal operations, B, H, S form, scalar, immediate operands/ scalar
// operands only / immediate, scalar operands
def : InstRW<[N2Write_7cyc_1M0_1V0], (instregex "^INDEX_(IR|RI|RR)_[BHS]$")>;

// Horizontal operations, D form, immediate operands only
def : InstRW<[N2Write_5cyc_2V0], (instrs INDEX_II_D)>;

// Horizontal operations, D form, scalar, immediate operands)/ scalar operands
// only / immediate, scalar operands
def : InstRW<[N2Write_8cyc_2M0_2V0], (instregex "^INDEX_(IR|RI|RR)_D$")>;

// Logical
def : InstRW<[N2Write_2cyc_1V],
             (instregex "^(AND|EOR|ORR)_ZI",
                        "^(AND|BIC|EOR|ORR)_ZZZ",
                        "^EOR(BT|TB)_ZZZ_[BHSD]",
                        "^(AND|BIC|EOR|NOT|ORR)_(ZPmZ|ZPZZ)_[BHSD]",
                        "^NOT_ZPmZ_[BHSD]")>;

// Max/min, basic and pairwise
def : InstRW<[N2Write_2cyc_1V], (instregex "^[SU](MAX|MIN)_ZI_[BHSD]",
                                           "^[SU](MAX|MIN)P?_ZPmZ_[BHSD]",
                                           "^[SU](MAX|MIN)_ZPZZ_[BHSD]")>;

// Matching operations
def : InstRW<[N2Write_2cyc_1V0_1M], (instregex "^N?MATCH_PPzZZ_[BH]$")>;

// Matrix multiply-accumulate
def : InstRW<[N2Write_3cyc_1V], (instrs SMMLA_ZZZ, UMMLA_ZZZ, USMMLA_ZZZ)>;

// Move prefix
def : InstRW<[N2Write_2cyc_1V], (instregex "^MOVPRFX_ZP[mz]Z_[BHSD]$",
                                           "^MOVPRFX_ZZ$")>;

// Multiply, B, H, S element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^MUL_(ZI|ZPmZ|ZZZI|ZZZ)_[BHS]",
                                             "^MUL_ZPZZ_[BHS]",
                                             "^[SU]MULH_(ZPmZ|ZZZ)_[BHS]",
                                             "^[SU]MULH_ZPZZ_[BHS]")>;

// Multiply, D element size
def : InstRW<[N2Write_5cyc_2V0], (instregex "^MUL_(ZI|ZPmZ|ZZZI|ZZZ)_D",
                                             "^MUL_ZPZZ_D",
                                             "^[SU]MULH_(ZPmZ|ZZZ)_D",
                                             "^[SU]MULH_ZPZZ_D")>;

// Multiply long
def : InstRW<[N2Write_4cyc_1V0], (instregex "^[SU]MULL[BT]_ZZZI_[SD]$",
                                            "^[SU]MULL[BT]_ZZZ_[HSD]$")>;

// Multiply accumulate, B, H, S element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^ML[AS]_ZZZI_[BHS]$",
                                            "^(ML[AS]|MAD|MSB)_(ZPmZZ|ZPZZZ)_[BHS]")>;

// Multiply accumulate, D element size
def : InstRW<[N2Write_5cyc_2V0], (instregex "^ML[AS]_ZZZI_D$",
                                            "^(ML[AS]|MAD|MSB)_(ZPmZZ|ZPZZZ)_D")>;

// Multiply accumulate long
def : InstRW<[N2Write_4cyc_1V0], (instregex "^[SU]ML[AS]L[BT]_ZZZ_[HSD]$",
                                            "^[SU]ML[AS]L[BT]_ZZZI_[SD]$")>;

// Multiply accumulate saturating doubling long regular
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQDML[AS](LB|LT|LBT)_ZZZ_[HSD]$",
                                            "^SQDML[AS](LB|LT)_ZZZI_[SD]$")>;

// Multiply saturating doubling high, B, H, S element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQDMULH_ZZZ_[BHS]$",
                                            "^SQDMULH_ZZZI_[HS]$")>;

// Multiply saturating doubling high, D element size
def : InstRW<[N2Write_5cyc_2V0], (instrs SQDMULH_ZZZ_D, SQDMULH_ZZZI_D)>;

// Multiply saturating doubling long
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQDMULL[BT]_ZZZ_[HSD]$",
                                            "^SQDMULL[BT]_ZZZI_[SD]$")>;

// Multiply saturating rounding doubling regular/complex accumulate, B, H, S
// element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQRDML[AS]H_ZZZ_[BHS]$",
                                            "^SQRDCMLAH_ZZZ_[BHS]$",
                                            "^SQRDML[AS]H_ZZZI_[HS]$",
                                            "^SQRDCMLAH_ZZZI_[HS]$")>;

// Multiply saturating rounding doubling regular/complex accumulate, D element
// size
def : InstRW<[N2Write_5cyc_2V0], (instregex "^SQRDML[AS]H_ZZZI?_D$",
                                            "^SQRDCMLAH_ZZZ_D$")>;

// Multiply saturating rounding doubling regular/complex, B, H, S element size
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SQRDMULH_ZZZ_[BHS]$",
                                            "^SQRDMULH_ZZZI_[HS]$")>;

// Multiply saturating rounding doubling regular/complex, D element size
def : InstRW<[N2Write_5cyc_2V0], (instregex "^SQRDMULH_ZZZI?_D$")>;

// Multiply/multiply long, (8x8) polynomial
def : InstRW<[N2Write_2cyc_1V0], (instregex "^PMUL_ZZZ_B$",
                                            "^PMULL[BT]_ZZZ_[HDQ]$")>;

// Predicate counting vector
def : InstRW<[N2Write_2cyc_1V0],
             (instregex "^(DEC|INC|SQDEC|SQINC|UQDEC|UQINC)[HWD]_ZPiI$")>;

// Reciprocal estimate
def : InstRW<[N2Write_4cyc_2V0], (instregex "^URECPE_ZPmZ_S", "^URSQRTE_ZPmZ_S")>;

// Reduction, arithmetic, B form
def : InstRW<[N2Write_11cyc_2V_2V1], (instregex "^[SU](ADD|MAX|MIN)V_VPZ_B")>;

// Reduction, arithmetic, H form
def : InstRW<[N2Write_9cyc_2V_2V1], (instregex "^[SU](ADD|MAX|MIN)V_VPZ_H")>;

// Reduction, arithmetic, S form
def : InstRW<[N2Write_8cyc_2V_2V1], (instregex "^[SU](ADD|MAX|MIN)V_VPZ_S")>;

// Reduction, arithmetic, D form
def : InstRW<[N2Write_8cyc_2V_2V1], (instregex "^[SU](ADD|MAX|MIN)V_VPZ_D")>;

// Reduction, logical
def : InstRW<[N2Write_6cyc_1V_1V1], (instregex "^(ANDV|EORV|ORV)_VPZ_[BHSD]$")>;

// Reverse, vector
def : InstRW<[N2Write_2cyc_1V], (instregex "^REV_ZZ_[BHSD]$",
                                           "^REVB_ZPmZ_[HSD]$",
                                           "^REVH_ZPmZ_[SD]$",
                                           "^REVW_ZPmZ_D$")>;

// Select, vector form
def : InstRW<[N2Write_2cyc_1V], (instregex "^SEL_ZPZZ_[BHSD]$")>;

// Table lookup
def : InstRW<[N2Write_2cyc_1V], (instregex "^TBL_ZZZZ?_[BHSD]$")>;

// Table lookup extension
def : InstRW<[N2Write_2cyc_1V], (instregex "^TBX_ZZZ_[BHSD]$")>;

// Transpose, vector form
def : InstRW<[N2Write_2cyc_1V], (instregex "^TRN[12]_ZZZ_[BHSDQ]$")>;

// Unpack and extend
def : InstRW<[N2Write_2cyc_1V], (instregex "^[SU]UNPK(HI|LO)_ZZ_[HSD]$")>;

// Zip/unzip
def : InstRW<[N2Write_2cyc_1V], (instregex "^(UZP|ZIP)[12]_ZZZ_[BHSDQ]$")>;

// SVE floating-point instructions
// -----------------------------------------------------------------------------

// Floating point absolute value/difference
def : InstRW<[N2Write_2cyc_1V], (instregex "^FAB[SD]_ZPmZ_[HSD]",
                                           "^FABD_ZPZZ_[HSD]",
                                           "^FABS_ZPmZ_[HSD]")>;

// Floating point arithmetic
def : InstRW<[N2Write_2cyc_1V], (instregex "^F(ADD|SUB)_(ZPm[IZ]|ZZZ)_[HSD]",
                                           "^F(ADD|SUB)_ZPZ[IZ]_[HSD]",
                                           "^FADDP_ZPmZZ_[HSD]",
                                           "^FNEG_ZPmZ_[HSD]",
                                           "^FSUBR_ZPm[IZ]_[HSD]",
                                           "^FSUBR_(ZPZI|ZPZZ)_[HSD]")>;

// Floating point associative add, F16
def : InstRW<[N2Write_10cyc_1V1], (instrs FADDA_VPZ_H)>;

// Floating point associative add, F32
def : InstRW<[N2Write_6cyc_1V1], (instrs FADDA_VPZ_S)>;

// Floating point associative add, F64
def : InstRW<[N2Write_4cyc_1V], (instrs FADDA_VPZ_D)>;

// Floating point compare
def : InstRW<[N2Write_2cyc_1V0], (instregex "^FACG[ET]_PPzZZ_[HSD]$",
                                            "^FCM(EQ|GE|GT|NE)_PPzZ[0Z]_[HSD]$",
                                            "^FCM(LE|LT)_PPzZ0_[HSD]$",
                                            "^FCMUO_PPzZZ_[HSD]$")>;

// Floating point complex add
def : InstRW<[N2Write_3cyc_1V], (instregex "^FCADD_ZPmZ_[HSD]$")>;

// Floating point complex multiply add
def : InstRW<[N2Write_5cyc_1V], (instregex "^FCMLA_ZPmZZ_[HSD]$",
                                           "^FCMLA_ZZZI_[HS]$")>;

// Floating point convert, long or narrow (F16 to F32 or F32 to F16)
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FCVT_ZPmZ_(HtoS|StoH)",
                                            "^FCVTLT_ZPmZ_HtoS",
                                            "^FCVTNT_ZPmZ_StoH")>;

// Floating point convert, long or narrow (F16 to F64, F32 to F64, F64 to F32
// or F64 to F16)
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FCVT_ZPmZ_(HtoD|StoD|DtoS|DtoH)",
                                            "^FCVTLT_ZPmZ_StoD",
                                            "^FCVTNT_ZPmZ_DtoS")>;

// Floating point convert, round to odd
def : InstRW<[N2Write_3cyc_1V0], (instrs FCVTX_ZPmZ_DtoS, FCVTXNT_ZPmZ_DtoS)>;

// Floating point base2 log, F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^FLOGB_(ZPmZ|ZPZZ)_H")>;

// Floating point base2 log, F32
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FLOGB_(ZPmZ|ZPZZ)_S")>;

// Floating point base2 log, F64
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FLOGB_(ZPmZ|ZPZZ)_D")>;

// Floating point convert to integer, F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^FCVTZ[SU]_ZPmZ_HtoH")>;

// Floating point convert to integer, F32
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FCVTZ[SU]_ZPmZ_(HtoS|StoS)")>;

// Floating point convert to integer, F64
def : InstRW<[N2Write_3cyc_1V0],
             (instregex "^FCVTZ[SU]_ZPmZ_(HtoD|StoD|DtoS|DtoD)")>;

// Floating point copy
def : InstRW<[N2Write_2cyc_1V], (instregex "^FCPY_ZPmI_[HSD]$",
                                           "^FDUP_ZI_[HSD]$")>;

// Floating point divide, F16
def : InstRW<[N2Write_13cyc_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_H")>;

// Floating point divide, F32
def : InstRW<[N2Write_10cyc_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_S")>;

// Floating point divide, F64
def : InstRW<[N2Write_15cyc_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_D")>;

// Floating point min/max pairwise
def : InstRW<[N2Write_2cyc_1V], (instregex "^F(MAX|MIN)(NM)?P_ZPmZZ_[HSD]")>;

// Floating point min/max
def : InstRW<[N2Write_2cyc_1V], (instregex "^F(MAX|MIN)(NM)?_ZPm[IZ]_[HSD]",
                                           "^F(MAX|MIN)(NM)?_ZPZ[IZ]_[HSD]")>;

// Floating point multiply
def : InstRW<[N2Write_3cyc_1V], (instregex "^(FSCALE|FMULX)_ZPmZ_[HSD]",
                                           "^FMULX_ZPZZ_[HSD]",
                                           "^FMUL_(ZPm[IZ]|ZZZI?)_[HSD]",
                                           "^FMUL_ZPZ[IZ]_[HSD]")>;

// Floating point multiply accumulate
def : InstRW<[N2Write_4cyc_1V], (instregex "^F(N?M(AD|SB)|N?ML[AS])_ZPmZZ_[HSD]$",
                                           "^FN?ML[AS]_ZPZZZ_[HSD]",
                                           "^FML[AS]_ZZZI_[HSD]$")>;

// Floating point multiply add/sub accumulate long
def : InstRW<[N2Write_4cyc_1V], (instregex "^FML[AS]L[BT]_ZZZI?_SHH$")>;

// Floating point reciprocal estimate, F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^FR(ECP|SQRT)E_ZZ_H", "^FRECPX_ZPmZ_H")>;

// Floating point reciprocal estimate, F32
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FR(ECP|SQRT)E_ZZ_S", "^FRECPX_ZPmZ_S")>;

// Floating point reciprocal estimate, F64
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FR(ECP|SQRT)E_ZZ_D", "^FRECPX_ZPmZ_D")>;

// Floating point reciprocal step
def : InstRW<[N2Write_4cyc_1V0], (instregex "^F(RECPS|RSQRTS)_ZZZ_[HSD]$")>;

// Floating point reduction, F16
def : InstRW<[N2Write_6cyc_2V],
             (instregex "^(FADDV|FMAXNMV|FMAXV|FMINNMV|FMINV)_VPZ_H$")>;

// Floating point reduction, F32
def : InstRW<[N2Write_4cyc_1V],
             (instregex "^(FADDV|FMAXNMV|FMAXV|FMINNMV|FMINV)_VPZ_S$")>;

// Floating point reduction, F64
def : InstRW<[N2Write_2cyc_1V],
             (instregex "^(FADDV|FMAXNMV|FMAXV|FMINNMV|FMINV)_VPZ_D$")>;

// Floating point round to integral, F16
def : InstRW<[N2Write_6cyc_4V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_H")>;

// Floating point round to integral, F32
def : InstRW<[N2Write_4cyc_2V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_S")>;

// Floating point round to integral, F64
def : InstRW<[N2Write_3cyc_1V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_D")>;

// Floating point square root, F16
def : InstRW<[N2Write_13cyc_1V0], (instregex "^FSQRT_ZPmZ_H")>;

// Floating point square root, F32
def : InstRW<[N2Write_10cyc_1V0], (instregex "^FSQRT_ZPmZ_S")>;

// Floating point square root, F64
def : InstRW<[N2Write_16cyc_1V0], (instregex "^FSQRT_ZPmZ_D")>;

// Floating point trigonometric exponentiation
def : InstRW<[N2Write_3cyc_1V1], (instregex "^FEXPA_ZZ_[HSD]$")>;

// Floating point trigonometric multiply add
def : InstRW<[N2Write_4cyc_1V], (instregex "^FTMAD_ZZI_[HSD]$")>;

// Floating point trigonometric, miscellaneous
def : InstRW<[N2Write_3cyc_1V], (instregex "^FTS(MUL|SEL)_ZZZ_[HSD]$")>;

// SVE BFloat16 (BF16) instructions
// -----------------------------------------------------------------------------

// Convert, F32 to BF16
def : InstRW<[N2Write_3cyc_1V0], (instrs BFCVT_ZPmZ, BFCVTNT_ZPmZ)>;

// Dot product
def : InstRW<[N2Write_4cyc_1V], (instrs BFDOT_ZZI, BFDOT_ZZZ)>;

// Matrix multiply accumulate
def : InstRW<[N2Write_5cyc_1V], (instrs BFMMLA_ZZZ)>;

// Multiply accumulate long
def : InstRW<[N2Write_4cyc_1V], (instregex "^BFMLAL[BT]_ZZZ(I)?$")>;

// SVE Load instructions
// -----------------------------------------------------------------------------

// Load vector
def : InstRW<[N2Write_6cyc_1L], (instrs LDR_ZXI)>;

// Load predicate
def : InstRW<[N2Write_6cyc_1L_1M], (instrs LDR_PXI)>;

// Contiguous load, scalar + imm
def : InstRW<[N2Write_6cyc_1L], (instregex "^LD1[BHWD]_IMM$",
                                           "^LD1S?B_[HSD]_IMM$",
                                           "^LD1S?H_[SD]_IMM$",
                                           "^LD1S?W_D_IMM$" )>;
// Contiguous load, scalar + scalar
def : InstRW<[N2Write_6cyc_1L01], (instregex "^LD1[BHWD]$",
                                             "^LD1S?B_[HSD]$",
                                             "^LD1S?H_[SD]$",
                                             "^LD1S?W_D$" )>;

// Contiguous load broadcast, scalar + imm
def : InstRW<[N2Write_6cyc_1L], (instregex "^LD1R[BHWD]_IMM$",
                                           "^LD1RSW_IMM$",
                                           "^LD1RS?B_[HSD]_IMM$",
                                           "^LD1RS?H_[SD]_IMM$",
                                           "^LD1RS?W_D_IMM$",
                                           "^LD1RQ_[BHWD]_IMM$")>;

// Contiguous load broadcast, scalar + scalar
def : InstRW<[N2Write_6cyc_1L], (instregex "^LD1RQ_[BHWD]$")>;

// Non temporal load, scalar + imm
def : InstRW<[N2Write_6cyc_1L], (instregex "^LDNT1[BHWD]_ZRI$")>;

// Non temporal load, scalar + scalar
def : InstRW<[N2Write_6cyc_1L_1S], (instregex "^LDNT1[BHWD]_ZRR$")>;

// Non temporal gather load, vector + scalar 32-bit element size
def : InstRW<[N2Write_9cyc_1L_1V], (instregex "^LDNT1[BHW]_ZZR_S$",
                                              "^LDNT1S[BH]_ZZR_S$")>;

// Non temporal gather load, vector + scalar 64-bit element size
def : InstRW<[N2Write_10cyc_2L_2V1], (instregex "^LDNT1S?[BHW]_ZZR_D$")>;
def : InstRW<[N2Write_10cyc_2L_2V1], (instrs LDNT1D_ZZR_D)>;

// Contiguous first faulting load, scalar + scalar
def : InstRW<[N2Write_6cyc_1L_1S], (instregex "^LDFF1[BHWD]$",
                                              "^LDFF1S?B_[HSD]$",
                                              "^LDFF1S?H_[SD]$",
                                              "^LDFF1S?W_D$")>;

// Contiguous non faulting load, scalar + imm
def : InstRW<[N2Write_6cyc_1L], (instregex "^LDNF1[BHWD]_IMM$",
                                           "^LDNF1S?B_[HSD]_IMM$",
                                           "^LDNF1S?H_[SD]_IMM$",
                                           "^LDNF1S?W_D_IMM$")>;

// Contiguous Load two structures to two vectors, scalar + imm
def : InstRW<[N2Write_8cyc_1L_1V], (instregex "^LD2[BHWD]_IMM$")>;

// Contiguous Load two structures to two vectors, scalar + scalar
def : InstRW<[N2Write_9cyc_1L_1V], (instregex "^LD2[BHWD]$")>;

// Contiguous Load three structures to three vectors, scalar + imm
def : InstRW<[N2Write_9cyc_1L_1V], (instregex "^LD3[BHWD]_IMM$")>;

// Contiguous Load three structures to three vectors, scalar + scalar
def : InstRW<[N2Write_10cyc_1V_1L_1S], (instregex "^LD3[BHWD]$")>;

// Contiguous Load four structures to four vectors, scalar + imm
def : InstRW<[N2Write_9cyc_2L_2V], (instregex "^LD4[BHWD]_IMM$")>;

// Contiguous Load four structures to four vectors, scalar + scalar
def : InstRW<[N2Write_10cyc_2L_2V_2S], (instregex "^LD4[BHWD]$")>;

// Gather load, vector + imm, 32-bit element size
def : InstRW<[N2Write_9cyc_1L_1V], (instregex "^GLD(FF)?1S?[BH]_S_IMM$",
                                              "^GLD(FF)?1W_IMM$")>;

// Gather load, vector + imm, 64-bit element size
def : InstRW<[N2Write_9cyc_2L_2V], (instregex "^GLD(FF)?1S?[BHW]_D_IMM$",
                                              "^GLD(FF)?1D_IMM$")>;

// Gather load, 64-bit element size
def : InstRW<[N2Write_9cyc_2L_2V],
             (instregex "^GLD(FF)?1S?[BHW]_D_[SU]XTW(_SCALED)?$",
                        "^GLD(FF)?1S?[BHW]_D(_SCALED)?$",
                        "^GLD(FF)?1D_[SU]XTW(_SCALED)?$",
                        "^GLD(FF)?1D(_SCALED)?$")>;

// Gather load, 32-bit scaled offset
def : InstRW<[N2Write_10cyc_2L_2V],
             (instregex "^GLD(FF)?1S?[HW]_S_[SU]XTW_SCALED$",
                        "^GLD(FF)?1W_[SU]XTW_SCALED")>;

// Gather load, 32-bit unpacked unscaled offset
def : InstRW<[N2Write_9cyc_1L_1V], (instregex "^GLD(FF)?1S?[BH]_S_[SU]XTW$",
                                              "^GLD(FF)?1W_[SU]XTW$")>;

// SVE Store instructions
// -----------------------------------------------------------------------------

// Store from predicate reg
def : InstRW<[N2Write_1cyc_1L01], (instrs STR_PXI)>;

// Store from vector reg
def : InstRW<[N2Write_2cyc_1L01_1V], (instrs STR_ZXI)>;

// Contiguous store, scalar + imm
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^ST1[BHWD]_IMM$",
                                                "^ST1B_[HSD]_IMM$",
                                                "^ST1H_[SD]_IMM$",
                                                "^ST1W_D_IMM$")>;

// Contiguous store, scalar + scalar
def : InstRW<[N2Write_2cyc_1L01_1S_1V], (instregex "^ST1H(_[SD])?$")>;
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^ST1[BWD]$",
                                                "^ST1B_[HSD]$",
                                                "^ST1W_D$")>;

// Contiguous store two structures from two vectors, scalar + imm
def : InstRW<[N2Write_4cyc_1L01_1V], (instregex "^ST2[BHWD]_IMM$")>;

// Contiguous store two structures from two vectors, scalar + scalar
def : InstRW<[N2Write_4cyc_1L01_1S_1V], (instrs ST2H)>;

// Contiguous store two structures from two vectors, scalar + scalar
def : InstRW<[N2Write_4cyc_1L01_1V], (instregex "^ST2[BWD]$")>;

// Contiguous store three structures from three vectors, scalar + imm
def : InstRW<[N2Write_7cyc_5L01_5V], (instregex "^ST3[BHWD]_IMM$")>;

// Contiguous store three structures from three vectors, scalar + scalar
def : InstRW<[N2Write_7cyc_5L01_5S_5V], (instrs ST3H)>;

// Contiguous store three structures from three vectors, scalar + scalar
def : InstRW<[N2Write_7cyc_5L01_5S_5V], (instregex "^ST3[BWD]$")>;

// Contiguous store four structures from four vectors, scalar + imm
def : InstRW<[N2Write_11cyc_9L01_9V], (instregex "^ST4[BHWD]_IMM$")>;

// Contiguous store four structures from four vectors, scalar + scalar
def : InstRW<[N2Write_11cyc_9L01_9S_9V], (instrs ST4H)>;

// Contiguous store four structures from four vectors, scalar + scalar
def : InstRW<[N2Write_11cyc_9L01_9S_9V], (instregex "^ST4[BWD]$")>;

// Non temporal store, scalar + imm
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STNT1[BHWD]_ZRI$")>;

// Non temporal store, scalar + scalar
def : InstRW<[N2Write_2cyc_1L01_1S_1V], (instrs STNT1H_ZRR)>;
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STNT1[BWD]_ZRR$")>;

// Scatter non temporal store, vector + scalar 32-bit element size
def : InstRW<[N2Write_4cyc_2L01_2V], (instregex "^STNT1[BHW]_ZZR_S")>;

// Scatter non temporal store, vector + scalar 64-bit element size
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^STNT1[BHWD]_ZZR_D")>;

// Scatter store vector + imm 32-bit element size
def : InstRW<[N2Write_4cyc_2L01_2V], (instregex "^SST1[BH]_S_IMM$",
                                                "^SST1W_IMM$")>;

// Scatter store vector + imm 64-bit element size
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^SST1[BHW]_D_IMM$",
                                                "^SST1D_IMM$")>;

// Scatter store, 32-bit scaled offset
def : InstRW<[N2Write_4cyc_2L01_2V],
             (instregex "^SST1(H_S|W)_[SU]XTW_SCALED$")>;

// Scatter store, 32-bit unpacked unscaled offset
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^SST1[BHW]_D_[SU]XTW$",
                                                "^SST1D_[SU]XTW$")>;

// Scatter store, 32-bit unpacked scaled offset
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^SST1[HW]_D_[SU]XTW_SCALED$",
                                                "^SST1D_[SU]XTW_SCALED$")>;

// Scatter store, 32-bit unscaled offset
def : InstRW<[N2Write_4cyc_2L01_2V], (instregex "^SST1[BH]_S_[SU]XTW$",
                                                "^SST1W_[SU]XTW$")>;

// Scatter store, 64-bit scaled offset
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^SST1[HW]_D_SCALED$",
                                                "^SST1D_SCALED$")>;

// Scatter store, 64-bit unscaled offset
def : InstRW<[N2Write_2cyc_1L01_1V], (instregex "^SST1[BHW]_D$",
                                                "^SST1D$")>;

// SVE Miscellaneous instructions
// -----------------------------------------------------------------------------

// Read first fault register, unpredicated
def : InstRW<[N2Write_2cyc_1M0], (instrs RDFFR_P)>;

// Read first fault register, predicated
def : InstRW<[N2Write_3cyc_1M0_1M], (instrs RDFFR_PPz)>;

// Read first fault register and set flags
def : InstRW<[N2Write_4cyc_2M0_2M], (instrs RDFFRS_PPz)>;

// Set first fault register
// Write to first fault register
def : InstRW<[N2Write_2cyc_1M0], (instrs SETFFR, WRFFR)>;

// Prefetch
def : InstRW<[N2Write_4cyc_1L], (instregex "^PRF[BHWD]")>;

// SVE Cryptographic instructions
// -----------------------------------------------------------------------------

// Crypto AES ops
def : InstRW<[N2Write_2cyc_1V], (instregex "^AES[DE]_ZZZ_B$",
                                           "^AESI?MC_ZZ_B$")>;

// Crypto SHA3 ops
def : InstRW<[N2Write_2cyc_1V0], (instregex "^(BCAX|EOR3)_ZZZZ$",
                                            "^RAX1_ZZZ_D$",
                                            "^XAR_ZZZI_[BHSD]$")>;

// Crypto SM4 ops
def : InstRW<[N2Write_4cyc_1V0], (instregex "^SM4E(KEY)?_ZZZ_S$")>;

}
