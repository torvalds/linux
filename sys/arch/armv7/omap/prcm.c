/* $OpenBSD: prcm.c,v 1.19 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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

/*-
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the Power, Reset and Clock Management Module (PRCM).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/omap/prcmvar.h>

#include <armv7/omap/am335x_prcmreg.h>
#include <armv7/omap/omap3_prcmreg.h>
#include <armv7/omap/omap4_prcmreg.h>

#include <dev/ofw/fdt.h>

#define PRCM_REVISION		0x0800
#define PRCM_SYSCONFIG		0x0810

uint32_t prcm_imask_mask[PRCM_REG_MAX];
uint32_t prcm_fmask_mask[PRCM_REG_MAX];
uint32_t prcm_imask_addr[PRCM_REG_MAX];
uint32_t prcm_fmask_addr[PRCM_REG_MAX];

#define SYS_CLK			13    /* SYS_CLK speed in MHz */
#define PRCM_AM335X_MASTER_OSC	24000 /* kHz */


struct prcm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_prcm;
	bus_space_handle_t	sc_cm1;
	bus_space_handle_t	sc_cm2;
	void (*sc_setup)(struct prcm_softc *sc);
	void (*sc_enablemodule)(struct prcm_softc *sc, int mod);
	void (*sc_setclock)(struct prcm_softc *sc,
	    int clock, int speed);
	uint32_t		cm1_avail;
	uint32_t		cm2_avail;
};

void	prcm_attach(struct device *, struct device *, void *);
int	prcm_setup_dpll5(struct prcm_softc *);
uint32_t prcm_v3_bit(int mod);
uint32_t prcm_am335x_clkctrl(int mod);

void prcm_am335x_enablemodule(struct prcm_softc *, int);
void prcm_am335x_setclock(struct prcm_softc *, int, int);

void prcm_v3_setup(struct prcm_softc *);
void prcm_v3_enablemodule(struct prcm_softc *, int);
void prcm_v3_setclock(struct prcm_softc *, int, int);

void prcm_v4_enablemodule(struct prcm_softc *, int);
int prcm_v4_hsusbhost_activate(int);
int prcm_v4_hsusbhost_set_source(int, int);

const struct cfattach	prcm_ca = {
	sizeof (struct prcm_softc), NULL, prcm_attach
};

struct cfdriver prcm_cd = {
	NULL, "prcm", DV_DULL
};

void
prcm_attach(struct device *parent, struct device *self, void *args)
{
	struct armv7_attach_args *aa = args;
	struct prcm_softc *sc = (struct prcm_softc *) self;
	u_int32_t reg;
	void *node;

	sc->sc_iot = aa->aa_iot;

	node = fdt_find_node("/");
	if (node == NULL)
		panic("%s: could not get fdt root node",
		    sc->sc_dev.dv_xname);

	if (fdt_is_compatible(node, "ti,am33xx")) {
		sc->sc_setup = NULL;
		sc->sc_enablemodule = prcm_am335x_enablemodule;
		sc->sc_setclock = prcm_am335x_setclock;
	} else if (fdt_is_compatible(node, "ti,omap3")) {
		sc->sc_setup = prcm_v3_setup;
		sc->sc_enablemodule = prcm_v3_enablemodule;
		sc->sc_setclock = prcm_v3_setclock;
	} else if (fdt_is_compatible(node, "ti,omap4")) {
		sc->sc_setup = NULL;
		sc->sc_enablemodule = prcm_v4_enablemodule;
		sc->sc_setclock = NULL;
		sc->cm1_avail = 1;
		sc->cm2_avail = 1;
	} else
		panic("%s: could not find a compatible soc",
		    sc->sc_dev.dv_xname);

	if (bus_space_map(sc->sc_iot, aa->aa_dev->mem[0].addr,
	    aa->aa_dev->mem[0].size, 0, &sc->sc_prcm))
		panic("prcm_attach: bus_space_map failed!");

	if (sc->cm1_avail &&
	    bus_space_map(sc->sc_iot, aa->aa_dev->mem[1].addr,
	    aa->aa_dev->mem[1].size, 0, &sc->sc_cm1))
		panic("prcm_attach: bus_space_map failed!");

	if (sc->cm2_avail &&
	    bus_space_map(sc->sc_iot, aa->aa_dev->mem[2].addr,
	    aa->aa_dev->mem[2].size, 0, &sc->sc_cm2))
		panic("prcm_attach: bus_space_map failed!");

	reg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_REVISION);
	printf(" rev %d.%d\n", reg >> 4 & 0xf, reg & 0xf);

	if (sc->sc_setup != NULL)
		sc->sc_setup(sc);
}

void
prcm_v3_setup(struct prcm_softc *sc)
{
	/* Setup the 120MHZ DPLL5 clock, to be used by USB. */
	prcm_setup_dpll5(sc);

	prcm_fmask_mask[PRCM_REG_CORE_CLK1] = PRCM_REG_CORE_CLK1_FMASK;
	prcm_imask_mask[PRCM_REG_CORE_CLK1] = PRCM_REG_CORE_CLK1_IMASK;
	prcm_fmask_addr[PRCM_REG_CORE_CLK1] = PRCM_REG_CORE_CLK1_FADDR;
	prcm_imask_addr[PRCM_REG_CORE_CLK1] = PRCM_REG_CORE_CLK1_IADDR;

	prcm_fmask_mask[PRCM_REG_CORE_CLK2] = PRCM_REG_CORE_CLK2_FMASK;
	prcm_imask_mask[PRCM_REG_CORE_CLK2] = PRCM_REG_CORE_CLK2_IMASK;
	prcm_fmask_addr[PRCM_REG_CORE_CLK2] = PRCM_REG_CORE_CLK2_FADDR;
	prcm_imask_addr[PRCM_REG_CORE_CLK2] = PRCM_REG_CORE_CLK2_IADDR;

	prcm_fmask_mask[PRCM_REG_CORE_CLK3] = PRCM_REG_CORE_CLK3_FMASK;
	prcm_imask_mask[PRCM_REG_CORE_CLK3] = PRCM_REG_CORE_CLK3_IMASK;
	prcm_fmask_addr[PRCM_REG_CORE_CLK3] = PRCM_REG_CORE_CLK3_FADDR;
	prcm_imask_addr[PRCM_REG_CORE_CLK3] = PRCM_REG_CORE_CLK3_IADDR;

	prcm_fmask_mask[PRCM_REG_WKUP] = PRCM_REG_WKUP_FMASK;
	prcm_imask_mask[PRCM_REG_WKUP] = PRCM_REG_WKUP_IMASK;
	prcm_fmask_addr[PRCM_REG_WKUP] = PRCM_REG_WKUP_FADDR;
	prcm_imask_addr[PRCM_REG_WKUP] = PRCM_REG_WKUP_IADDR;

	prcm_fmask_mask[PRCM_REG_PER] = PRCM_REG_PER_FMASK;
	prcm_imask_mask[PRCM_REG_PER] = PRCM_REG_PER_IMASK;
	prcm_fmask_addr[PRCM_REG_PER] = PRCM_REG_PER_FADDR;
	prcm_imask_addr[PRCM_REG_PER] = PRCM_REG_PER_IADDR;

	prcm_fmask_mask[PRCM_REG_USBHOST] = PRCM_REG_USBHOST_FMASK;
	prcm_imask_mask[PRCM_REG_USBHOST] = PRCM_REG_USBHOST_IMASK;
	prcm_fmask_addr[PRCM_REG_USBHOST] = PRCM_REG_USBHOST_FADDR;
	prcm_imask_addr[PRCM_REG_USBHOST] = PRCM_REG_USBHOST_IADDR;
}

void
prcm_setclock(int clock, int speed)
{
	struct prcm_softc *sc = prcm_cd.cd_devs[0];

	if (!sc->sc_setclock)
		panic("%s: not initialised!", __func__);

	sc->sc_setclock(sc, clock, speed);
}

void
prcm_am335x_setclock(struct prcm_softc *sc, int clock, int speed)
{
	u_int32_t oreg, reg, mask;

	/* set CLKSEL register */
	if (clock == 1) {
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm,
		    PRCM_AM335X_CLKSEL_TIMER2_CLK);
		mask = 3;
		reg = oreg & ~mask;
		reg |=0x02;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm,
		    PRCM_AM335X_CLKSEL_TIMER2_CLK, reg);
	} else if (clock == 2) {
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm,
		    PRCM_AM335X_CLKSEL_TIMER3_CLK);
		mask = 3;
		reg = oreg & ~mask;
		reg |=0x02;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm,
		    PRCM_AM335X_CLKSEL_TIMER3_CLK, reg);
	} else if (clock == 3) { /* DISP M1 */
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE);
		oreg &= ~0x7;
		oreg |= 0x4;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE, oreg);
		while(!(bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_IDLEST
		    & 0x10)));

		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKSEL);
		oreg &= 0xFFF800FF;
		oreg |= (speed & 0x7FF) << 8;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKSEL, oreg);

		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE);
		oreg |= 0x7;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE, oreg);
		while(!(bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_IDLEST
		    & 0x10)));
	} else if (clock == 4) { /* DISP N */
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE);
		oreg &= ~0x7;
		oreg |= 0x4;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE, oreg);
		while(!(bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_IDLEST
		    & 0x10)));

		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKSEL);
		oreg &= 0xFFFFFF80;
		oreg |= speed & 0x7F;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKSEL, oreg);

		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE);
		oreg |= 0x7;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_CLKMODE, oreg);
		while(!(bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_IDLEST
		    & 0x10)));
	} else if (clock == 5) { /* DISP M2 */
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_M2);
		oreg &= ~(0x1F);
		oreg |= speed & 0x1F;
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, PRCM_AM335X_DISP_M2, oreg);
	}
}

void
prcm_v3_setclock(struct prcm_softc *sc, int clock, int speed)
{
	u_int32_t oreg, reg, mask;

	if (clock == 1) {
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL_WKUP);
		mask = 1;
		reg = (oreg &~mask) | (speed & mask);
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL_WKUP, reg);
	} else if (clock >= 2 && clock <= 9) {
		int shift =  (clock-2);
		oreg = bus_space_read_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL_PER);
		mask = 1 << (shift);
		reg =  (oreg & ~mask) | ( (speed << shift) & mask);
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL_PER, reg);
	} else
		panic("%s: invalid clock %d", __func__, clock);
}

uint32_t
prcm_v3_bit(int mod)
{
	switch(mod) {
	case PRCM_MMC0:
		return PRCM_CLK_EN_MMC1;
	case PRCM_MMC1:
		return PRCM_CLK_EN_MMC2;
	case PRCM_MMC2:
		return PRCM_CLK_EN_MMC3;
	case PRCM_USB:
		return PRCM_CLK_EN_USB;
	case PRCM_GPIO0:
		return PRCM_CLK_EN_GPIO1;
	case PRCM_GPIO1:
		return PRCM_CLK_EN_GPIO2;
	case PRCM_GPIO2:
		return PRCM_CLK_EN_GPIO3;
	case PRCM_GPIO3:
		return PRCM_CLK_EN_GPIO4;
	case PRCM_GPIO4:
		return PRCM_CLK_EN_GPIO5;
	case PRCM_GPIO5:
		return PRCM_CLK_EN_GPIO6;
	case PRCM_I2C0:
		return PRCM_CLK_EN_I2C1;
	case PRCM_I2C1:
		return PRCM_CLK_EN_I2C2;
	case PRCM_I2C2:
		return PRCM_CLK_EN_I2C3;
	default:
		panic("%s: module not found", __func__);
	}
}

uint32_t
prcm_am335x_clkctrl(int mod)
{
	switch(mod) {
	case PRCM_TIMER2:
		return PRCM_AM335X_TIMER2_CLKCTRL;
	case PRCM_TIMER3:
		return PRCM_AM335X_TIMER3_CLKCTRL;
	case PRCM_MMC0:
		return PRCM_AM335X_MMC0_CLKCTRL;
	case PRCM_MMC1:
		return PRCM_AM335X_MMC1_CLKCTRL;
	case PRCM_MMC2:
		return PRCM_AM335X_MMC2_CLKCTRL;
	case PRCM_USB:
		return PRCM_AM335X_USB0_CLKCTRL;
	case PRCM_GPIO0:
		return PRCM_AM335X_GPIO0_CLKCTRL;
	case PRCM_GPIO1:
		return PRCM_AM335X_GPIO1_CLKCTRL;
	case PRCM_GPIO2:
		return PRCM_AM335X_GPIO2_CLKCTRL;
	case PRCM_GPIO3:
		return PRCM_AM335X_GPIO3_CLKCTRL;
	case PRCM_TPCC:
		return PRCM_AM335X_TPCC_CLKCTRL;
	case PRCM_TPTC0:
		return PRCM_AM335X_TPTC0_CLKCTRL;
	case PRCM_TPTC1:
		return PRCM_AM335X_TPTC1_CLKCTRL;
	case PRCM_TPTC2:
		return PRCM_AM335X_TPTC2_CLKCTRL;
	case PRCM_I2C0:
		return PRCM_AM335X_I2C0_CLKCTRL;
	case PRCM_I2C1:
		return PRCM_AM335X_I2C1_CLKCTRL;
	case PRCM_I2C2:
		return PRCM_AM335X_I2C2_CLKCTRL;
	case PRCM_LCDC:
		return PRCM_AM335X_LCDC_CLKCTRL;
	case PRCM_RNG:
		return PRCM_AM335X_RNG_CLKCTRL;
	default:
		panic("%s: module not found", __func__);
	}
}

void
prcm_enablemodule(int mod)
{
	struct prcm_softc *sc = prcm_cd.cd_devs[0];

	if (!sc->sc_enablemodule)
		panic("%s: not initialised!", __func__);

	sc->sc_enablemodule(sc, mod);
}

void
prcm_am335x_enablemodule(struct prcm_softc *sc, int mod)
{
	uint32_t clkctrl;
	int reg;

	/*set enable bits in CLKCTRL register */
	reg = prcm_am335x_clkctrl(mod);
	clkctrl = bus_space_read_4(sc->sc_iot, sc->sc_prcm, reg);
	clkctrl &=~AM335X_CLKCTRL_MODULEMODE_MASK;
	clkctrl |= AM335X_CLKCTRL_MODULEMODE_ENABLE;
	bus_space_write_4(sc->sc_iot, sc->sc_prcm, reg, clkctrl);

	/* wait until module is enabled */
	while (bus_space_read_4(sc->sc_iot, sc->sc_prcm, reg) & 0x30000)
		;
}

void
prcm_v3_enablemodule(struct prcm_softc *sc, int mod)
{
	uint32_t bit;
	uint32_t fclk, iclk, fmask, imask, mbit;
	int freg, ireg, reg;

	bit = prcm_v3_bit(mod);
	reg = bit >> 5;

	freg = prcm_fmask_addr[reg];
	ireg = prcm_imask_addr[reg];
	fmask = prcm_fmask_mask[reg];
	imask = prcm_imask_mask[reg];

	mbit = 1 << (bit & 0x1f);
	if (fmask & mbit) { /* dont access the register if bit isn't present */
		fclk = bus_space_read_4(sc->sc_iot, sc->sc_prcm, freg);
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, freg, fclk | mbit);
	}
	if (imask & mbit) { /* dont access the register if bit isn't present */
		iclk = bus_space_read_4(sc->sc_iot, sc->sc_prcm, ireg);
		bus_space_write_4(sc->sc_iot, sc->sc_prcm, ireg, iclk | mbit);
	}
	printf("\n");
}

void
prcm_v4_enablemodule(struct prcm_softc *sc, int mod)
{
	switch (mod) {
		case PRCM_MMC0:
		case PRCM_MMC1:
		case PRCM_MMC2:
		case PRCM_MMC3:
		case PRCM_MMC4:
			break;
		case PRCM_USBP1_PHY:
		case PRCM_USBP2_PHY:
			prcm_v4_hsusbhost_set_source(mod, 0);
		case PRCM_USB:
		case PRCM_USBTLL:
		case PRCM_USBP1_UTMI:
		case PRCM_USBP1_HSIC:
		case PRCM_USBP2_UTMI:
		case PRCM_USBP2_HSIC:
			prcm_v4_hsusbhost_activate(mod);
			return;
		case  PRCM_GPIO0:
		case  PRCM_GPIO1:
		case  PRCM_GPIO2:
		case  PRCM_GPIO3:
		case  PRCM_GPIO4:
		case  PRCM_GPIO5:
			/* XXX */
			break;
		case PRCM_I2C0:
		case PRCM_I2C1:
		case PRCM_I2C2:
		case PRCM_I2C3:
			/* XXX */
			break;
	default:
		panic("%s: module not found", __func__);
	}
}

int
prcm_v4_hsusbhost_activate(int type)
{
	struct prcm_softc *sc = prcm_cd.cd_devs[0];
	uint32_t i;
	uint32_t clksel_reg_off;
	uint32_t clksel, oclksel;

	switch (type) {
		case PRCM_USB:
		case PRCM_USBP1_PHY:
		case PRCM_USBP2_PHY:
			/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
			clksel_reg_off = O4_L3INIT_CM2_OFFSET + 0x58;
			clksel = bus_space_read_4(sc->sc_iot, sc->sc_cm2, clksel_reg_off);
			oclksel = clksel;
			/* Enable the module and also enable the optional func clocks */
			if (type == PRCM_USB) {
				clksel &= ~O4_CLKCTRL_MODULEMODE_MASK;
				clksel |=  /*O4_CLKCTRL_MODULEMODE_ENABLE*/2;

				clksel |= (0x1 << 15); /* USB-HOST clock control: FUNC48MCLK */
			}

			break;

		default:
			panic("%s: invalid type %d", __func__, type);
			return (EINVAL);
	}
	bus_space_write_4(sc->sc_iot, sc->sc_cm2, clksel_reg_off, clksel);

	/* Try MAX_MODULE_ENABLE_WAIT number of times to check if enabled */
	for (i = 0; i < O4_MAX_MODULE_ENABLE_WAIT; i++) {
		clksel = bus_space_read_4(sc->sc_iot, sc->sc_cm2, clksel_reg_off);
		if ((clksel & O4_CLKCTRL_IDLEST_MASK) == O4_CLKCTRL_IDLEST_ENABLED)
			break;
	}

	/* Check the enabled state */
	if ((clksel & O4_CLKCTRL_IDLEST_MASK) != O4_CLKCTRL_IDLEST_ENABLED) {
		printf("Error: HERE failed to enable module with clock %d\n", type);
		printf("Error: 0x%08x => 0x%08x\n", clksel_reg_off, clksel);
		return (ETIMEDOUT);
	}

	return (0);
}

int
prcm_v4_hsusbhost_set_source(int clk, int clksrc)
{
	struct prcm_softc *sc = prcm_cd.cd_devs[0];
	uint32_t clksel_reg_off;
	uint32_t clksel;
	unsigned int bit;

	if (clk == PRCM_USBP1_PHY)
		bit = 24;
	else if (clk != PRCM_USBP2_PHY)
		bit = 25;
	else
		return (-EINVAL);

	/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
	clksel_reg_off = O4_L3INIT_CM2_OFFSET + 0x58;
	clksel = bus_space_read_4(sc->sc_iot, sc->sc_cm2, clksel_reg_off);

	/* XXX: Set the clock source to either external or internal */
	if (clksrc == 0)
		clksel |= (0x1 << bit);
	else
		clksel &= ~(0x1 << bit);

	bus_space_write_4(sc->sc_iot, sc->sc_cm2, clksel_reg_off, clksel);

	return (0);
}

/*
 * OMAP35xx Power, Reset, and Clock Management Reference Guide
 * (sprufa5.pdf) and AM/DM37x Multimedia Device Technical Reference
 * Manual (sprugn4h.pdf) note that DPLL5 provides a 120MHz clock for
 * peripheral domain modules (page 107 and page 302).
 * The reference clock for DPLL5 is DPLL5_ALWON_FCLK which is
 * SYS_CLK, running at 13MHz.
 */
int
prcm_setup_dpll5(struct prcm_softc *sc)
{
	uint32_t val;

	/*
	 * We need to set the multiplier and divider values for PLL.
	 * To end up with 120MHz we take SYS_CLK, divide by it and multiply
	 * with 120 (sprugn4h.pdf, 13.4.11.4.1 SSC Configuration)
	 */
	val = ((120 & 0x7ff) << 8) | ((SYS_CLK - 1) & 0x7f);
	bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL4_PLL, val);

	/* Clock divider from the PLL to the 120MHz clock. */
	bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_CLKSEL5_PLL, val);

	/*
	 * spruf98o.pdf, page 2319:
	 * PERIPH2_DPLL_FREQSEL is 0x7 1.75MHz to 2.1MHz
	 * EN_PERIPH2_DPLL is 0x7
	 */
	val = (7 << 4) | (7 << 0);
	bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_CLKEN2_PLL, val);

	/* Disable the interconnect clock auto-idle. */
	bus_space_write_4(sc->sc_iot, sc->sc_prcm, CM_AUTOIDLE2_PLL, 0x0);

	/* Wait until DPLL5 is locked and there's clock activity. */
	while ((val = bus_space_read_4(sc->sc_iot, sc->sc_prcm,
	    CM_IDLEST_CKGEN) & 0x01) == 0x00) {
#ifdef DIAGNOSTIC
		printf("CM_IDLEST_PLL = 0x%08x\n", val);
#endif
	}

	return 0;
}
