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

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT8195 */
