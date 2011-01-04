/*
 *  sn95031.c -  TI sn95031 Codec driver
 *
 *  Copyright (C) 2010 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  Author: Harsha Priya <priya.harsha@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/intel_scu_ipc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include "sn95031.h"

#define SN95031_RATES (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100)
#define SN95031_FORMATS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

/*
 * todo:
 * capture paths
 * jack detection
 * PM functions
 */

static inline unsigned int sn95031_read(struct snd_soc_codec *codec,
			unsigned int reg)
{
	u8 value = 0;
	int ret;

	ret = intel_scu_ipc_ioread8(reg, &value);
	if (ret)
		pr_err("read of %x failed, err %d\n", reg, ret);
	return value;

}

static inline int sn95031_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	int ret;

	ret = intel_scu_ipc_iowrite8(reg, value);
	if (ret)
		pr_err("write of %x failed, err %d\n", reg, ret);
	return ret;
}

static int sn95031_set_vaud_bias(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
			pr_debug("vaud_bias powering up pll\n");
			/* power up the pll */
			snd_soc_write(codec, SN95031_AUDPLLCTRL, BIT(5));
			/* enable pcm 2 */
			snd_soc_update_bits(codec, SN95031_PCM2C2,
					BIT(0), BIT(0));
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			pr_debug("vaud_bias power up rail\n");
			/* power up the rail */
			snd_soc_write(codec, SN95031_VAUD,
					BIT(2)|BIT(1)|BIT(0));
			msleep(1);
		} else if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE) {
			/* turn off pcm */
			pr_debug("vaud_bias power dn pcm\n");
			snd_soc_update_bits(codec, SN95031_PCM2C2, BIT(0), 0);
			snd_soc_write(codec, SN95031_AUDPLLCTRL, 0);
		}
		break;


	case SND_SOC_BIAS_OFF:
		pr_debug("vaud_bias _OFF doing rail shutdown\n");
		snd_soc_write(codec, SN95031_VAUD, BIT(3));
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

static int sn95031_vhs_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		pr_debug("VHS SND_SOC_DAPM_EVENT_ON doing rail startup now\n");
		/* power up the rail */
		snd_soc_write(w->codec, SN95031_VHSP, 0x3D);
		snd_soc_write(w->codec, SN95031_VHSN, 0x3F);
		msleep(1);
	} else if (SND_SOC_DAPM_EVENT_OFF(event)) {
		pr_debug("VHS SND_SOC_DAPM_EVENT_OFF doing rail shutdown\n");
		snd_soc_write(w->codec, SN95031_VHSP, 0xC4);
		snd_soc_write(w->codec, SN95031_VHSN, 0x04);
	}
	return 0;
}

static int sn95031_vihf_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		pr_debug("VIHF SND_SOC_DAPM_EVENT_ON doing rail startup now\n");
		/* power up the rail */
		snd_soc_write(w->codec, SN95031_VIHF, 0x27);
		msleep(1);
	} else if (SND_SOC_DAPM_EVENT_OFF(event)) {
		pr_debug("VIHF SND_SOC_DAPM_EVENT_OFF doing rail shutdown\n");
		snd_soc_write(w->codec, SN95031_VIHF, 0x24);
	}
	return 0;
}

/* DAPM widgets */
static const struct snd_soc_dapm_widget sn95031_dapm_widgets[] = {

	/* all end points mic, hs etc */
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("EPOUT"),
	SND_SOC_DAPM_OUTPUT("IHFOUTL"),
	SND_SOC_DAPM_OUTPUT("IHFOUTR"),
	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),
	SND_SOC_DAPM_OUTPUT("VIB1OUT"),
	SND_SOC_DAPM_OUTPUT("VIB2OUT"),

	SND_SOC_DAPM_SUPPLY("Headset Rail", SND_SOC_NOPM, 0, 0,
			sn95031_vhs_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("Speaker Rail", SND_SOC_NOPM, 0, 0,
			sn95031_vihf_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* playback path driver enables */
	SND_SOC_DAPM_PGA("Headset Left Playback",
			SN95031_DRIVEREN, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Headset Right Playback",
			SN95031_DRIVEREN, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Speaker Left Playback",
			SN95031_DRIVEREN, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Speaker Right Playback",
			SN95031_DRIVEREN, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Vibra1 Playback",
			SN95031_DRIVEREN, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Vibra2 Playback",
			SN95031_DRIVEREN, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Earpiece Playback",
			SN95031_DRIVEREN, 6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Lineout Left Playback",
			SN95031_LOCTL, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Lineout Right Playback",
			SN95031_LOCTL, 4, 0, NULL, 0),

	/* playback path filter enable */
	SND_SOC_DAPM_PGA("Headset Left Filter",
			SN95031_HSEPRXCTRL, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Headset Right Filter",
			SN95031_HSEPRXCTRL, 5, 0,  NULL, 0),
	SND_SOC_DAPM_PGA("Speaker Left Filter",
			SN95031_IHFRXCTRL, 0, 0,  NULL, 0),
	SND_SOC_DAPM_PGA("Speaker Right Filter",
			SN95031_IHFRXCTRL, 1, 0,  NULL, 0),

	/* DACs */
	SND_SOC_DAPM_DAC("HSDAC Left", "Headset",
			SN95031_DACCONFIG, 0, 0),
	SND_SOC_DAPM_DAC("HSDAC Right", "Headset",
			SN95031_DACCONFIG, 1, 0),
	SND_SOC_DAPM_DAC("IHFDAC Left", "Speaker",
			SN95031_DACCONFIG, 2, 0),
	SND_SOC_DAPM_DAC("IHFDAC Right", "Speaker",
			SN95031_DACCONFIG, 3, 0),
	SND_SOC_DAPM_DAC("Vibra1 DAC", "Vibra1",
			SN95031_VIB1C5, 1, 0),
	SND_SOC_DAPM_DAC("Vibra2 DAC", "Vibra2",
			SN95031_VIB2C5, 1, 0),
};

static const struct snd_soc_dapm_route sn95031_audio_map[] = {
	/* headset and earpiece map */
	{ "HPOUTL", NULL, "Headset Left Playback" },
	{ "HPOUTR", NULL, "Headset Right Playback" },
	{ "EPOUT", NULL, "Earpiece Playback" },
	{ "Headset Left Playback", NULL, "Headset Left Filter"},
	{ "Headset Right Playback", NULL, "Headset Right Filter"},
	{ "Earpiece Playback", NULL, "Headset Left Filter"},
	{ "Headset Left Filter", NULL, "HSDAC Left"},
	{ "Headset Right Filter", NULL, "HSDAC Right"},
	{ "HSDAC Left", NULL, "Headset Rail"},
	{ "HSDAC Right", NULL, "Headset Rail"},

	/* speaker map */
	{ "IHFOUTL", "NULL", "Speaker Left Playback"},
	{ "IHFOUTR", "NULL", "Speaker Right Playback"},
	{ "Speaker Left Playback", NULL, "Speaker Left Filter"},
	{ "Speaker Right Playback", NULL, "Speaker Right Filter"},
	{ "Speaker Left Filter", NULL, "IHFDAC Left"},
	{ "Speaker Right Filter", NULL, "IHFDAC Right"},
	{ "IHFDAC Left", NULL, "Speaker Rail"},
	{ "IHFDAC Right", NULL, "Speaker Rail"},

	/* vibra map */
	{"VIB1OUT", NULL, "Vibra1 Playback"},
	{"Vibra1 Playback", NULL, "Vibra1 DAC"},

	{"VIB2OUT", NULL, "Vibra2 Playback"},
	{"Vibra2 Playback", NULL, "Vibra2 DAC"},

	/* lineout */
	{"LINEOUTL", NULL, "Lineout Left Playback"},
	{"LINEOUTR", NULL, "Lineout Right Playback"},
	{"Lineout Left Playback", NULL, "Headset Left Filter"},
	{"Lineout Left Playback", NULL, "Speaker Left Filter"},
	{"Lineout Left Playback", NULL, "Vibra1 DAC"},
	{"Lineout Right Playback", NULL, "Headset Right Filter"},
	{"Lineout Right Playback", NULL, "Speaker Right Filter"},
	{"Lineout Right Playback", NULL, "Vibra2 DAC"},
};

/* speaker and headset mutes, for audio pops and clicks */
static int sn95031_pcm_hs_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_update_bits(dai->codec,
			SN95031_HSLVOLCTRL, BIT(7), (!mute << 7));
	snd_soc_update_bits(dai->codec,
			SN95031_HSRVOLCTRL, BIT(7), (!mute << 7));
	return 0;
}

static int sn95031_pcm_spkr_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_update_bits(dai->codec,
			SN95031_IHFLVOLCTRL, BIT(7), (!mute << 7));
	snd_soc_update_bits(dai->codec,
			SN95031_IHFRVOLCTRL, BIT(7), (!mute << 7));
	return 0;
}

int sn95031_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int format, rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		format = BIT(4)|BIT(5);
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		format = 0;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(dai->codec, SN95031_PCM2C2,
			BIT(4)|BIT(5), format);

	switch (params_rate(params)) {
	case 48000:
		pr_debug("RATE_48000\n");
		rate = 0;
		break;

	case 44100:
		pr_debug("RATE_44100\n");
		rate = BIT(7);
		break;

	default:
		pr_err("ERR rate %d\n", params_rate(params));
		return -EINVAL;
	}
	snd_soc_update_bits(dai->codec, SN95031_PCM1C1, BIT(7), rate);

	return 0;
}

/* Codec DAI section */
static struct snd_soc_dai_ops sn95031_headset_dai_ops = {
	.digital_mute	= sn95031_pcm_hs_mute,
	.hw_params	= sn95031_pcm_hw_params,
};

static struct snd_soc_dai_ops sn95031_speaker_dai_ops = {
	.digital_mute	= sn95031_pcm_spkr_mute,
	.hw_params	= sn95031_pcm_hw_params,
};

static struct snd_soc_dai_ops sn95031_vib1_dai_ops = {
	.hw_params	= sn95031_pcm_hw_params,
};

static struct snd_soc_dai_ops sn95031_vib2_dai_ops = {
	.hw_params	= sn95031_pcm_hw_params,
};

struct snd_soc_dai_driver sn95031_dais[] = {
{
	.name = "SN95031 Headset",
	.playback = {
		.stream_name = "Headset",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SN95031_RATES,
		.formats = SN95031_FORMATS,
	},
	.ops = &sn95031_headset_dai_ops,
},
{	.name = "SN95031 Speaker",
	.playback = {
		.stream_name = "Speaker",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SN95031_RATES,
		.formats = SN95031_FORMATS,
	},
	.ops = &sn95031_speaker_dai_ops,
},
{	.name = "SN95031 Vibra1",
	.playback = {
		.stream_name = "Vibra1",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SN95031_RATES,
		.formats = SN95031_FORMATS,
	},
	.ops = &sn95031_vib1_dai_ops,
},
{	.name = "SN95031 Vibra2",
	.playback = {
		.stream_name = "Vibra2",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SN95031_RATES,
		.formats = SN95031_FORMATS,
	},
	.ops = &sn95031_vib2_dai_ops,
},
};

/* codec registration */
static int sn95031_codec_probe(struct snd_soc_codec *codec)
{
	int ret;

	pr_debug("codec_probe called\n");

	codec->dapm.bias_level = SND_SOC_BIAS_OFF;
	codec->dapm.idle_bias_off = 1;

	/* PCM interface config
	 * This sets the pcm rx slot conguration to max 6 slots
	 * for max 4 dais (2 stereo and 2 mono)
	 */
	snd_soc_write(codec, SN95031_PCM2RXSLOT01, 0x10);
	snd_soc_write(codec, SN95031_PCM2RXSLOT23, 0x32);
	snd_soc_write(codec, SN95031_PCM2RXSLOT45, 0x54);
	/* pcm port setting
	 * This sets the pcm port to slave and clock at 19.2Mhz which
	 * can support 6slots, sampling rate set per stream in hw-params
	 */
	snd_soc_write(codec, SN95031_PCM1C1, 0x00);
	snd_soc_write(codec, SN95031_PCM2C1, 0x01);
	snd_soc_write(codec, SN95031_PCM2C2, 0x0A);
	snd_soc_write(codec, SN95031_HSMIXER, BIT(0)|BIT(4));
	/* vendor vibra workround, the vibras are muted by
	 * custom register so unmute them
	 */
	snd_soc_write(codec, SN95031_SSR5, 0x80);
	snd_soc_write(codec, SN95031_SSR6, 0x80);
	snd_soc_write(codec, SN95031_VIB1C5, 0x00);
	snd_soc_write(codec, SN95031_VIB2C5, 0x00);
	/* configure vibras for pcm port */
	snd_soc_write(codec, SN95031_VIB1C3, 0x00);
	snd_soc_write(codec, SN95031_VIB2C3, 0x00);

	/* soft mute ramp time */
	snd_soc_write(codec, SN95031_SOFTMUTE, 0x3);
	/* fix the initial volume at 1dB,
	 * default in +9dB,
	 * 1dB give optimal swing on DAC, amps
	 */
	snd_soc_write(codec, SN95031_HSLVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_HSRVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_IHFLVOLCTRL, 0x08);
	snd_soc_write(codec, SN95031_IHFRVOLCTRL, 0x08);
	/* dac mode and lineout workaround */
	snd_soc_write(codec, SN95031_SSR2, 0x10);
	snd_soc_write(codec, SN95031_SSR3, 0x40);

	ret = snd_soc_dapm_new_controls(&codec->dapm, sn95031_dapm_widgets,
				ARRAY_SIZE(sn95031_dapm_widgets));
	if (ret)
		pr_err("soc_dapm_new_control failed %d", ret);
	ret = snd_soc_dapm_add_routes(&codec->dapm, sn95031_audio_map,
				ARRAY_SIZE(sn95031_audio_map));
	if (ret)
		pr_err("soc_dapm_add_routes failed %d", ret);

	return ret;
}

static int sn95031_codec_remove(struct snd_soc_codec *codec)
{
	pr_debug("codec_remove called\n");
	sn95031_set_vaud_bias(codec, SND_SOC_BIAS_OFF);

	return 0;
}

struct snd_soc_codec_driver sn95031_codec = {
	.probe =	sn95031_codec_probe,
	.remove =	sn95031_codec_remove,
	.read		= sn95031_read,
	.write		= sn95031_write,
	.set_bias_level	= sn95031_set_vaud_bias,
};

static int __devinit sn95031_device_probe(struct platform_device *pdev)
{
	pr_debug("codec device probe called for %s\n", dev_name(&pdev->dev));
	return snd_soc_register_codec(&pdev->dev, &sn95031_codec,
			sn95031_dais, ARRAY_SIZE(sn95031_dais));
}

static int __devexit sn95031_device_remove(struct platform_device *pdev)
{
	pr_debug("codec device remove called\n");
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver sn95031_codec_driver = {
	.driver		= {
		.name		= "sn95031",
		.owner		= THIS_MODULE,
	},
	.probe		= sn95031_device_probe,
	.remove		= sn95031_device_remove,
};

static int __init sn95031_init(void)
{
	pr_debug("driver init called\n");
	return platform_driver_register(&sn95031_codec_driver);
}
module_init(sn95031_init);

static void __exit sn95031_exit(void)
{
	pr_debug("driver exit called\n");
	platform_driver_unregister(&sn95031_codec_driver);
}
module_exit(sn95031_exit);

MODULE_DESCRIPTION("ASoC Intel(R) SN95031 codec driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sn95031");
