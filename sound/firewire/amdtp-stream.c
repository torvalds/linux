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
#define TAG_NO_CIP_HEADER	0
#define TAG_CIP			1

/* common isochronous packet header parameters */
#define CIP_EOH_SHIFT		31
#define CIP_EOH			(1u << CIP_EOH_SHIFT)
#define CIP_EOH_MASK		0x80000000
#define CIP_SID_SHIFT		24
#define CIP_SID_MASK		0x3f000000
#define CIP_DBS_MASK		0x00ff0000
#define CIP_DBS_SHIFT		16
#define CIP_SPH_MASK		0x00000400
#define CIP_SPH_SHIFT		10
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

// For iso header, tstamp and 2 CIP header.
#define IR_CTX_HEADER_SIZE_CIP		16
// For iso header and tstamp.
#define IR_CTX_HEADER_SIZE_NO_CIP	8
#define HEADER_TSTAMP_MASK	0x0000ffff

#define IT_PKT_HEADER_SIZE_CIP		8 // For 2 CIP header.
#define IT_PKT_HEADER_SIZE_NO_CIP	0 // Nothing.

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

static int apply_constraint_to_size(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *s = hw_param_interval(params, rule->var);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {0};
	unsigned int step = 0;
	int i;

	for (i = 0; i < CIP_SFC_COUNT; ++i) {
		if (snd_interval_test(r, amdtp_rate_table[i]))
			step = max(step, amdtp_syt_intervals[i]);
	}

	t.min = roundup(s->min, step);
	t.max = rounddown(s->max, step);
	t.integer = 1;

	return snd_interval_refine(s, &t);
}

/**
 * amdtp_stream_add_pcm_hw_constraints - add hw constraints for PCM substream
 * @s:		the AMDTP stream, which must be initialized.
 * @runtime:	the PCM substream runtime
 */
int amdtp_stream_add_pcm_hw_constraints(struct amdtp_stream *s,
					struct snd_pcm_runtime *runtime)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	int err;

	hw->info = SNDRV_PCM_INFO_BATCH |
		   SNDRV_PCM_INFO_BLOCK_TRANSFER |
		   SNDRV_PCM_INFO_INTERLEAVED |
		   SNDRV_PCM_INFO_JOINT_DUPLEX |
		   SNDRV_PCM_INFO_MMAP |
		   SNDRV_PCM_INFO_MMAP_VALID;

	/* SNDRV_PCM_INFO_BATCH */
	hw->periods_min = 2;
	hw->periods_max = UINT_MAX;

	/* bytes for a frame */
	hw->period_bytes_min = 4 * hw->channels_max;

	/* Just to prevent from allocating much pages. */
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * hw->periods_min;

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
	 */
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				  apply_constraint_to_size, NULL,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto end;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				  apply_constraint_to_size, NULL,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto end;
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

	// default buffering in the device.
	if (s->direction == AMDTP_OUT_STREAM) {
		s->ctx_data.rx.transfer_delay =
					TRANSFER_DELAY_TICKS - TICKS_PER_CYCLE;

		if (s->flags & CIP_BLOCKING) {
			// additional buffering needed to adjust for no-data
			// packets.
			s->ctx_data.rx.transfer_delay +=
				TICKS_PER_SECOND * s->syt_interval / rate;
		}
	}

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
	unsigned int cip_header_size = 0;

	if (s->flags & CIP_JUMBO_PAYLOAD)
		multiplier = 5;
	if (!(s->flags & CIP_NO_HEADER))
		cip_header_size = sizeof(__be32) * 2;

	return cip_header_size +
		s->syt_interval * s->data_block_quadlets * sizeof(__be32) * multiplier;
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
			// Sample_rate / 8000 is an integer, and precomputed.
			data_blocks = s->ctx_data.rx.data_block_state;
		} else {
			phase = s->ctx_data.rx.data_block_state;

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
			s->ctx_data.rx.data_block_state = phase;
		}
	}

	return data_blocks;
}

static unsigned int calculate_syt(struct amdtp_stream *s,
				  unsigned int cycle)
{
	unsigned int syt_offset, phase, index, syt;

	if (s->ctx_data.rx.last_syt_offset < TICKS_PER_CYCLE) {
		if (!cip_sfc_is_base_44100(s->sfc))
			syt_offset = s->ctx_data.rx.last_syt_offset +
				     s->ctx_data.rx.syt_offset_state;
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
			phase = s->ctx_data.rx.syt_offset_state;
			index = phase % 13;
			syt_offset = s->ctx_data.rx.last_syt_offset;
			syt_offset += 1386 + ((index && !(index & 3)) ||
					      phase == 146);
			if (++phase >= 147)
				phase = 0;
			s->ctx_data.rx.syt_offset_state = phase;
		}
	} else
		syt_offset = s->ctx_data.rx.last_syt_offset - TICKS_PER_CYCLE;
	s->ctx_data.rx.last_syt_offset = syt_offset;

	if (syt_offset < TICKS_PER_CYCLE) {
		syt_offset += s->ctx_data.rx.transfer_delay;
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
	WRITE_ONCE(s->pcm_buffer_pointer, ptr);

	s->pcm_period_pointer += frames;
	if (s->pcm_period_pointer >= pcm->runtime->period_size) {
		s->pcm_period_pointer -= pcm->runtime->period_size;
		tasklet_hi_schedule(&s->period_tasklet);
	}
}

static void pcm_period_tasklet(unsigned long data)
{
	struct amdtp_stream *s = (void *)data;
	struct snd_pcm_substream *pcm = READ_ONCE(s->pcm);

	if (pcm)
		snd_pcm_period_elapsed(pcm);
}

static int queue_packet(struct amdtp_stream *s, struct fw_iso_packet *params)
{
	int err;

	params->interrupt = IS_ALIGNED(s->packet_index + 1, INTERRUPT_INTERVAL);
	params->tag = s->tag;
	params->sy = 0;

	err = fw_iso_context_queue(s->context, params, &s->buffer.iso_buffer,
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
				   struct fw_iso_packet *params)
{
	params->skip =
		!!(params->header_length == 0 && params->payload_length == 0);
	return queue_packet(s, params);
}

static inline int queue_in_packet(struct amdtp_stream *s,
				  struct fw_iso_packet *params)
{
	// Queue one packet for IR context.
	params->header_length = s->ctx_data.tx.ctx_header_size;
	params->payload_length = s->ctx_data.tx.max_ctx_payload_length;
	params->skip = false;
	return queue_packet(s, params);
}

static void generate_cip_header(struct amdtp_stream *s, __be32 cip_header[2],
				unsigned int syt)
{
	cip_header[0] = cpu_to_be32(READ_ONCE(s->source_node_id_field) |
				(s->data_block_quadlets << CIP_DBS_SHIFT) |
				((s->sph << CIP_SPH_SHIFT) & CIP_SPH_MASK) |
				s->data_block_counter);
	cip_header[1] = cpu_to_be32(CIP_EOH |
			((s->fmt << CIP_FMT_SHIFT) & CIP_FMT_MASK) |
			((s->ctx_data.rx.fdf << CIP_FDF_SHIFT) & CIP_FDF_MASK) |
			(syt & CIP_SYT_MASK));
}

static void build_it_pkt_header(struct amdtp_stream *s, unsigned int cycle,
				struct fw_iso_packet *params,
				unsigned int data_blocks, unsigned int syt,
				unsigned int index)
{
	__be32 *cip_header;

	if (s->flags & CIP_DBC_IS_END_EVENT) {
		s->data_block_counter =
				(s->data_block_counter + data_blocks) & 0xff;
	}

	if (!(s->flags & CIP_NO_HEADER)) {
		cip_header = (__be32 *)params->header;
		generate_cip_header(s, cip_header, syt);
		params->header_length = 2 * sizeof(__be32);
	} else {
		cip_header = NULL;
	}

	if (!(s->flags & CIP_DBC_IS_END_EVENT)) {
		s->data_block_counter =
				(s->data_block_counter + data_blocks) & 0xff;
	}

	params->payload_length =
			data_blocks * sizeof(__be32) * s->data_block_quadlets;

	trace_amdtp_packet(s, cycle, cip_header, params->payload_length,
			   data_blocks, index);
}

static int check_cip_header(struct amdtp_stream *s, const __be32 *buf,
			    unsigned int payload_length,
			    unsigned int *data_blocks, unsigned int *dbc,
			    unsigned int *syt)
{
	u32 cip_header[2];
	unsigned int sph;
	unsigned int fmt;
	unsigned int fdf;
	bool lost;

	cip_header[0] = be32_to_cpu(buf[0]);
	cip_header[1] = be32_to_cpu(buf[1]);

	/*
	 * This module supports 'Two-quadlet CIP header with SYT field'.
	 * For convenience, also check FMT field is AM824 or not.
	 */
	if ((((cip_header[0] & CIP_EOH_MASK) == CIP_EOH) ||
	     ((cip_header[1] & CIP_EOH_MASK) != CIP_EOH)) &&
	    (!(s->flags & CIP_HEADER_WITHOUT_EOH))) {
		dev_info_ratelimited(&s->unit->device,
				"Invalid CIP header for AMDTP: %08X:%08X\n",
				cip_header[0], cip_header[1]);
		return -EAGAIN;
	}

	/* Check valid protocol or not. */
	sph = (cip_header[0] & CIP_SPH_MASK) >> CIP_SPH_SHIFT;
	fmt = (cip_header[1] & CIP_FMT_MASK) >> CIP_FMT_SHIFT;
	if (sph != s->sph || fmt != s->fmt) {
		dev_info_ratelimited(&s->unit->device,
				     "Detect unexpected protocol: %08x %08x\n",
				     cip_header[0], cip_header[1]);
		return -EAGAIN;
	}

	/* Calculate data blocks */
	fdf = (cip_header[1] & CIP_FDF_MASK) >> CIP_FDF_SHIFT;
	if (payload_length < sizeof(__be32) * 2 ||
	    (fmt == CIP_FMT_AM && fdf == AMDTP_FDF_NO_DATA)) {
		*data_blocks = 0;
	} else {
		unsigned int data_block_quadlets =
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

		*data_blocks = (payload_length / sizeof(__be32) - 2) /
							data_block_quadlets;
	}

	/* Check data block counter continuity */
	*dbc = cip_header[0] & CIP_DBC_MASK;
	if (*data_blocks == 0 && (s->flags & CIP_EMPTY_HAS_WRONG_DBC) &&
	    s->data_block_counter != UINT_MAX)
		*dbc = s->data_block_counter;

	if (((s->flags & CIP_SKIP_DBC_ZERO_CHECK) &&
	     *dbc == s->ctx_data.tx.first_dbc) ||
	    s->data_block_counter == UINT_MAX) {
		lost = false;
	} else if (!(s->flags & CIP_DBC_IS_END_EVENT)) {
		lost = *dbc != s->data_block_counter;
	} else {
		unsigned int dbc_interval;

		if (*data_blocks > 0 && s->ctx_data.tx.dbc_interval > 0)
			dbc_interval = s->ctx_data.tx.dbc_interval;
		else
			dbc_interval = *data_blocks;

		lost = *dbc != ((s->data_block_counter + dbc_interval) & 0xff);
	}

	if (lost) {
		dev_err(&s->unit->device,
			"Detect discontinuity of CIP: %02X %02X\n",
			s->data_block_counter, *dbc);
		return -EIO;
	}

	*syt = cip_header[1] & CIP_SYT_MASK;

	return 0;
}

static int parse_ir_ctx_header(struct amdtp_stream *s, unsigned int cycle,
			       const __be32 *ctx_header,
			       unsigned int *payload_length,
			       unsigned int *data_blocks, unsigned int *dbc,
			       unsigned int *syt, unsigned int index)
{
	const __be32 *cip_header;
	int err;

	*payload_length = be32_to_cpu(ctx_header[0]) >> ISO_DATA_LENGTH_SHIFT;
	if (*payload_length > s->ctx_data.tx.ctx_header_size +
					s->ctx_data.tx.max_ctx_payload_length) {
		dev_err(&s->unit->device,
			"Detect jumbo payload: %04x %04x\n",
			*payload_length, s->ctx_data.tx.max_ctx_payload_length);
		return -EIO;
	}

	if (!(s->flags & CIP_NO_HEADER)) {
		cip_header = ctx_header + 2;
		err = check_cip_header(s, cip_header, *payload_length,
				       data_blocks, dbc, syt);
		if (err < 0) {
			if (err != -EAGAIN)
				return err;

			*data_blocks = 0;
		}
	} else {
		cip_header = NULL;
		err = 0;
		*data_blocks = *payload_length / sizeof(__be32) /
			       s->data_block_quadlets;
		*dbc = s->data_block_counter;
		*syt = 0;
	}

	if (err >= 0 && s->flags & CIP_DBC_IS_END_EVENT)
		s->data_block_counter = *dbc;

	trace_amdtp_packet(s, cycle, cip_header, *payload_length, *data_blocks,
			   index);

	return err;
}

// In CYCLE_TIMER register of IEEE 1394, 7 bits are used to represent second. On
// the other hand, in DMA descriptors of 1394 OHCI, 3 bits are used to represent
// it. Thus, via Linux firewire subsystem, we can get the 3 bits for second.
static inline u32 compute_cycle_count(__be32 ctx_header_tstamp)
{
	u32 tstamp = be32_to_cpu(ctx_header_tstamp) & HEADER_TSTAMP_MASK;
	return (((tstamp >> 13) & 0x07) * 8000) + (tstamp & 0x1fff);
}

static inline u32 increment_cycle_count(u32 cycle, unsigned int addend)
{
	cycle += addend;
	if (cycle >= 8 * CYCLES_PER_SECOND)
		cycle -= 8 * CYCLES_PER_SECOND;
	return cycle;
}

// Align to actual cycle count for the packet which is going to be scheduled.
// This module queued the same number of isochronous cycle as QUEUE_LENGTH to
// skip isochronous cycle, therefore it's OK to just increment the cycle by
// QUEUE_LENGTH for scheduled cycle.
static inline u32 compute_it_cycle(const __be32 ctx_header_tstamp)
{
	u32 cycle = compute_cycle_count(ctx_header_tstamp);
	return increment_cycle_count(cycle, QUEUE_LENGTH);
}

static inline void cancel_stream(struct amdtp_stream *s)
{
	s->packet_index = -1;
	if (in_interrupt())
		amdtp_stream_pcm_abort(s);
	WRITE_ONCE(s->pcm_buffer_pointer, SNDRV_PCM_POS_XRUN);
}

static void out_stream_callback(struct fw_iso_context *context, u32 tstamp,
				size_t header_length, void *header,
				void *private_data)
{
	struct amdtp_stream *s = private_data;
	const __be32 *ctx_header = header;
	unsigned int i, packets = header_length / sizeof(*ctx_header);

	if (s->packet_index < 0)
		return;

	for (i = 0; i < packets; ++i) {
		u32 cycle;
		unsigned int syt;
		unsigned int data_block;
		__be32 *buffer;
		unsigned int pcm_frames;
		struct {
			struct fw_iso_packet params;
			__be32 header[IT_PKT_HEADER_SIZE_CIP / sizeof(__be32)];
		} template = { {0}, {0} };
		struct snd_pcm_substream *pcm;

		cycle = compute_it_cycle(*ctx_header);
		syt = calculate_syt(s, cycle);
		data_block = calculate_data_blocks(s, syt);
		buffer = s->buffer.packets[s->packet_index].buffer;
		pcm_frames = s->process_data_blocks(s, buffer, data_block, &syt);

		build_it_pkt_header(s, cycle, &template.params, data_block, syt,
				    i);

		if (queue_out_packet(s, &template.params) < 0) {
			cancel_stream(s);
			return;
		}

		pcm = READ_ONCE(s->pcm);
		if (pcm && pcm_frames > 0)
			update_pcm_pointers(s, pcm, pcm_frames);

		++ctx_header;
	}

	fw_iso_context_queue_flush(s->context);
}

static void in_stream_callback(struct fw_iso_context *context, u32 tstamp,
			       size_t header_length, void *header,
			       void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int i, packets;
	__be32 *ctx_header = header;

	if (s->packet_index < 0)
		return;

	// The number of packets in buffer.
	packets = header_length / s->ctx_data.tx.ctx_header_size;

	for (i = 0; i < packets; i++) {
		u32 cycle;
		unsigned int payload_length;
		unsigned int data_blocks;
		unsigned int dbc;
		unsigned int syt;
		__be32 *buffer;
		unsigned int pcm_frames = 0;
		struct fw_iso_packet params = {0};
		struct snd_pcm_substream *pcm;
		int err;

		cycle = compute_cycle_count(ctx_header[1]);
		err = parse_ir_ctx_header(s, cycle, ctx_header, &payload_length,
					  &data_blocks, &dbc, &syt, i);
		if (err < 0 && err != -EAGAIN)
			break;

		if (err >= 0) {
			buffer = s->buffer.packets[s->packet_index].buffer;
			pcm_frames = s->process_data_blocks(s, buffer,
							    data_blocks, &syt);

			if (!(s->flags & CIP_DBC_IS_END_EVENT)) {
				s->data_block_counter =
						(dbc + data_blocks) & 0xff;
			}
		}

		if (queue_in_packet(s, &params) < 0)
			break;

		pcm = READ_ONCE(s->pcm);
		if (pcm && pcm_frames > 0)
			update_pcm_pointers(s, pcm, pcm_frames);

		ctx_header += s->ctx_data.tx.ctx_header_size / sizeof(*ctx_header);
	}

	/* Queueing error or detecting invalid payload. */
	if (i < packets) {
		cancel_stream(s);
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
	const __be32 *ctx_header = header;
	u32 cycle;

	/*
	 * For in-stream, first packet has come.
	 * For out-stream, prepared to transmit first packet
	 */
	s->callbacked = true;
	wake_up(&s->callback_wait);

	if (s->direction == AMDTP_IN_STREAM) {
		cycle = compute_cycle_count(ctx_header[1]);

		context->callback.sc = in_stream_callback;
	} else {
		cycle = compute_it_cycle(*ctx_header);

		context->callback.sc = out_stream_callback;
	}

	s->start_cycle = cycle;

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
	} *entry, initial_state[] = {
		[CIP_SFC_32000]  = {  4, 3072 },
		[CIP_SFC_48000]  = {  6, 1024 },
		[CIP_SFC_96000]  = { 12, 1024 },
		[CIP_SFC_192000] = { 24, 1024 },
		[CIP_SFC_44100]  = {  0,   67 },
		[CIP_SFC_88200]  = {  0,   67 },
		[CIP_SFC_176400] = {  0,   67 },
	};
	unsigned int ctx_header_size;
	unsigned int max_ctx_payload_size;
	enum dma_data_direction dir;
	int type, tag, err;

	mutex_lock(&s->mutex);

	if (WARN_ON(amdtp_stream_running(s) ||
		    (s->data_block_quadlets < 1))) {
		err = -EBADFD;
		goto err_unlock;
	}

	if (s->direction == AMDTP_IN_STREAM) {
		s->data_block_counter = UINT_MAX;
	} else {
		entry = &initial_state[s->sfc];

		s->data_block_counter = 0;
		s->ctx_data.rx.data_block_state = entry->data_block;
		s->ctx_data.rx.syt_offset_state = entry->syt_offset;
		s->ctx_data.rx.last_syt_offset = TICKS_PER_CYCLE;
	}

	/* initialize packet buffer */
	if (s->direction == AMDTP_IN_STREAM) {
		dir = DMA_FROM_DEVICE;
		type = FW_ISO_CONTEXT_RECEIVE;
		if (!(s->flags & CIP_NO_HEADER))
			ctx_header_size = IR_CTX_HEADER_SIZE_CIP;
		else
			ctx_header_size = IR_CTX_HEADER_SIZE_NO_CIP;

		max_ctx_payload_size = amdtp_stream_get_max_payload(s) -
				       ctx_header_size;
	} else {
		dir = DMA_TO_DEVICE;
		type = FW_ISO_CONTEXT_TRANSMIT;
		ctx_header_size = 0;	// No effect for IT context.

		max_ctx_payload_size = amdtp_stream_get_max_payload(s);
		if (!(s->flags & CIP_NO_HEADER))
			max_ctx_payload_size -= IT_PKT_HEADER_SIZE_CIP;
	}

	err = iso_packets_buffer_init(&s->buffer, s->unit, QUEUE_LENGTH,
				      max_ctx_payload_size, dir);
	if (err < 0)
		goto err_unlock;

	s->context = fw_iso_context_create(fw_parent_device(s->unit)->card,
					  type, channel, speed, ctx_header_size,
					  amdtp_stream_first_callback, s);
	if (IS_ERR(s->context)) {
		err = PTR_ERR(s->context);
		if (err == -EBUSY)
			dev_err(&s->unit->device,
				"no free stream on this controller\n");
		goto err_buffer;
	}

	amdtp_stream_update(s);

	if (s->direction == AMDTP_IN_STREAM) {
		s->ctx_data.tx.max_ctx_payload_length = max_ctx_payload_size;
		s->ctx_data.tx.ctx_header_size = ctx_header_size;
	}

	if (s->flags & CIP_NO_HEADER)
		s->tag = TAG_NO_CIP_HEADER;
	else
		s->tag = TAG_CIP;

	s->packet_index = 0;
	do {
		struct fw_iso_packet params;
		if (s->direction == AMDTP_IN_STREAM) {
			err = queue_in_packet(s, &params);
		} else {
			params.header_length = 0;
			params.payload_length = 0;
			err = queue_out_packet(s, &params);
		}
		if (err < 0)
			goto err_context;
	} while (s->packet_index > 0);

	/* NOTE: TAG1 matches CIP. This just affects in stream. */
	tag = FW_ISO_CONTEXT_MATCH_TAG1;
	if ((s->flags & CIP_EMPTY_WITH_TAG0) || (s->flags & CIP_NO_HEADER))
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

	return READ_ONCE(s->pcm_buffer_pointer);
}
EXPORT_SYMBOL(amdtp_stream_pcm_pointer);

/**
 * amdtp_stream_pcm_ack - acknowledge queued PCM frames
 * @s: the AMDTP stream that transfers the PCM frames
 *
 * Returns zero always.
 */
int amdtp_stream_pcm_ack(struct amdtp_stream *s)
{
	/*
	 * Process isochronous packets for recent isochronous cycle to handle
	 * queued PCM frames.
	 */
	if (amdtp_stream_running(s))
		fw_iso_context_flush_completions(s->context);

	return 0;
}
EXPORT_SYMBOL(amdtp_stream_pcm_ack);

/**
 * amdtp_stream_update - update the stream after a bus reset
 * @s: the AMDTP stream
 */
void amdtp_stream_update(struct amdtp_stream *s)
{
	/* Precomputing. */
	WRITE_ONCE(s->source_node_id_field,
                   (fw_parent_device(s->unit)->card->node_id << CIP_SID_SHIFT) & CIP_SID_MASK);
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

	pcm = READ_ONCE(s->pcm);
	if (pcm)
		snd_pcm_stop_xrun(pcm);
}
EXPORT_SYMBOL(amdtp_stream_pcm_abort);
