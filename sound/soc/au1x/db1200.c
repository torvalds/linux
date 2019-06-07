// SPDX-License-Identifier: GPL-2.0-only
/*
 * DB1200/DB1300/DB1550 ASoC audio fabric support code.
 *
 * (c) 2008-2011 Manuel Lauss <manuel.lauss@googlemail.com>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-db1x00/bcsr.h>

#include "../codecs/wm8731.h"
#include "psc.h"

static const struct platform_device_id db1200_pids[] = {
	{
		.name		= "db1200-ac97",
		.driver_data	= 0,
	}, {
		.name		= "db1200-i2s",
		.driver_data	= 1,
	}, {
		.name		= "db1300-ac97",
		.driver_data	= 2,
	}, {
		.name		= "db1300-i2s",
		.driver_data	= 3,
	}, {
		.name		= "db1550-ac97",
		.driver_data	= 4,
	}, {
		.name		= "db1550-i2s",
		.driver_data	= 5,
	},
	{},
};

/*-------------------------  AC97 PART  ---------------------------*/

SND_SOC_DAILINK_DEFS(db1200_ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("au1xpsc_ac97.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("ac97-codec.1", "ac97-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("au1xpsc-pcm.1")));

static struct snd_soc_dai_link db1200_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	SND_SOC_DAILINK_REG(db1200_ac97),
};

static struct snd_soc_card db1200_ac97_machine = {
	.name		= "DB1200_AC97",
	.owner		= THIS_MODULE,
	.dai_link	= &db1200_ac97_dai,
	.num_links	= 1,
};

SND_SOC_DAILINK_DEFS(db1300_ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("au1xpsc_ac97.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec.1", "wm9712-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("au1xpsc-pcm.1")));

static struct snd_soc_dai_link db1300_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	SND_SOC_DAILINK_REG(db1300_ac97),
};

static struct snd_soc_card db1300_ac97_machine = {
	.name		= "DB1300_AC97",
	.owner		= THIS_MODULE,
	.dai_link	= &db1300_ac97_dai,
	.num_links	= 1,
};

static struct snd_soc_card db1550_ac97_machine = {
	.name		= "DB1550_AC97",
	.owner		= THIS_MODULE,
	.dai_link	= &db1200_ac97_dai,
	.num_links	= 1,
};

/*-------------------------  I2S PART  ---------------------------*/

static int db1200_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* WM8731 has its own 12MHz crystal */
	snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_XTAL,
				12000000, SND_SOC_CLOCK_IN);

	return 0;
}

static const struct snd_soc_ops db1200_i2s_wm8731_ops = {
	.startup	= db1200_i2s_startup,
};

SND_SOC_DAILINK_DEFS(db1200_i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("au1xpsc_i2s.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("au1xpsc-pcm.1")));

static struct snd_soc_dai_link db1200_i2s_dai = {
	.name		= "WM8731",
	.stream_name	= "WM8731 PCM",
	.dai_fmt	= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &db1200_i2s_wm8731_ops,
	SND_SOC_DAILINK_REG(db1200_i2s),
};

static struct snd_soc_card db1200_i2s_machine = {
	.name		= "DB1200_I2S",
	.owner		= THIS_MODULE,
	.dai_link	= &db1200_i2s_dai,
	.num_links	= 1,
};

SND_SOC_DAILINK_DEFS(db1300_i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("au1xpsc_i2s.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("au1xpsc-pcm.2")));

static struct snd_soc_dai_link db1300_i2s_dai = {
	.name		= "WM8731",
	.stream_name	= "WM8731 PCM",
	.dai_fmt	= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &db1200_i2s_wm8731_ops,
	SND_SOC_DAILINK_REG(db1300_i2s),
};

static struct snd_soc_card db1300_i2s_machine = {
	.name		= "DB1300_I2S",
	.owner		= THIS_MODULE,
	.dai_link	= &db1300_i2s_dai,
	.num_links	= 1,
};

SND_SOC_DAILINK_DEFS(db1550_i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("au1xpsc_i2s.3")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("au1xpsc-pcm.3")));

static struct snd_soc_dai_link db1550_i2s_dai = {
	.name		= "WM8731",
	.stream_name	= "WM8731 PCM",
	.dai_fmt	= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &db1200_i2s_wm8731_ops,
	SND_SOC_DAILINK_REG(db1550_i2s),
};

static struct snd_soc_card db1550_i2s_machine = {
	.name		= "DB1550_I2S",
	.owner		= THIS_MODULE,
	.dai_link	= &db1550_i2s_dai,
	.num_links	= 1,
};

/*-------------------------  COMMON PART  ---------------------------*/

static struct snd_soc_card *db1200_cards[] = {
	&db1200_ac97_machine,
	&db1200_i2s_machine,
	&db1300_ac97_machine,
	&db1300_i2s_machine,
	&db1550_ac97_machine,
	&db1550_i2s_machine,
};

static int db1200_audio_probe(struct platform_device *pdev)
{
	const struct platform_device_id *pid = platform_get_device_id(pdev);
	struct snd_soc_card *card;

	card = db1200_cards[pid->driver_data];
	card->dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver db1200_audio_driver = {
	.driver	= {
		.name	= "db1200-ac97",
		.pm	= &snd_soc_pm_ops,
	},
	.id_table	= db1200_pids,
	.probe		= db1200_audio_probe,
};

module_platform_driver(db1200_audio_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DB1200/DB1300/DB1550 ASoC audio support");
MODULE_AUTHOR("Manuel Lauss");
