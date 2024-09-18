// SPDX-License-Identifier: GPL-2.0
//
// ak4613.c  --  Asahi Kasei ALSA Soc Audio driver
//
// Copyright (C) 2015 Renesas Electronics Corporation
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// Based on ak4642.c by Kuninori Morimoto
// Based on wm8731.c by Richard Purdie
// Based on ak4535.c by Richard Purdie
// Based on wm8753.c by Liam Girdwood

/*
 *		+-------+
 *		|AK4613	|
 *	SDTO1 <-|	|
 *		|	|
 *	SDTI1 ->|	|
 *	SDTI2 ->|	|
 *	SDTI3 ->|	|
 *		+-------+
 *
 *	  +---+
 * clk	  |   |___________________________________________...
 *
 * [TDM512]
 * SDTO1  [L1][R1][L2][R2]
 * SDTI1  [L1][R1][L2][R2][L3][R3][L4][R4][L5][R5][L6][R6]
 *
 * [TDM256]
 * SDTO1  [L1][R1][L2][R2]
 * SDTI1  [L1][R1][L2][R2][L3][R3][L4][R4]
 * SDTI2  [L5][R5][L6][R6]
 *
 * [TDM128]
 * SDTO1  [L1][R1][L2][R2]
 * SDTI1  [L1][R1][L2][R2]
 * SDTI2  [L3][R3][L4][R4]
 * SDTI3  [L5][R5][L6][R6]
 *
 * [STEREO]
 *	Playback  2ch : SDTI1
 *	Capture   2ch : SDTO1
 *
 * [TDM512]
 *	Playback 12ch : SDTI1
 *	Capture   4ch : SDTO1
 *
 * [TDM256]
 *	Playback 12ch : SDTI1 + SDTI2
 *	Playback  8ch : SDTI1
 *	Capture   4ch : SDTO1
 *
 * [TDM128]
 *	Playback 12ch : SDTI1 + SDTI2 + SDTI3
 *	Playback  8ch : SDTI1 + SDTI2
 *	Playback  4ch : SDTI1
 *	Capture   4ch : SDTO1
 *
 *
 * !!! NOTE !!!
 *
 * Renesas is the only user of ak4613 on upstream so far,
 * but the chip connection is like below.
 * Thus, Renesas can't test all connection case.
 * Tested TDM is very limited.
 *
 * +-----+	+-----------+
 * | SoC |	|  AK4613   |
 * |     |<-----|SDTO1	 IN1|<-- Mic
 * |     |	|	 IN2|
 * |     |	|	    |
 * |     |----->|SDTI1	OUT1|--> Headphone
 * +-----+	|SDTI2	OUT2|
 *		|SDTI3	OUT3|
 *		|	OUT4|
 *		|	OUT5|
 *		|	OUT6|
 *		+-----------+
 *
 * Renesas SoC can handle [2,  6,8]    channels.
 * Ak4613      can handle [2,4,  8,12] channels.
 *
 * Because of above HW connection and available channels number,
 * Renesas could test are ...
 *
 *	[STEREO] Playback  2ch : SDTI1
 *		 Capture   2ch : SDTO1
 *	[TDM256] Playback  8ch : SDTI1 (*)
 *
 * (*) it used 8ch data between SoC <-> AK4613 on TDM256 mode,
 *     but could confirm is only first 2ch because only 1
 *     Headphone is connected.
 *
 * see
 *	AK4613_ENABLE_TDM_TEST
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_graph.h>
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

/* ICTRL */
#define ICTRL_MASK	(0x3)

/* OCTRL */
#define OCTRL_MASK	(0x3F)

/*
 * configs
 *
 * 0x000000BA
 *
 * B : AK4613_CONFIG_SDTI_x
 * A : AK4613_CONFIG_MODE_x
 */
#define AK4613_CONFIG_SET(priv, x)	 priv->configs |= AK4613_CONFIG_##x
#define AK4613_CONFIG_GET(priv, x)	(priv->configs &  AK4613_CONFIG_##x##_MASK)

/*
 * AK4613_CONFIG_SDTI_x
 *
 * It indicates how many SDTIx is connected.
 */
#define AK4613_CONFIG_SDTI_MASK		(0xF << 4)
#define AK4613_CONFIG_SDTI(x)		(((x) & 0xF) << 4)
#define AK4613_CONFIG_SDTI_set(priv, x)	  AK4613_CONFIG_SET(priv, SDTI(x))
#define AK4613_CONFIG_SDTI_get(priv)	((AK4613_CONFIG_GET(priv, SDTI) >> 4) & 0xF)

/*
 * AK4613_CONFIG_MODE_x
 *
 * Same as Ctrl1 :: TDM1/TDM0
 * No shift is requested
 * see
 *	AK4613_CTRL1_TO_MODE()
 *	Table 11/12/13/14
 */
#define AK4613_CONFIG_MODE_MASK		(0xF)
#define AK4613_CONFIG_MODE_STEREO	(0x0)
#define AK4613_CONFIG_MODE_TDM512	(0x1)
#define AK4613_CONFIG_MODE_TDM256	(0x2)
#define AK4613_CONFIG_MODE_TDM128	(0x3)

/*
 * !!!! FIXME !!!!
 *
 * Because of testable HW limitation, TDM256 8ch TDM was only tested.
 * This driver uses AK4613_ENABLE_TDM_TEST instead of new DT property so far.
 * Don't hesitate to update driver, you don't need to care compatible
 * with Renesas.
 *
 * #define AK4613_ENABLE_TDM_TEST
 */

struct ak4613_interface {
	unsigned int width;
	unsigned int fmt;
	u8 dif;
};

struct ak4613_priv {
	struct mutex lock;
	struct snd_pcm_hw_constraint_list constraint_rates;
	struct snd_pcm_hw_constraint_list constraint_channels;
	struct work_struct dummy_write_work;
	struct snd_soc_component *component;
	unsigned int rate;
	unsigned int sysclk;

	unsigned int fmt;
	unsigned int configs;
	int cnt;
	u8 ctrl1;
	u8 oc;
	u8 ic;
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

/*
 * CTRL1 register
 * see
 *	Table 11/12/13/14
 */
#define AUDIO_IFACE(_dif, _width, _fmt)		\
	{					\
		.dif	= _dif,			\
		.width	= _width,		\
		.fmt	= SND_SOC_DAIFMT_##_fmt,\
	}
static const struct ak4613_interface ak4613_iface[] = {
	/* It doesn't support asymmetric format */

	AUDIO_IFACE(0x03, 24, LEFT_J),
	AUDIO_IFACE(0x04, 24, I2S),
};
#define AK4613_CTRL1_TO_MODE(priv)	((priv)->ctrl1 >> 6) /* AK4613_CONFIG_MODE_x */

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
	struct snd_soc_component *component = dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;

	mutex_lock(&priv->lock);
	priv->cnt--;
	if (priv->cnt < 0) {
		dev_err(dev, "unexpected counter error\n");
		priv->cnt = 0;
	}
	if (!priv->cnt)
		priv->ctrl1 = 0;
	mutex_unlock(&priv->lock);
}

static void ak4613_hw_constraints(struct ak4613_priv *priv,
				  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	static const unsigned int ak4613_rates[] = {
		 32000,
		 44100,
		 48000,
		 64000,
		 88200,
		 96000,
		176400,
		192000,
	};
#define AK4613_CHANNEL_2	 0
#define AK4613_CHANNEL_4	 1
#define AK4613_CHANNEL_8	 2
#define AK4613_CHANNEL_12	 3
#define AK4613_CHANNEL_NONE	-1
	static const unsigned int ak4613_channels[] = {
		[AK4613_CHANNEL_2]  =  2,
		[AK4613_CHANNEL_4]  =  4,
		[AK4613_CHANNEL_8]  =  8,
		[AK4613_CHANNEL_12] = 12,
	};
#define MODE_MAX 4
#define SDTx_MAX 4
#define MASK(x) (1 << AK4613_CHANNEL_##x)
	static const int mask_list[MODE_MAX][SDTx_MAX] = {
		/* 				SDTO	 SDTIx1    SDTIx2		SDTIx3 */
		[AK4613_CONFIG_MODE_STEREO] = { MASK(2), MASK(2),  MASK(2),		MASK(2)},
		[AK4613_CONFIG_MODE_TDM512] = { MASK(4), MASK(12), MASK(12),		MASK(12)},
		[AK4613_CONFIG_MODE_TDM256] = { MASK(4), MASK(8),  MASK(8)|MASK(12),	MASK(8)|MASK(12)},
		[AK4613_CONFIG_MODE_TDM128] = { MASK(4), MASK(4),  MASK(4)|MASK(8),	MASK(4)|MASK(8)|MASK(12)},
	};
	struct snd_pcm_hw_constraint_list *constraint;
	unsigned int mask;
	unsigned int mode;
	unsigned int fs;
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int sdti_num;
	int i;

	constraint		= &priv->constraint_rates;
	constraint->list	= ak4613_rates;
	constraint->mask	= 0;
	constraint->count	= 0;

	/*
	 * Slave Mode
	 *	Normal: [32kHz, 48kHz] : 256fs,384fs or 512fs
	 *	Double: [64kHz, 96kHz] : 256fs
	 *	Quad  : [128kHz,192kHz]: 128fs
	 *
	 * Master mode
	 *	Normal: [32kHz, 48kHz] : 256fs or 512fs
	 *	Double: [64kHz, 96kHz] : 256fs
	 *	Quad  : [128kHz,192kHz]: 128fs
	*/
	for (i = 0; i < ARRAY_SIZE(ak4613_rates); i++) {
		/* minimum fs on each range */
		fs = (ak4613_rates[i] <= 96000) ? 256 : 128;

		if (priv->sysclk >= ak4613_rates[i] * fs)
			constraint->count = i + 1;
	}

	snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, constraint);


	sdti_num = AK4613_CONFIG_SDTI_get(priv);
	if (WARN_ON(sdti_num >= SDTx_MAX))
		return;

	if (priv->cnt) {
		/*
		 * If it was already working,
		 * the constraint is same as working mode.
		 */
		mode = AK4613_CTRL1_TO_MODE(priv);
		mask = 0; /* no default */
	} else {
		/*
		 * It is not yet working,
		 * the constraint is based on board configs.
		 * STEREO mask is default
		 */
		mode = AK4613_CONFIG_GET(priv, MODE);
		mask = mask_list[AK4613_CONFIG_MODE_STEREO][is_play * sdti_num];
	}

	if (WARN_ON(mode >= MODE_MAX))
		return;

	/* add each mode mask */
	mask |= mask_list[mode][is_play * sdti_num];

	constraint		= &priv->constraint_channels;
	constraint->list	= ak4613_channels;
	constraint->mask	= mask;
	constraint->count	= sizeof(ak4613_channels);
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, constraint);
}

static int ak4613_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);

	mutex_lock(&priv->lock);
	ak4613_hw_constraints(priv, substream);
	priv->cnt++;
	mutex_unlock(&priv->lock);

	return 0;
}

static int ak4613_dai_set_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);

	priv->sysclk = freq;

	return 0;
}

static int ak4613_dai_set_fmt(struct snd_soc_dai *dai, unsigned int format)
{
	struct snd_soc_component *component = dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int fmt;

	fmt = format & SND_SOC_DAIFMT_FORMAT_MASK;
	switch (fmt) {
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_I2S:
		priv->fmt = fmt;
		break;
	default:
		return -EINVAL;
	}

	fmt = format & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;
	switch (fmt) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		/*
		 * SUPPORTME
		 *
		 * "clock provider" is not yet supperted
		 */
		return -EINVAL;
	}

	return 0;
}

static int ak4613_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	unsigned int width = params_width(params);
	unsigned int fmt = priv->fmt;
	unsigned int rate;
	int i, ret;
	u8 ctrl2;

	rate = params_rate(params);
	switch (rate) {
	case 32000:
	case 44100:
	case 48000:
		ctrl2 = DFS_NORMAL_SPEED;
		break;
	case 64000:
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
	priv->rate = rate;

	/*
	 * FIXME
	 *
	 * It doesn't have full TDM suppert yet
	 */
	ret = -EINVAL;

	mutex_lock(&priv->lock);
	if (priv->cnt > 1) {
		/*
		 * If it was already working, use current priv->ctrl1
		 */
		ret = 0;
	} else {
		/*
		 * It is not yet working,
		 */
		unsigned int channel = params_channels(params);
		u8 tdm;

		/* STEREO or TDM */
		if (channel == 2)
			tdm = AK4613_CONFIG_MODE_STEREO;
		else
			tdm = AK4613_CONFIG_GET(priv, MODE);

		for (i = ARRAY_SIZE(ak4613_iface) - 1; i >= 0; i--) {
			const struct ak4613_interface *iface = ak4613_iface + i;

			if ((iface->fmt == fmt) && (iface->width == width)) {
				/*
				 * Ctrl1
				 * | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0  |
				 * |TDM1|TDM0|DIF2|DIF1|DIF0|ATS1|ATS0|SMUTE|
				 *  <  tdm  > < iface->dif >
				 */
				priv->ctrl1 = (tdm << 6) | (iface->dif << 3);
				ret = 0;
				break;
			}
		}
	}
	mutex_unlock(&priv->lock);

	if (ret < 0)
		goto hw_params_end;

	snd_soc_component_update_bits(component, CTRL1, FMT_MASK, priv->ctrl1);
	snd_soc_component_update_bits(component, CTRL2, DFS_MASK, ctrl2);

	snd_soc_component_update_bits(component, ICTRL, ICTRL_MASK, priv->ic);
	snd_soc_component_update_bits(component, OCTRL, OCTRL_MASK, priv->oc);

hw_params_end:
	if (ret < 0)
		dev_warn(dev, "unsupported data width/format combination\n");

	return ret;
}

static int ak4613_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	u8 mgmt1 = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		mgmt1 |= RSTN;
		fallthrough;
	case SND_SOC_BIAS_PREPARE:
		mgmt1 |= PMADC | PMDAC;
		fallthrough;
	case SND_SOC_BIAS_STANDBY:
		mgmt1 |= PMVR;
		fallthrough;
	case SND_SOC_BIAS_OFF:
	default:
		break;
	}

	snd_soc_component_write(component, PW_MGMT1, mgmt1);

	return 0;
}

static void ak4613_dummy_write(struct work_struct *work)
{
	struct ak4613_priv *priv = container_of(work,
						struct ak4613_priv,
						dummy_write_work);
	struct snd_soc_component *component = priv->component;
	unsigned int mgmt1;
	unsigned int mgmt3;

	/*
	 * PW_MGMT1 / PW_MGMT3 needs dummy write at least after 5 LR clocks
	 *
	 * Note
	 *
	 * To avoid extra delay, we want to avoid preemption here,
	 * but we can't. Because it uses I2C access which is using IRQ
	 * and sleep. Thus, delay might be more than 5 LR clocks
	 * see also
	 *	ak4613_dai_trigger()
	 */
	udelay(5000000 / priv->rate);

	mgmt1 = snd_soc_component_read(component, PW_MGMT1);
	mgmt3 = snd_soc_component_read(component, PW_MGMT3);

	snd_soc_component_write(component, PW_MGMT1, mgmt1);
	snd_soc_component_write(component, PW_MGMT3, mgmt3);
}

static int ak4613_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4613_priv *priv = snd_soc_component_get_drvdata(component);

	/*
	 * FIXME
	 *
	 * PW_MGMT1 / PW_MGMT3 needs dummy write at least after 5 LR clocks
	 * from Power Down Release. Otherwise, Playback volume will be 0dB.
	 * To avoid complex multiple delay/dummy_write method from
	 * ak4613_set_bias_level() / SND_SOC_DAPM_DAC_E("DACx", ...),
	 * call it once here.
	 *
	 * But, unfortunately, we can't "write" here because here is atomic
	 * context (It uses I2C access for writing).
	 * Thus, use schedule_work() to switching to normal context
	 * immediately.
	 *
	 * Note
	 *
	 * Calling ak4613_dummy_write() function might be delayed.
	 * In such case, ak4613 volume might be temporarily 0dB when
	 * beggining of playback.
	 * see also
	 *	ak4613_dummy_write()
	 */

	if ((cmd != SNDRV_PCM_TRIGGER_START) &&
	    (cmd != SNDRV_PCM_TRIGGER_RESUME))
		return 0;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return  0;

	priv->component = component;
	schedule_work(&priv->dummy_write_work);

	return 0;
}

/*
 * Select below from Sound Card, not Auto
 *	SND_SOC_DAIFMT_CBC_CFC
 *	SND_SOC_DAIFMT_CBP_CFP
 */
static const u64 ak4613_dai_formats =
	SND_SOC_POSSIBLE_DAIFMT_I2S	|
	SND_SOC_POSSIBLE_DAIFMT_LEFT_J;

static const struct snd_soc_dai_ops ak4613_dai_ops = {
	.startup	= ak4613_dai_startup,
	.shutdown	= ak4613_dai_shutdown,
	.set_sysclk	= ak4613_dai_set_sysclk,
	.set_fmt	= ak4613_dai_set_fmt,
	.trigger	= ak4613_dai_trigger,
	.hw_params	= ak4613_dai_hw_params,
	.auto_selectable_formats	= &ak4613_dai_formats,
	.num_auto_selectable_formats	= 1,
};

#define AK4613_PCM_RATE		(SNDRV_PCM_RATE_32000  |\
				 SNDRV_PCM_RATE_44100  |\
				 SNDRV_PCM_RATE_48000  |\
				 SNDRV_PCM_RATE_64000  |\
				 SNDRV_PCM_RATE_88200  |\
				 SNDRV_PCM_RATE_96000  |\
				 SNDRV_PCM_RATE_176400 |\
				 SNDRV_PCM_RATE_192000)
#define AK4613_PCM_FMTBIT	(SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver ak4613_dai = {
	.name = "ak4613-hifi",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 12,
		.rates		= AK4613_PCM_RATE,
		.formats	= AK4613_PCM_FMTBIT,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 4,
		.rates		= AK4613_PCM_RATE,
		.formats	= AK4613_PCM_FMTBIT,
	},
	.ops = &ak4613_dai_ops,
	.symmetric_rate = 1,
};

static int ak4613_suspend(struct snd_soc_component *component)
{
	struct regmap *regmap = dev_get_regmap(component->dev, NULL);

	regcache_cache_only(regmap, true);
	regcache_mark_dirty(regmap);
	return 0;
}

static int ak4613_resume(struct snd_soc_component *component)
{
	struct regmap *regmap = dev_get_regmap(component->dev, NULL);

	regcache_cache_only(regmap, false);
	return regcache_sync(regmap);
}

static const struct snd_soc_component_driver soc_component_dev_ak4613 = {
	.suspend		= ak4613_suspend,
	.resume			= ak4613_resume,
	.set_bias_level		= ak4613_set_bias_level,
	.controls		= ak4613_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4613_snd_controls),
	.dapm_widgets		= ak4613_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4613_dapm_widgets),
	.dapm_routes		= ak4613_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4613_intercon),
	.idle_bias_on		= 1,
	.endianness		= 1,
};

static void ak4613_parse_of(struct ak4613_priv *priv,
			    struct device *dev)
{
	struct device_node *np = dev->of_node;
	char prop[32];
	int sdti_num;
	int i;

	/* Input 1 - 2 */
	for (i = 0; i < 2; i++) {
		snprintf(prop, sizeof(prop), "asahi-kasei,in%d-single-end", i + 1);
		if (!of_property_read_bool(np, prop))
			priv->ic |= 1 << i;
	}

	/* Output 1 - 6 */
	for (i = 0; i < 6; i++) {
		snprintf(prop, sizeof(prop), "asahi-kasei,out%d-single-end", i + 1);
		if (!of_property_read_bool(np, prop))
			priv->oc |= 1 << i;
	}

	/*
	 * enable TDM256 test
	 *
	 * !!! FIXME !!!
	 *
	 * It should be configured by DT or other way
	 * if it was full supported.
	 * But it is using ifdef style for now for test
	 * purpose.
	 */
#if defined(AK4613_ENABLE_TDM_TEST)
	AK4613_CONFIG_SET(priv, MODE_TDM256);
#endif

	/*
	 * connected STDI
	 * TDM support is assuming it is probed via Audio-Graph-Card style here.
	 * Default is SDTIx1 if it was probed via Simple-Audio-Card for now.
	 */
	sdti_num = of_graph_get_endpoint_count(np);
	if ((sdti_num >= SDTx_MAX) || (sdti_num < 1))
		sdti_num = 1;

	AK4613_CONFIG_SDTI_set(priv, sdti_num);
}

static int ak4613_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	const struct regmap_config *regmap_cfg;
	struct regmap *regmap;
	struct ak4613_priv *priv;

	regmap_cfg = i2c_get_match_data(i2c);
	if (!regmap_cfg)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ak4613_parse_of(priv, dev);

	priv->ctrl1		= 0;
	priv->cnt		= 0;
	priv->sysclk		= 0;
	INIT_WORK(&priv->dummy_write_work, ak4613_dummy_write);

	mutex_init(&priv->lock);

	i2c_set_clientdata(i2c, priv);

	regmap = devm_regmap_init_i2c(i2c, regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_snd_soc_register_component(dev, &soc_component_dev_ak4613,
				      &ak4613_dai, 1);
}

static struct i2c_driver ak4613_i2c_driver = {
	.driver = {
		.name = "ak4613-codec",
		.of_match_table = ak4613_of_match,
	},
	.probe		= ak4613_i2c_probe,
	.id_table	= ak4613_i2c_id,
};

module_i2c_driver(ak4613_i2c_driver);

MODULE_DESCRIPTION("Soc AK4613 driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_LICENSE("GPL v2");
