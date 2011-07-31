/*
 * sh_dac_audio.c - SuperH DAC audio driver for ALSA
 *
 * Copyright (c) 2009 by Rafael Ignacio Zurita <rizurita@yahoo.com>
 *
 *
 * Based on sh_dac_audio.c (Copyright (C) 2004, 2005 by Andriy Skulysh)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
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
MODULE_SUPPORTED_DEVICE("{{SuperH DAC audio support}}");

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
	chip->wakeups_per_second = ktime_set(0, 1000000000 / chip->rate);
}


/* PCM INTERFACE */

static struct snd_pcm_hardware snd_sh_dac_pcm_hw = {
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

static int snd_sh_dac_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
			params_buffer_bytes(hw_params));
}

static int snd_sh_dac_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
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

static int snd_sh_dac_pcm_copy(struct snd_pcm_substream *substream, int channel,
	snd_pcm_uframes_t pos, void __user *src, snd_pcm_uframes_t count)
{
	/* channel is not used (interleaved data) */
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	ssize_t b_count = frames_to_bytes(runtime , count);
	ssize_t b_pos = frames_to_bytes(runtime , pos);

	if (count < 0)
		return -EINVAL;

	if (!count)
		return 0;

	memcpy_toio(chip->data_buffer + b_pos, src, b_count);
	chip->buffer_end = chip->data_buffer + b_pos + b_count;

	if (chip->empty) {
		chip->empty = 0;
		dac_audio_start_timer(chip);
	}

	return 0;
}

static int snd_sh_dac_pcm_silence(struct snd_pcm_substream *substream,
				  int channel, snd_pcm_uframes_t pos,
				  snd_pcm_uframes_t count)
{
	/* channel is not used (interleaved data) */
	struct snd_sh_dac *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	ssize_t b_count = frames_to_bytes(runtime , count);
	ssize_t b_pos = frames_to_bytes(runtime , pos);

	if (count < 0)
		return -EINVAL;

	if (!count)
		return 0;

	memset_io(chip->data_buffer + b_pos, 0, b_count);
	chip->buffer_end = chip->data_buffer + b_pos + b_count;

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
static struct snd_pcm_ops snd_sh_dac_pcm_ops = {
	.open		= snd_sh_dac_pcm_open,
	.close		= snd_sh_dac_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_sh_dac_pcm_hw_params,
	.hw_free	= snd_sh_dac_pcm_hw_free,
	.prepare	= snd_sh_dac_pcm_prepare,
	.trigger	= snd_sh_dac_pcm_trigger,
	.pointer	= snd_sh_dac_pcm_pointer,
	.copy		= snd_sh_dac_pcm_copy,
	.silence	= snd_sh_dac_pcm_silence,
	.mmap		= snd_pcm_lib_mmap_iomem,
};

static int __devinit snd_sh_dac_pcm(struct snd_sh_dac *chip, int device)
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
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					  snd_dma_continuous_data(GFP_KERNEL),
							48 * 1024,
							48 * 1024);

	return 0;
}
/* END OF PCM INTERFACE */


/* driver .remove  --  destructor */
static int snd_sh_dac_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);

	return 0;
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
static int __devinit snd_sh_dac_create(struct snd_card *card,
				       struct platform_device *devptr,
				       struct snd_sh_dac **rchip)
{
	struct snd_sh_dac *chip;
	int err;

	static struct snd_device_ops ops = {
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
static int __devinit snd_sh_dac_probe(struct platform_device *devptr)
{
	struct snd_sh_dac *chip;
	struct snd_card *card;
	int err;

	err = snd_card_create(index, id, THIS_MODULE, 0, &card);
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

	snd_printk("ALSA driver for SuperH DAC audio");

	platform_set_drvdata(devptr, card);
	return 0;

probe_error:
	snd_card_free(card);
	return err;
}

/*
 * "driver" definition
 */
static struct platform_driver driver = {
	.probe	= snd_sh_dac_probe,
	.remove = snd_sh_dac_remove,
	.driver = {
		.name = "dac_audio",
	},
};

static int __init sh_dac_init(void)
{
	return platform_driver_register(&driver);
}

static void __exit sh_dac_exit(void)
{
	platform_driver_unregister(&driver);
}

module_init(sh_dac_init);
module_exit(sh_dac_exit);
