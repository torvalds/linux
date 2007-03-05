#ifndef __ASM_IO_APIC_H
#define __ASM_IO_APIC_H

#include <asm/types.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>

/*
 * Intel IO-APIC support for SMP and UP systems.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 Ingo Molnar
 */

#define APIC_MISMATCH_DEBUG

/*
 * The structure of the IO-APIC:
 */
union IO_APIC_reg_00 {
	u32	raw;
	struct {
		u32	__reserved_2	: 14,
			LTS		:  1,
			delivery_type	:  1,
			__reserved_1	:  8,
			ID		:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_01 {
	u32	raw;
	struct {
		u32	version		:  8,
		__reserved_2	:  7,
		PRQ		:  1,
		entries		:  8,
		__reserved_1	:  8;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_02 {
	u32	raw;
	struct {
		u32	__reserved_2	: 24,
		arbitration	:  4,
		__reserved_1	:  4;
	} __attribute__ ((packed)) bits;
};

union IO_APIC_reg_03 {
	u32	raw;
	struct {
		u32	boot_DT		:  1,
			__reserved_1	: 31;
	} __attribute__ ((packed)) bits;
};

/*
 * # of IO-APICs and # of IRQ routing registers
 */
extern int nr_ioapics;
extern int nr_ioapic_registers[MAX_IO_APICS];

enum ioapic_irq_destination_types {
	dest_Fixed = 0,
	dest_LowestPrio = 1,
	dest_SMI = 2,
	dest__reserved_1 = 3,
	dest_NMI = 4,
	dest_INIT = 5,
	dest__reserved_2 = 6,
	dest_ExtINT = 7
};

struct IO_APIC_route_entry {
	__u32	vector		:  8,
		delivery_mode	:  3,	/* 000: FIXED
					 * 001: lowest prio
					 * 111: ExtINT
					 */
		dest_mode	:  1,	/* 0: physical, 1: logical */
		delivery_status	:  1,
		polarity	:  1,
		irr		:  1,
		trigger		:  1,	/* 0: edge, 1: level */
		mask		:  1,	/* 0: enabled, 1: disabled */
		__reserved_2	: 15;

	__u32	__reserved_3	: 24,
		dest		:  8;
} __attribute__ ((packed));

/*
 * MP-BIOS irq configuration table structures:
 */

/* I/O APIC entries */
extern struct mpc_config_ioapic mp_ioapics[MAX_IO_APICS];

/* # of MP IRQ source entries */
extern int mp_irq_entries;

/* MP IRQ source entries */
extern struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* non-0 if default (table-less) MP configuration */
extern int mpc_default_type;

/* 1 if "noapic" boot option passed */
extern int skip_ioapic_setup;

/*
 * If we use the IO-APIC for IRQ routing, disable automatic
 * assignment of PCI IRQ's.
 */
#define io_apic_assign_pci_irqs (mp_irq_entries && !skip_ioapic_setup && io_apic_irqs)

#ifdef CONFIG_ACPI
extern int io_apic_get_version (int ioapic);
extern int io_apic_get_redir_entries (int ioapic);
extern int io_apic_set_pci_routing (int ioapic, int pin, int irq, int, int);
#endif

extern int sis_apic_bug; /* dummy */ 

void enable_NMI_through_LVT0 (void * dummy);

extern spinlock_t i8259A_lock;

#endif
