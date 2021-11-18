// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_dmic.c - Tegra210 DMIC driver
//
// Copyright (c) 2020 NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "tegra210_dmic.h"
#include "tegra_cif.h"

static const struct reg_default tegra210_dmic_reg_defaults[] = {
	{ TEGRA210_DMIC_TX_INT_MASK, 0x00000001 },
	{ TEGRA210_DMIC_TX_CIF_CTRL, 0x00007700 },
	{ TEGRA210_DMIC_CG, 0x1 },
	{ TEGRA210_DMIC_CTRL, 0x00000301 },
	/* Below enables all filters - DCR, LP and SC */
	{ TEGRA210_DMIC_DBG_CTRL, 0xe },
	/* Below as per latest POR value */
	{ TEGRA210_DMIC_DCR_BIQUAD_0_COEF_4, 0x0 },
	/* LP filter is configured for pass through and used to apply gain */
	{ TEGRA210_DMIC_LP_BIQUAD_0_COEF_0, 0x00800000 },
	{ TEGRA210_DMIC_LP_BIQUAD_0_COEF_1, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_0_COEF_2, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_0_COEF_3, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_0_COEF_4, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_1_COEF_0, 0x00800000 },
	{ TEGRA210_DMIC_LP_BIQUAD_1_COEF_1, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_1_COEF_2, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_1_COEF_3, 0x0 },
	{ TEGRA210_DMIC_LP_BIQUAD_1_COEF_4, 0x0 },
};

static int __maybe_unused tegra210_dmic_runtime_suspend(struct device *dev)
{
	struct tegra210_dmic *dmic = dev_get_drvdata(dev);

	regcache_cache_only(dmic->regmap, true);
	regcache_mark_dirty(dmic->regmap);

	clk_disable_unprepare(dmic->clk_dmic);

	return 0;
}

static int __maybe_unused tegra210_dmic_runtime_resume(struct device *dev)
{
	struct tegra210_dmic *dmic = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(dmic->clk_dmic);
	if (err) {
		dev_err(dev, "failed to enable DMIC clock, err: %d\n", err);
		return err;
	}

	regcache_cache_only(dmic->regmap, false);
	regcache_sync(dmic->regmap);

	return 0;
}

static int tegra210_dmic_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct tegra210_dmic *dmic = snd_soc_dai_get_drvdata(dai);
	unsigned int srate, clk_rate, channels;
	struct tegra_cif_conf cif_conf;
	unsigned long long gain_q23 = DEFAULT_GAIN_Q23;
	int err;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	cif_conf.audio_ch = channels;

	switch (dmic->ch_select) {
	case DMIC_CH_SELECT_LEFT:
	case DMIC_CH_SELECT_RIGHT:
		cif_conf.client_ch = 1;
		break;
	case DMIC_CH_SELECT_STEREO:
		cif_conf.client_ch = 2;
		break;
	default:
		dev_err(dai->dev, "invalid DMIC client channels\n");
		return -EINVAL;
	}

	srate = params_rate(params);

	/*
	 * DMIC clock rate is a multiple of 'Over Sampling Ratio' and
	 * 'Sample Rate'. The supported OSR values are 64, 128 and 256.
	 */
	clk_rate = (DMIC_OSR_FACTOR << dmic->osr_val) * srate;

	err = clk_set_rate(dmic->clk_dmic, clk_rate);
	if (err) {
		dev_err(dai->dev, "can't set DMIC clock rate %u, err: %d\n",
			clk_rate, err);
		return err;
	}

	regmap_update_bits(dmic->regmap,
			   /* Reg */
			   TEGRA210_DMIC_CTRL,
			   /* Mask */
			   TEGRA210_DMIC_CTRL_LRSEL_POLARITY_MASK |
			   TEGRA210_DMIC_CTRL_OSR_MASK |
			   TEGRA210_DMIC_CTRL_CHANNEL_SELECT_MASK,
			   /* Value */
			   (dmic->lrsel << LRSEL_POL_SHIFT) |
			   (dmic->osr_val << OSR_SHIFT) |
			   ((dmic->ch_select + 1) << CH_SEL_SHIFT));

	/*
	 * Use LP filter gain register to apply boost.
	 * Boost Gain Volume control has 100x factor.
	 */
	if (dmic->boost_gain)
		gain_q23 = div_u64(gain_q23 * dmic->boost_gain, 100);

	regmap_write(dmic->regmap, TEGRA210_DMIC_LP_FILTER_GAIN,
		     (unsigned int)gain_q23);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		dev_err(dai->dev, "unsupported format!\n");
		return -EOPNOTSUPP;
	}

	cif_conf.client_bits = TEGRA_ACIF_BITS_24;
	cif_conf.mono_conv = dmic->mono_to_stereo;
	cif_conf.stereo_conv = dmic->stereo_to_mono;

	tegra_set_cif(dmic->regmap, TEGRA210_DMIC_TX_CIF_CTRL, &cif_conf);

	return 0;
}

static int tegra210_dmic_get_control(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_dmic *dmic = snd_soc_component_get_drvdata(comp);

	if (strstr(kcontrol->id.name, "Boost Gain Volume"))
		ucontrol->value.integer.value[0] = dmic->boost_gain;
	else if (strstr(kcontrol->id.name, "Channel Select"))
		ucontrol->value.enumerated.item[0] = dmic->ch_select;
	else if (strstr(kcontrol->id.name, "Mono To Stereo"))
		ucontrol->value.enumerated.item[0] = dmic->mono_to_stereo;
	else if (strstr(kcontrol->id.name, "Stereo To Mono"))
		ucontrol->value.enumerated.item[0] = dmic->stereo_to_mono;
	else if (strstr(kcontrol->id.name, "OSR Value"))
		ucontrol->value.enumerated.item[0] = dmic->osr_val;
	else if (strstr(kcontrol->id.name, "LR Polarity Select"))
		ucontrol->value.enumerated.item[0] = dmic->lrsel;

	return 0;
}

static int tegra210_dmic_put_control(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_dmic *dmic = snd_soc_component_get_drvdata(comp);

	if (strstr(kcontrol->id.name, "Boost Gain Volume"))
		dmic->boost_gain = ucontrol->value.integer.value[0];
	else if (strstr(kcontrol->id.name, "Channel Select"))
		dmic->ch_select = ucontrol->value.enumerated.item[0];
	else if (strstr(kcontrol->id.name, "Mono To Stereo"))
		dmic->mono_to_stereo = ucontrol->value.enumerated.item[0];
	else if (strstr(kcontrol->id.name, "Stereo To Mono"))
		dmic->stereo_to_mono = ucontrol->value.enumerated.item[0];
	else if (strstr(kcontrol->id.name, "OSR Value"))
		dmic->osr_val = ucontrol->value.enumerated.item[0];
	else if (strstr(kcontrol->id.name, "LR Polarity Select"))
		dmic->lrsel = ucontrol->value.enumerated.item[0];

	return 0;
}

static const struct snd_soc_dai_ops tegra210_dmic_dai_ops = {
	.hw_params	= tegra210_dmic_hw_params,
};

static struct snd_soc_dai_driver tegra210_dmic_dais[] = {
	{
		.name = "DMIC-CIF",
		.capture = {
			.stream_name = "CIF-Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
	},
	{
		.name = "DMIC-DAP",
		.capture = {
			.stream_name = "DAP-Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_dmic_dai_ops,
		.symmetric_rate = 1,
	},
};

static const struct snd_soc_dapm_widget tegra210_dmic_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("TX", NULL, 0, TEGRA210_DMIC_ENABLE, 0, 0),
	SND_SOC_DAPM_MIC("MIC", NULL),
};

static const struct snd_soc_dapm_route tegra210_dmic_routes[] = {
	{ "XBAR-RX",		NULL,	"XBAR-Capture" },
	{ "XBAR-Capture",	NULL,	"CIF-Capture" },
	{ "CIF-Capture",	NULL,	"TX" },
	{ "TX",			NULL,	"DAP-Capture" },
	{ "DAP-Capture",	NULL,	"MIC" },
};

static const char * const tegra210_dmic_ch_select[] = {
	"Left", "Right", "Stereo",
};

static const struct soc_enum tegra210_dmic_ch_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_dmic_ch_select),
			tegra210_dmic_ch_select);

static const char * const tegra210_dmic_mono_conv_text[] = {
	"Zero", "Copy",
};

static const char * const tegra210_dmic_stereo_conv_text[] = {
	"CH0", "CH1", "AVG",
};

static const struct soc_enum tegra210_dmic_mono_conv_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_dmic_mono_conv_text),
			tegra210_dmic_mono_conv_text);

static const struct soc_enum tegra210_dmic_stereo_conv_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_dmic_stereo_conv_text),
			tegra210_dmic_stereo_conv_text);

static const char * const tegra210_dmic_osr_text[] = {
	"OSR_64", "OSR_128", "OSR_256",
};

static const struct soc_enum tegra210_dmic_osr_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_dmic_osr_text),
			tegra210_dmic_osr_text);

static const char * const tegra210_dmic_lrsel_text[] = {
	"Left", "Right",
};

static const struct soc_enum tegra210_dmic_lrsel_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_dmic_lrsel_text),
			tegra210_dmic_lrsel_text);

static const struct snd_kcontrol_new tegra210_dmic_controls[] = {
	SOC_SINGLE_EXT("Boost Gain Volume", 0, 0, MAX_BOOST_GAIN, 0,
		       tegra210_dmic_get_control, tegra210_dmic_put_control),
	SOC_ENUM_EXT("Channel Select", tegra210_dmic_ch_enum,
		     tegra210_dmic_get_control, tegra210_dmic_put_control),
	SOC_ENUM_EXT("Mono To Stereo",
		     tegra210_dmic_mono_conv_enum, tegra210_dmic_get_control,
		     tegra210_dmic_put_control),
	SOC_ENUM_EXT("Stereo To Mono",
		     tegra210_dmic_stereo_conv_enum, tegra210_dmic_get_control,
		     tegra210_dmic_put_control),
	SOC_ENUM_EXT("OSR Value", tegra210_dmic_osr_enum,
		     tegra210_dmic_get_control, tegra210_dmic_put_control),
	SOC_ENUM_EXT("LR Polarity Select", tegra210_dmic_lrsel_enum,
		     tegra210_dmic_get_control, tegra210_dmic_put_control),
};

static const struct snd_soc_component_driver tegra210_dmic_compnt = {
	.dapm_widgets		= tegra210_dmic_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_dmic_widgets),
	.dapm_routes		= tegra210_dmic_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_dmic_routes),
	.controls		= tegra210_dmic_controls,
	.num_controls		= ARRAY_SIZE(tegra210_dmic_controls),
};

static bool tegra210_dmic_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_DMIC_TX_INT_MASK ... TEGRA210_DMIC_TX_CIF_CTRL:
	case TEGRA210_DMIC_ENABLE ... TEGRA210_DMIC_CG:
	case TEGRA210_DMIC_CTRL:
	case TEGRA210_DMIC_DBG_CTRL:
	case TEGRA210_DMIC_DCR_BIQUAD_0_COEF_4 ... TEGRA210_DMIC_LP_BIQUAD_1_COEF_4:
		return true;
	default:
		return false;
	}
}

static bool tegra210_dmic_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra210_dmic_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA210_DMIC_TX_STATUS:
	case TEGRA210_DMIC_TX_INT_STATUS:
	case TEGRA210_DMIC_STATUS:
	case TEGRA210_DMIC_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra210_dmic_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_DMIC_TX_STATUS:
	case TEGRA210_DMIC_TX_INT_STATUS:
	case TEGRA210_DMIC_TX_INT_SET:
	case TEGRA210_DMIC_SOFT_RESET:
	case TEGRA210_DMIC_STATUS:
	case TEGRA210_DMIC_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_dmic_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = TEGRA210_DMIC_LP_BIQUAD_1_COEF_4,
	.writeable_reg = tegra210_dmic_wr_reg,
	.readable_reg = tegra210_dmic_rd_reg,
	.volatile_reg = tegra210_dmic_volatile_reg,
	.reg_defaults = tegra210_dmic_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tegra210_dmic_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static int tegra210_dmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_dmic *dmic;
	void __iomem *regs;
	int err;

	dmic = devm_kzalloc(dev, sizeof(*dmic), GFP_KERNEL);
	if (!dmic)
		return -ENOMEM;

	dmic->osr_val = DMIC_OSR_64;
	dmic->ch_select = DMIC_CH_SELECT_STEREO;
	dmic->lrsel = DMIC_LRSEL_LEFT;
	dmic->boost_gain = 0;
	dmic->stereo_to_mono = 0; /* "CH0" */

	dev_set_drvdata(dev, dmic);

	dmic->clk_dmic = devm_clk_get(dev, "dmic");
	if (IS_ERR(dmic->clk_dmic)) {
		dev_err(dev, "can't retrieve DMIC clock\n");
		return PTR_ERR(dmic->clk_dmic);
	}

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dmic->regmap = devm_regmap_init_mmio(dev, regs,
					     &tegra210_dmic_regmap_config);
	if (IS_ERR(dmic->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(dmic->regmap);
	}

	regcache_cache_only(dmic->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_dmic_compnt,
					      tegra210_dmic_dais,
					      ARRAY_SIZE(tegra210_dmic_dais));
	if (err) {
		dev_err(dev, "can't register DMIC component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static int tegra210_dmic_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra210_dmic_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_dmic_runtime_suspend,
			   tegra210_dmic_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra210_dmic_of_match[] = {
	{ .compatible = "nvidia,tegra210-dmic" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_dmic_of_match);

static struct platform_driver tegra210_dmic_driver = {
	.driver = {
		.name = "tegra210-dmic",
		.of_match_table = tegra210_dmic_of_match,
		.pm = &tegra210_dmic_pm_ops,
	},
	.probe = tegra210_dmic_probe,
	.remove = tegra210_dmic_remove,
};
module_platform_driver(tegra210_dmic_driver)

MODULE_AUTHOR("Rahul Mittal <rmittal@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ASoC DMIC driver");
MODULE_LICENSE("GPL v2");
