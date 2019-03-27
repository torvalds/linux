/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/tegra_efuse.h>

#include <gnu/dts/include/dt-bindings/pinctrl/pinctrl-tegra-xusb.h>

#include "phydev_if.h"

/* FUSE calibration data. */
#define	FUSE_XUSB_CALIB				0x0F0
#define	  FUSE_XUSB_CALIB_HS_CURR_LEVEL_123(x)		(((x) >> 15) & 0x3F);
#define	  FUSE_XUSB_CALIB_HS_IREF_CAP(x)		(((x) >> 13) & 0x03);
#define	  FUSE_XUSB_CALIB_HS_SQUELCH_LEVEL(x)		(((x) >> 11) & 0x03);
#define	  FUSE_XUSB_CALIB_HS_TERM_RANGE_ADJ(x)		(((x) >>  7) & 0x0F);
#define	  FUSE_XUSB_CALIB_HS_CURR_LEVEL_0(x)		(((x) >>  0) & 0x3F);


/* Registers. */
#define	XUSB_PADCTL_USB2_PAD_MUX		0x004

#define	XUSB_PADCTL_USB2_PORT_CAP		0x008
#define	 USB2_PORT_CAP_ULPI_PORT_INTERNAL		(1 << 25)
#define	 USB2_PORT_CAP_ULPI_PORT_CAP			(1 << 24)
#define	 USB2_PORT_CAP_PORT_REVERSE_ID(p)		(1 << (3 + (p) * 4))
#define	 USB2_PORT_CAP_PORT_INTERNAL(p)			(1 << (2 + (p) * 4))
#define	 USB2_PORT_CAP_PORT_CAP(p, x)			(((x) & 3) << ((p) * 4))
#define	  USB2_PORT_CAP_PORT_CAP_OTG			0x3
#define	  USB2_PORT_CAP_PORT_CAP_DEVICE			0x2
#define	  USB2_PORT_CAP_PORT_CAP_HOST			0x1
#define	  USB2_PORT_CAP_PORT_CAP_DISABLED		0x0

#define	XUSB_PADCTL_SS_PORT_MAP			0x014
#define	 SS_PORT_MAP_PORT_INTERNAL(p)			(1 << (3 + (p) * 4))
#define	 SS_PORT_MAP_PORT_MAP(p, x)			(((x) & 7) << ((p) * 4))

#define	XUSB_PADCTL_ELPG_PROGRAM		0x01C
#define	 ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN		(1 << 26)
#define	 ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY	(1 << 25)
#define	 ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN		(1 << 24)
#define	 ELPG_PROGRAM_SSP_ELPG_VCORE_DOWN(x) 		(1 << (18 + (x) * 4))
#define	 ELPG_PROGRAM_SSP_ELPG_CLAMP_EN_EARLY(x) 	(1 << (17 + (x) * 4))
#define	 ELPG_PROGRAM_SSP_ELPG_CLAMP_EN(x)		(1 << (16 + (x) * 4))

#define	XUSB_PADCTL_IOPHY_PLL_P0_CTL1		0x040
#define	 IOPHY_PLL_P0_CTL1_PLL0_LOCKDET			(1 << 19)
#define	 IOPHY_PLL_P0_CTL1_REFCLK_SEL(x)		(((x) & 0xF) << 12)
#define	 IOPHY_PLL_P0_CTL1_PLL_RST			(1 << 1)

#define	XUSB_PADCTL_IOPHY_PLL_P0_CTL2		0x044
#define	 IOPHY_PLL_P0_CTL2_REFCLKBUF_EN			(1 << 6)
#define	 IOPHY_PLL_P0_CTL2_TXCLKREF_EN			(1 << 5)
#define	 IOPHY_PLL_P0_CTL2_TXCLKREF_SEL			(1 << 4)

#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2(x) 	(0x058 + (x) * 4)
#define	 IOPHY_USB3_PAD_CTL2_CDR_CNTL(x)		(((x) & 0x00FF) <<  4)
#define	 IOPHY_USB3_PAD_CTL2_RX_EQ(x)			(((x) & 0xFFFF) <<  8)
#define	 IOPHY_USB3_PAD_CTL2_RX_WANDER(x)		(((x) & 0x000F) <<  4)
#define	 IOPHY_USB3_PAD_CTL2_RX_TERM_CNTL(x)		(((x) & 0x0003) <<  2)
#define	 IOPHY_USB3_PAD_CTL2_TX_TERM_CNTL(x)		(((x) & 0x0003) <<  0)


#define	XUSB_PADCTL_IOPHY_USB3_PAD_CTL4(x)	(0x068 + (x) * 4)

#define	XUSB_PADCTL_USB2_OTG_PAD_CTL0(x) 	(0x0A0 + (x) * 4)
#define	 USB2_OTG_PAD_CTL0_LSBIAS_SEL			(1 << 23)
#define	 USB2_OTG_PAD_CTL0_DISCON_DETECT_METHOD		(1 << 22)
#define	 USB2_OTG_PAD_CTL0_PD_ZI			(1 << 21)
#define	 USB2_OTG_PAD_CTL0_PD2				(1 << 20)
#define	 USB2_OTG_PAD_CTL0_PD				(1 << 19)
#define	 USB2_OTG_PAD_CTL0_TERM_EN			(1 << 18)
#define	 USB2_OTG_PAD_CTL0_LS_LS_FSLEW(x)		(((x) & 0x03) << 16)
#define	 USB2_OTG_PAD_CTL0_LS_RSLEW(x)			(((x) & 0x03) << 14)
#define	 USB2_OTG_PAD_CTL0_FS_SLEW(x)			(((x) & 0x03) << 12)
#define	 USB2_OTG_PAD_CTL0_HS_SLEW(x)			(((x) & 0x3F) <<  6)
#define	 USB2_OTG_PAD_CTL0_HS_CURR_LEVEL(x)		(((x) & 0x3F) <<  0)

#define XUSB_PADCTL_USB2_OTG_PAD_CTL1(x) 	(0x0AC + (x) * 4)
#define	 USB2_OTG_PAD_CTL1_RPU_RANGE_ADJ(x)		(((x) & 0x3) << 11)
#define	 USB2_OTG_PAD_CTL1_HS_IREF_CAP(x)		(((x) & 0x3) <<  9)
#define	 USB2_OTG_PAD_CTL1_SPARE(x)			(((x) & 0x3) <<  7)
#define	 USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ(x)		(((x) & 0xF) <<  3)
#define	 USB2_OTG_PAD_CTL1_PD_DR			(1 <<  2)
#define	 USB2_OTG_PAD_CTL1_PD_DISC_FORCE_POWERUP	(1 <<  1)
#define	 USB2_OTG_PAD_CTL1_PD_CHRP_FORCE_POWERUP	(1 <<  0)

#define	XUSB_PADCTL_USB2_BIAS_PAD_CTL0		0x0B8
#define	 USB2_BIAS_PAD_CTL0_ADJRPU(x)			(((x) & 0x7) << 14)
#define	 USB2_BIAS_PAD_CTL0_PD_TRK			(1 << 13)
#define	 USB2_BIAS_PAD_CTL0_PD				(1 << 12)
#define	 USB2_BIAS_PAD_CTL0_TERM_OFFSETL(x)		(((x) & 0x3) <<  9)
#define	 USB2_BIAS_PAD_CTL0_VBUS_LEVEL(x)		(((x) & 0x3) <<  7)
#define	 USB2_BIAS_PAD_CTL0_HS_CHIRP_LEVEL(x)		(((x) & 0x3) <<  5)
#define	 USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL(x)		(((x) & 0x7) <<  2)
#define	 USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL(x)		(((x) & 0x3) <<  0)

#define	XUSB_PADCTL_HSIC_PAD0_CTL0		0x0C8
#define	 HSIC_PAD0_CTL0_HSIC_OPT(x)			(((x) & 0xF) << 16)
#define	 HSIC_PAD0_CTL0_TX_SLEWN(x)			(((x) & 0xF) << 12)
#define	 HSIC_PAD0_CTL0_TX_SLEWP(x)			(((x) & 0xF) <<  8)
#define	 HSIC_PAD0_CTL0_TX_RTUNEN(x)			(((x) & 0xF) <<  4)
#define	 HSIC_PAD0_CTL0_TX_RTUNEP(x)			(((x) & 0xF) <<  0)

#define	XUSB_PADCTL_USB3_PAD_MUX		0x134
#define	 USB3_PAD_MUX_PCIE_IDDQ_DISABLE(x) 		(1 << (1 + (x)))
#define	 USB3_PAD_MUX_SATA_IDDQ_DISABLE 		(1 << 6)


#define	XUSB_PADCTL_IOPHY_PLL_S0_CTL1		0x138
#define	 IOPHY_PLL_S0_CTL1_PLL1_LOCKDET			(1 << 27)
#define	 IOPHY_PLL_S0_CTL1_PLL1_MODE			(1 << 24)
#define	 IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD			(1 << 3)
#define	 IOPHY_PLL_S0_CTL1_PLL_RST_L			(1 << 1)
#define	 IOPHY_PLL_S0_CTL1_PLL_IDDQ			(1 << 0)

#define	XUSB_PADCTL_IOPHY_PLL_S0_CTL2		0x13C
#define	XUSB_PADCTL_IOPHY_PLL_S0_CTL3		0x140
#define	XUSB_PADCTL_IOPHY_PLL_S0_CTL4		0x144

#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1	0x148
#define	 IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD		(1 << 1)
#define	 IOPHY_MISC_PAD_S0_CTL1_IDDQ			(1 << 0)

#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL2	0x14C
#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL3	0x150
#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL4	0x154
#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL5	0x158
#define	XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL6	0x15C


#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))


struct padctl_softc {
	device_t	dev;
	struct resource	*mem_res;
	hwreset_t	rst;
	int		phy_ena_cnt;

	/* Fuses calibration data */
	uint32_t	hs_curr_level_0;
	uint32_t	hs_curr_level_123;
	uint32_t	hs_iref_cap;
	uint32_t	hs_term_range_adj;
	uint32_t	hs_squelch_level;

	uint32_t	hs_curr_level_offset;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-xusb-padctl",	1},
	{NULL,				0},
};

/* Ports. */
enum padctl_port_type {
	PADCTL_PORT_USB2,
	PADCTL_PORT_ULPI,
	PADCTL_PORT_HSIC,
	PADCTL_PORT_USB3,
};

struct padctl_lane;
struct padctl_port {
	enum padctl_port_type	type;
	const char		*name;
	const char		*base_name;
	int			idx;
	int			(*init)(struct padctl_softc *sc,
				    struct padctl_port *port);

	/* Runtime data. */
	bool			enabled;
	regulator_t		supply_vbus;	/* USB2, USB3 */
	bool			internal;	/* ULPI, USB2, USB3 */
	uint32_t		companion;	/* USB3 */
	struct padctl_lane	*lane;
};

static int usb3_port_init(struct padctl_softc *sc, struct padctl_port *port);

#define	PORT(t, n, p, i) {						\
	.type = t,							\
	.name = n "-" #p,						\
	.base_name = n,							\
	.idx = p,							\
	.init = i,							\
}
static struct padctl_port ports_tbl[] = {
	PORT(PADCTL_PORT_USB2, "usb2", 0, NULL),
	PORT(PADCTL_PORT_USB2, "usb2", 1, NULL),
	PORT(PADCTL_PORT_USB2, "usb2", 2, NULL),
	PORT(PADCTL_PORT_ULPI, "ulpi", 0, NULL),
	PORT(PADCTL_PORT_HSIC, "hsic", 0, NULL),
	PORT(PADCTL_PORT_HSIC, "hsic", 1, NULL),
	PORT(PADCTL_PORT_USB3, "usb3", 0, usb3_port_init),
	PORT(PADCTL_PORT_USB3, "usb3", 1, usb3_port_init),
};

/* Pads - a group of lannes. */
enum padctl_pad_type {
	PADCTL_PAD_USB2,
	PADCTL_PAD_ULPI,
	PADCTL_PAD_HSIC,
	PADCTL_PAD_PCIE,
	PADCTL_PAD_SATA,
};

struct padctl_lane;
struct padctl_pad {
	const char		*name;
	enum padctl_pad_type	type;
	int			(*powerup)(struct padctl_softc *sc,
				    struct padctl_lane *lane);
	int			(*powerdown)(struct padctl_softc *sc,
				    struct padctl_lane *lane);
	/* Runtime data. */
	bool			enabled;
	struct padctl_lane	*lanes[8]; 	/* Safe maximum value. */
	int			nlanes;
};

static int usb2_powerup(struct padctl_softc *sc, struct padctl_lane *lane);
static int usb2_powerdown(struct padctl_softc *sc, struct padctl_lane *lane);
static int pcie_powerup(struct padctl_softc *sc, struct padctl_lane *lane);
static int pcie_powerdown(struct padctl_softc *sc, struct padctl_lane *lane);
static int sata_powerup(struct padctl_softc *sc, struct padctl_lane *lane);
static int sata_powerdown(struct padctl_softc *sc, struct padctl_lane *lane);

#define	PAD(n, t, u, d) {						\
	.name = n,							\
	.type = t,							\
	.powerup = u,							\
	.powerdown = d,							\
}
static struct padctl_pad pads_tbl[] = {
	PAD("usb2", PADCTL_PAD_USB2, usb2_powerup, usb2_powerdown),
	PAD("ulpi", PADCTL_PAD_ULPI, NULL, NULL),
	PAD("hsic", PADCTL_PAD_HSIC, NULL, NULL),
	PAD("pcie", PADCTL_PAD_PCIE, pcie_powerup, pcie_powerdown),
	PAD("sata", PADCTL_PAD_SATA, sata_powerup, sata_powerdown),
};

/* Lanes. */
static char *otg_mux[] = {"snps", "xusb", "uart", "rsvd"};
static char *usb_mux[] = {"snps", "xusb"};
static char *pci_mux[] = {"pcie", "usb3-ss", "sata", "rsvd"};

struct padctl_lane {
	const char		*name;
	int			idx;
	bus_size_t		reg;
	uint32_t		shift;
	uint32_t		mask;
	char			**mux;
	int			nmux;
	/* Runtime data. */
	bool			enabled;
	struct padctl_pad	*pad;
	struct padctl_port	*port;
	int			mux_idx;

};

#define	LANE(n, p, r, s, m, mx) {					\
	.name = n "-" #p,						\
	.idx = p,							\
	.reg = r,							\
	.shift = s,							\
	.mask = m,							\
	.mux = mx,							\
	.nmux = nitems(mx),						\
}
static struct padctl_lane lanes_tbl[] = {
	LANE("usb2", 0, XUSB_PADCTL_USB2_PAD_MUX,  0, 0x3, otg_mux),
	LANE("usb2", 1, XUSB_PADCTL_USB2_PAD_MUX,  2, 0x3, otg_mux),
	LANE("usb2", 2, XUSB_PADCTL_USB2_PAD_MUX,  4, 0x3, otg_mux),
	LANE("ulpi", 0, XUSB_PADCTL_USB2_PAD_MUX, 12, 0x1, usb_mux),
	LANE("hsic", 0, XUSB_PADCTL_USB2_PAD_MUX, 14, 0x1, usb_mux),
	LANE("hsic", 1, XUSB_PADCTL_USB2_PAD_MUX, 15, 0x1, usb_mux),
	LANE("pcie", 0, XUSB_PADCTL_USB3_PAD_MUX, 16, 0x3, pci_mux),
	LANE("pcie", 1, XUSB_PADCTL_USB3_PAD_MUX, 18, 0x3, pci_mux),
	LANE("pcie", 2, XUSB_PADCTL_USB3_PAD_MUX, 20, 0x3, pci_mux),
	LANE("pcie", 3, XUSB_PADCTL_USB3_PAD_MUX, 22, 0x3, pci_mux),
	LANE("pcie", 4, XUSB_PADCTL_USB3_PAD_MUX, 24, 0x3, pci_mux),
	LANE("sata", 0, XUSB_PADCTL_USB3_PAD_MUX, 26, 0x3, pci_mux),
};

/* Define all possible mappings for USB3 port lanes */
struct padctl_lane_map {
	int			port_idx;
	enum padctl_pad_type	pad_type;
	int			lane_idx;
};

#define	LANE_MAP(pi, pt, li) {						\
	.port_idx = pi,							\
	.pad_type = pt,							\
	.lane_idx = li,							\
}
static struct padctl_lane_map lane_map_tbl[] = {
	LANE_MAP(0, PADCTL_PAD_PCIE, 0), 	/* port USB3-0 -> lane PCIE-0 */
	LANE_MAP(1, PADCTL_PAD_PCIE, 1), 	/* port USB3-1 -> lane PCIE-1 */
						/* -- or -- */
	LANE_MAP(1, PADCTL_PAD_SATA, 0), 	/* port USB3-1 -> lane SATA-0 */
};

 /* Phy class and methods. */
static int xusbpadctl_phy_enable(struct phynode *phy, bool enable);
static phynode_method_t xusbpadctl_phynode_methods[] = {
	PHYNODEMETHOD(phynode_enable,	xusbpadctl_phy_enable),
	PHYNODEMETHOD_END

};
DEFINE_CLASS_1(xusbpadctl_phynode, xusbpadctl_phynode_class,
    xusbpadctl_phynode_methods, 0, phynode_class);

static struct padctl_port *search_lane_port(struct padctl_softc *sc,
    struct padctl_lane *lane);
/* -------------------------------------------------------------------------
 *
 *   PHY functions
 */
static int
usb3_port_init(struct padctl_softc *sc, struct padctl_port *port)
{
	uint32_t reg;

	reg = RD4(sc, XUSB_PADCTL_SS_PORT_MAP);
	if (port->internal)
		reg &= ~SS_PORT_MAP_PORT_INTERNAL(port->idx);
	else
		reg |= SS_PORT_MAP_PORT_INTERNAL(port->idx);
	reg &= ~SS_PORT_MAP_PORT_MAP(port->idx, ~0);
	reg |= SS_PORT_MAP_PORT_MAP(port->idx, port->companion);
	WR4(sc, XUSB_PADCTL_SS_PORT_MAP, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_USB3_PAD_CTL2(port->idx));
	reg &= ~IOPHY_USB3_PAD_CTL2_CDR_CNTL(~0);
	reg &= ~IOPHY_USB3_PAD_CTL2_RX_EQ(~0);
	reg &= ~IOPHY_USB3_PAD_CTL2_RX_WANDER(~0);
	reg |= IOPHY_USB3_PAD_CTL2_CDR_CNTL(0x24);
	reg |= IOPHY_USB3_PAD_CTL2_RX_EQ(0xF070);
	reg |= IOPHY_USB3_PAD_CTL2_RX_WANDER(0xF);
	WR4(sc, XUSB_PADCTL_IOPHY_USB3_PAD_CTL2(port->idx), reg);

	WR4(sc, XUSB_PADCTL_IOPHY_USB3_PAD_CTL4(port->idx),
	    0x002008EE);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_SSP_ELPG_VCORE_DOWN(port->idx);
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_SSP_ELPG_CLAMP_EN_EARLY(port->idx);
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_SSP_ELPG_CLAMP_EN(port->idx);
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	return (0);
}

static int
pcie_powerup(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;
	int i;

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	reg &= ~IOPHY_PLL_P0_CTL1_REFCLK_SEL(~0);
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL2);
	reg |= IOPHY_PLL_P0_CTL2_REFCLKBUF_EN;
	reg |= IOPHY_PLL_P0_CTL2_TXCLKREF_EN;
	reg |= IOPHY_PLL_P0_CTL2_TXCLKREF_SEL;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL2, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	reg |= IOPHY_PLL_P0_CTL1_PLL_RST;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1, reg);
	DELAY(100);

	for (i = 100; i > 0; i--) {
		reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
		if (reg & IOPHY_PLL_P0_CTL1_PLL0_LOCKDET)
			break;
		DELAY(10);
	}
	if (i <= 0) {
		device_printf(sc->dev, "Failed to power up PCIe phy\n");
		return (ETIMEDOUT);
	}
	reg = RD4(sc, XUSB_PADCTL_USB3_PAD_MUX);
	reg |= USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->idx);
	WR4(sc, XUSB_PADCTL_USB3_PAD_MUX, reg);

	return (0);
}

static int
pcie_powerdown(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;

	reg = RD4(sc, XUSB_PADCTL_USB3_PAD_MUX);
	reg &= ~USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->idx);
	WR4(sc, XUSB_PADCTL_USB3_PAD_MUX, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	reg &= ~IOPHY_PLL_P0_CTL1_PLL_RST;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_P0_CTL1, reg);
	DELAY(100);

	return (0);

}

static int
sata_powerup(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;
	int i;

	reg = RD4(sc, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);
	reg &= ~IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD;
	reg &= ~IOPHY_MISC_PAD_S0_CTL1_IDDQ;
	WR4(sc, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg &= ~IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD;
	reg &= ~IOPHY_PLL_S0_CTL1_PLL_IDDQ;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg |= IOPHY_PLL_S0_CTL1_PLL1_MODE;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg |= IOPHY_PLL_S0_CTL1_PLL_RST_L;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);

	for (i = 100; i >= 0; i--) {
		reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
		if (reg & IOPHY_PLL_S0_CTL1_PLL1_LOCKDET)
			break;
		DELAY(100);
	}
	if (i <= 0) {
		device_printf(sc->dev, "Failed to power up SATA phy\n");
		return (ETIMEDOUT);
	}
	reg = RD4(sc, XUSB_PADCTL_USB3_PAD_MUX);
	reg |= IOPHY_PLL_S0_CTL1_PLL_RST_L;
	WR4(sc, XUSB_PADCTL_USB3_PAD_MUX, reg);

	reg = RD4(sc, XUSB_PADCTL_USB3_PAD_MUX);
	reg |= USB3_PAD_MUX_SATA_IDDQ_DISABLE;
	WR4(sc, XUSB_PADCTL_USB3_PAD_MUX, reg);

	return (0);
}

static int
sata_powerdown(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;

	reg = RD4(sc, XUSB_PADCTL_USB3_PAD_MUX);
	reg &= ~USB3_PAD_MUX_SATA_IDDQ_DISABLE;
	WR4(sc, XUSB_PADCTL_USB3_PAD_MUX, reg);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg &= ~IOPHY_PLL_S0_CTL1_PLL_RST_L;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg &= ~IOPHY_PLL_S0_CTL1_PLL1_MODE;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	reg |= IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD;
	reg |= IOPHY_PLL_S0_CTL1_PLL_IDDQ;
	WR4(sc, XUSB_PADCTL_IOPHY_PLL_S0_CTL1, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);
	reg |= IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD;
	reg |= IOPHY_MISC_PAD_S0_CTL1_IDDQ;
	WR4(sc, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1, reg);
	DELAY(100);

	return (0);
}

static int
usb2_powerup(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;
	struct padctl_port *port;
	int rv;

	port = search_lane_port(sc, lane);
	if (port == NULL) {
		device_printf(sc->dev, "Cannot find port for lane: %s\n",
		    lane->name);
	}
	reg = RD4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	reg &= ~USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL(~0);
	reg &= ~USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL(~0);
	reg |= USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL(sc->hs_squelch_level);
	reg |= USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL(5);
	WR4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0, reg);

	reg = RD4(sc, XUSB_PADCTL_USB2_PORT_CAP);
	reg &= ~USB2_PORT_CAP_PORT_CAP(lane->idx, ~0);
	reg |= USB2_PORT_CAP_PORT_CAP(lane->idx, USB2_PORT_CAP_PORT_CAP_HOST);
	WR4(sc, XUSB_PADCTL_USB2_PORT_CAP, reg);

	reg = RD4(sc, XUSB_PADCTL_USB2_OTG_PAD_CTL0(lane->idx));
	reg &= ~USB2_OTG_PAD_CTL0_HS_CURR_LEVEL(~0);
	reg &= ~USB2_OTG_PAD_CTL0_HS_SLEW(~0);
	reg &= ~USB2_OTG_PAD_CTL0_LS_RSLEW(~0);
	reg &= ~USB2_OTG_PAD_CTL0_PD;
	reg &= ~USB2_OTG_PAD_CTL0_PD2;
	reg &= ~USB2_OTG_PAD_CTL0_PD_ZI;

	reg |= USB2_OTG_PAD_CTL0_HS_SLEW(14);
	if (lane->idx == 0) {
		reg |= USB2_OTG_PAD_CTL0_HS_CURR_LEVEL(sc->hs_curr_level_0);
		reg |= USB2_OTG_PAD_CTL0_LS_RSLEW(3);
	} else {
		reg |= USB2_OTG_PAD_CTL0_HS_CURR_LEVEL(sc->hs_curr_level_123);
		reg |= USB2_OTG_PAD_CTL0_LS_RSLEW(0);
	}
	WR4(sc, XUSB_PADCTL_USB2_OTG_PAD_CTL0(lane->idx), reg);

	reg = RD4(sc, XUSB_PADCTL_USB2_OTG_PAD_CTL1(lane->idx));
	reg &= ~USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ(~0);
	reg &= ~USB2_OTG_PAD_CTL1_HS_IREF_CAP(~0);
	reg &= ~USB2_OTG_PAD_CTL1_PD_DR;
	reg &= ~USB2_OTG_PAD_CTL1_PD_DISC_FORCE_POWERUP;
	reg &= ~USB2_OTG_PAD_CTL1_PD_CHRP_FORCE_POWERUP;

	reg |= USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ(sc->hs_term_range_adj);
	reg |= USB2_OTG_PAD_CTL1_HS_IREF_CAP(sc->hs_iref_cap);
	WR4(sc, XUSB_PADCTL_USB2_OTG_PAD_CTL1(lane->idx), reg);

	if (port != NULL && port->supply_vbus != NULL) {
		rv = regulator_enable(port->supply_vbus);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot enable vbus regulator\n");
			return (rv);
		}
	}
	reg = RD4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	reg &= ~USB2_BIAS_PAD_CTL0_PD;
	WR4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0, reg);

	return (0);
}

static int
usb2_powerdown(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;
	struct padctl_port *port;
	int rv;

	port = search_lane_port(sc, lane);
	if (port == NULL) {
		device_printf(sc->dev, "Cannot find port for lane: %s\n",
		    lane->name);
	}
	reg = RD4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	reg |= USB2_BIAS_PAD_CTL0_PD;
	WR4(sc, XUSB_PADCTL_USB2_BIAS_PAD_CTL0, reg);

	if (port != NULL && port->supply_vbus != NULL) {
		rv = regulator_enable(port->supply_vbus);
		if (rv != 0) {
			device_printf(sc->dev,
			    "Cannot disable vbus regulator\n");
			return (rv);
		}
	}
	return (0);
}


static int
phy_powerup(struct padctl_softc *sc)
{
	uint32_t reg;

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg &= ~ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	return (0);
}

static int
phy_powerdown(struct padctl_softc *sc)
{
	uint32_t reg;

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg |= ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg |= ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	reg = RD4(sc, XUSB_PADCTL_ELPG_PROGRAM);
	reg |= ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN;
	WR4(sc, XUSB_PADCTL_ELPG_PROGRAM, reg);
	DELAY(100);

	return (0);
}

static int
xusbpadctl_phy_enable(struct phynode *phy, bool enable)
{
	device_t dev;
	intptr_t id;
	struct padctl_softc *sc;
	struct padctl_lane *lane;
	struct padctl_pad *pad;
	int rv;

	dev = phynode_get_device(phy);
	id = phynode_get_id(phy);
	sc = device_get_softc(dev);

	if (id < 0 || id >= nitems(lanes_tbl)) {
		device_printf(dev, "Unknown phy: %d\n", id);
		return (ENXIO);
	}
	lane = lanes_tbl + id;
	if (!lane->enabled) {
		device_printf(dev, "Lane is not enabled/configured: %s\n",
		    lane->name);
		return (ENXIO);
	}
	pad = lane->pad;
	if (enable) {
		if (sc->phy_ena_cnt == 0) {
			rv = phy_powerup(sc);
			if (rv != 0)
				return (rv);
		}
		sc->phy_ena_cnt++;
	}

	if (enable)
		rv = pad->powerup(sc, lane);
	else
		rv = pad->powerdown(sc, lane);
	if (rv != 0)
		return (rv);

	if (!enable) {
		 if (sc->phy_ena_cnt == 1) {
			rv = phy_powerdown(sc);
			if (rv != 0)
				return (rv);
		}
		sc->phy_ena_cnt--;
	}

	return (0);
}

/* -------------------------------------------------------------------------
 *
 *   FDT processing
 */
static struct padctl_port *
search_port(struct padctl_softc *sc, char *port_name)
{
	int i;

	for (i = 0; i < nitems(ports_tbl); i++) {
		if (strcmp(port_name, ports_tbl[i].name) == 0)
			return (&ports_tbl[i]);
	}
	return (NULL);
}

static struct padctl_port *
search_lane_port(struct padctl_softc *sc, struct padctl_lane *lane)
{
	int i;

	for (i = 0; i < nitems(ports_tbl); i++) {
		if (!ports_tbl[i].enabled)
			continue;
		if (ports_tbl[i].lane == lane)
			return (ports_tbl + i);
	}
	return (NULL);
}

static struct padctl_lane *
search_lane(struct padctl_softc *sc, char *lane_name)
{
	int i;

	for (i = 0; i < nitems(lanes_tbl); i++) {
		if (strcmp(lane_name, lanes_tbl[i].name) == 0)
			return 	(lanes_tbl + i);
	}
	return (NULL);
}

static struct padctl_lane *
search_pad_lane(struct padctl_softc *sc, enum padctl_pad_type type, int idx)
{
	int i;

	for (i = 0; i < nitems(lanes_tbl); i++) {
		if (!lanes_tbl[i].enabled)
			continue;
		if (type == lanes_tbl[i].pad->type && idx == lanes_tbl[i].idx)
			return 	(lanes_tbl + i);
	}
	return (NULL);
}

static struct padctl_lane *
search_usb3_pad_lane(struct padctl_softc *sc, int idx)
{
	int i;
	struct padctl_lane *lane, *tmp;

	lane = NULL;
	for (i = 0; i < nitems(lane_map_tbl); i++) {
		if (idx != lane_map_tbl[i].port_idx)
			continue;
		tmp = search_pad_lane(sc, lane_map_tbl[i].pad_type,
		    lane_map_tbl[i].lane_idx);
		if (tmp == NULL)
			continue;
		if (strcmp(tmp->mux[tmp->mux_idx], "usb3-ss") != 0)
			continue;
		if (lane != NULL) {
			device_printf(sc->dev, "Duplicated mappings found for"
			 " lanes: %s and %s\n", lane->name, tmp->name);
			return (NULL);
		}
		lane = tmp;
	}
	return (lane);
}

static struct padctl_pad *
search_pad(struct padctl_softc *sc, char *pad_name)
{
	int i;

	for (i = 0; i < nitems(pads_tbl); i++) {
		if (strcmp(pad_name, pads_tbl[i].name) == 0)
			return 	(pads_tbl + i);
	}
	return (NULL);
}

static int
search_mux(struct padctl_softc *sc, struct padctl_lane *lane, char *fnc_name)
{
	int i;

	for (i = 0; i < lane->nmux; i++) {
		if (strcmp(fnc_name, lane->mux[i]) == 0)
			return 	(i);
	}
	return (-1);
}

static int
config_lane(struct padctl_softc *sc, struct padctl_lane *lane)
{
	uint32_t reg;

	reg = RD4(sc, lane->reg);
	reg &= ~(lane->mask << lane->shift);
	reg |=  (lane->mux_idx & lane->mask) << lane->shift;
	WR4(sc, lane->reg, reg);
	return (0);
}

static int
process_lane(struct padctl_softc *sc, phandle_t node, struct padctl_pad *pad)
{
	struct padctl_lane *lane;
	struct phynode *phynode;
	struct phynode_init_def phy_init;
	char *name;
	char *function;
	int rv;

	name = NULL;
	function = NULL;
	rv = OF_getprop_alloc(node, "name", (void **)&name);
	if (rv <= 0) {
		device_printf(sc->dev, "Cannot read lane name.\n");
		return (ENXIO);
	}

	lane = search_lane(sc, name);
	if (lane == NULL) {
		device_printf(sc->dev, "Unknown lane: %s\n", name);
		rv = ENXIO;
		goto end;
	}

	/* Read function (mux) settings. */
	rv = OF_getprop_alloc(node, "nvidia,function", (void **)&function);
	if (rv <= 0) {
		device_printf(sc->dev, "Cannot read lane function.\n");
		rv = ENXIO;
		goto end;
	}

	lane->mux_idx = search_mux(sc, lane, function);
	if (lane->mux_idx == ~0) {
		device_printf(sc->dev, "Unknown function %s for lane %s\n",
		    function, name);
		rv = ENXIO;
		goto end;
	}

	rv = config_lane(sc, lane);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot configure lane: %s: %d\n",
		    name, rv);
		rv = ENXIO;
		goto end;
	}
	lane->pad = pad;
	lane->enabled = true;
	pad->lanes[pad->nlanes++] = lane;

	/* Create and register phy. */
	bzero(&phy_init, sizeof(phy_init));
	phy_init.id = lane - lanes_tbl;
	phy_init.ofw_node = node;
	phynode = phynode_create(sc->dev, &xusbpadctl_phynode_class, &phy_init);
	if (phynode == NULL) {
		device_printf(sc->dev, "Cannot create phy\n");
		rv = ENXIO;
		goto end;
	}
	if (phynode_register(phynode) == NULL) {
		device_printf(sc->dev, "Cannot create phy\n");
		return (ENXIO);
	}

	rv = 0;

end:
	if (name != NULL)
		OF_prop_free(name);
	if (function != NULL)
		OF_prop_free(function);
	return (rv);
}

static int
process_pad(struct padctl_softc *sc, phandle_t node)
{
	struct padctl_pad *pad;
	char *name;
	int rv;

	name = NULL;
	rv = OF_getprop_alloc(node, "name", (void **)&name);
	if (rv <= 0) {
		device_printf(sc->dev, "Cannot read pad name.\n");
		return (ENXIO);
	}
	pad = search_pad(sc, name);
	if (pad == NULL) {
		device_printf(sc->dev, "Unknown pad: %s\n", name);
		rv = ENXIO;
		goto end;
	}

	/* Read and process associated lanes. */
	node = ofw_bus_find_child(node, "lanes");
	if (node <= 0) {
		device_printf(sc->dev, "Cannot find regulators subnode\n");
		rv = ENXIO;
		goto end;
	}

	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;

		rv = process_lane(sc, node, pad);
		if (rv != 0)
			goto end;
	}
	pad->enabled = true;
	rv = 0;
end:
	if (name != NULL)
		OF_prop_free(name);
	return (rv);
}

static int
process_port(struct padctl_softc *sc, phandle_t node)
{

	struct padctl_port *port;
	char *name;
	int rv;

	name = NULL;
	rv = OF_getprop_alloc(node, "name", (void **)&name);
	if (rv <= 0) {
		device_printf(sc->dev, "Cannot read port name.\n");
		return (ENXIO);
	}

	port = search_port(sc, name);
	if (port == NULL) {
		device_printf(sc->dev, "Unknown port: %s\n", name);
		rv = ENXIO;
		goto end;
	}

	if (port->type == PADCTL_PORT_USB3) {
		rv = OF_getencprop(node,  "nvidia,usb2-companion",
		   &(port->companion), sizeof(port->companion));
		if (rv <= 0) {
			device_printf(sc->dev,
			    "Missing 'nvidia,usb2-companion' property "
			    "for port: %s\n", name);
			rv = ENXIO;
			goto end;
		}
	}

	if (OF_hasprop(node, "vbus-supply")) {
		rv = regulator_get_by_ofw_property(sc->dev, 0,
		    "vbus-supply", &port->supply_vbus);
		if (rv <= 0) {
			device_printf(sc->dev,
			    "Cannot get 'vbus-supply' regulator "
			    "for port: %s\n", name);
			rv = ENXIO;
			goto end;
		}
	}

	if (OF_hasprop(node, "nvidia,internal"))
		port->internal = true;
	/* Find assigned lane */
	if (port->lane == NULL) {
		switch(port->type) {
		/* Routing is fixed for USB2, ULPI AND HSIC. */
		case PADCTL_PORT_USB2:
			port->lane = search_pad_lane(sc, PADCTL_PAD_USB2,
			    port->idx);
			break;
		case PADCTL_PORT_ULPI:
			port->lane = search_pad_lane(sc, PADCTL_PAD_ULPI,
			    port->idx);
			break;
		case PADCTL_PORT_HSIC:
			port->lane = search_pad_lane(sc, PADCTL_PAD_HSIC,
			    port->idx);
			break;
		case PADCTL_PORT_USB3:
			port->lane = search_usb3_pad_lane(sc, port->idx);
			break;
		}
	}
	if (port->lane == NULL) {
		device_printf(sc->dev, "Cannot find lane for port: %s\n", name);
		rv = ENXIO;
		goto end;
	}
	port->enabled = true;
	rv = 0;
end:
	if (name != NULL)
		OF_prop_free(name);
	return (rv);
}

static int
parse_fdt(struct padctl_softc *sc, phandle_t base_node)
{
	phandle_t node;
	int rv;

	rv = 0;
	node = ofw_bus_find_child(base_node, "pads");

	if (node <= 0) {
		device_printf(sc->dev, "Cannot find pads subnode.\n");
		return (ENXIO);
	}
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;
		rv = process_pad(sc, node);
		if (rv != 0)
			return (rv);
	}

	node = ofw_bus_find_child(base_node, "ports");
	if (node <= 0) {
		device_printf(sc->dev, "Cannot find ports subnode.\n");
		return (ENXIO);
	}
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;
		rv = process_port(sc, node);
		if (rv != 0)
			return (rv);
	}

	return (0);
}

static void
load_calibration(struct padctl_softc *sc)
{
	uint32_t reg;

	/* All XUSB pad calibrations are packed into single dword.*/
	reg = tegra_fuse_read_4(FUSE_XUSB_CALIB);
	sc->hs_curr_level_0 = FUSE_XUSB_CALIB_HS_CURR_LEVEL_0(reg);
	sc->hs_curr_level_123 = FUSE_XUSB_CALIB_HS_CURR_LEVEL_123(reg);
	sc->hs_iref_cap = FUSE_XUSB_CALIB_HS_IREF_CAP(reg);
	sc->hs_squelch_level = FUSE_XUSB_CALIB_HS_SQUELCH_LEVEL(reg);
	sc->hs_term_range_adj = FUSE_XUSB_CALIB_HS_TERM_RANGE_ADJ(reg);
}

/* -------------------------------------------------------------------------
 *
 *   BUS functions
 */
static int
xusbpadctl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Tegra XUSB phy");
	return (BUS_PROBE_DEFAULT);
}

static int
xusbpadctl_detach(device_t dev)
{

	/* This device is always present. */
	return (EBUSY);
}

static int
xusbpadctl_attach(device_t dev)
{
	struct padctl_softc * sc;
	int i, rid, rv;
	struct padctl_port *port;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	rv = hwreset_get_by_ofw_name(dev, 0, "padctl", &sc->rst);
	if (rv != 0) {
		device_printf(dev, "Cannot get 'padctl' reset: %d\n", rv);
		return (rv);
	}
	rv = hwreset_deassert(sc->rst);
	if (rv != 0) {
		device_printf(dev, "Cannot unreset 'padctl' reset: %d\n", rv);
		return (rv);
	}

	load_calibration(sc);

	rv = parse_fdt(sc, node);
	if (rv != 0) {
		device_printf(dev, "Cannot parse fdt configuration: %d\n", rv);
		return (rv);
	}
	for (i = 0; i < nitems(ports_tbl); i++) {
		port = ports_tbl + i;
		if (!port->enabled)
			continue;
		if (port->init == NULL)
			continue;
		rv = port->init(sc, port);
		if (rv != 0) {
			device_printf(dev, "Cannot init port '%s'\n",
			    port->name);
			return (rv);
		}
	}
	return (0);
}

static device_method_t tegra_xusbpadctl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         xusbpadctl_probe),
	DEVMETHOD(device_attach,        xusbpadctl_attach),
	DEVMETHOD(device_detach,        xusbpadctl_detach),

	DEVMETHOD_END
};

static devclass_t tegra_xusbpadctl_devclass;
static DEFINE_CLASS_0(xusbpadctl, tegra_xusbpadctl_driver,
    tegra_xusbpadctl_methods, sizeof(struct padctl_softc));
EARLY_DRIVER_MODULE(tegra_xusbpadctl, simplebus, tegra_xusbpadctl_driver,
    tegra_xusbpadctl_devclass, NULL, NULL, 73);
