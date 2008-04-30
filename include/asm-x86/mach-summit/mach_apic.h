#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <asm/smp.h>

#define esr_disable (1)
#define NO_BALANCE_IRQ (0)

/* In clustered mode, the high nibble of APIC ID is a cluster number.
 * The low nibble is a 4-bit bitmap. */
#define XAPIC_DEST_CPUS_SHIFT	4
#define XAPIC_DEST_CPUS_MASK	((1u << XAPIC_DEST_CPUS_SHIFT) - 1)
#define XAPIC_DEST_CLUSTER_MASK	(XAPIC_DEST_CPUS_MASK << XAPIC_DEST_CPUS_SHIFT)

#define APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

static inline cpumask_t target_cpus(void)
{
	/* CPU_MASK_ALL (0xff) has undefined behaviour with
	 * dest_LowestPrio mode logical clustered apic interrupt routing
	 * Just start on cpu 0.  IRQ balancing will spread load
	 */
	return cpumask_of_cpu(0);
} 
#define TARGET_CPUS	(target_cpus())

#define INT_DELIVERY_MODE (dest_LowestPrio)
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

#define apicid_cluster(apicid) ((apicid) & XAPIC_DEST_CLUSTER_MASK)

extern u8 cpu_2_logical_apicid[];

static inline void init_apic_ldr(void)
{
	unsigned long val, id;
	int count = 0;
	u8 my_id = (u8)hard_smp_processor_id();
	u8 my_cluster = (u8)apicid_cluster(my_id);
#ifdef CONFIG_SMP
	u8 lid;
	int i;

	/* Create logical APIC IDs by counting CPUs already in cluster. */
	for (count = 0, i = NR_CPUS; --i >= 0; ) {
		lid = cpu_2_logical_apicid[i];
		if (lid != BAD_APICID && apicid_cluster(lid) == my_cluster)
			++count;
	}
#endif
	/* We only have a 4 wide bitmap in cluster mode.  If a deranged
	 * BIOS puts 5 CPUs in one APIC cluster, we're hosed. */
	BUG_ON(count >= XAPIC_DEST_CPUS_SHIFT);
	id = my_cluster | (1UL << count);
	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write_around(APIC_LDR, val);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apic_id_registered(void)
{
	return 1;
}

static inline void setup_apic_routing(void)
{
	printk("Enabling APIC mode:  Summit.  Using %d I/O APICs\n",
						nr_ioapics);
}

static inline int apicid_to_node(int logical_apicid)
{
#ifdef CONFIG_SMP
	return apicid_2_node[hard_smp_processor_id()];
#else
	return 0;
#endif
}

/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
       if (cpu >= NR_CPUS)
	       return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS)
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_id_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0x0F);
}

static inline physid_mask_t apicid_to_cpu_present(int apicid)
{
	return physid_mask_of_physid(0);
}

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return 1;
}

static inline void enable_apic_mode(void)
{
}

static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;	

	num_bits_set = cpus_weight(cpumask);
	/* Return id to all */
	if (num_bits_set == NR_CPUS)
		return (int) 0xFF;
	/* 
	 * The cpus in the mask must all be on the apic cluster.  If are not 
	 * on the same apicid cluster return default value of TARGET_CPUS. 
	 */
	cpu = first_cpu(cpumask);
	apicid = cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpu_isset(cpu, cpumask)) {
			int new_apicid = cpu_to_logical_apicid(cpu);
			if (apicid_cluster(apicid) != 
					apicid_cluster(new_apicid)){
				printk ("%s: Not a valid mask!\n",__FUNCTION__);
				return 0xFF;
			}
			apicid = apicid | new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

/* cpuid returns the value latched in the HW at reset, not the APIC ID
 * register's value.  For any box whose BIOS changes APIC IDs, like
 * clustered APIC systems, we must use hard_smp_processor_id.
 *
 * See Intel's IA-32 SW Dev's Manual Vol2 under CPUID.
 */
static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
