//==- RISCVSchedSiFiveP400.td - SiFiveP400 Scheduling Defs ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

def SiFiveP400Model : SchedMachineModel {
  let IssueWidth = 3;         // 3 micro-ops are dispatched per cycle.
  let MicroOpBufferSize = 56; // Max micro-ops that can be buffered.
  let LoadLatency = 4;        // Cycles for loads to access the cache.
  let MispredictPenalty = 9;  // Extra cycles for a mispredicted branch.
  let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                             HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                             HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                             HasStdExtZkr];
  let CompleteModel = false;
}

// The SiFiveP400 microarchitecure has 6 pipelines:
// Three pipelines for integer operations.
// One pipeline for FPU operations.
// One pipeline for Load operations.
// One pipeline for Store operations.
let SchedModel = SiFiveP400Model in {

def SiFiveP400IEXQ0       : ProcResource<1>;
def SiFiveP400IEXQ1       : ProcResource<1>;
def SiFiveP400IEXQ2       : ProcResource<1>;
def SiFiveP400FEXQ0       : ProcResource<1>;
def SiFiveP400Load        : ProcResource<1>;
def SiFiveP400Store       : ProcResource<1>;

def SiFiveP400IntArith    : ProcResGroup<[SiFiveP400IEXQ0, SiFiveP400IEXQ1, SiFiveP400IEXQ2]>;
defvar SiFiveP400Branch   = SiFiveP400IEXQ0;
defvar SiFiveP400SYS      = SiFiveP400IEXQ1;
defvar SiFiveP400MulDiv   = SiFiveP400IEXQ2;
defvar SiFiveP400I2F      = SiFiveP400IEXQ2;
def SiFiveP400Div         : ProcResource<1>;

defvar SiFiveP400FloatArith  = SiFiveP400FEXQ0;
defvar SiFiveP400F2I      = SiFiveP400FEXQ0;
def SiFiveP400FloatDiv    : ProcResource<1>;

let Latency = 1 in {
// Integer arithmetic and logic
def : WriteRes<WriteIALU, [SiFiveP400IntArith]>;
def : WriteRes<WriteIALU32, [SiFiveP400IntArith]>;
def : WriteRes<WriteShiftImm, [SiFiveP400IntArith]>;
def : WriteRes<WriteShiftImm32, [SiFiveP400IntArith]>;
def : WriteRes<WriteShiftReg, [SiFiveP400IntArith]>;
def : WriteRes<WriteShiftReg32, [SiFiveP400IntArith]>;
// Branching
def : WriteRes<WriteJmp, [SiFiveP400Branch]>;
def : WriteRes<WriteJal, [SiFiveP400Branch]>;
def : WriteRes<WriteJalr, [SiFiveP400Branch]>;
}

// CMOV
def P400WriteCMOV : SchedWriteRes<[SiFiveP400Branch, SiFiveP400IEXQ1]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[P400WriteCMOV], (instrs PseudoCCMOVGPRNoX0)>;

let Latency = 3 in {
// Integer multiplication
def : WriteRes<WriteIMul, [SiFiveP400MulDiv]>;
def : WriteRes<WriteIMul32, [SiFiveP400MulDiv]>;
// cpop[w] look exactly like multiply.
def : WriteRes<WriteCPOP, [SiFiveP400MulDiv]>;
def : WriteRes<WriteCPOP32, [SiFiveP400MulDiv]>;
}

// Integer division
def : WriteRes<WriteIDiv, [SiFiveP400MulDiv, SiFiveP400Div]> {
  let Latency = 35;
  let ReleaseAtCycles = [1, 34];
}
def : WriteRes<WriteIDiv32, [SiFiveP400MulDiv, SiFiveP400Div]> {
  let Latency = 20;
  let ReleaseAtCycles = [1, 19];
}

// Integer remainder
def : WriteRes<WriteIRem, [SiFiveP400MulDiv, SiFiveP400Div]> {
  let Latency = 35;
  let ReleaseAtCycles = [1, 34];
}
def : WriteRes<WriteIRem32, [SiFiveP400MulDiv, SiFiveP400Div]> {
  let Latency = 20;
  let ReleaseAtCycles = [1, 19];
}

let Latency = 1 in {
// Bitmanip
def : WriteRes<WriteRotateImm, [SiFiveP400IntArith]>;
def : WriteRes<WriteRotateImm32, [SiFiveP400IntArith]>;
def : WriteRes<WriteRotateReg, [SiFiveP400IntArith]>;
def : WriteRes<WriteRotateReg32, [SiFiveP400IntArith]>;

def : WriteRes<WriteCLZ, [SiFiveP400IntArith]>;
def : WriteRes<WriteCLZ32, [SiFiveP400IntArith]>;
def : WriteRes<WriteCTZ, [SiFiveP400IntArith]>;
def : WriteRes<WriteCTZ32, [SiFiveP400IntArith]>;

def : WriteRes<WriteORCB, [SiFiveP400IntArith]>;
def : WriteRes<WriteIMinMax, [SiFiveP400IntArith]>;

def : WriteRes<WriteREV8, [SiFiveP400IntArith]>;

def : WriteRes<WriteSHXADD, [SiFiveP400IntArith]>;
def : WriteRes<WriteSHXADD32, [SiFiveP400IntArith]>;

def : WriteRes<WriteSingleBit, [SiFiveP400IntArith]>;
def : WriteRes<WriteSingleBitImm, [SiFiveP400IntArith]>;
def : WriteRes<WriteBEXT, [SiFiveP400IntArith]>;
def : WriteRes<WriteBEXTI, [SiFiveP400IntArith]>;
}

// Memory
let Latency = 1 in {
def : WriteRes<WriteSTB, [SiFiveP400Store]>;
def : WriteRes<WriteSTH, [SiFiveP400Store]>;
def : WriteRes<WriteSTW, [SiFiveP400Store]>;
def : WriteRes<WriteSTD, [SiFiveP400Store]>;
def : WriteRes<WriteFST16, [SiFiveP400Store]>;
def : WriteRes<WriteFST32, [SiFiveP400Store]>;
def : WriteRes<WriteFST64, [SiFiveP400Store]>;
}
let Latency = 4 in {
def : WriteRes<WriteLDB, [SiFiveP400Load]>;
def : WriteRes<WriteLDH, [SiFiveP400Load]>;
}
let Latency = 4 in {
def : WriteRes<WriteLDW, [SiFiveP400Load]>;
def : WriteRes<WriteLDD, [SiFiveP400Load]>;
}

let Latency = 5 in {
def : WriteRes<WriteFLD16, [SiFiveP400Load]>;
def : WriteRes<WriteFLD32, [SiFiveP400Load]>;
def : WriteRes<WriteFLD64, [SiFiveP400Load]>;
}

// Atomic memory
let Latency = 3 in {
def : WriteRes<WriteAtomicSTW, [SiFiveP400Store]>;
def : WriteRes<WriteAtomicSTD, [SiFiveP400Store]>;
def : WriteRes<WriteAtomicW, [SiFiveP400Load]>;
def : WriteRes<WriteAtomicD, [SiFiveP400Load]>;
def : WriteRes<WriteAtomicLDW, [SiFiveP400Load]>;
def : WriteRes<WriteAtomicLDD, [SiFiveP400Load]>;
}

// Floating point
let Latency = 4 in {
def : WriteRes<WriteFAdd16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFAdd32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFAdd64, [SiFiveP400FloatArith]>;

def : WriteRes<WriteFMul16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMul32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMul64, [SiFiveP400FloatArith]>;

def : WriteRes<WriteFMA16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMA32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMA64, [SiFiveP400FloatArith]>;
}

let Latency = 2 in {
def : WriteRes<WriteFSGNJ16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFSGNJ32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFSGNJ64, [SiFiveP400FloatArith]>;

def : WriteRes<WriteFMinMax16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMinMax32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFMinMax64, [SiFiveP400FloatArith]>;
}

// Half precision.
def : WriteRes<WriteFDiv16, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 19;
  let ReleaseAtCycles = [1, 18];
}
def : WriteRes<WriteFSqrt16, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 18;
  let ReleaseAtCycles = [1, 17];
}

// Single precision.
def : WriteRes<WriteFDiv32, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 19;
  let ReleaseAtCycles = [1, 18];
}
def : WriteRes<WriteFSqrt32, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 18;
  let ReleaseAtCycles = [1, 17];
}

// Double precision
def : WriteRes<WriteFDiv64, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 33;
  let ReleaseAtCycles = [1, 32];
}
def : WriteRes<WriteFSqrt64, [SiFiveP400FEXQ0, SiFiveP400FloatDiv]> {
  let Latency = 33;
  let ReleaseAtCycles = [1, 32];
}

// Conversions
let Latency = 2 in {
def : WriteRes<WriteFCvtI32ToF16, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtI32ToF32, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtI32ToF64, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtI64ToF16, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtI64ToF32, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtI64ToF64, [SiFiveP400I2F]>;
def : WriteRes<WriteFCvtF16ToI32, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF16ToI64, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF16ToF32, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFCvtF16ToF64, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFCvtF32ToI32, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF32ToI64, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF32ToF16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFCvtF32ToF64, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFCvtF64ToI32, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF64ToI64, [SiFiveP400F2I]>;
def : WriteRes<WriteFCvtF64ToF16, [SiFiveP400FloatArith]>;
def : WriteRes<WriteFCvtF64ToF32, [SiFiveP400FloatArith]>;

def : WriteRes<WriteFClass16, [SiFiveP400F2I]>;
def : WriteRes<WriteFClass32, [SiFiveP400F2I]>;
def : WriteRes<WriteFClass64, [SiFiveP400F2I]>;
def : WriteRes<WriteFCmp16, [SiFiveP400F2I]>;
def : WriteRes<WriteFCmp32, [SiFiveP400F2I]>;
def : WriteRes<WriteFCmp64, [SiFiveP400F2I]>;
def : WriteRes<WriteFMovI16ToF16, [SiFiveP400I2F]>;
def : WriteRes<WriteFMovF16ToI16, [SiFiveP400F2I]>;
def : WriteRes<WriteFMovI32ToF32, [SiFiveP400I2F]>;
def : WriteRes<WriteFMovF32ToI32, [SiFiveP400F2I]>;
def : WriteRes<WriteFMovI64ToF64, [SiFiveP400I2F]>;
def : WriteRes<WriteFMovF64ToI64, [SiFiveP400F2I]>;
}

// Others
def : WriteRes<WriteCSR, [SiFiveP400SYS]>;
def : WriteRes<WriteNop, []>;

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

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedV;
defm : UnsupportedSchedXsfvcp;
defm : UnsupportedSchedZvk;
}
