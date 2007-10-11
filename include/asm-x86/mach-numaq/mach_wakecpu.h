#ifndef __ASM_MACH_WAKECPU_H
#define __ASM_MACH_WAKECPU_H

/* This file copes with machines that wakeup secondary CPUs by NMIs */

#define WAKE_SECONDARY_VIA_NMI

#define TRAMPOLINE_LOW phys_to_virt(0x8)
#define TRAMPOLINE_HIGH phys_to_virt(0xa)

#define boot_cpu_apicid boot_cpu_logical_apicid

/* We don't do anything here because we use NMI's to boot instead */
static inline void wait_for_init_deassert(atomic_t *deassert)
{
}

/*
 * Because we use NMIs rather than the INIT-STARTUP sequence to
 * bootstrap the CPUs, the APIC may be in a weird state. Kick it.
 */
static inline void smp_callin_clear_local_apic(void)
{
	clear_local_APIC();
}

static inline void store_NMI_vector(unsigned short *high, unsigned short *low)
{
	printk("Storing NMI vector\n");
	*high = *((volatile unsigned short *) TRAMPOLINE_HIGH);
	*low = *((volatile unsigned short *) TRAMPOLINE_LOW);
}

static inline void restore_NMI_vector(unsigned short *high, unsigned short *low)
{
	printk("Restoring NMI vector\n");
	*((volatile unsigned short *) TRAMPOLINE_HIGH) = *high;
	*((volatile unsigned short *) TRAMPOLINE_LOW) = *low;
}

#define inquire_remote_apic(apicid) {}

#endif /* __ASM_MACH_WAKECPU_H */
