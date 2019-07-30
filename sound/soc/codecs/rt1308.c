// SPDX-License-Identifier: GPL-2.0
//
// rt1308.c  --  RT1308 ALSA SoC amplifier component driver
//
// Copyright 2019 Realtek Semiconductor Corp.
// Author: Derek Fang <derek.fang@realtek.com>
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt1308.h"

static const struct reg_sequence init_list[] = {

	{ RT1308_I2C_I2S_SDW_SET,	0x01014005 },
	{ RT1308_CLASS_D_SET_2,		0x227f5501 },
	{ RT1308_PADS_1,		0x50150505 },
	{ RT1308_VREF,			0x18100000 },
	{ RT1308_IV_SENSE,		0x87010000 },
	{ RT1308_DUMMY_REG,		0x00000200 },
	{ RT1308_SIL_DET,		0xe1c30000 },
	{ RT1308_DC_CAL_2,		0x00ffff00 },
	{ RT1308_CLK_DET,		0x01000000 },
	{ RT1308_POWER_STATUS,		0x08800000 },
	{ RT1308_DAC_SET,		0xafaf0700 },

};
#define RT1308_INIT_REG_LEN ARRAY_SIZE(init_list)

struct rt1308_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;

	int sysclk;
	int sysclk_src;
	int lrck;
	int bclk;
	int master;

	int pll_src;
	int pll_in;
	int pll_out;
};

static const struct reg_default rt1308_reg[] = {

	{ 0x01, 0x1f3f5f00 },
	{ 0x02, 0x07000000 },
	{ 0x03, 0x80003e00 },
	{ 0x04, 0x80800600 },
	{ 0x05, 0x0aaa1a0a },
	{ 0x06, 0x52000000 },
	{ 0x07, 0x00000000 },
	{ 0x08, 0x00600000 },
	{ 0x09, 0xe1030000 },
	{ 0x0a, 0x00000000 },
	{ 0x0b, 0x30000000 },
	{ 0x0c, 0x7fff7000 },
	{ 0x10, 0xffff0700 },
	{ 0x11, 0x0a000000 },
	{ 0x12, 0x60040000 },
	{ 0x13, 0x00000000 },
	{ 0x14, 0x0f300000 },
	{ 0x15, 0x00000022 },
	{ 0x16, 0x02000000 },
	{ 0x17, 0x01004045 },
	{ 0x18, 0x00000000 },
	{ 0x19, 0x00000000 },
	{ 0x1a, 0x80000000 },
	{ 0x1b, 0x10325476 },
	{ 0x1c, 0x1d1d0000 },
	{ 0x20, 0xd2101300 },
	{ 0x21, 0xf3ffff00 },
	{ 0x22, 0x00000000 },
	{ 0x23, 0x00000000 },
	{ 0x24, 0x00000000 },
	{ 0x25, 0x00000000 },
	{ 0x26, 0x00000000 },
	{ 0x27, 0x00000000 },
	{ 0x28, 0x00000000 },
	{ 0x29, 0x00000000 },
	{ 0x2a, 0x00000000 },
	{ 0x2b, 0x00000000 },
	{ 0x2c, 0x00000000 },
	{ 0x2d, 0x00000000 },
	{ 0x2e, 0x00000000 },
	{ 0x2f, 0x00000000 },
	{ 0x30, 0x01000000 },
	{ 0x31, 0x20025501 },
	{ 0x32, 0x00000000 },
	{ 0x33, 0x105a0000 },
	{ 0x34, 0x10100000 },
	{ 0x35, 0x2aaa52aa },
	{ 0x36, 0x00c00000 },
	{ 0x37, 0x20046100 },
	{ 0x50, 0x10022f00 },
	{ 0x51, 0x003c0000 },
	{ 0x54, 0x04000000 },
	{ 0x55, 0x01000000 },
	{ 0x56, 0x02000000 },
	{ 0x57, 0x02000000 },
	{ 0x58, 0x02000000 },
	{ 0x59, 0x02000000 },
	{ 0x5b, 0x02000000 },
	{ 0x5c, 0x00000000 },
	{ 0x5d, 0x00000000 },
	{ 0x5e, 0x00000000 },
	{ 0x5f, 0x00000000 },
	{ 0x60, 0x02000000 },
	{ 0x61, 0x00000000 },
	{ 0x62, 0x00000000 },
	{ 0x63, 0x00000000 },
	{ 0x64, 0x00000000 },
	{ 0x65, 0x02000000 },
	{ 0x66, 0x00000000 },
	{ 0x67, 0x00000000 },
	{ 0x68, 0x00000000 },
	{ 0x69, 0x00000000 },
	{ 0x6a, 0x02000000 },
	{ 0x6c, 0x00000000 },
	{ 0x6d, 0x00000000 },
	{ 0x6e, 0x00000000 },
	{ 0x70, 0x10EC1308 },
	{ 0x71, 0x00000000 },
	{ 0x72, 0x00000000 },
	{ 0x73, 0x00000000 },
	{ 0x74, 0x00000000 },
	{ 0x75, 0x00000000 },
	{ 0x76, 0x00000000 },
	{ 0x77, 0x00000000 },
	{ 0x78, 0x00000000 },
	{ 0x79, 0x00000000 },
	{ 0x7a, 0x00000000 },
	{ 0x7b, 0x00000000 },
	{ 0x7c, 0x00000000 },
	{ 0x7d, 0x00000000 },
	{ 0x7e, 0x00000000 },
	{ 0x7f, 0x00020f00 },
	{ 0x80, 0x00000000 },
	{ 0x81, 0x00000000 },
	{ 0x82, 0x00000000 },
	{ 0x83, 0x00000000 },
	{ 0x84, 0x00000000 },
	{ 0x85, 0x00000000 },
	{ 0x86, 0x00000000 },
	{ 0x87, 0x00000000 },
	{ 0x88, 0x00000000 },
	{ 0x89, 0x00000000 },
	{ 0x8a, 0x00000000 },
	{ 0x8b, 0x00000000 },
	{ 0x8c, 0x00000000 },
	{ 0x8d, 0x00000000 },
	{ 0x8e, 0x00000000 },
	{ 0x90, 0x50250905 },
	{ 0x91, 0x15050000 },
	{ 0xa0, 0x00000000 },
	{ 0xa1, 0x00000000 },
	{ 0xa2, 0x00000000 },
	{ 0xa3, 0x00000000 },
	{ 0xa4, 0x00000000 },
	{ 0xb0, 0x00000000 },
	{ 0xb1, 0x00000000 },
	{ 0xb2, 0x00000000 },
	{ 0xb3, 0x00000000 },
	{ 0xb4, 0x00000000 },
	{ 0xb5, 0x00000000 },
	{ 0xb6, 0x00000000 },
	{ 0xb7, 0x00000000 },
	{ 0xb8, 0x00000000 },
	{ 0xb9, 0x00000000 },
	{ 0xba, 0x00000000 },
	{ 0xbb, 0x00000000 },
	{ 0xc0, 0x01000000 },
	{ 0xc1, 0x00000000 },
	{ 0xf0, 0x00000000 },
};

static int rt1308_reg_init(struct snd_soc_component *component)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);

	return regmap_multi_reg_write(rt1308->regmap, init_list,
				RT1308_INIT_REG_LEN);
}

static bool rt1308_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1308_RESET:
	case RT1308_RESET_N:
	case RT1308_CLK_2:
	case RT1308_SIL_DET:
	case RT1308_CLK_DET:
	case RT1308_DC_DET:
	case RT1308_DAC_SET:
	case RT1308_DAC_BUF:
	case RT1308_SDW_REG_RDATA:
	case RT1308_DC_CAL_1:
	case RT1308_PVDD_OFFSET_CTL:
	case RT1308_CAL_OFFSET_DAC_PBTL:
	case RT1308_CAL_OFFSET_DAC_L:
	case RT1308_CAL_OFFSET_DAC_R:
	case RT1308_CAL_OFFSET_PWM_L:
	case RT1308_CAL_OFFSET_PWM_R:
	case RT1308_CAL_PWM_VOS_ADC_L:
	case RT1308_CAL_PWM_VOS_ADC_R:
	case RT1308_MBIAS:
	case RT1308_POWER_STATUS:
	case RT1308_POWER_INT:
	case RT1308_SINE_TONE_GEN_2:
	case RT1308_BQ_SET:
	case RT1308_BQ_PARA_UPDATE:
	case RT1308_VEN_DEV_ID:
	case RT1308_VERSION_ID:
	case RT1308_EFUSE_1:
	case RT1308_EFUSE_READ_PVDD_L:
	case RT1308_EFUSE_READ_PVDD_R:
	case RT1308_EFUSE_READ_PVDD_PTBL:
	case RT1308_EFUSE_READ_DEV:
	case RT1308_EFUSE_READ_R0:
	case RT1308_EFUSE_READ_ADC_L:
	case RT1308_EFUSE_READ_ADC_R:
	case RT1308_EFUSE_READ_ADC_PBTL:
	case RT1308_EFUSE_RESERVE:
	case RT1308_EFUSE_DATA_0_MSB:
	case RT1308_EFUSE_DATA_0_LSB:
	case RT1308_EFUSE_DATA_1_MSB:
	case RT1308_EFUSE_DATA_1_LSB:
	case RT1308_EFUSE_DATA_2_MSB:
	case RT1308_EFUSE_DATA_2_LSB:
	case RT1308_EFUSE_DATA_3_MSB:
	case RT1308_EFUSE_DATA_3_LSB:
	case RT1308_EFUSE_STATUS_1:
	case RT1308_EFUSE_STATUS_2:
	case RT1308_DUMMY_REG:
		return true;
	default:
		return false;
	}
}

static bool rt1308_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1308_RESET:
	case RT1308_RESET_N:
	case RT1308_CLK_GATING ... RT1308_DC_DET_THRES:
	case RT1308_DAC_SET ... RT1308_AD_FILTER_SET:
	case RT1308_DC_CAL_1 ... RT1308_POWER_INT:
	case RT1308_SINE_TONE_GEN_1:
	case RT1308_SINE_TONE_GEN_2:
	case RT1308_BQ_SET:
	case RT1308_BQ_PARA_UPDATE:
	case RT1308_BQ_PRE_VOL_L ... RT1308_BQ_POST_VOL_R:
	case RT1308_BQ1_L_H0 ... RT1308_BQ2_R_A2:
	case RT1308_VEN_DEV_ID:
	case RT1308_VERSION_ID:
	case RT1308_SPK_BOUND:
	case RT1308_BQ1_EQ_L_1 ... RT1308_BQ2_EQ_R_3:
	case RT1308_EFUSE_1 ... RT1308_EFUSE_RESERVE:
	case RT1308_PADS_1:
	case RT1308_PADS_2:
	case RT1308_TEST_MODE:
	case RT1308_TEST_1:
	case RT1308_TEST_2:
	case RT1308_TEST_3:
	case RT1308_TEST_4:
	case RT1308_EFUSE_DATA_0_MSB ... RT1308_EFUSE_STATUS_2:
	case RT1308_TCON_1:
	case RT1308_TCON_2:
	case RT1308_DUMMY_REG:
	case RT1308_MAX_REG:
		return true;
	default:
		return false;
	}
}

static int rt1308_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(30);
		snd_soc_component_update_bits(component, RT1308_POWER_STATUS,
			RT1308_POW_PDB_REG_BIT | RT1308_POW_PDB_MN_BIT,
			RT1308_POW_PDB_REG_BIT | RT1308_POW_PDB_MN_BIT);
		msleep(40);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT1308_POWER_STATUS,
			RT1308_POW_PDB_REG_BIT | RT1308_POW_PDB_MN_BIT, 0);
		usleep_range(150000, 200000);
		break;

	default:
		break;
	}

	return 0;
}

static const char * const rt1308_rx_data_ch_select[] = {
	"LR",
	"LL",
	"RL",
	"RR",
};

static SOC_ENUM_SINGLE_DECL(rt1308_rx_data_ch_enum, RT1308_DATA_PATH, 24,
	rt1308_rx_data_ch_select);

static const struct snd_kcontrol_new rt1308_snd_controls[] = {

	/* I2S Data Channel Selection */
	SOC_ENUM("RX Channel Select", rt1308_rx_data_ch_enum),
};

static const struct snd_kcontrol_new rt1308_sto_dac_l =
	SOC_DAPM_SINGLE("Switch", RT1308_DAC_SET,
		RT1308_DVOL_MUTE_L_EN_SFT, 1, 1);

static const struct snd_kcontrol_new rt1308_sto_dac_r =
	SOC_DAPM_SINGLE("Switch", RT1308_DAC_SET,
		RT1308_DVOL_MUTE_R_EN_SFT, 1, 1);

static const struct snd_soc_dapm_widget rt1308_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Supply Widgets */
	SND_SOC_DAPM_SUPPLY("MBIAS20U", RT1308_POWER,
		RT1308_POW_MBIAS20U_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ALDO", RT1308_POWER,
		RT1308_POW_ALDO_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DBG", RT1308_POWER,
		RT1308_POW_DBG_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DACL", RT1308_POWER,
		RT1308_POW_DACL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK25M", RT1308_POWER,
		RT1308_POW_CLK25M_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_R", RT1308_POWER,
		RT1308_POW_ADC_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_L", RT1308_POWER,
		RT1308_POW_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DLDO", RT1308_POWER,
		RT1308_POW_DLDO_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF", RT1308_POWER,
		RT1308_POW_VREF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIXER_R", RT1308_POWER,
		RT1308_POW_MIXER_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIXER_L", RT1308_POWER,
		RT1308_POW_MIXER_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MBIAS4U", RT1308_POWER,
		RT1308_POW_MBIAS4U_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2_LDO", RT1308_POWER,
		RT1308_POW_PLL2_LDO_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2B", RT1308_POWER,
		RT1308_POW_PLL2B_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2F", RT1308_POWER,
		RT1308_POW_PLL2F_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2F2", RT1308_POWER,
		RT1308_POW_PLL2F2_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2B2", RT1308_POWER,
		RT1308_POW_PLL2B2_EN_BIT, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("DAC Power", RT1308_POWER,
		RT1308_POW_DAC1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("DAC L", SND_SOC_NOPM, 0, 0, &rt1308_sto_dac_l),
	SND_SOC_DAPM_SWITCH("DAC R", SND_SOC_NOPM, 0, 0, &rt1308_sto_dac_r),

	/* Output Lines */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1308_classd_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
};

static const struct snd_soc_dapm_route rt1308_dapm_routes[] = {

	{ "DAC", NULL, "AIF1RX" },

	{ "DAC", NULL, "MBIAS20U" },
	{ "DAC", NULL, "ALDO" },
	{ "DAC", NULL, "DBG" },
	{ "DAC", NULL, "DACL" },
	{ "DAC", NULL, "CLK25M" },
	{ "DAC", NULL, "ADC_R" },
	{ "DAC", NULL, "ADC_L" },
	{ "DAC", NULL, "DLDO" },
	{ "DAC", NULL, "VREF" },
	{ "DAC", NULL, "MIXER_R" },
	{ "DAC", NULL, "MIXER_L" },
	{ "DAC", NULL, "MBIAS4U" },
	{ "DAC", NULL, "PLL2_LDO" },
	{ "DAC", NULL, "PLL2B" },
	{ "DAC", NULL, "PLL2F" },
	{ "DAC", NULL, "PLL2F2" },
	{ "DAC", NULL, "PLL2B2" },

	{ "DAC L", "Switch", "DAC" },
	{ "DAC R", "Switch", "DAC" },
	{ "DAC L", NULL, "DAC Power" },
	{ "DAC R", NULL, "DAC Power" },

	{ "CLASS D", NULL, "DAC L" },
	{ "CLASS D", NULL, "DAC R" },
	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },
};

static int rt1308_get_clk_info(int sclk, int rate)
{
	int i, pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt1308_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt1308->lrck = params_rate(params);
	pre_div = rt1308_get_clk_info(rt1308->sysclk, rt1308->lrck);
	if (pre_div < 0) {
		dev_err(component->dev,
			"Unsupported clock setting %d\n", rt1308->lrck);
		return -EINVAL;
	}

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	bclk_ms = frame_size > 32;
	rt1308->bclk = rt1308->lrck * (32 << bclk_ms);

	dev_dbg(component->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt1308->lrck, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		val_len |= RT1308_I2S_DL_SEL_16B;
		break;
	case 20:
		val_len |= RT1308_I2S_DL_SEL_20B;
		break;
	case 24:
		val_len |= RT1308_I2S_DL_SEL_24B;
		break;
	case 8:
		val_len |= RT1308_I2S_DL_SEL_8B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT1308_AIF1:
		mask_clk = RT1308_DIV_FS_SYS_MASK;
		val_clk = pre_div << RT1308_DIV_FS_SYS_SFT;
		snd_soc_component_update_bits(component,
			RT1308_I2S_SET_2, RT1308_I2S_DL_SEL_MASK,
			val_len);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1308_CLK_1,
		mask_clk, val_clk);

	return 0;
}

static int rt1308_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, reg1_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		rt1308->master = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT1308_I2S_DF_SEL_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT1308_I2S_DF_SEL_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT1308_I2S_DF_SEL_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg1_val |= RT1308_I2S_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT1308_AIF1:
		snd_soc_component_update_bits(component,
			RT1308_I2S_SET_1, RT1308_I2S_DF_SEL_MASK,
			reg_val);
		snd_soc_component_update_bits(component,
			RT1308_I2S_SET_2, RT1308_I2S_BCLK_MASK,
			reg1_val);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt1308_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt1308->sysclk && clk_id == rt1308->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT1308_FS_SYS_S_MCLK:
		reg_val |= RT1308_SEL_FS_SYS_SRC_MCLK;
		snd_soc_component_update_bits(component,
			RT1308_CLK_DET, RT1308_MCLK_DET_EN_MASK,
			RT1308_MCLK_DET_EN);
		break;
	case RT1308_FS_SYS_S_BCLK:
		reg_val |= RT1308_SEL_FS_SYS_SRC_BCLK;
		break;
	case RT1308_FS_SYS_S_PLL:
		reg_val |= RT1308_SEL_FS_SYS_SRC_PLL;
		break;
	case RT1308_FS_SYS_S_RCCLK:
		reg_val |= RT1308_SEL_FS_SYS_SRC_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT1308_CLK_1,
		RT1308_SEL_FS_SYS_MASK, reg_val);
	rt1308->sysclk = freq;
	rt1308->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt1308_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt1308->pll_src && freq_in == rt1308->pll_in &&
	    freq_out == rt1308->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt1308->pll_in = 0;
		rt1308->pll_out = 0;
		snd_soc_component_update_bits(component,
			RT1308_CLK_1, RT1308_SEL_FS_SYS_MASK,
			RT1308_SEL_FS_SYS_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT1308_PLL_S_MCLK:
		snd_soc_component_update_bits(component,
			RT1308_CLK_2, RT1308_SEL_PLL_SRC_MASK,
			RT1308_SEL_PLL_SRC_MCLK);
		snd_soc_component_update_bits(component,
			RT1308_CLK_DET, RT1308_MCLK_DET_EN_MASK,
			RT1308_MCLK_DET_EN);
		break;
	case RT1308_PLL_S_BCLK:
		snd_soc_component_update_bits(component,
			RT1308_CLK_2, RT1308_SEL_PLL_SRC_MASK,
			RT1308_SEL_PLL_SRC_BCLK);
		break;
	case RT1308_PLL_S_RCCLK:
		snd_soc_component_update_bits(component,
			RT1308_CLK_2, RT1308_SEL_PLL_SRC_MASK,
			RT1308_SEL_PLL_SRC_RCCLK);
		freq_in = 25000000;
		break;
	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT1308_PLL_1,
		pll_code.k_code << RT1308_PLL1_K_SFT |
		pll_code.m_bp << RT1308_PLL1_M_BYPASS_SFT |
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT1308_PLL1_M_SFT |
		pll_code.n_code << RT1308_PLL1_N_SFT);

	rt1308->pll_in = freq_in;
	rt1308->pll_out = freq_out;
	rt1308->pll_src = source;

	return 0;
}

static int rt1308_probe(struct snd_soc_component *component)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);

	rt1308->component = component;

	return rt1308_reg_init(component);
}

static void rt1308_remove(struct snd_soc_component *component)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);

	regmap_write(rt1308->regmap, RT1308_RESET, 0);
}

#ifdef CONFIG_PM
static int rt1308_suspend(struct snd_soc_component *component)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1308->regmap, true);
	regcache_mark_dirty(rt1308->regmap);

	return 0;
}

static int rt1308_resume(struct snd_soc_component *component)
{
	struct rt1308_priv *rt1308 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1308->regmap, false);
	regcache_sync(rt1308->regmap);

	return 0;
}
#else
#define rt1308_suspend NULL
#define rt1308_resume NULL
#endif

#define RT1308_STEREO_RATES SNDRV_PCM_RATE_48000
#define RT1308_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt1308_aif_dai_ops = {
	.hw_params = rt1308_hw_params,
	.set_fmt = rt1308_set_dai_fmt,
};

static struct snd_soc_dai_driver rt1308_dai[] = {
	{
		.name = "rt1308-aif",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1308_STEREO_RATES,
			.formats = RT1308_FORMATS,
		},
		.ops = &rt1308_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt1308 = {
	.probe = rt1308_probe,
	.remove = rt1308_remove,
	.suspend = rt1308_suspend,
	.resume = rt1308_resume,
	.controls = rt1308_snd_controls,
	.num_controls = ARRAY_SIZE(rt1308_snd_controls),
	.dapm_widgets = rt1308_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1308_dapm_widgets),
	.dapm_routes = rt1308_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1308_dapm_routes),
	.set_sysclk = rt1308_set_component_sysclk,
	.set_pll = rt1308_set_component_pll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config rt1308_regmap = {
	.reg_bits = 8,
	.val_bits = 32,
	.max_register = RT1308_MAX_REG,
	.volatile_reg = rt1308_volatile_register,
	.readable_reg = rt1308_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt1308_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1308_reg),
	.use_single_read = true,
	.use_single_write = true,
};

#ifdef CONFIG_OF
static const struct of_device_id rt1308_of_match[] = {
	{ .compatible = "realtek,rt1308", },
	{ },
};
MODULE_DEVICE_TABLE(of, rt1308_of_match);
#endif

#ifdef CONFIG_ACPI
static struct acpi_device_id rt1308_acpi_match[] = {
	{ "10EC1308", 0, },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt1308_acpi_match);
#endif

static const struct i2c_device_id rt1308_i2c_id[] = {
	{ "rt1308", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1308_i2c_id);

static void rt1308_efuse(struct rt1308_priv *rt1308)
{
	regmap_write(rt1308->regmap, RT1308_RESET, 0);

	regmap_write(rt1308->regmap, RT1308_POWER_STATUS, 0x01800000);
	msleep(100);
	regmap_write(rt1308->regmap, RT1308_EFUSE_1, 0x44fe0f00);
	msleep(20);
	regmap_write(rt1308->regmap, RT1308_PVDD_OFFSET_CTL, 0x10000000);
}

static int rt1308_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt1308_priv *rt1308;
	int ret;
	unsigned int val;

	rt1308 = devm_kzalloc(&i2c->dev, sizeof(struct rt1308_priv),
				GFP_KERNEL);
	if (rt1308 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1308);

	rt1308->regmap = devm_regmap_init_i2c(i2c, &rt1308_regmap);
	if (IS_ERR(rt1308->regmap)) {
		ret = PTR_ERR(rt1308->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt1308->regmap, RT1308_VEN_DEV_ID, &val);
	/* ignore last byte difference */
	if ((val & 0xFFFFFF00) != RT1308_DEVICE_ID_NUM) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt1308\n", val);
		return -ENODEV;
	}

	rt1308_efuse(rt1308);

	return devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_rt1308,
			rt1308_dai, ARRAY_SIZE(rt1308_dai));
}

static void rt1308_i2c_shutdown(struct i2c_client *client)
{
	struct rt1308_priv *rt1308 = i2c_get_clientdata(client);

	regmap_write(rt1308->regmap, RT1308_RESET, 0);
}

static struct i2c_driver rt1308_i2c_driver = {
	.driver = {
		.name = "rt1308",
		.of_match_table = of_match_ptr(rt1308_of_match),
		.acpi_match_table = ACPI_PTR(rt1308_acpi_match),
	},
	.probe = rt1308_i2c_probe,
	.shutdown = rt1308_i2c_shutdown,
	.id_table = rt1308_i2c_id,
};
module_i2c_driver(rt1308_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1308 amplifier driver");
MODULE_AUTHOR("Derek Fang <derek.fang@realtek.com>");
MODULE_LICENSE("GPL v2");
