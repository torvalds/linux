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
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "cs4270.h"

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

/* Private data for the CS4270 */
struct cs4270_private {
	struct snd_soc_codec codec;
	u8 reg_cache[CS4270_NUMREGS];
	unsigned int mclk; /* Input frequency of the MCLK pin */
	unsigned int mode; /* The mode (I2S or left-justified) */
	unsigned int slave_mode;
	unsigned int manual_mute;
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
 */
static int cs4270_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs4270_private *cs4270 = codec->private_data;
	unsigned int rates = 0;
	unsigned int rate_min = -1;
	unsigned int rate_max = 0;
	unsigned int i;

	cs4270->mclk = freq;

	for (i = 0; i < NUM_MCLK_RATIOS; i++) {
		unsigned int rate = freq / cs4270_mode_ratios[i].ratio;
		rates |= snd_pcm_rate_to_rate_bit(rate);
		if (rate < rate_min)
			rate_min = rate;
		if (rate > rate_max)
			rate_max = rate;
	}
	/* FIXME: soc should support a rate list */
	rates &= ~SNDRV_PCM_RATE_KNOT;

	if (!rates) {
		dev_err(codec->dev, "could not find a valid sample rate\n");
		return -EINVAL;
	}

	codec_dai->playback.rates = rates;
	codec_dai->playback.rate_min = rate_min;
	codec_dai->playback.rate_max = rate_max;

	codec_dai->capture.rates = rates;
	codec_dai->capture.rate_min = rate_min;
	codec_dai->capture.rate_max = rate_max;

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
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs4270_private *cs4270 = codec->private_data;
	int ret = 0;

	/* set DAI format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		cs4270->mode = format & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(codec->dev, "invalid dai format\n");
		ret = -EINVAL;
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
		ret = -EINVAL;
	}

	return ret;
}

/**
 * cs4270_fill_cache - pre-fill the CS4270 register cache.
 * @codec: the codec for this CS4270
 *
 * This function fills in the CS4270 register cache by reading the register
 * values from the hardware.
 *
 * This CS4270 registers are cached to avoid excessive I2C I/O operations.
 * After the initial read to pre-fill the cache, the CS4270 never updates
 * the register values, so we won't have a cache coherency problem.
 *
 * We use the auto-increment feature of the CS4270 to read all registers in
 * one shot.
 */
static int cs4270_fill_cache(struct snd_soc_codec *codec)
{
	u8 *cache = codec->reg_cache;
	struct i2c_client *i2c_client = codec->control_data;
	s32 length;

	length = i2c_smbus_read_i2c_block_data(i2c_client,
		CS4270_FIRSTREG | CS4270_I2C_INCR, CS4270_NUMREGS, cache);

	if (length != CS4270_NUMREGS) {
		dev_err(codec->dev, "i2c read failure, addr=0x%x\n",
		       i2c_client->addr);
		return -EIO;
	}

	return 0;
}

/**
 * cs4270_read_reg_cache - read from the CS4270 register cache.
 * @codec: the codec for this CS4270
 * @reg: the register to read
 *
 * This function returns the value for a given register.  It reads only from
 * the register cache, not the hardware itself.
 *
 * This CS4270 registers are cached to avoid excessive I2C I/O operations.
 * After the initial read to pre-fill the cache, the CS4270 never updates
 * the register values, so we won't have a cache coherency problem.
 */
static unsigned int cs4270_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if ((reg < CS4270_FIRSTREG) || (reg > CS4270_LASTREG))
		return -EIO;

	return cache[reg - CS4270_FIRSTREG];
}

/**
 * cs4270_i2c_write - write to a CS4270 register via the I2C bus.
 * @codec: the codec for this CS4270
 * @reg: the register to write
 * @value: the value to write to the register
 *
 * This function writes the given value to the given CS4270 register, and
 * also updates the register cache.
 *
 * Note that we don't use the hw_write function pointer of snd_soc_codec.
 * That's because it's too clunky: the hw_write_t prototype does not match
 * i2c_smbus_write_byte_data(), and it's just another layer of overhead.
 */
static int cs4270_i2c_write(struct snd_soc_codec *codec, unsigned int reg,
			    unsigned int value)
{
	u8 *cache = codec->reg_cache;

	if ((reg < CS4270_FIRSTREG) || (reg > CS4270_LASTREG))
		return -EIO;

	/* Only perform an I2C operation if the new value is different */
	if (cache[reg - CS4270_FIRSTREG] != value) {
		struct i2c_client *client = codec->control_data;
		if (i2c_smbus_write_byte_data(client, reg, value)) {
			dev_err(codec->dev, "i2c write failed\n");
			return -EIO;
		}

		/* We've written to the hardware, so update the cache */
		cache[reg - CS4270_FIRSTREG] = value;
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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct cs4270_private *cs4270 = codec->private_data;
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
		dev_err(codec->dev, "could not find matching ratio\n");
		return -EINVAL;
	}

	/* Set the sample rate */

	reg = snd_soc_read(codec, CS4270_MODE);
	reg &= ~(CS4270_MODE_SPEED_MASK | CS4270_MODE_DIV_MASK);
	reg |= cs4270_mode_ratios[i].mclk;

	if (cs4270->slave_mode)
		reg |= CS4270_MODE_SLAVE;
	else
		reg |= cs4270_mode_ratios[i].speed_mode;

	ret = snd_soc_write(codec, CS4270_MODE, reg);
	if (ret < 0) {
		dev_err(codec->dev, "i2c write failed\n");
		return ret;
	}

	/* Set the DAI format */

	reg = snd_soc_read(codec, CS4270_FORMAT);
	reg &= ~(CS4270_FORMAT_DAC_MASK | CS4270_FORMAT_ADC_MASK);

	switch (cs4270->mode) {
	case SND_SOC_DAIFMT_I2S:
		reg |= CS4270_FORMAT_DAC_I2S | CS4270_FORMAT_ADC_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg |= CS4270_FORMAT_DAC_LJ | CS4270_FORMAT_ADC_LJ;
		break;
	default:
		dev_err(codec->dev, "unknown dai format\n");
		return -EINVAL;
	}

	ret = snd_soc_write(codec, CS4270_FORMAT, reg);
	if (ret < 0) {
		dev_err(codec->dev, "i2c write failed\n");
		return ret;
	}

	return ret;
}

/**
 * cs4270_dai_mute - enable/disable the CS4270 external mute
 * @dai: the SOC DAI
 * @mute: 0 = disable mute, 1 = enable mute
 *
 * This function toggles the mute bits in the MUTE register.  The CS4270's
 * mute capability is intended for external muting circuitry, so if the
 * board does not have the MUTEA or MUTEB pins connected to such circuitry,
 * then this function will do nothing.
 */
static int cs4270_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs4270_private *cs4270 = codec->private_data;
	int reg6;

	reg6 = snd_soc_read(codec, CS4270_MUTE);

	if (mute)
		reg6 |= CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B;
	else {
		reg6 &= ~(CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B);
		reg6 |= cs4270->manual_mute;
	}

	return snd_soc_write(codec, CS4270_MUTE, reg6);
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
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs4270_private *cs4270 = codec->private_data;
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

/*
 * cs4270_codec - global variable to store codec for the ASoC probe function
 *
 * If struct i2c_driver had a private_data field, we wouldn't need to use
 * cs4270_codec.  This is the only way to pass the codec structure from
 * cs4270_i2c_probe() to cs4270_probe().  Unfortunately, there is no good
 * way to synchronize these two functions.  cs4270_i2c_probe() can be called
 * multiple times before cs4270_probe() is called even once.  So for now, we
 * also only allow cs4270_i2c_probe() to be run once.  That means that we do
 * not support more than one cs4270 device in the system, at least for now.
 */
static struct snd_soc_codec *cs4270_codec;

static struct snd_soc_dai_ops cs4270_dai_ops = {
	.hw_params	= cs4270_hw_params,
	.set_sysclk	= cs4270_set_dai_sysclk,
	.set_fmt	= cs4270_set_dai_fmt,
	.digital_mute	= cs4270_dai_mute,
};

struct snd_soc_dai cs4270_dai = {
	.name = "cs4270",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = 0,
		.formats = CS4270_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = 0,
		.formats = CS4270_FORMATS,
	},
	.ops = &cs4270_dai_ops,
};
EXPORT_SYMBOL_GPL(cs4270_dai);

/**
 * cs4270_probe - ASoC probe function
 * @pdev: platform device
 *
 * This function is called when ASoC has all the pieces it needs to
 * instantiate a sound driver.
 */
static int cs4270_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = cs4270_codec;
	int ret;

	/* Connect the codec to the socdev.  snd_soc_new_pcms() needs this. */
	socdev->card->codec = codec;

	/* Register PCMs */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms\n");
		return ret;
	}

	/* Add the non-DAPM controls */
	ret = snd_soc_add_controls(codec, cs4270_snd_controls,
				ARRAY_SIZE(cs4270_snd_controls));
	if (ret < 0) {
		dev_err(codec->dev, "failed to add controls\n");
		goto error_free_pcms;
	}

	return 0;

error_free_pcms:
	snd_soc_free_pcms(socdev);

	return ret;
}

/**
 * cs4270_remove - ASoC remove function
 * @pdev: platform device
 *
 * This function is the counterpart to cs4270_probe().
 */
static int cs4270_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);

	return 0;
};

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
	struct snd_soc_codec *codec;
	struct cs4270_private *cs4270;
	unsigned int reg;
	int ret;

	/* For now, we only support one cs4270 device in the system.  See the
	 * comment for cs4270_codec.
	 */
	if (cs4270_codec) {
		dev_err(&i2c_client->dev, "ignoring CS4270 at addr %X\n",
		       i2c_client->addr);
		dev_err(&i2c_client->dev, "only one per board allowed\n");
		/* Should we return something other than ENODEV here? */
		return -ENODEV;
	}

	/* Verify that we have a CS4270 */

	ret = i2c_smbus_read_byte_data(i2c_client, CS4270_CHIPID);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to read i2c at addr %X\n",
		       i2c_client->addr);
		return ret;
	}
	/* The top four bits of the chip ID should be 1100. */
	if ((ret & 0xF0) != 0xC0) {
		dev_err(&i2c_client->dev, "device at addr %X is not a CS4270\n",
		       i2c_client->addr);
		return -ENODEV;
	}

	dev_info(&i2c_client->dev, "found device at i2c address %X\n",
		i2c_client->addr);
	dev_info(&i2c_client->dev, "hardware revision %X\n", ret & 0xF);

	/* Allocate enough space for the snd_soc_codec structure
	   and our private data together. */
	cs4270 = kzalloc(sizeof(struct cs4270_private), GFP_KERNEL);
	if (!cs4270) {
		dev_err(&i2c_client->dev, "could not allocate codec\n");
		return -ENOMEM;
	}
	codec = &cs4270->codec;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->dev = &i2c_client->dev;
	codec->name = "CS4270";
	codec->owner = THIS_MODULE;
	codec->dai = &cs4270_dai;
	codec->num_dai = 1;
	codec->private_data = cs4270;
	codec->control_data = i2c_client;
	codec->read = cs4270_read_reg_cache;
	codec->write = cs4270_i2c_write;
	codec->reg_cache = cs4270->reg_cache;
	codec->reg_cache_size = CS4270_NUMREGS;

	/* The I2C interface is set up, so pre-fill our register cache */

	ret = cs4270_fill_cache(codec);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to fill register cache\n");
		goto error_free_codec;
	}

	/* Disable auto-mute.  This feature appears to be buggy.  In some
	 * situations, auto-mute will not deactivate when it should, so we want
	 * this feature disabled by default.  An application (e.g. alsactl) can
	 * re-enabled it by using the controls.
	 */

	reg = cs4270_read_reg_cache(codec, CS4270_MUTE);
	reg &= ~CS4270_MUTE_AUTO;
	ret = cs4270_i2c_write(codec, CS4270_MUTE, reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "i2c write failed\n");
		return ret;
	}

	/* Disable automatic volume control.  The hardware enables, and it
	 * causes volume change commands to be delayed, sometimes until after
	 * playback has started.  An application (e.g. alsactl) can
	 * re-enabled it by using the controls.
	 */

	reg = cs4270_read_reg_cache(codec, CS4270_TRANS);
	reg &= ~(CS4270_TRANS_SOFT | CS4270_TRANS_ZERO);
	ret = cs4270_i2c_write(codec, CS4270_TRANS, reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "i2c write failed\n");
		return ret;
	}

	/* Initialize the DAI. Normally, we'd prefer to have a kmalloc'd DAI
	 * structure for each CS4270 device, but the machine driver needs to
	 * have a pointer to the DAI structure, so for now it must be a global
	 * variable.
	 */
	cs4270_dai.dev = &i2c_client->dev;

	/* Register the DAI.  If all the other ASoC driver have already
	 * registered, then this will call our probe function, so
	 * cs4270_codec needs to be ready.
	 */
	cs4270_codec = codec;
	ret = snd_soc_register_dai(&cs4270_dai);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to register DAIe\n");
		goto error_free_codec;
	}

	i2c_set_clientdata(i2c_client, cs4270);

	return 0;

error_free_codec:
	kfree(cs4270);
	cs4270_codec = NULL;
	cs4270_dai.dev = NULL;

	return ret;
}

/**
 * cs4270_i2c_remove - remove an I2C device
 * @i2c_client: the I2C client object
 *
 * This function is the counterpart to cs4270_i2c_probe().
 */
static int cs4270_i2c_remove(struct i2c_client *i2c_client)
{
	struct cs4270_private *cs4270 = i2c_get_clientdata(i2c_client);

	kfree(cs4270);
	cs4270_codec = NULL;
	cs4270_dai.dev = NULL;

	return 0;
}

/*
 * cs4270_id - I2C device IDs supported by this driver
 */
static struct i2c_device_id cs4270_id[] = {
	{"cs4270", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs4270_id);

#ifdef CONFIG_PM

/* This suspend/resume implementation can handle both - a simple standby
 * where the codec remains powered, and a full suspend, where the voltage
 * domain the codec is connected to is teared down and/or any other hardware
 * reset condition is asserted.
 *
 * The codec's own power saving features are enabled in the suspend callback,
 * and all registers are written back to the hardware when resuming.
 */

static int cs4270_soc_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct snd_soc_codec *codec = cs4270_codec;
	int reg = snd_soc_read(codec, CS4270_PWRCTL) | CS4270_PWRCTL_PDN_ALL;

	return snd_soc_write(codec, CS4270_PWRCTL, reg);
}

static int cs4270_soc_resume(struct platform_device *pdev)
{
	struct snd_soc_codec *codec = cs4270_codec;
	struct i2c_client *i2c_client = codec->control_data;
	int reg;

	/* In case the device was put to hard reset during sleep, we need to
	 * wait 500ns here before any I2C communication. */
	ndelay(500);

	/* first restore the entire register cache ... */
	for (reg = CS4270_FIRSTREG; reg <= CS4270_LASTREG; reg++) {
		u8 val = snd_soc_read(codec, reg);

		if (i2c_smbus_write_byte_data(i2c_client, reg, val)) {
			dev_err(codec->dev, "i2c write failed\n");
			return -EIO;
		}
	}

	/* ... then disable the power-down bits */
	reg = snd_soc_read(codec, CS4270_PWRCTL);
	reg &= ~CS4270_PWRCTL_PDN_ALL;

	return snd_soc_write(codec, CS4270_PWRCTL, reg);
}
#else
#define cs4270_soc_suspend	NULL
#define cs4270_soc_resume	NULL
#endif /* CONFIG_PM */

/*
 * cs4270_i2c_driver - I2C device identification
 *
 * This structure tells the I2C subsystem how to identify and support a
 * given I2C device type.
 */
static struct i2c_driver cs4270_i2c_driver = {
	.driver = {
		.name = "cs4270",
		.owner = THIS_MODULE,
	},
	.id_table = cs4270_id,
	.probe = cs4270_i2c_probe,
	.remove = cs4270_i2c_remove,
};

/*
 * ASoC codec device structure
 *
 * Assign this variable to the codec_dev field of the machine driver's
 * snd_soc_device structure.
 */
struct snd_soc_codec_device soc_codec_device_cs4270 = {
	.probe = 	cs4270_probe,
	.remove = 	cs4270_remove,
	.suspend =	cs4270_soc_suspend,
	.resume =	cs4270_soc_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_device_cs4270);

static int __init cs4270_init(void)
{
	pr_info("Cirrus Logic CS4270 ALSA SoC Codec Driver\n");

	return i2c_add_driver(&cs4270_i2c_driver);
}
module_init(cs4270_init);

static void __exit cs4270_exit(void)
{
	i2c_del_driver(&cs4270_i2c_driver);
}
module_exit(cs4270_exit);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Cirrus Logic CS4270 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
