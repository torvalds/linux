/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _MISC_CXL_H
#define _MISC_CXL_H

#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <uapi/misc/cxl.h>

/*
 * This documents the in kernel API for driver to use CXL. It allows kernel
 * drivers to bind to AFUs using an AFU configuration record exposed as a PCI
 * configuration record.
 *
 * This API enables control over AFU and contexts which can't be part of the
 * generic PCI API. This API is agnostic to the actual AFU.
 */

/* Get the AFU associated with a pci_dev */
struct cxl_afu *cxl_pci_to_afu(struct pci_dev *dev);

/* Get the AFU conf record number associated with a pci_dev */
unsigned int cxl_pci_to_cfg_record(struct pci_dev *dev);


/*
 * Context lifetime overview:
 *
 * An AFU context may be inited and then started and stoppped multiple times
 * before it's released. ie.
 *    - cxl_dev_context_init()
 *      - cxl_start_context()
 *      - cxl_stop_context()
 *      - cxl_start_context()
 *      - cxl_stop_context()
 *     ...repeat...
 *    - cxl_release_context()
 * Once released, a context can't be started again.
 *
 * One context is inited by the cxl driver for every pci_dev. This is to be
 * used as a default kernel context. cxl_get_context() will get this
 * context. This context will be released by PCI hot unplug, so doesn't need to
 * be released explicitly by drivers.
 *
 * Additional kernel contexts may be inited using cxl_dev_context_init().
 * These must be released using cxl_context_detach().
 *
 * Once a context has been inited, IRQs may be configured. Firstly these IRQs
 * must be allocated (cxl_allocate_afu_irqs()), then individually mapped to
 * specific handlers (cxl_map_afu_irq()).
 *
 * These IRQs can be unmapped (cxl_unmap_afu_irq()) and finally released
 * (cxl_free_afu_irqs()).
 *
 * The AFU can be reset (cxl_afu_reset()). This will cause the PSL/AFU
 * hardware to lose track of all contexts. It's upto the caller of
 * cxl_afu_reset() to restart these contexts.
 */

/*
 * On pci_enabled_device(), the cxl driver will init a single cxl context for
 * use by the driver. It doesn't start this context (as that will likely
 * generate DMA traffic for most AFUs).
 *
 * This gets the default context associated with this pci_dev.  This context
 * doesn't need to be released as this will be done by the PCI subsystem on hot
 * unplug.
 */
struct cxl_context *cxl_get_context(struct pci_dev *dev);
/*
 * Allocate and initalise a context associated with a AFU PCI device. This
 * doesn't start the context in the AFU.
 */
struct cxl_context *cxl_dev_context_init(struct pci_dev *dev);
/*
 * Release and free a context. Context should be stopped before calling.
 */
int cxl_release_context(struct cxl_context *ctx);

/*
 * Allocate AFU interrupts for this context. num=0 will allocate the default
 * for this AFU as given in the AFU descriptor. This number doesn't include the
 * interrupt 0 (CAIA defines AFU IRQ 0 for page faults). Each interrupt to be
 * used must map a handler with cxl_map_afu_irq.
 */
int cxl_allocate_afu_irqs(struct cxl_context *cxl, int num);
/* Free allocated interrupts */
void cxl_free_afu_irqs(struct cxl_context *cxl);

/*
 * Map a handler for an AFU interrupt associated with a particular context. AFU
 * IRQS numbers start from 1 (CAIA defines AFU IRQ 0 for page faults). cookie
 * is private data is that will be provided to the interrupt handler.
 */
int cxl_map_afu_irq(struct cxl_context *cxl, int num,
		    irq_handler_t handler, void *cookie, char *name);
/* unmap mapped IRQ handlers */
void cxl_unmap_afu_irq(struct cxl_context *cxl, int num, void *cookie);

/*
 * Start work on the AFU. This starts an cxl context and associates it with a
 * task. task == NULL will make it a kernel context.
 */
int cxl_start_context(struct cxl_context *ctx, u64 wed,
		      struct task_struct *task);
/*
 * Stop a context and remove it from the PSL
 */
int cxl_stop_context(struct cxl_context *ctx);

/* Reset the AFU */
int cxl_afu_reset(struct cxl_context *ctx);

/*
 * Set a context as a master context.
 * This sets the default problem space area mapped as the full space, rather
 * than just the per context area (for slaves).
 */
void cxl_set_master(struct cxl_context *ctx);

/*
 * Sets the context to use real mode memory accesses to operate with
 * translation disabled. Note that this only makes sense for kernel contexts
 * under bare metal, and will not work with virtualisation. May only be
 * performed on stopped contexts.
 */
int cxl_set_translation_mode(struct cxl_context *ctx, bool real_mode);

/*
 * Map and unmap the AFU Problem Space area. The amount and location mapped
 * depends on if this context is a master or slave.
 */
void __iomem *cxl_psa_map(struct cxl_context *ctx);
void cxl_psa_unmap(void __iomem *addr);

/*  Get the process element for this context */
int cxl_process_element(struct cxl_context *ctx);


/*
 * These calls allow drivers to create their own file descriptors and make them
 * identical to the cxl file descriptor user API. An example use case:
 *
 * struct file_operations cxl_my_fops = {};
 * ......
 *	// Init the context
 *	ctx = cxl_dev_context_init(dev);
 *	if (IS_ERR(ctx))
 *		return PTR_ERR(ctx);
 *	// Create and attach a new file descriptor to my file ops
 *	file = cxl_get_fd(ctx, &cxl_my_fops, &fd);
 *	// Start context
 *	rc = cxl_start_work(ctx, &work.work);
 *	if (rc) {
 *		fput(file);
 *		put_unused_fd(fd);
 *		return -ENODEV;
 *	}
 *	// No error paths after installing the fd
 *	fd_install(fd, file);
 *	return fd;
 *
 * This inits a context, and gets a file descriptor and associates some file
 * ops to that file descriptor. If the file ops are blank, the cxl driver will
 * fill them in with the default ones that mimic the standard user API.  Once
 * completed, the file descriptor can be installed. Once the file descriptor is
 * installed, it's visible to the user so no errors must occur past this point.
 *
 * If cxl_fd_release() file op call is installed, the context will be stopped
 * and released when the fd is released. Hence the driver won't need to manage
 * this itself.
 */

/*
 * Take a context and associate it with my file ops. Returns the associated
 * file and file descriptor. Any file ops which are blank are filled in by the
 * cxl driver with the default ops to mimic the standard API.
 */
struct file *cxl_get_fd(struct cxl_context *ctx, struct file_operations *fops,
			int *fd);
/* Get the context associated with this file */
struct cxl_context *cxl_fops_get_context(struct file *file);
/*
 * Start a context associated a struct cxl_ioctl_start_work used by the
 * standard cxl user API.
 */
int cxl_start_work(struct cxl_context *ctx,
		   struct cxl_ioctl_start_work *work);
/*
 * Export all the existing fops so drivers can use them
 */
int cxl_fd_open(struct inode *inode, struct file *file);
int cxl_fd_release(struct inode *inode, struct file *file);
long cxl_fd_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int cxl_fd_mmap(struct file *file, struct vm_area_struct *vm);
unsigned int cxl_fd_poll(struct file *file, struct poll_table_struct *poll);
ssize_t cxl_fd_read(struct file *file, char __user *buf, size_t count,
			   loff_t *off);

/*
 * For EEH, a driver may want to assert a PERST will reload the same image
 * from flash into the FPGA.
 *
 * This is a property of the entire adapter, not a single AFU, so drivers
 * should set this property with care!
 */
void cxl_perst_reloads_same_image(struct cxl_afu *afu,
				  bool perst_reloads_same_image);

/*
 * Read the VPD for the card where the AFU resides
 */
ssize_t cxl_read_adapter_vpd(struct pci_dev *dev, void *buf, size_t count);

#endif /* _MISC_CXL_H */
