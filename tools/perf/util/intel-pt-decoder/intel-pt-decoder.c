// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pt_decoder.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/compiler.h>

#include "../cache.h"
#include "../util.h"
#include "../auxtrace.h"

#include "intel-pt-insn-decoder.h"
#include "intel-pt-pkt-decoder.h"
#include "intel-pt-decoder.h"
#include "intel-pt-log.h"

#define INTEL_PT_BLK_SIZE 1024

#define BIT63 (((uint64_t)1 << 63))

#define INTEL_PT_RETURN 1

/* Maximum number of loops with no packets consumed i.e. stuck in a loop */
#define INTEL_PT_MAX_LOOPS 10000

struct intel_pt_blk {
	struct intel_pt_blk *prev;
	uint64_t ip[INTEL_PT_BLK_SIZE];
};

struct intel_pt_stack {
	struct intel_pt_blk *blk;
	struct intel_pt_blk *spare;
	int pos;
};

enum intel_pt_pkt_state {
	INTEL_PT_STATE_NO_PSB,
	INTEL_PT_STATE_NO_IP,
	INTEL_PT_STATE_ERR_RESYNC,
	INTEL_PT_STATE_IN_SYNC,
	INTEL_PT_STATE_TNT_CONT,
	INTEL_PT_STATE_TNT,
	INTEL_PT_STATE_TIP,
	INTEL_PT_STATE_TIP_PGD,
	INTEL_PT_STATE_FUP,
	INTEL_PT_STATE_FUP_NO_TIP,
};

static inline bool intel_pt_sample_time(enum intel_pt_pkt_state pkt_state)
{
	switch (pkt_state) {
	case INTEL_PT_STATE_NO_PSB:
	case INTEL_PT_STATE_NO_IP:
	case INTEL_PT_STATE_ERR_RESYNC:
	case INTEL_PT_STATE_IN_SYNC:
	case INTEL_PT_STATE_TNT_CONT:
		return true;
	case INTEL_PT_STATE_TNT:
	case INTEL_PT_STATE_TIP:
	case INTEL_PT_STATE_TIP_PGD:
	case INTEL_PT_STATE_FUP:
	case INTEL_PT_STATE_FUP_NO_TIP:
		return false;
	default:
		return true;
	};
}

#ifdef INTEL_PT_STRICT
#define INTEL_PT_STATE_ERR1	INTEL_PT_STATE_NO_PSB
#define INTEL_PT_STATE_ERR2	INTEL_PT_STATE_NO_PSB
#define INTEL_PT_STATE_ERR3	INTEL_PT_STATE_NO_PSB
#define INTEL_PT_STATE_ERR4	INTEL_PT_STATE_NO_PSB
#else
#define INTEL_PT_STATE_ERR1	(decoder->pkt_state)
#define INTEL_PT_STATE_ERR2	INTEL_PT_STATE_NO_IP
#define INTEL_PT_STATE_ERR3	INTEL_PT_STATE_ERR_RESYNC
#define INTEL_PT_STATE_ERR4	INTEL_PT_STATE_IN_SYNC
#endif

struct intel_pt_decoder {
	int (*get_trace)(struct intel_pt_buffer *buffer, void *data);
	int (*walk_insn)(struct intel_pt_insn *intel_pt_insn,
			 uint64_t *insn_cnt_ptr, uint64_t *ip, uint64_t to_ip,
			 uint64_t max_insn_cnt, void *data);
	bool (*pgd_ip)(uint64_t ip, void *data);
	int (*lookahead)(void *data, intel_pt_lookahead_cb_t cb, void *cb_data);
	void *data;
	struct intel_pt_state state;
	const unsigned char *buf;
	size_t len;
	bool return_compression;
	bool branch_enable;
	bool mtc_insn;
	bool pge;
	bool have_tma;
	bool have_cyc;
	bool fixup_last_mtc;
	bool have_last_ip;
	bool in_psb;
	enum intel_pt_param_flags flags;
	uint64_t pos;
	uint64_t last_ip;
	uint64_t ip;
	uint64_t cr3;
	uint64_t timestamp;
	uint64_t tsc_timestamp;
	uint64_t ref_timestamp;
	uint64_t buf_timestamp;
	uint64_t sample_timestamp;
	uint64_t ret_addr;
	uint64_t ctc_timestamp;
	uint64_t ctc_delta;
	uint64_t cycle_cnt;
	uint64_t cyc_ref_timestamp;
	uint32_t last_mtc;
	uint32_t tsc_ctc_ratio_n;
	uint32_t tsc_ctc_ratio_d;
	uint32_t tsc_ctc_mult;
	uint32_t tsc_slip;
	uint32_t ctc_rem_mask;
	int mtc_shift;
	struct intel_pt_stack stack;
	enum intel_pt_pkt_state pkt_state;
	struct intel_pt_pkt packet;
	struct intel_pt_pkt tnt;
	int pkt_step;
	int pkt_len;
	int last_packet_type;
	unsigned int cbr;
	unsigned int cbr_seen;
	unsigned int max_non_turbo_ratio;
	double max_non_turbo_ratio_fp;
	double cbr_cyc_to_tsc;
	double calc_cyc_to_tsc;
	bool have_calc_cyc_to_tsc;
	int exec_mode;
	unsigned int insn_bytes;
	uint64_t period;
	enum intel_pt_period_type period_type;
	uint64_t tot_insn_cnt;
	uint64_t period_insn_cnt;
	uint64_t period_mask;
	uint64_t period_ticks;
	uint64_t last_masked_timestamp;
	uint64_t tot_cyc_cnt;
	uint64_t sample_tot_cyc_cnt;
	uint64_t base_cyc_cnt;
	uint64_t cyc_cnt_timestamp;
	double tsc_to_cyc;
	bool continuous_period;
	bool overflow;
	bool set_fup_tx_flags;
	bool set_fup_ptw;
	bool set_fup_mwait;
	bool set_fup_pwre;
	bool set_fup_exstop;
	bool sample_cyc;
	unsigned int fup_tx_flags;
	unsigned int tx_flags;
	uint64_t fup_ptw_payload;
	uint64_t fup_mwait_payload;
	uint64_t fup_pwre_payload;
	uint64_t cbr_payload;
	uint64_t timestamp_insn_cnt;
	uint64_t sample_insn_cnt;
	uint64_t stuck_ip;
	int no_progress;
	int stuck_ip_prd;
	int stuck_ip_cnt;
	const unsigned char *next_buf;
	size_t next_len;
	unsigned char temp_buf[INTEL_PT_PKT_MAX_SZ];
};

static uint64_t intel_pt_lower_power_of_2(uint64_t x)
{
	int i;

	for (i = 0; x != 1; i++)
		x >>= 1;

	return x << i;
}

static void intel_pt_setup_period(struct intel_pt_decoder *decoder)
{
	if (decoder->period_type == INTEL_PT_PERIOD_TICKS) {
		uint64_t period;

		period = intel_pt_lower_power_of_2(decoder->period);
		decoder->period_mask  = ~(period - 1);
		decoder->period_ticks = period;
	}
}

static uint64_t multdiv(uint64_t t, uint32_t n, uint32_t d)
{
	if (!d)
		return 0;
	return (t / d) * n + ((t % d) * n) / d;
}

struct intel_pt_decoder *intel_pt_decoder_new(struct intel_pt_params *params)
{
	struct intel_pt_decoder *decoder;

	if (!params->get_trace || !params->walk_insn)
		return NULL;

	decoder = zalloc(sizeof(struct intel_pt_decoder));
	if (!decoder)
		return NULL;

	decoder->get_trace          = params->get_trace;
	decoder->walk_insn          = params->walk_insn;
	decoder->pgd_ip             = params->pgd_ip;
	decoder->lookahead          = params->lookahead;
	decoder->data               = params->data;
	decoder->return_compression = params->return_compression;
	decoder->branch_enable      = params->branch_enable;

	decoder->flags              = params->flags;

	decoder->period             = params->period;
	decoder->period_type        = params->period_type;

	decoder->max_non_turbo_ratio    = params->max_non_turbo_ratio;
	decoder->max_non_turbo_ratio_fp = params->max_non_turbo_ratio;

	intel_pt_setup_period(decoder);

	decoder->mtc_shift = params->mtc_period;
	decoder->ctc_rem_mask = (1 << decoder->mtc_shift) - 1;

	decoder->tsc_ctc_ratio_n = params->tsc_ctc_ratio_n;
	decoder->tsc_ctc_ratio_d = params->tsc_ctc_ratio_d;

	if (!decoder->tsc_ctc_ratio_n)
		decoder->tsc_ctc_ratio_d = 0;

	if (decoder->tsc_ctc_ratio_d) {
		if (!(decoder->tsc_ctc_ratio_n % decoder->tsc_ctc_ratio_d))
			decoder->tsc_ctc_mult = decoder->tsc_ctc_ratio_n /
						decoder->tsc_ctc_ratio_d;
	}

	/*
	 * A TSC packet can slip past MTC packets so that the timestamp appears
	 * to go backwards. One estimate is that can be up to about 40 CPU
	 * cycles, which is certainly less than 0x1000 TSC ticks, but accept
	 * slippage an order of magnitude more to be on the safe side.
	 */
	decoder->tsc_slip = 0x10000;

	intel_pt_log("timestamp: mtc_shift %u\n", decoder->mtc_shift);
	intel_pt_log("timestamp: tsc_ctc_ratio_n %u\n", decoder->tsc_ctc_ratio_n);
	intel_pt_log("timestamp: tsc_ctc_ratio_d %u\n", decoder->tsc_ctc_ratio_d);
	intel_pt_log("timestamp: tsc_ctc_mult %u\n", decoder->tsc_ctc_mult);
	intel_pt_log("timestamp: tsc_slip %#x\n", decoder->tsc_slip);

	return decoder;
}

static void intel_pt_pop_blk(struct intel_pt_stack *stack)
{
	struct intel_pt_blk *blk = stack->blk;

	stack->blk = blk->prev;
	if (!stack->spare)
		stack->spare = blk;
	else
		free(blk);
}

static uint64_t intel_pt_pop(struct intel_pt_stack *stack)
{
	if (!stack->pos) {
		if (!stack->blk)
			return 0;
		intel_pt_pop_blk(stack);
		if (!stack->blk)
			return 0;
		stack->pos = INTEL_PT_BLK_SIZE;
	}
	return stack->blk->ip[--stack->pos];
}

static int intel_pt_alloc_blk(struct intel_pt_stack *stack)
{
	struct intel_pt_blk *blk;

	if (stack->spare) {
		blk = stack->spare;
		stack->spare = NULL;
	} else {
		blk = malloc(sizeof(struct intel_pt_blk));
		if (!blk)
			return -ENOMEM;
	}

	blk->prev = stack->blk;
	stack->blk = blk;
	stack->pos = 0;
	return 0;
}

static int intel_pt_push(struct intel_pt_stack *stack, uint64_t ip)
{
	int err;

	if (!stack->blk || stack->pos == INTEL_PT_BLK_SIZE) {
		err = intel_pt_alloc_blk(stack);
		if (err)
			return err;
	}

	stack->blk->ip[stack->pos++] = ip;
	return 0;
}

static void intel_pt_clear_stack(struct intel_pt_stack *stack)
{
	while (stack->blk)
		intel_pt_pop_blk(stack);
	stack->pos = 0;
}

static void intel_pt_free_stack(struct intel_pt_stack *stack)
{
	intel_pt_clear_stack(stack);
	zfree(&stack->blk);
	zfree(&stack->spare);
}

void intel_pt_decoder_free(struct intel_pt_decoder *decoder)
{
	intel_pt_free_stack(&decoder->stack);
	free(decoder);
}

static int intel_pt_ext_err(int code)
{
	switch (code) {
	case -ENOMEM:
		return INTEL_PT_ERR_NOMEM;
	case -ENOSYS:
		return INTEL_PT_ERR_INTERN;
	case -EBADMSG:
		return INTEL_PT_ERR_BADPKT;
	case -ENODATA:
		return INTEL_PT_ERR_NODATA;
	case -EILSEQ:
		return INTEL_PT_ERR_NOINSN;
	case -ENOENT:
		return INTEL_PT_ERR_MISMAT;
	case -EOVERFLOW:
		return INTEL_PT_ERR_OVR;
	case -ENOSPC:
		return INTEL_PT_ERR_LOST;
	case -ELOOP:
		return INTEL_PT_ERR_NELOOP;
	default:
		return INTEL_PT_ERR_UNK;
	}
}

static const char *intel_pt_err_msgs[] = {
	[INTEL_PT_ERR_NOMEM]  = "Memory allocation failed",
	[INTEL_PT_ERR_INTERN] = "Internal error",
	[INTEL_PT_ERR_BADPKT] = "Bad packet",
	[INTEL_PT_ERR_NODATA] = "No more data",
	[INTEL_PT_ERR_NOINSN] = "Failed to get instruction",
	[INTEL_PT_ERR_MISMAT] = "Trace doesn't match instruction",
	[INTEL_PT_ERR_OVR]    = "Overflow packet",
	[INTEL_PT_ERR_LOST]   = "Lost trace data",
	[INTEL_PT_ERR_UNK]    = "Unknown error!",
	[INTEL_PT_ERR_NELOOP] = "Never-ending loop",
};

int intel_pt__strerror(int code, char *buf, size_t buflen)
{
	if (code < 1 || code >= INTEL_PT_ERR_MAX)
		code = INTEL_PT_ERR_UNK;
	strlcpy(buf, intel_pt_err_msgs[code], buflen);
	return 0;
}

static uint64_t intel_pt_calc_ip(const struct intel_pt_pkt *packet,
				 uint64_t last_ip)
{
	uint64_t ip;

	switch (packet->count) {
	case 1:
		ip = (last_ip & (uint64_t)0xffffffffffff0000ULL) |
		     packet->payload;
		break;
	case 2:
		ip = (last_ip & (uint64_t)0xffffffff00000000ULL) |
		     packet->payload;
		break;
	case 3:
		ip = packet->payload;
		/* Sign-extend 6-byte ip */
		if (ip & (uint64_t)0x800000000000ULL)
			ip |= (uint64_t)0xffff000000000000ULL;
		break;
	case 4:
		ip = (last_ip & (uint64_t)0xffff000000000000ULL) |
		     packet->payload;
		break;
	case 6:
		ip = packet->payload;
		break;
	default:
		return 0;
	}

	return ip;
}

static inline void intel_pt_set_last_ip(struct intel_pt_decoder *decoder)
{
	decoder->last_ip = intel_pt_calc_ip(&decoder->packet, decoder->last_ip);
	decoder->have_last_ip = true;
}

static inline void intel_pt_set_ip(struct intel_pt_decoder *decoder)
{
	intel_pt_set_last_ip(decoder);
	decoder->ip = decoder->last_ip;
}

static void intel_pt_decoder_log_packet(struct intel_pt_decoder *decoder)
{
	intel_pt_log_packet(&decoder->packet, decoder->pkt_len, decoder->pos,
			    decoder->buf);
}

static int intel_pt_bug(struct intel_pt_decoder *decoder)
{
	intel_pt_log("ERROR: Internal error\n");
	decoder->pkt_state = INTEL_PT_STATE_NO_PSB;
	return -ENOSYS;
}

static inline void intel_pt_clear_tx_flags(struct intel_pt_decoder *decoder)
{
	decoder->tx_flags = 0;
}

static inline void intel_pt_update_in_tx(struct intel_pt_decoder *decoder)
{
	decoder->tx_flags = decoder->packet.payload & INTEL_PT_IN_TX;
}

static int intel_pt_bad_packet(struct intel_pt_decoder *decoder)
{
	intel_pt_clear_tx_flags(decoder);
	decoder->have_tma = false;
	decoder->pkt_len = 1;
	decoder->pkt_step = 1;
	intel_pt_decoder_log_packet(decoder);
	if (decoder->pkt_state != INTEL_PT_STATE_NO_PSB) {
		intel_pt_log("ERROR: Bad packet\n");
		decoder->pkt_state = INTEL_PT_STATE_ERR1;
	}
	return -EBADMSG;
}

static inline void intel_pt_update_sample_time(struct intel_pt_decoder *decoder)
{
	decoder->sample_timestamp = decoder->timestamp;
	decoder->sample_insn_cnt = decoder->timestamp_insn_cnt;
}

static void intel_pt_reposition(struct intel_pt_decoder *decoder)
{
	decoder->ip = 0;
	decoder->pkt_state = INTEL_PT_STATE_NO_PSB;
	decoder->timestamp = 0;
	decoder->have_tma = false;
}

static int intel_pt_get_data(struct intel_pt_decoder *decoder, bool reposition)
{
	struct intel_pt_buffer buffer = { .buf = 0, };
	int ret;

	decoder->pkt_step = 0;

	intel_pt_log("Getting more data\n");
	ret = decoder->get_trace(&buffer, decoder->data);
	if (ret)
		return ret;
	decoder->buf = buffer.buf;
	decoder->len = buffer.len;
	if (!decoder->len) {
		intel_pt_log("No more data\n");
		return -ENODATA;
	}
	decoder->buf_timestamp = buffer.ref_timestamp;
	if (!buffer.consecutive || reposition) {
		intel_pt_reposition(decoder);
		decoder->ref_timestamp = buffer.ref_timestamp;
		decoder->state.trace_nr = buffer.trace_nr;
		intel_pt_log("Reference timestamp 0x%" PRIx64 "\n",
			     decoder->ref_timestamp);
		return -ENOLINK;
	}

	return 0;
}

static int intel_pt_get_next_data(struct intel_pt_decoder *decoder,
				  bool reposition)
{
	if (!decoder->next_buf)
		return intel_pt_get_data(decoder, reposition);

	decoder->buf = decoder->next_buf;
	decoder->len = decoder->next_len;
	decoder->next_buf = 0;
	decoder->next_len = 0;
	return 0;
}

static int intel_pt_get_split_packet(struct intel_pt_decoder *decoder)
{
	unsigned char *buf = decoder->temp_buf;
	size_t old_len, len, n;
	int ret;

	old_len = decoder->len;
	len = decoder->len;
	memcpy(buf, decoder->buf, len);

	ret = intel_pt_get_data(decoder, false);
	if (ret) {
		decoder->pos += old_len;
		return ret < 0 ? ret : -EINVAL;
	}

	n = INTEL_PT_PKT_MAX_SZ - len;
	if (n > decoder->len)
		n = decoder->len;
	memcpy(buf + len, decoder->buf, n);
	len += n;

	ret = intel_pt_get_packet(buf, len, &decoder->packet);
	if (ret < (int)old_len) {
		decoder->next_buf = decoder->buf;
		decoder->next_len = decoder->len;
		decoder->buf = buf;
		decoder->len = old_len;
		return intel_pt_bad_packet(decoder);
	}

	decoder->next_buf = decoder->buf + (ret - old_len);
	decoder->next_len = decoder->len - (ret - old_len);

	decoder->buf = buf;
	decoder->len = ret;

	return ret;
}

struct intel_pt_pkt_info {
	struct intel_pt_decoder	  *decoder;
	struct intel_pt_pkt       packet;
	uint64_t                  pos;
	int                       pkt_len;
	int                       last_packet_type;
	void                      *data;
};

typedef int (*intel_pt_pkt_cb_t)(struct intel_pt_pkt_info *pkt_info);

/* Lookahead packets in current buffer */
static int intel_pt_pkt_lookahead(struct intel_pt_decoder *decoder,
				  intel_pt_pkt_cb_t cb, void *data)
{
	struct intel_pt_pkt_info pkt_info;
	const unsigned char *buf = decoder->buf;
	size_t len = decoder->len;
	int ret;

	pkt_info.decoder          = decoder;
	pkt_info.pos              = decoder->pos;
	pkt_info.pkt_len          = decoder->pkt_step;
	pkt_info.last_packet_type = decoder->last_packet_type;
	pkt_info.data             = data;

	while (1) {
		do {
			pkt_info.pos += pkt_info.pkt_len;
			buf          += pkt_info.pkt_len;
			len          -= pkt_info.pkt_len;

			if (!len)
				return INTEL_PT_NEED_MORE_BYTES;

			ret = intel_pt_get_packet(buf, len, &pkt_info.packet);
			if (!ret)
				return INTEL_PT_NEED_MORE_BYTES;
			if (ret < 0)
				return ret;

			pkt_info.pkt_len = ret;
		} while (pkt_info.packet.type == INTEL_PT_PAD);

		ret = cb(&pkt_info);
		if (ret)
			return 0;

		pkt_info.last_packet_type = pkt_info.packet.type;
	}
}

struct intel_pt_calc_cyc_to_tsc_info {
	uint64_t        cycle_cnt;
	unsigned int    cbr;
	uint32_t        last_mtc;
	uint64_t        ctc_timestamp;
	uint64_t        ctc_delta;
	uint64_t        tsc_timestamp;
	uint64_t        timestamp;
	bool            have_tma;
	bool            fixup_last_mtc;
	bool            from_mtc;
	double          cbr_cyc_to_tsc;
};

/*
 * MTC provides a 8-bit slice of CTC but the TMA packet only provides the lower
 * 16 bits of CTC. If mtc_shift > 8 then some of the MTC bits are not in the CTC
 * provided by the TMA packet. Fix-up the last_mtc calculated from the TMA
 * packet by copying the missing bits from the current MTC assuming the least
 * difference between the two, and that the current MTC comes after last_mtc.
 */
static void intel_pt_fixup_last_mtc(uint32_t mtc, int mtc_shift,
				    uint32_t *last_mtc)
{
	uint32_t first_missing_bit = 1U << (16 - mtc_shift);
	uint32_t mask = ~(first_missing_bit - 1);

	*last_mtc |= mtc & mask;
	if (*last_mtc >= mtc) {
		*last_mtc -= first_missing_bit;
		*last_mtc &= 0xff;
	}
}

static int intel_pt_calc_cyc_cb(struct intel_pt_pkt_info *pkt_info)
{
	struct intel_pt_decoder *decoder = pkt_info->decoder;
	struct intel_pt_calc_cyc_to_tsc_info *data = pkt_info->data;
	uint64_t timestamp;
	double cyc_to_tsc;
	unsigned int cbr;
	uint32_t mtc, mtc_delta, ctc, fc, ctc_rem;

	switch (pkt_info->packet.type) {
	case INTEL_PT_TNT:
	case INTEL_PT_TIP_PGE:
	case INTEL_PT_TIP:
	case INTEL_PT_FUP:
	case INTEL_PT_PSB:
	case INTEL_PT_PIP:
	case INTEL_PT_MODE_EXEC:
	case INTEL_PT_MODE_TSX:
	case INTEL_PT_PSBEND:
	case INTEL_PT_PAD:
	case INTEL_PT_VMCS:
	case INTEL_PT_MNT:
	case INTEL_PT_PTWRITE:
	case INTEL_PT_PTWRITE_IP:
		return 0;

	case INTEL_PT_MTC:
		if (!data->have_tma)
			return 0;

		mtc = pkt_info->packet.payload;
		if (decoder->mtc_shift > 8 && data->fixup_last_mtc) {
			data->fixup_last_mtc = false;
			intel_pt_fixup_last_mtc(mtc, decoder->mtc_shift,
						&data->last_mtc);
		}
		if (mtc > data->last_mtc)
			mtc_delta = mtc - data->last_mtc;
		else
			mtc_delta = mtc + 256 - data->last_mtc;
		data->ctc_delta += mtc_delta << decoder->mtc_shift;
		data->last_mtc = mtc;

		if (decoder->tsc_ctc_mult) {
			timestamp = data->ctc_timestamp +
				data->ctc_delta * decoder->tsc_ctc_mult;
		} else {
			timestamp = data->ctc_timestamp +
				multdiv(data->ctc_delta,
					decoder->tsc_ctc_ratio_n,
					decoder->tsc_ctc_ratio_d);
		}

		if (timestamp < data->timestamp)
			return 1;

		if (pkt_info->last_packet_type != INTEL_PT_CYC) {
			data->timestamp = timestamp;
			return 0;
		}

		break;

	case INTEL_PT_TSC:
		/*
		 * For now, do not support using TSC packets - refer
		 * intel_pt_calc_cyc_to_tsc().
		 */
		if (data->from_mtc)
			return 1;
		timestamp = pkt_info->packet.payload |
			    (data->timestamp & (0xffULL << 56));
		if (data->from_mtc && timestamp < data->timestamp &&
		    data->timestamp - timestamp < decoder->tsc_slip)
			return 1;
		if (timestamp < data->timestamp)
			timestamp += (1ULL << 56);
		if (pkt_info->last_packet_type != INTEL_PT_CYC) {
			if (data->from_mtc)
				return 1;
			data->tsc_timestamp = timestamp;
			data->timestamp = timestamp;
			return 0;
		}
		break;

	case INTEL_PT_TMA:
		if (data->from_mtc)
			return 1;

		if (!decoder->tsc_ctc_ratio_d)
			return 0;

		ctc = pkt_info->packet.payload;
		fc = pkt_info->packet.count;
		ctc_rem = ctc & decoder->ctc_rem_mask;

		data->last_mtc = (ctc >> decoder->mtc_shift) & 0xff;

		data->ctc_timestamp = data->tsc_timestamp - fc;
		if (decoder->tsc_ctc_mult) {
			data->ctc_timestamp -= ctc_rem * decoder->tsc_ctc_mult;
		} else {
			data->ctc_timestamp -=
				multdiv(ctc_rem, decoder->tsc_ctc_ratio_n,
					decoder->tsc_ctc_ratio_d);
		}

		data->ctc_delta = 0;
		data->have_tma = true;
		data->fixup_last_mtc = true;

		return 0;

	case INTEL_PT_CYC:
		data->cycle_cnt += pkt_info->packet.payload;
		return 0;

	case INTEL_PT_CBR:
		cbr = pkt_info->packet.payload;
		if (data->cbr && data->cbr != cbr)
			return 1;
		data->cbr = cbr;
		data->cbr_cyc_to_tsc = decoder->max_non_turbo_ratio_fp / cbr;
		return 0;

	case INTEL_PT_TIP_PGD:
	case INTEL_PT_TRACESTOP:
	case INTEL_PT_EXSTOP:
	case INTEL_PT_EXSTOP_IP:
	case INTEL_PT_MWAIT:
	case INTEL_PT_PWRE:
	case INTEL_PT_PWRX:
	case INTEL_PT_OVF:
	case INTEL_PT_BAD: /* Does not happen */
	default:
		return 1;
	}

	if (!data->cbr && decoder->cbr) {
		data->cbr = decoder->cbr;
		data->cbr_cyc_to_tsc = decoder->cbr_cyc_to_tsc;
	}

	if (!data->cycle_cnt)
		return 1;

	cyc_to_tsc = (double)(timestamp - decoder->timestamp) / data->cycle_cnt;

	if (data->cbr && cyc_to_tsc > data->cbr_cyc_to_tsc &&
	    cyc_to_tsc / data->cbr_cyc_to_tsc > 1.25) {
		intel_pt_log("Timestamp: calculated %g TSC ticks per cycle too big (c.f. CBR-based value %g), pos " x64_fmt "\n",
			     cyc_to_tsc, data->cbr_cyc_to_tsc, pkt_info->pos);
		return 1;
	}

	decoder->calc_cyc_to_tsc = cyc_to_tsc;
	decoder->have_calc_cyc_to_tsc = true;

	if (data->cbr) {
		intel_pt_log("Timestamp: calculated %g TSC ticks per cycle c.f. CBR-based value %g, pos " x64_fmt "\n",
			     cyc_to_tsc, data->cbr_cyc_to_tsc, pkt_info->pos);
	} else {
		intel_pt_log("Timestamp: calculated %g TSC ticks per cycle c.f. unknown CBR-based value, pos " x64_fmt "\n",
			     cyc_to_tsc, pkt_info->pos);
	}

	return 1;
}

static void intel_pt_calc_cyc_to_tsc(struct intel_pt_decoder *decoder,
				     bool from_mtc)
{
	struct intel_pt_calc_cyc_to_tsc_info data = {
		.cycle_cnt      = 0,
		.cbr            = 0,
		.last_mtc       = decoder->last_mtc,
		.ctc_timestamp  = decoder->ctc_timestamp,
		.ctc_delta      = decoder->ctc_delta,
		.tsc_timestamp  = decoder->tsc_timestamp,
		.timestamp      = decoder->timestamp,
		.have_tma       = decoder->have_tma,
		.fixup_last_mtc = decoder->fixup_last_mtc,
		.from_mtc       = from_mtc,
		.cbr_cyc_to_tsc = 0,
	};

	/*
	 * For now, do not support using TSC packets for at least the reasons:
	 * 1) timing might have stopped
	 * 2) TSC packets within PSB+ can slip against CYC packets
	 */
	if (!from_mtc)
		return;

	intel_pt_pkt_lookahead(decoder, intel_pt_calc_cyc_cb, &data);
}

static int intel_pt_get_next_packet(struct intel_pt_decoder *decoder)
{
	int ret;

	decoder->last_packet_type = decoder->packet.type;

	do {
		decoder->pos += decoder->pkt_step;
		decoder->buf += decoder->pkt_step;
		decoder->len -= decoder->pkt_step;

		if (!decoder->len) {
			ret = intel_pt_get_next_data(decoder, false);
			if (ret)
				return ret;
		}

		ret = intel_pt_get_packet(decoder->buf, decoder->len,
					  &decoder->packet);
		if (ret == INTEL_PT_NEED_MORE_BYTES && BITS_PER_LONG == 32 &&
		    decoder->len < INTEL_PT_PKT_MAX_SZ && !decoder->next_buf) {
			ret = intel_pt_get_split_packet(decoder);
			if (ret < 0)
				return ret;
		}
		if (ret <= 0)
			return intel_pt_bad_packet(decoder);

		decoder->pkt_len = ret;
		decoder->pkt_step = ret;
		intel_pt_decoder_log_packet(decoder);
	} while (decoder->packet.type == INTEL_PT_PAD);

	return 0;
}

static uint64_t intel_pt_next_period(struct intel_pt_decoder *decoder)
{
	uint64_t timestamp, masked_timestamp;

	timestamp = decoder->timestamp + decoder->timestamp_insn_cnt;
	masked_timestamp = timestamp & decoder->period_mask;
	if (decoder->continuous_period) {
		if (masked_timestamp > decoder->last_masked_timestamp)
			return 1;
	} else {
		timestamp += 1;
		masked_timestamp = timestamp & decoder->period_mask;
		if (masked_timestamp > decoder->last_masked_timestamp) {
			decoder->last_masked_timestamp = masked_timestamp;
			decoder->continuous_period = true;
		}
	}

	if (masked_timestamp < decoder->last_masked_timestamp)
		return decoder->period_ticks;

	return decoder->period_ticks - (timestamp - masked_timestamp);
}

static uint64_t intel_pt_next_sample(struct intel_pt_decoder *decoder)
{
	switch (decoder->period_type) {
	case INTEL_PT_PERIOD_INSTRUCTIONS:
		return decoder->period - decoder->period_insn_cnt;
	case INTEL_PT_PERIOD_TICKS:
		return intel_pt_next_period(decoder);
	case INTEL_PT_PERIOD_NONE:
	case INTEL_PT_PERIOD_MTC:
	default:
		return 0;
	}
}

static void intel_pt_sample_insn(struct intel_pt_decoder *decoder)
{
	uint64_t timestamp, masked_timestamp;

	switch (decoder->period_type) {
	case INTEL_PT_PERIOD_INSTRUCTIONS:
		decoder->period_insn_cnt = 0;
		break;
	case INTEL_PT_PERIOD_TICKS:
		timestamp = decoder->timestamp + decoder->timestamp_insn_cnt;
		masked_timestamp = timestamp & decoder->period_mask;
		if (masked_timestamp > decoder->last_masked_timestamp)
			decoder->last_masked_timestamp = masked_timestamp;
		else
			decoder->last_masked_timestamp += decoder->period_ticks;
		break;
	case INTEL_PT_PERIOD_NONE:
	case INTEL_PT_PERIOD_MTC:
	default:
		break;
	}

	decoder->state.type |= INTEL_PT_INSTRUCTION;
}

static int intel_pt_walk_insn(struct intel_pt_decoder *decoder,
			      struct intel_pt_insn *intel_pt_insn, uint64_t ip)
{
	uint64_t max_insn_cnt, insn_cnt = 0;
	int err;

	if (!decoder->mtc_insn)
		decoder->mtc_insn = true;

	max_insn_cnt = intel_pt_next_sample(decoder);

	err = decoder->walk_insn(intel_pt_insn, &insn_cnt, &decoder->ip, ip,
				 max_insn_cnt, decoder->data);

	decoder->tot_insn_cnt += insn_cnt;
	decoder->timestamp_insn_cnt += insn_cnt;
	decoder->sample_insn_cnt += insn_cnt;
	decoder->period_insn_cnt += insn_cnt;

	if (err) {
		decoder->no_progress = 0;
		decoder->pkt_state = INTEL_PT_STATE_ERR2;
		intel_pt_log_at("ERROR: Failed to get instruction",
				decoder->ip);
		if (err == -ENOENT)
			return -ENOLINK;
		return -EILSEQ;
	}

	if (ip && decoder->ip == ip) {
		err = -EAGAIN;
		goto out;
	}

	if (max_insn_cnt && insn_cnt >= max_insn_cnt)
		intel_pt_sample_insn(decoder);

	if (intel_pt_insn->branch == INTEL_PT_BR_NO_BRANCH) {
		decoder->state.type = INTEL_PT_INSTRUCTION;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		decoder->ip += intel_pt_insn->length;
		err = INTEL_PT_RETURN;
		goto out;
	}

	if (intel_pt_insn->op == INTEL_PT_OP_CALL) {
		/* Zero-length calls are excluded */
		if (intel_pt_insn->branch != INTEL_PT_BR_UNCONDITIONAL ||
		    intel_pt_insn->rel) {
			err = intel_pt_push(&decoder->stack, decoder->ip +
					    intel_pt_insn->length);
			if (err)
				goto out;
		}
	} else if (intel_pt_insn->op == INTEL_PT_OP_RET) {
		decoder->ret_addr = intel_pt_pop(&decoder->stack);
	}

	if (intel_pt_insn->branch == INTEL_PT_BR_UNCONDITIONAL) {
		int cnt = decoder->no_progress++;

		decoder->state.from_ip = decoder->ip;
		decoder->ip += intel_pt_insn->length +
				intel_pt_insn->rel;
		decoder->state.to_ip = decoder->ip;
		err = INTEL_PT_RETURN;

		/*
		 * Check for being stuck in a loop.  This can happen if a
		 * decoder error results in the decoder erroneously setting the
		 * ip to an address that is itself in an infinite loop that
		 * consumes no packets.  When that happens, there must be an
		 * unconditional branch.
		 */
		if (cnt) {
			if (cnt == 1) {
				decoder->stuck_ip = decoder->state.to_ip;
				decoder->stuck_ip_prd = 1;
				decoder->stuck_ip_cnt = 1;
			} else if (cnt > INTEL_PT_MAX_LOOPS ||
				   decoder->state.to_ip == decoder->stuck_ip) {
				intel_pt_log_at("ERROR: Never-ending loop",
						decoder->state.to_ip);
				decoder->pkt_state = INTEL_PT_STATE_ERR_RESYNC;
				err = -ELOOP;
				goto out;
			} else if (!--decoder->stuck_ip_cnt) {
				decoder->stuck_ip_prd += 1;
				decoder->stuck_ip_cnt = decoder->stuck_ip_prd;
				decoder->stuck_ip = decoder->state.to_ip;
			}
		}
		goto out_no_progress;
	}
out:
	decoder->no_progress = 0;
out_no_progress:
	decoder->state.insn_op = intel_pt_insn->op;
	decoder->state.insn_len = intel_pt_insn->length;
	memcpy(decoder->state.insn, intel_pt_insn->buf,
	       INTEL_PT_INSN_BUF_SZ);

	if (decoder->tx_flags & INTEL_PT_IN_TX)
		decoder->state.flags |= INTEL_PT_IN_TX;

	return err;
}

static bool intel_pt_fup_event(struct intel_pt_decoder *decoder)
{
	bool ret = false;

	if (decoder->set_fup_tx_flags) {
		decoder->set_fup_tx_flags = false;
		decoder->tx_flags = decoder->fup_tx_flags;
		decoder->state.type = INTEL_PT_TRANSACTION;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		decoder->state.flags = decoder->fup_tx_flags;
		return true;
	}
	if (decoder->set_fup_ptw) {
		decoder->set_fup_ptw = false;
		decoder->state.type = INTEL_PT_PTW;
		decoder->state.flags |= INTEL_PT_FUP_IP;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		decoder->state.ptw_payload = decoder->fup_ptw_payload;
		return true;
	}
	if (decoder->set_fup_mwait) {
		decoder->set_fup_mwait = false;
		decoder->state.type = INTEL_PT_MWAIT_OP;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		decoder->state.mwait_payload = decoder->fup_mwait_payload;
		ret = true;
	}
	if (decoder->set_fup_pwre) {
		decoder->set_fup_pwre = false;
		decoder->state.type |= INTEL_PT_PWR_ENTRY;
		decoder->state.type &= ~INTEL_PT_BRANCH;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		decoder->state.pwre_payload = decoder->fup_pwre_payload;
		ret = true;
	}
	if (decoder->set_fup_exstop) {
		decoder->set_fup_exstop = false;
		decoder->state.type |= INTEL_PT_EX_STOP;
		decoder->state.type &= ~INTEL_PT_BRANCH;
		decoder->state.flags |= INTEL_PT_FUP_IP;
		decoder->state.from_ip = decoder->ip;
		decoder->state.to_ip = 0;
		ret = true;
	}
	return ret;
}

static inline bool intel_pt_fup_with_nlip(struct intel_pt_decoder *decoder,
					  struct intel_pt_insn *intel_pt_insn,
					  uint64_t ip, int err)
{
	return decoder->flags & INTEL_PT_FUP_WITH_NLIP && !err &&
	       intel_pt_insn->branch == INTEL_PT_BR_INDIRECT &&
	       ip == decoder->ip + intel_pt_insn->length;
}

static int intel_pt_walk_fup(struct intel_pt_decoder *decoder)
{
	struct intel_pt_insn intel_pt_insn;
	uint64_t ip;
	int err;

	ip = decoder->last_ip;

	while (1) {
		err = intel_pt_walk_insn(decoder, &intel_pt_insn, ip);
		if (err == INTEL_PT_RETURN)
			return 0;
		if (err == -EAGAIN ||
		    intel_pt_fup_with_nlip(decoder, &intel_pt_insn, ip, err)) {
			if (intel_pt_fup_event(decoder))
				return 0;
			return -EAGAIN;
		}
		decoder->set_fup_tx_flags = false;
		if (err)
			return err;

		if (intel_pt_insn.branch == INTEL_PT_BR_INDIRECT) {
			intel_pt_log_at("ERROR: Unexpected indirect branch",
					decoder->ip);
			decoder->pkt_state = INTEL_PT_STATE_ERR_RESYNC;
			return -ENOENT;
		}

		if (intel_pt_insn.branch == INTEL_PT_BR_CONDITIONAL) {
			intel_pt_log_at("ERROR: Unexpected conditional branch",
					decoder->ip);
			decoder->pkt_state = INTEL_PT_STATE_ERR_RESYNC;
			return -ENOENT;
		}

		intel_pt_bug(decoder);
	}
}

static int intel_pt_walk_tip(struct intel_pt_decoder *decoder)
{
	struct intel_pt_insn intel_pt_insn;
	int err;

	err = intel_pt_walk_insn(decoder, &intel_pt_insn, 0);
	if (err == INTEL_PT_RETURN &&
	    decoder->pgd_ip &&
	    decoder->pkt_state == INTEL_PT_STATE_TIP_PGD &&
	    (decoder->state.type & INTEL_PT_BRANCH) &&
	    decoder->pgd_ip(decoder->state.to_ip, decoder->data)) {
		/* Unconditional branch leaving filter region */
		decoder->no_progress = 0;
		decoder->pge = false;
		decoder->continuous_period = false;
		decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
		decoder->state.type |= INTEL_PT_TRACE_END;
		return 0;
	}
	if (err == INTEL_PT_RETURN)
		return 0;
	if (err)
		return err;

	if (intel_pt_insn.branch == INTEL_PT_BR_INDIRECT) {
		if (decoder->pkt_state == INTEL_PT_STATE_TIP_PGD) {
			decoder->pge = false;
			decoder->continuous_period = false;
			decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			decoder->state.from_ip = decoder->ip;
			if (decoder->packet.count == 0) {
				decoder->state.to_ip = 0;
			} else {
				decoder->state.to_ip = decoder->last_ip;
				decoder->ip = decoder->last_ip;
			}
			decoder->state.type |= INTEL_PT_TRACE_END;
		} else {
			decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			decoder->state.from_ip = decoder->ip;
			if (decoder->packet.count == 0) {
				decoder->state.to_ip = 0;
			} else {
				decoder->state.to_ip = decoder->last_ip;
				decoder->ip = decoder->last_ip;
			}
		}
		return 0;
	}

	if (intel_pt_insn.branch == INTEL_PT_BR_CONDITIONAL) {
		uint64_t to_ip = decoder->ip + intel_pt_insn.length +
				 intel_pt_insn.rel;

		if (decoder->pgd_ip &&
		    decoder->pkt_state == INTEL_PT_STATE_TIP_PGD &&
		    decoder->pgd_ip(to_ip, decoder->data)) {
			/* Conditional branch leaving filter region */
			decoder->pge = false;
			decoder->continuous_period = false;
			decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			decoder->ip = to_ip;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = to_ip;
			decoder->state.type |= INTEL_PT_TRACE_END;
			return 0;
		}
		intel_pt_log_at("ERROR: Conditional branch when expecting indirect branch",
				decoder->ip);
		decoder->pkt_state = INTEL_PT_STATE_ERR_RESYNC;
		return -ENOENT;
	}

	return intel_pt_bug(decoder);
}

static int intel_pt_walk_tnt(struct intel_pt_decoder *decoder)
{
	struct intel_pt_insn intel_pt_insn;
	int err;

	while (1) {
		err = intel_pt_walk_insn(decoder, &intel_pt_insn, 0);
		if (err == INTEL_PT_RETURN)
			return 0;
		if (err)
			return err;

		if (intel_pt_insn.op == INTEL_PT_OP_RET) {
			if (!decoder->return_compression) {
				intel_pt_log_at("ERROR: RET when expecting conditional branch",
						decoder->ip);
				decoder->pkt_state = INTEL_PT_STATE_ERR3;
				return -ENOENT;
			}
			if (!decoder->ret_addr) {
				intel_pt_log_at("ERROR: Bad RET compression (stack empty)",
						decoder->ip);
				decoder->pkt_state = INTEL_PT_STATE_ERR3;
				return -ENOENT;
			}
			if (!(decoder->tnt.payload & BIT63)) {
				intel_pt_log_at("ERROR: Bad RET compression (TNT=N)",
						decoder->ip);
				decoder->pkt_state = INTEL_PT_STATE_ERR3;
				return -ENOENT;
			}
			decoder->tnt.count -= 1;
			if (decoder->tnt.count)
				decoder->pkt_state = INTEL_PT_STATE_TNT_CONT;
			else
				decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			decoder->tnt.payload <<= 1;
			decoder->state.from_ip = decoder->ip;
			decoder->ip = decoder->ret_addr;
			decoder->state.to_ip = decoder->ip;
			return 0;
		}

		if (intel_pt_insn.branch == INTEL_PT_BR_INDIRECT) {
			/* Handle deferred TIPs */
			err = intel_pt_get_next_packet(decoder);
			if (err)
				return err;
			if (decoder->packet.type != INTEL_PT_TIP ||
			    decoder->packet.count == 0) {
				intel_pt_log_at("ERROR: Missing deferred TIP for indirect branch",
						decoder->ip);
				decoder->pkt_state = INTEL_PT_STATE_ERR3;
				decoder->pkt_step = 0;
				return -ENOENT;
			}
			intel_pt_set_last_ip(decoder);
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = decoder->last_ip;
			decoder->ip = decoder->last_ip;
			return 0;
		}

		if (intel_pt_insn.branch == INTEL_PT_BR_CONDITIONAL) {
			decoder->tnt.count -= 1;
			if (decoder->tnt.count)
				decoder->pkt_state = INTEL_PT_STATE_TNT_CONT;
			else
				decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			if (decoder->tnt.payload & BIT63) {
				decoder->tnt.payload <<= 1;
				decoder->state.from_ip = decoder->ip;
				decoder->ip += intel_pt_insn.length +
					       intel_pt_insn.rel;
				decoder->state.to_ip = decoder->ip;
				return 0;
			}
			/* Instruction sample for a non-taken branch */
			if (decoder->state.type & INTEL_PT_INSTRUCTION) {
				decoder->tnt.payload <<= 1;
				decoder->state.type = INTEL_PT_INSTRUCTION;
				decoder->state.from_ip = decoder->ip;
				decoder->state.to_ip = 0;
				decoder->ip += intel_pt_insn.length;
				return 0;
			}
			decoder->sample_cyc = false;
			decoder->ip += intel_pt_insn.length;
			if (!decoder->tnt.count) {
				intel_pt_update_sample_time(decoder);
				return -EAGAIN;
			}
			decoder->tnt.payload <<= 1;
			continue;
		}

		return intel_pt_bug(decoder);
	}
}

static int intel_pt_mode_tsx(struct intel_pt_decoder *decoder, bool *no_tip)
{
	unsigned int fup_tx_flags;
	int err;

	fup_tx_flags = decoder->packet.payload &
		       (INTEL_PT_IN_TX | INTEL_PT_ABORT_TX);
	err = intel_pt_get_next_packet(decoder);
	if (err)
		return err;
	if (decoder->packet.type == INTEL_PT_FUP) {
		decoder->fup_tx_flags = fup_tx_flags;
		decoder->set_fup_tx_flags = true;
		if (!(decoder->fup_tx_flags & INTEL_PT_ABORT_TX))
			*no_tip = true;
	} else {
		intel_pt_log_at("ERROR: Missing FUP after MODE.TSX",
				decoder->pos);
		intel_pt_update_in_tx(decoder);
	}
	return 0;
}

static uint64_t intel_pt_8b_tsc(uint64_t timestamp, uint64_t ref_timestamp)
{
	timestamp |= (ref_timestamp & (0xffULL << 56));

	if (timestamp < ref_timestamp) {
		if (ref_timestamp - timestamp > (1ULL << 55))
			timestamp += (1ULL << 56);
	} else {
		if (timestamp - ref_timestamp > (1ULL << 55))
			timestamp -= (1ULL << 56);
	}

	return timestamp;
}

static void intel_pt_calc_tsc_timestamp(struct intel_pt_decoder *decoder)
{
	uint64_t timestamp;

	decoder->have_tma = false;

	if (decoder->ref_timestamp) {
		timestamp = intel_pt_8b_tsc(decoder->packet.payload,
					    decoder->ref_timestamp);
		decoder->tsc_timestamp = timestamp;
		decoder->timestamp = timestamp;
		decoder->ref_timestamp = 0;
		decoder->timestamp_insn_cnt = 0;
	} else if (decoder->timestamp) {
		timestamp = decoder->packet.payload |
			    (decoder->timestamp & (0xffULL << 56));
		decoder->tsc_timestamp = timestamp;
		if (timestamp < decoder->timestamp &&
		    decoder->timestamp - timestamp < decoder->tsc_slip) {
			intel_pt_log_to("Suppressing backwards timestamp",
					timestamp);
			timestamp = decoder->timestamp;
		}
		if (timestamp < decoder->timestamp) {
			intel_pt_log_to("Wraparound timestamp", timestamp);
			timestamp += (1ULL << 56);
			decoder->tsc_timestamp = timestamp;
		}
		decoder->timestamp = timestamp;
		decoder->timestamp_insn_cnt = 0;
	}

	if (decoder->last_packet_type == INTEL_PT_CYC) {
		decoder->cyc_ref_timestamp = decoder->timestamp;
		decoder->cycle_cnt = 0;
		decoder->have_calc_cyc_to_tsc = false;
		intel_pt_calc_cyc_to_tsc(decoder, false);
	}

	intel_pt_log_to("Setting timestamp", decoder->timestamp);
}

static int intel_pt_overflow(struct intel_pt_decoder *decoder)
{
	intel_pt_log("ERROR: Buffer overflow\n");
	intel_pt_clear_tx_flags(decoder);
	decoder->timestamp_insn_cnt = 0;
	decoder->pkt_state = INTEL_PT_STATE_ERR_RESYNC;
	decoder->overflow = true;
	return -EOVERFLOW;
}

static inline void intel_pt_mtc_cyc_cnt_pge(struct intel_pt_decoder *decoder)
{
	if (decoder->have_cyc)
		return;

	decoder->cyc_cnt_timestamp = decoder->timestamp;
	decoder->base_cyc_cnt = decoder->tot_cyc_cnt;
}

static inline void intel_pt_mtc_cyc_cnt_cbr(struct intel_pt_decoder *decoder)
{
	decoder->tsc_to_cyc = decoder->cbr / decoder->max_non_turbo_ratio_fp;

	if (decoder->pge)
		intel_pt_mtc_cyc_cnt_pge(decoder);
}

static inline void intel_pt_mtc_cyc_cnt_upd(struct intel_pt_decoder *decoder)
{
	uint64_t tot_cyc_cnt, tsc_delta;

	if (decoder->have_cyc)
		return;

	decoder->sample_cyc = true;

	if (!decoder->pge || decoder->timestamp <= decoder->cyc_cnt_timestamp)
		return;

	tsc_delta = decoder->timestamp - decoder->cyc_cnt_timestamp;
	tot_cyc_cnt = tsc_delta * decoder->tsc_to_cyc + decoder->base_cyc_cnt;

	if (tot_cyc_cnt > decoder->tot_cyc_cnt)
		decoder->tot_cyc_cnt = tot_cyc_cnt;
}

static void intel_pt_calc_tma(struct intel_pt_decoder *decoder)
{
	uint32_t ctc = decoder->packet.payload;
	uint32_t fc = decoder->packet.count;
	uint32_t ctc_rem = ctc & decoder->ctc_rem_mask;

	if (!decoder->tsc_ctc_ratio_d)
		return;

	if (decoder->pge && !decoder->in_psb)
		intel_pt_mtc_cyc_cnt_pge(decoder);
	else
		intel_pt_mtc_cyc_cnt_upd(decoder);

	decoder->last_mtc = (ctc >> decoder->mtc_shift) & 0xff;
	decoder->ctc_timestamp = decoder->tsc_timestamp - fc;
	if (decoder->tsc_ctc_mult) {
		decoder->ctc_timestamp -= ctc_rem * decoder->tsc_ctc_mult;
	} else {
		decoder->ctc_timestamp -= multdiv(ctc_rem,
						  decoder->tsc_ctc_ratio_n,
						  decoder->tsc_ctc_ratio_d);
	}
	decoder->ctc_delta = 0;
	decoder->have_tma = true;
	decoder->fixup_last_mtc = true;
	intel_pt_log("CTC timestamp " x64_fmt " last MTC %#x  CTC rem %#x\n",
		     decoder->ctc_timestamp, decoder->last_mtc, ctc_rem);
}

static void intel_pt_calc_mtc_timestamp(struct intel_pt_decoder *decoder)
{
	uint64_t timestamp;
	uint32_t mtc, mtc_delta;

	if (!decoder->have_tma)
		return;

	mtc = decoder->packet.payload;

	if (decoder->mtc_shift > 8 && decoder->fixup_last_mtc) {
		decoder->fixup_last_mtc = false;
		intel_pt_fixup_last_mtc(mtc, decoder->mtc_shift,
					&decoder->last_mtc);
	}

	if (mtc > decoder->last_mtc)
		mtc_delta = mtc - decoder->last_mtc;
	else
		mtc_delta = mtc + 256 - decoder->last_mtc;

	decoder->ctc_delta += mtc_delta << decoder->mtc_shift;

	if (decoder->tsc_ctc_mult) {
		timestamp = decoder->ctc_timestamp +
			    decoder->ctc_delta * decoder->tsc_ctc_mult;
	} else {
		timestamp = decoder->ctc_timestamp +
			    multdiv(decoder->ctc_delta,
				    decoder->tsc_ctc_ratio_n,
				    decoder->tsc_ctc_ratio_d);
	}

	if (timestamp < decoder->timestamp)
		intel_pt_log("Suppressing MTC timestamp " x64_fmt " less than current timestamp " x64_fmt "\n",
			     timestamp, decoder->timestamp);
	else
		decoder->timestamp = timestamp;

	intel_pt_mtc_cyc_cnt_upd(decoder);

	decoder->timestamp_insn_cnt = 0;
	decoder->last_mtc = mtc;

	if (decoder->last_packet_type == INTEL_PT_CYC) {
		decoder->cyc_ref_timestamp = decoder->timestamp;
		decoder->cycle_cnt = 0;
		decoder->have_calc_cyc_to_tsc = false;
		intel_pt_calc_cyc_to_tsc(decoder, true);
	}

	intel_pt_log_to("Setting timestamp", decoder->timestamp);
}

static void intel_pt_calc_cbr(struct intel_pt_decoder *decoder)
{
	unsigned int cbr = decoder->packet.payload & 0xff;

	decoder->cbr_payload = decoder->packet.payload;

	if (decoder->cbr == cbr)
		return;

	decoder->cbr = cbr;
	decoder->cbr_cyc_to_tsc = decoder->max_non_turbo_ratio_fp / cbr;

	intel_pt_mtc_cyc_cnt_cbr(decoder);
}

static void intel_pt_calc_cyc_timestamp(struct intel_pt_decoder *decoder)
{
	uint64_t timestamp = decoder->cyc_ref_timestamp;

	decoder->have_cyc = true;

	decoder->cycle_cnt += decoder->packet.payload;
	if (decoder->pge)
		decoder->tot_cyc_cnt += decoder->packet.payload;
	decoder->sample_cyc = true;

	if (!decoder->cyc_ref_timestamp)
		return;

	if (decoder->have_calc_cyc_to_tsc)
		timestamp += decoder->cycle_cnt * decoder->calc_cyc_to_tsc;
	else if (decoder->cbr)
		timestamp += decoder->cycle_cnt * decoder->cbr_cyc_to_tsc;
	else
		return;

	if (timestamp < decoder->timestamp)
		intel_pt_log("Suppressing CYC timestamp " x64_fmt " less than current timestamp " x64_fmt "\n",
			     timestamp, decoder->timestamp);
	else
		decoder->timestamp = timestamp;

	decoder->timestamp_insn_cnt = 0;

	intel_pt_log_to("Setting timestamp", decoder->timestamp);
}

/* Walk PSB+ packets when already in sync. */
static int intel_pt_walk_psbend(struct intel_pt_decoder *decoder)
{
	int err;

	decoder->in_psb = true;

	while (1) {
		err = intel_pt_get_next_packet(decoder);
		if (err)
			goto out;

		switch (decoder->packet.type) {
		case INTEL_PT_PSBEND:
			err = 0;
			goto out;

		case INTEL_PT_TIP_PGD:
		case INTEL_PT_TIP_PGE:
		case INTEL_PT_TIP:
		case INTEL_PT_TNT:
		case INTEL_PT_TRACESTOP:
		case INTEL_PT_BAD:
		case INTEL_PT_PSB:
		case INTEL_PT_PTWRITE:
		case INTEL_PT_PTWRITE_IP:
		case INTEL_PT_EXSTOP:
		case INTEL_PT_EXSTOP_IP:
		case INTEL_PT_MWAIT:
		case INTEL_PT_PWRE:
		case INTEL_PT_PWRX:
			decoder->have_tma = false;
			intel_pt_log("ERROR: Unexpected packet\n");
			err = -EAGAIN;
			goto out;

		case INTEL_PT_OVF:
			err = intel_pt_overflow(decoder);
			goto out;

		case INTEL_PT_TSC:
			intel_pt_calc_tsc_timestamp(decoder);
			break;

		case INTEL_PT_TMA:
			intel_pt_calc_tma(decoder);
			break;

		case INTEL_PT_CBR:
			intel_pt_calc_cbr(decoder);
			break;

		case INTEL_PT_MODE_EXEC:
			decoder->exec_mode = decoder->packet.payload;
			break;

		case INTEL_PT_PIP:
			decoder->cr3 = decoder->packet.payload & (BIT63 - 1);
			break;

		case INTEL_PT_FUP:
			decoder->pge = true;
			if (decoder->packet.count)
				intel_pt_set_last_ip(decoder);
			break;

		case INTEL_PT_MODE_TSX:
			intel_pt_update_in_tx(decoder);
			break;

		case INTEL_PT_MTC:
			intel_pt_calc_mtc_timestamp(decoder);
			if (decoder->period_type == INTEL_PT_PERIOD_MTC)
				decoder->state.type |= INTEL_PT_INSTRUCTION;
			break;

		case INTEL_PT_CYC:
		case INTEL_PT_VMCS:
		case INTEL_PT_MNT:
		case INTEL_PT_PAD:
		default:
			break;
		}
	}
out:
	decoder->in_psb = false;

	return err;
}

static int intel_pt_walk_fup_tip(struct intel_pt_decoder *decoder)
{
	int err;

	if (decoder->tx_flags & INTEL_PT_ABORT_TX) {
		decoder->tx_flags = 0;
		decoder->state.flags &= ~INTEL_PT_IN_TX;
		decoder->state.flags |= INTEL_PT_ABORT_TX;
	} else {
		decoder->state.flags |= INTEL_PT_ASYNC;
	}

	while (1) {
		err = intel_pt_get_next_packet(decoder);
		if (err)
			return err;

		switch (decoder->packet.type) {
		case INTEL_PT_TNT:
		case INTEL_PT_FUP:
		case INTEL_PT_TRACESTOP:
		case INTEL_PT_PSB:
		case INTEL_PT_TSC:
		case INTEL_PT_TMA:
		case INTEL_PT_MODE_TSX:
		case INTEL_PT_BAD:
		case INTEL_PT_PSBEND:
		case INTEL_PT_PTWRITE:
		case INTEL_PT_PTWRITE_IP:
		case INTEL_PT_EXSTOP:
		case INTEL_PT_EXSTOP_IP:
		case INTEL_PT_MWAIT:
		case INTEL_PT_PWRE:
		case INTEL_PT_PWRX:
			intel_pt_log("ERROR: Missing TIP after FUP\n");
			decoder->pkt_state = INTEL_PT_STATE_ERR3;
			decoder->pkt_step = 0;
			return -ENOENT;

		case INTEL_PT_CBR:
			intel_pt_calc_cbr(decoder);
			break;

		case INTEL_PT_OVF:
			return intel_pt_overflow(decoder);

		case INTEL_PT_TIP_PGD:
			decoder->state.from_ip = decoder->ip;
			if (decoder->packet.count == 0) {
				decoder->state.to_ip = 0;
			} else {
				intel_pt_set_ip(decoder);
				decoder->state.to_ip = decoder->ip;
			}
			decoder->pge = false;
			decoder->continuous_period = false;
			decoder->state.type |= INTEL_PT_TRACE_END;
			return 0;

		case INTEL_PT_TIP_PGE:
			decoder->pge = true;
			intel_pt_log("Omitting PGE ip " x64_fmt "\n",
				     decoder->ip);
			decoder->state.from_ip = 0;
			if (decoder->packet.count == 0) {
				decoder->state.to_ip = 0;
			} else {
				intel_pt_set_ip(decoder);
				decoder->state.to_ip = decoder->ip;
			}
			decoder->state.type |= INTEL_PT_TRACE_BEGIN;
			intel_pt_mtc_cyc_cnt_pge(decoder);
			return 0;

		case INTEL_PT_TIP:
			decoder->state.from_ip = decoder->ip;
			if (decoder->packet.count == 0) {
				decoder->state.to_ip = 0;
			} else {
				intel_pt_set_ip(decoder);
				decoder->state.to_ip = decoder->ip;
			}
			return 0;

		case INTEL_PT_PIP:
			decoder->cr3 = decoder->packet.payload & (BIT63 - 1);
			break;

		case INTEL_PT_MTC:
			intel_pt_calc_mtc_timestamp(decoder);
			if (decoder->period_type == INTEL_PT_PERIOD_MTC)
				decoder->state.type |= INTEL_PT_INSTRUCTION;
			break;

		case INTEL_PT_CYC:
			intel_pt_calc_cyc_timestamp(decoder);
			break;

		case INTEL_PT_MODE_EXEC:
			decoder->exec_mode = decoder->packet.payload;
			break;

		case INTEL_PT_VMCS:
		case INTEL_PT_MNT:
		case INTEL_PT_PAD:
			break;

		default:
			return intel_pt_bug(decoder);
		}
	}
}

static int intel_pt_walk_trace(struct intel_pt_decoder *decoder)
{
	bool no_tip = false;
	int err;

	while (1) {
		err = intel_pt_get_next_packet(decoder);
		if (err)
			return err;
next:
		switch (decoder->packet.type) {
		case INTEL_PT_TNT:
			if (!decoder->packet.count)
				break;
			decoder->tnt = decoder->packet;
			decoder->pkt_state = INTEL_PT_STATE_TNT;
			err = intel_pt_walk_tnt(decoder);
			if (err == -EAGAIN)
				break;
			return err;

		case INTEL_PT_TIP_PGD:
			if (decoder->packet.count != 0)
				intel_pt_set_last_ip(decoder);
			decoder->pkt_state = INTEL_PT_STATE_TIP_PGD;
			return intel_pt_walk_tip(decoder);

		case INTEL_PT_TIP_PGE: {
			decoder->pge = true;
			intel_pt_mtc_cyc_cnt_pge(decoder);
			if (decoder->packet.count == 0) {
				intel_pt_log_at("Skipping zero TIP.PGE",
						decoder->pos);
				break;
			}
			intel_pt_set_ip(decoder);
			decoder->state.from_ip = 0;
			decoder->state.to_ip = decoder->ip;
			decoder->state.type |= INTEL_PT_TRACE_BEGIN;
			return 0;
		}

		case INTEL_PT_OVF:
			return intel_pt_overflow(decoder);

		case INTEL_PT_TIP:
			if (decoder->packet.count != 0)
				intel_pt_set_last_ip(decoder);
			decoder->pkt_state = INTEL_PT_STATE_TIP;
			return intel_pt_walk_tip(decoder);

		case INTEL_PT_FUP:
			if (decoder->packet.count == 0) {
				intel_pt_log_at("Skipping zero FUP",
						decoder->pos);
				no_tip = false;
				break;
			}
			intel_pt_set_last_ip(decoder);
			if (!decoder->branch_enable) {
				decoder->ip = decoder->last_ip;
				if (intel_pt_fup_event(decoder))
					return 0;
				no_tip = false;
				break;
			}
			if (decoder->set_fup_mwait)
				no_tip = true;
			err = intel_pt_walk_fup(decoder);
			if (err != -EAGAIN) {
				if (err)
					return err;
				if (no_tip)
					decoder->pkt_state =
						INTEL_PT_STATE_FUP_NO_TIP;
				else
					decoder->pkt_state = INTEL_PT_STATE_FUP;
				return 0;
			}
			if (no_tip) {
				no_tip = false;
				break;
			}
			return intel_pt_walk_fup_tip(decoder);

		case INTEL_PT_TRACESTOP:
			decoder->pge = false;
			decoder->continuous_period = false;
			intel_pt_clear_tx_flags(decoder);
			decoder->have_tma = false;
			break;

		case INTEL_PT_PSB:
			decoder->last_ip = 0;
			decoder->have_last_ip = true;
			intel_pt_clear_stack(&decoder->stack);
			err = intel_pt_walk_psbend(decoder);
			if (err == -EAGAIN)
				goto next;
			if (err)
				return err;
			break;

		case INTEL_PT_PIP:
			decoder->cr3 = decoder->packet.payload & (BIT63 - 1);
			break;

		case INTEL_PT_MTC:
			intel_pt_calc_mtc_timestamp(decoder);
			if (decoder->period_type != INTEL_PT_PERIOD_MTC)
				break;
			/*
			 * Ensure that there has been an instruction since the
			 * last MTC.
			 */
			if (!decoder->mtc_insn)
				break;
			decoder->mtc_insn = false;
			/* Ensure that there is a timestamp */
			if (!decoder->timestamp)
				break;
			decoder->state.type = INTEL_PT_INSTRUCTION;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = 0;
			decoder->mtc_insn = false;
			return 0;

		case INTEL_PT_TSC:
			intel_pt_calc_tsc_timestamp(decoder);
			break;

		case INTEL_PT_TMA:
			intel_pt_calc_tma(decoder);
			break;

		case INTEL_PT_CYC:
			intel_pt_calc_cyc_timestamp(decoder);
			break;

		case INTEL_PT_CBR:
			intel_pt_calc_cbr(decoder);
			if (!decoder->branch_enable &&
			    decoder->cbr != decoder->cbr_seen) {
				decoder->cbr_seen = decoder->cbr;
				decoder->state.type = INTEL_PT_CBR_CHG;
				decoder->state.from_ip = decoder->ip;
				decoder->state.to_ip = 0;
				decoder->state.cbr_payload =
							decoder->packet.payload;
				return 0;
			}
			break;

		case INTEL_PT_MODE_EXEC:
			decoder->exec_mode = decoder->packet.payload;
			break;

		case INTEL_PT_MODE_TSX:
			/* MODE_TSX need not be followed by FUP */
			if (!decoder->pge) {
				intel_pt_update_in_tx(decoder);
				break;
			}
			err = intel_pt_mode_tsx(decoder, &no_tip);
			if (err)
				return err;
			goto next;

		case INTEL_PT_BAD: /* Does not happen */
			return intel_pt_bug(decoder);

		case INTEL_PT_PSBEND:
		case INTEL_PT_VMCS:
		case INTEL_PT_MNT:
		case INTEL_PT_PAD:
			break;

		case INTEL_PT_PTWRITE_IP:
			decoder->fup_ptw_payload = decoder->packet.payload;
			err = intel_pt_get_next_packet(decoder);
			if (err)
				return err;
			if (decoder->packet.type == INTEL_PT_FUP) {
				decoder->set_fup_ptw = true;
				no_tip = true;
			} else {
				intel_pt_log_at("ERROR: Missing FUP after PTWRITE",
						decoder->pos);
			}
			goto next;

		case INTEL_PT_PTWRITE:
			decoder->state.type = INTEL_PT_PTW;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = 0;
			decoder->state.ptw_payload = decoder->packet.payload;
			return 0;

		case INTEL_PT_MWAIT:
			decoder->fup_mwait_payload = decoder->packet.payload;
			decoder->set_fup_mwait = true;
			break;

		case INTEL_PT_PWRE:
			if (decoder->set_fup_mwait) {
				decoder->fup_pwre_payload =
							decoder->packet.payload;
				decoder->set_fup_pwre = true;
				break;
			}
			decoder->state.type = INTEL_PT_PWR_ENTRY;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = 0;
			decoder->state.pwrx_payload = decoder->packet.payload;
			return 0;

		case INTEL_PT_EXSTOP_IP:
			err = intel_pt_get_next_packet(decoder);
			if (err)
				return err;
			if (decoder->packet.type == INTEL_PT_FUP) {
				decoder->set_fup_exstop = true;
				no_tip = true;
			} else {
				intel_pt_log_at("ERROR: Missing FUP after EXSTOP",
						decoder->pos);
			}
			goto next;

		case INTEL_PT_EXSTOP:
			decoder->state.type = INTEL_PT_EX_STOP;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = 0;
			return 0;

		case INTEL_PT_PWRX:
			decoder->state.type = INTEL_PT_PWR_EXIT;
			decoder->state.from_ip = decoder->ip;
			decoder->state.to_ip = 0;
			decoder->state.pwrx_payload = decoder->packet.payload;
			return 0;

		default:
			return intel_pt_bug(decoder);
		}
	}
}

static inline bool intel_pt_have_ip(struct intel_pt_decoder *decoder)
{
	return decoder->packet.count &&
	       (decoder->have_last_ip || decoder->packet.count == 3 ||
		decoder->packet.count == 6);
}

/* Walk PSB+ packets to get in sync. */
static int intel_pt_walk_psb(struct intel_pt_decoder *decoder)
{
	int err;

	decoder->in_psb = true;

	while (1) {
		err = intel_pt_get_next_packet(decoder);
		if (err)
			goto out;

		switch (decoder->packet.type) {
		case INTEL_PT_TIP_PGD:
			decoder->continuous_period = false;
			__fallthrough;
		case INTEL_PT_TIP_PGE:
		case INTEL_PT_TIP:
		case INTEL_PT_PTWRITE:
		case INTEL_PT_PTWRITE_IP:
		case INTEL_PT_EXSTOP:
		case INTEL_PT_EXSTOP_IP:
		case INTEL_PT_MWAIT:
		case INTEL_PT_PWRE:
		case INTEL_PT_PWRX:
			intel_pt_log("ERROR: Unexpected packet\n");
			err = -ENOENT;
			goto out;

		case INTEL_PT_FUP:
			decoder->pge = true;
			if (intel_pt_have_ip(decoder)) {
				uint64_t current_ip = decoder->ip;

				intel_pt_set_ip(decoder);
				if (current_ip)
					intel_pt_log_to("Setting IP",
							decoder->ip);
			}
			break;

		case INTEL_PT_MTC:
			intel_pt_calc_mtc_timestamp(decoder);
			break;

		case INTEL_PT_TSC:
			intel_pt_calc_tsc_timestamp(decoder);
			break;

		case INTEL_PT_TMA:
			intel_pt_calc_tma(decoder);
			break;

		case INTEL_PT_CYC:
			intel_pt_calc_cyc_timestamp(decoder);
			break;

		case INTEL_PT_CBR:
			intel_pt_calc_cbr(decoder);
			break;

		case INTEL_PT_PIP:
			decoder->cr3 = decoder->packet.payload & (BIT63 - 1);
			break;

		case INTEL_PT_MODE_EXEC:
			decoder->exec_mode = decoder->packet.payload;
			break;

		case INTEL_PT_MODE_TSX:
			intel_pt_update_in_tx(decoder);
			break;

		case INTEL_PT_TRACESTOP:
			decoder->pge = false;
			decoder->continuous_period = false;
			intel_pt_clear_tx_flags(decoder);
			__fallthrough;

		case INTEL_PT_TNT:
			decoder->have_tma = false;
			intel_pt_log("ERROR: Unexpected packet\n");
			if (decoder->ip)
				decoder->pkt_state = INTEL_PT_STATE_ERR4;
			else
				decoder->pkt_state = INTEL_PT_STATE_ERR3;
			err = -ENOENT;
			goto out;

		case INTEL_PT_BAD: /* Does not happen */
			err = intel_pt_bug(decoder);
			goto out;

		case INTEL_PT_OVF:
			err = intel_pt_overflow(decoder);
			goto out;

		case INTEL_PT_PSBEND:
			err = 0;
			goto out;

		case INTEL_PT_PSB:
		case INTEL_PT_VMCS:
		case INTEL_PT_MNT:
		case INTEL_PT_PAD:
		default:
			break;
		}
	}
out:
	decoder->in_psb = false;

	return err;
}

static int intel_pt_walk_to_ip(struct intel_pt_decoder *decoder)
{
	int err;

	while (1) {
		err = intel_pt_get_next_packet(decoder);
		if (err)
			return err;

		switch (decoder->packet.type) {
		case INTEL_PT_TIP_PGD:
			decoder->continuous_period = false;
			decoder->pge = false;
			if (intel_pt_have_ip(decoder))
				intel_pt_set_ip(decoder);
			if (!decoder->ip)
				break;
			decoder->state.type |= INTEL_PT_TRACE_END;
			return 0;

		case INTEL_PT_TIP_PGE:
			decoder->pge = true;
			intel_pt_mtc_cyc_cnt_pge(decoder);
			if (intel_pt_have_ip(decoder))
				intel_pt_set_ip(decoder);
			if (!decoder->ip)
				break;
			decoder->state.type |= INTEL_PT_TRACE_BEGIN;
			return 0;

		case INTEL_PT_TIP:
			decoder->pge = true;
			if (intel_pt_have_ip(decoder))
				intel_pt_set_ip(decoder);
			if (!decoder->ip)
				break;
			return 0;

		case INTEL_PT_FUP:
			if (intel_pt_have_ip(decoder))
				intel_pt_set_ip(decoder);
			if (decoder->ip)
				return 0;
			break;

		case INTEL_PT_MTC:
			intel_pt_calc_mtc_timestamp(decoder);
			break;

		case INTEL_PT_TSC:
			intel_pt_calc_tsc_timestamp(decoder);
			break;

		case INTEL_PT_TMA:
			intel_pt_calc_tma(decoder);
			break;

		case INTEL_PT_CYC:
			intel_pt_calc_cyc_timestamp(decoder);
			break;

		case INTEL_PT_CBR:
			intel_pt_calc_cbr(decoder);
			break;

		case INTEL_PT_PIP:
			decoder->cr3 = decoder->packet.payload & (BIT63 - 1);
			break;

		case INTEL_PT_MODE_EXEC:
			decoder->exec_mode = decoder->packet.payload;
			break;

		case INTEL_PT_MODE_TSX:
			intel_pt_update_in_tx(decoder);
			break;

		case INTEL_PT_OVF:
			return intel_pt_overflow(decoder);

		case INTEL_PT_BAD: /* Does not happen */
			return intel_pt_bug(decoder);

		case INTEL_PT_TRACESTOP:
			decoder->pge = false;
			decoder->continuous_period = false;
			intel_pt_clear_tx_flags(decoder);
			decoder->have_tma = false;
			break;

		case INTEL_PT_PSB:
			decoder->last_ip = 0;
			decoder->have_last_ip = true;
			intel_pt_clear_stack(&decoder->stack);
			err = intel_pt_walk_psb(decoder);
			if (err)
				return err;
			if (decoder->ip) {
				/* Do not have a sample */
				decoder->state.type = 0;
				return 0;
			}
			break;

		case INTEL_PT_TNT:
		case INTEL_PT_PSBEND:
		case INTEL_PT_VMCS:
		case INTEL_PT_MNT:
		case INTEL_PT_PAD:
		case INTEL_PT_PTWRITE:
		case INTEL_PT_PTWRITE_IP:
		case INTEL_PT_EXSTOP:
		case INTEL_PT_EXSTOP_IP:
		case INTEL_PT_MWAIT:
		case INTEL_PT_PWRE:
		case INTEL_PT_PWRX:
		default:
			break;
		}
	}
}

static int intel_pt_sync_ip(struct intel_pt_decoder *decoder)
{
	int err;

	decoder->set_fup_tx_flags = false;
	decoder->set_fup_ptw = false;
	decoder->set_fup_mwait = false;
	decoder->set_fup_pwre = false;
	decoder->set_fup_exstop = false;

	if (!decoder->branch_enable) {
		decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
		decoder->overflow = false;
		decoder->state.type = 0; /* Do not have a sample */
		return 0;
	}

	intel_pt_log("Scanning for full IP\n");
	err = intel_pt_walk_to_ip(decoder);
	if (err)
		return err;

	decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
	decoder->overflow = false;

	decoder->state.from_ip = 0;
	decoder->state.to_ip = decoder->ip;
	intel_pt_log_to("Setting IP", decoder->ip);

	return 0;
}

static int intel_pt_part_psb(struct intel_pt_decoder *decoder)
{
	const unsigned char *end = decoder->buf + decoder->len;
	size_t i;

	for (i = INTEL_PT_PSB_LEN - 1; i; i--) {
		if (i > decoder->len)
			continue;
		if (!memcmp(end - i, INTEL_PT_PSB_STR, i))
			return i;
	}
	return 0;
}

static int intel_pt_rest_psb(struct intel_pt_decoder *decoder, int part_psb)
{
	size_t rest_psb = INTEL_PT_PSB_LEN - part_psb;
	const char *psb = INTEL_PT_PSB_STR;

	if (rest_psb > decoder->len ||
	    memcmp(decoder->buf, psb + part_psb, rest_psb))
		return 0;

	return rest_psb;
}

static int intel_pt_get_split_psb(struct intel_pt_decoder *decoder,
				  int part_psb)
{
	int rest_psb, ret;

	decoder->pos += decoder->len;
	decoder->len = 0;

	ret = intel_pt_get_next_data(decoder, false);
	if (ret)
		return ret;

	rest_psb = intel_pt_rest_psb(decoder, part_psb);
	if (!rest_psb)
		return 0;

	decoder->pos -= part_psb;
	decoder->next_buf = decoder->buf + rest_psb;
	decoder->next_len = decoder->len - rest_psb;
	memcpy(decoder->temp_buf, INTEL_PT_PSB_STR, INTEL_PT_PSB_LEN);
	decoder->buf = decoder->temp_buf;
	decoder->len = INTEL_PT_PSB_LEN;

	return 0;
}

static int intel_pt_scan_for_psb(struct intel_pt_decoder *decoder)
{
	unsigned char *next;
	int ret;

	intel_pt_log("Scanning for PSB\n");
	while (1) {
		if (!decoder->len) {
			ret = intel_pt_get_next_data(decoder, false);
			if (ret)
				return ret;
		}

		next = memmem(decoder->buf, decoder->len, INTEL_PT_PSB_STR,
			      INTEL_PT_PSB_LEN);
		if (!next) {
			int part_psb;

			part_psb = intel_pt_part_psb(decoder);
			if (part_psb) {
				ret = intel_pt_get_split_psb(decoder, part_psb);
				if (ret)
					return ret;
			} else {
				decoder->pos += decoder->len;
				decoder->len = 0;
			}
			continue;
		}

		decoder->pkt_step = next - decoder->buf;
		return intel_pt_get_next_packet(decoder);
	}
}

static int intel_pt_sync(struct intel_pt_decoder *decoder)
{
	int err;

	decoder->pge = false;
	decoder->continuous_period = false;
	decoder->have_last_ip = false;
	decoder->last_ip = 0;
	decoder->ip = 0;
	intel_pt_clear_stack(&decoder->stack);

	err = intel_pt_scan_for_psb(decoder);
	if (err)
		return err;

	decoder->have_last_ip = true;
	decoder->pkt_state = INTEL_PT_STATE_NO_IP;

	err = intel_pt_walk_psb(decoder);
	if (err)
		return err;

	if (decoder->ip) {
		decoder->state.type = 0; /* Do not have a sample */
		decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
	} else {
		return intel_pt_sync_ip(decoder);
	}

	return 0;
}

static uint64_t intel_pt_est_timestamp(struct intel_pt_decoder *decoder)
{
	uint64_t est = decoder->sample_insn_cnt << 1;

	if (!decoder->cbr || !decoder->max_non_turbo_ratio)
		goto out;

	est *= decoder->max_non_turbo_ratio;
	est /= decoder->cbr;
out:
	return decoder->sample_timestamp + est;
}

const struct intel_pt_state *intel_pt_decode(struct intel_pt_decoder *decoder)
{
	int err;

	do {
		decoder->state.type = INTEL_PT_BRANCH;
		decoder->state.flags = 0;

		switch (decoder->pkt_state) {
		case INTEL_PT_STATE_NO_PSB:
			err = intel_pt_sync(decoder);
			break;
		case INTEL_PT_STATE_NO_IP:
			decoder->have_last_ip = false;
			decoder->last_ip = 0;
			decoder->ip = 0;
			__fallthrough;
		case INTEL_PT_STATE_ERR_RESYNC:
			err = intel_pt_sync_ip(decoder);
			break;
		case INTEL_PT_STATE_IN_SYNC:
			err = intel_pt_walk_trace(decoder);
			break;
		case INTEL_PT_STATE_TNT:
		case INTEL_PT_STATE_TNT_CONT:
			err = intel_pt_walk_tnt(decoder);
			if (err == -EAGAIN)
				err = intel_pt_walk_trace(decoder);
			break;
		case INTEL_PT_STATE_TIP:
		case INTEL_PT_STATE_TIP_PGD:
			err = intel_pt_walk_tip(decoder);
			break;
		case INTEL_PT_STATE_FUP:
			decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			err = intel_pt_walk_fup(decoder);
			if (err == -EAGAIN)
				err = intel_pt_walk_fup_tip(decoder);
			else if (!err)
				decoder->pkt_state = INTEL_PT_STATE_FUP;
			break;
		case INTEL_PT_STATE_FUP_NO_TIP:
			decoder->pkt_state = INTEL_PT_STATE_IN_SYNC;
			err = intel_pt_walk_fup(decoder);
			if (err == -EAGAIN)
				err = intel_pt_walk_trace(decoder);
			break;
		default:
			err = intel_pt_bug(decoder);
			break;
		}
	} while (err == -ENOLINK);

	if (err) {
		decoder->state.err = intel_pt_ext_err(err);
		decoder->state.from_ip = decoder->ip;
		intel_pt_update_sample_time(decoder);
		decoder->sample_tot_cyc_cnt = decoder->tot_cyc_cnt;
	} else {
		decoder->state.err = 0;
		if (decoder->cbr != decoder->cbr_seen && decoder->state.type) {
			decoder->cbr_seen = decoder->cbr;
			decoder->state.type |= INTEL_PT_CBR_CHG;
			decoder->state.cbr_payload = decoder->cbr_payload;
		}
		if (intel_pt_sample_time(decoder->pkt_state)) {
			intel_pt_update_sample_time(decoder);
			if (decoder->sample_cyc)
				decoder->sample_tot_cyc_cnt = decoder->tot_cyc_cnt;
		}
	}

	decoder->state.timestamp = decoder->sample_timestamp;
	decoder->state.est_timestamp = intel_pt_est_timestamp(decoder);
	decoder->state.cr3 = decoder->cr3;
	decoder->state.tot_insn_cnt = decoder->tot_insn_cnt;
	decoder->state.tot_cyc_cnt = decoder->sample_tot_cyc_cnt;

	return &decoder->state;
}

/**
 * intel_pt_next_psb - move buffer pointer to the start of the next PSB packet.
 * @buf: pointer to buffer pointer
 * @len: size of buffer
 *
 * Updates the buffer pointer to point to the start of the next PSB packet if
 * there is one, otherwise the buffer pointer is unchanged.  If @buf is updated,
 * @len is adjusted accordingly.
 *
 * Return: %true if a PSB packet is found, %false otherwise.
 */
static bool intel_pt_next_psb(unsigned char **buf, size_t *len)
{
	unsigned char *next;

	next = memmem(*buf, *len, INTEL_PT_PSB_STR, INTEL_PT_PSB_LEN);
	if (next) {
		*len -= next - *buf;
		*buf = next;
		return true;
	}
	return false;
}

/**
 * intel_pt_step_psb - move buffer pointer to the start of the following PSB
 *                     packet.
 * @buf: pointer to buffer pointer
 * @len: size of buffer
 *
 * Updates the buffer pointer to point to the start of the following PSB packet
 * (skipping the PSB at @buf itself) if there is one, otherwise the buffer
 * pointer is unchanged.  If @buf is updated, @len is adjusted accordingly.
 *
 * Return: %true if a PSB packet is found, %false otherwise.
 */
static bool intel_pt_step_psb(unsigned char **buf, size_t *len)
{
	unsigned char *next;

	if (!*len)
		return false;

	next = memmem(*buf + 1, *len - 1, INTEL_PT_PSB_STR, INTEL_PT_PSB_LEN);
	if (next) {
		*len -= next - *buf;
		*buf = next;
		return true;
	}
	return false;
}

/**
 * intel_pt_last_psb - find the last PSB packet in a buffer.
 * @buf: buffer
 * @len: size of buffer
 *
 * This function finds the last PSB in a buffer.
 *
 * Return: A pointer to the last PSB in @buf if found, %NULL otherwise.
 */
static unsigned char *intel_pt_last_psb(unsigned char *buf, size_t len)
{
	const char *n = INTEL_PT_PSB_STR;
	unsigned char *p;
	size_t k;

	if (len < INTEL_PT_PSB_LEN)
		return NULL;

	k = len - INTEL_PT_PSB_LEN + 1;
	while (1) {
		p = memrchr(buf, n[0], k);
		if (!p)
			return NULL;
		if (!memcmp(p + 1, n + 1, INTEL_PT_PSB_LEN - 1))
			return p;
		k = p - buf;
		if (!k)
			return NULL;
	}
}

/**
 * intel_pt_next_tsc - find and return next TSC.
 * @buf: buffer
 * @len: size of buffer
 * @tsc: TSC value returned
 * @rem: returns remaining size when TSC is found
 *
 * Find a TSC packet in @buf and return the TSC value.  This function assumes
 * that @buf starts at a PSB and that PSB+ will contain TSC and so stops if a
 * PSBEND packet is found.
 *
 * Return: %true if TSC is found, false otherwise.
 */
static bool intel_pt_next_tsc(unsigned char *buf, size_t len, uint64_t *tsc,
			      size_t *rem)
{
	struct intel_pt_pkt packet;
	int ret;

	while (len) {
		ret = intel_pt_get_packet(buf, len, &packet);
		if (ret <= 0)
			return false;
		if (packet.type == INTEL_PT_TSC) {
			*tsc = packet.payload;
			*rem = len;
			return true;
		}
		if (packet.type == INTEL_PT_PSBEND)
			return false;
		buf += ret;
		len -= ret;
	}
	return false;
}

/**
 * intel_pt_tsc_cmp - compare 7-byte TSCs.
 * @tsc1: first TSC to compare
 * @tsc2: second TSC to compare
 *
 * This function compares 7-byte TSC values allowing for the possibility that
 * TSC wrapped around.  Generally it is not possible to know if TSC has wrapped
 * around so for that purpose this function assumes the absolute difference is
 * less than half the maximum difference.
 *
 * Return: %-1 if @tsc1 is before @tsc2, %0 if @tsc1 == @tsc2, %1 if @tsc1 is
 * after @tsc2.
 */
static int intel_pt_tsc_cmp(uint64_t tsc1, uint64_t tsc2)
{
	const uint64_t halfway = (1ULL << 55);

	if (tsc1 == tsc2)
		return 0;

	if (tsc1 < tsc2) {
		if (tsc2 - tsc1 < halfway)
			return -1;
		else
			return 1;
	} else {
		if (tsc1 - tsc2 < halfway)
			return 1;
		else
			return -1;
	}
}

#define MAX_PADDING (PERF_AUXTRACE_RECORD_ALIGNMENT - 1)

/**
 * adj_for_padding - adjust overlap to account for padding.
 * @buf_b: second buffer
 * @buf_a: first buffer
 * @len_a: size of first buffer
 *
 * @buf_a might have up to 7 bytes of padding appended. Adjust the overlap
 * accordingly.
 *
 * Return: A pointer into @buf_b from where non-overlapped data starts
 */
static unsigned char *adj_for_padding(unsigned char *buf_b,
				      unsigned char *buf_a, size_t len_a)
{
	unsigned char *p = buf_b - MAX_PADDING;
	unsigned char *q = buf_a + len_a - MAX_PADDING;
	int i;

	for (i = MAX_PADDING; i; i--, p++, q++) {
		if (*p != *q)
			break;
	}

	return p;
}

/**
 * intel_pt_find_overlap_tsc - determine start of non-overlapped trace data
 *                             using TSC.
 * @buf_a: first buffer
 * @len_a: size of first buffer
 * @buf_b: second buffer
 * @len_b: size of second buffer
 * @consecutive: returns true if there is data in buf_b that is consecutive
 *               to buf_a
 *
 * If the trace contains TSC we can look at the last TSC of @buf_a and the
 * first TSC of @buf_b in order to determine if the buffers overlap, and then
 * walk forward in @buf_b until a later TSC is found.  A precondition is that
 * @buf_a and @buf_b are positioned at a PSB.
 *
 * Return: A pointer into @buf_b from where non-overlapped data starts, or
 * @buf_b + @len_b if there is no non-overlapped data.
 */
static unsigned char *intel_pt_find_overlap_tsc(unsigned char *buf_a,
						size_t len_a,
						unsigned char *buf_b,
						size_t len_b, bool *consecutive)
{
	uint64_t tsc_a, tsc_b;
	unsigned char *p;
	size_t len, rem_a, rem_b;

	p = intel_pt_last_psb(buf_a, len_a);
	if (!p)
		return buf_b; /* No PSB in buf_a => no overlap */

	len = len_a - (p - buf_a);
	if (!intel_pt_next_tsc(p, len, &tsc_a, &rem_a)) {
		/* The last PSB+ in buf_a is incomplete, so go back one more */
		len_a -= len;
		p = intel_pt_last_psb(buf_a, len_a);
		if (!p)
			return buf_b; /* No full PSB+ => assume no overlap */
		len = len_a - (p - buf_a);
		if (!intel_pt_next_tsc(p, len, &tsc_a, &rem_a))
			return buf_b; /* No TSC in buf_a => assume no overlap */
	}

	while (1) {
		/* Ignore PSB+ with no TSC */
		if (intel_pt_next_tsc(buf_b, len_b, &tsc_b, &rem_b)) {
			int cmp = intel_pt_tsc_cmp(tsc_a, tsc_b);

			/* Same TSC, so buffers are consecutive */
			if (!cmp && rem_b >= rem_a) {
				unsigned char *start;

				*consecutive = true;
				start = buf_b + len_b - (rem_b - rem_a);
				return adj_for_padding(start, buf_a, len_a);
			}
			if (cmp < 0)
				return buf_b; /* tsc_a < tsc_b => no overlap */
		}

		if (!intel_pt_step_psb(&buf_b, &len_b))
			return buf_b + len_b; /* No PSB in buf_b => no data */
	}
}

/**
 * intel_pt_find_overlap - determine start of non-overlapped trace data.
 * @buf_a: first buffer
 * @len_a: size of first buffer
 * @buf_b: second buffer
 * @len_b: size of second buffer
 * @have_tsc: can use TSC packets to detect overlap
 * @consecutive: returns true if there is data in buf_b that is consecutive
 *               to buf_a
 *
 * When trace samples or snapshots are recorded there is the possibility that
 * the data overlaps.  Note that, for the purposes of decoding, data is only
 * useful if it begins with a PSB packet.
 *
 * Return: A pointer into @buf_b from where non-overlapped data starts, or
 * @buf_b + @len_b if there is no non-overlapped data.
 */
unsigned char *intel_pt_find_overlap(unsigned char *buf_a, size_t len_a,
				     unsigned char *buf_b, size_t len_b,
				     bool have_tsc, bool *consecutive)
{
	unsigned char *found;

	/* Buffer 'b' must start at PSB so throw away everything before that */
	if (!intel_pt_next_psb(&buf_b, &len_b))
		return buf_b + len_b; /* No PSB */

	if (!intel_pt_next_psb(&buf_a, &len_a))
		return buf_b; /* No overlap */

	if (have_tsc) {
		found = intel_pt_find_overlap_tsc(buf_a, len_a, buf_b, len_b,
						  consecutive);
		if (found)
			return found;
	}

	/*
	 * Buffer 'b' cannot end within buffer 'a' so, for comparison purposes,
	 * we can ignore the first part of buffer 'a'.
	 */
	while (len_b < len_a) {
		if (!intel_pt_step_psb(&buf_a, &len_a))
			return buf_b; /* No overlap */
	}

	/* Now len_b >= len_a */
	while (1) {
		/* Potential overlap so check the bytes */
		found = memmem(buf_a, len_a, buf_b, len_a);
		if (found) {
			*consecutive = true;
			return adj_for_padding(buf_b + len_a, buf_a, len_a);
		}

		/* Try again at next PSB in buffer 'a' */
		if (!intel_pt_step_psb(&buf_a, &len_a))
			return buf_b; /* No overlap */
	}
}

/**
 * struct fast_forward_data - data used by intel_pt_ff_cb().
 * @timestamp: timestamp to fast forward towards
 * @buf_timestamp: buffer timestamp of last buffer with trace data earlier than
 *                 the fast forward timestamp.
 */
struct fast_forward_data {
	uint64_t timestamp;
	uint64_t buf_timestamp;
};

/**
 * intel_pt_ff_cb - fast forward lookahead callback.
 * @buffer: Intel PT trace buffer
 * @data: opaque pointer to fast forward data (struct fast_forward_data)
 *
 * Determine if @buffer trace is past the fast forward timestamp.
 *
 * Return: 1 (stop lookahead) if @buffer trace is past the fast forward
 *         timestamp, and 0 otherwise.
 */
static int intel_pt_ff_cb(struct intel_pt_buffer *buffer, void *data)
{
	struct fast_forward_data *d = data;
	unsigned char *buf;
	uint64_t tsc;
	size_t rem;
	size_t len;

	buf = (unsigned char *)buffer->buf;
	len = buffer->len;

	if (!intel_pt_next_psb(&buf, &len) ||
	    !intel_pt_next_tsc(buf, len, &tsc, &rem))
		return 0;

	tsc = intel_pt_8b_tsc(tsc, buffer->ref_timestamp);

	intel_pt_log("Buffer 1st timestamp " x64_fmt " ref timestamp " x64_fmt "\n",
		     tsc, buffer->ref_timestamp);

	/*
	 * If the buffer contains a timestamp earlier that the fast forward
	 * timestamp, then record it, else stop.
	 */
	if (tsc < d->timestamp)
		d->buf_timestamp = buffer->ref_timestamp;
	else
		return 1;

	return 0;
}

/**
 * intel_pt_fast_forward - reposition decoder forwards.
 * @decoder: Intel PT decoder
 * @timestamp: timestamp to fast forward towards
 *
 * Reposition decoder at the last PSB with a timestamp earlier than @timestamp.
 *
 * Return: 0 on success or negative error code on failure.
 */
int intel_pt_fast_forward(struct intel_pt_decoder *decoder, uint64_t timestamp)
{
	struct fast_forward_data d = { .timestamp = timestamp };
	unsigned char *buf;
	size_t len;
	int err;

	intel_pt_log("Fast forward towards timestamp " x64_fmt "\n", timestamp);

	/* Find buffer timestamp of buffer to fast forward to */
	err = decoder->lookahead(decoder->data, intel_pt_ff_cb, &d);
	if (err < 0)
		return err;

	/* Walk to buffer with same buffer timestamp */
	if (d.buf_timestamp) {
		do {
			decoder->pos += decoder->len;
			decoder->len = 0;
			err = intel_pt_get_next_data(decoder, true);
			/* -ENOLINK means non-consecutive trace */
			if (err && err != -ENOLINK)
				return err;
		} while (decoder->buf_timestamp != d.buf_timestamp);
	}

	if (!decoder->buf)
		return 0;

	buf = (unsigned char *)decoder->buf;
	len = decoder->len;

	if (!intel_pt_next_psb(&buf, &len))
		return 0;

	/*
	 * Walk PSBs while the PSB timestamp is less than the fast forward
	 * timestamp.
	 */
	do {
		uint64_t tsc;
		size_t rem;

		if (!intel_pt_next_tsc(buf, len, &tsc, &rem))
			break;
		tsc = intel_pt_8b_tsc(tsc, decoder->buf_timestamp);
		/*
		 * A TSC packet can slip past MTC packets but, after fast
		 * forward, decoding starts at the TSC timestamp. That means
		 * the timestamps may not be exactly the same as the timestamps
		 * that would have been decoded without fast forward.
		 */
		if (tsc < timestamp) {
			intel_pt_log("Fast forward to next PSB timestamp " x64_fmt "\n", tsc);
			decoder->pos += decoder->len - len;
			decoder->buf = buf;
			decoder->len = len;
			intel_pt_reposition(decoder);
		} else {
			break;
		}
	} while (intel_pt_step_psb(&buf, &len));

	return 0;
}
