/*
 * CS4270 ALSA SoC (ASoC) codec driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007-2009 Freescale Semiconductor, Inc.  This file is licensed
 * under the terms of the GNU General Public License version 2.  This
 * program is licensed "as is" without any warranty of any kind, whether
 * express or implied.
 *
 * This is an ASoC device driver for the Cirrus Logic CS4270 codec.
 *
 * Current features/limitations:
 *
 * - Software mode is supported.  Stand-alone mode is not supported.
 * - Only I2C is supported, not SPI
 * - Support for master and slave mode
 * - The machine driver's 'startup' function must call
 *   cs4270_set_dai_sysclk() with the value of MCLK.
 * - Only I2S and left-justified modes are supported
 * - Power management is supported
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>

/*
 * The codec isn't really big-endian or little-endian, since the I2S
 * interface requires data to be sent serially with the MSbit first.
 * However, to support BE and LE I2S devices, we specify both here.  That
 * way, ALSA will always match the bit patterns.
 */
#define CS4270_FORMATS (SNDRV_PCM_FMTBIT_S8      | \
			SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_S16_BE  | \
			SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S18_3BE | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE | \
			SNDRV_PCM_FMTBIT_S24_LE  | SNDRV_PCM_FMTBIT_S24_BE)

/* CS4270 registers addresses */
#define CS4270_CHIPID	0x01	/* Chip ID */
#define CS4270_PWRCTL	0x02	/* Power Control */
#define CS4270_MODE	0x03	/* Mode Control */
#define CS4270_FORMAT	0x04	/* Serial Format, ADC/DAC Control */
#define CS4270_TRANS	0x05	/* Transition Control */
#define CS4270_MUTE	0x06	/* Mute Control */
#define CS4270_VOLA	0x07	/* DAC Channel A Volume Control */
#define CS4270_VOLB	0x08	/* DAC Channel B Volume Control */

#define CS4270_FIRSTREG	0x01
#define CS4270_LASTREG	0x08
#define CS4270_NUMREGS	(CS4270_LASTREG - CS4270_FIRSTREG + 1)
#define CS4270_I2C_INCR	0x80

/* Bit masks for the CS4270 registers */
#define CS4270_CHIPID_ID	0xF0
#define CS4270_CHIPID_REV	0x0F
#define CS4270_PWRCTL_FREEZE	0x80
#define CS4270_PWRCTL_PDN_ADC	0x20
#define CS4270_PWRCTL_PDN_DAC	0x02
#define CS4270_PWRCTL_PDN	0x01
#define CS4270_PWRCTL_PDN_ALL	\
	(CS4270_PWRCTL_PDN_ADC | CS4270_PWRCTL_PDN_DAC | CS4270_PWRCTL_PDN)
#define CS4270_MODE_SPEED_MASK	0x30
#define CS4270_MODE_1X		0x00
#define CS4270_MODE_2X		0x10
#define CS4270_MODE_4X		0x20
#define CS4270_MODE_SLAVE	0x30
#define CS4270_MODE_DIV_MASK	0x0E
#define CS4270_MODE_DIV1	0x00
#define CS4270_MODE_DIV15	0x02
#define CS4270_MODE_DIV2	0x04
#define CS4270_MODE_DIV3	0x06
#define CS4270_MODE_DIV4	0x08
#define CS4270_MODE_POPGUARD	0x01
#define CS4270_FORMAT_FREEZE_A	0x80
#define CS4270_FORMAT_FREEZE_B	0x40
#define CS4270_FORMAT_LOOPBACK	0x20
#define CS4270_FORMAT_DAC_MASK	0x18
#define CS4270_FORMAT_DAC_LJ	0x00
#define CS4270_FORMAT_DAC_I2S	0x08
#define CS4270_FORMAT_DAC_RJ16	0x18
#define CS4270_FORMAT_DAC_RJ24	0x10
#define CS4270_FORMAT_ADC_MASK	0x01
#define CS4270_FORMAT_ADC_LJ	0x00
#define CS4270_FORMAT_ADC_I2S	0x01
#define CS4270_TRANS_ONE_VOL	0x80
#define CS4270_TRANS_SOFT	0x40
#define CS4270_TRANS_ZERO	0x20
#define CS4270_TRANS_INV_ADC_A	0x08
#define CS4270_TRANS_INV_ADC_B	0x10
#define CS4270_TRANS_INV_DAC_A	0x02
#define CS4270_TRANS_INV_DAC_B	0x04
#define CS4270_TRANS_DEEMPH	0x01
#define CS4270_MUTE_AUTO	0x20
#define CS4270_MUTE_ADC_A	0x08
#define CS4270_MUTE_ADC_B	0x10
#define CS4270_MUTE_POLARITY	0x04
#define CS4270_MUTE_DAC_A	0x01
#define CS4270_MUTE_DAC_B	0x02

/* Power-on default values for the registers
 *
 * This array contains the power-on default values of the registers, with the
 * exception of the "CHIPID" register (01h).  The lower four bits of that
 * register contain the hardware revision, so it is treated as volatile.
 */
static const struct reg_default cs4270_reg_defaults[] = {
	{ 2, 0x00 },
	{ 3, 0x30 },
	{ 4, 0x00 },
	{ 5, 0x60 },
	{ 6, 0x20 },
	{ 7, 0x00 },
	{ 8, 0x00 },
};

static const char *supply_names[] = {
	"va", "vd", "vlc"
};

/* Private data for the CS4270 */
struct cs4270_private {
	struct regmap *regmap;
	unsigned int mclk; /* Input frequency of the MCLK pin */
	unsigned int mode; /* The mode (I2S or left-justified) */
	unsigned int slave_mode;
	unsigned int manual_mute;

	/* power domain regulators */
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];

	/* reset gpio */
	struct gpio_desc *reset_gpio;
};

static const struct snd_soc_dapm_widget cs4270_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("AINL"),
SND_SOC_DAPM_INPUT("AINR"),

SND_SOC_DAPM_OUTPUT("AOUTL"),
SND_SOC_DAPM_OUTPUT("AOUTR"),
};

static const struct snd_soc_dapm_route cs4270_dapm_routes[] = {
	{ "Capture", NULL, "AINL" },
	{ "Capture", NULL, "AINR" },

	{ "AOUTL", NULL, "Playback" },
	{ "AOUTR", NULL, "Playback" },
};

/**
 * struct cs4270_mode_ratios - clock ratio tables
 * @ratio: the ratio of MCLK to the sample rate
 * @speed_mode: the Speed Mode bits to set in the Mode Control register for
 *              this ratio
 * @mclk: the Ratio Select bits to set in the Mode Control register for this
 *        ratio
 *
 * The data for this chart is taken from Table 5 of the CS4270 reference
 * manual.
 *
 * This table is used to determine how to program the Mode Control register.
 * It is also used by cs4270_set_dai_sysclk() to tell ALSA which sampling
 * rates the CS4270 currently supports.
 *
 * @speed_mode is the corresponding bit pattern to be written to the
 * MODE bits of the Mode Control Register
 *
 * @mclk is the corresponding bit pattern to be wirten to the MCLK bits of
 * the Mode Control Register.
 *
 * In situations where a single ratio is represented by multiple speed
 * modes, we favor the slowest speed.  E.g, for a ratio of 128, we pick
 * double-speed instead of quad-speed.  However, the CS4270 errata states
 * that divide-By-1.5 can cause failures, so we avoid that mode where
 * possible.
 *
 * Errata: There is an errata for the CS4270 where divide-by-1.5 does not
 * work if Vd is 3.3V.  If this effects you, select the
 * CONFIG_SND_SOC_CS4270_VD33_ERRATA Kconfig option, and the driver will
 * never select any sample rates that require divide-by-1.5.
 */
struct cs4270_mode_ratios {
	unsigned int ratio;
	u8 speed_mode;
	u8 mclk;
};

static struct cs4270_mode_ratios cs4270_mode_ratios[] = {
	{64, CS4270_MODE_4X, CS4270_MODE_DIV1},
#ifndef CONFIG_SND_SOC_CS4270_VD33_ERRATA
	{96, CS4270_MODE_4X, CS4270_MODE_DIV15},
#endif
	{128, CS4270_MODE_2X, CS4270_MODE_DIV1},
	{192, CS4270_MODE_4X, CS4270_MODE_DIV3},
	{256, CS4270_MODE_1X, CS4270_MODE_DIV1},
	{384, CS4270_MODE_2X, CS4270_MODE_DIV3},
	{512, CS4270_MODE_1X, CS4270_MODE_DIV2},
	{768, CS4270_MODE_1X, CS4270_MODE_DIV3},
	{1024, CS4270_MODE_1X, CS4270_MODE_DIV4}
};

/* The number of MCLK/LRCK ratios supported by the CS4270 */
#define NUM_MCLK_RATIOS		ARRAY_SIZE(cs4270_mode_ratios)

static bool cs4270_reg_is_readable(struct device *dev, unsigned int reg)
{
	return (reg >= CS4270_FIRSTREG) && (reg <= CS4270_LASTREG);
}

static bool cs4270_reg_is_volatile(struct device *dev, unsigned int reg)
{
	/* Unreadable registers are considered volatile */
	if ((reg < CS4270_FIRSTREG) || (reg > CS4270_LASTREG))
		return true;

	return reg == CS4270_CHIPID;
}

/**
 * cs4270_set_dai_sysclk - determine the CS4270 samples rates.
 * @codec_dai: the codec DAI
 * @clk_id: the clock ID (ignored)
 * @freq: the MCLK input frequency
 * @dir: the clock direction (ignored)
 *
 * This function is used to tell the codec driver what the input MCLK
 * frequency is.
 *
 * The value of MCLK is used to determine which sample rates are supported
 * by the CS4270.  The ratio of MCLK / Fs must be equal to one of nine
 * supported values - 64, 96, 128, 192, 256, 384, 512, 768, and 1024.
 *
 * This function calculates the nine ratios and determines which ones match
 * a standard sample rate.  If there's a match, then it is added to the list
 * of supported sample rates.
 *
 * This function must be called by the machine driver's 'startup' function,
 * otherwise the list of supported sample rates will not be available in
 * time for ALSA.
 *
 * For setups with variable MCLKs, pass 0 as 'freq' argument. This will cause
 * theoretically possible sample rates to be enabled. Call it again with a
 * proper value set one the external clock is set (most probably you would do
 * that from a machine's driver 'hw_param' hook.
 */
static int cs4270_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);

	cs4270->mclk = freq;
	return 0;
}

/**
 * cs4270_set_dai_fmt - configure the codec for the selected audio format
 * @codec_dai: the codec DAI
 * @format: a SND_SOC_DAIFMT_x value indicating the data format
 *
 * This function takes a bitmask of SND_SOC_DAIFMT_x bits and programs the
 * codec accordingly.
 *
 * Currently, this function only supports SND_SOC_DAIFMT_I2S and
 * SND_SOC_DAIFMT_LEFT_J.  The CS4270 codec also supports right-justified
 * data for playback only, but ASoC currently does not support different
 * formats for playback vs. record.
 */
static int cs4270_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);

	/* set DAI format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		cs4270->mode = format & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(component->dev, "invalid dai format\n");
		return -EINVAL;
	}

	/* set master/slave audio interface */
	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		cs4270->slave_mode = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		cs4270->slave_mode = 0;
		break;
	default:
		/* all other modes are unsupported by the hardware */
		dev_err(component->dev, "Unknown master/slave configuration\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * cs4270_hw_params - program the CS4270 with the given hardware parameters.
 * @substream: the audio stream
 * @params: the hardware parameters to set
 * @dai: the SOC DAI (ignored)
 *
 * This function programs the hardware with the values provided.
 * Specifically, the sample rate and the data format.
 *
 * The .ops functions are used to provide board-specific data, like input
 * frequencies, to this driver.  This function takes that information,
 * combines it with the hardware parameters provided, and programs the
 * hardware accordingly.
 */
static int cs4270_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int ret;
	unsigned int i;
	unsigned int rate;
	unsigned int ratio;
	int reg;

	/* Figure out which MCLK/LRCK ratio to use */

	rate = params_rate(params);	/* Sampling rate, in Hz */
	ratio = cs4270->mclk / rate;	/* MCLK/LRCK ratio */

	for (i = 0; i < NUM_MCLK_RATIOS; i++) {
		if (cs4270_mode_ratios[i].ratio == ratio)
			break;
	}

	if (i == NUM_MCLK_RATIOS) {
		/* We did not find a matching ratio */
		dev_err(component->dev, "could not find matching ratio\n");
		return -EINVAL;
	}

	/* Set the sample rate */

	reg = snd_soc_component_read(component, CS4270_MODE);
	reg &= ~(CS4270_MODE_SPEED_MASK | CS4270_MODE_DIV_MASK);
	reg |= cs4270_mode_ratios[i].mclk;

	if (cs4270->slave_mode)
		reg |= CS4270_MODE_SLAVE;
	else
		reg |= cs4270_mode_ratios[i].speed_mode;

	ret = snd_soc_component_write(component, CS4270_MODE, reg);
	if (ret < 0) {
		dev_err(component->dev, "i2c write failed\n");
		return ret;
	}

	/* Set the DAI format */

	reg = snd_soc_component_read(component, CS4270_FORMAT);
	reg &= ~(CS4270_FORMAT_DAC_MASK | CS4270_FORMAT_ADC_MASK);

	switch (cs4270->mode) {
	case SND_SOC_DAIFMT_I2S:
		reg |= CS4270_FORMAT_DAC_I2S | CS4270_FORMAT_ADC_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg |= CS4270_FORMAT_DAC_LJ | CS4270_FORMAT_ADC_LJ;
		break;
	default:
		dev_err(component->dev, "unknown dai format\n");
		return -EINVAL;
	}

	ret = snd_soc_component_write(component, CS4270_FORMAT, reg);
	if (ret < 0) {
		dev_err(component->dev, "i2c write failed\n");
		return ret;
	}

	return ret;
}

/**
 * cs4270_dai_mute - enable/disable the CS4270 external mute
 * @dai: the SOC DAI
 * @mute: 0 = disable mute, 1 = enable mute
 * @direction: (ignored)
 *
 * This function toggles the mute bits in the MUTE register.  The CS4270's
 * mute capability is intended for external muting circuitry, so if the
 * board does not have the MUTEA or MUTEB pins connected to such circuitry,
 * then this function will do nothing.
 */
static int cs4270_dai_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int reg6;

	reg6 = snd_soc_component_read(component, CS4270_MUTE);

	if (mute)
		reg6 |= CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B;
	else {
		reg6 &= ~(CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B);
		reg6 |= cs4270->manual_mute;
	}

	return snd_soc_component_write(component, CS4270_MUTE, reg6);
}

/**
 * cs4270_soc_put_mute - put callback for the 'Master Playback switch'
 * 			 alsa control.
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * This function basically passes the arguments on to the generic
 * snd_soc_put_volsw() function and saves the mute information in
 * our private data structure. This is because we want to prevent
 * cs4270_dai_mute() neglecting the user's decision to manually
 * mute the codec's output.
 *
 * Returns 0 for success.
 */
static int cs4270_soc_put_mute(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int left = !ucontrol->value.integer.value[0];
	int right = !ucontrol->value.integer.value[1];

	cs4270->manual_mute = (left ? CS4270_MUTE_DAC_A : 0) |
			      (right ? CS4270_MUTE_DAC_B : 0);

	return snd_soc_put_volsw(kcontrol, ucontrol);
}

/* A list of non-DAPM controls that the CS4270 supports */
static const struct snd_kcontrol_new cs4270_snd_controls[] = {
	SOC_DOUBLE_R("Master Playback Volume",
		CS4270_VOLA, CS4270_VOLB, 0, 0xFF, 1),
	SOC_SINGLE("Digital Sidetone Switch", CS4270_FORMAT, 5, 1, 0),
	SOC_SINGLE("Soft Ramp Switch", CS4270_TRANS, 6, 1, 0),
	SOC_SINGLE("Zero Cross Switch", CS4270_TRANS, 5, 1, 0),
	SOC_SINGLE("De-emphasis filter", CS4270_TRANS, 0, 1, 0),
	SOC_SINGLE("Popguard Switch", CS4270_MODE, 0, 1, 1),
	SOC_SINGLE("Auto-Mute Switch", CS4270_MUTE, 5, 1, 0),
	SOC_DOUBLE("Master Capture Switch", CS4270_MUTE, 3, 4, 1, 1),
	SOC_DOUBLE_EXT("Master Playback Switch", CS4270_MUTE, 0, 1, 1, 1,
		snd_soc_get_volsw, cs4270_soc_put_mute),
};

static const struct snd_soc_dai_ops cs4270_dai_ops = {
	.hw_params	= cs4270_hw_params,
	.set_sysclk	= cs4270_set_dai_sysclk,
	.set_fmt	= cs4270_set_dai_fmt,
	.mute_stream	= cs4270_dai_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver cs4270_dai = {
	.name = "cs4270-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 4000,
		.rate_max = 216000,
		.formats = CS4270_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 4000,
		.rate_max = 216000,
		.formats = CS4270_FORMATS,
	},
	.ops = &cs4270_dai_ops,
};

/**
 * cs4270_probe - ASoC probe function
 * @component: ASoC component
 *
 * This function is called when ASoC has all the pieces it needs to
 * instantiate a sound driver.
 */
static int cs4270_probe(struct snd_soc_component *component)
{
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int ret;

	/* Disable auto-mute.  This feature appears to be buggy.  In some
	 * situations, auto-mute will not deactivate when it should, so we want
	 * this feature disabled by default.  An application (e.g. alsactl) can
	 * re-enabled it by using the controls.
	 */
	ret = snd_soc_component_update_bits(component, CS4270_MUTE, CS4270_MUTE_AUTO, 0);
	if (ret < 0) {
		dev_err(component->dev, "i2c write failed\n");
		return ret;
	}

	/* Disable automatic volume control.  The hardware enables, and it
	 * causes volume change commands to be delayed, sometimes until after
	 * playback has started.  An application (e.g. alsactl) can
	 * re-enabled it by using the controls.
	 */
	ret = snd_soc_component_update_bits(component, CS4270_TRANS,
		CS4270_TRANS_SOFT | CS4270_TRANS_ZERO, 0);
	if (ret < 0) {
		dev_err(component->dev, "i2c write failed\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs4270->supplies),
				    cs4270->supplies);

	return ret;
}

/**
 * cs4270_remove - ASoC remove function
 * @component: ASoC component
 *
 * This function is the counterpart to cs4270_probe().
 */
static void cs4270_remove(struct snd_soc_component *component)
{
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);

	regulator_bulk_disable(ARRAY_SIZE(cs4270->supplies), cs4270->supplies);
};

#ifdef CONFIG_PM

/* This suspend/resume implementation can handle both - a simple standby
 * where the codec remains powered, and a full suspend, where the voltage
 * domain the codec is connected to is teared down and/or any other hardware
 * reset condition is asserted.
 *
 * The codec's own power saving features are enabled in the suspend callback,
 * and all registers are written back to the hardware when resuming.
 */

static int cs4270_soc_suspend(struct snd_soc_component *component)
{
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int reg, ret;

	reg = snd_soc_component_read(component, CS4270_PWRCTL) | CS4270_PWRCTL_PDN_ALL;
	if (reg < 0)
		return reg;

	ret = snd_soc_component_write(component, CS4270_PWRCTL, reg);
	if (ret < 0)
		return ret;

	regulator_bulk_disable(ARRAY_SIZE(cs4270->supplies),
			       cs4270->supplies);

	return 0;
}

static int cs4270_soc_resume(struct snd_soc_component *component)
{
	struct cs4270_private *cs4270 = snd_soc_component_get_drvdata(component);
	int reg, ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(cs4270->supplies),
				    cs4270->supplies);
	if (ret != 0)
		return ret;

	/* In case the device was put to hard reset during sleep, we need to
	 * wait 500ns here before any I2C communication. */
	ndelay(500);

	/* first restore the entire register cache ... */
	regcache_sync(cs4270->regmap);

	/* ... then disable the power-down bits */
	reg = snd_soc_component_read(component, CS4270_PWRCTL);
	reg &= ~CS4270_PWRCTL_PDN_ALL;

	return snd_soc_component_write(component, CS4270_PWRCTL, reg);
}
#else
#define cs4270_soc_suspend	NULL
#define cs4270_soc_resume	NULL
#endif /* CONFIG_PM */

/*
 * ASoC codec driver structure
 */
static const struct snd_soc_component_driver soc_component_device_cs4270 = {
	.probe			= cs4270_probe,
	.remove			= cs4270_remove,
	.suspend		= cs4270_soc_suspend,
	.resume			= cs4270_soc_resume,
	.controls		= cs4270_snd_controls,
	.num_controls		= ARRAY_SIZE(cs4270_snd_controls),
	.dapm_widgets		= cs4270_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4270_dapm_widgets),
	.dapm_routes		= cs4270_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4270_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

/*
 * cs4270_of_match - the device tree bindings
 */
static const struct of_device_id cs4270_of_match[] = {
	{ .compatible = "cirrus,cs4270", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs4270_of_match);

static const struct regmap_config cs4270_regmap = {
	.reg_bits =		8,
	.val_bits =		8,
	.max_register =		CS4270_LASTREG,
	.reg_defaults =		cs4270_reg_defaults,
	.num_reg_defaults =	ARRAY_SIZE(cs4270_reg_defaults),
	.cache_type =		REGCACHE_RBTREE,
	.write_flag_mask =	CS4270_I2C_INCR,

	.readable_reg =		cs4270_reg_is_readable,
	.volatile_reg =		cs4270_reg_is_volatile,
};

/**
 * cs4270_i2c_remove - deinitialize the I2C interface of the CS4270
 * @i2c_client: the I2C client object
 *
 * This function puts the chip into low power mode when the i2c device
 * is removed.
 */
static int cs4270_i2c_remove(struct i2c_client *i2c_client)
{
	struct cs4270_private *cs4270 = i2c_get_clientdata(i2c_client);

	gpiod_set_value_cansleep(cs4270->reset_gpio, 0);

	return 0;
}

/**
 * cs4270_i2c_probe - initialize the I2C interface of the CS4270
 * @i2c_client: the I2C client object
 * @id: the I2C device ID (ignored)
 *
 * This function is called whenever the I2C subsystem finds a device that
 * matches the device ID given via a prior call to i2c_add_driver().
 */
static int cs4270_i2c_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id)
{
	struct cs4270_private *cs4270;
	unsigned int val;
	int ret, i;

	cs4270 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs4270_private),
			      GFP_KERNEL);
	if (!cs4270)
		return -ENOMEM;

	/* get the power supply regulators */
	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		cs4270->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(&i2c_client->dev,
				      ARRAY_SIZE(cs4270->supplies),
				      cs4270->supplies);
	if (ret < 0)
		return ret;

	/* reset the device */
	cs4270->reset_gpio = devm_gpiod_get_optional(&i2c_client->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(cs4270->reset_gpio)) {
		dev_dbg(&i2c_client->dev, "Error getting CS4270 reset GPIO\n");
		return PTR_ERR(cs4270->reset_gpio);
	}

	if (cs4270->reset_gpio) {
		dev_dbg(&i2c_client->dev, "Found reset GPIO\n");
		gpiod_set_value_cansleep(cs4270->reset_gpio, 1);
	}

	/* Sleep 500ns before i2c communications */
	ndelay(500);

	cs4270->regmap = devm_regmap_init_i2c(i2c_client, &cs4270_regmap);
	if (IS_ERR(cs4270->regmap))
		return PTR_ERR(cs4270->regmap);

	/* Verify that we have a CS4270 */
	ret = regmap_read(cs4270->regmap, CS4270_CHIPID, &val);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to read i2c at addr %X\n",
		       i2c_client->addr);
		return ret;
	}
	/* The top four bits of the chip ID should be 1100. */
	if ((val & 0xF0) != 0xC0) {
		dev_err(&i2c_client->dev, "device at addr %X is not a CS4270\n",
		       i2c_client->addr);
		return -ENODEV;
	}

	dev_info(&i2c_client->dev, "found device at i2c address %X\n",
		i2c_client->addr);
	dev_info(&i2c_client->dev, "hardware revision %X\n", val & 0xF);

	i2c_set_clientdata(i2c_client, cs4270);

	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_device_cs4270, &cs4270_dai, 1);
	return ret;
}

/*
 * cs4270_id - I2C device IDs supported by this driver
 */
static const struct i2c_device_id cs4270_id[] = {
	{"cs4270", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs4270_id);

/*
 * cs4270_i2c_driver - I2C device identification
 *
 * This structure tells the I2C subsystem how to identify and support a
 * given I2C device type.
 */
static struct i2c_driver cs4270_i2c_driver = {
	.driver = {
		.name = "cs4270",
		.of_match_table = cs4270_of_match,
	},
	.id_table = cs4270_id,
	.probe = cs4270_i2c_probe,
	.remove = cs4270_i2c_remove,
};

module_i2c_driver(cs4270_i2c_driver);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Cirrus Logic CS4270 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
