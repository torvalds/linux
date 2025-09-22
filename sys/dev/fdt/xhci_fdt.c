/*	$OpenBSD: xhci_fdt.c,v 1.24 2023/07/23 11:49:17 kettenis Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

struct xhci_fdt_softc {
	struct xhci_softc	sc;
	int			sc_node;
	bus_space_handle_t	ph_ioh;
	void			*sc_ih;

	bus_addr_t		sc_otg_base;
	bus_size_t		sc_otg_size;
	bus_space_handle_t	sc_otg_ioh;

	struct device_ports	sc_ports;
	struct usb_controller_port sc_usb_controller_port;
	struct task		sc_snps_connect_task;
};

int	xhci_fdt_match(struct device *, void *, void *);
void	xhci_fdt_attach(struct device *, struct device *, void *);
int	xhci_fdt_activate(struct device *, int);

const struct cfattach xhci_fdt_ca = {
	sizeof(struct xhci_fdt_softc), xhci_fdt_match, xhci_fdt_attach, NULL,
	xhci_fdt_activate
};

int	xhci_cdns_attach(struct xhci_fdt_softc *);
int	xhci_snps_attach(struct xhci_fdt_softc *);
int	xhci_snps_init(struct xhci_fdt_softc *);
void	xhci_init_phys(struct xhci_fdt_softc *);

int
xhci_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "generic-xhci") ||
	    OF_is_compatible(faa->fa_node, "cavium,octeon-7130-xhci") ||
	    OF_is_compatible(faa->fa_node, "cdns,usb3") ||
	    OF_is_compatible(faa->fa_node, "snps,dwc3");
}

void
xhci_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct xhci_fdt_softc *sc = (struct xhci_fdt_softc *)self;
	struct fdt_attach_args *faa = aux;
	int error = 0;
	int idx;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "cdns,usb3")) {
		idx = OF_getindex(faa->fa_node, "otg", "reg-names");
		if (idx < 0 || idx > faa->fa_nreg) {
			printf(": no otg registers\n");
			return;
		}

		sc->sc_otg_base = faa->fa_reg[idx].addr;
		sc->sc_otg_size = faa->fa_reg[idx].size;
	}

	idx = OF_getindex(faa->fa_node, "xhci", "reg-names");
	if (idx == -1)
		idx = 0;
	if (idx >= faa->fa_nreg) {
		printf(": no xhci registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_size = faa->fa_reg[idx].size;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;

	if (bus_space_map(sc->sc.iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc.ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_USB,
	    xhci_intr, sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	/* Set up power and clocks */
	power_domain_enable(sc->sc_node);
	reset_deassert_all(sc->sc_node);
	clock_set_assigned(sc->sc_node);
	clock_enable_all(sc->sc_node);

	/* 
	 * Cadence and Synopsys DesignWare USB3 controllers need some
	 * extra attention because of the additional OTG
	 * functionality.
	 */
	if (OF_is_compatible(sc->sc_node, "cdns,usb3"))
		error = xhci_cdns_attach(sc);
	if (OF_is_compatible(sc->sc_node, "snps,dwc3"))
		error = xhci_snps_attach(sc);
	if (error) {
		printf(": can't initialize hardware\n");
		goto disestablish_ret;
	}

	xhci_init_phys(sc);

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
	fdt_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
}

int
xhci_fdt_activate(struct device *self, int act)
{
	struct xhci_fdt_softc *sc = (struct xhci_fdt_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		rv = xhci_activate(self, act);
		power_domain_disable(sc->sc_node);
		break;
	case DVACT_RESUME:
		power_domain_enable(sc->sc_node);
		if (OF_is_compatible(sc->sc_node, "snps,dwc3"))
			xhci_snps_init(sc);
		rv = xhci_activate(self, act);
		break;
	default:
		rv = xhci_activate(self, act);
		break;
	}

	return rv;
}

/*
 * Cadence USB3 controller.
 */

#define OTG_DID			0x00
#define  OTG_DID_V1		0x4024e
#define OTG_CMD			0x10
#define  OTG_CMD_HOST_BUS_REQ	(1 << 1)
#define  OTG_CMD_OTG_DIS	(1 << 3)
#define OTG_STS			0x14
#define  OTG_STS_XHCI_READY	(1 << 26)

int
xhci_cdns_attach(struct xhci_fdt_softc *sc)
{
	uint32_t did, sts;
	int timo;

	if (bus_space_map(sc->sc.iot, sc->sc_otg_base,
	    sc->sc_otg_size, 0, &sc->sc_otg_ioh))
		return ENOMEM;

	did = bus_space_read_4(sc->sc.iot, sc->sc_otg_ioh, OTG_DID);
	if (did != OTG_DID_V1)
		return ENOTSUP;

	bus_space_write_4(sc->sc.iot, sc->sc_otg_ioh, OTG_CMD,
	    OTG_CMD_HOST_BUS_REQ | OTG_CMD_OTG_DIS);
	for (timo = 100; timo > 0; timo--) {
		sts = bus_space_read_4(sc->sc.iot, sc->sc_otg_ioh, OTG_STS);
		if (sts & OTG_STS_XHCI_READY)
			break;
		delay(1000);
	}
	if (timo == 0) {
		bus_space_unmap(sc->sc.iot, sc->sc_otg_ioh, sc->sc_otg_size);
		return ETIMEDOUT;
	}

	return 0;
}

/*
 * Synopsys DesignWare USB3 controller.
 */

#define USB3_GCTL		0xc110
#define  USB3_GCTL_PRTCAPDIR_MASK	(0x3 << 12)
#define  USB3_GCTL_PRTCAPDIR_HOST	(0x1 << 12)
#define  USB3_GCTL_PRTCAPDIR_DEVICE	(0x2 << 12)
#define USB3_GUCTL1		0xc11c
#define  USB3_GUCTL1_TX_IPGAP_LINECHECK_DIS	(1 << 28)
#define USB3_GUSB2PHYCFG0	0xc200
#define  USB3_GUSB2PHYCFG0_U2_FREECLK_EXISTS	(1 << 30)
#define  USB3_GUSB2PHYCFG0_USBTRDTIM(n)	((n) << 10)
#define  USB3_GUSB2PHYCFG0_ENBLSLPM	(1 << 8)
#define  USB3_GUSB2PHYCFG0_SUSPENDUSB20	(1 << 6)
#define  USB3_GUSB2PHYCFG0_PHYIF	(1 << 3)

void
xhci_snps_do_connect(void *arg)
{
	struct xhci_fdt_softc *sc = arg;
	
	xhci_reinit(&sc->sc);
}

void
xhci_snps_connect(void *cookie)
{
	struct xhci_fdt_softc *sc = cookie;

	task_add(systq, &sc->sc_snps_connect_task);
}

void *
xhci_snps_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	return cookie;
}

int
xhci_snps_attach(struct xhci_fdt_softc *sc)
{
	/*
	 * On Apple hardware we need to reset the controller when we
	 * see a new connection.
	 */
	if (OF_is_compatible(sc->sc_node, "apple,dwc3")) {
		sc->sc_usb_controller_port.up_cookie = sc;
		sc->sc_usb_controller_port.up_connect = xhci_snps_connect;
		task_set(&sc->sc_snps_connect_task, xhci_snps_do_connect, sc);

		sc->sc_ports.dp_node = sc->sc_node;
		sc->sc_ports.dp_cookie = &sc->sc_usb_controller_port;
		sc->sc_ports.dp_ep_get_cookie = xhci_snps_ep_get_cookie;
		device_ports_register(&sc->sc_ports, EP_USB_CONTROLLER_PORT);
	}

	return xhci_snps_init(sc);
}

int
xhci_snps_init(struct xhci_fdt_softc *sc)
{
	char phy_type[16] = { 0 };
	int node = sc->sc_node;
	uint32_t reg;

	/* We don't support device mode, so always force host mode. */
	reg = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USB3_GCTL);
	reg &= ~USB3_GCTL_PRTCAPDIR_MASK;
	reg |= USB3_GCTL_PRTCAPDIR_HOST;
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USB3_GCTL, reg);

	/* Configure USB2 PHY type and quirks. */
	OF_getprop(node, "phy_type", phy_type, sizeof(phy_type));
	reg = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USB3_GUSB2PHYCFG0);
	reg &= ~USB3_GUSB2PHYCFG0_USBTRDTIM(0xf);
	if (strcmp(phy_type, "utmi_wide") == 0) {
		reg |= USB3_GUSB2PHYCFG0_PHYIF;
		reg |= USB3_GUSB2PHYCFG0_USBTRDTIM(0x5);
	} else {
		reg &= ~USB3_GUSB2PHYCFG0_PHYIF;
		reg |= USB3_GUSB2PHYCFG0_USBTRDTIM(0x9);
	}
	if (OF_getproplen(node, "snps,dis-u2-freeclk-exists-quirk") == 0)
		reg &= ~USB3_GUSB2PHYCFG0_U2_FREECLK_EXISTS;
	if (OF_getproplen(node, "snps,dis_enblslpm_quirk") == 0)
		reg &= ~USB3_GUSB2PHYCFG0_ENBLSLPM;
	if (OF_getproplen(node, "snps,dis_u2_susphy_quirk") == 0)
		reg &= ~USB3_GUSB2PHYCFG0_SUSPENDUSB20;
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USB3_GUSB2PHYCFG0, reg);

	/* Configure USB3 quirks. */
	reg = bus_space_read_4(sc->sc.iot, sc->sc.ioh, USB3_GUCTL1);
	if (OF_getproplen(node, "snps,dis-tx-ipgap-linecheck-quirk") == 0)
		reg |= USB3_GUCTL1_TX_IPGAP_LINECHECK_DIS;
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, USB3_GUCTL1, reg);

	return 0;
}

/*
 * PHY initialization.
 */

struct xhci_phy {
	const char *compat;
	void (*init)(struct xhci_fdt_softc *, uint32_t *);
};

void exynos5_usbdrd_init(struct xhci_fdt_softc *, uint32_t *);
void imx8mp_usb_init(struct xhci_fdt_softc *, uint32_t *);
void imx8mq_usb_init(struct xhci_fdt_softc *, uint32_t *);
void nop_xceiv_init(struct xhci_fdt_softc *, uint32_t *);

struct xhci_phy xhci_phys[] = {
	{ "fsl,imx8mp-usb-phy", imx8mp_usb_init },
	{ "fsl,imx8mq-usb-phy", imx8mq_usb_init },
	{ "samsung,exynos5250-usbdrd-phy", exynos5_usbdrd_init },
	{ "samsung,exynos5420-usbdrd-phy", exynos5_usbdrd_init },
	{ "usb-nop-xceiv", nop_xceiv_init },
};

uint32_t *
xhci_next_phy(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#phy-cells", 0);
	return cells + ncells + 1;
}

void
xhci_init_phy(struct xhci_fdt_softc *sc, uint32_t *cells)
{
	int node;
	int i;

	node = OF_getnodebyphandle(cells[0]);
	if (node == 0)
		return;

	for (i = 0; i < nitems(xhci_phys); i++) {
		if (OF_is_compatible(node, xhci_phys[i].compat)) {
			xhci_phys[i].init(sc, cells);
			return;
		}
	}
}

void
xhci_phy_enable(struct xhci_fdt_softc *sc, char *name)
{
	uint32_t *phys;
	uint32_t *phy;
	int idx, len;

	idx = OF_getindex(sc->sc_node, name, "phy-names");
	if (idx < 0)
		return;

	len = OF_getproplen(sc->sc_node, "phys");
	if (len <= 0)
		return;

	phys = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "phys", phys, len);

	phy = phys;
	while (phy && phy < phys + (len / sizeof(uint32_t))) {
		if (idx == 0) {
			xhci_init_phy(sc, phy);
			free(phys, M_TEMP, len);
			return;
		}

		phy = xhci_next_phy(phy);
		idx--;
	}
	free(phys, M_TEMP, len);
}

void
xhci_init_phys(struct xhci_fdt_softc *sc)
{
	int rv;

	rv = phy_enable_prop_idx(sc->sc_node, "usb-phy", 0);
	if (rv != 0) {
		rv = phy_enable(sc->sc_node, "usb2-phy");
		if (rv != 0)
			xhci_phy_enable(sc, "usb2-phy");
	}

	rv = phy_enable_prop_idx(sc->sc_node, "usb-phy", 1);
	if (rv != 0) {
		rv = phy_enable(sc->sc_node, "usb3-phy");
		if (rv != 0)
			xhci_phy_enable(sc, "usb3-phy");
	}
}

/*
 * Samsung Exynos 5 PHYs.
 */

/* Registers */
#define EXYNOS5_PHYUTMI			0x0008
#define  EXYNOS5_PHYUTMI_OTGDISABLE	(1 << 6)
#define EXYNOS5_PHYCLKRST		0x0010
#define  EXYNOS5_PHYCLKRST_SSC_EN	(1 << 20)
#define  EXYNOS5_PHYCLKRST_REF_SSP_EN	(1 << 19)
#define  EXYNOS5_PHYCLKRST_PORTRESET	(1 << 1)
#define  EXYNOS5_PHYCLKRST_COMMONONN	(1 << 0)
#define EXYNOS5_PHYTEST			0x0028
#define  EXYNOS5_PHYTEST_POWERDOWN_SSP	(1 << 3)
#define  EXYNOS5_PHYTEST_POWERDOWN_HSP	(1 << 2)

/* PMU registers */
#define EXYNOS5_USBDRD0_POWER		0x0704
#define EXYNOS5420_USBDRD1_POWER	0x0708
#define  EXYNOS5_USBDRD_POWER_EN	(1 << 0)

void
exynos5_usbdrd_init(struct xhci_fdt_softc *sc, uint32_t *cells)
{
	uint32_t phy_reg[2];
	struct regmap *pmurm;
	uint32_t pmureg;
	uint32_t val;
	bus_size_t offset;
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

	/* Power up the PHY block. */
	pmureg = OF_getpropint(node, "samsung,pmu-syscon", 0);
	pmurm = regmap_byphandle(pmureg);
	if (pmurm) {
		node = OF_getnodebyphandle(pmureg);
		if (sc->sc.sc_bus.bdev.dv_unit == 0)
			offset = EXYNOS5_USBDRD0_POWER;
		else
			offset = EXYNOS5420_USBDRD1_POWER;

		val = regmap_read_4(pmurm, offset);
		val |= EXYNOS5_USBDRD_POWER_EN;
		regmap_write_4(pmurm, offset, val);
	}

	/* Initialize the PHY.  Assumes U-Boot has done initial setup. */
	val = bus_space_read_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYTEST);
	CLR(val, EXYNOS5_PHYTEST_POWERDOWN_SSP);
	CLR(val, EXYNOS5_PHYTEST_POWERDOWN_HSP);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYTEST, val);

	bus_space_write_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYUTMI,
	    EXYNOS5_PHYUTMI_OTGDISABLE);

	val = bus_space_read_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYCLKRST);
	SET(val, EXYNOS5_PHYCLKRST_SSC_EN);
	SET(val, EXYNOS5_PHYCLKRST_REF_SSP_EN);
	SET(val, EXYNOS5_PHYCLKRST_COMMONONN);
	SET(val, EXYNOS5_PHYCLKRST_PORTRESET);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYCLKRST, val);
	delay(10);
	CLR(val, EXYNOS5_PHYCLKRST_PORTRESET);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, EXYNOS5_PHYCLKRST, val);
}

/*
 * i.MX8MQ PHYs.
 */

/* Registers */
#define IMX8MQ_PHY_CTRL0			0x0000
#define  IMX8MQ_PHY_CTRL0_REF_SSP_EN			(1 << 2)
#define  IMX8MQ_PHY_CTRL0_FSEL_24M			(0x2a << 5)
#define  IMX8MQ_PHY_CTRL0_FSEL_MASK			(0x3f << 5)
#define IMX8MQ_PHY_CTRL1			0x0004
#define  IMX8MQ_PHY_CTRL1_RESET				(1 << 0)
#define  IMX8MQ_PHY_CTRL1_ATERESET			(1 << 3)
#define  IMX8MQ_PHY_CTRL1_VDATSRCENB0			(1 << 19)
#define  IMX8MQ_PHY_CTRL1_VDATDETENB0			(1 << 20)
#define IMX8MQ_PHY_CTRL2			0x0008
#define  IMX8MQ_PHY_CTRL2_TXENABLEN0			(1 << 8)
#define  IMX8MQ_PHY_CTRL2_OTG_DISABLE			(1 << 9)
#define IMX8MQ_PHY_CTRL6			0x0018
#define  IMX8MQ_PHY_CTRL6_ALT_CLK_SEL			(1 << 0)
#define  IMX8MQ_PHY_CTRL6_ALT_CLK_EN			(1 << 1)

void
imx8mp_usb_init(struct xhci_fdt_softc *sc, uint32_t *cells)
{
	uint32_t phy_reg[2], reg;
	int node, vbus_supply;

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

	clock_set_assigned(node);
	clock_enable_all(node);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0);
	reg &= ~IMX8MQ_PHY_CTRL0_FSEL_MASK;
	reg |= IMX8MQ_PHY_CTRL0_FSEL_24M;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL6);
	reg &= ~(IMX8MQ_PHY_CTRL6_ALT_CLK_SEL | IMX8MQ_PHY_CTRL6_ALT_CLK_EN);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL6, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1);
	reg &= ~(IMX8MQ_PHY_CTRL1_VDATSRCENB0 | IMX8MQ_PHY_CTRL1_VDATDETENB0);
	reg |= IMX8MQ_PHY_CTRL1_RESET | IMX8MQ_PHY_CTRL1_ATERESET;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0);
	reg |= IMX8MQ_PHY_CTRL0_REF_SSP_EN;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL2);
	reg |= IMX8MQ_PHY_CTRL2_TXENABLEN0 | IMX8MQ_PHY_CTRL2_OTG_DISABLE;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL2, reg);

	delay(10);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1);
	reg &= ~(IMX8MQ_PHY_CTRL1_RESET | IMX8MQ_PHY_CTRL1_ATERESET);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1, reg);

	vbus_supply = OF_getpropint(node, "vbus-supply", 0);
	if (vbus_supply)
		regulator_enable(vbus_supply);
}

void
imx8mq_usb_init(struct xhci_fdt_softc *sc, uint32_t *cells)
{
	uint32_t phy_reg[2], reg;
	int node, vbus_supply;

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

	clock_set_assigned(node);
	clock_enable_all(node);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1);
	reg &= ~(IMX8MQ_PHY_CTRL1_VDATSRCENB0 | IMX8MQ_PHY_CTRL1_VDATDETENB0);
	reg |= IMX8MQ_PHY_CTRL1_RESET | IMX8MQ_PHY_CTRL1_ATERESET;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0);
	reg |= IMX8MQ_PHY_CTRL0_REF_SSP_EN;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL0, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL2);
	reg |= IMX8MQ_PHY_CTRL2_TXENABLEN0;
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL2, reg);

	reg = bus_space_read_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1);
	reg &= ~(IMX8MQ_PHY_CTRL1_RESET | IMX8MQ_PHY_CTRL1_ATERESET);
	bus_space_write_4(sc->sc.iot, sc->ph_ioh, IMX8MQ_PHY_CTRL1, reg);

	vbus_supply = OF_getpropint(node, "vbus-supply", 0);
	if (vbus_supply)
		regulator_enable(vbus_supply);
}

void
nop_xceiv_init(struct xhci_fdt_softc *sc, uint32_t *cells)
{
	uint32_t vcc_supply;
	int node;

	node = OF_getnodebyphandle(cells[0]);
	KASSERT(node != 0);

	vcc_supply = OF_getpropint(node, "vcc-supply", 0);
	if (vcc_supply)
		regulator_enable(vcc_supply);
}
