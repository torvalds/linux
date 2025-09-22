/* $OpenBSD: exdisplay.c,v 1.7 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <dev/cons.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wscons_callbacks.h>
#include <dev/wsfont/wsfont.h>
#include <dev/rasops/rasops.h>

#include <machine/intr.h>
#include <machine/bus.h>
#if NFDT > 0
#include <machine/fdt.h>
#endif
#include <armv7/armv7/armv7var.h>

/* registers */

struct exdisplay_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct rasops_info	*ro;
};

int exdisplay_match(struct device *parent, void *v, void *aux);
void exdisplay_attach(struct device *parent, struct device *self, void *args);
int exdisplay_cnattach(bus_space_tag_t iot, bus_addr_t iobase, size_t size);
void exdisplay_setup_rasops(struct rasops_info *rinfo, struct wsscreen_descr *descr);

const struct cfattach	exdisplay_ca = {
	sizeof (struct exdisplay_softc), NULL, exdisplay_attach
};
const struct cfattach	exdisplay_fdt_ca = {
	sizeof (struct exdisplay_softc), exdisplay_match, exdisplay_attach
};

struct cfdriver exdisplay_cd = {
	NULL, "exdisplay", DV_DULL
};

int exdisplay_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t exdisplay_wsmmap(void *, off_t, int);
int exdisplay_alloc_screen(void *, const struct wsscreen_descr *,
    void **, int *, int *, uint32_t *);
void exdisplay_free_screen(void *, void *);
int exdisplay_show_screen(void *, void *, int,
    void (*)(void *, int, int), void *);
void exdisplay_doswitch(void *, void *);
int exdisplay_load_font(void *, void *, struct wsdisplay_font *);
int exdisplay_list_font(void *, struct wsdisplay_font *);
int exdisplay_getchar(void *, int, int, struct wsdisplay_charcell *);
void exdisplay_burner(void *, u_int, u_int);

struct rasops_info exdisplay_ri;
struct wsscreen_descr exdisplay_stdscreen = {
	"std"
};

const struct wsscreen_descr *exdisplay_scrlist[] = {
	&exdisplay_stdscreen,
};

struct wsscreen_list exdisplay_screenlist = {
	nitems(exdisplay_scrlist), exdisplay_scrlist
};

struct wsdisplay_accessops exdisplay_accessops = {
	.ioctl = exdisplay_wsioctl,
	.mmap = exdisplay_wsmmap,
	.alloc_screen = exdisplay_alloc_screen,
	.free_screen = exdisplay_free_screen,
	.show_screen = exdisplay_show_screen,
	.getchar = exdisplay_getchar,
	.load_font = exdisplay_load_font,
	.list_font = exdisplay_list_font,
	.burn_screen = exdisplay_burner
};

int
exdisplay_match(struct device *parent, void *v, void *aux)
{
#if NFDT > 0
	struct armv7_attach_args *aa = aux;

	if (fdt_node_compatible("samsung,exynos5250-fimd", aa->aa_node))
		return 1;
#endif

	return 0;
}

void
exdisplay_attach(struct device *parent, struct device *self, void *args)
{
	struct armv7_attach_args *aa = args;
	struct exdisplay_softc *sc = (struct exdisplay_softc *) self;
	struct wsemuldisplaydev_attach_args waa;
	struct rasops_info *ri = &exdisplay_ri;
	struct armv7mem mem;

	sc->sc_iot = aa->aa_iot;
#if NFDT > 0
	if (aa->aa_node) {
		struct fdt_reg reg;
		if (fdt_get_reg(aa->aa_node, 0, &reg))
			panic("%s: could not extract memory data from FDT",
			    __func__);
		mem.addr = reg.addr;
		mem.size = reg.size;
	} else
#endif
	{
		mem.addr = aa->aa_dev->mem[0].addr;
		mem.size = aa->aa_dev->mem[0].size;
	}

	if (bus_space_map(sc->sc_iot, mem.addr, mem.size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

#if notyet
	/* FIXME: Set up framebuffer instead of re-using. */
	if (!fdt_find_compatible("simple-framebuffer")) {
		uint32_t defattr;

		ri->ri_bits = (u_char *)sc->sc_fbioh;
		exdisplay_setup_rasops(ri, &exdisplay_stdscreen);

		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&exdisplay_stdscreen, ri->ri_active,
		    0, 0, defattr);
	}
#endif

	sc->ro = ri;

	waa.console = 1;
	waa.scrdata = &exdisplay_screenlist;
	waa.accessops = &exdisplay_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	printf("%s: %dx%d\n", sc->sc_dev.dv_xname, ri->ri_width, ri->ri_height);

	config_found(self, &waa, wsemuldisplaydevprint);
}

int
exdisplay_cnattach(bus_space_tag_t iot, bus_addr_t iobase, size_t size)
{
	struct wsscreen_descr *descr = &exdisplay_stdscreen;
	struct rasops_info *ri = &exdisplay_ri;
	uint32_t defattr;

	if (bus_space_map(iot, iobase, size, 0, (bus_space_handle_t *)&ri->ri_bits))
		return ENOMEM;

	exdisplay_setup_rasops(ri, descr);

	/* assumes 16 bpp */
	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);

	wsdisplay_cnattach(descr, ri, ri->ri_ccol, ri->ri_crow, defattr);

	return 0;
}

void
exdisplay_setup_rasops(struct rasops_info *rinfo, struct wsscreen_descr *descr)
{
	rinfo->ri_flg = RI_CLEAR;
	rinfo->ri_depth = 16;
	rinfo->ri_width = 1366;
	rinfo->ri_height = 768;
	rinfo->ri_stride = rinfo->ri_width * rinfo->ri_depth / 8;

	/* swap B and R */
	if (rinfo->ri_depth == 16) {
		rinfo->ri_rnum = 5;
		rinfo->ri_rpos = 11;
		rinfo->ri_gnum = 6;
		rinfo->ri_gpos = 5;
		rinfo->ri_bnum = 5;
		rinfo->ri_bpos = 0;
	}

	wsfont_init();
	rinfo->ri_wsfcookie = wsfont_find(NULL, 8, 0, 0);
	wsfont_lock(rinfo->ri_wsfcookie, &rinfo->ri_font,
	    WSDISPLAY_FONTORDER_L2R, WSDISPLAY_FONTORDER_L2R);

	/* get rasops to compute screen size the first time */
	rasops_init(rinfo, 200, 200);

	descr->nrows = rinfo->ri_rows;
	descr->ncols = rinfo->ri_cols;
	descr->capabilities = rinfo->ri_caps;
	descr->textops = &rinfo->ri_ops;
}

int
exdisplay_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return (-1);
}

paddr_t
exdisplay_wsmmap(void *v, off_t off, int prot)
{
	return (-1);
}

int
exdisplay_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct exdisplay_softc *sc = v;
	struct rasops_info *ri = sc->ro;

	return rasops_alloc_screen(ri, cookiep, curxp, curyp, attrp);
}

void
exdisplay_free_screen(void *v, void *cookie)
{
	struct exdisplay_softc *sc = v;
	struct rasops_info *ri = sc->ro;

	return rasops_free_screen(ri, cookie);
}

int
exdisplay_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return (0);
}

void
exdisplay_doswitch(void *v, void *dummy)
{
}

int
exdisplay_getchar(void *v, int row, int col, struct wsdisplay_charcell *cell)
{
	struct exdisplay_softc *sc = v;
	struct rasops_info *ri = sc->ro;

	return rasops_getchar(ri, row, col, cell);
}

int
exdisplay_load_font(void *v, void *cookie, struct wsdisplay_font *font)
{
	struct exdisplay_softc *sc = v;
	struct rasops_info *ri = sc->ro;

	return rasops_load_font(ri, cookie, font);
}

int
exdisplay_list_font(void *v, struct wsdisplay_font *font)
{
	struct exdisplay_softc *sc = v;
	struct rasops_info *ri = sc->ro;

	return rasops_list_font(ri, font);
}

void
exdisplay_burner(void *v, u_int on, u_int flags)
{
}
