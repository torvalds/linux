/*
 * Persistent Storage - pstore.h
 *
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 *
 * This code is the generic layer to export data records from platform
 * level persistent storage via a file system.
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
#ifndef _LINUX_PSTORE_H
#define _LINUX_PSTORE_H

#include <linux/time.h>
#include <linux/kmsg_dump.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

/* types */
enum pstore_type_id {
	PSTORE_TYPE_DMESG	= 0,
	PSTORE_TYPE_MCE		= 1,
	PSTORE_TYPE_CONSOLE	= 2,
	PSTORE_TYPE_FTRACE	= 3,
	/* PPC64 partition types */
	PSTORE_TYPE_PPC_RTAS	= 4,
	PSTORE_TYPE_PPC_OF	= 5,
	PSTORE_TYPE_PPC_COMMON	= 6,
	PSTORE_TYPE_UNKNOWN	= 255
};

struct module;

struct pstore_info {
	struct module	*owner;
	char		*name;
	spinlock_t	buf_lock;	/* serialize access to 'buf' */
	char		*buf;
	size_t		bufsize;
	struct mutex	read_mutex;	/* serialize open/read/close */
	int		(*open)(struct pstore_info *psi);
	int		(*close)(struct pstore_info *psi);
	ssize_t		(*read)(u64 *id, enum pstore_type_id *type,
			int *count, struct timespec *time, char **buf,
			struct pstore_info *psi);
	int		(*write)(enum pstore_type_id type,
			enum kmsg_dump_reason reason, u64 *id,
			unsigned int part, int count, size_t hsize,
			size_t size, struct pstore_info *psi);
	int		(*write_buf)(enum pstore_type_id type,
			enum kmsg_dump_reason reason, u64 *id,
			unsigned int part, const char *buf, size_t hsize,
			size_t size, struct pstore_info *psi);
	int		(*erase)(enum pstore_type_id type, u64 id,
			int count, struct timespec time,
			struct pstore_info *psi);
	void		*data;
};

#ifdef CONFIG_PSTORE
extern int pstore_register(struct pstore_info *);
extern bool pstore_cannot_block_path(enum kmsg_dump_reason reason);
#else
static inline int
pstore_register(struct pstore_info *psi)
{
	return -ENODEV;
}
static inline bool
pstore_cannot_block_path(enum kmsg_dump_reason reason)
{
	return false;
}
#endif

#endif /*_LINUX_PSTORE_H*/
