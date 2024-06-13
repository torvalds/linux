// SPDX-License-Identifier: GPL-2.0-only
//
// rt711-sdca.c -- rt711 SDCA ALSA SoC audio driver
//
// Copyright(c) 2021 Realtek Semiconductor Corp.
//
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/jack.h>

#include "rt711-sdca.h"

static int rt711_sdca_index_write(struct rt711_sdca_priv *rt711,
		unsigned int nid, unsigned int reg, unsigned int value)
{
	int ret;
	struct regmap *regmap = rt711->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt711->slave->dev,
			"Failed to set private value: %06x <= %04x ret=%d\n",
			addr, value, ret);

	return ret;
}

static int rt711_sdca_index_read(struct rt711_sdca_priv *rt711,
		unsigned int nid, unsigned int reg, unsigned int *value)
{
	int ret;
	struct regmap *regmap = rt711->mbq_regmap;
	unsigned int addr = (nid << 20) | reg;

	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt711->slave->dev,
			"Failed to get private value: %06x => %04x ret=%d\n",
			addr, *value, ret);

	return ret;
}

static int rt711_sdca_index_update_bits(struct rt711_sdca_priv *rt711,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	int ret;

	ret = rt711_sdca_index_read(rt711, nid, reg, &tmp);
	if (ret < 0)
		return ret;

	set_mask_bits(&tmp, mask, val);
	return rt711_sdca_index_write(rt711, nid, reg, tmp);
}

static void rt711_sdca_reset(struct rt711_sdca_priv *rt711)
{
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_PARA_VERB_CTL, RT711_HIDDEN_REG_SW_RESET,
		RT711_HIDDEN_REG_SW_RESET);
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_RESET_CTL, 0x1, 0x1);
}

static int rt711_sdca_calibration(struct rt711_sdca_priv *rt711)
{
	unsigned int val, loop_rc = 0, loop_dc = 0;
	struct device *dev;
	struct regmap *regmap = rt711->regmap;
	int chk_cnt = 100;
	int ret = 0;

	mutex_lock(&rt711->calibrate_mutex);
	dev = regmap_get_device(regmap);

	regmap_read(rt711->regmap, RT711_RC_CAL_STATUS, &val);
	/* RC calibration */
	if (!(val & 0x40))
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_ANALOG_CTL,
			RT711_MISC_POWER_CTL0, 0x0010, 0x0010);

	for (loop_rc = 0; loop_rc < chk_cnt && !(val & 0x40); loop_rc++) {
		usleep_range(10000, 11000);
		ret = regmap_read(rt711->regmap, RT711_RC_CAL_STATUS, &val);
		if (ret < 0)
			goto _cali_fail_;
	}
	if (loop_rc == chk_cnt)
		dev_err(dev, "%s, RC calibration time-out!\n", __func__);

	/* HP calibration by manual mode setting */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_FSM_CTL, 0x2000, 0x2000);

	/* Calibration manual mode */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_FSM_CTL, 0xf, RT711_CALI_CTL);

	/* reset HP calibration */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, RT711_DAC_DC_FORCE_CALI_RST, 0x00);
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, RT711_DAC_DC_FORCE_CALI_RST,
		RT711_DAC_DC_FORCE_CALI_RST);

	/* cal_clk_en_reg */
	if (rt711->hw_ver == RT711_VER_VD0)
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_CALI,
			RT711_DAC_DC_CALI_CTL1, RT711_DAC_DC_CALI_CLK_EN,
			RT711_DAC_DC_CALI_CLK_EN);

	/* trigger */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, RT711_DAC_DC_CALI_TRIGGER,
		RT711_DAC_DC_CALI_TRIGGER);

	/* wait for calibration process */
	rt711_sdca_index_read(rt711, RT711_VENDOR_CALI,
		RT711_DAC_DC_CALI_CTL1, &val);

	for (loop_dc = 0; loop_dc < chk_cnt &&
		(val & RT711_DAC_DC_CALI_TRIGGER); loop_dc++) {
		usleep_range(10000, 11000);
		ret = rt711_sdca_index_read(rt711, RT711_VENDOR_CALI,
			RT711_DAC_DC_CALI_CTL1, &val);
		if (ret < 0)
			goto _cali_fail_;
	}
	if (loop_dc == chk_cnt)
		dev_err(dev, "%s, calibration time-out!\n", __func__);

	if (loop_dc == chk_cnt || loop_rc == chk_cnt)
		ret = -ETIMEDOUT;

_cali_fail_:
	/* enable impedance sense */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_FSM_CTL, RT711_FSM_IMP_EN, RT711_FSM_IMP_EN);

	/* release HP-JD and trigger FSM */
	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_DIGITAL_MISC_CTRL4, 0x201b);

	mutex_unlock(&rt711->calibrate_mutex);
	dev_dbg(dev, "%s calibration complete, ret=%d\n", __func__, ret);
	return ret;
}

static unsigned int rt711_sdca_button_detect(struct rt711_sdca_priv *rt711)
{
	unsigned int btn_type = 0, offset, idx, val, owner;
	int ret;
	unsigned char buf[3];

	/* get current UMP message owner */
	ret = regmap_read(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_CURRENT_OWNER, 0),
		&owner);
	if (ret < 0)
		return 0;

	/* if owner is device then there is no button event from device */
	if (owner == 1)
		return 0;

	/* read UMP message offset */
	ret = regmap_read(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0),
		&offset);
	if (ret < 0)
		goto _end_btn_det_;

	for (idx = 0; idx < sizeof(buf); idx++) {
		ret = regmap_read(rt711->regmap,
			RT711_BUF_ADDR_HID1 + offset + idx, &val);
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
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01,
				RT711_SDCA_CTL_HIDTX_SET_OWNER_TO_DEVICE, 0), 0x01);

	return btn_type;
}

static int rt711_sdca_headset_detect(struct rt711_sdca_priv *rt711)
{
	unsigned int det_mode;
	int ret;

	/* get detected_mode */
	ret = regmap_read(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_DETECTED_MODE, 0),
		&det_mode);
	if (ret < 0)
		goto io_error;

	switch (det_mode) {
	case 0x00:
		rt711->jack_type = 0;
		break;
	case 0x03:
		rt711->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		rt711->jack_type = SND_JACK_HEADSET;
		break;
	}

	/* write selected_mode */
	if (det_mode) {
		ret = regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_SELECTED_MODE, 0),
			det_mode);
		if (ret < 0)
			goto io_error;
	}

	dev_dbg(&rt711->slave->dev,
		"%s, detected_mode=0x%x\n", __func__, det_mode);

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static void rt711_sdca_jack_detect_handler(struct work_struct *work)
{
	struct rt711_sdca_priv *rt711 =
		container_of(work, struct rt711_sdca_priv, jack_detect_work.work);
	int btn_type = 0, ret;

	if (!rt711->hs_jack)
		return;

	if (!rt711->component->card || !rt711->component->card->instantiated)
		return;

	/* SDW_SCP_SDCA_INT_SDCA_0 is used for jack detection */
	if (rt711->scp_sdca_stat1 & SDW_SCP_SDCA_INT_SDCA_0) {
		ret = rt711_sdca_headset_detect(rt711);
		if (ret < 0)
			return;
	}

	/* SDW_SCP_SDCA_INT_SDCA_8 is used for button detection */
	if (rt711->scp_sdca_stat2 & SDW_SCP_SDCA_INT_SDCA_8)
		btn_type = rt711_sdca_button_detect(rt711);

	if (rt711->jack_type == 0)
		btn_type = 0;

	dev_dbg(&rt711->slave->dev,
		"in %s, jack_type=0x%x\n", __func__, rt711->jack_type);
	dev_dbg(&rt711->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);
	dev_dbg(&rt711->slave->dev,
		"in %s, scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
		rt711->scp_sdca_stat1, rt711->scp_sdca_stat2);

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
}

static void rt711_sdca_btn_check_handler(struct work_struct *work)
{
	struct rt711_sdca_priv *rt711 =
		container_of(work, struct rt711_sdca_priv, jack_btn_check_work.work);
	int btn_type = 0, ret, idx;
	unsigned int det_mode, offset, val;
	unsigned char buf[3];

	ret = regmap_read(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_DETECTED_MODE, 0),
		&det_mode);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (det_mode) {
		/* read UMP message offset */
		ret = regmap_read(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0),
			&offset);
		if (ret < 0)
			goto io_error;

		for (idx = 0; idx < sizeof(buf); idx++) {
			ret = regmap_read(rt711->regmap,
				RT711_BUF_ADDR_HID1 + offset + idx, &val);
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
	} else
		rt711->jack_type = 0;

	dev_dbg(&rt711->slave->dev, "%s, btn_type=0x%x\n",	__func__, btn_type);
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

static void rt711_sdca_jack_init(struct rt711_sdca_priv *rt711)
{
	mutex_lock(&rt711->calibrate_mutex);

	if (rt711->hs_jack) {
		/* Enable HID1 event & set button RTC mode */
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_PUSH_BTN_INT_CTL6, 0x80f0, 0x8000);
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_PUSH_BTN_INT_CTL2, 0x11dd, 0x11dd);
		rt711_sdca_index_write(rt711, RT711_VENDOR_HDA_CTL,
			RT711_PUSH_BTN_INT_CTL7, 0xffff);
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_PUSH_BTN_INT_CTL9, 0xf000, 0x0000);

		/* GE_mode_change_event_en & Hid1_push_button_event_en */
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_GE_MODE_RELATED_CTL, 0x0c00, 0x0c00);

		switch (rt711->jd_src) {
		case RT711_JD1:
			/* default settings was already for JD1 */
			break;
		case RT711_JD2:
			rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
				RT711_JD_CTL1, RT711_JD2_DIGITAL_MODE_SEL,
				RT711_JD2_DIGITAL_MODE_SEL);
			rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
				RT711_JD_CTL2, RT711_JD2_2PORT_200K_DECODE_HP | RT711_HP_JD_SEL_JD2,
				RT711_JD2_2PORT_200K_DECODE_HP | RT711_HP_JD_SEL_JD2);
			rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
				RT711_CC_DET1,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12,
				RT711_HP_JD_FINAL_RESULT_CTL_JD12);
			break;
		default:
			dev_warn(rt711->component->dev, "Wrong JD source\n");
			break;
		}

		/* set SCP_SDCA_IntMask1[0]=1 */
		sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INTMASK1, SDW_SCP_SDCA_INTMASK_SDCA_0);
		/* set SCP_SDCA_IntMask2[0]=1 */
		sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INTMASK2, SDW_SCP_SDCA_INTMASK_SDCA_8);
		dev_dbg(&rt711->slave->dev, "in %s enable\n", __func__);
	} else {
		/* disable HID 1/2 event */
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_GE_MODE_RELATED_CTL, 0x0c00, 0x0000);

		dev_dbg(&rt711->slave->dev, "in %s disable\n", __func__);
	}

	mutex_unlock(&rt711->calibrate_mutex);
}

static int rt711_sdca_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	int ret;

	rt711->hs_jack = hs_jack;

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

	rt711_sdca_jack_init(rt711);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

/* For SDCA control DAC/ADC Gain */
static int rt711_sdca_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned int read_l, read_r, gain_l_val, gain_r_val;
	unsigned int i, adc_vol_flag = 0, changed = 0;
	unsigned int lvalue, rvalue;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt711->mbq_regmap, mc->reg, &lvalue);
	regmap_read(rt711->mbq_regmap, mc->rreg, &rvalue);

	/* control value to 2's complement value */
	/* L Channel */
	gain_l_val = ucontrol->value.integer.value[0];
	if (gain_l_val > mc->max)
		gain_l_val = mc->max;
	read_l = gain_l_val;

	if (mc->shift == 8) /* boost gain */
		gain_l_val = (gain_l_val * 10) << mc->shift;
	else { /* ADC/DAC gain */
		if (adc_vol_flag && gain_l_val > mc->shift)
			gain_l_val = (gain_l_val - mc->shift) * 75;
		else
			gain_l_val = (mc->shift - gain_l_val) * 75;
		gain_l_val <<= 8;
		gain_l_val /= 100;
		if (!(adc_vol_flag && read_l > mc->shift)) {
			gain_l_val = ~gain_l_val;
			gain_l_val += 1;
		}
		gain_l_val &= 0xffff;
	}

	/* R Channel */
	gain_r_val = ucontrol->value.integer.value[1];
	if (gain_r_val > mc->max)
		gain_r_val = mc->max;
	read_r = gain_r_val;

	if (mc->shift == 8) /* boost gain */
		gain_r_val = (gain_r_val * 10) << mc->shift;
	else { /* ADC/DAC gain */
		if (adc_vol_flag && gain_r_val > mc->shift)
			gain_r_val = (gain_r_val - mc->shift) * 75;
		else
			gain_r_val = (mc->shift - gain_r_val) * 75;
		gain_r_val <<= 8;
		gain_r_val /= 100;
		if (!(adc_vol_flag && read_r > mc->shift)) {
			gain_r_val = ~gain_r_val;
			gain_r_val += 1;
		}
		gain_r_val &= 0xffff;
	}

	if (lvalue != gain_l_val || rvalue != gain_r_val)
		changed = 1;
	else
		return 0;

	for (i = 0; i < 3; i++) { /* retry 3 times at most */
		/* Lch*/
		regmap_write(rt711->mbq_regmap, mc->reg, gain_l_val);

		/* Rch */
		regmap_write(rt711->mbq_regmap, mc->rreg, gain_r_val);

		regmap_read(rt711->mbq_regmap, mc->reg, &read_l);
		regmap_read(rt711->mbq_regmap, mc->rreg, &read_r);
		if (read_r == gain_r_val && read_l == gain_l_val)
			break;
	}

	return i == 3 ? -EIO : changed;
}

static int rt711_sdca_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int read_l, read_r, ctl_l = 0, ctl_r = 0;
	unsigned int adc_vol_flag = 0, neg_flag = 0;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt711->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt711->mbq_regmap, mc->rreg, &read_r);

	/* 2's complement value to control value */
	if (mc->shift == 8) /* boost gain */
		ctl_l = (read_l >> mc->shift) / 10;
	else { /* ADC/DAC gain */
		ctl_l = read_l;
		if (read_l & BIT(15)) {
			ctl_l = 0xffff & ~(read_l - 1);
			neg_flag = 1;
		}
		ctl_l *= 100;
		ctl_l >>= 8;
		if (adc_vol_flag) {
			if (neg_flag)
				ctl_l = mc->shift - (ctl_l / 75);
			else
				ctl_l = mc->shift + (ctl_l / 75);
		} else
			ctl_l = mc->max - (ctl_l / 75);
	}

	neg_flag = 0;
	if (read_l != read_r) {
		if (mc->shift == 8) /* boost gain */
			ctl_r = (read_r >> mc->shift) / 10;
		else { /* ADC/DAC gain */
			ctl_r = read_r;
			if (read_r & BIT(15)) {
				ctl_r = 0xffff & ~(read_r - 1);
				neg_flag = 1;
			}
			ctl_r *= 100;
			ctl_r >>= 8;
			if (adc_vol_flag) {
				if (neg_flag)
					ctl_r = mc->shift - (ctl_r / 75);
				else
					ctl_r = mc->shift + (ctl_r / 75);
			} else
				ctl_r = mc->max - (ctl_r / 75);
		}
	} else
		ctl_r = ctl_l;

	ucontrol->value.integer.value[0] = ctl_l;
	ucontrol->value.integer.value[1] = ctl_r;

	return 0;
}

static int rt711_sdca_set_fu0f_capture_ctl(struct rt711_sdca_priv *rt711)
{
	int err;
	unsigned int ch_l, ch_r;

	ch_l = (rt711->fu0f_dapm_mute || rt711->fu0f_mixer_l_mute) ? 0x01 : 0x00;
	ch_r = (rt711->fu0f_dapm_mute || rt711->fu0f_mixer_r_mute) ? 0x01 : 0x00;

	err = regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F,
			RT711_SDCA_CTL_FU_MUTE, CH_L), ch_l);
	if (err < 0)
		return err;

	err = regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F,
			RT711_SDCA_CTL_FU_MUTE, CH_R), ch_r);
	if (err < 0)
		return err;

	return 0;
}

static int rt711_sdca_set_fu1e_capture_ctl(struct rt711_sdca_priv *rt711)
{
	int err;
	unsigned int ch_l, ch_r;

	ch_l = (rt711->fu1e_dapm_mute || rt711->fu1e_mixer_l_mute) ? 0x01 : 0x00;
	ch_r = (rt711->fu1e_dapm_mute || rt711->fu1e_mixer_r_mute) ? 0x01 : 0x00;

	err = regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E,
			RT711_SDCA_CTL_FU_MUTE, CH_L), ch_l);
	if (err < 0)
		return err;

	err = regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E,
			RT711_SDCA_CTL_FU_MUTE, CH_R), ch_r);
	if (err < 0)
		return err;

	return 0;
}

static int rt711_sdca_fu1e_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt711->fu1e_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt711->fu1e_mixer_r_mute;
	return 0;
}

static int rt711_sdca_fu1e_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	int err, changed = 0;

	if (rt711->fu1e_mixer_l_mute != !ucontrol->value.integer.value[0] ||
		rt711->fu1e_mixer_r_mute != !ucontrol->value.integer.value[1])
		changed = 1;

	rt711->fu1e_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt711->fu1e_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt711_sdca_set_fu1e_capture_ctl(rt711);
	if (err < 0)
		return err;

	return changed;
}

static int rt711_sdca_fu0f_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt711->fu0f_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt711->fu0f_mixer_r_mute;
	return 0;
}

static int rt711_sdca_fu0f_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	int err, changed = 0;

	if (rt711->fu0f_mixer_l_mute != !ucontrol->value.integer.value[0] ||
		rt711->fu0f_mixer_r_mute != !ucontrol->value.integer.value[1])
		changed = 1;

	rt711->fu0f_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt711->fu0f_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt711_sdca_set_fu0f_capture_ctl(rt711);
	if (err < 0)
		return err;

	return changed;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);

static const struct snd_kcontrol_new rt711_sdca_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("FU05 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05, RT711_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05, RT711_SDCA_CTL_FU_VOLUME, CH_R),
		0x57, 0x57, 0,
		rt711_sdca_set_gain_get, rt711_sdca_set_gain_put, out_vol_tlv),
	SOC_DOUBLE_EXT("FU1E Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt711_sdca_fu1e_capture_get, rt711_sdca_fu1e_capture_put),
	SOC_DOUBLE_EXT("FU0F Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt711_sdca_fu0f_capture_get, rt711_sdca_fu0f_capture_put),
	SOC_DOUBLE_R_EXT_TLV("FU1E Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E, RT711_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E, RT711_SDCA_CTL_FU_VOLUME, CH_R),
		0x17, 0x3f, 0,
		rt711_sdca_set_gain_get, rt711_sdca_set_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0F Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F, RT711_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F, RT711_SDCA_CTL_FU_VOLUME, CH_R),
		0x17, 0x3f, 0,
		rt711_sdca_set_gain_get, rt711_sdca_set_gain_put, in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU44 Gain Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PLATFORM_FU44, RT711_SDCA_CTL_FU_CH_GAIN, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PLATFORM_FU44, RT711_SDCA_CTL_FU_CH_GAIN, CH_R),
		8, 3, 0,
		rt711_sdca_set_gain_get, rt711_sdca_set_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU15 Gain Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PLATFORM_FU15, RT711_SDCA_CTL_FU_CH_GAIN, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PLATFORM_FU15, RT711_SDCA_CTL_FU_CH_GAIN, CH_R),
		8, 3, 0,
		rt711_sdca_set_gain_get, rt711_sdca_set_gain_put, mic_vol_tlv),
};

static int rt711_sdca_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, mask_sft;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 10;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		mask_sft = 13;
	else
		return -EINVAL;

	rt711_sdca_index_read(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_MUX_CTL1, &val);

	ucontrol->value.enumerated.item[0] = (val >> mask_sft) & 0x7;

	return 0;
}

static int rt711_sdca_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, mask_sft;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 10;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		mask_sft = 13;
	else
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	rt711_sdca_index_read(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_MUX_CTL1, &val2);
	val2 = (val2 >> mask_sft) & 0x7;

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change)
		rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
			RT711_HDA_LEGACY_MUX_CTL1, 0x7 << mask_sft,
			val << mask_sft);

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

static const struct snd_kcontrol_new rt711_sdca_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt711_adc22_enum,
			rt711_sdca_mux_get, rt711_sdca_mux_put);

static const struct snd_kcontrol_new rt711_sdca_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt711_adc23_enum,
			rt711_sdca_mux_get, rt711_sdca_mux_put);

static int rt711_sdca_fu05_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05,
				RT711_SDCA_CTL_FU_MUTE, CH_L),
				unmute);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05,
				RT711_SDCA_CTL_FU_MUTE, CH_R),
				unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05,
				RT711_SDCA_CTL_FU_MUTE, CH_L),
				mute);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05,
				RT711_SDCA_CTL_FU_MUTE, CH_R),
				mute);
		break;
	}
	return 0;
}

static int rt711_sdca_fu0f_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt711->fu0f_dapm_mute = false;
		rt711_sdca_set_fu0f_capture_ctl(rt711);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt711->fu0f_dapm_mute = true;
		rt711_sdca_set_fu0f_capture_ctl(rt711);
		break;
	}
	return 0;
}

static int rt711_sdca_fu1e_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt711->fu1e_dapm_mute = false;
		rt711_sdca_set_fu1e_capture_ctl(rt711);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt711->fu1e_dapm_mute = true;
		rt711_sdca_set_fu1e_capture_ctl(rt711);
		break;
	}
	return 0;
}

static int rt711_sdca_pde28_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDE28,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDE28,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static int rt711_sdca_pde29_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDE29,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDE29,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static int rt711_sdca_pde2a_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PDE2A,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PDE2A,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static int rt711_sdca_line1_power_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	static unsigned int sel_mode = 0xffff;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_read(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49,
				RT711_SDCA_CTL_SELECTED_MODE, 0),
				&sel_mode);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_LINE1,
				RT711_SDCA_CTL_VENDOR_DEF, 0),
				0x1);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49,
				RT711_SDCA_CTL_SELECTED_MODE, 0),
				0x7);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_LINE1,
				RT711_SDCA_CTL_VENDOR_DEF, 0),
				0x0);
		if (sel_mode != 0xffff)
			regmap_write(rt711->regmap,
				SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49,
				RT711_SDCA_CTL_SELECTED_MODE, 0),
				sel_mode);
		break;
	}

	return 0;
}

static int rt711_sdca_line2_power_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDELINE2,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_LINE2,
				RT711_SDCA_CTL_VENDOR_DEF, 0),
				0x1);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_LINE2,
				RT711_SDCA_CTL_VENDOR_DEF, 0),
				0x0);
		regmap_write(rt711->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PDELINE2,
				RT711_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt711_sdca_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),

	SND_SOC_DAPM_PGA_E("LINE1 Power", SND_SOC_NOPM,
		0, 0, NULL, 0, rt711_sdca_line1_power_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("LINE2 Power", SND_SOC_NOPM,
		0, 0, NULL, 0, rt711_sdca_line2_power_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY("PDE 28", SND_SOC_NOPM, 0, 0,
		rt711_sdca_pde28_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 29", SND_SOC_NOPM, 0, 0,
		rt711_sdca_pde29_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 2A", SND_SOC_NOPM, 0, 0,
		rt711_sdca_pde2a_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_DAC_E("FU 05", NULL, SND_SOC_NOPM, 0, 0,
		rt711_sdca_fu05_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 0F", NULL, SND_SOC_NOPM, 0, 0,
		rt711_sdca_fu0f_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 1E", NULL, SND_SOC_NOPM, 0, 0,
		rt711_sdca_fu1e_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt711_sdca_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt711_sdca_adc23_mux),

	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt711_sdca_audio_map[] = {
	{"FU 05", NULL, "DP3RX"},
	{"DP2TX", NULL, "FU 0F"},
	{"DP4TX", NULL, "FU 1E"},

	{"LINE1 Power", NULL, "LINE1"},
	{"LINE2 Power", NULL, "LINE2"},
	{"HP", NULL, "PDE 28"},
	{"FU 0F", NULL, "PDE 29"},
	{"FU 1E", NULL, "PDE 2A"},

	{"FU 0F", NULL, "ADC 22 Mux"},
	{"FU 1E", NULL, "ADC 23 Mux"},
	{"ADC 22 Mux", "DMIC", "DMIC1"},
	{"ADC 22 Mux", "LINE1", "LINE1 Power"},
	{"ADC 22 Mux", "LINE2", "LINE2 Power"},
	{"ADC 22 Mux", "MIC2", "MIC2"},
	{"ADC 23 Mux", "DMIC", "DMIC2"},
	{"ADC 23 Mux", "LINE1", "LINE1 Power"},
	{"ADC 23 Mux", "LINE2", "LINE2 Power"},
	{"ADC 23 Mux", "MIC2", "MIC2"},

	{"HP", NULL, "FU 05"},
};

static int rt711_sdca_parse_dt(struct rt711_sdca_priv *rt711, struct device *dev)
{
	device_property_read_u32(dev, "realtek,jd-src", &rt711->jd_src);

	return 0;
}

static int rt711_sdca_probe(struct snd_soc_component *component)
{
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	int ret;

	rt711_sdca_parse_dt(rt711, &rt711->slave->dev);
	rt711->component = component;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_sdca_dev_rt711 = {
	.probe = rt711_sdca_probe,
	.controls = rt711_sdca_snd_controls,
	.num_controls = ARRAY_SIZE(rt711_sdca_snd_controls),
	.dapm_widgets = rt711_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt711_sdca_dapm_widgets),
	.dapm_routes = rt711_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt711_sdca_audio_map),
	.set_jack = rt711_sdca_set_jack_detect,
	.endianness = 1,
};

static int rt711_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	struct sdw_stream_data *stream;

	if (!sdw_stream)
		return 0;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->sdw_stream = sdw_stream;

	/* Use tx_mask or rx_mask to configure stream tag and set dma_data */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = stream;
	else
		dai->capture_dma_data = stream;

	return 0;
}

static void rt711_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt711_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_data *stream;
	int retval, port, num_channels;
	unsigned int sampling_rate;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt711->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		port = 3;
	} else {
		direction = SDW_DATA_DIR_TX;
		if (dai->id == RT711_AIF1)
			port = 2;
		else if (dai->id == RT711_AIF2)
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

	retval = sdw_stream_add_slave(rt711->slave, &stream_config,
					&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	if (params_channels(params) > 16) {
		dev_err(component->dev, "Unsupported channels %d\n",
			params_channels(params));
		return -EINVAL;
	}

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 44100:
		sampling_rate = RT711_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT711_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT711_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT711_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "Rate %d is not supported\n",
			params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	regmap_write(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_CS01, RT711_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
		sampling_rate);
	regmap_write(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_CS11, RT711_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
		sampling_rate);
	regmap_write(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_CS1F, RT711_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
		sampling_rate);

	return 0;
}

static int rt711_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt711_sdca_priv *rt711 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt711->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt711->slave, stream->sdw_stream);
	return 0;
}

#define RT711_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_192000)
#define RT711_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt711_sdca_ops = {
	.hw_params	= rt711_sdca_pcm_hw_params,
	.hw_free	= rt711_sdca_pcm_hw_free,
	.set_stream	= rt711_sdca_set_sdw_stream,
	.shutdown	= rt711_sdca_shutdown,
};

static struct snd_soc_dai_driver rt711_sdca_dai[] = {
	{
		.name = "rt711-sdca-aif1",
		.id = RT711_AIF1,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.ops = &rt711_sdca_ops,
	},
	{
		.name = "rt711-sdca-aif2",
		.id = RT711_AIF2,
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT711_STEREO_RATES,
			.formats = RT711_FORMATS,
		},
		.ops = &rt711_sdca_ops,
	}
};

int rt711_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave)
{
	struct rt711_sdca_priv *rt711;
	int ret;

	rt711 = devm_kzalloc(dev, sizeof(*rt711), GFP_KERNEL);
	if (!rt711)
		return -ENOMEM;

	dev_set_drvdata(dev, rt711);
	rt711->slave = slave;
	rt711->regmap = regmap;
	rt711->mbq_regmap = mbq_regmap;

	mutex_init(&rt711->calibrate_mutex);
	mutex_init(&rt711->disable_irq_lock);

	INIT_DELAYED_WORK(&rt711->jack_detect_work, rt711_sdca_jack_detect_handler);
	INIT_DELAYED_WORK(&rt711->jack_btn_check_work, rt711_sdca_btn_check_handler);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt711->hw_init = false;
	rt711->first_hw_init = false;
	rt711->fu0f_dapm_mute = true;
	rt711->fu1e_dapm_mute = true;
	rt711->fu0f_mixer_l_mute = rt711->fu0f_mixer_r_mute = true;
	rt711->fu1e_mixer_l_mute = rt711->fu1e_mixer_r_mute = true;

	/* JD source uses JD2 in default */
	rt711->jd_src = RT711_JD2;

	ret =  devm_snd_soc_register_component(dev,
			&soc_sdca_dev_rt711,
			rt711_sdca_dai,
			ARRAY_SIZE(rt711_sdca_dai));

	dev_dbg(&slave->dev, "%s\n", __func__);

	return ret;
}

static void rt711_sdca_vd0_io_init(struct rt711_sdca_priv *rt711)
{
	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_GPIO_TEST_MODE_CTL2, 0x0e00);
	rt711_sdca_index_write(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_GPIO_CTL, 0x0008);

	regmap_write(rt711->regmap, 0x2f5a, 0x01);

	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_ADC27_VOL_SET, 0x8728);

	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_COMBO_JACK_AUTO_CTL3, 0xa472);

	regmap_write(rt711->regmap, 0x2f50, 0x02);

	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_ANALOG_CTL,
		RT711_MISC_POWER_CTL4, 0x6000, 0x6000);

	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_COMBO_JACK_AUTO_CTL3, 0x000c, 0x000c);

	rt711_sdca_index_write(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_CONFIG_CTL, 0x0000);

	rt711_sdca_index_write(rt711, RT711_VENDOR_VAD,
		RT711_VAD_SRAM_CTL1, 0x0050);
}

static void rt711_sdca_vd1_io_init(struct rt711_sdca_priv *rt711)
{
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_UNSOLICITED_CTL, 0x0300, 0x0000);

	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_COMBO_JACK_AUTO_CTL3, 0xa43e);

	regmap_write(rt711->regmap, 0x2f5a, 0x05);

	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_JD_CTRL6, 0x0500);

	rt711_sdca_index_write(rt711, RT711_VENDOR_REG,
		RT711_DMIC_CTL1, 0x6173);

	rt711_sdca_index_write(rt711, RT711_VENDOR_HDA_CTL,
		RT711_HDA_LEGACY_CONFIG_CTL, 0x0000);

	rt711_sdca_index_write(rt711, RT711_VENDOR_VAD,
		RT711_VAD_SRAM_CTL1, 0x0050);
}

int rt711_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(dev);
	int ret = 0;
	unsigned int val;

	rt711->disable_irq = false;

	if (rt711->hw_init)
		return 0;

	if (rt711->first_hw_init) {
		regcache_cache_only(rt711->regmap, false);
		regcache_cache_bypass(rt711->regmap, true);
		regcache_cache_only(rt711->mbq_regmap, false);
		regcache_cache_bypass(rt711->mbq_regmap, true);
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

	rt711_sdca_reset(rt711);

	rt711_sdca_index_read(rt711, RT711_VENDOR_REG, RT711_JD_PRODUCT_NUM, &val);
	rt711->hw_ver = val & 0xf;

	if (rt711->hw_ver == RT711_VER_VD0)
		rt711_sdca_vd0_io_init(rt711);
	else
		rt711_sdca_vd1_io_init(rt711);

	/* DP4 mux select from 08_filter_Out_pri */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_REG,
		RT711_FILTER_SRC_SEL, 0x1800, 0x0800);

	/* ge_exclusive_inbox_en disable */
	rt711_sdca_index_update_bits(rt711, RT711_VENDOR_HDA_CTL,
		RT711_PUSH_BTN_INT_CTL0, 0x20, 0x00);

	/* calibration */
	ret = rt711_sdca_calibration(rt711);
	if (ret < 0)
		dev_err(dev, "%s, calibration failed!\n", __func__);

	/* HP output enable */
	regmap_write(rt711->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_OT1, RT711_SDCA_CTL_VENDOR_DEF, 0), 0x4);

	/*
	 * if set_jack callback occurred early than io_init,
	 * we set up the jack detection function now
	 */
	if (rt711->hs_jack)
		rt711_sdca_jack_init(rt711);

	if (rt711->first_hw_init) {
		regcache_cache_bypass(rt711->regmap, false);
		regcache_mark_dirty(rt711->regmap);
		regcache_cache_bypass(rt711->mbq_regmap, false);
		regcache_mark_dirty(rt711->mbq_regmap);
	} else
		rt711->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt711->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

MODULE_DESCRIPTION("ASoC RT711 SDCA SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
