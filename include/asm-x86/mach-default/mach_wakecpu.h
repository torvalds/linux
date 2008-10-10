#ifndef ASM_X86__MACH_DEFAULT__MACH_WAKECPU_H
#define ASM_X86__MACH_DEFAULT__MACH_WAKECPU_H

/* 
 * This file copes with machines that wakeup secondary CPUs by the
 * INIT, INIT, STARTUP sequence.
 */

#define WAKE_SECONDARY_VIA_INIT

#define TRAMPOLINE_LOW phys_to_virt(0x467)
#define TRAMPOLINE_HIGH phys_to_virt(0x469)

#define boot_cpu_apicid boot_cpu_physical_apicid

static inline void wait_for_init_deassert(atomic_t *deassert)
{
	while (!atomic_read(deassert))
		cpu_relax();
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

#endif /* ASM_X86__MACH_DEFAULT__MACH_WAKECPU_H */
