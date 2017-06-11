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
#include <sound/pcm_params.h>
#include "amdtp-stream.h"

#define TICKS_PER_CYCLE		3072
#define CYCLES_PER_SECOND	8000
#define TICKS_PER_SECOND	(TICKS_PER_CYCLE * CYCLES_PER_SECOND)

/* Always support Linux tracing subsystem. */
#define CREATE_TRACE_POINTS
#include "amdtp-stream-trace.h"

#define TRANSFER_DELAY_TICKS	0x2e00 /* 479.17 microseconds */

/* isochronous header parameters */
#define ISO_DATA_LENGTH_SHIFT	16
#define TAG_CIP			1

/* common isochronous packet header parameters */
#define CIP_EOH_SHIFT		31
#define CIP_EOH			(1u << CIP_EOH_SHIFT)
#define CIP_EOH_MASK		0x80000000
#define CIP_SID_SHIFT		24
#define CIP_SID_MASK		0x3f000000
#define CIP_DBS_MASK		0x00ff0000
#define CIP_DBS_SHIFT		16
#define CIP_DBC_MASK		0x000000ff
#define CIP_FMT_SHIFT		24
#define CIP_FMT_MASK		0x3f000000
#define CIP_FDF_MASK		0x00ff0000
#define CIP_FDF_SHIFT		16
#define CIP_SYT_MASK		0x0000ffff
#define CIP_SYT_NO_INFO		0xffff

/* Audio and Music transfer protocol specific parameters */
#define CIP_FMT_AM		0x10
#define AMDTP_FDF_NO_DATA	0xff

/* TODO: make these configurable */
#define INTERRUPT_INTERVAL	16
#define QUEUE_LENGTH		48

#define IN_PACKET_HEADER_SIZE	4
#define OUT_PACKET_HEADER_SIZE	0

static void pcm_period_tasklet(unsigned long data);

/**
 * amdtp_stream_init - initialize an AMDTP stream structure
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @dir: the direction of stream
 * @flags: the packet transmission method to use
 * @fmt: the value of fmt field in CIP header
 * @process_data_blocks: callback handler to process data blocks
 * @protocol_size: the size to allocate newly for protocol
 */
int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		      enum amdtp_stream_direction dir, enum cip_flags flags,
		      unsigned int fmt,
		      amdtp_stream_process_data_blocks_t process_data_blocks,
		      unsigned int protocol_size)
{
	if (process_data_blocks == NULL)
		return -EINVAL;

	s->protocol = kzalloc(protocol_size, GFP_KERNEL);
	if (!s->protocol)
		return -ENOMEM;

	s->unit = unit;
	s->direction = dir;
	s->flags = flags;
	s->context = ERR_PTR(-1);
	mutex_init(&s->mutex);
	tasklet_init(&s->period_tasklet, pcm_period_tasklet, (unsigned long)s);
	s->packet_index = 0;

	init_waitqueue_head(&s->callback_wait);
	s->callbacked = false;

	s->fmt = fmt;
	s->process_data_blocks = process_data_blocks;

	return 0;
}
EXPORT_SYMBOL(amdtp_stream_init);

/**
 * amdtp_stream_destroy - free stream resources
 * @s: the AMDTP stream to destroy
 */
void amdtp_stream_destroy(struct amdtp_stream *s)
{
	/* Not initialized. */
	if (s->protocol == NULL)
		return;

	WARN_ON(amdtp_stream_running(s));
	kfree(s->protocol);
	mutex_destroy(&s->mutex);
}
EXPORT_SYMBOL(amdtp_stream_destroy);

const unsigned int amdtp_syt_intervals[CIP_SFC_COUNT] = {
	[CIP_SFC_32000]  =  8,
	[CIP_SFC_44100]  =  8,
	[CIP_SFC_48000]  =  8,
	[CIP_SFC_88200]  = 16,
	[CIP_SFC_96000]  = 16,
	[CIP_SFC_176400] = 32,
	[CIP_SFC_192000] = 32,
};
EXPORT_SYMBOL(amdtp_syt_intervals);

const unsigned int amdtp_rate_table[CIP_SFC_COUNT] = {
	[CIP_SFC_32000]  =  32000,
	[CIP_SFC_44100]  =  44100,
	[CIP_SFC_48000]  =  48000,
	[CIP_SFC_88200]  =  88200,
	[CIP_SFC_96000]  =  96000,
	[CIP_SFC_176400] = 176400,
	[CIP_SFC_192000] = 192000,
};
EXPORT_SYMBOL(amdtp_rate_table);

/**
 * amdtp_stream_add_pcm_hw_constraints - add hw constraints for PCM substream
 * @s:		the AMDTP stream, which must be initialized.
 * @runtime:	the PCM substream runtime
 */
int amdtp_stream_add_pcm_hw_constraints(struct amdtp_stream *s,
					struct snd_pcm_runtime *runtime)
{
	int err;

	/*
	 * Currently firewire-lib processes 16 packets in one software
	 * interrupt callback. This equals to 2msec but actually the
	 * interval of the interrupts has a jitter.
	 * Additionally, even if adding a constraint to fit period size to
	 * 2msec, actual calculated frames per period doesn't equal to 2msec,
	 * depending on sampling rate.
	 * Anyway, the interval to call snd_pcm_period_elapsed() cannot 2msec.
	 * Here let us use 5msec for safe period interrupt.
	 */
	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   5000, UINT_MAX);
	if (err < 0)
		goto end;

	/* Non-Blocking stream has no more constraints */
	if (!(s->flags & CIP_BLOCKING))
		goto end;

	/*
	 * One AMDTP packet can include some frames. In blocking mode, the
	 * number equals to SYT_INTERVAL. So the number is 8, 16 or 32,
	 * depending on its sampling rate. For accurate period interrupt, it's
	 * preferrable to align period/buffer sizes to current SYT_INTERVAL.
	 *
	 * TODO: These constraints can be improved with proper rules.
	 * Currently apply LCM of SYT_INTERVALs.
	 */
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 32);
	if (err < 0)
		goto end;
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 32);
end:
	return err;
}
EXPORT_SYMBOL(amdtp_stream_add_pcm_hw_constraints);

/**
 * amdtp_stream_set_parameters - set stream parameters
 * @s: the AMDTP stream to configure
 * @rate: the sample rate
 * @data_block_quadlets: the size of a data block in quadlet unit
 *
 * The parameters must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
int amdtp_stream_set_parameters(struct amdtp_stream *s, unsigned int rate,
				unsigned int data_block_quadlets)
{
	unsigned int sfc;

	for (sfc = 0; sfc < ARRAY_SIZE(amdtp_rate_table); ++sfc) {
		if (amdtp_rate_table[sfc] == rate)
			break;
	}
	if (sfc == ARRAY_SIZE(amdtp_rate_table))
		return -EINVAL;

	s->sfc = sfc;
	s->data_block_quadlets = data_block_quadlets;
	s->syt_interval = amdtp_syt_intervals[sfc];

	/* default buffering in the device */
	s->transfer_delay = TRANSFER_DELAY_TICKS - TICKS_PER_CYCLE;
	if (s->flags & CIP_BLOCKING)
		/* additional buffering needed to adjust for no-data packets */
		s->transfer_delay += TICKS_PER_SECOND * s->syt_interval / rate;

	return 0;
}
EXPORT_SYMBOL(amdtp_stream_set_parameters);

/**
 * amdtp_stream_get_max_payload - get the stream's packet size
 * @s: the AMDTP stream
 *
 * This function must not be called before the stream has been configured
 * with amdtp_stream_set_parameters().
 */
unsigned int amdtp_stream_get_max_payload(struct amdtp_stream *s)
{
	unsigned int multiplier = 1;

	if (s->flags & CIP_JUMBO_PAYLOAD)
		multiplier = 5;

	return 8 + s->syt_interval * s->data_block_quadlets * 4 * multiplier;
}
EXPORT_SYMBOL(amdtp_stream_get_max_payload);

/**
 * amdtp_stream_pcm_prepare - prepare PCM device for running
 * @s: the AMDTP stream
 *
 * This function should be called from the PCM device's .prepare callback.
 */
void amdtp_stream_pcm_prepare(struct amdtp_stream *s)
{
	tasklet_kill(&s->period_tasklet);
	s->pcm_buffer_pointer = 0;
	s->pcm_period_pointer = 0;
}
EXPORT_SYMBOL(amdtp_stream_pcm_prepare);

static unsigned int calculate_data_blocks(struct amdtp_stream *s,
					  unsigned int syt)
{
	unsigned int phase, data_blocks;

	/* Blocking mode. */
	if (s->flags & CIP_BLOCKING) {
		/* This module generate empty packet for 'no data'. */
		if (syt == CIP_SYT_NO_INFO)
			data_blocks = 0;
		else
			data_blocks = s->syt_interval;
	/* Non-blocking mode. */
	} else {
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
	}

	return data_blocks;
}

static unsigned int calculate_syt(struct amdtp_stream *s,
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
		syt_offset += s->transfer_delay;
		syt = (cycle + syt_offset / TICKS_PER_CYCLE) << 12;
		syt += syt_offset % TICKS_PER_CYCLE;

		return syt & CIP_SYT_MASK;
	} else {
		return CIP_SYT_NO_INFO;
	}
}

static void update_pcm_pointers(struct amdtp_stream *s,
				struct snd_pcm_substream *pcm,
				unsigned int frames)
{
	unsigned int ptr;

	ptr = s->pcm_buffer_pointer + frames;
	if (ptr >= pcm->runtime->buffer_size)
		ptr -= pcm->runtime->buffer_size;
	ACCESS_ONCE(s->pcm_buffer_pointer) = ptr;

	s->pcm_period_pointer += frames;
	if (s->pcm_period_pointer >= pcm->runtime->period_size) {
		s->pcm_period_pointer -= pcm->runtime->period_size;
		tasklet_hi_schedule(&s->period_tasklet);
	}
}

static void pcm_period_tasklet(unsigned long data)
{
	struct amdtp_stream *s = (void *)data;
	struct snd_pcm_substream *pcm = ACCESS_ONCE(s->pcm);

	if (pcm)
		snd_pcm_period_elapsed(pcm);
}

static int queue_packet(struct amdtp_stream *s, unsigned int header_length,
			unsigned int payload_length)
{
	struct fw_iso_packet p = {0};
	int err = 0;

	if (IS_ERR(s->context))
		goto end;

	p.interrupt = IS_ALIGNED(s->packet_index + 1, INTERRUPT_INTERVAL);
	p.tag = TAG_CIP;
	p.header_length = header_length;
	if (payload_length > 0)
		p.payload_length = payload_length;
	else
		p.skip = true;
	err = fw_iso_context_queue(s->context, &p, &s->buffer.iso_buffer,
				   s->buffer.packets[s->packet_index].offset);
	if (err < 0) {
		dev_err(&s->unit->device, "queueing error: %d\n", err);
		goto end;
	}

	if (++s->packet_index >= QUEUE_LENGTH)
		s->packet_index = 0;
end:
	return err;
}

static inline int queue_out_packet(struct amdtp_stream *s,
				   unsigned int payload_length)
{
	return queue_packet(s, OUT_PACKET_HEADER_SIZE, payload_length);
}

static inline int queue_in_packet(struct amdtp_stream *s)
{
	return queue_packet(s, IN_PACKET_HEADER_SIZE,
			    amdtp_stream_get_max_payload(s));
}

static int handle_out_packet(struct amdtp_stream *s, unsigned int cycle,
			     unsigned int index)
{
	__be32 *buffer;
	unsigned int syt;
	unsigned int data_blocks;
	unsigned int payload_length;
	unsigned int pcm_frames;
	struct snd_pcm_substream *pcm;

	buffer = s->buffer.packets[s->packet_index].buffer;
	syt = calculate_syt(s, cycle);
	data_blocks = calculate_data_blocks(s, syt);
	pcm_frames = s->process_data_blocks(s, buffer + 2, data_blocks, &syt);

	buffer[0] = cpu_to_be32(ACCESS_ONCE(s->source_node_id_field) |
				(s->data_block_quadlets << CIP_DBS_SHIFT) |
				s->data_block_counter);
	buffer[1] = cpu_to_be32(CIP_EOH |
				((s->fmt << CIP_FMT_SHIFT) & CIP_FMT_MASK) |
				((s->fdf << CIP_FDF_SHIFT) & CIP_FDF_MASK) |
				(syt & CIP_SYT_MASK));

	s->data_block_counter = (s->data_block_counter + data_blocks) & 0xff;
	payload_length = 8 + data_blocks * 4 * s->data_block_quadlets;

	trace_out_packet(s, cycle, buffer, payload_length, index);

	if (queue_out_packet(s, payload_length) < 0)
		return -EIO;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm && pcm_frames > 0)
		update_pcm_pointers(s, pcm, pcm_frames);

	/* No need to return the number of handled data blocks. */
	return 0;
}

static int handle_in_packet(struct amdtp_stream *s,
			    unsigned int payload_quadlets, unsigned int cycle,
			    unsigned int index)
{
	__be32 *buffer;
	u32 cip_header[2];
	unsigned int fmt, fdf, syt;
	unsigned int data_block_quadlets, data_block_counter, dbc_interval;
	unsigned int data_blocks;
	struct snd_pcm_substream *pcm;
	unsigned int pcm_frames;
	bool lost;

	buffer = s->buffer.packets[s->packet_index].buffer;
	cip_header[0] = be32_to_cpu(buffer[0]);
	cip_header[1] = be32_to_cpu(buffer[1]);

	trace_in_packet(s, cycle, cip_header, payload_quadlets, index);

	/*
	 * This module supports 'Two-quadlet CIP header with SYT field'.
	 * For convenience, also check FMT field is AM824 or not.
	 */
	if (((cip_header[0] & CIP_EOH_MASK) == CIP_EOH) ||
	    ((cip_header[1] & CIP_EOH_MASK) != CIP_EOH)) {
		dev_info_ratelimited(&s->unit->device,
				"Invalid CIP header for AMDTP: %08X:%08X\n",
				cip_header[0], cip_header[1]);
		data_blocks = 0;
		pcm_frames = 0;
		goto end;
	}

	/* Check valid protocol or not. */
	fmt = (cip_header[1] & CIP_FMT_MASK) >> CIP_FMT_SHIFT;
	if (fmt != s->fmt) {
		dev_info_ratelimited(&s->unit->device,
				     "Detect unexpected protocol: %08x %08x\n",
				     cip_header[0], cip_header[1]);
		data_blocks = 0;
		pcm_frames = 0;
		goto end;
	}

	/* Calculate data blocks */
	fdf = (cip_header[1] & CIP_FDF_MASK) >> CIP_FDF_SHIFT;
	if (payload_quadlets < 3 ||
	    (fmt == CIP_FMT_AM && fdf == AMDTP_FDF_NO_DATA)) {
		data_blocks = 0;
	} else {
		data_block_quadlets =
			(cip_header[0] & CIP_DBS_MASK) >> CIP_DBS_SHIFT;
		/* avoid division by zero */
		if (data_block_quadlets == 0) {
			dev_err(&s->unit->device,
				"Detect invalid value in dbs field: %08X\n",
				cip_header[0]);
			return -EPROTO;
		}
		if (s->flags & CIP_WRONG_DBS)
			data_block_quadlets = s->data_block_quadlets;

		data_blocks = (payload_quadlets - 2) / data_block_quadlets;
	}

	/* Check data block counter continuity */
	data_block_counter = cip_header[0] & CIP_DBC_MASK;
	if (data_blocks == 0 && (s->flags & CIP_EMPTY_HAS_WRONG_DBC) &&
	    s->data_block_counter != UINT_MAX)
		data_block_counter = s->data_block_counter;

	if (((s->flags & CIP_SKIP_DBC_ZERO_CHECK) &&
	     data_block_counter == s->tx_first_dbc) ||
	    s->data_block_counter == UINT_MAX) {
		lost = false;
	} else if (!(s->flags & CIP_DBC_IS_END_EVENT)) {
		lost = data_block_counter != s->data_block_counter;
	} else {
		if (data_blocks > 0 && s->tx_dbc_interval > 0)
			dbc_interval = s->tx_dbc_interval;
		else
			dbc_interval = data_blocks;

		lost = data_block_counter !=
		       ((s->data_block_counter + dbc_interval) & 0xff);
	}

	if (lost) {
		dev_err(&s->unit->device,
			"Detect discontinuity of CIP: %02X %02X\n",
			s->data_block_counter, data_block_counter);
		return -EIO;
	}

	syt = be32_to_cpu(buffer[1]) & CIP_SYT_MASK;
	pcm_frames = s->process_data_blocks(s, buffer + 2, data_blocks, &syt);

	if (s->flags & CIP_DBC_IS_END_EVENT)
		s->data_block_counter = data_block_counter;
	else
		s->data_block_counter =
				(data_block_counter + data_blocks) & 0xff;
end:
	if (queue_in_packet(s) < 0)
		return -EIO;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm && pcm_frames > 0)
		update_pcm_pointers(s, pcm, pcm_frames);

	return 0;
}

/*
 * In CYCLE_TIMER register of IEEE 1394, 7 bits are used to represent second. On
 * the other hand, in DMA descriptors of 1394 OHCI, 3 bits are used to represent
 * it. Thus, via Linux firewire subsystem, we can get the 3 bits for second.
 */
static inline u32 compute_cycle_count(u32 tstamp)
{
	return (((tstamp >> 13) & 0x07) * 8000) + (tstamp & 0x1fff);
}

static inline u32 increment_cycle_count(u32 cycle, unsigned int addend)
{
	cycle += addend;
	if (cycle >= 8 * CYCLES_PER_SECOND)
		cycle -= 8 * CYCLES_PER_SECOND;
	return cycle;
}

static inline u32 decrement_cycle_count(u32 cycle, unsigned int subtrahend)
{
	if (cycle < subtrahend)
		cycle += 8 * CYCLES_PER_SECOND;
	return cycle - subtrahend;
}

static void out_stream_callback(struct fw_iso_context *context, u32 tstamp,
				size_t header_length, void *header,
				void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int i, packets = header_length / 4;
	u32 cycle;

	if (s->packet_index < 0)
		return;

	cycle = compute_cycle_count(tstamp);

	/* Align to actual cycle count for the last packet. */
	cycle = increment_cycle_count(cycle, QUEUE_LENGTH - packets);

	for (i = 0; i < packets; ++i) {
		cycle = increment_cycle_count(cycle, 1);
		if (handle_out_packet(s, cycle, i) < 0) {
			s->packet_index = -1;
			if (in_interrupt())
				amdtp_stream_pcm_abort(s);
			WRITE_ONCE(s->pcm_buffer_pointer, SNDRV_PCM_POS_XRUN);
			return;
		}
	}

	fw_iso_context_queue_flush(s->context);
}

static void in_stream_callback(struct fw_iso_context *context, u32 tstamp,
			       size_t header_length, void *header,
			       void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int i, packets;
	unsigned int payload_quadlets, max_payload_quadlets;
	__be32 *headers = header;
	u32 cycle;

	if (s->packet_index < 0)
		return;

	/* The number of packets in buffer */
	packets = header_length / IN_PACKET_HEADER_SIZE;

	cycle = compute_cycle_count(tstamp);

	/* Align to actual cycle count for the last packet. */
	cycle = decrement_cycle_count(cycle, packets);

	/* For buffer-over-run prevention. */
	max_payload_quadlets = amdtp_stream_get_max_payload(s) / 4;

	for (i = 0; i < packets; i++) {
		cycle = increment_cycle_count(cycle, 1);

		/* The number of quadlets in this packet */
		payload_quadlets =
			(be32_to_cpu(headers[i]) >> ISO_DATA_LENGTH_SHIFT) / 4;
		if (payload_quadlets > max_payload_quadlets) {
			dev_err(&s->unit->device,
				"Detect jumbo payload: %02x %02x\n",
				payload_quadlets, max_payload_quadlets);
			break;
		}

		if (handle_in_packet(s, payload_quadlets, cycle, i) < 0)
			break;
	}

	/* Queueing error or detecting invalid payload. */
	if (i < packets) {
		s->packet_index = -1;
		if (in_interrupt())
			amdtp_stream_pcm_abort(s);
		WRITE_ONCE(s->pcm_buffer_pointer, SNDRV_PCM_POS_XRUN);
		return;
	}

	fw_iso_context_queue_flush(s->context);
}

/* this is executed one time */
static void amdtp_stream_first_callback(struct fw_iso_context *context,
					u32 tstamp, size_t header_length,
					void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;

	/*
	 * For in-stream, first packet has come.
	 * For out-stream, prepared to transmit first packet
	 */
	s->callbacked = true;
	wake_up(&s->callback_wait);

	if (s->direction == AMDTP_IN_STREAM)
		context->callback.sc = in_stream_callback;
	else
		context->callback.sc = out_stream_callback;

	context->callback.sc(context, tstamp, header_length, header, s);
}

/**
 * amdtp_stream_start - start transferring packets
 * @s: the AMDTP stream to start
 * @channel: the isochronous channel on the bus
 * @speed: firewire speed code
 *
 * The stream cannot be started until it has been configured with
 * amdtp_stream_set_parameters() and it must be started before any PCM or MIDI
 * device can be started.
 */
int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed)
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
	unsigned int header_size;
	enum dma_data_direction dir;
	int type, tag, err;

	mutex_lock(&s->mutex);

	if (WARN_ON(amdtp_stream_running(s) ||
		    (s->data_block_quadlets < 1))) {
		err = -EBADFD;
		goto err_unlock;
	}

	if (s->direction == AMDTP_IN_STREAM)
		s->data_block_counter = UINT_MAX;
	else
		s->data_block_counter = 0;
	s->data_block_state = initial_state[s->sfc].data_block;
	s->syt_offset_state = initial_state[s->sfc].syt_offset;
	s->last_syt_offset = TICKS_PER_CYCLE;

	/* initialize packet buffer */
	if (s->direction == AMDTP_IN_STREAM) {
		dir = DMA_FROM_DEVICE;
		type = FW_ISO_CONTEXT_RECEIVE;
		header_size = IN_PACKET_HEADER_SIZE;
	} else {
		dir = DMA_TO_DEVICE;
		type = FW_ISO_CONTEXT_TRANSMIT;
		header_size = OUT_PACKET_HEADER_SIZE;
	}
	err = iso_packets_buffer_init(&s->buffer, s->unit, QUEUE_LENGTH,
				      amdtp_stream_get_max_payload(s), dir);
	if (err < 0)
		goto err_unlock;

	s->context = fw_iso_context_create(fw_parent_device(s->unit)->card,
					   type, channel, speed, header_size,
					   amdtp_stream_first_callback, s);
	if (IS_ERR(s->context)) {
		err = PTR_ERR(s->context);
		if (err == -EBUSY)
			dev_err(&s->unit->device,
				"no free stream on this controller\n");
		goto err_buffer;
	}

	amdtp_stream_update(s);

	s->packet_index = 0;
	do {
		if (s->direction == AMDTP_IN_STREAM)
			err = queue_in_packet(s);
		else
			err = queue_out_packet(s, 0);
		if (err < 0)
			goto err_context;
	} while (s->packet_index > 0);

	/* NOTE: TAG1 matches CIP. This just affects in stream. */
	tag = FW_ISO_CONTEXT_MATCH_TAG1;
	if (s->flags & CIP_EMPTY_WITH_TAG0)
		tag |= FW_ISO_CONTEXT_MATCH_TAG0;

	s->callbacked = false;
	err = fw_iso_context_start(s->context, -1, 0, tag);
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
EXPORT_SYMBOL(amdtp_stream_start);

/**
 * amdtp_stream_pcm_pointer - get the PCM buffer position
 * @s: the AMDTP stream that transports the PCM data
 *
 * Returns the current buffer position, in frames.
 */
unsigned long amdtp_stream_pcm_pointer(struct amdtp_stream *s)
{
	/*
	 * This function is called in software IRQ context of period_tasklet or
	 * process context.
	 *
	 * When the software IRQ context was scheduled by software IRQ context
	 * of IR/IT contexts, queued packets were already handled. Therefore,
	 * no need to flush the queue in buffer anymore.
	 *
	 * When the process context reach here, some packets will be already
	 * queued in the buffer. These packets should be handled immediately
	 * to keep better granularity of PCM pointer.
	 *
	 * Later, the process context will sometimes schedules software IRQ
	 * context of the period_tasklet. Then, no need to flush the queue by
	 * the same reason as described for IR/IT contexts.
	 */
	if (!in_interrupt() && amdtp_stream_running(s))
		fw_iso_context_flush_completions(s->context);

	return ACCESS_ONCE(s->pcm_buffer_pointer);
}
EXPORT_SYMBOL(amdtp_stream_pcm_pointer);

/**
 * amdtp_stream_update - update the stream after a bus reset
 * @s: the AMDTP stream
 */
void amdtp_stream_update(struct amdtp_stream *s)
{
	/* Precomputing. */
	ACCESS_ONCE(s->source_node_id_field) =
		(fw_parent_device(s->unit)->card->node_id << CIP_SID_SHIFT) &
								CIP_SID_MASK;
}
EXPORT_SYMBOL(amdtp_stream_update);

/**
 * amdtp_stream_stop - stop sending packets
 * @s: the AMDTP stream to stop
 *
 * All PCM and MIDI devices of the stream must be stopped before the stream
 * itself can be stopped.
 */
void amdtp_stream_stop(struct amdtp_stream *s)
{
	mutex_lock(&s->mutex);

	if (!amdtp_stream_running(s)) {
		mutex_unlock(&s->mutex);
		return;
	}

	tasklet_kill(&s->period_tasklet);
	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);

	s->callbacked = false;

	mutex_unlock(&s->mutex);
}
EXPORT_SYMBOL(amdtp_stream_stop);

/**
 * amdtp_stream_pcm_abort - abort the running PCM device
 * @s: the AMDTP stream about to be stopped
 *
 * If the isochronous stream needs to be stopped asynchronously, call this
 * function first to stop the PCM device.
 */
void amdtp_stream_pcm_abort(struct amdtp_stream *s)
{
	struct snd_pcm_substream *pcm;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm)
		snd_pcm_stop_xrun(pcm);
}
EXPORT_SYMBOL(amdtp_stream_pcm_abort);
