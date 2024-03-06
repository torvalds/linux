// SPDX-License-Identifier: GPL-2.0
//
// rt711.c -- rt711 ALSA SoC audio driver
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
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/hda_verbs.h>
#include <sound/jack.h>

#include "rt711.h"

static int rt711_index_write(struct regmap *regmap,
		unsigned int nid, unsigned int reg, unsigned int value)
{
	int ret;
	unsigned int addr = ((RT711_PRIV_INDEX_W_H | nid) << 8) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		pr_err("Failed to set private value: %06x <= %04x ret=%d\n",
			addr, value, ret);

	return ret;
}

static int rt711_index_read(struct regmap *regmap,
		unsigned int nid, unsigned int reg, unsigned int *value)
{
	int ret;
	unsigned int addr = ((RT711_PRIV_INDEX_W_H | nid) << 8) | reg;

	*value = 0;
	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		pr_err("Failed to get private value: %06x => %04x ret=%d\n",
			addr, *value, ret);

	return ret;
}

static int rt711_index_update_bits(struct regmap *regmap, unsigned int nid,
			unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp, orig;
	int ret;

	ret = rt711_index_read(regmap, nid, reg, &orig);
	if (ret < 0)
		return ret;

	tmp = orig & ~mask;
	tmp |= val & mask;

	return rt711_index_write(regmap, nid, reg, tmp);
}

static void rt711_reset(struct regmap *regmap)
{
	regmap_write(regmap, RT711_FUNC_RESET, 0);
	rt711_index_update_bits(regmap, RT711_VENDOR_REG,
		RT711_PARA_VERB_CTL, RT711_HIDDEN_REG_SW_RESET,
		RT711_HIDDEN_REG_SW_RESET);
}

static int rt711_calibration(struct rt711_priv *rt711)
{
	unsigned int val, loop = 0;
	struct device *dev;
	struct regmap *regmap = rt711->regmap;
	int ret = 0;

	mutex_lock(&rt711->calibrate_mutex);
	regmap_write(rt711->regmap,
		RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	dev = regmap_get_device(regmap);

	/* Calibration manual mode */
	rt711_index_update_bits(regmap, RT711_VENDOR_REG, RT711_FSM_CTL,
		0xf, 0x0);

	/* trigger */
	rt711_index_update_bits(regmap, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, RT711_DAC_DC_CALI_TRIGGER,
		RT711_DAC_DC_CALI_TRIGGER);

	/* wait for calibration process */
	rt711_index_read(regmap, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, &val);

	while (val & RT711_DAC_DC_CALI_TRIGGER) {
		if (loop >= 500) {
			pr_err("%s, calibration time-out!\n",
							__func__);
			ret = -ETIMEDOUT;
			break;
		}
		loop++;

		usleep_range(10000, 11000);
		rt711_index_read(regmap, RT711_VENDOR_CALI,
			RT711_DAC_DC_CALI_CTL1, &val);
	}

	/* depop mode */
	rt711_index_update_bits(regmap, RT711_VENDOR_REG,
		RT711_FSM_CTL, 0xf, RT711_DEPOP_CTL);

	regmap_write(rt711->regmap,
		RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D3);
	mutex_unlock(&rt711->calibrate_mutex);

	dev_dbg(dev, "%s calibration complete, ret=%d\n", __func__, ret);
	return ret;
}

static unsigned int rt711_button_detect(struct rt711_priv *rt711)
{
	unsigned int btn_type = 0, val80, val81;
	int ret;

	ret = rt711_index_read(rt711->regmap, RT711_VENDOR_REG,
				RT711_IRQ_FLAG_TABLE1, &val80);
	if (ret < 0)
		goto read_error;
	ret = rt711_index_read(rt711->regmap, RT711_VENDOR_REG,
					RT711_IRQ_FLAG_TABLE2, &val81);
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

static int rt711_headset_detect(struct rt711_priv *rt711)
{
	unsigned int buf, loop = 0;
	int ret;
	unsigned int jack_status = 0, reg;

	ret = rt711_index_read(rt711->regmap, RT711_VENDOR_REG,
				RT711_COMBO_JACK_AUTO_CTL2, &buf);
	if (ret < 0)
		goto io_error;

	while (loop < 500 &&
		(buf & RT711_COMBOJACK_AUTO_DET_STATUS) == 0) {
		loop++;

		usleep_range(9000, 10000);
		ret = rt711_index_read(rt711->regmap, RT711_VENDOR_REG,
					RT711_COMBO_JACK_AUTO_CTL2, &buf);
		if (ret < 0)
			goto io_error;

		reg = RT711_VERB_GET_PIN_SENSE | RT711_HP_OUT;
		ret = regmap_read(rt711->regmap, reg, &jack_status);
		if (ret < 0)
			goto io_error;
		if ((jack_status & (1 << 31)) == 0)
			goto remove_error;
	}

	if (loop >= 500)
		goto to_error;

	if (buf & RT711_COMBOJACK_AUTO_DET_TRS)
		rt711->jack_type = SND_JACK_HEADPHONE;
	else if ((buf & RT711_COMBOJACK_AUTO_DET_CTIA) ||
		(buf & RT711_COMBOJACK_AUTO_DET_OMTP))
		rt711->jack_type = SND_JACK_HEADSET;

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

static void rt711_jack_detect_handler(struct work_struct *work)
{
	struct rt711_priv *rt711 =
		container_of(work, struct rt711_priv, jack_detect_work.work);
	int btn_type = 0, ret;
	unsigned int jack_status = 0, reg;

	if (!rt711->hs_jack)
		return;

	if (!snd_soc_card_is_instantiated(rt711->component->card))
		return;

	if (pm_runtime_status_suspended(rt711->slave->dev.parent)) {
		dev_dbg(&rt711->slave->dev,
			"%s: parent device is pm_runtime_status_suspended, skipping jack detection\n",
			__func__);
		return;
	}

	reg = RT711_VERB_GET_PIN_SENSE | RT711_HP_OUT;
	ret = regmap_read(rt711->regmap, reg, &jack_status);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (jack_status & (1 << 31)) {
		/* jack in */
		if (rt711->jack_type == 0) {
			ret = rt711_headset_detect(rt711);
			if (ret < 0)
				return;
			if (rt711->jack_type == SND_JACK_HEADSET)
				btn_type = rt711_button_detect(rt711);
		} else if (rt711->jack_type == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			btn_type = rt711_button_detect(rt711);
		}
	} else {
		/* jack out */
		rt711->jack_type = 0;
	}

	dev_dbg(&rt711->slave->dev,
		"in %s, jack_type=0x%x\n", __func__, rt711->jack_type);
	dev_dbg(&rt711->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);

	snd_soc_jack_report(rt711->hs_jack, rt711->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt711->hs_jack, rt711->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt711->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt711_btn_check_handler(struct work_struct *work)
{
	struct rt711_priv *rt711 = container_of(work, struct rt711_priv,
		jack_btn_check_work.work);
	int btn_type = 0, ret;
	unsigned int jack_status = 0, reg;

	reg = RT711_VERB_GET_PIN_SENSE | RT711_HP_OUT;
	ret = regmap_read(rt711->regmap, reg, &jack_status);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (jack_status & (1 << 31)) {
		if (rt711->jack_type == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			btn_type = rt711_button_detect(rt711);
		}
	} else {
		rt711->jack_type = 0;
	}

	/* cbj comparator */
	ret = rt711_index_read(rt711->regmap, RT711_VENDOR_REG,
		RT711_COMBO_JACK_AUTO_CTL2, &reg);
	if (ret < 0)
		goto io_error;

	if ((reg & 0xf0) == 0xf0)
		btn_type = 0;

	dev_dbg(&rt711->slave->dev,
		"%s, btn_type=0x%x\n",	__func__, btn_type);
	snd_soc_jack_report(rt711->hs_jack, rt711->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt711->hs_jack, rt711->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt711->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt711_jack_init(struct rt711_priv *rt711)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(rt711->component);

	mutex_lock(&rt711->calibrate_mutex);
	/* power on */
	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt711->regmap,
			RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	if (rt711->hs_jack) {
		/* unsolicited response & IRQ control */
		regmap_write(rt711->regmap,
			RT711_SET_MIC2_UNSOLICITED_ENABLE, 0x82);
		regmap_write(rt711->regmap,
			RT711_SET_HP_UNSOLICITED_ENABLE, 0x81);
		regmap_write(rt711->regmap,
			RT711_SET_INLINE_UNSOLICITED_ENABLE, 0x83);
		rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
			0x10, 0x2420);
		rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
			0x19, 0x2e11);

		switch (rt711->jd_src) {
		case RT711_JD1:
			/* default settings was already for JD1 */
			break;
		case RT711_JD2:
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_JD_CTL2, RT711_JD2_2PORT_200K_DECODE_HP |
				RT711_HP_JD_SEL_JD2,
				RT711_JD2_2PORT_200K_DECODE_HP |
				RT711_HP_JD_SEL_JD2);
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_CC_DET1,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12);
			break;
		case RT711_JD2_100K:
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_JD_CTL2, RT711_JD2_2PORT_100K_DECODE | RT711_JD2_1PORT_TYPE_DECODE |
				RT711_HP_JD_SEL_JD2 | RT711_JD1_2PORT_TYPE_100K_DECODE,
				RT711_JD2_2PORT_100K_DECODE_HP | RT711_JD2_1PORT_JD_HP |
				RT711_HP_JD_SEL_JD2 | RT711_JD1_2PORT_JD_RESERVED);
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_CC_DET1,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12);
			break;
		case RT711_JD2_1P8V_1PORT:
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_JD_CTL1, RT711_JD2_DIGITAL_JD_MODE_SEL,
				RT711_JD2_1_JD_MODE);
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_JD_CTL2, RT711_JD2_1PORT_TYPE_DECODE |
				RT711_HP_JD_SEL_JD2,
				RT711_JD2_1PORT_JD_HP |
				RT711_HP_JD_SEL_JD2);
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_JD_CTL4, RT711_JD2_PAD_PULL_UP_MASK |
				RT711_JD2_MODE_SEL_MASK,
				RT711_JD2_PAD_PULL_UP |
				RT711_JD2_MODE2_1P8V_1PORT);
			rt711_index_update_bits(rt711->regmap, RT711_VENDOR_REG,
				RT711_CC_DET1,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12);
			break;
		default:
			dev_warn(rt711->component->dev, "Wrong JD source\n");
			break;
		}

		dev_dbg(&rt711->slave->dev, "in %s enable\n", __func__);

		mod_delayed_work(system_power_efficient_wq,
			&rt711->jack_detect_work, msecs_to_jiffies(250));
	} else {
		regmap_write(rt711->regmap,
			RT711_SET_MIC2_UNSOLICITED_ENABLE, 0x00);
		regmap_write(rt711->regmap,
			RT711_SET_HP_UNSOLICITED_ENABLE, 0x00);
		regmap_write(rt711->regmap,
			RT711_SET_INLINE_UNSOLICITED_ENABLE, 0x00);

		dev_dbg(&rt711->slave->dev, "in %s disable\n", __func__);
	}

	/* power off */
	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt711->regmap,
			RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D3);
	mutex_unlock(&rt711->calibrate_mutex);
}

static int rt711_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	int ret;

	rt711->hs_jack = hs_jack;

	/* we can only resume if the device was initialized at least once */
	if (!rt711->first_hw_init)
		return 0;

	ret = pm_runtime_resume_and_get(component->dev);
	if (ret < 0) {
		if (ret != -EACCES) {
			dev_err(component->dev, "%s: failed to resume %d\n", __func__, ret);
			return ret;
		}

		/* pm_runtime not enabled yet */
		dev_dbg(component->dev,	"%s: skipping jack init for now\n", __func__);
		return 0;
	}

	rt711_jack_init(rt711);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

static void rt711_get_gain(struct rt711_priv *rt711, unsigned int addr_h,
				unsigned int addr_l, unsigned int val_h,
				unsigned int *r_val, unsigned int *l_val)
{
	/* R Channel */
	*r_val = (val_h << 8);
	regmap_read(rt711->regmap, addr_l, r_val);

	/* L Channel */
	val_h |= 0x20;
	*l_val = (val_h << 8);
	regmap_read(rt711->regmap, addr_h, l_val);
}

/* For Verb-Set Amplifier Gain (Verb ID = 3h) */
static int rt711_set_amp_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned int addr_h, addr_l, val_h, val_ll, val_lr;
	unsigned int read_ll, read_rl;
	int i;

	mutex_lock(&rt711->calibrate_mutex);

	/* Can't use update bit function, so read the original value first */
	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT711_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt711_get_gain(rt711, addr_h, addr_l, val_h, &read_rl, &read_ll);

	/* L Channel */
	if (mc->invert) {
		/* for mute/unmute */
		val_ll = (mc->max - ucontrol->value.integer.value[0])
					<< RT711_MUTE_SFT;
		/* keep gain */
		read_ll = read_ll & 0x7f;
		val_ll |= read_ll;
	} else {
		/* for gain */
		val_ll = ((ucontrol->value.integer.value[0]) & 0x7f);
		if (val_ll > mc->max)
			val_ll = mc->max;
		/* keep mute status */
		read_ll = read_ll & (1 << RT711_MUTE_SFT);
		val_ll |= read_ll;
	}

	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt711->regmap,
				RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	/* R Channel */
	if (mc->invert) {
		/* for mute/unmute */
		val_lr = (mc->max - ucontrol->value.integer.value[1])
					<< RT711_MUTE_SFT;
		/* keep gain */
		read_rl = read_rl & 0x7f;
		val_lr |= read_rl;
	} else {
		/* for gain */
		val_lr = ((ucontrol->value.integer.value[1]) & 0x7f);
		if (val_lr > mc->max)
			val_lr = mc->max;
		/* keep mute status */
		read_rl = read_rl & (1 << RT711_MUTE_SFT);
		val_lr |= read_rl;
	}

	for (i = 0; i < 3; i++) { /* retry 3 times at most */

		if (val_ll == val_lr) {
			/* Set both L/R channels at the same time */
			val_h = (1 << mc->shift) | (3 << 4);
			regmap_write(rt711->regmap,
				addr_h, (val_h << 8 | val_ll));
			regmap_write(rt711->regmap,
				addr_l, (val_h << 8 | val_ll));
		} else {
			/* Lch*/
			val_h = (1 << mc->shift) | (1 << 5);
			regmap_write(rt711->regmap,
				addr_h, (val_h << 8 | val_ll));

			/* Rch */
			val_h = (1 << mc->shift) | (1 << 4);
			regmap_write(rt711->regmap,
				addr_l, (val_h << 8 | val_lr));
		}
		/* check result */
		if (mc->shift == RT711_DIR_OUT_SFT) /* output */
			val_h = 0x80;
		else /* input */
			val_h = 0x0;

		rt711_get_gain(rt711, addr_h, addr_l, val_h,
					&read_rl, &read_ll);
		if (read_rl == val_lr && read_ll == val_ll)
			break;
	}

	if (dapm->bias_level <= SND_SOC_BIAS_STANDBY)
		regmap_write(rt711->regmap,
				RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D3);

	mutex_unlock(&rt711->calibrate_mutex);
	return 0;
}

static int rt711_set_amp_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int addr_h, addr_l, val_h;
	unsigned int read_ll, read_rl;

	/* switch to get command */
	addr_h = mc->reg;
	addr_l = mc->rreg;
	if (mc->shift == RT711_DIR_OUT_SFT) /* output */
		val_h = 0x80;
	else /* input */
		val_h = 0x0;

	rt711_get_gain(rt711, addr_h, addr_l, val_h, &read_rl, &read_ll);

	if (mc->invert) {
		/* mute/unmute for switch controls */
		read_ll = !((read_ll & 0x80) >> RT711_MUTE_SFT);
		read_rl = !((read_rl & 0x80) >> RT711_MUTE_SFT);
	} else {
		/* for gain volume controls */
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

static const struct snd_kcontrol_new rt711_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("DAC Surr Playback Volume",
		RT711_SET_GAIN_DAC2_H, RT711_SET_GAIN_DAC2_L,
		RT711_DIR_OUT_SFT, 0x57, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, out_vol_tlv),
	SOC_DOUBLE_R_EXT("ADC 08 Capture Switch",
		RT711_SET_GAIN_ADC2_H, RT711_SET_GAIN_ADC2_L,
		RT711_DIR_IN_SFT, 1, 1,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put),
	SOC_DOUBLE_R_EXT("ADC 09 Capture Switch",
		RT711_SET_GAIN_ADC1_H, RT711_SET_GAIN_ADC1_L,
		RT711_DIR_IN_SFT, 1, 1,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put),
	SOC_DOUBLE_R_EXT_TLV("ADC 08 Capture Volume",
		RT711_SET_GAIN_ADC2_H, RT711_SET_GAIN_ADC2_L,
		RT711_DIR_IN_SFT, 0x3f, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("ADC 09 Capture Volume",
		RT711_SET_GAIN_ADC1_H, RT711_SET_GAIN_ADC1_L,
		RT711_DIR_IN_SFT, 0x3f, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("AMIC Volume",
		RT711_SET_GAIN_AMIC_H, RT711_SET_GAIN_AMIC_L,
		RT711_DIR_IN_SFT, 3, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("DMIC1 Volume",
		RT711_SET_GAIN_DMIC1_H, RT711_SET_GAIN_DMIC1_L,
		RT711_DIR_IN_SFT, 3, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("DMIC2 Volume",
		RT711_SET_GAIN_DMIC2_H, RT711_SET_GAIN_DMIC2_L,
		RT711_DIR_IN_SFT, 3, 0,
		rt711_set_amp_gain_get, rt711_set_amp_gain_put, mic_vol_tlv),
};

static int rt711_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned int reg, val = 0, nid;
	int ret;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		nid = RT711_MIXER_IN1;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		nid = RT711_MIXER_IN2;
	else
		return -EINVAL;

	/* vid = 0xf01 */
	reg = RT711_VERB_SET_CONNECT_SEL | nid;
	ret = regmap_read(rt711->regmap, reg, &val);
	if (ret < 0) {
		dev_err(component->dev, "%s: sdw read failed: %d\n",
			__func__, ret);
		return ret;
	}

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int rt711_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, reg, nid;
	int ret;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		nid = RT711_MIXER_IN1;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		nid = RT711_MIXER_IN2;
	else
		return -EINVAL;

	/* Verb ID = 0x701h */
	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	reg = RT711_VERB_SET_CONNECT_SEL | nid;
	ret = regmap_read(rt711->regmap, reg, &val2);
	if (ret < 0) {
		dev_err(component->dev, "%s: sdw read failed: %d\n",
			__func__, ret);
		return ret;
	}

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change) {
		reg = RT711_VERB_SET_CONNECT_SEL | nid;
		regmap_write(rt711->regmap, reg, val);
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
	rt711_adc22_enum, SND_SOC_NOPM, 0, adc_mux_text);

static SOC_ENUM_SINGLE_DECL(
	rt711_adc23_enum, SND_SOC_NOPM, 0, adc_mux_text);

static const struct snd_kcontrol_new rt711_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt711_adc22_enum,
			rt711_mux_get, rt711_mux_put);

static const struct snd_kcontrol_new rt711_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt711_adc23_enum,
			rt711_mux_get, rt711_mux_put);

static int rt711_dac_surround_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned int val_h = (1 << RT711_DIR_OUT_SFT) | (0x3 << 4);
	unsigned int val_l;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_DAC2, 0x10);

		val_l = 0x00;
		regmap_write(rt711->regmap,
			RT711_SET_GAIN_HP_H, (val_h << 8 | val_l));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val_l = (1 << RT711_MUTE_SFT);
		regmap_write(rt711->regmap,
			RT711_SET_GAIN_HP_H, (val_h << 8 | val_l));
		usleep_range(50000, 55000);

		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_DAC2, 0x00);
		break;
	}
	return 0;
}

static int rt711_adc_09_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_ADC1, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_ADC1, 0x00);
		break;
	}
	return 0;
}

static int rt711_adc_08_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_ADC2, 0x10);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			RT711_SET_STREAMID_ADC2, 0x00);
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt711_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),

	SND_SOC_DAPM_DAC_E("DAC Surround", NULL, SND_SOC_NOPM, 0, 0,
		rt711_dac_surround_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC 09", NULL, SND_SOC_NOPM, 0, 0,
		rt711_adc_09_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC 08", NULL, SND_SOC_NOPM, 0, 0,
		rt711_adc_08_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt711_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt711_adc23_mux),

	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt711_audio_map[] = {
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

	{"HP", NULL, "DAC Surround"},
};

static int rt711_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
			regmap_write(rt711->regmap,
				RT711_SET_AUDIO_POWER_STATE,
				AC_PWRST_D0);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		mutex_lock(&rt711->calibrate_mutex);
		regmap_write(rt711->regmap,
			RT711_SET_AUDIO_POWER_STATE,
			AC_PWRST_D3);
		mutex_unlock(&rt711->calibrate_mutex);
		break;

	default:
		break;
	}

	return 0;
}

static int rt711_parse_dt(struct rt711_priv *rt711, struct device *dev)
{
	device_property_read_u32(dev, "realtek,jd-src",
		&rt711->jd_src);

	return 0;
}

static int rt711_probe(struct snd_soc_component *component)
{
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	int ret;

	rt711_parse_dt(rt711, &rt711->slave->dev);
	rt711->component = component;

	if (!rt711->first_hw_init)
		return 0;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_rt711 = {
	.probe = rt711_probe,
	.set_bias_level = rt711_set_bias_level,
	.controls = rt711_snd_controls,
	.num_controls = ARRAY_SIZE(rt711_snd_controls),
	.dapm_widgets = rt711_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt711_dapm_widgets),
	.dapm_routes = rt711_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt711_audio_map),
	.set_jack = rt711_set_jack_detect,
	.endianness = 1,
};

static int rt711_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt711_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt711_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_runtime *sdw_stream;
	int retval;
	unsigned int val = 0;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!sdw_stream)
		return -EINVAL;

	if (!rt711->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		port_config.num = 3;
	} else {
		if (dai->id == RT711_AIF1)
			port_config.num = 4;
		else if (dai->id == RT711_AIF2)
			port_config.num = 2;
		else
			return -EINVAL;
	}

	retval = sdw_stream_add_slave(rt711->slave, &stream_config,
					&port_config, 1, sdw_stream);
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
	regmap_write(rt711->regmap, RT711_DAC_FORMAT_H, val);
	regmap_write(rt711->regmap, RT711_ADC1_FORMAT_H, val);
	regmap_write(rt711->regmap, RT711_ADC2_FORMAT_H, val);

	return retval;
}

static int rt711_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt711_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt711->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt711->slave, sdw_stream);
	return 0;
}

#define RT711_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT711_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt711_ops = {
	.hw_params	= rt711_pcm_hw_params,
	.hw_free	= rt711_pcm_hw_free,
	.set_stream	= rt711_set_sdw_stream,
	.shutdown	= rt711_shutdown,
};

static struct snd_soc_dai_driver rt711_dai[] = {
	{
		.name = "rt711-aif1",
		.id = RT711_AIF1,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.ops = &rt711_ops,
	},
	{
		.name = "rt711-aif2",
		.id = RT711_AIF2,
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.ops = &rt711_ops,
	}
};

/* Bus clock frequency */
#define RT711_CLK_FREQ_9600000HZ 9600000
#define RT711_CLK_FREQ_12000000HZ 12000000
#define RT711_CLK_FREQ_6000000HZ 6000000
#define RT711_CLK_FREQ_4800000HZ 4800000
#define RT711_CLK_FREQ_2400000HZ 2400000
#define RT711_CLK_FREQ_12288000HZ 12288000

int rt711_clock_config(struct device *dev)
{
	struct rt711_priv *rt711 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (rt711->params.curr_dr_freq >> 1);

	switch (clk_freq) {
	case RT711_CLK_FREQ_12000000HZ:
		value = 0x0;
		break;
	case RT711_CLK_FREQ_6000000HZ:
		value = 0x1;
		break;
	case RT711_CLK_FREQ_9600000HZ:
		value = 0x2;
		break;
	case RT711_CLK_FREQ_4800000HZ:
		value = 0x3;
		break;
	case RT711_CLK_FREQ_2400000HZ:
		value = 0x4;
		break;
	case RT711_CLK_FREQ_12288000HZ:
		value = 0x5;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt711->regmap, 0xe0, value);
	regmap_write(rt711->regmap, 0xf0, value);

	dev_dbg(dev, "%s complete, clk_freq=%d\n", __func__, clk_freq);

	return 0;
}

static void rt711_calibration_work(struct work_struct *work)
{
	struct rt711_priv *rt711 =
		container_of(work, struct rt711_priv, calibration_work);

	rt711_calibration(rt711);
}

int rt711_init(struct device *dev, struct regmap *sdw_regmap,
			struct regmap *regmap, struct sdw_slave *slave)
{
	struct rt711_priv *rt711;
	int ret;

	rt711 = devm_kzalloc(dev, sizeof(*rt711), GFP_KERNEL);
	if (!rt711)
		return -ENOMEM;

	dev_set_drvdata(dev, rt711);
	rt711->slave = slave;
	rt711->sdw_regmap = sdw_regmap;
	rt711->regmap = regmap;

	regcache_cache_only(rt711->regmap, true);

	mutex_init(&rt711->calibrate_mutex);
	mutex_init(&rt711->disable_irq_lock);

	INIT_DELAYED_WORK(&rt711->jack_detect_work, rt711_jack_detect_handler);
	INIT_DELAYED_WORK(&rt711->jack_btn_check_work, rt711_btn_check_handler);
	INIT_WORK(&rt711->calibration_work, rt711_calibration_work);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt711->hw_init = false;
	rt711->first_hw_init = false;

	/* JD source uses JD2 in default */
	rt711->jd_src = RT711_JD2;

	ret =  devm_snd_soc_register_component(dev,
				&soc_codec_dev_rt711,
				rt711_dai,
				ARRAY_SIZE(rt711_dai));
	if (ret < 0)
		return ret;

	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);

	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(dev);

	pm_runtime_enable(dev);

	/* important note: the device is NOT tagged as 'active' and will remain
	 * 'suspended' until the hardware is enumerated/initialized. This is required
	 * to make sure the ASoC framework use of pm_runtime_get_sync() does not silently
	 * fail with -EACCESS because of race conditions between card creation and enumeration
	 */

	dev_dbg(dev, "%s\n", __func__);

	return ret;
}

int rt711_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt711_priv *rt711 = dev_get_drvdata(dev);

	rt711->disable_irq = false;

	if (rt711->hw_init)
		return 0;

	regcache_cache_only(rt711->regmap, false);
	if (rt711->first_hw_init)
		regcache_cache_bypass(rt711->regmap, true);

	/*
	 * PM runtime status is marked as 'active' only when a Slave reports as Attached
	 */
	if (!rt711->first_hw_init)
		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

	pm_runtime_get_noresume(&slave->dev);

	rt711_reset(rt711->regmap);

	/* power on */
	regmap_write(rt711->regmap, RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D0);

	/* Set Pin Widget */
	regmap_write(rt711->regmap, RT711_SET_PIN_MIC2, 0x25);
	regmap_write(rt711->regmap, RT711_SET_PIN_HP, 0xc0);
	regmap_write(rt711->regmap, RT711_SET_PIN_DMIC1, 0x20);
	regmap_write(rt711->regmap, RT711_SET_PIN_DMIC2, 0x20);
	regmap_write(rt711->regmap, RT711_SET_PIN_LINE1, 0x20);
	regmap_write(rt711->regmap, RT711_SET_PIN_LINE2, 0x20);

	/* Mute HP/ADC1/ADC2 */
	regmap_write(rt711->regmap, RT711_SET_GAIN_HP_H, 0xa080);
	regmap_write(rt711->regmap, RT711_SET_GAIN_HP_H, 0x9080);
	regmap_write(rt711->regmap, RT711_SET_GAIN_ADC2_H, 0x6080);
	regmap_write(rt711->regmap, RT711_SET_GAIN_ADC2_H, 0x5080);
	regmap_write(rt711->regmap, RT711_SET_GAIN_ADC1_H, 0x6080);
	regmap_write(rt711->regmap, RT711_SET_GAIN_ADC1_H, 0x5080);

	/* Set Configuration Default */
	regmap_write(rt711->regmap, 0x4f12, 0x91);
	regmap_write(rt711->regmap, 0x4e12, 0xd6);
	regmap_write(rt711->regmap, 0x4d12, 0x11);
	regmap_write(rt711->regmap, 0x4c12, 0x20);
	regmap_write(rt711->regmap, 0x4f13, 0x91);
	regmap_write(rt711->regmap, 0x4e13, 0xd6);
	regmap_write(rt711->regmap, 0x4d13, 0x11);
	regmap_write(rt711->regmap, 0x4c13, 0x21);
	regmap_write(rt711->regmap, 0x4c21, 0xf0);
	regmap_write(rt711->regmap, 0x4d21, 0x11);
	regmap_write(rt711->regmap, 0x4e21, 0x11);
	regmap_write(rt711->regmap, 0x4f21, 0x01);

	/* Data port arrangement */
	rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
		RT711_TX_RX_MUX_CTL, 0x0154);

	/* Set index */
	rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
		RT711_DIGITAL_MISC_CTRL4, 0x201b);
	rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
		RT711_COMBO_JACK_AUTO_CTL1, 0x5089);
	rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
		RT711_VREFOUT_CTL, 0x5064);
	rt711_index_write(rt711->regmap, RT711_VENDOR_REG,
		RT711_INLINE_CMD_CTL, 0xd249);

	/* Finish Initial Settings, set power to D3 */
	regmap_write(rt711->regmap, RT711_SET_AUDIO_POWER_STATE, AC_PWRST_D3);

	if (rt711->first_hw_init)
		rt711_calibration(rt711);
	else
		schedule_work(&rt711->calibration_work);

	/*
	 * if set_jack callback occurred early than io_init,
	 * we set up the jack detection function now
	 */
	if (rt711->hs_jack)
		rt711_jack_init(rt711);

	if (rt711->first_hw_init) {
		regcache_cache_bypass(rt711->regmap, false);
		regcache_mark_dirty(rt711->regmap);
	} else
		rt711->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt711->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

MODULE_DESCRIPTION("ASoC RT711 SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
