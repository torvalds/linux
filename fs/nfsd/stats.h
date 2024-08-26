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

void nfsd_proc_stat_init(struct net *net);
void nfsd_proc_stat_shutdown(struct net *net);

static inline void nfsd_stats_rc_hits_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_RC_HITS]);
}

static inline void nfsd_stats_rc_misses_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_RC_MISSES]);
}

static inline void nfsd_stats_rc_nocache_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_RC_NOCACHE]);
}

static inline void nfsd_stats_fh_stale_inc(struct nfsd_net *nn,
					   struct svc_export *exp)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_FH_STALE]);
	if (exp && exp->ex_stats)
		percpu_counter_inc(&exp->ex_stats->counter[EXP_STATS_FH_STALE]);
}

static inline void nfsd_stats_io_read_add(struct nfsd_net *nn,
					  struct svc_export *exp, s64 amount)
{
	percpu_counter_add(&nn->counter[NFSD_STATS_IO_READ], amount);
	if (exp && exp->ex_stats)
		percpu_counter_add(&exp->ex_stats->counter[EXP_STATS_IO_READ], amount);
}

static inline void nfsd_stats_io_write_add(struct nfsd_net *nn,
					   struct svc_export *exp, s64 amount)
{
	percpu_counter_add(&nn->counter[NFSD_STATS_IO_WRITE], amount);
	if (exp && exp->ex_stats)
		percpu_counter_add(&exp->ex_stats->counter[EXP_STATS_IO_WRITE], amount);
}

static inline void nfsd_stats_payload_misses_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_PAYLOAD_MISSES]);
}

static inline void nfsd_stats_drc_mem_usage_add(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_add(&nn->counter[NFSD_STATS_DRC_MEM_USAGE], amount);
}

static inline void nfsd_stats_drc_mem_usage_sub(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_sub(&nn->counter[NFSD_STATS_DRC_MEM_USAGE], amount);
}

#ifdef CONFIG_NFSD_V4
static inline void nfsd_stats_wdeleg_getattr_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_STATS_WDELEG_GETATTR]);
}
#endif
#endif /* _NFSD_STATS_H */
