/*	$OpenBSD: creator.c,v 1.57 2022/10/21 18:55:42 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/fsr.h>
#include <machine/openfirm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#include <sparc64/dev/creatorreg.h>
#include <sparc64/dev/creatorvar.h>

int	creator_match(struct device *, void *, void *);
void	creator_attach(struct device *, struct device *, void *);
int	creator_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t creator_mmap(void *, off_t, int);

void	creator_ras_fifo_wait(struct creator_softc *, int);
void	creator_ras_wait(struct creator_softc *);
void	creator_ras_init(struct creator_softc *);
int	creator_ras_copyrows(void *, int, int, int);
int	creator_ras_erasecols(void *, int, int, int, uint32_t);
int	creator_ras_eraserows(void *, int, int, uint32_t);
void	creator_ras_fill(struct creator_softc *);
void	creator_ras_setfg(struct creator_softc *, int32_t);

int	creator_setcursor(struct creator_softc *, struct wsdisplay_cursor *);
int	creator_updatecursor(struct creator_softc *, u_int);
void	creator_curs_enable(struct creator_softc *, u_int);

#ifndef SMALL_KERNEL
void	creator_load_firmware(struct device *);
#endif /* SMALL_KERNEL */
void	creator_load_sram(struct creator_softc *, u_int32_t *, u_int32_t);

struct wsdisplay_accessops creator_accessops = {
	.ioctl = creator_ioctl,
	.mmap = creator_mmap
};

struct cfdriver creator_cd = {
	NULL, "creator", DV_DULL
};

const struct cfattach creator_ca = {
	sizeof(struct creator_softc), creator_match, creator_attach
};

int
creator_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "SUNW,ffb") == 0 ||
	    strcmp(ma->ma_name, "SUNW,afb") == 0)
		return (1);
	return (0);
}

void
creator_attach(struct device *parent, struct device *self, void *aux)
{
	struct creator_softc *sc = (struct creator_softc *)self;
	struct mainbus_attach_args *ma = aux;
	extern int fbnode;
	int i, nregs;
	char *model;
	int btype;

	sc->sc_bt = ma->ma_bustag;

	nregs = min(ma->ma_nreg, FFB_NREGS);

	if (nregs <= FFB_REG_DFB24) {
		printf(": no dfb24 regs found\n");
		return;
	}

	if (bus_space_map(sc->sc_bt, ma->ma_reg[FFB_REG_DFB24].ur_paddr,
	    ma->ma_reg[FFB_REG_DFB24].ur_len, BUS_SPACE_MAP_LINEAR,
	    &sc->sc_pixel_h)) {
		printf(": failed to map dfb24\n");
		return;
	}

	if (bus_space_map(sc->sc_bt, ma->ma_reg[FFB_REG_FBC].ur_paddr,
	    ma->ma_reg[FFB_REG_FBC].ur_len, 0, &sc->sc_fbc_h)) {
		printf(": failed to map fbc\n");
		goto unmap_dfb24;
	}

	if (bus_space_map(sc->sc_bt, ma->ma_reg[FFB_REG_DAC].ur_paddr,
	    ma->ma_reg[FFB_REG_DAC].ur_len, 0, &sc->sc_dac_h)) {
		printf(": failed to map dac\n");
		goto unmap_fbc;
	}

	for (i = 0; i < nregs; i++) {
		sc->sc_addrs[i] = ma->ma_reg[i].ur_paddr;
		sc->sc_sizes[i] = ma->ma_reg[i].ur_len;
	}
	sc->sc_nreg = nregs;

	sc->sc_console = (fbnode == ma->ma_node);
	sc->sc_node = ma->ma_node;

	if (strcmp(ma->ma_name, "SUNW,afb") == 0)
		sc->sc_type = FFB_AFB;

	/*
	 * Prom reports only the length of the fcode header, we need
	 * the whole thing.
	 */
	sc->sc_sizes[0] = 0x00400000;

	if (sc->sc_type == FFB_CREATOR) {
		btype = getpropint(sc->sc_node, "board_type", 0);
		if ((btype & 7) == 3)
			printf(": Creator3D");
		else
			printf(": Creator");
	} else
		printf(": Elite3D");

	model = getpropstring(sc->sc_node, "model");
	if (model == NULL || strlen(model) == 0)
		model = "unknown";

	DAC_WRITE(sc, FFB_DAC_TYPE, DAC_TYPE_GETREV);
	sc->sc_dacrev = DAC_READ(sc, FFB_DAC_VALUE) >> 28;

	printf(", model %s, dac %u", model, sc->sc_dacrev);

	if (sc->sc_type == FFB_AFB)
		sc->sc_dacrev = 10;

	fb_setsize(&sc->sc_sunfb, 32, 1152, 900, sc->sc_node, 0);
	/* linesize has a fixed value, compensate */
	sc->sc_sunfb.sf_linebytes = 8192;
	sc->sc_sunfb.sf_fbsize = sc->sc_sunfb.sf_height * 8192;

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	sc->sc_sunfb.sf_ro.ri_bits = (void *)bus_space_vaddr(sc->sc_bt,
	    sc->sc_pixel_h);
	sc->sc_sunfb.sf_ro.ri_hw = sc;
	fbwscons_init(&sc->sc_sunfb, 0, sc->sc_console);

	if ((sc->sc_sunfb.sf_dev.dv_cfdata->cf_flags & CREATOR_CFFLAG_NOACCEL)
	    == 0) {
		sc->sc_sunfb.sf_ro.ri_ops.eraserows = creator_ras_eraserows;
		sc->sc_sunfb.sf_ro.ri_ops.erasecols = creator_ras_erasecols;
		sc->sc_sunfb.sf_ro.ri_ops.copyrows = creator_ras_copyrows;
		creator_ras_init(sc);

#ifndef SMALL_KERNEL
		/*
		 * Elite3D cards need a firmware for accelerated X to
		 * work.  Console framebuffer acceleration will work
		 * without it though, so doing this late should be
		 * fine.
		 */
		if (sc->sc_type == FFB_AFB)
			config_mountroot(self, creator_load_firmware);
#endif /* SMALL_KERNEL */
	}

	if (sc->sc_console)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &creator_accessops, sc->sc_console);
	return;

unmap_fbc:
	bus_space_unmap(sc->sc_bt, sc->sc_fbc_h,
	    ma->ma_reg[FFB_REG_FBC].ur_len);
unmap_dfb24:
	bus_space_unmap(sc->sc_bt, sc->sc_pixel_h,
	    ma->ma_reg[FFB_REG_DFB24].ur_len);
}

int
creator_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct creator_softc *sc = v;
	struct wsdisplay_cursor *curs;
	struct wsdisplay_fbinfo *wdf;
	struct wsdisplay_curpos *pos;
	u_char r[2], g[2], b[2];
	int error;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_SUNFFB;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			struct rasops_info *ri = &sc->sc_sunfb.sf_ro;
			uint32_t attr;

			if ((sc->sc_sunfb.sf_dev.dv_cfdata->cf_flags &
			    CREATOR_CFFLAG_NOACCEL) == 0)
				creator_ras_init(sc);

			/* Clear screen. */
			ri->ri_ops.pack_attr(ri,
			    WSCOL_BLACK, WSCOL_WHITE, WSATTR_WSCOLORS, &attr);
			ri->ri_ops.eraserows(ri, 0, ri->ri_rows, attr);
		} 
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = 32;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 0;
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;
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
			r[0] = sc->sc_curs_fg >> 0;
			g[0] = sc->sc_curs_fg >> 8;
			b[0] = sc->sc_curs_fg >> 16;
			r[1] = sc->sc_curs_bg >> 0;
			g[1] = sc->sc_curs_bg >> 8;
			b[1] = sc->sc_curs_bg >> 16;
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
	case WSDISPLAYIO_SCURPOS:
		pos = (struct wsdisplay_curpos *)data;
		sc->sc_curs_pos.x = pos->x;
		sc->sc_curs_pos.y = pos->y;
		creator_updatecursor(sc, WSDISPLAY_CURSOR_DOPOS);
		break;
	case WSDISPLAYIO_GCURPOS:
		pos = (struct wsdisplay_curpos *)data;
		pos->x = sc->sc_curs_pos.x;
		pos->y = sc->sc_curs_pos.y;
		break;
	case WSDISPLAYIO_SCURSOR:
		curs = (struct wsdisplay_cursor *)data;
		return (creator_setcursor(sc, curs));
	case WSDISPLAYIO_GCURMAX:
		pos = (struct wsdisplay_curpos *)data;
		pos->x = CREATOR_CURS_MAX;
		pos->y = CREATOR_CURS_MAX;
		break;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
	default:
		return -1; /* not supported yet */
        }

	return (0);
}

int
creator_setcursor(struct creator_softc *sc, struct wsdisplay_cursor *curs)
{
	u_int8_t r[2], g[2], b[2], image[128], mask[128];
	int error;
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
		if (curs->size.x > CREATOR_CURS_MAX ||
		    curs->size.y > CREATOR_CURS_MAX)
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
		sc->sc_curs_fg = ((r[0] << 0) | (g[0] << 8) | (b[0] << 16));
		sc->sc_curs_bg = ((r[1] << 0) | (g[1] << 8) | (b[1] << 16));
	}
	if (curs->which & WSDISPLAY_CURSOR_DOSHAPE) {
		sc->sc_curs_size.x = curs->size.x;
		sc->sc_curs_size.y = curs->size.y;
		bcopy(image, sc->sc_curs_image, imcount);
		bcopy(mask, sc->sc_curs_mask, imcount);
	}

	creator_updatecursor(sc, curs->which);

	return (0);
}

void
creator_curs_enable(struct creator_softc *sc, u_int ena)
{
	u_int32_t v;

	DAC_WRITE(sc, FFB_DAC_TYPE2, DAC_TYPE2_CURSENAB);
	if (sc->sc_dacrev <= 2)
		v = ena ? 3 : 0;
	else
		v = ena ? 0 : 3;
	DAC_WRITE(sc, FFB_DAC_VALUE2, v);
}

int
creator_updatecursor(struct creator_softc *sc, u_int which)
{
	creator_curs_enable(sc, 0);

	if (which & WSDISPLAY_CURSOR_DOCMAP) {
		DAC_WRITE(sc, FFB_DAC_TYPE2, DAC_TYPE2_CURSCMAP);
		DAC_WRITE(sc, FFB_DAC_VALUE2, sc->sc_curs_fg);
		DAC_WRITE(sc, FFB_DAC_VALUE2, sc->sc_curs_bg);
	}

	if (which & (WSDISPLAY_CURSOR_DOPOS | WSDISPLAY_CURSOR_DOHOT)) {
		u_int32_t x, y;

		x = sc->sc_curs_pos.x + CREATOR_CURS_MAX - sc->sc_curs_hot.x;
		y = sc->sc_curs_pos.y + CREATOR_CURS_MAX - sc->sc_curs_hot.y;
		DAC_WRITE(sc, FFB_DAC_TYPE2, DAC_TYPE2_CURSPOS);
		DAC_WRITE(sc, FFB_DAC_VALUE2,
		    ((x & 0xffff) << 16) | (y & 0xffff));
	}

	if (which & WSDISPLAY_CURSOR_DOCUR)
		creator_curs_enable(sc, sc->sc_curs_enabled);

	return (0);
}

const struct creator_mappings {
	bus_addr_t uoff;
	bus_addr_t poff;
	bus_size_t ulen;
} creator_map[] = {
	{ FFB_VOFF_SFB8R, FFB_POFF_SFB8R, FFB_VLEN_SFB8R },
	{ FFB_VOFF_SFB8G, FFB_POFF_SFB8G, FFB_VLEN_SFB8G },
	{ FFB_VOFF_SFB8B, FFB_POFF_SFB8B, FFB_VLEN_SFB8B },
	{ FFB_VOFF_SFB8X, FFB_POFF_SFB8X, FFB_VLEN_SFB8X },
	{ FFB_VOFF_SFB32, FFB_POFF_SFB32, FFB_VLEN_SFB32 },
	{ FFB_VOFF_SFB64, FFB_POFF_SFB64, FFB_VLEN_SFB64 },
	{ FFB_VOFF_FBC_REGS, FFB_POFF_FBC_REGS, FFB_VLEN_FBC_REGS },
	{ FFB_VOFF_BM_FBC_REGS, FFB_POFF_BM_FBC_REGS, FFB_VLEN_BM_FBC_REGS },
	{ FFB_VOFF_DFB8R, FFB_POFF_DFB8R, FFB_VLEN_DFB8R },
	{ FFB_VOFF_DFB8G, FFB_POFF_DFB8G, FFB_VLEN_DFB8G },
	{ FFB_VOFF_DFB8B, FFB_POFF_DFB8B, FFB_VLEN_DFB8B },
	{ FFB_VOFF_DFB8X, FFB_POFF_DFB8X, FFB_VLEN_DFB8X },
	{ FFB_VOFF_DFB24, FFB_POFF_DFB24, FFB_VLEN_DFB24 },
	{ FFB_VOFF_DFB32, FFB_POFF_DFB32, FFB_VLEN_DFB32 },
	{ FFB_VOFF_DFB422A, FFB_POFF_DFB422A, FFB_VLEN_DFB422A },
	{ FFB_VOFF_DFB422AD, FFB_POFF_DFB422AD, FFB_VLEN_DFB422AD },
	{ FFB_VOFF_DFB24B, FFB_POFF_DFB24B, FFB_VLEN_DFB24B },
	{ FFB_VOFF_DFB422B, FFB_POFF_DFB422B, FFB_VLEN_DFB422B },
	{ FFB_VOFF_DFB422BD, FFB_POFF_DFB422BD, FFB_VLEN_DFB422BD },
	{ FFB_VOFF_SFB16Z, FFB_POFF_SFB16Z, FFB_VLEN_SFB16Z },
	{ FFB_VOFF_SFB8Z, FFB_POFF_SFB8Z, FFB_VLEN_SFB8Z },
	{ FFB_VOFF_SFB422, FFB_POFF_SFB422, FFB_VLEN_SFB422 },
	{ FFB_VOFF_SFB422D, FFB_POFF_SFB422D, FFB_VLEN_SFB422D },
	{ FFB_VOFF_FBC_KREGS, FFB_POFF_FBC_KREGS, FFB_VLEN_FBC_KREGS },
	{ FFB_VOFF_DAC, FFB_POFF_DAC, FFB_VLEN_DAC },
	{ FFB_VOFF_PROM, FFB_POFF_PROM, FFB_VLEN_PROM },
	{ FFB_VOFF_EXP, FFB_POFF_EXP, FFB_VLEN_EXP },
};
#define	CREATOR_NMAPPINGS       nitems(creator_map)

paddr_t
creator_mmap(void *vsc, off_t off, int prot)
{
	paddr_t x;
	struct creator_softc *sc = vsc;
	int i;

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
		/* Turn virtual offset into physical offset */
		for (i = 0; i < CREATOR_NMAPPINGS; i++) {
			if (off >= creator_map[i].uoff &&
			    off < (creator_map[i].uoff + creator_map[i].ulen))
				break;
		}
		if (i == CREATOR_NMAPPINGS)
			break;

		off -= creator_map[i].uoff;
		off += creator_map[i].poff;
		off += sc->sc_addrs[0];

		/* Map based on physical offset */
		for (i = 0; i < sc->sc_nreg; i++) {
			/* Before this set? */
			if (off < sc->sc_addrs[i])
				continue;
			/* After this set? */
			if (off >= (sc->sc_addrs[i] + sc->sc_sizes[i]))
				continue;

			x = bus_space_mmap(sc->sc_bt, 0, off, prot,
			    BUS_SPACE_MAP_LINEAR);
			return (x);
		}
		break;
	case WSDISPLAYIO_MODE_DUMBFB:
		if (sc->sc_nreg <= FFB_REG_DFB24)
			break;
		if (off >= 0 && off < sc->sc_sizes[FFB_REG_DFB24])
			return (bus_space_mmap(sc->sc_bt,
			    sc->sc_addrs[FFB_REG_DFB24], off, prot,
			    BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

void
creator_ras_fifo_wait(struct creator_softc *sc, int n)
{
	int32_t cache = sc->sc_fifo_cache;

	if (cache < n) {
		do {
			cache = FBC_READ(sc, FFB_FBC_UCSR);
			cache = (cache & FBC_UCSR_FIFO_MASK) - 8;
		} while (cache < n);
	}
	sc->sc_fifo_cache = cache - n;
}

void
creator_ras_wait(struct creator_softc *sc)
{
	u_int32_t ucsr, r;

	while (1) {
		ucsr = FBC_READ(sc, FFB_FBC_UCSR);
		if ((ucsr & (FBC_UCSR_FB_BUSY|FBC_UCSR_RP_BUSY)) == 0)
			break;
		r = ucsr & (FBC_UCSR_READ_ERR | FBC_UCSR_FIFO_OVFL);
		if (r != 0)
			FBC_WRITE(sc, FFB_FBC_UCSR, r);
	}
}

void
creator_ras_init(struct creator_softc *sc)
{
	creator_ras_fifo_wait(sc, 7);
	FBC_WRITE(sc, FFB_FBC_PPC,
	    FBC_PPC_VCE_DIS | FBC_PPC_TBE_OPAQUE |
	    FBC_PPC_APE_DIS | FBC_PPC_CS_CONST);
	FBC_WRITE(sc, FFB_FBC_FBC,
	    FFB_FBC_WB_A | FFB_FBC_RB_A | FFB_FBC_SB_BOTH |
	    FFB_FBC_XE_OFF | FFB_FBC_RGBE_MASK);
	FBC_WRITE(sc, FFB_FBC_ROP, FBC_ROP_NEW);
	FBC_WRITE(sc, FFB_FBC_DRAWOP, FBC_DRAWOP_RECTANGLE);
	FBC_WRITE(sc, FFB_FBC_PMASK, 0xffffffff);
	FBC_WRITE(sc, FFB_FBC_FONTINC, 0x10000);
	sc->sc_fg_cache = 0;
	FBC_WRITE(sc, FFB_FBC_FG, sc->sc_fg_cache);
	creator_ras_wait(sc);
}

int
creator_ras_eraserows(void *cookie, int row, int n, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct creator_softc *sc = ri->ri_hw;
	int bg, fg;

	if (row < 0) {
		n += row;
		row = 0;
	}
	if (row + n > ri->ri_rows)
		n = ri->ri_rows - row;
	if (n <= 0)
		return 0;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	creator_ras_fill(sc);
	creator_ras_setfg(sc, ri->ri_devcmap[bg]);
	creator_ras_fifo_wait(sc, 4);
	if ((n == ri->ri_rows) && (ri->ri_flg & RI_FULLCLEAR)) {
		FBC_WRITE(sc, FFB_FBC_BY, 0);
		FBC_WRITE(sc, FFB_FBC_BX, 0);
		FBC_WRITE(sc, FFB_FBC_BH, ri->ri_height);
		FBC_WRITE(sc, FFB_FBC_BW, ri->ri_width);
	} else {
		row *= ri->ri_font->fontheight;
		FBC_WRITE(sc, FFB_FBC_BY, ri->ri_yorigin + row);
		FBC_WRITE(sc, FFB_FBC_BX, ri->ri_xorigin);
		FBC_WRITE(sc, FFB_FBC_BH, n * ri->ri_font->fontheight);
		FBC_WRITE(sc, FFB_FBC_BW, ri->ri_emuwidth);
	}
	creator_ras_wait(sc);

	return 0;
}

int
creator_ras_erasecols(void *cookie, int row, int col, int n, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct creator_softc *sc = ri->ri_hw;
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
	creator_ras_fill(sc);
	creator_ras_setfg(sc, ri->ri_devcmap[bg]);
	creator_ras_fifo_wait(sc, 4);
	FBC_WRITE(sc, FFB_FBC_BY, ri->ri_yorigin + row);
	FBC_WRITE(sc, FFB_FBC_BX, ri->ri_xorigin + col);
	FBC_WRITE(sc, FFB_FBC_BH, ri->ri_font->fontheight);
	FBC_WRITE(sc, FFB_FBC_BW, n - 1);
	creator_ras_wait(sc);

	return 0;
}

void
creator_ras_fill(struct creator_softc *sc)
{
	creator_ras_fifo_wait(sc, 2);
	FBC_WRITE(sc, FFB_FBC_ROP, FBC_ROP_NEW);
	FBC_WRITE(sc, FFB_FBC_DRAWOP, FBC_DRAWOP_RECTANGLE);
	creator_ras_wait(sc);
}

int
creator_ras_copyrows(void *cookie, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;
	struct creator_softc *sc = ri->ri_hw;

	if (dst == src)
		return 0;
	if (src < 0) {
		n += src;
		src = 0;
	}
	if ((src + n) > ri->ri_rows)
		n = ri->ri_rows - src;
	if (dst < 0) {
		n += dst;
		dst = 0;
	}
	if ((dst + n) > ri->ri_rows)
		n = ri->ri_rows - dst;
	if (n <= 0)
		return 0;
	n *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	creator_ras_fifo_wait(sc, 8);
	FBC_WRITE(sc, FFB_FBC_ROP, FBC_ROP_OLD | (FBC_ROP_OLD << 8));
	FBC_WRITE(sc, FFB_FBC_DRAWOP, FBC_DRAWOP_VSCROLL);
	FBC_WRITE(sc, FFB_FBC_BY, ri->ri_yorigin + src);
	FBC_WRITE(sc, FFB_FBC_BX, ri->ri_xorigin);
	FBC_WRITE(sc, FFB_FBC_DY, ri->ri_yorigin + dst);
	FBC_WRITE(sc, FFB_FBC_DX, ri->ri_xorigin);
	FBC_WRITE(sc, FFB_FBC_BH, n);
	FBC_WRITE(sc, FFB_FBC_BW, ri->ri_emuwidth);
	creator_ras_wait(sc);

	return 0;
}

void
creator_ras_setfg(struct creator_softc *sc, int32_t fg)
{
	creator_ras_fifo_wait(sc, 1);
	if (fg == sc->sc_fg_cache)
		return;
	sc->sc_fg_cache = fg;
	FBC_WRITE(sc, FFB_FBC_FG, fg);
	creator_ras_wait(sc);
}

#ifndef SMALL_KERNEL
struct creator_firmware {
	char		fw_ident[8];
	u_int32_t	fw_size;
	u_int32_t	fw_reserved[2];
	u_int32_t	fw_ucode[0];
};

#define CREATOR_FIRMWARE_REV	0x101

void
creator_load_firmware(struct device *self)
{
	struct creator_softc *sc = (struct creator_softc *)self;
	struct creator_firmware *fw;
	u_int32_t ascr;
	size_t buflen;
	u_char *buf;
	int error;

	error = loadfirmware("afb", &buf, &buflen);
	if (error) {
		printf("%s: error %d, could not read firmware %s\n",
		       sc->sc_sunfb.sf_dev.dv_xname, error, "afb");
		return;
	}

	fw = (struct creator_firmware *)buf;
	if (sizeof(*fw) > buflen ||
	    fw->fw_size * sizeof(u_int32_t) > (buflen - sizeof(*fw))) {
		printf("%s: corrupt firmware\n", sc->sc_sunfb.sf_dev.dv_xname);
		free(buf, M_DEVBUF, 0);
		return;
	}

	printf("%s: firmware rev %d.%d.%d\n", sc->sc_sunfb.sf_dev.dv_xname,
	       (fw->fw_ucode[CREATOR_FIRMWARE_REV] >> 16) & 0xff,
	       (fw->fw_ucode[CREATOR_FIRMWARE_REV] >> 8) & 0xff,
	       fw->fw_ucode[CREATOR_FIRMWARE_REV] & 0xff);

	ascr = FBC_READ(sc, FFB_FBC_ASCR);

	/* Stop all floats. */
	FBC_WRITE(sc, FFB_FBC_FEM, ascr & 0x3f);
	FBC_WRITE(sc, FFB_FBC_ASCR, FBC_ASCR_STOP);

	creator_ras_wait(sc);

	/* Load firmware into all secondary floats. */
	if (ascr & 0x3e) {
		FBC_WRITE(sc, FFB_FBC_FEM, ascr & 0x3e);
		creator_load_sram(sc, fw->fw_ucode, fw->fw_size);
	}

	/* Load firmware into primary float. */
	FBC_WRITE(sc, FFB_FBC_FEM, ascr & 0x01);
	creator_load_sram(sc, fw->fw_ucode, fw->fw_size);

	/* Restart all floats. */
	FBC_WRITE(sc, FFB_FBC_FEM, ascr & 0x3f);
	FBC_WRITE(sc, FFB_FBC_ASCR, FBC_ASCR_RESTART);

	creator_ras_wait(sc);

	free(buf, M_DEVBUF, 0);
}
#endif /* SMALL_KERNEL */

void
creator_load_sram(struct creator_softc *sc, u_int32_t *ucode, u_int32_t size)
{
	uint64_t pstate, fprs;
	caddr_t sram;

	sram = bus_space_vaddr(sc->sc_bt, sc->sc_fbc_h) + FFB_FBC_SRAM36;

	/*
	 * Apparently, loading the firmware into SRAM needs to be done
	 * using block copies.  And block copies use the
	 * floating-point registers.  Generally, using the FPU in the
	 * kernel is verboten.  But since we load the firmware before
	 * userland processes are started, thrashing the
	 * floating-point registers is fine.  We do need to enable the
	 * FPU before we access them though, otherwise we'll trap.
	 */
	pstate = sparc_rdpr(pstate);
	sparc_wrpr(pstate, pstate | PSTATE_PEF, 0);
	fprs = sparc_rd(fprs);
	sparc_wr(fprs, FPRS_FEF, 0);

	FBC_WRITE(sc, FFB_FBC_SRAMAR, 0);

	while (size > 0) {
		creator_ras_fifo_wait(sc, 16);

		__asm__ volatile("ld	[%0 + 0x00], %%f1\n\t"
				     "ld	[%0 + 0x04], %%f0\n\t"
				     "ld	[%0 + 0x08], %%f3\n\t"
				     "ld	[%0 + 0x0c], %%f2\n\t"
				     "ld	[%0 + 0x10], %%f5\n\t"
				     "ld	[%0 + 0x14], %%f4\n\t"
				     "ld	[%0 + 0x18], %%f7\n\t"
				     "ld	[%0 + 0x1c], %%f6\n\t"
				     "ld	[%0 + 0x20], %%f9\n\t"
				     "ld	[%0 + 0x24], %%f8\n\t"
				     "ld	[%0 + 0x28], %%f11\n\t"
				     "ld	[%0 + 0x2c], %%f10\n\t"
				     "ld	[%0 + 0x30], %%f13\n\t"
				     "ld	[%0 + 0x34], %%f12\n\t"
				     "ld	[%0 + 0x38], %%f15\n\t"
				     "ld	[%0 + 0x3c], %%f14\n\t"
				     "membar	#Sync\n\t"
				     "stda	%%f0, [%1] 240\n\t"
				     "membar	#Sync"
				     : : "r" (ucode), "r" (sram));

		ucode += 16;
		size -= 16;
	}

	sparc_wr(fprs, fprs, 0);
	sparc_wrpr(pstate, pstate, 0);

	creator_ras_wait(sc);
}
