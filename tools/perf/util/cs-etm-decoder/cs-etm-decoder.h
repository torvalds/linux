/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright(C) 2015-2018 Linaro Limited.
 *
 * Author: Tor Jeremiassen <tor@ti.com>
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef INCLUDE__CS_ETM_DECODER_H__
#define INCLUDE__CS_ETM_DECODER_H__

#include <linux/types.h>
#include <stdio.h>

struct cs_etm_decoder;

struct cs_etm_buffer {
	const unsigned char *buf;
	size_t len;
	u64 offset;
	u64 ref_timestamp;
};

enum cs_etm_sample_type {
	CS_ETM_EMPTY = 0,
	CS_ETM_RANGE = 1 << 0,
	CS_ETM_TRACE_ON = 1 << 1,
};

struct cs_etm_packet {
	enum cs_etm_sample_type sample_type;
	u64 start_addr;
	u64 end_addr;
	u8 last_instr_taken_branch;
	u8 exc;
	u8 exc_ret;
	int cpu;
};

struct cs_etm_queue;

typedef u32 (*cs_etm_mem_cb_type)(struct cs_etm_queue *, u64,
				  size_t, u8 *);

struct cs_etmv4_trace_params {
	u32 reg_idr0;
	u32 reg_idr1;
	u32 reg_idr2;
	u32 reg_idr8;
	u32 reg_configr;
	u32 reg_traceidr;
};

struct cs_etm_trace_params {
	int protocol;
	union {
		struct cs_etmv4_trace_params etmv4;
	};
};

struct cs_etm_decoder_params {
	int operation;
	void (*packet_printer)(const char *msg);
	cs_etm_mem_cb_type mem_acc_cb;
	u8 formatted;
	u8 fsyncs;
	u8 hsyncs;
	u8 frame_aligned;
	void *data;
};

/*
 * The following enums are indexed starting with 1 to align with the
 * open source coresight trace decoder library.
 */
enum {
	CS_ETM_PROTO_ETMV3 = 1,
	CS_ETM_PROTO_ETMV4i,
	CS_ETM_PROTO_ETMV4d,
};

enum {
	CS_ETM_OPERATION_PRINT = 1,
	CS_ETM_OPERATION_DECODE,
};

int cs_etm_decoder__process_data_block(struct cs_etm_decoder *decoder,
				       u64 indx, const u8 *buf,
				       size_t len, size_t *consumed);

struct cs_etm_decoder *
cs_etm_decoder__new(int num_cpu,
		    struct cs_etm_decoder_params *d_params,
		    struct cs_etm_trace_params t_params[]);

void cs_etm_decoder__free(struct cs_etm_decoder *decoder);

int cs_etm_decoder__add_mem_access_cb(struct cs_etm_decoder *decoder,
				      u64 start, u64 end,
				      cs_etm_mem_cb_type cb_func);

int cs_etm_decoder__get_packet(struct cs_etm_decoder *decoder,
			       struct cs_etm_packet *packet);

int cs_etm_decoder__reset(struct cs_etm_decoder *decoder);

#endif /* INCLUDE__CS_ETM_DECODER_H__ */
