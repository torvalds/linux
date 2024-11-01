// SPDX-License-Identifier: GPL-2.0
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
#include <linux/uaccess.h>
#include <linux/kernel.h>

#include "state.h"
#include "netns.h"

struct nfsd_fault_inject_op {
	char *file;
	u64 (*get)(void);
	u64 (*set_val)(u64);
	u64 (*set_clnt)(struct sockaddr_storage *, size_t);
};

static struct dentry *debug_dir;

static ssize_t fault_inject_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	static u64 val;
	char read_buf[25];
	size_t size;
	loff_t pos = *ppos;
	struct nfsd_fault_inject_op *op = file_inode(file)->i_private;

	if (!pos)
		val = op->get();
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
		val = op->set_clnt(&sa, size);
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
		val = op->set_val(val);
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
		.get	  = nfsd_inject_print_delegations,
		.set_val  = nfsd_inject_forget_delegations,
		.set_clnt = nfsd_inject_forget_client_delegations,
	},
	{
		.file     = "recall_delegations",
		.get	  = nfsd_inject_print_delegations,
		.set_val  = nfsd_inject_recall_delegations,
		.set_clnt = nfsd_inject_recall_client_delegations,
	},
};

void nfsd_fault_inject_init(void)
{
	unsigned int i;
	struct nfsd_fault_inject_op *op;
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;

	debug_dir = debugfs_create_dir("nfsd", NULL);

	for (i = 0; i < ARRAY_SIZE(inject_ops); i++) {
		op = &inject_ops[i];
		debugfs_create_file(op->file, mode, debug_dir, op, &fops_nfsd);
	}
}
