//==- MicroMipsInstrFPU.td - microMIPS FPU Instruction Info -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the microMIPS FPU instruction set.
//
//===----------------------------------------------------------------------===//

multiclass ADDS_MMM<string opstr, InstrItinClass Itin, bit IsComm,
                    SDPatternOperator OpNode = null_frag> {
  def _D32_MM : MMRel, ADDS_FT<opstr, AFGR64Opnd, Itin, IsComm, OpNode>,
                FGR_32 {
    string DecoderNamespace = "MicroMips";
  }
  // FIXME: This needs to be part of the instruction mapping tables.
  def _D64_MM : ADDS_FT<opstr, FGR64Opnd, Itin, IsComm, OpNode>, FGR_64 {
    string DecoderNamespace = "MicroMipsFP64";
  }
}

def FADD_S_MM : MMRel, ADDS_FT<"add.s", FGR32Opnd, II_ADD_S, 1, fadd>,
                ADDS_FM_MM<0, 0x30>, ISA_MICROMIPS;
def FDIV_S_MM : MMRel, ADDS_FT<"div.s", FGR32Opnd, II_DIV_S, 0, fdiv>,
                ADDS_FM_MM<0, 0xf0>, ISA_MICROMIPS;
def FMUL_S_MM : MMRel, ADDS_FT<"mul.s", FGR32Opnd, II_MUL_S, 1, fmul>,
                ADDS_FM_MM<0, 0xb0>, ISA_MICROMIPS;
def FSUB_S_MM : MMRel, ADDS_FT<"sub.s", FGR32Opnd, II_SUB_S, 0, fsub>,
                ADDS_FM_MM<0, 0x70>, ISA_MICROMIPS;

defm FADD : ADDS_MMM<"add.d", II_ADD_D, 1, fadd>,
            ADDS_FM_MM<1, 0x30>, ISA_MICROMIPS;
defm FDIV : ADDS_MMM<"div.d", II_DIV_D, 0, fdiv>,
            ADDS_FM_MM<1, 0xf0>, ISA_MICROMIPS;
defm FMUL : ADDS_MMM<"mul.d", II_MUL_D, 1, fmul>,
            ADDS_FM_MM<1, 0xb0>, ISA_MICROMIPS;
defm FSUB : ADDS_MMM<"sub.d", II_SUB_D, 0, fsub>,
            ADDS_FM_MM<1, 0x70>, ISA_MICROMIPS;

let DecoderNamespace = "MicroMips" in {
  def LWXC1_MM : MMRel, LWXC1_FT<"lwxc1", FGR32Opnd, II_LWXC1, load>,
                 LWXC1_FM_MM<0x48>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SWXC1_MM : MMRel, SWXC1_FT<"swxc1", FGR32Opnd, II_SWXC1, store>,
                 SWXC1_FM_MM<0x88>, ISA_MICROMIPS32_NOT_MIPS32R6;

  def LUXC1_MM : MMRel, LWXC1_FT<"luxc1", FGR64Opnd, II_LUXC1>,
                 LWXC1_FM_MM<0x148>, FGR_64, ISA_MICROMIPS32_NOT_MIPS32R6;
  def SUXC1_MM : MMRel, SWXC1_FT<"suxc1", FGR64Opnd, II_SUXC1>,
                 SWXC1_FM_MM<0x188>, FGR_64, ISA_MICROMIPS32_NOT_MIPS32R6;
}
let isCodeGenOnly = 1 in {
def FCMP_S32_MM : MMRel, CEQS_FT<"s", FGR32, II_C_CC_S, MipsFPCmp>,
                  CEQS_FM_MM<0>, ISA_MICROMIPS32_NOT_MIPS32R6 {
  // FIXME: This is a required to work around the fact that these instructions
  //        only use $fcc0. Ideally, MipsFPCmp nodes could be removed and the
  //        fcc register set is used directly.
  bits<3> fcc = 0;
}

def FCMP_D32_MM : MMRel, CEQS_FT<"d", AFGR64, II_C_CC_D, MipsFPCmp>,
                  CEQS_FM_MM<1>, ISA_MICROMIPS32_NOT_MIPS32R6 {
  // FIXME: This is a required to work around the fact that these instructions
  //        only use $fcc0. Ideally, MipsFPCmp nodes could be removed and the
  //        fcc register set is used directly.
  bits<3> fcc = 0;
}

}

let DecoderNamespace = "MicroMips" in {
  def BC1F_MM : MMRel, BC1F_FT<"bc1f", brtarget_mm, II_BC1F, MIPS_BRANCH_F>,
                BC1F_FM_MM<0x1c>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def BC1T_MM : MMRel, BC1F_FT<"bc1t", brtarget_mm, II_BC1T, MIPS_BRANCH_T>,
                BC1F_FM_MM<0x1d>, ISA_MICROMIPS32_NOT_MIPS32R6;
  def CVT_W_S_MM   : MMRel, ABSS_FT<"cvt.w.s", FGR32Opnd, FGR32Opnd, II_CVT>,
                     ROUND_W_FM_MM<0, 0x24>, ISA_MICROMIPS;
}

let DecoderNamespace = "MicroMips" in {
  def ROUND_W_S_MM : MMRel, StdMMR6Rel, ABSS_FT<"round.w.s", FGR32Opnd,
                                                FGR32Opnd, II_ROUND>,
                     ROUND_W_FM_MM<0, 0xec>, ISA_MICROMIPS;

  def CEIL_W_MM  : MMRel, ABSS_FT<"ceil.w.d", FGR32Opnd, AFGR64Opnd, II_CEIL>,
                   ROUND_W_FM_MM<1, 0x6c>, ISA_MICROMIPS, FGR_32;
  def FLOOR_W_MM : MMRel, ABSS_FT<"floor.w.d", FGR32Opnd, AFGR64Opnd, II_FLOOR>,
                   ROUND_W_FM_MM<1, 0x2c>, ISA_MICROMIPS, FGR_32;
  def ROUND_W_MM : MMRel, StdMMR6Rel, ABSS_FT<"round.w.d", FGR32Opnd,
                                              AFGR64Opnd, II_ROUND>,
                   ROUND_W_FM_MM<1, 0xec>, ISA_MICROMIPS, FGR_32;
  def TRUNC_W_MM : MMRel, ABSS_FT<"trunc.w.d", FGR32Opnd, AFGR64Opnd, II_TRUNC>,
                   ROUND_W_FM_MM<1, 0xac>, ISA_MICROMIPS, FGR_32;

  def CVT_L_S_MM   : MMRel, ABSS_FT<"cvt.l.s", FGR64Opnd, FGR32Opnd, II_CVT>,
                     ROUND_W_FM_MM<0, 0x4>, ISA_MICROMIPS, FGR_64;
  def CVT_L_D64_MM : MMRel, ABSS_FT<"cvt.l.d", FGR64Opnd, FGR64Opnd, II_CVT>,
                     ROUND_W_FM_MM<1, 0x4>, ISA_MICROMIPS, FGR_64;

  def CVT_W_D32_MM : MMRel, ABSS_FT<"cvt.w.d", FGR32Opnd, AFGR64Opnd, II_CVT>,
                     ROUND_W_FM_MM<1, 0x24>, ISA_MICROMIPS, FGR_32;
}
let DecoderNamespace = "MicroMipsFP64" in {
  def CVT_W_D64_MM : ABSS_FT<"cvt.w.d", FGR32Opnd, FGR64Opnd, II_CVT>,
                     ROUND_W_FM_MM<1, 0x24>, ISA_MICROMIPS, FGR_64;
}

multiclass ABSS_MMM<string opstr, InstrItinClass Itin,
                    SDPatternOperator OpNode = null_frag> {
  def _D32_MM : MMRel, ABSS_FT<opstr, AFGR64Opnd, AFGR64Opnd, Itin, OpNode>,
                ISA_MICROMIPS, FGR_32 {
    string DecoderNamespace = "MicroMips";
  }
  def _D64_MM : StdMMR6Rel, ABSS_FT<opstr, FGR64Opnd, FGR64Opnd, Itin, OpNode>,
                ISA_MICROMIPS, FGR_64 {
    string DecoderNamespace = "MicroMipsFP64";
  }
}

defm FSQRT : ABSS_MMM<"sqrt.d", II_SQRT_D, fsqrt>, ROUND_W_FM_MM<1, 0x28>;
defm FABS : ABSS_MMM<"abs.d", II_SQRT_D, fabs>, ABS_FM_MM<1, 0xd>;

let DecoderNamespace = "MicroMips", AdditionalPredicates = [UseAbs] in {
  def FABS_S_MM : MMRel, ABSS_FT<"abs.s", FGR32Opnd, FGR32Opnd, II_ABS, fabs>,
                  ABS_FM_MM<0, 0xd>, ISA_MICROMIPS;
}

def FMOV_S_MM : MMRel, ABSS_FT<"mov.s", FGR32Opnd, FGR32Opnd, II_MOV_S>,
                ABS_FM_MM<0, 0x1>, ISA_MICROMIPS {
  let isMoveReg = 1;
}
def FNEG_S_MM : MMRel, ABSS_FT<"neg.s", FGR32Opnd, FGR32Opnd, II_NEG, fneg>,
                ABS_FM_MM<0, 0x2d>, ISA_MICROMIPS;

let DecoderNamespace = "MicroMips" in {
  def CVT_D32_S_MM : MMRel, ABSS_FT<"cvt.d.s", AFGR64Opnd, FGR32Opnd, II_CVT>,
                     ABS_FM_MM<0, 0x4d>, ISA_MICROMIPS, FGR_32;
  def CVT_D32_W_MM : MMRel, ABSS_FT<"cvt.d.w", AFGR64Opnd, FGR32Opnd, II_CVT>,
                     ABS_FM_MM<1, 0x4d>, ISA_MICROMIPS, FGR_32;
}

let DecoderNamespace = "MicroMipsFP64" in {
  def CVT_D64_S_MM : ABSS_FT<"cvt.d.s", FGR64Opnd, FGR32Opnd, II_CVT>,
                     ABS_FM_MM<0, 0x4d>, ISA_MICROMIPS, FGR_64;
  def CVT_D64_W_MM : ABSS_FT<"cvt.d.w", FGR64Opnd, FGR32Opnd, II_CVT>,
                     ABS_FM_MM<1, 0x4d>, ISA_MICROMIPS, FGR_64;
  def CVT_S_D64_MM : ABSS_FT<"cvt.s.d", FGR32Opnd, FGR64Opnd, II_CVT>,
                     ABS_FM_MM<0, 0x6d>, ISA_MICROMIPS, FGR_64;
}

let DecoderNamespace = "MicroMips" in {
  def CVT_S_D32_MM : MMRel, ABSS_FT<"cvt.s.d", FGR32Opnd, AFGR64Opnd, II_CVT>,
                     ABS_FM_MM<0, 0x6d>, ISA_MICROMIPS, FGR_32;
  def CVT_S_W_MM : MMRel, ABSS_FT<"cvt.s.w", FGR32Opnd, FGR32Opnd, II_CVT>,
                   ABS_FM_MM<1, 0x6d>, ISA_MICROMIPS;
}


defm FNEG : ABSS_MMM<"neg.d", II_NEG, fneg>, ABS_FM_MM<1, 0x2d>;
defm FMOV : ABSS_MMM<"mov.d", II_MOV_D>, ABS_FM_MM<1, 0x1>;

let DecoderNamespace = "MicroMips" in {
  def MOVZ_I_S_MM : MMRel, CMov_I_F_FT<"movz.s", GPR32Opnd, FGR32Opnd,
                                       II_MOVZ_S>, CMov_I_F_FM_MM<0x78, 0>,
                    ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVN_I_S_MM : MMRel, CMov_I_F_FT<"movn.s", GPR32Opnd, FGR32Opnd,
                                     II_MOVN_S>, CMov_I_F_FM_MM<0x38, 0>,
                    ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVZ_I_D32_MM : MMRel, CMov_I_F_FT<"movz.d", GPR32Opnd, AFGR64Opnd,
                                         II_MOVZ_D>, CMov_I_F_FM_MM<0x78, 1>,
                      ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
  def MOVN_I_D32_MM : MMRel, CMov_I_F_FT<"movn.d", GPR32Opnd, AFGR64Opnd,
                                         II_MOVN_D>, CMov_I_F_FM_MM<0x38, 1>,
                      ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;

  def MOVT_S_MM : MMRel, CMov_F_F_FT<"movt.s", FGR32Opnd, II_MOVT_S,
                                     MipsCMovFP_T>, CMov_F_F_FM_MM<0x60, 0>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVF_S_MM : MMRel, CMov_F_F_FT<"movf.s", FGR32Opnd, II_MOVF_S,
                                     MipsCMovFP_F>, CMov_F_F_FM_MM<0x20, 0>,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
  def MOVT_D32_MM : MMRel, CMov_F_F_FT<"movt.d", AFGR64Opnd, II_MOVT_D,
                                     MipsCMovFP_T>, CMov_F_F_FM_MM<0x60, 1>,
                  ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
  def MOVF_D32_MM : MMRel, CMov_F_F_FT<"movf.d", AFGR64Opnd, II_MOVF_D,
                                       MipsCMovFP_F>, CMov_F_F_FM_MM<0x20, 1>,
                    ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;

  def MFC1_MM : MMRel, MFC1_FT<"mfc1", GPR32Opnd, FGR32Opnd,
                               II_MFC1, bitconvert>, MFC1_FM_MM<0x80>,
                ISA_MICROMIPS;
  def MTC1_MM : MMRel, MTC1_FT<"mtc1", FGR32Opnd, GPR32Opnd,
                               II_MTC1, bitconvert>, MFC1_FM_MM<0xa0>,
                ISA_MICROMIPS;

  def MADD_S_MM : MMRel, MADDS_FT<"madd.s", FGR32Opnd, II_MADD_S>,
                  MADDS_FM_MM<0x1>, ISA_MICROMIPS32_NOT_MIPS32R6, MADD4;
  def MSUB_S_MM : MMRel, MADDS_FT<"msub.s", FGR32Opnd, II_MSUB_S>,
                  MADDS_FM_MM<0x21>, ISA_MICROMIPS32_NOT_MIPS32R6, MADD4;
  let AdditionalPredicates = [NoNaNsFPMath, HasMadd4] in {
    def NMADD_S_MM : MMRel, NMADDS_FT<"nmadd.s", FGR32Opnd, II_NMADD_S>,
                     MADDS_FM_MM<0x2>, ISA_MICROMIPS32_NOT_MIPS32R6;
    def NMSUB_S_MM : MMRel, NMADDS_FT<"nmsub.s", FGR32Opnd, II_NMSUB_S>,
                     MADDS_FM_MM<0x22>, ISA_MICROMIPS32_NOT_MIPS32R6;
  }
  def MADD_D32_MM  : MMRel, MADDS_FT<"madd.d", AFGR64Opnd, II_MADD_D>,
                     MADDS_FM_MM<0x9>, ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32,
                     MADD4;
  def MSUB_D32_MM  : MMRel, MADDS_FT<"msub.d", AFGR64Opnd, II_MSUB_D>,
                     MADDS_FM_MM<0x29>, ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32,
                     MADD4;
  let AdditionalPredicates = [NoNaNsFPMath, HasMadd4] in {
    def NMADD_D32_MM : MMRel, NMADDS_FT<"nmadd.d", AFGR64Opnd, II_NMADD_D>,
                       MADDS_FM_MM<0xa>, ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
    def NMSUB_D32_MM : MMRel, NMADDS_FT<"nmsub.d", AFGR64Opnd, II_NMSUB_D>,
                       MADDS_FM_MM<0x2a>, ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
  }

  def FLOOR_W_S_MM : MMRel, ABSS_FT<"floor.w.s", FGR32Opnd, FGR32Opnd,
                                    II_FLOOR>, ROUND_W_FM_MM<0, 0x2c>,
                     ISA_MICROMIPS;
  def TRUNC_W_S_MM : MMRel, StdMMR6Rel, ABSS_FT<"trunc.w.s", FGR32Opnd,
                                                FGR32Opnd, II_TRUNC>,
                     ROUND_W_FM_MM<0, 0xac>, ISA_MICROMIPS;
  def CEIL_W_S_MM  : MMRel, ABSS_FT<"ceil.w.s", FGR32Opnd, FGR32Opnd, II_CEIL>,
                     ROUND_W_FM_MM<0, 0x6c>, ISA_MICROMIPS;

  def FSQRT_S_MM : MMRel, ABSS_FT<"sqrt.s", FGR32Opnd, FGR32Opnd, II_SQRT_S,
                                  fsqrt>, ROUND_W_FM_MM<0, 0x28>, ISA_MICROMIPS;

  def MTHC1_D32_MM : MMRel,
                     MTC1_64_FT<"mthc1", AFGR64Opnd, GPR32Opnd, II_MTHC1>,
                     MFC1_FM_MM<0xe0>, ISA_MICROMIPS, FGR_32;
  def MFHC1_D32_MM : MMRel, MFC1_FT<"mfhc1", GPR32Opnd, AFGR64Opnd, II_MFHC1>,
                     MFC1_FM_MM<0xc0>, ISA_MICROMIPS, FGR_32;
}

let DecoderNamespace = "MicroMipsFP64" in {
  def MTHC1_D64_MM : MTC1_64_FT<"mthc1", FGR64Opnd, GPR32Opnd, II_MTHC1>,
                     MFC1_FM_MM<0xe0>, ISA_MICROMIPS, FGR_64;
  def MFHC1_D64_MM : MFC1_FT<"mfhc1", GPR32Opnd, FGR64Opnd, II_MFHC1>,
                     MFC1_FM_MM<0xc0>, ISA_MICROMIPS, FGR_64;
  def MTC1_D64_MM : MTC1_FT<"mtc1", FGR64Opnd, GPR32Opnd, II_MTC1>,
                    MFC1_FM_MM<0xa0>, ISA_MICROMIPS, FGR_64;
}

let DecoderNamespace = "MicroMips" in {
  def CFC1_MM : MMRel, MFC1_FT<"cfc1", GPR32Opnd, CCROpnd, II_CFC1>,
                MFC1_FM_MM<0x40>, ISA_MICROMIPS;
  def CTC1_MM : MMRel, MTC1_FT<"ctc1", CCROpnd, GPR32Opnd, II_CTC1>,
                MFC1_FM_MM<0x60>, ISA_MICROMIPS;
  def RECIP_S_MM : MMRel, ABSS_FT<"recip.s", FGR32Opnd, FGR32Opnd,
                                    II_RECIP_S>,
                     ROUND_W_FM_MM<0b0, 0b01001000>, ISA_MICROMIPS;
  def RECIP_D32_MM : MMRel, ABSS_FT<"recip.d", AFGR64Opnd, AFGR64Opnd,
                                    II_RECIP_D>,
                     ROUND_W_FM_MM<0b1, 0b01001000>, ISA_MICROMIPS, FGR_32 {
    let BaseOpcode = "RECIP_D32";
  }
  let DecoderNamespace = "MicroMipsFP64" in
    def RECIP_D64_MM : MMRel, ABSS_FT<"recip.d", FGR64Opnd, FGR64Opnd,
                                      II_RECIP_D>,
                     ROUND_W_FM_MM<0b1, 0b01001000>, ISA_MICROMIPS, FGR_64;
  def RSQRT_S_MM : MMRel, ABSS_FT<"rsqrt.s", FGR32Opnd, FGR32Opnd,
                                  II_RECIP_S>,
                   ROUND_W_FM_MM<0b0, 0b00001000>, ISA_MICROMIPS;
  def RSQRT_D32_MM : MMRel, ABSS_FT<"rsqrt.d", AFGR64Opnd, AFGR64Opnd,
                                  II_RECIP_D>,
                   ROUND_W_FM_MM<0b1, 0b00001000>, ISA_MICROMIPS, FGR_32 {
    let BaseOpcode = "RSQRT_D32";
  }
  let DecoderNamespace = "MicroMipsFP64" in
    def RSQRT_D64_MM : MMRel, ABSS_FT<"rsqrt.d", FGR64Opnd, FGR64Opnd,
                                      II_RECIP_D>,
                       ROUND_W_FM_MM<0b1, 0b00001000>, ISA_MICROMIPS, FGR_64;
}

let DecoderNamespace = "MicroMips",  DecoderMethod = "DecodeFMemMMR2" in {
  def LDC1_MM_D32 : MMRel, LW_FT<"ldc1", AFGR64Opnd, mem_mm_16, II_LDC1, load>,
                    LW_FM_MM<0x2f>, ISA_MICROMIPS, FGR_32 {
    let BaseOpcode = "LDC132";
  }
  def SDC1_MM_D32 : MMRel, SW_FT<"sdc1", AFGR64Opnd, mem_mm_16, II_SDC1, store>,
                    LW_FM_MM<0x2e>, ISA_MICROMIPS, FGR_32 {
    let BaseOpcode = "SDC164";
  }
  def LWC1_MM : MMRel, LW_FT<"lwc1", FGR32Opnd, mem_mm_16, II_LWC1, load>,
                LW_FM_MM<0x27>, ISA_MICROMIPS;
  def SWC1_MM : MMRel, SW_FT<"swc1", FGR32Opnd, mem_mm_16, II_SWC1, store>,
                LW_FM_MM<0x26>, ISA_MICROMIPS;
}

let DecoderNamespace = "Mips64", DecoderMethod = "DecodeFMemMMR2" in {
  def LDC1_MM_D64 : MMRel, LW_FT<"ldc1", FGR64Opnd, mem_mm_16, II_LDC1, load>,
                    LW_FM_MM<0x2f>, ISA_MICROMIPS, FGR_64 {
    let BaseOpcode = "LDC164";
  }
  def SDC1_MM_D64 : MMRel, SW_FT<"sdc1", FGR64Opnd, mem_mm_16, II_SDC1, store>,
                    LW_FM_MM<0x2e>, ISA_MICROMIPS, FGR_64 {
    let BaseOpcode = "SDC164";
  }
}


multiclass C_COND_MM<string TypeStr, RegisterOperand RC, bits<2> fmt,
                     InstrItinClass itin> {
  def C_F_#NAME#_MM : MMRel, C_COND_FT<"f", TypeStr, RC, itin>,
                      C_COND_FM_MM<fmt, 0> {
    let BaseOpcode = "c.f."#NAME;
    let isCommutable = 1;
  }
  def C_UN_#NAME#_MM : MMRel, C_COND_FT<"un", TypeStr, RC, itin>,
                       C_COND_FM_MM<fmt, 1> {
    let BaseOpcode = "c.un."#NAME;
    let isCommutable = 1;
  }
  def C_EQ_#NAME#_MM : MMRel, C_COND_FT<"eq", TypeStr, RC, itin>,
                       C_COND_FM_MM<fmt, 2> {
    let BaseOpcode = "c.eq."#NAME;
    let isCommutable = 1;
  }
  def C_UEQ_#NAME#_MM : MMRel, C_COND_FT<"ueq", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 3> {
    let BaseOpcode = "c.ueq."#NAME;
    let isCommutable = 1;
  }
  def C_OLT_#NAME#_MM : MMRel, C_COND_FT<"olt", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 4> {
    let BaseOpcode = "c.olt."#NAME;
  }
  def C_ULT_#NAME#_MM : MMRel, C_COND_FT<"ult", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 5> {
    let BaseOpcode = "c.ult."#NAME;
  }
  def C_OLE_#NAME#_MM : MMRel, C_COND_FT<"ole", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 6> {
    let BaseOpcode = "c.ole."#NAME;
  }
  def C_ULE_#NAME#_MM : MMRel, C_COND_FT<"ule", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 7> {
    let BaseOpcode = "c.ule."#NAME;
  }
  def C_SF_#NAME#_MM : MMRel, C_COND_FT<"sf", TypeStr, RC, itin>,
                       C_COND_FM_MM<fmt, 8> {
    let BaseOpcode = "c.sf."#NAME;
    let isCommutable = 1;
  }
  def C_NGLE_#NAME#_MM : MMRel, C_COND_FT<"ngle", TypeStr, RC, itin>,
                         C_COND_FM_MM<fmt, 9> {
    let BaseOpcode = "c.ngle."#NAME;
  }
  def C_SEQ_#NAME#_MM : MMRel, C_COND_FT<"seq", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 10> {
    let BaseOpcode = "c.seq."#NAME;
    let isCommutable = 1;
  }
  def C_NGL_#NAME#_MM : MMRel, C_COND_FT<"ngl", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 11> {
    let BaseOpcode = "c.ngl."#NAME;
  }
  def C_LT_#NAME#_MM : MMRel, C_COND_FT<"lt", TypeStr, RC, itin>,
                       C_COND_FM_MM<fmt, 12> {
    let BaseOpcode = "c.lt."#NAME;
  }
  def C_NGE_#NAME#_MM : MMRel, C_COND_FT<"nge", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 13> {
    let BaseOpcode = "c.nge."#NAME;
  }
  def C_LE_#NAME#_MM : MMRel, C_COND_FT<"le", TypeStr, RC, itin>,
                       C_COND_FM_MM<fmt, 14> {
    let BaseOpcode = "c.le."#NAME;
  }
  def C_NGT_#NAME#_MM : MMRel, C_COND_FT<"ngt", TypeStr, RC, itin>,
                        C_COND_FM_MM<fmt, 15> {
    let BaseOpcode = "c.ngt."#NAME;
  }
}
let DecoderNamespace = "MicroMips" in {
  defm S   : C_COND_MM<"s", FGR32Opnd, 0b00, II_C_CC_S>,
             ISA_MICROMIPS32_NOT_MIPS32R6;
  defm D32 : C_COND_MM<"d", AFGR64Opnd, 0b01, II_C_CC_D>,
             ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
}

let DecoderNamespace = "Mips64" in
  defm D64 : C_COND_MM<"d", FGR64Opnd, 0b01, II_C_CC_D>,
             ISA_MICROMIPS32_NOT_MIPS32R6, FGR_64;

defm S_MM   : C_COND_ALIASES<"s", FGR32Opnd>, HARDFLOAT,
              ISA_MICROMIPS32_NOT_MIPS32R6;
defm D32_MM : C_COND_ALIASES<"d", AFGR64Opnd>, HARDFLOAT,
              ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
defm D64_MM : C_COND_ALIASES<"d", FGR64Opnd>, HARDFLOAT,
              ISA_MICROMIPS32_NOT_MIPS32R6, FGR_64;

defm : BC1_ALIASES<BC1T_MM, "bc1t", BC1F_MM, "bc1f">,
       ISA_MICROMIPS32_NOT_MIPS32R6, HARDFLOAT;


// To generate NMADD and NMSUB instructions when fneg node is present
let AdditionalPredicates = [NoNaNsFPMath, HasMadd4,
                            InMicroMips, NotMips32r6] in {
  defm : NMADD_NMSUB<NMADD_S_MM, NMSUB_S_MM, FGR32Opnd>,
         ISA_MICROMIPS32_NOT_MIPS32R6;
  defm : NMADD_NMSUB<NMADD_D32_MM, NMSUB_D32_MM, AFGR64Opnd>,
         ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
}

//===----------------------------------------------------------------------===//
// Floating Point Patterns
//===----------------------------------------------------------------------===//

// Patterns for loads/stores with a reg+imm operand.
let AddedComplexity = 40 in {
  def : LoadRegImmPat<LDC1_MM_D32, f64, load>, ISA_MICROMIPS, FGR_32;
  def : StoreRegImmPat<SDC1_MM_D32, f64>, ISA_MICROMIPS, FGR_32;
  def : LoadRegImmPat<LDC1_MM_D64, f64, load>, ISA_MICROMIPS, FGR_64;
  def : StoreRegImmPat<SDC1_MM_D64, f64>, ISA_MICROMIPS, FGR_64;
  def : LoadRegImmPat<LWC1_MM, f32, load>, ISA_MICROMIPS;
  def : StoreRegImmPat<SWC1_MM, f32>, ISA_MICROMIPS;
}

def : MipsPat<(MipsMTC1_D64 GPR32Opnd:$src),
              (MTC1_D64_MM GPR32Opnd:$src)>, ISA_MICROMIPS, FGR_64;

def : MipsPat<(f32 fpimm0), (MTC1_MM ZERO)>, ISA_MICROMIPS32_NOT_MIPS32R6;
def : MipsPat<(f32 fpimm0neg), (FNEG_S_MM (MTC1_MM ZERO))>,
      ISA_MICROMIPS32_NOT_MIPS32R6;

def : MipsPat<(f32 (fpround FGR64Opnd:$src)),
              (CVT_S_D64_MM FGR64Opnd:$src)>, ISA_MICROMIPS, FGR_64;
def : MipsPat<(f64 (fpextend FGR32Opnd:$src)),
              (CVT_D64_S_MM FGR32Opnd:$src)>, ISA_MICROMIPS, FGR_64;
def : MipsPat<(f32 (fpround AFGR64Opnd:$src)),
              (CVT_S_D32_MM AFGR64Opnd:$src)>, ISA_MICROMIPS, FGR_32;
def : MipsPat<(f64 (fpextend FGR32Opnd:$src)),
              (CVT_D32_S_MM FGR32Opnd:$src)>, ISA_MICROMIPS, FGR_32;
def : MipsPat<(MipsTruncIntFP AFGR64Opnd:$src),
              (TRUNC_W_MM AFGR64Opnd:$src)>, ISA_MICROMIPS32_NOT_MIPS32R6,
              FGR_32;
def : MipsPat<(MipsTruncIntFP FGR64Opnd:$src),
              (CVT_W_D64_MM FGR64Opnd:$src)>, ISA_MICROMIPS32_NOT_MIPS32R6,
              FGR_64;
def : MipsPat<(MipsTruncIntFP FGR32Opnd:$src),
              (TRUNC_W_S_MM FGR32Opnd:$src)>, ISA_MICROMIPS32_NOT_MIPS32R6;

// Selects
defm : MovzPats0<GPR32, FGR32, MOVZ_I_S_MM, SLT_MM, SLTu_MM, SLTi_MM, SLTiu_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;
defm : MovzPats1<GPR32, FGR32, MOVZ_I_S_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;

defm : MovnPats<GPR32, FGR32, MOVN_I_S_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6;

defm : MovzPats0<GPR32, AFGR64, MOVZ_I_D32_MM, SLT_MM, SLTu_MM, SLTi_MM,
                 SLTiu_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
defm : MovzPats1<GPR32, AFGR64, MOVZ_I_D32_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
defm : MovnPats<GPR32, AFGR64, MOVN_I_D32_MM, XOR_MM>,
       ISA_MICROMIPS32_NOT_MIPS32R6, FGR_32;
