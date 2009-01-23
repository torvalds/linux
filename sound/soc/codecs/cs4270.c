/*
 * CS4270 ALSA SoC (ASoC) codec driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007 Freescale Semiconductor, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * This is an ASoC device driver for the Cirrus Logic CS4270 codec.
 *
 * Current features/limitations:
 *
 * 1) Software mode is supported.  Stand-alone mode is not supported.
 * 2) Only I2C is supported, not SPI
 * 3) Only Master mode is supported, not Slave.
 * 4) The machine driver's 'startup' function must call
 *    cs4270_set_dai_sysclk() with the value of MCLK.
 * 5) Only I2S and left-justified modes are supported
 * 6) Power management is not supported
 * 7) The only supported control is volume and hardware mute (if enabled)
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/i2c.h>

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

/* Bit masks for the CS4270 registers */
#define CS4270_CHIPID_ID	0xF0
#define CS4270_CHIPID_REV	0x0F
#define CS4270_PWRCTL_FREEZE	0x80
#define CS4270_PWRCTL_PDN_ADC	0x20
#define CS4270_PWRCTL_PDN_DAC	0x02
#define CS4270_PWRCTL_PDN	0x01
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
};

/*
 * Clock Ratio Selection for Master Mode with I2C enabled
 *
 * The data for this chart is taken from Table 5 of the CS4270 reference
 * manual.
 *
 * This table is used to determine how to program the Mode Control register.
 * It is also used by cs4270_set_dai_sysclk() to tell ALSA which sampling
 * rates the CS4270 currently supports.
 *
 * Each element in this array corresponds to the ratios in mclk_ratios[].
 * These two arrays need to be in sync.
 *
 * 'speed_mode' is the corresponding bit pattern to be written to the
 * MODE bits of the Mode Control Register
 *
 * 'mclk' is the corresponding bit pattern to be wirten to the MCLK bits of
 * the Mode Control Register.
 *
 * In situations where a single ratio is represented by multiple speed
 * modes, we favor the slowest speed.  E.g, for a ratio of 128, we pick
 * double-speed instead of quad-speed.  However, the CS4270 errata states
 * that Divide-By-1.5 can cause failures, so we avoid that mode where
 * possible.
 *
 * ERRATA: There is an errata for the CS4270 where divide-by-1.5 does not
 * work if VD = 3.3V.  If this effects you, select the
 * CONFIG_SND_SOC_CS4270_VD33_ERRATA Kconfig option, and the driver will
 * never select any sample rates that require divide-by-1.5.
 */
static struct {
	unsigned int ratio;
	u8 speed_mode;
	u8 mclk;
} cs4270_mode_ratios[] = {
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

/*
 * Determine the CS4270 samples rates.
 *
 * 'freq' is the input frequency to MCLK.  The other parameters are ignored.
 *
 * The value of MCLK is used to determine which sample rates are supported
 * by the CS4270.  The ratio of MCLK / Fs must be equal to one of nine
 * support values: 64, 96, 128, 192, 256, 384, 512, 768, and 1024.
 *
 * This function calculates the nine ratios and determines which ones match
 * a standard sample rate.  If there's a match, then it is added to the list
 * of support sample rates.
 *
 * This function must be called by the machine driver's 'startup' function,
 * otherwise the list of supported sample rates will not be available in
 * time for ALSA.
 *
 * Note that in stand-alone mode, the sample rate is determined by input
 * pins M0, M1, MDIV1, and MDIV2.  Also in stand-alone mode, divide-by-3
 * is not a programmable option.  However, divide-by-3 is not an available
 * option in stand-alone mode.  This cases two problems: a ratio of 768 is
 * not available (it requires divide-by-3) and B) ratios 192 and 384 can
 * only be selected with divide-by-1.5, but there is an errate that make
 * this selection difficult.
 *
 * In addition, there is no mechanism for communicating with the machine
 * driver what the input settings can be.  This would need to be implemented
 * for stand-alone mode to work.
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
		printk(KERN_ERR "cs4270: could not find a valid sample rate\n");
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

/*
 * Configure the codec for the selected audio format
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

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		cs4270->mode = format & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		printk(KERN_ERR "cs4270: invalid DAI format\n");
		ret = -EINVAL;
	}

	return ret;
}

/*
 * Pre-fill the CS4270 register cache.
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
		CS4270_FIRSTREG | 0x80, CS4270_NUMREGS, cache);

	if (length != CS4270_NUMREGS) {
		printk(KERN_ERR "cs4270: I2C read failure, addr=0x%x\n",
		       i2c_client->addr);
		return -EIO;
	}

	return 0;
}

/*
 * Read from the CS4270 register cache.
 *
 * This CS4270 registers are cached to avoid excessive I2C I/O operations.
 * After the initial read to pre-fill the cache, the CS4270 never updates
 * the register values, so we won't have a cache coherncy problem.
 */
static unsigned int cs4270_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if ((reg < CS4270_FIRSTREG) || (reg > CS4270_LASTREG))
		return -EIO;

	return cache[reg - CS4270_FIRSTREG];
}

/*
 * Write to a CS4270 register via the I2C bus.
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
			printk(KERN_ERR "cs4270: I2C write failed\n");
			return -EIO;
		}

		/* We've written to the hardware, so update the cache */
		cache[reg - CS4270_FIRSTREG] = value;
	}

	return 0;
}

/*
 * Program the CS4270 with the given hardware parameters.
 *
 * The .ops functions are used to provide board-specific data, like
 * input frequencies, to this driver.  This function takes that information,
 * combines it with the hardware parameters provided, and programs the
 * hardware accordingly.
 */
static int cs4270_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->codec;
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
		printk(KERN_ERR "cs4270: could not find matching ratio\n");
		return -EINVAL;
	}

	/* Freeze and power-down the codec */

	ret = snd_soc_write(codec, CS4270_PWRCTL, CS4270_PWRCTL_FREEZE |
			    CS4270_PWRCTL_PDN_ADC | CS4270_PWRCTL_PDN_DAC |
			    CS4270_PWRCTL_PDN);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: I2C write failed\n");
		return ret;
	}

	/* Program the mode control register */

	reg = snd_soc_read(codec, CS4270_MODE);
	reg &= ~(CS4270_MODE_SPEED_MASK | CS4270_MODE_DIV_MASK);
	reg |= cs4270_mode_ratios[i].speed_mode | cs4270_mode_ratios[i].mclk;

	ret = snd_soc_write(codec, CS4270_MODE, reg);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: I2C write failed\n");
		return ret;
	}

	/* Program the format register */

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
		printk(KERN_ERR "cs4270: unknown format\n");
		return -EINVAL;
	}

	ret = snd_soc_write(codec, CS4270_FORMAT, reg);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: I2C write failed\n");
		return ret;
	}

	/* Disable auto-mute.  This feature appears to be buggy, because in
	   some situations, auto-mute will not deactivate when it should. */

	reg = snd_soc_read(codec, CS4270_MUTE);
	reg &= ~CS4270_MUTE_AUTO;
	ret = snd_soc_write(codec, CS4270_MUTE, reg);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: I2C write failed\n");
		return ret;
	}

	/* Disable automatic volume control.  It's enabled by default, and
	 * it causes volume change commands to be delayed, sometimes until
	 * after playback has started.
	 */

	reg = cs4270_read_reg_cache(codec, CS4270_TRANS);
	reg &= ~(CS4270_TRANS_SOFT | CS4270_TRANS_ZERO);
	ret = cs4270_i2c_write(codec, CS4270_TRANS, reg);
	if (ret < 0) {
		printk(KERN_ERR "I2C write failed\n");
		return ret;
	}

	/* Thaw and power-up the codec */

	ret = snd_soc_write(codec, CS4270_PWRCTL, 0);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: I2C write failed\n");
		return ret;
	}

	return ret;
}

#ifdef CONFIG_SND_SOC_CS4270_HWMUTE
/*
 * Set the CS4270 external mute
 *
 * This function toggles the mute bits in the MUTE register.  The CS4270's
 * mute capability is intended for external muting circuitry, so if the
 * board does not have the MUTEA or MUTEB pins connected to such circuitry,
 * then this function will do nothing.
 */
static int cs4270_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int reg6;

	reg6 = snd_soc_read(codec, CS4270_MUTE);

	if (mute)
		reg6 |= CS4270_MUTE_ADC_A | CS4270_MUTE_ADC_B |
			CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B;
	else
		reg6 &= ~(CS4270_MUTE_ADC_A | CS4270_MUTE_ADC_B |
			  CS4270_MUTE_DAC_A | CS4270_MUTE_DAC_B);

	return snd_soc_write(codec, CS4270_MUTE, reg6);
}
#else
#define cs4270_mute NULL
#endif

/* A list of non-DAPM controls that the CS4270 supports */
static const struct snd_kcontrol_new cs4270_snd_controls[] = {
	SOC_DOUBLE_R("Master Playback Volume",
		CS4270_VOLA, CS4270_VOLB, 0, 0xFF, 1)
};

/*
 * Global variable to store socdev for i2c probe function.
 *
 * If struct i2c_driver had a private_data field, we wouldn't need to use
 * cs4270_socdec.  This is the only way to pass the socdev structure to
 * cs4270_i2c_probe().
 *
 * The real solution to cs4270_socdev is to create a mechanism
 * that maps I2C addresses to snd_soc_device structures.  Perhaps the
 * creation of the snd_soc_device object should be moved out of
 * cs4270_probe() and into cs4270_i2c_probe(), but that would make this
 * driver dependent on I2C.  The CS4270 supports "stand-alone" mode, whereby
 * the chip is *not* connected to the I2C bus, but is instead configured via
 * input pins.
 */
static struct snd_soc_device *cs4270_socdev;

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
	.ops = {
		.hw_params = cs4270_hw_params,
		.set_sysclk = cs4270_set_dai_sysclk,
		.set_fmt = cs4270_set_dai_fmt,
		.digital_mute = cs4270_mute,
	},
};
EXPORT_SYMBOL_GPL(cs4270_dai);

/*
 * Initialize the I2C interface of the CS4270
 *
 * This function is called for whenever the I2C subsystem finds a device
 * at a particular address.
 *
 * Note: snd_soc_new_pcms() must be called before this function can be called,
 * because of snd_ctl_add().
 */
static int cs4270_i2c_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = cs4270_socdev;
	struct snd_soc_codec *codec;
	struct cs4270_private *cs4270;
	int i;
	int ret = 0;

	/* Verify that we have a CS4270 */

	ret = i2c_smbus_read_byte_data(i2c_client, CS4270_CHIPID);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: failed to read I2C\n");
		return ret;
	}
	/* The top four bits of the chip ID should be 1100. */
	if ((ret & 0xF0) != 0xC0) {
		printk(KERN_ERR "cs4270: device at addr %X is not a CS4270\n",
		       i2c_client->addr);
		return -ENODEV;
	}

	printk(KERN_INFO "cs4270: found device at I2C address %X\n",
		i2c_client->addr);
	printk(KERN_INFO "cs4270: hardware revision %X\n", ret & 0xF);

	/* Allocate enough space for the snd_soc_codec structure
	   and our private data together. */
	cs4270 = kzalloc(sizeof(struct cs4270_private), GFP_KERNEL);
	if (!cs4270) {
		printk(KERN_ERR "cs4270: Could not allocate codec structure\n");
		return -ENOMEM;
	}
	codec = &cs4270->codec;
	socdev->codec = codec;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

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
		printk(KERN_ERR "cs4270: failed to fill register cache\n");
		goto error_free_codec;
	}

	/* Register PCMs */

	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: failed to create PCMs\n");
		goto error_free_codec;
	}

	/* Add the non-DAPM controls */

	for (i = 0; i < ARRAY_SIZE(cs4270_snd_controls); i++) {
		struct snd_kcontrol *kctrl;

		kctrl = snd_soc_cnew(&cs4270_snd_controls[i], codec, NULL);
		if (!kctrl) {
			printk(KERN_ERR "cs4270: error creating control '%s'\n",
			       cs4270_snd_controls[i].name);
			ret = -ENOMEM;
			goto error_free_pcms;
		}

		ret = snd_ctl_add(codec->card, kctrl);
		if (ret < 0) {
			printk(KERN_ERR "cs4270: error adding control '%s'\n",
			       cs4270_snd_controls[i].name);
			goto error_free_pcms;
		}
	}

	/* Initialize the SOC device */

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "cs4270: failed to register card\n");
		goto error_free_pcms;;
	}

	i2c_set_clientdata(i2c_client, socdev);

	return 0;

error_free_pcms:
	snd_soc_free_pcms(socdev);

error_free_codec:
	kfree(cs4270);

	return ret;
}

static int cs4270_i2c_remove(struct i2c_client *i2c_client)
{
	struct snd_soc_device *socdev = i2c_get_clientdata(i2c_client);
	struct snd_soc_codec *codec = socdev->codec;
	struct cs4270_private *cs4270 = codec->private_data;

	snd_soc_free_pcms(socdev);
	kfree(cs4270);

	return 0;
}

static struct i2c_device_id cs4270_id[] = {
	{"cs4270", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs4270_id);

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
 * ASoC probe function
 *
 * This function is called when the machine driver calls
 * platform_device_add().
 */
static int cs4270_probe(struct platform_device *pdev)
{
	cs4270_socdev = platform_get_drvdata(pdev);;

	return i2c_add_driver(&cs4270_i2c_driver);
}

static int cs4270_remove(struct platform_device *pdev)
{
	i2c_del_driver(&cs4270_i2c_driver);

	return 0;
}

/*
 * ASoC codec device structure
 *
 * Assign this variable to the codec_dev field of the machine driver's
 * snd_soc_device structure.
 */
struct snd_soc_codec_device soc_codec_device_cs4270 = {
	.probe = 	cs4270_probe,
	.remove = 	cs4270_remove
};
EXPORT_SYMBOL_GPL(soc_codec_device_cs4270);

static int __init cs4270_init(void)
{
	printk(KERN_INFO "Cirrus Logic CS4270 ALSA SoC Codec Driver\n");

	return snd_soc_register_dai(&cs4270_dai);
}
module_init(cs4270_init);

static void __exit cs4270_exit(void)
{
	snd_soc_unregister_dai(&cs4270_dai);
}
module_exit(cs4270_exit);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Cirrus Logic CS4270 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
