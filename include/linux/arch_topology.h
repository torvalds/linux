/*
 * include/linux/arch_topology.h - arch specific cpu topology information
 */
#ifndef _LINUX_ARCH_TOPOLOGY_H_
#define _LINUX_ARCH_TOPOLOGY_H_

void normalize_cpu_capacity(void);

struct device_node;
int parse_cpu_capacity(struct device_node *cpu_node, int cpu);

struct sched_domain;
unsigned long arch_scale_cpu_capacity(struct sched_domain *sd, int cpu);

void set_capacity_scale(unsigned int cpu, unsigned long capacity);

#endif /* _LINUX_ARCH_TOPOLOGY_H_ */
