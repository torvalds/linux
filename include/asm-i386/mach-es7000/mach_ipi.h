#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

void send_IPI_mask_sequence(cpumask_t mask, int vector);

static inline void send_IPI_mask(cpumask_t mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	cpumask_t mask = cpu_online_map;
	cpu_clear(smp_processor_id(), mask);
	if (!cpus_empty(mask))
		send_IPI_mask(mask, vector);
}

static inline void send_IPI_all(int vector)
{
	send_IPI_mask(cpu_online_map, vector);
}

#endif /* __ASM_MACH_IPI_H */
