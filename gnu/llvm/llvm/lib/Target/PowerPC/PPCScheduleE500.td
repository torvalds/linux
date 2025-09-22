//===-- PPCScheduleE500.td - e500 Scheduling Defs ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the Freescale e500 32-bit
// Power processor.
//
// All information is derived from the "e500 Core Reference Manual",
// Freescale Document Number E500MCRM, Rev. 1, 03/2012.
//
//===----------------------------------------------------------------------===//
// Relevant functional units in the Freescale e500 core:
//
//  * Decode & Dispatch
//    Can dispatch up to 2 instructions per clock cycle to either the GPR Issue
//    queues (GIQx) or Branch issue queue (BIQ).
def E500_DIS0 : FuncUnit; // Dispatch stage - insn 1
def E500_DIS1 : FuncUnit; // Dispatch stage - insn 2

//  * Execute
//    6 pipelined execution units: SU0, SU1, BU, LSU, MU.
//    Some instructions can only execute in SU0 but not SU1.
def E500_SU0  : FuncUnit; // Simple unit 0
def E500_SU1  : FuncUnit; // Simple unit 1
def E500_BU    : FuncUnit; // Branch unit
def E500_MU    : FuncUnit; // MU pipeline
def E500_LSU_0 : FuncUnit; // LSU pipeline

def E500_GPR_Bypass : Bypass;
def E500_CR_Bypass  : Bypass;
def E500_DivBypass  : Bypass;

def PPCE500Itineraries : ProcessorItineraries<
  [E500_DIS0, E500_DIS1, E500_SU0, E500_SU1, E500_BU,
   E500_MU, E500_LSU_0],
  [E500_CR_Bypass, E500_GPR_Bypass, E500_DivBypass], [
  InstrItinData<IIC_IntSimple,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntGeneral,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntISEL,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1, 1], // Latency = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass,
                                  E500_CR_Bypass]>,
  InstrItinData<IIC_IntCompare,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [5, 1, 1], // Latency = 1 or 2
                                 [E500_CR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntDivW,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_MU], 0>,
                                  InstrStage<14, [E500_MU]>],
                                 [17, 1, 1], // Latency=4..35, Repeat= 4..35
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntMulHW,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_MU]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntMulHWU,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_MU]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntMulLI,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_MU]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntRotate,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntShift,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_IntTrapW,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<2, [E500_SU0]>],
                                 [5, 1], // Latency = 2, Repeat rate = 2
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_BrB,         [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_BU]>],
                                 [4, 1], // Latency = 1
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_BrCR,        [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_BU]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_CR_Bypass,
                                  E500_CR_Bypass, E500_CR_Bypass]>,
  InstrItinData<IIC_BrMCR,       [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_BU]>],
                                 [4, 1], // Latency = 1
                                 [E500_CR_Bypass, E500_CR_Bypass]>,
  InstrItinData<IIC_BrMCRX,      [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500_CR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBA,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3, Repeat rate = 1
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBF,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBI,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLoad,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLoadUpd, [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStLoadUpdX,[InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStStore,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTU,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStSTUX,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStICBI,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLHA,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLHAU,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLHAUX,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLMW,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [7, 1], // Latency = r+3
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStLWARX,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<3, [E500_LSU_0]>],
                                 [6, 1, 1], // Latency = 3, Repeat rate = 3
                                 [E500_GPR_Bypass,
                                  E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTWCX,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_LdStSync,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0]>]>,
  InstrItinData<IIC_SprMFSR,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_SU0]>],
                                 [7, 1],
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMTMSR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<2, [E500_SU0, E500_SU1]>],
                                 [5, 1], // Latency = 2, Repeat rate = 4
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSR,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0]>],
                                 [5, 1],
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprTLBSYNC,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_LSU_0], 0>]>,
  InstrItinData<IIC_SprMFCR,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<5, [E500_SU0]>],
                                 [8, 1],
                                 [E500_GPR_Bypass, E500_CR_Bypass]>,
  InstrItinData<IIC_SprMFCRF,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<5, [E500_SU0]>],
                                 [8, 1],
                                 [E500_GPR_Bypass, E500_CR_Bypass]>,
  InstrItinData<IIC_SprMFPMR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_SU0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMFMSR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_SU0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [E500_GPR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMFSPR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500_GPR_Bypass, E500_CR_Bypass]>,
  InstrItinData<IIC_SprMTPMR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500_CR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMFTB,     [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_SU0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSPR,    [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0, E500_SU1]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500_CR_Bypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSRIN,   [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0]>],
                                 [4, 1],
                                 [NoBypass, E500_GPR_Bypass]>,
  InstrItinData<IIC_FPDGeneral,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<6, [E500_MU]>],
                                 [9, 1, 1],  // Latency = 6, Repeat rate = 1
                                 [NoBypass]>,
  InstrItinData<IIC_FPSGeneral,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_MU]>],
                                 [7, 1, 1],  // Latency = 4, Repeat rate = 1
                                 [NoBypass]>,
  InstrItinData<IIC_FPDivD,      [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<32, [E500_MU]>],
                                 [35, 1, 1], // Latency = 32, Repeat rate = 32
                                 [E500_DivBypass]>,
  InstrItinData<IIC_FPDivS,      [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<29, [E500_MU]>],
                                 [32, 1, 1], // Latency = 29, Repeat rate = 29
                                 [E500_DivBypass]>,
  InstrItinData<IIC_VecGeneral,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<1, [E500_SU0]>],
                                 [4, 1, 1], // Latency = 1, Repeat rate = 1
                                 [NoBypass]>,
  InstrItinData<IIC_VecComplex,  [InstrStage<1, [E500_DIS0, E500_DIS1], 0>,
                                  InstrStage<4, [E500_MU]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [NoBypass]>
]>;

// ===---------------------------------------------------------------------===//
// e500 machine model for scheduling and other instruction cost heuristics.

def PPCE500Model : SchedMachineModel {
  let IssueWidth = 2;  // 2 micro-ops are dispatched per cycle.
  let LoadLatency = 5; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.

  let CompleteModel = 0;

  let Itineraries = PPCE500Itineraries;
}
