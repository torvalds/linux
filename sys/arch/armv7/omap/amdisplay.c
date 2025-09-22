/* $OpenBSD: amdisplay.c,v 1.20 2024/11/06 07:09:45 miod Exp $ */
/*
 * Copyright (c) 2016 Ian Sutton <ians@openbsd.org>
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

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>
#include <machine/fdt.h>

#include <armv7/omap/prcmvar.h>
#include <armv7/omap/amdisplayreg.h>
#include <armv7/omap/nxphdmivar.h>

#ifdef LCD_DEBUG
#define str(X) #X
int lcd_dbg_thresh = 20;
#define DPRINTF(n,s)	do { if ((n) <= lcd_dbg_thresh) printf s; } while (0)
#else
#define DPRINTF(n,s)	do {} while (0)
#endif

#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

#define LCD_MAX_PELCLK	170000		/* kHz */
#define LCD_MASTER_OSC	24000000	/* Hz */
#define LCD_M1_MAX	2048
#define LCD_M2_MAX	31
#define LCD_N_MAX	128

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amdisplay_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	void			*sc_ih;

	int			sc_flags;
#define LCD_RESET_PENDING	(1 << 0)
#define LCD_MODE_COMPAT		(1 << 1)
#define LCD_MODE_ALLOC		(1 << 2)

	struct edid_info	sc_edid;
	struct videomode	*sc_active_mode;
	int			sc_active_depth;

	bus_dmamap_t		sc_fb0_dma;
	bus_dma_segment_t	sc_fb0_dma_segs[1];
	void			*sc_fb0;
	int			sc_fb_dma_nsegs;
	bus_size_t		sc_fb_size;

	struct rasops_info	sc_ro;
	struct wsscreen_descr	sc_wsd;
	struct wsscreen_list	sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
};

int	amdisplay_match(struct device *, void *, void *);
void	amdisplay_attach(struct device *, struct device *, void *);
int	amdisplay_detach(struct device *, int);
int	amdisplay_intr(void *);

int	amdisplay_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	amdisplay_mmap(void *, off_t, int);
int	amdisplay_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);

int	amdisplay_setup_dma(struct amdisplay_softc *);
void	amdisplay_conf_crt_timings(struct amdisplay_softc *);
void	amdisplay_calc_freq(uint64_t);

struct wsdisplay_accessops amdisplay_accessops = {
	.ioctl = amdisplay_ioctl,
	.mmap = amdisplay_mmap,
	.alloc_screen = amdisplay_alloc_screen,
	.free_screen = rasops_free_screen,
	.show_screen = rasops_show_screen,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
};

const struct cfattach amdisplay_ca = {
	sizeof(struct amdisplay_softc), amdisplay_match, amdisplay_attach,
	amdisplay_detach
};

struct cfdriver amdisplay_cd = {
	NULL, "amdisplay", DV_DULL
};

#ifdef LCD_DEBUG
void
preg(uint32_t reg, char *rn, struct amdisplay_softc *sc)
{
	uint32_t read;

	read = HREAD4(sc, reg);
	DPRINTF(16, ("%s: %s: 0x%08x\n", DEVNAME(sc), rn, read));
}

void
dumpregs(struct amdisplay_softc *sc)
{
	preg(LCD_PID, str(AMDISPLAY_PID), sc);
	preg(LCD_CTRL, str(AMDISPLAY_CTRL), sc);
	preg(LCD_RASTER_CTRL, str(AMDISPLAY_RASTER_CTRL), sc);
	preg(LCD_RASTER_TIMING_0, str(AMDISPLAY_RASTER_TIMING_0), sc);
	preg(LCD_RASTER_TIMING_1, str(AMDISPLAY_RASTER_TIMING_1), sc);
	preg(LCD_RASTER_TIMING_2, str(AMDISPLAY_RASTER_TIMING_2), sc);
	preg(LCD_RASTER_SUBPANEL, str(AMDISPLAY_RASTER_SUBPANEL), sc);
	preg(LCD_RASTER_SUBPANEL_2, str(AMDISPLAY_RASTER_SUBPANEL_2), sc);
	preg(LCD_LCDDMA_CTRL, str(AMDISPLAY_LCDDMA_CTRL), sc);

	/* accessing these regs is liable to occur during CPU lockout period */
#if 0
	preg(LCD_LCDDMA_FB0_BASE, str(AMDISPLAY_LCDDMA_FB0_BASE), sc);
	preg(LCD_LCDDMA_FB0_CEILING, str(AMDISPLAY_LCDDMA_FB0_CEILING), sc);
	preg(LCD_LCDDMA_FB1_BASE, str(AMDISPLAY_LCDDMA_FB1_BASE), sc);
	preg(LCD_LCDDMA_FB1_CEILING, str(AMDISPLAY_LCDDMA_FB1_CEILING), sc);
#endif

	preg(LCD_SYSCONFIG, str(AMDISPLAY_SYSCONFIG), sc);
	preg(LCD_IRQSTATUS_RAW, str(AMDISPLAY_IRQSTATUS_RAW), sc);
	preg(LCD_IRQSTATUS, str(AMDISPLAY_IRQSTATUS), sc);
	preg(LCD_IRQENABLE_SET, str(AMDISPLAY_IRQENABLE_SET), sc);
	preg(LCD_IRQENABLE_CLEAR, str(AMDISPLAY_IRQENABLE_CLEAR), sc);
	preg(LCD_CLKC_ENABLE, str(AMDISPLAY_CLKC_ENABLE), sc);
	preg(LCD_CLKC_RESET, str(AMDISPLAY_CLKC_RESET), sc);
}
#endif

int
amdisplay_match(struct device *parent, void *v, void *aux)
{
	struct fdt_attach_args *faa = aux;
	return OF_is_compatible(faa->fa_node, "ti,am33xx-tilcdc");
}

void
amdisplay_attach(struct device *parent, struct device *self, void *args)
{
	struct amdisplay_softc	*sc = (struct amdisplay_softc *) self;
	struct fdt_attach_args	*faa = args;
	struct wsemuldisplaydev_attach_args wsaa;
	uint64_t pel_clk = 0;
	uint32_t reg;
	uint8_t *edid_buf;
	int stride, i = 0;

	sc->sc_iot  = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/* enable clock module */
	prcm_enablemodule(PRCM_LCDC);

	/* force ourselves out of standby/idle states */
	reg = HREAD4(sc, LCD_SYSCONFIG);
	reg &= ~(LCD_SYSCONFIG_STANDBYMODE | LCD_SYSCONFIG_IDLEMODE);
	reg |= (0x2 << LCD_SYSCONFIG_STANDBYMODE_SHAMT)
	    |  (0x2 << LCD_SYSCONFIG_IDLEMODE_SHAMT);
	HWRITE4(sc, LCD_SYSCONFIG, reg);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_BIO,
	    amdisplay_intr, sc, DEVNAME(sc));

	printf("\n");

	/* read/parse EDID bits from TDA19988 HDMI PHY */
	edid_buf = malloc(EDID_LENGTH, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_active_mode = malloc(sizeof(struct videomode), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	sc->sc_flags |= LCD_MODE_ALLOC;

	if (nxphdmi_get_edid(edid_buf, EDID_LENGTH) ||
	    edid_parse(DEVNAME(sc), edid_buf, &sc->sc_edid)) {
		printf("%s: no display attached.\n", DEVNAME(sc));
		free(edid_buf, M_DEVBUF, EDID_LENGTH);
		amdisplay_detach(self, 0);
		return;
	}

	free(edid_buf, M_DEVBUF, EDID_LENGTH);

#if defined(LCD_DEBUG) && defined(EDID_DEBUG)
	edid_print(&sc->sc_edid);
#endif

	/* determine max supported resolution our clock signal can handle */
	for (; i < sc->sc_edid.edid_nmodes - 1; i++) {
		if (sc->sc_edid.edid_modes[i].dot_clock < LCD_MAX_PELCLK &&
		    sc->sc_edid.edid_modes[i].dot_clock > pel_clk) {
			pel_clk = sc->sc_edid.edid_modes[i].dot_clock;
			memcpy(sc->sc_active_mode, &sc->sc_edid.edid_modes[i],
			    sizeof(struct videomode));
		}
	}

	i = 0;
	printf("%s: %s :: %d kHz pclk\n", DEVNAME(sc),
	    sc->sc_active_mode->name, sc->sc_active_mode->dot_clock);

	pel_clk *= 2000;
	amdisplay_calc_freq(pel_clk);

	sc->sc_active_mode->flags |= VID_HSKEW;
	sc->sc_active_depth = 16;

	/* configure DMA framebuffer */
	if (amdisplay_setup_dma(sc)) {
		printf("%s: couldn't allocate DMA framebuffer\n", DEVNAME(sc));
		amdisplay_detach(self, 0);
		return;
	}

	/* setup rasops */
	stride = sc->sc_active_mode->hdisplay * sc->sc_active_depth / 8;

	sc->sc_ro.ri_depth = sc->sc_active_depth;
	sc->sc_ro.ri_width = sc->sc_active_mode->hdisplay;
	sc->sc_ro.ri_height = sc->sc_active_mode->vdisplay;
	sc->sc_ro.ri_stride = stride;
	sc->sc_ro.ri_bits = sc->sc_fb0;

	sc->sc_ro.ri_rpos = 0;
	sc->sc_ro.ri_rnum = 5;
	sc->sc_ro.ri_gpos = 5;
	sc->sc_ro.ri_gnum = 6;
	sc->sc_ro.ri_bpos = 11;
	sc->sc_ro.ri_bnum = 5;
	sc->sc_ro.ri_hw = sc;
	sc->sc_ro.ri_flg = RI_CENTER | RI_VCONS | RI_WRONLY |
	    RI_CLEAR | RI_FULLCLEAR;

	if (rasops_init(&sc->sc_ro, 200, 200)) {
		printf("%s: no rasops\n", DEVNAME(sc));
		amdisplay_detach(self, 0);
		return;
	}

	/* ensure controller is off */
	HCLR4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_LCDEN);
	delay(100);

	/* set clock divisor needed for 640x480 VGA timings */
	reg = HREAD4(sc, LCD_CTRL);
	reg &= ~LCD_CTRL_CLKDIV;
	reg |= (0x2 << LCD_CTRL_CLKDIV_SHAMT);

	/* select raster mode & reset-on-underflow, write */
	reg |= LCD_CTRL_MODESEL;
	HWRITE4(sc, LCD_CTRL, reg);

	/* set stn565 + active matrix + palette loading only mode, delay */
	reg = HREAD4(sc, LCD_RASTER_CTRL);
	reg &= 0xF8000C7C;
	reg |= (LCD_RASTER_CTRL_LCDTFT)
	    |  (0x02  << LCD_RASTER_CTRL_PALMODE_SHAMT)
	    |  (0xFF << LCD_RASTER_CTRL_REQDLY_SHAMT);
	HWRITE4(sc, LCD_RASTER_CTRL, reg);

	/* set timing values */
	amdisplay_conf_crt_timings(sc);

	/* configure HDMI transmitter (TDA19988) with our mode details */
	nxphdmi_set_videomode(sc->sc_active_mode);

	/* latch pins/pads according to fdt node */
	pinctrl_byphandle(LCD_FDT_PHANDLE);

	/* configure DMA transfer settings */
	reg = HREAD4(sc, LCD_LCDDMA_CTRL);
	reg &= ~(LCD_LCDDMA_CTRL_DMA_MASTER_PRIO
	    | LCD_LCDDMA_CTRL_TH_FIFO_READY
	    | LCD_LCDDMA_CTRL_BURST_SIZE
	    | LCD_LCDDMA_CTRL_BYTE_SWAP
	    | LCD_LCDDMA_CTRL_BIGENDIAN
	    | LCD_LCDDMA_CTRL_FRAME_MODE);
	reg |= (0x4 << LCD_LCDDMA_CTRL_BURST_SIZE_SHAMT)
	    |  LCD_LCDDMA_CTRL_FRAME_MODE;
	HWRITE4(sc, LCD_LCDDMA_CTRL, reg);

	/* set framebuffer location + bounds */
	HWRITE4(sc, LCD_LCDDMA_FB0, sc->sc_fb0_dma_segs[0].ds_addr);
	HWRITE4(sc, LCD_LCDDMA_FB0_CEIL, (sc->sc_fb0_dma_segs[0].ds_addr
	    + sc->sc_fb_size));
	HWRITE4(sc, LCD_LCDDMA_FB1, sc->sc_fb0_dma_segs[0].ds_addr);
	HWRITE4(sc, LCD_LCDDMA_FB1_CEIL, (sc->sc_fb0_dma_segs[0].ds_addr
	    + sc->sc_fb_size));

	/* enable all intrs. */
	reg = 0;
	reg |= (LCD_IRQ_EOF1 | LCD_IRQ_EOF0 | LCD_IRQ_PL | LCD_IRQ_FUF |
	        LCD_IRQ_ACB | LCD_IRQ_SYNC | LCD_IRQ_RR_DONE | LCD_IRQ_DONE);

	HWRITE4(sc, LCD_IRQENABLE_SET, reg);

	/* enable dma & core clocks */
	HSET4(sc, LCD_CLKC_ENABLE, LCD_CLKC_ENABLE_DMA_CLK_EN
	    | LCD_CLKC_ENABLE_CORE_CLK_EN
	    | LCD_CLKC_ENABLE_LIDD_CLK_EN);

	/* perform controller clock reset */
	HSET4(sc, LCD_CLKC_RESET, LCD_CLKC_RESET_MAIN_RST);
	delay(100);
	HCLR4(sc, LCD_CLKC_RESET, LCD_CLKC_RESET_MAIN_RST);

	/* configure wsdisplay descr. */
	strlcpy(sc->sc_wsd.name, "std", sizeof(sc->sc_wsd.name));
	sc->sc_wsd.capabilities = sc->sc_ro.ri_caps;
	sc->sc_wsd.nrows = sc->sc_ro.ri_rows;
	sc->sc_wsd.ncols = sc->sc_ro.ri_cols;
	sc->sc_wsd.textops = &sc->sc_ro.ri_ops;
	sc->sc_wsd.fontwidth = sc->sc_ro.ri_font->fontwidth;
	sc->sc_wsd.fontheight = sc->sc_ro.ri_font->fontheight;

	sc->sc_scrlist[0] = &sc->sc_wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	/* attach console */
	memset(&wsaa, 0, sizeof(wsaa));
	wsaa.scrdata = &sc->sc_wsl;
	wsaa.accessops = &amdisplay_accessops;
	wsaa.accesscookie = &sc->sc_ro;

	config_found_sm(self, &wsaa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);

	/* enable controller */
	HSET4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_LCDEN);
}

int
amdisplay_detach(struct device *self, int flags)
{
	struct amdisplay_softc *sc = (struct amdisplay_softc *)self;

	if (ISSET(sc->sc_flags, LCD_MODE_ALLOC))
		free(sc->sc_active_mode, M_DEVBUF, sizeof(struct videomode));

	if (!sc->sc_fb0)
		return 0;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_fb0_dma, 0, sc->sc_fb_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->sc_dmat, sc->sc_fb0_dma);
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)(sc->sc_fb0), sc->sc_fb_size);
	bus_dmamem_free(sc->sc_dmat, sc->sc_fb0_dma_segs, sc->sc_fb_dma_nsegs);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_fb0_dma);

	return 0;
}

int
amdisplay_intr(void *arg)
{
	struct amdisplay_softc *sc = arg;
	uint32_t reg;

	reg = HREAD4(sc, LCD_IRQSTATUS);
	HWRITE4(sc, LCD_IRQSTATUS, reg);

	DPRINTF(25, ("%s: intr 0x%08x\n", DEVNAME(sc), reg));

	if (ISSET(reg, LCD_IRQ_PL)) {
		DPRINTF(10, ("%s: palette loaded, irq: 0x%08x\n",
		    DEVNAME(sc), reg));
		HCLR4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_LCDEN);
		delay(100);
		HCLR4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_PALMODE);
		HSET4(sc, LCD_RASTER_CTRL, 0x02 << LCD_RASTER_CTRL_PALMODE_SHAMT);
		HSET4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_LCDEN);
	}

	if (ISSET(reg, LCD_IRQ_FUF)) {
		DPRINTF(15, ("%s: FIFO underflow\n", DEVNAME(sc)));
	}

	if (ISSET(reg, LCD_IRQ_SYNC)) {
		sc->sc_flags |= LCD_RESET_PENDING;
		DPRINTF(18, ("%s: sync lost\n", DEVNAME(sc)));
	}

	if (ISSET(reg, LCD_IRQ_RR_DONE)) {
		DPRINTF(21, ("%s: frame done\n", DEVNAME(sc)));
		HWRITE4(sc, LCD_LCDDMA_FB0, sc->sc_fb0_dma_segs[0].ds_addr);
		HWRITE4(sc, LCD_LCDDMA_FB0_CEIL, (sc->sc_fb0_dma_segs[0].ds_addr
		    + sc->sc_fb_size) - 1);
	}

	if (ISSET(reg, LCD_IRQ_EOF0)) {
		DPRINTF(21, ("%s: framebuffer 0 done\n", DEVNAME(sc)));
	}

	if (ISSET(reg, LCD_IRQ_EOF1)) {
		DPRINTF(21, ("%s: framebuffer 1 done\n", DEVNAME(sc)));
	}

	if (ISSET(reg, LCD_IRQ_DONE)) {
		if (ISSET(sc->sc_flags, LCD_RESET_PENDING)) {
			HWRITE4(sc, LCD_IRQSTATUS, 0xFFFFFFFF);
			HSET4(sc, LCD_CLKC_RESET, LCD_CLKC_RESET_MAIN_RST);
			delay(10);
			HCLR4(sc, LCD_CLKC_RESET, LCD_CLKC_RESET_MAIN_RST);
			HSET4(sc, LCD_RASTER_CTRL, LCD_RASTER_CTRL_LCDEN);
			sc->sc_flags &= ~LCD_RESET_PENDING;
		}
		DPRINTF(15, ("%s: last frame done\n", DEVNAME(sc)));
	}

	if (ISSET(reg, LCD_IRQ_ACB)) {
		DPRINTF(15, ("%s: AC bias event\n", DEVNAME(sc)));
	}

	HWRITE4(sc, LCD_IRQ_END, 0);

	return 0;
}

int
amdisplay_setup_dma(struct amdisplay_softc *sc)
{
	bus_size_t bsize;

	bsize = (sc->sc_active_mode->hdisplay * sc->sc_active_mode->vdisplay
	    * sc->sc_active_depth) >> 3;

	sc->sc_fb_size = bsize;
	sc->sc_fb_dma_nsegs = 1;

	if (bus_dmamap_create(sc->sc_dmat, sc->sc_fb_size, sc->sc_fb_dma_nsegs,
	    sc->sc_fb_size, 0, BUS_DMA_NOWAIT, &(sc->sc_fb0_dma)))
		return -1;

	if (bus_dmamem_alloc(sc->sc_dmat, sc->sc_fb_size, 4, 0,
	    sc->sc_fb0_dma_segs, 1, &(sc->sc_fb_dma_nsegs),
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
		return -2;

	if (bus_dmamem_map(sc->sc_dmat, sc->sc_fb0_dma_segs,
	    sc->sc_fb_dma_nsegs, sc->sc_fb_size, (caddr_t *)&(sc->sc_fb0),
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT | BUS_DMA_NOCACHE))
		return -3;

	if (bus_dmamap_load(sc->sc_dmat, sc->sc_fb0_dma, sc->sc_fb0, bsize,
	    NULL, BUS_DMA_NOWAIT))
		return -4;

	memset(sc->sc_fb0, 0, bsize);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_fb0_dma, 0, bsize,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return 0;
}

void
amdisplay_conf_crt_timings(struct amdisplay_softc *sc)
{
	uint32_t timing0, timing1, timing2;
	uint32_t hbp, hfp, hsw, vbp, vfp, vsw, width, height;
	struct videomode *m = sc->sc_active_mode;

	timing0 = 0;
	timing1 = 0;
	timing2 = 0;

	hbp = (m->htotal - m->hsync_end) - 1;
	hfp = (m->hsync_start - m->hdisplay) - 1;
	hsw = (m->hsync_end - m->hsync_start) - 1;

	vbp = (m->vtotal - m->vsync_end);
	vfp = (m->vsync_start - m->vdisplay);
	vsw = (m->vsync_end - m->vsync_start) - 1;

	height = m->vdisplay - 1;
	width = m->hdisplay - 1;

	/* Horizontal back porch */
	timing0 |= (hbp & 0xff) << LCD_RASTER_TIMING_0_HBP_SHAMT;
	timing2 |= ((hbp >> 8) & 3) << LCD_RASTER_TIMING_2_HPB_HIGHBITS_SHAMT;
	/* Horizontal front porch */
	timing0 |= (hfp & 0xff) << LCD_RASTER_TIMING_0_HFP_SHAMT;
	timing2 |= ((hfp >> 8) & 3) << 0;
	/* Horizontal sync width */
	timing0 |= (hsw & 0x3f) << LCD_RASTER_TIMING_0_HSW_SHAMT;
	timing2 |= ((hsw >> 6) & 0xf) << LCD_RASTER_TIMING_2_HSW_HIGHBITS_SHAMT;

	/* Vertical back porch, front porch, sync width */
	timing1 |= (vbp & 0xff) << LCD_RASTER_TIMING_1_VBP_SHAMT;
	timing1 |= (vfp & 0xff) << LCD_RASTER_TIMING_1_VFP_SHAMT;
	timing1 |= (vsw & 0x3f) << LCD_RASTER_TIMING_1_VSW_SHAMT;

	/* Pixels per line */
	timing0 |= ((width >> 10) & 1)
	    << LCD_RASTER_TIMING_0_PPLMSB_SHAMT;
	timing0 |= ((width >> 4) & 0x3f)
	    << LCD_RASTER_TIMING_0_PPLLSB_SHAMT;

	/* Lines per panel */
	timing1 |= (height & 0x3ff);
	timing2 |= ((height >> 10 ) & 1)
	    << LCD_RASTER_TIMING_2_LPP_B10_SHAMT;

	/* waveform settings */
	timing2 |= LCD_RASTER_TIMING_2_PHSVS_ON_OFF;
	timing2 |= (0xff << LCD_RASTER_TIMING_2_ACBI_SHAMT);

	if (!ISSET(m->flags, VID_NHSYNC))
		timing2 |= LCD_RASTER_TIMING_2_IHS;
	if (!ISSET(m->flags, VID_NVSYNC))
		timing2 |= LCD_RASTER_TIMING_2_IVS;

	HWRITE4(sc, LCD_RASTER_TIMING_0, timing0);
	HWRITE4(sc, LCD_RASTER_TIMING_1, timing1);
	HWRITE4(sc, LCD_RASTER_TIMING_2, timing2);
}

void
amdisplay_calc_freq(uint64_t freq)
{
	uint64_t mul, div, i, j, delta, min_delta;

	min_delta = freq;
	for (i = 1; i < LCD_M1_MAX; i++) {
		for (j = 1; j < LCD_N_MAX; j++) {
			delta = abs(freq - i * (LCD_MASTER_OSC / j));
			if (delta < min_delta) {
				mul = i;
				div = j;
				min_delta = delta;
			}
			if (min_delta == 0)
				break;
		}
	}
	div--;

	prcm_setclock(4, div);
	prcm_setclock(3, mul);
	prcm_setclock(5, 1);
}

int
amdisplay_ioctl(void *sconf, u_long cmd, caddr_t data, int flat, struct proc *p)
{
	struct rasops_info *ri = sconf;
	struct wsdisplay_fbinfo	*wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 0;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_SMODE:
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		switch (ri->ri_depth) {
		case 32:
			*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
			break;
		case 24:
			*(u_int *)data = WSDISPLAYIO_DEPTH_24_24;
			break;
		case 16:
			*(u_int *)data = WSDISPLAYIO_DEPTH_16;
			break;
		case 15:
			*(u_int *)data = WSDISPLAYIO_DEPTH_15;
			break;
		default:
			return -1;
		}
		break;
	default:
		return -1;
	}

	return 0;
}

paddr_t
amdisplay_mmap(void *sconf, off_t off, int prot)
{
	struct rasops_info *ri = sconf;
	struct amdisplay_softc *sc = ri->ri_hw;

	if (off < 0 || off >= sc->sc_fb_size)
		return -1;

	return bus_dmamem_mmap(sc->sc_dmat, &sc->sc_fb0_dma_segs[0],
	    sc->sc_fb_dma_nsegs, off, prot, BUS_DMA_COHERENT);
}

int
amdisplay_alloc_screen(void *sconf, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(sconf, cookiep, curxp, curyp, attrp);
}
