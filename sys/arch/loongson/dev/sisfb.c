/*	$OpenBSD: sisfb.c,v 1.11 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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

/*
 * Minimalistic driver for the SIS315 Pro frame buffer found on the
 * Lemote Fuloong 2F systems.
 * Does not support acceleration, mode change, secondary output, or
 * anything fancy.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/mc6845.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

struct sisfb_softc;

/* minimal frame buffer information, suitable for early console */
struct sisfb {
	struct sisfb_softc	*sc;
	struct rasops_info	 ri;
	uint8_t			 cmap[256 * 3];

	bus_space_tag_t		 fbt;
	bus_space_handle_t	 fbh;

	bus_space_tag_t		 mmiot;
	bus_space_handle_t	 mmioh;

	bus_space_tag_t		 iot;
	bus_space_handle_t	 ioh;

	struct wsscreen_descr	 wsd;
};

struct sisfb_softc {
	struct device		 sc_dev;
	struct sisfb		*sc_fb;
	struct sisfb		 sc_fb_store;

	struct wsscreen_list	 sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
	int			 sc_nscr;
};

int	sisfb_match(struct device *, void *, void *);
void	sisfb_attach(struct device *, struct device *, void *);

const struct cfattach sisfb_ca = {
	sizeof(struct sisfb_softc), sisfb_match, sisfb_attach
};

struct cfdriver sisfb_cd = {
	NULL, "sisfb", DV_DULL
};

int	sisfb_alloc_screen(void *, const struct wsscreen_descr *, void **, int *,
	    int *, uint32_t *);
void	sisfb_free_screen(void *, void *);
int	sisfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	sisfb_list_font(void *, struct wsdisplay_font *);
int	sisfb_load_font(void *, void *, struct wsdisplay_font *);
paddr_t	sisfb_mmap(void *, off_t, int);
int	sisfb_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

struct wsdisplay_accessops sisfb_accessops = {
	.ioctl = sisfb_ioctl,
	.mmap = sisfb_mmap,
	.alloc_screen = sisfb_alloc_screen,
	.free_screen = sisfb_free_screen,
	.show_screen = sisfb_show_screen,
	.load_font = sisfb_load_font,
	.list_font = sisfb_list_font
};

int	sisfb_getcmap(uint8_t *, struct wsdisplay_cmap *);
void	sisfb_loadcmap(struct sisfb *, int, int);
int	sisfb_putcmap(uint8_t *, struct wsdisplay_cmap *);
int	sisfb_setup(struct sisfb *);

static struct sisfb sisfbcn;

const struct pci_matchid sisfb_devices[] = {
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_315PRO_VGA }
};

/*
 * Control Register access
 *
 * These are 8 bit registers; the choice of larger width types is intentional.
 */

#define	SIS_VGA_PORT_OFFSET	0x380

#define	SEQ_ADDR		(0x3c4 - SIS_VGA_PORT_OFFSET)
#define	SEQ_DATA		(0x3c5 - SIS_VGA_PORT_OFFSET)
#define	DAC_ADDR		(0x3c8 - SIS_VGA_PORT_OFFSET)
#define	DAC_DATA		(0x3c9 - SIS_VGA_PORT_OFFSET)
#undef	CRTC_ADDR
#define	CRTC_ADDR		(0x3d4 - SIS_VGA_PORT_OFFSET)
#define	CRTC_DATA		(0x3d5 - SIS_VGA_PORT_OFFSET)

static inline uint sisfb_crtc_read(struct sisfb *, uint);
static inline uint sisfb_seq_read(struct sisfb *, uint);
static inline void sisfb_seq_write(struct sisfb *, uint, uint);

static inline uint
sisfb_crtc_read(struct sisfb *fb, uint idx)
{
	uint val;
	bus_space_write_1(fb->iot, fb->ioh, CRTC_ADDR, idx);
	val = bus_space_read_1(fb->iot, fb->ioh, CRTC_DATA);
#ifdef SIS_DEBUG
	printf("CRTC %04x -> %02x\n", idx, val);
#endif
	return val;
}

static inline uint
sisfb_seq_read(struct sisfb *fb, uint idx)
{
	uint val;
	bus_space_write_1(fb->iot, fb->ioh, SEQ_ADDR, idx);
	val = bus_space_read_1(fb->iot, fb->ioh, SEQ_DATA);
#ifdef SIS_DEBUG
	printf("SEQ %04x -> %02x\n", idx, val);
#endif
	return val;
}

static inline void
sisfb_seq_write(struct sisfb *fb, uint idx, uint val)
{
#ifdef SIS_DEBUG
	printf("SEQ %04x <- %02x\n", idx, val);
#endif
	bus_space_write_1(fb->iot, fb->ioh, SEQ_ADDR, idx);
	bus_space_write_1(fb->iot, fb->ioh, SEQ_DATA, val);
}

int
sisfb_match(struct device *parent, void *vcf, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	return pci_matchbyid(pa, sisfb_devices, nitems(sisfb_devices));
}

void
sisfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct wsemuldisplaydev_attach_args waa;
	bus_space_tag_t fbt, mmiot, iot;
	bus_space_handle_t fbh, mmioh, ioh;
	bus_size_t fbsize, mmiosize;
	struct sisfb *fb;
	int console;

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &fbt, &fbh, NULL, &fbsize, 0) != 0) {
		printf(": can't map frame buffer\n");
		return;
	}

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 4, PCI_MAPREG_TYPE_MEM,
	    0, &mmiot, &mmioh, NULL, &mmiosize, 0) != 0) {
		printf(": can't map mmio area\n");
		goto fail1;
	}

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 8, PCI_MAPREG_TYPE_IO,
	    0, &iot, &ioh, NULL, NULL, 0) != 0) {
		printf(": can't map registers\n");
		goto fail2;
	}

	console = sisfbcn.ri.ri_hw != NULL;

	if (console)
		fb = &sisfbcn;
	else
		fb = &sc->sc_fb_store;

	fb->sc = sc;
	fb->fbt = fbt;
	fb->fbh = fbh;
	fb->mmiot = mmiot;
	fb->mmioh = mmioh;
	fb->iot = iot;
	fb->ioh = ioh;
	sc->sc_fb = fb;

	if (!console) {
		if (sisfb_setup(fb) != 0) {
			printf(": can't setup frame buffer\n");
			return;
		}
	}

	printf(": %dx%d, %dbpp\n",
	    fb->ri.ri_width, fb->ri.ri_height, fb->ri.ri_depth);

	sc->sc_scrlist[0] = &fb->wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	waa.console = console;
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &sisfb_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	config_found((struct device *)sc, &waa, wsemuldisplaydevprint);
	return;

fail2:
	bus_space_unmap(mmiot, mmioh, mmiosize);
fail1:
	bus_space_unmap(fbt, fbh, fbsize);
}

/*
 * wsdisplay accessops
 */

int
sisfb_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	if (sc->sc_nscr > 0)
		return ENOMEM;

	*cookiep = ri;
	*curxp = *curyp = 0;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, attrp);
	sc->sc_nscr++;

	return 0;
}

void
sisfb_free_screen(void *v, void *cookie)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;

	sc->sc_nscr--;
}

int
sisfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;
	struct sisfb *fb = sc->sc_fb;
	struct rasops_info *ri = &fb->ri;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int rc;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(uint *)data = WSDISPLAY_TYPE_SISFB;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(uint *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_GETCMAP:
		cm = (struct wsdisplay_cmap *)data;
		rc = sisfb_getcmap(fb->cmap, cm);
		if (rc != 0)
			return rc;
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		rc = sisfb_putcmap(fb->cmap, cm);
		if (rc != 0)
			return rc;
		if (ri->ri_depth == 8)
			sisfb_loadcmap(fb, cm->index, cm->count);
		break;
	default:
		return -1;
	}

	return 0;
}

int
sisfb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return 0;
}

paddr_t
sisfb_mmap(void *v, off_t offset, int prot)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	if ((offset & PAGE_MASK) != 0)
		return -1;

	if (offset < 0 || offset >= ri->ri_stride * ri->ri_height)
		return -1;

	/*
	 * Don't allow mmap if the frame buffer area is not page aligned.
	 * XXX we should reprogram it to a page aligned boundary at attach
	 * XXX time if this isn't the case.
	 */
	if (((paddr_t)ri->ri_bits & PAGE_MASK) != 0)
		return -1;

	return XKPHYS_TO_PHYS((paddr_t)ri->ri_bits) + offset;
}

int
sisfb_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
sisfb_list_font(void *v, struct wsdisplay_font *font)
{
	struct sisfb_softc *sc = (struct sisfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_list_font(ri, font);
}

/*
 * Frame buffer initialization.
 */

int
sisfb_setup(struct sisfb *fb)
{
	struct rasops_info *ri;
	uint width, height, bpp;
	bus_size_t fbaddr;
	uint tmp;

	/*
	 * Unlock access to extended registers.
	 */

	sisfb_seq_write(fb, 0x05, 0x86);

	/*
	 * Try and figure out display settings.
	 */

	height = sisfb_crtc_read(fb, CRTC_VDE);
	tmp = sisfb_crtc_read(fb, CRTC_OVERFLL);
	if (ISSET(tmp, 1 << 1))
		height |= 1 << 8;
	if (ISSET(tmp, 1 << 6))
		height |= 1 << 9;
	tmp = sisfb_seq_read(fb, 0x0a);
	if (ISSET(tmp, 1 << 1))
		height |= 1 << 10;
	height++;

	width = sisfb_crtc_read(fb, CRTC_HDISPLE);
	tmp = sisfb_seq_read(fb, 0x0b);
	if (ISSET(tmp, 1 << 2))
		width |= 1 << 8;
	if (ISSET(tmp, 1 << 3))
		width |= 1 << 9;
	width++;
	width <<= 3;

	fbaddr = sisfb_crtc_read(fb, CRTC_STARTADRL) |
	    (sisfb_crtc_read(fb, CRTC_STARTADRH) << 8) |
	    (sisfb_seq_read(fb, 0x0d) << 16) |
	    ((sisfb_seq_read(fb, 0x37) & 0x03) << 24);
	fbaddr <<= 2;
#ifdef SIS_DEBUG
	printf("FBADDR %08x\n", fbaddr);
#endif

	tmp = sisfb_seq_read(fb, 0x06);
	switch (tmp & 0x1c) {
	case 0x00:
		bpp = 8;
		break;
	case 0x04:
		bpp = 15;
		break;
	case 0x08:
		bpp = 16;
		break;
	case 0x10:
		bpp = 32;
		break;
	default:
		return EINVAL;
	}

	ri = &fb->ri;
	ri->ri_width = width;
	ri->ri_height = height;
	ri->ri_depth = bpp;
	ri->ri_stride = (ri->ri_width * ri->ri_depth) / 8;
	ri->ri_flg = RI_CENTER | RI_CLEAR | RI_FULLCLEAR;
	ri->ri_bits = (void *)(bus_space_vaddr(fb->fbt, fb->fbh) + fbaddr);
	ri->ri_hw = fb;

#ifdef __MIPSEL__
	/* swap B and R */
	switch (bpp) {
	case 15:
		ri->ri_rnum = 5;
		ri->ri_rpos = 10;
		ri->ri_gnum = 5;
		ri->ri_gpos = 5;
		ri->ri_bnum = 5;
		ri->ri_bpos = 0;
		break;
	case 16:
		ri->ri_rnum = 5;
		ri->ri_rpos = 11;
		ri->ri_gnum = 6;
		ri->ri_gpos = 5;
		ri->ri_bnum = 5;
		ri->ri_bpos = 0;
		break;
	}
#endif

	bcopy(rasops_cmap, fb->cmap, sizeof(fb->cmap));
	if (bpp == 8)
		sisfb_loadcmap(fb, 0, 256);

	rasops_init(ri, 160, 160);

	strlcpy(fb->wsd.name, "std", sizeof(fb->wsd.name));
	fb->wsd.ncols = ri->ri_cols;
	fb->wsd.nrows = ri->ri_rows;
	fb->wsd.textops = &ri->ri_ops;
	fb->wsd.fontwidth = ri->ri_font->fontwidth;
	fb->wsd.fontheight = ri->ri_font->fontheight;
	fb->wsd.capabilities = ri->ri_caps;

	return 0;
}

/*
 * Colormap handling routines.
 */

void
sisfb_loadcmap(struct sisfb *fb, int baseidx, int count)
{
	uint8_t *cmap = fb->cmap + baseidx * 3;

	bus_space_write_1(fb->iot, fb->ioh, DAC_ADDR, baseidx);
	while (count-- != 0) {
		bus_space_write_1(fb->iot, fb->ioh, DAC_DATA, *cmap++ >> 2);
		bus_space_write_1(fb->iot, fb->ioh, DAC_DATA, *cmap++ >> 2);
		bus_space_write_1(fb->iot, fb->ioh, DAC_DATA, *cmap++ >> 2);
	}
}

int
sisfb_getcmap(uint8_t *cmap, struct wsdisplay_cmap *cm)
{
	uint index = cm->index, count = cm->count, i;
	uint8_t ramp[256], *dst, *src;
	int rc;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	index *= 3;

	src = cmap + index;
	dst = ramp;
	for (i = 0; i < count; i++)
		*dst++ = *src, src += 3;
	rc = copyout(ramp, cm->red, count);
	if (rc != 0)
		return rc;

	src = cmap + index + 1;
	dst = ramp;
	for (i = 0; i < count; i++)
		*dst++ = *src, src += 3;
	rc = copyout(ramp, cm->green, count);
	if (rc != 0)
		return rc;

	src = cmap + index + 2;
	dst = ramp;
	for (i = 0; i < count; i++)
		*dst++ = *src, src += 3;
	rc = copyout(ramp, cm->blue, count);
	if (rc != 0)
		return rc;

	return 0;
}

int
sisfb_putcmap(uint8_t *cmap, struct wsdisplay_cmap *cm)
{
	uint index = cm->index, count = cm->count, i;
	uint8_t ramp[256], *dst, *src;
	int rc;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	index *= 3;

	rc = copyin(cm->red, ramp, count);
	if (rc != 0)
		return rc;
	dst = cmap + index;
	src = ramp;
	for (i = 0; i < count; i++)
		*dst = *src++, dst += 3;

	rc = copyin(cm->green, ramp, count);
	if (rc != 0)
		return rc;
	dst = cmap + index + 1;
	src = ramp;
	for (i = 0; i < count; i++)
		*dst = *src++, dst += 3;

	rc = copyin(cm->blue, ramp, count);
	if (rc != 0)
		return rc;
	dst = cmap + index + 2;
	src = ramp;
	for (i = 0; i < count; i++)
		*dst = *src++, dst += 3;

	return 0;
}

/*
 * Early console code
 */

int sisfb_cnattach(bus_space_tag_t, bus_space_tag_t, pcitag_t, pcireg_t);

int
sisfb_cnattach(bus_space_tag_t memt, bus_space_tag_t iot, pcitag_t tag,
    pcireg_t id)
{
	uint32_t defattr;
	struct rasops_info *ri;
	pcireg_t bar;
	int rc;

	/* filter out unrecognized devices */
	switch (id) {
	default:
		return ENODEV;
	case PCI_ID_CODE(PCI_VENDOR_SIS, PCI_PRODUCT_SIS_315PRO_VGA):
		break;
	}

	bar = pci_conf_read_early(tag, PCI_MAPREG_START);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
		return EINVAL;
	sisfbcn.fbt = memt;
	rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    BUS_SPACE_MAP_LINEAR, &sisfbcn.fbh);
	if (rc != 0)
		return rc;

	bar = pci_conf_read_early(tag, PCI_MAPREG_START + 4);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
		return EINVAL;
	sisfbcn.mmiot = memt;
	rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    BUS_SPACE_MAP_LINEAR, &sisfbcn.mmioh);
	if (rc != 0)
		return rc;

	bar = pci_conf_read_early(tag, PCI_MAPREG_START + 8);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_IO)
		return EINVAL;
	sisfbcn.iot = iot;
	rc = bus_space_map(iot, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    0, &sisfbcn.ioh);
	if (rc != 0)
		return rc;

	rc = sisfb_setup(&sisfbcn);
	if (rc != 0)
		return rc;

	ri = &sisfbcn.ri;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&sisfbcn.wsd, ri, 0, 0, defattr);

	return 0;
}
