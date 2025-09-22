//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Automatically generated file, do not edit!
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPMASK_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPMASK_H

HexagonInstruction InstructionEncodings[] = {
{ /*Tag:A2_addi*/
  /*Rd32=add(Rs32,#s16)*/
  0xf0000000,
  0xb0000000,
  0x0fe03fe0,
  0 },
{ /*Tag:A2_andir*/
  /*Rd32=and(Rs32,#s10)*/
  0xffc00000,
  0x76000000,
  0x00203fe0,
  0 },
{ /*Tag:A2_combineii*/
  /*Rdd32=combine(#s8,#S8)*/
  0xff800000,
  0x7c000000,
  0x00001fe0,
  0 },
{ /*Tag:A2_orir*/
  /*Rd32=or(Rs32,#s10)*/
  0xffc00000,
  0x76800000,
  0x00203fe0,
  0 },
{ /*Tag:A2_paddif*/
  /*if (!Pu4) Rd32=add(Rs32,#s8)*/
  0xff802000,
  0x74800000,
  0x00001fe0,
  0 },
{ /*Tag:A2_paddifnew*/
  /*if (!Pu4.new) Rd32=add(Rs32,#s8)*/
  0xff802000,
  0x74802000,
  0x00001fe0,
  0 },
{ /*Tag:A2_paddit*/
  /*if (Pu4) Rd32=add(Rs32,#s8)*/
  0xff802000,
  0x74000000,
  0x00001fe0,
  0 },
{ /*Tag:A2_padditnew*/
  /*if (Pu4.new) Rd32=add(Rs32,#s8)*/
  0xff802000,
  0x74002000,
  0x00001fe0,
  0 },
{ /*Tag:A2_subri*/
  /*Rd32=sub(#s10,Rs32)*/
  0xffc00000,
  0x76400000,
  0x00203fe0,
  0 },
{ /*Tag:A2_tfrsi*/
  /*Rd32=#s16*/
  0xff000000,
  0x78000000,
  0x00df3fe0,
  0 },
{ /*Tag:A4_cmpbgtui*/
  /*Pd4=cmpb.gtu(Rs32,#u7)*/
  0xff601018,
  0xdd400000,
  0x00000fe0,
  0 },
{ /*Tag:A4_cmpheqi*/
  /*Pd4=cmph.eq(Rs32,#s8)*/
  0xff600018,
  0xdd000008,
  0x00001fe0,
  0 },
{ /*Tag:A4_cmphgti*/
  /*Pd4=cmph.gt(Rs32,#s8)*/
  0xff600018,
  0xdd200008,
  0x00001fe0,
  0 },
{ /*Tag:A4_cmphgtui*/
  /*Pd4=cmph.gtu(Rs32,#u7)*/
  0xff601018,
  0xdd400008,
  0x00000fe0,
  0 },
{ /*Tag:A4_combineii*/
  /*Rdd32=combine(#s8,#U6)*/
  0xff800000,
  0x7c800000,
  0x001f2000,
  0 },
{ /*Tag:A4_combineir*/
  /*Rdd32=combine(#s8,Rs32)*/
  0xff602000,
  0x73202000,
  0x00001fe0,
  0 },
{ /*Tag:A4_combineri*/
  /*Rdd32=combine(Rs32,#s8)*/
  0xff602000,
  0x73002000,
  0x00001fe0,
  0 },
{ /*Tag:A4_rcmpeqi*/
  /*Rd32=cmp.eq(Rs32,#s8)*/
  0xff602000,
  0x73402000,
  0x00001fe0,
  0 },
{ /*Tag:A4_rcmpneqi*/
  /*Rd32=!cmp.eq(Rs32,#s8)*/
  0xff602000,
  0x73602000,
  0x00001fe0,
  0 },
{ /*Tag:C2_cmoveif*/
  /*if (!Pu4) Rd32=#s12*/
  0xff902000,
  0x7e800000,
  0x000f1fe0,
  0 },
{ /*Tag:C2_cmoveit*/
  /*if (Pu4) Rd32=#s12*/
  0xff902000,
  0x7e000000,
  0x000f1fe0,
  0 },
{ /*Tag:C2_cmovenewif*/
  /*if (!Pu4.new) Rd32=#s12*/
  0xff902000,
  0x7e802000,
  0x000f1fe0,
  0 },
{ /*Tag:C2_cmovenewit*/
  /*if (Pu4.new) Rd32=#s12*/
  0xff902000,
  0x7e002000,
  0x000f1fe0,
  0 },
{ /*Tag:C2_cmpeqi*/
  /*Pd4=cmp.eq(Rs32,#s10)*/
  0xffc0001c,
  0x75000000,
  0x00203fe0,
  0 },
{ /*Tag:C2_cmpgti*/
  /*Pd4=cmp.gt(Rs32,#s10)*/
  0xffc0001c,
  0x75400000,
  0x00203fe0,
  0 },
{ /*Tag:C2_cmpgtui*/
  /*Pd4=cmp.gtu(Rs32,#u9)*/
  0xffe0001c,
  0x75800000,
  0x00003fe0,
  0 },
{ /*Tag:C2_muxii*/
  /*Rd32=mux(Pu4,#s8,#S8)*/
  0xfe000000,
  0x7a000000,
  0x00001fe0,
  0 },
{ /*Tag:C2_muxir*/
  /*Rd32=mux(Pu4,Rs32,#s8)*/
  0xff802000,
  0x73000000,
  0x00001fe0,
  0 },
{ /*Tag:C2_muxri*/
  /*Rd32=mux(Pu4,#s8,Rs32)*/
  0xff802000,
  0x73800000,
  0x00001fe0,
  0 },
{ /*Tag:C4_addipc*/
  /*Rd32=add(pc,#u6)*/
  0xffff0000,
  0x6a490000,
  0x00001f80,
  0 },
{ /*Tag:C4_cmpltei*/
  /*Pd4=!cmp.gt(Rs32,#s10)*/
  0xffc0001c,
  0x75400010,
  0x00203fe0,
  0 },
{ /*Tag:C4_cmplteui*/
  /*Pd4=!cmp.gtu(Rs32,#u9)*/
  0xffe0001c,
  0x75800010,
  0x00003fe0,
  0 },
{ /*Tag:C4_cmpneqi*/
  /*Pd4=!cmp.eq(Rs32,#s10)*/
  0xffc0001c,
  0x75000010,
  0x00203fe0,
  0 },
{ /*Tag:J2_call*/
  /*call #r22:2*/
  0xfe000001,
  0x5a000000,
  0x01ff3ffe,
  0 },
{ /*Tag:J2_callf*/
  /*if (!Pu4) call #r15:2*/
  0xff200800,
  0x5d200000,
  0x00df20fe,
  0 },
{ /*Tag:J2_callt*/
  /*if (Pu4) call #r15:2*/
  0xff200800,
  0x5d000000,
  0x00df20fe,
  0 },
{ /*Tag:J2_jump*/
  /*jump #r22:2*/
  0xfe000000,
  0x58000000,
  0x01ff3ffe,
  0 },
{ /*Tag:J2_jumpf*/
  /*if (!Pu4) jump:nt #r15:2*/
  0xff201800,
  0x5c200000,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumpfnew*/
  /*if (!Pu4.new) jump:nt #r15:2*/
  0xff201800,
  0x5c200800,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumpfnewpt*/
  /*if (!Pu4.new) jump:t #r15:2*/
  0xff201800,
  0x5c201800,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumpfpt*/
  /*if (!Pu4) jump:t #r15:2*/
  0xff201800,
  0x5c201000,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumpt*/
  /*if (Pu4) jump:nt #r15:2*/
  0xff201800,
  0x5c000000,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumptnew*/
  /*if (Pu4.new) jump:nt #r15:2*/
  0xff201800,
  0x5c000800,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumptnewpt*/
  /*if (Pu4.new) jump:t #r15:2*/
  0xff201800,
  0x5c001800,
  0x00df20fe,
  0 },
{ /*Tag:J2_jumptpt*/
  /*if (Pu4) jump:t #r15:2*/
  0xff201800,
  0x5c001000,
  0x00df20fe,
  0 },
{ /*Tag:J2_loop0i*/
  /*loop0(#r7:2,#U10)*/
  0xffe00000,
  0x69000000,
  0x00001f18,
  0 },
{ /*Tag:J2_loop0r*/
  /*loop0(#r7:2,Rs32)*/
  0xffe00000,
  0x60000000,
  0x00001f18,
  0 },
{ /*Tag:J2_loop1i*/
  /*loop1(#r7:2,#U10)*/
  0xffe00000,
  0x69200000,
  0x00001f18,
  0 },
{ /*Tag:J2_loop1r*/
  /*loop1(#r7:2,Rs32)*/
  0xffe00000,
  0x60200000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop1si*/
  /*p3=sp1loop0(#r7:2,#U10)*/
  0xffe00000,
  0x69a00000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop1sr*/
  /*p3=sp1loop0(#r7:2,Rs32)*/
  0xffe00000,
  0x60a00000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop2si*/
  /*p3=sp2loop0(#r7:2,#U10)*/
  0xffe00000,
  0x69c00000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop2sr*/
  /*p3=sp2loop0(#r7:2,Rs32)*/
  0xffe00000,
  0x60c00000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop3si*/
  /*p3=sp3loop0(#r7:2,#U10)*/
  0xffe00000,
  0x69e00000,
  0x00001f18,
  0 },
{ /*Tag:J2_ploop3sr*/
  /*p3=sp3loop0(#r7:2,Rs32)*/
  0xffe00000,
  0x60e00000,
  0x00001f18,
  0 },
{ /*Tag:J4_cmpeq_f_jumpnv_nt*/
  /*if (!cmp.eq(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x20400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_f_jumpnv_t*/
  /*if (!cmp.eq(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x20402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_fp0_jump_nt*/
  /*p0=cmp.eq(Rs16,Rt16); if (!p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x14400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_fp0_jump_t*/
  /*p0=cmp.eq(Rs16,Rt16); if (!p0.new) jump:t #r9:2*/
  0xffc03000,
  0x14402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_fp1_jump_nt*/
  /*p1=cmp.eq(Rs16,Rt16); if (!p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x14401000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_fp1_jump_t*/
  /*p1=cmp.eq(Rs16,Rt16); if (!p1.new) jump:t #r9:2*/
  0xffc03000,
  0x14403000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_t_jumpnv_nt*/
  /*if (cmp.eq(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x20000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_t_jumpnv_t*/
  /*if (cmp.eq(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x20002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_tp0_jump_nt*/
  /*p0=cmp.eq(Rs16,Rt16); if (p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x14000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_tp0_jump_t*/
  /*p0=cmp.eq(Rs16,Rt16); if (p0.new) jump:t #r9:2*/
  0xffc03000,
  0x14002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_tp1_jump_nt*/
  /*p1=cmp.eq(Rs16,Rt16); if (p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x14001000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeq_tp1_jump_t*/
  /*p1=cmp.eq(Rs16,Rt16); if (p1.new) jump:t #r9:2*/
  0xffc03000,
  0x14003000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_f_jumpnv_nt*/
  /*if (!cmp.eq(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x24400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_f_jumpnv_t*/
  /*if (!cmp.eq(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x24402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_fp0_jump_nt*/
  /*p0=cmp.eq(Rs16,#U5); if (!p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x10400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_fp0_jump_t*/
  /*p0=cmp.eq(Rs16,#U5); if (!p0.new) jump:t #r9:2*/
  0xffc02000,
  0x10402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_fp1_jump_nt*/
  /*p1=cmp.eq(Rs16,#U5); if (!p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x12400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_fp1_jump_t*/
  /*p1=cmp.eq(Rs16,#U5); if (!p1.new) jump:t #r9:2*/
  0xffc02000,
  0x12402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_t_jumpnv_nt*/
  /*if (cmp.eq(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x24000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_t_jumpnv_t*/
  /*if (cmp.eq(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x24002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_tp0_jump_nt*/
  /*p0=cmp.eq(Rs16,#U5); if (p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x10000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_tp0_jump_t*/
  /*p0=cmp.eq(Rs16,#U5); if (p0.new) jump:t #r9:2*/
  0xffc02000,
  0x10002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_tp1_jump_nt*/
  /*p1=cmp.eq(Rs16,#U5); if (p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x12000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqi_tp1_jump_t*/
  /*p1=cmp.eq(Rs16,#U5); if (p1.new) jump:t #r9:2*/
  0xffc02000,
  0x12002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_f_jumpnv_nt*/
  /*if (!cmp.eq(Ns8.new,#-1)) jump:nt #r9:2*/
  0xffc02000,
  0x26400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_f_jumpnv_t*/
  /*if (!cmp.eq(Ns8.new,#-1)) jump:t #r9:2*/
  0xffc02000,
  0x26402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_fp0_jump_nt*/
  /*p0=cmp.eq(Rs16,#-1); if (!p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_fp0_jump_t*/
  /*p0=cmp.eq(Rs16,#-1); if (!p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_fp1_jump_nt*/
  /*p1=cmp.eq(Rs16,#-1); if (!p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_fp1_jump_t*/
  /*p1=cmp.eq(Rs16,#-1); if (!p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_t_jumpnv_nt*/
  /*if (cmp.eq(Ns8.new,#-1)) jump:nt #r9:2*/
  0xffc02000,
  0x26000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_t_jumpnv_t*/
  /*if (cmp.eq(Ns8.new,#-1)) jump:t #r9:2*/
  0xffc02000,
  0x26002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_tp0_jump_nt*/
  /*p0=cmp.eq(Rs16,#-1); if (p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_tp0_jump_t*/
  /*p0=cmp.eq(Rs16,#-1); if (p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_tp1_jump_nt*/
  /*p1=cmp.eq(Rs16,#-1); if (p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpeqn1_tp1_jump_t*/
  /*p1=cmp.eq(Rs16,#-1); if (p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_f_jumpnv_nt*/
  /*if (!cmp.gt(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x20c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_f_jumpnv_t*/
  /*if (!cmp.gt(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x20c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_fp0_jump_nt*/
  /*p0=cmp.gt(Rs16,Rt16); if (!p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x14c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_fp0_jump_t*/
  /*p0=cmp.gt(Rs16,Rt16); if (!p0.new) jump:t #r9:2*/
  0xffc03000,
  0x14c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_fp1_jump_nt*/
  /*p1=cmp.gt(Rs16,Rt16); if (!p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x14c01000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_fp1_jump_t*/
  /*p1=cmp.gt(Rs16,Rt16); if (!p1.new) jump:t #r9:2*/
  0xffc03000,
  0x14c03000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_t_jumpnv_nt*/
  /*if (cmp.gt(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x20800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_t_jumpnv_t*/
  /*if (cmp.gt(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x20802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_tp0_jump_nt*/
  /*p0=cmp.gt(Rs16,Rt16); if (p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x14800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_tp0_jump_t*/
  /*p0=cmp.gt(Rs16,Rt16); if (p0.new) jump:t #r9:2*/
  0xffc03000,
  0x14802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_tp1_jump_nt*/
  /*p1=cmp.gt(Rs16,Rt16); if (p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x14801000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgt_tp1_jump_t*/
  /*p1=cmp.gt(Rs16,Rt16); if (p1.new) jump:t #r9:2*/
  0xffc03000,
  0x14803000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_f_jumpnv_nt*/
  /*if (!cmp.gt(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x24c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_f_jumpnv_t*/
  /*if (!cmp.gt(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x24c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_fp0_jump_nt*/
  /*p0=cmp.gt(Rs16,#U5); if (!p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x10c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_fp0_jump_t*/
  /*p0=cmp.gt(Rs16,#U5); if (!p0.new) jump:t #r9:2*/
  0xffc02000,
  0x10c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_fp1_jump_nt*/
  /*p1=cmp.gt(Rs16,#U5); if (!p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x12c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_fp1_jump_t*/
  /*p1=cmp.gt(Rs16,#U5); if (!p1.new) jump:t #r9:2*/
  0xffc02000,
  0x12c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_t_jumpnv_nt*/
  /*if (cmp.gt(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x24800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_t_jumpnv_t*/
  /*if (cmp.gt(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x24802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_tp0_jump_nt*/
  /*p0=cmp.gt(Rs16,#U5); if (p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x10800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_tp0_jump_t*/
  /*p0=cmp.gt(Rs16,#U5); if (p0.new) jump:t #r9:2*/
  0xffc02000,
  0x10802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_tp1_jump_nt*/
  /*p1=cmp.gt(Rs16,#U5); if (p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x12800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgti_tp1_jump_t*/
  /*p1=cmp.gt(Rs16,#U5); if (p1.new) jump:t #r9:2*/
  0xffc02000,
  0x12802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_f_jumpnv_nt*/
  /*if (!cmp.gt(Ns8.new,#-1)) jump:nt #r9:2*/
  0xffc02000,
  0x26c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_f_jumpnv_t*/
  /*if (!cmp.gt(Ns8.new,#-1)) jump:t #r9:2*/
  0xffc02000,
  0x26c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_fp0_jump_nt*/
  /*p0=cmp.gt(Rs16,#-1); if (!p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11c00100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_fp0_jump_t*/
  /*p0=cmp.gt(Rs16,#-1); if (!p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11c02100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_fp1_jump_nt*/
  /*p1=cmp.gt(Rs16,#-1); if (!p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13c00100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_fp1_jump_t*/
  /*p1=cmp.gt(Rs16,#-1); if (!p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13c02100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_t_jumpnv_nt*/
  /*if (cmp.gt(Ns8.new,#-1)) jump:nt #r9:2*/
  0xffc02000,
  0x26800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_t_jumpnv_t*/
  /*if (cmp.gt(Ns8.new,#-1)) jump:t #r9:2*/
  0xffc02000,
  0x26802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_tp0_jump_nt*/
  /*p0=cmp.gt(Rs16,#-1); if (p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11800100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_tp0_jump_t*/
  /*p0=cmp.gt(Rs16,#-1); if (p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11802100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_tp1_jump_nt*/
  /*p1=cmp.gt(Rs16,#-1); if (p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13800100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtn1_tp1_jump_t*/
  /*p1=cmp.gt(Rs16,#-1); if (p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13802100,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_f_jumpnv_nt*/
  /*if (!cmp.gtu(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x21400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_f_jumpnv_t*/
  /*if (!cmp.gtu(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x21402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_fp0_jump_nt*/
  /*p0=cmp.gtu(Rs16,Rt16); if (!p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x15400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_fp0_jump_t*/
  /*p0=cmp.gtu(Rs16,Rt16); if (!p0.new) jump:t #r9:2*/
  0xffc03000,
  0x15402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_fp1_jump_nt*/
  /*p1=cmp.gtu(Rs16,Rt16); if (!p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x15401000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_fp1_jump_t*/
  /*p1=cmp.gtu(Rs16,Rt16); if (!p1.new) jump:t #r9:2*/
  0xffc03000,
  0x15403000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_t_jumpnv_nt*/
  /*if (cmp.gtu(Ns8.new,Rt32)) jump:nt #r9:2*/
  0xffc02000,
  0x21000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_t_jumpnv_t*/
  /*if (cmp.gtu(Ns8.new,Rt32)) jump:t #r9:2*/
  0xffc02000,
  0x21002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_tp0_jump_nt*/
  /*p0=cmp.gtu(Rs16,Rt16); if (p0.new) jump:nt #r9:2*/
  0xffc03000,
  0x15000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_tp0_jump_t*/
  /*p0=cmp.gtu(Rs16,Rt16); if (p0.new) jump:t #r9:2*/
  0xffc03000,
  0x15002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_tp1_jump_nt*/
  /*p1=cmp.gtu(Rs16,Rt16); if (p1.new) jump:nt #r9:2*/
  0xffc03000,
  0x15001000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtu_tp1_jump_t*/
  /*p1=cmp.gtu(Rs16,Rt16); if (p1.new) jump:t #r9:2*/
  0xffc03000,
  0x15003000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_f_jumpnv_nt*/
  /*if (!cmp.gtu(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x25400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_f_jumpnv_t*/
  /*if (!cmp.gtu(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x25402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_fp0_jump_nt*/
  /*p0=cmp.gtu(Rs16,#U5); if (!p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x11400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_fp0_jump_t*/
  /*p0=cmp.gtu(Rs16,#U5); if (!p0.new) jump:t #r9:2*/
  0xffc02000,
  0x11402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_fp1_jump_nt*/
  /*p1=cmp.gtu(Rs16,#U5); if (!p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x13400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_fp1_jump_t*/
  /*p1=cmp.gtu(Rs16,#U5); if (!p1.new) jump:t #r9:2*/
  0xffc02000,
  0x13402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_t_jumpnv_nt*/
  /*if (cmp.gtu(Ns8.new,#U5)) jump:nt #r9:2*/
  0xffc02000,
  0x25000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_t_jumpnv_t*/
  /*if (cmp.gtu(Ns8.new,#U5)) jump:t #r9:2*/
  0xffc02000,
  0x25002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_tp0_jump_nt*/
  /*p0=cmp.gtu(Rs16,#U5); if (p0.new) jump:nt #r9:2*/
  0xffc02000,
  0x11000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_tp0_jump_t*/
  /*p0=cmp.gtu(Rs16,#U5); if (p0.new) jump:t #r9:2*/
  0xffc02000,
  0x11002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_tp1_jump_nt*/
  /*p1=cmp.gtu(Rs16,#U5); if (p1.new) jump:nt #r9:2*/
  0xffc02000,
  0x13000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpgtui_tp1_jump_t*/
  /*p1=cmp.gtu(Rs16,#U5); if (p1.new) jump:t #r9:2*/
  0xffc02000,
  0x13002000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmplt_f_jumpnv_nt*/
  /*if (!cmp.gt(Rt32,Ns8.new)) jump:nt #r9:2*/
  0xffc02000,
  0x21c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmplt_f_jumpnv_t*/
  /*if (!cmp.gt(Rt32,Ns8.new)) jump:t #r9:2*/
  0xffc02000,
  0x21c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmplt_t_jumpnv_nt*/
  /*if (cmp.gt(Rt32,Ns8.new)) jump:nt #r9:2*/
  0xffc02000,
  0x21800000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmplt_t_jumpnv_t*/
  /*if (cmp.gt(Rt32,Ns8.new)) jump:t #r9:2*/
  0xffc02000,
  0x21802000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpltu_f_jumpnv_nt*/
  /*if (!cmp.gtu(Rt32,Ns8.new)) jump:nt #r9:2*/
  0xffc02000,
  0x22400000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpltu_f_jumpnv_t*/
  /*if (!cmp.gtu(Rt32,Ns8.new)) jump:t #r9:2*/
  0xffc02000,
  0x22402000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpltu_t_jumpnv_nt*/
  /*if (cmp.gtu(Rt32,Ns8.new)) jump:nt #r9:2*/
  0xffc02000,
  0x22000000,
  0x003000fe,
  0 },
{ /*Tag:J4_cmpltu_t_jumpnv_t*/
  /*if (cmp.gtu(Rt32,Ns8.new)) jump:t #r9:2*/
  0xffc02000,
  0x22002000,
  0x003000fe,
  0 },
{ /*Tag:J4_jumpseti*/
  /*Rd16=#U6 ; jump #r9:2*/
  0xff000000,
  0x16000000,
  0x003000fe,
  0 },
{ /*Tag:J4_jumpsetr*/
  /*Rd16=Rs16 ; jump #r9:2*/
  0xff000000,
  0x17000000,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_f_jumpnv_nt*/
  /*if (!tstbit(Ns8.new,#0)) jump:nt #r9:2*/
  0xffc02000,
  0x25c00000,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_f_jumpnv_t*/
  /*if (!tstbit(Ns8.new,#0)) jump:t #r9:2*/
  0xffc02000,
  0x25c02000,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_fp0_jump_nt*/
  /*p0=tstbit(Rs16,#0); if (!p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11c00300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_fp0_jump_t*/
  /*p0=tstbit(Rs16,#0); if (!p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11c02300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_fp1_jump_nt*/
  /*p1=tstbit(Rs16,#0); if (!p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13c00300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_fp1_jump_t*/
  /*p1=tstbit(Rs16,#0); if (!p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13c02300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_t_jumpnv_nt*/
  /*if (tstbit(Ns8.new,#0)) jump:nt #r9:2*/
  0xffc02000,
  0x25800000,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_t_jumpnv_t*/
  /*if (tstbit(Ns8.new,#0)) jump:t #r9:2*/
  0xffc02000,
  0x25802000,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_tp0_jump_nt*/
  /*p0=tstbit(Rs16,#0); if (p0.new) jump:nt #r9:2*/
  0xffc02300,
  0x11800300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_tp0_jump_t*/
  /*p0=tstbit(Rs16,#0); if (p0.new) jump:t #r9:2*/
  0xffc02300,
  0x11802300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_tp1_jump_nt*/
  /*p1=tstbit(Rs16,#0); if (p1.new) jump:nt #r9:2*/
  0xffc02300,
  0x13800300,
  0x003000fe,
  0 },
{ /*Tag:J4_tstbit0_tp1_jump_t*/
  /*p1=tstbit(Rs16,#0); if (p1.new) jump:t #r9:2*/
  0xffc02300,
  0x13802300,
  0x003000fe,
  0 },
{ /*Tag:L2_loadalignb_io*/
  /*Ryy32=memb_fifo(Rs32+#s11:0)*/
  0xf9e00000,
  0x90800000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadalignh_io*/
  /*Ryy32=memh_fifo(Rs32+#s11:1)*/
  0xf9e00000,
  0x90400000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadbsw2_io*/
  /*Rd32=membh(Rs32+#s11:1)*/
  0xf9e00000,
  0x90200000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadbsw4_io*/
  /*Rdd32=membh(Rs32+#s11:2)*/
  0xf9e00000,
  0x90e00000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadbzw2_io*/
  /*Rd32=memubh(Rs32+#s11:1)*/
  0xf9e00000,
  0x90600000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadbzw4_io*/
  /*Rdd32=memubh(Rs32+#s11:2)*/
  0xf9e00000,
  0x90a00000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrb_io*/
  /*Rd32=memb(Rs32+#s11:0)*/
  0xf9e00000,
  0x91000000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrbgp*/
  /*Rd32=memb(gp+#u16:0)*/
  0xf9e00000,
  0x49000000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_loadrd_io*/
  /*Rdd32=memd(Rs32+#s11:3)*/
  0xf9e00000,
  0x91c00000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrdgp*/
  /*Rdd32=memd(gp+#u16:3)*/
  0xf9e00000,
  0x49c00000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_loadrh_io*/
  /*Rd32=memh(Rs32+#s11:1)*/
  0xf9e00000,
  0x91400000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrhgp*/
  /*Rd32=memh(gp+#u16:1)*/
  0xf9e00000,
  0x49400000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_loadri_io*/
  /*Rd32=memw(Rs32+#s11:2)*/
  0xf9e00000,
  0x91800000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrigp*/
  /*Rd32=memw(gp+#u16:2)*/
  0xf9e00000,
  0x49800000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_loadrub_io*/
  /*Rd32=memub(Rs32+#s11:0)*/
  0xf9e00000,
  0x91200000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadrubgp*/
  /*Rd32=memub(gp+#u16:0)*/
  0xf9e00000,
  0x49200000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_loadruh_io*/
  /*Rd32=memuh(Rs32+#s11:1)*/
  0xf9e00000,
  0x91600000,
  0x06003fe0,
  0 },
{ /*Tag:L2_loadruhgp*/
  /*Rd32=memuh(gp+#u16:1)*/
  0xf9e00000,
  0x49600000,
  0x061f3fe0,
  0 },
{ /*Tag:L2_ploadrbf_io*/
  /*if (!Pt4) Rd32=memb(Rs32+#u6:0)*/
  0xffe02000,
  0x45000000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrbfnew_io*/
  /*if (!Pt4.new) Rd32=memb(Rs32+#u6:0)*/
  0xffe02000,
  0x47000000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrbt_io*/
  /*if (Pt4) Rd32=memb(Rs32+#u6:0)*/
  0xffe02000,
  0x41000000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrbtnew_io*/
  /*if (Pt4.new) Rd32=memb(Rs32+#u6:0)*/
  0xffe02000,
  0x43000000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrdf_io*/
  /*if (!Pt4) Rdd32=memd(Rs32+#u6:3)*/
  0xffe02000,
  0x45c00000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrdfnew_io*/
  /*if (!Pt4.new) Rdd32=memd(Rs32+#u6:3)*/
  0xffe02000,
  0x47c00000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrdt_io*/
  /*if (Pt4) Rdd32=memd(Rs32+#u6:3)*/
  0xffe02000,
  0x41c00000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrdtnew_io*/
  /*if (Pt4.new) Rdd32=memd(Rs32+#u6:3)*/
  0xffe02000,
  0x43c00000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrhf_io*/
  /*if (!Pt4) Rd32=memh(Rs32+#u6:1)*/
  0xffe02000,
  0x45400000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrhfnew_io*/
  /*if (!Pt4.new) Rd32=memh(Rs32+#u6:1)*/
  0xffe02000,
  0x47400000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrht_io*/
  /*if (Pt4) Rd32=memh(Rs32+#u6:1)*/
  0xffe02000,
  0x41400000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrhtnew_io*/
  /*if (Pt4.new) Rd32=memh(Rs32+#u6:1)*/
  0xffe02000,
  0x43400000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrif_io*/
  /*if (!Pt4) Rd32=memw(Rs32+#u6:2)*/
  0xffe02000,
  0x45800000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrifnew_io*/
  /*if (!Pt4.new) Rd32=memw(Rs32+#u6:2)*/
  0xffe02000,
  0x47800000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrit_io*/
  /*if (Pt4) Rd32=memw(Rs32+#u6:2)*/
  0xffe02000,
  0x41800000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadritnew_io*/
  /*if (Pt4.new) Rd32=memw(Rs32+#u6:2)*/
  0xffe02000,
  0x43800000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrubf_io*/
  /*if (!Pt4) Rd32=memub(Rs32+#u6:0)*/
  0xffe02000,
  0x45200000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrubfnew_io*/
  /*if (!Pt4.new) Rd32=memub(Rs32+#u6:0)*/
  0xffe02000,
  0x47200000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrubt_io*/
  /*if (Pt4) Rd32=memub(Rs32+#u6:0)*/
  0xffe02000,
  0x41200000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadrubtnew_io*/
  /*if (Pt4.new) Rd32=memub(Rs32+#u6:0)*/
  0xffe02000,
  0x43200000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadruhf_io*/
  /*if (!Pt4) Rd32=memuh(Rs32+#u6:1)*/
  0xffe02000,
  0x45600000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadruhfnew_io*/
  /*if (!Pt4.new) Rd32=memuh(Rs32+#u6:1)*/
  0xffe02000,
  0x47600000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadruht_io*/
  /*if (Pt4) Rd32=memuh(Rs32+#u6:1)*/
  0xffe02000,
  0x41600000,
  0x000007e0,
  0 },
{ /*Tag:L2_ploadruhtnew_io*/
  /*if (Pt4.new) Rd32=memuh(Rs32+#u6:1)*/
  0xffe02000,
  0x43600000,
  0x000007e0,
  0 },
{ /*Tag:L4_add_memopb_io*/
  /*memb(Rs32+#u6:0)+=Rt32*/
  0xff602060,
  0x3e000000,
  0x00001f80,
  0 },
{ /*Tag:L4_add_memoph_io*/
  /*memh(Rs32+#u6:1)+=Rt32*/
  0xff602060,
  0x3e200000,
  0x00001f80,
  0 },
{ /*Tag:L4_add_memopw_io*/
  /*memw(Rs32+#u6:2)+=Rt32*/
  0xff602060,
  0x3e400000,
  0x00001f80,
  0 },
{ /*Tag:L4_and_memopb_io*/
  /*memb(Rs32+#u6:0)&=Rt32*/
  0xff602060,
  0x3e000040,
  0x00001f80,
  0 },
{ /*Tag:L4_and_memoph_io*/
  /*memh(Rs32+#u6:1)&=Rt32*/
  0xff602060,
  0x3e200040,
  0x00001f80,
  0 },
{ /*Tag:L4_and_memopw_io*/
  /*memw(Rs32+#u6:2)&=Rt32*/
  0xff602060,
  0x3e400040,
  0x00001f80,
  0 },
{ /*Tag:L4_iadd_memopb_io*/
  /*memb(Rs32+#u6:0)+=#U5*/
  0xff602060,
  0x3f000000,
  0x00001f80,
  0 },
{ /*Tag:L4_iadd_memoph_io*/
  /*memh(Rs32+#u6:1)+=#U5*/
  0xff602060,
  0x3f200000,
  0x00001f80,
  0 },
{ /*Tag:L4_iadd_memopw_io*/
  /*memw(Rs32+#u6:2)+=#U5*/
  0xff602060,
  0x3f400000,
  0x00001f80,
  0 },
{ /*Tag:L4_iand_memopb_io*/
  /*memb(Rs32+#u6:0)=clrbit(#U5)*/
  0xff602060,
  0x3f000040,
  0x00001f80,
  0 },
{ /*Tag:L4_iand_memoph_io*/
  /*memh(Rs32+#u6:1)=clrbit(#U5)*/
  0xff602060,
  0x3f200040,
  0x00001f80,
  0 },
{ /*Tag:L4_iand_memopw_io*/
  /*memw(Rs32+#u6:2)=clrbit(#U5)*/
  0xff602060,
  0x3f400040,
  0x00001f80,
  0 },
{ /*Tag:L4_ior_memopb_io*/
  /*memb(Rs32+#u6:0)=setbit(#U5)*/
  0xff602060,
  0x3f000060,
  0x00001f80,
  0 },
{ /*Tag:L4_ior_memoph_io*/
  /*memh(Rs32+#u6:1)=setbit(#U5)*/
  0xff602060,
  0x3f200060,
  0x00001f80,
  0 },
{ /*Tag:L4_ior_memopw_io*/
  /*memw(Rs32+#u6:2)=setbit(#U5)*/
  0xff602060,
  0x3f400060,
  0x00001f80,
  0 },
{ /*Tag:L4_isub_memopb_io*/
  /*memb(Rs32+#u6:0)-=#U5*/
  0xff602060,
  0x3f000020,
  0x00001f80,
  0 },
{ /*Tag:L4_isub_memoph_io*/
  /*memh(Rs32+#u6:1)-=#U5*/
  0xff602060,
  0x3f200020,
  0x00001f80,
  0 },
{ /*Tag:L4_isub_memopw_io*/
  /*memw(Rs32+#u6:2)-=#U5*/
  0xff602060,
  0x3f400020,
  0x00001f80,
  0 },
{ /*Tag:L4_loadalignb_ap*/
  /*Ryy32=memb_fifo(Re32=#U6)*/
  0xffe03000,
  0x9a801000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadalignb_ur*/
  /*Ryy32=memb_fifo(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9c801000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadalignh_ap*/
  /*Ryy32=memh_fifo(Re32=#U6)*/
  0xffe03000,
  0x9a401000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadalignh_ur*/
  /*Ryy32=memh_fifo(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9c401000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbsw2_ap*/
  /*Rd32=membh(Re32=#U6)*/
  0xffe03000,
  0x9a201000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbsw2_ur*/
  /*Rd32=membh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9c201000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbsw4_ap*/
  /*Rdd32=membh(Re32=#U6)*/
  0xffe03000,
  0x9ae01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbsw4_ur*/
  /*Rdd32=membh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9ce01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbzw2_ap*/
  /*Rd32=memubh(Re32=#U6)*/
  0xffe03000,
  0x9a601000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbzw2_ur*/
  /*Rd32=memubh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9c601000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbzw4_ap*/
  /*Rdd32=memubh(Re32=#U6)*/
  0xffe03000,
  0x9aa01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadbzw4_ur*/
  /*Rdd32=memubh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9ca01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrb_ap*/
  /*Rd32=memb(Re32=#U6)*/
  0xffe03000,
  0x9b001000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrb_ur*/
  /*Rd32=memb(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9d001000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrd_ap*/
  /*Rdd32=memd(Re32=#U6)*/
  0xffe03000,
  0x9bc01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrd_ur*/
  /*Rdd32=memd(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9dc01000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrh_ap*/
  /*Rd32=memh(Re32=#U6)*/
  0xffe03000,
  0x9b401000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrh_ur*/
  /*Rd32=memh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9d401000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadri_ap*/
  /*Rd32=memw(Re32=#U6)*/
  0xffe03000,
  0x9b801000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadri_ur*/
  /*Rd32=memw(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9d801000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrub_ap*/
  /*Rd32=memub(Re32=#U6)*/
  0xffe03000,
  0x9b201000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadrub_ur*/
  /*Rd32=memub(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9d201000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadruh_ap*/
  /*Rd32=memuh(Re32=#U6)*/
  0xffe03000,
  0x9b601000,
  0x00000f60,
  0 },
{ /*Tag:L4_loadruh_ur*/
  /*Rd32=memuh(Rt32<<#u2+#U6)*/
  0xffe01000,
  0x9d601000,
  0x00000f60,
  0 },
{ /*Tag:L4_or_memopb_io*/
  /*memb(Rs32+#u6:0)|=Rt32*/
  0xff602060,
  0x3e000060,
  0x00001f80,
  0 },
{ /*Tag:L4_or_memoph_io*/
  /*memh(Rs32+#u6:1)|=Rt32*/
  0xff602060,
  0x3e200060,
  0x00001f80,
  0 },
{ /*Tag:L4_or_memopw_io*/
  /*memw(Rs32+#u6:2)|=Rt32*/
  0xff602060,
  0x3e400060,
  0x00001f80,
  0 },
{ /*Tag:L4_ploadrbf_abs*/
  /*if (!Pt4) Rd32=memb(#u6)*/
  0xffe03880,
  0x9f002880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrbfnew_abs*/
  /*if (!Pt4.new) Rd32=memb(#u6)*/
  0xffe03880,
  0x9f003880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrbt_abs*/
  /*if (Pt4) Rd32=memb(#u6)*/
  0xffe03880,
  0x9f002080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrbtnew_abs*/
  /*if (Pt4.new) Rd32=memb(#u6)*/
  0xffe03880,
  0x9f003080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrdf_abs*/
  /*if (!Pt4) Rdd32=memd(#u6)*/
  0xffe03880,
  0x9fc02880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrdfnew_abs*/
  /*if (!Pt4.new) Rdd32=memd(#u6)*/
  0xffe03880,
  0x9fc03880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrdt_abs*/
  /*if (Pt4) Rdd32=memd(#u6)*/
  0xffe03880,
  0x9fc02080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrdtnew_abs*/
  /*if (Pt4.new) Rdd32=memd(#u6)*/
  0xffe03880,
  0x9fc03080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrhf_abs*/
  /*if (!Pt4) Rd32=memh(#u6)*/
  0xffe03880,
  0x9f402880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrhfnew_abs*/
  /*if (!Pt4.new) Rd32=memh(#u6)*/
  0xffe03880,
  0x9f403880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrht_abs*/
  /*if (Pt4) Rd32=memh(#u6)*/
  0xffe03880,
  0x9f402080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrhtnew_abs*/
  /*if (Pt4.new) Rd32=memh(#u6)*/
  0xffe03880,
  0x9f403080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrif_abs*/
  /*if (!Pt4) Rd32=memw(#u6)*/
  0xffe03880,
  0x9f802880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrifnew_abs*/
  /*if (!Pt4.new) Rd32=memw(#u6)*/
  0xffe03880,
  0x9f803880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrit_abs*/
  /*if (Pt4) Rd32=memw(#u6)*/
  0xffe03880,
  0x9f802080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadritnew_abs*/
  /*if (Pt4.new) Rd32=memw(#u6)*/
  0xffe03880,
  0x9f803080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrubf_abs*/
  /*if (!Pt4) Rd32=memub(#u6)*/
  0xffe03880,
  0x9f202880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrubfnew_abs*/
  /*if (!Pt4.new) Rd32=memub(#u6)*/
  0xffe03880,
  0x9f203880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrubt_abs*/
  /*if (Pt4) Rd32=memub(#u6)*/
  0xffe03880,
  0x9f202080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadrubtnew_abs*/
  /*if (Pt4.new) Rd32=memub(#u6)*/
  0xffe03880,
  0x9f203080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadruhf_abs*/
  /*if (!Pt4) Rd32=memuh(#u6)*/
  0xffe03880,
  0x9f602880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadruhfnew_abs*/
  /*if (!Pt4.new) Rd32=memuh(#u6)*/
  0xffe03880,
  0x9f603880,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadruht_abs*/
  /*if (Pt4) Rd32=memuh(#u6)*/
  0xffe03880,
  0x9f602080,
  0x001f0100,
  0 },
{ /*Tag:L4_ploadruhtnew_abs*/
  /*if (Pt4.new) Rd32=memuh(#u6)*/
  0xffe03880,
  0x9f603080,
  0x001f0100,
  0 },
{ /*Tag:L4_sub_memopb_io*/
  /*memb(Rs32+#u6:0)-=Rt32*/
  0xff602060,
  0x3e000020,
  0x00001f80,
  0 },
{ /*Tag:L4_sub_memoph_io*/
  /*memh(Rs32+#u6:1)-=Rt32*/
  0xff602060,
  0x3e200020,
  0x00001f80,
  0 },
{ /*Tag:L4_sub_memopw_io*/
  /*memw(Rs32+#u6:2)-=Rt32*/
  0xff602060,
  0x3e400020,
  0x00001f80,
  0 },
{ /*Tag:M2_accii*/
  /*Rx32+=add(Rs32,#s8)*/
  0xff802000,
  0xe2000000,
  0x00001fe0,
  0 },
{ /*Tag:M2_macsin*/
  /*Rx32-=mpyi(Rs32,#u8)*/
  0xff802000,
  0xe1800000,
  0x00001fe0,
  0 },
{ /*Tag:M2_macsip*/
  /*Rx32+=mpyi(Rs32,#u8)*/
  0xff802000,
  0xe1000000,
  0x00001fe0,
  0 },
{ /*Tag:M2_mpysip*/
  /*Rd32=+mpyi(Rs32,#u8)*/
  0xff802000,
  0xe0000000,
  0x00001fe0,
  0 },
{ /*Tag:M2_naccii*/
  /*Rx32-=add(Rs32,#s8)*/
  0xff802000,
  0xe2800000,
  0x00001fe0,
  0 },
{ /*Tag:M4_mpyri_addi*/
  /*Rd32=add(#u6,mpyi(Rs32,#U6))*/
  0xff000000,
  0xd8000000,
  0x006020e0,
  0 },
{ /*Tag:M4_mpyri_addr*/
  /*Rd32=add(Ru32,mpyi(Rs32,#u6))*/
  0xff800000,
  0xdf800000,
  0x006020e0,
  0 },
{ /*Tag:M4_mpyrr_addi*/
  /*Rd32=add(#u6,mpyi(Rs32,Rt32))*/
  0xff800000,
  0xd7000000,
  0x006020e0,
  0 },
{ /*Tag:PS_loadrbabs*/
  /*Rd32=memb(#u16:0)*/
  0xf9e00000,
  0x49000000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_loadrdabs*/
  /*Rdd32=memd(#u16:3)*/
  0xf9e00000,
  0x49c00000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_loadrhabs*/
  /*Rd32=memh(#u16:1)*/
  0xf9e00000,
  0x49400000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_loadriabs*/
  /*Rd32=memw(#u16:2)*/
  0xf9e00000,
  0x49800000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_loadrubabs*/
  /*Rd32=memub(#u16:0)*/
  0xf9e00000,
  0x49200000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_loadruhabs*/
  /*Rd32=memuh(#u16:1)*/
  0xf9e00000,
  0x49600000,
  0x061f3fe0,
  0 },
{ /*Tag:PS_storerbabs*/
  /*memb(#u16:0)=Rt32*/
  0xf9e00000,
  0x48000000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerbnewabs*/
  /*memb(#u16:0)=Nt8.new*/
  0xf9e01800,
  0x48a00000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerdabs*/
  /*memd(#u16:3)=Rtt32*/
  0xf9e00000,
  0x48c00000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerfabs*/
  /*memh(#u16:1)=Rt32.h*/
  0xf9e00000,
  0x48600000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerhabs*/
  /*memh(#u16:1)=Rt32*/
  0xf9e00000,
  0x48400000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerhnewabs*/
  /*memh(#u16:1)=Nt8.new*/
  0xf9e01800,
  0x48a00800,
  0x061f20ff,
  0 },
{ /*Tag:PS_storeriabs*/
  /*memw(#u16:2)=Rt32*/
  0xf9e00000,
  0x48800000,
  0x061f20ff,
  0 },
{ /*Tag:PS_storerinewabs*/
  /*memw(#u16:2)=Nt8.new*/
  0xf9e01800,
  0x48a01000,
  0x061f20ff,
  0 },
{ /*Tag:S2_pstorerbf_io*/
  /*if (!Pv4) memb(Rs32+#u6:0)=Rt32*/
  0xffe00004,
  0x44000000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerbnewf_io*/
  /*if (!Pv4) memb(Rs32+#u6:0)=Nt8.new*/
  0xffe01804,
  0x44a00000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerbnewt_io*/
  /*if (Pv4) memb(Rs32+#u6:0)=Nt8.new*/
  0xffe01804,
  0x40a00000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerbt_io*/
  /*if (Pv4) memb(Rs32+#u6:0)=Rt32*/
  0xffe00004,
  0x40000000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerdf_io*/
  /*if (!Pv4) memd(Rs32+#u6:3)=Rtt32*/
  0xffe00004,
  0x44c00000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerdt_io*/
  /*if (Pv4) memd(Rs32+#u6:3)=Rtt32*/
  0xffe00004,
  0x40c00000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerff_io*/
  /*if (!Pv4) memh(Rs32+#u6:1)=Rt32.h*/
  0xffe00004,
  0x44600000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerft_io*/
  /*if (Pv4) memh(Rs32+#u6:1)=Rt32.h*/
  0xffe00004,
  0x40600000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerhf_io*/
  /*if (!Pv4) memh(Rs32+#u6:1)=Rt32*/
  0xffe00004,
  0x44400000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerhnewf_io*/
  /*if (!Pv4) memh(Rs32+#u6:1)=Nt8.new*/
  0xffe01804,
  0x44a00800,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerhnewt_io*/
  /*if (Pv4) memh(Rs32+#u6:1)=Nt8.new*/
  0xffe01804,
  0x40a00800,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerht_io*/
  /*if (Pv4) memh(Rs32+#u6:1)=Rt32*/
  0xffe00004,
  0x40400000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerif_io*/
  /*if (!Pv4) memw(Rs32+#u6:2)=Rt32*/
  0xffe00004,
  0x44800000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerinewf_io*/
  /*if (!Pv4) memw(Rs32+#u6:2)=Nt8.new*/
  0xffe01804,
  0x44a01000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerinewt_io*/
  /*if (Pv4) memw(Rs32+#u6:2)=Nt8.new*/
  0xffe01804,
  0x40a01000,
  0x000020f8,
  0 },
{ /*Tag:S2_pstorerit_io*/
  /*if (Pv4) memw(Rs32+#u6:2)=Rt32*/
  0xffe00004,
  0x40800000,
  0x000020f8,
  0 },
{ /*Tag:S2_storerb_io*/
  /*memb(Rs32+#s11:0)=Rt32*/
  0xf9e00000,
  0xa1000000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerbgp*/
  /*memb(gp+#u16:0)=Rt32*/
  0xf9e00000,
  0x48000000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerbnew_io*/
  /*memb(Rs32+#s11:0)=Nt8.new*/
  0xf9e01800,
  0xa1a00000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerbnewgp*/
  /*memb(gp+#u16:0)=Nt8.new*/
  0xf9e01800,
  0x48a00000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerd_io*/
  /*memd(Rs32+#s11:3)=Rtt32*/
  0xf9e00000,
  0xa1c00000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerdgp*/
  /*memd(gp+#u16:3)=Rtt32*/
  0xf9e00000,
  0x48c00000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerf_io*/
  /*memh(Rs32+#s11:1)=Rt32.h*/
  0xf9e00000,
  0xa1600000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerfgp*/
  /*memh(gp+#u16:1)=Rt32.h*/
  0xf9e00000,
  0x48600000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerh_io*/
  /*memh(Rs32+#s11:1)=Rt32*/
  0xf9e00000,
  0xa1400000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerhgp*/
  /*memh(gp+#u16:1)=Rt32*/
  0xf9e00000,
  0x48400000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerhnew_io*/
  /*memh(Rs32+#s11:1)=Nt8.new*/
  0xf9e01800,
  0xa1a00800,
  0x060020ff,
  0 },
{ /*Tag:S2_storerhnewgp*/
  /*memh(gp+#u16:1)=Nt8.new*/
  0xf9e01800,
  0x48a00800,
  0x061f20ff,
  0 },
{ /*Tag:S2_storeri_io*/
  /*memw(Rs32+#s11:2)=Rt32*/
  0xf9e00000,
  0xa1800000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerigp*/
  /*memw(gp+#u16:2)=Rt32*/
  0xf9e00000,
  0x48800000,
  0x061f20ff,
  0 },
{ /*Tag:S2_storerinew_io*/
  /*memw(Rs32+#s11:2)=Nt8.new*/
  0xf9e01800,
  0xa1a01000,
  0x060020ff,
  0 },
{ /*Tag:S2_storerinewgp*/
  /*memw(gp+#u16:2)=Nt8.new*/
  0xf9e01800,
  0x48a01000,
  0x061f20ff,
  0 },
{ /*Tag:S4_addaddi*/
  /*Rd32=add(Rs32,add(Ru32,#s6))*/
  0xff800000,
  0xdb000000,
  0x006020e0,
  0 },
{ /*Tag:S4_addi_asl_ri*/
  /*Rx32=add(#u8,asl(Rx32,#U5))*/
  0xff000016,
  0xde000004,
  0x00e020e8,
  0 },
{ /*Tag:S4_addi_lsr_ri*/
  /*Rx32=add(#u8,lsr(Rx32,#U5))*/
  0xff000016,
  0xde000014,
  0x00e020e8,
  0 },
{ /*Tag:S4_andi_asl_ri*/
  /*Rx32=and(#u8,asl(Rx32,#U5))*/
  0xff000016,
  0xde000000,
  0x00e020e8,
  0 },
{ /*Tag:S4_andi_lsr_ri*/
  /*Rx32=and(#u8,lsr(Rx32,#U5))*/
  0xff000016,
  0xde000010,
  0x00e020e8,
  0 },
{ /*Tag:S4_or_andi*/
  /*Rx32|=and(Rs32,#s10)*/
  0xffc00000,
  0xda000000,
  0x00203fe0,
  0 },
{ /*Tag:S4_or_andix*/
  /*Rx32=or(Ru32,and(Rx32,#s10))*/
  0xffc00000,
  0xda400000,
  0x00203fe0,
  0 },
{ /*Tag:S4_or_ori*/
  /*Rx32|=or(Rs32,#s10)*/
  0xffc00000,
  0xda800000,
  0x00203fe0,
  0 },
{ /*Tag:S4_ori_asl_ri*/
  /*Rx32=or(#u8,asl(Rx32,#U5))*/
  0xff000016,
  0xde000002,
  0x00e020e8,
  0 },
{ /*Tag:S4_ori_lsr_ri*/
  /*Rx32=or(#u8,lsr(Rx32,#U5))*/
  0xff000016,
  0xde000012,
  0x00e020e8,
  0 },
{ /*Tag:S4_pstorerbf_abs*/
  /*if (!Pv4) memb(#u6)=Rt32*/
  0xffe02084,
  0xaf000084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbfnew_abs*/
  /*if (!Pv4.new) memb(#u6)=Rt32*/
  0xffe02084,
  0xaf002084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbfnew_io*/
  /*if (!Pv4.new) memb(Rs32+#u6:0)=Rt32*/
  0xffe00004,
  0x46000000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerbnewf_abs*/
  /*if (!Pv4) memb(#u6)=Nt8.new*/
  0xffe03884,
  0xafa00084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbnewfnew_abs*/
  /*if (!Pv4.new) memb(#u6)=Nt8.new*/
  0xffe03884,
  0xafa02084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbnewfnew_io*/
  /*if (!Pv4.new) memb(Rs32+#u6:0)=Nt8.new*/
  0xffe01804,
  0x46a00000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerbnewt_abs*/
  /*if (Pv4) memb(#u6)=Nt8.new*/
  0xffe03884,
  0xafa00080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbnewtnew_abs*/
  /*if (Pv4.new) memb(#u6)=Nt8.new*/
  0xffe03884,
  0xafa02080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbnewtnew_io*/
  /*if (Pv4.new) memb(Rs32+#u6:0)=Nt8.new*/
  0xffe01804,
  0x42a00000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerbt_abs*/
  /*if (Pv4) memb(#u6)=Rt32*/
  0xffe02084,
  0xaf000080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbtnew_abs*/
  /*if (Pv4.new) memb(#u6)=Rt32*/
  0xffe02084,
  0xaf002080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerbtnew_io*/
  /*if (Pv4.new) memb(Rs32+#u6:0)=Rt32*/
  0xffe00004,
  0x42000000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerdf_abs*/
  /*if (!Pv4) memd(#u6)=Rtt32*/
  0xffe02084,
  0xafc00084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerdfnew_abs*/
  /*if (!Pv4.new) memd(#u6)=Rtt32*/
  0xffe02084,
  0xafc02084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerdfnew_io*/
  /*if (!Pv4.new) memd(Rs32+#u6:3)=Rtt32*/
  0xffe00004,
  0x46c00000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerdt_abs*/
  /*if (Pv4) memd(#u6)=Rtt32*/
  0xffe02084,
  0xafc00080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerdtnew_abs*/
  /*if (Pv4.new) memd(#u6)=Rtt32*/
  0xffe02084,
  0xafc02080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerdtnew_io*/
  /*if (Pv4.new) memd(Rs32+#u6:3)=Rtt32*/
  0xffe00004,
  0x42c00000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerff_abs*/
  /*if (!Pv4) memh(#u6)=Rt32.h*/
  0xffe02084,
  0xaf600084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerffnew_abs*/
  /*if (!Pv4.new) memh(#u6)=Rt32.h*/
  0xffe02084,
  0xaf602084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerffnew_io*/
  /*if (!Pv4.new) memh(Rs32+#u6:1)=Rt32.h*/
  0xffe00004,
  0x46600000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerft_abs*/
  /*if (Pv4) memh(#u6)=Rt32.h*/
  0xffe02084,
  0xaf600080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerftnew_abs*/
  /*if (Pv4.new) memh(#u6)=Rt32.h*/
  0xffe02084,
  0xaf602080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerftnew_io*/
  /*if (Pv4.new) memh(Rs32+#u6:1)=Rt32.h*/
  0xffe00004,
  0x42600000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerhf_abs*/
  /*if (!Pv4) memh(#u6)=Rt32*/
  0xffe02084,
  0xaf400084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhfnew_abs*/
  /*if (!Pv4.new) memh(#u6)=Rt32*/
  0xffe02084,
  0xaf402084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhfnew_io*/
  /*if (!Pv4.new) memh(Rs32+#u6:1)=Rt32*/
  0xffe00004,
  0x46400000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerhnewf_abs*/
  /*if (!Pv4) memh(#u6)=Nt8.new*/
  0xffe03884,
  0xafa00884,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhnewfnew_abs*/
  /*if (!Pv4.new) memh(#u6)=Nt8.new*/
  0xffe03884,
  0xafa02884,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhnewfnew_io*/
  /*if (!Pv4.new) memh(Rs32+#u6:1)=Nt8.new*/
  0xffe01804,
  0x46a00800,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerhnewt_abs*/
  /*if (Pv4) memh(#u6)=Nt8.new*/
  0xffe03884,
  0xafa00880,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhnewtnew_abs*/
  /*if (Pv4.new) memh(#u6)=Nt8.new*/
  0xffe03884,
  0xafa02880,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhnewtnew_io*/
  /*if (Pv4.new) memh(Rs32+#u6:1)=Nt8.new*/
  0xffe01804,
  0x42a00800,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerht_abs*/
  /*if (Pv4) memh(#u6)=Rt32*/
  0xffe02084,
  0xaf400080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhtnew_abs*/
  /*if (Pv4.new) memh(#u6)=Rt32*/
  0xffe02084,
  0xaf402080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerhtnew_io*/
  /*if (Pv4.new) memh(Rs32+#u6:1)=Rt32*/
  0xffe00004,
  0x42400000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerif_abs*/
  /*if (!Pv4) memw(#u6)=Rt32*/
  0xffe02084,
  0xaf800084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerifnew_abs*/
  /*if (!Pv4.new) memw(#u6)=Rt32*/
  0xffe02084,
  0xaf802084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerifnew_io*/
  /*if (!Pv4.new) memw(Rs32+#u6:2)=Rt32*/
  0xffe00004,
  0x46800000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerinewf_abs*/
  /*if (!Pv4) memw(#u6)=Nt8.new*/
  0xffe03884,
  0xafa01084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerinewfnew_abs*/
  /*if (!Pv4.new) memw(#u6)=Nt8.new*/
  0xffe03884,
  0xafa03084,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerinewfnew_io*/
  /*if (!Pv4.new) memw(Rs32+#u6:2)=Nt8.new*/
  0xffe01804,
  0x46a01000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerinewt_abs*/
  /*if (Pv4) memw(#u6)=Nt8.new*/
  0xffe03884,
  0xafa01080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerinewtnew_abs*/
  /*if (Pv4.new) memw(#u6)=Nt8.new*/
  0xffe03884,
  0xafa03080,
  0x00030078,
  0 },
{ /*Tag:S4_pstorerinewtnew_io*/
  /*if (Pv4.new) memw(Rs32+#u6:2)=Nt8.new*/
  0xffe01804,
  0x42a01000,
  0x000020f8,
  0 },
{ /*Tag:S4_pstorerit_abs*/
  /*if (Pv4) memw(#u6)=Rt32*/
  0xffe02084,
  0xaf800080,
  0x00030078,
  0 },
{ /*Tag:S4_pstoreritnew_abs*/
  /*if (Pv4.new) memw(#u6)=Rt32*/
  0xffe02084,
  0xaf802080,
  0x00030078,
  0 },
{ /*Tag:S4_pstoreritnew_io*/
  /*if (Pv4.new) memw(Rs32+#u6:2)=Rt32*/
  0xffe00004,
  0x42800000,
  0x000020f8,
  0 },
{ /*Tag:S4_storeirb_io*/
  /*memb(Rs32+#u6:0)=#S8*/
  0xfe600000,
  0x3c000000,
  0x0000207f,
  0 },
{ /*Tag:S4_storeirbf_io*/
  /*if (!Pv4) memb(Rs32+#u6:0)=#S6*/
  0xffe00000,
  0x38800000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirbfnew_io*/
  /*if (!Pv4.new) memb(Rs32+#u6:0)=#S6*/
  0xffe00000,
  0x39800000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirbt_io*/
  /*if (Pv4) memb(Rs32+#u6:0)=#S6*/
  0xffe00000,
  0x38000000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirbtnew_io*/
  /*if (Pv4.new) memb(Rs32+#u6:0)=#S6*/
  0xffe00000,
  0x39000000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirh_io*/
  /*memh(Rs32+#u6:1)=#S8*/
  0xfe600000,
  0x3c200000,
  0x0000207f,
  0 },
{ /*Tag:S4_storeirhf_io*/
  /*if (!Pv4) memh(Rs32+#u6:1)=#S6*/
  0xffe00000,
  0x38a00000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirhfnew_io*/
  /*if (!Pv4.new) memh(Rs32+#u6:1)=#S6*/
  0xffe00000,
  0x39a00000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirht_io*/
  /*if (Pv4) memh(Rs32+#u6:1)=#S6*/
  0xffe00000,
  0x38200000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirhtnew_io*/
  /*if (Pv4.new) memh(Rs32+#u6:1)=#S6*/
  0xffe00000,
  0x39200000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeiri_io*/
  /*memw(Rs32+#u6:2)=#S8*/
  0xfe600000,
  0x3c400000,
  0x0000207f,
  0 },
{ /*Tag:S4_storeirif_io*/
  /*if (!Pv4) memw(Rs32+#u6:2)=#S6*/
  0xffe00000,
  0x38c00000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirifnew_io*/
  /*if (!Pv4.new) memw(Rs32+#u6:2)=#S6*/
  0xffe00000,
  0x39c00000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeirit_io*/
  /*if (Pv4) memw(Rs32+#u6:2)=#S6*/
  0xffe00000,
  0x38400000,
  0x0000201f,
  0 },
{ /*Tag:S4_storeiritnew_io*/
  /*if (Pv4.new) memw(Rs32+#u6:2)=#S6*/
  0xffe00000,
  0x39400000,
  0x0000201f,
  0 },
{ /*Tag:S4_storerb_ap*/
  /*memb(Re32=#U6)=Rt32*/
  0xffe02080,
  0xab000080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerb_ur*/
  /*memb(Ru32<<#u2+#U6)=Rt32*/
  0xffe00080,
  0xad000080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerbnew_ap*/
  /*memb(Re32=#U6)=Nt8.new*/
  0xffe03880,
  0xaba00080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerbnew_ur*/
  /*memb(Ru32<<#u2+#U6)=Nt8.new*/
  0xffe01880,
  0xada00080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerd_ap*/
  /*memd(Re32=#U6)=Rtt32*/
  0xffe02080,
  0xabc00080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerd_ur*/
  /*memd(Ru32<<#u2+#U6)=Rtt32*/
  0xffe00080,
  0xadc00080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerf_ap*/
  /*memh(Re32=#U6)=Rt32.h*/
  0xffe02080,
  0xab600080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerf_ur*/
  /*memh(Ru32<<#u2+#U6)=Rt32.h*/
  0xffe00080,
  0xad600080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerh_ap*/
  /*memh(Re32=#U6)=Rt32*/
  0xffe02080,
  0xab400080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerh_ur*/
  /*memh(Ru32<<#u2+#U6)=Rt32*/
  0xffe00080,
  0xad400080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerhnew_ap*/
  /*memh(Re32=#U6)=Nt8.new*/
  0xffe03880,
  0xaba00880,
  0x0000003f,
  0 },
{ /*Tag:S4_storerhnew_ur*/
  /*memh(Ru32<<#u2+#U6)=Nt8.new*/
  0xffe01880,
  0xada00880,
  0x0000003f,
  0 },
{ /*Tag:S4_storeri_ap*/
  /*memw(Re32=#U6)=Rt32*/
  0xffe02080,
  0xab800080,
  0x0000003f,
  0 },
{ /*Tag:S4_storeri_ur*/
  /*memw(Ru32<<#u2+#U6)=Rt32*/
  0xffe00080,
  0xad800080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerinew_ap*/
  /*memw(Re32=#U6)=Nt8.new*/
  0xffe03880,
  0xaba01080,
  0x0000003f,
  0 },
{ /*Tag:S4_storerinew_ur*/
  /*memw(Ru32<<#u2+#U6)=Nt8.new*/
  0xffe01880,
  0xada01080,
  0x0000003f,
  0 },
{ /*Tag:S4_subaddi*/
  /*Rd32=add(Rs32,sub(#s6,Ru32))*/
  0xff800000,
  0xdb800000,
  0x006020e0,
  0 },
{ /*Tag:S4_subi_asl_ri*/
  /*Rx32=sub(#u8,asl(Rx32,#U5))*/
  0xff000016,
  0xde000006,
  0x00e020e8,
  0 },
{ /*Tag:S4_subi_lsr_ri*/
  /*Rx32=sub(#u8,lsr(Rx32,#U5))*/
  0xff000016,
  0xde000016,
  0x00e020e8,
  0 },
{ /*Tag:SA1_addi*/
  /*Rx16=add(Rx16,#s7)*/
  0xf8002000,
  0x20002000,
  0x07f00000,
  1 },
{ /*Tag:SA1_addi*/
  /*Rx16=add(Rx16,#s7)*/
  0xf8002000,
  0x40000000,
  0x07f00000,
  1 },
{ /*Tag:SA1_addi*/
  /*Rx16=add(Rx16,#s7)*/
  0xf8002000,
  0x40002000,
  0x07f00000,
  1 },
{ /*Tag:SA1_addi*/
  /*Rx16=add(Rx16,#s7)*/
  0xf8002000,
  0x60000000,
  0x07f00000,
  1 },
{ /*Tag:SA1_addi*/
  /*Rx16=add(Rx16,#s7)*/
  0xf8002000,
  0x60002000,
  0x07f00000,
  1 },
{ /*Tag:SA1_seti*/
  /*Rd16=#u6*/
  0xfc002000,
  0x28002000,
  0x03f00000,
  1 },
{ /*Tag:SA1_seti*/
  /*Rd16=#u6*/
  0xfc002000,
  0x48000000,
  0x03f00000,
  1 },
{ /*Tag:SA1_seti*/
  /*Rd16=#u6*/
  0xfc002000,
  0x48002000,
  0x03f00000,
  1 },
{ /*Tag:SA1_seti*/
  /*Rd16=#u6*/
  0xfc002000,
  0x68000000,
  0x03f00000,
  1 },
{ /*Tag:SA1_seti*/
  /*Rd16=#u6*/
  0xfc002000,
  0x68002000,
  0x03f00000,
  1 },
{ /*Tag:dup_A2_addi*/
  /*Rd32=add(Rs32,#s16)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_A2_andir*/
  /*Rd32=and(Rs32,#s10)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_A2_combineii*/
  /*Rdd32=combine(#s8,#S8)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_A2_tfrsi*/
  /*Rd32=#s16*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_A4_combineii*/
  /*Rdd32=combine(#s8,#U6)*/
  0x00000000,
  0x00000000,
  0x00002404,
  0 },
{ /*Tag:dup_A4_combineir*/
  /*Rdd32=combine(#s8,Rs32)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_A4_combineri*/
  /*Rdd32=combine(Rs32,#s8)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_C2_cmoveif*/
  /*if (!Pu4) Rd32=#s12*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_C2_cmoveit*/
  /*if (Pu4) Rd32=#s12*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_C2_cmovenewif*/
  /*if (!Pu4.new) Rd32=#s12*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_C2_cmovenewit*/
  /*if (Pu4.new) Rd32=#s12*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_C2_cmpeqi*/
  /*Pd4=cmp.eq(Rs32,#s10)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadrb_io*/
  /*Rd32=memb(Rs32+#s11:0)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadrd_io*/
  /*Rdd32=memd(Rs32+#s11:3)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadrh_io*/
  /*Rd32=memh(Rs32+#s11:1)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadri_io*/
  /*Rd32=memw(Rs32+#s11:2)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadrub_io*/
  /*Rd32=memub(Rs32+#s11:0)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_L2_loadruh_io*/
  /*Rd32=memuh(Rs32+#s11:1)*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_S2_storerb_io*/
  /*memb(Rs32+#s11:0)=Rt32*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_S2_storerd_io*/
  /*memd(Rs32+#s11:3)=Rtt32*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_S2_storerh_io*/
  /*memh(Rs32+#s11:1)=Rt32*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_S2_storeri_io*/
  /*memw(Rs32+#s11:2)=Rt32*/
  0x00000000,
  0x00000000,
  0x00000000,
  0 },
{ /*Tag:dup_S4_storeirb_io*/
  /*memb(Rs32+#u6:0)=#S8*/
  0x00000000,
  0x00000000,
  0x00002404,
  0 },
{ /*Tag:dup_S4_storeiri_io*/
  /*memw(Rs32+#u6:2)=#S8*/
  0x00000000,
  0x00000000,
  0x00002404,
  0 }
};

#endif  // LLVM_LIB_TARGET_HEXAGON_HEXAGONDEPMASK_H
