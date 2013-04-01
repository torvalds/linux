/*
 * da7213.h - DA7213 ASoC Codec Driver Platform Data
 *
 * Copyright (c) 2013 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DA7213_PDATA_H
#define _DA7213_PDATA_H

enum da7213_micbias_voltage {
	DA7213_MICBIAS_1_6V = 0,
	DA7213_MICBIAS_2_2V = 1,
	DA7213_MICBIAS_2_5V = 2,
	DA7213_MICBIAS_3_0V = 3,
};

enum da7213_dmic_data_sel {
	DA7213_DMIC_DATA_LRISE_RFALL = 0,
	DA7213_DMIC_DATA_LFALL_RRISE = 1,
};

enum da7213_dmic_samplephase {
	DA7213_DMIC_SAMPLE_ON_CLKEDGE = 0,
	DA7213_DMIC_SAMPLE_BETWEEN_CLKEDGE = 1,
};

enum da7213_dmic_clk_rate {
	DA7213_DMIC_CLK_3_0MHZ = 0,
	DA7213_DMIC_CLK_1_5MHZ = 1,
};

struct da7213_platform_data {
	/* Mic Bias voltage */
	enum da7213_micbias_voltage micbias1_lvl;
	enum da7213_micbias_voltage micbias2_lvl;

	/* DMIC config */
	enum da7213_dmic_data_sel dmic_data_sel;
	enum da7213_dmic_samplephase dmic_samplephase;
	enum da7213_dmic_clk_rate dmic_clk_rate;

	/* MCLK squaring config */
	bool mclk_squaring;
};

#endif /* _DA7213_PDATA_H */
