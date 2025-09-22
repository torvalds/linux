/*	$OpenBSD: xhci_acpi.c,v 1.13 2024/10/09 00:38:26 jsg Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis
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
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

struct xhci_acpi_softc {
	struct xhci_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	void		*sc_ih;
};

int	xhci_acpi_match(struct device *, void *, void *);
void	xhci_acpi_attach(struct device *, struct device *, void *);

const struct cfattach xhci_acpi_ca = {
	sizeof(struct xhci_acpi_softc), xhci_acpi_match, xhci_acpi_attach,
	NULL, xhci_activate
};

const char *xhci_hids[] = {
	"PNP0D10",
	"PNP0D15",
	"QCOM0304",		/* SDM845 URS */
	"QCOM0305",
	"QCOM0497",		/* SC8180 URS */
	"QCOM0498",
	"QCOM068B",		/* SC8280 URS */
	"QCOM068C",
	"QCOM0826",		/* SC7180 USB */
	"QCOM24B6",		/* SDM850 URS */
	"QCOM24B7",
	"QCOM0C8B",		/* X1E80100 URS */
	"QCOM0C8C",
	"QCOM0D07",
	NULL
};

int
xhci_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aaa, xhci_hids, cf->cf_driver->cd_name);
}

void
xhci_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct xhci_acpi_softc *sc = (struct xhci_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_node *node;
	int error;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	/* XXX: Attaching on that specific controller resets the X13s */
	extern char *hw_ver;
	if (hw_ver && strcmp(hw_ver, "ThinkPad X13s Gen 1") == 0 &&
	    strncmp(sc->sc_node->name, "USB2", 4) == 0) {
		printf(": disabled\n");
		return;
	}

	/*
	 * The Qualcomm dual role controller has the interrupt on a
	 * child node.  Find it and parse its resources to find the
	 * interrupt.
	 */
	if (strcmp(aaa->aaa_dev, "QCOM0304") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0305") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0497") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0498") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM068B") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM068C") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM24B6") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM24B7") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0C8B") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0C8C") == 0 ||
	    strcmp(aaa->aaa_dev, "QCOM0D07") == 0) {
		SIMPLEQ_FOREACH(node, &sc->sc_node->son, sib) {
			if (strncmp(node->name, "USB", 3) == 0) {
				aaa->aaa_node = node;
				acpi_parse_crs(sc->sc_acpi, aaa);
				break;
			}
		}
	}

	if (aaa->aaa_nirq < 1) {
		printf(": no interrupt\n");
		return;
	}

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.iot = aaa->aaa_bst[0];
	sc->sc.sc_size = aaa->aaa_size[0];
	sc->sc.sc_bus.dmatag = aaa->aaa_dmat;

	if (bus_space_map(sc->sc.iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_USB, xhci_intr, sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	strlcpy(sc->sc.sc_vendor, "Generic", sizeof(sc->sc.sc_vendor));
	if ((error = xhci_init(&sc->sc)) != 0) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, error);
		goto disestablish_ret;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);

	/* Now that the stack is ready, config' the HC and enable interrupts. */
	xhci_config(&sc->sc);

	return;

disestablish_ret:
	acpi_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	return;
}
