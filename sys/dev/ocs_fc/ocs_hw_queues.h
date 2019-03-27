/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 *
 */

#ifndef __OCS_HW_QUEUES_H__
#define __OCS_HW_QUEUES_H__

#define OCS_HW_MQ_DEPTH	128

typedef enum {
	QTOP_EQ = 0,
	QTOP_CQ,
	QTOP_WQ,
	QTOP_RQ,
	QTOP_MQ,
	QTOP_LAST,
} ocs_hw_qtop_entry_e;

typedef struct {
	ocs_hw_qtop_entry_e entry;
	uint8_t set_default;
	uint32_t len;
	uint8_t class;
	uint8_t ulp;
	uint8_t filter_mask;
} ocs_hw_qtop_entry_t;

typedef struct {
	struct rq_config {
		hw_eq_t *eq;
		uint32_t len;
		uint8_t class;
		uint8_t ulp;
		uint8_t filter_mask;
	} rq_cfg[16];
	uint32_t num_pairs;
} ocs_hw_mrq_t;


#define MAX_TOKENS			256
#define OCS_HW_MAX_QTOP_ENTRIES	200

typedef struct {
	ocs_os_handle_t os;
	ocs_hw_qtop_entry_t *entries;
	uint32_t alloc_count;
	uint32_t inuse_count;
	uint32_t entry_counts[QTOP_LAST];
	uint32_t rptcount[10];
	uint32_t rptcount_idx;
} ocs_hw_qtop_t;

extern ocs_hw_qtop_t *ocs_hw_qtop_parse(ocs_hw_t *hw, const char *qtop_string);
extern void ocs_hw_qtop_free(ocs_hw_qtop_t *qtop);
extern const char *ocs_hw_qtop_entry_name(ocs_hw_qtop_entry_e entry);
extern uint32_t ocs_hw_qtop_eq_count(ocs_hw_t *hw);

extern ocs_hw_rtn_e ocs_hw_init_queues(ocs_hw_t *hw, ocs_hw_qtop_t *qtop);
extern void hw_thread_eq_handler(ocs_hw_t *hw, hw_eq_t *eq, uint32_t max_isr_time_msec);
extern void hw_thread_cq_handler(ocs_hw_t *hw, hw_cq_t *cq);
extern  hw_wq_t *ocs_hw_queue_next_wq(ocs_hw_t *hw, ocs_hw_io_t *io);

#endif /* __OCS_HW_QUEUES_H__ */
