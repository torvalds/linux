/* $OpenBSD: acpiprt.c,v 1.53 2025/09/16 12:18:10 hshoexer Exp $ */
/*
 * Copyright (c) 2006 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <machine/i82093reg.h>
#include <machine/i82093var.h>

#include <machine/mpbiosvar.h>

#include "ioapic.h"

struct acpiprt_irq {
	int _int;
	int _shr;
	int _ll;
	int _he;
};

struct acpiprt_map {
	int bus, dev;
	int pin;
	int irq;
	struct acpiprt_softc *sc;
	struct aml_node *node;
	SIMPLEQ_ENTRY(acpiprt_map) list;
};

SIMPLEQ_HEAD(, acpiprt_map) acpiprt_map_list =
    SIMPLEQ_HEAD_INITIALIZER(acpiprt_map_list);

int	acpiprt_match(struct device *, void *, void *);
void	acpiprt_attach(struct device *, struct device *, void *);
int	acpiprt_getirq(int, union acpi_resource *, void *);
int	acpiprt_chooseirq(int, union acpi_resource *, void *);

struct acpiprt_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	int			sc_bus;
};

const struct cfattach acpiprt_ca = {
	sizeof(struct acpiprt_softc), acpiprt_match, acpiprt_attach
};

struct cfdriver acpiprt_cd = {
	NULL, "acpiprt", DV_DULL, CD_COCOVM
};

void	acpiprt_prt_add(struct acpiprt_softc *, struct aml_value *);
int	acpiprt_getpcibus(struct acpiprt_softc *, struct aml_node *);
void	acpiprt_route_interrupt(int bus, int dev, int pin);

int
acpiprt_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata  *cf = match;

	/* sanity */
	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpiprt_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpiprt_softc *sc = (struct acpiprt_softc *)self;
	struct acpi_attach_args *aa = aux;
	struct aml_value res;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;
	sc->sc_bus = acpiprt_getpcibus(sc, sc->sc_devnode);
	printf(": bus %d (%s)", sc->sc_bus, sc->sc_devnode->parent->name);

	if (sc->sc_bus == -1) {
		printf("\n");
		return;
	}

	if (aml_evalnode(sc->sc_acpi, sc->sc_devnode, 0, NULL, &res)) {
		printf(": no PCI interrupt routing table\n");
		return;
	}

	if (res.type != AML_OBJTYPE_PACKAGE) {
		printf(": _PRT is not a package\n");
		aml_freevalue(&res);
		return;
	}

	printf("\n");

	for (i = 0; i < res.length; i++)
		acpiprt_prt_add(sc, res.v_package[i]);

	aml_freevalue(&res);
}

int
acpiprt_getirq(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpiprt_irq *irq = arg;
	int typ, len;

	irq->_shr = 0;
	irq->_ll = 0;
	irq->_he = 1;

	typ = AML_CRSTYPE(crs);
	len = AML_CRSLEN(crs);
	switch (typ) {
	case SR_IRQ:
		irq->_int= ffs(letoh16(crs->sr_irq.irq_mask)) - 1;
		if (len > 2) {
			irq->_shr = (crs->sr_irq.irq_flags & SR_IRQ_SHR);
			irq->_ll = (crs->sr_irq.irq_flags & SR_IRQ_POLARITY);
			irq->_he = (crs->sr_irq.irq_flags & SR_IRQ_MODE);
		}
		break;
	case LR_EXTIRQ:
		irq->_int = letoh32(crs->lr_extirq.irq[0]);
		irq->_shr = (crs->lr_extirq.flags & LR_EXTIRQ_SHR);
		irq->_ll = (crs->lr_extirq.flags & LR_EXTIRQ_POLARITY);
		irq->_he = (crs->lr_extirq.flags & LR_EXTIRQ_MODE);
		break;
	default:
		printf("unknown interrupt: %x\n", typ);
	}
	return (0);
}

int
acpiprt_pri[16] = {
	0,			/* 8254 Counter 0 */
	1,			/* Keyboard */
	0,			/* 8259 Slave */
	2,			/* Serial Port A */
	2,			/* Serial Port B */
	5,			/* Parallel Port / Generic */
	2,			/* Floppy Disk */
	4,			/* Parallel Port / Generic */
	1,			/* RTC */
	6,			/* Generic */
	7,			/* Generic */
	7,			/* Generic */
	1,			/* Mouse */
	0,			/* FPU */
	2,			/* Primary IDE */
	3			/* Secondary IDE */
};

int
acpiprt_chooseirq(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpiprt_irq *irq = arg;
	int typ, len, i, pri = -1;

	irq->_shr = 0;
	irq->_ll = 0;
	irq->_he = 1;

	typ = AML_CRSTYPE(crs);
	len = AML_CRSLEN(crs);
	switch (typ) {
	case SR_IRQ:
		for (i = 0; i < sizeof(crs->sr_irq.irq_mask) * 8; i++) {
			if (crs->sr_irq.irq_mask & (1 << i) &&
			    acpiprt_pri[i] > pri) {
				irq->_int = i;
				pri = acpiprt_pri[irq->_int];
			}
		}
		if (len > 2) {
			irq->_shr = (crs->sr_irq.irq_flags & SR_IRQ_SHR);
			irq->_ll = (crs->sr_irq.irq_flags & SR_IRQ_POLARITY);
			irq->_he = (crs->sr_irq.irq_flags & SR_IRQ_MODE);
		}
		break;
	case LR_EXTIRQ:
		/* First try non-8259 interrupts. */
		for (i = 0; i < crs->lr_extirq.irq_count; i++) {
			if (crs->lr_extirq.irq[i] > 15) {
				irq->_int = crs->lr_extirq.irq[i];
				return (0);
			}
		}

		for (i = 0; i < crs->lr_extirq.irq_count; i++) {
			if (acpiprt_pri[crs->lr_extirq.irq[i]] > pri) {
				irq->_int = crs->lr_extirq.irq[i];
				pri = acpiprt_pri[irq->_int];
			}
		}
		irq->_shr = (crs->lr_extirq.flags & LR_EXTIRQ_SHR);
		irq->_ll = (crs->lr_extirq.flags & LR_EXTIRQ_POLARITY);
		irq->_he = (crs->lr_extirq.flags & LR_EXTIRQ_MODE);
		break;
	default:
		printf("unknown interrupt: %x\n", typ);
	}
	return (0);
}

void
acpiprt_prt_add(struct acpiprt_softc *sc, struct aml_value *v)
{
	struct aml_node	*node;
	struct aml_value res, *pp;
	struct acpiprt_irq irq;
	u_int64_t addr;
	int pin;
	int64_t sta;
#if NIOAPIC > 0
	struct mp_intr_map *map;
	struct ioapic_softc *apic;
#endif
	pci_chipset_tag_t pc = NULL;
	pcitag_t tag;
	pcireg_t reg;
	int bus, dev, func, nfuncs;
	struct acpiprt_map *p;

	if (v->type != AML_OBJTYPE_PACKAGE || v->length != 4) {
		printf("invalid mapping object\n");
		return;
	}

	addr = aml_val2int(v->v_package[0]);
	pin = aml_val2int(v->v_package[1]);
	if (pin > 3) {
		return;
	}

	pp = v->v_package[2];
	if (pp->type == AML_OBJTYPE_NAMEREF) {
		node = aml_searchrel(sc->sc_devnode,
		    aml_getname(pp->v_nameref));
		if (node == NULL) {
			printf("Invalid device\n");
			return;
		}
		pp = node->value;
	}
	if (pp->type == AML_OBJTYPE_OBJREF) {
		pp = pp->v_objref.ref;
	}
	if (pp->type == AML_OBJTYPE_DEVICE) {
		node = pp->node;

		sta = acpi_getsta(sc->sc_acpi, node);
		if ((sta & STA_PRESENT) == 0)
			return;

		if (aml_evalname(sc->sc_acpi, node, "_CRS", 0, NULL, &res)) {
			printf("no _CRS method\n");
			return;
		}

		if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
			printf("invalid _CRS object\n");
			aml_freevalue(&res);
			return;
		}
		aml_parse_resource(&res, acpiprt_getirq, &irq);
		aml_freevalue(&res);

		/* Pick a new IRQ if necessary. */
		if ((irq._int == 0 || irq._int == 2 || irq._int == 13) &&
		    !aml_evalname(sc->sc_acpi, node, "_PRS", 0, NULL, &res)){
			aml_parse_resource(&res, acpiprt_chooseirq, &irq);
			aml_freevalue(&res);
		}

		if ((p = malloc(sizeof(*p), M_ACPI, M_NOWAIT)) == NULL)
			return;
		p->bus = sc->sc_bus;
		p->dev = ACPI_PCI_DEV(addr << 16);
		p->pin = pin;
		p->irq = irq._int;
		p->sc = sc;
		p->node = node;
		SIMPLEQ_INSERT_TAIL(&acpiprt_map_list, p, list);
	} else {
		irq._int = aml_val2int(v->v_package[3]);
		irq._shr = 1;
		irq._ll = 1;
		irq._he = 0;
	}

#ifdef ACPI_DEBUG
	printf("%s: %s addr 0x%llx pin %d irq %d\n",
	    DEVNAME(sc), aml_nodename(pp->node), addr, pin, irq._int);
#endif

#if NIOAPIC > 0
	if (nioapics > 0) {
		apic = ioapic_find_bybase(irq._int);
		if (apic == NULL) {
			printf("%s: no apic found for irq %d\n",
			    DEVNAME(sc), irq._int);
			return;
		}

		map = malloc(sizeof(*map), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (map == NULL)
			return;

		map->ioapic = apic;
		map->ioapic_pin = irq._int - apic->sc_apic_vecbase;
		map->bus_pin = ((addr >> 14) & 0x7c) | (pin & 0x3);
		if (irq._ll)
			map->flags |= (MPS_INTPO_ACTLO << MPS_INTPO_SHIFT);
		else
			map->flags |= (MPS_INTPO_ACTHI << MPS_INTPO_SHIFT);
		if (irq._he)
			map->flags |= (MPS_INTTR_EDGE << MPS_INTTR_SHIFT);
		else
			map->flags |= (MPS_INTTR_LEVEL << MPS_INTTR_SHIFT);

		map->redir = (IOAPIC_REDLO_DEL_LOPRI << IOAPIC_REDLO_DEL_SHIFT);
		switch ((map->flags >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK) {
		case MPS_INTPO_DEF:
		case MPS_INTPO_ACTLO:
			map->redir |= IOAPIC_REDLO_ACTLO;
			break;
		}
		switch ((map->flags >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK) {
		case MPS_INTTR_DEF:
		case MPS_INTTR_LEVEL:
			map->redir |= IOAPIC_REDLO_LEVEL;
			break;
		}

		map->ioapic_ih = APIC_INT_VIA_APIC |
		    ((apic->sc_apicid << APIC_INT_APIC_SHIFT) |
		    (map->ioapic_pin << APIC_INT_PIN_SHIFT));

		apic->sc_pins[map->ioapic_pin].ip_map = map;

		map->next = mp_busses[sc->sc_bus].mb_intrs;
		mp_busses[sc->sc_bus].mb_intrs = map;

		return;
	}
#endif

	bus = sc->sc_bus;
	dev = ACPI_PCI_DEV(addr << 16);
	tag = pci_make_tag(pc, bus, dev, 0);

	reg = pci_conf_read(pc, tag, PCI_BHLC_REG);
	if (PCI_HDRTYPE_MULTIFN(reg))
		nfuncs = 8;
	else
		nfuncs = 1;

	for (func = 0; func < nfuncs; func++) {
		tag = pci_make_tag(pc, bus, dev, func);
		reg = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
		if (PCI_INTERRUPT_PIN(reg) == pin + 1) {
			reg &= ~(PCI_INTERRUPT_LINE_MASK << PCI_INTERRUPT_LINE_SHIFT);
			reg |= irq._int << PCI_INTERRUPT_LINE_SHIFT;
			pci_conf_write(pc, tag, PCI_INTERRUPT_REG, reg);
		}
	}
}

int
acpiprt_getpcibus(struct acpiprt_softc *sc, struct aml_node *node)
{
	/* Check if parent device has PCI mapping */
	return (node->parent && node->parent->pci) ?
		node->parent->pci->sub : -1;
}

void
acpiprt_route_interrupt(int bus, int dev, int pin)
{
	struct acpiprt_softc *sc;
	struct acpiprt_map *p;
	struct acpiprt_irq irq;
	struct aml_node *node = NULL;
	struct aml_value res, res2;
	union acpi_resource *crs;
	int newirq;
	int64_t sta;

	SIMPLEQ_FOREACH(p, &acpiprt_map_list, list) {
		if (p->bus == bus && p->dev == dev && p->pin == (pin - 1)) {
			newirq = p->irq;
			sc = p->sc;
			node = p->node;
			break;
		}
	}
	if (node == NULL)
		return;

	sta = acpi_getsta(sc->sc_acpi, node);
	KASSERT(sta & STA_PRESENT);

	if (aml_evalname(sc->sc_acpi, node, "_CRS", 0, NULL, &res)) {
		printf("no _CRS method\n");
		return;
	}
	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		printf("invalid _CRS object\n");
		aml_freevalue(&res);
		return;
	}
	aml_parse_resource(&res, acpiprt_getirq, &irq);

	/* Only re-route interrupts when necessary. */
	if ((sta & STA_ENABLED) && irq._int == newirq) {
		aml_freevalue(&res);
		return;
	}

	crs = (union acpi_resource *)res.v_buffer;
	switch (AML_CRSTYPE(crs)) {
	case SR_IRQ:
		crs->sr_irq.irq_mask = htole16(1 << newirq);
		break;
	case LR_EXTIRQ:
		crs->lr_extirq.irq[0] = htole32(newirq);
		break;
	}

	if (aml_evalname(sc->sc_acpi, node, "_SRS", 1, &res, &res2)) {
		printf("no _SRS method\n");
		aml_freevalue(&res);
		return;
	}
	aml_freevalue(&res);
	aml_freevalue(&res2);
}
