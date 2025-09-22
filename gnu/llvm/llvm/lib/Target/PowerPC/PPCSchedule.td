//===-- PPCSchedule.td - PowerPC Scheduling Definitions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction Itinerary classes used for PowerPC
//
def IIC_IntSimple    : InstrItinClass;
def IIC_IntGeneral   : InstrItinClass;
def IIC_IntCompare   : InstrItinClass;
def IIC_IntISEL      : InstrItinClass;
def IIC_IntDivD      : InstrItinClass;
def IIC_IntDivW      : InstrItinClass;
def IIC_IntMFFS      : InstrItinClass;
def IIC_IntMFVSCR    : InstrItinClass;
def IIC_IntMTFSB0    : InstrItinClass;
def IIC_IntMTSRD     : InstrItinClass;
def IIC_IntMulHD     : InstrItinClass;
def IIC_IntMulHW     : InstrItinClass;
def IIC_IntMulHWU    : InstrItinClass;
def IIC_IntMulLI     : InstrItinClass;
def IIC_IntRFID      : InstrItinClass;
def IIC_IntRotateD   : InstrItinClass;
def IIC_IntRotateDI  : InstrItinClass;
def IIC_IntRotate    : InstrItinClass;
def IIC_IntShift     : InstrItinClass;
def IIC_IntTrapD     : InstrItinClass;
def IIC_IntTrapW     : InstrItinClass;
def IIC_BrB          : InstrItinClass;
def IIC_BrCR         : InstrItinClass;
def IIC_BrMCR        : InstrItinClass;
def IIC_BrMCRX       : InstrItinClass;
def IIC_LdStDCBA     : InstrItinClass;
def IIC_LdStDCBF     : InstrItinClass;
def IIC_LdStDCBI     : InstrItinClass;
def IIC_LdStLoad     : InstrItinClass;
def IIC_LdStLoadUpd  : InstrItinClass;
def IIC_LdStLoadUpdX : InstrItinClass;
def IIC_LdStStore    : InstrItinClass;
def IIC_LdStDSS      : InstrItinClass;
def IIC_LdStICBI     : InstrItinClass;
def IIC_LdStLD       : InstrItinClass;
def IIC_LdStLDU      : InstrItinClass;
def IIC_LdStLDUX     : InstrItinClass;
def IIC_LdStLDARX    : InstrItinClass;
def IIC_LdStLFD      : InstrItinClass;
def IIC_LdStLFDU     : InstrItinClass;
def IIC_LdStLFDUX    : InstrItinClass;
def IIC_LdStLHA      : InstrItinClass;
def IIC_LdStLHAU     : InstrItinClass;
def IIC_LdStLHAUX    : InstrItinClass;
def IIC_LdStLMW      : InstrItinClass;
def IIC_LdStLQ       : InstrItinClass;
def IIC_LdStLQARX    : InstrItinClass;
def IIC_LdStLVecX    : InstrItinClass;
def IIC_LdStLWA      : InstrItinClass;
def IIC_LdStLWARX    : InstrItinClass;
def IIC_LdStSLBIA    : InstrItinClass;
def IIC_LdStSLBIE    : InstrItinClass;
def IIC_LdStSTD      : InstrItinClass;
def IIC_LdStSTDCX    : InstrItinClass;
def IIC_LdStSTQ      : InstrItinClass;
def IIC_LdStSTQCX    : InstrItinClass;
def IIC_LdStSTU      : InstrItinClass;
def IIC_LdStSTUX     : InstrItinClass;
def IIC_LdStSTFD     : InstrItinClass;
def IIC_LdStSTFDU    : InstrItinClass;
def IIC_LdStSTVEBX   : InstrItinClass;
def IIC_LdStSTWCX    : InstrItinClass;
def IIC_LdStSync     : InstrItinClass;
def IIC_LdStCOPY     : InstrItinClass;
def IIC_LdStPASTE    : InstrItinClass;
def IIC_SprISYNC     : InstrItinClass;
def IIC_SprMFSR      : InstrItinClass;
def IIC_SprMTMSR     : InstrItinClass;
def IIC_SprMTSR      : InstrItinClass;
def IIC_SprTLBSYNC   : InstrItinClass;
def IIC_SprMFCR      : InstrItinClass;
def IIC_SprMFCRF     : InstrItinClass;
def IIC_SprMFMSR     : InstrItinClass;
def IIC_SprMFSPR     : InstrItinClass;
def IIC_SprMFTB      : InstrItinClass;
def IIC_SprMTSPR     : InstrItinClass;
def IIC_SprMTSRIN    : InstrItinClass;
def IIC_SprRFI       : InstrItinClass;
def IIC_SprSC        : InstrItinClass;
def IIC_FPGeneral    : InstrItinClass;
def IIC_FPDGeneral   : InstrItinClass;
def IIC_FPSGeneral   : InstrItinClass;
def IIC_FPAddSub     : InstrItinClass;
def IIC_FPCompare    : InstrItinClass;
def IIC_FPDivD       : InstrItinClass;
def IIC_FPDivS       : InstrItinClass;
def IIC_FPFused      : InstrItinClass;
def IIC_FPRes        : InstrItinClass;
def IIC_FPSqrtD      : InstrItinClass;
def IIC_FPSqrtS      : InstrItinClass;
def IIC_VecGeneral   : InstrItinClass;
def IIC_VecFP        : InstrItinClass;
def IIC_VecFPCompare : InstrItinClass;
def IIC_VecComplex   : InstrItinClass;
def IIC_VecPerm      : InstrItinClass;
def IIC_VecFPRound   : InstrItinClass;
def IIC_VecVSL       : InstrItinClass;
def IIC_VecVSR       : InstrItinClass;
def IIC_SprMTMSRD    : InstrItinClass;
def IIC_SprSLIE      : InstrItinClass;
def IIC_SprSLBFEE    : InstrItinClass;
def IIC_SprSLBIE     : InstrItinClass;
def IIC_SprSLBIEG    : InstrItinClass;
def IIC_SprSLBMTE    : InstrItinClass;
def IIC_SprSLBMFEE   : InstrItinClass;
def IIC_SprSLBMFEV   : InstrItinClass;
def IIC_SprSLBIA     : InstrItinClass;
def IIC_SprSLBSYNC   : InstrItinClass;
def IIC_SprTLBIA     : InstrItinClass;
def IIC_SprTLBIEL    : InstrItinClass;
def IIC_SprTLBIE     : InstrItinClass;
def IIC_SprABORT     : InstrItinClass;
def IIC_SprMSGSYNC   : InstrItinClass;
def IIC_SprSTOP      : InstrItinClass;
def IIC_SprMFPMR     : InstrItinClass;
def IIC_SprMTPMR     : InstrItinClass;

//===----------------------------------------------------------------------===//
// Processor instruction itineraries.
include "PPCInstrInfo.td"

include "PPCSchedPredicates.td"
include "PPCScheduleG3.td"
include "PPCSchedule440.td"
include "PPCScheduleG4.td"
include "PPCScheduleG4Plus.td"
include "PPCScheduleG5.td"
include "PPCScheduleP7.td"
include "PPCScheduleP8.td"
include "PPCScheduleP9.td"
include "PPCScheduleP10.td"
include "PPCScheduleA2.td"
include "PPCScheduleE500.td"
include "PPCScheduleE500mc.td"
include "PPCScheduleE5500.td"
