/*	$OpenBSD: cgsix.c,v 1.62 2022/07/15 17:57:27 kettenis Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/sbus/sbusvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>
#include <dev/sbus/cgsixreg.h>
#include <dev/ic/bt458reg.h>

int cgsix_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t cgsix_mmap(void *, off_t, int);
int cgsix_is_console(int);
int cg6_bt_getcmap(union bt_cmap *, struct wsdisplay_cmap *);
int cg6_bt_putcmap(union bt_cmap *, struct wsdisplay_cmap *);
void cgsix_loadcmap_immediate(struct cgsix_softc *, u_int, u_int);
void cgsix_loadcmap_deferred(struct cgsix_softc *, u_int, u_int);
void cgsix_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
void cgsix_reset(struct cgsix_softc *, u_int32_t);
void cgsix_hardreset(struct cgsix_softc *);
void cgsix_burner(void *, u_int, u_int);
int cgsix_intr(void *);
void cgsix_ras_init(struct cgsix_softc *);
int cgsix_ras_copyrows(void *, int, int, int);
int cgsix_ras_copycols(void *, int, int, int, int);
int cgsix_ras_erasecols(void *, int, int, int, uint32_t);
int cgsix_ras_eraserows(void *, int, int, uint32_t);
int cgsix_ras_do_cursor(struct rasops_info *);
int cgsix_setcursor(struct cgsix_softc *, struct wsdisplay_cursor *);
int cgsix_updatecursor(struct cgsix_softc *, u_int);

struct wsdisplay_accessops cgsix_accessops = {
	.ioctl = cgsix_ioctl,
	.mmap = cgsix_mmap,
	.burn_screen = cgsix_burner
};

int	cgsixmatch(struct device *, void *, void *);
void	cgsixattach(struct device *, struct device *, void *);

const struct cfattach cgsix_ca = {
	sizeof (struct cgsix_softc), cgsixmatch, cgsixattach
};

struct cfdriver cgsix_cd = {
	NULL, "cgsix", DV_DULL
};

int
cgsixmatch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

void    
cgsixattach(struct device *parent, struct device *self, void *aux)
{
	struct cgsix_softc *sc = (struct cgsix_softc *)self;
	struct sbus_attach_args *sa = aux;
	int node, console;
	u_int32_t fhc, rev;
	const char *nam;

	node = sa->sa_node;
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_paddr = sbus_bus_addr(sa->sa_bustag, sa->sa_slot, sa->sa_offset);

	if (sa->sa_nreg != 1) {
		printf(": expected %d registers, got %d\n", 1, sa->sa_nreg);
		goto fail;
	}

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	/*
	 * Map just BT, FHC, FBC, THC, and video RAM.
	 */
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_BT_OFFSET,
	    CGSIX_BT_SIZE, 0, 0, &sc->sc_bt_regs) != 0) {
		printf(": cannot map bt registers\n");
		goto fail_bt;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_FHC_OFFSET,
	    CGSIX_FHC_SIZE, 0, 0, &sc->sc_fhc_regs) != 0) {
		printf(": cannot map fhc registers\n");
		goto fail_fhc;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_THC_OFFSET,
	    CGSIX_THC_SIZE, 0, 0, &sc->sc_thc_regs) != 0) {
		printf(": cannot map thc registers\n");
		goto fail_thc;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_VID_OFFSET,
	    sc->sc_sunfb.sf_fbsize, BUS_SPACE_MAP_LINEAR,
	    0, &sc->sc_vid_regs) != 0) {
		printf(": cannot map vid registers\n");
		goto fail_vid;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_TEC_OFFSET,
	    CGSIX_TEC_SIZE, 0, 0, &sc->sc_tec_regs) != 0) {
		printf(": cannot map tec registers\n");
		goto fail_tec;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset + CGSIX_FBC_OFFSET,
	    CGSIX_FBC_SIZE, 0, 0, &sc->sc_fbc_regs) != 0) {
		printf(": cannot map fbc registers\n");
		goto fail_fbc;
	}

	if ((sc->sc_ih = bus_intr_establish(sa->sa_bustag, sa->sa_pri,
	    IPL_TTY, 0, cgsix_intr, sc, self->dv_xname)) == NULL) {
		printf(": couldn't establish interrupt, pri %d\n%s",
		    INTLEV(sa->sa_pri), self->dv_xname);
	}

	/* if prom didn't initialize us, do it the hard way */
	if (OF_getproplen(node, "width") != sizeof(u_int32_t))
		cgsix_hardreset(sc);

	nam = getpropstring(node, "model");
	if (*nam == '\0')
		nam = sa->sa_name;
	printf(": %s", nam);

	console = cgsix_is_console(node);

	fhc = FHC_READ(sc);
	rev = (fhc & FHC_REV_MASK) >> FHC_REV_SHIFT;
	cgsix_reset(sc, rev);

	cgsix_burner(sc, 1, 0);

	sc->sc_sunfb.sf_ro.ri_bits = (void *)bus_space_vaddr(sc->sc_bustag,
	    sc->sc_vid_regs);
	sc->sc_sunfb.sf_ro.ri_hw = sc;

	printf(", %dx%d, rev %d\n", sc->sc_sunfb.sf_width,
	    sc->sc_sunfb.sf_height, rev);

	fbwscons_init(&sc->sc_sunfb, 0, console);
	fbwscons_setcolormap(&sc->sc_sunfb, cgsix_setcolor);

	/*
	 * Old rev. cg6 cards do not like the current acceleration code.
	 *
	 * Some hints from Sun point out at timing and cache problems, which
	 * will be investigated later.
	 */
	if (rev < 5)
		sc->sc_sunfb.sf_dev.dv_cfdata->cf_flags |= CG6_CFFLAG_NOACCEL;

	if ((sc->sc_sunfb.sf_dev.dv_cfdata->cf_flags & CG6_CFFLAG_NOACCEL)
	    == 0) {
		sc->sc_sunfb.sf_ro.ri_ops.copyrows = cgsix_ras_copyrows;
		sc->sc_sunfb.sf_ro.ri_ops.copycols = cgsix_ras_copycols;
		sc->sc_sunfb.sf_ro.ri_ops.eraserows = cgsix_ras_eraserows;
		sc->sc_sunfb.sf_ro.ri_ops.erasecols = cgsix_ras_erasecols;
		sc->sc_sunfb.sf_ro.ri_do_cursor = cgsix_ras_do_cursor;
		cgsix_ras_init(sc);
	}

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &cgsix_accessops, console);

	return;

fail_fbc:
	bus_space_unmap(sa->sa_bustag, sc->sc_tec_regs, CGSIX_TEC_SIZE);
fail_tec:
	bus_space_unmap(sa->sa_bustag, sc->sc_vid_regs, sc->sc_sunfb.sf_fbsize);
fail_vid:
	bus_space_unmap(sa->sa_bustag, sc->sc_thc_regs, CGSIX_THC_SIZE);
fail_thc:
	bus_space_unmap(sa->sa_bustag, sc->sc_fhc_regs, CGSIX_FHC_SIZE);
fail_fhc:
	bus_space_unmap(sa->sa_bustag, sc->sc_bt_regs, CGSIX_BT_SIZE);
fail_bt:
fail:
	return;
}

int
cgsix_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct cgsix_softc *sc = v;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	struct wsdisplay_cursor *curs;
	struct wsdisplay_curpos *pos;
	u_char r[2], g[2], b[2];
	int error, s;
	u_int mode;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_SUNCG6;
		break;
	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if ((sc->sc_sunfb.sf_dev.dv_cfdata->cf_flags &
		    CG6_CFFLAG_NOACCEL) == 0) {
			if (sc->sc_mode != WSDISPLAYIO_MODE_EMUL &&
			    mode == WSDISPLAYIO_MODE_EMUL)
				cgsix_ras_init(sc);
		}
		sc->sc_mode = mode;
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
	case WSDISPLAYIO_GETCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = cg6_bt_getcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = cg6_bt_putcmap(&sc->sc_cmap, cm);
		if (error)
			return (error);
		/* if we can handle interrupts, defer the update */
		if (sc->sc_ih != NULL)
			cgsix_loadcmap_deferred(sc, cm->index, cm->count);
		else
			cgsix_loadcmap_immediate(sc, cm->index, cm->count);
		break;
	case WSDISPLAYIO_SCURSOR:
		curs = (struct wsdisplay_cursor *)data;
		return (cgsix_setcursor(sc, curs));
	case WSDISPLAYIO_GCURSOR:
		curs = (struct wsdisplay_cursor *)data;
		if (curs->which & WSDISPLAY_CURSOR_DOCUR)
			curs->enable = sc->sc_curs_enabled;
		if (curs->which & WSDISPLAY_CURSOR_DOPOS) {
			curs->pos.x = sc->sc_curs_pos.x;
			curs->pos.y = sc->sc_curs_pos.y;
		}
		if (curs->which & WSDISPLAY_CURSOR_DOHOT) {
			curs->hot.x = sc->sc_curs_hot.x;
			curs->hot.y = sc->sc_curs_hot.y;
		}
		if (curs->which & WSDISPLAY_CURSOR_DOCMAP) {
			curs->cmap.index = 0;
			curs->cmap.count = 2;
			r[0] = sc->sc_curs_fg >> 16;
			g[0] = sc->sc_curs_fg >> 8;
			b[0] = sc->sc_curs_fg >> 0;
			r[1] = sc->sc_curs_bg >> 16;
			g[1] = sc->sc_curs_bg >> 8;
			b[1] = sc->sc_curs_bg >> 0;
			error = copyout(r, curs->cmap.red, sizeof(r));
			if (error)
				return (error);
			error = copyout(g, curs->cmap.green, sizeof(g));
			if (error)
				return (error);
			error = copyout(b, curs->cmap.blue, sizeof(b));
			if (error)
				return (error);
		}
		if (curs->which & WSDISPLAY_CURSOR_DOSHAPE) {
			size_t l;

			curs->size.x = sc->sc_curs_size.x;
			curs->size.y = sc->sc_curs_size.y;
			l = (sc->sc_curs_size.x * sc->sc_curs_size.y) / NBBY;
			error = copyout(sc->sc_curs_image, curs->image, l);
			if (error)
				return (error);
			error = copyout(sc->sc_curs_mask, curs->mask, l);
			if (error)
				return (error);
		}
		break;
	case WSDISPLAYIO_GCURPOS:
		pos = (struct wsdisplay_curpos *)data;
		pos->x = sc->sc_curs_pos.x;
		pos->y = sc->sc_curs_pos.y;
		break;
	case WSDISPLAYIO_SCURPOS:
		pos = (struct wsdisplay_curpos *)data;
		s = spltty();
		sc->sc_curs_pos.x = pos->x;
		sc->sc_curs_pos.y = pos->y;
		cgsix_updatecursor(sc, WSDISPLAY_CURSOR_DOPOS);
		splx(s);
		break;
	case WSDISPLAYIO_GCURMAX:
		pos = (struct wsdisplay_curpos *)data;
		pos->x = pos->y = 32;
		break;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;
	default:
		return -1; /* not supported */
        }

	return (0);
}

int
cgsix_setcursor(struct cgsix_softc *sc, struct wsdisplay_cursor *curs)
{
	u_int8_t r[2], g[2], b[2], image[128], mask[128];
	int s, error;
	size_t imcount;

	/*
	 * Do stuff that can generate errors first, then we'll blast it
	 * all at once.
	 */
	if (curs->which & WSDISPLAY_CURSOR_DOCMAP) {
		if (curs->cmap.count < 2)
			return (EINVAL);
		error = copyin(curs->cmap.red, r, sizeof(r));
		if (error)
			return (error);
		error = copyin(curs->cmap.green, g, sizeof(g));
		if (error)
			return (error);
		error = copyin(curs->cmap.blue, b, sizeof(b));
		if (error)
			return (error);
	}

	if (curs->which & WSDISPLAY_CURSOR_DOSHAPE) {
		if (curs->size.x > CG6_MAX_CURSOR ||
		    curs->size.y > CG6_MAX_CURSOR)
			return (EINVAL);
		imcount = (curs->size.x * curs->size.y) / NBBY;
		error = copyin(curs->image, image, imcount);
		if (error)
			return (error);
		error = copyin(curs->mask, mask, imcount);
		if (error)
			return (error);
	}

	/*
	 * Ok, everything is in kernel space and sane, update state.
	 */
	s = spltty();

	if (curs->which & WSDISPLAY_CURSOR_DOCUR)
		sc->sc_curs_enabled = curs->enable;
	if (curs->which & WSDISPLAY_CURSOR_DOPOS) {
		sc->sc_curs_pos.x = curs->pos.x;
		sc->sc_curs_pos.y = curs->pos.y;
	}
	if (curs->which & WSDISPLAY_CURSOR_DOHOT) {
		sc->sc_curs_hot.x = curs->hot.x;
		sc->sc_curs_hot.y = curs->hot.y;
	}
	if (curs->which & WSDISPLAY_CURSOR_DOCMAP) {
		sc->sc_curs_fg = ((r[0] << 16) | (g[0] << 8) | (b[0] << 0));
		sc->sc_curs_bg = ((r[1] << 16) | (g[1] << 8) | (b[1] << 0));
	}
	if (curs->which & WSDISPLAY_CURSOR_DOSHAPE) {
		sc->sc_curs_size.x = curs->size.x;
		sc->sc_curs_size.y = curs->size.y;
		bcopy(image, sc->sc_curs_image, imcount);
		bcopy(mask, sc->sc_curs_mask, imcount);
	}

	cgsix_updatecursor(sc, curs->which);
	splx(s);

	return (0);
}

int
cgsix_updatecursor(struct cgsix_softc *sc, u_int which)
{
	if (which & WSDISPLAY_CURSOR_DOCMAP) {
		BT_WRITE(sc, BT_ADDR, BT_OV1 << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_fg & 0x00ff0000) >> 16) << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_fg & 0x0000ff00) >> 8) << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_fg & 0x000000ff) >> 0) << 24);

		BT_WRITE(sc, BT_ADDR, BT_OV3 << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_bg & 0x00ff0000) >> 16) << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_bg & 0x0000ff00) >> 8) << 24);
		BT_WRITE(sc, BT_OMAP,
		    ((sc->sc_curs_bg & 0x000000ff) >> 0) << 24);
	}

	if (which & (WSDISPLAY_CURSOR_DOPOS | WSDISPLAY_CURSOR_DOHOT)) {
		u_int32_t x, y;

		x = sc->sc_curs_pos.x + CG6_MAX_CURSOR - sc->sc_curs_hot.x;
		y = sc->sc_curs_pos.y + CG6_MAX_CURSOR - sc->sc_curs_hot.y;
		THC_WRITE(sc, CG6_THC_CURSXY,
		    ((x & 0xffff) << 16) | (y & 0xffff));
	}

	if (which & WSDISPLAY_CURSOR_DOCUR) {
		u_int32_t c;

		/* Enable or disable the cursor overlay planes */
		if (sc->sc_curs_enabled) {
			BT_WRITE(sc, BT_ADDR, BT_CR << 24);
			c = BT_READ(sc, BT_CTRL);
			c |= (BTCR_DISPENA_OV0 | BTCR_DISPENA_OV1) << 24;
			BT_WRITE(sc, BT_CTRL, c);
		} else {
			BT_WRITE(sc, BT_ADDR, BT_CR << 24);
			c = BT_READ(sc, BT_CTRL);
			c &= ~((BTCR_DISPENA_OV0 | BTCR_DISPENA_OV1) << 24);
			BT_WRITE(sc, BT_CTRL, c);
			THC_WRITE(sc, CG6_THC_CURSXY, THC_CURSOFF);
		}
	}

	return (0);
}

struct mmo {
	off_t mo_uaddr;
	bus_size_t mo_size;
	bus_size_t mo_physoff;
};

paddr_t
cgsix_mmap(void *v, off_t off, int prot)
{
	struct cgsix_softc *sc = v;
	struct mmo *mo;
	bus_addr_t u;
	bus_size_t sz;

	static struct mmo mmo[] = {
		{ CG6_USER_RAM, 0, CGSIX_VID_OFFSET },

		/* do not actually know how big most of these are! */
		{ CG6_USER_FBC, 1, CGSIX_FBC_OFFSET },
		{ CG6_USER_TEC, 1, CGSIX_TEC_OFFSET },
		{ CG6_USER_BTREGS, 8192 /* XXX */, CGSIX_BT_OFFSET },
		{ CG6_USER_FHC, 1, CGSIX_FHC_OFFSET },
		{ CG6_USER_THC, CGSIX_THC_SIZE, CGSIX_THC_OFFSET },
		{ CG6_USER_ROM, 65536, CGSIX_ROM_OFFSET },
		{ CG6_USER_DHC, 1, CGSIX_DHC_OFFSET },
	};
#define	NMMO (sizeof mmo / sizeof *mmo)

	if (off & PGOFSET || off < 0)
		return (-1);

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
		for (mo = mmo; mo < &mmo[NMMO]; mo++) {
			if (off < mo->mo_uaddr)
				continue;
			u = off - mo->mo_uaddr;
			sz = mo->mo_size ? mo->mo_size : sc->sc_sunfb.sf_fbsize;
			if (u < sz) {
				return (bus_space_mmap(sc->sc_bustag,
				    sc->sc_paddr, u + mo->mo_physoff,
				    prot, BUS_SPACE_MAP_LINEAR));
			}
		}
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		/* Allow mapping as a dumb framebuffer from offset 0 */
		if (off >= 0 && off < sc->sc_sunfb.sf_fbsize)
			return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
			    off + CGSIX_VID_OFFSET, prot,
			    BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

int
cgsix_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
cg6_bt_getcmap(union bt_cmap *bcm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);
	for (i = 0; i < count; i++) {
		if ((error = copyout(&bcm->cm_map[index + i][0],
		    &rcm->red[i], 1)) != 0)
			return (error);
		if ((error = copyout(&bcm->cm_map[index + i][1],
		    &rcm->green[i], 1)) != 0)
			return (error);
		if ((error = copyout(&bcm->cm_map[index + i][2],
		    &rcm->blue[i], 1)) != 0)
			return (error);
	}
	return (0);
}

int
cg6_bt_putcmap(union bt_cmap *bcm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);
	for (i = 0; i < count; i++) {
		if ((error = copyin(&rcm->red[i],
		    &bcm->cm_map[index + i][0], 1)) != 0)
			return (error);
		if ((error = copyin(&rcm->green[i],
		    &bcm->cm_map[index + i][1], 1)) != 0)
			return (error);
		if ((error = copyin(&rcm->blue[i],
		    &bcm->cm_map[index + i][2], 1)) != 0)
			return (error);
	}
	return (0);
}

void
cgsix_loadcmap_deferred(struct cgsix_softc *sc, u_int start, u_int ncolors)
{
	u_int32_t thcm;

	thcm = THC_READ(sc, CG6_THC_MISC);
	thcm &= ~THC_MISC_RESET;
	thcm |= THC_MISC_INTEN;
	THC_WRITE(sc, CG6_THC_MISC, thcm);
}

void
cgsix_loadcmap_immediate(struct cgsix_softc *sc, u_int start, u_int ncolors)
{
	u_int cstart;
	u_int32_t v;
	int count;

	cstart = BT_D4M3(start);
	count = BT_D4M3(start + ncolors - 1) - BT_D4M3(start) + 3;
	BT_WRITE(sc, BT_ADDR, BT_D4M4(start) << 24);
	while (--count >= 0) {
		v = sc->sc_cmap.cm_chip[cstart];
		BT_WRITE(sc, BT_CMAP, v << 0);
		BT_WRITE(sc, BT_CMAP, v << 8);
		BT_WRITE(sc, BT_CMAP, v << 16);
		BT_WRITE(sc, BT_CMAP, v << 24);
		cstart++;
	}
}

void
cgsix_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct cgsix_softc *sc = v;
	union bt_cmap *bcm = &sc->sc_cmap;

	bcm->cm_map[index][0] = r;
	bcm->cm_map[index][1] = g;
	bcm->cm_map[index][2] = b;
	cgsix_loadcmap_immediate(sc, index, 1);
}

void
cgsix_reset(struct cgsix_softc *sc, u_int32_t fhcrev)
{
	u_int32_t fhc;

	/* hide the cursor, just in case */
	THC_WRITE(sc, CG6_THC_CURSXY, THC_CURSOFF);

	TEC_WRITE(sc, CG6_TEC_MV, 0);
	TEC_WRITE(sc, CG6_TEC_CLIP, 0);
	TEC_WRITE(sc, CG6_TEC_VDC, 0);

	/* take core of hardware bugs in old revisions */
	if (fhcrev < 5) {
		/*
		 * Keep current resolution; set cpu to 68020, set test
		 * window (size 1Kx1K), and for rev 1, disable dest cache.
		 */
		fhc = FHC_READ(sc);
		fhc &= FHC_RES_MASK;
		fhc |= FHC_CPU_68020 | FHC_TEST |
		    (11 << FHC_TESTX_SHIFT) | (11 << FHC_TESTY_SHIFT);
		if (fhcrev < 2)
			fhc |= FHC_DST_DISABLE;
		FHC_WRITE(sc, fhc);
	}

	/* enable cursor overlays in brooktree DAC */
	BT_WRITE(sc, BT_ADDR, BT_CR << 24);
	BT_WRITE(sc, BT_CTRL, BT_READ(sc, BT_CTRL) |
	    ((BTCR_DISPENA_OV1 | BTCR_DISPENA_OV0) << 24));
}

void
cgsix_hardreset(struct cgsix_softc *sc)
{
	u_int32_t fhc, rev;

	/* enable all of the bit planes */
	BT_WRITE(sc, BT_ADDR, BT_RMR << 24);
	BT_BARRIER(sc, BT_ADDR, BUS_SPACE_BARRIER_WRITE);
	BT_WRITE(sc, BT_CTRL, 0xff << 24);
	BT_BARRIER(sc, BT_CTRL, BUS_SPACE_BARRIER_WRITE);

	/* no bit planes should blink */
	BT_WRITE(sc, BT_ADDR, BT_BMR << 24);
	BT_BARRIER(sc, BT_ADDR, BUS_SPACE_BARRIER_WRITE);
	BT_WRITE(sc, BT_CTRL, 0x00 << 24);
	BT_BARRIER(sc, BT_CTRL, BUS_SPACE_BARRIER_WRITE);

	/*
	 * enable the RAMDAC, disable blink, disable overlay 0 and 1,
	 * use 4:1 multiplexor.
	 */
	BT_WRITE(sc, BT_ADDR, BT_CR << 24);
	BT_BARRIER(sc, BT_ADDR, BUS_SPACE_BARRIER_WRITE);
	BT_WRITE(sc, BT_CTRL,
	    (BTCR_MPLX_4 | BTCR_RAMENA | BTCR_BLINK_6464) << 24);
	BT_BARRIER(sc, BT_CTRL, BUS_SPACE_BARRIER_WRITE);

	/* disable the D/A read pins */
	BT_WRITE(sc, BT_ADDR, BT_CTR << 24);
	BT_BARRIER(sc, BT_ADDR, BUS_SPACE_BARRIER_WRITE);
	BT_WRITE(sc, BT_CTRL, 0x00 << 24);
	BT_BARRIER(sc, BT_CTRL, BUS_SPACE_BARRIER_WRITE);

	/* configure thc */
	THC_WRITE(sc, CG6_THC_MISC, THC_MISC_RESET | THC_MISC_INTR |
	    THC_MISC_CYCLS);
	THC_WRITE(sc, CG6_THC_MISC, THC_MISC_INTR | THC_MISC_CYCLS);

	THC_WRITE(sc, CG6_THC_HSYNC1, 0x10009);
	THC_WRITE(sc, CG6_THC_HSYNC2, 0x570000);
	THC_WRITE(sc, CG6_THC_HSYNC3, 0x15005d);
	THC_WRITE(sc, CG6_THC_VSYNC1, 0x10005);
	THC_WRITE(sc, CG6_THC_VSYNC2, 0x2403a8);
	THC_WRITE(sc, CG6_THC_REFRESH, 0x16b);

	THC_WRITE(sc, CG6_THC_MISC, THC_MISC_RESET | THC_MISC_INTR |
	    THC_MISC_CYCLS);
	THC_WRITE(sc, CG6_THC_MISC, THC_MISC_INTR | THC_MISC_CYCLS);

	/* configure fhc (1152x900) */
	fhc = FHC_READ(sc);
	rev = (fhc & FHC_REV_MASK) >> FHC_REV_SHIFT;

	fhc = FHC_RES_1152 | FHC_CPU_68020 | FHC_TEST;
	if (rev < 1)
		fhc |= FHC_FROP_DISABLE;
	if (rev < 2)
		fhc |= FHC_DST_DISABLE;
	FHC_WRITE(sc, fhc);
}

void
cgsix_burner(void *vsc, u_int on, u_int flags)
{
	struct cgsix_softc *sc = vsc;
	int s;
	u_int32_t thcm;

	s = splhigh();
	thcm = THC_READ(sc, CG6_THC_MISC);
	if (on)
		thcm |= THC_MISC_VIDEN | THC_MISC_SYNCEN;
	else {
		thcm &= ~THC_MISC_VIDEN;
		if (flags & WSDISPLAY_BURN_VBLANK)
			thcm &= ~THC_MISC_SYNCEN;
	}
	THC_WRITE(sc, CG6_THC_MISC, thcm);
	splx(s);
}

int
cgsix_intr(void *vsc)
{
	struct cgsix_softc *sc = vsc;
	u_int32_t thcm;

	thcm = THC_READ(sc, CG6_THC_MISC);
	if ((thcm & (THC_MISC_INTEN | THC_MISC_INTR)) !=
	    (THC_MISC_INTEN | THC_MISC_INTR)) {
		/* Not expecting an interrupt, it's not for us. */
		return (0);
	}

	/* Acknowledge the interrupt and disable it. */
	thcm &= ~(THC_MISC_RESET | THC_MISC_INTEN);
	thcm |= THC_MISC_INTR;
	THC_WRITE(sc, CG6_THC_MISC, thcm);
	cgsix_loadcmap_immediate(sc, 0, 256);
	return (1);
}

void
cgsix_ras_init(struct cgsix_softc *sc)
{
	u_int32_t m;

	CG6_DRAIN(sc);
	m = FBC_READ(sc, CG6_FBC_MODE);
	m &= ~FBC_MODE_MASK;
	m |= FBC_MODE_VAL;
	FBC_WRITE(sc, CG6_FBC_MODE, m);
}

int
cgsix_ras_copyrows(void *cookie, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;
	struct cgsix_softc *sc = ri->ri_hw;

	if (dst == src)
		return 0;
	if (src < 0) {
		n += src;
		src = 0;
	}
	if (src + n > ri->ri_rows)
		n = ri->ri_rows - src;
	if (dst < 0) {
		n += dst;
		dst = 0;
	}
	if (dst + n > ri->ri_rows)
		n = ri->ri_rows - dst;
	if (n <= 0)
		return 0;
	n *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	FBC_WRITE(sc, CG6_FBC_CLIP, 0);
	FBC_WRITE(sc, CG6_FBC_S, 0);
	FBC_WRITE(sc, CG6_FBC_OFFX, 0);
	FBC_WRITE(sc, CG6_FBC_OFFY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINX, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXX, ri->ri_width - 1);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXY, ri->ri_height - 1);
	FBC_WRITE(sc, CG6_FBC_ALU, FBC_ALU_COPY);
	FBC_WRITE(sc, CG6_FBC_X0, ri->ri_xorigin);
	FBC_WRITE(sc, CG6_FBC_Y0, ri->ri_yorigin + src);
	FBC_WRITE(sc, CG6_FBC_X1, ri->ri_xorigin + ri->ri_emuwidth - 1);
	FBC_WRITE(sc, CG6_FBC_Y1, ri->ri_yorigin + src + n - 1);
	FBC_WRITE(sc, CG6_FBC_X2, ri->ri_xorigin);
	FBC_WRITE(sc, CG6_FBC_Y2, ri->ri_yorigin + dst);
	FBC_WRITE(sc, CG6_FBC_X3, ri->ri_xorigin + ri->ri_emuwidth - 1);
	FBC_WRITE(sc, CG6_FBC_Y3, ri->ri_yorigin + dst + n - 1);
	CG6_BLIT_WAIT(sc);
	CG6_DRAIN(sc);

	return 0;
}

int
cgsix_ras_copycols(void *cookie, int row, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;
	struct cgsix_softc *sc = ri->ri_hw;

	if (dst == src)
		return 0;
	if ((row < 0) || (row >= ri->ri_rows))
		return 0;
	if (src < 0) {
		n += src;
		src = 0;
	}
	if (src + n > ri->ri_cols)
		n = ri->ri_cols - src;
	if (dst < 0) {
		n += dst;
		dst = 0;
	}
	if (dst + n > ri->ri_cols)
		n = ri->ri_cols - dst;
	if (n <= 0)
		return 0;
	n *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	FBC_WRITE(sc, CG6_FBC_CLIP, 0);
	FBC_WRITE(sc, CG6_FBC_S, 0);
	FBC_WRITE(sc, CG6_FBC_OFFX, 0);
	FBC_WRITE(sc, CG6_FBC_OFFY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINX, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXX, ri->ri_width - 1);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXY, ri->ri_height - 1);
	FBC_WRITE(sc, CG6_FBC_ALU, FBC_ALU_COPY);
	FBC_WRITE(sc, CG6_FBC_X0, ri->ri_xorigin + src);
	FBC_WRITE(sc, CG6_FBC_Y0, ri->ri_yorigin + row);
	FBC_WRITE(sc, CG6_FBC_X1, ri->ri_xorigin + src + n - 1);
	FBC_WRITE(sc, CG6_FBC_Y1,
	    ri->ri_yorigin + row + ri->ri_font->fontheight - 1);
	FBC_WRITE(sc, CG6_FBC_X2, ri->ri_xorigin + dst);
	FBC_WRITE(sc, CG6_FBC_Y2, ri->ri_yorigin + row);
	FBC_WRITE(sc, CG6_FBC_X3, ri->ri_xorigin + dst + n - 1);
	FBC_WRITE(sc, CG6_FBC_Y3,
	    ri->ri_yorigin + row + ri->ri_font->fontheight - 1);
	CG6_BLIT_WAIT(sc);
	CG6_DRAIN(sc);

	return 0;
}

int
cgsix_ras_erasecols(void *cookie, int row, int col, int n, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct cgsix_softc *sc = ri->ri_hw;
	int fg, bg;

	if ((row < 0) || (row >= ri->ri_rows))
		return 0;
	if (col < 0) {
		n += col;
		col = 0;
	}
	if (col + n > ri->ri_cols)
		n = ri->ri_cols - col;
	if (n <= 0)
		return 0;
	n *= ri->ri_font->fontwidth;
	col *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	FBC_WRITE(sc, CG6_FBC_CLIP, 0);
	FBC_WRITE(sc, CG6_FBC_S, 0);
	FBC_WRITE(sc, CG6_FBC_OFFX, 0);
	FBC_WRITE(sc, CG6_FBC_OFFY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINX, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXX, ri->ri_width - 1);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXY, ri->ri_height - 1);
	FBC_WRITE(sc, CG6_FBC_ALU, FBC_ALU_FILL);
	FBC_WRITE(sc, CG6_FBC_FG, ri->ri_devcmap[bg]);
	FBC_WRITE(sc, CG6_FBC_ARECTY, ri->ri_yorigin + row);
	FBC_WRITE(sc, CG6_FBC_ARECTX, ri->ri_xorigin + col);
	FBC_WRITE(sc, CG6_FBC_ARECTY,
	    ri->ri_yorigin + row + ri->ri_font->fontheight - 1);
	FBC_WRITE(sc, CG6_FBC_ARECTX, ri->ri_xorigin + col + n - 1);
	CG6_DRAW_WAIT(sc);
	CG6_DRAIN(sc);

	return 0;
}

int
cgsix_ras_eraserows(void *cookie, int row, int n, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct cgsix_softc *sc = ri->ri_hw;
	int fg, bg;

	if (row < 0) {
		n += row;
		row = 0;
	}
	if (row + n > ri->ri_rows)
		n = ri->ri_rows - row;
	if (n <= 0)
		return 0;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	FBC_WRITE(sc, CG6_FBC_CLIP, 0);
	FBC_WRITE(sc, CG6_FBC_S, 0);
	FBC_WRITE(sc, CG6_FBC_OFFX, 0);
	FBC_WRITE(sc, CG6_FBC_OFFY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINX, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXX, ri->ri_width - 1);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXY, ri->ri_height - 1);
	FBC_WRITE(sc, CG6_FBC_ALU, FBC_ALU_FILL);
	FBC_WRITE(sc, CG6_FBC_FG, ri->ri_devcmap[bg]);
	if ((n == ri->ri_rows) && (ri->ri_flg & RI_FULLCLEAR)) {
		FBC_WRITE(sc, CG6_FBC_ARECTY, 0);
		FBC_WRITE(sc, CG6_FBC_ARECTX, 0);
		FBC_WRITE(sc, CG6_FBC_ARECTY, ri->ri_height - 1);
		FBC_WRITE(sc, CG6_FBC_ARECTX, ri->ri_width - 1);
	} else {
		row *= ri->ri_font->fontheight;
		FBC_WRITE(sc, CG6_FBC_ARECTY, ri->ri_yorigin + row);
		FBC_WRITE(sc, CG6_FBC_ARECTX, ri->ri_xorigin);
		FBC_WRITE(sc, CG6_FBC_ARECTY,
		    ri->ri_yorigin + row + (n * ri->ri_font->fontheight) - 1);
		FBC_WRITE(sc, CG6_FBC_ARECTX,
		    ri->ri_xorigin + ri->ri_emuwidth - 1);
	}
	CG6_DRAW_WAIT(sc);
	CG6_DRAIN(sc);

	return 0;
}

int
cgsix_ras_do_cursor(struct rasops_info *ri)
{
	struct cgsix_softc *sc = ri->ri_hw;
	int row, col;

	row = ri->ri_crow * ri->ri_font->fontheight;
	col = ri->ri_ccol * ri->ri_font->fontwidth;
	FBC_WRITE(sc, CG6_FBC_CLIP, 0);
	FBC_WRITE(sc, CG6_FBC_S, 0);
	FBC_WRITE(sc, CG6_FBC_OFFX, 0);
	FBC_WRITE(sc, CG6_FBC_OFFY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINX, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMINY, 0);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXX, ri->ri_width - 1);
	FBC_WRITE(sc, CG6_FBC_CLIPMAXY, ri->ri_height - 1);
	FBC_WRITE(sc, CG6_FBC_ALU, FBC_ALU_FLIP);
	FBC_WRITE(sc, CG6_FBC_ARECTY, ri->ri_yorigin + row);
	FBC_WRITE(sc, CG6_FBC_ARECTX, ri->ri_xorigin + col);
	FBC_WRITE(sc, CG6_FBC_ARECTY,
	    ri->ri_yorigin + row + ri->ri_font->fontheight - 1);
	FBC_WRITE(sc, CG6_FBC_ARECTX,
	    ri->ri_xorigin + col + ri->ri_font->fontwidth - 1);
	CG6_DRAW_WAIT(sc);
	CG6_DRAIN(sc);

	return 0;
}
