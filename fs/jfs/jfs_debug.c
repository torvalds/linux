/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_debug.h"

#ifdef PROC_FS_JFS /* see jfs_debug.h */

static struct proc_dir_entry *base;
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

static const struct file_operations jfs_loglevel_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= jfs_loglevel_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= jfs_loglevel_proc_write,
};
#endif

static struct {
	const char	*name;
	const struct file_operations *proc_fops;
} Entries[] = {
#ifdef CONFIG_JFS_STATISTICS
	{ "lmstats",	&jfs_lmstats_proc_fops, },
	{ "txstats",	&jfs_txstats_proc_fops, },
	{ "xtstat",	&jfs_xtstat_proc_fops, },
	{ "mpstat",	&jfs_mpstat_proc_fops, },
#endif
#ifdef CONFIG_JFS_DEBUG
	{ "TxAnchor",	&jfs_txanchor_proc_fops, },
	{ "loglevel",	&jfs_loglevel_proc_fops }
#endif
};
#define NPROCENT	ARRAY_SIZE(Entries)

void jfs_proc_init(void)
{
	int i;

	if (!(base = proc_mkdir("fs/jfs", NULL)))
		return;
	base->owner = THIS_MODULE;

	for (i = 0; i < NPROCENT; i++)
		proc_create(Entries[i].name, 0, base, Entries[i].proc_fops);
}

void jfs_proc_clean(void)
{
	int i;

	if (base) {
		for (i = 0; i < NPROCENT; i++)
			remove_proc_entry(Entries[i].name, base);
		remove_proc_entry("fs/jfs", NULL);
	}
}

#endif /* PROC_FS_JFS */
