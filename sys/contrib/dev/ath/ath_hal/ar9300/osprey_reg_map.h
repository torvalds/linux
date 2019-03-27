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
/* File:       /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top/osprey_reg_map.h*/
/* Creator:    yli                                                           */
/* Time:       Wednesday Jan 6, 2010 [2:09:02 pm]                            */
/*                                                                           */
/* Path:       /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top             */
/* Arguments:  /cad/denali/blueprint/3.7//Linux/blueprint -codegen           */
/*             /trees/yli/yli-dev/chips/osprey/2.0/env/blueprint/ath_ansic.codegen*/
/*             -ath_ansic -Wdesc -I                                          */
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top -I          */
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint -I              */
/*             /trees/yli/yli-dev/chips/osprey/2.0/env/blueprint -I          */
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig -odir */
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top -eval       */
/*             {$INCLUDE_SYSCONFIG_FILES=1} -eval                            */
/*             $WAR_EV58615_for_ansic_codegen=1 osprey_reg.rdl               */
/*                                                                           */
/* Sources:    /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top/emulation_misc.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/mac_dma_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/amba_mac/svd/blueprint/svd_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/mac_pcu_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top/merlin2_0_radio_reg_map.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/mac/rtl/mac_dma/blueprint/mac_dma_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/host_intf/rtl/blueprint/efuse_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/mac/rtl/mac_dma/blueprint/mac_dcu_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/ip/pcie_axi/blueprint/DWC_pcie_ep.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/apb_analog/analog_intf_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/mac/rtl/mac_pcu/blueprint/mac_pcu_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/rtc/blueprint/rtc_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/DWC_pcie_dbi_axi_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/host_intf/rtl/blueprint/host_intf_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/mac/rtl/mac_dma/blueprint/mac_qcu_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/bb/blueprint/bb_reg_map.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/rtc_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/efuse_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/bb_reg_map_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/osprey_pcieconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top/osprey_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/radio_65_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/merlin2_0_radio_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/mac_qcu_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/mac_dcu_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/rtl/amba_mac/blueprint/rtc_sync_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/analog_intf_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/svd_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/top/osprey_radio_reg.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/blueprint/sysconfig/host_intf_reg_sysconfig.rdl*/
/*             /trees/yli/yli-dev/chips/osprey/2.0/env/blueprint/ath_ansic.pm*/
/*             /cad/local/lib/perl/Pinfo.pm                                  */
/*                                                                           */
/* Blueprint:   3.7 (Fri Oct 5 10:32:33 PDT 2007)                            */
/* Machine:    artemis                                                       */
/* OS:         Linux 2.6.9-78.0.5.ELlargesmp                                 */
/* Description:                                                              */
/*                                                                           */
/*This Register Map contains the complete register set for OSPREY.           */
/*                                                                           */
/* Copyright (C) 2010 Denali Software Inc.  All rights reserved              */
/* THIS FILE IS AUTOMATICALLY GENERATED BY DENALI BLUEPRINT, DO NOT EDIT     */
/*                                                                           */


#ifndef __REG_OSPREY_REG_MAP_H__
#define __REG_OSPREY_REG_MAP_H__

#include "osprey_reg_map_macro.h"
#include "poseidon_reg_map_macro.h"

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
  /* Jupiter */
  volatile u_int32_t MAC_DMA_ISR_S6;              /*       0x9c - 0xa0       */
  volatile u_int32_t MAC_DMA_IMR_P;               /*       0xa0 - 0xa4       */
  volatile u_int32_t MAC_DMA_IMR_S0;              /*       0xa4 - 0xa8       */
  volatile u_int32_t MAC_DMA_IMR_S1;              /*       0xa8 - 0xac       */
  volatile u_int32_t MAC_DMA_IMR_S2;              /*       0xac - 0xb0       */
  volatile u_int32_t MAC_DMA_IMR_S3;              /*       0xb0 - 0xb4       */
  volatile u_int32_t MAC_DMA_IMR_S4;              /*       0xb4 - 0xb8       */
  volatile u_int32_t MAC_DMA_IMR_S5;              /*       0xb8 - 0xbc       */
  /* Jupiter */
  volatile u_int32_t MAC_DMA_IMR_S6;              /*       0xbc - 0xc0       */
  volatile u_int32_t MAC_DMA_ISR_P_RAC;           /*       0xc0 - 0xc4       */
  volatile u_int32_t MAC_DMA_ISR_S0_S;            /*       0xc4 - 0xc8       */
  volatile u_int32_t MAC_DMA_ISR_S1_S;            /*       0xc8 - 0xcc       */
  /* Jupiter */
  volatile u_int32_t MAC_DMA_ISR_S6_S;            /*       0xcc - 0xd0       */
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
};

struct mac_qcu_reg {
  volatile char pad__0[0x800];                    /*        0x0 - 0x800      */
  volatile u_int32_t MAC_QCU_TXDP[10];            /*      0x800 - 0x828      */
  volatile char pad__1[0x8];                      /*      0x828 - 0x830      */
  volatile u_int32_t MAC_QCU_STATUS_RING_START;   /*      0x830 - 0x834      */
  volatile u_int32_t MAC_QCU_STATUS_RING_END;     /*      0x834 - 0x838      */
  volatile u_int32_t MAC_QCU_STATUS_RING_CURRENT; /*      0x838 - 0x83c      */
  volatile char pad__2[0x4];                      /*      0x83c - 0x840      */
  volatile u_int32_t MAC_QCU_TXE;                 /*      0x840 - 0x844      */
  volatile char pad__3[0x3c];                     /*      0x844 - 0x880      */
  volatile u_int32_t MAC_QCU_TXD;                 /*      0x880 - 0x884      */
  volatile char pad__4[0x3c];                     /*      0x884 - 0x8c0      */
  volatile u_int32_t MAC_QCU_CBR[10];             /*      0x8c0 - 0x8e8      */
  volatile char pad__5[0x18];                     /*      0x8e8 - 0x900      */
  volatile u_int32_t MAC_QCU_RDYTIME[10];         /*      0x900 - 0x928      */
  volatile char pad__6[0x18];                     /*      0x928 - 0x940      */
  volatile u_int32_t MAC_QCU_ONESHOT_ARM_SC;      /*      0x940 - 0x944      */
  volatile char pad__7[0x3c];                     /*      0x944 - 0x980      */
  volatile u_int32_t MAC_QCU_ONESHOT_ARM_CC;      /*      0x980 - 0x984      */
  volatile char pad__8[0x3c];                     /*      0x984 - 0x9c0      */
  volatile u_int32_t MAC_QCU_MISC[10];            /*      0x9c0 - 0x9e8      */
  volatile char pad__9[0x18];                     /*      0x9e8 - 0xa00      */
  volatile u_int32_t MAC_QCU_CNT[10];             /*      0xa00 - 0xa28      */
  volatile char pad__10[0x18];                    /*      0xa28 - 0xa40      */
  volatile u_int32_t MAC_QCU_RDYTIME_SHDN;        /*      0xa40 - 0xa44      */
  volatile u_int32_t MAC_QCU_DESC_CRC_CHK;        /*      0xa44 - 0xa48      */
  /* Jupiter_20 */
  volatile u_int32_t MAC_QCU_EOL;                 /*      0xa48 - 0xa4c      */
};

struct mac_dcu_reg {
  volatile char pad__0[0x1000];                   /*        0x0 - 0x1000     */
  volatile u_int32_t MAC_DCU_QCUMASK[10];         /*     0x1000 - 0x1028     */
  volatile char pad__1[0x8];                      /*     0x1028 - 0x1030     */
  volatile u_int32_t MAC_DCU_GBL_IFS_SIFS;        /*     0x1030 - 0x1034     */
  volatile char pad__2[0x4];                      /*     0x1034 - 0x1038     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_31_0;  /*     0x1038 - 0x103c     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_31_0;  /*     0x103c - 0x1040     */
  volatile u_int32_t MAC_DCU_LCL_IFS[10];         /*     0x1040 - 0x1068     */
  volatile char pad__3[0x8];                      /*     0x1068 - 0x1070     */
  volatile u_int32_t MAC_DCU_GBL_IFS_SLOT;        /*     0x1070 - 0x1074     */
  volatile char pad__4[0x4];                      /*     0x1074 - 0x1078     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_63_32; /*     0x1078 - 0x107c     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_63_32; /*     0x107c - 0x1080     */
  volatile u_int32_t MAC_DCU_RETRY_LIMIT[10];     /*     0x1080 - 0x10a8     */
  volatile char pad__5[0x8];                      /*     0x10a8 - 0x10b0     */
  volatile u_int32_t MAC_DCU_GBL_IFS_EIFS;        /*     0x10b0 - 0x10b4     */
  volatile char pad__6[0x4];                      /*     0x10b4 - 0x10b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_95_64; /*     0x10b8 - 0x10bc     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_95_64; /*     0x10bc - 0x10c0     */
  volatile u_int32_t MAC_DCU_CHANNEL_TIME[10];    /*     0x10c0 - 0x10e8     */
  volatile char pad__7[0x8];                      /*     0x10e8 - 0x10f0     */
  volatile u_int32_t MAC_DCU_GBL_IFS_MISC;        /*     0x10f0 - 0x10f4     */
  volatile char pad__8[0x4];                      /*     0x10f4 - 0x10f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU0_127_96;
                                                  /*     0x10f8 - 0x10fc     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU8_127_96;
                                                  /*     0x10fc - 0x1100     */
  volatile u_int32_t MAC_DCU_MISC[10];            /*     0x1100 - 0x1128     */
  volatile char pad__9[0x10];                     /*     0x1128 - 0x1138     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_31_0;  /*     0x1138 - 0x113c     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_31_0;  /*     0x113c - 0x1140     */
  volatile u_int32_t MAC_DCU_SEQ;                 /*     0x1140 - 0x1144     */
  volatile char pad__10[0x34];                    /*     0x1144 - 0x1178     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_63_32; /*     0x1178 - 0x117c     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_63_32; /*     0x117c - 0x1180     */
  volatile char pad__11[0x38];                    /*     0x1180 - 0x11b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_95_64; /*     0x11b8 - 0x11bc     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_95_64; /*     0x11bc - 0x11c0     */
  volatile char pad__12[0x38];                    /*     0x11c0 - 0x11f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU1_127_96;
                                                  /*     0x11f8 - 0x11fc     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU9_127_96;
                                                  /*     0x11fc - 0x1200     */
  volatile char pad__13[0x38];                    /*     0x1200 - 0x1238     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_31_0;  /*     0x1238 - 0x123c     */
  volatile char pad__14[0x34];                    /*     0x123c - 0x1270     */
  volatile u_int32_t MAC_DCU_PAUSE;               /*     0x1270 - 0x1274     */
  volatile char pad__15[0x4];                     /*     0x1274 - 0x1278     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_63_32; /*     0x1278 - 0x127c     */
  volatile char pad__16[0x34];                    /*     0x127c - 0x12b0     */
  volatile u_int32_t MAC_DCU_WOW_KACFG;           /*     0x12b0 - 0x12b4     */
  volatile char pad__17[0x4];                     /*     0x12b4 - 0x12b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_95_64; /*     0x12b8 - 0x12bc     */
  volatile char pad__18[0x34];                    /*     0x12bc - 0x12f0     */
  volatile u_int32_t MAC_DCU_TXSLOT;              /*     0x12f0 - 0x12f4     */
  volatile char pad__19[0x4];                     /*     0x12f4 - 0x12f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU2_127_96;
                                                  /*     0x12f8 - 0x12fc     */
  volatile char pad__20[0x3c];                    /*     0x12fc - 0x1338     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_31_0;  /*     0x1338 - 0x133c     */
  volatile char pad__21[0x3c];                    /*     0x133c - 0x1378     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_63_32; /*     0x1378 - 0x137c     */
  volatile char pad__22[0x3c];                    /*     0x137c - 0x13b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_95_64; /*     0x13b8 - 0x13bc     */
  volatile char pad__23[0x3c];                    /*     0x13bc - 0x13f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU3_127_96;
                                                  /*     0x13f8 - 0x13fc     */
  volatile char pad__24[0x3c];                    /*     0x13fc - 0x1438     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_31_0;  /*     0x1438 - 0x143c     */
  volatile u_int32_t MAC_DCU_TXFILTER_CLEAR;      /*     0x143c - 0x1440     */
  volatile char pad__25[0x38];                    /*     0x1440 - 0x1478     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_63_32; /*     0x1478 - 0x147c     */
  volatile u_int32_t MAC_DCU_TXFILTER_SET;        /*     0x147c - 0x1480     */
  volatile char pad__26[0x38];                    /*     0x1480 - 0x14b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_95_64; /*     0x14b8 - 0x14bc     */
  volatile char pad__27[0x3c];                    /*     0x14bc - 0x14f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU4_127_96;
                                                  /*     0x14f8 - 0x14fc     */
  volatile char pad__28[0x3c];                    /*     0x14fc - 0x1538     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_31_0;  /*     0x1538 - 0x153c     */
  volatile char pad__29[0x3c];                    /*     0x153c - 0x1578     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_63_32; /*     0x1578 - 0x157c     */
  volatile char pad__30[0x3c];                    /*     0x157c - 0x15b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_95_64; /*     0x15b8 - 0x15bc     */
  volatile char pad__31[0x3c];                    /*     0x15bc - 0x15f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU5_127_96;
                                                  /*     0x15f8 - 0x15fc     */
  volatile char pad__32[0x3c];                    /*     0x15fc - 0x1638     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_31_0;  /*     0x1638 - 0x163c     */
  volatile char pad__33[0x3c];                    /*     0x163c - 0x1678     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_63_32; /*     0x1678 - 0x167c     */
  volatile char pad__34[0x3c];                    /*     0x167c - 0x16b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_95_64; /*     0x16b8 - 0x16bc     */
  volatile char pad__35[0x3c];                    /*     0x16bc - 0x16f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU6_127_96;
                                                  /*     0x16f8 - 0x16fc     */
  volatile char pad__36[0x3c];                    /*     0x16fc - 0x1738     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_31_0;  /*     0x1738 - 0x173c     */
  volatile char pad__37[0x3c];                    /*     0x173c - 0x1778     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_63_32; /*     0x1778 - 0x177c     */
  volatile char pad__38[0x3c];                    /*     0x177c - 0x17b8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_95_64; /*     0x17b8 - 0x17bc     */
  volatile char pad__39[0x3c];                    /*     0x17bc - 0x17f8     */
  volatile u_int32_t MAC_DCU_TXFILTER_DCU7_127_96;
                                                  /*     0x17f8 - 0x17fc     */
};

struct host_intf_reg {
  volatile char pad__0[0x4000];                   /*        0x0 - 0x4000     */
  volatile u_int32_t HOST_INTF_RESET_CONTROL;     /*     0x4000 - 0x4004     */
  volatile u_int32_t HOST_INTF_WORK_AROUND;       /*     0x4004 - 0x4008     */
  volatile u_int32_t HOST_INTF_PM_STATE;          /*     0x4008 - 0x400c     */
  volatile u_int32_t HOST_INTF_CXPL_DEBUG_INFOL;  /*     0x400c - 0x4010     */
  volatile u_int32_t HOST_INTF_CXPL_DEBUG_INFOH;  /*     0x4010 - 0x4014     */
  volatile u_int32_t HOST_INTF_PM_CTRL;           /*     0x4014 - 0x4018     */
  volatile u_int32_t HOST_INTF_TIMEOUT;           /*     0x4018 - 0x401c     */
  volatile u_int32_t HOST_INTF_EEPROM_CTRL;       /*     0x401c - 0x4020     */
  volatile u_int32_t HOST_INTF_SREV;              /*     0x4020 - 0x4024     */
  volatile char pad__1[0x4];                      /*     0x4024 - 0x4028     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_CAUSE;   /*     0x4028 - 0x402c     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_ENABLE;  /*     0x402c - 0x4030     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_MASK;   /*     0x4030 - 0x4034     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_MASK;    /*     0x4034 - 0x4038     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_CAUSE;  /*     0x4038 - 0x403c     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_ENABLE; /*     0x403c - 0x4040     */
  volatile u_int32_t HOST_INTF_PCIE_PHY_RW;       /*     0x4040 - 0x4044     */
  volatile u_int32_t HOST_INTF_PCIE_PHY_LOAD;     /*     0x4044 - 0x4048     */
  volatile u_int32_t HOST_INTF_GPIO_OUT;          /*     0x4048 - 0x404c     */
  volatile u_int32_t HOST_INTF_GPIO_IN;           /*     0x404c - 0x4050     */
  volatile u_int32_t HOST_INTF_GPIO_OE;           /*     0x4050 - 0x4054     */
  volatile u_int32_t HOST_INTF_GPIO_OE1;          /*     0x4054 - 0x4058     */
  volatile u_int32_t HOST_INTF_GPIO_INTR_POLAR;   /*     0x4058 - 0x405c     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_VALUE;  /*     0x405c - 0x4060     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_MUX1;   /*     0x4060 - 0x4064     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_MUX2;   /*     0x4064 - 0x4068     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX1;  /*     0x4068 - 0x406c     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX2;  /*     0x406c - 0x4070     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX3;  /*     0x4070 - 0x4074     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_STATE;  /*     0x4074 - 0x4078     */
  volatile u_int32_t HOST_INTF_SPARE;             /*     0x4078 - 0x407c     */
  volatile u_int32_t HOST_INTF_PCIE_CORE_RST_EN;  /*     0x407c - 0x4080     */
  volatile u_int32_t HOST_INTF_CLKRUN;            /*     0x4080 - 0x4084     */
  volatile u_int32_t HOST_INTF_EEPROM_STS;        /*     0x4084 - 0x4088     */
  volatile u_int32_t HOST_INTF_OBS_CTRL;          /*     0x4088 - 0x408c     */
  volatile u_int32_t HOST_INTF_RFSILENT;          /*     0x408c - 0x4090     */
  volatile u_int32_t HOST_INTF_GPIO_PDPU;         /*     0x4090 - 0x4094     */
  volatile u_int32_t HOST_INTF_GPIO_PDPU1;        /*     0x4094 - 0x4098     */
  volatile u_int32_t HOST_INTF_GPIO_DS;           /*     0x4098 - 0x409c     */
  volatile u_int32_t HOST_INTF_GPIO_DS1;          /*     0x409c - 0x40a0     */
  volatile u_int32_t HOST_INTF_MISC;              /*     0x40a0 - 0x40a4     */
  volatile u_int32_t HOST_INTF_PCIE_MSI;          /*     0x40a4 - 0x40a8     */
  volatile char pad__2[0x8];                      /*     0x40a8 - 0x40b0     */
  volatile u_int32_t HOST_INTF_PCIE_PHY_LATENCY_NFTS_ADJ;
                                                  /*     0x40b0 - 0x40b4     */
  volatile u_int32_t HOST_INTF_MAC_TDMA_CCA_CNTL; /*     0x40b4 - 0x40b8     */
  volatile u_int32_t HOST_INTF_MAC_TXAPSYNC;      /*     0x40b8 - 0x40bc     */
  volatile u_int32_t HOST_INTF_MAC_TXSYNC_INITIAL_SYNC_TMR;
                                                  /*     0x40bc - 0x40c0     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_CAUSE;
                                                  /*     0x40c0 - 0x40c4     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_ENABLE;
                                                  /*     0x40c4 - 0x40c8     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_MASK;
                                                  /*     0x40c8 - 0x40cc     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_MASK;
                                                  /*     0x40cc - 0x40d0     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_CAUSE;
                                                  /*     0x40d0 - 0x40d4     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_ENABLE;
                                                  /*     0x40d4 - 0x40d8     */
  volatile u_int32_t HOST_INTF_OTP;               /*     0x40d8 - 0x40dc     */
  volatile char pad__3[0x4];                      /*     0x40dc - 0x40e0     */
  volatile u_int32_t PCIE_CO_ERR_CTR0;            /*     0x40e0 - 0x40e4     */
  volatile u_int32_t PCIE_CO_ERR_CTR1;            /*     0x40e4 - 0x40e8     */
  volatile u_int32_t PCIE_CO_ERR_CTR_CTRL;        /*     0x40e8 - 0x40ec     */
  /* Poseidon, Jupiter */
  volatile u_int32_t AXI_INTERCONNECT_CTRL;       /*     0x40ec - 0x40f0     */
  /* Jupiter */
  volatile u_int32_t PCIE_AXI_BRIDGE_CTRL;        /*     0x40f0 - 0x40f4     */
};

struct emulation_misc_regs {
  volatile char pad__0[0x4f00];                   /*        0x0 - 0x4f00     */
  volatile u_int32_t FPGA_PHY_LAYER_REVID;        /*     0x4f00 - 0x4f04     */
  volatile u_int32_t FPGA_LINK_LAYER_REVID;       /*     0x4f04 - 0x4f08     */
  volatile u_int32_t FPGA_REG1;                   /*     0x4f08 - 0x4f0c     */
  volatile u_int32_t FPGA_REG2;                   /*     0x4f0c - 0x4f10     */
  volatile u_int32_t FPGA_REG3;                   /*     0x4f10 - 0x4f14     */
  volatile u_int32_t FPGA_REG4;                   /*     0x4f14 - 0x4f18     */
  volatile u_int32_t FPGA_REG5;                   /*     0x4f18 - 0x4f1c     */
  volatile u_int32_t FPGA_REG6;                   /*     0x4f1c - 0x4f20     */
  volatile u_int32_t FPGA_REG7;                   /*     0x4f20 - 0x4f24     */
  volatile u_int32_t FPGA_REG8;                   /*     0x4f24 - 0x4f28     */
  volatile u_int32_t FPGA_REG9;                   /*     0x4f28 - 0x4f2c     */
  volatile u_int32_t FPGA_REG10;                  /*     0x4f2c - 0x4f30     */
  /* Aphrodite-start */
  volatile u_int32_t FPGA_REG11;                  /*     0x4f30 - 0x4f34     */
  volatile u_int32_t FPGA_REG12;                  /*     0x4f34 - 0x4f38     */
  volatile u_int32_t FPGA_REG13;                  /*     0x4f38 - 0x4f3c     */
  volatile u_int32_t FPGA_REG14;                  /*     0x4f3c - 0x4f40     */
  /* Aphrodite-end */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_0 {
  volatile u_int32_t ID;                          /*        0x0 - 0x4        */
  volatile u_int32_t STS_CMD_RGSTR;               /*        0x4 - 0x8        */
  volatile u_int32_t CLS_REV_ID;                  /*        0x8 - 0xc        */
  volatile u_int32_t BIST_HEAD_LAT_CACH;          /*        0xc - 0x10       */
  volatile u_int32_t BAS_ADR_0;                   /*       0x10 - 0x14       */
  volatile u_int32_t BAS_ADR_1;                   /*       0x14 - 0x18       */
  volatile u_int32_t BAS_ADR_2;                   /*       0x18 - 0x1c       */
  volatile u_int32_t BAS_ADR_3;                   /*       0x1c - 0x20       */
  volatile u_int32_t BAS_ADR_4;                   /*       0x20 - 0x24       */
  volatile u_int32_t BAS_ADR_5;                   /*       0x24 - 0x28       */
  volatile u_int32_t CRD_CIS_PTR;                 /*       0x28 - 0x2c       */
  volatile u_int32_t Sub_VenID;                   /*       0x2c - 0x30       */
  volatile u_int32_t EXP_ROM_ADDR;                /*       0x30 - 0x34       */
  volatile u_int32_t CAPPTR;                      /*       0x34 - 0x38       */
  volatile u_int32_t RESERVE2;                    /*       0x38 - 0x3c       */
  volatile u_int32_t LAT_INT;                     /*       0x3c - 0x40       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_1 {
  volatile u_int32_t CFG_PWR_CAP;                 /*        0x0 - 0x4        */
  volatile u_int32_t PWR_CSR;                     /*        0x4 - 0x8        */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_2 {
  volatile u_int32_t MSG_CTR;                     /*        0x0 - 0x4        */
  volatile u_int32_t MSI_L32;                     /*        0x4 - 0x8        */
  volatile u_int32_t MSI_U32;                     /*        0x8 - 0xc        */
  volatile u_int32_t MSI_DATA;                    /*        0xc - 0x10       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_3 {
  volatile u_int32_t PCIE_CAP;                    /*        0x0 - 0x4        */
  volatile u_int32_t DEV_CAP;                     /*        0x4 - 0x8        */
  volatile u_int32_t DEV_STS_CTRL;                /*        0x8 - 0xc        */
  volatile u_int32_t LNK_CAP;                     /*        0xc - 0x10       */
  volatile u_int32_t LNK_STS_CTRL;                /*       0x10 - 0x14       */
  volatile u_int32_t SLT_CAP;                     /*       0x14 - 0x18       */
  volatile u_int32_t SLT_STS_CTRL;                /*       0x18 - 0x1c       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_5 {
  volatile u_int32_t VPD_CAP;                     /*        0x0 - 0x4        */
  volatile u_int32_t VPD_DATA;                    /*        0x4 - 0x8        */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_6 {
  volatile u_int32_t PCIE_EN_CAP_AER;             /*        0x0 - 0x4        */
  volatile u_int32_t UN_ERR_ST_R;                 /*        0x4 - 0x8        */
  volatile u_int32_t UN_ERR_MS_R;                 /*        0x8 - 0xc        */
  volatile u_int32_t UN_ERR_SV_R;                 /*        0xc - 0x10       */
  volatile u_int32_t CO_ERR_ST_R;                 /*       0x10 - 0x14       */
  volatile u_int32_t CO_ERR_MS_R;                 /*       0x14 - 0x18       */
  volatile u_int32_t ADERR_CAP_CR;                /*       0x18 - 0x1c       */
  volatile u_int32_t HD_L_R0;                     /*       0x1c - 0x20       */
  volatile u_int32_t HD_L_R4;                     /*       0x20 - 0x24       */
  volatile u_int32_t HD_L_R8;                     /*       0x24 - 0x28       */
  volatile u_int32_t HD_L_R12;                    /*       0x28 - 0x2c       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_7 {
  volatile u_int32_t PCIE_EN_CAP_VC;              /*        0x0 - 0x4        */
  volatile u_int32_t PVC_CAP_R1;                  /*        0x4 - 0x8        */
  volatile u_int32_t P_CAP_R2;                    /*        0x8 - 0xc        */
  volatile u_int32_t PVC_STS_CTRL;                /*        0xc - 0x10       */
  volatile u_int32_t VC_CAP_R;                    /*       0x10 - 0x14       */
  volatile u_int32_t VC_CTL_R;                    /*       0x14 - 0x18       */
  volatile u_int32_t VC_STS_RSV;                  /*       0x18 - 0x1c       */
  volatile u_int32_t VCR_CAP_R1;                  /*       0x1c - 0x20       */
  volatile u_int32_t VCR_CTRL_R1;                 /*       0x20 - 0x24       */
  volatile u_int32_t VCR_STS_R1;                  /*       0x24 - 0x28       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_7_Jupiter {
  volatile u_int32_t PCIE_EN_CAP_VC;              /*        0x0 - 0x4        */
  volatile u_int32_t PVC_CAP_R1;                  /*        0x4 - 0x8        */
  volatile u_int32_t P_CAP_R2;                    /*        0x8 - 0xc        */
  volatile u_int32_t PVC_STS_CTRL;                /*        0xc - 0x10       */
  volatile u_int32_t VC_CAP_R;                    /*       0x10 - 0x14       */
  volatile u_int32_t VC_CTL_R;                    /*       0x14 - 0x18       */
  volatile u_int32_t VC_STS_RSV;                  /*       0x18 - 0x1c       */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_8 {
  volatile u_int32_t DEV_EN_CAP;                  /*        0x0 - 0x4        */
  volatile u_int32_t SN_R1;                       /*        0x4 - 0x8        */
  volatile u_int32_t SN_R2;                       /*        0x8 - 0xc        */
};

struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_9 {
  volatile u_int32_t LAT_REL_TIM;                 /*        0x0 - 0x4        */
  volatile u_int32_t OT_MSG_R;                    /*        0x4 - 0x8        */
  volatile u_int32_t PT_LNK_R;                    /*        0x8 - 0xc        */
  volatile u_int32_t ACk_FREQ_R;                  /*        0xc - 0x10       */
  volatile u_int32_t PT_LNK_CTRL_R;               /*       0x10 - 0x14       */
  volatile u_int32_t LN_SKW_R;                    /*       0x14 - 0x18       */
  volatile u_int32_t SYMB_N_R;                    /*       0x18 - 0x1c       */
  volatile u_int32_t SYMB_T_R;                    /*       0x1c - 0x20       */
  volatile u_int32_t FL_MSK_R2;                   /*       0x20 - 0x24       */
  volatile char pad__0[0x4];                      /*       0x24 - 0x28       */
  volatile u_int32_t DB_R0;                       /*       0x28 - 0x2c       */
  volatile u_int32_t DB_R1;                       /*       0x2c - 0x30       */
  volatile u_int32_t TR_P_STS_R;                  /*       0x30 - 0x34       */
  volatile u_int32_t TR_NP_STS_R;                 /*       0x34 - 0x38       */
  volatile u_int32_t TR_C_STS_R;                  /*       0x38 - 0x3c       */
  volatile u_int32_t Q_STS_R;                     /*       0x3c - 0x40       */
  volatile u_int32_t VC_TR_A_R1;                  /*       0x40 - 0x44       */
  volatile u_int32_t VC_TR_A_R2;                  /*       0x44 - 0x48       */
  volatile u_int32_t VC0_PR_Q_C;                  /*       0x48 - 0x4c       */
  volatile u_int32_t VC0_NPR_Q_C;                 /*       0x4c - 0x50       */
  volatile u_int32_t VC0_CR_Q_C;                  /*       0x50 - 0x54       */
  volatile u_int32_t VC1_PR_Q_C;                  /*       0x54 - 0x58       */
  volatile u_int32_t VC1_NPR_Q_C;                 /*       0x58 - 0x5c       */
  volatile u_int32_t VC1_CR_Q_C;                  /*       0x5c - 0x60       */
  volatile u_int32_t VC2_PR_Q_C;                  /*       0x60 - 0x64       */
  volatile u_int32_t VC2_NPR_Q_C;                 /*       0x64 - 0x68       */
  volatile u_int32_t VC2_CR_Q_C;                  /*       0x68 - 0x6c       */
  volatile u_int32_t VC3_PR_Q_C;                  /*       0x6c - 0x70       */
  volatile u_int32_t VC3_NPR_Q_C;                 /*       0x70 - 0x74       */
  volatile u_int32_t VC3_CR_Q_C;                  /*       0x74 - 0x78       */
  volatile u_int32_t VC4_PR_Q_C;                  /*       0x78 - 0x7c       */
  volatile u_int32_t VC4_NPR_Q_C;                 /*       0x7c - 0x80       */
  volatile u_int32_t VC4_CR_Q_C;                  /*       0x80 - 0x84       */
  volatile u_int32_t VC5_PR_Q_C;                  /*       0x84 - 0x88       */
  volatile u_int32_t VC5_NPR_Q_C;                 /*       0x88 - 0x8c       */
  volatile u_int32_t VC5_CR_Q_C;                  /*       0x8c - 0x90       */
  volatile u_int32_t VC6_PR_Q_C;                  /*       0x90 - 0x94       */
  volatile u_int32_t VC6_NPR_Q_C;                 /*       0x94 - 0x98       */
  volatile u_int32_t VC6_CR_Q_C;                  /*       0x98 - 0x9c       */
  volatile u_int32_t VC7_PR_Q_C;                  /*       0x9c - 0xa0       */
  volatile u_int32_t VC7_NPR_Q_C;                 /*       0xa0 - 0xa4       */
  volatile u_int32_t VC7_CR_Q_C;                  /*       0xa4 - 0xa8       */
  volatile u_int32_t VC0_PB_D;                    /*       0xa8 - 0xac       */
  volatile u_int32_t VC0_NPB_D;                   /*       0xac - 0xb0       */
  volatile u_int32_t VC0_CB_D;                    /*       0xb0 - 0xb4       */
  volatile u_int32_t VC1_PB_D;                    /*       0xb4 - 0xb8       */
  volatile u_int32_t VC1_NPB_D;                   /*       0xb8 - 0xbc       */
  volatile u_int32_t VC1_CB_D;                    /*       0xbc - 0xc0       */
  volatile u_int32_t VC2_PB_D;                    /*       0xc0 - 0xc4       */
  volatile u_int32_t VC2_NPB_D;                   /*       0xc4 - 0xc8       */
  volatile u_int32_t VC2_CB_D;                    /*       0xc8 - 0xcc       */
  volatile u_int32_t VC3_PB_D;                    /*       0xcc - 0xd0       */
  volatile u_int32_t VC3_NPB_D;                   /*       0xd0 - 0xd4       */
  volatile u_int32_t VC3_CB_D;                    /*       0xd4 - 0xd8       */
  volatile u_int32_t VC4_PB_D;                    /*       0xd8 - 0xdc       */
  volatile u_int32_t VC4_NPB_D;                   /*       0xdc - 0xe0       */
  volatile u_int32_t VC4_CB_D;                    /*       0xe0 - 0xe4       */
  volatile u_int32_t VC5_PB_D;                    /*       0xe4 - 0xe8       */
  volatile u_int32_t VC5_NPB_D;                   /*       0xe8 - 0xec       */
  volatile u_int32_t VC5_CB_D;                    /*       0xec - 0xf0       */
  volatile u_int32_t VC6_PB_D;                    /*       0xf0 - 0xf4       */
  volatile u_int32_t VC6_NPB_D;                   /*       0xf4 - 0xf8       */
  volatile u_int32_t VC6_CB_D;                    /*       0xf8 - 0xfc       */
  volatile u_int32_t VC7_PB_D;                    /*       0xfc - 0x100      */
  volatile u_int32_t VC7_NPB_D;                   /*      0x100 - 0x104      */
  volatile u_int32_t VC7_CB_D;                    /*      0x104 - 0x108      */
  volatile char pad__1[0x4];                      /*      0x108 - 0x10c      */
  volatile u_int32_t GEN2;                        /*      0x10c - 0x110      */
  volatile u_int32_t PHY_STS_R;                   /*      0x110 - 0x114      */
  volatile u_int32_t PHY_CTRL_R;                  /*      0x114 - 0x118      */
};

struct DWC_pcie_dbi_axi {
  volatile char pad__0[0x5000];                   /*        0x0 - 0x5000     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_0 DWC_pcie_dbi_axi_0;
                                                  /*     0x5000 - 0x5040     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_1 DWC_pcie_dbi_axi_1;
                                                  /*     0x5040 - 0x5048     */
  volatile char pad__1[0x8];                      /*     0x5048 - 0x5050     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_2 DWC_pcie_dbi_axi_2;
                                                  /*     0x5050 - 0x5060     */
  volatile char pad__2[0x10];                     /*     0x5060 - 0x5070     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_3 DWC_pcie_dbi_axi_3;
                                                  /*     0x5070 - 0x508c     */
  volatile char pad__3[0x44];                     /*     0x508c - 0x50d0     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_5 DWC_pcie_dbi_axi_5;
                                                  /*     0x50d0 - 0x50d8     */
  volatile char pad__4[0x28];                     /*     0x50d8 - 0x5100     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_6 DWC_pcie_dbi_axi_6;
                                                  /*     0x5100 - 0x512c     */
  volatile char pad__5[0x14];                     /*     0x512c - 0x5140     */
  union {
      struct {
          struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_7 DWC_pcie_dbi_axi_7;
                                                  /*     0x5140 - 0x5168     */
          volatile char pad__1[0x198];            /*     0x5168 - 0x5300     */
          struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_8 DWC_pcie_dbi_axi_8;
                                                  /*     0x5300 - 0x530c     */
          volatile char pad__2[0x3f4];            /*     0x530c - 0x5700     */
      } Osprey;

      struct pcie_dbi_axi {
          struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_7_Jupiter DWC_pcie_dbi_axi_7;
                                                  /*     0x5140 - 0x515c     */
          volatile char pad__1[0x4];              /*     0x515c - 0x5160     */
          struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_8 DWC_pcie_dbi_axi_8;
                                                  /*     0x5160 - 0x516c     */
          volatile char pad__2[0x594];            /*     0x516c - 0x5700     */
      } Jupiter;
  } overlay_0x5140;                               /*     0x5140 - 0x5700     */
  struct DWC_pcie_dbi_axi__DWC_pcie_dbi_axi_9 DWC_pcie_dbi_axi_9;
                                                  /*     0x5700 - 0x5818     */
};

struct rtc_reg {
  volatile char pad__0[0x7000];                   /*        0x0 - 0x7000     */
  volatile u_int32_t RESET_CONTROL;               /*     0x7000 - 0x7004     */
  volatile u_int32_t XTAL_CONTROL;                /*     0x7004 - 0x7008     */
  volatile u_int32_t REG_CONTROL0;                /*     0x7008 - 0x700c     */
  volatile u_int32_t REG_CONTROL1;                /*     0x700c - 0x7010     */
  volatile u_int32_t QUADRATURE;                  /*     0x7010 - 0x7014     */
  volatile u_int32_t PLL_CONTROL;                 /*     0x7014 - 0x7018     */
  volatile u_int32_t PLL_SETTLE;                  /*     0x7018 - 0x701c     */
  volatile u_int32_t XTAL_SETTLE;                 /*     0x701c - 0x7020     */
  volatile u_int32_t CLOCK_OUT;                   /*     0x7020 - 0x7024     */
  volatile u_int32_t BIAS_OVERRIDE;               /*     0x7024 - 0x7028     */
  volatile u_int32_t RESET_CAUSE;                 /*     0x7028 - 0x702c     */
  volatile u_int32_t SYSTEM_SLEEP;                /*     0x702c - 0x7030     */
  volatile u_int32_t MAC_SLEEP_CONTROL;           /*     0x7030 - 0x7034     */
  volatile u_int32_t KEEP_AWAKE;                  /*     0x7034 - 0x7038     */
  volatile u_int32_t DERIVED_RTC_CLK;             /*     0x7038 - 0x703c     */
  volatile u_int32_t PLL_CONTROL2;                /*     0x703c - 0x7040     */
};

struct rtc_sync_reg {
  volatile char pad__0[0x7040];                   /*        0x0 - 0x7040     */
  volatile u_int32_t RTC_SYNC_RESET;              /*     0x7040 - 0x7044     */
  volatile u_int32_t RTC_SYNC_STATUS;             /*     0x7044 - 0x7048     */
  volatile u_int32_t RTC_SYNC_DERIVED;            /*     0x7048 - 0x704c     */
  volatile u_int32_t RTC_SYNC_FORCE_WAKE;         /*     0x704c - 0x7050     */
  volatile u_int32_t RTC_SYNC_INTR_CAUSE;         /*     0x7050 - 0x7054     */
  volatile u_int32_t RTC_SYNC_INTR_ENABLE;        /*     0x7054 - 0x7058     */
  volatile u_int32_t RTC_SYNC_INTR_MASK;          /*     0x7058 - 0x705c     */
};

struct merlin2_0_radio_reg_map {
  volatile char pad__0[0x7800];                   /*        0x0 - 0x7800     */
  volatile u_int32_t RXTXBB1_CH1;                 /*     0x7800 - 0x7804     */
  volatile u_int32_t RXTXBB2_CH1;                 /*     0x7804 - 0x7808     */
  volatile u_int32_t RXTXBB3_CH1;                 /*     0x7808 - 0x780c     */
  volatile u_int32_t RXTXBB4_CH1;                 /*     0x780c - 0x7810     */
  volatile u_int32_t RF2G1_CH1;                   /*     0x7810 - 0x7814     */
  volatile u_int32_t RF2G2_CH1;                   /*     0x7814 - 0x7818     */
  volatile u_int32_t RF5G1_CH1;                   /*     0x7818 - 0x781c     */
  volatile u_int32_t RF5G2_CH1;                   /*     0x781c - 0x7820     */
  volatile u_int32_t RF5G3_CH1;                   /*     0x7820 - 0x7824     */
  volatile u_int32_t RXTXBB1_CH0;                 /*     0x7824 - 0x7828     */
  volatile u_int32_t RXTXBB2_CH0;                 /*     0x7828 - 0x782c     */
  volatile u_int32_t RXTXBB3_CH0;                 /*     0x782c - 0x7830     */
  volatile u_int32_t RXTXBB4_CH0;                 /*     0x7830 - 0x7834     */
  volatile u_int32_t RF5G1_CH0;                   /*     0x7834 - 0x7838     */
  volatile u_int32_t RF5G2_CH0;                   /*     0x7838 - 0x783c     */
  volatile u_int32_t RF5G3_CH0;                   /*     0x783c - 0x7840     */
  volatile u_int32_t RF2G1_CH0;                   /*     0x7840 - 0x7844     */
  volatile u_int32_t RF2G2_CH0;                   /*     0x7844 - 0x7848     */
  volatile u_int32_t SYNTH1;                      /*     0x7848 - 0x784c     */
  volatile u_int32_t SYNTH2;                      /*     0x784c - 0x7850     */
  volatile u_int32_t SYNTH3;                      /*     0x7850 - 0x7854     */
  volatile u_int32_t SYNTH4;                      /*     0x7854 - 0x7858     */
  volatile u_int32_t SYNTH5;                      /*     0x7858 - 0x785c     */
  volatile u_int32_t SYNTH6;                      /*     0x785c - 0x7860     */
  volatile u_int32_t SYNTH7;                      /*     0x7860 - 0x7864     */
  volatile u_int32_t SYNTH8;                      /*     0x7864 - 0x7868     */
  volatile u_int32_t SYNTH9;                      /*     0x7868 - 0x786c     */
  volatile u_int32_t SYNTH10;                     /*     0x786c - 0x7870     */
  volatile u_int32_t SYNTH11;                     /*     0x7870 - 0x7874     */
  volatile u_int32_t BIAS1;                       /*     0x7874 - 0x7878     */
  volatile u_int32_t BIAS2;                       /*     0x7878 - 0x787c     */
  volatile u_int32_t BIAS3;                       /*     0x787c - 0x7880     */
  volatile u_int32_t BIAS4;                       /*     0x7880 - 0x7884     */
  volatile u_int32_t GAIN0;                       /*     0x7884 - 0x7888     */
  volatile u_int32_t GAIN1;                       /*     0x7888 - 0x788c     */
  volatile u_int32_t TOP0;                        /*     0x788c - 0x7890     */
  volatile u_int32_t TOP1;                        /*     0x7890 - 0x7894     */
  volatile u_int32_t TOP2;                        /*     0x7894 - 0x7898     */
  volatile u_int32_t TOP3;                        /*     0x7898 - 0x789c     */
};

struct analog_intf_reg_csr {
  volatile char pad__0[0x7900];                   /*        0x0 - 0x7900     */
  volatile u_int32_t SW_OVERRIDE;                 /*     0x7900 - 0x7904     */
  volatile u_int32_t SIN_VAL;                     /*     0x7904 - 0x7908     */
  volatile u_int32_t SW_SCLK;                     /*     0x7908 - 0x790c     */
  volatile u_int32_t SW_CNTL;                     /*     0x790c - 0x7910     */
};

struct mac_pcu_reg {
  volatile char pad__0[0x8000];                   /*        0x0 - 0x8000     */
  volatile u_int32_t MAC_PCU_STA_ADDR_L32;        /*     0x8000 - 0x8004     */
  volatile u_int32_t MAC_PCU_STA_ADDR_U16;        /*     0x8004 - 0x8008     */
  volatile u_int32_t MAC_PCU_BSSID_L32;           /*     0x8008 - 0x800c     */
  volatile u_int32_t MAC_PCU_BSSID_U16;           /*     0x800c - 0x8010     */
  volatile u_int32_t MAC_PCU_BCN_RSSI_AVE;        /*     0x8010 - 0x8014     */
  volatile u_int32_t MAC_PCU_ACK_CTS_TIMEOUT;     /*     0x8014 - 0x8018     */
  volatile u_int32_t MAC_PCU_BCN_RSSI_CTL;        /*     0x8018 - 0x801c     */
  volatile u_int32_t MAC_PCU_USEC_LATENCY;        /*     0x801c - 0x8020     */
  volatile u_int32_t MAC_PCU_RESET_TSF;           /*     0x8020 - 0x8024     */
  volatile char pad__1[0x14];                     /*     0x8024 - 0x8038     */
  volatile u_int32_t MAC_PCU_MAX_CFP_DUR;         /*     0x8038 - 0x803c     */
  volatile u_int32_t MAC_PCU_RX_FILTER;           /*     0x803c - 0x8040     */
  volatile u_int32_t MAC_PCU_MCAST_FILTER_L32;    /*     0x8040 - 0x8044     */
  volatile u_int32_t MAC_PCU_MCAST_FILTER_U32;    /*     0x8044 - 0x8048     */
  volatile u_int32_t MAC_PCU_DIAG_SW;             /*     0x8048 - 0x804c     */
  volatile u_int32_t MAC_PCU_TSF_L32;             /*     0x804c - 0x8050     */
  volatile u_int32_t MAC_PCU_TSF_U32;             /*     0x8050 - 0x8054     */
  volatile u_int32_t MAC_PCU_TST_ADDAC;           /*     0x8054 - 0x8058     */
  volatile u_int32_t MAC_PCU_DEF_ANTENNA;         /*     0x8058 - 0x805c     */
  volatile u_int32_t MAC_PCU_AES_MUTE_MASK_0;     /*     0x805c - 0x8060     */
  volatile u_int32_t MAC_PCU_AES_MUTE_MASK_1;     /*     0x8060 - 0x8064     */
  volatile u_int32_t MAC_PCU_GATED_CLKS;          /*     0x8064 - 0x8068     */
  volatile u_int32_t MAC_PCU_OBS_BUS_2;           /*     0x8068 - 0x806c     */
  volatile u_int32_t MAC_PCU_OBS_BUS_1;           /*     0x806c - 0x8070     */
  volatile u_int32_t MAC_PCU_DYM_MIMO_PWR_SAVE;   /*     0x8070 - 0x8074     */
  volatile u_int32_t MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_LSB;
                                                  /*     0x8074 - 0x8078     */
  volatile u_int32_t MAC_PCU_TDMA_TXFRAME_START_TIME_TRIGGER_MSB;
                                                  /*     0x8078 - 0x807c     */
  volatile char pad__2[0x4];                      /*     0x807c - 0x8080     */
  volatile u_int32_t MAC_PCU_LAST_BEACON_TSF;     /*     0x8080 - 0x8084     */
  volatile u_int32_t MAC_PCU_NAV;                 /*     0x8084 - 0x8088     */
  volatile u_int32_t MAC_PCU_RTS_SUCCESS_CNT;     /*     0x8088 - 0x808c     */
  volatile u_int32_t MAC_PCU_RTS_FAIL_CNT;        /*     0x808c - 0x8090     */
  volatile u_int32_t MAC_PCU_ACK_FAIL_CNT;        /*     0x8090 - 0x8094     */
  volatile u_int32_t MAC_PCU_FCS_FAIL_CNT;        /*     0x8094 - 0x8098     */
  volatile u_int32_t MAC_PCU_BEACON_CNT;          /*     0x8098 - 0x809c     */
  volatile u_int32_t MAC_PCU_TDMA_SLOT_ALERT_CNTL;
                                                  /*     0x809c - 0x80a0     */
  volatile u_int32_t MAC_PCU_BASIC_SET;           /*     0x80a0 - 0x80a4     */
  volatile u_int32_t MAC_PCU_MGMT_SEQ;            /*     0x80a4 - 0x80a8     */
  volatile u_int32_t MAC_PCU_BF_RPT1;             /*     0x80a8 - 0x80ac     */
  volatile u_int32_t MAC_PCU_BF_RPT2;             /*     0x80ac - 0x80b0     */
  volatile u_int32_t MAC_PCU_TX_ANT_1;            /*     0x80b0 - 0x80b4     */
  volatile u_int32_t MAC_PCU_TX_ANT_2;            /*     0x80b4 - 0x80b8     */
  volatile u_int32_t MAC_PCU_TX_ANT_3;            /*     0x80b8 - 0x80bc     */
  volatile u_int32_t MAC_PCU_TX_ANT_4;            /*     0x80bc - 0x80c0     */
  volatile u_int32_t MAC_PCU_XRMODE;              /*     0x80c0 - 0x80c4     */
  volatile u_int32_t MAC_PCU_XRDEL;               /*     0x80c4 - 0x80c8     */
  volatile u_int32_t MAC_PCU_XRTO;                /*     0x80c8 - 0x80cc     */
  volatile u_int32_t MAC_PCU_XRCRP;               /*     0x80cc - 0x80d0     */
  volatile u_int32_t MAC_PCU_XRSTMP;              /*     0x80d0 - 0x80d4     */
  volatile u_int32_t MAC_PCU_SLP1;                /*     0x80d4 - 0x80d8     */
  volatile u_int32_t MAC_PCU_SLP2;                /*     0x80d8 - 0x80dc     */
  volatile u_int32_t MAC_PCU_SELF_GEN_DEFAULT;    /*     0x80dc - 0x80e0     */
  volatile u_int32_t MAC_PCU_ADDR1_MASK_L32;      /*     0x80e0 - 0x80e4     */
  volatile u_int32_t MAC_PCU_ADDR1_MASK_U16;      /*     0x80e4 - 0x80e8     */
  volatile u_int32_t MAC_PCU_TPC;                 /*     0x80e8 - 0x80ec     */
  volatile u_int32_t MAC_PCU_TX_FRAME_CNT;        /*     0x80ec - 0x80f0     */
  volatile u_int32_t MAC_PCU_RX_FRAME_CNT;        /*     0x80f0 - 0x80f4     */
  volatile u_int32_t MAC_PCU_RX_CLEAR_CNT;        /*     0x80f4 - 0x80f8     */
  volatile u_int32_t MAC_PCU_CYCLE_CNT;           /*     0x80f8 - 0x80fc     */
  volatile u_int32_t MAC_PCU_QUIET_TIME_1;        /*     0x80fc - 0x8100     */
  volatile u_int32_t MAC_PCU_QUIET_TIME_2;        /*     0x8100 - 0x8104     */
  volatile char pad__3[0x4];                      /*     0x8104 - 0x8108     */
  volatile u_int32_t MAC_PCU_QOS_NO_ACK;          /*     0x8108 - 0x810c     */
  volatile u_int32_t MAC_PCU_PHY_ERROR_MASK;      /*     0x810c - 0x8110     */
  volatile u_int32_t MAC_PCU_XRLAT;               /*     0x8110 - 0x8114     */
  volatile u_int32_t MAC_PCU_RXBUF;               /*     0x8114 - 0x8118     */
  volatile u_int32_t MAC_PCU_MIC_QOS_CONTROL;     /*     0x8118 - 0x811c     */
  volatile u_int32_t MAC_PCU_MIC_QOS_SELECT;      /*     0x811c - 0x8120     */
  volatile u_int32_t MAC_PCU_MISC_MODE;           /*     0x8120 - 0x8124     */
  volatile u_int32_t MAC_PCU_FILTER_OFDM_CNT;     /*     0x8124 - 0x8128     */
  volatile u_int32_t MAC_PCU_FILTER_CCK_CNT;      /*     0x8128 - 0x812c     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_1;       /*     0x812c - 0x8130     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_1_MASK;  /*     0x8130 - 0x8134     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_2;       /*     0x8134 - 0x8138     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_2_MASK;  /*     0x8138 - 0x813c     */
  volatile u_int32_t MAC_PCU_TSF_THRESHOLD;       /*     0x813c - 0x8140     */
  volatile char pad__4[0x4];                      /*     0x8140 - 0x8144     */
  volatile u_int32_t MAC_PCU_PHY_ERROR_EIFS_MASK; /*     0x8144 - 0x8148     */
  volatile char pad__5[0x20];                     /*     0x8148 - 0x8168     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_3;       /*     0x8168 - 0x816c     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_3_MASK;  /*     0x816c - 0x8170     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE;      /*     0x8170 - 0x8174     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_WL_WEIGHTS0;
                                                  /*     0x8174 - 0x8178     */
  volatile u_int32_t MAC_PCU_HCF_TIMEOUT;         /*     0x8178 - 0x817c     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE2;     /*     0x817c - 0x8180     */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS2[16]; /*     0x8180 - 0x81c0     */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS2_MODE;
                                                  /*     0x81c0 - 0x81c4     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_WL_WEIGHTS1;
                                                  /*     0x81c4 - 0x81c8     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_TSF_BT_ACTIVE;
                                                  /*     0x81c8 - 0x81cc     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_TSF_BT_PRIORITY;
                                                  /*     0x81cc - 0x81d0     */
  volatile u_int32_t MAC_PCU_TXSIFS;              /*     0x81d0 - 0x81d4     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_MODE3;     /*     0x81d4 - 0x81d8     */
  volatile char pad__6[0x14];                     /*     0x81d8 - 0x81ec     */
  volatile u_int32_t MAC_PCU_TXOP_X;              /*     0x81ec - 0x81f0     */
  volatile u_int32_t MAC_PCU_TXOP_0_3;            /*     0x81f0 - 0x81f4     */
  volatile u_int32_t MAC_PCU_TXOP_4_7;            /*     0x81f4 - 0x81f8     */
  volatile u_int32_t MAC_PCU_TXOP_8_11;           /*     0x81f8 - 0x81fc     */
  volatile u_int32_t MAC_PCU_TXOP_12_15;          /*     0x81fc - 0x8200     */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS[16];  /*     0x8200 - 0x8240     */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS_MODE; /*     0x8240 - 0x8244     */
  volatile u_int32_t MAC_PCU_SLP32_MODE;          /*     0x8244 - 0x8248     */
  volatile u_int32_t MAC_PCU_SLP32_WAKE;          /*     0x8248 - 0x824c     */
  volatile u_int32_t MAC_PCU_SLP32_INC;           /*     0x824c - 0x8250     */
  volatile u_int32_t MAC_PCU_SLP_MIB1;            /*     0x8250 - 0x8254     */
  volatile u_int32_t MAC_PCU_SLP_MIB2;            /*     0x8254 - 0x8258     */
  volatile u_int32_t MAC_PCU_SLP_MIB3;            /*     0x8258 - 0x825c     */
  volatile u_int32_t MAC_PCU_WOW1;                /*     0x825c - 0x8260     */
  volatile u_int32_t MAC_PCU_WOW2;                /*     0x8260 - 0x8264     */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER;      /*     0x8264 - 0x8268     */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER_32L;  /*     0x8268 - 0x826c     */
  volatile u_int32_t MAC_PCU_LOGIC_ANALYZER_16U;  /*     0x826c - 0x8270     */
  volatile u_int32_t MAC_PCU_WOW3_BEACON_FAIL;    /*     0x8270 - 0x8274     */
  volatile u_int32_t MAC_PCU_WOW3_BEACON;         /*     0x8274 - 0x8278     */
  volatile u_int32_t MAC_PCU_WOW3_KEEP_ALIVE;     /*     0x8278 - 0x827c     */
  volatile u_int32_t MAC_PCU_WOW_KA;              /*     0x827c - 0x8280     */
  volatile char pad__7[0x4];                      /*     0x8280 - 0x8284     */
  volatile u_int32_t PCU_1US;                     /*     0x8284 - 0x8288     */
  volatile u_int32_t PCU_KA;                      /*     0x8288 - 0x828c     */
  volatile u_int32_t WOW_EXACT;                   /*     0x828c - 0x8290     */
  volatile char pad__8[0x4];                      /*     0x8290 - 0x8294     */
  volatile u_int32_t PCU_WOW4;                    /*     0x8294 - 0x8298     */
  volatile u_int32_t PCU_WOW5;                    /*     0x8298 - 0x829c     */
  volatile u_int32_t MAC_PCU_PHY_ERR_CNT_MASK_CONT;
                                                  /*     0x829c - 0x82a0     */
  volatile char pad__9[0x60];                     /*     0x82a0 - 0x8300     */
  volatile u_int32_t MAC_PCU_AZIMUTH_MODE;        /*     0x8300 - 0x8304     */
  volatile char pad__10[0x10];                    /*     0x8304 - 0x8314     */
  volatile u_int32_t MAC_PCU_AZIMUTH_TIME_STAMP;  /*     0x8314 - 0x8318     */
  volatile u_int32_t MAC_PCU_20_40_MODE;          /*     0x8318 - 0x831c     */
  volatile u_int32_t MAC_PCU_H_XFER_TIMEOUT;      /*     0x831c - 0x8320     */
  volatile char pad__11[0x8];                     /*     0x8320 - 0x8328     */
  volatile u_int32_t MAC_PCU_RX_CLEAR_DIFF_CNT;   /*     0x8328 - 0x832c     */
  volatile u_int32_t MAC_PCU_SELF_GEN_ANTENNA_MASK;
                                                  /*     0x832c - 0x8330     */
  volatile u_int32_t MAC_PCU_BA_BAR_CONTROL;      /*     0x8330 - 0x8334     */
  volatile u_int32_t MAC_PCU_LEGACY_PLCP_SPOOF;   /*     0x8334 - 0x8338     */
  volatile u_int32_t MAC_PCU_PHY_ERROR_MASK_CONT; /*     0x8338 - 0x833c     */
  volatile u_int32_t MAC_PCU_TX_TIMER;            /*     0x833c - 0x8340     */
  volatile u_int32_t MAC_PCU_TXBUF_CTRL;          /*     0x8340 - 0x8344     */
  volatile u_int32_t MAC_PCU_MISC_MODE2;          /*     0x8344 - 0x8348     */
  volatile u_int32_t MAC_PCU_ALT_AES_MUTE_MASK;   /*     0x8348 - 0x834c     */
  volatile u_int32_t MAC_PCU_WOW6;                /*     0x834c - 0x8350     */
  volatile u_int32_t ASYNC_FIFO_REG1;             /*     0x8350 - 0x8354     */
  volatile u_int32_t ASYNC_FIFO_REG2;             /*     0x8354 - 0x8358     */
  volatile u_int32_t ASYNC_FIFO_REG3;             /*     0x8358 - 0x835c     */
  volatile u_int32_t MAC_PCU_WOW5;                /*     0x835c - 0x8360     */
  volatile u_int32_t MAC_PCU_WOW_LENGTH1;         /*     0x8360 - 0x8364     */
  volatile u_int32_t MAC_PCU_WOW_LENGTH2;         /*     0x8364 - 0x8368     */
  volatile u_int32_t WOW_PATTERN_MATCH_LESS_THAN_256_BYTES;
                                                  /*     0x8368 - 0x836c     */
  volatile char pad__12[0x4];                     /*     0x836c - 0x8370     */
  volatile u_int32_t MAC_PCU_WOW4;                /*     0x8370 - 0x8374     */
  volatile u_int32_t WOW2_EXACT;                  /*     0x8374 - 0x8378     */
  volatile u_int32_t PCU_WOW6;                    /*     0x8378 - 0x837c     */
  volatile u_int32_t PCU_WOW7;                    /*     0x837c - 0x8380     */
  volatile u_int32_t MAC_PCU_WOW_LENGTH3;         /*     0x8380 - 0x8384     */
  volatile u_int32_t MAC_PCU_WOW_LENGTH4;         /*     0x8384 - 0x8388     */
  volatile u_int32_t MAC_PCU_LOCATION_MODE_CONTROL;
                                                  /*     0x8388 - 0x838c     */
  volatile u_int32_t MAC_PCU_LOCATION_MODE_TIMER; /*     0x838c - 0x8390     */
  volatile u_int32_t MAC_PCU_TSF2_L32;            /*     0x8390 - 0x8394     */
  volatile u_int32_t MAC_PCU_TSF2_U32;            /*     0x8394 - 0x8398     */
  volatile u_int32_t MAC_PCU_BSSID2_L32;          /*     0x8398 - 0x839c     */
  volatile u_int32_t MAC_PCU_BSSID2_U16;          /*     0x839c - 0x83a0     */
  volatile u_int32_t MAC_PCU_DIRECT_CONNECT;      /*     0x83a0 - 0x83a4     */
  volatile u_int32_t MAC_PCU_TID_TO_AC;           /*     0x83a4 - 0x83a8     */
  volatile u_int32_t MAC_PCU_HP_QUEUE;            /*     0x83a8 - 0x83ac     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS0;
                                                  /*     0x83ac - 0x83b0     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS1;
                                                  /*     0x83b0 - 0x83b4     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS2;
                                                  /*     0x83b4 - 0x83b8     */
  volatile u_int32_t MAC_PCU_BLUETOOTH_BT_WEIGHTS3;
                                                  /*     0x83b8 - 0x83bc     */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT0; /*     0x83bc - 0x83c0     */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT1; /*     0x83c0 - 0x83c4     */
  volatile u_int32_t MAC_PCU_AGC_SATURATION_CNT2; /*     0x83c4 - 0x83c8     */
  volatile u_int32_t MAC_PCU_HW_BCN_PROC1;        /*     0x83c8 - 0x83cc     */
  volatile u_int32_t MAC_PCU_HW_BCN_PROC2;        /*     0x83cc - 0x83d0     */
  volatile u_int32_t MAC_PCU_MISC_MODE3;          /*     0x83d0 - 0x83d4     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_FILTER_RSSI_AVE;     /*     0x83d4 - 0x83d8     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_GENERIC_TIMERS_TSF_SEL;
                                                  /*     0x83d8 - 0x83dc     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_BEACON2_CNT;         /*     0x83dc - 0x83e0     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_LAST_BEACON2_TSF;    /*     0x83e0 - 0x83e4     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_BMISS_TIMEOUT;       /*     0x83e4 - 0x83e8     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_BMISS2_TIMEOUT;      /*     0x83e8 - 0x83ec     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_SLP3;                /*     0x83ec - 0x83f0     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_BCN_RSSI_CTL2;       /*     0x83f0 - 0x83f4     */
  /* Jupiter */
  volatile u_int32_t MAC_PCU_PHY_ERROR_AIFS_MASK; /*     0x83f4 - 0x83f8     */
  /* Jupiter_20 */
  volatile u_int32_t MAC_PCU_TBD_FILTER;          /*     0x83f8 - 0x83fc     */
  /* Jupiter_20 */
  volatile u_int32_t MAC_PCU_MISC_MODE4;          /*     0x83fc - 0x8400     */
  volatile u_int32_t MAC_PCU_TXBUF_BA[64];        /*     0x8400 - 0x8500     */
  /* Jupiter_20 */
  volatile u_int32_t MAC_PCU_SLP4;                /*     0x8500 - 0x8504     */
  volatile char pad__13[0x2fc];                   /*     0x8504 - 0x8800     */
  volatile u_int32_t MAC_PCU_KEY_CACHE[1024];     /*     0x8800 - 0x9800     */
  volatile char pad__14[0x4800];                  /*     0x9800 - 0xe000     */
  volatile u_int32_t MAC_PCU_BUF[2048];           /*     0xe000 - 0x10000    */
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
  /* Poseidon, Jupiter */
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
  /* Jupiter_20 */
  volatile u_int32_t BB_chn_tables_intf_addr;     /*      0x3f0 - 0x3f4      */
  /* Jupiter_20 */
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
  /* Poseidon, Jupiter_10 */
  volatile u_int32_t BB_bb_reg_page_control;      /*       0x1c - 0x20       */
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
  /* Poseidon, Jupiter */
  volatile u_int32_t BB_bt_coex_2;                /*       0x58 - 0x5c       */
  /* Poseidon, Jupiter */
  volatile u_int32_t BB_bt_coex_3;                /*       0x5c - 0x60       */
  /* Poseidon, Jupiter */
  volatile u_int32_t BB_bt_coex_4;                /*       0x60 - 0x64       */
  /* Poseidon, Jupiter */
  volatile u_int32_t BB_bt_coex_5;                /*       0x64 - 0x68       */
  /* Jupiter_20 */
  volatile u_int32_t BB_redpwr_ctrl_1;            /*       0x68 - 0x6c       */
  /* Jupiter_20 */
  volatile u_int32_t BB_redpwr_ctrl_2;            /*       0x6c - 0x70       */
  volatile char pad__0[0x110];                    /*       0x70 - 0x180      */
  volatile u_int32_t BB_rssi_b0;                  /*      0x180 - 0x184      */
  volatile u_int32_t BB_spur_est_cck_report_b0;   /*      0x184 - 0x188      */
  volatile u_int32_t BB_agc_dig_dc_status_i_b0;   /*      0x188 - 0x18c      */
  volatile u_int32_t BB_agc_dig_dc_status_q_b0;   /*      0x18c - 0x190      */
  /* Poseidon, Jupiter */
  volatile u_int32_t BB_dc_cal_status_b0;         /*      0x190 - 0x194      */
  volatile char pad__1[0x2c];                     /*      0x194 - 0x1c0      */
  volatile u_int32_t BB_bbb_sig_detect;           /*      0x1c0 - 0x1c4      */
  volatile u_int32_t BB_bbb_dagc_ctrl;            /*      0x1c4 - 0x1c8      */
  volatile u_int32_t BB_iqcorr_ctrl_cck;          /*      0x1c8 - 0x1cc      */
  volatile u_int32_t BB_cck_spur_mit;             /*      0x1cc - 0x1d0      */
  /* Not Poseidon */
  volatile u_int32_t BB_mrc_cck_ctrl;             /*      0x1d0 - 0x1d4      */
  /* Jupiter_20 */
  volatile u_int32_t BB_cck_blocker_det;          /*      0x1d4 - 0x1d8      */
  volatile char pad__2[0x28];                     /*      0x1d8 - 0x200      */
  volatile u_int32_t BB_rx_ocgain[128];           /*      0x200 - 0x400      */
};

struct sm_reg_map {
  volatile u_int32_t BB_D2_chip_id;               /*        0x0 - 0x4        */
  volatile u_int32_t BB_gen_controls;             /*        0x4 - 0x8        */
  volatile u_int32_t BB_modes_select;             /*        0x8 - 0xc        */
  volatile u_int32_t BB_active;                   /*        0xc - 0x10       */
  /* Poseidon, Jupiter_10 */
  volatile u_int32_t BB_bb_reg_page;              /*       0x10 - 0x14       */
  volatile char pad__0[0xc];                      /*       0x14 - 0x20       */
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
  /* Jupiter */
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
  volatile u_int32_t BB_tpc_19;                   /*      0x240 - 0x244      */
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
  union {
      struct {
          volatile u_int32_t BB_tx_gain_tab_pal_1;        /*      0x380 - 0x384      */
          volatile u_int32_t BB_tx_gain_tab_pal_2;        /*      0x384 - 0x388      */
          volatile u_int32_t BB_tx_gain_tab_pal_3;        /*      0x388 - 0x38c      */
          volatile u_int32_t BB_tx_gain_tab_pal_4;        /*      0x38c - 0x390      */
          volatile u_int32_t BB_tx_gain_tab_pal_5;        /*      0x390 - 0x394      */
          volatile u_int32_t BB_tx_gain_tab_pal_6;        /*      0x394 - 0x398      */
          volatile u_int32_t BB_tx_gain_tab_pal_7;        /*      0x398 - 0x39c      */
          volatile u_int32_t BB_tx_gain_tab_pal_8;        /*      0x39c - 0x3a0      */
          volatile u_int32_t BB_tx_gain_tab_pal_9;        /*      0x3a0 - 0x3a4      */
          volatile u_int32_t BB_tx_gain_tab_pal_10;       /*      0x3a4 - 0x3a8      */
          volatile u_int32_t BB_tx_gain_tab_pal_11;       /*      0x3a8 - 0x3ac      */
          volatile u_int32_t BB_tx_gain_tab_pal_12;       /*      0x3ac - 0x3b0      */
          volatile u_int32_t BB_tx_gain_tab_pal_13;       /*      0x3b0 - 0x3b4      */
          volatile u_int32_t BB_tx_gain_tab_pal_14;       /*      0x3b4 - 0x3b8      */
          volatile u_int32_t BB_tx_gain_tab_pal_15;       /*      0x3b8 - 0x3bc      */
          volatile u_int32_t BB_tx_gain_tab_pal_16;       /*      0x3bc - 0x3c0      */
          volatile u_int32_t BB_tx_gain_tab_pal_17;       /*      0x3c0 - 0x3c4      */
          volatile u_int32_t BB_tx_gain_tab_pal_18;       /*      0x3c4 - 0x3c8      */
          volatile u_int32_t BB_tx_gain_tab_pal_19;       /*      0x3c8 - 0x3cc      */
          volatile u_int32_t BB_tx_gain_tab_pal_20;       /*      0x3cc - 0x3d0      */
          volatile u_int32_t BB_tx_gain_tab_pal_21;       /*      0x3d0 - 0x3d4      */
          volatile u_int32_t BB_tx_gain_tab_pal_22;       /*      0x3d4 - 0x3d8      */
          volatile u_int32_t BB_tx_gain_tab_pal_23;       /*      0x3d8 - 0x3dc      */
          volatile u_int32_t BB_tx_gain_tab_pal_24;       /*      0x3dc - 0x3e0      */
          volatile u_int32_t BB_tx_gain_tab_pal_25;       /*      0x3e0 - 0x3e4      */
          volatile u_int32_t BB_tx_gain_tab_pal_26;       /*      0x3e4 - 0x3e8      */
          volatile u_int32_t BB_tx_gain_tab_pal_27;       /*      0x3e8 - 0x3ec      */
          volatile u_int32_t BB_tx_gain_tab_pal_28;       /*      0x3ec - 0x3f0      */
          volatile u_int32_t BB_tx_gain_tab_pal_29;       /*      0x3f0 - 0x3f4      */
          volatile u_int32_t BB_tx_gain_tab_pal_30;       /*      0x3f4 - 0x3f8      */
          volatile u_int32_t BB_tx_gain_tab_pal_31;       /*      0x3f8 - 0x3fc      */
          volatile u_int32_t BB_tx_gain_tab_pal_32;       /*      0x3fc - 0x400      */
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
          volatile u_int32_t BB_txiqcal_start;            /*      0x440 - 0x444      */
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
          volatile char pad__11[0x114];                   /*      0x4ac - 0x5c0      */
      } Osprey;
      struct {
          volatile u_int32_t BB_caltx_gain_set_0;         /*      0x380 - 0x384      */
          volatile u_int32_t BB_caltx_gain_set_2;         /*      0x384 - 0x388      */
          volatile u_int32_t BB_caltx_gain_set_4;         /*      0x388 - 0x38c      */
          volatile u_int32_t BB_caltx_gain_set_6;         /*      0x38c - 0x390      */
          volatile u_int32_t BB_caltx_gain_set_8;         /*      0x390 - 0x394      */
          volatile u_int32_t BB_caltx_gain_set_10;        /*      0x394 - 0x398      */
          volatile u_int32_t BB_caltx_gain_set_12;        /*      0x398 - 0x39c      */
          volatile u_int32_t BB_caltx_gain_set_14;        /*      0x39c - 0x3a0      */
          volatile u_int32_t BB_caltx_gain_set_16;        /*      0x3a0 - 0x3a4      */
          volatile u_int32_t BB_caltx_gain_set_18;        /*      0x3a4 - 0x3a8      */
          volatile u_int32_t BB_caltx_gain_set_20;        /*      0x3a8 - 0x3ac      */
          volatile u_int32_t BB_caltx_gain_set_22;        /*      0x3ac - 0x3b0      */
          volatile u_int32_t BB_caltx_gain_set_24;        /*      0x3b0 - 0x3b4      */
          volatile u_int32_t BB_caltx_gain_set_26;        /*      0x3b4 - 0x3b8      */
          volatile u_int32_t BB_caltx_gain_set_28;        /*      0x3b8 - 0x3bc      */
          volatile u_int32_t BB_caltx_gain_set_30;        /*      0x3bc - 0x3c0      */
          volatile char pad__11[0x4];                     /*      0x3c0 - 0x3c4      */
          volatile u_int32_t BB_txiqcal_control_0;        /*      0x3c4 - 0x3c8      */
          volatile u_int32_t BB_txiqcal_control_1;        /*      0x3c8 - 0x3cc      */
          volatile u_int32_t BB_txiqcal_control_2;        /*      0x3cc - 0x3d0      */
          volatile u_int32_t BB_txiq_corr_coeff_01_b0;    /*      0x3d0 - 0x3d4      */
          volatile u_int32_t BB_txiq_corr_coeff_23_b0;    /*      0x3d4 - 0x3d8      */
          volatile u_int32_t BB_txiq_corr_coeff_45_b0;    /*      0x3d8 - 0x3dc      */
          volatile u_int32_t BB_txiq_corr_coeff_67_b0;    /*      0x3dc - 0x3e0      */
          volatile u_int32_t BB_txiq_corr_coeff_89_b0;    /*      0x3e0 - 0x3e4      */
          volatile u_int32_t BB_txiq_corr_coeff_ab_b0;    /*      0x3e4 - 0x3e8      */
          volatile u_int32_t BB_txiq_corr_coeff_cd_b0;    /*      0x3e8 - 0x3ec      */
          volatile u_int32_t BB_txiq_corr_coeff_ef_b0;    /*      0x3ec - 0x3f0      */
          volatile u_int32_t BB_txiqcal_status_b0;        /*      0x3f0 - 0x3f4      */
          volatile char pad__12[0x16c];                   /*      0x3f4 - 0x560      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_0;      /*      0x560 - 0x564      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_4;      /*      0x564 - 0x568      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_8;      /*      0x568 - 0x56c      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_12;     /*      0x56c - 0x570      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_16;     /*      0x570 - 0x574      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_20;     /*      0x574 - 0x578      */
          volatile u_int32_t BB_cal_rxbb_gain_tbl_24;     /*      0x578 - 0x57c      */
          volatile char pad__13[0x4];                     /*      0x57c - 0x580      */
          volatile u_int32_t BB_paprd_trainer_cntl1;      /*      0x580 - 0x584      */
          volatile u_int32_t BB_paprd_trainer_cntl2;      /*      0x584 - 0x588      */
          volatile u_int32_t BB_paprd_trainer_cntl3;      /*      0x588 - 0x58c      */
          volatile u_int32_t BB_paprd_trainer_cntl4;      /*      0x58c - 0x590      */
          volatile u_int32_t BB_paprd_trainer_stat1;      /*      0x590 - 0x594      */
          volatile u_int32_t BB_paprd_trainer_stat2;      /*      0x594 - 0x598      */
          volatile u_int32_t BB_paprd_trainer_stat3;      /*      0x598 - 0x59c      */
          volatile char pad__14[0x24];                    /*      0x59c - 0x5c0      */
      } Poseidon;
      struct {
          volatile char pad__11[0x80];                    /*      0x380 - 0x400      */
          /* 0x400 - 0x4b0 same as Osprey - start */
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
          volatile char pad__13[0x4];                     /*      0x4ac - 0x4b0      */
          /* 0x400 - 0x4b0 same as Osprey - end */
          volatile u_int32_t BB_aic_ctrl_0_b0;            /*      0x4b0 - 0x4b4      */
          volatile u_int32_t BB_aic_ctrl_1_b0;            /*      0x4b4 - 0x4b8      */
          volatile u_int32_t BB_aic_ctrl_2_b0;            /*      0x4b8 - 0x4bc      */
          volatile u_int32_t BB_aic_ctrl_3_b0;            /*      0x4bc - 0x4c0      */
          volatile u_int32_t BB_aic_stat_0_b0;            /*      0x4c0 - 0x4c4      */
          volatile u_int32_t BB_aic_stat_1_b0;            /*      0x4c4 - 0x4c8      */
          volatile char pad__14[0xf8];                    /*      0x4c8 - 0x5c0      */
      } Jupiter_10;
      struct {
          volatile u_int32_t BB_rtt_ctrl;                 /*      0x380 - 0x384      */
          volatile u_int32_t BB_rtt_table_sw_intf_b0;     /*      0x384 - 0x388      */
          volatile u_int32_t BB_rtt_table_sw_intf_1_b0;   /*      0x388 - 0x38c      */
          volatile char pad__11[0x74];                    /*      0x38c - 0x400      */
          /* 0x400 - 0x4b0 same as Osprey - start */
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
          volatile char pad__13[0x4];                     /*      0x4ac - 0x4b0      */
          /* 0x400 - 0x4b0 same as Osprey - end */
          volatile u_int32_t BB_aic_ctrl_0_b0;            /*      0x4b0 - 0x4b4      */
          volatile u_int32_t BB_aic_ctrl_1_b0;            /*      0x4b4 - 0x4b8      */
          volatile u_int32_t BB_aic_ctrl_2_b0;            /*      0x4b8 - 0x4bc      */
          volatile u_int32_t BB_aic_ctrl_3_b0;            /*      0x4bc - 0x4c0      */
          volatile u_int32_t BB_aic_ctrl_4_b0;            /*      0x4c0 - 0x4c4      */
          volatile u_int32_t BB_aic_stat_0_b0;            /*      0x4c4 - 0x4c8      */
          volatile u_int32_t BB_aic_stat_1_b0;            /*      0x4c8 - 0x4cc      */
          volatile u_int32_t BB_aic_stat_2_b0;            /*      0x4cc - 0x4d0      */
          volatile char pad__14[0xf0];                    /*      0x4d0 - 0x5c0      */
      } Jupiter_20;
  } overlay_0xa580;
  volatile u_int32_t BB_panic_watchdog_status;    /*      0x5c0 - 0x5c4      */
  volatile u_int32_t BB_panic_watchdog_ctrl_1;    /*      0x5c4 - 0x5c8      */
  volatile u_int32_t BB_panic_watchdog_ctrl_2;    /*      0x5c8 - 0x5cc      */
  volatile u_int32_t BB_bluetooth_cntl;           /*      0x5cc - 0x5d0      */
  volatile u_int32_t BB_phyonly_warm_reset;       /*      0x5d0 - 0x5d4      */
  volatile u_int32_t BB_phyonly_control;          /*      0x5d4 - 0x5d8      */
  volatile char pad__12[0x4];                     /*      0x5d8 - 0x5dc      */
  volatile u_int32_t BB_eco_ctrl;                 /*      0x5dc - 0x5e0      */
  volatile char pad__13[0x10];                    /*      0x5e0 - 0x5f0      */
  /* Jupiter */
  volatile u_int32_t BB_tables_intf_addr_b0;      /*      0x5f0 - 0x5f4      */
  /* Jupiter */
  volatile u_int32_t BB_tables_intf_data_b0;      /*      0x5f4 - 0x5f8      */
};

struct chn1_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS1;       /*        0x0 - 0x4        */
  volatile char pad__0[0x2c];                     /*        0x4 - 0x30       */
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
  /* Jupiter_20 */
  volatile u_int32_t BB_chn1_tables_intf_addr;    /*      0x3f0 - 0x3f4      */
  /* Jupiter_20 */
  volatile u_int32_t BB_chn1_tables_intf_data;    /*      0x3f4 - 0x3f8      */
};

struct chn_ext_reg_map {
  volatile u_int32_t BB_paprd_pre_post_scale_0_1_b0;
                                                  /*        0x0 - 0x4        */
  volatile u_int32_t BB_paprd_pre_post_scale_1_1_b0;
                                                  /*        0x4 - 0x8        */
  volatile u_int32_t BB_paprd_pre_post_scale_2_1_b0;
                                                  /*        0x8 - 0xc        */
  volatile u_int32_t BB_paprd_pre_post_scale_3_1_b0;
                                                  /*        0xc - 0x10       */
  volatile u_int32_t BB_paprd_pre_post_scale_4_1_b0;
                                                  /*       0x10 - 0x14       */
  volatile u_int32_t BB_paprd_pre_post_scale_5_1_b0;
                                                  /*       0x14 - 0x18       */
  volatile u_int32_t BB_paprd_pre_post_scale_6_1_b0;
                                                  /*       0x18 - 0x1c       */
  volatile u_int32_t BB_paprd_pre_post_scale_7_1_b0;
                                                  /*       0x1c - 0x20       */
  volatile u_int32_t BB_paprd_pre_post_scale_0_2_b0;
                                                  /*       0x20 - 0x24       */
  volatile u_int32_t BB_paprd_pre_post_scale_1_2_b0;
                                                  /*       0x24 - 0x28       */
  volatile u_int32_t BB_paprd_pre_post_scale_2_2_b0;
                                                  /*       0x28 - 0x2c       */
  volatile u_int32_t BB_paprd_pre_post_scale_3_2_b0;
                                                  /*       0x2c - 0x30       */
  volatile u_int32_t BB_paprd_pre_post_scale_4_2_b0;
                                                  /*       0x30 - 0x34       */
  volatile u_int32_t BB_paprd_pre_post_scale_5_2_b0;
                                                  /*       0x34 - 0x38       */
  volatile u_int32_t BB_paprd_pre_post_scale_6_2_b0;
                                                  /*       0x38 - 0x3c       */
  volatile u_int32_t BB_paprd_pre_post_scale_7_2_b0;
                                                  /*       0x3c - 0x40       */
  volatile u_int32_t BB_paprd_pre_post_scale_0_3_b0;
                                                  /*       0x40 - 0x44       */
  volatile u_int32_t BB_paprd_pre_post_scale_1_3_b0;
                                                  /*       0x44 - 0x48       */
  volatile u_int32_t BB_paprd_pre_post_scale_2_3_b0;
                                                  /*       0x48 - 0x4c       */
  volatile u_int32_t BB_paprd_pre_post_scale_3_3_b0;
                                                  /*       0x4c - 0x50       */
  volatile u_int32_t BB_paprd_pre_post_scale_4_3_b0;
                                                  /*       0x50 - 0x54       */
  volatile u_int32_t BB_paprd_pre_post_scale_5_3_b0;
                                                  /*       0x54 - 0x58       */
  volatile u_int32_t BB_paprd_pre_post_scale_6_3_b0;
                                                  /*       0x58 - 0x5c       */
  volatile u_int32_t BB_paprd_pre_post_scale_7_3_b0;
                                                  /*       0x5c - 0x60       */
  volatile u_int32_t BB_paprd_pre_post_scale_0_4_b0;
                                                  /*       0x60 - 0x64       */
  volatile u_int32_t BB_paprd_pre_post_scale_1_4_b0;
                                                  /*       0x64 - 0x68       */
  volatile u_int32_t BB_paprd_pre_post_scale_2_4_b0;
                                                  /*       0x68 - 0x6c       */
  volatile u_int32_t BB_paprd_pre_post_scale_3_4_b0;
                                                  /*       0x6c - 0x70       */
  volatile u_int32_t BB_paprd_pre_post_scale_4_4_b0;
                                                  /*       0x70 - 0x74       */
  volatile u_int32_t BB_paprd_pre_post_scale_5_4_b0;
                                                  /*       0x74 - 0x78       */
  volatile u_int32_t BB_paprd_pre_post_scale_6_4_b0;
                                                  /*       0x78 - 0x7c       */
  volatile u_int32_t BB_paprd_pre_post_scale_7_4_b0;
                                                  /*       0x7c - 0x80       */
  volatile u_int32_t BB_paprd_power_at_am2am_cal_b0;
                                                  /*       0x80 - 0x84       */
  volatile u_int32_t BB_paprd_valid_obdb_b0;      /*       0x84 - 0x88       */
  volatile char pad__0[0x374];                    /*       0x88 - 0x3fc      */
  volatile u_int32_t BB_chn_ext_dummy_2;          /*      0x3fc - 0x400      */
};

struct sm_ext_reg_map {
  volatile u_int32_t BB_sm_ext_dummy1;            /*        0x0 - 0x4        */
  volatile char pad__0[0x2fc];                    /*        0x4 - 0x300      */
  volatile u_int32_t BB_green_tx_gain_tab_1;      /*      0x300 - 0x304      */
  volatile u_int32_t BB_green_tx_gain_tab_2;      /*      0x304 - 0x308      */
  volatile u_int32_t BB_green_tx_gain_tab_3;      /*      0x308 - 0x30c      */
  volatile u_int32_t BB_green_tx_gain_tab_4;      /*      0x30c - 0x310      */
  volatile u_int32_t BB_green_tx_gain_tab_5;      /*      0x310 - 0x314      */
  volatile u_int32_t BB_green_tx_gain_tab_6;      /*      0x314 - 0x318      */
  volatile u_int32_t BB_green_tx_gain_tab_7;      /*      0x318 - 0x31c      */
  volatile u_int32_t BB_green_tx_gain_tab_8;      /*      0x31c - 0x320      */
  volatile u_int32_t BB_green_tx_gain_tab_9;      /*      0x320 - 0x324      */
  volatile u_int32_t BB_green_tx_gain_tab_10;     /*      0x324 - 0x328      */
  volatile u_int32_t BB_green_tx_gain_tab_11;     /*      0x328 - 0x32c      */
  volatile u_int32_t BB_green_tx_gain_tab_12;     /*      0x32c - 0x330      */
  volatile u_int32_t BB_green_tx_gain_tab_13;     /*      0x330 - 0x334      */
  volatile u_int32_t BB_green_tx_gain_tab_14;     /*      0x334 - 0x338      */
  volatile u_int32_t BB_green_tx_gain_tab_15;     /*      0x338 - 0x33c      */
  volatile u_int32_t BB_green_tx_gain_tab_16;     /*      0x33c - 0x340      */
  volatile u_int32_t BB_green_tx_gain_tab_17;     /*      0x340 - 0x344      */
  volatile u_int32_t BB_green_tx_gain_tab_18;     /*      0x344 - 0x348      */
  volatile u_int32_t BB_green_tx_gain_tab_19;     /*      0x348 - 0x34c      */
  volatile u_int32_t BB_green_tx_gain_tab_20;     /*      0x34c - 0x350      */
  volatile u_int32_t BB_green_tx_gain_tab_21;     /*      0x350 - 0x354      */
  volatile u_int32_t BB_green_tx_gain_tab_22;     /*      0x354 - 0x358      */
  volatile u_int32_t BB_green_tx_gain_tab_23;     /*      0x358 - 0x35c      */
  volatile u_int32_t BB_green_tx_gain_tab_24;     /*      0x35c - 0x360      */
  volatile u_int32_t BB_green_tx_gain_tab_25;     /*      0x360 - 0x364      */
  volatile u_int32_t BB_green_tx_gain_tab_26;     /*      0x364 - 0x368      */
  volatile u_int32_t BB_green_tx_gain_tab_27;     /*      0x368 - 0x36c      */
  volatile u_int32_t BB_green_tx_gain_tab_28;     /*      0x36c - 0x370      */
  volatile u_int32_t BB_green_tx_gain_tab_29;     /*      0x370 - 0x374      */
  volatile u_int32_t BB_green_tx_gain_tab_30;     /*      0x374 - 0x378      */
  volatile u_int32_t BB_green_tx_gain_tab_31;     /*      0x378 - 0x37c      */
  volatile u_int32_t BB_green_tx_gain_tab_32;     /*      0x37c - 0x380      */
  volatile char pad__1[0x27c];                    /*      0x380 - 0x5fc      */
  volatile u_int32_t BB_sm_ext_dummy2;            /*      0x5fc - 0x600      */
};

struct agc1_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS3;       /*        0x0 - 0x4        */
  volatile u_int32_t BB_gain_force_max_gains_b1;  /*        0x4 - 0x8        */
  volatile char pad__0[0x10];                     /*        0x8 - 0x18       */
  volatile u_int32_t BB_ext_atten_switch_ctl_b1;  /*       0x18 - 0x1c       */
  /* Not Poseidon */
  volatile u_int32_t BB_cca_b1;                   /*       0x1c - 0x20       */
  /* Not Poseidon */
  volatile u_int32_t BB_cca_ctrl_2_b1;            /*       0x20 - 0x24       */
  volatile char pad__1[0x15c];                    /*       0x24 - 0x180      */
  volatile u_int32_t BB_rssi_b1;                  /*      0x180 - 0x184      */
  /* Not Poseidon */
  volatile u_int32_t BB_spur_est_cck_report_b1;   /*      0x184 - 0x188      */
  /* Not Poseidon */
  volatile u_int32_t BB_agc_dig_dc_status_i_b1;   /*      0x188 - 0x18c      */
  /* Not Poseidon */
  volatile u_int32_t BB_agc_dig_dc_status_q_b1;   /*      0x18c - 0x190      */
  /* Jupiter */
  volatile u_int32_t BB_dc_cal_status_b1;         /*      0x190 - 0x194      */
  volatile char pad__2[0x6c];                     /*      0x194 - 0x200      */
  volatile u_int32_t BB_rx_ocgain2[128];          /*      0x200 - 0x400      */
};

struct sm1_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS5;       /*        0x0 - 0x4        */
  volatile char pad__0[0x80];                     /*        0x4 - 0x84       */
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
  union {
    volatile u_int32_t BB_pdadc_tab_b1[32];       /*      0x240 - 0x2c0      */
      struct {
          volatile u_int32_t BB_tpc_19_b1;            /*      0x240 - 0x244      */
          volatile u_int32_t pad__7_1[31];            /*      0x244 - 0x2c0      */
          volatile char pad__8[0x190];                /*      0x2c0 - 0x450      */
      } Scorpion;
      struct {
          volatile u_int32_t BB_pdadc_tab_b1[32];         /*      0x240 - 0x2c0      */
          volatile char pad__8[0x190];                    /*      0x2c0 - 0x450      */
      } Osprey;
      struct {
          volatile u_int32_t BB_tpc_19_b1;                /*      0x240 - 0x244      */
          volatile char pad__8[0x3c];                     /*      0x244 - 0x280      */
          volatile u_int32_t BB_pdadc_tab_b1[32];         /*      0x280 - 0x300      */
          volatile char pad__9[0x84];                     /*      0x300 - 0x384      */
          volatile u_int32_t BB_rtt_table_sw_intf_b1;     /*      0x384 - 0x388      */
          volatile u_int32_t BB_rtt_table_sw_intf_1_b1;   /*      0x388 - 0x38c      */
          volatile char pad__10[0xc4];                    /*      0x38c - 0x450      */
      } Jupiter_20;
  } overlay_b440;
  volatile u_int32_t BB_txiq_corr_coeff_01_b1;    /*      0x450 - 0x454      */
  volatile u_int32_t BB_txiq_corr_coeff_23_b1;    /*      0x454 - 0x458      */
  volatile u_int32_t BB_txiq_corr_coeff_45_b1;    /*      0x458 - 0x45c      */
  volatile u_int32_t BB_txiq_corr_coeff_67_b1;    /*      0x45c - 0x460      */
  volatile u_int32_t BB_txiq_corr_coeff_89_b1;    /*      0x460 - 0x464      */
  volatile u_int32_t BB_txiq_corr_coeff_ab_b1;    /*      0x464 - 0x468      */
  volatile u_int32_t BB_txiq_corr_coeff_cd_b1;    /*      0x468 - 0x46c      */
  volatile u_int32_t BB_txiq_corr_coeff_ef_b1;    /*      0x46c - 0x470      */
  volatile char pad__9[0x1c];                     /*      0x470 - 0x48c      */
  volatile u_int32_t BB_txiqcal_status_b1;        /*      0x48c - 0x490      */
  volatile char pad__10[0x20];                    /*      0x490 - 0x4b0      */
  union {
      struct {
          volatile char pad__11[0x150];                   /*      0x4b0 - 0x600     */
      } Osprey;
      struct {
          volatile u_int32_t BB_aic_ctrl_0_b1;            /*      0x4b0 - 0x4b4      */
          volatile u_int32_t BB_aic_ctrl_1_b1;            /*      0x4b4 - 0x4b8      */
          volatile char pad__11[0x8];                     /*      0x4b8 - 0x4c0      */
          volatile u_int32_t BB_aic_stat_0_b1;            /*      0x4c0 - 0x4c4      */
          volatile u_int32_t BB_aic_stat_1_b1;            /*      0x4c4 - 0x4c8      */
          volatile char pad__12[0x128];                   /*      0x4c8 - 0x5f0      */
          volatile u_int32_t BB_tables_intf_addr_b1;      /*      0x5f0 - 0x5f4      */
          volatile u_int32_t BB_tables_intf_data_b1;      /*      0x5f4 - 0x5f8      */
          volatile char pad__13[0x8];                     /*      0x5f8 - 0x600      */
      } Jupiter_10;
      struct {
          volatile u_int32_t BB_aic_ctrl_0_b1;            /*      0x4b0 - 0x4b4      */
          volatile u_int32_t BB_aic_ctrl_1_b1;            /*      0x4b4 - 0x4b8      */
          volatile char pad__11[0x8];                     /*      0x4b8 - 0x4c0      */
          volatile u_int32_t BB_aic_ctrl_4_b1;            /*      0x4c0 - 0x4c4      */
          volatile u_int32_t BB_aic_stat_0_b1;            /*      0x4c4 - 0x4c8      */
          volatile u_int32_t BB_aic_stat_1_b1;            /*      0x4c8 - 0x4cc      */
          volatile u_int32_t BB_aic_stat_2_b1;            /*      0x4cc - 0x4d0      */
          volatile char pad__12[0x120];                   /*      0x4d0 - 0x5f0      */
          volatile u_int32_t BB_tables_intf_addr_b1;      /*      0x5f0 - 0x5f4      */
          volatile u_int32_t BB_tables_intf_data_b1;      /*      0x5f4 - 0x5f8      */
          volatile char pad__13[0x8];                     /*      0x5f8 - 0x600      */
      } Jupiter_20;
  } overlay_0x4b0;
};

struct chn2_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS2;       /*        0x0 - 0x4        */
  volatile char pad__0[0x2c];                     /*        0x4 - 0x30       */
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
};

struct agc2_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS4;       /*        0x0 - 0x4        */
  volatile u_int32_t BB_gain_force_max_gains_b2;  /*        0x4 - 0x8        */
  volatile char pad__0[0x10];                     /*        0x8 - 0x18       */
  volatile u_int32_t BB_ext_atten_switch_ctl_b2;  /*       0x18 - 0x1c       */
  volatile u_int32_t BB_cca_b2;                   /*       0x1c - 0x20       */
  volatile u_int32_t BB_cca_ctrl_2_b2;            /*       0x20 - 0x24       */
  volatile char pad__1[0x15c];                    /*       0x24 - 0x180      */
  volatile u_int32_t BB_rssi_b2;                  /*      0x180 - 0x184      */
  volatile char pad__2[0x4];                      /*      0x184 - 0x188      */
  volatile u_int32_t BB_agc_dig_dc_status_i_b2;   /*      0x188 - 0x18c      */
  volatile u_int32_t BB_agc_dig_dc_status_q_b2;   /*      0x18c - 0x190      */
};

struct sm2_reg_map {
  volatile u_int32_t BB_dummy_DONOTACCESS6;       /*        0x0 - 0x4        */
  volatile char pad__0[0x80];                     /*        0x4 - 0x84       */
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
  union {
    volatile u_int32_t BB_pdadc_tab_b2[32];       /*      0x240 - 0x2c0      */
    struct {
      volatile u_int32_t BB_tpc_19_b2;            /*      0x240 - 0x244      */
      volatile u_int32_t pad__7_1[31];            /*      0x244 - 0x2c0      */
    } Scorpion;
  } overlay_c440;
  volatile char pad__8[0x190];                    /*      0x2c0 - 0x450      */
  volatile u_int32_t BB_txiq_corr_coeff_01_b2;    /*      0x450 - 0x454      */
  volatile u_int32_t BB_txiq_corr_coeff_23_b2;    /*      0x454 - 0x458      */
  volatile u_int32_t BB_txiq_corr_coeff_45_b2;    /*      0x458 - 0x45c      */
  volatile u_int32_t BB_txiq_corr_coeff_67_b2;    /*      0x45c - 0x460      */
  volatile u_int32_t BB_txiq_corr_coeff_89_b2;    /*      0x460 - 0x464      */
  volatile u_int32_t BB_txiq_corr_coeff_ab_b2;    /*      0x464 - 0x468      */
  volatile u_int32_t BB_txiq_corr_coeff_cd_b2;    /*      0x468 - 0x46c      */
  volatile u_int32_t BB_txiq_corr_coeff_ef_b2;    /*      0x46c - 0x470      */
  volatile char pad__9[0x1c];                     /*      0x470 - 0x48c      */
  volatile u_int32_t BB_txiqcal_status_b2;        /*      0x48c - 0x490      */
  volatile char pad__10[0x16c];                   /*      0x490 - 0x5fc      */
  volatile u_int32_t BB_dummy_sm2;                /*      0x5fc - 0x600      */
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
  volatile char pad__0[0x9800];                   /*        0x0 - 0x9800     */
  struct chn_reg_map bb_chn_reg_map;              /*     0x9800 - 0x9bf8     */
  volatile char pad__1[0x8];                      /*     0x9bf8 - 0x9c00     */
  struct mrc_reg_map bb_mrc_reg_map;              /*     0x9c00 - 0x9c24     */
  volatile char pad__2[0xdc];                     /*     0x9c24 - 0x9d00     */
  struct bbb_reg_map bb_bbb_reg_map;              /*     0x9d00 - 0x9d20     */
  volatile char pad__3[0xe0];                     /*     0x9d20 - 0x9e00     */
  struct agc_reg_map bb_agc_reg_map;              /*     0x9e00 - 0xa200     */
  struct sm_reg_map bb_sm_reg_map;                /*     0xa200 - 0xa7f8     */
  volatile char pad__4[0x8];                      /*     0xa7f8 - 0xa800     */
  union {
      struct {
          struct chn1_reg_map bb_chn1_reg_map;            /*     0xa800 - 0xabf8     */
          volatile char pad__5[0x208];                    /*     0xabf8 - 0xae00     */
          struct agc1_reg_map bb_agc1_reg_map;            /*     0xae00 - 0xb200     */
          struct sm1_reg_map bb_sm1_reg_map;              /*     0xb200 - 0xb800     */
          struct chn2_reg_map bb_chn2_reg_map;            /*     0xb800 - 0xbbf0     */
          volatile char pad__6[0x210];                    /*     0xbbf0 - 0xbe00     */
          struct agc2_reg_map bb_agc2_reg_map;            /*     0xbe00 - 0xbf90     */
          volatile char pad__7[0x270];                    /*     0xbf90 - 0xc200     */
          struct sm2_reg_map bb_sm2_reg_map;              /*     0xc200 - 0xc800     */
      } Osprey;
      struct {
          struct chn_ext_reg_map bb_chn_ext_reg_map;      /*     0xa800 - 0xac00     */
          volatile char pad__5[0x600];                    /*     0xac00 - 0xb200     */
          struct sm_ext_reg_map bb_sm_ext_reg_map;        /*     0xb200 - 0xb800     */
          volatile char pad__6[0x600];                    /*     0xb800 - 0xbe00     */
          struct agc1_reg_map bb_agc1_reg_map;            /*     0xbe00 - 0xc1fc     */
          volatile char pad__7[0x4];                      /*     0xc1fc - 0xc200     */
          struct sm1_reg_map bb_sm1_reg_map;              /*     0xc200 - 0xc800     */
      } Poseidon;
  } overlay_0xa800;
  struct chn3_reg_map bb_chn3_reg_map;            /*     0xc800 - 0xcc00     */
  volatile char pad__8[0x200];                    /*     0xcc00 - 0xce00     */
  struct agc3_reg_map bb_agc3_reg_map;            /*     0xce00 - 0xcf84     */
  volatile char pad__9[0x27c];                    /*     0xcf84 - 0xd200     */
  struct sm3_reg_map bb_sm3_reg_map;              /*     0xd200 - 0xd800     */
};

struct svd_reg {
  volatile char pad__0[0x10000];                  /*        0x0 - 0x10000    */
  volatile u_int32_t TXBF_DBG;                    /*    0x10000 - 0x10004    */
  volatile u_int32_t TXBF;                        /*    0x10004 - 0x10008    */
  volatile u_int32_t TXBF_TIMER;                  /*    0x10008 - 0x1000c    */
  volatile u_int32_t TXBF_SW;                     /*    0x1000c - 0x10010    */
  volatile u_int32_t TXBF_SM;                     /*    0x10010 - 0x10014    */
  volatile u_int32_t TXBF1_CNTL;                  /*    0x10014 - 0x10018    */
  volatile u_int32_t TXBF2_CNTL;                  /*    0x10018 - 0x1001c    */
  volatile u_int32_t TXBF3_CNTL;                  /*    0x1001c - 0x10020    */
  volatile u_int32_t TXBF4_CNTL;                  /*    0x10020 - 0x10024    */
  volatile u_int32_t TXBF5_CNTL;                  /*    0x10024 - 0x10028    */
  volatile u_int32_t TXBF6_CNTL;                  /*    0x10028 - 0x1002c    */
  volatile u_int32_t TXBF7_CNTL;                  /*    0x1002c - 0x10030    */
  volatile u_int32_t TXBF8_CNTL;                  /*    0x10030 - 0x10034    */
  volatile char pad__1[0xfcc];                    /*    0x10034 - 0x11000    */
  volatile u_int32_t RC0[118];                    /*    0x11000 - 0x111d8    */
  volatile char pad__2[0x28];                     /*    0x111d8 - 0x11200    */
  volatile u_int32_t RC1[118];                    /*    0x11200 - 0x113d8    */
  volatile char pad__3[0x28];                     /*    0x113d8 - 0x11400    */
  volatile u_int32_t SVD_MEM0[114];               /*    0x11400 - 0x115c8    */
  volatile char pad__4[0x38];                     /*    0x115c8 - 0x11600    */
  volatile u_int32_t SVD_MEM1[114];               /*    0x11600 - 0x117c8    */
  volatile char pad__5[0x38];                     /*    0x117c8 - 0x11800    */
  volatile u_int32_t SVD_MEM2[114];               /*    0x11800 - 0x119c8    */
  volatile char pad__6[0x38];                     /*    0x119c8 - 0x11a00    */
  volatile u_int32_t SVD_MEM3[114];               /*    0x11a00 - 0x11bc8    */
  volatile char pad__7[0x38];                     /*    0x11bc8 - 0x11c00    */
  volatile u_int32_t SVD_MEM4[114];               /*    0x11c00 - 0x11dc8    */
  volatile char pad__8[0x638];                    /*    0x11dc8 - 0x12400    */
  volatile u_int32_t CVCACHE[512];                /*    0x12400 - 0x12c00    */
};

struct efuse_reg_WLAN {
  volatile char pad__0[0x14000];                  /*        0x0 - 0x14000    */
  volatile u_int32_t OTP_MEM[256];                /*    0x14000 - 0x14400    */
  volatile char pad__1[0x1b00];                   /*    0x14400 - 0x15f00    */
  volatile u_int32_t OTP_INTF0;                   /*    0x15f00 - 0x15f04    */
  volatile u_int32_t OTP_INTF1;                   /*    0x15f04 - 0x15f08    */
  volatile u_int32_t OTP_INTF2;                   /*    0x15f08 - 0x15f0c    */
  volatile u_int32_t OTP_INTF3;                   /*    0x15f0c - 0x15f10    */
  volatile u_int32_t OTP_INTF4;                   /*    0x15f10 - 0x15f14    */
  volatile u_int32_t OTP_INTF5;                   /*    0x15f14 - 0x15f18    */
  volatile u_int32_t OTP_STATUS0;                 /*    0x15f18 - 0x15f1c    */
  volatile u_int32_t OTP_STATUS1;                 /*    0x15f1c - 0x15f20    */
  volatile u_int32_t OTP_INTF6;                   /*    0x15f20 - 0x15f24    */
  volatile u_int32_t OTP_LDO_CONTROL;             /*    0x15f24 - 0x15f28    */
  volatile u_int32_t OTP_LDO_POWER_GOOD;          /*    0x15f28 - 0x15f2c    */
  volatile u_int32_t OTP_LDO_STATUS;              /*    0x15f2c - 0x15f30    */
  volatile u_int32_t OTP_VDDQ_HOLD_TIME;          /*    0x15f30 - 0x15f34    */
  volatile u_int32_t OTP_PGENB_SETUP_HOLD_TIME;   /*    0x15f34 - 0x15f38    */
  volatile u_int32_t OTP_STROBE_PULSE_INTERVAL;   /*    0x15f38 - 0x15f3c    */
  volatile u_int32_t OTP_CSB_ADDR_LOAD_SETUP_HOLD;
                                                  /*    0x15f3c - 0x15f40    */
};

struct radio65_reg {
  volatile char pad__0[0x16000];                  /*        0x0 - 0x16000    */
  volatile u_int32_t ch0_RXRF_BIAS1;              /*    0x16000 - 0x16004    */
  volatile u_int32_t ch0_RXRF_BIAS2;              /*    0x16004 - 0x16008    */
  volatile u_int32_t ch0_RXRF_GAINSTAGES;         /*    0x16008 - 0x1600c    */
  volatile u_int32_t ch0_RXRF_AGC;                /*    0x1600c - 0x16010    */
  /* Jupiter */
  volatile u_int32_t ch0_RXRF_BIAS3;              /*    0x16010 - 0x16014    */
  volatile char pad__1[0x2c];                     /*    0x16014 - 0x16040    */
  volatile u_int32_t ch0_TXRF1;                   /*    0x16040 - 0x16044    */
  volatile u_int32_t ch0_TXRF2;                   /*    0x16044 - 0x16048    */
  volatile u_int32_t ch0_TXRF3;                   /*    0x16048 - 0x1604c    */
  volatile u_int32_t ch0_TXRF4;                   /*    0x1604c - 0x16050    */
  volatile u_int32_t ch0_TXRF5;                   /*    0x16050 - 0x16054    */
  volatile u_int32_t ch0_TXRF6;                   /*    0x16054 - 0x16058    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF7;                   /*    0x16058 - 0x1605c    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF8;                   /*    0x1605c - 0x16060    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF9;                   /*    0x16060 - 0x16064    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF10;                  /*    0x16064 - 0x16068    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF11;                  /*    0x16068 - 0x1606c    */
  /* Not Poseidon */
  volatile u_int32_t ch0_TXRF12;                  /*    0x1606c - 0x16070    */
  volatile char pad__2[0x10];                     /*    0x16070 - 0x16080    */
  volatile u_int32_t ch0_SYNTH1;                  /*    0x16080 - 0x16084    */
  volatile u_int32_t ch0_SYNTH2;                  /*    0x16084 - 0x16088    */
  volatile u_int32_t ch0_SYNTH3;                  /*    0x16088 - 0x1608c    */
  volatile u_int32_t ch0_SYNTH4;                  /*    0x1608c - 0x16090    */
  volatile u_int32_t ch0_SYNTH5;                  /*    0x16090 - 0x16094    */
  volatile u_int32_t ch0_SYNTH6;                  /*    0x16094 - 0x16098    */
  volatile u_int32_t ch0_SYNTH7;                  /*    0x16098 - 0x1609c    */
  volatile u_int32_t ch0_SYNTH8;                  /*    0x1609c - 0x160a0    */
  volatile u_int32_t ch0_SYNTH9;                  /*    0x160a0 - 0x160a4    */
  volatile u_int32_t ch0_SYNTH10;                 /*    0x160a4 - 0x160a8    */
  volatile u_int32_t ch0_SYNTH11;                 /*    0x160a8 - 0x160ac    */
  volatile u_int32_t ch0_SYNTH12;                 /*    0x160ac - 0x160b0    */
  volatile u_int32_t ch0_SYNTH13;                 /*    0x160b0 - 0x160b4    */
  volatile u_int32_t ch0_SYNTH14;                 /*    0x160b4 - 0x160b8    */
  /* Jupiter */
  volatile u_int32_t ch0_SYNTH15;                 /*    0x160b8 - 0x160bc    */
  /* Jupiter */
  volatile u_int32_t ch0_SYNTH16;                 /*    0x160bc - 0x160c0    */
  volatile u_int32_t ch0_BIAS1;                   /*    0x160c0 - 0x160c4    */
  volatile u_int32_t ch0_BIAS2;                   /*    0x160c4 - 0x160c8    */
  volatile u_int32_t ch0_BIAS3;                   /*    0x160c8 - 0x160cc    */
  volatile u_int32_t ch0_BIAS4;                   /*    0x160cc - 0x160d0    */
  /* Poseidon */
  volatile u_int32_t ch0_BIAS5;                   /*    0x160d0 - 0x160d4    */
  volatile char pad__3[0x2c];                     /*    0x160d4 - 0x16100    */
  volatile u_int32_t ch0_RXTX1;                   /*    0x16100 - 0x16104    */
  volatile u_int32_t ch0_RXTX2;                   /*    0x16104 - 0x16108    */
  volatile u_int32_t ch0_RXTX3;                   /*    0x16108 - 0x1610c    */
  volatile u_int32_t ch0_RXTX4;                   /*    0x1610c - 0x16110    */
  /* Jupiter */
  volatile u_int32_t ch0_RXTX5;                   /*    0x16110 - 0x16114    */
  volatile char pad__4[0x2c];                     /*    0x16114 - 0x16140    */
  volatile u_int32_t ch0_BB1;                     /*    0x16140 - 0x16144    */
  volatile u_int32_t ch0_BB2;                     /*    0x16144 - 0x16148    */
  volatile u_int32_t ch0_BB3;                     /*    0x16148 - 0x1614c    */
  volatile char pad__6[0x34];                     /*    0x1614c - 0x16180    */
  union {
      struct {
          volatile u_int32_t ch0_pll_cntl;                /*    0x16180 - 0x16184    */
          volatile u_int32_t ch0_pll_mode;                /*    0x16184 - 0x16188    */
          volatile u_int32_t ch0_bb_dpll3;                /*    0x16188 - 0x1618c    */
          volatile u_int32_t ch0_bb_dpll4;                /*    0x1618c - 0x16190    */
          volatile char pad__6_1[0xf0];                   /*    0x16190 - 0x16280    */
          volatile u_int32_t ch0_PLLCLKMODA;              /*    0x16280 - 0x16284    */
          volatile u_int32_t ch0_PLLCLKMODA2;             /*    0x16284 - 0x16288    */
          volatile u_int32_t ch0_TOP;                     /*    0x16288 - 0x1628c    */
          volatile u_int32_t ch0_TOP2;                    /*    0x1628c - 0x16290    */
          volatile u_int32_t ch0_THERM;                   /*    0x16290 - 0x16294    */
          volatile u_int32_t ch0_XTAL;                    /*    0x16294 - 0x16298    */
          volatile char pad__7[0xe8];                     /*    0x16298 - 0x16380    */
      } Osprey;
      struct {
          volatile u_int32_t ch0_BB_DPLL1;                /*    0x16180 - 0x16184    */
          volatile u_int32_t ch0_BB_DPLL2;                /*    0x16184 - 0x16188    */
          volatile u_int32_t ch0_BB_DPLL3;                /*    0x16188 - 0x1618c    */
          volatile u_int32_t ch0_BB_DPLL4;                /*    0x1618c - 0x16190    */
          volatile char pad__7[0xb0];                     /*    0x16190 - 0x16240    */
          volatile u_int32_t ch0_DDR_DPLL1;               /*    0x16240 - 0x16244    */
          volatile u_int32_t ch0_DDR_DPLL2;               /*    0x16244 - 0x16248    */
          volatile u_int32_t ch0_DDR_DPLL3;               /*    0x16248 - 0x1624c    */
          volatile u_int32_t ch0_DDR_DPLL4;               /*    0x1624c - 0x16250    */
          volatile char pad__8[0x30];                     /*    0x16250 - 0x16280    */
          volatile u_int32_t ch0_TOP;                     /*    0x16280 - 0x16284    */
          volatile u_int32_t ch0_TOP2;                    /*    0x16284 - 0x16288    */
          volatile u_int32_t ch0_TOP3;                    /*    0x16288 - 0x1628c    */
          volatile u_int32_t ch0_THERM;                   /*    0x1628c - 0x16290    */
          volatile u_int32_t ch0_XTAL;                    /*    0x16290 - 0x16294    */
          volatile char pad__9[0xec];                     /*    0x16294 - 0x16380    */
      } Poseidon;
      struct {
          volatile char pad__6_1[0x100];                  /*    0x16180 - 0x16280    */
          volatile u_int32_t ch0_PLLCLKMODA1;             /*    0x16280 - 0x16284    */
          volatile u_int32_t ch0_PLLCLKMODA2;             /*    0x16284 - 0x16288    */
          volatile u_int32_t ch0_PLLCLKMODA3;             /*    0x16288 - 0x1628c    */
          volatile u_int32_t ch0_TOP1;                    /*    0x1628c - 0x16290    */
          volatile u_int32_t ch0_TOP2;                    /*    0x16290 - 0x16294    */
          volatile u_int32_t ch0_THERM;                   /*    0x16294 - 0x16298    */
          volatile u_int32_t ch0_XTAL;                    /*    0x16298 - 0x1629c    */
          volatile char pad__7[0xa4];                     /*    0x1629c - 0x16340    */
          volatile u_int32_t ch0_PMU1;                    /*    0x16340 - 0x16344    */
          volatile u_int32_t ch0_PMU2;                    /*    0x16344 - 0x16348    */
          volatile u_int32_t ch0_PMU3;                    /*    0x16348 - 0x1634c    */
          volatile char pad__8[0x34];                     /*    0x1634c - 0x16380    */
      } Jupiter;
      struct {
          volatile u_int32_t ch0_DPLL;                    /*    0x16180 - 0x16184    */
          volatile u_int32_t ch0_DPLL2;                   /*    0x16184 - 0x16188    */
          volatile u_int32_t ch0_DPLL3;                   /*    0x16188 - 0x1618c    */
          volatile u_int32_t ch0_DPLL4;                   /*    0x1618c - 0x16190    */
          volatile u_int32_t ch0_DPLL5;                   /*    0x16190 - 0x16194    */
          volatile char pad__6[0xec];                     /*    0x16194 - 0x16280    */
          volatile u_int32_t ch0_PLLCLKMODA1;             /*    0x16280 - 0x16284    */
          volatile u_int32_t ch0_PLLCLKMODA2;             /*    0x16284 - 0x16288    */
          volatile u_int32_t ch0_PLLCLKMODA3;             /*    0x16288 - 0x1628c    */
          volatile u_int32_t ch0_TOP1;                    /*    0x1628c - 0x16290    */
          volatile u_int32_t ch0_TOP2;                    /*    0x16290 - 0x16294    */
          volatile u_int32_t ch0_THERM;                   /*    0x16294 - 0x16298    */
          volatile u_int32_t ch0_XTAL;                    /*    0x16298 - 0x1629c    */
          volatile char pad__7[0xa4];                     /*    0x1629c - 0x16340    */
          volatile u_int32_t ch0_PMU1;                    /*    0x16340 - 0x16344    */
          volatile u_int32_t ch0_PMU2;                    /*    0x16344 - 0x16348    */
          volatile char pad__8[0x38];                     /*    0x16348 - 0x16380    */
      } Aphrodite;
  } overlay_0x16180;
  volatile u_int32_t ch0_rbist_cntrl;             /*    0x16380 - 0x16384    */
  volatile u_int32_t ch0_tx_dc_offset;            /*    0x16384 - 0x16388    */
  volatile u_int32_t ch0_tx_tonegen0;             /*    0x16388 - 0x1638c    */
  volatile u_int32_t ch0_tx_tonegen1;             /*    0x1638c - 0x16390    */
  volatile u_int32_t ch0_tx_lftonegen0;           /*    0x16390 - 0x16394    */
  volatile u_int32_t ch0_tx_linear_ramp_i;        /*    0x16394 - 0x16398    */
  volatile u_int32_t ch0_tx_linear_ramp_q;        /*    0x16398 - 0x1639c    */
  volatile u_int32_t ch0_tx_prbs_mag;             /*    0x1639c - 0x163a0    */
  volatile u_int32_t ch0_tx_prbs_seed_i;          /*    0x163a0 - 0x163a4    */
  volatile u_int32_t ch0_tx_prbs_seed_q;          /*    0x163a4 - 0x163a8    */
  volatile u_int32_t ch0_cmac_dc_cancel;          /*    0x163a8 - 0x163ac    */
  volatile u_int32_t ch0_cmac_dc_offset;          /*    0x163ac - 0x163b0    */
  volatile u_int32_t ch0_cmac_corr;               /*    0x163b0 - 0x163b4    */
  volatile u_int32_t ch0_cmac_power;              /*    0x163b4 - 0x163b8    */
  volatile u_int32_t ch0_cmac_cross_corr;         /*    0x163b8 - 0x163bc    */
  volatile u_int32_t ch0_cmac_i2q2;               /*    0x163bc - 0x163c0    */
  volatile u_int32_t ch0_cmac_power_hpf;          /*    0x163c0 - 0x163c4    */
  volatile u_int32_t ch0_rxdac_set1;              /*    0x163c4 - 0x163c8    */
  volatile u_int32_t ch0_rxdac_set2;              /*    0x163c8 - 0x163cc    */
  volatile u_int32_t ch0_rxdac_long_shift;        /*    0x163cc - 0x163d0    */
  volatile u_int32_t ch0_cmac_results_i;          /*    0x163d0 - 0x163d4    */
  volatile u_int32_t ch0_cmac_results_q;          /*    0x163d4 - 0x163d8    */
  volatile char pad__8[0x28];                     /*    0x163d8 - 0x16400    */
  volatile u_int32_t ch1_RXRF_BIAS1;              /*    0x16400 - 0x16404    */
  volatile u_int32_t ch1_RXRF_BIAS2;              /*    0x16404 - 0x16408    */
  volatile u_int32_t ch1_RXRF_GAINSTAGES;         /*    0x16408 - 0x1640c    */
  volatile u_int32_t ch1_RXRF_AGC;                /*    0x1640c - 0x16410    */
  /* Jupiter */
  volatile u_int32_t ch1_RXRF_BIAS3;              /*    0x16410 - 0x16414    */
  volatile char pad__9[0x2c];                     /*    0x16414 - 0x16440    */
  volatile u_int32_t ch1_TXRF1;                   /*    0x16440 - 0x16444    */
  volatile u_int32_t ch1_TXRF2;                   /*    0x16444 - 0x16448    */
  volatile u_int32_t ch1_TXRF3;                   /*    0x16448 - 0x1644c    */
  volatile u_int32_t ch1_TXRF4;                   /*    0x1644c - 0x16450    */
  volatile u_int32_t ch1_TXRF5;                   /*    0x16450 - 0x16454    */
  volatile u_int32_t ch1_TXRF6;                   /*    0x16454 - 0x16458    */
  volatile u_int32_t ch1_TXRF7;                   /*    0x16458 - 0x1645c    */
  volatile u_int32_t ch1_TXRF8;                   /*    0x1645c - 0x16460    */
  volatile u_int32_t ch1_TXRF9;                   /*    0x16460 - 0x16464    */
  volatile u_int32_t ch1_TXRF10;                  /*    0x16464 - 0x16468    */
  volatile u_int32_t ch1_TXRF11;                  /*    0x16468 - 0x1646c    */
  volatile u_int32_t ch1_TXRF12;                  /*    0x1646c - 0x16470    */
  volatile char pad__10[0x90];                    /*    0x16470 - 0x16500    */
  volatile u_int32_t ch1_RXTX1;                   /*    0x16500 - 0x16504    */
  volatile u_int32_t ch1_RXTX2;                   /*    0x16504 - 0x16508    */
  volatile u_int32_t ch1_RXTX3;                   /*    0x16508 - 0x1650c    */
  volatile u_int32_t ch1_RXTX4;                   /*    0x1650c - 0x16510    */
  /* Jupiter */
  volatile u_int32_t ch1_RXTX5;                   /*    0x16510 - 0x16514    */
  volatile char pad__11[0x2c];                    /*    0x16514 - 0x16540    */
  volatile u_int32_t ch1_BB1;                     /*    0x16540 - 0x16544    */
  volatile u_int32_t ch1_BB2;                     /*    0x16544 - 0x16548    */
  volatile u_int32_t ch1_BB3;                     /*    0x16548 - 0x1654c    */
  volatile char pad__12[0x234];                   /*    0x1654c - 0x16780    */
  volatile u_int32_t ch1_rbist_cntrl;             /*    0x16780 - 0x16784    */
  volatile u_int32_t ch1_tx_dc_offset;            /*    0x16784 - 0x16788    */
  volatile u_int32_t ch1_tx_tonegen0;             /*    0x16788 - 0x1678c    */
  volatile u_int32_t ch1_tx_tonegen1;             /*    0x1678c - 0x16790    */
  volatile u_int32_t ch1_tx_lftonegen0;           /*    0x16790 - 0x16794    */
  volatile u_int32_t ch1_tx_linear_ramp_i;        /*    0x16794 - 0x16798    */
  volatile u_int32_t ch1_tx_linear_ramp_q;        /*    0x16798 - 0x1679c    */
  volatile u_int32_t ch1_tx_prbs_mag;             /*    0x1679c - 0x167a0    */
  volatile u_int32_t ch1_tx_prbs_seed_i;          /*    0x167a0 - 0x167a4    */
  volatile u_int32_t ch1_tx_prbs_seed_q;          /*    0x167a4 - 0x167a8    */
  volatile u_int32_t ch1_cmac_dc_cancel;          /*    0x167a8 - 0x167ac    */
  volatile u_int32_t ch1_cmac_dc_offset;          /*    0x167ac - 0x167b0    */
  volatile u_int32_t ch1_cmac_corr;               /*    0x167b0 - 0x167b4    */
  volatile u_int32_t ch1_cmac_power;              /*    0x167b4 - 0x167b8    */
  volatile u_int32_t ch1_cmac_cross_corr;         /*    0x167b8 - 0x167bc    */
  volatile u_int32_t ch1_cmac_i2q2;               /*    0x167bc - 0x167c0    */
  volatile u_int32_t ch1_cmac_power_hpf;          /*    0x167c0 - 0x167c4    */
  volatile u_int32_t ch1_rxdac_set1;              /*    0x167c4 - 0x167c8    */
  volatile u_int32_t ch1_rxdac_set2;              /*    0x167c8 - 0x167cc    */
  volatile u_int32_t ch1_rxdac_long_shift;        /*    0x167cc - 0x167d0    */
  volatile u_int32_t ch1_cmac_results_i;          /*    0x167d0 - 0x167d4    */
  volatile u_int32_t ch1_cmac_results_q;          /*    0x167d4 - 0x167d8    */
  volatile char pad__13[0x28];                    /*    0x167d8 - 0x16800    */
  volatile u_int32_t ch2_RXRF_BIAS1;              /*    0x16800 - 0x16804    */
  volatile u_int32_t ch2_RXRF_BIAS2;              /*    0x16804 - 0x16808    */
  volatile u_int32_t ch2_RXRF_GAINSTAGES;         /*    0x16808 - 0x1680c    */
  volatile u_int32_t ch2_RXRF_AGC;                /*    0x1680c - 0x16810    */
  volatile char pad__14[0x30];                    /*    0x16810 - 0x16840    */
  volatile u_int32_t ch2_TXRF1;                   /*    0x16840 - 0x16844    */
  volatile u_int32_t ch2_TXRF2;                   /*    0x16844 - 0x16848    */
  volatile u_int32_t ch2_TXRF3;                   /*    0x16848 - 0x1684c    */
  volatile u_int32_t ch2_TXRF4;                   /*    0x1684c - 0x16850    */
  volatile u_int32_t ch2_TXRF5;                   /*    0x16850 - 0x16854    */
  volatile u_int32_t ch2_TXRF6;                   /*    0x16854 - 0x16858    */
  volatile u_int32_t ch2_TXRF7;                   /*    0x16858 - 0x1685c    */
  volatile u_int32_t ch2_TXRF8;                   /*    0x1685c - 0x16860    */
  volatile u_int32_t ch2_TXRF9;                   /*    0x16860 - 0x16864    */
  volatile u_int32_t ch2_TXRF10;                  /*    0x16864 - 0x16868    */
  volatile u_int32_t ch2_TXRF11;                  /*    0x16868 - 0x1686c    */
  volatile u_int32_t ch2_TXRF12;                  /*    0x1686c - 0x16870    */
  volatile char pad__15[0x90];                    /*    0x16870 - 0x16900    */
  volatile u_int32_t ch2_RXTX1;                   /*    0x16900 - 0x16904    */
  volatile u_int32_t ch2_RXTX2;                   /*    0x16904 - 0x16908    */
  volatile u_int32_t ch2_RXTX3;                   /*    0x16908 - 0x1690c    */
  volatile u_int32_t ch2_RXTX4;                   /*    0x1690c - 0x16910    */
  volatile char pad__16[0x30];                    /*    0x16910 - 0x16940    */
  volatile u_int32_t ch2_BB1;                     /*    0x16940 - 0x16944    */
  volatile u_int32_t ch2_BB2;                     /*    0x16944 - 0x16948    */
  volatile u_int32_t ch2_BB3;                     /*    0x16948 - 0x1694c    */
  volatile char pad__17[0x234];                   /*    0x1694c - 0x16b80    */
  volatile u_int32_t ch2_rbist_cntrl;             /*    0x16b80 - 0x16b84    */
  volatile u_int32_t ch2_tx_dc_offset;            /*    0x16b84 - 0x16b88    */
  volatile u_int32_t ch2_tx_tonegen0;             /*    0x16b88 - 0x16b8c    */
  volatile u_int32_t ch2_tx_tonegen1;             /*    0x16b8c - 0x16b90    */
  volatile u_int32_t ch2_tx_lftonegen0;           /*    0x16b90 - 0x16b94    */
  volatile u_int32_t ch2_tx_linear_ramp_i;        /*    0x16b94 - 0x16b98    */
  volatile u_int32_t ch2_tx_linear_ramp_q;        /*    0x16b98 - 0x16b9c    */
  volatile u_int32_t ch2_tx_prbs_mag;             /*    0x16b9c - 0x16ba0    */
  volatile u_int32_t ch2_tx_prbs_seed_i;          /*    0x16ba0 - 0x16ba4    */
  volatile u_int32_t ch2_tx_prbs_seed_q;          /*    0x16ba4 - 0x16ba8    */
  volatile u_int32_t ch2_cmac_dc_cancel;          /*    0x16ba8 - 0x16bac    */
  volatile u_int32_t ch2_cmac_dc_offset;          /*    0x16bac - 0x16bb0    */
  volatile u_int32_t ch2_cmac_corr;               /*    0x16bb0 - 0x16bb4    */
  volatile u_int32_t ch2_cmac_power;              /*    0x16bb4 - 0x16bb8    */
  volatile u_int32_t ch2_cmac_cross_corr;         /*    0x16bb8 - 0x16bbc    */
  volatile u_int32_t ch2_cmac_i2q2;               /*    0x16bbc - 0x16bc0    */
  volatile u_int32_t ch2_cmac_power_hpf;          /*    0x16bc0 - 0x16bc4    */
  volatile u_int32_t ch2_rxdac_set1;              /*    0x16bc4 - 0x16bc8    */
  volatile u_int32_t ch2_rxdac_set2;              /*    0x16bc8 - 0x16bcc    */
  volatile u_int32_t ch2_rxdac_long_shift;        /*    0x16bcc - 0x16bd0    */
  volatile u_int32_t ch2_cmac_results_i;          /*    0x16bd0 - 0x16bd4    */
  volatile u_int32_t ch2_cmac_results_q;          /*    0x16bd4 - 0x16bd8    */
  volatile char pad__18[0x4c4a8];                 /*    0x16bd8 - 0x63080    */
  /* Jupiter-start */
  volatile u_int32_t chbt_SYNTH1;                 /*    0x63080 - 0x63084    */
  volatile u_int32_t chbt_SYNTH2;                 /*    0x63084 - 0x63088    */
  volatile u_int32_t chbt_SYNTH3;                 /*    0x63088 - 0x6308c    */
  volatile u_int32_t chbt_SYNTH4;                 /*    0x6308c - 0x63090    */
  volatile u_int32_t chbt_SYNTH5;                 /*    0x63090 - 0x63094    */
  volatile u_int32_t chbt_SYNTH6;                 /*    0x63094 - 0x63098    */
  volatile u_int32_t chbt_SYNTH7;                 /*    0x63098 - 0x6309c    */
  volatile u_int32_t chbt_SYNTH8;                 /*    0x6309c - 0x630a0    */
  volatile char pad__19[0x20];                    /*    0x630a0 - 0x630c0    */
  volatile u_int32_t chbt_BIAS1;                  /*    0x630c0 - 0x630c4    */
  volatile u_int32_t chbt_BIAS2;                  /*    0x630c4 - 0x630c8    */
  volatile u_int32_t chbt_BIAS3;                  /*    0x630c8 - 0x630cc    */
  volatile u_int32_t chbt_BIAS4;                  /*    0x630cc - 0x630d0    */
  volatile u_int32_t chbt_BIAS5;                  /*    0x630d0 - 0x630d4    */
  volatile char pad__20[0x2c];                    /*    0x630d4 - 0x63100    */
  volatile u_int32_t chbt_TOP1;                   /*    0x63100 - 0x63104    */
  volatile u_int32_t chbt_TOP2;                   /*    0x63104 - 0x63108    */
  volatile u_int32_t chbt_TOP3;                   /*    0x63108 - 0x6310c    */
  volatile u_int32_t chbt_TOP4;                   /*    0x6310c - 0x63110    */
  volatile u_int32_t chbt_TOP5;                   /*    0x63110 - 0x63114    */
  volatile u_int32_t chbt_TOP6;                   /*    0x63114 - 0x63118    */
  volatile u_int32_t chbt_TOP7;                   /*    0x63118 - 0x6311c    */
  volatile u_int32_t chbt_TOP8;                   /*    0x6311c - 0x63120    */
  volatile u_int32_t chbt_TOP9;                   /*    0x63120 - 0x63124    */
  volatile u_int32_t chbt_TOP10;                  /*    0x63124 - 0x63128    */
  volatile char pad__21[0x158];                   /*    0x63128 - 0x63280    */
  volatile u_int32_t chbt_CLK1;                   /*    0x63280 - 0x63284    */
  volatile u_int32_t chbt_CLK2;                   /*    0x63284 - 0x63288    */
  volatile u_int32_t chbt_CLK3;                   /*    0x63288 - 0x6328c    */
  volatile char pad__22[0xb4];                    /*    0x6328c - 0x63340    */
  volatile u_int32_t chbt_PMU1;                   /*    0x63340 - 0x63344    */
  volatile u_int32_t chbt_PMU2;                   /*    0x63344 - 0x63348    */
  /* Jupiter-end */
  /* Aphrodite-start */
  volatile char pad__23[0x38];                    /*    0x63348 - 0x63380    */
  volatile u_int32_t chbt_rbist_cntrl;            /*    0x63380 - 0x63384    */
  volatile u_int32_t chbt_tx_dc_offset;           /*    0x63384 - 0x63388    */
  volatile u_int32_t chbt_tx_tonegen0;            /*    0x63388 - 0x6338c    */
  volatile u_int32_t chbt_tx_tonegen1;            /*    0x6338c - 0x63390    */
  volatile u_int32_t chbt_tx_lftonegen0;          /*    0x63390 - 0x63394    */
  volatile u_int32_t chbt_tx_linear_ramp_i;       /*    0x63394 - 0x63398    */
  volatile u_int32_t chbt_tx_linear_ramp_q;       /*    0x63398 - 0x6339c    */
  volatile u_int32_t chbt_tx_prbs_mag;            /*    0x6339c - 0x633a0    */
  volatile u_int32_t chbt_tx_prbs_seed_i;         /*    0x633a0 - 0x633a4    */
  volatile u_int32_t chbt_tx_prbs_seed_q;         /*    0x633a4 - 0x633a8    */
  volatile u_int32_t chbt_cmac_dc_cancel;         /*    0x633a8 - 0x633ac    */
  volatile u_int32_t chbt_cmac_dc_offset;         /*    0x633ac - 0x633b0    */
  volatile u_int32_t chbt_cmac_corr;              /*    0x633b0 - 0x633b4    */
  volatile u_int32_t chbt_cmac_power;             /*    0x633b4 - 0x633b8    */
  volatile u_int32_t chbt_cmac_cross_corr;        /*    0x633b8 - 0x633bc    */
  volatile u_int32_t chbt_cmac_i2q2;              /*    0x633bc - 0x633c0    */
  volatile u_int32_t chbt_cmac_power_hpf;         /*    0x633c0 - 0x633c4    */
  volatile u_int32_t chbt_rxdac_set1;             /*    0x633c4 - 0x633c8    */
  volatile u_int32_t chbt_rxdac_set2;             /*    0x633c8 - 0x633cc    */
  volatile u_int32_t chbt_rxdac_long_shift;       /*    0x633cc - 0x633d0    */
  volatile u_int32_t chbt_cmac_results_i;         /*    0x633d0 - 0x633d4    */
  volatile u_int32_t chbt_cmac_results_q;         /*    0x633d4 - 0x633d8    */
  /* Aphrodite-end */
};

struct pcie_phy_reg_csr {
  volatile char pad__0[0x18c00];                  /*        0x0 - 0x18c00    */
  volatile u_int32_t pcie_phy_reg_1;              /*    0x18c00 - 0x18c04    */
  volatile u_int32_t pcie_phy_reg_2;              /*    0x18c04 - 0x18c08    */
  volatile u_int32_t pcie_phy_reg_3;              /*    0x18c08 - 0x18c0c    */
};

struct pmu_reg {
  volatile char pad__0[0x16c40];                  /*        0x0 - 0x16c40    */
  volatile u_int32_t ch0_PMU1;                    /*    0x16c40 - 0x16c44    */
  volatile u_int32_t ch0_PMU2;                    /*    0x16c44 - 0x16c48    */
};

struct wlan_coex_reg {
  volatile char pad__0[0x1800];                   /*        0x0 - 0x1800     */
  volatile u_int32_t MCI_COMMAND0;                /*     0x1800 - 0x1804     */
  volatile u_int32_t MCI_COMMAND1;                /*     0x1804 - 0x1808     */
  volatile u_int32_t MCI_COMMAND2;                /*     0x1808 - 0x180c     */
  volatile u_int32_t MCI_RX_CTRL;                 /*     0x180c - 0x1810     */
  volatile u_int32_t MCI_TX_CTRL;                 /*     0x1810 - 0x1814     */
  volatile u_int32_t MCI_MSG_ATTRIBUTES_TABLE;    /*     0x1814 - 0x1818     */
  volatile u_int32_t MCI_SCHD_TABLE_0;            /*     0x1818 - 0x181c     */
  volatile u_int32_t MCI_SCHD_TABLE_1;            /*     0x181c - 0x1820     */
  volatile u_int32_t MCI_GPM_0;                   /*     0x1820 - 0x1824     */
  volatile u_int32_t MCI_GPM_1;                   /*     0x1824 - 0x1828     */
  volatile u_int32_t MCI_INTERRUPT_RAW;           /*     0x1828 - 0x182c     */
  volatile u_int32_t MCI_INTERRUPT_EN;            /*     0x182c - 0x1830     */
  volatile u_int32_t MCI_REMOTE_CPU_INT;          /*     0x1830 - 0x1834     */
  volatile u_int32_t MCI_REMOTE_CPU_INT_EN;       /*     0x1834 - 0x1838     */
  volatile u_int32_t MCI_INTERRUPT_RX_MSG_RAW;    /*     0x1838 - 0x183c     */
  volatile u_int32_t MCI_INTERRUPT_RX_MSG_EN;     /*     0x183c - 0x1840     */
  volatile u_int32_t MCI_CPU_INT;                 /*     0x1840 - 0x1844     */
  volatile u_int32_t MCI_RX_STATUS;               /*     0x1844 - 0x1848     */
  volatile u_int32_t MCI_CONT_STATUS;             /*     0x1848 - 0x184c     */
  volatile u_int32_t MCI_BT_PRI0;                 /*     0x184c - 0x1850     */
  volatile u_int32_t MCI_BT_PRI1;                 /*     0x1850 - 0x1854     */
  volatile u_int32_t MCI_BT_PRI2;                 /*     0x1854 - 0x1858     */
  volatile u_int32_t MCI_BT_PRI3;                 /*     0x1858 - 0x185c     */
  volatile u_int32_t MCI_BT_PRI;                  /*     0x185c - 0x1860     */
  volatile u_int32_t MCI_WL_FREQ0;                /*     0x1860 - 0x1864     */
  volatile u_int32_t MCI_WL_FREQ1;                /*     0x1864 - 0x1868     */
  volatile u_int32_t MCI_WL_FREQ2;                /*     0x1868 - 0x186c     */
  volatile u_int32_t MCI_GAIN;                    /*     0x186c - 0x1870     */
  volatile u_int32_t MCI_WBTIMER1;                /*     0x1870 - 0x1874     */
  volatile u_int32_t MCI_WBTIMER2;                /*     0x1874 - 0x1878     */
  volatile u_int32_t MCI_WBTIMER3;                /*     0x1878 - 0x187c     */
  volatile u_int32_t MCI_WBTIMER4;                /*     0x187c - 0x1880     */
  volatile u_int32_t MCI_MAXGAIN;                 /*     0x1880 - 0x1884     */
  volatile u_int32_t MCI_HW_SCHD_TBL_CTL;         /*     0x1884 - 0x1888     */
  volatile u_int32_t MCI_HW_SCHD_TBL_D0;          /*     0x1888 - 0x188c     */
  volatile u_int32_t MCI_HW_SCHD_TBL_D1;          /*     0x188c - 0x1890     */
  volatile u_int32_t MCI_HW_SCHD_TBL_D2;          /*     0x1890 - 0x1894     */
  volatile u_int32_t MCI_HW_SCHD_TBL_D3;          /*     0x1894 - 0x1898     */
  volatile u_int32_t MCI_TX_PAYLOAD0;             /*     0x1898 - 0x189c     */
  volatile u_int32_t MCI_TX_PAYLOAD1;             /*     0x189c - 0x18a0     */
  volatile u_int32_t MCI_TX_PAYLOAD2;             /*     0x18a0 - 0x18a4     */
  volatile u_int32_t MCI_TX_PAYLOAD3;             /*     0x18a4 - 0x18a8     */
  volatile u_int32_t BTCOEX_WBTIMER;              /*     0x18a8 - 0x18ac     */
  volatile u_int32_t BTCOEX_CTRL;                 /*     0x18ac - 0x18b0     */
  volatile u_int32_t BTCOEX_WL_WEIGHTS0;          /*     0x18b0 - 0x18b4     */
  volatile u_int32_t BTCOEX_WL_WEIGHTS1;          /*     0x18b4 - 0x18b8     */
  volatile u_int32_t BTCOEX_WL_WEIGHTS2;          /*     0x18b8 - 0x18bc     */
  volatile u_int32_t BTCOEX_WL_WEIGHTS3;          /*     0x18bc - 0x18c0     */
  volatile u_int32_t BTCOEX_MAX_TXPWR[8];         /*     0x18c0 - 0x18e0     */
  volatile char pad__1[0x60];                     /*     0x18e0 - 0x1940     */
  volatile u_int32_t BTCOEX_WL_LNA;               /*     0x1940 - 0x1944     */
  volatile u_int32_t BTCOEX_RFGAIN_CTRL;          /*     0x1944 - 0x1948     */
  volatile u_int32_t BTCOEX_CTRL2;                /*     0x1948 - 0x194c     */
  volatile u_int32_t BTCOEX_RC;                   /*     0x194c - 0x1950     */
  volatile u_int32_t BTCOEX_MAX_RFGAIN[16];       /*     0x1950 - 0x1990     */
  volatile char pad__2[0xc0];                     /*     0x1990 - 0x1a50     */
  volatile u_int32_t BTCOEX_DBG;                  /*     0x1a50 - 0x1a54     */
  volatile u_int32_t MCI_LAST_HW_MSG_HDR;         /*     0x1a54 - 0x1a58     */
  volatile u_int32_t MCI_LAST_HW_MSG_BDY;         /*     0x1a58 - 0x1a5c     */
  volatile u_int32_t MCI_SCHD_TABLE_2;            /*     0x1a5c - 0x1a60     */
  volatile u_int32_t BTCOEX_CTRL3;                /*     0x1a60 - 0x1a64     */
  /* Aphrodite-start */
  volatile u_int32_t BTCOEX_WL_LNADIV;            /*     0x1a64 - 0x1a68     */
  volatile u_int32_t BTCOEX_TXTX_RANGE;           /*     0x1a68 - 0x1a6c     */
  volatile u_int32_t MCI_INTERRUPT_1_RAW;         /*     0x1a6c - 0x1a70     */
  volatile u_int32_t MCI_INTERRUPT_1_EN;          /*     0x1a70 - 0x1a74     */
  volatile u_int32_t MCI_EV_MISC;                 /*     0x1a74 - 0x1a78     */
  volatile u_int32_t MCI_DBG_CNT_CTRL;            /*     0x1a78 - 0x1a7c     */
  volatile u_int32_t MCI_DBG_CNT1;                /*     0x1a7c - 0x1a80     */
  volatile u_int32_t MCI_DBG_CNT2;                /*     0x1a80 - 0x1a84     */
  volatile u_int32_t MCI_DBG_CNT3;                /*     0x1a84 - 0x1a88     */
  volatile u_int32_t MCI_DBG_CNT4;                /*     0x1a88 - 0x1a8c     */
  volatile u_int32_t MCI_DBG_CNT5;                /*     0x1a8c - 0x1a90     */
  volatile u_int32_t MCI_DBG_CNT6;                /*     0x1a90 - 0x1a94     */
  volatile u_int32_t MCI_DBG_CNT7;                /*     0x1a94 - 0x1a98     */
  volatile u_int32_t MCI_DBG_CNT8;                /*     0x1a98 - 0x1a9c     */
  volatile u_int32_t MCI_DBG_CNT9;                /*     0x1a9c - 0x1aa0     */
  volatile u_int32_t MCI_DBG_CNT10;               /*     0x1aa0 - 0x1aa4     */
  volatile u_int32_t MCI_DBG_CNT11;               /*     0x1aa4 - 0x1aa8     */
  volatile u_int32_t MCI_DBG_CNT12;               /*     0x1aa8 - 0x1aac     */
  volatile u_int32_t MCI_DBG_CNT13;               /*     0x1aac - 0x1ab0     */
  volatile u_int32_t MCI_DBG_CNT14;               /*     0x1ab0 - 0x1ab4     */
  volatile u_int32_t MCI_DBG_CNT15;               /*     0x1ab4 - 0x1ab8     */
  volatile u_int32_t MCI_DBG_CNT16;               /*     0x1ab8 - 0x1abc     */
  volatile u_int32_t MCI_DBG_CNT17;               /*     0x1abc - 0x1ac0     */
  volatile u_int32_t MCI_DBG_CNT18;               /*     0x1ac0 - 0x1ac4     */
  volatile u_int32_t MCI_DBG_CNT19;               /*     0x1ac4 - 0x1ac8     */
  /* Aphrodite-end */
};

struct uart1_reg_csr {
  volatile u_int32_t UART_DATA;                   /*        0x0 - 0x4        */
  volatile u_int32_t UART_CONTROL;                /*        0x4 - 0x8        */
  volatile u_int32_t UART_CLKDIV;                 /*        0x8 - 0xc        */
  volatile u_int32_t UART_INT;                    /*        0xc - 0x10       */
  volatile u_int32_t UART_INT_EN;                 /*       0x10 - 0x14       */
};

struct wlan_bt_glb_reg_pcie {
  volatile char pad__0[0x20000];                  /*        0x0 - 0x20000    */
  volatile u_int32_t GLB_GPIO_CONTROL;            /*    0x20000 - 0x20004    */
  volatile u_int32_t GLB_WLAN_WOW_STATUS;         /*    0x20004 - 0x20008    */
  volatile u_int32_t GLB_WLAN_WOW_ENABLE;         /*    0x20008 - 0x2000c    */
  volatile u_int32_t GLB_EMB_CPU_WOW_STATUS;      /*    0x2000c - 0x20010    */
  volatile u_int32_t GLB_EMB_CPU_WOW_ENABLE;      /*    0x20010 - 0x20014    */
  volatile u_int32_t GLB_MBOX_CONTROL_STATUS;     /*    0x20014 - 0x20018    */
  volatile u_int32_t GLB_SW_WOW_CONTROL;          /*    0x20018 - 0x2001c    */
  volatile u_int32_t GLB_APB_TIMEOUT;             /*    0x2001c - 0x20020    */
  volatile u_int32_t GLB_OTP_LDO_CONTROL;         /*    0x20020 - 0x20024    */
  volatile u_int32_t GLB_OTP_LDO_POWER_GOOD;      /*    0x20024 - 0x20028    */
  volatile u_int32_t GLB_OTP_LDO_STATUS;          /*    0x20028 - 0x2002c    */
  volatile u_int32_t GLB_SWREG_DISCONT_MODE;      /*    0x2002c - 0x20030    */
  volatile u_int32_t GLB_BT_GPIO_REMAP_OUT_CONTROL0;
                                                  /*    0x20030 - 0x20034    */
  volatile u_int32_t GLB_BT_GPIO_REMAP_OUT_CONTROL1;
                                                  /*    0x20034 - 0x20038    */
  volatile u_int32_t GLB_BT_GPIO_REMAP_IN_CONTROL0;
                                                  /*    0x20038 - 0x2003c    */
  volatile u_int32_t GLB_BT_GPIO_REMAP_IN_CONTROL1;
                                                  /*    0x2003c - 0x20040    */
  volatile u_int32_t GLB_BT_GPIO_REMAP_IN_CONTROL2;
                                                  /*    0x20040 - 0x20044    */
  union {
      struct {
          volatile char pad__1[0xc];                      /*    0x20044 - 0x20050    */
          volatile u_int32_t GLB_SCRATCH[16];             /*    0x20050 - 0x20090    */
          volatile char pad__2[0x370];                    /*    0x20090 - 0x20400    */
      } Jupiter_10;
      struct {
          volatile u_int32_t GLB_CONTROL;                 /*    0x20044 - 0x20048    */
          volatile u_int32_t GLB_STATUS;                  /*    0x20048 - 0x2004c    */
          volatile u_int32_t GLB_SCRATCH[16];             /*    0x2004c - 0x2008c    */
          volatile char pad__1[0x354];                    /*    0x2008c - 0x203e0    */
          struct uart1_reg_csr shared_uart1;              /*    0x203e0 - 0x203f4    */
          volatile char pad__2[0xc];                      /*    0x203f4 - 0x20400    */
      } Jupiter_20;
      struct {
          volatile u_int32_t GLB_CONTROL;                 /*    0x20044 - 0x20048    */
          volatile u_int32_t GLB_STATUS;                  /*    0x20048 - 0x2004c    */
          volatile char pad__1[0x4];                      /*    0x2004c - 0x20050    */
          volatile u_int32_t GLB_SCRATCH[16];             /*    0x20050 - 0x20090    */
          volatile char pad__2[0x70];                     /*    0x20090 - 0x20100    */
          volatile u_int32_t PLLOSC_CTRL;                 /*    0x20100 - 0x20104    */
          volatile u_int32_t PLLOSC_CFG;                  /*    0x20104 - 0x20108    */
          volatile char pad__3[0x4];                      /*    0x20108 - 0x2010c    */
          volatile u_int32_t INNOP_MEM_CONTROL;           /*    0x2010c - 0x20110    */
          volatile u_int32_t USB_CONFIG;                  /*    0x20110 - 0x20114    */
          volatile u_int32_t USB_SPARE32;                 /*    0x20114 - 0x20118    */
          volatile u_int32_t PCIE_AHB_BRIDGE_CFG;         /*    0x20118 - 0x2011c    */
          volatile u_int32_t PCIE_AHB_BRIDGE_CTRL;        /*    0x2011c - 0x20120    */
          volatile u_int32_t OPTIONAL_CTL_REG;            /*    0x20120 - 0x20124    */
          volatile u_int32_t PCIE_PWR_CTRL_REG;           /*    0x20124 - 0x20128    */
          volatile char pad__4[0x4];                      /*    0x20128 - 0x2012c    */
          volatile u_int32_t USBDEV_CLK_CTL_REG;          /*    0x2012c - 0x20130    */
          volatile u_int32_t UHOST_DEBUG_FSM;             /*    0x20130 - 0x20134    */
          volatile u_int32_t BRIDGE_DEBUG_FSM;            /*    0x20134 - 0x20138    */
          volatile u_int32_t BRIDGE_DEBUG_PTR;            /*    0x20138 - 0x2013c    */
          volatile u_int32_t BRIDGE_DEBUG_CLIENT_LOG0;    /*    0x2013c - 0x20140    */
          volatile u_int32_t BRIDGE_DEBUG_CLIENT_LOG1;    /*    0x20140 - 0x20144    */
          volatile u_int32_t BRIDGE_DEBUG_CLIENT_LOG2;    /*    0x20144 - 0x20148    */
          volatile char pad__5[0x298];                    /*    0x20148 - 0x203e0    */
          volatile u_int32_t GLB_UART[8];                 /*    0x203e0 - 0x20400    */
      } Aphrodite;
  } overlay_0x20044;
};

struct jupiter_reg_map__rtc_reg_csr {
  volatile u_int32_t RESET_CONTROL;               /*        0x0 - 0x4        */
  volatile u_int32_t PLL_SETTLE;                  /*        0x4 - 0x8        */
  volatile u_int32_t VDD_SETTLE;                  /*        0x8 - 0xc        */
  volatile u_int32_t PWR_CONTROL;                 /*        0xc - 0x10       */
  volatile u_int32_t XTAL_SETTLE;                 /*       0x10 - 0x14       */
  volatile u_int32_t RTC_CLOCK;                   /*       0x14 - 0x18       */
  volatile u_int32_t CORE_CLOCK;                  /*       0x18 - 0x1c       */
  volatile u_int32_t CLKBOOT;                     /*       0x1c - 0x20       */
  volatile u_int32_t UART_CLOCK;                  /*       0x20 - 0x24       */
  volatile u_int32_t SI_CLOCK;                    /*       0x24 - 0x28       */
  volatile u_int32_t CLOCK_CONTROL;               /*       0x28 - 0x2c       */
  volatile u_int32_t WDT_CONTROL;                 /*       0x2c - 0x30       */
  volatile u_int32_t WDT_STATUS;                  /*       0x30 - 0x34       */
  volatile u_int32_t WDT;                         /*       0x34 - 0x38       */
  volatile u_int32_t WDT_COUNT;                   /*       0x38 - 0x3c       */
  volatile u_int32_t WDT_RESET;                   /*       0x3c - 0x40       */
  volatile u_int32_t RTC_INT_STATUS;              /*       0x40 - 0x44       */
  volatile u_int32_t INT_SRC_MAPPING;             /*       0x44 - 0x48       */
  volatile u_int32_t UART_SI_GPIO_INT_STATUS;     /*       0x48 - 0x4c       */
  volatile u_int32_t LF_TIMER0;                   /*       0x4c - 0x50       */
  volatile u_int32_t LF_TIMER_COUNT0;             /*       0x50 - 0x54       */
  volatile u_int32_t LF_TIMER_CONTROL0;           /*       0x54 - 0x58       */
  volatile u_int32_t LF_TIMER_STATUS0;            /*       0x58 - 0x5c       */
  volatile u_int32_t LF_TIMER1;                   /*       0x5c - 0x60       */
  volatile u_int32_t LF_TIMER_COUNT1;             /*       0x60 - 0x64       */
  volatile u_int32_t LF_TIMER_CONTROL1;           /*       0x64 - 0x68       */
  volatile u_int32_t LF_TIMER_STATUS1;            /*       0x68 - 0x6c       */
  volatile u_int32_t RESET_CAUSE;                 /*       0x6c - 0x70       */
  volatile u_int32_t SYSTEM_SLEEP;                /*       0x70 - 0x74       */
  volatile u_int32_t KEEP_AWAKE;                  /*       0x74 - 0x78       */
  volatile u_int32_t LPO_CAL;                     /*       0x78 - 0x7c       */
  volatile u_int32_t OBS_CLOCK;                   /*       0x7c - 0x80       */
  volatile u_int32_t CHIP_REV;                    /*       0x80 - 0x84       */
  volatile u_int32_t PWR_ON_TIME;                 /*       0x84 - 0x88       */
  volatile u_int32_t PWD_TIME;                    /*       0x88 - 0x8c       */
  volatile u_int32_t USB_SUSPEND_POWER_REG;       /*       0x8c - 0x90       */
  volatile u_int32_t USB_SUSPEND_WAKEUP_COUNTER_REG;
                                                  /*       0x90 - 0x94       */
  volatile u_int32_t LPO_STEP_CFG;                /*       0x94 - 0x98       */
  volatile u_int32_t LPO_FAST_CYL;                /*       0x98 - 0x9c       */
  volatile u_int32_t LPO_LPO1;                    /*       0x9c - 0xa0       */
  volatile u_int32_t LPO_LPO2;                    /*       0xa0 - 0xa4       */
  volatile u_int32_t LPO_INT_RAW;                 /*       0xa4 - 0xa8       */
  volatile u_int32_t LPO_N1TARGET;                /*       0xa8 - 0xac       */
  volatile u_int32_t LPO_N2TARGET;                /*       0xac - 0xb0       */
  volatile u_int32_t LPO_DN1_MULT;                /*       0xb0 - 0xb4       */
  volatile u_int32_t LPO_DN2_MULT;                /*       0xb4 - 0xb8       */
  volatile u_int32_t LPO_NTARGET_MIN;             /*       0xb8 - 0xbc       */
  volatile u_int32_t LPO_NTARGET_MAX;             /*       0xbc - 0xc0       */
  volatile u_int32_t LPO_N1TARGET_DEBUG;          /*       0xc0 - 0xc4       */
  volatile u_int32_t LPO_N2TARGET_DEBUG;          /*       0xc4 - 0xc8       */
  volatile u_int32_t OTP;                         /*       0xc8 - 0xcc       */
  volatile u_int32_t OTP_STATUS;                  /*       0xcc - 0xd0       */
  volatile u_int32_t USB_PHY_TEST;                /*       0xd0 - 0xd4       */
  volatile u_int32_t USB_PHY_CONFIG;              /*       0xd4 - 0xd8       */
  volatile u_int32_t ADDAC_CLOCK_PHASE;           /*       0xd8 - 0xdc       */
  volatile u_int32_t THERM_CONTROL;               /*       0xdc - 0xe0       */
  volatile u_int32_t THERM_TRIGGER_INTERVAL1;     /*       0xe0 - 0xe4       */
  volatile u_int32_t THERM_TRIGGER_INTERVAL2;     /*       0xe4 - 0xe8       */
  volatile u_int32_t THERM_CORRECTION;            /*       0xe8 - 0xec       */
  volatile u_int32_t THERM_CORRECTION_VALUE1;     /*       0xec - 0xf0       */
  volatile u_int32_t THERM_CORRECTION_VALUE2;     /*       0xf0 - 0xf4       */
  volatile u_int32_t PLL_CONTROL;                 /*       0xf4 - 0xf8       */
  volatile u_int32_t VDD12D_SENSE;                /*       0xf8 - 0xfc       */
  volatile u_int32_t RBIAS;                       /*       0xfc - 0x100      */
  volatile u_int32_t THERM_CONTROL_VAL;           /*      0x100 - 0x104      */
  volatile u_int32_t PLL_OSC_CONTROL;             /*      0x104 - 0x108      */
  volatile u_int32_t AHB_ERR_INT;                 /*      0x108 - 0x10c      */
  volatile u_int32_t INT_P2_EN;                   /*      0x10c - 0x110      */
  volatile u_int32_t XTAL_CLOCK;                  /*      0x110 - 0x114      */
  volatile u_int32_t CHIP_MODES;                  /*      0x114 - 0x118      */
  volatile u_int32_t XTAL_FREQ;                   /*      0x118 - 0x11c      */
  volatile u_int32_t DEBUGGER_RESET;              /*      0x11c - 0x120      */
  volatile u_int32_t LPO_3_2K_CLK;                /*      0x120 - 0x124      */
  volatile u_int32_t LPO1_CLK_DEBUG;              /*      0x124 - 0x128      */
  volatile u_int32_t LPO2_CLK_DEBUG;              /*      0x128 - 0x12c      */
  volatile u_int32_t ADDR_CHECK;                  /*      0x12c - 0x130      */
  volatile u_int32_t RTC_DUMMY;                   /*      0x130 - 0x134      */
};

struct jupiter_reg_map__vmc_reg_csr {
  volatile u_int32_t BANK0_ADDR;                  /*        0x0 - 0x4        */
  volatile u_int32_t BANK1_ADDR;                  /*        0x4 - 0x8        */
  volatile u_int32_t BANK_CONFIG;                 /*        0x8 - 0xc        */
  volatile u_int32_t MC_BCAM_CONFLICT_ERROR;      /*        0xc - 0x10       */
  volatile char pad__0[0x10];                     /*       0x10 - 0x20       */
  volatile u_int32_t MC_BCAM_COMPARE[128];        /*       0x20 - 0x220      */
  volatile u_int32_t MC_BCAM_VALID[128];          /*      0x220 - 0x420      */
  volatile u_int32_t MC_BCAM_TARGET[128];         /*      0x420 - 0x620      */
};

struct jupiter_reg_map__apb_map_csr__uart_reg_csr {
  volatile u_int32_t UART_DATA;                   /*        0x0 - 0x4        */
  volatile u_int32_t UART_CONTROL;                /*        0x4 - 0x8        */
  volatile u_int32_t UART_CLKDIV;                 /*        0x8 - 0xc        */
  volatile u_int32_t UART_INT;                    /*        0xc - 0x10       */
  volatile u_int32_t UART_INT_EN;                 /*       0x10 - 0x14       */
};

struct jupiter_reg_map__si_reg_csr {
  volatile u_int32_t SI_CONFIG;                   /*        0x0 - 0x4        */
  volatile u_int32_t SI_CS;                       /*        0x4 - 0x8        */
  volatile u_int32_t SI_TX_DATA0;                 /*        0x8 - 0xc        */
  volatile u_int32_t SI_TX_DATA1;                 /*        0xc - 0x10       */
  volatile u_int32_t SI_RX_DATA0;                 /*       0x10 - 0x14       */
  volatile u_int32_t SI_RX_DATA1;                 /*       0x14 - 0x18       */
};

struct jupiter_reg_map__gpio_reg_csr {
  volatile u_int32_t GPIO_OUT;                    /*        0x0 - 0x4        */
  volatile u_int32_t GPIO_OUT_W1TS;               /*        0x4 - 0x8        */
  volatile u_int32_t GPIO_OUT_W1TC;               /*        0x8 - 0xc        */
  volatile u_int32_t GPIO_ENABLE;                 /*        0xc - 0x10       */
  volatile u_int32_t GPIO_ENABLE_W1TS;            /*       0x10 - 0x14       */
  volatile u_int32_t GPIO_ENABLE_W1TC;            /*       0x14 - 0x18       */
  volatile u_int32_t GPIO_IN;                     /*       0x18 - 0x1c       */
  volatile u_int32_t GPIO_STATUS;                 /*       0x1c - 0x20       */
  volatile u_int32_t GPIO_STATUS_W1TS;            /*       0x20 - 0x24       */
  volatile u_int32_t GPIO_STATUS_W1TC;            /*       0x24 - 0x28       */
  volatile u_int32_t GPIO_INT_ENABLE;             /*       0x28 - 0x2c       */
  volatile u_int32_t GPIO_INT_ENABLE_W1TS;        /*       0x2c - 0x30       */
  volatile u_int32_t GPIO_INT_ENABLE_W1TC;        /*       0x30 - 0x34       */
  volatile u_int32_t GPIO_PIN0;                   /*       0x34 - 0x38       */
  volatile u_int32_t GPIO_PIN1;                   /*       0x38 - 0x3c       */
  volatile u_int32_t GPIO_PIN2;                   /*       0x3c - 0x40       */
  volatile u_int32_t GPIO_PIN3;                   /*       0x40 - 0x44       */
  volatile u_int32_t GPIO_PIN4;                   /*       0x44 - 0x48       */
  volatile u_int32_t GPIO_PIN5;                   /*       0x48 - 0x4c       */
  volatile u_int32_t GPIO_PIN6;                   /*       0x4c - 0x50       */
  volatile u_int32_t GPIO_PIN7;                   /*       0x50 - 0x54       */
  volatile u_int32_t GPIO_PIN8;                   /*       0x54 - 0x58       */
  volatile u_int32_t GPIO_PIN9;                   /*       0x58 - 0x5c       */
  volatile u_int32_t GPIO_PIN10;                  /*       0x5c - 0x60       */
  volatile u_int32_t GPIO_PIN11;                  /*       0x60 - 0x64       */
  volatile u_int32_t GPIO_PIN12;                  /*       0x64 - 0x68       */
  volatile u_int32_t GPIO_PIN13;                  /*       0x68 - 0x6c       */
  volatile u_int32_t GPIO_PIN14;                  /*       0x6c - 0x70       */
  volatile u_int32_t GPIO_PIN15;                  /*       0x70 - 0x74       */
  volatile u_int32_t GPIO_PIN16;                  /*       0x74 - 0x78       */
  volatile u_int32_t GPIO_PIN17;                  /*       0x78 - 0x7c       */
  volatile u_int32_t GPIO_PIN18;                  /*       0x7c - 0x80       */
  volatile u_int32_t GPIO_PIN19;                  /*       0x80 - 0x84       */
  volatile u_int32_t SIGMA_DELTA;                 /*       0x84 - 0x88       */
  volatile u_int32_t DEBUG_CONTROL;               /*       0x88 - 0x8c       */
  volatile u_int32_t DEBUG_INPUT_SEL;             /*       0x8c - 0x90       */
  volatile u_int32_t DEBUG_PIN_SEL;               /*       0x90 - 0x94       */
  volatile u_int32_t DEBUG_OBS_BUS;               /*       0x94 - 0x98       */
};

struct jupiter_reg_map__mbox_reg_csr {
  volatile u_int32_t MBOX_FIFO[4];                /*        0x0 - 0x10       */
  volatile u_int32_t MBOX_FIFO_STATUS;            /*       0x10 - 0x14       */
  volatile u_int32_t MBOX_DMA_POLICY;             /*       0x14 - 0x18       */
  volatile u_int32_t MBOX0_DMA_RX_DESCRIPTOR_BASE;
                                                  /*       0x18 - 0x1c       */
  volatile u_int32_t MBOX0_DMA_RX_CONTROL;        /*       0x1c - 0x20       */
  volatile u_int32_t MBOX0_DMA_TX_DESCRIPTOR_BASE;
                                                  /*       0x20 - 0x24       */
  volatile u_int32_t MBOX0_DMA_TX_CONTROL;        /*       0x24 - 0x28       */
  volatile u_int32_t MBOX1_DMA_RX_DESCRIPTOR_BASE;
                                                  /*       0x28 - 0x2c       */
  volatile u_int32_t MBOX1_DMA_RX_CONTROL;        /*       0x2c - 0x30       */
  volatile u_int32_t MBOX1_DMA_TX_DESCRIPTOR_BASE;
                                                  /*       0x30 - 0x34       */
  volatile u_int32_t MBOX1_DMA_TX_CONTROL;        /*       0x34 - 0x38       */
  volatile u_int32_t MBOX2_DMA_RX_DESCRIPTOR_BASE;
                                                  /*       0x38 - 0x3c       */
  volatile u_int32_t MBOX2_DMA_RX_CONTROL;        /*       0x3c - 0x40       */
  volatile u_int32_t MBOX2_DMA_TX_DESCRIPTOR_BASE;
                                                  /*       0x40 - 0x44       */
  volatile u_int32_t MBOX2_DMA_TX_CONTROL;        /*       0x44 - 0x48       */
  volatile u_int32_t MBOX3_DMA_RX_DESCRIPTOR_BASE;
                                                  /*       0x48 - 0x4c       */
  volatile u_int32_t MBOX3_DMA_RX_CONTROL;        /*       0x4c - 0x50       */
  volatile u_int32_t MBOX3_DMA_TX_DESCRIPTOR_BASE;
                                                  /*       0x50 - 0x54       */
  volatile u_int32_t MBOX3_DMA_TX_CONTROL;        /*       0x54 - 0x58       */
  volatile u_int32_t FIFO_TIMEOUT;                /*       0x58 - 0x5c       */
  volatile u_int32_t MBOX_INT_STATUS;             /*       0x5c - 0x60       */
  volatile u_int32_t MBOX_INT_ENABLE;             /*       0x60 - 0x64       */
  volatile u_int32_t MBOX_DEBUG;                  /*       0x64 - 0x68       */
  volatile u_int32_t MBOX_FIFO_RESET;             /*       0x68 - 0x6c       */
  volatile char pad__0[0x4];                      /*       0x6c - 0x70       */
  volatile u_int32_t MBOX_TXFIFO_POP[4];          /*       0x70 - 0x80       */
  volatile u_int32_t HCI_FRAMER;                  /*       0x80 - 0x84       */
  volatile u_int32_t STEREO_CONFIG;               /*       0x84 - 0x88       */
  volatile u_int32_t STEREO_CONFIG1;              /*       0x88 - 0x8c       */
  volatile u_int32_t STEREO_CONFIG2;              /*       0x8c - 0x90       */
  volatile u_int32_t STEREO_VOLUME;               /*       0x90 - 0x94       */
  volatile u_int32_t STEREO_DEBUG;                /*       0x94 - 0x98       */
  volatile u_int32_t STEREO_CONFIG3;              /*       0x98 - 0x9c       */
};

struct jupiter_reg_map__lc_dma_reg_csr {
  volatile u_int32_t LC_DMA_MASTER;               /*        0x0 - 0x4        */
  volatile u_int32_t LC_DMA_TX_CONTROL;           /*        0x4 - 0x8        */
  volatile u_int32_t LC_DMA_RX_CONTROL;           /*        0x8 - 0xc        */
  volatile u_int32_t LC_DMA_TX_HW;                /*        0xc - 0x10       */
  volatile u_int32_t LC_DMA_RX_HW;                /*       0x10 - 0x14       */
  volatile u_int32_t LC_DMA_INT_STATUS;           /*       0x14 - 0x18       */
  volatile u_int32_t LC_DMA_TX_STATUS;            /*       0x18 - 0x1c       */
  volatile u_int32_t LC_DMA_TX_STATUS_W1TC;       /*       0x1c - 0x20       */
  volatile u_int32_t LC_DMA_TX_ENABLE;            /*       0x20 - 0x24       */
  volatile u_int32_t LC_DMA_RX_STATUS;            /*       0x24 - 0x28       */
  volatile u_int32_t LC_DMA_RX_STATUS_W1TC;       /*       0x28 - 0x2c       */
  volatile u_int32_t LC_DMA_RX_ENABLE;            /*       0x2c - 0x30       */
  volatile u_int32_t LC_DMA_DEBUG;                /*       0x30 - 0x34       */
};

struct jupiter_reg_map__lc_reg_csr {
  volatile u_int32_t LC_DEV_PARAM_DAC_L;          /*        0x0 - 0x4        */
  volatile u_int32_t LC_DEV_PARAM_DAC_U;          /*        0x4 - 0x8        */
  volatile u_int32_t LC_DEV_PARAM_BD_ADDR;        /*        0x8 - 0xc        */
  volatile u_int32_t LC_DEV_PARAM_FHS;            /*        0xc - 0x10       */
  volatile u_int32_t LC_DEV_PARAM_CTL;            /*       0x10 - 0x14       */
  volatile u_int32_t LC_DEV_PARAM_TIMING;         /*       0x14 - 0x18       */
  volatile u_int32_t LC_DEV_PARAM_TIMING_1;       /*       0x18 - 0x1c       */
  volatile u_int32_t LC_MISC;                     /*       0x1c - 0x20       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND1;       /*       0x20 - 0x24       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND2;       /*       0x24 - 0x28       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND3;       /*       0x28 - 0x2c       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND4;       /*       0x2c - 0x30       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND5;       /*       0x30 - 0x34       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND6;       /*       0x34 - 0x38       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND7;       /*       0x38 - 0x3c       */
  volatile u_int32_t LC_DEV_PARAM_COMMAND8;       /*       0x3c - 0x40       */
  volatile u_int32_t LC_DEV_PARAM_AC1_L;          /*       0x40 - 0x44       */
  volatile u_int32_t LC_DEV_PARAM_AC1_U;          /*       0x44 - 0x48       */
  volatile u_int32_t LC_DEV_PARAM_AC2_L;          /*       0x48 - 0x4c       */
  volatile u_int32_t LC_DEV_PARAM_AC2_U;          /*       0x4c - 0x50       */
  volatile u_int32_t LC_DEV_PARAM_CLOCK_OFFSET;   /*       0x50 - 0x54       */
  volatile u_int32_t LC_FREQUENCY;                /*       0x54 - 0x58       */
  volatile u_int32_t LC_CH_ASSESS_1;              /*       0x58 - 0x5c       */
  volatile u_int32_t LC_CH_ASSESS_2;              /*       0x5c - 0x60       */
  volatile u_int32_t LC_DEV_PARAM_ENCRYPTION_KEY0;
                                                  /*       0x60 - 0x64       */
  volatile u_int32_t LC_DEV_PARAM_ENCRYPTION_KEY1;
                                                  /*       0x64 - 0x68       */
  volatile u_int32_t LC_DEV_PARAM_ENCRYPTION_KEY2;
                                                  /*       0x68 - 0x6c       */
  volatile u_int32_t LC_DEV_PARAM_ENCRYPTION_KEY3;
                                                  /*       0x6c - 0x70       */
  volatile u_int32_t LC_DEV_PARAM_TX_CONTROL;     /*       0x70 - 0x74       */
  volatile u_int32_t LC_DEV_PARAM_RX_CONTROL;     /*       0x74 - 0x78       */
  volatile u_int32_t LC_DEV_PARAM_RX_STATUS1;     /*       0x78 - 0x7c       */
  volatile u_int32_t LC_DEV_PARAM_RX_STATUS2;     /*       0x7c - 0x80       */
  volatile char pad__0[0x4];                      /*       0x80 - 0x84       */
  volatile u_int32_t LC_BT_CLOCK0;                /*       0x84 - 0x88       */
  volatile u_int32_t LC_BT_CLOCK1;                /*       0x88 - 0x8c       */
  volatile u_int32_t LC_BT_CLOCK2;                /*       0x8c - 0x90       */
  volatile u_int32_t LC_BT_CLOCK3;                /*       0x90 - 0x94       */
  volatile u_int32_t LC_SYM_TIME0;                /*       0x94 - 0x98       */
  volatile u_int32_t LC_SYM_TIME1;                /*       0x98 - 0x9c       */
  volatile u_int32_t LC_SYM_TIME2;                /*       0x9c - 0xa0       */
  volatile u_int32_t LC_SYM_TIME3;                /*       0xa0 - 0xa4       */
  volatile char pad__1[0x4];                      /*       0xa4 - 0xa8       */
  volatile u_int32_t LC_ABORT;                    /*       0xa8 - 0xac       */
  volatile u_int32_t LC_PRBS;                     /*       0xac - 0xb0       */
  volatile u_int32_t LC_LAST_CORR_HECOK;          /*       0xb0 - 0xb4       */
  volatile char pad__2[0x4c];                     /*       0xb4 - 0x100      */
  volatile u_int32_t LC_SM_AFH_TABLE[24];         /*      0x100 - 0x160      */
  volatile char pad__3[0x20];                     /*      0x160 - 0x180      */
  volatile u_int32_t LC_SM_AFH_BITMAP_0;          /*      0x180 - 0x184      */
  volatile u_int32_t LC_SM_AFH_BITMAP_1;          /*      0x184 - 0x188      */
  volatile u_int32_t LC_SM_AFH_BITMAP_2;          /*      0x188 - 0x18c      */
  volatile u_int32_t LC_STAT0;                    /*      0x18c - 0x190      */
  volatile u_int32_t LC_STAT1;                    /*      0x190 - 0x194      */
  volatile u_int32_t LC_STAT2;                    /*      0x194 - 0x198      */
  volatile u_int32_t LC_STAT3;                    /*      0x198 - 0x19c      */
  volatile u_int32_t LC_STAT4;                    /*      0x19c - 0x1a0      */
  volatile u_int32_t LC_STAT5;                    /*      0x1a0 - 0x1a4      */
  volatile u_int32_t LC_STAT6;                    /*      0x1a4 - 0x1a8      */
  volatile u_int32_t LC_STAT7;                    /*      0x1a8 - 0x1ac      */
  volatile u_int32_t LC_STAT8;                    /*      0x1ac - 0x1b0      */
  volatile u_int32_t LC_STAT9;                    /*      0x1b0 - 0x1b4      */
  volatile char pad__4[0x14c];                    /*      0x1b4 - 0x300      */
  volatile u_int32_t LC_INTERRUPT_RAW;            /*      0x300 - 0x304      */
  volatile u_int32_t LC_INTERRUPT_EN;             /*      0x304 - 0x308      */
  volatile u_int32_t LC_INTERRUPT_RX_STATUS;      /*      0x308 - 0x30c      */
  volatile u_int32_t LC_AUDIO_DATAPATH;           /*      0x30c - 0x310      */
  volatile u_int32_t LC_VOICE_CHAN0;              /*      0x310 - 0x314      */
  volatile u_int32_t LC_VOICE_CHAN1;              /*      0x314 - 0x318      */
  volatile u_int32_t LC_VOICE_CHAN0_RX_ENERGY;    /*      0x318 - 0x31c      */
  volatile u_int32_t LC_VOICE_CHAN1_RX_ENERGY;    /*      0x31c - 0x320      */
  volatile u_int32_t LC_VOICE_CHAN0_TX_ENERGY;    /*      0x320 - 0x324      */
  volatile u_int32_t LC_VOICE_CHAN1_TX_ENERGY;    /*      0x324 - 0x328      */
  volatile u_int32_t LC_VOICE_CHAN0_ZERO_CROSS;   /*      0x328 - 0x32c      */
  volatile u_int32_t LC_VOICE_CHAN1_ZERO_CROSS;   /*      0x32c - 0x330      */
  volatile char pad__5[0xd0];                     /*      0x330 - 0x400      */
  volatile u_int32_t LC_RX_CTRL_DATAPATH;         /*      0x400 - 0x404      */
  volatile u_int32_t LC_DEBUG;                    /*      0x404 - 0x408      */
  volatile u_int32_t LC_TX_CTRL_DATAPATH;         /*      0x408 - 0x40c      */
  volatile u_int32_t LC_COMMAND9;                 /*      0x40c - 0x410      */
  volatile u_int32_t BT_CLOCK0_FREE_RUN;          /*      0x410 - 0x414      */
  volatile u_int32_t BT_CLOCK1_FREE_RUN;          /*      0x414 - 0x418      */
  volatile u_int32_t BT_CLOCK2_FREE_RUN;          /*      0x418 - 0x41c      */
  volatile u_int32_t BT_CLOCK3_FREE_RUN;          /*      0x41c - 0x420      */
  volatile u_int32_t LC_DEV_PARAM_COMMAND10;      /*      0x420 - 0x424      */
  volatile u_int32_t LC_DEV_PARAM_TIMING_2;       /*      0x424 - 0x428      */
  volatile u_int32_t LC_DEV_PARAM_COMMAND11;      /*      0x428 - 0x42c      */
  volatile u_int32_t MCI_SUB_PRIORITY_TABLE_0;    /*      0x42c - 0x430      */
  volatile u_int32_t MCI_SUB_PRIORITY_TABLE_1;    /*      0x430 - 0x434      */
  volatile u_int32_t MCI_SUB_PRIORITY_TABLE_2;    /*      0x434 - 0x438      */
  volatile u_int32_t MCI_SUB_PRIORITY_TABLE_3;    /*      0x438 - 0x43c      */
  volatile u_int32_t MCI_SUB_PRIORITY_TABLE_4;    /*      0x43c - 0x440      */
  volatile u_int32_t MCI_COMMAND0;                /*      0x440 - 0x444      */
  volatile u_int32_t MCI_COMMAND1;                /*      0x444 - 0x448      */
  volatile u_int32_t MCI_COMMAND2;                /*      0x448 - 0x44c      */
  volatile u_int32_t MCI_RX_CTRL;                 /*      0x44c - 0x450      */
  volatile u_int32_t MCI_TX_CTRL;                 /*      0x450 - 0x454      */
  volatile u_int32_t MCI_MSG_ATTRIBUTES_TABLE;    /*      0x454 - 0x458      */
  volatile u_int32_t MCI_SCHD_TABLE_0;            /*      0x458 - 0x45c      */
  volatile u_int32_t MCI_SCHD_TABLE_1;            /*      0x45c - 0x460      */
  volatile u_int32_t MCI_GPM_0;                   /*      0x460 - 0x464      */
  volatile u_int32_t MCI_GPM_1;                   /*      0x464 - 0x468      */
  volatile u_int32_t MCI_INTERRUPT_RAW;           /*      0x468 - 0x46c      */
  volatile u_int32_t MCI_INTERRUPT_EN;            /*      0x46c - 0x470      */
  volatile u_int32_t MCI_REMOTE_CPU_INT;          /*      0x470 - 0x474      */
  volatile u_int32_t MCI_REMOTE_CPU_INT_EN;       /*      0x474 - 0x478      */
  volatile u_int32_t MCI_INTERRUPT_RX_MSG_RAW;    /*      0x478 - 0x47c      */
  volatile u_int32_t MCI_INTERRUPT_RX_MSG_EN;     /*      0x47c - 0x480      */
  volatile u_int32_t MCI_CPU_INT;                 /*      0x480 - 0x484      */
  volatile u_int32_t MCI_RX_STATUS;               /*      0x484 - 0x488      */
  volatile u_int32_t WBTIMER;                     /*      0x488 - 0x48c      */
  volatile u_int32_t WB_BTCLK_SYNC_PN0;           /*      0x48c - 0x490      */
  volatile u_int32_t WB_BTCLK_SYNC_PN1;           /*      0x490 - 0x494      */
  volatile u_int32_t WB_BTCLK_SYNC_PN2;           /*      0x494 - 0x498      */
  volatile u_int32_t WB_BTCLK_SYNC_PN3;           /*      0x498 - 0x49c      */
  volatile u_int32_t LC_SERIAL;                   /*      0x49c - 0x4a0      */
  volatile u_int32_t LC_PHY_ERR;                  /*      0x4a0 - 0x4a4      */
  volatile u_int32_t LC_PHY_ERR_0;                /*      0x4a4 - 0x4a8      */
  volatile u_int32_t LC_PHY_ERR_1;                /*      0x4a8 - 0x4ac      */
  volatile u_int32_t LC_PHY_ERR_2;                /*      0x4ac - 0x4b0      */
  volatile u_int32_t LC_PHY_ERR_3;                /*      0x4b0 - 0x4b4      */
  volatile u_int32_t LC_PHY_ERR_4;                /*      0x4b4 - 0x4b8      */
  volatile u_int32_t LC_PHY_ERR_5;                /*      0x4b8 - 0x4bc      */
  volatile u_int32_t LC_SF_CTRL;                  /*      0x4bc - 0x4c0      */
  volatile u_int32_t LC_DUMMY;                    /*      0x4c0 - 0x4c4      */
  volatile u_int32_t LC_FOR_BQB;                  /*      0x4c4 - 0x4c8      */
  volatile u_int32_t SHARED_LNA_PARAM;            /*      0x4c8 - 0x4cc      */
  volatile u_int32_t LC_CHNASS0_SUB1;             /*      0x4cc - 0x4d0      */
  volatile u_int32_t LC_CHNASS1_SUB1;             /*      0x4d0 - 0x4d4      */
  volatile u_int32_t LC_CHNASS0_SUB2;             /*      0x4d4 - 0x4d8      */
  volatile u_int32_t LC_CHNASS1_SUB2;             /*      0x4d8 - 0x4dc      */
  volatile u_int32_t LC_CHNASS0_SUB3;             /*      0x4dc - 0x4e0      */
  volatile u_int32_t LC_CHNASS1_SUB3;             /*      0x4e0 - 0x4e4      */
  volatile u_int32_t LC_CHNASS0_SUB4;             /*      0x4e4 - 0x4e8      */
  volatile u_int32_t LC_CHNASS1_SUB4;             /*      0x4e8 - 0x4ec      */
  volatile u_int32_t LC_CHNASS0_SUB5;             /*      0x4ec - 0x4f0      */
  volatile u_int32_t LC_CHNASS1_SUB5;             /*      0x4f0 - 0x4f4      */
  volatile u_int32_t LC_CHNASS0_SUB6;             /*      0x4f4 - 0x4f8      */
  volatile u_int32_t LC_CHNASS1_SUB6;             /*      0x4f8 - 0x4fc      */
  volatile u_int32_t LC_CHNASS0_SUB7;             /*      0x4fc - 0x500      */
  volatile u_int32_t LC_CHNASS1_SUB7;             /*      0x500 - 0x504      */
  volatile u_int32_t LC_LE;                       /*      0x504 - 0x508      */
  volatile u_int32_t MCI_SCHD_TABLE_2;            /*      0x508 - 0x50c      */
  volatile u_int32_t WB_BTCLK_SYNC_LE_PN0;        /*      0x50c - 0x510      */
  volatile u_int32_t WB_BTCLK_SYNC_LE_PN1;        /*      0x510 - 0x514      */
  volatile u_int32_t LC_TB_LLR;                   /*      0x514 - 0x518      */
  volatile u_int32_t LC_SYM_TIME0_FREE_RUN;       /*      0x518 - 0x51c      */
  volatile u_int32_t LC_SYM_TIME1_FREE_RUN;       /*      0x51c - 0x520      */
  volatile u_int32_t LC_SYM_TIME2_FREE_RUN;       /*      0x520 - 0x524      */
  volatile u_int32_t LC_SYM_TIME3_FREE_RUN;       /*      0x524 - 0x528      */
  volatile u_int32_t WBTIMERCLK;                  /*      0x528 - 0x52c      */
};

struct jupiter_reg_map__synthBT_reg_csr {
  volatile u_int32_t SYNTHBT1;                    /*        0x0 - 0x4        */
  volatile u_int32_t SYNTHBT2;                    /*        0x4 - 0x8        */
  volatile u_int32_t SYNTHBT3;                    /*        0x8 - 0xc        */
  volatile u_int32_t SYNTHBT4;                    /*        0xc - 0x10       */
  volatile u_int32_t SYNTHBT5;                    /*       0x10 - 0x14       */
  volatile u_int32_t SYNTHBT6;                    /*       0x14 - 0x18       */
  volatile u_int32_t SYNTHBT7;                    /*       0x18 - 0x1c       */
  volatile u_int32_t SYNTHBT8;                    /*       0x1c - 0x20       */
};

struct jupiter_reg_map__BIASBT_reg_csr {
  volatile u_int32_t BIASBT1;                     /*        0x0 - 0x4        */
  volatile u_int32_t BIASBT2;                     /*        0x4 - 0x8        */
  volatile u_int32_t BIASBT3;                     /*        0x8 - 0xc        */
  volatile u_int32_t BIASBT4;                     /*        0xc - 0x10       */
  volatile u_int32_t BIASBT5;                     /*       0x10 - 0x14       */
};

struct jupiter_reg_map__TOPBT_reg_csr {
  volatile u_int32_t TOPBT1;                      /*        0x0 - 0x4        */
  volatile u_int32_t TOPBT2;                      /*        0x4 - 0x8        */
  volatile u_int32_t TOPBT3;                      /*        0x8 - 0xc        */
  volatile u_int32_t TOPBT4;                      /*        0xc - 0x10       */
  volatile u_int32_t TOPBT5;                      /*       0x10 - 0x14       */
  volatile u_int32_t TOPBT6;                      /*       0x14 - 0x18       */
  volatile u_int32_t TOPBT7;                      /*       0x18 - 0x1c       */
  volatile u_int32_t TOPBT8;                      /*       0x1c - 0x20       */
  volatile u_int32_t TOPBT9;                      /*       0x20 - 0x24       */
  volatile u_int32_t TOPBT10;                     /*       0x24 - 0x28       */
};

struct jupiter_reg_map__CLK_reg_csr {
  volatile u_int32_t CLK1;                        /*        0x0 - 0x4        */
  volatile u_int32_t CLK2;                        /*        0x4 - 0x8        */
  volatile u_int32_t CLK3;                        /*        0x8 - 0xc        */
};

struct jupiter_reg_map__analog_intf_athr_wlan_reg_csr {
  volatile char pad__0[0x880];                    /*        0x0 - 0x880      */
  struct jupiter_reg_map__synthBT_reg_csr synth_reg_map;
                                                  /*      0x880 - 0x8a0      */
  volatile char pad__1[0x20];                     /*      0x8a0 - 0x8c0      */
  struct jupiter_reg_map__BIASBT_reg_csr BIAS_reg_map;
                                                  /*      0x8c0 - 0x8d4      */
  volatile char pad__2[0x2c];                     /*      0x8d4 - 0x900      */
  struct jupiter_reg_map__TOPBT_reg_csr TOP_reg_map;
                                                  /*      0x900 - 0x928      */
  volatile char pad__3[0x158];                    /*      0x928 - 0xa80      */
  struct jupiter_reg_map__CLK_reg_csr CLK_reg_map;
                                                  /*      0xa80 - 0xa8c      */
};

struct jupiter_reg_map__efuse_reg {
  volatile u_int32_t OTP_MEM[128];                /*        0x0 - 0x200      */
  volatile char pad__0[0x1d00];                   /*      0x200 - 0x1f00     */
  volatile u_int32_t OTP_INTF0;                   /*     0x1f00 - 0x1f04     */
  volatile u_int32_t OTP_INTF1;                   /*     0x1f04 - 0x1f08     */
  volatile u_int32_t OTP_INTF2;                   /*     0x1f08 - 0x1f0c     */
  volatile u_int32_t OTP_INTF3;                   /*     0x1f0c - 0x1f10     */
  volatile u_int32_t OTP_INTF4;                   /*     0x1f10 - 0x1f14     */
  volatile u_int32_t OTP_INTF5;                   /*     0x1f14 - 0x1f18     */
  volatile u_int32_t OTP_STATUS0;                 /*     0x1f18 - 0x1f1c     */
  volatile u_int32_t OTP_STATUS1;                 /*     0x1f1c - 0x1f20     */
  volatile u_int32_t OTP_INTF6;                   /*     0x1f20 - 0x1f24     */
  volatile u_int32_t OTP_LDO_CONTROL;             /*     0x1f24 - 0x1f28     */
  volatile u_int32_t OTP_LDO_POWER_GOOD;          /*     0x1f28 - 0x1f2c     */
  volatile u_int32_t OTP_LDO_STATUS;              /*     0x1f2c - 0x1f30     */
  volatile u_int32_t OTP_VDDQ_HOLD_TIME;          /*     0x1f30 - 0x1f34     */
  volatile u_int32_t OTP_PGENB_SETUP_HOLD_TIME;   /*     0x1f34 - 0x1f38     */
  volatile u_int32_t OTP_STROBE_PULSE_INTERVAL;   /*     0x1f38 - 0x1f3c     */
  volatile u_int32_t OTP_CSB_ADDR_LOAD_SETUP_HOLD;
                                                  /*     0x1f3c - 0x1f40     */
};

struct jupiter_reg_map__modem_reg_csr {
  volatile u_int32_t START_REG;                   /*        0x0 - 0x4        */
  volatile u_int32_t RX_STATUS;                   /*        0x4 - 0x8        */
  volatile u_int32_t AC1_L;                       /*        0x8 - 0xc        */
  volatile u_int32_t AC1_U;                       /*        0xc - 0x10       */
  volatile u_int32_t AC2_L;                       /*       0x10 - 0x14       */
  volatile u_int32_t AC2_U;                       /*       0x14 - 0x18       */
  volatile u_int32_t TX_LATE;                     /*       0x18 - 0x1c       */
  volatile u_int32_t RF_SYNTH;                    /*       0x1c - 0x20       */
  volatile u_int32_t RF_RX_CONTROL;               /*       0x20 - 0x24       */
  volatile u_int32_t RF_TX_CONTROL;               /*       0x24 - 0x28       */
  volatile u_int32_t RF_FORCE;                    /*       0x28 - 0x2c       */
  volatile u_int32_t MODEM_CONTROL;               /*       0x2c - 0x30       */
  volatile u_int32_t DC_FREQ_TRACK;               /*       0x30 - 0x34       */
  volatile u_int32_t PSK_TRACK;                   /*       0x34 - 0x38       */
  volatile u_int32_t PSK_TRACK2;                  /*       0x38 - 0x3c       */
  volatile u_int32_t DEMOD_CTRL1;                 /*       0x3c - 0x40       */
  volatile u_int32_t DEMOD_CTRL2;                 /*       0x40 - 0x44       */
  volatile u_int32_t CORR_PARAM1;                 /*       0x44 - 0x48       */
  volatile u_int32_t CORR_PARAM2;                 /*       0x48 - 0x4c       */
  volatile u_int32_t RX_LFDATA;                   /*       0x4c - 0x50       */
  volatile u_int32_t ROT;                         /*       0x50 - 0x54       */
  volatile u_int32_t TX;                          /*       0x54 - 0x58       */
  volatile u_int32_t TX_GFSK1;                    /*       0x58 - 0x5c       */
  volatile u_int32_t TX_GFSK2;                    /*       0x5c - 0x60       */
  volatile u_int32_t TX_POWER_CORR0;              /*       0x60 - 0x64       */
  volatile u_int32_t TX_POWER_CORR1;              /*       0x64 - 0x68       */
  volatile u_int32_t SYNTH_CHN0;                  /*       0x68 - 0x6c       */
  volatile u_int32_t SYNTH_OFFSET;                /*       0x6c - 0x70       */
  volatile u_int32_t MODEM_DEBUG;                 /*       0x70 - 0x74       */
  volatile u_int32_t AGC_BYPASS;                  /*       0x74 - 0x78       */
  volatile u_int32_t AGC_SAT;                     /*       0x78 - 0x7c       */
  volatile u_int32_t AGC_DET1;                    /*       0x7c - 0x80       */
  volatile u_int32_t AGC_DET2;                    /*       0x80 - 0x84       */
  volatile u_int32_t AGC_GAIN1;                   /*       0x84 - 0x88       */
  volatile u_int32_t AGC_GAIN2;                   /*       0x88 - 0x8c       */
  volatile u_int32_t AGC_LINEAR_BLK;              /*       0x8c - 0x90       */
  volatile u_int32_t AGC_NONLIN_BLK;              /*       0x90 - 0x94       */
  volatile u_int32_t AGC_MIN_POWER;               /*       0x94 - 0x98       */
  volatile u_int32_t AGC_SLNA_SET0;               /*       0x98 - 0x9c       */
  volatile u_int32_t AGC_SLNA_SET1;               /*       0x9c - 0xa0       */
  volatile u_int32_t AGC_SLNA_SET2;               /*       0xa0 - 0xa4       */
  volatile u_int32_t AGC_SLNA_SET3;               /*       0xa4 - 0xa8       */
  volatile u_int32_t AGC_GAIN1_LEAN;              /*       0xa8 - 0xac       */
  volatile u_int32_t MODEM_CTRL;                  /*       0xac - 0xb0       */
  volatile u_int32_t DEMOD_CTRL3;                 /*       0xb0 - 0xb4       */
  volatile u_int32_t DEMOD_CTRL4;                 /*       0xb4 - 0xb8       */
  volatile u_int32_t TX_GFSK3;                    /*       0xb8 - 0xbc       */
  volatile u_int32_t LE_DEMOD;                    /*       0xbc - 0xc0       */
  volatile u_int32_t AGC_LE1;                     /*       0xc0 - 0xc4       */
  volatile u_int32_t AGC_LE2;                     /*       0xc4 - 0xc8       */
  volatile u_int32_t AGC_LE3;                     /*       0xc8 - 0xcc       */
  volatile u_int32_t AGC_LE4;                     /*       0xcc - 0xd0       */
  volatile u_int32_t AGC_LE5;                     /*       0xd0 - 0xd4       */
  volatile u_int32_t AGC_LE6;                     /*       0xd4 - 0xd8       */
  volatile u_int32_t LE_FREQ;                     /*       0xd8 - 0xdc       */
  volatile u_int32_t LE_BLOCKER;                  /*       0xdc - 0xe0       */
  volatile char pad__0[0x420];                    /*       0xe0 - 0x500      */
  volatile u_int32_t AGC_GAIN_TABLE[128];         /*      0x500 - 0x700      */
  volatile u_int32_t TX_ULP_CNTRL;                /*      0x700 - 0x704      */
  volatile u_int32_t SS_MANUAL1;                  /*      0x704 - 0x708      */
  volatile u_int32_t SS_MANUAL2;                  /*      0x708 - 0x70c      */
  volatile u_int32_t SS_RADIO_CTRL;               /*      0x70c - 0x710      */
  volatile u_int32_t PHY_ERR_CTRL1;               /*      0x710 - 0x714      */
  volatile u_int32_t PHY_ERR_CTRL2;               /*      0x714 - 0x718      */
  volatile u_int32_t PHY_ERR_CTRL3;               /*      0x718 - 0x71c      */
  volatile u_int32_t PHY_ERR_CTRL4;               /*      0x71c - 0x720      */
  volatile u_int32_t PHY_ERR_STATUS;              /*      0x720 - 0x724      */
  volatile u_int32_t RBIST_ENABLE_CONTROL;        /*      0x724 - 0x728      */
  volatile u_int32_t RBIST_TX_DC;                 /*      0x728 - 0x72c      */
  volatile u_int32_t RBIST_TX_TONE0;              /*      0x72c - 0x730      */
  volatile u_int32_t RBIST_TX_TONE1;              /*      0x730 - 0x734      */
  volatile u_int32_t RBIST_TX_TONE2;              /*      0x734 - 0x738      */
  volatile u_int32_t RBIST_TX_RAMP_I;             /*      0x738 - 0x73c      */
  volatile u_int32_t RBIST_TX_RAMP_Q;             /*      0x73c - 0x740      */
  volatile u_int32_t RBIST_TX_PRBS_MAG;           /*      0x740 - 0x744      */
  volatile u_int32_t RBIST_TX_PRBS_SEED_I;        /*      0x744 - 0x748      */
  volatile u_int32_t RBIST_TX_PRBS_SEED_Q;        /*      0x748 - 0x74c      */
  volatile u_int32_t RBIST_RX_DC_OFFSET;          /*      0x74c - 0x750      */
  volatile u_int32_t RBIST_RX_DC_OFFSET_CANCEL;   /*      0x750 - 0x754      */
  volatile u_int32_t RBIST_RX_DFT;                /*      0x754 - 0x758      */
  volatile u_int32_t RBIST_RX_POWER;              /*      0x758 - 0x75c      */
  volatile u_int32_t RBIST_RX_IQ;                 /*      0x75c - 0x760      */
  volatile u_int32_t RBIST_RX_I2Q2;               /*      0x760 - 0x764      */
  volatile u_int32_t RBIST_RX_HPF;                /*      0x764 - 0x768      */
  volatile u_int32_t RBIST_RX_RESULT_Q;           /*      0x768 - 0x76c      */
  volatile u_int32_t RBIST_RX_RESULT_I;           /*      0x76c - 0x770      */
  volatile u_int32_t CAL_EN;                      /*      0x770 - 0x774      */
  volatile u_int32_t CAL_CONFIG;                  /*      0x774 - 0x778      */
  volatile u_int32_t PASSIVE_RXIQ;                /*      0x778 - 0x77c      */
  volatile u_int32_t TX_CORR1;                    /*      0x77c - 0x780      */
  volatile u_int32_t TX_CORR2;                    /*      0x780 - 0x784      */
  volatile u_int32_t TX_CORR3;                    /*      0x784 - 0x788      */
  volatile u_int32_t TX_CORR4;                    /*      0x788 - 0x78c      */
  volatile u_int32_t RX_IQCORR_0;                 /*      0x78c - 0x790      */
  volatile u_int32_t RX_IQCORR_1;                 /*      0x790 - 0x794      */
  volatile u_int32_t RX_IQCORR_2;                 /*      0x794 - 0x798      */
  volatile u_int32_t RX_IQCORR_3;                 /*      0x798 - 0x79c      */
  volatile u_int32_t CAL_MEAS_I2_L;               /*      0x79c - 0x7a0      */
  volatile u_int32_t CAL_MEAS_I2_U;               /*      0x7a0 - 0x7a4      */
  volatile u_int32_t CAL_MEAS_IQ_L;               /*      0x7a4 - 0x7a8      */
  volatile u_int32_t CAL_MEAS_IQ_U;               /*      0x7a8 - 0x7ac      */
  volatile u_int32_t CAL_MEAS_Q2_L;               /*      0x7ac - 0x7b0      */
  volatile u_int32_t CAL_MEAS_Q2_U;               /*      0x7b0 - 0x7b4      */
  volatile u_int32_t CAP_SFT_DEBUG;               /*      0x7b4 - 0x7b8      */
  volatile u_int32_t RX_NOTCH_0;                  /*      0x7b8 - 0x7bc      */
  volatile u_int32_t RX_NOTCH_1;                  /*      0x7bc - 0x7c0      */
  volatile u_int32_t RX_NOTCH_2;                  /*      0x7c0 - 0x7c4      */
  volatile u_int32_t RX_NOTCH_INDEX_0;            /*      0x7c4 - 0x7c8      */
  volatile u_int32_t RX_NOTCH_INDEX_1;            /*      0x7c8 - 0x7cc      */
  volatile u_int32_t RX_NOTCH_INDEX_2;            /*      0x7cc - 0x7d0      */
  volatile u_int32_t RX_NOTCH_INDEX_3;            /*      0x7d0 - 0x7d4      */
  volatile u_int32_t RX_NOTCH_INDEX_4;            /*      0x7d4 - 0x7d8      */
  volatile u_int32_t RX_NOTCH_INDEX_5;            /*      0x7d8 - 0x7dc      */
  volatile u_int32_t RX_NOTCH_INDEX_6;            /*      0x7dc - 0x7e0      */
  volatile u_int32_t RX_NOTCH_INDEX_7;            /*      0x7e0 - 0x7e4      */
  volatile u_int32_t RX_NOTCH_PARAMS_0;           /*      0x7e4 - 0x7e8      */
  volatile u_int32_t RX_NOTCH_PARAMS_1;           /*      0x7e8 - 0x7ec      */
  volatile u_int32_t RX_NOTCH_PARAMS_2;           /*      0x7ec - 0x7f0      */
  volatile u_int32_t RX_NOTCH_PARAMS_3;           /*      0x7f0 - 0x7f4      */
  volatile u_int32_t RX_NOTCH_PARAMS_4;           /*      0x7f4 - 0x7f8      */
  volatile u_int32_t RX_NOTCH_PARAMS_5;           /*      0x7f8 - 0x7fc      */
  volatile u_int32_t RX_NOTCH_PARAMS_6;           /*      0x7fc - 0x800      */
  volatile u_int32_t CHNASS_CTRL;                 /*      0x800 - 0x804      */
  volatile u_int32_t CHNASS_SETUP_0;              /*      0x804 - 0x808      */
  volatile u_int32_t CHNASS_SETUP_1;              /*      0x808 - 0x80c      */
  volatile u_int32_t CHNASS_SETUP_2;              /*      0x80c - 0x810      */
  volatile u_int32_t CHNASS_SETUP_3;              /*      0x810 - 0x814      */
  volatile u_int32_t CHNASS_RSSI_0;               /*      0x814 - 0x818      */
  volatile u_int32_t CHNASS_RSSI_1;               /*      0x818 - 0x81c      */
  volatile u_int32_t SW_CTRL;                     /*      0x81c - 0x820      */
  volatile u_int32_t JUPITER_CTRL;                /*      0x820 - 0x824      */
  volatile u_int32_t JUPITER_GAIN;                /*      0x824 - 0x828      */
  volatile u_int32_t AGC_HIST_SETUP;              /*      0x828 - 0x82c      */
  volatile u_int32_t AGC_HIST_BANK_0;             /*      0x82c - 0x830      */
  volatile u_int32_t AGC_HIST_BANK_1;             /*      0x830 - 0x834      */
  volatile u_int32_t AGC_HIST_BANK_2;             /*      0x834 - 0x838      */
  volatile u_int32_t AGC_HIST_BANK_3;             /*      0x838 - 0x83c      */
  volatile u_int32_t AGC_HIST_BANK_4;             /*      0x83c - 0x840      */
  volatile u_int32_t SPARE;                       /*      0x840 - 0x844      */
};

struct jupiter_reg_map__le_dma_reg_csr {
  volatile u_int32_t LE_DMA_MASTER;               /*        0x0 - 0x4        */
  volatile u_int32_t LE_DMA_TX_CONTROL;           /*        0x4 - 0x8        */
  volatile u_int32_t LE_DMA_RX_CONTROL;           /*        0x8 - 0xc        */
  volatile u_int32_t LE_DMA_TX_HW;                /*        0xc - 0x10       */
  volatile u_int32_t LE_DMA_RX_HW;                /*       0x10 - 0x14       */
  volatile u_int32_t LE_DMA_INT_STATUS;           /*       0x14 - 0x18       */
  volatile u_int32_t LE_DMA_TX_STATUS;            /*       0x18 - 0x1c       */
  volatile u_int32_t LE_DMA_TX_STATUS_W1TC;       /*       0x1c - 0x20       */
  volatile u_int32_t LE_DMA_TX_ENABLE;            /*       0x20 - 0x24       */
  volatile u_int32_t LE_DMA_RX_STATUS;            /*       0x24 - 0x28       */
  volatile u_int32_t LE_DMA_RX_STATUS_W1TC;       /*       0x28 - 0x2c       */
  volatile u_int32_t LE_DMA_RX_ENABLE;            /*       0x2c - 0x30       */
  volatile u_int32_t LE_DMA_DEBUG;                /*       0x30 - 0x34       */
  volatile u_int32_t LE_DMA_DUMMY;                /*       0x34 - 0x38       */
};

struct jupiter_reg_map__le_reg_csr {
  volatile u_int32_t LE_PUBLIC_ADDRESS_L;         /*        0x0 - 0x4        */
  volatile u_int32_t LE_PUBLIC_ADDRESS_U;         /*        0x4 - 0x8        */
  volatile u_int32_t LE_RANDOM_ADDRESS_L;         /*        0x8 - 0xc        */
  volatile u_int32_t LE_RANDOM_ADDRESS_U;         /*        0xc - 0x10       */
  volatile u_int32_t LE_DEV_PARAM;                /*       0x10 - 0x14       */
  volatile u_int32_t COMMAND1;                    /*       0x14 - 0x18       */
  volatile u_int32_t COMMAND2;                    /*       0x18 - 0x1c       */
  volatile u_int32_t COMMAND3;                    /*       0x1c - 0x20       */
  volatile u_int32_t COMMAND4;                    /*       0x20 - 0x24       */
  volatile u_int32_t COMMAND5;                    /*       0x24 - 0x28       */
  volatile u_int32_t COMMAND6;                    /*       0x28 - 0x2c       */
  volatile u_int32_t COMMAND7;                    /*       0x2c - 0x30       */
  volatile u_int32_t COMMAND8;                    /*       0x30 - 0x34       */
  volatile u_int32_t COMMAND9;                    /*       0x34 - 0x38       */
  volatile u_int32_t COMMAND10;                   /*       0x38 - 0x3c       */
  volatile u_int32_t COMMAND11;                   /*       0x3c - 0x40       */
  volatile u_int32_t COMMAND12;                   /*       0x40 - 0x44       */
  volatile u_int32_t COMMAND13;                   /*       0x44 - 0x48       */
  volatile u_int32_t LE_ABORT;                    /*       0x48 - 0x4c       */
  volatile u_int32_t LE_RX_STATUS1;               /*       0x4c - 0x50       */
  volatile u_int32_t LE_RX_STATUS2;               /*       0x50 - 0x54       */
  volatile u_int32_t LE_RX_STATUS3;               /*       0x54 - 0x58       */
  volatile u_int32_t LE_RX_STATUS4;               /*       0x58 - 0x5c       */
  volatile u_int32_t LE_RX_STATUS5;               /*       0x5c - 0x60       */
  volatile u_int32_t LE_RX_STATUS6;               /*       0x60 - 0x64       */
  volatile u_int32_t LE_RX_STATUS7;               /*       0x64 - 0x68       */
  volatile u_int32_t LE_RX_STATUS8;               /*       0x68 - 0x6c       */
  volatile u_int32_t LE_RX_STATUS9;               /*       0x6c - 0x70       */
  volatile u_int32_t LE_INTERRUPT_EN;             /*       0x70 - 0x74       */
  volatile u_int32_t LE_INTERRUPT;                /*       0x74 - 0x78       */
  volatile u_int32_t LE_DATAPATH_CNTL;            /*       0x78 - 0x7c       */
  volatile u_int32_t LE_BT_CLOCK0;                /*       0x7c - 0x80       */
  volatile u_int32_t LE_BT_CLOCK1;                /*       0x80 - 0x84       */
  volatile u_int32_t LE_SYM_TIME0;                /*       0x84 - 0x88       */
  volatile u_int32_t LE_SYM_TIME1;                /*       0x88 - 0x8c       */
  volatile u_int32_t LE_TIMER0;                   /*       0x8c - 0x90       */
  volatile u_int32_t LE_TIMER0_FRAME;             /*       0x90 - 0x94       */
  volatile u_int32_t LE_TIMER1;                   /*       0x94 - 0x98       */
  volatile u_int32_t LE_TIMER1_FRAME;             /*       0x98 - 0x9c       */
  volatile u_int32_t LE_WL_TABLE[256];            /*       0x9c - 0x49c      */
  volatile u_int32_t LE_TIM;                      /*      0x49c - 0x4a0      */
  volatile u_int32_t TX_ERROR_GENERATION;         /*      0x4a0 - 0x4a4      */
  volatile u_int32_t LE_FREQ_MAP0;                /*      0x4a4 - 0x4a8      */
  volatile u_int32_t LE_FREQ_MAP1;                /*      0x4a8 - 0x4ac      */
  volatile u_int32_t LE_FREQ_MAP2;                /*      0x4ac - 0x4b0      */
  volatile u_int32_t LE_FREQ_MAP3;                /*      0x4b0 - 0x4b4      */
  volatile u_int32_t LE_FREQ_MAP4;                /*      0x4b4 - 0x4b8      */
  volatile u_int32_t LE_FREQ_MAP5;                /*      0x4b8 - 0x4bc      */
  volatile u_int32_t LE_FREQ_MAP6;                /*      0x4bc - 0x4c0      */
  volatile u_int32_t LE_FREQ_MAP7;                /*      0x4c0 - 0x4c4      */
  volatile u_int32_t LE_FREQ_MAP8;                /*      0x4c4 - 0x4c8      */
  volatile u_int32_t LE_FREQ_MAP9;                /*      0x4c8 - 0x4cc      */
  volatile u_int32_t LE_DEBUG_CTRL;               /*      0x4cc - 0x4d0      */
  volatile u_int32_t LE_DEBUG_OBS;                /*      0x4d0 - 0x4d4      */
  volatile u_int32_t LE_PHY_ERR;                  /*      0x4d4 - 0x4d8      */
  volatile u_int32_t LE_PHY_ERR_0;                /*      0x4d8 - 0x4dc      */
  volatile u_int32_t LE_PHY_ERR_1;                /*      0x4dc - 0x4e0      */
  volatile u_int32_t LE_PHY_ERR_2;                /*      0x4e0 - 0x4e4      */
  volatile u_int32_t LE_PHY_ERR_3;                /*      0x4e4 - 0x4e8      */
  volatile u_int32_t LE_PHY_ERR_4;                /*      0x4e8 - 0x4ec      */
  volatile u_int32_t LE_DUMMY;                    /*      0x4ec - 0x4f0      */
};

struct jupiter_reg_map__apb_map_csr {
  volatile char pad__0[0x4000];                   /*        0x0 - 0x4000     */
  struct jupiter_reg_map__rtc_reg_csr rtc;        /*     0x4000 - 0x4134     */
  volatile char pad__1[0x3ecc];                   /*     0x4134 - 0x8000     */
  struct jupiter_reg_map__vmc_reg_csr vmc;        /*     0x8000 - 0x8620     */
  volatile char pad__2[0x39e0];                   /*     0x8620 - 0xc000     */
  struct jupiter_reg_map__apb_map_csr__uart_reg_csr uart;
                                                  /*     0xc000 - 0xc014     */
  volatile char pad__3[0x3fec];                   /*     0xc014 - 0x10000    */
  struct jupiter_reg_map__si_reg_csr si;          /*    0x10000 - 0x10018    */
  volatile char pad__4[0x3fe8];                   /*    0x10018 - 0x14000    */
  struct jupiter_reg_map__gpio_reg_csr gpio;      /*    0x14000 - 0x14098    */
  volatile char pad__5[0x3f68];                   /*    0x14098 - 0x18000    */
  struct jupiter_reg_map__mbox_reg_csr mbox;      /*    0x18000 - 0x1809c    */
  volatile char pad__6[0x3f64];                   /*    0x1809c - 0x1c000    */
  struct jupiter_reg_map__lc_dma_reg_csr lc_dma;  /*    0x1c000 - 0x1c034    */
  volatile char pad__7[0x3fcc];                   /*    0x1c034 - 0x20000    */
  struct jupiter_reg_map__lc_reg_csr lc;          /*    0x20000 - 0x2052c    */
  volatile char pad__8[0x2ad4];                   /*    0x2052c - 0x23000    */
  struct jupiter_reg_map__analog_intf_athr_wlan_reg_csr analog;
                                                  /*    0x23000 - 0x23a90    */
  volatile char pad__9[0x570];                    /*    0x23a90 - 0x24000    */
  struct jupiter_reg_map__efuse_reg efuse;        /*    0x24000 - 0x25f40    */
  volatile char pad__10[0xc0];                    /*    0x25f40 - 0x26000    */
  struct jupiter_reg_map__modem_reg_csr modem;    /*    0x26000 - 0x26844    */
  volatile char pad__11[0x37bc];                  /*    0x26844 - 0x2a000    */
  struct jupiter_reg_map__le_dma_reg_csr le_dma;  /*    0x2a000 - 0x2a038    */
  volatile char pad__12[0x1fc8];                  /*    0x2a038 - 0x2c000    */
  struct jupiter_reg_map__le_reg_csr le;          /*    0x2c000 - 0x2c4f0    */
};

struct bt_apb_reg {
  volatile char pad__0[0x40000];                  /*        0x0 - 0x40000    */
  struct jupiter_reg_map__apb_map_csr bt_apb_map_block;
                                                  /*    0x40000 - 0x6c800    */
};

struct osprey_reg_map {
  struct mac_dma_reg mac_dma_reg_block;           /*        0x0 - 0x108      */
  volatile char pad__0;                           /*      0x108 - 0x0        */
  struct mac_qcu_reg mac_qcu_reg_block;           /*        0x0 - 0x24c      */
  volatile char pad__1;                           /*      0x24c - 0x0        */
  struct mac_dcu_reg mac_dcu_reg_block;           /*        0x0 - 0x7fc      */
  volatile char pad__2;                           /*      0x7fc - 0x0        */
  struct host_intf_reg host_intf_reg_block;       /*        0x0 - 0xf4       */
  volatile char pad__3;                           /*       0xf4 - 0x0        */
  struct emulation_misc_regs emulation_misc_reg_block;
                                                  /*        0x0 - 0x30       */
  volatile char pad__4;                           /*      Osprey: 0x30 - 0x0        */
  struct DWC_pcie_dbi_axi DWC_pcie_dbi_axi_block; /*      Osprey: 0x0  - 0x818      */
  volatile char pad__5;                           /*      0x818 - 0x0        */
  struct rtc_reg rtc_reg_block;                   /*      Osprey: 0x0  - 0x3c, Poseidon: 0x0  - 0x40       */
  volatile char pad__6;                           /*      Osprey: 0x3c - 0x0,  Poseidon: 0x40 - 0x0        */
  struct rtc_sync_reg rtc_sync_reg_block;         /*        0x0 - 0x1c       */
  volatile char pad__7;                           /*       0x1c - 0x0        */
  struct merlin2_0_radio_reg_map merlin2_0_radio_reg_map;
                                                  /*        0x0 - 0x9c       */
  volatile char pad__8;                           /*       0x9c - 0x0        */
  struct analog_intf_reg_csr analog_intf_reg_csr_block;
                                                  /*        0x0 - 0x10       */
  volatile char pad__9;                           /*       0x10 - 0x0        */
  struct mac_pcu_reg mac_pcu_reg_block;           /*        0x0 - 0x8000     */
  volatile char pad__10;                          /*     0x8000 - 0x0        */
  struct bb_reg_map bb_reg_block;                 /*        0x0 - 0x4000     */
  volatile char pad__11;                          /*     0x4000 - 0x0        */
  struct svd_reg svd_reg_block;                   /*        0x0 - 0x2c00     */
  volatile char pad__12;                          /*     0x2c00 - 0x0        */
  struct efuse_reg_WLAN efuse_reg_block;          /*        0x0 - 0x1f40     */
  volatile char pad__13;                          /*     0x1f40 - 0x0        */
  struct radio65_reg radio65_reg_block;           /*     Osprey:   0x0 - 0xbd8, Poseidon: 0x0   - 0x3d8      */
  volatile char pad__14;                          /*     Osprey: 0xbd8 - 0x0,   Poseidon: 0x3d8 - 0x0        */
  struct pmu_reg pmu_reg_block;                   /*     Osprey:   0x0 - 0x8        */
  volatile char pad__15;                          /*     Osprey:   0x8 - 0x0        */
  struct pcie_phy_reg_csr pcie_phy_reg_block;     /*        0x0 - 0xc        */
  volatile char pad__16;                          /*        0xc - 0x0        */
  struct wlan_coex_reg wlan_coex_reg_block;       /*        0x0 - 0x264      */
  volatile char pad__17;                          /*      0x264 - 0x0        */
  struct wlan_bt_glb_reg_pcie glb_reg_block;      /*        0x0 - 0x400      */
  volatile char pad__18;                          /*      0x400 - 0x0        */
  struct bt_apb_reg bt_apb_reg_block;             /*     Jupiter:  0x0 - 0x2c800    */
};

#endif /* __REG_OSPREY_REG_MAP_H__ */
