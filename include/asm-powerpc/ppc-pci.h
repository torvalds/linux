/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_PPC_PCI_H
#define _ASM_POWERPC_PPC_PCI_H
#ifdef __KERNEL__

#ifdef CONFIG_PCI

#include <linux/pci.h>
#include <asm/pci-bridge.h>

extern unsigned long isa_io_base;

extern void pci_setup_phb_io(struct pci_controller *hose, int primary);
extern void pci_setup_phb_io_dynamic(struct pci_controller *hose, int primary);


extern struct list_head hose_list;
extern int global_phb_number;

extern void find_and_init_phbs(void);

extern struct pci_dev *isa_bridge_pcidev;	/* may be NULL if no ISA bus */

/** Bus Unit ID macros; get low and hi 32-bits of the 64-bit BUID */
#define BUID_HI(buid) ((buid) >> 32)
#define BUID_LO(buid) ((buid) & 0xffffffff)

/* PCI device_node operations */
struct device_node;
typedef void *(*traverse_func)(struct device_node *me, void *data);
void *traverse_pci_devices(struct device_node *start, traverse_func pre,
		void *data);

extern void pci_devs_phb_init(void);
extern void pci_devs_phb_init_dynamic(struct pci_controller *phb);
extern void scan_phb(struct pci_controller *hose);

/* From rtas_pci.h */
extern void init_pci_config_tokens (void);
extern unsigned long get_phb_buid (struct device_node *);
extern int rtas_setup_phb(struct pci_controller *phb);

/* From iSeries PCI */
extern void iSeries_pcibios_init(void);

extern unsigned long pci_probe_only;

/* ---- EEH internal-use-only related routines ---- */
#ifdef CONFIG_EEH

void pci_addr_cache_insert_device(struct pci_dev *dev);
void pci_addr_cache_remove_device(struct pci_dev *dev);
void pci_addr_cache_build(void);
struct pci_dev *pci_get_device_by_addr(unsigned long addr);

/**
 * eeh_slot_error_detail -- record and EEH error condition to the log
 * @pdn:      pci device node
 * @severity: EEH_LOG_TEMP_FAILURE or EEH_LOG_PERM_FAILURE
 *
 * Obtains the EEH error details from the RTAS subsystem,
 * and then logs these details with the RTAS error log system.
 */
#define EEH_LOG_TEMP_FAILURE 1
#define EEH_LOG_PERM_FAILURE 2
void eeh_slot_error_detail (struct pci_dn *pdn, int severity);

/**
 * rtas_pci_enable - enable IO transfers for this slot
 * @pdn:       pci device node
 * @function:  either EEH_THAW_MMIO or EEH_THAW_DMA 
 *
 * Enable I/O transfers to this slot 
 */
#define EEH_THAW_MMIO 2
#define EEH_THAW_DMA  3
int rtas_pci_enable(struct pci_dn *pdn, int function);

/**
 * rtas_set_slot_reset -- unfreeze a frozen slot
 * @pdn:       pci device node
 *
 * Clear the EEH-frozen condition on a slot.  This routine
 * does this by asserting the PCI #RST line for 1/8th of
 * a second; this routine will sleep while the adapter is
 * being reset.
 *
 * Returns a non-zero value if the reset failed.
 */
int rtas_set_slot_reset (struct pci_dn *);
int eeh_wait_for_slot_status(struct pci_dn *pdn, int max_wait_msecs);

/** 
 * eeh_restore_bars - Restore device configuration info.
 * @pdn:       pci device node
 *
 * A reset of a PCI device will clear out its config space.
 * This routines will restore the config space for this
 * device, and is children, to values previously obtained
 * from the firmware.
 */
void eeh_restore_bars(struct pci_dn *);

/**
 * rtas_configure_bridge -- firmware initialization of pci bridge
 * @pdn:       pci device node
 *
 * Ask the firmware to configure all PCI bridges devices
 * located behind the indicated node. Required after a
 * pci device reset. Does essentially the same hing as
 * eeh_restore_bars, but for brdges, and lets firmware 
 * do the work.
 */
void rtas_configure_bridge(struct pci_dn *);

int rtas_write_config(struct pci_dn *, int where, int size, u32 val);
int rtas_read_config(struct pci_dn *, int where, int size, u32 *val);

/**
 * eeh_mark_slot -- set mode flags for pertition endpoint
 * @pdn:       pci device node
 *
 * mark and clear slots: find "partition endpoint" PE and set or 
 * clear the flags for each subnode of the PE.
 */
void eeh_mark_slot (struct device_node *dn, int mode_flag);
void eeh_clear_slot (struct device_node *dn, int mode_flag);

/**
 * find_device_pe -- Find the associated "Partiationable Endpoint" PE
 * @pdn:       pci device node
 */
struct device_node * find_device_pe(struct device_node *dn);

void eeh_sysfs_add_device(struct pci_dev *pdev);
void eeh_sysfs_remove_device(struct pci_dev *pdev);

#endif /* CONFIG_EEH */

#else /* CONFIG_PCI */
static inline void find_and_init_phbs(void) { }
static inline void init_pci_config_tokens(void) { }
#endif /* !CONFIG_PCI */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PPC_PCI_H */
