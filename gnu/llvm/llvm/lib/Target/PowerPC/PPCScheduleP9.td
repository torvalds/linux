//===-- PPCScheduleP9.td - PPC P9 Scheduling Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the POWER9 processor.
//
//===----------------------------------------------------------------------===//
def P9Model : SchedMachineModel {
  // The maximum number of instructions to be issued at the same time.
  // While a value of 8 is technically correct since 8 instructions can be
  // fetched from the instruction cache. However, only 6 instructions may be
  // actually dispatched at a time.
  let IssueWidth = 8;

  // Load latency is 4 or 5 cycles depending on the load. This latency assumes
  // that we have a cache hit. For a cache miss the load latency will be more.
  // There are two instructions (lxvl, lxvll) that have a latency of 6 cycles.
  // However it is not worth bumping this value up to 6 when the vast majority
  // of instructions are 4 or 5 cycles.
  let LoadLatency = 5;

  // A total of 16 cycles to recover from a branch mispredict.
  let MispredictPenalty = 16;

  // Try to make sure we have at least 10 dispatch groups in a loop.
  // A dispatch group is 6 instructions.
  let LoopMicroOpBufferSize = 60;

  // As iops are dispatched to a slice, they are held in an independent slice
  // issue queue until all register sources and other dependencies have been
  // resolved and they can be issued. Each of four execution slices has an
  // 11-entry iop issue queue.
  let MicroOpBufferSize = 44;

  let CompleteModel = 1;

  // Do not support SPE (Signal Processing Engine) or prefixed instructions on
  // Power 9, or MMA, or paired vector mem ops,  or PC relative mem ops, or
  // instructions introduced after ISA 3.0.
  let UnsupportedFeatures = [HasSPE, PrefixInstrs, MMA,
                             PairedVectorMemops, IsBookE,
                             PCRelativeMemops, IsISA3_1, IsISAFuture];
}

let SchedModel = P9Model in {

  // ***************** Processor Resources *****************

  // Dispatcher slots:
  // x0, x1, x2, and x3 are the dedicated slice dispatch ports, where each
  // corresponds to one of the four execution slices.
  def DISPx02 : ProcResource<2>;
  def DISPx13 : ProcResource<2>;
  // The xa and xb ports can be used to send an iop to either of the two slices
  // of the superslice, but are restricted to iops with only two primary sources.
  def DISPxab : ProcResource<2>;
  // b0 and b1 are dedicated dispatch ports into the branch slice.
  def DISPb01 : ProcResource<2>;

  // Any non BR dispatch ports
  def DISP_NBR
      : ProcResGroup<[ DISPx02, DISPx13, DISPxab]>;
  def DISP_SS : ProcResGroup<[ DISPx02, DISPx13]>;

  // Issue Ports
  // An instruction can go down one of two issue queues.
  // Address Generation (AGEN) mainly for loads and stores.
  // Execution (EXEC) for most other instructions.
  // Some instructions cannot be run on just any issue queue and may require an
  // Even or an Odd queue. The EXECE represents the even queues and the EXECO
  // represents the odd queues.
  def IP_AGEN : ProcResource<4>;
  def IP_EXEC : ProcResource<4>;
  def IP_EXECE : ProcResource<2> {
    //Even Exec Ports
    let Super = IP_EXEC;
  }
  def IP_EXECO : ProcResource<2> {
    //Odd Exec Ports
    let Super = IP_EXEC;
  }

  // Pipeline Groups
  // Four ALU (Fixed Point Arithmetic) units in total. Two even, two Odd.
  def ALU : ProcResource<4>;
  def ALUE : ProcResource<2> {
    //Even ALU pipelines
    let Super = ALU;
  }
  def ALUO : ProcResource<2> {
    //Odd ALU pipelines
    let Super = ALU;
  }

  // Two DIV (Fixed Point Divide) units.
  def DIV : ProcResource<2>;

  // Four DP (Floating Point) units in total. Two even, two Odd.
  def DP : ProcResource<4>;
  def DPE : ProcResource<2> {
    //Even DP pipelines
    let Super = DP;
  }
  def DPO : ProcResource<2> {
    //Odd DP pipelines
    let Super = DP;
  }

  // Four LS (Load or Store) units.
  def LS : ProcResource<4>;

  // Two PM (Permute) units.
  def PM : ProcResource<2>;

  // Only one DFU (Decimal Floating Point and Quad Precision) unit.
  def DFU : ProcResource<1>;

  // Only one Branch unit.
  def BR : ProcResource<1> {
    let BufferSize = 16;
  }

  // Only one CY (Crypto) unit.
  def CY : ProcResource<1>;

  // ***************** SchedWriteRes Definitions *****************

  // Dispatcher
  // Dispatch Rules: '-' or 'V'
  // Vector ('V') - vector iops (128-bit operand) take only one decode and
  // dispatch slot but are dispatched to both the even and odd slices of a
  // superslice.
  def DISP_1C : SchedWriteRes<[DISP_NBR]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }
  // Dispatch Rules: 'E' 
  // Even slice ('E')- certain operations must be sent only to an even slice.
  // Also consumes odd dispatch slice slot of the same superslice at dispatch
  def DISP_EVEN_1C : SchedWriteRes<[ DISPx02, DISPx13 ]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }
  // Dispatch Rules: 'P'
  // Paired ('P') - certain cracked and expanded iops are paired such that they
  // must dispatch together to the same superslice.
  def DISP_PAIR_1C : SchedWriteRes<[ DISP_SS, DISP_SS]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }
  // Tuple Restricted ('R') - certain iops preclude dispatching more than one
  // operation per slice for the super- slice to which they are dispatched
  def DISP_3SLOTS_1C : SchedWriteRes<[DISPx02, DISPx13, DISPxab]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }
  // Each execution and branch slice can receive up to two iops per cycle
  def DISP_BR_1C : SchedWriteRes<[ DISPxab ]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  // Issue Ports
  def IP_AGEN_1C : SchedWriteRes<[IP_AGEN]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXEC_1C : SchedWriteRes<[IP_EXEC]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXECE_1C : SchedWriteRes<[IP_EXECE]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXECO_1C : SchedWriteRes<[IP_EXECO]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  //Pipeline Groups

  // ALU Units
  // An ALU may take either 2 or 3 cycles to complete the operation.
  // However, the ALU unit is only ever busy for 1 cycle at a time and may
  // receive new instructions each cycle.
  def P9_ALU_2C : SchedWriteRes<[ALU]> {
    let Latency = 2;
  }

  def P9_ALUE_2C : SchedWriteRes<[ALUE]> {
    let Latency = 2;
  }

  def P9_ALUO_2C : SchedWriteRes<[ALUO]> {
    let Latency = 2;
  }

  def P9_ALU_3C : SchedWriteRes<[ALU]> {
    let Latency = 3;
  }

  def P9_ALUE_3C : SchedWriteRes<[ALUE]> {
    let Latency = 3;
  }

  def P9_ALUO_3C : SchedWriteRes<[ALUO]> {
    let Latency = 3;
  }

  // DIV Unit
  // A DIV unit may take from 5 to 40 cycles to complete.
  // Some DIV operations may keep the unit busy for up to 8 cycles.
  def P9_DIV_5C : SchedWriteRes<[DIV]> {
    let Latency = 5;
  }

  def P9_DIV_12C : SchedWriteRes<[DIV]> {
    let Latency = 12;
  }

  def P9_DIV_16C_8 : SchedWriteRes<[DIV]> {
    let ReleaseAtCycles = [8];
    let Latency = 16;
  }

  def P9_DIV_24C_8 : SchedWriteRes<[DIV]> {
    let ReleaseAtCycles = [8];
    let Latency = 24;
  }

  def P9_DIV_40C_8 : SchedWriteRes<[DIV]> {
    let ReleaseAtCycles = [8];
    let Latency = 40;
  }

  // DP Unit
  // A DP unit may take from 2 to 36 cycles to complete.
  // Some DP operations keep the unit busy for up to 10 cycles.
  def P9_DP_5C : SchedWriteRes<[DP]> {
    let Latency = 5;
  }

  def P9_DP_7C : SchedWriteRes<[DP]> {
    let Latency = 7;
  }

  def P9_DPE_7C : SchedWriteRes<[DPE]> {
    let Latency = 7;
  }

  def P9_DPO_7C : SchedWriteRes<[DPO]> {
    let Latency = 7;
  }

  def P9_DP_22C_5 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [5];
    let Latency = 22;
  }

  def P9_DPO_24C_8 : SchedWriteRes<[DPO]> {
    let ReleaseAtCycles = [8];
    let Latency = 24;
  }

  def P9_DPE_24C_8 : SchedWriteRes<[DPE]> {
    let ReleaseAtCycles = [8];
    let Latency = 24;
  }

  def P9_DP_26C_5 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [5];
    let Latency = 22;
  }

  def P9_DPE_27C_10 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [10];
    let Latency = 27;
  }

  def P9_DPO_27C_10 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [10];
    let Latency = 27;
  }

  def P9_DP_33C_8 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [8];
    let Latency = 33;
  }

  def P9_DPE_33C_8 : SchedWriteRes<[DPE]> {
    let ReleaseAtCycles = [8];
    let Latency = 33;
  }

  def P9_DPO_33C_8 : SchedWriteRes<[DPO]> {
    let ReleaseAtCycles = [8];
    let Latency = 33;
  }

  def P9_DP_36C_10 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [10];
    let Latency = 36;
  }

  def P9_DPE_36C_10 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [10];
    let Latency = 36;
  }

  def P9_DPO_36C_10 : SchedWriteRes<[DP]> {
    let ReleaseAtCycles = [10];
    let Latency = 36;
  }

  // PM Unit
  // Three cycle permute operations.
  def P9_PM_3C : SchedWriteRes<[PM]> {
    let Latency = 3;
  }

  // Load and Store Units
  // Loads can have 4, 5 or 6 cycles of latency.
  // Stores are listed as having a single cycle of latency. This is not
  // completely accurate since it takes more than 1 cycle to actually store
  // the value. However, since the store does not produce a result it can be
  // considered complete after one cycle.
  def P9_LS_1C : SchedWriteRes<[LS]> {
    let Latency = 1;
  }

  def P9_LS_4C : SchedWriteRes<[LS]> {
    let Latency = 4;
  }

  def P9_LS_5C : SchedWriteRes<[LS]> {
    let Latency = 5;
  }

  def P9_LS_6C : SchedWriteRes<[LS]> {
    let Latency = 6;
  }

  // DFU Unit
  // Some of the most expensive ops use the DFU.
  // Can take from 12 cycles to 76 cycles to obtain a result.
  // The unit may be busy for up to 62 cycles.
  def P9_DFU_12C : SchedWriteRes<[DFU]> {
    let Latency = 12;
  }

  def P9_DFU_23C : SchedWriteRes<[DFU]> {
    let Latency = 23;
    let ReleaseAtCycles = [11];
  }

  def P9_DFU_24C : SchedWriteRes<[DFU]> {
    let Latency = 24;
    let ReleaseAtCycles = [12];
  }

  def P9_DFU_37C : SchedWriteRes<[DFU]> {
    let Latency = 37;
    let ReleaseAtCycles = [25];
  }

  def P9_DFU_58C : SchedWriteRes<[DFU]> {
    let Latency = 58;
    let ReleaseAtCycles = [44];
  }

  def P9_DFU_76C : SchedWriteRes<[DFU]> {
    let Latency = 76;
    let ReleaseAtCycles = [62];
  }

  // 2 or 5 cycle latencies for the branch unit.
  def P9_BR_2C : SchedWriteRes<[BR]> {
    let Latency = 2;
  }

  def P9_BR_5C : SchedWriteRes<[BR]> {
    let Latency = 5;
  }

  // 6 cycle latency for the crypto unit
  def P9_CY_6C : SchedWriteRes<[CY]> {
    let Latency = 6;
  }

  // ***************** WriteSeq Definitions *****************

  // These are combinations of the resources listed above.
  // The idea is that some cracked instructions cannot be done in parallel and
  // so the latencies for their resources must be added.
  def P9_LoadAndALUOp_6C : WriteSequence<[P9_LS_4C, P9_ALU_2C]>;
  def P9_LoadAndALUOp_7C : WriteSequence<[P9_LS_5C, P9_ALU_2C]>;
  def P9_LoadAndALU2Op_7C : WriteSequence<[P9_LS_4C, P9_ALU_3C]>;
  def P9_LoadAndALU2Op_8C : WriteSequence<[P9_LS_5C, P9_ALU_3C]>;
  def P9_LoadAndPMOp_8C : WriteSequence<[P9_LS_5C, P9_PM_3C]>;
  def P9_IntDivAndALUOp_18C_8 : WriteSequence<[P9_DIV_16C_8, P9_ALU_2C]>;
  def P9_IntDivAndALUOp_26C_8 : WriteSequence<[P9_DIV_24C_8, P9_ALU_2C]>;
  def P9_IntDivAndALUOp_42C_8 : WriteSequence<[P9_DIV_40C_8, P9_ALU_2C]>;
  def P9_StoreAndALUOp_3C : WriteSequence<[P9_LS_1C, P9_ALU_2C]>;
  def P9_ALUOpAndALUOp_4C : WriteSequence<[P9_ALU_2C, P9_ALU_2C]>;
  def P9_ALU2OpAndALU2Op_6C : WriteSequence<[P9_ALU_3C, P9_ALU_3C]>;
  def P9_ALUOpAndALUOpAndALUOp_6C :
    WriteSequence<[P9_ALU_2C, P9_ALU_2C, P9_ALU_2C]>;
  def P9_DPOpAndALUOp_7C : WriteSequence<[P9_DP_5C, P9_ALU_2C]>;
  def P9_DPOpAndALU2Op_10C : WriteSequence<[P9_DP_7C, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_25C_5 : WriteSequence<[P9_DP_22C_5, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_29C_5 : WriteSequence<[P9_DP_26C_5, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_36C_8 : WriteSequence<[P9_DP_33C_8, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_39C_10 : WriteSequence<[P9_DP_36C_10, P9_ALU_3C]>;
  def P9_BROpAndALUOp_7C : WriteSequence<[P9_BR_5C, P9_ALU_2C]>;

  // Include the resource requirements of individual instructions.
  include "P9InstrResources.td"

}

