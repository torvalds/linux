/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Justin Hibbits
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/pci/pcivar.h>

#ifndef PCI_VENDOR_ID_ATI
#define PCI_VENDOR_ID_ATI 0x1002
#endif

/* From the xf86-video-ati driver's radeon_reg.h */
#define RADEON_LVDS_GEN_CNTL         0x02d0
#define  RADEON_LVDS_ON               (1   <<  0)
#define  RADEON_LVDS_DISPLAY_DIS      (1   <<  1)
#define  RADEON_LVDS_PANEL_TYPE       (1   <<  2)
#define  RADEON_LVDS_PANEL_FORMAT     (1   <<  3)
#define  RADEON_LVDS_RST_FM           (1   <<  6)
#define  RADEON_LVDS_EN               (1   <<  7)
#define  RADEON_LVDS_BL_MOD_LEVEL_SHIFT 8
#define  RADEON_LVDS_BL_MOD_LEVEL_MASK (0xff << 8)
#define  RADEON_LVDS_BL_MOD_EN        (1   << 16)
#define  RADEON_LVDS_DIGON            (1   << 18)
#define  RADEON_LVDS_BLON             (1   << 19)
#define RADEON_LVDS_PLL_CNTL         0x02d4
#define  RADEON_LVDS_PLL_EN           (1   << 16)
#define  RADEON_LVDS_PLL_RESET        (1   << 17)
#define RADEON_PIXCLKS_CNTL          0x002d
#define  RADEON_PIXCLK_LVDS_ALWAYS_ONb (1   << 14)
#define RADEON_DISP_PWR_MAN          0x0d08
#define  RADEON_AUTO_PWRUP_EN          (1 << 26)
#define RADEON_CLOCK_CNTL_DATA       0x000c
#define RADEON_CLOCK_CNTL_INDEX      0x0008
#define  RADEON_PLL_WR_EN              (1 << 7)
#define RADEON_CRTC_GEN_CNTL         0x0050

struct atibl_softc {
	struct resource *sc_memr;
	int		 sc_level;
};

static void atibl_identify(driver_t *driver, device_t parent);
static int atibl_probe(device_t dev);
static int atibl_attach(device_t dev);
static int atibl_setlevel(struct atibl_softc *sc, int newlevel);
static int atibl_getlevel(struct atibl_softc *sc);
static int atibl_resume(device_t dev);
static int atibl_suspend(device_t dev);
static int atibl_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t atibl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	atibl_identify),
	DEVMETHOD(device_probe,		atibl_probe),
	DEVMETHOD(device_attach,	atibl_attach),
	DEVMETHOD(device_suspend,	atibl_suspend),
	DEVMETHOD(device_resume,	atibl_resume),
	{0, 0},
};

static driver_t	atibl_driver = {
	"backlight",
	atibl_methods,
	sizeof(struct atibl_softc)
};

static devclass_t atibl_devclass;

DRIVER_MODULE(atibl, vgapci, atibl_driver, atibl_devclass, 0, 0);

static void
atibl_identify(driver_t *driver, device_t parent)
{
	if (OF_finddevice("mac-io/backlight") == -1)
		return;
	if (device_find_child(parent, "backlight", -1) == NULL)
		device_add_child(parent, "backlight", -1);
}

static int
atibl_probe(device_t dev)
{
	char		control[8];
	phandle_t	handle;

	handle = OF_finddevice("mac-io/backlight");

	if (handle == -1)
		return (ENXIO);

	if (OF_getprop(handle, "backlight-control", &control, sizeof(control)) < 0)
		return (ENXIO);

	if (strcmp(control, "ati") != 0 &&
	    (strcmp(control, "mnca") != 0 ||
	    pci_get_vendor(device_get_parent(dev)) != 0x1002))
		return (ENXIO);

	device_set_desc(dev, "PowerBook backlight for ATI graphics");

	return (0);
}

static int
atibl_attach(device_t dev)
{
	struct atibl_softc	*sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	int			 rid;

	sc = device_get_softc(dev);

	rid = 0x18;	/* BAR[2], for the MMIO register */
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
			RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_memr == NULL) {
		device_printf(dev, "Could not alloc mem resource!\n");
		return (ENXIO);
	}

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "level", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    atibl_sysctl, "I", "Backlight level (0-100)");

	return (0);
}

static uint32_t __inline
atibl_pll_rreg(struct atibl_softc *sc, uint32_t reg)
{
	uint32_t data, save, tmp;

	bus_write_1(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX, (reg & 0x3f));
	(void)bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA);
	(void)bus_read_4(sc->sc_memr, RADEON_CRTC_GEN_CNTL);

	data = bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA);

	/* Only necessary on R300, but won't hurt others. */
	save = bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX);
	tmp = save & (~0x3f | RADEON_PLL_WR_EN);
	bus_write_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX, tmp);
	tmp = bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA);
	bus_write_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX, save);

	return data;
}

static void __inline
atibl_pll_wreg(struct atibl_softc *sc, uint32_t reg, uint32_t val)
{
	uint32_t save, tmp;

	bus_write_1(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX,
	    ((reg & 0x3f) | RADEON_PLL_WR_EN));
	(void)bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA);
	(void)bus_read_4(sc->sc_memr, RADEON_CRTC_GEN_CNTL);

	bus_write_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA, val);
	DELAY(5000);

	/* Only necessary on R300, but won't hurt others. */
	save = bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX);
	tmp = save & (~0x3f | RADEON_PLL_WR_EN);
	bus_write_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX, tmp);
	tmp = bus_read_4(sc->sc_memr, RADEON_CLOCK_CNTL_DATA);
	bus_write_4(sc->sc_memr, RADEON_CLOCK_CNTL_INDEX, save);
}

static int
atibl_setlevel(struct atibl_softc *sc, int newlevel)
{
	uint32_t lvds_gen_cntl;
	uint32_t lvds_pll_cntl;
	uint32_t pixclks_cntl;
	uint32_t disp_pwr_reg;

	if (newlevel > 100)
		newlevel = 100;

	if (newlevel < 0)
		newlevel = 0;

	lvds_gen_cntl = bus_read_4(sc->sc_memr, RADEON_LVDS_GEN_CNTL);

	if (newlevel > 0) {
		newlevel = (newlevel * 5) / 2 + 5;
		disp_pwr_reg = bus_read_4(sc->sc_memr, RADEON_DISP_PWR_MAN);
		disp_pwr_reg |= RADEON_AUTO_PWRUP_EN;
		bus_write_4(sc->sc_memr, RADEON_DISP_PWR_MAN, disp_pwr_reg);
		lvds_pll_cntl = bus_read_4(sc->sc_memr, RADEON_LVDS_PLL_CNTL);
		lvds_pll_cntl |= RADEON_LVDS_PLL_EN;
		bus_write_4(sc->sc_memr, RADEON_LVDS_PLL_CNTL, lvds_pll_cntl);
		lvds_pll_cntl &= ~RADEON_LVDS_PLL_RESET;
		bus_write_4(sc->sc_memr, RADEON_LVDS_PLL_CNTL, lvds_pll_cntl);
		DELAY(1000);

		lvds_gen_cntl &= ~(RADEON_LVDS_DISPLAY_DIS | 
		    RADEON_LVDS_BL_MOD_LEVEL_MASK);
		lvds_gen_cntl |= RADEON_LVDS_ON | RADEON_LVDS_EN |
		    RADEON_LVDS_DIGON | RADEON_LVDS_BLON;
		lvds_gen_cntl |= (newlevel << RADEON_LVDS_BL_MOD_LEVEL_SHIFT) &
		    RADEON_LVDS_BL_MOD_LEVEL_MASK;
		lvds_gen_cntl |= RADEON_LVDS_BL_MOD_EN;
		DELAY(200000);
		bus_write_4(sc->sc_memr, RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
	} else {
		pixclks_cntl = atibl_pll_rreg(sc, RADEON_PIXCLKS_CNTL);
		atibl_pll_wreg(sc, RADEON_PIXCLKS_CNTL,
		    pixclks_cntl & ~RADEON_PIXCLK_LVDS_ALWAYS_ONb);
		lvds_gen_cntl |= RADEON_LVDS_DISPLAY_DIS;
		lvds_gen_cntl &= ~(RADEON_LVDS_BL_MOD_EN | RADEON_LVDS_BL_MOD_LEVEL_MASK);
		bus_write_4(sc->sc_memr, RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);
		lvds_gen_cntl &= ~(RADEON_LVDS_ON | RADEON_LVDS_EN);
		DELAY(200000);
		bus_write_4(sc->sc_memr, RADEON_LVDS_GEN_CNTL, lvds_gen_cntl);

		atibl_pll_wreg(sc, RADEON_PIXCLKS_CNTL, pixclks_cntl);
		DELAY(200000);
	}

	return (0);
}

static int
atibl_getlevel(struct atibl_softc *sc)
{
	uint32_t	lvds_gen_cntl;
	int			level;

	lvds_gen_cntl = bus_read_4(sc->sc_memr, RADEON_LVDS_GEN_CNTL);

	level = ((lvds_gen_cntl & RADEON_LVDS_BL_MOD_LEVEL_MASK) >>
	    RADEON_LVDS_BL_MOD_LEVEL_SHIFT);
	if (level != 0)
		level = ((level - 5) * 2) / 5;

	return (level);
}

static int
atibl_suspend(device_t dev)
{
	struct atibl_softc *sc;

	sc = device_get_softc(dev);

	sc->sc_level = atibl_getlevel(sc);
	atibl_setlevel(sc, 0);

	return (0);
}

static int
atibl_resume(device_t dev)
{
	struct atibl_softc *sc;

	sc = device_get_softc(dev);

	atibl_setlevel(sc, sc->sc_level);

	return (0);
}

static int
atibl_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct atibl_softc *sc;
	int newlevel, error;

	sc = arg1;

	newlevel = atibl_getlevel(sc);

	error = sysctl_handle_int(oidp, &newlevel, 0, req);

	if (error || !req->newptr)
		return (error);

	return (atibl_setlevel(sc, newlevel));
}
