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


enum {
	NFSD_STATS_RC_HITS,		/* repcache hits */
	NFSD_STATS_RC_MISSES,		/* repcache misses */
	NFSD_STATS_RC_NOCACHE,		/* uncached reqs */
	NFSD_STATS_FH_STALE,		/* FH stale error */
	NFSD_STATS_IO_READ,		/* bytes returned to read requests */
	NFSD_STATS_IO_WRITE,		/* bytes passed in write requests */
#ifdef CONFIG_NFSD_V4
	NFSD_STATS_FIRST_NFS4_OP,	/* count of individual nfsv4 operations */
	NFSD_STATS_LAST_NFS4_OP = NFSD_STATS_FIRST_NFS4_OP + LAST_NFS4_OP,
#define NFSD_STATS_NFS4_OP(op)	(NFSD_STATS_FIRST_NFS4_OP + (op))
#endif
	NFSD_STATS_COUNTERS_NUM
};

struct nfsd_stats {
	struct percpu_counter	counter[NFSD_STATS_COUNTERS_NUM];

	atomic_t	th_cnt;		/* number of available threads */
};

extern struct nfsd_stats	nfsdstats;

extern struct svc_stat		nfsd_svcstats;

int nfsd_percpu_counters_init(struct percpu_counter counters[], int num);
void nfsd_percpu_counters_reset(struct percpu_counter counters[], int num);
void nfsd_percpu_counters_destroy(struct percpu_counter counters[], int num);
int nfsd_stat_init(void);
void nfsd_stat_shutdown(void);

static inline void nfsd_stats_rc_hits_inc(void)
{
	percpu_counter_inc(&nfsdstats.counter[NFSD_STATS_RC_HITS]);
}

static inline void nfsd_stats_rc_misses_inc(void)
{
	percpu_counter_inc(&nfsdstats.counter[NFSD_STATS_RC_MISSES]);
}

static inline void nfsd_stats_rc_nocache_inc(void)
{
	percpu_counter_inc(&nfsdstats.counter[NFSD_STATS_RC_NOCACHE]);
}

static inline void nfsd_stats_fh_stale_inc(struct svc_export *exp)
{
	percpu_counter_inc(&nfsdstats.counter[NFSD_STATS_FH_STALE]);
	if (exp)
		percpu_counter_inc(&exp->ex_stats.counter[EXP_STATS_FH_STALE]);
}

static inline void nfsd_stats_io_read_add(struct svc_export *exp, s64 amount)
{
	percpu_counter_add(&nfsdstats.counter[NFSD_STATS_IO_READ], amount);
	if (exp)
		percpu_counter_add(&exp->ex_stats.counter[EXP_STATS_IO_READ], amount);
}

static inline void nfsd_stats_io_write_add(struct svc_export *exp, s64 amount)
{
	percpu_counter_add(&nfsdstats.counter[NFSD_STATS_IO_WRITE], amount);
	if (exp)
		percpu_counter_add(&exp->ex_stats.counter[EXP_STATS_IO_WRITE], amount);
}

static inline void nfsd_stats_payload_misses_inc(struct nfsd_net *nn)
{
	percpu_counter_inc(&nn->counter[NFSD_NET_PAYLOAD_MISSES]);
}

static inline void nfsd_stats_drc_mem_usage_add(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_add(&nn->counter[NFSD_NET_DRC_MEM_USAGE], amount);
}

static inline void nfsd_stats_drc_mem_usage_sub(struct nfsd_net *nn, s64 amount)
{
	percpu_counter_sub(&nn->counter[NFSD_NET_DRC_MEM_USAGE], amount);
}

#endif /* _NFSD_STATS_H */
