/*-
 * Copyright (c) 2002 Mitsuru IWASAKI <iwasaki@jp.freebsd.org>
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
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_pcibvar.h>

#include <machine/pci_cfgreg.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include "pcib_if.h"

/* Hooks for the ACPI CA debugging infrastructure. */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("PCI_LINK")

ACPI_SERIAL_DECL(pci_link, "ACPI PCI link");

#define NUM_ISA_INTERRUPTS	16
#define NUM_ACPI_INTERRUPTS	256

/*
 * An ACPI PCI link device may contain multiple links.  Each link has its
 * own ACPI resource.  _PRT entries specify which link is being used via
 * the Source Index.
 *
 * XXX: A note about Source Indices and DPFs:  Currently we assume that
 * the DPF start and end tags are not counted towards the index that
 * Source Index corresponds to.  Also, we assume that when DPFs are in use
 * they various sets overlap in terms of Indices.  Here's an example
 * resource list indicating these assumptions:
 *
 * Resource		Index
 * --------		-----
 * I/O Port		0
 * Start DPF		-
 * IRQ			1
 * MemIO		2
 * Start DPF		-
 * IRQ			1
 * MemIO		2
 * End DPF		-
 * DMA Channel		3
 *
 * The XXX is because I'm not sure if this is a valid assumption to make.
 */

/* States during DPF processing. */
#define	DPF_OUTSIDE	0
#define	DPF_FIRST	1
#define	DPF_IGNORE	2

struct link;

struct acpi_pci_link_softc {
	int	pl_num_links;
	int	pl_crs_bad;
	struct link *pl_links;
	device_t pl_dev;
};

struct link {
	struct acpi_pci_link_softc *l_sc;
	uint8_t	l_bios_irq;
	uint8_t	l_irq;
	uint8_t	l_initial_irq;
	UINT32	l_crs_type;
	int	l_res_index;
	int	l_num_irqs;
	int	*l_irqs;
	int	l_references;
	int	l_routed:1;
	int	l_isa_irq:1;
	ACPI_RESOURCE l_prs_template;
};

struct link_count_request {
	int	in_dpf;
	int	count;
};

struct link_res_request {
	struct acpi_pci_link_softc *sc;
	int	in_dpf;
	int	res_index;
	int	link_index;
};

static MALLOC_DEFINE(M_PCI_LINK, "pci_link", "ACPI PCI Link structures");

static int pci_link_interrupt_weights[NUM_ACPI_INTERRUPTS];
static int pci_link_bios_isa_irqs;

static char *pci_link_ids[] = { "PNP0C0F", NULL };

/*
 * Fetch the short name associated with an ACPI handle and save it in the
 * passed in buffer.
 */
static ACPI_STATUS
acpi_short_name(ACPI_HANDLE handle, char *buffer, size_t buflen)
{
	ACPI_BUFFER buf;

	buf.Length = buflen;
	buf.Pointer = buffer;
	return (AcpiGetName(handle, ACPI_SINGLE_NAME, &buf));
}

static int
acpi_pci_link_probe(device_t dev)
{
	char descr[28], name[12];
	int rv;

	/*
	 * We explicitly do not check _STA since not all systems set it to
	 * sensible values.
	 */
	if (acpi_disabled("pci_link"))
	    return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, pci_link_ids, NULL);
	if (rv > 0)
	  return (rv);
	
	if (ACPI_SUCCESS(acpi_short_name(acpi_get_handle(dev), name,
	    sizeof(name)))) {
		snprintf(descr, sizeof(descr), "ACPI PCI Link %s", name);
		device_set_desc_copy(dev, descr);
	} else
		device_set_desc(dev, "ACPI PCI Link");
	device_quiet(dev);
	return (rv);
}

static ACPI_STATUS
acpi_count_irq_resources(ACPI_RESOURCE *res, void *context)
{
	struct link_count_request *req;

	req = (struct link_count_request *)context;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		switch (req->in_dpf) {
		case DPF_OUTSIDE:
			/* We've started the first DPF. */
			req->in_dpf = DPF_FIRST;
			break;
		case DPF_FIRST:
			/* We've started the second DPF. */
			req->in_dpf = DPF_IGNORE;
			break;
		}
		break;
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		/* We are finished with DPF parsing. */
		KASSERT(req->in_dpf != DPF_OUTSIDE,
		    ("%s: end dpf when not parsing a dpf", __func__));
		req->in_dpf = DPF_OUTSIDE;
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		/*
		 * Don't count resources if we are in a DPF set that we are
		 * ignoring.
		 */
		if (req->in_dpf != DPF_IGNORE)
			req->count++;
	}
	return (AE_OK);
}

static ACPI_STATUS
link_add_crs(ACPI_RESOURCE *res, void *context)
{
	struct link_res_request *req;
	struct link *link;

	ACPI_SERIAL_ASSERT(pci_link);
	req = (struct link_res_request *)context;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		switch (req->in_dpf) {
		case DPF_OUTSIDE:
			/* We've started the first DPF. */
			req->in_dpf = DPF_FIRST;
			break;
		case DPF_FIRST:
			/* We've started the second DPF. */
			panic(
		"%s: Multiple dependent functions within a current resource",
			    __func__);
			break;
		}
		break;
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		/* We are finished with DPF parsing. */
		KASSERT(req->in_dpf != DPF_OUTSIDE,
		    ("%s: end dpf when not parsing a dpf", __func__));
		req->in_dpf = DPF_OUTSIDE;
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		KASSERT(req->link_index < req->sc->pl_num_links,
		    ("%s: array boundary violation", __func__));
		link = &req->sc->pl_links[req->link_index];
		link->l_res_index = req->res_index;
		link->l_crs_type = res->Type;
		req->link_index++;
		req->res_index++;

		/*
		 * Only use the current value if there's one IRQ.  Some
		 * systems return multiple IRQs (which is nonsense for _CRS)
		 * when the link hasn't been programmed.
		 */
		if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
			if (res->Data.Irq.InterruptCount == 1)
				link->l_irq = res->Data.Irq.Interrupts[0];
		} else if (res->Data.ExtendedIrq.InterruptCount == 1)
			link->l_irq = res->Data.ExtendedIrq.Interrupts[0];

		/*
		 * An IRQ of zero means that the link isn't routed.
		 */
		if (link->l_irq == 0)
			link->l_irq = PCI_INVALID_IRQ;
		break;
	default:
		req->res_index++;
	}
	return (AE_OK);
}

/*
 * Populate the set of possible IRQs for each device.
 */
static ACPI_STATUS
link_add_prs(ACPI_RESOURCE *res, void *context)
{
	ACPI_RESOURCE *tmp;
	struct link_res_request *req;
	struct link *link;
	UINT8 *irqs = NULL;
	UINT32 *ext_irqs = NULL;
	int i, is_ext_irq = 1;

	ACPI_SERIAL_ASSERT(pci_link);
	req = (struct link_res_request *)context;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		switch (req->in_dpf) {
		case DPF_OUTSIDE:
			/* We've started the first DPF. */
			req->in_dpf = DPF_FIRST;
			break;
		case DPF_FIRST:
			/* We've started the second DPF. */
			req->in_dpf = DPF_IGNORE;
			break;
		}
		break;
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		/* We are finished with DPF parsing. */
		KASSERT(req->in_dpf != DPF_OUTSIDE,
		    ("%s: end dpf when not parsing a dpf", __func__));
		req->in_dpf = DPF_OUTSIDE;
		break;
	case ACPI_RESOURCE_TYPE_IRQ:
		is_ext_irq = 0;
		/* fall through */
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		/*
		 * Don't parse resources if we are in a DPF set that we are
		 * ignoring.
		 */
		if (req->in_dpf == DPF_IGNORE)
			break;

		KASSERT(req->link_index < req->sc->pl_num_links,
		    ("%s: array boundary violation", __func__));
		link = &req->sc->pl_links[req->link_index];
		if (link->l_res_index == -1) {
			KASSERT(req->sc->pl_crs_bad,
			    ("res_index should be set"));
			link->l_res_index = req->res_index;
		}
		req->link_index++;
		req->res_index++;

		/*
		 * Stash a copy of the resource for later use when doing
		 * _SRS.
		 */
		tmp = &link->l_prs_template;
		if (is_ext_irq) {
			bcopy(res, tmp, ACPI_RS_SIZE(tmp->Data.ExtendedIrq));

			/*
			 * XXX acpi_AppendBufferResource() cannot handle
			 * optional data.
			 */
			bzero(&tmp->Data.ExtendedIrq.ResourceSource,
			    sizeof(tmp->Data.ExtendedIrq.ResourceSource));
			tmp->Length = ACPI_RS_SIZE(tmp->Data.ExtendedIrq);

			link->l_num_irqs =
			    res->Data.ExtendedIrq.InterruptCount;
			ext_irqs = res->Data.ExtendedIrq.Interrupts;
		} else {
			bcopy(res, tmp, ACPI_RS_SIZE(tmp->Data.Irq));
			link->l_num_irqs = res->Data.Irq.InterruptCount;
			irqs = res->Data.Irq.Interrupts;
		}
		if (link->l_num_irqs == 0)
			break;

		/*
		 * Save a list of the valid IRQs.  Also, if all of the
		 * valid IRQs are ISA IRQs, then mark this link as
		 * routed via an ISA interrupt.
		 */
		link->l_isa_irq = TRUE;
		link->l_irqs = malloc(sizeof(int) * link->l_num_irqs,
		    M_PCI_LINK, M_WAITOK | M_ZERO);
		for (i = 0; i < link->l_num_irqs; i++) {
			if (is_ext_irq) {
				link->l_irqs[i] = ext_irqs[i];
				if (ext_irqs[i] >= NUM_ISA_INTERRUPTS)
					link->l_isa_irq = FALSE;
			} else {
				link->l_irqs[i] = irqs[i];
				if (irqs[i] >= NUM_ISA_INTERRUPTS)
					link->l_isa_irq = FALSE;
			}
		}

		/*
		 * If this is not an ISA IRQ but _CRS used a non-extended
		 * IRQ descriptor, don't use _CRS as a template for _SRS.
		 */
		if (!req->sc->pl_crs_bad && !link->l_isa_irq &&
		    link->l_crs_type == ACPI_RESOURCE_TYPE_IRQ)
			req->sc->pl_crs_bad = TRUE;
		break;
	default:
		if (req->in_dpf == DPF_IGNORE)
			break;
		if (req->sc->pl_crs_bad)
			device_printf(req->sc->pl_dev,
		    "Warning: possible resource %d will be lost during _SRS\n",
			    req->res_index);
		req->res_index++;
	}
	return (AE_OK);
}

static int
link_valid_irq(struct link *link, int irq)
{
	int i;

	ACPI_SERIAL_ASSERT(pci_link);

	/* Invalid interrupts are never valid. */
	if (!PCI_INTERRUPT_VALID(irq))
		return (FALSE);

	/* Any interrupt in the list of possible interrupts is valid. */
	for (i = 0; i < link->l_num_irqs; i++)
		if (link->l_irqs[i] == irq)
			 return (TRUE);

	/*
	 * For links routed via an ISA interrupt, if the SCI is routed via
	 * an ISA interrupt, the SCI is always treated as a valid IRQ.
	 */
	if (link->l_isa_irq && AcpiGbl_FADT.SciInterrupt == irq &&
	    irq < NUM_ISA_INTERRUPTS)
		return (TRUE);

	/* If the interrupt wasn't found in the list it is not valid. */
	return (FALSE);
}

static void
acpi_pci_link_dump(struct acpi_pci_link_softc *sc, int header, const char *tag)
{
	struct link *link;
	char buf[16];
	int i, j;

	ACPI_SERIAL_ASSERT(pci_link);
	if (header) {
		snprintf(buf, sizeof(buf), "%s:",
		    device_get_nameunit(sc->pl_dev));
		printf("%-16.16s  Index  IRQ  Rtd  Ref  IRQs\n", buf);
	}
	for (i = 0; i < sc->pl_num_links; i++) {
		link = &sc->pl_links[i];
		printf("  %-14.14s  %5d  %3d   %c   %3d ", i == 0 ? tag : "", i,
		    link->l_irq, link->l_routed ? 'Y' : 'N',
		    link->l_references);
		if (link->l_num_irqs == 0)
			printf(" none");
		else for (j = 0; j < link->l_num_irqs; j++)
			printf(" %d", link->l_irqs[j]);
		printf("\n");
	}
}

static int
acpi_pci_link_attach(device_t dev)
{
	struct acpi_pci_link_softc *sc;
	struct link_count_request creq;
	struct link_res_request rreq;
	ACPI_STATUS status;
	int i;

	sc = device_get_softc(dev);
	sc->pl_dev = dev;
	ACPI_SERIAL_BEGIN(pci_link);

	/*
	 * Count the number of current resources so we know how big of
	 * a link array to allocate.  On some systems, _CRS is broken,
	 * so for those systems try to derive the count from _PRS instead.
	 */
	creq.in_dpf = DPF_OUTSIDE;
	creq.count = 0;
	status = AcpiWalkResources(acpi_get_handle(dev), "_CRS",
	    acpi_count_irq_resources, &creq);
	sc->pl_crs_bad = ACPI_FAILURE(status);
	if (sc->pl_crs_bad) {
		creq.in_dpf = DPF_OUTSIDE;
		creq.count = 0;
		status = AcpiWalkResources(acpi_get_handle(dev), "_PRS",
		    acpi_count_irq_resources, &creq);
		if (ACPI_FAILURE(status)) {
			device_printf(dev,
			    "Unable to parse _CRS or _PRS: %s\n",
			    AcpiFormatException(status));
			ACPI_SERIAL_END(pci_link);
			return (ENXIO);
		}
	}
	sc->pl_num_links = creq.count;
	if (creq.count == 0) {
		ACPI_SERIAL_END(pci_link);
		return (0);
	}
	sc->pl_links = malloc(sizeof(struct link) * sc->pl_num_links,
	    M_PCI_LINK, M_WAITOK | M_ZERO);

	/* Initialize the child links. */
	for (i = 0; i < sc->pl_num_links; i++) {
		sc->pl_links[i].l_irq = PCI_INVALID_IRQ;
		sc->pl_links[i].l_bios_irq = PCI_INVALID_IRQ;
		sc->pl_links[i].l_sc = sc;
		sc->pl_links[i].l_isa_irq = FALSE;
		sc->pl_links[i].l_res_index = -1;
	}

	/* Try to read the current settings from _CRS if it is valid. */
	if (!sc->pl_crs_bad) {
		rreq.in_dpf = DPF_OUTSIDE;
		rreq.link_index = 0;
		rreq.res_index = 0;
		rreq.sc = sc;
		status = AcpiWalkResources(acpi_get_handle(dev), "_CRS",
		    link_add_crs, &rreq);
		if (ACPI_FAILURE(status)) {
			device_printf(dev, "Unable to parse _CRS: %s\n",
			    AcpiFormatException(status));
			goto fail;
		}
	}

	/*
	 * Try to read the possible settings from _PRS.  Note that if the
	 * _CRS is toast, we depend on having a working _PRS.  However, if
	 * _CRS works, then it is ok for _PRS to be missing.
	 */
	rreq.in_dpf = DPF_OUTSIDE;
	rreq.link_index = 0;
	rreq.res_index = 0;
	rreq.sc = sc;
	status = AcpiWalkResources(acpi_get_handle(dev), "_PRS",
	    link_add_prs, &rreq);
	if (ACPI_FAILURE(status) &&
	    (status != AE_NOT_FOUND || sc->pl_crs_bad)) {
		device_printf(dev, "Unable to parse _PRS: %s\n",
		    AcpiFormatException(status));
		goto fail;
	}
	if (bootverbose)
		acpi_pci_link_dump(sc, 1, "Initial Probe");

	/* Verify initial IRQs if we have _PRS. */
	if (status != AE_NOT_FOUND)
		for (i = 0; i < sc->pl_num_links; i++)
			if (!link_valid_irq(&sc->pl_links[i],
			    sc->pl_links[i].l_irq))
				sc->pl_links[i].l_irq = PCI_INVALID_IRQ;
	if (bootverbose)
		acpi_pci_link_dump(sc, 0, "Validation");

	/* Save initial IRQs. */
	for (i = 0; i < sc->pl_num_links; i++)
		sc->pl_links[i].l_initial_irq = sc->pl_links[i].l_irq;

	/*
	 * Try to disable this link.  If successful, set the current IRQ to
	 * zero and flags to indicate this link is not routed.  If we can't
	 * run _DIS (i.e., the method doesn't exist), assume the initial
	 * IRQ was routed by the BIOS.
	 */
	if (ACPI_SUCCESS(AcpiEvaluateObject(acpi_get_handle(dev), "_DIS", NULL,
	    NULL)))
		for (i = 0; i < sc->pl_num_links; i++)
			sc->pl_links[i].l_irq = PCI_INVALID_IRQ;
	else
		for (i = 0; i < sc->pl_num_links; i++)
			if (PCI_INTERRUPT_VALID(sc->pl_links[i].l_irq))
				sc->pl_links[i].l_routed = TRUE;
	if (bootverbose)
		acpi_pci_link_dump(sc, 0, "After Disable");
	ACPI_SERIAL_END(pci_link);
	return (0);
fail:
	ACPI_SERIAL_END(pci_link);
	for (i = 0; i < sc->pl_num_links; i++)
		if (sc->pl_links[i].l_irqs != NULL)
			free(sc->pl_links[i].l_irqs, M_PCI_LINK);
	free(sc->pl_links, M_PCI_LINK);
	return (ENXIO);
}

/* XXX: Note that this is identical to pci_pir_search_irq(). */
static uint8_t
acpi_pci_link_search_irq(int bus, int device, int pin)
{
	uint32_t value;
	uint8_t func, maxfunc;

	/* See if we have a valid device at function 0. */
	value = pci_cfgregread(bus, device, 0, PCIR_HDRTYPE, 1);
	if ((value & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
		return (PCI_INVALID_IRQ);
	if (value & PCIM_MFDEV)
		maxfunc = PCI_FUNCMAX;
	else
		maxfunc = 0;

	/* Scan all possible functions at this device. */
	for (func = 0; func <= maxfunc; func++) {
		value = pci_cfgregread(bus, device, func, PCIR_DEVVENDOR, 4);
		if (value == 0xffffffff)
			continue;
		value = pci_cfgregread(bus, device, func, PCIR_INTPIN, 1);

		/*
		 * See if it uses the pin in question.  Note that the passed
		 * in pin uses 0 for A, .. 3 for D whereas the intpin
		 * register uses 0 for no interrupt, 1 for A, .. 4 for D.
		 */
		if (value != pin + 1)
			continue;
		value = pci_cfgregread(bus, device, func, PCIR_INTLINE, 1);
		if (bootverbose)
			printf(
		"ACPI: Found matching pin for %d.%d.INT%c at func %d: %d\n",
			    bus, device, pin + 'A', func, value);
		if (value != PCI_INVALID_IRQ)
			return (value);
	}
	return (PCI_INVALID_IRQ);
}

/*
 * Find the link structure that corresponds to the resource index passed in
 * via 'source_index'.
 */
static struct link *
acpi_pci_link_lookup(device_t dev, int source_index)
{
	struct acpi_pci_link_softc *sc;
	int i;

	ACPI_SERIAL_ASSERT(pci_link);
	sc = device_get_softc(dev);
	for (i = 0; i < sc->pl_num_links; i++)
		if (sc->pl_links[i].l_res_index == source_index)
			return (&sc->pl_links[i]);
	return (NULL);
}

void
acpi_pci_link_add_reference(device_t dev, int index, device_t pcib, int slot,
    int pin)
{
	struct link *link;
	uint8_t bios_irq;
	uintptr_t bus;

	/*
	 * Look up the PCI bus for the specified PCI bridge device.  Note
	 * that the PCI bridge device might not have any children yet.
	 * However, looking up its bus number doesn't require a valid child
	 * device, so we just pass NULL.
	 */
	if (BUS_READ_IVAR(pcib, NULL, PCIB_IVAR_BUS, &bus) != 0) {
		device_printf(pcib, "Unable to read PCI bus number");
		panic("PCI bridge without a bus number");
	}
		
	/* Bump the reference count. */
	ACPI_SERIAL_BEGIN(pci_link);
	link = acpi_pci_link_lookup(dev, index);
	if (link == NULL) {
		device_printf(dev, "apparently invalid index %d\n", index);
		ACPI_SERIAL_END(pci_link);
		return;
	}
	link->l_references++;
	if (link->l_routed)
		pci_link_interrupt_weights[link->l_irq]++;

	/*
	 * The BIOS only routes interrupts via ISA IRQs using the ATPICs
	 * (8259As).  Thus, if this link is routed via an ISA IRQ, go
	 * look to see if the BIOS routed an IRQ for this link at the
	 * indicated (bus, slot, pin).  If so, we prefer that IRQ for
	 * this link and add that IRQ to our list of known-good IRQs.
	 * This provides a good work-around for link devices whose _CRS
	 * method is either broken or bogus.  We only use the value
	 * returned by _CRS if we can't find a valid IRQ via this method
	 * in fact.
	 *
	 * If this link is not routed via an ISA IRQ (because we are using
	 * APIC for example), then don't bother looking up the BIOS IRQ
	 * as if we find one it won't be valid anyway.
	 */
	if (!link->l_isa_irq) {
		ACPI_SERIAL_END(pci_link);
		return;
	}

	/* Try to find a BIOS IRQ setting from any matching devices. */
	bios_irq = acpi_pci_link_search_irq(bus, slot, pin);
	if (!PCI_INTERRUPT_VALID(bios_irq)) {
		ACPI_SERIAL_END(pci_link);
		return;
	}

	/* Validate the BIOS IRQ. */
	if (!link_valid_irq(link, bios_irq)) {
		device_printf(dev, "BIOS IRQ %u for %d.%d.INT%c is invalid\n",
		    bios_irq, (int)bus, slot, pin + 'A');
	} else if (!PCI_INTERRUPT_VALID(link->l_bios_irq)) {
		link->l_bios_irq = bios_irq;
		if (bios_irq < NUM_ISA_INTERRUPTS)
			pci_link_bios_isa_irqs |= (1 << bios_irq);
		if (bios_irq != link->l_initial_irq &&
		    PCI_INTERRUPT_VALID(link->l_initial_irq))
			device_printf(dev,
			    "BIOS IRQ %u does not match initial IRQ %u\n",
			    bios_irq, link->l_initial_irq);
	} else if (bios_irq != link->l_bios_irq)
		device_printf(dev,
	    "BIOS IRQ %u for %d.%d.INT%c does not match previous BIOS IRQ %u\n",
		    bios_irq, (int)bus, slot, pin + 'A',
		    link->l_bios_irq);
	ACPI_SERIAL_END(pci_link);
}

static ACPI_STATUS
acpi_pci_link_srs_from_crs(struct acpi_pci_link_softc *sc, ACPI_BUFFER *srsbuf)
{
	ACPI_RESOURCE *end, *res;
	ACPI_STATUS status;
	struct link *link;
	int i, in_dpf;

	/* Fetch the _CRS. */
	ACPI_SERIAL_ASSERT(pci_link);
	srsbuf->Pointer = NULL;
	srsbuf->Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiGetCurrentResources(acpi_get_handle(sc->pl_dev), srsbuf);
	if (ACPI_SUCCESS(status) && srsbuf->Pointer == NULL)
		status = AE_NO_MEMORY;
	if (ACPI_FAILURE(status)) {
		if (bootverbose)
			device_printf(sc->pl_dev,
			    "Unable to fetch current resources: %s\n",
			    AcpiFormatException(status));
		return (status);
	}

	/* Fill in IRQ resources via link structures. */
	link = sc->pl_links;
	i = 0;
	in_dpf = DPF_OUTSIDE;
	res = (ACPI_RESOURCE *)srsbuf->Pointer;
	end = (ACPI_RESOURCE *)((char *)srsbuf->Pointer + srsbuf->Length);
	for (;;) {
		switch (res->Type) {
		case ACPI_RESOURCE_TYPE_START_DEPENDENT:
			switch (in_dpf) {
			case DPF_OUTSIDE:
				/* We've started the first DPF. */
				in_dpf = DPF_FIRST;
				break;
			case DPF_FIRST:
				/* We've started the second DPF. */
				panic(
		"%s: Multiple dependent functions within a current resource",
				    __func__);
				break;
			}
			break;
		case ACPI_RESOURCE_TYPE_END_DEPENDENT:
			/* We are finished with DPF parsing. */
			KASSERT(in_dpf != DPF_OUTSIDE,
			    ("%s: end dpf when not parsing a dpf", __func__));
			in_dpf = DPF_OUTSIDE;
			break;
		case ACPI_RESOURCE_TYPE_IRQ:
			MPASS(i < sc->pl_num_links);
			res->Data.Irq.InterruptCount = 1;
			if (PCI_INTERRUPT_VALID(link->l_irq)) {
				KASSERT(link->l_irq < NUM_ISA_INTERRUPTS,
		("%s: can't put non-ISA IRQ %d in legacy IRQ resource type",
				    __func__, link->l_irq));
				res->Data.Irq.Interrupts[0] = link->l_irq;
			} else
				res->Data.Irq.Interrupts[0] = 0;
			link++;
			i++;
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			MPASS(i < sc->pl_num_links);
			res->Data.ExtendedIrq.InterruptCount = 1;
			if (PCI_INTERRUPT_VALID(link->l_irq))
				res->Data.ExtendedIrq.Interrupts[0] =
				    link->l_irq;
			else
				res->Data.ExtendedIrq.Interrupts[0] = 0;
			link++;
			i++;
			break;
		}
		if (res->Type == ACPI_RESOURCE_TYPE_END_TAG)
			break;
		res = ACPI_NEXT_RESOURCE(res);
		if (res >= end)
			break;
	}
	return (AE_OK);
}

static ACPI_STATUS
acpi_pci_link_srs_from_links(struct acpi_pci_link_softc *sc,
    ACPI_BUFFER *srsbuf)
{
	ACPI_RESOURCE newres;
	ACPI_STATUS status;
	struct link *link;
	int i;

	/* Start off with an empty buffer. */
	srsbuf->Pointer = NULL;
	link = sc->pl_links;
	for (i = 0; i < sc->pl_num_links; i++) {

		/* Add a new IRQ resource from each link. */
		link = &sc->pl_links[i];
		if (link->l_prs_template.Type == ACPI_RESOURCE_TYPE_IRQ) {

			/* Build an IRQ resource. */
			bcopy(&link->l_prs_template, &newres,
			    ACPI_RS_SIZE(newres.Data.Irq));
			newres.Data.Irq.InterruptCount = 1;
			if (PCI_INTERRUPT_VALID(link->l_irq)) {
				KASSERT(link->l_irq < NUM_ISA_INTERRUPTS,
		("%s: can't put non-ISA IRQ %d in legacy IRQ resource type",
				    __func__, link->l_irq));
				newres.Data.Irq.Interrupts[0] = link->l_irq;
			} else
				newres.Data.Irq.Interrupts[0] = 0;
		} else {

			/* Build an ExtIRQ resuorce. */
			bcopy(&link->l_prs_template, &newres,
			    ACPI_RS_SIZE(newres.Data.ExtendedIrq));
			newres.Data.ExtendedIrq.InterruptCount = 1;
			if (PCI_INTERRUPT_VALID(link->l_irq))
				newres.Data.ExtendedIrq.Interrupts[0] =
				    link->l_irq;
			else
				newres.Data.ExtendedIrq.Interrupts[0] = 0;
		}

		/* Add the new resource to the end of the _SRS buffer. */
		status = acpi_AppendBufferResource(srsbuf, &newres);
		if (ACPI_FAILURE(status)) {
			device_printf(sc->pl_dev,
			    "Unable to build resources: %s\n",
			    AcpiFormatException(status));
			if (srsbuf->Pointer != NULL)
				AcpiOsFree(srsbuf->Pointer);
			return (status);
		}
	}
	return (AE_OK);
}

static ACPI_STATUS
acpi_pci_link_route_irqs(device_t dev)
{
	struct acpi_pci_link_softc *sc;
	ACPI_RESOURCE *resource, *end;
	ACPI_BUFFER srsbuf;
	ACPI_STATUS status;
	struct link *link;
	int i;

	ACPI_SERIAL_ASSERT(pci_link);
	sc = device_get_softc(dev);
	if (sc->pl_crs_bad)
		status = acpi_pci_link_srs_from_links(sc, &srsbuf);
	else
		status = acpi_pci_link_srs_from_crs(sc, &srsbuf);

	/* Write out new resources via _SRS. */
	status = AcpiSetCurrentResources(acpi_get_handle(dev), &srsbuf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "Unable to route IRQs: %s\n",
		    AcpiFormatException(status));
		AcpiOsFree(srsbuf.Pointer);
		return (status);
	}

	/*
	 * Perform acpi_config_intr() on each IRQ resource if it was just
	 * routed for the first time.
	 */
	link = sc->pl_links;
	i = 0;
	resource = (ACPI_RESOURCE *)srsbuf.Pointer;
	end = (ACPI_RESOURCE *)((char *)srsbuf.Pointer + srsbuf.Length);
	for (;;) {
		if (resource->Type == ACPI_RESOURCE_TYPE_END_TAG)
			break;
		switch (resource->Type) {
		case ACPI_RESOURCE_TYPE_IRQ:
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			MPASS(i < sc->pl_num_links);

			/*
			 * Only configure the interrupt and update the
			 * weights if this link has a valid IRQ and was
			 * previously unrouted.
			 */
			if (!link->l_routed &&
			    PCI_INTERRUPT_VALID(link->l_irq)) {
				link->l_routed = TRUE;
				acpi_config_intr(dev, resource);
				pci_link_interrupt_weights[link->l_irq] +=
				    link->l_references;
			}
			link++;
			i++;
			break;
		}
		resource = ACPI_NEXT_RESOURCE(resource);
		if (resource >= end)
			break;
	}
	AcpiOsFree(srsbuf.Pointer);
	return (AE_OK);
}

static int
acpi_pci_link_resume(device_t dev)
{
	struct acpi_pci_link_softc *sc;
	ACPI_STATUS status;
	int i, routed;

	/*
	 * If all of our links are routed, then restore the link via _SRS,
	 * otherwise, disable the link via _DIS.
	 */
	ACPI_SERIAL_BEGIN(pci_link);
	sc = device_get_softc(dev);
	routed = 0;
	for (i = 0; i < sc->pl_num_links; i++)
		if (sc->pl_links[i].l_routed)
			routed++;
	if (routed == sc->pl_num_links)
		status = acpi_pci_link_route_irqs(dev);
	else {
		AcpiEvaluateObject(acpi_get_handle(dev), "_DIS", NULL, NULL);
		status = AE_OK;
	}
	ACPI_SERIAL_END(pci_link);
	if (ACPI_FAILURE(status))
		return (ENXIO);
	else
		return (0);
}

/*
 * Pick an IRQ to use for this unrouted link.
 */
static uint8_t
acpi_pci_link_choose_irq(device_t dev, struct link *link)
{
	char tunable_buffer[64], link_name[5];
	u_int8_t best_irq, pos_irq;
	int best_weight, pos_weight, i;

	KASSERT(!link->l_routed, ("%s: link already routed", __func__));
	KASSERT(!PCI_INTERRUPT_VALID(link->l_irq),
	    ("%s: link already has an IRQ", __func__));

	/* Check for a tunable override. */
	if (ACPI_SUCCESS(acpi_short_name(acpi_get_handle(dev), link_name,
	    sizeof(link_name)))) {
		snprintf(tunable_buffer, sizeof(tunable_buffer),
		    "hw.pci.link.%s.%d.irq", link_name, link->l_res_index);
		if (getenv_int(tunable_buffer, &i) && PCI_INTERRUPT_VALID(i)) {
			if (!link_valid_irq(link, i))
				device_printf(dev,
				    "Warning, IRQ %d is not listed as valid\n",
				    i);
			return (i);
		}
		snprintf(tunable_buffer, sizeof(tunable_buffer),
		    "hw.pci.link.%s.irq", link_name);
		if (getenv_int(tunable_buffer, &i) && PCI_INTERRUPT_VALID(i)) {
			if (!link_valid_irq(link, i))
				device_printf(dev,
				    "Warning, IRQ %d is not listed as valid\n",
				    i);
			return (i);
		}
	}

	/*
	 * If we have a valid BIOS IRQ, use that.  We trust what the BIOS
	 * says it routed over what _CRS says the link thinks is routed.
	 */
	if (PCI_INTERRUPT_VALID(link->l_bios_irq))
		return (link->l_bios_irq);

	/*
	 * If we don't have a BIOS IRQ but do have a valid IRQ from _CRS,
	 * then use that.
	 */
	if (PCI_INTERRUPT_VALID(link->l_initial_irq))
		return (link->l_initial_irq);

	/*
	 * Ok, we have no useful hints, so we have to pick from the
	 * possible IRQs.  For ISA IRQs we only use interrupts that
	 * have already been used by the BIOS.
	 */
	best_irq = PCI_INVALID_IRQ;
	best_weight = INT_MAX;
	for (i = 0; i < link->l_num_irqs; i++) {
		pos_irq = link->l_irqs[i];
		if (pos_irq < NUM_ISA_INTERRUPTS &&
		    (pci_link_bios_isa_irqs & 1 << pos_irq) == 0)
			continue;
		pos_weight = pci_link_interrupt_weights[pos_irq];
		if (pos_weight < best_weight) {
			best_weight = pos_weight;
			best_irq = pos_irq;
		}
	}

	/*
	 * If this is an ISA IRQ, try using the SCI if it is also an ISA
	 * interrupt as a fallback.
	 */
	if (link->l_isa_irq) {
		pos_irq = AcpiGbl_FADT.SciInterrupt;
		pos_weight = pci_link_interrupt_weights[pos_irq];
		if (pos_weight < best_weight) {
			best_weight = pos_weight;
			best_irq = pos_irq;
		}
	}

	if (PCI_INTERRUPT_VALID(best_irq)) {
		if (bootverbose)
			device_printf(dev, "Picked IRQ %u with weight %d\n",
			    best_irq, best_weight);
	} else
		device_printf(dev, "Unable to choose an IRQ\n");
	return (best_irq);
}

int
acpi_pci_link_route_interrupt(device_t dev, int index)
{
	struct link *link;

	if (acpi_disabled("pci_link"))
		return (PCI_INVALID_IRQ);

	ACPI_SERIAL_BEGIN(pci_link);
	link = acpi_pci_link_lookup(dev, index);
	if (link == NULL)
		panic("%s: apparently invalid index %d", __func__, index);

	/*
	 * If this link device is already routed to an interrupt, just return
	 * the interrupt it is routed to.
	 */
	if (link->l_routed) {
		KASSERT(PCI_INTERRUPT_VALID(link->l_irq),
		    ("%s: link is routed but has an invalid IRQ", __func__));
		ACPI_SERIAL_END(pci_link);
		return (link->l_irq);
	}

	/* Choose an IRQ if we need one. */
	if (!PCI_INTERRUPT_VALID(link->l_irq)) {
		link->l_irq = acpi_pci_link_choose_irq(dev, link);

		/*
		 * Try to route the interrupt we picked.  If it fails, then
		 * assume the interrupt is not routed.
		 */
		if (PCI_INTERRUPT_VALID(link->l_irq)) {
			acpi_pci_link_route_irqs(dev);
			if (!link->l_routed)
				link->l_irq = PCI_INVALID_IRQ;
		}
	}
	ACPI_SERIAL_END(pci_link);

	return (link->l_irq);
}

/*
 * This is gross, but we abuse the identify routine to perform one-time
 * SYSINIT() style initialization for the driver.
 */
static void
acpi_pci_link_identify(driver_t *driver, device_t parent)
{

	/*
	 * If the SCI is an ISA IRQ, add it to the bitmask of known good
	 * ISA IRQs.
	 *
	 * XXX: If we are using the APIC, the SCI might have been
	 * rerouted to an APIC pin in which case this is invalid.  However,
	 * if we are using the APIC, we also shouldn't be having any PCI
	 * interrupts routed via ISA IRQs, so this is probably ok.
	 */
	if (AcpiGbl_FADT.SciInterrupt < NUM_ISA_INTERRUPTS)
		pci_link_bios_isa_irqs |= (1 << AcpiGbl_FADT.SciInterrupt);
}

static device_method_t acpi_pci_link_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	acpi_pci_link_identify),
	DEVMETHOD(device_probe,		acpi_pci_link_probe),
	DEVMETHOD(device_attach,	acpi_pci_link_attach),
	DEVMETHOD(device_resume,	acpi_pci_link_resume),

	DEVMETHOD_END
};

static driver_t acpi_pci_link_driver = {
	"pci_link",
	acpi_pci_link_methods,
	sizeof(struct acpi_pci_link_softc),
};

static devclass_t pci_link_devclass;

DRIVER_MODULE(acpi_pci_link, acpi, acpi_pci_link_driver, pci_link_devclass, 0,
    0);
MODULE_DEPEND(acpi_pci_link, acpi, 1, 1, 1);
