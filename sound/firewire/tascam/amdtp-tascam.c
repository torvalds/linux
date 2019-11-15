// SPDX-License-Identifier: GPL-2.0-only
/*
 * amdtp-tascam.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 */

#include <sound/pcm.h>
#include "tascam.h"

#define AMDTP_FMT_TSCM_TX	0x1e
#define AMDTP_FMT_TSCM_RX	0x3e

struct amdtp_tscm {
	unsigned int pcm_channels;
};

int amdtp_tscm_set_parameters(struct amdtp_stream *s, unsigned int rate)
{
	struct amdtp_tscm *p = s->protocol;
	unsigned int data_channels;

	if (amdtp_stream_running(s))
		return -EBUSY;

	data_channels = p->pcm_channels;

	/* Packets in in-stream have extra 2 data channels. */
	if (s->direction == AMDTP_IN_STREAM)
		data_channels += 2;

	return amdtp_stream_set_parameters(s, rate, data_channels);
}

static void write_pcm_s32(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			  __be32 *buffer, unsigned int frames,
			  unsigned int pcm_frames)
{
	struct amdtp_tscm *p = s->protocol;
	unsigned int channels = p->pcm_channels;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int pcm_buffer_pointer;
	int remaining_frames;
	const u32 *src;
	int i, c;

	pcm_buffer_pointer = s->pcm_buffer_pointer + pcm_frames;
	pcm_buffer_pointer %= runtime->buffer_size;

	src = (void *)runtime->dma_area +
				frames_to_bytes(runtime, pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			buffer[c] = cpu_to_be32(*src);
			src++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void read_pcm_s32(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			 __be32 *buffer, unsigned int frames,
			 unsigned int pcm_frames)
{
	struct amdtp_tscm *p = s->protocol;
	unsigned int channels = p->pcm_channels;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int pcm_buffer_pointer;
	int remaining_frames;
	u32 *dst;
	int i, c;

	pcm_buffer_pointer = s->pcm_buffer_pointer + pcm_frames;
	pcm_buffer_pointer %= runtime->buffer_size;

	dst  = (void *)runtime->dma_area +
				frames_to_bytes(runtime, pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - pcm_buffer_pointer;

	/* The first data channel is for event counter. */
	buffer += 1;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*dst = be32_to_cpu(buffer[c]);
			dst++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void write_pcm_silence(struct amdtp_stream *s, __be32 *buffer,
			      unsigned int data_blocks)
{
	struct amdtp_tscm *p = s->protocol;
	unsigned int channels, i, c;

	channels = p->pcm_channels;

	for (i = 0; i < data_blocks; ++i) {
		for (c = 0; c < channels; ++c)
			buffer[c] = 0x00000000;
		buffer += s->data_block_quadlets;
	}
}

int amdtp_tscm_add_pcm_hw_constraints(struct amdtp_stream *s,
				      struct snd_pcm_runtime *runtime)
{
	int err;

	/*
	 * Our implementation allows this protocol to deliver 24 bit sample in
	 * 32bit data channel.
	 */
	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		return err;

	return amdtp_stream_add_pcm_hw_constraints(s, runtime);
}

static void read_status_messages(struct amdtp_stream *s,
				 __be32 *buffer, unsigned int data_blocks)
{
	struct snd_tscm *tscm = container_of(s, struct snd_tscm, tx_stream);
	bool used = READ_ONCE(tscm->hwdep->used);
	int i;

	for (i = 0; i < data_blocks; i++) {
		unsigned int index;
		__be32 before;
		__be32 after;

		index = be32_to_cpu(buffer[0]) % SNDRV_FIREWIRE_TASCAM_STATE_COUNT;
		before = tscm->state[index];
		after = buffer[s->data_block_quadlets - 1];

		if (used && index > 4 && index < 16) {
			__be32 mask;

			if (index == 5)
				mask = cpu_to_be32(~0x0000ffff);
			else if (index == 6)
				mask = cpu_to_be32(~0x0000ffff);
			else if (index == 8)
				mask = cpu_to_be32(~0x000f0f00);
			else
				mask = cpu_to_be32(~0x00000000);

			if ((before ^ after) & mask) {
				struct snd_firewire_tascam_change *entry =
						&tscm->queue[tscm->push_pos];

				spin_lock_irq(&tscm->lock);
				entry->index = index;
				entry->before = before;
				entry->after = after;
				if (++tscm->push_pos >= SND_TSCM_QUEUE_COUNT)
					tscm->push_pos = 0;
				spin_unlock_irq(&tscm->lock);

				wake_up(&tscm->hwdep_wait);
			}
		}

		tscm->state[index] = after;
		buffer += s->data_block_quadlets;
	}
}

static unsigned int process_ir_ctx_payloads(struct amdtp_stream *s,
					    const struct pkt_desc *descs,
					    unsigned int packets,
					    struct snd_pcm_substream *pcm)
{
	unsigned int pcm_frames = 0;
	int i;

	for (i = 0; i < packets; ++i) {
		const struct pkt_desc *desc = descs + i;
		__be32 *buf = desc->ctx_payload;
		unsigned int data_blocks = desc->data_blocks;

		if (pcm) {
			read_pcm_s32(s, pcm, buf, data_blocks, pcm_frames);
			pcm_frames += data_blocks;
		}

		read_status_messages(s, buf, data_blocks);
	}

	return pcm_frames;
}

static unsigned int process_it_ctx_payloads(struct amdtp_stream *s,
					    const struct pkt_desc *descs,
					    unsigned int packets,
					    struct snd_pcm_substream *pcm)
{
	unsigned int pcm_frames = 0;
	int i;

	for (i = 0; i < packets; ++i) {
		const struct pkt_desc *desc = descs + i;
		__be32 *buf = desc->ctx_payload;
		unsigned int data_blocks = desc->data_blocks;

		if (pcm) {
			write_pcm_s32(s, pcm, buf, data_blocks, pcm_frames);
			pcm_frames += data_blocks;
		} else {
			write_pcm_silence(s, buf, data_blocks);
		}
	}

	return pcm_frames;
}

int amdtp_tscm_init(struct amdtp_stream *s, struct fw_unit *unit,
		    enum amdtp_stream_direction dir, unsigned int pcm_channels)
{
	amdtp_stream_process_ctx_payloads_t process_ctx_payloads;
	struct amdtp_tscm *p;
	unsigned int fmt;
	int err;

	if (dir == AMDTP_IN_STREAM) {
		fmt = AMDTP_FMT_TSCM_TX;
		process_ctx_payloads = process_ir_ctx_payloads;
	} else {
		fmt = AMDTP_FMT_TSCM_RX;
		process_ctx_payloads = process_it_ctx_payloads;
	}

	err = amdtp_stream_init(s, unit, dir,
			CIP_NONBLOCKING | CIP_SKIP_DBC_ZERO_CHECK, fmt,
			process_ctx_payloads, sizeof(struct amdtp_tscm));
	if (err < 0)
		return 0;

	if (dir == AMDTP_OUT_STREAM) {
		// Use fixed value for FDF field.
		s->ctx_data.rx.fdf = 0x00;
		// Not used.
		s->ctx_data.rx.syt_override = 0x0000;
	}

	/* This protocol uses fixed number of data channels for PCM samples. */
	p = s->protocol;
	p->pcm_channels = pcm_channels;

	return 0;
}
