/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Driver for VMware Virtual Machine Communication Interface (VMCI) device. */

#ifndef _VMCI_H_
#define _VMCI_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>

#include "vmci_datagram.h"
#include "vmci_kernel_if.h"

/* VMCI device vendor and device ID */
#define VMCI_VMWARE_VENDOR_ID	0x15AD
#define VMCI_VMWARE_DEVICE_ID	0x0740

#define VMCI_VERSION		1

struct vmci_dma_alloc {
	bus_dma_tag_t	dma_tag;
	caddr_t		dma_vaddr;
	bus_addr_t	dma_paddr;
	bus_dmamap_t	dma_map;
	bus_size_t	dma_size;
};

struct vmci_interrupt {
	struct resource	*vmci_irq;
	int		vmci_rid;
	void		*vmci_handler;
};

struct vmci_softc {
	device_t		vmci_dev;

	struct mtx		vmci_spinlock;

	struct resource		*vmci_res0;
	bus_space_tag_t		vmci_iot0;
	bus_space_handle_t	vmci_ioh0;
	unsigned int		vmci_ioaddr;
	struct resource		*vmci_res1;
	bus_space_tag_t		vmci_iot1;
	bus_space_handle_t	vmci_ioh1;

	struct vmci_dma_alloc	vmci_notifications_bitmap;

	int			vmci_num_intr;
	vmci_intr_type		vmci_intr_type;
	struct vmci_interrupt	vmci_intrs[VMCI_MAX_INTRS];
	struct task		vmci_interrupt_dq_task;
	struct task		vmci_interrupt_bm_task;

	struct task		vmci_delayed_work_task;
	struct mtx		vmci_delayed_work_lock;
	vmci_list(vmci_delayed_work_info) vmci_delayed_work_infos;

	unsigned int		capabilities;
};

int	vmci_dma_malloc(bus_size_t size, bus_size_t align,
	    struct vmci_dma_alloc *dma);
void	vmci_dma_free(struct vmci_dma_alloc *);
int	vmci_schedule_delayed_work_fn(vmci_work_fn *work_fn, void *data);

#endif /* !_VMCI_H_ */
