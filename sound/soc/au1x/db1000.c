/*
 * DB1000/DB1500/DB1100 ASoC audio fabric support code.
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
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
#include <asm/mach-db1x00/bcsr.h>

#include "psc.h"

static struct snd_soc_dai_link db1000_ac97_dai = {
	.name		= "AC97",
	.stream_name	= "AC97 HiFi",
	.codec_dai_name	= "ac97-hifi",
	.cpu_dai_name	= "alchemy-ac97c",
	.platform_name	= "alchemy-pcm-dma.0",
	.codec_name	= "ac97-codec",
};

static struct snd_soc_card db1000_ac97 = {
	.name		= "DB1000_AC97",
	.dai_link	= &db1000_ac97_dai,
	.num_links	= 1,
};

static int __devinit db1000_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &db1000_ac97;
	card->dev = &pdev->dev;
	return snd_soc_register_card(card);
}

static int __devexit db1000_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver db1000_audio_driver = {
	.driver	= {
		.name	= "db1000-audio",
		.owner	= THIS_MODULE,
		.pm	= &snd_soc_pm_ops,
	},
	.probe		= db1000_audio_probe,
	.remove		= __devexit_p(db1000_audio_remove),
};

static int __init db1000_audio_load(void)
{
	return platform_driver_register(&db1000_audio_driver);
}

static void __exit db1000_audio_unload(void)
{
	platform_driver_unregister(&db1000_audio_driver);
}

module_init(db1000_audio_load);
module_exit(db1000_audio_unload);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DB1000/DB1500/DB1100 ASoC audio");
MODULE_AUTHOR("Manuel Lauss");
