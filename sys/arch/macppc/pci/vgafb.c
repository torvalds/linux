/*	$OpenBSD: vgafb.c,v 1.65 2023/04/13 15:07:43 miod Exp $	*/
/*	$NetBSD: vga.c,v 1.3 1996/12/02 22:24:54 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <dev/ofw/openfirm.h>
#include <macppc/macppc/ofw_machdep.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/vga_pcivar.h>

struct vgafb_softc {
	struct device		sc_dev;
	int			sc_node;

	bus_addr_t		sc_mem_addr, sc_mmio_addr;
	bus_size_t		sc_mem_size, sc_mmio_size;

	struct rasops_info	sc_ri;
	uint8_t			sc_cmap[256 * 3];
	u_int			sc_mode;

	struct	wsscreen_descr	sc_wsd;
	struct	wsscreen_list	sc_wsl;
	struct	wsscreen_descr *sc_scrlist[1];

	int			sc_backlight_on;
};

int	vgafb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	vgafb_mmap(void *, off_t, int);
int	vgafb_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);
void	vgafb_free_screen(void *, void *);
int	vgafb_show_screen(void *, void *, int, void (*cb)(void *, int, int),
	    void *);
int	vgafb_load_font(void *, void *, struct wsdisplay_font *);
int	vgafb_list_font(void *, struct wsdisplay_font *);
void	vgafb_burn(void *v, u_int , u_int);
void	vgafb_restore_default_colors(struct vgafb_softc *);
int	vgafb_is_console(int);
int	vgafb_console_init(struct vgafb_softc *);
int	vgafb_mapregs(struct vgafb_softc *, struct pci_attach_args *);

struct wsdisplay_accessops vgafb_accessops = {
	.ioctl = vgafb_ioctl,
	.mmap = vgafb_mmap,
	.alloc_screen = vgafb_alloc_screen,
	.free_screen = vgafb_free_screen,
	.show_screen = vgafb_show_screen,
	.load_font = vgafb_load_font,
	.list_font = vgafb_list_font,
	.burn_screen = vgafb_burn
};

int	vgafb_getcmap(uint8_t *, struct wsdisplay_cmap *);
int	vgafb_putcmap(uint8_t *, struct wsdisplay_cmap *);

int	vgafb_match(struct device *, void *, void *);
void	vgafb_attach(struct device *, struct device *, void *);

const struct cfattach vgafb_ca = {
	sizeof(struct vgafb_softc), vgafb_match, vgafb_attach,
};

struct cfdriver vgafb_cd = {
	NULL, "vgafb", DV_DULL,
};

#ifdef APERTURE
extern int allowaperture;
#endif

int
vgafb_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	int node;

	if (DEVICE_IS_VGA_PCI(pa->pa_class) == 0) {
		/*
		 * XXX Graphic cards found in iMac G3 have a ``Misc''
		 * subclass, match them all.
		 */
		if (PCI_CLASS(pa->pa_class) != PCI_CLASS_DISPLAY ||
		    PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_DISPLAY_MISC)
			return (0);
	}

	/*
	 * XXX Non-console devices do not get configured by the PROM,
	 * XXX so do not attach them yet.
	 */
	node = PCITAG_NODE(pa->pa_tag);
	if (!vgafb_is_console(node))
		return (0);

	return (1);
}

void
vgafb_attach(struct device *parent, struct device *self, void *aux)
{
	struct vgafb_softc *sc = (struct vgafb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct wsemuldisplaydev_attach_args waa;

	sc->sc_node = PCITAG_NODE(pa->pa_tag);

	if (vgafb_mapregs(sc, pa))
		return;

	if (vgafb_console_init(sc))
		return;

	sc->sc_scrlist[0] = &sc->sc_wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	waa.console = 1;
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &vgafb_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	/* no need to keep the burner function if no hw support */
	if (cons_backlight_available == 0)
		vgafb_accessops.burn_screen = NULL;
	else {
		sc->sc_backlight_on = WSDISPLAYIO_VIDEO_OFF;
		vgafb_burn(sc, WSDISPLAYIO_VIDEO_ON, 0);	/* paranoia */
	}

#ifdef RAMDISK_HOOKS
	if (vga_aperture_needed(pa))
		printf("%s: aperture needed\n", sc->sc_dev.dv_xname);
#endif

	config_found(self, &waa, wsemuldisplaydevprint);
}

int
vgafb_console_init(struct vgafb_softc *sc)
{
	struct rasops_info *ri = &sc->sc_ri;
	uint32_t defattr;

	ri->ri_flg = RI_CENTER | RI_VCONS | RI_WRONLY;
	ri->ri_hw = sc;

	ofwconsswitch(ri);

	rasops_init(ri, 160, 160);

	strlcpy(sc->sc_wsd.name, "std", sizeof(sc->sc_wsd.name));
	sc->sc_wsd.capabilities = ri->ri_caps;
	sc->sc_wsd.nrows = ri->ri_rows;
	sc->sc_wsd.ncols = ri->ri_cols;
	sc->sc_wsd.textops = &ri->ri_ops;
	sc->sc_wsd.fontwidth = ri->ri_font->fontwidth;
	sc->sc_wsd.fontheight = ri->ri_font->fontheight;

	ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&sc->sc_wsd, ri->ri_active, ri->ri_ccol, ri->ri_crow,
	    defattr);

	return (0);
}

void
vgafb_restore_default_colors(struct vgafb_softc *sc)
{
	bcopy(rasops_cmap, sc->sc_cmap, sizeof(sc->sc_cmap));
	of_setcolors(sc->sc_cmap, 0, 256);
}

int
vgafb_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int rc;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_PCIVGA;
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
		rc = vgafb_getcmap(sc->sc_cmap, cm);
		if (rc != 0)
			return rc;
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		rc = vgafb_putcmap(sc->sc_cmap, cm);
		if (rc != 0)
			return (rc);
		if (ri->ri_depth == 8)
			of_setcolors(sc->sc_cmap, cm->index, cm->count);
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (ri->ri_depth == 8)
			vgafb_restore_default_colors(sc);
		break;
	case WSDISPLAYIO_GETPARAM:
	{
		struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			if (cons_backlight_available != 0) {
				dp->min = MIN_BRIGHTNESS;
				dp->max = MAX_BRIGHTNESS;
				dp->curval = cons_brightness;
				return 0;
			}
			return -1;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			if (cons_backlight_available != 0) {
				dp->min = 0;
				dp->max = 1;
				dp->curval = sc->sc_backlight_on;
				return 0;
			} else
				return -1;
		}
	}
		return -1;

	case WSDISPLAYIO_SETPARAM:
	{
		struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			if (cons_backlight_available == 1) {
				of_setbrightness(dp->curval);
				return 0;
			} else
				return -1;
		case WSDISPLAYIO_PARAM_BACKLIGHT:
			if (cons_backlight_available != 0) {
				vgafb_burn(sc,
				    dp->curval ? WSDISPLAYIO_VIDEO_ON :
				      WSDISPLAYIO_VIDEO_OFF, 0);
				return 0;
			} else
				return -1;
		}
	}
		return -1;

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

paddr_t
vgafb_mmap(void *v, off_t off, int prot)
{
	struct vgafb_softc *sc = v;

	if (off & PGOFSET)
		return (-1);

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
#ifdef APERTURE
		if (allowaperture == 0)
			return (-1);
#endif

		if (sc->sc_mmio_size == 0)
			return (-1);

		if (off >= sc->sc_mem_addr &&
		    off < (sc->sc_mem_addr + sc->sc_mem_size))
			return (off);

		if (off >= sc->sc_mmio_addr &&
		    off < (sc->sc_mmio_addr + sc->sc_mmio_size))
			return (off);
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if (off >= 0x00000 && off < sc->sc_mem_size)
			return (sc->sc_mem_addr + off);
		break;

	}

	return (-1);
}

int
vgafb_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
vgafb_getcmap(uint8_t *cmap, struct wsdisplay_cmap *cm)
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
vgafb_putcmap(uint8_t *cmap, struct wsdisplay_cmap *cm)
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

void
vgafb_burn(void *v, u_int on, u_int flags)
{
	struct vgafb_softc *sc = v;

	if (sc->sc_backlight_on != on) {
		of_setbacklight(on == WSDISPLAYIO_VIDEO_ON);
		sc->sc_backlight_on = on;
	}
}

int
vgafb_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_alloc_screen(ri, cookiep, curxp, curyp, attrp);
}

void
vgafb_free_screen(void *v, void *cookie)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_free_screen(ri, cookie);
}

int
vgafb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	if (cookie == ri->ri_active)
		return (0);

	return rasops_show_screen(ri, cookie, waitok, cb, cbarg);
}

int
vgafb_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
vgafb_list_font(void *v, struct wsdisplay_font *font)
{
	struct vgafb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_ri;

	return rasops_list_font(ri, font);
}

int
vgafb_mapregs(struct vgafb_softc *sc, struct pci_attach_args *pa)
{
	bus_addr_t ba;
	bus_size_t bs;
	int hasmem = 0, hasmmio = 0;
	uint32_t bar, cf;
	int rv;

	/*
	 * Look for the first 2 mem regions.  For r128, this skips the
	 * io region 0x14 and finds frame memory 0x10 and mmio 0x18.
	 * For nvidia, this finds mmio 0x10 and frame memory 0x14.
	 * Some nvidias have a 3rd mem region 0x18, which we ignore.
	 */
	for (bar = PCI_MAPREG_START; bar <= PCI_MAPREG_PPB_END; bar += 4) {
		cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar);
		if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_MEM) {
			/* Memory mapping... frame memory or mmio? */
			rv = pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar,
			    _PCI_MAPREG_TYPEBITS(cf), &ba, &bs, NULL);
			if (rv != 0)
				continue;

			if (bs == 0 /* || ba == 0 */) {
				/* ignore this entry */
			} else if (hasmem == 0) {
				/*
				 * first memory slot found goes into memory,
				 * this is for the case of no mmio
				 */
				sc->sc_mem_addr = ba;
				sc->sc_mem_size = bs;
				hasmem = 1;
			} else {
				/*
				 * Oh, we have a second `memory'
				 * region, is this region the vga memory
				 * or mmio, we guess that memory is
				 * the larger of the two.
				 */
				if (sc->sc_mem_size >= bs) {
					/* this is the mmio */
					sc->sc_mmio_addr = ba;
					sc->sc_mmio_size = bs;
					hasmmio = 1;
				} else {
					/* this is the memory */
					sc->sc_mmio_addr = sc->sc_mem_addr;
					sc->sc_mmio_size = sc->sc_mem_size;
					sc->sc_mem_addr = ba;
					sc->sc_mem_size = bs;
				}
				/* Ignore any other mem region. */
				break;
			}
			if (PCI_MAPREG_MEM_TYPE(cf) ==
			    PCI_MAPREG_MEM_TYPE_64BIT)
				bar += 4;
		}
	}

	/* failure to initialize io ports should not prevent attachment */
	if (hasmem == 0) {
		printf(": could not find memory space\n");
		return (1);
	}

	if (hasmmio)
		printf (", mmio");
	printf("\n");

	return (0);
}
