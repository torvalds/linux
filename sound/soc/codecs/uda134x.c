/*
 * uda134x.c  --  UDA134X ALSA SoC Codec driver
 *
 * Modifications by Christian Pellegrin <chripell@evolware.org>
 *
 * Copyright 2007 Dension Audio Systems Ltd.
 * Author: Zoltan Devai
 *
 * Based on the WM87xx drivers by Liam Girdwood and Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include <sound/uda134x.h>
#include <sound/l3.h>

#include "uda134x.h"


#define UDA134X_RATES SNDRV_PCM_RATE_8000_48000
#define UDA134X_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

struct uda134x_priv {
	int sysclk;
	int dai_fmt;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

/* In-data addresses are hard-coded into the reg-cache values */
static const char uda134x_reg[UDA134X_REGS_NUM] = {
	/* Extended address registers */
	0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* Status, data regs */
	0x00, 0x83, 0x00, 0x40, 0x80, 0xC0, 0x00,
};

/*
 * The codec has no support for reading its registers except for peak level...
 */
static inline unsigned int uda134x_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if (reg >= UDA134X_REGS_NUM)
		return -1;
	return cache[reg];
}

/*
 * Write the register cache
 */
static inline void uda134x_write_reg_cache(struct snd_soc_codec *codec,
	u8 reg, unsigned int value)
{
	u8 *cache = codec->reg_cache;

	if (reg >= UDA134X_REGS_NUM)
		return;
	cache[reg] = value;
}

/*
 * Write to the uda134x registers
 *
 */
static int uda134x_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	int ret;
	u8 addr;
	u8 data = value;
	struct uda134x_platform_data *pd = codec->control_data;

	pr_debug("%s reg: %02X, value:%02X\n", __func__, reg, value);

	if (reg >= UDA134X_REGS_NUM) {
		printk(KERN_ERR "%s unknown register: reg: %u",
		       __func__, reg);
		return -EINVAL;
	}

	uda134x_write_reg_cache(codec, reg, value);

	switch (reg) {
	case UDA134X_STATUS0:
	case UDA134X_STATUS1:
		addr = UDA134X_STATUS_ADDR;
		break;
	case UDA134X_DATA000:
	case UDA134X_DATA001:
	case UDA134X_DATA010:
	case UDA134X_DATA011:
		addr = UDA134X_DATA0_ADDR;
		break;
	case UDA134X_DATA1:
		addr = UDA134X_DATA1_ADDR;
		break;
	default:
		/* It's an extended address register */
		addr =  (reg | UDA134X_EXTADDR_PREFIX);

		ret = l3_write(&pd->l3,
			       UDA134X_DATA0_ADDR, &addr, 1);
		if (ret != 1)
			return -EIO;

		addr = UDA134X_DATA0_ADDR;
		data = (value | UDA134X_EXTDATA_PREFIX);
		break;
	}

	ret = l3_write(&pd->l3,
		       addr, &data, 1);
	if (ret != 1)
		return -EIO;

	return 0;
}

static inline void uda134x_reset(struct snd_soc_codec *codec)
{
	u8 reset_reg = uda134x_read_reg_cache(codec, UDA134X_STATUS0);
	uda134x_write(codec, UDA134X_STATUS0, reset_reg | (1<<6));
	msleep(1);
	uda134x_write(codec, UDA134X_STATUS0, reset_reg & ~(1<<6));
}

static int uda134x_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 mute_reg = uda134x_read_reg_cache(codec, UDA134X_DATA010);

	pr_debug("%s mute: %d\n", __func__, mute);

	if (mute)
		mute_reg |= (1<<2);
	else
		mute_reg &= ~(1<<2);

	uda134x_write(codec, UDA134X_DATA010, mute_reg);

	return 0;
}

static int uda134x_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);
	struct snd_pcm_runtime *master_runtime;

	if (uda134x->master_substream) {
		master_runtime = uda134x->master_substream->runtime;

		pr_debug("%s constraining to %d bits at %d\n", __func__,
			 master_runtime->sample_bits,
			 master_runtime->rate);

		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_RATE,
					     master_runtime->rate,
					     master_runtime->rate);

		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					     master_runtime->sample_bits,
					     master_runtime->sample_bits);

		uda134x->slave_substream = substream;
	} else
		uda134x->master_substream = substream;

	return 0;
}

static void uda134x_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);

	if (uda134x->master_substream == substream)
		uda134x->master_substream = uda134x->slave_substream;

	uda134x->slave_substream = NULL;
}

static int uda134x_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);
	u8 hw_params;

	if (substream == uda134x->slave_substream) {
		pr_debug("%s ignoring hw_params for slave substream\n",
			 __func__);
		return 0;
	}

	hw_params = uda134x_read_reg_cache(codec, UDA134X_STATUS0);
	hw_params &= STATUS0_SYSCLK_MASK;
	hw_params &= STATUS0_DAIFMT_MASK;

	pr_debug("%s sysclk: %d, rate:%d\n", __func__,
		 uda134x->sysclk, params_rate(params));

	/* set SYSCLK / fs ratio */
	switch (uda134x->sysclk / params_rate(params)) {
	case 512:
		break;
	case 384:
		hw_params |= (1<<4);
		break;
	case 256:
		hw_params |= (1<<5);
		break;
	default:
		printk(KERN_ERR "%s unsupported fs\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s dai_fmt: %d, params_format:%d\n", __func__,
		 uda134x->dai_fmt, params_format(params));

	/* set DAI format and word length */
	switch (uda134x->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			hw_params |= (1<<1);
			break;
		case SNDRV_PCM_FORMAT_S18_3LE:
			hw_params |= (1<<2);
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			hw_params |= ((1<<2) | (1<<1));
			break;
		default:
			printk(KERN_ERR "%s unsupported format (right)\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		hw_params |= (1<<3);
		break;
	default:
		printk(KERN_ERR "%s unsupported format\n", __func__);
		return -EINVAL;
	}

	uda134x_write(codec, UDA134X_STATUS0, hw_params);

	return 0;
}

static int uda134x_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s clk_id: %d, freq: %u, dir: %d\n", __func__,
		 clk_id, freq, dir);

	/* Anything between 256fs*8Khz and 512fs*48Khz should be acceptable
	   because the codec is slave. Of course limitations of the clock
	   master (the IIS controller) apply.
	   We'll error out on set_hw_params if it's not OK */
	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		uda134x->sysclk = freq;
		return 0;
	}

	printk(KERN_ERR "%s unsupported sysclk\n", __func__);
	return -EINVAL;
}

static int uda134x_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);

	pr_debug("%s fmt: %08X\n", __func__, fmt);

	/* codec supports only full slave mode */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		printk(KERN_ERR "%s unsupported slave mode\n", __func__);
		return -EINVAL;
	}

	/* no support for clock inversion */
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		printk(KERN_ERR "%s unsupported clock inversion\n", __func__);
		return -EINVAL;
	}

	/* We can't setup DAI format here as it depends on the word bit num */
	/* so let's just store the value for later */
	uda134x->dai_fmt = fmt;

	return 0;
}

static int uda134x_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	u8 reg;
	struct uda134x_platform_data *pd = codec->control_data;
	int i;
	u8 *cache = codec->reg_cache;

	pr_debug("%s bias level %d\n", __func__, level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* ADC, DAC on */
		switch (pd->model) {
		case UDA134X_UDA1340:
		case UDA134X_UDA1344:
		case UDA134X_UDA1345:
			reg = uda134x_read_reg_cache(codec, UDA134X_DATA011);
			uda134x_write(codec, UDA134X_DATA011, reg | 0x03);
			break;
		case UDA134X_UDA1341:
			reg = uda134x_read_reg_cache(codec, UDA134X_STATUS1);
			uda134x_write(codec, UDA134X_STATUS1, reg | 0x03);
			break;
		default:
			printk(KERN_ERR "UDA134X SoC codec: "
			       "unsupported model %d\n", pd->model);
			return -EINVAL;
		}
		break;
	case SND_SOC_BIAS_PREPARE:
		/* power on */
		if (pd->power) {
			pd->power(1);
			/* Sync reg_cache with the hardware */
			for (i = 0; i < ARRAY_SIZE(uda134x_reg); i++)
				codec->driver->write(codec, i, *cache++);
		}
		break;
	case SND_SOC_BIAS_STANDBY:
		/* ADC, DAC power off */
		switch (pd->model) {
		case UDA134X_UDA1340:
		case UDA134X_UDA1344:
		case UDA134X_UDA1345:
			reg = uda134x_read_reg_cache(codec, UDA134X_DATA011);
			uda134x_write(codec, UDA134X_DATA011, reg & ~(0x03));
			break;
		case UDA134X_UDA1341:
			reg = uda134x_read_reg_cache(codec, UDA134X_STATUS1);
			uda134x_write(codec, UDA134X_STATUS1, reg & ~(0x03));
			break;
		default:
			printk(KERN_ERR "UDA134X SoC codec: "
			       "unsupported model %d\n", pd->model);
			return -EINVAL;
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* power off */
		if (pd->power)
			pd->power(0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

static const char *uda134x_dsp_setting[] = {"Flat", "Minimum1",
					    "Minimum2", "Maximum"};
static const char *uda134x_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static const char *uda134x_mixmode[] = {"Differential", "Analog1",
					"Analog2", "Both"};

static const struct soc_enum uda134x_mixer_enum[] = {
SOC_ENUM_SINGLE(UDA134X_DATA010, 0, 0x04, uda134x_dsp_setting),
SOC_ENUM_SINGLE(UDA134X_DATA010, 3, 0x04, uda134x_deemph),
SOC_ENUM_SINGLE(UDA134X_EA010, 0, 0x04, uda134x_mixmode),
};

static const struct snd_kcontrol_new uda1341_snd_controls[] = {
SOC_SINGLE("Master Playback Volume", UDA134X_DATA000, 0, 0x3F, 1),
SOC_SINGLE("Capture Volume", UDA134X_EA010, 2, 0x07, 0),
SOC_SINGLE("Analog1 Volume", UDA134X_EA000, 0, 0x1F, 1),
SOC_SINGLE("Analog2 Volume", UDA134X_EA001, 0, 0x1F, 1),

SOC_SINGLE("Mic Sensitivity", UDA134X_EA010, 2, 7, 0),
SOC_SINGLE("Mic Volume", UDA134X_EA101, 0, 0x1F, 0),

SOC_SINGLE("Tone Control - Bass", UDA134X_DATA001, 2, 0xF, 0),
SOC_SINGLE("Tone Control - Treble", UDA134X_DATA001, 0, 3, 0),

SOC_ENUM("Sound Processing Filter", uda134x_mixer_enum[0]),
SOC_ENUM("PCM Playback De-emphasis", uda134x_mixer_enum[1]),
SOC_ENUM("Input Mux", uda134x_mixer_enum[2]),

SOC_SINGLE("AGC Switch", UDA134X_EA100, 4, 1, 0),
SOC_SINGLE("AGC Target Volume", UDA134X_EA110, 0, 0x03, 1),
SOC_SINGLE("AGC Timing", UDA134X_EA110, 2, 0x07, 0),

SOC_SINGLE("DAC +6dB Switch", UDA134X_STATUS1, 6, 1, 0),
SOC_SINGLE("ADC +6dB Switch", UDA134X_STATUS1, 5, 1, 0),
SOC_SINGLE("ADC Polarity Switch", UDA134X_STATUS1, 4, 1, 0),
SOC_SINGLE("DAC Polarity Switch", UDA134X_STATUS1, 3, 1, 0),
SOC_SINGLE("Double Speed Playback Switch", UDA134X_STATUS1, 2, 1, 0),
SOC_SINGLE("DC Filter Enable Switch", UDA134X_STATUS0, 0, 1, 0),
};

static const struct snd_kcontrol_new uda1340_snd_controls[] = {
SOC_SINGLE("Master Playback Volume", UDA134X_DATA000, 0, 0x3F, 1),

SOC_SINGLE("Tone Control - Bass", UDA134X_DATA001, 2, 0xF, 0),
SOC_SINGLE("Tone Control - Treble", UDA134X_DATA001, 0, 3, 0),

SOC_ENUM("Sound Processing Filter", uda134x_mixer_enum[0]),
SOC_ENUM("PCM Playback De-emphasis", uda134x_mixer_enum[1]),

SOC_SINGLE("DC Filter Enable Switch", UDA134X_STATUS0, 0, 1, 0),
};

static const struct snd_kcontrol_new uda1345_snd_controls[] = {
SOC_SINGLE("Master Playback Volume", UDA134X_DATA000, 0, 0x3F, 1),

SOC_ENUM("PCM Playback De-emphasis", uda134x_mixer_enum[1]),

SOC_SINGLE("DC Filter Enable Switch", UDA134X_STATUS0, 0, 1, 0),
};

static struct snd_soc_dai_ops uda134x_dai_ops = {
	.startup	= uda134x_startup,
	.shutdown	= uda134x_shutdown,
	.hw_params	= uda134x_hw_params,
	.digital_mute	= uda134x_mute,
	.set_sysclk	= uda134x_set_dai_sysclk,
	.set_fmt	= uda134x_set_dai_fmt,
};

static struct snd_soc_dai_driver uda134x_dai = {
	.name = "uda134x-hifi",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,
		.formats = UDA134X_FORMATS,
	},
	/* capture capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,
		.formats = UDA134X_FORMATS,
	},
	/* pcm operations */
	.ops = &uda134x_dai_ops,
};

static int uda134x_soc_probe(struct snd_soc_codec *codec)
{
	struct uda134x_priv *uda134x;
	struct uda134x_platform_data *pd = dev_get_drvdata(codec->card->dev);
	int ret;

	printk(KERN_INFO "UDA134X SoC Audio Codec\n");

	if (!pd) {
		printk(KERN_ERR "UDA134X SoC codec: "
		       "missing L3 bitbang function\n");
		return -ENODEV;
	}

	switch (pd->model) {
	case UDA134X_UDA1340:
	case UDA134X_UDA1341:
	case UDA134X_UDA1344:
	case UDA134X_UDA1345:
		break;
	default:
		printk(KERN_ERR "UDA134X SoC codec: "
		       "unsupported model %d\n",
			pd->model);
		return -EINVAL;
	}

	uda134x = kzalloc(sizeof(struct uda134x_priv), GFP_KERNEL);
	if (uda134x == NULL)
		return -ENOMEM;
	snd_soc_codec_set_drvdata(codec, uda134x);

	codec->control_data = pd;

	if (pd->power)
		pd->power(1);

	uda134x_reset(codec);

	if (pd->is_powered_on_standby)
		uda134x_set_bias_level(codec, SND_SOC_BIAS_ON);
	else
		uda134x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	switch (pd->model) {
	case UDA134X_UDA1340:
	case UDA134X_UDA1344:
		ret = snd_soc_add_controls(codec, uda1340_snd_controls,
					ARRAY_SIZE(uda1340_snd_controls));
	break;
	case UDA134X_UDA1341:
		ret = snd_soc_add_controls(codec, uda1341_snd_controls,
					ARRAY_SIZE(uda1341_snd_controls));
	break;
	case UDA134X_UDA1345:
		ret = snd_soc_add_controls(codec, uda1345_snd_controls,
					ARRAY_SIZE(uda1345_snd_controls));
	break;
	default:
		printk(KERN_ERR "%s unknown codec type: %d",
			__func__, pd->model);
		kfree(uda134x);
		return -EINVAL;
	}

	if (ret < 0) {
		printk(KERN_ERR "UDA134X: failed to register controls\n");
		kfree(uda134x);
		return ret;
	}

	return 0;
}

/* power down chip */
static int uda134x_soc_remove(struct snd_soc_codec *codec)
{
	struct uda134x_priv *uda134x = snd_soc_codec_get_drvdata(codec);

	uda134x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	uda134x_set_bias_level(codec, SND_SOC_BIAS_OFF);

	kfree(uda134x);
	return 0;
}

#if defined(CONFIG_PM)
static int uda134x_soc_suspend(struct snd_soc_codec *codec,
						pm_message_t state)
{
	uda134x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	uda134x_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int uda134x_soc_resume(struct snd_soc_codec *codec)
{
	uda134x_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	uda134x_set_bias_level(codec, SND_SOC_BIAS_ON);
	return 0;
}
#else
#define uda134x_soc_suspend NULL
#define uda134x_soc_resume NULL
#endif /* CONFIG_PM */

static struct snd_soc_codec_driver soc_codec_dev_uda134x = {
	.probe =        uda134x_soc_probe,
	.remove =       uda134x_soc_remove,
	.suspend =      uda134x_soc_suspend,
	.resume =       uda134x_soc_resume,
	.reg_cache_size = sizeof(uda134x_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = uda134x_reg,
	.reg_cache_step = 1,
	.read = uda134x_read_reg_cache,
	.write = uda134x_write,
#ifdef POWER_OFF_ON_STANDBY
	.set_bias_level = uda134x_set_bias_level,
#endif
};

static int __devinit uda134x_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_uda134x, &uda134x_dai, 1);
}

static int __devexit uda134x_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver uda134x_codec_driver = {
	.driver = {
		.name = "uda134x-codec",
		.owner = THIS_MODULE,
	},
	.probe = uda134x_codec_probe,
	.remove = __devexit_p(uda134x_codec_remove),
};

static int __init uda134x_codec_init(void)
{
	return platform_driver_register(&uda134x_codec_driver);
}
module_init(uda134x_codec_init);

static void __exit uda134x_codec_exit(void)
{
	platform_driver_unregister(&uda134x_codec_driver);
}
module_exit(uda134x_codec_exit);

MODULE_DESCRIPTION("UDA134X ALSA soc codec driver");
MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_LICENSE("GPL");
