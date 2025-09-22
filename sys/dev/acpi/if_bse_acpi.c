/*	$OpenBSD: if_bse_acpi.c,v 1.6 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis
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

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/mii/miivar.h>
#include <dev/ic/bcmgenetvar.h>

struct bse_acpi_softc {
	struct genet_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
};

int	bse_acpi_match(struct device *, void *, void *);
void	bse_acpi_attach(struct device *, struct device *, void *);

const struct cfattach bse_acpi_ca = {
	sizeof(struct bse_acpi_softc), bse_acpi_match, bse_acpi_attach
};

const char *bse_hids[] = {
	"BCM6E4E",
	NULL
};

int
bse_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, bse_hids, cf->cf_driver->cd_name);
}

void
bse_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct bse_acpi_softc *sc = (struct bse_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	char phy_mode[16] = { 0 };
	int error;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.sc_bst = aaa->aaa_bst[0];
	sc->sc.sc_dmat = aaa->aaa_dmat;

	if (bus_space_map(sc->sc.sc_bst, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.sc_bsh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc.sc_ih = acpi_intr_establish(aaa->aaa_irq[0],
	    aaa->aaa_irq_flags[0], IPL_NET, genet_intr,
	    sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc.sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	/*
	 * UEFI firmware initializes the hardware MAC address
	 * registers.  Read them here before we reset the hardware.
	 */
	genet_lladdr_read(&sc->sc, sc->sc.sc_lladdr);

	acpi_getprop(sc->sc_node, "phy-mode", phy_mode, sizeof(phy_mode));
	if (strcmp(phy_mode, "rgmii-id") == 0)
		sc->sc.sc_phy_mode = GENET_PHY_MODE_RGMII_ID;
	else if (strcmp(phy_mode, "rgmii-rxid") == 0)
		sc->sc.sc_phy_mode = GENET_PHY_MODE_RGMII_RXID;
	else if (strcmp(phy_mode, "rgmii-txid") == 0)
		sc->sc.sc_phy_mode = GENET_PHY_MODE_RGMII_TXID;
	else
		sc->sc.sc_phy_mode = GENET_PHY_MODE_RGMII;

	sc->sc.sc_phy_id = MII_PHY_ANY;
	error = genet_attach(&sc->sc);
	if (error)
		goto disestablish;

	return;

disestablish:
	acpi_intr_disestablish(sc->sc.sc_ih);
unmap:
	bus_space_unmap(sc->sc.sc_bst, sc->sc.sc_bsh, aaa->aaa_size[0]);
}
