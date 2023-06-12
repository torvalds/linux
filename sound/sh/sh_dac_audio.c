// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sh_dac_audio.c - SuperH DAC audio driver for ALSA
 *
 * Copyright (c) 2009 by Rafael Ignacio Zurita <rizurita@yahoo.com>
 *
 * Based on sh_dac_audio.c (Copyright (C) 2004, 2005 by Andriy Skulysh)
 */

#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/sh_dac_audio.h>
#include <asm/clock.h>
#include <asm/hd64461.h>
#include <mach/hp6xx.h>
#include <cpu/dac.h>

MODULE_AUTHOR("Rafael Ignacio Zurita <rizurita@yahoo.com>");
MODULE_DESCRIPTION("SuperH DAC audio driver");
MODULE_LICENSE("GPL");

/* Module Parameters */
static int index = SNDRV_DEFAULT_IDX1;
static char *id = SNDRV_DEFAULT_STR1;
module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for SuperH DAC audio.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for SuperH DAC audio.");

/* main struct */
struct snd_sh_dac {
	struct snd_card *card;
	struct snd_pcm_substream *substream;
	struct hrtimer hrtimer;
	ktime_t wakeups_per_second;

	int rate;
	int empty;
	char *data_buffer, *buffer_begin, *buffer_end;
	int processed; /* bytes proccesed, to compare with period_size */
	int buffer_size;
	struct dac_audio_pdata *pdata;
};


static void dac_audio_start_timer(struct snd_sh_dac *chip)
{
	hrtimer_start(&chip->hrtimer, chip->wakeups_per_second,
		      HRTIMER_MODE_REL);
}

static void dac_audio_stop_timer(struct snd_sh_dac *chip)
{
	hrtimer_cancel(&chip->hrtimer);
}

static void dac_audio_reset(struct snd_sh_dac *chip)
{
	dac_audio_stop_timer(chip);
	chip->buffer_begin = chip->buffer_end = chip->data_buffer;
	chip->processed = 0;
	chip->empty = 1;
}

static void dac_audio_set_rate(struct snd_sh_dac *chip)
{
	chip->wakeups_per_second = 1000000000 / chip->rate;
}


/* PCM INTERFACE */

static const struct snd_pcm_hardware snd_sh_dac_pcm_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_HALF_DUPLEX),
	.formats		= SNDRV_PCM_FMTBIT_U8,
	.rates			= SNDRV_PCM_RATE_8000,
	.rate_min		= 8000,
	.rate_max		= 8000,
	.channels_min		= 1,
	.channels_max		= 1,
	.buffer_bytes_max	= (48*1024),
	.period_bytes_min	= 1,
	.period_bytes_max	= (48*1024),
	.periods_min		= 1,
	.periods_max		= 1024,
};

static int snd_sh_dac_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw = snd_sh_dac_pcm_hw;

	chip->substream = substream;
	chip->buffer_begin = chip->buffer_end = chip->data_buffer;
	chip->processed = 0;
	chip->empty = 1;

	chip->pdata->start(chip->pdata);

	return 0;
}

static int snd_sh_dac_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);

	chip->substream = NULL;

	dac_audio_stop_timer(chip);
	chip->pdata->stop(chip->pdata);

	return 0;
}

static int snd_sh_dac_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = chip->substream->runtime;

	chip->buffer_size = runtime->buffer_size;
	memset(chip->data_buffer, 0, chip->pdata->buffer_size);

	return 0;
}

static int snd_sh_dac_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dac_audio_start_timer(chip);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		chip->buffer_begin = chip->buffer_end = chip->data_buffer;
		chip->processed = 0;
		chip->empty = 1;
		dac_audio_stop_timer(chip);
		break;
	default:
		 return -EINVAL;
	}

	return 0;
}

static int snd_sh_dac_pcm_copy(struct snd_pcm_substream *substream,
			       int channel, unsigned long pos,
			       void __user *src, unsigned long count)
{
	/* channel is not used (interleaved data) */
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);

	if (copy_from_user_toio(chip->data_buffer + pos, src, count))
		return -EFAULT;
	chip->buffer_end = chip->data_buffer + pos + count;

	if (chip->empty) {
		chip->empty = 0;
		dac_audio_start_timer(chip);
	}

	return 0;
}

static int snd_sh_dac_pcm_copy_kernel(struct snd_pcm_substream *substream,
				      int channel, unsigned long pos,
				      void *src, unsigned long count)
{
	/* channel is not used (interleaved data) */
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);

	memcpy_toio(chip->data_buffer + pos, src, count);
	chip->buffer_end = chip->data_buffer + pos + count;

	if (chip->empty) {
		chip->empty = 0;
		dac_audio_start_timer(chip);
	}

	return 0;
}

static int snd_sh_dac_pcm_silence(struct snd_pcm_substream *substream,
				  int channel, unsigned long pos,
				  unsigned long count)
{
	/* channel is not used (interleaved data) */
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);

	memset_io(chip->data_buffer + pos, 0, count);
	chip->buffer_end = chip->data_buffer + pos + count;

	if (chip->empty) {
		chip->empty = 0;
		dac_audio_start_timer(chip);
	}

	return 0;
}

static
snd_pcm_uframes_t snd_sh_dac_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);
	int pointer = chip->buffer_begin - chip->data_buffer;

	return pointer;
}

/* pcm ops */
static const struct snd_pcm_ops snd_sh_dac_pcm_ops = {
	.open		= snd_sh_dac_pcm_open,
	.close		= snd_sh_dac_pcm_close,
	.prepare	= snd_sh_dac_pcm_prepare,
	.trigger	= snd_sh_dac_pcm_trigger,
	.pointer	= snd_sh_dac_pcm_pointer,
	.copy_user	= snd_sh_dac_pcm_copy,
	.copy_kernel	= snd_sh_dac_pcm_copy_kernel,
	.fill_silence	= snd_sh_dac_pcm_silence,
	.mmap		= snd_pcm_lib_mmap_iomem,
};

static int snd_sh_dac_pcm(struct snd_sh_dac *chip, int device)
{
	int err;
	struct snd_pcm *pcm;

	/* device should be always 0 for us */
	err = snd_pcm_new(chip->card, "SH_DAC PCM", device, 1, 0, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;
	strcpy(pcm->name, "SH_DAC PCM");
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sh_dac_pcm_ops);

	/* buffer size=48K */
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
				       NULL, 48 * 1024, 48 * 1024);

	return 0;
}
/* END OF PCM INTERFACE */


/* driver .remove  --  destructor */
static void snd_sh_dac_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
}

/* free -- it has been defined by create */
static int snd_sh_dac_free(struct snd_sh_dac *chip)
{
	/* release the data */
	kfree(chip->data_buffer);
	kfree(chip);

	return 0;
}

static int snd_sh_dac_dev_free(struct snd_device *device)
{
	struct snd_sh_dac *chip = device->device_data;

	return snd_sh_dac_free(chip);
}

static enum hrtimer_restart sh_dac_audio_timer(struct hrtimer *handle)
{
	struct snd_sh_dac *chip = container_of(handle, struct snd_sh_dac,
					       hrtimer);
	struct snd_pcm_runtime *runtime = chip->substream->runtime;
	ssize_t b_ps = frames_to_bytes(runtime, runtime->period_size);

	if (!chip->empty) {
		sh_dac_output(*chip->buffer_begin, chip->pdata->channel);
		chip->buffer_begin++;

		chip->processed++;
		if (chip->processed >= b_ps) {
			chip->processed -= b_ps;
			snd_pcm_period_elapsed(chip->substream);
		}

		if (chip->buffer_begin == (chip->data_buffer +
					   chip->buffer_size - 1))
			chip->buffer_begin = chip->data_buffer;

		if (chip->buffer_begin == chip->buffer_end)
			chip->empty = 1;

	}

	if (!chip->empty)
		hrtimer_start(&chip->hrtimer, chip->wakeups_per_second,
			      HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

/* create  --  chip-specific constructor for the cards components */
static int snd_sh_dac_create(struct snd_card *card,
			     struct platform_device *devptr,
			     struct snd_sh_dac **rchip)
{
	struct snd_sh_dac *chip;
	int err;

	static const struct snd_device_ops ops = {
		   .dev_free = snd_sh_dac_dev_free,
	};

	*rchip = NULL;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->card = card;

	hrtimer_init(&chip->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->hrtimer.function = sh_dac_audio_timer;

	dac_audio_reset(chip);
	chip->rate = 8000;
	dac_audio_set_rate(chip);

	chip->pdata = devptr->dev.platform_data;

	chip->data_buffer = kmalloc(chip->pdata->buffer_size, GFP_KERNEL);
	if (chip->data_buffer == NULL) {
		kfree(chip);
		return -ENOMEM;
	}

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_sh_dac_free(chip);
		return err;
	}

	*rchip = chip;

	return 0;
}

/* driver .probe  --  constructor */
static int snd_sh_dac_probe(struct platform_device *devptr)
{
	struct snd_sh_dac *chip;
	struct snd_card *card;
	int err;

	err = snd_card_new(&devptr->dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0) {
			snd_printk(KERN_ERR "cannot allocate the card\n");
			return err;
	}

	err = snd_sh_dac_create(card, devptr, &chip);
	if (err < 0)
		goto probe_error;

	err = snd_sh_dac_pcm(chip, 0);
	if (err < 0)
		goto probe_error;

	strcpy(card->driver, "snd_sh_dac");
	strcpy(card->shortname, "SuperH DAC audio driver");
	printk(KERN_INFO "%s %s", card->longname, card->shortname);

	err = snd_card_register(card);
	if (err < 0)
		goto probe_error;

	snd_printk(KERN_INFO "ALSA driver for SuperH DAC audio");

	platform_set_drvdata(devptr, card);
	return 0;

probe_error:
	snd_card_free(card);
	return err;
}

/*
 * "driver" definition
 */
static struct platform_driver sh_dac_driver = {
	.probe	= snd_sh_dac_probe,
	.remove_new = snd_sh_dac_remove,
	.driver = {
		.name = "dac_audio",
	},
};

module_platform_driver(sh_dac_driver);
