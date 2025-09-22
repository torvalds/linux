//=- AArch64SchedExynosM5.td - Samsung Exynos M5 Sched Defs --*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Samsung Exynos M5 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The Exynos-M5 is an advanced superscalar microprocessor with a 6-wide
// in-order stage for decode and dispatch and a wider issue stage.
// The execution units and loads and stores are out-of-order.

def ExynosM5Model : SchedMachineModel {
  let IssueWidth            =   6; // Up to 6 uops per cycle.
  let MicroOpBufferSize     = 228; // ROB size.
  let LoopMicroOpBufferSize =  60; // Based on the instruction queue size.
  let LoadLatency           =   4; // Optimistic load cases.
  let MispredictPenalty     =  15; // Minimum branch misprediction penalty.
  let CompleteModel         =   1; // Use the default model otherwise.

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on the Exynos-M5.

let SchedModel = ExynosM5Model in {

def M5UnitA  : ProcResource<2>; // Simple integer
def M5UnitC  : ProcResource<2>; // Simple and complex integer
let Super =  M5UnitC, BufferSize = 1 in
def M5UnitD  : ProcResource<1>; // Integer division (inside C0, serialized)
def M5UnitE  : ProcResource<2>; // Simple 32-bit integer
let Super =  M5UnitC in
def M5UnitF  : ProcResource<2>; // CRC (inside C)
def M5UnitB  : ProcResource<1>; // Branch
def M5UnitL0 : ProcResource<1>; // Load
def M5UnitS0 : ProcResource<1>; // Store
def M5PipeLS : ProcResource<1>; // Load/Store
let Super = M5PipeLS in {
  def M5UnitL1 : ProcResource<1>;
  def M5UnitS1 : ProcResource<1>;
}
def M5PipeF0 : ProcResource<1>; // FP #0
let Super = M5PipeF0 in {
  def M5UnitFMAC0 : ProcResource<1>; // FP multiplication
  def M5UnitFADD0 : ProcResource<1>; // Simple FP
  def M5UnitNALU0 : ProcResource<1>; // Simple vector
  def M5UnitNDOT0 : ProcResource<1>; // Dot product vector
  def M5UnitNHAD  : ProcResource<1>; // Horizontal vector
  def M5UnitNMSC  : ProcResource<1>; // FP and vector miscellanea
  def M5UnitNMUL0 : ProcResource<1>; // Vector multiplication
  def M5UnitNSHT0 : ProcResource<1>; // Vector shifting
  def M5UnitNSHF0 : ProcResource<1>; // Vector shuffling
  def M5UnitNCRY0 : ProcResource<1>; // Cryptographic
}
def M5PipeF1 : ProcResource<1>; // FP #1
let Super = M5PipeF1 in {
  def M5UnitFMAC1 : ProcResource<1>; // FP multiplication
  def M5UnitFADD1 : ProcResource<1>; // Simple FP
  def M5UnitFCVT0 : ProcResource<1>; // FP conversion
  def M5UnitFDIV0 : ProcResource<2>; // FP division (serialized)
  def M5UnitFSQR0 : ProcResource<2>; // FP square root (serialized)
  def M5UnitFST0  : ProcResource<1>; // FP store
  def M5UnitNALU1 : ProcResource<1>; // Simple vector
  def M5UnitNDOT1 : ProcResource<1>; // Dot product vector
  def M5UnitNSHT1 : ProcResource<1>; // Vector shifting
  def M5UnitNSHF1 : ProcResource<1>; // Vector shuffling
}
def M5PipeF2 : ProcResource<1>; // FP #2
let Super = M5PipeF2 in {
  def M5UnitFMAC2 : ProcResource<1>; // FP multiplication
  def M5UnitFADD2 : ProcResource<1>; // Simple FP
  def M5UnitFCVT1 : ProcResource<1>; // FP conversion
  def M5UnitFDIV1 : ProcResource<2>; // FP division (serialized)
  def M5UnitFSQR1 : ProcResource<2>; // FP square root (serialized)
  def M5UnitFST1  : ProcResource<1>; // FP store
  def M5UnitNALU2 : ProcResource<1>; // Simple vector
  def M5UnitNDOT2 : ProcResource<1>; // Dot product vector
  def M5UnitNMUL1 : ProcResource<1>; // Vector multiplication
  def M5UnitNSHT2 : ProcResource<1>; // Vector shifting
  def M5UnitNCRY1 : ProcResource<1>; // Cryptographic
}

def M5UnitAX    : ProcResGroup<[M5UnitA,
                                M5UnitC]>;
def M5UnitAW    : ProcResGroup<[M5UnitA,
                                M5UnitC,
                                M5UnitE]>;
def M5UnitL     : ProcResGroup<[M5UnitL0,
                                M5UnitL1]>;
def M5UnitS     : ProcResGroup<[M5UnitS0,
                                M5UnitS1]>;
def M5UnitFMAC  : ProcResGroup<[M5UnitFMAC0,
                                M5UnitFMAC1,
                                M5UnitFMAC2]>;
def M5UnitFADD  : ProcResGroup<[M5UnitFADD0,
                                M5UnitFADD1,
                                M5UnitFADD2]>;
def M5UnitFCVT  : ProcResGroup<[M5UnitFCVT0,
                                M5UnitFCVT1]>;
def M5UnitFDIV  : ProcResGroup<[M5UnitFDIV0,
                                M5UnitFDIV1]>;
def M5UnitFSQR  : ProcResGroup<[M5UnitFSQR0,
                                M5UnitFSQR1]>;
def M5UnitFST   : ProcResGroup<[M5UnitFST0,
                                M5UnitFST1]>;
def M5UnitNALU  : ProcResGroup<[M5UnitNALU0,
                                M5UnitNALU1,
                                M5UnitNALU2]>;
def M5UnitNDOT  : ProcResGroup<[M5UnitNDOT0,
                                M5UnitNDOT1,
                                M5UnitNDOT2]>;
def M5UnitNMUL  : ProcResGroup<[M5UnitNMUL0,
                                M5UnitNMUL1]>;
def M5UnitNSHT  : ProcResGroup<[M5UnitNSHT0,
                                M5UnitNSHT1,
                                M5UnitNSHT2]>;
def M5UnitNSHF  : ProcResGroup<[M5UnitNSHF0,
                                M5UnitNSHF1]>;
def M5UnitNCRY  : ProcResGroup<[M5UnitNCRY0,
                                M5UnitNCRY1]>;

//===----------------------------------------------------------------------===//
// Resources details.

def M5WriteZ0 : SchedWriteRes<[]> { let Latency = 0; }
def M5WriteZ1 : SchedWriteRes<[]> { let Latency = 1;
                                    let NumMicroOps = 0; }
def M5WriteZ4 : SchedWriteRes<[]> { let Latency = 4;
                                    let NumMicroOps = 0; }

def M5WriteA1W : SchedWriteRes<[M5UnitAW]> { let Latency = 1; }
def M5WriteA1X : SchedWriteRes<[M5UnitAX]> { let Latency = 1; }
def M5WriteAAW : SchedWriteRes<[M5UnitAW]> { let Latency = 2;
                                             let ReleaseAtCycles = [2]; }
def M5WriteAAX : SchedWriteRes<[M5UnitAX]> { let Latency = 2;
                                             let ReleaseAtCycles = [2]; }
def M5WriteAB  : SchedWriteRes<[M5UnitAX,
                                M5UnitC,
                                M5UnitE]>  { let Latency = 2;
                                             let NumMicroOps = 2; }
def M5WriteAC  : SchedWriteRes<[M5UnitAX,
                                M5UnitAX,
                                M5UnitC]>  { let Latency = 3;
                                             let NumMicroOps = 3; }
def M5WriteAD  : SchedWriteRes<[M5UnitAW,
                                M5UnitC]>  { let Latency = 2;
                                              let NumMicroOps = 2; }
def M5WriteAFW : SchedWriteRes<[M5UnitAW]> { let Latency = 2;
                                             let NumMicroOps = 2; }
def M5WriteAFX : SchedWriteRes<[M5UnitAX]> { let Latency = 2;
                                             let NumMicroOps = 2; }
def M5WriteAUW : SchedWriteVariant<[SchedVar<IsCopyIdiomPred,   [M5WriteZ0]>,
                                    SchedVar<ExynosArithPred,   [M5WriteA1W]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1W]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAW]>]>;
def M5WriteAUX : SchedWriteVariant<[SchedVar<IsCopyIdiomPred,   [M5WriteZ0]>,
                                    SchedVar<ExynosArithPred,   [M5WriteA1X]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1X]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAX]>]>;
def M5WriteAVW : SchedWriteVariant<[SchedVar<ExynosResetPred,   [M5WriteZ0]>,
                                    SchedVar<ExynosArithPred,   [M5WriteA1W]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1W]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAW]>]>;
def M5WriteAVX : SchedWriteVariant<[SchedVar<ExynosResetPred,   [M5WriteZ0]>,
                                    SchedVar<ExynosArithPred,   [M5WriteA1X]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1X]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAX]>]>;
def M5WriteAXW : SchedWriteVariant<[SchedVar<ExynosArithPred,   [M5WriteA1W]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1W]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAW]>]>;
def M5WriteAXX : SchedWriteVariant<[SchedVar<ExynosArithPred,   [M5WriteA1X]>,
                                    SchedVar<ExynosLogicExPred, [M5WriteA1X]>,
                                    SchedVar<NoSchedPred,       [M5WriteAAX]>]>;
def M5WriteAYW : SchedWriteVariant<[SchedVar<IsRORImmIdiomPred, [M5WriteA1W]>,
                                    SchedVar<NoSchedPred,       [M5WriteAFW]>]>;
def M5WriteAYX : SchedWriteVariant<[SchedVar<IsRORImmIdiomPred, [M5WriteA1X]>,
                                    SchedVar<NoSchedPred,       [M5WriteAFX]>]>;

def M5WriteB1 : SchedWriteRes<[M5UnitB]> { let Latency = 1; }
def M5WriteBX : SchedWriteVariant<[SchedVar<ExynosBranchLinkLRPred, [M5WriteAC]>,
                                   SchedVar<NoSchedPred,            [M5WriteAB]>]>;

def M5WriteC1 : SchedWriteRes<[M5UnitC]> { let Latency = 1; }
def M5WriteC2 : SchedWriteRes<[M5UnitC]> { let Latency = 2; }
def M5WriteCA : SchedWriteRes<[M5UnitC]> { let Latency = 3;
                                           let ReleaseAtCycles = [2]; }

def M5WriteD10 : SchedWriteRes<[M5UnitD]> { let Latency = 10;
                                            let ReleaseAtCycles = [10]; }
def M5WriteD16 : SchedWriteRes<[M5UnitD]> { let Latency = 16;
                                            let ReleaseAtCycles = [16]; }

def M5WriteF2 : SchedWriteRes<[M5UnitF]> { let Latency = 2; }

def M5WriteL4 : SchedWriteRes<[M5UnitL]> { let Latency = 4; }
def M5WriteL5 : SchedWriteRes<[M5UnitL]> { let Latency = 5; }
def M5WriteL6 : SchedWriteRes<[M5UnitL]> { let Latency = 6; }
def M5WriteLA : SchedWriteRes<[M5UnitL,
                               M5UnitL]> { let Latency = 6;
                                           let NumMicroOps = 1; }
def M5WriteLB : SchedWriteRes<[M5UnitAX,
                               M5UnitL]> { let Latency = 6;
                                           let NumMicroOps = 2; }
def M5WriteLC : SchedWriteRes<[M5UnitAX,
                               M5UnitL,
                               M5UnitL]> { let Latency = 6;
                                           let NumMicroOps = 2; }
def M5WriteLD : SchedWriteRes<[M5UnitAX,
                               M5UnitL]> { let Latency = 4;
                                           let NumMicroOps = 2; }
def M5WriteLE : SchedWriteRes<[M5UnitAX,
                               M5UnitL]> { let Latency = 7;
                                           let NumMicroOps = 2; }
def M5WriteLFW : SchedWriteRes<[M5UnitAW,
                                M5UnitAW,
                                M5UnitAW,
                                M5UnitAW,
                                M5UnitL]>  { let Latency = 15;
                                             let NumMicroOps = 6;
                                             let ReleaseAtCycles = [1, 1, 1, 1, 15]; }
def M5WriteLFX : SchedWriteRes<[M5UnitAX,
                                M5UnitAX,
                                M5UnitAX,
                                M5UnitAX,
                                M5UnitL]>  { let Latency = 15;
                                             let NumMicroOps = 6;
                                             let ReleaseAtCycles = [1, 1, 1, 1, 15]; }
def M5WriteLGW : SchedWriteRes<[M5UnitAW,
                                M5UnitL]>  { let Latency = 13;
                                             let NumMicroOps = 1;
                                             let ReleaseAtCycles = [1, 13]; }
def M5WriteLGX : SchedWriteRes<[M5UnitAX,
                                M5UnitL]>  { let Latency = 13;
                                             let NumMicroOps = 1;
                                             let ReleaseAtCycles = [1, 13]; }
def M5WriteLH : SchedWriteRes<[]>        { let Latency = 6;
                                           let NumMicroOps = 0; }
def M5WriteLX : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M5WriteL5]>,
                                   SchedVar<NoSchedPred,         [M5WriteL4]>]>;
def M5WriteLY : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M5WriteLE]>,
                                   SchedVar<NoSchedPred,         [M5WriteL6]>]>;

def M5WriteS1  : SchedWriteRes<[M5UnitS]>  { let Latency = 1; }
def M5WriteSA  : SchedWriteRes<[M5UnitS0]> { let Latency = 4; }
def M5WriteSB  : SchedWriteRes<[M5UnitAX,
                                M5UnitS]>  { let Latency = 2;
                                             let NumMicroOps = 1; }
def M5WriteSX  : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M5WriteSB]>,
                                    SchedVar<NoSchedPred,         [M5WriteS1]>]>;

def M5ReadAdrBase : SchedReadVariant<[SchedVar<
                                        MCSchedPredicate<
                                          CheckAny<
                                            [ScaledIdxFn,
                                             ExynosScaledIdxFn]>>, [ReadDefault]>,
                                      SchedVar<NoSchedPred,        [ReadDefault]>]>;

def M5WriteNEONB   : SchedWriteRes<[M5UnitNALU,
                                    M5UnitS0]>    { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M5WriteNEONH   : SchedWriteRes<[M5UnitNALU,
                                    M5UnitS0]>    { let Latency = 2;
                                                    let NumMicroOps = 2; }
def M5WriteNEONI   : SchedWriteRes<[M5UnitS0,
                                    M5UnitNSHF]>  { let Latency = 6;
                                                    let NumMicroOps = 2; }
def M5WriteNEONK   : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitFCVT0,
                                    M5UnitS0]>    { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M5WriteNEONN   : SchedWriteRes<[M5UnitNMSC,
                                    M5UnitNMSC]>  { let Latency = 5;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [7, 7]; }
def M5WriteNEONO   : SchedWriteRes<[M5UnitNMSC,
                                    M5UnitNMSC,
                                    M5UnitNMSC]>  { let Latency = 8;
                                                    let NumMicroOps = 3;
                                                    let ReleaseAtCycles = [10, 10, 10]; }
def M5WriteNEONP   : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitS0,
                                    M5UnitFCVT]>  { let Latency = 7;
                                                    let NumMicroOps = 2; }
def M5WriteNEONQ   : SchedWriteRes<[M5UnitNMSC,
                                    M5UnitC]>     { let Latency = 3;
                                                    let NumMicroOps = 1; }
def M5WriteNEONU   : SchedWriteRes<[M5UnitFSQR,
                                    M5UnitFSQR]>  { let Latency = 7;
                                                    let ReleaseAtCycles = [4, 4]; }
def M5WriteNEONV   : SchedWriteRes<[M5UnitFDIV,
                                    M5UnitFDIV]>  { let Latency = 7;
                                                    let ReleaseAtCycles = [6, 6]; }
def M5WriteNEONW   : SchedWriteRes<[M5UnitFDIV,
                                    M5UnitFDIV]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [9, 9]; }
def M5WriteNEONX   : SchedWriteRes<[M5UnitFSQR,
                                    M5UnitFSQR]>  { let Latency = 8;
                                                    let ReleaseAtCycles = [5, 5]; }
def M5WriteNEONY   : SchedWriteRes<[M5UnitFSQR,
                                    M5UnitFSQR]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [9, 9]; }
def M5WriteNEONZ   : SchedWriteVariant<[SchedVar<ExynosQFormPred, [M5WriteNEONO]>,
                                        SchedVar<NoSchedPred,     [M5WriteNEONN]>]>;

def M5WriteFADD2   : SchedWriteRes<[M5UnitFADD]>  { let Latency = 2; }

def M5WriteFCVT2   : SchedWriteRes<[M5UnitFCVT]>  { let Latency = 2; }
def M5WriteFCVT2A  : SchedWriteRes<[M5UnitFCVT0]> { let Latency = 2; }
def M5WriteFCVT3   : SchedWriteRes<[M5UnitFCVT]>  { let Latency = 3; }
def M5WriteFCVT3A  : SchedWriteRes<[M5UnitFCVT0]> { let Latency = 3; }
def M5WriteFCVTA   : SchedWriteRes<[M5UnitFCVT0,
                                    M5UnitS0]>    { let Latency = 3;
                                                    let NumMicroOps = 1; }
def M5WriteFCVTB   : SchedWriteRes<[M5UnitFCVT,
                                    M5UnitS0]>    { let Latency = 4;
                                                    let NumMicroOps = 1; }
def M5WriteFCVTC   : SchedWriteRes<[M5UnitFCVT,
                                    M5UnitS0]>    { let Latency = 6;
                                                    let NumMicroOps = 1; }

def M5WriteFDIV5   : SchedWriteRes<[M5UnitFDIV]>  { let Latency = 5;
                                                    let ReleaseAtCycles = [2]; }
def M5WriteFDIV7   : SchedWriteRes<[M5UnitFDIV]>  { let Latency = 7;
                                                    let ReleaseAtCycles = [4]; }
def M5WriteFDIV12  : SchedWriteRes<[M5UnitFDIV]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [9]; }

def M5WriteFMAC3   : SchedWriteRes<[M5UnitFMAC]>  { let Latency = 3; }
def M5WriteFMAC4   : SchedWriteRes<[M5UnitFMAC]>  { let Latency = 4; }

def M5WriteFSQR5   : SchedWriteRes<[M5UnitFSQR]>  { let Latency = 5;
                                                    let ReleaseAtCycles = [2]; }
def M5WriteFSQR7   : SchedWriteRes<[M5UnitFSQR]>  { let Latency = 7;
                                                    let ReleaseAtCycles = [4]; }
def M5WriteFSQR8   : SchedWriteRes<[M5UnitFSQR]>  { let Latency = 8;
                                                    let ReleaseAtCycles = [5]; }
def M5WriteFSQR12  : SchedWriteRes<[M5UnitFSQR]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [9]; }

def M5WriteNALU1   : SchedWriteRes<[M5UnitNALU]>  { let Latency = 1; }
def M5WriteNALU2   : SchedWriteRes<[M5UnitNALU]>  { let Latency = 2; }

def M5WriteNDOT2   : SchedWriteRes<[M5UnitNDOT]>  { let Latency = 2; }

def M5WriteNCRY2   : SchedWriteRes<[M5UnitNCRY]>  { let Latency = 2; }
def M5WriteNCRY1A  : SchedWriteRes<[M5UnitNCRY0]> { let Latency = 1; }
def M5WriteNCRY2A  : SchedWriteRes<[M5UnitNCRY0]> { let Latency = 2; }
def M5WriteNCRY3A  : SchedWriteRes<[M5UnitNCRY0]> { let Latency = 3; }
def M5WriteNCRY5A  : SchedWriteRes<[M5UnitNCRY]>  { let Latency = 5; }

def M5WriteNHAD1   : SchedWriteRes<[M5UnitNHAD]>  { let Latency = 1; }
def M5WriteNHAD3   : SchedWriteRes<[M5UnitNHAD]>  { let Latency = 3; }

def M5WriteNMSC1   : SchedWriteRes<[M5UnitNMSC]>  { let Latency = 1; }
def M5WriteNMSC2   : SchedWriteRes<[M5UnitNMSC]>  { let Latency = 2; }

def M5WriteNMUL3   : SchedWriteRes<[M5UnitNMUL]>  { let Latency = 3; }

def M5WriteNSHF1   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 1; }
def M5WriteNSHF2   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 2; }
def M5WriteNSHFA   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 2; }
def M5WriteNSHFB   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 4;
                                                    let NumMicroOps = 2; }
def M5WriteNSHFC   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 6;
                                                    let NumMicroOps = 3; }
def M5WriteNSHFD   : SchedWriteRes<[M5UnitNSHF]>  { let Latency = 8;
                                                    let NumMicroOps = 4; }

def M5WriteNSHT2   : SchedWriteRes<[M5UnitNSHT]>  { let Latency = 2; }
def M5WriteNSHT4A  : SchedWriteRes<[M5UnitNSHT1]> { let Latency = 4; }

def M5WriteVLDA    : SchedWriteRes<[M5UnitL,
                                    M5UnitL]>     { let Latency = 6;
                                                    let NumMicroOps = 2; }
def M5WriteVLDB    : SchedWriteRes<[M5UnitL,
                                    M5UnitL,
                                    M5UnitL]>     { let Latency = 7;
                                                    let NumMicroOps = 3; }
def M5WriteVLDC    : SchedWriteRes<[M5UnitL,
                                    M5UnitL,
                                    M5UnitL,
                                    M5UnitL]>     { let Latency = 7;
                                                    let NumMicroOps = 4; }
def M5WriteVLDD    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [2, 1]; }
def M5WriteVLDF    : SchedWriteRes<[M5UnitL,
                                    M5UnitL]>     { let Latency = 11;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [6, 5]; }
def M5WriteVLDG    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 3;
                                                    let ReleaseAtCycles = [2, 1, 1]; }
def M5WriteVLDI    : SchedWriteRes<[M5UnitL,
                                    M5UnitL,
                                    M5UnitL]>     { let Latency = 13;
                                                    let NumMicroOps = 3; }
def M5WriteVLDJ    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF]>  { let Latency = 8;
                                                    let NumMicroOps = 4; }
def M5WriteVLDK    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF]>  { let Latency = 8;
                                                    let NumMicroOps = 5; }
def M5WriteVLDL    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitL,
                                    M5UnitNSHF]>  { let Latency = 8;
                                                    let NumMicroOps = 5; }
def M5WriteVLDM    : SchedWriteRes<[M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitL,
                                    M5UnitNSHF,
                                    M5UnitNSHF]>  { let Latency = 8;
                                                    let NumMicroOps = 6; }
def M5WriteVLDN    : SchedWriteRes<[M5UnitL,
                                    M5UnitL,
                                    M5UnitL,
                                    M5UnitL]>     { let Latency = 15;
                                                    let NumMicroOps = 4;
                                                    let ReleaseAtCycles = [2, 2, 2, 2]; }

def M5WriteVST1    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST]> { let Latency = 1;
                                                  let NumMicroOps = 1; }
def M5WriteVSTA    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 2;
                                                  let NumMicroOps = 2; }
def M5WriteVSTB    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 3;
                                                  let NumMicroOps = 3; }
def M5WriteVSTC    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 4;
                                                  let NumMicroOps = 4; }
def M5WriteVSTD    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST]> { let Latency = 2; }
def M5WriteVSTE    : SchedWriteRes<[M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 2;
                                                  let NumMicroOps = 1; }
def M5WriteVSTF    : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 4;
                                                  let NumMicroOps = 3; }
def M5WriteVSTG    : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 4;
                                                  let NumMicroOps = 5; }
def M5WriteVSTH    : SchedWriteRes<[M5UnitS0,
                                    M5UnitFST]> { let Latency = 1;
                                                  let NumMicroOps = 1; }
def M5WriteVSTI    : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 8;
                                                  let NumMicroOps = 5;
                                                  let ReleaseAtCycles = [1, 1, 1, 1, 2, 1, 2, 1, 2, 1, 2, 1]; }
def M5WriteVSTJ    : SchedWriteRes<[M5UnitA,
                                    M5UnitS0,
                                    M5UnitFST]> { let Latency = 1;
                                                  let NumMicroOps = 1; }
def M5WriteVSTK    : SchedWriteRes<[M5UnitAX,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 3;
                                                  let NumMicroOps = 2; }
def M5WriteVSTL    : SchedWriteRes<[M5UnitNSHF,
                                    M5UnitNSHF,
                                    M5UnitS,
                                    M5UnitFST,
                                    M5UnitS,
                                    M5UnitFST]> { let Latency = 4;
                                                    let NumMicroOps = 4;
                                                    let ReleaseAtCycles = [1, 1, 2, 1, 2, 1]; }
def M5WriteVSTY   : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M5WriteVSTK]>,
                                       SchedVar<NoSchedPred,         [WriteVST]>]>;

// Special cases.
def M5WriteCOPY    : SchedWriteVariant<[SchedVar<ExynosFPPred, [M5WriteNALU2]>,
                                        SchedVar<NoSchedPred,  [M5WriteZ0]>]>;
def M5WriteMOVI    : SchedWriteVariant<[SchedVar<IsZeroFPIdiomPred, [M5WriteZ0]>,
                                        SchedVar<NoSchedPred,       [M5WriteNALU1]>]>;

// Fast forwarding.
def M5ReadFM1      : SchedReadAdvance<+1, [M5WriteF2]>;
def M5ReadAESM2    : SchedReadAdvance<+2, [M5WriteNCRY2]>;
def M5ReadFMACM1   : SchedReadAdvance<+1, [M5WriteFMAC4]>;
def M5ReadNMULM1   : SchedReadAdvance<+1, [M5WriteNMUL3]>;

//===----------------------------------------------------------------------===//
// Coarse scheduling model.

// Branch instructions.
def : SchedAlias<WriteBr,    M5WriteZ0>;
def : SchedAlias<WriteBrReg, M5WriteC1>;

// Arithmetic and logical integer instructions.
def : SchedAlias<WriteI,     M5WriteA1W>;
def : SchedAlias<WriteIEReg, M5WriteA1W>; // FIXME: M5WriteAX crashes TableGen.
def : SchedAlias<WriteISReg, M5WriteA1W>; // FIXME: M5WriteAX crashes TableGen.
def : SchedAlias<WriteIS,    M5WriteA1W>;

// Move instructions.
def : SchedAlias<WriteImm, M5WriteA1W>;

// Divide and multiply instructions.
def : SchedAlias<WriteID32, M5WriteD10>;
def : SchedAlias<WriteID64, M5WriteD16>;
def : SchedAlias<WriteIM32, M5WriteC2>;
def : SchedAlias<WriteIM64, M5WriteCA>;

// Miscellaneous instructions.
def : SchedAlias<WriteExtr, M5WriteAYW>;

// Addressing modes.
def : SchedAlias<WriteAdr,    M5WriteZ1>;
def : SchedAlias<ReadAdrBase, M5ReadAdrBase>;

// Load instructions.
def : SchedAlias<WriteLD,    M5WriteL4>;
def : SchedAlias<WriteLDHi,  M5WriteZ4>;
def : SchedAlias<WriteLDIdx, M5WriteLX>;

// Store instructions.
def : SchedAlias<WriteST,    M5WriteS1>;
def : SchedAlias<WriteSTP,   M5WriteS1>;
def : SchedAlias<WriteSTX,   M5WriteS1>;
def : SchedAlias<WriteSTIdx, M5WriteSX>;

// Atomic load and store instructions.
def : SchedAlias<WriteAtomic, M5WriteLGW>;

// FP data instructions.
def : SchedAlias<WriteF,    M5WriteFADD2>;
def : SchedAlias<WriteFCmp, M5WriteNMSC2>;
def : SchedAlias<WriteFDiv, M5WriteFDIV12>;
def : SchedAlias<WriteFMul, M5WriteFMAC3>;

// FP miscellaneous instructions.
def : SchedAlias<WriteFCvt,  M5WriteFCVT2>;
def : SchedAlias<WriteFImm,  M5WriteNALU1>;
def : SchedAlias<WriteFCopy, M5WriteNALU2>;

// FP load instructions.
def : SchedAlias<WriteVLD, M5WriteL6>;

// FP store instructions.
def : SchedAlias<WriteVST, M5WriteVST1>;

// ASIMD FP instructions.
def : SchedAlias<WriteVd, M5WriteNALU1>;
def : SchedAlias<WriteVq, M5WriteNALU1>;

// Other miscellaneous instructions.
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }
def : WriteRes<WriteSys,     []> { let Latency = 1; }

//===----------------------------------------------------------------------===//
// Generic fast forwarding.

// TODO: Add FP register forwarding rules.

def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
// TODO: The forwarding for 32 bits actually saves 2 cycles.
def : ReadAdvance<ReadIMA,     3, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;
def : ReadAdvance<ReadST,      0>;

//===----------------------------------------------------------------------===//
// Finer scheduling model.

// Branch instructions
def : InstRW<[M5WriteB1],  (instrs Bcc)>;
def : InstRW<[M5WriteAFX], (instrs BL)>;
def : InstRW<[M5WriteBX],  (instrs BLR)>;
def : InstRW<[M5WriteC1],  (instregex "^CBN?Z[WX]")>;
def : InstRW<[M5WriteAD],  (instregex "^TBN?ZW")>;
def : InstRW<[M5WriteAB],  (instregex "^TBN?ZX")>;

// Arithmetic and logical integer instructions.
def : InstRW<[M5WriteA1W], (instregex "^(ADC|SBC)S?Wr$")>;
def : InstRW<[M5WriteA1X], (instregex "^(ADC|SBC)S?Xr$")>;
def : InstRW<[M5WriteAXW], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|SUB)Wrs$")>;
def : InstRW<[M5WriteAXX], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|SUB)Xrs$")>;
def : InstRW<[M5WriteAUW], (instrs ORRWrs)>;
def : InstRW<[M5WriteAUX], (instrs ORRXrs)>;
def : InstRW<[M5WriteAXW], (instregex "^(ADD|AND|BIC|SUB)SWrs$")>;
def : InstRW<[M5WriteAXX], (instregex "^(ADD|AND|BIC|SUB)SXrs$")>;
def : InstRW<[M5WriteAXW], (instregex "^(ADD|SUB)S?Wrx(64)?$")>;
def : InstRW<[M5WriteAXX], (instregex "^(ADD|SUB)S?Xrx(64)?$")>;
def : InstRW<[M5WriteAVW], (instrs ADDWri, ORRWri)>;
def : InstRW<[M5WriteAVX], (instrs ADDXri, ORRXri)>;
def : InstRW<[M5WriteA1W], (instregex "^CCM[NP]W[ir]$")>;
def : InstRW<[M5WriteA1X], (instregex "^CCM[NP]X[ir]$")>;
def : InstRW<[M5WriteA1W], (instrs CSELWr, CSINCWr, CSINVWr, CSNEGWr)>;
def : InstRW<[M5WriteA1X], (instrs CSELXr, CSINCXr, CSINVXr, CSNEGXr)>;

// Move instructions.
def : InstRW<[M5WriteCOPY], (instrs COPY)>;
def : InstRW<[M5WriteZ0],   (instrs ADR, ADRP)>;
def : InstRW<[M5WriteZ0],   (instregex "^MOV[NZ][WX]i$")>;

// Shift instructions.
def : InstRW<[M5WriteA1W], (instrs ASRVWr, LSLVWr, LSRVWr, RORVWr)>;
def : InstRW<[M5WriteA1X], (instrs ASRVXr, LSLVXr, LSRVXr, RORVXr)>;

// Miscellaneous instructions.
def : InstRW<[M5WriteAYW], (instrs EXTRWrri)>;
def : InstRW<[M5WriteAYX], (instrs EXTRXrri)>;
def : InstRW<[M5WriteA1W], (instrs BFMWri, SBFMWri, UBFMWri)>;
def : InstRW<[M5WriteA1X], (instrs BFMXri, SBFMXri, UBFMXri)>;
def : InstRW<[M5WriteA1W], (instrs CLSWr, CLZWr)>;
def : InstRW<[M5WriteA1X], (instrs CLSXr, CLZXr)>;
def : InstRW<[M5WriteA1W], (instrs RBITWr, REVWr, REV16Wr)>;
def : InstRW<[M5WriteA1X], (instrs RBITXr, REVXr, REV16Xr, REV32Xr)>;

// Load instructions.
def : InstRW<[M5WriteLD,
              WriteLDHi,
              WriteAdr],    (instregex "^LDP(SW|W|X)(post|pre)")>;
def : InstRW<[M5WriteL5,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roW")>;
def : InstRW<[WriteLDIdx,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roX")>;
def : InstRW<[M5WriteL5,
              ReadAdrBase], (instrs PRFMroW)>;
def : InstRW<[WriteLDIdx,
              ReadAdrBase], (instrs PRFMroX)>;

// Store instructions.
def : InstRW<[M5WriteSB,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roW")>;
def : InstRW<[WriteST,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roX")>;

// Atomic load and store instructions.
def : InstRW<[M5WriteLGW], (instregex "^CAS(A|AL|L)?[BHW]$")>;
def : InstRW<[M5WriteLGX], (instregex "^CAS(A|AL|L)?X$")>;
def : InstRW<[M5WriteLFW], (instregex "^CASP(A|AL|L)?W$")>;
def : InstRW<[M5WriteLFX], (instregex "^CASP(A|AL|L)?X$")>;
def : InstRW<[M5WriteLGW], (instregex "^LD(ADD|CLR|EOR|SET|[SU]MAX|[SU]MIN)(A|AL|L)?[BHW]$")>;
def : InstRW<[M5WriteLGX], (instregex "^LD(ADD|CLR|EOR|SET|[SU]MAX|[SU]MIN)(A|AL|L)?X$")>;
def : InstRW<[M5WriteLGW], (instregex "^SWP(A|AL|L)?[BHW]$")>;
def : InstRW<[M5WriteLGX], (instregex "^SWP(A|AL|L)?X$")>;

// FP data instructions.
def : InstRW<[M5WriteNSHF1],  (instrs FABSHr, FABSSr,FABSDr)>;
def : InstRW<[M5WriteFADD2],  (instregex "^F(ADD|SUB)[HSD]rr")>;
def : InstRW<[M5WriteFADD2],  (instregex "^FADDPv.i(16|32|64)")>;
def : InstRW<[M5WriteNEONQ],  (instregex "^FCCMPE?[HSD]rr")>;
def : InstRW<[M5WriteNMSC2],  (instregex "^FCMPE?[HSD]r[ir]")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)(16|32|64|v1)")>;
def : InstRW<[M5WriteFDIV5],  (instrs FDIVHrr)>;
def : InstRW<[M5WriteFDIV7],  (instrs FDIVSrr)>;
def : InstRW<[M5WriteFDIV12], (instrs FDIVDrr)>;
def : InstRW<[M5WriteNMSC1],  (instregex "^F(MAX|MIN)(NM)?[HSD]rr")>;
def : InstRW<[M5WriteFMAC3],  (instregex "^FN?MUL[HSD]rr")>;
def : InstRW<[M5WriteFMAC3],  (instrs FMULX16, FMULX32, FMULX64)>;
def : InstRW<[M5WriteFMAC4,
              M5ReadFMACM1],  (instregex "^FN?M(ADD|SUB)[HSD]rrr")>;
def : InstRW<[M5WriteNALU2],  (instrs FNEGHr, FNEGSr, FNEGDr)>;
def : InstRW<[M5WriteFCVT3A], (instregex "^FRINT.+r")>;
def : InstRW<[M5WriteNEONH],  (instregex "^FCSEL[HSD]rrr")>;
def : InstRW<[M5WriteFSQR5],  (instrs FSQRTHr)>;
def : InstRW<[M5WriteFSQR8],  (instrs FSQRTSr)>;
def : InstRW<[M5WriteFSQR12], (instrs FSQRTDr)>;

// FP miscellaneous instructions.
def : InstRW<[M5WriteFCVT2],  (instregex "^FCVT[HSD][HSD]r")>;
def : InstRW<[M5WriteFCVTC],  (instregex "^[SU]CVTF[SU][XW][HSD]ri")>;
def : InstRW<[M5WriteFCVTB],  (instregex "^FCVT[AMNPZ][SU][SU][XW][HSD]r")>;
def : InstRW<[M5WriteNALU1],  (instregex "^FMOV[HSD]i")>;
def : InstRW<[M5WriteNALU2],  (instregex "^FMOV[HSD]r")>;
def : InstRW<[M5WriteSA],     (instregex "^FMOV[WX][HSD]r")>;
def : InstRW<[M5WriteFCVTA],  (instregex "^FMOV[HSD][WX]r")>;
def : InstRW<[M5WriteNEONI],  (instregex "^FMOVXDHighr")>;
def : InstRW<[M5WriteNEONK],  (instregex "^FMOVDXHighr")>;
def : InstRW<[M5WriteFCVT3],  (instregex "^F(RECP|RSQRT)Ev1(f16|i32|i64)")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^FRECPXv1")>;
def : InstRW<[M5WriteFMAC4],  (instregex "^F(RECP|RSQRT)S(16|32|64)")>;

// FP load instructions.
def : InstRW<[WriteVLD],    (instregex "^LDR[SDQ]l")>;
def : InstRW<[WriteVLD],    (instregex "^LDUR[BHSDQ]i")>;
def : InstRW<[WriteVLD,
              WriteAdr],    (instregex "^LDR[BHSDQ](post|pre)")>;
def : InstRW<[WriteVLD],    (instregex "^LDR[BHSDQ]ui")>;
def : InstRW<[M5WriteLE,
              ReadAdrBase], (instregex "^LDR[BHSDQ]roW")>;
def : InstRW<[WriteVLD,
              ReadAdrBase], (instregex "^LDR[BHSD]roX")>;
def : InstRW<[M5WriteLY,
              ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[WriteVLD,
              M5WriteLH],   (instregex "^LDN?P[SD]i")>;
def : InstRW<[M5WriteLA,
              M5WriteLH],   (instregex "^LDN?PQi")>;
def : InstRW<[M5WriteLB,
              M5WriteLH,
              WriteAdr],    (instregex "^LDP[SD](post|pre)")>;
def : InstRW<[M5WriteLC,
              M5WriteLH,
              WriteAdr],    (instregex "^LDPQ(post|pre)")>;

// FP store instructions.
def : InstRW<[WriteVST],    (instregex "^STUR[BHSDQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STR[BHSDQ](post|pre)")>;
def : InstRW<[WriteVST],    (instregex "^STR[BHSDQ]ui")>;
def : InstRW<[WriteVST,
              ReadAdrBase], (instregex "^STR[BHSD]ro[WX]")>;
def : InstRW<[M5WriteVSTK,
              ReadAdrBase], (instregex "^STRQroW")>;
def : InstRW<[M5WriteVSTY,
              ReadAdrBase], (instregex "^STRQroX")>;
def : InstRW<[WriteVST],    (instregex "^STN?P[SD]i")>;
def : InstRW<[M5WriteVSTH], (instregex "^STN?PQi")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STP[SD](post|pre)")>;
def : InstRW<[M5WriteVSTJ,
              WriteAdr],    (instregex "^STPQ(post|pre)")>;

// ASIMD instructions.
def : InstRW<[M5WriteNHAD1],  (instregex "^[SU]ABDL?v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]ABAL?v")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^ABSv")>;
def : InstRW<[M5WriteNALU2],  (instregex "^(ADD|NEG|SUB)v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]?ADDL?Pv")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]H(ADD|SUB)v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU](ADD|SUB)[LW]v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^R?(ADD|SUB)HN2?v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]Q(ADD|SUB)v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^(SU|US)QADDv")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]RHADDv")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^SQ(ABS|NEG)v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]?ADDL?Vv")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^CM(EQ|GE|GT|HI|HS|LE|LT)v")>;
def : InstRW<[M5WriteNALU2],  (instregex "^CMTSTv")>;
def : InstRW<[M5WriteNALU2],  (instregex "^(AND|BIC|EOR|NOT|ORN|ORR)v")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^[SU](MIN|MAX)v")>;
def : InstRW<[M5WriteNMSC2],  (instregex "^[SU](MIN|MAX)Pv")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU](MIN|MAX)Vv")>;
def : InstRW<[M5WriteNMUL3],  (instregex "^(SQR?D)?MULH?v")>;
def : InstRW<[M5WriteNMUL3,
              M5ReadNMULM1],  (instregex "^ML[AS]v")>;
def : InstRW<[M5WriteNMUL3,
              M5ReadNMULM1],  (instregex "^SQRDML[AS]H")>;
def : InstRW<[M5WriteNMUL3],  (instregex "^(S|U|SQD)ML[AS]L(v1(i32|i64)|v2i32|v4i16|v8i8)")>;
def : InstRW<[M5WriteNMUL3,
              M5ReadNMULM1],  (instregex "^(S|U|SQD)ML[AS]L(v4i32|v8i16|v16i8)")>;
def : InstRW<[M5WriteNMUL3,
              M5ReadNMULM1],  (instregex "^(S|U|SQD)MULL(v1(i32|i64)|v2i32|v4i16|v8i8)")>;
def : InstRW<[M5WriteNMUL3,
              M5ReadNMULM1],  (instregex "^(S|U|SQD)MULL(v4i32|v8i16|v16i8)")>;
def : InstRW<[M5WriteNDOT2],  (instregex "^[SU]DOT(lane)?v")>;
def : InstRW<[M5WriteNHAD3],  (instregex "^[SU]ADALPv")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^[SU]R?SRA[dv]")>;
def : InstRW<[M5WriteNSHT2],  (instregex "^SHL[dv]")>;
def : InstRW<[M5WriteNSHT2],  (instregex "^S[LR]I[dv]")>;
def : InstRW<[M5WriteNSHT2],  (instregex "^[SU]SH[LR][dv]")>;
def : InstRW<[M5WriteNSHT2],  (instregex "^[SU]?SHLLv")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^[SU]?Q?R?SHRU?N[bhsv]")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^[SU]RSH[LR][dv]")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^[SU]QR?SHLU?[bhsdv]")>;

// ASIMD FP instructions.
def : InstRW<[M5WriteNSHF2],  (instregex "^FABSv.f(16|32|64)")>;
def : InstRW<[M5WriteFADD2],  (instregex "^F(ABD|ADD|SUB)v.f(16|32|64)")>;
def : InstRW<[M5WriteFADD2],  (instregex "^FADDPv.f(16|32|64)")>;
def : InstRW<[M5WriteNMSC1],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v[^1]")>;
def : InstRW<[M5WriteFCVT2],  (instregex "^FCVT(L|N|XN)v")>;
def : InstRW<[M5WriteFCVT2A], (instregex "^FCVT[AMNPZ][SU]v")>;
def : InstRW<[M5WriteFCVT2],  (instregex "^[SU]CVTFv.[fi](16|32|64)")>;
def : InstRW<[M5WriteFDIV7],  (instrs FDIVv4f16)>;
def : InstRW<[M5WriteNEONV],  (instrs FDIVv8f16)>;
def : InstRW<[M5WriteFDIV7],  (instrs FDIVv2f32)>;
def : InstRW<[M5WriteNEONV],  (instrs FDIVv4f32)>;
def : InstRW<[M5WriteNEONW],  (instrs FDIVv2f64)>;
def : InstRW<[M5WriteNMSC1],  (instregex "^F(MAX|MIN)(NM)?v")>;
def : InstRW<[M5WriteNMSC2],  (instregex "^F(MAX|MIN)(NM)?Pv")>;
def : InstRW<[M5WriteNEONZ],  (instregex "^F(MAX|MIN)(NM)?Vv")>;
def : InstRW<[M5WriteFMAC3],  (instregex "^FMULX?v.[fi](16|32|64)")>;
def : InstRW<[M5WriteFMAC4,
              M5ReadFMACM1],  (instregex "^FML[AS]v.[fi](16|32|64)")>;
def : InstRW<[M5WriteNALU2],  (instregex "^FNEGv.f(16|32|64)")>;
def : InstRW<[M5WriteFCVT3A], (instregex "^FRINT[AIMNPXZ]v")>;
def : InstRW<[M5WriteFSQR7],  (instrs FSQRTv4f16)>;
def : InstRW<[M5WriteNEONU],  (instrs FSQRTv8f16)>;
def : InstRW<[M5WriteFSQR8],  (instrs FSQRTv2f32)>;
def : InstRW<[M5WriteNEONX],  (instrs FSQRTv4f32)>;
def : InstRW<[M5WriteNEONY],  (instrs FSQRTv2f64)>;

// ASIMD miscellaneous instructions.
def : InstRW<[M5WriteNALU2],  (instregex "^RBITv")>;
def : InstRW<[M5WriteNALU2],  (instregex "^(BIF|BIT|BSL|BSP)v")>;
def : InstRW<[M5WriteNALU2],  (instregex "^CL[STZ]v")>;
def : InstRW<[M5WriteNEONB],  (instregex "^DUPv.+gpr")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^DUP(i8|i16|i32|i64)$")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^DUPv.+lane")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^EXTv")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^XTNv")>;
def : InstRW<[M5WriteNSHT4A], (instregex "^[SU]?QXTU?Nv")>;
def : InstRW<[M5WriteNEONB],  (instregex "^INSv.+gpr")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^INSv.+lane")>;
def : InstRW<[M5WriteMOVI],   (instregex "^(MOV|MVN)I")>;
def : InstRW<[M5WriteNALU1],  (instregex "^FMOVv.f(16|32|64)")>;
def : InstRW<[M5WriteFCVT3],  (instregex "^F(RECP|RSQRT)Ev[248]f(16|32|64)")>;
def : InstRW<[M5WriteFCVT3],  (instregex "^U(RECP|RSQRT)Ev[24]i32")>;
def : InstRW<[M5WriteFMAC4],  (instregex "^F(RECP|RSQRT)Sv.f(16|32|64)")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^REV(16|32|64)v")>;
def : InstRW<[M5WriteNSHFA],  (instregex "^TB[LX]v(8|16)i8One")>;
def : InstRW<[M5WriteNSHFB],  (instregex "^TB[LX]v(8|16)i8Two")>;
def : InstRW<[M5WriteNSHFC],  (instregex "^TB[LX]v(8|16)i8Three")>;
def : InstRW<[M5WriteNSHFD],  (instregex "^TB[LX]v(8|16)i8Four")>;
def : InstRW<[M5WriteNEONP],  (instregex "^[SU]MOVv")>;
def : InstRW<[M5WriteNSHF2],  (instregex "^(TRN|UZP|ZIP)[12]v")>;

// ASIMD load instructions.
def : InstRW<[WriteVLD],    (instregex "LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteVLD,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1Onev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDA], (instregex "LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDA,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1Twov(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDB], (instregex "LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDB,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1Threev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDC], (instregex "LD1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDC,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDD], (instregex "LD1i(8|16|32|64)$")>;
def : InstRW<[M5WriteVLDD,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1i(8|16|32|64)_POST$")>;
def : InstRW<[WriteVLD],    (instregex "LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteVLD,
              M5WriteA1X,
              WriteAdr],    (instregex "LD1Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDF], (instregex "LD2Twov(8b|16b|4h|8h|2s|4s|2d)$")>;
def : InstRW<[M5WriteVLDF,
              M5WriteA1X,
              WriteAdr],    (instregex "LD2Twov(8b|16b|4h|8h|2s|4s|2d)_POST$")>;
def : InstRW<[M5WriteVLDG], (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[M5WriteVLDG,
              M5WriteA1X,
              WriteAdr],    (instregex "LD2i(8|16|32|64)_POST$")>;
def : InstRW<[M5WriteVLDA], (instregex "LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDA,
              M5WriteA1X,
              WriteAdr],    (instregex "LD2Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDI], (instregex "LD3Threev(8b|16b|4h|8h|2s|4s|2d)$")>;
def : InstRW<[M5WriteVLDI,
              M5WriteA1X,
              WriteAdr],    (instregex "LD3Threev(8b|16b|4h|8h|2s|4s|2d)_POST$")>;
def : InstRW<[M5WriteVLDJ], (instregex "LD3i(8|16|32)$")>;
def : InstRW<[M5WriteVLDJ,
              M5WriteA1X,
              WriteAdr],    (instregex "LD3i(8|16|32)_POST$")>;
def : InstRW<[M5WriteVLDL], (instregex "LD3i64$")>;
def : InstRW<[M5WriteVLDL,
              M5WriteA1X,
              WriteAdr],    (instregex "LD3i64_POST$")>;
def : InstRW<[M5WriteVLDB], (instregex "LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDB,
              M5WriteA1X],  (instregex "LD3Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVLDN], (instregex "LD4Fourv(8b|16b|4h|8h|2s|4s|2d)$")>;
def : InstRW<[M5WriteVLDN,
              M5WriteA1X,
              WriteAdr],    (instregex "LD4Fourv(8b|16b|4h|8h|2s|4s|2d)_POST$")>;
def : InstRW<[M5WriteVLDK], (instregex "LD4i(8|16|32)$")>;
def : InstRW<[M5WriteVLDK,
              M5WriteA1X,
              WriteAdr],    (instregex "LD4i(8|16|32)_POST$")>;
def : InstRW<[M5WriteVLDM], (instregex "LD4i64$")>;
def : InstRW<[M5WriteVLDM,
              M5WriteA1X,
              WriteAdr],    (instregex "LD4i64_POST$")>;
def : InstRW<[M5WriteVLDC], (instregex "LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVLDC,
              M5WriteA1X,
              WriteAdr],    (instregex "LD4Rv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

// ASIMD store instructions.
def : InstRW<[WriteVST],    (instregex "ST1Onev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[WriteVST,
              M5WriteA1X,
              WriteAdr],    (instregex "ST1Onev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVSTA], (instregex "ST1Twov(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVSTA,
              M5WriteA1X,
              WriteAdr],    (instregex "ST1Twov(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;

def : InstRW<[M5WriteVSTB], (instregex "ST1Threev(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVSTB,
              M5WriteA1X,
              WriteAdr],    (instregex "ST1Threev(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[M5WriteVSTC], (instregex "ST1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)$")>;
def : InstRW<[M5WriteVSTC,
              M5WriteA1X,
              WriteAdr],    (instregex "ST1Fourv(8b|16b|4h|8h|2s|4s|1d|2d)_POST$")>;
def : InstRW<[WriteVST],    (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[WriteVST,
              M5WriteA1X,
              WriteAdr],    (instregex "ST1i(8|16|32|64)_POST$")>;
def : InstRW<[M5WriteVSTD], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[M5WriteVSTD,
              M5WriteA1X,
              WriteAdr],    (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[M5WriteVSTE], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M5WriteVSTE,
              M5WriteA1X,
              WriteAdr],    (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M5WriteVSTD], (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[M5WriteVSTD,
              M5WriteA1X,
              WriteAdr],    (instregex "ST2i(8|16|32|64)_POST$")>;
def : InstRW<[M5WriteVSTF], (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[M5WriteVSTF,
              M5WriteA1X,
              WriteAdr],    (instregex "ST3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[M5WriteVSTG], (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M5WriteVSTG,
              M5WriteA1X,
              WriteAdr],    (instregex "ST3Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M5WriteVSTA], (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[M5WriteVSTA,
              M5WriteA1X,
              WriteAdr],    (instregex "ST3i(8|16|32|64)_POST$")>;
def : InstRW<[M5WriteVSTL], (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[M5WriteVSTL,
              M5WriteA1X,
              WriteAdr],    (instregex "ST4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[M5WriteVSTI], (instregex "ST4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M5WriteVSTI,
              M5WriteA1X,
              WriteAdr],    (instregex "ST4Fourv(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M5WriteVSTA], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[M5WriteVSTA,
              M5WriteA1X,
              WriteAdr],    (instregex "ST4i(8|16|32|64)_POST$")>;

// Cryptography instructions.
def : InstRW<[M5WriteNCRY2],  (instregex "^AES[DE]")>;
def : InstRW<[M5WriteNCRY2,
              M5ReadAESM2],   (instregex "^AESI?MC")>;
def : InstRW<[M5WriteNCRY2A], (instregex "^PMULv")>;
def : InstRW<[M5WriteNCRY1A], (instregex "^PMULLv(1|8)i")>;
def : InstRW<[M5WriteNCRY3A], (instregex "^PMULLv(2|16)i")>;
def : InstRW<[M5WriteNCRY2A], (instregex "^SHA1(H|SU[01])")>;
def : InstRW<[M5WriteNCRY5A], (instregex "^SHA1[CMP]")>;
def : InstRW<[M5WriteNCRY2A], (instrs SHA256SU0rr)>;
def : InstRW<[M5WriteNCRY5A], (instrs SHA256SU1rrr)>;
def : InstRW<[M5WriteNCRY5A], (instregex "^SHA256H2?")>;

// CRC instructions.
def : InstRW<[M5WriteF2,
              M5ReadFM1], (instregex "^CRC32C?[BHWX]")>;

} // SchedModel = ExynosM5Model
