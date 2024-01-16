// SPDX-License-Identifier: GPL-2.0-only
/*
 * PC-Speaker driver for Linux
 *
 * Copyright (C) 1997-2001  David Woodhouse
 * Copyright (C) 2001-2008  Stas Sergeev
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include "pcsp_input.h"
#include "pcsp.h"

MODULE_AUTHOR("Stas Sergeev <stsp@users.sourceforge.net>");
MODULE_DESCRIPTION("PC-Speaker driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcspkr");

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static bool enable = SNDRV_DEFAULT_ENABLE1;	/* Enable this card */
static bool nopcm;	/* Disable PCM capability of the driver */

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for pcsp soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for pcsp soundcard.");
module_param(enable, bool, 0444);
MODULE_PARM_DESC(enable, "Enable PC-Speaker sound.");
module_param(nopcm, bool, 0444);
MODULE_PARM_DESC(nopcm, "Disable PC-Speaker PCM sound. Only beeps remain.");

struct snd_pcsp pcsp_chip;

static int snd_pcsp_create(struct snd_card *card)
{
	unsigned int resolution = hrtimer_resolution;
	int div, min_div, order;

	if (!nopcm) {
		if (resolution > PCSP_MAX_PERIOD_NS) {
			printk(KERN_ERR "PCSP: Timer resolution is not sufficient "
				"(%unS)\n", resolution);
			printk(KERN_ERR "PCSP: Make sure you have HPET and ACPI "
				"enabled.\n");
			printk(KERN_ERR "PCSP: Turned into nopcm mode.\n");
			nopcm = 1;
		}
	}

	if (loops_per_jiffy >= PCSP_MIN_LPJ && resolution <= PCSP_MIN_PERIOD_NS)
		min_div = MIN_DIV;
	else
		min_div = MAX_DIV;
#if PCSP_DEBUG
	printk(KERN_DEBUG "PCSP: lpj=%li, min_div=%i, res=%u\n",
	       loops_per_jiffy, min_div, resolution);
#endif

	div = MAX_DIV / min_div;
	order = fls(div) - 1;

	pcsp_chip.max_treble = min(order, PCSP_MAX_TREBLE);
	pcsp_chip.treble = min(pcsp_chip.max_treble, PCSP_DEFAULT_TREBLE);
	pcsp_chip.playback_ptr = 0;
	pcsp_chip.period_ptr = 0;
	atomic_set(&pcsp_chip.timer_active, 0);
	pcsp_chip.enable = 1;
	pcsp_chip.pcspkr = 1;

	spin_lock_init(&pcsp_chip.substream_lock);

	pcsp_chip.card = card;
	pcsp_chip.port = 0x61;
	pcsp_chip.irq = -1;
	pcsp_chip.dma = -1;
	card->private_data = &pcsp_chip;

	return 0;
}

static void pcsp_stop_beep(struct snd_pcsp *chip);

static void alsa_card_pcsp_free(struct snd_card *card)
{
	pcsp_stop_beep(card->private_data);
}

static int snd_card_pcsp_probe(int devnum, struct device *dev)
{
	struct snd_card *card;
	int err;

	if (devnum != 0)
		return -EINVAL;

	hrtimer_init(&pcsp_chip.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pcsp_chip.timer.function = pcsp_do_timer;

	err = snd_devm_card_new(dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	err = snd_pcsp_create(card);
	if (err < 0)
		return err;

	if (!nopcm) {
		err = snd_pcsp_new_pcm(&pcsp_chip);
		if (err < 0)
			return err;
	}
	err = snd_pcsp_new_mixer(&pcsp_chip, nopcm);
	if (err < 0)
		return err;

	strcpy(card->driver, "PC-Speaker");
	strcpy(card->shortname, "pcsp");
	sprintf(card->longname, "Internal PC-Speaker at port 0x%x",
		pcsp_chip.port);

	err = snd_card_register(card);
	if (err < 0)
		return err;
	card->private_free = alsa_card_pcsp_free;

	return 0;
}

static int alsa_card_pcsp_init(struct device *dev)
{
	int err;

	err = snd_card_pcsp_probe(0, dev);
	if (err) {
		printk(KERN_ERR "PC-Speaker initialization failed.\n");
		return err;
	}

	/* Well, CONFIG_DEBUG_PAGEALLOC makes the sound horrible. Lets alert */
	if (debug_pagealloc_enabled()) {
		printk(KERN_WARNING "PCSP: CONFIG_DEBUG_PAGEALLOC is enabled, "
		       "which may make the sound noisy.\n");
	}

	return 0;
}

static int pcsp_probe(struct platform_device *dev)
{
	int err;

	err = pcspkr_input_init(&pcsp_chip.input_dev, &dev->dev);
	if (err < 0)
		return err;

	err = alsa_card_pcsp_init(&dev->dev);
	if (err < 0)
		return err;

	platform_set_drvdata(dev, &pcsp_chip);
	return 0;
}

static void pcsp_stop_beep(struct snd_pcsp *chip)
{
	pcsp_sync_stop(chip);
	pcspkr_stop_sound();
}

#ifdef CONFIG_PM_SLEEP
static int pcsp_suspend(struct device *dev)
{
	struct snd_pcsp *chip = dev_get_drvdata(dev);
	pcsp_stop_beep(chip);
	return 0;
}

static SIMPLE_DEV_PM_OPS(pcsp_pm, pcsp_suspend, NULL);
#define PCSP_PM_OPS	&pcsp_pm
#else
#define PCSP_PM_OPS	NULL
#endif	/* CONFIG_PM_SLEEP */

static void pcsp_shutdown(struct platform_device *dev)
{
	struct snd_pcsp *chip = platform_get_drvdata(dev);
	pcsp_stop_beep(chip);
}

static struct platform_driver pcsp_platform_driver = {
	.driver		= {
		.name	= "pcspkr",
		.pm	= PCSP_PM_OPS,
	},
	.probe		= pcsp_probe,
	.shutdown	= pcsp_shutdown,
};

static int __init pcsp_init(void)
{
	if (!enable)
		return -ENODEV;
	return platform_driver_register(&pcsp_platform_driver);
}

static void __exit pcsp_exit(void)
{
	platform_driver_unregister(&pcsp_platform_driver);
}

module_init(pcsp_init);
module_exit(pcsp_exit);
