/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_LPASS_CORE_CC_SC7180_H
#define _DT_BINDINGS_CLK_QCOM_LPASS_CORE_CC_SC7180_H

/* LPASS_CORE_CC clocks */
#define LPASS_LPAAUDIO_DIG_PLL				0
#define LPASS_LPAAUDIO_DIG_PLL_OUT_ODD			1
#define CORE_CLK_SRC					2
#define EXT_MCLK0_CLK_SRC				3
#define LPAIF_PRI_CLK_SRC				4
#define LPAIF_SEC_CLK_SRC				5
#define LPASS_AUDIO_CORE_CORE_CLK			6
#define LPASS_AUDIO_CORE_EXT_MCLK0_CLK			7
#define LPASS_AUDIO_CORE_LPAIF_PRI_IBIT_CLK		8
#define LPASS_AUDIO_CORE_LPAIF_SEC_IBIT_CLK		9
#define LPASS_AUDIO_CORE_SYSNOC_MPORT_CORE_CLK		10

/* LPASS Core power domains */
#define LPASS_CORE_HM_GDSCR				0

/* LPASS Audio power domains */
#define LPASS_AUDIO_HM_GDSCR				0
#define LPASS_PDC_HM_GDSCR				1

#endif
