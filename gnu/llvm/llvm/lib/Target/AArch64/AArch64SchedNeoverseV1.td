//=- AArch64SchedNeoverseV1.td - NeoverseV1 Scheduling Model -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for the Arm Neoverse V1 processors.
//
// References:
// - "Arm Neoverse V1 Software Optimization Guide"
// - "Arm Neoverse V1 Platform: Unleashing a new performance tier for Arm-based computing"
//   https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/neoverse-v1-platform-a-new-performance-tier-for-arm
// - "Neoverse V1"
//   https://en.wikichip.org/wiki/arm_holdings/microarchitectures/neoverse_v1

//
//===----------------------------------------------------------------------===//

def NeoverseV1Model : SchedMachineModel {
  let IssueWidth            =  15; // Maximum micro-ops dispatch rate.
  let MicroOpBufferSize     = 256; // Micro-op re-order buffer.
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  11; // Cycles cost of branch mispredicted.
  let LoopMicroOpBufferSize =  16; // NOTE: Copied from Cortex-A57.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVE2Unsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCPA,
                                                    HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Neoverse V1.
// Instructions are first fetched and then decoded into internal macro-ops
// (MOPs).  From there, the MOPs proceed through register renaming and dispatch
// stages.  A MOP can be split into one or more micro-ops further down the
// pipeline, after the decode stage.  Once dispatched, micro-ops wait for their
// operands and issue out-of-order to one of the issue pipelines.  Each issue
// pipeline can accept one micro-op per cycle.

let SchedModel = NeoverseV1Model in {

// Define the issue ports.
def V1UnitB   : ProcResource<2>;  // Branch 0/1
def V1UnitS   : ProcResource<2>;  // Integer single cycle 0/1
def V1UnitM0  : ProcResource<1>;  // Integer multicycle 0
def V1UnitM1  : ProcResource<1>;  // Integer multicycle 1
def V1UnitL01 : ProcResource<2>;  // Load/Store 0/1
def V1UnitL2  : ProcResource<1>;  // Load 2
def V1UnitD   : ProcResource<2>;  // Store data 0/1
def V1UnitV0  : ProcResource<1>;  // FP/ASIMD 0
def V1UnitV1  : ProcResource<1>;  // FP/ASIMD 1
def V1UnitV2  : ProcResource<1>;  // FP/ASIMD 2
def V1UnitV3  : ProcResource<1>;  // FP/ASIMD 3

def V1UnitI   : ProcResGroup<[V1UnitS,
                              V1UnitM0, V1UnitM1]>;   // Integer units
def V1UnitJ   : ProcResGroup<[V1UnitS, V1UnitM0]>;    // Integer 0-2 units
def V1UnitM   : ProcResGroup<[V1UnitM0, V1UnitM1]>;   // Integer multicycle units
def V1UnitL   : ProcResGroup<[V1UnitL01, V1UnitL2]>;  // Load units
def V1UnitV   : ProcResGroup<[V1UnitV0, V1UnitV1,
                              V1UnitV2, V1UnitV3]>;   // FP/ASIMD units
def V1UnitV01 : ProcResGroup<[V1UnitV0, V1UnitV1]>;   // FP/ASIMD 0/1 units
def V1UnitV02 : ProcResGroup<[V1UnitV0, V1UnitV2]>;   // FP/ASIMD 0/2 units
def V1UnitV13 : ProcResGroup<[V1UnitV1, V1UnitV3]>;   // FP/ASIMD 1/3 units

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
def V1Write_0c_0Z : SchedWriteRes<[]>;


//===----------------------------------------------------------------------===//
// Define generic 1 micro-op types

def V1Write_1c_1B      : SchedWriteRes<[V1UnitB]>   { let Latency = 1; }
def V1Write_1c_1I      : SchedWriteRes<[V1UnitI]>   { let Latency = 1; }
def V1Write_1c_1J      : SchedWriteRes<[V1UnitJ]>   { let Latency = 1; }
def V1Write_4c_1L      : SchedWriteRes<[V1UnitL]>   { let Latency = 4; }
def V1Write_6c_1L      : SchedWriteRes<[V1UnitL]>   { let Latency = 6; }
def V1Write_1c_1L01    : SchedWriteRes<[V1UnitL01]> { let Latency = 1; }
def V1Write_4c_1L01    : SchedWriteRes<[V1UnitL01]> { let Latency = 4; }
def V1Write_6c_1L01    : SchedWriteRes<[V1UnitL01]> { let Latency = 6; }
def V1Write_2c_1M      : SchedWriteRes<[V1UnitM]>   { let Latency = 2; }
def V1Write_3c_1M      : SchedWriteRes<[V1UnitM]>   { let Latency = 3; }
def V1Write_4c_1M      : SchedWriteRes<[V1UnitM]>   { let Latency = 4; }
def V1Write_1c_1M0     : SchedWriteRes<[V1UnitM0]>  { let Latency = 1; }
def V1Write_2c_1M0     : SchedWriteRes<[V1UnitM0]>  { let Latency = 2; }
def V1Write_3c_1M0     : SchedWriteRes<[V1UnitM0]>  { let Latency = 3; }
def V1Write_5c_1M0     : SchedWriteRes<[V1UnitM0]>  { let Latency = 5; }
def V1Write_12c5_1M0   : SchedWriteRes<[V1UnitM0]>  { let Latency = 12;
                                                      let ReleaseAtCycles = [5]; }
def V1Write_20c5_1M0   : SchedWriteRes<[V1UnitM0]>  { let Latency = 20;
                                                      let ReleaseAtCycles = [5]; }
def V1Write_2c_1V      : SchedWriteRes<[V1UnitV]>   { let Latency = 2; }
def V1Write_3c_1V      : SchedWriteRes<[V1UnitV]>   { let Latency = 3; }
def V1Write_4c_1V      : SchedWriteRes<[V1UnitV]>   { let Latency = 4; }
def V1Write_5c_1V      : SchedWriteRes<[V1UnitV]>   { let Latency = 5; }
def V1Write_2c_1V0     : SchedWriteRes<[V1UnitV0]>  { let Latency = 2; }
def V1Write_3c_1V0     : SchedWriteRes<[V1UnitV0]>  { let Latency = 3; }
def V1Write_4c_1V0     : SchedWriteRes<[V1UnitV0]>  { let Latency = 4; }
def V1Write_6c_1V0     : SchedWriteRes<[V1UnitV0]>  { let Latency = 6; }
def V1Write_10c7_1V0   : SchedWriteRes<[V1UnitV0]>  { let Latency = 10;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_12c7_1V0   : SchedWriteRes<[V1UnitV0]>  { let Latency = 12;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_13c10_1V0  : SchedWriteRes<[V1UnitV0]>  { let Latency = 13;
                                                      let ReleaseAtCycles = [10]; }
def V1Write_15c7_1V0   : SchedWriteRes<[V1UnitV0]>  { let Latency = 15;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_16c7_1V0   : SchedWriteRes<[V1UnitV0]>  { let Latency = 16;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_20c7_1V0   : SchedWriteRes<[V1UnitV0]>  { let Latency = 20;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_2c_1V01    : SchedWriteRes<[V1UnitV01]> { let Latency = 2; }
def V1Write_3c_1V01    : SchedWriteRes<[V1UnitV01]> { let Latency = 3; }
def V1Write_4c_1V01    : SchedWriteRes<[V1UnitV01]> { let Latency = 4; }
def V1Write_5c_1V01    : SchedWriteRes<[V1UnitV01]> { let Latency = 5; }
def V1Write_3c_1V02    : SchedWriteRes<[V1UnitV02]> { let Latency = 3; }
def V1Write_4c_1V02    : SchedWriteRes<[V1UnitV02]> { let Latency = 4; }
def V1Write_7c7_1V02   : SchedWriteRes<[V1UnitV02]> { let Latency = 7;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_10c7_1V02  : SchedWriteRes<[V1UnitV02]> { let Latency = 10;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_13c5_1V02  : SchedWriteRes<[V1UnitV02]> { let Latency = 13;
                                                      let ReleaseAtCycles = [5]; }
def V1Write_13c11_1V02 : SchedWriteRes<[V1UnitV02]> { let Latency = 13;
                                                      let ReleaseAtCycles = [11]; }
def V1Write_15c7_1V02  : SchedWriteRes<[V1UnitV02]> { let Latency = 15;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_16c7_1V02  : SchedWriteRes<[V1UnitV02]> { let Latency = 16;
                                                      let ReleaseAtCycles = [7]; }
def V1Write_2c_1V1     : SchedWriteRes<[V1UnitV1]>  { let Latency = 2; }
def V1Write_3c_1V1     : SchedWriteRes<[V1UnitV1]>  { let Latency = 3; }
def V1Write_4c_1V1     : SchedWriteRes<[V1UnitV1]>  { let Latency = 4; }
def V1Write_2c_1V13    : SchedWriteRes<[V1UnitV13]> { let Latency = 2; }
def V1Write_4c_1V13    : SchedWriteRes<[V1UnitV13]> { let Latency = 4; }

//===----------------------------------------------------------------------===//
// Define generic 2 micro-op types

let Latency = 1, NumMicroOps = 2 in
def V1Write_1c_1B_1S     : SchedWriteRes<[V1UnitB, V1UnitS]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_1B_1M0    : SchedWriteRes<[V1UnitB, V1UnitM0]>;
let Latency = 3, NumMicroOps = 2 in
def V1Write_3c_1I_1M     : SchedWriteRes<[V1UnitI, V1UnitM]>;
let Latency = 5, NumMicroOps = 2 in
def V1Write_5c_1I_1L     : SchedWriteRes<[V1UnitI, V1UnitL]>;
let Latency = 7, NumMicroOps = 2 in
def V1Write_7c_1I_1L     : SchedWriteRes<[V1UnitI, V1UnitL]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_2L        : SchedWriteRes<[V1UnitL, V1UnitL]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_1L_1M     : SchedWriteRes<[V1UnitL, V1UnitM]>;
let Latency = 8, NumMicroOps = 2 in
def V1Write_8c_1L_1V     : SchedWriteRes<[V1UnitL, V1UnitV]>;
let Latency = 9, NumMicroOps = 2 in
def V1Write_9c_1L_1V     : SchedWriteRes<[V1UnitL, V1UnitV]>;
let Latency = 11, NumMicroOps = 2 in
def V1Write_11c_1L_1V     : SchedWriteRes<[V1UnitL, V1UnitV]>;
let Latency = 1, NumMicroOps = 2 in
def V1Write_1c_1L01_1D   : SchedWriteRes<[V1UnitL01, V1UnitD]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_1L01_1S   : SchedWriteRes<[V1UnitL01, V1UnitS]>;
let Latency = 7, NumMicroOps = 2 in
def V1Write_7c_1L01_1S   : SchedWriteRes<[V1UnitL01, V1UnitS]>;
let Latency = 2, NumMicroOps = 2 in
def V1Write_2c_1L01_1V   : SchedWriteRes<[V1UnitL01, V1UnitV]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_1L01_1V   : SchedWriteRes<[V1UnitL01, V1UnitV]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_1L01_1V   : SchedWriteRes<[V1UnitL01, V1UnitV]>;
let Latency = 2, NumMicroOps = 2 in
def V1Write_2c_1L01_1V01 : SchedWriteRes<[V1UnitL01, V1UnitV01]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_1L01_1V01 : SchedWriteRes<[V1UnitL01, V1UnitV01]>;
let Latency = 2, NumMicroOps = 2 in
def V1Write_2c_2M0       : SchedWriteRes<[V1UnitM0, V1UnitM0]>;
let Latency = 3, NumMicroOps = 2 in
def V1Write_3c_2M0       : SchedWriteRes<[V1UnitM0, V1UnitM0]>;
let Latency = 9, NumMicroOps = 2 in
def V1Write_9c_1M0_1L    : SchedWriteRes<[V1UnitM0, V1UnitL]>;
let Latency = 5, NumMicroOps = 2 in
def V1Write_5c_1M0_1V    : SchedWriteRes<[V1UnitM0, V1UnitV]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_1M0_1V0    : SchedWriteRes<[V1UnitM0, V1UnitV0]>;
let Latency = 7, NumMicroOps = 2 in
def V1Write_7c_1M0_1V0   : SchedWriteRes<[V1UnitM0, V1UnitV1]>;
let Latency = 5, NumMicroOps = 2 in
def V1Write_5c_1M0_1V01    : SchedWriteRes<[V1UnitM0, V1UnitV01]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_1M0_1V1   : SchedWriteRes<[V1UnitM0, V1UnitV1]>;
let Latency = 9, NumMicroOps = 2 in
def V1Write_9c_1M0_1V1    : SchedWriteRes<[V1UnitM0, V1UnitV1]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_2V        : SchedWriteRes<[V1UnitV, V1UnitV]>;
let Latency = 8, NumMicroOps = 2 in
def V1Write_8c_1V_1V01   : SchedWriteRes<[V1UnitV, V1UnitV01]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_2V0       : SchedWriteRes<[V1UnitV0, V1UnitV0]>;
let Latency = 5, NumMicroOps = 2 in
def V1Write_5c_2V0       : SchedWriteRes<[V1UnitV0, V1UnitV0]>;
let Latency = 2, NumMicroOps = 2 in
def V1Write_2c_2V01      : SchedWriteRes<[V1UnitV01, V1UnitV01]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_2V01      : SchedWriteRes<[V1UnitV01, V1UnitV01]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_2V02      : SchedWriteRes<[V1UnitV02, V1UnitV02]>;
let Latency = 6, NumMicroOps = 2 in
def V1Write_6c_2V02      : SchedWriteRes<[V1UnitV02, V1UnitV02]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_1V13_1V   : SchedWriteRes<[V1UnitV13, V1UnitV]>;
let Latency = 4, NumMicroOps = 2 in
def V1Write_4c_2V13      : SchedWriteRes<[V1UnitV13, V1UnitV13]>;

//===----------------------------------------------------------------------===//
// Define generic 3 micro-op types

let Latency = 2, NumMicroOps = 3 in
def V1Write_2c_1I_1L01_1V01 : SchedWriteRes<[V1UnitI, V1UnitL01, V1UnitV01]>;
let Latency = 7, NumMicroOps = 3 in
def V1Write_7c_2M0_1V01     : SchedWriteRes<[V1UnitM0, V1UnitM0, V1UnitV01]>;
let Latency = 8, NumMicroOps = 3 in
def V1Write_8c_1L_2V        : SchedWriteRes<[V1UnitL, V1UnitV, V1UnitV]>;
let Latency = 6, NumMicroOps = 3 in
def V1Write_6c_3L           : SchedWriteRes<[V1UnitL, V1UnitL, V1UnitL]>;
let Latency = 2, NumMicroOps = 3 in
def V1Write_2c_1L01_1S_1V   : SchedWriteRes<[V1UnitL01, V1UnitS, V1UnitV]>;
let Latency = 4, NumMicroOps = 3 in
def V1Write_4c_1L01_1S_1V   : SchedWriteRes<[V1UnitL01, V1UnitS, V1UnitV]>;
let Latency = 2, NumMicroOps = 3 in
def V1Write_2c_2L01_1V01    : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitV01]>;
let Latency = 6, NumMicroOps = 3 in
def V1Write_6c_3V           : SchedWriteRes<[V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 4, NumMicroOps = 3 in
def V1Write_4c_3V01         : SchedWriteRes<[V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 6, NumMicroOps = 3 in
def V1Write_6c_3V01         : SchedWriteRes<[V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 8, NumMicroOps = 3 in
def V1Write_8c_3V01         : SchedWriteRes<[V1UnitV01, V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 4 micro-op types

let Latency = 8, NumMicroOps = 4 in
def V1Write_8c_2M0_2V0   : SchedWriteRes<[V1UnitM0, V1UnitM0,
                                          V1UnitV0, V1UnitV0]>;
let Latency = 7, NumMicroOps = 4 in
def V1Write_7c_4L        : SchedWriteRes<[V1UnitL, V1UnitL, V1UnitL, V1UnitL]>;
let Latency = 8, NumMicroOps = 4 in
def V1Write_8c_2L_2V        : SchedWriteRes<[V1UnitL, V1UnitL,
                                             V1UnitV, V1UnitV]>;
let Latency = 9, NumMicroOps = 4 in
def V1Write_9c_2L_2V        : SchedWriteRes<[V1UnitL, V1UnitL,
                                             V1UnitV, V1UnitV]>;
let Latency = 11, NumMicroOps = 4 in
def V1Write_11c_2L_2V       : SchedWriteRes<[V1UnitL, V1UnitL,
                                             V1UnitV, V1UnitV]>;
let Latency = 10, NumMicroOps = 4 in
def V1Write_10c_2L01_2V     : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV, V1UnitV]>;
let Latency = 2, NumMicroOps = 4 in
def V1Write_2c_2L01_2V01    : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 4, NumMicroOps = 4 in
def V1Write_4c_2L01_2V01    : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 8, NumMicroOps = 4 in
def V1Write_8c_2L01_2V01    : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 9, NumMicroOps = 4 in
def V1Write_9c_2L01_2V01    : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 10, NumMicroOps = 4 in
def V1Write_10c_2L01_2V01   : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 10, NumMicroOps = 4 in
def V1Write_10c_1V_1V01_2V1 : SchedWriteRes<[V1UnitV, V1UnitV01,
                                             V1UnitV1, V1UnitV1]>;
let Latency = 12, NumMicroOps = 4 in
def V1Write_12c_1V_1V01_2V1 : SchedWriteRes<[V1UnitV, V1UnitV01,
                                             V1UnitV1, V1UnitV1]>;
let Latency = 6, NumMicroOps = 4 in
def V1Write_6c_4V0          : SchedWriteRes<[V1UnitV0, V1UnitV0,
                                             V1UnitV0, V1UnitV0]>;
let Latency = 12, NumMicroOps = 4 in
def V1Write_12c_4V01        : SchedWriteRes<[V1UnitV01, V1UnitV01,
                                             V1UnitV01, V1UnitV01]>;
let Latency = 6, NumMicroOps = 4 in
def V1Write_6c_4V02         : SchedWriteRes<[V1UnitV02, V1UnitV02]>;

//===----------------------------------------------------------------------===//
// Define generic 5 micro-op types

let Latency = 8, NumMicroOps = 5 in
def V1Write_8c_2L_3V            : SchedWriteRes<[V1UnitL, V1UnitL,
                                                 V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 14, NumMicroOps = 5 in
def V1Write_14c_1V_1V0_2V1_1V13 : SchedWriteRes<[V1UnitV,
                                                 V1UnitV0,
                                                 V1UnitV1, V1UnitV1,
                                                 V1UnitV13]>;
let Latency = 9, NumMicroOps = 5 in
def V1Write_9c_1V_4V01          : SchedWriteRes<[V1UnitV,
                                                 V1UnitV01, V1UnitV01,
                                                 V1UnitV01, V1UnitV01]>;
let Latency = 6, NumMicroOps = 5 in
def V1Write_6c_5V01             : SchedWriteRes<[V1UnitV01, V1UnitV01,
                                                 V1UnitV01, V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 6 micro-op types

let Latency = 6, NumMicroOps = 6 in
def V1Write_6c_3L_3V      : SchedWriteRes<[V1UnitL, V1UnitL, V1UnitL,
                                           V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 8, NumMicroOps = 6 in
def V1Write_8c_3L_3V      : SchedWriteRes<[V1UnitL, V1UnitL, V1UnitL,
                                           V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 2, NumMicroOps = 6 in
def V1Write_2c_3L01_3V01  : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 5, NumMicroOps = 6 in
def V1Write_5c_3L01_3V01  : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 6, NumMicroOps = 6 in
def V1Write_6c_3L01_3V01  : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 11, NumMicroOps = 6 in
def V1Write_11c_3L01_3V01 : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 11, NumMicroOps = 6 in
def V1Write_11c_1V_5V01   : SchedWriteRes<[V1UnitV,
                                           V1UnitV01, V1UnitV01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;
let Latency = 13, NumMicroOps = 6 in
def V1Write_13c_6V01      : SchedWriteRes<[V1UnitV01, V1UnitV01, V1UnitV01,
                                           V1UnitV01, V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 7 micro-op types

let Latency = 8, NumMicroOps = 7 in
def V1Write_8c_3L_4V         : SchedWriteRes<[V1UnitL, V1UnitL, V1UnitL,
                                              V1UnitV, V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 8, NumMicroOps = 7 in
def V1Write_13c_3L01_1S_3V01 : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                              V1UnitS,
                                              V1UnitV01, V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 8 micro-op types

let Latency = 9, NumMicroOps = 8 in
def V1Write_9c_4L_4V      : SchedWriteRes<[V1UnitL, V1UnitL,
                                           V1UnitL, V1UnitL,
                                           V1UnitV, V1UnitV,
                                           V1UnitV, V1UnitV]>;
let Latency = 2, NumMicroOps = 8 in
def V1Write_2c_4L01_4V01  : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                           V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01,
                                           V1UnitV01, V1UnitV01]>;
let Latency = 4, NumMicroOps = 8 in
def V1Write_4c_4L01_4V01  : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                           V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01,
                                           V1UnitV01, V1UnitV01]>;
let Latency = 12, NumMicroOps = 8 in
def V1Write_12c_4L01_4V01 : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                           V1UnitL01, V1UnitL01,
                                           V1UnitV01, V1UnitV01,
                                           V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 10 micro-op types

let Latency = 13, NumMicroOps = 10 in
def V1Write_13c_4L01_2S_4V01 : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                              V1UnitL01, V1UnitL01,
                                              V1UnitS, V1UnitS,
                                              V1UnitV01, V1UnitV01,
                                              V1UnitV01, V1UnitV01]>;
let Latency = 7, NumMicroOps = 10 in
def V1Write_7c_5L01_5V       : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                              V1UnitL01, V1UnitL01, V1UnitL01,
                                              V1UnitV, V1UnitV,
                                              V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 11, NumMicroOps = 10 in
def V1Write_11c_10V0         : SchedWriteRes<[V1UnitV0,
                                              V1UnitV0, V1UnitV0, V1UnitV0,
                                              V1UnitV0, V1UnitV0, V1UnitV0,
                                              V1UnitV0, V1UnitV0, V1UnitV0]>;

//===----------------------------------------------------------------------===//
// Define generic 12 micro-op types

let Latency = 7, NumMicroOps = 12 in
def V1Write_7c_6L01_6V01 : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                          V1UnitL01, V1UnitL01, V1UnitL01,
                                          V1UnitV01, V1UnitV01, V1UnitV01,
                                          V1UnitV01, V1UnitV01, V1UnitV01]>;

//===----------------------------------------------------------------------===//
// Define generic 15 micro-op types

let Latency = 7, NumMicroOps = 15 in
def V1Write_7c_5L01_5S_5V : SchedWriteRes<[V1UnitL01, V1UnitL01,
                                           V1UnitL01, V1UnitL01, V1UnitL01,
                                           V1UnitS, V1UnitS,
                                           V1UnitS, V1UnitS, V1UnitS,
                                           V1UnitV, V1UnitV,
                                           V1UnitV, V1UnitV, V1UnitV]>;


//===----------------------------------------------------------------------===//
// Define generic 18 micro-op types

let Latency = 19, NumMicroOps = 18 in
def V1Write_11c_9L01_9V : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                         V1UnitL01, V1UnitL01, V1UnitL01, 
                                         V1UnitL01, V1UnitL01, V1UnitL01,
                                         V1UnitV, V1UnitV, V1UnitV, 
                                         V1UnitV, V1UnitV, V1UnitV,
                                         V1UnitV, V1UnitV, V1UnitV]>;
let Latency = 19, NumMicroOps = 18 in
def V1Write_19c_18V0    : SchedWriteRes<[V1UnitV0, V1UnitV0, V1UnitV0,
                                         V1UnitV0, V1UnitV0, V1UnitV0, 
                                         V1UnitV0, V1UnitV0, V1UnitV0,
                                         V1UnitV0, V1UnitV0, V1UnitV0, 
                                         V1UnitV0, V1UnitV0, V1UnitV0,
                                         V1UnitV0, V1UnitV0, V1UnitV0]>;

//===----------------------------------------------------------------------===//
// Define generic 27 micro-op types

let Latency = 11, NumMicroOps = 27 in
def V1Write_11c_9L01_9S_9V : SchedWriteRes<[V1UnitL01, V1UnitL01, V1UnitL01,
                                            V1UnitL01, V1UnitL01, V1UnitL01, 
                                            V1UnitL01, V1UnitL01, V1UnitL01,
                                            V1UnitS, V1UnitS, V1UnitS, 
                                            V1UnitS, V1UnitS, V1UnitS,
                                            V1UnitS, V1UnitS, V1UnitS,
                                            V1UnitV, V1UnitV, V1UnitV, 
                                            V1UnitV, V1UnitV, V1UnitV,
                                            V1UnitV, V1UnitV, V1UnitV]>;


// Miscellaneous Instructions
// -----------------------------------------------------------------------------

// COPY
def : InstRW<[V1Write_1c_1I], (instrs COPY)>;

// MSR
def : WriteRes<WriteSys, []> { let Latency = 1; }


// Branch Instructions
// -----------------------------------------------------------------------------

// Branch, immed
// Compare and branch
def : SchedAlias<WriteBr, V1Write_1c_1B>;

// Branch, register
def : SchedAlias<WriteBrReg, V1Write_1c_1B>;

// Branch and link, immed
// Branch and link, register
def : InstRW<[V1Write_1c_1B_1S], (instrs BL, BLR)>;

// Compare and branch
def : InstRW<[V1Write_1c_1B], (instregex "^[CT]BN?Z[XW]$")>;


// Arithmetic and Logical Instructions
// -----------------------------------------------------------------------------

// ALU, basic
// Conditional compare
// Conditional select
// Logical, basic
// Address generation
// Count leading
// Reverse bits/bytes
// Move immediate
def : SchedAlias<WriteI, V1Write_1c_1I>;

// ALU, basic, flagset
def : InstRW<[V1Write_1c_1J],
             (instregex "^(ADD|SUB)S[WX]r[ir]$",
                        "^(ADC|SBC)S[WX]r$",
                        "^ANDS[WX]ri$",
                        "^(AND|BIC)S[WX]rr$")>;

// ALU, extend and shift
def : SchedAlias<WriteIEReg, V1Write_2c_1M>;

// Arithmetic, LSL shift, shift <= 4
// Arithmetic, LSR/ASR/ROR shift or LSL shift > 4
def V1WriteISReg : SchedWriteVariant<
                     [SchedVar<IsCheapLSL,  [V1Write_1c_1I]>,
                      SchedVar<NoSchedPred, [V1Write_2c_1M]>]>;
def              : SchedAlias<WriteISReg, V1WriteISReg>;

// Arithmetic, flagset, LSL shift, shift <= 4
// Arithmetic, flagset, LSR/ASR/ROR shift or LSL shift > 4
def V1WriteISRegS : SchedWriteVariant<
                      [SchedVar<IsCheapLSL,  [V1Write_1c_1J]>,
                       SchedVar<NoSchedPred, [V1Write_2c_1M]>]>;
def               : InstRW<[V1WriteISRegS],
                           (instregex "^(ADD|SUB)S(([WX]r[sx])|Xrx64)$")>;

// Logical, shift, no flagset
def : InstRW<[V1Write_1c_1I], (instregex "^(AND|BIC|EON|EOR|ORN|ORR)[WX]rs$")>;

// Logical, shift, flagset
def : InstRW<[V1Write_2c_1M], (instregex "^(AND|BIC)S[WX]rs$")>;

// Flag manipulation instructions
def : InstRW<[V1Write_1c_1J], (instrs SETF8, SETF16, RMIF, CFINV)>;


// Divide and multiply instructions
// -----------------------------------------------------------------------------

// Divide
def : SchedAlias<WriteID32, V1Write_12c5_1M0>;
def : SchedAlias<WriteID64, V1Write_20c5_1M0>;

// Multiply
// Multiply accumulate
// Multiply accumulate, long
// Multiply long
def V1WriteIM : SchedWriteVariant<
                  [SchedVar<NeoverseMULIdiomPred, [V1Write_2c_1M]>,
                   SchedVar<NoSchedPred,          [V1Write_2c_1M0]>]>;
def           : SchedAlias<WriteIM32, V1WriteIM>;
def           : SchedAlias<WriteIM64, V1WriteIM>;

// Multiply high
def : InstRW<[V1Write_3c_1M, ReadIM, ReadIM], (instrs SMULHrr, UMULHrr)>;


// Pointer Authentication Instructions (v8.3 PAC)
// -----------------------------------------------------------------------------

// Authenticate data address
// Authenticate instruction address
// Compute pointer authentication code for data address
// Compute pointer authentication code, using generic key
// Compute pointer authentication code for instruction address
def : InstRW<[V1Write_5c_1M0], (instregex "^AUT",
                                          "^PAC")>;

// Branch and link, register, with pointer authentication
// Branch, register, with pointer authentication
// Branch, return, with pointer authentication
def : InstRW<[V1Write_6c_1B_1M0], (instregex "^BL?RA[AB]Z?$",
                                             "^E?RETA[AB]$")>;

// Load register, with pointer authentication
def : InstRW<[V1Write_9c_1M0_1L], (instregex "^LDRA[AB](indexed|writeback)")>;

// Strip pointer authentication code
def : InstRW<[V1Write_2c_1M0], (instrs XPACD, XPACI, XPACLRI)>;


// Miscellaneous data-processing instructions
// -----------------------------------------------------------------------------

// Bitfield extract, one reg
// Bitfield extract, two regs
def V1WriteExtr : SchedWriteVariant<
                    [SchedVar<IsRORImmIdiomPred, [V1Write_1c_1I]>,
                     SchedVar<NoSchedPred,       [V1Write_3c_1I_1M]>]>;
def : SchedAlias<WriteExtr, V1WriteExtr>;

// Bitfield move, basic
// Variable shift
def : SchedAlias<WriteIS, V1Write_1c_1I>;

// Bitfield move, insert
def : InstRW<[V1Write_2c_1M], (instregex "^BFM[WX]ri$")>;

// Move immediate
def : SchedAlias<WriteImm, V1Write_1c_1I>;


// Load instructions
// -----------------------------------------------------------------------------

// Load register, immed offset
def : SchedAlias<WriteLD, V1Write_4c_1L>;

// Load register, immed offset, index
def : SchedAlias<WriteLDIdx, V1Write_4c_1L>;
def : SchedAlias<WriteAdr,   V1Write_1c_1I>;

// Load pair, immed offset
def : SchedAlias<WriteLDHi, V1Write_4c_1L>;
def : InstRW<[V1Write_4c_1L, V1Write_0c_0Z], (instrs LDPWi, LDNPWi)>;
def : InstRW<[WriteAdr, V1Write_4c_1L, V1Write_0c_0Z],
             (instrs LDPWpost, LDPWpre)>;

// Load pair, signed immed offset, signed words
def : InstRW<[V1Write_5c_1I_1L, V1Write_0c_0Z], (instrs LDPSWi)>;

// Load pair, immed post or pre-index, signed words
def : InstRW<[WriteAdr, V1Write_5c_1I_1L, V1Write_0c_0Z],
             (instrs LDPSWpost, LDPSWpre)>;


// Store instructions
// -----------------------------------------------------------------------------

// Store register, immed offset
def : SchedAlias<WriteST, V1Write_1c_1L01_1D>;

// Store register, immed offset, index
def : SchedAlias<WriteSTIdx, V1Write_1c_1L01_1D>;

// Store pair, immed offset
def : SchedAlias<WriteSTP, V1Write_1c_1L01_1D>;


// FP data processing instructions
// -----------------------------------------------------------------------------

// FP absolute value
// FP arithmetic
// FP min/max
// FP negate
def : SchedAlias<WriteF, V1Write_2c_1V>;

// FP compare
def : SchedAlias<WriteFCmp, V1Write_2c_1V0>;

// FP divide
// FP square root
def : SchedAlias<WriteFDiv, V1Write_10c7_1V02>;

// FP divide, H-form
// FP square root, H-form
def : InstRW<[V1Write_7c7_1V02], (instrs FDIVHrr, FSQRTHr)>;

// FP divide, S-form
// FP square root, S-form
def : InstRW<[V1Write_10c7_1V02], (instrs FDIVSrr, FSQRTSr)>;

// FP divide, D-form
def : InstRW<[V1Write_15c7_1V02], (instrs FDIVDrr)>;

// FP square root, D-form
def : InstRW<[V1Write_16c7_1V02], (instrs FSQRTDr)>;

// FP multiply
def : SchedAlias<WriteFMul, V1Write_3c_1V>;

// FP multiply accumulate
def : InstRW<[V1Write_4c_1V], (instregex "^FN?M(ADD|SUB)[HSD]rrr$")>;

// FP round to integral
def : InstRW<[V1Write_3c_1V02], (instregex "^FRINT[AIMNPXZ][HSD]r$",
                                           "^FRINT(32|64)[XZ][SD]r$")>;

// FP select
def : InstRW<[V1Write_2c_1V01], (instregex "^FCSEL[HSD]rrr$")>;


// FP miscellaneous instructions
// -----------------------------------------------------------------------------

// FP convert, from gen to vec reg
def : InstRW<[V1Write_3c_1M0], (instregex "^[SU]CVTF[SU][WX][HSD]ri$")>;

// FP convert, from vec to gen reg
def : InstRW<[V1Write_3c_1V0], (instregex "^FCVT[AMNPZ][SU][SU][WX][HSD]r$")>;

// FP convert, Javascript from vec to gen reg
def : InstRW<[V1Write_3c_1V0], (instrs FJCVTZS)>;

// FP convert, from vec to vec reg
def : SchedAlias<WriteFCvt, V1Write_3c_1V02>;

// FP move, immed
def : SchedAlias<WriteFImm, V1Write_2c_1V>;

// FP move, register
def : InstRW<[V1Write_2c_1V], (instrs FMOVHr, FMOVSr, FMOVDr)>;

// FP transfer, from gen to low half of vec reg
def : InstRW<[V1Write_3c_1M0], (instrs FMOVWHr, FMOVXHr, FMOVWSr, FMOVXDr)>;

// FP transfer, from gen to high half of vec reg
def : InstRW<[V1Write_5c_1M0_1V], (instrs FMOVXDHighr)>;

// FP transfer, from vec to gen reg
def : SchedAlias<WriteFCopy, V1Write_2c_1V1>;


// FP load instructions
// -----------------------------------------------------------------------------

// Load vector reg, literal, S/D/Q forms
// Load vector reg, unscaled immed
// Load vector reg, unsigned immed
def : InstRW<[V1Write_6c_1L, ReadAdrBase], (instregex "^LDR[SDQ]l$",
                                                      "^LDUR[BHSDQ]i$",
                                                      "^LDR[BHSDQ]ui$")>;

// Load vector reg, immed post-index
// Load vector reg, immed pre-index
def : InstRW<[WriteAdr, V1Write_6c_1L],
             (instregex "^LDR[BHSDQ](post|pre)$")>;

// Load vector reg, register offset, basic
// Load vector reg, register offset, scale, S/D-form
// Load vector reg, register offset, extend
// Load vector reg, register offset, extend, scale, S/D-form
def : InstRW<[V1Write_6c_1L, ReadAdrBase], (instregex "^LDR[BSD]ro[WX]$")>;

// Load vector reg, register offset, scale, H/Q-form
// Load vector reg, register offset, extend, scale, H/Q-form
def : InstRW<[V1Write_7c_1I_1L, ReadAdrBase], (instregex "^LDR[HQ]ro[WX]$")>;

// Load vector pair, immed offset, S/D-form
def : InstRW<[V1Write_6c_1L, V1Write_0c_0Z], (instregex "^LDN?P[SD]i$")>;

// Load vector pair, immed offset, Q-form
def : InstRW<[V1Write_6c_1L, WriteLDHi], (instrs LDPQi, LDNPQi)>;

// Load vector pair, immed post-index, S/D-form
// Load vector pair, immed pre-index, S/D-form
def : InstRW<[WriteAdr, V1Write_6c_1L, V1Write_0c_0Z],
             (instregex "^LDP[SD](pre|post)$")>;

// Load vector pair, immed post-index, Q-form
// Load vector pair, immed pre-index, Q-form
def : InstRW<[WriteAdr, V1Write_6c_1L, WriteLDHi],
             (instrs LDPQpost, LDPQpre)>;


// FP store instructions
// -----------------------------------------------------------------------------

// Store vector reg, unscaled immed, B/H/S/D/Q-form
def : InstRW<[V1Write_2c_1L01_1V01], (instregex "^STUR[BHSDQ]i$")>;

// Store vector reg, immed post-index, B/H/S/D/Q-form
// Store vector reg, immed pre-index, B/H/S/D/Q-form
def : InstRW<[WriteAdr, V1Write_2c_1L01_1V01],
             (instregex "^STR[BHSDQ](pre|post)$")>;

// Store vector reg, unsigned immed, B/H/S/D/Q-form
def : InstRW<[V1Write_2c_1L01_1V01], (instregex "^STR[BHSDQ]ui$")>;

// Store vector reg, register offset, basic, B/S/D-form
// Store vector reg, register offset, scale, B/S/D-form
// Store vector reg, register offset, extend, B/S/D-form
// Store vector reg, register offset, extend, scale, B/S/D-form
def : InstRW<[V1Write_2c_1L01_1V01, ReadAdrBase],
             (instregex "^STR[BSD]ro[WX]$")>;

// Store vector reg, register offset, basic, H/Q-form
// Store vector reg, register offset, scale, H/Q-form
// Store vector reg, register offset, extend, H/Q-form
// Store vector reg, register offset, extend, scale, H/Q-form
def : InstRW<[V1Write_2c_1I_1L01_1V01, ReadAdrBase],
             (instregex "^STR[HQ]ro[WX]$")>;

// Store vector pair, immed offset, S/D/Q-form
def : InstRW<[V1Write_2c_1L01_1V01], (instregex "^STN?P[SDQ]i$")>;

// Store vector pair, immed post-index, S/D-form
// Store vector pair, immed pre-index, S/D-form
def : InstRW<[WriteAdr, V1Write_2c_1L01_1V01],
             (instregex "^STP[SD](pre|post)$")>;

// Store vector pair, immed post-index, Q-form
// Store vector pair, immed pre-index, Q-form
def : InstRW<[WriteAdr, V1Write_2c_2L01_1V01], (instrs STPQpre, STPQpost)>;


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
def : SchedAlias<WriteVd, V1Write_2c_1V>;
def : SchedAlias<WriteVq, V1Write_2c_1V>;

// ASIMD absolute diff accum
// ASIMD absolute diff accum long
// ASIMD pairwise add and accumulate long
def : InstRW<[V1Write_4c_1V13], (instregex "^[SU]ABAL?v", "^[SU]ADALPv")>;

// ASIMD arith, reduce, 4H/4S
// ASIMD max/min, reduce, 4H/4S
def : InstRW<[V1Write_2c_1V13], (instregex "^(ADD|[SU]ADDL)Vv4(i16|i32)v$",
                                           "^[SU](MAX|MIN)Vv4(i16|i32)v$")>;

// ASIMD arith, reduce, 8B/8H
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[V1Write_4c_1V13_1V], (instregex "^(ADD|[SU]ADDL)Vv8(i8|i16)v$",
                                              "^[SU](MAX|MIN)Vv8(i8|i16)v$")>;

// ASIMD arith, reduce, 16B
// ASIMD max/min, reduce, 16B
def : InstRW<[V1Write_4c_2V13], (instregex "^(ADD|[SU]ADDL)Vv16i8v$",
                                           "[SU](MAX|MIN)Vv16i8v$")>;

// ASIMD dot product
// ASIMD dot product using signed and unsigned integers
def : InstRW<[V1Write_2c_1V], (instregex "^([SU]|SU|US)DOT(lane)?v(8|16)i8$")>;

// ASIMD matrix multiply- accumulate
def : InstRW<[V1Write_3c_1V], (instrs SMMLA, UMMLA, USMMLA)>;

// ASIMD multiply
// ASIMD multiply accumulate
// ASIMD multiply accumulate long
// ASIMD multiply accumulate high
// ASIMD multiply accumulate saturating long
def : InstRW<[V1Write_4c_1V02], 
             (instregex "^MUL(v[148]i16|v[124]i32)$",
                        "^SQR?DMULH(v[48]i16|v[24]i32)$",
                        "^ML[AS](v[148]i16|v[124]i32)$",
                        "^[SU]ML[AS]Lv",
                        "^SQRDML[AS]H(v[148]i16|v[124]i32)$",
                        "^SQDML[AS]Lv")>;

// ASIMD multiply/multiply long (8x8) polynomial
def : InstRW<[V1Write_3c_1V01], (instregex "^PMULL?v(8|16)i8$")>;

// ASIMD multiply long
def : InstRW<[V1Write_3c_1V02], (instregex "^([SU]|SQD)MULLv")>;

// ASIMD shift accumulate
// ASIMD shift by immed, complex
// ASIMD shift by register, complex
def : InstRW<[V1Write_4c_1V13],
             (instregex "^[SU]R?SRAv",
                        "^RSHRNv", "^SQRSHRU?Nv", "^(SQSHLU?|UQSHL)[bhsd]$",
                        "^(SQSHLU?|UQSHL)(v8i8|v16i8|v4i16|v8i16|v2i32|v4i32|v2i64)_shift$",
                        "^SQSHU?RNv", "^[SU]RSHRv", "^UQR?SHRNv", 
                        "^[SU]Q?RSHLv", "^[SU]QSHLv")>;

// ASIMD shift by immed, basic
// ASIMD shift by immed and insert, basic
// ASIMD shift by register, basic
def : InstRW<[V1Write_2c_1V13], (instregex "^SHLL?v", "^SHRNv", "^[SU]SHLLv",
                                          "^[SU]SHRv", "^S[LR]Iv", "^[SU]SHLv")>;


// ASIMD FP instructions
// -----------------------------------------------------------------------------

// ASIMD FP absolute value/difference
// ASIMD FP arith, normal
// ASIMD FP compare
// ASIMD FP complex add
// ASIMD FP max/min, normal
// ASIMD FP max/min, pairwise
// ASIMD FP negate
// Covered by "SchedAlias (WriteV[dq]...)" above

// ASIMD FP complex multiply add
// ASIMD FP multiply accumulate
def : InstRW<[V1Write_4c_1V], (instregex "^FCADD(v[48]f16|v[24]f32|v2f64)$",
                                         "^FML[AS]v")>;

// ASIMD FP convert, long (F16 to F32)
def : InstRW<[V1Write_4c_2V02], (instregex "^FCVTLv[48]i16$")>;

// ASIMD FP convert, long (F32 to F64)
def : InstRW<[V1Write_3c_1V02], (instregex "^FCVTLv[24]i32$")>;

// ASIMD FP convert, narrow (F32 to F16)
def : InstRW<[V1Write_4c_2V02], (instregex "^FCVTNv[48]i16$")>;

// ASIMD FP convert, narrow (F64 to F32)
def : InstRW<[V1Write_3c_1V02], (instregex "^FCVTNv[24]i32$",
                                           "^FCVTXN(v[24]f32|v1i64)$")>;

// ASIMD FP convert, other, D-form F32 and Q-form F64
def : InstRW<[V1Write_3c_1V02], (instregex "^[FSU]CVT[AMNPZ][SU]v2f(32|64)$",
                                           "^[SU]CVTFv2f(32|64)$")>;

// ASIMD FP convert, other, D-form F16 and Q-form F32
def : InstRW<[V1Write_4c_2V02], (instregex "^[FSU]CVT[AMNPZ][SU]v4f(16|32)$",
                                           "^[SU]CVTFv4f(16|32)$")>;

// ASIMD FP convert, other, Q-form F16
def : InstRW<[V1Write_6c_4V02], (instregex "^[FSU]CVT[AMNPZ][SU]v8f16$",
                                           "^[SU]CVTFv8f16$")>;

// ASIMD FP divide, D-form, F16
// ASIMD FP square root, D-form, F16
def : InstRW<[V1Write_7c7_1V02], (instrs FDIVv4f16, FSQRTv4f16)>;

// ASIMD FP divide, F32
// ASIMD FP square root, F32
def : InstRW<[V1Write_10c7_1V02], (instrs FDIVv2f32, FDIVv4f32,
                                          FSQRTv2f32, FSQRTv4f32)>;

// ASIMD FP divide, Q-form, F16
def : InstRW<[V1Write_13c5_1V02], (instrs FDIVv8f16)>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[V1Write_15c7_1V02], (instrs FDIVv2f64)>;

// ASIMD FP square root, Q-form, F16
def : InstRW<[V1Write_13c11_1V02], (instrs FSQRTv8f16)>;

// ASIMD FP square root, Q-form, F64
def : InstRW<[V1Write_16c7_1V02], (instrs FSQRTv2f64)>;

// ASIMD FP max/min, reduce, F32 and D-form F16
def : InstRW<[V1Write_4c_2V], (instregex "^F(MAX|MIN)(NM)?Vv4(i16|i32)v$")>;

// ASIMD FP max/min, reduce, Q-form F16
def : InstRW<[V1Write_6c_3V], (instregex "^F(MAX|MIN)(NM)?Vv8i16v$")>;

// ASIMD FP multiply
def : InstRW<[V1Write_3c_1V], (instregex "^FMULX?v")>;

// ASIMD FP multiply accumulate long
def : InstRW<[V1Write_5c_1V], (instregex "^FML[AS]L2?v")>;

// ASIMD FP round, D-form F32 and Q-form F64
def : InstRW<[V1Write_3c_1V02], (instregex "^FRINT[AIMNPXZ]v2f(32|64)$")>;

// ASIMD FP round, D-form F16 and Q-form F32
def : InstRW<[V1Write_4c_2V02], (instregex "^FRINT[AIMNPXZ]v4f(16|32)$")>;

// ASIMD FP round, Q-form F16
def : InstRW<[V1Write_6c_4V02], (instregex "^FRINT[AIMNPXZ]v8f16$")>;


// ASIMD BF instructions
// -----------------------------------------------------------------------------

// ASIMD convert, F32 to BF16
def : InstRW<[V1Write_4c_1V02], (instrs BFCVTN, BFCVTN2)>;

// ASIMD dot product
def : InstRW<[V1Write_4c_1V], (instregex "^BF(DOT|16DOTlane)v[48]bf16$")>;

// ASIMD matrix multiply accumulate
def : InstRW<[V1Write_5c_1V], (instrs BFMMLA)>;

// ASIMD multiply accumulate long
def : InstRW<[V1Write_4c_1V], (instregex "^BFMLAL[BT](Idx)?$")>;

// Scalar convert, F32 to BF16
def : InstRW<[V1Write_3c_1V02], (instrs BFCVT)>;


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
def : InstRW<[V1Write_3c_1M0],
             (instregex "^DUP((v16|v8)i8|(v8|v4)i16|(v4|v2)i32|v2i64)gpr$")>;

// ASIMD extract narrow, saturating
def : InstRW<[V1Write_4c_1V13], (instregex "^[SU]QXTNv", "^SQXTUNv")>;

// ASIMD reciprocal and square root estimate, D-form U32
// ASIMD reciprocal and square root estimate, D-form F32 and F64
def : InstRW<[V1Write_3c_1V02], (instrs URECPEv2i32,
                                        URSQRTEv2i32,
                                        FRECPEv1i32, FRECPEv2f32, FRECPEv1i64,
                                        FRSQRTEv1i32, FRSQRTEv2f32, FRSQRTEv1i64)>;

// ASIMD reciprocal and square root estimate, Q-form U32
// ASIMD reciprocal and square root estimate, D-form F16 and Q-form F32 and F64
def : InstRW<[V1Write_4c_1V02], (instrs URECPEv4i32,
                                        URSQRTEv4i32,
                                        FRECPEv1f16, FRECPEv4f16,
                                        FRECPEv4f32, FRECPEv2f64,
                                        FRSQRTEv1f16, FRSQRTEv4f16,
                                        FRSQRTEv4f32, FRSQRTEv2f64)>;

// ASIMD reciprocal and square root estimate, Q-form F16
def : InstRW<[V1Write_6c_2V02], (instrs FRECPEv8f16,
                                        FRSQRTEv8f16)>;

// ASIMD reciprocal exponent
def : InstRW<[V1Write_3c_1V02], (instrs FRECPXv1f16, FRECPXv1i32, FRECPXv1i64)>;

// ASIMD reciprocal step
def : InstRW<[V1Write_4c_1V], (instregex "^FRECPS(16|32|64)$", "^FRECPSv",
                                         "^FRSQRTS(16|32|64)$", "^FRSQRTSv")>;

// ASIMD table lookup, 1 or 2 table regs
// ASIMD table lookup extension, 1 table reg
def : InstRW<[V1Write_2c_2V01], (instregex "^TBLv(8|16)i8(One|Two)$",
                                           "^TBXv(8|16)i8One$")>;

// ASIMD table lookup, 3 table regs
// ASIMD table lookup extension, 2 table reg
def : InstRW<[V1Write_4c_2V01], (instrs TBLv8i8Three, TBLv16i8Three,
                                        TBXv8i8Two, TBXv16i8Two)>;

// ASIMD table lookup, 4 table regs
def : InstRW<[V1Write_4c_3V01], (instrs TBLv8i8Four, TBLv16i8Four)>;

// ASIMD table lookup extension, 3 table reg
def : InstRW<[V1Write_6c_3V01], (instrs TBXv8i8Three, TBXv16i8Three)>;

// ASIMD table lookup extension, 4 table reg
def : InstRW<[V1Write_6c_5V01], (instrs TBXv8i8Four, TBXv16i8Four)>;

// ASIMD transfer, element to gen reg
def : InstRW<[V1Write_2c_1V], (instregex "^SMOVvi(((8|16)to(32|64))|32to64)$",
                                         "^UMOVvi(8|16|32|64)$")>;

// ASIMD transfer, gen reg to element
def : InstRW<[V1Write_5c_1M0_1V], (instregex "^INSvi(8|16|32|64)gpr$")>;


// ASIMD load instructions
// -----------------------------------------------------------------------------

// ASIMD load, 1 element, multiple, 1 reg
def : InstRW<[V1Write_6c_1L],
             (instregex "^LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_6c_1L],
             (instregex "^LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg
def : InstRW<[V1Write_6c_2L],
             (instregex "^LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_6c_2L],
             (instregex "^LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg
def : InstRW<[V1Write_6c_3L],
             (instregex "^LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_6c_3L],
             (instregex "^LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form
def : InstRW<[V1Write_6c_2L],
             (instregex "^LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, V1Write_6c_2L],
             (instregex "^LD1Fourv(8b|4h|2s|1d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, Q-form
def : InstRW<[V1Write_7c_4L],
             (instregex "^LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_7c_4L],
             (instregex "^LD1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, one lane
// ASIMD load, 1 element, all lanes
def : InstRW<[V1Write_8c_1L_1V],
             (instregex "^LD1(i|Rv)(8|16|32|64)$",
                        "^LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_1L_1V],
             (instregex "^LD1i(8|16|32|64)_POST$",
                        "^LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form
def : InstRW<[V1Write_8c_1L_2V],
             (instregex "^LD2Twov(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, V1Write_8c_1L_2V],
             (instregex "^LD2Twov(8b|4h|2s)_POST$")>;
                        
// ASIMD load, 2 element, multiple, Q-form
def : InstRW<[V1Write_8c_2L_2V],
             (instregex "^LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_2L_2V],
             (instregex "^LD2Twov(16b|8h|4s|2d)_POST$")>;
                        
// ASIMD load, 2 element, one lane
// ASIMD load, 2 element, all lanes
def : InstRW<[V1Write_8c_1L_2V],
             (instregex "^LD2i(8|16|32|64)$",
                        "^LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_1L_2V],
             (instregex "^LD2i(8|16|32|64)_POST$",
                        "^LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
                        
// ASIMD load, 3 element, multiple, D-form
// ASIMD load, 3 element, one lane
// ASIMD load, 3 element, all lanes
def : InstRW<[V1Write_8c_2L_3V],
             (instregex "^LD3Threev(8b|4h|2s)$",
                        "^LD3i(8|16|32|64)$",
                        "^LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_2L_3V],
             (instregex "^LD3Threev(8b|4h|2s)_POST$",
                        "^LD3i(8|16|32|64)_POST$",
                        "^LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 3 element, multiple, Q-form
def : InstRW<[V1Write_8c_3L_3V],
             (instregex "^LD3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_3L_3V],
             (instregex "^LD3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form
// ASIMD load, 4 element, one lane
// ASIMD load, 4 element, all lanes
def : InstRW<[V1Write_8c_3L_4V],
             (instregex "^LD4Fourv(8b|4h|2s)$",
                        "^LD4i(8|16|32|64)$",
                        "^LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteAdr, V1Write_8c_3L_4V],
             (instregex "^LD4Fourv(8b|4h|2s)_POST$",
                        "^LD4i(8|16|32|64)_POST$",
                        "^LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD load, 4 element, multiple, Q-form
def : InstRW<[V1Write_9c_4L_4V],
             (instregex "^LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_9c_4L_4V],
             (instregex "^LD4Fourv(16b|8h|4s|2d)_POST$")>;


// ASIMD store instructions
// -----------------------------------------------------------------------------

// ASIMD store, 1 element, multiple, 1 reg
// ASIMD store, 1 element, multiple, 2 reg, D-form
def : InstRW<[V1Write_2c_1L01_1V01],
             (instregex "^ST1Onev(8b|16b|4h|8h|2s|4s|1d|2d)$",
                        "^ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, V1Write_2c_1L01_1V01],
             (instregex "^ST1Onev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$",
                        "^ST1Twov(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, Q-form
// ASIMD store, 1 element, multiple, 3 reg, D-form
// ASIMD store, 1 element, multiple, 4 reg, D-form
def : InstRW<[V1Write_2c_2L01_2V01],
             (instregex "^ST1Twov(16b|8h|4s|2d)$",
                        "^ST1Threev(8b|4h|2s|1d)$",
                        "^ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteAdr, V1Write_2c_2L01_2V01],
             (instregex "^ST1Twov(16b|8h|4s|2d)_POST$",
                        "^ST1Threev(8b|4h|2s|1d)_POST$",
                        "^ST1Fourv(8b|4h|2s|1d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[V1Write_2c_3L01_3V01],
             (instregex "^ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_2c_3L01_3V01],
             (instregex "^ST1Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[V1Write_2c_4L01_4V01],
             (instregex "^ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_2c_4L01_4V01],
             (instregex "^ST1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane
// ASIMD store, 2 element, multiple, D-form
// ASIMD store, 2 element, one lane
def : InstRW<[V1Write_4c_1L01_1V01],
             (instregex "^ST1i(8|16|32|64)$",
                        "^ST2Twov(8b|4h|2s)$",
                        "^ST2i(8|16|32|64)$")>;
def : InstRW<[WriteAdr, V1Write_4c_1L01_1V01],
             (instregex "^ST1i(8|16|32|64)_POST$",
                        "^ST2Twov(8b|4h|2s)_POST$",
                        "^ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, Q-form
// ASIMD store, 3 element, multiple, D-form
// ASIMD store, 3 element, one lane
// ASIMD store, 4 element, one lane, D
def : InstRW<[V1Write_4c_2L01_2V01],
             (instregex "^ST2Twov(16b|8h|4s|2d)$",
                        "^ST3Threev(8b|4h|2s)$",
                        "^ST3i(8|16|32|64)$",
                        "^ST4i64$")>;
def : InstRW<[WriteAdr, V1Write_4c_2L01_2V01],
             (instregex "^ST2Twov(16b|8h|4s|2d)_POST$",
                        "^ST3Threev(8b|4h|2s)_POST$",
                        "^ST3i(8|16|32|64)_POST$",
                        "^ST4i64_POST$")>;

// ASIMD store, 3 element, multiple, Q-form
def : InstRW<[V1Write_5c_3L01_3V01],
             (instregex "^ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteAdr, V1Write_5c_3L01_3V01],
             (instregex "^ST3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 4 element, multiple, D-form
def : InstRW<[V1Write_6c_3L01_3V01],
             (instregex "^ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[WriteAdr, V1Write_6c_3L01_3V01],
             (instregex "^ST4Fourv(8b|4h|2s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, B/H/S
def : InstRW<[V1Write_7c_6L01_6V01],
             (instregex "^ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[WriteAdr, V1Write_7c_6L01_6V01],
             (instregex "^ST4Fourv(16b|8h|4s)_POST$")>;

// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[V1Write_4c_4L01_4V01],
             (instrs ST4Fourv2d)>;
def : InstRW<[WriteAdr, V1Write_4c_4L01_4V01],
             (instrs ST4Fourv2d_POST)>;

// ASIMD store, 4 element, one lane, B/H/S
def : InstRW<[V1Write_6c_3L_3V],
             (instregex "^ST4i(8|16|32)$")>;
def : InstRW<[WriteAdr, V1Write_6c_3L_3V],
             (instregex "^ST4i(8|16|32)_POST$")>;


// Cryptography extensions
// -----------------------------------------------------------------------------

// Crypto polynomial (64x64) multiply long
// Covered by "SchedAlias (WriteV[dq]...)" above

// Crypto AES ops
def V1WriteVC : WriteSequence<[V1Write_2c_1V]>;
def V1ReadVC  : SchedReadAdvance<2, [V1WriteVC]>;
def           : InstRW<[V1WriteVC], (instrs AESDrr, AESErr)>;
def           : InstRW<[V1Write_2c_1V, V1ReadVC], (instrs AESMCrr, AESIMCrr)>;

// Crypto SHA1 hash acceleration op
// Crypto SHA1 schedule acceleration ops
// Crypto SHA256 schedule acceleration ops
// Crypto SHA512 hash acceleration ops
// Crypto SM3 ops
def : InstRW<[V1Write_2c_1V0], (instregex "^SHA1(H|SU[01])rr$",
                                          "^SHA256SU[01]rr$",
                                          "^SHA512(H2?|SU[01])$",
                                          "^SM3(PARTW(1|2SM3SS1)|TT[12][AB])$")>;

// Crypto SHA1 hash acceleration ops
// Crypto SHA256 hash acceleration ops
// Crypto SM4 ops
def : InstRW<[V1Write_4c_1V0], (instregex "^SHA1[CMP]rrr$",
                                          "^SHA256H2?rrr$",
                                          "^SM4E(KEY)?$")>;

// Crypto SHA3 ops
def : InstRW<[V1Write_2c_1V0], (instrs BCAX, EOR3, RAX1, XAR)>;


// CRC instruction
// -----------------------------------------------------------------------------

// CRC checksum ops
def : InstRW<[V1Write_2c_1M0], (instregex "^CRC32C?[BHWX]rr$")>;


// SVE Predicate instructions
// -----------------------------------------------------------------------------

// Loop control, based on predicate
def : InstRW<[V1Write_2c_1M0], (instregex "^BRK[AB]_PP[mz]P$")>;
def : InstRW<[V1Write_2c_1M0], (instrs BRKN_PPzP, BRKPA_PPzPP, BRKPB_PPzPP)>;

// Loop control, based on predicate and flag setting
def : InstRW<[V1Write_3c_2M0], (instrs BRKAS_PPzP, BRKBS_PPzP, BRKNS_PPzP,
                                       BRKPAS_PPzPP, BRKPBS_PPzPP)>;

// Loop control, based on GPR
def : InstRW<[V1Write_3c_2M0], (instregex "^WHILE(LE|LO|LS|LT)_P(WW|XX)_[BHSD]$")>;

// Loop terminate
def : InstRW<[V1Write_1c_1M0], (instregex "^CTERM(EQ|NE)_(WW|XX)$")>;

// Predicate counting scalar
// Predicate counting scalar, active predicate
def : InstRW<[V1Write_2c_1M0], (instrs ADDPL_XXI, ADDVL_XXI, RDVLI_XI)>;
def : InstRW<[V1Write_2c_1M0], (instregex "^(CNT|([SU]Q)?(DEC|INC))[BHWD]_XPiI$",
                                          "^SQ(DEC|INC)[BHWD]_XPiWdI$",
                                          "^UQ(DEC|INC)[BHWD]_WPiI$",
                                          "^CNTP_XPP_[BHSD]$",
                                          "^([SU]Q)?(DEC|INC)P_XP_[BHSD]$",
                                          "^UQ(DEC|INC)P_WP_[BHSD]$",
                                          "^[SU]Q(DEC|INC)P_XPWd_[BHSD]$")>;

// Predicate counting vector, active predicate
def : InstRW<[V1Write_7c_2M0_1V01], (instregex "^([SU]Q)?(DEC|INC)P_ZP_[HSD]$")>;

// Predicate logical
def : InstRW<[V1Write_1c_1M0],
             (instregex "^(AND|BIC|EOR|NAND|NOR|ORN|ORR)_PPzPP$")>;

// Predicate logical, flag setting
def : InstRW<[V1Write_2c_2M0],
             (instregex "^(AND|BIC|EOR|NAND|NOR|ORN|ORR)S_PPzPP$")>;

// Predicate reverse
// Predicate set/initialize/find next
// Predicate transpose
// Predicate unpack and widen
// Predicate zip/unzip
def : InstRW<[V1Write_2c_1M0], (instregex "^REV_PP_[BHSD]$",
                                          "^PFALSE$", "^PFIRST_B$",
                                          "^PNEXT_[BHSD]$", "^PTRUE_[BHSD]$",
                                          "^TRN[12]_PPP_[BHSDQ]$",
                                          "^(ZIP|UZP)[12]_PPP_[BHSDQ]$")>;

// Predicate set/initialize/find next
// Predicate unpack and widen
def : InstRW<[V1Write_2c_1M0], (instrs PTEST_PP,
                                       PUNPKHI_PP, PUNPKLO_PP)>;

// Predicate select
def : InstRW<[V1Write_1c_1M0], (instrs SEL_PPPP)>;

// Predicate set/initialize, set flags
def : InstRW<[V1Write_3c_2M0], (instregex "^PTRUES_[BHSD]$")>;



// SVE integer instructions
// -----------------------------------------------------------------------------

// Arithmetic, basic
// Logical
def : InstRW<[V1Write_2c_1V01],
             (instregex "^(ABS|CNOT|NEG)_ZPmZ_[BHSD]",
                        "^(ADD|SUB)_Z(I|P[mZ]Z|ZZ)_[BHSD]",
                        "^ADR_[SU]XTW_ZZZ_D_[0123]$",
                        "^ADR_LSL_ZZZ_[SD]_[0123]$",
                        "^[SU]ABD_ZP[mZ]Z_[BHSD]",
                        "^[SU](MAX|MIN)_Z(I|P[mZ]Z)_[BHSD]",
                        "^[SU]Q(ADD|SUB)_Z(I|ZZ)_[BHSD]$",
                        "^SUBR_Z(I|P[mZ]Z)_[BHSD]",
                        "^(AND|EOR|ORR)_ZI$",
                        "^(AND|BIC|EOR|EOR(BT|TB)?|ORR)_ZP?ZZ",
                        "^EOR(BT|TB)_ZZZ_[BHSD]$",
                        "^(AND|BIC|EOR|NOT|ORR)_ZPmZ_[BHSD]")>;

// Arithmetic, shift
def : InstRW<[V1Write_2c_1V1],
             (instregex "^(ASR|LSL|LSR)_WIDE_Z(Pm|Z)Z_[BHS]",
                        "^(ASR|LSL|LSR)_ZPm[IZ]_[BHSD]",
                        "^(ASR|LSL|LSR)_ZZI_[BHSD]",
                        "^(ASR|LSL|LSR)_ZPZ[IZ]_[BHSD]",
                        "^(ASRR|LSLR|LSRR)_ZPmZ_[BHSD]")>;

// Arithmetic, shift right for divide
def : InstRW<[V1Write_4c_1V1], (instregex "^ASRD_(ZPmI|ZPZI)_[BHSD]")>;

// Count/reverse bits
def : InstRW<[V1Write_2c_1V01], (instregex "^(CLS|CLZ|CNT|RBIT)_ZPmZ_[BHSD]")>;

// Broadcast logical bitmask immediate to vector
def : InstRW<[V1Write_2c_1V01], (instrs DUPM_ZI)>;

// Compare and set flags
def : InstRW<[V1Write_4c_1M0_1V0],
             (instregex "^CMP(EQ|GE|GT|HI|HS|LE|LO|LS|LT|NE)_PPzZ[IZ]_[BHSD]$",
                        "^CMP(EQ|GE|GT|HI|HS|LE|LO|LS|LT|NE)_WIDE_PPzZZ_[BHS]$")>;

// Conditional extract operations, scalar form
def : InstRW<[V1Write_9c_1M0_1V1], (instregex "^CLAST[AB]_RPZ_[BHSD]$")>;

// Conditional extract operations, SIMD&FP scalar and vector forms
def : InstRW<[V1Write_3c_1V1], (instregex "^CLAST[AB]_[VZ]PZ_[BHSD]$",
                                          "^COMPACT_ZPZ_[SD]$",
                                          "^SPLICE_ZPZZ?_[BHSD]$")>;

// Convert to floating point, 64b to float or convert to double
def : InstRW<[V1Write_3c_1V0], (instregex "^[SU]CVTF_ZPmZ_Dto[HSD]",
                                          "^[SU]CVTF_ZPmZ_StoD")>;

// Convert to floating point, 32b to single or half
def : InstRW<[V1Write_4c_2V0], (instregex "^[SU]CVTF_ZPmZ_Sto[HS]")>;

// Convert to floating point, 16b to half
def : InstRW<[V1Write_6c_4V0], (instregex "^[SU]CVTF_ZPmZ_HtoH")>;

// Copy, scalar
def : InstRW<[V1Write_5c_1M0_1V01], (instregex "^CPY_ZPmR_[BHSD]$")>;

// Copy, scalar SIMD&FP or imm
def : InstRW<[V1Write_2c_1V01], (instregex "^CPY_ZP([mz]I|mV)_[BHSD]$")>;

// Divides, 32 bit
def : InstRW<[V1Write_12c7_1V0], (instregex "^[SU]DIVR?_ZPmZ_S",
                                             "^[SU]DIV_ZPZZ_S")>;

// Divides, 64 bit
def : InstRW<[V1Write_20c7_1V0], (instregex "^[SU]DIVR?_ZPmZ_D",
                                             "^[SU]DIV_ZPZZ_D")>;

// Dot product, 8 bit
def : InstRW<[V1Write_3c_1V01], (instregex "^[SU]DOT_ZZZI?_S$")>;

// Dot product, 8 bit, using signed and unsigned integers
def : InstRW<[V1Write_3c_1V], (instrs SUDOT_ZZZI, USDOT_ZZZ, USDOT_ZZZI)>;

// Dot product, 16 bit
def : InstRW<[V1Write_4c_1V0], (instregex "^[SU]DOT_ZZZI?_D$")>;

// Duplicate, immediate and indexed form
def : InstRW<[V1Write_2c_1V01], (instregex "^DUP_ZI_[BHSD]$",
                                           "^DUP_ZZI_[BHSDQ]$")>;

// Duplicate, scalar form
def : InstRW<[V1Write_3c_1M0], (instregex "^DUP_ZR_[BHSD]$")>;

// Extend, sign or zero
def : InstRW<[V1Write_2c_1V1], (instregex "^[SU]XTB_ZPmZ_[HSD]",
                                          "^[SU]XTH_ZPmZ_[SD]",
                                          "^[SU]XTW_ZPmZ_[D]")>;

// Extract
def : InstRW<[V1Write_2c_1V01], (instrs EXT_ZZI)>;

// Extract/insert operation, SIMD and FP scalar form
def : InstRW<[V1Write_3c_1V1], (instregex "^LAST[AB]_VPZ_[BHSD]$",
                                          "^INSR_ZV_[BHSD]$")>;

// Extract/insert operation, scalar
def : InstRW<[V1Write_6c_1M0_1V1], (instregex "^LAST[AB]_RPZ_[BHSD]$",
                                              "^INSR_ZR_[BHSD]$")>;

// Horizontal operations, B, H, S form, imm, imm
def : InstRW<[V1Write_4c_1V0], (instregex "^INDEX_II_[BHS]$")>;

// Horizontal operations, B, H, S form, scalar, imm / scalar / imm, scalar
def : InstRW<[V1Write_7c_1M0_1V0], (instregex "^INDEX_(IR|RI|RR)_[BHS]$")>;

// Horizontal operations, D form, imm, imm
def : InstRW<[V1Write_5c_2V0], (instrs INDEX_II_D)>;

// Horizontal operations, D form, scalar, imm / scalar / imm, scalar
def : InstRW<[V1Write_8c_2M0_2V0], (instregex "^INDEX_(IR|RI|RR)_D$")>;

// Move prefix
def : InstRW<[V1Write_2c_1V01], (instregex "^MOVPRFX_ZP[mz]Z_[BHSD]$",
                                           "^MOVPRFX_ZZ$")>;

// Matrix multiply-accumulate
def : InstRW<[V1Write_3c_1V01], (instrs SMMLA_ZZZ, UMMLA_ZZZ, USMMLA_ZZZ)>;

// Multiply, B, H, S element size
def : InstRW<[V1Write_4c_1V0], (instregex "^MUL_(ZI|ZPmZ|ZZZI|ZZZ)_[BHS]",
                                          "^MUL_ZPZZ_[BHS]",
                                          "^[SU]MULH_(ZPmZ|ZZZ)_[BHS]",
                                          "^[SU]MULH_ZPZZ_[BHS]")>;

// Multiply, D element size
// Multiply accumulate, D element size
def : InstRW<[V1Write_5c_2V0], (instregex "^MUL_(ZI|ZPmZ|ZZZI|ZZZ)_D",
                                          "^MUL_ZPZZ_D",
                                          "^[SU]MULH_(ZPmZ|ZZZ)_D",
                                          "^[SU]MULH_ZPZZ_D",
                                          "^(MLA|MLS|MAD|MSB)_(ZPmZZ|ZPZZZ)_D")>;

// Multiply accumulate, B, H, S element size
// NOTE: This is not specified in the SOG.
def : InstRW<[V1Write_4c_1V0], (instregex "^(ML[AS]|MAD|MSB)_(ZPmZZ|ZPZZZ)_[BHS]")>;

// Predicate counting vector
def : InstRW<[V1Write_2c_1V0], (instregex "^([SU]Q)?(DEC|INC)[HWD]_ZPiI$")>;

// Reduction, arithmetic, B form
def : InstRW<[V1Write_14c_1V_1V0_2V1_1V13],
             (instregex "^[SU](ADD|MAX|MIN)V_VPZ_B")>;

// Reduction, arithmetic, H form
def : InstRW<[V1Write_12c_1V_1V01_2V1],
             (instregex "^[SU](ADD|MAX|MIN)V_VPZ_H")>;

// Reduction, arithmetic, S form
def : InstRW<[V1Write_10c_1V_1V01_2V1],
             (instregex "^[SU](ADD|MAX|MIN)V_VPZ_S")>;

// Reduction, arithmetic, D form
def : InstRW<[V1Write_8c_1V_1V01],
             (instregex "^[SU](ADD|MAX|MIN)V_VPZ_D")>;

// Reduction, logical
def : InstRW<[V1Write_12c_4V01], (instregex "^(AND|EOR|OR)V_VPZ_[BHSD]$")>;

// Reverse, vector
def : InstRW<[V1Write_2c_1V01], (instregex "^REV_ZZ_[BHSD]$",
                                           "^REVB_ZPmZ_[HSD]$",
                                           "^REVH_ZPmZ_[SD]$",
                                           "^REVW_ZPmZ_D$")>;

// Select, vector form
// Table lookup
// Table lookup extension
// Transpose, vector form
// Unpack and extend
// Zip/unzip
def : InstRW<[V1Write_2c_1V01], (instregex "^SEL_ZPZZ_[BHSD]$",
                                           "^TB[LX]_ZZZ_[BHSD]$",
                                           "^TRN[12]_ZZZ_[BHSDQ]$",
                                           "^[SU]UNPK(HI|LO)_ZZ_[HSD]$",
                                           "^(UZP|ZIP)[12]_ZZZ_[BHSDQ]$")>;


// SVE floating-point instructions
// -----------------------------------------------------------------------------

// Floating point absolute value/difference
def : InstRW<[V1Write_2c_1V01], (instregex "^FAB[SD]_ZPmZ_[HSD]",
                                           "^FABD_ZPZZ_[HSD]",
                                           "^FABS_ZPmZ_[HSD]")>;

// Floating point arithmetic
def : InstRW<[V1Write_2c_1V01], (instregex "^F(ADD|SUB)_(ZPm[IZ]|ZZZ)_[HSD]",
                                           "^F(ADD|SUB)_ZPZ[IZ]_[HSD]",
                                           "^FADDP_ZPmZZ_[HSD]",
                                           "^FNEG_ZPmZ_[HSD]",
                                           "^FSUBR_ZPm[IZ]_[HSD]",
                                           "^FSUBR_(ZPZI|ZPZZ)_[HSD]")>;

// Floating point associative add, F16
def : InstRW<[V1Write_19c_18V0], (instrs FADDA_VPZ_H)>;

// Floating point associative add, F32
def : InstRW<[V1Write_11c_10V0], (instrs FADDA_VPZ_S)>;

// Floating point associative add, F64
def : InstRW<[V1Write_8c_3V01], (instrs FADDA_VPZ_D)>;

// Floating point compare
def : InstRW<[V1Write_2c_1V0], (instregex "^FAC(GE|GT)_PPzZZ_[HSD]$",
                                          "^FCM(EQ|GE|GT|NE|UO)_PPzZZ_[HSD]$",
                                          "^FCM(EQ|GE|GT|LE|LT|NE)_PPzZ0_[HSD]$")>;

// Floating point complex add
def : InstRW<[V1Write_3c_1V01], (instregex "^FCADD_ZPmZ_[HSD]$")>;

// Floating point complex multiply add
def : InstRW<[V1Write_5c_1V01], (instregex "^FCMLA_ZPmZZ_[HSD]$",
                                           "^FCMLA_ZZZI_[HS]$")>;

// Floating point convert, long or narrow (F16 to F32 or F32 to F16)
// Floating point convert to integer, F32
def : InstRW<[V1Write_4c_2V0], (instregex "^FCVT_ZPmZ_(HtoS|StoH)",
                                          "^FCVTZ[SU]_ZPmZ_(HtoS|StoS)")>;

// Floating point convert, long or narrow (F16 to F64, F32 to F64, F64 to F32 or F64 to F16)
// Floating point convert to integer, F64
def : InstRW<[V1Write_3c_1V0], (instregex "^FCVT_ZPmZ_(HtoD|StoD|DtoS|DtoH)",
                                          "^FCVTZ[SU]_ZPmZ_(HtoD|StoD|DtoS|DtoD)")>;

// Floating point convert to integer, F16
def : InstRW<[V1Write_6c_4V0], (instregex "^FCVTZ[SU]_ZPmZ_HtoH")>;

// Floating point copy
def : InstRW<[V1Write_2c_1V01], (instregex "^FCPY_ZPmI_[HSD]$",
                                           "^FDUP_ZI_[HSD]$")>;

// Floating point divide, F16
def : InstRW<[V1Write_13c10_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_H")>;

// Floating point divide, F32
def : InstRW<[V1Write_10c7_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_S")>;

// Floating point divide, F64
def : InstRW<[V1Write_15c7_1V0], (instregex "^FDIVR?_(ZPmZ|ZPZZ)_D")>;

// Floating point min/max
def : InstRW<[V1Write_2c_1V01], (instregex "^F(MAX|MIN)(NM)?_ZPm[IZ]_[HSD]",
                                           "^F(MAX|MIN)(NM)?_ZPZ[IZ]_[HSD]")>;

// Floating point multiply
def : InstRW<[V1Write_3c_1V01], (instregex "^(FSCALE|FMULX)_ZPmZ_[HSD]",
                                           "^FMULX_ZPZZ_[HSD]",
                                           "^FMUL_(ZPm[IZ]|ZZZI?)_[HSD]",
                                           "^FMUL_ZPZ[IZ]_[HSD]")>;

// Floating point multiply accumulate
// Floating point reciprocal step
def : InstRW<[V1Write_4c_1V01], (instregex "^F(N?M(AD|SB)|N?ML[AS])_ZPmZZ_[HSD]$",
                                           "^FN?ML[AS]_ZPZZZ_[HSD]",
                                           "^FML[AS]_ZZZI_[HSD]$",
                                           "^F(RECPS|RSQRTS)_ZZZ_[HSD]$")>;

// Floating point reciprocal estimate, F16
def : InstRW<[V1Write_6c_4V0], (instrs FRECPE_ZZ_H, FRSQRTE_ZZ_H)>;

// Floating point reciprocal estimate, F32
def : InstRW<[V1Write_4c_2V0], (instrs FRECPE_ZZ_S, FRSQRTE_ZZ_S)>;

// Floating point reciprocal estimate, F64
def : InstRW<[V1Write_3c_1V0], (instrs FRECPE_ZZ_D, FRSQRTE_ZZ_D)>;

// Floating point reciprocal exponent
def : InstRW<[V1Write_3c_1V0], (instregex "^FRECPX_ZPmZ_[HSD]")>;

// Floating point reduction, F16
def : InstRW<[V1Write_13c_6V01], (instregex "^F(ADD|((MAX|MIN)(NM)?))V_VPZ_H$")>;

// Floating point reduction, F32
def : InstRW<[V1Write_11c_1V_5V01], (instregex "^F(ADD|((MAX|MIN)(NM)?))V_VPZ_S$")>;

// Floating point reduction, F64
def : InstRW<[V1Write_9c_1V_4V01], (instregex "^F(ADD|((MAX|MIN)(NM)?))V_VPZ_D$")>;

// Floating point round to integral, F16
def : InstRW<[V1Write_6c_1V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_H")>;

// Floating point round to integral, F32
def : InstRW<[V1Write_4c_1V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_S")>;

// Floating point round to integral, F64
def : InstRW<[V1Write_3c_1V0], (instregex "^FRINT[AIMNPXZ]_ZPmZ_D")>;

// Floating point square root, F16
def : InstRW<[V1Write_13c10_1V0], (instregex "^FSQRT_ZPmZ_H")>;

// Floating point square root, F32
def : InstRW<[V1Write_10c7_1V0], (instregex "^FSQRT_ZPmZ_S")>;

// Floating point square root, F64
def : InstRW<[V1Write_16c7_1V0], (instregex "^FSQRT_ZPmZ_D")>;

// Floating point trigonometric
def : InstRW<[V1Write_3c_1V01], (instregex "^FEXPA_ZZ_[HSD]$",
                                           "^FTMAD_ZZI_[HSD]$",
                                           "^FTS(MUL|SEL)_ZZZ_[HSD]$")>;


// SVE BFloat16 (BF16) instructions
// -----------------------------------------------------------------------------

// Convert, F32 to BF16
def : InstRW<[V1Write_4c_1V0], (instrs BFCVT_ZPmZ, BFCVTNT_ZPmZ)>;

// Dot product
def : InstRW<[V1Write_4c_1V01], (instrs BFDOT_ZZI, BFDOT_ZZZ)>;

// Matrix multiply accumulate
def : InstRW<[V1Write_5c_1V01], (instrs BFMMLA_ZZZ)>;

// Multiply accumulate long
def : InstRW<[V1Write_5c_1V01], (instregex "^BFMLAL[BT]_ZZZ(I)?$")>;


// SVE Load instructions
// -----------------------------------------------------------------------------

// Load vector
def : InstRW<[V1Write_6c_1L01], (instrs LDR_ZXI)>;

// Load predicate
def : InstRW<[V1Write_6c_1L_1M], (instrs LDR_PXI)>;

// Contiguous load, scalar + imm
// Contiguous load, scalar + scalar
// Contiguous load broadcast, scalar + imm
// Contiguous load broadcast, scalar + scalar
def : InstRW<[V1Write_6c_1L01], (instregex "^LD1[BHWD]_IMM$",
                                           "^LD1S?B_[HSD]_IMM$",
                                           "^LD1S?H_[SD]_IMM$",
                                           "^LD1S?W_D_IMM$",
                                           "^LD1[BWD]$",
                                           "^LD1S?B_[HSD]$",
                                           "^LD1S?W_D$",
                                           "^LD1R[BHWD]_IMM$",
                                           "^LD1RSW_IMM$",
                                           "^LD1RS?B_[HSD]_IMM$",
                                           "^LD1RS?H_[SD]_IMM$",
                                           "^LD1RS?W_D_IMM$",
                                           "^LD1RQ_[BHWD]_IMM$",
                                           "^LD1RQ_[BWD]$")>;
def : InstRW<[V1Write_7c_1L01_1S], (instregex "^LD1H$",
                                              "^LD1S?H_[SD]$",
                                              "^LD1RQ_H$")>;

// Non temporal load, scalar + imm
def : InstRW<[V1Write_6c_1L01], (instregex "^LDNT1[BHWD]_ZRI$")>;

// Non temporal load, scalar + scalar
def : InstRW<[V1Write_7c_1L01_1S], (instrs LDNT1H_ZRR)>;
def : InstRW<[V1Write_6c_1L01_1S], (instregex "^LDNT1[BWD]_ZRR$")>;

// Contiguous first faulting load, scalar + scalar
def : InstRW<[V1Write_7c_1L01_1S], (instregex "^LDFF1H$",
                                              "^LDFF1S?H_[SD]$")>;
def : InstRW<[V1Write_6c_1L01_1S], (instregex "^LDFF1[BWD]$",
                                              "^LDFF1S?B_[HSD]$",
                                              "^LDFF1S?W_D$")>;

// Contiguous non faulting load, scalar + imm
def : InstRW<[V1Write_6c_1L01], (instregex "^LDNF1[BHWD]_IMM$",
                                           "^LDNF1S?B_[HSD]_IMM$",
                                           "^LDNF1S?H_[SD]_IMM$",
                                           "^LDNF1S?W_D_IMM$")>;

// Contiguous Load two structures to two vectors, scalar + imm
def : InstRW<[V1Write_8c_2L01_2V01], (instregex "^LD2[BHWD]_IMM$")>;

// Contiguous Load two structures to two vectors, scalar + scalar
def : InstRW<[V1Write_10c_2L01_2V01], (instrs LD2H)>;
def : InstRW<[V1Write_9c_2L01_2V01],  (instregex "^LD2[BWD]$")>;

// Contiguous Load three structures to three vectors, scalar + imm
def : InstRW<[V1Write_11c_3L01_3V01], (instregex "^LD3[BHWD]_IMM$")>;

// Contiguous Load three structures to three vectors, scalar + scalar
def : InstRW<[V1Write_13c_3L01_1S_3V01], (instregex "^LD3[BHWD]$")>;

// Contiguous Load four structures to four vectors, scalar + imm
def : InstRW<[V1Write_12c_4L01_4V01], (instregex "^LD4[BHWD]_IMM$")>;

// Contiguous Load four structures to four vectors, scalar + scalar
def : InstRW<[V1Write_13c_4L01_2S_4V01], (instregex "^LD4[BHWD]$")>;

// Gather load, vector + imm, 32-bit element size
def : InstRW<[V1Write_11c_1L_1V], (instregex "^GLD(FF)?1S?[BH]_S_IMM$",
                                             "^GLD(FF)?1W_IMM$")>;

// Gather load, vector + imm, 64-bit element size
def : InstRW<[V1Write_9c_2L_2V],
             (instregex "^GLD(FF)?1S?[BHW]_D_IMM$",
                        "^GLD(FF)?1S?[BHW]_D(_[SU]XTW)?(_SCALED)?$",
                        "^GLD(FF)?1D_IMM$",
                        "^GLD(FF)?1D(_[SU]XTW)?(_SCALED)?$")>;

// Gather load, 32-bit scaled offset
def : InstRW<[V1Write_11c_2L_2V],
             (instregex "^GLD(FF)?1S?[HW]_S_[SU]XTW_SCALED$",
                        "^GLD(FF)?1W_[SU]XTW_SCALED")>;

// Gather load, 32-bit unpacked unscaled offset
def : InstRW<[V1Write_9c_1L_1V],
             (instregex "^GLD(FF)?1S?[BH]_S_[SU]XTW$",
                        "^GLD(FF)?1W_[SU]XTW$")>;

// Prefetch
// NOTE: This is not specified in the SOG.
def : InstRW<[V1Write_4c_1L01], (instregex "^PRF[BHWD]")>;


// SVE Store instructions
// -----------------------------------------------------------------------------

// Store from predicate reg
def : InstRW<[V1Write_1c_1L01], (instrs STR_PXI)>;

// Store from vector reg
def : InstRW<[V1Write_2c_1L01_1V], (instrs STR_ZXI)>;

// Contiguous store, scalar + imm
// Contiguous store, scalar + scalar
def : InstRW<[V1Write_2c_1L01_1V], (instregex "^ST1[BHWD]_IMM$",
                                              "^ST1B_[HSD]_IMM$",
                                              "^ST1H_[SD]_IMM$",
                                              "^ST1W_D_IMM$",
                                              "^ST1[BWD]$",
                                              "^ST1B_[HSD]$",
                                              "^ST1W_D$")>;
def : InstRW<[V1Write_2c_1L01_1S_1V], (instregex "^ST1H(_[SD])?$")>;

// Contiguous store two structures from two vectors, scalar + imm
// Contiguous store two structures from two vectors, scalar + scalar
def : InstRW<[V1Write_4c_1L01_1V], (instregex "^ST2[BHWD]_IMM$",
                                              "^ST2[BWD]$")>;
def : InstRW<[V1Write_4c_1L01_1S_1V], (instrs ST2H)>;

// Contiguous store three structures from three vectors, scalar + imm
def : InstRW<[V1Write_7c_5L01_5V], (instregex "^ST3[BHWD]_IMM$")>;

// Contiguous store three structures from three vectors, scalar + scalar
def : InstRW<[V1Write_7c_5L01_5S_5V], (instregex "^ST3[BHWD]$")>;

// Contiguous store four structures from four vectors, scalar + imm
def : InstRW<[V1Write_11c_9L01_9V], (instregex "^ST4[BHWD]_IMM$")>;

// Contiguous store four structures from four vectors, scalar + scalar
def : InstRW<[V1Write_11c_9L01_9S_9V], (instregex "^ST4[BHWD]$")>;

// Non temporal store, scalar + imm
// Non temporal store, scalar + scalar
def : InstRW<[V1Write_2c_1L01_1V], (instregex "^STNT1[BHWD]_ZRI$",
                                              "^STNT1[BWD]_ZRR$")>;
def : InstRW<[V1Write_2c_1L01_1S_1V], (instrs STNT1H_ZRR)>;

// Scatter store vector + imm 32-bit element size
// Scatter store, 32-bit scaled offset
// Scatter store, 32-bit unscaled offset
def : InstRW<[V1Write_10c_2L01_2V], (instregex "^SST1[BH]_S_IMM$",
                                               "^SST1W_IMM$",
                                               "^SST1(H_S|W)_[SU]XTW_SCALED$",
                                               "^SST1[BH]_S_[SU]XTW$",
                                               "^SST1W_[SU]XTW$")>;

// Scatter store, 32-bit unpacked unscaled offset
// Scatter store, 32-bit unpacked scaled offset
def : InstRW<[V1Write_6c_1L01_1V], (instregex "^SST1[BHW]_D_[SU]XTW$",
                                              "^SST1D_[SU]XTW$",
                                              "^SST1[HW]_D_[SU]XTW_SCALED$",
                                              "^SST1D_[SU]XTW_SCALED$")>;

// Scatter store vector + imm 64-bit element size
// Scatter store, 64-bit scaled offset
// Scatter store, 64-bit unscaled offset
def : InstRW<[V1Write_6c_1L01_1V], (instregex "^SST1[BHW]_D_IMM$",
                                              "^SST1D_IMM$",
                                              "^SST1[HW]_D_SCALED$",
                                              "^SST1D_SCALED$",
                                              "^SST1[BHW]_D$",
                                              "^SST1D$")>;


// SVE Miscellaneous instructions
// -----------------------------------------------------------------------------

// Read first fault register, unpredicated
// Set first fault register
// Write to first fault register
def : InstRW<[V1Write_2c_1M0], (instrs RDFFR_P,
                                       SETFFR,
                                       WRFFR)>;

// Read first fault register, predicated
def : InstRW<[V1Write_3c_2M0], (instrs RDFFR_PPz)>;

// Read first fault register and set flags
def : InstRW<[V1Write_4c_1M], (instrs RDFFRS_PPz)>;


}
