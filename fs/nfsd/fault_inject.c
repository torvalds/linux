/*
 * Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
 *
 * Uses debugfs to create fault injection points for client testing
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>

#include "state.h"
#include "fault_inject.h"

struct nfsd_fault_inject_op {
	char *file;
	void (*func)(u64);
};

static struct nfsd_fault_inject_op inject_ops[] = {
	{
		.file   = "forget_clients",
		.func   = nfsd_forget_clients,
	},
	{
		.file   = "forget_locks",
		.func   = nfsd_forget_locks,
	},
	{
		.file   = "forget_openowners",
		.func   = nfsd_forget_openowners,
	},
	{
		.file   = "forget_delegations",
		.func   = nfsd_forget_delegations,
	},
	{
		.file   = "recall_delegations",
		.func   = nfsd_recall_delegations,
	},
};

static long int NUM_INJECT_OPS = sizeof(inject_ops) / sizeof(struct nfsd_fault_inject_op);
static struct dentry *debug_dir;

static int nfsd_inject_set(void *op_ptr, u64 val)
{
	struct nfsd_fault_inject_op *op = op_ptr;

	if (val == 0)
		printk(KERN_INFO "NFSD Fault Injection: %s (all)", op->file);
	else
		printk(KERN_INFO "NFSD Fault Injection: %s (n = %llu)", op->file, val);

	op->func(val);
	return 0;
}

static int nfsd_inject_get(void *data, u64 *val)
{
	*val = 0;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_nfsd, nfsd_inject_get, nfsd_inject_set, "%llu\n");

void nfsd_fault_inject_cleanup(void)
{
	debugfs_remove_recursive(debug_dir);
}

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
