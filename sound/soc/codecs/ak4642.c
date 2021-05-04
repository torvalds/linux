// SPDX-License-Identifier: GPL-2.0
//
// ak4642.c  --  AK4642/AK4643 ALSA Soc Audio driver
//
// Copyright (C) 2009 Renesas Solutions Corp.
// Kuninori Morimoto <morimoto.kuninori@renesas.com>
//
// Based on wm8731.c by Richard Purdie
// Based on ak4535.c by Richard Purdie
// Based on wm8753.c by Liam Girdwood

/* ** CAUTION **
 *
 * This is very simple driver.
 * It can use headphone output / stereo input only
 *
 * AK4642 is tested.
 * AK4643 is tested.
 * AK4648 is tested.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define PW_MGMT1	0x00
#define PW_MGMT2	0x01
#define SG_SL1		0x02
#define SG_SL2		0x03
#define MD_CTL1		0x04
#define MD_CTL2		0x05
#define TIMER		0x06
#define ALC_CTL1	0x07
#define ALC_CTL2	0x08
#define L_IVC		0x09
#define L_DVC		0x0a
#define ALC_CTL3	0x0b
#define R_IVC		0x0c
#define R_DVC		0x0d
#define MD_CTL3		0x0e
#define MD_CTL4		0x0f
#define PW_MGMT3	0x10
#define DF_S		0x11
#define FIL3_0		0x12
#define FIL3_1		0x13
#define FIL3_2		0x14
#define FIL3_3		0x15
#define EQ_0		0x16
#define EQ_1		0x17
#define EQ_2		0x18
#define EQ_3		0x19
#define EQ_4		0x1a
#define EQ_5		0x1b
#define FIL1_0		0x1c
#define FIL1_1		0x1d
#define FIL1_2		0x1e
#define FIL1_3		0x1f	/* The maximum valid register for ak4642 */
#define PW_MGMT4	0x20
#define MD_CTL5		0x21
#define LO_MS		0x22
#define HP_MS		0x23
#define SPK_MS		0x24	/* The maximum valid register for ak4643 */
#define EQ_FBEQAB	0x25
#define EQ_FBEQCD	0x26
#define EQ_FBEQE	0x27	/* The maximum valid register for ak4648 */

/* PW_MGMT1*/
#define PMVCM		(1 << 6) /* VCOM Power Management */
#define PMMIN		(1 << 5) /* MIN Input Power Management */
#define PMDAC		(1 << 2) /* DAC Power Management */
#define PMADL		(1 << 0) /* MIC Amp Lch and ADC Lch Power Management */

/* PW_MGMT2 */
#define HPMTN		(1 << 6)
#define PMHPL		(1 << 5)
#define PMHPR		(1 << 4)
#define MS		(1 << 3) /* master/slave select */
#define MCKO		(1 << 1)
#define PMPLL		(1 << 0)

#define PMHP_MASK	(PMHPL | PMHPR)
#define PMHP		PMHP_MASK

/* PW_MGMT3 */
#define PMADR		(1 << 0) /* MIC L / ADC R Power Management */

/* SG_SL1 */
#define MINS		(1 << 6) /* Switch from MIN to Speaker */
#define DACL		(1 << 4) /* Switch from DAC to Stereo or Receiver */
#define PMMP		(1 << 2) /* MPWR pin Power Management */
#define MGAIN0		(1 << 0) /* MIC amp gain*/

/* SG_SL2 */
#define LOPS		(1 << 6) /* Stero Line-out Power Save Mode */

/* TIMER */
#define ZTM(param)	((param & 0x3) << 4) /* ALC Zero Crossing TimeOut */
#define WTM(param)	(((param & 0x4) << 4) | ((param & 0x3) << 2))

/* ALC_CTL1 */
#define ALC		(1 << 5) /* ALC Enable */
#define LMTH0		(1 << 0) /* ALC Limiter / Recovery Level */

/* MD_CTL1 */
#define PLL3		(1 << 7)
#define PLL2		(1 << 6)
#define PLL1		(1 << 5)
#define PLL0		(1 << 4)
#define PLL_MASK	(PLL3 | PLL2 | PLL1 | PLL0)

#define BCKO_MASK	(1 << 3)
#define BCKO_64		BCKO_MASK

#define DIF_MASK	(3 << 0)
#define DSP		(0 << 0)
#define RIGHT_J		(1 << 0)
#define LEFT_J		(2 << 0)
#define I2S		(3 << 0)

/* MD_CTL2 */
#define FSs(val)	(((val & 0x7) << 0) | ((val & 0x8) << 2))
#define PSs(val)	((val & 0x3) << 6)

/* MD_CTL3 */
#define BST1		(1 << 3)

/* MD_CTL4 */
#define DACH		(1 << 0)

struct ak4642_drvdata {
	const struct regmap_config *regmap_config;
	int extended_frequencies;
};

struct ak4642_priv {
	const struct ak4642_drvdata *drvdata;
	struct clk *mcko;
};

/*
 * Playback Volume (table 39)
 *
 * max : 0x00 : +12.0 dB
 *       ( 0.5 dB step )
 * min : 0xFE : -115.0 dB
 * mute: 0xFF
 */
static const DECLARE_TLV_DB_SCALE(out_tlv, -11550, 50, 1);

static const struct snd_kcontrol_new ak4642_snd_controls[] = {

	SOC_DOUBLE_R_TLV("Digital Playback Volume", L_DVC, R_DVC,
			 0, 0xFF, 1, out_tlv),
	SOC_SINGLE("ALC Capture Switch", ALC_CTL1, 5, 1, 0),
	SOC_SINGLE("ALC Capture ZC Switch", ALC_CTL1, 4, 1, 1),
};

static const struct snd_kcontrol_new ak4642_headphone_control =
	SOC_DAPM_SINGLE("Switch", PW_MGMT2, 6, 1, 0);

static const struct snd_kcontrol_new ak4642_lout_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACL", SG_SL1, 4, 1, 0),
};

/* event handlers */
static int ak4642_lout_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
	case SND_SOC_DAPM_PRE_PMU:
		/* Power save mode ON */
		snd_soc_component_update_bits(component, SG_SL2, LOPS, LOPS);
		break;
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_POST_PMD:
		/* Power save mode OFF */
		msleep(300);
		snd_soc_component_update_bits(component, SG_SL2, LOPS, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget ak4642_dapm_widgets[] = {

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("LINEOUT"),

	SND_SOC_DAPM_PGA("HPL Out", PW_MGMT2, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR Out", PW_MGMT2, 4, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH("Headphone Enable", SND_SOC_NOPM, 0, 0,
			    &ak4642_headphone_control),

	SND_SOC_DAPM_PGA("DACH", MD_CTL4, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER_E("LINEOUT Mixer", PW_MGMT1, 3, 0,
			   &ak4642_lout_mixer_controls[0],
			   ARRAY_SIZE(ak4642_lout_mixer_controls),
			   ak4642_lout_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	/* DAC */
	SND_SOC_DAPM_DAC("DAC", NULL, PW_MGMT1, 2, 0),
};

static const struct snd_soc_dapm_route ak4642_intercon[] = {

	/* Outputs */
	{"HPOUTL", NULL, "HPL Out"},
	{"HPOUTR", NULL, "HPR Out"},
	{"LINEOUT", NULL, "LINEOUT Mixer"},

	{"HPL Out", NULL, "Headphone Enable"},
	{"HPR Out", NULL, "Headphone Enable"},

	{"Headphone Enable", "Switch", "DACH"},

	{"DACH", NULL, "DAC"},

	{"LINEOUT Mixer", "DACL", "DAC"},

	{ "DAC", NULL, "Playback" },
};

/*
 * ak4642 register cache
 */
static const struct reg_default ak4643_reg[] = {
	{  0, 0x00 }, {  1, 0x00 }, {  2, 0x01 }, {  3, 0x00 },
	{  4, 0x02 }, {  5, 0x00 }, {  6, 0x00 }, {  7, 0x00 },
	{  8, 0xe1 }, {  9, 0xe1 }, { 10, 0x18 }, { 11, 0x00 },
	{ 12, 0xe1 }, { 13, 0x18 }, { 14, 0x11 }, { 15, 0x08 },
	{ 16, 0x00 }, { 17, 0x00 }, { 18, 0x00 }, { 19, 0x00 },
	{ 20, 0x00 }, { 21, 0x00 }, { 22, 0x00 }, { 23, 0x00 },
	{ 24, 0x00 }, { 25, 0x00 }, { 26, 0x00 }, { 27, 0x00 },
	{ 28, 0x00 }, { 29, 0x00 }, { 30, 0x00 }, { 31, 0x00 },
	{ 32, 0x00 }, { 33, 0x00 }, { 34, 0x00 }, { 35, 0x00 },
	{ 36, 0x00 },
};

/* The default settings for 0x0 ~ 0x1f registers are the same for ak4642
   and ak4643. So we reuse the ak4643 reg_default for ak4642.
   The valid registers for ak4642 are 0x0 ~ 0x1f which is a subset of ak4643,
   so define NUM_AK4642_REG_DEFAULTS for ak4642.
*/
#define ak4642_reg ak4643_reg
#define NUM_AK4642_REG_DEFAULTS	(FIL1_3 + 1)

static const struct reg_default ak4648_reg[] = {
	{  0, 0x00 }, {  1, 0x00 }, {  2, 0x01 }, {  3, 0x00 },
	{  4, 0x02 }, {  5, 0x00 }, {  6, 0x00 }, {  7, 0x00 },
	{  8, 0xe1 }, {  9, 0xe1 }, { 10, 0x18 }, { 11, 0x00 },
	{ 12, 0xe1 }, { 13, 0x18 }, { 14, 0x11 }, { 15, 0xb8 },
	{ 16, 0x00 }, { 17, 0x00 }, { 18, 0x00 }, { 19, 0x00 },
	{ 20, 0x00 }, { 21, 0x00 }, { 22, 0x00 }, { 23, 0x00 },
	{ 24, 0x00 }, { 25, 0x00 }, { 26, 0x00 }, { 27, 0x00 },
	{ 28, 0x00 }, { 29, 0x00 }, { 30, 0x00 }, { 31, 0x00 },
	{ 32, 0x00 }, { 33, 0x00 }, { 34, 0x00 }, { 35, 0x00 },
	{ 36, 0x00 }, { 37, 0x88 }, { 38, 0x88 }, { 39, 0x08 },
};

static int ak4642_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_component *component = dai->component;

	if (is_play) {
		/*
		 * start headphone output
		 *
		 * PLL, Master Mode
		 * Audio I/F Format :MSB justified (ADC & DAC)
		 * Bass Boost Level : Middle
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p97.
		 */
		snd_soc_component_write(component, L_IVC, 0x91); /* volume */
		snd_soc_component_write(component, R_IVC, 0x91); /* volume */
	} else {
		/*
		 * start stereo input
		 *
		 * PLL Master Mode
		 * Audio I/F Format:MSB justified (ADC & DAC)
		 * Pre MIC AMP:+20dB
		 * MIC Power On
		 * ALC setting:Refer to Table 35
		 * ALC bit=“1”
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p94.
		 */
		snd_soc_component_update_bits(component, SG_SL1, PMMP | MGAIN0, PMMP | MGAIN0);
		snd_soc_component_write(component, TIMER, ZTM(0x3) | WTM(0x3));
		snd_soc_component_write(component, ALC_CTL1, ALC | LMTH0);
		snd_soc_component_update_bits(component, PW_MGMT1, PMADL, PMADL);
		snd_soc_component_update_bits(component, PW_MGMT3, PMADR, PMADR);
	}

	return 0;
}

static void ak4642_dai_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_component *component = dai->component;

	if (is_play) {
	} else {
		/* stop stereo input */
		snd_soc_component_update_bits(component, PW_MGMT1, PMADL, 0);
		snd_soc_component_update_bits(component, PW_MGMT3, PMADR, 0);
		snd_soc_component_update_bits(component, ALC_CTL1, ALC, 0);
	}
}

static int ak4642_dai_set_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct ak4642_priv *priv = snd_soc_component_get_drvdata(component);
	u8 pll;
	int extended_freq = 0;

	switch (freq) {
	case 11289600:
		pll = PLL2;
		break;
	case 12288000:
		pll = PLL2 | PLL0;
		break;
	case 12000000:
		pll = PLL2 | PLL1;
		break;
	case 24000000:
		pll = PLL2 | PLL1 | PLL0;
		break;
	case 13500000:
		pll = PLL3 | PLL2;
		break;
	case 27000000:
		pll = PLL3 | PLL2 | PLL0;
		break;
	case 19200000:
		pll = PLL3;
		extended_freq = 1;
		break;
	case 13000000:
		pll = PLL3 | PLL2 | PLL1;
		extended_freq = 1;
		break;
	case 26000000:
		pll = PLL3 | PLL2 | PLL1 | PLL0;
		extended_freq = 1;
		break;
	default:
		return -EINVAL;
	}

	if (extended_freq && !priv->drvdata->extended_frequencies)
		return -EINVAL;

	snd_soc_component_update_bits(component, MD_CTL1, PLL_MASK, pll);

	return 0;
}

static int ak4642_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u8 data;
	u8 bcko;

	data = MCKO | PMPLL; /* use MCKO */
	bcko = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		data |= MS;
		bcko = BCKO_64;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, PW_MGMT2, MS | MCKO | PMPLL, data);
	snd_soc_component_update_bits(component, MD_CTL1, BCKO_MASK, bcko);

	/* format type */
	data = 0;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		data = LEFT_J;
		break;
	case SND_SOC_DAIFMT_I2S:
		data = I2S;
		break;
	/* FIXME
	 * Please add RIGHT_J / DSP support here
	 */
	default:
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, MD_CTL1, DIF_MASK, data);

	return 0;
}

static int ak4642_set_mcko(struct snd_soc_component *component,
			   u32 frequency)
{
	static const u32 fs_list[] = {
		[0] = 8000,
		[1] = 12000,
		[2] = 16000,
		[3] = 24000,
		[4] = 7350,
		[5] = 11025,
		[6] = 14700,
		[7] = 22050,
		[10] = 32000,
		[11] = 48000,
		[14] = 29400,
		[15] = 44100,
	};
	static const u32 ps_list[] = {
		[0] = 256,
		[1] = 128,
		[2] = 64,
		[3] = 32
	};
	int ps, fs;

	for (ps = 0; ps < ARRAY_SIZE(ps_list); ps++) {
		for (fs = 0; fs < ARRAY_SIZE(fs_list); fs++) {
			if (frequency == ps_list[ps] * fs_list[fs]) {
				snd_soc_component_write(component, MD_CTL2,
					      PSs(ps) | FSs(fs));
				return 0;
			}
		}
	}

	return 0;
}

static int ak4642_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4642_priv *priv = snd_soc_component_get_drvdata(component);
	u32 rate = clk_get_rate(priv->mcko);

	if (!rate)
		rate = params_rate(params) * 256;

	return ak4642_set_mcko(component, rate);
}

static int ak4642_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component, PW_MGMT1, 0x00);
		break;
	default:
		snd_soc_component_update_bits(component, PW_MGMT1, PMVCM, PMVCM);
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops ak4642_dai_ops = {
	.startup	= ak4642_dai_startup,
	.shutdown	= ak4642_dai_shutdown,
	.set_sysclk	= ak4642_dai_set_sysclk,
	.set_fmt	= ak4642_dai_set_fmt,
	.hw_params	= ak4642_dai_hw_params,
};

static struct snd_soc_dai_driver ak4642_dai = {
	.name = "ak4642-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE },
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &ak4642_dai_ops,
	.symmetric_rate = 1,
};

static int ak4642_suspend(struct snd_soc_component *component)
{
	struct regmap *regmap = dev_get_regmap(component->dev, NULL);

	regcache_cache_only(regmap, true);
	regcache_mark_dirty(regmap);
	return 0;
}

static int ak4642_resume(struct snd_soc_component *component)
{
	struct regmap *regmap = dev_get_regmap(component->dev, NULL);

	regcache_cache_only(regmap, false);
	regcache_sync(regmap);
	return 0;
}
static int ak4642_probe(struct snd_soc_component *component)
{
	struct ak4642_priv *priv = snd_soc_component_get_drvdata(component);

	if (priv->mcko)
		ak4642_set_mcko(component, clk_get_rate(priv->mcko));

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_ak4642 = {
	.probe			= ak4642_probe,
	.suspend		= ak4642_suspend,
	.resume			= ak4642_resume,
	.set_bias_level		= ak4642_set_bias_level,
	.controls		= ak4642_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4642_snd_controls),
	.dapm_widgets		= ak4642_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4642_dapm_widgets),
	.dapm_routes		= ak4642_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4642_intercon),
	.idle_bias_on		= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak4642_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= FIL1_3,
	.reg_defaults		= ak4642_reg,
	.num_reg_defaults	= NUM_AK4642_REG_DEFAULTS,
	.cache_type		= REGCACHE_RBTREE,
};

static const struct regmap_config ak4643_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= SPK_MS,
	.reg_defaults		= ak4643_reg,
	.num_reg_defaults	= ARRAY_SIZE(ak4643_reg),
	.cache_type		= REGCACHE_RBTREE,
};

static const struct regmap_config ak4648_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= EQ_FBEQE,
	.reg_defaults		= ak4648_reg,
	.num_reg_defaults	= ARRAY_SIZE(ak4648_reg),
	.cache_type		= REGCACHE_RBTREE,
};

static const struct ak4642_drvdata ak4642_drvdata = {
	.regmap_config = &ak4642_regmap,
};

static const struct ak4642_drvdata ak4643_drvdata = {
	.regmap_config = &ak4643_regmap,
};

static const struct ak4642_drvdata ak4648_drvdata = {
	.regmap_config = &ak4648_regmap,
	.extended_frequencies = 1,
};

#ifdef CONFIG_COMMON_CLK
static struct clk *ak4642_of_parse_mcko(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct clk *clk;
	const char *clk_name = np->name;
	const char *parent_clk_name = NULL;
	u32 rate;

	if (of_property_read_u32(np, "clock-frequency", &rate))
		return NULL;

	if (of_property_read_bool(np, "clocks"))
		parent_clk_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &clk_name);

	clk = clk_register_fixed_rate(dev, clk_name, parent_clk_name, 0, rate);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);

	return clk;
}
#else
#define ak4642_of_parse_mcko(d) 0
#endif

static const struct of_device_id ak4642_of_match[];
static int ak4642_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct device_node *np = dev->of_node;
	const struct ak4642_drvdata *drvdata = NULL;
	struct regmap *regmap;
	struct ak4642_priv *priv;
	struct clk *mcko = NULL;

	if (np) {
		const struct of_device_id *of_id;

		mcko = ak4642_of_parse_mcko(dev);
		if (IS_ERR(mcko))
			mcko = NULL;

		of_id = of_match_device(ak4642_of_match, dev);
		if (of_id)
			drvdata = of_id->data;
	} else {
		drvdata = (const struct ak4642_drvdata *)id->driver_data;
	}

	if (!drvdata) {
		dev_err(dev, "Unknown device type\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->drvdata = drvdata;
	priv->mcko = mcko;

	i2c_set_clientdata(i2c, priv);

	regmap = devm_regmap_init_i2c(i2c, drvdata->regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_snd_soc_register_component(dev,
				&soc_component_dev_ak4642, &ak4642_dai, 1);
}

static const struct of_device_id ak4642_of_match[] = {
	{ .compatible = "asahi-kasei,ak4642",	.data = &ak4642_drvdata},
	{ .compatible = "asahi-kasei,ak4643",	.data = &ak4643_drvdata},
	{ .compatible = "asahi-kasei,ak4648",	.data = &ak4648_drvdata},
	{},
};
MODULE_DEVICE_TABLE(of, ak4642_of_match);

static const struct i2c_device_id ak4642_i2c_id[] = {
	{ "ak4642", (kernel_ulong_t)&ak4642_drvdata },
	{ "ak4643", (kernel_ulong_t)&ak4643_drvdata },
	{ "ak4648", (kernel_ulong_t)&ak4648_drvdata },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4642_i2c_id);

static struct i2c_driver ak4642_i2c_driver = {
	.driver = {
		.name = "ak4642-codec",
		.of_match_table = ak4642_of_match,
	},
	.probe		= ak4642_i2c_probe,
	.id_table	= ak4642_i2c_id,
};

module_i2c_driver(ak4642_i2c_driver);

MODULE_DESCRIPTION("Soc AK4642 driver");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
MODULE_LICENSE("GPL v2");
