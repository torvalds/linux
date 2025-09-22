//==- ARMScheduleM55.td - Arm Cortex-M55 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for the Arm Cortex-M55 processors.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// Cortex-M55 is a lot like the M4/M33 in terms of scheduling. It technically
// has an extra pipeline stage but that is unimportant for scheduling, just
// starting our model a stage later. The main points of interest over an
// Cortex-M4 are MVE instructions and the ability to dual issue thumb1
// instructions.
//
//
// MVE
//
// The EPU pipelines now include both MVE and FP instructions. It has four
// pipelines across 4 stages (E1-E4). These pipelines are "control",
// "load/store", "integer" and "float/mul". We start the schedule at E2 to line
// up with the rest of the pipeline we model, and take the latency as the time
// between reading registers (almost always in E2) and register write (or
// forward, if it allows it). This mean that a lot of instructions (including
// loads) actually take 1 cycle (amazingly).
//
// Each MVE instruction needs to take 2 beats, each performing 64bits of the
// 128bit vector operation. So long as the beats are to different pipelines,
// the execution of the first-beat-of-the-second-instruction can overlap with
// the second-beat-of-the-first. For example a sequence of VLDR;VADD;VMUL;VSTR
// can look like this is a pipeline:
//          1    2    3    4    5
// LD/ST  : VLDR VLDR      VSTR VSTR
// INTEGER:      VADD VADD
// FP/MUL :           VMUL VMUL
//
// But a sequence of VLDR;VLDRB;VADD;VSTR because the loads cannot overlap,
// looks like:
//          1     2     3     4     5    6
// LD/ST  : VLDR  VLDR  VLDRB VLDRB VSTR VSTR
// INTEGER:                   VADD  VADD
//
// For this schedule, we currently model latencies and pipelines well for each
// instruction. MVE instruction take two beats, modelled using
// ReleaseAtCycles=[2].
//
//
// Dual Issue
//
// Cortex-M55 can dual issue two 16-bit T1 instructions providing one is one of
// NOPs, ITs, Brs, ADDri/SUBri, UXTB/H, SXTB/H and MOVri's. NOPs and IT's are
// not relevant (they will not appear when scheduling), Brs are only at the end
// of the block. The others are more useful, and where the problems arise.
//
// The first problem comes from the fact that we will only be seeing Thumb2
// instructions at the point in the pipeline where we do the scheduling. The
// Thumb2SizeReductionPass has not been run yet. Especially pre-ra scheduling
// (where the scheduler has the most freedom) we can only really guess at which
// instructions will become thumb1 instructions. We are quite optimistic, and
// may get some things wrong as a result.
//
// The other problem is one of telling llvm what to do exactly. The way we
// attempt to meld this is:
//  Set IssueWidth to 2 to allow 2 instructions per cycle.
//  All instructions we cannot dual issue are "SingleIssue=1" (MVE/FP and T2
//    instructions)
//  We guess at another set of instructions that will become T1 instruction.
//    These become the primary instruction in a dual issue pair (the normal
//    one). These use normal resources and latencies, but set SingleIssue = 0.
//  We guess at another set of instructions that will be shrank down into T1 DI
//    instructions (add, sub, mov's, etc), which become the secondary. These
//    don't use a resource, and set SingleIssue = 0.
//
// So our guessing is a bit rough. It may be possible to improve this by moving
// T2SizeReduction pass earlier in the pipeline, for example, so that at least
// Post-RA scheduling sees what is T1/T2. It may also be possible to write a
// custom instruction matcher for more accurately guess at T1 instructions.


def CortexM55Model : SchedMachineModel {
  let MicroOpBufferSize = 0;      // Explicitly set to zero since M55 is in-order.
  let IssueWidth = 2;             // There is some dual-issue support in M55.
  let MispredictPenalty = 3;      // Default is 10
  let LoadLatency = 4;            // Default is 4
  let PostRAScheduler = 1;
  let FullInstRWOverlapCheck = 1;

  let CompleteModel = 0;
  let UnsupportedFeatures = [IsARM, HasNEON, HasDotProd, HasMatMulInt8, HasZCZ,
                             IsNotMClass, HasV8, HasV8_3a, HasTrustZone, HasDFB,
                             IsWindows];
}


let SchedModel = CortexM55Model in {

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.

// Modeling each pipeline as a ProcResource using the BufferSize = 0 since
// M55 is in-order.
def M55UnitALU : ProcResource<1> { let BufferSize = 0; } // Int ALU
def M55UnitVecALU : ProcResource<1> { let BufferSize = 0; } // MVE integer pipe
def M55UnitVecFPALU : ProcResource<1> { let BufferSize = 0; } // MVE float pipe
def M55UnitLoadStore : ProcResource<1> { let BufferSize = 0; } // MVE load/store pipe
def M55UnitVecSys : ProcResource<1> { let BufferSize = 0; } // MVE control/sys pipe

// Some VMOV's can go down either pipeline. FIXME: This M55Write2IntFPE2 is
// intended to model the VMOV taking either Int or FP for 2 cycles. It is not
// clear if the llvm scheduler is using it like we want though.
def M55UnitVecIntFP: ProcResGroup<[M55UnitVecALU, M55UnitVecFPALU]>;


//===----------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types which both map the ProcResources and
// set the latency.

//=====//
// ALU //
//=====//

// Generic writes for Flags, GRPs and other extra operands (eg post-inc, vadc flags, vaddlv etc)
def M55WriteLat0  : SchedWriteRes<[]>  { let Latency = 0; let NumMicroOps = 0; }
def M55WriteLat1  : SchedWriteRes<[]>  { let Latency = 1; let NumMicroOps = 0; }
def M55WriteLat2  : SchedWriteRes<[]>  { let Latency = 2; let NumMicroOps = 0; }

// DX instructions are ALU instructions that take a single cycle. The
// instructions that may be shrank to T1 (and can be dual issued) are
// SingleIssue = 0. The others are SingleIssue = 1.
let SingleIssue = 0, Latency = 1 in {
    def : WriteRes<WriteALU, [M55UnitALU]>;
    def : WriteRes<WriteCMP, [M55UnitALU]>;
    def : WriteRes<WriteBr, [M55UnitALU]>;
    def : WriteRes<WriteBrL, [M55UnitALU]>;
    def : WriteRes<WriteBrTbl, [M55UnitALU]>;
    def : WriteRes<WriteST, [M55UnitALU]>;
    def M55WriteDX_DI : SchedWriteRes<[M55UnitALU]>;
}
let SingleIssue = 1, Latency = 1 in {
    def : WriteRes<WritePreLd, [M55UnitALU]>;
    def M55WriteDX_SI : SchedWriteRes<[M55UnitALU]>;
}

def : InstRW<[M55WriteDX_SI], (instregex "t2BF[CI]", "t2CPS", "t2DBG",
          "t2MRS", "t2MSR", "t2SEL", "t2SG", "t2TT")>;
def : InstRW<[M55WriteDX_SI], (instregex "t2SUBS_PC_LR", "COPY")>;
def : InstRW<[M55WriteDX_SI], (instregex "t2CS(EL|INC|INV|NEG)")>;
// Thumb 2 instructions that could be reduced to a thumb 1 instruction and can
// be dual issued with one of the above. This list is optimistic.
def : InstRW<[M55WriteDX_DI], (instregex "t2ADDC?rr$", "t2ADDrr$",
           "t2ADDSrr$", "t2ANDrr$", "t2ASRr[ir]$", "t2BICrr$", "t2CMNzrr$",
           "t2CMPr[ir]$", "t2EORrr$", "t2LSLr[ir]$", "t2LSRr[ir]$", "t2MVNr$",
           "t2ORRrr$", "t2REV(16|SH)?$", "t2RORrr$", "t2RSBr[ir]$", "t2RSBSri$",
           "t2SBCrr$", "t2SUBS?rr$", "t2TEQrr$", "t2TSTrr$", "t2STRi12$",
           "t2STRs$", "t2STRBi12$", "t2STRBs$", "t2STRHi12$", "t2STRHs$",
           "t2STR_POST$", "t2STMIA$", "t2STMIA_UPD$", "t2STMDB$", "t2STMDB_UPD$")>;
def : InstRW<[M55WriteDX_DI], (instregex "t2SETPAN$", "tADC$", "tADDhirr$",
           "tADDrSP$", "tADDrSPi$", "tADDrr$", "tADDspi$", "tADDspr$", "tADR$",
           "tAND$", "tASRri$", "tASRrr$", "tBIC$", "tBKPT$", "tCBNZ$", "tCBZ$",
           "tCMNz$", "tCMPhir$", "tCMPi8$", "tCMPr$", "tCPS$", "tEOR$", "tHINT$",
           "tHLT$", "tLSLri$", "tLSLrr$", "tLSRri$", "tLSRrr$", "tMOVSr$",
           "tMUL$", "tMVN$", "tORR$", "tPICADD$", "tPOP$", "tPUSH$", "tREV$",
           "tREV16$", "tREVSH$", "tROR$", "tRSB$", "tSBC$", "tSETEND$",
           "tSTMIA_UPD$", "tSTRBi$", "tSTRBr$", "tSTRHi$", "tSTRHr$", "tSTRi$",
           "tSTRr$", "tSTRspi$", "tSUBrr$", "tSUBspi$", "tSVC$", "tTRAP$",
           "tTST$", "tUDF$")>;
def : InstRW<[M55WriteDX_DI], (instregex "tB$", "tBLXNSr$", "tBLXr$", "tBX$",
           "tBXNS$", "tBcc$")>;


// CX instructions take 2 (or more) cycles. Again T1 instructions may be dual
// issues (SingleIssue = 0)
let SingleIssue = 0, Latency = 2 in {
    def : WriteRes<WriteLd, [M55UnitALU]>;
    def M55WriteCX_DI  : SchedWriteRes<[M55UnitALU]>;
}
let SingleIssue = 1, Latency = 2 in {
    def : WriteRes<WriteALUsi, [M55UnitALU]>;
    def : WriteRes<WriteALUsr, [M55UnitALU]>;
    def : WriteRes<WriteALUSsr, [M55UnitALU]>;
    def : WriteRes<WriteCMPsi, [M55UnitALU]>;
    def : WriteRes<WriteCMPsr, [M55UnitALU]>;
    def : WriteRes<WriteDIV, [M55UnitALU]>;
    def M55WriteCX_SI  : SchedWriteRes<[M55UnitALU]>;
}

def : SchedAlias<WriteMUL16, M55WriteCX_SI>;
def : SchedAlias<WriteMUL32, M55WriteCX_SI>;
def : SchedAlias<WriteMUL64Lo, M55WriteCX_SI>;
def : WriteRes<WriteMUL64Hi, []> { let Latency = 2; }
def : SchedAlias<WriteMAC16, M55WriteCX_SI>;
def : SchedAlias<WriteMAC32, M55WriteCX_SI>;
def : SchedAlias<WriteMAC64Lo, M55WriteCX_SI>;
def : WriteRes<WriteMAC64Hi, []> { let Latency = 2; }

def : InstRW<[M55WriteCX_SI], (instregex "t2CDP", "t2CLREX", "t2[DI][MS]B",
           "t2MCR", "t2MOVSs[ir]", "t2MRC", "t2MUL", "t2STC")>;
def : InstRW<[M55WriteCX_SI], (instregex "t2Q", "t2[SU](ADD|ASX|BFX|DIV)",
           "t2[SU]H(ADD|ASX|SUB|SAX)", "t2SM[LM]", "t2S(SAT|SUB|SAX)", "t2UQ",
           "t2USA", "t2USUB", "t2UXTA[BH]")>;
def : InstRW<[M55WriteCX_SI], (instregex "t2LD[AC]", "t2STL", "t2STRD")>;
def : InstRW<[M55WriteCX_SI], (instregex "MVE_[SU]Q?R?SH[LR]$")>;
def : InstRW<[M55WriteCX_SI, M55WriteLat2], (instregex "MVE_ASRL", "MVE_LSLL",
            "MVE_LSRL", "MVE_[SU]Q?R?SH[LR]L")>;
// This may be higher in practice, but that likely doesn't make a difference
// for scheduling
def : InstRW<[M55WriteCX_SI], (instregex "t2CLRM")>;

def : InstRW<[M55WriteCX_DI], (instregex "t2LDR[BH]?i12$", "t2LDRS?[BH]?s$",
           "t2LDM")>;
def : InstRW<[M55WriteCX_DI], (instregex "tLDM", "tLDRBi$", "tLDRBr$",
           "tLDRHi$", "tLDRHr$", "tLDRSB$", "tLDRSH$", "tLDRi$", "tLDRpci$",
           "tLDRr$", "tLDRspi$")>;

// Dual Issue instructions
let Latency = 1, SingleIssue = 0 in {
    def : WriteRes<WriteNoop, []>;
    def M55WriteDI : SchedWriteRes<[]>;
}

def : InstRW<[M55WriteDI], (instregex "tADDi[38]$", "tSUBi[38]$", "tMOVi8$",
           "tMOVr$", "tUXT[BH]$", "tSXT[BH]$")>;
// Thumb 2 instructions that could be reduced to a dual issuable Thumb 1
// instruction above.
def : InstRW<[M55WriteDI], (instregex "t2ADDS?ri$", "t2MOV[ir]$", "t2MOVi16$",
           "t2MOVr$", "t2SUBS?ri$", "t2[US]XT[BH]$")>;
def : InstRW<[M55WriteDI], (instregex "t2IT", "IT")>;


def : InstRW<[M55WriteLat0], (instregex "t2LoopDec")>;

// Forwarding

// No forwarding in the ALU normally
def : ReadAdvance<ReadALU, 0>;
def : ReadAdvance<ReadALUsr, 0>;
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 0>;

//=============//
// MVE and VFP //
//=============//

// The Writes that take ReleaseAtCycles=[2] are MVE instruction, the others VFP.

let SingleIssue = 1, Latency = 1 in {
  def M55WriteLSE2 : SchedWriteRes<[M55UnitLoadStore]>;
  def M55WriteIntE2 : SchedWriteRes<[M55UnitVecALU]>;
  def M55WriteFloatE2 : SchedWriteRes<[M55UnitVecFPALU]>;
  def M55WriteSysE2 : SchedWriteRes<[M55UnitVecSys]>;

  def M55Write2LSE2 : SchedWriteRes<[M55UnitLoadStore]> { let ReleaseAtCycles=[2]; }
  def M55Write2IntE2 : SchedWriteRes<[M55UnitVecALU]> { let ReleaseAtCycles=[2]; }
  def M55Write2FloatE2 : SchedWriteRes<[M55UnitVecFPALU]> { let ReleaseAtCycles=[2]; }
  def M55Write2IntFPE2 : SchedWriteRes<[M55UnitVecIntFP]> { let ReleaseAtCycles=[2]; }
}

let SingleIssue = 1, Latency = 2 in {
  def M55WriteLSE3 : SchedWriteRes<[M55UnitLoadStore]>;
  def M55WriteIntE3 : SchedWriteRes<[M55UnitVecALU]>;
  def M55WriteFloatE3 : SchedWriteRes<[M55UnitVecFPALU]>;

  def M55Write2LSE3 : SchedWriteRes<[M55UnitLoadStore]> { let ReleaseAtCycles=[2]; }
  def M55Write2IntE3 : SchedWriteRes<[M55UnitVecALU]> { let ReleaseAtCycles=[2]; }
  def M55Write2FloatE3 : SchedWriteRes<[M55UnitVecFPALU]> { let ReleaseAtCycles=[2]; }
}

let SingleIssue = 1, Latency = 3 in {
  def M55Write2IntE3Plus1 : SchedWriteRes<[M55UnitVecALU]> { let ReleaseAtCycles=[2]; }

  // Same as M55Write2IntE3/M55Write2FloatE3 above, but longer latency and no forwarding into stores
  def M55Write2IntE4NoFwd : SchedWriteRes<[M55UnitVecALU]> { let ReleaseAtCycles=[2]; }
  def M55Write2FloatE4NoFwd : SchedWriteRes<[M55UnitVecFPALU]> { let ReleaseAtCycles=[2]; }
}
let SingleIssue = 1, Latency = 4 in {
  def M55Write2IntE3Plus2 : SchedWriteRes<[M55UnitVecALU]> { let ReleaseAtCycles=[2]; }
  def M55WriteFloatE3Plus2 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 9 in {
  def M55WriteFloatE3Plus7 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 15 in {
  def M55WriteFloatE3Plus13 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 16 in {
  def M55WriteFloatE3Plus14 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 21 in {
  def M55WriteFloatE3Plus19 : SchedWriteRes<[M55UnitVecFPALU]>;
}
// VMUL (Double precision) + VADD (Double precision)
let SingleIssue = 1, Latency = 24 in {
  def M55WriteFloatE3Plus22 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 30 in {
  def M55WriteFloatE3Plus28 : SchedWriteRes<[M55UnitVecFPALU]>;
}
let SingleIssue = 1, Latency = 36 in {
  def M55WriteFloatE3Plus34 : SchedWriteRes<[M55UnitVecFPALU]>;
}

def M55Read0 : SchedReadAdvance<0>;
def M55Read1 : SchedReadAdvance<1, [M55Write2LSE3, M55Write2IntE3, M55Write2FloatE3]>;
def M55GatherQRead : SchedReadAdvance<-4>;

// MVE instructions

// Loads and Stores of different kinds

// Normal loads
def : InstRW<[M55Write2LSE2], (instregex "MVE_VLDR(B|H|W)(S|U)(8|16|32)$")>;
// Pre/post inc loads
def : InstRW<[M55WriteLat1, M55Write2LSE2], (instregex "MVE_VLDR(B|H|W)(S|U)(8|16|32)_(post|pre)$")>;
// Gather loads
def : InstRW<[M55Write2LSE3, M55Read0, M55GatherQRead], (instregex "MVE_VLDR(B|H|W|D)(S|U)(8|16|32|64)_rq")>;
def : InstRW<[M55Write2LSE3, M55GatherQRead], (instregex "MVE_VLDR(B|H|W|D)(S|U)(8|16|32|64)_qi$")>;
def : InstRW<[M55WriteLat1, M55Write2LSE3, M55GatherQRead], (instregex "MVE_VLDR(W|D)U(32|64)_qi_pre$")>;
// Interleaving loads
def : InstRW<[M55Write2LSE2], (instregex "MVE_VLD[24][0-3]_(8|16|32)$")>;
// Interleaving loads with wb
def : InstRW<[M55Write2LSE2, M55WriteLat1], (instregex "MVE_VLD[24][0-3]_(8|16|32)_wb$")>;

// Normal stores
def : InstRW<[M55Write2LSE2, M55Read1], (instregex "MVE_VSTR(B|H|W)U?(8|16|32)$")>;
// Pre/post inc stores
def : InstRW<[M55Write2LSE2, M55Read1], (instregex "MVE_VSTR(B|H|W)U?(8|16|32)_(post|pre)$")>;
// Scatter stores
def : InstRW<[M55Write2LSE2, M55Read0, M55Read0, M55GatherQRead], (instregex "MVE_VSTR(B|H|W|D)(8|16|32|64)_rq")>;
def : InstRW<[M55Write2LSE2, M55Read0, M55GatherQRead], (instregex "MVE_VSTR(B|H|W|D)(8|16|32|64)_qi")>;
// Interleaving stores
def : InstRW<[M55Write2LSE2], (instregex "MVE_VST(2|4)")>;

// Integer pipe operations

def : InstRW<[M55Write2IntE3Plus1], (instregex "MVE_VABAV")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VABD(u|s)")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VABS(u|s)")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_VADC")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VADD(_qr_)?i")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VAND")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VBIC")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VBRSR")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VCADDi")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VCLS")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VCLZ")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_V(D|I)?W?DUP")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VEOR")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VHADD")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VHCADD")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VHSUB")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_V(MAX|MIN)A?(s|u)")>;
def : InstRW<[M55Write2IntE3Plus2], (instregex "MVE_V(MAX|MIN)A?V(s|u)8")>;
def : InstRW<[M55Write2IntE3Plus1], (instregex "MVE_V(MAX|MIN)A?V(s|u)16")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_V(MAX|MIN)A?V(s|u)32")>;
def : InstRW<[M55Write2IntE4NoFwd], (instregex "MVE_VMOVN")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VMOVL")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_VMULL[BT]p")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VMVN")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VNEG(u|s)")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VORN")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VORR")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VPSEL")>;
def : InstRW<[M55Write2IntE2], (instregex "MQPRCopy")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VQABS")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VQADD")>;
def : InstRW<[M55Write2IntE4NoFwd], (instregex "MVE_VQMOV")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VQNEG")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VSHL")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_V[QR]SHL")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_VQRSHL")>;
def : InstRW<[M55Write2IntE4NoFwd], (instregex "MVE_VQ?R?SHRU?N")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VSHR_")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_VRSHR_")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VQSUB")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VREV")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VRHADD")>;
def : InstRW<[M55Write2IntE3], (instregex "MVE_VSBC")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VSLI")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VSRI")>;
def : InstRW<[M55Write2IntE2], (instregex "MVE_VSUB(_qr_)?i")>;

// FP/Mul pipe operations.

def : InstRW<[M55Write2FloatE2], (instregex "MVE_VABDf")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VABSf")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VADDf")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VADD_qr_f")>;
def : InstRW<[M55Write2FloatE3, M55WriteLat1], (instregex "MVE_VADDLV")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VADDV")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VCADDf")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCMLA")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCMUL")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VCMP(i|s|u)", "MVE_VPTv(4|8|16)(i|s|u)")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VCMPf", "MVE_VPTv(4|8)f")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCVTf16(u|s)16")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCVTf32(u|s)32")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCVT(u|s)16f16")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCVT(u|s)32f32")>;
def : InstRW<[M55Write2FloatE4NoFwd], (instregex "MVE_VCVTf16f32")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VCVTf32f16")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VFM(A|S)")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_V(MIN|MAX)NM")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VMOV_from_lane")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VMOV_rr_q")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VMOVi")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VMUL(_qr_)?[if]")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VQ?R?D?MULH")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VQ?D?MULL[TB]?[su]")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VQDMULL_qr_")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VQ?R?D?ML(A|S)[^L]")>;
def : InstRW<[M55Write2FloatE3, M55WriteLat1], (instregex "MVE_VR?ML(A|S)L")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VNEGf")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VRINTf")>;
def : InstRW<[M55Write2FloatE2], (instregex "MVE_VSUBf")>;
def : InstRW<[M55Write2FloatE3], (instregex "MVE_VSUB_qr_f")>;

// Some VMOV's can go down either pipeline.
def : InstRW<[M55Write2IntFPE2], (instregex "MVE_VMOV_to_lane", "MVE_VMOV_q_rr")>;

def : InstRW<[M55WriteSysE2], (instregex "MVE_VCTP")>;
def : InstRW<[M55WriteSysE2], (instregex "MVE_VPNOT")>;
def : InstRW<[M55WriteSysE2], (instregex "MVE_VPST")>;


// VFP instructions

def : SchedAlias<WriteFPCVT, M55WriteFloatE3>;
def : SchedAlias<WriteFPMOV, M55WriteFloatE3>;
def : SchedAlias<WriteFPALU32, M55WriteFloatE3>;
def : SchedAlias<WriteFPALU64, M55WriteFloatE3Plus13>;
def : SchedAlias<WriteFPMUL32, M55WriteFloatE3>;
def : SchedAlias<WriteFPMUL64, M55WriteFloatE3Plus19>;
def : SchedAlias<WriteFPMAC32, M55WriteFloatE3Plus2>;
def : SchedAlias<WriteFPMAC64, M55WriteFloatE3Plus34>;
def : SchedAlias<WriteFPDIV32, M55WriteFloatE3Plus14>;
def : SchedAlias<WriteFPDIV64, M55WriteFloatE3Plus28>;
def : SchedAlias<WriteFPSQRT32, M55WriteFloatE3Plus14>;
def : SchedAlias<WriteFPSQRT64, M55WriteFloatE3Plus28>;
def : ReadAdvance<ReadFPMUL, 0>;
def : ReadAdvance<ReadFPMAC, 0>;

def : InstRW<[M55WriteLSE3], (instregex "VLD")>;
def : InstRW<[M55WriteLSE2], (instregex "VST")>;
def : InstRW<[M55WriteLSE3], (instregex "VLLD", "VLST")>;

def : InstRW<[M55WriteFloatE3], (instregex "VABS(H|S|D)")>;
def : InstRW<[M55WriteFloatE3], (instregex "VCVT(A|M|N|P|R|X|Z)(S|U)(H|S|D)")>;
def : InstRW<[M55WriteFloatE3], (instregex "VCVT(B|T)(DH|HD)")>;
def : InstRW<[M55WriteFloatE2], (instregex "VCMPZ?(E|H|S|D)")>;
def : InstRW<[M55WriteFloatE3Plus7], (instregex "VDIVH")>;
def : InstRW<[M55WriteFloatE3], (instregex "VFN?M(A|S)(H|S)")>; // VFMA
def : InstRW<[M55WriteFloatE3Plus22], (instregex "VFN?M(A|S)D")>; // VFMA
def : InstRW<[M55WriteFloatE3], (instregex "VFP_V(MAX|MIN)NM")>;
def : InstRW<[M55WriteFloatE3], (instregex "VINSH$", "VMOVH$", "VMOVHR$", "VMOVSR$", "VMOVDRR$")>; // VINS, VMOVX, to-FP reg movs
def : InstRW<[M55WriteFloatE2], (instregex "VMOVD$", "VMOVS$", "VMOVR")>; // Other VMOV's
def : InstRW<[M55WriteFloatE2], (instregex "FCONSTH", "FCONSTS", "FCONSTD")>;
def : InstRW<[M55WriteFloatE2], (instregex "VGETLNi32", "VSETLNi32")>;
def : InstRW<[M55WriteFloatE2], (instregex "VMSR", "VMRS")>;
def : InstRW<[M55WriteFloatE3Plus2], (instregex "VN?ML(A|S)H")>; // VMLA
def : InstRW<[M55WriteFloatE3], (instregex "VNEG(H|S|D)")>;
def : InstRW<[M55WriteFloatE3], (instregex "VRINT(A|M|N|P|R|X|Z)(H|S|D)")>;
def : InstRW<[M55WriteFloatE3], (instregex "VSEL..(H|S|D)")>;
def : InstRW<[M55WriteFloatE3Plus7], (instregex "VSQRTH")>;

def : WriteRes<WriteVLD1, []>;
def : WriteRes<WriteVLD2, []>;
def : WriteRes<WriteVLD3, []>;
def : WriteRes<WriteVLD4, []>;
def : WriteRes<WriteVST1, []>;
def : WriteRes<WriteVST2, []>;
def : WriteRes<WriteVST3, []>;
def : WriteRes<WriteVST4, []>;

}
