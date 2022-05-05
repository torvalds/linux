/* SPDX-License-Identifier: GPL-2.0
 *
 * PDM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef __SND_SOC_STARFIVE_PDM_H__
#define __SND_SOC_STARFIVE_PDM_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <linux/dmaengine.h>
#include <linux/types.h>

#define PDM_DMIC_CTRL0		0x00
#define PDM_DC_SCALE0		0x04
#define PDM_DMIC_CTRL1		0x10
#define PDM_DC_SCALE1		0x14

/* PDM CTRL OFFSET */
#define PDM_DMIC_MSB_SHIFT_OFFSET	1
#define PDM_DMIC_VOL_OFFSET		16
#define PDM_DMIC_RVOL_OFFSET		22
#define PDM_DMIC_LVOL_OFFSET		23
#define PDM_DMIC_I2SMODE_OFFSET		24
#define PDM_DMIC_ENHPF_OFFSET		28
#define PDM_DMIC_FASTMODE_OFFSET	29
#define PDM_DMIC_DCBPS_OFFSET		30
#define PDM_DMIC_SW_RSTN_OFFSET		31

/* PDM SCALE OFFSET */
#define PDM_DMIC_DCOFF3_OFFSET		24
#define PDM_DMIC_DCOFF2_OFFSET		16
#define PDM_DMIC_DCOFF1_OFFSET		8
#define PDM_DMIC_SCALE_OFFSET		0

#define AUDIO_CLK_ADC_MCLK	0x0
#define AUDIO_CLK_I2SADC_BCLK	0xC
#define AUDIO_CLK_ADC_LRCLK	0x14
#define AUDIO_CLK_PDM_CLK	0x1C

#endif	/* __SND_SOC_STARFIVE_PDM_H__ */
