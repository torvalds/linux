/*
 * acpi.h - ACPI Interface
 *
 * Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/config.h>

#ifdef	CONFIG_ACPI

#ifndef _LINUX
#define _LINUX
#endif

#include <linux/list.h>

#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <asm/acpi.h>


#ifdef CONFIG_ACPI

enum acpi_irq_model_id {
	ACPI_IRQ_MODEL_PIC = 0,
	ACPI_IRQ_MODEL_IOAPIC,
	ACPI_IRQ_MODEL_IOSAPIC,
	ACPI_IRQ_MODEL_COUNT
};

extern enum acpi_irq_model_id	acpi_irq_model;


/* Root System Description Pointer (RSDP) */

struct acpi_table_rsdp {
	char			signature[8];
	u8			checksum;
	char			oem_id[6];
	u8			revision;
	u32			rsdt_address;
} __attribute__ ((packed));

struct acpi20_table_rsdp {
	char			signature[8];
	u8			checksum;
	char			oem_id[6];
	u8			revision;
	u32			rsdt_address;
	u32			length;
	u64			xsdt_address;
	u8			ext_checksum;
	u8			reserved[3];
} __attribute__ ((packed));

typedef struct {
	u8			type;
	u8			length;
} __attribute__ ((packed)) acpi_table_entry_header;

/* Root System Description Table (RSDT) */

struct acpi_table_rsdt {
	struct acpi_table_header header;
	u32			entry[8];
} __attribute__ ((packed));

/* Extended System Description Table (XSDT) */

struct acpi_table_xsdt {
	struct acpi_table_header header;
	u64			entry[1];
} __attribute__ ((packed));

/* Fixed ACPI Description Table (FADT) */

struct acpi_table_fadt {
	struct acpi_table_header header;
	u32 facs_addr;
	u32 dsdt_addr;
	/* ... */
} __attribute__ ((packed));

/* Multiple APIC Description Table (MADT) */

struct acpi_table_madt {
	struct acpi_table_header header;
	u32			lapic_address;
	struct {
		u32			pcat_compat:1;
		u32			reserved:31;
	}			flags;
} __attribute__ ((packed));

enum acpi_madt_entry_id {
	ACPI_MADT_LAPIC = 0,
	ACPI_MADT_IOAPIC,
	ACPI_MADT_INT_SRC_OVR,
	ACPI_MADT_NMI_SRC,
	ACPI_MADT_LAPIC_NMI,
	ACPI_MADT_LAPIC_ADDR_OVR,
	ACPI_MADT_IOSAPIC,
	ACPI_MADT_LSAPIC,
	ACPI_MADT_PLAT_INT_SRC,
	ACPI_MADT_ENTRY_COUNT
};

typedef struct {
	u16			polarity:2;
	u16			trigger:2;
	u16			reserved:12;
} __attribute__ ((packed)) acpi_interrupt_flags;

struct acpi_table_lapic {
	acpi_table_entry_header	header;
	u8			acpi_id;
	u8			id;
	struct {
		u32			enabled:1;
		u32			reserved:31;
	}			flags;
} __attribute__ ((packed));

struct acpi_table_ioapic {
	acpi_table_entry_header	header;
	u8			id;
	u8			reserved;
	u32			address;
	u32			global_irq_base;
} __attribute__ ((packed));

struct acpi_table_int_src_ovr {
	acpi_table_entry_header	header;
	u8			bus;
	u8			bus_irq;
	u32			global_irq;
	acpi_interrupt_flags	flags;
} __attribute__ ((packed));

struct acpi_table_nmi_src {
	acpi_table_entry_header	header;
	acpi_interrupt_flags	flags;
	u32			global_irq;
} __attribute__ ((packed));

struct acpi_table_lapic_nmi {
	acpi_table_entry_header	header;
	u8			acpi_id;
	acpi_interrupt_flags	flags;
	u8			lint;
} __attribute__ ((packed));

struct acpi_table_lapic_addr_ovr {
	acpi_table_entry_header	header;
	u8			reserved[2];
	u64			address;
} __attribute__ ((packed));

struct acpi_table_iosapic {
	acpi_table_entry_header	header;
	u8			id;
	u8			reserved;
	u32			global_irq_base;
	u64			address;
} __attribute__ ((packed));

struct acpi_table_lsapic {
	acpi_table_entry_header	header;
	u8			acpi_id;
	u8			id;
	u8			eid;
	u8			reserved[3];
	struct {
		u32			enabled:1;
		u32			reserved:31;
	}			flags;
} __attribute__ ((packed));

struct acpi_table_plat_int_src {
	acpi_table_entry_header	header;
	acpi_interrupt_flags	flags;
	u8			type;	/* See acpi_interrupt_type */
	u8			id;
	u8			eid;
	u8			iosapic_vector;
	u32			global_irq;
	struct {
		u32			cpei_override_flag:1;
		u32			reserved:31;
	}			plint_flags;
} __attribute__ ((packed));

enum acpi_interrupt_id {
	ACPI_INTERRUPT_PMI	= 1,
	ACPI_INTERRUPT_INIT,
	ACPI_INTERRUPT_CPEI,
	ACPI_INTERRUPT_COUNT
};

#define	ACPI_SPACE_MEM		0

struct acpi_gen_regaddr {
	u8  space_id;
	u8  bit_width;
	u8  bit_offset;
	u8  resv;
	u32 addrl;
	u32 addrh;
} __attribute__ ((packed));

struct acpi_table_hpet {
	struct acpi_table_header header;
	u32 id;
	struct acpi_gen_regaddr addr;
	u8 number;
	u16 min_tick;
	u8 page_protect;
} __attribute__ ((packed));

/*
 * Simple Boot Flags
 * http://www.microsoft.com/whdc/hwdev/resources/specs/simp_bios.mspx
 */
struct acpi_table_sbf
{
	u8 sbf_signature[4];
	u32 sbf_len;
	u8 sbf_revision;
	u8 sbf_csum;
	u8 sbf_oemid[6];
	u8 sbf_oemtable[8];
	u8 sbf_revdata[4];
	u8 sbf_creator[4];
	u8 sbf_crearev[4];
	u8 sbf_cmos;
	u8 sbf_spare[3];
} __attribute__ ((packed));

/*
 * System Resource Affinity Table (SRAT)
 * http://www.microsoft.com/whdc/hwdev/platform/proc/SRAT.mspx
 */

struct acpi_table_srat {
	struct acpi_table_header header;
	u32			table_revision;
	u64			reserved;
} __attribute__ ((packed));

enum acpi_srat_entry_id {
	ACPI_SRAT_PROCESSOR_AFFINITY = 0,
	ACPI_SRAT_MEMORY_AFFINITY,
	ACPI_SRAT_ENTRY_COUNT
};

struct acpi_table_processor_affinity {
	acpi_table_entry_header	header;
	u8			proximity_domain;
	u8			apic_id;
	struct {
		u32			enabled:1;
		u32			reserved:31;
	}			flags;
	u8			lsapic_eid;
	u8			reserved[7];
} __attribute__ ((packed));

struct acpi_table_memory_affinity {
	acpi_table_entry_header	header;
	u8			proximity_domain;
	u8			reserved1[5];
	u32			base_addr_lo;
	u32			base_addr_hi;
	u32			length_lo;
	u32			length_hi;
	u32			memory_type;	/* See acpi_address_range_id */
	struct {
		u32			enabled:1;
		u32			hot_pluggable:1;
		u32			reserved:30;
	}			flags;
	u64			reserved2;
} __attribute__ ((packed));

enum acpi_address_range_id {
	ACPI_ADDRESS_RANGE_MEMORY = 1,
	ACPI_ADDRESS_RANGE_RESERVED = 2,
	ACPI_ADDRESS_RANGE_ACPI = 3,
	ACPI_ADDRESS_RANGE_NVS	= 4,
	ACPI_ADDRESS_RANGE_COUNT
};

/*
 * System Locality Information Table (SLIT)
 *   see http://devresource.hp.com/devresource/docs/techpapers/ia64/slit.pdf
 */

struct acpi_table_slit {
	struct acpi_table_header header;
	u64			localities;
	u8			entry[1];	/* real size = localities^2 */
} __attribute__ ((packed));

/* Smart Battery Description Table (SBST) */

struct acpi_table_sbst {
	struct acpi_table_header header;
	u32			warning;	/* Warn user */
	u32			low;		/* Critical sleep */
	u32			critical;	/* Critical shutdown */
} __attribute__ ((packed));

/* Embedded Controller Boot Resources Table (ECDT) */

struct acpi_table_ecdt {
	struct acpi_table_header 	header;
	struct acpi_generic_address	ec_control;
	struct acpi_generic_address	ec_data;
	u32				uid;
	u8				gpe_bit;
	char				ec_id[0];
} __attribute__ ((packed));

/* PCI MMCONFIG */

/* Defined in PCI Firmware Specification 3.0 */
struct acpi_table_mcfg_config {
	u32				base_address;
	u32				base_reserved;
	u16				pci_segment_group_number;
	u8				start_bus_number;
	u8				end_bus_number;
	u8				reserved[4];
} __attribute__ ((packed));
struct acpi_table_mcfg {
	struct acpi_table_header	header;
	u8				reserved[8];
	struct acpi_table_mcfg_config	config[0];
} __attribute__ ((packed));

/* Table Handlers */

enum acpi_table_id {
	ACPI_TABLE_UNKNOWN = 0,
	ACPI_APIC,
	ACPI_BOOT,
	ACPI_DBGP,
	ACPI_DSDT,
	ACPI_ECDT,
	ACPI_ETDT,
	ACPI_FADT,
	ACPI_FACS,
	ACPI_OEMX,
	ACPI_PSDT,
	ACPI_SBST,
	ACPI_SLIT,
	ACPI_SPCR,
	ACPI_SRAT,
	ACPI_SSDT,
	ACPI_SPMI,
	ACPI_HPET,
	ACPI_MCFG,
	ACPI_TABLE_COUNT
};

typedef int (*acpi_table_handler) (unsigned long phys_addr, unsigned long size);

extern acpi_table_handler acpi_table_ops[ACPI_TABLE_COUNT];

typedef int (*acpi_madt_entry_handler) (acpi_table_entry_header *header, const unsigned long end);

char * __acpi_map_table (unsigned long phys_addr, unsigned long size);
unsigned long acpi_find_rsdp (void);
int acpi_boot_init (void);
int acpi_boot_table_init (void);
int acpi_numa_init (void);

int acpi_table_init (void);
int acpi_table_parse (enum acpi_table_id id, acpi_table_handler handler);
int acpi_get_table_header_early (enum acpi_table_id id, struct acpi_table_header **header);
int acpi_table_parse_madt (enum acpi_madt_entry_id id, acpi_madt_entry_handler handler, unsigned int max_entries);
int acpi_table_parse_srat (enum acpi_srat_entry_id id, acpi_madt_entry_handler handler, unsigned int max_entries);
int acpi_parse_mcfg (unsigned long phys_addr, unsigned long size);
void acpi_table_print (struct acpi_table_header *header, unsigned long phys_addr);
void acpi_table_print_madt_entry (acpi_table_entry_header *madt);
void acpi_table_print_srat_entry (acpi_table_entry_header *srat);

/* the following four functions are architecture-dependent */
void acpi_numa_slit_init (struct acpi_table_slit *slit);
void acpi_numa_processor_affinity_init (struct acpi_table_processor_affinity *pa);
void acpi_numa_memory_affinity_init (struct acpi_table_memory_affinity *ma);
void acpi_numa_arch_fixup(void);

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/* Arch dependent functions for cpu hotplug support */
int acpi_map_lsapic(acpi_handle handle, int *pcpu);
int acpi_unmap_lsapic(int cpu);
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base);
int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base);

extern int acpi_mp_config;

extern struct acpi_table_mcfg_config *pci_mmcfg_config;
extern int pci_mmcfg_config_num;

extern int sbf_port ;

#else	/* !CONFIG_ACPI */

#define acpi_mp_config	0

#endif 	/* !CONFIG_ACPI */

int acpi_register_gsi (u32 gsi, int edge_level, int active_high_low);
int acpi_gsi_to_irq (u32 gsi, unsigned int *irq);

/*
 * This function undoes the effect of one call to acpi_register_gsi().
 * If this matches the last registration, any IRQ resources for gsi
 * are freed.
 */
void acpi_unregister_gsi (u32 gsi);

#ifdef CONFIG_ACPI

struct acpi_prt_entry {
	struct list_head	node;
	struct acpi_pci_id	id;
	u8			pin;
	struct {
		acpi_handle		handle;
		u32			index;
	}			link;
	u32			irq;
};

struct acpi_prt_list {
	int			count;
	struct list_head	entries;
};

struct pci_dev;

int acpi_pci_irq_enable (struct pci_dev *dev);
void acpi_penalize_isa_irq(int irq, int active);

void acpi_pci_irq_disable (struct pci_dev *dev);

struct acpi_pci_driver {
	struct acpi_pci_driver *next;
	int (*add)(acpi_handle handle);
	void (*remove)(acpi_handle handle);
};

int acpi_pci_register_driver(struct acpi_pci_driver *driver);
void acpi_pci_unregister_driver(struct acpi_pci_driver *driver);

#endif /* CONFIG_ACPI */

#ifdef CONFIG_ACPI_EC

extern int ec_read(u8 addr, u8 *val);
extern int ec_write(u8 addr, u8 val);

#endif /*CONFIG_ACPI_EC*/

extern int acpi_blacklisted(void);
extern void acpi_bios_year(char *s);

#define	ACPI_CSTATE_LIMIT_DEFINED	/* for driver builds */
#ifdef	CONFIG_ACPI

/*
 * Set highest legal C-state
 * 0: C0 okay, but not C1
 * 1: C1 okay, but not C2
 * 2: C2 okay, but not C3 etc.
 */

extern unsigned int max_cstate;

static inline unsigned int acpi_get_cstate_limit(void)
{
	return max_cstate;
}
static inline void acpi_set_cstate_limit(unsigned int new_limit)
{
	max_cstate = new_limit;
	return;
}
#else
static inline unsigned int acpi_get_cstate_limit(void) { return 0; }
static inline void acpi_set_cstate_limit(unsigned int new_limit) { return; }
#endif

#ifdef CONFIG_ACPI_NUMA
int acpi_get_pxm(acpi_handle handle);
#else
static inline int acpi_get_pxm(acpi_handle handle)
{
	return 0;
}
#endif

extern int pnpacpi_disabled;

#else	/* CONFIG_ACPI */

static inline int acpi_boot_init(void)
{
	return 0;
}

static inline int acpi_boot_table_init(void)
{
	return 0;
}

#endif	/* CONFIG_ACPI */
#endif	/*_LINUX_ACPI_H*/
