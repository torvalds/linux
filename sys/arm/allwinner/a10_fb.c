/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 *
 * $FreeBSD$
 */

/*
 * Allwinner A10/A20 Framebuffer
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/fbio.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include "fb_if.h"
#include "hdmi_if.h"

#define	FB_DEFAULT_W	800
#define	FB_DEFAULT_H	600
#define	FB_DEFAULT_REF	60
#define	FB_BPP		32
#define	FB_ALIGN	0x1000

#define	HDMI_ENABLE_DELAY	20000
#define	DEBE_FREQ		300000000

#define	DOT_CLOCK_TO_HZ(c)	((c) * 1000)

/* Display backend */
#define	DEBE_REG_START		0x800
#define	DEBE_REG_END		0x1000
#define	DEBE_REG_WIDTH		4
#define	DEBE_MODCTL		0x800
#define	MODCTL_ITLMOD_EN	(1 << 28)
#define	MODCTL_OUT_SEL_MASK	(0x7 << 20)
#define	MODCTL_OUT_SEL(sel)	((sel) << 20)
#define	OUT_SEL_LCD		0
#define	MODCTL_LAY0_EN		(1 << 8)
#define	MODCTL_START_CTL	(1 << 1)
#define	MODCTL_EN		(1 << 0)
#define	DEBE_DISSIZE		0x808
#define	DIS_HEIGHT(h)		(((h) - 1) << 16)
#define	DIS_WIDTH(w)		(((w) - 1) << 0)
#define	DEBE_LAYSIZE0		0x810
#define	LAY_HEIGHT(h)		(((h) - 1) << 16)
#define	LAY_WIDTH(w)		(((w) - 1) << 0)
#define	DEBE_LAYCOOR0		0x820
#define	LAY_XCOOR(x)		((x) << 16)
#define	LAY_YCOOR(y)		((y) << 0)
#define	DEBE_LAYLINEWIDTH0	0x840
#define	DEBE_LAYFB_L32ADD0	0x850
#define	LAYFB_L32ADD(pa)	((pa) << 3)
#define	DEBE_LAYFB_H4ADD	0x860
#define	LAY0FB_H4ADD(pa)	((pa) >> 29)
#define	DEBE_REGBUFFCTL		0x870
#define	REGBUFFCTL_LOAD		(1 << 0)
#define	DEBE_ATTCTL1		0x8a0
#define	ATTCTL1_FBFMT(fmt)	((fmt) << 8)
#define	FBFMT_XRGB8888		9
#define	ATTCTL1_FBPS(ps)	((ps) << 0)
#define	FBPS_32BPP_ARGB		0

/* Timing controller */
#define	TCON_GCTL		0x000
#define	GCTL_TCON_EN		(1 << 31)
#define	GCTL_IO_MAP_SEL_TCON1	(1 << 0)
#define	TCON_GINT1		0x008
#define	GINT1_TCON1_LINENO(n)	(((n) + 2) << 0)
#define	TCON0_DCLK		0x044
#define	DCLK_EN			0xf0000000
#define	TCON1_CTL		0x090
#define	TCON1_EN		(1 << 31)
#define	INTERLACE_EN		(1 << 20)
#define	TCON1_SRC_SEL(src)	((src) << 0)
#define	TCON1_SRC_CH1		0
#define	TCON1_SRC_CH2		1
#define	TCON1_SRC_BLUE		2
#define	TCON1_START_DELAY(sd)	((sd) << 4)
#define	TCON1_BASIC0		0x094
#define	TCON1_BASIC1		0x098
#define	TCON1_BASIC2		0x09c
#define	TCON1_BASIC3		0x0a0
#define	TCON1_BASIC4		0x0a4
#define	TCON1_BASIC5		0x0a8
#define	BASIC_X(x)		(((x) - 1) << 16)
#define	BASIC_Y(y)		(((y) - 1) << 0)
#define	BASIC3_HT(ht)		(((ht) - 1) << 16)
#define	BASIC3_HBP(hbp)		(((hbp) - 1) << 0)
#define	BASIC4_VT(vt)		((vt) << 16)
#define	BASIC4_VBP(vbp)		(((vbp) - 1) << 0)
#define	BASIC5_HSPW(hspw)	(((hspw) - 1) << 16)
#define	BASIC5_VSPW(vspw)	(((vspw) - 1) << 0)
#define	TCON1_IO_POL		0x0f0
#define	IO_POL_IO2_INV		(1 << 26)
#define	IO_POL_PHSYNC		(1 << 25)
#define	IO_POL_PVSYNC		(1 << 24)
#define	TCON1_IO_TRI		0x0f4
#define	IO0_OUTPUT_TRI_EN	(1 << 24)
#define	IO1_OUTPUT_TRI_EN	(1 << 25)
#define	IO_TRI_MASK		0xffffffff
#define	START_DELAY(vbl)	(MIN(32, (vbl)) - 2)
#define	VBLANK_LEN(vt, vd, i)	((((vt) << (i)) >> 1) - (vd) - 2)
#define	VTOTAL(vt)		((vt) * 2)
#define	DIVIDE(x, y)		(((x) + ((y) / 2)) / (y))

struct a10fb_softc {
	device_t		dev;
	device_t		fbdev;
	struct resource		*res[2];

	/* Framebuffer */
	struct fb_info		info;
	size_t			fbsize;
	bus_addr_t		paddr;
	vm_offset_t		vaddr;

	/* HDMI */
	eventhandler_tag	hdmi_evh;
};

static struct resource_spec a10fb_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* DEBE */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },	/* TCON */
	{ -1, 0 }
};

#define	DEBE_READ(sc, reg)		bus_read_4((sc)->res[0], (reg))
#define	DEBE_WRITE(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

#define	TCON_READ(sc, reg)		bus_read_4((sc)->res[1], (reg))
#define	TCON_WRITE(sc, reg, val)	bus_write_4((sc)->res[1], (reg), (val))

static int
a10fb_allocfb(struct a10fb_softc *sc)
{
	sc->vaddr = kmem_alloc_contig(sc->fbsize, M_NOWAIT | M_ZERO, 0, ~0,
	    FB_ALIGN, 0, VM_MEMATTR_WRITE_COMBINING);
	if (sc->vaddr == 0) {
		device_printf(sc->dev, "failed to allocate FB memory\n");
		return (ENOMEM);
	}
	sc->paddr = pmap_kextract(sc->vaddr);

	return (0);
}

static void
a10fb_freefb(struct a10fb_softc *sc)
{
	kmem_free(sc->vaddr, sc->fbsize);
}

static int
a10fb_setup_debe(struct a10fb_softc *sc, const struct videomode *mode)
{
	int width, height, interlace, reg;
	clk_t clk_ahb, clk_dram, clk_debe;
	hwreset_t rst;
	uint32_t val;
	int error;

	interlace = !!(mode->flags & VID_INTERLACE);
	width = mode->hdisplay;
	height = mode->vdisplay << interlace;

	/* Leave reset */
	error = hwreset_get_by_ofw_name(sc->dev, 0, "de_be", &rst);
	if (error != 0) {
		device_printf(sc->dev, "cannot find reset 'de_be'\n");
		return (error);
	}
	error = hwreset_deassert(rst);
	if (error != 0) {
		device_printf(sc->dev, "couldn't de-assert reset 'de_be'\n");
		return (error);
	}
	/* Gating AHB clock for BE */
	error = clk_get_by_ofw_name(sc->dev, 0, "ahb_de_be", &clk_ahb);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'ahb_de_be'\n");
		return (error);
	}
	error = clk_enable(clk_ahb);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable clk 'ahb_de_be'\n");
		return (error);
	}
	/* Enable DRAM clock to BE */
	error = clk_get_by_ofw_name(sc->dev, 0, "dram_de_be", &clk_dram);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'dram_de_be'\n");
		return (error);
	}
	error = clk_enable(clk_dram);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable clk 'dram_de_be'\n");
		return (error);
	}
	/* Set BE clock to 300MHz and enable */
	error = clk_get_by_ofw_name(sc->dev, 0, "de_be", &clk_debe);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'de_be'\n");
		return (error);
	}
	error = clk_set_freq(clk_debe, DEBE_FREQ, CLK_SET_ROUND_DOWN);
	if (error != 0) {
		device_printf(sc->dev, "cannot set 'de_be' frequency\n");
		return (error);
	}
	error = clk_enable(clk_debe);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable clk 'de_be'\n");
		return (error);
	}

	/* Initialize all registers to 0 */
	for (reg = DEBE_REG_START; reg < DEBE_REG_END; reg += DEBE_REG_WIDTH)
		DEBE_WRITE(sc, reg, 0);

	/* Enable display backend */
	DEBE_WRITE(sc, DEBE_MODCTL, MODCTL_EN);

	/* Set display size */
	DEBE_WRITE(sc, DEBE_DISSIZE, DIS_HEIGHT(height) | DIS_WIDTH(width));

	/* Set layer 0 size, position, and stride */
	DEBE_WRITE(sc, DEBE_LAYSIZE0, LAY_HEIGHT(height) | LAY_WIDTH(width));
	DEBE_WRITE(sc, DEBE_LAYCOOR0, LAY_XCOOR(0) | LAY_YCOOR(0));
	DEBE_WRITE(sc, DEBE_LAYLINEWIDTH0, width * FB_BPP);

	/* Point layer 0 to FB memory */
	DEBE_WRITE(sc, DEBE_LAYFB_L32ADD0, LAYFB_L32ADD(sc->paddr));
	DEBE_WRITE(sc, DEBE_LAYFB_H4ADD, LAY0FB_H4ADD(sc->paddr));

	/* Set backend format and pixel sequence */
	DEBE_WRITE(sc, DEBE_ATTCTL1, ATTCTL1_FBFMT(FBFMT_XRGB8888) |
	    ATTCTL1_FBPS(FBPS_32BPP_ARGB));

	/* Enable layer 0, output to LCD, setup interlace */
	val = DEBE_READ(sc, DEBE_MODCTL);
	val |= MODCTL_LAY0_EN;
	val &= ~MODCTL_OUT_SEL_MASK;
	val |= MODCTL_OUT_SEL(OUT_SEL_LCD);
	if (interlace)
		val |= MODCTL_ITLMOD_EN;
	else
		val &= ~MODCTL_ITLMOD_EN;
	DEBE_WRITE(sc, DEBE_MODCTL, val);

	/* Commit settings */
	DEBE_WRITE(sc, DEBE_REGBUFFCTL, REGBUFFCTL_LOAD);

	/* Start DEBE */
	val = DEBE_READ(sc, DEBE_MODCTL);
	val |= MODCTL_START_CTL;
	DEBE_WRITE(sc, DEBE_MODCTL, val);

	return (0);
}

static int
a10fb_setup_pll(struct a10fb_softc *sc, uint64_t freq)
{
	clk_t clk_sclk1, clk_sclk2;
	int error;

	error = clk_get_by_ofw_name(sc->dev, 0, "lcd_ch1_sclk1", &clk_sclk1);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'lcd_ch1_sclk1'\n");
		return (error);
	}
	error = clk_get_by_ofw_name(sc->dev, 0, "lcd_ch1_sclk2", &clk_sclk2);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'lcd_ch1_sclk2'\n");
		return (error);
	}

	error = clk_set_freq(clk_sclk2, freq, 0);
	if (error != 0) {
		device_printf(sc->dev, "cannot set lcd ch1 frequency\n");
		return (error);
	}
	error = clk_enable(clk_sclk2);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable lcd ch1 sclk2\n");
		return (error);
	}
	error = clk_enable(clk_sclk1);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable lcd ch1 sclk1\n");
		return (error);
	}

	return (0);
}

static int
a10fb_setup_tcon(struct a10fb_softc *sc, const struct videomode *mode)
{
	u_int interlace, hspw, hbp, vspw, vbp, vbl, width, height, start_delay;
	u_int vtotal, framerate, clk;
	clk_t clk_ahb;
	hwreset_t rst;
	uint32_t val;
	int error;

	interlace = !!(mode->flags & VID_INTERLACE);
	width = mode->hdisplay;
	height = mode->vdisplay;
	hspw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_start;
	vspw = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_start;
	vbl = VBLANK_LEN(mode->vtotal, mode->vdisplay, interlace);
	start_delay = START_DELAY(vbl);

	/* Leave reset */
	error = hwreset_get_by_ofw_name(sc->dev, 0, "lcd", &rst);
	if (error != 0) {
		device_printf(sc->dev, "cannot find reset 'lcd'\n");
		return (error);
	}
	error = hwreset_deassert(rst);
	if (error != 0) {
		device_printf(sc->dev, "couldn't de-assert reset 'lcd'\n");
		return (error);
	}
	/* Gating AHB clock for LCD */
	error = clk_get_by_ofw_name(sc->dev, 0, "ahb_lcd", &clk_ahb);
	if (error != 0) {
		device_printf(sc->dev, "cannot find clk 'ahb_lcd'\n");
		return (error);
	}
	error = clk_enable(clk_ahb);
	if (error != 0) {
		device_printf(sc->dev, "cannot enable clk 'ahb_lcd'\n");
		return (error);
	}

	/* Disable TCON and TCON1 */
	TCON_WRITE(sc, TCON_GCTL, 0);
	TCON_WRITE(sc, TCON1_CTL, 0);

	/* Enable clocks */
	TCON_WRITE(sc, TCON0_DCLK, DCLK_EN);

	/* Disable IO and data output ports */
	TCON_WRITE(sc, TCON1_IO_TRI, IO_TRI_MASK);

	/* Disable TCON and select TCON1 */
	TCON_WRITE(sc, TCON_GCTL, GCTL_IO_MAP_SEL_TCON1);

	/* Source width and height */
	TCON_WRITE(sc, TCON1_BASIC0, BASIC_X(width) | BASIC_Y(height));
	/* Scaler width and height */
	TCON_WRITE(sc, TCON1_BASIC1, BASIC_X(width) | BASIC_Y(height));
	/* Output width and height */
	TCON_WRITE(sc, TCON1_BASIC2, BASIC_X(width) | BASIC_Y(height));
	/* Horizontal total and back porch */
	TCON_WRITE(sc, TCON1_BASIC3, BASIC3_HT(mode->htotal) | BASIC3_HBP(hbp));
	/* Vertical total and back porch */
	vtotal = VTOTAL(mode->vtotal);
	if (interlace) {
		framerate = DIVIDE(DIVIDE(DOT_CLOCK_TO_HZ(mode->dot_clock),
		    mode->htotal), mode->vtotal);
		clk = mode->htotal * (VTOTAL(mode->vtotal) + 1) * framerate;
		if ((clk / 2) == DOT_CLOCK_TO_HZ(mode->dot_clock))
			vtotal += 1;
	}
	TCON_WRITE(sc, TCON1_BASIC4, BASIC4_VT(vtotal) | BASIC4_VBP(vbp));
	/* Horizontal and vertical sync */
	TCON_WRITE(sc, TCON1_BASIC5, BASIC5_HSPW(hspw) | BASIC5_VSPW(vspw));
	/* Polarity */
	val = IO_POL_IO2_INV;
	if (mode->flags & VID_PHSYNC)
		val |= IO_POL_PHSYNC;
	if (mode->flags & VID_PVSYNC)
		val |= IO_POL_PVSYNC;
	TCON_WRITE(sc, TCON1_IO_POL, val);

	/* Set scan line for TCON1 line trigger */
	TCON_WRITE(sc, TCON_GINT1, GINT1_TCON1_LINENO(start_delay));

	/* Enable TCON1 */
	val = TCON1_EN;
	if (interlace)
		val |= INTERLACE_EN;
	val |= TCON1_START_DELAY(start_delay);
	val |= TCON1_SRC_SEL(TCON1_SRC_CH1);
	TCON_WRITE(sc, TCON1_CTL, val);

	/* Setup PLL */
	return (a10fb_setup_pll(sc, DOT_CLOCK_TO_HZ(mode->dot_clock)));
}

static void
a10fb_enable_tcon(struct a10fb_softc *sc, int onoff)
{
	uint32_t val;

	/* Enable TCON */
	val = TCON_READ(sc, TCON_GCTL);
	if (onoff)
		val |= GCTL_TCON_EN;
	else
		val &= ~GCTL_TCON_EN;
	TCON_WRITE(sc, TCON_GCTL, val);

	/* Enable TCON1 IO0/IO1 outputs */
	val = TCON_READ(sc, TCON1_IO_TRI);
	if (onoff)
		val &= ~(IO0_OUTPUT_TRI_EN | IO1_OUTPUT_TRI_EN);
	else
		val |= (IO0_OUTPUT_TRI_EN | IO1_OUTPUT_TRI_EN);
	TCON_WRITE(sc, TCON1_IO_TRI, val);
}

static int
a10fb_configure(struct a10fb_softc *sc, const struct videomode *mode)
{
	size_t fbsize;
	int error;

	fbsize = round_page(mode->hdisplay * mode->vdisplay * (FB_BPP / NBBY));

	/* Detach the old FB device */
	if (sc->fbdev != NULL) {
		device_delete_child(sc->dev, sc->fbdev);
		sc->fbdev = NULL;
	}

	/* If the FB size has changed, free the old FB memory */
	if (sc->fbsize > 0 && sc->fbsize != fbsize) {
		a10fb_freefb(sc);
		sc->vaddr = 0;
	}

	/* Allocate the FB if necessary */
	sc->fbsize = fbsize;
	if (sc->vaddr == 0) {
		error = a10fb_allocfb(sc);
		if (error != 0) {
			device_printf(sc->dev, "failed to allocate FB memory\n");
			return (ENXIO);
		}
	}

	/* Setup display backend */
	error = a10fb_setup_debe(sc, mode);
	if (error != 0)
		return (error);

	/* Setup display timing controller */
	error = a10fb_setup_tcon(sc, mode);
	if (error != 0)
		return (error);

	/* Attach framebuffer device */
	sc->info.fb_name = device_get_nameunit(sc->dev);
	sc->info.fb_vbase = (intptr_t)sc->vaddr;
	sc->info.fb_pbase = sc->paddr;
	sc->info.fb_size = sc->fbsize;
	sc->info.fb_bpp = sc->info.fb_depth = FB_BPP;
	sc->info.fb_stride = mode->hdisplay * (FB_BPP / NBBY);
	sc->info.fb_width = mode->hdisplay;
	sc->info.fb_height = mode->vdisplay;

	sc->fbdev = device_add_child(sc->dev, "fbd", device_get_unit(sc->dev));
	if (sc->fbdev == NULL) {
		device_printf(sc->dev, "failed to add fbd child\n");
		return (ENOENT);
	}

	error = device_probe_and_attach(sc->fbdev);
	if (error != 0) {
		device_printf(sc->dev, "failed to attach fbd device\n");
		return (error);
	}

	return (0);
}

static void
a10fb_hdmi_event(void *arg, device_t hdmi_dev)
{
	const struct videomode *mode;
	struct videomode hdmi_mode;
	struct a10fb_softc *sc;
	struct edid_info ei;
	uint8_t *edid;
	uint32_t edid_len;
	int error;

	sc = arg;
	edid = NULL;
	edid_len = 0;
	mode = NULL;

	error = HDMI_GET_EDID(hdmi_dev, &edid, &edid_len);
	if (error != 0) {
		device_printf(sc->dev, "failed to get EDID: %d\n", error);
	} else {
		error = edid_parse(edid, &ei);
		if (error != 0) {
			device_printf(sc->dev, "failed to parse EDID: %d\n",
			    error);
		} else {
			if (bootverbose)
				edid_print(&ei);
			mode = ei.edid_preferred_mode;
		}
	}

	/* If the preferred mode could not be determined, use the default */
	if (mode == NULL)
		mode = pick_mode_by_ref(FB_DEFAULT_W, FB_DEFAULT_H,
		    FB_DEFAULT_REF);

	if (mode == NULL) {
		device_printf(sc->dev, "failed to find usable video mode\n");
		return;
	}

	if (bootverbose)
		device_printf(sc->dev, "using %dx%d\n",
		    mode->hdisplay, mode->vdisplay);

	/* Disable HDMI */
	HDMI_ENABLE(hdmi_dev, 0);

	/* Disable timing controller */
	a10fb_enable_tcon(sc, 0);

	/* Configure DEBE and TCON */
	error = a10fb_configure(sc, mode);
	if (error != 0) {
		device_printf(sc->dev, "failed to configure FB: %d\n", error);
		return;
	}

	hdmi_mode = *mode;
	hdmi_mode.hskew = mode->hsync_end - mode->hsync_start;
	hdmi_mode.flags |= VID_HSKEW;
	HDMI_SET_VIDEOMODE(hdmi_dev, &hdmi_mode);

	/* Enable timing controller */
	a10fb_enable_tcon(sc, 1);

	DELAY(HDMI_ENABLE_DELAY);

	/* Enable HDMI */
	HDMI_ENABLE(hdmi_dev, 1);
}

static int
a10fb_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun7i-a20-fb"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner Framebuffer");
	return (BUS_PROBE_DEFAULT);
}

static int
a10fb_attach(device_t dev)
{
	struct a10fb_softc *sc;

	sc = device_get_softc(dev);

	sc->dev = dev;

	if (bus_alloc_resources(dev, a10fb_spec, sc->res)) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	sc->hdmi_evh = EVENTHANDLER_REGISTER(hdmi_event,
	    a10fb_hdmi_event, sc, 0);

	return (0);
}

static struct fb_info *
a10fb_fb_getinfo(device_t dev)
{
	struct a10fb_softc *sc;

	sc = device_get_softc(dev);

	return (&sc->info);
}

static device_method_t a10fb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a10fb_probe),
	DEVMETHOD(device_attach,	a10fb_attach),

	/* FB interface */
	DEVMETHOD(fb_getinfo,		a10fb_fb_getinfo),

	DEVMETHOD_END
};

static driver_t a10fb_driver = {
	"fb",
	a10fb_methods,
	sizeof(struct a10fb_softc),
};

static devclass_t a10fb_devclass;

DRIVER_MODULE(fb, simplebus, a10fb_driver, a10fb_devclass, 0, 0);
