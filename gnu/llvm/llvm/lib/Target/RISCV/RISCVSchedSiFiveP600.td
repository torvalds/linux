//==- RISCVSchedSiFiveP600.td - SiFiveP600 Scheduling Defs ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

/// c is true if mx has the worst case behavior compared to LMULs in MxList.
/// On the SiFiveP600, the worst case LMUL is the Largest LMUL
/// and the worst case sew is the smallest SEW for that LMUL.
class SiFiveP600IsWorstCaseMX<string mx, list<string> MxList> {
  string LLMUL = LargestLMUL<MxList>.r;
  bit c = !eq(mx, LLMUL);
}

class SiFiveP600IsWorstCaseMXSEW<string mx, int sew, list<string> MxList, bit isF = 0> {
  string LLMUL = LargestLMUL<MxList>.r;
  int SSEW = SmallestSEW<mx, isF>.r;
  bit c = !and(!eq(mx, LLMUL), !eq(sew, SSEW));
}

// 1 Micro-Op per cycle.
class SiFiveP600GetLMulCycles<string mx> {
  int c = !cond(
    !eq(mx, "M1") : 1,
    !eq(mx, "M2") : 2,
    !eq(mx, "M4") : 4,
    !eq(mx, "M8") : 8,
    !eq(mx, "MF2") : 1,
    !eq(mx, "MF4") : 1,
    !eq(mx, "MF8") : 1
  );
}

// Latency for segmented loads and stores are calculated as vl * nf.
class SiFiveP600GetCyclesSegmented<string mx, int sew, int nf> {
  defvar VLEN = 128;
  defvar VLUpperBound = !cond(
    !eq(mx, "M1") : !div(VLEN, sew),
    !eq(mx, "M2") : !div(!mul(VLEN, 2), sew),
    !eq(mx, "M4") : !div(!mul(VLEN, 4), sew),
    !eq(mx, "M8") : !div(!mul(VLEN, 8), sew),
    !eq(mx, "MF2") : !div(!div(VLEN, 2), sew),
    !eq(mx, "MF4") : !div(!div(VLEN, 4), sew),
    !eq(mx, "MF8") : !div(!div(VLEN, 8), sew),
  );
  int c = !mul(VLUpperBound, nf);
}

// SiFiveP600 machine model for scheduling and other instruction cost heuristics.
def SiFiveP600Model : SchedMachineModel {
  let IssueWidth = 4;         // 4 micro-ops are dispatched per cycle.
  let MicroOpBufferSize = 160; // Max micro-ops that can be buffered.
  let LoadLatency = 4;        // Cycles for loads to access the cache.
  let MispredictPenalty = 9;  // Extra cycles for a mispredicted branch.
  let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                             HasStdExtZknd, HasStdExtZkne, HasStdExtZknh,
                             HasStdExtZksed, HasStdExtZksh, HasStdExtZkr,
                             HasVendorXSfvqmaccqoq];
  let CompleteModel = false;
}

let SchedModel = SiFiveP600Model in {

def SiFiveP600IEXQ0       : ProcResource<1>;
def SiFiveP600IEXQ1       : ProcResource<1>;
def SiFiveP600IEXQ2       : ProcResource<1>;
def SiFiveP600IEXQ3       : ProcResource<1>;
def SiFiveP600FEXQ0       : ProcResource<1>;
def SiFiveP600FEXQ1       : ProcResource<1>;

// Two Load/Store ports that can issue either two loads, two stores, or one load
// and one store (P550 has one load and one separate store pipe).
def SiFiveP600LDST       : ProcResource<2>;

// 4-wide pipeline with 4 ALU pipes.
def SiFiveP600IntArith    : ProcResGroup<[SiFiveP600IEXQ0, SiFiveP600IEXQ1, SiFiveP600IEXQ2, SiFiveP600IEXQ3]>;
defvar SiFiveP600SYS      = SiFiveP600IEXQ0;
defvar SiFiveP600CMOV     = SiFiveP600IEXQ0;
defvar SiFiveP600MulI2F   = SiFiveP600IEXQ1;
def SiFiveP600Branch      : ProcResGroup<[SiFiveP600IEXQ2, SiFiveP600IEXQ3]>;
def SiFiveP600Div         : ProcResource<1>;

def SiFiveP600FloatArith  : ProcResGroup<[SiFiveP600FEXQ0, SiFiveP600FEXQ1]>;
defvar SiFiveP600F2I      = SiFiveP600FEXQ0;
def SiFiveP600FloatDiv    : ProcResource<1>;

// Vector pipeline
// VEXQ0 handle Mask, Simple Slide instructions,
// VEXQ1 handle Complex Slide, Permutation, Reductions, Divide instructions.
// Other vector instructions can be done in VEXQ0 and VEXQ1.
def SiFiveP600VEXQ0        : ProcResource<1>;
def SiFiveP600VEXQ1        : ProcResource<1>;
def SiFiveP600VectorArith  : ProcResGroup<[SiFiveP600VEXQ0, SiFiveP600VEXQ1]>;
def SiFiveP600VLD          : ProcResource<1>;
def SiFiveP600VST          : ProcResource<1>;
def SiFiveP600VDiv         : ProcResource<1>;
def SiFiveP600VFloatDiv    : ProcResource<1>;

// Integer arithmetic and logic
def : WriteRes<WriteIALU, [SiFiveP600IntArith]>;
def : WriteRes<WriteIALU32, [SiFiveP600IntArith]>;
def : WriteRes<WriteShiftImm, [SiFiveP600IntArith]>;
def : WriteRes<WriteShiftImm32, [SiFiveP600IntArith]>;
def : WriteRes<WriteShiftReg, [SiFiveP600IntArith]>;
def : WriteRes<WriteShiftReg32, [SiFiveP600IntArith]>;
// Branching
def : WriteRes<WriteJmp, [SiFiveP600Branch]>;
def : WriteRes<WriteJal, [SiFiveP600Branch]>;
def : WriteRes<WriteJalr, [SiFiveP600Branch]>;

// CMOV
def P600WriteCMOV : SchedWriteRes<[SiFiveP600Branch, SiFiveP600CMOV]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[P600WriteCMOV], (instrs PseudoCCMOVGPRNoX0)>;

let Latency = 3 in {
// Integer multiplication
def : WriteRes<WriteIMul, [SiFiveP600MulI2F]>;
def : WriteRes<WriteIMul32, [SiFiveP600MulI2F]>;
// cpop[w] look exactly like multiply.
def : WriteRes<WriteCPOP, [SiFiveP600MulI2F]>;
def : WriteRes<WriteCPOP32, [SiFiveP600MulI2F]>;
}

// Integer division
def : WriteRes<WriteIDiv, [SiFiveP600MulI2F, SiFiveP600Div]> {
  let Latency = 35;
  let ReleaseAtCycles = [1, 34];
}
def : WriteRes<WriteIDiv32,  [SiFiveP600MulI2F, SiFiveP600Div]> {
  let Latency = 20;
  let ReleaseAtCycles = [1, 19];
}

// Integer remainder
def : WriteRes<WriteIRem, [SiFiveP600MulI2F, SiFiveP600Div]> {
  let Latency = 35;
  let ReleaseAtCycles = [1, 34];
}
def : WriteRes<WriteIRem32, [SiFiveP600MulI2F, SiFiveP600Div]> {
  let Latency = 20;
  let ReleaseAtCycles = [1, 19];
}

// Bitmanip
def : WriteRes<WriteRotateImm, [SiFiveP600IntArith]>;
def : WriteRes<WriteRotateImm32, [SiFiveP600IntArith]>;
def : WriteRes<WriteRotateReg, [SiFiveP600IntArith]>;
def : WriteRes<WriteRotateReg32, [SiFiveP600IntArith]>;

def : WriteRes<WriteCLZ, [SiFiveP600IntArith]>;
def : WriteRes<WriteCLZ32, [SiFiveP600IntArith]>;
def : WriteRes<WriteCTZ, [SiFiveP600IntArith]>;
def : WriteRes<WriteCTZ32, [SiFiveP600IntArith]>;

def : WriteRes<WriteORCB, [SiFiveP600IntArith]>;
def : WriteRes<WriteIMinMax, [SiFiveP600IntArith]>;

def : WriteRes<WriteREV8, [SiFiveP600IntArith]>;

def : WriteRes<WriteSHXADD, [SiFiveP600IntArith]>;
def : WriteRes<WriteSHXADD32, [SiFiveP600IntArith]>;

def : WriteRes<WriteSingleBit, [SiFiveP600IntArith]>;
def : WriteRes<WriteSingleBitImm, [SiFiveP600IntArith]>;
def : WriteRes<WriteBEXT, [SiFiveP600IntArith]>;
def : WriteRes<WriteBEXTI, [SiFiveP600IntArith]>;

// Memory
def : WriteRes<WriteSTB, [SiFiveP600LDST]>;
def : WriteRes<WriteSTH, [SiFiveP600LDST]>;
def : WriteRes<WriteSTW, [SiFiveP600LDST]>;
def : WriteRes<WriteSTD, [SiFiveP600LDST]>;
def : WriteRes<WriteFST16, [SiFiveP600LDST]>;
def : WriteRes<WriteFST32, [SiFiveP600LDST]>;
def : WriteRes<WriteFST64, [SiFiveP600LDST]>;

let Latency = 4 in {
def : WriteRes<WriteLDB, [SiFiveP600LDST]>;
def : WriteRes<WriteLDH, [SiFiveP600LDST]>;
}
let Latency = 4 in {
def : WriteRes<WriteLDW, [SiFiveP600LDST]>;
def : WriteRes<WriteLDD, [SiFiveP600LDST]>;
}

let Latency = 5 in {
def : WriteRes<WriteFLD16, [SiFiveP600LDST]>;
def : WriteRes<WriteFLD32, [SiFiveP600LDST]>;
def : WriteRes<WriteFLD64, [SiFiveP600LDST]>;
}

// Atomic memory
let Latency = 3 in {
def : WriteRes<WriteAtomicSTW, [SiFiveP600LDST]>;
def : WriteRes<WriteAtomicSTD, [SiFiveP600LDST]>;
def : WriteRes<WriteAtomicW, [SiFiveP600LDST]>;
def : WriteRes<WriteAtomicD, [SiFiveP600LDST]>;
def : WriteRes<WriteAtomicLDW, [SiFiveP600LDST]>;
def : WriteRes<WriteAtomicLDD, [SiFiveP600LDST]>;
}

// Floating point
let Latency = 2 in {
def : WriteRes<WriteFAdd16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFAdd32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFAdd64, [SiFiveP600FloatArith]>;
}
let Latency = 3 in {
def : WriteRes<WriteFMul16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMul32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMul64, [SiFiveP600FloatArith]>;
}
let Latency = 4 in {
def : WriteRes<WriteFMA16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMA32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMA64, [SiFiveP600FloatArith]>;
}

let Latency = 2 in {
def : WriteRes<WriteFSGNJ16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFSGNJ32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFSGNJ64, [SiFiveP600FloatArith]>;

def : WriteRes<WriteFMinMax16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMinMax32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFMinMax64, [SiFiveP600FloatArith]>;
}

// Half precision.
def : WriteRes<WriteFDiv16, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 4;
  let ReleaseAtCycles = [1, 4];
}
def : WriteRes<WriteFSqrt16, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 18;
  let ReleaseAtCycles = [1, 17];
}

// Single precision.
def : WriteRes<WriteFDiv32, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 6;
  let ReleaseAtCycles = [1, 6];
}
def : WriteRes<WriteFSqrt32, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 18;
  let ReleaseAtCycles = [1, 17];
}

// Double precision
def : WriteRes<WriteFDiv64, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 11;
  let ReleaseAtCycles = [1, 11];
}
def : WriteRes<WriteFSqrt64, [SiFiveP600FEXQ1, SiFiveP600FloatDiv]> {
  let Latency = 33;
  let ReleaseAtCycles = [1, 32];
}

// Conversions
let Latency = 2 in {
def : WriteRes<WriteFCvtI32ToF16, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtI32ToF32, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtI32ToF64, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtI64ToF16, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtI64ToF32, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtI64ToF64, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFCvtF16ToI32, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF16ToI64, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF16ToF32, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFCvtF16ToF64, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFCvtF32ToI32, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF32ToI64, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF32ToF16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFCvtF32ToF64, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFCvtF64ToI32, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF64ToI64, [SiFiveP600F2I]>;
def : WriteRes<WriteFCvtF64ToF16, [SiFiveP600FloatArith]>;
def : WriteRes<WriteFCvtF64ToF32, [SiFiveP600FloatArith]>;

def : WriteRes<WriteFClass16, [SiFiveP600F2I]>;
def : WriteRes<WriteFClass32, [SiFiveP600F2I]>;
def : WriteRes<WriteFClass64, [SiFiveP600F2I]>;
def : WriteRes<WriteFCmp16, [SiFiveP600F2I]>;
def : WriteRes<WriteFCmp32, [SiFiveP600F2I]>;
def : WriteRes<WriteFCmp64, [SiFiveP600F2I]>;
def : WriteRes<WriteFMovI16ToF16, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFMovF16ToI16, [SiFiveP600F2I]>;
def : WriteRes<WriteFMovI32ToF32, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFMovF32ToI32, [SiFiveP600F2I]>;
def : WriteRes<WriteFMovI64ToF64, [SiFiveP600MulI2F]>;
def : WriteRes<WriteFMovF64ToI64, [SiFiveP600F2I]>;
}

// 6. Configuration-Setting Instructions
def : WriteRes<WriteVSETVLI, [SiFiveP600SYS]>;
def : WriteRes<WriteVSETIVLI, [SiFiveP600SYS]>;
def : WriteRes<WriteVSETVL, [SiFiveP600SYS]>;

// 7. Vector Loads and Stores
// FIXME: This unit is still being improved, currently
// it is based on stage numbers. Estimates are optimistic,
// latency may be longer.
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 8, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVLDE",    [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDM",    [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDFF",   [SiFiveP600VLD], mx, IsWorstCase>;
  }
  let Latency = 12, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVLDS8",   [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDS16",  [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDS32",  [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDS64",  [SiFiveP600VLD], mx, IsWorstCase>;
  }
  let Latency = 12, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVLDUX8",  [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDUX16", [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDUX32", [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDUX64", [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX8",  [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX16", [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX32", [SiFiveP600VLD], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVLDOX64", [SiFiveP600VLD], mx, IsWorstCase>;
  }
}

foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 8, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSTE",    [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTM",    [SiFiveP600VST], mx, IsWorstCase>;
  }
  let Latency = 12, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSTS8",   [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTS16",  [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTS32",  [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTS64",  [SiFiveP600VST], mx, IsWorstCase>;
  }
  let Latency = 12, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSTUX8",  [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX16", [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX32", [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTUX64", [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX8",  [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX16", [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX32", [SiFiveP600VST], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSTOX64", [SiFiveP600VST], mx, IsWorstCase>;
  }
}

foreach mx = SchedMxList in {
  foreach nf=2-8 in {
    foreach eew = [8, 16, 32, 64] in {
      defvar LMulLat = SiFiveP600GetCyclesSegmented<mx, eew, nf>.c;
      defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
      let Latency = !add(12, LMulLat), ReleaseAtCycles = [!add(12, LMulLat)] in {
        defm "" : LMULWriteResMX<"WriteVLSEG" # nf # "e" # eew,   [SiFiveP600VLD], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLSEGFF" # nf # "e" # eew, [SiFiveP600VLD], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLSSEG" # nf # "e" # eew,  [SiFiveP600VLD], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLUXSEG" # nf # "e" # eew, [SiFiveP600VLD], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVLOXSEG" # nf # "e" # eew, [SiFiveP600VLD], mx, IsWorstCase>;
      }
      let Latency = !add(1, LMulLat), ReleaseAtCycles = [!add(12, LMulLat)] in {
        defm "" : LMULWriteResMX<"WriteVSSEG" # nf # "e" # eew,   [SiFiveP600VST], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVSSSEG" # nf # "e" # eew,  [SiFiveP600VST], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVSUXSEG" # nf # "e" # eew, [SiFiveP600VST], mx, IsWorstCase>;
        defm "" : LMULWriteResMX<"WriteVSOXSEG" # nf # "e" # eew, [SiFiveP600VST], mx, IsWorstCase>;
      }
    }
  }
}

// Whole register move/load/store
foreach LMul = [1, 2, 4, 8] in {
  let Latency = 8, ReleaseAtCycles = [LMul] in {
    def : WriteRes<!cast<SchedWrite>("WriteVLD" # LMul # "R"), [SiFiveP600VLD]>;
    def : WriteRes<!cast<SchedWrite>("WriteVST" # LMul # "R"), [SiFiveP600VST]>;
  }
  let Latency = LMul, ReleaseAtCycles = [LMul] in {
    def : WriteRes<!cast<SchedWrite>("WriteVMov" # LMul # "V"), [SiFiveP600VectorArith]>;
  }
}

// 11. Vector Integer Arithmetic Instructions
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 1, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVIALUV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIALUX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIALUI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVExtV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUX",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICALUI",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICmpV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICmpX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVICmpI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeX", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMergeI", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMovI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  let Latency = 6, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVShiftV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVShiftX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVShiftI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMinMaxV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMinMaxX", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulX",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulAddV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIMulAddX", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}
// Widening
foreach mx = SchedMxListW in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 6, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVIWALUV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWALUX",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWALUI",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulX",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulAddV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIWMulAddX", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}

// Worst case needs 64 cycles if SEW is equal to 64.
foreach mx = SchedMxList in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = 64, ReleaseAtCycles = [LMulLat, !mul(63, LMulLat)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVIDivV", [SiFiveP600VEXQ1, SiFiveP600VDiv], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVIDivX", [SiFiveP600VEXQ1, SiFiveP600VDiv], mx, sew, IsWorstCase>;
    }
  }
}

// Narrowing Shift and Clips
foreach mx = SchedMxListW in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVNShiftV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNShiftX", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNShiftI", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNClipV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNClipX",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVNClipI",  [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}

// 12. Vector Fixed-Point Arithmetic Instructions
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 6, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSALUV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSALUX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSALUI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAALUV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAALUX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSMulV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSMulX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftX", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSShiftI", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}

// 13. Vector Floating-Point Instructions
foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, isF=1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListF, isF=1>.c;
    let Latency = 6, ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFALUV",  [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFALUF",  [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulV",  [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulF",  [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulAddV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMulAddF", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
    }
    let Latency = 2, ReleaseAtCycles = [LMulLat] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFRecpV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
    let Latency = 3, ReleaseAtCycles = [LMulLat] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFCvtIToFV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
  }
}
foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, isF=1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxList, isF=1>.c;
    let Latency = 1, ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMinMaxV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFMinMaxF", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSgnjV",   [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSgnjF",   [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
    }
  }
}
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 3, ReleaseAtCycles = [LMulLat] in
  defm "" : LMULWriteResMX<"WriteVFCvtFToIV", [SiFiveP600VectorArith], mx, IsWorstCase>;
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVFCmpV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFCmpF",  [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  let Latency = 1, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVFClassV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFMergeV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFMovV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}

// Widening
foreach mx = SchedMxListW in {
  foreach sew = SchedSEWSet<mx, isF=0, isWidening=1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListW>.c;
    let Latency = 3, ReleaseAtCycles = [LMulLat] in
    defm "" : LMULSEWWriteResMXSEW<"WriteVFWCvtIToFV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
  }
}
foreach mx = SchedMxListFW in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxListFW>.c;
  let Latency = 6, ReleaseAtCycles = [LMulLat] in
  defm "" : LMULWriteResMX<"WriteVFWCvtFToIV", [SiFiveP600VectorArith], mx, IsWorstCase>;
}
foreach mx = SchedMxListFW in {
  foreach sew = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListFW, isF=1>.c;
    let Latency = 6, ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWALUV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWALUF", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulF", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulAddV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWMulAddF", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWCvtFToFV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
    }
  }
}
// Narrowing
foreach mx = SchedMxListW in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxListW>.c;
  let Latency = 3, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVFNCvtFToIV", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
}
foreach mx = SchedMxListFW in {
  foreach sew = SchedSEWSet<mx, isF=1, isWidening=1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListFW, isF=1>.c;
    let Latency = 3, ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFNCvtIToFV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFNCvtFToFV", [SiFiveP600VectorArith], mx, sew, IsWorstCase>;
    }
  }
}

// Worst case needs 76 cycles if SEW is equal to 64.
foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, 1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListF, 1>.c;
    let Latency = 76, ReleaseAtCycles = [LMulLat, !mul(76, LMulLat)] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFDivV",  [SiFiveP600VEXQ1, SiFiveP600VFloatDiv], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFDivF",  [SiFiveP600VEXQ1, SiFiveP600VFloatDiv], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFSqrtV", [SiFiveP600VEXQ1, SiFiveP600VFloatDiv], mx, sew, IsWorstCase>;
    }
  }
}

// 14. Vector Reduction Operations
foreach mx = SchedMxList in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = !add(2, !mul(2, LMulLat)), ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVIRedV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVIRedMinMaxV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
    }
  }
}

foreach mx = SchedMxListWRed in {
  foreach sew = SchedSEWSet<mx, 0, 1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListWRed>.c;
    let Latency = !add(2, !mul(2, LMulLat)), ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVIWRedV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
    }
  }
}

foreach mx = SchedMxListF in {
  foreach sew = SchedSEWSet<mx, 1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListF, 1>.c;
    let Latency = !add(6, !mul(6, LMulLat)), ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRedV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRedMinMaxV_From",
                                     [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFRedOV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
    }
  }
}

foreach mx = SchedMxListFWRed in {
  foreach sew = SchedSEWSet<mx, 1, 1>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxListFWRed, 1>.c;
    let Latency = !add(6, !mul(6, LMulLat)), ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWRedV_From",  [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVFWRedOV_From", [SiFiveP600VEXQ1],
                                     mx, sew, IsWorstCase>;
    }
  }
}

// 15. Vector Mask Instructions
foreach mx = SchedMxList in {
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 1, ReleaseAtCycles = [1] in {
    defm "" : LMULWriteResMX<"WriteVMALUV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMPopV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMFFSV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVMSFSV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  let Latency = 1, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVIotaV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVIdxV", [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
}

// 16. Vector Permutation Instructions
// Simple Slide
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSlideI",  [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
  let Latency = 1, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVISlide1X", [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVFSlide1F", [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
}
foreach mx = ["MF8", "MF4", "MF2", "M1"] in {
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 2, ReleaseAtCycles = [1] in {
    defm "" : LMULWriteResMX<"WriteVSlideUpX",   [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSlideDownX", [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
}

// Complex Slide
foreach mx = ["M8", "M4", "M2"] in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = !add(4, LMulLat), ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSlideUpX",   [SiFiveP600VEXQ1], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSlideDownX", [SiFiveP600VEXQ1], mx, IsWorstCase>;
  }
}

let Latency = 2, ReleaseAtCycles = [1] in {
  def : WriteRes<WriteVMovSX, [SiFiveP600VectorArith]>;
  def : WriteRes<WriteVMovXS, [SiFiveP600VectorArith]>;
}
let Latency = 6, ReleaseAtCycles = [1] in {
  def : WriteRes<WriteVMovSF, [SiFiveP600VectorArith]>;
  def : WriteRes<WriteVMovFS, [SiFiveP600VectorArith]>;
}

// Simple Gather and Compress
foreach mx = ["MF8", "MF4", "MF2", "M1"] in {
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 3, ReleaseAtCycles = [1] in {
    defm "" : LMULWriteResMX<"WriteVRGatherVX", [SiFiveP600VEXQ1], mx, IsWorstCase>;
  }
}

foreach mx = ["MF8", "MF4", "MF2", "M1"] in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
    let Latency = 3, ReleaseAtCycles = [1] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherVV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherEI16VV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVCompressV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
    }
  }
}

// Complex Gather and Compress
foreach mx = ["M2", "M4", "M8"] in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 6, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVRGatherVX", [SiFiveP600VEXQ1], mx, IsWorstCase>;
  }
}

foreach mx = ["M2", "M4", "M8"] in {
  foreach sew = SchedSEWSet<mx>.val in {
    defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
    defvar IsWorstCase = SiFiveP600IsWorstCaseMXSEW<mx, sew, SchedMxList>.c;
    let Latency = 6, ReleaseAtCycles = [LMulLat] in {
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherVV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVRGatherEI16VV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
      defm "" : LMULSEWWriteResMXSEW<"WriteVCompressV", [SiFiveP600VEXQ1], mx, sew, IsWorstCase>;
    }
  }
}

// Simple Vrgather.vi
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  let Latency = 3, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVRGatherVI", [SiFiveP600VEXQ1], mx, IsWorstCase>;
  }
}

// Vector Crypto
foreach mx = SchedMxList in {
  defvar LMulLat = SiFiveP600GetLMulCycles<mx>.c;
  defvar IsWorstCase = SiFiveP600IsWorstCaseMX<mx, SchedMxList>.c;
  // Zvbb
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVBREVV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVCLZV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVCPOPV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVCTZV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVWSLLV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVWSLLX",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVWSLLI",   [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  // Zvbc
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVCLMULV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVCLMULX", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  // Zvkb
  // VANDN uses WriteVIALU[V|X|I]
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVBREV8V",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVREV8V",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVRotV",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVRotX",    [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVRotI",    [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  // Zvkg
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVGHSHV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVGMULV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  // ZvknhaOrZvknhb
  let Latency = 3, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSHA2CHV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSHA2CLV", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSHA2MSV", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  // Zvkned
  let Latency = 2, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVAESMVV",  [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAESKF1V", [SiFiveP600VectorArith], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVAESKF2V", [SiFiveP600VectorArith], mx, IsWorstCase>;
  }
  let Latency = 1, ReleaseAtCycles = [LMulLat] in
  defm "" : LMULWriteResMX<"WriteVAESZV",   [SiFiveP600VectorArith], mx, IsWorstCase>;
  // Zvksed
  let Latency = 3, ReleaseAtCycles = [LMulLat] in {
    defm "" : LMULWriteResMX<"WriteVSM4KV",   [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSM4RV",   [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSM3CV",   [SiFiveP600VEXQ0], mx, IsWorstCase>;
    defm "" : LMULWriteResMX<"WriteVSM3MEV",  [SiFiveP600VEXQ0], mx, IsWorstCase>;
  }
}

// Others
def : WriteRes<WriteCSR, [SiFiveP600SYS]>;
def : WriteRes<WriteNop, []>;
def : WriteRes<WriteRdVLENB, [SiFiveP600SYS]>;

// FIXME: This could be better modeled by looking at the regclasses of the operands.
def : InstRW<[WriteIALU, ReadIALU], (instrs COPY)>;

//===----------------------------------------------------------------------===//
// Bypass and advance
def : ReadAdvance<ReadJmp, 0>;
def : ReadAdvance<ReadJalr, 0>;
def : ReadAdvance<ReadCSR, 0>;
def : ReadAdvance<ReadStoreData, 0>;
def : ReadAdvance<ReadMemBase, 0>;
def : ReadAdvance<ReadIALU, 0>;
def : ReadAdvance<ReadIALU32, 0>;
def : ReadAdvance<ReadShiftImm, 0>;
def : ReadAdvance<ReadShiftImm32, 0>;
def : ReadAdvance<ReadShiftReg, 0>;
def : ReadAdvance<ReadShiftReg32, 0>;
def : ReadAdvance<ReadIDiv, 0>;
def : ReadAdvance<ReadIDiv32, 0>;
def : ReadAdvance<ReadIRem, 0>;
def : ReadAdvance<ReadIRem32, 0>;
def : ReadAdvance<ReadIMul, 0>;
def : ReadAdvance<ReadIMul32, 0>;
def : ReadAdvance<ReadAtomicWA, 0>;
def : ReadAdvance<ReadAtomicWD, 0>;
def : ReadAdvance<ReadAtomicDA, 0>;
def : ReadAdvance<ReadAtomicDD, 0>;
def : ReadAdvance<ReadAtomicLDW, 0>;
def : ReadAdvance<ReadAtomicLDD, 0>;
def : ReadAdvance<ReadAtomicSTW, 0>;
def : ReadAdvance<ReadAtomicSTD, 0>;
def : ReadAdvance<ReadFStoreData, 0>;
def : ReadAdvance<ReadFMemBase, 0>;
def : ReadAdvance<ReadFAdd16, 0>;
def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul16, 0>;
def : ReadAdvance<ReadFMA16, 0>;
def : ReadAdvance<ReadFMA16Addend, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 0>;
def : ReadAdvance<ReadFDiv16, 0>;
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt16, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp16, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ16, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax16, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF16ToI32, 0>;
def : ReadAdvance<ReadFCvtF16ToI64, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF16, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF16, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFCvtF16ToF32, 0>;
def : ReadAdvance<ReadFCvtF32ToF16, 0>;
def : ReadAdvance<ReadFCvtF16ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF16, 0>;
def : ReadAdvance<ReadFMovF16ToI16, 0>;
def : ReadAdvance<ReadFMovI16ToF16, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass16, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFClass64, 0>;

// Bitmanip
def : ReadAdvance<ReadRotateImm, 0>;
def : ReadAdvance<ReadRotateImm32, 0>;
def : ReadAdvance<ReadRotateReg, 0>;
def : ReadAdvance<ReadRotateReg32, 0>;
def : ReadAdvance<ReadCLZ, 0>;
def : ReadAdvance<ReadCLZ32, 0>;
def : ReadAdvance<ReadCTZ, 0>;
def : ReadAdvance<ReadCTZ32, 0>;
def : ReadAdvance<ReadCPOP, 0>;
def : ReadAdvance<ReadCPOP32, 0>;
def : ReadAdvance<ReadORCB, 0>;
def : ReadAdvance<ReadIMinMax, 0>;
def : ReadAdvance<ReadREV8, 0>;
def : ReadAdvance<ReadSHXADD, 0>;
def : ReadAdvance<ReadSHXADD32, 0>;
def : ReadAdvance<ReadSingleBit, 0>;
def : ReadAdvance<ReadSingleBitImm, 0>;

// 6. Configuration-Setting Instructions
def : ReadAdvance<ReadVSETVLI, 0>;
def : ReadAdvance<ReadVSETVL, 0>;

// 7. Vector Loads and Stores
def : ReadAdvance<ReadVLDX, 0>;
def : ReadAdvance<ReadVSTX, 0>;
defm "" : LMULReadAdvance<"ReadVSTEV", 0>;
defm "" : LMULReadAdvance<"ReadVSTM", 0>;
def : ReadAdvance<ReadVLDSX, 0>;
def : ReadAdvance<ReadVSTSX, 0>;
defm "" : LMULReadAdvance<"ReadVSTS8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTS64V", 0>;
defm "" : LMULReadAdvance<"ReadVLDUXV", 0>;
defm "" : LMULReadAdvance<"ReadVLDOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTUXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTUX64V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64", 0>;
defm "" : LMULReadAdvance<"ReadVSTOXV", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX8V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX16V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX32V", 0>;
defm "" : LMULReadAdvance<"ReadVSTOX64V", 0>;
// LMUL Aware
def : ReadAdvance<ReadVST1R, 0>;
def : ReadAdvance<ReadVST2R, 0>;
def : ReadAdvance<ReadVST4R, 0>;
def : ReadAdvance<ReadVST8R, 0>;

// 12. Vector Integer Arithmetic Instructions
defm : LMULReadAdvance<"ReadVIALUV", 0>;
defm : LMULReadAdvance<"ReadVIALUX", 0>;
defm : LMULReadAdvanceW<"ReadVIWALUV", 0>;
defm : LMULReadAdvanceW<"ReadVIWALUX", 0>;
defm : LMULReadAdvance<"ReadVExtV", 0>;
defm : LMULReadAdvance<"ReadVICALUV", 0>;
defm : LMULReadAdvance<"ReadVICALUX", 0>;
defm : LMULReadAdvance<"ReadVShiftV", 0>;
defm : LMULReadAdvance<"ReadVShiftX", 0>;
defm : LMULReadAdvanceW<"ReadVNShiftV", 0>;
defm : LMULReadAdvanceW<"ReadVNShiftX", 0>;
defm : LMULReadAdvance<"ReadVICmpV", 0>;
defm : LMULReadAdvance<"ReadVICmpX", 0>;
defm : LMULReadAdvance<"ReadVIMinMaxV", 0>;
defm : LMULReadAdvance<"ReadVIMinMaxX", 0>;
defm : LMULReadAdvance<"ReadVIMulV", 0>;
defm : LMULReadAdvance<"ReadVIMulX", 0>;
defm : LMULSEWReadAdvance<"ReadVIDivV", 0>;
defm : LMULSEWReadAdvance<"ReadVIDivX", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulV", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulX", 0>;
defm : LMULReadAdvance<"ReadVIMulAddV", 0>;
defm : LMULReadAdvance<"ReadVIMulAddX", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulAddV", 0>;
defm : LMULReadAdvanceW<"ReadVIWMulAddX", 0>;
defm : LMULReadAdvance<"ReadVIMergeV", 0>;
defm : LMULReadAdvance<"ReadVIMergeX", 0>;
defm : LMULReadAdvance<"ReadVIMovV", 0>;
defm : LMULReadAdvance<"ReadVIMovX", 0>;

// 13. Vector Fixed-Point Arithmetic Instructions
defm "" : LMULReadAdvance<"ReadVSALUV", 0>;
defm "" : LMULReadAdvance<"ReadVSALUX", 0>;
defm "" : LMULReadAdvance<"ReadVAALUV", 0>;
defm "" : LMULReadAdvance<"ReadVAALUX", 0>;
defm "" : LMULReadAdvance<"ReadVSMulV", 0>;
defm "" : LMULReadAdvance<"ReadVSMulX", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftV", 0>;
defm "" : LMULReadAdvance<"ReadVSShiftX", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipV", 0>;
defm "" : LMULReadAdvanceW<"ReadVNClipX", 0>;

// 14. Vector Floating-Point Instructions
defm "" : LMULSEWReadAdvanceF<"ReadVFALUV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFALUF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWALUF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFDivF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMulAddF", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWMulAddF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSqrtV", 0>;
defm "" : LMULSEWReadAdvance<"ReadVFRecpV", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpV", 0>;
defm "" : LMULReadAdvance<"ReadVFCmpF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFMinMaxF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjV", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFSgnjF", 0>;
defm "" : LMULReadAdvance<"ReadVFClassV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeV", 0>;
defm "" : LMULReadAdvance<"ReadVFMergeF", 0>;
defm "" : LMULReadAdvance<"ReadVFMovF", 0>;
defm "" : LMULSEWReadAdvanceF<"ReadVFCvtIToFV", 0>;
defm "" : LMULReadAdvance<"ReadVFCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceW<"ReadVFWCvtIToFV", 0>;
defm "" : LMULReadAdvanceFW<"ReadVFWCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFWCvtFToFV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtIToFV", 0>;
defm "" : LMULReadAdvanceW<"ReadVFNCvtFToIV", 0>;
defm "" : LMULSEWReadAdvanceFW<"ReadVFNCvtFToFV", 0>;

// 15. Vector Reduction Operations
def : ReadAdvance<ReadVIRedV, 0>;
def : ReadAdvance<ReadVIRedV0, 0>;
def : ReadAdvance<ReadVIWRedV, 0>;
def : ReadAdvance<ReadVIWRedV0, 0>;
def : ReadAdvance<ReadVFRedV, 0>;
def : ReadAdvance<ReadVFRedV0, 0>;
def : ReadAdvance<ReadVFRedOV, 0>;
def : ReadAdvance<ReadVFRedOV0, 0>;
def : ReadAdvance<ReadVFWRedV, 0>;
def : ReadAdvance<ReadVFWRedV0, 0>;
def : ReadAdvance<ReadVFWRedOV, 0>;
def : ReadAdvance<ReadVFWRedOV0, 0>;

// 16. Vector Mask Instructions
defm "" : LMULReadAdvance<"ReadVMALUV", 0>;
defm "" : LMULReadAdvance<"ReadVMPopV", 0>;
defm "" : LMULReadAdvance<"ReadVMFFSV", 0>;
defm "" : LMULReadAdvance<"ReadVMSFSV", 0>;
defm "" : LMULReadAdvance<"ReadVIotaV", 0>;

// 17. Vector Permutation Instructions
def : ReadAdvance<ReadVMovXS, 0>;
def : ReadAdvance<ReadVMovSX_V, 0>;
def : ReadAdvance<ReadVMovSX_X, 0>;
def : ReadAdvance<ReadVMovFS, 0>;
def : ReadAdvance<ReadVMovSF_V, 0>;
def : ReadAdvance<ReadVMovSF_F, 0>;
defm "" : LMULReadAdvance<"ReadVISlideV", 0>;
defm "" : LMULReadAdvance<"ReadVISlideX", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideV", 0>;
defm "" : LMULReadAdvance<"ReadVFSlideF", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherVV_index", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVRGatherEI16VV_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_data", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVX_index", 0>;
defm "" : LMULReadAdvance<"ReadVRGatherVI_data", 0>;
defm "" : LMULSEWReadAdvance<"ReadVCompressV", 0>;
// LMUL Aware
def : ReadAdvance<ReadVMov1V, 0>;
def : ReadAdvance<ReadVMov2V, 0>;
def : ReadAdvance<ReadVMov4V, 0>;
def : ReadAdvance<ReadVMov8V, 0>;

// Others
def : ReadAdvance<ReadVMask, 0>;
def : ReadAdvance<ReadVMergeOp_WorstCase, 0>;
foreach mx = SchedMxList in {
  def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx), 0>;
  foreach sew = SchedSEWSet<mx>.val in
    def : ReadAdvance<!cast<SchedRead>("ReadVMergeOp_" # mx  # "_E" # sew), 0>;
}

// Vector Crypto Extensions
// Zvbb
defm "" : LMULReadAdvance<"ReadVBREVV", 0>;
defm "" : LMULReadAdvance<"ReadVCLZV", 0>;
defm "" : LMULReadAdvance<"ReadVCPOPV", 0>;
defm "" : LMULReadAdvance<"ReadVCTZV", 0>;
defm "" : LMULReadAdvance<"ReadVWSLLV", 0>;
defm "" : LMULReadAdvance<"ReadVWSLLX", 0>;
// Zvbc
defm "" : LMULReadAdvance<"ReadVCLMULV", 0>;
defm "" : LMULReadAdvance<"ReadVCLMULX", 0>;
// Zvkb
// VANDN uses ReadVIALU[V|X|I]
defm "" : LMULReadAdvance<"ReadVBREV8V", 0>;
defm "" : LMULReadAdvance<"ReadVREV8V", 0>;
defm "" : LMULReadAdvance<"ReadVRotV", 0>;
defm "" : LMULReadAdvance<"ReadVRotX", 0>;
// Zvkg
defm "" : LMULReadAdvance<"ReadVGHSHV", 0>;
defm "" : LMULReadAdvance<"ReadVGMULV", 0>;
// Zvknha or Zvknhb
defm "" : LMULReadAdvance<"ReadVSHA2CHV", 0>;
defm "" : LMULReadAdvance<"ReadVSHA2CLV", 0>;
defm "" : LMULReadAdvance<"ReadVSHA2MSV", 0>;
// Zvkned
defm "" : LMULReadAdvance<"ReadVAESMVV", 0>;
defm "" : LMULReadAdvance<"ReadVAESKF1V", 0>;
defm "" : LMULReadAdvance<"ReadVAESKF2V", 0>;
defm "" : LMULReadAdvance<"ReadVAESZV", 0>;
// Zvksed
defm "" : LMULReadAdvance<"ReadVSM4KV", 0>;
defm "" : LMULReadAdvance<"ReadVSM4RV", 0>;
// Zbksh
defm "" : LMULReadAdvance<"ReadVSM3CV", 0>;
defm "" : LMULReadAdvance<"ReadVSM3MEV", 0>;

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedXsfvcp;
}
