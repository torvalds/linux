//===-- PPCScheduleG3.td - PPC G3 Scheduling Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the G3 (750) processor.
//
//===----------------------------------------------------------------------===//

def G3_BPU    : FuncUnit; // Branch unit
def G3_SLU    : FuncUnit; // Store/load unit
def G3_SRU    : FuncUnit; // special register unit
def G3_IU1    : FuncUnit; // integer unit 1 (simple)
def G3_IU2    : FuncUnit; // integer unit 2 (complex)
def G3_FPU1   : FuncUnit; // floating point unit 1

def G3Itineraries : ProcessorItineraries<
  [G3_IU1, G3_IU2, G3_FPU1, G3_BPU, G3_SRU, G3_SLU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<1, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<1, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<1, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_IntDivW     , [InstrStage<19, [G3_IU1]>]>,
  InstrItinData<IIC_IntMFFS     , [InstrStage<1, [G3_FPU1]>]>,
  InstrItinData<IIC_IntMTFSB0   , [InstrStage<3, [G3_FPU1]>]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<5, [G3_IU1]>]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<6, [G3_IU1]>]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<3, [G3_IU1]>]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<1, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_IntShift    , [InstrStage<1, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<2, [G3_IU1, G3_IU2]>]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [G3_BPU]>]>,
  InstrItinData<IIC_BrCR        , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_BrMCR       , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_LdStDCBA    , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStDCBF    , [InstrStage<3, [G3_SLU]>]>,
  InstrItinData<IIC_LdStDCBI    , [InstrStage<3, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStStore   , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStSTUX    , [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStICBI    , [InstrStage<3, [G3_SLU]>]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<2, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<2, [G3_SLU]>]>,  
  InstrItinData<IIC_LdStLMW     , [InstrStage<34, [G3_SLU]>]>,
  InstrItinData<IIC_LdStLWARX   , [InstrStage<3, [G3_SLU]>]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<8, [G3_SLU]>]>,
  InstrItinData<IIC_LdStSync    , [InstrStage<3, [G3_SLU]>]>,
  InstrItinData<IIC_SprISYNC    , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_SprMFSR     , [InstrStage<3, [G3_SRU]>]>,
  InstrItinData<IIC_SprMTMSR    , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_SprMTSR     , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_SprTLBSYNC  , [InstrStage<3, [G3_SRU]>]>,
  InstrItinData<IIC_SprMFCR     , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_SprMFMSR    , [InstrStage<1, [G3_SRU]>]>,
  InstrItinData<IIC_SprMFSPR    , [InstrStage<3, [G3_SRU]>]>,
  InstrItinData<IIC_SprMFTB     , [InstrStage<3, [G3_SRU]>]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_SprMTSRIN   , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_SprRFI      , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_SprSC       , [InstrStage<2, [G3_SRU]>]>,
  InstrItinData<IIC_FPGeneral   , [InstrStage<1, [G3_FPU1]>]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<1, [G3_FPU1]>]>,
  InstrItinData<IIC_FPCompare   , [InstrStage<1, [G3_FPU1]>]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<31, [G3_FPU1]>]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<17, [G3_FPU1]>]>,
  InstrItinData<IIC_FPFused     , [InstrStage<2, [G3_FPU1]>]>,
  InstrItinData<IIC_FPRes       , [InstrStage<10, [G3_FPU1]>]>
]>;
