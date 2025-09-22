//===-- RISCVSchedule.td - RISC-V Scheduling Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/// Define scheduler resources associated with def operands.
def WriteIALU       : SchedWrite;    // 32 or 64-bit integer ALU operations
def WriteIALU32     : SchedWrite;    // 32-bit integer ALU operations on RV64I
def WriteShiftImm   : SchedWrite;    // 32 or 64-bit shift by immediate operations
def WriteShiftImm32 : SchedWrite;    // 32-bit shift by immediate operations on RV64Ix
def WriteShiftReg   : SchedWrite;    // 32 or 64-bit shift by immediate operations
def WriteShiftReg32 : SchedWrite;    // 32-bit shift by immediate operations on RV64Ix
def WriteIDiv       : SchedWrite;    // 32-bit or 64-bit divide
def WriteIDiv32     : SchedWrite;    // 32-bit divide on RV64I
def WriteIRem       : SchedWrite;    // 32-bit or 64-bit remainder
def WriteIRem32     : SchedWrite;    // 32-bit remainder on RV64I
def WriteIMul       : SchedWrite;    // 32-bit or 64-bit multiply
def WriteIMul32     : SchedWrite;    // 32-bit multiply on RV64I
def WriteJmp        : SchedWrite;    // Jump
def WriteJal        : SchedWrite;    // Jump and link
def WriteJalr       : SchedWrite;    // Jump and link register
def WriteNop        : SchedWrite;
def WriteLDB        : SchedWrite;    // Load byte
def WriteLDH        : SchedWrite;    // Load half-word
def WriteLDW        : SchedWrite;    // Load word
def WriteLDD        : SchedWrite;    // Load double-word
def WriteCSR        : SchedWrite;    // CSR instructions
def WriteSTB        : SchedWrite;    // Store byte
def WriteSTH        : SchedWrite;    // Store half-word
def WriteSTW        : SchedWrite;    // Store word
def WriteSTD        : SchedWrite;    // Store double-word
def WriteAtomicB    : SchedWrite;    //Atomic memory operation byte size
def WriteAtomicH    : SchedWrite;    //Atomic memory operation halfword size
def WriteAtomicW    : SchedWrite;    //Atomic memory operation word size
def WriteAtomicD    : SchedWrite;    //Atomic memory operation double word size
def WriteAtomicLDW  : SchedWrite;    // Atomic load word
def WriteAtomicLDD  : SchedWrite;    // Atomic load double word
def WriteAtomicSTW  : SchedWrite;    // Atomic store word
def WriteAtomicSTD  : SchedWrite;    // Atomic store double word
def WriteFAdd16     : SchedWrite;    // 16-bit floating point addition/subtraction
def WriteFAdd32     : SchedWrite;    // 32-bit floating point addition/subtraction
def WriteFAdd64     : SchedWrite;    // 64-bit floating point addition/subtraction
def WriteFMul16     : SchedWrite;    // 16-bit floating point multiply
def WriteFMul32     : SchedWrite;    // 32-bit floating point multiply
def WriteFMul64     : SchedWrite;    // 64-bit floating point multiply
def WriteFMA16      : SchedWrite;    // 16-bit floating point fused multiply-add
def WriteFMA32      : SchedWrite;    // 32-bit floating point fused multiply-add
def WriteFMA64      : SchedWrite;    // 64-bit floating point fused multiply-add
def WriteFDiv16     : SchedWrite;    // 16-bit floating point divide
def WriteFDiv32     : SchedWrite;    // 32-bit floating point divide
def WriteFDiv64     : SchedWrite;    // 64-bit floating point divide
def WriteFSqrt16    : SchedWrite;    // 16-bit floating point sqrt
def WriteFSqrt32    : SchedWrite;    // 32-bit floating point sqrt
def WriteFSqrt64    : SchedWrite;    // 64-bit floating point sqrt

// Integer to float conversions
def WriteFCvtI32ToF16  : SchedWrite;
def WriteFCvtI32ToF32  : SchedWrite;
def WriteFCvtI32ToF64  : SchedWrite;
def WriteFCvtI64ToF16  : SchedWrite;    // RV64I only
def WriteFCvtI64ToF32  : SchedWrite;    // RV64I only
def WriteFCvtI64ToF64  : SchedWrite;    // RV64I only

//Float to integer conversions
def WriteFCvtF16ToI32  : SchedWrite;
def WriteFCvtF16ToI64  : SchedWrite;    // RV64I only
def WriteFCvtF32ToI32  : SchedWrite;
def WriteFCvtF32ToI64  : SchedWrite;    // RV64I only
def WriteFCvtF64ToI32  : SchedWrite;
def WriteFCvtF64ToI64  : SchedWrite;    // RV64I only

// Float to float conversions
def WriteFCvtF32ToF64  : SchedWrite;
def WriteFCvtF64ToF32  : SchedWrite;
def WriteFCvtF16ToF32  : SchedWrite;
def WriteFCvtF32ToF16  : SchedWrite;
def WriteFCvtF16ToF64  : SchedWrite;
def WriteFCvtF64ToF16  : SchedWrite;

// Zfa found instructions.
def WriteFRoundF32     : SchedWrite;
def WriteFRoundF64     : SchedWrite;
def WriteFRoundF16     : SchedWrite;

def WriteFClass16   : SchedWrite;    // 16-bit floating point classify
def WriteFClass32   : SchedWrite;    // 32-bit floating point classify
def WriteFClass64   : SchedWrite;    // 64-bit floating point classify
def WriteFCmp16     : SchedWrite;    // 16-bit floating point compare
def WriteFCmp32     : SchedWrite;    // 32-bit floating point compare
def WriteFCmp64     : SchedWrite;    // 64-bit floating point compare
def WriteFSGNJ16    : SchedWrite;    // 16-bit floating point sign-injection
def WriteFSGNJ32    : SchedWrite;    // 32-bit floating point sign-injection
def WriteFSGNJ64    : SchedWrite;    // 64-bit floating point sign-injection
def WriteFMinMax16  : SchedWrite;    // 16-bit floating point min or max
def WriteFMinMax32  : SchedWrite;    // 32-bit floating point min or max
def WriteFMinMax64  : SchedWrite;    // 64-bit floating point min or max

def WriteFMovF16ToI16     : SchedWrite;
def WriteFMovI16ToF16     : SchedWrite;
def WriteFMovF32ToI32     : SchedWrite;
def WriteFMovI32ToF32     : SchedWrite;
def WriteFMovF64ToI64     : SchedWrite;    // RV64I only
def WriteFMovI64ToF64     : SchedWrite;    // RV64I only

def WriteFLI16        : SchedWrite;    // Floating point constant load
def WriteFLI32        : SchedWrite;    // Floating point constant load
def WriteFLI64        : SchedWrite;    // Floating point constant load

def WriteFLD16        : SchedWrite;    // Floating point sp load
def WriteFLD32        : SchedWrite;    // Floating point sp load
def WriteFLD64        : SchedWrite;    // Floating point dp load
def WriteFST16        : SchedWrite;    // Floating point sp store
def WriteFST32        : SchedWrite;    // Floating point sp store
def WriteFST64        : SchedWrite;    // Floating point dp store

// short forward branch for Bullet
def WriteSFB        : SchedWrite;
def ReadSFBJmp      : SchedRead;
def ReadSFBALU      : SchedRead;

/// Define scheduler resources associated with use operands.
def ReadJmp         : SchedRead;
def ReadJalr        : SchedRead;
def ReadCSR         : SchedRead;
def ReadMemBase     : SchedRead;
def ReadFMemBase    : SchedRead;
def ReadStoreData   : SchedRead;
def ReadFStoreData  : SchedRead;
def ReadIALU        : SchedRead;
def ReadIALU32      : SchedRead;    // 32-bit integer ALU operations on RV64I
def ReadShiftImm    : SchedRead;
def ReadShiftImm32  : SchedRead;    // 32-bit shift by immediate operations on RV64Ix
def ReadShiftReg    : SchedRead;
def ReadShiftReg32  : SchedRead;    // 32-bit shift by register operations on RV64Ix
def ReadIDiv        : SchedRead;
def ReadIDiv32      : SchedRead;
def ReadIRem        : SchedRead;
def ReadIRem32      : SchedRead;
def ReadIMul        : SchedRead;
def ReadIMul32      : SchedRead;
def ReadAtomicBA    : SchedRead;
def ReadAtomicBD    : SchedRead;
def ReadAtomicHA    : SchedRead;
def ReadAtomicHD    : SchedRead;
def ReadAtomicWA    : SchedRead;
def ReadAtomicWD    : SchedRead;
def ReadAtomicDA    : SchedRead;
def ReadAtomicDD    : SchedRead;
def ReadAtomicLDW   : SchedRead;    // Atomic load word
def ReadAtomicLDD   : SchedRead;    // Atomic load double word
def ReadAtomicSTW   : SchedRead;    // Atomic store word
def ReadAtomicSTD   : SchedRead;    // Atomic store double word
def ReadFAdd16      : SchedRead;    // 16-bit floating point addition/subtraction
def ReadFAdd32      : SchedRead;    // 32-bit floating point addition/subtraction
def ReadFAdd64      : SchedRead;    // 64-bit floating point addition/subtraction
def ReadFMul16      : SchedRead;    // 16-bit floating point multiply
def ReadFMul32      : SchedRead;    // 32-bit floating point multiply
def ReadFMul64      : SchedRead;    // 64-bit floating point multiply
def ReadFMA16       : SchedRead;    // 16-bit floating point fused multiply-add
def ReadFMA16Addend : SchedRead;    // 16-bit floating point fused multiply-add (addend)
def ReadFMA32       : SchedRead;    // 32-bit floating point fused multiply-add
def ReadFMA32Addend : SchedRead;    // 32-bit floating point fused multiply-add (addend)
def ReadFMA64       : SchedRead;    // 64-bit floating point fused multiply-add
def ReadFMA64Addend : SchedRead;    // 64-bit floating point fused multiply-add (addend)
def ReadFDiv16      : SchedRead;    // 16-bit floating point divide
def ReadFDiv32      : SchedRead;    // 32-bit floating point divide
def ReadFDiv64      : SchedRead;    // 64-bit floating point divide
def ReadFSqrt16     : SchedRead;    // 16-bit floating point sqrt
def ReadFSqrt32     : SchedRead;    // 32-bit floating point sqrt
def ReadFSqrt64     : SchedRead;    // 64-bit floating point sqrt
def ReadFCmp16      : SchedRead;
def ReadFCmp32      : SchedRead;
def ReadFCmp64      : SchedRead;
def ReadFSGNJ16     : SchedRead;
def ReadFSGNJ32     : SchedRead;
def ReadFSGNJ64     : SchedRead;
def ReadFMinMax16   : SchedRead;
def ReadFMinMax32   : SchedRead;
def ReadFMinMax64   : SchedRead;
def ReadFCvtF16ToI32     : SchedRead;
def ReadFCvtF16ToI64     : SchedRead;
def ReadFCvtF32ToI32     : SchedRead;
def ReadFCvtF32ToI64     : SchedRead;
def ReadFCvtF64ToI32     : SchedRead;
def ReadFCvtF64ToI64     : SchedRead;
def ReadFCvtI32ToF16     : SchedRead;
def ReadFCvtI32ToF32     : SchedRead;
def ReadFCvtI32ToF64     : SchedRead;
def ReadFCvtI64ToF16     : SchedRead;
def ReadFCvtI64ToF32     : SchedRead;
def ReadFCvtI64ToF64     : SchedRead;
def ReadFMovF16ToI16     : SchedRead;
def ReadFMovI16ToF16     : SchedRead;
def ReadFMovF32ToI32     : SchedRead;
def ReadFMovI32ToF32     : SchedRead;
def ReadFMovF64ToI64     : SchedRead;
def ReadFMovI64ToF64     : SchedRead;
def ReadFCvtF32ToF64     : SchedRead;
def ReadFCvtF64ToF32     : SchedRead;
def ReadFCvtF16ToF32     : SchedRead;
def ReadFCvtF32ToF16     : SchedRead;
def ReadFCvtF16ToF64     : SchedRead;
def ReadFCvtF64ToF16     : SchedRead;
def ReadFRoundF16        : SchedRead;
def ReadFRoundF32        : SchedRead;
def ReadFRoundF64        : SchedRead;
def ReadFClass16         : SchedRead;
def ReadFClass32         : SchedRead;
def ReadFClass64         : SchedRead;

multiclass UnsupportedSchedZfh {
let Unsupported = true in {
def : WriteRes<WriteFAdd16, []>;
def : WriteRes<WriteFClass16, []>;
def : WriteRes<WriteFCvtF16ToF64, []>;
def : WriteRes<WriteFCvtF64ToF16, []>;
def : WriteRes<WriteFCvtI64ToF16, []>;
def : WriteRes<WriteFCvtF32ToF16, []>;
def : WriteRes<WriteFCvtI32ToF16, []>;
def : WriteRes<WriteFCvtF16ToI64, []>;
def : WriteRes<WriteFCvtF16ToF32, []>;
def : WriteRes<WriteFCvtF16ToI32, []>;
def : WriteRes<WriteFDiv16, []>;
def : WriteRes<WriteFCmp16, []>;
def : WriteRes<WriteFLD16, []>;
def : WriteRes<WriteFMA16, []>;
def : WriteRes<WriteFMinMax16, []>;
def : WriteRes<WriteFMul16, []>;
def : WriteRes<WriteFMovI16ToF16, []>;
def : WriteRes<WriteFMovF16ToI16, []>;
def : WriteRes<WriteFSGNJ16, []>;
def : WriteRes<WriteFST16, []>;
def : WriteRes<WriteFSqrt16, []>;

def : ReadAdvance<ReadFAdd16, 0>;
def : ReadAdvance<ReadFClass16, 0>;
def : ReadAdvance<ReadFCvtF16ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF16, 0>;
def : ReadAdvance<ReadFCvtI64ToF16, 0>;
def : ReadAdvance<ReadFCvtF32ToF16, 0>;
def : ReadAdvance<ReadFCvtI32ToF16, 0>;
def : ReadAdvance<ReadFCvtF16ToI64, 0>;
def : ReadAdvance<ReadFCvtF16ToF32, 0>;
def : ReadAdvance<ReadFCvtF16ToI32, 0>;
def : ReadAdvance<ReadFDiv16, 0>;
def : ReadAdvance<ReadFCmp16, 0>;
def : ReadAdvance<ReadFMA16, 0>;
def : ReadAdvance<ReadFMinMax16, 0>;
def : ReadAdvance<ReadFMul16, 0>;
def : ReadAdvance<ReadFMovI16ToF16, 0>;
def : ReadAdvance<ReadFMovF16ToI16, 0>;
def : ReadAdvance<ReadFSGNJ16, 0>;
def : ReadAdvance<ReadFSqrt16, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedF {
let Unsupported = true in {
def : WriteRes<WriteFST32, []>;
def : WriteRes<WriteFLD32, []>;
def : WriteRes<WriteFAdd32, []>;
def : WriteRes<WriteFSGNJ32, []>;
def : WriteRes<WriteFMinMax32, []>;
def : WriteRes<WriteFCvtI32ToF32, []>;
def : WriteRes<WriteFCvtI64ToF32, []>;
def : WriteRes<WriteFCvtF32ToI32, []>;
def : WriteRes<WriteFCvtF32ToI64, []>;
def : WriteRes<WriteFClass32, []>;
def : WriteRes<WriteFCmp32, []>;
def : WriteRes<WriteFMovF32ToI32, []>;
def : WriteRes<WriteFMovI32ToF32, []>;
def : WriteRes<WriteFMul32, []>;
def : WriteRes<WriteFMA32, []>;
def : WriteRes<WriteFDiv32, []>;
def : WriteRes<WriteFSqrt32, []>;

def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 0>;
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFStoreData, 0>;
def : ReadAdvance<ReadFMemBase, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedD {
let Unsupported = true in {
def : WriteRes<WriteFST64, []>;
def : WriteRes<WriteFLD64, []>;
def : WriteRes<WriteFAdd64, []>;
def : WriteRes<WriteFSGNJ64, []>;
def : WriteRes<WriteFMinMax64, []>;
def : WriteRes<WriteFCvtI32ToF64, []>;
def : WriteRes<WriteFCvtI64ToF64, []>;
def : WriteRes<WriteFCvtF64ToI32, []>;
def : WriteRes<WriteFCvtF64ToI64, []>;
def : WriteRes<WriteFCvtF32ToF64, []>;
def : WriteRes<WriteFCvtF64ToF32, []>;
def : WriteRes<WriteFClass64, []>;
def : WriteRes<WriteFCmp64, []>;
def : WriteRes<WriteFMovF64ToI64, []>;
def : WriteRes<WriteFMovI64ToF64, []>;
def : WriteRes<WriteFMul64, []>;
def : WriteRes<WriteFMA64, []>;
def : WriteRes<WriteFDiv64, []>;
def : WriteRes<WriteFSqrt64, []>;

def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass64, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedSFB {
let Unsupported = true in {
def : WriteRes<WriteSFB, []>;

def : ReadAdvance<ReadSFBJmp, 0>;
def : ReadAdvance<ReadSFBALU, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedZfa {
let Unsupported = true in {
def : WriteRes<WriteFRoundF16, []>;
def : WriteRes<WriteFRoundF32, []>;
def : WriteRes<WriteFRoundF64, []>;
def : WriteRes<WriteFLI16, []>;
def : WriteRes<WriteFLI32, []>;
def : WriteRes<WriteFLI64, []>;

def : ReadAdvance<ReadFRoundF32, 0>;
def : ReadAdvance<ReadFRoundF64, 0>;
def : ReadAdvance<ReadFRoundF16, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedZabha {
let Unsupported = true in {
def : WriteRes<WriteAtomicB, []>;
def : WriteRes<WriteAtomicH, []>;

def : ReadAdvance<ReadAtomicBA, 0>;
def : ReadAdvance<ReadAtomicBD, 0>;
def : ReadAdvance<ReadAtomicHA, 0>;
def : ReadAdvance<ReadAtomicHD, 0>;
} // Unsupported = true
}

multiclass UnsupportedSchedA {
let Unsupported = true in {
def : WriteRes<WriteAtomicW, []>;
def : WriteRes<WriteAtomicD, []>;
def : WriteRes<WriteAtomicLDW, []>;
def : WriteRes<WriteAtomicLDD, []>;
def : WriteRes<WriteAtomicSTW, []>;
def : WriteRes<WriteAtomicSTD, []>;

def : ReadAdvance<ReadAtomicWA, 0>;
def : ReadAdvance<ReadAtomicWD, 0>;
def : ReadAdvance<ReadAtomicDA, 0>;
def : ReadAdvance<ReadAtomicDD, 0>;
def : ReadAdvance<ReadAtomicLDW, 0>;
def : ReadAdvance<ReadAtomicLDD, 0>;
def : ReadAdvance<ReadAtomicSTW, 0>;
def : ReadAdvance<ReadAtomicSTD, 0>;
} // Unsupported = true
}

// Include the scheduler resources for other instruction extensions.
include "RISCVScheduleZb.td"
include "RISCVScheduleV.td"
include "RISCVScheduleXSf.td"
include "RISCVScheduleZvk.td"
