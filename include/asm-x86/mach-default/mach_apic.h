#ifndef ASM_X86__MACH_DEFAULT__MACH_APIC_H
#define ASM_X86__MACH_DEFAULT__MACH_APIC_H

#ifdef CONFIG_X86_LOCAL_APIC

#include <mach_apicdef.h>
#include <asm/smp.h>

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

static inline cpumask_t target_cpus(void)
{ 
#ifdef CONFIG_SMP
	return cpu_online_map;
#else
	return cpumask_of_cpu(0);
#endif
} 

#define NO_BALANCE_IRQ (0)
#define esr_disable (0)

#ifdef CONFIG_X86_64
#include <asm/genapic.h>
#define INT_DELIVERY_MODE (genapic->int_delivery_mode)
#define INT_DEST_MODE (genapic->int_dest_mode)
#define TARGET_CPUS	  (genapic->target_cpus())
#define apic_id_registered (genapic->apic_id_registered)
#define init_apic_ldr (genapic->init_apic_ldr)
#define cpu_mask_to_apicid (genapic->cpu_mask_to_apicid)
#define phys_pkg_id	(genapic->phys_pkg_id)
#define vector_allocation_domain    (genapic->vector_allocation_domain)
extern void setup_apic_routing(void);
#else
#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */
#define TARGET_CPUS (target_cpus())
/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(1UL << smp_processor_id());
	apic_write(APIC_LDR, val);
}

static inline int apic_id_registered(void)
{
	return physid_isset(GET_APIC_ID(read_apic_id()), phys_cpu_present_map);
}

static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	return cpus_addr(cpumask)[0];
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static inline void setup_apic_routing(void)
{
#ifdef CONFIG_X86_IO_APIC
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
					"Flat", nr_ioapics);
#endif
}

static inline int apicid_to_node(int logical_apicid)
{
#ifdef CONFIG_SMP
	return apicid_2_node[hard_smp_processor_id()];
#else
	return 0;
#endif
}
#endif

static inline unsigned long check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return physid_isset(apicid, bitmap);
}

static inline unsigned long check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	return phys_map;
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
	return 1 << cpu;
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS && cpu_present(mps_cpu))
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return physid_isset(boot_cpu_physical_apicid, phys_cpu_present_map);
}

static inline void enable_apic_mode(void)
{
}

#endif /* CONFIG_X86_LOCAL_APIC */
#endif /* ASM_X86__MACH_DEFAULT__MACH_APIC_H */
