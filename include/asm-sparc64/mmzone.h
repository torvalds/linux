#ifndef _SPARC64_MMZONE_H
#define _SPARC64_MMZONE_H

#ifdef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data *node_data[];

#define NODE_DATA(nid)		(node_data[nid])
#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)	(NODE_DATA(nid)->node_end_pfn)

extern int numa_cpu_lookup_table[];
extern cpumask_t numa_cpumask_lookup_table[];

#endif /* CONFIG_NEED_MULTIPLE_NODES */

#endif /* _SPARC64_MMZONE_H */
