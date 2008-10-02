#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#define xapic_phys_to_log_apicid(cpu) per_cpu(x86_bios_cpu_apicid, cpu)
#define esr_disable (1)

static inline int apic_id_registered(void)
{
	        return (1);
}

static inline cpumask_t target_cpus(void)
{ 
#if defined CONFIG_ES7000_CLUSTERED_APIC
	return CPU_MASK_ALL;
#else
	return cpumask_of_cpu(smp_processor_id());
#endif
}
#define TARGET_CPUS	(target_cpus())

#if defined CONFIG_ES7000_CLUSTERED_APIC
#define APIC_DFR_VALUE		(APIC_DFR_CLUSTER)
#define INT_DELIVERY_MODE	(dest_LowestPrio)
#define INT_DEST_MODE		(1)    /* logical delivery broadcast to all procs */
#define NO_BALANCE_IRQ 		(1)
#undef  WAKE_SECONDARY_VIA_INIT
#define WAKE_SECONDARY_VIA_MIP
#else
#define APIC_DFR_VALUE		(APIC_DFR_FLAT)
#define INT_DELIVERY_MODE	(dest_Fixed)
#define INT_DEST_MODE		(0)    /* phys delivery to target procs */
#define NO_BALANCE_IRQ 		(0)
#undef  APIC_DEST_LOGICAL
#define APIC_DEST_LOGICAL	0x0
#define WAKE_SECONDARY_VIA_INIT
#endif

static inline unsigned long check_apicid_used(physid_mask_t bitmap, int apicid)
{ 
	return 0;
} 
static inline unsigned long check_apicid_present(int bit) 
{
	return physid_isset(bit, phys_cpu_present_map);
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline unsigned long calculate_ldr(int cpu)
{
	unsigned long id;
	id = xapic_phys_to_log_apicid(cpu);
	return (SET_APIC_LOGICAL_ID(id));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LdR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

#ifndef CONFIG_X86_GENERICARCH
extern void enable_apic_mode(void);
#endif

extern int apic_version [MAX_APICS];
static inline void setup_apic_routing(void)
{
	int apic = per_cpu(x86_bios_cpu_apicid, smp_processor_id());
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ? 
		"Physical Cluster" : "Logical Cluster", nr_ioapics, cpus_addr(TARGET_CPUS)[0]);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}


static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (!mps_cpu)
		return boot_cpu_physical_apicid;
	else if (mps_cpu < NR_CPUS)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	static int id = 0;
	physid_mask_t mask;
	mask = physid_mask_of_physid(id);
	++id;
	return mask;
}

extern u8 cpu_2_logical_apicid[];
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

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xff);
}


static inline void setup_portio_remap(void)
{
}

extern unsigned int boot_cpu_physical_apicid;
static inline int check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = GET_APIC_ID(read_apic_id());
	return (1);
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
#if defined CONFIG_ES7000_CLUSTERED_APIC
		return 0xFF;
#else
		return cpu_to_logical_apicid(0);
#endif
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
#if defined CONFIG_ES7000_CLUSTERED_APIC
				return 0xFF;
#else
				return cpu_to_logical_apicid(0);
#endif
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
