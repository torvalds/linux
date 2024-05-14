/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2021 MediaTek Corporation. All rights reserved.
 *
 *  Header file for the mt8195 DSP clock  definition
 */

#ifndef __MT8195_CLK_H
#define __MT8195_CLK_H

struct snd_sof_dev;

/*DSP clock*/
enum adsp_clk_id {
	CLK_TOP_ADSP,
	CLK_TOP_CLK26M,
	CLK_TOP_AUDIO_LOCAL_BUS,
	CLK_TOP_MAINPLL_D7_D2,
	CLK_SCP_ADSP_AUDIODSP,
	CLK_TOP_AUDIO_H,
	ADSP_CLK_MAX
};

int mt8195_adsp_init_clock(struct snd_sof_dev *sdev);
int adsp_clock_on(struct snd_sof_dev *sdev);
int adsp_clock_off(struct snd_sof_dev *sdev);
#endif
