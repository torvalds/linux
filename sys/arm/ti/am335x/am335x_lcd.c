/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include "opt_syscons.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/clock.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/fb/fbreg.h>
#ifdef DEV_SC
#include <dev/syscons/syscons.h>
#else /* VT */
#include <dev/vt/vt.h>
#endif

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_scm.h>

#include "am335x_lcd.h"
#include "am335x_pwm.h"

#include "fb_if.h"
#include "hdmi_if.h"

#define	LCD_PID			0x00
#define	LCD_CTRL		0x04
#define		CTRL_DIV_MASK		0xff
#define		CTRL_DIV_SHIFT		8
#define		CTRL_AUTO_UFLOW_RESTART	(1 << 1)
#define		CTRL_RASTER_MODE	1
#define		CTRL_LIDD_MODE		0
#define	LCD_LIDD_CTRL		0x0C
#define	LCD_LIDD_CS0_CONF	0x10
#define	LCD_LIDD_CS0_ADDR	0x14
#define	LCD_LIDD_CS0_DATA	0x18
#define	LCD_LIDD_CS1_CONF	0x1C
#define	LCD_LIDD_CS1_ADDR	0x20
#define	LCD_LIDD_CS1_DATA	0x24
#define	LCD_RASTER_CTRL		0x28
#define		RASTER_CTRL_TFT24_UNPACKED	(1 << 26)
#define		RASTER_CTRL_TFT24		(1 << 25)
#define		RASTER_CTRL_STN565		(1 << 24)
#define		RASTER_CTRL_TFTPMAP		(1 << 23)
#define		RASTER_CTRL_NIBMODE		(1 << 22)
#define		RASTER_CTRL_PALMODE_SHIFT	20
#define		PALETTE_PALETTE_AND_DATA	0x00
#define		PALETTE_PALETTE_ONLY		0x01
#define		PALETTE_DATA_ONLY		0x02
#define		RASTER_CTRL_REQDLY_SHIFT	12
#define		RASTER_CTRL_MONO8B		(1 << 9)
#define		RASTER_CTRL_RBORDER		(1 << 8)
#define		RASTER_CTRL_LCDTFT		(1 << 7)
#define		RASTER_CTRL_LCDBW		(1 << 1)
#define		RASTER_CTRL_LCDEN		(1 << 0)
#define	LCD_RASTER_TIMING_0	0x2C
#define		RASTER_TIMING_0_HBP_SHIFT	24
#define		RASTER_TIMING_0_HFP_SHIFT	16
#define		RASTER_TIMING_0_HSW_SHIFT	10
#define		RASTER_TIMING_0_PPLLSB_SHIFT	4
#define		RASTER_TIMING_0_PPLMSB_SHIFT	3
#define	LCD_RASTER_TIMING_1	0x30
#define		RASTER_TIMING_1_VBP_SHIFT	24
#define		RASTER_TIMING_1_VFP_SHIFT	16
#define		RASTER_TIMING_1_VSW_SHIFT	10
#define		RASTER_TIMING_1_LPP_SHIFT	0
#define	LCD_RASTER_TIMING_2	0x34
#define		RASTER_TIMING_2_HSWHI_SHIFT	27
#define		RASTER_TIMING_2_LPP_B10_SHIFT	26
#define		RASTER_TIMING_2_PHSVS		(1 << 25)
#define		RASTER_TIMING_2_PHSVS_RISE	(1 << 24)
#define		RASTER_TIMING_2_PHSVS_FALL	(0 << 24)
#define		RASTER_TIMING_2_IOE		(1 << 23)
#define		RASTER_TIMING_2_IPC		(1 << 22)
#define		RASTER_TIMING_2_IHS		(1 << 21)
#define		RASTER_TIMING_2_IVS		(1 << 20)
#define		RASTER_TIMING_2_ACBI_SHIFT	16
#define		RASTER_TIMING_2_ACB_SHIFT	8
#define		RASTER_TIMING_2_HBPHI_SHIFT	4
#define		RASTER_TIMING_2_HFPHI_SHIFT	0
#define	LCD_RASTER_SUBPANEL	0x38
#define	LCD_RASTER_SUBPANEL2	0x3C
#define	LCD_LCDDMA_CTRL		0x40
#define		LCDDMA_CTRL_DMA_MASTER_PRIO_SHIFT		16
#define		LCDDMA_CTRL_TH_FIFO_RDY_SHIFT	8
#define		LCDDMA_CTRL_BURST_SIZE_SHIFT	4
#define		LCDDMA_CTRL_BYTES_SWAP		(1 << 3)
#define		LCDDMA_CTRL_BE			(1 << 1)
#define		LCDDMA_CTRL_FB0_ONLY		0
#define		LCDDMA_CTRL_FB0_FB1		(1 << 0)
#define	LCD_LCDDMA_FB0_BASE	0x44
#define	LCD_LCDDMA_FB0_CEILING	0x48
#define	LCD_LCDDMA_FB1_BASE	0x4C
#define	LCD_LCDDMA_FB1_CEILING	0x50
#define	LCD_SYSCONFIG		0x54
#define		SYSCONFIG_STANDBY_FORCE		(0 << 4)
#define		SYSCONFIG_STANDBY_NONE		(1 << 4)
#define		SYSCONFIG_STANDBY_SMART		(2 << 4)
#define		SYSCONFIG_IDLE_FORCE		(0 << 2)
#define		SYSCONFIG_IDLE_NONE		(1 << 2)
#define		SYSCONFIG_IDLE_SMART		(2 << 2)
#define	LCD_IRQSTATUS_RAW	0x58
#define	LCD_IRQSTATUS		0x5C
#define	LCD_IRQENABLE_SET	0x60
#define	LCD_IRQENABLE_CLEAR	0x64
#define		IRQ_EOF1		(1 << 9)
#define		IRQ_EOF0		(1 << 8)
#define		IRQ_PL			(1 << 6)
#define		IRQ_FUF			(1 << 5)
#define		IRQ_ACB			(1 << 3)
#define		IRQ_SYNC_LOST		(1 << 2)
#define		IRQ_RASTER_DONE		(1 << 1)
#define		IRQ_FRAME_DONE		(1 << 0)
#define	LCD_END_OF_INT_IND	0x68
#define	LCD_CLKC_ENABLE		0x6C
#define		CLKC_ENABLE_DMA		(1 << 2)
#define		CLKC_ENABLE_LDID	(1 << 1)
#define		CLKC_ENABLE_CORE	(1 << 0)
#define	LCD_CLKC_RESET		0x70
#define		CLKC_RESET_MAIN		(1 << 3)
#define		CLKC_RESET_DMA		(1 << 2)
#define		CLKC_RESET_LDID		(1 << 1)
#define		CLKC_RESET_CORE		(1 << 0)

#define	LCD_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	LCD_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	LCD_LOCK_INIT(_sc)	mtx_init(&(_sc)->sc_mtx, \
    device_get_nameunit(_sc->sc_dev), "am335x_lcd", MTX_DEF)
#define	LCD_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx);

#define	LCD_READ4(_sc, reg)	bus_read_4((_sc)->sc_mem_res, reg);
#define	LCD_WRITE4(_sc, reg, value)	\
    bus_write_4((_sc)->sc_mem_res, reg, value);

/* Backlight is controlled by eCAS interface on PWM unit 0 */
#define	PWM_UNIT	0
#define	PWM_PERIOD	100

#define	MODE_HBP(mode)	((mode)->htotal - (mode)->hsync_end)
#define	MODE_HFP(mode)	((mode)->hsync_start - (mode)->hdisplay)
#define	MODE_HSW(mode)	((mode)->hsync_end - (mode)->hsync_start)
#define	MODE_VBP(mode)	((mode)->vtotal - (mode)->vsync_end)
#define	MODE_VFP(mode)	((mode)->vsync_start - (mode)->vdisplay)
#define	MODE_VSW(mode)	((mode)->vsync_end - (mode)->vsync_start)

#define	MAX_PIXEL_CLOCK	126000
#define	MAX_BANDWIDTH	(1280*1024*60)

struct am335x_lcd_softc {
	device_t		sc_dev;
	struct fb_info		sc_fb_info;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	void			*sc_intr_hl;
	struct mtx		sc_mtx;
	int			sc_backlight;
	struct sysctl_oid	*sc_oid;

	struct panel_info	sc_panel;

	/* Framebuffer */
	bus_dma_tag_t		sc_dma_tag;
	bus_dmamap_t		sc_dma_map;
	size_t			sc_fb_size;
	bus_addr_t		sc_fb_phys;
	uint8_t			*sc_fb_base;

	/* HDMI framer */
	phandle_t		sc_hdmi_framer;
	eventhandler_tag	sc_hdmi_evh;
};

static void
am335x_fb_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static uint32_t
am335x_lcd_calc_divisor(uint32_t reference, uint32_t freq)
{
	uint32_t div, i;
	uint32_t delta, min_delta;

	min_delta = freq;
	div = 255;

	/* Raster mode case: divisors are in range from 2 to 255 */
	for (i = 2; i < 255; i++) {
		delta = abs(reference/i - freq);
		if (delta < min_delta) {
			div = i;
			min_delta = delta;
		}
	}

	return (div);
}

static int
am335x_lcd_sysctl_backlight(SYSCTL_HANDLER_ARGS)
{
	struct am335x_lcd_softc *sc = (struct am335x_lcd_softc*)arg1;
	int error;
	int backlight;

	backlight = sc->sc_backlight;
	error = sysctl_handle_int(oidp, &backlight, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	if (backlight < 0)
		backlight = 0;
	if (backlight > 100)
		backlight = 100;

	LCD_LOCK(sc);
	error = am335x_pwm_config_ecap(PWM_UNIT, PWM_PERIOD,
	    backlight*PWM_PERIOD/100);
	if (error == 0)
		sc->sc_backlight = backlight;
	LCD_UNLOCK(sc);

	return (error);
}

static uint32_t
am335x_mode_vrefresh(const struct videomode *mode)
{
	uint32_t refresh;

	/* Calculate vertical refresh rate */
        refresh = (mode->dot_clock * 1000 / mode->htotal);
	refresh = (refresh + mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & VID_INTERLACE)
		refresh *= 2;
	if (mode->flags & VID_DBLSCAN)
		refresh /= 2;

	return refresh;
}

static int
am335x_mode_is_valid(const struct videomode *mode)
{
	uint32_t hbp, hfp, hsw;
	uint32_t vbp, vfp, vsw;

	if (mode->dot_clock > MAX_PIXEL_CLOCK)
		return (0);

	if (mode->hdisplay & 0xf)
		return (0);

	if (mode->vdisplay > 2048)
		return (0);

	/* Check ranges for timing parameters */
	hbp = MODE_HBP(mode) - 1;
	hfp = MODE_HFP(mode) - 1;
	hsw = MODE_HSW(mode) - 1;
	vbp = MODE_VBP(mode);
	vfp = MODE_VFP(mode);
	vsw = MODE_VSW(mode) - 1;

	if (hbp > 0x3ff)
		return (0);
	if (hfp > 0x3ff)
		return (0);
	if (hsw > 0x3ff)
		return (0);

	if (vbp > 0xff)
		return (0);
	if (vfp > 0xff)
		return (0);
	if (vsw > 0x3f)
		return (0);
	if (mode->vdisplay*mode->hdisplay*am335x_mode_vrefresh(mode) 
	    > MAX_BANDWIDTH)
		return (0);

	return (1);
}

static void
am335x_read_hdmi_property(device_t dev)
{
	phandle_t node, xref;
	phandle_t endpoint;
	phandle_t hdmi_xref;
	struct am335x_lcd_softc *sc;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);
	sc->sc_hdmi_framer = 0;

	/*
	 * Old FreeBSD way of referencing to HDMI framer
	 */
	if (OF_getencprop(node, "hdmi", &hdmi_xref, sizeof(hdmi_xref)) != -1) {
		sc->sc_hdmi_framer = hdmi_xref;
		return;
	}

	/*
	 * Use bindings described in Linux docs:
	 * bindings/media/video-interfaces.txt
	 * We assume that the only endpoint in LCDC node
	 * is HDMI framer.
	 */
	node = ofw_bus_find_child(node, "port");

	/* No media bindings */
	if (node == 0)
		return;

	for (endpoint = OF_child(node); endpoint != 0; endpoint = OF_peer(endpoint)) {
		if (OF_getencprop(endpoint, "remote-endpoint", &xref, sizeof(xref)) != -1) {
			/* port/port@0/endpoint@0 */
			node = OF_node_from_xref(xref);
			/* port/port@0 */
			node = OF_parent(node);
			/* port */
			node = OF_parent(node);
			/* actual owner of port, in our case HDMI framer */
			sc->sc_hdmi_framer = OF_xref_from_node(OF_parent(node));
			if (sc->sc_hdmi_framer != 0)
				return;
		}
	}
}

static int
am335x_read_property(device_t dev, phandle_t node, const char *name, uint32_t *val)
{
	pcell_t cell;

	if ((OF_getencprop(node, name, &cell, sizeof(cell))) <= 0) {
		device_printf(dev, "missing '%s' attribute in LCD panel info\n",
		    name);
		return (ENXIO);
	}

	*val = cell;

	return (0);
}

static int
am335x_read_timing(device_t dev, phandle_t node, struct panel_info *panel)
{
	int error;
	phandle_t timings_node, timing_node, native;

	timings_node = ofw_bus_find_child(node, "display-timings");
	if (timings_node == 0) {
		device_printf(dev, "no \"display-timings\" node\n");
		return (-1);
	}

	if (OF_searchencprop(timings_node, "native-mode", &native,
	    sizeof(native)) == -1) {
		device_printf(dev, "no \"native-mode\" reference in \"timings\" node\n");
		return (-1);
	}

	timing_node = OF_node_from_xref(native);

	error = 0;
	if ((error = am335x_read_property(dev, timing_node,
	    "hactive", &panel->panel_width)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "vactive", &panel->panel_height)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "hfront-porch", &panel->panel_hfp)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "hback-porch", &panel->panel_hbp)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "hsync-len", &panel->panel_hsw)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "vfront-porch", &panel->panel_vfp)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "vback-porch", &panel->panel_vbp)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "vsync-len", &panel->panel_vsw)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "clock-frequency", &panel->panel_pxl_clk)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "pixelclk-active", &panel->pixelclk_active)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "hsync-active", &panel->hsync_active)))
		goto out;

	if ((error = am335x_read_property(dev, timing_node,
	    "vsync-active", &panel->vsync_active)))
		goto out;

out:
	return (error);
}

static int
am335x_read_panel_info(device_t dev, phandle_t node, struct panel_info *panel)
{
	phandle_t panel_info_node;

	panel_info_node = ofw_bus_find_child(node, "panel-info");
	if (panel_info_node == 0)
		return (-1);

	am335x_read_property(dev, panel_info_node,
	    "ac-bias", &panel->ac_bias);

	am335x_read_property(dev, panel_info_node,
	    "ac-bias-intrpt", &panel->ac_bias_intrpt);

	am335x_read_property(dev, panel_info_node,
	    "dma-burst-sz", &panel->dma_burst_sz);

	am335x_read_property(dev, panel_info_node,
	    "bpp", &panel->bpp);

	am335x_read_property(dev, panel_info_node,
	    "fdd", &panel->fdd);

	am335x_read_property(dev, panel_info_node,
	    "sync-edge", &panel->sync_edge);

	am335x_read_property(dev, panel_info_node,
	    "sync-ctrl", &panel->sync_ctrl);

	return (0);
}

static void
am335x_lcd_intr(void *arg)
{
	struct am335x_lcd_softc *sc = arg;
	uint32_t reg; 

	reg = LCD_READ4(sc, LCD_IRQSTATUS);
	LCD_WRITE4(sc, LCD_IRQSTATUS, reg);
	/* Read value back to make sure it reached the hardware */
	reg = LCD_READ4(sc, LCD_IRQSTATUS);

	if (reg & IRQ_SYNC_LOST) {
		reg = LCD_READ4(sc, LCD_RASTER_CTRL);
		reg &= ~RASTER_CTRL_LCDEN;
		LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 

		reg = LCD_READ4(sc, LCD_RASTER_CTRL);
		reg |= RASTER_CTRL_LCDEN;
		LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 
		goto done;
	}

	if (reg & IRQ_PL) {
		reg = LCD_READ4(sc, LCD_RASTER_CTRL);
		reg &= ~RASTER_CTRL_LCDEN;
		LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 

		reg = LCD_READ4(sc, LCD_RASTER_CTRL);
		reg |= RASTER_CTRL_LCDEN;
		LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 
		goto done;
	}

	if (reg & IRQ_EOF0) {
		LCD_WRITE4(sc, LCD_LCDDMA_FB0_BASE, sc->sc_fb_phys); 
		LCD_WRITE4(sc, LCD_LCDDMA_FB0_CEILING, sc->sc_fb_phys + sc->sc_fb_size - 1); 
		reg &= ~IRQ_EOF0;
	}

	if (reg & IRQ_EOF1) {
		LCD_WRITE4(sc, LCD_LCDDMA_FB1_BASE, sc->sc_fb_phys); 
		LCD_WRITE4(sc, LCD_LCDDMA_FB1_CEILING, sc->sc_fb_phys + sc->sc_fb_size - 1); 
		reg &= ~IRQ_EOF1;
	}

	if (reg & IRQ_FUF) {
		/* TODO: Handle FUF */
	}

	if (reg & IRQ_ACB) {
		/* TODO: Handle ACB */
	}

done:
	LCD_WRITE4(sc, LCD_END_OF_INT_IND, 0);
	/* Read value back to make sure it reached the hardware */
	reg = LCD_READ4(sc, LCD_END_OF_INT_IND);
}

static const struct videomode *
am335x_lcd_pick_mode(struct edid_info *ei)
{
	const struct videomode *videomode;
	const struct videomode *m;
	int n;

	/* Get standard VGA as default */
	videomode = NULL;

	/*
	 * Pick a mode.
	 */
	if (ei->edid_preferred_mode != NULL) {
		if (am335x_mode_is_valid(ei->edid_preferred_mode))
			videomode = ei->edid_preferred_mode;
	}

	if (videomode == NULL) {
		m = ei->edid_modes;

		sort_modes(ei->edid_modes,
		    &ei->edid_preferred_mode,
		    ei->edid_nmodes);
		for (n = 0; n < ei->edid_nmodes; n++)
			if (am335x_mode_is_valid(&m[n])) {
				videomode = &m[n];
				break;
			}
	}

	return videomode;
}

static int
am335x_lcd_configure(struct am335x_lcd_softc *sc)
{
	int div;
	uint32_t reg, timing0, timing1, timing2;
	uint32_t burst_log;
	size_t dma_size;
	uint32_t hbp, hfp, hsw;
	uint32_t vbp, vfp, vsw;
	uint32_t width, height;
	unsigned int ref_freq;
	int err;

	/*
	 * try to adjust clock to get double of requested frequency
	 * HDMI/DVI displays are very sensitive to error in frequncy value
	 */
	if (ti_prcm_clk_set_source_freq(LCDC_CLK, sc->sc_panel.panel_pxl_clk*2)) {
		device_printf(sc->sc_dev, "can't set source frequency\n");
		return (ENXIO);
	}

	if (ti_prcm_clk_get_source_freq(LCDC_CLK, &ref_freq)) {
		device_printf(sc->sc_dev, "can't get reference frequency\n");
		return (ENXIO);
	}

	/* Panle initialization */
	dma_size = round_page(sc->sc_panel.panel_width*sc->sc_panel.panel_height*sc->sc_panel.bpp/8);

	/*
	 * Now allocate framebuffer memory
	 */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(sc->sc_dev),
	    4, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    dma_size, 1,			/* maxsize, nsegments */
	    dma_size, 0,			/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sc_dma_tag);
	if (err)
		goto done;

	err = bus_dmamem_alloc(sc->sc_dma_tag, (void **)&sc->sc_fb_base,
	    BUS_DMA_COHERENT, &sc->sc_dma_map);

	if (err) {
		device_printf(sc->sc_dev, "cannot allocate framebuffer\n");
		goto done;
	}

	err = bus_dmamap_load(sc->sc_dma_tag, sc->sc_dma_map, sc->sc_fb_base,
	    dma_size, am335x_fb_dmamap_cb, &sc->sc_fb_phys, BUS_DMA_NOWAIT);

	if (err) {
		device_printf(sc->sc_dev, "cannot load DMA map\n");
		goto done;
	}

	/* Make sure it's blank */
	memset(sc->sc_fb_base, 0x0, dma_size);

	/* Calculate actual FB Size */
	sc->sc_fb_size = sc->sc_panel.panel_width*sc->sc_panel.panel_height*sc->sc_panel.bpp/8;

	/* Only raster mode is supported */
	reg = CTRL_RASTER_MODE;
	div = am335x_lcd_calc_divisor(ref_freq, sc->sc_panel.panel_pxl_clk);
	reg |= (div << CTRL_DIV_SHIFT);
	LCD_WRITE4(sc, LCD_CTRL, reg); 

	/* Set timing */
	timing0 = timing1 = timing2 = 0;

	hbp = sc->sc_panel.panel_hbp - 1;
	hfp = sc->sc_panel.panel_hfp - 1;
	hsw = sc->sc_panel.panel_hsw - 1;

	vbp = sc->sc_panel.panel_vbp;
	vfp = sc->sc_panel.panel_vfp;
	vsw = sc->sc_panel.panel_vsw - 1;

	height = sc->sc_panel.panel_height - 1;
	width = sc->sc_panel.panel_width - 1;

	/* Horizontal back porch */
	timing0 |= (hbp & 0xff) << RASTER_TIMING_0_HBP_SHIFT;
	timing2 |= ((hbp >> 8) & 3) << RASTER_TIMING_2_HBPHI_SHIFT;
	/* Horizontal front porch */
	timing0 |= (hfp & 0xff) << RASTER_TIMING_0_HFP_SHIFT;
	timing2 |= ((hfp >> 8) & 3) << RASTER_TIMING_2_HFPHI_SHIFT;
	/* Horizontal sync width */
	timing0 |= (hsw & 0x3f) << RASTER_TIMING_0_HSW_SHIFT;
	timing2 |= ((hsw >> 6) & 0xf) << RASTER_TIMING_2_HSWHI_SHIFT;

	/* Vertical back porch, front porch, sync width */
	timing1 |= (vbp & 0xff) << RASTER_TIMING_1_VBP_SHIFT;
	timing1 |= (vfp & 0xff) << RASTER_TIMING_1_VFP_SHIFT;
	timing1 |= (vsw & 0x3f) << RASTER_TIMING_1_VSW_SHIFT;

	/* Pixels per line */
	timing0 |= ((width >> 10) & 1)
	    << RASTER_TIMING_0_PPLMSB_SHIFT;
	timing0 |= ((width >> 4) & 0x3f)
	    << RASTER_TIMING_0_PPLLSB_SHIFT;

	/* Lines per panel */
	timing1 |= (height & 0x3ff) 
	    << RASTER_TIMING_1_LPP_SHIFT;
	timing2 |= ((height >> 10 ) & 1) 
	    << RASTER_TIMING_2_LPP_B10_SHIFT;

	/* clock signal settings */
	if (sc->sc_panel.sync_ctrl)
		timing2 |= RASTER_TIMING_2_PHSVS;
	if (sc->sc_panel.sync_edge)
		timing2 |= RASTER_TIMING_2_PHSVS_RISE;
	else
		timing2 |= RASTER_TIMING_2_PHSVS_FALL;
	if (sc->sc_panel.hsync_active == 0)
		timing2 |= RASTER_TIMING_2_IHS;
	if (sc->sc_panel.vsync_active == 0)
		timing2 |= RASTER_TIMING_2_IVS;
	if (sc->sc_panel.pixelclk_active == 0)
		timing2 |= RASTER_TIMING_2_IPC;

	/* AC bias */
	timing2 |= (sc->sc_panel.ac_bias << RASTER_TIMING_2_ACB_SHIFT);
	timing2 |= (sc->sc_panel.ac_bias_intrpt << RASTER_TIMING_2_ACBI_SHIFT);

	LCD_WRITE4(sc, LCD_RASTER_TIMING_0, timing0); 
	LCD_WRITE4(sc, LCD_RASTER_TIMING_1, timing1); 
	LCD_WRITE4(sc, LCD_RASTER_TIMING_2, timing2); 

	/* DMA settings */
	reg = LCDDMA_CTRL_FB0_FB1;
	/* Find power of 2 for current burst size */
	switch (sc->sc_panel.dma_burst_sz) {
	case 1:
		burst_log = 0;
		break;
	case 2:
		burst_log = 1;
		break;
	case 4:
		burst_log = 2;
		break;
	case 8:
		burst_log = 3;
		break;
	case 16:
	default:
		burst_log = 4;
		break;
	}
	reg |= (burst_log << LCDDMA_CTRL_BURST_SIZE_SHIFT);
	/* XXX: FIFO TH */
	reg |= (0 << LCDDMA_CTRL_TH_FIFO_RDY_SHIFT);
	LCD_WRITE4(sc, LCD_LCDDMA_CTRL, reg); 

	LCD_WRITE4(sc, LCD_LCDDMA_FB0_BASE, sc->sc_fb_phys); 
	LCD_WRITE4(sc, LCD_LCDDMA_FB0_CEILING, sc->sc_fb_phys + sc->sc_fb_size - 1); 
	LCD_WRITE4(sc, LCD_LCDDMA_FB1_BASE, sc->sc_fb_phys); 
	LCD_WRITE4(sc, LCD_LCDDMA_FB1_CEILING, sc->sc_fb_phys + sc->sc_fb_size - 1); 

	/* Enable LCD */
	reg = RASTER_CTRL_LCDTFT;
	reg |= (sc->sc_panel.fdd << RASTER_CTRL_REQDLY_SHIFT);
	reg |= (PALETTE_DATA_ONLY << RASTER_CTRL_PALMODE_SHIFT);
	if (sc->sc_panel.bpp >= 24)
		reg |= RASTER_CTRL_TFT24;
	if (sc->sc_panel.bpp == 32)
		reg |= RASTER_CTRL_TFT24_UNPACKED;
	LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 

	LCD_WRITE4(sc, LCD_CLKC_ENABLE,
	    CLKC_ENABLE_DMA | CLKC_ENABLE_LDID | CLKC_ENABLE_CORE);

	LCD_WRITE4(sc, LCD_CLKC_RESET, CLKC_RESET_MAIN);
	DELAY(100);
	LCD_WRITE4(sc, LCD_CLKC_RESET, 0);

	reg = IRQ_EOF1 | IRQ_EOF0 | IRQ_FUF | IRQ_PL |
	    IRQ_ACB | IRQ_SYNC_LOST |  IRQ_RASTER_DONE |
	    IRQ_FRAME_DONE;
	LCD_WRITE4(sc, LCD_IRQENABLE_SET, reg);

	reg = LCD_READ4(sc, LCD_RASTER_CTRL);
 	reg |= RASTER_CTRL_LCDEN;
	LCD_WRITE4(sc, LCD_RASTER_CTRL, reg); 

	LCD_WRITE4(sc, LCD_SYSCONFIG,
	    SYSCONFIG_STANDBY_SMART | SYSCONFIG_IDLE_SMART); 

	sc->sc_fb_info.fb_name = device_get_nameunit(sc->sc_dev);
	sc->sc_fb_info.fb_vbase = (intptr_t)sc->sc_fb_base;
	sc->sc_fb_info.fb_pbase = sc->sc_fb_phys;
	sc->sc_fb_info.fb_size = sc->sc_fb_size;
	sc->sc_fb_info.fb_bpp = sc->sc_fb_info.fb_depth = sc->sc_panel.bpp;
	sc->sc_fb_info.fb_stride = sc->sc_panel.panel_width*sc->sc_panel.bpp / 8;
	sc->sc_fb_info.fb_width = sc->sc_panel.panel_width;
	sc->sc_fb_info.fb_height = sc->sc_panel.panel_height;

#ifdef	DEV_SC
	err = (sc_attach_unit(device_get_unit(sc->sc_dev),
	    device_get_flags(sc->sc_dev) | SC_AUTODETECT_KBD));

	if (err) {
		device_printf(sc->sc_dev, "failed to attach syscons\n");
		goto fail;
	}

	am335x_lcd_syscons_setup((vm_offset_t)sc->sc_fb_base, sc->sc_fb_phys, &panel);
#else /* VT */
	device_t fbd = device_add_child(sc->sc_dev, "fbd",
	device_get_unit(sc->sc_dev));
	if (fbd != NULL) {
		if (device_probe_and_attach(fbd) != 0)
			device_printf(sc->sc_dev, "failed to attach fbd device\n");
	} else
		device_printf(sc->sc_dev, "failed to add fbd child\n");
#endif

done:
	return (err);
}

static void
am335x_lcd_hdmi_event(void *arg, device_t hdmi, int event)
{
	struct am335x_lcd_softc *sc;
	const struct videomode *videomode;
	struct videomode hdmi_mode;
	device_t hdmi_dev;
	uint8_t *edid;
	uint32_t edid_len;
	struct edid_info ei;

	sc = arg;

	/* Nothing to work with */
	if (!sc->sc_hdmi_framer) {
		device_printf(sc->sc_dev, "HDMI event without HDMI framer set\n");
		return;
	}

	hdmi_dev = OF_device_from_xref(sc->sc_hdmi_framer);
	if (!hdmi_dev) {
		device_printf(sc->sc_dev, "no actual device for \"hdmi\" property\n");
		return;
	}

	edid = NULL;
	edid_len = 0;
	if (HDMI_GET_EDID(hdmi_dev, &edid, &edid_len) != 0) {
		device_printf(sc->sc_dev, "failed to get EDID info from HDMI framer\n");
		return;
	}

	videomode = NULL;

	if (edid_parse(edid, &ei) == 0) {
		edid_print(&ei);
		videomode = am335x_lcd_pick_mode(&ei);
	} else
		device_printf(sc->sc_dev, "failed to parse EDID\n");

	/* Use standard VGA as fallback */
	if (videomode == NULL)
		videomode = pick_mode_by_ref(640, 480, 60);

	if (videomode == NULL) {
		device_printf(sc->sc_dev, "failed to find usable videomode");
		return;
	}

	device_printf(sc->sc_dev, "detected videomode: %dx%d @ %dKHz\n", videomode->hdisplay,
		videomode->vdisplay, am335x_mode_vrefresh(videomode));

	sc->sc_panel.panel_width = videomode->hdisplay;
	sc->sc_panel.panel_height = videomode->vdisplay;
	sc->sc_panel.panel_hfp = videomode->hsync_start - videomode->hdisplay;
	sc->sc_panel.panel_hbp = videomode->htotal - videomode->hsync_end;
	sc->sc_panel.panel_hsw = videomode->hsync_end - videomode->hsync_start;
	sc->sc_panel.panel_vfp = videomode->vsync_start - videomode->vdisplay;
	sc->sc_panel.panel_vbp = videomode->vtotal - videomode->vsync_end;
	sc->sc_panel.panel_vsw = videomode->vsync_end - videomode->vsync_start;
	sc->sc_panel.pixelclk_active = 1;

	/* logic for HSYNC should be reversed */
	if (videomode->flags & VID_NHSYNC)
		sc->sc_panel.hsync_active = 1;
	else
		sc->sc_panel.hsync_active = 0;

	if (videomode->flags & VID_NVSYNC)
		sc->sc_panel.vsync_active = 0;
	else
		sc->sc_panel.vsync_active = 1;

	sc->sc_panel.panel_pxl_clk = videomode->dot_clock * 1000;

	am335x_lcd_configure(sc);

	memcpy(&hdmi_mode, videomode, sizeof(hdmi_mode));
	hdmi_mode.hskew = videomode->hsync_end - videomode->hsync_start;
	hdmi_mode.flags |= VID_HSKEW;

	HDMI_SET_VIDEOMODE(hdmi_dev, &hdmi_mode);
}

static int
am335x_lcd_probe(device_t dev)
{
#ifdef DEV_SC
	int err;
#endif

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,am33xx-tilcdc"))
		return (ENXIO);

	device_set_desc(dev, "AM335x LCD controller");

#ifdef DEV_SC
	err = sc_probe_unit(device_get_unit(dev), 
	    device_get_flags(dev) | SC_AUTODETECT_KBD);
	if (err != 0)
		return (err);
#endif

	return (BUS_PROBE_DEFAULT);
}

static int
am335x_lcd_attach(device_t dev)
{
	struct am335x_lcd_softc *sc;

	int err;
	int rid;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	phandle_t root, panel_node;

	err = 0;
	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	am335x_read_hdmi_property(dev);

	root = OF_finddevice("/");
	if (root == -1) {
		device_printf(dev, "failed to get FDT root node\n");
		return (ENXIO);
	}

	sc->sc_panel.ac_bias = 255;
	sc->sc_panel.ac_bias_intrpt = 0;
	sc->sc_panel.dma_burst_sz = 16;
	sc->sc_panel.bpp = 16;
	sc->sc_panel.fdd = 128;
	sc->sc_panel.sync_edge = 0;
	sc->sc_panel.sync_ctrl = 1;

	panel_node = fdt_find_compatible(root, "ti,tilcdc,panel", 1);
	if (panel_node != 0) {
		device_printf(dev, "using static panel info\n");
		if (am335x_read_panel_info(dev, panel_node, &sc->sc_panel)) {
			device_printf(dev, "failed to read panel info\n");
			return (ENXIO);
		}

		if (am335x_read_timing(dev, panel_node, &sc->sc_panel)) {
			device_printf(dev, "failed to read timings\n");
			return (ENXIO);
		}
	}

	ti_prcm_clk_enable(LCDC_CLK);

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot allocate interrupt\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
			NULL, am335x_lcd_intr, sc,
			&sc->sc_intr_hl) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, rid,
		    sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, rid,
		    sc->sc_mem_res);
		device_printf(dev, "Unable to setup the irq handler.\n");
		return (ENXIO);
	}

	LCD_LOCK_INIT(sc);

	/* Init backlight interface */
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);
	sc->sc_oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "backlight", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    am335x_lcd_sysctl_backlight, "I", "LCD backlight");
	sc->sc_backlight = 0;
	/* Check if eCAS interface is available at this point */
	if (am335x_pwm_config_ecap(PWM_UNIT,
	    PWM_PERIOD, PWM_PERIOD) == 0)
		sc->sc_backlight = 100;

	if (panel_node != 0)
		am335x_lcd_configure(sc);
	else
		sc->sc_hdmi_evh = EVENTHANDLER_REGISTER(hdmi_event,
		    am335x_lcd_hdmi_event, sc, EVENTHANDLER_PRI_ANY);

	return (0);
}

static int
am335x_lcd_detach(device_t dev)
{
	/* Do not let unload driver */
	return (EBUSY);
}

static struct fb_info *
am335x_lcd_fb_getinfo(device_t dev)
{
	struct am335x_lcd_softc *sc;

	sc = device_get_softc(dev);

	return (&sc->sc_fb_info);
}

static device_method_t am335x_lcd_methods[] = {
	DEVMETHOD(device_probe,		am335x_lcd_probe),
	DEVMETHOD(device_attach,	am335x_lcd_attach),
	DEVMETHOD(device_detach,	am335x_lcd_detach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		am335x_lcd_fb_getinfo),

	DEVMETHOD_END
};

static driver_t am335x_lcd_driver = {
	"fb",
	am335x_lcd_methods,
	sizeof(struct am335x_lcd_softc),
};

static devclass_t am335x_lcd_devclass;

DRIVER_MODULE(am335x_lcd, simplebus, am335x_lcd_driver, am335x_lcd_devclass, 0, 0);
MODULE_VERSION(am335x_lcd, 1);
MODULE_DEPEND(am335x_lcd, simplebus, 1, 1, 1);
