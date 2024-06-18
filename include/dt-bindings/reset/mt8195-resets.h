/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)*/
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Christine Zhu <christine.zhu@mediatek.com>
 */

#ifndef _DT_BINDINGS_RESET_CONTROLLER_MT8195
#define _DT_BINDINGS_RESET_CONTROLLER_MT8195

/* TOPRGU resets */
#define MT8195_TOPRGU_CONN_MCU_SW_RST          0
#define MT8195_TOPRGU_INFRA_GRST_SW_RST        1
#define MT8195_TOPRGU_APU_SW_RST               2
#define MT8195_TOPRGU_INFRA_AO_GRST_SW_RST     6
#define MT8195_TOPRGU_MMSYS_SW_RST             7
#define MT8195_TOPRGU_MFG_SW_RST               8
#define MT8195_TOPRGU_VENC_SW_RST              9
#define MT8195_TOPRGU_VDEC_SW_RST              10
#define MT8195_TOPRGU_IMG_SW_RST               11
#define MT8195_TOPRGU_APMIXEDSYS_SW_RST        13
#define MT8195_TOPRGU_AUDIO_SW_RST             14
#define MT8195_TOPRGU_CAMSYS_SW_RST            15
#define MT8195_TOPRGU_EDPTX_SW_RST             16
#define MT8195_TOPRGU_ADSPSYS_SW_RST           21
#define MT8195_TOPRGU_DPTX_SW_RST              22
#define MT8195_TOPRGU_SPMI_MST_SW_RST          23

#define MT8195_TOPRGU_SW_RST_NUM               16

/* INFRA resets */
#define MT8195_INFRA_RST0_THERM_CTRL_SWRST     0
#define MT8195_INFRA_RST3_THERM_CTRL_PTP_SWRST 1
#define MT8195_INFRA_RST4_THERM_CTRL_MCU_SWRST 2
#define MT8195_INFRA_RST2_PCIE_P0_SWRST        3
#define MT8195_INFRA_RST2_PCIE_P1_SWRST        4
#define MT8195_INFRA_RST2_USBSIF_P1_SWRST      5

/* VDOSYS1 */
#define MT8195_VDOSYS1_SW0_RST_B_SMI_LARB2                     0
#define MT8195_VDOSYS1_SW0_RST_B_SMI_LARB3                     1
#define MT8195_VDOSYS1_SW0_RST_B_GALS                          2
#define MT8195_VDOSYS1_SW0_RST_B_FAKE_ENG0                     3
#define MT8195_VDOSYS1_SW0_RST_B_FAKE_ENG1                     4
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA0                     5
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA1                     6
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA2                     7
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA3                     8
#define MT8195_VDOSYS1_SW0_RST_B_VPP_MERGE0                    9
#define MT8195_VDOSYS1_SW0_RST_B_VPP_MERGE1                    10
#define MT8195_VDOSYS1_SW0_RST_B_VPP_MERGE2                    11
#define MT8195_VDOSYS1_SW0_RST_B_VPP_MERGE3                    12
#define MT8195_VDOSYS1_SW0_RST_B_VPP_MERGE4                    13
#define MT8195_VDOSYS1_SW0_RST_B_VPP2_TO_VDO1_DL_ASYNC         14
#define MT8195_VDOSYS1_SW0_RST_B_VPP3_TO_VDO1_DL_ASYNC         15
#define MT8195_VDOSYS1_SW0_RST_B_DISP_MUTEX                    16
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA4                     17
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA5                     18
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA6                     19
#define MT8195_VDOSYS1_SW0_RST_B_MDP_RDMA7                     20
#define MT8195_VDOSYS1_SW0_RST_B_DP_INTF0                      21
#define MT8195_VDOSYS1_SW0_RST_B_DPI0                          22
#define MT8195_VDOSYS1_SW0_RST_B_DPI1                          23
#define MT8195_VDOSYS1_SW0_RST_B_DISP_MONITOR                  24
#define MT8195_VDOSYS1_SW0_RST_B_MERGE0_DL_ASYNC               25
#define MT8195_VDOSYS1_SW0_RST_B_MERGE1_DL_ASYNC               26
#define MT8195_VDOSYS1_SW0_RST_B_MERGE2_DL_ASYNC               27
#define MT8195_VDOSYS1_SW0_RST_B_MERGE3_DL_ASYNC               28
#define MT8195_VDOSYS1_SW0_RST_B_MERGE4_DL_ASYNC               29
#define MT8195_VDOSYS1_SW0_RST_B_VDO0_DSC_TO_VDO1_DL_ASYNC     30
#define MT8195_VDOSYS1_SW0_RST_B_VDO0_MERGE_TO_VDO1_DL_ASYNC   31
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_FE0                   32
#define MT8195_VDOSYS1_SW1_RST_B_HDR_GFX_FE0                   33
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_BE                    34
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_FE1                   48
#define MT8195_VDOSYS1_SW1_RST_B_HDR_GFX_FE1                   49
#define MT8195_VDOSYS1_SW1_RST_B_DISP_MIXER                    50
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_FE0_DL_ASYNC          51
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_FE1_DL_ASYNC          52
#define MT8195_VDOSYS1_SW1_RST_B_HDR_GFX_FE0_DL_ASYNC          53
#define MT8195_VDOSYS1_SW1_RST_B_HDR_GFX_FE1_DL_ASYNC          54
#define MT8195_VDOSYS1_SW1_RST_B_HDR_VDO_BE_DL_ASYNC           55

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT8195 */
