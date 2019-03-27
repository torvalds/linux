/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                           */
/* File:       /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top/scorpion_reg_map.h*/
/* Creator:    irshad                                                        */
/* Time:       Wednesday Feb 15, 2012 [5:06:37 pm]                           */
/*                                                                           */
/* Path:       /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top*/
/* Arguments:  /cad/denali/blueprint/3.7.3//Linux-64bit/blueprint -dump      */
/*             -codegen                                                      */
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/flow/blueprint/ath_ansic.codegen*/
/*             -ath_ansic -Wdesc -I                                          */
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top*/
/*             -I /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint */
/*             -I                                                            */
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/flow/blueprint*/
/*             -I                                                            */
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig*/
/*             -odir                                                         */
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top*/
/*             -eval {$INCLUDE_SYSCONFIG_FILES=1} -eval                      */
/*             $WAR_EV58615_for_ansic_codegen=1 scorpion_reg.rdl             */
/*                                                                           */
/* Sources:    /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/mac_dcu_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/rtc/rtc_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dma_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/rtc_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/mac_pcu_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dcu_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/mac/rtl/mac_pcu/blueprint/mac_pcu_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/wmac_wrap/rtc_sync_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_qcu_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/mac_dma_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top/scorpion_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/bb_reg_map_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/top/scorpion_radio_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/svd_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/radio_65_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/bb/blueprint/bb_reg_map.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/rtc_sync_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/rtl/svd/svd_reg.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/blueprint/sysconfig/mac_qcu_reg_sysconfig.rdl*/
/*             /trees/irshad/irshad-scorpion/chips/scorpion/1.0/flow/blueprint/ath_ansic.pm*/
/*             /cad/local/lib/perl/Pinfo.pm                                  */
/*                                                                           */
/* Blueprint:   3.7.3 (Fri Aug 29 12:39:16 PDT 2008)                         */
/* Machine:    rupavathi.users.atheros.com                                   */
/* OS:         Linux 2.6.9-89.ELsmp                                          */
/* Description:                                                              */
/*                                                                           */
/*This Register Map contains the complete register set for scorpion.         */
/*                                                                           */
/* Copyright (C) 2012 Denali Software Inc.  All rights reserved              */
/* THIS FILE IS AUTOMATICALLY GENERATED BY DENALI BLUEPRINT, DO NOT EDIT     */
/*                                                                           */


#ifndef __REG_SCORPION_REG_MAP_H__
#define __REG_SCORPION_REG_MAP_H__

#include "scorpion_reg_map_macro.h"

struct mac_dma_reg {
  volatile char pad__0[0x8];                      /*        0x0 - 0x8        */
  volatile u_int32_t MAC_DMA_CR;                  /*        0x8 - 0xc        */
  volatile char pad__1[0x8];                      /*        0xc - 0x14       */
  volatile u_int32_t MAC_DMA_CFG;                 /*       0x14 - 0x18       */
  volatile u_int32_t MAC_DMA_RXBUFPTR_THRESH;     /*       0x18 - 0x1c       */
  volatile u_int32_t MAC_DMA_TXDPPTR_THRESH;      /*       0x1c - 0x20       */
  volatile u_int32_t MAC_DMA_MIRT;                /*       0x20 - 0x24       */
  volatile u_int32_t MAC_DMA_GLOBAL_IER;          /*       0x24 - 0x28       */
  volatile u_int32_t MAC_DMA_TIMT;                /*       0x28 - 0x2c       */
  volatile u_int32_t MAC_DMA_RIMT;                /*       0x2c - 0x30       */
  volatile u_int32_t MAC_DMA_TXCFG;               /*       0x30 - 0x34       */
  volatile u_int32_t MAC_DMA_RXCFG;               /*       0x34 - 0x38       */
  volatile u_int32_t MAC_DMA_RXJLA;               /*       0x38 - 0x3c       */
  volatile char pad__2[0x4];                      /*       0x3c - 0x40       */
  volatile u_int32_t MAC_DMA_MIBC;                /*       0x40 - 0x44       */
  volatile u_int32_t MAC_DMA_TOPS;                /*       0x44 - 0x48       */
  volatile u_int32_t MAC_DMA_RXNPTO;              /*       0x48 - 0x4c       */
  volatile u_int32_t MAC_DMA_TXNPTO;              /*       0x4c - 0x50       */
  volatile u_int32_t MAC_DMA_RPGTO;               /*       0x50 - 0x54       */
  volatile char pad__3[0x4];                      /*       0x54 - 0x58       */
  volatile u_int32_t MAC_DMA_MACMISC;             /*       0x58 - 0x5c       */
  volatile u_int32_t MAC_DMA_INTER;               /*       0x5c - 0x60       */
  volatile u_int32_t MAC_DMA_DATABUF;             /*       0x60 - 0x64       */
  volatile u_int32_t MAC_DMA_GTT;                 /*       0x64 - 0x68       */
  volatile u_int32_t MAC_DMA_GTTM;                /*       0x68 - 0x6c       */
  volatile u_int32_t MAC_DMA_CST;                 /*       0x6c - 0x70       */
  volatile u_int32_t MAC_DMA_RXDP_SIZE;           /*       0x70 - 0x74       */
  volatile u_int32_t MAC_DMA_RX_QUEUE_HP_RXDP;    /*       0x74 - 0x78       */
  volatile u_int32_t MAC_DMA_RX_QUEUE_LP_RXDP;    /*       0x78 - 0x7c       */
  volatile char pad__4[0x4];                      /*       0x7c - 0x80       */
  volatile u_int32_t MAC_DMA_ISR_P;               /*       0x80 - 0x84       */
  volatile u_int32_t MAC_DMA_ISR_S0;              /*       0x84 - 0x88       */
  volatile u_int32_t MAC_DMA_ISR_S1;              /*       0x88 - 0x8c       */
  volatile u_int32_t MAC_DMA_ISR_S2;              /*       0x8c - 0x90       */
  volatile u_int32_t MAC_DMA_ISR_S3;              /*       0x90 - 0x94       */
  volatile u_int32_t MAC_DMA_ISR_S4;              /*       0x94 - 0x98       */
  volatile u_int32_t MAC_DMA_ISR_S5;              /*       0x98 - 0x9c       */
  volatile char pad__5[0x4];                      /*       0x9c - 0xa0       */
  volatile u_int32_t MAC_DMA_IMR_P;               /*       0xa0 - 0xa4       */
  volatile u_int32_t MAC_DMA_IMR_S0;              /*       0xa4 - 0xa8       */
  volatile u_int32_t MAC_DMA_IMR_S1;              /*       0xa8 - 0xac       */
  volatile u_int32_t MAC_DMA_IMR_S2;              /*       0xac - 0xb0       */
  volatile u_int32_t MAC_DMA_IMR_S3;              /*       0xb0 - 0xb4       */
  volatile u_int32_t MAC_DMA_IMR_S4;              /*       0xb4 - 0xb8       */
  volatile u_int32_t MAC_DMA_IMR_S5;              /*       0xb8 - 0xbc       */
  volatile char pad__6[0x4];                      /*       0xbc - 0xc0       */
  volatile u_int32_t MAC_DMA_ISR_P_RAC;           /*       0xc0 - 0xc4       */
  volatile u_int32_t MAC_DMA_ISR_S0_S;            /*       0xc4 - 0xc8       */
  volatile u_int32_t MAC_DMA_ISR_S1_S;            /*       0xc8 - 0xcc       */
  volatile char pad__7[0x4];                      /*       0xcc - 0xd0       */
  volatile u_int32_t MAC_DMA_ISR_S2_S;            /*       0xd0 - 0xd4       */
  volatile u_int32_t MAC_DMA_ISR_S3_S;            /*       0xd4 - 0xd8       */
  volatile u_int32_t MAC_DMA_ISR_S4_S;            /*       0xd8 - 0xdc       */
  volatile u_int32_t MAC_DMA_ISR_S5_S;            /*       0xdc - 0xe0       */
  volatile u_int32_t MAC_DMA_DMADBG_0;            /*       0xe0 - 0xe4       */
  volatile u_int32_t MAC_DMA_DMADBG_1;            /*       0xe4 - 0xe8       */
  volatile u_int32_t MAC_DMA_DMADBG_2;            /*       0xe8 - 0xec       */
  volatile u_int32_t MAC_DMA_DMADBG_3;            /*       0xec - 0xf0       */
  volatile u_int32_t MAC_DMA_DMADBG_4;            /*       0xf0 - 0xf4       */
  volatile u_int32_t MAC_DMA_DMADBG_5;            /*       0xf4 - 0xf8       */
  volatile u_int32_t MAC_DMA_DMADBG_6;            /*       0xf8 - 0xfc       */
  volatile u_int32_t MAC_DMA_DMADBG_7;            /*       0xfc - 0x100      */
  volatile u_int32_t MAC_DMA_QCU_TXDP_REMAINING_QCU_7_0;
                                                  /*      0x100 - 0x104      */
  volatile u_int32_t MAC_DMA_QCU_TXDP_REMAINING_QCU_9_8;
                                                  /*      0x104 - 0x108      */
  volatile u_int32_t MAC_DMA_TIMT_0;              /*      0x108 - 0x10c      */
  volatile u_int32_t MAC_DMA_TIMT_1;              /*      0x10c - 0x110      */
  volatile u_int32_t MAC_DMA_TIMT_2;              /*      0x110 - 0x114      */
  volatile u_int32_t MAC_DMA_TIMT_3;              /*      0x114 - 0x118      */
  volatile u_int32_t MAC_DMA_TIMT_4;              /*      0x118 - 0x11c      */
  volatile u_int32_t MAC_DMA_TIMT_5;              /*      0x11c - 0x120      */
  volatile u_int32_t MAC_DMA_TIMT_6;              /*      0x120 - 0x124      */
  volatile u_int32_t MAC_DMA_TIMT_7;              /*      0x124 - 0x128      */
  volatile u_int32_t MAC_DMA_TIMT_8;              /*      0x128 - 0x12c      */
  volatile u_int32_t MAC_DMA_TIMT_9;              /*      0x12c - 0x130      */
};

struct mac_qcu_reg {
  volatile u_int32_t MAC_QCU_TXDP[10];            /*        0x0 - 0x28       */
  volatile char pad__0[0x8];                      /*       0x28 - 0x30       */
  volatile u_int32_t MAC_QCU_STATUS_RING_START;   /*       0x30 - 0x34       */
  volatile u_int32_t MAC_QCU_STATUS_RING_END;     /*       0x34 - 0x38       */
  volatile u_int32_t MAC_QCU_STATUS_RING_CURRENT; /*       0x38 - 0x3c       */
  volatile char pad__1[0x4];                      /*       0x3c - 0x40       */
  volatile u_int32_t MAC_QCU_TXE;                 /*       0x40 - 0x44       */
  volatile char pad__2[0x3c];                     /*       0x44 - 0x80       */
  volatile u_int32_t MAC_QCU_TXD;                 /*       0x80 - 0x84       */
  volatile char pad__3[0x3c];                     /*       0x84 - 0xc0       */
  volatile u_int32_t MAC_QCU_CBR[10];             /*       0xc0 - 0xe8       */
  volatile char pad__4[0x18];                     /*       0xe8 - 0x100      */
  volatile u_int32_t MAC_QCU_RDYTIME[10];         /*      0x100 - 0x128      */
  volatile char pad__5[0x18];                     /*      0x128 - 0x140      */
  volatile u_int32_t MAC_QCU_ONESHOT_ARM_SC;      /*      0x140 - 0x144      */
  volatile char pad__6[0x3c];                     /*      0x144 - 0x180      */
  volatile u_int32_t MAC_QCU_ONESHOT_ARM_CC;      /*      0x180 - 0x184      */
  volatile char pad__7[0x3c];                     /*      0x184 - 0x1c0      */
  volatile u_int32_t MAC_QCU_MISC[10];            /*      0x1c0 - 0x1e8      */
  volatile char pad__8[0x18];                     /*      0x1e8 - 0x200      */
  volatile u_int32_t MAC_QCU_CNT[10];             /*      0x200 - 0x228      */
  volatile char pad__9[0x18];                     /*      0x228 - 0x240      */
  volatile u_int32_t MAC_QCU_RDYTIME_SHDN;        /*      0x240 - 0x244      */
  volatile u_int32_t MAC_QCU_DESC_CRC_CHK;        /*      0x244 - 0x248      */
};

struct mac_dcu_reg {
  volatile u_int32_t MAC_DCU_QCUMASK[10];         /*        0x0 - 0x28       */
  volatile char pad__0[0x8];                      /*       0x28 - 0x30       */
  volatile u_int32_t MAC_DCU_GBL_IFS_SIFS;        /*       0x30 - 0x34       */
  volatile char pad__1[0x4];                      /*       0x34 - 0x38       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_31_0;  /*       0x38 - 0x3c       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_31_0;  /*       0x3c - 0x40       */
  volatile u_int32_t MAC_DCU_LCL_IFS[10];         /*       0x40 - 0x68       */
  volatile char pad__2[0x8];                      /*       0x68 - 0x70       */
  volatile u_int32_t MAC_DCU_GBL_IFS_SLOT;        /*       0x70 - 0x74       */
  volatile char pad__3[0x4];                      /*       0x74 - 0x78       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_63_32; /*       0x78 - 0x7c       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_63_32; /*       0x7c - 0x80       */
  volatile u_int32_t MAC_DCU_RETRY_LIMIT[10];     /*       0x80 - 0xa8       */
  volatile char pad__4[0x8];                      /*       0xa8 - 0xb0       */
  volatile u_int32_t MAC_DCU_GBL_IFS_EIFS;        /*       0xb0 - 0xb4       */
  volatile char pad__5[0x4];                      /*       0xb4 - 0xb8       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_95_64; /*       0xb8 - 0xbc       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_95_64; /*       0xbc - 0xc0       */
  volatile u_int32_t MAC_DCU_CHANNEL_TIME[10];    /*       0xc0 - 0xe8       */
  volatile char pad__6[0x8];                      /*       0xe8 - 0xf0       */
  volatile u_int32_t MAC_DCU_GBL_IFS_MISC;        /*       0xf0 - 0xf4       */
  volatile char pad__7[0x4];                      /*       0xf4 - 0xf8       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_127_96;
                                                  /*       0xf8 - 0xfc       */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_127_96;
                                                  /*       0xfc - 0x100      */
  volatile u_int32_t MAC_DCU_MISC[10];            /*      0x100 - 0x128      */
  volatile char pad__8[0x10];                     /*      0x128 - 0x138      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_31_0;  /*      0x138 - 0x13c      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_31_0;  /*      0x13c - 0x140      */
  volatile u_int32_t MAC_DCU_SEQ;                 /*      0x140 - 0x144      */
  volatile char pad__9[0x34];                     /*      0x144 - 0x178      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_63_32; /*      0x178 - 0x17c      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_63_32; /*      0x17c - 0x180      */
  volatile char pad__10[0x38];                    /*      0x180 - 0x1b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_95_64; /*      0x1b8 - 0x1bc      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_95_64; /*      0x1bc - 0x1c0      */
  volatile char pad__11[0x38];                    /*      0x1c0 - 0x1f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_127_96;
                                                  /*      0x1f8 - 0x1fc      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_127_96;
                                                  /*      0x1fc - 0x200      */
  volatile char pad__12[0x38];                    /*      0x200 - 0x238      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_31_0;  /*      0x238 - 0x23c      */
  volatile char pad__13[0x34];                    /*      0x23c - 0x270      */
  volatile u_int32_t MAC_DCU_PAUSE;               /*      0x270 - 0x274      */
  volatile char pad__14[0x4];                     /*      0x274 - 0x278      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_63_32; /*      0x278 - 0x27c      */
  volatile char pad__15[0x34];                    /*      0x27c - 0x2b0      */
  volatile u_int32_t MAC_DCU_WOW_KACFG;           /*      0x2b0 - 0x2b4      */
  volatile char pad__16[0x4];                     /*      0x2b4 - 0x2b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_95_64; /*      0x2b8 - 0x2bc      */
  volatile char pad__17[0x34];                    /*      0x2bc - 0x2f0      */
  volatile u_int32_t MAC_DCU_TXSLOT;              /*      0x2f0 - 0x2f4      */
  volatile char pad__18[0x4];                     /*      0x2f4 - 0x2f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_127_96;
                                                  /*      0x2f8 - 0x2fc      */
  volatile char pad__19[0x3c];                    /*      0x2fc - 0x338      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_31_0;  /*      0x338 - 0x33c      */
  volatile char pad__20[0x3c];                    /*      0x33c - 0x378      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_63_32; /*      0x378 - 0x37c      */
  volatile char pad__21[0x3c];                    /*      0x37c - 0x3b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_95_64; /*      0x3b8 - 0x3bc      */
  volatile char pad__22[0x3c];                    /*      0x3bc - 0x3f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_127_96;
                                                  /*      0x3f8 - 0x3fc      */
  volatile char pad__23[0x3c];                    /*      0x3fc - 0x438      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_31_0;  /*      0x438 - 0x43c      */
  volatile u_int32_t MAC_DCU_TXFILTER_CLEAR;      /*      0x43c - 0x440      */
  volatile char pad__24[0x38];                    /*      0x440 - 0x478      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_63_32; /*      0x478 - 0x47c      */
  volatile u_int32_t MAC_DCU_TXFILTER_SET;        /*      0x47c - 0x480      */
  volatile char pad__25[0x38];                    /*      0x480 - 0x4b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_95_64; /*      0x4b8 - 0x4bc      */
  volatile char pad__26[0x3c];                    /*      0x4bc - 0x4f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_127_96;
                                                  /*      0x4f8 - 0x4fc      */
  volatile char pad__27[0x3c];                    /*      0x4fc - 0x538      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_31_0;  /*      0x538 - 0x53c      */
  volatile char pad__28[0x3c];                    /*      0x53c - 0x578      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_63_32; /*      0x578 - 0x57c      */
  volatile char pad__29[0x3c];                    /*      0x57c - 0x5b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_95_64; /*      0x5b8 - 0x5bc      */
  volatile char pad__30[0x3c];                    /*      0x5bc - 0x5f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_127_96;
                                                  /*      0x5f8 - 0x5fc      */
  volatile char pad__31[0x3c];                    /*      0x5fc - 0x638      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_31_0;  /*      0x638 - 0x63c      */
  volatile char pad__32[0x3c];                    /*      0x63c - 0x678      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_63_32; /*      0x678 - 0x67c      */
  volatile char pad__33[0x3c];                    /*      0x67c - 0x6b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_95_64; /*      0x6b8 - 0x6bc      */
  volatile char pad__34[0x3c];                    /*      0x6bc - 0x6f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_127_96;
                                                  /*      0x6f8 - 0x6fc      */
  volatile char pad__35[0x3c];                    /*      0x6fc - 0x738      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_31_0;  /*      0x738 - 0x73c      */
  volatile char pad__36[0x3c];                    /*      0x73c - 0x778      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_63_32; /*      0x778 - 0x77c      */
  volatile char pad__37[0x3c];                    /*      0x77c - 0x7b8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_95_64; /*      0x7b8 - 0x7bc      */
  volatile char pad__38[0x3c];                    /*      0x7bc - 0x7f8      */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_127_96;
                                                  /*      0x7f8 - 0x7fc      */
  volatile char pad__39[0x704];                   /*      0x7fc - 0xf00      */
  volatile u_int32_t MAC_SLEEP_STATUS;            /*      0xf00 - 0xf04      */
  volatile u_int32_t MAC_LED_CONFIG;              /*      0xf04 - 0xf08      */
};

struct rtc_reg {
  volatile u_int32_t RESET_CONTROL;               /*        0x0 - 0x4        */
  volatile u_int32_t XTAL_CONTROL;                /*        0x4 - 0x8        */
  volatile u_int32_t REG_CONTROL0;                /*        0x8 - 0xc        */
  volatile u_int32_t REG_CONTROL1;                /*        0xc - 0x10       */
  volatile u_int32_t QUADRATURE;                  /*       0x10 - 0x14       */
  volatile u_int32_t PLL_CONTROL;                 /*       0x14 - 0x18       */
  volatile u_int32_t PLL_SETTLE;                  /*       0x18 - 0x1c       */
  volatile u_int32_t XTAL_SETTLE;                 /*       0x1c - 0x20       */
  volatile u_int32_t CLOCK_OUT;                   /*       0x20 - 0x24       */
  volatile u_int32_t BIAS_OVERRIDE;               /*       0x24 - 0x28       */
  volatile u_int32_t RESET_CAUSE;                 /*       0x28 - 0x2c       */
  volatile u_int32_t SYSTEM_SLEEP;                /*       0x2c - 0x30       */
  volatile u_int32_t MAC_SLEEP_CONTROL;           /*       0x30 - 0x34       */
  volatile u_int32_t KEEP_AWAKE;                  /*       0x34 - 0x38       */
  volatile u_int32_t DERIVED_RTC_CLK;             /*       0x38 - 0x3c       */
  volatile u_int32_t PLL_CONTROL2;                /*       0x3c - 0x40       */
};

struct rtc_sync_reg {
  volatile u_int32_t RTC_SYNC_RESET;              /*        0x0 - 0x4        */
  volatile u_int32_t RTC_SYNC_STATUS;             /*        0x4 - 0x8        */
  volatile u_int32_t RTC_SYNC_DERIVED;            /*        0x8 - 0xc        */
  volatile u_int32_t RTC_SYNC_FORCE_WAKE;         /*        0xc - 0x10       */
  volatile u_int32_t RTC_SYNC_INTR_CAUSE;         /*       0x10 - 0x14       */
  volatile u_int32_t RTC_SYNC_INTR_ENABLE;        /*       0x14 - 0x18       */
  volatile u_int32_t RTC_SYNC_INTR_MASK;          /*       0x18 - 0x1c       */
};

struct mac_pcu_reg {
  volatile u_int32_t MAC_PCU_STA_ADDR_L32;        /*        0x0 - 0x4        */
  volatile u_int32_t MAC_PCU_STA_ADDR_U16;        /*        0x4 - 0x8        */
  volatile u_int32_t MAC_PCU_BSSID_L32;           /*        0x8 - 0xc        */
  volatile u_int32_t MAC_PCU_BSSID_U16;           /*        0xc - 0x10       */
  volatile u_int32_t MAC_PCU_BCN_RSSI_AVE;        /*       0x10 - 0x14       */
  volatile u_int32_t MAC_PCU_ACK_CTS_TIMEOUT;     /*       0x14 - 0x18       */
  volatile u_int32_t MAC_PCU_BCN_RSSI_CTL;        /*       0x18 - 0x1c       */
  volatile u_int32_t MAC_PCU_USEC_LATENCY;        /*       0x1c - 0x20       */
  volatile u_int32_t MAC_PCU_RESET_TSF;           /*       0x20 - 0x24       */
  volatile char pad__0[0x14];                     /*       0x24 - 0x38       */
  volatile u_int32_t MAC_PCU_MAX_CFP_DUR;         /*       0x38 - 0x3c       */
  volatile u_int32_t MAC_PCU_RX_FILTER;           /*       0x3c - 0x40       */
  volatile u_int32_t MAC_PCU_MCAST_FILTER_L32;    /*       0x40 - 0x44       */
  volatile u_int32_t MAC_PCU_MCAST_FILTER_U32;    /*       0x44 - 0x48       */
  volatile u_int32_t MAC_PCU_DIAG_SW;             /*       0x48 - 0x4c       */
  volatile u_int32_t MAC_PCU_TSF_L32;             /*       0x4c - 0x50       */
  volatile u_int32_t MAC_PCU_TSF_U32;             /*       0x50 - 0x54       */
  volatile u_int32_t MAC_PCU_TST_ADDAC;           /*       0x54 - 0x58       */
  volatile u_int32_t MAC_PCU_DEF_ANTENNA;         /*       0x58 - 0x5c       */
  volatile u_int32_t MAC_PCU_AES_MUTE_MASK_0;     /*       0x5c - 0x60       */
  volatile u_int32_t MAC_PCU_AES_MUTE_MASK_1;     /*       0x60 - 0x64       */
  volatile u_int32_t MAC_PCU_GATED_CLKS;          /*       0x64 - 0x68       */
  volatile u_int32_t MAC_PCU_OBS_BUS_2;           /*       0x68 - 0x6c       */
  volatile u_int32_t MAC_PCU_OBS_BUS_1;           /*       0x6c - 0x70       */
  volatile u_int32_t MAC_PCU_DYM_MIMO_PWR_SAVE;   /*       0x70 - 0x74       */
  volatile u_int32_t MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_LSB;
                                                  /*       0x74 - 0x78       */
  volatile u_int32_t MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_MSB;
                                                  /*       0x78 - 0x7c       */
  volatile char pad__1[0x4];                      /*       0x7c - 0x80       */
  volatile u_int32_t MAC_PCU_LAST_BEACON_TSF;     /*       0x80 - 0x84       */
  volatile u_int32_t MAC_PCU_NAV;                 /*       0x84 - 0x88       */
  volatile u_int32_t MAC_PCU_RTS_SUCCESS_CNT;     /*       0x88 - 0x8c       */
  volatile u_int32_t MAC_PCU_RTS_FAIL_CNT;        /*       0x8c - 0x90       */
  volatile u_int32_t MAC_PCU_ACK_FAIL_CNT;        /*       0x90 - 0x94       */
  volatile u_int32_t MAC_PCU_FCS_FAIL_CNT;        /*       0x94 - 0x98       */
  volatile u_int32_t MAC_PCU_BEACON_CNT;          /*       0x98 - 0x9c       */
  volatile u_int32_t MAC_PCU_TDMA_SLOT_ALERT_CNTL;
                                                  /*       0x9c - 0xa0       */
  volatile u_int32_t MAC_PCU_BASIC_SET;           /*       0xa0 - 0xa4       */
  volatile u_int32_t MAC_PCU_MGMT_SEQ;            /*       0xa4 - 0xa8       */
  volatile u_int32_t MAC_PCU_BF_RPT1;             /*       0xa8 - 0xac       */
  volatile u_int32_t MAC_PCU_BF_RPT2;             /*       0xac - 0xb0       */
  volatile u_int32_t MAC_PCU_TX_ANT_1;            /*       0xb0 - 0xb4       */
  volatile u_int32_t MAC_PCU_TX_ANT_2;            /*       0xb4 - 0xb8       */
  volatile u_int32_t MAC_PCU_TX_ANT_3;            /*       0xb8 - 0xbc       */
  volatile u_int32_t MAC_PCU_TX_ANT_4;            /*       0xbc - 0xc0       */
  volatile u_int32_t MAC_PCU_XRMODE;              /*       0xc0 - 0xc4       */
  volatile u_int32_t MAC_PCU_XRDEL;               /*       0xc4 - 0xc8       */
  volatile u_int32_t MAC_PCU_XRTO;                /*       0xc8 - 0xcc       */
  volatile u_int32_t MAC_PCU_XRCRP;               /*       0xcc - 0xd0       */
  volatile u_int32_t MAC_PCU_XRSTMP;              /*       0xd0 - 0xd4       */
  volatile u_int32_t MAC_PCU_SLP1;                /*       0xd4 - 0xd8       */
  volatile u_int32_t MAC_PCU_SLP2;                /*       0xd8 - 0xdc       */
  volatile u_int32_t MAC_PCU_SELF_GEN_DEFAULT;    /*       0xdc - 0xe0       */
  volatile u_int32_t MAC_PCU_ADDR1_MASK_L32;      /*       0xe0 - 0xe4       */
  volatile u_int32_t MAC_PCU_ADDR1_MASK_U16;      /*       0xe4 - 0xe8       */
  volatile u_int32_t MAC_PCU_TPC;                 /*       0xe8 - 0xec       */
  volatile u_int32_t MAC_PCU_TX_FRAME_CNT;        /*       0xec - 0xf0       */
  volatile u_int32_t MAC_PCU_RX_FRAME_CNT;        /*       0xf0 - 0xf4       */
  volatile u_int32_t MAC_PCU_RX_CLEAR_CNT;        /*       0xf4 - 0xf8       */
  volatile u_int32_t MAC_PCU_CYCLE_CNT;           /*       0xf8 - 0xfc       */
  volatile u_int32_t MAC_PCU_QUIET_TIME_1;        /*       0xfc - 0x100      */
  volatile u_int32_t MAC_PCU_QUIET_TIME_2;        /*      0x100 - 0x104      */
  volatile char pad__2[0x4];                      /*      0x104 - 0x108      */
  volatile u_int32_t MAC_PCU_QOS_NO_ACK;          /*      0x108 - 0x10c      */
  volatile u_int32_t MAC_PCU_PHY_ERROR_MASK;      /*      0x10c - 0x110      */
  volatile u_int32_t MAC_PCU_XRLAT;               /*      0x110 - 0x114      */
  volatile u_int32_t MAC_PCU_RXBUF;               /*      0x114 - 0x118      */
  volatile u_int32_t MAC_PCU_MIC_QOS_CONTROL;     /*      0x118 - 0x11c      */
  volatile u_int32_t MAC_PCU_MIC_QOS_SELECT;      /*      0x11c - 0x120      */
  volatile u_int32_t MAC_PCU_MISC_MODE;           /*      0x120 - 0x124      */
  volatile u_int32_t MAC_PCU_FILTER_OFDM_CNT;     /*      0x124 - 0x128      */
  volatile u_int32_t MAC_PCU_FILTER_CCK_CNT;      /*      0x128 - 0x12c      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_1;       /*      0x12c - 0x130      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_1_MASK;  /*      0x130 - 0x134      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_2;       /*      0x134 - 0x138      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_2_MASK;  /*      0x138 - 0x13c      */
  volatile u_int32_t MAC_PCU_TSF_THRESHOLD;       /*      0x13c - 0x140      */
  volatile u_int32_t MAC_PCU_MISC_MODE4;          /*      0x140 - 0x144      */
  volatile u_int32_t MAC_PCU_PHY_ERROR_EIFS_MASK; /*      0x144 - 0x148      */
  volatile char pad__3[0x20];                     /*      0x148 - 0x168      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_3;       /*      0x168 - 0x16c      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_3_MASK;  /*      0x16c - 0x170      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE;      /*      0x170 - 0x174      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_WL_WEIGHTS0;
                                                  /*      0x174 - 0x178      */
  volatile u_int32_t MAC_PCU_HCF_TIMEOUT;         /*      0x178 - 0x17c      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE2;     /*      0x17c - 0x180      */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS2[16]; /*      0x180 - 0x1c0      */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS2_MODE;
                                                  /*      0x1c0 - 0x1c4      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_WL_WEIGHTS1;
                                                  /*      0x1c4 - 0x1c8      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_TSF_BT_ACTIVE;
                                                  /*      0x1c8 - 0x1cc      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_TSF_BT_PRIORITY;
                                                  /*      0x1cc - 0x1d0      */
  volatile u_int32_t MAC_PCU_TXSIFS;              /*      0x1d0 - 0x1d4      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE3;     /*      0x1d4 - 0x1d8      */
  volatile char pad__4[0x14];                     /*      0x1d8 - 0x1ec      */
  volatile u_int32_t MAC_PCU_TXOP_X;              /*      0x1ec - 0x1f0      */
  volatile u_int32_t MAC_PCU_TXOP_0_3;            /*      0x1f0 - 0x1f4      */
  volatile u_int32_t MAC_PCU_TXOP_4_7;            /*      0x1f4 - 0x1f8      */
  volatile u_int32_t MAC_PCU_TXOP_8_11;           /*      0x1f8 - 0x1fc      */
  volatile u_int32_t MAC_PCU_TXOP_12_15;          /*      0x1fc - 0x200      */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS[16];  /*      0x200 - 0x240      */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS_MODE; /*      0x240 - 0x244      */
  volatile u_int32_t MAC_PCU_SLP32_MODE;          /*      0x244 - 0x248      */
  volatile u_int32_t MAC_PCU_SLP32_WAKE;          /*      0x248 - 0x24c      */
  volatile u_int32_t MAC_PCU_SLP32_INC;           /*      0x24c - 0x250      */
  volatile u_int32_t MAC_PCU_SLP_MIB1;            /*      0x250 - 0x254      */
  volatile u_int32_t MAC_PCU_SLP_MIB2;            /*      0x254 - 0x258      */
  volatile u_int32_t MAC_PCU_SLP_MIB3;            /*      0x258 - 0x25c      */
  volatile u_int32_t MAC_PCU_WOW1;                /*      0x25c - 0x260      */
  volatile u_int32_t MAC_PCU_WOW2;                /*      0x260 - 0x264      */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER;      /*      0x264 - 0x268      */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER_32L;  /*      0x268 - 0x26c      */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER_16U;  /*      0x26c - 0x270      */
  volatile u_int32_t MAC_PCU_WOW3_BEACON_FAIL;    /*      0x270 - 0x274      */
  volatile u_int32_t MAC_PCU_WOW3_BEACON;         /*      0x274 - 0x278      */
  volatile u_int32_t MAC_PCU_WOW3_KEEP_ALIVE;     /*      0x278 - 0x27c      */
  volatile u_int32_t MAC_PCU_WOW_KA;              /*      0x27c - 0x280      */
  volatile char pad__5[0x4];                      /*      0x280 - 0x284      */
  volatile u_int32_t PCU_1US;                     /*      0x284 - 0x288      */
  volatile u_int32_t PCU_KA;                      /*      0x288 - 0x28c      */
  volatile u_int32_t WOW_EXACT;                   /*      0x28c - 0x290      */
  volatile char pad__6[0x4];                      /*      0x290 - 0x294      */
  volatile u_int32_t PCU_WOW4;                    /*      0x294 - 0x298      */
  volatile u_int32_t PCU_WOW5;                    /*      0x298 - 0x29c      */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_MASK_CONT;
                                                  /*      0x29c - 0x2a0      */
  volatile char pad__7[0x60];                     /*      0x2a0 - 0x300      */
  volatile u_int32_t MAC_PCU_AZIMUTH_MODE;        /*      0x300 - 0x304      */
  volatile char pad__8[0x10];                     /*      0x304 - 0x314      */
  volatile u_int32_t MAC_PCU_AZIMUTH_TIME_STAMP;  /*      0x314 - 0x318      */
  volatile u_int32_t MAC_PCU_20_40_MODE;          /*      0x318 - 0x31c      */
  volatile u_int32_t MAC_PCU_H_XFER_TIMEOUT;      /*      0x31c - 0x320      */
  volatile char pad__9[0x8];                      /*      0x320 - 0x328      */
  volatile u_int32_t MAC_PCU_RX_CLEAR_DIFF_CNT;   /*      0x328 - 0x32c      */
  volatile u_int32_t MAC_PCU_SELF_GEN_ANTENNA_MASK;
                                                  /*      0x32c - 0x330      */
  volatile u_int32_t MAC_PCU_BA_BAR_CONTROL;      /*      0x330 - 0x334      */
  volatile u_int32_t MAC_PCU_LEGACY_PLCP_SPOOF;   /*      0x334 - 0x338      */
  volatile u_int32_t MAC_PCU_PHY_ERROR_MASK_CONT; /*      0x338 - 0x33c      */
  volatile u_int32_t MAC_PCU_TX_TIMER;            /*      0x33c - 0x340      */
  volatile u_int32_t MAC_PCU_TXBUF_CTRL;          /*      0x340 - 0x344      */
  volatile u_int32_t MAC_PCU_MISC_MODE2;          /*      0x344 - 0x348      */
  volatile u_int32_t MAC_PCU_ALT_AES_MUTE_MASK;   /*      0x348 - 0x34c      */
  volatile u_int32_t MAC_PCU_WOW6;                /*      0x34c - 0x350      */
  volatile u_int32_t ASYNC_FIFO_REG1;             /*      0x350 - 0x354      */
  volatile u_int32_t ASYNC_FIFO_REG2;             /*      0x354 - 0x358      */
  volatile u_int32_t ASYNC_FIFO_REG3;             /*      0x358 - 0x35c      */
  volatile u_int32_t MAC_PCU_WOW5;                /*      0x35c - 0x360      */
  volatile u_int32_t MAC_PCU_WOW_LENGTH1;         /*      0x360 - 0x364      */
  volatile u_int32_t MAC_PCU_WOW_LENGTH2;         /*      0x364 - 0x368      */
  volatile u_int32_t WOW_PATTERN_MATCH_LESS_THAN_256_BYTES;
                                                  /*      0x368 - 0x36c      */
  volatile char pad__10[0x4];                     /*      0x36c - 0x370      */
  volatile u_int32_t MAC_PCU_WOW4;                /*      0x370 - 0x374      */
  volatile u_int32_t WOW2_EXACT;                  /*      0x374 - 0x378      */
  volatile u_int32_t PCU_WOW6;                    /*      0x378 - 0x37c      */
  volatile u_int32_t PCU_WOW7;                    /*      0x37c - 0x380      */
  volatile u_int32_t MAC_PCU_WOW_LENGTH3;         /*      0x380 - 0x384      */
  volatile u_int32_t MAC_PCU_WOW_LENGTH4;         /*      0x384 - 0x388      */
  volatile u_int32_t MAC_PCU_LOCATION_MODE_CONTROL;
                                                  /*      0x388 - 0x38c      */
  volatile u_int32_t MAC_PCU_LOCATION_MODE_TIMER; /*      0x38c - 0x390      */
  volatile u_int32_t MAC_PCU_TSF2_L32;            /*      0x390 - 0x394      */
  volatile u_int32_t MAC_PCU_TSF2_U32;            /*      0x394 - 0x398      */
  volatile u_int32_t MAC_PCU_BSSID2_L32;          /*      0x398 - 0x39c      */
  volatile u_int32_t MAC_PCU_BSSID2_U16;          /*      0x39c - 0x3a0      */
  volatile u_int32_t MAC_PCU_DIRECT_CONNECT;      /*      0x3a0 - 0x3a4      */
  volatile u_int32_t MAC_PCU_TID_TO_AC;           /*      0x3a4 - 0x3a8      */
  volatile u_int32_t MAC_PCU_HP_QUEUE;            /*      0x3a8 - 0x3ac      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS0;
                                                  /*      0x3ac - 0x3b0      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS1;
                                                  /*      0x3b0 - 0x3b4      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS2;
                                                  /*      0x3b4 - 0x3b8      */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS3;
                                                  /*      0x3b8 - 0x3bc      */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT0; /*      0x3bc - 0x3c0      */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT1; /*      0x3c0 - 0x3c4      */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT2; /*      0x3c4 - 0x3c8      */
  volatile u_int32_t MAC_PCU_HW_BCN_PROC1;        /*      0x3c8 - 0x3cc      */
  volatile u_int32_t MAC_PCU_HW_BCN_PROC2;        /*      0x3cc - 0x3d0      */
  volatile u_int32_t MAC_PCU_MISC_MODE3;          /*      0x3d0 - 0x3d4      */
  volatile u_int32_t MAC_PCU_FILTER_RSSI_AVE;     /*      0x3d4 - 0x3d8      */
  volatile u_int32_t MAC_PCU_PHY_ERROR_AIFS_MASK; /*      0x3d8 - 0x3dc      */
  volatile u_int32_t MAC_PCU_PS_FILTER;           /*      0x3dc - 0x3e0      */
  volatile char pad__11[0x20];                    /*      0x3e0 - 0x400      */
  volatile u_int32_t MAC_PCU_TXBUF_BA[64];        /*      0x400 - 0x500      */
  volatile char pad__12[0x300];                   /*      0x500 - 0x800      */
  volatile u_int32_t MAC_PCU_KEY_CACHE[1024];     /*      0x800 - 0x1800     */
};

struct chn_reg_map {
  volatile u_int32_t BB_timing_controls_1;        /*        0x0 - 0x4        */
  volatile u_int32_t BB_timing_controls_2;        /*        0x4 - 0x8        */
  volatile u_int32_t BB_timing_controls_3;        /*        0x8 - 0xc        */
  volatile u_int32_t BB_timing_control_4;         /*        0xc - 0x10       */
  volatile u_int32_t BB_timing_control_5;         /*       0x10 - 0x14       */
  volatile u_int32_t BB_timing_control_6;         /*       0x14 - 0x18       */
  volatile u_int32_t BB_timing_control_11;        /*       0x18 - 0x1c       */
  volatile u_int32_t BB_spur_mask_controls;       /*       0x1c - 0x20       */
  volatile u_int32_t BB_find_signal_low;          /*       0x20 - 0x24       */
  volatile u_int32_t BB_sfcorr;                   /*       0x24 - 0x28       */
  volatile u_int32_t BB_self_corr_low;            /*       0x28 - 0x2c       */
  volatile u_int32_t BB_ext_chan_scorr_thr;       /*       0x2c - 0x30       */
  volatile u_int32_t BB_ext_chan_pwr_thr_2_b0;    /*       0x30 - 0x34       */
  volatile u_int32_t BB_radar_detection;          /*       0x34 - 0x38       */
  volatile u_int32_t BB_radar_detection_2;        /*       0x38 - 0x3c       */
  volatile u_int32_t BB_extension_radar;          /*       0x3c - 0x40       */
  volatile char pad__0[0x40];                     /*       0x40 - 0x80       */
  volatile u_int32_t BB_multichain_control;       /*       0x80 - 0x84       */
  volatile u_int32_t BB_per_chain_csd;            /*       0x84 - 0x88       */
  volatile char pad__1[0x18];                     /*       0x88 - 0xa0       */
  volatile u_int32_t BB_tx_crc;                   /*       0xa0 - 0xa4       */
  volatile u_int32_t BB_tstdac_constant;          /*       0xa4 - 0xa8       */
  volatile u_int32_t BB_spur_report_b0;           /*       0xa8 - 0xac       */
  volatile char pad__2[0x4];                      /*       0xac - 0xb0       */
  volatile u_int32_t BB_txiqcal_control_3;        /*       0xb0 - 0xb4       */
  volatile char pad__3[0x8];                      /*       0xb4 - 0xbc       */
  volatile u_int32_t BB_green_tx_control_1;       /*       0xbc - 0xc0       */
  volatile u_int32_t BB_iq_adc_meas_0_b0;         /*       0xc0 - 0xc4       */
  volatile u_int32_t BB_iq_adc_meas_1_b0;         /*       0xc4 - 0xc8       */
  volatile u_int32_t BB_iq_adc_meas_2_b0;         /*       0xc8 - 0xcc       */
  volatile u_int32_t BB_iq_adc_meas_3_b0;         /*       0xcc - 0xd0       */
  volatile u_int32_t BB_tx_phase_ramp_b0;         /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_adc_gain_dc_corr_b0;      /*       0xd4 - 0xd8       */
  volatile char pad__4[0x4];                      /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_rx_iq_corr_b0;            /*       0xdc - 0xe0       */
  volatile char pad__5[0x4];                      /*       0xe0 - 0xe4       */
  volatile u_int32_t BB_paprd_am2am_mask;         /*       0xe4 - 0xe8       */
  volatile u_int32_t BB_paprd_am2pm_mask;         /*       0xe8 - 0xec       */
  volatile u_int32_t BB_paprd_ht40_mask;          /*       0xec - 0xf0       */
  volatile u_int32_t BB_paprd_ctrl0_b0;           /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_paprd_ctrl1_b0;           /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_pa_gain123_b0;            /*       0xf8 - 0xfc       */
  volatile u_int32_t BB_pa_gain45_b0;             /*       0xfc - 0x100      */
  volatile u_int32_t BB_paprd_pre_post_scale_0_b0;
                                                  /*      0x100 - 0x104      */
  volatile u_int32_t BB_paprd_pre_post_scale_1_b0;
                                                  /*      0x104 - 0x108      */
  volatile u_int32_t BB_paprd_pre_post_scale_2_b0;
                                                  /*      0x108 - 0x10c      */
  volatile u_int32_t BB_paprd_pre_post_scale_3_b0;
                                                  /*      0x10c - 0x110      */
  volatile u_int32_t BB_paprd_pre_post_scale_4_b0;
                                                  /*      0x110 - 0x114      */
  volatile u_int32_t BB_paprd_pre_post_scale_5_b0;
                                                  /*      0x114 - 0x118      */
  volatile u_int32_t BB_paprd_pre_post_scale_6_b0;
                                                  /*      0x118 - 0x11c      */
  volatile u_int32_t BB_paprd_pre_post_scale_7_b0;
                                                  /*      0x11c - 0x120      */
  volatile u_int32_t BB_paprd_mem_tab_b0[120];    /*      0x120 - 0x300      */
  volatile u_int32_t BB_chan_info_chan_tab_b0[60];
                                                  /*      0x300 - 0x3f0      */
  volatile u_int32_t BB_chn_tables_intf_addr;     /*      0x3f0 - 0x3f4      */
  volatile u_int32_t BB_chn_tables_intf_data;     /*      0x3f4 - 0x3f8      */
};

struct mrc_reg_map {
  volatile u_int32_t BB_timing_control_3a;        /*        0x0 - 0x4        */
  volatile u_int32_t BB_ldpc_cntl1;               /*        0x4 - 0x8        */
  volatile u_int32_t BB_ldpc_cntl2;               /*        0x8 - 0xc        */
  volatile u_int32_t BB_pilot_spur_mask;          /*        0xc - 0x10       */
  volatile u_int32_t BB_chan_spur_mask;           /*       0x10 - 0x14       */
  volatile u_int32_t BB_short_gi_delta_slope;     /*       0x14 - 0x18       */
  volatile u_int32_t BB_ml_cntl1;                 /*       0x18 - 0x1c       */
  volatile u_int32_t BB_ml_cntl2;                 /*       0x1c - 0x20       */
  volatile u_int32_t BB_tstadc;                   /*       0x20 - 0x24       */
};

struct bbb_reg_map {
  volatile u_int32_t BB_bbb_rx_ctrl_1;            /*        0x0 - 0x4        */
  volatile u_int32_t BB_bbb_rx_ctrl_2;            /*        0x4 - 0x8        */
  volatile u_int32_t BB_bbb_rx_ctrl_3;            /*        0x8 - 0xc        */
  volatile u_int32_t BB_bbb_rx_ctrl_4;            /*        0xc - 0x10       */
  volatile u_int32_t BB_bbb_rx_ctrl_5;            /*       0x10 - 0x14       */
  volatile u_int32_t BB_bbb_rx_ctrl_6;            /*       0x14 - 0x18       */
  volatile u_int32_t BB_force_clken_cck;          /*       0x18 - 0x1c       */
};

struct agc_reg_map {
  volatile u_int32_t BB_settling_time;            /*        0x0 - 0x4        */
  volatile u_int32_t BB_gain_force_max_gains_b0;  /*        0x4 - 0x8        */
  volatile u_int32_t BB_gains_min_offsets;        /*        0x8 - 0xc        */
  volatile u_int32_t BB_desired_sigsize;          /*        0xc - 0x10       */
  volatile u_int32_t BB_find_signal;              /*       0x10 - 0x14       */
  volatile u_int32_t BB_agc;                      /*       0x14 - 0x18       */
  volatile u_int32_t BB_ext_atten_switch_ctl_b0;  /*       0x18 - 0x1c       */
  volatile u_int32_t BB_cca_b0;                   /*       0x1c - 0x20       */
  volatile u_int32_t BB_cca_ctrl_2_b0;            /*       0x20 - 0x24       */
  volatile u_int32_t BB_restart;                  /*       0x24 - 0x28       */
  volatile u_int32_t BB_multichain_gain_ctrl;     /*       0x28 - 0x2c       */
  volatile u_int32_t BB_ext_chan_pwr_thr_1;       /*       0x2c - 0x30       */
  volatile u_int32_t BB_ext_chan_detect_win;      /*       0x30 - 0x34       */
  volatile u_int32_t BB_pwr_thr_20_40_det;        /*       0x34 - 0x38       */
  volatile u_int32_t BB_rifs_srch;                /*       0x38 - 0x3c       */
  volatile u_int32_t BB_peak_det_ctrl_1;          /*       0x3c - 0x40       */
  volatile u_int32_t BB_peak_det_ctrl_2;          /*       0x40 - 0x44       */
  volatile u_int32_t BB_rx_gain_bounds_1;         /*       0x44 - 0x48       */
  volatile u_int32_t BB_rx_gain_bounds_2;         /*       0x48 - 0x4c       */
  volatile u_int32_t BB_peak_det_cal_ctrl;        /*       0x4c - 0x50       */
  volatile u_int32_t BB_agc_dig_dc_ctrl;          /*       0x50 - 0x54       */
  volatile u_int32_t BB_bt_coex_1;                /*       0x54 - 0x58       */
  volatile u_int32_t BB_bt_coex_2;                /*       0x58 - 0x5c       */
  volatile u_int32_t BB_bt_coex_3;                /*       0x5c - 0x60       */
  volatile u_int32_t BB_bt_coex_4;                /*       0x60 - 0x64       */
  volatile u_int32_t BB_bt_coex_5;                /*       0x64 - 0x68       */
  volatile u_int32_t BB_redpwr_ctrl_1;            /*       0x68 - 0x6c       */
  volatile u_int32_t BB_redpwr_ctrl_2;            /*       0x6c - 0x70       */
  volatile char pad__0[0x110];                    /*       0x70 - 0x180      */
  volatile u_int32_t BB_rssi_b0;                  /*      0x180 - 0x184      */
  volatile u_int32_t BB_spur_est_cck_report_b0;   /*      0x184 - 0x188      */
  volatile u_int32_t BB_agc_dig_dc_status_i_b0;   /*      0x188 - 0x18c      */
  volatile u_int32_t BB_agc_dig_dc_status_q_b0;   /*      0x18c - 0x190      */
  volatile u_int32_t BB_dc_cal_status_b0;         /*      0x190 - 0x194      */
  volatile char pad__1[0x2c];                     /*      0x194 - 0x1c0      */
  volatile u_int32_t BB_bbb_sig_detect;           /*      0x1c0 - 0x1c4      */
  volatile u_int32_t BB_bbb_dagc_ctrl;            /*      0x1c4 - 0x1c8      */
  volatile u_int32_t BB_iqcorr_ctrl_cck;          /*      0x1c8 - 0x1cc      */
  volatile u_int32_t BB_cck_spur_mit;             /*      0x1cc - 0x1d0      */
  volatile u_int32_t BB_mrc_cck_ctrl;             /*      0x1d0 - 0x1d4      */
  volatile u_int32_t BB_cck_blocker_det;          /*      0x1d4 - 0x1d8      */
  volatile char pad__2[0x28];                     /*      0x1d8 - 0x200      */
  volatile u_int32_t BB_rx_ocgain[128];           /*      0x200 - 0x400      */
};

struct sm_reg_map {
  volatile u_int32_t BB_D2_chip_id;               /*        0x0 - 0x4        */
  volatile u_int32_t BB_gen_controls;             /*        0x4 - 0x8        */
  volatile u_int32_t BB_modes_select;             /*        0x8 - 0xc        */
  volatile u_int32_t BB_active;                   /*        0xc - 0x10       */
  volatile char pad__0[0x10];                     /*       0x10 - 0x20       */
  volatile u_int32_t BB_vit_spur_mask_A;          /*       0x20 - 0x24       */
  volatile u_int32_t BB_vit_spur_mask_B;          /*       0x24 - 0x28       */
  volatile u_int32_t BB_spectral_scan;            /*       0x28 - 0x2c       */
  volatile u_int32_t BB_radar_bw_filter;          /*       0x2c - 0x30       */
  volatile u_int32_t BB_search_start_delay;       /*       0x30 - 0x34       */
  volatile u_int32_t BB_max_rx_length;            /*       0x34 - 0x38       */
  volatile u_int32_t BB_frame_control;            /*       0x38 - 0x3c       */
  volatile u_int32_t BB_rfbus_request;            /*       0x3c - 0x40       */
  volatile u_int32_t BB_rfbus_grant;              /*       0x40 - 0x44       */
  volatile u_int32_t BB_rifs;                     /*       0x44 - 0x48       */
  volatile u_int32_t BB_spectral_scan_2;          /*       0x48 - 0x4c       */
  volatile char pad__1[0x4];                      /*       0x4c - 0x50       */
  volatile u_int32_t BB_rx_clear_delay;           /*       0x50 - 0x54       */
  volatile u_int32_t BB_analog_power_on_time;     /*       0x54 - 0x58       */
  volatile u_int32_t BB_tx_timing_1;              /*       0x58 - 0x5c       */
  volatile u_int32_t BB_tx_timing_2;              /*       0x5c - 0x60       */
  volatile u_int32_t BB_tx_timing_3;              /*       0x60 - 0x64       */
  volatile u_int32_t BB_xpa_timing_control;       /*       0x64 - 0x68       */
  volatile char pad__2[0x18];                     /*       0x68 - 0x80       */
  volatile u_int32_t BB_misc_pa_control;          /*       0x80 - 0x84       */
  volatile u_int32_t BB_switch_table_chn_b0;      /*       0x84 - 0x88       */
  volatile u_int32_t BB_switch_table_com1;        /*       0x88 - 0x8c       */
  volatile u_int32_t BB_switch_table_com2;        /*       0x8c - 0x90       */
  volatile char pad__3[0x10];                     /*       0x90 - 0xa0       */
  volatile u_int32_t BB_multichain_enable;        /*       0xa0 - 0xa4       */
  volatile char pad__4[0x1c];                     /*       0xa4 - 0xc0       */
  volatile u_int32_t BB_cal_chain_mask;           /*       0xc0 - 0xc4       */
  volatile u_int32_t BB_agc_control;              /*       0xc4 - 0xc8       */
  volatile u_int32_t BB_iq_adc_cal_mode;          /*       0xc8 - 0xcc       */
  volatile u_int32_t BB_fcal_1;                   /*       0xcc - 0xd0       */
  volatile u_int32_t BB_fcal_2_b0;                /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_dft_tone_ctrl_b0;         /*       0xd4 - 0xd8       */
  volatile u_int32_t BB_cl_cal_ctrl;              /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_cl_map_0_b0;              /*       0xdc - 0xe0       */
  volatile u_int32_t BB_cl_map_1_b0;              /*       0xe0 - 0xe4       */
  volatile u_int32_t BB_cl_map_2_b0;              /*       0xe4 - 0xe8       */
  volatile u_int32_t BB_cl_map_3_b0;              /*       0xe8 - 0xec       */
  volatile u_int32_t BB_cl_map_pal_0_b0;          /*       0xec - 0xf0       */
  volatile u_int32_t BB_cl_map_pal_1_b0;          /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_cl_map_pal_2_b0;          /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_cl_map_pal_3_b0;          /*       0xf8 - 0xfc       */
  volatile char pad__5[0x4];                      /*       0xfc - 0x100      */
  volatile u_int32_t BB_cl_tab_b0[16];            /*      0x100 - 0x140      */
  volatile u_int32_t BB_synth_control;            /*      0x140 - 0x144      */
  volatile u_int32_t BB_addac_clk_select;         /*      0x144 - 0x148      */
  volatile u_int32_t BB_pll_cntl;                 /*      0x148 - 0x14c      */
  volatile u_int32_t BB_analog_swap;              /*      0x14c - 0x150      */
  volatile u_int32_t BB_addac_parallel_control;   /*      0x150 - 0x154      */
  volatile char pad__6[0x4];                      /*      0x154 - 0x158      */
  volatile u_int32_t BB_force_analog;             /*      0x158 - 0x15c      */
  volatile char pad__7[0x4];                      /*      0x15c - 0x160      */
  volatile u_int32_t BB_test_controls;            /*      0x160 - 0x164      */
  volatile u_int32_t BB_test_controls_status;     /*      0x164 - 0x168      */
  volatile u_int32_t BB_tstdac;                   /*      0x168 - 0x16c      */
  volatile u_int32_t BB_channel_status;           /*      0x16c - 0x170      */
  volatile u_int32_t BB_chaninfo_ctrl;            /*      0x170 - 0x174      */
  volatile u_int32_t BB_chan_info_noise_pwr;      /*      0x174 - 0x178      */
  volatile u_int32_t BB_chan_info_gain_diff;      /*      0x178 - 0x17c      */
  volatile u_int32_t BB_chan_info_fine_timing;    /*      0x17c - 0x180      */
  volatile u_int32_t BB_chan_info_gain_b0;        /*      0x180 - 0x184      */
  volatile char pad__8[0xc];                      /*      0x184 - 0x190      */
  volatile u_int32_t BB_scrambler_seed;           /*      0x190 - 0x194      */
  volatile u_int32_t BB_bbb_tx_ctrl;              /*      0x194 - 0x198      */
  volatile u_int32_t BB_bbb_txfir_0;              /*      0x198 - 0x19c      */
  volatile u_int32_t BB_bbb_txfir_1;              /*      0x19c - 0x1a0      */
  volatile u_int32_t BB_bbb_txfir_2;              /*      0x1a0 - 0x1a4      */
  volatile u_int32_t BB_heavy_clip_ctrl;          /*      0x1a4 - 0x1a8      */
  volatile u_int32_t BB_heavy_clip_20;            /*      0x1a8 - 0x1ac      */
  volatile u_int32_t BB_heavy_clip_40;            /*      0x1ac - 0x1b0      */
  volatile u_int32_t BB_illegal_tx_rate;          /*      0x1b0 - 0x1b4      */
  volatile char pad__9[0xc];                      /*      0x1b4 - 0x1c0      */
  volatile u_int32_t BB_powertx_rate1;            /*      0x1c0 - 0x1c4      */
  volatile u_int32_t BB_powertx_rate2;            /*      0x1c4 - 0x1c8      */
  volatile u_int32_t BB_powertx_rate3;            /*      0x1c8 - 0x1cc      */
  volatile u_int32_t BB_powertx_rate4;            /*      0x1cc - 0x1d0      */
  volatile u_int32_t BB_powertx_rate5;            /*      0x1d0 - 0x1d4      */
  volatile u_int32_t BB_powertx_rate6;            /*      0x1d4 - 0x1d8      */
  volatile u_int32_t BB_powertx_rate7;            /*      0x1d8 - 0x1dc      */
  volatile u_int32_t BB_powertx_rate8;            /*      0x1dc - 0x1e0      */
  volatile u_int32_t BB_powertx_rate9;            /*      0x1e0 - 0x1e4      */
  volatile u_int32_t BB_powertx_rate10;           /*      0x1e4 - 0x1e8      */
  volatile u_int32_t BB_powertx_rate11;           /*      0x1e8 - 0x1ec      */
  volatile u_int32_t BB_powertx_rate12;           /*      0x1ec - 0x1f0      */
  volatile u_int32_t BB_powertx_max;              /*      0x1f0 - 0x1f4      */
  volatile u_int32_t BB_powertx_sub;              /*      0x1f4 - 0x1f8      */
  volatile u_int32_t BB_tpc_1;                    /*      0x1f8 - 0x1fc      */
  volatile u_int32_t BB_tpc_2;                    /*      0x1fc - 0x200      */
  volatile u_int32_t BB_tpc_3;                    /*      0x200 - 0x204      */
  volatile u_int32_t BB_tpc_4_b0;                 /*      0x204 - 0x208      */
  volatile u_int32_t BB_tpc_5_b0;                 /*      0x208 - 0x20c      */
  volatile u_int32_t BB_tpc_6_b0;                 /*      0x20c - 0x210      */
  volatile u_int32_t BB_tpc_7;                    /*      0x210 - 0x214      */
  volatile u_int32_t BB_tpc_8;                    /*      0x214 - 0x218      */
  volatile u_int32_t BB_tpc_9;                    /*      0x218 - 0x21c      */
  volatile u_int32_t BB_tpc_10;                   /*      0x21c - 0x220      */
  volatile u_int32_t BB_tpc_11_b0;                /*      0x220 - 0x224      */
  volatile u_int32_t BB_tpc_12;                   /*      0x224 - 0x228      */
  volatile u_int32_t BB_tpc_13;                   /*      0x228 - 0x22c      */
  volatile u_int32_t BB_tpc_14;                   /*      0x22c - 0x230      */
  volatile u_int32_t BB_tpc_15;                   /*      0x230 - 0x234      */
  volatile u_int32_t BB_tpc_16;                   /*      0x234 - 0x238      */
  volatile u_int32_t BB_tpc_17;                   /*      0x238 - 0x23c      */
  volatile u_int32_t BB_tpc_18;                   /*      0x23c - 0x240      */
  volatile u_int32_t BB_tpc_19_b0;                /*      0x240 - 0x244      */
  volatile u_int32_t BB_tpc_20;                   /*      0x244 - 0x248      */
  volatile u_int32_t BB_therm_adc_1;              /*      0x248 - 0x24c      */
  volatile u_int32_t BB_therm_adc_2;              /*      0x24c - 0x250      */
  volatile u_int32_t BB_therm_adc_3;              /*      0x250 - 0x254      */
  volatile u_int32_t BB_therm_adc_4;              /*      0x254 - 0x258      */
  volatile u_int32_t BB_tx_forced_gain;           /*      0x258 - 0x25c      */
  volatile char pad__10[0x24];                    /*      0x25c - 0x280      */
  volatile u_int32_t BB_pdadc_tab_b0[32];         /*      0x280 - 0x300      */
  volatile u_int32_t BB_tx_gain_tab_1;            /*      0x300 - 0x304      */
  volatile u_int32_t BB_tx_gain_tab_2;            /*      0x304 - 0x308      */
  volatile u_int32_t BB_tx_gain_tab_3;            /*      0x308 - 0x30c      */
  volatile u_int32_t BB_tx_gain_tab_4;            /*      0x30c - 0x310      */
  volatile u_int32_t BB_tx_gain_tab_5;            /*      0x310 - 0x314      */
  volatile u_int32_t BB_tx_gain_tab_6;            /*      0x314 - 0x318      */
  volatile u_int32_t BB_tx_gain_tab_7;            /*      0x318 - 0x31c      */
  volatile u_int32_t BB_tx_gain_tab_8;            /*      0x31c - 0x320      */
  volatile u_int32_t BB_tx_gain_tab_9;            /*      0x320 - 0x324      */
  volatile u_int32_t BB_tx_gain_tab_10;           /*      0x324 - 0x328      */
  volatile u_int32_t BB_tx_gain_tab_11;           /*      0x328 - 0x32c      */
  volatile u_int32_t BB_tx_gain_tab_12;           /*      0x32c - 0x330      */
  volatile u_int32_t BB_tx_gain_tab_13;           /*      0x330 - 0x334      */
  volatile u_int32_t BB_tx_gain_tab_14;           /*      0x334 - 0x338      */
  volatile u_int32_t BB_tx_gain_tab_15;           /*      0x338 - 0x33c      */
  volatile u_int32_t BB_tx_gain_tab_16;           /*      0x33c - 0x340      */
  volatile u_int32_t BB_tx_gain_tab_17;           /*      0x340 - 0x344      */
  volatile u_int32_t BB_tx_gain_tab_18;           /*      0x344 - 0x348      */
  volatile u_int32_t BB_tx_gain_tab_19;           /*      0x348 - 0x34c      */
  volatile u_int32_t BB_tx_gain_tab_20;           /*      0x34c - 0x350      */
  volatile u_int32_t BB_tx_gain_tab_21;           /*      0x350 - 0x354      */
  volatile u_int32_t BB_tx_gain_tab_22;           /*      0x354 - 0x358      */
  volatile u_int32_t BB_tx_gain_tab_23;           /*      0x358 - 0x35c      */
  volatile u_int32_t BB_tx_gain_tab_24;           /*      0x35c - 0x360      */
  volatile u_int32_t BB_tx_gain_tab_25;           /*      0x360 - 0x364      */
  volatile u_int32_t BB_tx_gain_tab_26;           /*      0x364 - 0x368      */
  volatile u_int32_t BB_tx_gain_tab_27;           /*      0x368 - 0x36c      */
  volatile u_int32_t BB_tx_gain_tab_28;           /*      0x36c - 0x370      */
  volatile u_int32_t BB_tx_gain_tab_29;           /*      0x370 - 0x374      */
  volatile u_int32_t BB_tx_gain_tab_30;           /*      0x374 - 0x378      */
  volatile u_int32_t BB_tx_gain_tab_31;           /*      0x378 - 0x37c      */
  volatile u_int32_t BB_tx_gain_tab_32;           /*      0x37c - 0x380      */
  volatile u_int32_t BB_rtt_ctrl;                 /*      0x380 - 0x384      */
  volatile u_int32_t BB_rtt_table_sw_intf_b0;     /*      0x384 - 0x388      */
  volatile u_int32_t BB_rtt_table_sw_intf_1_b0;   /*      0x388 - 0x38c      */
  volatile char pad__11[0x74];                    /*      0x38c - 0x400      */
  volatile u_int32_t BB_caltx_gain_set_0;         /*      0x400 - 0x404      */
  volatile u_int32_t BB_caltx_gain_set_2;         /*      0x404 - 0x408      */
  volatile u_int32_t BB_caltx_gain_set_4;         /*      0x408 - 0x40c      */
  volatile u_int32_t BB_caltx_gain_set_6;         /*      0x40c - 0x410      */
  volatile u_int32_t BB_caltx_gain_set_8;         /*      0x410 - 0x414      */
  volatile u_int32_t BB_caltx_gain_set_10;        /*      0x414 - 0x418      */
  volatile u_int32_t BB_caltx_gain_set_12;        /*      0x418 - 0x41c      */
  volatile u_int32_t BB_caltx_gain_set_14;        /*      0x41c - 0x420      */
  volatile u_int32_t BB_caltx_gain_set_16;        /*      0x420 - 0x424      */
  volatile u_int32_t BB_caltx_gain_set_18;        /*      0x424 - 0x428      */
  volatile u_int32_t BB_caltx_gain_set_20;        /*      0x428 - 0x42c      */
  volatile u_int32_t BB_caltx_gain_set_22;        /*      0x42c - 0x430      */
  volatile u_int32_t BB_caltx_gain_set_24;        /*      0x430 - 0x434      */
  volatile u_int32_t BB_caltx_gain_set_26;        /*      0x434 - 0x438      */
  volatile u_int32_t BB_caltx_gain_set_28;        /*      0x438 - 0x43c      */
  volatile u_int32_t BB_caltx_gain_set_30;        /*      0x43c - 0x440      */
  volatile char pad__12[0x4];                     /*      0x440 - 0x444      */
  volatile u_int32_t BB_txiqcal_control_0;        /*      0x444 - 0x448      */
  volatile u_int32_t BB_txiqcal_control_1;        /*      0x448 - 0x44c      */
  volatile u_int32_t BB_txiqcal_control_2;        /*      0x44c - 0x450      */
  volatile u_int32_t BB_txiq_corr_coeff_01_b0;    /*      0x450 - 0x454      */
  volatile u_int32_t BB_txiq_corr_coeff_23_b0;    /*      0x454 - 0x458      */
  volatile u_int32_t BB_txiq_corr_coeff_45_b0;    /*      0x458 - 0x45c      */
  volatile u_int32_t BB_txiq_corr_coeff_67_b0;    /*      0x45c - 0x460      */
  volatile u_int32_t BB_txiq_corr_coeff_89_b0;    /*      0x460 - 0x464      */
  volatile u_int32_t BB_txiq_corr_coeff_ab_b0;    /*      0x464 - 0x468      */
  volatile u_int32_t BB_txiq_corr_coeff_cd_b0;    /*      0x468 - 0x46c      */
  volatile u_int32_t BB_txiq_corr_coeff_ef_b0;    /*      0x46c - 0x470      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_0;      /*      0x470 - 0x474      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_4;      /*      0x474 - 0x478      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_8;      /*      0x478 - 0x47c      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_12;     /*      0x47c - 0x480      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_16;     /*      0x480 - 0x484      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_20;     /*      0x484 - 0x488      */
  volatile u_int32_t BB_cal_rxbb_gain_tbl_24;     /*      0x488 - 0x48c      */
  volatile u_int32_t BB_txiqcal_status_b0;        /*      0x48c - 0x490      */
  volatile u_int32_t BB_paprd_trainer_cntl1;      /*      0x490 - 0x494      */
  volatile u_int32_t BB_paprd_trainer_cntl2;      /*      0x494 - 0x498      */
  volatile u_int32_t BB_paprd_trainer_cntl3;      /*      0x498 - 0x49c      */
  volatile u_int32_t BB_paprd_trainer_cntl4;      /*      0x49c - 0x4a0      */
  volatile u_int32_t BB_paprd_trainer_stat1;      /*      0x4a0 - 0x4a4      */
  volatile u_int32_t BB_paprd_trainer_stat2;      /*      0x4a4 - 0x4a8      */
  volatile u_int32_t BB_paprd_trainer_stat3;      /*      0x4a8 - 0x4ac      */
  volatile char pad__13[0x114];                   /*      0x4ac - 0x5c0      */
  volatile u_int32_t BB_watchdog_status;          /*      0x5c0 - 0x5c4      */
  volatile u_int32_t BB_watchdog_ctrl_1;          /*      0x5c4 - 0x5c8      */
  volatile u_int32_t BB_watchdog_ctrl_2;          /*      0x5c8 - 0x5cc      */
  volatile u_int32_t BB_bluetooth_cntl;           /*      0x5cc - 0x5d0      */
  volatile u_int32_t BB_phyonly_warm_reset;       /*      0x5d0 - 0x5d4      */
  volatile u_int32_t BB_phyonly_control;          /*      0x5d4 - 0x5d8      */
  volatile char pad__14[0x4];                     /*      0x5d8 - 0x5dc      */
  volatile u_int32_t BB_eco_ctrl;                 /*      0x5dc - 0x5e0      */
  volatile char pad__15[0x10];                    /*      0x5e0 - 0x5f0      */
  volatile u_int32_t BB_tables_intf_addr_b0;      /*      0x5f0 - 0x5f4      */
  volatile u_int32_t BB_tables_intf_data_b0;      /*      0x5f4 - 0x5f8      */
};

struct chn1_reg_map {
  volatile char pad__0[0x30];                     /*        0x0 - 0x30       */
  volatile u_int32_t BB_ext_chan_pwr_thr_2_b1;    /*       0x30 - 0x34       */
  volatile char pad__1[0x74];                     /*       0x34 - 0xa8       */
  volatile u_int32_t BB_spur_report_b1;           /*       0xa8 - 0xac       */
  volatile char pad__2[0x14];                     /*       0xac - 0xc0       */
  volatile u_int32_t BB_iq_adc_meas_0_b1;         /*       0xc0 - 0xc4       */
  volatile u_int32_t BB_iq_adc_meas_1_b1;         /*       0xc4 - 0xc8       */
  volatile u_int32_t BB_iq_adc_meas_2_b1;         /*       0xc8 - 0xcc       */
  volatile u_int32_t BB_iq_adc_meas_3_b1;         /*       0xcc - 0xd0       */
  volatile u_int32_t BB_tx_phase_ramp_b1;         /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_adc_gain_dc_corr_b1;      /*       0xd4 - 0xd8       */
  volatile char pad__3[0x4];                      /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_rx_iq_corr_b1;            /*       0xdc - 0xe0       */
  volatile char pad__4[0x10];                     /*       0xe0 - 0xf0       */
  volatile u_int32_t BB_paprd_ctrl0_b1;           /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_paprd_ctrl1_b1;           /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_pa_gain123_b1;            /*       0xf8 - 0xfc       */
  volatile u_int32_t BB_pa_gain45_b1;             /*       0xfc - 0x100      */
  volatile u_int32_t BB_paprd_pre_post_scale_0_b1;
                                                  /*      0x100 - 0x104      */
  volatile u_int32_t BB_paprd_pre_post_scale_1_b1;
                                                  /*      0x104 - 0x108      */
  volatile u_int32_t BB_paprd_pre_post_scale_2_b1;
                                                  /*      0x108 - 0x10c      */
  volatile u_int32_t BB_paprd_pre_post_scale_3_b1;
                                                  /*      0x10c - 0x110      */
  volatile u_int32_t BB_paprd_pre_post_scale_4_b1;
                                                  /*      0x110 - 0x114      */
  volatile u_int32_t BB_paprd_pre_post_scale_5_b1;
                                                  /*      0x114 - 0x118      */
  volatile u_int32_t BB_paprd_pre_post_scale_6_b1;
                                                  /*      0x118 - 0x11c      */
  volatile u_int32_t BB_paprd_pre_post_scale_7_b1;
                                                  /*      0x11c - 0x120      */
  volatile u_int32_t BB_paprd_mem_tab_b1[120];    /*      0x120 - 0x300      */
  volatile u_int32_t BB_chan_info_chan_tab_b1[60];
                                                  /*      0x300 - 0x3f0      */
  volatile u_int32_t BB_chn1_tables_intf_addr;    /*      0x3f0 - 0x3f4      */
  volatile u_int32_t BB_chn1_tables_intf_data;    /*      0x3f4 - 0x3f8      */
};

struct agc1_reg_map {
  volatile char pad__0[0x4];                      /*        0x0 - 0x4        */
  volatile u_int32_t BB_gain_force_max_gains_b1;  /*        0x4 - 0x8        */
  volatile char pad__1[0x10];                     /*        0x8 - 0x18       */
  volatile u_int32_t BB_ext_atten_switch_ctl_b1;  /*       0x18 - 0x1c       */
  volatile u_int32_t BB_cca_b1;                   /*       0x1c - 0x20       */
  volatile u_int32_t BB_cca_ctrl_2_b1;            /*       0x20 - 0x24       */
  volatile char pad__2[0x15c];                    /*       0x24 - 0x180      */
  volatile u_int32_t BB_rssi_b1;                  /*      0x180 - 0x184      */
  volatile u_int32_t BB_spur_est_cck_report_b1;   /*      0x184 - 0x188      */
  volatile u_int32_t BB_agc_dig_dc_status_i_b1;   /*      0x188 - 0x18c      */
  volatile u_int32_t BB_agc_dig_dc_status_q_b1;   /*      0x18c - 0x190      */
  volatile u_int32_t BB_dc_cal_status_b1;         /*      0x190 - 0x194      */
  volatile char pad__3[0x6c];                     /*      0x194 - 0x200      */
  volatile u_int32_t BB_rx_ocgain2[128];          /*      0x200 - 0x400      */
};

struct sm1_reg_map {
  volatile char pad__0[0x84];                     /*        0x0 - 0x84       */
  volatile u_int32_t BB_switch_table_chn_b1;      /*       0x84 - 0x88       */
  volatile char pad__1[0x48];                     /*       0x88 - 0xd0       */
  volatile u_int32_t BB_fcal_2_b1;                /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_dft_tone_ctrl_b1;         /*       0xd4 - 0xd8       */
  volatile char pad__2[0x4];                      /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_cl_map_0_b1;              /*       0xdc - 0xe0       */
  volatile u_int32_t BB_cl_map_1_b1;              /*       0xe0 - 0xe4       */
  volatile u_int32_t BB_cl_map_2_b1;              /*       0xe4 - 0xe8       */
  volatile u_int32_t BB_cl_map_3_b1;              /*       0xe8 - 0xec       */
  volatile u_int32_t BB_cl_map_pal_0_b1;          /*       0xec - 0xf0       */
  volatile u_int32_t BB_cl_map_pal_1_b1;          /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_cl_map_pal_2_b1;          /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_cl_map_pal_3_b1;          /*       0xf8 - 0xfc       */
  volatile char pad__3[0x4];                      /*       0xfc - 0x100      */
  volatile u_int32_t BB_cl_tab_b1[16];            /*      0x100 - 0x140      */
  volatile char pad__4[0x40];                     /*      0x140 - 0x180      */
  volatile u_int32_t BB_chan_info_gain_b1;        /*      0x180 - 0x184      */
  volatile char pad__5[0x80];                     /*      0x184 - 0x204      */
  volatile u_int32_t BB_tpc_4_b1;                 /*      0x204 - 0x208      */
  volatile u_int32_t BB_tpc_5_b1;                 /*      0x208 - 0x20c      */
  volatile u_int32_t BB_tpc_6_b1;                 /*      0x20c - 0x210      */
  volatile char pad__6[0x10];                     /*      0x210 - 0x220      */
  volatile u_int32_t BB_tpc_11_b1;                /*      0x220 - 0x224      */
  volatile char pad__7[0x1c];                     /*      0x224 - 0x240      */
  volatile u_int32_t BB_tpc_19_b1;                /*      0x240 - 0x244      */
  volatile char pad__8[0x3c];                     /*      0x244 - 0x280      */
  volatile u_int32_t BB_pdadc_tab_b1[32];         /*      0x280 - 0x300      */
  volatile char pad__9[0x84];                     /*      0x300 - 0x384      */
  volatile u_int32_t BB_rtt_table_sw_intf_b1;     /*      0x384 - 0x388      */
  volatile u_int32_t BB_rtt_table_sw_intf_1_b1;   /*      0x388 - 0x38c      */
  volatile char pad__10[0xc4];                    /*      0x38c - 0x450      */
  volatile u_int32_t BB_txiq_corr_coeff_01_b1;    /*      0x450 - 0x454      */
  volatile u_int32_t BB_txiq_corr_coeff_23_b1;    /*      0x454 - 0x458      */
  volatile u_int32_t BB_txiq_corr_coeff_45_b1;    /*      0x458 - 0x45c      */
  volatile u_int32_t BB_txiq_corr_coeff_67_b1;    /*      0x45c - 0x460      */
  volatile u_int32_t BB_txiq_corr_coeff_89_b1;    /*      0x460 - 0x464      */
  volatile u_int32_t BB_txiq_corr_coeff_ab_b1;    /*      0x464 - 0x468      */
  volatile u_int32_t BB_txiq_corr_coeff_cd_b1;    /*      0x468 - 0x46c      */
  volatile u_int32_t BB_txiq_corr_coeff_ef_b1;    /*      0x46c - 0x470      */
  volatile char pad__11[0x1c];                    /*      0x470 - 0x48c      */
  volatile u_int32_t BB_txiqcal_status_b1;        /*      0x48c - 0x490      */
  volatile char pad__12[0x160];                   /*      0x490 - 0x5f0      */
  volatile u_int32_t BB_tables_intf_addr_b1;      /*      0x5f0 - 0x5f4      */
  volatile u_int32_t BB_tables_intf_data_b1;      /*      0x5f4 - 0x5f8      */
};

struct chn2_reg_map {
  volatile char pad__0[0x30];                     /*        0x0 - 0x30       */
  volatile u_int32_t BB_ext_chan_pwr_thr_2_b2;    /*       0x30 - 0x34       */
  volatile char pad__1[0x74];                     /*       0x34 - 0xa8       */
  volatile u_int32_t BB_spur_report_b2;           /*       0xa8 - 0xac       */
  volatile char pad__2[0x14];                     /*       0xac - 0xc0       */
  volatile u_int32_t BB_iq_adc_meas_0_b2;         /*       0xc0 - 0xc4       */
  volatile u_int32_t BB_iq_adc_meas_1_b2;         /*       0xc4 - 0xc8       */
  volatile u_int32_t BB_iq_adc_meas_2_b2;         /*       0xc8 - 0xcc       */
  volatile u_int32_t BB_iq_adc_meas_3_b2;         /*       0xcc - 0xd0       */
  volatile u_int32_t BB_tx_phase_ramp_b2;         /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_adc_gain_dc_corr_b2;      /*       0xd4 - 0xd8       */
  volatile char pad__3[0x4];                      /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_rx_iq_corr_b2;            /*       0xdc - 0xe0       */
  volatile char pad__4[0x10];                     /*       0xe0 - 0xf0       */
  volatile u_int32_t BB_paprd_ctrl0_b2;           /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_paprd_ctrl1_b2;           /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_pa_gain123_b2;            /*       0xf8 - 0xfc       */
  volatile u_int32_t BB_pa_gain45_b2;             /*       0xfc - 0x100      */
  volatile u_int32_t BB_paprd_pre_post_scale_0_b2;
                                                  /*      0x100 - 0x104      */
  volatile u_int32_t BB_paprd_pre_post_scale_1_b2;
                                                  /*      0x104 - 0x108      */
  volatile u_int32_t BB_paprd_pre_post_scale_2_b2;
                                                  /*      0x108 - 0x10c      */
  volatile u_int32_t BB_paprd_pre_post_scale_3_b2;
                                                  /*      0x10c - 0x110      */
  volatile u_int32_t BB_paprd_pre_post_scale_4_b2;
                                                  /*      0x110 - 0x114      */
  volatile u_int32_t BB_paprd_pre_post_scale_5_b2;
                                                  /*      0x114 - 0x118      */
  volatile u_int32_t BB_paprd_pre_post_scale_6_b2;
                                                  /*      0x118 - 0x11c      */
  volatile u_int32_t BB_paprd_pre_post_scale_7_b2;
                                                  /*      0x11c - 0x120      */
  volatile u_int32_t BB_paprd_mem_tab_b2[120];    /*      0x120 - 0x300      */
  volatile u_int32_t BB_chan_info_chan_tab_b2[60];
                                                  /*      0x300 - 0x3f0      */
  volatile u_int32_t BB_chn2_tables_intf_addr;    /*      0x3f0 - 0x3f4      */
  volatile u_int32_t BB_chn2_tables_intf_data;    /*      0x3f4 - 0x3f8      */
};

struct agc2_reg_map {
  volatile char pad__0[0x4];                      /*        0x0 - 0x4        */
  volatile u_int32_t BB_gain_force_max_gains_b2;  /*        0x4 - 0x8        */
  volatile char pad__1[0x10];                     /*        0x8 - 0x18       */
  volatile u_int32_t BB_ext_atten_switch_ctl_b2;  /*       0x18 - 0x1c       */
  volatile u_int32_t BB_cca_b2;                   /*       0x1c - 0x20       */
  volatile u_int32_t BB_cca_ctrl_2_b2;            /*       0x20 - 0x24       */
  volatile char pad__2[0x15c];                    /*       0x24 - 0x180      */
  volatile u_int32_t BB_rssi_b2;                  /*      0x180 - 0x184      */
  volatile char pad__3[0x4];                      /*      0x184 - 0x188      */
  volatile u_int32_t BB_agc_dig_dc_status_i_b2;   /*      0x188 - 0x18c      */
  volatile u_int32_t BB_agc_dig_dc_status_q_b2;   /*      0x18c - 0x190      */
  volatile u_int32_t BB_dc_cal_status_b2;         /*      0x190 - 0x194      */
};

struct sm2_reg_map {
  volatile char pad__0[0x84];                     /*        0x0 - 0x84       */
  volatile u_int32_t BB_switch_table_chn_b2;      /*       0x84 - 0x88       */
  volatile char pad__1[0x48];                     /*       0x88 - 0xd0       */
  volatile u_int32_t BB_fcal_2_b2;                /*       0xd0 - 0xd4       */
  volatile u_int32_t BB_dft_tone_ctrl_b2;         /*       0xd4 - 0xd8       */
  volatile char pad__2[0x4];                      /*       0xd8 - 0xdc       */
  volatile u_int32_t BB_cl_map_0_b2;              /*       0xdc - 0xe0       */
  volatile u_int32_t BB_cl_map_1_b2;              /*       0xe0 - 0xe4       */
  volatile u_int32_t BB_cl_map_2_b2;              /*       0xe4 - 0xe8       */
  volatile u_int32_t BB_cl_map_3_b2;              /*       0xe8 - 0xec       */
  volatile u_int32_t BB_cl_map_pal_0_b2;          /*       0xec - 0xf0       */
  volatile u_int32_t BB_cl_map_pal_1_b2;          /*       0xf0 - 0xf4       */
  volatile u_int32_t BB_cl_map_pal_2_b2;          /*       0xf4 - 0xf8       */
  volatile u_int32_t BB_cl_map_pal_3_b2;          /*       0xf8 - 0xfc       */
  volatile char pad__3[0x4];                      /*       0xfc - 0x100      */
  volatile u_int32_t BB_cl_tab_b2[16];            /*      0x100 - 0x140      */
  volatile char pad__4[0x40];                     /*      0x140 - 0x180      */
  volatile u_int32_t BB_chan_info_gain_b2;        /*      0x180 - 0x184      */
  volatile char pad__5[0x80];                     /*      0x184 - 0x204      */
  volatile u_int32_t BB_tpc_4_b2;                 /*      0x204 - 0x208      */
  volatile u_int32_t BB_tpc_5_b2;                 /*      0x208 - 0x20c      */
  volatile u_int32_t BB_tpc_6_b2;                 /*      0x20c - 0x210      */
  volatile char pad__6[0x10];                     /*      0x210 - 0x220      */
  volatile u_int32_t BB_tpc_11_b2;                /*      0x220 - 0x224      */
  volatile char pad__7[0x1c];                     /*      0x224 - 0x240      */
  volatile u_int32_t BB_tpc_19_b2;                /*      0x240 - 0x244      */
  volatile char pad__8[0x3c];                     /*      0x244 - 0x280      */
  volatile u_int32_t BB_pdadc_tab_b2[32];         /*      0x280 - 0x300      */
  volatile char pad__9[0x84];                     /*      0x300 - 0x384      */
  volatile u_int32_t BB_rtt_table_sw_intf_b2;     /*      0x384 - 0x388      */
  volatile u_int32_t BB_rtt_table_sw_intf_1_b2;   /*      0x388 - 0x38c      */
  volatile char pad__10[0xc4];                    /*      0x38c - 0x450      */
  volatile u_int32_t BB_txiq_corr_coeff_01_b2;    /*      0x450 - 0x454      */
  volatile u_int32_t BB_txiq_corr_coeff_23_b2;    /*      0x454 - 0x458      */
  volatile u_int32_t BB_txiq_corr_coeff_45_b2;    /*      0x458 - 0x45c      */
  volatile u_int32_t BB_txiq_corr_coeff_67_b2;    /*      0x45c - 0x460      */
  volatile u_int32_t BB_txiq_corr_coeff_89_b2;    /*      0x460 - 0x464      */
  volatile u_int32_t BB_txiq_corr_coeff_ab_b2;    /*      0x464 - 0x468      */
  volatile u_int32_t BB_txiq_corr_coeff_cd_b2;    /*      0x468 - 0x46c      */
  volatile u_int32_t BB_txiq_corr_coeff_ef_b2;    /*      0x46c - 0x470      */
  volatile char pad__11[0x1c];                    /*      0x470 - 0x48c      */
  volatile u_int32_t BB_txiqcal_status_b2;        /*      0x48c - 0x490      */
  volatile char pad__12[0x160];                   /*      0x490 - 0x5f0      */
  volatile u_int32_t BB_tables_intf_addr_b2;      /*      0x5f0 - 0x5f4      */
  volatile u_int32_t BB_tables_intf_data_b2;      /*      0x5f4 - 0x5f8      */
};

struct chn3_reg_map {
  volatile u_int32_t BB_dummy1[256];              /*        0x0 - 0x400      */
};

struct agc3_reg_map {
  volatile u_int32_t BB_dummy;                    /*        0x0 - 0x4        */
  volatile char pad__0[0x17c];                    /*        0x4 - 0x180      */
  volatile u_int32_t BB_rssi_b3;                  /*      0x180 - 0x184      */
};

struct sm3_reg_map {
  volatile u_int32_t BB_dummy2[384];              /*        0x0 - 0x600      */
};

struct bb_reg_map {
  struct chn_reg_map bb_chn_reg_map;              /*        0x0 - 0x3f8      */
  volatile char pad__0[0x8];                      /*      0x3f8 - 0x400      */
  struct mrc_reg_map bb_mrc_reg_map;              /*      0x400 - 0x424      */
  volatile char pad__1[0xdc];                     /*      0x424 - 0x500      */
  struct bbb_reg_map bb_bbb_reg_map;              /*      0x500 - 0x51c      */
  volatile char pad__2[0xe4];                     /*      0x51c - 0x600      */
  struct agc_reg_map bb_agc_reg_map;              /*      0x600 - 0xa00      */
  struct sm_reg_map bb_sm_reg_map;                /*      0xa00 - 0xff8      */
  volatile char pad__3[0x8];                      /*      0xff8 - 0x1000     */
  struct chn1_reg_map bb_chn1_reg_map;            /*     0x1000 - 0x13c8     */
  volatile char pad__4[0x238];                    /*     0x13c8 - 0x1600     */
  struct agc1_reg_map bb_agc1_reg_map;            /*     0x1600 - 0x19fc     */
  volatile char pad__5[0x4];                      /*     0x19fc - 0x1a00     */
  struct sm1_reg_map bb_sm1_reg_map;              /*     0x1a00 - 0x1f74     */
  volatile char pad__6[0x8c];                     /*     0x1f74 - 0x2000     */
  struct chn2_reg_map bb_chn2_reg_map;            /*     0x2000 - 0x23c8     */
  volatile char pad__7[0x238];                    /*     0x23c8 - 0x2600     */
  struct agc2_reg_map bb_agc2_reg_map;            /*     0x2600 - 0x2790     */
  volatile char pad__8[0x270];                    /*     0x2790 - 0x2a00     */
  struct sm2_reg_map bb_sm2_reg_map;              /*     0x2a00 - 0x2f74     */
  volatile char pad__9[0x8c];                     /*     0x2f74 - 0x3000     */
  struct chn3_reg_map bb_chn3_reg_map;            /*     0x3000 - 0x3400     */
  volatile char pad__10[0x200];                   /*     0x3400 - 0x3600     */
  struct agc3_reg_map bb_agc3_reg_map;            /*     0x3600 - 0x3784     */
  volatile char pad__11[0x27c];                   /*     0x3784 - 0x3a00     */
  struct sm3_reg_map bb_sm3_reg_map;              /*     0x3a00 - 0x4000     */
};

struct mac_pcu_buf_reg {
  volatile u_int32_t MAC_PCU_BUF[2048];           /*        0x0 - 0x2000     */
};

struct svd_reg {
  volatile u_int32_t TXBF_DBG;                    /*        0x0 - 0x4        */
  volatile u_int32_t TXBF;                        /*        0x4 - 0x8        */
  volatile u_int32_t TXBF_TIMER;                  /*        0x8 - 0xc        */
  volatile u_int32_t TXBF_SW;                     /*        0xc - 0x10       */
  volatile u_int32_t TXBF_SM;                     /*       0x10 - 0x14       */
  volatile u_int32_t TXBF1_CNTL;                  /*       0x14 - 0x18       */
  volatile u_int32_t TXBF2_CNTL;                  /*       0x18 - 0x1c       */
  volatile u_int32_t TXBF3_CNTL;                  /*       0x1c - 0x20       */
  volatile u_int32_t TXBF4_CNTL;                  /*       0x20 - 0x24       */
  volatile u_int32_t TXBF5_CNTL;                  /*       0x24 - 0x28       */
  volatile u_int32_t TXBF6_CNTL;                  /*       0x28 - 0x2c       */
  volatile u_int32_t TXBF7_CNTL;                  /*       0x2c - 0x30       */
  volatile u_int32_t TXBF8_CNTL;                  /*       0x30 - 0x34       */
  volatile char pad__0[0xfcc];                    /*       0x34 - 0x1000     */
  volatile u_int32_t RC0[118];                    /*     0x1000 - 0x11d8     */
  volatile char pad__1[0x28];                     /*     0x11d8 - 0x1200     */
  volatile u_int32_t RC1[118];                    /*     0x1200 - 0x13d8     */
  volatile char pad__2[0x28];                     /*     0x13d8 - 0x1400     */
  volatile u_int32_t SVD_MEM0[114];               /*     0x1400 - 0x15c8     */
  volatile char pad__3[0x38];                     /*     0x15c8 - 0x1600     */
  volatile u_int32_t SVD_MEM1[114];               /*     0x1600 - 0x17c8     */
  volatile char pad__4[0x38];                     /*     0x17c8 - 0x1800     */
  volatile u_int32_t SVD_MEM2[114];               /*     0x1800 - 0x19c8     */
  volatile char pad__5[0x38];                     /*     0x19c8 - 0x1a00     */
  volatile u_int32_t SVD_MEM3[114];               /*     0x1a00 - 0x1bc8     */
  volatile char pad__6[0x38];                     /*     0x1bc8 - 0x1c00     */
  volatile u_int32_t SVD_MEM4[114];               /*     0x1c00 - 0x1dc8     */
  volatile char pad__7[0x638];                    /*     0x1dc8 - 0x2400     */
  volatile u_int32_t CVCACHE[512];                /*     0x2400 - 0x2c00     */
};

struct radio65_reg {
  volatile u_int32_t ch0_RXRF_BIAS1;              /*        0x0 - 0x4        */
  volatile u_int32_t ch0_RXRF_BIAS2;              /*        0x4 - 0x8        */
  volatile u_int32_t ch0_RXRF_GAINSTAGES;         /*        0x8 - 0xc        */
  volatile u_int32_t ch0_RXRF_AGC;                /*        0xc - 0x10       */
  volatile char pad__0[0x30];                     /*       0x10 - 0x40       */
  volatile u_int32_t ch0_TXRF1;                   /*       0x40 - 0x44       */
  volatile u_int32_t ch0_TXRF2;                   /*       0x44 - 0x48       */
  volatile u_int32_t ch0_TXRF3;                   /*       0x48 - 0x4c       */
  volatile u_int32_t ch0_TXRF4;                   /*       0x4c - 0x50       */
  volatile u_int32_t ch0_TXRF5;                   /*       0x50 - 0x54       */
  volatile u_int32_t ch0_TXRF6;                   /*       0x54 - 0x58       */
  volatile char pad__1[0x28];                     /*       0x58 - 0x80       */
  volatile u_int32_t ch0_SYNTH1;                  /*       0x80 - 0x84       */
  volatile u_int32_t ch0_SYNTH2;                  /*       0x84 - 0x88       */
  volatile u_int32_t ch0_SYNTH3;                  /*       0x88 - 0x8c       */
  volatile u_int32_t ch0_SYNTH4;                  /*       0x8c - 0x90       */
  volatile u_int32_t ch0_SYNTH5;                  /*       0x90 - 0x94       */
  volatile u_int32_t ch0_SYNTH6;                  /*       0x94 - 0x98       */
  volatile u_int32_t ch0_SYNTH7;                  /*       0x98 - 0x9c       */
  volatile u_int32_t ch0_SYNTH8;                  /*       0x9c - 0xa0       */
  volatile u_int32_t ch0_SYNTH9;                  /*       0xa0 - 0xa4       */
  volatile u_int32_t ch0_SYNTH10;                 /*       0xa4 - 0xa8       */
  volatile u_int32_t ch0_SYNTH11;                 /*       0xa8 - 0xac       */
  volatile u_int32_t ch0_SYNTH12;                 /*       0xac - 0xb0       */
  volatile u_int32_t ch0_SYNTH13;                 /*       0xb0 - 0xb4       */
  volatile u_int32_t ch0_SYNTH14;                 /*       0xb4 - 0xb8       */
  volatile char pad__2[0x8];                      /*       0xb8 - 0xc0       */
  volatile u_int32_t ch0_BIAS1;                   /*       0xc0 - 0xc4       */
  volatile u_int32_t ch0_BIAS2;                   /*       0xc4 - 0xc8       */
  volatile u_int32_t ch0_BIAS3;                   /*       0xc8 - 0xcc       */
  volatile u_int32_t ch0_BIAS4;                   /*       0xcc - 0xd0       */
  volatile char pad__3[0x30];                     /*       0xd0 - 0x100      */
  volatile u_int32_t ch0_RXTX1;                   /*      0x100 - 0x104      */
  volatile u_int32_t ch0_RXTX2;                   /*      0x104 - 0x108      */
  volatile u_int32_t ch0_RXTX3;                   /*      0x108 - 0x10c      */
  volatile u_int32_t ch0_RXTX4;                   /*      0x10c - 0x110      */
  volatile char pad__4[0x30];                     /*      0x110 - 0x140      */
  volatile u_int32_t ch0_BB1;                     /*      0x140 - 0x144      */
  volatile u_int32_t ch0_BB2;                     /*      0x144 - 0x148      */
  volatile u_int32_t ch0_BB3;                     /*      0x148 - 0x14c      */
  volatile char pad__5[0x34];                     /*      0x14c - 0x180      */
  volatile u_int32_t ch0_BB_PLL;                  /*      0x180 - 0x184      */
  volatile u_int32_t ch0_BB_PLL2;                 /*      0x184 - 0x188      */
  volatile u_int32_t ch0_BB_PLL3;                 /*      0x188 - 0x18c      */
  volatile u_int32_t ch0_BB_PLL4;                 /*      0x18c - 0x190      */
  volatile char pad__6[0x30];                     /*      0x190 - 0x1c0      */
  volatile u_int32_t ch0_CPU_PLL;                 /*      0x1c0 - 0x1c4      */
  volatile u_int32_t ch0_CPU_PLL2;                /*      0x1c4 - 0x1c8      */
  volatile u_int32_t ch0_CPU_PLL3;                /*      0x1c8 - 0x1cc      */
  volatile u_int32_t ch0_CPU_PLL4;                /*      0x1cc - 0x1d0      */
  volatile char pad__7[0x30];                     /*      0x1d0 - 0x200      */
  volatile u_int32_t ch0_AUDIO_PLL;               /*      0x200 - 0x204      */
  volatile u_int32_t ch0_AUDIO_PLL2;              /*      0x204 - 0x208      */
  volatile u_int32_t ch0_AUDIO_PLL3;              /*      0x208 - 0x20c      */
  volatile u_int32_t ch0_AUDIO_PLL4;              /*      0x20c - 0x210      */
  volatile char pad__8[0x30];                     /*      0x210 - 0x240      */
  volatile u_int32_t ch0_DDR_PLL;                 /*      0x240 - 0x244      */
  volatile u_int32_t ch0_DDR_PLL2;                /*      0x244 - 0x248      */
  volatile u_int32_t ch0_DDR_PLL3;                /*      0x248 - 0x24c      */
  volatile u_int32_t ch0_DDR_PLL4;                /*      0x24c - 0x250      */
  volatile char pad__9[0x30];                     /*      0x250 - 0x280      */
  volatile u_int32_t ch0_TOP;                     /*      0x280 - 0x284      */
  volatile u_int32_t ch0_TOP2;                    /*      0x284 - 0x288      */
  volatile u_int32_t ch0_TOP3;                    /*      0x288 - 0x28c      */
  volatile u_int32_t ch0_THERM;                   /*      0x28c - 0x290      */
  volatile u_int32_t ch0_XTAL;                    /*      0x290 - 0x294      */
  volatile char pad__10[0xec];                    /*      0x294 - 0x380      */
  volatile u_int32_t ch0_rbist_cntrl;             /*      0x380 - 0x384      */
  volatile u_int32_t ch0_tx_dc_offset;            /*      0x384 - 0x388      */
  volatile u_int32_t ch0_tx_tonegen0;             /*      0x388 - 0x38c      */
  volatile u_int32_t ch0_tx_tonegen1;             /*      0x38c - 0x390      */
  volatile u_int32_t ch0_tx_lftonegen0;           /*      0x390 - 0x394      */
  volatile u_int32_t ch0_tx_linear_ramp_i;        /*      0x394 - 0x398      */
  volatile u_int32_t ch0_tx_linear_ramp_q;        /*      0x398 - 0x39c      */
  volatile u_int32_t ch0_tx_prbs_mag;             /*      0x39c - 0x3a0      */
  volatile u_int32_t ch0_tx_prbs_seed_i;          /*      0x3a0 - 0x3a4      */
  volatile u_int32_t ch0_tx_prbs_seed_q;          /*      0x3a4 - 0x3a8      */
  volatile u_int32_t ch0_cmac_dc_cancel;          /*      0x3a8 - 0x3ac      */
  volatile u_int32_t ch0_cmac_dc_offset;          /*      0x3ac - 0x3b0      */
  volatile u_int32_t ch0_cmac_corr;               /*      0x3b0 - 0x3b4      */
  volatile u_int32_t ch0_cmac_power;              /*      0x3b4 - 0x3b8      */
  volatile u_int32_t ch0_cmac_cross_corr;         /*      0x3b8 - 0x3bc      */
  volatile u_int32_t ch0_cmac_i2q2;               /*      0x3bc - 0x3c0      */
  volatile u_int32_t ch0_cmac_power_hpf;          /*      0x3c0 - 0x3c4      */
  volatile u_int32_t ch0_rxdac_set1;              /*      0x3c4 - 0x3c8      */
  volatile u_int32_t ch0_rxdac_set2;              /*      0x3c8 - 0x3cc      */
  volatile u_int32_t ch0_rxdac_long_shift;        /*      0x3cc - 0x3d0      */
  volatile u_int32_t ch0_cmac_results_i;          /*      0x3d0 - 0x3d4      */
  volatile u_int32_t ch0_cmac_results_q;          /*      0x3d4 - 0x3d8      */
  volatile char pad__11[0x28];                    /*      0x3d8 - 0x400      */
  volatile u_int32_t ch1_RXRF_BIAS1;              /*      0x400 - 0x404      */
  volatile u_int32_t ch1_RXRF_BIAS2;              /*      0x404 - 0x408      */
  volatile u_int32_t ch1_RXRF_GAINSTAGES;         /*      0x408 - 0x40c      */
  volatile u_int32_t ch1_RXRF_AGC;                /*      0x40c - 0x410      */
  volatile char pad__12[0x30];                    /*      0x410 - 0x440      */
  volatile u_int32_t ch1_TXRF1;                   /*      0x440 - 0x444      */
  volatile u_int32_t ch1_TXRF2;                   /*      0x444 - 0x448      */
  volatile u_int32_t ch1_TXRF3;                   /*      0x448 - 0x44c      */
  volatile u_int32_t ch1_TXRF4;                   /*      0x44c - 0x450      */
  volatile u_int32_t ch1_TXRF5;                   /*      0x450 - 0x454      */
  volatile u_int32_t ch1_TXRF6;                   /*      0x454 - 0x458      */
  volatile char pad__13[0xa8];                    /*      0x458 - 0x500      */
  volatile u_int32_t ch1_RXTX1;                   /*      0x500 - 0x504      */
  volatile u_int32_t ch1_RXTX2;                   /*      0x504 - 0x508      */
  volatile u_int32_t ch1_RXTX3;                   /*      0x508 - 0x50c      */
  volatile u_int32_t ch1_RXTX4;                   /*      0x50c - 0x510      */
  volatile char pad__14[0x30];                    /*      0x510 - 0x540      */
  volatile u_int32_t ch1_BB1;                     /*      0x540 - 0x544      */
  volatile u_int32_t ch1_BB2;                     /*      0x544 - 0x548      */
  volatile u_int32_t ch1_BB3;                     /*      0x548 - 0x54c      */
  volatile char pad__15[0x234];                   /*      0x54c - 0x780      */
  volatile u_int32_t ch1_rbist_cntrl;             /*      0x780 - 0x784      */
  volatile u_int32_t ch1_tx_dc_offset;            /*      0x784 - 0x788      */
  volatile u_int32_t ch1_tx_tonegen0;             /*      0x788 - 0x78c      */
  volatile u_int32_t ch1_tx_tonegen1;             /*      0x78c - 0x790      */
  volatile u_int32_t ch1_tx_lftonegen0;           /*      0x790 - 0x794      */
  volatile u_int32_t ch1_tx_linear_ramp_i;        /*      0x794 - 0x798      */
  volatile u_int32_t ch1_tx_linear_ramp_q;        /*      0x798 - 0x79c      */
  volatile u_int32_t ch1_tx_prbs_mag;             /*      0x79c - 0x7a0      */
  volatile u_int32_t ch1_tx_prbs_seed_i;          /*      0x7a0 - 0x7a4      */
  volatile u_int32_t ch1_tx_prbs_seed_q;          /*      0x7a4 - 0x7a8      */
  volatile u_int32_t ch1_cmac_dc_cancel;          /*      0x7a8 - 0x7ac      */
  volatile u_int32_t ch1_cmac_dc_offset;          /*      0x7ac - 0x7b0      */
  volatile u_int32_t ch1_cmac_corr;               /*      0x7b0 - 0x7b4      */
  volatile u_int32_t ch1_cmac_power;              /*      0x7b4 - 0x7b8      */
  volatile u_int32_t ch1_cmac_cross_corr;         /*      0x7b8 - 0x7bc      */
  volatile u_int32_t ch1_cmac_i2q2;               /*      0x7bc - 0x7c0      */
  volatile u_int32_t ch1_cmac_power_hpf;          /*      0x7c0 - 0x7c4      */
  volatile u_int32_t ch1_rxdac_set1;              /*      0x7c4 - 0x7c8      */
  volatile u_int32_t ch1_rxdac_set2;              /*      0x7c8 - 0x7cc      */
  volatile u_int32_t ch1_rxdac_long_shift;        /*      0x7cc - 0x7d0      */
  volatile u_int32_t ch1_cmac_results_i;          /*      0x7d0 - 0x7d4      */
  volatile u_int32_t ch1_cmac_results_q;          /*      0x7d4 - 0x7d8      */
  volatile char pad__16[0x28];                    /*      0x7d8 - 0x800      */
  volatile u_int32_t ch2_RXRF_BIAS1;              /*      0x800 - 0x804      */
  volatile u_int32_t ch2_RXRF_BIAS2;              /*      0x804 - 0x808      */
  volatile u_int32_t ch2_RXRF_GAINSTAGES;         /*      0x808 - 0x80c      */
  volatile u_int32_t ch2_RXRF_AGC;                /*      0x80c - 0x810      */
  volatile char pad__17[0x30];                    /*      0x810 - 0x840      */
  volatile u_int32_t ch2_TXRF1;                   /*      0x840 - 0x844      */
  volatile u_int32_t ch2_TXRF2;                   /*      0x844 - 0x848      */
  volatile u_int32_t ch2_TXRF3;                   /*      0x848 - 0x84c      */
  volatile u_int32_t ch2_TXRF4;                   /*      0x84c - 0x850      */
  volatile u_int32_t ch2_TXRF5;                   /*      0x850 - 0x854      */
  volatile u_int32_t ch2_TXRF6;                   /*      0x854 - 0x858      */
  volatile char pad__18[0xa8];                    /*      0x858 - 0x900      */
  volatile u_int32_t ch2_RXTX1;                   /*      0x900 - 0x904      */
  volatile u_int32_t ch2_RXTX2;                   /*      0x904 - 0x908      */
  volatile u_int32_t ch2_RXTX3;                   /*      0x908 - 0x90c      */
  volatile u_int32_t ch2_RXTX4;                   /*      0x90c - 0x910      */
  volatile char pad__19[0x30];                    /*      0x910 - 0x940      */
  volatile u_int32_t ch2_BB1;                     /*      0x940 - 0x944      */
  volatile u_int32_t ch2_BB2;                     /*      0x944 - 0x948      */
  volatile u_int32_t ch2_BB3;                     /*      0x948 - 0x94c      */
  volatile char pad__20[0x234];                   /*      0x94c - 0xb80      */
  volatile u_int32_t ch2_rbist_cntrl;             /*      0xb80 - 0xb84      */
  volatile u_int32_t ch2_tx_dc_offset;            /*      0xb84 - 0xb88      */
  volatile u_int32_t ch2_tx_tonegen0;             /*      0xb88 - 0xb8c      */
  volatile u_int32_t ch2_tx_tonegen1;             /*      0xb8c - 0xb90      */
  volatile u_int32_t ch2_tx_lftonegen0;           /*      0xb90 - 0xb94      */
  volatile u_int32_t ch2_tx_linear_ramp_i;        /*      0xb94 - 0xb98      */
  volatile u_int32_t ch2_tx_linear_ramp_q;        /*      0xb98 - 0xb9c      */
  volatile u_int32_t ch2_tx_prbs_mag;             /*      0xb9c - 0xba0      */
  volatile u_int32_t ch2_tx_prbs_seed_i;          /*      0xba0 - 0xba4      */
  volatile u_int32_t ch2_tx_prbs_seed_q;          /*      0xba4 - 0xba8      */
  volatile u_int32_t ch2_cmac_dc_cancel;          /*      0xba8 - 0xbac      */
  volatile u_int32_t ch2_cmac_dc_offset;          /*      0xbac - 0xbb0      */
  volatile u_int32_t ch2_cmac_corr;               /*      0xbb0 - 0xbb4      */
  volatile u_int32_t ch2_cmac_power;              /*      0xbb4 - 0xbb8      */
  volatile u_int32_t ch2_cmac_cross_corr;         /*      0xbb8 - 0xbbc      */
  volatile u_int32_t ch2_cmac_i2q2;               /*      0xbbc - 0xbc0      */
  volatile u_int32_t ch2_cmac_power_hpf;          /*      0xbc0 - 0xbc4      */
  volatile u_int32_t ch2_rxdac_set1;              /*      0xbc4 - 0xbc8      */
  volatile u_int32_t ch2_rxdac_set2;              /*      0xbc8 - 0xbcc      */
  volatile u_int32_t ch2_rxdac_long_shift;        /*      0xbcc - 0xbd0      */
  volatile u_int32_t ch2_cmac_results_i;          /*      0xbd0 - 0xbd4      */
  volatile u_int32_t ch2_cmac_results_q;          /*      0xbd4 - 0xbd8      */
};

struct scorpion_reg_map {
  struct mac_dma_reg mac_dma_reg_map;             /*        0x0 - 0x128      */
  volatile char pad__0[0x6d8];                    /*      0x128 - 0x800      */
  struct mac_qcu_reg mac_qcu_reg_map;             /*      0x800 - 0xa48      */
  volatile char pad__1[0x5b8];                    /*      0xa48 - 0x1000     */
  struct mac_dcu_reg mac_dcu_reg_map;             /*     0x1000 - 0x1f08     */
  volatile char pad__2[0x50f8];                   /*     0x1f08 - 0x7000     */
  struct rtc_reg rtc_reg_map;                     /*     0x7000 - 0x7040     */
  struct rtc_sync_reg rtc_sync_reg_map;           /*     0x7040 - 0x705c     */
  volatile char pad__3[0xfa4];                    /*     0x705c - 0x8000     */
  struct mac_pcu_reg mac_pcu_reg_map;             /*     0x8000 - 0x9800     */
  struct bb_reg_map bb_reg_map;                   /*     0x9800 - 0xd800     */
  volatile char pad__4[0x800];                    /*     0xd800 - 0xe000     */
  struct mac_pcu_buf_reg mac_pcu_buf_reg_map;     /*     0xe000 - 0x10000    */
  struct svd_reg svd_reg_map;                     /*    0x10000 - 0x12c00    */
  volatile char pad__5[0x3400];                   /*    0x12c00 - 0x16000    */
  struct radio65_reg radio65_reg_map;             /*    0x16000 - 0x16bd8    */
};

#endif /* __REG_SCORPION_REG_MAP_H__ */
