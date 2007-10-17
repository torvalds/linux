#ifndef _ASM_X8664_NUMA_H 
#define _ASM_X8664_NUMA_H 1

#include <linux/nodemask.h>
#include <asm/apicdef.h>

struct bootnode {
	u64 start,end; 
};

extern int compute_hash_shift(struct bootnode *nodes, int numnodes);

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern void numa_add_cpu(int cpu);
extern void numa_init_array(void);
extern int numa_off;

extern void numa_set_node(int cpu, int node);
extern void srat_reserve_add_area(int nodeid);
extern int hotadd_percent;

extern unsigned char apicid_to_node[MAX_LOCAL_APIC];
#ifdef CONFIG_NUMA
extern void __init init_cpu_to_node(void);

static inline void clear_node_cpumask(int cpu)
{
	clear_bit(cpu, &node_to_cpumask[cpu_to_node(cpu)]);
}

#else
#define init_cpu_to_node() do {} while (0)
#define clear_node_cpumask(cpu) do {} while (0)
#endif

#define NUMA_NO_NODE 0xff

#endif
