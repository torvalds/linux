// SPDX-License-Identifier: GPL-2.0-only
//
// rt715-sdca.c -- rt715 ALSA SoC audio driver
//
// Copyright(c) 2020 Realtek Semiconductor Corp.
//
//
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/soundwire/sdw.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/soundwire/sdw_registers.h>

#include "rt715-sdca.h"

static int rt715_index_write(struct rt715_sdca_priv *rt715, unsigned int nid,
		unsigned int reg, unsigned int value)
{
	struct regmap *regmap = rt715->mbq_regmap;
	unsigned int addr;
	int ret;

	addr = (nid << 20) | reg;

	ret = regmap_write(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt715->slave->dev,
				"Failed to set private value: %08x <= %04x %d\n", ret, addr,
				value);

	return ret;
}

static int rt715_index_read(struct rt715_sdca_priv *rt715,
		unsigned int nid, unsigned int reg, unsigned int *value)
{
	struct regmap *regmap = rt715->mbq_regmap;
	unsigned int addr;
	int ret;

	addr = (nid << 20) | reg;

	ret = regmap_read(regmap, addr, value);
	if (ret < 0)
		dev_err(&rt715->slave->dev,
				"Failed to get private value: %06x => %04x ret=%d\n",
				addr, *value, ret);

	return ret;
}

static int rt715_index_update_bits(struct rt715_sdca_priv *rt715,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	int ret;

	ret = rt715_index_read(rt715, nid, reg, &tmp);
	if (ret < 0)
		return ret;

	set_mask_bits(&tmp, mask, val);

	return rt715_index_write(rt715, nid, reg, tmp);
}

/* SDCA Volume/Boost control */
static int rt715_set_amp_gain_put_sdca(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	unsigned int val_l, val_r, gain_l_val, gain_r_val;
	int ret;

	/* control value to 2s complement */
	/* L channel */
	gain_l_val = ucontrol->value.integer.value[0];
	if (gain_l_val > mc->max)
		gain_l_val = mc->max;
	val_l = gain_l_val;

	if (mc->shift == 8) {
		gain_l_val = (gain_l_val * 10) << mc->shift;
	} else {
		gain_l_val =
			((abs(gain_l_val - mc->shift) * RT715_SDCA_DB_STEP) << 8) / 1000;
		if (val_l <= mc->shift) {
			gain_l_val = ~gain_l_val;
			gain_l_val += 1;
		}
		gain_l_val &= 0xffff;
	}

	/* R channel */
	gain_r_val = ucontrol->value.integer.value[1];
	if (gain_r_val > mc->max)
		gain_r_val = mc->max;
	val_r = gain_r_val;

	if (mc->shift == 8) {
		gain_r_val = (gain_r_val * 10) << mc->shift;
	} else {
		gain_r_val =
			((abs(gain_r_val - mc->shift) * RT715_SDCA_DB_STEP) << 8) / 1000;
		if (val_r <= mc->shift) {
			gain_r_val = ~gain_r_val;
			gain_r_val += 1;
		}
		gain_r_val &= 0xffff;
	}

	/* Lch*/
	ret = regmap_write(rt715->mbq_regmap, mc->reg, gain_l_val);
	if (ret != 0) {
		dev_err(component->dev, "Failed to write 0x%x=0x%x\n", mc->reg,
			gain_l_val);
		return ret;
	}
	/* Rch */
	ret = regmap_write(rt715->mbq_regmap, mc->rreg, gain_r_val);
	if (ret != 0) {
		dev_err(component->dev, "Failed to write 0x%x=0x%x\n", mc->rreg,
			gain_r_val);
		return ret;
	}

	return 0;
}

static int rt715_set_amp_gain_get_sdca(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	unsigned int val_l, val_r, ctl_l, ctl_r, neg_flag = 0;
	int ret;

	ret = regmap_read(rt715->mbq_regmap, mc->reg, &val_l);
	if (ret < 0)
		dev_err(component->dev, "Failed to read 0x%x, ret=%d\n", mc->reg, ret);
	ret = regmap_read(rt715->mbq_regmap, mc->rreg, &val_r);
	if (ret < 0)
		dev_err(component->dev, "Failed to read 0x%x, ret=%d\n", mc->rreg,
				ret);

	/* L channel */
	if (mc->shift == 8) {
		ctl_l = (val_l >> mc->shift) / 10;
	} else {
		ctl_l = val_l;
		if (ctl_l & BIT(15)) {
			ctl_l = ~(val_l - 1) & 0xffff;
			neg_flag = 1;
		}
		ctl_l *= 1000;
		ctl_l >>= 8;
		if (neg_flag)
			ctl_l = mc->shift - ctl_l / RT715_SDCA_DB_STEP;
		else
			ctl_l = mc->shift + ctl_l / RT715_SDCA_DB_STEP;
	}

	neg_flag = 0;
	/* R channel */
	if (mc->shift == 8) {
		ctl_r = (val_r >> mc->shift) / 10;
	} else {
		ctl_r = val_r;
		if (ctl_r & BIT(15)) {
			ctl_r = ~(val_r - 1) & 0xffff;
			neg_flag = 1;
		}
		ctl_r *= 1000;
		ctl_r >>= 8;
		if (neg_flag)
			ctl_r = mc->shift - ctl_r / RT715_SDCA_DB_STEP;
		else
			ctl_r = mc->shift + ctl_r / RT715_SDCA_DB_STEP;
	}

	ucontrol->value.integer.value[0] = ctl_l;
	ucontrol->value.integer.value[1] = ctl_r;

	return 0;
}

static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);

#define SOC_DOUBLE_R_EXT(xname, reg_left, reg_right, xshift, xmax, xinvert,\
	 xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = SOC_DOUBLE_R_VALUE(reg_left, reg_right, xshift, \
					    xmax, xinvert) }

static const struct snd_kcontrol_new rt715_snd_controls_sdca[] = {
	/* Capture switch */
	SOC_DOUBLE_R("FU0A Capture Switch",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC7_27_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC7_27_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_02),
			0, 1, 1),
	SOC_DOUBLE_R("FU02 1_2 Capture Switch",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_02),
			0, 1, 1),
	SOC_DOUBLE_R("FU02 3_4 Capture Switch",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_04),
			0, 1, 1),
	SOC_DOUBLE_R("FU06 1_2 Capture Switch",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_02),
			0, 1, 1),
	SOC_DOUBLE_R("FU06 3_4 Capture Switch",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_MUTE_CTRL, CH_04),
			0, 1, 1),
	/* Volume Control */
	SOC_DOUBLE_R_EXT_TLV("FU0A Capture Volume",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC7_27_VOL,
			RT715_SDCA_FU_VOL_CTRL, CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC7_27_VOL,
			RT715_SDCA_FU_VOL_CTRL, CH_02),
			0x2f, 0x7f, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU02 1_2 Capture Volume",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_VOL_CTRL, CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_VOL_CTRL, CH_02),
			0x2f, 0x7f, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU02 3_4 Capture Volume",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC8_9_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_04), 0x2f, 0x7f, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU06 1_2 Capture Volume",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_02), 0x2f, 0x7f, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		in_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU06 3_4 Capture Volume",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_ADC10_11_VOL,
			RT715_SDCA_FU_VOL_CTRL,
			CH_04), 0x2f, 0x7f, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		in_vol_tlv),
	/* MIC Boost Control */
	SOC_DOUBLE_R_EXT_TLV("FU0E 1_2 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_02), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0E 3_4 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_04), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0E 5_6 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_05),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_06), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0E 7_8 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_07),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_DMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_08), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0C 1_2 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_01),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_02), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0C 3_4 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_03),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_04), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0C 5_6 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_05),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_06), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
	SOC_DOUBLE_R_EXT_TLV("FU0C 7_8 Boost",
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_07),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_FU_AMIC_GAIN_EN,
			RT715_SDCA_FU_DMIC_GAIN_CTRL,
			CH_08), 8, 3, 0,
		rt715_set_amp_gain_get_sdca, rt715_set_amp_gain_put_sdca,
		mic_vol_tlv),
};

static int rt715_mux_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	unsigned int val, mask_sft;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 12;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		mask_sft = 8;
	else if (strstr(ucontrol->id.name, "ADC 24 Mux"))
		mask_sft = 4;
	else if (strstr(ucontrol->id.name, "ADC 25 Mux"))
		mask_sft = 0;
	else
		return -EINVAL;

	rt715_index_read(rt715, RT715_VENDOR_HDA_CTL,
		RT715_HDA_LEGACY_MUX_CTL1, &val);
	val = (val >> mask_sft) & 0xf;

	/*
	 * The first two indices of ADC Mux 24/25 are routed to the same
	 * hardware source. ie, ADC Mux 24 0/1 will both connect to MIC2.
	 * To have a unique set of inputs, we skip the index1 of the muxes.
	 */
	if ((strstr(ucontrol->id.name, "ADC 24 Mux") ||
		strstr(ucontrol->id.name, "ADC 25 Mux")) && val > 0)
		val -= 1;
	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int rt715_mux_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
				snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, val2 = 0, change, mask_sft;

	if (item[0] >= e->items)
		return -EINVAL;

	if (strstr(ucontrol->id.name, "ADC 22 Mux"))
		mask_sft = 12;
	else if (strstr(ucontrol->id.name, "ADC 23 Mux"))
		mask_sft = 8;
	else if (strstr(ucontrol->id.name, "ADC 24 Mux"))
		mask_sft = 4;
	else if (strstr(ucontrol->id.name, "ADC 25 Mux"))
		mask_sft = 0;
	else
		return -EINVAL;

	/* Verb ID = 0x701h, nid = e->reg */
	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;

	rt715_index_read(rt715, RT715_VENDOR_HDA_CTL,
		RT715_HDA_LEGACY_MUX_CTL1, &val2);
	val2 = (val2 >> mask_sft) & 0xf;

	change = val != val2;

	if (change)
		rt715_index_update_bits(rt715, RT715_VENDOR_HDA_CTL,
			RT715_HDA_LEGACY_MUX_CTL1, 0xf << mask_sft, val << mask_sft);

	snd_soc_dapm_mux_update_power(dapm, kcontrol, item[0], e, NULL);

	return change;
}

static const char * const adc_22_23_mux_text[] = {
	"MIC1",
	"MIC2",
	"LINE1",
	"LINE2",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

/*
 * Due to mux design for nid 24 (MUX_IN3)/25 (MUX_IN4), connection index 0 and
 * 1 will be connected to the same dmic source, therefore we skip index 1 to
 * avoid misunderstanding on usage of dapm routing.
 */
static int rt715_adc_24_25_values[] = {
	0,
	2,
	3,
	4,
	5,
};

static const char * const adc_24_mux_text[] = {
	"MIC2",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

static const char * const adc_25_mux_text[] = {
	"MIC1",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
};

static SOC_ENUM_SINGLE_DECL(rt715_adc22_enum, SND_SOC_NOPM, 0,
	adc_22_23_mux_text);

static SOC_ENUM_SINGLE_DECL(rt715_adc23_enum, SND_SOC_NOPM, 0,
	adc_22_23_mux_text);

static SOC_VALUE_ENUM_SINGLE_DECL(rt715_adc24_enum,
	SND_SOC_NOPM, 0, 0xf,
	adc_24_mux_text, rt715_adc_24_25_values);
static SOC_VALUE_ENUM_SINGLE_DECL(rt715_adc25_enum,
	SND_SOC_NOPM, 0, 0xf,
	adc_25_mux_text, rt715_adc_24_25_values);

static const struct snd_kcontrol_new rt715_adc22_mux =
	SOC_DAPM_ENUM_EXT("ADC 22 Mux", rt715_adc22_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc23_mux =
	SOC_DAPM_ENUM_EXT("ADC 23 Mux", rt715_adc23_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc24_mux =
	SOC_DAPM_ENUM_EXT("ADC 24 Mux", rt715_adc24_enum,
			rt715_mux_get, rt715_mux_put);

static const struct snd_kcontrol_new rt715_adc25_mux =
	SOC_DAPM_ENUM_EXT("ADC 25 Mux", rt715_adc25_enum,
			rt715_mux_get, rt715_mux_put);

static int rt715_pde23_24_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt715->regmap,
			SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_CREQ_POW_EN,
				RT715_SDCA_REQ_POW_CTRL,
				CH_00), 0x00);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt715->regmap,
			SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_CREQ_POW_EN,
				RT715_SDCA_REQ_POW_CTRL,
				CH_00), 0x03);
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt715_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("DMIC3"),
	SND_SOC_DAPM_INPUT("DMIC4"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("LINE2"),

	SND_SOC_DAPM_SUPPLY("PDE23_24", SND_SOC_NOPM, 0, 0,
		rt715_pde23_24_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_ADC("ADC 07", NULL, SND_SOC_NOPM, 4, 0),
	SND_SOC_DAPM_ADC("ADC 08", NULL, SND_SOC_NOPM, 4, 0),
	SND_SOC_DAPM_ADC("ADC 09", NULL, SND_SOC_NOPM, 4, 0),
	SND_SOC_DAPM_ADC("ADC 27", NULL, SND_SOC_NOPM, 4, 0),
	SND_SOC_DAPM_MUX("ADC 22 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc22_mux),
	SND_SOC_DAPM_MUX("ADC 23 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc23_mux),
	SND_SOC_DAPM_MUX("ADC 24 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc24_mux),
	SND_SOC_DAPM_MUX("ADC 25 Mux", SND_SOC_NOPM, 0, 0,
		&rt715_adc25_mux),
	SND_SOC_DAPM_AIF_OUT("DP4TX", "DP4 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP6TX", "DP6 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt715_audio_map[] = {
	{"DP6TX", NULL, "ADC 09"},
	{"DP6TX", NULL, "ADC 08"},
	{"DP4TX", NULL, "ADC 07"},
	{"DP4TX", NULL, "ADC 27"},
	{"DP4TX", NULL, "ADC 09"},
	{"DP4TX", NULL, "ADC 08"},

	{"LINE1", NULL, "PDE23_24"},
	{"LINE2", NULL, "PDE23_24"},
	{"MIC1", NULL, "PDE23_24"},
	{"MIC2", NULL, "PDE23_24"},
	{"DMIC1", NULL, "PDE23_24"},
	{"DMIC2", NULL, "PDE23_24"},
	{"DMIC3", NULL, "PDE23_24"},
	{"DMIC4", NULL, "PDE23_24"},

	{"ADC 09", NULL, "ADC 22 Mux"},
	{"ADC 08", NULL, "ADC 23 Mux"},
	{"ADC 07", NULL, "ADC 24 Mux"},
	{"ADC 27", NULL, "ADC 25 Mux"},
	{"ADC 22 Mux", "MIC1", "MIC1"},
	{"ADC 22 Mux", "MIC2", "MIC2"},
	{"ADC 22 Mux", "LINE1", "LINE1"},
	{"ADC 22 Mux", "LINE2", "LINE2"},
	{"ADC 22 Mux", "DMIC1", "DMIC1"},
	{"ADC 22 Mux", "DMIC2", "DMIC2"},
	{"ADC 22 Mux", "DMIC3", "DMIC3"},
	{"ADC 22 Mux", "DMIC4", "DMIC4"},
	{"ADC 23 Mux", "MIC1", "MIC1"},
	{"ADC 23 Mux", "MIC2", "MIC2"},
	{"ADC 23 Mux", "LINE1", "LINE1"},
	{"ADC 23 Mux", "LINE2", "LINE2"},
	{"ADC 23 Mux", "DMIC1", "DMIC1"},
	{"ADC 23 Mux", "DMIC2", "DMIC2"},
	{"ADC 23 Mux", "DMIC3", "DMIC3"},
	{"ADC 23 Mux", "DMIC4", "DMIC4"},
	{"ADC 24 Mux", "MIC2", "MIC2"},
	{"ADC 24 Mux", "DMIC1", "DMIC1"},
	{"ADC 24 Mux", "DMIC2", "DMIC2"},
	{"ADC 24 Mux", "DMIC3", "DMIC3"},
	{"ADC 24 Mux", "DMIC4", "DMIC4"},
	{"ADC 25 Mux", "MIC1", "MIC1"},
	{"ADC 25 Mux", "DMIC1", "DMIC1"},
	{"ADC 25 Mux", "DMIC2", "DMIC2"},
	{"ADC 25 Mux", "DMIC3", "DMIC3"},
	{"ADC 25 Mux", "DMIC4", "DMIC4"},
};

static const struct snd_soc_component_driver soc_codec_dev_rt715_sdca = {
	.controls = rt715_snd_controls_sdca,
	.num_controls = ARRAY_SIZE(rt715_snd_controls_sdca),
	.dapm_widgets = rt715_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt715_dapm_widgets),
	.dapm_routes = rt715_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt715_audio_map),
};

static int rt715_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	struct rt715_sdw_stream_data *stream;

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

static void rt715_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)

{
	struct rt715_sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!stream)
		return;

	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt715_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct rt715_sdw_stream_data *stream;
	int retval, port, num_channels;
	unsigned int val;

	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt715->slave)
		return -EINVAL;

	switch (dai->id) {
	case RT715_AIF1:
		direction = SDW_DATA_DIR_TX;
		port = 6;
		rt715_index_write(rt715, RT715_VENDOR_REG, RT715_SDW_INPUT_SEL,
			0xa500);
		break;
	case RT715_AIF2:
		direction = SDW_DATA_DIR_TX;
		port = 4;
		rt715_index_write(rt715, RT715_VENDOR_REG, RT715_SDW_INPUT_SEL,
			0xaf00);
		break;
	default:
		dev_err(component->dev, "Invalid DAI id %d\n", dai->id);
		return -EINVAL;
	}

	stream_config.frame_rate =  params_rate(params);
	stream_config.ch_count = params_channels(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	num_channels = params_channels(params);
	port_config.ch_mask = GENMASK(num_channels - 1, 0);
	port_config.num = port;

	retval = sdw_stream_add_slave(rt715->slave, &stream_config,
					&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(component->dev, "Unable to configure port, retval:%d\n",
			retval);
		return retval;
	}

	switch (params_rate(params)) {
	case 8000:
		val = 0x1;
		break;
	case 11025:
		val = 0x2;
		break;
	case 12000:
		val = 0x3;
		break;
	case 16000:
		val = 0x4;
		break;
	case 22050:
		val = 0x5;
		break;
	case 24000:
		val = 0x6;
		break;
	case 32000:
		val = 0x7;
		break;
	case 44100:
		val = 0x8;
		break;
	case 48000:
		val = 0x9;
		break;
	case 88200:
		val = 0xa;
		break;
	case 96000:
		val = 0xb;
		break;
	case 176400:
		val = 0xc;
		break;
	case 192000:
		val = 0xd;
		break;
	case 384000:
		val = 0xe;
		break;
	case 768000:
		val = 0xf;
		break;
	default:
		dev_err(component->dev, "Unsupported sample rate %d\n",
			params_rate(params));
		return -EINVAL;
	}

	regmap_write(rt715->regmap,
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_CS_FREQ_IND_EN,
			RT715_SDCA_FREQ_IND_CTRL, CH_00), val);

	return 0;
}

static int rt715_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt715_sdca_priv *rt715 = snd_soc_component_get_drvdata(component);
	struct rt715_sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt715->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt715->slave, stream->sdw_stream);
	return 0;
}

#define RT715_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT715_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt715_ops = {
	.hw_params	= rt715_pcm_hw_params,
	.hw_free	= rt715_pcm_hw_free,
	.set_sdw_stream	= rt715_set_sdw_stream,
	.shutdown	= rt715_shutdown,
};

static struct snd_soc_dai_driver rt715_dai[] = {
	{
		.name = "rt715-aif1",
		.id = RT715_AIF1,
		.capture = {
			.stream_name = "DP6 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT715_STEREO_RATES,
			.formats = RT715_FORMATS,
		},
		.ops = &rt715_ops,
	},
	{
		.name = "rt715-aif2",
		.id = RT715_AIF2,
		.capture = {
			.stream_name = "DP4 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT715_STEREO_RATES,
			.formats = RT715_FORMATS,
		},
		.ops = &rt715_ops,
	},
};

/* Bus clock frequency */
#define RT715_CLK_FREQ_9600000HZ 9600000
#define RT715_CLK_FREQ_12000000HZ 12000000
#define RT715_CLK_FREQ_6000000HZ 6000000
#define RT715_CLK_FREQ_4800000HZ 4800000
#define RT715_CLK_FREQ_2400000HZ 2400000
#define RT715_CLK_FREQ_12288000HZ 12288000

int rt715_init(struct device *dev, struct regmap *mbq_regmap,
	struct regmap *regmap, struct sdw_slave *slave)
{
	struct rt715_sdca_priv *rt715;
	int ret;

	rt715 = devm_kzalloc(dev, sizeof(*rt715), GFP_KERNEL);
	if (!rt715)
		return -ENOMEM;

	dev_set_drvdata(dev, rt715);
	rt715->slave = slave;
	rt715->regmap = regmap;
	rt715->mbq_regmap = mbq_regmap;
	rt715->hw_sdw_ver = slave->id.sdw_version;
	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt715->hw_init = false;
	rt715->first_init = false;

	ret = devm_snd_soc_register_component(dev,
			&soc_codec_dev_rt715_sdca,
			rt715_dai,
			ARRAY_SIZE(rt715_dai));

	return ret;
}

int rt715_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt715_sdca_priv *rt715 = dev_get_drvdata(dev);
	unsigned int hw_ver;

	if (rt715->hw_init)
		return 0;

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!rt715->first_init) {
		/* set autosuspend parameters */
		pm_runtime_set_autosuspend_delay(&slave->dev, 3000);
		pm_runtime_use_autosuspend(&slave->dev);

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

		/* make sure the device does not suspend immediately */
		pm_runtime_mark_last_busy(&slave->dev);

		pm_runtime_enable(&slave->dev);

		rt715->first_init = true;
	}

	pm_runtime_get_noresume(&slave->dev);

	rt715_index_read(rt715, RT715_VENDOR_REG,
		RT715_PRODUCT_NUM, &hw_ver);
	hw_ver = hw_ver & 0x000f;

	/* set clock selector = external */
	regmap_write(rt715->regmap,
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_CX_CLK_SEL_EN,
			RT715_SDCA_CX_CLK_SEL_CTRL, CH_00), 0x1);
	/* set GPIO_4/5/6 to be 3rd/4th DMIC usage */
	if (hw_ver == 0x0)
		rt715_index_update_bits(rt715, RT715_VENDOR_REG,
			RT715_AD_FUNC_EN, 0x54, 0x54);
	else if (hw_ver == 0x1) {
		rt715_index_update_bits(rt715, RT715_VENDOR_REG,
			RT715_AD_FUNC_EN, 0x55, 0x55);
		rt715_index_update_bits(rt715, RT715_VENDOR_REG,
			RT715_REV_1, 0x40, 0x40);
	}
	/* trigger mode = VAD enable */
	regmap_write(rt715->regmap,
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_SMPU_TRIG_ST_EN,
			RT715_SDCA_SMPU_TRIG_EN_CTRL, CH_00), 0x2);
	/* SMPU-1 interrupt enable mask */
	regmap_update_bits(rt715->regmap, RT715_INT_MASK, 0x1, 0x1);

	/* Mark Slave initialization complete */
	rt715->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	return 0;
}

MODULE_DESCRIPTION("ASoC rt715 driver SDW SDCA");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL v2");
