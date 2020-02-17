// SPDX-License-Identifier: GPL-2.0
/*
 * procfs-based user access to knfsd statistics
 *
 * /proc/net/rpc/nfsd
 *
 * Format:
 *	rc <hits> <misses> <nocache>
 *			Statistsics for the reply cache
 *	fh <stale> <total-lookups> <anonlookups> <dir-not-in-dcache> <nondir-not-in-dcache>
 *			statistics for filehandle lookup
 *	io <bytes-read> <bytes-written>
 *			statistics for IO throughput
 *	th <threads> <fullcnt> <10%-20%> <20%-30%> ... <90%-100%> <100%> 
 *			time (seconds) when nfsd thread usage above thresholds
 *			and number of times that all threads were in use
 *	ra cache-size  <10%  <20%  <30% ... <100% not-found
 *			number of times that read-ahead entry was found that deep in
 *			the cache.
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

	seq_printf(seq, "rc %u %u %u\nfh %u %u %u %u %u\nio %u %u\n",
		      nfsdstats.rchits,
		      nfsdstats.rcmisses,
		      nfsdstats.rcnocache,
		      nfsdstats.fh_stale,
		      nfsdstats.fh_lookup,
		      nfsdstats.fh_anon,
		      nfsdstats.fh_nocache_dir,
		      nfsdstats.fh_nocache_nondir,
		      nfsdstats.io_read,
		      nfsdstats.io_write);
	/* thread usage: */
	seq_printf(seq, "th %u %u", nfsdstats.th_cnt, nfsdstats.th_fullcnt);
	for (i=0; i<10; i++) {
		unsigned int jifs = nfsdstats.th_usage[i];
		unsigned int sec = jifs / HZ, msec = (jifs % HZ)*1000/HZ;
		seq_printf(seq, " %u.%03u", sec, msec);
	}

	/* newline and ra-cache */
	seq_printf(seq, "\nra %u", nfsdstats.ra_size);
	for (i=0; i<11; i++)
		seq_printf(seq, " %u", nfsdstats.ra_depth[i]);
	seq_putc(seq, '\n');
	
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
