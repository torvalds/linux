/*-
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
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

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_pcivar.h>

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include "pcib_if.h"
#include "pci_if.h"

/* Hooks for the ACPI CA debugging infrastructure. */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("PCI")

struct acpi_pci_devinfo {
	struct pci_devinfo	ap_dinfo;
	ACPI_HANDLE		ap_handle;
	int			ap_flags;
};

ACPI_SERIAL_DECL(pci_powerstate, "ACPI PCI power methods");

/* Be sure that ACPI and PCI power states are equivalent. */
CTASSERT(ACPI_STATE_D0 == PCI_POWERSTATE_D0);
CTASSERT(ACPI_STATE_D1 == PCI_POWERSTATE_D1);
CTASSERT(ACPI_STATE_D2 == PCI_POWERSTATE_D2);
CTASSERT(ACPI_STATE_D3 == PCI_POWERSTATE_D3);

static struct pci_devinfo *acpi_pci_alloc_devinfo(device_t dev);
static void	acpi_pci_child_deleted(device_t dev, device_t child);
static int	acpi_pci_child_location_str_method(device_t cbdev,
		    device_t child, char *buf, size_t buflen);
static int	acpi_pci_probe(device_t dev);
static int	acpi_pci_read_ivar(device_t dev, device_t child, int which,
		    uintptr_t *result);
static int	acpi_pci_write_ivar(device_t dev, device_t child, int which,
		    uintptr_t value);
static ACPI_STATUS acpi_pci_save_handle(ACPI_HANDLE handle, UINT32 level,
		    void *context, void **status);
static int	acpi_pci_set_powerstate_method(device_t dev, device_t child,
		    int state);
static void	acpi_pci_update_device(ACPI_HANDLE handle, device_t pci_child);
static bus_dma_tag_t acpi_pci_get_dma_tag(device_t bus, device_t child);

static device_method_t acpi_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		acpi_pci_probe),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	acpi_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	acpi_pci_write_ivar),
	DEVMETHOD(bus_child_deleted,	acpi_pci_child_deleted),
	DEVMETHOD(bus_child_location_str, acpi_pci_child_location_str_method),
	DEVMETHOD(bus_get_cpus,		acpi_get_cpus),
	DEVMETHOD(bus_get_dma_tag,	acpi_pci_get_dma_tag),
	DEVMETHOD(bus_get_domain,	acpi_get_domain),

	/* PCI interface */
	DEVMETHOD(pci_alloc_devinfo,	acpi_pci_alloc_devinfo),
	DEVMETHOD(pci_child_added,	acpi_pci_child_added),
	DEVMETHOD(pci_set_powerstate,	acpi_pci_set_powerstate_method),

	DEVMETHOD_END
};

static devclass_t pci_devclass;

DEFINE_CLASS_1(pci, acpi_pci_driver, acpi_pci_methods, sizeof(struct pci_softc),
    pci_driver);
DRIVER_MODULE(acpi_pci, pcib, acpi_pci_driver, pci_devclass, 0, 0);
MODULE_DEPEND(acpi_pci, acpi, 1, 1, 1);
MODULE_DEPEND(acpi_pci, pci, 1, 1, 1);
MODULE_VERSION(acpi_pci, 1);

static struct pci_devinfo *
acpi_pci_alloc_devinfo(device_t dev)
{
	struct acpi_pci_devinfo *dinfo;

	dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);
	return (&dinfo->ap_dinfo);
}

static int
acpi_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
    struct acpi_pci_devinfo *dinfo;

    dinfo = device_get_ivars(child);
    switch (which) {
    case ACPI_IVAR_HANDLE:
	*result = (uintptr_t)dinfo->ap_handle;
	return (0);
    case ACPI_IVAR_FLAGS:
	*result = (uintptr_t)dinfo->ap_flags;
	return (0);
    }
    return (pci_read_ivar(dev, child, which, result));
}

static int
acpi_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
    struct acpi_pci_devinfo *dinfo;

    dinfo = device_get_ivars(child);
    switch (which) {
    case ACPI_IVAR_HANDLE:
	dinfo->ap_handle = (ACPI_HANDLE)value;
	return (0);
    case ACPI_IVAR_FLAGS:
	dinfo->ap_flags = (int)value;
	return (0);
    }
    return (pci_write_ivar(dev, child, which, value));
}

static void
acpi_pci_child_deleted(device_t dev, device_t child)
{
	struct acpi_pci_devinfo *dinfo = device_get_ivars(child);

	if (acpi_get_device(dinfo->ap_handle) == child)
		AcpiDetachData(dinfo->ap_handle, acpi_fake_objhandler);
	pci_child_deleted(dev, child);
}

static int
acpi_pci_child_location_str_method(device_t cbdev, device_t child, char *buf,
    size_t buflen)
{
    struct acpi_pci_devinfo *dinfo = device_get_ivars(child);
    int pxm;
    char buf2[32];

    pci_child_location_str_method(cbdev, child, buf, buflen);

    if (dinfo->ap_handle) {
        strlcat(buf, " handle=", buflen);
        strlcat(buf, acpi_name(dinfo->ap_handle), buflen);

        if (ACPI_SUCCESS(acpi_GetInteger(dinfo->ap_handle, "_PXM", &pxm))) {
                snprintf(buf2, 32, " _PXM=%d", pxm);
                strlcat(buf, buf2, buflen);
        }
    }
    return (0);
}

/*
 * PCI power manangement
 */
static int
acpi_pci_set_powerstate_method(device_t dev, device_t child, int state)
{
	ACPI_HANDLE h;
	ACPI_STATUS status;
	int old_state, error;

	error = 0;
	if (state < ACPI_STATE_D0 || state > ACPI_STATE_D3)
		return (EINVAL);

	/*
	 * We set the state using PCI Power Management outside of setting
	 * the ACPI state.  This means that when powering down a device, we
	 * first shut it down using PCI, and then using ACPI, which lets ACPI
	 * try to power down any Power Resources that are now no longer used.
	 * When powering up a device, we let ACPI set the state first so that
	 * it can enable any needed Power Resources before changing the PCI
	 * power state.
	 */
	ACPI_SERIAL_BEGIN(pci_powerstate);
	old_state = pci_get_powerstate(child);
	if (old_state < state && pci_do_power_suspend) {
		error = pci_set_powerstate_method(dev, child, state);
		if (error)
			goto out;
	}
	h = acpi_get_handle(child);
	status = acpi_pwr_switch_consumer(h, state);
	if (ACPI_SUCCESS(status)) {
		if (bootverbose)
			device_printf(dev, "set ACPI power state D%d on %s\n",
			    state, acpi_name(h));
	} else if (status != AE_NOT_FOUND)
		device_printf(dev,
		    "failed to set ACPI power state D%d on %s: %s\n",
		    state, acpi_name(h), AcpiFormatException(status));
	if (old_state > state && pci_do_power_resume)
		error = pci_set_powerstate_method(dev, child, state);

out:
	ACPI_SERIAL_END(pci_powerstate);
	return (error);
}

static void
acpi_pci_update_device(ACPI_HANDLE handle, device_t pci_child)
{
	ACPI_STATUS status;
	device_t child;

	/*
	 * Occasionally a PCI device may show up as an ACPI device
	 * with a _HID.  (For example, the TabletPC TC1000 has a
	 * second PCI-ISA bridge that has a _HID for an
	 * acpi_sysresource device.)  In that case, leave ACPI-CA's
	 * device data pointing at the ACPI-enumerated device.
	 */
	child = acpi_get_device(handle);
	if (child != NULL) {
		KASSERT(device_get_parent(child) ==
		    devclass_get_device(devclass_find("acpi"), 0),
		    ("%s: child (%s)'s parent is not acpi0", __func__,
		    acpi_name(handle)));
		return;
	}

	/*
	 * Update ACPI-CA to use the PCI enumerated device_t for this handle.
	 */
	status = AcpiAttachData(handle, acpi_fake_objhandler, pci_child);
	if (ACPI_FAILURE(status))
		printf("WARNING: Unable to attach object data to %s - %s\n",
		    acpi_name(handle), AcpiFormatException(status));
}

static ACPI_STATUS
acpi_pci_save_handle(ACPI_HANDLE handle, UINT32 level, void *context,
    void **status)
{
	struct acpi_pci_devinfo *dinfo;
	device_t child;
	int func, slot;
	UINT32 address;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	child = context;
	if (ACPI_FAILURE(acpi_GetInteger(handle, "_ADR", &address)))
		return_ACPI_STATUS (AE_OK);
	slot = ACPI_ADR_PCI_SLOT(address);
	func = ACPI_ADR_PCI_FUNC(address);
	dinfo = device_get_ivars(child);
	if (dinfo->ap_dinfo.cfg.func == func &&
	    dinfo->ap_dinfo.cfg.slot == slot) {
		dinfo->ap_handle = handle;
		acpi_pci_update_device(handle, child);
		return_ACPI_STATUS (AE_CTRL_TERMINATE);
	}
	return_ACPI_STATUS (AE_OK);
}

void
acpi_pci_child_added(device_t dev, device_t child)
{

	/*
	 * PCI devices are added via the bus scan in the normal PCI
	 * bus driver.  As each device is added, the
	 * acpi_pci_child_added() callback walks the ACPI namespace
	 * under the bridge driver to save ACPI handles to all the
	 * devices that appear in the ACPI namespace as immediate
	 * descendants of the bridge.
	 *
	 * XXX: Sometimes PCI devices show up in the ACPI namespace that
	 * pci_add_children() doesn't find.  We currently just ignore
	 * these devices.
	 */
	AcpiWalkNamespace(ACPI_TYPE_DEVICE, acpi_get_handle(dev), 1,
	    acpi_pci_save_handle, NULL, child, NULL);
}

static int
acpi_pci_probe(device_t dev)
{

	if (acpi_get_handle(dev) == NULL)
		return (ENXIO);
	device_set_desc(dev, "ACPI PCI bus");
	return (BUS_PROBE_DEFAULT);
}

#ifdef ACPI_DMAR
bus_dma_tag_t dmar_get_dma_tag(device_t dev, device_t child);
static bus_dma_tag_t
acpi_pci_get_dma_tag(device_t bus, device_t child)
{
	bus_dma_tag_t tag;

	if (device_get_parent(child) == bus) {
		/* try dmar and return if it works */
		tag = dmar_get_dma_tag(bus, child);
	} else
		tag = NULL;
	if (tag == NULL)
		tag = pci_get_dma_tag(bus, child);
	return (tag);
}
#else
static bus_dma_tag_t
acpi_pci_get_dma_tag(device_t bus, device_t child)
{

	return (pci_get_dma_tag(bus, child));
}
#endif

