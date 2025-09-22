//===-- RISCVInstrInfoZfa.td - RISC-V 'Zfa' instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'Zfa'
// additional floating-point extension, version 1.0.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

// 5-bit floating-point immediate encodings.
def LoadFPImmOperand : AsmOperandClass {
  let Name = "LoadFPImm";
  let ParserMethod = "parseFPImm";
  let RenderMethod = "addFPImmOperands";
  let DiagnosticType = "InvalidLoadFPImm";
}

def loadfpimm : Operand<XLenVT> {
  let ParserMatchClass = LoadFPImmOperand;
  let PrintMethod = "printFPImmOperand";
}

def RTZArg : AsmOperandClass {
  let Name = "RTZArg";
  let RenderMethod = "addFRMArgOperands";
  let DiagnosticType = "InvalidRTZArg";
  let ParserMethod = "parseFRMArg";
}

def rtzarg : Operand<XLenVT> {
  let ParserMatchClass = RTZArg;
  let PrintMethod = "printFRMArg";
  let DecoderMethod = "decodeFRMArg";
}

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1 in
class FPBinaryOp_rr<bits<7> funct7, bits<3> funct3, DAGOperand rdty,
                    DAGOperand rsty, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs rdty:$rd),
              (ins rsty:$rs1, rsty:$rs2), opcodestr, "$rd, $rs1, $rs2">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPFLI_r<bits<7> funct7, bits<5> rs2val, bits<3> funct3,
              DAGOperand rdty, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs rdty:$rd),
              (ins loadfpimm:$imm), opcodestr, "$rd, $imm"> {
  bits<5> imm;

  let rs2 = rs2val;
  let rs1 = imm;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    UseNamedOperandTable = 1, hasPostISelHook = 1 in
class FPUnaryOp_r_rtz<bits<7> funct7, bits<5> rs2val, DAGOperand rdty,
                      DAGOperand rs1ty, string opcodestr>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs rdty:$rd),
                 (ins rs1ty:$rs1, rtzarg:$frm), opcodestr,
                  "$rd, $rs1$frm"> {
  let rs2 = rs2val;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZfa] in {
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def FLI_S : FPFLI_r<0b1111000, 0b00001, 0b000, FPR32, "fli.s">,
            Sched<[WriteFLI32]>;

let SchedRW = [WriteFMinMax32, ReadFMinMax32, ReadFMinMax32] in {
def FMINM_S: FPALU_rr<0b0010100, 0b010, "fminm.s", FPR32, Commutable=1>;
def FMAXM_S: FPALU_rr<0b0010100, 0b011, "fmaxm.s", FPR32, Commutable=1>;
}

def FROUND_S : FPUnaryOp_r_frm<0b0100000, 0b00100, FPR32, FPR32, "fround.s">,
               Sched<[WriteFRoundF32, ReadFRoundF32]>;
def FROUNDNX_S : FPUnaryOp_r_frm<0b0100000, 0b00101, FPR32, FPR32, "froundnx.s">,
                 Sched<[WriteFRoundF32, ReadFRoundF32]>;

let SchedRW = [WriteFCmp32, ReadFCmp32, ReadFCmp32] in {
def FLTQ_S : FPCmp_rr<0b1010000, 0b101, "fltq.s", FPR32>;
def FLEQ_S : FPCmp_rr<0b1010000, 0b100, "fleq.s", FPR32>;
}
} // Predicates = [HasStdExtZfa]

let Predicates = [HasStdExtZfa, HasStdExtD] in {
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def FLI_D : FPFLI_r<0b1111001, 0b00001, 0b000, FPR64, "fli.d">,
            Sched<[WriteFLI64]>;

let SchedRW = [WriteFMinMax64, ReadFMinMax64, ReadFMinMax64] in {
def FMINM_D: FPALU_rr<0b0010101, 0b010, "fminm.d", FPR64, Commutable=1>;
def FMAXM_D: FPALU_rr<0b0010101, 0b011, "fmaxm.d", FPR64, Commutable=1>;
}

def FROUND_D : FPUnaryOp_r_frm<0b0100001, 0b00100, FPR64, FPR64, "fround.d">,
               Sched<[WriteFRoundF64, ReadFRoundF64]>;
def FROUNDNX_D : FPUnaryOp_r_frm<0b0100001, 0b00101, FPR64, FPR64, "froundnx.d">,
                 Sched<[WriteFRoundF64, ReadFRoundF64]>;

let IsSignExtendingOpW = 1 in
def FCVTMOD_W_D
    : FPUnaryOp_r_rtz<0b1100001, 0b01000, GPR, FPR64, "fcvtmod.w.d">,
      Sched<[WriteFCvtF64ToI32, ReadFCvtF64ToI32]>;

let SchedRW = [WriteFCmp64, ReadFCmp64, ReadFCmp64] in {
def FLTQ_D : FPCmp_rr<0b1010001, 0b101, "fltq.d", FPR64>;
def FLEQ_D : FPCmp_rr<0b1010001, 0b100, "fleq.d", FPR64>;
}
} // Predicates = [HasStdExtZfa, HasStdExtD]

let Predicates = [HasStdExtZfa, HasStdExtD, IsRV32] in {
let mayRaiseFPException = 0 in {
def FMVH_X_D : FPUnaryOp_r<0b1110001, 0b00001, 0b000, GPR, FPR64, "fmvh.x.d">,
               Sched<[WriteFMovF64ToI64, ReadFMovF64ToI64]>;
def FMVP_D_X : FPBinaryOp_rr<0b1011001, 0b000, FPR64, GPR, "fmvp.d.x">,
               Sched<[WriteFMovI64ToF64, ReadFMovI64ToF64]>;
}

let isCodeGenOnly = 1, mayRaiseFPException = 0 in
def FMV_X_W_FPR64 : FPUnaryOp_r<0b1110000, 0b00000, 0b000, GPR, FPR64,
                                "fmv.x.w">,
                    Sched<[WriteFMovF64ToI64, ReadFMovF64ToI64]>;
} // Predicates = [HasStdExtZfa, HasStdExtD, IsRV32]

let Predicates = [HasStdExtZfa, HasStdExtZfhOrZvfh] in
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def FLI_H : FPFLI_r<0b1111010, 0b00001, 0b000, FPR16, "fli.h">,
            Sched<[WriteFLI16]>;

let Predicates = [HasStdExtZfa, HasStdExtZfh] in {
let SchedRW = [WriteFMinMax16, ReadFMinMax16, ReadFMinMax16] in {
def FMINM_H: FPALU_rr<0b0010110, 0b010, "fminm.h", FPR16, Commutable=1>;
def FMAXM_H: FPALU_rr<0b0010110, 0b011, "fmaxm.h", FPR16, Commutable=1>;
}

def FROUND_H : FPUnaryOp_r_frm<0b0100010, 0b00100, FPR16, FPR16, "fround.h">,
               Sched<[WriteFRoundF16, ReadFRoundF16]>;
def FROUNDNX_H : FPUnaryOp_r_frm<0b0100010, 0b00101, FPR16, FPR16, "froundnx.h">,
                 Sched<[WriteFRoundF16, ReadFRoundF16]>;

let SchedRW = [WriteFCmp16, ReadFCmp16, ReadFCmp16] in {
def FLTQ_H : FPCmp_rr<0b1010010, 0b101, "fltq.h", FPR16>;
def FLEQ_H : FPCmp_rr<0b1010010, 0b100, "fleq.h", FPR16>;
}
} // Predicates = [HasStdExtZfa, HasStdExtZfh]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZfa] in {
def : InstAlias<"fgtq.s $rd, $rs, $rt",
                (FLTQ_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;
def : InstAlias<"fgeq.s $rd, $rs, $rt",
                (FLEQ_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;
}

let Predicates = [HasStdExtZfa, HasStdExtD] in {
def : InstAlias<"fgtq.d $rd, $rs, $rt",
                (FLTQ_D GPR:$rd, FPR64:$rt, FPR64:$rs), 0>;
def : InstAlias<"fgeq.d $rd, $rs, $rt",
                (FLEQ_D GPR:$rd, FPR64:$rt, FPR64:$rs), 0>;
}

let Predicates = [HasStdExtZfa, HasStdExtZfh] in {
def : InstAlias<"fgtq.h $rd, $rs, $rt",
                (FLTQ_H GPR:$rd, FPR16:$rt, FPR16:$rs), 0>;
def : InstAlias<"fgeq.h $rd, $rs, $rt",
                (FLEQ_H GPR:$rd, FPR16:$rt, FPR16:$rs), 0>;
}

//===----------------------------------------------------------------------===//
// Codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZfa] in {
def: PatFprFpr<fminimum, FMINM_S, FPR32, f32>;
def: PatFprFpr<fmaximum, FMAXM_S, FPR32, f32>;

// frint rounds according to the current rounding mode and detects
// inexact conditions.
def: Pat<(any_frint FPR32:$rs1), (FROUNDNX_S FPR32:$rs1, FRM_DYN)>;

// fnearbyint is like frint but does not detect inexact conditions.
def: Pat<(any_fnearbyint FPR32:$rs1), (FROUND_S FPR32:$rs1, FRM_DYN)>;

def: Pat<(any_fround FPR32:$rs1), (FROUND_S FPR32:$rs1, FRM_RMM)>;
def: Pat<(any_ffloor FPR32:$rs1), (FROUND_S FPR32:$rs1, FRM_RDN)>;
def: Pat<(any_fceil FPR32:$rs1), (FROUND_S FPR32:$rs1, FRM_RUP)>;
def: Pat<(any_ftrunc FPR32:$rs1), (FROUND_S FPR32:$rs1, FRM_RTZ)>;

def: PatSetCC<FPR32, strict_fsetcc, SETLT, FLTQ_S, f32>;
def: PatSetCC<FPR32, strict_fsetcc, SETOLT, FLTQ_S, f32>;
def: PatSetCC<FPR32, strict_fsetcc, SETLE, FLEQ_S, f32>;
def: PatSetCC<FPR32, strict_fsetcc, SETOLE, FLEQ_S, f32>;
} // Predicates = [HasStdExtZfa]

let Predicates = [HasStdExtZfa, HasStdExtD] in {
def: PatFprFpr<fminimum, FMINM_D, FPR64, f64>;
def: PatFprFpr<fmaximum, FMAXM_D, FPR64, f64>;

// frint rounds according to the current rounding mode and detects
// inexact conditions.
def: Pat<(any_frint FPR64:$rs1), (FROUNDNX_D FPR64:$rs1, FRM_DYN)>;

// fnearbyint is like frint but does not detect inexact conditions.
def: Pat<(any_fnearbyint FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_DYN)>;

def: Pat<(any_fround FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_RMM)>;
def: Pat<(any_froundeven FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_RNE)>;
def: Pat<(any_ffloor FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_RDN)>;
def: Pat<(any_fceil FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_RUP)>;
def: Pat<(any_ftrunc FPR64:$rs1), (FROUND_D FPR64:$rs1, FRM_RTZ)>;

def: PatSetCC<FPR64, strict_fsetcc, SETLT, FLTQ_D, f64>;
def: PatSetCC<FPR64, strict_fsetcc, SETOLT, FLTQ_D, f64>;
def: PatSetCC<FPR64, strict_fsetcc, SETLE, FLEQ_D, f64>;
def: PatSetCC<FPR64, strict_fsetcc, SETOLE, FLEQ_D, f64>;
} // Predicates = [HasStdExtZfa, HasStdExtD]

let Predicates = [HasStdExtZfa, HasStdExtD, IsRV32] in {
def : Pat<(RISCVBuildPairF64 GPR:$rs1, GPR:$rs2),
          (FMVP_D_X GPR:$rs1, GPR:$rs2)>;
}

let Predicates = [HasStdExtZfa, HasStdExtZfh] in {
def: PatFprFpr<fminimum, FMINM_H, FPR16, f16>;
def: PatFprFpr<fmaximum, FMAXM_H, FPR16, f16>;

// frint rounds according to the current rounding mode and detects
// inexact conditions.
def: Pat<(f16 (any_frint FPR16:$rs1)), (FROUNDNX_H FPR16:$rs1, FRM_DYN)>;

// fnearbyint is like frint but does not detect inexact conditions.
def: Pat<(f16 (any_fnearbyint FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_DYN)>;

def: Pat<(f16 (any_fround FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_RMM)>;
def: Pat<(f16 (any_froundeven FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_RNE)>;
def: Pat<(f16 (any_ffloor FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_RDN)>;
def: Pat<(f16 (any_fceil FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_RUP)>;
def: Pat<(f16 (any_ftrunc FPR16:$rs1)), (FROUND_H FPR16:$rs1, FRM_RTZ)>;

def: PatSetCC<FPR16, strict_fsetcc, SETLT, FLTQ_H, f16>;
def: PatSetCC<FPR16, strict_fsetcc, SETOLT, FLTQ_H, f16>;
def: PatSetCC<FPR16, strict_fsetcc, SETLE, FLEQ_H, f16>;
def: PatSetCC<FPR16, strict_fsetcc, SETOLE, FLEQ_H, f16>;
} // Predicates = [HasStdExtZfa, HasStdExtZfh]
