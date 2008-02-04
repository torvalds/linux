#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <asm/io.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>

#define APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

static inline cpumask_t target_cpus(void)
{
	return CPU_MASK_ALL;
}

#define TARGET_CPUS (target_cpus())

#define NO_BALANCE_IRQ (1)
#define esr_disable (1)

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 0     /* physical delivery on LOCAL quad */
 
#define check_apicid_used(bitmap, apicid) physid_isset(apicid, bitmap)
#define check_apicid_present(bit) physid_isset(bit, phys_cpu_present_map)
#define apicid_cluster(apicid) (apicid & 0xF0)

static inline int apic_id_registered(void)
{
	return 1;
}

static inline void init_apic_ldr(void)
{
	/* Already done in NUMA-Q firmware */
}

static inline void setup_apic_routing(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"NUMA-Q", nr_ioapics);
}

/*
 * Skip adding the timer int on secondary nodes, which causes
 * a small but painful rift in the time-space continuum.
 */
static inline int multi_timer_check(int apic, int irq)
{
	return apic != 0 && irq == 0;
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* We don't have a good way to do this yet - hack */
	return physids_promote(0xFUL);
}

/* Mapping from cpu number to logical apicid */
extern u8 cpu_2_logical_apicid[];
static inline int cpu_to_logical_apicid(int cpu)
{
       if (cpu >= NR_CPUS)
	       return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
}

/*
 * Supporting over 60 cpus on NUMA-Q requires a locality-dependent
 * cpu to APIC ID relation to properly interact with the intelligent
 * mode of the cluster controller.
 */
static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < 60)
		return ((mps_cpu >> 2) << 4) | (1 << (mps_cpu & 0x3));
	else
		return BAD_APICID;
}

static inline int generate_logical_apicid(int quad, int phys_apicid)
{
	return (quad << 4) + (phys_apicid ? phys_apicid << 1 : 1);
}

static inline int apicid_to_node(int logical_apicid) 
{
	return logical_apicid >> 4;
}

static inline physid_mask_t apicid_to_cpu_present(int logical_apicid)
{
	int node = apicid_to_node(logical_apicid);
	int cpu = __ffs(logical_apicid & 0xf);

	return physid_mask_of_physid(cpu + 4*node);
}

static inline int mpc_apic_id(struct mpc_config_processor *m, 
			struct mpc_config_translation *translation_record)
{
	int quad = translation_record->trans_quad;
	int logical_apicid = generate_logical_apicid(quad, m->mpc_apicid);

	printk("Processor #%d %u:%u APIC version %d (quad %d, apic %d)\n",
	       m->mpc_apicid,
	       (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	       (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	       m->mpc_apicver, quad, logical_apicid);
	return logical_apicid;
}

extern void *xquad_portio;

static inline void setup_portio_remap(void)
{
	int num_quads = num_online_nodes();

	if (num_quads <= 1)
       		return;

	printk("Remapping cross-quad port I/O for %d quads\n", num_quads);
	xquad_portio = ioremap(XQUAD_PORTIO_BASE, num_quads*XQUAD_PORTIO_QUAD);
	printk("xquad_portio vaddr 0x%08lx, len %08lx\n",
		(u_long) xquad_portio, (u_long) num_quads*XQUAD_PORTIO_QUAD);
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

static inline void enable_apic_mode(void)
{
}

/*
 * We use physical apicids here, not logical, so just return the default
 * physical broadcast to stop people from breaking us
 */
static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	return (int) 0xF;
}

/* No NUMA-Q box has a HT CPU, but it can't hurt to use the default code. */
static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
