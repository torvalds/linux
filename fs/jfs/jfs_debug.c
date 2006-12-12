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
#include <asm/uaccess.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_debug.h"

#ifdef CONFIG_JFS_DEBUG
void dump_mem(char *label, void *data, int length)
{
	int i, j;
	int *intptr = data;
	char *charptr = data;
	char buf[10], line[80];

	printk("%s: dump of %d bytes of data at 0x%p\n\n", label, length,
	       data);
	for (i = 0; i < length; i += 16) {
		line[0] = 0;
		for (j = 0; (j < 4) && (i + j * 4 < length); j++) {
			sprintf(buf, " %08x", intptr[i / 4 + j]);
			strcat(line, buf);
		}
		buf[0] = ' ';
		buf[2] = 0;
		for (j = 0; (j < 16) && (i + j < length); j++) {
			buf[1] =
			    isprint(charptr[i + j]) ? charptr[i + j] : '.';
			strcat(line, buf);
		}
		printk("%s\n", line);
	}
}
#endif

#ifdef PROC_FS_JFS /* see jfs_debug.h */

static struct proc_dir_entry *base;
#ifdef CONFIG_JFS_DEBUG
static int loglevel_read(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", jfsloglevel);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}

static int loglevel_write(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
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
#endif

static struct {
	const char	*name;
	read_proc_t	*read_fn;
	write_proc_t	*write_fn;
} Entries[] = {
#ifdef CONFIG_JFS_STATISTICS
	{ "lmstats",	jfs_lmstats_read, },
	{ "txstats",	jfs_txstats_read, },
	{ "xtstat",	jfs_xtstat_read, },
	{ "mpstat",	jfs_mpstat_read, },
#endif
#ifdef CONFIG_JFS_DEBUG
	{ "TxAnchor",	jfs_txanchor_read, },
	{ "loglevel",	loglevel_read, loglevel_write }
#endif
};
#define NPROCENT	ARRAY_SIZE(Entries)

void jfs_proc_init(void)
{
	int i;

	if (!(base = proc_mkdir("jfs", proc_root_fs)))
		return;
	base->owner = THIS_MODULE;

	for (i = 0; i < NPROCENT; i++) {
		struct proc_dir_entry *p;
		if ((p = create_proc_entry(Entries[i].name, 0, base))) {
			p->read_proc = Entries[i].read_fn;
			p->write_proc = Entries[i].write_fn;
		}
	}
}

void jfs_proc_clean(void)
{
	int i;

	if (base) {
		for (i = 0; i < NPROCENT; i++)
			remove_proc_entry(Entries[i].name, base);
		remove_proc_entry("jfs", proc_root_fs);
	}
}

#endif /* PROC_FS_JFS */
