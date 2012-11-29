/*
 * Copyright (c) 2011 Bryan Schumaker <bjschuma@netapp.com>
 *
 * Uses debugfs to create fault injection points for client testing
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "state.h"

struct nfsd_fault_inject_op {
	char *file;
	u64 (*forget)(struct nfs4_client *, u64);
	u64 (*print)(struct nfs4_client *, u64);
};

static struct nfsd_fault_inject_op inject_ops[] = {
	{
		.file   = "forget_clients",
		.forget = nfsd_forget_client,
		.print  = nfsd_print_client,
	},
	{
		.file   = "forget_locks",
		.forget = nfsd_forget_client_locks,
		.print  = nfsd_print_client_locks,
	},
	{
		.file   = "forget_openowners",
		.forget = nfsd_forget_client_openowners,
		.print  = nfsd_print_client_openowners,
	},
	{
		.file   = "forget_delegations",
		.forget = nfsd_forget_client_delegations,
		.print  = nfsd_print_client_delegations,
	},
	{
		.file   = "recall_delegations",
		.forget = nfsd_recall_client_delegations,
		.print  = nfsd_print_client_delegations,
	},
};

static long int NUM_INJECT_OPS = sizeof(inject_ops) / sizeof(struct nfsd_fault_inject_op);
static struct dentry *debug_dir;

static void nfsd_inject_set(struct nfsd_fault_inject_op *op, u64 val)
{
	u64 count = 0;

	if (val == 0)
		printk(KERN_INFO "NFSD Fault Injection: %s (all)", op->file);
	else
		printk(KERN_INFO "NFSD Fault Injection: %s (n = %llu)", op->file, val);

	nfs4_lock_state();
	count = nfsd_for_n_state(val, op->forget);
	nfs4_unlock_state();
	printk(KERN_INFO "NFSD: %s: found %llu", op->file, count);
}

static void nfsd_inject_get(struct nfsd_fault_inject_op *op, u64 *val)
{
	nfs4_lock_state();
	*val = nfsd_for_n_state(0, op->print);
	nfs4_unlock_state();
}

static ssize_t fault_inject_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	static u64 val;
	char read_buf[25];
	size_t size, ret;
	loff_t pos = *ppos;

	if (!pos)
		nfsd_inject_get(file->f_dentry->d_inode->i_private, &val);
	size = scnprintf(read_buf, sizeof(read_buf), "%llu\n", val);

	if (pos < 0)
		return -EINVAL;
	if (pos >= size || !len)
		return 0;
	if (len > size - pos)
		len = size - pos;
	ret = copy_to_user(buf, read_buf + pos, len);
	if (ret == len)
		return -EFAULT;
	len -= ret;
	*ppos = pos + len;
	return len;
}

static ssize_t fault_inject_write(struct file *file, const char __user *buf,
				  size_t len, loff_t *ppos)
{
	char write_buf[24];
	size_t size = min(sizeof(write_buf), len) - 1;
	u64 val;

	if (copy_from_user(write_buf, buf, size))
		return -EFAULT;

	val = simple_strtoll(write_buf, NULL, 0);
	nfsd_inject_set(file->f_dentry->d_inode->i_private, val);
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
