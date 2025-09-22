//=- AArch64SchedExynosM3.td - Samsung Exynos M3 Sched Defs --*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Samsung Exynos M3 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The Exynos-M3 is an advanced superscalar microprocessor with a 6-wide
// in-order stage for decode and dispatch and a wider issue stage.
// The execution units and loads and stores are out-of-order.

def ExynosM3Model : SchedMachineModel {
  let IssueWidth            =   6; // Up to 6 uops per cycle.
  let MicroOpBufferSize     = 228; // ROB size.
  let LoopMicroOpBufferSize =  40; // Based on the instruction queue size.
  let LoadLatency           =   4; // Optimistic load cases.
  let MispredictPenalty     =  16; // Minimum branch misprediction penalty.
  let CompleteModel         =   1; // Use the default model otherwise.

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    PAUnsupported.F,
                                                    SMEUnsupported.F,
                                                    [HasMTE, HasCSSC]);
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on the Exynos-M3,
// which has 12 pipelines, each with its own queue with out-of-order dispatch.

let SchedModel = ExynosM3Model in {

def M3UnitA  : ProcResource<2>; // Simple integer
def M3UnitC  : ProcResource<2>; // Simple and complex integer
def M3UnitD  : ProcResource<1>; // Integer division (inside C0, serialized)
def M3UnitB  : ProcResource<2>; // Branch
def M3UnitL  : ProcResource<2>; // Load
def M3UnitS  : ProcResource<1>; // Store
def M3PipeF0 : ProcResource<1>; // FP #0
let Super = M3PipeF0 in {
  def M3UnitFMAC0 : ProcResource<1>; // FP multiplication
  def M3UnitFADD0 : ProcResource<1>; // Simple FP
  def M3UnitFCVT0 : ProcResource<1>; // FP conversion
  def M3UnitFSQR  : ProcResource<2>; // FP square root (serialized)
  def M3UnitNALU0 : ProcResource<1>; // Simple vector
  def M3UnitNMSC  : ProcResource<1>; // FP and vector miscellanea
  def M3UnitNSHT0 : ProcResource<1>; // Vector shifting
  def M3UnitNSHF0 : ProcResource<1>; // Vector shuffling
}
def M3PipeF1 : ProcResource<1>; // FP #1
let Super = M3PipeF1 in {
  def M3UnitFMAC1 : ProcResource<1>; // FP multiplication
  def M3UnitFADD1 : ProcResource<1>; // Simple FP
  def M3UnitFDIV0 : ProcResource<2>; // FP division (serialized)
  def M3UnitFCVT1 : ProcResource<1>; // FP conversion
  def M3UnitFST0  : ProcResource<1>; // FP store
  def M3UnitNALU1 : ProcResource<1>; // Simple vector
  def M3UnitNCRY0 : ProcResource<1>; // Cryptographic
  def M3UnitNMUL  : ProcResource<1>; // Vector multiplication
  def M3UnitNSHT1 : ProcResource<1>; // Vector shifting
  def M3UnitNSHF1 : ProcResource<1>; // Vector shuffling
}
def M3PipeF2 : ProcResource<1>; // FP #2
let Super = M3PipeF2 in {
  def M3UnitFMAC2 : ProcResource<1>; // FP multiplication
  def M3UnitFADD2 : ProcResource<1>; // Simple FP
  def M3UnitFDIV1 : ProcResource<2>; // FP division (serialized)
  def M3UnitFST1  : ProcResource<1>; // FP store
  def M3UnitNALU2 : ProcResource<1>; // Simple vector
  def M3UnitNCRY1 : ProcResource<1>; // Cryptographic
  def M3UnitNSHT2 : ProcResource<1>; // Vector shifting
  def M3UnitNSHF2 : ProcResource<1>; // Vector shuffling
}


def M3UnitALU  : ProcResGroup<[M3UnitA,
                               M3UnitC]>;
def M3UnitFMAC : ProcResGroup<[M3UnitFMAC0,
                               M3UnitFMAC1,
                               M3UnitFMAC2]>;
def M3UnitFADD : ProcResGroup<[M3UnitFADD0,
                               M3UnitFADD1,
                               M3UnitFADD2]>;
def M3UnitFDIV : ProcResGroup<[M3UnitFDIV0,
                               M3UnitFDIV1]>;
def M3UnitFCVT : ProcResGroup<[M3UnitFCVT0,
                               M3UnitFCVT1]>;
def M3UnitFST  : ProcResGroup<[M3UnitFST0,
                               M3UnitFST1]>;
def M3UnitNALU : ProcResGroup<[M3UnitNALU0,
                               M3UnitNALU1,
                               M3UnitNALU2]>;
def M3UnitNCRY : ProcResGroup<[M3UnitNCRY0,
                               M3UnitNCRY1]>;
def M3UnitNSHT : ProcResGroup<[M3UnitNSHT0,
                               M3UnitNSHT1,
                               M3UnitNSHT2]>;
def M3UnitNSHF : ProcResGroup<[M3UnitNSHF0,
                               M3UnitNSHF1,
                               M3UnitNSHF2]>;

//===----------------------------------------------------------------------===//
// Coarse scheduling model.

def M3WriteZ0 : SchedWriteRes<[]> { let Latency = 0;
                                    let NumMicroOps = 1; }
def M3WriteZ1 : SchedWriteRes<[]> { let Latency = 1;
                                    let NumMicroOps = 0; }

def M3WriteA1 : SchedWriteRes<[M3UnitALU]> { let Latency = 1; }
def M3WriteAA : SchedWriteRes<[M3UnitALU]> { let Latency = 2;
                                             let ReleaseAtCycles = [2]; }
def M3WriteAB : SchedWriteRes<[M3UnitALU,
                               M3UnitC]>   { let Latency = 1;
                                             let NumMicroOps = 2; }
def M3WriteAC : SchedWriteRes<[M3UnitALU,
                               M3UnitALU,
                               M3UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 3; }
def M3WriteAD : SchedWriteRes<[M3UnitALU,
                               M3UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M3WriteC1 : SchedWriteRes<[M3UnitC]>   { let Latency = 1; }
def M3WriteC2 : SchedWriteRes<[M3UnitC]>   { let Latency = 2; }
def M3WriteAU : SchedWriteVariant<[SchedVar<IsCopyIdiomPred, [M3WriteZ0]>,
                                   SchedVar<ExynosArithPred, [M3WriteA1]>,
                                   SchedVar<ExynosLogicPred, [M3WriteA1]>,
                                   SchedVar<NoSchedPred,     [M3WriteAA]>]>;
def M3WriteAV : SchedWriteVariant<[SchedVar<IsCopyIdiomPred, [M3WriteZ0]>,
                                   SchedVar<ExynosArithPred, [M3WriteA1]>,
                                   SchedVar<NoSchedPred,     [M3WriteAA]>]>;
def M3WriteAW : SchedWriteVariant<[SchedVar<IsZeroIdiomPred, [M3WriteZ0]>,
                                   SchedVar<ExynosLogicPred, [M3WriteA1]>,
                                   SchedVar<NoSchedPred,     [M3WriteAA]>]>;
def M3WriteAX : SchedWriteVariant<[SchedVar<ExynosArithPred, [M3WriteA1]>,
                                   SchedVar<ExynosLogicPred, [M3WriteA1]>,
                                   SchedVar<NoSchedPred,     [M3WriteAA]>]>;
def M3WriteAY : SchedWriteVariant<[SchedVar<IsRORImmIdiomPred, [M3WriteA1]>,
                                   SchedVar<NoSchedPred,       [M3WriteAA]>]>;

def M3WriteB1 : SchedWriteRes<[M3UnitB]> { let Latency = 1; }
def M3WriteBX : SchedWriteVariant<[SchedVar<ExynosBranchLinkLRPred, [M3WriteAC]>,
                                   SchedVar<NoSchedPred,            [M3WriteAB]>]>;

def M3WriteL4 : SchedWriteRes<[M3UnitL]> { let Latency = 4; }
def M3WriteL5 : SchedWriteRes<[M3UnitL]> { let Latency = 5; }
def M3WriteLA : SchedWriteRes<[M3UnitL,
                               M3UnitL]> { let Latency = 5;
                                           let NumMicroOps = 1; }
def M3WriteLB : SchedWriteRes<[M3UnitA,
                               M3UnitL]> { let Latency = 5;
                                           let NumMicroOps = 2; }
def M3WriteLC : SchedWriteRes<[M3UnitA,
                               M3UnitL,
                               M3UnitL]> { let Latency = 5;
                                           let NumMicroOps = 2; }
def M3WriteLD : SchedWriteRes<[M3UnitA,
                               M3UnitL]> { let Latency = 4;
                                           let NumMicroOps = 2; }
def M3WriteLE : SchedWriteRes<[M3UnitA,
                               M3UnitL]> { let Latency = 6;
                                           let NumMicroOps = 2; }
def M3WriteLH : SchedWriteRes<[]>        { let Latency = 5;
                                           let NumMicroOps = 0; }
def M3WriteLX : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M3WriteL5]>,
                                   SchedVar<NoSchedPred,         [M3WriteL4]>]>;
def M3WriteLY : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M3WriteLE]>,
                                   SchedVar<NoSchedPred,         [M3WriteL5]>]>;

def M3WriteS1 : SchedWriteRes<[M3UnitS]>   { let Latency = 1; }
def M3WriteSA : SchedWriteRes<[M3UnitA,
                               M3UnitS,
                               M3UnitFST]> { let Latency = 3;
                                             let NumMicroOps = 2; }
def M3WriteSB : SchedWriteRes<[M3UnitA,
                               M3UnitS]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M3WriteSC : SchedWriteRes<[M3UnitA,
                               M3UnitS,
                               M3UnitFST]> { let Latency = 1;
                                             let NumMicroOps = 2; }
def M3WriteSY : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M3WriteSA]>,
                                   SchedVar<NoSchedPred,         [WriteVST]>]>;

def M3ReadAdrBase : SchedReadVariant<[SchedVar<ExynosScaledIdxPred, [ReadDefault]>,
                                      SchedVar<NoSchedPred,         [ReadDefault]>]>;

// Branch instructions.
def : SchedAlias<WriteBr, M3WriteZ0>;
def : SchedAlias<WriteBrReg, M3WriteC1>;

// Arithmetic and logical integer instructions.
def : SchedAlias<WriteI,     M3WriteA1>;
def : SchedAlias<WriteISReg, M3WriteA1>;
def : SchedAlias<WriteIEReg, M3WriteA1>;
def : SchedAlias<WriteIS,    M3WriteA1>;

// Move instructions.
def : SchedAlias<WriteImm, M3WriteA1>;

// Divide and multiply instructions.
def : WriteRes<WriteID32, [M3UnitC,
                           M3UnitD]>  { let Latency = 12;
                                        let ReleaseAtCycles = [1, 12]; }
def : WriteRes<WriteID64, [M3UnitC,
                           M3UnitD]>  { let Latency = 21;
                                        let ReleaseAtCycles = [1, 21]; }
def : WriteRes<WriteIM32, [M3UnitC]>  { let Latency = 3; }
def : WriteRes<WriteIM64, [M3UnitC]>  { let Latency = 4;
                                        let ReleaseAtCycles = [2]; }

// Miscellaneous instructions.
def : SchedAlias<WriteExtr, M3WriteAY>;

// Addressing modes.
def : SchedAlias<WriteAdr,    M3WriteZ1>;
def : SchedAlias<ReadAdrBase, M3ReadAdrBase>;

// Load instructions.
def : SchedAlias<WriteLD, M3WriteL4>;
def : WriteRes<WriteLDHi, []> { let Latency = 4;
                                let NumMicroOps = 0; }
def : SchedAlias<WriteLDIdx, M3WriteLB>;

// Store instructions.
def : SchedAlias<WriteST,    M3WriteS1>;
def : SchedAlias<WriteSTP,   M3WriteS1>;
def : SchedAlias<WriteSTX,   M3WriteS1>;
def : SchedAlias<WriteSTIdx, M3WriteSB>;

// FP data instructions.
def : WriteRes<WriteF,    [M3UnitFADD]>  { let Latency = 2; }
def : WriteRes<WriteFCmp, [M3UnitNMSC]>  { let Latency = 2; }
def : WriteRes<WriteFDiv, [M3UnitFDIV]>  { let Latency = 12;
                                           let ReleaseAtCycles = [12]; }
def : WriteRes<WriteFMul, [M3UnitFMAC]>  { let Latency = 4; }

// FP miscellaneous instructions.
def : WriteRes<WriteFCvt,  [M3UnitFCVT]> { let Latency = 3; }
def : WriteRes<WriteFImm,  [M3UnitNALU]> { let Latency = 1; }
def : WriteRes<WriteFCopy, [M3UnitNALU]> { let Latency = 1; }

// FP load instructions.
def : SchedAlias<WriteVLD, M3WriteL5>;

// FP store instructions.
def : WriteRes<WriteVST, [M3UnitS,
                          M3UnitFST]> { let Latency = 1;
                                        let NumMicroOps = 1; }

// ASIMD FP instructions.
def : WriteRes<WriteVd, [M3UnitNALU]> { let Latency = 3; }
def : WriteRes<WriteVq, [M3UnitNALU]> { let Latency = 3; }

// Other miscellaneous instructions.
def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }
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

def M3WriteNEONA   : SchedWriteRes<[M3UnitNSHF,
                                    M3UnitFADD]>  { let Latency = 3;
                                                    let NumMicroOps = 2; }
def M3WriteNEONB   : SchedWriteRes<[M3UnitNALU,
                                    M3UnitFST]>   { let Latency = 10;
                                                    let NumMicroOps = 2; }
def M3WriteNEOND   : SchedWriteRes<[M3UnitNSHF,
                                    M3UnitFST]>   { let Latency = 6;
                                                    let NumMicroOps = 2; }
def M3WriteNEONH   : SchedWriteRes<[M3UnitNALU,
                                    M3UnitS]>     { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M3WriteNEONI   : SchedWriteRes<[M3UnitNSHF,
                                    M3UnitS]>     { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M3WriteNEONV   : SchedWriteRes<[M3UnitFDIV0,
                                    M3UnitFDIV1]>  { let Latency = 7;
                                                     let NumMicroOps = 2;
                                                     let ReleaseAtCycles = [8, 8]; }
def M3WriteNEONW   : SchedWriteRes<[M3UnitFDIV0,
                                    M3UnitFDIV1]>  { let Latency = 12;
                                                     let NumMicroOps = 2;
                                                     let ReleaseAtCycles = [13, 13]; }
def M3WriteNEONX   : SchedWriteRes<[M3UnitFSQR,
                                    M3UnitFSQR]>  { let Latency = 18;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [19, 19]; }
def M3WriteNEONY   : SchedWriteRes<[M3UnitFSQR,
                                    M3UnitFSQR]>  { let Latency = 25;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [26, 26]; }
def M3WriteNEONZ   : SchedWriteRes<[M3UnitNMSC,
                                    M3UnitNMSC]>  { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M3WriteFADD2   : SchedWriteRes<[M3UnitFADD]>  { let Latency = 2; }
def M3WriteFCVT2   : SchedWriteRes<[M3UnitFCVT]>  { let Latency = 2; }
def M3WriteFCVT3   : SchedWriteRes<[M3UnitFCVT]>  { let Latency = 3; }
def M3WriteFCVT3A  : SchedWriteRes<[M3UnitFCVT0]> { let Latency = 3; }
def M3WriteFCVT4A  : SchedWriteRes<[M3UnitFCVT0]> { let Latency = 4; }
def M3WriteFCVT4   : SchedWriteRes<[M3UnitFCVT]>  { let Latency = 4; }
def M3WriteFDIV10  : SchedWriteRes<[M3UnitFDIV]>  { let Latency = 7;
                                                    let ReleaseAtCycles = [8]; }
def M3WriteFDIV12  : SchedWriteRes<[M3UnitFDIV]>  { let Latency = 12;
                                                    let ReleaseAtCycles = [13]; }
def M3WriteFMAC3   : SchedWriteRes<[M3UnitFMAC]>  { let Latency = 3; }
def M3WriteFMAC4   : SchedWriteRes<[M3UnitFMAC]>  { let Latency = 4; }
def M3WriteFMAC5   : SchedWriteRes<[M3UnitFMAC]>  { let Latency = 5; }
def M3WriteFSQR17  : SchedWriteRes<[M3UnitFSQR]>  { let Latency = 18;
                                                    let ReleaseAtCycles = [19]; }
def M3WriteFSQR25  : SchedWriteRes<[M3UnitFSQR]>  { let Latency = 25;
                                                    let ReleaseAtCycles = [26]; }
def M3WriteNALU1   : SchedWriteRes<[M3UnitNALU]>  { let Latency = 1; }
def M3WriteNCRY1A  : SchedWriteRes<[M3UnitNCRY0]> { let Latency = 1; }
def M3WriteNCRY3A  : SchedWriteRes<[M3UnitNCRY0]> { let Latency = 3; }
def M3WriteNCRY5A  : SchedWriteRes<[M3UnitNCRY]>  { let Latency = 5; }
def M3WriteNMSC1   : SchedWriteRes<[M3UnitNMSC]>  { let Latency = 1; }
def M3WriteNMSC2   : SchedWriteRes<[M3UnitNMSC]>  { let Latency = 2; }
def M3WriteNMSC3   : SchedWriteRes<[M3UnitNMSC]>  { let Latency = 3; }
def M3WriteNMUL3   : SchedWriteRes<[M3UnitNMUL]>  { let Latency = 3; }
def M3WriteNSHF1   : SchedWriteRes<[M3UnitNSHF]>  { let Latency = 1; }
def M3WriteNSHF3   : SchedWriteRes<[M3UnitNSHF]>  { let Latency = 3; }
def M3WriteNSHT1   : SchedWriteRes<[M3UnitNSHT]>  { let Latency = 1; }
def M3WriteNSHT2   : SchedWriteRes<[M3UnitNSHT]>  { let Latency = 2; }
def M3WriteNSHT3   : SchedWriteRes<[M3UnitNSHT]>  { let Latency = 3; }
def M3WriteVLDA    : SchedWriteRes<[M3UnitL,
                                    M3UnitL]>     { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M3WriteVLDB    : SchedWriteRes<[M3UnitL,
                                    M3UnitL,
                                    M3UnitL]>     { let Latency = 6;
                                                    let NumMicroOps = 3; }
def M3WriteVLDC    : SchedWriteRes<[M3UnitL,
                                    M3UnitL,
                                    M3UnitL,
                                    M3UnitL]>     { let Latency = 6;
                                                    let NumMicroOps = 4; }
def M3WriteVLDD    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU]>  { let Latency = 7;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [2, 1]; }
def M3WriteVLDE    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU]>  { let Latency = 6;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [2, 1]; }
def M3WriteVLDF    : SchedWriteRes<[M3UnitL,
                                    M3UnitL]>     { let Latency = 10;
                                                    let NumMicroOps = 2;
                                                    let ReleaseAtCycles = [5, 5]; }
def M3WriteVLDG    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU]>  { let Latency = 7;
                                                    let NumMicroOps = 3;
                                                    let ReleaseAtCycles = [2, 1, 1]; }
def M3WriteVLDH    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU]>  { let Latency = 6;
                                                    let NumMicroOps = 3;
                                                    let ReleaseAtCycles = [2, 1, 1]; }
def M3WriteVLDI    : SchedWriteRes<[M3UnitL,
                                    M3UnitL,
                                    M3UnitL]>     { let Latency = 12;
                                                    let NumMicroOps = 3;
                                                    let ReleaseAtCycles = [6, 6, 6]; }
def M3WriteVLDJ    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU,
                                    M3UnitNALU]>  { let Latency = 7;
                                                    let NumMicroOps = 4;
                                                    let ReleaseAtCycles = [2, 1, 1, 1]; }
def M3WriteVLDK    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU,
                                    M3UnitNALU,
                                    M3UnitNALU]>  { let Latency = 9;
                                                    let NumMicroOps = 5;
                                                    let ReleaseAtCycles = [4, 1, 1, 1, 1]; }
def M3WriteVLDL    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU,
                                    M3UnitL,
                                    M3UnitNALU]>  { let Latency = 6;
                                                    let NumMicroOps = 5;
                                                    let ReleaseAtCycles = [6, 1, 1, 6, 1]; }
def M3WriteVLDM    : SchedWriteRes<[M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU,
                                    M3UnitL,
                                    M3UnitNALU,
                                    M3UnitNALU]>  { let Latency = 7;
                                                    let NumMicroOps = 6;
                                                    let ReleaseAtCycles = [6, 1, 1, 6, 1, 1]; }
def M3WriteVLDN    : SchedWriteRes<[M3UnitL,
                                    M3UnitL,
                                    M3UnitL,
                                    M3UnitL]>     { let Latency = 14;
                                                    let NumMicroOps = 4;
                                                    let ReleaseAtCycles = [6, 6, 6, 6]; }
def M3WriteVSTA    : WriteSequence<[WriteVST], 2>;
def M3WriteVSTB    : WriteSequence<[WriteVST], 3>;
def M3WriteVSTC    : WriteSequence<[WriteVST], 4>;
def M3WriteVSTD    : SchedWriteRes<[M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 7;
                                                    let NumMicroOps = 4;
                                                    let ReleaseAtCycles = [1, 3, 1, 3]; }
def M3WriteVSTE    : SchedWriteRes<[M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 8;
                                                    let NumMicroOps = 6;
                                                    let ReleaseAtCycles = [1, 3, 1, 3, 1, 3]; }
def M3WriteVSTF    : SchedWriteRes<[M3UnitNALU,
                                    M3UnitFST,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 15;
                                                    let NumMicroOps = 7;
                                                    let ReleaseAtCycles = [1, 3, 3, 1, 3, 1, 3]; }
def M3WriteVSTG    : SchedWriteRes<[M3UnitNALU,
                                    M3UnitFST,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 16;
                                                    let NumMicroOps = 9;
                                                    let ReleaseAtCycles = [1, 3, 3, 1, 3, 1, 3, 1, 3]; }
def M3WriteVSTH    : SchedWriteRes<[M3UnitNALU,
                                    M3UnitFST,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 14;
                                                    let NumMicroOps = 5;
                                                    let ReleaseAtCycles = [1, 3, 3, 1, 3]; }
def M3WriteVSTI    : SchedWriteRes<[M3UnitNALU,
                                    M3UnitFST,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST,
                                    M3UnitS,
                                    M3UnitFST]>   { let Latency = 17;
                                                    let NumMicroOps = 9;
                                                    let ReleaseAtCycles = [1, 3, 3, 1, 3, 1, 3, 1, 3]; }

// Special cases.
def M3WriteAES     : SchedWriteRes<[M3UnitNCRY]>  { let Latency = 1; }
def M3WriteCOPY    : SchedWriteVariant<[SchedVar<ExynosFPPred, [M3WriteNALU1]>,
                                        SchedVar<NoSchedPred,  [M3WriteZ0]>]>;
def M3WriteMOVI    : SchedWriteVariant<[SchedVar<IsZeroFPIdiomPred, [M3WriteZ0]>,
                                        SchedVar<NoSchedPred,       [M3WriteNALU1]>]>;

// Fast forwarding.
def M3ReadAES      : SchedReadAdvance<1, [M3WriteAES]>;
def M3ReadFMAC     : SchedReadAdvance<1, [M3WriteFMAC4,
                                          M3WriteFMAC5]>;
def M3ReadNMUL     : SchedReadAdvance<1, [M3WriteNMUL3]>;

// Branch instructions
def : InstRW<[M3WriteB1], (instrs Bcc)>;
def : InstRW<[M3WriteA1], (instrs BL)>;
def : InstRW<[M3WriteBX], (instrs BLR)>;
def : InstRW<[M3WriteC1], (instregex "^CBN?Z[WX]")>;
def : InstRW<[M3WriteAD], (instregex "^TBN?Z[WX]")>;

// Arithmetic and logical integer instructions.
def : InstRW<[M3WriteAX], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|SUB)[WX]rs$")>;
def : InstRW<[M3WriteAU], (instrs ORRWrs, ORRXrs)>;
def : InstRW<[M3WriteAX], (instregex "^(ADD|SUB)S?[WX]rx(64)?$")>;
def : InstRW<[M3WriteAX], (instregex "^(ADD|AND|BIC|SUB)S[WX]rs$")>;
def : InstRW<[M3WriteAV], (instrs ADDWri, ADDXri)>;
def : InstRW<[M3WriteAW], (instrs ORRWri, ORRXri)>;

// Move instructions.
def : InstRW<[M3WriteCOPY], (instrs COPY)>;
def : InstRW<[M3WriteZ0],   (instrs ADR, ADRP)>;
def : InstRW<[M3WriteZ0],   (instregex "^MOV[NZ][WX]i")>;

// Divide and multiply instructions.

// Miscellaneous instructions.

// Load instructions.
def : InstRW<[M3WriteLD,
              WriteLDHi,
              WriteAdr],    (instregex "^LDP(SW|W|X)(post|pre)")>;
def : InstRW<[M3WriteLB,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roW")>;
def : InstRW<[M3WriteLX,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roX")>;
def : InstRW<[M3WriteLB,
              ReadAdrBase], (instrs PRFMroW)>;
def : InstRW<[M3WriteLX,
              ReadAdrBase], (instrs PRFMroX)>;

// Store instructions.
def : InstRW<[M3WriteSB,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roW")>;
def : InstRW<[WriteST,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roX")>;

// FP data instructions.
def : InstRW<[M3WriteNSHF1],  (instregex "^FABS[DS]r")>;
def : InstRW<[M3WriteFADD2],  (instregex "^F(ADD|SUB)[DS]rr")>;
def : InstRW<[M3WriteFDIV10], (instrs FDIVSrr)>;
def : InstRW<[M3WriteFDIV12], (instrs FDIVDrr)>;
def : InstRW<[M3WriteNMSC1],  (instregex "^F(MAX|MIN).+rr")>;
def : InstRW<[M3WriteFMAC3],  (instregex "^FN?MUL[DS]rr")>;
def : InstRW<[M3WriteFMAC4,
              M3ReadFMAC],    (instregex "^FN?M(ADD|SUB)[DS]rrr")>;
def : InstRW<[M3WriteNALU1],  (instregex "^FNEG[DS]r")>;
def : InstRW<[M3WriteFCVT3A], (instregex "^FRINT.+r")>;
def : InstRW<[M3WriteNEONH],  (instregex "^FCSEL[DS]rrr")>;
def : InstRW<[M3WriteFSQR17], (instrs FSQRTSr)>;
def : InstRW<[M3WriteFSQR25], (instrs FSQRTDr)>;

// FP miscellaneous instructions.
def : InstRW<[M3WriteFCVT3],  (instregex "^FCVT[DHS][DHS]r")>;
def : InstRW<[M3WriteFCVT4A], (instregex "^[SU]CVTF[SU][XW][DHS]ri")>;
def : InstRW<[M3WriteFCVT3A], (instregex "^FCVT[AMNPZ][SU]U[XW][DHS]r")>;
def : InstRW<[M3WriteFCVT3A], (instregex "^FCVTZ[SU][dhs]")>;
def : InstRW<[M3WriteNALU1],  (instregex "^FMOV[DS][ir]")>;
def : InstRW<[M3WriteFCVT4],  (instregex "^[FU](RECP|RSQRT)Ev1")>;
def : InstRW<[M3WriteNMSC1],  (instregex "^FRECPXv1")>;
def : InstRW<[M3WriteFMAC4,
              M3ReadFMAC],    (instregex "^F(RECP|RSQRT)S(16|32|64)")>;
def : InstRW<[M3WriteNALU1],  (instregex "^FMOV[WX][DS]r")>;
def : InstRW<[M3WriteNALU1],  (instregex "^FMOV[DS][WX]r")>;
def : InstRW<[M3WriteNEONI],  (instregex "^FMOV(DX|XD)Highr")>;

// FP load instructions.
def : InstRW<[WriteVLD],    (instregex "^LDR[DSQ]l")>;
def : InstRW<[WriteVLD],    (instregex "^LDUR[BDHSQ]i")>;
def : InstRW<[WriteVLD,
              WriteAdr],    (instregex "^LDR[BDHSQ](post|pre)")>;
def : InstRW<[WriteVLD],    (instregex "^LDR[BDHSQ]ui")>;
def : InstRW<[M3WriteLE,
              ReadAdrBase], (instregex "^LDR[BDHS]roW")>;
def : InstRW<[WriteVLD,
              ReadAdrBase], (instregex "^LDR[BDHS]roX")>;
def : InstRW<[M3WriteLY,
              ReadAdrBase], (instregex "^LDRQro[WX]")>;
def : InstRW<[WriteVLD,
              M3WriteLH],   (instregex "^LDN?P[DS]i")>;
def : InstRW<[M3WriteLA,
              M3WriteLH],   (instregex "^LDN?PQi")>;
def : InstRW<[M3WriteLB,
              M3WriteLH,
              WriteAdr],    (instregex "^LDP[DS](post|pre)")>;
def : InstRW<[M3WriteLC,
              M3WriteLH,
              WriteAdr],    (instregex "^LDPQ(post|pre)")>;

// FP store instructions.
def : InstRW<[WriteVST],    (instregex "^STUR[BDHSQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STR[BDHSQ](post|pre)")>;
def : InstRW<[WriteVST],    (instregex "^STR[BDHSQ]ui")>;
def : InstRW<[M3WriteSA,
              ReadAdrBase], (instregex "^STR[BDHS]roW")>;
def : InstRW<[M3WriteSA,
              ReadAdrBase], (instregex "^STRQroW")>;
def : InstRW<[WriteVST,
              ReadAdrBase], (instregex "^STR[BDHS]roX")>;
def : InstRW<[M3WriteSY,
              ReadAdrBase], (instregex "^STRQroX")>;
def : InstRW<[WriteVST],    (instregex "^STN?P[DSQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STP[DS](post|pre)")>;
def : InstRW<[M3WriteSC,
              WriteAdr],    (instregex "^STPQ(post|pre)")>;

// ASIMD instructions.
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]ABAL?v")>;
def : InstRW<[M3WriteNMSC1], (instregex "^[SU]ABDL?v")>;
def : InstRW<[M3WriteNMSC1], (instregex "^((SQ)?ABS|SQNEG)v")>;
def : InstRW<[M3WriteNALU1], (instregex "^(ADD|NEG|SUB)v")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]?ADDL?Pv")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]H(ADD|SUB)v")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU](ADD|SUB)[LW]V?v")>;
def : InstRW<[M3WriteNMSC3], (instregex "^R?(ADD|SUB)HN2?v")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]Q(ADD|SUB)v")>;
def : InstRW<[M3WriteNMSC3], (instregex "^(SU|US)QADDv")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]RHADDv")>;
def : InstRW<[M3WriteNMSC1], (instregex "^CM(EQ|GE|GT|HI|HS|LE|LT)v")>;
def : InstRW<[M3WriteNALU1], (instregex "^CMTSTv")>;
def : InstRW<[M3WriteNALU1], (instregex "^(AND|BIC|EOR|MVNI|NOT|ORN|ORR)v")>;
def : InstRW<[M3WriteNMSC1], (instregex "^[SU](MIN|MAX)v")>;
def : InstRW<[M3WriteNMSC2], (instregex "^[SU](MIN|MAX)Pv")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU](MIN|MAX)Vv")>;
def : InstRW<[M3WriteNMUL3], (instregex "^(MUL|SQR?DMULH)v")>;
def : InstRW<[M3WriteNMUL3,
              M3ReadNMUL],   (instregex "^ML[AS]v")>;
def : InstRW<[M3WriteNMUL3], (instregex "^[SU]ML[AS]Lv")>;
def : InstRW<[M3WriteNMUL3], (instregex "^SQDML[AS]L")>;
def : InstRW<[M3WriteNMUL3], (instregex "^(S|U|SQD)MULLv")>;
def : InstRW<[M3WriteNMSC3], (instregex "^[SU]ADALPv")>;
def : InstRW<[M3WriteNSHT3], (instregex "^[SU]R?SRAv")>;
def : InstRW<[M3WriteNSHT1], (instregex "^SHL[dv]")>;
def : InstRW<[M3WriteNSHT1], (instregex "^[SU]SH[LR][dv]")>;
def : InstRW<[M3WriteNSHT1], (instregex "^S[RS]I[dv]")>;
def : InstRW<[M3WriteNSHT2], (instregex "^[SU]?SHLLv")>;
def : InstRW<[M3WriteNSHT3], (instregex "^(([SU]Q)?R)?SHRU?N[bhsv]")>;
def : InstRW<[M3WriteNSHT3], (instregex "^[SU]RSH[LR][dv]")>;
def : InstRW<[M3WriteNSHT3], (instregex "^[SU]QR?SHLU?[bdhsv]")>;

// ASIMD FP instructions.
def : InstRW<[M3WriteNSHF1],  (instregex "^FABSv")>;
def : InstRW<[M3WriteFADD2],  (instregex "^F(ABD|ADD|SUB)v")>;
def : InstRW<[M3WriteNEONA],  (instregex "^FADDP")>;
def : InstRW<[M3WriteNMSC1],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v[^1]")>;
def : InstRW<[M3WriteFCVT3],  (instregex "^FCVT(L|N|XN)v")>;
def : InstRW<[M3WriteFCVT2],  (instregex "^FCVT[AMNPZ][SU]v")>;
def : InstRW<[M3WriteFCVT2],  (instregex "^[SU]CVTFv")>;
def : InstRW<[M3WriteFDIV10], (instrs FDIVv2f32)>;
def : InstRW<[M3WriteNEONV],  (instrs FDIVv4f32)>;
def : InstRW<[M3WriteNEONW],  (instrs FDIVv2f64)>;
def : InstRW<[M3WriteNMSC1],  (instregex "^F(MAX|MIN)(NM)?v")>;
def : InstRW<[M3WriteNMSC2],  (instregex "^F(MAX|MIN)(NM)?Pv")>;
def : InstRW<[M3WriteNEONZ],  (instregex "^F(MAX|MIN)(NM)?Vv")>;
def : InstRW<[M3WriteFMAC3],  (instregex "^FMULX?v.[fi]")>;
def : InstRW<[M3WriteFMAC4,
              M3ReadFMAC],    (instregex "^FML[AS]v.f")>;
def : InstRW<[M3WriteFMAC5,
              M3ReadFMAC],    (instregex "^FML[AS]v.i")>;
def : InstRW<[M3WriteNALU1],  (instregex "^FNEGv")>;
def : InstRW<[M3WriteFCVT3A], (instregex "^FRINT[AIMNPXZ]v")>;
def : InstRW<[M3WriteFSQR17], (instrs FSQRTv2f32)>;
def : InstRW<[M3WriteNEONX],  (instrs FSQRTv4f32)>;
def : InstRW<[M3WriteNEONY],  (instrs FSQRTv2f64)>;

// ASIMD miscellaneous instructions.
def : InstRW<[M3WriteNALU1], (instregex "^RBITv")>;
def : InstRW<[M3WriteNALU1], (instregex "^(BIF|BIT|BSL|BSP)v")>;
def : InstRW<[M3WriteNEONB], (instregex "^DUPv.+gpr")>;
def : InstRW<[M3WriteNSHF1], (instregex "^DUPv.+lane")>;
def : InstRW<[M3WriteNSHF1], (instregex "^EXTv")>;
def : InstRW<[M3WriteNSHF1], (instregex "^[SU]?Q?XTU?Nv")>;
def : InstRW<[M3WriteNSHF1], (instregex "^DUP(i8|i16|i32|i64)$")>;
def : InstRW<[M3WriteNSHF1], (instregex "^INSv.+lane")>;
def : InstRW<[M3WriteMOVI],  (instregex "^MOVI")>;
def : InstRW<[M3WriteNALU1], (instregex "^FMOVv")>;
def : InstRW<[M3WriteFCVT4], (instregex "^[FU](RECP|RSQRT)Ev[248]")>;
def : InstRW<[M3WriteFMAC4,
              M3ReadFMAC],   (instregex "^F(RECP|RSQRT)Sv")>;
def : InstRW<[M3WriteNSHF1], (instregex "^REV(16|32|64)v")>;
def : InstRW<[M3WriteNSHF1], (instregex "^TB[LX]v")>;
def : InstRW<[M3WriteNEOND], (instregex "^[SU]MOVv")>;
def : InstRW<[M3WriteNSHF3], (instregex "^INSv.+gpr")>;
def : InstRW<[M3WriteNSHF1], (instregex "^(TRN|UZP|ZIP)[12]v")>;

// ASIMD load instructions.
def : InstRW<[M3WriteL5],   (instregex "LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteL5,
              M3WriteA1],   (instregex "LD1Onev(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteL5],   (instregex "LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteL5,
              M3WriteA1],   (instregex "LD1Onev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDA], (instregex "LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDA,
              M3WriteA1],   (instregex "LD1Twov(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDA], (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDA,
              M3WriteA1],   (instregex "LD1Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDB], (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDB,
              M3WriteA1],   (instregex "LD1Threev(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDB], (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDB,
              M3WriteA1],   (instregex "LD1Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDC], (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDC,
              M3WriteA1],   (instregex "LD1Fourv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDC], (instregex "LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDC,
              M3WriteA1],   (instregex "LD1Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDD], (instregex "LD1i(8|16|32)$")>;
def : InstRW<[M3WriteVLDD,
              M3WriteA1],   (instregex "LD1i(8|16|32)_POST")>;
def : InstRW<[M3WriteVLDE], (instregex "LD1i(64)$")>;
def : InstRW<[M3WriteVLDE,
              M3WriteA1],   (instregex "LD1i(64)_POST")>;

def : InstRW<[M3WriteL5],   (instregex "LD1Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteL5,
              M3WriteA1],   (instregex "LD1Rv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteL5],   (instregex "LD1Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteL5,
              M3WriteA1],   (instregex "LD1Rv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDF], (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[M3WriteVLDF,
              M3WriteA1],   (instregex "LD2Twov(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVLDF], (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDF,
              M3WriteA1],   (instregex "LD2Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDG], (instregex "LD2i(8|16|32)$")>;
def : InstRW<[M3WriteVLDG,
              M3WriteA1],   (instregex "LD2i(8|16|32)_POST")>;
def : InstRW<[M3WriteVLDH], (instregex "LD2i(64)$")>;
def : InstRW<[M3WriteVLDH,
              M3WriteA1],   (instregex "LD2i(64)_POST")>;

def : InstRW<[M3WriteVLDA], (instregex "LD2Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDA,
              M3WriteA1],   (instregex "LD2Rv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDA], (instregex "LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDA,
              M3WriteA1],   (instregex "LD2Rv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDI], (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[M3WriteVLDI,
              M3WriteA1],   (instregex "LD3Threev(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVLDI], (instregex "LD3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDI,
              M3WriteA1],   (instregex "LD3Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDJ], (instregex "LD3i(8|16|32)$")>;
def : InstRW<[M3WriteVLDJ,
              M3WriteA1],   (instregex "LD3i(8|16|32)_POST")>;
def : InstRW<[M3WriteVLDL], (instregex "LD3i(64)$")>;
def : InstRW<[M3WriteVLDL,
              M3WriteA1],   (instregex "LD3i(64)_POST")>;

def : InstRW<[M3WriteVLDB], (instregex "LD3Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDB,
              M3WriteA1],   (instregex "LD3Rv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDB], (instregex "LD3Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDB,
              M3WriteA1],   (instregex "LD3Rv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDN], (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[M3WriteVLDN,
              M3WriteA1],   (instregex "LD4Fourv(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVLDN], (instregex "LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDN,
              M3WriteA1],   (instregex "LD4Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVLDK], (instregex "LD4i(8|16|32)$")>;
def : InstRW<[M3WriteVLDK,
              M3WriteA1],   (instregex "LD4i(8|16|32)_POST")>;
def : InstRW<[M3WriteVLDM], (instregex "LD4i(64)$")>;
def : InstRW<[M3WriteVLDM,
              M3WriteA1],   (instregex "LD4i(64)_POST")>;

def : InstRW<[M3WriteVLDC], (instregex "LD4Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVLDC,
              M3WriteA1],   (instregex "LD4Rv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVLDC], (instregex "LD4Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVLDC,
              M3WriteA1],   (instregex "LD4Rv(16b|8h|4s|2d)_POST")>;

// ASIMD store instructions.
def : InstRW<[WriteVST],    (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "ST1Onev(8b|4h|2s|1d)_POST")>;
def : InstRW<[WriteVST],    (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "ST1Onev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTA], (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVSTA,
              WriteAdr],    (instregex "ST1Twov(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVSTA], (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTA,
              WriteAdr],    (instregex "ST1Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTB], (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVSTB,
              WriteAdr],    (instregex "ST1Threev(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVSTB], (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTB,
              WriteAdr],    (instregex "ST1Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTC], (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M3WriteVSTC,
              WriteAdr],    (instregex "ST1Fourv(8b|4h|2s|1d)_POST")>;
def : InstRW<[M3WriteVSTC], (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTC,
              WriteAdr],    (instregex "ST1Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTD], (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[M3WriteVSTD,
              WriteAdr],    (instregex "ST1i(8|16|32|64)_POST")>;

def : InstRW<[M3WriteVSTD], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[M3WriteVSTD,
              WriteAdr],    (instregex "ST2Twov(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVSTE], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTE,
              WriteAdr],    (instregex "ST2Twov(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTD], (instregex "ST2i(8|16|32)$")>;
def : InstRW<[M3WriteVSTD,
              WriteAdr],    (instregex "ST2i(8|16|32)_POST")>;
def : InstRW<[M3WriteVSTD], (instregex "ST2i(64)$")>;
def : InstRW<[M3WriteVSTD,
              WriteAdr],    (instregex "ST2i(64)_POST")>;

def : InstRW<[M3WriteVSTF], (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[M3WriteVSTF,
              WriteAdr],    (instregex "ST3Threev(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVSTG], (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTG,
              WriteAdr],    (instregex "ST3Threev(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTH], (instregex "ST3i(8|16|32)$")>;
def : InstRW<[M3WriteVSTH,
              WriteAdr],    (instregex "ST3i(8|16|32)_POST")>;
def : InstRW<[M3WriteVSTF], (instregex "ST3i(64)$")>;
def : InstRW<[M3WriteVSTF,
              WriteAdr],    (instregex "ST3i(64)_POST")>;

def : InstRW<[M3WriteVSTF], (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[M3WriteVSTF,
              WriteAdr],    (instregex "ST4Fourv(8b|4h|2s)_POST")>;
def : InstRW<[M3WriteVSTI], (instregex "ST4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M3WriteVSTI,
              WriteAdr],    (instregex "ST4Fourv(16b|8h|4s|2d)_POST")>;

def : InstRW<[M3WriteVSTF], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[M3WriteVSTF,
              WriteAdr],    (instregex "ST4i(8|16|32|64)_POST")>;

// Cryptography instructions.
def : InstRW<[M3WriteAES],    (instregex "^AES[DE]")>;
def : InstRW<[M3WriteAES,
              M3ReadAES],     (instregex "^AESI?MC")>;

def : InstRW<[M3WriteNCRY3A], (instregex "^PMULL?v")>;

def : InstRW<[M3WriteNCRY1A], (instregex "^SHA1([CHMP]|SU[01])")>;
def : InstRW<[M3WriteNCRY1A], (instregex "^SHA256SU0")>;
def : InstRW<[M3WriteNCRY5A], (instregex "^SHA256(H2?|SU1)")>;

// CRC instructions.
def : InstRW<[M3WriteC2], (instregex "^CRC32")>;

} // SchedModel = ExynosM3Model
