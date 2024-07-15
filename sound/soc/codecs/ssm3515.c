// SPDX-License-Identifier: GPL-2.0-only OR MIT
//
// Analog Devices' SSM3515 audio amp driver
//
// Copyright (C) The Asahi Linux Contributors

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>


#define SSM3515_PWR		0x00
#define SSM3515_PWR_APWDN_EN	BIT(7)
#define SSM3515_PWR_BSNS_PWDN	BIT(6)
#define SSM3515_PWR_S_RST	BIT(1)
#define SSM3515_PWR_SPWDN	BIT(0)

#define SSM3515_GEC		0x01
#define SSM3515_GEC_EDGE	BIT(4)
#define SSM3515_GEC_EDGE_SHIFT	4
#define SSM3515_GEC_ANA_GAIN	GENMASK(1, 0)

#define SSM3515_DAC		0x02
#define SSM3515_DAC_HV		BIT(7)
#define SSM3515_DAC_MUTE	BIT(6)
#define SSM3515_DAC_HPF		BIT(5)
#define SSM3515_DAC_LPM		BIT(4)
#define SSM3515_DAC_FS		GENMASK(2, 0)

#define SSM3515_DAC_VOL		0x03

#define SSM3515_SAI1		0x04
#define SSM3515_SAI1_DAC_POL	BIT(7)
#define SSM3515_SAI1_BCLK_POL	BIT(6)
#define SSM3515_SAI1_TDM_BCLKS	GENMASK(5, 3)
#define SSM3515_SAI1_FSYNC_MODE	BIT(2)
#define SSM3515_SAI1_SDATA_FMT	BIT(1)
#define SSM3515_SAI1_SAI_MODE	BIT(0)

#define SSM3515_SAI2		0x05
#define SSM3515_SAI2_DATA_WIDTH	BIT(7)
#define SSM3515_SAI2_AUTO_SLOT	BIT(4)
#define SSM3515_SAI2_TDM_SLOT	GENMASK(3, 0)

#define SSM3515_VBAT_OUT	0x06

#define SSM3515_STATUS		0x0a
#define SSM3515_STATUS_UVLO_REG	BIT(6)
#define SSM3515_STATUS_LIM_EG	BIT(5)
#define SSM3515_STATUS_CLIP	BIT(4)
#define SSM3515_STATUS_AMP_OC	BIT(3)
#define SSM3515_STATUS_OTF	BIT(2)
#define SSM3515_STATUS_OTW	BIT(1)
#define SSM3515_STATUS_BAT_WARN	BIT(0)

static bool ssm3515_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SSM3515_STATUS:
	case SSM3515_VBAT_OUT:
		return true;

	default:
		return false;
	}
}

static const struct reg_default ssm3515_reg_defaults[] = {
	{ SSM3515_PWR, 0x81 },
	{ SSM3515_GEC, 0x01 },
	{ SSM3515_DAC, 0x32 },
	{ SSM3515_DAC_VOL, 0x40 },
	{ SSM3515_SAI1, 0x11 },
	{ SSM3515_SAI2, 0x00 },
};

static const struct regmap_config ssm3515_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = ssm3515_volatile_reg,
	.max_register = 0xb,
	.reg_defaults = ssm3515_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ssm3515_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

struct ssm3515_data {
	struct device *dev;
	struct regmap *regmap;
};

// The specced range is -71.25...24.00 dB with step size of 0.375 dB,
// and a mute item below that. This is represented by -71.62...24.00 dB
// with the mute item mapped onto the low end.
static DECLARE_TLV_DB_MINMAX_MUTE(ssm3515_dac_volume, -7162, 2400);

static const char * const ssm3515_ana_gain_text[] = {
	"8.4 V Span", "12.6 V Span", "14 V Span", "15 V Span",
};

static SOC_ENUM_SINGLE_DECL(ssm3515_ana_gain_enum, SSM3515_GEC,
			    __bf_shf(SSM3515_GEC_ANA_GAIN),
			    ssm3515_ana_gain_text);

static const struct snd_kcontrol_new ssm3515_snd_controls[] = {
	SOC_SINGLE_TLV("DAC Playback Volume", SSM3515_DAC_VOL,
		       0, 255, 1, ssm3515_dac_volume),
	SOC_SINGLE("Low EMI Mode Switch", SSM3515_GEC,
		   __bf_shf(SSM3515_GEC_EDGE), 1, 0),
	SOC_SINGLE("Soft Volume Ramping Switch", SSM3515_DAC,
		   __bf_shf(SSM3515_DAC_HV), 1, 1),
	SOC_SINGLE("HPF Switch", SSM3515_DAC,
		   __bf_shf(SSM3515_DAC_HPF), 1, 0),
	SOC_SINGLE("DAC Invert Switch", SSM3515_SAI1,
		   __bf_shf(SSM3515_SAI1_DAC_POL), 1, 0),
	SOC_ENUM("DAC Analog Gain Select", ssm3515_ana_gain_enum),
};

static void ssm3515_read_faults(struct snd_soc_component *component)
{
	int ret;

	ret = snd_soc_component_read(component, SSM3515_STATUS);
	if (ret <= 0) {
		/*
		 * If the read was erroneous, ASoC core has printed a message,
		 * and that's all that's appropriate in handling the error here.
		 */
		return;
	}

	dev_err(component->dev, "device reports:%s%s%s%s%s%s%s\n",
		FIELD_GET(SSM3515_STATUS_UVLO_REG, ret) ? " voltage regulator fault" : "",
		FIELD_GET(SSM3515_STATUS_LIM_EG, ret)   ? " limiter engaged" : "",
		FIELD_GET(SSM3515_STATUS_CLIP, ret)     ? " clipping detected" : "",
		FIELD_GET(SSM3515_STATUS_AMP_OC, ret)   ? " amp over-current fault" : "",
		FIELD_GET(SSM3515_STATUS_OTF, ret)      ? " overtemperature fault" : "",
		FIELD_GET(SSM3515_STATUS_OTW, ret)      ? " overtemperature warning" : "",
		FIELD_GET(SSM3515_STATUS_BAT_WARN, ret) ? " bat voltage low warning" : "");
}

static int ssm3515_probe(struct snd_soc_component *component)
{
	int ret;

	/* Start out muted */
	ret = snd_soc_component_update_bits(component, SSM3515_DAC,
			SSM3515_DAC_MUTE, SSM3515_DAC_MUTE);
	if (ret < 0)
		return ret;

	/* Disable the 'master power-down' */
	ret = snd_soc_component_update_bits(component, SSM3515_PWR,
			SSM3515_PWR_SPWDN, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int ssm3515_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	int ret;

	ret = snd_soc_component_update_bits(dai->component,
					    SSM3515_DAC,
					    SSM3515_DAC_MUTE,
					    FIELD_PREP(SSM3515_DAC_MUTE, mute));
	if (ret < 0)
		return ret;
	return 0;
}

static int ssm3515_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret, rateval;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
	case SNDRV_PCM_FORMAT_S24:
		ret = snd_soc_component_update_bits(component,
				SSM3515_SAI2, SSM3515_SAI2_DATA_WIDTH,
				FIELD_PREP(SSM3515_SAI2_DATA_WIDTH,
					   params_width(params) == 16));
		if (ret < 0)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 8000 ... 12000:
		rateval = 0;
		break;
	case 16000 ... 24000:
		rateval = 1;
		break;
	case 32000 ... 48000:
		rateval = 2;
		break;
	case 64000 ... 96000:
		rateval = 3;
		break;
	case 128000 ... 192000:
		rateval = 4;
		break;
	case 48001 ... 63999: /* this is ...72000 but overlaps */
		rateval = 5;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component,
			SSM3515_DAC, SSM3515_DAC_FS,
			FIELD_PREP(SSM3515_DAC_FS, rateval));
	if (ret < 0)
		return ret;

	return 0;
}

static int ssm3515_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	bool fpol_inv = false; /* non-inverted: frame starts with low-to-high FSYNC */
	int ret;
	u8 sai1 = 0;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_IB_IF:
		sai1 |= SSM3515_SAI1_BCLK_POL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		fpol_inv = 1;
		sai1 &= ~SSM3515_SAI1_SDATA_FMT; /* 1 bit start delay */
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		fpol_inv = 0;
		sai1 |= SSM3515_SAI1_SDATA_FMT; /* no start delay */
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_IF:
		fpol_inv ^= 1;
		break;
	}

	/* Set the serial input to 'TDM mode' */
	sai1 |= SSM3515_SAI1_SAI_MODE;

	if (fpol_inv) {
		/*
		 * We configure the codec in a 'TDM mode', in which the
		 * FSYNC_MODE bit of SAI1 is supposed to select between
		 * what the datasheet calls 'Pulsed FSYNC mode' and '50%
		 * FSYNC mode'.
		 *
		 * Experiments suggest that this bit in fact simply selects
		 * the FSYNC polarity, so go with that.
		 */
		sai1 |= SSM3515_SAI1_FSYNC_MODE;
	}

	ret = snd_soc_component_update_bits(component, SSM3515_SAI1,
			SSM3515_SAI1_BCLK_POL | SSM3515_SAI1_SDATA_FMT |
			SSM3515_SAI1_SAI_MODE | SSM3515_SAI1_FSYNC_MODE, sai1);

	if (ret < 0)
		return ret;
	return 0;
}

static int ssm3515_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	int slot, tdm_bclks_val, ret;

	if (tx_mask == 0 || rx_mask != 0)
		return -EINVAL;

	slot = __ffs(tx_mask);

	if (tx_mask & ~BIT(slot))
		return -EINVAL;

	switch (slot_width) {
	case 16:
		tdm_bclks_val = 0;
		break;
	case 24:
		tdm_bclks_val = 1;
		break;
	case 32:
		tdm_bclks_val = 2;
		break;
	case 48:
		tdm_bclks_val = 3;
		break;
	case 64:
		tdm_bclks_val = 4;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, SSM3515_SAI1,
			SSM3515_SAI1_TDM_BCLKS,
			FIELD_PREP(SSM3515_SAI1_TDM_BCLKS, tdm_bclks_val));
	if (ret < 0)
		return ret;

	ret = snd_soc_component_update_bits(component, SSM3515_SAI2,
			SSM3515_SAI2_TDM_SLOT,
			FIELD_PREP(SSM3515_SAI2_TDM_SLOT, slot));
	if (ret < 0)
		return ret;

	return 0;
}

static int ssm3515_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	/*
	 * We don't get live notification of faults, so at least at
	 * this time, when playback is over, check if we have tripped
	 * over anything and if so, log it.
	 */
	ssm3515_read_faults(dai->component);
	return 0;
}

static const struct snd_soc_dai_ops ssm3515_dai_ops = {
	.mute_stream	= ssm3515_mute,
	.hw_params	= ssm3515_hw_params,
	.set_fmt	= ssm3515_set_fmt,
	.set_tdm_slot	= ssm3515_set_tdm_slot,
	.hw_free	= ssm3515_hw_free,
};

static struct snd_soc_dai_driver ssm3515_dai_driver = {
	.name = "SSM3515 SAI",
	.id = 0,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &ssm3515_dai_ops,
};

static const struct snd_soc_dapm_widget ssm3515_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route ssm3515_dapm_routes[] = {
	{"OUT", NULL, "DAC"},
	{"DAC", NULL, "Playback"},
};

static const struct snd_soc_component_driver ssm3515_asoc_component = {
	.probe = ssm3515_probe,
	.controls = ssm3515_snd_controls,
	.num_controls = ARRAY_SIZE(ssm3515_snd_controls),
	.dapm_widgets = ssm3515_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ssm3515_dapm_widgets),
	.dapm_routes = ssm3515_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(ssm3515_dapm_routes),
	.endianness = 1,
};

static int ssm3515_i2c_probe(struct i2c_client *client)
{
	struct ssm3515_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &client->dev;
	i2c_set_clientdata(client, data);

	data->regmap = devm_regmap_init_i2c(client, &ssm3515_i2c_regmap);
	if (IS_ERR(data->regmap))
		return dev_err_probe(data->dev, PTR_ERR(data->regmap),
				     "initializing register map\n");

	/* Perform a reset */
	ret = regmap_update_bits(data->regmap, SSM3515_PWR,
			SSM3515_PWR_S_RST, SSM3515_PWR_S_RST);
	if (ret < 0)
		return dev_err_probe(data->dev, ret,
				     "performing software reset\n");
	regmap_reinit_cache(data->regmap, &ssm3515_i2c_regmap);

	return devm_snd_soc_register_component(data->dev,
			&ssm3515_asoc_component,
			&ssm3515_dai_driver, 1);
}

static const struct of_device_id ssm3515_of_match[] = {
	{ .compatible = "adi,ssm3515" },
	{}
};
MODULE_DEVICE_TABLE(of, ssm3515_of_match);

static struct i2c_driver ssm3515_i2c_driver = {
	.driver = {
		.name = "ssm3515",
		.of_match_table = ssm3515_of_match,
	},
	.probe = ssm3515_i2c_probe,
};
module_i2c_driver(ssm3515_i2c_driver);

MODULE_AUTHOR("Martin Povišer <povik+lin@cutebit.org>");
MODULE_DESCRIPTION("ASoC SSM3515 audio amp driver");
MODULE_LICENSE("Dual MIT/GPL");
