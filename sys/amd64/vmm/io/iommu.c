/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/cpu.h>
#include <machine/md_var.h>

#include "vmm_util.h"
#include "vmm_mem.h"
#include "iommu.h"

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, iommu, CTLFLAG_RW, 0, "bhyve iommu parameters");

static int iommu_avail;
SYSCTL_INT(_hw_vmm_iommu, OID_AUTO, initialized, CTLFLAG_RD, &iommu_avail,
    0, "bhyve iommu initialized?");

static int iommu_enable = 1;
SYSCTL_INT(_hw_vmm_iommu, OID_AUTO, enable, CTLFLAG_RDTUN, &iommu_enable, 0,
    "Enable use of I/O MMU (required for PCI passthrough).");

static struct iommu_ops *ops;
static void *host_domain;
static eventhandler_tag add_tag, delete_tag;

static __inline int
IOMMU_INIT(void)
{
	if (ops != NULL)
		return ((*ops->init)());
	else
		return (ENXIO);
}

static __inline void
IOMMU_CLEANUP(void)
{
	if (ops != NULL && iommu_avail)
		(*ops->cleanup)();
}

static __inline void *
IOMMU_CREATE_DOMAIN(vm_paddr_t maxaddr)
{

	if (ops != NULL && iommu_avail)
		return ((*ops->create_domain)(maxaddr));
	else
		return (NULL);
}

static __inline void
IOMMU_DESTROY_DOMAIN(void *dom)
{

	if (ops != NULL && iommu_avail)
		(*ops->destroy_domain)(dom);
}

static __inline uint64_t
IOMMU_CREATE_MAPPING(void *domain, vm_paddr_t gpa, vm_paddr_t hpa, uint64_t len)
{

	if (ops != NULL && iommu_avail)
		return ((*ops->create_mapping)(domain, gpa, hpa, len));
	else
		return (len);		/* XXX */
}

static __inline uint64_t
IOMMU_REMOVE_MAPPING(void *domain, vm_paddr_t gpa, uint64_t len)
{

	if (ops != NULL && iommu_avail)
		return ((*ops->remove_mapping)(domain, gpa, len));
	else
		return (len);		/* XXX */
}

static __inline void
IOMMU_ADD_DEVICE(void *domain, uint16_t rid)
{

	if (ops != NULL && iommu_avail)
		(*ops->add_device)(domain, rid);
}

static __inline void
IOMMU_REMOVE_DEVICE(void *domain, uint16_t rid)
{

	if (ops != NULL && iommu_avail)
		(*ops->remove_device)(domain, rid);
}

static __inline void
IOMMU_INVALIDATE_TLB(void *domain)
{

	if (ops != NULL && iommu_avail)
		(*ops->invalidate_tlb)(domain);
}

static __inline void
IOMMU_ENABLE(void)
{

	if (ops != NULL && iommu_avail)
		(*ops->enable)();
}

static __inline void
IOMMU_DISABLE(void)
{

	if (ops != NULL && iommu_avail)
		(*ops->disable)();
}

static void
iommu_pci_add(void *arg, device_t dev)
{

	/* Add new devices to the host domain. */
	iommu_add_device(host_domain, pci_get_rid(dev));
}

static void
iommu_pci_delete(void *arg, device_t dev)
{

	iommu_remove_device(host_domain, pci_get_rid(dev));
}

static void
iommu_init(void)
{
	int error, bus, slot, func;
	vm_paddr_t maxaddr;
	devclass_t dc;
	device_t dev;

	if (!iommu_enable)
		return;

	if (vmm_is_intel())
		ops = &iommu_ops_intel;
	else if (vmm_is_amd())
		ops = &iommu_ops_amd;
	else
		ops = NULL;

	error = IOMMU_INIT();
	if (error)
		return;

	iommu_avail = 1;

	/*
	 * Create a domain for the devices owned by the host
	 */
	maxaddr = vmm_mem_maxaddr();
	host_domain = IOMMU_CREATE_DOMAIN(maxaddr);
	if (host_domain == NULL) {
		printf("iommu_init: unable to create a host domain");
		IOMMU_CLEANUP();
		ops = NULL;
		iommu_avail = 0;
		return;
	}

	/*
	 * Create 1:1 mappings from '0' to 'maxaddr' for devices assigned to
	 * the host
	 */
	iommu_create_mapping(host_domain, 0, 0, maxaddr);

	add_tag = EVENTHANDLER_REGISTER(pci_add_device, iommu_pci_add, NULL, 0);
	delete_tag = EVENTHANDLER_REGISTER(pci_delete_device, iommu_pci_delete,
	    NULL, 0);
	dc = devclass_find("ppt");
	for (bus = 0; bus <= PCI_BUSMAX; bus++) {
		for (slot = 0; slot <= PCI_SLOTMAX; slot++) {
			for (func = 0; func <= PCI_FUNCMAX; func++) {
				dev = pci_find_dbsf(0, bus, slot, func);
				if (dev == NULL)
					continue;

				/* Skip passthrough devices. */
				if (dc != NULL &&
				    device_get_devclass(dev) == dc)
					continue;

				/*
				 * Everything else belongs to the host
				 * domain.
				 */
				iommu_add_device(host_domain,
				    pci_get_rid(dev));
			}
		}
	}
	IOMMU_ENABLE();

}

void
iommu_cleanup(void)
{

	if (add_tag != NULL) {
		EVENTHANDLER_DEREGISTER(pci_add_device, add_tag);
		add_tag = NULL;
	}
	if (delete_tag != NULL) {
		EVENTHANDLER_DEREGISTER(pci_delete_device, delete_tag);
		delete_tag = NULL;
	}
	IOMMU_DISABLE();
	IOMMU_DESTROY_DOMAIN(host_domain);
	IOMMU_CLEANUP();
}

void *
iommu_create_domain(vm_paddr_t maxaddr)
{
	static volatile int iommu_initted;

	if (iommu_initted < 2) {
		if (atomic_cmpset_int(&iommu_initted, 0, 1)) {
			iommu_init();
			atomic_store_rel_int(&iommu_initted, 2);
		} else
			while (iommu_initted == 1)
				cpu_spinwait();
	}
	return (IOMMU_CREATE_DOMAIN(maxaddr));
}

void
iommu_destroy_domain(void *dom)
{

	IOMMU_DESTROY_DOMAIN(dom);
}

void
iommu_create_mapping(void *dom, vm_paddr_t gpa, vm_paddr_t hpa, size_t len)
{
	uint64_t mapped, remaining;

	remaining = len;

	while (remaining > 0) {
		mapped = IOMMU_CREATE_MAPPING(dom, gpa, hpa, remaining);
		gpa += mapped;
		hpa += mapped;
		remaining -= mapped;
	}
}

void
iommu_remove_mapping(void *dom, vm_paddr_t gpa, size_t len)
{
	uint64_t unmapped, remaining;

	remaining = len;

	while (remaining > 0) {
		unmapped = IOMMU_REMOVE_MAPPING(dom, gpa, remaining);
		gpa += unmapped;
		remaining -= unmapped;
	}
}

void *
iommu_host_domain(void)
{

	return (host_domain);
}

void
iommu_add_device(void *dom, uint16_t rid)
{

	IOMMU_ADD_DEVICE(dom, rid);
}

void
iommu_remove_device(void *dom, uint16_t rid)
{

	IOMMU_REMOVE_DEVICE(dom, rid);
}

void
iommu_invalidate_tlb(void *domain)
{

	IOMMU_INVALIDATE_TLB(domain);
}
