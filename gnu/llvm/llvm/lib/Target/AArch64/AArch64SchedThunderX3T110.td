//=- AArch64SchedThunderX3T110.td - Marvell ThunderX3 T110 ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for Marvell ThunderX3T110
// family of processors.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pipeline Description.

def ThunderX3T110Model : SchedMachineModel {
  let IssueWidth            =   4; // 4 micro-ops dispatched at a time.
  let MicroOpBufferSize     =  70; // 70 entries in micro-op re-order buffer.
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  12; // Extra cycles for mispredicted branch.
  // Determined via a mix of micro-arch details and experimentation.
  let LoopMicroOpBufferSize = 128; // FIXME: might be much bigger in TX3.
  let PostRAScheduler       =   1; // Using PostRA sched.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

let SchedModel = ThunderX3T110Model in {

// Issue ports.

// Port 0: ALU.
def THX3T110P0 : ProcResource<1>;

// Port 1: ALU.
def THX3T110P1 : ProcResource<1>;

// Port 2: ALU/Branch.
def THX3T110P2 : ProcResource<1>;

// Port 3: ALU/Branch.
def THX3T110P3 : ProcResource<1>;

// Port 4: Load/Store.
def THX3T110P4 : ProcResource<1>;

// Port 5: Load/store.
def THX3T110P5 : ProcResource<1>;

// Port 6: FP/Neon/SIMD/Crypto.
def THX3T110P6FP0 : ProcResource<1>;

// Port 7: FP/Neon/SIMD/Crypto.
def THX3T110P7FP1 : ProcResource<1>;

// Port 8: FP/Neon/SIMD/Crypto.
def THX3T110P8FP2 : ProcResource<1>;

// Port 9: FP/Neon/SIMD/Crypto.
def THX3T110P9FP3 : ProcResource<1>;

// Port 10: Store Data Unit.
def THX3T110SD0 : ProcResource<1>;

// Define groups for the functional units on each issue port.  Each group
// created will be used by a WriteRes.

// Integer divide/mulhi micro-ops only on port I1.
def THX3T110I1 : ProcResGroup<[THX3T110P1]>;

// Branch micro-ops on ports I2/I3.
def THX3T110I23 : ProcResGroup<[THX3T110P2, THX3T110P3]>;

// Branch micro-ops on ports I1/I2/I3.
def THX3T110I123 : ProcResGroup<[THX3T110P1, THX3T110P2, THX3T110P3]>;

// Integer micro-ops on ports I0/I1/I2.
def THX3T110I012 : ProcResGroup<[THX3T110P0, THX3T110P1, THX3T110P2]>;

// Integer micro-ops on ports I0/I1/I2/I3.
def THX3T110I0123 : ProcResGroup<[THX3T110P0, THX3T110P1,
                                  THX3T110P2, THX3T110P3]>;

// FP micro-ops on ports FP0/FP1/FP2/FP3.
def THX3T110FP0123 : ProcResGroup<[THX3T110P6FP0, THX3T110P7FP1,
                                   THX3T110P8FP2, THX3T110P9FP3]>;

// FP micro-ops on ports FP2/FP3.
def THX3T110FP23 : ProcResGroup<[THX3T110P8FP2, THX3T110P9FP3]>;

// ASIMD micro-ops on ports FP0/FP1/FP2/FP3.
def THX3T110SIMD : ProcResGroup<[THX3T110P6FP0, THX3T110P7FP1,
                                 THX3T110P8FP2, THX3T110P9FP3]>;

// Store data micro-ops only on port 10.
def THX3T110SD : ProcResGroup<[THX3T110SD0]>;

// Load/store micro-ops on ports P4/P5.
def THX3T110LS : ProcResGroup<[THX3T110P4, THX3T110P5]>;

// 70 entry unified scheduler.
def THX3T110ANY: ProcResGroup<[THX3T110P0, THX3T110P1, THX3T110P2,
                               THX3T110P3, THX3T110P4, THX3T110P5,
                               THX3T110P6FP0, THX3T110P7FP1,
                               THX3T110P8FP2, THX3T110P9FP3]> {
  let BufferSize = 70;
}

// Define commonly used write types for InstRW specializations.
// All definitions follow the format: THX3T110Write_<NumCycles>Cyc_<Resources>.

// 3 cycles on I1.
def THX3T110Write_3Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

// 4 cycles on I1.
def THX3T110Write_4Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// 5 cycles on I1.
def THX3T110Write_5Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// 7 cycles on I1.
def THX3T110Write_7Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 23 cycles on I1.
def THX3T110Write_23Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 23;
  let ReleaseAtCycles = [13, 23];
  let NumMicroOps = 4;
}

// 39 cycles on I1.
def THX3T110Write_39Cyc_I1 : SchedWriteRes<[THX3T110I1]> {
  let Latency = 39;
  let ReleaseAtCycles = [13, 39];
  let NumMicroOps = 4;
}

// 1 cycle on I2/I3
def THX3T110Write_1Cyc_I23 : SchedWriteRes<[THX3T110I23]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 8 cycles on I2/I3
def THX3T110Write_8Cyc_I23 : SchedWriteRes<[THX3T110I23]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 1 cycle on I1/I2/I3
def THX3T110Write_1Cyc_I123 : SchedWriteRes<[THX3T110I123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 8 cycles on I1/I2/I3
def THX3T110Write_8Cyc_I123 : SchedWriteRes<[THX3T110I123]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 1 cycle on I0/I1/I2/I3.
def THX3T110Write_1Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 2 cycles on I0/I1/I2/I3.
def THX3T110Write_2Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// 3 cycles on I0/I1/I2/I3.
def THX3T110Write_3Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

// 4 cycles on I0/I1/I2/I3.
def THX3T110Write_4Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 5 cycles on I0/I1/I2/I3.
def THX3T110Write_5Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on I0/I1/I2/I3.
def THX3T110Write_6Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 8 cycles on I0/I1/I2/I3.
def THX3T110Write_8Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

// 13 cycles on I0/I1/I2/I3.
def THX3T110Write_13Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 13;
  let NumMicroOps = 3;
}

// 23 cycles on I0/I1/I2/I3.
def THX3T110Write_23Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 23;
  let NumMicroOps = 3;
}

// 39 cycles on I0/I1/I2/I3.
def THX3T110Write_39Cyc_I0123 : SchedWriteRes<[THX3T110I0123]> {
  let Latency = 39;
  let NumMicroOps = 3;
}

// 4 cycles on F2/F3.
def THX3T110Write_4Cyc_F23 : SchedWriteRes<[THX3T110FP23]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// 5 cycles on F0/F1/F2/F3.
def THX3T110Write_5Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// 6 cycles on F0/F1/F2/F3.
def THX3T110Write_6Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 7 cycles on F0/F1/F2/F3.
def THX3T110Write_7Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 8 cycles on F0/F1/F2/F3.
def THX3T110Write_8Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 10 cycles on F0/F1/F2/F3.
def THX3T110Write_10Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

// 16 cycles on F0/F1/F2/F3.
def THX3T110Write_16Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 16;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [8];
}

// 23 cycles on F0/F1/F2/F3.
def THX3T110Write_23Cyc_F01 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 23;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [11];
}

// 1 cycle on LS0/LS1.
def THX3T110Write_1Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

// 2 cycles on LS0/LS1.
def THX3T110Write_2Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// 4 cycles on LS0/LS1.
def THX3T110Write_4Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2];
}

// 5 cycles on LS0/LS1.
def THX3T110Write_5Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0/LS1.
def THX3T110Write_6Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 4 + 5 cycles on LS0/LS1.
// First resource is available after 4 cycles.
// Second resource is available after 5 cycles.
// Load vector pair, immed offset, Q-form [LDP/LDNP].
def THX3T110Write_4_5Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [4, 5];
}

// 4 + 8 cycles on LS0/LS1.
// First resource is available after 4 cycles.
// Second resource is available after 8 cycles.
// Load vector pair, immed offset, S/D-form [LDP/LDNP].
def THX3T110Write_4_8Cyc_LS01 : SchedWriteRes<[THX3T110LS]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [4, 8];
}

// 11 cycles on LS0/LS1 and I1.
def THX3T110Write_11Cyc_LS01_I1 :
  SchedWriteRes<[THX3T110LS, THX3T110I1]> {
  let Latency = 11;
  let NumMicroOps = 4;
}

// 1 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_1Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 1 cycles on LS0/LS1 and 2 of I0/I1/I2/I3.
def THX3T110Write_1Cyc_LS01_I0123_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123, THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 3;
}

// 4 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_4Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 4 cycles on LS0/LS1 and 2 of I0/I1/I2/I3.
def THX3T110Write_4Cyc_LS01_I0123_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123, THX3T110I0123]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 5 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_5Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 5 cycles on LS0/LS1 and 2 of I0/I1/I2/I3.
def THX3T110Write_5Cyc_LS01_I0123_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123, THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_6Cyc_LS01_I012 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 6;
  let NumMicroOps = 4;
}

// 6 cycles on LS0/LS1 and 2 of I0/I1/I2/I3.
def THX3T110Write_6Cyc_LS01_I0123_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123, THX3T110I0123]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 1 cycle on LS0/LS1 and SD.
def THX3T110Write_1Cyc_LS01_SD :
  SchedWriteRes<[THX3T110LS, THX3T110SD]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 2 cycles on LS0/LS1 and SD.
def THX3T110Write_2Cyc_LS01_SD :
  SchedWriteRes<[THX3T110LS, THX3T110SD]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// 4 cycles on LS0/LS1 and SD.
def THX3T110Write_4Cyc_LS01_SD :
  SchedWriteRes<[THX3T110LS, THX3T110SD]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 5 cycles on LS0/LS1 and SD.
def THX3T110Write_5Cyc_LS01_SD :
  SchedWriteRes<[THX3T110LS, THX3T110SD]> {
  let Latency = 5;
  let NumMicroOps = 4;
}

// 6 cycles on LS0/LS1 and SD.
def THX3T110Write_6Cyc_LS01_SD :
  SchedWriteRes<[THX3T110LS, THX3T110SD]> {
  let Latency = 6;
  let NumMicroOps = 5;
}

// 1 cycle on LS0/LS1, SD and I0/I1/I2/I3.
def THX3T110Write_1Cyc_LS01_SD_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 2 cycles on LS0/LS1, SD and I0/I1/I2/I3.
def THX3T110Write_2Cyc_LS01_SD_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// 4 cycles on LS0/LS1, SD and I0/I1/I2/I3.
def THX3T110Write_4Cyc_LS01_SD_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 5 cycles on LS0/LS1, SD and I0/I1/I2/I3.
def THX3T110Write_5Cyc_LS01_SD_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 4;
}

// 6 cycles on LS0/LS1, SD and I0/I1/I2/I3.
def THX3T110Write_6Cyc_LS01_SD_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 6;
  let NumMicroOps = 5;
}

// 1 cycles on LS0/LS1 and F0/F1/F2/F3.
def THX3T110Write_1Cyc_LS01_F0123 :
  SchedWriteRes<[THX3T110LS, THX3T110FP0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 5 cycles on LS0/LS1 and F0/F1/F2/F3.
def THX3T110Write_5Cyc_LS01_F0123 :
  SchedWriteRes<[THX3T110LS, THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0/LS1 and F0/F1/F2/F3.
def THX3T110Write_6Cyc_LS01_F0123 :
  SchedWriteRes<[THX3T110LS, THX3T110FP0123]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 7 cycles on LS0/LS1 and F0/F1/F2/F3.
def THX3T110Write_7Cyc_LS01_F0123 :
  SchedWriteRes<[THX3T110LS, THX3T110FP0123]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 8 cycles on LS0/LS1 and F0/F1/F2/F3.
def THX3T110Write_8Cyc_LS01_F0123 :
  SchedWriteRes<[THX3T110LS, THX3T110FP0123]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 8 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_8Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 12 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_12Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 12;
  let NumMicroOps = 4;
}

// 16 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_16Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 16;
  let NumMicroOps = 5;
}

// 24 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_24Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 24;
  let NumMicroOps = 10;
}

// 32 cycles on LS0/LS1 and I0/I1/I2/I3.
def THX3T110Write_32Cyc_LS01_I0123 :
  SchedWriteRes<[THX3T110LS, THX3T110I0123]> {
  let Latency = 32;
  let NumMicroOps = 14;
}

// 3 cycles on F0/F1/F2/F3.
def THX3T110Write_3Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

// 4 cycles on F0/F1/F2/F3.
def THX3T110Write_4Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// 5 cycles on F0/F1/F2/F3.
def THX3T110Write_5Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// 10 cycles on F0/F1/F2/F3.
def THX3T110Write_10Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 10;
  let NumMicroOps = 4;
}

// 15 cycles on F0/F1/F2/F3.
def THX3T110Write_15Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 15;
  let NumMicroOps = 7;
}

// 16 cycles on F0/F1/F2/F3.
def THX3T110Write_16Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 16;
  let NumMicroOps = 3;
}

// 18 cycles on F0/F1/F2/F3.
def THX3T110Write_18Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 18;
  let NumMicroOps = 3;
}

// 19 cycles on F0/F1/F2/F3.
def THX3T110Write_19Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 19;
  let NumMicroOps = 4;
}

// 20 cycles on F0/F1/F2/F3.
def THX3T110Write_20Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 20;
  let NumMicroOps = 4;
}

// 23 cycles on F0/F1/F2/F3.
def THX3T110Write_23Cyc_F0123 : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 23;
  let NumMicroOps = 4;
}

// 3 cycles on F2/F3 and 4 cycles on F0/F1/F2/F3.
def THX3T110Write_3_4Cyc_F23_F0123 :
  SchedWriteRes<[THX3T110FP23, THX3T110FP0123]> {
  let Latency = 3;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [3, 4];
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
def : ReadAdvance<ReadVLD,     0>;
def : ReadAdvance<ReadST,      0>;

//===----------------------------------------------------------------------===//
// 3. Instruction Tables.

//---
// 3.1 Branch Instructions
//---

// Branch, immed
// Branch and link, immed
// Compare and branch
def : WriteRes<WriteBr,      [THX3T110I23]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Branch, register
// Branch and link, register != LR
// Branch and link, register = LR
def : WriteRes<WriteBrReg,   [THX3T110I23]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteAtomic,  []> {
  let Latency = 4;
  let NumMicroOps = 2;
}

//---
// Branch
//---
def : InstRW<[THX3T110Write_1Cyc_I23], (instrs B, BL, BR, BLR)>;
def : InstRW<[THX3T110Write_1Cyc_I23], (instrs Bcc)>;
def : InstRW<[THX3T110Write_1Cyc_I23], (instrs RET)>;
def : InstRW<[THX3T110Write_1Cyc_I23],
            (instrs CBZW, CBZX, CBNZW, CBNZX, TBZW, TBZX, TBNZW, TBNZX)>;

//---
// 3.2 Arithmetic and Logical Instructions
// 3.3 Move and Shift Instructions
//---


// ALU, basic
// Conditional compare
// Conditional select
// Address generation
def : WriteRes<WriteI,       [THX3T110I0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
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
def : WriteRes<WriteISReg,   [THX3T110I0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 2;
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

def : WriteRes<WriteIEReg,   [THX3T110I0123]> {
  let Latency = 1;
  let ReleaseAtCycles = [1];
  let NumMicroOps = 2;
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
def : WriteRes<WriteImm,     [THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def : InstRW<[THX3T110Write_1Cyc_I0123],
            (instrs MOVKWi, MOVKXi, MOVNWi, MOVNXi, MOVZWi, MOVZXi)>;

def : InstRW<[THX3T110Write_1Cyc_I0123],
            (instrs ASRVWr, ASRVXr, LSLVWr, LSLVXr, RORVWr, RORVXr)>;

// Variable shift
def : WriteRes<WriteIS,      [THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

//---
// 3.4 Divide and Multiply Instructions
//---

// Divide, W-form
// Latency range of 13-23/13-39.
def : WriteRes<WriteID32,    [THX3T110I1]> {
  let Latency = 39;
  let ReleaseAtCycles = [39];
  let NumMicroOps = 4;
}

// Divide, X-form
def : WriteRes<WriteID64,    [THX3T110I1]> {
  let Latency = 23;
  let ReleaseAtCycles = [23];
  let NumMicroOps = 4;
}

// Multiply accumulate, W-form
def : WriteRes<WriteIM32,    [THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// Multiply accumulate, X-form
def : WriteRes<WriteIM64,    [THX3T110I0123]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

//def : InstRW<[WriteIM32, ReadIM, ReadIM, ReadIMA, THX3T110Write_5Cyc_I012],
//             (instrs MADDWrrr, MSUBWrrr)>;
def : InstRW<[WriteIM32], (instrs MADDWrrr, MSUBWrrr)>;
def : InstRW<[WriteIM32], (instrs MADDXrrr, MSUBXrrr)>;
def : InstRW<[THX3T110Write_5Cyc_I0123],
            (instregex "(S|U)(MADDL|MSUBL)rrr")>;

def : InstRW<[WriteID32], (instrs SDIVWr, UDIVWr)>;
def : InstRW<[WriteID64], (instrs SDIVXr, UDIVXr)>;

// Bitfield extract, two reg
def : WriteRes<WriteExtr,    [THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Multiply high
def : InstRW<[THX3T110Write_4Cyc_I1], (instrs SMULHrr, UMULHrr)>;

// Miscellaneous Data-Processing Instructions
// Bitfield extract
def : InstRW<[THX3T110Write_1Cyc_I0123], (instrs EXTRWrri, EXTRXrri)>;

// Bitifield move - basic
def : InstRW<[THX3T110Write_1Cyc_I0123],
            (instrs SBFMWri, SBFMXri, UBFMWri, UBFMXri)>;

// Bitfield move, insert
def : InstRW<[THX3T110Write_1Cyc_I0123], (instregex "^BFM")>;
def : InstRW<[THX3T110Write_1Cyc_I0123], (instregex "(S|U)?BFM.*")>;

// Count leading
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123],
            (instregex "^CLS(W|X)r$", "^CLZ(W|X)r$")>;

// Reverse bits
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123], (instrs RBITWr, RBITXr)>;

// Cryptography Extensions
def : InstRW<[THX3T110Write_4Cyc_F0123], (instregex "^AES[DE]")>;
def : InstRW<[THX3T110Write_4Cyc_F0123], (instregex "^AESI?MC")>;
def : InstRW<[THX3T110Write_4Cyc_F0123], (instregex "^PMULL")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SHA1SU0")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SHA1(H|SU1)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SHA1[CMP]")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SHA256SU0")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SHA256(H|H2|SU1)")>;

// CRC Instructions
// def : InstRW<[THX3T110Write_4Cyc_I1], (instregex "^CRC32", "^CRC32C")>;
def : InstRW<[THX3T110Write_4Cyc_I1],
            (instrs CRC32Brr, CRC32Hrr, CRC32Wrr, CRC32Xrr)>;

def : InstRW<[THX3T110Write_4Cyc_I1],
            (instrs CRC32CBrr, CRC32CHrr, CRC32CWrr, CRC32CXrr)>;

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
def : WriteRes<WriteLD,      [THX3T110LS]> {
  let Latency = 4;
  let NumMicroOps = 4;
}

// Load register, immed post-index
// NOTE: Handled by WriteLD, WriteI.
// Load register, immed pre-index
// NOTE: Handled by WriteLD, WriteAdr.
def : WriteRes<WriteAdr,     [THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Load pair, immed offset, normal
// Load pair, immed offset, signed words, base != SP
// Load pair, immed offset signed words, base = SP
// LDP only breaks into *one* LS micro-op.  Thus
// the resources are handled by WriteLD.
def : WriteRes<WriteLDHi,    []> {
  let Latency = 4;
  let NumMicroOps = 4;
}

// Load register offset, basic
// Load register, register offset, scale by 4/8
// Load register, register offset, scale by 2
// Load register offset, extend
// Load register, register offset, extend, scale by 4/8
// Load register, register offset, extend, scale by 2
def THX3T110WriteLDIdx : SchedWriteVariant<[
  SchedVar<ScaledIdxPred, [THX3T110Write_4Cyc_LS01_I0123_I0123]>,
  SchedVar<NoSchedPred,   [THX3T110Write_4Cyc_LS01_I0123]>]>;
def : SchedAlias<WriteLDIdx, THX3T110WriteLDIdx>;

def THX3T110ReadAdrBase : SchedReadVariant<[
  SchedVar<ScaledIdxPred, [ReadDefault]>,
  SchedVar<NoSchedPred,   [ReadDefault]>]>;
def : SchedAlias<ReadAdrBase, THX3T110ReadAdrBase>;

// Load pair, immed pre-index, normal
// Load pair, immed pre-index, signed words
// Load pair, immed post-index, normal
// Load pair, immed post-index, signed words
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDNPDi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDNPQi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDNPSi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDNPWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDNPXi)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPDi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPQi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPSi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPSWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, WriteLDHi], (instrs LDPXi)>;

def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRBui)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRDui)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRHui)>;
def : InstRW<[THX3T110Write_5Cyc_LS01], (instrs LDRQui)>;
def : InstRW<[THX3T110Write_5Cyc_LS01], (instrs LDRSui)>;

def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRDl)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRQl)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRWl)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDRXl)>;

def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRBi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRHi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRXi)>;

def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRSBWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRSBXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRSHWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRSHXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDTRSWi)>;

def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPDpre)>;
def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPQpre)>;
def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPSpre)>;
def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;
def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;

def : InstRW<[THX3T110Write_4Cyc_LS01, WriteAdr],
            (instrs LDRBpre, LDRDpre, LDRHpre, LDRQpre,
                    LDRSpre, LDRWpre, LDRXpre,
                    LDRSBWpre, LDRSBXpre, LDRSBWpost, LDRSBXpost,
                    LDRSHWpre, LDRSHXpre, LDRSHWpost, LDRSHXpost,
                    LDRBBpre, LDRBBpost, LDRHHpre, LDRHHpost)>;

def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteLDHi, WriteAdr],
            (instrs LDPDpost, LDPQpost, LDPSpost, LDPWpost, LDPXpost)>;

def : InstRW<[THX3T110Write_5Cyc_LS01_I0123, WriteI],
            (instrs LDRBpost, LDRDpost, LDRHpost,
                    LDRQpost, LDRSpost, LDRWpost, LDRXpost)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123_I0123, WriteLDHi, WriteAdr],
            (instrs LDPDpre, LDPQpre, LDPSpre, LDPWpre, LDPXpre)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123_I0123, WriteAdr],
            (instrs LDRBpre, LDRDpre, LDRHpre, LDRQpre,
                    LDRSpre, LDRWpre, LDRXpre)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123_I0123, WriteLDHi, WriteAdr],
            (instrs LDPDpost, LDPQpost, LDPSpost, LDPWpost, LDPXpost)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123_I0123, WriteI],
            (instrs LDRBpost, LDRDpost, LDRHpost, LDRQpost,
                    LDRSpost, LDRWpost, LDRXpost)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRBroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRDroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRHroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRHHroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRQroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSHWroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSHXroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRWroW)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRXroW)>;

def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRBroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRDroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRHHroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRHroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSHWroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRSHXroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRWroX)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_I0123, ReadAdrBase], (instrs LDRXroX)>;

def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURBi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURBBi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURDi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURHi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURHHi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURQi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSBWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSBXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSHWi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSHXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instrs LDURSWi)>;

// Load exclusive
def : InstRW<[THX3T110Write_4Cyc_LS01], (instregex "^LDAR(B|H|W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instregex "^LDAXR(B|H|W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instregex "^LDXR(B|H|W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instregex "^LDAXP(W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01], (instregex "^LDXP(W|X)$")>;

//---
// Prefetch
//---
def : InstRW<[THX3T110Write_6Cyc_LS01_I012], (instrs PRFMl)>;
def : InstRW<[THX3T110Write_6Cyc_LS01_I012], (instrs PRFUMi)>;
def : InstRW<[THX3T110Write_6Cyc_LS01_I012], (instrs PRFMui)>;
def : InstRW<[THX3T110Write_6Cyc_LS01_I012], (instrs PRFMroW)>;
def : InstRW<[THX3T110Write_6Cyc_LS01_I012], (instrs PRFMroX)>;

//--
// 3.7 Store Instructions
// 3.11 FP Store Instructions
//--

// Store register, unscaled immed
// Store register, immed unprivileged
// Store register, unsigned immed
def : WriteRes<WriteST,      [THX3T110LS, THX3T110SD]> {
  let Latency = 1;
  let NumMicroOps = 2;
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
def : WriteRes<WriteSTIdx, [THX3T110LS, THX3T110SD, THX3T110I0123]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Store pair, immed offset, W-form
// Store pair, immed offset, X-form
def : WriteRes<WriteSTP,     [THX3T110LS, THX3T110SD]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Store pair, immed post-index, W-form
// Store pair, immed post-index, X-form
// Store pair, immed pre-index, W-form
// Store pair, immed pre-index, X-form
// NOTE: Handled by WriteAdr, WriteSTP.
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURBi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURBBi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURDi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURHi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURHHi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURQi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURSi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURWi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STURXi)>;

def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_SD], (instrs STTRBi)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_SD], (instrs STTRHi)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_SD], (instrs STTRWi)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_SD], (instrs STTRXi)>;

def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STNPDi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STNPQi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STNPXi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STNPWi)>;

def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STPDi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STPQi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STPXi)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_SD], (instrs STPWi)>;

def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRBui)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRDui)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRHui)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRQui)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRXui)>;
def : InstRW<[THX3T110Write_1Cyc_LS01_I0123], (instrs STRWui)>;

def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRBui)>;
def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRDui)>;
def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRHui)>;
def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRQui)>;
def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRXui)>;
def : InstRW<[WriteSTP, THX3T110Write_1Cyc_LS01_SD], (instrs STRWui)>;

def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRBui)>;
def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRDui)>;
def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRHui)>;
def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRQui)>;
def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRXui)>;
def : InstRW<[WriteSTIdx, THX3T110Write_1Cyc_LS01_SD_I0123], (instrs STRWui)>;

def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRBBroW, STRBBroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[WriteAdr, THX3T110Write_1Cyc_LS01_I0123, ReadAdrBase],
            (instrs STRXroW, STRXroX)>;

// Store exclusive
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instrs STNPWi, STNPXi)>;
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instregex "^STLR(B|H|W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instregex "^STXP(W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instregex "^STXR(B|H|W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instregex "^STLXP(W|X)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01_SD], (instregex "^STLXR(B|H|W|X)$")>;

//---
// 3.8 FP Data Processing Instructions
//---

// FP absolute value
// FP min/max
// FP negate
def : WriteRes<WriteF,       [THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// FP arithmetic
def : InstRW<[THX3T110Write_6Cyc_F01], (instregex "^FADD", "^FSUB")>;

// FP compare
def : WriteRes<WriteFCmp,    [THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// FP Mul, Div, Sqrt
def : WriteRes<WriteFDiv, [THX3T110FP0123]> {
  let Latency = 22;
  let ReleaseAtCycles = [19];
}

def THX3T110XWriteFDiv : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 16;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 4;
}

def THX3T110XWriteFDivSP : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 16;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 4;
}

def THX3T110XWriteFDivDP : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 23;
  let ReleaseAtCycles = [12];
  let NumMicroOps = 4;
}

def THX3T110XWriteFSqrtSP : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 16;
  let ReleaseAtCycles = [8];
  let NumMicroOps = 4;
}

def THX3T110XWriteFSqrtDP : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 23;
  let ReleaseAtCycles = [12];
  let NumMicroOps = 4;
}

// FP divide, S-form
// FP square root, S-form
def : InstRW<[THX3T110XWriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[THX3T110XWriteFSqrtSP], (instrs FSQRTSr)>;
def : InstRW<[THX3T110XWriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[THX3T110XWriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[THX3T110Write_16Cyc_F01], (instregex "^FDIVSrr", "^FSQRTSr")>;

// FP divide, D-form
// FP square root, D-form
def : InstRW<[THX3T110XWriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[THX3T110XWriteFSqrtDP], (instrs FSQRTDr)>;
def : InstRW<[THX3T110XWriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[THX3T110XWriteFSqrtDP], (instregex "^.*SQRT.*64$")>;
def : InstRW<[THX3T110Write_23Cyc_F01], (instregex "^FDIVDrr", "^FSQRTDr")>;

// FP multiply
// FP multiply accumulate
def : WriteRes<WriteFMul, [THX3T110FP0123]> {
  let Latency = 6;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 3;
}

def THX3T110XWriteFMul : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 6;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 3;
}

def THX3T110XWriteFMulAcc : SchedWriteRes<[THX3T110FP0123]> {
  let Latency = 6;
  let ReleaseAtCycles = [2];
  let NumMicroOps = 3;
}

def : InstRW<[THX3T110XWriteFMul], (instregex "^FMUL", "^FNMUL")>;
def : InstRW<[THX3T110XWriteFMulAcc],
            (instregex "^FMADD", "^FMSUB", "^FNMADD", "^FNMSUB")>;

// FP round to integral
def : InstRW<[THX3T110Write_7Cyc_F01],
            (instregex "^FRINT(A|I|M|N|P|X|Z)(Sr|Dr)")>;

// FP select
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123], (instregex "^FCSEL")>;

//---
// 3.9 FP Miscellaneous Instructions
//---

// FP convert, from vec to vec reg
// FP convert, from gen to vec reg
// FP convert, from vec to gen reg
def : WriteRes<WriteFCvt, [THX3T110FP0123]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// FP move, immed
// FP move, register
def : WriteRes<WriteFImm, [THX3T110FP0123]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// FP transfer, from gen to vec reg
// FP transfer, from vec to gen reg
def : WriteRes<WriteFCopy, [THX3T110FP0123]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def : InstRW<[THX3T110Write_5Cyc_F01], (instrs FMOVXDHighr, FMOVDXHighr)>;

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
def : WriteRes<WriteVd, [THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}
def : WriteRes<WriteVq, [THX3T110FP0123]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [4];
}

// ASIMD arith, reduce, 4H/4S
// ASIMD arith, reduce, 8B/8H
// ASIMD arith, reduce, 16B

// ASIMD logical (MVN (alias for NOT), ORN, ORR)
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^ANDv", "^BICv", "^EORv", "^ORRv", "^ORNv", "^NOTv")>;

// ASIMD arith, reduce
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^ADDVv", "^SADDLVv", "^UADDLVv")>;

// ASIMD polynomial (8x8) multiply long
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^(S|U|SQD)MULL")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "(S|U|SQD)(MLAL|MLSL|MULL)v.*")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^PMULL(v8i8|v16i8)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^PMULL(v1i64|v2i64)")>;

// ASIMD absolute diff accum, D-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU]ABA(v8i8|v4i16|v2i32)$")>;
// ASIMD absolute diff accum, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU]ABA(v16i8|v8i16|v4i32)$")>;
// ASIMD absolute diff accum long
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU]ABAL")>;
// ASIMD arith, reduce, 4H/4S
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v$")>;
// ASIMD arith, reduce, 8B
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU]?ADDL?V(v8i16|v4i32)v$")>;
// ASIMD arith, reduce, 16B/16H
def : InstRW<[THX3T110Write_10Cyc_F0123],
            (instregex "^[SU]?ADDL?Vv16i8v$")>;
// ASIMD max/min, reduce, 4H/4S
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v$")>;
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v$")>;
// ASIMD max/min, reduce, 16B/16H
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU](MIN|MAX)Vv16i8v$")>;
// ASIMD multiply, D-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^(P?MUL|SQR?DMULH)" #
                       "(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)" #
                       "(_indexed)?$")>;
// ASIMD multiply, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^(P?MUL|SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?$")>;
// ASIMD multiply accumulate, D-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^ML[AS](v8i8|v4i16|v2i32)(_indexed)?$")>;
// ASIMD multiply accumulate, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^ML[AS](v16i8|v8i16|v4i32)(_indexed)?$")>;
// ASIMD shift accumulate
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "SRSRAv","SSRAv","URSRAv","USRAv")>;

// ASIMD shift by immed, basic
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "RSHRNv","SHRNv", "SQRSHRNv","SQRSHRUNv",
                       "SQSHRNv","SQSHRUNv", "UQRSHRNv",
                       "UQSHRNv","SQXTNv","SQXTUNv","UQXTNv")>;
// ASIMD shift by immed, complex
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^[SU]?(Q|R){1,2}SHR")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SQSHLU")>;
// ASIMD shift by register, basic, Q-form
def : InstRW<[THX3T110Write_5Cyc_F01],
            (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// ASIMD shift by register, complex, D-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU][QR]{1,2}SHL" #
                       "(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32|b|d|h|s)")>;
// ASIMD shift by register, complex, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^[SU][QR]{1,2}SHL(v16i8|v8i16|v4i32|v2i64)")>;

// ASIMD Arithmetic
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "(ADD|SUB)(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "(ADD|SUB)(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "(ADD|SUB)HNv.*")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "(RADD|RSUB)HNv.*")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^SQADD", "^SQNEG", "^SQSUB", "^SRHADD",
                       "^SUQADD", "^UQADD", "^UQSUB", "^URHADD", "^USQADD")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "ADDP(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "((AND|ORN|EOR|EON)S?(Xr[rsi]|v16i8|v8i16|v4i32)|" #
                       "(ORR|BIC)S?(Xr[rs]|v16i8|v8i16|v4i32))")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "(CLS|CLZ|CNT)(v4i32|v8i16|v16i8)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SADALP","^UADALP")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SADDLPv","^UADDLPv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SADDLV","^UADDLV")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
             (instregex "^ADDVv","^SMAXVv","^UMAXVv","^SMINVv","^UMINVv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
             (instregex "^SABAv","^UABAv","^SABALv","^UABALv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^SQADDv","^SQSUBv","^UQADDv","^UQSUBv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^SUQADDv","^USQADDv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^ADDHNv","^RADDHNv", "^RSUBHNv",
                       "^SQABS", "^SQADD", "^SQNEG", "^SQSUB",
                       "^SRHADD", "^SUBHNv", "^SUQADD",
                       "^UQADD", "^UQSUB", "^URHADD", "^USQADD")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^CMEQv","^CMGEv","^CMGTv",
                       "^CMLEv","^CMLTv", "^CMHIv","^CMHSv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^SMAXv","^SMINv","^UMAXv","^UMINv",
                       "^SMAXPv","^SMINPv","^UMAXPv","^UMINPv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^SABDv","^UABDv", "^SABDLv","^UABDLv")>;

//---
// 3.13 ASIMD Floating-point Instructions
//---

// ASIMD FP absolute value
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FABSv")>;

// ASIMD FP arith, normal, D-form
// ASIMD FP arith, normal, Q-form
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123],
            (instregex "^FABDv", "^FADDv", "^FSUBv")>;

// ASIMD FP arith,pairwise, D-form
// ASIMD FP arith, pairwise, Q-form
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123], (instregex "^FADDPv")>;

// ASIMD FP compare, D-form
// ASIMD FP compare, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FACGEv", "^FACGTv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FCMEQv", "^FCMGEv",
                                                 "^FCMGTv", "^FCMLEv",
                                                 "^FCMLTv")>;

// ASIMD FP round, D-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FRINT[AIMNPXZ](v2f32)")>;
// ASIMD FP round, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FRINT[AIMNPXZ](v4f32|v2f64)")>;

// ASIMD FP convert, long
// ASIMD FP convert, narrow
// ASIMD FP convert, other, D-form
// ASIMD FP convert, other, Q-form
// NOTE: Handled by WriteV.

// ASIMD FP convert, long and narrow
def : InstRW<[THX3T110Write_5Cyc_F01], (instregex "^FCVT(L|N|XN)v")>;
// ASIMD FP convert, other, D-form
def : InstRW<[THX3T110Write_5Cyc_F01],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v2f32|v1i32|v2i32|v1i64)")>;
// ASIMD FP convert, other, Q-form
def : InstRW<[THX3T110Write_5Cyc_F01],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP divide, D-form, F32
def : InstRW<[THX3T110Write_16Cyc_F0123], (instrs FDIVv2f32)>;
def : InstRW<[THX3T110Write_16Cyc_F0123], (instregex "FDIVv2f32")>;

// ASIMD FP divide, Q-form, F32
def : InstRW<[THX3T110Write_16Cyc_F0123], (instrs FDIVv4f32)>;
def : InstRW<[THX3T110Write_16Cyc_F0123], (instregex "FDIVv4f32")>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[THX3T110Write_23Cyc_F0123], (instrs FDIVv2f64)>;
def : InstRW<[THX3T110Write_23Cyc_F0123], (instregex "FDIVv2f64")>;

// ASIMD FP max/min, normal, D-form
// ASIMD FP max/min, normal, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FMAXv", "^FMAXNMv",
                                                "^FMINv", "^FMINNMv")>;

// ASIMD FP max/min, pairwise, D-form
// ASIMD FP max/min, pairwise, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FMAXPv", "^FMAXNMPv",
                                                "^FMINPv", "^FMINNMPv")>;

// ASIMD FP max/min, reduce
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FMAXVv", "^FMAXNMVv",
                                                "^FMINVv", "^FMINNMVv")>;

// ASIMD FP multiply, D-form, FZ
// ASIMD FP multiply, D-form, no FZ
// ASIMD FP multiply, Q-form, FZ
// ASIMD FP multiply, Q-form, no FZ
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FMULv", "^FMULXv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FMULX?(v2f32|v1i32|v2i32|v1i64|32|64)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FMULX?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP multiply accumulate, Dform, FZ
// ASIMD FP multiply accumulate, Dform, no FZ
// ASIMD FP multiply accumulate, Qform, FZ
// ASIMD FP multiply accumulate, Qform, no FZ
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FMLAv", "^FMLSv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FML[AS](v2f32|v1i32|v2i32|v1i64)")>;
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FML[AS](v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP negate
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^FNEGv")>;

//--
// 3.14 ASIMD Miscellaneous Instructions
//--

// ASIMD bit reverse
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^RBITv")>;

// ASIMD bitwise insert, D-form
// ASIMD bitwise insert, Q-form
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123],
            (instregex "^BIFv", "^BITv", "^BSLv")>;

// ASIMD count, D-form
// ASIMD count, Q-form
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123],
            (instregex "^CLSv", "^CLZv", "^CNTv")>;

// ASIMD duplicate, gen reg
// ASIMD duplicate, element
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^DUPv")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^DUP(i8|i16|i32|i64)$")>;
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^DUPv.+gpr")>;

// ASIMD extract
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^EXTv")>;

// ASIMD extract narrow
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^XTNv")>;

// ASIMD extract narrow, saturating
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^SQXTNv", "^SQXTUNv", "^UQXTNv")>;

// ASIMD insert, element to element
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^INSv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^[SU]MOVv")>;

// ASIMD move, integer immed
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123], (instregex "^MOVIv")>;

// ASIMD move, FP immed
def : InstRW<[THX3T110Write_3_4Cyc_F23_F0123], (instregex "^FMOVv")>;

// ASIMD transpose
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^TRN1", "^TRN2")>;

// ASIMD unzip/zip
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^UZP1", "^UZP2", "^ZIP1", "^ZIP2")>;

// ASIMD reciprocal estimate, D-form
// ASIMD reciprocal estimate, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^FRECPEv", "^FRECPXv", "^URECPEv",
                       "^FRSQRTEv", "^URSQRTEv")>;

// ASIMD reciprocal step, D-form, FZ
// ASIMD reciprocal step, D-form, no FZ
// ASIMD reciprocal step, Q-form, FZ
// ASIMD reciprocal step, Q-form, no FZ
def : InstRW<[THX3T110Write_5Cyc_F0123],
              (instregex "^FRECPSv", "^FRSQRTSv")>;

// ASIMD reverse
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instregex "^REV16v", "^REV32v", "^REV64v")>;

// ASIMD table lookup, D-form
// ASIMD table lookup, Q-form
def : InstRW<[THX3T110Write_5Cyc_F0123],
            (instrs TBLv8i8One, TBLv16i8One, TBXv8i8One, TBXv16i8One)>;
def : InstRW<[THX3T110Write_10Cyc_F0123],
            (instrs TBLv8i8Two, TBLv16i8Two, TBXv8i8Two, TBXv16i8Two)>;
def : InstRW<[THX3T110Write_15Cyc_F0123],
            (instrs TBLv8i8Three, TBLv16i8Three, TBXv8i8Three, TBXv16i8Three)>;
def : InstRW<[THX3T110Write_20Cyc_F0123],
            (instrs TBLv8i8Four, TBLv16i8Four, TBXv8i8Four, TBXv16i8Four)>;

// ASIMD transfer, element to word or word
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^[SU]MOVv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "(S|U)MOVv.*")>;

// ASIMD transfer gen reg to element
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^INSv")>;

// ASIMD transpose
def : InstRW<[THX3T110Write_5Cyc_F0123],
              (instregex "^TRN1v", "^TRN2v", "^UZP1v", "^UZP2v")>;

// ASIMD unzip/zip
def : InstRW<[THX3T110Write_5Cyc_F0123], (instregex "^ZIP1v", "^ZIP2v")>;

//--
// 3.15 ASIMD Load Instructions
//--

// ASIMD load, 1 element, multiple, 1 reg, D-form
// ASIMD load, 1 element, multiple, 1 reg, Q-form
def : InstRW<[THX3T110Write_4Cyc_LS01],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01, WriteAdr],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, D-form
// ASIMD load, 1 element, multiple, 2 reg, Q-form
def : InstRW<[THX3T110Write_4Cyc_LS01],
            (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_4Cyc_LS01, WriteAdr],
            (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, D-form
// ASIMD load, 1 element, multiple, 3 reg, Q-form
def : InstRW<[THX3T110Write_5Cyc_LS01],
            (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01, WriteAdr],
            (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form
// ASIMD load, 1 element, multiple, 4 reg, Q-form
def : InstRW<[THX3T110Write_6Cyc_LS01],
            (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_6Cyc_LS01, WriteAdr],
            (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, one lane, B/H/S
// ASIMD load, 1 element, one lane, D
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123],
            (instregex "^LD1i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD1i(8|16|32|64)_POST$")>;

// ASIMD load, 1 element, all lanes, D-form, B/H/S
// ASIMD load, 1 element, all lanes, D-form, D
// ASIMD load, 1 element, all lanes, Q-form
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form, B/H/S
// ASIMD load, 2 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, one lane, B/H
// ASIMD load, 2 element, one lane, S
// ASIMD load, 2 element, one lane, D
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123],
            (instregex "^LD2i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD2i(8|16|32|64)_POST$")>;

// ASIMD load, 2 element, all lanes, D-form, B/H/S
// ASIMD load, 2 element, all lanes, D-form, D
// ASIMD load, 2 element, all lanes, Q-form
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_5Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, multiple, D-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_8Cyc_LS01_F0123],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_8Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, one lone, B/H
// ASIMD load, 3 element, one lane, S
// ASIMD load, 3 element, one lane, D
def : InstRW<[THX3T110Write_7Cyc_LS01_F0123],
            (instregex "^LD3i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_7Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD3i(8|16|32|64)_POST$")>;

// ASIMD load, 3 element, all lanes, D-form, B/H/S
// ASIMD load, 3 element, all lanes, D-form, D
// ASIMD load, 3 element, all lanes, Q-form, B/H/S
// ASIMD load, 3 element, all lanes, Q-form, D
def : InstRW<[THX3T110Write_7Cyc_LS01_F0123],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_7Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_8Cyc_LS01_F0123],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_8Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, one lane, B/H
// ASIMD load, 4 element, one lane, S
// ASIMD load, 4 element, one lane, D
def : InstRW<[THX3T110Write_6Cyc_LS01_F0123],
            (instregex "^LD4i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_6Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD4i(8|16|32|64)_POST$")>;

// ASIMD load, 4 element, all lanes, D-form, B/H/S
// ASIMD load, 4 element, all lanes, D-form, D
// ASIMD load, 4 element, all lanes, Q-form, B/H/S
// ASIMD load, 4 element, all lanes, Q-form, D
def : InstRW<[THX3T110Write_6Cyc_LS01_F0123],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_6Cyc_LS01_F0123, WriteAdr],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

//--
// 3.16 ASIMD Store Instructions
//--

// ASIMD store, 1 element, multiple, 1 reg, D-form
// ASIMD store, 1 element, multiple, 1 reg, Q-form
def : InstRW<[THX3T110Write_1Cyc_LS01],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form
// ASIMD store, 1 element, multiple, 2 reg, Q-form
def : InstRW<[THX3T110Write_1Cyc_LS01],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form
// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[THX3T110Write_1Cyc_LS01],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form
// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[THX3T110Write_1Cyc_LS01],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane, B/H/S
// ASIMD store, 1 element, one lane, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST1i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane, B/H/S
// ASIMD store, 2 element, one lane, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST2i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H
// ASIMD store, 3 element, one lane, S
// ASIMD store, 3 element, one lane, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST3i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST3i(8|16|32|64)_POST$")>;

// ASIMD store, 4 element, multiple, D-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 4 element, one lane, B/H
// ASIMD store, 4 element, one lane, S
// ASIMD store, 4 element, one lane, D
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123],
            (instregex "^ST4i(8|16|32|64)$")>;
def : InstRW<[THX3T110Write_1Cyc_LS01_F0123, WriteAdr],
            (instregex "^ST4i(8|16|32|64)_POST$")>;

// V8.1a Atomics (LSE)
def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs CASB, CASH, CASW, CASX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs CASAB, CASAH, CASAW, CASAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs CASLB, CASLH, CASLW, CASLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs CASALB, CASALH, CASALW, CASALX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDLARB, LDLARH, LDLARW, LDLARX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDADDB, LDADDH, LDADDW, LDADDX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDADDAB, LDADDAH, LDADDAW, LDADDAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDADDLB, LDADDLH, LDADDLW, LDADDLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs LDADDALB, LDADDALH, LDADDALW, LDADDALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDCLRB, LDCLRH, LDCLRW, LDCLRX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDCLRAB, LDCLRAH, LDCLRAW, LDCLRAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDCLRLB, LDCLRLH, LDCLRLW, LDCLRLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs LDCLRALB, LDCLRALH, LDCLRALW, LDCLRALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDEORB, LDEORH, LDEORW, LDEORX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDEORAB, LDEORAH, LDEORAW, LDEORAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDEORLB, LDEORLH, LDEORLW, LDEORLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs LDEORALB, LDEORALH, LDEORALW, LDEORALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDSETB, LDSETH, LDSETW, LDSETX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDSETAB, LDSETAH, LDSETAW, LDSETAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs LDSETLB, LDSETLH, LDSETLW, LDSETLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs LDSETALB, LDSETALH, LDSETALW, LDSETALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDSMAXB, LDSMAXH, LDSMAXW, LDSMAXX,
             LDSMAXAB, LDSMAXAH, LDSMAXAW, LDSMAXAX,
             LDSMAXLB, LDSMAXLH, LDSMAXLW, LDSMAXLX,
             LDSMAXALB, LDSMAXALH, LDSMAXALW, LDSMAXALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDSMINB, LDSMINH, LDSMINW, LDSMINX,
             LDSMINAB, LDSMINAH, LDSMINAW, LDSMINAX,
             LDSMINLB, LDSMINLH, LDSMINLW, LDSMINLX,
             LDSMINALB, LDSMINALH, LDSMINALW, LDSMINALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDUMAXB, LDUMAXH, LDUMAXW, LDUMAXX,
             LDUMAXAB, LDUMAXAH, LDUMAXAW, LDUMAXAX,
             LDUMAXLB, LDUMAXLH, LDUMAXLW, LDUMAXLX,
             LDUMAXALB, LDUMAXALH, LDUMAXALW, LDUMAXALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs LDUMINB, LDUMINH, LDUMINW, LDUMINX,
             LDUMINAB, LDUMINAH, LDUMINAW, LDUMINAX,
             LDUMINLB, LDUMINLH, LDUMINLW, LDUMINLX,
             LDUMINALB, LDUMINALH, LDUMINALW, LDUMINALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs SWPB, SWPH, SWPW, SWPX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs SWPAB, SWPAH, SWPAW, SWPAX)>;

def : InstRW<[THX3T110Write_6Cyc_I0123, WriteAtomic],
            (instrs SWPLB, SWPLH, SWPLW, SWPLX)>;

def : InstRW<[THX3T110Write_8Cyc_I0123, WriteAtomic],
            (instrs SWPALB, SWPALH, SWPALW, SWPALX)>;

def : InstRW<[THX3T110Write_4Cyc_I0123, WriteAtomic],
            (instrs STLLRB, STLLRH, STLLRW, STLLRX)>;

// V8.3a PAC
def : InstRW<[THX3T110Write_11Cyc_LS01_I1], (instregex "^LDRAA", "^LDRAB")>;
def : InstRW<[THX3T110Write_8Cyc_I123],
            (instrs BLRAA, BLRAAZ, BLRAB, BLRABZ,
                    BRAA, BRAAZ, BRAB, BRABZ)>;
def : InstRW<[THX3T110Write_8Cyc_I123], (instrs RETAA, RETAB)>;

} // SchedModel = ThunderX3T110Model
