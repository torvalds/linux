// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Based on code by Hu Jin
 * Copyright (C) 2014 Asahi Kasei Microdevices Corporation
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/* Registers and fields */
#define AK4375_00_POWER_MANAGEMENT1		0x00
#define PMPLL					BIT(0)	/* 0: PLL off, 1: PLL on */
#define AK4375_01_POWER_MANAGEMENT2		0x01
#define PMCP1					BIT(0)	/* Charge Pump 1: LDO1 and DAC */
#define PMCP2					BIT(1)	/* Charge Pump 2: Class-G HP Amp */
#define PMLDO1P					BIT(4)
#define PMLDO1N					BIT(5)
#define PMLDO					(PMLDO1P | PMLDO1N)
#define AK4375_02_POWER_MANAGEMENT3		0x02
#define AK4375_03_POWER_MANAGEMENT4		0x03
#define AK4375_04_OUTPUT_MODE_SETTING		0x04
#define AK4375_05_CLOCK_MODE_SELECT		0x05
#define FS_MASK					GENMASK(4, 0)
#define FS_8KHZ					0x00
#define FS_11_025KHZ				0x01
#define FS_16KHZ				0x04
#define FS_22_05KHZ				0x05
#define FS_32KHZ				0x08
#define FS_44_1KHZ				0x09
#define FS_48KHZ				0x0a
#define FS_88_2KHZ				0x0d
#define FS_96KHZ				0x0e
#define FS_176_4KHZ				0x11
#define FS_192KHZ				0x12
#define CM_MASK					GENMASK(6, 5)	/* For SRC Bypass mode */
#define CM_0					(0x0 << 5)
#define CM_1					(0x1 << 5)
#define CM_2					(0x2 << 5)
#define CM_3					(0x3 << 5)
#define AK4375_06_DIGITAL_FILTER_SELECT		0x06
#define DADFSEL					BIT(5)	/* 0: in SRC Bypass mode, 1: in SRC mode */
#define DASL					BIT(6)
#define DASD					BIT(7)
#define AK4375_07_DAC_MONO_MIXING		0x07
#define DACMUTE_MASK				(GENMASK(5, 4) | GENMASK(1, 0)) /* Clear to mute */
#define AK4375_08_JITTER_CLEANER_SETTING1	0x08
#define AK4375_09_JITTER_CLEANER_SETTING2	0x09
#define AK4375_0A_JITTER_CLEANER_SETTING3	0x0a
#define SELDAIN					BIT(1)	/* 0: SRC Bypass mode, 1: SRC mode */
#define XCKSEL					BIT(6)	/* 0: PLL0, 1: MCKI */
#define XCKCPSEL				BIT(7)	/* Should be equal to SELDAIN and XCKSEL */
#define AK4375_0B_LCH_OUTPUT_VOLUME		0x0b
#define AK4375_0C_RCH_OUTPUT_VOLUME		0x0c
#define AK4375_0D_HP_VOLUME_CONTROL		0x0d
#define AK4375_0E_PLL_CLK_SOURCE_SELECT		0x0e
#define PLS					BIT(0)	/* 0: MCKI, 1: BCLK */
#define AK4375_0F_PLL_REF_CLK_DIVIDER1		0x0f	/* Reference clock divider [15:8] bits */
#define AK4375_10_PLL_REF_CLK_DIVIDER2		0x10	/* Reference clock divider [7:0] bis */
#define AK4375_11_PLL_FB_CLK_DIVIDER1		0x11	/* Feedback clock divider [15:8] bits */
#define AK4375_12_PLL_FB_CLK_DIVIDER2		0x12	/* Feedback clock divider [7:0] bits */
#define AK4375_13_SRC_CLK_SOURCE		0x13	/* SRC Bypass: SRCCKS=XCKSEL=SELDAIN=0 */
#define SRCCKS					BIT(0)	/* SRC Clock source 0: MCKI, 1: PLL0 */
#define DIV					BIT(4)
#define AK4375_14_DAC_CLK_DIVIDER		0x14
#define AK4375_15_AUDIO_IF_FORMAT		0x15
#define DEVICEID_MASK				GENMASK(7, 5)
#define AK4375_24_MODE_CONTROL			0x24

#define AK4375_PLL_FREQ_OUT_112896000		112896000	/* 44.1 kHz base rate */
#define AK4375_PLL_FREQ_OUT_122880000		122880000	/* 32 and 48 kHz base rates */

#define DEVICEID_AK4375				0x00
#define DEVICEID_AK4375A			0x01
#define DEVICEID_AK4376A			0x02
#define DEVICEID_AK4377				0x03
#define DEVICEID_AK4331				0x07

static const char * const supply_names[] = {
	"avdd", "tvdd"
};

struct ak4375_drvdata {
	struct snd_soc_dai_driver *dai_drv;
	const struct snd_soc_component_driver *comp_drv;
};

struct ak4375_priv {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *pdn_gpiod;
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];
	unsigned int rate;
	unsigned int pld;
	u8 mute_save;
};

static const struct reg_default ak4375_reg_defaults[] = {
	{ 0x00, 0x00 }, { 0x01, 0x00 }, { 0x02, 0x00 },
	{ 0x03, 0x00 }, { 0x04, 0x00 }, { 0x05, 0x00 },
	{ 0x06, 0x00 }, { 0x07, 0x00 }, { 0x08, 0x00 },
	{ 0x09, 0x00 }, { 0x0a, 0x00 }, { 0x0b, 0x19 },
	{ 0x0c, 0x19 }, { 0x0d, 0x75 }, { 0x0e, 0x01 },
	{ 0x0f, 0x00 }, { 0x10, 0x00 }, { 0x11, 0x00 },
	{ 0x12, 0x00 }, { 0x13, 0x00 }, { 0x14, 0x00 },
	{ 0x15, 0x00 }, { 0x24, 0x00 },
};

/*
 * Output Digital volume control:
 * from -12.5 to 3 dB in 0.5 dB steps (mute instead of -12.5 dB)
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -1250, 50, 0);

/*
 * HP-Amp Analog volume control:
 * from -4.2 to 6 dB in 2 dB steps (mute instead of -4.2 dB)
 */
static DECLARE_TLV_DB_SCALE(hpg_tlv, -4200, 20, 0);

static const char * const ak4375_ovolcn_select_texts[]	= { "Dependent", "Independent" };
static const char * const ak4375_mdac_select_texts[]	= { "x1", "x1/2" };
static const char * const ak4375_cpmode_select_texts[]	= {
	"Automatic Switching",
	"+-VDD Operation",
	"+-1/2VDD Operation"
};

/*
 * DASD, DASL bits Digital Filter Setting
 * 0, 0 : Sharp Roll-Off Filter
 * 0, 1 : Slow Roll-Off Filter
 * 1, 0 : Short delay Sharp Roll-Off Filter
 * 1, 1 : Short delay Slow Roll-Off Filter
 */
static const char * const ak4375_digfil_select_texts[] = {
	"Sharp Roll-Off Filter",
	"Slow Roll-Off Filter",
	"Short delay Sharp Roll-Off Filter",
	"Short delay Slow Roll-Off Filter",
};

static const struct soc_enum ak4375_ovolcn_enum =
	SOC_ENUM_SINGLE(AK4375_0B_LCH_OUTPUT_VOLUME, 7,
			ARRAY_SIZE(ak4375_ovolcn_select_texts), ak4375_ovolcn_select_texts);
static const struct soc_enum ak4375_mdacl_enum =
	SOC_ENUM_SINGLE(AK4375_07_DAC_MONO_MIXING, 2,
			ARRAY_SIZE(ak4375_mdac_select_texts), ak4375_mdac_select_texts);
static const struct soc_enum ak4375_mdacr_enum =
	SOC_ENUM_SINGLE(AK4375_07_DAC_MONO_MIXING, 6,
			ARRAY_SIZE(ak4375_mdac_select_texts), ak4375_mdac_select_texts);
static const struct soc_enum ak4375_cpmode_enum =
	SOC_ENUM_SINGLE(AK4375_03_POWER_MANAGEMENT4, 2,
			ARRAY_SIZE(ak4375_cpmode_select_texts), ak4375_cpmode_select_texts);
static const struct soc_enum ak4375_digfil_enum =
	SOC_ENUM_SINGLE(AK4375_06_DIGITAL_FILTER_SELECT, 6,
			ARRAY_SIZE(ak4375_digfil_select_texts), ak4375_digfil_select_texts);

static const struct snd_kcontrol_new ak4375_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Output Volume", AK4375_0B_LCH_OUTPUT_VOLUME,
			 AK4375_0C_RCH_OUTPUT_VOLUME, 0, 0x1f, 0, dac_tlv),
	SOC_SINGLE_TLV("HP-Amp Analog Volume",
		       AK4375_0D_HP_VOLUME_CONTROL, 0, 0x1f, 0, hpg_tlv),

	SOC_DOUBLE("DAC Signal Invert Switch", AK4375_07_DAC_MONO_MIXING, 3, 7, 1, 0),

	SOC_ENUM("Digital Volume Control", ak4375_ovolcn_enum),
	SOC_ENUM("DACL Signal Level", ak4375_mdacl_enum),
	SOC_ENUM("DACR Signal Level", ak4375_mdacr_enum),
	SOC_ENUM("Charge Pump Mode", ak4375_cpmode_enum),
	SOC_ENUM("DAC Digital Filter Mode", ak4375_digfil_enum),
};

static const struct snd_kcontrol_new ak4375_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACL Switch", AK4375_07_DAC_MONO_MIXING, 0, 1, 0),
	SOC_DAPM_SINGLE("RDACL Switch", AK4375_07_DAC_MONO_MIXING, 1, 1, 0),
};

static const struct snd_kcontrol_new ak4375_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACR Switch", AK4375_07_DAC_MONO_MIXING, 4, 1, 0),
	SOC_DAPM_SINGLE("RDACR Switch", AK4375_07_DAC_MONO_MIXING, 5, 1, 0),
};

static int ak4375_dac_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, AK4375_00_POWER_MANAGEMENT1, PMPLL, PMPLL);
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMCP1, PMCP1);
		usleep_range(6500, 7000);
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMLDO, PMLDO);
		usleep_range(1000, 2000);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMCP2, PMCP2);
		usleep_range(4500, 5000);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMCP2, 0x0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMLDO, 0x0);
		snd_soc_component_update_bits(component, AK4375_01_POWER_MANAGEMENT2, PMCP1, 0x0);
		snd_soc_component_update_bits(component, AK4375_00_POWER_MANAGEMENT1, PMPLL, 0x0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget ak4375_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("DAC", NULL, AK4375_02_POWER_MANAGEMENT3, 0, 0, ak4375_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_IN("SDTI", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),

	SND_SOC_DAPM_MIXER("HPR Mixer", AK4375_03_POWER_MANAGEMENT4, 1, 0,
			   &ak4375_hpr_mixer_controls[0], ARRAY_SIZE(ak4375_hpr_mixer_controls)),
	SND_SOC_DAPM_MIXER("HPL Mixer", AK4375_03_POWER_MANAGEMENT4, 0, 0,
			   &ak4375_hpl_mixer_controls[0], ARRAY_SIZE(ak4375_hpl_mixer_controls)),
};

static const struct snd_soc_dapm_route ak4375_intercon[] = {
	{ "DAC",	NULL,		"SDTI" },

	{ "HPL Mixer",	"LDACL Switch",	"DAC" },
	{ "HPL Mixer",	"RDACL Switch",	"DAC" },
	{ "HPR Mixer",	"LDACR Switch",	"DAC" },
	{ "HPR Mixer",	"RDACR Switch",	"DAC" },

	{ "HPL",	NULL,		"HPL Mixer" },
	{ "HPR",	NULL,		"HPR Mixer" },
};

static int ak4375_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4375_priv *ak4375 = snd_soc_component_get_drvdata(component);
	unsigned int freq_in, freq_out;

	ak4375->rate = params_rate(params);

	if (ak4375->rate <= 96000)
		ak4375->pld = 0;
	else
		ak4375->pld = 1;

	freq_in = 32 * ak4375->rate / (ak4375->pld + 1);

	if ((ak4375->rate % 8000) == 0)
		freq_out = AK4375_PLL_FREQ_OUT_122880000;
	else
		freq_out = AK4375_PLL_FREQ_OUT_112896000;

	return snd_soc_dai_set_pll(dai, 0, 0, freq_in, freq_out);
}

static int ak4375_dai_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
			      unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct ak4375_priv *ak4375 = snd_soc_component_get_drvdata(component);
	unsigned int mclk, plm, mdiv, div;
	u8 cms, fs, cm;

	cms = snd_soc_component_read(component, AK4375_05_CLOCK_MODE_SELECT);
	fs = cms & ~FS_MASK;
	cm = cms & ~CM_MASK;

	switch (ak4375->rate) {
	case 8000:
		fs |= FS_8KHZ;
		break;
	case 11025:
		fs |= FS_11_025KHZ;
		break;
	case 16000:
		fs |= FS_16KHZ;
		break;
	case 22050:
		fs |= FS_22_05KHZ;
		break;
	case 32000:
		fs |= FS_32KHZ;
		break;
	case 44100:
		fs |= FS_44_1KHZ;
		break;
	case 48000:
		fs |= FS_48KHZ;
		break;
	case 88200:
		fs |= FS_88_2KHZ;
		break;
	case 96000:
		fs |= FS_96KHZ;
		break;
	case 176400:
		fs |= FS_176_4KHZ;
		break;
	case 192000:
		fs |= FS_192KHZ;
		break;
	default:
		return -EINVAL;
	}

	if (ak4375->rate <= 24000) {
		cm |= CM_1;
		mclk = 512 * ak4375->rate;
		mdiv = freq_out / mclk - 1;
		div = 0;
	} else if (ak4375->rate <= 96000) {
		cm |= CM_0;
		mclk = 256 * ak4375->rate;
		mdiv = freq_out / mclk - 1;
		div = 0;
	} else {
		cm |= CM_3;
		mclk = 128 * ak4375->rate;
		mdiv = 4;
		div = 1;
	}

	/* Writing both fields in one go seems to make playback choppy on start */
	snd_soc_component_update_bits(component, AK4375_05_CLOCK_MODE_SELECT, FS_MASK, fs);
	snd_soc_component_update_bits(component, AK4375_05_CLOCK_MODE_SELECT, CM_MASK, cm);

	snd_soc_component_write(component, AK4375_0F_PLL_REF_CLK_DIVIDER1,
				(ak4375->pld & 0xff00) >> 8);
	snd_soc_component_write(component, AK4375_10_PLL_REF_CLK_DIVIDER2,
				ak4375->pld & 0x00ff);

	plm = freq_out / freq_in - 1;
	snd_soc_component_write(component, AK4375_11_PLL_FB_CLK_DIVIDER1, (plm & 0xff00) >> 8);
	snd_soc_component_write(component, AK4375_12_PLL_FB_CLK_DIVIDER2, plm & 0x00ff);

	snd_soc_component_update_bits(component, AK4375_13_SRC_CLK_SOURCE, DIV, div);

	/* SRCCKS bit: force to 1 for SRC PLL source clock */
	snd_soc_component_update_bits(component, AK4375_13_SRC_CLK_SOURCE, SRCCKS, SRCCKS);

	snd_soc_component_write(component, AK4375_14_DAC_CLK_DIVIDER, mdiv);

	dev_dbg(ak4375->dev, "rate=%d mclk=%d f_in=%d f_out=%d PLD=%d PLM=%d MDIV=%d DIV=%d\n",
		ak4375->rate, mclk, freq_in, freq_out, ak4375->pld, plm, mdiv, div);

	return 0;
}

static int ak4375_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct ak4375_priv *ak4375 = snd_soc_component_get_drvdata(component);
	u8 val = snd_soc_component_read(component, AK4375_07_DAC_MONO_MIXING);

	dev_dbg(ak4375->dev, "mute=%d val=%d\n", mute, val);

	if (mute) {
		ak4375->mute_save = val & DACMUTE_MASK;
		val &= ~DACMUTE_MASK;
	} else {
		val |= ak4375->mute_save;
	}

	snd_soc_component_write(component, AK4375_07_DAC_MONO_MIXING, val);

	return 0;
}

#define AK4375_RATES	(SNDRV_PCM_RATE_8000_48000 |\
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |\
			 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define AK4375_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops ak4375_dai_ops = {
	.hw_params	= ak4375_hw_params,
	.mute_stream	= ak4375_mute,
	.set_pll	= ak4375_dai_set_pll,
};

static struct snd_soc_dai_driver ak4375_dai = {
	.name = "ak4375-hifi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= AK4375_RATES,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.formats	= AK4375_FORMATS,
	},
	.ops = &ak4375_dai_ops,
};

static void ak4375_power_off(struct ak4375_priv *ak4375)
{
	gpiod_set_value_cansleep(ak4375->pdn_gpiod, 0);
	usleep_range(1000, 2000);

	regulator_bulk_disable(ARRAY_SIZE(ak4375->supplies), ak4375->supplies);
}

static int ak4375_power_on(struct ak4375_priv *ak4375)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ak4375->supplies), ak4375->supplies);
	if (ret < 0) {
		dev_err(ak4375->dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	usleep_range(3000, 4000);

	gpiod_set_value_cansleep(ak4375->pdn_gpiod, 1);
	usleep_range(1000, 2000);

	return 0;
}

static int __maybe_unused ak4375_runtime_suspend(struct device *dev)
{
	struct ak4375_priv *ak4375 = dev_get_drvdata(dev);

	regcache_cache_only(ak4375->regmap, true);
	ak4375_power_off(ak4375);

	return 0;
}

static int __maybe_unused ak4375_runtime_resume(struct device *dev)
{
	struct ak4375_priv *ak4375 = dev_get_drvdata(dev);
	int ret;

	ret = ak4375_power_on(ak4375);
	if (ret < 0)
		return ret;

	regcache_cache_only(ak4375->regmap, false);
	regcache_mark_dirty(ak4375->regmap);

	return regcache_sync(ak4375->regmap);
}

static const struct snd_soc_component_driver soc_codec_dev_ak4375 = {
	.controls		= ak4375_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4375_snd_controls),
	.dapm_widgets		= ak4375_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4375_dapm_widgets),
	.dapm_routes		= ak4375_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4375_intercon),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config ak4375_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= AK4375_24_MODE_CONTROL,
	.reg_defaults		= ak4375_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ak4375_reg_defaults),
	.cache_type		= REGCACHE_RBTREE,
};

static const struct ak4375_drvdata ak4375_drvdata = {
	.dai_drv = &ak4375_dai,
	.comp_drv = &soc_codec_dev_ak4375,
};

static const struct dev_pm_ops ak4375_pm = {
	SET_RUNTIME_PM_OPS(ak4375_runtime_suspend, ak4375_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static int ak4375_i2c_probe(struct i2c_client *i2c)
{
	struct ak4375_priv *ak4375;
	const struct ak4375_drvdata *drvdata;
	unsigned int deviceid;
	int ret, i;

	ak4375 = devm_kzalloc(&i2c->dev, sizeof(*ak4375), GFP_KERNEL);
	if (!ak4375)
		return -ENOMEM;

	ak4375->regmap = devm_regmap_init_i2c(i2c, &ak4375_regmap);
	if (IS_ERR(ak4375->regmap))
		return PTR_ERR(ak4375->regmap);

	i2c_set_clientdata(i2c, ak4375);
	ak4375->dev = &i2c->dev;

	drvdata = of_device_get_match_data(&i2c->dev);

	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		ak4375->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(ak4375->dev, ARRAY_SIZE(ak4375->supplies), ak4375->supplies);
	if (ret < 0) {
		dev_err(ak4375->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ak4375->pdn_gpiod = devm_gpiod_get_optional(ak4375->dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(ak4375->pdn_gpiod))
		return dev_err_probe(ak4375->dev, PTR_ERR(ak4375->pdn_gpiod),
				     "failed to get pdn\n");

	ret = ak4375_power_on(ak4375);
	if (ret < 0)
		return ret;

	/* Don't read deviceid from cache */
	regcache_cache_bypass(ak4375->regmap, true);

	ret = regmap_read(ak4375->regmap, AK4375_15_AUDIO_IF_FORMAT, &deviceid);
	if (ret < 0) {
		dev_err(ak4375->dev, "unable to read DEVICEID!\n");
		return ret;
	}

	regcache_cache_bypass(ak4375->regmap, false);

	deviceid = (deviceid & DEVICEID_MASK) >> 5;

	switch (deviceid) {
	case DEVICEID_AK4331:
		dev_err(ak4375->dev, "found untested AK4331\n");
		return -EINVAL;
	case DEVICEID_AK4375:
		dev_dbg(ak4375->dev, "found AK4375\n");
		break;
	case DEVICEID_AK4375A:
		dev_dbg(ak4375->dev, "found AK4375A\n");
		break;
	case DEVICEID_AK4376A:
		dev_err(ak4375->dev, "found unsupported AK4376/A!\n");
		return -EINVAL;
	case DEVICEID_AK4377:
		dev_err(ak4375->dev, "found unsupported AK4377!\n");
		return -EINVAL;
	default:
		dev_err(ak4375->dev, "unrecognized DEVICEID!\n");
		return -EINVAL;
	}

	pm_runtime_set_active(ak4375->dev);
	pm_runtime_enable(ak4375->dev);

	ret = devm_snd_soc_register_component(ak4375->dev, drvdata->comp_drv,
					      drvdata->dai_drv, 1);
	if (ret < 0) {
		dev_err(ak4375->dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ak4375_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
}

static const struct of_device_id ak4375_of_match[] = {
	{ .compatible = "asahi-kasei,ak4375", .data = &ak4375_drvdata },
	{ },
};
MODULE_DEVICE_TABLE(of, ak4375_of_match);

static struct i2c_driver ak4375_i2c_driver = {
	.driver = {
		.name = "ak4375",
		.pm = &ak4375_pm,
		.of_match_table = ak4375_of_match,
	},
	.probe = ak4375_i2c_probe,
	.remove = ak4375_i2c_remove,
};
module_i2c_driver(ak4375_i2c_driver);

MODULE_AUTHOR("Vincent Knecht <vincent.knecht@mailoo.org>");
MODULE_DESCRIPTION("ASoC AK4375 DAC driver");
MODULE_LICENSE("GPL");
