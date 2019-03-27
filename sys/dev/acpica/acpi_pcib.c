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
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_pcibvar.h>

#include <dev/pci/pcivar.h>
#include "pcib_if.h"

/* Hooks for the ACPI CA debugging infrastructure. */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("PCI")

ACPI_SERIAL_DECL(pcib, "ACPI PCI bus methods");

/*
 * For locking, we assume the caller is not concurrent since this is
 * triggered by newbus methods.
 */

struct prt_lookup_request {
    ACPI_PCI_ROUTING_TABLE *pr_entry;
    u_int	pr_pin;
    u_int	pr_slot;
};

typedef	void prt_entry_handler(ACPI_PCI_ROUTING_TABLE *entry, void *arg);

static void	prt_attach_devices(ACPI_PCI_ROUTING_TABLE *entry, void *arg);
static void	prt_lookup_device(ACPI_PCI_ROUTING_TABLE *entry, void *arg);
static void	prt_walk_table(ACPI_BUFFER *prt, prt_entry_handler *handler,
		    void *arg);

static void
prt_walk_table(ACPI_BUFFER *prt, prt_entry_handler *handler, void *arg)
{
    ACPI_PCI_ROUTING_TABLE *entry;
    char *prtptr;

    /* First check to see if there is a table to walk. */
    if (prt == NULL || prt->Pointer == NULL)
	return;

    /* Walk the table executing the handler function for each entry. */
    prtptr = prt->Pointer;
    entry = (ACPI_PCI_ROUTING_TABLE *)prtptr;
    while (entry->Length != 0) {
	handler(entry, arg);
	prtptr += entry->Length;
	entry = (ACPI_PCI_ROUTING_TABLE *)prtptr;
    }
}

static void
prt_attach_devices(ACPI_PCI_ROUTING_TABLE *entry, void *arg)
{
    ACPI_HANDLE handle;
    device_t child, pcib;
    int error;

    /* We only care about entries that reference a link device. */
    if (entry->Source[0] == '\0')
	return;

    /*
     * In practice, we only see SourceIndex's of 0 out in the wild.
     * When indices != 0 have been found, they've been bugs in the ASL.
     */
    if (entry->SourceIndex != 0)
	return;

    /* Lookup the associated handle and device. */
    pcib = (device_t)arg;
    if (ACPI_FAILURE(AcpiGetHandle(ACPI_ROOT_OBJECT, entry->Source, &handle)))
	return;
    child = acpi_get_device(handle);
    if (child == NULL)
	return;

    /* If the device hasn't been probed yet, force it to do so. */
    error = device_probe_and_attach(child);
    if (error != 0) {
	device_printf(pcib, "failed to force attach of %s\n",
	    acpi_name(handle));
	return;
    }

    /* Add a reference for a specific bus/device/pin tuple. */
    acpi_pci_link_add_reference(child, entry->SourceIndex, pcib,
	ACPI_ADR_PCI_SLOT(entry->Address), entry->Pin);
}

void
acpi_pcib_fetch_prt(device_t dev, ACPI_BUFFER *prt)
{
    ACPI_STATUS status;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    /*
     * Get the PCI interrupt routing table for this bus.  If we can't
     * get it, this is not an error but may reduce functionality.  There
     * are several valid bridges in the field that do not have a _PRT, so
     * only warn about missing tables if bootverbose is set.
     */
    prt->Length = ACPI_ALLOCATE_BUFFER;
    status = AcpiGetIrqRoutingTable(acpi_get_handle(dev), prt);
    if (ACPI_FAILURE(status) && (bootverbose || status != AE_NOT_FOUND))
	device_printf(dev,
	    "could not get PCI interrupt routing table for %s - %s\n",
	    acpi_name(acpi_get_handle(dev)), AcpiFormatException(status));

    /*
     * Ensure all the link devices are attached.
     */
    prt_walk_table(prt, prt_attach_devices, dev);
}

static void
prt_lookup_device(ACPI_PCI_ROUTING_TABLE *entry, void *arg)
{
    struct prt_lookup_request *pr;

    pr = (struct prt_lookup_request *)arg;
    if (pr->pr_entry != NULL)
	return;

    /*
     * Compare the slot number (high word of Address) and pin number
     * (note that ACPI uses 0 for INTA) to check for a match.
     *
     * Note that the low word of the Address field (function number)
     * is required by the specification to be 0xffff.  We don't risk
     * checking it here.
     */
    if (ACPI_ADR_PCI_SLOT(entry->Address) == pr->pr_slot &&
	entry->Pin == pr->pr_pin)
	    pr->pr_entry = entry;
}

/*
 * Route an interrupt for a child of the bridge.
 */
int
acpi_pcib_route_interrupt(device_t pcib, device_t dev, int pin,
    ACPI_BUFFER *prtbuf)
{
    ACPI_PCI_ROUTING_TABLE *prt;
    struct prt_lookup_request pr;
    ACPI_HANDLE lnkdev;
    int interrupt;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    lnkdev = NULL;
    interrupt = PCI_INVALID_IRQ;

    /* ACPI numbers pins 0-3, not 1-4 like the BIOS. */
    pin--;

    ACPI_SERIAL_BEGIN(pcib);

    /* Search for a matching entry in the routing table. */
    pr.pr_entry = NULL;
    pr.pr_pin = pin;
    pr.pr_slot = pci_get_slot(dev);
    prt_walk_table(prtbuf, prt_lookup_device, &pr);
    if (pr.pr_entry == NULL) {
	device_printf(pcib, "no PRT entry for %d.%d.INT%c\n", pci_get_bus(dev),
	    pci_get_slot(dev), 'A' + pin);
	goto out;
    }
    prt = pr.pr_entry;

    if (bootverbose) {
	device_printf(pcib, "matched entry for %d.%d.INT%c",
	    pci_get_bus(dev), pci_get_slot(dev), 'A' + pin);
	if (prt->Source[0] != '\0')
		printf(" (src %s:%u)", prt->Source, prt->SourceIndex);
	printf("\n");
    }

    /*
     * If source is empty/NULL, the source index is a global IRQ number
     * and it's hard-wired so we're done.
     *
     * XXX: If the source index is non-zero, ignore the source device and
     * assume that this is a hard-wired entry.
     */
    if (prt->Source[0] == '\0' || prt->SourceIndex != 0) {
	if (bootverbose)
	    device_printf(pcib, "slot %d INT%c hardwired to IRQ %d\n",
		pci_get_slot(dev), 'A' + pin, prt->SourceIndex);
	if (prt->SourceIndex) {
	    interrupt = prt->SourceIndex;
	    BUS_CONFIG_INTR(dev, interrupt, INTR_TRIGGER_LEVEL,
		INTR_POLARITY_LOW);
	} else
	    device_printf(pcib, "error: invalid hard-wired IRQ of 0\n");
	goto out;
    }

    /*
     * We have to find the source device (PCI interrupt link device).
     */
    if (ACPI_FAILURE(AcpiGetHandle(ACPI_ROOT_OBJECT, prt->Source, &lnkdev))) {
	device_printf(pcib, "couldn't find PCI interrupt link device %s\n",
	    prt->Source);
	goto out;
    }
    interrupt = acpi_pci_link_route_interrupt(acpi_get_device(lnkdev),
	prt->SourceIndex);

    if (bootverbose && PCI_INTERRUPT_VALID(interrupt))
	device_printf(pcib, "slot %d INT%c routed to irq %d via %s\n",
	    pci_get_slot(dev), 'A' + pin, interrupt, acpi_name(lnkdev));

out:
    ACPI_SERIAL_END(pcib);
#ifdef INTRNG
    if (PCI_INTERRUPT_VALID(interrupt)) {
	interrupt  = acpi_map_intr(dev, interrupt, lnkdev);
	KASSERT(PCI_INTERRUPT_VALID(interrupt), ("mapping fail"));
    }
#endif
    return_VALUE(interrupt);
}

int
acpi_pcib_power_for_sleep(device_t pcib, device_t dev, int *pstate)
{
    device_t acpi_dev;

    acpi_dev = devclass_get_device(devclass_find("acpi"), 0);
    acpi_device_pwr_for_sleep(acpi_dev, dev, pstate);
    return (0);
}

int
acpi_pcib_get_cpus(device_t pcib, device_t dev, enum cpu_sets op,
    size_t setsize, cpuset_t *cpuset)
{

	return (bus_get_cpus(pcib, op, setsize, cpuset));
}
