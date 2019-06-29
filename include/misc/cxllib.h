/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2017 IBM Corp.
 */

#ifndef _MISC_CXLLIB_H
#define _MISC_CXLLIB_H

#include <linux/pci.h>
#include <asm/reg.h>

/*
 * cxl driver exports a in-kernel 'library' API which can be called by
 * other drivers to help interacting with an IBM XSL.
 */

/*
 * tells whether capi is supported on the PCIe slot where the
 * device is seated
 *
 * Input:
 *	dev: device whose slot needs to be checked
 *	flags: 0 for the time being
 */
bool cxllib_slot_is_supported(struct pci_dev *dev, unsigned long flags);


/*
 * Returns the configuration parameters to be used by the XSL or device
 *
 * Input:
 *	dev: device, used to find PHB
 * Output:
 *	struct cxllib_xsl_config:
 *		version
 *		capi BAR address, i.e. 0x2000000000000-0x2FFFFFFFFFFFF
 *		capi BAR size
 *		data send control (XSL_DSNCTL)
 *		dummy read address (XSL_DRA)
 */
#define CXL_XSL_CONFIG_VERSION1		1
struct cxllib_xsl_config {
	u32	version;     /* format version for register encoding */
	u32	log_bar_size;/* log size of the capi_window */
	u64	bar_addr;    /* address of the start of capi window */
	u64	dsnctl;      /* matches definition of XSL_DSNCTL */
	u64	dra;         /* real address that can be used for dummy read */
};

int cxllib_get_xsl_config(struct pci_dev *dev, struct cxllib_xsl_config *cfg);


/*
 * Activate capi for the pci host bridge associated with the device.
 * Can be extended to deactivate once we know how to do it.
 * Device must be ready to accept messages from the CAPP unit and
 * respond accordingly (TLB invalidates, ...)
 *
 * PHB is switched to capi mode through calls to skiboot.
 * CAPP snooping is activated
 *
 * Input:
 *	dev: device whose PHB should switch mode
 *	mode: mode to switch to i.e. CAPI or PCI
 *	flags: options related to the mode
 */
enum cxllib_mode {
	CXL_MODE_CXL,
	CXL_MODE_PCI,
};

#define CXL_MODE_NO_DMA       0
#define CXL_MODE_DMA_TVT0     1
#define CXL_MODE_DMA_TVT1     2

int cxllib_switch_phb_mode(struct pci_dev *dev, enum cxllib_mode mode,
			unsigned long flags);


/*
 * Set the device for capi DMA.
 * Define its dma_ops and dma offset so that allocations will be using TVT#1
 *
 * Input:
 *	dev: device to set
 *	flags: options. CXL_MODE_DMA_TVT1 should be used
 */
int cxllib_set_device_dma(struct pci_dev *dev, unsigned long flags);


/*
 * Get the Process Element structure for the given thread
 *
 * Input:
 *    task: task_struct for the context of the translation
 *    translation_mode: whether addresses should be translated
 * Output:
 *    attr: attributes to fill up the Process Element structure from CAIA
 */
struct cxllib_pe_attributes {
	u64 sr;
	u32 lpid;
	u32 tid;
	u32 pid;
};
#define CXL_TRANSLATED_MODE 0
#define CXL_REAL_MODE 1

int cxllib_get_PE_attributes(struct task_struct *task,
	     unsigned long translation_mode, struct cxllib_pe_attributes *attr);


/*
 * Handle memory fault.
 * Fault in all the pages of the specified buffer for the permissions
 * provided in ‘flags’
 *
 * Shouldn't be called from interrupt context
 *
 * Input:
 *	mm: struct mm for the thread faulting the pages
 *	addr: base address of the buffer to page in
 *	size: size of the buffer to page in
 *	flags: permission requested (DSISR_ISSTORE...)
 */
int cxllib_handle_fault(struct mm_struct *mm, u64 addr, u64 size, u64 flags);


#endif /* _MISC_CXLLIB_H */
