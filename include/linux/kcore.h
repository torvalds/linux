/* SPDX-License-Identifier: GPL-2.0 */
/*
 * /proc/kcore definitions
 */
#ifndef _LINUX_KCORE_H
#define _LINUX_KCORE_H

enum kcore_type {
	KCORE_TEXT,
	KCORE_VMALLOC,
	KCORE_RAM,
	KCORE_VMEMMAP,
	KCORE_USER,
};

struct kcore_list {
	struct list_head list;
	unsigned long addr;
	size_t size;
	int type;
};

#ifdef CONFIG_PROC_KCORE
void __init kclist_add(struct kcore_list *, void *, size_t, int type);

extern int __init register_mem_pfn_is_ram(int (*fn)(unsigned long pfn));
#else
static inline
void kclist_add(struct kcore_list *new, void *addr, size_t size, int type)
{
}
#endif

#endif /* _LINUX_KCORE_H */
