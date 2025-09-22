//===-- PPCScheduleE500mc.td - e500mc Scheduling Defs ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the Freescale e500mc 32-bit
// Power processor.
//
// All information is derived from the "e500mc Core Reference Manual",
// Freescale Document Number E500MCRM, Rev. 1, 03/2012.
//
//===----------------------------------------------------------------------===//
// Relevant functional units in the Freescale e500mc core:
//
//  * Decode & Dispatch
//    Can dispatch up to 2 instructions per clock cycle to either the GPR Issue
//    queues (GIQx), FP Issue Queue (FIQ), or Branch issue queue (BIQ).
def E500mc_DIS0 : FuncUnit; // Dispatch stage - insn 1
def E500mc_DIS1 : FuncUnit; // Dispatch stage - insn 2

//  * Execute
//    6 pipelined execution units: SFX0, SFX1, BU, FPU, LSU, CFX.
//    Some instructions can only execute in SFX0 but not SFX1.
//    The CFX has a bypass path, allowing non-divide instructions to execute
//    while a divide instruction is executed.
def E500mc_SFX0  : FuncUnit; // Simple unit 0
def E500mc_SFX1  : FuncUnit; // Simple unit 1
def E500mc_BU    : FuncUnit; // Branch unit
def E500mc_CFX_DivBypass
               : FuncUnit; // CFX divide bypass path
def E500mc_CFX_0 : FuncUnit; // CFX pipeline
def E500mc_LSU_0 : FuncUnit; // LSU pipeline
def E500mc_FPU_0 : FuncUnit; // FPU pipeline

def E500mc_GPR_Bypass : Bypass;
def E500mc_FPR_Bypass : Bypass;
def E500mc_CR_Bypass  : Bypass;

def PPCE500mcItineraries : ProcessorItineraries<
  [E500mc_DIS0, E500mc_DIS1, E500mc_SFX0, E500mc_SFX1, E500mc_BU, E500mc_CFX_DivBypass,
   E500mc_CFX_0, E500mc_LSU_0, E500mc_FPU_0],
  [E500mc_CR_Bypass, E500mc_GPR_Bypass, E500mc_FPR_Bypass], [
  InstrItinData<IIC_IntSimple,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntGeneral,  [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntISEL,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1, 1], // Latency = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass,
                                  E500mc_CR_Bypass]>,
  InstrItinData<IIC_IntCompare,  [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [5, 1, 1], // Latency = 1 or 2
                                 [E500mc_CR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntDivW,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_CFX_0], 0>,
                                  InstrStage<14, [E500mc_CFX_DivBypass]>],
                                 [17, 1, 1], // Latency=4..35, Repeat= 4..35
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntMFFS,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<8, [E500mc_FPU_0]>],
                                 [11], // Latency = 8
                                 [E500mc_FPR_Bypass]>,
  InstrItinData<IIC_IntMTFSB0,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<8, [E500mc_FPU_0]>],
                                 [11, 1, 1], // Latency = 8
                                 [NoBypass, NoBypass, NoBypass]>,
  InstrItinData<IIC_IntMulHW,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_CFX_0]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntMulHWU,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_CFX_0]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntMulLI,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_CFX_0]>],
                                 [7, 1, 1], // Latency = 4, Repeat rate = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntRotate,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntShift,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_IntTrapW,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<2, [E500mc_SFX0]>],
                                 [5, 1], // Latency = 2, Repeat rate = 2
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_BrB,         [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_BU]>],
                                 [4, 1], // Latency = 1
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_BrCR,        [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_BU]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_CR_Bypass,
                                  E500mc_CR_Bypass, E500mc_CR_Bypass]>,
  InstrItinData<IIC_BrMCR,       [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_BU]>],
                                 [4, 1], // Latency = 1
                                 [E500mc_CR_Bypass, E500mc_CR_Bypass]>,
  InstrItinData<IIC_BrMCRX,      [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1, 1], // Latency = 1
                                 [E500mc_CR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBA,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3, Repeat rate = 1
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBF,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStDCBI,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLoad,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLoadUpd, [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStLoadUpdX,[InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStStore,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTU,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStSTUX,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStICBI,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTFD,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1, 1], // Latency = 3
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTFDU,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1, 1], // Latency = 3
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStLFD,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [7, 1, 1], // Latency = 4
                                 [E500mc_FPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLFDU,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [7, 1, 1], // Latency = 4
                                 [E500mc_FPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStLFDUX,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [7, 1, 1], // Latency = 4
                                 [E500mc_FPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass],
                                 2>, // 2 micro-ops
  InstrItinData<IIC_LdStLHA,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLHAU,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLHAUX,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLMW,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [7, 1], // Latency = r+3
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStLWARX,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<3, [E500mc_LSU_0]>],
                                 [6, 1, 1], // Latency = 3, Repeat rate = 3
                                 [E500mc_GPR_Bypass,
                                  E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStSTWCX,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>],
                                 [6, 1], // Latency = 3
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_LdStSync,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0]>]>,
  InstrItinData<IIC_SprMFSR,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_SFX0]>],
                                 [7, 1],
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMTMSR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<2, [E500mc_SFX0, E500mc_SFX1]>],
                                 [5, 1], // Latency = 2, Repeat rate = 4
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSR,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0]>],
                                 [5, 1],
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprTLBSYNC,  [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_LSU_0], 0>]>,
  InstrItinData<IIC_SprMFCR,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<5, [E500mc_SFX0]>],
                                 [8, 1],
                                 [E500mc_GPR_Bypass, E500mc_CR_Bypass]>,
  InstrItinData<IIC_SprMFCRF,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<5, [E500mc_SFX0]>],
                                 [8, 1],
                                 [E500mc_GPR_Bypass, E500mc_CR_Bypass]>,
  InstrItinData<IIC_SprMFPMR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_SFX0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMFMSR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_SFX0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [E500mc_GPR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMFSPR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500mc_GPR_Bypass, E500mc_CR_Bypass]>,
  InstrItinData<IIC_SprMTPMR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500mc_CR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMFTB,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_SFX0]>],
                                 [7, 1], // Latency = 4, Repeat rate = 4
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSPR,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0, E500mc_SFX1]>],
                                 [4, 1], // Latency = 1, Repeat rate = 1
                                 [E500mc_CR_Bypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_SprMTSRIN,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<1, [E500mc_SFX0]>],
                                 [4, 1],
                                 [NoBypass, E500mc_GPR_Bypass]>,
  InstrItinData<IIC_FPGeneral,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<2, [E500mc_FPU_0]>],
                                 [11, 1, 1], // Latency = 8, Repeat rate = 2
                                 [E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPAddSub,    [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_FPU_0]>],
                                 [13, 1, 1], // Latency = 10, Repeat rate = 4
                                 [E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPCompare,   [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<2, [E500mc_FPU_0]>],
                                 [11, 1, 1], // Latency = 8, Repeat rate = 2
                                 [E500mc_CR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPDivD,      [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<68, [E500mc_FPU_0]>],
                                 [71, 1, 1], // Latency = 68, Repeat rate = 68
                                 [E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPDivS,      [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<38, [E500mc_FPU_0]>],
                                 [41, 1, 1], // Latency = 38, Repeat rate = 38
                                 [E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPFused,     [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<4, [E500mc_FPU_0]>],
                                 [13, 1, 1, 1], // Latency = 10, Repeat rate = 4
                                 [E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass, E500mc_FPR_Bypass,
                                  E500mc_FPR_Bypass]>,
  InstrItinData<IIC_FPRes,       [InstrStage<1, [E500mc_DIS0, E500mc_DIS1], 0>,
                                  InstrStage<38, [E500mc_FPU_0]>],
                                 [41, 1], // Latency = 38, Repeat rate = 38
                                 [E500mc_FPR_Bypass, E500mc_FPR_Bypass]>
]>;

// ===---------------------------------------------------------------------===//
// e500mc machine model for scheduling and other instruction cost heuristics.

def PPCE500mcModel : SchedMachineModel {
  let IssueWidth = 2;  // 2 micro-ops are dispatched per cycle.
  let LoadLatency = 5; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.

  let CompleteModel = 0;

  let Itineraries = PPCE500mcItineraries;
}
