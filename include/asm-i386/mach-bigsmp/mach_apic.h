#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H
#include <asm/smp.h>

#define SEQUENTIAL_APICID
#ifdef SEQUENTIAL_APICID
#define xapic_phys_to_log_apicid(phys_apic) ( (1ul << ((phys_apic) & 0x3)) |\
		((phys_apic<<2) & (~0xf)) )
#elif CLUSTERED_APICID
#define xapic_phys_to_log_apicid(phys_apic) ( (1ul << ((phys_apic) & 0x3)) |\
		((phys_apic) & (~0xf)) )
#endif

#define NO_BALANCE_IRQ (1)
#define esr_disable (1)

#define NO_IOAPIC_CHECK (0)

static inline int apic_id_registered(void)
{
	return (1);
}

#define APIC_DFR_VALUE	(APIC_DFR_CLUSTER)
/* Round robin the irqs amoung the online cpus */
static inline cpumask_t target_cpus(void)
{ 
	static unsigned long cpu = NR_CPUS;
	do {
		if (cpu >= NR_CPUS)
			cpu = first_cpu(cpu_online_map);
		else
			cpu = next_cpu(cpu, cpu_online_map);
	} while (cpu >= NR_CPUS);
	return cpumask_of_cpu(cpu);
}
#define TARGET_CPUS	(target_cpus())

#define INT_DELIVERY_MODE dest_Fixed
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */

static inline unsigned long check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}

/* we don't use the phys_cpu_present_map to indicate apicid presence */
static inline unsigned long check_apicid_present(int bit) 
{
	return 1;
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline unsigned long calculate_ldr(unsigned long old)
{
	unsigned long id;
	id = xapic_phys_to_log_apicid(hard_smp_processor_id());
	return ((old & ~APIC_LDR_MASK) | SET_APIC_LOGICAL_ID(id));
}

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

	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val = calculate_ldr(val);
	apic_write_around(APIC_LDR, val);
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"Cluster", nr_ioapics);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}

extern u8 bios_cpu_apicid[];

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS)
		return (int)bios_cpu_apicid[mps_cpu];
	else
		return BAD_APICID;
}

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

extern u8 cpu_2_logical_apicid[];
/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
       if (cpu >= NR_CPUS)
	       return BAD_APICID;
       return (int)cpu_2_logical_apicid[cpu];
 }

static inline int mpc_apic_id(struct mpc_config_processor *m,
			struct mpc_config_translation *translation_record)
{
	printk("Processor #%d %ld:%ld APIC version %d\n",
	        m->mpc_apicid,
	        (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	        (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	        m->mpc_apicver);
	return m->mpc_apicid;
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xFUL);
}

#define WAKE_SECONDARY_VIA_INIT

static inline void setup_portio_remap(void)
{
}

static inline void enable_apic_mode(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

/* As we are using single CPU as destination, pick only one CPU here */
static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	int cpu;
	int apicid;	

	cpu = first_cpu(cpumask);
	apicid = cpu_to_logical_apicid(cpu);
	return apicid;
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
