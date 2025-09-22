/*	$OpenBSD: mvsw.c,v 1.5 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/mii.h>

/* Registers */

/* SMI registers */
#define MVSW_SMI_CMD			0x00
#define  MVSW_SMI_CMD_BUSY		0x8000
#define  MVSW_SMI_CMD_C45		0x0000
#define  MVSW_SMI_CMD_C22		0x1000
#define  MVSW_SMI_CMD_C22_READ		0x0800
#define  MVSW_SMI_CMD_C22_WRITE		0x0400
#define  MVSW_SMI_CMD_C45_READ		0x0c00
#define  MVSW_SMI_CMD_C45_READINC	0x0800
#define  MVSW_SMI_CMD_C45_WRITE		0x0400
#define  MVSW_SMI_CMD_C45_ADDR		0x0000
#define  MVSW_SMI_CMD_DEVAD(x)		((x) << 5)
#define  MVSW_SMI_CMD_REGAD(x)		((x) << 0)
#define MVSW_SMI_DATA			0x01

#define MVSW_SMI_TIMEOUT	1600

/* Switch registers */
#define MVSW_PORT(x)			(0x10 + (x))
#define MVSW_G2				0x1c

#define MVSW_PORT_SWITCHID		0x03
#define  MVSW_PORT_SWITCHID_PROD_MASK	0xfff0
#define  MVSW_PORT_SWITCHID_PROD_88E6141 0x3400
#define  MVSW_PORT_SWITCHID_PROD_88E6341 0x3410
#define  MVSW_PORT_SWITCHID_REV_MASK	0x000f
#define MVSW_PORT_CTRL			0x04
#define  MVSW_PORT_CTRL_STATE_MASK	0x0003
#define  MVSW_PORT_CTRL_STATE_FORWARD	0x0003
#define MVSW_G2_SMI_PHY_CMD		0x18
#define MVSW_G2_SMI_PHY_DATA		0x19

/* SERDES registers */
#define MVSW_SERDES(x)			(0x10 + (x))
#define MVSW_SERDES_BMCR		(0x2000 + MII_BMCR)

/* XXX #include <dev/mii/mdio.h> */
#define MDIO_MMD_PHYXS		4

struct mvsw_softc {
	struct device	sc_dev;

	struct mii_bus	*sc_mdio;
	int 		sc_reg;
};

int	mvsw_match(struct device *, void *, void *);
void	mvsw_attach(struct device *, struct device *, void *);

const struct cfattach mvsw_ca = {
	sizeof (struct mvsw_softc), mvsw_match, mvsw_attach
};

struct cfdriver mvsw_cd = {
	NULL, "mvsw", DV_DULL
};

int	mvsw_smi_read(struct mvsw_softc *, int, int);
void	mvsw_smi_write(struct mvsw_softc *, int, int, int);
int	mvsw_phy_read(struct mvsw_softc *, int, int);
void	mvsw_phy_write(struct mvsw_softc *, int, int, int);
int	mvsw_serdes_read(struct mvsw_softc *, int, int, int);
void	mvsw_serdes_write(struct mvsw_softc *, int, int, int, int);

void	mvsw_port_enable(struct mvsw_softc *, int);
void	mvsw_phy_enable(struct mvsw_softc *, int);
void	mvsw_serdes_enable(struct mvsw_softc *, int);

int
mvsw_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,mv88e6085");
}

void
mvsw_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvsw_softc *sc = (struct mvsw_softc *)self;
	struct fdt_attach_args *faa = aux;
	int ports, port, node;
	uint32_t phy;
	uint16_t swid;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_reg = faa->fa_reg[0].addr;
	printf(" phy %d", sc->sc_reg);

	sc->sc_mdio = mii_bynode(OF_parent(faa->fa_node));
	if (sc->sc_mdio == NULL) {
		printf(": can't find mdio bus\n");
		return;
	}

	swid = mvsw_smi_read(sc, MVSW_PORT(0), MVSW_PORT_SWITCHID);
	switch (swid & MVSW_PORT_SWITCHID_PROD_MASK) {
	case MVSW_PORT_SWITCHID_PROD_88E6141:
		printf(": 88E6141");
		break;
	case MVSW_PORT_SWITCHID_PROD_88E6341:
		printf(": 88E6341");
		break;
	default:
		printf(": unknown product 0x%04x\n",
		   swid & MVSW_PORT_SWITCHID_PROD_MASK);
		return;
	}
	printf(" rev %d\n", swid & MVSW_PORT_SWITCHID_REV_MASK);

	ports = OF_getnodebyname(faa->fa_node, "ports");
	if (ports == 0)
		return;
	for (port = OF_child(ports); port; port = OF_peer(port)) {
		phy = OF_getpropint(port, "phy-handle", 0);
		node = OF_getnodebyphandle(phy);
		if (node)
			mvsw_phy_enable(sc, node);
		else
			mvsw_serdes_enable(sc, port);

		mvsw_port_enable(sc, port);
	}
}

static inline int
mvsw_read(struct mvsw_softc *sc, int reg)
{
	struct mii_bus *md = sc->sc_mdio;
	return md->md_readreg(md->md_cookie, sc->sc_reg, reg);
}

static inline void
mvsw_write(struct mvsw_softc *sc, int reg, int val)
{
	struct mii_bus *md = sc->sc_mdio;
	md->md_writereg(md->md_cookie, sc->sc_reg, reg, val);
}

int
mvsw_smi_wait(struct mvsw_softc *sc)
{
	int i;

	for (i = 0; i < MVSW_SMI_TIMEOUT; i++) {
		if ((mvsw_read(sc, MVSW_SMI_CMD) & MVSW_SMI_CMD_BUSY) == 0)
			return 0;
		delay(10);
	}

	printf("%s: SMI timeout\n", sc->sc_dev.dv_xname);
	return ETIMEDOUT;
}

int
mvsw_smi_read(struct mvsw_softc *sc, int phy, int reg)
{
	if (mvsw_smi_wait(sc))
		return -1;

	mvsw_write(sc, MVSW_SMI_CMD, MVSW_SMI_CMD_BUSY |
	    MVSW_SMI_CMD_DEVAD(phy) | MVSW_SMI_CMD_REGAD(reg) |
	    MVSW_SMI_CMD_C22 | MVSW_SMI_CMD_C22_READ);

	if (mvsw_smi_wait(sc))
		return -1;

	return mvsw_read(sc, MVSW_SMI_DATA);
}

void
mvsw_smi_write(struct mvsw_softc *sc, int phy, int reg, int val)
{
	if (mvsw_smi_wait(sc))
		return;

	mvsw_write(sc, MVSW_SMI_DATA, val);
	mvsw_write(sc, MVSW_SMI_CMD, MVSW_SMI_CMD_BUSY |
	    MVSW_SMI_CMD_DEVAD(phy) | MVSW_SMI_CMD_REGAD(reg) |
	    MVSW_SMI_CMD_C22 | MVSW_SMI_CMD_C22_WRITE);

	mvsw_smi_wait(sc);
}

int
mvsw_phy_wait(struct mvsw_softc *sc)
{
	int i;

	for (i = 0; i < MVSW_SMI_TIMEOUT; i++) {
		if ((mvsw_smi_read(sc, MVSW_G2,
		    MVSW_G2_SMI_PHY_CMD) & MVSW_SMI_CMD_BUSY) == 0)
			return 0;
		delay(10);
	}

	printf("%s: SMI PHY timeout\n", sc->sc_dev.dv_xname);
	return ETIMEDOUT;
}

int
mvsw_phy_read(struct mvsw_softc *sc, int phy, int reg)
{
	if (mvsw_phy_wait(sc))
		return -1;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(phy) | MVSW_SMI_CMD_REGAD(reg) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C22 | MVSW_SMI_CMD_C22_READ);

	if (mvsw_phy_wait(sc))
		return -1;

	return mvsw_smi_read(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA);
}

void
mvsw_phy_write(struct mvsw_softc *sc, int phy, int reg, int val)
{
	if (mvsw_phy_wait(sc))
		return;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA, val);
	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(phy) | MVSW_SMI_CMD_REGAD(reg) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C22 | MVSW_SMI_CMD_C22_WRITE);
}

int
mvsw_serdes_read(struct mvsw_softc *sc, int port, int dev, int addr)
{
	if (mvsw_phy_wait(sc))
		return -1;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA, addr);
	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(port) | MVSW_SMI_CMD_REGAD(dev) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C45 | MVSW_SMI_CMD_C45_ADDR);

	if (mvsw_phy_wait(sc))
		return -1;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(port) | MVSW_SMI_CMD_REGAD(dev) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C45 | MVSW_SMI_CMD_C45_READ);

	if (mvsw_phy_wait(sc))
		return -1;

	return mvsw_smi_read(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA);
}

void
mvsw_serdes_write(struct mvsw_softc *sc, int port, int dev, int addr, int val)
{
	if (mvsw_phy_wait(sc))
		return;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA, addr);
	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(port) | MVSW_SMI_CMD_REGAD(dev) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C45 | MVSW_SMI_CMD_C45_ADDR);

	if (mvsw_phy_wait(sc))
		return;

	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_DATA, val);
	mvsw_smi_write(sc, MVSW_G2, MVSW_G2_SMI_PHY_CMD,
	    MVSW_SMI_CMD_DEVAD(port) | MVSW_SMI_CMD_REGAD(dev) |
	    MVSW_SMI_CMD_BUSY | MVSW_SMI_CMD_C45 | MVSW_SMI_CMD_C45_WRITE);
}

void
mvsw_port_enable(struct mvsw_softc *sc, int node)
{
	uint16_t val;
	int port;

	port = OF_getpropint(node, "reg", -1);
	if (port == -1)
		return;

	/* Enable port. */
	val = mvsw_smi_read(sc, MVSW_PORT(port), MVSW_PORT_CTRL);
	val &= ~MVSW_PORT_CTRL_STATE_MASK;
	val |= MVSW_PORT_CTRL_STATE_FORWARD;
	mvsw_smi_write(sc, MVSW_PORT(port), MVSW_PORT_CTRL, val);
}

void
mvsw_phy_enable(struct mvsw_softc *sc, int node)
{
	uint16_t val;
	int phy;

	phy = OF_getpropint(node, "reg", -1);
	if (phy == -1)
		return;

	/* Power-up PHY. */
	val = mvsw_phy_read(sc, phy, MII_BMCR);
	val &= ~BMCR_PDOWN;
	mvsw_phy_write(sc, phy, MII_BMCR, val);
}

void
mvsw_serdes_enable(struct mvsw_softc *sc, int node)
{
	uint16_t val;
	int port;

	port = OF_getpropint(node, "reg", -1);
	if (port == -1)
		return;

	/* Power-up SERDES. */
	val = mvsw_serdes_read(sc, MVSW_SERDES(port),
	    MDIO_MMD_PHYXS, MVSW_SERDES_BMCR);
	val &= ~BMCR_PDOWN;
	val |= BMCR_AUTOEN;
	mvsw_serdes_write(sc, MVSW_SERDES(port),
	    MDIO_MMD_PHYXS, MVSW_SERDES_BMCR, val);
}
