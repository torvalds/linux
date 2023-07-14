/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/sdw.h -- SoundWire helpers for ALSA/ASoC
 *
 * Copyright (c) 2022 Cirrus Logic Inc.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#include <linux/soundwire/sdw.h>
#include <sound/asound.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#ifndef __INCLUDE_SOUND_SDW_H
#define __INCLUDE_SOUND_SDW_H

/**
 * snd_sdw_params_to_config() - Conversion from hw_params to SoundWire config
 *
 * @substream: Pointer to the PCM substream structure
 * @params: Pointer to the hardware params structure
 * @stream_config: Stream configuration for the SoundWire audio stream
 * @port_config: Port configuration for the SoundWire audio stream
 *
 * This function provides a basic conversion from the hw_params structure to
 * SoundWire configuration structures. The user will at a minimum need to also
 * set the port number in the port config, but may also override more of the
 * setup, or in the case of a complex user, not use this helper at all and
 * open-code everything.
 */
static inline void snd_sdw_params_to_config(struct snd_pcm_substream *substream,
					    struct snd_pcm_hw_params *params,
					    struct sdw_stream_config *stream_config,
					    struct sdw_port_config *port_config)
{
	stream_config->frame_rate = params_rate(params);
	stream_config->ch_count = params_channels(params);
	stream_config->bps = snd_pcm_format_width(params_format(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stream_config->direction = SDW_DATA_DIR_RX;
	else
		stream_config->direction = SDW_DATA_DIR_TX;

	port_config->ch_mask = GENMASK(stream_config->ch_count - 1, 0);
}

#endif
