// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_amx.c - Tegra210 AMX driver
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

#include "tegra210_amx.h"
#include "tegra_cif.h"

/*
 * The counter is in terms of AHUB clock cycles. If a frame is not
 * received within these clock cycles, the AMX input channel gets
 * automatically disabled. For now the counter is calculated as a
 * function of sample rate (8 kHz) and AHUB clock (49.152 MHz).
 * If later an accurate number is needed, the counter needs to be
 * calculated at runtime.
 *
 *     count = ahub_clk / sample_rate
 */
#define TEGRA194_MAX_FRAME_IDLE_COUNT	0x1800

#define AMX_CH_REG(id, reg) ((reg) + ((id) * TEGRA210_AMX_AUDIOCIF_CH_STRIDE))

static const struct reg_default tegra210_amx_reg_defaults[] = {
	{ TEGRA210_AMX_RX_INT_MASK, 0x0000000f},
	{ TEGRA210_AMX_RX1_CIF_CTRL, 0x00007000},
	{ TEGRA210_AMX_RX2_CIF_CTRL, 0x00007000},
	{ TEGRA210_AMX_RX3_CIF_CTRL, 0x00007000},
	{ TEGRA210_AMX_RX4_CIF_CTRL, 0x00007000},
	{ TEGRA210_AMX_TX_INT_MASK, 0x00000001},
	{ TEGRA210_AMX_TX_CIF_CTRL, 0x00007000},
	{ TEGRA210_AMX_CG, 0x1},
	{ TEGRA210_AMX_CFG_RAM_CTRL, 0x00004000},
};

static void tegra210_amx_write_map_ram(struct tegra210_amx *amx)
{
	int i;

	regmap_write(amx->regmap, TEGRA210_AMX_CFG_RAM_CTRL,
		     TEGRA210_AMX_CFG_RAM_CTRL_SEQ_ACCESS_EN |
		     TEGRA210_AMX_CFG_RAM_CTRL_ADDR_INIT_EN |
		     TEGRA210_AMX_CFG_RAM_CTRL_RW_WRITE);

	for (i = 0; i < TEGRA210_AMX_RAM_DEPTH; i++)
		regmap_write(amx->regmap, TEGRA210_AMX_CFG_RAM_DATA,
			     amx->map[i]);

	regmap_write(amx->regmap, TEGRA210_AMX_OUT_BYTE_EN0, amx->byte_mask[0]);
	regmap_write(amx->regmap, TEGRA210_AMX_OUT_BYTE_EN1, amx->byte_mask[1]);
}

static int tegra210_amx_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra210_amx *amx = snd_soc_dai_get_drvdata(dai);
	unsigned int val;
	int err;

	/* Ensure if AMX is disabled */
	err = regmap_read_poll_timeout(amx->regmap, TEGRA210_AMX_STATUS, val,
				       !(val & 0x1), 10, 10000);
	if (err < 0) {
		dev_err(dai->dev, "failed to stop AMX, err = %d\n", err);
		return err;
	}

	/*
	 * Soft Reset: Below performs module soft reset which clears
	 * all FSM logic, flushes flow control of FIFO and resets the
	 * state register. It also brings module back to disabled
	 * state (without flushing the data in the pipe).
	 */
	regmap_update_bits(amx->regmap, TEGRA210_AMX_SOFT_RESET,
			   TEGRA210_AMX_SOFT_RESET_SOFT_RESET_MASK,
			   TEGRA210_AMX_SOFT_RESET_SOFT_EN);

	err = regmap_read_poll_timeout(amx->regmap, TEGRA210_AMX_SOFT_RESET,
				       val, !(val & 0x1), 10, 10000);
	if (err < 0) {
		dev_err(dai->dev, "failed to reset AMX, err = %d\n", err);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra210_amx_runtime_suspend(struct device *dev)
{
	struct tegra210_amx *amx = dev_get_drvdata(dev);

	regcache_cache_only(amx->regmap, true);
	regcache_mark_dirty(amx->regmap);

	return 0;
}

static int __maybe_unused tegra210_amx_runtime_resume(struct device *dev)
{
	struct tegra210_amx *amx = dev_get_drvdata(dev);

	regcache_cache_only(amx->regmap, false);
	regcache_sync(amx->regmap);

	regmap_update_bits(amx->regmap,
		TEGRA210_AMX_CTRL,
		TEGRA210_AMX_CTRL_RX_DEP_MASK,
		TEGRA210_AMX_WAIT_ON_ANY << TEGRA210_AMX_CTRL_RX_DEP_SHIFT);

	tegra210_amx_write_map_ram(amx);

	return 0;
}

static int tegra210_amx_set_audio_cif(struct snd_soc_dai *dai,
				      struct snd_pcm_hw_params *params,
				      unsigned int reg)
{
	struct tegra210_amx *amx = snd_soc_dai_get_drvdata(dai);
	int channels, audio_bits;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	switch (params_format(params)) {
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

	tegra_set_cif(amx->regmap, reg, &cif_conf);

	return 0;
}

static int tegra210_amx_in_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct tegra210_amx *amx = snd_soc_dai_get_drvdata(dai);

	if (amx->soc_data->auto_disable) {
		regmap_write(amx->regmap,
			     AMX_CH_REG(dai->id, TEGRA194_AMX_RX1_FRAME_PERIOD),
			     TEGRA194_MAX_FRAME_IDLE_COUNT);
		regmap_write(amx->regmap, TEGRA210_AMX_CYA, 1);
	}

	return tegra210_amx_set_audio_cif(dai, params,
			AMX_CH_REG(dai->id, TEGRA210_AMX_RX1_CIF_CTRL));
}

static int tegra210_amx_out_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	return tegra210_amx_set_audio_cif(dai, params,
					  TEGRA210_AMX_TX_CIF_CTRL);
}

static int tegra210_amx_get_byte_map(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tegra210_amx *amx = snd_soc_component_get_drvdata(cmpnt);
	unsigned char *bytes_map = (unsigned char *)&amx->map;
	int reg = mc->reg;
	int enabled;

	if (reg > 31)
		enabled = amx->byte_mask[1] & (1 << (reg - 32));
	else
		enabled = amx->byte_mask[0] & (1 << reg);

	if (enabled)
		ucontrol->value.integer.value[0] = bytes_map[reg];
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int tegra210_amx_put_byte_map(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_amx *amx = snd_soc_component_get_drvdata(cmpnt);
	unsigned char *bytes_map = (unsigned char *)&amx->map;
	int reg = mc->reg;
	int value = ucontrol->value.integer.value[0];

	if (value == bytes_map[reg])
		return 0;

	if (value >= 0 && value <= 255) {
		/* Update byte map and enable slot */
		bytes_map[reg] = value;
		if (reg > 31)
			amx->byte_mask[1] |= (1 << (reg - 32));
		else
			amx->byte_mask[0] |= (1 << reg);
	} else {
		/* Reset byte map and disable slot */
		bytes_map[reg] = 0;
		if (reg > 31)
			amx->byte_mask[1] &= ~(1 << (reg - 32));
		else
			amx->byte_mask[0] &= ~(1 << reg);
	}

	return 1;
}

static const struct snd_soc_dai_ops tegra210_amx_out_dai_ops = {
	.hw_params	= tegra210_amx_out_hw_params,
	.startup	= tegra210_amx_startup,
};

static const struct snd_soc_dai_ops tegra210_amx_in_dai_ops = {
	.hw_params	= tegra210_amx_in_hw_params,
};

#define IN_DAI(id)						\
	{							\
		.name = "AMX-RX-CIF" #id,			\
		.playback = {					\
			.stream_name = "RX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "RX" #id "-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_amx_in_dai_ops,		\
	}

#define OUT_DAI							\
	{							\
		.name = "AMX-TX-CIF",				\
		.playback = {					\
			.stream_name = "TX-CIF-Playback",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "TX-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				   SNDRV_PCM_FMTBIT_S16_LE |	\
				   SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra210_amx_out_dai_ops,		\
	}

static struct snd_soc_dai_driver tegra210_amx_dais[] = {
	IN_DAI(1),
	IN_DAI(2),
	IN_DAI(3),
	IN_DAI(4),
	OUT_DAI,
};

static const struct snd_soc_dapm_widget tegra210_amx_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX1", NULL, 0, TEGRA210_AMX_CTRL, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX2", NULL, 0, TEGRA210_AMX_CTRL, 1, 0),
	SND_SOC_DAPM_AIF_IN("RX3", NULL, 0, TEGRA210_AMX_CTRL, 2, 0),
	SND_SOC_DAPM_AIF_IN("RX4", NULL, 0, TEGRA210_AMX_CTRL, 3, 0),
	SND_SOC_DAPM_AIF_OUT("TX", NULL, 0, TEGRA210_AMX_ENABLE,
			     TEGRA210_AMX_ENABLE_SHIFT, 0),
};

#define STREAM_ROUTES(id, sname)					  \
	{ "RX" #id " XBAR-" sname,	NULL,	"RX" #id " XBAR-TX" },	  \
	{ "RX" #id "-CIF-" sname,	NULL,	"RX" #id " XBAR-" sname },\
	{ "RX" #id,			NULL,	"RX" #id "-CIF-" sname }, \
	{ "TX",				NULL,	"RX" #id },		  \
	{ "TX-CIF-" sname,		NULL,	"TX" },			  \
	{ "XBAR-" sname,		NULL,	"TX-CIF-" sname },	  \
	{ "XBAR-RX",			NULL,	"XBAR-" sname }

#define AMX_ROUTES(id)			\
	STREAM_ROUTES(id, "Playback"),	\
	STREAM_ROUTES(id, "Capture")

static const struct snd_soc_dapm_route tegra210_amx_routes[] = {
	AMX_ROUTES(1),
	AMX_ROUTES(2),
	AMX_ROUTES(3),
	AMX_ROUTES(4),
};

#define TEGRA210_AMX_BYTE_MAP_CTRL(reg)					\
	SOC_SINGLE_EXT("Byte Map " #reg, reg, 0, 256, 0,		\
		       tegra210_amx_get_byte_map,			\
		       tegra210_amx_put_byte_map)

static struct snd_kcontrol_new tegra210_amx_controls[] = {
	TEGRA210_AMX_BYTE_MAP_CTRL(0),
	TEGRA210_AMX_BYTE_MAP_CTRL(1),
	TEGRA210_AMX_BYTE_MAP_CTRL(2),
	TEGRA210_AMX_BYTE_MAP_CTRL(3),
	TEGRA210_AMX_BYTE_MAP_CTRL(4),
	TEGRA210_AMX_BYTE_MAP_CTRL(5),
	TEGRA210_AMX_BYTE_MAP_CTRL(6),
	TEGRA210_AMX_BYTE_MAP_CTRL(7),
	TEGRA210_AMX_BYTE_MAP_CTRL(8),
	TEGRA210_AMX_BYTE_MAP_CTRL(9),
	TEGRA210_AMX_BYTE_MAP_CTRL(10),
	TEGRA210_AMX_BYTE_MAP_CTRL(11),
	TEGRA210_AMX_BYTE_MAP_CTRL(12),
	TEGRA210_AMX_BYTE_MAP_CTRL(13),
	TEGRA210_AMX_BYTE_MAP_CTRL(14),
	TEGRA210_AMX_BYTE_MAP_CTRL(15),
	TEGRA210_AMX_BYTE_MAP_CTRL(16),
	TEGRA210_AMX_BYTE_MAP_CTRL(17),
	TEGRA210_AMX_BYTE_MAP_CTRL(18),
	TEGRA210_AMX_BYTE_MAP_CTRL(19),
	TEGRA210_AMX_BYTE_MAP_CTRL(20),
	TEGRA210_AMX_BYTE_MAP_CTRL(21),
	TEGRA210_AMX_BYTE_MAP_CTRL(22),
	TEGRA210_AMX_BYTE_MAP_CTRL(23),
	TEGRA210_AMX_BYTE_MAP_CTRL(24),
	TEGRA210_AMX_BYTE_MAP_CTRL(25),
	TEGRA210_AMX_BYTE_MAP_CTRL(26),
	TEGRA210_AMX_BYTE_MAP_CTRL(27),
	TEGRA210_AMX_BYTE_MAP_CTRL(28),
	TEGRA210_AMX_BYTE_MAP_CTRL(29),
	TEGRA210_AMX_BYTE_MAP_CTRL(30),
	TEGRA210_AMX_BYTE_MAP_CTRL(31),
	TEGRA210_AMX_BYTE_MAP_CTRL(32),
	TEGRA210_AMX_BYTE_MAP_CTRL(33),
	TEGRA210_AMX_BYTE_MAP_CTRL(34),
	TEGRA210_AMX_BYTE_MAP_CTRL(35),
	TEGRA210_AMX_BYTE_MAP_CTRL(36),
	TEGRA210_AMX_BYTE_MAP_CTRL(37),
	TEGRA210_AMX_BYTE_MAP_CTRL(38),
	TEGRA210_AMX_BYTE_MAP_CTRL(39),
	TEGRA210_AMX_BYTE_MAP_CTRL(40),
	TEGRA210_AMX_BYTE_MAP_CTRL(41),
	TEGRA210_AMX_BYTE_MAP_CTRL(42),
	TEGRA210_AMX_BYTE_MAP_CTRL(43),
	TEGRA210_AMX_BYTE_MAP_CTRL(44),
	TEGRA210_AMX_BYTE_MAP_CTRL(45),
	TEGRA210_AMX_BYTE_MAP_CTRL(46),
	TEGRA210_AMX_BYTE_MAP_CTRL(47),
	TEGRA210_AMX_BYTE_MAP_CTRL(48),
	TEGRA210_AMX_BYTE_MAP_CTRL(49),
	TEGRA210_AMX_BYTE_MAP_CTRL(50),
	TEGRA210_AMX_BYTE_MAP_CTRL(51),
	TEGRA210_AMX_BYTE_MAP_CTRL(52),
	TEGRA210_AMX_BYTE_MAP_CTRL(53),
	TEGRA210_AMX_BYTE_MAP_CTRL(54),
	TEGRA210_AMX_BYTE_MAP_CTRL(55),
	TEGRA210_AMX_BYTE_MAP_CTRL(56),
	TEGRA210_AMX_BYTE_MAP_CTRL(57),
	TEGRA210_AMX_BYTE_MAP_CTRL(58),
	TEGRA210_AMX_BYTE_MAP_CTRL(59),
	TEGRA210_AMX_BYTE_MAP_CTRL(60),
	TEGRA210_AMX_BYTE_MAP_CTRL(61),
	TEGRA210_AMX_BYTE_MAP_CTRL(62),
	TEGRA210_AMX_BYTE_MAP_CTRL(63),
};

static const struct snd_soc_component_driver tegra210_amx_cmpnt = {
	.dapm_widgets		= tegra210_amx_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_amx_widgets),
	.dapm_routes		= tegra210_amx_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_amx_routes),
	.controls		= tegra210_amx_controls,
	.num_controls		= ARRAY_SIZE(tegra210_amx_controls),
};

static bool tegra210_amx_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_AMX_RX_INT_MASK ... TEGRA210_AMX_RX4_CIF_CTRL:
	case TEGRA210_AMX_TX_INT_MASK ... TEGRA210_AMX_CG:
	case TEGRA210_AMX_CTRL ... TEGRA210_AMX_CYA:
	case TEGRA210_AMX_CFG_RAM_CTRL ... TEGRA210_AMX_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra194_amx_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA194_AMX_RX1_FRAME_PERIOD ... TEGRA194_AMX_RX4_FRAME_PERIOD:
		return true;
	default:
		return tegra210_amx_wr_reg(dev, reg);
	}
}

static bool tegra210_amx_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_AMX_RX_STATUS ... TEGRA210_AMX_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra194_amx_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA194_AMX_RX1_FRAME_PERIOD ... TEGRA194_AMX_RX4_FRAME_PERIOD:
		return true;
	default:
		return tegra210_amx_rd_reg(dev, reg);
	}
}

static bool tegra210_amx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_AMX_RX_STATUS:
	case TEGRA210_AMX_RX_INT_STATUS:
	case TEGRA210_AMX_RX_INT_SET:
	case TEGRA210_AMX_TX_STATUS:
	case TEGRA210_AMX_TX_INT_STATUS:
	case TEGRA210_AMX_TX_INT_SET:
	case TEGRA210_AMX_SOFT_RESET:
	case TEGRA210_AMX_STATUS:
	case TEGRA210_AMX_INT_STATUS:
	case TEGRA210_AMX_CFG_RAM_CTRL:
	case TEGRA210_AMX_CFG_RAM_DATA:
		return true;
	default:
		break;
	}

	return false;
}

static const struct regmap_config tegra210_amx_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_AMX_CFG_RAM_DATA,
	.writeable_reg		= tegra210_amx_wr_reg,
	.readable_reg		= tegra210_amx_rd_reg,
	.volatile_reg		= tegra210_amx_volatile_reg,
	.reg_defaults		= tegra210_amx_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_amx_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct regmap_config tegra194_amx_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA194_AMX_RX4_LAST_FRAME_PERIOD,
	.writeable_reg		= tegra194_amx_wr_reg,
	.readable_reg		= tegra194_amx_rd_reg,
	.volatile_reg		= tegra210_amx_volatile_reg,
	.reg_defaults		= tegra210_amx_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_amx_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct tegra210_amx_soc_data soc_data_tegra210 = {
	.regmap_conf	= &tegra210_amx_regmap_config,
};

static const struct tegra210_amx_soc_data soc_data_tegra194 = {
	.regmap_conf	= &tegra194_amx_regmap_config,
	.auto_disable	= true,
};

static const struct of_device_id tegra210_amx_of_match[] = {
	{ .compatible = "nvidia,tegra210-amx", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra194-amx", .data = &soc_data_tegra194 },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_amx_of_match);

static int tegra210_amx_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_amx *amx;
	void __iomem *regs;
	int err;
	const struct of_device_id *match;
	struct tegra210_amx_soc_data *soc_data;

	match = of_match_device(tegra210_amx_of_match, dev);

	soc_data = (struct tegra210_amx_soc_data *)match->data;

	amx = devm_kzalloc(dev, sizeof(*amx), GFP_KERNEL);
	if (!amx)
		return -ENOMEM;

	amx->soc_data = soc_data;

	dev_set_drvdata(dev, amx);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	amx->regmap = devm_regmap_init_mmio(dev, regs,
					    soc_data->regmap_conf);
	if (IS_ERR(amx->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(amx->regmap);
	}

	regcache_cache_only(amx->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_amx_cmpnt,
					      tegra210_amx_dais,
					      ARRAY_SIZE(tegra210_amx_dais));
	if (err) {
		dev_err(dev, "can't register AMX component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static int tegra210_amx_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra210_amx_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_amx_runtime_suspend,
			   tegra210_amx_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver tegra210_amx_driver = {
	.driver = {
		.name = "tegra210-amx",
		.of_match_table = tegra210_amx_of_match,
		.pm = &tegra210_amx_pm_ops,
	},
	.probe = tegra210_amx_platform_probe,
	.remove = tegra210_amx_platform_remove,
};
module_platform_driver(tegra210_amx_driver);

MODULE_AUTHOR("Songhee Baek <sbaek@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 AMX ASoC driver");
MODULE_LICENSE("GPL v2");
