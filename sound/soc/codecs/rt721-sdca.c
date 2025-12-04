// SPDX-License-Identifier: GPL-2.0-only
//
// rt721-sdca.c -- rt721 SDCA ALSA SoC audio driver
//
// Copyright(c) 2024 Realtek Semiconductor Corp.
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

#include "rt721-sdca.h"
#include "rt-sdw-common.h"

static void rt721_sdca_jack_detect_handler(struct work_struct *work)
{
	struct rt721_sdca_priv *rt721 =
		container_of(work, struct rt721_sdca_priv, jack_detect_work.work);
	int btn_type = 0;

	if (!rt721->hs_jack)
		return;

	if (!rt721->component->card || !rt721->component->card->instantiated)
		return;

	/* SDW_SCP_SDCA_INT_SDCA_6 is used for jack detection */
	if (rt721->scp_sdca_stat1 & SDW_SCP_SDCA_INT_SDCA_0) {
		rt721->jack_type = rt_sdca_headset_detect(rt721->regmap,
							RT721_SDCA_ENT_GE49);
		if (rt721->jack_type < 0)
			return;
	}

	/* SDW_SCP_SDCA_INT_SDCA_8 is used for button detection */
	if (rt721->scp_sdca_stat2 & SDW_SCP_SDCA_INT_SDCA_8)
		btn_type = rt_sdca_button_detect(rt721->regmap,
					RT721_SDCA_ENT_HID01, RT721_BUF_ADDR_HID1,
					RT721_SDCA_HID_ID);

	if (rt721->jack_type == 0)
		btn_type = 0;

	dev_dbg(&rt721->slave->dev,
		"in %s, jack_type=%d\n", __func__, rt721->jack_type);
	dev_dbg(&rt721->slave->dev,
		"in %s, btn_type=0x%x\n", __func__, btn_type);
	dev_dbg(&rt721->slave->dev,
		"in %s, scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
		rt721->scp_sdca_stat1, rt721->scp_sdca_stat2);

	snd_soc_jack_report(rt721->hs_jack, rt721->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt721->hs_jack, rt721->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt721->jack_btn_check_work, msecs_to_jiffies(200));
	}
}

static void rt721_sdca_btn_check_handler(struct work_struct *work)
{
	struct rt721_sdca_priv *rt721 =
		container_of(work, struct rt721_sdca_priv, jack_btn_check_work.work);
	int btn_type = 0, ret, idx;
	unsigned int det_mode, offset, val;
	unsigned char buf[3];

	ret = regmap_read(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_GE49,
			RT721_SDCA_CTL_DETECTED_MODE, 0), &det_mode);
	if (ret < 0)
		goto io_error;

	/* pin attached */
	if (det_mode) {
		/* read UMP message offset */
		ret = regmap_read(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, RT721_SDCA_ENT_HID01,
				RT721_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0), &offset);
		if (ret < 0)
			goto io_error;

		for (idx = 0; idx < sizeof(buf); idx++) {
			ret = regmap_read(rt721->regmap,
				RT721_BUF_ADDR_HID1 + offset + idx, &val);
			if (ret < 0)
				goto io_error;
			buf[idx] = val & 0xff;
		}
		/* Report ID for HID1 */
		if (buf[0] == 0x11)
			btn_type = rt_sdca_btn_type(&buf[1]);
	} else
		rt721->jack_type = 0;

	dev_dbg(&rt721->slave->dev, "%s, btn_type=0x%x\n",	__func__, btn_type);
	snd_soc_jack_report(rt721->hs_jack, rt721->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

	if (btn_type) {
		/* button released */
		snd_soc_jack_report(rt721->hs_jack, rt721->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3);

		mod_delayed_work(system_power_efficient_wq,
			&rt721->jack_btn_check_work, msecs_to_jiffies(200));
	}

	return;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static void rt721_sdca_dmic_preset(struct rt721_sdca_priv *rt721)
{
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_ANA_POW_PART,
		RT721_VREF1_HV_CTRL1, 0xe000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8007);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL9, 0x2a2a);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL10, 0x2a00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL6, 0x2a2a);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL5, 0x2626);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL8, 0x1e00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL7, 0x1515);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_CH_FLOAT_CTL3, 0x0304);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_CH_FLOAT_CTL4, 0x0304);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_HDA_LEGACY_CTL1, 0x0000);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_IT26,
			RT721_SDCA_CTL_VENDOR_DEF, 0), 0x01);
	regmap_write(rt721->mbq_regmap, 0x5910009, 0x2e01);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_RC_CALIB_CTRL,
		RT721_RC_CALIB_CTRL0, 0x0b00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_RC_CALIB_CTRL,
		RT721_RC_CALIB_CTRL0, 0x0b40);
	regmap_write(rt721->regmap, 0x2f5c, 0x25);
}

static void rt721_sdca_amp_preset(struct rt721_sdca_priv *rt721)
{
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_ANA_POW_PART,
		RT721_VREF1_HV_CTRL1, 0xe000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8007);
	regmap_write(rt721->mbq_regmap, 0x5810000, 0x6420);
	regmap_write(rt721->mbq_regmap, 0x5810000, 0x6421);
	regmap_write(rt721->mbq_regmap, 0x5810000, 0xe421);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_CH_FLOAT_CTL6, 0x5561);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_REG,
		RT721_GPIO_PAD_CTRL5, 0x8003);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_OT23,
			RT721_SDCA_CTL_VENDOR_DEF, 0), 0x04);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
			RT721_SDCA_CTL_FU_MUTE, CH_01), 0x00);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
			RT721_SDCA_CTL_FU_MUTE, CH_02), 0x00);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_FU55,
			RT721_SDCA_CTL_FU_MUTE, CH_01), 0x00);
	regmap_write(rt721->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_FU55,
			RT721_SDCA_CTL_FU_MUTE, CH_02), 0x00);
}

static void rt721_sdca_jack_preset(struct rt721_sdca_priv *rt721)
{
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_ANA_POW_PART,
		RT721_VREF1_HV_CTRL1, 0xe000);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_MISC_POWER_CTL31, 0x8007);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_GE_REL_CTRL1, 0x8011);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_UMP_HID_CTRL3, 0xcf00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_UMP_HID_CTRL4, 0x000f);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_UMP_HID_CTRL1, 0x1100);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_UMP_HID_CTRL5, 0x0c12);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_JD_CTRL,
		RT721_JD_1PIN_GAT_CTRL2, 0xc002);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_RC_CALIB_CTRL,
		RT721_RC_CALIB_CTRL0, 0x0b00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_RC_CALIB_CTRL,
		RT721_RC_CALIB_CTRL0, 0x0b40);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_UAJ_TOP_TCON14, 0x3333);
	regmap_write(rt721->mbq_regmap, 0x5810035, 0x0036);
	regmap_write(rt721->mbq_regmap, 0x5810030, 0xee00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_CAP_PORT_CTRL,
		RT721_HP_AMP_2CH_CAL1, 0x0140);
	regmap_write(rt721->mbq_regmap, 0x5810000, 0x0021);
	regmap_write(rt721->mbq_regmap, 0x5810000, 0x8021);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_CAP_PORT_CTRL,
		RT721_HP_AMP_2CH_CAL18, 0x5522);
	regmap_write(rt721->mbq_regmap, 0x5b10007, 0x2000);
	regmap_write(rt721->mbq_regmap, 0x5B10017, 0x1b0f);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_CBJ_CTRL,
		RT721_CBJ_A0_GAT_CTRL1, 0x2a02);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_CAP_PORT_CTRL,
		RT721_HP_AMP_2CH_CAL4, 0xa105);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_UAJ_TOP_TCON14, 0x3b33);
	regmap_write(rt721->mbq_regmap, 0x310400, 0x3023);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_UAJ_TOP_TCON14, 0x3f33);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_UAJ_TOP_TCON13, 0x6048);
	regmap_write(rt721->mbq_regmap, 0x310401, 0x3000);
	regmap_write(rt721->mbq_regmap, 0x310402, 0x1b00);
	regmap_write(rt721->mbq_regmap, 0x310300, 0x000f);
	regmap_write(rt721->mbq_regmap, 0x310301, 0x3000);
	regmap_write(rt721->mbq_regmap, 0x310302, 0x1b00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_VENDOR_ANA_CTL,
		RT721_UAJ_TOP_TCON17, 0x0008);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_DAC_CTRL,
		RT721_DAC_2CH_CTRL3, 0x55ff);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_DAC_CTRL,
		RT721_DAC_2CH_CTRL4, 0xcc00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_ANA_POW_PART,
		RT721_MBIAS_LV_CTRL2, 0x6677);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_ANA_POW_PART,
		RT721_VREF2_LV_CTRL1, 0x7600);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL2, 0x1234);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL3, 0x3512);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL1, 0x4040);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_ENT_FLOAT_CTL4, 0x1201);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_BOOST_CTRL,
		RT721_BST_4CH_TOP_GATING_CTRL1, 0x002a);
	regmap_write(rt721->regmap, 0x2f58, 0x07);

	regmap_write(rt721->regmap, 0x2f51, 0x00);
	rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_MISC_CTL, 0x0004);
}

static void rt721_sdca_jack_init(struct rt721_sdca_priv *rt721)
{
	mutex_lock(&rt721->calibrate_mutex);
	if (rt721->hs_jack) {
		sdw_write_no_pm(rt721->slave, SDW_SCP_SDCA_INTMASK1,
			SDW_SCP_SDCA_INTMASK_SDCA_0);
		sdw_write_no_pm(rt721->slave, SDW_SCP_SDCA_INTMASK2,
			SDW_SCP_SDCA_INTMASK_SDCA_8);
		dev_dbg(&rt721->slave->dev, "in %s enable\n", __func__);
		rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
			RT721_HDA_LEGACY_UAJ_CTL, 0x036E);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_XU03,
				RT721_SDCA_CTL_SELECTED_MODE, 0), 0);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_XU0D,
				RT721_SDCA_CTL_SELECTED_MODE, 0), 0);
		rt_sdca_index_write(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
			RT721_XU_REL_CTRL, 0x0000);
		rt_sdca_index_update_bits(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
			RT721_GE_REL_CTRL1, 0x4000, 0x4000);
	}
	mutex_unlock(&rt721->calibrate_mutex);
}

static int rt721_sdca_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	int ret;

	rt721->hs_jack = hs_jack;

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

	rt721_sdca_jack_init(rt721);

	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

/* For SDCA control DAC/ADC Gain */
static int rt721_sdca_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned int read_l, read_r, gain_l_val, gain_r_val;
	unsigned int adc_vol_flag = 0, changed = 0;
	unsigned int lvalue, rvalue;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendA = 0x200;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt721->mbq_regmap, mc->reg, &lvalue);
	regmap_read(rt721->mbq_regmap, mc->rreg, &rvalue);

	/* L Channel */
	gain_l_val = ucontrol->value.integer.value[0];
	if (gain_l_val > mc->max)
		gain_l_val = mc->max;

	if (mc->shift == 8) {
		/* boost gain */
		gain_l_val = gain_l_val * tendB;
	} else if (mc->shift == 1) {
		/* FU33 boost gain */
		if (gain_l_val == 0)
			gain_l_val = 0x8000;
		else
			gain_l_val = (gain_l_val - 1) * tendA;
	} else {
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

	if (mc->shift == 8) {
		/* boost gain */
		gain_r_val = gain_r_val * tendB;
	} else if (mc->shift == 1) {
		/* FU33 boost gain */
		if (gain_r_val == 0)
			gain_r_val = 0x8000;
		else
			gain_r_val = (gain_r_val - 1) * tendA;
	} else {
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
	regmap_write(rt721->mbq_regmap, mc->reg, gain_l_val);

	/* Rch */
	regmap_write(rt721->mbq_regmap, mc->rreg, gain_r_val);

	regmap_read(rt721->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt721->mbq_regmap, mc->rreg, &read_r);
	if (read_r == gain_r_val && read_l == gain_l_val)
		return changed;

	return -EIO;
}

static int rt721_sdca_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int read_l, read_r, ctl_l = 0, ctl_r = 0;
	unsigned int adc_vol_flag = 0;
	const unsigned int interval_offset = 0xc0;
	const unsigned int tendA = 0x200;
	const unsigned int tendB = 0xa00;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume") ||
		strstr(ucontrol->id.name, "FU0F Capture Volume"))
		adc_vol_flag = 1;

	regmap_read(rt721->mbq_regmap, mc->reg, &read_l);
	regmap_read(rt721->mbq_regmap, mc->rreg, &read_r);

	if (mc->shift == 8) {
		/* boost gain */
		ctl_l = read_l / tendB;
	} else if (mc->shift == 1) {
		/* FU33 boost gain */
		if (read_l == 0x8000 || read_l == 0xfe00)
			ctl_l = 0;
		else
			ctl_l = read_l / tendA + 1;
	} else {
		if (adc_vol_flag)
			ctl_l = mc->max - (((0x1e00 - read_l) & 0xffff) / interval_offset);
		else
			ctl_l = mc->max - (((0 - read_l) & 0xffff) / interval_offset);
	}

	if (read_l != read_r) {
		if (mc->shift == 8) {
			/* boost gain */
			ctl_r = read_r / tendB;
		} else if (mc->shift == 1) {
			/* FU33 boost gain */
			if (read_r == 0x8000 || read_r == 0xfe00)
				ctl_r = 0;
			else
				ctl_r = read_r / tendA + 1;
		} else { /* ADC/DAC gain */
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

static int rt721_sdca_set_fu1e_capture_ctl(struct rt721_sdca_priv *rt721)
{
	int err, i;
	unsigned int ch_mute;

	for (i = 0; i < ARRAY_SIZE(rt721->fu1e_mixer_mute); i++) {
		ch_mute = rt721->fu1e_dapm_mute || rt721->fu1e_mixer_mute[i];
		err = regmap_write(rt721->regmap,
				SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E,
				RT721_SDCA_CTL_FU_MUTE, CH_01) + i, ch_mute);
		if (err < 0)
			return err;
	}

	return 0;
}

static int rt721_sdca_fu1e_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct rt721_sdca_dmic_kctrl_priv *p =
		(struct rt721_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	unsigned int i;

	for (i = 0; i < p->count; i++)
		ucontrol->value.integer.value[i] = !rt721->fu1e_mixer_mute[i];

	return 0;
}

static int rt721_sdca_fu1e_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct rt721_sdca_dmic_kctrl_priv *p =
		(struct rt721_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	int err, changed = 0, i;

	for (i = 0; i < p->count; i++) {
		if (rt721->fu1e_mixer_mute[i] != !ucontrol->value.integer.value[i])
			changed = 1;
		rt721->fu1e_mixer_mute[i] = !ucontrol->value.integer.value[i];
	}

	err = rt721_sdca_set_fu1e_capture_ctl(rt721);
	if (err < 0)
		return err;

	return changed;
}

static int rt721_sdca_set_fu0f_capture_ctl(struct rt721_sdca_priv *rt721)
{
	int err;
	unsigned int ch_l, ch_r;

	ch_l = (rt721->fu0f_dapm_mute || rt721->fu0f_mixer_l_mute) ? 0x01 : 0x00;
	ch_r = (rt721->fu0f_dapm_mute || rt721->fu0f_mixer_r_mute) ? 0x01 : 0x00;

	err = regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F,
			RT721_SDCA_CTL_FU_MUTE, CH_L), ch_l);
	if (err < 0)
		return err;

	err = regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F,
			RT721_SDCA_CTL_FU_MUTE, CH_R), ch_r);
	if (err < 0)
		return err;

	return 0;
}

static int rt721_sdca_fu0f_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt721->fu0f_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt721->fu0f_mixer_r_mute;
	return 0;
}

static int rt721_sdca_fu0f_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	int err, changed = 0;

	if (rt721->fu0f_mixer_l_mute != !ucontrol->value.integer.value[0] ||
		rt721->fu0f_mixer_r_mute != !ucontrol->value.integer.value[1])
		changed = 1;

	rt721->fu0f_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt721->fu0f_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt721_sdca_set_fu0f_capture_ctl(rt721);
	if (err < 0)
		return err;

	return changed;
}

static int rt721_sdca_fu_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct rt721_sdca_dmic_kctrl_priv *p =
		(struct rt721_sdca_dmic_kctrl_priv *)kcontrol->private_value;

	if (p->max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = p->count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = p->max;
	return 0;
}

static int rt721_sdca_dmic_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct rt721_sdca_dmic_kctrl_priv *p =
		(struct rt721_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	unsigned int boost_step = 0x0a00;
	unsigned int vol_max = 0x1e00;
	unsigned int regvalue, ctl, i;
	unsigned int adc_vol_flag = 0;
	const unsigned int interval_offset = 0xc0;

	if (strstr(ucontrol->id.name, "FU1E Capture Volume"))
		adc_vol_flag = 1;

	/* check all channels */
	for (i = 0; i < p->count; i++) {
		regmap_read(rt721->mbq_regmap, p->reg_base + i, &regvalue);

		if (!adc_vol_flag) /* boost gain */
			ctl = regvalue / boost_step;
		else /* ADC gain */
			ctl = p->max - (((vol_max - regvalue) & 0xffff) / interval_offset);

		ucontrol->value.integer.value[i] = ctl;
	}

	return 0;
}

static int rt721_sdca_dmic_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt721_sdca_dmic_kctrl_priv *p =
		(struct rt721_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
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
		regmap_read(rt721->mbq_regmap, p->reg_base + i, &regvalue[i]);

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
		err = regmap_write(rt721->mbq_regmap, p->reg_base + i, gain_val[i]);
		if (err < 0)
			dev_err(&rt721->slave->dev, "%#08x can't be set\n", p->reg_base + i);
	}

	return changed;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(boost_vol_tlv, 0, 1000, 0);
static const DECLARE_TLV_DB_SCALE(mic2_boost_vol_tlv, -200, 200, 0);

static const struct snd_kcontrol_new rt721_sdca_controls[] = {
	/* Headphone playback settings */
	SOC_DOUBLE_R_EXT_TLV("FU05 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
			RT721_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
			RT721_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x57, 0,
		rt721_sdca_set_gain_get, rt721_sdca_set_gain_put, out_vol_tlv),
	/* Headset mic capture settings */
	SOC_DOUBLE_EXT("FU0F Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt721_sdca_fu0f_capture_get, rt721_sdca_fu0f_capture_put),
	SOC_DOUBLE_R_EXT_TLV("FU0F Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F,
			RT721_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F,
			RT721_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x3f, 0,
		rt721_sdca_set_gain_get, rt721_sdca_set_gain_put, mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU33 Boost Volume",
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PLATFORM_FU44,
			RT721_SDCA_CTL_FU_CH_GAIN, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PLATFORM_FU44,
			RT721_SDCA_CTL_FU_CH_GAIN, CH_R), 1, 0x15, 0,
		rt721_sdca_set_gain_get, rt721_sdca_set_gain_put, mic2_boost_vol_tlv),
	/* AMP playback settings */
	SOC_DOUBLE_R_EXT_TLV("FU06 Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
			RT721_SDCA_CTL_FU_VOLUME, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
			RT721_SDCA_CTL_FU_VOLUME, CH_R), 0, 0x57, 0,
		rt721_sdca_set_gain_get, rt721_sdca_set_gain_put, out_vol_tlv),
	/* DMIC capture settings */
	RT_SDCA_FU_CTRL("FU1E Capture Switch",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E,
			RT721_SDCA_CTL_FU_MUTE, CH_01), 1, 1, 4, rt721_sdca_fu_info,
			rt721_sdca_fu1e_capture_get, rt721_sdca_fu1e_capture_put),
	RT_SDCA_EXT_TLV("FU1E Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E,
			RT721_SDCA_CTL_FU_VOLUME, CH_01),
		rt721_sdca_dmic_set_gain_get, rt721_sdca_dmic_set_gain_put,
			4, 0x3f, mic_vol_tlv, rt721_sdca_fu_info),
	RT_SDCA_EXT_TLV("FU15 Boost Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_FU15,
			RT721_SDCA_CTL_FU_CH_GAIN, CH_01),
		rt721_sdca_dmic_set_gain_get, rt721_sdca_dmic_set_gain_put,
			4, 3, boost_vol_tlv, rt721_sdca_fu_info),
};

static int rt721_sdca_adc_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, mask_sft, mask;

	if (strstr(ucontrol->id.name, "ADC 09 Mux")) {
		mask_sft = 12;
		mask = 0x7;
	} else if (strstr(ucontrol->id.name, "ADC 08 R Mux")) {
		mask_sft = 10;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 08 L Mux")) {
		mask_sft = 8;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 10 R Mux")) {
		mask_sft = 6;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 10 L Mux")) {
		mask_sft = 4;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 07 R Mux")) {
		mask_sft = 2;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 07 L Mux")) {
		mask_sft = 0;
		mask = 0x3;
	} else
		return -EINVAL;

	rt_sdca_index_read(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_HDA_LEGACY_MUX_CTL0, &val);

	ucontrol->value.enumerated.item[0] = (val >> mask_sft) & mask;

	return 0;
}

static int rt721_sdca_adc_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, mask_sft, mask;
	unsigned int check;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "ADC 09 Mux")) {
		mask_sft = 12;
		mask = 0x7;
	} else if (strstr(ucontrol->id.name, "ADC 08 R Mux")) {
		mask_sft = 10;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 08 L Mux")) {
		mask_sft = 8;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 10 R Mux")) {
		mask_sft = 6;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 10 L Mux")) {
		mask_sft = 4;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 07 R Mux")) {
		mask_sft = 2;
		mask = 0x3;
	} else if (strstr(ucontrol->id.name, "ADC 07 L Mux")) {
		mask_sft = 0;
		mask = 0x3;
	} else
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	rt_sdca_index_read(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
		RT721_HDA_LEGACY_MUX_CTL0, &val2);

	if (strstr(ucontrol->id.name, "ADC 09 Mux"))
		val2 = (val2 >> mask_sft) & 0x7;
	else
		val2 = (val2 >> mask_sft) & 0x3;

	if (val == val2)
		change = 0;
	else
		change = 1;

	if (change) {
		rt_sdca_index_read(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
			RT721_HDA_LEGACY_MUX_CTL0, &check);
		rt_sdca_index_update_bits(rt721->mbq_regmap, RT721_HDA_SDCA_FLOAT,
			RT721_HDA_LEGACY_MUX_CTL0, mask << mask_sft,
			val << mask_sft);
	}

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
		item[0], e, NULL);

	return change;
}

static const char * const adc09_mux_text[] = {
	"MIC2",
	"LINE1",
	"LINE2",
};
static const char * const adc07_10_mux_text[] = {
	"DMIC1 RE",
	"DMIC1 FE",
	"DMIC2 RE",
	"DMIC2 FE",
};

static SOC_ENUM_SINGLE_DECL(
	rt721_adc09_enum, SND_SOC_NOPM, 0, adc09_mux_text);
static SOC_ENUM_SINGLE_DECL(
	rt721_dmic_enum, SND_SOC_NOPM, 0, adc07_10_mux_text);

static const struct snd_kcontrol_new rt721_sdca_adc09_mux =
	SOC_DAPM_ENUM_EXT("ADC 09 Mux", rt721_adc09_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc08_r_mux =
	SOC_DAPM_ENUM_EXT("ADC 08 R Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc08_l_mux =
	SOC_DAPM_ENUM_EXT("ADC 08 L Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc10_r_mux =
	SOC_DAPM_ENUM_EXT("ADC 10 R Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc10_l_mux =
	SOC_DAPM_ENUM_EXT("ADC 10 L Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc07_r_mux =
	SOC_DAPM_ENUM_EXT("ADC 07 R Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);
static const struct snd_kcontrol_new rt721_sdca_adc07_l_mux =
	SOC_DAPM_ENUM_EXT("ADC 07 L Mux", rt721_dmic_enum,
			rt721_sdca_adc_mux_get, rt721_sdca_adc_mux_put);


static int rt721_sdca_fu42_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(100);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
				RT721_SDCA_CTL_FU_MUTE, CH_L), unmute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
				RT721_SDCA_CTL_FU_MUTE, CH_R), unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
				RT721_SDCA_CTL_FU_MUTE, CH_L), mute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05,
				RT721_SDCA_CTL_FU_MUTE, CH_R), mute);
		break;
	}
	return 0;
}

static int rt721_sdca_fu21_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
				RT721_SDCA_CTL_FU_MUTE, CH_L), unmute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
				RT721_SDCA_CTL_FU_MUTE, CH_R), unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
				RT721_SDCA_CTL_FU_MUTE, CH_L), mute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06,
				RT721_SDCA_CTL_FU_MUTE, CH_R), mute);
		break;
	}
	return 0;
}

static int rt721_sdca_fu23_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char unmute = 0x0, mute = 0x1;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
				RT721_SDCA_CTL_FU_MUTE, CH_L), unmute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
				RT721_SDCA_CTL_FU_MUTE, CH_R), unmute);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
				RT721_SDCA_CTL_FU_MUTE, CH_L), mute);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE23,
				RT721_SDCA_CTL_FU_MUTE, CH_R), mute);
		break;
	}
	return 0;
}

static int rt721_sdca_fu113_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt721->fu1e_dapm_mute = false;
		rt721_sdca_set_fu1e_capture_ctl(rt721);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt721->fu1e_dapm_mute = true;
		rt721_sdca_set_fu1e_capture_ctl(rt721);
		break;
	}
	return 0;
}

static int rt721_sdca_fu36_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt721->fu0f_dapm_mute = false;
		rt721_sdca_set_fu0f_capture_ctl(rt721);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt721->fu0f_dapm_mute = true;
		rt721_sdca_set_fu0f_capture_ctl(rt721);
		break;
	}
	return 0;
}

static int rt721_sdca_pde47_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PDE40,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PDE40,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt721_sdca_pde41_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE41,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_PDE41,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt721_sdca_pde11_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_PDE2A,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_PDE2A,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static int rt721_sdca_pde34_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PDE12,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PDE12,
				RT721_SDCA_CTL_REQ_POWER_STATE, 0), ps3);
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt721_sdca_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),
	SND_SOC_DAPM_INPUT("DMIC1_2"),
	SND_SOC_DAPM_INPUT("DMIC3_4"),

	SND_SOC_DAPM_SUPPLY("PDE 41", SND_SOC_NOPM, 0, 0,
		rt721_sdca_pde41_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 47", SND_SOC_NOPM, 0, 0,
		rt721_sdca_pde47_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 11", SND_SOC_NOPM, 0, 0,
		rt721_sdca_pde11_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 34", SND_SOC_NOPM, 0, 0,
		rt721_sdca_pde34_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_DAC_E("FU 21", NULL, SND_SOC_NOPM, 0, 0,
		rt721_sdca_fu21_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU 23", NULL, SND_SOC_NOPM, 0, 0,
		rt721_sdca_fu23_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU 42", NULL, SND_SOC_NOPM, 0, 0,
		rt721_sdca_fu42_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 36", NULL, SND_SOC_NOPM, 0, 0,
		rt721_sdca_fu36_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 113", NULL, SND_SOC_NOPM, 0, 0,
		rt721_sdca_fu113_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("ADC 09 Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc09_mux),
	SND_SOC_DAPM_MUX("ADC 08 R Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc08_r_mux),
	SND_SOC_DAPM_MUX("ADC 08 L Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc08_l_mux),
	SND_SOC_DAPM_MUX("ADC 10 R Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc10_r_mux),
	SND_SOC_DAPM_MUX("ADC 10 L Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc10_l_mux),
	SND_SOC_DAPM_MUX("ADC 07 R Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc07_r_mux),
	SND_SOC_DAPM_MUX("ADC 07 L Mux", SND_SOC_NOPM, 0, 0,
		&rt721_sdca_adc07_l_mux),

	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Headphone Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Headset Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Speaker Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP6TX", "DP6 DMic Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt721_sdca_audio_map[] = {
	{"FU 42", NULL, "DP1RX"},
	{"FU 21", NULL, "DP3RX"},
	{"FU 23", NULL, "DP3RX"},

	{"ADC 09 Mux", "MIC2", "MIC2"},
	{"ADC 09 Mux", "LINE1", "LINE1"},
	{"ADC 09 Mux", "LINE2", "LINE2"},
	{"ADC 07 R Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 07 R Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 07 R Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 07 R Mux", "DMIC2 FE", "DMIC3_4"},
	{"ADC 07 L Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 07 L Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 07 L Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 07 L Mux", "DMIC2 FE", "DMIC3_4"},
	{"ADC 08 R Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 08 R Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 08 R Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 08 R Mux", "DMIC2 FE", "DMIC3_4"},
	{"ADC 08 L Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 08 L Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 08 L Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 08 L Mux", "DMIC2 FE", "DMIC3_4"},
	{"ADC 10 R Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 10 R Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 10 R Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 10 R Mux", "DMIC2 FE", "DMIC3_4"},
	{"ADC 10 L Mux", "DMIC1 RE", "DMIC1_2"},
	{"ADC 10 L Mux", "DMIC1 FE", "DMIC1_2"},
	{"ADC 10 L Mux", "DMIC2 RE", "DMIC3_4"},
	{"ADC 10 L Mux", "DMIC2 FE", "DMIC3_4"},
	{"FU 36", NULL, "PDE 34"},
	{"FU 36", NULL, "ADC 09 Mux"},
	{"FU 113", NULL, "PDE 11"},
	{"FU 113", NULL, "ADC 07 R Mux"},
	{"FU 113", NULL, "ADC 07 L Mux"},
	{"FU 113", NULL, "ADC 10 R Mux"},
	{"FU 113", NULL, "ADC 10 L Mux"},
	{"DP2TX", NULL, "FU 36"},
	{"DP6TX", NULL, "FU 113"},

	{"HP", NULL, "PDE 47"},
	{"HP", NULL, "FU 42"},
	{"SPK", NULL, "PDE 41"},
	{"SPK", NULL, "FU 21"},
	{"SPK", NULL, "FU 23"},
};

static int rt721_sdca_parse_dt(struct rt721_sdca_priv *rt721, struct device *dev)
{
	device_property_read_u32(dev, "realtek,jd-src", &rt721->jd_src);

	return 0;
}

static int rt721_sdca_probe(struct snd_soc_component *component)
{
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	int ret;

	rt721_sdca_parse_dt(rt721, &rt721->slave->dev);
	rt721->component = component;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_sdca_dev_rt721 = {
	.probe = rt721_sdca_probe,
	.controls = rt721_sdca_controls,
	.num_controls = ARRAY_SIZE(rt721_sdca_controls),
	.dapm_widgets = rt721_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt721_sdca_dapm_widgets),
	.dapm_routes = rt721_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt721_sdca_audio_map),
	.set_jack = rt721_sdca_set_jack_detect,
	.endianness = 1,
};

static int rt721_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt721_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt721_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
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

	if (!rt721->slave)
		return -EINVAL;

	/*
	 * RT721_AIF1 with port = 1 for headphone playback
	 * RT721_AIF1 with port = 2 for headset-mic capture
	 * RT721_AIF2 with port = 3 for speaker playback
	 * RT721_AIF3 with port = 6 for digital-mic capture
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		if (dai->id == RT721_AIF1)
			port = 1;
		else if (dai->id == RT721_AIF2)
			port = 3;
		else
			return -EINVAL;
	} else {
		direction = SDW_DATA_DIR_TX;
		if (dai->id == RT721_AIF1)
			port = 2;
		else if (dai->id == RT721_AIF3)
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

	retval = sdw_stream_add_slave(rt721->slave, &stream_config,
					&port_config, 1, sdw_stream);
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
	case 8000:
		sampling_rate = RT721_SDCA_RATE_8000HZ;
		break;
	case 16000:
		sampling_rate = RT721_SDCA_RATE_16000HZ;
		break;
	case 24000:
		sampling_rate = RT721_SDCA_RATE_24000HZ;
		break;
	case 32000:
		sampling_rate = RT721_SDCA_RATE_32000HZ;
		break;
	case 44100:
		sampling_rate = RT721_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT721_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT721_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT721_SDCA_RATE_192000HZ;
		break;
	case 384000:
		sampling_rate = RT721_SDCA_RATE_384000HZ;
		break;
	case 768000:
		sampling_rate = RT721_SDCA_RATE_768000HZ;
		break;
	default:
		dev_err(component->dev, "Rate %d is not supported\n",
			params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	if (dai->id == RT721_AIF1) {
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_CS01,
				RT721_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_CS11,
				RT721_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);
	}

	if (dai->id == RT721_AIF2)
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_CS31,
				RT721_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);

	if (dai->id == RT721_AIF3)
		regmap_write(rt721->regmap,
			SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_CS1F,
				RT721_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), sampling_rate);

	return 0;
}

static int rt721_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt721_sdca_priv *rt721 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt721->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt721->slave, sdw_stream);
	return 0;
}

#define RT721_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define RT721_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt721_sdca_ops = {
	.hw_params	= rt721_sdca_pcm_hw_params,
	.hw_free	= rt721_sdca_pcm_hw_free,
	.set_stream	= rt721_sdca_set_sdw_stream,
	.shutdown	= rt721_sdca_shutdown,
};

static struct snd_soc_dai_driver rt721_sdca_dai[] = {
	{
		.name = "rt721-sdca-aif1",
		.id = RT721_AIF1,
		.playback = {
			.stream_name = "DP1 Headphone Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT721_STEREO_RATES,
			.formats = RT721_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Headset Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT721_STEREO_RATES,
			.formats = RT721_FORMATS,
		},
		.ops = &rt721_sdca_ops,
	},
	{
		.name = "rt721-sdca-aif2",
		.id = RT721_AIF2,
		.playback = {
			.stream_name = "DP3 Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT721_STEREO_RATES,
			.formats = RT721_FORMATS,
		},
		.ops = &rt721_sdca_ops,
	},
	{
		.name = "rt721-sdca-aif3",
		.id = RT721_AIF3,
		.capture = {
			.stream_name = "DP6 DMic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT721_STEREO_RATES,
			.formats = RT721_FORMATS,
		},
		.ops = &rt721_sdca_ops,
	}
};

int rt721_sdca_init(struct device *dev, struct regmap *regmap,
			struct regmap *mbq_regmap, struct sdw_slave *slave)
{
	struct rt721_sdca_priv *rt721;

	rt721 = devm_kzalloc(dev, sizeof(*rt721), GFP_KERNEL);
	if (!rt721)
		return -ENOMEM;

	dev_set_drvdata(dev, rt721);
	rt721->slave = slave;
	rt721->regmap = regmap;
	rt721->mbq_regmap = mbq_regmap;

	regcache_cache_only(rt721->regmap, true);
	regcache_cache_only(rt721->mbq_regmap, true);

	mutex_init(&rt721->calibrate_mutex);
	mutex_init(&rt721->disable_irq_lock);

	INIT_DELAYED_WORK(&rt721->jack_detect_work, rt721_sdca_jack_detect_handler);
	INIT_DELAYED_WORK(&rt721->jack_btn_check_work, rt721_sdca_btn_check_handler);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt721->hw_init = false;
	rt721->first_hw_init = false;
	rt721->fu1e_dapm_mute = true;
	rt721->fu0f_dapm_mute = true;
	rt721->fu0f_mixer_l_mute = rt721->fu0f_mixer_r_mute = true;
	rt721->fu1e_mixer_mute[0] = rt721->fu1e_mixer_mute[1] =
		rt721->fu1e_mixer_mute[2] = rt721->fu1e_mixer_mute[3] = true;

	return devm_snd_soc_register_component(dev,
			&soc_sdca_dev_rt721, rt721_sdca_dai, ARRAY_SIZE(rt721_sdca_dai));
}

int rt721_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(dev);

	rt721->disable_irq = false;

	if (rt721->hw_init)
		return 0;

	regcache_cache_only(rt721->regmap, false);
	regcache_cache_only(rt721->mbq_regmap, false);
	if (rt721->first_hw_init) {
		regcache_cache_bypass(rt721->regmap, true);
		regcache_cache_bypass(rt721->mbq_regmap, true);
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
	rt721_sdca_dmic_preset(rt721);
	rt721_sdca_amp_preset(rt721);
	rt721_sdca_jack_preset(rt721);
	if (rt721->first_hw_init) {
		regcache_cache_bypass(rt721->regmap, false);
		regcache_mark_dirty(rt721->regmap);
		regcache_cache_bypass(rt721->mbq_regmap, false);
		regcache_mark_dirty(rt721->mbq_regmap);
	} else
		rt721->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt721->hw_init = true;

	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

MODULE_DESCRIPTION("ASoC RT721 SDCA SDW driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL");
