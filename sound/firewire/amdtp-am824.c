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
 * amdtp_am824_set_pcm_position - set an index of data channel for a channel
 *				  of PCM frame
 * @s: the AMDTP stream
 * @index: the index of data channel in an data block
 * @position: the channel of PCM frame
 */
void amdtp_am824_set_pcm_position(struct amdtp_stream *s, unsigned int index,
				 unsigned int position)
{
	if (index < s->pcm_channels)
		s->pcm_positions[index] = position;
}
EXPORT_SYMBOL_GPL(amdtp_am824_set_pcm_position);

/**
 * amdtp_am824_set_midi_position - set a index of data channel for MIDI
 *				   conformant data channel
 * @s: the AMDTP stream
 * @position: the index of data channel in an data block
 */
void amdtp_am824_set_midi_position(struct amdtp_stream *s,
				   unsigned int position)
{
	s->midi_position = position;
}
EXPORT_SYMBOL_GPL(amdtp_am824_set_midi_position);

/**
 * amdtp_am824_add_pcm_hw_constraints - add hw constraints for PCM substream
 * @s:		the AMDTP stream for AM824 data block, must be initialized.
 * @runtime:	the PCM substream runtime
 *
 */
int amdtp_am824_add_pcm_hw_constraints(struct amdtp_stream *s,
				       struct snd_pcm_runtime *runtime)
{
	int err;

	err = amdtp_stream_add_pcm_hw_constraints(s, runtime);
	if (err < 0)
		return err;

	/* AM824 in IEC 61883-6 can deliver 24bit data. */
	return snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
}
EXPORT_SYMBOL_GPL(amdtp_am824_add_pcm_hw_constraints);

/**
 * amdtp_am824_midi_trigger - start/stop playback/capture with a MIDI device
 * @s: the AMDTP stream
 * @port: index of MIDI port
 * @midi: the MIDI device to be started, or %NULL to stop the current device
 *
 * Call this function on a running isochronous stream to enable the actual
 * transmission of MIDI data.  This function should be called from the MIDI
 * device's .trigger callback.
 */
void amdtp_am824_midi_trigger(struct amdtp_stream *s, unsigned int port,
			      struct snd_rawmidi_substream *midi)
{
	if (port < s->midi_ports)
		ACCESS_ONCE(s->midi[port]) = midi;
}
EXPORT_SYMBOL_GPL(amdtp_am824_midi_trigger);

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
