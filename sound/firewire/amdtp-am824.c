// SPDX-License-Identifier: GPL-2.0-only
/*
 * AM824 format in Audio and Music Data Transmission Protocol (IEC 61883-6)
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include <linux/slab.h>

#include "amdtp-am824.h"

#define CIP_FMT_AM		0x10

/* "Clock-based rate control mode" is just supported. */
#define AMDTP_FDF_AM824		0x00

/*
 * Nominally 3125 bytes/second, but the MIDI port's clock might be
 * 1% too slow, and the bus clock 100 ppm too fast.
 */
#define MIDI_BYTES_PER_SECOND	3093

/*
 * Several devices look only at the first eight data blocks.
 * In any case, this is more than enough for the MIDI data rate.
 */
#define MAX_MIDI_RX_BLOCKS	8

struct amdtp_am824 {
	struct snd_rawmidi_substream *midi[AM824_MAX_CHANNELS_FOR_MIDI * 8];
	int midi_fifo_limit;
	int midi_fifo_used[AM824_MAX_CHANNELS_FOR_MIDI * 8];
	unsigned int pcm_channels;
	unsigned int midi_ports;

	u8 pcm_positions[AM824_MAX_CHANNELS_FOR_PCM];
	u8 midi_position;

	unsigned int frame_multiplier;
};

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
	struct amdtp_am824 *p = s->protocol;
	unsigned int midi_channels;
	unsigned int i;
	int err;

	if (amdtp_stream_running(s))
		return -EINVAL;

	if (pcm_channels > AM824_MAX_CHANNELS_FOR_PCM)
		return -EINVAL;

	midi_channels = DIV_ROUND_UP(midi_ports, 8);
	if (midi_channels > AM824_MAX_CHANNELS_FOR_MIDI)
		return -EINVAL;

	if (WARN_ON(amdtp_stream_running(s)) ||
	    WARN_ON(pcm_channels > AM824_MAX_CHANNELS_FOR_PCM) ||
	    WARN_ON(midi_channels > AM824_MAX_CHANNELS_FOR_MIDI))
		return -EINVAL;

	err = amdtp_stream_set_parameters(s, rate,
					  pcm_channels + midi_channels);
	if (err < 0)
		return err;

	s->ctx_data.rx.fdf = AMDTP_FDF_AM824 | s->sfc;

	p->pcm_channels = pcm_channels;
	p->midi_ports = midi_ports;

	/*
	 * In IEC 61883-6, one data block represents one event. In ALSA, one
	 * event equals to one PCM frame. But Dice has a quirk at higher
	 * sampling rate to transfer two PCM frames in one data block.
	 */
	if (double_pcm_frames)
		p->frame_multiplier = 2;
	else
		p->frame_multiplier = 1;

	/* init the position map for PCM and MIDI channels */
	for (i = 0; i < pcm_channels; i++)
		p->pcm_positions[i] = i;
	p->midi_position = p->pcm_channels;

	/*
	 * We do not know the actual MIDI FIFO size of most devices.  Just
	 * assume two bytes, i.e., one byte can be received over the bus while
	 * the previous one is transmitted over MIDI.
	 * (The value here is adjusted for midi_ratelimit_per_packet().)
	 */
	p->midi_fifo_limit = rate - MIDI_BYTES_PER_SECOND * s->syt_interval + 1;

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
	struct amdtp_am824 *p = s->protocol;

	if (index < p->pcm_channels)
		p->pcm_positions[index] = position;
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
	struct amdtp_am824 *p = s->protocol;

	p->midi_position = position;
}
EXPORT_SYMBOL_GPL(amdtp_am824_set_midi_position);

static void write_pcm_s32(struct amdtp_stream *s,
			  struct snd_pcm_substream *pcm,
			  __be32 *buffer, unsigned int frames)
{
	struct amdtp_am824 *p = s->protocol;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u32 *src;

	channels = p->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			buffer[p->pcm_positions[c]] =
					cpu_to_be32((*src >> 8) | 0x40000000);
			src++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void read_pcm_s32(struct amdtp_stream *s,
			 struct snd_pcm_substream *pcm,
			 __be32 *buffer, unsigned int frames)
{
	struct amdtp_am824 *p = s->protocol;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	u32 *dst;

	channels = p->pcm_channels;
	dst  = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*dst = be32_to_cpu(buffer[p->pcm_positions[c]]) << 8;
			dst++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void write_pcm_silence(struct amdtp_stream *s,
			      __be32 *buffer, unsigned int frames)
{
	struct amdtp_am824 *p = s->protocol;
	unsigned int i, c, channels = p->pcm_channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c)
			buffer[p->pcm_positions[c]] = cpu_to_be32(0x40000000);
		buffer += s->data_block_quadlets;
	}
}

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
	struct amdtp_am824 *p = s->protocol;

	if (port < p->midi_ports)
		WRITE_ONCE(p->midi[port], midi);
}
EXPORT_SYMBOL_GPL(amdtp_am824_midi_trigger);

/*
 * To avoid sending MIDI bytes at too high a rate, assume that the receiving
 * device has a FIFO, and track how much it is filled.  This values increases
 * by one whenever we send one byte in a packet, but the FIFO empties at
 * a constant rate independent of our packet rate.  One packet has syt_interval
 * samples, so the number of bytes that empty out of the FIFO, per packet(!),
 * is MIDI_BYTES_PER_SECOND * syt_interval / sample_rate.  To avoid storing
 * fractional values, the values in midi_fifo_used[] are measured in bytes
 * multiplied by the sample rate.
 */
static bool midi_ratelimit_per_packet(struct amdtp_stream *s, unsigned int port)
{
	struct amdtp_am824 *p = s->protocol;
	int used;

	used = p->midi_fifo_used[port];
	if (used == 0) /* common shortcut */
		return true;

	used -= MIDI_BYTES_PER_SECOND * s->syt_interval;
	used = max(used, 0);
	p->midi_fifo_used[port] = used;

	return used < p->midi_fifo_limit;
}

static void midi_rate_use_one_byte(struct amdtp_stream *s, unsigned int port)
{
	struct amdtp_am824 *p = s->protocol;

	p->midi_fifo_used[port] += amdtp_rate_table[s->sfc];
}

static void write_midi_messages(struct amdtp_stream *s, __be32 *buffer,
				unsigned int frames)
{
	struct amdtp_am824 *p = s->protocol;
	unsigned int f, port;
	u8 *b;

	for (f = 0; f < frames; f++) {
		b = (u8 *)&buffer[p->midi_position];

		port = (s->data_block_counter + f) % 8;
		if (f < MAX_MIDI_RX_BLOCKS &&
		    midi_ratelimit_per_packet(s, port) &&
		    p->midi[port] != NULL &&
		    snd_rawmidi_transmit(p->midi[port], &b[1], 1) == 1) {
			midi_rate_use_one_byte(s, port);
			b[0] = 0x81;
		} else {
			b[0] = 0x80;
			b[1] = 0;
		}
		b[2] = 0;
		b[3] = 0;

		buffer += s->data_block_quadlets;
	}
}

static void read_midi_messages(struct amdtp_stream *s,
			       __be32 *buffer, unsigned int frames)
{
	struct amdtp_am824 *p = s->protocol;
	unsigned int f, port;
	int len;
	u8 *b;

	for (f = 0; f < frames; f++) {
		port = (8 - s->ctx_data.tx.first_dbc + s->data_block_counter + f) % 8;
		b = (u8 *)&buffer[p->midi_position];

		len = b[0] - 0x80;
		if ((1 <= len) &&  (len <= 3) && (p->midi[port]))
			snd_rawmidi_receive(p->midi[port], b + 1, len);

		buffer += s->data_block_quadlets;
	}
}

static unsigned int process_rx_data_blocks(struct amdtp_stream *s, __be32 *buffer,
					   unsigned int data_blocks, unsigned int *syt)
{
	struct amdtp_am824 *p = s->protocol;
	struct snd_pcm_substream *pcm = READ_ONCE(s->pcm);
	unsigned int pcm_frames;

	if (pcm) {
		write_pcm_s32(s, pcm, buffer, data_blocks);
		pcm_frames = data_blocks * p->frame_multiplier;
	} else {
		write_pcm_silence(s, buffer, data_blocks);
		pcm_frames = 0;
	}

	if (p->midi_ports)
		write_midi_messages(s, buffer, data_blocks);

	return pcm_frames;
}

static unsigned int process_tx_data_blocks(struct amdtp_stream *s, __be32 *buffer,
					   unsigned int data_blocks, unsigned int *syt)
{
	struct amdtp_am824 *p = s->protocol;
	struct snd_pcm_substream *pcm = READ_ONCE(s->pcm);
	unsigned int pcm_frames;

	if (pcm) {
		read_pcm_s32(s, pcm, buffer, data_blocks);
		pcm_frames = data_blocks * p->frame_multiplier;
	} else {
		pcm_frames = 0;
	}

	if (p->midi_ports)
		read_midi_messages(s, buffer, data_blocks);

	return pcm_frames;
}

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
	amdtp_stream_process_data_blocks_t process_data_blocks;

	if (dir == AMDTP_IN_STREAM)
		process_data_blocks = process_tx_data_blocks;
	else
		process_data_blocks = process_rx_data_blocks;

	return amdtp_stream_init(s, unit, dir, flags, CIP_FMT_AM,
				 process_data_blocks,
				 sizeof(struct amdtp_am824));
}
EXPORT_SYMBOL_GPL(amdtp_am824_init);
