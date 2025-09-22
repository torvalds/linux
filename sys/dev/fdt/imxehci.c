/*	$OpenBSD: imxehci.c,v 1.7 2024/09/01 03:08:56 jsg Exp $ */
/*
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

/* usb phy */
#define USBPHY_PWD			0x00
#define USBPHY_CTRL			0x30
#define USBPHY_CTRL_SET			0x34
#define USBPHY_CTRL_CLR			0x38
#define USBPHY_CTRL_TOG			0x3c

#define USBPHY_CTRL_ENUTMILEVEL2	(1 << 14)
#define USBPHY_CTRL_ENUTMILEVEL3	(1 << 15)
#define USBPHY_CTRL_CLKGATE		(1 << 30)
#define USBPHY_CTRL_SFTRST		(1U << 31)

/* ehci */
#define USB_EHCI_OFFSET			0x100

#define EHCI_PS_PTS_UTMI_MASK	((1 << 25) | (3 << 30))

/* usb non-core */
#define USBNC_USB_OTG_CTRL		0x00
#define USBNC_USB_UH1_CTRL		0x04

#define USBNC_USB_CTRL_PWR_POL		(1 << 9)
#define USBNC_USB_CTRL_OVER_CUR_POL	(1 << 8)
#define USBNC_USB_CTRL_OVER_CUR_DIS	(1 << 7)
#define USBNC_USB_CTRL_NON_BURST	(1 << 1)

/* anatop */
#define ANALOG_USB1_CHRG_DETECT			0x1b0
#define ANALOG_USB1_CHRG_DETECT_SET		0x1b4
#define ANALOG_USB1_CHRG_DETECT_CLR		0x1b8
#define  ANALOG_USB1_CHRG_DETECT_CHK_CHRG_B		(1 << 19)
#define  ANALOG_USB1_CHRG_DETECT_EN_B			(1 << 20)
#define ANALOG_USB2_CHRG_DETECT			0x210
#define ANALOG_USB2_CHRG_DETECT_SET		0x214
#define ANALOG_USB2_CHRG_DETECT_CLR		0x218
#define  ANALOG_USB2_CHRG_DETECT_CHK_CHRG_B		(1 << 19)
#define  ANALOG_USB2_CHRG_DETECT_EN_B			(1 << 20)

int	imxehci_match(struct device *, void *, void *);
void	imxehci_attach(struct device *, struct device *, void *);
int	imxehci_detach(struct device *, int);

struct imxehci_softc {
	struct ehci_softc	sc;
	void			*sc_ih;
	bus_space_handle_t	uh_ioh;
	bus_space_handle_t	ph_ioh;
	bus_space_handle_t	nc_ioh;
	uint32_t		sc_unit;
};

const struct cfattach imxehci_ca = {
	sizeof (struct imxehci_softc), imxehci_match, imxehci_attach,
	imxehci_detach
};

struct cfdriver imxehci_cd = {
	NULL, "imxehci", DV_DULL
};

void	imxehci_init_phy(struct imxehci_softc *, uint32_t *);

int
imxehci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx27-usb") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx7d-usb"));
}

void
imxehci_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxehci_softc *sc = (struct imxehci_softc *)self;
	struct fdt_attach_args *faa = aux;
	usbd_status r;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;
	uint32_t phy[1], misc[2];
	uint32_t misc_reg[2];
	uint32_t off, reg;
	uint32_t vbus;
	int misc_node;

	if (faa->fa_nreg < 1)
		return;

	if (OF_getpropintarray(faa->fa_node, "phys",
	    phy, sizeof(phy)) != sizeof(phy)) {
		if (OF_getpropintarray(faa->fa_node, "fsl,usbphy",
		    phy, sizeof(phy)) != sizeof(phy))
			return;
	}

	if (OF_getpropintarray(faa->fa_node, "fsl,usbmisc",
	    misc, sizeof(misc)) != sizeof(misc))
		return;

	misc_node = OF_getnodebyphandle(misc[0]);
	if (misc_node == 0)
		return;

	if (OF_getpropintarray(misc_node, "reg", misc_reg,
	    sizeof(misc_reg)) != sizeof(misc_reg))
		return;

	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;
	sc->sc.sc_size = faa->fa_reg[0].size - USB_EHCI_OFFSET;
	sc->sc.sc_flags = EHCIF_USBMODE;
	sc->sc_unit = misc[1];

	/* Map I/O space */
	if (bus_space_map(sc->sc.iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->uh_ioh)) {
		printf(": cannot map mem space\n");
		goto out;
	}
	if (bus_space_subregion(sc->sc.iot, sc->uh_ioh, USB_EHCI_OFFSET,
	    sc->sc.sc_size, &sc->sc.ioh)) {
		printf(": cannot map mem space\n");
		goto mem0;
	}

	if (bus_space_map(sc->sc.iot, misc_reg[0],
	    misc_reg[1], 0, &sc->nc_ioh)) {
		printf(": cannot map mem space\n");
		goto mem1;
	}

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");
	power_domain_enable(faa->fa_node);
	clock_set_assigned(faa->fa_node);
	clock_enable(faa->fa_node, NULL);
	delay(1000);

	/* over current and polarity setting */
	switch (misc[1]) {
	case 0:
		off = USBNC_USB_OTG_CTRL;
		break;
	case 1:
		off = USBNC_USB_UH1_CTRL;
		break;
	default:
		printf("%s: invalid usbmisc property\n", devname);
		return;
	}

	reg = bus_space_read_4(sc->sc.iot, sc->nc_ioh, off);
	reg &= ~USBNC_USB_CTRL_OVER_CUR_DIS;
	if (OF_getproplen(faa->fa_node, "disable-over-current") == 0)
		reg |= USBNC_USB_CTRL_OVER_CUR_DIS;
	if (OF_getproplen(faa->fa_node, "over-current-active-low") == 0)
		reg |= USBNC_USB_CTRL_OVER_CUR_POL;
	else if (OF_getproplen(faa->fa_node, "over-current-active-high") == 0)
		reg &= ~USBNC_USB_CTRL_OVER_CUR_POL;
	if (OF_getproplen(faa->fa_node, "power-active-high") == 0)
		reg |= USBNC_USB_CTRL_PWR_POL;
	reg |= USBNC_USB_CTRL_NON_BURST;
	bus_space_write_4(sc->sc.iot, sc->nc_ioh, off, reg);

	/* enable usb bus power */
	vbus = OF_getpropint(faa->fa_node, "vbus-supply", 0);
	if (vbus)
		regulator_enable(vbus);

	/* Disable interrupts, so we don't get any spurious ones. */
	sc->sc.sc_offs = EREAD1(&sc->sc, EHCI_CAPLENGTH);
	EOWRITE2(&sc->sc, EHCI_USBINTR, 0);

	/* Stop then Reset */
	uint32_t val = EOREAD4(&sc->sc, EHCI_USBCMD);
	val &= ~EHCI_CMD_RS;
	EOWRITE4(&sc->sc, EHCI_USBCMD, val);

	while (EOREAD4(&sc->sc, EHCI_USBCMD) & EHCI_CMD_RS)
		;

	val = EOREAD4(&sc->sc, EHCI_USBCMD);
	val |= EHCI_CMD_HCRESET;
	EOWRITE4(&sc->sc, EHCI_USBCMD, val);

	while (EOREAD4(&sc->sc, EHCI_USBCMD) & EHCI_CMD_HCRESET)
		;

	/* power up PHY */
	imxehci_init_phy(sc, phy);

	/* set host mode */
	EOWRITE4(&sc->sc, EHCI_USBMODE,
	    EOREAD4(&sc->sc, EHCI_USBMODE) | EHCI_USBMODE_CM_HOST);

	/* set to UTMI mode */
	EOWRITE4(&sc->sc, EHCI_PORTSC(1),
	    EOREAD4(&sc->sc, EHCI_PORTSC(1)) & ~EHCI_PS_PTS_UTMI_MASK);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_USB,
	    ehci_intr, &sc->sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto mem2;
	}

	strlcpy(sc->sc.sc_vendor, "i.MX", sizeof(sc->sc.sc_vendor));
	r = ehci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);
		goto intr;
	}

	config_found(self, &sc->sc.sc_bus, usbctlprint);

	goto out;

intr:
	fdt_intr_disestablish(sc->sc_ih);
	sc->sc_ih = NULL;
mem2:
	bus_space_unmap(sc->sc.iot, sc->nc_ioh, misc_reg[1]);
mem1:
mem0:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, faa->fa_reg[0].size);
	sc->sc.sc_size = 0;
out:
	return;
}

int
imxehci_detach(struct device *self, int flags)
{
	struct imxehci_softc		*sc = (struct imxehci_softc *)self;
	int				rv;

	rv = ehci_detach(self, flags);
	if (rv)
		return (rv);

	if (sc->sc_ih != NULL) {
		fdt_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	return (0);
}

/*
 * PHY initialization.
 */

struct imxehci_phy {
	const char *compat;
	void (*init)(struct imxehci_softc *, uint32_t *);
};

void imx23_usb_init(struct imxehci_softc *, uint32_t *);
static void nop_xceiv_init(struct imxehci_softc *, uint32_t *);

struct imxehci_phy imxehci_phys[] = {
	{ "fsl,imx23-usbphy", imx23_usb_init },
	{ "usb-nop-xceiv", nop_xceiv_init },
};

void
imxehci_init_phy(struct imxehci_softc *sc, uint32_t *cells)
{
	int node;
	int i;

	node = OF_getnodebyphandle(cells[0]);
	if (node == 0)
		return;

	for (i = 0; i < nitems(imxehci_phys); i++) {
		if (OF_is_compatible(node, imxehci_phys[i].compat)) {
			imxehci_phys[i].init(sc, cells);
			return;
		}
	}
}

/*
 * i.MX5/6 PHYs.
 */

void
imx23_usb_init(struct imxehci_softc *sc, uint32_t *cells)
{
	struct regmap *rm = NULL;
	uint32_t phy_reg[2];
	uint32_t anatop[1];
	int node;

	node = OF_getnodebyphandle(cells[0]);
	KASSERT(node != 0);

	if (OF_getpropintarray(node, "reg", phy_reg,
	    sizeof(phy_reg)) != sizeof(phy_reg))
		return;

	if (bus_space_map(sc->sc.iot, phy_reg[0],
	    phy_reg[1], 0, &sc->ph_ioh)) {
		printf("%s: can't map PHY registers\n",
		    sc->sc.sc_bus.bdev.dv_xname);
		return;
	}

	if (OF_getpropintarray(node, "fsl,anatop",
	    anatop, sizeof(anatop)) == sizeof(anatop))
		rm = regmap_byphandle(anatop[0]);

	/* Disable the charger detection, else signal on DP will be poor */
	switch (sc->sc_unit) {
	case 0:
		if (rm != NULL)
			regmap_write_4(rm, ANALOG_USB1_CHRG_DETECT_SET,
			    ANALOG_USB1_CHRG_DETECT_CHK_CHRG_B |
			    ANALOG_USB1_CHRG_DETECT_EN_B);
		break;
	case 1:
		if (rm != NULL)
			regmap_write_4(rm, ANALOG_USB2_CHRG_DETECT_SET,
			    ANALOG_USB2_CHRG_DETECT_CHK_CHRG_B |
			    ANALOG_USB2_CHRG_DETECT_EN_B);
		break;
	}

	clock_enable(node, NULL);

	/* Reset USBPHY module */
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL_SET,
	    USBPHY_CTRL_SFTRST);

	delay(10);

	/* Remove CLKGATE and SFTRST */
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL_CLR,
	    USBPHY_CTRL_CLKGATE | USBPHY_CTRL_SFTRST);

	delay(10);

	/* Power up the PHY */
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_PWD, 0);

	/* enable FS/LS device */
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL_SET,
	    USBPHY_CTRL_ENUTMILEVEL2 | USBPHY_CTRL_ENUTMILEVEL3);
}

/*
 * i.MX7 PHYs.
 */

static void
nop_xceiv_init(struct imxehci_softc *sc, uint32_t *cells)
{
	int node;

	node = OF_getnodebyphandle(cells[0]);
	KASSERT(node != 0);

	clock_set_assigned(node);
	clock_enable(node, NULL);
}
