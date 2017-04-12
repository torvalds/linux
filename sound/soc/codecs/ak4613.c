/*
 * ak4613.c  --  Asahi Kasei ALSA Soc Audio driver
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * Based on ak4642.c by Kuninori Morimoto
 * Based on wm8731.c by Richard Purdie
 * Based on ak4535.c by Richard Purdie
 * Based on wm8753.c by Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#define PW_MGMT1	0x00 /* Power Management 1 */
#define PW_MGMT2	0x01 /* Power Management 2 */
#define PW_MGMT3	0x02 /* Power Management 3 */
#define CTRL1		0x03 /* Control 1 */
#define CTRL2		0x04 /* Control 2 */
#define DEMP1		0x05 /* De-emphasis1 */
#define DEMP2		0x06 /* De-emphasis2 */
#define OFD		0x07 /* Overflow Detect */
#define ZRD		0x08 /* Zero Detect */
#define ICTRL		0x09 /* Input Control */
#define OCTRL		0x0a /* Output Control */
#define LOUT1		0x0b /* LOUT1 Volume Control */
#define ROUT1		0x0c /* ROUT1 Volume Control */
#define LOUT2		0x0d /* LOUT2 Volume Control */
#define ROUT2		0x0e /* ROUT2 Volume Control */
#define LOUT3		0x0f /* LOUT3 Volume Control */
#define ROUT3		0x10 /* ROUT3 Volume Control */
#define LOUT4		0x11 /* LOUT4 Volume Control */
#define ROUT4		0x12 /* ROUT4 Volume Control */
#define LOUT5		0x13 /* LOUT5 Volume Control */
#define ROUT5		0x14 /* ROUT5 Volume Control */
#define LOUT6		0x15 /* LOUT6 Volume Control */
#define ROUT6		0x16 /* ROUT6 Volume Control */

/* PW_MGMT1 */
#define RSTN		BIT(0)
#define PMDAC		BIT(1)
#define PMADC		BIT(2)
#define PMVR		BIT(3)

/* PW_MGMT2 */
#define PMAD_ALL	0x7

/* PW_MGMT3 */
#define PMDA_ALL	0x3f

/* CTRL1 */
#define DIF0		BIT(3)
#define DIF1		BIT(4)
#define DIF2		BIT(5)
#define TDM0		BIT(6)
#define TDM1		BIT(7)
#define NO_FMT		(0xff)
#define FMT_MASK	(0xf8)

/* CTRL2 */
#define DFS_MASK		(3 << 2)
#define DFS_NORMAL_SPEED	(0 << 2)
#define DFS_DOUBLE_SPEED	(1 << 2)
#define DFS_QUAD_SPEED		(2 << 2)

struct ak4613_formats {
	unsigned int width;
	unsigned int fmt;
};

struct ak4613_interface {
	struct ak4613_formats capture;
	struct ak4613_formats playback;
};

struct ak4613_priv {
	struct mutex lock;
	const struct ak4613_interface *iface;

	unsigned int fmt;
	u8 oc;
	u8 ic;
	int cnt;
};

/*
 * Playback Volume
 *
 * max : 0x00 : 0 dB
 *       ( 0.5 dB step )
 * min : 0xFE : -127.0 dB
 * mute: 0xFF
 */
static const DECLARE_TLV_DB_SCALE(out_tlv, -12750, 50, 1);

static const struct snd_kcontrol_new ak4613_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Playback Volume1", LOUT1, ROUT1,
			 0, 0xFF, 1, out_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume2", LOUT2, ROUT2,
			 0, 0xFF, 1, out_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume3", LOUT3, ROUT3,
			 0, 0xFF, 1, out_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume4", LOUT4, ROUT4,
			 0, 0xFF, 1, out_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume5", LOUT5, ROUT5,
			 0, 0xFF, 1, out_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume6", LOUT6, ROUT6,
			 0, 0xFF, 1, out_tlv),
};

static const struct reg_default ak4613_reg[] = {
	{ 0x0,  0x0f }, { 0x1,  0x07 }, { 0x2,  0x3f }, { 0x3,  0x20 },
	{ 0x4,  0x20 }, { 0x5,  0x55 }, { 0x6,  0x05 }, { 0x7,  0x07 },
	{ 0x8,  0x0f }, { 0x9,  0x07 }, { 0xa,  0x3f }, { 0xb,  0x00 },
	{ 0xc,  0x00 }, { 0xd,  0x00 }, { 0xe,  0x00 }, { 0xf,  0x00 },
	{ 0x10, 0x00 }, { 0x11, 0x00 }, { 0x12, 0x00 }, { 0x13, 0x00 },
	{ 0x14, 0x00 }, { 0x15, 0x00 }, { 0x16, 0x00 },
};

#define AUDIO_IFACE_TO_VAL(fmts) ((fmts - ak4613_iface) << 3)
#define AUDIO_IFACE(b, fmt) { b, SND_SOC_DAIFMT_##fmt }
static const struct ak4613_interface ak4613_iface[] = {
	/* capture */				/* playback */
	[0] = {	AUDIO_IFACE(24, LEFT_J),	AUDIO_IFACE(16, RIGHT_J) },
	[1] = {	AUDIO_IFACE(24, LEFT_J),	AUDIO_IFACE(20, RIGHT_J) },
	[2] = {	AUDIO_IFACE(24, LEFT_J),	AUDIO_IFACE(24, RIGHT_J) },
	[3] = {	AUDIO_IFACE(24, LEFT_J),	AUDIO_IFACE(24, LEFT_J) },
	[4] = {	AUDIO_IFACE(24, I2S),		AUDIO_IFACE(24, I2S) },
};

static const struct regmap_config ak4613_regmap_cfg = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0x16,
	.reg_defaults		= ak4613_reg,
	.num_reg_defaults	= ARRAY_SIZE(ak4613_reg),
	.cache_type		= REGCACHE_RBTREE,
};

static const struct of_device_id ak4613_of_match[] = {
	{ .compatible = "asahi-kasei,ak4613",	.data = &ak4613_regmap_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, ak4613_of_match);

static const struct i2c_device_id ak4613_i2c_id[] = {
	{ "ak4613", (kernel_ulong_t)&ak4613_regmap_cfg },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4613_i2c_id);

static const struct snd_soc_dapm_widget ak4613_dapm_widgets[] = {

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("LOUT3"),
	SND_SOC_DAPM_OUTPUT("LOUT4"),
	SND_SOC_DAPM_OUTPUT("LOUT5"),
	SND_SOC_DAPM_OUTPUT("LOUT6"),

	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT3"),
	SND_SOC_DAPM_OUTPUT("ROUT4"),
	SND_SOC_DAPM_OUTPUT("ROUT5"),
	SND_SOC_DAPM_OUTPUT("ROUT6"),

	/* Inputs */
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),

	SND_SOC_DAPM_INPUT("RIN1"),
	SND_SOC_DAPM_INPUT("RIN2"),

	/* DAC */
	SND_SOC_DAPM_DAC("DAC1", NULL, PW_MGMT3, 0, 0),
	SND_SOC_DAPM_DAC("DAC2", NULL, PW_MGMT3, 1, 0),
	SND_SOC_DAPM_DAC("DAC3", NULL, PW_MGMT3, 2, 0),
	SND_SOC_DAPM_DAC("DAC4", NULL, PW_MGMT3, 3, 0),
	SND_SOC_DAPM_DAC("DAC5", NULL, PW_MGMT3, 4, 0),
	SND_SOC_DAPM_DAC("DAC6", NULL, PW_MGMT3, 5, 0),

	/* ADC */
	SND_SOC_DAPM_ADC("ADC1", NULL, PW_MGMT2, 0, 0),
	SND_SOC_DAPM_ADC("ADC2", NULL, PW_MGMT2, 1, 0),
};

static const struct snd_soc_dapm_route ak4613_intercon[] = {
	{"LOUT1", NULL, "DAC1"},
	{"LOUT2", NULL, "DAC2"},
	{"LOUT3", NULL, "DAC3"},
	{"LOUT4", NULL, "DAC4"},
	{"LOUT5", NULL, "DAC5"},
	{"LOUT6", NULL, "DAC6"},

	{"ROUT1", NULL, "DAC1"},
	{"ROUT2", NULL, "DAC2"},
	{"ROUT3", NULL, "DAC3"},
	{"ROUT4", NULL, "DAC4"},
	{"ROUT5", NULL, "DAC5"},
	{"ROUT6", NULL, "DAC6"},

	{"DAC1", NULL, "Playback"},
	{"DAC2", NULL, "Playback"},
	{"DAC3", NULL, "Playback"},
	{"DAC4", NULL, "Playback"},
	{"DAC5", NULL, "Playback"},
	{"DAC6", NULL, "Playback"},

	{"Capture", NULL, "ADC1"},
	{"Capture", NULL, "ADC2"},

	{"ADC1", NULL, "LIN1"},
	{"ADC2", NULL, "LIN2"},

	{"ADC1", NULL, "RIN1"},
	{"ADC2", NULL, "RIN2"},
};

static void ak4613_dai_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4613_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct device *dev = codec->dev;

	mutex_lock(&priv->lock);
	priv->cnt--;
	if (priv->cnt < 0) {
		dev_err(dev, "unexpected counter error\n");
		priv->cnt = 0;
	}
	if (!priv->cnt)
		priv->iface = NULL;
	mutex_unlock(&priv->lock);
}

static int ak4613_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4613_priv *priv = snd_soc_codec_get_drvdata(codec);

	fmt &= SND_SOC_DAIFMT_FORMAT_MASK;

	switch (fmt) {
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_I2S:
		priv->fmt = fmt;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static bool ak4613_dai_fmt_matching(const struct ak4613_interface *iface,
				    int is_play,
				    unsigned int fmt, unsigned int width)
{
	const struct ak4613_formats *fmts;

	fmts = (is_play) ? &iface->playback : &iface->capture;

	if (fmts->fmt != fmt)
		return false;

	if (fmt == SND_SOC_DAIFMT_RIGHT_J) {
		if (fmts->width != width)
			return false;
	} else {
		if (fmts->width < width)
			return false;
	}

	return true;
}

static int ak4613_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4613_priv *priv = snd_soc_codec_get_drvdata(codec);
	const struct ak4613_interface *iface;
	struct device *dev = codec->dev;
	unsigned int width = params_width(params);
	unsigned int fmt = priv->fmt;
	unsigned int rate;
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int i, ret;
	u8 fmt_ctrl, ctrl2;

	rate = params_rate(params);
	switch (rate) {
	case 32000:
	case 44100:
	case 48000:
		ctrl2 = DFS_NORMAL_SPEED;
		break;
	case 88200:
	case 96000:
		ctrl2 = DFS_DOUBLE_SPEED;
		break;
	case 176400:
	case 192000:
		ctrl2 = DFS_QUAD_SPEED;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * FIXME
	 *
	 * It doesn't support TDM at this point
	 */
	fmt_ctrl = NO_FMT;
	ret = -EINVAL;
	iface = NULL;

	mutex_lock(&priv->lock);
	if (priv->iface) {
		if (ak4613_dai_fmt_matching(priv->iface, is_play, fmt, width))
			iface = priv->iface;
	} else {
		for (i = ARRAY_SIZE(ak4613_iface); i >= 0; i--) {
			if (!ak4613_dai_fmt_matching(ak4613_iface + i,
						     is_play,
						     fmt, width))
				continue;
			iface = ak4613_iface + i;
			break;
		}
	}

	if ((priv->iface == NULL) ||
	    (priv->iface == iface)) {
		priv->iface = iface;
		priv->cnt++;
		ret = 0;
	}
	mutex_unlock(&priv->lock);

	if (ret < 0)
		goto hw_params_end;

	fmt_ctrl = AUDIO_IFACE_TO_VAL(iface);

	snd_soc_update_bits(codec, CTRL1, FMT_MASK, fmt_ctrl);
	snd_soc_update_bits(codec, CTRL2, DFS_MASK, ctrl2);

	snd_soc_write(codec, ICTRL, priv->ic);
	snd_soc_write(codec, OCTRL, priv->oc);

hw_params_end:
	if (ret < 0)
		dev_warn(dev, "unsupported data width/format combination\n");

	return ret;
}

static int ak4613_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u8 mgmt1 = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		mgmt1 |= RSTN;
		/* fall through */
	case SND_SOC_BIAS_PREPARE:
		mgmt1 |= PMADC | PMDAC;
		/* fall through */
	case SND_SOC_BIAS_STANDBY:
		mgmt1 |= PMVR;
		/* fall through */
	case SND_SOC_BIAS_OFF:
	default:
		break;
	}

	snd_soc_write(codec, PW_MGMT1, mgmt1);

	return 0;
}

static const struct snd_soc_dai_ops ak4613_dai_ops = {
	.shutdown	= ak4613_dai_shutdown,
	.set_fmt	= ak4613_dai_set_fmt,
	.hw_params	= ak4613_dai_hw_params,
};

#define AK4613_PCM_RATE		(SNDRV_PCM_RATE_32000  |\
				 SNDRV_PCM_RATE_44100  |\
				 SNDRV_PCM_RATE_48000  |\
				 SNDRV_PCM_RATE_64000  |\
				 SNDRV_PCM_RATE_88200  |\
				 SNDRV_PCM_RATE_96000  |\
				 SNDRV_PCM_RATE_176400 |\
				 SNDRV_PCM_RATE_192000)
#define AK4613_PCM_FMTBIT	(SNDRV_PCM_FMTBIT_S16_LE |\
				 SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver ak4613_dai = {
	.name = "ak4613-hifi",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= AK4613_PCM_RATE,
		.formats	= AK4613_PCM_FMTBIT,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= AK4613_PCM_RATE,
		.formats	= AK4613_PCM_FMTBIT,
	},
	.ops = &ak4613_dai_ops,
	.symmetric_rates = 1,
};

static int ak4613_suspend(struct snd_soc_codec *codec)
{
	struct regmap *regmap = dev_get_regmap(codec->dev, NULL);

	regcache_cache_only(regmap, true);
	regcache_mark_dirty(regmap);
	return 0;
}

static int ak4613_resume(struct snd_soc_codec *codec)
{
	struct regmap *regmap = dev_get_regmap(codec->dev, NULL);

	regcache_cache_only(regmap, false);
	return regcache_sync(regmap);
}

static struct snd_soc_codec_driver soc_codec_dev_ak4613 = {
	.suspend		= ak4613_suspend,
	.resume			= ak4613_resume,
	.set_bias_level		= ak4613_set_bias_level,
	.component_driver = {
		.controls		= ak4613_snd_controls,
		.num_controls		= ARRAY_SIZE(ak4613_snd_controls),
		.dapm_widgets		= ak4613_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(ak4613_dapm_widgets),
		.dapm_routes		= ak4613_intercon,
		.num_dapm_routes	= ARRAY_SIZE(ak4613_intercon),
	},
};

static void ak4613_parse_of(struct ak4613_priv *priv,
			    struct device *dev)
{
	struct device_node *np = dev->of_node;
	char prop[32];
	int i;

	/* Input 1 - 2 */
	for (i = 0; i < 2; i++) {
		snprintf(prop, sizeof(prop), "asahi-kasei,in%d-single-end", i + 1);
		if (!of_get_property(np, prop, NULL))
			priv->ic |= 1 << i;
	}

	/* Output 1 - 6 */
	for (i = 0; i < 6; i++) {
		snprintf(prop, sizeof(prop), "asahi-kasei,out%d-single-end", i + 1);
		if (!of_get_property(np, prop, NULL))
			priv->oc |= 1 << i;
	}
}

static int ak4613_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct device_node *np = dev->of_node;
	const struct regmap_config *regmap_cfg;
	struct regmap *regmap;
	struct ak4613_priv *priv;

	regmap_cfg = NULL;
	if (np) {
		const struct of_device_id *of_id;

		of_id = of_match_device(ak4613_of_match, dev);
		if (of_id)
			regmap_cfg = of_id->data;
	} else {
		regmap_cfg = (const struct regmap_config *)id->driver_data;
	}

	if (!regmap_cfg)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ak4613_parse_of(priv, dev);

	priv->iface		= NULL;
	priv->cnt		= 0;

	mutex_init(&priv->lock);

	i2c_set_clientdata(i2c, priv);

	regmap = devm_regmap_init_i2c(i2c, regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return snd_soc_register_codec(dev, &soc_codec_dev_ak4613,
				      &ak4613_dai, 1);
}

static int ak4613_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver ak4613_i2c_driver = {
	.driver = {
		.name = "ak4613-codec",
		.of_match_table = ak4613_of_match,
	},
	.probe		= ak4613_i2c_probe,
	.remove		= ak4613_i2c_remove,
	.id_table	= ak4613_i2c_id,
};

module_i2c_driver(ak4613_i2c_driver);

MODULE_DESCRIPTION("Soc AK4613 driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_LICENSE("GPL v2");
