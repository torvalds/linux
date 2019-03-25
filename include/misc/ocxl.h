// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#ifndef _MISC_OCXL_H_
#define _MISC_OCXL_H_

#include <linux/pci.h>

/*
 * Opencapi drivers all need some common facilities, like parsing the
 * device configuration space, adding a Process Element to the Shared
 * Process Area, etc...
 *
 * The ocxl module provides a kernel API, to allow other drivers to
 * reuse common code. A bit like a in-kernel library.
 */

#define OCXL_AFU_NAME_SZ      (24+1)  /* add 1 for NULL termination */

/*
 * The following 2 structures are a fairly generic way of representing
 * the configuration data for a function and AFU, as read from the
 * configuration space.
 */
struct ocxl_afu_config {
	u8 idx;
	int dvsec_afu_control_pos; /* offset of AFU control DVSEC */
	char name[OCXL_AFU_NAME_SZ];
	u8 version_major;
	u8 version_minor;
	u8 afuc_type;
	u8 afum_type;
	u8 profile;
	u8 global_mmio_bar;     /* global MMIO area */
	u64 global_mmio_offset;
	u32 global_mmio_size;
	u8 pp_mmio_bar;         /* per-process MMIO area */
	u64 pp_mmio_offset;
	u32 pp_mmio_stride;
	u8 log_mem_size;
	u8 pasid_supported_log;
	u16 actag_supported;
};

struct ocxl_fn_config {
	int dvsec_tl_pos;       /* offset of the Transaction Layer DVSEC */
	int dvsec_function_pos; /* offset of the Function DVSEC */
	int dvsec_afu_info_pos; /* offset of the AFU information DVSEC */
	s8 max_pasid_log;
	s8 max_afu_index;
};

/*
 * Read the configuration space of a function and fill in a
 * ocxl_fn_config structure with all the function details
 */
int ocxl_config_read_function(struct pci_dev *dev,
				struct ocxl_fn_config *fn);

/*
 * Read the configuration space of a function for the AFU specified by
 * the index 'afu_idx'. Fills in a ocxl_afu_config structure
 */
int ocxl_config_read_afu(struct pci_dev *dev,
				struct ocxl_fn_config *fn,
				struct ocxl_afu_config *afu,
				u8 afu_idx);

/*
 * Tell an AFU, by writing in the configuration space, the PASIDs that
 * it can use. Range starts at 'pasid_base' and its size is a multiple
 * of 2
 *
 * 'afu_control_offset' is the offset of the AFU control DVSEC which
 * can be found in the function configuration
 */
void ocxl_config_set_afu_pasid(struct pci_dev *dev,
				int afu_control_offset,
				int pasid_base, u32 pasid_count_log);

/*
 * Get the actag configuration for the function:
 * 'base' is the first actag value that can be used.
 * 'enabled' it the number of actags available, starting from base.
 * 'supported' is the total number of actags desired by all the AFUs
 *             of the function.
 */
int ocxl_config_get_actag_info(struct pci_dev *dev,
				u16 *base, u16 *enabled, u16 *supported);

/*
 * Tell a function, by writing in the configuration space, the actags
 * it can use.
 *
 * 'func_offset' is the offset of the Function DVSEC that can found in
 * the function configuration
 */
void ocxl_config_set_actag(struct pci_dev *dev, int func_offset,
				u32 actag_base, u32 actag_count);

/*
 * Tell an AFU, by writing in the configuration space, the actags it
 * can use.
 *
 * 'afu_control_offset' is the offset of the AFU control DVSEC for the
 * desired AFU. It can be found in the AFU configuration
 */
void ocxl_config_set_afu_actag(struct pci_dev *dev,
				int afu_control_offset,
				int actag_base, int actag_count);

/*
 * Enable/disable an AFU, by writing in the configuration space.
 *
 * 'afu_control_offset' is the offset of the AFU control DVSEC for the
 * desired AFU. It can be found in the AFU configuration
 */
void ocxl_config_set_afu_state(struct pci_dev *dev,
				int afu_control_offset, int enable);

/*
 * Set the Transaction Layer configuration in the configuration space.
 * Only needed for function 0.
 *
 * It queries the host TL capabilities, find some common ground
 * between the host and device, and set the Transaction Layer on both
 * accordingly.
 */
int ocxl_config_set_TL(struct pci_dev *dev, int tl_dvsec);

/*
 * Request an AFU to terminate a PASID.
 * Will return once the AFU has acked the request, or an error in case
 * of timeout.
 *
 * The hardware can only terminate one PASID at a time, so caller must
 * guarantee some kind of serialization.
 *
 * 'afu_control_offset' is the offset of the AFU control DVSEC for the
 * desired AFU. It can be found in the AFU configuration
 */
int ocxl_config_terminate_pasid(struct pci_dev *dev,
				int afu_control_offset, int pasid);

/*
 * Set up the opencapi link for the function.
 *
 * When called for the first time for a link, it sets up the Shared
 * Process Area for the link and the interrupt handler to process
 * translation faults.
 *
 * Returns a 'link handle' that should be used for further calls for
 * the link
 */
int ocxl_link_setup(struct pci_dev *dev, int PE_mask,
			void **link_handle);

/*
 * Remove the association between the function and its link.
 */
void ocxl_link_release(struct pci_dev *dev, void *link_handle);

/*
 * Add a Process Element to the Shared Process Area for a link.
 * The process is defined by its PASID, pid, tid and its mm_struct.
 *
 * 'xsl_err_cb' is an optional callback if the driver wants to be
 * notified when the translation fault interrupt handler detects an
 * address error.
 * 'xsl_err_data' is an argument passed to the above callback, if
 * defined
 */
int ocxl_link_add_pe(void *link_handle, int pasid, u32 pidr, u32 tidr,
		u64 amr, struct mm_struct *mm,
		void (*xsl_err_cb)(void *data, u64 addr, u64 dsisr),
		void *xsl_err_data);

/*
 * Remove a Process Element from the Shared Process Area for a link
 */
int ocxl_link_remove_pe(void *link_handle, int pasid);

/*
 * Allocate an AFU interrupt associated to the link.
 *
 * 'hw_irq' is the hardware interrupt number
 * 'obj_handle' is the 64-bit object handle to be passed to the AFU to
 * trigger the interrupt.
 * On P9, 'obj_handle' is an address, which, if written, triggers the
 * interrupt. It is an MMIO address which needs to be remapped (one
 * page).
 */
int ocxl_link_irq_alloc(void *link_handle, int *hw_irq,
			u64 *obj_handle);

/*
 * Free a previously allocated AFU interrupt
 */
void ocxl_link_free_irq(void *link_handle, int hw_irq);

#endif /* _MISC_OCXL_H_ */
