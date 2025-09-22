//===- AArch64SchedPredNeoverse.td - AArch64 Sched Preds -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines scheduling predicate definitions that are used by the
// AArch64 Neoverse processors.
//
//===----------------------------------------------------------------------===//

// Auxiliary predicates.

// Check for LSL shift == 0
def NeoverseNoLSL : MCSchedPredicate<
                      CheckAll<[CheckShiftLSL,
                                CheckShiftBy0]>>;

// Identify LDR/STR H/Q-form scaled (and potentially extended) FP instructions
def NeoverseHQForm : MCSchedPredicate<
                       CheckAll<[
                         CheckAny<[CheckHForm, CheckQForm]>,
                         CheckImmOperand<4, 1>]>>;

// Check if <Pd> == <Pg>
def NeoversePdIsPgFn : TIIPredicate<
                         "isNeoversePdSameAsPg",
                         MCOpcodeSwitchStatement<
                           [MCOpcodeSwitchCase<[BRKA_PPmP, BRKB_PPmP],
                             MCReturnStatement<CheckSameRegOperand<1, 2>>>],
                           MCReturnStatement<CheckSameRegOperand<0, 1>>>>;
def NeoversePdIsPg : MCSchedPredicate<NeoversePdIsPgFn>;

// Check if SVE INC/DEC (scalar), ALL, {1, 2, 4}
def NeoverseCheapIncDec : MCSchedPredicate<
                            CheckAll<[CheckOpcode<[
                                        INCB_XPiI, INCH_XPiI,
                                        INCW_XPiI, INCD_XPiI,
                                        DECB_XPiI, DECH_XPiI,
                                        DECW_XPiI, DECD_XPiI]>,
                                      CheckImmOperand<2, 31>,
                                      CheckAny<[
                                        CheckImmOperand<3, 1>,
                                        CheckImmOperand<3, 2>,
                                        CheckImmOperand<3, 4>]>]>>;

// Identify "[SU]?(MADD|MSUB)L?" as the alias for "[SU]?(MUL|MNEG)L?".
def NeoverseMULIdiomPred : MCSchedPredicate< // <op> Rd, Rs, Rv, ZR
                             CheckAll<[CheckOpcode<
                                         [MADDWrrr, MADDXrrr,
                                          MSUBWrrr, MSUBXrrr,
                                          SMADDLrrr, UMADDLrrr,
                                          SMSUBLrrr, UMSUBLrrr]>,
                                       CheckIsReg3Zero]>>;

def NeoverseZeroMove : MCSchedPredicate<
                         CheckAny<[
                           // MOV Wd, #0
                           // MOV Xd, #0
                           CheckAll<[CheckOpcode<[MOVZWi, MOVZXi]>,
                                     CheckIsImmOperand<1>,
                                     CheckImmOperand<1, 0>,
                                     CheckImmOperand<2, 0>]>,
                           // MOV Wd, WZR
                           // MOV Xd, XZR
                           // MOV Wd, Wn
                           // MOV Xd, Xn
                           CheckAll<[CheckOpcode<[ORRWrs, ORRXrs]>,
                                     CheckAll<[CheckIsReg1Zero,
                                               CheckImmOperand<3, 0>]>]>,
                           // FMOV Hd, WZR
                           // FMOV Hd, XZR
                           // FMOV Sd, WZR
                           // FMOV Dd, XZR
                           CheckAll<[CheckOpcode<[FMOVWHr, FMOVXHr,
                                                  FMOVWSr, FMOVXDr]>,
                                     CheckIsReg1Zero]>,
                           // MOVI Dd, #0
                           // MOVI Vd.2D, #0
                           CheckAll<[CheckOpcode<[MOVID, MOVIv2d_ns]>,
                                     CheckImmOperand<1, 0>]>
                         ]>>;
