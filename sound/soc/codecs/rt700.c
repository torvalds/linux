// SPDX-License-Identifier: GPL-2.0
//
// rt700.c -- rt700 ALSA SoC audio driver
//
// Copyright(c) 2019 Realtek Semiconductor Corp.
//
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/soundwire/sdw.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/hda_verbs.h>
#include <sound/jack.h>

#include "rt700.h"

static int rt700_index_write(struct regmap *regmap,
		unsigned int reg, unsigned int value)
{
	int ret;
	unsigned int addr = (RT700_PRIV_INDEX_W_H << 8) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		pr_err("Failed to set private value: %06x <= %04x ret=%d\n",
			addr, value, ret);

	return ret;
}

static int rt700_index_read(struct regmap *regmap,
		unsigned int reg, unsigned int *value)
{
	int ret;
	unsigned int addr = (RT700_PRIV_INDEX_W_H << 8) | reg;

	*value = 0;
	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		pr_err("Failed to get private value: %06x => %04x ret=%d\n",
			addr, *value, ret);

	return ret;
}

static unsigned int rt700_button_detect(struct rt700_priv *rt700)
{
	unsigned int btn_type = 0, val80, val81;
	int ret;

	ret = rt700_index_read(rt700->regmap, RT700_IRQ_FLAG_TABLE1, &val80);
	if (ret < 0)
		goto read_error;
	ret = rt700_index_read(rt700->regmap, RT700_IRQ_FLAG_TABLE2, &val81);
	if (ret < 0)
		goto read_error;

	val80 &= 0x0381;
	val81 &= 0xff00;

	switch (val80) {
	case 0x0200:
	case 0x0100:
	case 0x0080:
		btn_type |= SND_JACK_BTN_0;
		break;
	case 0x0001:
		btn_type |= SND_JACK_BTN_3;
		break;
	}
	switch (val81) {
	case 0x8000:
	case 0x4000:
	case 0x2000:
		btn_type |= SND_JACK_BTN_1;
		break;
	case 0x1000:
	case 0x0800:
	case 0x0400:
		btn_type |= SND_JACK_BTN_2;
		break;
	case 0x0200:
	case 0x0100:
		btn_type |= SND_JACK_BTN_3;
		break;
	}
read_error:
	return btn_type;
}

static int rt700_headset_detect(struct rt700_priv *rt700)
{
	unsigned int buf, loop = 0;
	int ret;
	unsigned int jack_status = 0, reg;

	ret = rt700_index_read(rt700->regmap,
					RT700_COMBO_JACK_AUTO_CTL2, &buf);
	if (ret < 0)
		goto io_error;

	while (loop < 500 &&
		(buf & RT700_COMBOJACK_AUTO_DET_STATUS) == 0) {
		loop++;

		usleep_range(9000, 10000);
		ret = rt700_index_read(rt700->regmap,
					RT700_COMBO_JACK_AUTO_CTL2, &buf);
		if (ret < 0)
			goto io_error;

		reg = RT700_VERB_GET_PIN_SENSE | RT700_HP_OUT;
		ret = regmap_read(rt700->regmap, reg, &jack_status);
		if ((jack_status & (1 << 31)) == 0)
			goto remove_error;
	}

	if (loop >= 500)
		goto to_error;

	if (buf & RT700_COMBOJACK_AUTO_DET_TRS)
		rt700->jack_type = SND_JACK_HEADPHONE;
	else if ((buf & RT700_COMBOJACK_AUTO_DET_CTIA) ||
		(buf & RT700_COMBOJACK_AUTO_DET_OMTP))
		rt700->jack_type = SND_JACK_HEADSET;

	return 0;

to_error:
	ret = -ETIMEDOUT;
	pr_err_ratelimited("Time-out error in %s\n", __func__);
	return ret;
io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
remove_error:
	pr_err_ratelimited("Jack removal in %s\n", __func__);
	return -ENODEV;
}

static void rt700_jack_detect_handler(struct work_struct *work)
{
	struct rt700_priv *rt700 =
		container_of(work, struct rt700_priv, jack_detect_work.work);
	int btn_type = 0, ret;
	unsigned int jack_status = 0, reg;

	if (!rt700->hs_jack)
		return;

	if (!rt700->component->card->instantiated)
		return;

	reg = RT700_VERB_GET_PIN_SENSE | RT700_HP_OUT;
	ret = regmap_read(rt700->regmap, reg, &jack_status);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (jack_status & (1 << 31)) {
		/* jack in */
		if (rt700->jack_type == 0) {
			ret = rt700_headset_detect(rt700);
			if (ret < 0)
				return;
			if (rt700->jack_type == SND_JACK_HEADSET)
				btn_type = rt700_button_detect(rt700);
		} else if (rt700->jack_type == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			btn_type = rt700_button_detect(rt700);
		}
	} else {
		/* jack out */
		rt700->jack_type = 0;
	}

	dev_dbg(&rt700->slave->dev,
		"in %s, jack_type=0x%x\n", __func__, rt700->jack_type);
	dev_dbg(&rt700->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);

	snd_soc_jack_report(rt700->hs_jack, rt700->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt700->hs_jack, rt700->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt700->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt700_btn_check_handler(struct work_struct *work)
{
	struct rt700_priv *rt700 = container_of(work, struct rt700_priv,
		jack_btn_check_work.work);
	int btn_type = 0, ret;
	unsigned int jack_status = 0, reg;

	reg = RT700_VERB_GET_PIN_SENSE | RT700_HP_OUT;
	ret = regmap_read(rt700->regmap, reg, &jack_status);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (jack_status & (1 << 31)) {
		if (rt700->jack_type == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			btn_type = rt700_button_detect(rt700);
		}
	} else {
		rt700->jack_type = 0;
	}

	/* cbj comparator */
	ret = rt700_index_read(rt700->regmap, RT700_COMBO_JACK_AUTO_CTL2, &reg);
	if (ret < 0)
		goto io_error;

	if ((reg & 0xf0) == 0xf0)
		btn_type = 0;

	dev_dbg(&rt700->slave->dev,
		"%s, btn_type=0x%x\n",	__func__, btn_type);
	snd_soc_jack_report(rt700->hs_jack, rt700->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt700->hs_jack, rt700->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt700->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt700_jack_init(struct rt700_priv *rt700)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(rt700->component);

	/* power on */
	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt700->regmap,
			RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	if (rt700->hs_jack) {
		/* Enable Jack Detection */
		regmap_write(rt700->regmap,
			RT700_SET_MIC2_UNSOLICITED_ENABLE, 0x82);
		regmap_write(rt700->regmap,
			RT700_SET_HP_UNSOLICITED_ENABLE, 0x81);
		regmap_write(rt700->regmap,
			RT700_SET_INLINE_UNSOLICITED_ENABLE, 0x83);
		rt700_index_write(rt700->regmap, 0x10, 0x2420);
		rt700_index_write(rt700->regmap, 0x19, 0x2e11);

		dev_dbg(&rt700->slave->dev, "in %s enable\n", __func__);

		mod_delayed_work(system_power_efficient_wq,
			&rt700->jack_detect_work, msecs_to_jiffies(250));
	} else {
		regmap_write(rt700->regmap,
			RT700_SET_MIC2_UNSOLICITED_ENABLE, 0x00);
		regmap_write(rt700->regmap,
			RT700_SET_HP_UNSOLICITED_ENABLE, 0x00);
		regmap_write(rt700->regmap,
			RT700_SET_INLINE_UNSOLICITED_ENABLE, 0x00);

		dev_dbg(&rt700->slave->dev, "in %s disable\n", __func__);
	}

	/* power off */
	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt700->regmap,
			RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D3);
}

static int rt700_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	rt700->hs_jack = hs_jack;

	if (!rt700->hw_init) {
		dev_dbg(&rt700->slave->dev,
			"%s hw_init not ready yet\n", __func__);
		return 0;
	}

	rt700_jack_init(rt700);

	return 0;
}

static void rt700_get_gain(struct rt700_priv *rt700, unsigned int addr_h,
				unsigned int addr_l, unsigned int val_h,
				unsigned int *r_val, unsigned int *l_val)
{
	/* R Channel */
	*r_val = (val_h << 8);
	regmap_read(rt700->regmap, addr_l, r_val);

	/* L Channel */
	val_h |= 0x20;
	*l_val = (val_h << 8);
	regmap_read(rt700->regmap, addr_h, l_val);
}

/* For Verb-Set Amplifier Gain (Verb ID = 3h) */
static int rt700_set_amp_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	unsigned int addr_h, addr_l, val_h, val_ll, val_lr;
	unsigned int read_ll, read_rl;
	int i;

	/* Can't use update bit function, so read the original value first */
	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT700_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt700_get_gain(rt700, addr_h, addr_l, val_h, &read_rl, &read_ll);

	/* L Channel */
	if (mc->invert) {
		/* for mute */
		val_ll = (mc->max - ucontrol->value.integer.value[0]) << 7;
		/* keep gain */
		read_ll = read_ll & 0x7f;
		val_ll |= read_ll;
	} else {
		/* for gain */
		val_ll = ((ucontrol->value.integer.value[0]) & 0x7f);
		if (val_ll > mc->max)
			val_ll = mc->max;
		/* keep mute status */
		read_ll = read_ll & 0x80;
		val_ll |= read_ll;
	}

	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt700->regmap,
				RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	/* R Channel */
	if (mc->invert) {
		/* for mute */
		val_lr = (mc->max - ucontrol->value.integer.value[1]) << 7;
		/* keep gain */
		read_rl = read_rl & 0x7f;
		val_lr |= read_rl;
	} else {
		/* for gain */
		val_lr = ((ucontrol->value.integer.value[1]) & 0x7f);
		if (val_lr > mc->max)
			val_lr = mc->max;
		/* keep mute status */
		read_rl = read_rl & 0x80;
		val_lr |= read_rl;
	}

	for (i = 0; i < 3; i++) { /* retry 3 times at most */
		if (val_ll == val_lr) {
			/* Set both L/R channels at the same time */
			val_h = (1 << mc->shift) | (3 << 4);
			regmap_write(rt700->regmap,
				addr_h, (val_h << 8 | val_ll));
			regmap_write(rt700->regmap,
				addr_l, (val_h << 8 | val_ll));
		} else {
			/* Lch*/
			val_h = (1 << mc->shift) | (1 << 5);
			regmap_write(rt700->regmap,
				addr_h, (val_h << 8 | val_ll));

			/* Rch */
			val_h = (1 << mc->shift) | (1 << 4);
			regmap_write(rt700->regmap,
				addr_l, (val_h << 8 | val_lr));
		}
		/* check result */
		if (mc->shift == RT700_DIR_OUT_SFT) /* output */
			val_h = 0x80;
		else /* input */
			val_h = 0x0;

		rt700_get_gain(rt700, addr_h, addr_l, val_h,
					&read_rl, &read_ll);
		if (read_rl == val_lr && read_ll == val_ll)
			break;
	}

	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt700->regmap,
				RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D3);
	return 0;
}

static int rt700_set_amp_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int addr_h, addr_l, val_h;
	unsigned int read_ll, read_rl;

	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT700_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt700_get_gain(rt700, addr_h, addr_l, val_h, &read_rl, &read_ll);

	if (mc->invert) {
		/* for mute status */
		read_ll = !((read_ll & 0x80) >> RT700_MUTE_SFT);
		read_rl = !((read_rl & 0x80) >> RT700_MUTE_SFT);
	} else {
		/* for gain */
		read_ll = read_ll & 0x7f;
		read_rl = read_rl & 0x7f;
	}
	ucontrol->value.integer.value[0] = read_ll;
	ucontrol->value.integer.value[1] = read_rl;

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);

static const struct snd_kcontrol_new rt700_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("DAC Front Playback Volume",
		RT700_SET_GAIN_DAC1_H, RT700_SET_GAIN_DAC1_L,
		RT700_DIR_OUT_SFT, 0x57, 0,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put, out_vol_tlv),
	SOC_DOUBLE_R_EXT("ADC 08 Capture Switch",
		RT700_SET_GAIN_ADC2_H, RT700_SET_GAIN_ADC2_L,
		RT700_DIR_IN_SFT, 1, 1,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put),
	SOC_DOUBLE_R_EXT("ADC 09 Capture Switch",
		RT700_SET_GAIN_ADC1_H,	RT700_SET_GAIN_ADC1_L,
		RT700_DIR_IN_SFT, 1, 1,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put),
	SOC_DOUBLE_R_EXT_TLV("ADC 08 Capture Volume",
		RT700_SET_GAIN_ADC2_H,	RT700_SET_GAIN_ADC2_L,
		RT700_DIR_IN_SFT, 0x3f, 0,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("ADC 09 Capture Volume",
		RT700_SET_GAIN_ADC1_H, RT700_SET_GAIN_ADC1_L,
		RT700_DIR_IN_SFT, 0x3f, 0,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("AMIC Volume",
		RT700_SET_GAIN_AMIC_H,	RT700_SET_GAIN_AMIC_L,
		RT700_DIR_IN_SFT, 3, 0,
		rt700_set_amp_gain_get, rt700_set_amp_gain_put, mic_vol_tlv),
};

static int rt700_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	unsigned int reg, val = 0, nid;
	int ret;

	if (strstr(ucontrol->id.name, "HPO Mux"))
		nid = RT700_HP_OUT;
	else if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		nid = RT700_MIXER_IN1;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		nid = RT700_MIXER_IN2;
	else
		return -EINVAL;

	/* vid = 0xf01 */
	reg = RT700_VERB_SET_CONNECT_SEL | nid;
	ret = regmap_read(rt700->regmap, reg, &val);
	if (ret < 0)
		return ret;

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int rt700_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, reg, nid;
	int ret;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "HPO Mux"))
		nid = RT700_HP_OUT;
	else if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		nid = RT700_MIXER_IN1;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		nid = RT700_MIXER_IN2;
	else
		return -EINVAL;

	/* Verb ID = 0x701h */
	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	reg = RT700_VERB_SET_CONNECT_SEL | nid;
	ret = regmap_read(rt700->regmap, reg, &val2);
	if (ret < 0)
		return ret;

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change) {
		reg = RT700_VERB_SET_CONNECT_SEL | nid;
		regmap_write(rt700->regmap, reg, val);
	}

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
						item[0], e, NULL);

	return change;
}

static const char * const adc_mux_text[] = {
	"MIC2",
	"LINE1",
	"LINE2",
	"DMIC",
};

static SOC_ENUM_SINGLE_DECL(
	rt700_adc22_enum, SND_SOC_NOPM, 0, adc_mux_text);

static SOC_ENUM_SINGLE_DECL(
	rt700_adc23_enum, SND_SOC_NOPM, 0, adc_mux_text);

static const struct snd_kcontrol_new rt700_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt700_adc22_enum,
			rt700_mux_get, rt700_mux_put);

static const struct snd_kcontrol_new rt700_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt700_adc23_enum,
			rt700_mux_get, rt700_mux_put);

static const char * const out_mux_text[] = {
	"Front",
	"Surround",
};

static SOC_ENUM_SINGLE_DECL(
	rt700_hp_enum, SND_SOC_NOPM, 0, out_mux_text);

static const struct snd_kcontrol_new rt700_hp_mux =
	SOC_DAPM_ENUM_EXT("HP Mux", rt700_hp_enum,
			rt700_mux_get, rt700_mux_put);

static int rt700_dac_front_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_DAC1, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_DAC1, 0x00);
		break;
	}
	return 0;
}

static int rt700_dac_surround_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_DAC2, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_DAC2, 0x00);
		break;
	}
	return 0;
}

static int rt700_adc_09_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_ADC1, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_ADC1, 0x00);
		break;
	}
	return 0;
}

static int rt700_adc_08_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_ADC2, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt700->regmap,
			RT700_SET_STREAMID_ADC2, 0x00);
		break;
	}
	return 0;
}

static int rt700_hpo_mux_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	unsigned int val_h = (1 << RT700_DIR_OUT_SFT) | (0x3 << 4);
	unsigned int val_l;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val_l = 0x00;
		regmap_write(rt700->regmap,
			RT700_SET_GAIN_HP_H, (val_h << 8 | val_l));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val_l = (1 << RT700_MUTE_SFT);
		regmap_write(rt700->regmap,
			RT700_SET_GAIN_HP_H, (val_h << 8 | val_l));
		usleep_range(50000, 55000);
		break;
	}
	return 0;
}

static int rt700_spk_pga_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	unsigned int val_h = (1 << RT700_DIR_OUT_SFT) | (0x3 << 4);
	unsigned int val_l;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val_l = 0x00;
		regmap_write(rt700->regmap,
			RT700_SET_GAIN_SPK_H, (val_h << 8 | val_l));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val_l = (1 << RT700_MUTE_SFT);
		regmap_write(rt700->regmap,
			RT700_SET_GAIN_SPK_H, (val_h << 8 | val_l));
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt700_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),
	SND_SOC_DAPM_DAC_E("DAC Front", NULL, SND_SOC_NOPM, 0, 0,
		rt700_dac_front_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DAC Surround", NULL, SND_SOC_NOPM, 0, 0,
		rt700_dac_surround_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX_E("HPO Mux", SND_SOC_NOPM, 0, 0, &rt700_hp_mux,
		rt700_hpo_mux_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("SPK PGA", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt700_spk_pga_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC 09", NULL, SND_SOC_NOPM, 0, 0,
		rt700_adc_09_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC 08", NULL, SND_SOC_NOPM, 0, 0,
		rt700_adc_08_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt700_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt700_adc23_mux),
	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt700_audio_map[] = {
	{"DAC Front", NULL, "DP1RX"},
	{"DAC Surround", NULL, "DP3RX"},
	{"DP2TX", NULL, "ADC 09"},
	{"DP4TX", NULL, "ADC 08"},
	{"ADC 09", NULL, "ADC 22 Mux"},
	{"ADC 08", NULL, "ADC 23 Mux"},
	{"ADC 22 Mux", "DMIC", "DMIC1"},
	{"ADC 22 Mux", "LINE1", "LINE1"},
	{"ADC 22 Mux", "LINE2", "LINE2"},
	{"ADC 22 Mux", "MIC2", "MIC2"},
	{"ADC 23 Mux", "DMIC", "DMIC2"},
	{"ADC 23 Mux", "LINE1", "LINE1"},
	{"ADC 23 Mux", "LINE2", "LINE2"},
	{"ADC 23 Mux", "MIC2", "MIC2"},
	{"HPO Mux", "Front", "DAC Front"},
	{"HPO Mux", "Surround", "DAC Surround"},
	{"HP", NULL, "HPO Mux"},
	{"SPK PGA", NULL, "DAC Front"},
	{"SPK", NULL, "SPK PGA"},
};

static int rt700_probe(struct snd_soc_component *component)
{
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	rt700->component = component;

	return 0;
}

static int rt700_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			regmap_write(rt700->regmap,
				RT700_SET_AUDIO_POWER_STATE,
				AC_PWRST_D0);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_write(rt700->regmap,
			RT700_SET_AUDIO_POWER_STATE,
			AC_PWRST_D3);
		break;

	default:
		break;
	}
	dapm->bias_level = level;
	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_rt700 = {
	.probe = rt700_probe,
	.set_bias_level = rt700_set_bias_level,
	.controls = rt700_snd_controls,
	.num_controls = ARRAY_SIZE(rt700_snd_controls),
	.dapm_widgets = rt700_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt700_dapm_widgets),
	.dapm_routes = rt700_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt700_audio_map),
	.set_jack = rt700_set_jack_detect,
};

static int rt700_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	struct sdw_stream_data *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->sdw_stream = (struct sdw_stream_runtime *)sdw_stream;

	/* Use tx_mask or rx_mask to configure stream tag and set dma_data */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = stream;
	else
		dai->capture_dma_data = stream;

	return 0;
}

static void rt700_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt700_pcm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_data *stream;
	int retval, port, num_channels;
	unsigned int val = 0;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt700->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	/* This code assumes port 1 for playback and port 2 for capture */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		port = 1;
	} else {
		direction = SDW_DATA_DIR_TX;
		port = 2;
	}

	switch (dai->id) {
	case RT700_AIF1:
		break;
	case RT700_AIF2:
		port += 2;
		break;
	default:
		dev_err(component->dev, "Invalid DAI id %d\n", dai->id);
		return -EINVAL;
	}

	stream_config.frame_rate = params_rate(params);
	stream_config.ch_count = params_channels(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	num_channels = params_channels(params);
	port_config.ch_mask = (1 << (num_channels)) - 1;
	port_config.num = port;

	retval = sdw_stream_add_slave(rt700->slave, &stream_config,
					&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	if (params_channels(params) <= 16) {
		/* bit 3:0 Number of Channel */
		val |= (params_channels(params) - 1);
	} else {
		dev_err(component->dev, "Unsupported channels %d\n",
			params_channels(params));
		return -EINVAL;
	}

	switch (params_width(params)) {
	/* bit 6:4 Bits per Sample */
	case 8:
		break;
	case 16:
		val |= (0x1 << 4);
		break;
	case 20:
		val |= (0x2 << 4);
		break;
	case 24:
		val |= (0x3 << 4);
		break;
	case 32:
		val |= (0x4 << 4);
		break;
	default:
		return -EINVAL;
	}

	/* 48Khz */
	regmap_write(rt700->regmap, RT700_DAC_FORMAT_H, val);
	regmap_write(rt700->regmap, RT700_ADC_FORMAT_H, val);

	return retval;
}

static int rt700_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt700_priv *rt700 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt700->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt700->slave, stream->sdw_stream);
	return 0;
}

#define RT700_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT700_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt700_ops = {
	.hw_params	= rt700_pcm_hw_params,
	.hw_free	= rt700_pcm_hw_free,
	.set_sdw_stream	= rt700_set_sdw_stream,
	.shutdown	= rt700_shutdown,
};

static struct snd_soc_dai_driver rt700_dai[] = {
	{
		.name = "rt700-aif1",
		.id = RT700_AIF1,
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT700_STEREO_RATES,
			.formats = RT700_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT700_STEREO_RATES,
			.formats = RT700_FORMATS,
		},
		.ops = &rt700_ops,
	},
	{
		.name = "rt700-aif2",
		.id = RT700_AIF2,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT700_STEREO_RATES,
			.formats = RT700_FORMATS,
		},
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT700_STEREO_RATES,
			.formats = RT700_FORMATS,
		},
		.ops = &rt700_ops,
	},
};

/* Bus clock frequency */
#define RT700_CLK_FREQ_9600000HZ 9600000
#define RT700_CLK_FREQ_12000000HZ 12000000
#define RT700_CLK_FREQ_6000000HZ 6000000
#define RT700_CLK_FREQ_4800000HZ 4800000
#define RT700_CLK_FREQ_2400000HZ 2400000
#define RT700_CLK_FREQ_12288000HZ 12288000

int rt700_clock_config(struct device *dev)
{
	struct rt700_priv *rt700 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (rt700->params.curr_dr_freq >> 1);

	switch (clk_freq) {
	case RT700_CLK_FREQ_12000000HZ:
		value = 0x0;
		break;
	case RT700_CLK_FREQ_6000000HZ:
		value = 0x1;
		break;
	case RT700_CLK_FREQ_9600000HZ:
		value = 0x2;
		break;
	case RT700_CLK_FREQ_4800000HZ:
		value = 0x3;
		break;
	case RT700_CLK_FREQ_2400000HZ:
		value = 0x4;
		break;
	case RT700_CLK_FREQ_12288000HZ:
		value = 0x5;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt700->regmap, 0xe0, value);
	regmap_write(rt700->regmap, 0xf0, value);

	dev_dbg(dev, "%s complete, clk_freq=%d\n", __func__, clk_freq);

	return 0;
}

int rt700_init(struct device *dev, struct regmap *sdw_regmap,
			struct regmap *regmap, struct sdw_slave *slave)

{
	struct rt700_priv *rt700;
	int ret;

	rt700 = devm_kzalloc(dev, sizeof(*rt700), GFP_KERNEL);
	if (!rt700)
		return -ENOMEM;

	dev_set_drvdata(dev, rt700);
	rt700->slave = slave;
	rt700->sdw_regmap = sdw_regmap;
	rt700->regmap = regmap;

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt700->hw_init = false;
	rt700->first_hw_init = false;

	ret =  devm_snd_soc_register_component(dev,
				&soc_codec_dev_rt700,
				rt700_dai,
				ARRAY_SIZE(rt700_dai));

	dev_dbg(&slave->dev, "%s\n", __func__);

	return ret;
}

int rt700_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt700_priv *rt700 = dev_get_drvdata(dev);

	if (rt700->hw_init)
		return 0;

	if (rt700->first_hw_init) {
		regcache_cache_only(rt700->regmap, false);
		regcache_cache_bypass(rt700->regmap, true);
	}

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!rt700->first_hw_init) {
		/* set autosuspend parameters */
		pm_runtime_set_autosuspend_delay(&slave->dev, 3000);
		pm_runtime_use_autosuspend(&slave->dev);

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

		/* make sure the device does not suspend immediately */
		pm_runtime_mark_last_busy(&slave->dev);

		pm_runtime_enable(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	/* reset */
	regmap_write(rt700->regmap, 0xff01, 0x0000);
	regmap_write(rt700->regmap, 0x7520, 0x001a);
	regmap_write(rt700->regmap, 0x7420, 0xc003);

	/* power on */
	regmap_write(rt700->regmap, RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D0);
	/* Set Pin Widget */
	regmap_write(rt700->regmap, RT700_SET_PIN_HP, 0x40);
	regmap_write(rt700->regmap, RT700_SET_PIN_SPK, 0x40);
	regmap_write(rt700->regmap, RT700_SET_EAPD_SPK, RT700_EAPD_HIGH);
	regmap_write(rt700->regmap, RT700_SET_PIN_DMIC1, 0x20);
	regmap_write(rt700->regmap, RT700_SET_PIN_DMIC2, 0x20);
	regmap_write(rt700->regmap, RT700_SET_PIN_MIC2, 0x20);

	/* Set Configuration Default */
	regmap_write(rt700->regmap, 0x4f12, 0x91);
	regmap_write(rt700->regmap, 0x4e12, 0xd6);
	regmap_write(rt700->regmap, 0x4d12, 0x11);
	regmap_write(rt700->regmap, 0x4c12, 0x20);
	regmap_write(rt700->regmap, 0x4f13, 0x91);
	regmap_write(rt700->regmap, 0x4e13, 0xd6);
	regmap_write(rt700->regmap, 0x4d13, 0x11);
	regmap_write(rt700->regmap, 0x4c13, 0x21);

	regmap_write(rt700->regmap, 0x4f19, 0x02);
	regmap_write(rt700->regmap, 0x4e19, 0xa1);
	regmap_write(rt700->regmap, 0x4d19, 0x90);
	regmap_write(rt700->regmap, 0x4c19, 0x80);

	/* Enable Line2 */
	regmap_write(rt700->regmap,  0x371b, 0x40);
	regmap_write(rt700->regmap,  0x731b, 0xb0);
	regmap_write(rt700->regmap,  0x839b, 0x00);

	/* Set index */
	rt700_index_write(rt700->regmap, 0x4a, 0x201b);
	rt700_index_write(rt700->regmap, 0x45, 0x5089);
	rt700_index_write(rt700->regmap, 0x6b, 0x5064);
	rt700_index_write(rt700->regmap, 0x48, 0xd249);

	/* Finish Initial Settings, set power to D3 */
	regmap_write(rt700->regmap, RT700_SET_AUDIO_POWER_STATE, AC_PWRST_D3);

	if (!rt700->first_hw_init) {
		INIT_DELAYED_WORK(&rt700->jack_detect_work,
			rt700_jack_detect_handler);
		INIT_DELAYED_WORK(&rt700->jack_btn_check_work,
			rt700_btn_check_handler);
	}

	/*
	 * if set_jack callback occurred early than io_init,
	 * we set up the jack detection function now
	 */
	if (rt700->hs_jack)
		rt700_jack_init(rt700);

	if (rt700->first_hw_init) {
		regcache_cache_bypass(rt700->regmap, false);
		regcache_mark_dirty(rt700->regmap);
	} else
		rt700->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt700->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);

	return 0;
}

MODULE_DESCRIPTION("ASoC RT700 driver SDW");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL v2");
