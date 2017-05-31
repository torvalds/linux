/*
 * include/linux/arch_topology.h - arch specific cpu topology information
 */
#ifndef _LINUX_ARCH_TOPOLOGY_H_
#define _LINUX_ARCH_TOPOLOGY_H_

void topology_normalize_cpu_scale(void);

struct device_node;
int topology_parse_cpu_capacity(struct device_node *cpu_node, int cpu);

struct sched_domain;
unsigned long topology_get_cpu_scale(struct sched_domain *sd, int cpu);

void topology_set_cpu_scale(unsigned int cpu, unsigned long capacity);

#endif /* _LINUX_ARCH_TOPOLOGY_H_ */
