//===- HexagonIntrinsicsV5.td - V5 Instruction intrinsics --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def : T_PR_pat <M2_vrcmpys_s1,     int_hexagon_M2_vrcmpys_s1>;
def : T_PPR_pat<M2_vrcmpys_acc_s1, int_hexagon_M2_vrcmpys_acc_s1>;
def : T_PR_pat <M2_vrcmpys_s1rp,   int_hexagon_M2_vrcmpys_s1rp>;

// Vector reduce add unsigned halfwords
def : T_PP_pat<M2_vradduh, int_hexagon_M2_vradduh>;

def: T_RP_pat<A2_addsp,   int_hexagon_A2_addsp>;
def: T_PP_pat<A2_addpsat, int_hexagon_A2_addpsat>;
def: T_PP_pat<A2_minp,    int_hexagon_A2_minp>;
def: T_PP_pat<A2_minup,   int_hexagon_A2_minup>;
def: T_PP_pat<A2_maxp,    int_hexagon_A2_maxp>;
def: T_PP_pat<A2_maxup,   int_hexagon_A2_maxup>;

// Vector reduce multiply word by signed half (32x16)
//Rdd=vrmpyweh(Rss,Rtt)[:<<1]
def : T_PP_pat <M4_vrmpyeh_s0, int_hexagon_M4_vrmpyeh_s0>;
def : T_PP_pat <M4_vrmpyeh_s1, int_hexagon_M4_vrmpyeh_s1>;

//Rdd=vrmpywoh(Rss,Rtt)[:<<1]
def : T_PP_pat <M4_vrmpyoh_s0, int_hexagon_M4_vrmpyoh_s0>;
def : T_PP_pat <M4_vrmpyoh_s1, int_hexagon_M4_vrmpyoh_s1>;

//Rdd+=vrmpyweh(Rss,Rtt)[:<<1]
def : T_PPP_pat <M4_vrmpyeh_acc_s0, int_hexagon_M4_vrmpyeh_acc_s0>;
def : T_PPP_pat <M4_vrmpyeh_acc_s1, int_hexagon_M4_vrmpyeh_acc_s1>;

//Rdd=vrmpywoh(Rss,Rtt)[:<<1]
def : T_PPP_pat <M4_vrmpyoh_acc_s0, int_hexagon_M4_vrmpyoh_acc_s0>;
def : T_PPP_pat <M4_vrmpyoh_acc_s1, int_hexagon_M4_vrmpyoh_acc_s1>;

// Vector multiply halfwords, signed by unsigned
// Rdd=vmpyhsu(Rs,Rt)[:<<1]:sat
def : T_RR_pat <M2_vmpy2su_s0, int_hexagon_M2_vmpy2su_s0>;
def : T_RR_pat <M2_vmpy2su_s1, int_hexagon_M2_vmpy2su_s1>;

// Rxx+=vmpyhsu(Rs,Rt)[:<<1]:sat
def : T_PRR_pat <M2_vmac2su_s0, int_hexagon_M2_vmac2su_s0>;
def : T_PRR_pat <M2_vmac2su_s1, int_hexagon_M2_vmac2su_s1>;

// Vector polynomial multiply halfwords
// Rdd=vpmpyh(Rs,Rt)
def : T_RR_pat <M4_vpmpyh, int_hexagon_M4_vpmpyh>;
// Rxx[^]=vpmpyh(Rs,Rt)
def : T_PRR_pat <M4_vpmpyh_acc, int_hexagon_M4_vpmpyh_acc>;

// Polynomial multiply words
// Rdd=pmpyw(Rs,Rt)
def : T_RR_pat <M4_pmpyw, int_hexagon_M4_pmpyw>;
// Rxx^=pmpyw(Rs,Rt)
def : T_PRR_pat <M4_pmpyw_acc, int_hexagon_M4_pmpyw_acc>;

//Rxx^=asr(Rss,Rt)
def : T_PPR_pat <S2_asr_r_p_xor, int_hexagon_S2_asr_r_p_xor>;
//Rxx^=asl(Rss,Rt)
def : T_PPR_pat <S2_asl_r_p_xor, int_hexagon_S2_asl_r_p_xor>;
//Rxx^=lsr(Rss,Rt)
def : T_PPR_pat <S2_lsr_r_p_xor, int_hexagon_S2_lsr_r_p_xor>;
//Rxx^=lsl(Rss,Rt)
def : T_PPR_pat <S2_lsl_r_p_xor, int_hexagon_S2_lsl_r_p_xor>;

// Multiply and use upper result
def : T_RR_pat <M2_mpysu_up, int_hexagon_M2_mpysu_up>;
def : T_RR_pat <M2_mpy_up_s1, int_hexagon_M2_mpy_up_s1>;
def : T_RR_pat <M2_hmmpyh_s1, int_hexagon_M2_hmmpyh_s1>;
def : T_RR_pat <M2_hmmpyl_s1, int_hexagon_M2_hmmpyl_s1>;
def : T_RR_pat <M2_mpy_up_s1_sat, int_hexagon_M2_mpy_up_s1_sat>;

def : T_PP_pat <A2_vaddub, int_hexagon_A2_vaddb_map>;
def : T_PP_pat <A2_vsubub, int_hexagon_A2_vsubb_map>;

// Vector reduce add unsigned halfwords
def : T_PP_pat <M2_vraddh, int_hexagon_M2_vraddh>;

def: T_P_pat<S2_brevp, int_hexagon_S2_brevp>;
def: T_P_pat<S2_ct0p,  int_hexagon_S2_ct0p>;
def: T_P_pat<S2_ct1p,  int_hexagon_S2_ct1p>;

def: T_Q_RR_pat<C4_nbitsset,  int_hexagon_C4_nbitsset>;
def: T_Q_RR_pat<C4_nbitsclr,  int_hexagon_C4_nbitsclr>;
def: T_Q_RI_pat<C4_nbitsclri, int_hexagon_C4_nbitsclri>;

def : T_Q_PI_pat<A4_vcmpbeqi,     int_hexagon_A4_vcmpbeqi>;
def : T_Q_PI_pat<A4_vcmpbgti,     int_hexagon_A4_vcmpbgti>;
def : T_Q_PI_pat<A4_vcmpbgtui,    int_hexagon_A4_vcmpbgtui>;
def : T_Q_PI_pat<A4_vcmpheqi,     int_hexagon_A4_vcmpheqi>;
def : T_Q_PI_pat<A4_vcmphgti,     int_hexagon_A4_vcmphgti>;
def : T_Q_PI_pat<A4_vcmphgtui,    int_hexagon_A4_vcmphgtui>;
def : T_Q_PI_pat<A4_vcmpweqi,     int_hexagon_A4_vcmpweqi>;
def : T_Q_PI_pat<A4_vcmpwgti,     int_hexagon_A4_vcmpwgti>;
def : T_Q_PI_pat<A4_vcmpwgtui,    int_hexagon_A4_vcmpwgtui>;
def : T_Q_PP_pat<A4_vcmpbeq_any,  int_hexagon_A4_vcmpbeq_any>;

def : T_Q_RR_pat<A4_cmpbeq,   int_hexagon_A4_cmpbeq>;
def : T_Q_RR_pat<A4_cmpbgt,   int_hexagon_A4_cmpbgt>;
def : T_Q_RR_pat<A4_cmpbgtu,  int_hexagon_A4_cmpbgtu>;
def : T_Q_RR_pat<A4_cmpheq,   int_hexagon_A4_cmpheq>;
def : T_Q_RR_pat<A4_cmphgt,   int_hexagon_A4_cmphgt>;
def : T_Q_RR_pat<A4_cmphgtu,  int_hexagon_A4_cmphgtu>;

def : T_Q_RI_pat<A4_cmpbeqi,  int_hexagon_A4_cmpbeqi>;
def : T_Q_RI_pat<A4_cmpbgti,  int_hexagon_A4_cmpbgti>;
def : T_Q_RI_pat<A4_cmpbgtui, int_hexagon_A4_cmpbgtui>;

def : T_Q_RI_pat<A4_cmpheqi,  int_hexagon_A4_cmpheqi>;
def : T_Q_RI_pat<A4_cmphgti,  int_hexagon_A4_cmphgti>;
def : T_Q_RI_pat<A4_cmphgtui, int_hexagon_A4_cmphgtui>;

def : T_Q_RP_pat<A4_boundscheck, int_hexagon_A4_boundscheck>;
def : T_Q_PR_pat<A4_tlbmatch,    int_hexagon_A4_tlbmatch>;

def : T_RRR_pat <M4_mpyrr_addr,    int_hexagon_M4_mpyrr_addr>;
def : T_IRR_pat <M4_mpyrr_addi,    int_hexagon_M4_mpyrr_addi>;
def : T_IRI_pat <M4_mpyri_addi,    int_hexagon_M4_mpyri_addi>;
def : T_RIR_pat <M4_mpyri_addr_u2, int_hexagon_M4_mpyri_addr_u2>;
def : T_RRI_pat <M4_mpyri_addr,    int_hexagon_M4_mpyri_addr>;
def : T_RRR_pat <M4_mac_up_s1_sat, int_hexagon_M4_mac_up_s1_sat>;
def : T_RRR_pat <M4_nac_up_s1_sat, int_hexagon_M4_nac_up_s1_sat>;

// Complex multiply 32x16
def : T_PR_pat <M4_cmpyi_wh, int_hexagon_M4_cmpyi_wh>;
def : T_PR_pat <M4_cmpyr_wh, int_hexagon_M4_cmpyr_wh>;

def : T_PR_pat <M4_cmpyi_whc, int_hexagon_M4_cmpyi_whc>;
def : T_PR_pat <M4_cmpyr_whc, int_hexagon_M4_cmpyr_whc>;

def : T_PP_pat<A4_andnp, int_hexagon_A4_andnp>;
def : T_PP_pat<A4_ornp,  int_hexagon_A4_ornp>;

// Complex add/sub halfwords/words
def : T_PP_pat <S4_vxaddsubw, int_hexagon_S4_vxaddsubw>;
def : T_PP_pat <S4_vxsubaddw, int_hexagon_S4_vxsubaddw>;
def : T_PP_pat <S4_vxaddsubh, int_hexagon_S4_vxaddsubh>;
def : T_PP_pat <S4_vxsubaddh, int_hexagon_S4_vxsubaddh>;

def : T_PP_pat <S4_vxaddsubhr, int_hexagon_S4_vxaddsubhr>;
def : T_PP_pat <S4_vxsubaddhr, int_hexagon_S4_vxsubaddhr>;

// Extract bitfield
def : T_PP_pat  <S4_extractp_rp, int_hexagon_S4_extractp_rp>;
def : T_RP_pat  <S4_extract_rp, int_hexagon_S4_extract_rp>;
def : T_PII_pat <S4_extractp, int_hexagon_S4_extractp>;
def : T_RII_pat <S4_extract, int_hexagon_S4_extract>;

// Vector conditional negate
// Rdd=vcnegh(Rss,Rt)
def : T_PR_pat <S2_vcnegh, int_hexagon_S2_vcnegh>;

// Shift an immediate left by register amount
def : T_IR_pat<S4_lsli, int_hexagon_S4_lsli>;

// Vector reduce maximum halfwords
def : T_PPR_pat <A4_vrmaxh, int_hexagon_A4_vrmaxh>;
def : T_PPR_pat <A4_vrmaxuh, int_hexagon_A4_vrmaxuh>;

// Vector reduce maximum words
def : T_PPR_pat <A4_vrmaxw, int_hexagon_A4_vrmaxw>;
def : T_PPR_pat <A4_vrmaxuw, int_hexagon_A4_vrmaxuw>;

// Vector reduce minimum halfwords
def : T_PPR_pat <A4_vrminh, int_hexagon_A4_vrminh>;
def : T_PPR_pat <A4_vrminuh, int_hexagon_A4_vrminuh>;

// Vector reduce minimum words
def : T_PPR_pat <A4_vrminw, int_hexagon_A4_vrminw>;
def : T_PPR_pat <A4_vrminuw, int_hexagon_A4_vrminuw>;

// Rotate and reduce bytes
def : Pat <(int_hexagon_S4_vrcrotate DoubleRegs:$src1, IntRegs:$src2,
                                     u2_0ImmPred:$src3),
           (S4_vrcrotate DoubleRegs:$src1, IntRegs:$src2, u2_0ImmPred:$src3)>;

// Rotate and reduce bytes with accumulation
// Rxx+=vrcrotate(Rss,Rt,#u2)
def : Pat <(int_hexagon_S4_vrcrotate_acc DoubleRegs:$src1, DoubleRegs:$src2,
                                         IntRegs:$src3, u2_0ImmPred:$src4),
           (S4_vrcrotate_acc DoubleRegs:$src1, DoubleRegs:$src2,
                             IntRegs:$src3, u2_0ImmPred:$src4)>;

// Vector conditional negate
def : T_PPR_pat<S2_vrcnegh, int_hexagon_S2_vrcnegh>;

// Logical xor with xor accumulation
def : T_PPP_pat<M4_xor_xacc, int_hexagon_M4_xor_xacc>;

// ALU64 - Vector min/max byte
def : T_PP_pat <A2_vminb, int_hexagon_A2_vminb>;
def : T_PP_pat <A2_vmaxb, int_hexagon_A2_vmaxb>;

// Shift and add/sub/and/or
def : T_IRI_pat <S4_andi_asl_ri, int_hexagon_S4_andi_asl_ri>;
def : T_IRI_pat <S4_ori_asl_ri,  int_hexagon_S4_ori_asl_ri>;
def : T_IRI_pat <S4_addi_asl_ri, int_hexagon_S4_addi_asl_ri>;
def : T_IRI_pat <S4_subi_asl_ri, int_hexagon_S4_subi_asl_ri>;
def : T_IRI_pat <S4_andi_lsr_ri, int_hexagon_S4_andi_lsr_ri>;
def : T_IRI_pat <S4_ori_lsr_ri,  int_hexagon_S4_ori_lsr_ri>;
def : T_IRI_pat <S4_addi_lsr_ri, int_hexagon_S4_addi_lsr_ri>;
def : T_IRI_pat <S4_subi_lsr_ri, int_hexagon_S4_subi_lsr_ri>;

// Split bitfield
def : T_RI_pat <A4_bitspliti, int_hexagon_A4_bitspliti>;
def : T_RR_pat <A4_bitsplit,  int_hexagon_A4_bitsplit>;

def: T_RR_pat<S4_parity,      int_hexagon_S4_parity>;

def: T_Q_RI_pat<S4_ntstbit_i, int_hexagon_S4_ntstbit_i>;
def: T_Q_RR_pat<S4_ntstbit_r, int_hexagon_S4_ntstbit_r>;

def: T_RI_pat<S4_clbaddi,     int_hexagon_S4_clbaddi>;
def: T_PI_pat<S4_clbpaddi,    int_hexagon_S4_clbpaddi>;
def: T_P_pat <S4_clbpnorm,    int_hexagon_S4_clbpnorm>;

//*******************************************************************
//            ALU32/ALU
//*******************************************************************

// ALU32 / ALU / Logical Operations.
def: T_RR_pat<A4_andn, int_hexagon_A4_andn>;
def: T_RR_pat<A4_orn,  int_hexagon_A4_orn>;

//*******************************************************************
//            ALU32/PERM
//*******************************************************************

// Combine Words Into Doublewords.
def: T_RI_pat<A4_combineri, int_hexagon_A4_combineri, s32_0ImmPred>;
def: T_IR_pat<A4_combineir, int_hexagon_A4_combineir, s32_0ImmPred>;

//*******************************************************************
//           ALU32/PRED
//*******************************************************************

// Compare
def : T_Q_RI_pat<C4_cmpneqi, int_hexagon_C4_cmpneqi, s32_0ImmPred>;
def : T_Q_RI_pat<C4_cmpltei, int_hexagon_C4_cmpltei, s32_0ImmPred>;
def : T_Q_RI_pat<C4_cmplteui, int_hexagon_C4_cmplteui, u32_0ImmPred>;

// Compare To General Register.
def: T_Q_RR_pat<C4_cmpneq,  int_hexagon_C4_cmpneq>;
def: T_Q_RR_pat<C4_cmplte,  int_hexagon_C4_cmplte>;
def: T_Q_RR_pat<C4_cmplteu, int_hexagon_C4_cmplteu>;

def: T_RR_pat<A4_rcmpeq,  int_hexagon_A4_rcmpeq>;
def: T_RR_pat<A4_rcmpneq, int_hexagon_A4_rcmpneq>;

def: T_RI_pat<A4_rcmpeqi,  int_hexagon_A4_rcmpeqi>;
def: T_RI_pat<A4_rcmpneqi, int_hexagon_A4_rcmpneqi>;

//*******************************************************************
//           CR
//*******************************************************************

// CR / Logical Operations On Predicates.
def: T_Q_QQQ_pat<C4_and_and,  int_hexagon_C4_and_and>;
def: T_Q_QQQ_pat<C4_and_andn, int_hexagon_C4_and_andn>;
def: T_Q_QQQ_pat<C4_and_or,   int_hexagon_C4_and_or>;
def: T_Q_QQQ_pat<C4_and_orn,  int_hexagon_C4_and_orn>;
def: T_Q_QQQ_pat<C4_or_and,   int_hexagon_C4_or_and>;
def: T_Q_QQQ_pat<C4_or_andn,  int_hexagon_C4_or_andn>;
def: T_Q_QQQ_pat<C4_or_or,    int_hexagon_C4_or_or>;
def: T_Q_QQQ_pat<C4_or_orn,   int_hexagon_C4_or_orn>;

//*******************************************************************
//           XTYPE/ALU
//*******************************************************************

// Add And Accumulate.

def : T_RRI_pat <S4_addaddi, int_hexagon_S4_addaddi>;
def : T_RIR_pat <S4_subaddi, int_hexagon_S4_subaddi>;


// XTYPE / ALU / Logical-logical Words.
def : T_RRR_pat <M4_or_xor,   int_hexagon_M4_or_xor>;
def : T_RRR_pat <M4_and_xor,  int_hexagon_M4_and_xor>;
def : T_RRR_pat <M4_or_and,   int_hexagon_M4_or_and>;
def : T_RRR_pat <M4_and_and,  int_hexagon_M4_and_and>;
def : T_RRR_pat <M4_xor_and,  int_hexagon_M4_xor_and>;
def : T_RRR_pat <M4_or_or,    int_hexagon_M4_or_or>;
def : T_RRR_pat <M4_and_or,   int_hexagon_M4_and_or>;
def : T_RRR_pat <M4_xor_or,   int_hexagon_M4_xor_or>;
def : T_RRR_pat <M4_or_andn,  int_hexagon_M4_or_andn>;
def : T_RRR_pat <M4_and_andn, int_hexagon_M4_and_andn>;
def : T_RRR_pat <M4_xor_andn, int_hexagon_M4_xor_andn>;

def : T_RRI_pat <S4_or_andi, int_hexagon_S4_or_andi>;
def : T_RRI_pat <S4_or_andix,  int_hexagon_S4_or_andix>;
def : T_RRI_pat <S4_or_ori, int_hexagon_S4_or_ori>;

// Modulo wrap.
def : T_RR_pat <A4_modwrapu, int_hexagon_A4_modwrapu>;

// Arithmetic/Convergent round
// Rd=[cround|round](Rs,Rt)[:sat]
// Rd=[cround|round](Rs,#u5)[:sat]
def : T_RI_pat <A4_cround_ri, int_hexagon_A4_cround_ri>;
def : T_RR_pat <A4_cround_rr, int_hexagon_A4_cround_rr>;

def : T_RI_pat <A4_round_ri, int_hexagon_A4_round_ri>;
def : T_RR_pat <A4_round_rr, int_hexagon_A4_round_rr>;

def : T_RI_pat <A4_round_ri_sat, int_hexagon_A4_round_ri_sat>;
def : T_RR_pat <A4_round_rr_sat, int_hexagon_A4_round_rr_sat>;

def : T_P_pat <A2_roundsat, int_hexagon_A2_roundsat>;

//Rdd[+]=vrmpybsu(Rss,Rtt)
//Rdd[+]=vrmpybuu(Rss,Rtt)
def : T_PP_pat  <M5_vrmpybsu, int_hexagon_M5_vrmpybsu>;
def : T_PP_pat  <M5_vrmpybuu, int_hexagon_M5_vrmpybuu>;

def : T_PP_pat <M5_vdmpybsu, int_hexagon_M5_vdmpybsu>;

def : T_PPP_pat <M5_vrmacbsu, int_hexagon_M5_vrmacbsu>;
def : T_PPP_pat <M5_vrmacbuu, int_hexagon_M5_vrmacbuu>;
//Rxx+=vdmpybsu(Rss,Rtt):sat
def : T_PPP_pat <M5_vdmacbsu, int_hexagon_M5_vdmacbsu>;

// Vector multiply bytes
// Rdd=vmpyb[s]u(Rs,Rt)
def : T_RR_pat <M5_vmpybsu, int_hexagon_M5_vmpybsu>;
def : T_RR_pat <M5_vmpybuu, int_hexagon_M5_vmpybuu>;

// Rxx+=vmpyb[s]u(Rs,Rt)
def : T_PRR_pat <M5_vmacbsu, int_hexagon_M5_vmacbsu>;
def : T_PRR_pat <M5_vmacbuu, int_hexagon_M5_vmacbuu>;

// Rd=vaddhub(Rss,Rtt):sat
def : T_PP_pat <A5_vaddhubs, int_hexagon_A5_vaddhubs>;

def : T_FF_pat<F2_sfadd, int_hexagon_F2_sfadd>;
def : T_FF_pat<F2_sfsub, int_hexagon_F2_sfsub>;
def : T_FF_pat<F2_sfmpy, int_hexagon_F2_sfmpy>;
def : T_FF_pat<F2_sfmax, int_hexagon_F2_sfmax>;
def : T_FF_pat<F2_sfmin, int_hexagon_F2_sfmin>;

def : T_FF_pat<F2_sffixupn, int_hexagon_F2_sffixupn>;
def : T_FF_pat<F2_sffixupd, int_hexagon_F2_sffixupd>;
def : T_F_pat <F2_sffixupr, int_hexagon_F2_sffixupr>;

def : T_Q_QQ_pat<C4_fastcorner9,     int_hexagon_C4_fastcorner9>;
def : T_Q_QQ_pat<C4_fastcorner9_not, int_hexagon_C4_fastcorner9_not>;

def : T_P_pat <S5_popcountp, int_hexagon_S5_popcountp>;
def : T_PI_pat <S5_asrhub_sat, int_hexagon_S5_asrhub_sat>;

def : T_PI_pat <S2_asr_i_p_rnd, int_hexagon_S2_asr_i_p_rnd>;
def : T_PI_pat <S2_asr_i_p_rnd_goodsyntax,
                int_hexagon_S2_asr_i_p_rnd_goodsyntax>;

def : T_PI_pat <S5_asrhub_rnd_sat_goodsyntax,
                int_hexagon_S5_asrhub_rnd_sat_goodsyntax>;

def : T_PI_pat <S5_vasrhrnd_goodsyntax, int_hexagon_S5_vasrhrnd_goodsyntax>;

def : T_FFF_pat <F2_sffma, int_hexagon_F2_sffma>;
def : T_FFF_pat <F2_sffms, int_hexagon_F2_sffms>;
def : T_FFF_pat <F2_sffma_lib, int_hexagon_F2_sffma_lib>;
def : T_FFF_pat <F2_sffms_lib, int_hexagon_F2_sffms_lib>;
def : T_FFFQ_pat <F2_sffma_sc, int_hexagon_F2_sffma_sc>;

// Compare floating-point value
def : T_Q_FF_pat <F2_sfcmpge, int_hexagon_F2_sfcmpge>;
def : T_Q_FF_pat <F2_sfcmpuo, int_hexagon_F2_sfcmpuo>;
def : T_Q_FF_pat <F2_sfcmpeq, int_hexagon_F2_sfcmpeq>;
def : T_Q_FF_pat <F2_sfcmpgt, int_hexagon_F2_sfcmpgt>;

def : T_Q_DD_pat <F2_dfcmpeq, int_hexagon_F2_dfcmpeq>;
def : T_Q_DD_pat <F2_dfcmpgt, int_hexagon_F2_dfcmpgt>;
def : T_Q_DD_pat <F2_dfcmpge, int_hexagon_F2_dfcmpge>;
def : T_Q_DD_pat <F2_dfcmpuo, int_hexagon_F2_dfcmpuo>;

// Create floating-point value
def : T_I_pat <F2_sfimm_p, int_hexagon_F2_sfimm_p>;
def : T_I_pat <F2_sfimm_n, int_hexagon_F2_sfimm_n>;
def : T_I_pat <F2_dfimm_p, int_hexagon_F2_dfimm_p>;
def : T_I_pat <F2_dfimm_n, int_hexagon_F2_dfimm_n>;

def : T_Q_DI_pat <F2_dfclass, int_hexagon_F2_dfclass>;
def : T_Q_FI_pat <F2_sfclass, int_hexagon_F2_sfclass>;
def : T_F_pat <F2_conv_sf2df, int_hexagon_F2_conv_sf2df>;
def : T_D_pat <F2_conv_df2sf, int_hexagon_F2_conv_df2sf>;
def : T_R_pat <F2_conv_uw2sf, int_hexagon_F2_conv_uw2sf>;
def : T_R_pat <F2_conv_uw2df, int_hexagon_F2_conv_uw2df>;
def : T_R_pat <F2_conv_w2sf,  int_hexagon_F2_conv_w2sf>;
def : T_R_pat <F2_conv_w2df,  int_hexagon_F2_conv_w2df>;
def : T_P_pat <F2_conv_ud2sf, int_hexagon_F2_conv_ud2sf>;
def : T_P_pat <F2_conv_ud2df, int_hexagon_F2_conv_ud2df>;
def : T_P_pat <F2_conv_d2sf,  int_hexagon_F2_conv_d2sf>;
def : T_P_pat <F2_conv_d2df,  int_hexagon_F2_conv_d2df>;
def : T_F_pat <F2_conv_sf2uw, int_hexagon_F2_conv_sf2uw>;
def : T_F_pat <F2_conv_sf2w,  int_hexagon_F2_conv_sf2w>;
def : T_F_pat <F2_conv_sf2ud, int_hexagon_F2_conv_sf2ud>;
def : T_F_pat <F2_conv_sf2d,  int_hexagon_F2_conv_sf2d>;
def : T_D_pat <F2_conv_df2uw, int_hexagon_F2_conv_df2uw>;
def : T_D_pat <F2_conv_df2w,  int_hexagon_F2_conv_df2w>;
def : T_D_pat <F2_conv_df2ud, int_hexagon_F2_conv_df2ud>;
def : T_D_pat <F2_conv_df2d,  int_hexagon_F2_conv_df2d>;
def : T_F_pat <F2_conv_sf2uw_chop, int_hexagon_F2_conv_sf2uw_chop>;
def : T_F_pat <F2_conv_sf2w_chop,  int_hexagon_F2_conv_sf2w_chop>;
def : T_F_pat <F2_conv_sf2ud_chop, int_hexagon_F2_conv_sf2ud_chop>;
def : T_F_pat <F2_conv_sf2d_chop,  int_hexagon_F2_conv_sf2d_chop>;
def : T_D_pat <F2_conv_df2uw_chop, int_hexagon_F2_conv_df2uw_chop>;
def : T_D_pat <F2_conv_df2w_chop,  int_hexagon_F2_conv_df2w_chop>;
def : T_D_pat <F2_conv_df2ud_chop, int_hexagon_F2_conv_df2ud_chop>;
def : T_D_pat <F2_conv_df2d_chop,  int_hexagon_F2_conv_df2d_chop>;
