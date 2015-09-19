/*
 * AM824 format in Audio and Music Data Transmission Protocol (IEC 61883-6)
 *
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "amdtp-am824.h"

#define CIP_FMT_AM		0x10

/* "Clock-based rate control mode" is just supported. */
#define AMDTP_FDF_AM824		0x00

/**
 * amdtp_am824_set_parameters - set stream parameters
 * @s: the AMDTP stream to configure
 * @rate: the sample rate
 * @pcm_channels: the number of PCM samples in each data block, to be encoded
 *                as AM824 multi-bit linear audio
 * @midi_ports: the number of MIDI ports (i.e., MPX-MIDI Data Channels)
 * @double_pcm_frames: one data block transfers two PCM frames
 *
 * The parameters must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
int amdtp_am824_set_parameters(struct amdtp_stream *s, unsigned int rate,
			       unsigned int pcm_channels,
			       unsigned int midi_ports,
			       bool double_pcm_frames)
{
	int err;

	err = amdtp_stream_set_parameters(s, rate, pcm_channels, midi_ports);
	if (err < 0)
		return err;

	s->fdf = AMDTP_FDF_AM824 | s->sfc;

	/*
	 * In IEC 61883-6, one data block represents one event. In ALSA, one
	 * event equals to one PCM frame. But Dice has a quirk at higher
	 * sampling rate to transfer two PCM frames in one data block.
	 */
	if (double_pcm_frames)
		s->frame_multiplier = 2;
	else
		s->frame_multiplier = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(amdtp_am824_set_parameters);

/**
 * amdtp_am824_init - initialize an AMDTP stream structure to handle AM824
 *		      data block
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @dir: the direction of stream
 * @flags: the packet transmission method to use
 */
int amdtp_am824_init(struct amdtp_stream *s, struct fw_unit *unit,
		     enum amdtp_stream_direction dir, enum cip_flags flags)
{
	return amdtp_stream_init(s, unit, dir, flags, CIP_FMT_AM);
}
EXPORT_SYMBOL_GPL(amdtp_am824_init);
