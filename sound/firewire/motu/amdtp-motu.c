/*
 * amdtp-motu.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/slab.h>
#include <sound/pcm.h>
#include "motu.h"

#define CIP_FMT_MOTU		0x02
#define MOTU_FDF_AM824		0x22

struct amdtp_motu {
	/* For timestamp processing.  */
	unsigned int quotient_ticks_per_event;
	unsigned int remainder_ticks_per_event;
	unsigned int next_ticks;
	unsigned int next_accumulated;
	unsigned int next_cycles;
	unsigned int next_seconds;

	unsigned int pcm_chunks;
	unsigned int pcm_byte_offset;
};

int amdtp_motu_set_parameters(struct amdtp_stream *s, unsigned int rate,
			      struct snd_motu_packet_format *formats)
{
	static const struct {
		unsigned int quotient_ticks_per_event;
		unsigned int remainder_ticks_per_event;
	} params[] = {
		[CIP_SFC_44100]  = { 557, 123 },
		[CIP_SFC_48000]  = { 512,   0 },
		[CIP_SFC_88200]  = { 278, 282 },
		[CIP_SFC_96000]  = { 256,   0 },
		[CIP_SFC_176400] = { 139, 141 },
		[CIP_SFC_192000] = { 128,   0 },
	};
	struct amdtp_motu *p = s->protocol;
	unsigned int pcm_chunks, data_chunks, data_block_quadlets;
	unsigned int delay;
	unsigned int mode;
	int i, err;

	if (amdtp_stream_running(s))
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		if (snd_motu_clock_rates[i] == rate) {
			mode = i >> 1;
			break;
		}
	}
	if (i == ARRAY_SIZE(snd_motu_clock_rates))
		return -EINVAL;

	pcm_chunks = formats->fixed_part_pcm_chunks[mode] +
		     formats->differed_part_pcm_chunks[mode];
	data_chunks = formats->msg_chunks + pcm_chunks;

	/*
	 * Each data block includes SPH in its head. Data chunks follow with
	 * 3 byte alignment. Padding follows with zero to conform to quadlet
	 * alignment.
	 */
	data_block_quadlets = 1 + DIV_ROUND_UP(data_chunks * 3, 4);

	err = amdtp_stream_set_parameters(s, rate, data_block_quadlets);
	if (err < 0)
		return err;

	p->pcm_chunks = pcm_chunks;
	p->pcm_byte_offset = formats->pcm_byte_offset;

	/* IEEE 1394 bus requires. */
	delay = 0x2e00;

	/* For no-data or empty packets to adjust PCM sampling frequency. */
	delay += 8000 * 3072 * s->syt_interval / rate;

	p->next_seconds = 0;
	p->next_cycles = delay / 3072;
	p->quotient_ticks_per_event = params[s->sfc].quotient_ticks_per_event;
	p->remainder_ticks_per_event = params[s->sfc].remainder_ticks_per_event;
	p->next_ticks = delay % 3072;
	p->next_accumulated = 0;

	return 0;
}

static void read_pcm_s32(struct amdtp_stream *s,
			 struct snd_pcm_runtime *runtime,
			 __be32 *buffer, unsigned int data_blocks)
{
	struct amdtp_motu *p = s->protocol;
	unsigned int channels, remaining_frames, i, c;
	u8 *byte;
	u32 *dst;

	channels = p->pcm_chunks;
	dst = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < data_blocks; ++i) {
		byte = (u8 *)buffer + p->pcm_byte_offset;

		for (c = 0; c < channels; ++c) {
			*dst = (byte[0] << 24) | (byte[1] << 16) | byte[2];
			byte += 3;
			dst++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void write_pcm_s32(struct amdtp_stream *s,
			  struct snd_pcm_runtime *runtime,
			  __be32 *buffer, unsigned int data_blocks)
{
	struct amdtp_motu *p = s->protocol;
	unsigned int channels, remaining_frames, i, c;
	u8 *byte;
	const u32 *src;

	channels = p->pcm_chunks;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < data_blocks; ++i) {
		byte = (u8 *)buffer + p->pcm_byte_offset;

		for (c = 0; c < channels; ++c) {
			byte[0] = (*src >> 24) & 0xff;
			byte[1] = (*src >> 16) & 0xff;
			byte[2] = (*src >>  8) & 0xff;
			byte += 3;
			src++;
		}

		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void write_pcm_silence(struct amdtp_stream *s, __be32 *buffer,
			      unsigned int data_blocks)
{
	struct amdtp_motu *p = s->protocol;
	unsigned int channels, i, c;
	u8 *byte;

	channels = p->pcm_chunks;

	for (i = 0; i < data_blocks; ++i) {
		byte = (u8 *)buffer + p->pcm_byte_offset;

		for (c = 0; c < channels; ++c) {
			byte[0] = 0;
			byte[1] = 0;
			byte[2] = 0;
			byte += 3;
		}

		buffer += s->data_block_quadlets;
	}
}

int amdtp_motu_add_pcm_hw_constraints(struct amdtp_stream *s,
				      struct snd_pcm_runtime *runtime)
{
	int err;

	/* TODO: how to set an constraint for exactly 24bit PCM sample? */
	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		return err;

	return amdtp_stream_add_pcm_hw_constraints(s, runtime);
}

static unsigned int process_tx_data_blocks(struct amdtp_stream *s,
				__be32 *buffer, unsigned int data_blocks,
				unsigned int *syt)
{
	struct snd_pcm_substream *pcm;

	pcm = ACCESS_ONCE(s->pcm);
	if (data_blocks > 0 && pcm)
		read_pcm_s32(s, pcm->runtime, buffer, data_blocks);

	return data_blocks;
}

static inline void compute_next_elapse_from_start(struct amdtp_motu *p)
{
	p->next_accumulated += p->remainder_ticks_per_event;
	if (p->next_accumulated >= 441) {
		p->next_accumulated -= 441;
		p->next_ticks++;
	}

	p->next_ticks += p->quotient_ticks_per_event;
	if (p->next_ticks >= 3072) {
		p->next_ticks -= 3072;
		p->next_cycles++;
	}

	if (p->next_cycles >= 8000) {
		p->next_cycles -= 8000;
		p->next_seconds++;
	}

	if (p->next_seconds >= 128)
		p->next_seconds -= 128;
}

static void write_sph(struct amdtp_stream *s, __be32 *buffer,
		      unsigned int data_blocks)
{
	struct amdtp_motu *p = s->protocol;
	unsigned int next_cycles;
	unsigned int i;
	u32 sph;

	for (i = 0; i < data_blocks; i++) {
		next_cycles = (s->start_cycle + p->next_cycles) % 8000;
		sph = ((next_cycles << 12) | p->next_ticks) & 0x01ffffff;
		*buffer = cpu_to_be32(sph);

		compute_next_elapse_from_start(p);

		buffer += s->data_block_quadlets;
	}
}

static unsigned int process_rx_data_blocks(struct amdtp_stream *s,
				__be32 *buffer, unsigned int data_blocks,
				unsigned int *syt)
{
	struct snd_pcm_substream *pcm;

	/* Not used. */
	*syt = 0xffff;

	/* TODO: how to interact control messages between userspace? */

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm)
		write_pcm_s32(s, pcm->runtime, buffer, data_blocks);
	else
		write_pcm_silence(s, buffer, data_blocks);

	write_sph(s, buffer, data_blocks);

	return data_blocks;
}

int amdtp_motu_init(struct amdtp_stream *s, struct fw_unit *unit,
		    enum amdtp_stream_direction dir,
		    const struct snd_motu_protocol *const protocol)
{
	amdtp_stream_process_data_blocks_t process_data_blocks;
	int fmt = CIP_FMT_MOTU;
	int flags = CIP_BLOCKING;
	int err;

	if (dir == AMDTP_IN_STREAM) {
		process_data_blocks = process_tx_data_blocks;
	} else {
		process_data_blocks = process_rx_data_blocks;
		flags |= CIP_DBC_IS_END_EVENT;
	}

	err = amdtp_stream_init(s, unit, dir, flags, fmt, process_data_blocks,
				sizeof(struct amdtp_motu));
	if (err < 0)
		return err;

	s->sph = 1;
	s->fdf = MOTU_FDF_AM824;

	return 0;
}
