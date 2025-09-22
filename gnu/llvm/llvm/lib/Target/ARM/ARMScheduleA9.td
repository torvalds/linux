//=- ARMScheduleA9.td - ARM Cortex-A9 Scheduling Definitions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the ARM Cortex A9 processors.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// This section contains legacy support for itineraries. This is
// required until SD and PostRA schedulers are replaced by MachineScheduler.

//
// Ad-hoc scheduling information derived from pretty vague "Cortex-A9 Technical
// Reference Manual".
//
// Functional units
def A9_Issue0  : FuncUnit; // Issue 0
def A9_Issue1  : FuncUnit; // Issue 1
def A9_Branch  : FuncUnit; // Branch
def A9_ALU0    : FuncUnit; // ALU / MUL pipeline 0
def A9_ALU1    : FuncUnit; // ALU pipeline 1
def A9_AGU     : FuncUnit; // Address generation unit for ld / st
def A9_NPipe   : FuncUnit; // NEON pipeline
def A9_MUX0    : FuncUnit; // AGU + NEON/FPU multiplexer
def A9_LSUnit  : FuncUnit; // L/S Unit
def A9_DRegsVFP: FuncUnit; // FP register set, VFP side
def A9_DRegsN  : FuncUnit; // FP register set, NEON side

// Bypasses
def A9_LdBypass : Bypass;

def CortexA9Itineraries : ProcessorItineraries<
  [A9_Issue0, A9_Issue1, A9_Branch, A9_ALU0, A9_ALU1, A9_AGU, A9_NPipe, A9_MUX0,
   A9_LSUnit, A9_DRegsVFP, A9_DRegsN],
  [A9_LdBypass], [
  // Two fully-pipelined integer ALU pipelines

  //
  // Move instructions, unconditional
  InstrItinData<IIC_iMOVi   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1]>,
  InstrItinData<IIC_iMOVr   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iMOVsi  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iMOVsr  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0, A9_ALU1]>], [2, 1, 1]>,
  InstrItinData<IIC_iMOVix2 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [2]>,
  InstrItinData<IIC_iMOVix2addpc,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_ALU0, A9_ALU1]>,
                                  InstrStage<1, [A9_ALU0, A9_ALU1]>,
                                  InstrStage<1, [A9_ALU0, A9_ALU1]>], [3]>,
  InstrItinData<IIC_iMOVix2ld,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_AGU], 0>,
                               InstrStage<1, [A9_LSUnit]>], [5]>,
  //
  // MVN instructions
  InstrItinData<IIC_iMVNi   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>],
                              [1]>,
  InstrItinData<IIC_iMVNr   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>],
                              [1, 1], [NoBypass, A9_LdBypass]>,
  InstrItinData<IIC_iMVNsi  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0, A9_ALU1]>],
                              [2, 1]>,
  InstrItinData<IIC_iMVNsr  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<3, [A9_ALU0, A9_ALU1]>],
                              [3, 1, 1]>,
  //
  // No operand cycles
  InstrItinData<IIC_iALUx   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>]>,
  //
  // Binary Instructions that produce a result
  InstrItinData<IIC_iALUi , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>],
                            [1, 1], [NoBypass, A9_LdBypass]>,
  InstrItinData<IIC_iALUr , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>],
                            [1, 1, 1], [NoBypass, A9_LdBypass, A9_LdBypass]>,
  InstrItinData<IIC_iALUsi, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<2, [A9_ALU0, A9_ALU1]>],
                            [2, 1, 1], [NoBypass, A9_LdBypass, NoBypass]>,
  InstrItinData<IIC_iALUsir,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<2, [A9_ALU0, A9_ALU1]>],
                            [2, 1, 1], [NoBypass, NoBypass, A9_LdBypass]>,
  InstrItinData<IIC_iALUsr, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<3, [A9_ALU0, A9_ALU1]>],
                            [3, 1, 1, 1],
                            [NoBypass, A9_LdBypass, NoBypass, NoBypass]>,
  //
  // Bitwise Instructions that produce a result
  InstrItinData<IIC_iBITi , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iBITr , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1, 1]>,
  InstrItinData<IIC_iBITsi, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<2, [A9_ALU0, A9_ALU1]>], [2, 1, 1]>,
  InstrItinData<IIC_iBITsr, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<3, [A9_ALU0, A9_ALU1]>], [3, 1, 1, 1]>,
  //
  // Unary Instructions that produce a result

  // CLZ, RBIT, etc.
  InstrItinData<IIC_iUNAr , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,

  // BFC, BFI, UBFX, SBFX
  InstrItinData<IIC_iUNAsi, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<2, [A9_ALU0, A9_ALU1]>], [2, 1]>,

  //
  // Zero and sign extension instructions
  InstrItinData<IIC_iEXTr , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<1, [A9_ALU0, A9_ALU1]>], [2, 1]>,
  InstrItinData<IIC_iEXTAr, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<2, [A9_ALU0, A9_ALU1]>], [3, 1, 1]>,
  InstrItinData<IIC_iEXTAsr,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                             InstrStage<3, [A9_ALU0, A9_ALU1]>], [3, 1, 1, 1]>,
  //
  // Compare instructions
  InstrItinData<IIC_iCMPi   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>],
                               [1], [A9_LdBypass]>,
  InstrItinData<IIC_iCMPr   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>],
                               [1, 1], [A9_LdBypass, A9_LdBypass]>,
  InstrItinData<IIC_iCMPsi  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0, A9_ALU1]>],
                                [1, 1], [A9_LdBypass, NoBypass]>,
  InstrItinData<IIC_iCMPsr  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<3, [A9_ALU0, A9_ALU1]>],
                              [1, 1, 1], [A9_LdBypass, NoBypass, NoBypass]>,
  //
  // Test instructions
  InstrItinData<IIC_iTSTi   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1]>,
  InstrItinData<IIC_iTSTr   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iTSTsi  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iTSTsr  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<3, [A9_ALU0, A9_ALU1]>], [1, 1, 1]>,
  //
  // Move instructions, conditional
  // FIXME: Correctly model the extra input dep on the destination.
  InstrItinData<IIC_iCMOVi  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1]>,
  InstrItinData<IIC_iCMOVr  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iCMOVsi , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [1, 1]>,
  InstrItinData<IIC_iCMOVsr , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0, A9_ALU1]>], [2, 1, 1]>,
  InstrItinData<IIC_iCMOVix2, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>,
                               InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_ALU0, A9_ALU1]>], [2]>,

  // Integer multiply pipeline
  //
  InstrItinData<IIC_iMUL16  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0]>], [3, 1, 1]>,
  InstrItinData<IIC_iMAC16  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0]>],
                              [3, 1, 1, 1]>,
  InstrItinData<IIC_iMUL32  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0]>], [4, 1, 1]>,
  InstrItinData<IIC_iMAC32  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<2, [A9_ALU0]>],
                              [4, 1, 1, 1]>,
  InstrItinData<IIC_iMUL64  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<3, [A9_ALU0]>], [4, 5, 1, 1]>,
  InstrItinData<IIC_iMAC64  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<3, [A9_ALU0]>],
                              [4, 5, 1, 1]>,
  // Integer load pipeline
  // FIXME: The timings are some rough approximations
  //
  // Immediate offset
  InstrItinData<IIC_iLoad_i   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_i, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [4, 1], [A9_LdBypass]>,
  // FIXME: If address is 64-bit aligned, AGU cycles is 1.
  InstrItinData<IIC_iLoad_d_i , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 3, 1], [A9_LdBypass]>,
  //
  // Register offset
  InstrItinData<IIC_iLoad_r   , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_r, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [4, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_d_r , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 3, 1, 1], [A9_LdBypass]>,
  //
  // Scaled register offset
  InstrItinData<IIC_iLoad_si  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit], 0>],
                                [4, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_si,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [5, 1, 1], [A9_LdBypass]>,
  //
  // Immediate offset with update
  InstrItinData<IIC_iLoad_iu  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 2, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_iu,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [4, 3, 1], [A9_LdBypass]>,
  //
  // Register offset with update
  InstrItinData<IIC_iLoad_ru  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 2, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_ru,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [4, 3, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_d_ru, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [3, 3, 1, 1], [A9_LdBypass]>,
  //
  // Scaled register offset with update
  InstrItinData<IIC_iLoad_siu , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>],
                                [4, 3, 1, 1], [A9_LdBypass]>,
  InstrItinData<IIC_iLoad_bh_siu,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<2, [A9_AGU], 0>,
                                  InstrStage<1, [A9_LSUnit]>],
                                 [5, 4, 1, 1], [A9_LdBypass]>,
  //
  // Load multiple, def is the 5th operand.
  // FIXME: This assumes 3 to 4 registers.
  InstrItinData<IIC_iLoad_m  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<2, [A9_AGU], 1>,
                                InstrStage<2, [A9_LSUnit]>],
                               [1, 1, 1, 1, 3],
                         [NoBypass, NoBypass, NoBypass, NoBypass, A9_LdBypass],
                         -1>, // dynamic uops
  //
  // Load multiple + update, defs are the 1st and 5th operands.
  InstrItinData<IIC_iLoad_mu , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<2, [A9_AGU], 1>,
                                InstrStage<2, [A9_LSUnit]>],
                               [2, 1, 1, 1, 3],
                         [NoBypass, NoBypass, NoBypass, NoBypass, A9_LdBypass],
                         -1>, // dynamic uops
  //
  // Load multiple plus branch
  InstrItinData<IIC_iLoad_mBr, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<1, [A9_AGU], 1>,
                                InstrStage<2, [A9_LSUnit]>,
                                InstrStage<1, [A9_Branch]>],
                               [1, 2, 1, 1, 3],
                         [NoBypass, NoBypass, NoBypass, NoBypass, A9_LdBypass],
                         -1>, // dynamic uops
  //
  // Pop, def is the 3rd operand.
  InstrItinData<IIC_iPop  ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<2, [A9_AGU], 1>,
                                InstrStage<2, [A9_LSUnit]>],
                               [1, 1, 3],
                               [NoBypass, NoBypass, A9_LdBypass],
                               -1>, // dynamic uops
  //
  // Pop + branch, def is the 3rd operand.
  InstrItinData<IIC_iPop_Br,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<2, [A9_AGU], 1>,
                                InstrStage<2, [A9_LSUnit]>,
                                InstrStage<1, [A9_Branch]>],
                               [1, 1, 3],
                               [NoBypass, NoBypass, A9_LdBypass],
                               -1>, // dynamic uops
  //
  // iLoadi + iALUr for t2LDRpci_pic.
  InstrItinData<IIC_iLoadiALU, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<1, [A9_AGU], 0>,
                                InstrStage<1, [A9_LSUnit]>,
                                InstrStage<1, [A9_ALU0, A9_ALU1]>],
                               [2, 1]>,

  // Integer store pipeline
  ///
  // Immediate offset
  InstrItinData<IIC_iStore_i  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1]>,
  InstrItinData<IIC_iStore_bh_i,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 1>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1]>,
  // FIXME: If address is 64-bit aligned, AGU cycles is 1.
  InstrItinData<IIC_iStore_d_i, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 1>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1]>,
  //
  // Register offset
  InstrItinData<IIC_iStore_r  , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<1, [A9_AGU], 0>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1, 1]>,
  InstrItinData<IIC_iStore_bh_r,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 1>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1, 1]>,
  InstrItinData<IIC_iStore_d_r, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                 InstrStage<1, [A9_MUX0], 0>,
                                 InstrStage<2, [A9_AGU], 1>,
                                 InstrStage<1, [A9_LSUnit]>], [1, 1, 1]>,
  //
  // Scaled register offset
  InstrItinData<IIC_iStore_si ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<1, [A9_AGU], 0>,
                                  InstrStage<1, [A9_LSUnit]>], [1, 1, 1]>,
  InstrItinData<IIC_iStore_bh_si,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<2, [A9_AGU], 1>,
                                  InstrStage<1, [A9_LSUnit]>], [1, 1, 1]>,
  //
  // Immediate offset with update
  InstrItinData<IIC_iStore_iu ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<1, [A9_AGU], 0>,
                                  InstrStage<1, [A9_LSUnit]>], [2, 1, 1]>,
  InstrItinData<IIC_iStore_bh_iu,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<2, [A9_AGU], 1>,
                                  InstrStage<1, [A9_LSUnit]>], [3, 1, 1]>,
  //
  // Register offset with update
  InstrItinData<IIC_iStore_ru ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<1, [A9_AGU], 0>,
                                  InstrStage<1, [A9_LSUnit]>],
                                 [2, 1, 1, 1]>,
  InstrItinData<IIC_iStore_bh_ru,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<2, [A9_AGU], 1>,
                                  InstrStage<1, [A9_LSUnit]>],
                                 [3, 1, 1, 1]>,
  InstrItinData<IIC_iStore_d_ru, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                  InstrStage<1, [A9_MUX0], 0>,
                                  InstrStage<2, [A9_AGU], 1>,
                                  InstrStage<1, [A9_LSUnit]>],
                                 [3, 1, 1, 1]>,
  //
  // Scaled register offset with update
  InstrItinData<IIC_iStore_siu,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                    InstrStage<1, [A9_MUX0], 0>,
                                    InstrStage<1, [A9_AGU], 0>,
                                    InstrStage<1, [A9_LSUnit]>],
                                   [2, 1, 1, 1]>,
  InstrItinData<IIC_iStore_bh_siu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                    InstrStage<1, [A9_MUX0], 0>,
                                    InstrStage<2, [A9_AGU], 1>,
                                    InstrStage<1, [A9_LSUnit]>],
                                   [3, 1, 1, 1]>,
  //
  // Store multiple
  InstrItinData<IIC_iStore_m , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<1, [A9_AGU], 0>,
                                InstrStage<2, [A9_LSUnit]>],
                [], [], -1>, // dynamic uops
  //
  // Store multiple + update
  InstrItinData<IIC_iStore_mu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<1, [A9_AGU], 0>,
                                InstrStage<2, [A9_LSUnit]>],
                [2], [], -1>, // dynamic uops
  //
  // Preload
  InstrItinData<IIC_Preload,   [InstrStage<1, [A9_Issue0, A9_Issue1]>], [1, 1]>,

  // Branch
  //
  // no delay slots, so the latency of a branch is unimportant
  InstrItinData<IIC_Br       , [InstrStage<1, [A9_Issue0], 0>,
                                InstrStage<1, [A9_Issue1], 0>,
                                InstrStage<1, [A9_Branch]>]>,

  // VFP and NEON shares the same register file. This means that every VFP
  // instruction should wait for full completion of the consecutive NEON
  // instruction and vice-versa. We model this behavior with two artificial FUs:
  // DRegsVFP and DRegsVFP.
  //
  // Every VFP instruction:
  //  - Acquires DRegsVFP resource for 1 cycle
  //  - Reserves DRegsN resource for the whole duration (including time to
  //    register file writeback!).
  // Every NEON instruction does the same but with FUs swapped.
  //
  // Since the reserved FU cannot be acquired, this models precisely
  // "cross-domain" stalls.

  // VFP
  // Issue through integer pipeline, and execute in NEON unit.

  // FP Special Register to Integer Register File Move
  InstrItinData<IIC_fpSTAT , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                              InstrStage<1, [A9_MUX0], 0>,
                              InstrStage<1, [A9_DRegsVFP], 0, Required>,
                              InstrStage<2, [A9_DRegsN],   0, Reserved>,
                              InstrStage<1, [A9_NPipe]>],
                             [1]>,
  //
  // Single-precision FP Unary
  InstrItinData<IIC_fpUNA32 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra latency cycles since wbck is 2 cycles
                               InstrStage<3, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,
  //
  // Double-precision FP Unary
  InstrItinData<IIC_fpUNA64 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra latency cycles since wbck is 2 cycles
                               InstrStage<3, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,

  //
  // Single-precision FP Compare
  InstrItinData<IIC_fpCMP32 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra latency cycles since wbck is 4 cycles
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,
  //
  // Double-precision FP Compare
  InstrItinData<IIC_fpCMP64 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra latency cycles since wbck is 4 cycles
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,
  //
  // Single to Double FP Convert
  InstrItinData<IIC_fpCVTSD , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Double to Single FP Convert
  InstrItinData<IIC_fpCVTDS , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,

  //
  // Single to Half FP Convert
  InstrItinData<IIC_fpCVTSH , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Half to Single FP Convert
  InstrItinData<IIC_fpCVTHS , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<3, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 1]>,

  //
  // Single-Precision FP to Integer Convert
  InstrItinData<IIC_fpCVTSI , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Double-Precision FP to Integer Convert
  InstrItinData<IIC_fpCVTDI , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Integer to Single-Precision FP Convert
  InstrItinData<IIC_fpCVTIS , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Integer to Double-Precision FP Convert
  InstrItinData<IIC_fpCVTID , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Single-precision FP ALU
  InstrItinData<IIC_fpALU32 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1, 1]>,
  //
  // Double-precision FP ALU
  InstrItinData<IIC_fpALU64 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<5, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1, 1]>,
  //
  // Single-precision FP Multiply
  InstrItinData<IIC_fpMUL32 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<6, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [5, 1, 1]>,
  //
  // Double-precision FP Multiply
  InstrItinData<IIC_fpMUL64 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<7, [A9_DRegsN],   0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 1, 1]>,
  //
  // Single-precision FP MAC
  InstrItinData<IIC_fpMAC32 , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<9, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [8, 1, 1, 1]>,
  //
  // Double-precision FP MAC
  InstrItinData<IIC_fpMAC64 , [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<10, [A9_DRegsN],  0, Reserved>,
                               InstrStage<2,  [A9_NPipe]>],
                              [9, 1, 1, 1]>,
  //
  // Single-precision Fused FP MAC
  InstrItinData<IIC_fpFMAC32, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<9, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [8, 1, 1, 1]>,
  //
  // Double-precision Fused FP MAC
  InstrItinData<IIC_fpFMAC64, [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<10, [A9_DRegsN],  0, Reserved>,
                               InstrStage<2,  [A9_NPipe]>],
                              [9, 1, 1, 1]>,
  //
  // Single-precision FP DIV
  InstrItinData<IIC_fpDIV32 , [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<16, [A9_DRegsN],  0, Reserved>,
                               InstrStage<10, [A9_NPipe]>],
                              [15, 1, 1]>,
  //
  // Double-precision FP DIV
  InstrItinData<IIC_fpDIV64 , [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<26, [A9_DRegsN],  0, Reserved>,
                               InstrStage<20, [A9_NPipe]>],
                              [25, 1, 1]>,
  //
  // Single-precision FP SQRT
  InstrItinData<IIC_fpSQRT32, [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<18, [A9_DRegsN],   0, Reserved>,
                               InstrStage<13, [A9_NPipe]>],
                              [17, 1]>,
  //
  // Double-precision FP SQRT
  InstrItinData<IIC_fpSQRT64, [InstrStage<1,  [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1,  [A9_MUX0], 0>,
                               InstrStage<1,  [A9_DRegsVFP], 0, Required>,
                               InstrStage<33, [A9_DRegsN],   0, Reserved>,
                               InstrStage<28, [A9_NPipe]>],
                              [32, 1]>,

  //
  // Integer to Single-precision Move
  InstrItinData<IIC_fpMOVIS,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra 1 latency cycle since wbck is 2 cycles
                               InstrStage<3, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,
  //
  // Integer to Double-precision Move
  InstrItinData<IIC_fpMOVID,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               // Extra 1 latency cycle since wbck is 2 cycles
                               InstrStage<3, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1, 1]>,
  //
  // Single-precision to Integer Move
  //
  // On A9 move-from-VFP is free to issue with no stall if other VFP
  // operations are in flight. I assume it still can't dual-issue though.
  InstrItinData<IIC_fpMOVSI,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>],
                              [2, 1]>,
  //
  // Double-precision to Integer Move
  //
  // On A9 move-from-VFP is free to issue with no stall if other VFP
  // operations are in flight. I assume it still can't dual-issue though.
  InstrItinData<IIC_fpMOVDI,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>],
                              [2, 1, 1]>,
  //
  // Single-precision FP Load
  InstrItinData<IIC_fpLoad32, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1]>,
  //
  // Double-precision FP Load
  // FIXME: Result latency is 1 if address is 64-bit aligned.
  InstrItinData<IIC_fpLoad64, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1]>,
  //
  // FP Load Multiple
  // FIXME: assumes 2 doubles which requires 2 LS cycles.
  InstrItinData<IIC_fpLoad_m, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                [1, 1, 1, 1], [], -1>, // dynamic uops
  //
  // FP Load Multiple + update
  // FIXME: assumes 2 doubles which requires 2 LS cycles.
  InstrItinData<IIC_fpLoad_mu,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                [2, 1, 1, 1], [], -1>, // dynamic uops
  //
  // Single-precision FP Store
  InstrItinData<IIC_fpStore32,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1]>,
  //
  // Double-precision FP Store
  InstrItinData<IIC_fpStore64,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1]>,
  //
  // FP Store Multiple
  // FIXME: assumes 2 doubles which requires 2 LS cycles.
  InstrItinData<IIC_fpStore_m,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsVFP], 0, Required>,
                               InstrStage<2, [A9_DRegsN],   0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                [1, 1, 1, 1], [], -1>, // dynamic uops
  //
  // FP Store Multiple + update
  // FIXME: assumes 2 doubles which requires 2 LS cycles.
  InstrItinData<IIC_fpStore_mu,[InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                                InstrStage<1, [A9_MUX0], 0>,
                                InstrStage<1, [A9_DRegsVFP], 0, Required>,
                                InstrStage<2, [A9_DRegsN],   0, Reserved>,
                                InstrStage<1, [A9_NPipe], 0>,
                                InstrStage<2, [A9_LSUnit]>],
                [2, 1, 1, 1], [], -1>, // dynamic uops
  // NEON
  // VLD1
  InstrItinData<IIC_VLD1,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1]>,
  // VLD1x2
  InstrItinData<IIC_VLD1x2,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1]>,
  // VLD1x3
  InstrItinData<IIC_VLD1x3,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 2, 1]>,
  // VLD1x4
  InstrItinData<IIC_VLD1x4,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 2, 2, 1]>,
  // VLD1u
  InstrItinData<IIC_VLD1u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 2, 1]>,
  // VLD1x2u
  InstrItinData<IIC_VLD1x2u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 2, 1]>,
  // VLD1x3u
  InstrItinData<IIC_VLD1x3u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 2, 2, 1]>,
  // VLD1x4u
  InstrItinData<IIC_VLD1x4u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 2, 2, 2, 1]>,
  //
  // VLD1ln
  InstrItinData<IIC_VLD1ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [3, 1, 1, 1]>,
  //
  // VLD1lnu
  InstrItinData<IIC_VLD1lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [3, 2, 1, 1, 1, 1]>,
  //
  // VLD1dup
  InstrItinData<IIC_VLD1dup,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1]>,
  //
  // VLD1dupu
  InstrItinData<IIC_VLD1dupu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 2, 1, 1]>,
  //
  // VLD2
  InstrItinData<IIC_VLD2,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 2, 1]>,
  //
  // VLD2x2
  InstrItinData<IIC_VLD2x2,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 3, 2, 3, 1]>,
  //
  // VLD2ln
  InstrItinData<IIC_VLD2ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [3, 3, 1, 1, 1, 1]>,
  //
  // VLD2u
  InstrItinData<IIC_VLD2u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 2, 2, 1, 1, 1]>,
  //
  // VLD2x2u
  InstrItinData<IIC_VLD2x2u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 3, 2, 3, 2, 1]>,
  //
  // VLD2lnu
  InstrItinData<IIC_VLD2lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [3, 3, 2, 1, 1, 1, 1, 1]>,
  //
  // VLD2dup
  InstrItinData<IIC_VLD2dup,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 2, 1]>,
  //
  // VLD2dupu
  InstrItinData<IIC_VLD2dupu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 2, 2, 1, 1]>,
  //
  // VLD3
  InstrItinData<IIC_VLD3,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 1]>,
  //
  // VLD3ln
  InstrItinData<IIC_VLD3ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<11,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<5, [A9_NPipe], 0>,
                               InstrStage<5, [A9_LSUnit]>],
                              [5, 5, 6, 1, 1, 1, 1, 2]>,
  //
  // VLD3u
  InstrItinData<IIC_VLD3u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 2, 1]>,
  //
  // VLD3lnu
  InstrItinData<IIC_VLD3lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<11,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<5, [A9_NPipe], 0>,
                               InstrStage<5, [A9_LSUnit]>],
                              [5, 5, 6, 2, 1, 1, 1, 1, 1, 2]>,
  //
  // VLD3dup
  InstrItinData<IIC_VLD3dup,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 1]>,
  //
  // VLD3dupu
  InstrItinData<IIC_VLD3dupu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 2, 1, 1]>,
  //
  // VLD4
  InstrItinData<IIC_VLD4,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 4, 1]>,
  //
  // VLD4ln
  InstrItinData<IIC_VLD4ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<10,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe], 0>,
                               InstrStage<4, [A9_LSUnit]>],
                              [4, 4, 5, 5, 1, 1, 1, 1, 2, 2]>,
  //
  // VLD4u
  InstrItinData<IIC_VLD4u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<9,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [3, 3, 4, 4, 2, 1]>,
  //
  // VLD4lnu
  InstrItinData<IIC_VLD4lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<10,[A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe], 0>,
                               InstrStage<4, [A9_LSUnit]>],
                              [4, 4, 5, 5, 2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VLD4dup
  InstrItinData<IIC_VLD4dup,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 2, 3, 3, 1]>,
  //
  // VLD4dupu
  InstrItinData<IIC_VLD4dupu, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 2, 3, 3, 2, 1, 1]>,
  //
  // VST1
  InstrItinData<IIC_VST1,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1]>,
  //
  // VST1x2
  InstrItinData<IIC_VST1x2,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1, 1]>,
  //
  // VST1x3
  InstrItinData<IIC_VST1x3,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST1x4
  InstrItinData<IIC_VST1x4,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST1u
  InstrItinData<IIC_VST1u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1]>,
  //
  // VST1x2u
  InstrItinData<IIC_VST1x2u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST1x3u
  InstrItinData<IIC_VST1x3u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST1x4u
  InstrItinData<IIC_VST1x4u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST1ln
  InstrItinData<IIC_VST1ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1]>,
  //
  // VST1lnu
  InstrItinData<IIC_VST1lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1]>,
  //
  // VST2
  InstrItinData<IIC_VST2,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1, 1]>,
  //
  // VST2x2
  InstrItinData<IIC_VST2x2,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST2u
  InstrItinData<IIC_VST2u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST2x2u
  InstrItinData<IIC_VST2x2u,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST2ln
  InstrItinData<IIC_VST2ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [1, 1, 1, 1]>,
  //
  // VST2lnu
  InstrItinData<IIC_VST2lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe], 0>,
                               InstrStage<1, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST3
  InstrItinData<IIC_VST3,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST3u
  InstrItinData<IIC_VST3u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST3ln
  InstrItinData<IIC_VST3ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST3lnu
  InstrItinData<IIC_VST3lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe], 0>,
                               InstrStage<3, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST4
  InstrItinData<IIC_VST4,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST4u
  InstrItinData<IIC_VST4u,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST4ln
  InstrItinData<IIC_VST4ln,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST4lnu
  InstrItinData<IIC_VST4lnu,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<2, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe], 0>,
                               InstrStage<2, [A9_LSUnit]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,

  //
  // Double-register Integer Unary
  InstrItinData<IIC_VUNAiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2]>,
  //
  // Quad-register Integer Unary
  InstrItinData<IIC_VUNAiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2]>,
  //
  // Double-register Integer Q-Unary
  InstrItinData<IIC_VQUNAiD,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Quad-register Integer CountQ-Unary
  InstrItinData<IIC_VQUNAiQ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1]>,
  //
  // Double-register Integer Binary
  InstrItinData<IIC_VBINiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 2, 2]>,
  //
  // Quad-register Integer Binary
  InstrItinData<IIC_VBINiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 2, 2]>,
  //
  // Double-register Integer Subtract
  InstrItinData<IIC_VSUBiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 2, 1]>,
  //
  // Quad-register Integer Subtract
  InstrItinData<IIC_VSUBiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 2, 1]>,
  //
  // Double-register Integer Shift
  InstrItinData<IIC_VSHLiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 1, 1]>,
  //
  // Quad-register Integer Shift
  InstrItinData<IIC_VSHLiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 1, 1]>,
  //
  // Double-register Integer Shift (4 cycle)
  InstrItinData<IIC_VSHLi4D,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1, 1]>,
  //
  // Quad-register Integer Shift (4 cycle)
  InstrItinData<IIC_VSHLi4Q,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 1, 1]>,
  //
  // Double-register Integer Binary (4 cycle)
  InstrItinData<IIC_VBINi4D,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2, 2]>,
  //
  // Quad-register Integer Binary (4 cycle)
  InstrItinData<IIC_VBINi4Q,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2, 2]>,
  //
  // Double-register Integer Subtract (4 cycle)
  InstrItinData<IIC_VSUBi4D,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2, 1]>,
  //
  // Quad-register Integer Subtract (4 cycle)
  InstrItinData<IIC_VSUBi4Q,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [4, 2, 1]>,

  //
  // Double-register Integer Count
  InstrItinData<IIC_VCNTiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 2, 2]>,
  //
  // Quad-register Integer Count
  // Result written in N3, but that is relative to the last cycle of multicycle,
  // so we use 4 for those cases
  InstrItinData<IIC_VCNTiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [4, 2, 2]>,
  //
  // Double-register Absolute Difference and Accumulate
  InstrItinData<IIC_VABAD,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [6, 3, 2, 1]>,
  //
  // Quad-register Absolute Difference and Accumulate
  InstrItinData<IIC_VABAQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 3, 2, 1]>,
  //
  // Double-register Integer Pair Add Long
  InstrItinData<IIC_VPALiD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [6, 3, 1]>,
  //
  // Quad-register Integer Pair Add Long
  InstrItinData<IIC_VPALiQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 3, 1]>,

  //
  // Double-register Integer Multiply (.8, .16)
  InstrItinData<IIC_VMULi16D, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [6, 2, 2]>,
  //
  // Quad-register Integer Multiply (.8, .16)
  InstrItinData<IIC_VMULi16Q, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [7, 2, 2]>,

  //
  // Double-register Integer Multiply (.32)
  InstrItinData<IIC_VMULi32D, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [7, 2, 1]>,
  //
  // Quad-register Integer Multiply (.32)
  InstrItinData<IIC_VMULi32Q, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 9 cycles
                               InstrStage<10, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe]>],
                              [9, 2, 1]>,
  //
  // Double-register Integer Multiply-Accumulate (.8, .16)
  InstrItinData<IIC_VMACi16D, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [6, 3, 2, 2]>,
  //
  // Double-register Integer Multiply-Accumulate (.32)
  InstrItinData<IIC_VMACi32D, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [7, 3, 2, 1]>,
  //
  // Quad-register Integer Multiply-Accumulate (.8, .16)
  InstrItinData<IIC_VMACi16Q, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [7, 3, 2, 2]>,
  //
  // Quad-register Integer Multiply-Accumulate (.32)
  InstrItinData<IIC_VMACi32Q, [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 9 cycles
                               InstrStage<10, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe]>],
                              [9, 3, 2, 1]>,

  //
  // Move
  InstrItinData<IIC_VMOV,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<1, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1,1]>,
  //
  // Move Immediate
  InstrItinData<IIC_VMOVImm,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3]>,
  //
  // Double-register Permute Move
  InstrItinData<IIC_VMOVD,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 1]>,
  //
  // Quad-register Permute Move
  InstrItinData<IIC_VMOVQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 1]>,
  //
  // Integer to Single-precision Move
  InstrItinData<IIC_VMOVIS ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1]>,
  //
  // Integer to Double-precision Move
  InstrItinData<IIC_VMOVID ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [1, 1, 1]>,
  //
  // Single-precision to Integer Move
  InstrItinData<IIC_VMOVSI ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 1]>,
  //
  // Double-precision to Integer Move
  InstrItinData<IIC_VMOVDI ,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<3, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 2, 1]>,
  //
  // Integer to Lane Move
  InstrItinData<IIC_VMOVISL , [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               InstrStage<4, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 1, 1]>,

  //
  // Vector narrow move
  InstrItinData<IIC_VMOVN,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [3, 1]>,
  //
  // Double-register FP Unary
  InstrItinData<IIC_VUNAD,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [5, 2]>,
  //
  // Quad-register FP Unary
  // Result written in N5, but that is relative to the last cycle of multicycle,
  // so we use 6 for those cases
  InstrItinData<IIC_VUNAQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 2]>,
  //
  // Double-register FP Binary
  // FIXME: We're using this itin for many instructions and [2, 2] here is too
  // optimistic.
  InstrItinData<IIC_VBIND,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [5, 2, 2]>,

  //
  // VPADD, etc.
  InstrItinData<IIC_VPBIND,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [5, 1, 1]>,
  //
  // Double-register FP VMUL
  InstrItinData<IIC_VFMULD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [5, 2, 1]>,
  //
  // Quad-register FP Binary
  // Result written in N5, but that is relative to the last cycle of multicycle,
  // so we use 6 for those cases
  // FIXME: We're using this itin for many instructions and [2, 2] here is too
  // optimistic.
  InstrItinData<IIC_VBINQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 2, 2]>,
  //
  // Quad-register FP VMUL
  InstrItinData<IIC_VFMULQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [6, 2, 1]>,
  //
  // Double-register FP Multiple-Accumulate
  InstrItinData<IIC_VMACD,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 3, 2, 1]>,
  //
  // Quad-register FP Multiple-Accumulate
  // Result written in N9, but that is relative to the last cycle of multicycle,
  // so we use 10 for those cases
  InstrItinData<IIC_VMACQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 9 cycles
                               InstrStage<10, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe]>],
                              [8, 4, 2, 1]>,
  //
  // Double-register Fused FP Multiple-Accumulate
  InstrItinData<IIC_VFMACD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [6, 3, 2, 1]>,
  //
  // Quad-register Fused FP Multiple-Accumulate
  // Result written in N9, but that is relative to the last cycle of multicycle,
  // so we use 10 for those cases
  InstrItinData<IIC_VFMACQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 9 cycles
                               InstrStage<10, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<4, [A9_NPipe]>],
                              [8, 4, 2, 1]>,
  //
  // Double-register Reciprical Step
  InstrItinData<IIC_VRECSD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 10 cycles
                               InstrStage<11, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [9, 2, 2]>,
  //
  // Quad-register Reciprical Step
  InstrItinData<IIC_VRECSQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 11 cycles
                               InstrStage<12, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [10, 2, 2]>,
  //
  // Double-register Permute
  InstrItinData<IIC_VPERMD,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 2, 1, 1]>,
  //
  // Quad-register Permute
  // Result written in N2, but that is relative to the last cycle of multicycle,
  // so we use 3 for those cases
  InstrItinData<IIC_VPERMQ,   [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 3, 1, 1]>,
  //
  // Quad-register Permute (3 cycle issue)
  // Result written in N2, but that is relative to the last cycle of multicycle,
  // so we use 4 for those cases
  InstrItinData<IIC_VPERMQ3,  [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 8 cycles
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe]>],
                              [4, 4, 1, 1]>,

  //
  // Double-register VEXT
  InstrItinData<IIC_VEXTD,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 6 cycles
                               InstrStage<7, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<1, [A9_NPipe]>],
                              [2, 1, 1]>,
  //
  // Quad-register VEXT
  InstrItinData<IIC_VEXTQ,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 1, 2]>,
  //
  // VTB
  InstrItinData<IIC_VTB1,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 2, 1]>,
  InstrItinData<IIC_VTB2,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<2, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 2, 2, 1]>,
  InstrItinData<IIC_VTB3,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<2, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 8 cycles
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe]>],
                              [4, 2, 2, 3, 1]>,
  InstrItinData<IIC_VTB4,     [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 8 cycles
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe]>],
                              [4, 2, 2, 3, 3, 1]>,
  //
  // VTBX
  InstrItinData<IIC_VTBX1,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 1, 2, 1]>,
  InstrItinData<IIC_VTBX2,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 7 cycles
                               InstrStage<8, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [3, 1, 2, 2, 1]>,
  InstrItinData<IIC_VTBX3,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 8 cycles
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<3, [A9_NPipe]>],
                              [4, 1, 2, 2, 3, 1]>,
  InstrItinData<IIC_VTBX4,    [InstrStage<1, [A9_Issue0, A9_Issue1], 0>,
                               InstrStage<1, [A9_MUX0], 0>,
                               InstrStage<1, [A9_DRegsN],   0, Required>,
                               // Extra latency cycles since wbck is 8 cycles
                               InstrStage<9, [A9_DRegsVFP], 0, Reserved>,
                               InstrStage<2, [A9_NPipe]>],
                              [4, 1, 2, 2, 3, 3, 1]>
]>;

// ===---------------------------------------------------------------------===//
// The following definitions describe the simpler per-operand machine model.
// This works with MachineScheduler and will eventually replace itineraries.

class A9WriteLMOpsListType<list<WriteSequence> writes> {
  list <WriteSequence> Writes = writes;
  SchedMachineModel SchedModel = ?;
}

// Cortex-A9 machine model for scheduling and other instruction cost heuristics.
def CortexA9Model : SchedMachineModel {
  let IssueWidth = 2; // 2 micro-ops are dispatched per cycle.
  let MicroOpBufferSize = 56; // Based on available renamed registers.
  let LoadLatency = 2; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 8; // Based on estimate of pipeline depth.

  let Itineraries = CortexA9Itineraries;

  // FIXME: Many vector operations were never given an itinerary. We
  // haven't mapped these to the new model either.
  let CompleteModel = 0;

  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.
//
// The AGU unit has BufferSize=1 so that the latency between operations
// that use it are considered to stall other operations.
//
// The FP unit has BufferSize=0 so that it is a hard dispatch
// hazard. No instruction may be dispatched while the unit is reserved.

let SchedModel = CortexA9Model in {

def A9UnitALU : ProcResource<2>;
def A9UnitMul : ProcResource<1> { let Super = A9UnitALU; }
def A9UnitAGU : ProcResource<1> { let BufferSize = 1; }
def A9UnitLS  : ProcResource<1>;
def A9UnitFP  : ProcResource<1> { let BufferSize = 0; }
def A9UnitB   : ProcResource<1>;

//===----------------------------------------------------------------------===//
// Define scheduler read/write types with their resources and latency on A9.

// Consume an issue slot, but no processor resources. This is useful when all
// other writes associated with the operand have NumMicroOps = 0.
def A9WriteIssue : SchedWriteRes<[]> { let Latency = 0; }

// Write an integer register.
def A9WriteI : SchedWriteRes<[A9UnitALU]>;
// Write an integer shifted-by register
def A9WriteIsr : SchedWriteRes<[A9UnitALU]> { let Latency = 2; }

// Basic ALU.
def A9WriteALU : SchedWriteRes<[A9UnitALU]>;
// ALU with operand shifted by immediate.
def : WriteRes<WriteALUsi, [A9UnitALU]> { let Latency = 2; }
// ALU with operand shifted by register.
def A9WriteALUsr : SchedWriteRes<[A9UnitALU]> { let Latency = 3; }

// Multiplication
def A9WriteM   : SchedWriteRes<[A9UnitMul, A9UnitMul]> { let Latency = 4; }
def A9WriteMHi : SchedWriteRes<[A9UnitMul]> { let Latency = 5;
                                              let NumMicroOps = 0; }
def A9WriteM16   : SchedWriteRes<[A9UnitMul]> { let Latency = 3; }
def A9WriteM16Hi : SchedWriteRes<[A9UnitMul]> { let Latency = 4;
                                                let NumMicroOps = 0; }
def : SchedAlias<WriteMUL16, A9WriteM16>;
def : SchedAlias<WriteMUL32, A9WriteM>;
def : SchedAlias<WriteMUL64Lo, A9WriteM>;
def : SchedAlias<WriteMUL64Hi, A9WriteMHi>;
def : SchedAlias<WriteMAC16, A9WriteM16>;
def : SchedAlias<WriteMAC32, A9WriteM>;
def : SchedAlias<WriteMAC64Lo, A9WriteM>;
def : SchedAlias<WriteMAC64Hi, A9WriteMHi>;
def : ReadAdvance<ReadMUL, 0>;
def : ReadAdvance<ReadMAC, 0>;

// Floating-point
// Only one FP or AGU instruction may issue per cycle. We model this
// by having FP instructions consume the AGU resource.
def A9WriteF      : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 4; }
def A9WriteFMov   : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 1; }
def A9WriteFMulS  : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 5; }
def A9WriteFMulD  : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 6; }
def A9WriteFMAS   : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 8; }

def A9WriteFMAD   : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 9; }
def A9WriteFDivS  : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 15; }
def A9WriteFDivD  : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 25; }
def A9WriteFSqrtS : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 17; }
def A9WriteFSqrtD : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 32; }

// NEON has an odd mix of latencies. Simply name the write types by latency.
def A9WriteV1 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 1; }
def A9WriteV2 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 2; }
def A9WriteV3 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 3; }
def A9WriteV4 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 4; }
def A9WriteV5 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 5; }
def A9WriteV6 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 6; }
def A9WriteV7 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 7; }
def A9WriteV9 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 9; }
def A9WriteV10 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> { let Latency = 10; }

def : WriteRes<WriteVLD1, []>;
def : WriteRes<WriteVLD2, []>;
def : WriteRes<WriteVLD3, []>;
def : WriteRes<WriteVLD4, []>;
def : WriteRes<WriteVST1, []>;
def : WriteRes<WriteVST2, []>;
def : WriteRes<WriteVST3, []>;
def : WriteRes<WriteVST4, []>;

// Reserve A9UnitFP for 2 consecutive cycles.
def A9Write2V4 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> {
  let Latency = 4;
  let ReleaseAtCycles = [2, 1];
}
def A9Write2V7 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> {
  let Latency = 7;
  let ReleaseAtCycles = [2, 1];
}
def A9Write2V9 : SchedWriteRes<[A9UnitFP, A9UnitAGU]> {
  let Latency = 9;
  let ReleaseAtCycles = [2, 1];
}

// Branches don't have a def operand but still consume resources.
def A9WriteB : SchedWriteRes<[A9UnitB]>;

// Address generation.
def A9WriteAdr : SchedWriteRes<[A9UnitAGU]> { let NumMicroOps = 0; }

// Load Integer.
def A9WriteL : SchedWriteRes<[A9UnitLS]> { let Latency = 3; }
def : SchedAlias<WriteLd, A9WriteL>;
// Load the upper 32-bits using the same micro-op.
def A9WriteLHi : SchedWriteRes<[]> { let Latency = 3;
                                     let NumMicroOps = 0; }
// Offset shifted by register.
def A9WriteLsi : SchedWriteRes<[A9UnitLS]> { let Latency = 4; }
// Load (and zero extend) a byte.
def A9WriteLb : SchedWriteRes<[A9UnitLS]> { let Latency = 4; }
def A9WriteLbsi : SchedWriteRes<[A9UnitLS]> { let Latency = 5; }

// Load or Store Float, aligned.
def A9WriteLSfp : SchedWriteRes<[A9UnitLS, A9UnitFP]> { let Latency = 1; }

// Store Integer.
def A9WriteS : SchedWriteRes<[A9UnitLS]>;

//===----------------------------------------------------------------------===//
// Define resources dynamically for load multiple variants.

// Define helpers for extra latency without consuming resources.
def A9WriteCycle1 : SchedWriteRes<[]> { let Latency = 1; let NumMicroOps = 0; }
foreach NumCycles = 2-8 in {
def A9WriteCycle#NumCycles : WriteSequence<[A9WriteCycle1], NumCycles>;
} // foreach NumCycles

// Define address generation sequences and predicates for 8 flavors of LDMs.
foreach NumAddr = 1-8 in {

// Define A9WriteAdr1-8 as a sequence of A9WriteAdr with additive
// latency for instructions that generate multiple loads or stores.
def A9WriteAdr#NumAddr : WriteSequence<[A9WriteAdr], NumAddr>;

// Define a predicate to select the LDM based on number of memory addresses.
def A9LMAdr#NumAddr#Pred :
  SchedPredicate<"(TII->getNumLDMAddresses(*MI)+1)/2 == "#NumAddr>;

} // foreach NumAddr

// Fall-back for unknown LDMs.
def A9LMUnknownPred : SchedPredicate<"TII->getNumLDMAddresses(*MI) == 0">;

// LDM/VLDM/VLDn address generation latency & resources.
// Dynamically select the A9WriteAdrN sequence using a predicate.
def A9WriteLMAdr : SchedWriteVariant<[
  SchedVar<A9LMAdr1Pred, [A9WriteAdr1]>,
  SchedVar<A9LMAdr2Pred, [A9WriteAdr2]>,
  SchedVar<A9LMAdr3Pred, [A9WriteAdr3]>,
  SchedVar<A9LMAdr4Pred, [A9WriteAdr4]>,
  SchedVar<A9LMAdr5Pred, [A9WriteAdr5]>,
  SchedVar<A9LMAdr6Pred, [A9WriteAdr6]>,
  SchedVar<A9LMAdr7Pred, [A9WriteAdr7]>,
  SchedVar<A9LMAdr8Pred, [A9WriteAdr8]>,
  // For unknown LDM/VLDM/VSTM, assume 2 32-bit registers.
  SchedVar<A9LMUnknownPred, [A9WriteAdr2]>]>;

// Define LDM Resources.
// These take no issue resource, so they can be combined with other
// writes like WriteB.
// A9WriteLMLo takes a single LS resource and 2 cycles.
def A9WriteLMLo : SchedWriteRes<[A9UnitLS]> { let Latency = 2;
                                              let NumMicroOps = 0; }
// Assuming aligned access, the upper half of each pair is free with
// the same latency.
def A9WriteLMHi : SchedWriteRes<[]> { let Latency = 2;
                                      let NumMicroOps = 0; }
// Each A9WriteL#N variant adds N cycles of latency without consuming
// additional resources.
foreach NumAddr = 1-8 in {
def A9WriteL#NumAddr : WriteSequence<
  [A9WriteLMLo, !cast<SchedWrite>("A9WriteCycle"#NumAddr)]>;
def A9WriteL#NumAddr#Hi : WriteSequence<
  [A9WriteLMHi, !cast<SchedWrite>("A9WriteCycle"#NumAddr)]>;
}

//===----------------------------------------------------------------------===//
// LDM: Load multiple into 32-bit integer registers.

def A9WriteLMOpsList : A9WriteLMOpsListType<
                 [A9WriteL1, A9WriteL1Hi,
                  A9WriteL2, A9WriteL2Hi,
                  A9WriteL3, A9WriteL3Hi,
                  A9WriteL4, A9WriteL4Hi,
                  A9WriteL5, A9WriteL5Hi,
                  A9WriteL6, A9WriteL6Hi,
                  A9WriteL7, A9WriteL7Hi,
                  A9WriteL8, A9WriteL8Hi]>;

// A9WriteLM variants expand into a pair of writes for each 64-bit
// value loaded. When the number of registers is odd, the last
// A9WriteLnHi is naturally ignored because the instruction has no
// following def operands.  These variants take no issue resource, so
// they may need to be part of a WriteSequence that includes A9WriteIssue.
def A9WriteLM : SchedWriteVariant<[
  SchedVar<A9LMAdr1Pred, A9WriteLMOpsList.Writes[0-1]>,
  SchedVar<A9LMAdr2Pred, A9WriteLMOpsList.Writes[0-3]>,
  SchedVar<A9LMAdr3Pred, A9WriteLMOpsList.Writes[0-5]>,
  SchedVar<A9LMAdr4Pred, A9WriteLMOpsList.Writes[0-7]>,
  SchedVar<A9LMAdr5Pred, A9WriteLMOpsList.Writes[0-9]>,
  SchedVar<A9LMAdr6Pred, A9WriteLMOpsList.Writes[0-11]>,
  SchedVar<A9LMAdr7Pred, A9WriteLMOpsList.Writes[0-13]>,
  SchedVar<A9LMAdr8Pred, A9WriteLMOpsList.Writes[0-15]>,
  // For unknown LDMs, define the maximum number of writes, but only
  // make the first two consume resources.
  SchedVar<A9LMUnknownPred, [A9WriteL1, A9WriteL1Hi,
                             A9WriteL2, A9WriteL2Hi,
                             A9WriteL3Hi, A9WriteL3Hi,
                             A9WriteL4Hi, A9WriteL4Hi,
                             A9WriteL5Hi, A9WriteL5Hi,
                             A9WriteL6Hi, A9WriteL6Hi,
                             A9WriteL7Hi, A9WriteL7Hi,
                             A9WriteL8Hi, A9WriteL8Hi]>]> {
  let Variadic = 1;
}

//===----------------------------------------------------------------------===//
// VFP Load/Store Multiple Variants, and NEON VLDn/VSTn support.

// A9WriteLfpOp is the same as A9WriteLSfp but takes no issue resources
// so can be used in WriteSequences for in single-issue instructions that
// encapsulate multiple loads.
def A9WriteLfpOp : SchedWriteRes<[A9UnitLS, A9UnitFP]> {
  let Latency = 1;
  let NumMicroOps = 0;
}

foreach NumAddr = 1-8 in {

// Helper for A9WriteLfp1-8: A sequence of fp loads with no micro-ops.
def A9WriteLfp#NumAddr#Seq : WriteSequence<[A9WriteLfpOp], NumAddr>;

// A9WriteLfp1-8 definitions are statically expanded into a sequence of
// A9WriteLfpOps with additive latency that takes a single issue slot.
// Used directly to describe NEON VLDn.
def A9WriteLfp#NumAddr : WriteSequence<
  [A9WriteIssue, !cast<SchedWrite>("A9WriteLfp"#NumAddr#Seq)]>;

// A9WriteLfp1-8Mov adds a cycle of latency and FP resource for
// permuting loaded values.
def A9WriteLfp#NumAddr#Mov : WriteSequence<
  [A9WriteF, !cast<SchedWrite>("A9WriteLfp"#NumAddr#Seq)]>;

} // foreach NumAddr

// Define VLDM/VSTM PreRA resources.
// A9WriteLMfpPreRA are dynamically expanded into the correct
// A9WriteLfp1-8 sequence based on a predicate. This supports the
// preRA VLDM variants in which all 64-bit loads are written to the
// same tuple of either single or double precision registers.
def A9WriteLMfpPreRA : SchedWriteVariant<[
  SchedVar<A9LMAdr1Pred, [A9WriteLfp1]>,
  SchedVar<A9LMAdr2Pred, [A9WriteLfp2]>,
  SchedVar<A9LMAdr3Pred, [A9WriteLfp3]>,
  SchedVar<A9LMAdr4Pred, [A9WriteLfp4]>,
  SchedVar<A9LMAdr5Pred, [A9WriteLfp5]>,
  SchedVar<A9LMAdr6Pred, [A9WriteLfp6]>,
  SchedVar<A9LMAdr7Pred, [A9WriteLfp7]>,
  SchedVar<A9LMAdr8Pred, [A9WriteLfp8]>,
  // For unknown VLDM/VSTM PreRA, assume 2xS registers.
  SchedVar<A9LMUnknownPred, [A9WriteLfp2]>]>;

// Define VLDM/VSTM PostRA Resources.
// A9WriteLMfpLo takes a LS and FP resource and one issue slot but no latency.
def A9WriteLMfpLo : SchedWriteRes<[A9UnitLS, A9UnitFP]> { let Latency = 0; }

foreach NumAddr = 1-8 in {

// Each A9WriteL#N variant adds N cycles of latency without consuming
// additional resources.
def A9WriteLMfp#NumAddr : WriteSequence<
  [A9WriteLMfpLo, !cast<SchedWrite>("A9WriteCycle"#NumAddr)]>;

// Assuming aligned access, the upper half of each pair is free with
// the same latency.
def A9WriteLMfp#NumAddr#Hi : WriteSequence<
  [A9WriteLMHi, !cast<SchedWrite>("A9WriteCycle"#NumAddr)]>;

} // foreach NumAddr

// VLDM PostRA Variants. These variants expand A9WriteLMfpPostRA into a
// pair of writes for each 64-bit data loaded. When the number of
// registers is odd, the last WriteLMfpnHi is naturally ignored because
// the instruction has no following def operands.

def A9WriteLMfpPostRAOpsList : A9WriteLMOpsListType<
                 [A9WriteLMfp1, A9WriteLMfp2,       // 0-1
                  A9WriteLMfp3, A9WriteLMfp4,       // 2-3
                  A9WriteLMfp5, A9WriteLMfp6,       // 4-5
                  A9WriteLMfp7, A9WriteLMfp8,       // 6-7
                  A9WriteLMfp1Hi,                   // 8-8
                  A9WriteLMfp2Hi, A9WriteLMfp2Hi,   // 9-10
                  A9WriteLMfp3Hi, A9WriteLMfp3Hi,   // 11-12
                  A9WriteLMfp4Hi, A9WriteLMfp4Hi,   // 13-14
                  A9WriteLMfp5Hi, A9WriteLMfp5Hi,   // 15-16
                  A9WriteLMfp6Hi, A9WriteLMfp6Hi,   // 17-18
                  A9WriteLMfp7Hi, A9WriteLMfp7Hi,   // 19-20
                  A9WriteLMfp8Hi, A9WriteLMfp8Hi]>; // 21-22

def A9WriteLMfpPostRA : SchedWriteVariant<[
  SchedVar<A9LMAdr1Pred, A9WriteLMfpPostRAOpsList.Writes[0-0, 8-8]>,
  SchedVar<A9LMAdr2Pred, A9WriteLMfpPostRAOpsList.Writes[0-1, 9-10]>,
  SchedVar<A9LMAdr3Pred, A9WriteLMfpPostRAOpsList.Writes[0-2, 10-12]>,
  SchedVar<A9LMAdr4Pred, A9WriteLMfpPostRAOpsList.Writes[0-3, 11-14]>,
  SchedVar<A9LMAdr5Pred, A9WriteLMfpPostRAOpsList.Writes[0-4, 12-16]>,
  SchedVar<A9LMAdr6Pred, A9WriteLMfpPostRAOpsList.Writes[0-5, 13-18]>,
  SchedVar<A9LMAdr7Pred, A9WriteLMfpPostRAOpsList.Writes[0-6, 14-20]>,
  SchedVar<A9LMAdr8Pred, A9WriteLMfpPostRAOpsList.Writes[0-7, 15-22]>,
  // For unknown LDMs, define the maximum number of writes, but only
  // make the first two consume resources. We are optimizing for the case
  // where the operands are DPRs, and this determines the first eight
  // types. The remaining eight types are filled to cover the case
  // where the operands are SPRs.
  SchedVar<A9LMUnknownPred, [A9WriteLMfp1, A9WriteLMfp2,
                             A9WriteLMfp3Hi, A9WriteLMfp4Hi,
                             A9WriteLMfp5Hi, A9WriteLMfp6Hi,
                             A9WriteLMfp7Hi, A9WriteLMfp8Hi,
                             A9WriteLMfp5Hi, A9WriteLMfp5Hi,
                             A9WriteLMfp6Hi, A9WriteLMfp6Hi,
                             A9WriteLMfp7Hi, A9WriteLMfp7Hi,
                             A9WriteLMfp8Hi, A9WriteLMfp8Hi]>]> {
  let Variadic = 1;
}

// Distinguish between our multiple MI-level forms of the same
// VLDM/VSTM instructions.
def A9PreRA : SchedPredicate<
  "MI->getOperand(0).getReg().isVirtual()">;
def A9PostRA : SchedPredicate<
  "MI->getOperand(0).getReg().isPhysical()">;

// VLDM represents all destination registers as a single register
// tuple, unlike LDM. So the number of write operands is not variadic.
def A9WriteLMfp : SchedWriteVariant<[
  SchedVar<A9PreRA, [A9WriteLMfpPreRA]>,
  SchedVar<A9PostRA, [A9WriteLMfpPostRA]>]>;

//===----------------------------------------------------------------------===//
// Resources for other (non-LDM/VLDM) Variants.

// These mov immediate writers are unconditionally expanded with
// additive latency.
def A9WriteI2 : WriteSequence<[A9WriteI, A9WriteI]>;
def A9WriteI2pc : WriteSequence<[A9WriteI, A9WriteI, WriteALU]>;
def A9WriteI2ld  : WriteSequence<[A9WriteI, A9WriteI, A9WriteL]>;

// Some ALU operations can read loaded integer values one cycle early.
def A9ReadALU : SchedReadAdvance<1,
  [A9WriteL, A9WriteLHi, A9WriteLsi, A9WriteLb, A9WriteLbsi,
   A9WriteL1, A9WriteL2, A9WriteL3, A9WriteL4,
   A9WriteL5, A9WriteL6, A9WriteL7, A9WriteL8,
   A9WriteL1Hi, A9WriteL2Hi, A9WriteL3Hi, A9WriteL4Hi,
   A9WriteL5Hi, A9WriteL6Hi, A9WriteL7Hi, A9WriteL8Hi]>;

// Read types for operands that are unconditionally read in cycle N
// after the instruction issues, decreases producer latency by N-1.
def A9Read2 : SchedReadAdvance<1>;
def A9Read3 : SchedReadAdvance<2>;
def A9Read4 : SchedReadAdvance<3>;

//===----------------------------------------------------------------------===//
// Map itinerary classes to scheduler read/write resources per operand.
//
// For ARM, we piggyback scheduler resources on the Itinerary classes
// to avoid perturbing the existing instruction definitions.

// This table follows the ARM Cortex-A9 Technical Reference Manuals,
// mostly in order.

def :ItinRW<[WriteALU], [IIC_iMOVi,IIC_iMOVr,IIC_iMOVsi,
                         IIC_iMVNi,IIC_iMVNsi,
                         IIC_iCMOVi,IIC_iCMOVr,IIC_iCMOVsi]>;
def :ItinRW<[WriteALU, A9ReadALU],[IIC_iMVNr]>;
def :ItinRW<[A9WriteIsr], [IIC_iMOVsr,IIC_iMVNsr,IIC_iCMOVsr]>;

def :ItinRW<[A9WriteI2],   [IIC_iMOVix2,IIC_iCMOVix2]>;
def :ItinRW<[A9WriteI2pc], [IIC_iMOVix2addpc]>;
def :ItinRW<[A9WriteI2ld], [IIC_iMOVix2ld]>;

def :ItinRW<[WriteALU], [IIC_iBITi,IIC_iBITr,IIC_iUNAr,IIC_iTSTi,IIC_iTSTr]>;
def :ItinRW<[WriteALU, A9ReadALU], [IIC_iALUi, IIC_iCMPi, IIC_iCMPsi]>;
def :ItinRW<[WriteALU, A9ReadALU, A9ReadALU],[IIC_iALUr,IIC_iCMPr]>;
def :ItinRW<[WriteALUsi], [IIC_iBITsi,IIC_iUNAsi,IIC_iEXTr,IIC_iTSTsi]>;
def :ItinRW<[WriteALUsi, A9ReadALU], [IIC_iALUsi]>;
def :ItinRW<[WriteALUsi, ReadDefault, A9ReadALU], [IIC_iALUsir]>; // RSB
def :ItinRW<[A9WriteALUsr], [IIC_iBITsr,IIC_iTSTsr,IIC_iEXTAr,IIC_iEXTAsr]>;
def :ItinRW<[A9WriteALUsr, A9ReadALU], [IIC_iALUsr,IIC_iCMPsr]>;

// A9WriteHi ignored for MUL32.
def :ItinRW<[A9WriteM, A9WriteMHi], [IIC_iMUL32,IIC_iMAC32,
                                     IIC_iMUL64,IIC_iMAC64]>;
// FIXME: SMLALxx needs itin classes
def :ItinRW<[A9WriteM16, A9WriteM16Hi], [IIC_iMUL16,IIC_iMAC16]>;

// TODO: For floating-point ops, we model the pipeline forwarding
// latencies here. WAW latencies are sometimes longer.

def :ItinRW<[A9WriteFMov], [IIC_fpSTAT, IIC_fpMOVIS, IIC_fpMOVID, IIC_fpMOVSI,
                            IIC_fpUNA32, IIC_fpUNA64,
                            IIC_fpCMP32, IIC_fpCMP64]>;
def :ItinRW<[A9WriteFMov, A9WriteFMov], [IIC_fpMOVDI]>;
def :ItinRW<[A9WriteF], [IIC_fpCVTSD, IIC_fpCVTDS, IIC_fpCVTSH, IIC_fpCVTHS,
                         IIC_fpCVTIS, IIC_fpCVTID, IIC_fpCVTSI, IIC_fpCVTDI,
                         IIC_fpALU32, IIC_fpALU64]>;
def :ItinRW<[A9WriteFMulS], [IIC_fpMUL32]>;
def :ItinRW<[A9WriteFMulD], [IIC_fpMUL64]>;
def :ItinRW<[A9WriteFMAS], [IIC_fpMAC32]>;
def :ItinRW<[A9WriteFMAD], [IIC_fpMAC64]>;
def :ItinRW<[A9WriteFDivS], [IIC_fpDIV32]>;
def :ItinRW<[A9WriteFDivD], [IIC_fpDIV64]>;
def :ItinRW<[A9WriteFSqrtS], [IIC_fpSQRT32]>;
def :ItinRW<[A9WriteFSqrtD], [IIC_fpSQRT64]>;

def :ItinRW<[A9WriteB], [IIC_Br]>;

// A9 PLD is processed in a dedicated unit.
def :ItinRW<[], [IIC_Preload]>;

// Note: We must assume that loads are aligned, since the machine
// model cannot know this statically and A9 ignores alignment hints.

// A9WriteAdr consumes AGU regardless address writeback. But it's
// latency is only relevant for users of an updated address.
def :ItinRW<[A9WriteL, A9WriteAdr], [IIC_iLoad_i,IIC_iLoad_r,
                                     IIC_iLoad_iu,IIC_iLoad_ru]>;
def :ItinRW<[A9WriteLsi, A9WriteAdr], [IIC_iLoad_si,IIC_iLoad_siu]>;
def :ItinRW<[A9WriteLb, A9WriteAdr2], [IIC_iLoad_bh_i,IIC_iLoad_bh_r,
                                       IIC_iLoad_bh_iu,IIC_iLoad_bh_ru]>;
def :ItinRW<[A9WriteLbsi, A9WriteAdr2], [IIC_iLoad_bh_si,IIC_iLoad_bh_siu]>;
def :ItinRW<[A9WriteL, A9WriteLHi, A9WriteAdr], [IIC_iLoad_d_i,IIC_iLoad_d_r,
                                            IIC_iLoad_d_ru]>;
// Store either has no def operands, or the one def for address writeback.
def :ItinRW<[A9WriteAdr, A9WriteS], [IIC_iStore_i, IIC_iStore_r,
                                     IIC_iStore_iu, IIC_iStore_ru,
                                     IIC_iStore_d_i, IIC_iStore_d_r,
                                     IIC_iStore_d_ru]>;
def :ItinRW<[A9WriteAdr2, A9WriteS], [IIC_iStore_si, IIC_iStore_siu,
                                      IIC_iStore_bh_i, IIC_iStore_bh_r,
                                      IIC_iStore_bh_iu, IIC_iStore_bh_ru]>;
def :ItinRW<[A9WriteAdr3, A9WriteS], [IIC_iStore_bh_si, IIC_iStore_bh_siu]>;

// A9WriteML will be expanded into a separate write for each def
// operand. Address generation consumes resources, but A9WriteLMAdr
// is listed after all def operands, so has no effective latency.
//
// Note: A9WriteLM expands into an even number of def operands. The
// actual number of def operands may be less by one.
def :ItinRW<[A9WriteLM, A9WriteLMAdr, A9WriteIssue], [IIC_iLoad_m, IIC_iPop]>;

// Load multiple with address writeback has an extra def operand in
// front of the loaded registers.
//
// Reuse the load-multiple variants for store-multiple because the
// resources are identical, For stores only the address writeback
// has a def operand so the WriteL latencies are unused.
def :ItinRW<[A9WriteLMAdr, A9WriteLM, A9WriteIssue], [IIC_iLoad_mu,
                                                      IIC_iStore_m,
                                                      IIC_iStore_mu]>;
def :ItinRW<[A9WriteLM, A9WriteLMAdr, A9WriteB], [IIC_iLoad_mBr, IIC_iPop_Br]>;
def :ItinRW<[A9WriteL, A9WriteAdr, WriteALU], [IIC_iLoadiALU]>;

def :ItinRW<[A9WriteLSfp, A9WriteAdr], [IIC_fpLoad32, IIC_fpLoad64]>;

def :ItinRW<[A9WriteLMfp, A9WriteLMAdr], [IIC_fpLoad_m]>;
def :ItinRW<[A9WriteLMAdr, A9WriteLMfp], [IIC_fpLoad_mu]>;
def :ItinRW<[A9WriteAdr, A9WriteLSfp], [IIC_fpStore32, IIC_fpStore64,
                                        IIC_fpStore_m, IIC_fpStore_mu]>;

// Note: Unlike VLDM, VLD1 expects the writeback operand after the
// normal writes.
def :ItinRW<[A9WriteLfp1, A9WriteAdr1], [IIC_VLD1, IIC_VLD1u,
                                         IIC_VLD1x2, IIC_VLD1x2u]>;
def :ItinRW<[A9WriteLfp2, A9WriteAdr2], [IIC_VLD1x3, IIC_VLD1x3u,
                                         IIC_VLD1x4, IIC_VLD1x4u,
                                         IIC_VLD4dup, IIC_VLD4dupu]>;
def :ItinRW<[A9WriteLfp1Mov, A9WriteAdr1], [IIC_VLD1dup, IIC_VLD1dupu,
                                            IIC_VLD2, IIC_VLD2u,
                                            IIC_VLD2dup, IIC_VLD2dupu]>;
def :ItinRW<[A9WriteLfp2Mov, A9WriteAdr1], [IIC_VLD1ln, IIC_VLD1lnu,
                                            IIC_VLD2x2, IIC_VLD2x2u,
                                            IIC_VLD2ln, IIC_VLD2lnu]>;
def :ItinRW<[A9WriteLfp3Mov, A9WriteAdr3], [IIC_VLD3, IIC_VLD3u,
                                            IIC_VLD3dup, IIC_VLD3dupu]>;
def :ItinRW<[A9WriteLfp4Mov, A9WriteAdr4], [IIC_VLD4, IIC_VLD4u,
                                            IIC_VLD4ln, IIC_VLD4lnu]>;
def :ItinRW<[A9WriteLfp5Mov, A9WriteAdr5], [IIC_VLD3ln, IIC_VLD3lnu]>;

// Vector stores use similar resources to vector loads, so use the
// same write types. The address write must be first for stores with
// address writeback.
def :ItinRW<[A9WriteAdr1, A9WriteLfp1], [IIC_VST1, IIC_VST1u,
                                         IIC_VST1x2, IIC_VST1x2u,
                                         IIC_VST1ln, IIC_VST1lnu,
                                         IIC_VST2, IIC_VST2u,
                                         IIC_VST2x2, IIC_VST2x2u,
                                         IIC_VST2ln, IIC_VST2lnu]>;
def :ItinRW<[A9WriteAdr2, A9WriteLfp2], [IIC_VST1x3, IIC_VST1x3u,
                                         IIC_VST1x4, IIC_VST1x4u,
                                         IIC_VST3, IIC_VST3u,
                                         IIC_VST3ln, IIC_VST3lnu,
                                         IIC_VST4, IIC_VST4u,
                                         IIC_VST4ln, IIC_VST4lnu]>;

// NEON moves.
def :ItinRW<[A9WriteV2], [IIC_VMOVSI, IIC_VMOVDI, IIC_VMOVD, IIC_VMOVQ]>;
def :ItinRW<[A9WriteV1], [IIC_VMOV, IIC_VMOVIS, IIC_VMOVID]>;
def :ItinRW<[A9WriteV3], [IIC_VMOVISL, IIC_VMOVN]>;

// NEON integer arithmetic
//
// VADD/VAND/VORR/VEOR/VBIC/VORN/VBIT/VBIF/VBSL
def :ItinRW<[A9WriteV3, A9Read2, A9Read2], [IIC_VBINiD, IIC_VBINiQ]>;
// VSUB/VMVN/VCLSD/VCLZD/VCNTD
def :ItinRW<[A9WriteV3, A9Read2], [IIC_VSUBiD, IIC_VSUBiQ, IIC_VCNTiD]>;
// VADDL/VSUBL/VNEG are mapped later under IIC_SHLi.
// ...
// VHADD/VRHADD/VQADD/VTST/VADH/VRADH
def :ItinRW<[A9WriteV4, A9Read2, A9Read2], [IIC_VBINi4D, IIC_VBINi4Q]>;

// VSBH/VRSBH/VHSUB/VQSUB/VABD/VCEQ/VCGE/VCGT/VMAX/VMIN/VPMAX/VPMIN/VABDL
def :ItinRW<[A9WriteV4, A9Read2], [IIC_VSUBi4D, IIC_VSUBi4Q]>;
// VQNEG/VQABS
def :ItinRW<[A9WriteV4], [IIC_VQUNAiD, IIC_VQUNAiQ]>;
// VABS
def :ItinRW<[A9WriteV4, A9Read2], [IIC_VUNAiD, IIC_VUNAiQ]>;
// VPADD/VPADDL are mapped later under IIC_SHLi.
// ...
// VCLSQ/VCLZQ/VCNTQ, takes two cycles.
def :ItinRW<[A9Write2V4, A9Read3], [IIC_VCNTiQ]>;
// VMOVimm/VMVNimm/VORRimm/VBICimm
def :ItinRW<[A9WriteV3], [IIC_VMOVImm]>;
def :ItinRW<[A9WriteV6, A9Read3, A9Read2], [IIC_VABAD, IIC_VABAQ]>;
def :ItinRW<[A9WriteV6, A9Read3], [IIC_VPALiD, IIC_VPALiQ]>;

// NEON integer multiply
//
// Note: these don't quite match the timing docs, but they do match
// the original A9 itinerary.
def :ItinRW<[A9WriteV6, A9Read2, A9Read2], [IIC_VMULi16D]>;
def :ItinRW<[A9WriteV7, A9Read2, A9Read2], [IIC_VMULi16Q]>;
def :ItinRW<[A9Write2V7, A9Read2], [IIC_VMULi32D]>;
def :ItinRW<[A9Write2V9, A9Read2], [IIC_VMULi32Q]>;
def :ItinRW<[A9WriteV6, A9Read3, A9Read2, A9Read2], [IIC_VMACi16D]>;
def :ItinRW<[A9WriteV7, A9Read3, A9Read2, A9Read2], [IIC_VMACi16Q]>;
def :ItinRW<[A9Write2V7, A9Read3, A9Read2], [IIC_VMACi32D]>;
def :ItinRW<[A9Write2V9, A9Read3, A9Read2], [IIC_VMACi32Q]>;

// NEON integer shift
// TODO: Q,Q,Q shifts should actually reserve FP for 2 cycles.
def :ItinRW<[A9WriteV3], [IIC_VSHLiD, IIC_VSHLiQ]>;
def :ItinRW<[A9WriteV4], [IIC_VSHLi4D, IIC_VSHLi4Q]>;

// NEON permute
def :ItinRW<[A9WriteV2, A9WriteV2], [IIC_VPERMD, IIC_VPERMQ, IIC_VEXTD]>;
def :ItinRW<[A9WriteV3, A9WriteV4, ReadDefault, A9Read2],
            [IIC_VPERMQ3, IIC_VEXTQ]>;
def :ItinRW<[A9WriteV3, A9Read2], [IIC_VTB1]>;
def :ItinRW<[A9WriteV3, A9Read2, A9Read2], [IIC_VTB2]>;
def :ItinRW<[A9WriteV4, A9Read2, A9Read2, A9Read3], [IIC_VTB3]>;
def :ItinRW<[A9WriteV4, A9Read2, A9Read2, A9Read3, A9Read3], [IIC_VTB4]>;
def :ItinRW<[A9WriteV3, ReadDefault, A9Read2], [IIC_VTBX1]>;
def :ItinRW<[A9WriteV3, ReadDefault, A9Read2, A9Read2], [IIC_VTBX2]>;
def :ItinRW<[A9WriteV4, ReadDefault, A9Read2, A9Read2, A9Read3], [IIC_VTBX3]>;
def :ItinRW<[A9WriteV4, ReadDefault, A9Read2, A9Read2, A9Read3, A9Read3],
            [IIC_VTBX4]>;

// NEON floating-point
def :ItinRW<[A9WriteV5, A9Read2, A9Read2], [IIC_VBIND]>;
def :ItinRW<[A9WriteV6, A9Read2, A9Read2], [IIC_VBINQ]>;
def :ItinRW<[A9WriteV5, A9Read2], [IIC_VUNAD, IIC_VFMULD]>;
def :ItinRW<[A9WriteV6, A9Read2], [IIC_VUNAQ, IIC_VFMULQ]>;
def :ItinRW<[A9WriteV9, A9Read3, A9Read2], [IIC_VMACD, IIC_VFMACD]>;
def :ItinRW<[A9WriteV10, A9Read3, A9Read2], [IIC_VMACQ, IIC_VFMACQ]>;
def :ItinRW<[A9WriteV9, A9Read2, A9Read2], [IIC_VRECSD]>;
def :ItinRW<[A9WriteV10, A9Read2, A9Read2], [IIC_VRECSQ]>;

// Map SchedRWs that are identical for cortexa9 to existing resources.
def : SchedAlias<WriteALU, A9WriteALU>;
def : SchedAlias<WriteALUsr, A9WriteALUsr>;
def : SchedAlias<WriteALUSsr, A9WriteALUsr>;
def : SchedAlias<ReadALU, A9ReadALU>;
def : SchedAlias<ReadALUsr, A9ReadALU>;
def : SchedAlias<WriteST, A9WriteS>;

// ===---------------------------------------------------------------------===//
// Floating-point. Map target defined SchedReadWrite to processor specific ones
//
def : WriteRes<WriteFPCVT, [A9UnitFP, A9UnitAGU]> { let Latency = 4; }
def : SchedAlias<WriteFPMOV, A9WriteFMov>;

def : SchedAlias<WriteFPALU32, A9WriteF>;
def : SchedAlias<WriteFPALU64, A9WriteF>;

def : SchedAlias<WriteFPMUL32, A9WriteFMulS>;
def : SchedAlias<WriteFPMUL64, A9WriteFMulD>;

def : SchedAlias<WriteFPMAC32, A9WriteFMAS>;
def : SchedAlias<WriteFPMAC64, A9WriteFMAD>;

def : SchedAlias<WriteFPDIV32, A9WriteFDivS>;
def : SchedAlias<WriteFPDIV64, A9WriteFDivD>;
def : SchedAlias<WriteFPSQRT32, A9WriteFSqrtS>;
def : SchedAlias<WriteFPSQRT64, A9WriteFSqrtD>;

def : ReadAdvance<ReadFPMUL, 0>;
def : ReadAdvance<ReadFPMAC, 0>;

// ===---------------------------------------------------------------------===//
// Subtarget-specific overrides. Map opcodes to list of SchedReadWrite types.
//
def : InstRW< [WriteALU],
      (instregex "ANDri", "ORRri", "EORri", "BICri", "ANDrr", "ORRrr", "EORrr",
                 "BICrr")>;
def : InstRW< [WriteALUsi], (instrs ANDrsi, ORRrsi, EORrsi, BICrsi)>;
def : InstRW< [WriteALUsr], (instrs ANDrsr, ORRrsr, EORrsr, BICrsr)>;


def : SchedAlias<WriteCMP, A9WriteALU>;
def : SchedAlias<WriteCMPsi, A9WriteALU>;
def : SchedAlias<WriteCMPsr, A9WriteALU>;

def : InstRW< [A9WriteIsr], (instregex "MOVsr", "MOVsi", "MVNsr", "MOVCCsi",
                                       "MOVCCsr")>;
def : InstRW< [WriteALU, A9ReadALU], (instregex "MVNr")>;
def : InstRW< [A9WriteI2], (instregex "MOVCCi32imm", "MOVi32imm")>;
def : InstRW< [A9WriteI2pc], (instregex "MOV_ga_pcrel")>;
def : InstRW< [A9WriteI2ld], (instregex "MOV_ga_pcrel_ldr")>;

def : InstRW< [WriteALU], (instregex "SEL")>;

def : InstRW< [WriteALUsi], (instregex "BFC", "BFI", "UBFX", "SBFX")>;

def : InstRW< [A9WriteM],
      (instregex "MUL", "MULv5", "SMMUL", "SMMULR", "MLA", "MLAv5", "MLS",
      "SMMLA", "SMMLAR", "SMMLS", "SMMLSR")>;
def : InstRW< [A9WriteM, A9WriteMHi],
      (instregex "SMULL", "SMULLv5", "UMULL", "UMULLv5", "SMLAL$", "UMLAL",
      "UMAAL", "SMLALv5", "UMLALv5", "SMLALBB", "SMLALBT", "SMLALTB",
      "SMLALTT")>;
// FIXME: These instructions used to have NoItinerary. Just copied the one from above.
def : InstRW< [A9WriteM, A9WriteMHi],
      (instregex "SMLAD", "SMLADX", "SMLALD", "SMLALDX", "SMLSD", "SMLSDX",
      "SMLSLD", "SMLSLDX", "SMUAD", "SMUADX", "SMUSD", "SMUSDX")>;

def : InstRW<[A9WriteM16, A9WriteM16Hi],
      (instregex "SMULBB", "SMULBT", "SMULTB", "SMULTT", "SMULWB", "SMULWT")>;
def : InstRW<[A9WriteM16, A9WriteM16Hi],
      (instregex "SMLABB", "SMLABT", "SMLATB", "SMLATT", "SMLAWB", "SMLAWT")>;

def : InstRW<[A9WriteL], (instregex "LDRi12", "PICLDR$")>;
def : InstRW<[A9WriteLsi], (instregex "LDRrs")>;
def : InstRW<[A9WriteLb],
      (instregex "LDRBi12", "PICLDRH", "PICLDRB", "PICLDRSH", "PICLDRSB",
      "LDRH", "LDRSH", "LDRSB")>;
def : InstRW<[A9WriteLbsi], (instregex "LDRrs")>;

def : WriteRes<WriteDIV, []> { let Latency = 0; }

def : WriteRes<WriteBr, [A9UnitB]>;
def : WriteRes<WriteBrL, [A9UnitB]>;
def : WriteRes<WriteBrTbl, [A9UnitB]>;
def : WriteRes<WritePreLd, []>;
def : WriteRes<WriteNoop, []> { let Latency = 0; let NumMicroOps = 0; }
} // SchedModel = CortexA9Model
