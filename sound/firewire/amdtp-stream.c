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

#define OHCI_SECOND_MODULUS		8

/* Always support Linux tracing subsystem. */
#define CREATE_TRACE_POINTS
#include "amdtp-stream-trace.h"

#define TRANSFER_DELAY_TICKS	0x2e00 /* 479.17 microseconds */

/* isochronous header parameters */
#define ISO_DATA_LENGTH_SHIFT	16
#define TAG_NO_CIP_HEADER	0
#define TAG_CIP			1

// Common Isochronous Packet (CIP) header parameters. Use two quadlets CIP header when supported.
#define CIP_HEADER_QUADLETS	2
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
#define CIP_FDF_NO_DATA		0xff
#define CIP_SYT_MASK		0x0000ffff
#define CIP_SYT_NO_INFO		0xffff
#define CIP_SYT_CYCLE_MODULUS	16
#define CIP_NO_DATA		((CIP_FDF_NO_DATA << CIP_FDF_SHIFT) | CIP_SYT_NO_INFO)

#define CIP_HEADER_SIZE		(sizeof(__be32) * CIP_HEADER_QUADLETS)

/* Audio and Music transfer protocol specific parameters */
#define CIP_FMT_AM		0x10
#define AMDTP_FDF_NO_DATA	0xff

// For iso header and tstamp.
#define IR_CTX_HEADER_DEFAULT_QUADLETS	2
// Add nothing.
#define IR_CTX_HEADER_SIZE_NO_CIP	(sizeof(__be32) * IR_CTX_HEADER_DEFAULT_QUADLETS)
// Add two quadlets CIP header.
#define IR_CTX_HEADER_SIZE_CIP		(IR_CTX_HEADER_SIZE_NO_CIP + CIP_HEADER_SIZE)
#define HEADER_TSTAMP_MASK	0x0000ffff

#define IT_PKT_HEADER_SIZE_CIP		CIP_HEADER_SIZE
#define IT_PKT_HEADER_SIZE_NO_CIP	0 // Nothing.

// The initial firmware of OXFW970 can postpone transmission of packet during finishing
// asynchronous transaction. This module accepts 5 cycles to skip as maximum to avoid buffer
// overrun. Actual device can skip more, then this module stops the packet streaming.
#define IR_JUMBO_PAYLOAD_MAX_SKIP_CYCLES	5

/**
 * amdtp_stream_init - initialize an AMDTP stream structure
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @dir: the direction of stream
 * @flags: the details of the streaming protocol consist of cip_flags enumeration-constants.
 * @fmt: the value of fmt field in CIP header
 * @process_ctx_payloads: callback handler to process payloads of isoc context
 * @protocol_size: the size to allocate newly for protocol
 */
int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		      enum amdtp_stream_direction dir, unsigned int flags,
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
	s->packet_index = 0;

	init_waitqueue_head(&s->ready_wait);

	s->fmt = fmt;
	s->process_ctx_payloads = process_ctx_payloads;

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

	hw->info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		   SNDRV_PCM_INFO_INTERLEAVED |
		   SNDRV_PCM_INFO_JOINT_DUPLEX |
		   SNDRV_PCM_INFO_MMAP |
		   SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_NO_PERIOD_WAKEUP;

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
	s->transfer_delay = TRANSFER_DELAY_TICKS - TICKS_PER_CYCLE;

	// additional buffering needed to adjust for no-data packets.
	if (s->flags & CIP_BLOCKING)
		s->transfer_delay += TICKS_PER_SECOND * s->syt_interval / rate;

	return 0;
}
EXPORT_SYMBOL(amdtp_stream_set_parameters);

// The CIP header is processed in context header apart from context payload.
static int amdtp_stream_get_max_ctx_payload_size(struct amdtp_stream *s)
{
	unsigned int multiplier;

	if (s->flags & CIP_JUMBO_PAYLOAD)
		multiplier = IR_JUMBO_PAYLOAD_MAX_SKIP_CYCLES;
	else
		multiplier = 1;

	return s->syt_interval * s->data_block_quadlets * sizeof(__be32) * multiplier;
}

/**
 * amdtp_stream_get_max_payload - get the stream's packet size
 * @s: the AMDTP stream
 *
 * This function must not be called before the stream has been configured
 * with amdtp_stream_set_parameters().
 */
unsigned int amdtp_stream_get_max_payload(struct amdtp_stream *s)
{
	unsigned int cip_header_size;

	if (!(s->flags & CIP_NO_HEADER))
		cip_header_size = CIP_HEADER_SIZE;
	else
		cip_header_size = 0;

	return cip_header_size + amdtp_stream_get_max_ctx_payload_size(s);
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
	s->pcm_buffer_pointer = 0;
	s->pcm_period_pointer = 0;
}
EXPORT_SYMBOL(amdtp_stream_pcm_prepare);

static void pool_blocking_data_blocks(struct amdtp_stream *s, struct seq_desc *descs,
				      const unsigned int seq_size, unsigned int seq_tail,
				      unsigned int count)
{
	const unsigned int syt_interval = s->syt_interval;
	int i;

	for (i = 0; i < count; ++i) {
		struct seq_desc *desc = descs + seq_tail;

		if (desc->syt_offset != CIP_SYT_NO_INFO)
			desc->data_blocks = syt_interval;
		else
			desc->data_blocks = 0;

		seq_tail = (seq_tail + 1) % seq_size;
	}
}

static void pool_ideal_nonblocking_data_blocks(struct amdtp_stream *s, struct seq_desc *descs,
					       const unsigned int seq_size, unsigned int seq_tail,
					       unsigned int count)
{
	const enum cip_sfc sfc = s->sfc;
	unsigned int state = s->ctx_data.rx.data_block_state;
	int i;

	for (i = 0; i < count; ++i) {
		struct seq_desc *desc = descs + seq_tail;

		if (!cip_sfc_is_base_44100(sfc)) {
			// Sample_rate / 8000 is an integer, and precomputed.
			desc->data_blocks = state;
		} else {
			unsigned int phase = state;

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
				desc->data_blocks = 5 + ((phase & 1) ^ (phase == 0 || phase >= 40));
			else
				/* 12 11 11 11 11 ... or 23 22 22 22 22 ... */
				desc->data_blocks = 11 * (sfc >> 1) + (phase == 0);
			if (++phase >= (80 >> (sfc >> 1)))
				phase = 0;
			state = phase;
		}

		seq_tail = (seq_tail + 1) % seq_size;
	}

	s->ctx_data.rx.data_block_state = state;
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

static void pool_ideal_syt_offsets(struct amdtp_stream *s, struct seq_desc *descs,
				   const unsigned int seq_size, unsigned int seq_tail,
				   unsigned int count)
{
	const enum cip_sfc sfc = s->sfc;
	unsigned int last = s->ctx_data.rx.last_syt_offset;
	unsigned int state = s->ctx_data.rx.syt_offset_state;
	int i;

	for (i = 0; i < count; ++i) {
		struct seq_desc *desc = descs + seq_tail;

		desc->syt_offset = calculate_syt_offset(&last, &state, sfc);

		seq_tail = (seq_tail + 1) % seq_size;
	}

	s->ctx_data.rx.last_syt_offset = last;
	s->ctx_data.rx.syt_offset_state = state;
}

static unsigned int compute_syt_offset(unsigned int syt, unsigned int cycle,
				       unsigned int transfer_delay)
{
	unsigned int cycle_lo = (cycle % CYCLES_PER_SECOND) & 0x0f;
	unsigned int syt_cycle_lo = (syt & 0xf000) >> 12;
	unsigned int syt_offset;

	// Round up.
	if (syt_cycle_lo < cycle_lo)
		syt_cycle_lo += CIP_SYT_CYCLE_MODULUS;
	syt_cycle_lo -= cycle_lo;

	// Subtract transfer delay so that the synchronization offset is not so large
	// at transmission.
	syt_offset = syt_cycle_lo * TICKS_PER_CYCLE + (syt & 0x0fff);
	if (syt_offset < transfer_delay)
		syt_offset += CIP_SYT_CYCLE_MODULUS * TICKS_PER_CYCLE;

	return syt_offset - transfer_delay;
}

// Both of the producer and consumer of the queue runs in the same clock of IEEE 1394 bus.
// Additionally, the sequence of tx packets is severely checked against any discontinuity
// before filling entries in the queue. The calculation is safe even if it looks fragile by
// overrun.
static unsigned int calculate_cached_cycle_count(struct amdtp_stream *s, unsigned int head)
{
	const unsigned int cache_size = s->ctx_data.tx.cache.size;
	unsigned int cycles = s->ctx_data.tx.cache.tail;

	if (cycles < head)
		cycles += cache_size;
	cycles -= head;

	return cycles;
}

static void cache_seq(struct amdtp_stream *s, const struct pkt_desc *descs, unsigned int desc_count)
{
	const unsigned int transfer_delay = s->transfer_delay;
	const unsigned int cache_size = s->ctx_data.tx.cache.size;
	struct seq_desc *cache = s->ctx_data.tx.cache.descs;
	unsigned int cache_tail = s->ctx_data.tx.cache.tail;
	bool aware_syt = !(s->flags & CIP_UNAWARE_SYT);
	int i;

	for (i = 0; i < desc_count; ++i) {
		struct seq_desc *dst = cache + cache_tail;
		const struct pkt_desc *src = descs + i;

		if (aware_syt && src->syt != CIP_SYT_NO_INFO)
			dst->syt_offset = compute_syt_offset(src->syt, src->cycle, transfer_delay);
		else
			dst->syt_offset = CIP_SYT_NO_INFO;
		dst->data_blocks = src->data_blocks;

		cache_tail = (cache_tail + 1) % cache_size;
	}

	s->ctx_data.tx.cache.tail = cache_tail;
}

static void pool_ideal_seq_descs(struct amdtp_stream *s, unsigned int count)
{
	struct seq_desc *descs = s->ctx_data.rx.seq.descs;
	unsigned int seq_tail = s->ctx_data.rx.seq.tail;
	const unsigned int seq_size = s->ctx_data.rx.seq.size;

	pool_ideal_syt_offsets(s, descs, seq_size, seq_tail, count);

	if (s->flags & CIP_BLOCKING)
		pool_blocking_data_blocks(s, descs, seq_size, seq_tail, count);
	else
		pool_ideal_nonblocking_data_blocks(s, descs, seq_size, seq_tail, count);

	s->ctx_data.rx.seq.tail = (seq_tail + count) % seq_size;
}

static void pool_replayed_seq(struct amdtp_stream *s, unsigned int count)
{
	struct amdtp_stream *target = s->ctx_data.rx.replay_target;
	const struct seq_desc *cache = target->ctx_data.tx.cache.descs;
	const unsigned int cache_size = target->ctx_data.tx.cache.size;
	unsigned int cache_head = s->ctx_data.rx.cache_head;
	struct seq_desc *descs = s->ctx_data.rx.seq.descs;
	const unsigned int seq_size = s->ctx_data.rx.seq.size;
	unsigned int seq_tail = s->ctx_data.rx.seq.tail;
	int i;

	for (i = 0; i < count; ++i) {
		descs[seq_tail] = cache[cache_head];
		seq_tail = (seq_tail + 1) % seq_size;
		cache_head = (cache_head + 1) % cache_size;
	}

	s->ctx_data.rx.seq.tail = seq_tail;
	s->ctx_data.rx.cache_head = cache_head;
}

static void pool_seq_descs(struct amdtp_stream *s, unsigned int count)
{
	struct amdtp_domain *d = s->domain;

	if (!d->replay.enable || !s->ctx_data.rx.replay_target) {
		pool_ideal_seq_descs(s, count);
	} else {
		if (!d->replay.on_the_fly) {
			pool_replayed_seq(s, count);
		} else {
			struct amdtp_stream *tx = s->ctx_data.rx.replay_target;
			const unsigned int cache_size = tx->ctx_data.tx.cache.size;
			const unsigned int cache_head = s->ctx_data.rx.cache_head;
			unsigned int cached_cycles = calculate_cached_cycle_count(tx, cache_head);

			if (cached_cycles > count && cached_cycles > cache_size / 2)
				pool_replayed_seq(s, count);
			else
				pool_ideal_seq_descs(s, count);
		}
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

		// The program in user process should periodically check the status of intermediate
		// buffer associated to PCM substream to process PCM frames in the buffer, instead
		// of receiving notification of period elapsed by poll wait.
		if (!pcm->runtime->no_period_wakeup) {
			if (in_softirq()) {
				// In software IRQ context for 1394 OHCI.
				snd_pcm_period_elapsed(pcm);
			} else {
				// In process context of ALSA PCM application under acquired lock of
				// PCM substream.
				snd_pcm_period_elapsed_under_stream_lock(pcm);
			}
		}
	}
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
				struct fw_iso_packet *params, unsigned int header_length,
				unsigned int data_blocks,
				unsigned int data_block_counter,
				unsigned int syt, unsigned int index)
{
	unsigned int payload_length;
	__be32 *cip_header;

	payload_length = data_blocks * sizeof(__be32) * s->data_block_quadlets;
	params->payload_length = payload_length;

	if (header_length > 0) {
		cip_header = (__be32 *)params->header;
		generate_cip_header(s, cip_header, data_block_counter, syt);
		params->header_length = header_length;
	} else {
		cip_header = NULL;
	}

	trace_amdtp_packet(s, cycle, cip_header, payload_length + header_length, data_blocks,
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
	if (payload_length == 0 || (fmt == CIP_FMT_AM && fdf == AMDTP_FDF_NO_DATA)) {
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

		*data_blocks = payload_length / sizeof(__be32) / data_block_quadlets;
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

		if (!(s->flags & CIP_DBC_IS_PAYLOAD_QUADLETS)) {
			if (*data_blocks > 0 && s->ctx_data.tx.dbc_interval > 0)
				dbc_interval = s->ctx_data.tx.dbc_interval;
			else
				dbc_interval = *data_blocks;
		} else {
			dbc_interval = payload_length / sizeof(__be32);
		}

		lost = dbc != ((*data_block_counter + dbc_interval) & 0xff);
	}

	if (lost) {
		dev_err(&s->unit->device,
			"Detect discontinuity of CIP: %02X %02X\n",
			*data_block_counter, dbc);
		return -EIO;
	}

	*data_block_counter = dbc;

	if (!(s->flags & CIP_UNAWARE_SYT))
		*syt = cip_header[1] & CIP_SYT_MASK;

	return 0;
}

static int parse_ir_ctx_header(struct amdtp_stream *s, unsigned int cycle,
			       const __be32 *ctx_header,
			       unsigned int *data_blocks,
			       unsigned int *data_block_counter,
			       unsigned int *syt, unsigned int packet_index, unsigned int index)
{
	unsigned int payload_length;
	const __be32 *cip_header;
	unsigned int cip_header_size;

	payload_length = be32_to_cpu(ctx_header[0]) >> ISO_DATA_LENGTH_SHIFT;

	if (!(s->flags & CIP_NO_HEADER))
		cip_header_size = CIP_HEADER_SIZE;
	else
		cip_header_size = 0;

	if (payload_length > cip_header_size + s->ctx_data.tx.max_ctx_payload_length) {
		dev_err(&s->unit->device,
			"Detect jumbo payload: %04x %04x\n",
			payload_length, cip_header_size + s->ctx_data.tx.max_ctx_payload_length);
		return -EIO;
	}

	if (cip_header_size > 0) {
		if (payload_length >= cip_header_size) {
			int err;

			cip_header = ctx_header + IR_CTX_HEADER_DEFAULT_QUADLETS;
			err = check_cip_header(s, cip_header, payload_length - cip_header_size,
					       data_blocks, data_block_counter, syt);
			if (err < 0)
				return err;
		} else {
			// Handle the cycle so that empty packet arrives.
			cip_header = NULL;
			*data_blocks = 0;
			*syt = 0;
		}
	} else {
		cip_header = NULL;
		*data_blocks = payload_length / sizeof(__be32) / s->data_block_quadlets;
		*syt = 0;

		if (*data_block_counter == UINT_MAX)
			*data_block_counter = 0;
	}

	trace_amdtp_packet(s, cycle, cip_header, payload_length, *data_blocks,
			   *data_block_counter, packet_index, index);

	return 0;
}

// In CYCLE_TIMER register of IEEE 1394, 7 bits are used to represent second. On
// the other hand, in DMA descriptors of 1394 OHCI, 3 bits are used to represent
// it. Thus, via Linux firewire subsystem, we can get the 3 bits for second.
static inline u32 compute_ohci_cycle_count(__be32 ctx_header_tstamp)
{
	u32 tstamp = be32_to_cpu(ctx_header_tstamp) & HEADER_TSTAMP_MASK;
	return (((tstamp >> 13) & 0x07) * 8000) + (tstamp & 0x1fff);
}

static inline u32 increment_ohci_cycle_count(u32 cycle, unsigned int addend)
{
	cycle += addend;
	if (cycle >= OHCI_SECOND_MODULUS * CYCLES_PER_SECOND)
		cycle -= OHCI_SECOND_MODULUS * CYCLES_PER_SECOND;
	return cycle;
}

static int compare_ohci_cycle_count(u32 lval, u32 rval)
{
	if (lval == rval)
		return 0;
	else if (lval < rval && rval - lval < OHCI_SECOND_MODULUS * CYCLES_PER_SECOND / 2)
		return -1;
	else
		return 1;
}

// Align to actual cycle count for the packet which is going to be scheduled.
// This module queued the same number of isochronous cycle as the size of queue
// to kip isochronous cycle, therefore it's OK to just increment the cycle by
// the size of queue for scheduled cycle.
static inline u32 compute_ohci_it_cycle(const __be32 ctx_header_tstamp,
					unsigned int queue_size)
{
	u32 cycle = compute_ohci_cycle_count(ctx_header_tstamp);
	return increment_ohci_cycle_count(cycle, queue_size);
}

static int generate_device_pkt_descs(struct amdtp_stream *s,
				     struct pkt_desc *descs,
				     const __be32 *ctx_header,
				     unsigned int packets,
				     unsigned int *desc_count)
{
	unsigned int next_cycle = s->next_cycle;
	unsigned int dbc = s->data_block_counter;
	unsigned int packet_index = s->packet_index;
	unsigned int queue_size = s->queue_size;
	int i;
	int err;

	*desc_count = 0;
	for (i = 0; i < packets; ++i) {
		struct pkt_desc *desc = descs + *desc_count;
		unsigned int cycle;
		bool lost;
		unsigned int data_blocks;
		unsigned int syt;

		cycle = compute_ohci_cycle_count(ctx_header[1]);
		lost = (next_cycle != cycle);
		if (lost) {
			if (s->flags & CIP_NO_HEADER) {
				// Fireface skips transmission just for an isoc cycle corresponding
				// to empty packet.
				unsigned int prev_cycle = next_cycle;

				next_cycle = increment_ohci_cycle_count(next_cycle, 1);
				lost = (next_cycle != cycle);
				if (!lost) {
					// Prepare a description for the skipped cycle for
					// sequence replay.
					desc->cycle = prev_cycle;
					desc->syt = 0;
					desc->data_blocks = 0;
					desc->data_block_counter = dbc;
					desc->ctx_payload = NULL;
					++desc;
					++(*desc_count);
				}
			} else if (s->flags & CIP_JUMBO_PAYLOAD) {
				// OXFW970 skips transmission for several isoc cycles during
				// asynchronous transaction. The sequence replay is impossible due
				// to the reason.
				unsigned int safe_cycle = increment_ohci_cycle_count(next_cycle,
								IR_JUMBO_PAYLOAD_MAX_SKIP_CYCLES);
				lost = (compare_ohci_cycle_count(safe_cycle, cycle) < 0);
			}
			if (lost) {
				dev_err(&s->unit->device, "Detect discontinuity of cycle: %d %d\n",
					next_cycle, cycle);
				return -EIO;
			}
		}

		err = parse_ir_ctx_header(s, cycle, ctx_header, &data_blocks, &dbc, &syt,
					  packet_index, i);
		if (err < 0)
			return err;

		desc->cycle = cycle;
		desc->syt = syt;
		desc->data_blocks = data_blocks;
		desc->data_block_counter = dbc;
		desc->ctx_payload = s->buffer.packets[packet_index].buffer;

		if (!(s->flags & CIP_DBC_IS_END_EVENT))
			dbc = (dbc + desc->data_blocks) & 0xff;

		next_cycle = increment_ohci_cycle_count(next_cycle, 1);
		++(*desc_count);
		ctx_header += s->ctx_data.tx.ctx_header_size / sizeof(*ctx_header);
		packet_index = (packet_index + 1) % queue_size;
	}

	s->next_cycle = next_cycle;
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

static void generate_pkt_descs(struct amdtp_stream *s, const __be32 *ctx_header, unsigned int packets)
{
	struct pkt_desc *descs = s->pkt_descs;
	const struct seq_desc *seq_descs = s->ctx_data.rx.seq.descs;
	const unsigned int seq_size = s->ctx_data.rx.seq.size;
	unsigned int dbc = s->data_block_counter;
	unsigned int seq_head = s->ctx_data.rx.seq.head;
	bool aware_syt = !(s->flags & CIP_UNAWARE_SYT);
	int i;

	for (i = 0; i < packets; ++i) {
		struct pkt_desc *desc = descs + i;
		unsigned int index = (s->packet_index + i) % s->queue_size;
		const struct seq_desc *seq = seq_descs + seq_head;

		desc->cycle = compute_ohci_it_cycle(*ctx_header, s->queue_size);

		if (aware_syt && seq->syt_offset != CIP_SYT_NO_INFO)
			desc->syt = compute_syt(seq->syt_offset, desc->cycle, s->transfer_delay);
		else
			desc->syt = CIP_SYT_NO_INFO;

		desc->data_blocks = seq->data_blocks;

		if (s->flags & CIP_DBC_IS_END_EVENT)
			dbc = (dbc + desc->data_blocks) & 0xff;

		desc->data_block_counter = dbc;

		if (!(s->flags & CIP_DBC_IS_END_EVENT))
			dbc = (dbc + desc->data_blocks) & 0xff;

		desc->ctx_payload = s->buffer.packets[index].buffer;

		seq_head = (seq_head + 1) % seq_size;

		++ctx_header;
	}

	s->data_block_counter = dbc;
	s->ctx_data.rx.seq.head = seq_head;
}

static inline void cancel_stream(struct amdtp_stream *s)
{
	s->packet_index = -1;
	if (in_softirq())
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

static void process_rx_packets(struct fw_iso_context *context, u32 tstamp, size_t header_length,
			       void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	const struct amdtp_domain *d = s->domain;
	const __be32 *ctx_header = header;
	const unsigned int events_per_period = d->events_per_period;
	unsigned int event_count = s->ctx_data.rx.event_count;
	unsigned int pkt_header_length;
	unsigned int packets;
	bool need_hw_irq;
	int i;

	if (s->packet_index < 0)
		return;

	// Calculate the number of packets in buffer and check XRUN.
	packets = header_length / sizeof(*ctx_header);

	pool_seq_descs(s, packets);

	generate_pkt_descs(s, ctx_header, packets);

	process_ctx_payloads(s, s->pkt_descs, packets);

	if (!(s->flags & CIP_NO_HEADER))
		pkt_header_length = IT_PKT_HEADER_SIZE_CIP;
	else
		pkt_header_length = 0;

	if (s == d->irq_target) {
		// At NO_PERIOD_WAKEUP mode, the packets for all IT/IR contexts are processed by
		// the tasks of user process operating ALSA PCM character device by calling ioctl(2)
		// with some requests, instead of scheduled hardware IRQ of an IT context.
		struct snd_pcm_substream *pcm = READ_ONCE(s->pcm);
		need_hw_irq = !pcm || !pcm->runtime->no_period_wakeup;
	} else {
		need_hw_irq = false;
	}

	for (i = 0; i < packets; ++i) {
		const struct pkt_desc *desc = s->pkt_descs + i;
		struct {
			struct fw_iso_packet params;
			__be32 header[CIP_HEADER_QUADLETS];
		} template = { {0}, {0} };
		bool sched_irq = false;

		build_it_pkt_header(s, desc->cycle, &template.params, pkt_header_length,
				    desc->data_blocks, desc->data_block_counter,
				    desc->syt, i);

		if (s == s->domain->irq_target) {
			event_count += desc->data_blocks;
			if (event_count >= events_per_period) {
				event_count -= events_per_period;
				sched_irq = need_hw_irq;
			}
		}

		if (queue_out_packet(s, &template.params, sched_irq) < 0) {
			cancel_stream(s);
			return;
		}
	}

	s->ctx_data.rx.event_count = event_count;
}

static void skip_rx_packets(struct fw_iso_context *context, u32 tstamp, size_t header_length,
			    void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;
	const __be32 *ctx_header = header;
	unsigned int packets;
	unsigned int cycle;
	int i;

	if (s->packet_index < 0)
		return;

	packets = header_length / sizeof(*ctx_header);

	cycle = compute_ohci_it_cycle(ctx_header[packets - 1], s->queue_size);
	s->next_cycle = increment_ohci_cycle_count(cycle, 1);

	for (i = 0; i < packets; ++i) {
		struct fw_iso_packet params = {
			.header_length = 0,
			.payload_length = 0,
		};
		bool sched_irq = (s == d->irq_target && i == packets - 1);

		if (queue_out_packet(s, &params, sched_irq) < 0) {
			cancel_stream(s);
			return;
		}
	}
}

static void irq_target_callback(struct fw_iso_context *context, u32 tstamp, size_t header_length,
				void *header, void *private_data);

static void process_rx_packets_intermediately(struct fw_iso_context *context, u32 tstamp,
					size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;
	__be32 *ctx_header = header;
	const unsigned int queue_size = s->queue_size;
	unsigned int packets;
	unsigned int offset;

	if (s->packet_index < 0)
		return;

	packets = header_length / sizeof(*ctx_header);

	offset = 0;
	while (offset < packets) {
		unsigned int cycle = compute_ohci_it_cycle(ctx_header[offset], queue_size);

		if (compare_ohci_cycle_count(cycle, d->processing_cycle.rx_start) >= 0)
			break;

		++offset;
	}

	if (offset > 0) {
		unsigned int length = sizeof(*ctx_header) * offset;

		skip_rx_packets(context, tstamp, length, ctx_header, private_data);
		if (amdtp_streaming_error(s))
			return;

		ctx_header += offset;
		header_length -= length;
	}

	if (offset < packets) {
		s->ready_processing = true;
		wake_up(&s->ready_wait);

		process_rx_packets(context, tstamp, header_length, ctx_header, private_data);
		if (amdtp_streaming_error(s))
			return;

		if (s == d->irq_target)
			s->context->callback.sc = irq_target_callback;
		else
			s->context->callback.sc = process_rx_packets;
	}
}

static void process_tx_packets(struct fw_iso_context *context, u32 tstamp, size_t header_length,
			       void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	__be32 *ctx_header = header;
	unsigned int packets;
	unsigned int desc_count;
	int i;
	int err;

	if (s->packet_index < 0)
		return;

	// Calculate the number of packets in buffer and check XRUN.
	packets = header_length / s->ctx_data.tx.ctx_header_size;

	desc_count = 0;
	err = generate_device_pkt_descs(s, s->pkt_descs, ctx_header, packets, &desc_count);
	if (err < 0) {
		if (err != -EAGAIN) {
			cancel_stream(s);
			return;
		}
	} else {
		struct amdtp_domain *d = s->domain;

		process_ctx_payloads(s, s->pkt_descs, desc_count);

		if (d->replay.enable)
			cache_seq(s, s->pkt_descs, desc_count);
	}

	for (i = 0; i < packets; ++i) {
		struct fw_iso_packet params = {0};

		if (queue_in_packet(s, &params) < 0) {
			cancel_stream(s);
			return;
		}
	}
}

static void drop_tx_packets(struct fw_iso_context *context, u32 tstamp, size_t header_length,
			    void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	const __be32 *ctx_header = header;
	unsigned int packets;
	unsigned int cycle;
	int i;

	if (s->packet_index < 0)
		return;

	packets = header_length / s->ctx_data.tx.ctx_header_size;

	ctx_header += (packets - 1) * s->ctx_data.tx.ctx_header_size / sizeof(*ctx_header);
	cycle = compute_ohci_cycle_count(ctx_header[1]);
	s->next_cycle = increment_ohci_cycle_count(cycle, 1);

	for (i = 0; i < packets; ++i) {
		struct fw_iso_packet params = {0};

		if (queue_in_packet(s, &params) < 0) {
			cancel_stream(s);
			return;
		}
	}
}

static void process_tx_packets_intermediately(struct fw_iso_context *context, u32 tstamp,
					size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;
	__be32 *ctx_header;
	unsigned int packets;
	unsigned int offset;

	if (s->packet_index < 0)
		return;

	packets = header_length / s->ctx_data.tx.ctx_header_size;

	offset = 0;
	ctx_header = header;
	while (offset < packets) {
		unsigned int cycle = compute_ohci_cycle_count(ctx_header[1]);

		if (compare_ohci_cycle_count(cycle, d->processing_cycle.tx_start) >= 0)
			break;

		ctx_header += s->ctx_data.tx.ctx_header_size / sizeof(__be32);
		++offset;
	}

	ctx_header = header;

	if (offset > 0) {
		size_t length = s->ctx_data.tx.ctx_header_size * offset;

		drop_tx_packets(context, tstamp, length, ctx_header, s);
		if (amdtp_streaming_error(s))
			return;

		ctx_header += length / sizeof(*ctx_header);
		header_length -= length;
	}

	if (offset < packets) {
		s->ready_processing = true;
		wake_up(&s->ready_wait);

		process_tx_packets(context, tstamp, header_length, ctx_header, s);
		if (amdtp_streaming_error(s))
			return;

		context->callback.sc = process_tx_packets;
	}
}

static void drop_tx_packets_initially(struct fw_iso_context *context, u32 tstamp,
				      size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;
	__be32 *ctx_header;
	unsigned int count;
	unsigned int events;
	int i;

	if (s->packet_index < 0)
		return;

	count = header_length / s->ctx_data.tx.ctx_header_size;

	// Attempt to detect any event in the batch of packets.
	events = 0;
	ctx_header = header;
	for (i = 0; i < count; ++i) {
		unsigned int payload_quads =
			(be32_to_cpu(*ctx_header) >> ISO_DATA_LENGTH_SHIFT) / sizeof(__be32);
		unsigned int data_blocks;

		if (s->flags & CIP_NO_HEADER) {
			data_blocks = payload_quads / s->data_block_quadlets;
		} else {
			__be32 *cip_headers = ctx_header + IR_CTX_HEADER_DEFAULT_QUADLETS;

			if (payload_quads < CIP_HEADER_QUADLETS) {
				data_blocks = 0;
			} else {
				payload_quads -= CIP_HEADER_QUADLETS;

				if (s->flags & CIP_UNAWARE_SYT) {
					data_blocks = payload_quads / s->data_block_quadlets;
				} else {
					u32 cip1 = be32_to_cpu(cip_headers[1]);

					// NODATA packet can includes any data blocks but they are
					// not available as event.
					if ((cip1 & CIP_NO_DATA) == CIP_NO_DATA)
						data_blocks = 0;
					else
						data_blocks = payload_quads / s->data_block_quadlets;
				}
			}
		}

		events += data_blocks;

		ctx_header += s->ctx_data.tx.ctx_header_size / sizeof(__be32);
	}

	drop_tx_packets(context, tstamp, header_length, header, s);

	if (events > 0)
		s->ctx_data.tx.event_starts = true;

	// Decide the cycle count to begin processing content of packet in IR contexts.
	{
		unsigned int stream_count = 0;
		unsigned int event_starts_count = 0;
		unsigned int cycle = UINT_MAX;

		list_for_each_entry(s, &d->streams, list) {
			if (s->direction == AMDTP_IN_STREAM) {
				++stream_count;
				if (s->ctx_data.tx.event_starts)
					++event_starts_count;
			}
		}

		if (stream_count == event_starts_count) {
			unsigned int next_cycle;

			list_for_each_entry(s, &d->streams, list) {
				if (s->direction != AMDTP_IN_STREAM)
					continue;

				next_cycle = increment_ohci_cycle_count(s->next_cycle,
								d->processing_cycle.tx_init_skip);
				if (cycle == UINT_MAX ||
				    compare_ohci_cycle_count(next_cycle, cycle) > 0)
					cycle = next_cycle;

				s->context->callback.sc = process_tx_packets_intermediately;
			}

			d->processing_cycle.tx_start = cycle;
		}
	}
}

static void process_ctxs_in_domain(struct amdtp_domain *d)
{
	struct amdtp_stream *s;

	list_for_each_entry(s, &d->streams, list) {
		if (s != d->irq_target && amdtp_stream_running(s))
			fw_iso_context_flush_completions(s->context);

		if (amdtp_streaming_error(s))
			goto error;
	}

	return;
error:
	if (amdtp_stream_running(d->irq_target))
		cancel_stream(d->irq_target);

	list_for_each_entry(s, &d->streams, list) {
		if (amdtp_stream_running(s))
			cancel_stream(s);
	}
}

static void irq_target_callback(struct fw_iso_context *context, u32 tstamp, size_t header_length,
				void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;

	process_rx_packets(context, tstamp, header_length, header, private_data);
	process_ctxs_in_domain(d);
}

static void irq_target_callback_intermediately(struct fw_iso_context *context, u32 tstamp,
					size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;

	process_rx_packets_intermediately(context, tstamp, header_length, header, private_data);
	process_ctxs_in_domain(d);
}

static void irq_target_callback_skip(struct fw_iso_context *context, u32 tstamp,
				     size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;
	bool ready_to_start;

	skip_rx_packets(context, tstamp, header_length, header, private_data);
	process_ctxs_in_domain(d);

	if (d->replay.enable && !d->replay.on_the_fly) {
		unsigned int rx_count = 0;
		unsigned int rx_ready_count = 0;
		struct amdtp_stream *rx;

		list_for_each_entry(rx, &d->streams, list) {
			struct amdtp_stream *tx;
			unsigned int cached_cycles;

			if (rx->direction != AMDTP_OUT_STREAM)
				continue;
			++rx_count;

			tx = rx->ctx_data.rx.replay_target;
			cached_cycles = calculate_cached_cycle_count(tx, 0);
			if (cached_cycles > tx->ctx_data.tx.cache.size / 2)
				++rx_ready_count;
		}

		ready_to_start = (rx_count == rx_ready_count);
	} else {
		ready_to_start = true;
	}

	// Decide the cycle count to begin processing content of packet in IT contexts. All of IT
	// contexts are expected to start and get callback when reaching here.
	if (ready_to_start) {
		unsigned int cycle = s->next_cycle;
		list_for_each_entry(s, &d->streams, list) {
			if (s->direction != AMDTP_OUT_STREAM)
				continue;

			if (compare_ohci_cycle_count(s->next_cycle, cycle) > 0)
				cycle = s->next_cycle;

			if (s == d->irq_target)
				s->context->callback.sc = irq_target_callback_intermediately;
			else
				s->context->callback.sc = process_rx_packets_intermediately;
		}

		d->processing_cycle.rx_start = cycle;
	}
}

// This is executed one time. For in-stream, first packet has come. For out-stream, prepared to
// transmit first packet.
static void amdtp_stream_first_callback(struct fw_iso_context *context,
					u32 tstamp, size_t header_length,
					void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	struct amdtp_domain *d = s->domain;

	if (s->direction == AMDTP_IN_STREAM) {
		context->callback.sc = drop_tx_packets_initially;
	} else {
		if (s == d->irq_target)
			context->callback.sc = irq_target_callback_skip;
		else
			context->callback.sc = skip_rx_packets;
	}

	context->callback.sc(context, tstamp, header_length, header, s);
}

/**
 * amdtp_stream_start - start transferring packets
 * @s: the AMDTP stream to start
 * @channel: the isochronous channel on the bus
 * @speed: firewire speed code
 * @queue_size: The number of packets in the queue.
 * @idle_irq_interval: the interval to queue packet during initial state.
 *
 * The stream cannot be started until it has been configured with
 * amdtp_stream_set_parameters() and it must be started before any PCM or MIDI
 * device can be started.
 */
static int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed,
			      unsigned int queue_size, unsigned int idle_irq_interval)
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
	if (s->direction == AMDTP_IN_STREAM) {
		dir = DMA_FROM_DEVICE;
		type = FW_ISO_CONTEXT_RECEIVE;
		if (!(s->flags & CIP_NO_HEADER))
			ctx_header_size = IR_CTX_HEADER_SIZE_CIP;
		else
			ctx_header_size = IR_CTX_HEADER_SIZE_NO_CIP;
	} else {
		dir = DMA_TO_DEVICE;
		type = FW_ISO_CONTEXT_TRANSMIT;
		ctx_header_size = 0;	// No effect for IT context.
	}
	max_ctx_payload_size = amdtp_stream_get_max_ctx_payload_size(s);

	err = iso_packets_buffer_init(&s->buffer, s->unit, queue_size, max_ctx_payload_size, dir);
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
		s->ctx_data.tx.event_starts = false;

		if (s->domain->replay.enable) {
			// struct fw_iso_context.drop_overflow_headers is false therefore it's
			// possible to cache much unexpectedly.
			s->ctx_data.tx.cache.size = max_t(unsigned int, s->syt_interval * 2,
							  queue_size * 3 / 2);
			s->ctx_data.tx.cache.tail = 0;
			s->ctx_data.tx.cache.descs = kcalloc(s->ctx_data.tx.cache.size,
						sizeof(*s->ctx_data.tx.cache.descs), GFP_KERNEL);
			if (!s->ctx_data.tx.cache.descs) {
				err = -ENOMEM;
				goto err_context;
			}
		}
	} else {
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

		s->ctx_data.rx.seq.descs = kcalloc(queue_size, sizeof(*s->ctx_data.rx.seq.descs), GFP_KERNEL);
		if (!s->ctx_data.rx.seq.descs) {
			err = -ENOMEM;
			goto err_context;
		}
		s->ctx_data.rx.seq.size = queue_size;
		s->ctx_data.rx.seq.tail = 0;
		s->ctx_data.rx.seq.head = 0;

		entry = &initial_state[s->sfc];
		s->ctx_data.rx.data_block_state = entry->data_block;
		s->ctx_data.rx.syt_offset_state = entry->syt_offset;
		s->ctx_data.rx.last_syt_offset = TICKS_PER_CYCLE;

		s->ctx_data.rx.event_count = 0;
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

	s->ready_processing = false;
	err = fw_iso_context_start(s->context, -1, 0, tag);
	if (err < 0)
		goto err_pkt_descs;

	mutex_unlock(&s->mutex);

	return 0;
err_pkt_descs:
	kfree(s->pkt_descs);
err_context:
	if (s->direction == AMDTP_OUT_STREAM) {
		kfree(s->ctx_data.rx.seq.descs);
	} else {
		if (s->domain->replay.enable)
			kfree(s->ctx_data.tx.cache.descs);
	}
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

	// Process isochronous packets queued till recent isochronous cycle to handle PCM frames.
	if (irq_target && amdtp_stream_running(irq_target)) {
		// In software IRQ context, the call causes dead-lock to disable the tasklet
		// synchronously.
		if (!in_softirq())
			fw_iso_context_flush_completions(irq_target->context);
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
	if (irq_target && amdtp_stream_running(irq_target))
		fw_iso_context_flush_completions(irq_target->context);

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

	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);
	kfree(s->pkt_descs);

	if (s->direction == AMDTP_OUT_STREAM) {
		kfree(s->ctx_data.rx.seq.descs);
	} else {
		if (s->domain->replay.enable)
			kfree(s->ctx_data.tx.cache.descs);
	}

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

// Make the reference from rx stream to tx stream for sequence replay. When the number of tx streams
// is less than the number of rx streams, the first tx stream is selected.
static int make_association(struct amdtp_domain *d)
{
	unsigned int dst_index = 0;
	struct amdtp_stream *rx;

	// Make association to replay target.
	list_for_each_entry(rx, &d->streams, list) {
		if (rx->direction == AMDTP_OUT_STREAM) {
			unsigned int src_index = 0;
			struct amdtp_stream *tx = NULL;
			struct amdtp_stream *s;

			list_for_each_entry(s, &d->streams, list) {
				if (s->direction == AMDTP_IN_STREAM) {
					if (dst_index == src_index) {
						tx = s;
						break;
					}

					++src_index;
				}
			}
			if (!tx) {
				// Select the first entry.
				list_for_each_entry(s, &d->streams, list) {
					if (s->direction == AMDTP_IN_STREAM) {
						tx = s;
						break;
					}
				}
				// No target is available to replay sequence.
				if (!tx)
					return -EINVAL;
			}

			rx->ctx_data.rx.replay_target = tx;
			rx->ctx_data.rx.cache_head = 0;

			++dst_index;
		}
	}

	return 0;
}

/**
 * amdtp_domain_start - start sending packets for isoc context in the domain.
 * @d: the AMDTP domain.
 * @tx_init_skip_cycles: the number of cycles to skip processing packets at initial stage of IR
 *			 contexts.
 * @replay_seq: whether to replay the sequence of packet in IR context for the sequence of packet in
 *		IT context.
 * @replay_on_the_fly: transfer rx packets according to nominal frequency, then begin to replay
 *		       according to arrival of events in tx packets.
 */
int amdtp_domain_start(struct amdtp_domain *d, unsigned int tx_init_skip_cycles, bool replay_seq,
		       bool replay_on_the_fly)
{
	unsigned int events_per_buffer = d->events_per_buffer;
	unsigned int events_per_period = d->events_per_period;
	unsigned int queue_size;
	struct amdtp_stream *s;
	bool found = false;
	int err;

	if (replay_seq) {
		err = make_association(d);
		if (err < 0)
			return err;
	}
	d->replay.enable = replay_seq;
	d->replay.on_the_fly = replay_on_the_fly;

	// Select an IT context as IRQ target.
	list_for_each_entry(s, &d->streams, list) {
		if (s->direction == AMDTP_OUT_STREAM) {
			found = true;
			break;
		}
	}
	if (!found)
		return -ENXIO;
	d->irq_target = s;

	d->processing_cycle.tx_init_skip = tx_init_skip_cycles;

	// This is a case that AMDTP streams in domain run just for MIDI
	// substream. Use the number of events equivalent to 10 msec as
	// interval of hardware IRQ.
	if (events_per_period == 0)
		events_per_period = amdtp_rate_table[d->irq_target->sfc] / 100;
	if (events_per_buffer == 0)
		events_per_buffer = events_per_period * 3;

	queue_size = DIV_ROUND_UP(CYCLES_PER_SECOND * events_per_buffer,
				  amdtp_rate_table[d->irq_target->sfc]);

	list_for_each_entry(s, &d->streams, list) {
		unsigned int idle_irq_interval = 0;

		if (s->direction == AMDTP_OUT_STREAM && s == d->irq_target) {
			idle_irq_interval = DIV_ROUND_UP(CYCLES_PER_SECOND * events_per_period,
							 amdtp_rate_table[d->irq_target->sfc]);
		}

		// Starts immediately but actually DMA context starts several hundred cycles later.
		err = amdtp_stream_start(s, s->channel, s->speed, queue_size, idle_irq_interval);
		if (err < 0)
			goto error;
	}

	return 0;
error:
	list_for_each_entry(s, &d->streams, list)
		amdtp_stream_stop(s);
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
}
EXPORT_SYMBOL_GPL(amdtp_domain_stop);
