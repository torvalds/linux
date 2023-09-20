/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * da7219-aad.h - DA7322 ASoC Codec AAD Driver Platform Data
 *
 * Copyright (c) 2015 Dialog Semiconductor Ltd.
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#ifndef __DA7219_AAD_PDATA_H
#define __DA7219_AAD_PDATA_H

enum da7219_aad_micbias_pulse_lvl {
	DA7219_AAD_MICBIAS_PULSE_LVL_OFF = 0,
	DA7219_AAD_MICBIAS_PULSE_LVL_2_8V = 6,
	DA7219_AAD_MICBIAS_PULSE_LVL_2_9V,
};

enum da7219_aad_btn_cfg {
	DA7219_AAD_BTN_CFG_2MS = 1,
	DA7219_AAD_BTN_CFG_5MS,
	DA7219_AAD_BTN_CFG_10MS,
	DA7219_AAD_BTN_CFG_50MS,
	DA7219_AAD_BTN_CFG_100MS,
	DA7219_AAD_BTN_CFG_200MS,
	DA7219_AAD_BTN_CFG_500MS,
};

enum da7219_aad_mic_det_thr {
	DA7219_AAD_MIC_DET_THR_200_OHMS = 0,
	DA7219_AAD_MIC_DET_THR_500_OHMS,
	DA7219_AAD_MIC_DET_THR_750_OHMS,
	DA7219_AAD_MIC_DET_THR_1000_OHMS,
};

enum da7219_aad_jack_ins_deb {
	DA7219_AAD_JACK_INS_DEB_5MS = 0,
	DA7219_AAD_JACK_INS_DEB_10MS,
	DA7219_AAD_JACK_INS_DEB_20MS,
	DA7219_AAD_JACK_INS_DEB_50MS,
	DA7219_AAD_JACK_INS_DEB_100MS,
	DA7219_AAD_JACK_INS_DEB_200MS,
	DA7219_AAD_JACK_INS_DEB_500MS,
	DA7219_AAD_JACK_INS_DEB_1S,
};

enum da7219_aad_jack_ins_det_pty {
	DA7219_AAD_JACK_INS_DET_PTY_LOW = 0,
	DA7219_AAD_JACK_INS_DET_PTY_HIGH,
};

enum da7219_aad_jack_det_rate {
	DA7219_AAD_JACK_DET_RATE_32_64MS = 0,
	DA7219_AAD_JACK_DET_RATE_64_128MS,
	DA7219_AAD_JACK_DET_RATE_128_256MS,
	DA7219_AAD_JACK_DET_RATE_256_512MS,
};

enum da7219_aad_jack_rem_deb {
	DA7219_AAD_JACK_REM_DEB_1MS = 0,
	DA7219_AAD_JACK_REM_DEB_5MS,
	DA7219_AAD_JACK_REM_DEB_10MS,
	DA7219_AAD_JACK_REM_DEB_20MS,
};

enum da7219_aad_btn_avg {
	DA7219_AAD_BTN_AVG_1 = 0,
	DA7219_AAD_BTN_AVG_2,
	DA7219_AAD_BTN_AVG_4,
	DA7219_AAD_BTN_AVG_8,
};

enum da7219_aad_adc_1bit_rpt {
	DA7219_AAD_ADC_1BIT_RPT_1 = 0,
	DA7219_AAD_ADC_1BIT_RPT_2,
	DA7219_AAD_ADC_1BIT_RPT_4,
	DA7219_AAD_ADC_1BIT_RPT_8,
};

struct da7219_aad_pdata {
	int irq;

	enum da7219_aad_micbias_pulse_lvl micbias_pulse_lvl;
	u32 micbias_pulse_time;
	enum da7219_aad_btn_cfg btn_cfg;
	enum da7219_aad_mic_det_thr mic_det_thr;
	enum da7219_aad_jack_ins_deb jack_ins_deb;
	enum da7219_aad_jack_ins_det_pty jack_ins_det_pty;
	enum da7219_aad_jack_det_rate jack_det_rate;
	enum da7219_aad_jack_rem_deb jack_rem_deb;

	u8 a_d_btn_thr;
	u8 d_b_btn_thr;
	u8 b_c_btn_thr;
	u8 c_mic_btn_thr;

	enum da7219_aad_btn_avg btn_avg;
	enum da7219_aad_adc_1bit_rpt adc_1bit_rpt;
};

#endif /* __DA7219_AAD_PDATA_H */
