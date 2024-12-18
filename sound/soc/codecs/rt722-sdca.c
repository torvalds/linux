// SPDX-License-Identifier: GPL-2.0-only
//
// rt722-sdca.c -- rt722 SDCA ALSA SoC audio driver
//
// Copyright(c) 2023 Realtek Semiconductor Corp.
//
//

#include <linux/bitops.h>
#include <sound/core.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/pcm.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/slab.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "rt722-sdca.h"

int rt722_sdca_index_write(struct rt722_sdca_priv *rt722,
		unsigned int nid, unsigned int reg, unsigned int value)
{
	struct regmap *regmap = rt722->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;
	int ret;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt722->slave->dev,
			"%s: Failed to set private value: %06x <= %04x ret=%d\n",
			__func__, addr, value, ret);

	return ret;
}

int rt722_sdca_index_read(struct rt722_sdca_priv *rt722,
		unsigned int nid, unsigned int reg, unsigned int *value)
{
	int ret;
	struct regmap *regmap = rt722->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;

	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt722->slave->dev,
			"%s: Failed to get private value: %06x => %04x ret=%d\n",
			__func__, addr, *value, ret);

	return ret;
}

static int rt722_sdca_index_update_bits(struct rt722_sdca_priv *rt722,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	int ret;

	ret = rt722_sdca_index_read(rt722, nid, reg, &tmp);
	if (ret < 0)
		return ret;

	set_mask_bits(&tmp, mask, val);
	return rt722_sdca_index_write(rt722, nid, reg, tmp);
}

static int rt722_sdca_btn_type(unsigned char *buffer)
{
	if ((*buffer & 0xf0) == 0x10 || (*buffer & 0x0f) == 0x01 || (*(buffer + 1) == 0x01) ||
		(*(buffer + 1) == 0x10))
		return SND_JACK_BTN_2;
	else if ((*buffer & 0xf0) == 0x20 || (*buffer & 0x0f) == 0x02 || (*(buffer + 1) == 0x02) ||
		(*(buffer + 1) == 0x20))
		return SND_JACK_BTN_3;
	else if ((*buffer & 0xf0) == 0x40 || (*buffer & 0x0f) == 0x04 || (*(buffer + 1) == 0x04) ||
		(*(buffer + 1) == 0x40))
		return SND_JACK_BTN_0;
	else if ((*buffer & 0xf0) == 0x80 || (*buffer & 0x0f) == 0x08 || (*(buffer + 1) == 0x08) ||
		(*(buffer + 1) == 0x80))
		return SND_JACK_BTN_1;

	return 0;
}

static unsigned int rt722_sdca_button_detect(struct rt722_sdca_priv *rt722)
{
	unsigned int btn_type = 0, offset, idx, val, owner;
	int ret;
	unsigned char buf[3];

	/* get current UMP message owner */
	ret = regmap_read(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT722_SDCA_ENT_HID01,
			RT722_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), &owner);
	if (ret < 0)
		return 0;

	/* if owner is device then there is no button event from device */
	if (owner == 1)
		return 0;

	/* read UMP message offset */
	ret = regmap_read(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT722_SDCA_ENT_HID01,
			RT722_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0), &offset);
	if (ret < 0)
		goto _end_btn_det_;

	for (idx = 0; idx < sizeof(buf); idx++) {
		ret = regmap_read(rt722->regmap,
			RT722_BUF_ADDR_HID1 + offset + idx, &val);
		if (ret < 0)
			goto _end_btn_det_;
		buf[idx] = val & 0xff;
	}

	if (buf[0] == 0x11)
		btn_type = rt722_sdca_btn_type(&buf[1]);

_end_btn_det_:
	/* Host is owner, so set back to device */
	if (owner == 0)
		/* set owner to device */
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT722_SDCA_ENT_HID01,
				RT722_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), 0x01);

	return btn_type;
}

static int rt722_sdca_headset_detect(struct rt722_sdca_priv *rt722)
{
	unsigned int det_mode;
	int ret;

	/* get detected_mode */
	ret = regmap_read(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_GE49,
			RT722_SDCA_CTL_DETECTED_MODE, 0), &det_mode);
	if (ret < 0)
		goto io_error;

	switch (det_mode) {
	case 0x00:
		rt722->jack_type = 0;
		break;
	case 0x03:
		rt722->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		rt722->jack_type = SND_JACK_HEADSET;
		break;
	}

	/* write selected_mode */
	if (det_mode) {
		ret = regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_GE49,
				RT722_SDCA_CTL_SELECTED_MODE, 0), det_mode);
		if (ret < 0)
			goto io_error;
	}

	dev_dbg(&rt722->slave->dev,
		"%s, detected_mode=0x%x\n", __func__, det_mode);

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static void rt722_sdca_jack_detect_handler(struct work_struct *work)
{
	struct rt722_sdca_priv *rt722 =
		container_of(work, struct rt722_sdca_priv, jack_detect_work.work);
	int btn_type = 0, ret;

	if (!rt722->hs_jack)
		return;

	if (!rt722->component->card || !rt722->component->card->instantiated)
		return;

	/* SDW_SCP_SDCA_INT_SDCA_6 is used for jack detection */
	if (rt722->scp_sdca_stat1 & SDW_SCP_SDCA_INT_SDCA_6) {
		ret = rt722_sdca_headset_detect(rt722);
		if (ret < 0)
			return;
	}

	/* SDW_SCP_SDCA_INT_SDCA_8 is used for button detection */
	if (rt722->scp_sdca_stat2 & SDW_SCP_SDCA_INT_SDCA_8)
		btn_type = rt722_sdca_button_detect(rt722);

	if (rt722->jack_type == 0)
		btn_type = 0;

	dev_dbg(&rt722->slave->dev,
		"in %s, jack_type=%d\n", __func__, rt722->jack_type);
	dev_dbg(&rt722->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);
	dev_dbg(&rt722->slave->dev,
		"in %s, scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
		rt722->scp_sdca_stat1, rt722->scp_sdca_stat2);

	snd_soc_jack_report(rt722->hs_jack, rt722->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt722->hs_jack, rt722->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt722->jack_btn_check_work, msecs_to_jiffies(200));
	}
}

static void rt722_sdca_btn_check_handler(struct work_struct *work)
{
	struct rt722_sdca_priv *rt722 =
		container_of(work, struct rt722_sdca_priv, jack_btn_check_work.work);
	int btn_type = 0, ret, idx;
	unsigned int det_mode, offset, val;
	unsigned char buf[3];

	ret = regmap_read(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_GE49,
			RT722_SDCA_CTL_DETECTED_MODE, 0), &det_mode);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (det_mode) {
		/* read UMP message offset */
		ret = regmap_read(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT722_SDCA_ENT_HID01,
				RT722_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0), &offset);
		if (ret < 0)
			goto io_error;

		for (idx = 0; idx < sizeof(buf); idx++) {
			ret = regmap_read(rt722->regmap,
				RT722_BUF_ADDR_HID1 + offset + idx, &val);
			if (ret < 0)
				goto io_error;
			buf[idx] = val & 0xff;
		}

		if (buf[0] == 0x11)
			btn_type = rt722_sdca_btn_type(&buf[1]);
	} else
		rt722->jack_type = 0;

	dev_dbg(&rt722->slave->dev, "%s, btn_type=0x%x\n",	__func__, btn_type);
	snd_soc_jack_report(rt722->hs_jack, rt722->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt722->hs_jack, rt722->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt722->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt722_sdca_jack_init(struct rt722_sdca_priv *rt722)
{
	mutex_lock(&rt722->calibrate_mutex);
	if (rt722->hs_jack) {
		/* set SCP_SDCA_IntMask1[0]=1 */
		sdw_write_no_pm(rt722->slave, SDW_SCP_SDCA_INTMASK1,
			SDW_SCP_SDCA_INTMASK_SDCA_0 | SDW_SCP_SDCA_INTMASK_SDCA_6);
		/* set SCP_SDCA_IntMask2[0]=1 */
		sdw_write_no_pm(rt722->slave, SDW_SCP_SDCA_INTMASK2,
			SDW_SCP_SDCA_INTMASK_SDCA_8);
		dev_dbg(&rt722->slave->dev, "in %s enable\n", __func__);
		rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
			RT722_HDA_LEGACY_UNSOL_CTL, 0x016E);
		/* set XU(et03h) & XU(et0Dh) to Not bypassed */
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_XU03,
				RT722_SDCA_CTL_SELECTED_MODE, 0), 0);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_XU0D,
				RT722_SDCA_CTL_SELECTED_MODE, 0), 0);
		/* trigger GE interrupt */
		rt722_sdca_index_update_bits(rt722, RT722_VENDOR_HDA_CTL,
			RT722_GE_RELATED_CTL2, 0x4000, 0x4000);
	}
	mutex_unlock(&rt722->calibrate_mutex);
}

static int rt722_sdca_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	int ret;

	rt722->hs_jack = hs_jack;

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

	rt722_sdca_jack_init(rt722);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

/* For SDCA control DAC/ADC Gain */
static int rt722_sdca_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned int read_l, read_r, gain_l_val, gain_r_val;
	unsigned int adc_vol_flag = 0, changed = 0;
	unsigned int lvalue, rvalue;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt722->mbq_regmap, mc->reg, &lvalue);
	regmap_read(rt722->mbq_regmap, mc->rreg, &rvalue);

	/* L Channel */
	gain_l_val = ucontrol->value.integer.value[0];
	if (gain_l_val > mc->max)
		gain_l_val = mc->max;

	if (mc->shift == 8) /* boost gain */
		gain_l_val = gain_l_val * tendB;
	else {
		/* ADC/DAC gain */
		if (adc_vol_flag)
			gain_l_val = 0x1e00 - ((mc->max - gain_l_val) * interval_offset);
		else
			gain_l_val = 0 - ((mc->max - gain_l_val) * interval_offset);
		gain_l_val &= 0xffff;
	}

	/* R Channel */
	gain_r_val = ucontrol->value.integer.value[1];
	if (gain_r_val > mc->max)
		gain_r_val = mc->max;

	if (mc->shift == 8) /* boost gain */
		gain_r_val = gain_r_val * tendB;
	else {
		/* ADC/DAC gain */
		if (adc_vol_flag)
			gain_r_val = 0x1e00 - ((mc->max - gain_r_val) * interval_offset);
		else
			gain_r_val = 0 - ((mc->max - gain_r_val) * interval_offset);
		gain_r_val &= 0xffff;
	}

	if (lvalue != gain_l_val || rvalue != gain_r_val)
		changed = 1;
	else
		return 0;

	/* Lch*/
	regmap_write(rt722->mbq_regmap, mc->reg, gain_l_val);

	/* Rch */
	regmap_write(rt722->mbq_regmap, mc->rreg, gain_r_val);

	regmap_read(rt722->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt722->mbq_regmap, mc->rreg, &read_r);
	if (read_r == gain_r_val && read_l == gain_l_val)
		return changed;

	return -EIO;
}

static int rt722_sdca_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int read_l, read_r, ctl_l = 0, ctl_r = 0;
	unsigned int adc_vol_flag = 0;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt722->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt722->mbq_regmap, mc->rreg, &read_r);

	if (mc->shift == 8) /* boost gain */
		ctl_l = read_l / tendB;
	else {
		if (adc_vol_flag)
			ctl_l = mc->max - (((0x1e00 - read_l) & 0xffff) / interval_offset);
		else
			ctl_l = mc->max - (((0 - read_l) & 0xffff) / interval_offset);
	}

	if (read_l != read_r) {
		if (mc->shift == 8) /* boost gain */
			ctl_r = read_r / tendB;
		else { /* ADC/DAC gain */
			if (adc_vol_flag)
				ctl_r = mc->max - (((0x1e00 - read_r) & 0xffff) / interval_offset);
			else
				ctl_r = mc->max - (((0 - read_r) & 0xffff) / interval_offset);
		}
	} else {
		ctl_r = ctl_l;
	}

	ucontrol->value.integer.value[0] = ctl_l;
	ucontrol->value.integer.value[1] = ctl_r;

	return 0;
}

static int rt722_sdca_set_fu1e_capture_ctl(struct rt722_sdca_priv *rt722)
{
	int err, i;
	unsigned int ch_mute;

	for (i = 0; i < ARRAY_SIZE(rt722->fu1e_mixer_mute); i++) {
		ch_mute = rt722->fu1e_dapm_mute || rt722->fu1e_mixer_mute[i];
		err = regmap_write(rt722->regmap,
				SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_USER_FU1E,
				RT722_SDCA_CTL_FU_MUTE, CH_01) + i, ch_mute);
		if (err < 0)
			return err;
	}

	return 0;
}

static int rt722_sdca_fu1e_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct rt722_sdca_dmic_kctrl_priv *p =
		(struct rt722_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	unsigned int i;

	for (i = 0; i < p->count; i++)
		ucontrol->value.integer.value[i] = !rt722->fu1e_mixer_mute[i];

	return 0;
}

static int rt722_sdca_fu1e_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct rt722_sdca_dmic_kctrl_priv *p =
		(struct rt722_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	int err, changed = 0, i;

	for (i = 0; i < p->count; i++) {
		if (rt722->fu1e_mixer_mute[i] != !ucontrol->value.integer.value[i])
			changed = 1;
		rt722->fu1e_mixer_mute[i] = !ucontrol->value.integer.value[i];
	}

	err = rt722_sdca_set_fu1e_capture_ctl(rt722);
	if (err < 0)
		return err;

	return changed;
}

static int rt722_sdca_set_fu0f_capture_ctl(struct rt722_sdca_priv *rt722)
{
	int err;
	unsigned int ch_l, ch_r;

	ch_l = (rt722->fu0f_dapm_mute || rt722->fu0f_mixer_l_mute) ? 0x01 : 0x00;
	ch_r = (rt722->fu0f_dapm_mute || rt722->fu0f_mixer_r_mute) ? 0x01 : 0x00;

	err = regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU0F,
			RT722_SDCA_CTL_FU_MUTE, CH_L), ch_l);
	if (err < 0)
		return err;

	err = regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU0F,
			RT722_SDCA_CTL_FU_MUTE, CH_R), ch_r);
	if (err < 0)
		return err;

	return 0;
}

static int rt722_sdca_fu0f_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt722->fu0f_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt722->fu0f_mixer_r_mute;
	return 0;
}

static int rt722_sdca_fu0f_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	int err, changed = 0;

	if (rt722->fu0f_mixer_l_mute != !ucontrol->value.integer.value[0] ||
		rt722->fu0f_mixer_r_mute != !ucontrol->value.integer.value[1])
		changed = 1;

	rt722->fu0f_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt722->fu0f_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt722_sdca_set_fu0f_capture_ctl(rt722);
	if (err < 0)
		return err;

	return changed;
}

static int rt722_sdca_fu_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct rt722_sdca_dmic_kctrl_priv *p =
		(struct rt722_sdca_dmic_kctrl_priv *)kcontrol->private_value;

	if (p->max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = p->count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = p->max;
	return 0;
}

static int rt722_sdca_dmic_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct rt722_sdca_dmic_kctrl_priv *p =
		(struct rt722_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	unsigned int boost_step = 0x0a00;
	unsigned int vol_max = 0x1e00;
	unsigned int regvalue, ctl, i;
	unsigned int adc_vol_flag = 0;
	const unsigned int interval_offset = 0xc0;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume"))
		adc_vol_flag = 1;

	/* check all channels */
	for (i = 0; i < p->count; i++) {
		regmap_read(rt722->mbq_regmap, p->reg_base + i, &regvalue);

		if (!adc_vol_flag) /* boost gain */
			ctl = regvalue / boost_step;
		else /* ADC gain */
			ctl = p->max - (((vol_max - regvalue) & 0xffff) / interval_offset);

		ucontrol->value.integer.value[i] = ctl;
	}

	return 0;
}

static int rt722_sdca_dmic_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt722_sdca_dmic_kctrl_priv *p =
		(struct rt722_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned int boost_step = 0x0a00;
	unsigned int vol_max = 0x1e00;
	unsigned int gain_val[4];
	unsigned int i, adc_vol_flag = 0, changed = 0;
	unsigned int regvalue[4];
	const unsigned int interval_offset = 0xc0;
	int err;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume"))
		adc_vol_flag = 1;

	/* check all channels */
	for (i = 0; i < p->count; i++) {
		regmap_read(rt722->mbq_regmap, p->reg_base + i, &regvalue[i]);

		gain_val[i] = ucontrol->value.integer.value[i];
		if (gain_val[i] > p->max)
			gain_val[i] = p->max;

		if (!adc_vol_flag) /* boost gain */
			gain_val[i] = gain_val[i] * boost_step;
		else { /* ADC gain */
			gain_val[i] = vol_max - ((p->max - gain_val[i]) * interval_offset);
			gain_val[i] &= 0xffff;
		}

		if (regvalue[i] != gain_val[i])
			changed = 1;
	}

	if (!changed)
		return 0;

	for (i = 0; i < p->count; i++) {
		err = regmap_write(rt722->mbq_regmap, p->reg_base + i, gain_val[i]);
		if (err < 0)
			dev_err(&rt722->slave->dev, "%s: %#08x can't be set\n",
				__func__, p->reg_base + i);
	}

	return changed;
}

#define RT722_SDCA_PR_VALUE(xreg_base, xcount, xmax, xinvert) \
	((unsigned long)&(struct rt722_sdca_dmic_kctrl_priv) \
		{.reg_base = xreg_base, .count = xcount, .max = xmax, \
		.invert = xinvert})

#define RT722_SDCA_FU_CTRL(xname, reg_base, xmax, xinvert, xcount) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = rt722_sdca_fu_info, \
	.get = rt722_sdca_fu1e_capture_get, \
	.put = rt722_sdca_fu1e_capture_put, \
	.private_value = RT722_SDCA_PR_VALUE(reg_base, xcount, xmax, xinvert)}

#define RT722_SDCA_EXT_TLV(xname, reg_base, xhandler_get,\
	 xhandler_put, xcount, xmax, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = rt722_sdca_fu_info, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = RT722_SDCA_PR_VALUE(reg_base, xcount, xmax, 0) }

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(boost_vol_tlv, 0, 1000, 0);

static const struct snd_kcontrol_new rt722_sdca_controls[] = {
	/* Headphone playback settings */
	SOC_DOUBLE_R_EXT_TLV("FU05 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
			RT722_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
			RT722_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x57, 0,
		rt722_sdca_set_gain_get, rt722_sdca_set_gain_put, out_vol_tlv),
	/* Headset mic capture settings */
	SOC_DOUBLE_EXT("FU0F Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt722_sdca_fu0f_capture_get, rt722_sdca_fu0f_capture_put),
	SOC_DOUBLE_R_EXT_TLV("FU0F Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU0F,
			RT722_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU0F,
			RT722_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x3f, 0,
		rt722_sdca_set_gain_get, rt722_sdca_set_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU33 Boost Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PLATFORM_FU44,
			RT722_SDCA_CTL_FU_CH_GAIN, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PLATFORM_FU44,
			RT722_SDCA_CTL_FU_CH_GAIN, CH_R), 8, 3, 0,
		rt722_sdca_set_gain_get, rt722_sdca_set_gain_put, boost_vol_tlv),
	/* AMP playback settings */
	SOC_DOUBLE_R_EXT_TLV("FU06 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
			RT722_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
			RT722_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x57, 0,
		rt722_sdca_set_gain_get, rt722_sdca_set_gain_put, out_vol_tlv),
	/* DMIC capture settings */
	RT722_SDCA_FU_CTRL("FU1E Capture Switch",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_USER_FU1E,
			RT722_SDCA_CTL_FU_MUTE, CH_01), 1, 1, 4),
	RT722_SDCA_EXT_TLV("FU1E Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_USER_FU1E,
			RT722_SDCA_CTL_FU_VOLUME, CH_01),
		rt722_sdca_dmic_set_gain_get, rt722_sdca_dmic_set_gain_put,
			4, 0x3f, mic_vol_tlv),
	RT722_SDCA_EXT_TLV("FU15 Boost Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_FU15,
			RT722_SDCA_CTL_FU_CH_GAIN, CH_01),
		rt722_sdca_dmic_set_gain_get, rt722_sdca_dmic_set_gain_put,
			4, 3, boost_vol_tlv),
};

static int rt722_sdca_adc_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, mask_sft;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 12;
	else if (strstr(ucontrol->id.name, "ADC 24 Mux"))
		mask_sft = 4;
	else if (strstr(ucontrol->id.name, "ADC 25 Mux"))
		mask_sft = 0;
	else
		return -EINVAL;

	rt722_sdca_index_read(rt722, RT722_VENDOR_HDA_CTL,
		RT722_HDA_LEGACY_MUX_CTL0, &val);

	ucontrol->value.enumerated.item[0] = (val >> mask_sft) & 0x7;

	return 0;
}

static int rt722_sdca_adc_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, mask_sft;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 12;
	else if (strstr(ucontrol->id.name, "ADC 24 Mux"))
		mask_sft = 4;
	else if (strstr(ucontrol->id.name, "ADC 25 Mux"))
		mask_sft = 0;
	else
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	rt722_sdca_index_read(rt722, RT722_VENDOR_HDA_CTL,
		RT722_HDA_LEGACY_MUX_CTL0, &val2);
	val2 = (0x7 << mask_sft) & val2;

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change)
		rt722_sdca_index_update_bits(rt722, RT722_VENDOR_HDA_CTL,
			RT722_HDA_LEGACY_MUX_CTL0, 0x7 << mask_sft,
			val << mask_sft);

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
		item[0], e, NULL);

	return change;
}

static const char * const adc22_mux_text[] = {
	"MIC2",
	"LINE1",
	"LINE2",
};

static const char * const adc07_10_mux_text[] = {
	"DMIC1",
	"DMIC2",
};

static SOC_ENUM_SINGLE_DECL(
	rt722_adc22_enum, SND_SOC_NOPM, 0, adc22_mux_text);

static SOC_ENUM_SINGLE_DECL(
	rt722_adc24_enum, SND_SOC_NOPM, 0, adc07_10_mux_text);

static SOC_ENUM_SINGLE_DECL(
	rt722_adc25_enum, SND_SOC_NOPM, 0, adc07_10_mux_text);

static const struct snd_kcontrol_new rt722_sdca_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt722_adc22_enum,
			rt722_sdca_adc_mux_get, rt722_sdca_adc_mux_put);

static const struct snd_kcontrol_new rt722_sdca_adc24_mux =
	SOC_DAPM_ENUM_EXT("ADC 24 Mux", rt722_adc24_enum,
			rt722_sdca_adc_mux_get, rt722_sdca_adc_mux_put);

static const struct snd_kcontrol_new rt722_sdca_adc25_mux =
	SOC_DAPM_ENUM_EXT("ADC 25 Mux", rt722_adc25_enum,
			rt722_sdca_adc_mux_get, rt722_sdca_adc_mux_put);

static int rt722_sdca_fu42_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
				RT722_SDCA_CTL_FU_MUTE, CH_L), unmute);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
				RT722_SDCA_CTL_FU_MUTE, CH_R), unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
				RT722_SDCA_CTL_FU_MUTE, CH_L), mute);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_USER_FU05,
				RT722_SDCA_CTL_FU_MUTE, CH_R), mute);
		break;
	}
	return 0;
}

static int rt722_sdca_fu21_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
				RT722_SDCA_CTL_FU_MUTE, CH_L), unmute);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
				RT722_SDCA_CTL_FU_MUTE, CH_R), unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
				RT722_SDCA_CTL_FU_MUTE, CH_L), mute);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_USER_FU06,
				RT722_SDCA_CTL_FU_MUTE, CH_R), mute);
		break;
	}
	return 0;
}

static int rt722_sdca_fu113_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt722->fu1e_dapm_mute = false;
		rt722_sdca_set_fu1e_capture_ctl(rt722);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt722->fu1e_dapm_mute = true;
		rt722_sdca_set_fu1e_capture_ctl(rt722);
		break;
	}
	return 0;
}

static int rt722_sdca_fu36_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt722->fu0f_dapm_mute = false;
		rt722_sdca_set_fu0f_capture_ctl(rt722);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt722->fu0f_dapm_mute = true;
		rt722_sdca_set_fu0f_capture_ctl(rt722);
		break;
	}
	return 0;
}

static int rt722_sdca_pde47_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PDE40,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PDE40,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt722_sdca_pde23_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_PDE23,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_PDE23,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt722_sdca_pde11_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_PDE2A,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_PDE2A,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt722_sdca_pde12_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PDE12,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_PDE12,
				RT722_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt722_sdca_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),
	SND_SOC_DAPM_INPUT("DMIC1_2"),
	SND_SOC_DAPM_INPUT("DMIC3_4"),

	SND_SOC_DAPM_SUPPLY("PDE 23", SND_SOC_NOPM, 0, 0,
		rt722_sdca_pde23_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 47", SND_SOC_NOPM, 0, 0,
		rt722_sdca_pde47_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 11", SND_SOC_NOPM, 0, 0,
		rt722_sdca_pde11_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 12", SND_SOC_NOPM, 0, 0,
		rt722_sdca_pde12_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_DAC_E("FU 21", NULL, SND_SOC_NOPM, 0, 0,
		rt722_sdca_fu21_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU 42", NULL, SND_SOC_NOPM, 0, 0,
		rt722_sdca_fu42_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 36", NULL, SND_SOC_NOPM, 0, 0,
		rt722_sdca_fu36_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 113", NULL, SND_SOC_NOPM, 0, 0,
		rt722_sdca_fu113_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt722_sdca_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 24 Mux", SND_SOC_NOPM, 0, 0,
		&rt722_sdca_adc24_mux),
	SND_SOC_DAPM_MUX("ADC 25 Mux", SND_SOC_NOPM, 0, 0,
		&rt722_sdca_adc25_mux),

	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Headphone Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Headset Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Speaker Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP6TX", "DP6 DMic Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt722_sdca_audio_map[] = {
	{"FU 42", NULL, "DP1RX"},
	{"FU 21", NULL, "DP3RX"},

	{"ADC 22 Mux", "MIC2", "MIC2"},
	{"ADC 22 Mux", "LINE1", "LINE1"},
	{"ADC 22 Mux", "LINE2", "LINE2"},
	{"ADC 24 Mux", "DMIC1", "DMIC1_2"},
	{"ADC 24 Mux", "DMIC2", "DMIC3_4"},
	{"ADC 25 Mux", "DMIC1", "DMIC1_2"},
	{"ADC 25 Mux", "DMIC2", "DMIC3_4"},
	{"FU 36", NULL, "PDE 12"},
	{"FU 36", NULL, "ADC 22 Mux"},
	{"FU 113", NULL, "PDE 11"},
	{"FU 113", NULL, "ADC 24 Mux"},
	{"FU 113", NULL, "ADC 25 Mux"},
	{"DP2TX", NULL, "FU 36"},
	{"DP6TX", NULL, "FU 113"},

	{"HP", NULL, "PDE 47"},
	{"HP", NULL, "FU 42"},
	{"SPK", NULL, "PDE 23"},
	{"SPK", NULL, "FU 21"},
};

static int rt722_sdca_parse_dt(struct rt722_sdca_priv *rt722, struct device *dev)
{
	device_property_read_u32(dev, "realtek,jd-src", &rt722->jd_src);

	return 0;
}

static int rt722_sdca_probe(struct snd_soc_component *component)
{
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	int ret;

	rt722_sdca_parse_dt(rt722, &rt722->slave->dev);
	rt722->component = component;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_sdca_dev_rt722 = {
	.probe = rt722_sdca_probe,
	.controls = rt722_sdca_controls,
	.num_controls = ARRAY_SIZE(rt722_sdca_controls),
	.dapm_widgets = rt722_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt722_sdca_dapm_widgets),
	.dapm_routes = rt722_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt722_sdca_audio_map),
	.set_jack = rt722_sdca_set_jack_detect,
	.endianness = 1,
};

static int rt722_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt722_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt722_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_runtime *sdw_stream;
	int retval, port, num_channels;
	unsigned int sampling_rate;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!sdw_stream)
		return -EINVAL;

	if (!rt722->slave)
		return -EINVAL;

	/*
	 * RT722_AIF1 with port = 1 for headphone playback
	 * RT722_AIF1 with port = 2 for headset-mic capture
	 * RT722_AIF2 with port = 3 for speaker playback
	 * RT722_AIF3 with port = 6 for digital-mic capture
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		if (dai->id == RT722_AIF1)
			port = 1;
		else if (dai->id == RT722_AIF2)
			port = 3;
		else
			return -EINVAL;
	} else {
		direction = SDW_DATA_DIR_TX;
		if (dai->id == RT722_AIF1)
			port = 2;
		else if (dai->id == RT722_AIF3)
			port = 6;
		else
			return -EINVAL;
	}
	stream_config.frame_rate = params_rate(params);
	stream_config.ch_count = params_channels(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	num_channels = params_channels(params);
	port_config.ch_mask = GENMASK(num_channels - 1, 0);
	port_config.num = port;

	retval = sdw_stream_add_slave(rt722->slave, &stream_config,
					&port_config, 1, sdw_stream);
	if (retval) {
		dev_err(dai->dev, "%s: Unable to configure port\n", __func__);
		return retval;
	}

	if (params_channels(params) > 16) {
		dev_err(component->dev, "%s: Unsupported channels %d\n",
			__func__, params_channels(params));
		return -EINVAL;
	}

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 44100:
		sampling_rate = RT722_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT722_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT722_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT722_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	if (dai->id == RT722_AIF1) {
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_CS01,
				RT722_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT722_SDCA_ENT_CS11,
				RT722_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);
	}

	if (dai->id == RT722_AIF2)
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_CS31,
				RT722_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);

	if (dai->id == RT722_AIF3)
		regmap_write(rt722->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_CS1F,
				RT722_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);

	return 0;
}

static int rt722_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt722_sdca_priv *rt722 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt722->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt722->slave, sdw_stream);
	return 0;
}

#define RT722_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define RT722_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt722_sdca_ops = {
	.hw_params	= rt722_sdca_pcm_hw_params,
	.hw_free	= rt722_sdca_pcm_hw_free,
	.set_stream	= rt722_sdca_set_sdw_stream,
	.shutdown	= rt722_sdca_shutdown,
};

static struct snd_soc_dai_driver rt722_sdca_dai[] = {
	{
		.name = "rt722-sdca-aif1",
		.id = RT722_AIF1,
		.playback = {
			.stream_name = "DP1 Headphone Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT722_STEREO_RATES,
			.formats = RT722_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Headset Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT722_STEREO_RATES,
			.formats = RT722_FORMATS,
		},
		.ops = &rt722_sdca_ops,
	},
	{
		.name = "rt722-sdca-aif2",
		.id = RT722_AIF2,
		.playback = {
			.stream_name = "DP3 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT722_STEREO_RATES,
			.formats = RT722_FORMATS,
		},
		.ops = &rt722_sdca_ops,
	},
	{
		.name = "rt722-sdca-aif3",
		.id = RT722_AIF3,
		.capture = {
			.stream_name = "DP6 DMic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT722_STEREO_RATES,
			.formats = RT722_FORMATS,
		},
		.ops = &rt722_sdca_ops,
	}
};

int rt722_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave)
{
	struct rt722_sdca_priv *rt722;

	rt722 = devm_kzalloc(dev, sizeof(*rt722), GFP_KERNEL);
	if (!rt722)
		return -ENOMEM;

	dev_set_drvdata(dev, rt722);
	rt722->slave = slave;
	rt722->regmap = regmap;
	rt722->mbq_regmap = mbq_regmap;

	mutex_init(&rt722->calibrate_mutex);
	mutex_init(&rt722->disable_irq_lock);

	INIT_DELAYED_WORK(&rt722->jack_detect_work, rt722_sdca_jack_detect_handler);
	INIT_DELAYED_WORK(&rt722->jack_btn_check_work, rt722_sdca_btn_check_handler);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt722->hw_init = false;
	rt722->first_hw_init = false;
	rt722->fu1e_dapm_mute = true;
	rt722->fu0f_dapm_mute = true;
	rt722->fu0f_mixer_l_mute = rt722->fu0f_mixer_r_mute = true;
	rt722->fu1e_mixer_mute[0] = rt722->fu1e_mixer_mute[1] =
		rt722->fu1e_mixer_mute[2] = rt722->fu1e_mixer_mute[3] = true;

	return devm_snd_soc_register_component(dev,
			&soc_sdca_dev_rt722, rt722_sdca_dai, ARRAY_SIZE(rt722_sdca_dai));
}

static void rt722_sdca_dmic_preset(struct rt722_sdca_priv *rt722)
{
	/* Set AD07 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_ADC0A_08_PDE_FLOAT_CTL, 0x2a29);
	/* Set AD10 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_ADC10_PDE_FLOAT_CTL, 0x2a00);
	/* Set DMIC1/DMIC2 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_DMIC1_2_PDE_FLOAT_CTL, 0x2a2a);
	/* Set DMIC2 IT entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_DMIC_ENT_FLOAT_CTL, 0x2626);
	/* Set AD10 FU entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_ADC_ENT_FLOAT_CTL, 0x1e00);
	/* Set DMIC2 FU entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_DMIC_GAIN_ENT_FLOAT_CTL0, 0x1515);
	/* Set AD10 FU channel floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_ADC_VOL_CH_FLOAT_CTL, 0x0304);
	/* Set DMIC2 FU channel floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_DMIC_GAIN_ENT_FLOAT_CTL2, 0x0304);
	/* vf71f_r12_07_06 and vf71f_r13_07_06 = 2’b00 */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL,
		RT722_HDA_LEGACY_CONFIG_CTL0, 0x0000);
	/* Enable vf707_r12_05/vf707_r13_05 */
	regmap_write(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT722_SDCA_ENT_IT26,
			RT722_SDCA_CTL_VENDOR_DEF, 0), 0x01);
	/* Fine tune PDE2A latency */
	regmap_write(rt722->regmap, 0x2f5c, 0x25);
}

static void rt722_sdca_amp_preset(struct rt722_sdca_priv *rt722)
{
	/* Set DVQ=01 */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_CLSD_CTRL6,
		0xc215);
	/* Reset dc_cal_top */
	rt722_sdca_index_write(rt722, RT722_VENDOR_CALI, RT722_DC_CALIB_CTRL,
		0x702c);
	/* W1C Trigger Calibration */
	rt722_sdca_index_write(rt722, RT722_VENDOR_CALI, RT722_DC_CALIB_CTRL,
		0xf02d);
	/* Set DAC02/ClassD power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_AMP_PDE_FLOAT_CTL,
		0x2323);
	/* Set EAPD high */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_EAPD_CTL,
		0x0002);
	/* Enable vf707_r14 */
	regmap_write(rt722->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT722_SDCA_ENT_OT23,
			RT722_SDCA_CTL_VENDOR_DEF, CH_08), 0x04);
}

static void rt722_sdca_jack_preset(struct rt722_sdca_priv *rt722)
{
	int loop_check, chk_cnt = 100, ret;
	unsigned int calib_status = 0;

	/* Config analog bias */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_ANALOG_BIAS_CTL3,
		0xa081);
	/* GE related settings */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_GE_RELATED_CTL2,
		0xa009);
	/* Button A, B, C, D bypass mode */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_UMP_HID_CTL4,
		0xcf00);
	/* HID1 slot enable */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_UMP_HID_CTL5,
		0x000f);
	/* Report ID for HID1 */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_UMP_HID_CTL0,
		0x1100);
	/* OSC/OOC for slot 2, 3 */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_UMP_HID_CTL7,
		0x0c12);
	/* Set JD de-bounce clock control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_JD_CTRL1,
		0x7002);
	/* Set DVQ=01 */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_CLSD_CTRL6,
		0xc215);
	/* FSM switch to calibration manual mode */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_FSM_CTL,
		0x4100);
	/* W1C Trigger DC calibration (HP) */
	rt722_sdca_index_write(rt722, RT722_VENDOR_CALI, RT722_DAC_DC_CALI_CTL3,
		0x008d);
	/* check HP calibration FSM status */
	for (loop_check = 0; loop_check < chk_cnt; loop_check++) {
		usleep_range(10000, 11000);
		ret = rt722_sdca_index_read(rt722, RT722_VENDOR_CALI,
			RT722_DAC_DC_CALI_CTL3, &calib_status);
		if (ret < 0)
			dev_dbg(&rt722->slave->dev, "calibration failed!, ret=%d\n", ret);
		if ((calib_status & 0x0040) == 0x0)
			break;
	}

	if (loop_check == chk_cnt)
		dev_dbg(&rt722->slave->dev, "%s, calibration time-out!\n", __func__);

	/* Set ADC09 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_ADC0A_08_PDE_FLOAT_CTL,
		0x2a12);
	/* Set MIC2 and LINE1 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_MIC2_LINE2_PDE_FLOAT_CTL,
		0x3429);
	/* Set ET41h and LINE2 power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_ET41_LINE2_PDE_FLOAT_CTL,
		0x4112);
	/* Set DAC03 and HP power entity floating control */
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_DAC03_HP_PDE_FLOAT_CTL,
		0x4040);
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_ENT_FLOAT_CTRL_1,
		0x4141);
	rt722_sdca_index_write(rt722, RT722_VENDOR_HDA_CTL, RT722_FLOAT_CTRL_1,
		0x0101);
	/* Fine tune PDE40 latency */
	regmap_write(rt722->regmap, 0x2f58, 0x07);
	regmap_write(rt722->regmap, 0x2f03, 0x06);
	/* MIC VRefo */
	rt722_sdca_index_update_bits(rt722, RT722_VENDOR_REG,
		RT722_COMBO_JACK_AUTO_CTL1, 0x0200, 0x0200);
	rt722_sdca_index_update_bits(rt722, RT722_VENDOR_REG,
		RT722_VREFO_GAT, 0x4000, 0x4000);
	/* Release HP-JD, EN_CBJ_TIE_GL/R open, en_osw gating auto done bit */
	rt722_sdca_index_write(rt722, RT722_VENDOR_REG, RT722_DIGITAL_MISC_CTRL4,
		0x0010);
}

int rt722_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt722_sdca_priv *rt722 = dev_get_drvdata(dev);

	rt722->disable_irq = false;

	if (rt722->hw_init)
		return 0;

	if (rt722->first_hw_init) {
		regcache_cache_only(rt722->regmap, false);
		regcache_cache_bypass(rt722->regmap, true);
		regcache_cache_only(rt722->mbq_regmap, false);
		regcache_cache_bypass(rt722->mbq_regmap, true);
	} else {
		/*
		 * PM runtime is only enabled when a Slave reports as Attached
		 */

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

	rt722_sdca_dmic_preset(rt722);
	rt722_sdca_amp_preset(rt722);
	rt722_sdca_jack_preset(rt722);

	if (rt722->first_hw_init) {
		regcache_cache_bypass(rt722->regmap, false);
		regcache_mark_dirty(rt722->regmap);
		regcache_cache_bypass(rt722->mbq_regmap, false);
		regcache_mark_dirty(rt722->mbq_regmap);
	} else
		rt722->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt722->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

MODULE_DESCRIPTION("ASoC RT722 SDCA SDW driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL");
