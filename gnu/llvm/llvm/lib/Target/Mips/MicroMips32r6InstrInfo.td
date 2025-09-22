//=- MicroMips32r6InstrInfo.td - MicroMips r6 Instruction Information -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes microMIPSr6 instructions.
//
//===----------------------------------------------------------------------===//

def brtarget21_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget21OpValueMM";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget21MM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
  let PrintMethod = "printBranchOperand";
}

def brtarget26_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget26OpValueMM";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget26MM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
  let PrintMethod = "printBranchOperand";
}

def brtargetr6 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValueMMR6";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTargetMM";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
  let PrintMethod = "printBranchOperand";
}

def brtarget_lsl2_mm : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValueLsl2MMR6";
  let OperandType = "OPERAND_PCREL";
  // Instructions that use this operand have their decoder method
  // set with DecodeDisambiguates
  let DecoderMethod = "";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
  let PrintMethod = "printBranchOperand";
}

//===----------------------------------------------------------------------===//
//
// Instruction Encodings
//
//===----------------------------------------------------------------------===//
class ADD_MMR6_ENC : ARITH_FM_MMR6<"add", 0x110>;
class ADDIU_MMR6_ENC : ADDI_FM_MMR6<"addiu", 0xc>;
class ADDU_MMR6_ENC : ARITH_FM_MMR6<"addu", 0x150>;
class ADDIUPC_MMR6_ENC : PCREL19_FM_MMR6<0b00>;
class ALUIPC_MMR6_ENC : PCREL16_FM_MMR6<0b11111>;
class AND_MMR6_ENC : ARITH_FM_MMR6<"and", 0x250>;
class ANDI_MMR6_ENC : ADDI_FM_MMR6<"andi", 0x34>;
class AUIPC_MMR6_ENC  : PCREL16_FM_MMR6<0b11110>;
class ALIGN_MMR6_ENC : POOL32A_ALIGN_FM_MMR6<0b011111>;
class AUI_MMR6_ENC : AUI_FM_MMR6;
class BALC_MMR6_ENC  : BRANCH_OFF26_FM<0b101101>;
class BC_MMR6_ENC : BRANCH_OFF26_FM<0b100101>;
class BC16_MMR6_ENC : BC16_FM_MM16R6;
class BEQZC16_MMR6_ENC : BEQZC_BNEZC_FM_MM16R6<0x23>;
class BNEZC16_MMR6_ENC : BEQZC_BNEZC_FM_MM16R6<0x2b>;
class BITSWAP_MMR6_ENC : POOL32A_BITSWAP_FM_MMR6<0b101100>;
class BRK_MMR6_ENC : BREAK_MMR6_ENC<"break">;
class BEQZC_MMR6_ENC : CMP_BRANCH_OFF21_FM_MMR6<0b100000>;
class BNEZC_MMR6_ENC : CMP_BRANCH_OFF21_FM_MMR6<0b101000>;
class BGEC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"bgec", 0b111101>,
                      DecodeDisambiguates<"POP75GroupBranchMMR6">;
class BGEUC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"bgeuc", 0b110000>,
                       DecodeDisambiguates<"BlezGroupBranchMMR6">;
class BLTC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"bltc", 0b110101>,
                      DecodeDisambiguates<"POP65GroupBranchMMR6">;
class BLTUC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"bltuc", 0b111000>,
                       DecodeDisambiguates<"BgtzGroupBranchMMR6">;
class BEQC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"beqc", 0b011101>;
class BNEC_MMR6_ENC : CMP_BRANCH_2R_OFF16_FM_MMR6<"bnec", 0b011111>;
class BLTZC_MMR6_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM_MMR6<"bltzc", 0b110101>,
                       DecodeDisambiguates<"POP65GroupBranchMMR6">;
class BLEZC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"blezc", 0b111101>,
                       DecodeDisambiguates<"POP75GroupBranchMMR6">;
class BGEZC_MMR6_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM_MMR6<"bgezc", 0b111101>,
                       DecodeDisambiguates<"POP75GroupBranchMMR6">;
class BGTZC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"bgtzc", 0b110101>,
                       DecodeDisambiguates<"POP65GroupBranchMMR6">;
class BEQZALC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"beqzalc", 0b011101>,
                         DecodeDisambiguates<"POP35GroupBranchMMR6">;
class BNEZALC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"bnezalc", 0b011111>,
                         DecodeDisambiguates<"POP37GroupBranchMMR6">;
class BGTZALC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"bgtzalc", 0b111000>,
                         MMDecodeDisambiguatedBy<"BgtzGroupBranchMMR6">;
class BLTZALC_MMR6_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM_MMR6<"bltzalc", 0b111000>,
                         MMDecodeDisambiguatedBy<"BgtzGroupBranchMMR6">;
class BGEZALC_MMR6_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM_MMR6<"bgezalc", 0b110000>,
                         MMDecodeDisambiguatedBy<"BlezGroupBranchMMR6">;
class BLEZALC_MMR6_ENC : CMP_BRANCH_1R_RT_OFF16_FM_MMR6<"blezalc", 0b110000>,
                         MMDecodeDisambiguatedBy<"BlezGroupBranchMMR6">;
class CACHE_MMR6_ENC : CACHE_PREF_FM_MMR6<0b001000, 0b0110>;
class CLO_MMR6_ENC : POOL32A_2R_FM_MMR6<0b0100101100>;
class CLZ_MMR6_ENC : SPECIAL_2R_FM_MMR6<0b010000>;
class DIV_MMR6_ENC : ARITH_FM_MMR6<"div", 0x118>;
class DIVU_MMR6_ENC : ARITH_FM_MMR6<"divu", 0x198>;
class EHB_MMR6_ENC : BARRIER_MMR6_ENC<"ehb", 0x3>;
class EI_MMR6_ENC : POOL32A_EIDI_MMR6_ENC<"ei", 0x15d>;
class DI_MMR6_ENC : POOL32A_EIDI_MMR6_ENC<"di", 0b0100011101>;
class ERET_MMR6_ENC : POOL32A_ERET_FM_MMR6<"eret", 0x3cd>;
class DERET_MMR6_ENC : POOL32A_ERET_FM_MMR6<"eret", 0b1110001101>;
class ERETNC_MMR6_ENC : ERETNC_FM_MMR6<"eretnc">;
class GINVI_MMR6_ENC : POOL32A_GINV_FM_MMR6<"ginvi", 0b00>;
class GINVT_MMR6_ENC : POOL32A_GINV_FM_MMR6<"ginvt", 0b10>;
class JALRC16_MMR6_ENC : POOL16C_JALRC_FM_MM16R6<0xb>;
class JIALC_MMR6_ENC : JMP_IDX_COMPACT_FM<0b100000>;
class JIC_MMR6_ENC   : JMP_IDX_COMPACT_FM<0b101000>;
class JRC16_MMR6_ENC: POOL16C_JALRC_FM_MM16R6<0x3>;
class JRCADDIUSP_MMR6_ENC : POOL16C_JRCADDIUSP_FM_MM16R6<0x13>;
class LSA_MMR6_ENC : POOL32A_LSA_FM<0b001111>;
class LWPC_MMR6_ENC  : PCREL19_FM_MMR6<0b01>;
class LWM16_MMR6_ENC : POOL16C_LWM_SWM_FM_MM16R6<0x2>;
class MFC0_MMR6_ENC : POOL32A_MFTC0_FM_MMR6<"mfc0", 0b00011, 0b111100>;
class MFC1_MMR6_ENC : POOL32F_MFTC1_FM_MMR6<"mfc1", 0b10000000>;
class MFC2_MMR6_ENC : POOL32A_MFTC2_FM_MMR6<"mfc2", 0b0100110100>;
class MFHC0_MMR6_ENC : POOL32A_MFTC0_FM_MMR6<"mfhc0", 0b00011, 0b110100>;
class MFHC2_MMR6_ENC : POOL32A_MFTC2_FM_MMR6<"mfhc2", 0b1000110100>;
class MOD_MMR6_ENC : ARITH_FM_MMR6<"mod", 0x158>;
class MODU_MMR6_ENC : ARITH_FM_MMR6<"modu", 0x1d8>;
class MUL_MMR6_ENC : ARITH_FM_MMR6<"mul", 0x18>;
class MUH_MMR6_ENC : ARITH_FM_MMR6<"muh", 0x58>;
class MULU_MMR6_ENC : ARITH_FM_MMR6<"mulu", 0x98>;
class MUHU_MMR6_ENC : ARITH_FM_MMR6<"muhu", 0xd8>;
class MTC0_MMR6_ENC : POOL32A_MFTC0_FM_MMR6<"mtc0", 0b01011, 0b111100>;
class MTC1_MMR6_ENC : POOL32F_MFTC1_FM_MMR6<"mtc1", 0b10100000>;
class MTC2_MMR6_ENC : POOL32A_MFTC2_FM_MMR6<"mtc2", 0b0101110100>;
class MTHC0_MMR6_ENC : POOL32A_MFTC0_FM_MMR6<"mthc0", 0b01011, 0b110100>;
class MTHC2_MMR6_ENC : POOL32A_MFTC2_FM_MMR6<"mthc2", 0b1001110100>;
class NOR_MMR6_ENC : ARITH_FM_MMR6<"nor", 0x2d0>;
class OR_MMR6_ENC : ARITH_FM_MMR6<"or", 0x290>;
class ORI_MMR6_ENC : ADDI_FM_MMR6<"ori", 0x14>;
class PREF_MMR6_ENC : CACHE_PREF_FM_MMR6<0b011000, 0b0010>;
class SB16_MMR6_ENC : LOAD_STORE_FM_MM16<0x22>;
class SELEQZ_MMR6_ENC : POOL32A_FM_MMR6<0b0101000000>;
class SELNEZ_MMR6_ENC : POOL32A_FM_MMR6<0b0110000000>;
class SH16_MMR6_ENC : LOAD_STORE_FM_MM16<0x2a>;
class SLL_MMR6_ENC : SHIFT_MMR6_ENC<"sll", 0x00, 0b0>;
class SUB_MMR6_ENC : ARITH_FM_MMR6<"sub", 0x190>;
class SUBU_MMR6_ENC : ARITH_FM_MMR6<"subu", 0x1d0>;
class SW_MMR6_ENC : SW32_FM_MMR6<"sw", 0x3e>;
class SW16_MMR6_ENC : LOAD_STORE_FM_MM16<0x3a>;
class SWM16_MMR6_ENC : POOL16C_LWM_SWM_FM_MM16R6<0xa>;
class SWSP_MMR6_ENC : LOAD_STORE_SP_FM_MM16<0x32>;
class WRPGPR_MMR6_ENC : POOL32A_WRPGPR_WSBH_FM_MMR6<"wrpgpr", 0x3c5>;
class WSBH_MMR6_ENC : POOL32A_WRPGPR_WSBH_FM_MMR6<"wsbh", 0x1ec>;
class LB_MMR6_ENC : LB32_FM_MMR6;
class LBU_MMR6_ENC : LBU32_FM_MMR6;
class PAUSE_MMR6_ENC : POOL32A_PAUSE_FM_MMR6<"pause", 0b00101>;
class RDHWR_MMR6_ENC : POOL32A_RDHWR_FM_MMR6;
class WAIT_MMR6_ENC : WAIT_FM_MM, MMR6Arch<"wait">;
class SSNOP_MMR6_ENC : BARRIER_FM_MM<0x1>, MMR6Arch<"ssnop">;
class SYNC_MMR6_ENC : POOL32A_SYNC_FM_MMR6;
class SYNCI_MMR6_ENC : POOL32I_SYNCI_FM_MMR6, MMR6Arch<"synci">;
class RDPGPR_MMR6_ENC : POOL32A_RDPGPR_FM_MMR6<0b1110000101>;
class SDBBP_MMR6_ENC : SDBBP_FM_MM, MMR6Arch<"sdbbp">;
class SIGRIE_MMR6_ENC : SIGRIE_FM_MM, MMR6Arch<"sigrie">;
class XOR_MMR6_ENC : ARITH_FM_MMR6<"xor", 0x310>;
class XORI_MMR6_ENC : ADDI_FM_MMR6<"xori", 0x1c>;
class ABS_S_MMR6_ENC : POOL32F_ABS_FM_MMR6<"abs.s", 0, 0b0001101>;
class ABS_D_MMR6_ENC : POOL32F_ABS_FM_MMR6<"abs.d", 1, 0b0001101>;
class FLOOR_L_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"floor.l.s", 0, 0b00001100>;
class FLOOR_L_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"floor.l.d", 1, 0b00001100>;
class FLOOR_W_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"floor.w.s", 0, 0b00101100>;
class FLOOR_W_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"floor.w.d", 1, 0b00101100>;
class CEIL_L_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"ceil.l.s", 0, 0b01001100>;
class CEIL_L_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"ceil.l.d", 1, 0b01001100>;
class CEIL_W_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"ceil.w.s", 0, 0b01101100>;
class CEIL_W_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"ceil.w.d", 1, 0b01101100>;
class TRUNC_L_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"trunc.l.s", 0, 0b10001100>;
class TRUNC_L_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"trunc.l.d", 1, 0b10001100>;
class TRUNC_W_S_MMR6_ENC : POOL32F_MATH_FM_MMR6<"trunc.w.s", 0, 0b10101100>;
class TRUNC_W_D_MMR6_ENC : POOL32F_MATH_FM_MMR6<"trunc.w.d", 1, 0b10101100>;
class SB_MMR6_ENC : SB32_SH32_STORE_FM_MMR6<0b000110>;
class SH_MMR6_ENC : SB32_SH32_STORE_FM_MMR6<0b001110>;
class LW_MMR6_ENC : LOAD_WORD_FM_MMR6;
class LUI_MMR6_ENC : LOAD_UPPER_IMM_FM_MMR6;
class JALRC_HB_MMR6_ENC : POOL32A_JALRC_FM_MMR6<"jalrc.hb", 0b0001111100>;
class RINT_S_MMR6_ENC : POOL32F_RINT_FM_MMR6<"rint.s", 0>;
class RINT_D_MMR6_ENC : POOL32F_RINT_FM_MMR6<"rint.d", 1>;
class ROUND_L_S_MMR6_ENC : POOL32F_RECIP_ROUND_FM_MMR6<"round.l.s", 0,
                                                       0b11001100>;
class ROUND_L_D_MMR6_ENC : POOL32F_RECIP_ROUND_FM_MMR6<"round.l.d", 1,
                                                       0b11001100>;
class ROUND_W_S_MMR6_ENC : POOL32F_RECIP_ROUND_FM_MMR6<"round.w.s", 0,
                                                       0b11101100>;
class ROUND_W_D_MMR6_ENC : POOL32F_RECIP_ROUND_FM_MMR6<"round.w.d", 1,
                                                       0b11101100>;
class SEL_S_MMR6_ENC : POOL32F_SEL_FM_MMR6<"sel.s", 0, 0b010111000>;
class SEL_D_MMR6_ENC : POOL32F_SEL_FM_MMR6<"sel.d", 1, 0b010111000>;
class SELEQZ_S_MMR6_ENC : POOL32F_SEL_FM_MMR6<"seleqz.s", 0, 0b000111000>;
class SELEQZ_D_MMR6_ENC : POOL32F_SEL_FM_MMR6<"seleqz.d", 1, 0b000111000>;
class SELNEZ_S_MMR6_ENC : POOL32F_SEL_FM_MMR6<"selnez.s", 0, 0b001111000>;
class SELNEZ_D_MMR6_ENC : POOL32F_SEL_FM_MMR6<"selnez.d", 1, 0b001111000>;
class CLASS_S_MMR6_ENC : POOL32F_CLASS_FM_MMR6<"class.s", 0, 0b001100000>;
class CLASS_D_MMR6_ENC : POOL32F_CLASS_FM_MMR6<"class.d", 1, 0b001100000>;
class EXT_MMR6_ENC : POOL32A_EXT_INS_FM_MMR6<"ext", 0b101100>;
class INS_MMR6_ENC : POOL32A_EXT_INS_FM_MMR6<"ins", 0b001100>;
class JALRC_MMR6_ENC : POOL32A_JALRC_FM_MMR6<"jalrc", 0b0000111100>;
class BOVC_MMR6_ENC : POP35_BOVC_FM_MMR6<"bovc">;
class BNVC_MMR6_ENC : POP37_BNVC_FM_MMR6<"bnvc">;
class ADDU16_MMR6_ENC : POOL16A_ADDU16_FM_MMR6;
class AND16_MMR6_ENC : POOL16C_AND16_FM_MMR6;
class ANDI16_MMR6_ENC : ANDI_FM_MM16<0b001011>;
class NOT16_MMR6_ENC : POOL16C_NOT16_FM_MMR6;
class OR16_MMR6_ENC : POOL16C_OR16_XOR16_FM_MMR6<0b1001>;
class SLL16_MMR6_ENC : SHIFT_FM_MM16<0>;
class SRL16_MMR6_ENC : SHIFT_FM_MM16<1>;
class BREAK16_MMR6_ENC : POOL16C_BREAKPOINT_FM_MMR6<0b011011>;
class LI16_MMR6_ENC : LI_FM_MM16;
class MOVE16_MMR6_ENC : MOVE_FM_MM16<0b000011>;
class MOVEP_MMR6_ENC  : POOL16C_MOVEP16_FM_MMR6;
class SDBBP16_MMR6_ENC : POOL16C_BREAKPOINT_FM_MMR6<0b111011>;
class SUBU16_MMR6_ENC : POOL16A_SUBU16_FM_MMR6;
class XOR16_MMR6_ENC : POOL16C_OR16_XOR16_FM_MMR6<0b1000>;
class TLBINV_MMR6_ENC : POOL32A_TLBINV_FM_MMR6<"tlbinv", 0x10d>;
class TLBINVF_MMR6_ENC : POOL32A_TLBINV_FM_MMR6<"tlbinvf", 0x14d>;
class DVP_MMR6_ENC : POOL32A_DVPEVP_FM_MMR6<"dvp", 0b0001100101>;
class EVP_MMR6_ENC : POOL32A_DVPEVP_FM_MMR6<"evp", 0b0011100101>;
class BC1EQZC_MMR6_ENC : POOL32I_BRANCH_COP_1_2_FM_MMR6<"bc1eqzc", 0b01000>;
class BC1NEZC_MMR6_ENC : POOL32I_BRANCH_COP_1_2_FM_MMR6<"bc1nezc", 0b01001>;
class BC2EQZC_MMR6_ENC : POOL32I_BRANCH_COP_1_2_FM_MMR6<"bc2eqzc", 0b01010>;
class BC2NEZC_MMR6_ENC : POOL32I_BRANCH_COP_1_2_FM_MMR6<"bc2nezc", 0b01011>;
class LDC1_MMR6_ENC : LDWC1_SDWC1_FM_MMR6<"ldc1", 0b101111>;
class SDC1_MMR6_ENC : LDWC1_SDWC1_FM_MMR6<"sdc1", 0b101110>;
class LDC2_MMR6_ENC : POOL32B_LDWC2_SDWC2_FM_MMR6<"ldc2", 0b0010>;
class SDC2_MMR6_ENC : POOL32B_LDWC2_SDWC2_FM_MMR6<"sdc2", 0b1010>;
class LWC2_MMR6_ENC : POOL32B_LDWC2_SDWC2_FM_MMR6<"lwc2", 0b0000>;
class SWC2_MMR6_ENC : POOL32B_LDWC2_SDWC2_FM_MMR6<"swc2", 0b1000>;

class LL_MMR6_ENC  : POOL32C_LL_E_SC_E_FM_MMR6<"ll", 0b0011, 0b000>;
class SC_MMR6_ENC  : POOL32C_LL_E_SC_E_FM_MMR6<"sc", 0b1011, 0b000>;

/// Floating Point Instructions
class FADD_S_MMR6_ENC : POOL32F_ARITH_FM_MMR6<"add.s", 0, 0b00110000>;
class FSUB_S_MMR6_ENC : POOL32F_ARITH_FM_MMR6<"sub.s", 0, 0b01110000>;
class FMUL_S_MMR6_ENC : POOL32F_ARITH_FM_MMR6<"mul.s", 0, 0b10110000>;
class FDIV_S_MMR6_ENC : POOL32F_ARITH_FM_MMR6<"div.s", 0, 0b11110000>;
class MADDF_S_MMR6_ENC : POOL32F_ARITHF_FM_MMR6<"maddf.s", 0, 0b110111000>;
class MADDF_D_MMR6_ENC : POOL32F_ARITHF_FM_MMR6<"maddf.d", 1, 0b110111000>;
class MSUBF_S_MMR6_ENC : POOL32F_ARITHF_FM_MMR6<"msubf.s", 0, 0b111111000>;
class MSUBF_D_MMR6_ENC : POOL32F_ARITHF_FM_MMR6<"msubf.d", 1, 0b111111000>;
class FMOV_S_MMR6_ENC : POOL32F_MOV_NEG_FM_MMR6<"mov.s", 0, 0b0000001>;
class FMOV_D_MMR6_ENC : POOL32F_MOV_NEG_FM_MMR6<"mov.d", 1, 0b0000001>;
class FNEG_S_MMR6_ENC : POOL32F_MOV_NEG_FM_MMR6<"neg.s", 0, 0b0101101>;
class MAX_S_MMR6_ENC : POOL32F_MINMAX_FM<"max.s", 0, 0b000001011>;
class MAX_D_MMR6_ENC : POOL32F_MINMAX_FM<"max.d", 1, 0b000001011>;
class MAXA_S_MMR6_ENC : POOL32F_MINMAX_FM<"maxa.s", 0, 0b000101011>;
class MAXA_D_MMR6_ENC : POOL32F_MINMAX_FM<"maxa.d", 1, 0b000101011>;
class MIN_S_MMR6_ENC : POOL32F_MINMAX_FM<"min.s", 0, 0b000000011>;
class MIN_D_MMR6_ENC : POOL32F_MINMAX_FM<"min.d", 1, 0b000000011>;
class MINA_S_MMR6_ENC : POOL32F_MINMAX_FM<"mina.s", 0, 0b000100011>;
class MINA_D_MMR6_ENC : POOL32F_MINMAX_FM<"mina.d", 1, 0b000100011>;

class CVT_L_S_MMR6_ENC : POOL32F_CVT_LW_FM<"cvt.l.s", 0, 0b00000100>;
class CVT_L_D_MMR6_ENC : POOL32F_CVT_LW_FM<"cvt.l.d", 1, 0b00000100>;
class CVT_W_S_MMR6_ENC : POOL32F_CVT_LW_FM<"cvt.w.s", 0, 0b00100100>;
class CVT_D_L_MMR6_ENC : POOL32F_CVT_DS_FM<"cvt.d.l", 2, 0b1001101>;
class CVT_S_W_MMR6_ENC : POOL32F_CVT_DS_FM<"cvt.s.w", 1, 0b1101101>;
class CVT_S_L_MMR6_ENC : POOL32F_CVT_DS_FM<"cvt.s.l", 2, 0b1101101>;

//===----------------------------------------------------------------------===//
//
// Instruction Descriptions
//
//===----------------------------------------------------------------------===//

class CMP_CBR_RT_Z_MMR6_DESC_BASE<string instr_asm, DAGOperand opnd,
                                  RegisterOperand GPROpnd>
    : BRANCH_DESC_BASE {
  dag InOperandList = (ins GPROpnd:$rt, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCZC;
}

class BEQZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"beqzalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BGEZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bgezalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BGTZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bgtzalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BLEZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"blezalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BLTZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bltzalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BNEZALC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bnezalc", brtarget_mm,
                                                      GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BLTZC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bltzc", brtarget_lsl2_mm,
                                                    GPR32Opnd>;
class BLEZC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"blezc", brtarget_lsl2_mm,
                                                    GPR32Opnd>;
class BGEZC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bgezc", brtarget_lsl2_mm,
                                                    GPR32Opnd>;
class BGTZC_MMR6_DESC : CMP_CBR_RT_Z_MMR6_DESC_BASE<"bgtzc", brtarget_lsl2_mm,
                                                    GPR32Opnd>;

class CMP_CBR_2R_MMR6_DESC_BASE<string instr_asm, DAGOperand opnd,
                                RegisterOperand GPROpnd> : BRANCH_DESC_BASE {
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rs, $rt, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCC;
}

class BGEC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"bgec", brtarget_lsl2_mm,
                                                 GPR32Opnd>;
class BGEUC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"bgeuc", brtarget_lsl2_mm,
                                                 GPR32Opnd>;
class BLTC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"bltc", brtarget_lsl2_mm,
                                                 GPR32Opnd>;
class BLTUC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"bltuc", brtarget_lsl2_mm,
                                                 GPR32Opnd>;
class BEQC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"beqc", brtarget_lsl2_mm,
                                                 GPR32Opnd>;
class BNEC_MMR6_DESC : CMP_CBR_2R_MMR6_DESC_BASE<"bnec", brtarget_lsl2_mm,
                                                 GPR32Opnd>;

class ADD_MMR6_DESC : ArithLogicR<"add", GPR32Opnd, 1, II_ADD>;
class ADDIU_MMR6_DESC
    : ArithLogicI<"addiu", simm16, GPR32Opnd, II_ADDIU, immSExt16, add>;
class ADDU_MMR6_DESC : ArithLogicR<"addu", GPR32Opnd, 1, II_ADDU>;
class MUL_MMR6_DESC : ArithLogicR<"mul", GPR32Opnd, 1, II_MUL, mul>;
class MUH_MMR6_DESC : ArithLogicR<"muh", GPR32Opnd, 1, II_MUH, mulhs>;
class MULU_MMR6_DESC : ArithLogicR<"mulu", GPR32Opnd, 1, II_MULU>;
class MUHU_MMR6_DESC : ArithLogicR<"muhu", GPR32Opnd, 1, II_MUHU, mulhu>;

class BC_MMR6_DESC_BASE<string instr_asm, DAGOperand opnd, InstrItinClass Itin>
    : BRANCH_DESC_BASE, MMR6Arch<instr_asm> {
  dag InOperandList = (ins opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$offset");
  bit isBarrier = 1;
  InstrItinClass Itinerary = Itin;
}

class BALC_MMR6_DESC : BC_MMR6_DESC_BASE<"balc", brtarget26_mm, II_BALC> {
  bit isCall = 1;
  list<Register> Defs = [RA];
}
class BC_MMR6_DESC : BC_MMR6_DESC_BASE<"bc", brtarget26_mm, II_BC> {
  list<dag> Pattern = [(br bb:$offset)];
}

class BC16_MMR6_DESC : MicroMipsInst16<(outs), (ins brtarget10_mm:$offset),
                                       !strconcat("bc16", "\t$offset"), [],
                                       II_BC, FrmI>,
                       MMR6Arch<"bc16"> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let hasDelaySlot = 0;
  let AdditionalPredicates = [RelocPIC];
  let Defs = [AT];
}

class BEQZC_BNEZC_MM16R6_DESC_BASE<string instr_asm>
    : CBranchZeroMM<instr_asm, brtarget7_mm, GPRMM16Opnd>,
      MMR6Arch<instr_asm> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 0;
  let Defs = [AT];
}
class BEQZC16_MMR6_DESC : BEQZC_BNEZC_MM16R6_DESC_BASE<"beqzc16">;
class BNEZC16_MMR6_DESC : BEQZC_BNEZC_MM16R6_DESC_BASE<"bnezc16">;

class SUB_MMR6_DESC : ArithLogicR<"sub", GPR32Opnd, 0, II_SUB>;
class SUBU_MMR6_DESC : ArithLogicR<"subu", GPR32Opnd, 0,II_SUBU>;

class BITSWAP_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd>
    : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_BITSWAP;
}

class BITSWAP_MMR6_DESC : BITSWAP_MMR6_DESC_BASE<"bitswap", GPR32Opnd>;

class BRK_MMR6_DESC : BRK_FT<"break">;

class CACHE_HINT_MMR6_DESC<string instr_asm, Operand MemOpnd,
                           InstrItinClass Itin>
      : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins MemOpnd:$addr, uimm5:$hint);
  string AsmString = !strconcat(instr_asm, "\t$hint, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeCacheOpMM";
  InstrItinClass Itinerary = Itin;
}

class CACHE_MMR6_DESC : CACHE_HINT_MMR6_DESC<"cache", mem_mm_12, II_CACHE>;
class PREF_MMR6_DESC : CACHE_HINT_MMR6_DESC<"pref", mem_mm_12, II_PREF>;

class LB_LBU_MMR6_DESC_BASE<string instr_asm, Operand MemOpnd,
                            RegisterOperand GPROpnd, InstrItinClass Itin>
    : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins MemOpnd:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  string DecoderMethod = "DecodeLoadByte15";
  bit mayLoad = 1;
  InstrItinClass Itinerary = Itin;
}
class LB_MMR6_DESC : LB_LBU_MMR6_DESC_BASE<"lb", mem_mm_16, GPR32Opnd, II_LB>;
class LBU_MMR6_DESC : LB_LBU_MMR6_DESC_BASE<"lbu", mem_mm_16, GPR32Opnd,
                                            II_LBU>;

class CLO_CLZ_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                             InstrItinClass Itin> : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins GPROpnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs");
  InstrItinClass Itinerary = Itin;
}

class CLO_MMR6_DESC : CLO_CLZ_MMR6_DESC_BASE<"clo", GPR32Opnd, II_CLO>;
class CLZ_MMR6_DESC : CLO_CLZ_MMR6_DESC_BASE<"clz", GPR32Opnd, II_CLZ>;

class EHB_MMR6_DESC : Barrier<"ehb", II_EHB>;
class EI_MMR6_DESC : DEI_FT<"ei", GPR32Opnd, II_EI>;
class DI_MMR6_DESC : DEI_FT<"di", GPR32Opnd, II_DI>;

class ERET_MMR6_DESC : ER_FT<"eret", II_ERET>;
class DERET_MMR6_DESC : ER_FT<"deret", II_DERET>;
class ERETNC_MMR6_DESC : ER_FT<"eretnc", II_ERETNC>;

class JALRC16_MMR6_DESC_BASE<string opstr, RegisterOperand RO>
    : MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
                      [(MipsJmpLink RO:$rs)], II_JALR, FrmR>,
      MMR6Arch<opstr> {
  let isCall = 1;
  let hasDelaySlot = 0;
  let Defs = [RA];
  let hasPostISelHook = 1;
}
class JALRC16_MMR6_DESC : JALRC16_MMR6_DESC_BASE<"jalr", GPR32Opnd>;

class JMP_MMR6_IDX_COMPACT_DESC_BASE<string opstr, DAGOperand opnd,
                                     RegisterOperand GPROpnd,
                                     InstrItinClass Itin>
    : MMR6Arch<opstr> {
  dag InOperandList = (ins GPROpnd:$rt, opnd:$offset);
  string AsmString = !strconcat(opstr, "\t$rt, $offset");
  list<dag> Pattern = [];
  bit isTerminator = 1;
  bit hasDelaySlot = 0;
  InstrItinClass Itinerary = Itin;
}

class JIALC_MMR6_DESC : JMP_MMR6_IDX_COMPACT_DESC_BASE<"jialc", calloffset16,
                                                       GPR32Opnd, II_JIALC> {
  bit isCall = 1;
  list<Register> Defs = [RA];
}

class JIC_MMR6_DESC : JMP_MMR6_IDX_COMPACT_DESC_BASE<"jic", jmpoffset16,
                                                     GPR32Opnd, II_JIC> {
  bit isBarrier = 1;
  list<Register> Defs = [AT];
}

class JRC16_MMR6_DESC_BASE<string opstr, RegisterOperand RO>
    : MicroMipsInst16<(outs), (ins RO:$rs), !strconcat(opstr, "\t$rs"),
                      [], II_JR, FrmR>,
      MMR6Arch<opstr> {
  let hasDelaySlot = 0;
  let isBranch = 1;
  let isIndirectBranch = 1;
}
class JRC16_MMR6_DESC : JRC16_MMR6_DESC_BASE<"jrc16", GPR32Opnd>;

class JRCADDIUSP_MMR6_DESC
    : MicroMipsInst16<(outs), (ins uimm5_lsl2:$imm), "jrcaddiusp\t$imm",
                      [], II_JRADDIUSP, FrmR>,
      MMR6Arch<"jrcaddiusp"> {
  let hasDelaySlot = 0;
  let isTerminator = 1;
  let isBarrier = 1;
  let isBranch = 1;
  let isIndirectBranch = 1;
}

class ALIGN_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                      Operand ImmOpnd, InstrItinClass Itin>
    : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, ImmOpnd:$bp);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt, $bp");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class ALIGN_MMR6_DESC : ALIGN_MMR6_DESC_BASE<"align", GPR32Opnd, uimm2,
                                             II_ALIGN>;

class AUI_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                         InstrItinClass Itin> : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins GPROpnd:$rs, uimm16:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class AUI_MMR6_DESC : AUI_MMR6_DESC_BASE<"aui", GPR32Opnd, II_AUI>;

class ALUIPC_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                            InstrItinClass Itin> : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins simm16:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rt, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class ALUIPC_MMR6_DESC : ALUIPC_MMR6_DESC_BASE<"aluipc", GPR32Opnd, II_ALUIPC>;
class AUIPC_MMR6_DESC : ALUIPC_MMR6_DESC_BASE<"auipc", GPR32Opnd, II_AUIPC>;

class LSA_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                         Operand ImmOpnd, InstrItinClass Itin>
    : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, ImmOpnd:$imm2);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $rd, $imm2");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class LSA_MMR6_DESC : LSA_MMR6_DESC_BASE<"lsa", GPR32Opnd, uimm2_plus1, II_LSA>;

class PCREL_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                           Operand ImmOpnd, InstrItinClass Itin>
    : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins ImmOpnd:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rt, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class ADDIUPC_MMR6_DESC : PCREL_MMR6_DESC_BASE<"addiupc", GPR32Opnd,
                                               simm19_lsl2, II_ADDIUPC>;
class LWPC_MMR6_DESC: PCREL_MMR6_DESC_BASE<"lwpc", GPR32Opnd, simm19_lsl2,
                                           II_LWPC>;

class SELEQNE_Z_MMR6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                               InstrItinClass Itin> : MMR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class SELEQZ_MMR6_DESC : SELEQNE_Z_MMR6_DESC_BASE<"seleqz", GPR32Opnd,
                                                  II_SELCCZ>;
class SELNEZ_MMR6_DESC : SELEQNE_Z_MMR6_DESC_BASE<"selnez", GPR32Opnd,
                                                  II_SELCCZ>;
class PAUSE_MMR6_DESC : Barrier<"pause", II_PAUSE>;
class RDHWR_MMR6_DESC : MMR6Arch<"rdhwr">, MipsR6Inst {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins HWRegsOpnd:$rs, uimm3:$sel);
  string AsmString = !strconcat("rdhwr", "\t$rt, $rs, $sel");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_RDHWR;
  Format Form = FrmR;
}

class WAIT_MMR6_DESC : WaitMM<"wait">;
// FIXME: ssnop should not be defined for R6. Per MD000582 microMIPS32 6.03:
//        Assemblers targeting specifically Release 6 should reject the SSNOP
//        instruction with an error.
class SSNOP_MMR6_DESC : Barrier<"ssnop", II_SSNOP>;
class SLL_MMR6_DESC : shift_rotate_imm<"sll", uimm5, GPR32Opnd, II_SLL>;

class DIVMOD_MMR6_DESC_BASE<string opstr, RegisterOperand GPROpnd,
                            InstrItinClass Itin,
                            SDPatternOperator OpNode=null_frag>
    : MipsR6Inst {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(opstr, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set GPROpnd:$rd, (OpNode GPROpnd:$rs, GPROpnd:$rt))];
  string BaseOpcode = opstr;
  Format f = FrmR;
  let isCommutable = 0;
  let isReMaterializable = 1;
  InstrItinClass Itinerary = Itin;

  // This instruction doesn't trap division by zero itself. We must insert
  // teq instructions as well.
  bit usesCustomInserter = 1;
}
class DIV_MMR6_DESC  : DIVMOD_MMR6_DESC_BASE<"div", GPR32Opnd, II_DIV, sdiv>;
class DIVU_MMR6_DESC : DIVMOD_MMR6_DESC_BASE<"divu", GPR32Opnd, II_DIVU, udiv>;
class MOD_MMR6_DESC  : DIVMOD_MMR6_DESC_BASE<"mod", GPR32Opnd, II_MOD, srem>;
class MODU_MMR6_DESC : DIVMOD_MMR6_DESC_BASE<"modu", GPR32Opnd, II_MODU, urem>;
class AND_MMR6_DESC : ArithLogicR<"and", GPR32Opnd, 1, II_AND, and>;
class ANDI_MMR6_DESC : ArithLogicI<"andi", uimm16, GPR32Opnd, II_ANDI>;
class NOR_MMR6_DESC : LogicNOR<"nor", GPR32Opnd>;
class OR_MMR6_DESC : ArithLogicR<"or", GPR32Opnd, 1, II_OR, or>;
class ORI_MMR6_DESC : ArithLogicI<"ori", uimm16, GPR32Opnd, II_ORI, immZExt16,
                                  or> {
  int AddedComplexity = 1;
}
class XOR_MMR6_DESC : ArithLogicR<"xor", GPR32Opnd, 1, II_XOR, xor>;
class XORI_MMR6_DESC : ArithLogicI<"xori", uimm16, GPR32Opnd, II_XORI,
                                   immZExt16, xor>;
class SW_MMR6_DESC : Store<"sw", GPR32Opnd> {
  InstrItinClass Itinerary = II_SW;
}
class WRPGPR_WSBH_MMR6_DESC_BASE<string instr_asm, RegisterOperand RO,
                                 InstrItinClass Itin> {
  dag InOperandList = (ins RO:$rs);
  dag OutOperandList = (outs RO:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs");
  list<dag> Pattern = [];
  Format f = FrmR;
  string BaseOpcode = instr_asm;
  bit hasSideEffects = 0;
  InstrItinClass Itinerary = Itin;
}
class WRPGPR_MMR6_DESC : WRPGPR_WSBH_MMR6_DESC_BASE<"wrpgpr", GPR32Opnd,
                                                    II_WRPGPR>;
class WSBH_MMR6_DESC : WRPGPR_WSBH_MMR6_DESC_BASE<"wsbh", GPR32Opnd, II_WSBH>;

class MTC0_MMR6_DESC_BASE<string opstr, RegisterOperand DstRC,
                         RegisterOperand SrcRC, InstrItinClass Itin> {
  dag InOperandList = (ins SrcRC:$rt, uimm3:$sel);
  dag OutOperandList = (outs DstRC:$rs);
  string AsmString = !strconcat(opstr, "\t$rt, $rs, $sel");
  list<dag> Pattern = [];
  Format f = FrmFR;
  string BaseOpcode = opstr;
  InstrItinClass Itinerary = Itin;
}
class MTC1_MMR6_DESC_BASE<
      string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
      InstrItinClass Itin = NoItinerary, SDPatternOperator OpNode = null_frag>
      : MipsR6Inst {
  dag InOperandList = (ins SrcRC:$rt);
  dag OutOperandList = (outs DstRC:$fs);
  string AsmString = !strconcat(opstr, "\t$rt, $fs");
  list<dag> Pattern = [(set DstRC:$fs, (OpNode SrcRC:$rt))];
  Format f = FrmFR;
  InstrItinClass Itinerary = Itin;
  string BaseOpcode = opstr;
}
class MTC1_64_MMR6_DESC_BASE<
      string opstr, RegisterOperand DstRC, RegisterOperand SrcRC,
      InstrItinClass Itin = NoItinerary> : MipsR6Inst {
  dag InOperandList = (ins DstRC:$fs_in, SrcRC:$rt);
  dag OutOperandList = (outs DstRC:$fs);
  string AsmString = !strconcat(opstr, "\t$rt, $fs");
  list<dag> Pattern = [];
  Format f = FrmFR;
  InstrItinClass Itinerary = Itin;
  string BaseOpcode = opstr;
  // $fs_in is part of a white lie to work around a widespread bug in the FPU
  // implementation. See expandBuildPairF64 for details.
  let Constraints = "$fs = $fs_in";
}
class MTC2_MMR6_DESC_BASE<string opstr, RegisterOperand DstRC,
                         RegisterOperand SrcRC, InstrItinClass Itin> {
  dag InOperandList = (ins SrcRC:$rt);
  dag OutOperandList = (outs DstRC:$impl);
  string AsmString = !strconcat(opstr, "\t$rt, $impl");
  list<dag> Pattern = [];
  Format f = FrmFR;
  string BaseOpcode = opstr;
  InstrItinClass Itinerary = Itin;
}

class MTC0_MMR6_DESC : MTC0_MMR6_DESC_BASE<"mtc0", COP0Opnd, GPR32Opnd,
                                           II_MTC0>;
class MTC1_MMR6_DESC : MTC1_MMR6_DESC_BASE<"mtc1", FGR32Opnd, GPR32Opnd,
                                           II_MTC1, bitconvert>, HARDFLOAT;
class MTC2_MMR6_DESC : MTC2_MMR6_DESC_BASE<"mtc2", COP2Opnd, GPR32Opnd,
                                           II_MTC2>;
class MTHC0_MMR6_DESC : MTC0_MMR6_DESC_BASE<"mthc0", COP0Opnd, GPR32Opnd,
                                            II_MTHC0>;
class MTHC2_MMR6_DESC : MTC2_MMR6_DESC_BASE<"mthc2", COP2Opnd, GPR32Opnd,
                                            II_MTC2>;

class MFC0_MMR6_DESC_BASE<string opstr, RegisterOperand DstRC,
                          RegisterOperand SrcRC, InstrItinClass Itin> {
  dag InOperandList = (ins SrcRC:$rs, uimm3:$sel);
  dag OutOperandList = (outs DstRC:$rt);
  string AsmString = !strconcat(opstr, "\t$rt, $rs, $sel");
  list<dag> Pattern = [];
  Format f = FrmFR;
  string BaseOpcode = opstr;
  InstrItinClass Itinerary = Itin;
}
class MFC1_MMR6_DESC_BASE<string opstr, RegisterOperand DstRC,
                          RegisterOperand SrcRC,
                          InstrItinClass Itin = NoItinerary,
                          SDPatternOperator OpNode = null_frag> : MipsR6Inst {
  dag InOperandList = (ins SrcRC:$fs);
  dag OutOperandList = (outs DstRC:$rt);
  string AsmString = !strconcat(opstr, "\t$rt, $fs");
  list<dag> Pattern = [(set DstRC:$rt, (OpNode SrcRC:$fs))];
  Format f = FrmFR;
  InstrItinClass Itinerary = Itin;
  string BaseOpcode = opstr;
}
class MFC2_MMR6_DESC_BASE<string opstr, RegisterOperand DstRC,
                          RegisterOperand SrcRC, InstrItinClass Itin> {
  dag InOperandList = (ins SrcRC:$impl);
  dag OutOperandList = (outs DstRC:$rt);
  string AsmString = !strconcat(opstr, "\t$rt, $impl");
  list<dag> Pattern = [];
  Format f = FrmFR;
  string BaseOpcode = opstr;
  InstrItinClass Itinerary = Itin;
}
class MFC0_MMR6_DESC : MFC0_MMR6_DESC_BASE<"mfc0", GPR32Opnd, COP0Opnd,
                                           II_MFC0>;
class MFC1_MMR6_DESC : MFC1_MMR6_DESC_BASE<"mfc1", GPR32Opnd, FGR32Opnd,
                                           II_MFC1, bitconvert>, HARDFLOAT;
class MFC2_MMR6_DESC : MFC2_MMR6_DESC_BASE<"mfc2", GPR32Opnd, COP2Opnd,
                                           II_MFC2>;
class MFHC0_MMR6_DESC : MFC0_MMR6_DESC_BASE<"mfhc0", GPR32Opnd, COP0Opnd,
                                            II_MFHC0>;
class MFHC2_MMR6_DESC : MFC2_MMR6_DESC_BASE<"mfhc2", GPR32Opnd, COP2Opnd,
                                            II_MFC2>;

class LDC1_D64_MMR6_DESC : MipsR6Inst, HARDFLOAT, FGR_64 {
  dag InOperandList = (ins mem_mm_16:$addr);
  dag OutOperandList = (outs FGR64Opnd:$ft);
  string AsmString = !strconcat("ldc1", "\t$ft, $addr");
  list<dag> Pattern = [(set FGR64Opnd:$ft, (load addrimm16:$addr))];
  Format f = FrmFI;
  InstrItinClass Itinerary = II_LDC1;
  string BaseOpcode = "ldc1";
  bit mayLoad = 1;
  let DecoderMethod = "DecodeFMemMMR2";
}

class SDC1_D64_MMR6_DESC : MipsR6Inst, HARDFLOAT, FGR_64 {
  dag InOperandList = (ins FGR64Opnd:$ft, mem_mm_16:$addr);
  dag OutOperandList = (outs);
  string AsmString = !strconcat("sdc1", "\t$ft, $addr");
  list<dag> Pattern = [(store FGR64Opnd:$ft, addrimm16:$addr)];
  Format f = FrmFI;
  InstrItinClass Itinerary = II_SDC1;
  string BaseOpcode = "sdc1";
  bit mayStore = 1;
  let DecoderMethod = "DecodeFMemMMR2";
}

class LDC2_LWC2_MMR6_DESC_BASE<string opstr, InstrItinClass itin> {
  dag OutOperandList = (outs COP2Opnd:$rt);
  dag InOperandList = (ins mem_mm_11:$addr);
  string AsmString = !strconcat(opstr, "\t$rt, $addr");
  list<dag> Pattern = [(set COP2Opnd:$rt, (load addrimm11:$addr))];
  Format f = FrmFI;
  InstrItinClass Itinerary = itin;
  string BaseOpcode = opstr;
  bit mayLoad = 1;
  string DecoderMethod = "DecodeFMemCop2MMR6";
}
class LDC2_MMR6_DESC : LDC2_LWC2_MMR6_DESC_BASE<"ldc2", II_LDC2>;
class LWC2_MMR6_DESC : LDC2_LWC2_MMR6_DESC_BASE<"lwc2", II_LWC2>;

class SDC2_SWC2_MMR6_DESC_BASE<string opstr, InstrItinClass itin> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins COP2Opnd:$rt, mem_mm_11:$addr);
  string AsmString = !strconcat(opstr, "\t$rt, $addr");
  list<dag> Pattern = [(store COP2Opnd:$rt, addrimm11:$addr)];
  Format f = FrmFI;
  InstrItinClass Itinerary = itin;
  string BaseOpcode = opstr;
  bit mayStore = 1;
  string DecoderMethod = "DecodeFMemCop2MMR6";
}
class SDC2_MMR6_DESC : SDC2_SWC2_MMR6_DESC_BASE<"sdc2", II_SDC2>;
class SWC2_MMR6_DESC : SDC2_SWC2_MMR6_DESC_BASE<"swc2", II_SWC2>;

class GINV_MMR6_DESC_BASE<string opstr,
                          RegisterOperand SrcRC, InstrItinClass Itin> {
  dag InOperandList = (ins SrcRC:$rs, uimm2:$type);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(opstr, "\t$rs, $type");
  list<dag> Pattern = [];
  Format f = FrmFR;
  string BaseOpcode = opstr;
  InstrItinClass Itinerary = Itin;
}

class GINVI_MMR6_DESC : GINV_MMR6_DESC_BASE<"ginvi", GPR32Opnd,
                                            II_GINVI> {
  bits<2> type = 0b00;
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = "ginvi\t$rs";
}
class GINVT_MMR6_DESC : GINV_MMR6_DESC_BASE<"ginvt", GPR32Opnd,
                                            II_GINVT>;

class SC_MMR6_DESC_BASE<string opstr, InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$dst);
  dag InOperandList = (ins GPR32Opnd:$rt, mem_mm_9:$addr);
  string AsmString = !strconcat(opstr, "\t$rt, $addr");
  InstrItinClass Itinerary = itin;
  string BaseOpcode = opstr;
  bit mayStore = 1;
  string Constraints = "$rt = $dst";
  string DecoderMethod = "DecodeMemMMImm9";
}

class LL_MMR6_DESC_BASE<string opstr, InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins mem_mm_9:$addr);
  string AsmString = !strconcat(opstr, "\t$rt, $addr");
  InstrItinClass Itinerary = itin;
  string BaseOpcode = opstr;
  bit mayLoad = 1;
  string DecoderMethod = "DecodeMemMMImm9";
}

class SC_MMR6_DESC : SC_MMR6_DESC_BASE<"sc", II_SC>;
class LL_MMR6_DESC : LL_MMR6_DESC_BASE<"ll", II_LL>;

/// Floating Point Instructions
class FARITH_MMR6_DESC_BASE<string instr_asm, RegisterOperand RC,
                            InstrItinClass Itin, bit isComm,
                            SDPatternOperator OpNode = null_frag> : HARDFLOAT {
  dag OutOperandList = (outs RC:$fd);
  dag InOperandList = (ins RC:$ft, RC:$fs);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [(set RC:$fd, (OpNode RC:$fs, RC:$ft))];
  InstrItinClass Itinerary = Itin;
  bit isCommutable = isComm;
}
class FADD_S_MMR6_DESC
  : FARITH_MMR6_DESC_BASE<"add.s", FGR32Opnd, II_ADD_S, 1, fadd>;
class FSUB_S_MMR6_DESC
  : FARITH_MMR6_DESC_BASE<"sub.s", FGR32Opnd, II_SUB_S, 0, fsub>;
class FMUL_S_MMR6_DESC
  : FARITH_MMR6_DESC_BASE<"mul.s", FGR32Opnd, II_MUL_S, 1, fmul>;
class FDIV_S_MMR6_DESC
  : FARITH_MMR6_DESC_BASE<"div.s", FGR32Opnd, II_DIV_S, 0, fdiv>;
class MADDF_S_MMR6_DESC : COP1_4R_DESC_BASE<"maddf.s", FGR32Opnd,
                                            II_MADDF_S>, HARDFLOAT;
class MADDF_D_MMR6_DESC : COP1_4R_DESC_BASE<"maddf.d", FGR64Opnd,
                                            II_MADDF_D>, HARDFLOAT;
class MSUBF_S_MMR6_DESC : COP1_4R_DESC_BASE<"msubf.s", FGR32Opnd,
                                            II_MSUBF_S>, HARDFLOAT;
class MSUBF_D_MMR6_DESC : COP1_4R_DESC_BASE<"msubf.d", FGR64Opnd,
                                            II_MSUBF_D>, HARDFLOAT;

class FMOV_FNEG_MMR6_DESC_BASE<string instr_asm, RegisterOperand DstRC,
                               RegisterOperand SrcRC, InstrItinClass Itin,
                               SDPatternOperator OpNode = null_frag>
                               : HARDFLOAT, NeverHasSideEffects {
  dag OutOperandList = (outs DstRC:$ft);
  dag InOperandList = (ins SrcRC:$fs);
  string AsmString = !strconcat(instr_asm, "\t$ft, $fs");
  list<dag> Pattern = [(set DstRC:$ft, (OpNode SrcRC:$fs))];
  InstrItinClass Itinerary = Itin;
  Format Form = FrmFR;
}
class FMOV_S_MMR6_DESC
  : FMOV_FNEG_MMR6_DESC_BASE<"mov.s", FGR32Opnd, FGR32Opnd, II_MOV_S>;
class FMOV_D_MMR6_DESC
  : FMOV_FNEG_MMR6_DESC_BASE<"mov.d", FGR64Opnd, FGR64Opnd, II_MOV_D>;
class FNEG_S_MMR6_DESC
  : FMOV_FNEG_MMR6_DESC_BASE<"neg.s", FGR32Opnd, FGR32Opnd, II_NEG, fneg>;

class MAX_S_MMR6_DESC : MAX_MIN_DESC_BASE<"max.s", FGR32Opnd, II_MAX_S>,
                        HARDFLOAT;
class MAX_D_MMR6_DESC : MAX_MIN_DESC_BASE<"max.d", FGR64Opnd, II_MAX_D>,
                        HARDFLOAT;
class MIN_S_MMR6_DESC : MAX_MIN_DESC_BASE<"min.s", FGR32Opnd, II_MIN_S>,
                        HARDFLOAT;
class MIN_D_MMR6_DESC : MAX_MIN_DESC_BASE<"min.d", FGR64Opnd, II_MIN_D>,
                        HARDFLOAT;

class MAXA_S_MMR6_DESC : MAX_MIN_DESC_BASE<"maxa.s", FGR32Opnd, II_MAXA_S>,
                         HARDFLOAT;
class MAXA_D_MMR6_DESC : MAX_MIN_DESC_BASE<"maxa.d", FGR64Opnd, II_MAXA_D>,
                         HARDFLOAT;
class MINA_S_MMR6_DESC : MAX_MIN_DESC_BASE<"mina.s", FGR32Opnd, II_MINA_S>,
                         HARDFLOAT;
class MINA_D_MMR6_DESC : MAX_MIN_DESC_BASE<"mina.d", FGR64Opnd, II_MINA_D>,
                         HARDFLOAT;

class CVT_MMR6_DESC_BASE<
    string instr_asm, RegisterOperand DstRC, RegisterOperand SrcRC,
    InstrItinClass Itin, SDPatternOperator OpNode = null_frag>
    : HARDFLOAT, NeverHasSideEffects {
  dag OutOperandList = (outs DstRC:$ft);
  dag InOperandList = (ins SrcRC:$fs);
  string AsmString = !strconcat(instr_asm, "\t$ft, $fs");
  list<dag> Pattern = [(set DstRC:$ft, (OpNode SrcRC:$fs))];
  InstrItinClass Itinerary = Itin;
  Format Form = FrmFR;
}

class CVT_L_S_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.l.s", FGR64Opnd, FGR32Opnd,
                                             II_CVT>;
class CVT_L_D_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.l.d", FGR64Opnd, FGR64Opnd,
                                             II_CVT>;
class CVT_W_S_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.w.s", FGR32Opnd, FGR32Opnd,
                                             II_CVT>;
class CVT_D_L_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.d.l", FGR64Opnd, FGR64Opnd,
                                             II_CVT>, FGR_64;
class CVT_S_W_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.s.w", FGR32Opnd, FGR32Opnd,
                                             II_CVT>;
class CVT_S_L_MMR6_DESC : CVT_MMR6_DESC_BASE<"cvt.s.l", FGR64Opnd, FGR32Opnd,
                                             II_CVT>, FGR_64;

multiclass CMP_CC_MMR6<bits<6> format, string Typestr,
                       RegisterOperand FGROpnd, InstrItinClass Itin> {
  def CMP_AF_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.af.", Typestr), format, FIELD_CMP_COND_AF>,
      CMP_CONDN_DESC_BASE<"af", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_UN_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.un.", Typestr), format, FIELD_CMP_COND_UN>,
      CMP_CONDN_DESC_BASE<"un", Typestr, FGROpnd, Itin, setuo>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_EQ_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.eq.", Typestr), format, FIELD_CMP_COND_EQ>,
      CMP_CONDN_DESC_BASE<"eq", Typestr, FGROpnd, Itin, setoeq>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_UEQ_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.ueq.", Typestr), format, FIELD_CMP_COND_UEQ>,
      CMP_CONDN_DESC_BASE<"ueq", Typestr, FGROpnd, Itin, setueq>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_LT_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.lt.", Typestr), format, FIELD_CMP_COND_LT>,
      CMP_CONDN_DESC_BASE<"lt", Typestr, FGROpnd, Itin, setolt>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_ULT_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.ult.", Typestr), format, FIELD_CMP_COND_ULT>,
      CMP_CONDN_DESC_BASE<"ult", Typestr, FGROpnd, Itin, setult>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_LE_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.le.", Typestr), format, FIELD_CMP_COND_LE>,
      CMP_CONDN_DESC_BASE<"le", Typestr, FGROpnd, Itin, setole>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_ULE_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.ule.", Typestr), format, FIELD_CMP_COND_ULE>,
      CMP_CONDN_DESC_BASE<"ule", Typestr, FGROpnd, Itin, setule>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SAF_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.saf.", Typestr), format, FIELD_CMP_COND_SAF>,
      CMP_CONDN_DESC_BASE<"saf", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SUN_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.sun.", Typestr), format, FIELD_CMP_COND_SUN>,
      CMP_CONDN_DESC_BASE<"sun", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SEQ_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.seq.", Typestr), format, FIELD_CMP_COND_SEQ>,
      CMP_CONDN_DESC_BASE<"seq", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SUEQ_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.sueq.", Typestr), format, FIELD_CMP_COND_SUEQ>,
      CMP_CONDN_DESC_BASE<"sueq", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SLT_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.slt.", Typestr), format, FIELD_CMP_COND_SLT>,
      CMP_CONDN_DESC_BASE<"slt", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SULT_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.sult.", Typestr), format, FIELD_CMP_COND_SULT>,
      CMP_CONDN_DESC_BASE<"sult", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SLE_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.sle.", Typestr), format, FIELD_CMP_COND_SLE>,
      CMP_CONDN_DESC_BASE<"sle", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
  def CMP_SULE_#NAME : R6MMR6Rel, POOL32F_CMP_FM<
      !strconcat("cmp.sule.", Typestr), format, FIELD_CMP_COND_SULE>,
      CMP_CONDN_DESC_BASE<"sule", Typestr, FGROpnd, Itin>, HARDFLOAT,
      ISA_MICROMIPS32R6;
}

class ABSS_FT_MMR6_DESC_BASE<string instr_asm, RegisterOperand DstRC,
                             RegisterOperand SrcRC, InstrItinClass Itin,
                             SDPatternOperator OpNode = null_frag>
    : HARDFLOAT, NeverHasSideEffects {
  dag OutOperandList = (outs DstRC:$ft);
  dag InOperandList  = (ins SrcRC:$fs);
  string AsmString   = !strconcat(instr_asm, "\t$ft, $fs");
  list<dag>  Pattern = [(set DstRC:$ft, (OpNode SrcRC:$fs))];
  InstrItinClass Itinerary = Itin;
  Format Form = FrmFR;
  list<Predicate> EncodingPredicates = [HasStdEnc];
}

class FLOOR_L_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"floor.l.s", FGR64Opnd,
                                                    FGR32Opnd, II_FLOOR>;
class FLOOR_L_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"floor.l.d", FGR64Opnd,
                                                    FGR64Opnd, II_FLOOR>;
class FLOOR_W_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"floor.w.s", FGR32Opnd,
                                                    FGR32Opnd, II_FLOOR>;
class FLOOR_W_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"floor.w.d", FGR32Opnd,
                                                    AFGR64Opnd, II_FLOOR>;
class CEIL_L_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"ceil.l.s", FGR64Opnd,
                                                   FGR32Opnd, II_CEIL>;
class CEIL_L_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"ceil.l.d", FGR64Opnd,
                                                   FGR64Opnd, II_CEIL>;
class CEIL_W_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"ceil.w.s", FGR32Opnd,
                                                   FGR32Opnd, II_CEIL>;
class CEIL_W_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"ceil.w.d", FGR32Opnd,
                                                   AFGR64Opnd, II_CEIL>;
class TRUNC_L_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"trunc.l.s", FGR64Opnd,
                                                    FGR32Opnd, II_TRUNC>;
class TRUNC_L_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"trunc.l.d", FGR64Opnd,
                                                    FGR64Opnd, II_TRUNC>;
class TRUNC_W_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"trunc.w.s", FGR32Opnd,
                                                    FGR32Opnd, II_TRUNC>;
class TRUNC_W_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"trunc.w.d", FGR32Opnd,
                                                    FGR64Opnd, II_TRUNC>;
class SQRT_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"sqrt.s", FGR32Opnd, FGR32Opnd,
                                                 II_SQRT_S, fsqrt>;
class SQRT_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"sqrt.d", AFGR64Opnd,
                                                AFGR64Opnd, II_SQRT_D, fsqrt>;
class ROUND_L_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"round.l.s", FGR64Opnd,
                                                   FGR32Opnd, II_ROUND>;
class ROUND_L_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"round.l.d", FGR64Opnd,
                                                   FGR64Opnd, II_ROUND>;
class ROUND_W_S_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"round.w.s", FGR32Opnd,
                                                   FGR32Opnd, II_ROUND>;
class ROUND_W_D_MMR6_DESC : ABSS_FT_MMR6_DESC_BASE<"round.w.d", FGR64Opnd,
                                                   FGR64Opnd, II_ROUND>;

class SEL_S_MMR6_DESC : COP1_SEL_DESC_BASE<"sel.s", FGR32Opnd, II_SEL_S>;
class SEL_D_MMR6_DESC : COP1_SEL_D_DESC_BASE<"sel.d", FGR64Opnd, II_SEL_D>;

class SELEQZ_S_MMR6_DESC : SELEQNEZ_DESC_BASE<"seleqz.s", FGR32Opnd,
                                              II_SELCCZ_S>;
class SELEQZ_D_MMR6_DESC : SELEQNEZ_DESC_BASE<"seleqz.d", FGR64Opnd,
                                              II_SELCCZ_D>;
class SELNEZ_S_MMR6_DESC : SELEQNEZ_DESC_BASE<"selnez.s", FGR32Opnd,
                                              II_SELCCZ_S>;
class SELNEZ_D_MMR6_DESC : SELEQNEZ_DESC_BASE<"selnez.d", FGR64Opnd,
                                              II_SELCCZ_D>;
class RINT_S_MMR6_DESC : CLASS_RINT_DESC_BASE<"rint.s", FGR32Opnd,
                                              II_RINT_S>;
class RINT_D_MMR6_DESC : CLASS_RINT_DESC_BASE<"rint.d", FGR64Opnd,
                                              II_RINT_S>;
class CLASS_S_MMR6_DESC  : CLASS_RINT_DESC_BASE<"class.s", FGR32Opnd,
                                              II_CLASS_S>;
class CLASS_D_MMR6_DESC  : CLASS_RINT_DESC_BASE<"class.d", FGR64Opnd,
                                              II_CLASS_S>;

class STORE_MMR6_DESC_BASE<string opstr, DAGOperand RO,
                           InstrItinClass Itin>
    : Store<opstr, RO>, MMR6Arch<opstr> {
  let DecoderMethod = "DecodeMemMMImm16";
  InstrItinClass Itinerary = Itin;
}
class SB_MMR6_DESC : STORE_MMR6_DESC_BASE<"sb", GPR32Opnd, II_SB>;

class SH_MMR6_DESC : STORE_MMR6_DESC_BASE<"sh", GPR32Opnd, II_SH>;
class ADDU16_MMR6_DESC : ArithRMM16<"addu16", GPRMM16Opnd, 1, II_ADDU, add>,
      MMR6Arch<"addu16"> {
  int AddedComplexity = 1;
}
class AND16_MMR6_DESC : LogicRMM16<"and16", GPRMM16Opnd, II_AND>,
      MMR6Arch<"and16">;
class ANDI16_MMR6_DESC : AndImmMM16<"andi16", GPRMM16Opnd, II_AND>,
      MMR6Arch<"andi16">;
class NOT16_MMR6_DESC : NotMM16<"not16", GPRMM16Opnd>, MMR6Arch<"not16"> {
  int AddedComplexity = 1;
}
class OR16_MMR6_DESC : LogicRMM16<"or16", GPRMM16Opnd, II_OR>, MMR6Arch<"or16">;
class SLL16_MMR6_DESC : ShiftIMM16<"sll16", uimm3_shift, GPRMM16Opnd, II_SLL>,
      MMR6Arch<"sll16">;
class SRL16_MMR6_DESC : ShiftIMM16<"srl16", uimm3_shift, GPRMM16Opnd, II_SRL>,
      MMR6Arch<"srl16">;
class BREAK16_MMR6_DESC : BrkSdbbp16MM<"break16", II_BREAK>,
                          MMR6Arch<"break16">;
class LI16_MMR6_DESC : LoadImmMM16<"li16", li16_imm, GPRMM16Opnd>,
      MMR6Arch<"li16">, IsAsCheapAsAMove;
class MOVE16_MMR6_DESC : MoveMM16<"move16", GPR32Opnd>, MMR6Arch<"move16">;
class MOVEP_MMR6_DESC : MovePMM16<"movep", GPRMM16OpndMovePPairFirst,
                                  GPRMM16OpndMovePPairSecond, GPRMM16OpndMoveP>,
                        MMR6Arch<"movep">;
class SDBBP16_MMR6_DESC : BrkSdbbp16MM<"sdbbp16", II_SDBBP>,
                          MMR6Arch<"sdbbp16">;
class SUBU16_MMR6_DESC : ArithRMM16<"subu16", GPRMM16Opnd, 0, II_SUBU, sub>,
      MMR6Arch<"subu16"> {
  int AddedComplexity = 1;
}
class XOR16_MMR6_DESC : LogicRMM16<"xor16", GPRMM16Opnd, II_XOR>,
      MMR6Arch<"xor16">;

class LW_MMR6_DESC : MMR6Arch<"lw">, MipsR6Inst {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins mem:$addr);
  string AsmString = "lw\t$rt, $addr";
  let DecoderMethod = "DecodeMemMMImm16";
  let canFoldAsLoad = 1;
  let mayLoad = 1;
  list<dag> Pattern = [(set GPR32Opnd:$rt, (load addrDefault:$addr))];
  InstrItinClass Itinerary = II_LW;
}

class LUI_MMR6_DESC : IsAsCheapAsAMove, MMR6Arch<"lui">, MipsR6Inst{
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins uimm16:$imm16);
  string AsmString = "lui\t$rt, $imm16";
  list<dag> Pattern = [];
  bit hasSideEffects = 0;
  bit isReMaterializable = 1;
  InstrItinClass Itinerary = II_LUI;
  Format Form = FrmI;
}

class SYNC_MMR6_DESC : MMR6Arch<"sync">, MipsR6Inst {
  dag OutOperandList = (outs);
  dag InOperandList = (ins uimm5:$stype);
  string AsmString = !strconcat("sync", "\t$stype");
  list<dag> Pattern = [(MipsSync immZExt5:$stype)];
  InstrItinClass Itinerary = II_SYNC;
  bit HasSideEffects = 1;
}

class SYNCI_MMR6_DESC : SYNCI_FT<"synci", mem_mm_16> {
  let DecoderMethod = "DecodeSynciR6";
}

class RDPGPR_MMR6_DESC : MMR6Arch<"rdpgpr">, MipsR6Inst {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rd);
  string AsmString = !strconcat("rdpgpr", "\t$rt, $rd");
  InstrItinClass Itinerary = II_RDPGPR;
}

class SDBBP_MMR6_DESC : MipsR6Inst {
  dag OutOperandList = (outs);
  dag InOperandList = (ins uimm20:$code_);
  string AsmString = !strconcat("sdbbp", "\t$code_");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_SDBBP;
}

class SIGRIE_MMR6_DESC : MipsR6Inst {
  dag OutOperandList = (outs);
  dag InOperandList = (ins uimm16:$code_);
  string AsmString = !strconcat("sigrie", "\t$code_");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_SIGRIE;
}

class LWM16_MMR6_DESC
    : MicroMipsInst16<(outs reglist16:$rt), (ins mem_mm_4sp:$addr),
                      !strconcat("lwm16", "\t$rt, $addr"), [],
                      II_LWM, FrmI>,
      MMR6Arch<"lwm16"> {
  let DecoderMethod = "DecodeMemMMReglistImm4Lsl2";
  let mayLoad = 1;
  ComplexPattern Addr = addr;
}

class SWM16_MMR6_DESC
    : MicroMipsInst16<(outs), (ins reglist16:$rt, mem_mm_4sp:$addr),
                      !strconcat("swm16", "\t$rt, $addr"), [],
                      II_SWM, FrmI>,
      MMR6Arch<"swm16"> {
  let DecoderMethod = "DecodeMemMMReglistImm4Lsl2";
  let mayStore = 1;
  ComplexPattern Addr = addr;
}

class SB16_MMR6_DESC_BASE<string opstr, DAGOperand RTOpnd,
                          InstrItinClass Itin, Operand MemOpnd>
    : MicroMipsInst16<(outs), (ins RTOpnd:$rt, MemOpnd:$addr),
                      !strconcat(opstr, "\t$rt, $addr"), [], Itin, FrmI>,
      MMR6Arch<opstr> {
  let DecoderMethod = "DecodeMemMMImm4";
  let mayStore = 1;
}

class SB16_MMR6_DESC
    : SB16_MMR6_DESC_BASE<"sb16", GPRMM16OpndZero, II_SB, mem_mm_4>;
class SH16_MMR6_DESC
    : SB16_MMR6_DESC_BASE<"sh16", GPRMM16OpndZero, II_SH, mem_mm_4_lsl1>;
class SW16_MMR6_DESC
    : SB16_MMR6_DESC_BASE<"sw16", GPRMM16OpndZero, II_SW, mem_mm_4_lsl2>;

class SWSP_MMR6_DESC
    : MicroMipsInst16<(outs), (ins GPR32Opnd:$rt, mem_mm_sp_imm5_lsl2:$offset),
                      !strconcat("sw", "\t$rt, $offset"), [], II_SW, FrmI>,
      MMR6Arch<"swsp"> {
  let DecoderMethod = "DecodeMemMMSPImm5Lsl2";
  let mayStore = 1;
}

class JALRC_HB_MMR6_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = !strconcat("jalrc.hb", "\t$rt, $rs");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_JALR_HB;
  Format Form = FrmJ;
  bit isIndirectBranch = 1;
  bit hasDelaySlot = 0;
}

class TLBINV_MMR6_DESC_BASE<string opstr, InstrItinClass Itin> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins);
  string AsmString = opstr;
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
}

class TLBINV_MMR6_DESC : TLBINV_MMR6_DESC_BASE<"tlbinv", II_TLBINV>;
class TLBINVF_MMR6_DESC : TLBINV_MMR6_DESC_BASE<"tlbinvf", II_TLBINVF>;

class DVPEVP_MMR6_DESC_BASE<string opstr, InstrItinClass Itin> {
  dag OutOperandList = (outs GPR32Opnd:$rs);
  dag InOperandList = (ins);
  string AsmString = !strconcat(opstr, "\t$rs");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
  bit hasUnModeledSideEffects = 1;
}

class DVP_MMR6_DESC : DVPEVP_MMR6_DESC_BASE<"dvp", II_DVP>;
class EVP_MMR6_DESC : DVPEVP_MMR6_DESC_BASE<"evp", II_EVP>;

class BEQZC_MMR6_DESC
    : CMP_CBR_EQNE_Z_DESC_BASE<"beqzc", brtarget21_mm, GPR32Opnd>,
      MMR6Arch<"beqzc">;
class BNEZC_MMR6_DESC
    : CMP_CBR_EQNE_Z_DESC_BASE<"bnezc", brtarget21_mm, GPR32Opnd>,
      MMR6Arch<"bnezc">;

class BRANCH_COP1_MMR6_DESC_BASE<string opstr> :
    InstSE<(outs), (ins FGR64Opnd:$rt, brtarget_mm:$offset),
           !strconcat(opstr, "\t$rt, $offset"), [], II_BC1CCZ, FrmI>,
    HARDFLOAT, BRANCH_DESC_BASE {
  list<Register> Defs = [AT];
}

class BC1EQZC_MMR6_DESC : BRANCH_COP1_MMR6_DESC_BASE<"bc1eqzc">;
class BC1NEZC_MMR6_DESC : BRANCH_COP1_MMR6_DESC_BASE<"bc1nezc">;

class BRANCH_COP2_MMR6_DESC_BASE<string opstr, InstrItinClass Itin>
    : BRANCH_DESC_BASE {
  dag InOperandList = (ins COP2Opnd:$rt, brtarget_mm:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(opstr, "\t$rt, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = Itin;
}

class BC2EQZC_MMR6_DESC : BRANCH_COP2_MMR6_DESC_BASE<"bc2eqzc", II_BC2CCZ>;
class BC2NEZC_MMR6_DESC : BRANCH_COP2_MMR6_DESC_BASE<"bc2nezc", II_BC2CCZ>;

class EXT_MMR6_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs, uimm5:$pos, uimm5_plus1:$size);
  string AsmString = !strconcat("ext", "\t$rt, $rs, $pos, $size");
  list<dag> Pattern = [(set GPR32Opnd:$rt, (MipsExt GPR32Opnd:$rs, imm:$pos,
                       imm:$size))];
  InstrItinClass Itinerary = II_EXT;
  Format Form = FrmR;
  string BaseOpcode = "ext";
}

class INS_MMR6_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs, uimm5:$pos, uimm5_inssize_plus1:$size,
                       GPR32Opnd:$src);
  string AsmString = !strconcat("ins", "\t$rt, $rs, $pos, $size");
  list<dag> Pattern = [(set GPR32Opnd:$rt, (MipsIns GPR32Opnd:$rs, imm:$pos,
                       imm:$size, GPR32Opnd:$src))];
  InstrItinClass Itinerary = II_INS;
  Format Form = FrmR;
  string BaseOpcode = "ins";
  string Constraints = "$src = $rt";
}

class JALRC_MMR6_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = !strconcat("jalrc", "\t$rt, $rs");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_JALRC;
  bit isCall = 1;
  bit hasDelaySlot = 0;
  list<Register> Defs = [RA];
}

class BOVC_BNVC_MMR6_DESC_BASE<string instr_asm, Operand opnd,
                               RegisterOperand GPROpnd>
    : BRANCH_DESC_BASE {
  dag InOperandList = (ins GPROpnd:$rt, GPROpnd:$rs, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCC;
}

class BOVC_MMR6_DESC : BOVC_BNVC_MMR6_DESC_BASE<"bovc", brtargetr6, GPR32Opnd>;
class BNVC_MMR6_DESC : BOVC_BNVC_MMR6_DESC_BASE<"bnvc", brtargetr6, GPR32Opnd>;

//===----------------------------------------------------------------------===//
//
// Instruction Definitions
//
//===----------------------------------------------------------------------===//

let DecoderNamespace = "MicroMipsR6" in {
def ADD_MMR6 : StdMMR6Rel, ADD_MMR6_DESC, ADD_MMR6_ENC, ISA_MICROMIPS32R6;
def ADDIU_MMR6 : StdMMR6Rel, ADDIU_MMR6_DESC, ADDIU_MMR6_ENC, ISA_MICROMIPS32R6;
def ADDU_MMR6 : StdMMR6Rel, ADDU_MMR6_DESC, ADDU_MMR6_ENC, ISA_MICROMIPS32R6;
def ADDIUPC_MMR6 : R6MMR6Rel, ADDIUPC_MMR6_ENC, ADDIUPC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def ALUIPC_MMR6 : R6MMR6Rel, ALUIPC_MMR6_ENC, ALUIPC_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def AND_MMR6 : StdMMR6Rel, AND_MMR6_DESC, AND_MMR6_ENC, ISA_MICROMIPS32R6;
def ANDI_MMR6 : StdMMR6Rel, ANDI_MMR6_DESC, ANDI_MMR6_ENC, ISA_MICROMIPS32R6;
def AUIPC_MMR6 : R6MMR6Rel, AUIPC_MMR6_ENC, AUIPC_MMR6_DESC, ISA_MICROMIPS32R6;
def ALIGN_MMR6 : R6MMR6Rel, ALIGN_MMR6_ENC, ALIGN_MMR6_DESC, ISA_MICROMIPS32R6;
def AUI_MMR6 : R6MMR6Rel, AUI_MMR6_ENC, AUI_MMR6_DESC, ISA_MICROMIPS32R6;
def BALC_MMR6 : R6MMR6Rel, BALC_MMR6_ENC, BALC_MMR6_DESC, ISA_MICROMIPS32R6;
def BC_MMR6 : R6MMR6Rel, BC_MMR6_ENC, BC_MMR6_DESC, ISA_MICROMIPS32R6;
def BC16_MMR6 : StdMMR6Rel, BC16_MMR6_DESC, BC16_MMR6_ENC, ISA_MICROMIPS32R6;
def BEQZC_MMR6 : R6MMR6Rel, BEQZC_MMR6_ENC, BEQZC_MMR6_DESC,
                 ISA_MICROMIPS32R6;
def BEQZC16_MMR6 : StdMMR6Rel, BEQZC16_MMR6_DESC, BEQZC16_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def BNEZC_MMR6 : R6MMR6Rel, BNEZC_MMR6_ENC, BNEZC_MMR6_DESC,
                 ISA_MICROMIPS32R6;
def BNEZC16_MMR6 : StdMMR6Rel, BNEZC16_MMR6_DESC, BNEZC16_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def BITSWAP_MMR6 : R6MMR6Rel, BITSWAP_MMR6_ENC, BITSWAP_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BEQZALC_MMR6 : R6MMR6Rel, BEQZALC_MMR6_ENC, BEQZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BNEZALC_MMR6 : R6MMR6Rel, BNEZALC_MMR6_ENC, BNEZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BREAK_MMR6 : StdMMR6Rel, BRK_MMR6_DESC, BRK_MMR6_ENC, ISA_MICROMIPS32R6;
def CACHE_MMR6 : R6MMR6Rel, CACHE_MMR6_ENC, CACHE_MMR6_DESC, ISA_MICROMIPS32R6;
def CLO_MMR6 : R6MMR6Rel, CLO_MMR6_ENC, CLO_MMR6_DESC, ISA_MICROMIPS32R6;
def CLZ_MMR6 : R6MMR6Rel, CLZ_MMR6_ENC, CLZ_MMR6_DESC, ISA_MICROMIPS32R6;
def DIV_MMR6 : R6MMR6Rel, DIV_MMR6_DESC, DIV_MMR6_ENC, ISA_MICROMIPS32R6;
def DIVU_MMR6 : R6MMR6Rel, DIVU_MMR6_DESC, DIVU_MMR6_ENC, ISA_MICROMIPS32R6;
def EHB_MMR6 : StdMMR6Rel, EHB_MMR6_DESC, EHB_MMR6_ENC, ISA_MICROMIPS32R6;
def EI_MMR6 : StdMMR6Rel, EI_MMR6_DESC, EI_MMR6_ENC, ISA_MICROMIPS32R6;
def DI_MMR6 : StdMMR6Rel, DI_MMR6_DESC, DI_MMR6_ENC, ISA_MICROMIPS32R6;
def ERET_MMR6 : StdMMR6Rel, ERET_MMR6_DESC, ERET_MMR6_ENC, ISA_MICROMIPS32R6;
def DERET_MMR6 : StdMMR6Rel, DERET_MMR6_DESC, DERET_MMR6_ENC, ISA_MICROMIPS32R6;
def ERETNC_MMR6 : R6MMR6Rel, ERETNC_MMR6_DESC, ERETNC_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def GINVI_MMR6 : R6MMR6Rel, GINVI_MMR6_ENC, GINVI_MMR6_DESC,
                 ISA_MICROMIPS32R6, ASE_GINV;
def GINVT_MMR6 : R6MMR6Rel, GINVT_MMR6_ENC, GINVT_MMR6_DESC,
                 ISA_MICROMIPS32R6, ASE_GINV;
let FastISelShouldIgnore = 1 in
def JALRC16_MMR6 : R6MMR6Rel, JALRC16_MMR6_DESC, JALRC16_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def JIALC_MMR6 : R6MMR6Rel, JIALC_MMR6_ENC, JIALC_MMR6_DESC, ISA_MICROMIPS32R6;
def JIC_MMR6 : R6MMR6Rel, JIC_MMR6_ENC, JIC_MMR6_DESC, ISA_MICROMIPS32R6;
def JRC16_MMR6 : R6MMR6Rel, JRC16_MMR6_DESC, JRC16_MMR6_ENC, ISA_MICROMIPS32R6;
def JRCADDIUSP_MMR6 : R6MMR6Rel, JRCADDIUSP_MMR6_DESC, JRCADDIUSP_MMR6_ENC,
                      ISA_MICROMIPS32R6;
def LSA_MMR6 : R6MMR6Rel, LSA_MMR6_ENC, LSA_MMR6_DESC, ISA_MICROMIPS32R6;
def LWPC_MMR6 : R6MMR6Rel, LWPC_MMR6_ENC, LWPC_MMR6_DESC, ISA_MICROMIPS32R6;
def LWM16_MMR6 : StdMMR6Rel, LWM16_MMR6_DESC, LWM16_MMR6_ENC, ISA_MICROMIPS32R6;
def MTC0_MMR6 : StdMMR6Rel, MTC0_MMR6_ENC, MTC0_MMR6_DESC, ISA_MICROMIPS32R6;
def MTC1_MMR6 : StdMMR6Rel, MTC1_MMR6_DESC, MTC1_MMR6_ENC, ISA_MICROMIPS32R6;
def MTC2_MMR6 : StdMMR6Rel, MTC2_MMR6_ENC, MTC2_MMR6_DESC, ISA_MICROMIPS32R6;
def MTHC0_MMR6 : R6MMR6Rel, MTHC0_MMR6_ENC, MTHC0_MMR6_DESC, ISA_MICROMIPS32R6;
def MTHC2_MMR6 : StdMMR6Rel, MTHC2_MMR6_ENC, MTHC2_MMR6_DESC, ISA_MICROMIPS32R6;
def MFC0_MMR6 : StdMMR6Rel, MFC0_MMR6_ENC, MFC0_MMR6_DESC, ISA_MICROMIPS32R6;
def MFC1_MMR6 : StdMMR6Rel, MFC1_MMR6_DESC, MFC1_MMR6_ENC, ISA_MICROMIPS32R6;
def MFC2_MMR6 : StdMMR6Rel, MFC2_MMR6_ENC, MFC2_MMR6_DESC, ISA_MICROMIPS32R6;
def MFHC0_MMR6 : R6MMR6Rel, MFHC0_MMR6_ENC, MFHC0_MMR6_DESC, ISA_MICROMIPS32R6;
def MFHC2_MMR6 : StdMMR6Rel, MFHC2_MMR6_ENC, MFHC2_MMR6_DESC, ISA_MICROMIPS32R6;
def MOD_MMR6 : R6MMR6Rel, MOD_MMR6_DESC, MOD_MMR6_ENC, ISA_MICROMIPS32R6;
def MODU_MMR6 : R6MMR6Rel, MODU_MMR6_DESC, MODU_MMR6_ENC, ISA_MICROMIPS32R6;
def MUL_MMR6 : R6MMR6Rel, MUL_MMR6_DESC, MUL_MMR6_ENC, ISA_MICROMIPS32R6;
def MUH_MMR6 : R6MMR6Rel, MUH_MMR6_DESC, MUH_MMR6_ENC, ISA_MICROMIPS32R6;
def MULU_MMR6 : R6MMR6Rel, MULU_MMR6_DESC, MULU_MMR6_ENC, ISA_MICROMIPS32R6;
def MUHU_MMR6 : R6MMR6Rel, MUHU_MMR6_DESC, MUHU_MMR6_ENC, ISA_MICROMIPS32R6;
def NOR_MMR6 : StdMMR6Rel, NOR_MMR6_DESC, NOR_MMR6_ENC, ISA_MICROMIPS32R6;
def OR_MMR6 : StdMMR6Rel, OR_MMR6_DESC, OR_MMR6_ENC, ISA_MICROMIPS32R6;
def ORI_MMR6 : StdMMR6Rel, ORI_MMR6_DESC, ORI_MMR6_ENC, ISA_MICROMIPS32R6;
def PREF_MMR6 : R6MMR6Rel, PREF_MMR6_ENC, PREF_MMR6_DESC, ISA_MICROMIPS32R6;
def SB16_MMR6 : StdMMR6Rel, SB16_MMR6_DESC, SB16_MMR6_ENC, ISA_MICROMIPS32R6;
def SELEQZ_MMR6 : R6MMR6Rel, SELEQZ_MMR6_ENC, SELEQZ_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def SELNEZ_MMR6 : R6MMR6Rel, SELNEZ_MMR6_ENC, SELNEZ_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def SH16_MMR6 : StdMMR6Rel, SH16_MMR6_DESC, SH16_MMR6_ENC, ISA_MICROMIPS32R6;
def SLL_MMR6 : StdMMR6Rel, SLL_MMR6_DESC, SLL_MMR6_ENC, ISA_MICROMIPS32R6;
def SUB_MMR6 : StdMMR6Rel, SUB_MMR6_DESC, SUB_MMR6_ENC, ISA_MICROMIPS32R6;
def SUBU_MMR6 : StdMMR6Rel, SUBU_MMR6_DESC, SUBU_MMR6_ENC, ISA_MICROMIPS32R6;
def SW16_MMR6 : StdMMR6Rel, SW16_MMR6_DESC, SW16_MMR6_ENC, ISA_MICROMIPS32R6;
def SWM16_MMR6 : StdMMR6Rel, SWM16_MMR6_DESC, SWM16_MMR6_ENC, ISA_MICROMIPS32R6;
def SWSP_MMR6 : StdMMR6Rel, SWSP_MMR6_DESC, SWSP_MMR6_ENC, ISA_MICROMIPS32R6;
def WRPGPR_MMR6 : StdMMR6Rel, WRPGPR_MMR6_ENC, WRPGPR_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def WSBH_MMR6 : StdMMR6Rel, WSBH_MMR6_ENC, WSBH_MMR6_DESC, ISA_MICROMIPS32R6;
def LB_MMR6 : R6MMR6Rel, LB_MMR6_ENC, LB_MMR6_DESC, ISA_MICROMIPS32R6;
def LBU_MMR6 : R6MMR6Rel, LBU_MMR6_ENC, LBU_MMR6_DESC, ISA_MICROMIPS32R6;
def PAUSE_MMR6 : StdMMR6Rel, PAUSE_MMR6_DESC, PAUSE_MMR6_ENC, ISA_MICROMIPS32R6;
def RDHWR_MMR6 : R6MMR6Rel, RDHWR_MMR6_DESC, RDHWR_MMR6_ENC, ISA_MICROMIPS32R6;
def WAIT_MMR6 : StdMMR6Rel, WAIT_MMR6_DESC, WAIT_MMR6_ENC, ISA_MICROMIPS32R6;
def SSNOP_MMR6 : StdMMR6Rel, SSNOP_MMR6_DESC, SSNOP_MMR6_ENC, ISA_MICROMIPS32R6;
def SYNC_MMR6 : StdMMR6Rel, SYNC_MMR6_DESC, SYNC_MMR6_ENC, ISA_MICROMIPS32R6;
def SYNCI_MMR6 : StdMMR6Rel, SYNCI_MMR6_DESC, SYNCI_MMR6_ENC, ISA_MICROMIPS32R6;
def RDPGPR_MMR6 : R6MMR6Rel, RDPGPR_MMR6_DESC, RDPGPR_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def SDBBP_MMR6 : R6MMR6Rel, SDBBP_MMR6_DESC, SDBBP_MMR6_ENC, ISA_MICROMIPS32R6;
def SIGRIE_MMR6 : R6MMR6Rel, SIGRIE_MMR6_DESC, SIGRIE_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def XOR_MMR6 : StdMMR6Rel, XOR_MMR6_DESC, XOR_MMR6_ENC, ISA_MICROMIPS32R6;
def XORI_MMR6 : StdMMR6Rel, XORI_MMR6_DESC, XORI_MMR6_ENC, ISA_MICROMIPS32R6;
let DecoderMethod = "DecodeMemMMImm16" in {
  def SW_MMR6 : StdMMR6Rel, SW_MMR6_DESC, SW_MMR6_ENC, ISA_MICROMIPS32R6;
}
/// Floating Point Instructions
def FADD_S_MMR6 : StdMMR6Rel, FADD_S_MMR6_ENC, FADD_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def FSUB_S_MMR6 : StdMMR6Rel, FSUB_S_MMR6_ENC, FSUB_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def FMUL_S_MMR6 : StdMMR6Rel, FMUL_S_MMR6_ENC, FMUL_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def FDIV_S_MMR6 : StdMMR6Rel, FDIV_S_MMR6_ENC, FDIV_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def MADDF_S_MMR6 : R6MMR6Rel, MADDF_S_MMR6_ENC, MADDF_S_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def MADDF_D_MMR6 : R6MMR6Rel, MADDF_D_MMR6_ENC, MADDF_D_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def MSUBF_S_MMR6 : R6MMR6Rel, MSUBF_S_MMR6_ENC, MSUBF_S_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def MSUBF_D_MMR6 : R6MMR6Rel, MSUBF_D_MMR6_ENC, MSUBF_D_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def FMOV_S_MMR6 : StdMMR6Rel, FMOV_S_MMR6_ENC, FMOV_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def FMOV_D_MMR6 : StdMMR6Rel, FMOV_D_MMR6_ENC, FMOV_D_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def FNEG_S_MMR6 : StdMMR6Rel, FNEG_S_MMR6_ENC, FNEG_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def MAX_S_MMR6 : R6MMR6Rel, MAX_S_MMR6_ENC, MAX_S_MMR6_DESC, ISA_MICROMIPS32R6;
def MAX_D_MMR6 : R6MMR6Rel, MAX_D_MMR6_ENC, MAX_D_MMR6_DESC, ISA_MICROMIPS32R6;
def MIN_S_MMR6 : R6MMR6Rel, MIN_S_MMR6_ENC, MIN_S_MMR6_DESC, ISA_MICROMIPS32R6;
def MIN_D_MMR6 : R6MMR6Rel, MIN_D_MMR6_ENC, MIN_D_MMR6_DESC, ISA_MICROMIPS32R6;
def MAXA_S_MMR6 : R6MMR6Rel, MAXA_S_MMR6_ENC, MAXA_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def MAXA_D_MMR6 : R6MMR6Rel, MAXA_D_MMR6_ENC, MAXA_D_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def MINA_S_MMR6 : R6MMR6Rel, MINA_S_MMR6_ENC, MINA_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def MINA_D_MMR6 : R6MMR6Rel, MINA_D_MMR6_ENC, MINA_D_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def CVT_L_S_MMR6 : StdMMR6Rel, CVT_L_S_MMR6_ENC, CVT_L_S_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CVT_L_D_MMR6 : StdMMR6Rel, CVT_L_D_MMR6_ENC, CVT_L_D_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CVT_W_S_MMR6 : StdMMR6Rel, CVT_W_S_MMR6_ENC, CVT_W_S_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CVT_D_L_MMR6 : StdMMR6Rel, CVT_D_L_MMR6_ENC, CVT_D_L_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CVT_S_W_MMR6 : StdMMR6Rel, CVT_S_W_MMR6_ENC, CVT_S_W_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CVT_S_L_MMR6 : StdMMR6Rel, CVT_S_L_MMR6_ENC, CVT_S_L_MMR6_DESC,
                   ISA_MICROMIPS32R6;
defm S_MMR6 : CMP_CC_MMR6<0b000101, "s", FGR32Opnd, II_CMP_CC_S>;
defm D_MMR6 : CMP_CC_MMR6<0b010101, "d", FGR64Opnd, II_CMP_CC_D>;
def FLOOR_L_S_MMR6 : StdMMR6Rel, FLOOR_L_S_MMR6_ENC, FLOOR_L_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def FLOOR_L_D_MMR6 : StdMMR6Rel, FLOOR_L_D_MMR6_ENC, FLOOR_L_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def FLOOR_W_S_MMR6 : StdMMR6Rel, FLOOR_W_S_MMR6_ENC, FLOOR_W_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def FLOOR_W_D_MMR6 : StdMMR6Rel, FLOOR_W_D_MMR6_ENC, FLOOR_W_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def CEIL_L_S_MMR6 : StdMMR6Rel, CEIL_L_S_MMR6_ENC, CEIL_L_S_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def CEIL_L_D_MMR6 : StdMMR6Rel, CEIL_L_D_MMR6_ENC, CEIL_L_D_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def CEIL_W_S_MMR6 : StdMMR6Rel, CEIL_W_S_MMR6_ENC, CEIL_W_S_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def CEIL_W_D_MMR6 : StdMMR6Rel, CEIL_W_D_MMR6_ENC, CEIL_W_D_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def TRUNC_L_S_MMR6 : StdMMR6Rel, TRUNC_L_S_MMR6_ENC, TRUNC_L_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def TRUNC_L_D_MMR6 : StdMMR6Rel, TRUNC_L_D_MMR6_ENC, TRUNC_L_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def TRUNC_W_S_MMR6 : StdMMR6Rel, TRUNC_W_S_MMR6_ENC, TRUNC_W_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def TRUNC_W_D_MMR6 : StdMMR6Rel, TRUNC_W_D_MMR6_ENC, TRUNC_W_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def SB_MMR6 : StdMMR6Rel, SB_MMR6_DESC, SB_MMR6_ENC, ISA_MICROMIPS32R6;
def SH_MMR6 : StdMMR6Rel, SH_MMR6_DESC, SH_MMR6_ENC, ISA_MICROMIPS32R6;
def LW_MMR6 : StdMMR6Rel, LW_MMR6_DESC, LW_MMR6_ENC, ISA_MICROMIPS32R6;
def LUI_MMR6 : R6MMR6Rel, LUI_MMR6_DESC, LUI_MMR6_ENC, ISA_MICROMIPS32R6;
def ADDU16_MMR6 : StdMMR6Rel, ADDU16_MMR6_DESC, ADDU16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def AND16_MMR6 : StdMMR6Rel, AND16_MMR6_DESC, AND16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def ANDI16_MMR6 : StdMMR6Rel, ANDI16_MMR6_DESC, ANDI16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def NOT16_MMR6 : StdMMR6Rel, NOT16_MMR6_DESC, NOT16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def OR16_MMR6 : StdMMR6Rel, OR16_MMR6_DESC, OR16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def SLL16_MMR6 : StdMMR6Rel, SLL16_MMR6_DESC, SLL16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def SRL16_MMR6 : StdMMR6Rel, SRL16_MMR6_DESC, SRL16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def BREAK16_MMR6 : StdMMR6Rel, BREAK16_MMR6_DESC, BREAK16_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def LI16_MMR6 : StdMMR6Rel, LI16_MMR6_DESC, LI16_MMR6_ENC,
                ISA_MICROMIPS32R6;
def MOVE16_MMR6 : StdMMR6Rel, MOVE16_MMR6_DESC, MOVE16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def MOVEP_MMR6  : StdMMR6Rel, MOVEP_MMR6_DESC, MOVEP_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def SDBBP16_MMR6 : StdMMR6Rel, SDBBP16_MMR6_DESC, SDBBP16_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def SUBU16_MMR6 : StdMMR6Rel, SUBU16_MMR6_DESC, SUBU16_MMR6_ENC,
                  ISA_MICROMIPS32R6;
def XOR16_MMR6 : StdMMR6Rel, XOR16_MMR6_DESC, XOR16_MMR6_ENC,
                 ISA_MICROMIPS32R6;
def JALRC_HB_MMR6 : R6MMR6Rel, JALRC_HB_MMR6_ENC, JALRC_HB_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def EXT_MMR6 : StdMMR6Rel, EXT_MMR6_ENC, EXT_MMR6_DESC, ISA_MICROMIPS32R6;
def INS_MMR6 : StdMMR6Rel, INS_MMR6_ENC, INS_MMR6_DESC, ISA_MICROMIPS32R6;
def JALRC_MMR6 : R6MMR6Rel, JALRC_MMR6_ENC, JALRC_MMR6_DESC, ISA_MICROMIPS32R6;
def RINT_S_MMR6 : StdMMR6Rel, RINT_S_MMR6_ENC, RINT_S_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def RINT_D_MMR6 : StdMMR6Rel, RINT_D_MMR6_ENC, RINT_D_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def ROUND_L_S_MMR6 : StdMMR6Rel, ROUND_L_S_MMR6_ENC, ROUND_L_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def ROUND_L_D_MMR6 : StdMMR6Rel, ROUND_L_D_MMR6_ENC, ROUND_L_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def ROUND_W_S_MMR6 : StdMMR6Rel, ROUND_W_S_MMR6_ENC, ROUND_W_S_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def ROUND_W_D_MMR6 : StdMMR6Rel, ROUND_W_D_MMR6_ENC, ROUND_W_D_MMR6_DESC,
                     ISA_MICROMIPS32R6;
def SEL_S_MMR6 : R6MMR6Rel, SEL_S_MMR6_ENC, SEL_S_MMR6_DESC, ISA_MICROMIPS32R6;
def SEL_D_MMR6 : R6MMR6Rel, SEL_D_MMR6_ENC, SEL_D_MMR6_DESC, ISA_MICROMIPS32R6;
def SELEQZ_S_MMR6 : R6MMR6Rel, SELEQZ_S_MMR6_ENC, SELEQZ_S_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def SELEQZ_D_MMR6 : R6MMR6Rel, SELEQZ_D_MMR6_ENC, SELEQZ_D_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def SELNEZ_S_MMR6 : R6MMR6Rel, SELNEZ_S_MMR6_ENC, SELNEZ_S_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def SELNEZ_D_MMR6 : R6MMR6Rel, SELNEZ_D_MMR6_ENC, SELNEZ_D_MMR6_DESC,
                    ISA_MICROMIPS32R6;
def CLASS_S_MMR6 : StdMMR6Rel, CLASS_S_MMR6_ENC, CLASS_S_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def CLASS_D_MMR6 : StdMMR6Rel, CLASS_D_MMR6_ENC, CLASS_D_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def TLBINV_MMR6 : StdMMR6Rel, TLBINV_MMR6_ENC, TLBINV_MMR6_DESC,
                  ISA_MICROMIPS32R6;
def TLBINVF_MMR6 : StdMMR6Rel, TLBINVF_MMR6_ENC, TLBINVF_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def DVP_MMR6 : R6MMR6Rel, DVP_MMR6_ENC, DVP_MMR6_DESC, ISA_MICROMIPS32R6;
def EVP_MMR6 : R6MMR6Rel, EVP_MMR6_ENC, EVP_MMR6_DESC, ISA_MICROMIPS32R6;
def BC1EQZC_MMR6 : R6MMR6Rel, BC1EQZC_MMR6_DESC, BC1EQZC_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def BC1NEZC_MMR6 : R6MMR6Rel, BC1NEZC_MMR6_DESC, BC1NEZC_MMR6_ENC,
                   ISA_MICROMIPS32R6;
def BC2EQZC_MMR6 : R6MMR6Rel, MipsR6Inst, BC2EQZC_MMR6_ENC, BC2EQZC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BC2NEZC_MMR6 : R6MMR6Rel, MipsR6Inst, BC2NEZC_MMR6_ENC, BC2NEZC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
let DecoderNamespace = "MicroMipsFP64" in {
  def LDC1_D64_MMR6 : StdMMR6Rel, LDC1_D64_MMR6_DESC, LDC1_MMR6_ENC,
                      ISA_MICROMIPS32R6 {
    let BaseOpcode = "LDC164";
  }
  def SDC1_D64_MMR6 : StdMMR6Rel, SDC1_D64_MMR6_DESC, SDC1_MMR6_ENC,
                      ISA_MICROMIPS32R6;
}
def LDC2_MMR6 : StdMMR6Rel, LDC2_MMR6_ENC, LDC2_MMR6_DESC, ISA_MICROMIPS32R6;
def SDC2_MMR6 : StdMMR6Rel, SDC2_MMR6_ENC, SDC2_MMR6_DESC, ISA_MICROMIPS32R6;
def LWC2_MMR6 : StdMMR6Rel, LWC2_MMR6_ENC, LWC2_MMR6_DESC, ISA_MICROMIPS32R6;
def SWC2_MMR6 : StdMMR6Rel, SWC2_MMR6_ENC, SWC2_MMR6_DESC, ISA_MICROMIPS32R6;
def LL_MMR6   : R6MMR6Rel, LL_MMR6_ENC, LL_MMR6_DESC, ISA_MICROMIPS32R6;
def SC_MMR6   : R6MMR6Rel, SC_MMR6_ENC, SC_MMR6_DESC, ISA_MICROMIPS32R6;
}

def BOVC_MMR6 : R6MMR6Rel, BOVC_MMR6_ENC, BOVC_MMR6_DESC, ISA_MICROMIPS32R6,
                MMDecodeDisambiguatedBy<"POP35GroupBranchMMR6">;
def BNVC_MMR6 : R6MMR6Rel, BNVC_MMR6_ENC, BNVC_MMR6_DESC, ISA_MICROMIPS32R6,
                MMDecodeDisambiguatedBy<"POP37GroupBranchMMR6">;
def BGEC_MMR6 : R6MMR6Rel, BGEC_MMR6_ENC, BGEC_MMR6_DESC, ISA_MICROMIPS32R6;
def BGEUC_MMR6 : R6MMR6Rel, BGEUC_MMR6_ENC, BGEUC_MMR6_DESC, ISA_MICROMIPS32R6;
def BLTC_MMR6 : R6MMR6Rel, BLTC_MMR6_ENC, BLTC_MMR6_DESC, ISA_MICROMIPS32R6;
def BLTUC_MMR6 : R6MMR6Rel, BLTUC_MMR6_ENC, BLTUC_MMR6_DESC, ISA_MICROMIPS32R6;
def BEQC_MMR6 : R6MMR6Rel, BEQC_MMR6_ENC, BEQC_MMR6_DESC, ISA_MICROMIPS32R6,
                DecodeDisambiguates<"POP35GroupBranchMMR6">;
def BNEC_MMR6 : R6MMR6Rel, BNEC_MMR6_ENC, BNEC_MMR6_DESC, ISA_MICROMIPS32R6,
                DecodeDisambiguates<"POP37GroupBranchMMR6">;
def BLTZC_MMR6 : R6MMR6Rel, BLTZC_MMR6_ENC, BLTZC_MMR6_DESC, ISA_MICROMIPS32R6;
def BLEZC_MMR6 : R6MMR6Rel, BLEZC_MMR6_ENC, BLEZC_MMR6_DESC, ISA_MICROMIPS32R6;
def BGEZC_MMR6 : R6MMR6Rel, BGEZC_MMR6_ENC, BGEZC_MMR6_DESC, ISA_MICROMIPS32R6;
def BGTZC_MMR6 : R6MMR6Rel, BGTZC_MMR6_ENC, BGTZC_MMR6_DESC, ISA_MICROMIPS32R6;
def BGEZALC_MMR6 : R6MMR6Rel, BGEZALC_MMR6_ENC, BGEZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BGTZALC_MMR6 : R6MMR6Rel, BGTZALC_MMR6_ENC, BGTZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BLEZALC_MMR6 : R6MMR6Rel, BLEZALC_MMR6_ENC, BLEZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;
def BLTZALC_MMR6 : R6MMR6Rel, BLTZALC_MMR6_ENC, BLTZALC_MMR6_DESC,
                   ISA_MICROMIPS32R6;

//===----------------------------------------------------------------------===//
//
// MicroMips instruction aliases
//
//===----------------------------------------------------------------------===//

def : MipsInstAlias<"ei", (EI_MMR6 ZERO), 1>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"di", (DI_MMR6 ZERO), 1>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"nop", (SLL_MMR6 ZERO, ZERO, 0), 1>, ISA_MICROMIPS32R6;
def B_MMR6_Pseudo : MipsAsmPseudoInst<(outs), (ins brtarget_mm:$offset),
                                      !strconcat("b", "\t$offset")> {
  string DecoderNamespace = "MicroMipsR6";
}
def : MipsInstAlias<"sync", (SYNC_MMR6 0), 1>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"sdbbp", (SDBBP_MMR6 0), 1>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"sigrie", (SIGRIE_MMR6 0), 1>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"rdhwr $rt, $rs",
                    (RDHWR_MMR6 GPR32Opnd:$rt, HWRegsOpnd:$rs, 0), 1>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"mtc0 $rt, $rs",
                    (MTC0_MMR6 COP0Opnd:$rs, GPR32Opnd:$rt, 0), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"mthc0 $rt, $rs",
                    (MTHC0_MMR6 COP0Opnd:$rs, GPR32Opnd:$rt, 0), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"mfc0 $rt, $rs",
                    (MFC0_MMR6 GPR32Opnd:$rt, COP0Opnd:$rs, 0), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"mfhc0 $rt, $rs",
                    (MFHC0_MMR6 GPR32Opnd:$rt, COP0Opnd:$rs, 0), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"jalrc.hb $rs", (JALRC_HB_MMR6 RA, GPR32Opnd:$rs), 1>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"jal $offset", (BALC_MMR6 brtarget26_mm:$offset), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"dvp", (DVP_MMR6 ZERO), 0>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"evp", (EVP_MMR6 ZERO), 0>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"jalrc $rs", (JALRC_MMR6 RA, GPR32Opnd:$rs), 1>,
      ISA_MICROMIPS32R6;
def : MipsInstAlias<"and $rs, $rt, $imm",
                    (ANDI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rt, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"and $rs, $imm",
                    (ANDI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rs, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"or $rs, $rt, $imm",
                    (ORI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rt, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"or $rs, $imm",
                    (ORI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rs, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"xor $rs, $rt, $imm",
                    (XORI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rt, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"xor $rs, $imm",
                    (XORI_MMR6 GPR32Opnd:$rs, GPR32Opnd:$rs, uimm16:$imm), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"not $rt, $rs",
                    (NOR_MMR6 GPR32Opnd:$rt, GPR32Opnd:$rs, ZERO), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"not $rt",
                    (NOR_MMR6 GPR32Opnd:$rt, GPR32Opnd:$rt, ZERO), 0>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"lapc $rd, $imm",
                    (ADDIUPC_MMR6 GPR32Opnd:$rd, simm19_lsl2:$imm)>,
                    ISA_MICROMIPS32R6;
def : MipsInstAlias<"neg $rt, $rs",
                    (SUB_MMR6 GPR32Opnd:$rt, ZERO, GPR32Opnd:$rs), 1>,
      ISA_MICROMIPS32R6;
def : MipsInstAlias<"neg $rt",
                    (SUB_MMR6 GPR32Opnd:$rt, ZERO, GPR32Opnd:$rt), 1>,
      ISA_MICROMIPS32R6;
def : MipsInstAlias<"negu $rt, $rs",
                    (SUBU_MMR6 GPR32Opnd:$rt, ZERO, GPR32Opnd:$rs), 1>,
      ISA_MICROMIPS32R6;
def : MipsInstAlias<"negu $rt",
                    (SUBU_MMR6 GPR32Opnd:$rt, ZERO, GPR32Opnd:$rt), 1>,
      ISA_MICROMIPS32R6;
def : MipsInstAlias<"beqz16 $rs, $offset", (BEQZC16_MMR6 GPRMM16Opnd:$rs,
                                                         brtarget7_mm:$offset),
                    0>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"bnez16 $rs, $offset", (BNEZC16_MMR6 GPRMM16Opnd:$rs,
                                                         brtarget7_mm:$offset),
                    0>, ISA_MICROMIPS32R6;
def : MipsInstAlias<"b16 $offset", (BC16_MMR6 brtarget10_mm:$offset), 0>,
                    ISA_MICROMIPS32R6;

//===----------------------------------------------------------------------===//
//
// MicroMips arbitrary patterns that map to one or more instructions
//
//===----------------------------------------------------------------------===//

def : MipsPat<(store GPRMM16:$src, addrimm4lsl2:$addr),
              (SW16_MMR6 GPRMM16:$src, addrimm4lsl2:$addr)>, ISA_MICROMIPS32R6;
def : MipsPat<(subc GPR32:$lhs, GPR32:$rhs),
              (SUBU_MMR6 GPR32:$lhs, GPR32:$rhs)>, ISA_MICROMIPS32R6;

def : MipsPat<(select i32:$cond, i32:$t, i32:$f),
              (OR_MM (SELNEZ_MMR6 i32:$t, i32:$cond),
                     (SELEQZ_MMR6 i32:$f, i32:$cond))>,
              ISA_MICROMIPS32R6;
def : MipsPat<(select i32:$cond, i32:$t, immz),
              (SELNEZ_MMR6 i32:$t, i32:$cond)>,
              ISA_MICROMIPS32R6;
def : MipsPat<(select i32:$cond, immz, i32:$f),
              (SELEQZ_MMR6 i32:$f, i32:$cond)>,
              ISA_MICROMIPS32R6;

defm : SelectInt_Pats<i32, OR_MM, XORI_MMR6, SLTi_MM, SLTiu_MM, SELEQZ_MMR6,
                      SELNEZ_MMR6, immZExt16, i32>, ISA_MICROMIPS32R6;

defm S_MMR6 : Cmp_Pats<f32, NOR_MMR6, ZERO>, ISA_MICROMIPS32R6;
defm D_MMR6 : Cmp_Pats<f64, NOR_MMR6, ZERO>, ISA_MICROMIPS32R6;

def : MipsPat<(f32 fpimm0), (MTC1_MMR6 ZERO)>, ISA_MICROMIPS32R6;
def : MipsPat<(f32 fpimm0neg), (FNEG_S_MMR6(MTC1_MMR6 ZERO))>,
      ISA_MICROMIPS32R6;
def : MipsPat<(MipsTruncIntFP FGR64Opnd:$src),
              (TRUNC_W_D_MMR6 FGR64Opnd:$src)>, ISA_MICROMIPS32R6;
def : MipsPat<(MipsTruncIntFP FGR32Opnd:$src),
              (TRUNC_W_S_MMR6 FGR32Opnd:$src)>, ISA_MICROMIPS32R6;

def : MipsPat<(and GPRMM16:$src, immZExtAndi16:$imm),
              (ANDI16_MMR6 GPRMM16:$src, immZExtAndi16:$imm)>,
              ISA_MICROMIPS32R6;
def : MipsPat<(and GPR32:$src, immZExt16:$imm),
              (ANDI_MMR6 GPR32:$src, immZExt16:$imm)>, ISA_MICROMIPS32R6;
def : MipsPat<(i32 immZExt16:$imm),
              (XORI_MMR6 ZERO, immZExt16:$imm)>, ISA_MICROMIPS32R6;
def : MipsPat<(not GPRMM16:$in),
              (NOT16_MMR6 GPRMM16:$in)>, ISA_MICROMIPS32R6;
def : MipsPat<(not GPR32:$in),
              (NOR_MMR6 GPR32Opnd:$in, ZERO)>, ISA_MICROMIPS32R6;
// Patterns for load with a reg+imm operand.
let AddedComplexity = 41 in {
  def : LoadRegImmPat<LDC1_D64_MMR6, f64, load>, FGR_64, ISA_MICROMIPS32R6;
  def : StoreRegImmPat<SDC1_D64_MMR6, f64>, FGR_64, ISA_MICROMIPS32R6;
}

let isCall=1, hasDelaySlot=0, isCTI=1, Defs = [RA] in {
  class JumpLinkMMR6<Instruction JumpInst, DAGOperand Opnd> :
    PseudoSE<(outs), (ins calltarget:$target), [], II_JAL>,
    PseudoInstExpansion<(JumpInst Opnd:$target)>;
}

def JAL_MMR6 : JumpLinkMMR6<BALC_MMR6, brtarget26_mm>, ISA_MICROMIPS32R6;

def : MipsPat<(MipsJmpLink (i32 texternalsym:$dst)),
              (JAL_MMR6 texternalsym:$dst)>, ISA_MICROMIPS32R6;
def : MipsPat<(MipsJmpLink (iPTR tglobaladdr:$dst)),
              (JAL_MMR6 tglobaladdr:$dst)>, ISA_MICROMIPS32R6;

def TAILCALL_MMR6 : TailCall<BC_MMR6, brtarget26_mm>, ISA_MICROMIPS32R6;

def TAILCALLREG_MMR6  : TailCallReg<JRC16_MM, GPR32Opnd>, ISA_MICROMIPS32R6;

def PseudoIndirectBranch_MMR6 : PseudoIndirectBranchBase<JRC16_MMR6,
                                                         GPR32Opnd>,
                                ISA_MICROMIPS32R6;

def : MipsPat<(MipsTailCall (iPTR tglobaladdr:$dst)),
              (TAILCALL_MMR6 tglobaladdr:$dst)>, ISA_MICROMIPS32R6;

def : MipsPat<(MipsTailCall (iPTR texternalsym:$dst)),
              (TAILCALL_MMR6 texternalsym:$dst)>, ISA_MICROMIPS32R6;


def : MipsPat<(brcond (i32 (setne GPR32:$lhs, 0)), bb:$dst),
              (BNEZC_MMR6 GPR32:$lhs, bb:$dst)>, ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (seteq GPR32:$lhs, 0)), bb:$dst),
              (BEQZC_MMR6 GPR32:$lhs, bb:$dst)>, ISA_MICROMIPS32R6;

def : MipsPat<(brcond (i32 (setge GPR32:$lhs, GPR32:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLT_MM GPR32:$lhs, GPR32:$rhs), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setuge GPR32:$lhs, GPR32:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLTu_MM GPR32:$lhs, GPR32:$rhs), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setge GPR32:$lhs, immSExt16:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLTi_MM GPR32:$lhs, immSExt16:$rhs), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setuge GPR32:$lhs, immSExt16:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLTiu_MM GPR32:$lhs, immSExt16:$rhs), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setgt GPR32:$lhs, immSExt16Plus1:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLTi_MM GPR32:$lhs, (Plus1 imm:$rhs)), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setugt GPR32:$lhs, immSExt16Plus1:$rhs)), bb:$dst),
              (BEQZC_MMR6 (SLTiu_MM GPR32:$lhs, (Plus1 imm:$rhs)), bb:$dst)>,
      ISA_MICROMIPS32R6;

def : MipsPat<(brcond (i32 (setle GPR32:$lhs, GPR32:$rhs)), bb:$dst),
              (BEQZC_MMR6  (SLT_MM GPR32:$rhs, GPR32:$lhs), bb:$dst)>,
      ISA_MICROMIPS32R6;
def : MipsPat<(brcond (i32 (setule GPR32:$lhs, GPR32:$rhs)), bb:$dst),
              (BEQZC_MMR6  (SLTu_MM GPR32:$rhs, GPR32:$lhs), bb:$dst)>,
      ISA_MICROMIPS32R6;

def : MipsPat<(brcond GPR32:$cond, bb:$dst),
              (BNEZC_MMR6 GPR32:$cond, bb:$dst)>, ISA_MICROMIPS32R6;
