#ifndef __ASM_MACH_WAKECPU_H
#define __ASM_MACH_WAKECPU_H

/* 
 * This file copes with machines that wakeup secondary CPUs by the
 * INIT, INIT, STARTUP sequence.
 */

#ifdef CONFIG_ES7000_CLUSTERED_APIC
#define WAKE_SECONDARY_VIA_MIP
#else
#define WAKE_SECONDARY_VIA_INIT
#endif

#ifdef WAKE_SECONDARY_VIA_MIP
extern int es7000_start_cpu(int cpu, unsigned long eip);
static inline int
wakeup_secondary_cpu(int phys_apicid, unsigned long start_eip)
{
	int boot_error = 0;
	boot_error = es7000_start_cpu(phys_apicid, start_eip);
	return boot_error;
}
#endif

#define TRAMPOLINE_LOW phys_to_virt(0x467)
#define TRAMPOLINE_HIGH phys_to_virt(0x469)

#define boot_cpu_apicid boot_cpu_physical_apicid

static inline void wait_for_init_deassert(atomic_t *deassert)
{
#ifdef WAKE_SECONDARY_VIA_INIT
	while (!atomic_read(deassert))
		cpu_relax();
#endif
	return;
}

/* Nothing to do for most platforms, since cleared by the INIT cycle */
static inline void smp_callin_clear_local_apic(void)
{
}

static inline void store_NMI_vector(unsigned short *high, unsigned short *low)
{
}

static inline void restore_NMI_vector(unsigned short *high, unsigned short *low)
{
}

#if APIC_DEBUG
 #define inquire_remote_apic(apicid) __inquire_remote_apic(apicid)
#else
 #define inquire_remote_apic(apicid) {}
#endif

#endif /* __ASM_MACH_WAKECPU_H */
