/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Statistics for NFS server.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */
#ifndef _NFSD_STATS_H
#define _NFSD_STATS_H

#include <uapi/linux/nfsd/stats.h>
#include <linux/percpu_counter.h>

#include "export.h"
#include "netns.h"

struct proc_dir_entry *nfsd_proc_stat_init(struct net *net);
void nfsd_proc_stat_shutdown(struct net *net);

/**
 * nfsd_stats_rc_hits_inc - Count a duplicate reply cache hit
 * @nn: target network namespace
 *
 * These reply cache counters are updated once per RPC. Readers use
 * percpu_counter_sum_positive(), so local batching does not affect
 * read accuracy.
 */
static inline void nfsd_stats_rc_hits_inc(struct nfsd_net *nn)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_RC_HITS], 1);
}

/**
 * nfsd_stats_rc_misses_inc - Count a duplicate reply cache miss
 * @nn: target network namespace
 *
 * See nfsd_stats_rc_hits_inc() for batching rationale.
 */
static inline void nfsd_stats_rc_misses_inc(struct nfsd_net *nn)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_RC_MISSES], 1);
}

/**
 * nfsd_stats_rc_nocache_inc - Count a request not cached in the reply cache
 * @nn: target network namespace
 *
 * See nfsd_stats_rc_hits_inc() for batching rationale.
 */
static inline void nfsd_stats_rc_nocache_inc(struct nfsd_net *nn)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_RC_NOCACHE], 1);
}

static inline void nfsd_stats_fh_stale_inc(struct nfsd_net *nn,
					   struct svc_export *exp)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_FH_STALE]);
	if (exp && exp->ex_stats)
		percpu_counter_inc(&exp->ex_stats->counter[EXP_STATS_FH_STALE]);
}

/**
 * nfsd_stats_io_read_add - Count number of bytes for an NFS READ
 * @nn: target network namespace
 * @exp: target export
 * @amount: byte count
 *
 * These counters are updated on every READ request. Readers use
 * percpu_counter_sum_positive(), so local batching does not affect
 * read accuracy.
 */
static inline void nfsd_stats_io_read_add(struct nfsd_net *nn,
					  struct svc_export *exp, s64 amount)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_IO_READ], amount);
	if (exp && exp->ex_stats)
		percpu_counter_add_local(&exp->ex_stats->counter[EXP_STATS_IO_READ],
					 amount);
}

/**
 * nfsd_stats_io_write_add - Count number of bytes for an NFS WRITE
 * @nn: target network namespace
 * @exp: target export
 * @amount: byte count
 *
 * These counters are updated on every WRITE request. Readers use
 * percpu_counter_sum_positive(), so local batching does not affect
 * read accuracy.
 */
static inline void nfsd_stats_io_write_add(struct nfsd_net *nn,
					   struct svc_export *exp, s64 amount)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_IO_WRITE], amount);
	if (exp && exp->ex_stats)
		percpu_counter_add_local(&exp->ex_stats->counter[EXP_STATS_IO_WRITE],
					 amount);
}

static inline void nfsd_stats_payload_misses_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_PAYLOAD_MISSES]);
}

/**
 * nfsd_stats_drc_mem_usage_add - Add memory used by a cache item
 * @nn: target network namespace
 * @amount: byte count
 *
 * percpu_counter_add_local() keeps updates on the per-CPU fast
 * path. The sole reader, percpu_counter_sum_positive(), sums the
 * per-CPU deltas, so batching locally does not lose accuracy.
 */
static inline void nfsd_stats_drc_mem_usage_add(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_add_local(&nn->counter[NFSD_STATS_DRC_MEM_USAGE],
				 amount);
}

/**
 * nfsd_stats_drc_mem_usage_sub - Subtract memory used by a cache item
 * @nn: target network namespace
 * @amount: byte count
 *
 * See nfsd_stats_drc_mem_usage_add() for batching rationale.
 */
static inline void nfsd_stats_drc_mem_usage_sub(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_sub_local(&nn->counter[NFSD_STATS_DRC_MEM_USAGE],
				 amount);
}

#ifdef CONFIG_NFSD_V4
static inline void nfsd_stats_cb_op_inc(struct nfsd_net *nn, u32 opcode)
{
	if (opcode >= OP_CB_GETATTR && opcode <= OP_CB_OFFLOAD)
		percpu_counter_inc(&nn->cb_counter[opcode]);
}
#endif
#endif /* _NFSD_STATS_H */
