/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * da7218.h - DA7218 ASoC Codec Driver Platform Data
 *
 * Copyright (c) 2015 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#ifndef _DA7218_PDATA_H
#define _DA7218_PDATA_H

/* Mic Bias */
enum da7218_micbias_voltage {
	DA7218_MICBIAS_1_2V = -1,
	DA7218_MICBIAS_1_6V,
	DA7218_MICBIAS_1_8V,
	DA7218_MICBIAS_2_0V,
	DA7218_MICBIAS_2_2V,
	DA7218_MICBIAS_2_4V,
	DA7218_MICBIAS_2_6V,
	DA7218_MICBIAS_2_8V,
	DA7218_MICBIAS_3_0V,
};

enum da7218_mic_amp_in_sel {
	DA7218_MIC_AMP_IN_SEL_DIFF = 0,
	DA7218_MIC_AMP_IN_SEL_SE_P,
	DA7218_MIC_AMP_IN_SEL_SE_N,
};

/* DMIC */
enum da7218_dmic_data_sel {
	DA7218_DMIC_DATA_LRISE_RFALL = 0,
	DA7218_DMIC_DATA_LFALL_RRISE,
};

enum da7218_dmic_samplephase {
	DA7218_DMIC_SAMPLE_ON_CLKEDGE = 0,
	DA7218_DMIC_SAMPLE_BETWEEN_CLKEDGE,
};

enum da7218_dmic_clk_rate {
	DA7218_DMIC_CLK_3_0MHZ = 0,
	DA7218_DMIC_CLK_1_5MHZ,
};

/* Headphone Detect */
enum da7218_hpldet_jack_rate {
	DA7218_HPLDET_JACK_RATE_5US = 0,
	DA7218_HPLDET_JACK_RATE_10US,
	DA7218_HPLDET_JACK_RATE_20US,
	DA7218_HPLDET_JACK_RATE_40US,
	DA7218_HPLDET_JACK_RATE_80US,
	DA7218_HPLDET_JACK_RATE_160US,
	DA7218_HPLDET_JACK_RATE_320US,
	DA7218_HPLDET_JACK_RATE_640US,
};

enum da7218_hpldet_jack_debounce {
	DA7218_HPLDET_JACK_DEBOUNCE_OFF = 0,
	DA7218_HPLDET_JACK_DEBOUNCE_2,
	DA7218_HPLDET_JACK_DEBOUNCE_3,
	DA7218_HPLDET_JACK_DEBOUNCE_4,
};

enum da7218_hpldet_jack_thr {
	DA7218_HPLDET_JACK_THR_84PCT = 0,
	DA7218_HPLDET_JACK_THR_88PCT,
	DA7218_HPLDET_JACK_THR_92PCT,
	DA7218_HPLDET_JACK_THR_96PCT,
};

struct da7218_hpldet_pdata {
	enum da7218_hpldet_jack_rate jack_rate;
	enum da7218_hpldet_jack_debounce jack_debounce;
	enum da7218_hpldet_jack_thr jack_thr;
	bool comp_inv;
	bool hyst;
	bool discharge;
};

struct da7218_pdata {
	/* Mic */
	enum da7218_micbias_voltage micbias1_lvl;
	enum da7218_micbias_voltage micbias2_lvl;
	enum da7218_mic_amp_in_sel mic1_amp_in_sel;
	enum da7218_mic_amp_in_sel mic2_amp_in_sel;

	/* DMIC */
	enum da7218_dmic_data_sel dmic1_data_sel;
	enum da7218_dmic_data_sel dmic2_data_sel;
	enum da7218_dmic_samplephase dmic1_samplephase;
	enum da7218_dmic_samplephase dmic2_samplephase;
	enum da7218_dmic_clk_rate dmic1_clk_rate;
	enum da7218_dmic_clk_rate dmic2_clk_rate;

	/* HP Diff Supply - DA7217 only */
	bool hp_diff_single_supply;

	/* HP Detect - DA7218 only */
	struct da7218_hpldet_pdata *hpldet_pdata;
};

#endif /* _DA7218_PDATA_H */
