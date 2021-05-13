// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Stephan Gerhold
 *
 * Register definitions/sequences taken from various tfa98xx kernel drivers:
 * Copyright (C) 2014-2020 NXP Semiconductors, All Rights Reserved.
 * Copyright (C) 2013 Sony Mobile Communications Inc.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#define TFA989X_STATUSREG		0x00
#define TFA989X_BATTERYVOLTAGE		0x01
#define TFA989X_TEMPERATURE		0x02
#define TFA989X_REVISIONNUMBER		0x03
#define TFA989X_REVISIONNUMBER_REV_MSK	GENMASK(7, 0)	/* device revision */
#define TFA989X_I2SREG			0x04
#define TFA989X_I2SREG_CHSA		6	/* amplifier input select */
#define TFA989X_I2SREG_CHSA_MSK		GENMASK(7, 6)
#define TFA989X_I2SREG_I2SSR		12	/* sample rate */
#define TFA989X_I2SREG_I2SSR_MSK	GENMASK(15, 12)
#define TFA989X_BAT_PROT		0x05
#define TFA989X_AUDIO_CTR		0x06
#define TFA989X_DCDCBOOST		0x07
#define TFA989X_SPKR_CALIBRATION	0x08
#define TFA989X_SYS_CTRL		0x09
#define TFA989X_SYS_CTRL_PWDN		0	/* power down */
#define TFA989X_SYS_CTRL_I2CR		1	/* I2C reset */
#define TFA989X_SYS_CTRL_CFE		2	/* enable CoolFlux DSP */
#define TFA989X_SYS_CTRL_AMPE		3	/* enable amplifier */
#define TFA989X_SYS_CTRL_DCA		4	/* enable boost */
#define TFA989X_SYS_CTRL_SBSL		5	/* DSP configured */
#define TFA989X_SYS_CTRL_AMPC		6	/* amplifier enabled by DSP */
#define TFA989X_I2S_SEL_REG		0x0a
#define TFA989X_I2S_SEL_REG_SPKR_MSK	GENMASK(10, 9)	/* speaker impedance */
#define TFA989X_I2S_SEL_REG_DCFG_MSK	GENMASK(14, 11)	/* DCDC compensation */
#define TFA989X_PWM_CONTROL		0x41
#define TFA989X_CURRENTSENSE1		0x46
#define TFA989X_CURRENTSENSE2		0x47
#define TFA989X_CURRENTSENSE3		0x48
#define TFA989X_CURRENTSENSE4		0x49

#define TFA9895_REVISION		0x12

struct tfa989x_rev {
	unsigned int rev;
	int (*init)(struct regmap *regmap);
};

static bool tfa989x_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg > TFA989X_REVISIONNUMBER;
}

static bool tfa989x_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg < TFA989X_REVISIONNUMBER;
}

static const struct regmap_config tfa989x_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.writeable_reg	= tfa989x_writeable_reg,
	.volatile_reg	= tfa989x_volatile_reg,
	.cache_type	= REGCACHE_RBTREE,
};

static const char * const chsa_text[] = { "Left", "Right", /* "DSP" */ };
static SOC_ENUM_SINGLE_DECL(chsa_enum, TFA989X_I2SREG, TFA989X_I2SREG_CHSA, chsa_text);
static const struct snd_kcontrol_new chsa_mux = SOC_DAPM_ENUM("Amp Input", chsa_enum);

static const struct snd_soc_dapm_widget tfa989x_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_SUPPLY("POWER", TFA989X_SYS_CTRL, TFA989X_SYS_CTRL_PWDN, 1, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("AMPE", TFA989X_SYS_CTRL, TFA989X_SYS_CTRL_AMPE, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Amp Input", SND_SOC_NOPM, 0, 0, &chsa_mux),
	SND_SOC_DAPM_AIF_IN("AIFINL", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFINR", "HiFi Playback", 1, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route tfa989x_dapm_routes[] = {
	{"OUT", NULL, "AMPE"},
	{"AMPE", NULL, "POWER"},
	{"AMPE", NULL, "Amp Input"},
	{"Amp Input", "Left", "AIFINL"},
	{"Amp Input", "Right", "AIFINR"},
};

static const struct snd_soc_component_driver tfa989x_component = {
	.dapm_widgets		= tfa989x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tfa989x_dapm_widgets),
	.dapm_routes		= tfa989x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tfa989x_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const unsigned int tfa989x_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

static int tfa989x_find_sample_rate(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tfa989x_rates); ++i)
		if (tfa989x_rates[i] == rate)
			return i;

	return -EINVAL;
}

static int tfa989x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int sr;

	sr = tfa989x_find_sample_rate(params_rate(params));
	if (sr < 0)
		return sr;

	return snd_soc_component_update_bits(component, TFA989X_I2SREG,
					     TFA989X_I2SREG_I2SSR_MSK,
					     sr << TFA989X_I2SREG_I2SSR);
}

static const struct snd_soc_dai_ops tfa989x_dai_ops = {
	.hw_params = tfa989x_hw_params,
};

static struct snd_soc_dai_driver tfa989x_dai = {
	.name = "tfa989x-hifi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.rate_min	= 8000,
		.rate_max	= 48000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &tfa989x_dai_ops,
};

static const struct reg_sequence tfa9895_reg_init[] = {
	/* some other registers must be set for optimal amplifier behaviour */
	{ TFA989X_BAT_PROT, 0x13ab },
	{ TFA989X_AUDIO_CTR, 0x001f },

	/* peak voltage protection is always on, but may be written */
	{ TFA989X_SPKR_CALIBRATION, 0x3c4e },

	/* TFA989X_SYSCTRL_DCA = 0 */
	{ TFA989X_SYS_CTRL, 0x024d },
	{ TFA989X_PWM_CONTROL, 0x0308 },
	{ TFA989X_CURRENTSENSE4, 0x0e82 },
};

static int tfa9895_init(struct regmap *regmap)
{
	return regmap_multi_reg_write(regmap, tfa9895_reg_init,
				      ARRAY_SIZE(tfa9895_reg_init));
}

static const struct tfa989x_rev tfa9895_rev = {
	.rev	= TFA9895_REVISION,
	.init	= tfa9895_init,
};

/*
 * Note: At the moment this driver bypasses the "CoolFlux DSP" built into the
 * TFA989X amplifiers. Unfortunately, there seems to be absolutely
 * no documentation for it - the public "short datasheets" do not provide
 * any information about the DSP or available registers.
 *
 * Usually the TFA989X amplifiers are configured through proprietary userspace
 * libraries. There are also some (rather complex) kernel drivers but even those
 * rely on obscure firmware blobs for configuration (so-called "containers").
 * They seem to contain different "profiles" with tuned speaker settings, sample
 * rates and volume steps (which would be better exposed as separate ALSA mixers).
 *
 * Bypassing the DSP disables volume control (and perhaps some speaker
 * optimization?), but at least allows using the speaker without obscure
 * kernel drivers and firmware.
 *
 * Ideally NXP (or now Goodix) should release proper documentation for these
 * amplifiers so that support for the "CoolFlux DSP" can be implemented properly.
 */
static int tfa989x_dsp_bypass(struct regmap *regmap)
{
	int ret;

	/* Clear CHSA to bypass DSP and take input from I2S 1 left channel */
	ret = regmap_clear_bits(regmap, TFA989X_I2SREG, TFA989X_I2SREG_CHSA_MSK);
	if (ret)
		return ret;

	/* Set DCDC compensation to off and speaker impedance to 8 ohm */
	ret = regmap_update_bits(regmap, TFA989X_I2S_SEL_REG,
				 TFA989X_I2S_SEL_REG_DCFG_MSK |
				 TFA989X_I2S_SEL_REG_SPKR_MSK,
				 TFA989X_I2S_SEL_REG_SPKR_MSK);
	if (ret)
		return ret;

	/* Set DCDC to follower mode and disable CoolFlux DSP */
	return regmap_clear_bits(regmap, TFA989X_SYS_CTRL,
				 BIT(TFA989X_SYS_CTRL_DCA) |
				 BIT(TFA989X_SYS_CTRL_CFE) |
				 BIT(TFA989X_SYS_CTRL_AMPC));
}

static int tfa989x_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	const struct tfa989x_rev *rev;
	struct regmap *regmap;
	unsigned int val;
	int ret;

	rev = device_get_match_data(dev);
	if (!rev) {
		dev_err(dev, "unknown device revision\n");
		return -ENODEV;
	}

	regmap = devm_regmap_init_i2c(i2c, &tfa989x_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Bypass regcache for reset and init sequence */
	regcache_cache_bypass(regmap, true);

	/* Dummy read to generate i2c clocks, required on some devices */
	regmap_read(regmap, TFA989X_REVISIONNUMBER, &val);

	ret = regmap_read(regmap, TFA989X_REVISIONNUMBER, &val);
	if (ret) {
		dev_err(dev, "failed to read revision number: %d\n", ret);
		return ret;
	}

	val &= TFA989X_REVISIONNUMBER_REV_MSK;
	if (val != rev->rev) {
		dev_err(dev, "invalid revision number, expected %#x, got %#x\n",
			rev->rev, val);
		return -ENODEV;
	}

	ret = regmap_write(regmap, TFA989X_SYS_CTRL, BIT(TFA989X_SYS_CTRL_I2CR));
	if (ret) {
		dev_err(dev, "failed to reset I2C registers: %d\n", ret);
		return ret;
	}

	ret = rev->init(regmap);
	if (ret) {
		dev_err(dev, "failed to initialize registers: %d\n", ret);
		return ret;
	}

	ret = tfa989x_dsp_bypass(regmap);
	if (ret) {
		dev_err(dev, "failed to enable DSP bypass: %d\n", ret);
		return ret;
	}
	regcache_cache_bypass(regmap, false);

	return devm_snd_soc_register_component(dev, &tfa989x_component,
					       &tfa989x_dai, 1);
}

static const struct of_device_id tfa989x_of_match[] = {
	{ .compatible = "nxp,tfa9895", .data = &tfa9895_rev },
	{ }
};
MODULE_DEVICE_TABLE(of, tfa989x_of_match);

static struct i2c_driver tfa989x_i2c_driver = {
	.driver = {
		.name = "tfa989x",
		.of_match_table = tfa989x_of_match,
	},
	.probe_new = tfa989x_i2c_probe,
};
module_i2c_driver(tfa989x_i2c_driver);

MODULE_DESCRIPTION("ASoC NXP/Goodix TFA989X (TFA1) driver");
MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
MODULE_LICENSE("GPL");
