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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/metrics.h>
#include <net/net_namespace.h>

#define RPCDBG_FACILITY	RPCDBG_MISC

struct proc_dir_entry	*proc_net_rpc = NULL;

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
			seq_printf(seq, " %u",
					vers->procs[j].p_count);
		seq_putc(seq, '\n');
	}
	return 0;
}

static int rpc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpc_proc_show, PDE(inode)->data);
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
void svc_seq_show(struct seq_file *seq, const struct svc_stat *statp) {
	const struct svc_program *prog = statp->program;
	const struct svc_procedure *proc;
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
		if (!(vers = prog->pg_vers[i]) || !(proc = vers->vs_proc))
			continue;
		seq_printf(seq, "proc%d %u", i, vers->vs_nproc);
		for (j = 0; j < vers->vs_nproc; j++, proc++)
			seq_printf(seq, " %u", proc->pc_count);
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
	struct rpc_iostats *new;
	new = kcalloc(clnt->cl_maxproc, sizeof(struct rpc_iostats), GFP_KERNEL);
	return new;
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
 * rpc_count_iostats - tally up per-task stats
 * @task: completed rpc_task
 *
 * Relies on the caller for serialization.
 */
void rpc_count_iostats(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_iostats *stats = task->tk_client->cl_metrics;
	struct rpc_iostats *op_metrics;
	long rtt, execute, queue;

	if (!stats || !req)
		return;
	op_metrics = &stats[task->tk_msg.rpc_proc->p_statidx];

	op_metrics->om_ops++;
	op_metrics->om_ntrans += req->rq_ntrans;
	op_metrics->om_timeouts += task->tk_timeouts;

	op_metrics->om_bytes_sent += task->tk_bytes_sent;
	op_metrics->om_bytes_recv += req->rq_received;

	queue = (long)req->rq_xtime - task->tk_start;
	if (queue < 0)
		queue = -queue;
	op_metrics->om_queue += queue;

	rtt = task->tk_rtt;
	if (rtt < 0)
		rtt = -rtt;
	op_metrics->om_rtt += rtt;

	execute = (long)jiffies - task->tk_start;
	if (execute < 0)
		execute = -execute;
	op_metrics->om_execute += execute;
}

static void _print_name(struct seq_file *seq, unsigned int op,
			struct rpc_procinfo *procs)
{
	if (procs[op].p_name)
		seq_printf(seq, "\t%12s: ", procs[op].p_name);
	else if (op == 0)
		seq_printf(seq, "\t        NULL: ");
	else
		seq_printf(seq, "\t%12u: ", op);
}

#define MILLISECS_PER_JIFFY	(1000 / HZ)

void rpc_print_iostats(struct seq_file *seq, struct rpc_clnt *clnt)
{
	struct rpc_iostats *stats = clnt->cl_metrics;
	struct rpc_xprt *xprt = clnt->cl_xprt;
	unsigned int op, maxproc = clnt->cl_maxproc;

	if (!stats)
		return;

	seq_printf(seq, "\tRPC iostats version: %s  ", RPC_IOSTATS_VERS);
	seq_printf(seq, "p/v: %u/%u (%s)\n",
			clnt->cl_prog, clnt->cl_vers, clnt->cl_protname);

	if (xprt)
		xprt->ops->print_stats(xprt, seq);

	seq_printf(seq, "\tper-op statistics\n");
	for (op = 0; op < maxproc; op++) {
		struct rpc_iostats *metrics = &stats[op];
		_print_name(seq, op, clnt->cl_procinfo);
		seq_printf(seq, "%lu %lu %lu %Lu %Lu %Lu %Lu %Lu\n",
				metrics->om_ops,
				metrics->om_ntrans,
				metrics->om_timeouts,
				metrics->om_bytes_sent,
				metrics->om_bytes_recv,
				metrics->om_queue * MILLISECS_PER_JIFFY,
				metrics->om_rtt * MILLISECS_PER_JIFFY,
				metrics->om_execute * MILLISECS_PER_JIFFY);
	}
}
EXPORT_SYMBOL_GPL(rpc_print_iostats);

/*
 * Register/unregister RPC proc files
 */
static inline struct proc_dir_entry *
do_register(const char *name, void *data, const struct file_operations *fops)
{
	rpc_proc_init();
	dprintk("RPC:       registering /proc/net/rpc/%s\n", name);

	return proc_create_data(name, 0, proc_net_rpc, fops, data);
}

struct proc_dir_entry *
rpc_proc_register(struct rpc_stat *statp)
{
	return do_register(statp->program->name, statp, &rpc_proc_fops);
}
EXPORT_SYMBOL_GPL(rpc_proc_register);

void
rpc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}
EXPORT_SYMBOL_GPL(rpc_proc_unregister);

struct proc_dir_entry *
svc_proc_register(struct svc_stat *statp, const struct file_operations *fops)
{
	return do_register(statp->program->pg_name, statp, fops);
}
EXPORT_SYMBOL_GPL(svc_proc_register);

void
svc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}
EXPORT_SYMBOL_GPL(svc_proc_unregister);

void
rpc_proc_init(void)
{
	dprintk("RPC:       registering /proc/net/rpc\n");
	if (!proc_net_rpc) {
		struct proc_dir_entry *ent;
		ent = proc_mkdir("rpc", init_net.proc_net);
		if (ent) {
			ent->owner = THIS_MODULE;
			proc_net_rpc = ent;
		}
	}
}

void
rpc_proc_exit(void)
{
	dprintk("RPC:       unregistering /proc/net/rpc\n");
	if (proc_net_rpc) {
		proc_net_rpc = NULL;
		remove_proc_entry("rpc", init_net.proc_net);
	}
}

