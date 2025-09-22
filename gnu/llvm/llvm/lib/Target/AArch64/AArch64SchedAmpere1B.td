//=- AArch64SchedAmpere1B.td - Ampere-1B scheduling def -----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Ampere Computing Ampere-1B to
// support instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

// The Ampere-1B core is an out-of-order micro-architecture.  The front
// end has branch prediction, with a 10-cycle recovery time from a
// mispredicted branch.  Instructions coming out of the front end are
// decoded into internal micro-ops (uops).

def Ampere1BModel : SchedMachineModel {
  let IssueWidth            =  12;  // Maximum micro-ops dispatch rate.
  let MicroOpBufferSize     = 208;  // micro-op re-order buffer size
  let LoadLatency           =   3;  // Optimistic load latency
  let MispredictPenalty     =  10;  // Branch mispredict penalty
  let LoopMicroOpBufferSize =  32;  // Instruction queue size
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    SMEUnsupported.F,
                                                    PAUnsupported.F);
}

let SchedModel = Ampere1BModel in {

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on Ampere-1B.

def Ampere1BUnitA  : ProcResource<2>;  // integer single-cycle, branch, and flags r/w
def Ampere1BUnitB  : ProcResource<2>;  // integer single-cycle, and complex shifts
def Ampere1BUnitBS : ProcResource<1>;  // integer multi-cycle
def Ampere1BUnitL  : ProcResource<2>;  // load
def Ampere1BUnitS  : ProcResource<2>;  // store address calculation
def Ampere1BUnitX  : ProcResource<1>;  // FP and vector operations, and flag write
def Ampere1BUnitY  : ProcResource<1>;  // FP and vector operations, and crypto
def Ampere1BUnitZ  : ProcResource<1>;  // FP store data and FP-to-integer moves

def Ampere1BUnitAB : ProcResGroup<[Ampere1BUnitA, Ampere1BUnitB]>;
def Ampere1BUnitXY : ProcResGroup<[Ampere1BUnitX, Ampere1BUnitY]>;

//===----------------------------------------------------------------------===//
// Define customized scheduler read/write types specific to the Ampere-1.

def Ampere1BWrite_1cyc_1A : SchedWriteRes<[Ampere1BUnitA]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_2A : SchedWriteRes<[Ampere1BUnitA, Ampere1BUnitA]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1BWrite_1cyc_1B : SchedWriteRes<[Ampere1BUnitB]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_1BS : SchedWriteRes<[Ampere1BUnitBS]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_1BS_1B : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitB]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1BWrite_1cyc_1AB : SchedWriteRes<[Ampere1BUnitAB]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_1AB_1A : SchedWriteRes<[Ampere1BUnitAB, Ampere1BUnitA]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1BWrite_1cyc_1L : SchedWriteRes<[Ampere1BUnitL]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_1S : SchedWriteRes<[Ampere1BUnitS]> {
  let Latency = 1;
  let NumMicroOps = 1;
}

def Ampere1BWrite_1cyc_2S : SchedWriteRes<[Ampere1BUnitS, Ampere1BUnitS]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def Ampere1BWrite_2cyc_1Y : SchedWriteRes<[Ampere1BUnitY]> {
  let Latency = 2;
  let NumMicroOps = 1;
}

def Ampere1BWrite_2cyc_2AB : SchedWriteRes<[Ampere1BUnitAB, Ampere1BUnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1BWrite_2cyc_1B_1AB : SchedWriteRes<[Ampere1BUnitB, Ampere1BUnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1BWrite_2cyc_1B_1S : SchedWriteRes<[Ampere1BUnitB, Ampere1BUnitS]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1BWrite_2cyc_1B_1S_1AB : SchedWriteRes<[Ampere1BUnitB,
                                                  Ampere1BUnitS,
                                                  Ampere1BUnitAB]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1BWrite_2cyc_1S_2Z : SchedWriteRes<[Ampere1BUnitS,
                                              Ampere1BUnitZ,
                                              Ampere1BUnitZ]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

def Ampere1BWrite_2cyc_1XY : SchedWriteRes<[Ampere1BUnitXY]> {
  let Latency = 2;
  let NumMicroOps = 1;
}

def Ampere1BWrite_2cyc_1S_1Z : SchedWriteRes<[Ampere1BUnitS, Ampere1BUnitZ]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def Ampere1BWrite_3cyc_1BS : SchedWriteRes<[Ampere1BUnitBS]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1BWrite_3cyc_1L : SchedWriteRes<[Ampere1BUnitL]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1BWrite_3cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1BWrite_3cyc_1XY : SchedWriteRes<[Ampere1BUnitXY]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1BWrite_3cyc_1Z : SchedWriteRes<[Ampere1BUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 1;
}

def Ampere1BWrite_3cyc_1S_1Z : SchedWriteRes<[Ampere1BUnitS,
                                              Ampere1BUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

def Ampere1BWrite_3cyc_1S_2Z : SchedWriteRes<[Ampere1BUnitS,
                                              Ampere1BUnitZ, Ampere1BUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def Ampere1BWrite_3cyc_2S_2Z : SchedWriteRes<[Ampere1BUnitS, Ampere1BUnitS,
                                              Ampere1BUnitZ, Ampere1BUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 4;
}

def Ampere1BWrite_4cyc_1BS_1AB : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitAB]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1BWrite_4cyc_1L : SchedWriteRes<[Ampere1BUnitL]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1BWrite_4cyc_2L : SchedWriteRes<[Ampere1BUnitL, Ampere1BUnitL]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1BWrite_4cyc_1L_1B : SchedWriteRes<[Ampere1BUnitL, Ampere1BUnitB]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1BWrite_4cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1BWrite_4cyc_1XY : SchedWriteRes<[Ampere1BUnitXY]> {
  let Latency = 4;
  let NumMicroOps = 1;
}

def Ampere1BWrite_4cyc_2XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def Ampere1BWrite_5cyc_1BS : SchedWriteRes<[Ampere1BUnitBS]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1BWrite_4cyc_1XY_1S_1Z : SchedWriteRes<[Ampere1BUnitXY,
                                                  Ampere1BUnitS,
                                                  Ampere1BUnitZ]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

def Ampere1BWrite_4cyc_3S_3Z : SchedWriteRes<[Ampere1BUnitS, Ampere1BUnitS,
                                              Ampere1BUnitS, Ampere1BUnitZ,
                                              Ampere1BUnitZ, Ampere1BUnitZ]> {
  let Latency = 4;
  let NumMicroOps = 6;
}

def Ampere1BWrite_5cyc_4S_4Z : SchedWriteRes<[Ampere1BUnitS, Ampere1BUnitS,
                                              Ampere1BUnitS, Ampere1BUnitS,
                                              Ampere1BUnitZ, Ampere1BUnitZ,
                                              Ampere1BUnitZ, Ampere1BUnitZ]> {
  let Latency = 5;
  let NumMicroOps = 8;
}

def Ampere1BWrite_5cyc_1L_1BS : SchedWriteRes<[Ampere1BUnitL,
                                               Ampere1BUnitBS]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def Ampere1BWrite_5cyc_3L : SchedWriteRes<[Ampere1BUnitL,
                                           Ampere1BUnitL,
                                           Ampere1BUnitL]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

def Ampere1BWrite_5cyc_4L : SchedWriteRes<[Ampere1BUnitL,
                                           Ampere1BUnitL,
                                           Ampere1BUnitL,
                                           Ampere1BUnitL]> {
  let Latency = 5;
  let NumMicroOps = 4;
}

def Ampere1BWrite_5cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 5;
  let NumMicroOps = 1;
}

def Ampere1BWrite_5cyc_2XY_2S_2Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ]> {
  let Latency = 5;
  let NumMicroOps = 6;
}

def Ampere1BWrite_6cyc_1BS_1A : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitA]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1BWrite_6cyc_1BS_2A : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitA,
                                                               Ampere1BUnitA]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def Ampere1BWrite_6cyc_1L_1XY : SchedWriteRes<[Ampere1BUnitL, Ampere1BUnitXY]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1BWrite_6cyc_2L_2XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 6;
  let NumMicroOps = 4;
}

def Ampere1BWrite_6cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1BWrite_6cyc_2XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def Ampere1BWrite_6cyc_3XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                            Ampere1BUnitXY]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def Ampere1BWrite_6cyc_2XY_2S_2Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ]> {
  let Latency = 6;
  let NumMicroOps = 6;
}

def Ampere1BWrite_6cyc_3XY_3S_3Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitS,  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ,  Ampere1BUnitZ]> {
  let Latency = 6;
  let NumMicroOps = 9;
}

def Ampere1BWrite_7cyc_1BS_1XY : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitXY]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1BWrite_7cyc_1XY_1Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitZ]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1BWrite_7cyc_1X_1Z : SchedWriteRes<[Ampere1BUnitX, Ampere1BUnitZ]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def Ampere1BWrite_7cyc_3L_3XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitL,  Ampere1BUnitXY,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 7;
  let NumMicroOps = 6;
}

def Ampere1BWrite_7cyc_4L_4XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitXY, Ampere1BUnitXY,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 7;
  let NumMicroOps = 8;
}

def Ampere1BWrite_7cyc_4XY_4S_4Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ]> {
  let Latency = 7;
  let NumMicroOps = 12;
}

def Ampere1BWrite_8cyc_1BS_1L : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitL]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Ampere1BWrite_8cyc_1BS_1XY : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Ampere1BWrite_8cyc_2L_3XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitXY, Ampere1BUnitXY,
                                               Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 5;
}

def Ampere1BWrite_8cyc_3L_3XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitL,  Ampere1BUnitXY,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 6;
}

def Ampere1BWrite_8cyc_4L_4XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitXY, Ampere1BUnitXY,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 8;
}

def Ampere1BWrite_8cyc_2XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def Ampere1BWrite_8cyc_4XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                            Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

def Ampere1BWrite_9cyc_6XY_4S_4Z : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitXY, Ampere1BUnitXY,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitS,  Ampere1BUnitS,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ,
                                                  Ampere1BUnitZ,  Ampere1BUnitZ]> {
  let Latency = 9;
  let NumMicroOps = 14;
}

def Ampere1BWrite_9cyc_1A_1BS_1X : SchedWriteRes<[Ampere1BUnitA, Ampere1BUnitBS, Ampere1BUnitX]> {
  let Latency = 9;
  let NumMicroOps = 3;
}

def Ampere1BWrite_9cyc_1A_1BS_1XY : SchedWriteRes<[Ampere1BUnitA, Ampere1BUnitBS, Ampere1BUnitXY]> {
  let Latency = 9;
  let NumMicroOps = 3;
}

def Ampere1BWrite_9cyc_3L_3XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                               Ampere1BUnitL,  Ampere1BUnitXY,
                                               Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 9;
  let NumMicroOps = 6;
}

def Ampere1BWrite_9cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 9;
  let NumMicroOps = 1;
}

def Ampere1BWrite_9cyc_3XY : SchedWriteRes<[Ampere1BUnitXY, Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 9;
  let NumMicroOps = 3;
}

def Ampere1BWrite_10cyc_4L_8XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                                Ampere1BUnitL,  Ampere1BUnitL,
                                                Ampere1BUnitXY, Ampere1BUnitXY,
                                                Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 10;
  let NumMicroOps = 12;
}

def Ampere1BWrite_11cyc_1BS_2XY : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 11;
  let NumMicroOps = 3;
}

def Ampere1BWrite_11cyc_4L_8XY : SchedWriteRes<[Ampere1BUnitL,  Ampere1BUnitL,
                                                Ampere1BUnitL,  Ampere1BUnitL,
                                                Ampere1BUnitXY, Ampere1BUnitXY,
                                                Ampere1BUnitXY, Ampere1BUnitXY]> {
  let Latency = 11;
  let NumMicroOps = 12;
}

def Ampere1BWrite_12cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 12;
  let NumMicroOps = 1;
}

def Ampere1BWrite_13cyc_1BS_1X : SchedWriteRes<[Ampere1BUnitBS, Ampere1BUnitX]> {
  let Latency = 13;
  let NumMicroOps = 2;
}

def Ampere1BWrite_17cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 17;
  let NumMicroOps = 1;
}

def Ampere1BWrite_19cyc_2BS_1X : SchedWriteRes<[Ampere1BUnitBS,
                                                Ampere1BUnitBS,
                                                Ampere1BUnitX]> {
  let Latency = 13;
  let NumMicroOps = 3;
}

def Ampere1BWrite_19cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 19;
  let NumMicroOps = 1;
}

def Ampere1BWrite_21cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 21;
  let NumMicroOps = 1;
}

def Ampere1BWrite_33cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 33;
  let NumMicroOps = 1;
}

def Ampere1BWrite_39cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 39;
  let NumMicroOps = 1;
}

def Ampere1BWrite_63cyc_1X : SchedWriteRes<[Ampere1BUnitX]> {
  let Latency = 63;
  let NumMicroOps = 1;
}

// For basic arithmetic, we have more flexibility for short shifts (LSL shift <= 4),
// which are a single uop, and for extended registers, which have full flexibility
// across Unit A or B for both uops.
def Ampere1BWrite_Arith : SchedWriteVariant<[
                                SchedVar<RegExtendedPred, [Ampere1BWrite_2cyc_2AB]>,
                                SchedVar<IsCheapLSL,      [Ampere1BWrite_1cyc_1AB]>,
                                SchedVar<NoSchedPred,     [Ampere1BWrite_2cyc_1B_1AB]>]>;

def Ampere1BWrite_ArithFlagsetting : SchedWriteVariant<[
                                SchedVar<RegExtendedPred, [Ampere1BWrite_2cyc_2AB]>,
                                SchedVar<IsCheapLSL,      [Ampere1BWrite_1cyc_1AB]>,
                                SchedVar<NoSchedPred,     [Ampere1BWrite_2cyc_1B_1AB]>]>;

//===----------------------------------------------------------------------===//
// Map the target-defined scheduler read/write resources and latencies for Ampere-1.
// This provides a coarse model, which is then specialised below.

def : WriteRes<WriteImm,   [Ampere1BUnitAB]>;  // MOVN, MOVZ
def : WriteRes<WriteI,     [Ampere1BUnitAB]>;  // ALU
def : WriteRes<WriteISReg, [Ampere1BUnitB, Ampere1BUnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}  // ALU of Shifted-Reg
def : WriteRes<WriteIEReg, [Ampere1BUnitAB, Ampere1BUnitAB]> {
  let Latency = 2;
  let NumMicroOps = 2;
}  // ALU of Extended-Reg
def : WriteRes<WriteExtr,  [Ampere1BUnitB]>;  // EXTR shifts a reg pair
def : WriteRes<WriteIS,    [Ampere1BUnitB]>;  // Shift/Scale
def : WriteRes<WriteID32,  [Ampere1BUnitBS, Ampere1BUnitX]> {
  let Latency = 13;
}  // 32-bit Divide
def : WriteRes<WriteID64,  [Ampere1BUnitBS, Ampere1BUnitX]> {
  let Latency = 19;
}  // 64-bit Divide
def : WriteRes<WriteIM32,  [Ampere1BUnitBS]> {
  let Latency = 3;
}  // 32-bit Multiply
def : WriteRes<WriteIM64,  [Ampere1BUnitBS, Ampere1BUnitAB]> {
  let Latency = 3;
}  // 64-bit Multiply
def : WriteRes<WriteBr,    [Ampere1BUnitA]>;
def : WriteRes<WriteBrReg, [Ampere1BUnitA, Ampere1BUnitA]>;
def : WriteRes<WriteLD,    [Ampere1BUnitL]> {
  let Latency = 3;
}  // Load from base addr plus immediate offset
def : WriteRes<WriteST,    [Ampere1BUnitS]> {
  let Latency = 1;
}  // Store to base addr plus immediate offset
def : WriteRes<WriteSTP,   [Ampere1BUnitS, Ampere1BUnitS]> {
  let Latency = 1;
  let NumMicroOps = 1;
}  // Store a register pair.
def : WriteRes<WriteAdr,   [Ampere1BUnitAB]>;
def : WriteRes<WriteLDIdx, [Ampere1BUnitAB, Ampere1BUnitS]> {
  let Latency = 3;
  let NumMicroOps = 1;
}  // Load from a register index (maybe scaled).
def : WriteRes<WriteSTIdx, [Ampere1BUnitS, Ampere1BUnitS]> {
  let Latency = 1;
  let NumMicroOps = 2;
}  // Store to a register index (maybe scaled).
def : WriteRes<WriteF,  [Ampere1BUnitXY]> {
  let Latency = 2;
}  // General floating-point ops.
def : WriteRes<WriteFCmp,  [Ampere1BUnitX]> {
  let Latency = 3;
}  // Floating-point compare.
def : WriteRes<WriteFCvt,  [Ampere1BUnitXY]> {
  let Latency = 3;
}  // Float conversion.
def : WriteRes<WriteFCopy, [Ampere1BUnitXY]> {
}  // Float-int register copy.
def : WriteRes<WriteFImm,  [Ampere1BUnitXY]> {
  let Latency = 2;
}  // Float-int register copy.
def : WriteRes<WriteFMul,  [Ampere1BUnitXY]> {
  let Latency = 4;
}  // Floating-point multiply.
def : WriteRes<WriteFDiv,  [Ampere1BUnitXY]> {
  let Latency = 19;
}  // Floating-point division.
def : WriteRes<WriteVd,    [Ampere1BUnitXY]> {
  let Latency = 3;
}  // 64bit Vector D ops.
def : WriteRes<WriteVq,    [Ampere1BUnitXY]> {
  let Latency = 3;
}  // 128bit Vector Q ops.
def : WriteRes<WriteVLD,   [Ampere1BUnitL, Ampere1BUnitL]> {
  let Latency = 4;
}  // Vector loads.
def : WriteRes<WriteVST,   [Ampere1BUnitS, Ampere1BUnitZ]> {
  let Latency = 2;
}  // Vector stores.

def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteLDHi,    []> {
  let Latency = 3;
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
// Specialising the scheduling model further for Ampere-1B.

def : InstRW<[Ampere1BWrite_1cyc_1AB], (instrs COPY)>;

// Branch instructions
def : InstRW<[Ampere1BWrite_1cyc_1A], (instrs Bcc, BL, RET)>;
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instrs CBZW, CBZX, CBNZW, CBNZX, TBZW, TBZX, TBNZW, TBNZX)>;
def : InstRW<[Ampere1BWrite_1cyc_2A], (instrs BLR)>;

// Common Short Sequence Compression (CSSC)
def : InstRW<[Ampere1BWrite_1cyc_1AB], (instregex "^ABS[WX]")>;
def : InstRW<[Ampere1BWrite_3cyc_1BS], (instregex "^CNT[WX]")>;
def : InstRW<[Ampere1BWrite_1cyc_1B], (instregex "^CTZ[WX]")>;
def : InstRW<[Ampere1BWrite_1cyc_1AB_1A], (instregex "^[SU](MAX|MIN)[WX]")>;

// Cryptography instructions
// -- AES encryption/decryption
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^AES[DE]")>;
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^AESI?MC")>;
// -- Polynomial multiplication
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^PMUL", "^PMULL")>;
// -- SHA-256 hash
def : InstRW<[Ampere1BWrite_4cyc_1X], (instregex "^SHA256(H|H2)")>;
// -- SHA-256 schedule update
def : InstRW<[Ampere1BWrite_2cyc_1Y], (instregex "^SHA256SU[01]")>;
// -- SHA-3 instructions
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^BCAX", "^EOR3", "^RAX1", "^XAR")>;
// -- SHA-512 hash
def : InstRW<[Ampere1BWrite_4cyc_1X], (instregex "^SHA512(H|H2)")>;
// -- SHA-512 schedule update
def : InstRW<[Ampere1BWrite_2cyc_1Y], (instregex "^SHA512SU[01]")>;
// -- SHA1 choose/majority/parity
def : InstRW<[Ampere1BWrite_4cyc_1X], (instregex "^SHA1[CMP]")>;
// -- SHA1 hash/schedule update
def : InstRW<[Ampere1BWrite_2cyc_1Y], (instregex "^SHA1SU[01]")>;
def : InstRW<[Ampere1BWrite_2cyc_1Y], (instregex "^SHA1H")>;
// -- SM3 hash
def : InstRW<[Ampere1BWrite_2cyc_1XY],
    (instregex "^SM3PARTW[12]$", "^SM3SS1$", "^SM3TT[12][AB]$")>;
def : InstRW<[Ampere1BWrite_4cyc_1X], (instrs SM4E, SM4ENCKEY)>;

// FP and vector load instructions
// -- Load 1-element structure to one/all lanes
// ---- all lanes
def : InstRW<[Ampere1BWrite_6cyc_1L_1XY],
        (instregex "^LD1Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// ---- one lane
def : InstRW<[Ampere1BWrite_6cyc_1L_1XY],
        (instregex "^LD1i(8|16|32|64)")>;
// -- Load 1-element structure to one/all lanes, 1D size
def : InstRW<[Ampere1BWrite_4cyc_1L],
        (instregex "^LD1Rv1d")>;
// -- Load 1-element structures to 1 register
def : InstRW<[Ampere1BWrite_4cyc_1L],
        (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 2 registers
def : InstRW<[Ampere1BWrite_4cyc_2L],
        (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 3 registers
def : InstRW<[Ampere1BWrite_5cyc_3L],
        (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 1-element structures to 4 registers
def : InstRW<[Ampere1BWrite_5cyc_4L],
        (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Load 2-element structure to all lanes of 2 registers, 1D size
def : InstRW<[Ampere1BWrite_4cyc_2L],
        (instregex "^LD2Rv1d")>;
// -- Load 2-element structure to all lanes of 2 registers, other sizes
def : InstRW<[Ampere1BWrite_6cyc_2L_2XY],
        (instregex "^LD2Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 2-element structure to one lane of 2 registers
def : InstRW<[Ampere1BWrite_6cyc_2L_2XY],
        (instregex "^LD2i(8|16|32|64)")>;
// -- Load 2-element structures to 2 registers, 16B/8H/4S/2D size
def : InstRW<[Ampere1BWrite_6cyc_2L_2XY],
        (instregex "^LD2Twov(16b|8h|4s|2d)")>;
// -- Load 2-element structures to 2 registers, 8B/4H/2S size
def : InstRW<[Ampere1BWrite_8cyc_2L_3XY],
        (instregex "^LD2Twov(8b|4h|2s)")>;
// -- Load 3-element structure to all lanes of 3 registers, 1D size
def : InstRW<[Ampere1BWrite_5cyc_3L],
        (instregex "^LD3Rv1d")>;
// -- Load 3-element structure to all lanes of 3 registers, other sizes
def : InstRW<[Ampere1BWrite_7cyc_3L_3XY],
        (instregex "^LD3Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 3-element structure to one lane of 3 registers
def : InstRW<[Ampere1BWrite_7cyc_3L_3XY],
        (instregex "^LD3i(8|16|32|64)")>;
// -- Load 3-element structures to 3 registers, 16B/8H/4S sizes
def : InstRW<[Ampere1BWrite_8cyc_3L_3XY],
        (instregex "^LD3Threev(16b|8h|4s)")>;
// -- Load 3-element structures to 3 registers, 2D size
def : InstRW<[Ampere1BWrite_7cyc_3L_3XY],
        (instregex "^LD3Threev2d")>;
// -- Load 3-element structures to 3 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1BWrite_9cyc_3L_3XY],
        (instregex "^LD3Threev(8b|4h|2s)")>;
// -- Load 4-element structure to all lanes of 4 registers, 1D size
def : InstRW<[Ampere1BWrite_5cyc_4L],
        (instregex "^LD4Rv1d")>;
// -- Load 4-element structure to all lanes of 4 registers, other sizes
def : InstRW<[Ampere1BWrite_7cyc_4L_4XY],
        (instregex "^LD4Rv(8b|4h|2s|16b|8h|4s|2d)")>;
// -- Load 4-element structure to one lane of 4 registers
def : InstRW<[Ampere1BWrite_7cyc_4L_4XY],
        (instregex "^LD4i(8|16|32|64)")>;
// -- Load 4-element structures to 4 registers, 2D size
def : InstRW<[Ampere1BWrite_8cyc_4L_4XY],
        (instregex "^LD4Fourv2d")>;
// -- Load 4-element structures to 4 registers, 2S size
def : InstRW<[Ampere1BWrite_11cyc_4L_8XY],
        (instregex "^LD4Fourv2s")>;
// -- Load 4-element structures to 4 registers, other sizes
def : InstRW<[Ampere1BWrite_10cyc_4L_8XY],
        (instregex "^LD4Fourv(8b|4h|16b|8h|4s)")>;
// -- Load pair, Q-form
def : InstRW<[Ampere1BWrite_4cyc_2L], (instregex "LDN?PQ")>;
// -- Load pair, S/D-form
def : InstRW<[Ampere1BWrite_5cyc_1L_1BS], (instregex "LDN?P(S|D)")>;
// -- Load register
def : InstRW<[Ampere1BWrite_4cyc_1L], (instregex "LDU?R[BHSDQ]i")>;
// -- Load register, sign-extended register
def : InstRW<[Ampere1BWrite_4cyc_1L], (instregex "LDR[BHSDQ]ro(W|X)")>;

// FP and vector store instructions
// -- Store 1-element structure from one lane of 1 register
def : InstRW<[Ampere1BWrite_4cyc_1XY_1S_1Z],
        (instregex "^ST1i(8|16|32|64)")>;
// -- Store 1-element structures from 1 register
def : InstRW<[Ampere1BWrite_2cyc_1S_1Z],
        (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 2 registers
def : InstRW<[Ampere1BWrite_3cyc_2S_2Z],
        (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 3 registers
def : InstRW<[Ampere1BWrite_4cyc_3S_3Z],
        (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 1-element structures from 4 registers
def : InstRW<[Ampere1BWrite_5cyc_4S_4Z],
        (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 2-element structure from one lane of 2 registers
def : InstRW<[Ampere1BWrite_5cyc_2XY_2S_2Z],
        (instregex "^ST2i(8|16|32|64)")>;
// -- Store 2-element structures from 2 registers, 16B/8H/4S/2D sizes
def : InstRW<[Ampere1BWrite_5cyc_2XY_2S_2Z],
        (instregex "^ST2Twov(16b|8h|4s|2d)")>;
// -- Store 2-element structures from 2 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1BWrite_6cyc_2XY_2S_2Z],
        (instregex "^ST2Twov(8b|4h|2s)")>;
// -- Store 3-element structure from one lane of 3 registers
def : InstRW<[Ampere1BWrite_6cyc_3XY_3S_3Z],
        (instregex "^ST3i(8|16|32|64)")>;
// -- Store 3-element structures from 3 registers
def : InstRW<[Ampere1BWrite_6cyc_3XY_3S_3Z],
        (instregex "^ST3Threev(8b|4h|2s|1d|16b|8h|4s|2d)")>;
// -- Store 4-element structure from one lane of 4 registers
def : InstRW<[Ampere1BWrite_7cyc_4XY_4S_4Z],
        (instregex "^ST4i(8|16|32|64)")>;
// -- Store 4-element structures from 4 registers, 16B/8H/4S sizes
def : InstRW<[Ampere1BWrite_7cyc_4XY_4S_4Z],
        (instregex "^ST4Fourv(16b|8h|4s)")>;
// -- Store 4-element structures from 4 registers, 2D sizes
def : InstRW<[Ampere1BWrite_7cyc_4XY_4S_4Z],
        (instregex "^ST4Fourv2d")>;
// -- Store 4-element structures from 4 registers, 8B/4H/2S sizes
def : InstRW<[Ampere1BWrite_9cyc_6XY_4S_4Z],
        (instregex "^ST4Fourv(8b|4h|2s)")>;
// -- Store pair, Q-form
def : InstRW<[Ampere1BWrite_3cyc_2S_2Z], (instregex "^STN?PQ")>;
// -- Store pair, S/D-form
def : InstRW<[Ampere1BWrite_3cyc_2S_2Z], (instregex "^STN?P[SD]")>;
// -- Store register
def : InstRW<[Ampere1BWrite_2cyc_1S_1Z], (instregex "^STU?R[BHSDQ](ui|i)")>;
// -- Store register, sign-extended register offset
def : InstRW<[Ampere1BWrite_2cyc_1S_1Z], (instregex "^STR[BHSDQ]ro[XW]")>;

// FP data processing, bfloat16 format
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instrs BFCVT)>;
def : InstRW<[Ampere1BWrite_8cyc_2XY], (instrs BFCVTN, BFCVTN2)>;
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^BFDOTv", "^BF16DOT")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instrs BFMMLA)>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^BFMLAL")>;

// FP data processing, scalar/vector, half precision
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^F(ABD|ABS)v.[fi]16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(ADD|ADDP|CADD|NEG|NMUL|SUB)v.[fi]16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v.[fi]16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)16")>;
def : InstRW<[Ampere1BWrite_3cyc_1X],
        (instregex "^FCMPE?H")>;
def : InstRW<[Ampere1BWrite_9cyc_1A_1BS_1X],
        (instregex "^FCCMPE?H")>;
def : InstRW<[Ampere1BWrite_9cyc_1A_1BS_1XY],
        (instregex "^FCSELH")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVT[AMNPZ][SU]v.[if]16")>;
// Convert FP to integer, H-form
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^[SUd]CVTFv.[fi]16")>;
// Convert to FP from GPR, H-form
def : InstRW<[Ampere1BWrite_8cyc_1BS_1XY], (instregex "^[SU]CVTF_ZPmZ_[DSH]toH$")>;
// Convert to FP from GPR, fixed-point, H-form
def : InstRW<[Ampere1BWrite_11cyc_1BS_2XY], (instregex "^[SU]CVTF[SU][WX]Hri$")>;
def : InstRW<[Ampere1BWrite_9cyc_1X], (instrs FDIVHrr)>;
def : InstRW<[Ampere1BWrite_17cyc_1X], (instregex "^FDIVv.[if]16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^F(MAX|MIN)(NM)?P?v.[if]16")>;
def : InstRW<[Ampere1BWrite_6cyc_2XY], (instregex "^F(MAX|MIN)(NM)?Vv4[if]16")>;
def : InstRW<[Ampere1BWrite_9cyc_3XY], (instregex "^F(MAX|MIN)(NM)?Vv8[if]16")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FMULX?v.[if]16")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instrs FMULX16)>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FN?M(ADD|SUB)[H]rrr")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FML[AS]v.[if]16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FRECPXv.[if]16")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^F(RECP|RSQRT)S16")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FRINT[AIMNPXZ]v.[if]16")>;
// FP square root, H-form
def : InstRW<[Ampere1BWrite_21cyc_1X], (instrs FSQRTHr)>;
// FP square root, vector-form, F16
def : InstRW<[Ampere1BWrite_39cyc_1X], (instregex "^FSQRTv.f16")>;

// FP data processing, scalar/vector, single/double precision
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^F(ABD|ABS)v.[fi](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(ADD|ADDP|CADD|NEG|NMUL|SUB)v.[fi](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v.[fi](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)(32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1X],
        (instregex "^FCMPE?(S|D)")>;
def : InstRW<[Ampere1BWrite_9cyc_1A_1BS_1X],
        (instregex "^FCCMPE?(S|D)")>;
def : InstRW<[Ampere1BWrite_9cyc_1A_1BS_1XY],
        (instregex "^FCSEL(S|D)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVT[AMNPZ][SU]v.[if](32|64)")>;
// Convert FP to integer, S/D-form
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^[SUd]CVTFv.[fi](32|64)")>;
// Convert to FP from GPR, S/D-form
def : InstRW<[Ampere1BWrite_8cyc_1BS_1XY], (instregex "^[SU]CVTF_ZPmZ_[DSH]to[DS]$")>;
// Convert to FP from GPR, fixed-point, S/D-form
def : InstRW<[Ampere1BWrite_11cyc_1BS_2XY], (instregex "^[SU]CVTF[SU][WX][SD]ri$")>;
def : InstRW<[Ampere1BWrite_19cyc_1X], (instregex "^FDIVv.[if](64)", "FDIVD")>;
def : InstRW<[Ampere1BWrite_12cyc_1X], (instregex "^FDIVv.[if](32)", "FDIVS")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^F(MAX|MIN)(NM)?P?v.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_6cyc_2XY], (instregex "^F(MAX|MIN)(NM)?Vv.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FMULX?v.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instrs FMULX32, FMULX64)>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instrs FMULSrr, FNMULSrr)>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instrs FMULDrr, FNMULDrr)>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FN?M(ADD|SUB)[SD]rrr")>;
def : InstRW<[Ampere1BWrite_4cyc_1XY], (instregex "^FML[AS]v.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FRECPXv.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^F(RECP|RSQRT)S(32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FRINT[AIMNPXZ]v.[if](32|64)")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FRINT(32|64)")>;
def : InstRW<[Ampere1BWrite_63cyc_1X], (instregex "^FSQRTv.f64", "^FSQRTDr")>;
def : InstRW<[Ampere1BWrite_33cyc_1X], (instregex "^FSQRTv.f32", "^FSQRTSr")>;

// FP miscellaneous instructions
def : InstRW<[Ampere1BWrite_7cyc_1XY_1Z], (instregex "^FCVT[AMNPZ][SU][SU][XW][HSD]r")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVT[HSD]Hr")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVT[HSD][SD]r")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVTLv")>;
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^FCVT(N|XN)v")>;
def : InstRW<[Ampere1BWrite_7cyc_1X_1Z], (instrs FJCVTZS)>;
def : InstRW<[Ampere1BWrite_5cyc_1BS], (instregex "^FMOV[HSD][WX]r")>;
def : InstRW<[Ampere1BWrite_7cyc_1BS_1XY], (instregex "^FMOVDXHighr")>;
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^FMOV[HSD][ri]")>;
def : InstRW<[Ampere1BWrite_5cyc_1X], (instregex "^FMOVXDHighr")>;
def : InstRW<[Ampere1BWrite_3cyc_1Z], (instregex "^FMOV[WX][HSD]r")>;

// Integer arithmetic and logical instructions
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instregex "ADC(W|X)r", "SBC(W|X)r")>;
def : InstRW<[Ampere1BWrite_Arith],
        (instregex "(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)[WX]r[sx]")>;
def : InstRW<[Ampere1BWrite_1cyc_1AB],
        (instregex "(ADD|AND|BIC|EON|EOR|ORN|ORR|SUB)[WX]r[ri]")>;
def : InstRW<[Ampere1BWrite_ArithFlagsetting],
        (instregex "(ADD|AND|BIC|SUB)S[WX]r[sx]")>;
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instregex "(ADD|AND|BIC|SUB)S[WX]r[ri]")>;
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instregex "(ADC|SBC)S[WX]r")>;
def : InstRW<[Ampere1BWrite_1cyc_1A], (instrs RMIF)>;
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instregex "(CCMN|CCMP)(X|W)")>;
def : InstRW<[Ampere1BWrite_1cyc_1A],
        (instregex "(CSEL|CSINC|CSINV|CSNEG)(X|W)")>;
def : InstRW<[Ampere1BWrite_13cyc_1BS_1X], (instrs SDIVWr, UDIVWr)>;
def : InstRW<[Ampere1BWrite_19cyc_2BS_1X], (instrs SDIVXr, UDIVXr)>;
def : InstRW<[Ampere1BWrite_3cyc_1BS],
        (instregex "(S|U)MULHr")>;
def : InstRW<[Ampere1BWrite_4cyc_1BS_1AB],
        (instregex "(S|U)?M(ADD|SUB)L?r")>;

// Integer load instructions
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "(LDNP|LDP|LDPSW)(X|W)")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDR(B|D|H|Q|S)ui")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDR(D|Q|W|X)l")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDTR(B|H|W|X)i")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDTRS(BW|BX|HW|HX|W)i")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDUR(BB|HH|X|W)i")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDURS(BW|BX|HW|HX|W)i")>;
def : InstRW<[Ampere1BWrite_3cyc_1L],
        (instregex "LDR(HH|SHW|SHX|W|X)ro(W|X)")>;
def : InstRW<[Ampere1BWrite_1cyc_1L],
        (instrs PRFMl, PRFUMi, PRFUMi)>;
def : InstRW<[Ampere1BWrite_1cyc_1L],
        (instrs PRFMroW, PRFMroX)>;

// Integer miscellaneous instructions
def : InstRW<[Ampere1BWrite_1cyc_1A],  (instrs ADR, ADRP)>;
def : InstRW<[Ampere1BWrite_1cyc_1B],  (instregex "EXTR(W|X)")>;
def : InstRW<[Ampere1BWrite_1cyc_1B],  (instregex "(S|U)?BFM(W|X)")>;
def : InstRW<[Ampere1BWrite_3cyc_1BS], (instregex "^CRC32C?[BHWX]")>;
def : InstRW<[Ampere1BWrite_1cyc_1B],  (instregex "CLS(W|X)")>;
def : InstRW<[Ampere1BWrite_1cyc_1A],  (instrs SETF8, SETF16)>;
def : InstRW<[Ampere1BWrite_1cyc_1AB],
        (instrs MOVKWi, MOVKXi, MOVNWi, MOVNXi, MOVZWi, MOVZXi)>;
def : InstRW<[Ampere1BWrite_1cyc_1B],
        (instregex "(RBIT|REV|REV16)(W|X)r", "REV32Xr")>;
def : InstRW<[Ampere1BWrite_1cyc_1B],
        (instregex "(ASR|LSL|LSR|ROR)V(W|X)r")>;

// Integer store instructions
def : InstRW<[Ampere1BWrite_1cyc_2S],        (instregex "STNP(X|W)i")>;
def : InstRW<[Ampere1BWrite_1cyc_2S],        (instrs STPXi)>;
def : InstRW<[Ampere1BWrite_2cyc_1B_1S],     (instrs STPWi)>;
def : InstRW<[Ampere1BWrite_2cyc_1B_1S_1AB], (instregex "STP(W|X)(pre|post)")>;
def : InstRW<[Ampere1BWrite_1cyc_1S],        (instrs STTRBi, STTRHi, STTRWi, STTRXi)>;
def : InstRW<[Ampere1BWrite_1cyc_1S],        (instregex "STUR(BB|HH|X|W)i",
                                                        "STR(X|W)ui",
                                                        "STUR(BB|HH|X|W)i")>;
def : InstRW<[Ampere1BWrite_1cyc_2S],        (instrs STRWroX, STRXroX)>;
def : InstRW<[Ampere1BWrite_1cyc_2S],        (instrs STRWroW, STRXroW)>;

// Memory tagging

// Insert Random Tags
def : InstRW<[Ampere1BWrite_1cyc_1BS_1B], (instrs IRG, IRGstack)>;
// Load allocation tag
def : InstRW<[Ampere1BWrite_4cyc_1L_1B], (instrs LDG, LDGM)>;
// Store allocation tags
def : InstRW<[Ampere1BWrite_1cyc_1S],
    (instrs STGi, STGM, STGPreIndex, STGPostIndex)>;
// Store allocation tags and pair of registers
def : InstRW<[Ampere1BWrite_1cyc_2S],
    (instrs STGPi, STGPpre, STGPpost)>;
// Store allocation tags and zero data
def : InstRW<[Ampere1BWrite_1cyc_1S],
    (instrs STZGi, STZGM, STZGPreIndex, STZGPostIndex)>;
// Store two tags
def : InstRW<[Ampere1BWrite_1cyc_2S],
    (instrs ST2Gi, ST2GPreIndex, ST2GPostIndex)>;
// Store two tags and zero data
def : InstRW<[Ampere1BWrite_1cyc_2S],
    (instrs STZ2Gi, STZ2GPreIndex, STZ2GPostIndex)>;
// Subtract Pointer
def : InstRW<[Ampere1BWrite_1cyc_1AB], (instrs SUBP)>;
// Subtract Pointer, flagset
def : InstRW<[Ampere1BWrite_1cyc_1AB], (instrs SUBPS)>;
// Insert Tag Mask
def : InstRW<[Ampere1BWrite_1cyc_1AB], (instrs GMI)>;
// Arithmetic, immediate to logical address tag
def : InstRW<[Ampere1BWrite_1cyc_1B], (instrs ADDG, SUBG)>;

// Pointer authentication
def : InstRW<[Ampere1BWrite_5cyc_1BS], (instregex "^AUT")>;
def : InstRW<[Ampere1BWrite_6cyc_1BS_1A],
        (instregex "BRA(A|AZ|B|BZ)", "RETA(A|B)", "ERETA(A|B)")>;
def : InstRW<[Ampere1BWrite_6cyc_1BS_2A],
        (instrs BLRAA, BLRAAZ, BLRAB, BLRABZ)>;
def : InstRW<[Ampere1BWrite_5cyc_1BS], (instregex "^PAC")>;
def : InstRW<[Ampere1BWrite_8cyc_1BS_1L], (instregex "^LDRA(A|B)")>;
def : InstRW<[Ampere1BWrite_1cyc_1B], (instrs XPACD, XPACI)>;

// Vector integer instructions
// -- absolute difference
def : InstRW<[Ampere1BWrite_2cyc_1XY],
             (instregex "^SABAv", "^SABALv", "^SABDv", "^SABDLv",
                        "^UABAv", "^UABALv", "^UABDv", "^UABDLv")>;
// -- arithmetic
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^ABSv", "^(ADD|SUB)v", "^SADDLv", "^SADDW", "SHADD",
                   "SHSUB", "^SRHADD", "^URHADD", "SSUBL", "SSUBW",
                   "^UADDLv", "^UADDW", "UHADD", "UHSUB", "USUBL", "USUBW")>;
// -- arithmetic, horizontal, 16B
def : InstRW<[Ampere1BWrite_8cyc_4XY],
            (instregex "^ADDVv16i8v", "^SADDLVv16i8v", "^UADDLVv16i8v")>;
def : InstRW<[Ampere1BWrite_8cyc_4XY],
            (instregex "^[SU](MIN|MAX)Vv16i8v")>;
// -- arithmetic, horizontal, 4H/4S
def : InstRW<[Ampere1BWrite_4cyc_2XY],
            (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v")>;
def : InstRW<[Ampere1BWrite_4cyc_2XY],
            (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v")>;
// -- arithmetic, horizontal, 8B/8H
def : InstRW<[Ampere1BWrite_6cyc_3XY],
            (instregex "^[SU]?ADDL?V(v8i16|v4i32)v")>;
def : InstRW<[Ampere1BWrite_6cyc_3XY],
            (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v")>;
// -- arithmetic, narrowing
def : InstRW<[Ampere1BWrite_6cyc_2XY], (instregex "(ADD|SUB)HNv.*")>;
def : InstRW<[Ampere1BWrite_6cyc_2XY], (instregex "(RADD|RSUB)HNv.*")>;
// -- arithmetic, pairwise
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^ADDPv", "^SADALP", "^UADALP", "^SADDLPv", "^UADDLPv")>;
// -- arithmetic, saturating
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^SQADD", "^SQSUB", "^SUQADD", "^UQADD", "^UQSUB", "^USQADD")>;
// -- bit count
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^(CLS|CLZ|CNT)v")>;
// -- compare
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^CMEQv", "^CMGEv", "^CMGTv", "^CMLEv", "^CMLTv",
                   "^CMHIv", "^CMHSv")>;
// -- compare non-zero
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^CMTSTv")>;
// -- dot product
def : InstRW<[Ampere1BWrite_3cyc_1XY], (instregex "^(S|SU|U|US)DOTv")>;
// -- fp reciprocal estimate
def : InstRW<[Ampere1BWrite_6cyc_1X], (instregex "^FRECPEv", "^FRSQRTEv")>;
// -- integer reciprocal estimate
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^URECPEv", "^URSQRTEv")>;
// -- logical
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^ANDv", "^BICv", "^EORv", "^ORRv", "^ORNv", "^NOTv")>;
// -- logical, narrowing
def : InstRW<[Ampere1BWrite_6cyc_2XY],
        (instregex "RSHRNv",
                   "SHRNv", "SQSHRNv", "SQSHRUNv",
                   "UQXTNv")>;
// -- matrix multiply
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instrs SMMLA, UMMLA, USMMLA)>;
// -- max/min
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^SMAXv", "^SMINv", "^UMAXv", "^UMINv")>;
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^SMAXPv", "^SMINPv", "^UMAXPv", "^UMINPv")>;
// -- move immediate
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^MOVIv", "^MVNIv")>;
// -- multiply
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "MULv", "SMULLv", "UMULLv", "SQDMUL(H|L)v", "SQRDMULHv")>;
// -- multiply accumulate
def : InstRW<[Ampere1BWrite_3cyc_1XY],
        (instregex "MLAv", "MLSv", "(S|U|SQD)(MLAL|MLSL)v", "SQRDML(A|S)Hv")>;
// -- negation, saturating
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^SQABS", "^SQNEG")>;
// -- reverse bits/bytes
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^RBITv", "^REV16v", "^REV32v", "^REV64v")>;
// -- shift
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// -- shift and accumulate
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "SRSRAv", "SSRAv", "URSRAv", "USRAv")>;
// -- shift, saturating
def : InstRW<[Ampere1BWrite_2cyc_1XY],
        (instregex "^SQRSHLv", "^SQRSHRNv", "^SQRSHRUNv", "^SQSHL", "^SQSHLU",
                   "^SQXTNv", "^SQXTUNv", "^UQSHRNv", "UQRSHRNv", "^UQRSHL",
                   "^UQSHL")>;

// Vector miscellaneous instructions
// -- duplicate element
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^DUPv.+lane")>;
// -- duplicate from GPR
def : InstRW<[Ampere1BWrite_5cyc_1BS], (instregex "^DUPv.+gpr")>;
// -- extract narrow
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^XTNv")>;
// -- insert/extract element
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^EXTv", "^INSv.+lane")>;
// -- move FP immediate
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^FMOVv")>;
// -- move element to GPR
def : InstRW<[Ampere1BWrite_5cyc_1X], (instregex "(S|U)MOVv")>;
// -- move from GPR to any element
def : InstRW<[Ampere1BWrite_7cyc_1BS_1XY], (instregex "^INSv.+gpr")>;
// -- table lookup
def : InstRW<[Ampere1BWrite_2cyc_1XY],
            (instrs TBLv8i8One, TBLv16i8One, TBXv8i8One, TBXv16i8One)>;
def : InstRW<[Ampere1BWrite_4cyc_2XY],
            (instrs TBLv8i8Two, TBLv16i8Two, TBXv8i8Two, TBXv16i8Two)>;
def : InstRW<[Ampere1BWrite_6cyc_3XY],
            (instrs TBLv8i8Three, TBLv16i8Three, TBXv8i8Three, TBXv16i8Three)>;
def : InstRW<[Ampere1BWrite_8cyc_4XY],
            (instrs TBLv8i8Four, TBLv16i8Four, TBXv8i8Four, TBXv16i8Four)>;
// -- transpose
def : InstRW<[Ampere1BWrite_2cyc_1XY],
              (instregex "^TRN1v", "^TRN2v", "^UZP1v", "^UZP2v")>;
// -- zip/unzip
def : InstRW<[Ampere1BWrite_2cyc_1XY], (instregex "^ZIP1v", "^ZIP2v")>;

} // SchedModel = Ampere1BModel
