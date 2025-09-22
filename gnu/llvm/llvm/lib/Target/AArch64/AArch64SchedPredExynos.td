//===- AArch64SchedPredExynos.td - AArch64 Sched Preds -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines scheduling predicate definitions that are used by the
// AArch64 Exynos processors.
//
//===----------------------------------------------------------------------===//

// Auxiliary predicates.

// Check the shift in arithmetic and logic instructions.
def ExynosCheckShift : CheckAny<[CheckShiftBy0,
                                 CheckAll<
                                   [CheckShiftLSL,
                                    CheckAny<
                                      [CheckShiftBy1,
                                       CheckShiftBy2,
                                       CheckShiftBy3]>]>]>;

// Exynos predicates.

// Identify BLR specifying the LR register as the indirect target register.
def ExynosBranchLinkLRPred : MCSchedPredicate<
                               CheckAll<[CheckOpcode<[BLR]>,
                                         CheckRegOperand<0, LR>]>>;

// Identify arithmetic instructions without or with limited extension or shift.
def ExynosArithFn   : TIIPredicate<
                        "isExynosArithFast",
                        MCOpcodeSwitchStatement<
                          [MCOpcodeSwitchCase<
                             IsArithExtOp.ValidOpcodes,
                             MCReturnStatement<
                               CheckAny<[CheckExtBy0,
                                         CheckAll<
                                           [CheckAny<
                                             [CheckExtUXTW,
                                              CheckExtUXTX]>,
                                            CheckAny<
                                              [CheckExtBy1,
                                               CheckExtBy2,
                                               CheckExtBy3]>]>]>>>,
                           MCOpcodeSwitchCase<
                             IsArithShiftOp.ValidOpcodes,
                             MCReturnStatement<ExynosCheckShift>>,
                           MCOpcodeSwitchCase<
                             IsArithUnshiftOp.ValidOpcodes,
                             MCReturnStatement<TruePred>>,
                           MCOpcodeSwitchCase<
                             IsArithImmOp.ValidOpcodes,
                             MCReturnStatement<TruePred>>],
                          MCReturnStatement<FalsePred>>>;
def ExynosArithPred : MCSchedPredicate<ExynosArithFn>;

// Identify logic instructions with limited shift.
def ExynosLogicFn   : TIIPredicate<
                        "isExynosLogicFast",
                        MCOpcodeSwitchStatement<
                          [MCOpcodeSwitchCase<
                             IsLogicShiftOp.ValidOpcodes,
                             MCReturnStatement<ExynosCheckShift>>,
                           MCOpcodeSwitchCase<
                             IsLogicUnshiftOp.ValidOpcodes,
                             MCReturnStatement<TruePred>>,
                           MCOpcodeSwitchCase<
                             IsLogicImmOp.ValidOpcodes,
                             MCReturnStatement<TruePred>>],
                          MCReturnStatement<FalsePred>>>;
def ExynosLogicPred : MCSchedPredicate<ExynosLogicFn>;

// Identify more logic instructions with limited shift.
def ExynosLogicExFn   : TIIPredicate<
                          "isExynosLogicExFast",
                          MCOpcodeSwitchStatement<
                            [MCOpcodeSwitchCase<
                               IsLogicShiftOp.ValidOpcodes,
                               MCReturnStatement<
                                 CheckAny<
                                   [ExynosCheckShift,
                                    CheckAll<
                                     [CheckShiftLSL,
                                      CheckShiftBy8]>]>>>,
                             MCOpcodeSwitchCase<
                               IsLogicUnshiftOp.ValidOpcodes,
                               MCReturnStatement<TruePred>>,
                             MCOpcodeSwitchCase<
                               IsLogicImmOp.ValidOpcodes,
                               MCReturnStatement<TruePred>>],
                            MCReturnStatement<FalsePred>>>;
def ExynosLogicExPred : MCSchedPredicate<ExynosLogicExFn>;

// Identify a load or store using the register offset addressing mode
// with a scaled non-extended register.
def ExynosScaledIdxFn   : TIIPredicate<"isExynosScaledAddr",
                                       MCOpcodeSwitchStatement<
                                         [MCOpcodeSwitchCase<
                                            IsLoadStoreRegOffsetOp.ValidOpcodes,
                                            MCReturnStatement<
                                              CheckAny<
                                                [CheckMemExtSXTW,
                                                 CheckMemExtUXTW,
                                                 CheckMemScaled]>>>],
                                         MCReturnStatement<FalsePred>>>;
def ExynosScaledIdxPred : MCSchedPredicate<ExynosScaledIdxFn>;

// Identify FP instructions.
def ExynosFPPred : MCSchedPredicate<CheckFpOrNEON>;

// Identify 128-bit NEON instructions.
def ExynosQFormPred : MCSchedPredicate<CheckQForm>;

// Identify instructions that reset a register efficiently.
def ExynosResetFn   : TIIPredicate<
                        "isExynosResetFast",
                        MCOpcodeSwitchStatement<
                          [MCOpcodeSwitchCase<
                             [ADR, ADRP,
                              MOVNWi, MOVNXi,
                              MOVZWi, MOVZXi],
                             MCReturnStatement<TruePred>>,
                           MCOpcodeSwitchCase<
                             [ORRWri, ORRXri],
                             MCReturnStatement<CheckIsReg1Zero>>],
                          MCReturnStatement<
                            CheckAny<
                              [IsCopyIdiomFn,
                               IsZeroFPIdiomFn]>>>>;
def ExynosResetPred : MCSchedPredicate<ExynosResetFn>;

// Identify cheap arithmetic and logic immediate instructions.
def ExynosCheapFn : TIIPredicate<
                      "isExynosCheapAsMove",
                      MCOpcodeSwitchStatement<
                        [MCOpcodeSwitchCase<
                           IsArithLogicImmOp.ValidOpcodes,
                           MCReturnStatement<TruePred>>],
                        MCReturnStatement<
                          CheckAny<
                            [ExynosArithFn, ExynosResetFn, ExynosLogicFn]>>>>;
