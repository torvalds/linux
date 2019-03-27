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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/pciio.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/resource.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmm_lapic.h"
#include "vmm_ktr.h"

#include "iommu.h"
#include "ppt.h"

/* XXX locking */

#define	MAX_MSIMSGS	32

/*
 * If the MSI-X table is located in the middle of a BAR then that MMIO
 * region gets split into two segments - one segment above the MSI-X table
 * and the other segment below the MSI-X table - with a hole in place of
 * the MSI-X table so accesses to it can be trapped and emulated.
 *
 * So, allocate a MMIO segment for each BAR register + 1 additional segment.
 */
#define	MAX_MMIOSEGS	((PCIR_MAX_BAR_0 + 1) + 1)

MALLOC_DEFINE(M_PPTMSIX, "pptmsix", "Passthru MSI-X resources");

struct pptintr_arg {				/* pptintr(pptintr_arg) */
	struct pptdev	*pptdev;
	uint64_t	addr;
	uint64_t	msg_data;
};

struct pptseg {
	vm_paddr_t	gpa;
	size_t		len;
	int		wired;
};

struct pptdev {
	device_t	dev;
	struct vm	*vm;			/* owner of this device */
	TAILQ_ENTRY(pptdev)	next;
	struct pptseg mmio[MAX_MMIOSEGS];
	struct {
		int	num_msgs;		/* guest state */

		int	startrid;		/* host state */
		struct resource *res[MAX_MSIMSGS];
		void	*cookie[MAX_MSIMSGS];
		struct pptintr_arg arg[MAX_MSIMSGS];
	} msi;

	struct {
		int num_msgs;
		int startrid;
		int msix_table_rid;
		struct resource *msix_table_res;
		struct resource **res;
		void **cookie;
		struct pptintr_arg *arg;
	} msix;
};

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, ppt, CTLFLAG_RW, 0, "bhyve passthru devices");

static int num_pptdevs;
SYSCTL_INT(_hw_vmm_ppt, OID_AUTO, devices, CTLFLAG_RD, &num_pptdevs, 0,
    "number of pci passthru devices");

static TAILQ_HEAD(, pptdev) pptdev_list = TAILQ_HEAD_INITIALIZER(pptdev_list);

static int
ppt_probe(device_t dev)
{
	int bus, slot, func;
	struct pci_devinfo *dinfo;

	dinfo = (struct pci_devinfo *)device_get_ivars(dev);

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);
	func = pci_get_function(dev);

	/*
	 * To qualify as a pci passthrough device a device must:
	 * - be allowed by administrator to be used in this role
	 * - be an endpoint device
	 */
	if ((dinfo->cfg.hdrtype & PCIM_HDRTYPE) != PCIM_HDRTYPE_NORMAL)
		return (ENXIO);
	else if (vmm_is_pptdev(bus, slot, func))
		return (0);
	else
		/*
		 * Returning BUS_PROBE_NOWILDCARD here matches devices that the
		 * SR-IOV infrastructure specified as "ppt" passthrough devices.
		 * All normal devices that did not have "ppt" specified as their
		 * driver will not be matched by this.
		 */
		return (BUS_PROBE_NOWILDCARD);
}

static int
ppt_attach(device_t dev)
{
	struct pptdev *ppt;

	ppt = device_get_softc(dev);

	iommu_remove_device(iommu_host_domain(), pci_get_rid(dev));
	num_pptdevs++;
	TAILQ_INSERT_TAIL(&pptdev_list, ppt, next);
	ppt->dev = dev;

	if (bootverbose)
		device_printf(dev, "attached\n");

	return (0);
}

static int
ppt_detach(device_t dev)
{
	struct pptdev *ppt;

	ppt = device_get_softc(dev);

	if (ppt->vm != NULL)
		return (EBUSY);
	num_pptdevs--;
	TAILQ_REMOVE(&pptdev_list, ppt, next);
	pci_disable_busmaster(dev);
	iommu_add_device(iommu_host_domain(), pci_get_rid(dev));

	return (0);
}

static device_method_t ppt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ppt_probe),
	DEVMETHOD(device_attach,	ppt_attach),
	DEVMETHOD(device_detach,	ppt_detach),
	{0, 0}
};

static devclass_t ppt_devclass;
DEFINE_CLASS_0(ppt, ppt_driver, ppt_methods, sizeof(struct pptdev));
DRIVER_MODULE(ppt, pci, ppt_driver, ppt_devclass, NULL, NULL);

static struct pptdev *
ppt_find(int bus, int slot, int func)
{
	device_t dev;
	struct pptdev *ppt;
	int b, s, f;

	TAILQ_FOREACH(ppt, &pptdev_list, next) {
		dev = ppt->dev;
		b = pci_get_bus(dev);
		s = pci_get_slot(dev);
		f = pci_get_function(dev);
		if (bus == b && slot == s && func == f)
			return (ppt);
	}
	return (NULL);
}

static void
ppt_unmap_mmio(struct vm *vm, struct pptdev *ppt)
{
	int i;
	struct pptseg *seg;

	for (i = 0; i < MAX_MMIOSEGS; i++) {
		seg = &ppt->mmio[i];
		if (seg->len == 0)
			continue;
		(void)vm_unmap_mmio(vm, seg->gpa, seg->len);
		bzero(seg, sizeof(struct pptseg));
	}
}

static void
ppt_teardown_msi(struct pptdev *ppt)
{
	int i, rid;
	void *cookie;
	struct resource *res;

	if (ppt->msi.num_msgs == 0)
		return;

	for (i = 0; i < ppt->msi.num_msgs; i++) {
		rid = ppt->msi.startrid + i;
		res = ppt->msi.res[i];
		cookie = ppt->msi.cookie[i];

		if (cookie != NULL)
			bus_teardown_intr(ppt->dev, res, cookie);

		if (res != NULL)
			bus_release_resource(ppt->dev, SYS_RES_IRQ, rid, res);
		
		ppt->msi.res[i] = NULL;
		ppt->msi.cookie[i] = NULL;
	}

	if (ppt->msi.startrid == 1)
		pci_release_msi(ppt->dev);

	ppt->msi.num_msgs = 0;
}

static void 
ppt_teardown_msix_intr(struct pptdev *ppt, int idx)
{
	int rid;
	struct resource *res;
	void *cookie;

	rid = ppt->msix.startrid + idx;
	res = ppt->msix.res[idx];
	cookie = ppt->msix.cookie[idx];

	if (cookie != NULL) 
		bus_teardown_intr(ppt->dev, res, cookie);

	if (res != NULL) 
		bus_release_resource(ppt->dev, SYS_RES_IRQ, rid, res);

	ppt->msix.res[idx] = NULL;
	ppt->msix.cookie[idx] = NULL;
}

static void 
ppt_teardown_msix(struct pptdev *ppt)
{
	int i;

	if (ppt->msix.num_msgs == 0) 
		return;

	for (i = 0; i < ppt->msix.num_msgs; i++) 
		ppt_teardown_msix_intr(ppt, i);

	if (ppt->msix.msix_table_res) {
		bus_release_resource(ppt->dev, SYS_RES_MEMORY, 
				     ppt->msix.msix_table_rid,
				     ppt->msix.msix_table_res);
		ppt->msix.msix_table_res = NULL;
		ppt->msix.msix_table_rid = 0;
	}

	free(ppt->msix.res, M_PPTMSIX);
	free(ppt->msix.cookie, M_PPTMSIX);
	free(ppt->msix.arg, M_PPTMSIX);

	pci_release_msi(ppt->dev);

	ppt->msix.num_msgs = 0;
}

int
ppt_avail_devices(void)
{

	return (num_pptdevs);
}

int
ppt_assigned_devices(struct vm *vm)
{
	struct pptdev *ppt;
	int num;

	num = 0;
	TAILQ_FOREACH(ppt, &pptdev_list, next) {
		if (ppt->vm == vm)
			num++;
	}
	return (num);
}

boolean_t
ppt_is_mmio(struct vm *vm, vm_paddr_t gpa)
{
	int i;
	struct pptdev *ppt;
	struct pptseg *seg;

	TAILQ_FOREACH(ppt, &pptdev_list, next) {
		if (ppt->vm != vm)
			continue;

		for (i = 0; i < MAX_MMIOSEGS; i++) {
			seg = &ppt->mmio[i];
			if (seg->len == 0)
				continue;
			if (gpa >= seg->gpa && gpa < seg->gpa + seg->len)
				return (TRUE);
		}
	}

	return (FALSE);
}

static void
ppt_pci_reset(device_t dev)
{
	int ps;

	if (pcie_flr(dev,
		     max(pcie_get_max_completion_timeout(dev) / 1000, 10),
		     true))
		return;

	/*
	 * If FLR fails, attempt a power-management reset by cycling
	 * the device in/out of D3 state.
	 * PCI spec says we can only go into D3 state from D0 state.
	 * Transition from D[12] into D0 before going to D3 state.
	 */
	ps = pci_get_powerstate(dev);
	if (ps != PCI_POWERSTATE_D0 && ps != PCI_POWERSTATE_D3)
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D3)
		pci_set_powerstate(dev, PCI_POWERSTATE_D3);
	pci_set_powerstate(dev, ps);
}

int
ppt_assign_device(struct vm *vm, int bus, int slot, int func)
{
	struct pptdev *ppt;

	ppt = ppt_find(bus, slot, func);
	if (ppt != NULL) {
		/*
		 * If this device is owned by a different VM then we
		 * cannot change its owner.
		 */
		if (ppt->vm != NULL && ppt->vm != vm)
			return (EBUSY);

		pci_save_state(ppt->dev);
		ppt_pci_reset(ppt->dev);
		pci_restore_state(ppt->dev);
		ppt->vm = vm;
		iommu_add_device(vm_iommu_domain(vm), pci_get_rid(ppt->dev));
		return (0);
	}
	return (ENOENT);
}

int
ppt_unassign_device(struct vm *vm, int bus, int slot, int func)
{
	struct pptdev *ppt;

	ppt = ppt_find(bus, slot, func);
	if (ppt != NULL) {
		/*
		 * If this device is not owned by this 'vm' then bail out.
		 */
		if (ppt->vm != vm)
			return (EBUSY);

		pci_save_state(ppt->dev);
		ppt_pci_reset(ppt->dev);
		pci_restore_state(ppt->dev);
		ppt_unmap_mmio(vm, ppt);
		ppt_teardown_msi(ppt);
		ppt_teardown_msix(ppt);
		iommu_remove_device(vm_iommu_domain(vm), pci_get_rid(ppt->dev));
		ppt->vm = NULL;
		return (0);
	}
	return (ENOENT);
}

int
ppt_unassign_all(struct vm *vm)
{
	struct pptdev *ppt;
	int bus, slot, func;
	device_t dev;

	TAILQ_FOREACH(ppt, &pptdev_list, next) {
		if (ppt->vm == vm) {
			dev = ppt->dev;
			bus = pci_get_bus(dev);
			slot = pci_get_slot(dev);
			func = pci_get_function(dev);
			vm_unassign_pptdev(vm, bus, slot, func);
		}
	}

	return (0);
}

int
ppt_map_mmio(struct vm *vm, int bus, int slot, int func,
	     vm_paddr_t gpa, size_t len, vm_paddr_t hpa)
{
	int i, error;
	struct pptseg *seg;
	struct pptdev *ppt;

	ppt = ppt_find(bus, slot, func);
	if (ppt != NULL) {
		if (ppt->vm != vm)
			return (EBUSY);

		for (i = 0; i < MAX_MMIOSEGS; i++) {
			seg = &ppt->mmio[i];
			if (seg->len == 0) {
				error = vm_map_mmio(vm, gpa, len, hpa);
				if (error == 0) {
					seg->gpa = gpa;
					seg->len = len;
				}
				return (error);
			}
		}
		return (ENOSPC);
	}
	return (ENOENT);
}

static int
pptintr(void *arg)
{
	struct pptdev *ppt;
	struct pptintr_arg *pptarg;
	
	pptarg = arg;
	ppt = pptarg->pptdev;

	if (ppt->vm != NULL)
		lapic_intr_msi(ppt->vm, pptarg->addr, pptarg->msg_data);
	else {
		/*
		 * XXX
		 * This is not expected to happen - panic?
		 */
	}

	/*
	 * For legacy interrupts give other filters a chance in case
	 * the interrupt was not generated by the passthrough device.
	 */
	if (ppt->msi.startrid == 0)
		return (FILTER_STRAY);
	else
		return (FILTER_HANDLED);
}

int
ppt_setup_msi(struct vm *vm, int vcpu, int bus, int slot, int func,
	      uint64_t addr, uint64_t msg, int numvec)
{
	int i, rid, flags;
	int msi_count, startrid, error, tmp;
	struct pptdev *ppt;

	if (numvec < 0 || numvec > MAX_MSIMSGS)
		return (EINVAL);

	ppt = ppt_find(bus, slot, func);
	if (ppt == NULL)
		return (ENOENT);
	if (ppt->vm != vm)		/* Make sure we own this device */
		return (EBUSY);

	/* Free any allocated resources */
	ppt_teardown_msi(ppt);

	if (numvec == 0)		/* nothing more to do */
		return (0);

	flags = RF_ACTIVE;
	msi_count = pci_msi_count(ppt->dev);
	if (msi_count == 0) {
		startrid = 0;		/* legacy interrupt */
		msi_count = 1;
		flags |= RF_SHAREABLE;
	} else
		startrid = 1;		/* MSI */

	/*
	 * The device must be capable of supporting the number of vectors
	 * the guest wants to allocate.
	 */
	if (numvec > msi_count)
		return (EINVAL);

	/*
	 * Make sure that we can allocate all the MSI vectors that are needed
	 * by the guest.
	 */
	if (startrid == 1) {
		tmp = numvec;
		error = pci_alloc_msi(ppt->dev, &tmp);
		if (error)
			return (error);
		else if (tmp != numvec) {
			pci_release_msi(ppt->dev);
			return (ENOSPC);
		} else {
			/* success */
		}
	}
	
	ppt->msi.startrid = startrid;

	/*
	 * Allocate the irq resource and attach it to the interrupt handler.
	 */
	for (i = 0; i < numvec; i++) {
		ppt->msi.num_msgs = i + 1;
		ppt->msi.cookie[i] = NULL;

		rid = startrid + i;
		ppt->msi.res[i] = bus_alloc_resource_any(ppt->dev, SYS_RES_IRQ,
							 &rid, flags);
		if (ppt->msi.res[i] == NULL)
			break;

		ppt->msi.arg[i].pptdev = ppt;
		ppt->msi.arg[i].addr = addr;
		ppt->msi.arg[i].msg_data = msg + i;

		error = bus_setup_intr(ppt->dev, ppt->msi.res[i],
				       INTR_TYPE_NET | INTR_MPSAFE,
				       pptintr, NULL, &ppt->msi.arg[i],
				       &ppt->msi.cookie[i]);
		if (error != 0)
			break;
	}
	
	if (i < numvec) {
		ppt_teardown_msi(ppt);
		return (ENXIO);
	}

	return (0);
}

int
ppt_setup_msix(struct vm *vm, int vcpu, int bus, int slot, int func,
	       int idx, uint64_t addr, uint64_t msg, uint32_t vector_control)
{
	struct pptdev *ppt;
	struct pci_devinfo *dinfo;
	int numvec, alloced, rid, error;
	size_t res_size, cookie_size, arg_size;

	ppt = ppt_find(bus, slot, func);
	if (ppt == NULL)
		return (ENOENT);
	if (ppt->vm != vm)		/* Make sure we own this device */
		return (EBUSY);

	dinfo = device_get_ivars(ppt->dev);
	if (!dinfo) 
		return (ENXIO);

	/* 
	 * First-time configuration:
	 * 	Allocate the MSI-X table
	 *	Allocate the IRQ resources
	 *	Set up some variables in ppt->msix
	 */
	if (ppt->msix.num_msgs == 0) {
		numvec = pci_msix_count(ppt->dev);
		if (numvec <= 0)
			return (EINVAL);

		ppt->msix.startrid = 1;
		ppt->msix.num_msgs = numvec;

		res_size = numvec * sizeof(ppt->msix.res[0]);
		cookie_size = numvec * sizeof(ppt->msix.cookie[0]);
		arg_size = numvec * sizeof(ppt->msix.arg[0]);

		ppt->msix.res = malloc(res_size, M_PPTMSIX, M_WAITOK | M_ZERO);
		ppt->msix.cookie = malloc(cookie_size, M_PPTMSIX,
					  M_WAITOK | M_ZERO);
		ppt->msix.arg = malloc(arg_size, M_PPTMSIX, M_WAITOK | M_ZERO);

		rid = dinfo->cfg.msix.msix_table_bar;
		ppt->msix.msix_table_res = bus_alloc_resource_any(ppt->dev,
					       SYS_RES_MEMORY, &rid, RF_ACTIVE);

		if (ppt->msix.msix_table_res == NULL) {
			ppt_teardown_msix(ppt);
			return (ENOSPC);
		}
		ppt->msix.msix_table_rid = rid;

		alloced = numvec;
		error = pci_alloc_msix(ppt->dev, &alloced);
		if (error || alloced != numvec) {
			ppt_teardown_msix(ppt);
			return (error == 0 ? ENOSPC: error);
		}
	}

	if ((vector_control & PCIM_MSIX_VCTRL_MASK) == 0) {
		/* Tear down the IRQ if it's already set up */
		ppt_teardown_msix_intr(ppt, idx);

		/* Allocate the IRQ resource */
		ppt->msix.cookie[idx] = NULL;
		rid = ppt->msix.startrid + idx;
		ppt->msix.res[idx] = bus_alloc_resource_any(ppt->dev, SYS_RES_IRQ,
							    &rid, RF_ACTIVE);
		if (ppt->msix.res[idx] == NULL)
			return (ENXIO);
	
		ppt->msix.arg[idx].pptdev = ppt;
		ppt->msix.arg[idx].addr = addr;
		ppt->msix.arg[idx].msg_data = msg;
	
		/* Setup the MSI-X interrupt */
		error = bus_setup_intr(ppt->dev, ppt->msix.res[idx],
				       INTR_TYPE_NET | INTR_MPSAFE,
				       pptintr, NULL, &ppt->msix.arg[idx],
				       &ppt->msix.cookie[idx]);
	
		if (error != 0) {
			bus_teardown_intr(ppt->dev, ppt->msix.res[idx], ppt->msix.cookie[idx]);
			bus_release_resource(ppt->dev, SYS_RES_IRQ, rid, ppt->msix.res[idx]);
			ppt->msix.cookie[idx] = NULL;
			ppt->msix.res[idx] = NULL;
			return (ENXIO);
		}
	} else {
		/* Masked, tear it down if it's already been set up */
		ppt_teardown_msix_intr(ppt, idx);
	}

	return (0);
}
