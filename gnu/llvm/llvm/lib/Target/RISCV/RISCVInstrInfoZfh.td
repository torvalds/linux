//===-- RISCVInstrInfoZfh.td - RISC-V 'Zfh' instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'Zfh'
// half-precision floating-point extension, version 1.0.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RISC-V specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_RISCVFMV_H_X
    : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisVT<1, XLenVT>]>;
def SDT_RISCVFMV_X_EXTH
    : SDTypeProfile<1, 1, [SDTCisVT<0, XLenVT>, SDTCisFP<1>]>;

def riscv_fmv_h_x
    : SDNode<"RISCVISD::FMV_H_X", SDT_RISCVFMV_H_X>;
def riscv_fmv_x_anyexth
    : SDNode<"RISCVISD::FMV_X_ANYEXTH", SDT_RISCVFMV_X_EXTH>;
def riscv_fmv_x_signexth
    : SDNode<"RISCVISD::FMV_X_SIGNEXTH", SDT_RISCVFMV_X_EXTH>;

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

// Zhinxmin and Zhinx

def FPR16INX : RegisterOperand<GPRF16> {
  let ParserMatchClass = GPRAsFPR;
  let DecoderMethod = "DecodeGPRRegisterClass";
}

def ZfhExt     : ExtInfo<"", "", [HasStdExtZfh],
                         f16, FPR16, FPR32, ?, FPR16>;
def ZfhminExt  : ExtInfo<"", "", [HasStdExtZfhmin],
                         f16, FPR16, FPR32, ?, FPR16>;
def ZfhDExt    : ExtInfo<"", "", [HasStdExtZfh, HasStdExtD],
                         ?, ?, FPR32, FPR64, FPR16>;
def ZfhminDExt : ExtInfo<"", "", [HasStdExtZfhmin, HasStdExtD],
                         ?, ?, FPR32, FPR64, FPR16>;

def ZhinxExt            : ExtInfo<"_INX", "RVZfinx",
                                  [HasStdExtZhinx],
                                  f16, FPR16INX, FPR32INX, ?, FPR16INX>;
def ZhinxminExt         : ExtInfo<"_INX", "RVZfinx",
                                  [HasStdExtZhinxmin],
                                  f16, FPR16INX, FPR32INX, ?, FPR16INX>;
def ZhinxZdinxExt       : ExtInfo<"_INX", "RVZfinx",
                                  [HasStdExtZhinx, HasStdExtZdinx, IsRV64],
                                  ?, ?, FPR32INX, FPR64INX, FPR16INX>;
def ZhinxminZdinxExt    : ExtInfo<"_INX", "RVZfinx",
                                  [HasStdExtZhinxmin, HasStdExtZdinx, IsRV64],
                                  ?, ?, FPR32INX, FPR64INX, FPR16INX>;
def ZhinxZdinx32Ext     : ExtInfo<"_IN32X", "RV32Zdinx",
                                  [HasStdExtZhinx, HasStdExtZdinx, IsRV32],
                                  ?, ?, FPR32INX, FPR64IN32X, FPR16INX >;
def ZhinxminZdinx32Ext  : ExtInfo<"_IN32X", "RV32Zdinx",
                                  [HasStdExtZhinxmin, HasStdExtZdinx, IsRV32],
                                  ?, ?, FPR32INX, FPR64IN32X, FPR16INX>;

defvar ZfhExts = [ZfhExt, ZhinxExt];
defvar ZfhminExts = [ZfhminExt, ZhinxminExt];
defvar ZfhDExts = [ZfhDExt, ZhinxZdinxExt, ZhinxZdinx32Ext];
defvar ZfhminDExts = [ZfhminDExt, ZhinxminZdinxExt, ZhinxminZdinx32Ext];

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasHalfFPLoadStoreMove] in {
def FLH : FPLoad_r<0b001, "flh", FPR16, WriteFLD16>;

// Operands for stores are in the order srcreg, base, offset rather than
// reflecting the order these fields are specified in the instruction
// encoding.
def FSH : FPStore_r<0b001, "fsh", FPR16, WriteFST16>;
} // Predicates = [HasHalfFPLoadStoreMove]

foreach Ext = ZfhExts in {
  let SchedRW = [WriteFMA16, ReadFMA16, ReadFMA16, ReadFMA16Addend] in {
    defm FMADD_H  : FPFMA_rrr_frm_m<OPC_MADD,  0b10, "fmadd.h",  Ext>;
    defm FMSUB_H  : FPFMA_rrr_frm_m<OPC_MSUB,  0b10, "fmsub.h",  Ext>;
    defm FNMSUB_H : FPFMA_rrr_frm_m<OPC_NMSUB, 0b10, "fnmsub.h", Ext>;
    defm FNMADD_H : FPFMA_rrr_frm_m<OPC_NMADD, 0b10, "fnmadd.h", Ext>;
  }

  let SchedRW = [WriteFAdd16, ReadFAdd16, ReadFAdd16] in {
    defm FADD_H : FPALU_rr_frm_m<0b0000010, "fadd.h", Ext, Commutable=1>;
    defm FSUB_H : FPALU_rr_frm_m<0b0000110, "fsub.h", Ext>;
  }
  let SchedRW = [WriteFMul16, ReadFMul16, ReadFMul16] in
  defm FMUL_H : FPALU_rr_frm_m<0b0001010, "fmul.h", Ext, Commutable=1>;

  let SchedRW = [WriteFDiv16, ReadFDiv16, ReadFDiv16] in
  defm FDIV_H : FPALU_rr_frm_m<0b0001110, "fdiv.h", Ext>;

  defm FSQRT_H : FPUnaryOp_r_frm_m<0b0101110, 0b00000, Ext, Ext.PrimaryTy,
                                   Ext.PrimaryTy, "fsqrt.h">,
                 Sched<[WriteFSqrt16, ReadFSqrt16]>;

  let SchedRW = [WriteFSGNJ16, ReadFSGNJ16, ReadFSGNJ16],
      mayRaiseFPException = 0 in {
    defm FSGNJ_H  : FPALU_rr_m<0b0010010, 0b000, "fsgnj.h",  Ext>;
    defm FSGNJN_H : FPALU_rr_m<0b0010010, 0b001, "fsgnjn.h", Ext>;
    defm FSGNJX_H : FPALU_rr_m<0b0010010, 0b010, "fsgnjx.h", Ext>;
  }

  let SchedRW = [WriteFMinMax16, ReadFMinMax16, ReadFMinMax16] in {
    defm FMIN_H   : FPALU_rr_m<0b0010110, 0b000, "fmin.h", Ext, Commutable=1>;
    defm FMAX_H   : FPALU_rr_m<0b0010110, 0b001, "fmax.h", Ext, Commutable=1>;
  }

  let IsSignExtendingOpW = 1 in
  defm FCVT_W_H : FPUnaryOp_r_frm_m<0b1100010, 0b00000, Ext, GPR, Ext.PrimaryTy,
                                    "fcvt.w.h">,
                  Sched<[WriteFCvtF16ToI32, ReadFCvtF16ToI32]>;

  let IsSignExtendingOpW = 1 in
  defm FCVT_WU_H : FPUnaryOp_r_frm_m<0b1100010, 0b00001, Ext, GPR, Ext.PrimaryTy,
                                     "fcvt.wu.h">,
                   Sched<[WriteFCvtF16ToI32, ReadFCvtF16ToI32]>;

  defm FCVT_H_W : FPUnaryOp_r_frm_m<0b1101010, 0b00000, Ext, Ext.PrimaryTy, GPR,
                                    "fcvt.h.w">,
                  Sched<[WriteFCvtI32ToF16, ReadFCvtI32ToF16]>;

  defm FCVT_H_WU : FPUnaryOp_r_frm_m<0b1101010, 0b00001, Ext, Ext.PrimaryTy, GPR,
                                     "fcvt.h.wu">,
                   Sched<[WriteFCvtI32ToF16, ReadFCvtI32ToF16]>;
} // foreach Ext = ZfhExts

foreach Ext = ZfhminExts in {
  defm FCVT_H_S : FPUnaryOp_r_frm_m<0b0100010, 0b00000, Ext, Ext.PrimaryTy,
                                    Ext.F32Ty, "fcvt.h.s">,
                  Sched<[WriteFCvtF32ToF16, ReadFCvtF32ToF16]>;

  defm FCVT_S_H : FPUnaryOp_r_frmlegacy_m<0b0100000, 0b00010,Ext, Ext.F32Ty,
                                          Ext.PrimaryTy, "fcvt.s.h">,
                 Sched<[WriteFCvtF16ToF32, ReadFCvtF16ToF32]>;
} // foreach Ext = ZfhminExts

let Predicates = [HasHalfFPLoadStoreMove] in {
let mayRaiseFPException = 0, IsSignExtendingOpW = 1 in
def FMV_X_H : FPUnaryOp_r<0b1110010, 0b00000, 0b000, GPR, FPR16, "fmv.x.h">,
              Sched<[WriteFMovF16ToI16, ReadFMovF16ToI16]>;

let mayRaiseFPException = 0 in
def FMV_H_X : FPUnaryOp_r<0b1111010, 0b00000, 0b000, FPR16, GPR, "fmv.h.x">,
              Sched<[WriteFMovI16ToF16, ReadFMovI16ToF16]>;
} // Predicates = [HasHalfFPLoadStoreMove]

foreach Ext = ZfhExts in {
  let SchedRW = [WriteFCmp16, ReadFCmp16, ReadFCmp16] in {
    defm FEQ_H : FPCmp_rr_m<0b1010010, 0b010, "feq.h", Ext, Commutable=1>;
    defm FLT_H : FPCmp_rr_m<0b1010010, 0b001, "flt.h", Ext>;
    defm FLE_H : FPCmp_rr_m<0b1010010, 0b000, "fle.h", Ext>;
  }

  let mayRaiseFPException = 0 in
  defm FCLASS_H : FPUnaryOp_r_m<0b1110010, 0b00000, 0b001, Ext, GPR, Ext.PrimaryTy,
                                "fclass.h">,
                  Sched<[WriteFClass16, ReadFClass16]>;

  defm FCVT_L_H  : FPUnaryOp_r_frm_m<0b1100010, 0b00010, Ext, GPR, Ext.PrimaryTy,
                                     "fcvt.l.h", [IsRV64]>,
                   Sched<[WriteFCvtF16ToI64, ReadFCvtF16ToI64]>;

  defm FCVT_LU_H  : FPUnaryOp_r_frm_m<0b1100010, 0b00011, Ext, GPR, Ext.PrimaryTy,
                                      "fcvt.lu.h", [IsRV64]>,
                    Sched<[WriteFCvtF16ToI64, ReadFCvtF16ToI64]>;

  defm FCVT_H_L : FPUnaryOp_r_frm_m<0b1101010, 0b00010, Ext, Ext.PrimaryTy, GPR,
                                    "fcvt.h.l", [IsRV64]>,
                  Sched<[WriteFCvtI64ToF16, ReadFCvtI64ToF16]>;

  defm FCVT_H_LU : FPUnaryOp_r_frm_m<0b1101010, 0b00011, Ext, Ext.PrimaryTy, GPR,
                                     "fcvt.h.lu", [IsRV64]>,
                   Sched<[WriteFCvtI64ToF16, ReadFCvtI64ToF16]>;
} // foreach Ext = ZfhExts

foreach Ext = ZfhminDExts in {
  defm FCVT_H_D : FPUnaryOp_r_frm_m<0b0100010, 0b00001, Ext, Ext.F16Ty,
                                   Ext.F64Ty, "fcvt.h.d">,
                  Sched<[WriteFCvtF64ToF16, ReadFCvtF64ToF16]>;

  defm FCVT_D_H : FPUnaryOp_r_frmlegacy_m<0b0100001, 0b00010, Ext, Ext.F64Ty,
                                          Ext.F16Ty, "fcvt.d.h">,
                  Sched<[WriteFCvtF16ToF64, ReadFCvtF16ToF64]>;
} // foreach Ext = ZfhminDExts

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZfhmin] in {
def : InstAlias<"flh $rd, (${rs1})",  (FLH FPR16:$rd,  GPR:$rs1, 0), 0>;
def : InstAlias<"fsh $rs2, (${rs1})", (FSH FPR16:$rs2, GPR:$rs1, 0), 0>;
} // Predicates = [HasStdExtZfhmin]

let Predicates = [HasStdExtZfh] in {
def : InstAlias<"fmv.h $rd, $rs",  (FSGNJ_H  FPR16:$rd, FPR16:$rs, FPR16:$rs)>;
def : InstAlias<"fabs.h $rd, $rs", (FSGNJX_H FPR16:$rd, FPR16:$rs, FPR16:$rs)>;
def : InstAlias<"fneg.h $rd, $rs", (FSGNJN_H FPR16:$rd, FPR16:$rs, FPR16:$rs)>;

// fgt.h/fge.h are recognised by the GNU assembler but the canonical
// flt.h/fle.h forms will always be printed. Therefore, set a zero weight.
def : InstAlias<"fgt.h $rd, $rs, $rt",
                (FLT_H GPR:$rd, FPR16:$rt, FPR16:$rs), 0>;
def : InstAlias<"fge.h $rd, $rs, $rt",
                (FLE_H GPR:$rd, FPR16:$rt, FPR16:$rs), 0>;

let usesCustomInserter = 1 in {
def PseudoQuietFLE_H : PseudoQuietFCMP<FPR16>;
def PseudoQuietFLT_H : PseudoQuietFCMP<FPR16>;
}
} // Predicates = [HasStdExtZfh]

let Predicates = [HasStdExtZfhmin] in {
def PseudoFLH  : PseudoFloatLoad<"flh", FPR16>;
def PseudoFSH  : PseudoStore<"fsh", FPR16>;
} // Predicates = [HasStdExtZfhmin]

let Predicates = [HasStdExtZhinx] in {
def : InstAlias<"fmv.h $rd, $rs",  (FSGNJ_H_INX  FPR16INX:$rd, FPR16INX:$rs, FPR16INX:$rs)>;
def : InstAlias<"fabs.h $rd, $rs", (FSGNJX_H_INX FPR16INX:$rd, FPR16INX:$rs, FPR16INX:$rs)>;
def : InstAlias<"fneg.h $rd, $rs", (FSGNJN_H_INX FPR16INX:$rd, FPR16INX:$rs, FPR16INX:$rs)>;

def : InstAlias<"fgt.h $rd, $rs, $rt",
                (FLT_H_INX GPR:$rd, FPR16INX:$rt, FPR16INX:$rs), 0>;
def : InstAlias<"fge.h $rd, $rs, $rt",
                (FLE_H_INX GPR:$rd, FPR16INX:$rt, FPR16INX:$rs), 0>;

let usesCustomInserter = 1 in {
def PseudoQuietFLE_H_INX : PseudoQuietFCMP<FPR16INX>;
def PseudoQuietFLT_H_INX : PseudoQuietFCMP<FPR16INX>;
}
} // Predicates = [HasStdExtZhinxmin]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//


/// Float conversion operations

// [u]int32<->float conversion patterns must be gated on IsRV32 or IsRV64, so
// are defined later.

/// Float arithmetic operations

foreach Ext = ZfhExts in {
  defm : PatFprFprDynFrm_m<any_fadd, FADD_H, Ext>;
  defm : PatFprFprDynFrm_m<any_fsub, FSUB_H, Ext>;
  defm : PatFprFprDynFrm_m<any_fmul, FMUL_H, Ext>;
  defm : PatFprFprDynFrm_m<any_fdiv, FDIV_H, Ext>;
}

let Predicates = [HasStdExtZfh] in {
def : Pat<(f16 (any_fsqrt FPR16:$rs1)), (FSQRT_H FPR16:$rs1, FRM_DYN)>;

def : Pat<(f16 (fneg FPR16:$rs1)), (FSGNJN_H $rs1, $rs1)>;
def : Pat<(f16 (fabs FPR16:$rs1)), (FSGNJX_H $rs1, $rs1)>;

def : Pat<(riscv_fclass (f16 FPR16:$rs1)), (FCLASS_H $rs1)>;

def : PatFprFpr<fcopysign, FSGNJ_H, FPR16, f16>;
def : Pat<(f16 (fcopysign FPR16:$rs1, (f16 (fneg FPR16:$rs2)))), (FSGNJN_H $rs1, $rs2)>;
def : Pat<(f16 (fcopysign FPR16:$rs1, FPR32:$rs2)),
          (FSGNJ_H $rs1, (FCVT_H_S $rs2, FRM_DYN))>;

// fmadd: rs1 * rs2 + rs3
def : Pat<(f16 (any_fma FPR16:$rs1, FPR16:$rs2, FPR16:$rs3)),
          (FMADD_H $rs1, $rs2, $rs3, FRM_DYN)>;

// fmsub: rs1 * rs2 - rs3
def : Pat<(f16 (any_fma FPR16:$rs1, FPR16:$rs2, (fneg FPR16:$rs3))),
          (FMSUB_H FPR16:$rs1, FPR16:$rs2, FPR16:$rs3, FRM_DYN)>;

// fnmsub: -rs1 * rs2 + rs3
def : Pat<(f16 (any_fma (fneg FPR16:$rs1), FPR16:$rs2, FPR16:$rs3)),
          (FNMSUB_H FPR16:$rs1, FPR16:$rs2, FPR16:$rs3, FRM_DYN)>;

// fnmadd: -rs1 * rs2 - rs3
def : Pat<(f16 (any_fma (fneg FPR16:$rs1), FPR16:$rs2, (fneg FPR16:$rs3))),
          (FNMADD_H FPR16:$rs1, FPR16:$rs2, FPR16:$rs3, FRM_DYN)>;

// fnmadd: -(rs1 * rs2 + rs3) (the nsz flag on the FMA)
def : Pat<(f16 (fneg (any_fma_nsz FPR16:$rs1, FPR16:$rs2, FPR16:$rs3))),
          (FNMADD_H FPR16:$rs1, FPR16:$rs2, FPR16:$rs3, FRM_DYN)>;
} // Predicates = [HasStdExtZfh]

let Predicates = [HasStdExtZhinx] in {

/// Float conversion operations

// [u]int32<->float conversion patterns must be gated on IsRV32 or IsRV64, so
// are defined later.

/// Float arithmetic operations

def : Pat<(any_fsqrt FPR16INX:$rs1), (FSQRT_H_INX FPR16INX:$rs1, FRM_DYN)>;

def : Pat<(fneg FPR16INX:$rs1), (FSGNJN_H_INX $rs1, $rs1)>;
def : Pat<(fabs FPR16INX:$rs1), (FSGNJX_H_INX $rs1, $rs1)>;

def : Pat<(riscv_fclass FPR16INX:$rs1), (FCLASS_H_INX $rs1)>;

def : PatFprFpr<fcopysign, FSGNJ_H_INX, FPR16INX, f16>;
def : Pat<(fcopysign FPR16INX:$rs1, (fneg FPR16INX:$rs2)), (FSGNJN_H_INX $rs1, $rs2)>;
def : Pat<(fcopysign FPR16INX:$rs1, FPR32INX:$rs2),
          (FSGNJ_H_INX $rs1, (FCVT_H_S_INX $rs2, FRM_DYN))>;

// fmadd: rs1 * rs2 + rs3
def : Pat<(any_fma FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3),
          (FMADD_H_INX $rs1, $rs2, $rs3, FRM_DYN)>;

// fmsub: rs1 * rs2 - rs3
def : Pat<(any_fma FPR16INX:$rs1, FPR16INX:$rs2, (fneg FPR16INX:$rs3)),
          (FMSUB_H_INX FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3, FRM_DYN)>;

// fnmsub: -rs1 * rs2 + rs3
def : Pat<(any_fma (fneg FPR16INX:$rs1), FPR16INX:$rs2, FPR16INX:$rs3),
          (FNMSUB_H_INX FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3, FRM_DYN)>;

// fnmadd: -rs1 * rs2 - rs3
def : Pat<(any_fma (fneg FPR16INX:$rs1), FPR16INX:$rs2, (fneg FPR16INX:$rs3)),
          (FNMADD_H_INX FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3, FRM_DYN)>;

// fnmadd: -(rs1 * rs2 + rs3) (the nsz flag on the FMA)
def : Pat<(fneg (any_fma_nsz FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3)),
          (FNMADD_H_INX FPR16INX:$rs1, FPR16INX:$rs2, FPR16INX:$rs3, FRM_DYN)>;
} // Predicates = [HasStdExtZhinx]

// The ratified 20191213 ISA spec defines fmin and fmax in a way that matches
// LLVM's fminnum and fmaxnum
// <https://github.com/riscv/riscv-isa-manual/commit/cd20cee7efd9bac7c5aa127ec3b451749d2b3cce>.
foreach Ext = ZfhExts in {
  defm : PatFprFpr_m<fminnum, FMIN_H, Ext>;
  defm : PatFprFpr_m<fmaxnum, FMAX_H, Ext>;
  defm : PatFprFpr_m<riscv_fmin, FMIN_H, Ext>;
  defm : PatFprFpr_m<riscv_fmax, FMAX_H, Ext>;
}

/// Setcc
// FIXME: SETEQ/SETLT/SETLE imply nonans, can we pick better instructions for
// strict versions of those.

// Match non-signaling FEQ_D
foreach Ext = ZfhExts in {
  defm : PatSetCC_m<any_fsetcc,    SETEQ,  FEQ_H,            Ext>;
  defm : PatSetCC_m<any_fsetcc,    SETOEQ, FEQ_H,            Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETLT,  PseudoQuietFLT_H, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETOLT, PseudoQuietFLT_H, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETLE,  PseudoQuietFLE_H, Ext>;
  defm : PatSetCC_m<strict_fsetcc, SETOLE, PseudoQuietFLE_H, Ext>;
}

let Predicates = [HasStdExtZfh] in {
// Match signaling FEQ_H
def : Pat<(XLenVT (strict_fsetccs (f16 FPR16:$rs1), FPR16:$rs2, SETEQ)),
          (AND (XLenVT (FLE_H $rs1, $rs2)),
               (XLenVT (FLE_H $rs2, $rs1)))>;
def : Pat<(XLenVT (strict_fsetccs (f16 FPR16:$rs1), FPR16:$rs2, SETOEQ)),
          (AND (XLenVT (FLE_H $rs1, $rs2)),
               (XLenVT (FLE_H $rs2, $rs1)))>;
// If both operands are the same, use a single FLE.
def : Pat<(XLenVT (strict_fsetccs (f16 FPR16:$rs1), (f16 FPR16:$rs1), SETEQ)),
          (FLE_H $rs1, $rs1)>;
def : Pat<(XLenVT (strict_fsetccs (f16 FPR16:$rs1), (f16 FPR16:$rs1), SETOEQ)),
          (FLE_H $rs1, $rs1)>;
} // Predicates = [HasStdExtZfh]

let Predicates = [HasStdExtZhinx] in {
// Match signaling FEQ_H
def : Pat<(XLenVT (strict_fsetccs FPR16INX:$rs1, FPR16INX:$rs2, SETEQ)),
          (AND (XLenVT (FLE_H_INX $rs1, $rs2)),
               (XLenVT (FLE_H_INX $rs2, $rs1)))>;
def : Pat<(XLenVT (strict_fsetccs FPR16INX:$rs1, FPR16INX:$rs2, SETOEQ)),
          (AND (XLenVT (FLE_H_INX $rs1, $rs2)),
               (XLenVT (FLE_H_INX $rs2, $rs1)))>;
// If both operands are the same, use a single FLE.
def : Pat<(XLenVT (strict_fsetccs FPR16INX:$rs1, FPR16INX:$rs1, SETEQ)),
          (FLE_H_INX $rs1, $rs1)>;
def : Pat<(XLenVT (strict_fsetccs FPR16INX:$rs1, FPR16INX:$rs1, SETOEQ)),
          (FLE_H_INX $rs1, $rs1)>;
} // Predicates = [HasStdExtZhinx]

foreach Ext = ZfhExts in {
  defm : PatSetCC_m<any_fsetccs, SETLT,  FLT_H, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETOLT, FLT_H, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETLE,  FLE_H, Ext>;
  defm : PatSetCC_m<any_fsetccs, SETOLE, FLE_H, Ext>;
}

let Predicates = [HasStdExtZfh] in {
defm Select_FPR16 : SelectCC_GPR_rrirr<FPR16, f16>;

def PseudoFROUND_H : PseudoFROUND<FPR16, f16>;
} // Predicates = [HasStdExtZfh]

let Predicates = [HasStdExtZhinx] in {
defm Select_FPR16INX : SelectCC_GPR_rrirr<FPR16INX, f16>;

def PseudoFROUND_H_INX : PseudoFROUND<FPR16INX, f16>;
} // Predicates = [HasStdExtZhinx]

let Predicates = [HasStdExtZfhmin] in {
/// Loads
def : LdPat<load, FLH, f16>;

/// Stores
def : StPat<store, FSH, FPR16, f16>;
} // Predicates = [HasStdExtZfhmin]

let Predicates = [HasStdExtZhinxmin] in {
/// Loads
def : Pat<(f16 (load (AddrRegImm (XLenVT GPR:$rs1), simm12:$imm12))),
          (COPY_TO_REGCLASS (LH GPR:$rs1, simm12:$imm12), GPRF16)>;

/// Stores
def : Pat<(store (f16 FPR16INX:$rs2),
                 (AddrRegImm (XLenVT GPR:$rs1), simm12:$imm12)),
          (SH (COPY_TO_REGCLASS FPR16INX:$rs2, GPR), GPR:$rs1, simm12:$imm12)>;
} // Predicates = [HasStdExtZhinxmin]

let Predicates = [HasStdExtZfhmin] in {
/// Float conversion operations

// f32 -> f16, f16 -> f32
def : Pat<(f16 (any_fpround FPR32:$rs1)), (FCVT_H_S FPR32:$rs1, FRM_DYN)>;
def : Pat<(any_fpextend (f16 FPR16:$rs1)), (FCVT_S_H FPR16:$rs1, FRM_RNE)>;

// Moves (no conversion)
def : Pat<(f16 (riscv_fmv_h_x GPR:$src)), (FMV_H_X GPR:$src)>;
def : Pat<(riscv_fmv_x_anyexth (f16 FPR16:$src)), (FMV_X_H FPR16:$src)>;
def : Pat<(riscv_fmv_x_signexth (f16 FPR16:$src)), (FMV_X_H FPR16:$src)>;

def : Pat<(fcopysign FPR32:$rs1, (f16 FPR16:$rs2)), (FSGNJ_S $rs1, (FCVT_S_H $rs2, FRM_RNE))>;
} // Predicates = [HasStdExtZfhmin]

let Predicates = [HasStdExtZhinxmin] in {
/// Float conversion operations

// f32 -> f16, f16 -> f32
def : Pat<(any_fpround FPR32INX:$rs1), (FCVT_H_S_INX FPR32INX:$rs1, FRM_DYN)>;
def : Pat<(any_fpextend FPR16INX:$rs1), (FCVT_S_H_INX FPR16INX:$rs1, FRM_RNE)>;

// Moves (no conversion)
def : Pat<(f16 (riscv_fmv_h_x GPR:$src)), (COPY_TO_REGCLASS GPR:$src, GPR)>;
def : Pat<(riscv_fmv_x_anyexth FPR16INX:$src), (COPY_TO_REGCLASS FPR16INX:$src, GPR)>;
def : Pat<(riscv_fmv_x_signexth FPR16INX:$src), (COPY_TO_REGCLASS FPR16INX:$src, GPR)>;

def : Pat<(fcopysign FPR32INX:$rs1, FPR16INX:$rs2), (FSGNJ_S_INX $rs1, (FCVT_S_H_INX $rs2, FRM_RNE))>;
} // Predicates = [HasStdExtZhinxmin]

let Predicates = [HasStdExtZfh] in {
// half->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint (f16 FPR16:$rs1))), (FCVT_W_H $rs1, 0b001)>;
def : Pat<(i32 (any_fp_to_uint (f16 FPR16:$rs1))), (FCVT_WU_H $rs1, 0b001)>;

// Saturating half->[u]int32.
def : Pat<(i32 (riscv_fcvt_x (f16 FPR16:$rs1), timm:$frm)), (FCVT_W_H $rs1, timm:$frm)>;
def : Pat<(i32 (riscv_fcvt_xu (f16 FPR16:$rs1), timm:$frm)), (FCVT_WU_H $rs1, timm:$frm)>;

// half->int32 with current rounding mode.
def : Pat<(i32 (any_lrint (f16 FPR16:$rs1))), (FCVT_W_H $rs1, FRM_DYN)>;

// half->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround (f16 FPR16:$rs1))), (FCVT_W_H $rs1, FRM_RMM)>;

// [u]int->half. Match GCC and default to using dynamic rounding mode.
def : Pat<(f16 (any_sint_to_fp (i32 GPR:$rs1))), (FCVT_H_W $rs1, FRM_DYN)>;
def : Pat<(f16 (any_uint_to_fp (i32 GPR:$rs1))), (FCVT_H_WU $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZfh]

let Predicates = [HasStdExtZhinx] in {
// half->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint FPR16INX:$rs1)), (FCVT_W_H_INX $rs1, 0b001)>;
def : Pat<(i32 (any_fp_to_uint FPR16INX:$rs1)), (FCVT_WU_H_INX $rs1, 0b001)>;

// Saturating float->[u]int32.
def : Pat<(i32 (riscv_fcvt_x FPR16INX:$rs1, timm:$frm)), (FCVT_W_H_INX $rs1, timm:$frm)>;
def : Pat<(i32 (riscv_fcvt_xu FPR16INX:$rs1, timm:$frm)), (FCVT_WU_H_INX $rs1, timm:$frm)>;

// half->int32 with current rounding mode.
def : Pat<(i32 (any_lrint FPR16INX:$rs1)), (FCVT_W_H_INX $rs1, FRM_DYN)>;

// half->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround FPR16INX:$rs1)), (FCVT_W_H_INX $rs1, FRM_RMM)>;

// [u]int->half. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i32 GPR:$rs1)), (FCVT_H_W_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i32 GPR:$rs1)), (FCVT_H_WU_INX $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZhinx]

let Predicates = [HasStdExtZfh, IsRV64] in {
// Use target specific isd nodes to help us remember the result is sign
// extended. Matching sext_inreg+fptoui/fptosi may cause the conversion to be
// duplicated if it has another user that didn't need the sign_extend.
def : Pat<(riscv_any_fcvt_w_rv64 (f16 FPR16:$rs1), timm:$frm),  (FCVT_W_H $rs1, timm:$frm)>;
def : Pat<(riscv_any_fcvt_wu_rv64 (f16 FPR16:$rs1), timm:$frm), (FCVT_WU_H $rs1, timm:$frm)>;

// half->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint (f16 FPR16:$rs1))), (FCVT_L_H $rs1, 0b001)>;
def : Pat<(i64 (any_fp_to_uint (f16 FPR16:$rs1))), (FCVT_LU_H $rs1, 0b001)>;

// Saturating half->[u]int64.
def : Pat<(i64 (riscv_fcvt_x (f16 FPR16:$rs1), timm:$frm)), (FCVT_L_H $rs1, timm:$frm)>;
def : Pat<(i64 (riscv_fcvt_xu (f16 FPR16:$rs1), timm:$frm)), (FCVT_LU_H $rs1, timm:$frm)>;

// half->int64 with current rounding mode.
def : Pat<(i64 (any_lrint (f16 FPR16:$rs1))), (FCVT_L_H $rs1, FRM_DYN)>;
def : Pat<(i64 (any_llrint (f16 FPR16:$rs1))), (FCVT_L_H $rs1, FRM_DYN)>;

// half->int64 rounded to nearest with ties rounded away from zero.
def : Pat<(i64 (any_lround (f16 FPR16:$rs1))), (FCVT_L_H $rs1, FRM_RMM)>;
def : Pat<(i64 (any_llround (f16 FPR16:$rs1))), (FCVT_L_H $rs1, FRM_RMM)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(f16 (any_sint_to_fp (i64 (sexti32 (i64 GPR:$rs1))))), (FCVT_H_W $rs1, FRM_DYN)>;
def : Pat<(f16 (any_uint_to_fp (i64 (zexti32 (i64 GPR:$rs1))))), (FCVT_H_WU $rs1, FRM_DYN)>;
def : Pat<(f16 (any_sint_to_fp (i64 GPR:$rs1))), (FCVT_H_L $rs1, FRM_DYN)>;
def : Pat<(f16 (any_uint_to_fp (i64 GPR:$rs1))), (FCVT_H_LU $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZfh, IsRV64]

let Predicates = [HasStdExtZhinx, IsRV64] in {
// Use target specific isd nodes to help us remember the result is sign
// extended. Matching sext_inreg+fptoui/fptosi may cause the conversion to be
// duplicated if it has another user that didn't need the sign_extend.
def : Pat<(riscv_any_fcvt_w_rv64 FPR16INX:$rs1, timm:$frm),  (FCVT_W_H_INX $rs1, timm:$frm)>;
def : Pat<(riscv_any_fcvt_wu_rv64 FPR16INX:$rs1, timm:$frm), (FCVT_WU_H_INX $rs1, timm:$frm)>;

// half->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint FPR16INX:$rs1)), (FCVT_L_H_INX $rs1, 0b001)>;
def : Pat<(i64 (any_fp_to_uint FPR16INX:$rs1)), (FCVT_LU_H_INX $rs1, 0b001)>;

// Saturating float->[u]int64.
def : Pat<(i64 (riscv_fcvt_x FPR16INX:$rs1, timm:$frm)), (FCVT_L_H_INX $rs1, timm:$frm)>;
def : Pat<(i64 (riscv_fcvt_xu FPR16INX:$rs1, timm:$frm)), (FCVT_LU_H_INX $rs1, timm:$frm)>;

// half->int64 with current rounding mode.
def : Pat<(i64 (any_lrint FPR16INX:$rs1)), (FCVT_L_H_INX $rs1, FRM_DYN)>;
def : Pat<(i64 (any_llrint FPR16INX:$rs1)), (FCVT_L_H_INX $rs1, FRM_DYN)>;

// half->int64 rounded to nearest with ties rounded away from zero.
def : Pat<(i64 (any_lround FPR16INX:$rs1)), (FCVT_L_H_INX $rs1, FRM_RMM)>;
def : Pat<(i64 (any_llround FPR16INX:$rs1)), (FCVT_L_H_INX $rs1, FRM_RMM)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i64 (sexti32 (i64 GPR:$rs1)))), (FCVT_H_W_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 (zexti32 (i64 GPR:$rs1)))), (FCVT_H_WU_INX $rs1, FRM_DYN)>;
def : Pat<(any_sint_to_fp (i64 GPR:$rs1)), (FCVT_H_L_INX $rs1, FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 GPR:$rs1)), (FCVT_H_LU_INX $rs1, FRM_DYN)>;
} // Predicates = [HasStdExtZhinx, IsRV64]

let Predicates = [HasStdExtZfhmin, HasStdExtD] in {
/// Float conversion operations
// f64 -> f16, f16 -> f64
def : Pat<(f16 (any_fpround FPR64:$rs1)), (FCVT_H_D FPR64:$rs1, FRM_DYN)>;
def : Pat<(any_fpextend (f16 FPR16:$rs1)), (FCVT_D_H FPR16:$rs1, FRM_RNE)>;

/// Float arithmetic operations
def : Pat<(f16 (fcopysign FPR16:$rs1, FPR64:$rs2)),
          (FSGNJ_H $rs1, (FCVT_H_D $rs2, FRM_DYN))>;
def : Pat<(fcopysign FPR64:$rs1, (f16 FPR16:$rs2)), (FSGNJ_D $rs1, (FCVT_D_H $rs2, FRM_RNE))>;
} // Predicates = [HasStdExtZfhmin, HasStdExtD]

let Predicates = [HasStdExtZhinxmin, HasStdExtZdinx, IsRV32] in {
/// Float conversion operations
// f64 -> f16, f16 -> f64
def : Pat<(any_fpround FPR64IN32X:$rs1), (FCVT_H_D_IN32X FPR64IN32X:$rs1, FRM_DYN)>;
def : Pat<(any_fpextend FPR16INX:$rs1), (FCVT_D_H_IN32X FPR16INX:$rs1, FRM_RNE)>;

/// Float arithmetic operations
def : Pat<(fcopysign FPR16INX:$rs1, FPR64IN32X:$rs2),
          (FSGNJ_H_INX $rs1, (FCVT_H_D_IN32X $rs2, 0b111))>;
def : Pat<(fcopysign FPR64IN32X:$rs1, FPR16INX:$rs2), (FSGNJ_D_IN32X $rs1, (FCVT_D_H_IN32X $rs2, FRM_RNE))>;
} // Predicates = [HasStdExtZhinxmin, HasStdExtZdinx, IsRV32]

let Predicates = [HasStdExtZhinxmin, HasStdExtZdinx, IsRV64] in {
/// Float conversion operations
// f64 -> f16, f16 -> f64
def : Pat<(any_fpround FPR64INX:$rs1), (FCVT_H_D_INX FPR64INX:$rs1, FRM_DYN)>;
def : Pat<(any_fpextend FPR16INX:$rs1), (FCVT_D_H_INX FPR16INX:$rs1, FRM_RNE)>;

/// Float arithmetic operations
def : Pat<(fcopysign FPR16INX:$rs1, FPR64INX:$rs2),
          (FSGNJ_H_INX $rs1, (FCVT_H_D_INX $rs2, 0b111))>;
def : Pat<(fcopysign FPR64INX:$rs1, FPR16INX:$rs2), (FSGNJ_D_INX $rs1, (FCVT_D_H_INX $rs2, FRM_RNE))>;
} // Predicates = [HasStdExtZhinxmin, HasStdExtZdinx, IsRV64]

let Predicates = [HasStdExtZfhmin, NoStdExtZfh] in {
// half->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint (f16 FPR16:$rs1))), (FCVT_W_S (FCVT_S_H $rs1, FRM_RNE), FRM_RTZ)>;
def : Pat<(i32 (any_fp_to_uint (f16 FPR16:$rs1))), (FCVT_WU_S (FCVT_S_H $rs1, FRM_RNE), FRM_RTZ)>;

// half->int32 with current rounding mode.
def : Pat<(i32 (any_lrint (f16 FPR16:$rs1))), (FCVT_W_S (FCVT_S_H $rs1, FRM_RNE), FRM_DYN)>;

// half->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround (f16 FPR16:$rs1))), (FCVT_W_S (FCVT_S_H $rs1, FRM_RNE), FRM_RMM)>;

// [u]int->half. Match GCC and default to using dynamic rounding mode.
def : Pat<(f16 (any_sint_to_fp (i32 GPR:$rs1))), (FCVT_H_S (FCVT_S_W $rs1, FRM_DYN), FRM_DYN)>;
def : Pat<(f16 (any_uint_to_fp (i32 GPR:$rs1))), (FCVT_H_S (FCVT_S_WU $rs1, FRM_DYN), FRM_DYN)>;
} // Predicates = [HasStdExtZfhmin, NoStdExtZfh]

let Predicates = [HasStdExtZhinxmin, NoStdExtZhinx] in {
// half->[u]int. Round-to-zero must be used.
def : Pat<(i32 (any_fp_to_sint FPR16INX:$rs1)), (FCVT_W_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RTZ)>;
def : Pat<(i32 (any_fp_to_uint FPR16INX:$rs1)), (FCVT_WU_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RTZ)>;

// half->int32 with current rounding mode.
def : Pat<(i32 (any_lrint FPR16INX:$rs1)), (FCVT_W_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_DYN)>;

// half->int32 rounded to nearest with ties rounded away from zero.
def : Pat<(i32 (any_lround FPR16INX:$rs1)), (FCVT_W_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RMM)>;

// [u]int->half. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i32 GPR:$rs1)), (FCVT_H_S_INX (FCVT_S_W_INX $rs1, FRM_DYN), FRM_DYN)>;
def : Pat<(any_uint_to_fp (i32 GPR:$rs1)), (FCVT_H_S_INX (FCVT_S_WU_INX $rs1, FRM_DYN), FRM_DYN)>;
} // Predicates = [HasStdExtZhinxmin, NoStdExtZhinx]

let Predicates = [HasStdExtZfhmin, NoStdExtZfh, IsRV64] in {
// half->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint (f16 FPR16:$rs1))), (FCVT_L_S (FCVT_S_H $rs1, FRM_RNE), FRM_RTZ)>;
def : Pat<(i64 (any_fp_to_uint (f16 FPR16:$rs1))), (FCVT_LU_S (FCVT_S_H $rs1, FRM_RNE), FRM_RTZ)>;

// half->int64 with current rounding mode.
def : Pat<(i64 (any_lrint (f16 FPR16:$rs1))), (FCVT_L_S (FCVT_S_H $rs1, FRM_RNE), FRM_DYN)>;
def : Pat<(i64 (any_llrint (f16 FPR16:$rs1))), (FCVT_L_S (FCVT_S_H $rs1, FRM_RNE), FRM_DYN)>;

// half->int64 rounded to nearest with ties rounded away from zero.
def : Pat<(i64 (any_lround (f16 FPR16:$rs1))), (FCVT_L_S (FCVT_S_H $rs1, FRM_RNE), FRM_RMM)>;
def : Pat<(i64 (any_llround (f16 FPR16:$rs1))), (FCVT_L_S (FCVT_S_H $rs1, FRM_RNE), FRM_RMM)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(f16 (any_sint_to_fp (i64 GPR:$rs1))), (FCVT_H_S (FCVT_S_L $rs1, FRM_DYN), FRM_DYN)>;
def : Pat<(f16 (any_uint_to_fp (i64 GPR:$rs1))), (FCVT_H_S (FCVT_S_LU $rs1, FRM_DYN), FRM_DYN)>;
} // Predicates = [HasStdExtZfhmin, NoStdExtZfh, IsRV64]

let Predicates = [HasStdExtZhinxmin, NoStdExtZhinx, IsRV64] in {
// half->[u]int64. Round-to-zero must be used.
def : Pat<(i64 (any_fp_to_sint FPR16INX:$rs1)), (FCVT_L_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RTZ)>;
def : Pat<(i64 (any_fp_to_uint FPR16INX:$rs1)), (FCVT_LU_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RTZ)>;

// half->int64 with current rounding mode.
def : Pat<(i64 (any_lrint FPR16INX:$rs1)), (FCVT_L_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_DYN)>;
def : Pat<(i64 (any_llrint FPR16INX:$rs1)), (FCVT_L_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_DYN)>;

// half->int64 rounded to nearest with ties rounded away from zero.
def : Pat<(i64 (any_lround FPR16INX:$rs1)), (FCVT_L_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RMM)>;
def : Pat<(i64 (any_llround FPR16INX:$rs1)), (FCVT_L_S_INX (FCVT_S_H_INX $rs1, FRM_RNE), FRM_RMM)>;

// [u]int->fp. Match GCC and default to using dynamic rounding mode.
def : Pat<(any_sint_to_fp (i64 GPR:$rs1)), (FCVT_H_S_INX (FCVT_S_L_INX $rs1, FRM_DYN), FRM_DYN)>;
def : Pat<(any_uint_to_fp (i64 GPR:$rs1)), (FCVT_H_S_INX (FCVT_S_LU_INX $rs1, FRM_DYN), FRM_DYN)>;
} // Predicates = [HasStdExtZhinxmin, NoStdExtZhinx, IsRV64]
