#ifndef __ASM_MPSPEC_H
#define __ASM_MPSPEC_H

#include <linux/cpumask.h>
#include <asm/mpspec_def.h>
#include <mach_mpspec.h>

extern int mp_bus_id_to_type [MAX_MP_BUSSES];
extern int mp_bus_id_to_node [MAX_MP_BUSSES];
extern int mp_bus_id_to_local [MAX_MP_BUSSES];
extern int quad_local_to_mp_bus_id [NR_CPUS/4][4];
extern int mp_bus_id_to_pci_bus [MAX_MP_BUSSES];

extern unsigned int def_to_bigsmp;
extern unsigned int boot_cpu_physical_apicid;
extern int smp_found_config;
extern void find_smp_config (void);
extern void get_smp_config (void);
extern int nr_ioapics;
extern int apic_version [MAX_APICS];
extern int mp_irq_entries;
extern struct mpc_config_intsrc mp_irqs [MAX_IRQ_SOURCES];
extern int mpc_default_type;
extern unsigned long mp_lapic_addr;
extern int pic_mode;

#ifdef CONFIG_ACPI
extern void mp_register_lapic (u8 id, u8 enabled);
extern void mp_register_lapic_address (u64 address);
extern void mp_register_ioapic (u8 id, u32 address, u32 gsi_base);
extern void mp_override_legacy_irq (u8 bus_irq, u8 polarity, u8 trigger, u32 gsi);
extern void mp_config_acpi_legacy_irqs (void);
extern int mp_register_gsi (u32 gsi, int edge_level, int active_high_low);
#endif /* CONFIG_ACPI */

#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_APICS)

struct physid_mask
{
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

#define physid_set(physid, map)			set_bit(physid, (map).mask)
#define physid_clear(physid, map)		clear_bit(physid, (map).mask)
#define physid_isset(physid, map)		test_bit(physid, (map).mask)
#define physid_test_and_set(physid, map)	test_and_set_bit(physid, (map).mask)

#define physids_and(dst, src1, src2)		bitmap_and((dst).mask, (src1).mask, (src2).mask, MAX_APICS)
#define physids_or(dst, src1, src2)		bitmap_or((dst).mask, (src1).mask, (src2).mask, MAX_APICS)
#define physids_clear(map)			bitmap_zero((map).mask, MAX_APICS)
#define physids_complement(dst, src)		bitmap_complement((dst).mask,(src).mask, MAX_APICS)
#define physids_empty(map)			bitmap_empty((map).mask, MAX_APICS)
#define physids_equal(map1, map2)		bitmap_equal((map1).mask, (map2).mask, MAX_APICS)
#define physids_weight(map)			bitmap_weight((map).mask, MAX_APICS)
#define physids_shift_right(d, s, n)		bitmap_shift_right((d).mask, (s).mask, n, MAX_APICS)
#define physids_shift_left(d, s, n)		bitmap_shift_left((d).mask, (s).mask, n, MAX_APICS)
#define physids_coerce(map)			((map).mask[0])

#define physids_promote(physids)						\
	({									\
		physid_mask_t __physid_mask = PHYSID_MASK_NONE;			\
		__physid_mask.mask[0] = physids;				\
		__physid_mask;							\
	})

#define physid_mask_of_physid(physid)						\
	({									\
		physid_mask_t __physid_mask = PHYSID_MASK_NONE;			\
		physid_set(physid, __physid_mask);				\
		__physid_mask;							\
	})

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

extern physid_mask_t phys_cpu_present_map;

#endif

