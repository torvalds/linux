/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 * Author: Argus Lin <argus.lin@mediatek.com>
 */

#ifndef _ACCDET_H_
#define _ACCDET_H_

#include <linux/ctype.h>
#include <linux/string.h>

#define ACCDET_DEVNAME "accdet"

#define HEADSET_MODE_1		(1)
#define HEADSET_MODE_2		(2)
#define HEADSET_MODE_6		(6)

#define MT6359_ACCDET_NUM_BUTTONS 4
#define MT6359_ACCDET_JACK_MASK (SND_JACK_HEADPHONE | \
				SND_JACK_HEADSET | \
				SND_JACK_BTN_0 | \
				SND_JACK_BTN_1 | \
				SND_JACK_BTN_2 | \
				SND_JACK_BTN_3)
#define MT6359_ACCDET_BTN_MASK (SND_JACK_BTN_0 | \
				SND_JACK_BTN_1 | \
				SND_JACK_BTN_2 | \
				SND_JACK_BTN_3)

enum eint_moisture_status {
	M_PLUG_IN =		0,
	M_WATER_IN =		1,
	M_HP_PLUG_IN =		2,
	M_PLUG_OUT =		3,
	M_NO_ACT =		4,
	M_UNKNOWN =		5,
};

enum {
	accdet_state000 = 0,
	accdet_state001,
	accdet_state010,
	accdet_state011,
	accdet_auxadc,
	eint_state000,
	eint_state001,
	eint_state010,
	eint_state011,
	eint_inverter_state000,
};

struct three_key_threshold {
	unsigned int mid;
	unsigned int up;
	unsigned int down;
};

struct four_key_threshold {
	unsigned int mid;
	unsigned int voice;
	unsigned int up;
	unsigned int down;
};

struct pwm_deb_settings {
	unsigned int pwm_width;
	unsigned int pwm_thresh;
	unsigned int fall_delay;
	unsigned int rise_delay;
	unsigned int debounce0;
	unsigned int debounce1;
	unsigned int debounce3;
	unsigned int debounce4;
	unsigned int eint_pwm_width;
	unsigned int eint_pwm_thresh;
	unsigned int eint_debounce0;
	unsigned int eint_debounce1;
	unsigned int eint_debounce2;
	unsigned int eint_debounce3;
	unsigned int eint_inverter_debounce;

};

struct dts_data {
	unsigned int mic_vol;
	unsigned int mic_mode;
	unsigned int plugout_deb;
	unsigned int eint_pol;
	struct pwm_deb_settings *pwm_deb;
	struct three_key_threshold three_key;
	struct four_key_threshold four_key;
	unsigned int moisture_detect_enable;
	unsigned int eint_detect_mode;
	unsigned int eint_use_ext_res;
	unsigned int eint_comp_vth;
	unsigned int moisture_detect_mode;
	unsigned int moisture_comp_vth;
	unsigned int moisture_comp_vref2;
	unsigned int moisture_use_ext_res;
};

struct mt6359_accdet {
	struct snd_soc_jack *jack;
	struct device *dev;
	struct regmap *regmap;
	struct dts_data *data;
	unsigned int caps;
	int accdet_irq;
	int accdet_eint0;
	int accdet_eint1;
	struct mutex res_lock; /* lock protection */
	bool jack_plugged;
	unsigned int jack_type;
	unsigned int btn_type;
	unsigned int accdet_status;
	unsigned int pre_accdet_status;
	unsigned int cali_voltage;
	unsigned int jd_sts;
	struct work_struct accdet_work;
	struct workqueue_struct *accdet_workqueue;
	struct work_struct jd_work;
	struct workqueue_struct *jd_workqueue;
};

#if IS_ENABLED(CONFIG_SND_SOC_MT6359_ACCDET)
int mt6359_accdet_enable_jack_detect(struct snd_soc_component *component,
				     struct snd_soc_jack *jack);
#else
static inline int
mt6359_accdet_enable_jack_detect(struct snd_soc_component *component,
				 struct snd_soc_jack *jack)
{
	return -EOPNOTSUPP;
}
#endif
#endif
