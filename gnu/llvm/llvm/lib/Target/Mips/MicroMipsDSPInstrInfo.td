//===- MicroMipsDSPInstrInfo.td - Micromips DSP instructions -*- tablegen *-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes MicroMips DSP instructions.
//
//===----------------------------------------------------------------------===//

// Instruction encoding.
class ADDQ_PH_MM_ENC : POOL32A_3R_FMT<"addq.ph", 0b00000001101>;
class ADDQ_S_PH_MM_ENC : POOL32A_3R_FMT<"addq_s.ph", 0b10000001101>;
class ADDQ_S_W_MM_ENC : POOL32A_3RB0_FMT<"addq_s.w", 0b1100000101>;
class ADDQH_PH_MMR2_ENC : POOL32A_3R_FMT<"addqh.ph", 0b00001001101>;
class ADDQH_R_PH_MMR2_ENC : POOL32A_3R_FMT<"addqh_r.ph", 0b10001001101>;
class ADDQH_W_MMR2_ENC: POOL32A_3R_FMT<"addqh.w", 0b00010001101>;
class ADDQH_R_W_MMR2_ENC : POOL32A_3R_FMT<"addqh_r.w", 0b10010001101>;
class ADDU_PH_MMR2_ENC : POOL32A_3R_FMT<"addu.ph", 0b00100001101>;
class ADDU_S_PH_MMR2_ENC : POOL32A_3R_FMT<"addu_s.ph", 0b10100001101>;
class ADDU_QB_MM_ENC : POOL32A_3R_FMT<"addu.qb", 0b00011001101>;
class ADDU_S_QB_MM_ENC : POOL32A_3R_FMT<"addu_s.qb", 0b10011001101>;
class ADDUH_QB_MMR2_ENC : POOL32A_3R_FMT<"adduh.qb", 0b00101001101>;
class ADDUH_R_QB_MMR2_ENC : POOL32A_3R_FMT<"adduh_r.qb", 0b10101001101>;
class ADDSC_MM_ENC : POOL32A_3RB0_FMT<"addsc", 0b1110000101>;
class ADDWC_MM_ENC : POOL32A_3RB0_FMT<"addwc", 0b1111000101>;
class DPA_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpa.w.ph", 0b00000010>;
class DPAQ_S_W_PH_MM_ENC : POOL32A_2RAC_FMT<"dpaq_s.w.ph", 0b00001010>;
class DPAQ_SA_L_W_MM_ENC : POOL32A_2RAC_FMT<"dpaq_sa.l.w", 0b01001010>;
class DPAQX_S_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpaqx_s.w.ph", 0b10001010>;
class DPAQX_SA_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpaqx_sa.w.ph", 0b11001010>;
class DPAU_H_QBL_MM_ENC : POOL32A_2RAC_FMT<"dpau.h.qbl", 0b10000010>;
class DPAU_H_QBR_MM_ENC : POOL32A_2RAC_FMT<"dpau.h.qbr", 0b11000010>;
class DPAX_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpax.w.ph", 0b01000010>;
class ABSQ_S_PH_MM_ENC : POOL32A_2R_FMT<"absq_s.ph", 0b0001000100>;
class ABSQ_S_W_MM_ENC : POOL32A_2R_FMT<"absq_s.w", 0b0010000100>;
class ABSQ_S_QB_MMR2_ENC : POOL32A_2R_FMT<"absq_s.qb", 0b0000000100>;
class INSV_MM_ENC : POOL32A_2R_FMT<"insv", 0b0100000100>;
class MADD_DSP_MM_ENC : POOL32A_2RAC_FMT<"madd", 0b00101010>;
class MADDU_DSP_MM_ENC : POOL32A_2RAC_FMT<"maddu", 0b01101010>;
class MSUB_DSP_MM_ENC : POOL32A_2RAC_FMT<"msub", 0b10101010>;
class MSUBU_DSP_MM_ENC : POOL32A_2RAC_FMT<"msubu", 0b11101010>;
class MULT_DSP_MM_ENC : POOL32A_2RAC_FMT<"mult", 0b00110010>;
class MULTU_DSP_MM_ENC : POOL32A_2RAC_FMT<"multu", 0b01110010>;
class SHLL_PH_MM_ENC : POOL32A_2RSA4_FMT<"shll.ph", 0b001110110101>;
class SHLL_S_PH_MM_ENC : POOL32A_2RSA4_FMT<"shll_s.ph", 0b101110110101>;
class SHLL_QB_MM_ENC : POOL32A_2RSA3_FMT<"shll.qb", 0b0100001>;
class SHLLV_PH_MM_ENC : POOL32A_3R_FMT<"shllv.ph", 0b00000001110>;
class SHLLV_S_PH_MM_ENC : POOL32A_3R_FMT<"shllv_s.ph", 0b10000001110>;
class SHLLV_QB_MM_ENC : POOL32A_3RB0_FMT<"shllv.qb", 0b1110010101>;
class SHLLV_S_W_MM_ENC : POOL32A_3RB0_FMT<"shllv_s.w", 0b1111010101>;
class SHLL_S_W_MM_ENC : POOL32A_2RSA5B0_FMT<"shll_s.w", 0b1111110101>;
class SHRA_QB_MMR2_ENC : POOL32A_2RSA3_FMT<"shra.qb", 0b0000111>;
class SHRA_R_QB_MMR2_ENC : POOL32A_2RSA3_FMT<"shra_r.qb", 0b1000111>;
class SHRA_PH_MM_ENC : POOL32A_2RSA4B0_FMT<"shra.ph", 0b01100110101>;
class SHRA_R_PH_MM_ENC : POOL32A_2RSA4B0_FMT<"shra_r.ph", 0b11100110101>;
class SHRAV_PH_MM_ENC : POOL32A_3R_FMT<"shrav.ph", 0b00110001101>;
class SHRAV_R_PH_MM_ENC : POOL32A_3R_FMT<"shrav_r.ph", 0b10110001101>;
class SHRAV_QB_MMR2_ENC : POOL32A_3R_FMT<"shrav.qb", 0b00111001101>;
class SHRAV_R_QB_MMR2_ENC : POOL32A_3R_FMT<"shrav_r.qb", 0b10111001101>;
class SHRAV_R_W_MM_ENC : POOL32A_3RB0_FMT<"shrav_r.w", 0b1011010101>;
class SHRA_R_W_MM_ENC : POOL32A_2RSA5B0_FMT<"shra_r.w", 0b1011110101>;
class SHRL_PH_MMR2_ENC : POOL32A_2RSA4OP6_FMT<"shrl.ph", 0b001111>;
class SHRL_QB_MM_ENC : POOL32A_2RSA3_FMT<"shrl.qb", 0b1100001>;
class SHRLV_PH_MMR2_ENC : POOL32A_3RB0_FMT<"shrlv.ph", 0b1100010101>;
class SHRLV_QB_MM_ENC : POOL32A_3RB0_FMT<"shrlv.qb", 0b1101010101>;
class PRECEQ_W_PHL_MM_ENC : POOL32A_2R_FMT<"preceq.w.phl", 0b0101000100>;
class PRECEQ_W_PHR_MM_ENC : POOL32A_2R_FMT<"preceq.w.phr", 0b0110000100>;
class PRECEQU_PH_QBL_MM_ENC : POOL32A_2R_FMT<"precequ.ph.qbl", 0b0111000100>;
class PRECEQU_PH_QBLA_MM_ENC : POOL32A_2R_FMT<"precequ.ph.qbla", 0b0111001100>;
class PRECEQU_PH_QBR_MM_ENC : POOL32A_2R_FMT<"precequ.ph.qbr", 0b1001000100>;
class PRECEQU_PH_QBRA_MM_ENC : POOL32A_2R_FMT<"precequ.ph.qbra", 0b1001001100>;
class PRECEU_PH_QBL_MM_ENC : POOL32A_2R_FMT<"preceu.ph.qbl", 0b1011000100>;
class PRECEU_PH_QBLA_MM_ENC : POOL32A_2R_FMT<"preceu.ph.qbla", 0b1011001100>;
class PRECEU_PH_QBR_MM_ENC : POOL32A_2R_FMT<"preceu.ph.qbr", 0b1101000100>;
class PRECEU_PH_QBRA_MM_ENC : POOL32A_2R_FMT<"preceu.ph.qbra", 0b1101001100>;
class SUBQ_PH_MM_ENC : POOL32A_3R_FMT<"subq.ph", 0b01000001101>;
class SUBQ_S_PH_MM_ENC : POOL32A_3R_FMT<"subq_s.ph", 0b11000001101>;
class SUBQ_S_W_MM_ENC : POOL32A_3RB0_FMT<"subq_s.w", 0b1101000101>;
class SUBQH_PH_MMR2_ENC : POOL32A_3R_FMT<"subqh.ph", 0b01001001101>;
class SUBQH_R_PH_MMR2_ENC : POOL32A_3R_FMT<"subqh_r.ph", 0b11001001101>;
class SUBQH_W_MMR2_ENC : POOL32A_3R_FMT<"subqh.w", 0b01010001101>;
class SUBQH_R_W_MMR2_ENC : POOL32A_3R_FMT<"subqh_r.w", 0b11010001101>;
class SUBU_PH_MMR2_ENC : POOL32A_3R_FMT<"subu.ph", 0b01100001101>;
class SUBU_S_PH_MMR2_ENC : POOL32A_3R_FMT<"subu_s.ph", 0b11100001101>;
class SUBU_QB_MM_ENC : POOL32A_3R_FMT<"subu.qb", 0b01011001101>;
class SUBU_S_QB_MM_ENC : POOL32A_3R_FMT<"subu_s.qb", 0b11011001101>;
class SUBUH_QB_MMR2_ENC : POOL32A_3R_FMT<"subuh.qb", 0b01101001101>;
class SUBUH_R_QB_MMR2_ENC : POOL32A_3R_FMT<"subuh_r.qb", 0b11101001101>;
class EXTP_MM_ENC : POOL32A_1RIMM5AC_FMT<"extp", 0b10011001>;
class EXTPDP_MM_ENC : POOL32A_1RIMM5AC_FMT<"extpdp", 0b11011001>;
class EXTPDPV_MM_ENC : POOL32A_2RAC_FMT<"extpdpv", 0b11100010>;
class EXTPV_MM_ENC : POOL32A_2RAC_FMT<"extpv", 0b10100010>;
class EXTR_W_MM_ENC : POOL32A_1RIMM5AC_FMT<"extr.w", 0b00111001>;
class EXTR_R_W_MM_ENC : POOL32A_1RIMM5AC_FMT<"extr_r.w", 0b01111001>;
class EXTR_RS_W_MM_ENC : POOL32A_1RIMM5AC_FMT<"extr_rs.w", 0b10111001>;
class EXTR_S_H_MM_ENC : POOL32A_1RIMM5AC_FMT<"extr_s.h", 0b11111001>;
class EXTRV_W_MM_ENC : POOL32A_2RAC_FMT<"extrv.w", 0b00111010>;
class EXTRV_R_W_MM_ENC : POOL32A_2RAC_FMT<"extrv_r.w", 0b01111010>;
class EXTRV_RS_W_MM_ENC : POOL32A_2RAC_FMT<"extrv_rs.w", 0b10111010>;
class EXTRV_S_H_MM_ENC : POOL32A_2RAC_FMT<"extrv_s.h", 0b11111010>;
class DPS_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dps.w.ph", 0b00010010>;
class DPSQ_S_W_PH_MM_ENC : POOL32A_2RAC_FMT<"dpsq_s.w.ph", 0b00011010>;
class DPSQ_SA_L_W_MM_ENC : POOL32A_2RAC_FMT<"dpsq_sa.l.w", 0b01011010>;
class DPSQX_S_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpsqx_s.w.ph", 0b10011010>;
class DPSQX_SA_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpsqx_sa.w.ph", 0b11011010>;
class DPSU_H_QBL_MM_ENC : POOL32A_2RAC_FMT<"dpsu.h.qbl", 0b10010010>;
class DPSU_H_QBR_MM_ENC : POOL32A_2RAC_FMT<"dpsu.h.qbr", 0b11010010>;
class DPSX_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"dpsx.w.ph", 0b01010010>;
class MUL_PH_MMR2_ENC : POOL32A_3R_FMT<"mul.ph", 0b00000101101>;
class MUL_S_PH_MMR2_ENC : POOL32A_3R_FMT<"mul_s.ph", 0b10000101101>;
class MULEQ_S_W_PHL_MM_ENC : POOL32A_3RB0_FMT<"muleq_s.w.phl", 0b0000100101>;
class MULEQ_S_W_PHR_MM_ENC : POOL32A_3RB0_FMT<"muleq_s.w.phr", 0b0001100101>;
class MULEU_S_PH_QBL_MM_ENC : POOL32A_3RB0_FMT<"muleu_s.ph.qbl", 0b0010010101>;
class MULEU_S_PH_QBR_MM_ENC : POOL32A_3RB0_FMT<"muleu_s.ph.qbr", 0b0011010101>;
class MULQ_RS_PH_MM_ENC : POOL32A_3RB0_FMT<"mulq_rs.ph", 0b0100010101>;
class MULQ_RS_W_MMR2_ENC : POOL32A_3RB0_FMT<"mulq_rs.w", 0b0110010101>;
class MULQ_S_PH_MMR2_ENC : POOL32A_3RB0_FMT<"mulq_s.ph", 0b0101010101>;
class MULQ_S_W_MMR2_ENC : POOL32A_3RB0_FMT<"mulq_s.w", 0b0111010101>;
class PRECR_QB_PH_MMR2_ENC : POOL32A_3RB0_FMT<"precr.qb.ph", 0b0001101101>;
class PRECR_SRA_PH_W_MMR2_ENC
    : POOL32A_2RSA5_FMT<"precr_sra.ph.w", 0b01111001101>;
class PRECR_SRA_R_PH_W_MMR2_ENC
    : POOL32A_2RSA5_FMT<"precr_sra_r.ph.w", 0b11111001101>;
class PRECRQ_PH_W_MM_ENC : POOL32A_3RB0_FMT<"precrq.ph.w", 0b0011101101>;
class PRECRQ_QB_PH_MM_ENC : POOL32A_3RB0_FMT<"precrq.qb.ph", 0b0010101101>;
class PRECRQU_S_QB_PH_MM_ENC
    : POOL32A_3RB0_FMT<"precrqu_s.qb.ph", 0b0101101101>;
class PRECRQ_RS_PH_W_MM_ENC : POOL32A_3RB0_FMT<"precrq_rs.ph.w", 0b0100101101>;
class LBUX_MM_ENC : POOL32A_1RMEMB0_FMT<"lbux", 0b1000100101>;
class LHX_MM_ENC : POOL32A_1RMEMB0_FMT<"lhx", 0b0101100101>;
class LWX_MM_ENC : POOL32A_1RMEMB0_FMT<"lwx", 0b0110100101>;
class MAQ_S_W_PHL_MM_ENC : POOL32A_2RAC_FMT<"maq_s.w.phl", 0b01101001>;
class MAQ_SA_W_PHL_MM_ENC : POOL32A_2RAC_FMT<"maq_sa.w.phl", 0b11101001>;
class MAQ_S_W_PHR_MM_ENC : POOL32A_2RAC_FMT<"maq_s.w.phr", 0b00101001>;
class MAQ_SA_W_PHR_MM_ENC : POOL32A_2RAC_FMT<"maq_sa.w.phr", 0b10101001>;
class MFHI_MM_ENC : POOL32A_1RAC_FMT<"mfhi", 0b00000001>;
class MFLO_MM_ENC : POOL32A_1RAC_FMT<"mflo", 0b01000001>;
class MTHI_MM_ENC : POOL32A_1RAC_FMT<"mthi", 0b10000001>;
class MTLO_MM_ENC : POOL32A_1RAC_FMT<"mthi", 0b11000001>;
class PREPEND_MMR2_ENC : POOL32A_2RSA5B0_FMT<"prepend", 0b1001010101>;
class RADDU_W_QB_MM_ENC : POOL32A_2R_FMT<"raddu.w.qb", 0b1111000100>;
class RDDSP_MM_ENC : POOL32A_1RMASK7_FMT<"rddsp", 0b00011001>;
class REPL_PH_MM_ENC : POOL32A_1RIMM10_FMT<"repl.ph", 0b0000111101>;
class REPL_QB_MM_ENC : POOL32A_1RIMM8_FMT<"repl.qb", 0b010111>;
class REPLV_PH_MM_ENC : POOL32A_2R_FMT<"replv.ph", 0b0000001100>;
class REPLV_QB_MM_ENC : POOL32A_2R_FMT<"replv.qb", 0b0001001100>;
class MTHLIP_MM_ENC : POOL32A_1RAC_FMT<"mthlip", 0b00001001>;
class PACKRL_PH_MM_ENC : POOL32A_3RB0_FMT<"packrl.ph", 0b0110101101>;
class PICK_PH_MM_ENC : POOL32A_3RB0_FMT<"pick.ph", 0b1000101101>;
class PICK_QB_MM_ENC : POOL32A_3RB0_FMT<"pick.qb", 0b0111101101>;
class SHILO_MM_ENC : POOL32A_4B0SHIFT6AC4B0_FMT<"shilo", 0b0000011101>;
class SHILOV_MM_ENC : POOL32A_5B01RAC_FMT<"shilov", 0b01001001>;
class WRDSP_MM_ENC : POOL32A_1RMASK7_FMT<"wrdsp", 0b01011001>;
class APPEND_MMR2_ENC : POOL32A_2RSA5B0_FMT<"append", 0b1000010101>;
class MODSUB_MM_ENC : POOL32A_3RB0_FMT<"modsub", 0b1010010101>;
class MULSA_W_PH_MMR2_ENC : POOL32A_2RAC_FMT<"mulsa.w.ph", 0b10110010>;
class MULSAQ_S_W_PH_MM_ENC : POOL32A_2RAC_FMT<"mulsaq_s.w.ph", 0b11110010>;
class BPOSGE32C_MMR3_ENC : POOL32I_IMMB0_FMT<"bposge32c", 0b11001>;
class BITREV_MM_ENC : POOL32A_2R_FMT<"bitrev", 0b0011000100>;
class BALIGN_MMR2_ENC : POOL32A_2RBP_FMT<"balign">;
class BPOSGE32_MM_ENC : POOL32I_IMMB0_FMT<"bposge32", 0b11011>;
class CMP_EQ_PH_MM_ENC : POOL32A_2RB0_FMT<"cmp.eq.ph", 0b0000000101>;
class CMP_LE_PH_MM_ENC : POOL32A_2RB0_FMT<"cmp.le.ph", 0b0010000101>;
class CMP_LT_PH_MM_ENC : POOL32A_2RB0_FMT<"cmp.lt.ph", 0b0001000101>;
class CMPGDU_EQ_QB_MMR2_ENC : POOL32A_3RB0_FMT<"cmpgdu.eq.qb", 0b0110000101>;
class CMPGDU_LT_QB_MMR2_ENC : POOL32A_3RB0_FMT<"cmpgdu.lt.qb", 0b0111000101>;
class CMPGDU_LE_QB_MMR2_ENC : POOL32A_3RB0_FMT<"cmpgdu.le.qb", 0b1000000101>;
class CMPGU_EQ_QB_MM_ENC : POOL32S_3RB0_FMT<"cmpgu.eq.qb", 0b0011000101>;
class CMPGU_LT_QB_MM_ENC : POOL32S_3RB0_FMT<"cmpgu.lt.qb", 0b0100000101>;
class CMPGU_LE_QB_MM_ENC : POOL32S_3RB0_FMT<"cmpgu.le.qb", 0b0101000101>;
class CMPU_EQ_QB_MM_ENC : POOL32A_2R2B0_FMT<"cmpu.eq.qb", 0b1001000101>;
class CMPU_LT_QB_MM_ENC : POOL32A_2R2B0_FMT<"cmpu.lt.qb", 0b1010000101>;
class CMPU_LE_QB_MM_ENC : POOL32A_2R2B0_FMT<"cmpu.le.qb", 0b1011000101>;

// Instruction desc.
class ABSQ_S_PH_MM_R2_DESC_BASE<string opstr, SDPatternOperator OpNode,
                                InstrItinClass itin, RegisterOperand ROD,
                                RegisterOperand ROS = ROD> {
  dag OutOperandList = (outs ROD:$rt);
  dag InOperandList = (ins ROS:$rs);
  string AsmString = !strconcat(opstr, "\t$rt, $rs");
  list<dag> Pattern = [(set ROD:$rt, (OpNode ROS:$rs))];
  InstrItinClass Itinerary = itin;
}
class ABSQ_S_PH_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "absq_s.ph", int_mips_absq_s_ph, NoItinerary, DSPROpnd>, Defs<[DSPOutFlag20]>;
class ABSQ_S_W_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "absq_s.w", int_mips_absq_s_w, NoItinerary, GPR32Opnd>, Defs<[DSPOutFlag20]>;
class ABSQ_S_QB_MMR2_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "absq_s.qb", int_mips_absq_s_qb, NoItinerary, DSPROpnd>, Defs<[DSPOutFlag20]>;
class PRECEQ_W_PHL_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceq.w.phl", int_mips_preceq_w_phl, NoItinerary, GPR32Opnd, DSPROpnd>;
class PRECEQ_W_PHR_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceq.w.phr", int_mips_preceq_w_phr, NoItinerary, GPR32Opnd, DSPROpnd>;
class PRECEQU_PH_QBL_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "precequ.ph.qbl", int_mips_precequ_ph_qbl, NoItinerary, DSPROpnd>;
class PRECEQU_PH_QBLA_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "precequ.ph.qbla", int_mips_precequ_ph_qbla, NoItinerary, DSPROpnd>;
class PRECEQU_PH_QBR_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "precequ.ph.qbr", int_mips_precequ_ph_qbr, NoItinerary, DSPROpnd>;
class PRECEQU_PH_QBRA_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "precequ.ph.qbra", int_mips_precequ_ph_qbra, NoItinerary, DSPROpnd>;
class PRECEU_PH_QBL_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceu.ph.qbl", int_mips_preceu_ph_qbl, NoItinerary, DSPROpnd>;
class PRECEU_PH_QBLA_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceu.ph.qbla", int_mips_preceu_ph_qbla, NoItinerary, DSPROpnd>;
class PRECEU_PH_QBR_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceu.ph.qbr", int_mips_preceu_ph_qbr, NoItinerary, DSPROpnd>;
class PRECEU_PH_QBRA_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<
  "preceu.ph.qbra", int_mips_preceu_ph_qbra, NoItinerary, DSPROpnd>;

class SHLL_R2_MM_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           SDPatternOperator ImmPat, InstrItinClass itin,
                           RegisterOperand RO, Operand ImmOpnd> {
  dag OutOperandList = (outs RO:$rt);
  dag InOperandList = (ins RO:$rs, ImmOpnd:$sa);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $sa");
  list<dag> Pattern = [(set RO:$rt, (OpNode RO:$rs, ImmPat:$sa))];
  InstrItinClass Itinerary = itin;
  bit hasSideEffects = 1;
}
class SHLL_PH_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shll.ph", null_frag, immZExt4, NoItinerary, DSPROpnd, uimm4>,
  Defs<[DSPOutFlag22]>;
class SHLL_S_PH_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shll_s.ph", int_mips_shll_s_ph, immZExt4, NoItinerary, DSPROpnd, uimm4>,
  Defs<[DSPOutFlag22]>;
class SHLL_QB_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shll.qb", null_frag, immZExt3, NoItinerary, DSPROpnd, uimm3>,
  Defs<[DSPOutFlag22]>;
class SHLL_S_W_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shll_s.w", int_mips_shll_s_w, immZExt5, NoItinerary, GPR32Opnd, uimm5>,
  Defs<[DSPOutFlag22]>;
class SHRA_QB_MMR2_DESC : SHLL_R2_MM_DESC_BASE<
  "shra.qb", null_frag, immZExt3, NoItinerary, DSPROpnd, uimm3>;
class SHRA_R_QB_MMR2_DESC : SHLL_R2_MM_DESC_BASE<
  "shra_r.qb", int_mips_shra_r_qb, immZExt3, NoItinerary, DSPROpnd, uimm3>;
class SHRA_PH_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shra.ph", null_frag, immZExt4, NoItinerary, DSPROpnd, uimm4>;
class SHRA_R_PH_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shra_r.ph", int_mips_shra_r_ph, immZExt4, NoItinerary, DSPROpnd, uimm4>;
class SHRA_R_W_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shra_r.w", int_mips_shra_r_w, immZExt5, NoItinerary, GPR32Opnd, uimm5>;
class SHRL_QB_MM_DESC : SHLL_R2_MM_DESC_BASE<
  "shrl.qb", null_frag, immZExt3, NoItinerary, DSPROpnd, uimm3>;
class SHRL_PH_MMR2_DESC : SHLL_R2_MM_DESC_BASE<
  "shrl.ph", null_frag, immZExt4, NoItinerary, DSPROpnd, uimm4>;

class SHLLV_R3_MM_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                            InstrItinClass itin, RegisterOperand RO> {
  dag OutOperandList = (outs RO:$rd);
  dag InOperandList =  (ins RO:$rt, GPR32Opnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt, $rs");
  list<dag> Pattern = [(set RO:$rd, (OpNode RO:$rt, GPR32Opnd:$rs))];
  InstrItinClass Itinerary = itin;
}
class SHLLV_PH_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shllv.ph", int_mips_shll_ph, NoItinerary, DSPROpnd>, Defs<[DSPOutFlag22]>;
class SHLLV_S_PH_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shllv_s.ph", int_mips_shll_s_ph, NoItinerary, DSPROpnd>,
  Defs<[DSPOutFlag22]>;
class SHLLV_QB_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shllv.qb", int_mips_shll_qb, NoItinerary, DSPROpnd>, Defs<[DSPOutFlag22]>;
class SHLLV_S_W_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shllv_s.w", int_mips_shll_s_w, NoItinerary, GPR32Opnd>, Defs<[DSPOutFlag22]>;
class SHRAV_PH_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrav.ph", int_mips_shra_ph, NoItinerary, DSPROpnd>;
class SHRAV_R_PH_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrav_r.ph", int_mips_shra_r_ph, NoItinerary, DSPROpnd>;
class SHRAV_QB_MMR2_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrav.qb", int_mips_shra_qb, NoItinerary, DSPROpnd>;
class SHRAV_R_QB_MMR2_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrav_r.qb", int_mips_shra_r_qb, NoItinerary, DSPROpnd>;
class SHRAV_R_W_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrav_r.w", int_mips_shra_r_w, NoItinerary, GPR32Opnd>;
class SHRLV_PH_MMR2_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrlv.ph", int_mips_shrl_ph, NoItinerary, DSPROpnd>;
class SHRLV_QB_MM_DESC : SHLLV_R3_MM_DESC_BASE<
  "shrlv.qb", int_mips_shrl_qb, NoItinerary, DSPROpnd>;

class EXT_MM_2R_DESC_BASE<string instr_asm> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins ACC64DSPOpnd:$ac, GPR32Opnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $ac, $rs");
  InstrItinClass Itinerary = NoItinerary;
}
class EXT_MM_1R_DESC_BASE<string instr_asm> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins ACC64DSPOpnd:$ac, uimm5:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rt, $ac, $imm");
  InstrItinClass Itinerary = NoItinerary;
}

class EXTP_MM_DESC : EXT_MM_1R_DESC_BASE<"extp">,
                     Uses<[DSPPos]>,
                     Defs<[DSPEFI]>;
class EXTPDP_MM_DESC : EXT_MM_1R_DESC_BASE<"extpdp">,
                       Uses<[DSPPos]>,
                       Defs<[DSPPos, DSPEFI]>;
class EXTPDPV_MM_DESC : EXT_MM_2R_DESC_BASE<"extpdpv">,
                        Uses<[DSPPos]>,
                        Defs<[DSPPos, DSPEFI]>;
class EXTPV_MM_DESC : EXT_MM_2R_DESC_BASE<"extpv">,
                      Uses<[DSPPos]>,
                      Defs<[DSPEFI]>;
class EXTR_W_MM_DESC : EXT_MM_1R_DESC_BASE<"extr.w">,
                       Defs<[DSPOutFlag23]>;
class EXTR_R_W_MM_DESC : EXT_MM_1R_DESC_BASE<"extr_r.w">,
                         Defs<[DSPOutFlag23]>;
class EXTR_RS_W_MM_DESC : EXT_MM_1R_DESC_BASE<"extr_rs.w">,
                          Defs<[DSPOutFlag23]>;
class EXTR_S_H_MM_DESC : EXT_MM_1R_DESC_BASE<"extr_s.h">,
                         Defs<[DSPOutFlag23]>;
class EXTRV_W_MM_DESC : EXT_MM_2R_DESC_BASE<"extrv.w">, Defs<[DSPOutFlag23]>;
class EXTRV_R_W_MM_DESC : EXT_MM_2R_DESC_BASE<"extrv_r.w">,
                          Defs<[DSPOutFlag23]>;
class EXTRV_RS_W_MM_DESC : EXT_MM_2R_DESC_BASE<"extrv_rs.w">,
                           Defs<[DSPOutFlag23]>;
class EXTRV_S_H_MM_DESC : EXT_MM_2R_DESC_BASE<"extrv_s.h">,
                          Defs<[DSPOutFlag23]>;

class MFHI_MM_DESC_BASE<string instr_asm, RegisterOperand RO, SDNode OpNode,
                        InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rs);
  dag InOperandList = (ins RO:$ac);
  string AsmString = !strconcat(instr_asm, "\t$rs, $ac");
  list<dag> Pattern = [(set GPR32Opnd:$rs, (OpNode RO:$ac))];
  InstrItinClass Itinerary = itin;
}

class MFHI_MM_DESC : MFHI_MM_DESC_BASE<"mfhi", ACC64DSPOpnd, MipsMFHI,
                                       NoItinerary>;
class MFLO_MM_DESC : MFHI_MM_DESC_BASE<"mflo", ACC64DSPOpnd, MipsMFLO,
                                       NoItinerary>;

class RADDU_W_QB_MM_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins DSPROpnd:$rs);
  string AsmString = !strconcat("raddu.w.qb", "\t$rt, $rs");
  list<dag> Pattern = [(set GPR32Opnd:$rt, (int_mips_raddu_w_qb DSPROpnd:$rs))];
  InstrItinClass Itinerary = NoItinerary;
  string BaseOpcode = "raddu.w.qb";
}

class RDDSP_MM_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins uimm7:$mask);
  string AsmString = !strconcat("rddsp", "\t$rt, $mask");
  list<dag> Pattern = [(set GPR32Opnd:$rt, (int_mips_rddsp timmZExt7:$mask))];
  InstrItinClass Itinerary = NoItinerary;
}

class REPL_QB_MM_DESC {
  dag OutOperandList = (outs DSPROpnd:$rt);
  dag InOperandList = (ins uimm8:$imm);
  string AsmString = !strconcat("repl.qb", "\t$rt, $imm");
  list<dag> Pattern = [(set DSPROpnd:$rt, (int_mips_repl_qb immZExt8:$imm))];
  InstrItinClass Itinerary = NoItinerary;
}

class REPLV_PH_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<"replv.ph", int_mips_repl_ph,
                                                   NoItinerary, DSPROpnd,
                                                   GPR32Opnd>;
class REPLV_QB_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<"replv.qb", int_mips_repl_qb,
                                                   NoItinerary, DSPROpnd,
                                                   GPR32Opnd>;

class WRDSP_MM_DESC {
  dag OutOperandList = (outs);
  dag InOperandList = (ins GPR32Opnd:$rt, uimm7:$mask);
  string AsmString = !strconcat("wrdsp", "\t$rt, $mask");
  list<dag> Pattern = [(int_mips_wrdsp GPR32Opnd:$rt, timmZExt7:$mask)];
  InstrItinClass Itinerary = NoItinerary;
}

class BPOSGE32C_MMR3_DESC {
  dag OutOperandList = (outs);
  dag InOperandList = (ins brtarget1SImm16:$offset);
  string AsmString = !strconcat("bposge32c", "\t$offset");
  InstrItinClass Itinerary = NoItinerary;
  bit isBranch = 1;
  bit isTerminator = 1;
  bit hasDelaySlot = 0;
}

class BALIGN_MMR2_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs, uimm2:$bp, GPR32Opnd:$src);
  string AsmString = !strconcat("balign", "\t$rt, $rs, $bp");
  list<dag> Pattern =  [(set GPR32Opnd:$rt, (int_mips_balign GPR32Opnd:$src,
                                                             GPR32Opnd:$rs,
                                                             immZExt2:$bp))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$src = $rt";
}

class BITREV_MM_DESC : ABSQ_S_PH_MM_R2_DESC_BASE<"bitrev", int_mips_bitrev,
                                                 NoItinerary, GPR32Opnd>;

class BPOSGE32_MM_DESC : BPOSGE32_DESC_BASE<"bposge32", brtarget_mm,
                                            NoItinerary>;

let DecoderNamespace = "MicroMipsDSP", Arch = "mmdsp",
    EncodingPredicates = [InMicroMips], ASEPredicate = [HasDSP] in {
  def LWDSP_MM : Load<"lw", DSPROpnd, null_frag, II_LW>, DspMMRel,
                 LW_FM_MM<0x3f>;
  def SWDSP_MM : Store<"sw", DSPROpnd, null_frag, II_SW>, DspMMRel,
                 LW_FM_MM<0x3e>;
}
// Instruction defs.
// microMIPS DSP Rev 1
def ADDQ_PH_MM : DspMMRel, ADDQ_PH_MM_ENC, ADDQ_PH_DESC;
def ADDQ_S_PH_MM : DspMMRel, ADDQ_S_PH_MM_ENC, ADDQ_S_PH_DESC;
def ADDQ_S_W_MM : DspMMRel, ADDQ_S_W_MM_ENC, ADDQ_S_W_DESC;
def ADDU_QB_MM : DspMMRel, ADDU_QB_MM_ENC, ADDU_QB_DESC;
def ADDU_S_QB_MM : DspMMRel, ADDU_S_QB_MM_ENC, ADDU_S_QB_DESC;
def ADDSC_MM : DspMMRel, ADDSC_MM_ENC, ADDSC_DESC;
def ADDWC_MM : DspMMRel, ADDWC_MM_ENC, ADDWC_DESC;
def DPAQ_S_W_PH_MM : DspMMRel, DPAQ_S_W_PH_MM_ENC, DPAQ_S_W_PH_DESC;
def DPAQ_SA_L_W_MM : DspMMRel, DPAQ_SA_L_W_MM_ENC, DPAQ_SA_L_W_DESC;
def DPAU_H_QBL_MM : DspMMRel, DPAU_H_QBL_MM_ENC, DPAU_H_QBL_DESC;
def DPAU_H_QBR_MM : DspMMRel, DPAU_H_QBR_MM_ENC, DPAU_H_QBR_DESC;
def ABSQ_S_PH_MM : DspMMRel, ABSQ_S_PH_MM_ENC, ABSQ_S_PH_MM_DESC;
def ABSQ_S_W_MM : DspMMRel, ABSQ_S_W_MM_ENC, ABSQ_S_W_MM_DESC;
def INSV_MM : DspMMRel, INSV_MM_ENC, INSV_DESC;
def MADD_DSP_MM : DspMMRel, MADD_DSP_MM_ENC, MADD_DSP_DESC;
def MADDU_DSP_MM : DspMMRel, MADDU_DSP_MM_ENC, MADDU_DSP_DESC;
def MSUB_DSP_MM : DspMMRel, MSUB_DSP_MM_ENC, MSUB_DSP_DESC;
def MSUBU_DSP_MM : DspMMRel, MSUBU_DSP_MM_ENC, MSUBU_DSP_DESC;
def MULT_DSP_MM : DspMMRel, MULT_DSP_MM_ENC, MULT_DSP_DESC;
def MULTU_DSP_MM : DspMMRel, MULTU_DSP_MM_ENC, MULTU_DSP_DESC;
def SHLL_PH_MM : DspMMRel, SHLL_PH_MM_ENC, SHLL_PH_MM_DESC;
def SHLL_S_PH_MM : DspMMRel, SHLL_S_PH_MM_ENC, SHLL_S_PH_MM_DESC;
def SHLL_QB_MM : DspMMRel, SHLL_QB_MM_ENC, SHLL_QB_MM_DESC;
def SHLLV_PH_MM : DspMMRel, SHLLV_PH_MM_ENC, SHLLV_PH_MM_DESC;
def SHLLV_S_PH_MM : DspMMRel, SHLLV_S_PH_MM_ENC, SHLLV_S_PH_MM_DESC;
def SHLLV_QB_MM : DspMMRel, SHLLV_QB_MM_ENC, SHLLV_QB_MM_DESC;
def SHLLV_S_W_MM : DspMMRel, SHLLV_S_W_MM_ENC, SHLLV_S_W_MM_DESC;
def SHLL_S_W_MM : DspMMRel, SHLL_S_W_MM_ENC, SHLL_S_W_MM_DESC;
def SHRA_PH_MM : DspMMRel, SHRA_PH_MM_ENC, SHRA_PH_MM_DESC;
def SHRA_R_PH_MM : DspMMRel, SHRA_R_PH_MM_ENC, SHRA_R_PH_MM_DESC;
def SHRAV_PH_MM : DspMMRel, SHRAV_PH_MM_ENC, SHRAV_PH_MM_DESC;
def SHRAV_R_PH_MM : DspMMRel, SHRAV_R_PH_MM_ENC, SHRAV_R_PH_MM_DESC;
def SHRAV_R_W_MM : DspMMRel, SHRAV_R_W_MM_ENC, SHRAV_R_W_MM_DESC;
def SHRA_R_W_MM : DspMMRel, SHRA_R_W_MM_ENC, SHRA_R_W_MM_DESC;
def SHRL_QB_MM : DspMMRel, SHRL_QB_MM_ENC, SHRL_QB_MM_DESC;
def SHRLV_QB_MM : DspMMRel, SHRLV_QB_MM_ENC, SHRLV_QB_MM_DESC;
def PRECEQ_W_PHL_MM : DspMMRel, PRECEQ_W_PHL_MM_ENC, PRECEQ_W_PHL_MM_DESC;
def PRECEQ_W_PHR_MM : DspMMRel, PRECEQ_W_PHR_MM_ENC, PRECEQ_W_PHR_MM_DESC;
def PRECEQU_PH_QBL_MM : DspMMRel, PRECEQU_PH_QBL_MM_ENC, PRECEQU_PH_QBL_MM_DESC;
def PRECEQU_PH_QBLA_MM : DspMMRel, PRECEQU_PH_QBLA_MM_ENC,
                         PRECEQU_PH_QBLA_MM_DESC;
def PRECEQU_PH_QBR_MM : DspMMRel, PRECEQU_PH_QBR_MM_ENC, PRECEQU_PH_QBR_MM_DESC;
def PRECEQU_PH_QBRA_MM : DspMMRel, PRECEQU_PH_QBRA_MM_ENC,
                         PRECEQU_PH_QBRA_MM_DESC;
def PRECEU_PH_QBL_MM : DspMMRel, PRECEU_PH_QBL_MM_ENC, PRECEU_PH_QBL_MM_DESC;
def PRECEU_PH_QBLA_MM : DspMMRel, PRECEU_PH_QBLA_MM_ENC, PRECEU_PH_QBLA_MM_DESC;
def PRECEU_PH_QBR_MM : DspMMRel, PRECEU_PH_QBR_MM_ENC, PRECEU_PH_QBR_MM_DESC;
def PRECEU_PH_QBRA_MM : DspMMRel, PRECEU_PH_QBRA_MM_ENC, PRECEU_PH_QBRA_MM_DESC;
def SUBQ_PH_MM : DspMMRel, SUBQ_PH_MM_ENC, SUBQ_PH_DESC;
def SUBQ_S_PH_MM : DspMMRel, SUBQ_S_PH_MM_ENC, SUBQ_S_PH_DESC;
def SUBQ_S_W_MM : DspMMRel, SUBQ_S_W_MM_ENC, SUBQ_S_W_DESC;
def SUBU_QB_MM : DspMMRel, SUBU_QB_MM_ENC, SUBU_QB_DESC;
def SUBU_S_QB_MM : DspMMRel, SUBU_S_QB_MM_ENC, SUBU_S_QB_DESC;
def EXTP_MM : DspMMRel, EXTP_MM_ENC, EXTP_MM_DESC;
def EXTPDP_MM : DspMMRel, EXTPDP_MM_ENC, EXTPDP_MM_DESC;
def EXTPDPV_MM : DspMMRel, EXTPDPV_MM_ENC, EXTPDPV_MM_DESC;
def EXTPV_MM : DspMMRel, EXTPV_MM_ENC, EXTPV_MM_DESC;
def EXTR_W_MM : DspMMRel, EXTR_W_MM_ENC, EXTR_W_MM_DESC;
def EXTR_R_W_MM : DspMMRel, EXTR_R_W_MM_ENC, EXTR_R_W_MM_DESC;
def EXTR_RS_W_MM : DspMMRel, EXTR_RS_W_MM_ENC, EXTR_RS_W_MM_DESC;
def EXTR_S_H_MM : DspMMRel, EXTR_S_H_MM_ENC, EXTR_S_H_MM_DESC;
def EXTRV_W_MM : DspMMRel, EXTRV_W_MM_ENC, EXTRV_W_MM_DESC;
def EXTRV_R_W_MM : DspMMRel, EXTRV_R_W_MM_ENC, EXTRV_R_W_MM_DESC;
def EXTRV_RS_W_MM : DspMMRel, EXTRV_RS_W_MM_ENC, EXTRV_RS_W_MM_DESC;
def EXTRV_S_H_MM : DspMMRel, EXTRV_S_H_MM_ENC, EXTRV_S_H_MM_DESC;
def DPSQ_S_W_PH_MM : DspMMRel, DPSQ_S_W_PH_MM_ENC, DPSQ_S_W_PH_DESC;
def DPSQ_SA_L_W_MM : DspMMRel, DPSQ_SA_L_W_MM_ENC, DPSQ_SA_L_W_DESC;
def DPSU_H_QBL_MM : DspMMRel, DPSU_H_QBL_MM_ENC, DPSU_H_QBL_DESC;
def DPSU_H_QBR_MM : DspMMRel, DPSU_H_QBR_MM_ENC, DPSU_H_QBR_DESC;
def MULEQ_S_W_PHL_MM : DspMMRel, MULEQ_S_W_PHL_MM_ENC, MULEQ_S_W_PHL_DESC;
def MULEQ_S_W_PHR_MM : DspMMRel, MULEQ_S_W_PHR_MM_ENC, MULEQ_S_W_PHR_DESC;
def MULEU_S_PH_QBL_MM : DspMMRel, MULEU_S_PH_QBL_MM_ENC, MULEU_S_PH_QBL_DESC;
def MULEU_S_PH_QBR_MM : DspMMRel, MULEU_S_PH_QBR_MM_ENC, MULEU_S_PH_QBR_DESC;
def MULQ_RS_PH_MM : DspMMRel, MULQ_RS_PH_MM_ENC, MULQ_RS_PH_DESC;
def PRECRQ_PH_W_MM : DspMMRel, PRECRQ_PH_W_MM_ENC, PRECRQ_PH_W_DESC;
def PRECRQ_QB_PH_MM : DspMMRel, PRECRQ_QB_PH_MM_ENC, PRECRQ_QB_PH_DESC;
def PRECRQU_S_QB_PH_MM : DspMMRel, PRECRQU_S_QB_PH_MM_ENC, PRECRQU_S_QB_PH_DESC;
def PRECRQ_RS_PH_W_MM : DspMMRel, PRECRQ_RS_PH_W_MM_ENC, PRECRQ_RS_PH_W_DESC;
def LBUX_MM : DspMMRel, LBUX_MM_ENC, LBUX_DESC;
def LHX_MM : DspMMRel, LHX_MM_ENC, LHX_DESC;
def LWX_MM : DspMMRel, LWX_MM_ENC, LWX_DESC;
def MAQ_S_W_PHL_MM : DspMMRel, MAQ_S_W_PHL_MM_ENC, MAQ_S_W_PHL_DESC;
def MAQ_SA_W_PHL_MM : DspMMRel, MAQ_SA_W_PHL_MM_ENC, MAQ_SA_W_PHL_DESC;
def MAQ_S_W_PHR_MM : DspMMRel, MAQ_S_W_PHR_MM_ENC, MAQ_S_W_PHR_DESC;
def MAQ_SA_W_PHR_MM : DspMMRel, MAQ_SA_W_PHR_MM_ENC, MAQ_SA_W_PHR_DESC;
def MFHI_DSP_MM : DspMMRel, MFHI_MM_ENC, MFHI_MM_DESC;
def MFLO_DSP_MM : DspMMRel, MFLO_MM_ENC, MFLO_MM_DESC;
def MTHI_DSP_MM : DspMMRel, MTHI_MM_ENC, MTHI_DESC;
def MTLO_DSP_MM : DspMMRel, MTLO_MM_ENC, MTLO_DESC;
def RADDU_W_QB_MM : DspMMRel, RADDU_W_QB_MM_ENC, RADDU_W_QB_MM_DESC;
def RDDSP_MM : DspMMRel, RDDSP_MM_ENC, RDDSP_MM_DESC;
def REPL_PH_MM : DspMMRel, REPL_PH_MM_ENC, REPL_PH_DESC;
def REPL_QB_MM : DspMMRel, REPL_QB_MM_ENC, REPL_QB_MM_DESC;
def REPLV_PH_MM : DspMMRel, REPLV_PH_MM_ENC, REPLV_PH_MM_DESC;
def REPLV_QB_MM : DspMMRel, REPLV_QB_MM_ENC, REPLV_QB_MM_DESC;
def MTHLIP_MM : DspMMRel, MTHLIP_MM_ENC, MTHLIP_DESC;
def PACKRL_PH_MM : DspMMRel, PACKRL_PH_MM_ENC, PACKRL_PH_DESC;
def PICK_PH_MM : DspMMRel, PICK_PH_MM_ENC, PICK_PH_DESC;
def PICK_QB_MM : DspMMRel, PICK_QB_MM_ENC, PICK_QB_DESC;
def SHILO_MM : DspMMRel, SHILO_MM_ENC, SHILO_DESC;
def SHILOV_MM : DspMMRel, SHILOV_MM_ENC, SHILOV_DESC;
def WRDSP_MM : DspMMRel, WRDSP_MM_ENC, WRDSP_MM_DESC;
def MODSUB_MM : DspMMRel, MODSUB_MM_ENC, MODSUB_DESC;
def MULSAQ_S_W_PH_MM : DspMMRel, MULSAQ_S_W_PH_MM_ENC, MULSAQ_S_W_PH_DESC;
def BITREV_MM : DspMMRel, BITREV_MM_ENC, BITREV_MM_DESC;
def BPOSGE32_MM : DspMMRel, BPOSGE32_MM_ENC, BPOSGE32_MM_DESC,
                  ISA_MICROMIPS32_NOT_MIPS32R6;
def CMP_EQ_PH_MM : DspMMRel, CMP_EQ_PH_MM_ENC, CMP_EQ_PH_DESC;
def CMP_LT_PH_MM : DspMMRel, CMP_LT_PH_MM_ENC, CMP_LT_PH_DESC;
def CMP_LE_PH_MM : DspMMRel, CMP_LE_PH_MM_ENC, CMP_LE_PH_DESC;
def CMPGU_EQ_QB_MM : DspMMRel, CMPGU_EQ_QB_MM_ENC, CMPGU_EQ_QB_DESC;
def CMPGU_LT_QB_MM : DspMMRel, CMPGU_LT_QB_MM_ENC, CMPGU_LT_QB_DESC;
def CMPGU_LE_QB_MM : DspMMRel, CMPGU_LE_QB_MM_ENC, CMPGU_LE_QB_DESC;
def CMPU_EQ_QB_MM : DspMMRel, CMPU_EQ_QB_MM_ENC, CMPU_EQ_QB_DESC;
def CMPU_LT_QB_MM : DspMMRel, CMPU_LT_QB_MM_ENC, CMPU_LT_QB_DESC;
def CMPU_LE_QB_MM : DspMMRel, CMPU_LE_QB_MM_ENC, CMPU_LE_QB_DESC;
// microMIPS DSP Rev 2
def ABSQ_S_QB_MMR2 : DspMMRel, ABSQ_S_QB_MMR2_ENC, ABSQ_S_QB_MMR2_DESC,
                     ISA_DSPR2;
def ADDQH_PH_MMR2 : DspMMRel, ADDQH_PH_MMR2_ENC, ADDQH_PH_DESC, ISA_DSPR2;
def ADDQH_R_PH_MMR2 : DspMMRel, ADDQH_R_PH_MMR2_ENC, ADDQH_R_PH_DESC, ISA_DSPR2;
def ADDQH_W_MMR2 : DspMMRel, ADDQH_W_MMR2_ENC, ADDQH_W_DESC, ISA_DSPR2;
def ADDQH_R_W_MMR2 : DspMMRel, ADDQH_R_W_MMR2_ENC, ADDQH_R_W_DESC, ISA_DSPR2;
def ADDU_PH_MMR2 : DspMMRel, ADDU_PH_MMR2_ENC, ADDU_PH_DESC, ISA_DSPR2;
def ADDU_S_PH_MMR2 : DspMMRel, ADDU_S_PH_MMR2_ENC, ADDU_S_PH_DESC, ISA_DSPR2;
def ADDUH_QB_MMR2 : DspMMRel, ADDUH_QB_MMR2_ENC, ADDUH_QB_DESC, ISA_DSPR2;
def ADDUH_R_QB_MMR2 : DspMMRel, ADDUH_R_QB_MMR2_ENC, ADDUH_R_QB_DESC, ISA_DSPR2;
def DPA_W_PH_MMR2 : DspMMRel, DPA_W_PH_MMR2_ENC, DPA_W_PH_DESC, ISA_DSPR2;
def DPAQX_S_W_PH_MMR2 : DspMMRel, DPAQX_S_W_PH_MMR2_ENC, DPAQX_S_W_PH_DESC,
                        ISA_DSPR2;
def DPAQX_SA_W_PH_MMR2 : DspMMRel, DPAQX_SA_W_PH_MMR2_ENC, DPAQX_SA_W_PH_DESC,
                         ISA_DSPR2;
def DPAX_W_PH_MMR2 : DspMMRel, DPAX_W_PH_MMR2_ENC, DPAX_W_PH_DESC, ISA_DSPR2;
def SHRA_QB_MMR2 : DspMMRel, SHRA_QB_MMR2_ENC, SHRA_QB_MMR2_DESC, ISA_DSPR2;
def SHRA_R_QB_MMR2 : DspMMRel, SHRA_R_QB_MMR2_ENC, SHRA_R_QB_MMR2_DESC,
                     ISA_DSPR2;
def SHRAV_QB_MMR2 : DspMMRel, SHRAV_QB_MMR2_ENC, SHRAV_QB_MMR2_DESC, ISA_DSPR2;
def SHRAV_R_QB_MMR2 : DspMMRel, SHRAV_R_QB_MMR2_ENC, SHRAV_R_QB_MMR2_DESC,
                      ISA_DSPR2;
def BALIGN_MMR2 : DspMMRel, BALIGN_MMR2_ENC, BALIGN_MMR2_DESC, ISA_DSPR2;
def CMPGDU_EQ_QB_MMR2 : DspMMRel, CMPGDU_EQ_QB_MMR2_ENC, CMPGDU_EQ_QB_DESC,
                        ISA_DSPR2;
def CMPGDU_LT_QB_MMR2 : DspMMRel, CMPGDU_LT_QB_MMR2_ENC, CMPGDU_LT_QB_DESC,
                        ISA_DSPR2;
def CMPGDU_LE_QB_MMR2 : DspMMRel, CMPGDU_LE_QB_MMR2_ENC, CMPGDU_LE_QB_DESC,
                        ISA_DSPR2;
def SHRL_PH_MMR2 : DspMMRel, SHRL_PH_MMR2_ENC, SHRL_PH_MMR2_DESC, ISA_DSPR2;
def SHRLV_PH_MMR2 : DspMMRel, SHRLV_PH_MMR2_ENC, SHRLV_PH_MMR2_DESC, ISA_DSPR2;
def SUBQH_PH_MMR2 : DspMMRel, SUBQH_PH_MMR2_ENC, SUBQH_PH_DESC, ISA_DSPR2;
def SUBQH_R_PH_MMR2 : DspMMRel, SUBQH_R_PH_MMR2_ENC, SUBQH_R_PH_DESC, ISA_DSPR2;
def SUBQH_W_MMR2 : DspMMRel, SUBQH_W_MMR2_ENC, SUBQH_W_DESC, ISA_DSPR2;
def SUBQH_R_W_MMR2 : DspMMRel, SUBQH_R_W_MMR2_ENC, SUBQH_R_W_DESC, ISA_DSPR2;
def SUBU_PH_MMR2 : DspMMRel, SUBU_PH_MMR2_ENC, SUBU_PH_DESC, ISA_DSPR2;
def SUBU_S_PH_MMR2 : DspMMRel, SUBU_S_PH_MMR2_ENC, SUBU_S_PH_DESC, ISA_DSPR2;
def SUBUH_QB_MMR2 : DspMMRel, SUBUH_QB_MMR2_ENC, SUBUH_QB_DESC, ISA_DSPR2;
def SUBUH_R_QB_MMR2 : DspMMRel, SUBUH_R_QB_MMR2_ENC, SUBUH_R_QB_DESC, ISA_DSPR2;
def DPS_W_PH_MMR2 : DspMMRel, DPS_W_PH_MMR2_ENC, DPS_W_PH_DESC, ISA_DSPR2;
def DPSQX_S_W_PH_MMR2 : DspMMRel, DPSQX_S_W_PH_MMR2_ENC, DPSQX_S_W_PH_DESC,
                        ISA_DSPR2;
def DPSQX_SA_W_PH_MMR2 : DspMMRel, DPSQX_SA_W_PH_MMR2_ENC, DPSQX_SA_W_PH_DESC,
                         ISA_DSPR2;
def DPSX_W_PH_MMR2 : DspMMRel, DPSX_W_PH_MMR2_ENC, DPSX_W_PH_DESC, ISA_DSPR2;
def MUL_PH_MMR2 : DspMMRel, MUL_PH_MMR2_ENC, MUL_PH_DESC, ISA_DSPR2;
def MUL_S_PH_MMR2 : DspMMRel, MUL_S_PH_MMR2_ENC, MUL_S_PH_DESC, ISA_DSPR2;
def MULQ_RS_W_MMR2 : DspMMRel, MULQ_RS_W_MMR2_ENC, MULQ_RS_W_DESC, ISA_DSPR2;
def MULQ_S_PH_MMR2 : DspMMRel, MULQ_S_PH_MMR2_ENC, MULQ_S_PH_DESC, ISA_DSPR2;
def MULQ_S_W_MMR2 : DspMMRel, MULQ_S_W_MMR2_ENC, MULQ_S_W_DESC, ISA_DSPR2;
def PRECR_QB_PH_MMR2 : DspMMRel, PRECR_QB_PH_MMR2_ENC, PRECR_QB_PH_DESC,
                       ISA_DSPR2;
def PRECR_SRA_PH_W_MMR2 : DspMMRel, PRECR_SRA_PH_W_MMR2_ENC,
                          PRECR_SRA_PH_W_DESC, ISA_DSPR2;
def PRECR_SRA_R_PH_W_MMR2 : DspMMRel, PRECR_SRA_R_PH_W_MMR2_ENC,
                            PRECR_SRA_R_PH_W_DESC, ISA_DSPR2;
def PREPEND_MMR2 : DspMMRel, PREPEND_MMR2_ENC, PREPEND_DESC, ISA_DSPR2;

// Instruction alias.
def : MMDSPInstAlias<"wrdsp $rt", (WRDSP_MM GPR32Opnd:$rt, 0x1F), 1>;
def APPEND_MMR2 : DspMMRel, APPEND_MMR2_ENC, APPEND_DESC, ISA_DSPR2;
def MULSA_W_PH_MMR2 : DspMMRel, MULSA_W_PH_MMR2_ENC, MULSA_W_PH_DESC, ISA_DSPR2;
// microMIPS DSP Rev 3
def BPOSGE32C_MMR3 : DspMMRel, BPOSGE32C_MMR3_ENC, BPOSGE32C_MMR3_DESC,
                     ISA_DSPR3;
