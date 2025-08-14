// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#include "us144mkii.h"

/**
 * tascam_playback_open() - Opens the PCM playback substream.
 * @substream: The ALSA PCM substream to open.
 *
 * This function sets the hardware parameters for the playback substream
 * and stores a reference to the substream in the driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_playback_open(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	substream->runtime->hw = tascam_pcm_hw;
	tascam->playback_substream = substream;
	atomic_set(&tascam->playback_active, 0);

	return 0;
}

/**
 * tascam_playback_close() - Closes the PCM playback substream.
 * @substream: The ALSA PCM substream to close.
 *
 * This function clears the reference to the playback substream in the
 * driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_playback_close(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	tascam->playback_substream = NULL;

	return 0;
}

/**
 * tascam_playback_prepare() - Prepares the PCM playback substream for use.
 * @substream: The ALSA PCM substream to prepare.
 *
 * This function initializes playback-related counters.
 *
 * Return: 0 on success.
 */
static int tascam_playback_prepare(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	tascam->driver_playback_pos = 0;
	tascam->playback_frames_consumed = 0;

	return 0;
}

/**
 * tascam_playback_pointer() - Returns the current playback pointer position.
 * @substream: The ALSA PCM substream.
 *
 * This function returns the current position of the playback pointer within
 * the ALSA ring buffer, in frames.
 *
 * Return: The current playback pointer position in frames.
 */
static snd_pcm_uframes_t
tascam_playback_pointer(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u64 pos;

	if (!atomic_read(&tascam->playback_active))
		return 0;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		pos = tascam->playback_frames_consumed;
	}

	if (runtime->buffer_size == 0)
		return 0;

	return do_div(pos, runtime->buffer_size);
}

/**
 * tascam_playback_ops - ALSA PCM operations for playback.
 *
 * This structure defines the callback functions for playback stream operations,
 * including open, close, ioctl, hardware parameters, hardware free, prepare,
 * trigger, and pointer.
 */
const struct snd_pcm_ops tascam_playback_ops = {
	.open = tascam_playback_open,
	.close = tascam_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = tascam_pcm_hw_params,
	.hw_free = tascam_pcm_hw_free,
	.prepare = tascam_playback_prepare,
	.trigger = tascam_pcm_trigger,
	.pointer = tascam_playback_pointer,
};
