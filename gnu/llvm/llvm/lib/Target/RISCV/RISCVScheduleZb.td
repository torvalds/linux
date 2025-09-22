//===- RISCVScheduleB.td - RISC-V Scheduling Definitions B -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/// Define scheduler resources associated with def operands.

// Zba extension
def WriteSHXADD      : SchedWrite; // sh1add/sh2add/sh3add
def WriteSHXADD32    : SchedWrite; // sh1add.uw/sh2add.uw/sh3add.uw

// Zbb extension
def WriteRotateImm   : SchedWrite;
def WriteRotateImm32 : SchedWrite;
def WriteRotateReg   : SchedWrite;
def WriteRotateReg32 : SchedWrite;
def WriteCLZ         : SchedWrite;
def WriteCLZ32       : SchedWrite;
def WriteCTZ         : SchedWrite;
def WriteCTZ32       : SchedWrite;
def WriteCPOP        : SchedWrite;
def WriteCPOP32      : SchedWrite;
def WriteREV8        : SchedWrite;
def WriteORCB        : SchedWrite;
def WriteIMinMax     : SchedWrite;

// Zbc extension
def WriteCLMUL       : SchedWrite; // CLMUL/CLMULR/CLMULH

// Zbs extension
def WriteSingleBit   : SchedWrite; // BCLR/BSET/BINV
def WriteSingleBitImm: SchedWrite; // BCLRI/BSETI/BINVI
def WriteBEXT : SchedWrite; // BEXT
def WriteBEXTI : SchedWrite; // BEXTI

// Zbkb extension
def WriteBREV8       : SchedWrite; // brev8
def WritePACK        : SchedWrite; // pack/packh
def WritePACK32      : SchedWrite; // packw
def WriteZIP         : SchedWrite; // zip/unzip

// Zbkx extension
def WriteXPERM       : SchedWrite; // xperm4/xperm8

/// Define scheduler resources associated with use operands.

// Zba extension
def ReadSHXADD      : SchedRead; // sh1add/sh2add/sh3add
def ReadSHXADD32    : SchedRead; // sh1add.uw/sh2add.uw/sh3add.uw

// Zbb extension
def ReadRotateImm   : SchedRead;
def ReadRotateImm32 : SchedRead;
def ReadRotateReg   : SchedRead;
def ReadRotateReg32 : SchedRead;
def ReadCLZ         : SchedRead;
def ReadCLZ32       : SchedRead;
def ReadCTZ         : SchedRead;
def ReadCTZ32       : SchedRead;
def ReadCPOP        : SchedRead;
def ReadCPOP32      : SchedRead;
def ReadREV8        : SchedRead;
def ReadORCB        : SchedRead;
def ReadIMinMax     : SchedRead;

// Zbc extension
def ReadCLMUL       : SchedRead; // CLMUL/CLMULR/CLMULH

// Zbs extension
def ReadSingleBit   : SchedRead; // BCLR/BSET/BINV/BEXT
def ReadSingleBitImm: SchedRead; // BCLRI/BSETI/BINVI/BEXTI

// Zbkb extension
def ReadBREV8       : SchedRead; // brev8
def ReadPACK        : SchedRead; // pack/packh
def ReadPACK32      : SchedRead; // packw
def ReadZIP         : SchedRead; // zip/unzip

// Zbkx extension
def ReadXPERM       : SchedRead; // xperm4/xperm8

/// Define default scheduler resources for B.

multiclass UnsupportedSchedZba {
let Unsupported = true in {
def : WriteRes<WriteSHXADD, []>;
def : WriteRes<WriteSHXADD32, []>;

def : ReadAdvance<ReadSHXADD, 0>;
def : ReadAdvance<ReadSHXADD32, 0>;
}
}

multiclass UnsupportedSchedZbb {
let Unsupported = true in {
def : WriteRes<WriteRotateImm, []>;
def : WriteRes<WriteRotateImm32, []>;
def : WriteRes<WriteRotateReg, []>;
def : WriteRes<WriteRotateReg32, []>;
def : WriteRes<WriteCLZ, []>;
def : WriteRes<WriteCLZ32, []>;
def : WriteRes<WriteCTZ, []>;
def : WriteRes<WriteCTZ32, []>;
def : WriteRes<WriteCPOP, []>;
def : WriteRes<WriteCPOP32, []>;
def : WriteRes<WriteREV8, []>;
def : WriteRes<WriteORCB, []>;
def : WriteRes<WriteIMinMax, []>;

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
def : ReadAdvance<ReadREV8, 0>;
def : ReadAdvance<ReadORCB, 0>;
def : ReadAdvance<ReadIMinMax, 0>;
}
}

multiclass UnsupportedSchedZbc {
let Unsupported = true in {
def : WriteRes<WriteCLMUL, []>;

def : ReadAdvance<ReadCLMUL, 0>;
}
}

multiclass UnsupportedSchedZbs {
let Unsupported = true in {
def : WriteRes<WriteSingleBit, []>;
def : WriteRes<WriteSingleBitImm, []>;
def : WriteRes<WriteBEXT, []>;
def : WriteRes<WriteBEXTI, []>;

def : ReadAdvance<ReadSingleBit, 0>;
def : ReadAdvance<ReadSingleBitImm, 0>;
}
}

multiclass UnsupportedSchedZbkb {
let Unsupported = true in {
def : WriteRes<WriteBREV8, []>;
def : WriteRes<WritePACK, []>;
def : WriteRes<WritePACK32, []>;
def : WriteRes<WriteZIP, []>;

def : ReadAdvance<ReadBREV8, 0>;
def : ReadAdvance<ReadPACK, 0>;
def : ReadAdvance<ReadPACK32, 0>;
def : ReadAdvance<ReadZIP, 0>;
}
}

multiclass UnsupportedSchedZbkx {
let Unsupported = true in {
def : WriteRes<WriteXPERM, []>;

def : ReadAdvance<ReadXPERM, 0>;
}
}
