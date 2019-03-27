/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include "opt_pci.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>

#include <machine/pci_cfgreg.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>
#include "pcib_if.h"

#include <dev/acpica/acpi_pcibvar.h>

/* Hooks for the ACPI CA debugging infrastructure. */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("PCI_ACPI")

struct acpi_hpcib_softc {
    device_t		ap_dev;
    ACPI_HANDLE		ap_handle;
    bus_dma_tag_t	ap_dma_tag;
    int			ap_flags;
    uint32_t		ap_osc_ctl;

    int			ap_segment;	/* PCI domain */
    int			ap_bus;		/* bios-assigned bus number */
    int			ap_addr;	/* device/func of PCI-Host bridge */

    ACPI_BUFFER		ap_prt;		/* interrupt routing table */
#ifdef NEW_PCIB
    struct pcib_host_resources ap_host_res;
#endif
};

static int		acpi_pcib_acpi_probe(device_t bus);
static int		acpi_pcib_acpi_attach(device_t bus);
static int		acpi_pcib_read_ivar(device_t dev, device_t child,
			    int which, uintptr_t *result);
static int		acpi_pcib_write_ivar(device_t dev, device_t child,
			    int which, uintptr_t value);
static uint32_t		acpi_pcib_read_config(device_t dev, u_int bus,
			    u_int slot, u_int func, u_int reg, int bytes);
static void		acpi_pcib_write_config(device_t dev, u_int bus,
			    u_int slot, u_int func, u_int reg, uint32_t data,
			    int bytes);
static int		acpi_pcib_acpi_route_interrupt(device_t pcib,
			    device_t dev, int pin);
static int		acpi_pcib_alloc_msi(device_t pcib, device_t dev,
			    int count, int maxcount, int *irqs);
static int		acpi_pcib_map_msi(device_t pcib, device_t dev,
			    int irq, uint64_t *addr, uint32_t *data);
static int		acpi_pcib_alloc_msix(device_t pcib, device_t dev,
			    int *irq);
static struct resource *acpi_pcib_acpi_alloc_resource(device_t dev,
			    device_t child, int type, int *rid,
			    rman_res_t start, rman_res_t end, rman_res_t count,
			    u_int flags);
#ifdef NEW_PCIB
static int		acpi_pcib_acpi_adjust_resource(device_t dev,
			    device_t child, int type, struct resource *r,
			    rman_res_t start, rman_res_t end);
#ifdef PCI_RES_BUS
static int		acpi_pcib_acpi_release_resource(device_t dev,
			    device_t child, int type, int rid,
			    struct resource *r);
#endif
#endif
static int		acpi_pcib_request_feature(device_t pcib, device_t dev,
			    enum pci_feature feature);
static bus_dma_tag_t	acpi_pcib_get_dma_tag(device_t bus, device_t child);

static device_method_t acpi_pcib_acpi_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		acpi_pcib_acpi_probe),
    DEVMETHOD(device_attach,		acpi_pcib_acpi_attach),
    DEVMETHOD(device_shutdown,		bus_generic_shutdown),
    DEVMETHOD(device_suspend,		bus_generic_suspend),
    DEVMETHOD(device_resume,		bus_generic_resume),

    /* Bus interface */
    DEVMETHOD(bus_read_ivar,		acpi_pcib_read_ivar),
    DEVMETHOD(bus_write_ivar,		acpi_pcib_write_ivar),
    DEVMETHOD(bus_alloc_resource,	acpi_pcib_acpi_alloc_resource),
#ifdef NEW_PCIB
    DEVMETHOD(bus_adjust_resource,	acpi_pcib_acpi_adjust_resource),
#else
    DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
#endif
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
    DEVMETHOD(bus_release_resource,	acpi_pcib_acpi_release_resource),
#else
    DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
#endif
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
    DEVMETHOD(bus_get_cpus,		acpi_pcib_get_cpus),
    DEVMETHOD(bus_get_dma_tag,		acpi_pcib_get_dma_tag),

    /* pcib interface */
    DEVMETHOD(pcib_maxslots,		pcib_maxslots),
    DEVMETHOD(pcib_read_config,		acpi_pcib_read_config),
    DEVMETHOD(pcib_write_config,	acpi_pcib_write_config),
    DEVMETHOD(pcib_route_interrupt,	acpi_pcib_acpi_route_interrupt),
    DEVMETHOD(pcib_alloc_msi,		acpi_pcib_alloc_msi),
    DEVMETHOD(pcib_release_msi,		pcib_release_msi),
    DEVMETHOD(pcib_alloc_msix,		acpi_pcib_alloc_msix),
    DEVMETHOD(pcib_release_msix,	pcib_release_msix),
    DEVMETHOD(pcib_map_msi,		acpi_pcib_map_msi),
    DEVMETHOD(pcib_power_for_sleep,	acpi_pcib_power_for_sleep),
    DEVMETHOD(pcib_request_feature,	acpi_pcib_request_feature),

    DEVMETHOD_END
};

static devclass_t pcib_devclass;

DEFINE_CLASS_0(pcib, acpi_pcib_acpi_driver, acpi_pcib_acpi_methods,
    sizeof(struct acpi_hpcib_softc));
DRIVER_MODULE(acpi_pcib, acpi, acpi_pcib_acpi_driver, pcib_devclass, 0, 0);
MODULE_DEPEND(acpi_pcib, acpi, 1, 1, 1);

static int
acpi_pcib_acpi_probe(device_t dev)
{
    ACPI_DEVICE_INFO	*devinfo;
    ACPI_HANDLE		h;
    int			root;

    if (acpi_disabled("pcib") || (h = acpi_get_handle(dev)) == NULL ||
	ACPI_FAILURE(AcpiGetObjectInfo(h, &devinfo)))
	return (ENXIO);
    root = (devinfo->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;
    AcpiOsFree(devinfo);
    if (!root || pci_cfgregopen() == 0)
	return (ENXIO);

    device_set_desc(dev, "ACPI Host-PCI bridge");
    return (0);
}

#ifdef NEW_PCIB
static ACPI_STATUS
acpi_pcib_producer_handler(ACPI_RESOURCE *res, void *context)
{
	struct acpi_hpcib_softc *sc;
	UINT64 length, min, max;
	u_int flags;
	int error, type;

	sc = context;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		panic("host bridge has depenedent resources");
	case ACPI_RESOURCE_TYPE_ADDRESS16:
	case ACPI_RESOURCE_TYPE_ADDRESS32:
	case ACPI_RESOURCE_TYPE_ADDRESS64:
	case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
		if (res->Data.Address.ProducerConsumer != ACPI_PRODUCER)
			break;
		switch (res->Type) {
		case ACPI_RESOURCE_TYPE_ADDRESS16:
			min = res->Data.Address16.Address.Minimum;
			max = res->Data.Address16.Address.Maximum;
			length = res->Data.Address16.Address.AddressLength;
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS32:
			min = res->Data.Address32.Address.Minimum;
			max = res->Data.Address32.Address.Maximum;
			length = res->Data.Address32.Address.AddressLength;
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS64:
			min = res->Data.Address64.Address.Minimum;
			max = res->Data.Address64.Address.Maximum;
			length = res->Data.Address64.Address.AddressLength;
			break;
		default:
			KASSERT(res->Type ==
			    ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64,
			    ("should never happen"));
			min = res->Data.ExtAddress64.Address.Minimum;
			max = res->Data.ExtAddress64.Address.Maximum;
			length = res->Data.ExtAddress64.Address.AddressLength;
			break;
		}
		if (length == 0)
			break;
		if (min + length - 1 != max &&
		    (res->Data.Address.MinAddressFixed != ACPI_ADDRESS_FIXED ||
		    res->Data.Address.MaxAddressFixed != ACPI_ADDRESS_FIXED))
			break;
		flags = 0;
		switch (res->Data.Address.ResourceType) {
		case ACPI_MEMORY_RANGE:
			type = SYS_RES_MEMORY;
			if (res->Type != ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64) {
				if (res->Data.Address.Info.Mem.Caching ==
				    ACPI_PREFETCHABLE_MEMORY)
					flags |= RF_PREFETCHABLE;
			} else {
				/*
				 * XXX: Parse prefetch flag out of
				 * TypeSpecific.
				 */
			}
			break;
		case ACPI_IO_RANGE:
			type = SYS_RES_IOPORT;
			break;
#ifdef PCI_RES_BUS
		case ACPI_BUS_NUMBER_RANGE:
			type = PCI_RES_BUS;
			break;
#endif
		default:
			return (AE_OK);
		}

		if (min + length - 1 != max)
			device_printf(sc->ap_dev,
			    "Length mismatch for %d range: %jx vs %jx\n", type,
			    (uintmax_t)(max - min + 1), (uintmax_t)length);
#ifdef __i386__
		if (min > ULONG_MAX) {
			device_printf(sc->ap_dev,
			    "Ignoring %d range above 4GB (%#jx-%#jx)\n",
			    type, (uintmax_t)min, (uintmax_t)max);
			break;
		}
		if (max > ULONG_MAX) {
			device_printf(sc->ap_dev,
       		    "Truncating end of %d range above 4GB (%#jx-%#jx)\n",
			    type, (uintmax_t)min, (uintmax_t)max);
			max = ULONG_MAX;
		}
#endif
		error = pcib_host_res_decodes(&sc->ap_host_res, type, min, max,
		    flags);
		if (error)
			panic("Failed to manage %d range (%#jx-%#jx): %d",
			    type, (uintmax_t)min, (uintmax_t)max, error);
		break;
	default:
		break;
	}
	return (AE_OK);
}
#endif

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
static int
first_decoded_bus(struct acpi_hpcib_softc *sc, rman_res_t *startp)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(&sc->ap_host_res.hr_rl, PCI_RES_BUS, 0);
	if (rle == NULL)
		return (ENXIO);
	*startp = rle->start;
	return (0);
}
#endif

static int
acpi_pcib_osc(struct acpi_hpcib_softc *sc, uint32_t osc_ctl)
{
	ACPI_STATUS status;
	uint32_t cap_set[3];

	static uint8_t pci_host_bridge_uuid[ACPI_UUID_LENGTH] = {
		0x5b, 0x4d, 0xdb, 0x33, 0xf7, 0x1f, 0x1c, 0x40,
		0x96, 0x57, 0x74, 0x41, 0xc0, 0x3d, 0xd7, 0x66
	};

	/*
	 * Don't invoke _OSC if a control is already granted.
	 * However, always invoke _OSC during attach when 0 is passed.
	 */
	if (osc_ctl != 0 && (sc->ap_osc_ctl & osc_ctl) == osc_ctl)
		return (0);

	/* Support Field: Extended PCI Config Space, MSI */
	cap_set[PCI_OSC_SUPPORT] = PCIM_OSC_SUPPORT_EXT_PCI_CONF |
	    PCIM_OSC_SUPPORT_MSI;

	/* Control Field */
	cap_set[PCI_OSC_CTL] = sc->ap_osc_ctl | osc_ctl;

	status = acpi_EvaluateOSC(sc->ap_handle, pci_host_bridge_uuid, 1,
	    nitems(cap_set), cap_set, cap_set, false);
	if (ACPI_FAILURE(status)) {
		if (status == AE_NOT_FOUND) {
			sc->ap_osc_ctl |= osc_ctl;
			return (0);
		}
		device_printf(sc->ap_dev, "_OSC failed: %s\n",
		    AcpiFormatException(status));
		return (EIO);
	}

	/*
	 * _OSC may return an error in the status word, but will
	 * update the control mask always.  _OSC should not revoke
	 * previously-granted controls.
	 */
	if ((cap_set[PCI_OSC_CTL] & sc->ap_osc_ctl) != sc->ap_osc_ctl)
		device_printf(sc->ap_dev, "_OSC revoked %#x\n",
		    (cap_set[PCI_OSC_CTL] & sc->ap_osc_ctl) ^ sc->ap_osc_ctl);
	sc->ap_osc_ctl = cap_set[PCI_OSC_CTL];
	if ((sc->ap_osc_ctl & osc_ctl) != osc_ctl)
		return (EIO);

	return (0);
}

static int
acpi_pcib_acpi_attach(device_t dev)
{
    struct acpi_hpcib_softc	*sc;
    ACPI_STATUS			status;
    static int bus0_seen = 0;
    u_int slot, func, busok;
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
    struct resource *bus_res;
    rman_res_t start;
    int rid;
#endif
    int error, domain;
    uint8_t busno;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    sc = device_get_softc(dev);
    sc->ap_dev = dev;
    sc->ap_handle = acpi_get_handle(dev);

    /*
     * Don't attach if we're not really there.
     */
    if (!acpi_DeviceIsPresent(dev))
	return (ENXIO);

    acpi_pcib_osc(sc, 0);

    /*
     * Get our segment number by evaluating _SEG.
     * It's OK for this to not exist.
     */
    status = acpi_GetInteger(sc->ap_handle, "_SEG", &sc->ap_segment);
    if (ACPI_FAILURE(status)) {
	if (status != AE_NOT_FOUND) {
	    device_printf(dev, "could not evaluate _SEG - %s\n",
		AcpiFormatException(status));
	    return_VALUE (ENXIO);
	}
	/* If it's not found, assume 0. */
	sc->ap_segment = 0;
    }

    /*
     * Get the address (device and function) of the associated
     * PCI-Host bridge device from _ADR.  Assume we don't have one if
     * it doesn't exist.
     */
    status = acpi_GetInteger(sc->ap_handle, "_ADR", &sc->ap_addr);
    if (ACPI_FAILURE(status)) {
	device_printf(dev, "could not evaluate _ADR - %s\n",
	    AcpiFormatException(status));
	sc->ap_addr = -1;
    }

#ifdef NEW_PCIB
    /*
     * Determine which address ranges this bridge decodes and setup
     * resource managers for those ranges.
     */
    if (pcib_host_res_init(sc->ap_dev, &sc->ap_host_res) != 0)
	    panic("failed to init hostb resources");
    if (!acpi_disabled("hostres")) {
	status = AcpiWalkResources(sc->ap_handle, "_CRS",
	    acpi_pcib_producer_handler, sc);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
	    device_printf(sc->ap_dev, "failed to parse resources: %s\n",
		AcpiFormatException(status));
    }
#endif

    /*
     * Get our base bus number by evaluating _BBN.
     * If this doesn't work, we assume we're bus number 0.
     *
     * XXX note that it may also not exist in the case where we are
     *     meant to use a private configuration space mechanism for this bus,
     *     so we should dig out our resources and check to see if we have
     *     anything like that.  How do we do this?
     * XXX If we have the requisite information, and if we don't think the
     *     default PCI configuration space handlers can deal with this bus,
     *     we should attach our own handler.
     * XXX invoke _REG on this for the PCI config space address space?
     * XXX It seems many BIOS's with multiple Host-PCI bridges do not set
     *     _BBN correctly.  They set _BBN to zero for all bridges.  Thus,
     *     if _BBN is zero and PCI bus 0 already exists, we try to read our
     *     bus number from the configuration registers at address _ADR.
     *     We only do this for domain/segment 0 in the hopes that this is
     *     only needed for old single-domain machines.
     */
    status = acpi_GetInteger(sc->ap_handle, "_BBN", &sc->ap_bus);
    if (ACPI_FAILURE(status)) {
	if (status != AE_NOT_FOUND) {
	    device_printf(dev, "could not evaluate _BBN - %s\n",
		AcpiFormatException(status));
	    return (ENXIO);
	} else {
	    /* If it's not found, assume 0. */
	    sc->ap_bus = 0;
	}
    }

    /*
     * If this is segment 0, the bus is zero, and PCI bus 0 already
     * exists, read the bus number via PCI config space.
     */
    busok = 1;
    if (sc->ap_segment == 0 && sc->ap_bus == 0 && bus0_seen) {
	busok = 0;
	if (sc->ap_addr != -1) {
	    /* XXX: We assume bus 0. */
	    slot = ACPI_ADR_PCI_SLOT(sc->ap_addr);
	    func = ACPI_ADR_PCI_FUNC(sc->ap_addr);
	    if (bootverbose)
		device_printf(dev, "reading config registers from 0:%d:%d\n",
		    slot, func);
	    if (host_pcib_get_busno(pci_cfgregread, 0, slot, func, &busno) == 0)
		device_printf(dev, "couldn't read bus number from cfg space\n");
	    else {
		sc->ap_bus = busno;
		busok = 1;
	    }
	}
    }

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
    /*
     * If nothing else worked, hope that ACPI at least lays out the
     * Host-PCI bridges in order and that as a result the next free
     * bus number is our bus number.
     */
    if (busok == 0) {
	    /*
	     * If we have a region of bus numbers, use the first
	     * number for our bus.
	     */
	    if (first_decoded_bus(sc, &start) == 0)
		    sc->ap_bus = start;
	    else {
		    rid = 0;
		    bus_res = pci_domain_alloc_bus(sc->ap_segment, dev, &rid, 0,
			PCI_BUSMAX, 1, 0);
		    if (bus_res == NULL) {
			    device_printf(dev,
				"could not allocate bus number\n");
			    pcib_host_res_free(dev, &sc->ap_host_res);
			    return (ENXIO);
		    }
		    sc->ap_bus = rman_get_start(bus_res);
		    pci_domain_release_bus(sc->ap_segment, dev, rid, bus_res);
	    }
    } else {
	    /*
	     * Require the bus number from _BBN to match the start of any
	     * decoded range.
	     */
	    if (first_decoded_bus(sc, &start) == 0 && sc->ap_bus != start) {
		    device_printf(dev,
		"bus number %d does not match start of decoded range %ju\n",
			sc->ap_bus, (uintmax_t)start);
		    pcib_host_res_free(dev, &sc->ap_host_res);
		    return (ENXIO);
	    }
    }
#else
    /*
     * If nothing else worked, hope that ACPI at least lays out the
     * host-PCI bridges in order and that as a result our unit number
     * is actually our bus number.  There are several reasons this
     * might not be true.
     */
    if (busok == 0) {
	sc->ap_bus = device_get_unit(dev);
	device_printf(dev, "trying bus number %d\n", sc->ap_bus);
    }
#endif

    /* If this is bus 0 on segment 0, note that it has been seen already. */
    if (sc->ap_segment == 0 && sc->ap_bus == 0)
	    bus0_seen = 1;

    acpi_pcib_fetch_prt(dev, &sc->ap_prt);

    error = bus_dma_tag_create(bus_get_dma_tag(dev), 1,
	0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	NULL, NULL, BUS_SPACE_MAXSIZE, BUS_SPACE_UNRESTRICTED,
	BUS_SPACE_MAXSIZE, 0, NULL, NULL, &sc->ap_dma_tag);
    if (error != 0)
	goto errout;
    error = bus_get_domain(dev, &domain);
    if (error == 0)
	error = bus_dma_tag_set_domain(sc->ap_dma_tag, domain);
    /* Don't fail to attach if the domain can't be queried or set. */
    error = 0;

    bus_generic_probe(dev);
    if (device_add_child(dev, "pci", -1) == NULL) {
	bus_dma_tag_destroy(sc->ap_dma_tag);
	sc->ap_dma_tag = NULL;
	error = ENXIO;
	goto errout;
    }
    return (bus_generic_attach(dev));

errout:
    device_printf(device_get_parent(dev), "couldn't attach pci bus\n");
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
    pcib_host_res_free(dev, &sc->ap_host_res);
#endif
    return (error);
}

/*
 * Support for standard PCI bridge ivars.
 */
static int
acpi_pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
    struct acpi_hpcib_softc	*sc = device_get_softc(dev);

    switch (which) {
    case PCIB_IVAR_DOMAIN:
	*result = sc->ap_segment;
	return (0);
    case PCIB_IVAR_BUS:
	*result = sc->ap_bus;
	return (0);
    case ACPI_IVAR_HANDLE:
	*result = (uintptr_t)sc->ap_handle;
	return (0);
    case ACPI_IVAR_FLAGS:
	*result = (uintptr_t)sc->ap_flags;
	return (0);
    }
    return (ENOENT);
}

static int
acpi_pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
    struct acpi_hpcib_softc	*sc = device_get_softc(dev);

    switch (which) {
    case PCIB_IVAR_DOMAIN:
	return (EINVAL);
    case PCIB_IVAR_BUS:
	sc->ap_bus = value;
	return (0);
    case ACPI_IVAR_HANDLE:
	sc->ap_handle = (ACPI_HANDLE)value;
	return (0);
    case ACPI_IVAR_FLAGS:
	sc->ap_flags = (int)value;
	return (0);
    }
    return (ENOENT);
}

static uint32_t
acpi_pcib_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
    return (pci_cfgregread(bus, slot, func, reg, bytes));
}

static void
acpi_pcib_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t data, int bytes)
{
    pci_cfgregwrite(bus, slot, func, reg, data, bytes);
}

static int
acpi_pcib_acpi_route_interrupt(device_t pcib, device_t dev, int pin)
{
    struct acpi_hpcib_softc *sc = device_get_softc(pcib);

    return (acpi_pcib_route_interrupt(pcib, dev, pin, &sc->ap_prt));
}

static int
acpi_pcib_alloc_msi(device_t pcib, device_t dev, int count, int maxcount,
    int *irqs)
{
	device_t bus;

	bus = device_get_parent(pcib);
	return (PCIB_ALLOC_MSI(device_get_parent(bus), dev, count, maxcount,
	    irqs));
}

static int
acpi_pcib_alloc_msix(device_t pcib, device_t dev, int *irq)
{
	device_t bus;

	bus = device_get_parent(pcib);
	return (PCIB_ALLOC_MSIX(device_get_parent(bus), dev, irq));
}

static int
acpi_pcib_map_msi(device_t pcib, device_t dev, int irq, uint64_t *addr,
    uint32_t *data)
{
	struct acpi_hpcib_softc *sc;
	device_t bus, hostb;
	int error;

	bus = device_get_parent(pcib);
	error = PCIB_MAP_MSI(device_get_parent(bus), dev, irq, addr, data);
	if (error)
		return (error);

	sc = device_get_softc(pcib);
	if (sc->ap_addr == -1)
		return (0);
	/* XXX: Assumes all bridges are on bus 0. */
	hostb = pci_find_dbsf(sc->ap_segment, 0, ACPI_ADR_PCI_SLOT(sc->ap_addr),
	    ACPI_ADR_PCI_FUNC(sc->ap_addr));
	if (hostb != NULL)
		pci_ht_map_msi(hostb, *addr);
	return (0);
}

struct resource *
acpi_pcib_acpi_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
#ifdef NEW_PCIB
    struct acpi_hpcib_softc *sc;
    struct resource *res;
#endif

#if defined(__i386__) || defined(__amd64__)
    start = hostb_alloc_start(type, start, end, count);
#endif

#ifdef NEW_PCIB
    sc = device_get_softc(dev);
#ifdef PCI_RES_BUS
    if (type == PCI_RES_BUS)
	return (pci_domain_alloc_bus(sc->ap_segment, child, rid, start, end,
	    count, flags));
#endif
    res = pcib_host_res_alloc(&sc->ap_host_res, child, type, rid, start, end,
	count, flags);

    /*
     * XXX: If this is a request for a specific range, assume it is
     * correct and pass it up to the parent.  What we probably want to
     * do long-term is explicitly trust any firmware-configured
     * resources during the initial bus scan on boot and then disable
     * this after that.
     */
    if (res == NULL && start + count - 1 == end)
	res = bus_generic_alloc_resource(dev, child, type, rid, start, end,
	    count, flags);
    return (res);
#else
    return (bus_generic_alloc_resource(dev, child, type, rid, start, end,
	count, flags));
#endif
}

#ifdef NEW_PCIB
int
acpi_pcib_acpi_adjust_resource(device_t dev, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct acpi_hpcib_softc *sc;

	sc = device_get_softc(dev);
#ifdef PCI_RES_BUS
	if (type == PCI_RES_BUS)
		return (pci_domain_adjust_bus(sc->ap_segment, child, r, start,
		    end));
#endif
	return (pcib_host_res_adjust(&sc->ap_host_res, child, type, r, start,
	    end));
}

#ifdef PCI_RES_BUS
int
acpi_pcib_acpi_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct acpi_hpcib_softc *sc;

	sc = device_get_softc(dev);
	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(sc->ap_segment, child, rid, r));
	return (bus_generic_release_resource(dev, child, type, rid, r));
}
#endif
#endif

static int
acpi_pcib_request_feature(device_t pcib, device_t dev, enum pci_feature feature)
{
	uint32_t osc_ctl;
	struct acpi_hpcib_softc *sc;

	sc = device_get_softc(pcib);

	switch (feature) {
	case PCI_FEATURE_HP:
		osc_ctl = PCIM_OSC_CTL_PCIE_HP;
		break;
	case PCI_FEATURE_AER:
		osc_ctl = PCIM_OSC_CTL_PCIE_AER;
		break;
	default:
		return (EINVAL);
	}

	return (acpi_pcib_osc(sc, osc_ctl));
}

static bus_dma_tag_t
acpi_pcib_get_dma_tag(device_t bus, device_t child)
{
	struct acpi_hpcib_softc *sc;

	sc = device_get_softc(bus);

	return (sc->ap_dma_tag);
}
