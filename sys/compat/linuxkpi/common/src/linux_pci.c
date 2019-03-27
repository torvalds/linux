/*-
 * Copyright (c) 2015-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/stdarg.h>

#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/compat.h>

static device_probe_t linux_pci_probe;
static device_attach_t linux_pci_attach;
static device_detach_t linux_pci_detach;
static device_suspend_t linux_pci_suspend;
static device_resume_t linux_pci_resume;
static device_shutdown_t linux_pci_shutdown;

static device_method_t pci_methods[] = {
	DEVMETHOD(device_probe, linux_pci_probe),
	DEVMETHOD(device_attach, linux_pci_attach),
	DEVMETHOD(device_detach, linux_pci_detach),
	DEVMETHOD(device_suspend, linux_pci_suspend),
	DEVMETHOD(device_resume, linux_pci_resume),
	DEVMETHOD(device_shutdown, linux_pci_shutdown),
	DEVMETHOD_END
};

static struct pci_driver *
linux_pci_find(device_t dev, const struct pci_device_id **idp)
{
	const struct pci_device_id *id;
	struct pci_driver *pdrv;
	uint16_t vendor;
	uint16_t device;
	uint16_t subvendor;
	uint16_t subdevice;

	vendor = pci_get_vendor(dev);
	device = pci_get_device(dev);
	subvendor = pci_get_subvendor(dev);
	subdevice = pci_get_subdevice(dev);

	spin_lock(&pci_lock);
	list_for_each_entry(pdrv, &pci_drivers, links) {
		for (id = pdrv->id_table; id->vendor != 0; id++) {
			if (vendor == id->vendor &&
			    (PCI_ANY_ID == id->device || device == id->device) &&
			    (PCI_ANY_ID == id->subvendor || subvendor == id->subvendor) &&
			    (PCI_ANY_ID == id->subdevice || subdevice == id->subdevice)) {
				*idp = id;
				spin_unlock(&pci_lock);
				return (pdrv);
			}
		}
	}
	spin_unlock(&pci_lock);
	return (NULL);
}

static int
linux_pci_probe(device_t dev)
{
	const struct pci_device_id *id;
	struct pci_driver *pdrv;

	if ((pdrv = linux_pci_find(dev, &id)) == NULL)
		return (ENXIO);
	if (device_get_driver(dev) != &pdrv->bsddriver)
		return (ENXIO);
	device_set_desc(dev, pdrv->name);
	return (0);
}

static int
linux_pci_attach(device_t dev)
{
	struct resource_list_entry *rle;
	struct pci_bus *pbus;
	struct pci_dev *pdev;
	struct pci_devinfo *dinfo;
	struct pci_driver *pdrv;
	const struct pci_device_id *id;
	device_t parent;
	devclass_t devclass;
	int error;

	linux_set_current(curthread);

	pdrv = linux_pci_find(dev, &id);
	pdev = device_get_softc(dev);

	parent = device_get_parent(dev);
	devclass = device_get_devclass(parent);
	if (pdrv->isdrm) {
		dinfo = device_get_ivars(parent);
		device_set_ivars(dev, dinfo);
	} else {
		dinfo = device_get_ivars(dev);
	}

	pdev->dev.parent = &linux_root_device;
	pdev->dev.bsddev = dev;
	INIT_LIST_HEAD(&pdev->dev.irqents);
	pdev->devfn = PCI_DEVFN(pci_get_slot(dev), pci_get_function(dev));
	pdev->device = dinfo->cfg.device;
	pdev->vendor = dinfo->cfg.vendor;
	pdev->subsystem_vendor = dinfo->cfg.subvendor;
	pdev->subsystem_device = dinfo->cfg.subdevice;
	pdev->class = pci_get_class(dev);
	pdev->revision = pci_get_revid(dev);
	pdev->dev.dma_mask = &pdev->dma_mask;
	pdev->pdrv = pdrv;
	kobject_init(&pdev->dev.kobj, &linux_dev_ktype);
	kobject_set_name(&pdev->dev.kobj, device_get_nameunit(dev));
	kobject_add(&pdev->dev.kobj, &linux_root_device.kobj,
	    kobject_name(&pdev->dev.kobj));
	rle = linux_pci_get_rle(pdev, SYS_RES_IRQ, 0);
	if (rle != NULL)
		pdev->dev.irq = rle->start;
	else
		pdev->dev.irq = LINUX_IRQ_INVALID;
	pdev->irq = pdev->dev.irq;

	if (pdev->bus == NULL) {
		pbus = malloc(sizeof(*pbus), M_DEVBUF, M_WAITOK | M_ZERO);
		pbus->self = pdev;
		pbus->number = pci_get_bus(dev);
		pdev->bus = pbus;
	}

	spin_lock(&pci_lock);
	list_add(&pdev->links, &pci_devices);
	spin_unlock(&pci_lock);

	error = pdrv->probe(pdev, id);
	if (error) {
		spin_lock(&pci_lock);
		list_del(&pdev->links);
		spin_unlock(&pci_lock);
		put_device(&pdev->dev);
		error = -error;
	}
	return (error);
}

static int
linux_pci_detach(device_t dev)
{
	struct pci_dev *pdev;

	linux_set_current(curthread);
	pdev = device_get_softc(dev);

	pdev->pdrv->remove(pdev);

	spin_lock(&pci_lock);
	list_del(&pdev->links);
	spin_unlock(&pci_lock);
	device_set_desc(dev, NULL);
	put_device(&pdev->dev);

	return (0);
}

static int
linux_pci_suspend(device_t dev)
{
	const struct dev_pm_ops *pmops;
	struct pm_message pm = { };
	struct pci_dev *pdev;
	int error;

	error = 0;
	linux_set_current(curthread);
	pdev = device_get_softc(dev);
	pmops = pdev->pdrv->driver.pm;

	if (pdev->pdrv->suspend != NULL)
		error = -pdev->pdrv->suspend(pdev, pm);
	else if (pmops != NULL && pmops->suspend != NULL) {
		error = -pmops->suspend(&pdev->dev);
		if (error == 0 && pmops->suspend_late != NULL)
			error = -pmops->suspend_late(&pdev->dev);
	}
	return (error);
}

static int
linux_pci_resume(device_t dev)
{
	const struct dev_pm_ops *pmops;
	struct pci_dev *pdev;
	int error;

	error = 0;
	linux_set_current(curthread);
	pdev = device_get_softc(dev);
	pmops = pdev->pdrv->driver.pm;

	if (pdev->pdrv->resume != NULL)
		error = -pdev->pdrv->resume(pdev);
	else if (pmops != NULL && pmops->resume != NULL) {
		if (pmops->resume_early != NULL)
			error = -pmops->resume_early(&pdev->dev);
		if (error == 0 && pmops->resume != NULL)
			error = -pmops->resume(&pdev->dev);
	}
	return (error);
}

static int
linux_pci_shutdown(device_t dev)
{
	struct pci_dev *pdev;

	linux_set_current(curthread);
	pdev = device_get_softc(dev);
	if (pdev->pdrv->shutdown != NULL)
		pdev->pdrv->shutdown(pdev);
	return (0);
}

static int
_linux_pci_register_driver(struct pci_driver *pdrv, devclass_t dc)
{
	int error;

	linux_set_current(curthread);
	spin_lock(&pci_lock);
	list_add(&pdrv->links, &pci_drivers);
	spin_unlock(&pci_lock);
	pdrv->bsddriver.name = pdrv->name;
	pdrv->bsddriver.methods = pci_methods;
	pdrv->bsddriver.size = sizeof(struct pci_dev);

	mtx_lock(&Giant);
	error = devclass_add_driver(dc, &pdrv->bsddriver,
	    BUS_PASS_DEFAULT, &pdrv->bsdclass);
	mtx_unlock(&Giant);
	return (-error);
}

int
linux_pci_register_driver(struct pci_driver *pdrv)
{
	devclass_t dc;

	dc = devclass_find("pci");
	if (dc == NULL)
		return (-ENXIO);
	pdrv->isdrm = false;
	return (_linux_pci_register_driver(pdrv, dc));
}

int
linux_pci_register_drm_driver(struct pci_driver *pdrv)
{
	devclass_t dc;

	dc = devclass_create("vgapci");
	if (dc == NULL)
		return (-ENXIO);
	pdrv->isdrm = true;
	pdrv->name = "drmn";
	return (_linux_pci_register_driver(pdrv, dc));
}

void
linux_pci_unregister_driver(struct pci_driver *pdrv)
{
	devclass_t bus;

	bus = devclass_find("pci");

	spin_lock(&pci_lock);
	list_del(&pdrv->links);
	spin_unlock(&pci_lock);
	mtx_lock(&Giant);
	if (bus != NULL)
		devclass_delete_driver(bus, &pdrv->bsddriver);
	mtx_unlock(&Giant);
}
