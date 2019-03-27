/*-
 * Copyright (c) 2016 Justin Hibbits
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
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/vt/vt.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include "fb_if.h"

#define	DIU_DESC_1		0x000	/* Plane1 Area Descriptor Pointer Register */
#define	DIU_DESC_2		0x004	/* Plane2 Area Descriptor Pointer Register */
#define	DIU_DESC_3		0x008	/* Plane3 Area Descriptor Pointer Register */
#define	DIU_GAMMA		0x00C	/* Gamma Register */
#define	DIU_PALETTE		0x010	/* Palette Register */
#define	DIU_CURSOR		0x014	/* Cursor Register */
#define	DIU_CURS_POS		0x018	/* Cursor Position Register */
#define	 CURSOR_Y_SHIFT		 16
#define	 CURSOR_X_SHIFT		 0
#define	DIU_DIU_MODE		0x01C	/* DIU4 Mode */
#define	 DIU_MODE_M		0x7
#define	 DIU_MODE_S		0
#define	 DIU_MODE_NORMAL	0x1
#define	 DIU_MODE_2		0x2
#define	 DIU_MODE_3		0x3
#define	 DIU_MODE_COLBAR	0x4
#define	DIU_BGND		0x020	/* Background */
#define	DIU_BGND_WB		0x024	/* Background Color in write back Mode Register */
#define	DIU_DISP_SIZE		0x028	/* Display Size */
#define	 DELTA_Y_S		16
#define	 DELTA_X_S		0
#define	DIU_WB_SIZE		0x02C	/* Write back Plane Size Register */
#define	 DELTA_Y_WB_S		16
#define	 DELTA_X_WB_S		0
#define	DIU_WB_MEM_ADDR		0x030	/* Address to Store the write back Plane Register */
#define	DIU_HSYN_PARA		0x034	/* Horizontal Sync Parameter */
#define	 BP_H_SHIFT		22
#define	 PW_H_SHIFT		11
#define	 FP_H_SHIFT		0
#define	DIU_VSYN_PARA		0x038	/* Vertical Sync Parameter */
#define	 BP_V_SHIFT		22
#define	 PW_V_SHIFT		11
#define	 FP_V_SHIFT		0
#define	DIU_SYNPOL		0x03C	/* Synchronize Polarity */
#define	 BP_VS			(1 << 4)
#define	 BP_HS			(1 << 3)
#define	 INV_CS			(1 << 2)
#define	 INV_VS			(1 << 1)
#define	 INV_HS			(1 << 0)
#define	 INV_PDI_VS		(1 << 8) /* Polarity of PDI input VSYNC. */
#define	 INV_PDI_HS		(1 << 9) /* Polarity of PDI input HSYNC. */
#define	 INV_PDI_DE		(1 << 10) /* Polarity of PDI input DE. */
#define	DIU_THRESHOLD		0x040	/* Threshold */
#define	 LS_BF_VS_SHIFT		16
#define	 OUT_BUF_LOW_SHIFT	0
#define	DIU_INT_STATUS		0x044	/* Interrupt Status */
#define	DIU_INT_MASK		0x048	/* Interrupt Mask */
#define	DIU_COLBAR_1		0x04C	/* COLBAR_1 */
#define	 DIU_COLORBARn_R(x)	 ((x & 0xff) << 16)
#define	 DIU_COLORBARn_G(x)	 ((x & 0xff) << 8)
#define	 DIU_COLORBARn_B(x)	 ((x & 0xff) << 0)
#define	DIU_COLBAR_2		0x050	/* COLBAR_2 */
#define	DIU_COLBAR_3		0x054	/* COLBAR_3 */
#define	DIU_COLBAR_4		0x058	/* COLBAR_4 */
#define	DIU_COLBAR_5		0x05c	/* COLBAR_5 */
#define	DIU_COLBAR_6		0x060	/* COLBAR_6 */
#define	DIU_COLBAR_7		0x064	/* COLBAR_7 */
#define	DIU_COLBAR_8		0x068	/* COLBAR_8 */
#define	DIU_FILLING		0x06C	/* Filling Register */
#define	DIU_PLUT		0x070	/* Priority Look Up Table Register */

/* Control Descriptor */
#define DIU_CTRLDESCL(n, m)	0x200 + (0x40 * n) + 0x4 * (m - 1)
#define DIU_CTRLDESCLn_1(n)	DIU_CTRLDESCL(n, 1)
#define DIU_CTRLDESCLn_2(n)	DIU_CTRLDESCL(n, 2)
#define DIU_CTRLDESCLn_3(n)	DIU_CTRLDESCL(n, 3)
#define	 TRANS_SHIFT		20
#define DIU_CTRLDESCLn_4(n)	DIU_CTRLDESCL(n, 4)
#define	 BPP_MASK		0xf		/* Bit per pixel Mask */
#define	 BPP_SHIFT		16		/* Bit per pixel Shift */
#define	 BPP24			0x5
#define	 EN_LAYER		(1 << 31)	/* Enable the layer */
#define DIU_CTRLDESCLn_5(n)	DIU_CTRLDESCL(n, 5)
#define DIU_CTRLDESCLn_6(n)	DIU_CTRLDESCL(n, 6)
#define DIU_CTRLDESCLn_7(n)	DIU_CTRLDESCL(n, 7)
#define DIU_CTRLDESCLn_8(n)	DIU_CTRLDESCL(n, 8)
#define DIU_CTRLDESCLn_9(n)	DIU_CTRLDESCL(n, 9)

#define	NUM_LAYERS	1

struct panel_info {
	uint32_t	panel_width;
	uint32_t	panel_height;
	uint32_t	panel_hbp;
	uint32_t	panel_hpw;
	uint32_t	panel_hfp;
	uint32_t	panel_vbp;
	uint32_t	panel_vpw;
	uint32_t	panel_vfp;
	uint32_t	panel_freq;
	uint32_t	clk_div;
};

struct diu_area_descriptor {
	uint32_t	pixel_format;
	uint32_t	bitmap_address;
	uint32_t	source_size;
	uint32_t	aoi_size;
	uint32_t	aoi_offset;
	uint32_t	display_offset;
	uint32_t	chroma_key_max;
	uint32_t	chroma_key_min;
	uint32_t	next_ad_addr;
} __aligned(32);

struct diu_softc {
	struct resource		*res[2];
	void			*ih;
	device_t		sc_dev;
	device_t		sc_fbd;		/* fbd child */
	struct fb_info		sc_info;
	struct panel_info	sc_panel;
	struct diu_area_descriptor *sc_planes[3];
	uint8_t			*sc_gamma;
	uint8_t			*sc_cursor;
};

static struct resource_spec diu_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
diu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,diu"))
		return (ENXIO);

	device_set_desc(dev, "Freescale Display Interface Unit");
	return (BUS_PROBE_DEFAULT);
}

static void
diu_intr(void *arg)
{
	struct diu_softc *sc;
	int reg;

	sc = arg;

	/* Ack interrupts */
	reg = bus_read_4(sc->res[0], DIU_INT_STATUS);
	bus_write_4(sc->res[0], DIU_INT_STATUS, reg);

	/* TODO interrupt handler */
}

static int
diu_set_pxclk(device_t dev, unsigned int freq)
{
	phandle_t node;
	unsigned long bus_freq;
	uint32_t pxclk_set;
	uint32_t clkdvd;

	node = ofw_bus_get_node(device_get_parent(dev));
	if ((bus_freq = mpc85xx_get_platform_clock()) <= 0) {
		device_printf(dev, "Unable to get bus frequency\n");
		return (ENXIO);
	}

	/* freq is in kHz */
	freq *= 1000;
	/* adding freq/2 to round-to-closest */
	pxclk_set = min(max((bus_freq + freq/2) / freq, 2), 255) << 16;
	pxclk_set |= OCP85XX_CLKDVDR_PXCKEN;
	clkdvd = ccsr_read4(OCP85XX_CLKDVDR);
	clkdvd &= ~(OCP85XX_CLKDVDR_PXCKEN | OCP85XX_CLKDVDR_PXCKINV |
		OCP85XX_CLKDVDR_PXCLK_MASK);
	ccsr_write4(OCP85XX_CLKDVDR, clkdvd);
	ccsr_write4(OCP85XX_CLKDVDR, clkdvd | pxclk_set);

	return (0);
}

static int
diu_init(struct diu_softc *sc)
{
	struct panel_info *panel;
	int reg;

	panel = &sc->sc_panel;

	/* Temporarily disable the DIU while configuring */
	reg = bus_read_4(sc->res[0], DIU_DIU_MODE);
	reg &= ~(DIU_MODE_M << DIU_MODE_S);
	bus_write_4(sc->res[0], DIU_DIU_MODE, reg);

	if (diu_set_pxclk(sc->sc_dev, panel->panel_freq) < 0) {
		return (ENXIO);
	}

	/* Configure DIU */
	/* Need to set these somehow later... */
	bus_write_4(sc->res[0], DIU_GAMMA, vtophys(sc->sc_gamma));
	bus_write_4(sc->res[0], DIU_CURSOR, vtophys(sc->sc_cursor));
	bus_write_4(sc->res[0], DIU_CURS_POS, 0);

	reg = ((sc->sc_info.fb_height) << DELTA_Y_S);
	reg |= sc->sc_info.fb_width;
	bus_write_4(sc->res[0], DIU_DISP_SIZE, reg);

	reg = (panel->panel_hbp << BP_H_SHIFT);
	reg |= (panel->panel_hpw << PW_H_SHIFT);
	reg |= (panel->panel_hfp << FP_H_SHIFT);
	bus_write_4(sc->res[0], DIU_HSYN_PARA, reg);

	reg = (panel->panel_vbp << BP_V_SHIFT);
	reg |= (panel->panel_vpw << PW_V_SHIFT);
	reg |= (panel->panel_vfp << FP_V_SHIFT);
	bus_write_4(sc->res[0], DIU_VSYN_PARA, reg);

	bus_write_4(sc->res[0], DIU_BGND, 0);

	/* Mask all the interrupts */
	bus_write_4(sc->res[0], DIU_INT_MASK, 0x3f);

	/* Reset all layers */
	sc->sc_planes[0] = contigmalloc(sizeof(struct diu_area_descriptor),
		M_DEVBUF, M_ZERO, 0, BUS_SPACE_MAXADDR_32BIT, 32, 0);
	bus_write_4(sc->res[0], DIU_DESC_1, vtophys(sc->sc_planes[0]));
	bus_write_4(sc->res[0], DIU_DESC_2, 0);
	bus_write_4(sc->res[0], DIU_DESC_3, 0);

	/* Setup first plane */
	/* Area descriptor fields are little endian, so byte swap. */
	/* Word 0: Pixel format */
	/* Set to 8:8:8:8 ARGB, 4 bytes per pixel, no flip. */
#define MAKE_PXLFMT(as,rs,gs,bs,a,r,g,b,f,s)	\
		htole32((as << (4 * a)) | (rs << 4 * r) | \
		    (gs << 4 * g) | (bs << 4 * b) | \
		    (f << 28) | (s << 16) | \
		    (a << 25) | (r << 19) | \
		    (g << 21) | (b << 24))
	reg = MAKE_PXLFMT(8, 8, 8, 8, 3, 2, 1, 0, 1, 3);
	sc->sc_planes[0]->pixel_format = reg;
	/* Word 1: Bitmap address */
	sc->sc_planes[0]->bitmap_address = htole32(sc->sc_info.fb_pbase);
	/* Word 2: Source size/global alpha */
	reg = (sc->sc_info.fb_width | (sc->sc_info.fb_height << 12));
	sc->sc_planes[0]->source_size = htole32(reg);
	/* Word 3: AOI Size */
	reg = (sc->sc_info.fb_width | (sc->sc_info.fb_height << 16));
	sc->sc_planes[0]->aoi_size = htole32(reg);
	/* Word 4: AOI Offset */
	sc->sc_planes[0]->aoi_offset = 0;
	/* Word 5: Display offset */
	sc->sc_planes[0]->display_offset = 0;
	/* Word 6: Chroma key max */
	sc->sc_planes[0]->chroma_key_max = 0;
	/* Word 7: Chroma key min */
	reg = 255 << 16 | 255 << 8 | 255;
	sc->sc_planes[0]->chroma_key_min = htole32(reg);
	/* Word 8: Next AD */
	sc->sc_planes[0]->next_ad_addr = 0;

	/* TODO: derive this from the panel size */
	bus_write_4(sc->res[0], DIU_PLUT, 0x1f5f666);

	/* Enable DIU in normal mode */
	reg = bus_read_4(sc->res[0], DIU_DIU_MODE);
	reg &= ~(DIU_MODE_M << DIU_MODE_S);
	reg |= (DIU_MODE_NORMAL << DIU_MODE_S);
	bus_write_4(sc->res[0], DIU_DIU_MODE, reg);

	return (0);
}

static int
diu_attach(device_t dev)
{
	struct edid_info edid;
	struct diu_softc *sc;
	const struct videomode *videomode;
	void *edid_cells;
	const char *vm_name;
	phandle_t node;
	int h, r, w;
	int err, i;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	if (bus_alloc_resources(dev, diu_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);
	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[1], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, diu_intr, sc, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	/* TODO: Eventually, allow EDID to be dynamically provided. */
	if (OF_getprop_alloc(node, "edid", &edid_cells) <= 0) {
		/* Get a resource hint: hint.fb.N.mode */
		if (resource_string_value(device_get_name(dev),
		    device_get_unit(dev), "mode", &vm_name) != 0) {
			device_printf(dev,
			    "No EDID data and no video-mode env set\n");
			return (ENXIO);
		}
	}
	if (edid_cells != NULL) {
		if (edid_parse(edid_cells, &edid) != 0) {
			device_printf(dev, "Error parsing EDID\n");
			OF_prop_free(edid_cells);
			return (ENXIO);
		}
		videomode = edid.edid_preferred_mode;
	} else {
		/* Parse video-mode kenv variable. */
		if ((err = sscanf(vm_name, "%dx%d@%d", &w, &h, &r)) != 3) {
			device_printf(dev,
			    "Cannot parse video mode: %s\n", vm_name);
			return (ENXIO);
		}
		videomode = pick_mode_by_ref(w, h, r);
		if (videomode == NULL) {
			device_printf(dev,
			    "Cannot find mode for %dx%d@%d", w, h, r);
			return (ENXIO);
		}
	}

	sc->sc_panel.panel_width = videomode->hdisplay;
	sc->sc_panel.panel_height = videomode->vdisplay;
	sc->sc_panel.panel_hbp = videomode->hsync_start - videomode->hdisplay;
	sc->sc_panel.panel_hfp = videomode->htotal - videomode->hsync_end;
	sc->sc_panel.panel_hpw = videomode->hsync_end - videomode->hsync_start;
	sc->sc_panel.panel_vbp = videomode->vsync_start - videomode->vdisplay;
	sc->sc_panel.panel_vfp = videomode->vtotal - videomode->vsync_end;
	sc->sc_panel.panel_vpw = videomode->vsync_end - videomode->vsync_start;
	sc->sc_panel.panel_freq = videomode->dot_clock;

	sc->sc_info.fb_width = sc->sc_panel.panel_width;
	sc->sc_info.fb_height = sc->sc_panel.panel_height;
	sc->sc_info.fb_stride = sc->sc_info.fb_width * 4;
	sc->sc_info.fb_bpp = sc->sc_info.fb_depth = 32;
	sc->sc_info.fb_size = sc->sc_info.fb_height * sc->sc_info.fb_stride;
	sc->sc_info.fb_vbase = (intptr_t)contigmalloc(sc->sc_info.fb_size,
	    M_DEVBUF, M_ZERO, 0, BUS_SPACE_MAXADDR_32BIT, PAGE_SIZE, 0);
	sc->sc_info.fb_pbase = (intptr_t)vtophys(sc->sc_info.fb_vbase);
	sc->sc_info.fb_flags = FB_FLAG_MEMATTR;
	sc->sc_info.fb_memattr = VM_MEMATTR_DEFAULT;
	
	/* Gamma table is 3 consecutive segments of 256 bytes. */
	sc->sc_gamma = contigmalloc(3 * 256, M_DEVBUF, 0, 0,
	    BUS_SPACE_MAXADDR_32BIT, PAGE_SIZE, 0);
	/* Initialize gamma to default */
	for (i = 0; i < 3 * 256; i++)
		sc->sc_gamma[i] = (i % 256);

	/* Cursor format is 32x32x16bpp */
	sc->sc_cursor = contigmalloc(32 * 32 * 2, M_DEVBUF, M_ZERO, 0,
	    BUS_SPACE_MAXADDR_32BIT, PAGE_SIZE, 0);

	diu_init(sc);

	sc->sc_info.fb_name = device_get_nameunit(dev);

	/* Ask newbus to attach framebuffer device to me. */
	sc->sc_fbd = device_add_child(dev, "fbd", device_get_unit(dev));
	if (sc->sc_fbd == NULL)
		device_printf(dev, "Can't attach fbd device\n");

	if ((err = device_probe_and_attach(sc->sc_fbd)) != 0) {
		device_printf(dev, "Failed to attach fbd device: %d\n", err);
	}

	return (0);
}

static struct fb_info *
diu_fb_getinfo(device_t dev)
{
	struct diu_softc *sc = device_get_softc(dev);

	return (&sc->sc_info);
}

static device_method_t diu_methods[] = {
	DEVMETHOD(device_probe,		diu_probe),
	DEVMETHOD(device_attach,	diu_attach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		diu_fb_getinfo),
	{ 0, 0 }
};

static driver_t diu_driver = {
	"fb",
	diu_methods,
	sizeof(struct diu_softc),
};

static devclass_t diu_devclass;

DRIVER_MODULE(fb, simplebus, diu_driver, diu_devclass, 0, 0);
