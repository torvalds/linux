/*
 * intel_pt_decoder.h: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef INCLUDE__INTEL_PT_DECODER_H__
#define INCLUDE__INTEL_PT_DECODER_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "intel-pt-insn-decoder.h"

#define INTEL_PT_IN_TX		(1 << 0)
#define INTEL_PT_ABORT_TX	(1 << 1)
#define INTEL_PT_ASYNC		(1 << 2)

enum intel_pt_sample_type {
	INTEL_PT_BRANCH		= 1 << 0,
	INTEL_PT_INSTRUCTION	= 1 << 1,
	INTEL_PT_TRANSACTION	= 1 << 2,
};

enum intel_pt_period_type {
	INTEL_PT_PERIOD_NONE,
	INTEL_PT_PERIOD_INSTRUCTIONS,
	INTEL_PT_PERIOD_TICKS,
	INTEL_PT_PERIOD_MTC,
};

enum {
	INTEL_PT_ERR_NOMEM = 1,
	INTEL_PT_ERR_INTERN,
	INTEL_PT_ERR_BADPKT,
	INTEL_PT_ERR_NODATA,
	INTEL_PT_ERR_NOINSN,
	INTEL_PT_ERR_MISMAT,
	INTEL_PT_ERR_OVR,
	INTEL_PT_ERR_LOST,
	INTEL_PT_ERR_UNK,
	INTEL_PT_ERR_NELOOP,
	INTEL_PT_ERR_MAX,
};

struct intel_pt_state {
	enum intel_pt_sample_type type;
	int err;
	uint64_t from_ip;
	uint64_t to_ip;
	uint64_t cr3;
	uint64_t tot_insn_cnt;
	uint64_t timestamp;
	uint64_t est_timestamp;
	uint64_t trace_nr;
	uint32_t flags;
	enum intel_pt_insn_op insn_op;
	int insn_len;
	char insn[INTEL_PT_INSN_BUF_SZ];
};

struct intel_pt_insn;

struct intel_pt_buffer {
	const unsigned char *buf;
	size_t len;
	bool consecutive;
	uint64_t ref_timestamp;
	uint64_t trace_nr;
};

struct intel_pt_params {
	int (*get_trace)(struct intel_pt_buffer *buffer, void *data);
	int (*walk_insn)(struct intel_pt_insn *intel_pt_insn,
			 uint64_t *insn_cnt_ptr, uint64_t *ip, uint64_t to_ip,
			 uint64_t max_insn_cnt, void *data);
	bool (*pgd_ip)(uint64_t ip, void *data);
	void *data;
	bool return_compression;
	uint64_t period;
	enum intel_pt_period_type period_type;
	unsigned max_non_turbo_ratio;
	unsigned int mtc_period;
	uint32_t tsc_ctc_ratio_n;
	uint32_t tsc_ctc_ratio_d;
};

struct intel_pt_decoder;

struct intel_pt_decoder *intel_pt_decoder_new(struct intel_pt_params *params);
void intel_pt_decoder_free(struct intel_pt_decoder *decoder);

const struct intel_pt_state *intel_pt_decode(struct intel_pt_decoder *decoder);

unsigned char *intel_pt_find_overlap(unsigned char *buf_a, size_t len_a,
				     unsigned char *buf_b, size_t len_b,
				     bool have_tsc);

int intel_pt__strerror(int code, char *buf, size_t buflen);

#endif
