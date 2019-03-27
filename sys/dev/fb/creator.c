/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
 * Copyright (c) 2005 - 2006 Marius Strobl <marius@FreeBSD.org>
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
#include <sys/conf.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_private.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>
#include <machine/sc_machdep.h>

#include <sys/rman.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/creatorreg.h>
#include <dev/fb/gfb.h>
#include <dev/syscons/syscons.h>

#define	CREATOR_DRIVER_NAME	"creator"

struct creator_softc {
	video_adapter_t		sc_va;			/* XXX must be first */

	phandle_t		sc_node;

	struct cdev		*sc_si;

	struct resource		*sc_reg[FFB_NREG];
	bus_space_tag_t		sc_bt[FFB_NREG];
	bus_space_handle_t	sc_bh[FFB_NREG];
	u_long			sc_reg_size;

	u_int			sc_height;
	u_int			sc_width;

	u_int			sc_xmargin;
	u_int			sc_ymargin;

	const u_char		*sc_font;

	int			sc_bg_cache;
	int			sc_fg_cache;
	int			sc_fifo_cache;
	int			sc_fontinc_cache;
	int			sc_fontw_cache;
	int			sc_pmask_cache;

	u_int			sc_flags;
#define	CREATOR_AFB		(1 << 0)
#define	CREATOR_CONSOLE		(1 << 1)
#define	CREATOR_CUREN		(1 << 2)
#define	CREATOR_CURINV		(1 << 3)
#define	CREATOR_PAC1		(1 << 4)
};

#define	FFB_READ(sc, reg, off)						\
	bus_space_read_4((sc)->sc_bt[(reg)], (sc)->sc_bh[(reg)], (off))
#define	FFB_WRITE(sc, reg, off, val)					\
	bus_space_write_4((sc)->sc_bt[(reg)], (sc)->sc_bh[(reg)], (off), (val))

#define	C(r, g, b)	((b << 16) | (g << 8) | (r))
static const uint32_t creator_cmap[] = {
	C(0x00, 0x00, 0x00),		/* black */
	C(0x00, 0x00, 0xff),		/* blue */
	C(0x00, 0xff, 0x00),		/* green */
	C(0x00, 0xc0, 0xc0),		/* cyan */
	C(0xff, 0x00, 0x00),		/* red */
	C(0xc0, 0x00, 0xc0),		/* magenta */
	C(0xc0, 0xc0, 0x00),		/* brown */
	C(0xc0, 0xc0, 0xc0),		/* light grey */
	C(0x80, 0x80, 0x80),		/* dark grey */
	C(0x80, 0x80, 0xff),		/* light blue */
	C(0x80, 0xff, 0x80),		/* light green */
	C(0x80, 0xff, 0xff),		/* light cyan */
	C(0xff, 0x80, 0x80),		/* light red */
	C(0xff, 0x80, 0xff),		/* light magenta */
	C(0xff, 0xff, 0x80),		/* yellow */
	C(0xff, 0xff, 0xff),		/* white */
};
#undef C

static const struct {
	vm_offset_t virt;
	vm_paddr_t phys;
	vm_size_t size;
} creator_fb_map[] = {
	{ FFB_VIRT_SFB8R,	FFB_PHYS_SFB8R,		FFB_SIZE_SFB8R },
	{ FFB_VIRT_SFB8G,	FFB_PHYS_SFB8G,		FFB_SIZE_SFB8G },
	{ FFB_VIRT_SFB8B,	FFB_PHYS_SFB8B,		FFB_SIZE_SFB8B },
	{ FFB_VIRT_SFB8X,	FFB_PHYS_SFB8X,		FFB_SIZE_SFB8X },
	{ FFB_VIRT_SFB32,	FFB_PHYS_SFB32,		FFB_SIZE_SFB32 },
	{ FFB_VIRT_SFB64,	FFB_PHYS_SFB64,		FFB_SIZE_SFB64 },
	{ FFB_VIRT_FBC,		FFB_PHYS_FBC,		FFB_SIZE_FBC },
	{ FFB_VIRT_FBC_BM,	FFB_PHYS_FBC_BM,	FFB_SIZE_FBC_BM },
	{ FFB_VIRT_DFB8R,	FFB_PHYS_DFB8R,		FFB_SIZE_DFB8R },
	{ FFB_VIRT_DFB8G,	FFB_PHYS_DFB8G,		FFB_SIZE_DFB8G },
	{ FFB_VIRT_DFB8B,	FFB_PHYS_DFB8B,		FFB_SIZE_DFB8B },
	{ FFB_VIRT_DFB8X,	FFB_PHYS_DFB8X,		FFB_SIZE_DFB8X },
	{ FFB_VIRT_DFB24,	FFB_PHYS_DFB24,		FFB_SIZE_DFB24 },
	{ FFB_VIRT_DFB32,	FFB_PHYS_DFB32,		FFB_SIZE_DFB32 },
	{ FFB_VIRT_DFB422A,	FFB_PHYS_DFB422A,	FFB_SIZE_DFB422A },
	{ FFB_VIRT_DFB422AD,	FFB_PHYS_DFB422AD,	FFB_SIZE_DFB422AD },
	{ FFB_VIRT_DFB24B,	FFB_PHYS_DFB24B,	FFB_SIZE_DFB24B },
	{ FFB_VIRT_DFB422B,	FFB_PHYS_DFB422B,	FFB_SIZE_DFB422B },
	{ FFB_VIRT_DFB422BD,	FFB_PHYS_DFB422BD,	FFB_SIZE_DFB422BD },
	{ FFB_VIRT_SFB16Z,	FFB_PHYS_SFB16Z,	FFB_SIZE_SFB16Z },
	{ FFB_VIRT_SFB8Z,	FFB_PHYS_SFB8Z,		FFB_SIZE_SFB8Z },
	{ FFB_VIRT_SFB422,	FFB_PHYS_SFB422,	FFB_SIZE_SFB422 },
	{ FFB_VIRT_SFB422D,	FFB_PHYS_SFB422D,	FFB_SIZE_SFB422D },
	{ FFB_VIRT_FBC_KREG,	FFB_PHYS_FBC_KREG,	FFB_SIZE_FBC_KREG },
	{ FFB_VIRT_DAC,		FFB_PHYS_DAC,		FFB_SIZE_DAC },
	{ FFB_VIRT_PROM,	FFB_PHYS_PROM,		FFB_SIZE_PROM },
	{ FFB_VIRT_EXP,		FFB_PHYS_EXP,		FFB_SIZE_EXP },
};

#define	CREATOR_FB_MAP_SIZE	nitems(creator_fb_map)

extern const struct gfb_font gallant12x22;

static struct creator_softc creator_softc;
static struct bus_space_tag creator_bst_store[FFB_FBC];

static device_probe_t creator_bus_probe;
static device_attach_t creator_bus_attach;

static device_method_t creator_bus_methods[] = {
	DEVMETHOD(device_probe,		creator_bus_probe),
	DEVMETHOD(device_attach,	creator_bus_attach),

	{ 0, 0 }
};

static devclass_t creator_devclass;

DEFINE_CLASS_0(creator, creator_bus_driver, creator_bus_methods,
    sizeof(struct creator_softc));
DRIVER_MODULE(creator, nexus, creator_bus_driver, creator_devclass, 0, 0);
DRIVER_MODULE(creator, upa, creator_bus_driver, creator_devclass, 0, 0);

static d_open_t creator_fb_open;
static d_close_t creator_fb_close;
static d_ioctl_t creator_fb_ioctl;
static d_mmap_t creator_fb_mmap;

static struct cdevsw creator_fb_devsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	creator_fb_open,
	.d_close =	creator_fb_close,
	.d_ioctl =	creator_fb_ioctl,
	.d_mmap =	creator_fb_mmap,
	.d_name =	"fb",
};

static void creator_cursor_enable(struct creator_softc *sc, int onoff);
static void creator_cursor_install(struct creator_softc *sc);
static void creator_shutdown(void *xsc);

static int creator_configure(int flags);

static vi_probe_t creator_probe;
static vi_init_t creator_init;
static vi_get_info_t creator_get_info;
static vi_query_mode_t creator_query_mode;
static vi_set_mode_t creator_set_mode;
static vi_save_font_t creator_save_font;
static vi_load_font_t creator_load_font;
static vi_show_font_t creator_show_font;
static vi_save_palette_t creator_save_palette;
static vi_load_palette_t creator_load_palette;
static vi_set_border_t creator_set_border;
static vi_save_state_t creator_save_state;
static vi_load_state_t creator_load_state;
static vi_set_win_org_t creator_set_win_org;
static vi_read_hw_cursor_t creator_read_hw_cursor;
static vi_set_hw_cursor_t creator_set_hw_cursor;
static vi_set_hw_cursor_shape_t creator_set_hw_cursor_shape;
static vi_blank_display_t creator_blank_display;
static vi_mmap_t creator_mmap;
static vi_ioctl_t creator_ioctl;
static vi_clear_t creator_clear;
static vi_fill_rect_t creator_fill_rect;
static vi_bitblt_t creator_bitblt;
static vi_diag_t creator_diag;
static vi_save_cursor_palette_t creator_save_cursor_palette;
static vi_load_cursor_palette_t creator_load_cursor_palette;
static vi_copy_t creator_copy;
static vi_putp_t creator_putp;
static vi_putc_t creator_putc;
static vi_puts_t creator_puts;
static vi_putm_t creator_putm;

static video_switch_t creatorvidsw = {
	.probe			= creator_probe,
	.init			= creator_init,
	.get_info		= creator_get_info,
	.query_mode		= creator_query_mode,
	.set_mode		= creator_set_mode,
	.save_font		= creator_save_font,
	.load_font		= creator_load_font,
	.show_font		= creator_show_font,
	.save_palette		= creator_save_palette,
	.load_palette		= creator_load_palette,
	.set_border		= creator_set_border,
	.save_state		= creator_save_state,
	.load_state		= creator_load_state,
	.set_win_org		= creator_set_win_org,
	.read_hw_cursor		= creator_read_hw_cursor,
	.set_hw_cursor		= creator_set_hw_cursor,
	.set_hw_cursor_shape	= creator_set_hw_cursor_shape,
	.blank_display		= creator_blank_display,
	.mmap			= creator_mmap,
	.ioctl			= creator_ioctl,
	.clear			= creator_clear,
	.fill_rect		= creator_fill_rect,
	.bitblt			= creator_bitblt,
	.diag			= creator_diag,
	.save_cursor_palette	= creator_save_cursor_palette,
	.load_cursor_palette	= creator_load_cursor_palette,
	.copy			= creator_copy,
	.putp			= creator_putp,
	.putc			= creator_putc,
	.puts			= creator_puts,
	.putm			= creator_putm
};

VIDEO_DRIVER(creator, creatorvidsw, creator_configure);

extern sc_rndr_sw_t txtrndrsw;
RENDERER(creator, 0, txtrndrsw, gfb_set);

RENDERER_MODULE(creator, gfb_set);

static const u_char creator_mouse_pointer[64][8] __aligned(8) = {
	{ 0x00, 0x00, },	/* ............ */
	{ 0x80, 0x00, },	/* *........... */
	{ 0xc0, 0x00, },	/* **.......... */
	{ 0xe0, 0x00, },	/* ***......... */
	{ 0xf0, 0x00, },	/* ****........ */
	{ 0xf8, 0x00, },	/* *****....... */
	{ 0xfc, 0x00, },	/* ******...... */
	{ 0xfe, 0x00, },	/* *******..... */
	{ 0xff, 0x00, },	/* ********.... */
	{ 0xff, 0x80, },	/* *********... */
	{ 0xfc, 0xc0, },	/* ******..**.. */
	{ 0xdc, 0x00, },	/* **.***...... */
	{ 0x8e, 0x00, },	/* *...***..... */
	{ 0x0e, 0x00, },	/* ....***..... */
	{ 0x07, 0x00, },	/* .....***.... */
	{ 0x04, 0x00, },	/* .....*...... */
	{ 0x00, 0x00, },	/* ............ */
	{ 0x00, 0x00, },	/* ............ */
	{ 0x00, 0x00, },	/* ............ */
	{ 0x00, 0x00, },	/* ............ */
	{ 0x00, 0x00, },	/* ............ */
	{ 0x00, 0x00, },	/* ............ */
};

static inline void creator_ras_fifo_wait(struct creator_softc *sc, int n);
static inline void creator_ras_setfontinc(struct creator_softc *sc, int fontinc);
static inline void creator_ras_setfontw(struct creator_softc *sc, int fontw);
static inline void creator_ras_setbg(struct creator_softc *sc, int bg);
static inline void creator_ras_setfg(struct creator_softc *sc, int fg);
static inline void creator_ras_setpmask(struct creator_softc *sc, int pmask);
static inline void creator_ras_wait(struct creator_softc *sc);

static inline void
creator_ras_wait(struct creator_softc *sc)
{
	int ucsr;
	int r;

	for (;;) {
		ucsr = FFB_READ(sc, FFB_FBC, FFB_FBC_UCSR);
		if ((ucsr & (FBC_UCSR_FB_BUSY | FBC_UCSR_RP_BUSY)) == 0)
			break;
		r = ucsr & (FBC_UCSR_READ_ERR | FBC_UCSR_FIFO_OVFL);
		if (r != 0)
			FFB_WRITE(sc, FFB_FBC, FFB_FBC_UCSR, r);
	}
}

static inline void
creator_ras_fifo_wait(struct creator_softc *sc, int n)
{
	int cache;

	cache = sc->sc_fifo_cache;
	while (cache < n)
		cache = (FFB_READ(sc, FFB_FBC, FFB_FBC_UCSR) &
		    FBC_UCSR_FIFO_MASK) - 8;
	sc->sc_fifo_cache = cache - n;
}

static inline void
creator_ras_setfontinc(struct creator_softc *sc, int fontinc)
{

	if (fontinc == sc->sc_fontinc_cache)
		return;
	sc->sc_fontinc_cache = fontinc;
	creator_ras_fifo_wait(sc, 1);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_FONTINC, fontinc);
	creator_ras_wait(sc);
}

static inline void
creator_ras_setfontw(struct creator_softc *sc, int fontw)
{

	if (fontw == sc->sc_fontw_cache)
		return;
	sc->sc_fontw_cache = fontw;
	creator_ras_fifo_wait(sc, 1);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_FONTW, fontw);
	creator_ras_wait(sc);
}

static inline void
creator_ras_setbg(struct creator_softc *sc, int bg)
{

	if (bg == sc->sc_bg_cache)
		return;
	sc->sc_bg_cache = bg;
	creator_ras_fifo_wait(sc, 1);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_BG, bg);
	creator_ras_wait(sc);
}

static inline void
creator_ras_setfg(struct creator_softc *sc, int fg)
{

	if (fg == sc->sc_fg_cache)
		return;
	sc->sc_fg_cache = fg;
	creator_ras_fifo_wait(sc, 1);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_FG, fg);
	creator_ras_wait(sc);
}

static inline void
creator_ras_setpmask(struct creator_softc *sc, int pmask)
{

	if (pmask == sc->sc_pmask_cache)
		return;
	sc->sc_pmask_cache = pmask;
	creator_ras_fifo_wait(sc, 1);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_PMASK, pmask);
	creator_ras_wait(sc);
}

/*
 * video driver interface
 */
static int
creator_configure(int flags)
{
	struct creator_softc *sc;
	phandle_t chosen;
	phandle_t output;
	ihandle_t stdout;
	bus_addr_t addr;
	char buf[sizeof("SUNW,ffb")];
	int i;
	int space;

	/*
	 * For the high-level console probing return the number of
	 * registered adapters.
	 */
	if (!(flags & VIO_PROBE_ONLY)) {
		for (i = 0; vid_find_adapter(CREATOR_DRIVER_NAME, i) >= 0; i++)
			;
		return (i);
	}

	/* Low-level console probing and initialization. */

	sc = &creator_softc;
	if (sc->sc_va.va_flags & V_ADP_REGISTERED)
		goto found;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		return (0);
	if (OF_getprop(chosen, "stdout", &stdout, sizeof(stdout)) == -1)
		return (0);
	if ((output = OF_instance_to_package(stdout)) == -1)
		return (0);
	if (OF_getprop(output, "name", buf, sizeof(buf)) == -1)
		return (0);
	if (strcmp(buf, "SUNW,ffb") == 0 || strcmp(buf, "SUNW,afb") == 0) {
		sc->sc_flags = CREATOR_CONSOLE;
		if (strcmp(buf, "SUNW,afb") == 0)
			sc->sc_flags |= CREATOR_AFB;
		sc->sc_node = output;
	} else
		return (0);

	for (i = FFB_DAC; i <= FFB_FBC; i++) {
		if (OF_decode_addr(output, i, &space, &addr) != 0)
			return (0);
		sc->sc_bt[i] = &creator_bst_store[i - FFB_DAC];
		sc->sc_bh[i] = sparc64_fake_bustag(space, addr, sc->sc_bt[i]);
	}

	if (creator_init(0, &sc->sc_va, 0) < 0)
		return (0);

 found:
	/* Return number of found adapters. */
	return (1);
}

static int
creator_probe(int unit, video_adapter_t **adpp, void *arg, int flags)
{

	return (0);
}

static int
creator_init(int unit, video_adapter_t *adp, int flags)
{
	struct creator_softc *sc;
	phandle_t options;
	video_info_t *vi;
	char buf[sizeof("screen-#columns")];

	sc = (struct creator_softc *)adp;
	vi = &adp->va_info;

	vid_init_struct(adp, CREATOR_DRIVER_NAME, -1, unit);

	if (OF_getprop(sc->sc_node, "height", &sc->sc_height,
	    sizeof(sc->sc_height)) == -1)
		return (ENXIO);
	if (OF_getprop(sc->sc_node, "width", &sc->sc_width,
	    sizeof(sc->sc_width)) == -1)
		return (ENXIO);
	if ((options = OF_finddevice("/options")) == -1)
		return (ENXIO);
	if (OF_getprop(options, "screen-#rows", buf, sizeof(buf)) == -1)
		return (ENXIO);
	vi->vi_height = strtol(buf, NULL, 10);
	if (OF_getprop(options, "screen-#columns", buf, sizeof(buf)) == -1)
		return (ENXIO);
	vi->vi_width = strtol(buf, NULL, 10);
	vi->vi_cwidth = gallant12x22.width;
	vi->vi_cheight = gallant12x22.height;
	vi->vi_flags = V_INFO_COLOR;
	vi->vi_mem_model = V_INFO_MM_OTHER;

	sc->sc_font = gallant12x22.data;
	sc->sc_xmargin = (sc->sc_width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->sc_ymargin = (sc->sc_height - (vi->vi_height * vi->vi_cheight)) / 2;

	creator_set_mode(adp, 0);

	if (!(sc->sc_flags & CREATOR_AFB)) {
		FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE, FFB_DAC_CFG_DID);
		if (((FFB_READ(sc, FFB_DAC, FFB_DAC_VALUE) &
		    FFB_DAC_CFG_DID_PNUM) >> 12) != 0x236e) {
			sc->sc_flags |= CREATOR_PAC1;
			FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE, FFB_DAC_CFG_UCTRL);
			if (((FFB_READ(sc, FFB_DAC, FFB_DAC_VALUE) &
			    FFB_DAC_UCTRL_MANREV) >> 8) <= 2)
				sc->sc_flags |= CREATOR_CURINV;
		}
	}

	creator_blank_display(adp, V_DISPLAY_ON);
	creator_clear(adp);

	/*
	 * Setting V_ADP_MODECHANGE serves as hack so creator_set_mode()
	 * (which will invalidate our caches and restore our settings) is
	 * called when the X server shuts down.  Otherwise screen corruption
	 * happens most of the time.
	 */
	adp->va_flags |= V_ADP_COLOR | V_ADP_MODECHANGE | V_ADP_BORDER |
	    V_ADP_INITIALIZED;
	if (vid_register(adp) < 0)
		return (ENXIO);
	adp->va_flags |= V_ADP_REGISTERED;

	return (0);
}

static int
creator_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{

	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
creator_query_mode(video_adapter_t *adp, video_info_t *info)
{

	return (ENODEV);
}

static int
creator_set_mode(video_adapter_t *adp, int mode)
{
	struct creator_softc *sc;

	sc = (struct creator_softc *)adp;
	sc->sc_bg_cache = -1;
	sc->sc_fg_cache = -1;
	sc->sc_fontinc_cache = -1;
	sc->sc_fontw_cache = -1;
	sc->sc_pmask_cache = -1;

	creator_ras_wait(sc);
	sc->sc_fifo_cache = 0;
	creator_ras_fifo_wait(sc, 2);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_PPC, FBC_PPC_VCE_DIS |
	    FBC_PPC_TBE_OPAQUE | FBC_PPC_APE_DIS | FBC_PPC_CS_CONST);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_FBC, FFB_FBC_WB_A | FFB_FBC_RB_A |
	    FFB_FBC_SB_BOTH | FFB_FBC_XE_OFF | FFB_FBC_RGBE_MASK);
	return (0);
}

static int
creator_save_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{

	return (ENODEV);
}

static int
creator_load_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{

	return (ENODEV);
}

static int
creator_show_font(video_adapter_t *adp, int page)
{

	return (ENODEV);
}

static int
creator_save_palette(video_adapter_t *adp, u_char *palette)
{

	return (ENODEV);
}

static int
creator_load_palette(video_adapter_t *adp, u_char *palette)
{

	return (ENODEV);
}

static int
creator_set_border(video_adapter_t *adp, int border)
{
	struct creator_softc *sc;

	sc = (struct creator_softc *)adp;
	creator_fill_rect(adp, border, 0, 0, sc->sc_width, sc->sc_ymargin);
	creator_fill_rect(adp, border, 0, sc->sc_height - sc->sc_ymargin,
	    sc->sc_width, sc->sc_ymargin);
	creator_fill_rect(adp, border, 0, 0, sc->sc_xmargin, sc->sc_height);
	creator_fill_rect(adp, border, sc->sc_width - sc->sc_xmargin, 0,
	    sc->sc_xmargin, sc->sc_height);
	return (0);
}

static int
creator_save_state(video_adapter_t *adp, void *p, size_t size)
{

	return (ENODEV);
}

static int
creator_load_state(video_adapter_t *adp, void *p)
{

	return (ENODEV);
}

static int
creator_set_win_org(video_adapter_t *adp, off_t offset)
{

	return (ENODEV);
}

static int
creator_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{

	*col = 0;
	*row = 0;
	return (0);
}

static int
creator_set_hw_cursor(video_adapter_t *adp, int col, int row)
{

	return (ENODEV);
}

static int
creator_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
    int celsize, int blink)
{

	return (ENODEV);
}

static int
creator_blank_display(video_adapter_t *adp, int mode)
{
	struct creator_softc *sc;
	uint32_t v;
	int i;

	sc = (struct creator_softc *)adp;
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE, FFB_DAC_CFG_TGEN);
	v = FFB_READ(sc, FFB_DAC, FFB_DAC_VALUE);
	switch (mode) {
	case V_DISPLAY_ON:
		v |= FFB_DAC_CFG_TGEN_VIDE;
		break;
	case V_DISPLAY_BLANK:
	case V_DISPLAY_STAND_BY:
	case V_DISPLAY_SUSPEND:
		v &= ~FFB_DAC_CFG_TGEN_VIDE;
		break;
	}
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE, FFB_DAC_CFG_TGEN);
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE, v);
	for (i = 0; i < 10; i++) {
		FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE, FFB_DAC_CFG_TGEN);
		(void)FFB_READ(sc, FFB_DAC, FFB_DAC_VALUE);
	}
	return (0);
}

static int
creator_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{

	return (EINVAL);
}

static int
creator_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{
	struct creator_softc *sc;
	struct fbcursor *fbc;
	struct fbtype *fb;

	sc = (struct creator_softc *)adp;
	switch (cmd) {
	case FBIOGTYPE:
		fb = (struct fbtype *)data;
		fb->fb_type = FBTYPE_CREATOR;
		fb->fb_height = sc->sc_height;
		fb->fb_width = sc->sc_width;
		fb->fb_depth = fb->fb_cmsize = fb->fb_size = 0;
		break;
	case FBIOSCURSOR:
		fbc = (struct fbcursor *)data;
		if (fbc->set & FB_CUR_SETCUR && fbc->enable == 0) {
			creator_cursor_enable(sc, 0);
			sc->sc_flags &= ~CREATOR_CUREN;
		} else
			return (ENODEV);
		break;
		break;
	default:
		return (fb_commonioctl(adp, cmd, data));
	}
	return (0);
}

static int
creator_clear(video_adapter_t *adp)
{
	struct creator_softc *sc;

	sc = (struct creator_softc *)adp;
	creator_fill_rect(adp, (SC_NORM_ATTR >> 4) & 0xf, 0, 0, sc->sc_width,
	    sc->sc_height);
	return (0);
}

static int
creator_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
	struct creator_softc *sc;

	sc = (struct creator_softc *)adp;
	creator_ras_setpmask(sc, 0xffffffff);
	creator_ras_fifo_wait(sc, 2);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_ROP, FBC_ROP_NEW);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_DRAWOP, FBC_DRAWOP_RECTANGLE);
	creator_ras_setfg(sc, creator_cmap[val & 0xf]);
	/*
	 * Note that at least the Elite3D cards are sensitive to the order
	 * of operations here.
	 */
	creator_ras_fifo_wait(sc, 4);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_BY, y);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_BX, x);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_BH, cy);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_BW, cx);
	creator_ras_wait(sc);
	return (0);
}

static int
creator_bitblt(video_adapter_t *adp, ...)
{

	return (ENODEV);
}

static int
creator_diag(video_adapter_t *adp, int level)
{
	video_info_t info;

	fb_dump_adp_info(adp->va_name, adp, level);
	creator_get_info(adp, 0, &info);
	fb_dump_mode_info(adp->va_name, adp, &info, level);
	return (0);
}

static int
creator_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (ENODEV);
}

static int
creator_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (ENODEV);
}

static int
creator_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst, int n)
{

	return (ENODEV);
}

static int
creator_putp(video_adapter_t *adp, vm_offset_t off, u_int32_t p, u_int32_t a,
    int size, int bpp, int bit_ltor, int byte_ltor)
{

	return (ENODEV);
}

static int
creator_putc(video_adapter_t *adp, vm_offset_t off, u_int8_t c, u_int8_t a)
{
	struct creator_softc *sc;
	const uint16_t *p;
	int row;
	int col;
	int i;

	sc = (struct creator_softc *)adp;
	row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
	col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = (const uint16_t *)sc->sc_font + (c * adp->va_info.vi_cheight);
	creator_ras_setfg(sc, creator_cmap[a & 0xf]);
	creator_ras_setbg(sc, creator_cmap[(a >> 4) & 0xf]);
	creator_ras_fifo_wait(sc, 1 + adp->va_info.vi_cheight);
	FFB_WRITE(sc, FFB_FBC, FFB_FBC_FONTXY,
	    ((row + sc->sc_ymargin) << 16) | (col + sc->sc_xmargin));
	creator_ras_setfontw(sc, adp->va_info.vi_cwidth);
	creator_ras_setfontinc(sc, 0x10000);
	for (i = 0; i < adp->va_info.vi_cheight; i++) {
		FFB_WRITE(sc, FFB_FBC, FFB_FBC_FONT, *p++ << 16);
	}
	return (0);
}

static int
creator_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		vidd_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);
	}

	return (0);
}

static int
creator_putm(video_adapter_t *adp, int x, int y, u_int8_t *pixel_image,
    u_int32_t pixel_mask, int size, int width)
{
	struct creator_softc *sc;

	sc = (struct creator_softc *)adp;
	if (!(sc->sc_flags & CREATOR_CUREN)) {
		creator_cursor_install(sc);
		creator_cursor_enable(sc, 1);
		sc->sc_flags |= CREATOR_CUREN;
	}
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE2, FFB_DAC_CUR_POS);
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2,
	    ((y + sc->sc_ymargin) << 16) | (x + sc->sc_xmargin));
	return (0);
}

/*
 * bus interface
 */
static int
creator_bus_probe(device_t dev)
{
	const char *name;
	phandle_t node;
	int type;

	name = ofw_bus_get_name(dev);
	node = ofw_bus_get_node(dev);
	if (strcmp(name, "SUNW,ffb") == 0) {
		if (OF_getprop(node, "board_type", &type, sizeof(type)) == -1)
			return (ENXIO);
		switch (type & 7) {
		case 0x0:
			device_set_desc(dev, "Creator");
			break;
		case 0x3:
			device_set_desc(dev, "Creator3D");
			break;
		default:
			return (ENXIO);
		}
	} else if (strcmp(name, "SUNW,afb") == 0)
		device_set_desc(dev, "Elite3D");
	else
		return (ENXIO);
	return (BUS_PROBE_DEFAULT);
}

static int
creator_bus_attach(device_t dev)
{
	struct creator_softc *sc;
	video_adapter_t *adp;
	video_switch_t *sw;
	phandle_t node;
	int error;
	int rid;
	int unit;
	int i;

	node = ofw_bus_get_node(dev);
	if ((sc = (struct creator_softc *)vid_get_adapter(vid_find_adapter(
	    CREATOR_DRIVER_NAME, 0))) != NULL && sc->sc_node == node) {
		device_printf(dev, "console\n");
		device_set_softc(dev, sc);
	} else {
		sc = device_get_softc(dev);
		sc->sc_node = node;
	}
	adp = &sc->sc_va;

	/*
	 * Allocate resources regardless of whether we are the console
	 * and already obtained the bus tags and handles for the FFB_DAC
	 * and FFB_FBC register banks in creator_configure() or not so
	 * the resources are marked as taken in the respective RMAN.
	 * The supported cards use either 15 (Creator, Elite3D?) or 24
	 * (Creator3D?) register banks.  We make sure that we can also
	 * allocate the resources for at least the FFB_DAC and FFB_FBC
	 * banks here.  We try but don't actually care whether we can
	 * allocate more than these two resources and just limit the
	 * range accessible via creator_fb_mmap() accordingly.
	 */
	for (i = 0; i < FFB_NREG; i++) {
		rid = i;
		sc->sc_reg[i] = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &rid, RF_ACTIVE);
		if (sc->sc_reg[i] == NULL) {
			if (i <= FFB_FBC) {
				device_printf(dev,
				    "cannot allocate resources\n");
				error = ENXIO;
				goto fail;
			}
			break;
		}
		sc->sc_bt[i] = rman_get_bustag(sc->sc_reg[i]);
		sc->sc_bh[i] = rman_get_bushandle(sc->sc_reg[i]);
	}
	/*
	 * The XFree86/X.Org sunffb(4) expects to be able to access the
	 * memory spanned by the first and the last resource as one chunk
	 * via creator_fb_mmap(), using offsets from the first resource,
	 * even though the backing resources are actually non-continuous.
	 * So make sure that the memory we provide is at least backed by
	 * increasing resources.
	 */
	for (i = 1; i < FFB_NREG && sc->sc_reg[i] != NULL &&
	    rman_get_start(sc->sc_reg[i]) > rman_get_start(sc->sc_reg[i - 1]);
	    i++)
		;
	sc->sc_reg_size = rman_get_end(sc->sc_reg[i - 1]) -
	    rman_get_start(sc->sc_reg[0]) + 1;

	if (!(sc->sc_flags & CREATOR_CONSOLE)) {
		if ((sw = vid_get_switch(CREATOR_DRIVER_NAME)) == NULL) {
			device_printf(dev, "cannot get video switch\n");
			error = ENODEV;
			goto fail;
		}
		/*
		 * During device configuration we don't necessarily probe
		 * the adapter which is the console first so we can't use
		 * the device unit number for the video adapter unit.  The
		 * worst case would be that we use the video adapter unit
		 * 0 twice.  As it doesn't really matter which unit number
		 * the corresponding video adapter has just use the next
		 * unused one.
		 */
		for (i = 0; i < devclass_get_maxunit(creator_devclass); i++)
			if (vid_find_adapter(CREATOR_DRIVER_NAME, i) < 0)
				break;
		if (strcmp(ofw_bus_get_name(dev), "SUNW,afb") == 0)
			sc->sc_flags |= CREATOR_AFB;
		if ((error = sw->init(i, adp, 0)) != 0) {
			device_printf(dev, "cannot initialize adapter\n");
			goto fail;
		}
	}

	if (bootverbose) {
		if (sc->sc_flags & CREATOR_PAC1)
			device_printf(dev,
			    "BT9068/PAC1 RAMDAC (%s cursor control)\n",
			    sc->sc_flags & CREATOR_CURINV ? "inverted" :
			    "normal");
		else
			device_printf(dev, "BT498/PAC2 RAMDAC\n");
	}
	device_printf(dev, "resolution %dx%d\n", sc->sc_width, sc->sc_height);

	unit = device_get_unit(dev);
	sc->sc_si = make_dev(&creator_fb_devsw, unit, UID_ROOT, GID_WHEEL,
	    0600, "fb%d", unit);
	sc->sc_si->si_drv1 = sc;

	EVENTHANDLER_REGISTER(shutdown_final, creator_shutdown, sc,
	    SHUTDOWN_PRI_DEFAULT);

	return (0);

 fail:
	for (i = 0; i < FFB_NREG && sc->sc_reg[i] != NULL; i++)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->sc_reg[i]), sc->sc_reg[i]);
	return (error);
}

/*
 * /dev/fb interface
 */
static int
creator_fb_open(struct cdev *dev, int flags, int mode, struct thread *td)
{

	return (0);
}

static int
creator_fb_close(struct cdev *dev, int flags, int mode, struct thread *td)
{

	return (0);
}

static int
creator_fb_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags,
    struct thread *td)
{
	struct creator_softc *sc;

	sc = dev->si_drv1;
	return (creator_ioctl(&sc->sc_va, cmd, data));
}

static int
creator_fb_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct creator_softc *sc;
	int i;

	/*
	 * NB: This is a special implementation based on the /dev/fb
	 * requirements of the XFree86/X.Org sunffb(4).
	 */
	sc = dev->si_drv1;
	for (i = 0; i < CREATOR_FB_MAP_SIZE; i++) {
		if (offset >= creator_fb_map[i].virt &&
		    offset < creator_fb_map[i].virt + creator_fb_map[i].size) {
			offset += creator_fb_map[i].phys -
			    creator_fb_map[i].virt;
			if (offset >= sc->sc_reg_size)
				return (EINVAL);
			*paddr = sc->sc_bh[0] + offset;
			return (0);
		}
	}
	return (EINVAL);
}

/*
 * internal functions
 */
static void
creator_cursor_enable(struct creator_softc *sc, int onoff)
{
	int v;

	FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE2, FFB_DAC_CUR_CTRL);
	if (sc->sc_flags & CREATOR_CURINV)
		v = onoff ? FFB_DAC_CUR_CTRL_P0 | FFB_DAC_CUR_CTRL_P1 : 0;
	else
		v = onoff ? 0 : FFB_DAC_CUR_CTRL_P0 | FFB_DAC_CUR_CTRL_P1;
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2, v);
}

static void
creator_cursor_install(struct creator_softc *sc)
{
	int i, j;

	creator_cursor_enable(sc, 0);
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE2, FFB_DAC_CUR_COLOR1);
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2, 0xffffff);
	FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2, 0x0);
	for (i = 0; i < 2; i++) {
		FFB_WRITE(sc, FFB_DAC, FFB_DAC_TYPE2,
		    i ? FFB_DAC_CUR_BITMAP_P0 : FFB_DAC_CUR_BITMAP_P1);
		for (j = 0; j < 64; j++) {
			FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2,
			    *(const uint32_t *)(&creator_mouse_pointer[j][0]));
			FFB_WRITE(sc, FFB_DAC, FFB_DAC_VALUE2,
			    *(const uint32_t *)(&creator_mouse_pointer[j][4]));
		}
	}
}

static void
creator_shutdown(void *xsc)
{
	struct creator_softc *sc = xsc;

	creator_cursor_enable(sc, 0);
	/*
	 * In case this is the console set the cursor of the stdout
	 * instance to the start of the last line so OFW output ends
	 * up beneath what FreeBSD left on the screen.
	 */
	if (sc->sc_flags & CREATOR_CONSOLE) {
		OF_interpret("stdout @ is my-self 0 to column#", 0);
		OF_interpret("stdout @ is my-self #lines 1 - to line#", 0);
	}
}
