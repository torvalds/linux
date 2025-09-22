/*	$OpenBSD: astfb.c,v 1.5 2022/07/15 17:57:26 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis.
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
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/rasops/rasops.h>

#define ASTFB_PCI_FB	0x10

struct astfb_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	bus_addr_t		sc_fbaddr;
	bus_size_t		sc_fbsize;

	struct rasops_info	sc_ri;
	struct wsscreen_descr	sc_wsd;
	struct wsscreen_list	sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
};

int	astfb_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	astfb_wsmmap(void *, off_t, int);
int	astfb_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);

struct wsdisplay_accessops astfb_accessops = {
	.ioctl = astfb_wsioctl,
	.mmap = astfb_wsmmap,
	.alloc_screen = astfb_alloc_screen,
	.free_screen = rasops_free_screen,
	.show_screen = rasops_show_screen,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback
};

int	astfb_match(struct device *, void *, void *);
void	astfb_attach(struct device *, struct device *, void *);

const struct cfattach astfb_ca = {
	sizeof(struct astfb_softc), astfb_match, astfb_attach
};

struct cfdriver astfb_cd = {
	NULL, "astfb", DV_DULL
};

int
astfb_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ASPEED &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ASPEED_AST2000 &&
	    PCITAG_NODE(pa->pa_tag) != 0)
		return 1;

	return 0;
}

void
astfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct astfb_softc *sc = (struct astfb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct rasops_info *ri = &sc->sc_ri;
	struct wsemuldisplaydev_attach_args waa;
	int node = PCITAG_NODE(pa->pa_tag);
	uint32_t addr[5];
	int console = 0;
	uint32_t defattr;

	if (OF_getpropintarray(node, "assigned-addresses", addr,
	    sizeof(addr)) < sizeof(addr)) {
		printf(": no framebuffer\n");
		return;
	}

	if (node == stdout_node)
		console = 1;

	sc->sc_fbaddr = (bus_addr_t)addr[1] << 32 | addr[2];
	sc->sc_fbsize = (bus_size_t)addr[3] << 32 | addr[4];

	sc->sc_iot = pa->pa_memt;
	if (bus_space_map(sc->sc_iot, sc->sc_fbaddr, sc->sc_fbsize,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_ioh)) {
		printf(": can't map framebuffer\n");
		return;
	}

	printf("\n");

	ri->ri_bits = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	ri->ri_hw = sc;

	ri->ri_width = OF_getpropint(node, "width", 0);
	ri->ri_height = OF_getpropint(node, "height", 0);
	ri->ri_depth = OF_getpropint(node, "depth", 0);
	ri->ri_stride = ri->ri_width * ((ri->ri_depth + 7) / 8);
	ri->ri_flg = RI_CENTER | RI_CLEAR | RI_FULLCLEAR | RI_WRONLY;

	switch (ri->ri_depth) {
	case 32:
		ri->ri_rnum = 8;
		ri->ri_rpos = 8;
		ri->ri_gnum = 8;
		ri->ri_gpos = 16;
		ri->ri_bnum = 8;
		ri->ri_bpos = 24;
		break;
	case 16:
		ri->ri_rnum = 5;
		ri->ri_rpos = 0;
		ri->ri_gnum = 6;
		ri->ri_gpos = 6;
		ri->ri_bnum = 5;
		ri->ri_bpos = 11;
		break;
	}

	printf("%s: %dx%d, %dbpp\n", sc->sc_dev.dv_xname,
	    ri->ri_width, ri->ri_height, ri->ri_depth);

	ri->ri_flg |= RI_VCONS;
	rasops_init(ri, 160, 160);

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
		    0, 0, defattr);
	}

	memset(&waa, 0, sizeof(waa));
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &astfb_accessops;
	waa.accesscookie = ri;
	waa.console = console;

	config_found_sm(self, &waa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
}

int
astfb_wsioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct rasops_info *ri = v;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_ASTFB;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
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
			*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
			break;
		case 16:
			*(u_int *)data = WSDISPLAYIO_DEPTH_16;
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
astfb_wsmmap(void *v, off_t off, int prot)
{
	struct rasops_info *ri = v;
	struct astfb_softc *sc = ri->ri_hw;

	if (off < 0 || off >= sc->sc_fbaddr)
		return -1;
	
	return (bus_space_mmap(sc->sc_iot, sc->sc_fbaddr,
	    off, prot, BUS_SPACE_MAP_LINEAR));
}

int
astfb_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
}
