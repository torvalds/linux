//===-- RISCVInstrInfoF.td - RISC-V 'F' instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'F',
// Single-Precision Floating-Point instruction set extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RISC-V specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_RISCVFMV_W_X_RV64
    : SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisVT<1, i64>]>;
def SDT_RISCVFMV_X_ANYEXTW_RV64
    : SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, f32>]>;
def SDT_RISCVFCVT_W_RV64
    : SDTypeProfile<1, 2, [SDTCisVT<0, i64>, SDTCisFP<1>,
                           SDTCisVT<2, i64>]>;
def SDT_RISCVFCVT_X
    : SDTypeProfile<1, 2, [SDTCisVT<0, XLenVT>, SDTCisFP<1>,
                           SDTCisVT<2, XLenVT>]>;

def SDT_RISCVFROUND
    : SDTypeProfile<1, 3, [SDTCisFP<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                           SDTCisVT<3, XLenVT>]>;
def SDT_RISCVFCLASS
    : SDTypeProfile<1, 1, [SDTCisVT<0, XLenVT>, SDTCisFP<1>]>;

def riscv_fclass
    : SDNode<"RISCVISD::FCLASS", SDT_RISCVFCLASS>;

def riscv_fround
    : SDNode<"RISCVISD::FROUND", SDT_RISCVFROUND>;

def riscv_fmv_w_x_rv64
    : SDNode<"RISCVISD::FMV_W_X_RV64", SDT_RISCVFMV_W_X_RV64>;
def riscv_fmv_x_anyextw_rv64
    : SDNode<"RISCVISD::FMV_X_ANYEXTW_RV64", SDT_RISCVFMV_X_ANYEXTW_RV64>;
def riscv_fcvt_w_rv64
    : SDNode<"RISCVISD::FCVT_W_RV64", SDT_RISCVFCVT_W_RV64>;
def riscv_fcvt_wu_rv64
    : SDNode<"RISCVISD::FCVT_WU_RV64", SDT_RISCVFCVT_W_RV64>;
def riscv_fcvt_x
    : SDNode<"RISCVISD::FCVT_X", SDT_RISCVFCVT_X>;
def riscv_fcvt_xu
    : SDNode<"RISCVISD::FCVT_XU", SDT_RISCVFCVT_X>;

def riscv_fmin : SDNode<"RISCVISD::FMIN", SDTFPBinOp>;
def riscv_fmax : SDNode<"RISCVISD::FMAX", SDTFPBinOp>;

def riscv_strict_fcvt_w_rv64
    : SDNode<"RISCVISD::STRICT_FCVT_W_RV64", SDT_RISCVFCVT_W_RV64,
             [SDNPHasChain]>;
def riscv_strict_fcvt_wu_rv64
    : SDNode<"RISCVISD::STRICT_FCVT_WU_RV64", SDT_RISCVFCVT_W_RV64,
             [SDNPHasChain]>;

def riscv_any_fcvt_w_rv64 : PatFrags<(ops node:$src, node:$frm),
                                     [(riscv_strict_fcvt_w_rv64 node:$src, node:$frm),
                                      (riscv_fcvt_w_rv64 node:$src, node:$frm)]>;
def riscv_any_fcvt_wu_rv64 : PatFrags<(ops node:$src, node:$frm),
                                      [(riscv_strict_fcvt_wu_rv64 node:$src, node:$frm),
                                       (riscv_fcvt_wu_rv64 node:$src, node:$frm)]>;

def any_fma_nsz : PatFrag<(ops node:$rs1, node:$rs2, node:$rs3),
                          (any_fma node:$rs1, node:$rs2, node:$rs3), [{
  return N->getFlags().hasNoSignedZeros();
}]>;
//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

// Zfinx

def GPRAsFPR : AsmOperandClass {
  let Name = "GPRAsFPR";
  let ParserMethod = "parseGPRAsFPR";
  let RenderMethod = "addRegOperands";
}

def FPR32INX : RegisterOperand<GPRF32> {
  let ParserMatchClass = GPRAsFPR;
  let DecoderMethod = "DecodeGPRRegisterClass";
}

// Describes a combination of predicates from F/D/Zfh/Zfhmin or
// Zfinx/Zdinx/Zhinx/Zhinxmin that are applied to scalar FP instruction.
// Contains the DAGOperand for the primary type for the predicates. The primary
// type may be unset for combinations of predicates like Zfh+D.
// Also contains the DAGOperand for f16/f32/f64, instruction suffix, and
// decoder namespace that go with an instruction given those predicates.
//
// The DAGOperand can be unset if the predicates are not enough to define it.
class ExtInfo<string suffix, string space, list<Predicate> predicates,
              ValueType primaryvt, DAGOperand primaryty, DAGOperand f32ty,
              DAGOperand f64ty, DAGOperand f16ty> {
  list<Predicate> Predicates = predicates;
  string Suffix = suffix;
  string Space = space;
  DAGOperand PrimaryTy = primaryty;
  DAGOperand F16Ty = f16ty;
  DAGOperand F32Ty = f32ty;
  DAGOperand F64Ty = f64ty;
  ValueType PrimaryVT = primaryvt;
}

def FExt       : ExtInfo<"", "", [HasStdExtF], f32, FPR32, FPR32, ?, ?>;

def ZfinxExt   : ExtInfo<"_INX", "RVZfinx", [HasStdExtZfinx], f32, FPR32INX, FPR32INX, ?, ?>;

defvar FExts   = [FExt, ZfinxExt];

// Floating-point rounding mode

def FRMArg : AsmOperandClass {
  let Name = "FRMArg";
  let RenderMethod = "addFRMArgOperands";
  let ParserMethod = "parseFRMArg";
  let IsOptional = 1;
  let DefaultMethod = "defaultFRMArgOp";
}

def frmarg : Operand<XLenVT> {
  let ParserMatchClass = FRMArg;
  let PrintMethod = "printFRMArg";
  let DecoderMethod = "decodeFRMArg";
}

// Variants of the rounding mode operand that default to 'rne'. This is used
// for historical/legacy reasons. fcvt functions where the rounding mode
// doesn't affect the output originally always set it to 0b000 ('rne'). As old
// versions of LLVM and GCC will fail to decode versions of these instructions
// with the rounding mode set to something other than 'rne', we retain this
// default.
def FRMArgLegacy : AsmOperandClass {
  let Name = "FRMArgLegacy";
  let RenderMethod = "addFRMArgOperands";
  let ParserMethod = "parseFRMArg";
  let IsOptional = 1;
  let DefaultMethod = "defaultFRMArgLegacyOp";
}

def frmarglegacy : Operand<XLenVT> {
  let ParserMatchClass = FRMArgLegacy;
  let PrintMethod = "printFRMArgLegacy";
  let DecoderMethod = "decodeFRMArg";
}

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class FPLoad_r<bits<3> funct3, string opcodestr, DAGOperand rty,
               SchedWrite sw>
    : RVInstI<funct3, OPC_LOAD_FP, (outs rty:$rd),
              (ins GPRMem:$rs1, simm12:$imm12),
              opcodestr, "$rd, ${imm12}(${rs1})">,
      Sched<[sw, ReadFMemBase]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class FPStore_r<bits<3> funct3, string opcodestr, DAGOperand rty,
                SchedWrite sw>
    : RVInstS<funct3, OPC_STORE_FP, (outs),
              (ins rty:$rs2, GPRMem:$rs1, simm12:$imm12),
              opcodestr, "$rs2, ${imm12}(${rs1})">,
      Sched<[sw, ReadFStoreData, ReadFMemBase]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    UseNamedOperandTable = 1, hasPostISelHook = 1, isCommutable = 1 in
class FPFMA_rrr_frm<RISCVOpcode opcode, bits<2> funct2, string opcodestr,
                    DAGOperand rty>
    : RVInstR4Frm<funct2, opcode, (outs rty:$rd),
                  (ins rty:$rs1, rty:$rs2, rty:$rs3, frmarg:$frm),
                  opcodestr, "$rd, $rs1, $rs2, $rs3$frm">;

multiclass FPFMA_rrr_frm_m<RISCVOpcode opcode, bits<2> funct2,
                           string opcodestr, ExtInfo Ext> {
  let Predicates = Ext.Predicates, DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPFMA_rrr_frm<opcode, funct2, opcodestr, Ext.PrimaryTy>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1 in
class FPALU_rr<bits<7> funct7, bits<3> funct3, string opcodestr,
               DAGOperand rty, bit Commutable>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs rty:$rd),
              (ins rty:$rs1, rty:$rs2), opcodestr, "$rd, $rs1, $rs2"> {
  let isCommutable = Commutable;
}
multiclass FPALU_rr_m<bits<7> funct7, bits<3> funct3, string opcodestr,
                      ExtInfo Ext, bit Commutable = 0> {
  let Predicates = Ext.Predicates, DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPALU_rr<funct7, funct3, opcodestr, Ext.PrimaryTy, Commutable>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    UseNamedOperandTable = 1, hasPostISelHook = 1 in
class FPALU_rr_frm<bits<7> funct7, string opcodestr, DAGOperand rty,
                   bit Commutable>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs rty:$rd),
                 (ins rty:$rs1, rty:$rs2, frmarg:$frm), opcodestr,
                  "$rd, $rs1, $rs2$frm"> {
  let isCommutable = Commutable;
}
multiclass FPALU_rr_frm_m<bits<7> funct7, string opcodestr,
                          ExtInfo Ext, bit Commutable = 0> {
  let Predicates = Ext.Predicates, DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPALU_rr_frm<funct7, opcodestr, Ext.PrimaryTy, Commutable>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1 in
class FPUnaryOp_r<bits<7> funct7, bits<5> rs2val, bits<3> funct3,
                  DAGOperand rdty, DAGOperand rs1ty, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs rdty:$rd), (ins rs1ty:$rs1),
              opcodestr, "$rd, $rs1"> {
  let rs2 = rs2val;
}
multiclass FPUnaryOp_r_m<bits<7> funct7, bits<5> rs2val, bits<3> funct3,
                         ExtInfo Ext, DAGOperand rdty, DAGOperand rs1ty,
                         string opcodestr> {
  let Predicates = Ext.Predicates, DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPUnaryOp_r<funct7, rs2val, funct3, rdty, rs1ty, opcodestr>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    UseNamedOperandTable = 1, hasPostISelHook = 1 in
class FPUnaryOp_r_frm<bits<7> funct7, bits<5> rs2val, DAGOperand rdty,
                      DAGOperand rs1ty, string opcodestr>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs rdty:$rd),
                 (ins rs1ty:$rs1, frmarg:$frm), opcodestr,
                  "$rd, $rs1$frm"> {
  let rs2 = rs2val;
}
multiclass FPUnaryOp_r_frm_m<bits<7> funct7, bits<5> rs2val,
                             ExtInfo Ext, DAGOperand rdty, DAGOperand rs1ty,
                             string opcodestr, list<Predicate> ExtraPreds = []> {
  let Predicates = !listconcat(Ext.Predicates, ExtraPreds),
      DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPUnaryOp_r_frm<funct7, rs2val, rdty, rs1ty,
                                   opcodestr>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    UseNamedOperandTable = 1, hasPostISelHook = 1 in
class FPUnaryOp_r_frmlegacy<bits<7> funct7, bits<5> rs2val, DAGOperand rdty,
                            DAGOperand rs1ty, string opcodestr>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs rdty:$rd),
                 (ins rs1ty:$rs1, frmarglegacy:$frm), opcodestr,
                  "$rd, $rs1$frm"> {
  let rs2 = rs2val;
}
multiclass FPUnaryOp_r_frmlegacy_m<bits<7> funct7, bits<5> rs2val,
                                   ExtInfo Ext, DAGOperand rdty, DAGOperand rs1ty,
                                   string opcodestr, list<Predicate> ExtraPreds = []> {
  let Predicates = !listconcat(Ext.Predicates, ExtraPreds),
      DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPUnaryOp_r_frmlegacy<funct7, rs2val, rdty, rs1ty,
                                         opcodestr>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, mayRaiseFPException = 1,
    IsSignExtendingOpW = 1 in
class FPCmp_rr<bits<7> funct7, bits<3> funct3, string opcodestr,
               DAGOperand rty, bit Commutable = 0>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs GPR:$rd),
              (ins rty:$rs1, rty:$rs2), opcodestr, "$rd, $rs1, $rs2"> {
  let isCommutable = Commutable;
}
multiclass FPCmp_rr_m<bits<7> funct7, bits<3> funct3, string opcodestr,
                      ExtInfo Ext, bit Commutable = 0> {
  let Predicates = Ext.Predicates, DecoderNamespace = Ext.Space in
  def Ext.Suffix : FPCmp_rr<funct7, funct3, opcodestr, Ext.PrimaryTy, Commutable>;
}

class PseudoFROUND<DAGOperand Ty, ValueType vt>
    : Pseudo<(outs Ty:$rd), (ins Ty:$rs1, Ty:$rs2, ixlenimm:$rm),
      [(set Ty:$rd, (vt (riscv_fround Ty:$rs1, Ty:$rs2, timm:$rm)))]> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  let usesCustomInserter = 1;
  let mayRaiseFPException = 1;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtF] in {
def FLW : FPLoad_r<0b010, "flw", FPR32, WriteFLD32>;

// Operands for stores are in the order srcreg, base, offset rather than
// reflecting the order these fields are specified in the instruction
// encoding.
def FSW : FPStore_r<0b010, "fsw", FPR32, WriteFST32>;
} // Predicates = [HasStdExtF]

foreach Ext = FExts in {
  let SchedRW = [WriteFMA32, ReadFMA32, ReadFMA32, ReadFMA32Addend] in {
    defm FMADD_S  : FPFMA_rrr_frm_m<OPC_MADD,  0b00, "fmadd.s",  Ext>;
    defm FMSUB_S  : FPFMA_rrr_frm_m<OPC_MSUB,  0b00, "fmsub.s",  Ext>;
    defm FNMSUB_S : FPFMA_rrr_frm_m<OPC_NMSUB, 0b00, "fnmsub.s", Ext>;
    defm FNMADD_S : FPFMA_rrr_frm_m<OPC_NMADD, 0b00, "fnmadd.s", Ext>;
  }

  let SchedRW = [WriteFAdd32, ReadFAdd32, ReadFAdd32] in {
    defm FADD_S : FPALU_rr_frm_m<0b0000000, "fadd.s", Ext, Commutable=1>;
    defm FSUB_S : FPALU_rr_frm_m<0b0000100, "fsub.s", Ext>;
  }

  let SchedRW = [WriteFMul32, ReadFMul32, ReadFMul32] in
  defm FMUL_S : FPALU_rr_frm_m<0b0001000, "fmul.s", Ext, Commutable=1>;

  let SchedRW = [WriteFDiv32, ReadFDiv32, ReadFDiv32] in
  defm FDIV_S : FPALU_rr_frm_m<0b0001100, "fdiv.s", Ext>;

  defm FSQRT_S : FPUnaryOp_r_frm_m<0b0101100, 0b00000, Ext, Ext.PrimaryTy,
                                   Ext.PrimaryTy, "fsqrt.s">,
                 Sched<[WriteFSqrt32, ReadFSqrt32]>;

  let SchedRW = [WriteFSGNJ32, ReadFSGNJ32, ReadFSGNJ32],
      mayRaiseFPException = 0 in {
    defm FSGNJ_S  : FPALU_rr_m<0b0010000, 0b000, "fsgnj.s",  Ext>;
    defm FSGNJN_S : FPALU_rr_m<0b0010000, 0b001, "fsgnjn.s", Ext>;
    defm FSGNJX_S : FPALU_rr_m<0b0010000, 0b010, "fsgnjx.s", Ext>;
  }

  let SchedRW = [WriteFMinMax32, ReadFMinMax32, ReadFMinMax32] in {
    defm FMIN_S   : FPALU_rr_m<0b0010100, 0b000, "fmin.s", Ext, Commutable=1>;
    defm FMAX_S   : FPALU_rr_m<0b0010100, 0b001, "fmax.s", Ext, Commutable=1>;
  }

  let IsSignExtendingOpW = 1 in
  defm FCVT_W_S : FPUnaryOp_r_frm_m<0b1100000, 0b00000, Ext, GPR, Ext.PrimaryTy,
                                    "fcvt.w.s">,
                  Sched<[WriteFCvtF32ToI32, ReadFCvtF32ToI32]>;

  let IsSignExtendingOpW = 1 in
  defm FCVT_WU_S : FPUnaryOp_r_frm_m<0b1100000, 0b00001, Ext, GPR, Ext.PrimaryTy,
                                     "fcvt.wu.s">,
                   Sched<[WriteFCvtF32ToI32, ReadFCvtF32ToI32]>;

  let SchedRW = [WriteFCmp32, ReadFCmp32, ReadFCmp32] in {
  defm FEQ_S : FPCmp_rr_m<0b1010000, 0b010, "feq.s", Ext, Commutable=1>;
  defm FLT_S : FPCmp_rr_m<0b1010000, 0b001, "flt.s", Ext>;
  defm FLE_S : FPCmp_rr_m<0b1010000, 0b000, "fle.s", Ext>;
  }

  let mayRaiseFPException = 0 in
  defm FCLASS_S : FPUnaryOp_r_m<0b1110000, 0b00000, 0b001, Ext, GPR, Ext.PrimaryTy,
                                "fclass.s">,
                  Sched<[WriteFClass32, ReadFClass32]>;

  defm FCVT_S_W : FPUnaryOp_r_frm_m<0b1101000, 0b00000, Ext, Ext.PrimaryTy, GPR,
                                    "fcvt.s.w">,
                  Sched<[WriteFCvtI32ToF32, ReadFCvtI32ToF32]>;

  defm FCVT_S_WU : FPUnaryOp_r_frm_m<0b1101000, 0b00001, Ext, Ext.PrimaryTy, GPR,
                                     "fcvt.s.wu">,
                   Sched<[WriteFCvtI32ToF32, ReadFCvtI32ToF32]>;

  defm FCVT_L_S  : FPUnaryOp_r_frm_m<0b1100000, 0b00010, Ext, GPR, Ext.PrimaryTy,
                                     "fcvt.l.s", [IsRV64]>,
                   Sched<[WriteFCvtF32ToI64, ReadFCvtF32ToI64]>;

  defm FCVT_LU_S  : FPUnaryOp_r_frm_m<0b1100000, 0b00011, Ext, GPR, Ext.PrimaryTy,
                                      "fcvt.lu.s", [IsRV64]>,
                    Sched<[WriteFCvtF32ToI64, ReadFCvtF32ToI64]>;

  defm FCVT_S_L : FPUnaryOp_r_frm_m<0b1101000, 0b00010, Ext, Ext.PrimaryTy, GPR,
                                    "fcvt.s.l", [IsRV64]>,
                  Sched<[WriteFCvtI64ToF32, ReadFCvtI64ToF32]>;

  defm FCVT_S_LU : FPUnaryOp_r_frm_m<0b1101000, 0b00011, Ext, Ext.PrimaryTy, GPR,
                                     "fcvt.s.lu", [IsRV64]>,
                   Sched<[WriteFCvtI64ToF32, ReadFCvtI64ToF32]>;
} // foreach Ext = FExts

let Predicates = [HasStdExtF], mayRaiseFPException = 0,
    IsSignExtendingOpW = 1 in
def FMV_X_W : FPUnaryOp_r<0b1110000, 0b00000, 0b000, GPR, FPR32, "fmv.x.w">,
              Sched<[WriteFMovF32ToI32, ReadFMovF32ToI32]>;

let Predicates = [HasStdExtF], mayRaiseFPException = 0 in
def FMV_W_X : FPUnaryOp_r<0b1111000, 0b00000, 0b000, FPR32, GPR, "fmv.w.x">,
              Sched<[WriteFMovI32ToF32, ReadFMovI32ToF32]>;

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtF] in {
def : InstAlias<"flw $rd, (${rs1})",  (FLW FPR32:$rd,  GPR:$rs1, 0), 0>;
def : InstAlias<"fsw $rs2, (${rs1})", (FSW FPR32:$rs2, GPR:$rs1, 0), 0>;

def : InstAlias<"fmv.s $rd, $rs",  (FSGNJ_S  FPR32:$rd, FPR32:$rs, FPR32:$rs)>;
def : InstAlias<"fabs.s $rd, $rs", (FSGNJX_S FPR32:$rd, FPR32:$rs, FPR32:$rs)>;
def : InstAlias<"fneg.s $rd, $rs", (FSGNJN_S FPR32:$rd, FPR32:$rs, FPR32:$rs)>;

// fgt.s/fge.s are recognised by the GNU assembler but the canonical
// flt.s/fle.s forms will always be printed. Therefore, set a zero weight.
def : InstAlias<"fgt.s $rd, $rs, $rt",
                (FLT_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;
def : InstAlias<"fge.s $rd, $rs, $rt",
                (FLE_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;

// The following csr instructions actually alias instructions from the base ISA.
// However, it only makes sense to support them when the F extension is enabled.
// NOTE: "frcsr", "frrm", and "frflags" are more specialized version of "csrr".
def : InstAlias<"frcsr $rd",      (CSRRS GPR:$rd, SysRegFCSR.Encoding, X0), 2>;
def : InstAlias<"fscsr $rd, $rs", (CSRRW GPR:$rd, SysRegFCSR.Encoding, GPR:$rs)>;
def : InstAlias<"fscsr $rs",      (CSRRW      X0, SysRegFCSR.Encoding, GPR:$rs), 2>;

// frsr, fssr are obsolete aliases replaced by frcsr, fscsr, so give them
// zero weight.
def : InstAlias<"frsr $rd",       (CSRRS GPR:$rd, SysRegFCSR.Encoding, X0), 0>;
def : InstAlias<"fssr $rd, $rs",  (CSRRW GPR:$rd, SysRegFCSR.Encoding, GPR:$rs), 0>;
def : InstAlias<"fssr $rs",       (CSRRW      X0, SysRegFCSR.Encoding, GPR:$rs), 0>;

def : InstAlias<"frrm $rd",        (CSRRS  GPR:$rd, SysRegFRM.Encoding, X0), 2>;
def : InstAlias<"fsrm $rd, $rs",   (CSRRW  GPR:$rd, SysRegFRM.Encoding, GPR:$rs)>;
def : InstAlias<"fsrm $rs",        (CSRRW       X0, SysRegFRM.Encoding, GPR:$rs), 2>;
def : InstAlias<"fsrmi $rd, $imm", (CSRRWI GPR:$rd, SysRegFRM.Encoding, uimm5:$imm)>;
def : InstAlias<"fsrmi $imm",      (CSRRWI      X0, SysRegFRM.Encoding, uimm5:$imm), 2>;

def : InstAlias<"frflags $rd",        (CSRRS  GPR:$rd, SysRegFFLAGS.Encoding, X0), 2>;
def : InstAlias<"fsflags $rd, $rs",   (CSRRW  GPR:$rd, SysRegFFLAGS.Encoding, GPR:$rs)>;
def : InstAlias<"fsflags $rs",        (CSRRW       X0, SysRegFFLAGS.Encoding, GPR:$rs), 2>;
def : InstAlias<"fsflagsi $rd, $imm", (CSRRWI GPR:$rd, SysRegFFLAGS.Encoding, uimm5:$imm)>;
def : InstAlias<"fsflagsi $imm",      (CSRRWI      X0, SysRegFFLAGS.Encoding, uimm5:$imm), 2>;

// fmv.w.x and fmv.x.w were previously known as fmv.s.x and fmv.x.s. Both
// spellings should be supported by standard tools.
def : MnemonicAlias<"fmv.s.x", "fmv.w.x">;
def : MnemonicAlias<"fmv.x.s", "fmv.x.w">;

def PseudoFLW  : PseudoFloatLoad<"flw", FPR32>;
def PseudoFSW  : PseudoStore<"fsw", FPR32>;
let usesCustomInserter = 1 in {
def PseudoQuietFLE_S : PseudoQuietFCMP<FPR32>;
def PseudoQuietFLT_S : PseudoQuietFCMP<FPR32>;
}
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
def : InstAlias<"fabs.s $rd, $rs", (FSGNJX_S_INX FPR32INX:$rd, FPR32INX:$rs, FPR32INX:$rs)>;
def : InstAlias<"fneg.s $rd, $rs", (FSGNJN_S_INX FPR32INX:$rd, FPR32INX:$rs, FPR32INX:$rs)>;

def : InstAlias<"fgt.s $rd, $rs, $rt",
                (FLT_S_INX GPR:$rd, FPR32INX:$rt, FPR32INX:$rs), 0>;
def : InstAlias<"fge.s $rd, $rs, $rt",
                (FLE_S_INX GPR:$rd, FPR32INX:$rt, FPR32INX:$rs), 0>;
let usesCustomInserter = 1 in {
def PseudoQuietFLE_S_INX : PseudoQuietFCMP<FPR32INX>;
def PseudoQuietFLT_S_INX : PseudoQuietFCMP<FPR32INX>;
}
} // Predicates = [HasStdExtZfinx]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

defvar FRM_RNE = 0b000;
defvar FRM_RTZ = 0b001;
defvar FRM_RDN = 0b010;
defvar FRM_RUP = 0b011;
defvar FRM_RMM = 0b100;
defvar FRM_DYN = 0b111;

/// Floating point constants
def fpimm0    : PatLeaf<(fpimm), [{ return N->isExactlyValue(+0.0); }]>;

/// Generic pattern classes
class PatSetCC<DAGOperand Ty, SDPatternOperator OpNode, CondCode Cond,
               RVInst Inst, ValueType vt>
    : Pat<(XLenVT (OpNode (vt Ty:$rs1), Ty:$rs2, Cond)), (Inst $rs1, $rs2)>;
multiclass PatSetCC_m<SDPatternOperator OpNode, CondCode Cond,
                      RVInst Inst, ExtInfo Ext> {
  let Predicates = Ext.Predicates in
  def Ext.Suffix : PatSetCC<Ext.PrimaryTy, OpNode, Cond,
                            !cast<RVInst>(Inst#Ext.Suffix), Ext.PrimaryVT>;
}

class PatFprFpr<SDPatternOperator OpNode, RVInstR Inst,
                DAGOperand RegTy, ValueType vt>
    : Pat<(OpNode (vt RegTy:$rs1), (vt RegTy:$rs2)), (Inst $rs1, $rs2)>;
multiclass PatFprFpr_m<SDPatternOperator OpNode, RVInstR Inst,
                       ExtInfo Ext> {
  let Predicates = Ext.Predicates in
  def Ext.Suffix : PatFprFpr<OpNode, !cast<RVInstR>(Inst#Ext.Suffix),
                             Ext.PrimaryTy, Ext.PrimaryVT>;
}

class PatFprFprDynFrm<SDPatternOperator OpNode, RVInstRFrm Inst,
                      DAGOperand RegTy, ValueType vt>
    : Pat<(OpNode (vt RegTy:$rs1), (vt RegTy:$rs2)), (Inst $rs1, $rs2, FRM_DYN)>;
multiclass PatFprFprDynFrm_m<SDPatternOperator OpNode, RVInstRFrm Inst,
                             ExtInfo Ext> {
  let Predicates = Ext.Predicates in
  def Ext.Suffix : PatFprFprDynFrm<OpNode,
                                   !cast<RVInstRFrm>(Inst#Ext.Suffix),
                                   Ext.PrimaryTy, Ext.PrimaryVT>;
}

/// Float conversion operations

// [u]int32<->float conversion patterns must be gated on IsRV32 or IsRV64, so
// are defined later.

/// Float arithmetic operations
foreach Ext = FExts in {
  defm : PatFprFprDynFrm_m<any_fadd, FADD_S, Ext>;
  defm : PatFprFprDynFrm_m<any_fsub, FSUB_S, Ext>;
  defm : PatFprFprDynFrm_m<any_fmul, FMUL_S, Ext>;
  defm : PatFprFprDynFrm_m<any_fdiv, FDIV_S, Ext>;
}

let Predicates = [HasStdExtF] in {
def : Pat<(any_fsqrt FPR32:$rs1), (FSQRT_S FPR32:$rs1, FRM_DYN)>;

def : Pat<(fneg FPR32:$rs1), (FSGNJN_S $rs1, $rs1)>;
def : Pat<(fabs FPR32:$rs1), (FSGNJX_S $rs1, $rs1)>;

def : Pat<(riscv_fclass FPR32:$rs1), (FCLASS_S $rs1)>;
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
def : Pat<(any_fsqrt FPR32INX:$rs1), (FSQRT_S_INX FPR32INX:$rs1, FRM_DYN)>;

def : Pat<(fneg FPR32INX:$rs1), (FSGNJN_S_INX $rs1, $rs1)>;
def : Pat<(fabs FPR32INX:$rs1), (FSGNJX_S_INX $rs1, $rs1)>;

def : Pat<(riscv_fclass FPR32INX:$rs1), (FCLASS_S_INX $rs1)>;
} // Predicates = [HasStdExtZfinx]

foreach Ext = FExts in
defm : PatFprFpr_m<fcopysign, FSGNJ_S, Ext>;

let Predicates = [HasStdExtF] in {
def : Pat<(fcopysign FPR32:$rs1, (fneg FPR32:$rs2)), (FSGNJN_S $rs1, $rs2)>;

// fmadd: rs1 * rs2 + rs3
def : Pat<(any_fma FPR32:$rs1, FPR32:$rs2, FPR32:$rs3),
          (FMADD_S $rs1, $rs2, $rs3, FRM_DYN)>;

// fmsub: rs1 * rs2 - rs3
def : Pat<(any_fma FPR32:$rs1, FPR32:$rs2, (fneg FPR32:$rs3)),
          (FMSUB_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, FRM_DYN)>;

// fnmsub: -rs1 * rs2 + rs3
def : Pat<(any_fma (fneg FPR32:$rs1), FPR32:$rs2, FPR32:$rs3),
          (FNMSUB_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, FRM_DYN)>;

// fnmadd: -rs1 * rs2 - rs3
def : Pat<(any_fma (fneg FPR32:$rs1), FPR32:$rs2, (fneg FPR32:$rs3)),
          (FNMADD_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, FRM_DYN)>;

// fnmadd: -(rs1 * rs2 + rs3) (the nsz flag on the FMA)
def : Pat<(fneg (any_fma_nsz FPR32:$rs1, FPR32:$rs2, FPR32:$rs3)),
          (FNMADD_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, FRM_DYN)>;
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
def : Pat<(fcopysign FPR32INX:$rs1, (fneg FPR32INX:$rs2)), (FSGNJN_S_INX $rs1, $rs2)>;

// fmadd: rs1 * rs2 + rs3
def : Pat<(any_fma FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3),
          (FMADD_S_INX $rs1, $rs2, $rs3, FRM_DYN)>;

// fmsub: rs1 * rs2 - rs3
def : Pat<(any_fma FPR32INX:$rs1, FPR32INX:$rs2, (fneg FPR32INX:$rs3)),
          (FMSUB_S_INX FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3, FRM_DYN)>;

// fnmsub: -rs1 * rs2 + rs3
def : Pat<(any_fma (fneg FPR32INX:$rs1), FPR32INX:$rs2, FPR32INX:$rs3),
          (FNMSUB_S_INX FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3, FRM_DYN)>;

// fnmadd: -rs1 * rs2 - rs3
def : Pat<(any_fma (fneg FPR32INX:$rs1), FPR32INX:$rs2, (fneg FPR32INX:$rs3)),
          (FNMADD_S_INX FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3, FRM_DYN)>;

// fnmadd: -(rs1 * rs2 + rs3) (the nsz flag on the FMA)
def : Pat<(fneg (any_fma_nsz FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3)),
          (FNMADD_S_INX FPR32INX:$rs1, FPR32INX:$rs2, FPR32INX:$rs3, FRM_DYN)>;
} // Predicates = [HasStdExtZfinx]

// The ratified 20191213 ISA spec defines fmin and fmax in a way that matches
// LLVM's fminnum and fmaxnum
// <https://github.com/riscv/riscv-isa-manual/commit/cd20cee7efd9bac7c5aa127ec3b451749d2b3cce>.
foreach Ext = FExts in {
  defm : PatFprFpr_m<fminnum, FMIN_S, Ext>;
  defm : PatFprFpr_m<fmaxnum, FMAX_S, Ext>;
  defm : PatFprFpr_m<riscv_fmin, FMIN_S, Ext>;
  defm : PatFprFpr_m<riscv_fmax, FMAX_S, Ext>;
}

/// Setcc
// FIXME: SETEQ/SETLT/SETLE imply nonans, can we pick better instructions for
// strict versions of those.

// Match non-signaling FEQ_S
foreach Ext = FExts in {
  defm : PatSetCC_m<any_fsetcc,    SETEQ,  FEQ_S,            Ext>;
  defm : PatSetCC_m<any_fsetcc,    SETOEQ, FEQ_S,            Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETLT,  PseudoQuietFLT_S, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETOLT, PseudoQuietFLT_S, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETLE,  PseudoQuietFLE_S, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETOLE, PseudoQuietFLE_S, Ext>;
}

let Predicates = [HasStdExtF] in {
// Match signaling FEQ_S
def : Pat<(XLenVT (strict_fsetccs FPR32:$rs1, FPR32:$rs2, SETEQ)),
          (AND (XLenVT (FLE_S $rs1, $rs2)),
               (XLenVT (FLE_S $rs2, $rs1)))>;
def : Pat<(XLenVT (strict_fsetccs FPR32:$rs1, FPR32:$rs2, SETOEQ)),
          (AND (XLenVT (FLE_S $rs1, $rs2)),
               (XLenVT (FLE_S $rs2, $rs1)))>;
// If both operands are the same, use a single FLE.
def : Pat<(XLenVT (strict_fsetccs FPR32:$rs1, FPR32:$rs1, SETEQ)),
          (FLE_S $rs1, $rs1)>;
def : Pat<(XLenVT (strict_fsetccs FPR32:$rs1, FPR32:$rs1, SETOEQ)),
          (FLE_S $rs1, $rs1)>;
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
// Match signaling FEQ_S
def : Pat<(XLenVT (strict_fsetccs FPR32INX:$rs1, FPR32INX:$rs2, SETEQ)),
          (AND (XLenVT (FLE_S_INX $rs1, $rs2)),
               (XLenVT (FLE_S_INX $rs2, $rs1)))>;
def : Pat<(XLenVT (strict_fsetccs FPR32INX:$rs1, FPR32INX:$rs2, SETOEQ)),
          (AND (XLenVT (FLE_S_INX $rs1, $rs2)),
               (XLenVT (FLE_S_INX $rs2, $rs1)))>;
// If both operands are the same, use a single FLE.
def : Pat<(XLenVT (strict_fsetccs FPR32INX:$rs1, FPR32INX:$rs1, SETEQ)),
          (FLE_S_INX $rs1, $rs1)>;
def : Pat<(XLenVT (strict_fsetccs FPR32INX:$rs1, FPR32INX:$rs1, SETOEQ)),
          (FLE_S_INX $rs1, $rs1)>;
} // Predicates = [HasStdExtZfinx]

foreach Ext = FExts in {
  defm : PatSetCC_m<any_fsetccs, SETLT,  FLT_S, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETOLT, FLT_S, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETLE,  FLE_S, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETOLE, FLE_S, Ext>;
}

let Predicates = [HasStdExtF] in {
defm Select_FPR32 : SelectCC_GPR_rrirr<FPR32, f32>;

def PseudoFROUND_S : PseudoFROUND<FPR32, f32>;

/// Loads

def : LdPat<load, FLW, f32>;

/// Stores

def : StPat<store, FSW, FPR32, f32>;

} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
defm Select_FPR32INX : SelectCC_GPR_rrirr<FPR32INX, f32>;

def PseudoFROUND_S_INX : PseudoFROUND<FPR32INX, f32>;

/// Loads
def : Pat<(f32 (load (AddrRegImm (XLenVT GPR:$rs1), simm12:$imm12))),
          (COPY_TO_REGCLASS (LW GPR:$rs1, simm12:$imm12), GPRF32)>;

/// Stores
def : Pat<(store (f32 FPR32INX:$rs2), (AddrRegImm (XLenVT GPR:$rs1), simm12:$imm12)),
          (SW (COPY_TO_REGCLASS FPR32INX:$rs2, GPR), GPR:$rs1, simm12:$imm12)>;
} // Predicates = [HasStdExtZfinx]

let Predicates = [HasStdExtF] in {
// Moves (no conversion)
def : Pat<(bitconvert (i32 GPR:$rs1)), (FMV_W_X GPR:$rs1)>;
def : Pat<(i32 (bitconvert FPR32:$rs1)), (FMV_X_W FPR32:$rs1)>;
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
// Moves (no conversion)
def : Pat<(f32 (bitconvert (i32 GPR:$rs1))), (COPY_TO_REGCLASS GPR:$rs1, GPRF32)>;
def : Pat<(i32 (bitconvert FPR32INX:$rs1)), (COPY_TO_REGCLASS FPR32INX:$rs1, GPR)>;
} // Predicates = [HasStdExtZfinx]

let Predicates = [HasStdExtF] in {
// float->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint FPR32:$rs1)), (FCVT_W_S $rs1, FRM_RTZ)>;
def : Pat<(i32 (any_fp_to_uint FPR32:$rs1)), (FCVT_WU_S $rs1, FRM_RTZ)>;

// Saturating float->[u]int32.
def : Pat<(i32 (riscv_fcvt_x FPR32:$rs1, timm:$frm)), (FCVT_W_S $rs1, timm:$frm)>;
def : Pat<(i32 (riscv_fcvt_xu FPR32:$rs1, timm:$frm)), (FCVT_WU_S $rs1, timm:$frm)>;

// float->int32 with current rounding mode.
def : Pat<(i32 (any_lrint FPR32:$rs1)), (FCVT_W_S $rs1, FRM_DYN)>;

// float->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround FPR32:$rs1)), (FCVT_W_S $rs1, FRM_RMM)>;

// [u]int->float. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i32 GPR:$rs1)), (FCVT_S_W $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i32 GPR:$rs1)), (FCVT_S_WU $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtZfinx] in {
// float->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint FPR32INX:$rs1)), (FCVT_W_S_INX $rs1, FRM_RTZ)>;
def : Pat<(i32 (any_fp_to_uint FPR32INX:$rs1)), (FCVT_WU_S_INX $rs1, FRM_RTZ)>;

// Saturating float->[u]int32.
def : Pat<(i32 (riscv_fcvt_x FPR32INX:$rs1, timm:$frm)), (FCVT_W_S_INX $rs1, timm:$frm)>;
def : Pat<(i32 (riscv_fcvt_xu FPR32INX:$rs1, timm:$frm)), (FCVT_WU_S_INX $rs1, timm:$frm)>;

// float->int32 with current rounding mode.
def : Pat<(i32 (any_lrint FPR32INX:$rs1)), (FCVT_W_S_INX $rs1, FRM_DYN)>;

// float->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround FPR32INX:$rs1)), (FCVT_W_S_INX $rs1, FRM_RMM)>;

// [u]int->float. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i32 GPR:$rs1)), (FCVT_S_W_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i32 GPR:$rs1)), (FCVT_S_WU_INX $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZfinx]

let Predicates = [HasStdExtF, IsRV64] in {
// Moves (no conversion)
def : Pat<(riscv_fmv_w_x_rv64 GPR:$src), (FMV_W_X GPR:$src)>;
def : Pat<(riscv_fmv_x_anyextw_rv64 FPR32:$src), (FMV_X_W FPR32:$src)>;

// Use target specific isd nodes to help us remember the result is sign
// extended. Matching sext_inreg+fptoui/fptosi may cause the conversion to be
// duplicated if it has another user that didn't need the sign_extend.
def : Pat<(riscv_any_fcvt_w_rv64 FPR32:$rs1, timm:$frm),  (FCVT_W_S $rs1, timm:$frm)>;
def : Pat<(riscv_any_fcvt_wu_rv64 FPR32:$rs1, timm:$frm), (FCVT_WU_S $rs1, timm:$frm)>;

// float->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint FPR32:$rs1)), (FCVT_L_S $rs1, FRM_RTZ)>;
def : Pat<(i64 (any_fp_to_uint FPR32:$rs1)), (FCVT_LU_S $rs1, FRM_RTZ)>;

// Saturating float->[u]int64.
def : Pat<(i64 (riscv_fcvt_x FPR32:$rs1, timm:$frm)), (FCVT_L_S $rs1, timm:$frm)>;
def : Pat<(i64 (riscv_fcvt_xu FPR32:$rs1, timm:$frm)), (FCVT_LU_S $rs1, timm:$frm)>;

// float->int64 with current rounding mode.
def : Pat<(i64 (any_lrint FPR32:$rs1)), (FCVT_L_S $rs1, FRM_DYN)>;
def : Pat<(i64 (any_llrint FPR32:$rs1)), (FCVT_L_S $rs1, FRM_DYN)>;

// float->int64 rounded to neartest with ties rounded away from zero.
def : Pat<(i64 (any_lround FPR32:$rs1)), (FCVT_L_S $rs1, FRM_RMM)>;
def : Pat<(i64 (any_llround FPR32:$rs1)), (FCVT_L_S $rs1, FRM_RMM)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i64 (sexti32 (i64 GPR:$rs1)))), (FCVT_S_W $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 (zexti32 (i64 GPR:$rs1)))), (FCVT_S_WU $rs1, FRM_DYN)>;
def : Pat<(any_sint_to_fp (i64 GPR:$rs1)), (FCVT_S_L $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 GPR:$rs1)), (FCVT_S_LU $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtF, IsRV64]

let Predicates = [HasStdExtZfinx, IsRV64] in {
// Moves (no conversion)
def : Pat<(riscv_fmv_w_x_rv64 GPR:$src), (COPY_TO_REGCLASS GPR:$src, GPRF32)>;
def : Pat<(riscv_fmv_x_anyextw_rv64 GPRF32:$src), (COPY_TO_REGCLASS GPRF32:$src, GPR)>;

// Use target specific isd nodes to help us remember the result is sign
// extended. Matching sext_inreg+fptoui/fptosi may cause the conversion to be
// duplicated if it has another user that didn't need the sign_extend.
def : Pat<(riscv_any_fcvt_w_rv64 FPR32INX:$rs1, timm:$frm),  (FCVT_W_S_INX $rs1, timm:$frm)>;
def : Pat<(riscv_any_fcvt_wu_rv64 FPR32INX:$rs1, timm:$frm), (FCVT_WU_S_INX $rs1, timm:$frm)>;

// float->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint FPR32INX:$rs1)), (FCVT_L_S_INX $rs1, FRM_RTZ)>;
def : Pat<(i64 (any_fp_to_uint FPR32INX:$rs1)), (FCVT_LU_S_INX $rs1, FRM_RTZ)>;

// Saturating float->[u]int64.
def : Pat<(i64 (riscv_fcvt_x FPR32INX:$rs1, timm:$frm)), (FCVT_L_S_INX $rs1, timm:$frm)>;
def : Pat<(i64 (riscv_fcvt_xu FPR32INX:$rs1, timm:$frm)), (FCVT_LU_S_INX $rs1, timm:$frm)>;

// float->int64 with current rounding mode.
def : Pat<(i64 (any_lrint FPR32INX:$rs1)), (FCVT_L_S_INX $rs1, FRM_DYN)>;
def : Pat<(i64 (any_llrint FPR32INX:$rs1)), (FCVT_L_S_INX $rs1, FRM_DYN)>;

// float->int64 rounded to neartest with ties rounded away from zero.
def : Pat<(i64 (any_lround FPR32INX:$rs1)), (FCVT_L_S_INX $rs1, FRM_DYN)>;
def : Pat<(i64 (any_llround FPR32INX:$rs1)), (FCVT_L_S_INX $rs1, FRM_DYN)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i64 (sexti32 (i64 GPR:$rs1)))), (FCVT_S_W_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 (zexti32 (i64 GPR:$rs1)))), (FCVT_S_WU_INX $rs1, FRM_DYN)>;
def : Pat<(any_sint_to_fp (i64 GPR:$rs1)), (FCVT_S_L_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 GPR:$rs1)), (FCVT_S_LU_INX $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZfinx, IsRV64]
