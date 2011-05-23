/*
 * Persistent Storage - platform driver interface parts.
 *
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmsg_dump.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "internal.h"

/*
 * pstore_lock just protects "psinfo" during
 * calls to pstore_register()
 */
static DEFINE_SPINLOCK(pstore_lock);
static struct pstore_info *psinfo;

/* How much of the console log to snapshot */
static unsigned long kmsg_bytes = 10240;

void pstore_set_kmsg_bytes(int bytes)
{
	kmsg_bytes = bytes;
}

/* Tag each group of saved records with a sequence number */
static int	oopscount;

static char *reason_str[] = {
	"Oops", "Panic", "Kexec", "Restart", "Halt", "Poweroff", "Emergency"
};

/*
 * callback from kmsg_dump. (s2,l2) has the most recently
 * written bytes, older bytes are in (s1,l1). Save as much
 * as we can from the end of the buffer.
 */
static void pstore_dump(struct kmsg_dumper *dumper,
	    enum kmsg_dump_reason reason,
	    const char *s1, unsigned long l1,
	    const char *s2, unsigned long l2)
{
	unsigned long	s1_start, s2_start;
	unsigned long	l1_cpy, l2_cpy;
	unsigned long	size, total = 0;
	char		*dst, *why;
	u64		id;
	int		hsize, part = 1;

	if (reason < ARRAY_SIZE(reason_str))
		why = reason_str[reason];
	else
		why = "Unknown";

	mutex_lock(&psinfo->buf_mutex);
	oopscount++;
	while (total < kmsg_bytes) {
		dst = psinfo->buf;
		hsize = sprintf(dst, "%s#%d Part%d\n", why, oopscount, part++);
		size = psinfo->bufsize - hsize;
		dst += hsize;

		l2_cpy = min(l2, size);
		l1_cpy = min(l1, size - l2_cpy);

		if (l1_cpy + l2_cpy == 0)
			break;

		s2_start = l2 - l2_cpy;
		s1_start = l1 - l1_cpy;

		memcpy(dst, s1 + s1_start, l1_cpy);
		memcpy(dst + l1_cpy, s2 + s2_start, l2_cpy);

		id = psinfo->write(PSTORE_TYPE_DMESG, hsize + l1_cpy + l2_cpy);
		if (reason == KMSG_DUMP_OOPS && pstore_is_mounted())
			pstore_mkfile(PSTORE_TYPE_DMESG, psinfo->name, id,
				      psinfo->buf, hsize + l1_cpy + l2_cpy,
				      CURRENT_TIME, psinfo->erase);
		l1 -= l1_cpy;
		l2 -= l2_cpy;
		total += l1_cpy + l2_cpy;
	}
	mutex_unlock(&psinfo->buf_mutex);
}

static struct kmsg_dumper pstore_dumper = {
	.dump = pstore_dump,
};

/*
 * platform specific persistent storage driver registers with
 * us here. If pstore is already mounted, call the platform
 * read function right away to populate the file system. If not
 * then the pstore mount code will call us later to fill out
 * the file system.
 *
 * Register with kmsg_dump to save last part of console log on panic.
 */
int pstore_register(struct pstore_info *psi)
{
	struct module *owner = psi->owner;

	spin_lock(&pstore_lock);
	if (psinfo) {
		spin_unlock(&pstore_lock);
		return -EBUSY;
	}
	psinfo = psi;
	spin_unlock(&pstore_lock);

	if (owner && !try_module_get(owner)) {
		psinfo = NULL;
		return -EINVAL;
	}

	if (pstore_is_mounted())
		pstore_get_records();

	kmsg_dump_register(&pstore_dumper);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_register);

/*
 * Read all the records from the persistent store. Create and
 * file files in our filesystem.
 */
void pstore_get_records(void)
{
	struct pstore_info *psi = psinfo;
	ssize_t			size;
	u64			id;
	enum pstore_type_id	type;
	struct timespec		time;
	int			failed = 0, rc;

	if (!psi)
		return;

	mutex_lock(&psinfo->buf_mutex);
	rc = psi->open(psi);
	if (rc)
		goto out;

	while ((size = psi->read(&id, &type, &time)) > 0) {
		if (pstore_mkfile(type, psi->name, id, psi->buf, (size_t)size,
				  time, psi->erase))
			failed++;
	}
	psi->close(psi);
out:
	mutex_unlock(&psinfo->buf_mutex);

	if (failed)
		printk(KERN_WARNING "pstore: failed to load %d record(s) from '%s'\n",
		       failed, psi->name);
}

/*
 * Call platform driver to write a record to the
 * persistent store.
 */
int pstore_write(enum pstore_type_id type, char *buf, size_t size)
{
	u64	id;

	if (!psinfo)
		return -ENODEV;

	if (size > psinfo->bufsize)
		return -EFBIG;

	mutex_lock(&psinfo->buf_mutex);
	memcpy(psinfo->buf, buf, size);
	id = psinfo->write(type, size);
	if (pstore_is_mounted())
		pstore_mkfile(PSTORE_TYPE_DMESG, psinfo->name, id, psinfo->buf,
			      size, CURRENT_TIME, psinfo->erase);
	mutex_unlock(&psinfo->buf_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_write);
