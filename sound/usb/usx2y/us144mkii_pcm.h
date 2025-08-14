/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#ifndef __US144MKII_PCM_H
#define __US144MKII_PCM_H

#include "us144mkii.h"

/**
 * tascam_pcm_hw - Hardware capabilities for TASCAM US-144MKII PCM.
 *
 * Defines the supported PCM formats, rates, channels, and buffer/period sizes
 * for the TASCAM US-144MKII audio interface.
 */
extern const struct snd_pcm_hardware tascam_pcm_hw;

/**
 * tascam_playback_ops - ALSA PCM operations for playback.
 *
 * This structure defines the callback functions for playback stream operations.
 */
extern const struct snd_pcm_ops tascam_playback_ops;

/**
 * tascam_capture_ops - ALSA PCM operations for capture.
 *
 * This structure defines the callback functions for capture stream operations.
 */
extern const struct snd_pcm_ops tascam_capture_ops;

/**
 * tascam_init_pcm() - Initializes the ALSA PCM device.
 * @pcm: Pointer to the ALSA PCM device to initialize.
 *
 * This function sets up the PCM operations and preallocates pages for the
 * PCM buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_init_pcm(struct snd_pcm *pcm);

/**
 * tascam_pcm_hw_params() - Configures hardware parameters for PCM streams.
 * @substream: The ALSA PCM substream.
 * @params: The hardware parameters to apply.
 *
 * This function is a stub for handling hardware parameter configuration.
 *
 * Return: 0 on success.
 */
int tascam_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params);

/**
 * tascam_pcm_hw_free() - Frees hardware parameters for PCM streams.
 * @substream: The ALSA PCM substream.
 *
 * This function is a stub for freeing hardware-related resources.
 *
 * Return: 0 on success.
 */
int tascam_pcm_hw_free(struct snd_pcm_substream *substream);

/**
 * tascam_pcm_trigger() - Triggers the start or stop of PCM streams.
 * @substream: The ALSA PCM substream.
 * @cmd: The trigger command (e.g., SNDRV_PCM_TRIGGER_START).
 *
 * This function handles starting and stopping of playback and capture streams
 * by setting atomic flags.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_pcm_trigger(struct snd_pcm_substream *substream, int cmd);

#endif /* __US144MKII_PCM_H */
