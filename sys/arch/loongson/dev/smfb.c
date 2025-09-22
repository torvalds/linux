/*	$OpenBSD: smfb.c,v 1.23 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
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
 * SiliconMotion SM502 and SM712 frame buffer driver.
 *
 * Assumes its video output is an LCD panel, in 5:6:5 mode, and fixed
 * 1024x600 (Yeeloong) or 1368x768 (Lynloong) or 800x480 (EBT700)
 * resolution depending on the system model.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#include <dev/ic/vgareg.h>
#include <dev/isa/isareg.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <loongson/dev/voyagerreg.h>
#include <loongson/dev/voyagervar.h>
#include <loongson/dev/smfbreg.h>

struct smfb_softc;

/* minimal frame buffer information, suitable for early console */
struct smfb {
	struct smfb_softc	*sc;
	struct rasops_info	ri;
	int			is5xx;

	/* DPR registers */
	bus_space_tag_t		dprt;
	bus_space_handle_t	dprh;
	/* MMIO space (SM7xx) or control registers (SM5xx) */
	bus_space_tag_t		mmiot;
	bus_space_handle_t	mmioh;
	/* DCR registers (SM5xx) */
	bus_space_tag_t		dcrt;
	bus_space_handle_t	dcrh;

	struct wsscreen_descr	wsd;
};

#define	DCR_READ(fb, reg) \
	bus_space_read_4((fb)->dcrt, (fb)->dcrh, (reg))
#define	DCR_WRITE(fb, reg, val) \
	bus_space_write_4((fb)->dcrt, (fb)->dcrh, (reg), (val))
#define	DPR_READ(fb, reg) \
	bus_space_read_4((fb)->dprt, (fb)->dprh, (reg))
#define	DPR_WRITE(fb, reg, val) \
	bus_space_write_4((fb)->dprt, (fb)->dprh, (reg), (val))

struct smfb_softc {
	struct device		 sc_dev;
	struct smfb		*sc_fb;
	struct smfb		 sc_fb_store;

	struct wsscreen_list	 sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
	int			 sc_nscr;
};

int	smfb_pci_match(struct device *, void *, void *);
void	smfb_pci_attach(struct device *, struct device *, void *);
int	smfb_voyager_match(struct device *, void *, void *);
void	smfb_voyager_attach(struct device *, struct device *, void *);
int	smfb_activate(struct device *, int);

const struct cfattach smfb_pci_ca = {
	sizeof(struct smfb_softc), smfb_pci_match, smfb_pci_attach,
	NULL, smfb_activate
};

const struct cfattach smfb_voyager_ca = {
	sizeof(struct smfb_softc), smfb_voyager_match, smfb_voyager_attach,
	smfb_activate
};

struct cfdriver smfb_cd = {
	NULL, "smfb", DV_DULL
};

int	smfb_alloc_screen(void *, const struct wsscreen_descr *, void **, int *,
	    int *, uint32_t *);
void	smfb_burner(void *, uint, uint);
void	smfb_free_screen(void *, void *);
int	smfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	smfb_list_font(void *, struct wsdisplay_font *);
int	smfb_load_font(void *, void *, struct wsdisplay_font *);
paddr_t	smfb_mmap(void *, off_t, int);
int	smfb_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

struct wsdisplay_accessops smfb_accessops = {
	.ioctl = smfb_ioctl,
	.mmap = smfb_mmap,
	.alloc_screen = smfb_alloc_screen,
	.free_screen = smfb_free_screen,
	.show_screen = smfb_show_screen,
	.load_font = smfb_load_font,
	.list_font = smfb_list_font,
	.burn_screen = smfb_burner
};

int	smfb_setup(struct smfb *, bus_space_tag_t, bus_space_handle_t,
	    bus_space_tag_t, bus_space_handle_t);

void	smfb_copyrect(struct smfb *, int, int, int, int, int, int);
void	smfb_fillrect(struct smfb *, int, int, int, int, int);
int	smfb_copyrows(void *, int, int, int);
int	smfb_copycols(void *, int, int, int, int);
int	smfb_erasecols(void *, int, int, int, uint32_t);
int	smfb_eraserows(void *, int, int, uint32_t);
int	smfb_wait(struct smfb *);

void	smfb_wait_panel_vsync(struct smfb *, int);
uint8_t	smfb_vgats_read(struct smfb *, uint);
void	smfb_vgats_write(struct smfb *, uint, uint8_t);

void	smfb_attach_common(struct smfb_softc *, int, bus_space_tag_t,
	    bus_space_handle_t, bus_space_tag_t, bus_space_handle_t);

static struct smfb smfbcn;

const struct pci_matchid smfb_devices[] = {
	{ PCI_VENDOR_SMI, PCI_PRODUCT_SMI_SM712 }
};

int
smfb_pci_match(struct device *parent, void *vcf, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	return pci_matchbyid(pa, smfb_devices, nitems(smfb_devices));
}

int
smfb_voyager_match(struct device *parent, void *vcf, void *aux)
{
	struct voyager_attach_args *vaa = (struct voyager_attach_args *)aux;
	struct cfdata *cf = (struct cfdata *)vcf;

	return strcmp(vaa->vaa_name, cf->cf_driver->cd_name) == 0;
}

void
smfb_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct smfb_softc *sc = (struct smfb_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	bus_space_tag_t memt;
	bus_space_handle_t memh;

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &memt, &memh, NULL, NULL, 0) != 0) {
		printf(": can't map frame buffer\n");
		return;
	}

	smfb_attach_common(sc, 0, memt, memh, memt, memh);
}

void
smfb_voyager_attach(struct device *parent, struct device *self, void *aux)
{
	struct smfb_softc *sc = (struct smfb_softc *)self;
	struct voyager_attach_args *vaa = (struct voyager_attach_args *)aux;

	smfb_attach_common(sc, 1, vaa->vaa_fbt, vaa->vaa_fbh, vaa->vaa_mmiot,
	    vaa->vaa_mmioh);
}

void
smfb_attach_common(struct smfb_softc *sc, int is5xx, bus_space_tag_t memt,
    bus_space_handle_t memh, bus_space_tag_t mmiot, bus_space_handle_t mmioh)
{
	struct wsemuldisplaydev_attach_args waa;
	int console;

	console = smfbcn.ri.ri_hw != NULL;

	if (console) {
		sc->sc_fb = &smfbcn;
		sc->sc_fb->sc = sc;
	} else {
		sc->sc_fb = &sc->sc_fb_store;
		sc->sc_fb->is5xx = is5xx;
		if (smfb_setup(sc->sc_fb, memt, memh, mmiot, mmioh) != 0) {
			printf(": can't setup frame buffer\n");
			return;
		}
	}

	printf(": %dx%d, %dbpp\n", sc->sc_fb->ri.ri_width,
	    sc->sc_fb->ri.ri_height, sc->sc_fb->ri.ri_depth);

	sc->sc_scrlist[0] = &sc->sc_fb->wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	waa.console = console;
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &smfb_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	config_found((struct device *)sc, &waa, wsemuldisplaydevprint);
}

/*
 * wsdisplay accessops
 */

int
smfb_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
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
smfb_free_screen(void *v, void *cookie)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;

	sc->sc_nscr--;
}

int
smfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(uint *)data = WSDISPLAY_TYPE_SMFB;
		break;
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
		*(uint *)data = ri->ri_stride;
		break;
	default:
		return -1;
	}

	return 0;
}

int
smfb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return 0;
}

paddr_t
smfb_mmap(void *v, off_t offset, int prot)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	if ((offset & PAGE_MASK) != 0)
		return -1;

	if (offset < 0 || offset >= ri->ri_stride * ri->ri_height)
		return -1;

	return XKPHYS_TO_PHYS((paddr_t)ri->ri_bits) + offset;
}

int
smfb_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
smfb_list_font(void *v, struct wsdisplay_font *font)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_list_font(ri, font);
}

void
smfb_burner(void *v, uint on, uint flg)
{
	struct smfb_softc *sc = (struct smfb_softc *)v;
	struct smfb *fb = sc->sc_fb;

	if (fb->is5xx) {
		if (on) {
			/*
			 * Wait for a few cycles after restoring power,
			 * to prevent white flickering.
			 */
			DCR_WRITE(fb, DCR_PANEL_DISPLAY_CONTROL,
			    DCR_READ(fb, DCR_PANEL_DISPLAY_CONTROL) | PDC_VDD);
			smfb_wait_panel_vsync(fb, 4);
			DCR_WRITE(fb, DCR_PANEL_DISPLAY_CONTROL,
			    DCR_READ(fb, DCR_PANEL_DISPLAY_CONTROL) | PDC_DATA);
			smfb_wait_panel_vsync(fb, 4);
			DCR_WRITE(fb, DCR_PANEL_DISPLAY_CONTROL,
			    DCR_READ(fb, DCR_PANEL_DISPLAY_CONTROL) |
			    (PDC_BIAS | PDC_EN));
		} else
			DCR_WRITE(fb, DCR_PANEL_DISPLAY_CONTROL,
			    DCR_READ(fb, DCR_PANEL_DISPLAY_CONTROL) &
			    ~(PDC_EN | PDC_BIAS | PDC_DATA | PDC_VDD));
	} else {
		if (on) {
			smfb_vgats_write(fb, 0x31,
			    smfb_vgats_read(fb, 0x31) | 0x01);
		} else {
			smfb_vgats_write(fb, 0x21,
			    smfb_vgats_read(fb, 0x21) | 0x30);
			smfb_vgats_write(fb, 0x31,
			    smfb_vgats_read(fb, 0x31) & ~0x01);
		}
	}
}

/*
 * Frame buffer initialization.
 */

int
smfb_setup(struct smfb *fb, bus_space_tag_t memt, bus_space_handle_t memh,
    bus_space_tag_t mmiot, bus_space_handle_t mmioh)
{
	struct rasops_info *ri;
	int accel = 0;
	int rc;

	ri = &fb->ri;
	switch (sys_platform->system_type) {
	case LOONGSON_EBT700:
		ri->ri_width = 800;
		ri->ri_height = 480;
		break;
	case LOONGSON_LYNLOONG:
		ri->ri_width = 1368;
		ri->ri_height = 768;
		break;
	default:
	case LOONGSON_GDIUM:
	case LOONGSON_YEELOONG:
		ri->ri_width = 1024;
		ri->ri_height = 600;
		break;
	}
	ri->ri_depth = 16;
	ri->ri_stride = (ri->ri_width * ri->ri_depth) / 8;
	ri->ri_flg = RI_CENTER | RI_CLEAR | RI_FULLCLEAR;
	ri->ri_bits = (void *)bus_space_vaddr(memt, memh);
	ri->ri_hw = fb;

#ifdef __MIPSEL__
	/* swap B and R */
	ri->ri_rnum = 5;
	ri->ri_rpos = 11;
	ri->ri_gnum = 6;
	ri->ri_gpos = 5;
	ri->ri_bnum = 5;
	ri->ri_bpos = 0;
#endif

	rasops_init(ri, 160, 160);

	strlcpy(fb->wsd.name, "std", sizeof(fb->wsd.name));
	fb->wsd.ncols = ri->ri_cols;
	fb->wsd.nrows = ri->ri_rows;
	fb->wsd.textops = &ri->ri_ops;
	fb->wsd.fontwidth = ri->ri_font->fontwidth;
	fb->wsd.fontheight = ri->ri_font->fontheight;
	fb->wsd.capabilities = ri->ri_caps;

	if (fb->is5xx) {
		fb->dcrt = mmiot;
		if ((rc = bus_space_subregion(mmiot, mmioh, SM5XX_DCR_BASE,
		    SM5XX_DCR_SIZE, &fb->dcrh)) != 0)
			return rc;
		fb->dprt = mmiot;
		if ((rc = bus_space_subregion(mmiot, mmioh, SM5XX_DPR_BASE,
		    SMXXX_DPR_SIZE, &fb->dprh)) != 0)
			return rc;
		fb->mmiot = mmiot;
		if ((rc = bus_space_subregion(mmiot, mmioh, SM5XX_MMIO_BASE,
		    SM5XX_MMIO_SIZE, &fb->mmioh)) != 0)
			return rc;
		accel = 1;
	} else {
		fb->dprt = memt;
		if ((rc = bus_space_subregion(memt, memh, SM7XX_DPR_BASE,
		    SMXXX_DPR_SIZE, &fb->dprh)) != 0)
			return rc;
		fb->mmiot = memt;
		if ((rc = bus_space_subregion(memt, memh, SM7XX_MMIO_BASE,
		    SM7XX_MMIO_SIZE, &fb->mmioh)) != 0)
			return rc;
		accel = 1;
	}

	/*
	 * Setup 2D acceleration whenever possible
	 */

	if (accel) {
		if (smfb_wait(fb) != 0)
			accel = 0;
	}
	if (accel) {
		DPR_WRITE(fb, DPR_CROP_TOPLEFT_COORDS, DPR_COORDS(0, 0));
		/* use of width both times is intentional */
		DPR_WRITE(fb, DPR_PITCH,
		    DPR_COORDS(ri->ri_width, ri->ri_width));
		DPR_WRITE(fb, DPR_SRC_WINDOW,
		    DPR_COORDS(ri->ri_width, ri->ri_width));
		DPR_WRITE(fb, DPR_BYTE_BIT_MASK, 0xffffffff);
		DPR_WRITE(fb, DPR_COLOR_COMPARE_MASK, 0);
		DPR_WRITE(fb, DPR_COLOR_COMPARE, 0);
		DPR_WRITE(fb, DPR_SRC_BASE, 0);
		DPR_WRITE(fb, DPR_DST_BASE, 0);
		DPR_READ(fb, DPR_DST_BASE);

		ri->ri_ops.copycols = smfb_copycols;
		ri->ri_ops.copyrows = smfb_copyrows;
		ri->ri_ops.erasecols = smfb_erasecols;
		ri->ri_ops.eraserows = smfb_eraserows;
	}

	return 0;
}

void
smfb_copyrect(struct smfb *fb, int sx, int sy, int dx, int dy, int w, int h)
{
	uint32_t dir;

	/* Compute rop direction */
	if (sy < dy || (sy == dy && sx <= dx)) {
		sx += w - 1;
		dx += w - 1;
		sy += h - 1;
		dy += h - 1;
		dir = DE_CTRL_RTOL;
	} else
		dir = 0;

	DPR_WRITE(fb, DPR_SRC_COORDS, DPR_COORDS(sx, sy));
	DPR_WRITE(fb, DPR_DST_COORDS, DPR_COORDS(dx, dy));
	DPR_WRITE(fb, DPR_SPAN_COORDS, DPR_COORDS(w, h));
	DPR_WRITE(fb, DPR_DE_CTRL, DE_CTRL_START | DE_CTRL_ROP_ENABLE | dir |
	    (DE_CTRL_COMMAND_BITBLT << DE_CTRL_COMMAND_SHIFT) |
	    (DE_CTRL_ROP_SRC << DE_CTRL_ROP_SHIFT));
	DPR_READ(fb, DPR_DE_CTRL);

	smfb_wait(fb);
}

void
smfb_fillrect(struct smfb *fb, int x, int y, int w, int h, int bg)
{
	struct rasops_info *ri;

	ri = &fb->ri;

	DPR_WRITE(fb, DPR_FG_COLOR, ri->ri_devcmap[bg]);
	DPR_WRITE(fb, DPR_DST_COORDS, DPR_COORDS(x, y));
	DPR_WRITE(fb, DPR_SPAN_COORDS, DPR_COORDS(w, h));
	DPR_WRITE(fb, DPR_DE_CTRL, DE_CTRL_START | DE_CTRL_ROP_ENABLE |
	    (DE_CTRL_COMMAND_SOLIDFILL << DE_CTRL_COMMAND_SHIFT) |
	    (DE_CTRL_ROP_SRC << DE_CTRL_ROP_SHIFT));
	DPR_READ(fb, DPR_DE_CTRL);

	smfb_wait(fb);
}

int
smfb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct smfb *fb = ri->ri_hw;
	struct wsdisplay_font *f = ri->ri_font;

	num *= f->fontheight;
	src *= f->fontheight;
	dst *= f->fontheight;

	smfb_copyrect(fb, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
smfb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct smfb *fb = ri->ri_hw;
	struct wsdisplay_font *f = ri->ri_font;

	num *= f->fontwidth;
	src *= f->fontwidth;
	dst *= f->fontwidth;
	row *= f->fontheight;

	smfb_copyrect(fb, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row, num, f->fontheight);

	return 0;
}

int
smfb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct smfb *fb = ri->ri_hw;
	struct wsdisplay_font *f = ri->ri_font;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= f->fontheight;
	col *= f->fontwidth;
	num *= f->fontwidth;

	smfb_fillrect(fb, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, f->fontheight, bg);

	return 0;
}

int
smfb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct smfb *fb = ri->ri_hw;
	struct wsdisplay_font *f = ri->ri_font;
	int bg, fg;
	int x, y, w;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	if ((num == ri->ri_rows) && ISSET(ri->ri_flg, RI_FULLCLEAR)) {
		num = ri->ri_height;
		x = y = 0;
		w = ri->ri_width;
	} else {
		num *= f->fontheight;
		x = ri->ri_xorigin;
		y = ri->ri_yorigin + row * f->fontheight;
		w = ri->ri_emuwidth;
	}
	smfb_fillrect(fb, x, y, w, num, bg);

	return 0;
}

int
smfb_wait(struct smfb *fb)
{
	uint32_t reg;
	int i;

	i = 10000;
	while (i-- != 0) {
		if (fb->is5xx) {
			reg = bus_space_read_4(fb->mmiot, fb->mmioh,
			    VOYAGER_SYSTEM_CONTROL);
			if ((reg & (VSC_FIFO_EMPTY | VSC_2DENGINE_BUSY)) ==
			    VSC_FIFO_EMPTY)
				return 0;
		} else {
			reg = smfb_vgats_read(fb, 0x16);
			if ((reg & 0x18) == 0x10)
				return 0;
		}
		delay(1);
	}

	return EBUSY;
}

/*
 * wait for a few panel vertical retrace cycles (5xx only)
 */
void
smfb_wait_panel_vsync(struct smfb *fb, int ncycles)
{
	while (ncycles-- != 0) {
		/* wait for end of retrace-in-progress */
		while (ISSET(bus_space_read_4(fb->mmiot, fb->mmioh,
		    VOYAGER_COMMANDLIST_STATUS), VCS_SP))
			delay(10);
		/* wait for start of retrace */
		while (!ISSET(bus_space_read_4(fb->mmiot, fb->mmioh,
		    VOYAGER_COMMANDLIST_STATUS), VCS_SP))
			delay(10);
	}
}

/*
 * vga sequencer access through mmio space (non-5xx only)
 */

uint8_t
smfb_vgats_read(struct smfb *fb, uint regno)
{
	bus_space_write_1(fb->mmiot, fb->mmioh, IO_VGA + VGA_TS_INDEX, regno);
	return bus_space_read_1(fb->mmiot, fb->mmioh, IO_VGA + VGA_TS_DATA);
}

void
smfb_vgats_write(struct smfb *fb, uint regno, uint8_t value)
{
	bus_space_write_1(fb->mmiot, fb->mmioh, IO_VGA + VGA_TS_INDEX, regno);
	bus_space_write_1(fb->mmiot, fb->mmioh, IO_VGA + VGA_TS_DATA, value);
}

/*
 * Early console code
 */

int smfb_cnattach(bus_space_tag_t, bus_space_tag_t, pcitag_t, pcireg_t);

int
smfb_cnattach(bus_space_tag_t memt, bus_space_tag_t iot, pcitag_t tag,
    pcireg_t id)
{
	uint32_t defattr;
	struct rasops_info *ri;
	bus_space_handle_t fbh, mmioh;
	pcireg_t bar;
	int rc, is5xx;

	/* filter out unrecognized devices */
	switch (id) {
	default:
		return ENODEV;
	case PCI_ID_CODE(PCI_VENDOR_SMI, PCI_PRODUCT_SMI_SM712):
		is5xx = 0;
		break;
	case PCI_ID_CODE(PCI_VENDOR_SMI, PCI_PRODUCT_SMI_SM501):
		is5xx = 1;
		break;
	}

	smfbcn.is5xx = is5xx;

	bar = pci_conf_read_early(tag, PCI_MAPREG_START);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
		return EINVAL;
	rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    BUS_SPACE_MAP_LINEAR, &fbh);
	if (rc != 0)
		return rc;

	if (smfbcn.is5xx) {
		bar = pci_conf_read_early(tag, PCI_MAPREG_START + 0x04);
		if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
			return EINVAL;
		rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
		    BUS_SPACE_MAP_LINEAR, &mmioh);
		if (rc != 0)
			return rc;
	} else {
		mmioh = fbh;
	}

	rc = smfb_setup(&smfbcn, memt, fbh, memt, mmioh);
	if (rc != 0)
		return rc;

	ri = &smfbcn.ri;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&smfbcn.wsd, ri, 0, 0, defattr);

	return 0;
}

int
smfb_activate(struct device *self, int act)
{
	struct smfb_softc *sc = (struct smfb_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		smfb_burner(sc, 0, 0);
		break;
	case DVACT_RESUME:
		smfb_burner(sc, 1, 0);
		break;
	}

	return 0;
}
