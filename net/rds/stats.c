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

DEFINE_PER_CPU_SHARED_ALIGNED(struct rds_statistics, rds_stats);
EXPORT_PER_CPU_SYMBOL_GPL(rds_stats);

/* :.,$s/unsigned long\>.*\<s_\(.*\);/"\1",/g */

static const char *const rds_stat_names[] = {
	"conn_reset",
	"recv_drop_bad_checksum",
	"recv_drop_old_seq",
	"recv_drop_no_sock",
	"recv_drop_dead_sock",
	"recv_deliver_raced",
	"recv_delivered",
	"recv_queued",
	"recv_immediate_retry",
	"recv_delayed_retry",
	"recv_ack_required",
	"recv_rdma_bytes",
	"recv_ping",
	"send_queue_empty",
	"send_queue_full",
	"send_sem_contention",
	"send_sem_queue_raced",
	"send_immediate_retry",
	"send_delayed_retry",
	"send_drop_acked",
	"send_ack_required",
	"send_queued",
	"send_rdma",
	"send_rdma_bytes",
	"send_pong",
	"page_remainder_hit",
	"page_remainder_miss",
	"copy_to_user",
	"copy_from_user",
	"cong_update_queued",
	"cong_update_received",
	"cong_send_error",
	"cong_send_blocked",
};

void rds_stats_info_copy(struct rds_info_iterator *iter,
			 uint64_t *values, const char *const *names, size_t nr)
{
	struct rds_info_counter ctr;
	size_t i;

	for (i = 0; i < nr; i++) {
		BUG_ON(strlen(names[i]) >= sizeof(ctr.name));
		strncpy(ctr.name, names[i], sizeof(ctr.name) - 1);
		ctr.value = values[i];

		rds_info_copy(iter, &ctr, sizeof(ctr));
	}
}
EXPORT_SYMBOL_GPL(rds_stats_info_copy);

/*
 * This gives global counters across all the transports.  The strings
 * are copied in so that the tool doesn't need knowledge of the specific
 * stats that we're exporting.  Some are pretty implementation dependent
 * and may change over time.  That doesn't stop them from being useful.
 *
 * This is the only function in the chain that knows about the byte granular
 * length in userspace.  It converts it to number of stat entries that the
 * rest of the functions operate in.
 */
static void rds_stats_info(struct socket *sock, unsigned int len,
			   struct rds_info_iterator *iter,
			   struct rds_info_lengths *lens)
{
	struct rds_statistics stats = {0, };
	uint64_t *src;
	uint64_t *sum;
	size_t i;
	int cpu;
	unsigned int avail;

	avail = len / sizeof(struct rds_info_counter);

	if (avail < ARRAY_SIZE(rds_stat_names)) {
		avail = 0;
		goto trans;
	}

	for_each_online_cpu(cpu) {
		src = (uint64_t *)&(per_cpu(rds_stats, cpu));
		sum = (uint64_t *)&stats;
		for (i = 0; i < sizeof(stats) / sizeof(uint64_t); i++)
			*(sum++) += *(src++);
	}

	rds_stats_info_copy(iter, (uint64_t *)&stats, rds_stat_names,
			    ARRAY_SIZE(rds_stat_names));
	avail -= ARRAY_SIZE(rds_stat_names);

trans:
	lens->each = sizeof(struct rds_info_counter);
	lens->nr = rds_trans_stats_info_copy(iter, avail) +
		   ARRAY_SIZE(rds_stat_names);
}

void rds_stats_exit(void)
{
	rds_info_deregister_func(RDS_INFO_COUNTERS, rds_stats_info);
}

int __init rds_stats_init(void)
{
	rds_info_register_func(RDS_INFO_COUNTERS, rds_stats_info);
	return 0;
}
