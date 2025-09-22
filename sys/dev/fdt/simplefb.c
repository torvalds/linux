/*	$OpenBSD: simplefb.c,v 1.21 2024/11/12 20:52:35 tobhe Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#define SIMPLEFB_WIDTH	160
#define SIMPLEFB_HEIGHT	50

struct simplefb_format {
	const char *format;
	int depth;
	int rpos, rnum;
	int gpos, gnum;
	int bpos, bnum;
};

/*
 * Supported pixel formats.  Layout omitted when it matches the
 * rasops defaults.
 */
const struct simplefb_format simplefb_formats[] = {
	{ "r5g6b5", 16, 11, 5, 5, 6, 0, 5 },
	{ "x1r5g5b5", 15, 10, 5, 5, 5, 0, 5 },
	{ "a1r5g5b5", 15, 10, 5, 5, 5, 0, 5 },
	{ "r8g8b8", 24, 16, 8, 8, 8, 0, 8 },
	{ "x8r8g8b8", 32, 16, 8, 8, 8, 0, 8 },
	{ "a8r8g8b8", 32, 16, 8, 8, 8, 0, 8 },
	{ "x8b8g8r8", 32 },
	{ "a8b8g8r8", 32 },
	{ "x2r10g10b10", 32, 20, 10, 10, 10, 0, 10 },
	{ "a2r10g10b10", 32, 20, 10, 10, 10, 0, 10 },
};

struct simplefb_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct rasops_info	sc_ri;
	struct wsscreen_descr	sc_wsd;
	struct wsscreen_list	sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];

	struct simplefb_format	*sc_format;
	paddr_t			sc_paddr;
	psize_t			sc_psize;
};

void (*simplefb_burn_hook)(u_int) = NULL;

struct rasops_info simplefb_ri;
struct wsscreen_descr simplefb_wsd = { "std" };
struct wsdisplay_charcell simplefb_bs[SIMPLEFB_WIDTH * SIMPLEFB_HEIGHT];

int	simplefb_match(struct device *, void *, void *);
void	simplefb_attach(struct device *, struct device *, void *);

const struct cfattach simplefb_ca = {
	sizeof(struct simplefb_softc), simplefb_match, simplefb_attach
};

struct cfdriver simplefb_cd = {
	NULL, "simplefb", DV_DULL
};

const char *simplefb_init(int, struct rasops_info *);

int	simplefb_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	simplefb_wsmmap(void *, off_t, int);
int	simplefb_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);
void	simplefb_burn_screen(void *, u_int, u_int);

struct wsdisplay_accessops simplefb_accessops = {
	.ioctl = simplefb_wsioctl,
	.mmap = simplefb_wsmmap,
	.alloc_screen = simplefb_alloc_screen,
	.free_screen = rasops_free_screen,
	.show_screen = rasops_show_screen,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback,
	.burn_screen = simplefb_burn_screen,
};

int
simplefb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	/* Don't attach if it has no address space. */
	if (faa->fa_nreg < 1 || faa->fa_reg[0].size == 0)
		return 0;

	/* Don't attach if another driver already claimed our framebuffer. */
	if (rasops_check_framebuffer(faa->fa_reg[0].addr))
		return 0;

	return OF_is_compatible(faa->fa_node, "simple-framebuffer");
}

void
simplefb_attach(struct device *parent, struct device *self, void *aux)
{
	struct simplefb_softc *sc = (struct simplefb_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct rasops_info *ri = &sc->sc_ri;
	struct wsemuldisplaydev_attach_args waa;
	const char *format;
	int console = 0;
	uint32_t defattr;

	format = simplefb_init(faa->fa_node, ri);
	if (format) {
		printf(": unsupported format \"%s\"\n", format);
		return;
	}

	if (faa->fa_node == stdout_node)
		console = 1;

	sc->sc_iot = faa->fa_iot;
	sc->sc_paddr = faa->fa_reg[0].addr;
	sc->sc_psize = faa->fa_reg[0].size;
	if (bus_space_map(sc->sc_iot, sc->sc_paddr, sc->sc_psize,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE, &sc->sc_ioh)) {
		printf(": can't map framebuffer\n");
		return;
	}

	ri->ri_bits = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	ri->ri_hw = sc;

	if (console) {
		/* Preserve contents. */
		ri->ri_bs = simplefb_bs;
		ri->ri_flg &= ~RI_CLEAR;
	}

	printf(": %dx%d, %dbpp\n", ri->ri_width, ri->ri_height, ri->ri_depth);

	ri->ri_flg |= RI_VCONS;
	rasops_init(ri, SIMPLEFB_HEIGHT, SIMPLEFB_WIDTH);

	strlcpy(sc->sc_wsd.name, "std", sizeof(sc->sc_wsd.name));
	sc->sc_wsd.capabilities = ri->ri_caps;
	sc->sc_wsd.nrows = ri->ri_rows;
	sc->sc_wsd.ncols = ri->ri_cols;
	sc->sc_wsd.textops = &ri->ri_ops;
	sc->sc_wsd.fontwidth = ri->ri_font->fontwidth;
	sc->sc_wsd.fontheight = ri->ri_font->fontheight;

	sc->sc_scrlist[0] = &sc->sc_wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	if (console) {
		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&sc->sc_wsd, ri->ri_active,
		    simplefb_ri.ri_ccol, simplefb_ri.ri_crow, defattr);
	}

	memset(&waa, 0, sizeof(waa));
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &simplefb_accessops;
	waa.accesscookie = ri;
	waa.console = console;

	config_found_sm(self, &waa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
}

const char *
simplefb_init(int node, struct rasops_info *ri)
{
	const struct simplefb_format *fmt = NULL;
	static char format[16];
	int i;

	format[0] = 0;
	OF_getprop(node, "format", format, sizeof(format));
	format[sizeof(format) - 1] = 0;

	for (i = 0; i < nitems(simplefb_formats); i++) {
		if (strcmp(format, simplefb_formats[i].format) == 0) {
			fmt = &simplefb_formats[i];
			break;
		}
	}
	if (fmt == NULL)
		return format;

	ri->ri_width = OF_getpropint(node, "width", 0);
	ri->ri_height = OF_getpropint(node, "height", 0);
	ri->ri_stride = OF_getpropint(node, "stride", 0);
	ri->ri_depth = fmt->depth;
	ri->ri_rpos = fmt->rpos;
	ri->ri_rnum = fmt->rnum;
	ri->ri_gpos = fmt->gpos;
	ri->ri_gnum = fmt->gnum;
	ri->ri_bpos = fmt->bpos;
	ri->ri_bnum = fmt->bnum;
	ri->ri_flg = RI_CENTER | RI_CLEAR | RI_FULLCLEAR | RI_WRONLY;

	return NULL;
}

int
simplefb_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct rasops_info *ri = v;
	struct simplefb_softc *sc = ri->ri_hw;
	struct wsdisplay_param *dp = (struct wsdisplay_param *)data;
	struct wsdisplay_fbinfo	*wdf;

	switch (cmd) {
	case WSDISPLAYIO_GETPARAM:
		if (ws_get_param)
			return ws_get_param(dp);
		return -1;
	case WSDISPLAYIO_SETPARAM:
		if (ws_set_param)
			return ws_set_param(dp);
		return -1;
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_EFIFB;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = sc->sc_paddr & PAGE_MASK;
		wdf->cmsize = 0;	/* color map is unavailable */
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_SMODE:
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		switch (ri->ri_depth) {
		case 32:
			if (ri->ri_rnum == 10)
				*(u_int *)data = WSDISPLAYIO_DEPTH_30;
			else
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
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;
	default:
		return -1;
	}

	return 0;
}

paddr_t
simplefb_wsmmap(void *v, off_t off, int prot)
{
	struct rasops_info *ri = v;
	struct simplefb_softc *sc = ri->ri_hw;

	if (off < 0 || off >= (sc->sc_psize + (sc->sc_paddr & PAGE_MASK)))
		return -1;

	return (((sc->sc_paddr & ~PAGE_MASK) + off) | PMAP_NOCACHE);
}

int
simplefb_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
} 

void
simplefb_burn_screen(void *v, u_int on, u_int flags)
{
	if (simplefb_burn_hook != NULL)
		simplefb_burn_hook(on);
}

#include "ukbd.h"

#if NUKBD > 0
#include <dev/usb/ukbdvar.h>
#endif

void
simplefb_init_cons(bus_space_tag_t iot)
{
	struct rasops_info *ri = &simplefb_ri;
	bus_space_handle_t ioh;
	struct fdt_reg reg;
	void *node;
	uint32_t defattr = 0;

	node = fdt_find_cons("simple-framebuffer");
	if (node == NULL)
		return;
	
	if (fdt_get_reg(node, 0, &reg))
		return;

	if (bus_space_map(iot, reg.addr, reg.size,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE, &ioh))
		return;

	ri->ri_bits = bus_space_vaddr(iot, ioh);

	if (simplefb_init(stdout_node, ri))
		return;

	ri->ri_bs = simplefb_bs;
	rasops_init(ri, SIMPLEFB_HEIGHT, SIMPLEFB_WIDTH);

	simplefb_wsd.capabilities = ri->ri_caps;
	simplefb_wsd.ncols = ri->ri_cols;
	simplefb_wsd.nrows = ri->ri_rows;
	simplefb_wsd.textops = &ri->ri_ops;
	simplefb_wsd.fontwidth = ri->ri_font->fontwidth;
	simplefb_wsd.fontheight = ri->ri_font->fontheight;

	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&simplefb_wsd, ri, 0, 0, defattr);

#if NUKBD > 0
	/* Allow USB keyboards to become the console input device. */
	ukbd_cnattach();
#endif
}
