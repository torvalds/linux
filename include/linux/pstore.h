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

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kmsg_dump.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/types.h>

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
	PSTORE_TYPE_PMSG	= 7,
	PSTORE_TYPE_PPC_OPAL	= 8,
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
	int		flags;
	int		(*open)(struct pstore_info *psi);
	int		(*close)(struct pstore_info *psi);
	ssize_t		(*read)(u64 *id, enum pstore_type_id *type,
			int *count, struct timespec *time, char **buf,
			bool *compressed, ssize_t *ecc_notice_size,
			struct pstore_info *psi);
	int		(*write)(enum pstore_type_id type,
			enum kmsg_dump_reason reason, u64 *id,
			unsigned int part, int count, bool compressed,
			size_t size, struct pstore_info *psi);
	int		(*write_buf)(enum pstore_type_id type,
			enum kmsg_dump_reason reason, u64 *id,
			unsigned int part, const char *buf, bool compressed,
			size_t size, struct pstore_info *psi);
	int		(*write_buf_user)(enum pstore_type_id type,
			enum kmsg_dump_reason reason, u64 *id,
			unsigned int part, const char __user *buf,
			bool compressed, size_t size, struct pstore_info *psi);
	int		(*erase)(enum pstore_type_id type, u64 id,
			int count, struct timespec time,
			struct pstore_info *psi);
	void		*data;
};

#define PSTORE_FLAGS_DMESG	(1 << 0)
#define PSTORE_FLAGS_FRAGILE	PSTORE_FLAGS_DMESG
#define PSTORE_FLAGS_CONSOLE	(1 << 1)
#define PSTORE_FLAGS_FTRACE	(1 << 2)
#define PSTORE_FLAGS_PMSG	(1 << 3)

extern int pstore_register(struct pstore_info *);
extern void pstore_unregister(struct pstore_info *);
extern bool pstore_cannot_block_path(enum kmsg_dump_reason reason);

struct pstore_ftrace_record {
	unsigned long ip;
	unsigned long parent_ip;
	u64 ts;
};

/*
 * ftrace related stuff: Both backends and frontends need these so expose
 * them here.
 */

#if NR_CPUS <= 2 && defined(CONFIG_ARM_THUMB)
#define PSTORE_CPU_IN_IP 0x1
#elif NR_CPUS <= 4 && defined(CONFIG_ARM)
#define PSTORE_CPU_IN_IP 0x3
#endif

#define TS_CPU_SHIFT 8
#define TS_CPU_MASK (BIT(TS_CPU_SHIFT) - 1)

/*
 * If CPU number can be stored in IP, store it there, otherwise store it in
 * the time stamp. This means more timestamp resolution is available when
 * the CPU can be stored in the IP.
 */
#ifdef PSTORE_CPU_IN_IP
static inline void
pstore_ftrace_encode_cpu(struct pstore_ftrace_record *rec, unsigned int cpu)
{
	rec->ip |= cpu;
}

static inline unsigned int
pstore_ftrace_decode_cpu(struct pstore_ftrace_record *rec)
{
	return rec->ip & PSTORE_CPU_IN_IP;
}

static inline u64
pstore_ftrace_read_timestamp(struct pstore_ftrace_record *rec)
{
	return rec->ts;
}

static inline void
pstore_ftrace_write_timestamp(struct pstore_ftrace_record *rec, u64 val)
{
	rec->ts = val;
}
#else
static inline void
pstore_ftrace_encode_cpu(struct pstore_ftrace_record *rec, unsigned int cpu)
{
	rec->ts &= ~(TS_CPU_MASK);
	rec->ts |= cpu;
}

static inline unsigned int
pstore_ftrace_decode_cpu(struct pstore_ftrace_record *rec)
{
	return rec->ts & TS_CPU_MASK;
}

static inline u64
pstore_ftrace_read_timestamp(struct pstore_ftrace_record *rec)
{
	return rec->ts >> TS_CPU_SHIFT;
}

static inline void
pstore_ftrace_write_timestamp(struct pstore_ftrace_record *rec, u64 val)
{
	rec->ts = (rec->ts & TS_CPU_MASK) | (val << TS_CPU_SHIFT);
}
#endif

#endif /*_LINUX_PSTORE_H*/
