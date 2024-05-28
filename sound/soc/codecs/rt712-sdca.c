// SPDX-License-Identifier: GPL-2.0-only
//
// rt712-sdca.c -- rt712 SDCA ALSA SoC audio driver
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
#include <linux/pm_runtime.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/slab.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include "rt712-sdca.h"

static int rt712_sdca_index_write(struct rt712_sdca_priv *rt712,
		unsigned int nid, unsigned int reg, unsigned int value)
{
	int ret;
	struct regmap *regmap = rt712->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt712->slave->dev,
			"%s: Failed to set private value: %06x <= %04x ret=%d\n",
			__func__, addr, value, ret);

	return ret;
}

static int rt712_sdca_index_read(struct rt712_sdca_priv *rt712,
		unsigned int nid, unsigned int reg, unsigned int *value)
{
	int ret;
	struct regmap *regmap = rt712->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;

	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt712->slave->dev,
			"%s: Failed to get private value: %06x => %04x ret=%d\n",
			__func__, addr, *value, ret);

	return ret;
}

static int rt712_sdca_index_update_bits(struct rt712_sdca_priv *rt712,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	int ret;

	ret = rt712_sdca_index_read(rt712, nid, reg, &tmp);
	if (ret < 0)
		return ret;

	set_mask_bits(&tmp, mask, val);
	return rt712_sdca_index_write(rt712, nid, reg, tmp);
}

static int rt712_sdca_calibration(struct rt712_sdca_priv *rt712)
{
	unsigned int val, loop_rc = 0, loop_dc = 0;
	struct device *dev;
	struct regmap *regmap = rt712->regmap;
	int chk_cnt = 100;
	int ret = 0;

	mutex_lock(&rt712->calibrate_mutex);
	dev = regmap_get_device(regmap);

	/* Set HP-JD source from JD1 */
	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_CC_DET1, 0x043a);

	/* FSM switch to calibration manual mode */
	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_FSM_CTL, 0x4100);

	/* Calibration setting */
	rt712_sdca_index_write(rt712, RT712_VENDOR_CALI, RT712_DAC_DC_CALI_CTL1, 0x7883);

	/* W1C Trigger DC calibration (HP & Class-D) */
	rt712_sdca_index_write(rt712, RT712_VENDOR_CALI, RT712_DAC_DC_CALI_CTL1, 0xf893);

	/* wait for calibration process */
	rt712_sdca_index_read(rt712, RT712_VENDOR_CALI,
		RT712_DAC_DC_CALI_CTL1, &val);

	for (loop_dc = 0; loop_dc < chk_cnt &&
		(val & RT712_DAC_DC_CALI_TRIGGER); loop_dc++) {
		usleep_range(10000, 11000);
		ret = rt712_sdca_index_read(rt712, RT712_VENDOR_CALI,
			RT712_DAC_DC_CALI_CTL1, &val);
		if (ret < 0)
			goto _cali_fail_;
	}
	if (loop_dc == chk_cnt)
		dev_err(dev, "%s, calibration time-out!\n", __func__);

	if (loop_dc == chk_cnt || loop_rc == chk_cnt)
		ret = -ETIMEDOUT;

_cali_fail_:
	/* Enable Rldet in FSM */
	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_FSM_CTL, 0x4500);

	/* Sensing Lch+Rch */
	rt712_sdca_index_write(rt712, RT712_VENDOR_IMS_DRE, RT712_IMS_DIGITAL_CTL1, 0x040f);

	/* Sine gen path control */
	rt712_sdca_index_write(rt712, RT712_VENDOR_IMS_DRE, RT712_IMS_DIGITAL_CTL5, 0x0000);

	/* Release HP-JD, EN_CBJ_TIE_GL/R open, en_osw gating auto done bit */
	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_DIGITAL_MISC_CTRL4, 0x0010);

	mutex_unlock(&rt712->calibrate_mutex);
	dev_dbg(dev, "%s calibration complete, ret=%d\n", __func__, ret);
	return ret;
}

static unsigned int rt712_sdca_button_detect(struct rt712_sdca_priv *rt712)
{
	unsigned int btn_type = 0, offset, idx, val, owner;
	int ret;
	unsigned char buf[3];

	/* get current UMP message owner */
	ret = regmap_read(rt712->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT712_SDCA_ENT_HID01, RT712_SDCA_CTL_HIDTX_CURRENT_OWNER, 0),
		&owner);
	if (ret < 0)
		return 0;

	/* if owner is device then there is no button event from device */
	if (owner == 1)
		return 0;

	/* read UMP message offset */
	ret = regmap_read(rt712->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT712_SDCA_ENT_HID01, RT712_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0),
		&offset);
	if (ret < 0)
		goto _end_btn_det_;

	for (idx = 0; idx < sizeof(buf); idx++) {
		ret = regmap_read(rt712->regmap,
			RT712_BUF_ADDR_HID1 + offset + idx, &val);
		if (ret < 0)
			goto _end_btn_det_;
		buf[idx] = val & 0xff;
	}

	if (buf[0] == 0x11) {
		switch (buf[1] & 0xf0) {
		case 0x10:
			btn_type |= SND_JACK_BTN_2;
			break;
		case 0x20:
			btn_type |= SND_JACK_BTN_3;
			break;
		case 0x40:
			btn_type |= SND_JACK_BTN_0;
			break;
		case 0x80:
			btn_type |= SND_JACK_BTN_1;
			break;
		}
		switch (buf[2]) {
		case 0x01:
		case 0x10:
			btn_type |= SND_JACK_BTN_2;
			break;
		case 0x02:
		case 0x20:
			btn_type |= SND_JACK_BTN_3;
			break;
		case 0x04:
		case 0x40:
			btn_type |= SND_JACK_BTN_0;
			break;
		case 0x08:
		case 0x80:
			btn_type |= SND_JACK_BTN_1;
			break;
		}
	}

_end_btn_det_:
	/* Host is owner, so set back to device */
	if (owner == 0)
		/* set owner to device */
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT712_SDCA_ENT_HID01,
				RT712_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE, 0), 0x01);

	return btn_type;
}

static int rt712_sdca_headset_detect(struct rt712_sdca_priv *rt712)
{
	unsigned int det_mode;
	int ret;

	/* get detected_mode */
	ret = regmap_read(rt712->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_GE49, RT712_SDCA_CTL_DETECTED_MODE, 0),
		&det_mode);
	if (ret < 0)
		goto io_error;

	switch (det_mode) {
	case 0x00:
		rt712->jack_type = 0;
		break;
	case 0x03:
		rt712->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		rt712->jack_type = SND_JACK_HEADSET;
		break;
	}

	/* write selected_mode */
	if (det_mode) {
		ret = regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_GE49, RT712_SDCA_CTL_SELECTED_MODE, 0),
			det_mode);
		if (ret < 0)
			goto io_error;
	}

	dev_dbg(&rt712->slave->dev,
		"%s, detected_mode=0x%x\n", __func__, det_mode);

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static void rt712_sdca_jack_detect_handler(struct work_struct *work)
{
	struct rt712_sdca_priv *rt712 =
		container_of(work, struct rt712_sdca_priv, jack_detect_work.work);
	int btn_type = 0, ret;

	if (!rt712->hs_jack)
		return;

	if (!rt712->component->card || !rt712->component->card->instantiated)
		return;

	/* SDW_SCP_SDCA_INT_SDCA_0 is used for jack detection */
	if (rt712->scp_sdca_stat1 & SDW_SCP_SDCA_INT_SDCA_0) {
		ret = rt712_sdca_headset_detect(rt712);
		if (ret < 0)
			return;
	}

	/* SDW_SCP_SDCA_INT_SDCA_8 is used for button detection */
	if (rt712->scp_sdca_stat2 & SDW_SCP_SDCA_INT_SDCA_8)
		btn_type = rt712_sdca_button_detect(rt712);

	if (rt712->jack_type == 0)
		btn_type = 0;

	dev_dbg(&rt712->slave->dev,
		"in %s, jack_type=0x%x\n", __func__, rt712->jack_type);
	dev_dbg(&rt712->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);
	dev_dbg(&rt712->slave->dev,
		"in %s, scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
		rt712->scp_sdca_stat1, rt712->scp_sdca_stat2);

	snd_soc_jack_report(rt712->hs_jack, rt712->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt712->hs_jack, rt712->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt712->jack_btn_check_work, msecs_to_jiffies(200));
	}
}

static void rt712_sdca_btn_check_handler(struct work_struct *work)
{
	struct rt712_sdca_priv *rt712 =
		container_of(work, struct rt712_sdca_priv, jack_btn_check_work.work);
	int btn_type = 0, ret, idx;
	unsigned int det_mode, offset, val;
	unsigned char buf[3];

	ret = regmap_read(rt712->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_GE49, RT712_SDCA_CTL_DETECTED_MODE, 0),
		&det_mode);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (det_mode) {
		/* read UMP message offset */
		ret = regmap_read(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT712_SDCA_ENT_HID01, RT712_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0),
			&offset);
		if (ret < 0)
			goto io_error;

		for (idx = 0; idx < sizeof(buf); idx++) {
			ret = regmap_read(rt712->regmap,
				RT712_BUF_ADDR_HID1 + offset + idx, &val);
			if (ret < 0)
				goto io_error;
			buf[idx] = val & 0xff;
		}

		if (buf[0] == 0x11) {
			switch (buf[1] & 0xf0) {
			case 0x10:
				btn_type |= SND_JACK_BTN_2;
				break;
			case 0x20:
				btn_type |= SND_JACK_BTN_3;
				break;
			case 0x40:
				btn_type |= SND_JACK_BTN_0;
				break;
			case 0x80:
				btn_type |= SND_JACK_BTN_1;
				break;
			}
			switch (buf[2]) {
			case 0x01:
			case 0x10:
				btn_type |= SND_JACK_BTN_2;
				break;
			case 0x02:
			case 0x20:
				btn_type |= SND_JACK_BTN_3;
				break;
			case 0x04:
			case 0x40:
				btn_type |= SND_JACK_BTN_0;
				break;
			case 0x08:
			case 0x80:
				btn_type |= SND_JACK_BTN_1;
				break;
			}
		}
	} else {
		rt712->jack_type = 0;
	}

	dev_dbg(&rt712->slave->dev, "%s, btn_type=0x%x\n",	__func__, btn_type);
	snd_soc_jack_report(rt712->hs_jack, rt712->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt712->hs_jack, rt712->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt712->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt712_sdca_jack_init(struct rt712_sdca_priv *rt712)
{
	mutex_lock(&rt712->calibrate_mutex);

	if (rt712->hs_jack) {
		/* Enable HID1 event & set button RTC mode */
		rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL,
			RT712_UMP_HID_CTL5, 0xfff0);
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_UMP_HID_CTL0, 0x1100, 0x1100);
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_UMP_HID_CTL7, 0xf000, 0x0000);

		/* detected_mode_change_event_en & hid1_push_button_event_en */
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_GE_RELATED_CTL1, 0x0c00, 0x0c00);
		/* ge_inbox_en */
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_GE_RELATED_CTL2, 0x0020, 0x0000);

		switch (rt712->jd_src) {
		case RT712_JD1:
			/* Set HP-JD source from JD1 */
			rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_CC_DET1, 0x043a);
			break;
		default:
			dev_warn(rt712->component->dev, "Wrong JD source\n");
			break;
		}

		/* set SCP_SDCA_IntMask1[0]=1 */
		sdw_write_no_pm(rt712->slave, SDW_SCP_SDCA_INTMASK1, SDW_SCP_SDCA_INTMASK_SDCA_0);
		/* set SCP_SDCA_IntMask2[0]=1 */
		sdw_write_no_pm(rt712->slave, SDW_SCP_SDCA_INTMASK2, SDW_SCP_SDCA_INTMASK_SDCA_8);
		dev_dbg(&rt712->slave->dev, "in %s enable\n", __func__);

		/* trigger GE interrupt */
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_GE_RELATED_CTL1, 0x0080, 0x0080);
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_GE_RELATED_CTL1, 0x0080, 0x0000);
	} else {
		/* disable HID1 & detected_mode_change event */
		rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
			RT712_GE_RELATED_CTL1, 0x0c00, 0x0000);

		dev_dbg(&rt712->slave->dev, "in %s disable\n", __func__);
	}

	mutex_unlock(&rt712->calibrate_mutex);
}

static int rt712_sdca_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	int ret;

	rt712->hs_jack = hs_jack;

	if (!rt712->first_hw_init)
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

	rt712_sdca_jack_init(rt712);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

/* For SDCA control DAC/ADC Gain */
static int rt712_sdca_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned int read_l, read_r, gain_l_val, gain_r_val;
	unsigned int adc_vol_flag = 0;
	unsigned int lvalue, rvalue;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt712->mbq_regmap, mc->reg, &lvalue);
	regmap_read(rt712->mbq_regmap, mc->rreg, &rvalue);

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

	if (lvalue == gain_l_val && rvalue == gain_r_val)
		return 0;

	/* Lch*/
	regmap_write(rt712->mbq_regmap, mc->reg, gain_l_val);
	/* Rch */
	regmap_write(rt712->mbq_regmap, mc->rreg, gain_r_val);

	regmap_read(rt712->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt712->mbq_regmap, mc->rreg, &read_r);
	if (read_r == gain_r_val && read_l == gain_l_val)
		return 1;

	return -EIO;
}

static int rt712_sdca_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int read_l, read_r, ctl_l = 0, ctl_r = 0;
	unsigned int adc_vol_flag = 0;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt712->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt712->mbq_regmap, mc->rreg, &read_r);

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
	} else
		ctl_r = ctl_l;

	ucontrol->value.integer.value[0] = ctl_l;
	ucontrol->value.integer.value[1] = ctl_r;

	return 0;
}

static int rt712_sdca_set_fu0f_capture_ctl(struct rt712_sdca_priv *rt712)
{
	int err;
	unsigned int ch_l, ch_r;

	ch_l = (rt712->fu0f_dapm_mute || rt712->fu0f_mixer_l_mute) ? 0x01 : 0x00;
	ch_r = (rt712->fu0f_dapm_mute || rt712->fu0f_mixer_r_mute) ? 0x01 : 0x00;

	err = regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU0F,
			RT712_SDCA_CTL_FU_MUTE, CH_L), ch_l);
	if (err < 0)
		return err;

	err = regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU0F,
			RT712_SDCA_CTL_FU_MUTE, CH_R), ch_r);
	if (err < 0)
		return err;

	return 0;
}

static int rt712_sdca_fu0f_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt712->fu0f_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt712->fu0f_mixer_r_mute;
	return 0;
}

static int rt712_sdca_fu0f_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	int err;

	if (rt712->fu0f_mixer_l_mute == !ucontrol->value.integer.value[0] &&
		rt712->fu0f_mixer_r_mute == !ucontrol->value.integer.value[1])
		return 0;

	rt712->fu0f_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt712->fu0f_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt712_sdca_set_fu0f_capture_ctl(rt712);
	if (err < 0)
		return err;

	return 1;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(boost_vol_tlv, 0, 1000, 0);

static const struct snd_kcontrol_new rt712_sdca_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("FU05 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05, RT712_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05, RT712_SDCA_CTL_FU_VOLUME, CH_R),
		0, 0x57, 0,
		rt712_sdca_set_gain_get, rt712_sdca_set_gain_put, out_vol_tlv),
	SOC_DOUBLE_EXT("FU0F Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt712_sdca_fu0f_capture_get, rt712_sdca_fu0f_capture_put),
	SOC_DOUBLE_R_EXT_TLV("FU0F Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU0F, RT712_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU0F, RT712_SDCA_CTL_FU_VOLUME, CH_R),
		0, 0x3f, 0,
		rt712_sdca_set_gain_get, rt712_sdca_set_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU44 Boost Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PLATFORM_FU44, RT712_SDCA_CTL_FU_CH_GAIN, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PLATFORM_FU44, RT712_SDCA_CTL_FU_CH_GAIN, CH_R),
		8, 3, 0,
		rt712_sdca_set_gain_get, rt712_sdca_set_gain_put, boost_vol_tlv),
};

static const struct snd_kcontrol_new rt712_sdca_spk_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("FU06 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_USER_FU06, RT712_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_USER_FU06, RT712_SDCA_CTL_FU_VOLUME, CH_R),
		0, 0x57, 0,
		rt712_sdca_set_gain_get, rt712_sdca_set_gain_put, out_vol_tlv),
};

static int rt712_sdca_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, mask = 0x3300;

	rt712_sdca_index_read(rt712, RT712_VENDOR_HDA_CTL, RT712_MIXER_CTL1, &val);

	val = val & mask;
	switch (val) {
	case 0x3000:
		val = 1;
		break;
	case 0x0300:
		val = 0;
		break;
	}

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int rt712_sdca_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int mask_sft;
	unsigned int val;

	if (item[0] >= e->items)
		return -EINVAL;

	if (ucontrol->value.enumerated.item[0] == 0)
		mask_sft = 12;
	else if (ucontrol->value.enumerated.item[0] == 1)
		mask_sft = 8;
	else
		return -EINVAL;

	rt712_sdca_index_read(rt712, RT712_VENDOR_HDA_CTL, RT712_MIXER_CTL1, &val);
	val = (val >> mask_sft) & 0x3;
	if (!val)
		return 0;

	rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL,
		RT712_MIXER_CTL1, 0x3fff);
	rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
		RT712_MIXER_CTL1, 0x3 << mask_sft, 0);

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
		item[0], e, NULL);

	return 1;
}

static const char * const adc_mux_text[] = {
	"MIC2",
	"LINE2",
};

static SOC_ENUM_SINGLE_DECL(
	rt712_adc23_enum, SND_SOC_NOPM, 0, adc_mux_text);

static const struct snd_kcontrol_new rt712_sdca_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt712_adc23_enum,
			rt712_sdca_mux_get, rt712_sdca_mux_put);

static int rt712_sdca_fu05_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05,
				RT712_SDCA_CTL_FU_MUTE, CH_L),
				unmute);
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05,
				RT712_SDCA_CTL_FU_MUTE, CH_R),
				unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05,
				RT712_SDCA_CTL_FU_MUTE, CH_L),
				mute);
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_USER_FU05,
				RT712_SDCA_CTL_FU_MUTE, CH_R),
				mute);
		break;
	}
	return 0;
}

static int rt712_sdca_fu0f_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt712->fu0f_dapm_mute = false;
		rt712_sdca_set_fu0f_capture_ctl(rt712);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt712->fu0f_dapm_mute = true;
		rt712_sdca_set_fu0f_capture_ctl(rt712);
		break;
	}
	return 0;
}

static int rt712_sdca_pde40_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PDE40,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PDE40,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static int rt712_sdca_pde12_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PDE12,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_PDE12,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static int rt712_sdca_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_PDE23,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_PDE23,
				RT712_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;

	default:
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new rt712_spk_sto_dac =
	SOC_DAPM_DOUBLE_R("Switch",
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_USER_FU06, RT712_SDCA_CTL_FU_MUTE, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_USER_FU06, RT712_SDCA_CTL_FU_MUTE, CH_R),
		0, 1, 1);

static const struct snd_soc_dapm_widget rt712_sdca_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE2"),

	SND_SOC_DAPM_SUPPLY("PDE 40", SND_SOC_NOPM, 0, 0,
		rt712_sdca_pde40_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 12", SND_SOC_NOPM, 0, 0,
		rt712_sdca_pde12_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_DAC_E("FU 05", NULL, SND_SOC_NOPM, 0, 0,
		rt712_sdca_fu05_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 0F", NULL, SND_SOC_NOPM, 0, 0,
		rt712_sdca_fu0f_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt712_sdca_adc23_mux),

	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt712_sdca_audio_map[] = {
	{ "FU 05", NULL, "DP1RX" },
	{ "DP4TX", NULL, "FU 0F" },

	{ "FU 0F", NULL, "PDE 12" },
	{ "FU 0F", NULL, "ADC 23 Mux" },
	{ "ADC 23 Mux", "LINE2", "LINE2" },
	{ "ADC 23 Mux", "MIC2", "MIC2" },

	{ "HP", NULL, "PDE 40" },
	{ "HP", NULL, "FU 05" },
};

static const struct snd_soc_dapm_widget rt712_sdca_spk_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SWITCH("FU06", SND_SOC_NOPM, 0, 0, &rt712_spk_sto_dac),

	/* Output */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt712_sdca_classd_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
};

static const struct snd_soc_dapm_route rt712_sdca_spk_dapm_routes[] = {
	{ "FU06", "Switch", "DP3RX" },
	{ "CLASS D", NULL, "FU06" },
	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },
};

static int rt712_sdca_parse_dt(struct rt712_sdca_priv *rt712, struct device *dev)
{
	device_property_read_u32(dev, "realtek,jd-src", &rt712->jd_src);

	return 0;
}

static int rt712_sdca_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	int ret;

	rt712_sdca_parse_dt(rt712, &rt712->slave->dev);
	rt712->component = component;

	/* add SPK route */
	if (rt712->hw_id != RT712_DEV_ID_713) {
		snd_soc_add_component_controls(component,
			rt712_sdca_spk_controls, ARRAY_SIZE(rt712_sdca_spk_controls));
		snd_soc_dapm_new_controls(dapm,
			rt712_sdca_spk_dapm_widgets, ARRAY_SIZE(rt712_sdca_spk_dapm_widgets));
		snd_soc_dapm_add_routes(dapm,
			rt712_sdca_spk_dapm_routes, ARRAY_SIZE(rt712_sdca_spk_dapm_routes));
	}

	if (!rt712->first_hw_init)
		return 0;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_sdca_dev_rt712 = {
	.probe = rt712_sdca_probe,
	.controls = rt712_sdca_controls,
	.num_controls = ARRAY_SIZE(rt712_sdca_controls),
	.dapm_widgets = rt712_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt712_sdca_dapm_widgets),
	.dapm_routes = rt712_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt712_sdca_audio_map),
	.set_jack = rt712_sdca_set_jack_detect,
	.endianness = 1,
};

static int rt712_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt712_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt712_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
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

	if (!rt712->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		if (dai->id == RT712_AIF1)
			port = 1;
		else if (dai->id == RT712_AIF2)
			port = 3;
		else
			return -EINVAL;
	} else {
		direction = SDW_DATA_DIR_TX;
		if (dai->id == RT712_AIF1)
			port = 4;
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

	retval = sdw_stream_add_slave(rt712->slave, &stream_config,
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
		sampling_rate = RT712_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT712_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT712_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT712_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	switch (dai->id) {
	case RT712_AIF1:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_CS01, RT712_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
			sampling_rate);
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_CS11, RT712_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
			sampling_rate);
		break;
	case RT712_AIF2:
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_CS31, RT712_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
			sampling_rate);
		break;
	default:
		dev_err(component->dev, "%s: Wrong DAI id\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int rt712_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt712_sdca_priv *rt712 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt712->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt712->slave, sdw_stream);
	return 0;
}

#define RT712_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_192000)
#define RT712_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt712_sdca_ops = {
	.hw_params	= rt712_sdca_pcm_hw_params,
	.hw_free	= rt712_sdca_pcm_hw_free,
	.set_stream	= rt712_sdca_set_sdw_stream,
	.shutdown	= rt712_sdca_shutdown,
};

static struct snd_soc_dai_driver rt712_sdca_dai[] = {
	{
		.name = "rt712-sdca-aif1",
		.id = RT712_AIF1,
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT712_STEREO_RATES,
			.formats = RT712_FORMATS,
		},
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT712_STEREO_RATES,
			.formats = RT712_FORMATS,
		},
		.ops = &rt712_sdca_ops,
	},
	{
		.name = "rt712-sdca-aif2",
		.id = RT712_AIF2,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT712_STEREO_RATES,
			.formats = RT712_FORMATS,
		},
		.ops = &rt712_sdca_ops,
	}
};

int rt712_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave)
{
	struct rt712_sdca_priv *rt712;
	int ret;

	rt712 = devm_kzalloc(dev, sizeof(*rt712), GFP_KERNEL);
	if (!rt712)
		return -ENOMEM;

	dev_set_drvdata(dev, rt712);
	rt712->slave = slave;
	rt712->regmap = regmap;
	rt712->mbq_regmap = mbq_regmap;

	regcache_cache_only(rt712->regmap, true);
	regcache_cache_only(rt712->mbq_regmap, true);

	mutex_init(&rt712->calibrate_mutex);
	mutex_init(&rt712->disable_irq_lock);

	INIT_DELAYED_WORK(&rt712->jack_detect_work, rt712_sdca_jack_detect_handler);
	INIT_DELAYED_WORK(&rt712->jack_btn_check_work, rt712_sdca_btn_check_handler);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt712->hw_init = false;
	rt712->first_hw_init = false;
	rt712->fu0f_dapm_mute = true;
	rt712->fu0f_mixer_l_mute = rt712->fu0f_mixer_r_mute = true;

	/* JD source uses JD1 in default */
	rt712->jd_src = RT712_JD1;

	if (slave->id.part_id != RT712_PART_ID_713)
		ret =  devm_snd_soc_register_component(dev,
				&soc_sdca_dev_rt712, rt712_sdca_dai, ARRAY_SIZE(rt712_sdca_dai));
	else
		ret =  devm_snd_soc_register_component(dev,
				&soc_sdca_dev_rt712, rt712_sdca_dai, 1);
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

	return 0;
}

int rt712_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt712_sdca_priv *rt712 = dev_get_drvdata(dev);
	int ret = 0;
	unsigned int val, hibernation_flag;

	rt712->disable_irq = false;

	if (rt712->hw_init)
		return 0;

	regcache_cache_only(rt712->regmap, false);
	regcache_cache_only(rt712->mbq_regmap, false);
	if (rt712->first_hw_init) {
		regcache_cache_bypass(rt712->regmap, true);
		regcache_cache_bypass(rt712->mbq_regmap, true);
	} else {
		/*
		 *  PM runtime status is marked as 'active' only when a Slave reports as Attached
		 */

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	rt712_sdca_index_read(rt712, RT712_VENDOR_REG, RT712_JD_PRODUCT_NUM, &val);
	rt712->hw_id = (val & 0xf000) >> 12;

	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_ANALOG_BIAS_CTL3, 0xaa81);
	rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_LDO2_3_CTL1, 0xa1e0);
	rt712_sdca_index_write(rt712, RT712_VENDOR_IMS_DRE, RT712_HP_DETECT_RLDET_CTL1, 0x0000);
	rt712_sdca_index_write(rt712, RT712_VENDOR_IMS_DRE, RT712_HP_DETECT_RLDET_CTL2, 0x0000);
	rt712_sdca_index_write(rt712, RT712_VENDOR_ANALOG_CTL, RT712_MISC_POWER_CTL7, 0x0000);
	regmap_write(rt712->regmap, RT712_RC_CAL, 0x23);

	/* calibration */
	rt712_sdca_index_read(rt712, RT712_VENDOR_REG, RT712_SW_CONFIG1, &hibernation_flag);
	if (!hibernation_flag) {
		ret = rt712_sdca_calibration(rt712);
		if (ret < 0)
			dev_err(dev, "%s, calibration failed!\n", __func__);
	}

	rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
		RT712_MIXER_CTL1, 0x3000, 0x0000);
	rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL,
		RT712_ADC0A_08_PDE_FLOAT_CTL, 0x1112);
	rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL,
		RT712_MIC2_LINE2_PDE_FLOAT_CTL, 0x3412);
	rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL,
		RT712_DAC03_HP_PDE_FLOAT_CTL, 0x4040);

	rt712_sdca_index_update_bits(rt712, RT712_VENDOR_HDA_CTL,
		RT712_HDA_LEGACY_GPIO_WAKE_EN_CTL, 0x0001, 0x0000);
	regmap_write(rt712->regmap, 0x2f50, 0x00);
	regmap_write(rt712->regmap, 0x2f54, 0x00);
	regmap_write(rt712->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT712_SDCA_ENT_IT09, RT712_SDCA_CTL_VENDOR_DEF, 0), 0x01);

	/* add SPK settings */
	if (rt712->hw_id != RT712_DEV_ID_713) {
		rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL, RT712_AMP_PDE_FLOAT_CTL, 0x2323);
		rt712_sdca_index_write(rt712, RT712_VENDOR_HDA_CTL, RT712_EAPD_CTL, 0x0002);
		regmap_write(rt712->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT712_SDCA_ENT_OT23, RT712_SDCA_CTL_VENDOR_DEF, 0), 0x04);
	}

	/*
	 * if set_jack callback occurred early than io_init,
	 * we set up the jack detection function now
	 */
	if (rt712->hs_jack)
		rt712_sdca_jack_init(rt712);

	if (!hibernation_flag)
		rt712_sdca_index_write(rt712, RT712_VENDOR_REG, RT712_SW_CONFIG1, 0x0001);

	if (rt712->first_hw_init) {
		regcache_cache_bypass(rt712->regmap, false);
		regcache_mark_dirty(rt712->regmap);
		regcache_cache_bypass(rt712->mbq_regmap, false);
		regcache_mark_dirty(rt712->mbq_regmap);
	} else
		rt712->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt712->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

MODULE_DESCRIPTION("ASoC RT712 SDCA SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
