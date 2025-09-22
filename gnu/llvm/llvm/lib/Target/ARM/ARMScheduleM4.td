//==- ARMScheduleM4.td - Cortex-M4 Scheduling Definitions -*- tablegen -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SchedRead/Write data for the ARM Cortex-M4 processor.
//
//===----------------------------------------------------------------------===//

def CortexM4Model : SchedMachineModel {
  let IssueWidth        = 1; // Only IT can be dual-issued, so assume single-issue
  let MicroOpBufferSize = 0; // In-order
  let LoadLatency       = 2; // Latency when not pipelined, not pc-relative
  let MispredictPenalty = 2; // Best case branch taken cost
  let PostRAScheduler   = 1;

  let CompleteModel = 0;
  let UnsupportedFeatures = [IsARM, HasNEON, HasDotProd, HasZCZ, HasMVEInt,
          IsNotMClass, HasDPVFP, HasFPARMv8, HasFullFP16, Has8MSecExt, HasV8,
          HasV8_3a, HasTrustZone, HasDFB, IsWindows];
}


// We model the entire cpu as a single pipeline with a BufferSize = 0 since
// Cortex-M4 is in-order.

def M4Unit : ProcResource<1> { let BufferSize = 0; }


let SchedModel = CortexM4Model in {

// Some definitions of latencies we apply to different instructions

class M4UnitL1<SchedWrite write> : WriteRes<write, [M4Unit]> { let Latency = 1; }
class M4UnitL2<SchedWrite write> : WriteRes<write, [M4Unit]> { let Latency = 2; }
class M4UnitL3<SchedWrite write> : WriteRes<write, [M4Unit]> { let Latency = 3; }
class M4UnitL14<SchedWrite write> : WriteRes<write, [M4Unit]> { let Latency = 14; }
def M4UnitL1_wr : SchedWriteRes<[M4Unit]> { let Latency = 1; }
def M4UnitL2_wr : SchedWriteRes<[M4Unit]> { let Latency = 2; }
class M4UnitL1I<dag instr> : InstRW<[M4UnitL1_wr], instr>;
class M4UnitL2I<dag instr> : InstRW<[M4UnitL2_wr], instr>;


// Loads, MAC's and DIV all get a higher latency of 2
def : M4UnitL2<WriteLd>;
def : M4UnitL2<WriteMAC32>;
def : M4UnitL2<WriteMAC64Hi>;
def : M4UnitL2<WriteMAC64Lo>;
def : M4UnitL2<WriteMAC16>;
def : M4UnitL2<WriteDIV>;

def : M4UnitL2I<(instregex "(t|t2)LDM")>;
def : M4UnitL2I<(instregex "(t|t2)LDR")>;


// Stores we use a latency of 1 as they have no outputs

def : M4UnitL1<WriteST>;
def : M4UnitL1I<(instregex "(t|t2)STM")>;


// Everything else has a Latency of 1

def : M4UnitL1<WriteALU>;
def : M4UnitL1<WriteALUsi>;
def : M4UnitL1<WriteALUsr>;
def : M4UnitL1<WriteALUSsr>;
def : M4UnitL1<WriteBr>;
def : M4UnitL1<WriteBrL>;
def : M4UnitL1<WriteBrTbl>;
def : M4UnitL1<WriteCMPsi>;
def : M4UnitL1<WriteCMPsr>;
def : M4UnitL1<WriteCMP>;
def : M4UnitL1<WriteMUL32>;
def : M4UnitL1<WriteMUL64Hi>;
def : M4UnitL1<WriteMUL64Lo>;
def : M4UnitL1<WriteMUL16>;
def : M4UnitL1<WriteNoop>;
def : M4UnitL1<WritePreLd>;
def : M4UnitL1I<(instregex "(t|t2)MOV")>;
def : M4UnitL1I<(instrs COPY)>;
def : M4UnitL1I<(instregex "t2IT", "t2MSR", "t2MRS")>;
def : M4UnitL1I<(instregex "t2CLREX")>;
def : M4UnitL1I<(instregex "t2SEL", "t2USAD8", "t2SML[AS]",
    "t2(S|Q|SH|U|UQ|UH|QD)(ADD|ASX|SAX|SUB)", "t2USADA8", "(t|t2)REV")>;

// These instructions are not of much interest to scheduling as they will not
// be generated or it is not very useful to schedule them. They are here to make
// the model more complete.
def : M4UnitL1I<(instregex "t2CDP", "t2LDC", "t2MCR", "t2MRC", "t2MRRC", "t2STC")>;
def : M4UnitL1I<(instregex "tCPS", "t2ISB", "t2DSB", "t2DMB", "t2?HINT$")>;
def : M4UnitL1I<(instregex "t2?UDF$", "tBKPT", "t2DBG")>;
def : M4UnitL1I<(instregex "t?2?Int_eh_sjlj_", "tADDframe", "t?ADJCALL")>;
def : M4UnitL1I<(instregex "CMP_SWAP", "JUMPTABLE", "MEMCPY")>;
def : M4UnitL1I<(instregex "VSETLNi32", "VGETLNi32")>;

def : ReadAdvance<ReadALU, 0>;
def : ReadAdvance<ReadALUsr, 0>;
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 0>;

// Most FP instructions are single-cycle latency, except MAC's, Div's and Sqrt's.
// Loads still take 2 cycles.

def : M4UnitL1<WriteFPCVT>;
def : M4UnitL1<WriteFPMOV>;
def : M4UnitL1<WriteFPALU32>;
def : M4UnitL1<WriteFPALU64>;
def : M4UnitL1<WriteFPMUL32>;
def : M4UnitL1<WriteFPMUL64>;
def : M4UnitL2I<(instregex "VLD")>;
def : M4UnitL1I<(instregex "VST")>;
def : M4UnitL3<WriteFPMAC32>;
def : M4UnitL3<WriteFPMAC64>;
def : M4UnitL14<WriteFPDIV32>;
def : M4UnitL14<WriteFPDIV64>;
def : M4UnitL14<WriteFPSQRT32>;
def : M4UnitL14<WriteFPSQRT64>;
def : M4UnitL1<WriteVLD1>;
def : M4UnitL1<WriteVLD2>;
def : M4UnitL1<WriteVLD3>;
def : M4UnitL1<WriteVLD4>;
def : M4UnitL1<WriteVST1>;
def : M4UnitL1<WriteVST2>;
def : M4UnitL1<WriteVST3>;
def : M4UnitL1<WriteVST4>;
def : M4UnitL1I<(instregex "VMOVS", "FCONSTS", "VCMP", "VNEG", "VABS")>;
def : M4UnitL2I<(instregex "VMOVD")>;
def : M4UnitL1I<(instregex "VMRS", "VMSR", "FMSTAT")>;

def : ReadAdvance<ReadFPMUL, 0>;
def : ReadAdvance<ReadFPMAC, 0>;

}
