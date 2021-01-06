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

static int nfsd_proc_show(struct seq_file *seq, void *v)
{
	int i;

	seq_printf(seq, "rc %u %u %u\nfh %u 0 0 0 0\nio %u %u\n",
		      nfsdstats.rchits,
		      nfsdstats.rcmisses,
		      nfsdstats.rcnocache,
		      nfsdstats.fh_stale,
		      nfsdstats.io_read,
		      nfsdstats.io_write);

	/* thread usage: */
	seq_printf(seq, "th %u 0", nfsdstats.th_cnt);

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
	for (i = 0; i <= LAST_NFS4_OP; i++)
		seq_printf(seq, " %u", nfsdstats.nfs4_opcount[i]);

	seq_putc(seq, '\n');
#endif

	return 0;
}

static int nfsd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nfsd_proc_show, NULL);
}

static const struct proc_ops nfsd_proc_ops = {
	.proc_open	= nfsd_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

void
nfsd_stat_init(void)
{
	svc_proc_register(&init_net, &nfsd_svcstats, &nfsd_proc_ops);
}

void
nfsd_stat_shutdown(void)
{
	svc_proc_unregister(&init_net, "nfsd");
}
