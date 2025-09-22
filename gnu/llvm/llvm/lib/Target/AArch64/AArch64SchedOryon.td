//=- AArch64SchedOryon.td - Qualcomm Oryon CPU 001 ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for Qualcomm Oryon
// family of processors.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pipeline Description.

def OryonModel : SchedMachineModel {
  let IssueWidth            =  14;
  let MicroOpBufferSize     = 376;
  let LoadLatency           =   4;
  let MispredictPenalty     =  13; // 13 cycles for mispredicted branch.
  let LoopMicroOpBufferSize =   0; // Do not have a LoopMicroOpBuffer
  let PostRAScheduler       =   1; // Using PostRA sched.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = !listconcat(SVEUnsupported.F,
                                                    SMEUnsupported.F,
                                                    MTEUnsupported.F,
                                                    PAUnsupported.F,
                                                    [HasPAuth, HasCSSC]);
}

let SchedModel = OryonModel in {

// Issue ports.
// IXU has 6 ports p0 ~ p5
// LSU has 4 ports p6 ~ p9(ls0 ~ ls3), p10/p11(std0, std1) has to work with ls0~ls3
// VXU has 4 ports p12 ~ p15

// cross IXU/LSU/VXU resource group for FMOV P41 of VXU
// I2V
def ORYONI4FP0 : ProcResource<1>;
def ORYONI5FP1 : ProcResource<1>;
// V2I
def ORYONFP0I4 : ProcResource<1>;
def ORYONFP1I5 : ProcResource<1>;

// store 1 for normal store instructions
def ORYONST0 : ProcResource<1>;
// store 2 for normal store instructions
def ORYONST1 : ProcResource<1>;

// Port 0: ALU/Indirect/Direct Branch.
def ORYONP0 : ProcResource<1>;

// Port 1: ALU/Direct Branch.
def ORYONP1 : ProcResource<1>;

// Port 2: ALU.
def ORYONP2 : ProcResource<1>;

// Port 3: ALU.
def ORYONP3 : ProcResource<1>;

// Port 4: ALU.
def ORYONP4 : ProcResource<1> {
    let Super = ORYONI4FP0;
    let Super = ORYONFP0I4; }

// Port 5: ALU.
def ORYONP5 : ProcResource<1> {
    let Super = ORYONI5FP1;
    let Super = ORYONFP1I5; }

// Port 6: Load/Store. LS0
def ORYONP6 : ProcResource<1> {
    let Super = ORYONST0; }

// Port 7: Load/store. LS1
def ORYONP7 : ProcResource<1> {
    let Super = ORYONST0; }

// Port 8: Load/Store. LS2
def ORYONP8 : ProcResource<1> {
    let Super = ORYONST1; }

// Port 9: Load/store. LS3
def ORYONP9 : ProcResource<1> {
    let Super = ORYONST1; }

// Port 10: Load/Store. STD0
def ORYONP10SD0 : ProcResource<1> {
    let Super = ORYONST0; }

// Port 11: Load/store. STD1
def ORYONP11SD1 : ProcResource<1> {
    let Super = ORYONST1; }

// Port 12: FP/Neon/SIMD/Crypto.
def ORYONP12FP0 : ProcResource<1> {
    let Super = ORYONI4FP0;
    let Super = ORYONFP0I4; }

// Port 13: FP/Neon/SIMD/Crypto.
def ORYONP13FP1 : ProcResource<1> {
    let Super = ORYONI5FP1;
    let Super = ORYONFP1I5; }

// Port 14: FP/Neon/SIMD/Crypto.
def ORYONP14FP2 : ProcResource<1>;

// Port 15: FP/Neon/SIMD/Crypto.
def ORYONP15FP3 : ProcResource<1>;

// Define groups for the functional units on each issue port.  Each group
// created will be used by a WriteRes.

// Integer add/shift/logical/misc. instructions on port I0/I1/I2/I3/I4/I5.
def ORYONI012345 : ProcResGroup<[ORYONP0, ORYONP1, ORYONP2,
                                  ORYONP3, ORYONP4, ORYONP5]> {
  let BufferSize = 120;
}

// Direct Conditional Branch instructions on ports I0/I1.
def ORYONI01 : ProcResGroup<[ORYONP0, ORYONP1]> {
  let BufferSize = 40;
}

// Indirect/crypto Conditional Branch instructions on ports I0.
def ORYONI0 : ProcResGroup<[ORYONP0]> {
  let BufferSize = 20;
}

// Crypto/CRC/PAU instructions on ports I2.
def ORYONI2 : ProcResGroup<[ORYONP2]> {
  let BufferSize = 20;
}

// Multiply/Multiply-ADD instructions on ports I4/I5.
def ORYONI45 : ProcResGroup<[ORYONP4, ORYONP5]> {
  let BufferSize = 40;
}

// Divide instructions on ports I5.
def ORYONI5 : ProcResGroup<[ORYONP5]> {
  let BufferSize = 20;
}

// Comparison instructions on ports I0/I1/I2/I3.
def ORYONI0123 : ProcResGroup<[ORYONP0, ORYONP1,
                                ORYONP2, ORYONP3]> {
  let BufferSize = 80;
}

// Load instructions on ports P6/P7/P8/P9.
def ORYONLD : ProcResGroup<[ORYONP6, ORYONP7, ORYONP8, ORYONP9]> {
  let BufferSize = 64;
}

// Store instructions on combo of STA/STD pipes
def ORYONST : ProcResGroup<[ORYONST0, ORYONST1]> {
    let BufferSize = 64;
}

// Arithmetic and CRYP-AED ASIMD/FP instructions on ports FP0/FP1/FP2/FP3.
def ORYONFP0123 : ProcResGroup<[ORYONP12FP0, ORYONP13FP1,
                                   ORYONP14FP2, ORYONP15FP3]> {
  let BufferSize = 192;
}

// FP Comparison and F/I move instructions on ports FP0/FP1.
def ORYONFP01 : ProcResGroup<[ORYONP12FP0, ORYONP13FP1]> {
  let BufferSize = 96;
}

// FDIV instructions on ports FP3.
def ORYONFP3 : ProcResGroup<[ORYONP15FP3]> {
  let BufferSize = 48;
}

// CRYP-SHA instructions on ports FP1.
def ORYONFP1 : ProcResGroup<[ORYONP14FP2]> {
  let BufferSize = 48;
}

def ORYONFP2 : ProcResGroup<[ORYONP14FP2]> {
  let BufferSize = 48;
}

// Reciprocal, Squre root on FP0.
def ORYONFP0 : ProcResGroup<[ORYONP12FP0]> {
  let BufferSize = 48;
}

// cross IXU/LSU/VXU resource group for FMOV P41 of VXU
// I2V
def ORYONI2V : ProcResGroup<[ORYONI4FP0, ORYONI5FP1]> {
    let BufferSize = 40;
}

// V2I
def ORYONV2I : ProcResGroup<[ORYONFP0I4, ORYONFP1I5]> {
    let BufferSize = 96;
}

// Define commonly used write types for InstRW specializations.
// All definitions follow the format: ORYONWrite_<NumCycles>Cyc_<Resources>.

// Because of the complexity of Oryon CPU, we skip the following
// generic definitions and define each instruction specifically

// These WriteRes entries are not used in the Falkor sched model.
def : WriteRes<WriteImm, []>     { let Unsupported = 1; }
def : WriteRes<WriteI, []>       { let Unsupported = 1; }
def : WriteRes<WriteISReg, []>   { let Unsupported = 1; }
def : WriteRes<WriteIEReg, []>   { let Unsupported = 1; }
def : WriteRes<WriteExtr, []>    { let Unsupported = 1; }
def : WriteRes<WriteIS, []>      { let Unsupported = 1; }
def : WriteRes<WriteID32, []>    { let Unsupported = 1; }
def : WriteRes<WriteID64, []>    { let Unsupported = 1; }
def : WriteRes<WriteIM32, []>    { let Unsupported = 1; }
def : WriteRes<WriteIM64, []>    { let Unsupported = 1; }
def : WriteRes<WriteBr, []>      { let Unsupported = 1; }
def : WriteRes<WriteBrReg, []>   { let Unsupported = 1; }
def : WriteRes<WriteLD, []>      { let Unsupported = 1; }
def : WriteRes<WriteST, []>      { let Unsupported = 1; }
def : WriteRes<WriteSTP, []>     { let Unsupported = 1; }
def : WriteRes<WriteAdr, []>     { let Unsupported = 1; }
def : WriteRes<WriteLDIdx, []>   { let Unsupported = 1; }
def : WriteRes<WriteSTIdx, []>   { let Unsupported = 1; }
def : WriteRes<WriteF, []>       { let Unsupported = 1; }
def : WriteRes<WriteFCmp, []>    { let Unsupported = 1; }
def : WriteRes<WriteFCvt, []>    { let Unsupported = 1; }
def : WriteRes<WriteFCopy, []>   { let Unsupported = 1; }
def : WriteRes<WriteFImm, []>    { let Unsupported = 1; }
def : WriteRes<WriteFMul, []>    { let Unsupported = 1; }
def : WriteRes<WriteFDiv, []>    { let Unsupported = 1; }
def : WriteRes<WriteVd, []>      { let Unsupported = 1; }
def : WriteRes<WriteVq, []>      { let Unsupported = 1; }
def : WriteRes<WriteVLD, []>     { let Unsupported = 1; }
def : WriteRes<WriteVST, []>     { let Unsupported = 1; }
def : WriteRes<WriteSys, []>     { let Unsupported = 1; }
def : WriteRes<WriteBarrier, []> { let Unsupported = 1; }
def : WriteRes<WriteHint, []>    { let Unsupported = 1; }
def : WriteRes<WriteLDHi, []>    { let Unsupported = 1; }
def : WriteRes<WriteAtomic, []>  { let Unsupported = 1; }

// These ReadAdvance entries will be defined in later implementation
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


//IXU resource definition
// 1 cycles NO pipe
def ORYONWrite_1Cyc_NONE : SchedWriteRes<[]>;

// 1 cycles on I01.
def ORYONWrite_1Cyc_I01 : SchedWriteRes<[ORYONI01]>;

def ORYONWrite_1Cyc_2Uops_I01 : SchedWriteRes<[ORYONI01]> {
  let NumMicroOps = 2;
}

def ORYONWrite_1Cyc_I0 : SchedWriteRes<[ORYONI0]>;

// 7 cycles on I2. PAC*/AUT* instructions
def ORYONWrite_7Cyc_I2 : SchedWriteRes<[ORYONI2]> {
  let Latency = 7;
}

// 7 cycles on I2. PAC*/AUT* instructions
def ORYONWrite_7Cyc_3Uops_I2 : SchedWriteRes<[ORYONI2]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 9 (7+1+1) cycles on I2 and I0/I1, I0. Authentication branch instructions
// these instructions are broken down to three uops
// a.	PtrAuth on pipe 2 taking 7 cycles
// b.	Link Register Update on pipes 0 and 1 taking 1 cycle
// c.	Indirect branch on pipe 0 taking 1 cycle

def ORYONWrite_9Cyc_I012 : SchedWriteRes<[ORYONI2, ORYONI01]> {
  let Latency = 9;
  let NumMicroOps = 3;
}

// 3 cycles on I2. CRC32 and CRC32C instructions
def ORYONWrite_3Cyc_I2 : SchedWriteRes<[ORYONI2]> {
  let Latency = 3;
}

// 1 cycle on I012345
def ORYONWrite_1Cyc_I012345 : SchedWriteRes<[ORYONI012345]>;

// 1 cycle on I0123
def ORYONWrite_1Cyc_I0123 : SchedWriteRes<[ORYONI0123]>;

// 1 cycle on 2 of I012345
def ORYONWrite_1Cyc_I012345_I012345 :
SchedWriteRes<[ORYONI012345, ORYONI012345]> ;

// 2 cycle on 2 of I0123 with ReleaseAtCycles
def ORYONWrite_2Cyc_I0123_I0123_RC :
SchedWriteRes<[ORYONI0123, ORYONI0123]> {
  let Latency = 2;
  let ReleaseAtCycles = [2,2];
}

// 2 cycle on 2 of I012345
def ORYONWrite_2Cyc_I012345_I012345_RC :
SchedWriteRes<[ORYONI012345, ORYONI012345]> {
  let Latency = 2;
  let ReleaseAtCycles = [2,2];
}

// 3 cycle on 2 of I45
def ORYONWrite_3Cyc_I45_I45_RC :
SchedWriteRes<[ORYONI45, ORYONI45]> {
  let Latency = 3;
  let ReleaseAtCycles = [2,2];
}

// 3 cycle on I45
def ORYONWrite_3Cyc_I45 : SchedWriteRes<[ORYONI45]> {
  let Latency = 3;
}

// 7 cycle on I2 32-bit integer division
def ORYONWrite_7Cyc_I2_RC : SchedWriteRes<[ORYONI2]> {
  let Latency = 7;
  let ReleaseAtCycles = [2];
}

// 9 cycle on I2 64-bit integer division
def ORYONWrite_9Cyc_I2_RC : SchedWriteRes<[ORYONI2]> {
  let Latency = 9;
  let ReleaseAtCycles = [2];
}

// LSU resource definition
// need to define WriteLDAdr, WriteAdrAdr, WriteLDHi, WriteSTX
// 4 cycle on LS(P6789)
def ORYONWrite_4Cyc_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 4;
}

// 4 cycle for Post/Pre inc/dec access, also covers all pair loads Post/Pre
def ORYONWrite_4Cyc_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 4;
}

// 5 (4+1) for VXU SIMD access/could also include FP
// resource might not be correct, as VXU resource not included
def ORYONWrite_5Cyc_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
}

def ORYONWrite_5Cyc_2Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def ORYONWrite_5Cyc_3Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

def ORYONWrite_5Cyc_4Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 4;
}

def ORYONWrite_5Cyc_5Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 5;
}

def ORYONWrite_5Cyc_6Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 6;
}

def ORYONWrite_5Cyc_8Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 8;
}

def ORYONWrite_5Cyc_10Uops_LD : SchedWriteRes<[ORYONLD]> {
  let Latency = 5;
  let NumMicroOps = 10;
}

// 6 cycle for Post/Pre inc/dec access
def ORYONWrite_5Cyc_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
}

def ORYONWrite_5Cyc_2Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def ORYONWrite_5Cyc_3Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

def ORYONWrite_5Cyc_4Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 4;
}

def ORYONWrite_5Cyc_5Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 5;
}

def ORYONWrite_5Cyc_6Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 6;
}

def ORYONWrite_5Cyc_8Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 8;
}

def ORYONWrite_5Cyc_10Uops_LD_I012345 : SchedWriteRes<[ORYONLD, ORYONI012345]> {
  let Latency = 5;
  let NumMicroOps = 10;
}

// 1 cycle for all generic stores
def ORYONWrite_1Cyc_ST : SchedWriteRes<[ORYONST]>;

def ORYONWrite_1Cyc_2Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 2;
}

def ORYONWrite_1Cyc_3Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 3;
}

def ORYONWrite_1Cyc_4Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 4;
}

def ORYONWrite_1Cyc_5Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 5;
}

def ORYONWrite_1Cyc_6Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 6;
}

def ORYONWrite_1Cyc_8Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 8;
}

def ORYONWrite_1Cyc_10Uops_ST : SchedWriteRes<[ORYONST]> {
  let NumMicroOps = 10;
}

// 1 cycle for neon write: float + ASIMD with Post/Pre Inc/Dec access
// also includes Pair store until further informed
def ORYONWrite_1Cyc_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 3;
}

def ORYONWrite_1Cyc_2Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 2;
}

def ORYONWrite_1Cyc_3Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 3;
}

def ORYONWrite_1Cyc_4Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 4;
}

def ORYONWrite_1Cyc_5Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 5;
}

def ORYONWrite_1Cyc_6Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 6;
}

def ORYONWrite_1Cyc_8Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 8;
}

def ORYONWrite_1Cyc_10Uops_ST_I012345 : SchedWriteRes<[ORYONST, ORYONI012345]> {
  let NumMicroOps = 10;
}

// VXU resource definition

// I2V instruction has 1 uOp
// I2v with convert has 2 uOps
// all I2V, V2I's throughputs are 2
// On VXU doc, p37 -- latencies and throughput
// P41, resource taken, P42, uOps
def ORYONWrite_I2V_4Cyc_I45 : SchedWriteRes<[ORYONI2V]> {
  let Latency = 4;
}

// inline a FCVT, so add one more uOp
def ORYONWrite_I2V_7Cyc_I45 : SchedWriteRes<[ORYONI2V]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

// V2I move instruction has 1/2 uOps, P42 in VXU doc
// Latency is 3, FCVT is also 3 cycle
// move + convert is 6 (3+3) cycles
// throughput is 2
def ORYONWrite_V2I_3Cyc_FP01 : SchedWriteRes<[ORYONV2I]> {
  let Latency = 3;
}

// inline a FCVT, so add one more uOp
def ORYONWrite_V2I_6Cyc_FP01 : SchedWriteRes<[ORYONV2I]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def ORYONWrite_V2V_2Cyc_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 2;
}

def ORYONWrite_V2V_3Cyc_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 3;
}

def ORYONWrite_V2V_6Cyc_FP01 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def ORYONWrite_4Cyc_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 4;
}

def ORYONWrite_3Cyc_FP0 : SchedWriteRes<[ORYONFP0]> {
  let Latency = 3;
}

def ORYONWrite_3Cyc_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 3;
}

def ORYONWrite_3Cyc_2Uops_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

def ORYONWrite_2Cyc_FP0123 : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 2;
}

def ORYONWrite_2Cyc_FP01 : SchedWriteRes<[ORYONFP01]> {
  let Latency = 2;
}

// 2 cycle on FP1
def ORYONWrite_2Cyc_FP1 : SchedWriteRes<[ORYONFP1]> {
  let Latency = 2;
}

// 3 cycle on FP1
def ORYONWrite_3Cyc_FP1 : SchedWriteRes<[ORYONFP1]> {
  let Latency = 3;
}

// 4 cycle , 0.5 throughput on FP1
def ORYONWrite_4Cyc_FP1_RC4 : SchedWriteRes<[ORYONFP1]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
}

// 5 cycle , 1 throughput on FP1
def ORYONWrite_5Cyc_FP1 : SchedWriteRes<[ORYONFP1]> {
  let Latency = 5;
}

// 8 cycle , 2 throughput on FP0123
def ORYONWrite_8Cyc_FP0123_RC : SchedWriteRes<[ORYONFP0123]> {
  let Latency = 8;
  let ReleaseAtCycles = [2];
}

def ORYONWrite_6Cyc_FP3 : SchedWriteRes<[ORYONFP3]> {
  let Latency = 6;
}

def ORYONWrite_7Cyc_FP3 : SchedWriteRes<[ORYONFP3]> {
  let Latency = 7;
}

def ORYONWrite_8Cyc_FP3 : SchedWriteRes<[ORYONFP3]> {
  let Latency = 8;
}

def ORYONWrite_9Cyc_FP3 : SchedWriteRes<[ORYONFP3]> {
  let Latency = 9;
}

def ORYONWrite_10Cyc_FP3 : SchedWriteRes<[ORYONFP3]> {
  let Latency = 10;
}

def ORYONWrite_8Cyc_FP3_RC : SchedWriteRes<[ORYONFP3]> {
  let Latency = 8;
  let ReleaseAtCycles = [2];
}

def ORYONWrite_10Cyc_FP3_RC : SchedWriteRes<[ORYONFP3]> {
  let Latency = 10;
  let ReleaseAtCycles = [2];
}

def ORYONWrite_13Cyc_FP3_RC : SchedWriteRes<[ORYONFP3]> {
  let Latency = 13;
  let ReleaseAtCycles = [2];
}

def ORYONWrite_4Cyc_FP0123_RC :
SchedWriteRes<[ORYONFP0123]> {
  let Latency = 4;
  let ReleaseAtCycles = [2];
}

def ORYONWrite_4Cyc_FP0123_FP0123_RC :
SchedWriteRes<[ORYONFP0123, ORYONFP0123]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [2,2];
}

def ORYONWrite_4Cyc_FP0123_FP0123_FP0123_RC :
SchedWriteRes<[ORYONFP0123, ORYONFP0123, ORYONFP0123]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ReleaseAtCycles = [3,3,3];
}

def ORYONWrite_6Cyc_FP0123_FP0123_FP0123_FP0123_RC :
SchedWriteRes<[ORYONFP0123, ORYONFP0123, ORYONFP0123, ORYONFP0123]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ReleaseAtCycles = [6,6,6,6];
}

//===----------------------------------------------------------------------===//
// Instruction Tables in IXU
//===----------------------------------------------------------------------===//

//---
// Arithmetic Instructions
//---

//1, 1, 6
def : InstRW<[ORYONWrite_1Cyc_I012345],
            (instregex "^ADD(W|X)r(i|r|x)", "^SUB(W|X)r(i|r|x)")>;

//2,2,3
def : InstRW<[ORYONWrite_2Cyc_I012345_I012345_RC],
            (instregex "^ADD(W|X)rs", "^SUB(W|X)rs")>;

//1,1,4 alias CMP, CMN on page 75
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^ADDS(W|X)r(i|r|x)(64)?", "^SUBS(W|X)r(i|r|x)")>;

//2,2,2 alias CMP, CMN on page 75
def : InstRW<[ORYONWrite_2Cyc_I0123_I0123_RC],
            (instregex "^ADDS(W|X)rs", "^SUBS(W|X)rs")>;

//1,1,4
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^ADC(W|X)r","^SBC(W|X)r",
                       "^ADCS(W|X)r","^SBCS(W|X)r")>;

//1,1,2
def : InstRW<[ORYONWrite_1Cyc_2Uops_I01],
            (instrs ADR,ADRP)>;

//1,1,4
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^CSEL(W|X)r", "^CSINV(W|X)r",
                       "^CSNEG(W|X)r", "^CSINC(W|X)r")>;

//---
//Compare Instruciton
//---

// We have CCMP, CCMN as LLVM DAG node
// CMP is an alias of SUBS as above
// CMN is an alias of ADDS as above
// We also have no way to get shift compare node in LLVM
//2,2,1.5 CMP, CMN

//1,1,4
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^CCMP(W|X)(i|r)", "^CCMN(W|X)(i|r)")>;

//---
// Branch
//---

def : InstRW<[ORYONWrite_1Cyc_NONE], (instrs B)>;
def : InstRW<[ORYONWrite_1Cyc_I01], (instrs BL)>;
def : InstRW<[ORYONWrite_1Cyc_I01],
            (instrs Bcc, CBZW, CBZX, CBNZW, CBNZX, TBZW, TBZX, TBNZW, TBNZX)>;
def : InstRW<[ORYONWrite_1Cyc_I0], (instrs BR, BLR)>;
def : InstRW<[ORYONWrite_1Cyc_I0], (instrs RET)>;

// 3 uOp, 1 cycle for branch, 7 cycle for Authentication,
// 1 cycle for updating link register
// V8.3a PAC
def : InstRW<[ORYONWrite_9Cyc_I012],
            (instrs BLRAA, BLRAAZ, BLRAB, BLRABZ,
                    BRAA, BRAAZ, BRAB, BRABZ)>;
def : InstRW<[ORYONWrite_9Cyc_I012], (instrs RETAA, RETAB, ERETAA, ERETAB)>;

def : InstRW<[ORYONWrite_7Cyc_3Uops_I2], (instregex "^LDRAA", "^LDRAB")>;

// Logical Instructions
//---

//1,1,4 TST is an alias of ANDS
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^ANDS(W|X)r(i|r|x)", "^BICS(W|X)r(i|r|x)")>;

//2,2,2 TST shift is an alias
def : InstRW<[ORYONWrite_2Cyc_I0123_I0123_RC],
            (instregex "^ANDS(W|X)rs", "^BICS(W|X)rs")>;

//1,1,6
def : InstRW<[ORYONWrite_1Cyc_I012345],
            (instregex "^AND(W|X)r(i|r|x)", "^EOR(W|X)r(i|r|x)",
                       "^ORR(W|X)r(i|r|x)", "^BIC(W|X)r(i|r|x)",
                       "^EON(W|X)r(i|r|x)", "^ORN(W|X)r(i|r|x)")>;

//2,2,3
def : InstRW<[ORYONWrite_2Cyc_I012345_I012345_RC],
            (instregex "^AND(W|X)rs", "^EOR(W|X)rs", "^ORR(W|X)rs",
                       "^BIC(W|X)rs", "^EON(W|X)rs", "^ORN(W|X)rs")>;


//---
// Shift Instructions
//---

//1,1,6
def : InstRW<[ORYONWrite_1Cyc_I012345],
            (instregex "^ASRV(W|X)r", "^LSLV(W|X)r",
                       "^LSRV(W|X)r", "^RORV(W|X)r",
                       "RMIF")>;

//---
// Move-Data Bit-field and Sign_Extension Instructions
//---

//1,1,6
def : InstRW<[ORYONWrite_1Cyc_I012345],
            (instregex "^MOVK(W|X)i", "^MOVN(W|X)i",
                       "^MOVZ(W|X)i", "^SBFM(W|X)ri",
                       "^UBFM(W|X)ri", "^BFM(W|X)ri",
                       "^SXT(W|B|H|X)", "^UXT(H|B)")>;

// COPY instruction is an LLVM internal DAG node, needs further study
def : InstRW<[ORYONWrite_1Cyc_I012345], (instrs COPY)>;

//---
// Reverse Instructions
//---

//1,1,6
def : InstRW<[ORYONWrite_1Cyc_I012345],
            (instregex "^RBIT(W|X)r", "^REV(16|32|64)?(W|X)r")>;


//---
// Flag Manipulate Instructions
//---

//1,1,4
def : InstRW<[ORYONWrite_1Cyc_I0123],
            (instregex "^SETF8", "^SETF16", "^CFINV")>;

//---
// Miscellaneous Instructions
//---

//1,1,6
def : InstRW<[ORYONWrite_1Cyc_I012345],
              (instregex "^CLS(W|X)r$", "^CLZ(W|X)r$", "^EXTR(W|X)rri")>;


//---
// Multiply Instructions
//---

//1,3,2
def : InstRW<[ORYONWrite_3Cyc_I45],
            (instregex "^MADD(W|X)rrr", "^MSUB(W|X)rrr",
                       "^(S|U)MADDLrrr", "^(S|U)MSUBLrrr",
                       "^(S|U)MULHrr")>;

//---
// Divide Instructions
//---

def : InstRW<[ORYONWrite_7Cyc_I2_RC],
             (instregex "^(S|U)DIVWr")>;

def : InstRW<[ORYONWrite_9Cyc_I2_RC],
             (instregex "^(S|U)DIVXr")>;


//---
// Cryptgraphy Instructions
//
//1,3,1  on I2
def : InstRW<[ORYONWrite_3Cyc_I2],
            (instregex "^CRC32(B|H|W|X)rr", "^CRC32C(B|H|W|X)rr")>;

//---
// PAU instructions
//---

// on p47 of IXU document, we have 7 cycles for all PAU instructions
// here we just assume all signing and pauth instructions are 7 cycles
// assume all are 7 cycles here

// signing instrucitons
def : InstRW<[ORYONWrite_7Cyc_I2], (instrs PACIA, PACIB,
                                            PACDA, PACDB,
                                            PACIZA, PACIZB,
                                            PACDZA, PACDZB,
                                            PACGA)>;
// authentication instrucitons
def : InstRW<[ORYONWrite_7Cyc_I2], (instrs AUTIA, AUTIB,
                                            AUTDA, AUTDB,
                                            AUTIZA, AUTIZB,
                                            AUTDZA, AUTDZB)>;
def : InstRW<[ORYONWrite_7Cyc_I2], (instrs XPACI, XPACD)>;

//===----------------------------------------------------------------------===//
// Instruction Tables in LSU
//===----------------------------------------------------------------------===//

// 4 cycle Load-to-use from L1D$
// Neon load with 5 cycle
// 6 cycle to STA ?
// STD cycle ?
// NEON STD + 2

// Load Instructions
// FP Load Instructions

// Load pair, immed pre-index, normal
// Load pair, immed pre-index, signed words
// Load pair, immed post-index, normal
// Load pair, immed post-index, signed words
// NOTE: Handled by WriteLD, WriteLDHi, WriteAdr.

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDNPDi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDNPQi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDNPSi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDNPWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDNPXi)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPDi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPQi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPSi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPSWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDPXi)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRBui)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRDui)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRHui)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRQui)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSui)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRDl)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRQl)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRWl)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRXl)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRBi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRHi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRXi)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRSBWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRSBXi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRSHWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRSHXi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDTRSWi)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPDpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPQpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPSpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPWpre)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRBpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRDpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRHpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRQpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRWpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRXpre)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSBWpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSBXpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSBWpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSBXpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSHWpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSHXpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSHWpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSHXpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRBBpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRBBpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRHHpre)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRHHpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPDpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPQpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPSpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPWpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345],
            (instrs LDPXpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRBpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRDpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRHpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRQpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRSpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRWpost)>;
def : InstRW<[ORYONWrite_4Cyc_LD_I012345], (instrs LDRXpost)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRBroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRDroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRHroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRHHroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRQroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSHWroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSHXroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRWroW)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRXroW)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRBroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRDroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRHHroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRHroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRQroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSHWroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRSHXroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRWroX)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDRXroX)>;

def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURBi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURBBi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURDi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURHi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURHHi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURQi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURXi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSBWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSBXi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSHWi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSHXi)>;
def : InstRW<[ORYONWrite_4Cyc_LD], (instrs LDURSWi)>;



// Store register, immed post-index
// NOTE: Handled by WriteST, ReadAdrBase

// Store register, immed pre-index
// NOTE: Handled by WriteST

// Store pair, immed post-index, W-form
// Store pair, immed post-indx, X-form
// Store pair, immed pre-index, W-form
// Store pair, immed pre-index, X-form
// NOTE: Handled by WriteSTP.

def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURBi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURBBi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURDi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURHi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURHHi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURQi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURSi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURWi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STURXi)>;

def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STTRBi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STTRHi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STTRWi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STTRXi)>;

def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STNPDi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STNPQi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STNPXi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STNPWi)>;

def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STPDi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STPQi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STPXi)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STPWi)>;

def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRBui)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRDui)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRHui)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRQui)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRXui)>;
def : InstRW<[ORYONWrite_1Cyc_ST], (instrs STRWui)>;

def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STPXpre, STPXpost)>;

def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instrs STRXpre, STRXpost)>;

def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instrs STRXroW, STRXroX)>;

// ASIMD Load instructions, 4 cycle access + 2 cycle NEON access
// ASIMD load, 1 element, multiple, 1 reg, D-form 1uOps
// ASIMD load, 1 element, multiple, 1 reg, Q-form 1uOps
def : InstRW<[ORYONWrite_5Cyc_LD],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;

def : InstRW<[ORYONWrite_5Cyc_LD_I012345],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, D-form 3 uOps
// ASIMD load, 1 element, multiple, 2 reg, Q-form 2 uOps
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD],
            (instregex "^LD1Twov(8b|4h|2s|1d)$")>;

def : InstRW<[ORYONWrite_5Cyc_2Uops_LD],
            (instregex "^LD1Twov(16b|8h|4s|2d)$")>;

def : InstRW<[ORYONWrite_5Cyc_3Uops_LD_I012345],
            (instregex "^LD1Twov(8b|4h|2s|1d)_POST$")>;

def : InstRW<[ORYONWrite_5Cyc_2Uops_LD_I012345],
            (instregex "^LD1Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, D-form 4 uOps
// ASIMD load, 1 element, multiple, 3 reg, Q-form 3 uOps
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD],
            (instregex "^LD1Threev(8b|4h|2s|1d)$")>;

def : InstRW<[ORYONWrite_5Cyc_3Uops_LD],
            (instregex "^LD1Threev(16b|8h|4s|2d)$")>;

def : InstRW<[ORYONWrite_5Cyc_4Uops_LD_I012345],
            (instregex "^LD1Threev(8b|4h|2s|1d)_POST$")>;

def : InstRW<[ORYONWrite_5Cyc_3Uops_LD_I012345],
            (instregex "^LD1Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form 6 uOps
// ASIMD load, 1 element, multiple, 4 reg, Q-form 4 uOps
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD],
            (instregex "^LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD],
            (instregex "^LD1Fourv(16b|8h|4s|2d)$")>;

def : InstRW<[ORYONWrite_5Cyc_6Uops_LD_I012345],
            (instregex "^LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD_I012345],
            (instregex "^LD1Fourv(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, one lane, B/H/S 2uOps
// ASIMD load, 1 element, one lane, D     2UOps
def : InstRW<[ORYONWrite_5Cyc_2Uops_LD], (instregex "^LD1i(8|16|32|64)$")>;
def : InstRW<[ORYONWrite_5Cyc_2Uops_LD_I012345],
            (instregex "^LD1i(8|16|32|64)_POST$")>;

// ASIMD load, 1 element, all lanes, D-form, B/H/S 2uOps
// ASIMD load, 1 element, all lanes, D-form, D     2uOps
// ASIMD load, 1 element, all lanes, Q-form        2uOps
def : InstRW<[ORYONWrite_5Cyc_2Uops_LD],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_2Uops_LD_I012345],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form, B/H/S 3 uOps
// ASIMD load, 2 element, multiple, Q-form, D     4 uOps
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD],
            (instregex "^LD2Twov(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD],
            (instregex "^LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD_I012345],
            (instregex "^LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD_I012345],
            (instregex "^LD2Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, one lane, B/H           3 uOps
// ASIMD load, 2 element, one lane, S             3 uOps
// ASIMD load, 2 element, one lane, D             3 uOps
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD], (instregex "^LD2i(8|16|32|64)$")>;
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD_I012345],
            (instregex "^LD2i(8|16|32|64)_POST$")>;

// ASIMD load, 2 element, all lanes, D-form, B/H/S 3 uOps
// ASIMD load, 2 element, all lanes, D-form, D     3 uOps
// ASIMD load, 2 element, all lanes, Q-form        3 uOps
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_3Uops_LD_I012345],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, multiple, D-form, B/H/S  5 uOps
// ASIMD load, 3 element, multiple, Q-form, B/H/S  6 uOps
// ASIMD load, 3 element, multiple, Q-form, D      6 uOps
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD],
            (instregex "^LD3Threev(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD],
            (instregex "^LD3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD_I012345],
            (instregex "^LD3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD_I012345],
            (instregex "^LD3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, one lone, B/H            4 uOps
// ASIMD load, 3 element, one lane, S              4 uOps
// ASIMD load, 3 element, one lane, D              5 uOps
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD], (instregex "^LD3i(8|16|32)$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD], (instregex "^LD3i(64)$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD_I012345],
            (instregex "^LD3i(8|16|32)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD_I012345],
            (instregex "^LD3i(64)_POST$")>;

// ASIMD load, 3 element, all lanes, D-form, B/H/S 4 uOps
// ASIMD load, 3 element, all lanes, D-form, D     5 uOps
// ASIMD load, 3 element, all lanes, Q-form, B/H/S 4 uOps
// ASIMD load, 3 element, all lanes, Q-form, D     5 uOps
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD],
            (instregex "^LD3Rv(8b|4h|2s|16b|8h|4s)$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD],
            (instregex "^LD3Rv(1d|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_4Uops_LD_I012345],
            (instregex "^LD3Rv(8b|4h|2s|16b|8h|4s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD_I012345],
            (instregex "^LD3Rv(1d|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form, B/H/S  6 uOps
// ASIMD load, 4 element, multiple, Q-form, B/H/S  10 uOps
// ASIMD load, 4 element, multiple, Q-form, D      8 uOps
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD],
            (instregex "^LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_5Cyc_10Uops_LD],
            (instregex "^LD4Fourv(16b|8h|4s)$")>;
def : InstRW<[ORYONWrite_5Cyc_8Uops_LD],
            (instregex "^LD4Fourv(2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD_I012345],
            (instregex "^LD4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_10Uops_LD_I012345],
            (instregex "^LD4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_8Uops_LD_I012345],
            (instregex "^LD4Fourv(2d)_POST$")>;

// ASIMD load, 4 element, one lane, B/H            5 uOps
// ASIMD load, 4 element, one lane, S              5 uOps
// ASIMD load, 4 element, one lane, D              6 uOps
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD], (instregex "^LD4i(8|16|32)$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD], (instregex "^LD4i(64)$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD_I012345],
            (instregex "^LD4i(8|16|32)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD_I012345],
            (instregex "^LD4i(64)_POST$")>;

// ASIMD load, 4 element, all lanes, D-form, B/H/S    5 uOps
// ASIMD load, 4 element, all lanes, D-form, D        6 uOps
// ASIMD load, 4 element, all lanes, Q-form, B/H/S    5 uOps
// ASIMD load, 4 element, all lanes, Q-form, D        6 uOps
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD],
            (instregex "^LD4Rv(8b|4h|2s|16b|8h|4s)$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD],
            (instregex "^LD4Rv(1d|2d)$")>;
def : InstRW<[ORYONWrite_5Cyc_5Uops_LD_I012345],
            (instregex "^LD4Rv(8b|4h|2s|16b|8h|4s)_POST$")>;
def : InstRW<[ORYONWrite_5Cyc_6Uops_LD_I012345],
            (instregex "^LD4Rv(1d|2d)_POST$")>;

// ASIMD Store Instructions
// ASIMD store, 1 element, multiple, 1 reg, D-form    1 uOps
// ASIMD store, 1 element, multiple, 1 reg, Q-form    1 uops
def : InstRW<[ORYONWrite_1Cyc_ST],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_ST_I012345],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form    2 uOps
// ASIMD store, 1 element, multiple, 2 reg, Q-form    2 uOps
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST_I012345],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form    3 uOps
// ASIMD store, 1 element, multiple, 3 reg, Q-form    3 uOps
def : InstRW<[ORYONWrite_1Cyc_3Uops_ST],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_3Uops_ST_I012345],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form    4 uOps
// ASIMD store, 1 element, multiple, 4 reg, Q-form    4 uOps
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST_I012345],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane, B/H/S            2 uOps
// ASIMD store, 1 element, one lane, D                2 uOps
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST],
            (instregex "^ST1i(8|16|32|64)$")>;
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST_I012345],
            (instregex "^ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S    2 uOps
// ASIMD store, 2 element, multiple, Q-form, B/H/S    4 uOps
// ASIMD store, 2 element, multiple, Q-form, D        4 uOps
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST],
            (instregex "^ST2Twov(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST],
            (instregex "^ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST_I012345],
            (instregex "^ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST_I012345],
            (instregex "^ST2Twov(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane, B/H/S            2 uOps
// ASIMD store, 2 element, one lane, D                2 uOps
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST],
            (instregex "^ST2i(8|16|32|64)$")>;
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST_I012345],
            (instregex "^ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S    4 uOps
// ASIMD store, 3 element, multiple, Q-form, B/H/S    6 uOps
// ASIMD store, 3 element, multiple, Q-form, D        6 uOps
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST],
            (instregex "^ST3Threev(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_1Cyc_6Uops_ST],
            (instregex "^ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST_I012345],
            (instregex "^ST3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_6Uops_ST_I012345],
            (instregex "^ST3Threev(16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H              2 uOps
// ASIMD store, 3 element, one lane, S                2 uOps
// ASIMD store, 3 element, one lane, D                4 uOps
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST], (instregex "^ST3i(8|16|32)$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST], (instregex "^ST3i(64)$")>;
def : InstRW<[ORYONWrite_1Cyc_2Uops_ST_I012345],
            (instregex "^ST3i(8|16|32)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST_I012345],
            (instregex "^ST3i(64)_POST$")>;


// ASIMD store, 4 element, multiple, D-form, B/H/S    5 uOps
// ASIMD store, 4 element, multiple, Q-form, B/H/S    10 uOps
// ASIMD store, 4 element, multiple, Q-form, D        8 uOps
def : InstRW<[ORYONWrite_1Cyc_5Uops_ST],
            (instregex "^ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[ORYONWrite_1Cyc_10Uops_ST],
            (instregex "^ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[ORYONWrite_1Cyc_8Uops_ST],
            (instregex "^ST4Fourv(2d)$")>;
def : InstRW<[ORYONWrite_1Cyc_5Uops_ST_I012345],
            (instregex "^ST4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_10Uops_ST_I012345],
            (instregex "^ST4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_8Uops_ST_I012345],
            (instregex "^ST4Fourv(2d)_POST$")>;

// ASIMD store, 4 element, one lane, B/H              3 uOps
// ASIMD store, 4 element, one lane, S                3 uOps
// ASIMD store, 4 element, one lane, D                4 uOps
def : InstRW<[ORYONWrite_1Cyc_3Uops_ST], (instregex "^ST4i(8|16|32)$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST], (instregex "^ST4i(64)$")>;
def : InstRW<[ORYONWrite_1Cyc_3Uops_ST_I012345],
            (instregex "^ST4i(8|16|32)_POST$")>;
def : InstRW<[ORYONWrite_1Cyc_4Uops_ST_I012345],
            (instregex "^ST4i(64)_POST$")>;


//===----------------------------------------------------------------------===//
// Instruction Tables in VXU
//===----------------------------------------------------------------------===//
// all uOps are not clearly written in the VXU document

// I2V
def : InstRW<[ORYONWrite_I2V_4Cyc_I45], (instregex "^FMOV[HSD][WX]r", "^FMOVDXHighr")>;

// I2V with convert
def : InstRW<[ORYONWrite_I2V_7Cyc_I45], (instregex "^[SU]CVTF[SU][XW][HSD]ri")>;

// V2I
def : InstRW<[ORYONWrite_V2I_3Cyc_FP01], (instregex "^FMOV[WX][HSD]r", "FMOVXDHighr")>;

// V2I with convert 2nd [SU] necessary?
def : InstRW<[ORYONWrite_V2I_6Cyc_FP01], (instregex "^FCVT[AMNPZ][SU][SU][XW][HSD]r")>;

// float to float move immediate, row 7 in big chart
def : InstRW<[ORYONWrite_V2V_2Cyc_FP0123], (instregex "^FMOV[HSD]r")>;
def : InstRW<[ORYONWrite_V2V_2Cyc_FP0123], (instregex "^FMOV[HSD]i")>;

// float to float conversion within VXU, precision conversion
def : InstRW<[ORYONWrite_V2V_6Cyc_FP01], (instregex "^FJCVTZS")>;
def : InstRW<[ORYONWrite_V2V_3Cyc_FP0123], (instregex "^FCVT[HSD][HSD]r",
                                                       "^FRINT(A|I|M|N|P|X|Z)(Sr|Dr)")>;

// floating comparison write to NZCV
def : InstRW<[ORYONWrite_2Cyc_FP01], (instregex "^FCMP(E)?[HSD]r[ir]")>;
def : InstRW<[ORYONWrite_2Cyc_FP01], (instregex "^FCCMP(E)?[HSD]rr")>;

// floating point conditional select
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^FCSEL")>;

// floating multiply-add
def : InstRW<[ORYONWrite_4Cyc_FP0123], (instregex "^(F|FN)MADD", "^(F|FN)MSUB")>;

// floating unary, cycle/throughput? xls row14
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^F(ABS|NEG)[SD]r")>;

//floating division/square root
def : InstRW<[ORYONWrite_7Cyc_FP3], (instregex "^FDIVHrr")>;
def : InstRW<[ORYONWrite_8Cyc_FP3], (instregex "^FDIVSrr")>;
def : InstRW<[ORYONWrite_10Cyc_FP3], (instregex "^FDIVDrr")>;

def : InstRW<[ORYONWrite_8Cyc_FP3_RC], (instregex "^FSQRTHr")>;
def : InstRW<[ORYONWrite_10Cyc_FP3_RC], (instregex "^FSQRTSr")>;
def : InstRW<[ORYONWrite_13Cyc_FP3_RC], (instregex "^FSQRTDr")>;

//==========
// SIMD move instructions
//==========

// ASIMD DUP element
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^DUPv.+lane")>;
// ASIMD DUP general thoughput undecided, 3? FP0123
// VXU doc, p42, 2 uOps
def : InstRW<[ORYONWrite_3Cyc_2Uops_FP0123], (instregex "^DUPv.+gpr")>;

// ASIMD insert, element to element
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^INSv.+lane")>;
// ASIMD insert,  gen reg 3? FP0123?
def : InstRW<[ORYONWrite_3Cyc_2Uops_FP0123], (instregex "^INSv.+gpr")>;

// ASIMD move, FP immed
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^FMOVv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^[SU]MOVv")>;

//==========
// SIMD arithmetic instructions
//==========
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^ADDv", "^SUBv",
                                         "^BIFv", "^BITv", "^BSLv",
                                         "^ANDv", "^BICv", "^EORv",
                                         "^ORRv", "^ORNv")>;


def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^FABDv", "^FADDv", "^FSUBv")>;

// floating division
def : InstRW<[ORYONWrite_6Cyc_FP3], (instregex "^FDIVv.*16$")>;
def : InstRW<[ORYONWrite_7Cyc_FP3], (instregex "^FDIVv.*32$")>;
def : InstRW<[ORYONWrite_9Cyc_FP3], (instregex "^FDIVv.*64$")>;

def : InstRW<[ORYONWrite_4Cyc_FP0123], (instregex "^FMUL(X)?v",
                                                   "^FRECPSv", "^FRSQRTSv")>;

def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^MLAv","^MLSv", "^MULv",
                                                   "^PMULv", "UABAv")>;

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "SABAv", "SABDv",
                                                   "^(SH|UH)(ADD|SUB)v",
                                                   "^S(MAX|MIN)v",
                                                   "^(SQ|UQ)(ADD|SUB)v",
                                                   "^(SQ|SQR|UQ|UQR)SHLv",
                                                   "^(SR|UR)HADDv",
                                                   "^(SR|UR)SHLv",
                                                   "^UABDv",
                                                   "^U(MAX|MIN)v")>;
// IMAX or UMAX in the above line
//==========
// SIMD compare instructions
//==========

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^CMEQv","^CMGEv","^CMGTv",
                                                   "^CMLEv","^CMLTv", "^CMHIv",
                                                   "^CMHSv",
                                                   "^FCMEQv", "^FCMGEv",
                                                   "^FCMGTv", "^FCMLEv",
                                                   "^FCMLTv",
                                                   "^FACGEv", "^FACGTv")>;

//==========
// SIMD widening and narrowing arithmetic instructions
//==========
// NO need to list ADDHN2, RADDHN2, RSUBHN2 as they are not distinguished
// from ADDHN, RADDHN, RSUBHN in td file(v16i8, v8i16, v4i32).
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^ADDHNv",
                                                   "^SUBHNv",
                                                   "^RADDHNv",
                                                   "^RSUBHNv",
                                                   "^SABD(L|L2)v", "^UABD(L|L2)v",
                                                   "^(S|U)(ADD|SUB)(L|L2|W|W2)v")>;

def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^PMUL(L|L2)v","^SABA(L|L2)v",
                                                   "^(S|U|SQ)(MLA|MSL|MUL)(L|L2)v")>;

//==========
// SIMD unary arithmetic instructions
//==========
//^MVNv is an alias of ^NOTv
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^ABSv", "^CLSv","^CLZv", "^CNTv",
                                                   "^NEGv", "^NOTv",
                                                   "^RBITv", "^REV(16|32|64)v",
                                                   "^SQ(ABS|NEG)v", "^SQ(XT|XTU)(N|N2)v",
                                                   "^(SU|US)QADDv",
                                                   "^UQXT(N|N2)v", "^XTN2?v")>;

def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^FCVT(L|L2|N|N2|XN|XN2)v",
                                                   "^FRINT[AIMNPXZ]v",
                                                   "^FRSQRTEv",
                                                   "^(S|U)ADALPv",
                                                   "^(S|U)ADDLPv")>;


def : InstRW<[ORYONWrite_3Cyc_FP0], (instregex "^URECPEv", "^URSQRTEv",
                                                "^FRECPEv", "^FRECPXv")>;

def : InstRW<[ORYONWrite_8Cyc_FP3_RC], (instregex "^FSQRTv.*16$")>;
def : InstRW<[ORYONWrite_10Cyc_FP3_RC], (instregex "^FSQRTv.*32$")>;
def : InstRW<[ORYONWrite_13Cyc_FP3_RC], (instregex "^FSQRTv.*64$")>;

//==========
// SIMD binary elememt arithmetic instructions
//==========

def : InstRW<[ORYONWrite_4Cyc_FP0123], (instregex "^FMLAv", "^FMLSv")>;

def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex  "^SQDMULHv",
                                                   "^SQRD(MLA|MLS|MUL)Hv")>;

//==========
// SIMD permute instructions
//==========

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^EXTv", "^TRN(1|2)v",
                                                   "^UZP(1|2)v", "^ZIP(1|2)v")>;

//==========
// SIMD immediate instructions
//==========

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex  "^MOVIv", "^MVNIv")>;

//==========
// SIMD shift(immediate) instructions
//==========
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^RSHR(N|N2)v", "^SHLv",
                                                   "^(SHL|SHR)(N|N2)v",
                                                   "^SLIv",
                                                   "^(SQ|SQR)SHR(U)?(N|N2)v",
                                                   "^(UQ|UQR)SHR(N|N2)v",
                                                   "^SQSHLUv",
                                                   "^SRIv",
                                                   "^(S|SR|U|UR)SHRv",
                                                   "^(S|SR|U|UR)SRAv",
                                                   "^(S|U)SHL(L|L2)v")>;

//==========
// SIMD floating-point and integer conversion instructions
//==========
// same as above conversion

//==========
// SIMD reduce (acoss vector lanes) instructions
//==========

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^ADDVv",
                                                   "^(FMAX|FMIN)(V|NMV)v",
                                                   "^(S|U)ADDLVv",
                                                   "^(S|U)(MAX|MIN)Vv")>;
//==========
// SIMD pairwise arithmetic instructions
//==========

def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^ADDPv", "^FADDPv",
                                                   "^(FMAX|FMIN)(NMP|P)v",
                                                   "^(S|U)(MIN|MAX)Pv")>;
//==========
// SIMD dot prodcut instructions
//==========

def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^(U|S)DOTv")>;

//==========
// SIMD table lookup instructions
//==========
// TBL 1-reg/2-reg; TBX 1-reg, 1uOp, throughput=4 latency=2
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instrs TBLv8i8One, TBLv16i8One,
                                                TBXv8i8One, TBXv16i8One,
                                                TBLv8i8Two, TBLv16i8Two)>;

// TBL 3-reg/4-reg, 3uops, throughtput=4/3=1.33 latency=4
def : InstRW<[ORYONWrite_4Cyc_FP0123_FP0123_FP0123_RC],
            (instrs TBLv8i8Three, TBLv16i8Three,
                    TBLv8i8Four, TBLv16i8Four)>;


// TBX 2-reg 2 uOps, throughput=2 latency=4
def : InstRW<[ORYONWrite_4Cyc_FP0123_FP0123_RC], (instrs TBXv8i8Two, TBXv16i8Two)>;

// TBX 3-reg/4-reg, 4uOps, throughput=1, latency=6
def : InstRW<[ORYONWrite_6Cyc_FP0123_FP0123_FP0123_FP0123_RC],
            (instrs TBXv8i8Three, TBXv16i8Three,
                    TBXv8i8Four, TBXv16i8Four)>;


//==========
// SIMD complex number arithmetic instructions
//==========

def : InstRW<[ORYONWrite_4Cyc_FP0123], (instregex "^FCADDv", "^FCMLAv")>;

//==========
// SIMD cryptographic instructions
//==========
// 3,4 on IMLA, CRYP
def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^AES[DE]",
                                                   "^SM3(TT1|TT2)(A|B)")>;

// 2,4 on CRYP
def : InstRW<[ORYONWrite_2Cyc_FP0123], (instregex "^AESI?MC",
                                                   "^EOR3",
                                                   "^RAX1",
                                                   "^XAR",
                                                   "^BCAX",
                                                   "^SM3SS1",
                                                   "^SM3PART(W1|W2)")>;
// 5,1 on CRYP
def : InstRW<[ORYONWrite_5Cyc_FP1], (instregex "^SM4E",
                                                "^SM4EKEY")>;

// 2,1 on CRYP
def : InstRW<[ORYONWrite_2Cyc_FP1], (instregex "^SHA1(H|SU0|SU1)",
                                                "^SHA256SU0",
                                                "^SHA512(SU0|SU1)")>;

// 3,1 on CRYP
def : InstRW<[ORYONWrite_3Cyc_FP1], (instregex "^SHA256SU1",
                                                "^SHA512(H|H2)")>;

// 4,0.25 on CRYP
def : InstRW<[ORYONWrite_4Cyc_FP1_RC4], (instregex "^SHA1(C|P|M)",
                                                "^SHA256(H|H2)")>;

//==========
// SIMD v8.6 instructions
//==========
// 4,2 on IMLA
def : InstRW<[ORYONWrite_4Cyc_FP0123_RC], (instregex "^(S|U|US)MMLA$")>;

// 4,0.5 on IMLA
def : InstRW<[ORYONWrite_8Cyc_FP0123_RC], (instregex "^BFMMLA$")>;

// 4,0.5 on IMLA
def : InstRW<[ORYONWrite_8Cyc_FP0123_RC], (instregex "^BFMLAL(B|T)")>;

// 3,4
def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^(US|SU)DOTv")>;

// 3,1
def : InstRW<[ORYONWrite_4Cyc_FP0123], (instregex "^BF(16)?DOTv")>;

// 3,4
def : InstRW<[ORYONWrite_3Cyc_FP0123], (instregex "^BFCVT(N|N2)?$")>;


} // SchedModel = OryonModel
