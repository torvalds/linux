// SPDX-License-Identifier: GPL-2.0
//
// rt1016.c  --  RT1016 ALSA SoC audio amplifier driver
//
// Copyright 2020 Realtek Semiconductor Corp.
// Author: Oder Chiou <oder_chiou@realtek.com>
//

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
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
#include "rt1016.h"

static const struct reg_sequence rt1016_patch[] = {
	{RT1016_VOL_CTRL_3,	0x8900},
	{RT1016_ANA_CTRL_1,	0xa002},
	{RT1016_ANA_CTRL_2,	0x0002},
	{RT1016_CLOCK_4,	0x6700},
	{RT1016_CLASSD_3,	0xdc55},
	{RT1016_CLASSD_4,	0x376a},
	{RT1016_CLASSD_5,	0x009f},
};

static const struct reg_default rt1016_reg[] = {
	{0x00, 0x0000},
	{0x01, 0x5400},
	{0x02, 0x5506},
	{0x03, 0xf800},
	{0x04, 0x0000},
	{0x05, 0xbfbf},
	{0x06, 0x8900},
	{0x07, 0xa002},
	{0x08, 0x0000},
	{0x09, 0x0000},
	{0x0a, 0x0000},
	{0x0c, 0x0000},
	{0x0d, 0x0000},
	{0x0e, 0x10ec},
	{0x0f, 0x6595},
	{0x11, 0x0002},
	{0x1c, 0x0000},
	{0x1d, 0x0000},
	{0x1e, 0x0000},
	{0x1f, 0xf000},
	{0x20, 0x0000},
	{0x21, 0x6000},
	{0x22, 0x0000},
	{0x23, 0x6700},
	{0x24, 0x0000},
	{0x25, 0x0000},
	{0x26, 0x0000},
	{0x40, 0x0018},
	{0x60, 0x00a5},
	{0x80, 0x0010},
	{0x81, 0x0009},
	{0x82, 0x0000},
	{0x83, 0x0000},
	{0xa0, 0x0700},
	{0xc0, 0x0080},
	{0xc1, 0x02a0},
	{0xc2, 0x1400},
	{0xc3, 0x0a4a},
	{0xc4, 0x552a},
	{0xc5, 0x087e},
	{0xc6, 0x0020},
	{0xc7, 0xa833},
	{0xc8, 0x0433},
	{0xc9, 0x8040},
	{0xca, 0xdc55},
	{0xcb, 0x376a},
	{0xcc, 0x009f},
	{0xcf, 0x0020},
};

static bool rt1016_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1016_ANA_FLAG:
	case RT1016_VERSION2_ID:
	case RT1016_VERSION1_ID:
	case RT1016_VENDER_ID:
	case RT1016_DEVICE_ID:
	case RT1016_TEST_SIGNAL:
	case RT1016_SC_CTRL_1:
		return true;

	default:
		return false;
	}
}

static bool rt1016_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1016_RESET:
	case RT1016_PADS_CTRL_1:
	case RT1016_PADS_CTRL_2:
	case RT1016_I2C_CTRL:
	case RT1016_VOL_CTRL_1:
	case RT1016_VOL_CTRL_2:
	case RT1016_VOL_CTRL_3:
	case RT1016_ANA_CTRL_1:
	case RT1016_MUX_SEL:
	case RT1016_RX_I2S_CTRL:
	case RT1016_ANA_FLAG:
	case RT1016_VERSION2_ID:
	case RT1016_VERSION1_ID:
	case RT1016_VENDER_ID:
	case RT1016_DEVICE_ID:
	case RT1016_ANA_CTRL_2:
	case RT1016_TEST_SIGNAL:
	case RT1016_TEST_CTRL_1:
	case RT1016_TEST_CTRL_2:
	case RT1016_TEST_CTRL_3:
	case RT1016_CLOCK_1:
	case RT1016_CLOCK_2:
	case RT1016_CLOCK_3:
	case RT1016_CLOCK_4:
	case RT1016_CLOCK_5:
	case RT1016_CLOCK_6:
	case RT1016_CLOCK_7:
	case RT1016_I2S_CTRL:
	case RT1016_DAC_CTRL_1:
	case RT1016_SC_CTRL_1:
	case RT1016_SC_CTRL_2:
	case RT1016_SC_CTRL_3:
	case RT1016_SC_CTRL_4:
	case RT1016_SIL_DET:
	case RT1016_SYS_CLK:
	case RT1016_BIAS_CUR:
	case RT1016_DAC_CTRL_2:
	case RT1016_LDO_CTRL:
	case RT1016_CLASSD_1:
	case RT1016_PLL1:
	case RT1016_PLL2:
	case RT1016_PLL3:
	case RT1016_CLASSD_2:
	case RT1016_CLASSD_OUT:
	case RT1016_CLASSD_3:
	case RT1016_CLASSD_4:
	case RT1016_CLASSD_5:
	case RT1016_PWR_CTRL:
		return true;

	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9550, 50, 0);

static const struct snd_kcontrol_new rt1016_snd_controls[] = {
	SOC_DOUBLE_TLV("DAC Playback Volume", RT1016_VOL_CTRL_2,
		RT1016_L_VOL_SFT, RT1016_R_VOL_SFT, 191, 0, dac_vol_tlv),
	SOC_DOUBLE("DAC Playback Switch", RT1016_VOL_CTRL_1,
		RT1016_DA_MUTE_L_SFT, RT1016_DA_MUTE_R_SFT, 1, 1),
};

static int rt1016_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(source->dapm);
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);

	if (rt1016->sysclk_src == RT1016_SCLK_S_PLL)
		return 1;
	else
		return 0;
}

/* Interface data select */
static const char * const rt1016_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt1016_if_data_swap_enum,
	RT1016_I2S_CTRL, RT1016_I2S_DATA_SWAP_SFT, rt1016_data_select);

static const struct snd_kcontrol_new rt1016_if_data_swap_mux =
	SOC_DAPM_ENUM("Data Swap Mux", rt1016_if_data_swap_enum);

static const struct snd_soc_dapm_widget rt1016_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("Data Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt1016_if_data_swap_mux),

	SND_SOC_DAPM_SUPPLY("DAC Filter", RT1016_CLOCK_3,
		RT1016_PWR_DAC_FILTER_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAMOD", RT1016_CLOCK_3, RT1016_PWR_DACMOD_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("FIFO", RT1016_CLOCK_3, RT1016_PWR_CLK_FIFO_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("Pure DC", RT1016_CLOCK_3,
		RT1016_PWR_CLK_PUREDC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK Silence Det", RT1016_CLOCK_3,
		RT1016_PWR_SIL_DET_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RC 25M", RT1016_CLOCK_3, RT1016_PWR_RC_25M_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT1016_CLOCK_3, RT1016_PWR_PLL1_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("ANA CTRL", RT1016_CLOCK_3, RT1016_PWR_ANA_CTRL_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK SYS", RT1016_CLOCK_3, RT1016_PWR_CLK_SYS_BIT,
		0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("LRCK Det", RT1016_CLOCK_4, RT1016_PWR_LRCK_DET_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BCLK Det", RT1016_CLOCK_4, RT1016_PWR_BCLK_DET_BIT,
		0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("CKGEN DAC", RT1016_DAC_CTRL_2,
		RT1016_CKGEN_DAC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VCM SLOW", RT1016_CLASSD_1, RT1016_VCM_SLOW_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("Silence Det", RT1016_SIL_DET,
		RT1016_SIL_DET_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2", RT1016_PLL2, RT1016_PLL2_EN_BIT, 0, NULL,
		0),

	SND_SOC_DAPM_SUPPLY_S("BG1 BG2", 1, RT1016_PWR_CTRL,
		RT1016_PWR_BG_1_2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("MBIAS BG", 1, RT1016_PWR_CTRL,
		RT1016_PWR_MBIAS_BG_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("PLL", 1, RT1016_PWR_CTRL, RT1016_PWR_PLL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("BASIC", 1, RT1016_PWR_CTRL, RT1016_PWR_BASIC_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("CLASS D", 1, RT1016_PWR_CTRL,
		RT1016_PWR_CLSD_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("25M", 1, RT1016_PWR_CTRL, RT1016_PWR_25M_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACL", 1, RT1016_PWR_CTRL, RT1016_PWR_DACL_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACR", 1, RT1016_PWR_CTRL, RT1016_PWR_DACR_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("LDO2", 1, RT1016_PWR_CTRL, RT1016_PWR_LDO2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("VREF", 1, RT1016_PWR_CTRL, RT1016_PWR_VREF_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("MBIAS", 1, RT1016_PWR_CTRL, RT1016_PWR_MBIAS_BIT,
		0, NULL, 0),

	SND_SOC_DAPM_AIF_IN("AIFRX", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("SPO"),
};

static const struct snd_soc_dapm_route rt1016_dapm_routes[] = {
	{ "Data Swap Mux", "L/R", "AIFRX" },
	{ "Data Swap Mux", "R/L", "AIFRX" },
	{ "Data Swap Mux", "L/L", "AIFRX" },
	{ "Data Swap Mux", "R/R", "AIFRX" },

	{ "DAC", NULL, "DAC Filter" },
	{ "DAC", NULL, "DAMOD" },
	{ "DAC", NULL, "FIFO" },
	{ "DAC", NULL, "Pure DC" },
	{ "DAC", NULL, "Silence Det" },
	{ "DAC", NULL, "ANA CTRL" },
	{ "DAC", NULL, "CLK SYS" },
	{ "DAC", NULL, "LRCK Det" },
	{ "DAC", NULL, "BCLK Det" },
	{ "DAC", NULL, "CKGEN DAC" },
	{ "DAC", NULL, "VCM SLOW" },

	{ "PLL", NULL, "PLL1" },
	{ "PLL", NULL, "PLL2" },
	{ "25M", NULL, "RC 25M" },
	{ "Silence Det", NULL, "CLK Silence Det" },

	{ "DAC", NULL, "Data Swap Mux" },
	{ "DAC", NULL, "BG1 BG2" },
	{ "DAC", NULL, "MBIAS BG" },
	{ "DAC", NULL, "PLL", rt1016_is_sys_clk_from_pll},
	{ "DAC", NULL, "BASIC" },
	{ "DAC", NULL, "CLASS D" },
	{ "DAC", NULL, "25M" },
	{ "DAC", NULL, "DACL" },
	{ "DAC", NULL, "DACR" },
	{ "DAC", NULL, "LDO2" },
	{ "DAC", NULL, "VREF" },
	{ "DAC", NULL, "MBIAS" },

	{ "SPO", NULL, "DAC" },
};

static int rt1016_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);
	int pre_div, bclk_ms, frame_size;
	unsigned int val_len = 0;

	rt1016->lrck = params_rate(params);
	pre_div = rl6231_get_clk_info(rt1016->sysclk, rt1016->lrck);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock rate\n");
		return -EINVAL;
	}

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	bclk_ms = frame_size > 32;
	rt1016->bclk = rt1016->lrck * (32 << bclk_ms);

	if (bclk_ms && rt1016->master)
		snd_soc_component_update_bits(component, RT1016_I2S_CTRL,
			RT1016_I2S_BCLK_MS_MASK, RT1016_I2S_BCLK_MS_64);

	dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt1016->lrck, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		val_len = RT1016_I2S_DL_16;
		break;
	case 20:
		val_len = RT1016_I2S_DL_20;
		break;
	case 24:
		val_len = RT1016_I2S_DL_24;
		break;
	case 32:
		val_len = RT1016_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1016_I2S_CTRL,
		RT1016_I2S_DL_MASK, val_len);
	snd_soc_component_update_bits(component, RT1016_CLOCK_2,
		RT1016_FS_PD_MASK | RT1016_OSR_PD_MASK,
		((pre_div + 3) << RT1016_FS_PD_SFT) |
		(pre_div << RT1016_OSR_PD_SFT));

	return 0;
}

static int rt1016_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		reg_val |= RT1016_I2S_MS_M;
		rt1016->master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT1016_I2S_MS_S;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT1016_I2S_BCLK_POL_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT1016_I2S_DF_LEFT;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT1016_I2S_DF_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT1016_I2S_DF_PCM_B;
		break;

	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1016_I2S_CTRL,
			RT1016_I2S_MS_MASK | RT1016_I2S_BCLK_POL_MASK |
			RT1016_I2S_DF_MASK, reg_val);

	return 0;
}

static int rt1016_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt1016->sysclk && clk_id == rt1016->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT1016_SCLK_S_MCLK:
		reg_val |= RT1016_CLK_SYS_SEL_MCLK;
		break;

	case RT1016_SCLK_S_PLL:
		reg_val |= RT1016_CLK_SYS_SEL_PLL;
		break;

	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	rt1016->sysclk = freq;
	rt1016->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	snd_soc_component_update_bits(component, RT1016_CLOCK_1,
			RT1016_CLK_SYS_SEL_MASK, reg_val);

	return 0;
}

static int rt1016_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt1016->pll_in = 0;
		rt1016->pll_out = 0;

		return 0;
	}

	if (source == rt1016->pll_src && freq_in == rt1016->pll_in &&
		freq_out == rt1016->pll_out)
		return 0;

	switch (source) {
	case RT1016_PLL_S_MCLK:
		snd_soc_component_update_bits(component, RT1016_CLOCK_1,
			RT1016_PLL_SEL_MASK, RT1016_PLL_SEL_MCLK);
		break;

	case RT1016_PLL_S_BCLK:
		snd_soc_component_update_bits(component, RT1016_CLOCK_1,
			RT1016_PLL_SEL_MASK, RT1016_PLL_SEL_BCLK);
		break;

	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out * 4, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupported input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "mbypass=%d m=%d n=%d kbypass=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_bp,
		(pll_code.k_bp ? 0 : pll_code.k_code));

	snd_soc_component_write(component, RT1016_PLL1,
		((pll_code.m_bp ? 0 : pll_code.m_code) << RT1016_PLL_M_SFT) |
		(pll_code.m_bp << RT1016_PLL_M_BP_SFT) |
		pll_code.n_code);
	snd_soc_component_write(component, RT1016_PLL2,
		(pll_code.k_bp << RT1016_PLL_K_BP_SFT) |
		(pll_code.k_bp ? 0 : pll_code.k_code));

	rt1016->pll_in = freq_in;
	rt1016->pll_out = freq_out;
	rt1016->pll_src = source;

	return 0;
}

static int rt1016_probe(struct snd_soc_component *component)
{
	struct rt1016_priv *rt1016 =
		snd_soc_component_get_drvdata(component);

	rt1016->component = component;

	return 0;
}

static void rt1016_remove(struct snd_soc_component *component)
{
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);

	regmap_write(rt1016->regmap, RT1016_RESET, 0);
}

#define RT1016_STEREO_RATES SNDRV_PCM_RATE_8000_48000
#define RT1016_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt1016_aif_dai_ops = {
	.hw_params = rt1016_hw_params,
	.set_fmt = rt1016_set_dai_fmt,
};

static struct snd_soc_dai_driver rt1016_dai[] = {
	{
		.name = "rt1016-aif",
		.id = 0,
		.playback = {
			.stream_name = "AIF Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1016_STEREO_RATES,
			.formats = RT1016_FORMATS,
		},
		.ops = &rt1016_aif_dai_ops,
	}
};

#ifdef CONFIG_PM
static int rt1016_suspend(struct snd_soc_component *component)
{
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1016->regmap, true);
	regcache_mark_dirty(rt1016->regmap);

	return 0;
}

static int rt1016_resume(struct snd_soc_component *component)
{
	struct rt1016_priv *rt1016 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1016->regmap, false);
	regcache_sync(rt1016->regmap);

	return 0;
}
#else
#define rt1016_suspend NULL
#define rt1016_resume NULL
#endif

static const struct snd_soc_component_driver soc_component_dev_rt1016 = {
	.probe = rt1016_probe,
	.remove = rt1016_remove,
	.suspend = rt1016_suspend,
	.resume = rt1016_resume,
	.controls = rt1016_snd_controls,
	.num_controls = ARRAY_SIZE(rt1016_snd_controls),
	.dapm_widgets = rt1016_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1016_dapm_widgets),
	.dapm_routes = rt1016_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1016_dapm_routes),
	.set_sysclk = rt1016_set_component_sysclk,
	.set_pll = rt1016_set_component_pll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config rt1016_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = RT1016_PWR_CTRL,
	.volatile_reg = rt1016_volatile_register,
	.readable_reg = rt1016_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt1016_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1016_reg),
};

static const struct i2c_device_id rt1016_i2c_id[] = {
	{ "rt1016", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1016_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt1016_of_match[] = {
	{ .compatible = "realtek,rt1016", },
	{},
};
MODULE_DEVICE_TABLE(of, rt1016_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt1016_acpi_match[] = {
	{"10EC1016", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt1016_acpi_match);
#endif

static int rt1016_i2c_probe(struct i2c_client *i2c)
{
	struct rt1016_priv *rt1016;
	int ret;
	unsigned int val;

	rt1016 = devm_kzalloc(&i2c->dev, sizeof(struct rt1016_priv),
				GFP_KERNEL);
	if (rt1016 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1016);

	rt1016->regmap = devm_regmap_init_i2c(i2c, &rt1016_regmap);
	if (IS_ERR(rt1016->regmap)) {
		ret = PTR_ERR(rt1016->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt1016->regmap, RT1016_DEVICE_ID, &val);
	if (val != RT1016_DEVICE_ID_VAL) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt1016\n", val);
		return -ENODEV;
	}

	regmap_write(rt1016->regmap, RT1016_RESET, 0);

	ret = regmap_register_patch(rt1016->regmap, rt1016_patch,
				    ARRAY_SIZE(rt1016_patch));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	return devm_snd_soc_register_component(&i2c->dev,
		&soc_component_dev_rt1016,
		rt1016_dai, ARRAY_SIZE(rt1016_dai));
}

static void rt1016_i2c_shutdown(struct i2c_client *client)
{
	struct rt1016_priv *rt1016 = i2c_get_clientdata(client);

	regmap_write(rt1016->regmap, RT1016_RESET, 0);
}

static struct i2c_driver rt1016_i2c_driver = {
	.driver = {
		.name = "rt1016",
		.of_match_table = of_match_ptr(rt1016_of_match),
		.acpi_match_table = ACPI_PTR(rt1016_acpi_match),
	},
	.probe = rt1016_i2c_probe,
	.shutdown = rt1016_i2c_shutdown,
	.id_table = rt1016_i2c_id,
};
module_i2c_driver(rt1016_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1016 driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
