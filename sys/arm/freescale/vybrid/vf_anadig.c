/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Vybrid Family Analog components control digital interface (ANADIG)
 * Chapter 11, Vybrid Reference Manual, Rev. 5, 07/2013
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
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>

#define	ANADIG_PLL3_CTRL	0x010	/* PLL3 Control */
#define	ANADIG_PLL7_CTRL	0x020	/* PLL7 Control */
#define	ANADIG_PLL2_CTRL	0x030	/* PLL2 Control */
#define	ANADIG_PLL2_SS		0x040	/* PLL2 Spread Spectrum */
#define	ANADIG_PLL2_NUM		0x050	/* PLL2 Numerator */
#define	ANADIG_PLL2_DENOM	0x060	/* PLL2 Denominator */
#define	ANADIG_PLL4_CTRL	0x070	/* PLL4 Control */
#define	ANADIG_PLL4_NUM		0x080	/* PLL4 Numerator */
#define	ANADIG_PLL4_DENOM	0x090	/* PLL4 Denominator */
#define	ANADIG_PLL6_CTRL	0x0A0	/* PLL6 Control */
#define	ANADIG_PLL6_NUM		0x0B0	/* PLL6 Numerator */
#define	ANADIG_PLL6_DENOM	0x0C0	/* PLL6 Denominator */
#define	ANADIG_PLL5_CTRL	0x0E0	/* PLL5 Control */
#define	ANADIG_PLL3_PFD		0x0F0	/* PLL3 PFD */
#define	ANADIG_PLL2_PFD		0x100	/* PLL2 PFD */
#define	ANADIG_REG_1P1		0x110	/* Regulator 1P1 */
#define	ANADIG_REG_3P0		0x120	/* Regulator 3P0 */
#define	ANADIG_REG_2P5		0x130	/* Regulator 2P5 */
#define	ANADIG_ANA_MISC0	0x150	/* Analog Miscellaneous */
#define	ANADIG_ANA_MISC1	0x160	/* Analog Miscellaneous */
#define	ANADIG_ANADIG_DIGPROG	0x260	/* Digital Program */
#define	ANADIG_PLL1_CTRL	0x270	/* PLL1 Control */
#define	ANADIG_PLL1_SS		0x280	/* PLL1 Spread Spectrum */
#define	ANADIG_PLL1_NUM		0x290	/* PLL1 Numerator */
#define	ANADIG_PLL1_DENOM	0x2A0	/* PLL1 Denominator */
#define	ANADIG_PLL1_PFD		0x2B0	/* PLL1_PFD */
#define	ANADIG_PLL_LOCK		0x2C0	/* PLL Lock */

#define	USB_VBUS_DETECT(n)		(0x1A0 + 0x60 * n)
#define	USB_CHRG_DETECT(n)		(0x1B0 + 0x60 * n)
#define	USB_VBUS_DETECT_STATUS(n)	(0x1C0 + 0x60 * n)
#define	USB_CHRG_DETECT_STATUS(n)	(0x1D0 + 0x60 * n)
#define	USB_LOOPBACK(n)			(0x1E0 + 0x60 * n)
#define	USB_MISC(n)			(0x1F0 + 0x60 * n)

#define	ANADIG_PLL_LOCKED	(1U << 31)
#define	ENABLE_LINREG		(1 << 0)
#define	EN_CLK_TO_UTMI		(1 << 30)

#define	CTRL_BYPASS		(1 << 16)
#define	CTRL_PWR		(1 << 12)
#define	CTRL_PLL_EN		(1 << 13)
#define	EN_USB_CLKS		(1 << 6)

#define	PLL4_CTRL_DIV_SEL_S	0
#define	PLL4_CTRL_DIV_SEL_M	0x7f

struct anadig_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

struct anadig_softc *anadig_sc;

static struct resource_spec anadig_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
anadig_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-anadig"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family ANADIG Unit");
	return (BUS_PROBE_DEFAULT);
}

static int
enable_pll(struct anadig_softc *sc, int pll_ctrl)
{
	int reg;

	reg = READ4(sc, pll_ctrl);
	reg &= ~(CTRL_BYPASS | CTRL_PWR);
	if (pll_ctrl == ANADIG_PLL3_CTRL || pll_ctrl == ANADIG_PLL7_CTRL) {
		/* It is USB PLL. Power bit logic is reversed */
		reg |= (CTRL_PWR | EN_USB_CLKS);
	}
	WRITE4(sc, pll_ctrl, reg);

	/* Wait for PLL lock */
	while (!(READ4(sc, pll_ctrl) & ANADIG_PLL_LOCKED))
		;

	reg = READ4(sc, pll_ctrl);
	reg |= (CTRL_PLL_EN);
	WRITE4(sc, pll_ctrl, reg);

	return (0);
}

uint32_t
pll4_configure_output(uint32_t mfi, uint32_t mfn, uint32_t mfd)
{
	struct anadig_softc *sc;
	int reg;

	sc = anadig_sc;

	/*
	 * PLLout = Fsys * (MFI+(MFN/MFD))
	 */

	reg = READ4(sc, ANADIG_PLL4_CTRL);
	reg &= ~(PLL4_CTRL_DIV_SEL_M << PLL4_CTRL_DIV_SEL_S);
	reg |= (mfi << PLL4_CTRL_DIV_SEL_S);
	WRITE4(sc, ANADIG_PLL4_CTRL, reg);
	WRITE4(sc, ANADIG_PLL4_NUM, mfn);
	WRITE4(sc, ANADIG_PLL4_DENOM, mfd);

	return (0);
}

static int
anadig_attach(device_t dev)
{
	struct anadig_softc *sc;
	int reg;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, anadig_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	anadig_sc = sc;

	/* Enable USB PLLs */
	enable_pll(sc, ANADIG_PLL3_CTRL);
	enable_pll(sc, ANADIG_PLL7_CTRL);

	/* Enable other PLLs */
	enable_pll(sc, ANADIG_PLL1_CTRL);
	enable_pll(sc, ANADIG_PLL2_CTRL);
	enable_pll(sc, ANADIG_PLL4_CTRL);
	enable_pll(sc, ANADIG_PLL5_CTRL);
	enable_pll(sc, ANADIG_PLL6_CTRL);

	/* Enable USB voltage regulator */
	reg = READ4(sc, ANADIG_REG_3P0);
	reg |= (ENABLE_LINREG);
	WRITE4(sc, ANADIG_REG_3P0, reg);

	/* Give clocks to USB */
	reg = READ4(sc, USB_MISC(0));
	reg |= (EN_CLK_TO_UTMI);
	WRITE4(sc, USB_MISC(0), reg);

	reg = READ4(sc, USB_MISC(1));
	reg |= (EN_CLK_TO_UTMI);
	WRITE4(sc, USB_MISC(1), reg);

#if 0
	printf("USB_ANALOG_USB_MISC(0) == 0x%08x\n",
	    READ4(sc, USB_ANALOG_USB_MISC(0)));
	printf("USB_ANALOG_USB_MISC(1) == 0x%08x\n",
	    READ4(sc, USB_ANALOG_USB_MISC(1)));
#endif

	return (0);
}

static device_method_t anadig_methods[] = {
	DEVMETHOD(device_probe,		anadig_probe),
	DEVMETHOD(device_attach,	anadig_attach),
	{ 0, 0 }
};

static driver_t anadig_driver = {
	"anadig",
	anadig_methods,
	sizeof(struct anadig_softc),
};

static devclass_t anadig_devclass;

DRIVER_MODULE(anadig, simplebus, anadig_driver, anadig_devclass, 0, 0);
