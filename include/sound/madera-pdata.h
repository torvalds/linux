/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform data for Madera codec driver
 *
 * Copyright (C) 2016-2019 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#ifndef MADERA_CODEC_PDATA_H
#define MADERA_CODEC_PDATA_H

#include <linux/kernel.h>

#define MADERA_MAX_INPUT		6
#define MADERA_MAX_MUXED_CHANNELS	4
#define MADERA_MAX_OUTPUT		6
#define MADERA_MAX_AIF			4
#define MADERA_MAX_PDM_SPK		2
#define MADERA_MAX_DSP			7

/**
 * struct madera_codec_pdata
 *
 * @max_channels_clocked: Maximum number of channels that I2S clocks will be
 *			  generated for. Useful when clock master for systems
 *			  where the I2S bus has multiple data lines.
 * @dmic_ref:		  Indicates how the MICBIAS pins have been externally
 *			  connected to DMICs on each input. A value of 0
 *			  indicates MICVDD and is the default. Other values are:
 *			  For CS47L35 one of the CS47L35_DMIC_REF_xxx values
 *			  For all other codecs one of the MADERA_DMIC_REF_xxx
 *			  Also see the datasheet for a description of the
 *			  INn_DMIC_SUP field.
 * @inmode:		  Mode for the ADC inputs. One of the MADERA_INMODE_xxx
 *			  values. Two-dimensional array
 *			  [input_number][channel number], with four slots per
 *			  input in the order
 *			  [n][0]=INnAL [n][1]=INnAR [n][2]=INnBL [n][3]=INnBR
 * @out_mono:		  For each output set the value to TRUE to indicate that
 *			  the output is mono. [0]=OUT1, [1]=OUT2, ...
 * @pdm_fmt:		  PDM speaker data format. See the PDM_SPKn_FMT field in
 *			  the datasheet for a description of this value.
 * @pdm_mute:		  PDM mute format. See the PDM_SPKn_CTRL_1 register
 *			  in the datasheet for a description of this value.
 */
struct madera_codec_pdata {
	u32 max_channels_clocked[MADERA_MAX_AIF];

	u32 dmic_ref[MADERA_MAX_INPUT];

	u32 inmode[MADERA_MAX_INPUT][MADERA_MAX_MUXED_CHANNELS];

	bool out_mono[MADERA_MAX_OUTPUT];

	u32 pdm_fmt[MADERA_MAX_PDM_SPK];
	u32 pdm_mute[MADERA_MAX_PDM_SPK];
};

#endif
