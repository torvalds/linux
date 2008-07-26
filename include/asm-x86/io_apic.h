#ifndef ASM_X86__IO_APIC_H
#define ASM_X86__IO_APIC_H

#include <linux/types.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>

/*
 * Intel IO-APIC support for SMP and UP systems.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 Ingo Molnar
 */

/* I/O Unit Redirection Table */
#define IO_APIC_REDIR_VECTOR_MASK	0x000FF
#define IO_APIC_REDIR_DEST_LOGICAL	0x00800
#define IO_APIC_REDIR_DEST_PHYSICAL	0x00000
#define IO_APIC_REDIR_SEND_PENDING	(1 << 12)
#define IO_APIC_REDIR_REMOTE_IRR	(1 << 14)
#define IO_APIC_REDIR_LEVEL_TRIGGER	(1 << 15)
#define IO_APIC_REDIR_MASKED		(1 << 16)

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

#ifdef CONFIG_X86_32
	union {
		struct {
			__u32	__reserved_1	: 24,
				physical_dest	:  4,
				__reserved_2	:  4;
		} physical;

		struct {
			__u32	__reserved_1	: 24,
				logical_dest	:  8;
		} logical;
	} dest;
#else
	__u32	__reserved_3	: 24,
		dest		:  8;
#endif

} __attribute__ ((packed));

#ifdef CONFIG_X86_IO_APIC

/*
 * # of IO-APICs and # of IRQ routing registers
 */
extern int nr_ioapics;
extern int nr_ioapic_registers[MAX_IO_APICS];

/*
 * MP-BIOS irq configuration table structures:
 */

#define MP_MAX_IOAPIC_PIN 127

struct mp_config_ioapic {
	unsigned long mp_apicaddr;
	unsigned int mp_apicid;
	unsigned char mp_type;
	unsigned char mp_apicver;
	unsigned char mp_flags;
};

struct mp_config_intsrc {
	unsigned int mp_dstapic;
	unsigned char mp_type;
	unsigned char mp_irqtype;
	unsigned short mp_irqflag;
	unsigned char mp_srcbus;
	unsigned char mp_srcbusirq;
	unsigned char mp_dstirq;
};

/* I/O APIC entries */
extern struct mp_config_ioapic mp_ioapics[MAX_IO_APICS];

/* # of MP IRQ source entries */
extern int mp_irq_entries;

/* MP IRQ source entries */
extern struct mp_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* non-0 if default (table-less) MP configuration */
extern int mpc_default_type;

/* Older SiS APIC requires we rewrite the index register */
extern int sis_apic_bug;

/* 1 if "noapic" boot option passed */
extern int skip_ioapic_setup;

/* 1 if the timer IRQ uses the '8259A Virtual Wire' mode */
extern int timer_through_8259;

static inline void disable_ioapic_setup(void)
{
	skip_ioapic_setup = 1;
}

/*
 * If we use the IO-APIC for IRQ routing, disable automatic
 * assignment of PCI IRQ's.
 */
#define io_apic_assign_pci_irqs \
	(mp_irq_entries && !skip_ioapic_setup && io_apic_irqs)

#ifdef CONFIG_ACPI
extern int io_apic_get_unique_id(int ioapic, int apic_id);
extern int io_apic_get_version(int ioapic);
extern int io_apic_get_redir_entries(int ioapic);
extern int io_apic_set_pci_routing(int ioapic, int pin, int irq,
				   int edge_level, int active_high_low);
#endif /* CONFIG_ACPI */

extern int (*ioapic_renumber_irq)(int ioapic, int irq);
extern void ioapic_init_mappings(void);

#else  /* !CONFIG_X86_IO_APIC */
#define io_apic_assign_pci_irqs 0
static const int timer_through_8259 = 0;
static inline void ioapic_init_mappings(void) { }
#endif

#endif /* ASM_X86__IO_APIC_H */
