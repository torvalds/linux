#ifndef _ASM_X8664_NUMA_H 
#define _ASM_X8664_NUMA_H 1

#include <linux/nodemask.h>
#include <asm/numnodes.h>

struct node { 
	u64 start,end; 
};

extern int compute_hash_shift(struct node *nodes, int numnodes);
extern int pxm_to_node(int nid);

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern void numa_add_cpu(int cpu);
extern void numa_init_array(void);
extern int numa_off;

extern void numa_set_node(int cpu, int node);

extern unsigned char apicid_to_node[256];

#define NUMA_NO_NODE 0xff

#endif
