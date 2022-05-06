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

#define PDM_DMIC_CTRL0			0x00
#define PDM_DC_SCALE0			0x04
#define PDM_DMIC_CTRL1			0x10
#define PDM_DC_SCALE1			0x14

/* PDM CTRL OFFSET */
#define PDM_DMIC_MSB_SHIFT		1
#define PDM_DMIC_MSB_MASK		(0x7 << PDM_DMIC_MSB_SHIFT)
#define PDM_DMIC_VOL_SHIFT		16
#define PDM_DMIC_VOL_MASK		(0x3f << PDM_DMIC_VOL_SHIFT)
#define PDM_DMIC_RVOL_MASK		BIT(22)
#define PDM_DMIC_LVOL_MASK		BIT(23)
#define PDM_DMIC_I2S_SLAVE		BIT(24)
#define PDM_DMIC_HPF_EN			BIT(28)
#define PDM_DMIC_FASTMODE_MASK		BIT(29)
#define PDM_DMIC_DC_BYPASS_MASK		BIT(30)
#define PDM_DMIC_SW_RSTN_MASK		BIT(31)

/* PDM SCALE OFFSET */
#define PDM_DMIC_DCOFF3_SHIFT		24
#define PDM_DMIC_DCOFF2_SHIFT		16
#define PDM_DMIC_DCOFF1_SHIFT		8
#define PDM_DMIC_DCOFF1_MASK		(0xfffff << PDM_DMIC_DCOFF1_SHIFT)
#define PDM_DMIC_DCOFF1_EN		(0xc0005 << PDM_DMIC_DCOFF1_SHIFT)
#define PDM_DC_SCALE0_MASK		0x3f

#define AUDIO_CLK_ADC_MCLK		0x0
#define AUDIO_CLK_I2SADC_BCLK		0xC
#define AUDIO_CLK_ADC_LRCLK		0x14
#define AUDIO_CLK_PDM_CLK		0x1C

#endif	/* __SND_SOC_STARFIVE_PDM_H__ */
