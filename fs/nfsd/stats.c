// SPDX-License-Identifier: GPL-2.0
/*
 * procfs-based user access to knfsd statistics
 *
 * /proc/net/rpc/nfsd
 *
 * Format:
 *	rc <hits> <misses> <nocache>
 *			Statistsics for the reply cache
 *	fh <stale> <deprecated filehandle cache stats>
 *			statistics for filehandle lookup
 *	io <bytes-read> <bytes-written>
 *			statistics for IO throughput
 *	th <threads> <deprecated thread usage histogram stats>
 *			number of threads
 *	ra <deprecated ra-cache stats>
 *
 *	plus generic RPC stats (see net/sunrpc/stats.c)
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/sunrpc/stats.h>
#include <net/net_namespace.h>

#include "nfsd.h"

struct nfsd_stats	nfsdstats;
struct svc_stat		nfsd_svcstats = {
	.program	= &nfsd_program,
};

static int nfsd_show(struct seq_file *seq, void *v)
{
	int i;

	seq_printf(seq, "rc %lld %lld %lld\nfh %lld 0 0 0 0\nio %lld %lld\n",
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_HITS]),
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_MISSES]),
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_NOCACHE]),
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_FH_STALE]),
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_IO_READ]),
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_IO_WRITE]));

	/* thread usage: */
	seq_printf(seq, "th %u 0", atomic_read(&nfsdstats.th_cnt));

	/* deprecated thread usage histogram stats */
	for (i = 0; i < 10; i++)
		seq_puts(seq, " 0.000");

	/* deprecated ra-cache stats */
	seq_puts(seq, "\nra 0 0 0 0 0 0 0 0 0 0 0 0\n");

	/* show my rpc info */
	svc_seq_show(seq, &nfsd_svcstats);

#ifdef CONFIG_NFSD_V4
	/* Show count for individual nfsv4 operations */
	/* Writing operation numbers 0 1 2 also for maintaining uniformity */
	seq_printf(seq,"proc4ops %u", LAST_NFS4_OP + 1);
	for (i = 0; i <= LAST_NFS4_OP; i++) {
		seq_printf(seq, " %lld",
			   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_NFS4_OP(i)]));
	}
	seq_printf(seq, "\nwdeleg_getattr %lld",
		percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_WDELEG_GETATTR]));

	seq_putc(seq, '\n');
#endif

	return 0;
}

DEFINE_PROC_SHOW_ATTRIBUTE(nfsd);

int nfsd_percpu_counters_init(struct percpu_counter counters[], int num)
{
	int i, err = 0;

	for (i = 0; !err && i < num; i++)
		err = percpu_counter_init(&counters[i], 0, GFP_KERNEL);

	if (!err)
		return 0;

	for (; i > 0; i--)
		percpu_counter_destroy(&counters[i-1]);

	return err;
}

void nfsd_percpu_counters_reset(struct percpu_counter counters[], int num)
{
	int i;

	for (i = 0; i < num; i++)
		percpu_counter_set(&counters[i], 0);
}

void nfsd_percpu_counters_destroy(struct percpu_counter counters[], int num)
{
	int i;

	for (i = 0; i < num; i++)
		percpu_counter_destroy(&counters[i]);
}

static int nfsd_stat_counters_init(void)
{
	return nfsd_percpu_counters_init(nfsdstats.counter, NFSD_STATS_COUNTERS_NUM);
}

static void nfsd_stat_counters_destroy(void)
{
	nfsd_percpu_counters_destroy(nfsdstats.counter, NFSD_STATS_COUNTERS_NUM);
}

int nfsd_stat_init(void)
{
	int err;

	err = nfsd_stat_counters_init();
	if (err)
		return err;

	svc_proc_register(&init_net, &nfsd_svcstats, &nfsd_proc_ops);

	return 0;
}

void nfsd_stat_shutdown(void)
{
	nfsd_stat_counters_destroy();
	svc_proc_unregister(&init_net, "nfsd");
}
