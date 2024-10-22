// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_ope.c - Tegra210 OPE driver
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_mbdrc.h"
#include "tegra210_ope.h"
#include "tegra210_peq.h"
#include "tegra_cif.h"

static const struct reg_default tegra210_ope_reg_defaults[] = {
	{ TEGRA210_OPE_RX_INT_MASK, 0x00000001},
	{ TEGRA210_OPE_RX_CIF_CTRL, 0x00007700},
	{ TEGRA210_OPE_TX_INT_MASK, 0x00000001},
	{ TEGRA210_OPE_TX_CIF_CTRL, 0x00007700},
	{ TEGRA210_OPE_CG, 0x1},
};

static int tegra210_ope_set_audio_cif(struct tegra210_ope *ope,
				      struct snd_pcm_hw_params *params,
				      unsigned int reg)
{
	int channels, audio_bits;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);
	if (channels < 2)
		return -EINVAL;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	cif_conf.audio_bits = audio_bits;
	cif_conf.client_bits = audio_bits;

	tegra_set_cif(ope->regmap, reg, &cif_conf);

	return 0;
}

static int tegra210_ope_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra210_ope *ope = snd_soc_dai_get_drvdata(dai);
	int err;

	/* Set RX and TX CIF */
	err = tegra210_ope_set_audio_cif(ope, params,
					 TEGRA210_OPE_RX_CIF_CTRL);
	if (err) {
		dev_err(dev, "Can't set OPE RX CIF: %d\n", err);
		return err;
	}

	err = tegra210_ope_set_audio_cif(ope, params,
					 TEGRA210_OPE_TX_CIF_CTRL);
	if (err) {
		dev_err(dev, "Can't set OPE TX CIF: %d\n", err);
		return err;
	}

	tegra210_mbdrc_hw_params(dai->component);

	return err;
}

static int tegra210_ope_component_probe(struct snd_soc_component *cmpnt)
{
	struct tegra210_ope *ope = dev_get_drvdata(cmpnt->dev);

	tegra210_peq_component_init(cmpnt);
	tegra210_mbdrc_component_init(cmpnt);

	/*
	 * The OPE, PEQ and MBDRC functionalities are combined under one
	 * device registered by OPE driver. In fact OPE HW block includes
	 * sub blocks PEQ and MBDRC. However driver registers separate
	 * regmap interfaces for each of these. ASoC core depends on
	 * dev_get_regmap() to populate the regmap field for a given ASoC
	 * component. A component can have one regmap reference and since
	 * the DAPM routes depend on OPE regmap only, below explicit
	 * assignment is done to highlight this. This is needed for ASoC
	 * core to access correct regmap during DAPM path setup.
	 */
	snd_soc_component_init_regmap(cmpnt, ope->regmap);

	return 0;
}

static const struct snd_soc_dai_ops tegra210_ope_dai_ops = {
	.hw_params	= tegra210_ope_hw_params,
};

static struct snd_soc_dai_driver tegra210_ope_dais[] = {
	{
		.name = "OPE-RX-CIF",
		.playback = {
			.stream_name = "RX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "RX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
	},
	{
		.name = "OPE-TX-CIF",
		.playback = {
			.stream_name = "TX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "TX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_ope_dai_ops,
	}
};

static const struct snd_soc_dapm_widget tegra210_ope_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX", NULL, 0, TEGRA210_OPE_ENABLE,
			     TEGRA210_OPE_EN_SHIFT, 0),
};

#define OPE_ROUTES(sname)					\
	{ "RX XBAR-" sname,	NULL,	"XBAR-TX" },		\
	{ "RX-CIF-" sname,	NULL,	"RX XBAR-" sname },	\
	{ "RX",			NULL,	"RX-CIF-" sname },	\
	{ "TX-CIF-" sname,	NULL,	"TX" },			\
	{ "TX XBAR-" sname,	NULL,	"TX-CIF-" sname },	\
	{ "XBAR-RX",		NULL,	"TX XBAR-" sname }

static const struct snd_soc_dapm_route tegra210_ope_routes[] = {
	{ "TX", NULL, "RX" },
	OPE_ROUTES("Playback"),
	OPE_ROUTES("Capture"),
};

static const char * const tegra210_ope_data_dir_text[] = {
	"MBDRC to PEQ",
	"PEQ to MBDRC"
};

static const struct soc_enum tegra210_ope_data_dir_enum =
	SOC_ENUM_SINGLE(TEGRA210_OPE_DIR, TEGRA210_OPE_DIR_SHIFT,
			2, tegra210_ope_data_dir_text);

static int tegra210_ope_get_data_dir(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.enumerated.item[0] = ope->data_dir;

	return 0;
}

static int tegra210_ope_put_data_dir(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_ope *ope = snd_soc_component_get_drvdata(cmpnt);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == ope->data_dir)
		return 0;

	ope->data_dir = value;

	return 1;
}

static const struct snd_kcontrol_new tegra210_ope_controls[] = {
	SOC_ENUM_EXT("Data Flow Direction", tegra210_ope_data_dir_enum,
		     tegra210_ope_get_data_dir, tegra210_ope_put_data_dir),
};

static const struct snd_soc_component_driver tegra210_ope_cmpnt = {
	.probe			= tegra210_ope_component_probe,
	.dapm_widgets		= tegra210_ope_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_ope_widgets),
	.dapm_routes		= tegra210_ope_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_ope_routes),
	.controls		= tegra210_ope_controls,
	.num_controls		= ARRAY_SIZE(tegra210_ope_controls),
};

static bool tegra210_ope_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_OPE_RX_INT_MASK ... TEGRA210_OPE_RX_CIF_CTRL:
	case TEGRA210_OPE_TX_INT_MASK ... TEGRA210_OPE_TX_CIF_CTRL:
	case TEGRA210_OPE_ENABLE ... TEGRA210_OPE_CG:
	case TEGRA210_OPE_DIR:
		return true;
	default:
		return false;
	}
}

static bool tegra210_ope_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra210_ope_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA210_OPE_RX_STATUS:
	case TEGRA210_OPE_RX_INT_STATUS:
	case TEGRA210_OPE_TX_STATUS:
	case TEGRA210_OPE_TX_INT_STATUS:
	case TEGRA210_OPE_STATUS:
	case TEGRA210_OPE_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra210_ope_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_OPE_RX_STATUS:
	case TEGRA210_OPE_RX_INT_STATUS:
	case TEGRA210_OPE_TX_STATUS:
	case TEGRA210_OPE_TX_INT_STATUS:
	case TEGRA210_OPE_SOFT_RESET:
	case TEGRA210_OPE_STATUS:
	case TEGRA210_OPE_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_ope_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_OPE_DIR,
	.writeable_reg		= tegra210_ope_wr_reg,
	.readable_reg		= tegra210_ope_rd_reg,
	.volatile_reg		= tegra210_ope_volatile_reg,
	.reg_defaults		= tegra210_ope_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_ope_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static int tegra210_ope_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_ope *ope;
	void __iomem *regs;
	int err;

	ope = devm_kzalloc(dev, sizeof(*ope), GFP_KERNEL);
	if (!ope)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ope->regmap = devm_regmap_init_mmio(dev, regs,
					    &tegra210_ope_regmap_config);
	if (IS_ERR(ope->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(ope->regmap);
	}

	regcache_cache_only(ope->regmap, true);

	dev_set_drvdata(dev, ope);

	err = tegra210_peq_regmap_init(pdev);
	if (err < 0) {
		dev_err(dev, "PEQ init failed\n");
		return err;
	}

	err = tegra210_mbdrc_regmap_init(pdev);
	if (err < 0) {
		dev_err(dev, "MBDRC init failed\n");
		return err;
	}

	err = devm_snd_soc_register_component(dev, &tegra210_ope_cmpnt,
					      tegra210_ope_dais,
					      ARRAY_SIZE(tegra210_ope_dais));
	if (err) {
		dev_err(dev, "can't register OPE component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static void tegra210_ope_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused tegra210_ope_runtime_suspend(struct device *dev)
{
	struct tegra210_ope *ope = dev_get_drvdata(dev);

	tegra210_peq_save(ope->peq_regmap, ope->peq_biquad_gains,
			  ope->peq_biquad_shifts);

	regcache_cache_only(ope->mbdrc_regmap, true);
	regcache_cache_only(ope->peq_regmap, true);
	regcache_cache_only(ope->regmap, true);

	regcache_mark_dirty(ope->regmap);
	regcache_mark_dirty(ope->peq_regmap);
	regcache_mark_dirty(ope->mbdrc_regmap);

	return 0;
}

static int __maybe_unused tegra210_ope_runtime_resume(struct device *dev)
{
	struct tegra210_ope *ope = dev_get_drvdata(dev);

	regcache_cache_only(ope->regmap, false);
	regcache_cache_only(ope->peq_regmap, false);
	regcache_cache_only(ope->mbdrc_regmap, false);

	regcache_sync(ope->regmap);
	regcache_sync(ope->peq_regmap);
	regcache_sync(ope->mbdrc_regmap);

	tegra210_peq_restore(ope->peq_regmap, ope->peq_biquad_gains,
			     ope->peq_biquad_shifts);

	return 0;
}

static const struct dev_pm_ops tegra210_ope_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_ope_runtime_suspend,
			   tegra210_ope_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra210_ope_of_match[] = {
	{ .compatible = "nvidia,tegra210-ope" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_ope_of_match);

static struct platform_driver tegra210_ope_driver = {
	.driver = {
		.name = "tegra210-ope",
		.of_match_table = tegra210_ope_of_match,
		.pm = &tegra210_ope_pm_ops,
	},
	.probe = tegra210_ope_probe,
	.remove = tegra210_ope_remove,
};
module_platform_driver(tegra210_ope_driver)

MODULE_AUTHOR("Sumit Bhattacharya <sumitb@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 OPE ASoC driver");
MODULE_LICENSE("GPL");
