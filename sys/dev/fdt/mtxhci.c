/*	$OpenBSD: mtxhci.c,v 1.1 2025/03/25 04:17:52 hastings Exp $	*/
/*
 * Copyright (c) 2025 James Hastings <hastings@openbsd.org>
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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

#define MTXHCI_MAX_PORTS	4

/* registers */
#define MTXHCI_RESET		0x00
#define  RESET_ASSERT		(1 << 0)
#define MTXHCI_CFG_HOST		0x04
#define MTXHCI_CFG_DEV		0x08
#define MTXHCI_CFG_PCIE		0x0c
#define  CFG_PWRDN		(1 << 0)
#define MTXHCI_STA		0x10
#define  STA_USB3		(1 << 16)
#define  STA_XHCI		(1 << 11)
#define  STA_SYS		(1 << 10)
#define  STA_REF		(1 << 8)
#define  STA_PLL		(1 << 0)
#define MTXHCI_CAPS		0x24
#define  CAP_USB2_PORTS(x)	(((x) >> 8) & 0x7)
#define  CAP_USB3_PORTS(x)	(((x) >> 0) & 0x7)
#define MTXHCI_USB3_PORT(x)	0x30 + (x) * 8
#define MTXHCI_USB2_PORT(x)	0x50 + (x) * 8
#define  CFG_PORT_HOST		(1 << 2)
#define  CFG_PORT_PWRDN		(1 << 1)
#define  CFG_PORT_DISABLE	(1 << 0)

#define HREAD4(sc, reg)							\
	bus_space_read_4((sc)->sc.iot, (sc)->sc_port_ioh, (reg))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc.iot, (sc)->sc_port_ioh, (reg), (val))

struct mtxhci_softc {
	struct xhci_softc	sc;
	int			sc_node;

	bus_space_handle_t	sc_port_ioh;
	bus_size_t		sc_port_ios;
	int			sc_port_node;

	int			sc_ports_usb2;
	int			sc_ports_usb3;

	void			*sc_ih;
};

int	mtxhci_match(struct device *, void *, void *);
void	mtxhci_attach(struct device *, struct device *, void *);

const struct cfattach mtxhci_ca = {
	sizeof(struct mtxhci_softc), mtxhci_match, mtxhci_attach
};

struct cfdriver mtxhci_cd = {
	NULL, "mtxhci", DV_DULL
};

int	mtxhci_host_init(struct mtxhci_softc *);

int
mtxhci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "mediatek,mtk-xhci"))
		return 1;

	return 0;
}

void
mtxhci_attach(struct device *parent, struct device *self, void *aux)
{
	struct mtxhci_softc *sc = (struct mtxhci_softc *)self;
	struct fdt_attach_args *faa = aux;
	int error = 0, idx;

	idx = OF_getindex(faa->fa_node, "ippc", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg) {
		printf(": no ippc registers\n");
		return;
	}

	if (bus_space_map(faa->fa_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc_port_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_port_node = faa->fa_node;
	sc->sc_port_ios = faa->fa_reg[idx].size;

	idx = OF_getindex(faa->fa_node, "mac", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg) {
		printf(": no mac registers\n");
		goto unmap_port;
	}

	if (bus_space_map(faa->fa_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc.ioh)) {
		printf(": can't map registers\n");
		goto unmap_port;
	}

	sc->sc_node = faa->fa_node;
	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_size = faa->fa_reg[idx].size;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_USB,
	    xhci_intr, sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	power_domain_enable(sc->sc_node);
	reset_deassert_all(sc->sc_node);
	clock_set_assigned(sc->sc_node);
	clock_enable_all(sc->sc_node);

	if ((error = mtxhci_host_init(sc)) != 0) {
		printf(": host init failed, error=%d\n", error);
		goto disestablish_ret;
	}

	strlcpy(sc->sc.sc_vendor, "MediaTek", sizeof(sc->sc.sc_vendor));
	if ((error = xhci_init(&sc->sc)) != 0) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, error);
		goto disestablish_ret;
	}

	config_found(self, &sc->sc.sc_bus, usbctlprint);

	xhci_config(&sc->sc);

	return;

disestablish_ret:
	fdt_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(faa->fa_iot, sc->sc.ioh, sc->sc.sc_size);
unmap_port:
	bus_space_unmap(faa->fa_iot, sc->sc_port_ioh, sc->sc_port_ios);
}

int
mtxhci_host_init(struct mtxhci_softc *sc)
{
	uint32_t mask, val;
	int i, ntries;

	/* port capabilities */
	val = HREAD4(sc, MTXHCI_CAPS);
	sc->sc_ports_usb3 = MIN(MTXHCI_MAX_PORTS, CAP_USB3_PORTS(val));
	sc->sc_ports_usb2 = MIN(MTXHCI_MAX_PORTS, CAP_USB2_PORTS(val));

	if (sc->sc_ports_usb3 == 0 && sc->sc_ports_usb2 == 0)
		return ENXIO;

	/* enable phys */
	phy_enable_idx(sc->sc_port_node, -1);

	/* reset */
	val = HREAD4(sc, MTXHCI_RESET);
	val |= RESET_ASSERT;
	HWRITE4(sc, MTXHCI_RESET, val);
	delay(10);
	val &= ~RESET_ASSERT;
	HWRITE4(sc, MTXHCI_RESET, val);
	delay(10);

	/* disable device mode */
	val = HREAD4(sc, MTXHCI_CFG_DEV);
	val |= CFG_PWRDN;
	HWRITE4(sc, MTXHCI_CFG_DEV, val);

	/* enable host mode */
	val = HREAD4(sc, MTXHCI_CFG_HOST);
	val &= ~CFG_PWRDN;
	HWRITE4(sc, MTXHCI_CFG_HOST, val);

	mask = (STA_XHCI | STA_PLL | STA_SYS | STA_REF);
	if (sc->sc_ports_usb3) {
		mask |= STA_USB3;

		/* disable PCIe mode */
		val = HREAD4(sc, MTXHCI_CFG_PCIE);
		val |= CFG_PWRDN;
		HWRITE4(sc, MTXHCI_CFG_PCIE, val);
	}

	/* configure host ports */
	for (i = 0; i < sc->sc_ports_usb3; i++) {
		val = HREAD4(sc, MTXHCI_USB3_PORT(i));
		val &= ~(CFG_PORT_DISABLE | CFG_PORT_PWRDN);
		val |= CFG_PORT_HOST;
		HWRITE4(sc, MTXHCI_USB3_PORT(i), val);
	}
	for (i = 0; i < sc->sc_ports_usb2; i++) {
		val = HREAD4(sc, MTXHCI_USB2_PORT(i));
		val &= ~(CFG_PORT_DISABLE | CFG_PORT_PWRDN);
		val |= CFG_PORT_HOST;
		HWRITE4(sc, MTXHCI_USB2_PORT(i), val);
	}

	for (ntries = 0; ntries < 100; ntries++) {
		val = HREAD4(sc, MTXHCI_STA);
		if ((val & mask) == mask)
			break;
		delay(50);
	}
	if (ntries == 100)
		return ETIMEDOUT;

	return 0;
}
