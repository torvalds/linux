// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/net/sunrpc/stats.c
 *
 * procfs-based user access to generic RPC statistics. The stats files
 * reside in /proc/net/rpc.
 *
 * The read routines assume that the buffer passed in is just big enough.
 * If you implement an RPC service that has its own stats routine which
 * appends the generic RPC stats, make sure you don't exceed the PAGE_SIZE
 * limit.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/metrics.h>
#include <linux/rcupdate.h>

#include <trace/events/sunrpc.h>

#include "netns.h"

#define RPCDBG_FACILITY	RPCDBG_MISC

/*
 * Get RPC client stats
 */
static int rpc_proc_show(struct seq_file *seq, void *v) {
	const struct rpc_stat	*statp = seq->private;
	const struct rpc_program *prog = statp->program;
	unsigned int i, j;

	seq_printf(seq,
		"net %u %u %u %u\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	seq_printf(seq,
		"rpc %u %u %u\n",
			statp->rpccnt,
			statp->rpcretrans,
			statp->rpcauthrefresh);

	for (i = 0; i < prog->nrvers; i++) {
		const struct rpc_version *vers = prog->version[i];
		if (!vers)
			continue;
		seq_printf(seq, "proc%u %u",
					vers->number, vers->nrprocs);
		for (j = 0; j < vers->nrprocs; j++)
			seq_printf(seq, " %u", vers->counts[j]);
		seq_putc(seq, '\n');
	}
	return 0;
}

static int rpc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpc_proc_show, PDE_DATA(inode));
}

static const struct file_operations rpc_proc_fops = {
	.owner = THIS_MODULE,
	.open = rpc_proc_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Get RPC server stats
 */
void svc_seq_show(struct seq_file *seq, const struct svc_stat *statp)
{
	const struct svc_program *prog = statp->program;
	const struct svc_version *vers;
	unsigned int i, j;

	seq_printf(seq,
		"net %u %u %u %u\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	seq_printf(seq,
		"rpc %u %u %u %u %u\n",
			statp->rpccnt,
			statp->rpcbadfmt+statp->rpcbadauth+statp->rpcbadclnt,
			statp->rpcbadfmt,
			statp->rpcbadauth,
			statp->rpcbadclnt);

	for (i = 0; i < prog->pg_nvers; i++) {
		vers = prog->pg_vers[i];
		if (!vers)
			continue;
		seq_printf(seq, "proc%d %u", i, vers->vs_nproc);
		for (j = 0; j < vers->vs_nproc; j++)
			seq_printf(seq, " %u", vers->vs_count[j]);
		seq_putc(seq, '\n');
	}
}
EXPORT_SYMBOL_GPL(svc_seq_show);

/**
 * rpc_alloc_iostats - allocate an rpc_iostats structure
 * @clnt: RPC program, version, and xprt
 *
 */
struct rpc_iostats *rpc_alloc_iostats(struct rpc_clnt *clnt)
{
	struct rpc_iostats *stats;
	int i;

	stats = kcalloc(clnt->cl_maxproc, sizeof(*stats), GFP_KERNEL);
	if (stats) {
		for (i = 0; i < clnt->cl_maxproc; i++)
			spin_lock_init(&stats[i].om_lock);
	}
	return stats;
}
EXPORT_SYMBOL_GPL(rpc_alloc_iostats);

/**
 * rpc_free_iostats - release an rpc_iostats structure
 * @stats: doomed rpc_iostats structure
 *
 */
void rpc_free_iostats(struct rpc_iostats *stats)
{
	kfree(stats);
}
EXPORT_SYMBOL_GPL(rpc_free_iostats);

/**
 * rpc_count_iostats_metrics - tally up per-task stats
 * @task: completed rpc_task
 * @op_metrics: stat structure for OP that will accumulate stats from @task
 */
void rpc_count_iostats_metrics(const struct rpc_task *task,
			       struct rpc_iostats *op_metrics)
{
	struct rpc_rqst *req = task->tk_rqstp;
	ktime_t backlog, execute, now;

	if (!op_metrics || !req)
		return;

	now = ktime_get();
	spin_lock(&op_metrics->om_lock);

	op_metrics->om_ops++;
	/* kernel API: om_ops must never become larger than om_ntrans */
	op_metrics->om_ntrans += max(req->rq_ntrans, 1);
	op_metrics->om_timeouts += task->tk_timeouts;

	op_metrics->om_bytes_sent += req->rq_xmit_bytes_sent;
	op_metrics->om_bytes_recv += req->rq_reply_bytes_recvd;

	backlog = 0;
	if (ktime_to_ns(req->rq_xtime)) {
		backlog = ktime_sub(req->rq_xtime, task->tk_start);
		op_metrics->om_queue = ktime_add(op_metrics->om_queue, backlog);
	}

	op_metrics->om_rtt = ktime_add(op_metrics->om_rtt, req->rq_rtt);

	execute = ktime_sub(now, task->tk_start);
	op_metrics->om_execute = ktime_add(op_metrics->om_execute, execute);

	spin_unlock(&op_metrics->om_lock);

	trace_rpc_stats_latency(req->rq_task, backlog, req->rq_rtt, execute);
}
EXPORT_SYMBOL_GPL(rpc_count_iostats_metrics);

/**
 * rpc_count_iostats - tally up per-task stats
 * @task: completed rpc_task
 * @stats: array of stat structures
 *
 * Uses the statidx from @task
 */
void rpc_count_iostats(const struct rpc_task *task, struct rpc_iostats *stats)
{
	rpc_count_iostats_metrics(task,
				  &stats[task->tk_msg.rpc_proc->p_statidx]);
}
EXPORT_SYMBOL_GPL(rpc_count_iostats);

static void _print_name(struct seq_file *seq, unsigned int op,
			const struct rpc_procinfo *procs)
{
	if (procs[op].p_name)
		seq_printf(seq, "\t%12s: ", procs[op].p_name);
	else if (op == 0)
		seq_printf(seq, "\t        NULL: ");
	else
		seq_printf(seq, "\t%12u: ", op);
}

static void _add_rpc_iostats(struct rpc_iostats *a, struct rpc_iostats *b)
{
	a->om_ops += b->om_ops;
	a->om_ntrans += b->om_ntrans;
	a->om_timeouts += b->om_timeouts;
	a->om_bytes_sent += b->om_bytes_sent;
	a->om_bytes_recv += b->om_bytes_recv;
	a->om_queue = ktime_add(a->om_queue, b->om_queue);
	a->om_rtt = ktime_add(a->om_rtt, b->om_rtt);
	a->om_execute = ktime_add(a->om_execute, b->om_execute);
}

static void _print_rpc_iostats(struct seq_file *seq, struct rpc_iostats *stats,
			       int op, const struct rpc_procinfo *procs)
{
	_print_name(seq, op, procs);
	seq_printf(seq, "%lu %lu %lu %Lu %Lu %Lu %Lu %Lu\n",
		   stats->om_ops,
		   stats->om_ntrans,
		   stats->om_timeouts,
		   stats->om_bytes_sent,
		   stats->om_bytes_recv,
		   ktime_to_ms(stats->om_queue),
		   ktime_to_ms(stats->om_rtt),
		   ktime_to_ms(stats->om_execute));
}

void rpc_clnt_show_stats(struct seq_file *seq, struct rpc_clnt *clnt)
{
	struct rpc_xprt *xprt;
	unsigned int op, maxproc = clnt->cl_maxproc;

	if (!clnt->cl_metrics)
		return;

	seq_printf(seq, "\tRPC iostats version: %s  ", RPC_IOSTATS_VERS);
	seq_printf(seq, "p/v: %u/%u (%s)\n",
			clnt->cl_prog, clnt->cl_vers, clnt->cl_program->name);

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	if (xprt)
		xprt->ops->print_stats(xprt, seq);
	rcu_read_unlock();

	seq_printf(seq, "\tper-op statistics\n");
	for (op = 0; op < maxproc; op++) {
		struct rpc_iostats stats = {};
		struct rpc_clnt *next = clnt;
		do {
			_add_rpc_iostats(&stats, &next->cl_metrics[op]);
			if (next == next->cl_parent)
				break;
			next = next->cl_parent;
		} while (next);
		_print_rpc_iostats(seq, &stats, op, clnt->cl_procinfo);
	}
}
EXPORT_SYMBOL_GPL(rpc_clnt_show_stats);

/*
 * Register/unregister RPC proc files
 */
static inline struct proc_dir_entry *
do_register(struct net *net, const char *name, void *data,
	    const struct file_operations *fops)
{
	struct sunrpc_net *sn;

	dprintk("RPC:       registering /proc/net/rpc/%s\n", name);
	sn = net_generic(net, sunrpc_net_id);
	return proc_create_data(name, 0, sn->proc_net_rpc, fops, data);
}

struct proc_dir_entry *
rpc_proc_register(struct net *net, struct rpc_stat *statp)
{
	return do_register(net, statp->program->name, statp, &rpc_proc_fops);
}
EXPORT_SYMBOL_GPL(rpc_proc_register);

void
rpc_proc_unregister(struct net *net, const char *name)
{
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	remove_proc_entry(name, sn->proc_net_rpc);
}
EXPORT_SYMBOL_GPL(rpc_proc_unregister);

struct proc_dir_entry *
svc_proc_register(struct net *net, struct svc_stat *statp, const struct file_operations *fops)
{
	return do_register(net, statp->program->pg_name, statp, fops);
}
EXPORT_SYMBOL_GPL(svc_proc_register);

void
svc_proc_unregister(struct net *net, const char *name)
{
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	remove_proc_entry(name, sn->proc_net_rpc);
}
EXPORT_SYMBOL_GPL(svc_proc_unregister);

int rpc_proc_init(struct net *net)
{
	struct sunrpc_net *sn;

	dprintk("RPC:       registering /proc/net/rpc\n");
	sn = net_generic(net, sunrpc_net_id);
	sn->proc_net_rpc = proc_mkdir("rpc", net->proc_net);
	if (sn->proc_net_rpc == NULL)
		return -ENOMEM;

	return 0;
}

void rpc_proc_exit(struct net *net)
{
	dprintk("RPC:       unregistering /proc/net/rpc\n");
	remove_proc_entry("rpc", net->proc_net);
}
