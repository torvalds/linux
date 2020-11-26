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
	u64 lpc_mem_offset;
	u64 lpc_mem_size;
	u64 special_purpose_mem_offset;
	u64 special_purpose_mem_size;
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

enum ocxl_endian {
	OCXL_BIG_ENDIAN = 0,    /**< AFU data is big-endian */
	OCXL_LITTLE_ENDIAN = 1, /**< AFU data is little-endian */
	OCXL_HOST_ENDIAN = 2,   /**< AFU data is the same endianness as the host */
};

// These are opaque outside the ocxl driver
struct ocxl_afu;
struct ocxl_fn;
struct ocxl_context;

// Device detection & initialisation

/**
 * ocxl_function_open() - Open an OpenCAPI function on an OpenCAPI device
 * @dev: The PCI device that contains the function
 *
 * Returns an opaque pointer to the function, or an error pointer (check with IS_ERR)
 */
struct ocxl_fn *ocxl_function_open(struct pci_dev *dev);

/**
 * ocxl_function_afu_list() - Get the list of AFUs associated with a PCI function device
 * Returns a list of struct ocxl_afu *
 *
 * @fn: The OpenCAPI function containing the AFUs
 */
struct list_head *ocxl_function_afu_list(struct ocxl_fn *fn);

/**
 * ocxl_function_fetch_afu() - Fetch an AFU instance from an OpenCAPI function
 * @fn: The OpenCAPI function to get the AFU from
 * @afu_idx: The index of the AFU to get
 *
 * If successful, the AFU should be released with ocxl_afu_put()
 *
 * Returns a pointer to the AFU, or NULL on error
 */
struct ocxl_afu *ocxl_function_fetch_afu(struct ocxl_fn *fn, u8 afu_idx);

/**
 * ocxl_afu_get() - Take a reference to an AFU
 * @afu: The AFU to increment the reference count on
 */
void ocxl_afu_get(struct ocxl_afu *afu);

/**
 * ocxl_afu_put() - Release a reference to an AFU
 * @afu: The AFU to decrement the reference count on
 */
void ocxl_afu_put(struct ocxl_afu *afu);


/**
 * ocxl_function_config() - Get the configuration information for an OpenCAPI function
 * @fn: The OpenCAPI function to get the config for
 *
 * Returns the function config, or NULL on error
 */
const struct ocxl_fn_config *ocxl_function_config(struct ocxl_fn *fn);

/**
 * ocxl_function_close() - Close an OpenCAPI function
 * This will free any AFUs previously retrieved from the function, and
 * detach and associated contexts. The contexts must by freed by the caller.
 *
 * @fn: The OpenCAPI function to close
 *
 */
void ocxl_function_close(struct ocxl_fn *fn);

// Context allocation

/**
 * ocxl_context_alloc() - Allocate an OpenCAPI context
 * @context: The OpenCAPI context to allocate, must be freed with ocxl_context_free
 * @afu: The AFU the context belongs to
 * @mapping: The mapping to unmap when the context is closed (may be NULL)
 */
int ocxl_context_alloc(struct ocxl_context **context, struct ocxl_afu *afu,
			struct address_space *mapping);

/**
 * ocxl_context_free() - Free an OpenCAPI context
 * @ctx: The OpenCAPI context to free
 */
void ocxl_context_free(struct ocxl_context *ctx);

/**
 * ocxl_context_attach() - Grant access to an MM to an OpenCAPI context
 * @ctx: The OpenCAPI context to attach
 * @amr: The value of the AMR register to restrict access
 * @mm: The mm to attach to the context
 *
 * Returns 0 on success, negative on failure
 */
int ocxl_context_attach(struct ocxl_context *ctx, u64 amr,
				struct mm_struct *mm);

/**
 * ocxl_context_detach() - Detach an MM from an OpenCAPI context
 * @ctx: The OpenCAPI context to attach
 *
 * Returns 0 on success, negative on failure
 */
int ocxl_context_detach(struct ocxl_context *ctx);

// AFU IRQs

/**
 * ocxl_afu_irq_alloc() - Allocate an IRQ associated with an AFU context
 * @ctx: the AFU context
 * @irq_id: out, the IRQ ID
 *
 * Returns 0 on success, negative on failure
 */
int ocxl_afu_irq_alloc(struct ocxl_context *ctx, int *irq_id);

/**
 * ocxl_afu_irq_free() - Frees an IRQ associated with an AFU context
 * @ctx: the AFU context
 * @irq_id: the IRQ ID
 *
 * Returns 0 on success, negative on failure
 */
int ocxl_afu_irq_free(struct ocxl_context *ctx, int irq_id);

/**
 * ocxl_afu_irq_get_addr() - Gets the address of the trigger page for an IRQ
 * This can then be provided to an AFU which will write to that
 * page to trigger the IRQ.
 * @ctx: The AFU context that the IRQ is associated with
 * @irq_id: The IRQ ID
 *
 * returns the trigger page address, or 0 if the IRQ is not valid
 */
u64 ocxl_afu_irq_get_addr(struct ocxl_context *ctx, int irq_id);

/**
 * ocxl_irq_set_handler() - Provide a callback to be called when an IRQ is triggered
 * @ctx: The AFU context that the IRQ is associated with
 * @irq_id: The IRQ ID
 * @handler: the callback to be called when the IRQ is triggered
 * @free_private: the callback to be called when the IRQ is freed (may be NULL)
 * @private: Private data to be passed to the callbacks
 *
 * Returns 0 on success, negative on failure
 */
int ocxl_irq_set_handler(struct ocxl_context *ctx, int irq_id,
		irqreturn_t (*handler)(void *private),
		void (*free_private)(void *private),
		void *private);

// AFU Metadata

/**
 * ocxl_afu_config() - Get a pointer to the config for an AFU
 * @afu: a pointer to the AFU to get the config for
 *
 * Returns a pointer to the AFU config
 */
struct ocxl_afu_config *ocxl_afu_config(struct ocxl_afu *afu);

/**
 * ocxl_afu_set_private() - Assign opaque hardware specific information to an OpenCAPI AFU.
 * @afu: The OpenCAPI AFU
 * @private: the opaque hardware specific information to assign to the driver
 */
void ocxl_afu_set_private(struct ocxl_afu *afu, void *private);

/**
 * ocxl_afu_get_private() - Fetch the hardware specific information associated with
 * an external OpenCAPI AFU. This may be consumed by an external OpenCAPI driver.
 * @afu: The OpenCAPI AFU
 *
 * Returns the opaque pointer associated with the device, or NULL if not set
 */
void *ocxl_afu_get_private(struct ocxl_afu *afu);

// Global MMIO
/**
 * ocxl_global_mmio_read32() - Read a 32 bit value from global MMIO
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @val: returns the value
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_read32(struct ocxl_afu *afu, size_t offset,
			    enum ocxl_endian endian, u32 *val);

/**
 * ocxl_global_mmio_read64() - Read a 64 bit value from global MMIO
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @val: returns the value
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_read64(struct ocxl_afu *afu, size_t offset,
			    enum ocxl_endian endian, u64 *val);

/**
 * ocxl_global_mmio_write32() - Write a 32 bit value to global MMIO
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @val: The value to write
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_write32(struct ocxl_afu *afu, size_t offset,
			     enum ocxl_endian endian, u32 val);

/**
 * ocxl_global_mmio_write64() - Write a 64 bit value to global MMIO
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @val: The value to write
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_write64(struct ocxl_afu *afu, size_t offset,
			     enum ocxl_endian endian, u64 val);

/**
 * ocxl_global_mmio_set32() - Set bits in a 32 bit global MMIO register
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @mask: a mask of the bits to set
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_set32(struct ocxl_afu *afu, size_t offset,
			   enum ocxl_endian endian, u32 mask);

/**
 * ocxl_global_mmio_set64() - Set bits in a 64 bit global MMIO register
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @mask: a mask of the bits to set
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_set64(struct ocxl_afu *afu, size_t offset,
			   enum ocxl_endian endian, u64 mask);

/**
 * ocxl_global_mmio_clear32() - Set bits in a 32 bit global MMIO register
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @mask: a mask of the bits to set
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_clear32(struct ocxl_afu *afu, size_t offset,
			     enum ocxl_endian endian, u32 mask);

/**
 * ocxl_global_mmio_clear64() - Set bits in a 64 bit global MMIO register
 * @afu: The AFU
 * @offset: The Offset from the start of MMIO
 * @endian: the endianness that the MMIO data is in
 * @mask: a mask of the bits to set
 *
 * Returns 0 for success, negative on error
 */
int ocxl_global_mmio_clear64(struct ocxl_afu *afu, size_t offset,
			     enum ocxl_endian endian, u64 mask);

// Functions left here are for compatibility with the cxlflash driver

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
 * Read the configuration space of a function and fill in a
 * ocxl_fn_config structure with all the function details
 */
int ocxl_config_read_function(struct pci_dev *dev,
				struct ocxl_fn_config *fn);

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
 */
int ocxl_link_irq_alloc(void *link_handle, int *hw_irq);

/*
 * Free a previously allocated AFU interrupt
 */
void ocxl_link_free_irq(void *link_handle, int hw_irq);

#endif /* _MISC_OCXL_H_ */
