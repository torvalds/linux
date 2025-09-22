//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//===----------------------------------------------------------------------===//



#ifndef __HEXAGON_PROTOS_H_
#define __HEXAGON_PROTOS_H_ 1

/* ==========================================================================
   Assembly Syntax:       Rd32=abs(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_abs_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_abs_R __builtin_HEXAGON_A2_abs

/* ==========================================================================
   Assembly Syntax:       Rdd32=abs(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_abs_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_abs_P __builtin_HEXAGON_A2_absp

/* ==========================================================================
   Assembly Syntax:       Rd32=abs(Rs32):sat
   C Intrinsic Prototype: Word32 Q6_R_abs_R_sat(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_abs_R_sat __builtin_HEXAGON_A2_abssat

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_add_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_add_RR __builtin_HEXAGON_A2_add

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.h,Rs32.h):<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RhRh_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RhRh_s16 __builtin_HEXAGON_A2_addh_h16_hh

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.h,Rs32.l):<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RhRl_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RhRl_s16 __builtin_HEXAGON_A2_addh_h16_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.h):<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RlRh_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRh_s16 __builtin_HEXAGON_A2_addh_h16_lh

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.l):<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RlRl_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRl_s16 __builtin_HEXAGON_A2_addh_h16_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.h,Rs32.h):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RhRh_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RhRh_sat_s16 __builtin_HEXAGON_A2_addh_h16_sat_hh

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.h,Rs32.l):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RhRl_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RhRl_sat_s16 __builtin_HEXAGON_A2_addh_h16_sat_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.h):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RlRh_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRh_sat_s16 __builtin_HEXAGON_A2_addh_h16_sat_lh

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.l):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_add_RlRl_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRl_sat_s16 __builtin_HEXAGON_A2_addh_h16_sat_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.h)
   C Intrinsic Prototype: Word32 Q6_R_add_RlRh(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRh __builtin_HEXAGON_A2_addh_l16_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.l)
   C Intrinsic Prototype: Word32 Q6_R_add_RlRl(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRl __builtin_HEXAGON_A2_addh_l16_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_add_RlRh_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRh_sat __builtin_HEXAGON_A2_addh_l16_sat_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rt32.l,Rs32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_add_RlRl_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_RlRl_sat __builtin_HEXAGON_A2_addh_l16_sat_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rs32,#s16)
   C Intrinsic Prototype: Word32 Q6_R_add_RI(Word32 Rs, Word32 Is16)
   Instruction Type:      ALU32_ADDI
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_add_RI __builtin_HEXAGON_A2_addi

/* ==========================================================================
   Assembly Syntax:       Rdd32=add(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_add_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_add_PP __builtin_HEXAGON_A2_addp

/* ==========================================================================
   Assembly Syntax:       Rdd32=add(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_add_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_add_PP_sat __builtin_HEXAGON_A2_addpsat

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_add_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_add_RR_sat __builtin_HEXAGON_A2_addsat

/* ==========================================================================
   Assembly Syntax:       Rdd32=add(Rs32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_add_RP(Word32 Rs, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_add_RP __builtin_HEXAGON_A2_addsp

/* ==========================================================================
   Assembly Syntax:       Rd32=and(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_and_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_and_RR __builtin_HEXAGON_A2_and

/* ==========================================================================
   Assembly Syntax:       Rd32=and(Rs32,#s10)
   C Intrinsic Prototype: Word32 Q6_R_and_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_and_RI __builtin_HEXAGON_A2_andir

/* ==========================================================================
   Assembly Syntax:       Rdd32=and(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_and_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_and_PP __builtin_HEXAGON_A2_andp

/* ==========================================================================
   Assembly Syntax:       Rd32=aslh(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_aslh_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_aslh_R __builtin_HEXAGON_A2_aslh

/* ==========================================================================
   Assembly Syntax:       Rd32=asrh(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_asrh_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_asrh_R __builtin_HEXAGON_A2_asrh

/* ==========================================================================
   Assembly Syntax:       Rd32=combine(Rt32.h,Rs32.h)
   C Intrinsic Prototype: Word32 Q6_R_combine_RhRh(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_combine_RhRh __builtin_HEXAGON_A2_combine_hh

/* ==========================================================================
   Assembly Syntax:       Rd32=combine(Rt32.h,Rs32.l)
   C Intrinsic Prototype: Word32 Q6_R_combine_RhRl(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_combine_RhRl __builtin_HEXAGON_A2_combine_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=combine(Rt32.l,Rs32.h)
   C Intrinsic Prototype: Word32 Q6_R_combine_RlRh(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_combine_RlRh __builtin_HEXAGON_A2_combine_lh

/* ==========================================================================
   Assembly Syntax:       Rd32=combine(Rt32.l,Rs32.l)
   C Intrinsic Prototype: Word32 Q6_R_combine_RlRl(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_combine_RlRl __builtin_HEXAGON_A2_combine_ll

/* ==========================================================================
   Assembly Syntax:       Rdd32=combine(#s8,#S8)
   C Intrinsic Prototype: Word64 Q6_P_combine_II(Word32 Is8, Word32 IS8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_combine_II __builtin_HEXAGON_A2_combineii

/* ==========================================================================
   Assembly Syntax:       Rdd32=combine(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_combine_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_combine_RR __builtin_HEXAGON_A2_combinew

/* ==========================================================================
   Assembly Syntax:       Rd32=max(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_max_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_max_RR __builtin_HEXAGON_A2_max

/* ==========================================================================
   Assembly Syntax:       Rdd32=max(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_max_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_max_PP __builtin_HEXAGON_A2_maxp

/* ==========================================================================
   Assembly Syntax:       Rd32=maxu(Rs32,Rt32)
   C Intrinsic Prototype: UWord32 Q6_R_maxu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_maxu_RR __builtin_HEXAGON_A2_maxu

/* ==========================================================================
   Assembly Syntax:       Rdd32=maxu(Rss32,Rtt32)
   C Intrinsic Prototype: UWord64 Q6_P_maxu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_maxu_PP __builtin_HEXAGON_A2_maxup

/* ==========================================================================
   Assembly Syntax:       Rd32=min(Rt32,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_min_RR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_min_RR __builtin_HEXAGON_A2_min

/* ==========================================================================
   Assembly Syntax:       Rdd32=min(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_min_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_min_PP __builtin_HEXAGON_A2_minp

/* ==========================================================================
   Assembly Syntax:       Rd32=minu(Rt32,Rs32)
   C Intrinsic Prototype: UWord32 Q6_R_minu_RR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_minu_RR __builtin_HEXAGON_A2_minu

/* ==========================================================================
   Assembly Syntax:       Rdd32=minu(Rtt32,Rss32)
   C Intrinsic Prototype: UWord64 Q6_P_minu_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_minu_PP __builtin_HEXAGON_A2_minup

/* ==========================================================================
   Assembly Syntax:       Rd32=neg(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_neg_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_neg_R __builtin_HEXAGON_A2_neg

/* ==========================================================================
   Assembly Syntax:       Rdd32=neg(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_neg_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_neg_P __builtin_HEXAGON_A2_negp

/* ==========================================================================
   Assembly Syntax:       Rd32=neg(Rs32):sat
   C Intrinsic Prototype: Word32 Q6_R_neg_R_sat(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_neg_R_sat __builtin_HEXAGON_A2_negsat

/* ==========================================================================
   Assembly Syntax:       Rd32=not(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_not_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_not_R __builtin_HEXAGON_A2_not

/* ==========================================================================
   Assembly Syntax:       Rdd32=not(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_not_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_not_P __builtin_HEXAGON_A2_notp

/* ==========================================================================
   Assembly Syntax:       Rd32=or(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_or_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_or_RR __builtin_HEXAGON_A2_or

/* ==========================================================================
   Assembly Syntax:       Rd32=or(Rs32,#s10)
   C Intrinsic Prototype: Word32 Q6_R_or_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_or_RI __builtin_HEXAGON_A2_orir

/* ==========================================================================
   Assembly Syntax:       Rdd32=or(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_or_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_or_PP __builtin_HEXAGON_A2_orp

/* ==========================================================================
   Assembly Syntax:       Rd32=round(Rss32):sat
   C Intrinsic Prototype: Word32 Q6_R_round_P_sat(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_round_P_sat __builtin_HEXAGON_A2_roundsat

/* ==========================================================================
   Assembly Syntax:       Rd32=sat(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_sat_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sat_P __builtin_HEXAGON_A2_sat

/* ==========================================================================
   Assembly Syntax:       Rd32=satb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_satb_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_satb_R __builtin_HEXAGON_A2_satb

/* ==========================================================================
   Assembly Syntax:       Rd32=sath(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_sath_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sath_R __builtin_HEXAGON_A2_sath

/* ==========================================================================
   Assembly Syntax:       Rd32=satub(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_satub_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_satub_R __builtin_HEXAGON_A2_satub

/* ==========================================================================
   Assembly Syntax:       Rd32=satuh(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_satuh_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_satuh_R __builtin_HEXAGON_A2_satuh

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_sub_RR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_sub_RR __builtin_HEXAGON_A2_sub

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.h,Rs32.h):<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RhRh_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RhRh_s16 __builtin_HEXAGON_A2_subh_h16_hh

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.h,Rs32.l):<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RhRl_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RhRl_s16 __builtin_HEXAGON_A2_subh_h16_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.h):<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRh_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRh_s16 __builtin_HEXAGON_A2_subh_h16_lh

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.l):<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRl_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRl_s16 __builtin_HEXAGON_A2_subh_h16_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.h,Rs32.h):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RhRh_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RhRh_sat_s16 __builtin_HEXAGON_A2_subh_h16_sat_hh

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.h,Rs32.l):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RhRl_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RhRl_sat_s16 __builtin_HEXAGON_A2_subh_h16_sat_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.h):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRh_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRh_sat_s16 __builtin_HEXAGON_A2_subh_h16_sat_lh

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.l):sat:<<16
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRl_sat_s16(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRl_sat_s16 __builtin_HEXAGON_A2_subh_h16_sat_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.h)
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRh(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRh __builtin_HEXAGON_A2_subh_l16_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.l)
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRl(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRl __builtin_HEXAGON_A2_subh_l16_ll

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRh_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRh_sat __builtin_HEXAGON_A2_subh_l16_sat_hl

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32.l,Rs32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_sub_RlRl_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_RlRl_sat __builtin_HEXAGON_A2_subh_l16_sat_ll

/* ==========================================================================
   Assembly Syntax:       Rdd32=sub(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_sub_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_sub_PP __builtin_HEXAGON_A2_subp

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(#s10,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_sub_IR(Word32 Is10, Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_sub_IR __builtin_HEXAGON_A2_subri

/* ==========================================================================
   Assembly Syntax:       Rd32=sub(Rt32,Rs32):sat
   C Intrinsic Prototype: Word32 Q6_R_sub_RR_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_sub_RR_sat __builtin_HEXAGON_A2_subsat

/* ==========================================================================
   Assembly Syntax:       Rd32=vaddh(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_vaddh_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vaddh_RR __builtin_HEXAGON_A2_svaddh

/* ==========================================================================
   Assembly Syntax:       Rd32=vaddh(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_vaddh_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vaddh_RR_sat __builtin_HEXAGON_A2_svaddhs

/* ==========================================================================
   Assembly Syntax:       Rd32=vadduh(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_vadduh_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vadduh_RR_sat __builtin_HEXAGON_A2_svadduhs

/* ==========================================================================
   Assembly Syntax:       Rd32=vavgh(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_vavgh_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vavgh_RR __builtin_HEXAGON_A2_svavgh

/* ==========================================================================
   Assembly Syntax:       Rd32=vavgh(Rs32,Rt32):rnd
   C Intrinsic Prototype: Word32 Q6_R_vavgh_RR_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vavgh_RR_rnd __builtin_HEXAGON_A2_svavghs

/* ==========================================================================
   Assembly Syntax:       Rd32=vnavgh(Rt32,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_vnavgh_RR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vnavgh_RR __builtin_HEXAGON_A2_svnavgh

/* ==========================================================================
   Assembly Syntax:       Rd32=vsubh(Rt32,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_vsubh_RR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vsubh_RR __builtin_HEXAGON_A2_svsubh

/* ==========================================================================
   Assembly Syntax:       Rd32=vsubh(Rt32,Rs32):sat
   C Intrinsic Prototype: Word32 Q6_R_vsubh_RR_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vsubh_RR_sat __builtin_HEXAGON_A2_svsubhs

/* ==========================================================================
   Assembly Syntax:       Rd32=vsubuh(Rt32,Rs32):sat
   C Intrinsic Prototype: Word32 Q6_R_vsubuh_RR_sat(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vsubuh_RR_sat __builtin_HEXAGON_A2_svsubuhs

/* ==========================================================================
   Assembly Syntax:       Rd32=swiz(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_swiz_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_swiz_R __builtin_HEXAGON_A2_swiz

/* ==========================================================================
   Assembly Syntax:       Rd32=sxtb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_sxtb_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_sxtb_R __builtin_HEXAGON_A2_sxtb

/* ==========================================================================
   Assembly Syntax:       Rd32=sxth(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_sxth_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_sxth_R __builtin_HEXAGON_A2_sxth

/* ==========================================================================
   Assembly Syntax:       Rdd32=sxtw(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_sxtw_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_sxtw_R __builtin_HEXAGON_A2_sxtw

/* ==========================================================================
   Assembly Syntax:       Rd32=Rs32
   C Intrinsic Prototype: Word32 Q6_R_equals_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_equals_R __builtin_HEXAGON_A2_tfr

/* ==========================================================================
   Assembly Syntax:       Rx32.h=#u16
   C Intrinsic Prototype: Word32 Q6_Rh_equals_I(Word32 Rx, Word32 Iu16)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_Rh_equals_I __builtin_HEXAGON_A2_tfrih

/* ==========================================================================
   Assembly Syntax:       Rx32.l=#u16
   C Intrinsic Prototype: Word32 Q6_Rl_equals_I(Word32 Rx, Word32 Iu16)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_Rl_equals_I __builtin_HEXAGON_A2_tfril

/* ==========================================================================
   Assembly Syntax:       Rdd32=Rss32
   C Intrinsic Prototype: Word64 Q6_P_equals_P(Word64 Rss)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_equals_P __builtin_HEXAGON_A2_tfrp

/* ==========================================================================
   Assembly Syntax:       Rdd32=#s8
   C Intrinsic Prototype: Word64 Q6_P_equals_I(Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_equals_I __builtin_HEXAGON_A2_tfrpi

/* ==========================================================================
   Assembly Syntax:       Rd32=#s16
   C Intrinsic Prototype: Word32 Q6_R_equals_I(Word32 Is16)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_equals_I __builtin_HEXAGON_A2_tfrsi

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsh(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsh_P __builtin_HEXAGON_A2_vabsh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsh(Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vabsh_P_sat(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsh_P_sat __builtin_HEXAGON_A2_vabshsat

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsw(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsw_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsw_P __builtin_HEXAGON_A2_vabsw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsw(Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vabsw_P_sat(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsw_P_sat __builtin_HEXAGON_A2_vabswsat

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddb(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vaddb_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      MAPPING
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_vaddb_PP __builtin_HEXAGON_A2_vaddb_map

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vaddh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddh_PP __builtin_HEXAGON_A2_vaddh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vaddh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddh_PP_sat __builtin_HEXAGON_A2_vaddhs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vaddub_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddub_PP __builtin_HEXAGON_A2_vaddub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddub(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vaddub_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddub_PP_sat __builtin_HEXAGON_A2_vaddubs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vadduh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vadduh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vadduh_PP_sat __builtin_HEXAGON_A2_vadduhs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vaddw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddw_PP __builtin_HEXAGON_A2_vaddw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaddw(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vaddw_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaddw_PP_sat __builtin_HEXAGON_A2_vaddws

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vavgh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgh_PP __builtin_HEXAGON_A2_vavgh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgh(Rss32,Rtt32):crnd
   C Intrinsic Prototype: Word64 Q6_P_vavgh_PP_crnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgh_PP_crnd __builtin_HEXAGON_A2_vavghcr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgh(Rss32,Rtt32):rnd
   C Intrinsic Prototype: Word64 Q6_P_vavgh_PP_rnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgh_PP_rnd __builtin_HEXAGON_A2_vavghr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vavgub_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgub_PP __builtin_HEXAGON_A2_vavgub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgub(Rss32,Rtt32):rnd
   C Intrinsic Prototype: Word64 Q6_P_vavgub_PP_rnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgub_PP_rnd __builtin_HEXAGON_A2_vavgubr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavguh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vavguh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavguh_PP __builtin_HEXAGON_A2_vavguh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavguh(Rss32,Rtt32):rnd
   C Intrinsic Prototype: Word64 Q6_P_vavguh_PP_rnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavguh_PP_rnd __builtin_HEXAGON_A2_vavguhr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavguw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vavguw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavguw_PP __builtin_HEXAGON_A2_vavguw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavguw(Rss32,Rtt32):rnd
   C Intrinsic Prototype: Word64 Q6_P_vavguw_PP_rnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavguw_PP_rnd __builtin_HEXAGON_A2_vavguwr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vavgw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgw_PP __builtin_HEXAGON_A2_vavgw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgw(Rss32,Rtt32):crnd
   C Intrinsic Prototype: Word64 Q6_P_vavgw_PP_crnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgw_PP_crnd __builtin_HEXAGON_A2_vavgwcr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vavgw(Rss32,Rtt32):rnd
   C Intrinsic Prototype: Word64 Q6_P_vavgw_PP_rnd(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vavgw_PP_rnd __builtin_HEXAGON_A2_vavgwr

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.eq(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_eq_PP __builtin_HEXAGON_A2_vcmpbeq

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.gtu(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_gtu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_gtu_PP __builtin_HEXAGON_A2_vcmpbgtu

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.eq(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmph_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_eq_PP __builtin_HEXAGON_A2_vcmpheq

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.gt(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmph_gt_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_gt_PP __builtin_HEXAGON_A2_vcmphgt

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.gtu(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmph_gtu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_gtu_PP __builtin_HEXAGON_A2_vcmphgtu

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.eq(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_eq_PP __builtin_HEXAGON_A2_vcmpweq

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.gt(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_gt_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_gt_PP __builtin_HEXAGON_A2_vcmpwgt

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.gtu(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_gtu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_gtu_PP __builtin_HEXAGON_A2_vcmpwgtu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vconj(Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vconj_P_sat(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vconj_P_sat __builtin_HEXAGON_A2_vconj

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxb(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxb_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxb_PP __builtin_HEXAGON_A2_vmaxb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxh_PP __builtin_HEXAGON_A2_vmaxh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxub(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxub_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxub_PP __builtin_HEXAGON_A2_vmaxub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxuh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxuh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxuh_PP __builtin_HEXAGON_A2_vmaxuh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxuw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxuw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxuw_PP __builtin_HEXAGON_A2_vmaxuw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmaxw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vmaxw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmaxw_PP __builtin_HEXAGON_A2_vmaxw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminb(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminb_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminb_PP __builtin_HEXAGON_A2_vminb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminh_PP __builtin_HEXAGON_A2_vminh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminub(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminub_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminub_PP __builtin_HEXAGON_A2_vminub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminuh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminuh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminuh_PP __builtin_HEXAGON_A2_vminuh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminuw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminuw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminuw_PP __builtin_HEXAGON_A2_vminuw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vminw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vminw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vminw_PP __builtin_HEXAGON_A2_vminw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vnavgh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgh_PP __builtin_HEXAGON_A2_vnavgh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgh(Rtt32,Rss32):crnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vnavgh_PP_crnd_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgh_PP_crnd_sat __builtin_HEXAGON_A2_vnavghcr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgh(Rtt32,Rss32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vnavgh_PP_rnd_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgh_PP_rnd_sat __builtin_HEXAGON_A2_vnavghr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vnavgw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgw_PP __builtin_HEXAGON_A2_vnavgw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgw(Rtt32,Rss32):crnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vnavgw_PP_crnd_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgw_PP_crnd_sat __builtin_HEXAGON_A2_vnavgwcr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vnavgw(Rtt32,Rss32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vnavgw_PP_rnd_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vnavgw_PP_rnd_sat __builtin_HEXAGON_A2_vnavgwr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vraddub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vraddub_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vraddub_PP __builtin_HEXAGON_A2_vraddub

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vraddub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vraddubacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vraddubacc_PP __builtin_HEXAGON_A2_vraddub_acc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrsadub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrsadub_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrsadub_PP __builtin_HEXAGON_A2_vrsadub

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrsadub(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrsadubacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrsadubacc_PP __builtin_HEXAGON_A2_vrsadub_acc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubb(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vsubb_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      MAPPING
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_vsubb_PP __builtin_HEXAGON_A2_vsubb_map

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsubh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubh_PP __builtin_HEXAGON_A2_vsubh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubh(Rtt32,Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vsubh_PP_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubh_PP_sat __builtin_HEXAGON_A2_vsubhs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubub(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsubub_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubub_PP __builtin_HEXAGON_A2_vsubub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubub(Rtt32,Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vsubub_PP_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubub_PP_sat __builtin_HEXAGON_A2_vsububs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubuh(Rtt32,Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vsubuh_PP_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubuh_PP_sat __builtin_HEXAGON_A2_vsubuhs

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsubw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubw_PP __builtin_HEXAGON_A2_vsubw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsubw(Rtt32,Rss32):sat
   C Intrinsic Prototype: Word64 Q6_P_vsubw_PP_sat(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsubw_PP_sat __builtin_HEXAGON_A2_vsubws

/* ==========================================================================
   Assembly Syntax:       Rd32=xor(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_xor_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_xor_RR __builtin_HEXAGON_A2_xor

/* ==========================================================================
   Assembly Syntax:       Rdd32=xor(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_xor_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_xor_PP __builtin_HEXAGON_A2_xorp

/* ==========================================================================
   Assembly Syntax:       Rd32=zxtb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_zxtb_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_zxtb_R __builtin_HEXAGON_A2_zxtb

/* ==========================================================================
   Assembly Syntax:       Rd32=zxth(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_zxth_R(Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_zxth_R __builtin_HEXAGON_A2_zxth

/* ==========================================================================
   Assembly Syntax:       Rd32=and(Rt32,~Rs32)
   C Intrinsic Prototype: Word32 Q6_R_and_RnR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_and_RnR __builtin_HEXAGON_A4_andn

/* ==========================================================================
   Assembly Syntax:       Rdd32=and(Rtt32,~Rss32)
   C Intrinsic Prototype: Word64 Q6_P_and_PnP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_and_PnP __builtin_HEXAGON_A4_andnp

/* ==========================================================================
   Assembly Syntax:       Rdd32=bitsplit(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_bitsplit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_bitsplit_RR __builtin_HEXAGON_A4_bitsplit

/* ==========================================================================
   Assembly Syntax:       Rdd32=bitsplit(Rs32,#u5)
   C Intrinsic Prototype: Word64 Q6_P_bitsplit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_bitsplit_RI __builtin_HEXAGON_A4_bitspliti

/* ==========================================================================
   Assembly Syntax:       Pd4=boundscheck(Rs32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_boundscheck_RP(Word32 Rs, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_boundscheck_RP __builtin_HEXAGON_A4_boundscheck

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.eq(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmpb_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_eq_RR __builtin_HEXAGON_A4_cmpbeq

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.eq(Rs32,#u8)
   C Intrinsic Prototype: Byte Q6_p_cmpb_eq_RI(Word32 Rs, Word32 Iu8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_eq_RI __builtin_HEXAGON_A4_cmpbeqi

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.gt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmpb_gt_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_gt_RR __builtin_HEXAGON_A4_cmpbgt

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.gt(Rs32,#s8)
   C Intrinsic Prototype: Byte Q6_p_cmpb_gt_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_gt_RI __builtin_HEXAGON_A4_cmpbgti

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.gtu(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmpb_gtu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_gtu_RR __builtin_HEXAGON_A4_cmpbgtu

/* ==========================================================================
   Assembly Syntax:       Pd4=cmpb.gtu(Rs32,#u7)
   C Intrinsic Prototype: Byte Q6_p_cmpb_gtu_RI(Word32 Rs, Word32 Iu7)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmpb_gtu_RI __builtin_HEXAGON_A4_cmpbgtui

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.eq(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmph_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_eq_RR __builtin_HEXAGON_A4_cmpheq

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.eq(Rs32,#s8)
   C Intrinsic Prototype: Byte Q6_p_cmph_eq_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_eq_RI __builtin_HEXAGON_A4_cmpheqi

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.gt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmph_gt_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_gt_RR __builtin_HEXAGON_A4_cmphgt

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.gt(Rs32,#s8)
   C Intrinsic Prototype: Byte Q6_p_cmph_gt_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_gt_RI __builtin_HEXAGON_A4_cmphgti

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.gtu(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmph_gtu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_gtu_RR __builtin_HEXAGON_A4_cmphgtu

/* ==========================================================================
   Assembly Syntax:       Pd4=cmph.gtu(Rs32,#u7)
   C Intrinsic Prototype: Byte Q6_p_cmph_gtu_RI(Word32 Rs, Word32 Iu7)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmph_gtu_RI __builtin_HEXAGON_A4_cmphgtui

/* ==========================================================================
   Assembly Syntax:       Rdd32=combine(#s8,Rs32)
   C Intrinsic Prototype: Word64 Q6_P_combine_IR(Word32 Is8, Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_combine_IR __builtin_HEXAGON_A4_combineir

/* ==========================================================================
   Assembly Syntax:       Rdd32=combine(Rs32,#s8)
   C Intrinsic Prototype: Word64 Q6_P_combine_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_combine_RI __builtin_HEXAGON_A4_combineri

/* ==========================================================================
   Assembly Syntax:       Rd32=cround(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_cround_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cround_RI __builtin_HEXAGON_A4_cround_ri

/* ==========================================================================
   Assembly Syntax:       Rd32=cround(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_cround_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cround_RR __builtin_HEXAGON_A4_cround_rr

/* ==========================================================================
   Assembly Syntax:       Rd32=modwrap(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_modwrap_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_modwrap_RR __builtin_HEXAGON_A4_modwrapu

/* ==========================================================================
   Assembly Syntax:       Rd32=or(Rt32,~Rs32)
   C Intrinsic Prototype: Word32 Q6_R_or_RnR(Word32 Rt, Word32 Rs)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_or_RnR __builtin_HEXAGON_A4_orn

/* ==========================================================================
   Assembly Syntax:       Rdd32=or(Rtt32,~Rss32)
   C Intrinsic Prototype: Word64 Q6_P_or_PnP(Word64 Rtt, Word64 Rss)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_or_PnP __builtin_HEXAGON_A4_ornp

/* ==========================================================================
   Assembly Syntax:       Rd32=cmp.eq(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_cmp_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_cmp_eq_RR __builtin_HEXAGON_A4_rcmpeq

/* ==========================================================================
   Assembly Syntax:       Rd32=cmp.eq(Rs32,#s8)
   C Intrinsic Prototype: Word32 Q6_R_cmp_eq_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_cmp_eq_RI __builtin_HEXAGON_A4_rcmpeqi

/* ==========================================================================
   Assembly Syntax:       Rd32=!cmp.eq(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_not_cmp_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_not_cmp_eq_RR __builtin_HEXAGON_A4_rcmpneq

/* ==========================================================================
   Assembly Syntax:       Rd32=!cmp.eq(Rs32,#s8)
   C Intrinsic Prototype: Word32 Q6_R_not_cmp_eq_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_not_cmp_eq_RI __builtin_HEXAGON_A4_rcmpneqi

/* ==========================================================================
   Assembly Syntax:       Rd32=round(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_round_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_round_RI __builtin_HEXAGON_A4_round_ri

/* ==========================================================================
   Assembly Syntax:       Rd32=round(Rs32,#u5):sat
   C Intrinsic Prototype: Word32 Q6_R_round_RI_sat(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_round_RI_sat __builtin_HEXAGON_A4_round_ri_sat

/* ==========================================================================
   Assembly Syntax:       Rd32=round(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_round_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_round_RR __builtin_HEXAGON_A4_round_rr

/* ==========================================================================
   Assembly Syntax:       Rd32=round(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_round_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_round_RR_sat __builtin_HEXAGON_A4_round_rr_sat

/* ==========================================================================
   Assembly Syntax:       Pd4=tlbmatch(Rss32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_tlbmatch_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_tlbmatch_PR __builtin_HEXAGON_A4_tlbmatch

/* ==========================================================================
   Assembly Syntax:       Pd4=any8(vcmpb.eq(Rss32,Rtt32))
   C Intrinsic Prototype: Byte Q6_p_any8_vcmpb_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_any8_vcmpb_eq_PP __builtin_HEXAGON_A4_vcmpbeq_any

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.eq(Rss32,#u8)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_eq_PI(Word64 Rss, Word32 Iu8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_eq_PI __builtin_HEXAGON_A4_vcmpbeqi

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.gt(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_gt_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_gt_PP __builtin_HEXAGON_A4_vcmpbgt

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.gt(Rss32,#s8)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_gt_PI(Word64 Rss, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_gt_PI __builtin_HEXAGON_A4_vcmpbgti

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpb.gtu(Rss32,#u7)
   C Intrinsic Prototype: Byte Q6_p_vcmpb_gtu_PI(Word64 Rss, Word32 Iu7)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpb_gtu_PI __builtin_HEXAGON_A4_vcmpbgtui

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.eq(Rss32,#s8)
   C Intrinsic Prototype: Byte Q6_p_vcmph_eq_PI(Word64 Rss, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_eq_PI __builtin_HEXAGON_A4_vcmpheqi

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.gt(Rss32,#s8)
   C Intrinsic Prototype: Byte Q6_p_vcmph_gt_PI(Word64 Rss, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_gt_PI __builtin_HEXAGON_A4_vcmphgti

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmph.gtu(Rss32,#u7)
   C Intrinsic Prototype: Byte Q6_p_vcmph_gtu_PI(Word64 Rss, Word32 Iu7)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmph_gtu_PI __builtin_HEXAGON_A4_vcmphgtui

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.eq(Rss32,#s8)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_eq_PI(Word64 Rss, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_eq_PI __builtin_HEXAGON_A4_vcmpweqi

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.gt(Rss32,#s8)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_gt_PI(Word64 Rss, Word32 Is8)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_gt_PI __builtin_HEXAGON_A4_vcmpwgti

/* ==========================================================================
   Assembly Syntax:       Pd4=vcmpw.gtu(Rss32,#u7)
   C Intrinsic Prototype: Byte Q6_p_vcmpw_gtu_PI(Word64 Rss, Word32 Iu7)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_vcmpw_gtu_PI __builtin_HEXAGON_A4_vcmpwgtui

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrmaxh(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrmaxh_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmaxh_PR __builtin_HEXAGON_A4_vrmaxh

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrmaxuh(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrmaxuh_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmaxuh_PR __builtin_HEXAGON_A4_vrmaxuh

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrmaxuw(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrmaxuw_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmaxuw_PR __builtin_HEXAGON_A4_vrmaxuw

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrmaxw(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrmaxw_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmaxw_PR __builtin_HEXAGON_A4_vrmaxw

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrminh(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrminh_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrminh_PR __builtin_HEXAGON_A4_vrminh

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrminuh(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrminuh_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrminuh_PR __builtin_HEXAGON_A4_vrminuh

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrminuw(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrminuw_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrminuw_PR __builtin_HEXAGON_A4_vrminuw

/* ==========================================================================
   Assembly Syntax:       Rxx32=vrminw(Rss32,Ru32)
   C Intrinsic Prototype: Word64 Q6_P_vrminw_PR(Word64 Rxx, Word64 Rss, Word32 Ru)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrminw_PR __builtin_HEXAGON_A4_vrminw

/* ==========================================================================
   Assembly Syntax:       Rd32=vaddhub(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word32 Q6_R_vaddhub_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vaddhub_PP_sat __builtin_HEXAGON_A5_vaddhubs

/* ==========================================================================
   Assembly Syntax:       Pd4=all8(Ps4)
   C Intrinsic Prototype: Byte Q6_p_all8_p(Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_all8_p __builtin_HEXAGON_C2_all8

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Pt4,Ps4)
   C Intrinsic Prototype: Byte Q6_p_and_pp(Byte Pt, Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_pp __builtin_HEXAGON_C2_and

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Pt4,!Ps4)
   C Intrinsic Prototype: Byte Q6_p_and_pnp(Byte Pt, Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_pnp __builtin_HEXAGON_C2_andn

/* ==========================================================================
   Assembly Syntax:       Pd4=any8(Ps4)
   C Intrinsic Prototype: Byte Q6_p_any8_p(Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_any8_p __builtin_HEXAGON_C2_any8

/* ==========================================================================
   Assembly Syntax:       Pd4=bitsclr(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_bitsclr_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_bitsclr_RR __builtin_HEXAGON_C2_bitsclr

/* ==========================================================================
   Assembly Syntax:       Pd4=bitsclr(Rs32,#u6)
   C Intrinsic Prototype: Byte Q6_p_bitsclr_RI(Word32 Rs, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_bitsclr_RI __builtin_HEXAGON_C2_bitsclri

/* ==========================================================================
   Assembly Syntax:       Pd4=bitsset(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_bitsset_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_bitsset_RR __builtin_HEXAGON_C2_bitsset

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.eq(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_eq_RR __builtin_HEXAGON_C2_cmpeq

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.eq(Rs32,#s10)
   C Intrinsic Prototype: Byte Q6_p_cmp_eq_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_eq_RI __builtin_HEXAGON_C2_cmpeqi

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.eq(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmp_eq_PP __builtin_HEXAGON_C2_cmpeqp

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.ge(Rs32,#s8)
   C Intrinsic Prototype: Byte Q6_p_cmp_ge_RI(Word32 Rs, Word32 Is8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_ge_RI __builtin_HEXAGON_C2_cmpgei

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.geu(Rs32,#u8)
   C Intrinsic Prototype: Byte Q6_p_cmp_geu_RI(Word32 Rs, Word32 Iu8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_geu_RI __builtin_HEXAGON_C2_cmpgeui

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_gt_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_gt_RR __builtin_HEXAGON_C2_cmpgt

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gt(Rs32,#s10)
   C Intrinsic Prototype: Byte Q6_p_cmp_gt_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_gt_RI __builtin_HEXAGON_C2_cmpgti

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gt(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_gt_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmp_gt_PP __builtin_HEXAGON_C2_cmpgtp

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gtu(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_gtu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_gtu_RR __builtin_HEXAGON_C2_cmpgtu

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gtu(Rs32,#u9)
   C Intrinsic Prototype: Byte Q6_p_cmp_gtu_RI(Word32 Rs, Word32 Iu9)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_gtu_RI __builtin_HEXAGON_C2_cmpgtui

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.gtu(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_gtu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_cmp_gtu_PP __builtin_HEXAGON_C2_cmpgtup

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.lt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_lt_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_lt_RR __builtin_HEXAGON_C2_cmplt

/* ==========================================================================
   Assembly Syntax:       Pd4=cmp.ltu(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_cmp_ltu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_cmp_ltu_RR __builtin_HEXAGON_C2_cmpltu

/* ==========================================================================
   Assembly Syntax:       Rdd32=mask(Pt4)
   C Intrinsic Prototype: Word64 Q6_P_mask_p(Byte Pt)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mask_p __builtin_HEXAGON_C2_mask

/* ==========================================================================
   Assembly Syntax:       Rd32=mux(Pu4,Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mux_pRR(Byte Pu, Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mux_pRR __builtin_HEXAGON_C2_mux

/* ==========================================================================
   Assembly Syntax:       Rd32=mux(Pu4,#s8,#S8)
   C Intrinsic Prototype: Word32 Q6_R_mux_pII(Byte Pu, Word32 Is8, Word32 IS8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mux_pII __builtin_HEXAGON_C2_muxii

/* ==========================================================================
   Assembly Syntax:       Rd32=mux(Pu4,Rs32,#s8)
   C Intrinsic Prototype: Word32 Q6_R_mux_pRI(Byte Pu, Word32 Rs, Word32 Is8)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mux_pRI __builtin_HEXAGON_C2_muxir

/* ==========================================================================
   Assembly Syntax:       Rd32=mux(Pu4,#s8,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_mux_pIR(Byte Pu, Word32 Is8, Word32 Rs)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mux_pIR __builtin_HEXAGON_C2_muxri

/* ==========================================================================
   Assembly Syntax:       Pd4=not(Ps4)
   C Intrinsic Prototype: Byte Q6_p_not_p(Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_p __builtin_HEXAGON_C2_not

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Pt4,Ps4)
   C Intrinsic Prototype: Byte Q6_p_or_pp(Byte Pt, Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_pp __builtin_HEXAGON_C2_or

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Pt4,!Ps4)
   C Intrinsic Prototype: Byte Q6_p_or_pnp(Byte Pt, Byte Ps)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_pnp __builtin_HEXAGON_C2_orn

/* ==========================================================================
   Assembly Syntax:       Pd4=Ps4
   C Intrinsic Prototype: Byte Q6_p_equals_p(Byte Ps)
   Instruction Type:      MAPPING
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_equals_p __builtin_HEXAGON_C2_pxfer_map

/* ==========================================================================
   Assembly Syntax:       Rd32=Ps4
   C Intrinsic Prototype: Word32 Q6_R_equals_p(Byte Ps)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_equals_p __builtin_HEXAGON_C2_tfrpr

/* ==========================================================================
   Assembly Syntax:       Pd4=Rs32
   C Intrinsic Prototype: Byte Q6_p_equals_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_equals_R __builtin_HEXAGON_C2_tfrrp

/* ==========================================================================
   Assembly Syntax:       Rd32=vitpack(Ps4,Pt4)
   C Intrinsic Prototype: Word32 Q6_R_vitpack_pp(Byte Ps, Byte Pt)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vitpack_pp __builtin_HEXAGON_C2_vitpack

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmux(Pu4,Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vmux_pPP(Byte Pu, Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmux_pPP __builtin_HEXAGON_C2_vmux

/* ==========================================================================
   Assembly Syntax:       Pd4=xor(Ps4,Pt4)
   C Intrinsic Prototype: Byte Q6_p_xor_pp(Byte Ps, Byte Pt)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_xor_pp __builtin_HEXAGON_C2_xor

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Ps4,and(Pt4,Pu4))
   C Intrinsic Prototype: Byte Q6_p_and_and_ppp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_and_ppp __builtin_HEXAGON_C4_and_and

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Ps4,and(Pt4,!Pu4))
   C Intrinsic Prototype: Byte Q6_p_and_and_ppnp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_and_ppnp __builtin_HEXAGON_C4_and_andn

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Ps4,or(Pt4,Pu4))
   C Intrinsic Prototype: Byte Q6_p_and_or_ppp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_or_ppp __builtin_HEXAGON_C4_and_or

/* ==========================================================================
   Assembly Syntax:       Pd4=and(Ps4,or(Pt4,!Pu4))
   C Intrinsic Prototype: Byte Q6_p_and_or_ppnp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_and_or_ppnp __builtin_HEXAGON_C4_and_orn

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.gt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_gt_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_gt_RR __builtin_HEXAGON_C4_cmplte

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.gt(Rs32,#s10)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_gt_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_gt_RI __builtin_HEXAGON_C4_cmpltei

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.gtu(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_gtu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_gtu_RR __builtin_HEXAGON_C4_cmplteu

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.gtu(Rs32,#u9)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_gtu_RI(Word32 Rs, Word32 Iu9)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_gtu_RI __builtin_HEXAGON_C4_cmplteui

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.eq(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_eq_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_eq_RR __builtin_HEXAGON_C4_cmpneq

/* ==========================================================================
   Assembly Syntax:       Pd4=!cmp.eq(Rs32,#s10)
   C Intrinsic Prototype: Byte Q6_p_not_cmp_eq_RI(Word32 Rs, Word32 Is10)
   Instruction Type:      ALU32_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_p_not_cmp_eq_RI __builtin_HEXAGON_C4_cmpneqi

/* ==========================================================================
   Assembly Syntax:       Pd4=fastcorner9(Ps4,Pt4)
   C Intrinsic Prototype: Byte Q6_p_fastcorner9_pp(Byte Ps, Byte Pt)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_fastcorner9_pp __builtin_HEXAGON_C4_fastcorner9

/* ==========================================================================
   Assembly Syntax:       Pd4=!fastcorner9(Ps4,Pt4)
   C Intrinsic Prototype: Byte Q6_p_not_fastcorner9_pp(Byte Ps, Byte Pt)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_fastcorner9_pp __builtin_HEXAGON_C4_fastcorner9_not

/* ==========================================================================
   Assembly Syntax:       Pd4=!bitsclr(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_bitsclr_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_bitsclr_RR __builtin_HEXAGON_C4_nbitsclr

/* ==========================================================================
   Assembly Syntax:       Pd4=!bitsclr(Rs32,#u6)
   C Intrinsic Prototype: Byte Q6_p_not_bitsclr_RI(Word32 Rs, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_bitsclr_RI __builtin_HEXAGON_C4_nbitsclri

/* ==========================================================================
   Assembly Syntax:       Pd4=!bitsset(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_bitsset_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_bitsset_RR __builtin_HEXAGON_C4_nbitsset

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Ps4,and(Pt4,Pu4))
   C Intrinsic Prototype: Byte Q6_p_or_and_ppp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_and_ppp __builtin_HEXAGON_C4_or_and

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Ps4,and(Pt4,!Pu4))
   C Intrinsic Prototype: Byte Q6_p_or_and_ppnp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_and_ppnp __builtin_HEXAGON_C4_or_andn

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Ps4,or(Pt4,Pu4))
   C Intrinsic Prototype: Byte Q6_p_or_or_ppp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_or_ppp __builtin_HEXAGON_C4_or_or

/* ==========================================================================
   Assembly Syntax:       Pd4=or(Ps4,or(Pt4,!Pu4))
   C Intrinsic Prototype: Byte Q6_p_or_or_ppnp(Byte Ps, Byte Pt, Byte Pu)
   Instruction Type:      CR
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_or_or_ppnp __builtin_HEXAGON_C4_or_orn

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_d2df(Rss32)
   C Intrinsic Prototype: Float64 Q6_P_convert_d2df_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_d2df_P __builtin_HEXAGON_F2_conv_d2df

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_d2sf(Rss32)
   C Intrinsic Prototype: Float32 Q6_R_convert_d2sf_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_d2sf_P __builtin_HEXAGON_F2_conv_d2sf

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_df2d(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_convert_df2d_P(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_df2d_P __builtin_HEXAGON_F2_conv_df2d

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_df2d(Rss32):chop
   C Intrinsic Prototype: Word64 Q6_P_convert_df2d_P_chop(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_df2d_P_chop __builtin_HEXAGON_F2_conv_df2d_chop

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_df2sf(Rss32)
   C Intrinsic Prototype: Float32 Q6_R_convert_df2sf_P(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_df2sf_P __builtin_HEXAGON_F2_conv_df2sf

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_df2ud(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_convert_df2ud_P(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_df2ud_P __builtin_HEXAGON_F2_conv_df2ud

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_df2ud(Rss32):chop
   C Intrinsic Prototype: Word64 Q6_P_convert_df2ud_P_chop(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_df2ud_P_chop __builtin_HEXAGON_F2_conv_df2ud_chop

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_df2uw(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_convert_df2uw_P(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_df2uw_P __builtin_HEXAGON_F2_conv_df2uw

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_df2uw(Rss32):chop
   C Intrinsic Prototype: Word32 Q6_R_convert_df2uw_P_chop(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_df2uw_P_chop __builtin_HEXAGON_F2_conv_df2uw_chop

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_df2w(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_convert_df2w_P(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_df2w_P __builtin_HEXAGON_F2_conv_df2w

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_df2w(Rss32):chop
   C Intrinsic Prototype: Word32 Q6_R_convert_df2w_P_chop(Float64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_df2w_P_chop __builtin_HEXAGON_F2_conv_df2w_chop

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_sf2d(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_convert_sf2d_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_sf2d_R __builtin_HEXAGON_F2_conv_sf2d

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_sf2d(Rs32):chop
   C Intrinsic Prototype: Word64 Q6_P_convert_sf2d_R_chop(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_sf2d_R_chop __builtin_HEXAGON_F2_conv_sf2d_chop

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_sf2df(Rs32)
   C Intrinsic Prototype: Float64 Q6_P_convert_sf2df_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_sf2df_R __builtin_HEXAGON_F2_conv_sf2df

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_sf2ud(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_convert_sf2ud_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_sf2ud_R __builtin_HEXAGON_F2_conv_sf2ud

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_sf2ud(Rs32):chop
   C Intrinsic Prototype: Word64 Q6_P_convert_sf2ud_R_chop(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_sf2ud_R_chop __builtin_HEXAGON_F2_conv_sf2ud_chop

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_sf2uw(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_convert_sf2uw_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_sf2uw_R __builtin_HEXAGON_F2_conv_sf2uw

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_sf2uw(Rs32):chop
   C Intrinsic Prototype: Word32 Q6_R_convert_sf2uw_R_chop(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_sf2uw_R_chop __builtin_HEXAGON_F2_conv_sf2uw_chop

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_sf2w(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_convert_sf2w_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_sf2w_R __builtin_HEXAGON_F2_conv_sf2w

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_sf2w(Rs32):chop
   C Intrinsic Prototype: Word32 Q6_R_convert_sf2w_R_chop(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_sf2w_R_chop __builtin_HEXAGON_F2_conv_sf2w_chop

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_ud2df(Rss32)
   C Intrinsic Prototype: Float64 Q6_P_convert_ud2df_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_ud2df_P __builtin_HEXAGON_F2_conv_ud2df

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_ud2sf(Rss32)
   C Intrinsic Prototype: Float32 Q6_R_convert_ud2sf_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_ud2sf_P __builtin_HEXAGON_F2_conv_ud2sf

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_uw2df(Rs32)
   C Intrinsic Prototype: Float64 Q6_P_convert_uw2df_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_uw2df_R __builtin_HEXAGON_F2_conv_uw2df

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_uw2sf(Rs32)
   C Intrinsic Prototype: Float32 Q6_R_convert_uw2sf_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_uw2sf_R __builtin_HEXAGON_F2_conv_uw2sf

/* ==========================================================================
   Assembly Syntax:       Rdd32=convert_w2df(Rs32)
   C Intrinsic Prototype: Float64 Q6_P_convert_w2df_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_convert_w2df_R __builtin_HEXAGON_F2_conv_w2df

/* ==========================================================================
   Assembly Syntax:       Rd32=convert_w2sf(Rs32)
   C Intrinsic Prototype: Float32 Q6_R_convert_w2sf_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_convert_w2sf_R __builtin_HEXAGON_F2_conv_w2sf

/* ==========================================================================
   Assembly Syntax:       Pd4=dfclass(Rss32,#u5)
   C Intrinsic Prototype: Byte Q6_p_dfclass_PI(Float64 Rss, Word32 Iu5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_dfclass_PI __builtin_HEXAGON_F2_dfclass

/* ==========================================================================
   Assembly Syntax:       Pd4=dfcmp.eq(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_dfcmp_eq_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_dfcmp_eq_PP __builtin_HEXAGON_F2_dfcmpeq

/* ==========================================================================
   Assembly Syntax:       Pd4=dfcmp.ge(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_dfcmp_ge_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_dfcmp_ge_PP __builtin_HEXAGON_F2_dfcmpge

/* ==========================================================================
   Assembly Syntax:       Pd4=dfcmp.gt(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_dfcmp_gt_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_dfcmp_gt_PP __builtin_HEXAGON_F2_dfcmpgt

/* ==========================================================================
   Assembly Syntax:       Pd4=dfcmp.uo(Rss32,Rtt32)
   C Intrinsic Prototype: Byte Q6_p_dfcmp_uo_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_dfcmp_uo_PP __builtin_HEXAGON_F2_dfcmpuo

/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmake(#u10):neg
   C Intrinsic Prototype: Float64 Q6_P_dfmake_I_neg(Word32 Iu10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmake_I_neg __builtin_HEXAGON_F2_dfimm_n

/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmake(#u10):pos
   C Intrinsic Prototype: Float64 Q6_P_dfmake_I_pos(Word32 Iu10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmake_I_pos __builtin_HEXAGON_F2_dfimm_p

/* ==========================================================================
   Assembly Syntax:       Rd32=sfadd(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfadd_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfadd_RR __builtin_HEXAGON_F2_sfadd

/* ==========================================================================
   Assembly Syntax:       Pd4=sfclass(Rs32,#u5)
   C Intrinsic Prototype: Byte Q6_p_sfclass_RI(Float32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_sfclass_RI __builtin_HEXAGON_F2_sfclass

/* ==========================================================================
   Assembly Syntax:       Pd4=sfcmp.eq(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_sfcmp_eq_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_sfcmp_eq_RR __builtin_HEXAGON_F2_sfcmpeq

/* ==========================================================================
   Assembly Syntax:       Pd4=sfcmp.ge(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_sfcmp_ge_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_sfcmp_ge_RR __builtin_HEXAGON_F2_sfcmpge

/* ==========================================================================
   Assembly Syntax:       Pd4=sfcmp.gt(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_sfcmp_gt_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_sfcmp_gt_RR __builtin_HEXAGON_F2_sfcmpgt

/* ==========================================================================
   Assembly Syntax:       Pd4=sfcmp.uo(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_sfcmp_uo_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_sfcmp_uo_RR __builtin_HEXAGON_F2_sfcmpuo

/* ==========================================================================
   Assembly Syntax:       Rd32=sffixupd(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sffixupd_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sffixupd_RR __builtin_HEXAGON_F2_sffixupd

/* ==========================================================================
   Assembly Syntax:       Rd32=sffixupn(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sffixupn_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sffixupn_RR __builtin_HEXAGON_F2_sffixupn

/* ==========================================================================
   Assembly Syntax:       Rd32=sffixupr(Rs32)
   C Intrinsic Prototype: Float32 Q6_R_sffixupr_R(Float32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sffixupr_R __builtin_HEXAGON_F2_sffixupr

/* ==========================================================================
   Assembly Syntax:       Rx32+=sfmpy(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfmpyacc_RR(Float32 Rx, Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpyacc_RR __builtin_HEXAGON_F2_sffma

/* ==========================================================================
   Assembly Syntax:       Rx32+=sfmpy(Rs32,Rt32):lib
   C Intrinsic Prototype: Float32 Q6_R_sfmpyacc_RR_lib(Float32 Rx, Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpyacc_RR_lib __builtin_HEXAGON_F2_sffma_lib

/* ==========================================================================
   Assembly Syntax:       Rx32+=sfmpy(Rs32,Rt32,Pu4):scale
   C Intrinsic Prototype: Float32 Q6_R_sfmpyacc_RRp_scale(Float32 Rx, Float32 Rs, Float32 Rt, Byte Pu)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpyacc_RRp_scale __builtin_HEXAGON_F2_sffma_sc

/* ==========================================================================
   Assembly Syntax:       Rx32-=sfmpy(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfmpynac_RR(Float32 Rx, Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpynac_RR __builtin_HEXAGON_F2_sffms

/* ==========================================================================
   Assembly Syntax:       Rx32-=sfmpy(Rs32,Rt32):lib
   C Intrinsic Prototype: Float32 Q6_R_sfmpynac_RR_lib(Float32 Rx, Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpynac_RR_lib __builtin_HEXAGON_F2_sffms_lib

/* ==========================================================================
   Assembly Syntax:       Rd32=sfmake(#u10):neg
   C Intrinsic Prototype: Float32 Q6_R_sfmake_I_neg(Word32 Iu10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmake_I_neg __builtin_HEXAGON_F2_sfimm_n

/* ==========================================================================
   Assembly Syntax:       Rd32=sfmake(#u10):pos
   C Intrinsic Prototype: Float32 Q6_R_sfmake_I_pos(Word32 Iu10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmake_I_pos __builtin_HEXAGON_F2_sfimm_p

/* ==========================================================================
   Assembly Syntax:       Rd32=sfmax(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfmax_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmax_RR __builtin_HEXAGON_F2_sfmax

/* ==========================================================================
   Assembly Syntax:       Rd32=sfmin(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfmin_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmin_RR __builtin_HEXAGON_F2_sfmin

/* ==========================================================================
   Assembly Syntax:       Rd32=sfmpy(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfmpy_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfmpy_RR __builtin_HEXAGON_F2_sfmpy

/* ==========================================================================
   Assembly Syntax:       Rd32=sfsub(Rs32,Rt32)
   C Intrinsic Prototype: Float32 Q6_R_sfsub_RR(Float32 Rs, Float32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sfsub_RR __builtin_HEXAGON_F2_sfsub

/* ==========================================================================
   Assembly Syntax:       Rd32=memb(Rx32++#s4:0:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memb_IM_circ(void** Rx, Word32 Is4_0, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memb_IM_circ __builtin_HEXAGON_L2_loadrb_pci

/* ==========================================================================
   Assembly Syntax:       Rd32=memb(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memb_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memb_M_circ __builtin_HEXAGON_L2_loadrb_pcr

/* ==========================================================================
   Assembly Syntax:       Rdd32=memd(Rx32++#s4:3:circ(Mu2))
   C Intrinsic Prototype: Word64 Q6_P_memd_IM_circ(void** Rx, Word32 Is4_3, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_P_memd_IM_circ __builtin_HEXAGON_L2_loadrd_pci

/* ==========================================================================
   Assembly Syntax:       Rdd32=memd(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word64 Q6_P_memd_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_P_memd_M_circ __builtin_HEXAGON_L2_loadrd_pcr

/* ==========================================================================
   Assembly Syntax:       Rd32=memh(Rx32++#s4:1:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memh_IM_circ(void** Rx, Word32 Is4_1, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memh_IM_circ __builtin_HEXAGON_L2_loadrh_pci

/* ==========================================================================
   Assembly Syntax:       Rd32=memh(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memh_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memh_M_circ __builtin_HEXAGON_L2_loadrh_pcr

/* ==========================================================================
   Assembly Syntax:       Rd32=memw(Rx32++#s4:2:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memw_IM_circ(void** Rx, Word32 Is4_2, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memw_IM_circ __builtin_HEXAGON_L2_loadri_pci

/* ==========================================================================
   Assembly Syntax:       Rd32=memw(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memw_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memw_M_circ __builtin_HEXAGON_L2_loadri_pcr

/* ==========================================================================
   Assembly Syntax:       Rd32=memub(Rx32++#s4:0:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memub_IM_circ(void** Rx, Word32 Is4_0, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memub_IM_circ __builtin_HEXAGON_L2_loadrub_pci

/* ==========================================================================
   Assembly Syntax:       Rd32=memub(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memub_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memub_M_circ __builtin_HEXAGON_L2_loadrub_pcr

/* ==========================================================================
   Assembly Syntax:       Rd32=memuh(Rx32++#s4:1:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memuh_IM_circ(void** Rx, Word32 Is4_1, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memuh_IM_circ __builtin_HEXAGON_L2_loadruh_pci

/* ==========================================================================
   Assembly Syntax:       Rd32=memuh(Rx32++I:circ(Mu2))
   C Intrinsic Prototype: Word32 Q6_R_memuh_M_circ(void** Rx, Word32 Mu, void* BaseAddress)
   Instruction Type:      LD
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_R_memuh_M_circ __builtin_HEXAGON_L2_loadruh_pcr

/* ==========================================================================
   Assembly Syntax:       Rx32+=add(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_addacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_addacc_RR __builtin_HEXAGON_M2_acci

/* ==========================================================================
   Assembly Syntax:       Rx32+=add(Rs32,#s8)
   C Intrinsic Prototype: Word32 Q6_R_addacc_RI(Word32 Rx, Word32 Rs, Word32 Is8)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_addacc_RI __builtin_HEXAGON_M2_accii

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyi(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyiacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyiacc_RR __builtin_HEXAGON_M2_cmaci_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyr(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyracc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyracc_RR __builtin_HEXAGON_M2_cmacr_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpy(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpyacc_RR_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyacc_RR_sat __builtin_HEXAGON_M2_cmacs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpyacc_RR_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyacc_RR_s1_sat __builtin_HEXAGON_M2_cmacs_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpy(Rs32,Rt32*):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpyacc_RR_conj_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyacc_RR_conj_sat __builtin_HEXAGON_M2_cmacsc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpy(Rs32,Rt32*):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpyacc_RR_conj_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyacc_RR_conj_s1_sat __builtin_HEXAGON_M2_cmacsc_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyi(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyi_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyi_RR __builtin_HEXAGON_M2_cmpyi_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyr(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyr_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpyr_RR __builtin_HEXAGON_M2_cmpyr_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpy(Rs32,Rt32):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpy_RR_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpy_RR_rnd_sat __builtin_HEXAGON_M2_cmpyrs_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpy(Rs32,Rt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpy_RR_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpy_RR_s1_rnd_sat __builtin_HEXAGON_M2_cmpyrs_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpy(Rs32,Rt32*):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpy_RR_conj_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpy_RR_conj_rnd_sat __builtin_HEXAGON_M2_cmpyrsc_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpy(Rs32,Rt32*):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpy_RR_conj_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpy_RR_conj_s1_rnd_sat __builtin_HEXAGON_M2_cmpyrsc_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpy(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpy_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpy_RR_sat __builtin_HEXAGON_M2_cmpys_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpy_RR_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpy_RR_s1_sat __builtin_HEXAGON_M2_cmpys_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpy(Rs32,Rt32*):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpy_RR_conj_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpy_RR_conj_sat __builtin_HEXAGON_M2_cmpysc_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpy(Rs32,Rt32*):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpy_RR_conj_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpy_RR_conj_s1_sat __builtin_HEXAGON_M2_cmpysc_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=cmpy(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpynac_RR_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpynac_RR_sat __builtin_HEXAGON_M2_cnacs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=cmpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpynac_RR_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpynac_RR_s1_sat __builtin_HEXAGON_M2_cnacs_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=cmpy(Rs32,Rt32*):sat
   C Intrinsic Prototype: Word64 Q6_P_cmpynac_RR_conj_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpynac_RR_conj_sat __builtin_HEXAGON_M2_cnacsc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=cmpy(Rs32,Rt32*):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_cmpynac_RR_conj_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cmpynac_RR_conj_s1_sat __builtin_HEXAGON_M2_cnacsc_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RR __builtin_HEXAGON_M2_dpmpyss_acc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RR __builtin_HEXAGON_M2_dpmpyss_nac_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32):rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RR_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RR_rnd __builtin_HEXAGON_M2_dpmpyss_rnd_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_mpy_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RR __builtin_HEXAGON_M2_dpmpyss_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RR __builtin_HEXAGON_M2_dpmpyuu_acc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RR __builtin_HEXAGON_M2_dpmpyuu_nac_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32,Rt32)
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RR __builtin_HEXAGON_M2_dpmpyuu_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32.h):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RRh_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RRh_s1_rnd_sat __builtin_HEXAGON_M2_hmmpyh_rs1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RRh_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RRh_s1_sat __builtin_HEXAGON_M2_hmmpyh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32.l):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RRl_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RRl_s1_rnd_sat __builtin_HEXAGON_M2_hmmpyl_rs1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RRl_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RRl_s1_sat __builtin_HEXAGON_M2_hmmpyl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyi(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpyiacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyiacc_RR __builtin_HEXAGON_M2_maci

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyi(Rs32,#u8)
   C Intrinsic Prototype: Word32 Q6_R_mpyinac_RI(Word32 Rx, Word32 Rs, Word32 Iu8)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyinac_RI __builtin_HEXAGON_M2_macsin

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyi(Rs32,#u8)
   C Intrinsic Prototype: Word32 Q6_R_mpyiacc_RI(Word32 Rx, Word32 Rs, Word32 Iu8)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyiacc_RI __builtin_HEXAGON_M2_macsip

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywoh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywohacc_PP_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywohacc_PP_rnd_sat __builtin_HEXAGON_M2_mmachs_rs0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywoh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywohacc_PP_s1_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywohacc_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmachs_rs1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywoh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywohacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywohacc_PP_sat __builtin_HEXAGON_M2_mmachs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywoh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywohacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywohacc_PP_s1_sat __builtin_HEXAGON_M2_mmachs_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywehacc_PP_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywehacc_PP_rnd_sat __builtin_HEXAGON_M2_mmacls_rs0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywehacc_PP_s1_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywehacc_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmacls_rs1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywehacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywehacc_PP_sat __builtin_HEXAGON_M2_mmacls_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywehacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywehacc_PP_s1_sat __builtin_HEXAGON_M2_mmacls_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywouh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouhacc_PP_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouhacc_PP_rnd_sat __builtin_HEXAGON_M2_mmacuhs_rs0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywouh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouhacc_PP_s1_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouhacc_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmacuhs_rs1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywouh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouhacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouhacc_PP_sat __builtin_HEXAGON_M2_mmacuhs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpywouh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouhacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouhacc_PP_s1_sat __builtin_HEXAGON_M2_mmacuhs_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweuh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuhacc_PP_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuhacc_PP_rnd_sat __builtin_HEXAGON_M2_mmaculs_rs0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweuh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuhacc_PP_s1_rnd_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuhacc_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmaculs_rs1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweuh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuhacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuhacc_PP_sat __builtin_HEXAGON_M2_mmaculs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyweuh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuhacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuhacc_PP_s1_sat __builtin_HEXAGON_M2_mmaculs_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywoh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywoh_PP_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywoh_PP_rnd_sat __builtin_HEXAGON_M2_mmpyh_rs0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywoh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywoh_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywoh_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmpyh_rs1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywoh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywoh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywoh_PP_sat __builtin_HEXAGON_M2_mmpyh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywoh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywoh_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywoh_PP_s1_sat __builtin_HEXAGON_M2_mmpyh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweh_PP_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweh_PP_rnd_sat __builtin_HEXAGON_M2_mmpyl_rs0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweh_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweh_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmpyl_rs1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweh_PP_sat __builtin_HEXAGON_M2_mmpyl_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweh_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweh_PP_s1_sat __builtin_HEXAGON_M2_mmpyl_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywouh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouh_PP_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouh_PP_rnd_sat __builtin_HEXAGON_M2_mmpyuh_rs0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywouh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouh_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouh_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmpyuh_rs1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywouh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouh_PP_sat __builtin_HEXAGON_M2_mmpyuh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpywouh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpywouh_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpywouh_PP_s1_sat __builtin_HEXAGON_M2_mmpyuh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweuh(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuh_PP_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuh_PP_rnd_sat __builtin_HEXAGON_M2_mmpyul_rs0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweuh(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuh_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuh_PP_s1_rnd_sat __builtin_HEXAGON_M2_mmpyul_rs1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweuh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuh_PP_sat __builtin_HEXAGON_M2_mmpyul_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyweuh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyweuh_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyweuh_PP_s1_sat __builtin_HEXAGON_M2_mmpyul_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRh __builtin_HEXAGON_M2_mpy_acc_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRh_s1 __builtin_HEXAGON_M2_mpy_acc_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRl __builtin_HEXAGON_M2_mpy_acc_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRl_s1 __builtin_HEXAGON_M2_mpy_acc_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRh __builtin_HEXAGON_M2_mpy_acc_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRh_s1 __builtin_HEXAGON_M2_mpy_acc_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRl __builtin_HEXAGON_M2_mpy_acc_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRl_s1 __builtin_HEXAGON_M2_mpy_acc_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRh_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRh_sat __builtin_HEXAGON_M2_mpy_acc_sat_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRh_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRh_s1_sat __builtin_HEXAGON_M2_mpy_acc_sat_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRl_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRl_sat __builtin_HEXAGON_M2_mpy_acc_sat_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.h,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RhRl_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RhRl_s1_sat __builtin_HEXAGON_M2_mpy_acc_sat_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRh_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRh_sat __builtin_HEXAGON_M2_mpy_acc_sat_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRh_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRh_s1_sat __builtin_HEXAGON_M2_mpy_acc_sat_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRl_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRl_sat __builtin_HEXAGON_M2_mpy_acc_sat_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32.l,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RlRl_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RlRl_s1_sat __builtin_HEXAGON_M2_mpy_acc_sat_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh __builtin_HEXAGON_M2_mpy_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_s1 __builtin_HEXAGON_M2_mpy_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl __builtin_HEXAGON_M2_mpy_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_s1 __builtin_HEXAGON_M2_mpy_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh __builtin_HEXAGON_M2_mpy_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_s1 __builtin_HEXAGON_M2_mpy_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl __builtin_HEXAGON_M2_mpy_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_s1 __builtin_HEXAGON_M2_mpy_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRh __builtin_HEXAGON_M2_mpy_nac_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRh_s1 __builtin_HEXAGON_M2_mpy_nac_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRl __builtin_HEXAGON_M2_mpy_nac_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRl_s1 __builtin_HEXAGON_M2_mpy_nac_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRh __builtin_HEXAGON_M2_mpy_nac_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRh_s1 __builtin_HEXAGON_M2_mpy_nac_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRl __builtin_HEXAGON_M2_mpy_nac_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRl_s1 __builtin_HEXAGON_M2_mpy_nac_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRh_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRh_sat __builtin_HEXAGON_M2_mpy_nac_sat_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRh_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRh_s1_sat __builtin_HEXAGON_M2_mpy_nac_sat_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRl_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRl_sat __builtin_HEXAGON_M2_mpy_nac_sat_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.h,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RhRl_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RhRl_s1_sat __builtin_HEXAGON_M2_mpy_nac_sat_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRh_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRh_sat __builtin_HEXAGON_M2_mpy_nac_sat_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRh_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRh_s1_sat __builtin_HEXAGON_M2_mpy_nac_sat_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRl_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRl_sat __builtin_HEXAGON_M2_mpy_nac_sat_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32.l,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RlRl_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RlRl_s1_sat __builtin_HEXAGON_M2_mpy_nac_sat_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_rnd __builtin_HEXAGON_M2_mpy_rnd_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):<<1:rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_s1_rnd __builtin_HEXAGON_M2_mpy_rnd_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_rnd __builtin_HEXAGON_M2_mpy_rnd_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):<<1:rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_s1_rnd __builtin_HEXAGON_M2_mpy_rnd_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_rnd __builtin_HEXAGON_M2_mpy_rnd_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):<<1:rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_s1_rnd __builtin_HEXAGON_M2_mpy_rnd_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_rnd __builtin_HEXAGON_M2_mpy_rnd_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):<<1:rnd
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_s1_rnd __builtin_HEXAGON_M2_mpy_rnd_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_sat __builtin_HEXAGON_M2_mpy_sat_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_s1_sat __builtin_HEXAGON_M2_mpy_sat_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_sat __builtin_HEXAGON_M2_mpy_sat_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_s1_sat __builtin_HEXAGON_M2_mpy_sat_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_sat __builtin_HEXAGON_M2_mpy_sat_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_s1_sat __builtin_HEXAGON_M2_mpy_sat_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_sat __builtin_HEXAGON_M2_mpy_sat_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_s1_sat __builtin_HEXAGON_M2_mpy_sat_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.h):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRh_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRh_s1_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.h,Rt32.l):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RhRl_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RhRl_s1_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.h):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRh_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRh_s1_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32.l,Rt32.l):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RlRl_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RlRl_s1_rnd_sat __builtin_HEXAGON_M2_mpy_sat_rnd_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpy_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RR __builtin_HEXAGON_M2_mpy_up

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpy_RR_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RR_s1 __builtin_HEXAGON_M2_mpy_up_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpy_RR_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpy_RR_s1_sat __builtin_HEXAGON_M2_mpy_up_s1_sat

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RhRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RhRh __builtin_HEXAGON_M2_mpyd_acc_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RhRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RhRh_s1 __builtin_HEXAGON_M2_mpyd_acc_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RhRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RhRl __builtin_HEXAGON_M2_mpyd_acc_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RhRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RhRl_s1 __builtin_HEXAGON_M2_mpyd_acc_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RlRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RlRh __builtin_HEXAGON_M2_mpyd_acc_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RlRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RlRh_s1 __builtin_HEXAGON_M2_mpyd_acc_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RlRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RlRl __builtin_HEXAGON_M2_mpyd_acc_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyacc_RlRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyacc_RlRl_s1 __builtin_HEXAGON_M2_mpyd_acc_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRh __builtin_HEXAGON_M2_mpyd_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRh_s1 __builtin_HEXAGON_M2_mpyd_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRl __builtin_HEXAGON_M2_mpyd_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRl_s1 __builtin_HEXAGON_M2_mpyd_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRh __builtin_HEXAGON_M2_mpyd_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRh_s1 __builtin_HEXAGON_M2_mpyd_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRl __builtin_HEXAGON_M2_mpyd_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRl_s1 __builtin_HEXAGON_M2_mpyd_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RhRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RhRh __builtin_HEXAGON_M2_mpyd_nac_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RhRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RhRh_s1 __builtin_HEXAGON_M2_mpyd_nac_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RhRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RhRl __builtin_HEXAGON_M2_mpyd_nac_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RhRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RhRl_s1 __builtin_HEXAGON_M2_mpyd_nac_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RlRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RlRh __builtin_HEXAGON_M2_mpyd_nac_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RlRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RlRh_s1 __builtin_HEXAGON_M2_mpyd_nac_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RlRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RlRl __builtin_HEXAGON_M2_mpyd_nac_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpy(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpynac_RlRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpynac_RlRl_s1 __builtin_HEXAGON_M2_mpyd_nac_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.h):rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRh_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRh_rnd __builtin_HEXAGON_M2_mpyd_rnd_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.h):<<1:rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRh_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRh_s1_rnd __builtin_HEXAGON_M2_mpyd_rnd_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.l):rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRl_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRl_rnd __builtin_HEXAGON_M2_mpyd_rnd_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.h,Rt32.l):<<1:rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RhRl_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RhRl_s1_rnd __builtin_HEXAGON_M2_mpyd_rnd_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.h):rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRh_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRh_rnd __builtin_HEXAGON_M2_mpyd_rnd_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.h):<<1:rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRh_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRh_s1_rnd __builtin_HEXAGON_M2_mpyd_rnd_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.l):rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRl_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRl_rnd __builtin_HEXAGON_M2_mpyd_rnd_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpy(Rs32.l,Rt32.l):<<1:rnd
   C Intrinsic Prototype: Word64 Q6_P_mpy_RlRl_s1_rnd(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpy_RlRl_s1_rnd __builtin_HEXAGON_M2_mpyd_rnd_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyi(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpyi_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyi_RR __builtin_HEXAGON_M2_mpyi

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyi(Rs32,#m9)
   C Intrinsic Prototype: Word32 Q6_R_mpyi_RI(Word32 Rs, Word32 Im9)
   Instruction Type:      M
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mpyi_RI __builtin_HEXAGON_M2_mpysmi

/* ==========================================================================
   Assembly Syntax:       Rd32=mpysu(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpysu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpysu_RR __builtin_HEXAGON_M2_mpysu_up

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RhRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RhRh __builtin_HEXAGON_M2_mpyu_acc_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RhRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RhRh_s1 __builtin_HEXAGON_M2_mpyu_acc_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RhRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RhRl __builtin_HEXAGON_M2_mpyu_acc_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RhRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RhRl_s1 __builtin_HEXAGON_M2_mpyu_acc_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RlRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RlRh __builtin_HEXAGON_M2_mpyu_acc_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RlRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RlRh_s1 __builtin_HEXAGON_M2_mpyu_acc_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RlRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RlRl __builtin_HEXAGON_M2_mpyu_acc_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyuacc_RlRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyuacc_RlRl_s1 __builtin_HEXAGON_M2_mpyu_acc_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RhRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RhRh __builtin_HEXAGON_M2_mpyu_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RhRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RhRh_s1 __builtin_HEXAGON_M2_mpyu_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RhRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RhRl __builtin_HEXAGON_M2_mpyu_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RhRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RhRl_s1 __builtin_HEXAGON_M2_mpyu_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RlRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RlRh __builtin_HEXAGON_M2_mpyu_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RlRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RlRh_s1 __builtin_HEXAGON_M2_mpyu_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RlRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RlRl __builtin_HEXAGON_M2_mpyu_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RlRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RlRl_s1 __builtin_HEXAGON_M2_mpyu_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RhRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RhRh __builtin_HEXAGON_M2_mpyu_nac_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RhRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RhRh_s1 __builtin_HEXAGON_M2_mpyu_nac_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RhRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RhRl __builtin_HEXAGON_M2_mpyu_nac_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RhRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RhRl_s1 __builtin_HEXAGON_M2_mpyu_nac_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RlRh(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RlRh __builtin_HEXAGON_M2_mpyu_nac_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RlRh_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RlRh_s1 __builtin_HEXAGON_M2_mpyu_nac_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RlRl(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RlRl __builtin_HEXAGON_M2_mpyu_nac_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word32 Q6_R_mpyunac_RlRl_s1(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyunac_RlRl_s1 __builtin_HEXAGON_M2_mpyu_nac_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyu(Rs32,Rt32)
   C Intrinsic Prototype: UWord32 Q6_R_mpyu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyu_RR __builtin_HEXAGON_M2_mpyu_up

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RhRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RhRh __builtin_HEXAGON_M2_mpyud_acc_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RhRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RhRh_s1 __builtin_HEXAGON_M2_mpyud_acc_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RhRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RhRl __builtin_HEXAGON_M2_mpyud_acc_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RhRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RhRl_s1 __builtin_HEXAGON_M2_mpyud_acc_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RlRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RlRh __builtin_HEXAGON_M2_mpyud_acc_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RlRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RlRh_s1 __builtin_HEXAGON_M2_mpyud_acc_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RlRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RlRl __builtin_HEXAGON_M2_mpyud_acc_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyuacc_RlRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyuacc_RlRl_s1 __builtin_HEXAGON_M2_mpyud_acc_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RhRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RhRh __builtin_HEXAGON_M2_mpyud_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RhRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RhRh_s1 __builtin_HEXAGON_M2_mpyud_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RhRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RhRl __builtin_HEXAGON_M2_mpyud_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RhRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RhRl_s1 __builtin_HEXAGON_M2_mpyud_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RlRh(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RlRh __builtin_HEXAGON_M2_mpyud_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RlRh_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RlRh_s1 __builtin_HEXAGON_M2_mpyud_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RlRl(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RlRl __builtin_HEXAGON_M2_mpyud_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: UWord64 Q6_P_mpyu_RlRl_s1(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyu_RlRl_s1 __builtin_HEXAGON_M2_mpyud_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.h,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RhRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RhRh __builtin_HEXAGON_M2_mpyud_nac_hh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.h,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RhRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RhRh_s1 __builtin_HEXAGON_M2_mpyud_nac_hh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.h,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RhRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RhRl __builtin_HEXAGON_M2_mpyud_nac_hl_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.h,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RhRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RhRl_s1 __builtin_HEXAGON_M2_mpyud_nac_hl_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.l,Rt32.h)
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RlRh(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RlRh __builtin_HEXAGON_M2_mpyud_nac_lh_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.l,Rt32.h):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RlRh_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RlRh_s1 __builtin_HEXAGON_M2_mpyud_nac_lh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.l,Rt32.l)
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RlRl(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RlRl __builtin_HEXAGON_M2_mpyud_nac_ll_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32-=mpyu(Rs32.l,Rt32.l):<<1
   C Intrinsic Prototype: Word64 Q6_P_mpyunac_RlRl_s1(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_mpyunac_RlRl_s1 __builtin_HEXAGON_M2_mpyud_nac_ll_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=mpyui(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpyui_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_mpyui_RR __builtin_HEXAGON_M2_mpyui

/* ==========================================================================
   Assembly Syntax:       Rx32-=add(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_addnac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_addnac_RR __builtin_HEXAGON_M2_nacci

/* ==========================================================================
   Assembly Syntax:       Rx32-=add(Rs32,#s8)
   C Intrinsic Prototype: Word32 Q6_R_addnac_RI(Word32 Rx, Word32 Rs, Word32 Is8)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_addnac_RI __builtin_HEXAGON_M2_naccii

/* ==========================================================================
   Assembly Syntax:       Rx32+=sub(Rt32,Rs32)
   C Intrinsic Prototype: Word32 Q6_R_subacc_RR(Word32 Rx, Word32 Rt, Word32 Rs)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_subacc_RR __builtin_HEXAGON_M2_subacc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsdiffh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsdiffh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsdiffh_PP __builtin_HEXAGON_M2_vabsdiffh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsdiffw(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsdiffw_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsdiffw_PP __builtin_HEXAGON_M2_vabsdiffw

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vcmpyi(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyiacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyiacc_PP_sat __builtin_HEXAGON_M2_vcmac_s0_sat_i

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vcmpyr(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyracc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyracc_PP_sat __builtin_HEXAGON_M2_vcmac_s0_sat_r

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcmpyi(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyi_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyi_PP_sat __builtin_HEXAGON_M2_vcmpy_s0_sat_i

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcmpyr(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyr_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyr_PP_sat __builtin_HEXAGON_M2_vcmpy_s0_sat_r

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcmpyi(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyi_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyi_PP_s1_sat __builtin_HEXAGON_M2_vcmpy_s1_sat_i

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcmpyr(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vcmpyr_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcmpyr_PP_s1_sat __builtin_HEXAGON_M2_vcmpy_s1_sat_r

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vdmpy(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpyacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpyacc_PP_sat __builtin_HEXAGON_M2_vdmacs_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vdmpy(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpyacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpyacc_PP_s1_sat __builtin_HEXAGON_M2_vdmacs_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=vdmpy(Rss32,Rtt32):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vdmpy_PP_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vdmpy_PP_rnd_sat __builtin_HEXAGON_M2_vdmpyrs_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=vdmpy(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vdmpy_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vdmpy_PP_s1_rnd_sat __builtin_HEXAGON_M2_vdmpyrs_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vdmpy(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpy_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpy_PP_sat __builtin_HEXAGON_M2_vdmpys_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vdmpy(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpy_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpy_PP_s1_sat __builtin_HEXAGON_M2_vdmpys_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyh(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpyhacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhacc_RR __builtin_HEXAGON_M2_vmac2

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyeh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpyehacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyehacc_PP __builtin_HEXAGON_M2_vmac2es

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyeh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyehacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyehacc_PP_sat __builtin_HEXAGON_M2_vmac2es_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyeh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyehacc_PP_s1_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyehacc_PP_s1_sat __builtin_HEXAGON_M2_vmac2es_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyh(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhacc_RR_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhacc_RR_sat __builtin_HEXAGON_M2_vmac2s_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyh(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhacc_RR_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhacc_RR_s1_sat __builtin_HEXAGON_M2_vmac2s_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyhsu(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhsuacc_RR_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhsuacc_RR_sat __builtin_HEXAGON_M2_vmac2su_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpyhsu(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhsuacc_RR_s1_sat(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhsuacc_RR_s1_sat __builtin_HEXAGON_M2_vmac2su_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyeh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyeh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyeh_PP_sat __builtin_HEXAGON_M2_vmpy2es_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyeh(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyeh_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyeh_PP_s1_sat __builtin_HEXAGON_M2_vmpy2es_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyh(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyh_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyh_RR_sat __builtin_HEXAGON_M2_vmpy2s_s0

/* ==========================================================================
   Assembly Syntax:       Rd32=vmpyh(Rs32,Rt32):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vmpyh_RR_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vmpyh_RR_rnd_sat __builtin_HEXAGON_M2_vmpy2s_s0pack

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyh(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyh_RR_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyh_RR_s1_sat __builtin_HEXAGON_M2_vmpy2s_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=vmpyh(Rs32,Rt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vmpyh_RR_s1_rnd_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vmpyh_RR_s1_rnd_sat __builtin_HEXAGON_M2_vmpy2s_s1pack

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyhsu(Rs32,Rt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhsu_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhsu_RR_sat __builtin_HEXAGON_M2_vmpy2su_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpyhsu(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vmpyhsu_RR_s1_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpyhsu_RR_s1_sat __builtin_HEXAGON_M2_vmpy2su_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=vraddh(Rss32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_vraddh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vraddh_PP __builtin_HEXAGON_M2_vraddh

/* ==========================================================================
   Assembly Syntax:       Rd32=vradduh(Rss32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_vradduh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vradduh_PP __builtin_HEXAGON_M2_vradduh

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcmpyi(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyiacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyiacc_PP __builtin_HEXAGON_M2_vrcmaci_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcmpyi(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyiacc_PP_conj(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyiacc_PP_conj __builtin_HEXAGON_M2_vrcmaci_s0c

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcmpyr(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyracc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyracc_PP __builtin_HEXAGON_M2_vrcmacr_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcmpyr(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyracc_PP_conj(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyracc_PP_conj __builtin_HEXAGON_M2_vrcmacr_s0c

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcmpyi(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyi_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyi_PP __builtin_HEXAGON_M2_vrcmpyi_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcmpyi(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyi_PP_conj(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyi_PP_conj __builtin_HEXAGON_M2_vrcmpyi_s0c

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcmpyr(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyr_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyr_PP __builtin_HEXAGON_M2_vrcmpyr_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcmpyr(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_vrcmpyr_PP_conj(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcmpyr_PP_conj __builtin_HEXAGON_M2_vrcmpyr_s0c

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcmpys(Rss32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vrcmpysacc_PR_s1_sat(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_vrcmpysacc_PR_s1_sat __builtin_HEXAGON_M2_vrcmpys_acc_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcmpys(Rss32,Rt32):<<1:sat
   C Intrinsic Prototype: Word64 Q6_P_vrcmpys_PR_s1_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_vrcmpys_PR_s1_sat __builtin_HEXAGON_M2_vrcmpys_s1

/* ==========================================================================
   Assembly Syntax:       Rd32=vrcmpys(Rss32,Rt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vrcmpys_PR_s1_rnd_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vrcmpys_PR_s1_rnd_sat __builtin_HEXAGON_M2_vrcmpys_s1rp

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpyh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpyhacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpyhacc_PP __builtin_HEXAGON_M2_vrmac_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpyh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpyh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpyh_PP __builtin_HEXAGON_M2_vrmpy_s0

/* ==========================================================================
   Assembly Syntax:       Rx32^=xor(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_xorxacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_xorxacc_RR __builtin_HEXAGON_M2_xor_xacc

/* ==========================================================================
   Assembly Syntax:       Rx32&=and(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andand_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andand_RR __builtin_HEXAGON_M4_and_and

/* ==========================================================================
   Assembly Syntax:       Rx32&=and(Rs32,~Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andand_RnR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andand_RnR __builtin_HEXAGON_M4_and_andn

/* ==========================================================================
   Assembly Syntax:       Rx32&=or(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_orand_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_orand_RR __builtin_HEXAGON_M4_and_or

/* ==========================================================================
   Assembly Syntax:       Rx32&=xor(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_xorand_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_xorand_RR __builtin_HEXAGON_M4_and_xor

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiwh(Rss32,Rt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiwh_PR_s1_rnd_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpyiwh_PR_s1_rnd_sat __builtin_HEXAGON_M4_cmpyi_wh

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiwh(Rss32,Rt32*):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiwh_PR_conj_s1_rnd_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpyiwh_PR_conj_s1_rnd_sat __builtin_HEXAGON_M4_cmpyi_whc

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrwh(Rss32,Rt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrwh_PR_s1_rnd_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpyrwh_PR_s1_rnd_sat __builtin_HEXAGON_M4_cmpyr_wh

/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrwh(Rss32,Rt32*):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrwh_PR_conj_s1_rnd_sat(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cmpyrwh_PR_conj_s1_rnd_sat __builtin_HEXAGON_M4_cmpyr_whc

/* ==========================================================================
   Assembly Syntax:       Rx32+=mpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpyacc_RR_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyacc_RR_s1_sat __builtin_HEXAGON_M4_mac_up_s1_sat

/* ==========================================================================
   Assembly Syntax:       Rd32=add(#u6,mpyi(Rs32,#U6))
   C Intrinsic Prototype: Word32 Q6_R_add_mpyi_IRI(Word32 Iu6, Word32 Rs, Word32 IU6)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_mpyi_IRI __builtin_HEXAGON_M4_mpyri_addi

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Ru32,mpyi(Rs32,#u6))
   C Intrinsic Prototype: Word32 Q6_R_add_mpyi_RRI(Word32 Ru, Word32 Rs, Word32 Iu6)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_mpyi_RRI __builtin_HEXAGON_M4_mpyri_addr

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Ru32,mpyi(#u6:2,Rs32))
   C Intrinsic Prototype: Word32 Q6_R_add_mpyi_RIR(Word32 Ru, Word32 Iu6_2, Word32 Rs)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_mpyi_RIR __builtin_HEXAGON_M4_mpyri_addr_u2

/* ==========================================================================
   Assembly Syntax:       Rd32=add(#u6,mpyi(Rs32,Rt32))
   C Intrinsic Prototype: Word32 Q6_R_add_mpyi_IRR(Word32 Iu6, Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_mpyi_IRR __builtin_HEXAGON_M4_mpyrr_addi

/* ==========================================================================
   Assembly Syntax:       Ry32=add(Ru32,mpyi(Ry32,Rs32))
   C Intrinsic Prototype: Word32 Q6_R_add_mpyi_RRR(Word32 Ru, Word32 Ry, Word32 Rs)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_mpyi_RRR __builtin_HEXAGON_M4_mpyrr_addr

/* ==========================================================================
   Assembly Syntax:       Rx32-=mpy(Rs32,Rt32):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_mpynac_RR_s1_sat(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpynac_RR_s1_sat __builtin_HEXAGON_M4_nac_up_s1_sat

/* ==========================================================================
   Assembly Syntax:       Rx32|=and(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andor_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andor_RR __builtin_HEXAGON_M4_or_and

/* ==========================================================================
   Assembly Syntax:       Rx32|=and(Rs32,~Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andor_RnR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andor_RnR __builtin_HEXAGON_M4_or_andn

/* ==========================================================================
   Assembly Syntax:       Rx32|=or(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_oror_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_oror_RR __builtin_HEXAGON_M4_or_or

/* ==========================================================================
   Assembly Syntax:       Rx32|=xor(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_xoror_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_xoror_RR __builtin_HEXAGON_M4_or_xor

/* ==========================================================================
   Assembly Syntax:       Rdd32=pmpyw(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_pmpyw_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_pmpyw_RR __builtin_HEXAGON_M4_pmpyw

/* ==========================================================================
   Assembly Syntax:       Rxx32^=pmpyw(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_pmpywxacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_pmpywxacc_RR __builtin_HEXAGON_M4_pmpyw_acc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vpmpyh(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vpmpyh_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vpmpyh_RR __builtin_HEXAGON_M4_vpmpyh

/* ==========================================================================
   Assembly Syntax:       Rxx32^=vpmpyh(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vpmpyhxacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vpmpyhxacc_RR __builtin_HEXAGON_M4_vpmpyh_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpyweh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpywehacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywehacc_PP __builtin_HEXAGON_M4_vrmpyeh_acc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpyweh(Rss32,Rtt32):<<1
   C Intrinsic Prototype: Word64 Q6_P_vrmpywehacc_PP_s1(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywehacc_PP_s1 __builtin_HEXAGON_M4_vrmpyeh_acc_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpyweh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpyweh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpyweh_PP __builtin_HEXAGON_M4_vrmpyeh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpyweh(Rss32,Rtt32):<<1
   C Intrinsic Prototype: Word64 Q6_P_vrmpyweh_PP_s1(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpyweh_PP_s1 __builtin_HEXAGON_M4_vrmpyeh_s1

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpywoh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpywohacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywohacc_PP __builtin_HEXAGON_M4_vrmpyoh_acc_s0

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpywoh(Rss32,Rtt32):<<1
   C Intrinsic Prototype: Word64 Q6_P_vrmpywohacc_PP_s1(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywohacc_PP_s1 __builtin_HEXAGON_M4_vrmpyoh_acc_s1

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpywoh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpywoh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywoh_PP __builtin_HEXAGON_M4_vrmpyoh_s0

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpywoh(Rss32,Rtt32):<<1
   C Intrinsic Prototype: Word64 Q6_P_vrmpywoh_PP_s1(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpywoh_PP_s1 __builtin_HEXAGON_M4_vrmpyoh_s1

/* ==========================================================================
   Assembly Syntax:       Rx32^=and(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andxacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andxacc_RR __builtin_HEXAGON_M4_xor_and

/* ==========================================================================
   Assembly Syntax:       Rx32^=and(Rs32,~Rt32)
   C Intrinsic Prototype: Word32 Q6_R_andxacc_RnR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andxacc_RnR __builtin_HEXAGON_M4_xor_andn

/* ==========================================================================
   Assembly Syntax:       Rx32^=or(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_orxacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_orxacc_RR __builtin_HEXAGON_M4_xor_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=xor(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_xorxacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_xorxacc_PP __builtin_HEXAGON_M4_xor_xacc

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vdmpybsu(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpybsuacc_PP_sat(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpybsuacc_PP_sat __builtin_HEXAGON_M5_vdmacbsu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vdmpybsu(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vdmpybsu_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vdmpybsu_PP_sat __builtin_HEXAGON_M5_vdmpybsu

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpybsu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpybsuacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpybsuacc_RR __builtin_HEXAGON_M5_vmacbsu

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vmpybu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpybuacc_RR(Word64 Rxx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpybuacc_RR __builtin_HEXAGON_M5_vmacbuu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpybsu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpybsu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpybsu_RR __builtin_HEXAGON_M5_vmpybsu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vmpybu(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vmpybu_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vmpybu_RR __builtin_HEXAGON_M5_vmpybuu

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpybsu(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpybsuacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpybsuacc_PP __builtin_HEXAGON_M5_vrmacbsu

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrmpybu(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpybuacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpybuacc_PP __builtin_HEXAGON_M5_vrmacbuu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpybsu(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpybsu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpybsu_PP __builtin_HEXAGON_M5_vrmpybsu

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrmpybu(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vrmpybu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrmpybu_PP __builtin_HEXAGON_M5_vrmpybuu

/* ==========================================================================
   Assembly Syntax:       Rd32=addasl(Rt32,Rs32,#u3)
   C Intrinsic Prototype: Word32 Q6_R_addasl_RRI(Word32 Rt, Word32 Rs, Word32 Iu3)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_addasl_RRI __builtin_HEXAGON_S2_addasl_rrri

/* ==========================================================================
   Assembly Syntax:       Rdd32=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asl_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asl_PI __builtin_HEXAGON_S2_asl_i_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_aslacc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslacc_PI __builtin_HEXAGON_S2_asl_i_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asland_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asland_PI __builtin_HEXAGON_S2_asl_i_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_aslnac_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslnac_PI __builtin_HEXAGON_S2_asl_i_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_aslor_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslor_PI __builtin_HEXAGON_S2_asl_i_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=asl(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_aslxacc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslxacc_PI __builtin_HEXAGON_S2_asl_i_p_xacc

/* ==========================================================================
   Assembly Syntax:       Rd32=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asl_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asl_RI __builtin_HEXAGON_S2_asl_i_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_aslacc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslacc_RI __builtin_HEXAGON_S2_asl_i_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asland_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asland_RI __builtin_HEXAGON_S2_asl_i_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_aslnac_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslnac_RI __builtin_HEXAGON_S2_asl_i_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_aslor_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslor_RI __builtin_HEXAGON_S2_asl_i_r_or

/* ==========================================================================
   Assembly Syntax:       Rd32=asl(Rs32,#u5):sat
   C Intrinsic Prototype: Word32 Q6_R_asl_RI_sat(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asl_RI_sat __builtin_HEXAGON_S2_asl_i_r_sat

/* ==========================================================================
   Assembly Syntax:       Rx32^=asl(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_aslxacc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslxacc_RI __builtin_HEXAGON_S2_asl_i_r_xacc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaslh(Rss32,#u4)
   C Intrinsic Prototype: Word64 Q6_P_vaslh_PI(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaslh_PI __builtin_HEXAGON_S2_asl_i_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaslw(Rss32,#u5)
   C Intrinsic Prototype: Word64 Q6_P_vaslw_PI(Word64 Rss, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaslw_PI __builtin_HEXAGON_S2_asl_i_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asl_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asl_PR __builtin_HEXAGON_S2_asl_r_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_aslacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslacc_PR __builtin_HEXAGON_S2_asl_r_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asland_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asland_PR __builtin_HEXAGON_S2_asl_r_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_aslnac_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslnac_PR __builtin_HEXAGON_S2_asl_r_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_aslor_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslor_PR __builtin_HEXAGON_S2_asl_r_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=asl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_aslxacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_aslxacc_PR __builtin_HEXAGON_S2_asl_r_p_xor

/* ==========================================================================
   Assembly Syntax:       Rd32=asl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asl_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asl_RR __builtin_HEXAGON_S2_asl_r_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=asl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_aslacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslacc_RR __builtin_HEXAGON_S2_asl_r_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=asl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asland_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asland_RR __builtin_HEXAGON_S2_asl_r_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=asl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_aslnac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslnac_RR __builtin_HEXAGON_S2_asl_r_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=asl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_aslor_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_aslor_RR __builtin_HEXAGON_S2_asl_r_r_or

/* ==========================================================================
   Assembly Syntax:       Rd32=asl(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_asl_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asl_RR_sat __builtin_HEXAGON_S2_asl_r_r_sat

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaslh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vaslh_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaslh_PR __builtin_HEXAGON_S2_asl_r_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vaslw(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vaslw_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vaslw_PR __builtin_HEXAGON_S2_asl_r_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=asr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asr_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asr_PI __builtin_HEXAGON_S2_asr_i_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=asr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asracc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asracc_PI __builtin_HEXAGON_S2_asr_i_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=asr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asrand_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asrand_PI __builtin_HEXAGON_S2_asr_i_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=asr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asrnac_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asrnac_PI __builtin_HEXAGON_S2_asr_i_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=asr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asror_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asror_PI __builtin_HEXAGON_S2_asr_i_p_or

/* ==========================================================================
   Assembly Syntax:       Rdd32=asr(Rss32,#u6):rnd
   C Intrinsic Prototype: Word64 Q6_P_asr_PI_rnd(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asr_PI_rnd __builtin_HEXAGON_S2_asr_i_p_rnd

/* ==========================================================================
   Assembly Syntax:       Rdd32=asrrnd(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_asrrnd_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_asrrnd_PI __builtin_HEXAGON_S2_asr_i_p_rnd_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rd32=asr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asr_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asr_RI __builtin_HEXAGON_S2_asr_i_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=asr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asracc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asracc_RI __builtin_HEXAGON_S2_asr_i_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=asr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asrand_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asrand_RI __builtin_HEXAGON_S2_asr_i_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=asr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asrnac_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asrnac_RI __builtin_HEXAGON_S2_asr_i_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=asr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asror_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asror_RI __builtin_HEXAGON_S2_asr_i_r_or

/* ==========================================================================
   Assembly Syntax:       Rd32=asr(Rs32,#u5):rnd
   C Intrinsic Prototype: Word32 Q6_R_asr_RI_rnd(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asr_RI_rnd __builtin_HEXAGON_S2_asr_i_r_rnd

/* ==========================================================================
   Assembly Syntax:       Rd32=asrrnd(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_asrrnd_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_asrrnd_RI __builtin_HEXAGON_S2_asr_i_r_rnd_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rd32=vasrw(Rss32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_vasrw_PI(Word64 Rss, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vasrw_PI __builtin_HEXAGON_S2_asr_i_svw_trun

/* ==========================================================================
   Assembly Syntax:       Rdd32=vasrh(Rss32,#u4)
   C Intrinsic Prototype: Word64 Q6_P_vasrh_PI(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vasrh_PI __builtin_HEXAGON_S2_asr_i_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vasrw(Rss32,#u5)
   C Intrinsic Prototype: Word64 Q6_P_vasrw_PI(Word64 Rss, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vasrw_PI __builtin_HEXAGON_S2_asr_i_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asr_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asr_PR __builtin_HEXAGON_S2_asr_r_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asracc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asracc_PR __builtin_HEXAGON_S2_asr_r_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asrand_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asrand_PR __builtin_HEXAGON_S2_asr_r_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asrnac_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asrnac_PR __builtin_HEXAGON_S2_asr_r_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asror_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asror_PR __builtin_HEXAGON_S2_asr_r_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=asr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_asrxacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_asrxacc_PR __builtin_HEXAGON_S2_asr_r_p_xor

/* ==========================================================================
   Assembly Syntax:       Rd32=asr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asr_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asr_RR __builtin_HEXAGON_S2_asr_r_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=asr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asracc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asracc_RR __builtin_HEXAGON_S2_asr_r_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=asr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asrand_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asrand_RR __builtin_HEXAGON_S2_asr_r_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=asr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asrnac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asrnac_RR __builtin_HEXAGON_S2_asr_r_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=asr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_asror_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asror_RR __builtin_HEXAGON_S2_asr_r_r_or

/* ==========================================================================
   Assembly Syntax:       Rd32=asr(Rs32,Rt32):sat
   C Intrinsic Prototype: Word32 Q6_R_asr_RR_sat(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_asr_RR_sat __builtin_HEXAGON_S2_asr_r_r_sat

/* ==========================================================================
   Assembly Syntax:       Rd32=vasrw(Rss32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_vasrw_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vasrw_PR __builtin_HEXAGON_S2_asr_r_svw_trun

/* ==========================================================================
   Assembly Syntax:       Rdd32=vasrh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vasrh_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vasrh_PR __builtin_HEXAGON_S2_asr_r_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vasrw(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vasrw_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vasrw_PR __builtin_HEXAGON_S2_asr_r_vw

/* ==========================================================================
   Assembly Syntax:       Rd32=brev(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_brev_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_brev_R __builtin_HEXAGON_S2_brev

/* ==========================================================================
   Assembly Syntax:       Rdd32=brev(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_brev_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_brev_P __builtin_HEXAGON_S2_brevp

/* ==========================================================================
   Assembly Syntax:       Rd32=cl0(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_cl0_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cl0_R __builtin_HEXAGON_S2_cl0

/* ==========================================================================
   Assembly Syntax:       Rd32=cl0(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_cl0_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cl0_P __builtin_HEXAGON_S2_cl0p

/* ==========================================================================
   Assembly Syntax:       Rd32=cl1(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_cl1_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cl1_R __builtin_HEXAGON_S2_cl1

/* ==========================================================================
   Assembly Syntax:       Rd32=cl1(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_cl1_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_cl1_P __builtin_HEXAGON_S2_cl1p

/* ==========================================================================
   Assembly Syntax:       Rd32=clb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_clb_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_clb_R __builtin_HEXAGON_S2_clb

/* ==========================================================================
   Assembly Syntax:       Rd32=normamt(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_normamt_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_normamt_R __builtin_HEXAGON_S2_clbnorm

/* ==========================================================================
   Assembly Syntax:       Rd32=clb(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_clb_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_clb_P __builtin_HEXAGON_S2_clbp

/* ==========================================================================
   Assembly Syntax:       Rd32=clrbit(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_clrbit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_clrbit_RI __builtin_HEXAGON_S2_clrbit_i

/* ==========================================================================
   Assembly Syntax:       Rd32=clrbit(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_clrbit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_clrbit_RR __builtin_HEXAGON_S2_clrbit_r

/* ==========================================================================
   Assembly Syntax:       Rd32=ct0(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_ct0_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_ct0_R __builtin_HEXAGON_S2_ct0

/* ==========================================================================
   Assembly Syntax:       Rd32=ct0(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_ct0_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_ct0_P __builtin_HEXAGON_S2_ct0p

/* ==========================================================================
   Assembly Syntax:       Rd32=ct1(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_ct1_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_ct1_R __builtin_HEXAGON_S2_ct1

/* ==========================================================================
   Assembly Syntax:       Rd32=ct1(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_ct1_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_ct1_P __builtin_HEXAGON_S2_ct1p

/* ==========================================================================
   Assembly Syntax:       Rdd32=deinterleave(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_deinterleave_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_deinterleave_P __builtin_HEXAGON_S2_deinterleave

/* ==========================================================================
   Assembly Syntax:       Rd32=extractu(Rs32,#u5,#U5)
   C Intrinsic Prototype: Word32 Q6_R_extractu_RII(Word32 Rs, Word32 Iu5, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_extractu_RII __builtin_HEXAGON_S2_extractu

/* ==========================================================================
   Assembly Syntax:       Rd32=extractu(Rs32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_extractu_RP(Word32 Rs, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_extractu_RP __builtin_HEXAGON_S2_extractu_rp

/* ==========================================================================
   Assembly Syntax:       Rdd32=extractu(Rss32,#u6,#U6)
   C Intrinsic Prototype: Word64 Q6_P_extractu_PII(Word64 Rss, Word32 Iu6, Word32 IU6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_extractu_PII __builtin_HEXAGON_S2_extractup

/* ==========================================================================
   Assembly Syntax:       Rdd32=extractu(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_extractu_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_extractu_PP __builtin_HEXAGON_S2_extractup_rp

/* ==========================================================================
   Assembly Syntax:       Rx32=insert(Rs32,#u5,#U5)
   C Intrinsic Prototype: Word32 Q6_R_insert_RII(Word32 Rx, Word32 Rs, Word32 Iu5, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_insert_RII __builtin_HEXAGON_S2_insert

/* ==========================================================================
   Assembly Syntax:       Rx32=insert(Rs32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_insert_RP(Word32 Rx, Word32 Rs, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_insert_RP __builtin_HEXAGON_S2_insert_rp

/* ==========================================================================
   Assembly Syntax:       Rxx32=insert(Rss32,#u6,#U6)
   C Intrinsic Prototype: Word64 Q6_P_insert_PII(Word64 Rxx, Word64 Rss, Word32 Iu6, Word32 IU6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_insert_PII __builtin_HEXAGON_S2_insertp

/* ==========================================================================
   Assembly Syntax:       Rxx32=insert(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_insert_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_insert_PP __builtin_HEXAGON_S2_insertp_rp

/* ==========================================================================
   Assembly Syntax:       Rdd32=interleave(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_interleave_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_interleave_P __builtin_HEXAGON_S2_interleave

/* ==========================================================================
   Assembly Syntax:       Rdd32=lfs(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_lfs_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lfs_PP __builtin_HEXAGON_S2_lfsp

/* ==========================================================================
   Assembly Syntax:       Rdd32=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsl_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsl_PR __builtin_HEXAGON_S2_lsl_r_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lslacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lslacc_PR __builtin_HEXAGON_S2_lsl_r_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsland_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsland_PR __builtin_HEXAGON_S2_lsl_r_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lslnac_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lslnac_PR __builtin_HEXAGON_S2_lsl_r_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lslor_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lslor_PR __builtin_HEXAGON_S2_lsl_r_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=lsl(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lslxacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lslxacc_PR __builtin_HEXAGON_S2_lsl_r_p_xor

/* ==========================================================================
   Assembly Syntax:       Rd32=lsl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsl_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsl_RR __builtin_HEXAGON_S2_lsl_r_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=lsl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lslacc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lslacc_RR __builtin_HEXAGON_S2_lsl_r_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=lsl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsland_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsland_RR __builtin_HEXAGON_S2_lsl_r_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=lsl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lslnac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lslnac_RR __builtin_HEXAGON_S2_lsl_r_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=lsl(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lslor_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lslor_RR __builtin_HEXAGON_S2_lsl_r_r_or

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlslh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vlslh_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlslh_PR __builtin_HEXAGON_S2_lsl_r_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlslw(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vlslw_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlslw_PR __builtin_HEXAGON_S2_lsl_r_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsr_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsr_PI __builtin_HEXAGON_S2_lsr_i_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsracc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsracc_PI __builtin_HEXAGON_S2_lsr_i_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsrand_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrand_PI __builtin_HEXAGON_S2_lsr_i_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsrnac_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrnac_PI __builtin_HEXAGON_S2_lsr_i_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsror_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsror_PI __builtin_HEXAGON_S2_lsr_i_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=lsr(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_lsrxacc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrxacc_PI __builtin_HEXAGON_S2_lsr_i_p_xacc

/* ==========================================================================
   Assembly Syntax:       Rd32=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsr_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsr_RI __builtin_HEXAGON_S2_lsr_i_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsracc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsracc_RI __builtin_HEXAGON_S2_lsr_i_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsrand_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsrand_RI __builtin_HEXAGON_S2_lsr_i_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsrnac_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsrnac_RI __builtin_HEXAGON_S2_lsr_i_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsror_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsror_RI __builtin_HEXAGON_S2_lsr_i_r_or

/* ==========================================================================
   Assembly Syntax:       Rx32^=lsr(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_lsrxacc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsrxacc_RI __builtin_HEXAGON_S2_lsr_i_r_xacc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlsrh(Rss32,#u4)
   C Intrinsic Prototype: Word64 Q6_P_vlsrh_PI(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlsrh_PI __builtin_HEXAGON_S2_lsr_i_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlsrw(Rss32,#u5)
   C Intrinsic Prototype: Word64 Q6_P_vlsrw_PI(Word64 Rss, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlsrw_PI __builtin_HEXAGON_S2_lsr_i_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsr_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsr_PR __builtin_HEXAGON_S2_lsr_r_p

/* ==========================================================================
   Assembly Syntax:       Rxx32+=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsracc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsracc_PR __builtin_HEXAGON_S2_lsr_r_p_acc

/* ==========================================================================
   Assembly Syntax:       Rxx32&=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsrand_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrand_PR __builtin_HEXAGON_S2_lsr_r_p_and

/* ==========================================================================
   Assembly Syntax:       Rxx32-=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsrnac_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrnac_PR __builtin_HEXAGON_S2_lsr_r_p_nac

/* ==========================================================================
   Assembly Syntax:       Rxx32|=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsror_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsror_PR __builtin_HEXAGON_S2_lsr_r_p_or

/* ==========================================================================
   Assembly Syntax:       Rxx32^=lsr(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_lsrxacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_lsrxacc_PR __builtin_HEXAGON_S2_lsr_r_p_xor

/* ==========================================================================
   Assembly Syntax:       Rd32=lsr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsr_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsr_RR __builtin_HEXAGON_S2_lsr_r_r

/* ==========================================================================
   Assembly Syntax:       Rx32+=lsr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsracc_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsracc_RR __builtin_HEXAGON_S2_lsr_r_r_acc

/* ==========================================================================
   Assembly Syntax:       Rx32&=lsr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsrand_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsrand_RR __builtin_HEXAGON_S2_lsr_r_r_and

/* ==========================================================================
   Assembly Syntax:       Rx32-=lsr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsrnac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsrnac_RR __builtin_HEXAGON_S2_lsr_r_r_nac

/* ==========================================================================
   Assembly Syntax:       Rx32|=lsr(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsror_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsror_RR __builtin_HEXAGON_S2_lsr_r_r_or

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlsrh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vlsrh_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlsrh_PR __builtin_HEXAGON_S2_lsr_r_vh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vlsrw(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vlsrw_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vlsrw_PR __builtin_HEXAGON_S2_lsr_r_vw

/* ==========================================================================
   Assembly Syntax:       Rdd32=packhl(Rs32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_packhl_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU32_3op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_packhl_RR __builtin_HEXAGON_S2_packhl

/* ==========================================================================
   Assembly Syntax:       Rd32=parity(Rss32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_parity_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_parity_PP __builtin_HEXAGON_S2_parityp

/* ==========================================================================
   Assembly Syntax:       Rd32=setbit(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_setbit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_setbit_RI __builtin_HEXAGON_S2_setbit_i

/* ==========================================================================
   Assembly Syntax:       Rd32=setbit(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_setbit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_setbit_RR __builtin_HEXAGON_S2_setbit_r

/* ==========================================================================
   Assembly Syntax:       Rdd32=shuffeb(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_shuffeb_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_shuffeb_PP __builtin_HEXAGON_S2_shuffeb

/* ==========================================================================
   Assembly Syntax:       Rdd32=shuffeh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_shuffeh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_shuffeh_PP __builtin_HEXAGON_S2_shuffeh

/* ==========================================================================
   Assembly Syntax:       Rdd32=shuffob(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_shuffob_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_shuffob_PP __builtin_HEXAGON_S2_shuffob

/* ==========================================================================
   Assembly Syntax:       Rdd32=shuffoh(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_shuffoh_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_shuffoh_PP __builtin_HEXAGON_S2_shuffoh

/* ==========================================================================
   Assembly Syntax:       memb(Rx32++#s4:0:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memb_IMR_circ(void** Rx, Word32 Is4_0, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memb_IMR_circ __builtin_HEXAGON_S2_storerb_pci

/* ==========================================================================
   Assembly Syntax:       memb(Rx32++I:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memb_MR_circ(void** Rx, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memb_MR_circ __builtin_HEXAGON_S2_storerb_pcr

/* ==========================================================================
   Assembly Syntax:       memd(Rx32++#s4:3:circ(Mu2))=Rtt32
   C Intrinsic Prototype: void Q6_memd_IMP_circ(void** Rx, Word32 Is4_3, Word32 Mu, Word64 Rtt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memd_IMP_circ __builtin_HEXAGON_S2_storerd_pci

/* ==========================================================================
   Assembly Syntax:       memd(Rx32++I:circ(Mu2))=Rtt32
   C Intrinsic Prototype: void Q6_memd_MP_circ(void** Rx, Word32 Mu, Word64 Rtt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memd_MP_circ __builtin_HEXAGON_S2_storerd_pcr

/* ==========================================================================
   Assembly Syntax:       memh(Rx32++#s4:1:circ(Mu2))=Rt32.h
   C Intrinsic Prototype: void Q6_memh_IMRh_circ(void** Rx, Word32 Is4_1, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memh_IMRh_circ __builtin_HEXAGON_S2_storerf_pci

/* ==========================================================================
   Assembly Syntax:       memh(Rx32++I:circ(Mu2))=Rt32.h
   C Intrinsic Prototype: void Q6_memh_MRh_circ(void** Rx, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memh_MRh_circ __builtin_HEXAGON_S2_storerf_pcr

/* ==========================================================================
   Assembly Syntax:       memh(Rx32++#s4:1:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memh_IMR_circ(void** Rx, Word32 Is4_1, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memh_IMR_circ __builtin_HEXAGON_S2_storerh_pci

/* ==========================================================================
   Assembly Syntax:       memh(Rx32++I:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memh_MR_circ(void** Rx, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memh_MR_circ __builtin_HEXAGON_S2_storerh_pcr

/* ==========================================================================
   Assembly Syntax:       memw(Rx32++#s4:2:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memw_IMR_circ(void** Rx, Word32 Is4_2, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memw_IMR_circ __builtin_HEXAGON_S2_storeri_pci

/* ==========================================================================
   Assembly Syntax:       memw(Rx32++I:circ(Mu2))=Rt32
   C Intrinsic Prototype: void Q6_memw_MR_circ(void** Rx, Word32 Mu, Word32 Rt, void* BaseAddress)
   Instruction Type:      ST
   Execution Slots:       SLOT01
   ========================================================================== */

#define Q6_memw_MR_circ __builtin_HEXAGON_S2_storeri_pcr

/* ==========================================================================
   Assembly Syntax:       Rd32=vsathb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_vsathb_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsathb_R __builtin_HEXAGON_S2_svsathb

/* ==========================================================================
   Assembly Syntax:       Rd32=vsathub(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_vsathub_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsathub_R __builtin_HEXAGON_S2_svsathub

/* ==========================================================================
   Assembly Syntax:       Rx32=tableidxb(Rs32,#u4,#U5)
   C Intrinsic Prototype: Word32 Q6_R_tableidxb_RII(Word32 Rx, Word32 Rs, Word32 Iu4, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_tableidxb_RII __builtin_HEXAGON_S2_tableidxb_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rx32=tableidxd(Rs32,#u4,#U5)
   C Intrinsic Prototype: Word32 Q6_R_tableidxd_RII(Word32 Rx, Word32 Rs, Word32 Iu4, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_tableidxd_RII __builtin_HEXAGON_S2_tableidxd_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rx32=tableidxh(Rs32,#u4,#U5)
   C Intrinsic Prototype: Word32 Q6_R_tableidxh_RII(Word32 Rx, Word32 Rs, Word32 Iu4, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_tableidxh_RII __builtin_HEXAGON_S2_tableidxh_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rx32=tableidxw(Rs32,#u4,#U5)
   C Intrinsic Prototype: Word32 Q6_R_tableidxw_RII(Word32 Rx, Word32 Rs, Word32 Iu4, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_tableidxw_RII __builtin_HEXAGON_S2_tableidxw_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rd32=togglebit(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_togglebit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_togglebit_RI __builtin_HEXAGON_S2_togglebit_i

/* ==========================================================================
   Assembly Syntax:       Rd32=togglebit(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_togglebit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_togglebit_RR __builtin_HEXAGON_S2_togglebit_r

/* ==========================================================================
   Assembly Syntax:       Pd4=tstbit(Rs32,#u5)
   C Intrinsic Prototype: Byte Q6_p_tstbit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_tstbit_RI __builtin_HEXAGON_S2_tstbit_i

/* ==========================================================================
   Assembly Syntax:       Pd4=tstbit(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_tstbit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_tstbit_RR __builtin_HEXAGON_S2_tstbit_r

/* ==========================================================================
   Assembly Syntax:       Rdd32=valignb(Rtt32,Rss32,#u3)
   C Intrinsic Prototype: Word64 Q6_P_valignb_PPI(Word64 Rtt, Word64 Rss, Word32 Iu3)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_valignb_PPI __builtin_HEXAGON_S2_valignib

/* ==========================================================================
   Assembly Syntax:       Rdd32=valignb(Rtt32,Rss32,Pu4)
   C Intrinsic Prototype: Word64 Q6_P_valignb_PPp(Word64 Rtt, Word64 Rss, Byte Pu)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_valignb_PPp __builtin_HEXAGON_S2_valignrb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcnegh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vcnegh_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcnegh_PR __builtin_HEXAGON_S2_vcnegh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vcrotate(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vcrotate_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vcrotate_PR __builtin_HEXAGON_S2_vcrotate

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcnegh(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_vrcneghacc_PR(Word64 Rxx, Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcneghacc_PR __builtin_HEXAGON_S2_vrcnegh

/* ==========================================================================
   Assembly Syntax:       Rd32=vrndwh(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vrndwh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vrndwh_P __builtin_HEXAGON_S2_vrndpackwh

/* ==========================================================================
   Assembly Syntax:       Rd32=vrndwh(Rss32):sat
   C Intrinsic Prototype: Word32 Q6_R_vrndwh_P_sat(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vrndwh_P_sat __builtin_HEXAGON_S2_vrndpackwhs

/* ==========================================================================
   Assembly Syntax:       Rd32=vsathb(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vsathb_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsathb_P __builtin_HEXAGON_S2_vsathb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsathb(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsathb_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsathb_P __builtin_HEXAGON_S2_vsathb_nopack

/* ==========================================================================
   Assembly Syntax:       Rd32=vsathub(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vsathub_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsathub_P __builtin_HEXAGON_S2_vsathub

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsathub(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsathub_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsathub_P __builtin_HEXAGON_S2_vsathub_nopack

/* ==========================================================================
   Assembly Syntax:       Rd32=vsatwh(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vsatwh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsatwh_P __builtin_HEXAGON_S2_vsatwh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsatwh(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsatwh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsatwh_P __builtin_HEXAGON_S2_vsatwh_nopack

/* ==========================================================================
   Assembly Syntax:       Rd32=vsatwuh(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vsatwuh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsatwuh_P __builtin_HEXAGON_S2_vsatwuh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsatwuh(Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vsatwuh_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsatwuh_P __builtin_HEXAGON_S2_vsatwuh_nopack

/* ==========================================================================
   Assembly Syntax:       Rd32=vsplatb(Rs32)
   C Intrinsic Prototype: Word32 Q6_R_vsplatb_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vsplatb_R __builtin_HEXAGON_S2_vsplatrb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsplath(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vsplath_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsplath_R __builtin_HEXAGON_S2_vsplatrh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vspliceb(Rss32,Rtt32,#u3)
   C Intrinsic Prototype: Word64 Q6_P_vspliceb_PPI(Word64 Rss, Word64 Rtt, Word32 Iu3)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vspliceb_PPI __builtin_HEXAGON_S2_vspliceib

/* ==========================================================================
   Assembly Syntax:       Rdd32=vspliceb(Rss32,Rtt32,Pu4)
   C Intrinsic Prototype: Word64 Q6_P_vspliceb_PPp(Word64 Rss, Word64 Rtt, Byte Pu)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vspliceb_PPp __builtin_HEXAGON_S2_vsplicerb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsxtbh(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vsxtbh_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsxtbh_R __builtin_HEXAGON_S2_vsxtbh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vsxthw(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vsxthw_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsxthw_R __builtin_HEXAGON_S2_vsxthw

/* ==========================================================================
   Assembly Syntax:       Rd32=vtrunehb(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vtrunehb_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vtrunehb_P __builtin_HEXAGON_S2_vtrunehb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vtrunewh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vtrunewh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vtrunewh_PP __builtin_HEXAGON_S2_vtrunewh

/* ==========================================================================
   Assembly Syntax:       Rd32=vtrunohb(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_vtrunohb_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vtrunohb_P __builtin_HEXAGON_S2_vtrunohb

/* ==========================================================================
   Assembly Syntax:       Rdd32=vtrunowh(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vtrunowh_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vtrunowh_PP __builtin_HEXAGON_S2_vtrunowh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vzxtbh(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vzxtbh_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vzxtbh_R __builtin_HEXAGON_S2_vzxtbh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vzxthw(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vzxthw_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vzxthw_R __builtin_HEXAGON_S2_vzxthw

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rs32,add(Ru32,#s6))
   C Intrinsic Prototype: Word32 Q6_R_add_add_RRI(Word32 Rs, Word32 Ru, Word32 Is6)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_add_RRI __builtin_HEXAGON_S4_addaddi

/* ==========================================================================
   Assembly Syntax:       Rx32=add(#u8,asl(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_add_asl_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_asl_IRI __builtin_HEXAGON_S4_addi_asl_ri

/* ==========================================================================
   Assembly Syntax:       Rx32=add(#u8,lsr(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_add_lsr_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_lsr_IRI __builtin_HEXAGON_S4_addi_lsr_ri

/* ==========================================================================
   Assembly Syntax:       Rx32=and(#u8,asl(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_and_asl_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_and_asl_IRI __builtin_HEXAGON_S4_andi_asl_ri

/* ==========================================================================
   Assembly Syntax:       Rx32=and(#u8,lsr(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_and_lsr_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_and_lsr_IRI __builtin_HEXAGON_S4_andi_lsr_ri

/* ==========================================================================
   Assembly Syntax:       Rd32=add(clb(Rs32),#s6)
   C Intrinsic Prototype: Word32 Q6_R_add_clb_RI(Word32 Rs, Word32 Is6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_clb_RI __builtin_HEXAGON_S4_clbaddi

/* ==========================================================================
   Assembly Syntax:       Rd32=add(clb(Rss32),#s6)
   C Intrinsic Prototype: Word32 Q6_R_add_clb_PI(Word64 Rss, Word32 Is6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_clb_PI __builtin_HEXAGON_S4_clbpaddi

/* ==========================================================================
   Assembly Syntax:       Rd32=normamt(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_normamt_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_normamt_P __builtin_HEXAGON_S4_clbpnorm

/* ==========================================================================
   Assembly Syntax:       Rd32=extract(Rs32,#u5,#U5)
   C Intrinsic Prototype: Word32 Q6_R_extract_RII(Word32 Rs, Word32 Iu5, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_extract_RII __builtin_HEXAGON_S4_extract

/* ==========================================================================
   Assembly Syntax:       Rd32=extract(Rs32,Rtt32)
   C Intrinsic Prototype: Word32 Q6_R_extract_RP(Word32 Rs, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_extract_RP __builtin_HEXAGON_S4_extract_rp

/* ==========================================================================
   Assembly Syntax:       Rdd32=extract(Rss32,#u6,#U6)
   C Intrinsic Prototype: Word64 Q6_P_extract_PII(Word64 Rss, Word32 Iu6, Word32 IU6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_extract_PII __builtin_HEXAGON_S4_extractp

/* ==========================================================================
   Assembly Syntax:       Rdd32=extract(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_extract_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_extract_PP __builtin_HEXAGON_S4_extractp_rp

/* ==========================================================================
   Assembly Syntax:       Rd32=lsl(#s6,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_lsl_IR(Word32 Is6, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_lsl_IR __builtin_HEXAGON_S4_lsli

/* ==========================================================================
   Assembly Syntax:       Pd4=!tstbit(Rs32,#u5)
   C Intrinsic Prototype: Byte Q6_p_not_tstbit_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_tstbit_RI __builtin_HEXAGON_S4_ntstbit_i

/* ==========================================================================
   Assembly Syntax:       Pd4=!tstbit(Rs32,Rt32)
   C Intrinsic Prototype: Byte Q6_p_not_tstbit_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_tstbit_RR __builtin_HEXAGON_S4_ntstbit_r

/* ==========================================================================
   Assembly Syntax:       Rx32|=and(Rs32,#s10)
   C Intrinsic Prototype: Word32 Q6_R_andor_RI(Word32 Rx, Word32 Rs, Word32 Is10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_andor_RI __builtin_HEXAGON_S4_or_andi

/* ==========================================================================
   Assembly Syntax:       Rx32=or(Ru32,and(Rx32,#s10))
   C Intrinsic Prototype: Word32 Q6_R_or_and_RRI(Word32 Ru, Word32 Rx, Word32 Is10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_or_and_RRI __builtin_HEXAGON_S4_or_andix

/* ==========================================================================
   Assembly Syntax:       Rx32|=or(Rs32,#s10)
   C Intrinsic Prototype: Word32 Q6_R_oror_RI(Word32 Rx, Word32 Rs, Word32 Is10)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_oror_RI __builtin_HEXAGON_S4_or_ori

/* ==========================================================================
   Assembly Syntax:       Rx32=or(#u8,asl(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_or_asl_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_or_asl_IRI __builtin_HEXAGON_S4_ori_asl_ri

/* ==========================================================================
   Assembly Syntax:       Rx32=or(#u8,lsr(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_or_lsr_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_or_lsr_IRI __builtin_HEXAGON_S4_ori_lsr_ri

/* ==========================================================================
   Assembly Syntax:       Rd32=parity(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_parity_RR(Word32 Rs, Word32 Rt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_parity_RR __builtin_HEXAGON_S4_parity

/* ==========================================================================
   Assembly Syntax:       Rd32=add(Rs32,sub(#s6,Ru32))
   C Intrinsic Prototype: Word32 Q6_R_add_sub_RIR(Word32 Rs, Word32 Is6, Word32 Ru)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_add_sub_RIR __builtin_HEXAGON_S4_subaddi

/* ==========================================================================
   Assembly Syntax:       Rx32=sub(#u8,asl(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_sub_asl_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_asl_IRI __builtin_HEXAGON_S4_subi_asl_ri

/* ==========================================================================
   Assembly Syntax:       Rx32=sub(#u8,lsr(Rx32,#U5))
   C Intrinsic Prototype: Word32 Q6_R_sub_lsr_IRI(Word32 Iu8, Word32 Rx, Word32 IU5)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_sub_lsr_IRI __builtin_HEXAGON_S4_subi_lsr_ri

/* ==========================================================================
   Assembly Syntax:       Rdd32=vrcrotate(Rss32,Rt32,#u2)
   C Intrinsic Prototype: Word64 Q6_P_vrcrotate_PRI(Word64 Rss, Word32 Rt, Word32 Iu2)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcrotate_PRI __builtin_HEXAGON_S4_vrcrotate

/* ==========================================================================
   Assembly Syntax:       Rxx32+=vrcrotate(Rss32,Rt32,#u2)
   C Intrinsic Prototype: Word64 Q6_P_vrcrotateacc_PRI(Word64 Rxx, Word64 Rss, Word32 Rt, Word32 Iu2)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vrcrotateacc_PRI __builtin_HEXAGON_S4_vrcrotate_acc

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxaddsubh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vxaddsubh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxaddsubh_PP_sat __builtin_HEXAGON_S4_vxaddsubh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxaddsubh(Rss32,Rtt32):rnd:>>1:sat
   C Intrinsic Prototype: Word64 Q6_P_vxaddsubh_PP_rnd_rs1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxaddsubh_PP_rnd_rs1_sat __builtin_HEXAGON_S4_vxaddsubhr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxaddsubw(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vxaddsubw_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxaddsubw_PP_sat __builtin_HEXAGON_S4_vxaddsubw

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxsubaddh(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vxsubaddh_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxsubaddh_PP_sat __builtin_HEXAGON_S4_vxsubaddh

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxsubaddh(Rss32,Rtt32):rnd:>>1:sat
   C Intrinsic Prototype: Word64 Q6_P_vxsubaddh_PP_rnd_rs1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxsubaddh_PP_rnd_rs1_sat __builtin_HEXAGON_S4_vxsubaddhr

/* ==========================================================================
   Assembly Syntax:       Rdd32=vxsubaddw(Rss32,Rtt32):sat
   C Intrinsic Prototype: Word64 Q6_P_vxsubaddw_PP_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vxsubaddw_PP_sat __builtin_HEXAGON_S4_vxsubaddw

/* ==========================================================================
   Assembly Syntax:       Rd32=vasrhub(Rss32,#u4):rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_vasrhub_PI_rnd_sat(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_R_vasrhub_PI_rnd_sat __builtin_HEXAGON_S5_asrhub_rnd_sat_goodsyntax

/* ==========================================================================
   Assembly Syntax:       Rd32=vasrhub(Rss32,#u4):sat
   C Intrinsic Prototype: Word32 Q6_R_vasrhub_PI_sat(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_vasrhub_PI_sat __builtin_HEXAGON_S5_asrhub_sat

/* ==========================================================================
   Assembly Syntax:       Rd32=popcount(Rss32)
   C Intrinsic Prototype: Word32 Q6_R_popcount_P(Word64 Rss)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_popcount_P __builtin_HEXAGON_S5_popcountp

/* ==========================================================================
   Assembly Syntax:       Rdd32=vasrh(Rss32,#u4):rnd
   C Intrinsic Prototype: Word64 Q6_P_vasrh_PI_rnd(Word64 Rss, Word32 Iu4)
   Instruction Type:      S_2op
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_P_vasrh_PI_rnd __builtin_HEXAGON_S5_vasrhrnd_goodsyntax

/* ==========================================================================
   Assembly Syntax:       dccleana(Rs32)
   C Intrinsic Prototype: void Q6_dccleana_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dccleana_A __builtin_HEXAGON_Y2_dccleana

/* ==========================================================================
   Assembly Syntax:       dccleaninva(Rs32)
   C Intrinsic Prototype: void Q6_dccleaninva_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dccleaninva_A __builtin_HEXAGON_Y2_dccleaninva

/* ==========================================================================
   Assembly Syntax:       dcfetch(Rs32)
   C Intrinsic Prototype: void Q6_dcfetch_A(Address Rs)
   Instruction Type:      MAPPING
   Execution Slots:       SLOT0123
   ========================================================================== */

#define Q6_dcfetch_A __builtin_HEXAGON_Y2_dcfetch

/* ==========================================================================
   Assembly Syntax:       dcinva(Rs32)
   C Intrinsic Prototype: void Q6_dcinva_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dcinva_A __builtin_HEXAGON_Y2_dcinva

/* ==========================================================================
   Assembly Syntax:       dczeroa(Rs32)
   C Intrinsic Prototype: void Q6_dczeroa_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dczeroa_A __builtin_HEXAGON_Y2_dczeroa

/* ==========================================================================
   Assembly Syntax:       l2fetch(Rs32,Rt32)
   C Intrinsic Prototype: void Q6_l2fetch_AR(Address Rs, Word32 Rt)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_l2fetch_AR __builtin_HEXAGON_Y4_l2fetch

/* ==========================================================================
   Assembly Syntax:       l2fetch(Rs32,Rtt32)
   C Intrinsic Prototype: void Q6_l2fetch_AP(Address Rs, Word64 Rtt)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_l2fetch_AP __builtin_HEXAGON_Y5_l2fetch

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rdd32=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_rol_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_rol_PI __builtin_HEXAGON_S6_rol_i_p
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rxx32+=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_rolacc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_rolacc_PI __builtin_HEXAGON_S6_rol_i_p_acc
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rxx32&=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_roland_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_roland_PI __builtin_HEXAGON_S6_rol_i_p_and
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rxx32-=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_rolnac_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_rolnac_PI __builtin_HEXAGON_S6_rol_i_p_nac
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rxx32|=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_rolor_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_rolor_PI __builtin_HEXAGON_S6_rol_i_p_or
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rxx32^=rol(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_rolxacc_PI(Word64 Rxx, Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_rolxacc_PI __builtin_HEXAGON_S6_rol_i_p_xacc
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rd32=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_rol_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_rol_RI __builtin_HEXAGON_S6_rol_i_r
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rx32+=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_rolacc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_rolacc_RI __builtin_HEXAGON_S6_rol_i_r_acc
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rx32&=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_roland_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_roland_RI __builtin_HEXAGON_S6_rol_i_r_and
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rx32-=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_rolnac_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_rolnac_RI __builtin_HEXAGON_S6_rol_i_r_nac
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rx32|=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_rolor_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_rolor_RI __builtin_HEXAGON_S6_rol_i_r_or
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 60
/* ==========================================================================
   Assembly Syntax:       Rx32^=rol(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_rolxacc_RI(Word32 Rx, Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_rolxacc_RI __builtin_HEXAGON_S6_rol_i_r_xacc
#endif /* __HEXAGON_ARCH___ >= 60 */

#if __HEXAGON_ARCH__ >= 62
/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsdiffb(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsdiffb_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsdiffb_PP __builtin_HEXAGON_M6_vabsdiffb
#endif /* __HEXAGON_ARCH___ >= 62 */

#if __HEXAGON_ARCH__ >= 62
/* ==========================================================================
   Assembly Syntax:       Rdd32=vabsdiffub(Rtt32,Rss32)
   C Intrinsic Prototype: Word64 Q6_P_vabsdiffub_PP(Word64 Rtt, Word64 Rss)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vabsdiffub_PP __builtin_HEXAGON_M6_vabsdiffub
#endif /* __HEXAGON_ARCH___ >= 62 */

#if __HEXAGON_ARCH__ >= 62
/* ==========================================================================
   Assembly Syntax:       Rdd32=vsplatb(Rs32)
   C Intrinsic Prototype: Word64 Q6_P_vsplatb_R(Word32 Rs)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vsplatb_R __builtin_HEXAGON_S6_vsplatrbp
#endif /* __HEXAGON_ARCH___ >= 62 */

#if __HEXAGON_ARCH__ >= 62
/* ==========================================================================
   Assembly Syntax:       Rdd32=vtrunehb(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vtrunehb_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vtrunehb_PP __builtin_HEXAGON_S6_vtrunehb_ppp
#endif /* __HEXAGON_ARCH___ >= 62 */

#if __HEXAGON_ARCH__ >= 62
/* ==========================================================================
   Assembly Syntax:       Rdd32=vtrunohb(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vtrunohb_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vtrunohb_PP __builtin_HEXAGON_S6_vtrunohb_ppp
#endif /* __HEXAGON_ARCH___ >= 62 */

#if __HEXAGON_ARCH__ >= 65
/* ==========================================================================
   Assembly Syntax:       Pd4=!any8(vcmpb.eq(Rss32,Rtt32))
   C Intrinsic Prototype: Byte Q6_p_not_any8_vcmpb_eq_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      ALU64
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_p_not_any8_vcmpb_eq_PP __builtin_HEXAGON_A6_vcmpbeq_notany
#endif /* __HEXAGON_ARCH___ >= 65 */

#if __HEXAGON_ARCH__ >= 66
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfadd(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfadd_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfadd_PP __builtin_HEXAGON_F2_dfadd
#endif /* __HEXAGON_ARCH___ >= 66 */

#if __HEXAGON_ARCH__ >= 66
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfsub(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfsub_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfsub_PP __builtin_HEXAGON_F2_dfsub
#endif /* __HEXAGON_ARCH___ >= 66 */

#if __HEXAGON_ARCH__ >= 66
/* ==========================================================================
   Assembly Syntax:       Rx32-=mpyi(Rs32,Rt32)
   C Intrinsic Prototype: Word32 Q6_R_mpyinac_RR(Word32 Rx, Word32 Rs, Word32 Rt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mpyinac_RR __builtin_HEXAGON_M2_mnaci
#endif /* __HEXAGON_ARCH___ >= 66 */

#if __HEXAGON_ARCH__ >= 66
/* ==========================================================================
   Assembly Syntax:       Rd32=mask(#u5,#U5)
   C Intrinsic Prototype: Word32 Q6_R_mask_II(Word32 Iu5, Word32 IU5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_mask_II __builtin_HEXAGON_S2_mask
#endif /* __HEXAGON_ARCH___ >= 66 */

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=clip(Rs32,#u5)
   C Intrinsic Prototype: Word32 Q6_R_clip_RI(Word32 Rs, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_R_clip_RI __builtin_HEXAGON_A7_clip
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cround(Rss32,#u6)
   C Intrinsic Prototype: Word64 Q6_P_cround_PI(Word64 Rss, Word32 Iu6)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cround_PI __builtin_HEXAGON_A7_croundd_ri
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cround(Rss32,Rt32)
   C Intrinsic Prototype: Word64 Q6_P_cround_PR(Word64 Rss, Word32 Rt)
   Instruction Type:      S_3op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_cround_PR __builtin_HEXAGON_A7_croundd_rr
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=vclip(Rss32,#u5)
   C Intrinsic Prototype: Word64 Q6_P_vclip_PI(Word64 Rss, Word32 Iu5)
   Instruction Type:      S_2op
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_vclip_PI __builtin_HEXAGON_A7_vclip
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmax(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmax_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmax_PP __builtin_HEXAGON_F2_dfmax
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmin(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmin_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmin_PP __builtin_HEXAGON_F2_dfmin
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmpyfix(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmpyfix_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmpyfix_PP __builtin_HEXAGON_F2_dfmpyfix
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rxx32+=dfmpyhh(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmpyhhacc_PP(Float64 Rxx, Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmpyhhacc_PP __builtin_HEXAGON_F2_dfmpyhh
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rxx32+=dfmpylh(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmpylhacc_PP(Float64 Rxx, Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmpylhacc_PP __builtin_HEXAGON_F2_dfmpylh
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67
/* ==========================================================================
   Assembly Syntax:       Rdd32=dfmpyll(Rss32,Rtt32)
   C Intrinsic Prototype: Float64 Q6_P_dfmpyll_PP(Float64 Rss, Float64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT23
   ========================================================================== */

#define Q6_P_dfmpyll_PP __builtin_HEXAGON_F2_dfmpyll
#endif /* __HEXAGON_ARCH___ >= 67 */

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyiw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyiw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyiw_PP __builtin_HEXAGON_M7_dcmpyiw
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyiw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyiwacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyiwacc_PP __builtin_HEXAGON_M7_dcmpyiw_acc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyiw(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_cmpyiw_PP_conj(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyiw_PP_conj __builtin_HEXAGON_M7_dcmpyiwc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyiw(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_cmpyiwacc_PP_conj(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyiwacc_PP_conj __builtin_HEXAGON_M7_dcmpyiwc_acc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyrw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyrw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyrw_PP __builtin_HEXAGON_M7_dcmpyrw
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyrw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_cmpyrwacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyrwacc_PP __builtin_HEXAGON_M7_dcmpyrw_acc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=cmpyrw(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_cmpyrw_PP_conj(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyrw_PP_conj __builtin_HEXAGON_M7_dcmpyrwc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rxx32+=cmpyrw(Rss32,Rtt32*)
   C Intrinsic Prototype: Word64 Q6_P_cmpyrwacc_PP_conj(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_cmpyrwacc_PP_conj __builtin_HEXAGON_M7_dcmpyrwc_acc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rdd32=vdmpyw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vdmpyw_PP(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_vdmpyw_PP __builtin_HEXAGON_M7_vdmpy
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rxx32+=vdmpyw(Rss32,Rtt32)
   C Intrinsic Prototype: Word64 Q6_P_vdmpywacc_PP(Word64 Rxx, Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_P_vdmpywacc_PP __builtin_HEXAGON_M7_vdmpy_acc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiw(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiw_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyiw_PP_s1_sat __builtin_HEXAGON_M7_wcmpyiw
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiw(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiw_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyiw_PP_s1_rnd_sat __builtin_HEXAGON_M7_wcmpyiw_rnd
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiw(Rss32,Rtt32*):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiw_PP_conj_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyiw_PP_conj_s1_sat __builtin_HEXAGON_M7_wcmpyiwc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyiw(Rss32,Rtt32*):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyiw_PP_conj_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyiw_PP_conj_s1_rnd_sat __builtin_HEXAGON_M7_wcmpyiwc_rnd
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrw(Rss32,Rtt32):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrw_PP_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyrw_PP_s1_sat __builtin_HEXAGON_M7_wcmpyrw
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrw(Rss32,Rtt32):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrw_PP_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyrw_PP_s1_rnd_sat __builtin_HEXAGON_M7_wcmpyrw_rnd
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrw(Rss32,Rtt32*):<<1:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrw_PP_conj_s1_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyrw_PP_conj_s1_sat __builtin_HEXAGON_M7_wcmpyrwc
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 67 && defined __HEXAGON_AUDIO__
/* ==========================================================================
   Assembly Syntax:       Rd32=cmpyrw(Rss32,Rtt32*):<<1:rnd:sat
   C Intrinsic Prototype: Word32 Q6_R_cmpyrw_PP_conj_s1_rnd_sat(Word64 Rss, Word64 Rtt)
   Instruction Type:      M
   Execution Slots:       SLOT3
   ========================================================================== */

#define Q6_R_cmpyrw_PP_conj_s1_rnd_sat __builtin_HEXAGON_M7_wcmpyrwc_rnd
#endif /* __HEXAGON_ARCH___ >= 67  && defined __HEXAGON_AUDIO__*/

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       dmlink(Rs32,Rt32)
   C Intrinsic Prototype: void Q6_dmlink_AA(Address Rs, Address Rt)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dmlink_AA __builtin_HEXAGON_Y6_dmlink
#endif /* __HEXAGON_ARCH___ >= 68 */

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       Rd32=dmpause
   C Intrinsic Prototype: Word32 Q6_R_dmpause()
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_R_dmpause __builtin_HEXAGON_Y6_dmpause
#endif /* __HEXAGON_ARCH___ >= 68 */

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       Rd32=dmpoll
   C Intrinsic Prototype: Word32 Q6_R_dmpoll()
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_R_dmpoll __builtin_HEXAGON_Y6_dmpoll
#endif /* __HEXAGON_ARCH___ >= 68 */

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       dmresume(Rs32)
   C Intrinsic Prototype: void Q6_dmresume_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dmresume_A __builtin_HEXAGON_Y6_dmresume
#endif /* __HEXAGON_ARCH___ >= 68 */

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       dmstart(Rs32)
   C Intrinsic Prototype: void Q6_dmstart_A(Address Rs)
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_dmstart_A __builtin_HEXAGON_Y6_dmstart
#endif /* __HEXAGON_ARCH___ >= 68 */

#if __HEXAGON_ARCH__ >= 68
/* ==========================================================================
   Assembly Syntax:       Rd32=dmwait
   C Intrinsic Prototype: Word32 Q6_R_dmwait()
   Instruction Type:      ST
   Execution Slots:       SLOT0
   ========================================================================== */

#define Q6_R_dmwait __builtin_HEXAGON_Y6_dmwait
#endif /* __HEXAGON_ARCH___ >= 68 */

#include <hexagon_circ_brev_intrinsics.h>
#ifdef __HVX__
#include <hvx_hexagon_protos.h>
#endif /* __HVX__ */
#endif
