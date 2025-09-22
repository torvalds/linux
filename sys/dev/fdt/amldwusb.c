/*	$OpenBSD: amldwusb.c,v 1.5 2023/09/22 01:10:44 jsg Exp $	*/
/*
 * Copyright (c) 2019 Mark kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

/* Glue registers. */

#define U2P_R0(i)			(0x00 + (i) * 0x20)
#define  U2P_R0_HOST_DEVICE			(1 << 0)
#define  U2P_R0_POWER_OK			(1 << 1)
#define  U2P_R0_HAST_MODE			(1 << 2)
#define  U2P_R0_POWER_ON_RESET			(1 << 3)
#define  U2P_R0_ID_PULLUP			(1 << 4)
#define  U2P_R0_DRV_VBUS			(1 << 5)
#define U2P_R1(i)			(0x04 + (i) * 0x20)
#define  U2P_R1_PHY_READY			(1 << 0)
#define  U2P_R1_ID_DIG				(1 << 1)
#define  U2P_R1_OTG_SESSION_VALID		(1 << 2)
#define  U2P_R1_VBUS_VALID			(1 << 3)

#define USB_R0				0x80
#define  USB_R0_P30_LANE0_TX2RX_LOOPBACK	(1 << 17)
#define  USB_R0_P30_LANE0_EXT_PCLK_REQ		(1 << 18)
#define  USB_R0_P30_PCS_RX_LOS_MASK_VAL_MASK	(0x3ff << 19)
#define  USB_R0_P30_PCS_RX_LOS_MASK_VAL_SHIFT	19
#define  USB_R0_U2D_SS_SCALEDOWN_MODE_MASK	(0x3 << 29)
#define  USB_R0_U2D_SS_SCALEDOWN_MODE_SHIFT	29
#define  USB_R0_U2D_ACT				(1U << 31)
#define USB_R1				0x84
#define  USB_R1_U3H_BIGENDIAN_GS		(1 << 0)
#define  USB_R1_U3H_PME_ENABLE			(1 << 1)
#define  USB_R1_U3H_HUB_PORT_OVERCURRENT_MASK	(0x7 << 2)
#define  USB_R1_U3H_HUB_PORT_OVERCURRENT_SHIFT	2
#define  USB_R1_U3H_HUB_PORT_PERM_ATTACH_MASK	(0x7 << 7)
#define  USB_R1_U3H_HUB_PORT_PERM_ATTACH_SHIFT	7
#define  USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK	(0x3 << 12)
#define  USB_R1_U3H_HOST_U2_PORT_DISABLE_SHIFT	12
#define  USB_R1_U3H_HOST_U3_PORT_DISABLE	(1 << 16)
#define  USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT (1 << 17)
#define  USB_R1_U3H_HOST_MSI_ENABLE		(1 << 18)
#define  USB_R1_U3H_FLADJ_30MHZ_REG_MASK	(0x3f << 19)
#define  USB_R1_U3H_FLADJ_30MHZ_REG_SHIFT	19
#define  USB_R1_P30_PCS_TX_SWING_FULL_MASK	(0x7f << 25)
#define  USB_R1_P30_PCS_TX_SWING_FULL_SHIFT	25
#define USB_R2				0x88
#define  USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK	(0x3f << 20)
#define  USB_R2_P30_PCS_TX_DEEMPH_3P5DB_SHIFT	20
#define  USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK	(0x3f << 26)
#define  USB_R2_P30_PCS_TX_DEEMPH_6DB_SHIFT	26
#define USB_R3				0x8c
#define  USB_R3_P30_SSC_ENABLE			(1 << 0)
#define  USB_R3_P30_SSC_RANGE_MASK		(0x7 << 1)
#define  USB_R3_P30_SSC_RANGE_SHIFT		1
#define  USB_R3_P30_SSC_REF_CLK_SEL_MASK	(0x1ff << 4)
#define  USB_R3_P30_SSC_REF_CLK_SEL_SHIFT	4
#define  USB_R3_P30_REF_SSP_EN			(1 << 13)
#define USB_R4				0x90
#define  USB_R4_P21_PORT_RESET_0		(1 << 0)
#define  USB_R4_P21_SLEEP_M0			(1 << 1)
#define  USB_R4_MEM_PD_MASK			(0x3 << 2)
#define  USB_R4_MEM_PD_SHIFT			2
#define  USB_R4_P21_ONLY			(1 << 4)
#define USB_R5				0x94
#define  USB_R5_ID_DIG_SYNC			(1 << 0)
#define  USB_R5_ID_DIG_REG			(1 << 1)
#define  USB_R5_ID_DIG_CFG_MASK			(0x3 << 2)
#define  USB_R5_ID_DIG_CFG_SHIFT		2
#define  USB_R5_ID_DIG_EN_0			(1 << 4)
#define  USB_R5_ID_DIG_EN_1			(1 << 5)
#define  USB_R5_ID_DIG_CURR			(1 << 6)
#define  USB_R5_ID_DIG_IRQ			(1 << 7)
#define  USB_R5_ID_DIG_TH_MASK			(0xff << 8)
#define  USB_R5_ID_DIG_TH_SHIFT			8
#define  USB_R5_ID_DIG_CNT_MASK			(0xff << 16)
#define  USB_R5_ID_DIG_CNT_SHIFT		16

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amldwusb_softc {
	struct simplebus_softc	sc_sbus;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	amldwusb_match(struct device *, void *, void *);
void	amldwusb_attach(struct device *, struct device *, void *);

const struct cfattach amldwusb_ca = {
	sizeof(struct amldwusb_softc), amldwusb_match, amldwusb_attach
};

struct cfdriver amldwusb_cd = {
	NULL, "amldwusb", DV_DULL
};

void	amldwusb_init_usb2(struct amldwusb_softc *);
void	amldwusb_init_usb3(struct amldwusb_softc *);

int
amldwusb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,meson-g12a-usb-ctrl");
}

void
amldwusb_attach(struct device *parent, struct device *self, void *aux)
{
	struct amldwusb_softc *sc = (struct amldwusb_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t vbus_supply;
	uint32_t reg;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	power_domain_enable(faa->fa_node);
	clock_enable_all(faa->fa_node);

	reset_assert_all(faa->fa_node);
	delay(10);
	reset_deassert_all(faa->fa_node);

	vbus_supply = OF_getpropint(faa->fa_node, "vbus-supply", 0);
	if (vbus_supply)
		regulator_enable(vbus_supply);

	amldwusb_init_usb2(sc);

	reg = HREAD4(sc, USB_R1);
	reg &= ~USB_R1_U3H_FLADJ_30MHZ_REG_MASK;
	reg |= (0x20 << USB_R1_U3H_FLADJ_30MHZ_REG_SHIFT);
	HWRITE4(sc, USB_R1, reg);

	HSET4(sc, USB_R5, USB_R5_ID_DIG_EN_0);
	HSET4(sc, USB_R5, USB_R5_ID_DIG_EN_1);
	reg = HREAD4(sc, USB_R5);
	reg &= ~USB_R5_ID_DIG_TH_MASK;
	reg |= (0xff << USB_R5_ID_DIG_TH_SHIFT);
	HWRITE4(sc, USB_R5, reg);

	/* Initialize PHYs. */
	phy_enable(faa->fa_node, "usb2-phy0");
	phy_enable(faa->fa_node, "usb2-phy1");

	/* Only enable USB 3.0 logic and PHY if we have one. */
	if (OF_getindex(faa->fa_node, "usb3-phy0", "phy-names") >= 0) {
		amldwusb_init_usb3(sc);
		phy_enable(faa->fa_node, "usb3-phy0");
	}

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

void
amldwusb_init_usb2(struct amldwusb_softc *sc)
{
	int i;

	for (i = 0; i < 3; i++) {
		HSET4(sc, U2P_R0(i), U2P_R0_POWER_ON_RESET);

		/* We don't support device mode, so always force host mode. */
		HSET4(sc, U2P_R0(i), U2P_R0_HOST_DEVICE);

		HCLR4(sc, U2P_R0(i), U2P_R0_POWER_ON_RESET);
	}
}

void
amldwusb_init_usb3(struct amldwusb_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, USB_R3);
	reg &= ~USB_R3_P30_SSC_RANGE_MASK;
	reg |= USB_R3_P30_SSC_ENABLE;
	reg |= (2 << USB_R3_P30_SSC_RANGE_SHIFT);
	reg |= USB_R3_P30_REF_SSP_EN;
	HWRITE4(sc, USB_R3, reg);

	delay(2);

	reg = HREAD4(sc, USB_R2);
	reg &= ~USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK;
	reg |= (0x15 << USB_R2_P30_PCS_TX_DEEMPH_3P5DB_SHIFT);
	HWRITE4(sc, USB_R2, reg);
	reg = HREAD4(sc, USB_R2);
	reg &= ~USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK;
	reg |= (0x15 << USB_R2_P30_PCS_TX_DEEMPH_6DB_SHIFT);
	HWRITE4(sc, USB_R2, reg);

	delay(2);

	HSET4(sc, USB_R1, USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT);
	reg = HREAD4(sc, USB_R1);
	reg &= ~USB_R1_P30_PCS_TX_SWING_FULL_MASK;
	reg |= (0x7f << USB_R1_P30_PCS_TX_SWING_FULL_SHIFT);
	HWRITE4(sc, USB_R1, reg);
}
