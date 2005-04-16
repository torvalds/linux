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
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>

#define RPCDBG_FACILITY	RPCDBG_MISC

struct proc_dir_entry	*proc_net_rpc = NULL;

/*
 * Get RPC client stats
 */
static int rpc_proc_show(struct seq_file *seq, void *v) {
	const struct rpc_stat	*statp = seq->private;
	const struct rpc_program *prog = statp->program;
	int		i, j;

	seq_printf(seq,
		"net %d %d %d %d\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	seq_printf(seq,
		"rpc %d %d %d\n",
			statp->rpccnt,
			statp->rpcretrans,
			statp->rpcauthrefresh);

	for (i = 0; i < prog->nrvers; i++) {
		const struct rpc_version *vers = prog->version[i];
		if (!vers)
			continue;
		seq_printf(seq, "proc%d %d",
					vers->number, vers->nrprocs);
		for (j = 0; j < vers->nrprocs; j++)
			seq_printf(seq, " %d",
					vers->procs[j].p_count);
		seq_putc(seq, '\n');
	}
	return 0;
}

static int rpc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpc_proc_show, PDE(inode)->data);
}

static struct file_operations rpc_proc_fops = {
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
	int		i, j;

	seq_printf(seq,
		"net %d %d %d %d\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	seq_printf(seq,
		"rpc %d %d %d %d %d\n",
			statp->rpccnt,
			statp->rpcbadfmt+statp->rpcbadauth+statp->rpcbadclnt,
			statp->rpcbadfmt,
			statp->rpcbadauth,
			statp->rpcbadclnt);

	for (i = 0; i < prog->pg_nvers; i++) {
		if (!(vers = prog->pg_vers[i]) || !(proc = vers->vs_proc))
			continue;
		seq_printf(seq, "proc%d %d", i, vers->vs_nproc);
		for (j = 0; j < vers->vs_nproc; j++, proc++)
			seq_printf(seq, " %d", proc->pc_count);
		seq_putc(seq, '\n');
	}
}

/*
 * Register/unregister RPC proc files
 */
static inline struct proc_dir_entry *
do_register(const char *name, void *data, struct file_operations *fops)
{
	struct proc_dir_entry *ent;

	rpc_proc_init();
	dprintk("RPC: registering /proc/net/rpc/%s\n", name);

	ent = create_proc_entry(name, 0, proc_net_rpc);
	if (ent) {
		ent->proc_fops = fops;
		ent->data = data;
	}
	return ent;
}

struct proc_dir_entry *
rpc_proc_register(struct rpc_stat *statp)
{
	return do_register(statp->program->name, statp, &rpc_proc_fops);
}

void
rpc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}

struct proc_dir_entry *
svc_proc_register(struct svc_stat *statp, struct file_operations *fops)
{
	return do_register(statp->program->pg_name, statp, fops);
}

void
svc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}

void
rpc_proc_init(void)
{
	dprintk("RPC: registering /proc/net/rpc\n");
	if (!proc_net_rpc) {
		struct proc_dir_entry *ent;
		ent = proc_mkdir("rpc", proc_net);
		if (ent) {
			ent->owner = THIS_MODULE;
			proc_net_rpc = ent;
		}
	}
}

void
rpc_proc_exit(void)
{
	dprintk("RPC: unregistering /proc/net/rpc\n");
	if (proc_net_rpc) {
		proc_net_rpc = NULL;
		remove_proc_entry("net/rpc", NULL);
	}
}

