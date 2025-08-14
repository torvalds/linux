// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#include "us144mkii.h"

/**
 * tascam_capture_open() - Opens the PCM capture substream.
 * @substream: The ALSA PCM substream to open.
 *
 * This function sets the hardware parameters for the capture substream
 * and stores a reference to the substream in the driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_capture_open(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	substream->runtime->hw = tascam_pcm_hw;
	tascam->capture_substream = substream;
	atomic_set(&tascam->capture_active, 0);

	return 0;
}

/**
 * tascam_capture_close() - Closes the PCM capture substream.
 * @substream: The ALSA PCM substream to close.
 *
 * This function clears the reference to the capture substream in the
 * driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_capture_close(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	tascam->capture_substream = NULL;

	return 0;
}

/**
 * tascam_capture_prepare() - Prepares the PCM capture substream for use.
 * @substream: The ALSA PCM substream to prepare.
 *
 * This function initializes capture-related counters.
 *
 * Return: 0 on success.
 */
static int tascam_capture_prepare(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	tascam->driver_capture_pos = 0;
	tascam->capture_frames_processed = 0;
	tascam->last_capture_period_pos = 0;

	return 0;
}

/**
 * tascam_capture_pointer() - Returns the current capture pointer position.
 * @substream: The ALSA PCM substream.
 *
 * This function returns the current position of the capture pointer within
 * the ALSA ring buffer, in frames.
 *
 * Return: The current capture pointer position in frames.
 */
static snd_pcm_uframes_t
tascam_capture_pointer(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u64 pos;

	if (!atomic_read(&tascam->capture_active))
		return 0;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		pos = tascam->capture_frames_processed;
	}

	if (runtime->buffer_size == 0)
		return 0;

	return do_div(pos, runtime->buffer_size);
}

/**
 * tascam_capture_ops - ALSA PCM operations for capture.
 *
 * This structure defines the callback functions for capture stream operations,
 * including open, close, ioctl, hardware parameters, hardware free, prepare,
 * trigger, and pointer.
 */
const struct snd_pcm_ops tascam_capture_ops = {
	.open = tascam_capture_open,
	.close = tascam_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = tascam_pcm_hw_params,
	.hw_free = tascam_pcm_hw_free,
	.prepare = tascam_capture_prepare,
	.trigger = tascam_pcm_trigger,
	.pointer = tascam_capture_pointer,
};
