// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_adx.c - Tegra210 ADX driver
//
// Copyright (c) 2021 NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_adx.h"
#include "tegra_cif.h"

static const struct reg_default tegra210_adx_reg_defaults[] = {
	{ TEGRA210_ADX_RX_INT_MASK, 0x00000001},
	{ TEGRA210_ADX_RX_CIF_CTRL, 0x00007000},
	{ TEGRA210_ADX_TX_INT_MASK, 0x0000000f },
	{ TEGRA210_ADX_TX1_CIF_CTRL, 0x00007000},
	{ TEGRA210_ADX_TX2_CIF_CTRL, 0x00007000},
	{ TEGRA210_ADX_TX3_CIF_CTRL, 0x00007000},
	{ TEGRA210_ADX_TX4_CIF_CTRL, 0x00007000},
	{ TEGRA210_ADX_CG, 0x1},
	{ TEGRA210_ADX_CFG_RAM_CTRL, 0x00004000},
};

static void tegra210_adx_write_map_ram(struct tegra210_adx *adx)
{
	int i;

	regmap_write(adx->regmap, TEGRA210_ADX_CFG_RAM_CTRL,
		     TEGRA210_ADX_CFG_RAM_CTRL_SEQ_ACCESS_EN |
		     TEGRA210_ADX_CFG_RAM_CTRL_ADDR_INIT_EN |
		     TEGRA210_ADX_CFG_RAM_CTRL_RW_WRITE);

	for (i = 0; i < TEGRA210_ADX_RAM_DEPTH; i++)
		regmap_write(adx->regmap, TEGRA210_ADX_CFG_RAM_DATA,
			     adx->map[i]);

	regmap_write(adx->regmap, TEGRA210_ADX_IN_BYTE_EN0, adx->byte_mask[0]);
	regmap_write(adx->regmap, TEGRA210_ADX_IN_BYTE_EN1, adx->byte_mask[1]);
}

static int tegra210_adx_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra210_adx *adx = snd_soc_dai_get_drvdata(dai);
	unsigned int val;
	int err;

	/* Ensure if ADX status is disabled */
	err = regmap_read_poll_timeout_atomic(adx->regmap, TEGRA210_ADX_STATUS,
					      val, !(val & 0x1), 10, 10000);
	if (err < 0) {
		dev_err(dai->dev, "failed to stop ADX, err = %d\n", err);
		return err;
	}

	/*
	 * Soft Reset: Below performs module soft reset which clears
	 * all FSM logic, flushes flow control of FIFO and resets the
	 * state register. It also brings module back to disabled
	 * state (without flushing the data in the pipe).
	 */
	regmap_update_bits(adx->regmap, TEGRA210_ADX_SOFT_RESET,
			   TEGRA210_ADX_SOFT_RESET_SOFT_RESET_MASK,
			   TEGRA210_ADX_SOFT_RESET_SOFT_EN);

	err = regmap_read_poll_timeout(adx->regmap, TEGRA210_ADX_SOFT_RESET,
				       val, !(val & 0x1), 10, 10000);
	if (err < 0) {
		dev_err(dai->dev, "failed to reset ADX, err = %d\n", err);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra210_adx_runtime_suspend(struct device *dev)
{
	struct tegra210_adx *adx = dev_get_drvdata(dev);

	regcache_cache_only(adx->regmap, true);
	regcache_mark_dirty(adx->regmap);

	return 0;
}

static int __maybe_unused tegra210_adx_runtime_resume(struct device *dev)
{
	struct tegra210_adx *adx = dev_get_drvdata(dev);

	regcache_cache_only(adx->regmap, false);
	regcache_sync(adx->regmap);

	tegra210_adx_write_map_ram(adx);

	return 0;
}

static int tegra210_adx_set_audio_cif(struct snd_soc_dai *dai,
				      unsigned int channels,
				      unsigned int format,
				      unsigned int reg)
{
	struct tegra210_adx *adx = snd_soc_dai_get_drvdata(dai);
	struct tegra_cif_conf cif_conf;
	int audio_bits;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	if (channels < 1 || channels > 16)
		return -EINVAL;

	switch (format) {
	case SNDRV_PCM_FORMAT_S8:
		audio_bits = TEGRA_ACIF_BITS_8;
		break;
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

	tegra_set_cif(adx->regmap, reg, &cif_conf);

	return 0;
}

static int tegra210_adx_out_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	return tegra210_adx_set_audio_cif(dai, params_channels(params),
			params_format(params),
			TEGRA210_ADX_TX1_CIF_CTRL + ((dai->id - 1) * TEGRA210_ADX_AUDIOCIF_CH_STRIDE));
}

static int tegra210_adx_in_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	return tegra210_adx_set_audio_cif(dai, params_channels(params),
					  params_format(params),
					  TEGRA210_ADX_RX_CIF_CTRL);
}

static int tegra210_adx_get_byte_map(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_adx *adx = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc;
	unsigned char *bytes_map = (unsigned char *)&adx->map;
	int enabled;

	mc = (struct soc_mixer_control *)kcontrol->private_value;
	enabled = adx->byte_mask[mc->reg / 32] & (1 << (mc->reg % 32));

	if (enabled)
		ucontrol->value.integer.value[0] = bytes_map[mc->reg];
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int tegra210_adx_put_byte_map(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_adx *adx = snd_soc_component_get_drvdata(cmpnt);
	unsigned char *bytes_map = (unsigned char *)&adx->map;
	int value = ucontrol->value.integer.value[0];
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;;

	if (value == bytes_map[mc->reg])
		return 0;

	if (value >= 0 && value <= 255) {
		/* update byte map and enable slot */
		bytes_map[mc->reg] = value;
		adx->byte_mask[mc->reg / 32] |= (1 << (mc->reg % 32));
	} else {
		/* reset byte map and disable slot */
		bytes_map[mc->reg] = 0;
		adx->byte_mask[mc->reg / 32] &= ~(1 << (mc->reg % 32));
	}

	return 1;
}

static const struct snd_soc_dai_ops tegra210_adx_in_dai_ops = {
	.hw_params	= tegra210_adx_in_hw_params,
	.startup	= tegra210_adx_startup,
};

static const struct snd_soc_dai_ops tegra210_adx_out_dai_ops = {
	.hw_params	= tegra210_adx_out_hw_params,
};

#define IN_DAI							\
	{							\
		.name = "ADX-RX-CIF",				\
		.playback = {					\
			.stream_name = "RX-CIF-Playback",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "RX-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_adx_in_dai_ops,		\
	}

#define OUT_DAI(id)						\
	{							\
		.name = "ADX-TX" #id "-CIF",			\
		.playback = {					\
			.stream_name = "TX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "TX" #id "-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_adx_out_dai_ops,		\
	}

static struct snd_soc_dai_driver tegra210_adx_dais[] = {
	IN_DAI,
	OUT_DAI(1),
	OUT_DAI(2),
	OUT_DAI(3),
	OUT_DAI(4),
};

static const struct snd_soc_dapm_widget tegra210_adx_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", NULL, 0, TEGRA210_ADX_ENABLE,
			    TEGRA210_ADX_ENABLE_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("TX1", NULL, 0, TEGRA210_ADX_CTRL, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX2", NULL, 0, TEGRA210_ADX_CTRL, 1, 0),
	SND_SOC_DAPM_AIF_OUT("TX3", NULL, 0, TEGRA210_ADX_CTRL, 2, 0),
	SND_SOC_DAPM_AIF_OUT("TX4", NULL, 0, TEGRA210_ADX_CTRL, 3, 0),
};

#define STREAM_ROUTES(id, sname)					  \
	{ "XBAR-" sname,		NULL,	"XBAR-TX" },		  \
	{ "RX-CIF-" sname,		NULL,	"XBAR-" sname },	  \
	{ "RX",				NULL,	"RX-CIF-" sname },	  \
	{ "TX" #id,			NULL,	"RX" },			  \
	{ "TX" #id "-CIF-" sname,	NULL,	"TX" #id },		  \
	{ "TX" #id " XBAR-" sname,	NULL,	"TX" #id "-CIF-" sname }, \
	{ "TX" #id " XBAR-RX",		NULL,	"TX" #id " XBAR-" sname }

#define ADX_ROUTES(id)			\
	STREAM_ROUTES(id, "Playback"),	\
	STREAM_ROUTES(id, "Capture")

#define STREAM_ROUTES(id, sname)					  \
	{ "XBAR-" sname,		NULL,	"XBAR-TX" },		  \
	{ "RX-CIF-" sname,		NULL,	"XBAR-" sname },	  \
	{ "RX",				NULL,	"RX-CIF-" sname },	  \
	{ "TX" #id,			NULL,	"RX" },			  \
	{ "TX" #id "-CIF-" sname,	NULL,	"TX" #id },		  \
	{ "TX" #id " XBAR-" sname,	NULL,	"TX" #id "-CIF-" sname }, \
	{ "TX" #id " XBAR-RX",		NULL,	"TX" #id " XBAR-" sname }

#define ADX_ROUTES(id)			\
	STREAM_ROUTES(id, "Playback"),	\
	STREAM_ROUTES(id, "Capture")

static const struct snd_soc_dapm_route tegra210_adx_routes[] = {
	ADX_ROUTES(1),
	ADX_ROUTES(2),
	ADX_ROUTES(3),
	ADX_ROUTES(4),
};

#define TEGRA210_ADX_BYTE_MAP_CTRL(reg)			 \
	SOC_SINGLE_EXT("Byte Map " #reg, reg, 0, 256, 0, \
		       tegra210_adx_get_byte_map,	 \
		       tegra210_adx_put_byte_map)

static struct snd_kcontrol_new tegra210_adx_controls[] = {
	TEGRA210_ADX_BYTE_MAP_CTRL(0),
	TEGRA210_ADX_BYTE_MAP_CTRL(1),
	TEGRA210_ADX_BYTE_MAP_CTRL(2),
	TEGRA210_ADX_BYTE_MAP_CTRL(3),
	TEGRA210_ADX_BYTE_MAP_CTRL(4),
	TEGRA210_ADX_BYTE_MAP_CTRL(5),
	TEGRA210_ADX_BYTE_MAP_CTRL(6),
	TEGRA210_ADX_BYTE_MAP_CTRL(7),
	TEGRA210_ADX_BYTE_MAP_CTRL(8),
	TEGRA210_ADX_BYTE_MAP_CTRL(9),
	TEGRA210_ADX_BYTE_MAP_CTRL(10),
	TEGRA210_ADX_BYTE_MAP_CTRL(11),
	TEGRA210_ADX_BYTE_MAP_CTRL(12),
	TEGRA210_ADX_BYTE_MAP_CTRL(13),
	TEGRA210_ADX_BYTE_MAP_CTRL(14),
	TEGRA210_ADX_BYTE_MAP_CTRL(15),
	TEGRA210_ADX_BYTE_MAP_CTRL(16),
	TEGRA210_ADX_BYTE_MAP_CTRL(17),
	TEGRA210_ADX_BYTE_MAP_CTRL(18),
	TEGRA210_ADX_BYTE_MAP_CTRL(19),
	TEGRA210_ADX_BYTE_MAP_CTRL(20),
	TEGRA210_ADX_BYTE_MAP_CTRL(21),
	TEGRA210_ADX_BYTE_MAP_CTRL(22),
	TEGRA210_ADX_BYTE_MAP_CTRL(23),
	TEGRA210_ADX_BYTE_MAP_CTRL(24),
	TEGRA210_ADX_BYTE_MAP_CTRL(25),
	TEGRA210_ADX_BYTE_MAP_CTRL(26),
	TEGRA210_ADX_BYTE_MAP_CTRL(27),
	TEGRA210_ADX_BYTE_MAP_CTRL(28),
	TEGRA210_ADX_BYTE_MAP_CTRL(29),
	TEGRA210_ADX_BYTE_MAP_CTRL(30),
	TEGRA210_ADX_BYTE_MAP_CTRL(31),
	TEGRA210_ADX_BYTE_MAP_CTRL(32),
	TEGRA210_ADX_BYTE_MAP_CTRL(33),
	TEGRA210_ADX_BYTE_MAP_CTRL(34),
	TEGRA210_ADX_BYTE_MAP_CTRL(35),
	TEGRA210_ADX_BYTE_MAP_CTRL(36),
	TEGRA210_ADX_BYTE_MAP_CTRL(37),
	TEGRA210_ADX_BYTE_MAP_CTRL(38),
	TEGRA210_ADX_BYTE_MAP_CTRL(39),
	TEGRA210_ADX_BYTE_MAP_CTRL(40),
	TEGRA210_ADX_BYTE_MAP_CTRL(41),
	TEGRA210_ADX_BYTE_MAP_CTRL(42),
	TEGRA210_ADX_BYTE_MAP_CTRL(43),
	TEGRA210_ADX_BYTE_MAP_CTRL(44),
	TEGRA210_ADX_BYTE_MAP_CTRL(45),
	TEGRA210_ADX_BYTE_MAP_CTRL(46),
	TEGRA210_ADX_BYTE_MAP_CTRL(47),
	TEGRA210_ADX_BYTE_MAP_CTRL(48),
	TEGRA210_ADX_BYTE_MAP_CTRL(49),
	TEGRA210_ADX_BYTE_MAP_CTRL(50),
	TEGRA210_ADX_BYTE_MAP_CTRL(51),
	TEGRA210_ADX_BYTE_MAP_CTRL(52),
	TEGRA210_ADX_BYTE_MAP_CTRL(53),
	TEGRA210_ADX_BYTE_MAP_CTRL(54),
	TEGRA210_ADX_BYTE_MAP_CTRL(55),
	TEGRA210_ADX_BYTE_MAP_CTRL(56),
	TEGRA210_ADX_BYTE_MAP_CTRL(57),
	TEGRA210_ADX_BYTE_MAP_CTRL(58),
	TEGRA210_ADX_BYTE_MAP_CTRL(59),
	TEGRA210_ADX_BYTE_MAP_CTRL(60),
	TEGRA210_ADX_BYTE_MAP_CTRL(61),
	TEGRA210_ADX_BYTE_MAP_CTRL(62),
	TEGRA210_ADX_BYTE_MAP_CTRL(63),
};

static const struct snd_soc_component_driver tegra210_adx_cmpnt = {
	.dapm_widgets		= tegra210_adx_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_adx_widgets),
	.dapm_routes		= tegra210_adx_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_adx_routes),
	.controls		= tegra210_adx_controls,
	.num_controls		= ARRAY_SIZE(tegra210_adx_controls),
};

static bool tegra210_adx_wr_reg(struct device *dev,
				unsigned int reg)
{
	switch (reg) {
	case TEGRA210_ADX_TX_INT_MASK ... TEGRA210_ADX_TX4_CIF_CTRL:
	case TEGRA210_ADX_RX_INT_MASK ... TEGRA210_ADX_RX_CIF_CTRL:
	case TEGRA210_ADX_ENABLE ... TEGRA210_ADX_CG:
	case TEGRA210_ADX_CTRL ... TEGRA210_ADX_IN_BYTE_EN1:
	case TEGRA210_ADX_CFG_RAM_CTRL ... TEGRA210_ADX_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_adx_rd_reg(struct device *dev,
				unsigned int reg)
{
	switch (reg) {
	case TEGRA210_ADX_RX_STATUS ... TEGRA210_ADX_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_adx_volatile_reg(struct device *dev,
				unsigned int reg)
{
	switch (reg) {
	case TEGRA210_ADX_RX_STATUS:
	case TEGRA210_ADX_RX_INT_STATUS:
	case TEGRA210_ADX_RX_INT_SET:
	case TEGRA210_ADX_TX_STATUS:
	case TEGRA210_ADX_TX_INT_STATUS:
	case TEGRA210_ADX_TX_INT_SET:
	case TEGRA210_ADX_SOFT_RESET:
	case TEGRA210_ADX_STATUS:
	case TEGRA210_ADX_INT_STATUS:
	case TEGRA210_ADX_CFG_RAM_CTRL:
	case TEGRA210_ADX_CFG_RAM_DATA:
		return true;
	default:
		break;
	}

	return false;
}

static const struct regmap_config tegra210_adx_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_ADX_CFG_RAM_DATA,
	.writeable_reg		= tegra210_adx_wr_reg,
	.readable_reg		= tegra210_adx_rd_reg,
	.volatile_reg		= tegra210_adx_volatile_reg,
	.reg_defaults		= tegra210_adx_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_adx_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra210_adx_of_match[] = {
	{ .compatible = "nvidia,tegra210-adx" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_adx_of_match);

static int tegra210_adx_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_adx *adx;
	void __iomem *regs;
	int err;

	adx = devm_kzalloc(dev, sizeof(*adx), GFP_KERNEL);
	if (!adx)
		return -ENOMEM;

	dev_set_drvdata(dev, adx);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	adx->regmap = devm_regmap_init_mmio(dev, regs,
					    &tegra210_adx_regmap_config);
	if (IS_ERR(adx->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(adx->regmap);
	}

	regcache_cache_only(adx->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_adx_cmpnt,
					      tegra210_adx_dais,
					      ARRAY_SIZE(tegra210_adx_dais));
	if (err) {
		dev_err(dev, "can't register ADX component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static int tegra210_adx_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra210_adx_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_adx_runtime_suspend,
			   tegra210_adx_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra210_adx_driver = {
	.driver = {
		.name = "tegra210-adx",
		.of_match_table = tegra210_adx_of_match,
		.pm = &tegra210_adx_pm_ops,
	},
	.probe = tegra210_adx_platform_probe,
	.remove = tegra210_adx_platform_remove,
};
module_platform_driver(tegra210_adx_driver);

MODULE_AUTHOR("Arun Shamanna Lakshmi <aruns@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ADX ASoC driver");
MODULE_LICENSE("GPL v2");
