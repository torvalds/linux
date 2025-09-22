/* $OpenBSD: vga.c,v 1.74 2021/05/27 23:24:40 cheloha Exp $ */
/* $NetBSD: vga.c,v 1.28.2.1 2000/06/30 16:27:47 simonb Exp $ */

/*-
  * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
  * Copyright (c) 1992-1998 Søren Schmidt
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without
  * modification, are permitted provided that the following conditions
  * are met:
  * 1. Redistributions of source code must retain the above copyright
  *    notice, this list of conditions and the following disclaimer as
  *    the first lines of this file unmodified.
  * 2. Redistributions in binary form must reproduce the above copyright
  *    notice, this list of conditions and the following disclaimer in the
  *    documentation and/or other materials provided with the distribution.
  * 3. The name of the author may not be used to endorse or promote products
  *    derived from this software without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
  * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
  * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
  * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  */
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

#include "vga.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <machine/bus.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/unicode.h>

#include <dev/ic/vgavar.h>
#include <dev/ic/pcdisplay.h>

static struct vgafont {
	char name[WSFONT_NAME_SIZE];
	int height;
	int encoding;
#ifdef notyet
	int firstchar, numchars;
#endif
	int slot;
	void *fontdata;
} vga_builtinfont = {
	.name = "builtin",
	.height = 16,
	.encoding = WSDISPLAY_FONTENC_IBM,
#ifdef notyet
	.firstchar = 0,
	.numchars = 256,
#endif
	.slot = 0,
	.fontdata = NULL
};

int vgaconsole, vga_console_type, vga_console_attached;
struct vgascreen vga_console_screen;
struct vga_config vga_console_vc;

int	vga_selectfont(struct vga_config *, struct vgascreen *,
    const char *, const char *);
void	vga_init_screen(struct vga_config *, struct vgascreen *,
    const struct wsscreen_descr *, int, uint32_t *);
void	vga_init(struct vga_config *, bus_space_tag_t, bus_space_tag_t);
void	vga_setfont(struct vga_config *, struct vgascreen *);
void	vga_pick_monitor_type(struct vga_config *);

int	vga_mapchar(void *, int, unsigned int *);
int	vga_putchar(void *, int, int, u_int, uint32_t);
int	vga_pack_attr(void *, int, int, int, uint32_t *);
int	vga_copyrows(void *, int, int, int);
void	vga_unpack_attr(void *, uint32_t, int *, int *, int *);

static const struct wsdisplay_emulops vga_emulops = {
	pcdisplay_cursor,
	vga_mapchar,
	vga_putchar,
	pcdisplay_copycols,
	pcdisplay_erasecols,
	vga_copyrows,
	pcdisplay_eraserows,
	vga_pack_attr,
	vga_unpack_attr
};

/*
 * translate WS(=ANSI) color codes to standard pc ones
 */
static const unsigned char fgansitopc[] = {
#ifdef __alpha__
	/*
	 * XXX DEC HAS SWITCHED THE CODES FOR BLUE AND RED!!!
	 * XXX We should probably not bother with this
	 * XXX (reinitialize the palette registers).
	 */
	FG_BLACK, FG_BLUE, FG_GREEN, FG_CYAN, FG_RED,
	FG_MAGENTA, FG_BROWN, FG_LIGHTGREY
#else
	FG_BLACK, FG_RED, FG_GREEN, FG_BROWN, FG_BLUE,
	FG_MAGENTA, FG_CYAN, FG_LIGHTGREY
#endif
}, bgansitopc[] = {
#ifdef __alpha__
	BG_BLACK, BG_BLUE, BG_GREEN, BG_CYAN, BG_RED,
	BG_MAGENTA, BG_BROWN, BG_LIGHTGREY
#else
	BG_BLACK, BG_RED, BG_GREEN, BG_BROWN, BG_BLUE,
	BG_MAGENTA, BG_CYAN, BG_LIGHTGREY
#endif
};

/*
 * translate standard pc color codes to WS(=ANSI) ones
 */
static const u_int8_t pctoansi[] = {
#ifdef __alpha__
	WSCOL_BLACK, WSCOL_RED, WSCOL_GREEN, WSCOL_BROWN,
	WSCOL_BLUE, WSCOL_MAGENTA, WSCOL_CYAN, WSCOL_WHITE
#else
	WSCOL_BLACK, WSCOL_BLUE, WSCOL_GREEN, WSCOL_CYAN,
	WSCOL_RED, WSCOL_MAGENTA, WSCOL_BROWN, WSCOL_WHITE
#endif
};


const struct wsscreen_descr vga_stdscreen = {
	"80x25", 80, 25,
	&vga_emulops,
	8, 16,
	WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_BLINK
}, vga_stdscreen_mono = {
	"80x25", 80, 25,
	&vga_emulops,
	8, 16,
	WSSCREEN_HILIT | WSSCREEN_UNDERLINE | WSSCREEN_BLINK | WSSCREEN_REVERSE
}, vga_stdscreen_bf = {
	"80x25bf", 80, 25,
	&vga_emulops,
	8, 16,
	WSSCREEN_WSCOLORS | WSSCREEN_BLINK
}, vga_40lscreen = {
	"80x40", 80, 40,
	&vga_emulops,
	8, 10,
	WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_BLINK
}, vga_40lscreen_mono = {
	"80x40", 80, 40,
	&vga_emulops,
	8, 10,
	WSSCREEN_HILIT | WSSCREEN_UNDERLINE | WSSCREEN_BLINK | WSSCREEN_REVERSE
}, vga_40lscreen_bf = {
	"80x40bf", 80, 40,
	&vga_emulops,
	8, 10,
	WSSCREEN_WSCOLORS | WSSCREEN_BLINK
}, vga_50lscreen = {
	"80x50", 80, 50,
	&vga_emulops,
	8, 8,
	WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_BLINK
}, vga_50lscreen_mono = {
	"80x50", 80, 50,
	&vga_emulops,
	8, 8,
	WSSCREEN_HILIT | WSSCREEN_UNDERLINE | WSSCREEN_BLINK | WSSCREEN_REVERSE
}, vga_50lscreen_bf = {
	"80x50bf", 80, 50,
	&vga_emulops,
	8, 8,
	WSSCREEN_WSCOLORS | WSSCREEN_BLINK
};

#define VGA_SCREEN_CANTWOFONTS(type) (!((type)->capabilities & WSSCREEN_HILIT))

const struct wsscreen_descr *_vga_scrlist[] = {
	&vga_stdscreen,
	&vga_stdscreen_bf,
	&vga_40lscreen,
	&vga_40lscreen_bf,
	&vga_50lscreen,
	&vga_50lscreen_bf,
	/* XXX other formats, graphics screen? */
}, *_vga_scrlist_mono[] = {
	&vga_stdscreen_mono,
	&vga_40lscreen_mono,
	&vga_50lscreen_mono,
	/* XXX other formats, graphics screen? */
};

const struct wsscreen_list vga_screenlist = {
	sizeof(_vga_scrlist) / sizeof(struct wsscreen_descr *),
	_vga_scrlist
}, vga_screenlist_mono = {
	sizeof(_vga_scrlist_mono) / sizeof(struct wsscreen_descr *),
	_vga_scrlist_mono
};

int	vga_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	vga_mmap(void *, off_t, int);
int	vga_alloc_screen(void *, const struct wsscreen_descr *,
			 void **, int *, int *, uint32_t *);
void	vga_free_screen(void *, void *);
int	vga_show_screen(void *, void *, int,
			void (*) (void *, int, int), void *);
int	vga_load_font(void *, void *, struct wsdisplay_font *);
int	vga_list_font(void *, struct wsdisplay_font *);
void	vga_scrollback(void *, void *, int);
void	vga_burner(void *v, u_int on, u_int flags);
int	vga_getchar(void *, int, int, struct wsdisplay_charcell *);

void vga_doswitch(void *);

const struct wsdisplay_accessops vga_accessops = {
	.ioctl = vga_ioctl,
	.mmap = vga_mmap,
	.alloc_screen = vga_alloc_screen,
	.free_screen = vga_free_screen,
	.show_screen = vga_show_screen,
	.load_font = vga_load_font,
	.list_font = vga_list_font,
	.scrollback = vga_scrollback,
	.getchar = vga_getchar,
	.burn_screen = vga_burner
};

/*
 * The following functions implement back-end configuration grabbing
 * and attachment.
 */
int
vga_common_probe(bus_space_tag_t iot, bus_space_tag_t memt)
{
	bus_space_handle_t ioh_vga, ioh_6845, memh;
	u_int8_t regval;
	u_int16_t vgadata;
	int gotio_vga, gotio_6845, gotmem, mono, rv;
	int dispoffset;

	gotio_vga = gotio_6845 = gotmem = rv = 0;

	if (bus_space_map(iot, 0x3c0, 0x10, 0, &ioh_vga))
		goto bad;
	gotio_vga = 1;

	/* read "misc output register" */
	regval = bus_space_read_1(iot, ioh_vga, 0xc);
	mono = !(regval & 1);

	if (bus_space_map(iot, (mono ? 0x3b0 : 0x3d0), 0x10, 0, &ioh_6845))
		goto bad;
	gotio_6845 = 1;

	if (bus_space_map(memt, 0xa0000, 0x20000, 0, &memh))
		goto bad;
	gotmem = 1;

	dispoffset = (mono ? 0x10000 : 0x18000);

	vgadata = bus_space_read_2(memt, memh, dispoffset);
	bus_space_write_2(memt, memh, dispoffset, 0xa55a);
	if (bus_space_read_2(memt, memh, dispoffset) != 0xa55a)
		goto bad;
	bus_space_write_2(memt, memh, dispoffset, vgadata);

	/*
	 * check if this is really a VGA
	 * (try to write "Color Select" register as XFree86 does)
	 * XXX check before if at least EGA?
	 */
	/* reset state */
	(void) bus_space_read_1(iot, ioh_6845, 10);
	bus_space_write_1(iot, ioh_vga, VGA_ATC_INDEX,
			  20 | 0x20); /* colselect | enable */
	regval = bus_space_read_1(iot, ioh_vga, VGA_ATC_DATAR);
	/* toggle the implemented bits */
	bus_space_write_1(iot, ioh_vga, VGA_ATC_DATAW, regval ^ 0x0f);
	bus_space_write_1(iot, ioh_vga, VGA_ATC_INDEX,
			  20 | 0x20);
	/* read back */
	if (bus_space_read_1(iot, ioh_vga, VGA_ATC_DATAR) != (regval ^ 0x0f))
		goto bad;
	/* restore contents */
	bus_space_write_1(iot, ioh_vga, VGA_ATC_DATAW, regval);

	rv = 1;
bad:
	if (gotio_vga)
		bus_space_unmap(iot, ioh_vga, 0x10);
	if (gotio_6845)
		bus_space_unmap(iot, ioh_6845, 0x10);
	if (gotmem)
		bus_space_unmap(memt, memh, 0x20000);

	return (rv);
}

/*
 * We want at least ASCII 32..127 be present in the
 * first font slot.
 */
int
vga_selectfont(struct vga_config *vc, struct vgascreen *scr, const char *name1,
    const char *name2) /* NULL: take first found */
{
	const struct wsscreen_descr *type = scr->pcs.type;
	struct vgafont *f1, *f2;
	int i;

	f1 = f2 = 0;

	for (i = 0; i < VGA_MAXFONT; i++) {
		struct vgafont *f = vc->vc_fonts[i];
		if (!f || f->height != type->fontheight)
			continue;
		if (!f1 && (!name1 || !*name1 ||
		     !strncmp(name1, f->name, WSFONT_NAME_SIZE))) {
			f1 = f;
			continue;
		}
		if (!f2 &&
		    VGA_SCREEN_CANTWOFONTS(type) &&
		    (!name2 || !*name2 ||
		     !strncmp(name2, f->name, WSFONT_NAME_SIZE))) {
			f2 = f;
			continue;
		}
	}

	/*
	 * The request fails if no primary font was found,
	 * or if a second font was requested but not found.
	 */
	if (f1 && (!name2 || !*name2 || f2)) {
#ifdef VGAFONTDEBUG
		if (scr != &vga_console_screen || vga_console_attached) {
			printf("vga (%s): font1=%s (slot %d)", type->name,
			       f1->name, f1->slot);
			if (f2)
				printf(", font2=%s (slot %d)",
				       f2->name, f2->slot);
			printf("\n");
		}
#endif
		scr->fontset1 = f1;
		scr->fontset2 = f2;
		return (0);
	}
	return (ENXIO);
}

void
vga_init_screen(struct vga_config *vc, struct vgascreen *scr,
    const struct wsscreen_descr *type, int existing, uint32_t *attrp)
{
	int cpos;
	int res;

	scr->cfg = vc;
	scr->pcs.hdl = (struct pcdisplay_handle *)&vc->hdl;
	scr->pcs.type = type;
	scr->pcs.active = 0;
	scr->mindispoffset = 0;
	scr->maxdispoffset = 0x8000 - type->nrows * type->ncols * 2;

	if (existing) {
		cpos = vga_6845_read(&vc->hdl, cursorh) << 8;
		cpos |= vga_6845_read(&vc->hdl, cursorl);

		/* make sure we have a valid cursor position */
		if (cpos < 0 || cpos >= type->nrows * type->ncols)
			cpos = 0;

		scr->pcs.dispoffset = vga_6845_read(&vc->hdl, startadrh) << 9;
		scr->pcs.dispoffset |= vga_6845_read(&vc->hdl, startadrl) << 1;

		/* make sure we have a valid memory offset */
		if (scr->pcs.dispoffset < scr->mindispoffset ||
		    scr->pcs.dispoffset > scr->maxdispoffset)
			scr->pcs.dispoffset = scr->mindispoffset;
	} else {
		cpos = 0;
		scr->pcs.dispoffset = scr->mindispoffset;
	}
	scr->pcs.visibleoffset = scr->pcs.dispoffset;
	scr->vga_rollover = 0;

	scr->pcs.vc_crow = cpos / type->ncols;
	scr->pcs.vc_ccol = cpos % type->ncols;
	pcdisplay_cursor_init(&scr->pcs, existing);

#ifdef __alpha__
	if (!vc->hdl.vh_mono)
		/*
		 * DEC firmware uses a blue background.
		 */
		res = vga_pack_attr(scr, WSCOL_WHITE, WSCOL_BLUE,
				     WSATTR_WSCOLORS, attrp);
	else
#endif
	res = vga_pack_attr(scr, 0, 0, 0, attrp);
#ifdef DIAGNOSTIC
	if (res)
		panic("vga_init_screen: attribute botch");
#endif

	scr->pcs.mem = NULL;

	scr->fontset1 = scr->fontset2 = 0;
	if (vga_selectfont(vc, scr, 0, 0)) {
		if (scr == &vga_console_screen)
			panic("vga_init_screen: no font");
		else
			printf("vga_init_screen: no font\n");
	}

	vc->nscreens++;
	LIST_INSERT_HEAD(&vc->screens, scr, next);
}

void
vga_init(struct vga_config *vc, bus_space_tag_t iot, bus_space_tag_t memt)
{
	struct vga_handle *vh = &vc->hdl;
	u_int8_t mor;
	int i;

        vh->vh_iot = iot;
        vh->vh_memt = memt;

        if (bus_space_map(vh->vh_iot, 0x3c0, 0x10, 0, &vh->vh_ioh_vga))
                panic("vga_common_setup: can't map vga i/o");

	/* read "misc output register" */
	mor = bus_space_read_1(vh->vh_iot, vh->vh_ioh_vga, 0xc);
	vh->vh_mono = !(mor & 1);

	if (bus_space_map(vh->vh_iot, (vh->vh_mono ? 0x3b0 : 0x3d0), 0x10, 0,
			  &vh->vh_ioh_6845))
                panic("vga_common_setup: can't map 6845 i/o");

        if (bus_space_map(vh->vh_memt, 0xa0000, 0x20000, 0, &vh->vh_allmemh))
                panic("vga_common_setup: can't map mem space");

        if (bus_space_subregion(vh->vh_memt, vh->vh_allmemh,
				(vh->vh_mono ? 0x10000 : 0x18000), 0x8000,
				&vh->vh_memh))
                panic("vga_common_setup: mem subrange failed");

#ifdef __alpha__
	vga_pick_monitor_type(vc);
#endif

	vc->nscreens = 0;
	LIST_INIT(&vc->screens);
	vc->active = NULL;
#ifdef __alpha__
	if (vc->custom_list.screens != NULL)
		vc->currenttype = vc->custom_list.screens[0];
	else
#endif
		vc->currenttype =
		    vh->vh_mono ? &vga_stdscreen_mono : &vga_stdscreen;

	vc->vc_fonts[0] = &vga_builtinfont;
	for (i = 1; i < VGA_MAXFONT; i++)
		vc->vc_fonts[i] = NULL;

	vc->currentfontset1 = vc->currentfontset2 = 0;

	vga_save_palette(vc);
}

struct vga_config *
vga_common_attach(struct device *self, bus_space_tag_t iot,
    bus_space_tag_t memt, int type)
{
	return vga_extended_attach(self, iot, memt, type, NULL);
}

struct vga_config *
vga_extended_attach(struct device *self, bus_space_tag_t iot,
    bus_space_tag_t memt, int type, paddr_t (*map)(void *, off_t, int))
{
	int console;
	struct vga_config *vc;
	struct wsemuldisplaydev_attach_args aa;

	console = vga_is_console(iot, type);
	if (console)
		vga_console_attached = 1;

	if (type == -1)
		return NULL;

	if (console) {
		vc = &vga_console_vc;
	} else {
		vc = malloc(sizeof(*vc), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (vc == NULL)
			return NULL;
		vga_init(vc, iot, memt);
	}

	vc->vc_softc = self;
	vc->vc_type = type;
	vc->vc_mmap = map;

	aa.console = console;
#ifdef __alpha__
	if (vc->custom_list.screens != NULL)
		aa.scrdata = &vc->custom_list;
	else
#endif
		aa.scrdata =
		    vc->hdl.vh_mono ? &vga_screenlist_mono : &vga_screenlist;

	aa.accessops = &vga_accessops;
	aa.accesscookie = vc;
	aa.defaultscreens = 0;

        config_found_sm(self, &aa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);

	return vc;
}

int
vga_cnattach(bus_space_tag_t iot, bus_space_tag_t memt, int type, int check)
{
	uint32_t defattr;
	const struct wsscreen_descr *scr;

	if (check && !vga_common_probe(iot, memt))
		return (ENXIO);

	/* set up bus-independent VGA configuration */
	vga_init(&vga_console_vc, iot, memt);
	scr = vga_console_vc.currenttype;
	vga_init_screen(&vga_console_vc, &vga_console_screen, scr, 1, &defattr);

	vga_console_screen.pcs.active = 1;
	vga_console_vc.active = &vga_console_screen;

	wsdisplay_cnattach(scr, &vga_console_screen,
			   vga_console_screen.pcs.vc_ccol,
			   vga_console_screen.pcs.vc_crow,
			   defattr);

	vgaconsole = 1;
	vga_console_type = type;
	return (0);
}

int
vga_is_console(bus_space_tag_t iot, int type)
{
	if (vgaconsole &&
	    !vga_console_attached &&
	    iot == vga_console_vc.hdl.vh_iot &&
	    (vga_console_type == -1 || (type == vga_console_type)))
		return (1);
	return (0);
}

int
vga_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vga_config *vc = v;
	int mode;
#if NVGA_PCI > 0
	int error;

	if (vc->vc_type == WSDISPLAY_TYPE_PCIVGA &&
	    (error = vga_pci_ioctl(v, cmd, data, flag, p)) != ENOTTY)
		return (error);
#endif

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(int *)data = vc->vc_type;
		/* XXX should get detailed hardware information here */
		break;

	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if (mode == WSDISPLAYIO_MODE_EMUL) {
			vga_restore_fonts(vc);
			vga_restore_palette(vc);
		}
		break;

	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;

	case WSDISPLAYIO_GINFO:
	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		/* NONE of these operations are by the generic VGA driver. */
		return ENOTTY;
	}

	return (0);
}

paddr_t
vga_mmap(void *v, off_t offset, int prot)
{
	struct vga_config *vc = v;

	if (vc->vc_mmap != NULL)
		return (*vc->vc_mmap)(v, offset, prot);

	return (paddr_t)-1;
}

int
vga_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *defattrp)
{
	struct vga_config *vc = v;
	struct vgascreen *scr;

	if (vc->nscreens == 1) {
		/*
		 * When allocating the second screen, get backing store
		 * for the first one too.
		 * XXX We could be more clever and use video RAM.
		 */
		scr = LIST_FIRST(&vc->screens);
		scr->pcs.mem = mallocarray(scr->pcs.type->ncols,
		    scr->pcs.type->nrows * 2, M_DEVBUF, M_WAITOK);
	}

	scr = malloc(sizeof(struct vgascreen), M_DEVBUF, M_WAITOK);
	vga_init_screen(vc, scr, type, vc->nscreens == 0, defattrp);

	if (vc->nscreens == 1) {
		scr->pcs.active = 1;
		vc->active = scr;
		vc->currenttype = type;
	} else {
		scr->pcs.mem = mallocarray(type->ncols,
		    type->nrows * 2, M_DEVBUF, M_WAITOK);
		pcdisplay_eraserows(&scr->pcs, 0, type->nrows, *defattrp);
	}

	*cookiep = scr;
	*curxp = scr->pcs.vc_ccol;
	*curyp = scr->pcs.vc_crow;

	return (0);
}

void
vga_free_screen(void *v, void *cookie)
{
	struct vgascreen *vs = cookie;
	struct vga_config *vc = vs->cfg;

	LIST_REMOVE(vs, next);
	vc->nscreens--;
	if (vs != &vga_console_screen) {
		/*
		 * deallocating the one but last screen
		 * removes backing store for the last one
		 */
		if (vc->nscreens == 1)
			free(LIST_FIRST(&vc->screens)->pcs.mem, M_DEVBUF, 0);

		/* Last screen has no backing store */
		if (vc->nscreens != 0)
			free(vs->pcs.mem, M_DEVBUF, 0);

		free(vs, M_DEVBUF, sizeof *vs);
	} else
		panic("vga_free_screen: console");

	if (vc->active == vs)
		vc->active = NULL;
}

void
vga_setfont(struct vga_config *vc, struct vgascreen *scr)
{
	int fontslot1, fontslot2;

	fontslot1 = (scr->fontset1 ? scr->fontset1->slot : 0);
	fontslot2 = (scr->fontset2 ? scr->fontset2->slot : fontslot1);
	if (vc->currentfontset1 != fontslot1 ||
	    vc->currentfontset2 != fontslot2) {
		vga_setfontset(&vc->hdl, fontslot1, fontslot2);
		vc->currentfontset1 = fontslot1;
		vc->currentfontset2 = fontslot2;
	}
}

int
vga_show_screen(void *v, void *cookie, int waitok, void (*cb)(void *, int, int),
    void *cbarg)
{
	struct vgascreen *scr = cookie, *oldscr;
	struct vga_config *vc = scr->cfg;

	oldscr = vc->active; /* can be NULL! */
	if (scr == oldscr) {
		return (0);
	}

	vc->wantedscreen = cookie;
	vc->switchcb = cb;
	vc->switchcbarg = cbarg;
	if (cb) {
		timeout_set(&vc->vc_switch_timeout, vga_doswitch, vc);
		timeout_add(&vc->vc_switch_timeout, 0);
		return (EAGAIN);
	}

	vga_doswitch(vc);
	return (0);
}

void
vga_doswitch(void *arg)
{
	struct vga_config *vc = arg;
	struct vgascreen *scr, *oldscr;
	struct vga_handle *vh = &vc->hdl;
	const struct wsscreen_descr *type;
	int s;

	scr = vc->wantedscreen;
	if (!scr) {
		printf("vga_doswitch: disappeared\n");
		(*vc->switchcb)(vc->switchcbarg, EIO, 0);
		return;
	}

	type = scr->pcs.type;
	oldscr = vc->active; /* can be NULL! */
	if (scr == oldscr)
		return;
	s = spltty();
#ifdef DIAGNOSTIC
	if (oldscr) {
		if (!oldscr->pcs.active)
			panic("vga_show_screen: not active");
		if (oldscr->pcs.type != vc->currenttype)
			panic("vga_show_screen: bad type");
	}
	if (scr->pcs.active)
		panic("vga_show_screen: active");
#endif

	scr->vga_rollover = 0;

	if (oldscr) {
		const struct wsscreen_descr *oldtype = oldscr->pcs.type;

		oldscr->pcs.active = 0;
		bus_space_read_region_2(vh->vh_memt, vh->vh_memh,
					oldscr->pcs.dispoffset, oldscr->pcs.mem,
					oldtype->ncols * oldtype->nrows);
	}

	if (vc->currenttype != type) {
		vga_setscreentype(vh, type);
		vc->currenttype = type;
	}

	vga_restore_fonts(vc);
	vga_setfont(vc, scr);
	vga_restore_palette(vc);

	scr->pcs.visibleoffset = scr->pcs.dispoffset = scr->mindispoffset;
	if (!oldscr || (scr->pcs.dispoffset != oldscr->pcs.dispoffset)) {
		vga_6845_write(vh, startadrh, scr->pcs.dispoffset >> 9);
		vga_6845_write(vh, startadrl, scr->pcs.dispoffset >> 1);
	}

	bus_space_write_region_2(vh->vh_memt, vh->vh_memh,
				scr->pcs.dispoffset, scr->pcs.mem,
				type->ncols * type->nrows);
	scr->pcs.active = 1;
	splx(s);

	vc->active = scr;

	pcdisplay_cursor_reset(&scr->pcs);
	pcdisplay_cursor(&scr->pcs, scr->pcs.cursoron,
			 scr->pcs.vc_crow, scr->pcs.vc_ccol);

	vc->wantedscreen = 0;
	if (vc->switchcb)
		(*vc->switchcb)(vc->switchcbarg, 0, 0);
}

int
vga_load_font(void *v, void *cookie, struct wsdisplay_font *data)
{
	struct vga_config *vc = v;
	struct vgascreen *scr = cookie;
	char *name2;
	int res, slot;
	struct vgafont *f;

	if (data->data == NULL) {
		if (scr == NULL)
			return EINVAL;

		if ((name2 = data->name) != NULL) {
			while (*name2 && *name2 != ',')
				name2++;
			if (*name2)
				*name2++ = '\0';
		}
		res = vga_selectfont(vc, scr, data->name, name2);
		if (res == 0)
			vga_setfont(vc, scr);
		return (res);
	}

	if (data->fontwidth != 8 || data->stride != 1)
		return (EINVAL); /* XXX 1 byte per line */
	if (data->firstchar != 0 || data->numchars != 256)
		return (EINVAL);

	if (data->index < 0) {
		for (slot = 0; slot < VGA_MAXFONT; slot++)
			if (!vc->vc_fonts[slot])
				break;
	} else
		slot = data->index;

	if (slot >= VGA_MAXFONT)
		return (ENOSPC);

	if (vc->vc_fonts[slot] != NULL)
		return (EEXIST);
	f = malloc(sizeof(struct vgafont), M_DEVBUF, M_WAITOK | M_CANFAIL);
	if (f == NULL)
		return (ENOMEM);
	strlcpy(f->name, data->name, sizeof(f->name));
	f->height = data->fontheight;
	f->encoding = data->encoding;
#ifdef notyet
	f->firstchar = data->firstchar;
	f->numchars = data->numchars;
#endif
#ifdef VGAFONTDEBUG
	printf("vga: load %s (8x%d, enc %d) font to slot %d\n", f->name,
	       f->height, f->encoding, slot);
#endif
	vga_loadchars(&vc->hdl, slot, 0, 256, f->height, data->data);
	f->slot = slot;
	f->fontdata = data->data;
	vc->vc_fonts[slot] = f;
	data->cookie = f;
	data->index = slot;

	return (0);
}

int
vga_list_font(void *v, struct wsdisplay_font *data)
{
	struct vga_config *vc = v;
	struct vgafont *f;

	if (data->index < 0 || data->index >= VGA_MAXFONT)
		return EINVAL;

	if ((f = vc->vc_fonts[data->index]) == NULL)
		return EINVAL;

	strlcpy(data->name, f->name, sizeof data->name);
#ifdef notyet
	data->firstchar = f->firstchar;
	data->numchars = f->numchars;
#else
	data->firstchar = 0;
	data->numchars = 256;
#endif
	data->encoding = f->encoding;
	data->fontwidth = 8;
	data->fontheight = f->height;
	data->stride = 1;
	data->bitorder = data->byteorder = WSDISPLAY_FONTORDER_L2R;

	return (0);
}

void
vga_scrollback(void *v, void *cookie, int lines)
{
	struct vga_config *vc = v;
	struct vgascreen *scr = cookie;
	struct vga_handle *vh = &vc->hdl;

	if (lines == 0) {
		if (scr->pcs.visibleoffset == scr->pcs.dispoffset)
			return;

		scr->pcs.visibleoffset = scr->pcs.dispoffset;	/* reset */
	}
	else {
		int vga_scr_end;
		int margin = scr->pcs.type->ncols * 2;
		int ul, we, p, st;

		vga_scr_end = (scr->pcs.dispoffset + scr->pcs.type->ncols *
		    scr->pcs.type->nrows * 2);
		if (scr->vga_rollover > vga_scr_end + margin) {
			ul = vga_scr_end;
			we = scr->vga_rollover + scr->pcs.type->ncols * 2;
		} else {
			ul = 0;
			we = 0x8000;
		}
		p = (scr->pcs.visibleoffset - ul + we) % we + lines *
		    (scr->pcs.type->ncols * 2);
		st = (scr->pcs.dispoffset - ul + we) % we;
		if (p < margin)
			p = 0;
		if (p > st - margin)
			p = st;
		scr->pcs.visibleoffset = (p + ul) % we;
	}
	
	/* update visible position */
	vga_6845_write(vh, startadrh, scr->pcs.visibleoffset >> 9);
	vga_6845_write(vh, startadrl, scr->pcs.visibleoffset >> 1);
}

int
vga_pack_attr(void *id, int fg, int bg, int flags, uint32_t *attrp)
{
	struct vgascreen *scr = id;
	struct vga_config *vc = scr->cfg;

	if (vc->hdl.vh_mono) {
		if (flags & WSATTR_WSCOLORS)
			return (EINVAL);
		if (flags & WSATTR_REVERSE)
			*attrp = 0x70;
		else
			*attrp = 0x07;
		if (flags & WSATTR_UNDERLINE)
			*attrp |= FG_UNDERLINE;
		if (flags & WSATTR_HILIT)
			*attrp |= FG_INTENSE;
	} else {
		if (flags & (WSATTR_UNDERLINE | WSATTR_REVERSE))
			return (EINVAL);
		if (flags & WSATTR_WSCOLORS)
			*attrp = fgansitopc[fg & 7] | bgansitopc[bg & 7];
		else
			*attrp = 7;
		if ((flags & WSATTR_HILIT) || (fg & 8) || (bg & 8))
			*attrp += 8;
	}
	if (flags & WSATTR_BLINK)
		*attrp |= FG_BLINK;
	return (0);
}

void
vga_unpack_attr(void *id, uint32_t attr, int *fg, int *bg, int *ul)
{
	struct vgascreen *scr = id;
	struct vga_config *vc = scr->cfg;

	if (vc->hdl.vh_mono) {
		*fg = (attr & 0x07) == 0x07 ? WSCOL_WHITE : WSCOL_BLACK;
		*bg = attr & 0x70 ? WSCOL_WHITE : WSCOL_BLACK;
		if (ul != NULL)
			*ul = *fg != WSCOL_WHITE && (attr & 0x01) ? 1 : 0;
	} else {
		*fg = pctoansi[attr & 0x07];
		*bg = pctoansi[(attr & 0x70) >> 4];
		if (ul != NULL)
			*ul = 0;
	}
	if (attr & FG_INTENSE)
		*fg += 8;
}

int
vga_copyrows(void *id, int srcrow, int dstrow, int nrows)
{
	struct vgascreen *scr = id;
	bus_space_tag_t memt = scr->pcs.hdl->ph_memt;
	bus_space_handle_t memh = scr->pcs.hdl->ph_memh;
	int ncols = scr->pcs.type->ncols;
	bus_size_t srcoff, dstoff;
	int s;

	srcoff = srcrow * ncols + 0;
	dstoff = dstrow * ncols + 0;

	s = spltty();
	if (scr->pcs.active) {
		if (dstrow == 0 && (srcrow + nrows == scr->pcs.type->nrows)) {
#ifdef PCDISPLAY_SOFTCURSOR
			int cursoron = scr->pcs.cursoron;

			/* NOTE this assumes pcdisplay_cursor() never fails */
			if (cursoron)
				pcdisplay_cursor(&scr->pcs, 0,
				    scr->pcs.vc_crow, scr->pcs.vc_ccol);
#endif
			/* scroll up whole screen */
			if ((scr->pcs.dispoffset + srcrow * ncols * 2)
			    <= scr->maxdispoffset) {
				scr->pcs.dispoffset += srcrow * ncols * 2;
			} else {
				bus_space_copy_2(memt, memh,
					scr->pcs.dispoffset + srcoff * 2,
					memh, scr->mindispoffset,
					nrows * ncols);
				scr->vga_rollover = scr->pcs.dispoffset;
				scr->pcs.dispoffset = scr->mindispoffset;
			}
			scr->pcs.visibleoffset = scr->pcs.dispoffset;
			vga_6845_write(&scr->cfg->hdl, startadrh,
				       scr->pcs.dispoffset >> 9);
			vga_6845_write(&scr->cfg->hdl, startadrl,
				       scr->pcs.dispoffset >> 1);
#ifdef PCDISPLAY_SOFTCURSOR
			/* NOTE this assumes pcdisplay_cursor() never fails */
			if (cursoron)
				pcdisplay_cursor(&scr->pcs, 1,
				    scr->pcs.vc_crow, scr->pcs.vc_ccol);
#endif
		} else {
			bus_space_copy_2(memt, memh,
					scr->pcs.dispoffset + srcoff * 2,
					memh, scr->pcs.dispoffset + dstoff * 2,
					nrows * ncols);
		}
	} else
		bcopy(&scr->pcs.mem[srcoff], &scr->pcs.mem[dstoff],
		      nrows * ncols * 2);
	splx(s);

	return 0;
}

int _vga_mapchar(void *, struct vgafont *, int, unsigned int *);

int
_vga_mapchar(void *id, struct vgafont *font, int uni, unsigned int *index)
{

	switch (font->encoding) {
	case WSDISPLAY_FONTENC_ISO:
		if (uni < 256) {
			*index = uni;
			return (5);
		} else {
			*index = '?';
			return (0);
		}
		break;
	case WSDISPLAY_FONTENC_IBM:
		return (pcdisplay_mapchar(id, uni, index));
	default:
#ifdef VGAFONTDEBUG
		printf("_vga_mapchar: encoding=%d\n", font->encoding);
#endif
		*index = '?';
		return (0);
	}
}

int
vga_mapchar(void *id, int uni, unsigned int *index)
{
	struct vgascreen *scr = id;
	unsigned int idx1, idx2;
	int res1, res2;

	res1 = 0;
	idx1 = ' '; /* space */
	if (scr->fontset1)
		res1 = _vga_mapchar(id, scr->fontset1, uni, &idx1);
	res2 = -1;
	if (scr->fontset2) {
		KASSERT(VGA_SCREEN_CANTWOFONTS(scr->pcs.type));
		res2 = _vga_mapchar(id, scr->fontset2, uni, &idx2);
	}
	if (res2 >= res1) {
		*index = idx2 | 0x0800; /* attribute bit 3 */
		return (res2);
	}
	*index = idx1;
	return (res1);
}

int
vga_putchar(void *c, int row, int col, u_int uc, uint32_t attr)
{
	struct vgascreen *scr = c;
	int rc;
	int s;
	
	s = spltty();
	if (scr->pcs.active && scr->pcs.visibleoffset != scr->pcs.dispoffset)
		vga_scrollback(scr->cfg, scr, 0);
	rc = pcdisplay_putchar(c, row, col, uc, attr);
	splx(s);

	return rc;
}

void
vga_burner(void *v, u_int on, u_int flags)
{
	struct vga_config *vc = v;
	struct vga_handle *vh = &vc->hdl;
	u_int8_t r;
	int s;

	s = splhigh();
	vga_ts_write(vh, syncreset, 0x01);
	if (on) {
		vga_ts_write(vh, mode, (vga_ts_read(vh, mode) & ~0x20));
		r = vga_6845_read(vh, mode) | 0x80;
		DELAY(10000);
		vga_6845_write(vh, mode, r);
	} else {
		vga_ts_write(vh, mode, (vga_ts_read(vh, mode) | 0x20));
		if (flags & WSDISPLAY_BURN_VBLANK) {
			r = vga_6845_read(vh, mode) & ~0x80;
			DELAY(10000);
			vga_6845_write(vh, mode, r);
		}
	}
	vga_ts_write(vh, syncreset, 0x03);
	splx(s);
}

int
vga_getchar(void *c, int row, int col, struct wsdisplay_charcell *cell)
{
	struct vga_config *vc = c;
	
	return (pcdisplay_getchar(vc->active, row, col, cell));
}	

void
vga_save_palette(struct vga_config *vc)
{
	struct vga_handle *vh = &vc->hdl;
	uint i;
	uint8_t *palette = vc->vc_palette;

	if (vh->vh_mono)
		return;

	vga_raw_write(vh, VGA_DAC_MASK, 0xff);
	vga_raw_write(vh, VGA_DAC_READ, 0x00);
	for (i = 0; i < 3 * 256; i++)
		*palette++ = vga_raw_read(vh, VGA_DAC_DATA);
}

void
vga_restore_palette(struct vga_config *vc)
{
	struct vga_handle *vh = &vc->hdl;
	uint i;
	uint8_t *palette = vc->vc_palette;

	if (vh->vh_mono)
		return;

	vga_raw_write(vh, VGA_DAC_MASK, 0xff);
	vga_raw_write(vh, VGA_DAC_WRITE, 0x00);
	for (i = 0; i < 3 * 256; i++)
		vga_raw_write(vh, VGA_DAC_DATA, *palette++);
}

void
vga_restore_fonts(struct vga_config *vc)
{
	int slot;
	struct vgafont *f;

	for (slot = 0; slot < VGA_MAXFONT; slot++) {
		f = vc->vc_fonts[slot];
		if (f == NULL || f->fontdata == NULL)
			continue;

		vga_loadchars(&vc->hdl, slot, 0, 256, f->height, f->fontdata);
	}
}

#ifdef __alpha__
void
vga_pick_monitor_type(struct vga_config *vc)
{
	struct vga_handle *vh = &vc->hdl;

	/*
	 * The Tadpole Alphabook1 uses a 800x600 flat panel in text mode,
	 * causing the display console to really be 100x37 instead of the
	 * usual 80x25.
	 * We attempt to detect this here by checking the CRTC registers.
	 */
	unsigned int hend, oflow, vend;
	unsigned int width, height;

	hend = vga_6845_read(vh, hdisple);
	oflow = vga_6845_read(vh, overfll);
	vend = vga_6845_read(vh, vde);
	if (oflow & 0x02)
		vend |= 0x100;
	if (oflow & 0x40)
		vend |= 0x200;

	width = hend + 1;
	height = (vend + 1) / 16;

	/* check that the values sound plausible */
	if ((width > 80 && width <= 128) && (height > 25 && height <= 50)) {
		snprintf(vc->custom_scr.name, sizeof(vc->custom_scr.name),
		    "%ux%u", width, height);
		vc->custom_scr.ncols = width;
		vc->custom_scr.nrows = height;
		vc->custom_scr.textops = &vga_emulops;
		vc->custom_scr.fontwidth = 8;
		vc->custom_scr.fontheight = 16;
		vc->custom_scr.capabilities =
		    WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_BLINK;
		vc->custom_scrlist[0] = &vc->custom_scr;
		vc->custom_list.nscreens = 1;
		vc->custom_list.screens =
		    (const struct wsscreen_descr **)vc->custom_scrlist;
	}
}
#endif

struct cfdriver vga_cd = {
	NULL, "vga", DV_DULL
};
