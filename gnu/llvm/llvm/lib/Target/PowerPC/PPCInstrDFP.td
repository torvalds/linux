//===-- PPCInstrDFP.td - PowerPC Decimal Floating Point ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the PowerPC Decimal Floating Point (DFP) instructions.
//
//===----------------------------------------------------------------------===//

// We provide no scheduling info for the DFP instructions.
// While they are not pseudo instructions we don't intend on scheduling them.
let hasNoSchedulingInfo = 1 in {
let mayRaiseFPException = 1, hasSideEffects = 0 in {

let isCommutable = 1 in {
defm DADD : XForm_28r<59, 2, (outs f8rc:$RST), (ins f8rc:$RA, f8rc:$RB),
                      "dadd",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

defm DADDQ : XForm_28r<63, 2, (outs fpairrc:$RST), (ins fpairrc:$RA, fpairrc:$RB),
                       "daddq",  "$RST, $RA, $RB", IIC_FPGeneral, []>;
}

defm DSUB : XForm_28r<59, 514, (outs f8rc:$RST), (ins f8rc:$RA, f8rc:$RB),
                      "dsub",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

defm DSUBQ : XForm_28r<63, 514, (outs fpairrc:$RST), (ins fpairrc:$RA, fpairrc:$RB),
                       "dsubq",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

let isCommutable = 1 in {
defm DMUL : XForm_28r<59, 34, (outs f8rc:$RST), (ins f8rc:$RA, f8rc:$RB),
                      "dmul",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

defm DMULQ : XForm_28r<63, 34, (outs fpairrc:$RST), (ins fpairrc:$RA, fpairrc:$RB),
                       "dmulq",  "$RST, $RA, $RB", IIC_FPGeneral, []>;
}

defm DDIV : XForm_28r<59, 546, (outs f8rc:$RST), (ins f8rc:$RA, f8rc:$RB),
                      "ddiv",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

defm DDIVQ : XForm_28r<63, 546, (outs fpairrc:$RST), (ins fpairrc:$RA, fpairrc:$RB),
                       "ddivq",  "$RST, $RA, $RB", IIC_FPGeneral, []>;

let isCompare = 1 in {
  def DCMPU : XForm_17<59, 642, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                       "dcmpu $BF, $RA, $RB", IIC_FPCompare>;

  def DCMPUQ : XForm_17<63, 642, (outs crrc:$BF), (ins fpairrc:$RA, fpairrc:$RB),
                        "dcmpuq $BF, $RA, $RB", IIC_FPCompare>;

  def DCMPO : XForm_17<59, 130, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                       "dcmpo $BF, $RA, $RB", IIC_FPCompare>;

  def DCMPOQ : XForm_17<63, 130, (outs crrc:$BF), (ins fpairrc:$RA, fpairrc:$RB),
                        "dcmpoq $BF, $RA, $RB", IIC_FPCompare>;
}

// 5.6.4 DFP Quantum Adjustment Instructions
defm DQUAI: Z23Form_TE5_FRTB5_RMC2r<59, 67, (outs f8rc:$FRT),
                                    (ins s5imm:$TE, f8rc:$FRB, u2imm:$RMC),
                                    "dquai", "$TE, $FRT, $FRB, $RMC", []>;
defm DQUAIQ: Z23Form_TE5_FRTB5_RMC2r<63, 67, (outs fpairrc:$FRT),
                                     (ins s5imm:$TE, fpairrc:$FRB, u2imm:$RMC),
                                     "dquaiq", "$TE, $FRT, $FRB, $RMC", []>;
defm DQUA: Z23Form_FRTAB5_RMC2r<59, 3, (outs f8rc:$FRT),
                                (ins f8rc:$FRA, f8rc:$FRB, u2imm:$RMC),
                                "dqua", "$FRT, $FRA, $FRB, $RMC", []>;
defm DQUAQ: Z23Form_FRTAB5_RMC2r<63, 3, (outs fpairrc:$FRT),
                                 (ins fpairrc:$FRA, fpairrc:$FRB, u2imm:$RMC),
                                 "dquaq", "$FRT, $FRA, $FRB, $RMC", []>;
defm DRRND: Z23Form_FRTAB5_RMC2r<59, 35, (outs f8rc:$FRT),
                                (ins f8rc:$FRA, f8rc:$FRB, u2imm:$RMC),
                                "drrnd", "$FRT, $FRA, $FRB, $RMC", []>;
defm DRRNDQ: Z23Form_FRTAB5_RMC2r<63, 35, (outs fpairrc:$FRT),
                                 (ins f8rc:$FRA, fpairrc:$FRB, u2imm:$RMC),
                                 "drrndq", "$FRT, $FRA, $FRB, $RMC", []>;
defm DRINTX: Z23Form_FRTB5_R1_RMC2r<59, 99, (outs f8rc:$FRT),
                                    (ins u1imm:$R, f8rc:$FRB, u2imm:$RMC),
                                    "drintx", "$R, $FRT, $FRB, $RMC", []>;
defm DRINTXQ: Z23Form_FRTB5_R1_RMC2r<63, 99, (outs fpairrc:$FRT),
                                     (ins u1imm:$R, fpairrc:$FRB, u2imm:$RMC),
                                     "drintxq", "$R, $FRT, $FRB, $RMC", []>;
defm DRINTN: Z23Form_FRTB5_R1_RMC2r<59, 227, (outs f8rc:$FRT),
                                    (ins u1imm:$R, f8rc:$FRB, u2imm:$RMC),
                                    "drintn", "$R, $FRT, $FRB, $RMC", []>;
defm DRINTNQ: Z23Form_FRTB5_R1_RMC2r<63, 227, (outs fpairrc:$FRT),
                                     (ins u1imm:$R, fpairrc:$FRB, u2imm:$RMC),
                                     "drintnq", "$R, $FRT, $FRB, $RMC", []>;

// 5.6.5 DFP Conversion Instructions
defm DCTDP: XForm_26r<59, 258, (outs f8rc:$RST), (ins f8rc:$RB),
                      "dctdp", "$RST, $RB", IIC_FPGeneral, []>;
defm DCTQPQ: XForm_26r<63, 258, (outs fpairrc:$RST), (ins f8rc:$RB),
                       "dctqpq", "$RST, $RB", IIC_FPGeneral, []>;
defm DRSP: XForm_26r<59, 770, (outs f8rc:$RST), (ins f8rc:$RB),
                     "drsp", "$RST, $RB", IIC_FPGeneral, []>;
defm DRDPQ: XForm_26r<63, 770, (outs fpairrc:$RST), (ins fpairrc:$RB),
                      "drdpq", "$RST, $RB", IIC_FPGeneral, []>;
defm DCFFIX: XForm_26r<59, 802, (outs f8rc:$RST), (ins f8rc:$RB),
                       "dcffix", "$RST, $RB", IIC_FPGeneral, []>;
defm DCFFIXQ: XForm_26r<63, 802, (outs fpairrc:$RST), (ins f8rc:$RB),
                        "dcffixq", "$RST, $RB", IIC_FPGeneral, []>;
defm DCTFIX: XForm_26r<59, 290, (outs f8rc:$RST), (ins f8rc:$RB),
                       "dctfix", "$RST, $RB", IIC_FPGeneral, []>;
defm DCTFIXQ: XForm_26r<63, 290, (outs f8rc:$RST), (ins fpairrc:$RB),
                        "dctfixq", "$RST, $RB", IIC_FPGeneral, []>;
let Predicates = [HasP10Vector] in {
  def DCFFIXQQ: XForm_26<63, 994, (outs fpairrc:$RST), (ins vrrc:$RB),
                         "dcffixqq $RST, $RB", IIC_FPGeneral, []>;
let RA = 1 in
  def DCTFIXQQ: XForm_base_r3xo<63, 994, (outs vrrc:$RST), (ins fpairrc:$RB),
                                "dctfixqq $RST, $RB", IIC_FPGeneral, []>;
} // HasP10Vector

// 5.6.6 DFP Format Instructions
defm DENBCD: XForm_S1_FRTB5r<59, 834, (outs f8rc:$FRT),
                              (ins u1imm:$S, f8rc:$FRB),
                              "denbcd", "$S, $FRT, $FRB", []>;
defm DENBCDQ: XForm_S1_FRTB5r<63, 834, (outs fpairrc:$FRT),
                               (ins u1imm:$S, fpairrc:$FRB),
                               "denbcdq", "$S, $FRT, $FRB", []>;
} // mayRaiseFPException

// 5.6.6 DFP none exception raising format instructions.
defm DDEDPD: XForm_SP2_FRTB5r<59, 322, (outs f8rc:$FRT),
                              (ins u2imm:$SP, f8rc:$FRB),
                              "ddedpd", "$SP, $FRT, $FRB", []>;
defm DDEDPDQ: XForm_SP2_FRTB5r<63, 322, (outs fpairrc:$FRT),
                               (ins u2imm:$SP, fpairrc:$FRB),
                               "ddedpdq", "$SP, $FRT, $FRB", []>;
defm DXEX: XForm_26r<59, 354, (outs f8rc:$RST), (ins f8rc:$RB),
                     "dxex", "$RST, $RB", NoItinerary, []>;
defm DXEXQ: XForm_26r<63, 354, (outs f8rc:$RST), (ins fpairrc:$RB),
                      "dxexq", "$RST, $RB", NoItinerary, []>;
defm DIEX: XForm_base_r3xo_r<59, 866, (outs f8rc:$RST),
                             (ins f8rc:$RA, f8rc:$RB),
                             "diex", "$RST, $RA, $RB", []>;
defm DIEXQ: XForm_base_r3xo_r<63, 866, (outs fpairrc:$RST),
                              (ins f8rc:$RA, fpairrc:$RB),
                              "diexq", "$RST, $RA, $RB", []>;
defm DSCLI: Z22Form_FRTA5_SH6r<59, 66, (outs f8rc:$FRT),
                               (ins f8rc:$FRA, u6imm:$SH),
                               "dscli", "$FRT, $FRA, $SH", []>;
defm DSCLIQ: Z22Form_FRTA5_SH6r<63, 66, (outs fpairrc:$FRT),
                                (ins fpairrc:$FRA, u6imm:$SH),
                                "dscliq", "$FRT, $FRA, $SH", []>;
defm DSCRI: Z22Form_FRTA5_SH6r<59, 98, (outs f8rc:$FRT),
                               (ins f8rc:$FRA, u6imm:$SH),
                               "dscri", "$FRT, $FRA, $SH", []>;
defm DSCRIQ: Z22Form_FRTA5_SH6r<63, 98, (outs fpairrc:$FRT),
                                (ins fpairrc:$FRA, u6imm:$SH),
                                "dscriq", "$FRT, $FRA, $SH", []>;

// 5.6.3 DFP Test Instructions
def DTSTDC : Z22Form_BF3_FRA5_DCM6<59, 194, (outs crrc:$BF),
                                   (ins f8rc:$FRA, u6imm:$DCM),
                                   "dtstdc $BF, $FRA, $DCM", IIC_FPCompare, []>;

def DTSTDCQ : Z22Form_BF3_FRA5_DCM6<63, 194, (outs crrc:$BF),
                                    (ins fpairrc:$FRA, u6imm:$DCM),
                                    "dtstdcq $BF, $FRA, $DCM", IIC_FPCompare, []>;

def DTSTDG : Z22Form_BF3_FRA5_DCM6<59, 226, (outs crrc:$BF),
                                   (ins f8rc:$FRA, u6imm:$DCM),
                                   "dtstdg $BF, $FRA, $DCM", IIC_FPCompare, []>;

def DTSTDGQ : Z22Form_BF3_FRA5_DCM6<63, 226, (outs crrc:$BF),
                                    (ins fpairrc:$FRA, u6imm:$DCM),
                                    "dtstdgq $BF, $FRA, $DCM", IIC_FPCompare, []>;

def DTSTEX : XForm_17<59, 162, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                      "dtstex $BF, $RA, $RB", IIC_FPCompare>;

def DTSTEXQ : XForm_17<63, 162, (outs crrc:$BF), (ins fpairrc:$RA, fpairrc:$RB),
                       "dtstexq $BF, $RA, $RB", IIC_FPCompare>;

def DTSTSF : XForm_17<59, 674, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                      "dtstsf $BF, $RA, $RB", IIC_FPCompare>;

def DTSTSFQ : XForm_17<63, 674, (outs crrc:$BF), (ins f8rc:$RA, fpairrc:$RB),
                       "dtstsfq $BF, $RA, $RB", IIC_FPCompare>;

def DTSTSFI : XForm_BF3_UIM6_FRB5<59, 675, (outs crrc:$BF),
                                  (ins u6imm:$UIM, f8rc:$FRB),
                                  "dtstsfi $BF, $UIM, $FRB", IIC_FPCompare, []>;

def DTSTSFIQ : XForm_BF3_UIM6_FRB5<63, 675, (outs crrc:$BF),
                                   (ins u6imm:$UIM, fpairrc:$FRB),
                                   "dtstsfiq $BF, $UIM, $FRB", IIC_FPCompare, []>;

} // hasNoSchedulingInfo
