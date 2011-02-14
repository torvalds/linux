/*
 * DB1200 ASoC audio fabric support code.
 *
 * (c) 2008-9 Manuel Lauss <manuel.lauss@gmail.com>
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

/*-------------------------  AC97 PART  ---------------------------*/

static struct snd_soc_dai_link db1200_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	.codec_dai_name	= "ac97-hifi",
	.cpu_dai_name	= "au1xpsc_ac97.1",
	.platform_name	= "au1xpsc-pcm.1",
	.codec_name	= "ac97-codec.1",
};

static struct snd_soc_card db1200_ac97_machine = {
	.name		= "DB1200_AC97",
	.dai_link	= &db1200_ac97_dai,
	.num_links	= 1,
};

/*-------------------------  I2S PART  ---------------------------*/

static int db1200_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	/* WM8731 has its own 12MHz crystal */
	snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_XTAL,
				12000000, SND_SOC_CLOCK_IN);

	/* codec is bitclock and lrclk master */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_LEFT_J |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		goto out;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_LEFT_J |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	return ret;
}

static struct snd_soc_ops db1200_i2s_wm8731_ops = {
	.startup	= db1200_i2s_startup,
};

static struct snd_soc_dai_link db1200_i2s_dai = {
	.name		= "WM8731",
	.stream_name	= "WM8731 PCM",
	.codec_dai_name	= "wm8731-hifi",
	.cpu_dai_name	= "au1xpsc_i2s.1",
	.platform_name	= "au1xpsc-pcm.1",
	.codec_name	= "wm8731-codec.0-001b",
	.ops		= &db1200_i2s_wm8731_ops,
};

static struct snd_soc_card db1200_i2s_machine = {
	.name		= "DB1200_I2S",
	.dai_link	= &db1200_i2s_dai,
	.num_links	= 1,
};

/*-------------------------  COMMON PART  ---------------------------*/

static struct platform_device *db1200_asoc_dev;

static int __init db1200_audio_load(void)
{
	int ret;

	ret = -ENOMEM;
	db1200_asoc_dev = platform_device_alloc("soc-audio", 1); /* PSC1 */
	if (!db1200_asoc_dev)
		goto out;

	/* DB1200 board setup set PSC1MUX to preferred audio device */
	if (bcsr_read(BCSR_RESETS) & BCSR_RESETS_PSC1MUX)
		platform_set_drvdata(db1200_asoc_dev, &db1200_i2s_machine);
	else
		platform_set_drvdata(db1200_asoc_dev, &db1200_ac97_machine);

	ret = platform_device_add(db1200_asoc_dev);

	if (ret) {
		platform_device_put(db1200_asoc_dev);
		db1200_asoc_dev = NULL;
	}
out:
	return ret;
}

static void __exit db1200_audio_unload(void)
{
	platform_device_unregister(db1200_asoc_dev);
}

module_init(db1200_audio_load);
module_exit(db1200_audio_unload);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DB1200 ASoC audio support");
MODULE_AUTHOR("Manuel Lauss");
