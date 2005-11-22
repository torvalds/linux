/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <linux/config.h>

/*
 * generic non-linear memory support:
 *
 * 1) we will not split memory into more chunks than will fit into the
 *    flags field of the struct page
 */

#ifdef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data *node_data[];
/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)		(node_data[nid])

/*
 * Following are specific to this numa platform.
 */

extern int numa_cpu_lookup_table[];
extern cpumask_t numa_cpumask_lookup_table[];
#ifdef CONFIG_MEMORY_HOTPLUG
extern unsigned long max_pfn;
#endif

/*
 * Following are macros that each numa implmentation must define.
 */

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)	(NODE_DATA(nid)->node_end_pfn)

#endif /* CONFIG_NEED_MULTIPLE_NODES */

#ifdef CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID
extern int __init early_pfn_to_nid(unsigned long pfn);
#endif

#endif /* _ASM_MMZONE_H_ */
