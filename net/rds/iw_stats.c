/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "rds.h"
#include "iw.h"

DEFINE_PER_CPU(struct rds_iw_statistics, rds_iw_stats) ____cacheline_aligned;

static char *rds_iw_stat_names[] = {
	"iw_connect_raced",
	"iw_listen_closed_stale",
	"iw_tx_cq_call",
	"iw_tx_cq_event",
	"iw_tx_ring_full",
	"iw_tx_throttle",
	"iw_tx_sg_mapping_failure",
	"iw_tx_stalled",
	"iw_tx_credit_updates",
	"iw_rx_cq_call",
	"iw_rx_cq_event",
	"iw_rx_ring_empty",
	"iw_rx_refill_from_cq",
	"iw_rx_refill_from_thread",
	"iw_rx_alloc_limit",
	"iw_rx_credit_updates",
	"iw_ack_sent",
	"iw_ack_send_failure",
	"iw_ack_send_delayed",
	"iw_ack_send_piggybacked",
	"iw_ack_received",
	"iw_rdma_mr_alloc",
	"iw_rdma_mr_free",
	"iw_rdma_mr_used",
	"iw_rdma_mr_pool_flush",
	"iw_rdma_mr_pool_wait",
	"iw_rdma_mr_pool_depleted",
};

unsigned int rds_iw_stats_info_copy(struct rds_info_iterator *iter,
				    unsigned int avail)
{
	struct rds_iw_statistics stats = {0, };
	uint64_t *src;
	uint64_t *sum;
	size_t i;
	int cpu;

	if (avail < ARRAY_SIZE(rds_iw_stat_names))
		goto out;

	for_each_online_cpu(cpu) {
		src = (uint64_t *)&(per_cpu(rds_iw_stats, cpu));
		sum = (uint64_t *)&stats;
		for (i = 0; i < sizeof(stats) / sizeof(uint64_t); i++)
			*(sum++) += *(src++);
	}

	rds_stats_info_copy(iter, (uint64_t *)&stats, rds_iw_stat_names,
			    ARRAY_SIZE(rds_iw_stat_names));
out:
	return ARRAY_SIZE(rds_iw_stat_names);
}
