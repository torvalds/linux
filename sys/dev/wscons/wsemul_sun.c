/* $OpenBSD: wsemul_sun.c,v 1.37 2023/07/24 17:03:32 miod Exp $ */
/* $NetBSD: wsemul_sun.c,v 1.11 2000/01/05 11:19:36 drochner Exp $ */

/*
 * Copyright (c) 2007, 2013 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
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
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file implements a sun terminal personality for wscons.
 *
 * Derived from old rcons code.
 * Color support from NetBSD's rcons color code, and wsemul_vt100.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>

#include <dev/wscons/wscons_features.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/ascii.h>

void	*wsemul_sun_cnattach(const struct wsscreen_descr *, void *,
    int, int, uint32_t);
void	*wsemul_sun_attach(int, const struct wsscreen_descr *,
    void *, int, int, void *, uint32_t);
u_int	wsemul_sun_output(void *, const u_char *, u_int, int);
int	wsemul_sun_translate(void *, kbd_t, keysym_t, const u_char **);
void	wsemul_sun_detach(void *, u_int *, u_int *);
void	wsemul_sun_resetop(void *, enum wsemul_resetops);

const struct wsemul_ops wsemul_sun_ops = {
	"sun",
	wsemul_sun_cnattach,
	wsemul_sun_attach,
	wsemul_sun_output,
	wsemul_sun_translate,
	wsemul_sun_detach,
	wsemul_sun_resetop
};

#define	SUN_EMUL_STATE_NORMAL	0	/* normal processing */
#define	SUN_EMUL_STATE_HAVEESC	1	/* seen start of ctl seq */
#define	SUN_EMUL_STATE_CONTROL	2	/* processing ESC [ ctl seq */
#define	SUN_EMUL_STATE_PERCENT	3	/* processing ESC % ctl seq */

#define	SUN_EMUL_FLAGS_UTF8	0x01	/* UTF-8 character set */

#define	SUN_EMUL_NARGS	2		/* max # of args to a command */

struct wsemul_sun_emuldata {
	const struct wsdisplay_emulops *emulops;
	struct wsemul_abortstate abortstate;
	void *emulcookie;
	void *cbcookie;
	int scrcapabilities;
	u_int nrows, ncols, crow, ccol;
	uint32_t defattr;		/* default attribute (rendition) */

	u_int state;			/* processing state */
	u_int flags;
	u_int args[SUN_EMUL_NARGS];	/* command args, if CONTROL */
	int nargs;			/* number of args */

	u_int scrolldist;		/* distance to scroll */
	uint32_t curattr, bkgdattr;	/* currently used attribute */
	uint32_t kernattr;		/* attribute for kernel output */
	int attrflags, fgcol, bgcol;	/* properties of curattr */

	struct wsemul_inputstate instate;	/* userland input state */
	struct wsemul_inputstate kstate;	/* kernel input state */

#ifdef HAVE_UTF8_SUPPORT
	u_char translatebuf[4];
#else
	u_char translatebuf[1];
#endif

#ifdef DIAGNOSTIC
	int console;
#endif
};

void	wsemul_sun_init(struct wsemul_sun_emuldata *,
	    const struct wsscreen_descr *, void *, int, int, uint32_t);
int	wsemul_sun_jump_scroll(struct wsemul_sun_emuldata *, const u_char *,
	    u_int, int);
void	wsemul_sun_reset(struct wsemul_sun_emuldata *);
int	wsemul_sun_output_lowchars(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *, int);
int	wsemul_sun_output_normal(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *, int);
int	wsemul_sun_output_haveesc(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *);
int	wsemul_sun_output_control(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *);
int	wsemul_sun_output_percent(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *);
int	wsemul_sun_control(struct wsemul_sun_emuldata *,
	    struct wsemul_inputstate *);
int	wsemul_sun_selectattribute(struct wsemul_sun_emuldata *, int, int, int,
	    uint32_t *, uint32_t *);
int	wsemul_sun_scrollup(struct wsemul_sun_emuldata *, u_int);

struct wsemul_sun_emuldata wsemul_sun_console_emuldata;

/* some useful utility macros */
#define	ARG(n,c) \
	((n) >= edp->nargs ? 0 : edp->args[(n) + MAX(0, edp->nargs - (c))])
#define	NORMALIZE(arg)		((arg) != 0 ? (arg) : 1)
#define	COLS_LEFT		(edp->ncols - 1 - edp->ccol)
#define	ROWS_LEFT		(edp->nrows - 1 - edp->crow)

void
wsemul_sun_init(struct wsemul_sun_emuldata *edp,
    const struct wsscreen_descr *type, void *cookie, int ccol, int crow,
    uint32_t defattr)
{
	edp->emulops = type->textops;
	edp->emulcookie = cookie;
	edp->scrcapabilities = type->capabilities;
	edp->nrows = type->nrows;
	edp->ncols = type->ncols;
	edp->crow = crow;
	edp->ccol = ccol;
	edp->defattr = defattr;
	wsemul_reset_abortstate(&edp->abortstate);
}

void
wsemul_sun_reset(struct wsemul_sun_emuldata *edp)
{
	edp->flags = 0;
	edp->state = SUN_EMUL_STATE_NORMAL;
	edp->bkgdattr = edp->curattr = edp->defattr;
	edp->attrflags = 0;
	edp->fgcol = WSCOL_BLACK;
	edp->bgcol = WSCOL_WHITE;
	edp->scrolldist = 1;
	edp->instate.inchar = 0;
	edp->instate.lbound = 0;
	edp->instate.mbleft = 0;
	edp->kstate.inchar = 0;
	edp->kstate.lbound = 0;
	edp->kstate.mbleft = 0;
}

void *
wsemul_sun_cnattach(const struct wsscreen_descr *type, void *cookie, int ccol,
    int crow, uint32_t defattr)
{
	struct wsemul_sun_emuldata *edp;
	int res;

	edp = &wsemul_sun_console_emuldata;
	wsemul_sun_init(edp, type, cookie, ccol, crow, defattr);

#ifndef WS_KERNEL_FG
#define WS_KERNEL_FG WSCOL_BLACK
#endif
#ifndef WS_KERNEL_BG
#define WS_KERNEL_BG WSCOL_WHITE
#endif
#ifndef WS_KERNEL_COLATTR
#define WS_KERNEL_COLATTR 0
#endif
#ifndef WS_KERNEL_MONOATTR
#define WS_KERNEL_MONOATTR 0
#endif
	if (type->capabilities & WSSCREEN_WSCOLORS)
		res = (*edp->emulops->pack_attr)(cookie,
					    WS_KERNEL_FG, WS_KERNEL_BG,
					    WS_KERNEL_COLATTR | WSATTR_WSCOLORS,
					    &edp->kernattr);
	else
		res = (*edp->emulops->pack_attr)(cookie, 0, 0,
					    WS_KERNEL_MONOATTR,
					    &edp->kernattr);
	if (res)
		edp->kernattr = defattr;

	edp->cbcookie = NULL;

#ifdef DIAGNOSTIC
	edp->console = 1;
#endif

	wsemul_sun_reset(edp);
	return (edp);
}

void *
wsemul_sun_attach(int console, const struct wsscreen_descr *type, void *cookie,
    int ccol, int crow, void *cbcookie, uint32_t defattr)
{
	struct wsemul_sun_emuldata *edp;

	if (console) {
		edp = &wsemul_sun_console_emuldata;
#ifdef DIAGNOSTIC
		KASSERT(edp->console == 1);
#endif
	} else {
		edp = malloc(sizeof *edp, M_DEVBUF, M_NOWAIT);
		if (edp == NULL)
			return (NULL);
		wsemul_sun_init(edp, type, cookie, ccol, crow, defattr);

#ifdef DIAGNOSTIC
		edp->console = 0;
#endif
	}

	edp->cbcookie = cbcookie;

	wsemul_sun_reset(edp);
	return (edp);
}

int
wsemul_sun_output_lowchars(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate, int kernel)
{
	u_int n;
	int rc = 0;

	switch (instate->inchar) {
	case ASCII_NUL:
	default:
		/* ignore */
		break;

	case ASCII_BEL:		/* "Bell (BEL)" */
		wsdisplay_emulbell(edp->cbcookie);
		break;

	case ASCII_BS:		/* "Backspace (BS)" */
		if (edp->ccol > 0)
			edp->ccol--;
		break;

	case ASCII_CR:		/* "Return (CR)" */
		edp->ccol = 0;
		break;

	case ASCII_HT:		/* "Tab (TAB)" */
		n = min(8 - (edp->ccol & 7), COLS_LEFT);
		if (n != 0) {
			WSEMULOP(rc, edp, &edp->abortstate, erasecols,
			    (edp->emulcookie, edp->crow, edp->ccol, n,
			     kernel ? edp->kernattr : edp->bkgdattr));
			if (rc != 0)
				break;
			edp->ccol += n;
		}
		break;

	case ASCII_FF:		/* "Form Feed (FF)" */
		WSEMULOP(rc, edp, &edp->abortstate, eraserows,
		    (edp->emulcookie, 0, edp->nrows, edp->bkgdattr));
		if (rc != 0)
			break;
		edp->ccol = edp->crow = 0;
		break;

	case ASCII_VT:		/* "Reverse Line Feed" */
		if (edp->crow > 0)
			edp->crow--;
		break;

	case ASCII_ESC:		/* "Escape (ESC)" */
		if (kernel) {
			printf("wsemul_sun_output_lowchars: ESC in kernel "
			    "output ignored\n");
			break;	/* ignore the ESC */
		}

		edp->state = SUN_EMUL_STATE_HAVEESC;
		break;

	case ASCII_LF:		/* "Line Feed (LF)" */
		/* if the cur line isn't the last, incr and leave. */
		if (ROWS_LEFT > 0)
			edp->crow++;
		else {
			rc = wsemul_sun_scrollup(edp, edp->scrolldist);
			if (rc != 0)
				break;
		}
		break;
	}

	return rc;
}

int
wsemul_sun_output_normal(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate, int kernel)
{
	int rc;
	u_int outchar;

	(*edp->emulops->mapchar)(edp->emulcookie, instate->inchar, &outchar);
	WSEMULOP(rc, edp, &edp->abortstate, putchar,
	    (edp->emulcookie, edp->crow, edp->ccol,
	     outchar, kernel ? edp->kernattr : edp->curattr));
	if (rc != 0)
		return rc;

	if (++edp->ccol >= edp->ncols) {
		/* if the cur line isn't the last, incr and leave. */
		if (ROWS_LEFT > 0)
			edp->crow++;
		else {
			rc = wsemul_sun_scrollup(edp, edp->scrolldist);
			if (rc != 0) {
				/* undo line wrap */
				edp->ccol--;

				return rc;
			}
		}
		edp->ccol = 0;
	}

	return 0;
}

int
wsemul_sun_output_haveesc(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate)
{
	switch (instate->inchar) {
	case '[':		/* continuation of multi-char sequence */
		edp->nargs = 0;
		bzero(edp->args, sizeof (edp->args));
		edp->state = SUN_EMUL_STATE_CONTROL;
		break;
#ifdef HAVE_UTF8_SUPPORT
	case '%':
		edp->state = SUN_EMUL_STATE_PERCENT;
		break;
#endif
	default:
#ifdef DEBUG
		printf("ESC %x unknown\n", instate->inchar);
#endif
		edp->state = SUN_EMUL_STATE_NORMAL;	/* XXX is this wise? */
		break;
	}
	return 0;
}

int
wsemul_sun_control(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate)
{
	u_int n, src, dst;
	int flags, fgcol, bgcol;
	uint32_t attr, bkgdattr;
	int rc = 0;

	switch (instate->inchar) {
	case '@':		/* "Insert Character (ICH)" */
		n = min(NORMALIZE(ARG(0,1)), COLS_LEFT + 1);
		src = edp->ccol;
		dst = edp->ccol + n;
		if (dst < edp->ncols) {
			WSEMULOP(rc, edp, &edp->abortstate, copycols,
			    (edp->emulcookie, edp->crow, src, dst,
			     edp->ncols - dst));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    (edp->emulcookie, edp->crow, src, n, edp->bkgdattr));
		break;

	case 'A':		/* "Cursor Up (CUU)" */
		edp->crow -= min(NORMALIZE(ARG(0,1)), edp->crow);
		break;

	case 'E':		/* "Cursor Next Line (CNL)" */
		edp->ccol = 0;
		/* FALLTHROUGH */
	case 'B':		/* "Cursor Down (CUD)" */
		edp->crow += min(NORMALIZE(ARG(0,1)), ROWS_LEFT);
		break;

	case 'C':		/* "Cursor Forward (CUF)" */
		edp->ccol += min(NORMALIZE(ARG(0,1)), COLS_LEFT);
		break;

	case 'D':		/* "Cursor Backward (CUB)" */
		edp->ccol -= min(NORMALIZE(ARG(0,1)), edp->ccol);
		break;

	case 'f':		/* "Horizontal And Vertical Position (HVP)" */
	case 'H':		/* "Cursor Position (CUP)" */
		edp->crow = min(NORMALIZE(ARG(0,2)), edp->nrows) - 1;
		edp->ccol = min(NORMALIZE(ARG(1,2)), edp->ncols) - 1;
		break;

	case 'J':		/* "Erase in Display (ED)" */
		if (ROWS_LEFT > 0) {
			WSEMULOP(rc, edp, &edp->abortstate, eraserows,
			    (edp->emulcookie, edp->crow + 1, ROWS_LEFT,
			     edp->bkgdattr));
			if (rc != 0)
				break;
		}
		/* FALLTHROUGH */
	case 'K':		/* "Erase in Line (EL)" */
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    (edp->emulcookie, edp->crow, edp->ccol, COLS_LEFT + 1,
		     edp->bkgdattr));
		break;

	case 'L':		/* "Insert Line (IL)" */
		n = min(NORMALIZE(ARG(0,1)), ROWS_LEFT + 1);
		src = edp->crow;
		dst = edp->crow + n;
		if (dst < edp->nrows) {
			WSEMULOP(rc, edp, &edp->abortstate, copyrows,
			    (edp->emulcookie, src, dst, edp->nrows - dst));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, eraserows,
		    (edp->emulcookie, src, n, edp->bkgdattr));
		break;

	case 'M':		/* "Delete Line (DL)" */
		n = min(NORMALIZE(ARG(0,1)), ROWS_LEFT + 1);
		src = edp->crow + n;
		dst = edp->crow;
		if (src < edp->nrows) {
			WSEMULOP(rc, edp, &edp->abortstate, copyrows,
			    (edp->emulcookie, src, dst, edp->nrows - src));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, eraserows,
		    (edp->emulcookie, dst + edp->nrows - src, n,
		     edp->bkgdattr));
		break;

	case 'P':		/* "Delete Character (DCH)" */
		n = min(NORMALIZE(ARG(0,1)), COLS_LEFT + 1);
		src = edp->ccol + n;
		dst = edp->ccol;
		if (src < edp->ncols) {
			WSEMULOP(rc, edp, &edp->abortstate, copycols,
			    (edp->emulcookie, edp->crow, src, dst,
			     edp->ncols - src));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    (edp->emulcookie, edp->crow, edp->ncols - n, n,
		     edp->bkgdattr));
		break;

	case 'm':		/* "Select Graphic Rendition (SGR)" */
		flags = edp->attrflags;
		fgcol = edp->fgcol;
		bgcol = edp->bgcol;

		for (n = 0; n < edp->nargs; n++) {
			switch (ARG(n,edp->nargs)) {
			/* Clear all attributes || End underline */
			case 0:
				if (n == edp->nargs - 1) {
					edp->bkgdattr =
					    edp->curattr = edp->defattr;
					edp->attrflags = 0;
					edp->fgcol = WSCOL_BLACK;
					edp->bgcol = WSCOL_WHITE;
					return 0;
				}
				flags = 0;
				fgcol = WSCOL_BLACK;
				bgcol = WSCOL_WHITE;
				break;
			/* Begin bold */
			case 1:
				flags |= WSATTR_HILIT;
				break;
			/* Begin underline */
			case 4:
				flags |= WSATTR_UNDERLINE;
				break;
			/* Begin reverse */
			case 7:
				flags |= WSATTR_REVERSE;
				break;
			/* ANSI foreground color */
			case 30: case 31: case 32: case 33:
			case 34: case 35: case 36: case 37:
				fgcol = ARG(n,edp->nargs) - 30;
				break;
			/* ANSI background color */
			case 40: case 41: case 42: case 43:
			case 44: case 45: case 46: case 47:
				bgcol = ARG(n,edp->nargs) - 40;
				break;
			}
		}
setattr:
		if (wsemul_sun_selectattribute(edp, flags, fgcol, bgcol, &attr,
		    &bkgdattr)) {
#ifdef DEBUG
			printf("error allocating attr %d/%d/%x\n",
			    fgcol, bgcol, flags);
#endif
		} else {
			edp->curattr = attr;
			edp->bkgdattr = bkgdattr;
			edp->attrflags = flags;
			edp->fgcol = fgcol;
			edp->bgcol = bgcol;
		}
		break;

	case 'p':		/* "Black On White (SUNBOW)" */
		flags = 0;
		fgcol = WSCOL_BLACK;
		bgcol = WSCOL_WHITE;
		goto setattr;

	case 'q':		/* "White On Black (SUNWOB)" */
		flags = 0;
		fgcol = WSCOL_WHITE;
		bgcol = WSCOL_BLACK;
		goto setattr;

	case 'r':		/* "Set Scrolling (SUNSCRL)" */
		edp->scrolldist = min(ARG(0,1), edp->nrows);
		break;

	case 's':		/* "Reset Terminal Emulator (SUNRESET)" */
		wsemul_sun_reset(edp);
		break;
	}

	return rc;
}

int
wsemul_sun_output_control(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate)
{
	int oargs;
	int rc;

	switch (instate->inchar) {
	case '0': case '1': case '2': case '3': case '4': /* argument digit */
	case '5': case '6': case '7': case '8': case '9':
		/*
		 * If we receive more arguments than we are expecting,
		 * discard the earliest arguments.
		 */
		if (edp->nargs > SUN_EMUL_NARGS - 1) {
			bcopy(edp->args + 1, edp->args,
			    (SUN_EMUL_NARGS - 1) * sizeof(edp->args[0]));
			edp->args[edp->nargs = SUN_EMUL_NARGS - 1] = 0;
		}
		edp->args[edp->nargs] = (edp->args[edp->nargs] * 10) +
		    (instate->inchar - '0');
		break;

	case ';':		/* argument terminator */
		if (edp->nargs < SUN_EMUL_NARGS)
			edp->nargs++;
		break;

	default:		/* end of escape sequence */
		oargs = edp->nargs;
		if (edp->nargs < SUN_EMUL_NARGS)
			edp->nargs++;
		rc = wsemul_sun_control(edp, instate);
		if (rc != 0) {
			/* undo nargs progress */
			edp->nargs = oargs;

			return rc;
		}
		edp->state = SUN_EMUL_STATE_NORMAL;
		break;
	}

	return 0;
}

#ifdef HAVE_UTF8_SUPPORT
int
wsemul_sun_output_percent(struct wsemul_sun_emuldata *edp,
    struct wsemul_inputstate *instate)
{
	switch (instate->inchar) {
	case 'G':
		edp->flags |= SUN_EMUL_FLAGS_UTF8;
		edp->kstate.mbleft = edp->instate.mbleft = 0;
		break;
	case '@':
		edp->flags &= ~SUN_EMUL_FLAGS_UTF8;
		break;
	}
	edp->state = SUN_EMUL_STATE_NORMAL;
	return 0;
}
#endif

u_int
wsemul_sun_output(void *cookie, const u_char *data, u_int count, int kernel)
{
	struct wsemul_sun_emuldata *edp = cookie;
	struct wsemul_inputstate *instate;
	u_int prev_count, processed = 0;
#ifdef HAVE_JUMP_SCROLL
	int lines;
#endif
	int rc = 0;

#ifdef DIAGNOSTIC
	if (kernel && !edp->console)
		panic("wsemul_sun_output: kernel output, not console");
#endif

	instate = kernel ? &edp->kstate : &edp->instate;

	switch (edp->abortstate.state) {
	case ABORT_FAILED_CURSOR:
		/*
		 * If we could not display the cursor back, we pretended not
		 * having been able to process the last byte. But this
		 * is a lie, so compensate here.
		 */
		data++, count--;
		processed++;
		wsemul_reset_abortstate(&edp->abortstate);
		break;
	case ABORT_OK:
		/* remove cursor image */
		rc = (*edp->emulops->cursor)
		    (edp->emulcookie, 0, edp->crow, edp->ccol);
		if (rc != 0)
			return 0;
		break;
	default:
		break;
	}

	for (;;) {
#ifdef HAVE_JUMP_SCROLL
		switch (edp->abortstate.state) {
		case ABORT_FAILED_JUMP_SCROLL:
			/*
			 * If we failed a previous jump scroll attempt, we
			 * need to try to resume it with the same distance.
			 * We can not recompute it since there might be more
			 * bytes in the tty ring, causing a different result.
			 */
			lines = edp->abortstate.lines;
			break;
		case ABORT_OK:
			/*
			 * If scrolling is not disabled and we are the bottom of
			 * the screen, count newlines until an escape sequence
			 * appears.
			 */
			if ((edp->state == SUN_EMUL_STATE_NORMAL || kernel) &&
			    ROWS_LEFT == 0 && edp->scrolldist != 0)
				lines = wsemul_sun_jump_scroll(edp, data,
				    count, kernel);
			else
				lines = 0;
			break;
		default:
			/*
			 * If we are recovering a non-scrolling failure,
			 * do not try to scroll yet.
			 */
			lines = 0;
			break;
		}

		if (lines > 1) {
			wsemul_resume_abort(&edp->abortstate);
			rc = wsemul_sun_scrollup(edp, lines);
			if (rc != 0) {
				wsemul_abort_jump_scroll(&edp->abortstate,
				    lines);
				return processed;
			}
			wsemul_reset_abortstate(&edp->abortstate);
			edp->crow--;
		}
#endif

		wsemul_resume_abort(&edp->abortstate);

		prev_count = count;
		if (wsemul_getchar(&data, &count, instate,
#ifdef HAVE_UTF8_SUPPORT
		    (edp->state == SUN_EMUL_STATE_NORMAL && !kernel) ?
		      edp->flags & SUN_EMUL_FLAGS_UTF8 : 0
#else
		    0
#endif
		    ) != 0)
			break;

		if (instate->inchar < ' ') {
			rc = wsemul_sun_output_lowchars(edp, instate, kernel);
			if (rc != 0)
				break;
			processed += prev_count - count;
			continue;
		}

		if (kernel) {
			rc = wsemul_sun_output_normal(edp, instate, 1);
			if (rc != 0)
				break;
			processed += prev_count - count;
			continue;
		}

		switch (edp->state) {
		case SUN_EMUL_STATE_NORMAL:
			rc = wsemul_sun_output_normal(edp, instate, 0);
			break;
		case SUN_EMUL_STATE_HAVEESC:
			rc = wsemul_sun_output_haveesc(edp, instate);
			break;
		case SUN_EMUL_STATE_CONTROL:
			rc = wsemul_sun_output_control(edp, instate);
			break;
#ifdef HAVE_UTF8_SUPPORT
		case SUN_EMUL_STATE_PERCENT:
			rc = wsemul_sun_output_percent(edp, instate);
			break;
#endif
		default:
#ifdef DIAGNOSTIC
			panic("wsemul_sun: invalid state %d", edp->state);
#else
			/* try to recover, if things get screwed up... */
			edp->state = SUN_EMUL_STATE_NORMAL;
			rc = wsemul_sun_output_normal(edp, instate, 0);
#endif
			break;
		}
		if (rc != 0)
			break;
		processed += prev_count - count;
	}

	if (rc != 0)
		wsemul_abort_other(&edp->abortstate);
	else {
		/* put cursor image back */
		rc = (*edp->emulops->cursor)
		    (edp->emulcookie, 1, edp->crow, edp->ccol);
		if (rc != 0) {
			/*
			 * Pretend the last byte hasn't been processed, while
			 * remembering that only the cursor operation really
			 * needs to be done.
			 */
			wsemul_abort_cursor(&edp->abortstate);
			processed--;
		}
	}

	if (rc == 0)
		wsemul_reset_abortstate(&edp->abortstate);

	return processed;
}

#ifdef HAVE_JUMP_SCROLL
int
wsemul_sun_jump_scroll(struct wsemul_sun_emuldata *edp, const u_char *data,
    u_int count, int kernel)
{
	u_int pos, lines;
	struct wsemul_inputstate tmpstate;

	lines = 0;
	pos = edp->ccol;
	tmpstate = kernel ? edp->kstate : edp->instate;	/* structure copy */

	while (wsemul_getchar(&data, &count, &tmpstate,
#ifdef HAVE_UTF8_SUPPORT
	    kernel ? 0 : edp->flags & SUN_EMUL_FLAGS_UTF8
#else
	    0
#endif
	    ) == 0) {
		if (tmpstate.inchar == ASCII_FF ||
		    tmpstate.inchar == ASCII_VT ||
		    tmpstate.inchar == ASCII_ESC)
			break;

		switch (tmpstate.inchar) {
		case ASCII_BS:
			if (pos > 0)
				pos--;
			break;
		case ASCII_CR:
			pos = 0;
			break;
		case ASCII_HT:
			pos = (pos + 7) & ~7;
			if (pos >= edp->ncols)
				pos = edp->ncols - 1;
			break;
		case ASCII_LF:
			break;
		default:
			if (++pos >= edp->ncols) {
				pos = 0;
				tmpstate.inchar = ASCII_LF;
			}
			break;
		}
		if (tmpstate.inchar == ASCII_LF) {
			if (++lines >= edp->nrows - 1)
				break;
		}
	}

	return lines;
}
#endif

/*
 * Get an attribute from the graphics driver.
 * Try to find replacements if the desired appearance is not supported.
 */
int
wsemul_sun_selectattribute(struct wsemul_sun_emuldata *edp, int flags,
    int fgcol, int bgcol, uint32_t *attr, uint32_t *bkgdattr)
{
	int error;

	/*
	 * Rasops will force white on black as normal output colors, unless
	 * WSATTR_WSCOLORS is specified. Since Sun console is black on white,
	 * always use WSATTR_WSCOLORS and our colors, as we know better.
	 */
	if (!(edp->scrcapabilities & WSSCREEN_WSCOLORS)) {
		flags &= ~WSATTR_WSCOLORS;
	} else {
		flags |= WSATTR_WSCOLORS;
	}

	error = (*edp->emulops->pack_attr)(edp->emulcookie, fgcol, bgcol,
					    flags & WSATTR_WSCOLORS, bkgdattr);
	if (error)
		return (error);

	if ((flags & WSATTR_HILIT) &&
	    !(edp->scrcapabilities & WSSCREEN_HILIT)) {
		flags &= ~WSATTR_HILIT;
		if (edp->scrcapabilities & WSSCREEN_WSCOLORS) {
			fgcol = WSCOL_RED;
			flags |= WSATTR_WSCOLORS;
		}
	}
	if ((flags & WSATTR_UNDERLINE) &&
	    !(edp->scrcapabilities & WSSCREEN_UNDERLINE)) {
		flags &= ~WSATTR_UNDERLINE;
		if (edp->scrcapabilities & WSSCREEN_WSCOLORS) {
			fgcol = WSCOL_CYAN;
			flags &= ~WSATTR_UNDERLINE;
			flags |= WSATTR_WSCOLORS;
		}
	}
	if ((flags & WSATTR_BLINK) &&
	    !(edp->scrcapabilities & WSSCREEN_BLINK)) {
		flags &= ~WSATTR_BLINK;
	}
	if ((flags & WSATTR_REVERSE) &&
	    !(edp->scrcapabilities & WSSCREEN_REVERSE)) {
		flags &= ~WSATTR_REVERSE;
		if (edp->scrcapabilities & WSSCREEN_WSCOLORS) {
			int help;
			help = bgcol;
			bgcol = fgcol;
			fgcol = help;
			flags |= WSATTR_WSCOLORS;
		}
	}
	error = (*edp->emulops->pack_attr)(edp->emulcookie, fgcol, bgcol,
					    flags, attr);
	if (error)
		return (error);

	return (0);
}

static const u_char *sun_fkeys[] = {
	"\033[224z",	/* F1 */
	"\033[225z",
	"\033[226z",
	"\033[227z",
	"\033[228z",
	"\033[229z",
	"\033[230z",
	"\033[231z",
	"\033[232z",
	"\033[233z",
	"\033[234z",
	"\033[235z",	/* F12 */
};

static const u_char *sun_lkeys[] = {
	"\033[207z",	/* KS_Help */
	NULL,		/* KS_Execute */
	"\033[200z",	/* KS_Find */
	NULL,		/* KS_Select */
	"\033[193z",	/* KS_Again */
	"\033[194z",	/* KS_Props */
	"\033[195z",	/* KS_Undo */
	"\033[196z",	/* KS_Front */
	"\033[197z",	/* KS_Copy */
	"\033[198z",	/* KS_Open */
	"\033[199z",	/* KS_Paste */
	"\033[201z",	/* KS_Cut */
};

int
wsemul_sun_translate(void *cookie, kbd_t layout, keysym_t in,
    const u_char **out)
{
	struct wsemul_sun_emuldata *edp = cookie;

	if (KS_GROUP(in) == KS_GROUP_Ascii) {
		*out = edp->translatebuf;
		return (wsemul_utf8_translate(KS_VALUE(in), layout,
		    edp->translatebuf, edp->flags & SUN_EMUL_FLAGS_UTF8));
	}

	if (KS_GROUP(in) == KS_GROUP_Keypad && (in & 0x80) == 0) {
		edp->translatebuf[0] = in & 0xff; /* turn into ASCII */
		*out = edp->translatebuf;
		return (1);
	}

	if (in >= KS_f1 && in <= KS_f12) {
		*out = sun_fkeys[in - KS_f1];
		return (6);
	}
	if (in >= KS_F1 && in <= KS_F12) {
		*out = sun_fkeys[in - KS_F1];
		return (6);
	}
	if (in >= KS_KP_F1 && in <= KS_KP_F4) {
		*out = sun_fkeys[in - KS_KP_F1];
		return (6);
	}
	if (in >= KS_Help && in <= KS_Cut && sun_lkeys[in - KS_Help] != NULL) {
		*out = sun_lkeys[in - KS_Help];
		return (6);
	}

	switch (in) {
	case KS_Home:
	case KS_KP_Home:
	case KS_KP_Begin:
		*out = "\033[214z";
		return (6);
	case KS_End:
	case KS_KP_End:
		*out = "\033[220z";
		return (6);
	case KS_Insert:
	case KS_KP_Insert:
		*out = "\033[247z";
		return (6);
	case KS_Prior:
	case KS_KP_Prior:
		*out = "\033[216z";
		return (6);
	case KS_Next:
	case KS_KP_Next:
		*out = "\033[222z";
		return (6);
	case KS_Up:
	case KS_KP_Up:
		*out = "\033[A";
		return (3);
	case KS_Down:
	case KS_KP_Down:
		*out = "\033[B";
		return (3);
	case KS_Left:
	case KS_KP_Left:
		*out = "\033[D";
		return (3);
	case KS_Right:
	case KS_KP_Right:
		*out = "\033[C";
		return (3);
	case KS_KP_Delete:
		*out = "\177";
		return (1);
	}
	return (0);
}

void
wsemul_sun_detach(void *cookie, u_int *crowp, u_int *ccolp)
{
	struct wsemul_sun_emuldata *edp = cookie;

	*crowp = edp->crow;
	*ccolp = edp->ccol;
	if (edp != &wsemul_sun_console_emuldata)
		free(edp, M_DEVBUF, sizeof *edp);
}

void
wsemul_sun_resetop(void *cookie, enum wsemul_resetops op)
{
	struct wsemul_sun_emuldata *edp = cookie;

	switch (op) {
	case WSEMUL_RESET:
		wsemul_sun_reset(edp);
		break;
	case WSEMUL_CLEARSCREEN:
		(*edp->emulops->eraserows)(edp->emulcookie, 0, edp->nrows,
		    edp->bkgdattr);
		edp->ccol = edp->crow = 0;
		(*edp->emulops->cursor)(edp->emulcookie, 1, 0, 0);
		break;
	case WSEMUL_CLEARCURSOR:
		(*edp->emulops->cursor)(edp->emulcookie, 0, edp->crow,
		    edp->ccol);
		break;
	default:
		break;
	}
}

int
wsemul_sun_scrollup(struct wsemul_sun_emuldata *edp, u_int lines)
{
	int rc;

	/*
	 * if we're in wrap-around mode, go to the first
	 * line and clear it.
	 */
	if (lines == 0) {
		WSEMULOP(rc, edp, &edp->abortstate, eraserows,
		    (edp->emulcookie, 0, 1, edp->bkgdattr));
		if (rc != 0)
			return rc;

		edp->crow = 0;
		return 0;
	}

	/*
	 * If the scrolling distance is equal to the screen height
	 * (usually 34), clear the screen; otherwise, scroll by the
	 * scrolling distance.
	 */
	if (lines < edp->nrows) {
		WSEMULOP(rc, edp, &edp->abortstate, copyrows,
		    (edp->emulcookie, lines, 0, edp->nrows - lines));
		if (rc != 0)
			return rc;
	}
	WSEMULOP(rc, edp, &edp->abortstate, eraserows,
	    (edp->emulcookie, edp->nrows - lines, lines, edp->bkgdattr));
	if (rc != 0)
		return rc;

	edp->crow -= lines - 1;

	return 0;
}
