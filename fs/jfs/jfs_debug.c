// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_debug.h"

#ifdef PROC_FS_JFS /* see jfs_debug.h */

#ifdef CONFIG_JFS_DEBUG
static int jfs_loglevel_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", jfsloglevel);
	return 0;
}

static int jfs_loglevel_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, jfs_loglevel_proc_show, NULL);
}

static ssize_t jfs_loglevel_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char c;

	if (get_user(c, buffer))
		return -EFAULT;

	/* yes, I know this is an ASCIIism.  --hch */
	if (c < '0' || c > '9')
		return -EINVAL;
	jfsloglevel = c - '0';
	return count;
}

static const struct proc_ops jfs_loglevel_proc_ops = {
	.proc_open	= jfs_loglevel_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= jfs_loglevel_proc_write,
};
#endif

void jfs_proc_init(void)
{
	struct proc_dir_entry *base;

	base = proc_mkdir("fs/jfs", NULL);
	if (!base)
		return;

#ifdef CONFIG_JFS_STATISTICS
	proc_create_single("lmstats", 0, base, jfs_lmstats_proc_show);
	proc_create_single("txstats", 0, base, jfs_txstats_proc_show);
	proc_create_single("xtstat", 0, base, jfs_xtstat_proc_show);
	proc_create_single("mpstat", 0, base, jfs_mpstat_proc_show);
#endif
#ifdef CONFIG_JFS_DEBUG
	proc_create_single("TxAnchor", 0, base, jfs_txanchor_proc_show);
	proc_create("loglevel", 0, base, &jfs_loglevel_proc_ops);
#endif
}

void jfs_proc_clean(void)
{
	remove_proc_subtree("fs/jfs", NULL);
}

#endif /* PROC_FS_JFS */
