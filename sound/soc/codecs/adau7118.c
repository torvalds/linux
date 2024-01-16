// SPDX-License-Identifier: GPL-2.0
//
// Analog Devices ADAU7118 8 channel PDM-to-I2S/TDM Converter driver
//
// Copyright 2019 Analog Devices Inc.

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "adau7118.h"

#define ADAU7118_DEC_RATIO_MASK		GENMASK(1, 0)
#define ADAU7118_DEC_RATIO(x)		FIELD_PREP(ADAU7118_DEC_RATIO_MASK, x)
#define ADAU7118_CLK_MAP_MASK		GENMASK(7, 4)
#define ADAU7118_SLOT_WIDTH_MASK	GENMASK(5, 4)
#define ADAU7118_SLOT_WIDTH(x)		FIELD_PREP(ADAU7118_SLOT_WIDTH_MASK, x)
#define ADAU7118_TRISTATE_MASK		BIT(6)
#define ADAU7118_TRISTATE(x)		FIELD_PREP(ADAU7118_TRISTATE_MASK, x)
#define ADAU7118_DATA_FMT_MASK		GENMASK(3, 1)
#define ADAU7118_DATA_FMT(x)		FIELD_PREP(ADAU7118_DATA_FMT_MASK, x)
#define ADAU7118_SAI_MODE_MASK		BIT(0)
#define ADAU7118_SAI_MODE(x)		FIELD_PREP(ADAU7118_SAI_MODE_MASK, x)
#define ADAU7118_LRCLK_BCLK_POL_MASK	GENMASK(1, 0)
#define ADAU7118_LRCLK_BCLK_POL(x) \
				FIELD_PREP(ADAU7118_LRCLK_BCLK_POL_MASK, x)
#define ADAU7118_SPT_SLOT_MASK		GENMASK(7, 4)
#define ADAU7118_SPT_SLOT(x)		FIELD_PREP(ADAU7118_SPT_SLOT_MASK, x)
#define ADAU7118_FULL_SOFT_R_MASK	BIT(1)
#define ADAU7118_FULL_SOFT_R(x)		FIELD_PREP(ADAU7118_FULL_SOFT_R_MASK, x)

struct adau7118_data {
	struct regmap *map;
	struct device *dev;
	struct regulator *iovdd;
	struct regulator *dvdd;
	u32 slot_width;
	u32 slots;
	bool hw_mode;
	bool right_j;
};

/* Input Enable */
static const struct snd_kcontrol_new adau7118_dapm_pdm_control[4] = {
	SOC_DAPM_SINGLE("Capture Switch", ADAU7118_REG_ENABLES, 0, 1, 0),
	SOC_DAPM_SINGLE("Capture Switch", ADAU7118_REG_ENABLES, 1, 1, 0),
	SOC_DAPM_SINGLE("Capture Switch", ADAU7118_REG_ENABLES, 2, 1, 0),
	SOC_DAPM_SINGLE("Capture Switch", ADAU7118_REG_ENABLES, 3, 1, 0),
};

static const struct snd_soc_dapm_widget adau7118_widgets_sw[] = {
	/* Input Enable Switches */
	SND_SOC_DAPM_SWITCH("PDM0", SND_SOC_NOPM, 0, 0,
			    &adau7118_dapm_pdm_control[0]),
	SND_SOC_DAPM_SWITCH("PDM1", SND_SOC_NOPM, 0, 0,
			    &adau7118_dapm_pdm_control[1]),
	SND_SOC_DAPM_SWITCH("PDM2", SND_SOC_NOPM, 0, 0,
			    &adau7118_dapm_pdm_control[2]),
	SND_SOC_DAPM_SWITCH("PDM3", SND_SOC_NOPM, 0, 0,
			    &adau7118_dapm_pdm_control[3]),

	/* PDM Clocks */
	SND_SOC_DAPM_SUPPLY("PDM_CLK0", ADAU7118_REG_ENABLES, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PDM_CLK1", ADAU7118_REG_ENABLES, 5, 0, NULL, 0),

	/* Output channels */
	SND_SOC_DAPM_AIF_OUT("AIF1TX1", "Capture", 0, ADAU7118_REG_SPT_CX(0),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX2", "Capture", 0, ADAU7118_REG_SPT_CX(1),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX3", "Capture", 0, ADAU7118_REG_SPT_CX(2),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX4", "Capture", 0, ADAU7118_REG_SPT_CX(3),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX5", "Capture", 0, ADAU7118_REG_SPT_CX(4),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX6", "Capture", 0, ADAU7118_REG_SPT_CX(5),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX7", "Capture", 0, ADAU7118_REG_SPT_CX(6),
			     0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX8", "Capture", 0, ADAU7118_REG_SPT_CX(7),
			     0, 0),
};

static const struct snd_soc_dapm_route adau7118_routes_sw[] = {
	{ "PDM0", "Capture Switch", "PDM_DAT0" },
	{ "PDM1", "Capture Switch", "PDM_DAT1" },
	{ "PDM2", "Capture Switch", "PDM_DAT2" },
	{ "PDM3", "Capture Switch", "PDM_DAT3" },
	{ "AIF1TX1", NULL, "PDM0" },
	{ "AIF1TX2", NULL, "PDM0" },
	{ "AIF1TX3", NULL, "PDM1" },
	{ "AIF1TX4", NULL, "PDM1" },
	{ "AIF1TX5", NULL, "PDM2" },
	{ "AIF1TX6", NULL, "PDM2" },
	{ "AIF1TX7", NULL, "PDM3" },
	{ "AIF1TX8", NULL, "PDM3" },
	{ "Capture", NULL, "PDM_CLK0" },
	{ "Capture", NULL, "PDM_CLK1" },
};

static const struct snd_soc_dapm_widget adau7118_widgets_hw[] = {
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route adau7118_routes_hw[] = {
	{ "AIF1TX", NULL, "PDM_DAT0" },
	{ "AIF1TX", NULL, "PDM_DAT1" },
	{ "AIF1TX", NULL, "PDM_DAT2" },
	{ "AIF1TX", NULL, "PDM_DAT3" },
};

static const struct snd_soc_dapm_widget adau7118_widgets[] = {
	SND_SOC_DAPM_INPUT("PDM_DAT0"),
	SND_SOC_DAPM_INPUT("PDM_DAT1"),
	SND_SOC_DAPM_INPUT("PDM_DAT2"),
	SND_SOC_DAPM_INPUT("PDM_DAT3"),
};

static int adau7118_set_channel_map(struct snd_soc_dai *dai,
				    unsigned int tx_num, unsigned int *tx_slot,
				    unsigned int rx_num, unsigned int *rx_slot)
{
	struct adau7118_data *st =
		snd_soc_component_get_drvdata(dai->component);
	int chan, ret;

	dev_dbg(st->dev, "Set channel map, %d", tx_num);

	for (chan = 0; chan < tx_num; chan++) {
		ret = snd_soc_component_update_bits(dai->component,
					ADAU7118_REG_SPT_CX(chan),
					ADAU7118_SPT_SLOT_MASK,
					ADAU7118_SPT_SLOT(tx_slot[chan]));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int adau7118_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct adau7118_data *st =
		snd_soc_component_get_drvdata(dai->component);
	int ret = 0;
	u32 regval;

	dev_dbg(st->dev, "Set format, fmt:%d\n", fmt);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ret = snd_soc_component_update_bits(dai->component,
						    ADAU7118_REG_SPT_CTRL1,
						    ADAU7118_DATA_FMT_MASK,
						    ADAU7118_DATA_FMT(0));
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ret = snd_soc_component_update_bits(dai->component,
						    ADAU7118_REG_SPT_CTRL1,
						    ADAU7118_DATA_FMT_MASK,
						    ADAU7118_DATA_FMT(1));
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		st->right_j = true;
		break;
	default:
		dev_err(st->dev, "Invalid format %d",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		regval = ADAU7118_LRCLK_BCLK_POL(0);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		regval = ADAU7118_LRCLK_BCLK_POL(2);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		regval = ADAU7118_LRCLK_BCLK_POL(1);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		regval = ADAU7118_LRCLK_BCLK_POL(3);
		break;
	default:
		dev_err(st->dev, "Invalid Inv mask %d",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(dai->component,
					    ADAU7118_REG_SPT_CTRL2,
					    ADAU7118_LRCLK_BCLK_POL_MASK,
					    regval);
	if (ret < 0)
		return ret;

	return 0;
}

static int adau7118_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct adau7118_data *st =
		snd_soc_component_get_drvdata(dai->component);
	int ret;

	dev_dbg(st->dev, "Set tristate, %d\n", tristate);

	ret = snd_soc_component_update_bits(dai->component,
					    ADAU7118_REG_SPT_CTRL1,
					    ADAU7118_TRISTATE_MASK,
					    ADAU7118_TRISTATE(tristate));
	if (ret < 0)
		return ret;

	return 0;
}

static int adau7118_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				 unsigned int rx_mask, int slots,
				 int slot_width)
{
	struct adau7118_data *st =
		snd_soc_component_get_drvdata(dai->component);
	int ret = 0;
	u32 regval;

	dev_dbg(st->dev, "Set tdm, slots:%d width:%d\n", slots, slot_width);

	switch (slot_width) {
	case 32:
		regval = ADAU7118_SLOT_WIDTH(0);
		break;
	case 24:
		regval = ADAU7118_SLOT_WIDTH(2);
		break;
	case 16:
		regval = ADAU7118_SLOT_WIDTH(1);
		break;
	default:
		dev_err(st->dev, "Invalid slot width:%d\n", slot_width);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(dai->component,
					    ADAU7118_REG_SPT_CTRL1,
					    ADAU7118_SLOT_WIDTH_MASK, regval);
	if (ret < 0)
		return ret;

	st->slot_width = slot_width;
	st->slots = slots;

	return 0;
}

static int adau7118_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct adau7118_data *st =
		snd_soc_component_get_drvdata(dai->component);
	u32 data_width = params_width(params), slots_width;
	int ret;
	u32 regval;

	if (!st->slots) {
		/* set stereo mode */
		ret = snd_soc_component_update_bits(dai->component,
						    ADAU7118_REG_SPT_CTRL1,
						    ADAU7118_SAI_MODE_MASK,
						    ADAU7118_SAI_MODE(0));
		if (ret < 0)
			return ret;

		slots_width = 32;
	} else {
		slots_width = st->slot_width;
	}

	if (data_width > slots_width) {
		dev_err(st->dev, "Invalid data_width:%d, slots_width:%d",
			data_width, slots_width);
		return -EINVAL;
	}

	if (st->right_j) {
		switch (slots_width - data_width) {
		case 8:
			/* delay bclck by 8 */
			regval = ADAU7118_DATA_FMT(2);
			break;
		case 12:
			/* delay bclck by 12 */
			regval = ADAU7118_DATA_FMT(3);
			break;
		case 16:
			/* delay bclck by 16 */
			regval = ADAU7118_DATA_FMT(4);
			break;
		default:
			dev_err(st->dev,
				"Cannot set right_j setting, slot_w:%d, data_w:%d\n",
					slots_width, data_width);
			return -EINVAL;
		}

		ret = snd_soc_component_update_bits(dai->component,
						    ADAU7118_REG_SPT_CTRL1,
						    ADAU7118_DATA_FMT_MASK,
						    regval);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int adau7118_set_bias_level(struct snd_soc_component *component,
				   enum snd_soc_bias_level level)
{
	struct adau7118_data *st = snd_soc_component_get_drvdata(component);
	int ret = 0;

	dev_dbg(st->dev, "Set bias level %d\n", level);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) ==
							SND_SOC_BIAS_OFF) {
			/* power on */
			ret = regulator_enable(st->iovdd);
			if (ret)
				return ret;

			/* there's no timing constraints before enabling dvdd */
			ret = regulator_enable(st->dvdd);
			if (ret) {
				regulator_disable(st->iovdd);
				return ret;
			}

			if (st->hw_mode)
				return 0;

			regcache_cache_only(st->map, false);
			/* sync cache */
			ret = snd_soc_component_cache_sync(component);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* power off */
		ret = regulator_disable(st->dvdd);
		if (ret)
			return ret;

		ret = regulator_disable(st->iovdd);
		if (ret)
			return ret;

		if (st->hw_mode)
			return 0;

		/* cache only */
		regcache_mark_dirty(st->map);
		regcache_cache_only(st->map, true);

		break;
	}

	return ret;
}

static int adau7118_component_probe(struct snd_soc_component *component)
{
	struct adau7118_data *st = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
					snd_soc_component_get_dapm(component);
	int ret = 0;

	if (st->hw_mode) {
		ret = snd_soc_dapm_new_controls(dapm, adau7118_widgets_hw,
					ARRAY_SIZE(adau7118_widgets_hw));
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm, adau7118_routes_hw,
					      ARRAY_SIZE(adau7118_routes_hw));
	} else {
		snd_soc_component_init_regmap(component, st->map);
		ret = snd_soc_dapm_new_controls(dapm, adau7118_widgets_sw,
					ARRAY_SIZE(adau7118_widgets_sw));
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm, adau7118_routes_sw,
					      ARRAY_SIZE(adau7118_routes_sw));
	}

	return ret;
}

static const struct snd_soc_dai_ops adau7118_ops = {
	.hw_params = adau7118_hw_params,
	.set_channel_map = adau7118_set_channel_map,
	.set_fmt = adau7118_set_fmt,
	.set_tdm_slot = adau7118_set_tdm_slot,
	.set_tristate = adau7118_set_tristate,
};

static struct snd_soc_dai_driver adau7118_dai = {
	.name = "adau7118-hifi-capture",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S20_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S24_3LE,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 4000,
		.rate_max = 192000,
		.sig_bits = 24,
	},
};

static const struct snd_soc_component_driver adau7118_component_driver = {
	.probe			= adau7118_component_probe,
	.set_bias_level		= adau7118_set_bias_level,
	.dapm_widgets		= adau7118_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adau7118_widgets),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int adau7118_regulator_setup(struct adau7118_data *st)
{
	st->iovdd = devm_regulator_get(st->dev, "iovdd");
	if (IS_ERR(st->iovdd)) {
		dev_err(st->dev, "Could not get iovdd: %ld\n",
			PTR_ERR(st->iovdd));
		return PTR_ERR(st->iovdd);
	}

	st->dvdd = devm_regulator_get(st->dev, "dvdd");
	if (IS_ERR(st->dvdd)) {
		dev_err(st->dev, "Could not get dvdd: %ld\n",
			PTR_ERR(st->dvdd));
		return PTR_ERR(st->dvdd);
	}
	/* just assume the device is in reset */
	if (!st->hw_mode) {
		regcache_mark_dirty(st->map);
		regcache_cache_only(st->map, true);
	}

	return 0;
}

static int adau7118_parset_dt(const struct adau7118_data *st)
{
	int ret;
	u32 dec_ratio = 0;
	/* 4 inputs */
	u32 clk_map[4], regval;

	if (st->hw_mode)
		return 0;

	ret = device_property_read_u32(st->dev, "adi,decimation-ratio",
				       &dec_ratio);
	if (!ret) {
		switch (dec_ratio) {
		case 64:
			regval = ADAU7118_DEC_RATIO(0);
			break;
		case 32:
			regval = ADAU7118_DEC_RATIO(1);
			break;
		case 16:
			regval = ADAU7118_DEC_RATIO(2);
			break;
		default:
			dev_err(st->dev, "Invalid dec ratio: %u", dec_ratio);
			return -EINVAL;
		}

		ret = regmap_update_bits(st->map,
					 ADAU7118_REG_DEC_RATIO_CLK_MAP,
					 ADAU7118_DEC_RATIO_MASK, regval);
		if (ret)
			return ret;
	}

	ret = device_property_read_u32_array(st->dev, "adi,pdm-clk-map",
					     clk_map, ARRAY_SIZE(clk_map));
	if (!ret) {
		int pdm;
		u32 _clk_map = 0;

		for (pdm = 0; pdm < ARRAY_SIZE(clk_map); pdm++)
			_clk_map |= (clk_map[pdm] << (pdm + 4));

		ret = regmap_update_bits(st->map,
					 ADAU7118_REG_DEC_RATIO_CLK_MAP,
					 ADAU7118_CLK_MAP_MASK, _clk_map);
		if (ret)
			return ret;
	}

	return 0;
}

int adau7118_probe(struct device *dev, struct regmap *map, bool hw_mode)
{
	struct adau7118_data *st;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->dev = dev;
	st->hw_mode = hw_mode;
	dev_set_drvdata(dev, st);

	if (!hw_mode) {
		st->map = map;
		adau7118_dai.ops = &adau7118_ops;
		/*
		 * Perform a full soft reset. This will set all register's
		 * with their reset values.
		 */
		ret = regmap_update_bits(map, ADAU7118_REG_RESET,
					 ADAU7118_FULL_SOFT_R_MASK,
					 ADAU7118_FULL_SOFT_R(1));
		if (ret)
			return ret;
	}

	ret = adau7118_parset_dt(st);
	if (ret)
		return ret;

	ret = adau7118_regulator_setup(st);
	if (ret)
		return ret;

	return devm_snd_soc_register_component(dev,
					       &adau7118_component_driver,
					       &adau7118_dai, 1);
}
EXPORT_SYMBOL_GPL(adau7118_probe);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("ADAU7118 8 channel PDM-to-I2S/TDM Converter driver");
MODULE_LICENSE("GPL");
