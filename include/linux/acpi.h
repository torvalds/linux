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


#ifdef	CONFIG_ACPI

#ifndef _LINUX
#define _LINUX
#endif

#include <linux/list.h>
#include <linux/mod_devicetable.h>

#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_numa.h>
#include <asm/acpi.h>


#ifdef CONFIG_ACPI

enum acpi_irq_model_id {
	ACPI_IRQ_MODEL_PIC = 0,
	ACPI_IRQ_MODEL_IOAPIC,
	ACPI_IRQ_MODEL_IOSAPIC,
	ACPI_IRQ_MODEL_PLATFORM,
	ACPI_IRQ_MODEL_COUNT
};

extern enum acpi_irq_model_id	acpi_irq_model;

enum acpi_interrupt_id {
	ACPI_INTERRUPT_PMI	= 1,
	ACPI_INTERRUPT_INIT,
	ACPI_INTERRUPT_CPEI,
	ACPI_INTERRUPT_COUNT
};

#define	ACPI_SPACE_MEM		0

enum acpi_address_range_id {
	ACPI_ADDRESS_RANGE_MEMORY = 1,
	ACPI_ADDRESS_RANGE_RESERVED = 2,
	ACPI_ADDRESS_RANGE_ACPI = 3,
	ACPI_ADDRESS_RANGE_NVS	= 4,
	ACPI_ADDRESS_RANGE_COUNT
};


/* Table Handlers */

typedef int (*acpi_table_handler) (struct acpi_table_header *table);

typedef int (*acpi_table_entry_handler) (struct acpi_subtable_header *header, const unsigned long end);

char * __acpi_map_table (unsigned long phys_addr, unsigned long size);
unsigned long acpi_find_rsdp (void);
int acpi_boot_init (void);
int acpi_boot_table_init (void);
int acpi_numa_init (void);

int acpi_table_init (void);
int acpi_table_parse (char *id, acpi_table_handler handler);
int __init acpi_table_parse_entries(char *id, unsigned long table_size,
	int entry_id, acpi_table_entry_handler handler, unsigned int max_entries);
int acpi_table_parse_madt (enum acpi_madt_type id, acpi_table_entry_handler handler, unsigned int max_entries);
int acpi_parse_mcfg (struct acpi_table_header *header);
void acpi_table_print_madt_entry (struct acpi_subtable_header *madt);

/* the following four functions are architecture-dependent */
#ifdef CONFIG_HAVE_ARCH_PARSE_SRAT
#define NR_NODE_MEMBLKS MAX_NUMNODES
#define acpi_numa_slit_init(slit) do {} while (0)
#define acpi_numa_processor_affinity_init(pa) do {} while (0)
#define acpi_numa_memory_affinity_init(ma) do {} while (0)
#define acpi_numa_arch_fixup() do {} while (0)
#else
void acpi_numa_slit_init (struct acpi_table_slit *slit);
void acpi_numa_processor_affinity_init (struct acpi_srat_cpu_affinity *pa);
void acpi_numa_memory_affinity_init (struct acpi_srat_mem_affinity *ma);
void acpi_numa_arch_fixup(void);
#endif

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/* Arch dependent functions for cpu hotplug support */
int acpi_map_lsapic(acpi_handle handle, int *pcpu);
int acpi_unmap_lsapic(int cpu);
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base);
int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base);

extern int acpi_mp_config;

extern struct acpi_mcfg_allocation *pci_mmcfg_config;
extern int pci_mmcfg_config_num;

extern int sbf_port;
extern unsigned long acpi_realmode_flags;

#else	/* !CONFIG_ACPI */

#define acpi_mp_config	0

#endif 	/* !CONFIG_ACPI */

int acpi_register_gsi (u32 gsi, int triggering, int polarity);
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
extern int ec_transaction(u8 command,
                          const u8 *wdata, unsigned wdata_len,
                          u8 *rdata, unsigned rdata_len,
			  int force_poll);

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
int acpi_get_node(acpi_handle *handle);
#else
static inline int acpi_get_pxm(acpi_handle handle)
{
	return 0;
}
static inline int acpi_get_node(acpi_handle *handle)
{
	return 0;
}
#endif
extern int acpi_paddr_to_node(u64 start_addr, u64 size);

extern int pnpacpi_disabled;

#define PXM_INVAL	(-1)
#define NID_INVAL	(-1)

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
