/*
 * Audio and Music Data Transmission Protocol (IEC 61883-6) streams
 * with Common Isochronous Packet (IEC 61883-1) headers
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/firewire.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include "amdtp.h"

#define TICKS_PER_CYCLE		3072
#define CYCLES_PER_SECOND	8000
#define TICKS_PER_SECOND	(TICKS_PER_CYCLE * CYCLES_PER_SECOND)

#define TRANSFER_DELAY_TICKS	0x2e00 /* 479.17 µs */

#define TAG_CIP			1

#define CIP_EOH			(1u << 31)
#define CIP_FMT_AM		(0x10 << 24)
#define AMDTP_FDF_AM824		(0 << 19)
#define AMDTP_FDF_SFC_SHIFT	16

/* TODO: make these configurable */
#define INTERRUPT_INTERVAL	16
#define QUEUE_LENGTH		48

/**
 * amdtp_out_stream_init - initialize an AMDTP output stream structure
 * @s: the AMDTP output stream to initialize
 * @unit: the target of the stream
 * @flags: the packet transmission method to use
 */
int amdtp_out_stream_init(struct amdtp_out_stream *s, struct fw_unit *unit,
			  enum cip_out_flags flags)
{
	if (flags != CIP_NONBLOCKING)
		return -EINVAL;

	s->unit = fw_unit_get(unit);
	s->flags = flags;
	s->context = ERR_PTR(-1);
	mutex_init(&s->mutex);
	s->packet_index = 0;

	return 0;
}
EXPORT_SYMBOL(amdtp_out_stream_init);

/**
 * amdtp_out_stream_destroy - free stream resources
 * @s: the AMDTP output stream to destroy
 */
void amdtp_out_stream_destroy(struct amdtp_out_stream *s)
{
	WARN_ON(!IS_ERR(s->context));
	mutex_destroy(&s->mutex);
	fw_unit_put(s->unit);
}
EXPORT_SYMBOL(amdtp_out_stream_destroy);

/**
 * amdtp_out_stream_set_rate - set the sample rate
 * @s: the AMDTP output stream to configure
 * @rate: the sample rate
 *
 * The sample rate must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
void amdtp_out_stream_set_rate(struct amdtp_out_stream *s, unsigned int rate)
{
	static const struct {
		unsigned int rate;
		unsigned int syt_interval;
	} rate_info[] = {
		[CIP_SFC_32000]  = {  32000,  8, },
		[CIP_SFC_44100]  = {  44100,  8, },
		[CIP_SFC_48000]  = {  48000,  8, },
		[CIP_SFC_88200]  = {  88200, 16, },
		[CIP_SFC_96000]  = {  96000, 16, },
		[CIP_SFC_176400] = { 176400, 32, },
		[CIP_SFC_192000] = { 192000, 32, },
	};
	unsigned int sfc;

	if (WARN_ON(!IS_ERR(s->context)))
		return;

	for (sfc = 0; sfc < ARRAY_SIZE(rate_info); ++sfc)
		if (rate_info[sfc].rate == rate) {
			s->sfc = sfc;
			s->syt_interval = rate_info[sfc].syt_interval;
			return;
		}
	WARN_ON(1);
}
EXPORT_SYMBOL(amdtp_out_stream_set_rate);

/**
 * amdtp_out_stream_get_max_payload - get the stream's packet size
 * @s: the AMDTP output stream
 *
 * This function must not be called before the stream has been configured
 * with amdtp_out_stream_set_hw_params(), amdtp_out_stream_set_pcm(), and
 * amdtp_out_stream_set_midi().
 */
unsigned int amdtp_out_stream_get_max_payload(struct amdtp_out_stream *s)
{
	static const unsigned int max_data_blocks[] = {
		[CIP_SFC_32000]  =  4,
		[CIP_SFC_44100]  =  6,
		[CIP_SFC_48000]  =  6,
		[CIP_SFC_88200]  = 12,
		[CIP_SFC_96000]  = 12,
		[CIP_SFC_176400] = 23,
		[CIP_SFC_192000] = 24,
	};

	s->data_block_quadlets = s->pcm_channels;
	s->data_block_quadlets += DIV_ROUND_UP(s->midi_ports, 8);

	return 8 + max_data_blocks[s->sfc] * 4 * s->data_block_quadlets;
}
EXPORT_SYMBOL(amdtp_out_stream_get_max_payload);

static void amdtp_write_s16(struct amdtp_out_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames);
static void amdtp_write_s32(struct amdtp_out_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames);

/**
 * amdtp_out_stream_set_pcm_format - set the PCM format
 * @s: the AMDTP output stream to configure
 * @format: the format of the ALSA PCM device
 *
 * The sample format must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
void amdtp_out_stream_set_pcm_format(struct amdtp_out_stream *s,
				     snd_pcm_format_t format)
{
	if (WARN_ON(!IS_ERR(s->context)))
		return;

	switch (format) {
	default:
		WARN_ON(1);
		/* fall through */
	case SNDRV_PCM_FORMAT_S16:
		s->transfer_samples = amdtp_write_s16;
		break;
	case SNDRV_PCM_FORMAT_S32:
		s->transfer_samples = amdtp_write_s32;
		break;
	}
}
EXPORT_SYMBOL(amdtp_out_stream_set_pcm_format);

static unsigned int calculate_data_blocks(struct amdtp_out_stream *s)
{
	unsigned int phase, data_blocks;

	if (!cip_sfc_is_base_44100(s->sfc)) {
		/* Sample_rate / 8000 is an integer, and precomputed. */
		data_blocks = s->data_block_state;
	} else {
		phase = s->data_block_state;

		/*
		 * This calculates the number of data blocks per packet so that
		 * 1) the overall rate is correct and exactly synchronized to
		 *    the bus clock, and
		 * 2) packets with a rounded-up number of blocks occur as early
		 *    as possible in the sequence (to prevent underruns of the
		 *    device's buffer).
		 */
		if (s->sfc == CIP_SFC_44100)
			/* 6 6 5 6 5 6 5 ... */
			data_blocks = 5 + ((phase & 1) ^
					   (phase == 0 || phase >= 40));
		else
			/* 12 11 11 11 11 ... or 23 22 22 22 22 ... */
			data_blocks = 11 * (s->sfc >> 1) + (phase == 0);
		if (++phase >= (80 >> (s->sfc >> 1)))
			phase = 0;
		s->data_block_state = phase;
	}

	return data_blocks;
}

static unsigned int calculate_syt(struct amdtp_out_stream *s,
				  unsigned int cycle)
{
	unsigned int syt_offset, phase, index, syt;

	if (s->last_syt_offset < TICKS_PER_CYCLE) {
		if (!cip_sfc_is_base_44100(s->sfc))
			syt_offset = s->last_syt_offset + s->syt_offset_state;
		else {
		/*
		 * The time, in ticks, of the n'th SYT_INTERVAL sample is:
		 *   n * SYT_INTERVAL * 24576000 / sample_rate
		 * Modulo TICKS_PER_CYCLE, the difference between successive
		 * elements is about 1386.23.  Rounding the results of this
		 * formula to the SYT precision results in a sequence of
		 * differences that begins with:
		 *   1386 1386 1387 1386 1386 1386 1387 1386 1386 1386 1387 ...
		 * This code generates _exactly_ the same sequence.
		 */
			phase = s->syt_offset_state;
			index = phase % 13;
			syt_offset = s->last_syt_offset;
			syt_offset += 1386 + ((index && !(index & 3)) ||
					      phase == 146);
			if (++phase >= 147)
				phase = 0;
			s->syt_offset_state = phase;
		}
	} else
		syt_offset = s->last_syt_offset - TICKS_PER_CYCLE;
	s->last_syt_offset = syt_offset;

	if (syt_offset < TICKS_PER_CYCLE) {
		syt_offset += TRANSFER_DELAY_TICKS - TICKS_PER_CYCLE;
		syt = (cycle + syt_offset / TICKS_PER_CYCLE) << 12;
		syt += syt_offset % TICKS_PER_CYCLE;

		return syt & 0xffff;
	} else {
		return 0xffff; /* no info */
	}
}

static void amdtp_write_s32(struct amdtp_out_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	const u32 *src;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*buffer = cpu_to_be32((*src >> 8) | 0x40000000);
			src++;
			buffer++;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void amdtp_write_s16(struct amdtp_out_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	const u16 *src;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*buffer = cpu_to_be32((*src << 8) | 0x40000000);
			src++;
			buffer++;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void amdtp_fill_pcm_silence(struct amdtp_out_stream *s,
				   __be32 *buffer, unsigned int frames)
{
	unsigned int i, c;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < s->pcm_channels; ++c)
			buffer[c] = cpu_to_be32(0x40000000);
		buffer += s->data_block_quadlets;
	}
}

static void amdtp_fill_midi(struct amdtp_out_stream *s,
			    __be32 *buffer, unsigned int frames)
{
	unsigned int i;

	for (i = 0; i < frames; ++i)
		buffer[s->pcm_channels + i * s->data_block_quadlets] =
						cpu_to_be32(0x80000000);
}

static void queue_out_packet(struct amdtp_out_stream *s, unsigned int cycle)
{
	__be32 *buffer;
	unsigned int index, data_blocks, syt, ptr;
	struct snd_pcm_substream *pcm;
	struct fw_iso_packet packet;
	int err;

	if (s->packet_index < 0)
		return;
	index = s->packet_index;

	data_blocks = calculate_data_blocks(s);
	syt = calculate_syt(s, cycle);

	buffer = s->buffer.packets[index].buffer;
	buffer[0] = cpu_to_be32(ACCESS_ONCE(s->source_node_id_field) |
				(s->data_block_quadlets << 16) |
				s->data_block_counter);
	buffer[1] = cpu_to_be32(CIP_EOH | CIP_FMT_AM | AMDTP_FDF_AM824 |
				(s->sfc << AMDTP_FDF_SFC_SHIFT) | syt);
	buffer += 2;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm)
		s->transfer_samples(s, pcm, buffer, data_blocks);
	else
		amdtp_fill_pcm_silence(s, buffer, data_blocks);
	if (s->midi_ports)
		amdtp_fill_midi(s, buffer, data_blocks);

	s->data_block_counter = (s->data_block_counter + data_blocks) & 0xff;

	packet.payload_length = 8 + data_blocks * 4 * s->data_block_quadlets;
	packet.interrupt = IS_ALIGNED(index + 1, INTERRUPT_INTERVAL);
	packet.skip = 0;
	packet.tag = TAG_CIP;
	packet.sy = 0;
	packet.header_length = 0;

	err = fw_iso_context_queue(s->context, &packet, &s->buffer.iso_buffer,
				   s->buffer.packets[index].offset);
	if (err < 0) {
		dev_err(&s->unit->device, "queueing error: %d\n", err);
		s->packet_index = -1;
		amdtp_out_stream_pcm_abort(s);
		return;
	}

	if (++index >= QUEUE_LENGTH)
		index = 0;
	s->packet_index = index;

	if (pcm) {
		ptr = s->pcm_buffer_pointer + data_blocks;
		if (ptr >= pcm->runtime->buffer_size)
			ptr -= pcm->runtime->buffer_size;
		ACCESS_ONCE(s->pcm_buffer_pointer) = ptr;

		s->pcm_period_pointer += data_blocks;
		if (s->pcm_period_pointer >= pcm->runtime->period_size) {
			s->pcm_period_pointer -= pcm->runtime->period_size;
			snd_pcm_period_elapsed(pcm);
		}
	}
}

static void out_packet_callback(struct fw_iso_context *context, u32 cycle,
				size_t header_length, void *header, void *data)
{
	struct amdtp_out_stream *s = data;
	unsigned int i, packets = header_length / 4;

	/*
	 * Compute the cycle of the last queued packet.
	 * (We need only the four lowest bits for the SYT, so we can ignore
	 * that bits 0-11 must wrap around at 3072.)
	 */
	cycle += QUEUE_LENGTH - packets;

	for (i = 0; i < packets; ++i)
		queue_out_packet(s, ++cycle);
	fw_iso_context_queue_flush(s->context);
}

static int queue_initial_skip_packets(struct amdtp_out_stream *s)
{
	struct fw_iso_packet skip_packet = {
		.skip = 1,
	};
	unsigned int i;
	int err;

	for (i = 0; i < QUEUE_LENGTH; ++i) {
		skip_packet.interrupt = IS_ALIGNED(s->packet_index + 1,
						   INTERRUPT_INTERVAL);
		err = fw_iso_context_queue(s->context, &skip_packet, NULL, 0);
		if (err < 0)
			return err;
		if (++s->packet_index >= QUEUE_LENGTH)
			s->packet_index = 0;
	}

	return 0;
}

/**
 * amdtp_out_stream_start - start sending packets
 * @s: the AMDTP output stream to start
 * @channel: the isochronous channel on the bus
 * @speed: firewire speed code
 *
 * The stream cannot be started until it has been configured with
 * amdtp_out_stream_set_hw_params(), amdtp_out_stream_set_pcm(), and
 * amdtp_out_stream_set_midi(); and it must be started before any
 * PCM or MIDI device can be started.
 */
int amdtp_out_stream_start(struct amdtp_out_stream *s, int channel, int speed)
{
	static const struct {
		unsigned int data_block;
		unsigned int syt_offset;
	} initial_state[] = {
		[CIP_SFC_32000]  = {  4, 3072 },
		[CIP_SFC_48000]  = {  6, 1024 },
		[CIP_SFC_96000]  = { 12, 1024 },
		[CIP_SFC_192000] = { 24, 1024 },
		[CIP_SFC_44100]  = {  0,   67 },
		[CIP_SFC_88200]  = {  0,   67 },
		[CIP_SFC_176400] = {  0,   67 },
	};
	int err;

	mutex_lock(&s->mutex);

	if (WARN_ON(!IS_ERR(s->context) ||
		    (!s->pcm_channels && !s->midi_ports))) {
		err = -EBADFD;
		goto err_unlock;
	}

	s->data_block_state = initial_state[s->sfc].data_block;
	s->syt_offset_state = initial_state[s->sfc].syt_offset;
	s->last_syt_offset = TICKS_PER_CYCLE;

	err = iso_packets_buffer_init(&s->buffer, s->unit, QUEUE_LENGTH,
				      amdtp_out_stream_get_max_payload(s),
				      DMA_TO_DEVICE);
	if (err < 0)
		goto err_unlock;

	s->context = fw_iso_context_create(fw_parent_device(s->unit)->card,
					   FW_ISO_CONTEXT_TRANSMIT,
					   channel, speed, 0,
					   out_packet_callback, s);
	if (IS_ERR(s->context)) {
		err = PTR_ERR(s->context);
		if (err == -EBUSY)
			dev_err(&s->unit->device,
				"no free output stream on this controller\n");
		goto err_buffer;
	}

	amdtp_out_stream_update(s);

	s->packet_index = 0;
	s->data_block_counter = 0;
	err = queue_initial_skip_packets(s);
	if (err < 0)
		goto err_context;

	err = fw_iso_context_start(s->context, -1, 0, 0);
	if (err < 0)
		goto err_context;

	mutex_unlock(&s->mutex);

	return 0;

err_context:
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
err_buffer:
	iso_packets_buffer_destroy(&s->buffer, s->unit);
err_unlock:
	mutex_unlock(&s->mutex);

	return err;
}
EXPORT_SYMBOL(amdtp_out_stream_start);

/**
 * amdtp_out_stream_update - update the stream after a bus reset
 * @s: the AMDTP output stream
 */
void amdtp_out_stream_update(struct amdtp_out_stream *s)
{
	ACCESS_ONCE(s->source_node_id_field) =
		(fw_parent_device(s->unit)->card->node_id & 0x3f) << 24;
}
EXPORT_SYMBOL(amdtp_out_stream_update);

/**
 * amdtp_out_stream_stop - stop sending packets
 * @s: the AMDTP output stream to stop
 *
 * All PCM and MIDI devices of the stream must be stopped before the stream
 * itself can be stopped.
 */
void amdtp_out_stream_stop(struct amdtp_out_stream *s)
{
	mutex_lock(&s->mutex);

	if (IS_ERR(s->context)) {
		mutex_unlock(&s->mutex);
		return;
	}

	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);

	mutex_unlock(&s->mutex);
}
EXPORT_SYMBOL(amdtp_out_stream_stop);

/**
 * amdtp_out_stream_pcm_abort - abort the running PCM device
 * @s: the AMDTP stream about to be stopped
 *
 * If the isochronous stream needs to be stopped asynchronously, call this
 * function first to stop the PCM device.
 */
void amdtp_out_stream_pcm_abort(struct amdtp_out_stream *s)
{
	struct snd_pcm_substream *pcm;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm) {
		snd_pcm_stream_lock_irq(pcm);
		if (snd_pcm_running(pcm))
			snd_pcm_stop(pcm, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock_irq(pcm);
	}
}
EXPORT_SYMBOL(amdtp_out_stream_pcm_abort);
