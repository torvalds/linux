//=- AArch64SchedAmpere1.td - Ampere-1 scheduling def -----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Ampere Computing Ampere-1 to
// support instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

// The Ampere-1 core is an out-of-order micro-architecture.  The front
// end has branch prediction, with a 10-cycle recovery time from a
// mispredicted branch.  Instructions coming out of the front end are
// decoded into internal micro-ops (uops).

def Ampere1Model : SchedMachineModel {
  let IssueWidth            =   4;  // 4-way decode and dispatch
  let MicroOpBufferSize     = 192;  // re-order buffer size
  let LoadLatency           =   4;  // Optimistic load latency
  let MispredictPenalty     =  10;  // Branch mispredict penalty
  let LoopMicroOpBufferSize =  32;  // Instruction queue size
  let CompleteModel = 0;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    SMEUnsupported.F,
                                                    PAUnsupported.F,
                                                    [HasMTE]);
}

let SchedModel = Ampere1Model in {

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Ampere-1.
// Ampere-1 has 12 pipelines that 8 independent scheduler (4 integer, 2 FP,
// and 2 memory) issue into.  The integer and FP schedulers can each issue
// one uop per cycle, while the memory schedulers can each issue one load
// and one store address calculation per cycle.

def Ampere1UnitA  : ProcResource<2>;  // integer single-cycle, branch, and flags r/w
def Ampere1UnitB  : ProcResource<2>;  // integer single-cycle, and complex shifts
def Ampere1UnitBS : ProcResource<1>;  // integer multi-cycle
def Ampere1UnitL  : ProcResource<2>;  // load
def Ampere1UnitS  : ProcResource<2>;  // store address calculation
def Ampere1UnitX  : ProcResource<1>;  // FP and vector operations, and flag write
def Ampere1UnitY  : ProcResource<1>;  // FP and vector operations, and crypto
def Ampere1UnitZ  : ProcResource<1>;  // FP store data and FP-to-integer moves

def Ampere1UnitAB : ProcResGroup<[Ampere1UnitA, Ampere1UnitB]>;
def Ampere1UnitXY : ProcResGroup<[Ampere1UnitX, Ampere1UnitY]>;

//===----------------------------------------------------------------------===//
// Define customized scheduler read/write types specific to the Ampere-1.

def Ampere1Write_1cyc_1A : SchedWriteRes<[Ampere1UnitA]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1Write_1cyc_2A : SchedWriteRes<[Ampere1UnitA, Ampere1UnitA]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1Write_1cyc_1B : SchedWriteRes<[Ampere1UnitB]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1Write_1cyc_1AB : SchedWriteRes<[Ampere1UnitAB]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1Write_1cyc_1L : SchedWriteRes<[Ampere1UnitL]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1Write_1cyc_1S : SchedWriteRes<[Ampere1UnitS]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1Write_1cyc_2S : SchedWriteRes<[Ampere1UnitS, Ampere1UnitS]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1Y : SchedWriteRes<[Ampere1UnitY]> {
  let Latency = 2;
  let NumMicroOps = 1;
}

def Ampere1Write_2cyc_2AB : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1B_1AB : SchedWriteRes<[Ampere1UnitB, Ampere1UnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1B_1A : SchedWriteRes<[Ampere1UnitB, Ampere1UnitA]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1AB_1A : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitA]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1AB_1L : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitL]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1AB_2S : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitS,
                                                             Ampere1UnitS]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1Write_2cyc_1AB_1S_1Z : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitS,
                                                                Ampere1UnitZ]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1Write_2cyc_1B_1S : SchedWriteRes<[Ampere1UnitB, Ampere1UnitS]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_2cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 2;
  let NumMicroOps = 1;
}

def Ampere1Write_2cyc_1S_1Z : SchedWriteRes<[Ampere1UnitS, Ampere1UnitZ]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1Write_3cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1Write_3cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1Write_3cyc_1B_1S_1AB : SchedWriteRes<[Ampere1UnitB, Ampere1UnitS,
                                                               Ampere1UnitAB]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1Write_3cyc_1S_2Z : SchedWriteRes<[Ampere1UnitS, Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1Write_3cyc_2S_2Z : SchedWriteRes<[Ampere1UnitS, Ampere1UnitS,
                                             Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 2;
  let NumMicroOps = 4;
}

def Ampere1Write_4cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_1L : SchedWriteRes<[Ampere1UnitL]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_1X : SchedWriteRes<[Ampere1UnitX]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_1Y : SchedWriteRes<[Ampere1UnitY]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_1Z : SchedWriteRes<[Ampere1UnitZ]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_2L : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1Write_4cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1Write_4cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1Write_4cyc_1XY_1S_1Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitS, Ampere1UnitZ]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

def Ampere1Write_4cyc_3S_3Z : SchedWriteRes<[Ampere1UnitS, Ampere1UnitS, Ampere1UnitS,
                                             Ampere1UnitZ, Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 4;
  let NumMicroOps = 6;
}

def Ampere1Write_5cyc_1AB_1L : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitL]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def Ampere1Write_5cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1Write_5cyc_1X : SchedWriteRes<[Ampere1UnitX]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1Write_5cyc_1L : SchedWriteRes<[Ampere1UnitL]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1Write_5cyc_2L : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def Ampere1Write_5cyc_1L_1BS : SchedWriteRes<[Ampere1UnitL, Ampere1UnitBS]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def Ampere1Write_5cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1Write_5cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def Ampere1Write_5cyc_4S_4Z : SchedWriteRes<[Ampere1UnitS, Ampere1UnitS,
                                             Ampere1UnitS, Ampere1UnitS,
                                             Ampere1UnitZ, Ampere1UnitZ,
                                             Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 5;
  let NumMicroOps = 8;
}

def Ampere1Write_5cyc_2XY_2S_2Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 5;
  let NumMicroOps = 6;
}

def Ampere1Write_6cyc_2XY_2S_2Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 6;
  let NumMicroOps = 6;
}

def Ampere1Write_6cyc_3XY_3S_3Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 6;
  let NumMicroOps = 9;
}

def Ampere1Write_6cyc_1AB_1L : SchedWriteRes<[Ampere1UnitAB, Ampere1UnitL]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1Write_6cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 6;
  let NumMicroOps = 1;
}

def Ampere1Write_6cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1Write_6cyc_3XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def Ampere1Write_6cyc_3L : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL, Ampere1UnitL]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def Ampere1Write_6cyc_4L : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                          Ampere1UnitL, Ampere1UnitL]> {
  let Latency = 6;
  let NumMicroOps = 4;
}

def Ampere1Write_6cyc_1XY_1Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitZ]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1Write_7cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 7;
  let NumMicroOps = 1;
}

def Ampere1Write_7cyc_1BS_1XY : SchedWriteRes<[Ampere1UnitBS, Ampere1UnitXY]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1Write_7cyc_1L_1XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitXY]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1Write_7cyc_2L_2XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 7;
  let NumMicroOps = 4;
}

def Ampere1Write_7cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1Write_7cyc_4XY_4S_4Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ,
                                                 Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 7;
  let NumMicroOps = 12;
}

def Ampere1Write_8cyc_1BS_1A : SchedWriteRes<[Ampere1UnitBS, Ampere1UnitA]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Ampere1Write_8cyc_1BS_2A : SchedWriteRes<[Ampere1UnitBS, Ampere1UnitA,
                                                             Ampere1UnitA]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

def Ampere1Write_8cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Ampere1Write_8cyc_4XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                           Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def Ampere1Write_8cyc_3L_3XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 8;
  let NumMicroOps = 6;
}

def Ampere1Write_8cyc_4L_4XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY,
                                              Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 8;
  let NumMicroOps = 8;
}

def Ampere1Write_9cyc_3L_3XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 9;
  let NumMicroOps = 6;
}

def Ampere1Write_9cyc_4L_4XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY,
                                              Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 9;
  let NumMicroOps = 8;
}

def Ampere1Write_9cyc_3XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 9;
  let NumMicroOps = 3;
}

def Ampere1Write_9cyc_2L_3XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                              Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 9;
  let NumMicroOps = 5;
}

def Ampere1Write_9cyc_6XY_4S_4Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ,
                                                 Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 9;
  let NumMicroOps = 14;
}

def Ampere1Write_9cyc_8XY_4S_4Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitXY, Ampere1UnitXY,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitS, Ampere1UnitS,
                                                 Ampere1UnitZ, Ampere1UnitZ,
                                                 Ampere1UnitZ, Ampere1UnitZ]> {
  let Latency = 9;
  let NumMicroOps = 16;
}

def Ampere1Write_10cyc_2XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 10;
  let NumMicroOps = 2;
}

def Ampere1Write_10cyc_1XY_1Z : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitZ]> {
  let Latency = 10;
  let NumMicroOps = 2;
}

def Ampere1Write_10cyc_1X_1Z : SchedWriteRes<[Ampere1UnitX, Ampere1UnitZ]> {
  let Latency = 10;
  let NumMicroOps = 2;
}

def Ampere1Write_10cyc_3L_3XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL, Ampere1UnitL,
                                               Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 10;
  let NumMicroOps = 6;
}

def Ampere1Write_10cyc_1A_1BS_1X : SchedWriteRes<[Ampere1UnitA, Ampere1UnitBS, Ampere1UnitX]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def Ampere1Write_10cyc_1A_1BS_1XY : SchedWriteRes<[Ampere1UnitA, Ampere1UnitBS, Ampere1UnitXY]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

def Ampere1Write_11cyc_1BS_1L : SchedWriteRes<[Ampere1UnitBS, Ampere1UnitL]> {
  let Latency = 11;
  let NumMicroOps = 2;
}

def Ampere1Write_11cyc_1A_1BS_1X : SchedWriteRes<[Ampere1UnitA, Ampere1UnitBS, Ampere1UnitX]> {
  let Latency = 11;
  let NumMicroOps = 3;
}

def Ampere1Write_11cyc_1A_1BS_1XY : SchedWriteRes<[Ampere1UnitA, Ampere1UnitBS, Ampere1UnitXY]> {
  let Latency = 11;
  let NumMicroOps = 3;
}

def Ampere1Write_11cyc_4L_8XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                               Ampere1UnitL, Ampere1UnitL,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 11;
  let NumMicroOps = 12;
}

def Ampere1Write_12cyc_4L_8XY : SchedWriteRes<[Ampere1UnitL, Ampere1UnitL,
                                               Ampere1UnitL, Ampere1UnitL,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY,
                                               Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 12;
  let NumMicroOps = 12;
}

def Ampere1Write_12cyc_3XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 12;
  let NumMicroOps = 3;
}

def Ampere1Write_12cyc_4XY : SchedWriteRes<[Ampere1UnitXY, Ampere1UnitXY,
                                            Ampere1UnitXY, Ampere1UnitXY]> {
  let Latency = 12;
  let NumMicroOps = 4;
}

def Ampere1Write_18cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 18;
  let NumMicroOps = 1;
}

def Ampere1Write_19cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 19;
  let NumMicroOps = 1;
}

def Ampere1Write_25cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 25;
  let NumMicroOps = 1;
}

def Ampere1Write_32cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 32;
  let NumMicroOps = 1;
}

def Ampere1Write_34cyc_1BS : SchedWriteRes<[Ampere1UnitBS]> {
  let Latency = 34;
  let NumMicroOps = 1;
}

def Ampere1Write_34cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 34;
  let NumMicroOps = 1;
}

def Ampere1Write_39cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 39;
  let NumMicroOps = 1;
}

def Ampere1Write_62cyc_1XY : SchedWriteRes<[Ampere1UnitXY]> {
  let Latency = 62;
  let NumMicroOps = 1;
}

// For basic arithmetic, we have more flexibility for short shifts (LSL shift <= 4),
// which are a single uop, and for extended registers, which have full flexibility
// across Unit A or B for both uops.
def Ampere1Write_Arith : SchedWriteVariant<[
                                SchedVar<RegExtendedPred, [Ampere1Write_2cyc_2AB]>,
                                SchedVar<IsCheapLSL,      [Ampere1Write_1cyc_1AB]>,
                                SchedVar<NoSchedPred,     [Ampere1Write_2cyc_1B_1AB]>]>;

def Ampere1Write_ArithFlagsetting : SchedWriteVariant<[
                                SchedVar<RegExtendedPred, [Ampere1Write_2cyc_1AB_1A]>,
                                SchedVar<IsCheapLSL,      [Ampere1Write_1cyc_1A]>,
                                SchedVar<NoSchedPred,     [Ampere1Write_2cyc_1B_1A]>]>;

//===----------------------------------------------------------------------===//
// Map the target-defined scheduler read/write resources and latencies for Ampere-1.
// This provides a coarse model, which is then specialised below.

def : WriteRes<WriteImm,   [Ampere1UnitAB]>;  // MOVN, MOVZ
def : WriteRes<WriteI,     [Ampere1UnitAB]>;  // ALU
def : WriteRes<WriteISReg, [Ampere1UnitB, Ampere1UnitA]> {
  let Latency = 2;
  let NumMicroOps = 2;
}  // ALU of Shifted-Reg
def : WriteRes<WriteIEReg, [Ampere1UnitAB, Ampere1UnitA]> {
  let Latency = 2;
  let NumMicroOps = 2;
}  // ALU of Extended-Reg
def : WriteRes<WriteExtr,  [Ampere1UnitB]>;  // EXTR shifts a reg pair
def : WriteRes<WriteIS,    [Ampere1UnitB]>;  // Shift/Scale
def : WriteRes<WriteID32,  [Ampere1UnitBS]> {
  let Latency = 18;
}  // 32-bit Divide
def : WriteRes<WriteID64,  [Ampere1UnitBS]> {
  let Latency = 34;
}  // 64-bit Divide
def : WriteRes<WriteIM32,  [Ampere1UnitBS]> {
  let Latency = 3;
}  // 32-bit Multiply
def : WriteRes<WriteIM64,  [Ampere1UnitBS]> {
  let Latency = 3;
}  // 32-bit Multiply
def : WriteRes<WriteBr,    [Ampere1UnitA]>;
def : WriteRes<WriteBrReg, [Ampere1UnitA, Ampere1UnitA]>;
def : WriteRes<WriteLD,    [Ampere1UnitL]> {
  let Latency = 4;
}  // Load from base addr plus immediate offset
def : WriteRes<WriteST,    [Ampere1UnitS]> {
  let Latency = 1;
}  // Store to base addr plus immediate offset
def : WriteRes<WriteSTP,   [Ampere1UnitS, Ampere1UnitS]> {
  let Latency = 1;
  let NumMicroOps = 2;
}  // Store a register pair.
def : WriteRes<WriteAdr,   [Ampere1UnitAB]>;
def : WriteRes<WriteLDIdx, [Ampere1UnitAB, Ampere1UnitS]> {
  let Latency = 5;
  let NumMicroOps = 2;
}  // Load from a register index (maybe scaled).
def : WriteRes<WriteSTIdx, [Ampere1UnitS, Ampere1UnitS]> {
  let Latency = 1;
  let NumMicroOps = 2;
}  // Store to a register index (maybe scaled).
def : WriteRes<WriteF,  [Ampere1UnitXY]> {
  let Latency = 2;
}  // General floating-point ops.
def : WriteRes<WriteFCmp,  [Ampere1UnitX]> {
  let Latency = 5;
}  // Floating-point compare.
def : WriteRes<WriteFCvt,  [Ampere1UnitXY]> {
  let Latency = 6;
}  // Float conversion.
def : WriteRes<WriteFCopy, [Ampere1UnitXY]> {
}  // Float-int register copy.
def : WriteRes<WriteFImm,  [Ampere1UnitXY]> {
  let Latency = 2;
}  // Float-int register copy.
def : WriteRes<WriteFMul,  [Ampere1UnitXY]> {
  let Latency = 5;
}  // Floating-point multiply.
def : WriteRes<WriteFDiv,  [Ampere1UnitXY]> {
  let Latency = 34;
}  // Floating-point division.
def : WriteRes<WriteVd,    [Ampere1UnitXY]> {
  let Latency = 3;
}  // 64bit Vector D ops.
def : WriteRes<WriteVq,    [Ampere1UnitXY]> {
  let Latency = 3;
}  // 128bit Vector Q ops.
def : WriteRes<WriteVLD,   [Ampere1UnitL, Ampere1UnitL]> {
  let Latency = 5;
}  // Vector loads.
def : WriteRes<WriteVST,   [Ampere1UnitS, Ampere1UnitZ]> {
  let Latency = 2;
}  // Vector stores.

def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteLDHi,    []> {
  let Latency = 4;
}  // The second register of a load-pair: LDP,LDPSW,LDNP,LDXP,LDAXP

// Forwarding logic.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     1, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadST,      0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;

//===----------------------------------------------------------------------===//
// Specialising the scheduling model further for Ampere-1.

def : InstRW<[Ampere1Write_1cyc_1AB], (instrs COPY)>;

// Branch instructions
def : InstRW<[Ampere1Write_1cyc_1A], (instrs Bcc, BL, RET)>;
def : InstRW<[Ampere1Write_1cyc_1A],
        (instrs CBZW, CBZX, CBNZW, CBNZX, TBZW, TBZX, TBNZW, TBNZX)>;
def : InstRW<[Ampere1Write_1cyc_2A], (instrs BLR)>;

// Cryptography instructions
// -- AES encryption/decryption
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^AES[DE]")>;
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^AESI?MC")>;
// -- Polynomial multiplication
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^PMUL", "^PMULL")>;
// -- SHA-256 hash
def : InstRW<[Ampere1Write_4cyc_1X], (instregex "^SHA256(H|H2)")>;
// -- SHA-256 schedule update
def : InstRW<[Ampere1Write_4cyc_1Y], (instregex "^SHA256SU[01]")>;
// -- SHA-3 instructions
def : InstRW<[Ampere1Write_2cyc_1XY],
        (instregex "^BCAX", "^EOR3", "^RAX1", "^XAR")>;
// -- SHA-512 hash
def : InstRW<[Ampere1Write_4cyc_1X], (instregex "^SHA512(H|H2)")>;
// -- SHA-512 schedule update
def : InstRW<[Ampere1Write_4cyc_1Y], (instregex "^SHA512SU[01]")>;
// -- SHA1 choose/majority/parity
def : InstRW<[Ampere1Write_4cyc_1X], (instregex "^SHA1[CMP]")>;
// -- SHA1 hash/schedule update
def : InstRW<[Ampere1Write_2cyc_1Y], (instregex "^SHA1SU[01]")>;
def : InstRW<[Ampere1Write_2cyc_1Y], (instregex "^SHA1H")>;

// FP and vector load instructions
// -- Load 1-element structure to one/all lanes
// ---- all lanes
def : InstRW<[Ampere1Write_7cyc_1L_1XY],
        (instregex "^LD1Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// ---- one lane
def : InstRW<[Ampere1Write_7cyc_1L_1XY],
        (instregex "^LD1i(8|16|32|64)")>;
// -- Load 1-element structure to one/all lanes, 1D size
def : InstRW<[Ampere1Write_5cyc_1L],
        (instregex "^LD1Rv1d")>;
// -- Load 1-element structures to 1 register
def : InstRW<[Ampere1Write_5cyc_1L],
        (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 2 registers
def : InstRW<[Ampere1Write_5cyc_2L],
        (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 3 registers
def : InstRW<[Ampere1Write_6cyc_3L],
        (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 4 registers
def : InstRW<[Ampere1Write_6cyc_4L],
        (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 2-element structure to all lanes of 2 registers, 1D size
def : InstRW<[Ampere1Write_5cyc_2L],
        (instregex "^LD2Rv1d")>;
// -- Load 2-element structure to all lanes of 2 registers, other sizes
def : InstRW<[Ampere1Write_7cyc_2L_2XY],
        (instregex "^LD2Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 2-element structure to one lane of 2 registers
def : InstRW<[Ampere1Write_7cyc_2L_2XY],
        (instregex "^LD2i(8|16|32|64)")>;
// -- Load 2-element structures to 2 registers, 16B/8H/4S/2D size
def : InstRW<[Ampere1Write_7cyc_2L_2XY],
        (instregex "^LD2Twov(16b|8h|4s|2d)")>;
// -- Load 2-element structures to 2 registers, 8B/4H/2S size
def : InstRW<[Ampere1Write_9cyc_2L_3XY],
        (instregex "^LD2Twov(8b|4h|2s)")>;
// -- Load 3-element structure to all lanes of 3 registers, 1D size
def : InstRW<[Ampere1Write_6cyc_3L],
        (instregex "^LD3Rv1d")>;
// -- Load 3-element structure to all lanes of 3 registers, other sizes
def : InstRW<[Ampere1Write_8cyc_3L_3XY],
        (instregex "^LD3Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 3-element structure to one lane of 3 registers
def : InstRW<[Ampere1Write_8cyc_3L_3XY],
        (instregex "^LD3i(8|16|32|64)")>;
// -- Load 3-element structures to 3 registers, 16B/8H/4S sizes
def : InstRW<[Ampere1Write_9cyc_3L_3XY],
        (instregex "^LD3Threev(16b|8h|4s)")>;
// -- Load 3-element structures to 3 registers, 2D size
def : InstRW<[Ampere1Write_8cyc_3L_3XY],
        (instregex "^LD3Threev2d")>;
// -- Load 3-element structures to 3 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1Write_10cyc_3L_3XY],
        (instregex "^LD3Threev(8b|4h|2s)")>;
// -- Load 4-element structure to all lanes of 4 registers, 1D size
def : InstRW<[Ampere1Write_6cyc_4L],
        (instregex "^LD4Rv1d")>;
// -- Load 4-element structure to all lanes of 4 registers, other sizes
def : InstRW<[Ampere1Write_8cyc_4L_4XY],
        (instregex "^LD4Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 4-element structure to one lane of 4 registers
def : InstRW<[Ampere1Write_6cyc_4L],
        (instregex "^LD4i(8|16|32|64)")>;
// -- Load 4-element structures to 4 registers, 2D size
def : InstRW<[Ampere1Write_9cyc_4L_4XY],
        (instregex "^LD4Fourv2d")>;
// -- Load 4-element structures to 4 registers, 2S size
def : InstRW<[Ampere1Write_12cyc_4L_8XY],
        (instregex "^LD4Fourv2s")>;
// -- Load 4-element structures to 4 registers, other sizes
def : InstRW<[Ampere1Write_11cyc_4L_8XY],
        (instregex "^LD4Fourv(8b|4h|16b|8h|4s)")>;
// -- Load pair, Q-form
def : InstRW<[Ampere1Write_5cyc_2L], (instregex "LDN?PQ")>;
// -- Load pair, S/D-form
def : InstRW<[Ampere1Write_5cyc_1L_1BS], (instregex "LDN?P(S|D)")>;
// -- Load register
def : InstRW<[Ampere1Write_5cyc_1L], (instregex "LDU?R[BHSDQ]i")>;
// -- Load register, sign-extended register
def : InstRW<[Ampere1Write_6cyc_1AB_1L], (instregex "LDR[BHSDQ]ro(W|X)")>;

// FP and vector store instructions
// -- Store 1-element structure from one lane of 1 register
def : InstRW<[Ampere1Write_4cyc_1XY_1S_1Z],
        (instregex "^ST1i(8|16|32|64)")>;
// -- Store 1-element structures from 1 register
def : InstRW<[Ampere1Write_2cyc_1S_1Z],
        (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 2 registers
def : InstRW<[Ampere1Write_3cyc_2S_2Z],
        (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 3 registers
def : InstRW<[Ampere1Write_4cyc_3S_3Z],
        (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 4 registers
def : InstRW<[Ampere1Write_5cyc_4S_4Z],
        (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 2-element structure from one lane of 2 registers
def : InstRW<[Ampere1Write_5cyc_2XY_2S_2Z],
        (instregex "^ST2i(8|16|32|64)")>;
// -- Store 2-element structures from 2 registers, 16B/8H/4S/2D sizes
def : InstRW<[Ampere1Write_5cyc_2XY_2S_2Z],
        (instregex "^ST2Twov(16b|8h|4s|2d)")>;
// -- Store 2-element structures from 2 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1Write_6cyc_2XY_2S_2Z],
        (instregex "^ST2Twov(8b|4h|2s)")>;
// -- Store 3-element structure from one lane of 3 registers
def : InstRW<[Ampere1Write_6cyc_3XY_3S_3Z],
        (instregex "^ST3i(8|16|32|64)")>;
// -- Store 3-element structures from 3 registers
def : InstRW<[Ampere1Write_6cyc_3XY_3S_3Z],
        (instregex "^ST3Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 4-element structure from one lane of 4 registers
def : InstRW<[Ampere1Write_7cyc_4XY_4S_4Z],
        (instregex "^ST4i(8|16|32|64)")>;
// -- Store 4-element structures from 4 registers, 16B/8H/4S sizes
def : InstRW<[Ampere1Write_9cyc_8XY_4S_4Z],
        (instregex "^ST4Fourv(16b|8h|4s)")>;
// -- Store 4-element structures from 4 registers, 2D sizes
def : InstRW<[Ampere1Write_7cyc_4XY_4S_4Z],
        (instregex "^ST4Fourv2d")>;
// -- Store 4-element structures from 4 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1Write_9cyc_6XY_4S_4Z],
        (instregex "^ST4Fourv(8b|4h|2s)")>;
// -- Store pair, Q-form
def : InstRW<[Ampere1Write_3cyc_2S_2Z], (instregex "^STN?PQ")>;
// -- Store pair, S/D-form
def : InstRW<[Ampere1Write_3cyc_1S_2Z],	(instregex "^STN?P[SD]")>;
// -- Store register
def : InstRW<[Ampere1Write_2cyc_1S_1Z],	(instregex "^STU?R[BHSDQ](ui|i)")>;
// -- Store register, sign-extended register offset
def : InstRW<[Ampere1Write_2cyc_1AB_1S_1Z], (instregex "^STR[BHSDQ]ro[XW]")>;

// FP data processing, bfloat16 format
def : InstRW<[Ampere1Write_5cyc_1XY], (instrs BFCVT)>;
def : InstRW<[Ampere1Write_7cyc_2XY], (instrs BFCVTN, BFCVTN2)>;
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^BFDOTv", "^BF16DOT")>;
def : InstRW<[Ampere1Write_4cyc_2XY], (instrs BFMMLA)>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^BFMLAL")>;

// FP data processing, scalar/vector, half precision
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^F(ABD|ABS)v.[fi]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY],
        (instregex "^F(ADD|ADDP|CADD|NEG|NMUL|SUB)v.[fi]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v.[fi]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)16")>;
def : InstRW<[Ampere1Write_4cyc_1X],
        (instregex "^FCMPE?H")>;
def : InstRW<[Ampere1Write_10cyc_1A_1BS_1X],
        (instregex "^FCCMPE?H")>;
def : InstRW<[Ampere1Write_10cyc_1A_1BS_1XY],
        (instregex "^FCSELH")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FCVT[AMNPZ][SU]v.[if]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^[SU]CVTFv.[fi]16")>;
def : InstRW<[Ampere1Write_25cyc_1XY], (instregex "^FDIVv.[if]16", "FDIVH")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^F(MAX|MIN)(NM)?P?v.[if]16")>;
def : InstRW<[Ampere1Write_8cyc_2XY], (instregex "^F(MAX|MIN)(NM)?Vv4[if]16")>;
def : InstRW<[Ampere1Write_12cyc_3XY], (instregex "^F(MAX|MIN)(NM)?Vv8[if]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FMULX?v.[if]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instrs FMULX16)>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FN?M(ADD|SUB)[H]rrr")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FML[AS]v.[if]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FRECPXv.[if]16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^F(RECP|RSQRT)S16")>;
def : InstRW<[Ampere1Write_4cyc_1XY], (instregex "^FRINT[AIMNPXZ]v.[if]16")>;
def : InstRW<[Ampere1Write_39cyc_1XY], (instregex "^FSQRTv.f16", "^FSQRTHr")>;

// FP data processing, scalar/vector, single/double precision
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^F(ABD|ABS)v.[fi](32|64)")>;
def : InstRW<[Ampere1Write_5cyc_1XY],
        (instregex "^F(ADD|ADDP|CADD|NEG|NMUL|SUB)v.[fi](32|64)")>;
def : InstRW<[Ampere1Write_5cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v.[fi](32|64)")>;
def : InstRW<[Ampere1Write_5cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)(32|64)")>;
def : InstRW<[Ampere1Write_5cyc_1X],
        (instregex "^FCMPE?(S|D)")>;
def : InstRW<[Ampere1Write_11cyc_1A_1BS_1X],
        (instregex "^FCCMPE?(S|D)")>;
def : InstRW<[Ampere1Write_11cyc_1A_1BS_1XY],
        (instregex "^FCSEL(S|D)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FCVT[AMNPZ][SU]v.[if](32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^[SU]CVTFv.[fi](32|64)")>;
def : InstRW<[Ampere1Write_34cyc_1XY], (instregex "^FDIVv.[if](64)", "FDIVD")>;
def : InstRW<[Ampere1Write_19cyc_1XY], (instregex "^FDIVv.[if](32)", "FDIVS")>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^F(MAX|MIN)(NM)?P?v.[if](32|64)")>;
def : InstRW<[Ampere1Write_10cyc_2XY], (instregex "^F(MAX|MIN)(NM)?Vv.[if](32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FMULX?v.[if](32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instrs FMULX32, FMULX64)>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^FN?M(ADD|SUB)[SD]rrr")>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^FML[AS]v.[if](32|64)")>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^FRECPXv.[if](32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^F(RECP|RSQRT)S(32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FRINT[AIMNPXZ]v.[if](32|64)")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FRINT(32|64)")>;
def : InstRW<[Ampere1Write_62cyc_1XY], (instregex "^FSQRTv.f64", "^FSQRTDr")>;
def : InstRW<[Ampere1Write_32cyc_1XY], (instregex "^FSQRTv.f32", "^FSQRTSr")>;

// FP miscellaneous instructions
def : InstRW<[Ampere1Write_10cyc_1XY_1Z], (instregex "^FCVT[AMNPZ][SU][SU][XW][HSD]r")>;
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^FCVT[HSD]Hr")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FCVT[HSD][SD]r")>;
def : InstRW<[Ampere1Write_6cyc_1XY], (instregex "^FCVTLv")>;
def : InstRW<[Ampere1Write_8cyc_2XY], (instregex "^FCVT(N|XN)v")>;
def : InstRW<[Ampere1Write_10cyc_1X_1Z], (instrs FJCVTZS)>;
def : InstRW<[Ampere1Write_5cyc_1BS], (instregex "^FMOV[HSD][WX]r")>;
def : InstRW<[Ampere1Write_7cyc_1BS_1XY], (instregex "^FMOVDXHighr")>;
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^FMOV[HSD][ri]")>;
def : InstRW<[Ampere1Write_6cyc_1XY_1Z], (instregex "^FMOVXDHighr")>;
def : InstRW<[Ampere1Write_4cyc_1Z], (instregex "^FMOV[WX][HSD]r")>;

// Integer arithmetic and logical instructions
def : InstRW<[Ampere1Write_1cyc_1A],
        (instregex "ADC(W|X)r", "SBC(W|X)r")>;
def : InstRW<[Ampere1Write_Arith],
        (instregex "(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)(W|X)r[sx]")>;
def : InstRW<[Ampere1Write_1cyc_1AB],
        (instregex "(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)(W|X)r[ri]")>;
def : InstRW<[Ampere1Write_ArithFlagsetting],
        (instregex "(ADD|AND|BIC|SUB)S(W|X)r[sx]")>;
def : InstRW<[Ampere1Write_1cyc_1A],
        (instregex "(ADD|AND|BIC|SUB)S(W|X)r[ri]")>;
def : InstRW<[Ampere1Write_1cyc_1A],
        (instregex "(ADC|SBC)S(W|X)r")>;
def : InstRW<[Ampere1Write_1cyc_1A], (instrs RMIF)>;
def : InstRW<[Ampere1Write_1cyc_1A],
        (instregex "(CCMN|CCMP)(X|W)")>;
def : InstRW<[Ampere1Write_1cyc_1A],
        (instregex "(CSEL|CSINC|CSINV|CSNEG)(X|W)")>;
def : InstRW<[Ampere1Write_18cyc_1BS], (instrs SDIVWr, UDIVWr)>;
def : InstRW<[Ampere1Write_34cyc_1BS], (instrs SDIVXr, UDIVXr)>;
def : InstRW<[Ampere1Write_3cyc_1BS],
        (instregex "(S|U)MULHr")>;
def : InstRW<[Ampere1Write_4cyc_1BS],
        (instregex "(S|U)?M(ADD|SUB)L?r")>;

// Integer load instructions
def : InstRW<[Ampere1Write_4cyc_2L],
        (instregex "(LDNP|LDP|LDPSW)(X|W)")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDR(B|D|H|Q|S)ui")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDR(D|Q|W|X)l")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDTR(B|H|W|X)i")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDTRS(BW|BX|HW|HX|W)i")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDUR(BB|HH|X|W)i")>;
def : InstRW<[Ampere1Write_4cyc_1L],
        (instregex "LDURS(BW|BX|HW|HX|W)i")>;
def : InstRW<[Ampere1Write_5cyc_1AB_1L],
        (instregex "LDR(HH|SHW|SHX|W|X)ro(W|X)")>;
def : InstRW<[Ampere1Write_1cyc_1L],
        (instrs PRFMl, PRFUMi, PRFUMi)>;
def : InstRW<[Ampere1Write_2cyc_1AB_1L],
        (instrs PRFMroW, PRFMroX)>;

// Integer miscellaneous instructions
def : InstRW<[Ampere1Write_1cyc_1A],  (instrs ADR, ADRP)>;
def : InstRW<[Ampere1Write_1cyc_1B],  (instregex "EXTR(W|X)")>;
def : InstRW<[Ampere1Write_1cyc_1B],  (instregex "(S|U)?BFM(W|X)")>;
def : InstRW<[Ampere1Write_3cyc_1BS], (instregex "^CRC32C?[BHWX]")>;
def : InstRW<[Ampere1Write_1cyc_1B],  (instregex "CLS(W|X)")>;
def : InstRW<[Ampere1Write_1cyc_1A],  (instrs SETF8, SETF16)>;
def : InstRW<[Ampere1Write_1cyc_1AB],
        (instrs MOVKWi, MOVKXi, MOVNWi, MOVNXi, MOVZWi, MOVZXi)>;
def : InstRW<[Ampere1Write_1cyc_1B],
        (instregex "(RBIT|REV|REV16)(W|X)r", "REV32Xr")>;
def : InstRW<[Ampere1Write_1cyc_1B],
        (instregex "(ASR|LSL|LSR|ROR)V(W|X)r")>;

// Integer store instructions
def : InstRW<[Ampere1Write_1cyc_2S],  (instregex "STNP(X|W)i")>;
def : InstRW<[Ampere1Write_2cyc_1B_1S],
        (instrs STPWi, STPXi)>;
def : InstRW<[Ampere1Write_3cyc_1B_1S_1AB],
        (instregex "STP(W|X)(pre|post)")>;
def : InstRW<[Ampere1Write_1cyc_1S],
        (instrs STTRBi, STTRHi, STTRWi, STTRXi)>;
def : InstRW<[Ampere1Write_1cyc_1S],
        (instregex "STUR(BB|HH|X|W)i",
                   "STR(X|W)ui",
                   "STUR(BB|HH|X|W)i")>;
def : InstRW<[Ampere1Write_1cyc_2S], (instrs STRWroX, STRXroX)>;
def : InstRW<[Ampere1Write_2cyc_1AB_2S], (instrs STRWroW, STRXroW)>;

// Pointer authentication
//def : InstRW<[Ampere1Write_7cyc_1BS],
//	(instrs AUTIAZ, AUTIBZ, AUTIASP, AUTIBSP, AUTIA1716, AUTIB1716)>;
def : InstRW<[Ampere1Write_8cyc_1BS_1A],
        (instregex "BRA(A|AZ|B|BZ)", "RETA(A|B)", "ERETA(A|B)")>;
def : InstRW<[Ampere1Write_8cyc_1BS_2A],
        (instrs BLRAA, BLRAAZ, BLRAB, BLRABZ)>;
//def : InstRW<[Ampere1Write_7cyc_1BS],
//	(instrs PACIAZ, PACIBZ, PACIASP, PACIBSP, PACIA1716, PACIB1716)>;
def : InstRW<[Ampere1Write_11cyc_1BS_1L], (instregex "^LDRA(A|B)")>;
def : InstRW<[Ampere1Write_7cyc_1BS], (instrs XPACD, XPACI)>;

// Vector integer instructions
// -- absolute difference
def : InstRW<[Ampere1Write_3cyc_1XY],
             (instregex "^SABAv", "^SABALv", "^SABDv", "^SABDLv",
                        "^UABAv", "^UABALv", "^UABDv", "^UABDLv")>;
// -- arithmetic
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^ABSv", "^(ADD|SUB)v", "^SADDLv", "^SADDW", "SHADD",
                   "SHSUB", "^SRHADD", "^URHADD", "SSUBL", "SSUBW",
                   "^UADDLv", "^UADDW", "UHADD", "UHSUB", "USUBL", "USUBW")>;
// -- arithmetic, horizontal, 16B
def : InstRW<[Ampere1Write_12cyc_4XY],
            (instregex "^ADDVv16i8v", "^SADDLVv16i8v", "^UADDLVv16i8v")>;
def : InstRW<[Ampere1Write_12cyc_4XY],
            (instregex "^[SU](MIN|MAX)Vv16i8v")>;
// -- arithmetic, horizontal, 4H/4S
def : InstRW<[Ampere1Write_6cyc_2XY],
            (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v")>;
def : InstRW<[Ampere1Write_6cyc_2XY],
            (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v")>;
// -- arithmetic, horizontal, 8B/8H
def : InstRW<[Ampere1Write_9cyc_3XY],
            (instregex "^[SU]?ADDL?V(v8i16|v4i32)v")>;
def : InstRW<[Ampere1Write_9cyc_3XY],
            (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v")>;
// -- arithmetic, narrowing
def : InstRW<[Ampere1Write_5cyc_2XY], (instregex "(ADD|SUB)HNv.*")>;
def : InstRW<[Ampere1Write_5cyc_2XY], (instregex "(RADD|RSUB)HNv.*")>;
// -- arithmetic, pairwise
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^ADDPv", "^SADALP", "^UADALP", "^SADDLPv", "^UADDLPv")>;
// -- arithmetic, saturating
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^SQADD", "^SQSUB", "^SUQADD", "^UQADD", "^UQSUB", "^USQADD")>;
// -- bit count
def : InstRW<[Ampere1Write_2cyc_1XY],
        (instregex "^(CLS|CLZ|CNT)v")>;
// -- compare
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^CMEQv", "^CMGEv", "^CMGTv", "^CMLEv", "^CMLTv",
                   "^CMHIv", "^CMHSv")>;
// -- compare non-zero
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^CMTSTv")>;
// -- dot product
def : InstRW<[Ampere1Write_3cyc_1XY], (instregex "^(S|SU|U|US)DOTv")>;
// -- fp reciprocal estimate
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^FRECPEv", "^FRSQRTEv")>;
// -- integer reciprocal estimate
def : InstRW<[Ampere1Write_5cyc_1XY], (instregex "^URECPEv", "^URSQRTEv")>;
// -- logical
def : InstRW<[Ampere1Write_2cyc_1XY],
        (instregex "^ANDv", "^BICv", "^EORv", "^ORRv", "^ORNv", "^NOTv")>;
// -- logical, narrowing
def : InstRW<[Ampere1Write_5cyc_2XY],
        (instregex "RSHRNv",
                   "SHRNv", "SQSHRNv", "SQSHRUNv",
                   "UQXTNv")>;
// -- matrix multiply
def : InstRW<[Ampere1Write_6cyc_2XY],
        (instrs SMMLA, UMMLA, USMMLA)>;
// -- max/min
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^SMAXv", "^SMINv", "^UMAXv", "^UMINv")>;
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^SMAXPv", "^SMINPv", "^UMAXPv", "^UMINPv")>;
// -- move immediate
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^MOVIv", "^MVNIv")>;
// -- multiply
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "MULv", "SMULLv", "UMULLv", "SQDMUL(H|L)v", "SQRDMULHv")>;
// -- multiply accumulate
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "MLAv", "MLSv", "(S|U|SQD)(MLAL|MLSL)v", "SQRDML(A|S)Hv")>;
// -- negation, saturating
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^SQABS", "^SQNEG")>;
// -- reverse bits/bytes
def : InstRW<[Ampere1Write_2cyc_1XY],
        (instregex "^RBITv", "^REV16v", "^REV32v", "^REV64v")>;
// -- shift
def : InstRW<[Ampere1Write_3cyc_1XY], (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// -- shift and accumulate
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "SRSRAv", "SSRAv", "URSRAv", "USRAv")>;
// -- shift, saturating
def : InstRW<[Ampere1Write_3cyc_1XY],
        (instregex "^SQRSHLv", "^SQRSHRNv", "^SQRSHRUNv", "^SQSHL", "^SQSHLU",
                   "^SQXTNv", "^SQXTUNv", "^UQSHRNv", "UQRSHRNv", "^UQRSHL",
                   "^UQSHL")>;

// Vector miscellaneous instructions
// -- duplicate element
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^DUPv.+lane")>;
// -- duplicate from GPR
def : InstRW<[Ampere1Write_5cyc_1BS], (instregex "^DUPv.+gpr")>;
// -- extract narrow
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^XTNv")>;
// -- insert/extract element
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^EXTv", "^INSv.+lane")>;
// -- move FP immediate
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^FMOVv")>;
// -- move element to GPR
def : InstRW<[Ampere1Write_6cyc_1XY_1Z], (instregex "(S|U)MOVv")>;
// -- move from GPR to any element
def : InstRW<[Ampere1Write_7cyc_1BS_1XY], (instregex "^INSv.+gpr")>;
// -- table lookup
def : InstRW<[Ampere1Write_2cyc_1XY],
            (instrs TBLv8i8One, TBLv16i8One, TBXv8i8One, TBXv16i8One)>;
def : InstRW<[Ampere1Write_4cyc_2XY],
            (instrs TBLv8i8Two, TBLv16i8Two, TBXv8i8Two, TBXv16i8Two)>;
def : InstRW<[Ampere1Write_6cyc_3XY],
            (instrs TBLv8i8Three, TBLv16i8Three, TBXv8i8Three, TBXv16i8Three)>;
def : InstRW<[Ampere1Write_8cyc_4XY],
            (instrs TBLv8i8Four, TBLv16i8Four, TBXv8i8Four, TBXv16i8Four)>;
// -- transpose
def : InstRW<[Ampere1Write_2cyc_1XY],
              (instregex "^TRN1v", "^TRN2v", "^UZP1v", "^UZP2v")>;
// -- zip/unzip
def : InstRW<[Ampere1Write_2cyc_1XY], (instregex "^ZIP1v", "^ZIP2v")>;

} // SchedModel = Ampere1Model
