// SPDX-License-Identifier: GPL-2.0-only
/*
 * Audio and Music Data Transmission Protocol (IEC 61883-6) streams
 * with Common Isochronous Packet (IEC 61883-1) headers
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "amdtp-stream.h"

#define TICKS_PER_CYCLE		3072
#define CYCLES_PER_SECOND	8000
#define TICKS_PER_SECOND	(TICKS_PER_CYCLE * CYCLES_PER_SECOND)

#define OHCI_MAX_SECOND		8

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

// For iso header, tstamp and 2 CIP header.
#define IR_CTX_HEADER_SIZE_CIP		16
// For iso header and tstamp.
#define IR_CTX_HEADER_SIZE_NO_CIP	8
#define HEADER_TSTAMP_MASK	0x0000ffff

#define IT_PKT_HEADER_SIZE_CIP		8 // For 2 CIP header.
#define IT_PKT_HEADER_SIZE_NO_CIP	0 // Nothing.

static void pcm_period_work(struct work_struct *work);

/**
 * amdtp_stream_init - initialize an AMDTP stream structure
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @dir: the direction of stream
 * @flags: the packet transmission method to use
 * @fmt: the value of fmt field in CIP header
 * @process_ctx_payloads: callback handler to process payloads of isoc context
 * @protocol_size: the size to allocate newly for protocol
 */
int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		      enum amdtp_stream_direction dir, enum cip_flags flags,
		      unsigned int fmt,
		      amdtp_stream_process_ctx_payloads_t process_ctx_payloads,
		      unsigned int protocol_size)
{
	if (process_ctx_payloads == NULL)
		return -EINVAL;

	s->protocol = kzalloc(protocol_size, GFP_KERNEL);
	if (!s->protocol)
		return -ENOMEM;

	s->unit = unit;
	s->direction = dir;
	s->flags = flags;
	s->context = ERR_PTR(-1);
	mutex_init(&s->mutex);
	INIT_WORK(&s->period_work, pcm_period_work);
	s->packet_index = 0;

	init_waitqueue_head(&s->callback_wait);
	s->callbacked = false;

	s->fmt = fmt;
	s->process_ctx_payloads = process_ctx_payloads;

	if (dir == AMDTP_OUT_STREAM)
		s->ctx_data.rx.syt_override = -1;

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
	unsigned int ctx_header_size;
	unsigned int maximum_usec_per_period;
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

	// Linux driver for 1394 OHCI controller voluntarily flushes isoc
	// context when total size of accumulated context header reaches
	// PAGE_SIZE. This kicks work for the isoc context and brings
	// callback in the middle of scheduled interrupts.
	// Although AMDTP streams in the same domain use the same events per
	// IRQ, use the largest size of context header between IT/IR contexts.
	// Here, use the value of context header in IR context is for both
	// contexts.
	if (!(s->flags & CIP_NO_HEADER))
		ctx_header_size = IR_CTX_HEADER_SIZE_CIP;
	else
		ctx_header_size = IR_CTX_HEADER_SIZE_NO_CIP;
	maximum_usec_per_period = USEC_PER_SEC * PAGE_SIZE /
				  CYCLES_PER_SECOND / ctx_header_size;

	// In IEC 61883-6, one isoc packet can transfer events up to the value
	// of syt interval. This comes from the interval of isoc cycle. As 1394
	// OHCI controller can generate hardware IRQ per isoc packet, the
	// interval is 125 usec.
	// However, there are two ways of transmission in IEC 61883-6; blocking
	// and non-blocking modes. In blocking mode, the sequence of isoc packet
	// includes 'empty' or 'NODATA' packets which include no event. In
	// non-blocking mode, the number of events per packet is variable up to
	// the syt interval.
	// Due to the above protocol design, the minimum PCM frames per
	// interrupt should be double of the value of syt interval, thus it is
	// 250 usec.
	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   250, maximum_usec_per_period);
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
	cancel_work_sync(&s->period_work);
	s->pcm_buffer_pointer = 0;
	s->pcm_period_pointer = 0;
}
EXPORT_SYMBOL(amdtp_stream_pcm_prepare);

static unsigned int calculate_data_blocks(unsigned int *data_block_state,
				bool is_blocking, bool is_no_info,
				unsigned int syt_interval, enum cip_sfc sfc)
{
	unsigned int data_blocks;

	/* Blocking mode. */
	if (is_blocking) {
		/* This module generate empty packet for 'no data'. */
		if (is_no_info)
			data_blocks = 0;
		else
			data_blocks = syt_interval;
	/* Non-blocking mode. */
	} else {
		if (!cip_sfc_is_base_44100(sfc)) {
			// Sample_rate / 8000 is an integer, and precomputed.
			data_blocks = *data_block_state;
		} else {
			unsigned int phase = *data_block_state;

		/*
		 * This calculates the number of data blocks per packet so that
		 * 1) the overall rate is correct and exactly synchronized to
		 *    the bus clock, and
		 * 2) packets with a rounded-up number of blocks occur as early
		 *    as possible in the sequence (to prevent underruns of the
		 *    device's buffer).
		 */
			if (sfc == CIP_SFC_44100)
				/* 6 6 5 6 5 6 5 ... */
				data_blocks = 5 + ((phase & 1) ^
						   (phase == 0 || phase >= 40));
			else
				/* 12 11 11 11 11 ... or 23 22 22 22 22 ... */
				data_blocks = 11 * (sfc >> 1) + (phase == 0);
			if (++phase >= (80 >> (sfc >> 1)))
				phase = 0;
			*data_block_state = phase;
		}
	}

	return data_blocks;
}

static unsigned int calculate_syt_offset(unsigned int *last_syt_offset,
			unsigned int *syt_offset_state, enum cip_sfc sfc)
{
	unsigned int syt_offset;

	if (*last_syt_offset < TICKS_PER_CYCLE) {
		if (!cip_sfc_is_base_44100(sfc))
			syt_offset = *last_syt_offset + *syt_offset_state;
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
			unsigned int phase = *syt_offset_state;
			unsigned int index = phase % 13;

			syt_offset = *last_syt_offset;
			syt_offset += 1386 + ((index && !(index & 3)) ||
					      phase == 146);
			if (++phase >= 147)
				phase = 0;
			*syt_offset_state = phase;
		}
	} else
		syt_offset = *last_syt_offset - TICKS_PER_CYCLE;
	*last_syt_offset = syt_offset;

	if (syt_offset >= TICKS_PER_CYCLE)
		syt_offset = CIP_SYT_NO_INFO;

	return syt_offset;
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
		queue_work(system_highpri_wq, &s->period_work);
	}
}

static void pcm_period_work(struct work_struct *work)
{
	struct amdtp_stream *s = container_of(work, struct amdtp_stream,
					      period_work);
	struct snd_pcm_substream *pcm = READ_ONCE(s->pcm);

	if (pcm)
		snd_pcm_period_elapsed(pcm);
}

static int queue_packet(struct amdtp_stream *s, struct fw_iso_packet *params,
			bool sched_irq)
{
	int err;

	params->interrupt = sched_irq;
	params->tag = s->tag;
	params->sy = 0;

	err = fw_iso_context_queue(s->context, params, &s->buffer.iso_buffer,
				   s->buffer.packets[s->packet_index].offset);
	if (err < 0) {
		dev_err(&s->unit->device, "queueing error: %d\n", err);
		goto end;
	}

	if (++s->packet_index >= s->queue_size)
		s->packet_index = 0;
end:
	return err;
}

static inline int queue_out_packet(struct amdtp_stream *s,
				   struct fw_iso_packet *params, bool sched_irq)
{
	params->skip =
		!!(params->header_length == 0 && params->payload_length == 0);
	return queue_packet(s, params, sched_irq);
}

static inline int queue_in_packet(struct amdtp_stream *s,
				  struct fw_iso_packet *params)
{
	// Queue one packet for IR context.
	params->header_length = s->ctx_data.tx.ctx_header_size;
	params->payload_length = s->ctx_data.tx.max_ctx_payload_length;
	params->skip = false;
	return queue_packet(s, params, false);
}

static void generate_cip_header(struct amdtp_stream *s, __be32 cip_header[2],
			unsigned int data_block_counter, unsigned int syt)
{
	cip_header[0] = cpu_to_be32(READ_ONCE(s->source_node_id_field) |
				(s->data_block_quadlets << CIP_DBS_SHIFT) |
				((s->sph << CIP_SPH_SHIFT) & CIP_SPH_MASK) |
				data_block_counter);
	cip_header[1] = cpu_to_be32(CIP_EOH |
			((s->fmt << CIP_FMT_SHIFT) & CIP_FMT_MASK) |
			((s->ctx_data.rx.fdf << CIP_FDF_SHIFT) & CIP_FDF_MASK) |
			(syt & CIP_SYT_MASK));
}

static void build_it_pkt_header(struct amdtp_stream *s, unsigned int cycle,
				struct fw_iso_packet *params,
				unsigned int data_blocks,
				unsigned int data_block_counter,
				unsigned int syt, unsigned int index)
{
	unsigned int payload_length;
	__be32 *cip_header;

	payload_length = data_blocks * sizeof(__be32) * s->data_block_quadlets;
	params->payload_length = payload_length;

	if (!(s->flags & CIP_NO_HEADER)) {
		cip_header = (__be32 *)params->header;
		generate_cip_header(s, cip_header, data_block_counter, syt);
		params->header_length = 2 * sizeof(__be32);
		payload_length += params->header_length;
	} else {
		cip_header = NULL;
	}

	trace_amdtp_packet(s, cycle, cip_header, payload_length, data_blocks,
			   data_block_counter, s->packet_index, index);
}

static int check_cip_header(struct amdtp_stream *s, const __be32 *buf,
			    unsigned int payload_length,
			    unsigned int *data_blocks,
			    unsigned int *data_block_counter, unsigned int *syt)
{
	u32 cip_header[2];
	unsigned int sph;
	unsigned int fmt;
	unsigned int fdf;
	unsigned int dbc;
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
	dbc = cip_header[0] & CIP_DBC_MASK;
	if (*data_blocks == 0 && (s->flags & CIP_EMPTY_HAS_WRONG_DBC) &&
	    *data_block_counter != UINT_MAX)
		dbc = *data_block_counter;

	if ((dbc == 0x00 && (s->flags & CIP_SKIP_DBC_ZERO_CHECK)) ||
	    *data_block_counter == UINT_MAX) {
		lost = false;
	} else if (!(s->flags & CIP_DBC_IS_END_EVENT)) {
		lost = dbc != *data_block_counter;
	} else {
		unsigned int dbc_interval;

		if (*data_blocks > 0 && s->ctx_data.tx.dbc_interval > 0)
			dbc_interval = s->ctx_data.tx.dbc_interval;
		else
			dbc_interval = *data_blocks;

		lost = dbc != ((*data_block_counter + dbc_interval) & 0xff);
	}

	if (lost) {
		dev_err(&s->unit->device,
			"Detect discontinuity of CIP: %02X %02X\n",
			*data_block_counter, dbc);
		return -EIO;
	}

	*data_block_counter = dbc;

	*syt = cip_header[1] & CIP_SYT_MASK;

	return 0;
}

static int parse_ir_ctx_header(struct amdtp_stream *s, unsigned int cycle,
			       const __be32 *ctx_header,
			       unsigned int *payload_length,
			       unsigned int *data_blocks,
			       unsigned int *data_block_counter,
			       unsigned int *syt, unsigned int packet_index, unsigned int index)
{
	const __be32 *cip_header;
	unsigned int cip_header_size;
	int err;

	*payload_length = be32_to_cpu(ctx_header[0]) >> ISO_DATA_LENGTH_SHIFT;

	if (!(s->flags & CIP_NO_HEADER))
		cip_header_size = 8;
	else
		cip_header_size = 0;

	if (*payload_length > cip_header_size + s->ctx_data.tx.max_ctx_payload_length) {
		dev_err(&s->unit->device,
			"Detect jumbo payload: %04x %04x\n",
			*payload_length, cip_header_size + s->ctx_data.tx.max_ctx_payload_length);
		return -EIO;
	}

	if (cip_header_size > 0) {
		cip_header = ctx_header + 2;
		err = check_cip_header(s, cip_header, *payload_length,
				       data_blocks, data_block_counter, syt);
		if (err < 0)
			return err;
	} else {
		cip_header = NULL;
		err = 0;
		*data_blocks = *payload_length / sizeof(__be32) /
			       s->data_block_quadlets;
		*syt = 0;

		if (*data_block_counter == UINT_MAX)
			*data_block_counter = 0;
	}

	trace_amdtp_packet(s, cycle, cip_header, *payload_length, *data_blocks,
			   *data_block_counter, packet_index, index);

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
	if (cycle >= OHCI_MAX_SECOND * CYCLES_PER_SECOND)
		cycle -= OHCI_MAX_SECOND * CYCLES_PER_SECOND;
	return cycle;
}

// Align to actual cycle count for the packet which is going to be scheduled.
// This module queued the same number of isochronous cycle as the size of queue
// to kip isochronous cycle, therefore it's OK to just increment the cycle by
// the size of queue for scheduled cycle.
static inline u32 compute_it_cycle(const __be32 ctx_header_tstamp,
				   unsigned int queue_size)
{
	u32 cycle = compute_cycle_count(ctx_header_tstamp);
	return increment_cycle_count(cycle, queue_size);
}

static int generate_device_pkt_descs(struct amdtp_stream *s,
				     struct pkt_desc *descs,
				     const __be32 *ctx_header,
				     unsigned int packets)
{
	unsigned int dbc = s->data_block_counter;
	unsigned int packet_index = s->packet_index;
	unsigned int queue_size = s->queue_size;
	int i;
	int err;

	for (i = 0; i < packets; ++i) {
		struct pkt_desc *desc = descs + i;
		unsigned int cycle;
		unsigned int payload_length;
		unsigned int data_blocks;
		unsigned int syt;

		cycle = compute_cycle_count(ctx_header[1]);

		err = parse_ir_ctx_header(s, cycle, ctx_header, &payload_length,
					  &data_blocks, &dbc, &syt, packet_index, i);
		if (err < 0)
			return err;

		desc->cycle = cycle;
		desc->syt = syt;
		desc->data_blocks = data_blocks;
		desc->data_block_counter = dbc;
		desc->ctx_payload = s->buffer.packets[packet_index].buffer;

		if (!(s->flags & CIP_DBC_IS_END_EVENT))
			dbc = (dbc + desc->data_blocks) & 0xff;

		ctx_header +=
			s->ctx_data.tx.ctx_header_size / sizeof(*ctx_header);

		packet_index = (packet_index + 1) % queue_size;
	}

	s->data_block_counter = dbc;

	return 0;
}

static unsigned int compute_syt(unsigned int syt_offset, unsigned int cycle,
				unsigned int transfer_delay)
{
	unsigned int syt;

	syt_offset += transfer_delay;
	syt = ((cycle + syt_offset / TICKS_PER_CYCLE) << 12) |
	      (syt_offset % TICKS_PER_CYCLE);
	return syt & CIP_SYT_MASK;
}

static void generate_pkt_descs(struct amdtp_stream *s, struct pkt_desc *descs,
			       const __be32 *ctx_header, unsigned int packets,
			       const struct seq_desc *seq_descs,
			       unsigned int seq_size)
{
	unsigned int dbc = s->data_block_counter;
	unsigned int seq_index = s->ctx_data.rx.seq_index;
	int i;

	for (i = 0; i < packets; ++i) {
		struct pkt_desc *desc = descs + i;
		unsigned int index = (s->packet_index + i) % s->queue_size;
		const struct seq_desc *seq = seq_descs + seq_index;
		unsigned int syt;

		desc->cycle = compute_it_cycle(*ctx_header, s->queue_size);

		syt = seq->syt_offset;
		if (syt != CIP_SYT_NO_INFO) {
			syt = compute_syt(syt, desc->cycle,
					  s->ctx_data.rx.transfer_delay);
		}
		desc->syt = syt;
		desc->data_blocks = seq->data_blocks;

		if (s->flags & CIP_DBC_IS_END_EVENT)
			dbc = (dbc + desc->data_blocks) & 0xff;

		desc->data_block_counter = dbc;

		if (!(s->flags & CIP_DBC_IS_END_EVENT))
			dbc = (dbc + desc->data_blocks) & 0xff;

		desc->ctx_payload = s->buffer.packets[index].buffer;

		seq_index = (seq_index + 1) % seq_size;

		++ctx_header;
	}

	s->data_block_counter = dbc;
	s->ctx_data.rx.seq_index = seq_index;
}

static inline void cancel_stream(struct amdtp_stream *s)
{
	s->packet_index = -1;
	if (current_work() == &s->period_work)
		amdtp_stream_pcm_abort(s);
	WRITE_ONCE(s->pcm_buffer_pointer, SNDRV_PCM_POS_XRUN);
}

static void process_ctx_payloads(struct amdtp_stream *s,
				 const struct pkt_desc *descs,
				 unsigned int packets)
{
	struct snd_pcm_substream *pcm;
	unsigned int pcm_frames;

	pcm = READ_ONCE(s->pcm);
	pcm_frames = s->process_ctx_payloads(s, descs, packets, pcm);
	if (pcm)
		update_pcm_pointers(s, pcm, pcm_frames);
}

static void out_stream_callback(struct fw_iso_context *context, u32 tstamp,
				size_t header_length, void *header,
				void *private_data)
{
	struct amdtp_stream *s = private_data;
	const struct amdtp_domain *d = s->domain;
	const __be32 *ctx_header = header;
	unsigned int events_per_period = s->ctx_data.rx.events_per_period;
	unsigned int event_count = s->ctx_data.rx.event_count;
	unsigned int packets;
	int i;

	if (s->packet_index < 0)
		return;

	// Calculate the number of packets in buffer and check XRUN.
	packets = header_length / sizeof(*ctx_header);

	generate_pkt_descs(s, s->pkt_descs, ctx_header, packets, d->seq_descs,
			   d->seq_size);

	process_ctx_payloads(s, s->pkt_descs, packets);

	for (i = 0; i < packets; ++i) {
		const struct pkt_desc *desc = s->pkt_descs + i;
		unsigned int syt;
		struct {
			struct fw_iso_packet params;
			__be32 header[IT_PKT_HEADER_SIZE_CIP / sizeof(__be32)];
		} template = { {0}, {0} };
		bool sched_irq = false;

		if (s->ctx_data.rx.syt_override < 0)
			syt = desc->syt;
		else
			syt = s->ctx_data.rx.syt_override;

		build_it_pkt_header(s, desc->cycle, &template.params,
				    desc->data_blocks, desc->data_block_counter,
				    syt, i);

		if (s == s->domain->irq_target) {
			event_count += desc->data_blocks;
			if (event_count >= events_per_period) {
				event_count -= events_per_period;
				sched_irq = true;
			}
		}

		if (queue_out_packet(s, &template.params, sched_irq) < 0) {
			cancel_stream(s);
			return;
		}
	}

	s->ctx_data.rx.event_count = event_count;
}

static void in_stream_callback(struct fw_iso_context *context, u32 tstamp,
			       size_t header_length, void *header,
			       void *private_data)
{
	struct amdtp_stream *s = private_data;
	__be32 *ctx_header = header;
	unsigned int packets;
	int i;
	int err;

	if (s->packet_index < 0)
		return;

	// Calculate the number of packets in buffer and check XRUN.
	packets = header_length / s->ctx_data.tx.ctx_header_size;

	err = generate_device_pkt_descs(s, s->pkt_descs, ctx_header, packets);
	if (err < 0) {
		if (err != -EAGAIN) {
			cancel_stream(s);
			return;
		}
	} else {
		process_ctx_payloads(s, s->pkt_descs, packets);
	}

	for (i = 0; i < packets; ++i) {
		struct fw_iso_packet params = {0};

		if (queue_in_packet(s, &params) < 0) {
			cancel_stream(s);
			return;
		}
	}
}

static void pool_ideal_seq_descs(struct amdtp_domain *d, unsigned int packets)
{
	struct amdtp_stream *irq_target = d->irq_target;
	unsigned int seq_tail = d->seq_tail;
	unsigned int seq_size = d->seq_size;
	unsigned int min_avail;
	struct amdtp_stream *s;

	min_avail = d->seq_size;
	list_for_each_entry(s, &d->streams, list) {
		unsigned int seq_index;
		unsigned int avail;

		if (s->direction == AMDTP_IN_STREAM)
			continue;

		seq_index = s->ctx_data.rx.seq_index;
		avail = d->seq_tail;
		if (seq_index > avail)
			avail += d->seq_size;
		avail -= seq_index;

		if (avail < min_avail)
			min_avail = avail;
	}

	while (min_avail < packets) {
		struct seq_desc *desc = d->seq_descs + seq_tail;

		desc->syt_offset = calculate_syt_offset(&d->last_syt_offset,
					&d->syt_offset_state, irq_target->sfc);
		desc->data_blocks = calculate_data_blocks(&d->data_block_state,
				!!(irq_target->flags & CIP_BLOCKING),
				desc->syt_offset == CIP_SYT_NO_INFO,
				irq_target->syt_interval, irq_target->sfc);

		++seq_tail;
		seq_tail %= seq_size;

		++min_avail;
	}

	d->seq_tail = seq_tail;
}

static void irq_target_callback(struct fw_iso_context *context, u32 tstamp,
				size_t header_length, void *header,
				void *private_data)
{
	struct amdtp_stream *irq_target = private_data;
	struct amdtp_domain *d = irq_target->domain;
	unsigned int packets = header_length / sizeof(__be32);
	struct amdtp_stream *s;

	// Record enough entries with extra 3 cycles at least.
	pool_ideal_seq_descs(d, packets + 3);

	out_stream_callback(context, tstamp, header_length, header, irq_target);
	if (amdtp_streaming_error(irq_target))
		goto error;

	list_for_each_entry(s, &d->streams, list) {
		if (s != irq_target && amdtp_stream_running(s)) {
			fw_iso_context_flush_completions(s->context);
			if (amdtp_streaming_error(s))
				goto error;
		}
	}

	return;
error:
	if (amdtp_stream_running(irq_target))
		cancel_stream(irq_target);

	list_for_each_entry(s, &d->streams, list) {
		if (amdtp_stream_running(s))
			cancel_stream(s);
	}
}

// this is executed one time.
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
		cycle = compute_it_cycle(*ctx_header, s->queue_size);

		if (s == s->domain->irq_target)
			context->callback.sc = irq_target_callback;
		else
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
 * @start_cycle: the isochronous cycle to start the context. Start immediately
 *		 if negative value is given.
 * @queue_size: The number of packets in the queue.
 * @idle_irq_interval: the interval to queue packet during initial state.
 *
 * The stream cannot be started until it has been configured with
 * amdtp_stream_set_parameters() and it must be started before any PCM or MIDI
 * device can be started.
 */
static int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed,
			      int start_cycle, unsigned int queue_size,
			      unsigned int idle_irq_interval)
{
	bool is_irq_target = (s == s->domain->irq_target);
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
		// NOTE: IT context should be used for constant IRQ.
		if (is_irq_target) {
			err = -EINVAL;
			goto err_unlock;
		}

		s->data_block_counter = UINT_MAX;
	} else {
		s->data_block_counter = 0;
	}

	// initialize packet buffer.
	max_ctx_payload_size = amdtp_stream_get_max_payload(s);
	if (s->direction == AMDTP_IN_STREAM) {
		dir = DMA_FROM_DEVICE;
		type = FW_ISO_CONTEXT_RECEIVE;
		if (!(s->flags & CIP_NO_HEADER)) {
			max_ctx_payload_size -= 8;
			ctx_header_size = IR_CTX_HEADER_SIZE_CIP;
		} else {
			ctx_header_size = IR_CTX_HEADER_SIZE_NO_CIP;
		}
	} else {
		dir = DMA_TO_DEVICE;
		type = FW_ISO_CONTEXT_TRANSMIT;
		ctx_header_size = 0;	// No effect for IT context.

		if (!(s->flags & CIP_NO_HEADER))
			max_ctx_payload_size -= IT_PKT_HEADER_SIZE_CIP;
	}

	err = iso_packets_buffer_init(&s->buffer, s->unit, queue_size,
				      max_ctx_payload_size, dir);
	if (err < 0)
		goto err_unlock;
	s->queue_size = queue_size;

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

	s->pkt_descs = kcalloc(s->queue_size, sizeof(*s->pkt_descs),
			       GFP_KERNEL);
	if (!s->pkt_descs) {
		err = -ENOMEM;
		goto err_context;
	}

	s->packet_index = 0;
	do {
		struct fw_iso_packet params;

		if (s->direction == AMDTP_IN_STREAM) {
			err = queue_in_packet(s, &params);
		} else {
			bool sched_irq = false;

			params.header_length = 0;
			params.payload_length = 0;

			if (is_irq_target) {
				sched_irq = !((s->packet_index + 1) %
					      idle_irq_interval);
			}

			err = queue_out_packet(s, &params, sched_irq);
		}
		if (err < 0)
			goto err_pkt_descs;
	} while (s->packet_index > 0);

	/* NOTE: TAG1 matches CIP. This just affects in stream. */
	tag = FW_ISO_CONTEXT_MATCH_TAG1;
	if ((s->flags & CIP_EMPTY_WITH_TAG0) || (s->flags & CIP_NO_HEADER))
		tag |= FW_ISO_CONTEXT_MATCH_TAG0;

	s->callbacked = false;
	err = fw_iso_context_start(s->context, start_cycle, 0, tag);
	if (err < 0)
		goto err_pkt_descs;

	mutex_unlock(&s->mutex);

	return 0;
err_pkt_descs:
	kfree(s->pkt_descs);
err_context:
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
err_buffer:
	iso_packets_buffer_destroy(&s->buffer, s->unit);
err_unlock:
	mutex_unlock(&s->mutex);

	return err;
}

/**
 * amdtp_domain_stream_pcm_pointer - get the PCM buffer position
 * @d: the AMDTP domain.
 * @s: the AMDTP stream that transports the PCM data
 *
 * Returns the current buffer position, in frames.
 */
unsigned long amdtp_domain_stream_pcm_pointer(struct amdtp_domain *d,
					      struct amdtp_stream *s)
{
	struct amdtp_stream *irq_target = d->irq_target;

	if (irq_target && amdtp_stream_running(irq_target)) {
		// This function is called in software IRQ context of
		// period_work or process context.
		//
		// When the software IRQ context was scheduled by software IRQ
		// context of IT contexts, queued packets were already handled.
		// Therefore, no need to flush the queue in buffer furthermore.
		//
		// When the process context reach here, some packets will be
		// already queued in the buffer. These packets should be handled
		// immediately to keep better granularity of PCM pointer.
		//
		// Later, the process context will sometimes schedules software
		// IRQ context of the period_work. Then, no need to flush the
		// queue by the same reason as described in the above
		if (current_work() != &s->period_work) {
			// Queued packet should be processed without any kernel
			// preemption to keep latency against bus cycle.
			preempt_disable();
			fw_iso_context_flush_completions(irq_target->context);
			preempt_enable();
		}
	}

	return READ_ONCE(s->pcm_buffer_pointer);
}
EXPORT_SYMBOL_GPL(amdtp_domain_stream_pcm_pointer);

/**
 * amdtp_domain_stream_pcm_ack - acknowledge queued PCM frames
 * @d: the AMDTP domain.
 * @s: the AMDTP stream that transfers the PCM frames
 *
 * Returns zero always.
 */
int amdtp_domain_stream_pcm_ack(struct amdtp_domain *d, struct amdtp_stream *s)
{
	struct amdtp_stream *irq_target = d->irq_target;

	// Process isochronous packets for recent isochronous cycle to handle
	// queued PCM frames.
	if (irq_target && amdtp_stream_running(irq_target)) {
		// Queued packet should be processed without any kernel
		// preemption to keep latency against bus cycle.
		preempt_disable();
		fw_iso_context_flush_completions(irq_target->context);
		preempt_enable();
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amdtp_domain_stream_pcm_ack);

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
static void amdtp_stream_stop(struct amdtp_stream *s)
{
	mutex_lock(&s->mutex);

	if (!amdtp_stream_running(s)) {
		mutex_unlock(&s->mutex);
		return;
	}

	cancel_work_sync(&s->period_work);
	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);
	kfree(s->pkt_descs);

	s->callbacked = false;

	mutex_unlock(&s->mutex);
}

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

/**
 * amdtp_domain_init - initialize an AMDTP domain structure
 * @d: the AMDTP domain to initialize.
 */
int amdtp_domain_init(struct amdtp_domain *d)
{
	INIT_LIST_HEAD(&d->streams);

	d->events_per_period = 0;

	d->seq_descs = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(amdtp_domain_init);

/**
 * amdtp_domain_destroy - destroy an AMDTP domain structure
 * @d: the AMDTP domain to destroy.
 */
void amdtp_domain_destroy(struct amdtp_domain *d)
{
	// At present nothing to do.
	return;
}
EXPORT_SYMBOL_GPL(amdtp_domain_destroy);

/**
 * amdtp_domain_add_stream - register isoc context into the domain.
 * @d: the AMDTP domain.
 * @s: the AMDTP stream.
 * @channel: the isochronous channel on the bus.
 * @speed: firewire speed code.
 */
int amdtp_domain_add_stream(struct amdtp_domain *d, struct amdtp_stream *s,
			    int channel, int speed)
{
	struct amdtp_stream *tmp;

	list_for_each_entry(tmp, &d->streams, list) {
		if (s == tmp)
			return -EBUSY;
	}

	list_add(&s->list, &d->streams);

	s->channel = channel;
	s->speed = speed;
	s->domain = d;

	return 0;
}
EXPORT_SYMBOL_GPL(amdtp_domain_add_stream);

static int get_current_cycle_time(struct fw_card *fw_card, int *cur_cycle)
{
	int generation;
	int rcode;
	__be32 reg;
	u32 data;

	// This is a request to local 1394 OHCI controller and expected to
	// complete without any event waiting.
	generation = fw_card->generation;
	smp_rmb();	// node_id vs. generation.
	rcode = fw_run_transaction(fw_card, TCODE_READ_QUADLET_REQUEST,
				   fw_card->node_id, generation, SCODE_100,
				   CSR_REGISTER_BASE + CSR_CYCLE_TIME,
				   &reg, sizeof(reg));
	if (rcode != RCODE_COMPLETE)
		return -EIO;

	data = be32_to_cpu(reg);
	*cur_cycle = data >> 12;

	return 0;
}

/**
 * amdtp_domain_start - start sending packets for isoc context in the domain.
 * @d: the AMDTP domain.
 * @ir_delay_cycle: the cycle delay to start all IR contexts.
 */
int amdtp_domain_start(struct amdtp_domain *d, unsigned int ir_delay_cycle)
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
	unsigned int events_per_buffer = d->events_per_buffer;
	unsigned int events_per_period = d->events_per_period;
	unsigned int idle_irq_interval;
	unsigned int queue_size;
	struct amdtp_stream *s;
	int cycle;
	int err;

	// Select an IT context as IRQ target.
	list_for_each_entry(s, &d->streams, list) {
		if (s->direction == AMDTP_OUT_STREAM)
			break;
	}
	if (!s)
		return -ENXIO;
	d->irq_target = s;

	// This is a case that AMDTP streams in domain run just for MIDI
	// substream. Use the number of events equivalent to 10 msec as
	// interval of hardware IRQ.
	if (events_per_period == 0)
		events_per_period = amdtp_rate_table[d->irq_target->sfc] / 100;
	if (events_per_buffer == 0)
		events_per_buffer = events_per_period * 3;

	queue_size = DIV_ROUND_UP(CYCLES_PER_SECOND * events_per_buffer,
				  amdtp_rate_table[d->irq_target->sfc]);

	d->seq_descs = kcalloc(queue_size, sizeof(*d->seq_descs), GFP_KERNEL);
	if (!d->seq_descs)
		return -ENOMEM;
	d->seq_size = queue_size;
	d->seq_tail = 0;

	entry = &initial_state[s->sfc];
	d->data_block_state = entry->data_block;
	d->syt_offset_state = entry->syt_offset;
	d->last_syt_offset = TICKS_PER_CYCLE;

	if (ir_delay_cycle > 0) {
		struct fw_card *fw_card = fw_parent_device(s->unit)->card;

		err = get_current_cycle_time(fw_card, &cycle);
		if (err < 0)
			goto error;

		// No need to care overflow in cycle field because of enough
		// width.
		cycle += ir_delay_cycle;

		// Round up to sec field.
		if ((cycle & 0x00001fff) >= CYCLES_PER_SECOND) {
			unsigned int sec;

			// The sec field can overflow.
			sec = (cycle & 0xffffe000) >> 13;
			cycle = (++sec << 13) |
				((cycle & 0x00001fff) / CYCLES_PER_SECOND);
		}

		// In OHCI 1394 specification, lower 2 bits are available for
		// sec field.
		cycle &= 0x00007fff;
	} else {
		cycle = -1;
	}

	list_for_each_entry(s, &d->streams, list) {
		int cycle_match;

		if (s->direction == AMDTP_IN_STREAM) {
			cycle_match = cycle;
		} else {
			// IT context starts immediately.
			cycle_match = -1;
			s->ctx_data.rx.seq_index = 0;
		}

		if (s != d->irq_target) {
			err = amdtp_stream_start(s, s->channel, s->speed,
						 cycle_match, queue_size, 0);
			if (err < 0)
				goto error;
		}
	}

	s = d->irq_target;
	s->ctx_data.rx.events_per_period = events_per_period;
	s->ctx_data.rx.event_count = 0;
	s->ctx_data.rx.seq_index = 0;

	idle_irq_interval = DIV_ROUND_UP(CYCLES_PER_SECOND * events_per_period,
					 amdtp_rate_table[d->irq_target->sfc]);
	err = amdtp_stream_start(s, s->channel, s->speed, -1, queue_size,
				 idle_irq_interval);
	if (err < 0)
		goto error;

	return 0;
error:
	list_for_each_entry(s, &d->streams, list)
		amdtp_stream_stop(s);
	kfree(d->seq_descs);
	d->seq_descs = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(amdtp_domain_start);

/**
 * amdtp_domain_stop - stop sending packets for isoc context in the same domain.
 * @d: the AMDTP domain to which the isoc contexts belong.
 */
void amdtp_domain_stop(struct amdtp_domain *d)
{
	struct amdtp_stream *s, *next;

	if (d->irq_target)
		amdtp_stream_stop(d->irq_target);

	list_for_each_entry_safe(s, next, &d->streams, list) {
		list_del(&s->list);

		if (s != d->irq_target)
			amdtp_stream_stop(s);
	}

	d->events_per_period = 0;
	d->irq_target = NULL;

	kfree(d->seq_descs);
	d->seq_descs = NULL;
}
EXPORT_SYMBOL_GPL(amdtp_domain_stop);
