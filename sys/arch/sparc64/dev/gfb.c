/*	$OpenBSD: gfb.c,v 1.4 2022/07/15 17:57:26 kettenis Exp $	*/

/*
 * Copyright (c) 2009 Mark Kettenis.
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
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

struct gfb_softc {
	struct sunfb sc_sunfb;
	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_pixel_h;
	int sc_console;
	int sc_node;
	u_int sc_mode;
};

int	gfb_match(struct device *, void *, void *);
void	gfb_attach(struct device *, struct device *, void *);

int	gfb_ioctl(void *, u_long, caddr_t, int, struct proc *);

struct wsdisplay_accessops gfb_accessops = {
	.ioctl = gfb_ioctl
};

struct cfdriver gfb_cd = {
	NULL, "gfb", DV_DULL
};

const struct cfattach gfb_ca = {
	sizeof(struct gfb_softc), gfb_match, gfb_attach
};

int
gfb_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "SUNW,gfb") == 0)
		return (1);
	return (0);
}

void
gfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct gfb_softc *sc = (struct gfb_softc *)self;
	struct mainbus_attach_args *ma = aux;
	extern int fbnode;

	sc->sc_bt = ma->ma_bustag;

	printf("\n");

	if (bus_space_map(ma->ma_bustag, ma->ma_reg[6].ur_paddr,
	    ma->ma_reg[6].ur_len, BUS_SPACE_MAP_LINEAR, &sc->sc_pixel_h))
		return;

	sc->sc_console = (fbnode == ma->ma_node);
	sc->sc_node = ma->ma_node;

	fb_setsize(&sc->sc_sunfb, 32, 1152, 900, sc->sc_node, 0);
	/* linesize has a fixed value, compensate */
	sc->sc_sunfb.sf_linebytes = 16384;
	sc->sc_sunfb.sf_fbsize = sc->sc_sunfb.sf_height * 16384;

	sc->sc_sunfb.sf_ro.ri_bits = (void *)bus_space_vaddr(sc->sc_bt,
	    sc->sc_pixel_h);
	sc->sc_sunfb.sf_ro.ri_hw = sc;
	fbwscons_init(&sc->sc_sunfb, 0, sc->sc_console);

	if (sc->sc_console)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &gfb_accessops, sc->sc_console);
	return;
}

int
gfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct gfb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

#if 0
	case WSDISPLAYIO_GETCMAP:
		return gfb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return gfb_putcmap(sc, (struct wsdisplay_cmap *)data);
#endif

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return (0);
}
