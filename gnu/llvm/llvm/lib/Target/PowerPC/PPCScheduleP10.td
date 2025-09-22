//===--- PPCScheduleP10.td - P10 Scheduling Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//
// This file defines the resources required by P10 instructions.
//===----------------------------------------------------------------------===//
// Modeling pipeline forwarding logic.
def P10BR_Read : SchedRead;
def P10DF_Read : SchedRead;
def P10DV_Read : SchedRead;
def P10DX_Read : SchedRead;
def P10F2_Read : SchedRead;
def P10FX_Read : SchedRead;
def P10LD_Read : SchedRead;
def P10MU_Read : SchedRead;
def P10PM_Read : SchedRead;
def P10ST_Read : SchedRead;
def P10SX_Read : SchedRead;
def P10vMU_Read : SchedRead;

def P10Model : SchedMachineModel {
  let IssueWidth = 8;
  let MicroOpBufferSize = 44;
  let LoopMicroOpBufferSize = 60;
  let CompleteModel = 1;

  // Power 10 does not support instructions from SPE, Book E and HTM.
  let UnsupportedFeatures = [HasSPE, IsE500, IsBookE, IsISAFuture, HasHTM];
}

let SchedModel = P10Model in {

  // ***************** Processor Resources *****************

  // Pipeline Groups

  def P10_BF : ProcResource<4>; // Four Binary Floating Point pipelines.
  def P10_BR : ProcResource<2>; // Two Branch pipelines.
  def P10_CY : ProcResource<4>; // Four Crypto pipelines.
  def P10_DF : ProcResource<1>; // One Decimal Floating Point pipelines.
  def P10_DV : ProcResource<2>; // Two Fixed-point divide (DIV) pipelines.
  def P10_DX : ProcResource<2>; // Two 128-bit fixed-point and BCD pipelines.
  def P10_FX : ProcResource<4>; // Four ALU pipelines.
  def P10_LD : ProcResource<2>; // Two Load pipelines.
  def P10_MM : ProcResource<2>; // Two 512-bit SIMD matrix multiply engine pipelines.
  def P10_PM : ProcResource<4>; // Four 128-bit permute (PM) pipelines.
  def P10_ST : ProcResource<2>; // Two ST-D pipelines.
  def P10_SX : ProcResource<2>; // Two Simple Fixed-point (SFX) pipelines.

  // Dispatch Groups

  // Dispatch to any slots
  def P10_ANY_SLOT : ProcResource<8>;

  let Super = P10_ANY_SLOT in {

    // Dispatch to even slots
    def P10_EVEN_SLOT : ProcResource<4>;

    // Dispatch to odd slots
    def P10_ODD_SLOT : ProcResource<4>;
  }

  // Dispatch Rules
  let NumMicroOps = 0, Latency = 1 in {
    // Dispatch Rule '-'
    def P10W_DISP_ANY : SchedWriteRes<[P10_ANY_SLOT]>;

    // Dispatch Rule '-', even slot
    def P10W_DISP_EVEN : SchedWriteRes<[P10_EVEN_SLOT]>;

    // Dispatch Rule 'P'
    def P10W_DISP_PAIR : SchedWriteRes<[P10_EVEN_SLOT, P10_ODD_SLOT]>;
  }

  // ***************** SchedWriteRes Definitions *****************

  // A BF pipeline may take from 7 to 36 cycles to complete.
  // Some BF operations may keep the pipeline busy for up to 10 cycles.
  def P10W_BF_7C : SchedWriteRes<[P10_BF]> {
    let Latency = 7;
  }

  def P10W_BF_22C : SchedWriteRes<[P10_BF]> {
    let ReleaseAtCycles = [ 5 ];
    let Latency = 22;
  }

  def P10W_BF_24C : SchedWriteRes<[P10_BF]> {
    let ReleaseAtCycles = [ 8 ];
    let Latency = 24;
  }

  def P10W_BF_26C : SchedWriteRes<[P10_BF]> {
    let ReleaseAtCycles = [ 5 ];
    let Latency = 26;
  }

  def P10W_BF_27C : SchedWriteRes<[P10_BF]> {
    let ReleaseAtCycles = [ 7 ];
    let Latency = 27;
  }

  def P10W_BF_36C : SchedWriteRes<[P10_BF]> {
    let ReleaseAtCycles = [ 10 ];
    let Latency = 36;
  }

  // A BR pipeline may take 2 cycles to complete.
  def P10W_BR_2C : SchedWriteRes<[P10_BR]> {
    let Latency = 2;
  }

  // A CY pipeline may take 7 cycles to complete.
  def P10W_CY_7C : SchedWriteRes<[P10_CY]> {
    let Latency = 7;
  }

  // A DF pipeline may take from 13 to 174 cycles to complete.
  // Some DF operations may keep the pipeline busy for up to 67 cycles.
  def P10W_DF_13C : SchedWriteRes<[P10_DF]> {
    let Latency = 13;
  }

  def P10W_DF_24C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 16 ];
    let Latency = 24;
  }

  def P10W_DF_25C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 17 ];
    let Latency = 25;
  }

  def P10W_DF_26C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 18 ];
    let Latency = 26;
  }

  def P10W_DF_32C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 22 ];
    let Latency = 32;
  }

  def P10W_DF_33C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 25 ];
    let Latency = 33;
  }

  def P10W_DF_34C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 25 ];
    let Latency = 34;
  }

  def P10W_DF_38C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 30 ];
    let Latency = 38;
  }

  def P10W_DF_40C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 17 ];
    let Latency = 40;
  }

  def P10W_DF_43C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 34 ];
    let Latency = 43;
  }

  def P10W_DF_59C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 49 ];
    let Latency = 59;
  }

  def P10W_DF_61C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 12 ];
    let Latency = 61;
  }

  def P10W_DF_68C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 15 ];
    let Latency = 68;
  }

  def P10W_DF_77C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 67 ];
    let Latency = 77;
  }

  def P10W_DF_87C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 12 ];
    let Latency = 87;
  }

  def P10W_DF_100C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 32 ];
    let Latency = 100;
  }

  def P10W_DF_174C : SchedWriteRes<[P10_DF]> {
    let ReleaseAtCycles = [ 33 ];
    let Latency = 174;
  }

  // A DV pipeline may take from 20 to 83 cycles to complete.
  // Some DV operations may keep the pipeline busy for up to 33 cycles.
  def P10W_DV_20C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 10 ];
    let Latency = 20;
  }

  def P10W_DV_25C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 10 ];
    let Latency = 25;
  }

  def P10W_DV_27C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 10 ];
    let Latency = 27;
  }

  def P10W_DV_41C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 10 ];
    let Latency = 41;
  }

  def P10W_DV_43C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 21 ];
    let Latency = 43;
  }

  def P10W_DV_47C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 21 ];
    let Latency = 47;
  }

  def P10W_DV_54C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 33 ];
    let Latency = 54;
  }

  def P10W_DV_60C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 33 ];
    let Latency = 60;
  }

  def P10W_DV_75C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 21 ];
    let Latency = 75;
  }

  def P10W_DV_83C : SchedWriteRes<[P10_DV]> {
    let ReleaseAtCycles = [ 33 ];
    let Latency = 83;
  }

  // A DX pipeline may take 5 cycles to complete.
  def P10W_DX_5C : SchedWriteRes<[P10_DX]> {
    let Latency = 5;
  }

  // A F2 pipeline may take 4 cycles to complete.
  def P10W_F2_4C : SchedWriteRes<[P10_FX]> {
    let Latency = 4;
  }

  // A FX pipeline may take from 2 to 3 cycles to complete.
  def P10W_FX_2C : SchedWriteRes<[P10_FX]> {
    let Latency = 2;
  }

  def P10W_FX_3C : SchedWriteRes<[P10_FX]> {
    let Latency = 3;
  }

  // A LD pipeline may take 6 cycles to complete.
  def P10W_LD_6C : SchedWriteRes<[P10_LD]> {
    let Latency = 6;
  }

  // A MF pipeline may take 13 cycles to complete.
  def P10W_MF_13C : SchedWriteRes<[P10_SX]> {
    let Latency = 13;
  }

  // A MFL pipeline may take 13 cycles to complete.
  def P10W_MFL_13C : SchedWriteRes<[P10_SX]> {
    let Latency = 13;
  }

  // A MM pipeline may take 10 cycles to complete.
  def P10W_MM_10C : SchedWriteRes<[P10_MM]> {
    let Latency = 10;
  }

  // A MU pipeline may take 5 cycles to complete.
  def P10W_MU_5C : SchedWriteRes<[P10_BF]> {
    let Latency = 5;
  }

  // A PM pipeline may take 4 cycles to complete.
  def P10W_PM_4C : SchedWriteRes<[P10_PM]> {
    let Latency = 4;
  }

  // A ST pipeline may take 3 cycles to complete.
  def P10W_ST_3C : SchedWriteRes<[P10_ST]> {
    let Latency = 3;
  }

  // A SX pipeline may take from 0 to 3 cycles to complete.
  def P10W_SX : SchedWriteRes<[P10_SX]> {
    let Latency = 0;
  }

  def P10W_SX_3C : SchedWriteRes<[P10_SX]> {
    let Latency = 3;
  }

  // A vMU pipeline may take 7 cycles to complete.
  def P10W_vMU_7C : SchedWriteRes<[P10_BF]> {
    let Latency = 7;
  }

  // ***************** Read Advance Definitions *****************

  // Modeling pipeline forwarding logic.
  def P10BF_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_CY_7C, P10W_DF_13C, P10W_MM_10C]>;
  def P10BF_Read_2C : SchedReadAdvance<2, [P10W_BF_7C]>;
  def P10BR_Read_1C : SchedReadAdvance<1, [P10W_FX_3C, P10W_F2_4C]>;
  def P10CY_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_DF_13C, P10W_MM_10C]>;
  def P10CY_Read_3C : SchedReadAdvance<3, [P10W_CY_7C]>;
  def P10DF_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_CY_7C, P10W_DF_13C, P10W_MM_10C]>;
  def P10DV_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_CY_7C, P10W_DF_13C, P10W_MM_10C]>;
  def P10DX_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_CY_7C, P10W_DF_13C, P10W_MM_10C]>;
  def P10F2_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C, P10W_PM_4C]>;
  def P10FX_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C, P10W_PM_4C]>;
  def P10LD_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C]>;
  def P10MM_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_CY_7C, P10W_DF_13C]>;
  def P10MM_Read_6C : SchedReadAdvance<6, [P10W_MM_10C]>;
  def P10MU_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_DF_13C]>;
  def P10PM_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C, P10W_PM_4C]>;
  def P10ST_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C, P10W_PM_4C]>;
  def P10SX_Read_1C : SchedReadAdvance<1, [P10W_ST_3C, P10W_SX_3C, P10W_FX_3C, P10W_F2_4C, P10W_PM_4C, P10W_MM_10C]>;
  def P10vMU_Read_1C : SchedReadAdvance<1, [P10W_DX_5C, P10W_MU_5C, P10W_vMU_7C, P10W_BF_7C, P10W_CY_7C, P10W_DF_13C, P10W_MM_10C]>;

  // Save 1 cycles if pipeline BF reads the data from pipelines DX, MU, vMU, CY, DF, MM.
  // Save 2 cycles if pipeline BF reads the data from pipelines BF.
  def P10BF_Read : SchedReadVariant<[
        SchedVar<P10W_BF_7C_Pred, [P10BF_Read_2C]>,
        SchedVar<NoSchedPred,     [P10BF_Read_1C]>
  ]>;

  // Save 1 cycles if pipeline CY reads the data from pipelines DX, MU, vMU, BF, DF, MM.
  // Save 3 cycles if pipeline CY reads the data from pipelines CY.
  def P10CY_Read : SchedReadVariant<[
        SchedVar<P10W_CY_7C_Pred, [P10CY_Read_3C]>,
        SchedVar<NoSchedPred,     [P10CY_Read_1C]>
  ]>;

  // Save 1 cycles if pipeline MM reads the data from pipelines DX, MU, vMU, BF, CY, DF.
  // Save 6 cycles if pipeline MM reads the data from pipelines MM.
  def P10MM_Read : SchedReadVariant<[
        SchedVar<P10W_MM_10C_Pred, [P10MM_Read_6C]>,
        SchedVar<NoSchedPred,     [P10MM_Read_1C]>
  ]>;

  // Save 1 cycles if pipeline BR reads the data from pipelines FX, F2.
  def : SchedAlias<P10BR_Read, P10BR_Read_1C>;

  // Save 1 cycles if pipeline DF reads the data from pipelines DX, MU, vMU, BF, CY, DF, MM.
  def : SchedAlias<P10DF_Read, P10DF_Read_1C>;

  // Save 1 cycles if pipeline DV reads the data from pipelines DX, MU, vMU, BF, CY, DF, MM.
  def : SchedAlias<P10DV_Read, P10DV_Read_1C>;

  // Save 1 cycles if pipeline DX reads the data from pipelines DX, MU, vMU, BF, CY, DF, MM.
  def : SchedAlias<P10DX_Read, P10DX_Read_1C>;

  // Save 1 cycles if pipeline F2 reads the data from pipelines ST, SX, FX, F2, PM.
  def : SchedAlias<P10F2_Read, P10F2_Read_1C>;

  // Save 1 cycles if pipeline FX reads the data from pipelines ST, SX, FX, F2, PM.
  def : SchedAlias<P10FX_Read, P10FX_Read_1C>;

  // Save 1 cycles if pipeline LD reads the data from pipelines ST, SX, FX, F2.
  def : SchedAlias<P10LD_Read, P10LD_Read_1C>;

  // Save 1 cycles if pipeline MU reads the data from pipelines DX, MU, DF.
  def : SchedAlias<P10MU_Read, P10MU_Read_1C>;

  // Save 1 cycles if pipeline PM reads the data from pipelines ST, SX, FX, F2, PM.
  def : SchedAlias<P10PM_Read, P10PM_Read_1C>;

  // Save 1 cycles if pipeline ST reads the data from pipelines ST, SX, FX, F2, PM.
  def : SchedAlias<P10ST_Read, P10ST_Read_1C>;

  // Save 1 cycles if pipeline SX reads the data from pipelines ST, SX, FX, F2, PM, MM.
  def : SchedAlias<P10SX_Read, P10SX_Read_1C>;

  // Save 1 cycles if pipeline vMU reads the data from pipelines DX, MU, vMU, BF, CY, DF, MM.
  def : SchedAlias<P10vMU_Read, P10vMU_Read_1C>;

  include "P10InstrResources.td"
}
