/*	$OpenBSD: exehci.c,v 1.11 2021/10/24 17:52:27 mpi Exp $ */
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

/* registers */
#define USBPHY_CTRL0			0x00
#define USBPHY_TUNE0			0x04
#define HSICPHY_CTRL1			0x10
#define HSICPHY_TUNE1			0x14
#define HSICPHY_CTRL2			0x20
#define HSICPHY_TUNE2			0x24
#define EHCI_CTRL			0x30
#define OHCI_CTRL			0x34
#define USBOTG_SYS			0x38
#define USBOTG_TUNE			0x40

/* bits and bytes */
#define CLK_24MHZ			5

#define HOST_CTRL0_PHYSWRSTALL		(1U << 31)
#define HOST_CTRL0_COMMONON_N		(1 << 9)
#define HOST_CTRL0_SIDDQ		(1 << 6)
#define HOST_CTRL0_FORCESLEEP		(1 << 5)
#define HOST_CTRL0_FORCESUSPEND		(1 << 4)
#define HOST_CTRL0_WORDINTERFACE	(1 << 3)
#define HOST_CTRL0_UTMISWRST		(1 << 2)
#define HOST_CTRL0_LINKSWRST		(1 << 1)
#define HOST_CTRL0_PHYSWRST		(1 << 0)

#define HOST_CTRL0_FSEL_MASK		(7 << 16)

#define EHCI_CTRL_ENAINCRXALIGN		(1 << 29)
#define EHCI_CTRL_ENAINCR4		(1 << 28)
#define EHCI_CTRL_ENAINCR8		(1 << 27)
#define EHCI_CTRL_ENAINCR16		(1 << 26)

/* SYSREG registers */
#define USB20PHY_CFG			0x230
#define  USB20PHY_CFG_HOST_LINK_EN	(1 << 0)

/* PMU registers */
#define USB_HOST_POWER_5250	0x708
#define USB_HOST_POWER_54XX	0x70c
#define USB_HOST_POWER_EN	(1 << 0)

int	exehci_match(struct device *, void *, void *);
void	exehci_attach(struct device *, struct device *, void *);
int	exehci_detach(struct device *, int);

struct exehci_softc {
	struct ehci_softc	sc;
	void			*sc_ih;
	int			sc_phy;
	bus_space_handle_t	ph_ioh;
};

const struct cfattach exehci_ca = {
	sizeof (struct exehci_softc), exehci_match, exehci_attach,
	exehci_detach
};

struct cfdriver exehci_cd = {
	NULL, "exehci", DV_DULL
};

void	exehci_setup(struct exehci_softc *);

int
exehci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "samsung,exynos4210-ehci");
}

void
exehci_attach(struct device *parent, struct device *self, void *aux)
{
	struct exehci_softc *sc = (struct exehci_softc *)self;
	struct fdt_attach_args *faa = aux;
	usbd_status r;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;
	uint32_t phys[2];
	uint32_t phy_reg[2];
	int node;

	if (faa->fa_nreg < 1)
		return;

	node = OF_child(faa->fa_node);
	if (node == 0)
		node = faa->fa_node;

	if (OF_getpropintarray(node, "phys", phys,
	    sizeof(phys)) != sizeof(phys))
		return;

	sc->sc_phy = OF_getnodebyphandle(phys[0]);
	if (sc->sc_phy == 0)
		return;

	if (OF_getpropintarray(sc->sc_phy, "reg", phy_reg,
	    sizeof(phy_reg)) != sizeof(phy_reg))
		return;

	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;
	sc->sc.sc_size = faa->fa_reg[0].size;

	/* Map I/O space */
	if (bus_space_map(sc->sc.iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc.ioh)) {
		printf(": cannot map mem space\n");
		goto out;
	}

	if (bus_space_map(sc->sc.iot, phy_reg[0],
	    phy_reg[1], 0, &sc->ph_ioh)) {
		printf(": cannot map mem space\n");
		goto mem0;
	}

	printf("\n");

	clock_enable_all(faa->fa_node);

	exehci_setup(sc);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_USB,
	    ehci_intr, &sc->sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto mem1;
	}

	strlcpy(sc->sc.sc_vendor, "Exynos 5", sizeof(sc->sc.sc_vendor));
	r = ehci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);
		goto intr;
	}

	config_found(self, &sc->sc.sc_bus, usbctlprint);

	goto out;

intr:
	arm_intr_disestablish(sc->sc_ih);
	sc->sc_ih = NULL;
mem1:
	bus_space_unmap(sc->sc.iot, sc->ph_ioh, phy_reg[1]);
mem0:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	sc->sc.sc_size = 0;
out:
	return;
}

int
exehci_detach(struct device *self, int flags)
{
	struct exehci_softc *sc = (struct exehci_softc *)self;
	int rv;

	rv = ehci_detach(self, flags);
	if (rv)
		return rv;

	if (sc->sc_ih != NULL) {
		arm_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	return 0;
}

void
exehci_setup(struct exehci_softc *sc)
{
	struct regmap *pmurm, *sysrm;
	uint32_t pmureg, sysreg;
	bus_size_t offset;
	uint32_t val;
	int node;

#if 0
	/* VBUS, GPIO_X11, only on SMDK5250 and Chromebooks */
	exgpio_set_dir(0xa9, EXGPIO_DIR_OUT);
	exgpio_set_bit(0xa9);
	delay(3000);
#endif

	/* Enable host mode. */
	sysreg = OF_getpropint(sc->sc_phy, "samsung,sysreg-phandle", 0);
	sysrm = regmap_byphandle(sysreg);
	if (sysrm) {
		val = regmap_read_4(sysrm, USB20PHY_CFG);
		val |= USB20PHY_CFG_HOST_LINK_EN;
		regmap_write_4(sysrm, USB20PHY_CFG, val);
	}

	/* Power up the PHY block. */
	pmureg = OF_getpropint(sc->sc_phy, "samsung,pmureg-phandle", 0);
	pmurm = regmap_byphandle(pmureg);
	if (pmurm) {
		node = OF_getnodebyphandle(pmureg);
		if (OF_is_compatible(node, "samsung,exynos5250-pmu"))
			offset = USB_HOST_POWER_5250;
		else
			offset = USB_HOST_POWER_54XX;
		
		val = regmap_read_4(pmurm, offset);
		val |= USB_HOST_POWER_EN;
		regmap_write_4(pmurm, offset, val);
	}

	delay(10000);

	/* Setting up host and device simultaneously */
	val = bus_space_read_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL0);
	val &= ~(HOST_CTRL0_FSEL_MASK |
		 /* HOST Phy setting */
		 HOST_CTRL0_PHYSWRST |
		 HOST_CTRL0_PHYSWRSTALL |
		 HOST_CTRL0_SIDDQ |
		 HOST_CTRL0_FORCESUSPEND |
		 HOST_CTRL0_FORCESLEEP);
	val |= (/* Setting up the ref freq */
		 CLK_24MHZ << 16 |
		 HOST_CTRL0_COMMONON_N |
		 /* HOST Phy setting */
		 HOST_CTRL0_LINKSWRST |
		 HOST_CTRL0_UTMISWRST);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL0, val);
	delay(10000);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL0,
	    bus_space_read_4(sc->sc.iot, sc->ph_ioh, USBPHY_CTRL0) &
		~(HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST));
	delay(20000);

	/* EHCI Ctrl setting */
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, EHCI_CTRL,
	    bus_space_read_4(sc->sc.iot, sc->ph_ioh, EHCI_CTRL) |
		EHCI_CTRL_ENAINCRXALIGN |
		EHCI_CTRL_ENAINCR4 |
		EHCI_CTRL_ENAINCR8 |
		EHCI_CTRL_ENAINCR16);

#if 0
	/* HSIC USB Hub initialization. */
	if (1) {
		exgpio_set_dir(0xc8, EXGPIO_DIR_OUT);
		exgpio_clear_bit(0xc8);
		delay(1000);
		exgpio_set_bit(0xc8);
		delay(5000);

		val = bus_space_read_4(sc->sc.iot, sc->ph_ioh, HSICPHY_CTRL1);
		val &= ~(HOST_CTRL0_SIDDQ |
			 HOST_CTRL0_FORCESLEEP |
			 HOST_CTRL0_FORCESUSPEND);
		bus_space_write_4(sc->sc.iot, sc->ph_ioh, HSICPHY_CTRL1, val);
		val |= HOST_CTRL0_PHYSWRST;
		bus_space_write_4(sc->sc.iot, sc->ph_ioh, HSICPHY_CTRL1, val);
		delay(1000);
		val &= ~HOST_CTRL0_PHYSWRST;
		bus_space_write_4(sc->sc.iot, sc->ph_ioh, HSICPHY_CTRL1, val);
	}
#endif

	/* PHY clock and power setup time */
	delay(50000);
}
