/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctpcm.c
 *
 * @Brief
 * This file contains the definition of the pcm device functions.
 *
 * @Author	Liu Chun
 * @Date 	Apr 2 2008
 *
 */

#include "ctpcm.h"
#include "cttimer.h"
#include <linux/slab.h>
#include <sound/pcm.h>

/* Hardware descriptions for playback */
static struct snd_pcm_hardware ct_pcm_playback_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE),
	.formats		= (SNDRV_PCM_FMTBIT_U8 |
				   SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE |
				   SNDRV_PCM_FMTBIT_FLOAT_LE),
	.rates			= (SNDRV_PCM_RATE_CONTINUOUS |
				   SNDRV_PCM_RATE_8000_192000),
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (128*1024),
	.period_bytes_min	= (64),
	.period_bytes_max	= (128*1024),
	.periods_min		= 2,
	.periods_max		= 1024,
	.fifo_size		= 0,
};

static struct snd_pcm_hardware ct_spdif_passthru_playback_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_48000 |
				   SNDRV_PCM_RATE_44100 |
				   SNDRV_PCM_RATE_32000),
	.rate_min		= 32000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= (128*1024),
	.period_bytes_min	= (64),
	.period_bytes_max	= (128*1024),
	.periods_min		= 2,
	.periods_max		= 1024,
	.fifo_size		= 0,
};

/* Hardware descriptions for capture */
static struct snd_pcm_hardware ct_pcm_capture_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_PAUSE |
				   SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= (SNDRV_PCM_FMTBIT_U8 |
				   SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE |
				   SNDRV_PCM_FMTBIT_FLOAT_LE),
	.rates			= (SNDRV_PCM_RATE_CONTINUOUS |
				   SNDRV_PCM_RATE_8000_96000),
	.rate_min		= 8000,
	.rate_max		= 96000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (128*1024),
	.period_bytes_min	= (384),
	.period_bytes_max	= (64*1024),
	.periods_min		= 2,
	.periods_max		= 1024,
	.fifo_size		= 0,
};

static void ct_atc_pcm_interrupt(struct ct_atc_pcm *atc_pcm)
{
	struct ct_atc_pcm *apcm = atc_pcm;

	if (!apcm->substream)
		return;

	snd_pcm_period_elapsed(apcm->substream);
}

static void ct_atc_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	struct ct_atc_pcm *apcm = runtime->private_data;
	struct ct_atc *atc = snd_pcm_substream_chip(apcm->substream);

	atc->pcm_release_resources(atc, apcm);
	ct_timer_instance_free(apcm->timer);
	kfree(apcm);
	runtime->private_data = NULL;
}

/* pcm playback operations */
static int ct_pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm;
	int err;

	apcm = kzalloc(sizeof(*apcm), GFP_KERNEL);
	if (!apcm)
		return -ENOMEM;

	apcm->substream = substream;
	apcm->interrupt = ct_atc_pcm_interrupt;
	runtime->private_data = apcm;
	runtime->private_free = ct_atc_pcm_free_substream;
	if (IEC958 == substream->pcm->device) {
		runtime->hw = ct_spdif_passthru_playback_hw;
		atc->spdif_out_passthru(atc, 1);
	} else {
		runtime->hw = ct_pcm_playback_hw;
		if (FRONT == substream->pcm->device)
			runtime->hw.channels_max = 8;
	}

	err = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0) {
		kfree(apcm);
		return err;
	}
	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					   1024, UINT_MAX);
	if (err < 0) {
		kfree(apcm);
		return err;
	}

	apcm->timer = ct_timer_instance_new(atc->timer, apcm);
	if (!apcm->timer)
		return -ENOMEM;

	return 0;
}

static int ct_pcm_playback_close(struct snd_pcm_substream *substream)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);

	/* TODO: Notify mixer inactive. */
	if (IEC958 == substream->pcm->device)
		atc->spdif_out_passthru(atc, 0);

	/* The ct_atc_pcm object will be freed by runtime->private_free */

	return 0;
}

static int ct_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *hw_params)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct ct_atc_pcm *apcm = substream->runtime->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	/* clear previous resources */
	atc->pcm_release_resources(atc, apcm);
	return err;
}

static int ct_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct ct_atc_pcm *apcm = substream->runtime->private_data;

	/* clear previous resources */
	atc->pcm_release_resources(atc, apcm);
	/* Free snd-allocated pages */
	return snd_pcm_lib_free_pages(substream);
}


static int ct_pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	int err;
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	if (IEC958 == substream->pcm->device)
		err = atc->spdif_passthru_playback_prepare(atc, apcm);
	else
		err = atc->pcm_playback_prepare(atc, apcm);

	if (err < 0) {
		printk(KERN_ERR "ctxfi: Preparing pcm playback failed!!!\n");
		return err;
	}

	return 0;
}

static int
ct_pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		atc->pcm_playback_start(atc, apcm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		atc->pcm_playback_stop(atc, apcm);
		break;
	default:
		break;
	}

	return 0;
}

static snd_pcm_uframes_t
ct_pcm_playback_pointer(struct snd_pcm_substream *substream)
{
	unsigned long position;
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	/* Read out playback position */
	position = atc->pcm_playback_position(atc, apcm);
	position = bytes_to_frames(runtime, position);
	if (position >= runtime->buffer_size)
		position = 0;
	return position;
}

/* pcm capture operations */
static int ct_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm;
	int err;

	apcm = kzalloc(sizeof(*apcm), GFP_KERNEL);
	if (!apcm)
		return -ENOMEM;

	apcm->started = 0;
	apcm->substream = substream;
	apcm->interrupt = ct_atc_pcm_interrupt;
	runtime->private_data = apcm;
	runtime->private_free = ct_atc_pcm_free_substream;
	runtime->hw = ct_pcm_capture_hw;
	runtime->hw.rate_max = atc->rsr * atc->msr;

	err = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0) {
		kfree(apcm);
		return err;
	}
	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					   1024, UINT_MAX);
	if (err < 0) {
		kfree(apcm);
		return err;
	}

	apcm->timer = ct_timer_instance_new(atc->timer, apcm);
	if (!apcm->timer)
		return -ENOMEM;

	return 0;
}

static int ct_pcm_capture_close(struct snd_pcm_substream *substream)
{
	/* The ct_atc_pcm object will be freed by runtime->private_free */
	/* TODO: Notify mixer inactive. */
	return 0;
}

static int ct_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	int err;
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	err = atc->pcm_capture_prepare(atc, apcm);
	if (err < 0) {
		printk(KERN_ERR "ctxfi: Preparing pcm capture failed!!!\n");
		return err;
	}

	return 0;
}

static int
ct_pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		atc->pcm_capture_start(atc, apcm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		atc->pcm_capture_stop(atc, apcm);
		break;
	default:
		atc->pcm_capture_stop(atc, apcm);
		break;
	}

	return 0;
}

static snd_pcm_uframes_t
ct_pcm_capture_pointer(struct snd_pcm_substream *substream)
{
	unsigned long position;
	struct ct_atc *atc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = runtime->private_data;

	/* Read out playback position */
	position = atc->pcm_capture_position(atc, apcm);
	position = bytes_to_frames(runtime, position);
	if (position >= runtime->buffer_size)
		position = 0;
	return position;
}

/* PCM operators for playback */
static struct snd_pcm_ops ct_pcm_playback_ops = {
	.open	 	= ct_pcm_playback_open,
	.close		= ct_pcm_playback_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= ct_pcm_hw_params,
	.hw_free	= ct_pcm_hw_free,
	.prepare	= ct_pcm_playback_prepare,
	.trigger	= ct_pcm_playback_trigger,
	.pointer	= ct_pcm_playback_pointer,
	.page		= snd_pcm_sgbuf_ops_page,
};

/* PCM operators for capture */
static struct snd_pcm_ops ct_pcm_capture_ops = {
	.open	 	= ct_pcm_capture_open,
	.close		= ct_pcm_capture_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= ct_pcm_hw_params,
	.hw_free	= ct_pcm_hw_free,
	.prepare	= ct_pcm_capture_prepare,
	.trigger	= ct_pcm_capture_trigger,
	.pointer	= ct_pcm_capture_pointer,
	.page		= snd_pcm_sgbuf_ops_page,
};

/* Create ALSA pcm device */
int ct_alsa_pcm_create(struct ct_atc *atc,
		       enum CTALSADEVS device,
		       const char *device_name)
{
	struct snd_pcm *pcm;
	int err;
	int playback_count, capture_count;

	playback_count = (IEC958 == device) ? 1 : 8;
	capture_count = (FRONT == device) ? 1 : 0;
	err = snd_pcm_new(atc->card, "ctxfi", device,
			  playback_count, capture_count, &pcm);
	if (err < 0) {
		printk(KERN_ERR "ctxfi: snd_pcm_new failed!! Err=%d\n", err);
		return err;
	}

	pcm->private_data = atc;
	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strlcpy(pcm->name, device_name, sizeof(pcm->name));

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ct_pcm_playback_ops);

	if (FRONT == device)
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_CAPTURE, &ct_pcm_capture_ops);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
			snd_dma_pci_data(atc->pci), 128*1024, 128*1024);

#ifdef CONFIG_PM
	atc->pcms[device] = pcm;
#endif

	return 0;
}
