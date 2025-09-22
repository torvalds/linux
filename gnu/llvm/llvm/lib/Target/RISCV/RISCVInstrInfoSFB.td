//===-- RISCVInstrInfoSFB.td - Pseudos for SFB -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the pseudos for SFB (Short Forward Branch).
//
//===----------------------------------------------------------------------===//

let Predicates = [HasShortForwardBranchOpt], isSelect = 1,
    Constraints = "$dst = $falsev", isCommutable = 1, Size = 8 in {
// This instruction moves $truev to $dst when the condition is true. It will
// be expanded to control flow in RISCVExpandPseudoInsts.
def PseudoCCMOVGPR : Pseudo<(outs GPR:$dst),
                            (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                             GPR:$falsev, GPR:$truev),
                            [(set GPR:$dst,
                              (riscv_selectcc_frag:$cc (XLenVT GPR:$lhs),
                                                       GPR:$rhs, cond,
                                                       (XLenVT GPR:$truev),
                                                       GPR:$falsev))]>,
                     Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                            ReadSFBALU, ReadSFBALU]>;
}

// This should always expand to a branch+c.mv so the size is 6 or 4 if the
// branch is compressible.
let Predicates = [HasConditionalMoveFusion, NoShortForwardBranchOpt],
    Constraints = "$dst = $falsev", isCommutable = 1, Size = 6 in {
// This instruction moves $truev to $dst when the condition is true. It will
// be expanded to control flow in RISCVExpandPseudoInsts.
// We use GPRNoX0 because c.mv cannot encode X0.
def PseudoCCMOVGPRNoX0 : Pseudo<(outs GPRNoX0:$dst),
                                (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                                 GPRNoX0:$falsev, GPRNoX0:$truev),
                                [(set GPRNoX0:$dst,
                                  (riscv_selectcc_frag:$cc (XLenVT GPR:$lhs),
                                                           (XLenVT GPR:$rhs),
                                                           cond, (XLenVT GPRNoX0:$truev),
                                                           (XLenVT GPRNoX0:$falsev)))]>,
                         Sched<[]>;
}

// Conditional binops, that updates update $dst to (op rs1, rs2) when condition
// is true. Returns $falsev otherwise. Selected by optimizeSelect.
// TODO: Can we use DefaultOperands on the regular binop to accomplish this more
// like how ARM does predication?
let Predicates = [HasShortForwardBranchOpt], hasSideEffects = 0,
    mayLoad = 0, mayStore = 0, Size = 8, Constraints = "$dst = $falsev" in {
def PseudoCCADD : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCSUB : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCSLL : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                         ReadSFBALU, ReadSFBALU]>;
def PseudoCCSRL : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                         ReadSFBALU, ReadSFBALU]>;
def PseudoCCSRA : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                         ReadSFBALU, ReadSFBALU]>;
def PseudoCCAND : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCOR  : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCXOR : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;

def PseudoCCADDI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCSLLI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCSRLI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCSRAI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCANDI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCORI  : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;
def PseudoCCXORI : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU]>;

// RV64I instructions
def PseudoCCADDW : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                          ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCSUBW : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                          ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCSLLW : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU, ReadSFBALU]>;
def PseudoCCSRLW : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU, ReadSFBALU]>;
def PseudoCCSRAW : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                          ReadSFBALU, ReadSFBALU]>;

def PseudoCCADDIW : Pseudo<(outs GPR:$dst),
                           (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                            GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                    Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                           ReadSFBALU]>;
def PseudoCCSLLIW : Pseudo<(outs GPR:$dst),
                           (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                            GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                    Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                           ReadSFBALU]>;
def PseudoCCSRLIW : Pseudo<(outs GPR:$dst),
                           (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                            GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                    Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                           ReadSFBALU]>;
def PseudoCCSRAIW : Pseudo<(outs GPR:$dst),
                           (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                            GPR:$falsev, GPR:$rs1, simm12:$rs2), []>,
                    Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp, ReadSFBALU,
                           ReadSFBALU]>;

// Zbb/Zbkb instructions
def PseudoCCANDN : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                          ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCORN : Pseudo<(outs GPR:$dst),
                         (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                          GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                  Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                         ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
def PseudoCCXNOR : Pseudo<(outs GPR:$dst),
                          (ins GPR:$lhs, GPR:$rhs, ixlenimm:$cc,
                           GPR:$falsev, GPR:$rs1, GPR:$rs2), []>,
                   Sched<[WriteSFB, ReadSFBJmp, ReadSFBJmp,
                          ReadSFBALU, ReadSFBALU, ReadSFBALU]>;
}

let Predicates = [HasShortForwardBranchOpt] in
def : Pat<(XLenVT (abs GPR:$rs1)),
          (PseudoCCSUB (XLenVT GPR:$rs1), (XLenVT X0), /* COND_LT */ 2,
           (XLenVT GPR:$rs1), (XLenVT X0), (XLenVT GPR:$rs1))>;
let Predicates = [HasShortForwardBranchOpt, IsRV64] in
def : Pat<(sext_inreg (abs 33signbits_node:$rs1), i32),
          (PseudoCCSUBW (i64 GPR:$rs1), (i64 X0), /* COND_LT */ 2,
           (i64 GPR:$rs1), (i64 X0), (i64 GPR:$rs1))>;
