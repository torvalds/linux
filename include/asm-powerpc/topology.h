#ifndef _ASM_POWERPC_TOPOLOGY_H
#define _ASM_POWERPC_TOPOLOGY_H
#ifdef __KERNEL__


struct sys_device;
struct device_node;

#ifdef CONFIG_NUMA

#include <asm/mmzone.h>

static inline int cpu_to_node(int cpu)
{
	return numa_cpu_lookup_table[cpu];
}

#define parent_node(node)	(node)

static inline cpumask_t node_to_cpumask(int node)
{
	return numa_cpumask_lookup_table[node];
}

static inline int node_to_first_cpu(int node)
{
	cpumask_t tmp;
	tmp = node_to_cpumask(node);
	return first_cpu(tmp);
}

int of_node_to_nid(struct device_node *device);

struct pci_bus;
extern int pcibus_to_node(struct pci_bus *bus);

#define pcibus_to_cpumask(bus)	(pcibus_to_node(bus) == -1 ? \
					CPU_MASK_ALL : \
					node_to_cpumask(pcibus_to_node(bus)) \
				)

/* sched_domains SD_NODE_INIT for PPC64 machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.child			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 1,			\
	.per_cpu_gain		= 100,			\
	.busy_idx		= 3,			\
	.idle_idx		= 1,			\
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_NEWIDLE	\
				| SD_WAKE_IDLE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

extern void __init dump_numa_cpu_topology(void);

extern int sysfs_add_device_to_node(struct sys_device *dev, int nid);
extern void sysfs_remove_device_from_node(struct sys_device *dev, int nid);

#else

static inline int of_node_to_nid(struct device_node *device)
{
	return 0;
}

static inline void dump_numa_cpu_topology(void) {}

static inline int sysfs_add_device_to_node(struct sys_device *dev, int nid)
{
	return 0;
}

static inline void sysfs_remove_device_from_node(struct sys_device *dev,
						int nid)
{
}


#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#ifdef CONFIG_SMP
#include <asm/cputable.h>
#define smt_capable()		(cpu_has_feature(CPU_FTR_SMT))

#ifdef CONFIG_PPC64
#include <asm/smp.h>

#define topology_thread_siblings(cpu)	(cpu_sibling_map[cpu])
#endif
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_TOPOLOGY_H */
