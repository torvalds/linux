#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

/* Avoid include hell */
#define NMI_VECTOR 0x02

void send_IPI_mask_bitmask(cpumask_t mask, int vector);
void __send_IPI_shortcut(unsigned int shortcut, int vector);

extern int no_broadcast;

static inline void send_IPI_mask(cpumask_t mask, int vector)
{
	send_IPI_mask_bitmask(mask, vector);
}

static inline void __local_send_IPI_allbutself(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR) {
		cpumask_t mask = cpu_online_map;

		cpu_clear(smp_processor_id(), mask);
		send_IPI_mask(mask, vector);
	} else
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void __local_send_IPI_all(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		send_IPI_mask(cpu_online_map, vector);
	else
		__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then we get an APIC send 
	 * error if we try to broadcast, thus avoid sending IPIs in this case.
	 */
	if (!(num_online_cpus() > 1))
		return;

	__local_send_IPI_allbutself(vector);
	return;
}

static inline void send_IPI_all(int vector)
{
	__local_send_IPI_all(vector);
}

#endif /* __ASM_MACH_IPI_H */
