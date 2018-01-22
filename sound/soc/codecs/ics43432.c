/*
 * I2S MEMS microphone driver for InvenSense ICS-43432
 *
 * - Non configurable.
 * - I2S interface, 64 BCLs per frame, 32 bits per channel, 24 bit data
 *
 * Copyright (c) 2015 Axis Communications AB
 *
 * Licensed under GPL v2.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define ICS43432_RATE_MIN 7190 /* Hz, from data sheet */
#define ICS43432_RATE_MAX 52800  /* Hz, from data sheet */

#define ICS43432_FORMATS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32)

static struct snd_soc_dai_driver ics43432_dai = {
	.name = "ics43432-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rate_min = ICS43432_RATE_MIN,
		.rate_max = ICS43432_RATE_MAX,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = ICS43432_FORMATS,
	},
};

static const struct snd_soc_codec_driver ics43432_codec_driver = {
};

static int ics43432_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &ics43432_codec_driver,
			&ics43432_dai, 1);
}

static int ics43432_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ics43432_ids[] = {
	{ .compatible = "invensense,ics43432", },
	{ }
};
MODULE_DEVICE_TABLE(of, ics43432_ids);
#endif

static struct platform_driver ics43432_driver = {
	.driver = {
		.name = "ics43432",
		.of_match_table = of_match_ptr(ics43432_ids),
	},
	.probe = ics43432_probe,
	.remove = ics43432_remove,
};

module_platform_driver(ics43432_driver);

MODULE_DESCRIPTION("ASoC ICS43432 driver");
MODULE_AUTHOR("Ricard Wanderlof <ricardw@axis.com>");
MODULE_LICENSE("GPL v2");
