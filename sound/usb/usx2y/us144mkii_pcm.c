// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#include "us144mkii.h"

const struct snd_pcm_hardware tascam_pcm_hw = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats = SNDRV_PCM_FMTBIT_S24_3LE,
	.rates = (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000),
	.rate_min = 44100,
	.rate_max = 96000,
	.channels_min = NUM_CHANNELS,
	.channels_max = NUM_CHANNELS,
	.buffer_bytes_max = 1024 * 1024,
	.period_bytes_min = 48 * BYTES_PER_FRAME,
	.period_bytes_max = 1024 * BYTES_PER_FRAME,
	.periods_min = 2,
	.periods_max = 1024,
};

int tascam_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	return 0;
}

int tascam_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

int tascam_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);
	int err = 0;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
			if (!atomic_read(&tascam->playback_active)) {
				atomic_set(&tascam->playback_active, 1);
				atomic_set(&tascam->capture_active, 1);
			}
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (atomic_read(&tascam->playback_active)) {
				atomic_set(&tascam->playback_active, 0);
				atomic_set(&tascam->capture_active, 0);
			}
			break;
		default:
			err = -EINVAL;
			break;
		}
	}

	return err;
}

int tascam_init_pcm(struct snd_pcm *pcm)
{
	struct tascam_card *tascam = pcm->private_data;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &tascam_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &tascam_capture_ops);

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
				       tascam->dev->dev.parent, 64 * 1024,
				       tascam_pcm_hw.buffer_bytes_max);

	return 0;
}
