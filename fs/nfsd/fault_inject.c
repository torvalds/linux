/*
 * Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
 *
 * Uses debugfs to create fault injection points for client testing
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/nsproxy.h>
#include <linux/sunrpc/addr.h>
#include <asm/uaccess.h>

#include "state.h"
#include "netns.h"

struct nfsd_fault_inject_op {
	char *file;
	u64 (*get)(struct nfsd_fault_inject_op *);
	u64 (*set_val)(struct nfsd_fault_inject_op *, u64);
	u64 (*set_clnt)(struct nfsd_fault_inject_op *,
			struct sockaddr_storage *, size_t);
	u64 (*forget)(struct nfs4_client *, u64);
	u64 (*print)(struct nfs4_client *, u64);
};

static struct dentry *debug_dir;

static u64 nfsd_inject_set(struct nfsd_fault_inject_op *op, u64 val)
{
	u64 count;

	nfs4_lock_state();
	count = nfsd_for_n_state(val, op->forget);
	nfs4_unlock_state();
	return count;
}

static u64 nfsd_inject_set_client(struct nfsd_fault_inject_op *op,
				   struct sockaddr_storage *addr,
				   size_t addr_size)
{
	struct nfs4_client *clp;
	u64 count = 0;

	nfs4_lock_state();
	clp = nfsd_find_client(addr, addr_size);
	if (clp)
		count = op->forget(clp, 0);
	nfs4_unlock_state();
	return count;
}

static u64 nfsd_inject_get(struct nfsd_fault_inject_op *op)
{
	u64 count;

	nfs4_lock_state();
	count = nfsd_for_n_state(0, op->print);
	nfs4_unlock_state();

	return count;
}

static ssize_t fault_inject_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	static u64 val;
	char read_buf[25];
	size_t size;
	loff_t pos = *ppos;
	struct nfsd_fault_inject_op *op = file_inode(file)->i_private;

	if (!pos)
		val = op->get(op);
	size = scnprintf(read_buf, sizeof(read_buf), "%llu\n", val);

	return simple_read_from_buffer(buf, len, ppos, read_buf, size);
}

static ssize_t fault_inject_write(struct file *file, const char __user *buf,
				  size_t len, loff_t *ppos)
{
	char write_buf[INET6_ADDRSTRLEN];
	size_t size = min(sizeof(write_buf) - 1, len);
	struct net *net = current->nsproxy->net_ns;
	struct sockaddr_storage sa;
	struct nfsd_fault_inject_op *op = file_inode(file)->i_private;
	u64 val;
	char *nl;

	if (copy_from_user(write_buf, buf, size))
		return -EFAULT;
	write_buf[size] = '\0';

	/* Deal with any embedded newlines in the string */
	nl = strchr(write_buf, '\n');
	if (nl) {
		size = nl - write_buf;
		*nl = '\0';
	}

	size = rpc_pton(net, write_buf, size, (struct sockaddr *)&sa, sizeof(sa));
	if (size > 0) {
		val = op->set_clnt(op, &sa, size);
		if (val)
			pr_info("NFSD [%s]: Client %s had %llu state object(s)\n",
				op->file, write_buf, val);
	} else {
		val = simple_strtoll(write_buf, NULL, 0);
		if (val == 0)
			pr_info("NFSD Fault Injection: %s (all)", op->file);
		else
			pr_info("NFSD Fault Injection: %s (n = %llu)",
				op->file, val);
		val = op->set_val(op, val);
		pr_info("NFSD: %s: found %llu", op->file, val);
	}
	return len; /* on success, claim we got the whole input */
}

static const struct file_operations fops_nfsd = {
	.owner   = THIS_MODULE,
	.read    = fault_inject_read,
	.write   = fault_inject_write,
};

void nfsd_fault_inject_cleanup(void)
{
	debugfs_remove_recursive(debug_dir);
}

static struct nfsd_fault_inject_op inject_ops[] = {
	{
		.file     = "forget_clients",
		.get	  = nfsd_inject_print_clients,
		.set_val  = nfsd_inject_forget_clients,
		.set_clnt = nfsd_inject_forget_client,
	},
	{
		.file     = "forget_locks",
		.get	  = nfsd_inject_print_locks,
		.set_val  = nfsd_inject_forget_locks,
		.set_clnt = nfsd_inject_forget_client_locks,
	},
	{
		.file     = "forget_openowners",
		.get	  = nfsd_inject_print_openowners,
		.set_val  = nfsd_inject_forget_openowners,
		.set_clnt = nfsd_inject_forget_client_openowners,
	},
	{
		.file     = "forget_delegations",
		.get	  = nfsd_inject_get,
		.set_val  = nfsd_inject_set,
		.set_clnt = nfsd_inject_set_client,
		.forget   = nfsd_forget_client_delegations,
		.print    = nfsd_print_client_delegations,
	},
	{
		.file     = "recall_delegations",
		.get	  = nfsd_inject_get,
		.set_val  = nfsd_inject_set,
		.set_clnt = nfsd_inject_set_client,
		.forget   = nfsd_recall_client_delegations,
		.print    = nfsd_print_client_delegations,
	},
};

#define NUM_INJECT_OPS (sizeof(inject_ops)/sizeof(struct nfsd_fault_inject_op))

int nfsd_fault_inject_init(void)
{
	unsigned int i;
	struct nfsd_fault_inject_op *op;
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;

	debug_dir = debugfs_create_dir("nfsd", NULL);
	if (!debug_dir)
		goto fail;

	for (i = 0; i < NUM_INJECT_OPS; i++) {
		op = &inject_ops[i];
		if (!debugfs_create_file(op->file, mode, debug_dir, op, &fops_nfsd))
			goto fail;
	}
	return 0;

fail:
	nfsd_fault_inject_cleanup();
	return -ENOMEM;
}
