// SPDX-License-Identifier: GPL-2.0-only
//
// tegra186_dspk.c - Tegra186 DSPK driver
//
// Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "tegra186_dspk.h"
#include "tegra_cif.h"

static const struct reg_default tegra186_dspk_reg_defaults[] = {
	{ TEGRA186_DSPK_RX_INT_MASK, 0x00000007 },
	{ TEGRA186_DSPK_RX_CIF_CTRL, 0x00007700 },
	{ TEGRA186_DSPK_CG,	     0x00000001 },
	{ TEGRA186_DSPK_CORE_CTRL,   0x00000310 },
	{ TEGRA186_DSPK_CODEC_CTRL,  0x03000000 },
};

static int tegra186_dspk_get_fifo_th(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = dspk->rx_fifo_th;

	return 0;
}

static int tegra186_dspk_put_fifo_th(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	int value = ucontrol->value.integer.value[0];

	if (value == dspk->rx_fifo_th)
		return 0;

	dspk->rx_fifo_th = value;

	return 1;
}

static int tegra186_dspk_get_osr_val(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = dspk->osr_val;

	return 0;
}

static int tegra186_dspk_put_osr_val(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == dspk->osr_val)
		return 0;

	dspk->osr_val = value;

	return 1;
}

static int tegra186_dspk_get_pol_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = dspk->lrsel;

	return 0;
}

static int tegra186_dspk_put_pol_sel(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == dspk->lrsel)
		return 0;

	dspk->lrsel = value;

	return 1;
}

static int tegra186_dspk_get_ch_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = dspk->ch_sel;

	return 0;
}

static int tegra186_dspk_put_ch_sel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == dspk->ch_sel)
		return 0;

	dspk->ch_sel = value;

	return 1;
}

static int tegra186_dspk_get_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = dspk->mono_to_stereo;

	return 0;
}

static int tegra186_dspk_put_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == dspk->mono_to_stereo)
		return 0;

	dspk->mono_to_stereo = value;

	return 1;
}

static int tegra186_dspk_get_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = dspk->stereo_to_mono;

	return 0;
}

static int tegra186_dspk_put_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_dspk *dspk = snd_soc_component_get_drvdata(codec);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == dspk->stereo_to_mono)
		return 0;

	dspk->stereo_to_mono = value;

	return 1;
}

static int __maybe_unused tegra186_dspk_runtime_suspend(struct device *dev)
{
	struct tegra186_dspk *dspk = dev_get_drvdata(dev);

	regcache_cache_only(dspk->regmap, true);
	regcache_mark_dirty(dspk->regmap);

	clk_disable_unprepare(dspk->clk_dspk);

	return 0;
}

static int __maybe_unused tegra186_dspk_runtime_resume(struct device *dev)
{
	struct tegra186_dspk *dspk = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(dspk->clk_dspk);
	if (err) {
		dev_err(dev, "failed to enable DSPK clock, err: %d\n", err);
		return err;
	}

	regcache_cache_only(dspk->regmap, false);
	regcache_sync(dspk->regmap);

	return 0;
}

static int tegra186_dspk_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct tegra186_dspk *dspk = snd_soc_dai_get_drvdata(dai);
	unsigned int channels, srate, dspk_clk;
	struct device *dev = dai->dev;
	struct tegra_cif_conf cif_conf;
	unsigned int max_th;
	int err;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);
	cif_conf.audio_ch = channels;

	/* Client channel */
	switch (dspk->ch_sel) {
	case DSPK_CH_SELECT_LEFT:
	case DSPK_CH_SELECT_RIGHT:
		cif_conf.client_ch = 1;
		break;
	case DSPK_CH_SELECT_STEREO:
		cif_conf.client_ch = 2;
		break;
	default:
		dev_err(dev, "Invalid DSPK client channels\n");
		return -EINVAL;
	}

	cif_conf.client_bits = TEGRA_ACIF_BITS_24;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		dev_err(dev, "unsupported format!\n");
		return -EOPNOTSUPP;
	}

	srate = params_rate(params);

	/* RX FIFO threshold in terms of frames */
	max_th = (TEGRA186_DSPK_RX_FIFO_DEPTH / cif_conf.audio_ch) - 1;

	if (dspk->rx_fifo_th > max_th)
		dspk->rx_fifo_th = max_th;

	cif_conf.threshold = dspk->rx_fifo_th;
	cif_conf.mono_conv = dspk->mono_to_stereo;
	cif_conf.stereo_conv = dspk->stereo_to_mono;

	tegra_set_cif(dspk->regmap, TEGRA186_DSPK_RX_CIF_CTRL,
		      &cif_conf);

	/*
	 * DSPK clock and PDM codec clock should be synchronous with 4:1 ratio,
	 * this is because it takes 4 clock cycles to send out one sample to
	 * codec by sigma delta modulator. Finally the clock rate is a multiple
	 * of 'Over Sampling Ratio', 'Sample Rate' and 'Interface Clock Ratio'.
	 */
	dspk_clk = (DSPK_OSR_FACTOR << dspk->osr_val) * srate * DSPK_CLK_RATIO;

	err = clk_set_rate(dspk->clk_dspk, dspk_clk);
	if (err) {
		dev_err(dev, "can't set DSPK clock rate %u, err: %d\n",
			dspk_clk, err);

		return err;
	}

	regmap_update_bits(dspk->regmap,
			   /* Reg */
			   TEGRA186_DSPK_CORE_CTRL,
			   /* Mask */
			   TEGRA186_DSPK_OSR_MASK |
			   TEGRA186_DSPK_CHANNEL_SELECT_MASK |
			   TEGRA186_DSPK_CTRL_LRSEL_POLARITY_MASK,
			   /* Value */
			   (dspk->osr_val << DSPK_OSR_SHIFT) |
			   ((dspk->ch_sel + 1) << CH_SEL_SHIFT) |
			   (dspk->lrsel << LRSEL_POL_SHIFT));

	return 0;
}

static const struct snd_soc_dai_ops tegra186_dspk_dai_ops = {
	.hw_params	= tegra186_dspk_hw_params,
};

static struct snd_soc_dai_driver tegra186_dspk_dais[] = {
	{
	    .name = "DSPK-CIF",
	    .playback = {
		.stream_name = "CIF-Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	    },
	},
	{
	    .name = "DSPK-DAP",
	    .playback = {
		.stream_name = "DAP-Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	    },
	    .ops = &tegra186_dspk_dai_ops,
	    .symmetric_rate = 1,
	},
};

static const struct snd_soc_dapm_widget tegra186_dspk_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", NULL, 0, TEGRA186_DSPK_ENABLE, 0, 0),
	SND_SOC_DAPM_SPK("SPK", NULL),
};

static const struct snd_soc_dapm_route tegra186_dspk_routes[] = {
	{ "XBAR-Playback",	NULL,	"XBAR-TX" },
	{ "CIF-Playback",	NULL,	"XBAR-Playback" },
	{ "RX",			NULL,	"CIF-Playback" },
	{ "DAP-Playback",	NULL,	"RX" },
	{ "SPK",		NULL,	"DAP-Playback" },
};

static const char * const tegra186_dspk_ch_sel_text[] = {
	"Left", "Right", "Stereo",
};

static const struct soc_enum tegra186_dspk_ch_sel_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tegra186_dspk_ch_sel_text),
			tegra186_dspk_ch_sel_text);

static const char * const tegra186_dspk_osr_text[] = {
	"OSR_32", "OSR_64", "OSR_128", "OSR_256",
};

static const struct soc_enum tegra186_dspk_osr_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tegra186_dspk_osr_text),
			tegra186_dspk_osr_text);

static const char * const tegra186_dspk_lrsel_text[] = {
	"Left", "Right",
};

static const char * const tegra186_dspk_mono_conv_text[] = {
	"Zero", "Copy",
};

static const struct soc_enum tegra186_dspk_mono_conv_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(tegra186_dspk_mono_conv_text),
			tegra186_dspk_mono_conv_text);

static const char * const tegra186_dspk_stereo_conv_text[] = {
	"CH0", "CH1", "AVG",
};

static const struct soc_enum tegra186_dspk_stereo_conv_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(tegra186_dspk_stereo_conv_text),
			tegra186_dspk_stereo_conv_text);

static const struct soc_enum tegra186_dspk_lrsel_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(tegra186_dspk_lrsel_text),
			tegra186_dspk_lrsel_text);

static const struct snd_kcontrol_new tegrat186_dspk_controls[] = {
	SOC_SINGLE_EXT("FIFO Threshold", SND_SOC_NOPM, 0,
		       TEGRA186_DSPK_RX_FIFO_DEPTH - 1, 0,
		       tegra186_dspk_get_fifo_th, tegra186_dspk_put_fifo_th),
	SOC_ENUM_EXT("OSR Value", tegra186_dspk_osr_enum,
		     tegra186_dspk_get_osr_val, tegra186_dspk_put_osr_val),
	SOC_ENUM_EXT("LR Polarity Select", tegra186_dspk_lrsel_enum,
		     tegra186_dspk_get_pol_sel, tegra186_dspk_put_pol_sel),
	SOC_ENUM_EXT("Channel Select", tegra186_dspk_ch_sel_enum,
		     tegra186_dspk_get_ch_sel, tegra186_dspk_put_ch_sel),
	SOC_ENUM_EXT("Mono To Stereo", tegra186_dspk_mono_conv_enum,
		     tegra186_dspk_get_mono_to_stereo,
		     tegra186_dspk_put_mono_to_stereo),
	SOC_ENUM_EXT("Stereo To Mono", tegra186_dspk_stereo_conv_enum,
		     tegra186_dspk_get_stereo_to_mono,
		     tegra186_dspk_put_stereo_to_mono),
};

static const struct snd_soc_component_driver tegra186_dspk_cmpnt = {
	.dapm_widgets = tegra186_dspk_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra186_dspk_widgets),
	.dapm_routes = tegra186_dspk_routes,
	.num_dapm_routes = ARRAY_SIZE(tegra186_dspk_routes),
	.controls = tegrat186_dspk_controls,
	.num_controls = ARRAY_SIZE(tegrat186_dspk_controls),
};

static bool tegra186_dspk_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA186_DSPK_RX_INT_MASK ... TEGRA186_DSPK_RX_CIF_CTRL:
	case TEGRA186_DSPK_ENABLE ... TEGRA186_DSPK_CG:
	case TEGRA186_DSPK_CORE_CTRL ... TEGRA186_DSPK_CODEC_CTRL:
		return true;
	default:
		return false;
	}
}

static bool tegra186_dspk_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra186_dspk_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA186_DSPK_RX_STATUS:
	case TEGRA186_DSPK_RX_INT_STATUS:
	case TEGRA186_DSPK_STATUS:
	case TEGRA186_DSPK_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra186_dspk_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA186_DSPK_RX_STATUS:
	case TEGRA186_DSPK_RX_INT_STATUS:
	case TEGRA186_DSPK_STATUS:
	case TEGRA186_DSPK_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra186_dspk_regmap = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA186_DSPK_CODEC_CTRL,
	.writeable_reg		= tegra186_dspk_wr_reg,
	.readable_reg		= tegra186_dspk_rd_reg,
	.volatile_reg		= tegra186_dspk_volatile_reg,
	.reg_defaults		= tegra186_dspk_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra186_dspk_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra186_dspk_of_match[] = {
	{ .compatible = "nvidia,tegra186-dspk" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra186_dspk_of_match);

static int tegra186_dspk_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra186_dspk *dspk;
	void __iomem *regs;
	int err;

	dspk = devm_kzalloc(dev, sizeof(*dspk), GFP_KERNEL);
	if (!dspk)
		return -ENOMEM;

	dspk->osr_val = DSPK_OSR_64;
	dspk->lrsel = DSPK_LRSEL_LEFT;
	dspk->ch_sel = DSPK_CH_SELECT_STEREO;
	dspk->mono_to_stereo = 0; /* "Zero" */

	dev_set_drvdata(dev, dspk);

	dspk->clk_dspk = devm_clk_get(dev, "dspk");
	if (IS_ERR(dspk->clk_dspk)) {
		dev_err(dev, "can't retrieve DSPK clock\n");
		return PTR_ERR(dspk->clk_dspk);
	}

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dspk->regmap = devm_regmap_init_mmio(dev, regs, &tegra186_dspk_regmap);
	if (IS_ERR(dspk->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(dspk->regmap);
	}

	regcache_cache_only(dspk->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra186_dspk_cmpnt,
					      tegra186_dspk_dais,
					      ARRAY_SIZE(tegra186_dspk_dais));
	if (err) {
		dev_err(dev, "can't register DSPK component, err: %d\n",
			err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static void tegra186_dspk_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops tegra186_dspk_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra186_dspk_runtime_suspend,
			   tegra186_dspk_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra186_dspk_driver = {
	.driver = {
		.name = "tegra186-dspk",
		.of_match_table = tegra186_dspk_of_match,
		.pm = &tegra186_dspk_pm_ops,
	},
	.probe = tegra186_dspk_platform_probe,
	.remove_new = tegra186_dspk_platform_remove,
};
module_platform_driver(tegra186_dspk_driver);

MODULE_AUTHOR("Mohan Kumar <mkumard@nvidia.com>");
MODULE_AUTHOR("Sameer Pujar <spujar@nvidia.com>");
MODULE_DESCRIPTION("Tegra186 ASoC DSPK driver");
MODULE_LICENSE("GPL v2");
