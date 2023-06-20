/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

/*
 * Copyright (c) 2022 MediaTek Corporation. All rights reserved.
 *
 *  Header file for the mt8186 DSP clock definition
 */

#ifndef __MT8186_CLK_H
#define __MT8186_CLK_H

struct snd_sof_dev;

/* DSP clock */
enum adsp_clk_id {
	CLK_TOP_AUDIODSP,
	CLK_TOP_ADSP_BUS,
	ADSP_CLK_MAX
};

int mt8186_adsp_init_clock(struct snd_sof_dev *sdev);
int mt8186_adsp_clock_on(struct snd_sof_dev *sdev);
void mt8186_adsp_clock_off(struct snd_sof_dev *sdev);
#endif
