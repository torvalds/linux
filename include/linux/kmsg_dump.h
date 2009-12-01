/*
 * linux/include/kmsg_dump.h
 *
 * Copyright (C) 2009 Net Insight AB
 *
 * Author: Simon Kagstrom <simon.kagstrom@netinsight.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#ifndef _LINUX_KMSG_DUMP_H
#define _LINUX_KMSG_DUMP_H

#include <linux/list.h>

enum kmsg_dump_reason {
	KMSG_DUMP_OOPS,
	KMSG_DUMP_PANIC,
};

/**
 * struct kmsg_dumper - kernel crash message dumper structure
 * @dump:	The callback which gets called on crashes. The buffer is passed
 * 		as two sections, where s1 (length l1) contains the older
 * 		messages and s2 (length l2) contains the newer.
 * @list:	Entry in the dumper list (private)
 * @registered:	Flag that specifies if this is already registered
 */
struct kmsg_dumper {
	void (*dump)(struct kmsg_dumper *dumper, enum kmsg_dump_reason reason,
			const char *s1, unsigned long l1,
			const char *s2, unsigned long l2);
	struct list_head list;
	int registered;
};

#ifdef CONFIG_PRINTK
void kmsg_dump(enum kmsg_dump_reason reason);

int kmsg_dump_register(struct kmsg_dumper *dumper);

int kmsg_dump_unregister(struct kmsg_dumper *dumper);
#else
static inline void kmsg_dump(enum kmsg_dump_reason reason)
{
}

static inline int kmsg_dump_register(struct kmsg_dumper *dumper)
{
	return -EINVAL;
}

static inline int kmsg_dump_unregister(struct kmsg_dumper *dumper)
{
	return -EINVAL;
}
#endif

#endif /* _LINUX_KMSG_DUMP_H */
