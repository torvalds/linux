/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Samsung Exynos 5 Display Controller
 * Chapter 15, Exynos 5 Dual User's Manual Public Rev 1.00
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
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/eventhandler.h>
#include <sys/gpio.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/vt/vt.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <arm/samsung/exynos/exynos5_common.h>

#include "gpio_if.h"

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include "fb_if.h"

#define	FIMDBYPASS_DISP1	(1 << 15)

#define	VIDCON0		(0x0)
#define	VIDCON0_ENVID	(1 << 1)
#define	VIDCON0_ENVID_F	(1 << 0)
#define	CLKVAL_F	0xb
#define	CLKVAL_F_OFFSET	6

#define	WINCON0		0x0020
#define	WINCON1		0x0024
#define	WINCON2		0x0028
#define	WINCON3		0x002C
#define	WINCON4		0x0030

#define	ENLOCAL_F			(1 << 22)
#define	BPPMODE_F_RGB_16BIT_565		0x5
#define	BPPMODE_F_OFFSET		2
#define	ENWIN_F_ENABLE			(1 << 0)
#define	HALF_WORD_SWAP_EN		(1 << 16)

#define	SHADOWCON	0x0034
#define	CHANNEL0_EN	(1 << 0)

#define	VIDOSD0A	0x0040
#define	VIDOSD0B	0x0044
#define	VIDOSD0C	0x0048

#define	VIDW00ADD0B0	0x00A0
#define	VIDW00ADD0B1	0x00A4
#define	VIDW00ADD0B2	0x20A0
#define	VIDW00ADD1B0	0x00D0
#define	VIDW00ADD1B1	0x00D4
#define	VIDW00ADD1B2	0x20D0

#define	VIDW00ADD2	0x0100
#define	VIDW01ADD2	0x0104
#define	VIDW02ADD2	0x0108
#define	VIDW03ADD2	0x010C
#define	VIDW04ADD2	0x0110

#define	VIDCON1		(0x04)
#define	VIDTCON0	0x0010
#define	VIDTCON1	0x0014
#define	VIDTCON2	0x0018
#define	VIDTCON3	0x001C

#define	VIDINTCON0	0x0130
#define	VIDINTCON1	0x0134

#define	VSYNC_PULSE_WIDTH_VAL		0x3
#define	VSYNC_PULSE_WIDTH_OFFSET	0
#define	V_FRONT_PORCH_VAL		0x3
#define	V_FRONT_PORCH_OFFSET		8
#define	V_BACK_PORCH_VAL		0x3
#define	V_BACK_PORCH_OFFSET		16

#define	HSYNC_PULSE_WIDTH_VAL		0x3
#define	HSYNC_PULSE_WIDTH_OFFSET	0
#define	H_FRONT_PORCH_VAL		0x3
#define	H_FRONT_PORCH_OFFSET		8
#define	H_BACK_PORCH_VAL		0x3
#define	H_BACK_PORCH_OFFSET		16

#define	HOZVAL_OFFSET		0
#define	LINEVAL_OFFSET		11

#define	OSD_RIGHTBOTX_F_OFFSET		11
#define	OSD_RIGHTBOTY_F_OFFSET		0

#define	DPCLKCON	0x27c
#define	DPCLKCON_EN	(1 << 1)

#define	DREAD4(_sc, _reg)         \
	bus_space_read_4(_sc->bst_disp, _sc->bsh_disp, _reg)
#define	DWRITE4(_sc, _reg, _val)  \
	bus_space_write_4(_sc->bst_disp, _sc->bsh_disp, _reg, _val)

struct panel_info {
	uint32_t	width;
	uint32_t	height;
	uint32_t	h_back_porch;
	uint32_t	h_pulse_width;
	uint32_t	h_front_porch;
	uint32_t	v_back_porch;
	uint32_t	v_pulse_width;
	uint32_t	v_front_porch;
	uint32_t	clk_div;
	uint32_t	backlight_pin;
	uint32_t	fixvclk;
	uint32_t	ivclk;
	uint32_t	clkval_f;
};

struct fimd_softc {
	struct resource		*res[3];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	bus_space_tag_t		bst_disp;
	bus_space_handle_t	bsh_disp;
	bus_space_tag_t		bst_sysreg;
	bus_space_handle_t	bsh_sysreg;

	void			*ih;
	device_t		dev;
	device_t		sc_fbd;		/* fbd child */
	struct fb_info		sc_info;
	struct panel_info	*panel;
};

static struct resource_spec fimd_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Timer registers */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },	/* FIMD */
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },	/* DISP */
	{ -1, 0 }
};

static int
fimd_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "exynos,fimd"))
		return (ENXIO);

	device_set_desc(dev, "Samsung Exynos 5 Display Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
get_panel_info(struct fimd_softc *sc, struct panel_info *panel)
{
	phandle_t node;
	pcell_t dts_value[3];
	int len;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	/* panel size */
	if ((len = OF_getproplen(node, "panel-size")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "panel-size", dts_value, len);
	panel->width = dts_value[0];
	panel->height = dts_value[1];

	/* hsync */
	if ((len = OF_getproplen(node, "panel-hsync")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "panel-hsync", dts_value, len);
	panel->h_back_porch = dts_value[0];
	panel->h_pulse_width = dts_value[1];
	panel->h_front_porch = dts_value[2];

	/* vsync */
	if ((len = OF_getproplen(node, "panel-vsync")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "panel-vsync", dts_value, len);
	panel->v_back_porch = dts_value[0];
	panel->v_pulse_width = dts_value[1];
	panel->v_front_porch = dts_value[2];

	/* clk divider */
	if ((len = OF_getproplen(node, "panel-clk-div")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "panel-clk-div", dts_value, len);
	panel->clk_div = dts_value[0];

	/* backlight pin */
	if ((len = OF_getproplen(node, "panel-backlight-pin")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "panel-backlight-pin", dts_value, len);
	panel->backlight_pin = dts_value[0];

	return (0);
}

static int
fimd_init(struct fimd_softc *sc)
{
	struct panel_info *panel;
	int reg;

	panel = sc->panel;

	/* fb_init */
	reg = panel->ivclk | panel->fixvclk;
	DWRITE4(sc,VIDCON1,reg);

	reg = (VIDCON0_ENVID | VIDCON0_ENVID_F);
	reg |= (panel->clkval_f << CLKVAL_F_OFFSET);
	WRITE4(sc,VIDCON0,reg);

	reg = (panel->v_pulse_width << VSYNC_PULSE_WIDTH_OFFSET);
	reg |= (panel->v_front_porch << V_FRONT_PORCH_OFFSET);
	reg |= (panel->v_back_porch << V_BACK_PORCH_OFFSET);
	DWRITE4(sc,VIDTCON0,reg);

	reg = (panel->h_pulse_width << HSYNC_PULSE_WIDTH_OFFSET);
	reg |= (panel->h_front_porch << H_FRONT_PORCH_OFFSET);
	reg |= (panel->h_back_porch << H_BACK_PORCH_OFFSET);
	DWRITE4(sc,VIDTCON1,reg);

	reg = ((panel->width - 1) << HOZVAL_OFFSET);
	reg |= ((panel->height - 1) << LINEVAL_OFFSET);
	DWRITE4(sc,VIDTCON2,reg);

	reg = sc->sc_info.fb_pbase;
	WRITE4(sc, VIDW00ADD0B0, reg);
	reg += (sc->sc_info.fb_stride * (sc->sc_info.fb_height + 1));
	WRITE4(sc, VIDW00ADD1B0, reg);
	WRITE4(sc, VIDW00ADD2, sc->sc_info.fb_stride);

	reg = ((panel->width - 1) << OSD_RIGHTBOTX_F_OFFSET);
	reg |= ((panel->height - 1) << OSD_RIGHTBOTY_F_OFFSET);
	WRITE4(sc,VIDOSD0B,reg);

	reg = panel->width * panel->height;
	WRITE4(sc,VIDOSD0C,reg);

	reg = READ4(sc, SHADOWCON);
	reg |= CHANNEL0_EN;
	reg &= ~(1 << 5); /* disable local path for channel0 */
	WRITE4(sc,SHADOWCON,reg);

	reg = BPPMODE_F_RGB_16BIT_565 << BPPMODE_F_OFFSET;
	reg |= ENWIN_F_ENABLE | HALF_WORD_SWAP_EN; /* Note: swap=0 when ENLOCAL==1 */
	reg &= ~ENLOCAL_F; /* use DMA */
	WRITE4(sc,WINCON0,reg);

	/* Enable DisplayPort Clk */
	WRITE4(sc, DPCLKCON, DPCLKCON_EN);

	return (0);
}

static int
fimd_attach(device_t dev)
{
	struct panel_info panel;
	struct fimd_softc *sc;
	device_t gpio_dev;
	int reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, fimd_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);
	sc->bst_disp = rman_get_bustag(sc->res[1]);
	sc->bsh_disp = rman_get_bushandle(sc->res[1]);
	sc->bst_sysreg = rman_get_bustag(sc->res[2]);
	sc->bsh_sysreg = rman_get_bushandle(sc->res[2]);

	if (get_panel_info(sc, &panel)) {
		device_printf(dev, "Can't get panel info\n");
		return (ENXIO);
	}

	panel.fixvclk = 0;
	panel.ivclk = 0;
	panel.clkval_f = 2;

	sc->panel = &panel;

	/* Get the GPIO device, we need this to give power to USB */
	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		/* TODO */
	}

	reg = bus_space_read_4(sc->bst_sysreg, sc->bsh_sysreg, 0x214);
	reg |= FIMDBYPASS_DISP1;
	bus_space_write_4(sc->bst_sysreg, sc->bsh_sysreg, 0x214, reg);

	sc->sc_info.fb_width = panel.width;
	sc->sc_info.fb_height = panel.height;
	sc->sc_info.fb_stride = sc->sc_info.fb_width * 2;
	sc->sc_info.fb_bpp = sc->sc_info.fb_depth = 16;
	sc->sc_info.fb_size = sc->sc_info.fb_height * sc->sc_info.fb_stride;
	sc->sc_info.fb_vbase = (intptr_t)kmem_alloc_contig(sc->sc_info.fb_size,
	    M_ZERO, 0, ~0, PAGE_SIZE, 0, VM_MEMATTR_UNCACHEABLE);
	sc->sc_info.fb_pbase = (intptr_t)vtophys(sc->sc_info.fb_vbase);

#if 0
	printf("%dx%d [%d]\n", sc->sc_info.fb_width, sc->sc_info.fb_height,
	    sc->sc_info.fb_stride);
	printf("pbase == 0x%08x\n", sc->sc_info.fb_pbase);
#endif

	memset((int8_t *)sc->sc_info.fb_vbase, 0x0, sc->sc_info.fb_size);

	fimd_init(sc);

	sc->sc_info.fb_name = device_get_nameunit(dev);

	/* Ask newbus to attach framebuffer device to me. */
	sc->sc_fbd = device_add_child(dev, "fbd", device_get_unit(dev));
	if (sc->sc_fbd == NULL)
		device_printf(dev, "Can't attach fbd device\n");

	if (device_probe_and_attach(sc->sc_fbd) != 0) {
		device_printf(sc->dev, "Failed to attach fbd device\n");
	}

	return (0);
}

static struct fb_info *
fimd_fb_getinfo(device_t dev)
{
	struct fimd_softc *sc = device_get_softc(dev);

	return (&sc->sc_info);
}

static device_method_t fimd_methods[] = {
	DEVMETHOD(device_probe,		fimd_probe),
	DEVMETHOD(device_attach,	fimd_attach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		fimd_fb_getinfo),
	{ 0, 0 }
};

static driver_t fimd_driver = {
	"fb",
	fimd_methods,
	sizeof(struct fimd_softc),
};

static devclass_t fimd_devclass;

DRIVER_MODULE(fb, simplebus, fimd_driver, fimd_devclass, 0, 0);
