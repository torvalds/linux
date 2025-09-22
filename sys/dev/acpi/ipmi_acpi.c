/* $OpenBSD: ipmi_acpi.c,v 1.7 2025/01/28 02:20:49 yasuoka Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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
#include <machine/apmvar.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>
#undef DEVNAME

#include <dev/ipmivar.h>

#define DEVNAME(s)		((s)->sc.sc_dev.dv_xname)

int	ipmi_acpi_match(struct device *, void *, void *);
void	ipmi_acpi_attach(struct device *, struct device *, void *);
int	ipmi_acpi_parse_crs(int, union acpi_resource *, void *);

struct ipmi_acpi_softc {
	struct ipmi_softc	 sc;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	int			 sc_ift;
	int			 sc_srv;

	bus_size_t		 sc_iobase;
	int			 sc_iospacing;
	char			 sc_iotype;
};

const struct cfattach ipmi_acpi_ca = {
	sizeof(struct ipmi_acpi_softc), ipmi_acpi_match, ipmi_acpi_attach,
	NULL, ipmi_activate
};

const char *ipmi_acpi_hids[] = { ACPI_DEV_IPMI, NULL };

int
ipmi_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	/* sanity */
	return (acpi_matchhids(aa, ipmi_acpi_hids, cf->cf_driver->cd_name));
}

void
ipmi_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ipmi_acpi_softc *sc = (struct ipmi_acpi_softc *)self;
	struct acpi_attach_args *aa = aux;
	struct ipmi_attach_args ia;
	struct aml_value res;
	int64_t ift, srv = 0;
	int rc;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	rc = aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_IFT", 0, NULL, &ift);
	if (rc) {
		printf(": no _IFT\n");
		return;
	}
	sc->sc_ift = ift;

	aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_SRV", 0, NULL, &srv);
	sc->sc_srv = srv;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CRS", 0, NULL, &res)) {
		printf(": no _CRS method\n");
		return;
	}
	if (res.type != AML_OBJTYPE_BUFFER) {
		printf(": invalid _CRS object (type %d len %d)\n",
		    res.type, res.length);
		aml_freevalue(&res);
		return;
	}

	aml_parse_resource(&res, ipmi_acpi_parse_crs, sc);
	aml_freevalue(&res);

	if (sc->sc_iotype == 0) {
		printf("%s: incomplete resources (ift %d)\n",
		    DEVNAME(sc), sc->sc_ift);
		return;
	}

	memset(&ia, 0, sizeof(ia));
	ia.iaa_iot = sc->sc_acpi->sc_iot;
	ia.iaa_memt = sc->sc_acpi->sc_memt;
	ia.iaa_if_type = sc->sc_ift;
	ia.iaa_if_rev = (sc->sc_srv >> 4);
	ia.iaa_if_irq = -1;
	ia.iaa_if_irqlvl = 0;
	ia.iaa_if_iosize = 1;
	ia.iaa_if_iospacing = sc->sc_iospacing;
	ia.iaa_if_iobase = sc->sc_iobase;
	ia.iaa_if_iotype = sc->sc_iotype;

	ipmi_attach_common(&sc->sc, &ia);
}

int
ipmi_acpi_parse_crs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct ipmi_acpi_softc *sc = arg;
	int type = AML_CRSTYPE(crs);
	bus_size_t addr;
	char iotype;

	switch (type) {
	case SR_IRQ:
		/* Ignore for now. */
		return 0;
	case SR_IOPORT:
		addr = crs->sr_ioport._max;
		iotype = 'i';
		break;
	case LR_MEM32FIXED:
		addr = crs->lr_m32fixed._bas;
		iotype = 'm';
		break;
	case LR_EXTIRQ:
		/* Ignore for now. */
		return 0;
	default:
		printf("\n%s: unexpected resource #%d type %d",
		    DEVNAME(sc), crsidx, type);
		sc->sc_iotype = 0;
		return -1;
	}

	switch (crsidx) {
	case 0:
		sc->sc_iobase = addr;
		sc->sc_iospacing = 1;
		sc->sc_iotype = iotype;
		break;
	case 1:
		if (sc->sc_iotype != iotype) {
			printf("\n%s: unexpected resource #%d type %d\n",
			    DEVNAME(sc), crsidx, type);
			sc->sc_iotype = 0;
			return -1;
		}
		if (addr <= sc->sc_iobase) {
			sc->sc_iotype = 0;
			return -1;
		}
		sc->sc_iospacing = addr - sc->sc_iobase;
		break;
	default:
		printf("\n%s: invalid resource #%d type %d (ift %d)",
		    DEVNAME(sc), crsidx, type, sc->sc_ift);
		sc->sc_iotype = 0;
		return -1;
	}

	return 0;
}
