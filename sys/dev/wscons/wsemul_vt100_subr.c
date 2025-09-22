/* $OpenBSD: wsemul_vt100_subr.c,v 1.32 2024/11/05 15:54:12 miod Exp $ */
/* $NetBSD: wsemul_vt100_subr.c,v 1.7 2000/04/28 21:56:16 mycroft Exp $ */

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymvar.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/wsemul_vt100var.h>

int	vt100_selectattribute(struct wsemul_vt100_emuldata *, int, int, int,
	    uint32_t *, uint32_t *);
int	vt100_ansimode(struct wsemul_vt100_emuldata *, int, int);
int	vt100_decmode(struct wsemul_vt100_emuldata *, int, int);
#define VTMODE_SET 33
#define VTMODE_RESET 44
#define VTMODE_REPORT 55

/*
 * scroll up within scrolling region
 */
int
wsemul_vt100_scrollup(struct wsemul_vt100_emuldata *edp, int n)
{
	int help;
	int rc;

	if (n > edp->scrreg_nrows)
		n = edp->scrreg_nrows;

	help = edp->scrreg_nrows - n;
	if (help > 0) {
		WSEMULOP(rc, edp, &edp->abortstate, copyrows,
		    (edp->emulcookie, edp->scrreg_startrow + n,
		     edp->scrreg_startrow, help));
		if (rc != 0)
			return rc;
	}
	WSEMULOP(rc, edp, &edp->abortstate, eraserows,
	    (edp->emulcookie, edp->scrreg_startrow + help, n, edp->bkgdattr));
	if (rc != 0)
		return rc;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
	if (edp->dblwid) {
		if (help > 0)
			memmove(&edp->dblwid[edp->scrreg_startrow],
			    &edp->dblwid[edp->scrreg_startrow + n], help);
		memset(&edp->dblwid[edp->scrreg_startrow + help], 0, n);
	}
	CHECK_DW;
#endif

	return 0;
}

/*
 * scroll down within scrolling region
 */
int
wsemul_vt100_scrolldown(struct wsemul_vt100_emuldata *edp, int n)
{
	int help;
	int rc;

	if (n > edp->scrreg_nrows)
		n = edp->scrreg_nrows;

	help = edp->scrreg_nrows - n;
	if (help > 0) {
		WSEMULOP(rc, edp, &edp->abortstate, copyrows,
		    (edp->emulcookie, edp->scrreg_startrow,
		     edp->scrreg_startrow + n, help));
		if (rc != 0)
			return rc;
	}
	WSEMULOP(rc, edp, &edp->abortstate, eraserows,
	    (edp->emulcookie, edp->scrreg_startrow, n, edp->bkgdattr));
	if (rc != 0)
		return rc;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
	if (edp->dblwid) {
		if (help > 0)
			memmove(&edp->dblwid[edp->scrreg_startrow + n],
			    &edp->dblwid[edp->scrreg_startrow], help);
		memset(&edp->dblwid[edp->scrreg_startrow], 0, n);
	}
	CHECK_DW;
#endif

	return 0;
}

/*
 * erase in display
 */
int
wsemul_vt100_ed(struct wsemul_vt100_emuldata *edp, int arg)
{
	int n;
	int rc;

	switch (arg) {
	case 0: /* cursor to end */
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(edp->ccol, COLS_LEFT + 1, edp->bkgdattr));
		if (rc != 0)
			break;
		n = edp->nrows - edp->crow - 1;
		if (n > 0) {
			WSEMULOP(rc, edp, &edp->abortstate, eraserows,
			    (edp->emulcookie, edp->crow + 1, n, edp->bkgdattr));
			if (rc != 0)
				break;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
			if (edp->dblwid)
				memset(&edp->dblwid[edp->crow + 1], 0, n);
#endif
		}
		break;
	case 1: /* beginning to cursor */
		if (edp->crow > 0) {
			WSEMULOP(rc, edp, &edp->abortstate, eraserows,
			    (edp->emulcookie, 0, edp->crow, edp->bkgdattr));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(0, edp->ccol + 1, edp->bkgdattr));
		if (rc != 0)
			break;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
		if (edp->dblwid) {
			if (edp->crow > 0)
				memset(&edp->dblwid[0], 0, edp->crow);
		}
#endif
		break;
	case 2: /* complete display */
		WSEMULOP(rc, edp, &edp->abortstate, eraserows,
		    (edp->emulcookie, 0, edp->nrows, edp->bkgdattr));
		if (rc != 0)
			break;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
		if (edp->dblwid)
			memset(&edp->dblwid[0], 0, edp->nrows);
#endif
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("ed(%d) unknown\n", arg);
#endif
		rc = 0;
		break;
	}
	if (rc != 0)
		return rc;

	CHECK_DW;

	return 0;
}

/*
 * erase in line
 */
int
wsemul_vt100_el(struct wsemul_vt100_emuldata *edp, int arg)
{
	int rc;

	switch (arg) {
	case 0: /* cursor to end */
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(edp->ccol, COLS_LEFT + 1, edp->bkgdattr));
		break;
	case 1: /* beginning to cursor */
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(0, edp->ccol + 1, edp->bkgdattr));
		break;
	case 2: /* complete line */
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    (edp->emulcookie, edp->crow, 0, edp->ncols, edp->bkgdattr));
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("el(%d) unknown\n", arg);
#endif
		rc = 0;
		break;
	}

	return rc;
}

/*
 * handle commands after CSI (ESC[)
 */
int
wsemul_vt100_handle_csi(struct wsemul_vt100_emuldata *edp,
    struct wsemul_inputstate *instate, int kernel)
{
	int n, m, help, flags, fgcol, bgcol;
	uint32_t attr, bkgdattr;
	u_char c;
	int rc = 0;

	if (instate->inchar >= 0x100)
		c = 0x00;	/* cause the switch below to end in default: */
	else
		c = (u_char)instate->inchar;

#define A3(a, b, c) (((a) << 16) | ((b) << 8) | (c))
	switch (A3(edp->modif1, edp->modif2, c)) {
	case A3('>', '\0', 'c'): /* DA secondary */
		wsdisplay_emulinput(edp->cbcookie, WSEMUL_VT_ID2,
		    sizeof(WSEMUL_VT_ID2) - 1);
		break;

	case A3('\0', '\0', 'J'): /* ED selective erase in display */
	case A3('?', '\0', 'J'): /* DECSED selective erase in display */
		rc = wsemul_vt100_ed(edp, ARG(0));
		break;
	case A3('\0', '\0', 'K'): /* EL selective erase in line */
	case A3('?', '\0', 'K'): /* DECSEL selective erase in line */
		rc = wsemul_vt100_el(edp, ARG(0));
		break;
	case A3('\0', '\0', 'h'): /* SM */
		for (n = 0; n < edp->nargs; n++)
			vt100_ansimode(edp, ARG(n), VTMODE_SET);
		break;
	case A3('?', '\0', 'h'): /* DECSM */
		for (n = 0; n < edp->nargs; n++) {
			rc = vt100_decmode(edp, ARG(n), VTMODE_SET);
			if (rc != 0)
				break;
		}
		break;
	case A3('\0', '\0', 'l'): /* RM */
		for (n = 0; n < edp->nargs; n++)
			vt100_ansimode(edp, ARG(n), VTMODE_RESET);
		break;
	case A3('?', '\0', 'l'): /* DECRM */
		for (n = 0; n < edp->nargs; n++) {
			rc = vt100_decmode(edp, ARG(n), VTMODE_RESET);
			if (rc != 0)
				break;
		}
		break;
	case A3('\0', '$', 'p'): /* DECRQM request mode ANSI */
		vt100_ansimode(edp, ARG(0), VTMODE_REPORT);
		break;
	case A3('?', '$', 'p'): /* DECRQM request mode DEC */
		rc = vt100_decmode(edp, ARG(0), VTMODE_REPORT);
		break;
	case A3('\0', '\0', 'i'): /* MC printer controller mode */
	case A3('?', '\0', 'i'): /* MC printer controller mode */
		switch (ARG(0)) {
		case 0: /* print screen */
		case 1: /* print cursor line */
		case 4: /* off */
		case 5: /* on */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI%di ignored\n", ARG(0));
#endif
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%di unknown\n", ARG(0));
#endif
			break;
		}
		break;

#define A2(a, b) (((a) << 8) | (b))
	case A2('!', 'p'): /* DECSTR soft reset VT300 only */
		wsemul_vt100_reset(edp);
		break;

	case A2('"', 'p'): /* DECSCL */
		switch (ARG(0)) {
		case 61: /* VT100 mode (no further arguments!) */
			break;
		case 62:
		case 63: /* VT300 mode */
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d\"p unknown\n", ARG(0));
#endif
			break;
		}
		switch (ARG(1)) {
		case 0:
		case 2: /* 8-bit controls */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI%d;%d\"p ignored\n", ARG(0), ARG(1));
#endif
			break;
		case 1: /* 7-bit controls */
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d;%d\"p unknown\n", ARG(0), ARG(1));
#endif
			break;
		}
		break;
	case A2('"', 'q'): /* DECSCA select character attribute VT300 */
		switch (ARG(0)) {
		case 0:
		case 1: /* erasable */
			break;
		case 2: /* not erasable */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI2\"q ignored\n");
#endif
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d\"q unknown\n", ARG(0));
#endif
			break;
		}
		break;

	case A2('$', 'u'): /* DECRQTSR request terminal status report */
		switch (ARG(0)) {
		case 0: /* ignored */
			break;
		case 1: /* terminal state report */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI1$u ignored\n");
#endif
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d$u unknown\n", ARG(0));
#endif
			break;
		}
		break;
	case A2('$', 'w'): /* DECRQPSR request presentation status report
				(VT300 only) */
		switch (ARG(0)) {
		case 0: /* error */
			break;
		case 1: /* cursor information report */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI1$w ignored\n");
#endif
			break;
		case 2: /* tab stop report */
		    {
			int i, n, ps = 0;
			char buf[20];

			wsdisplay_emulinput(edp->cbcookie, "\033P2$u", 5);
			if (edp->tabs != NULL)
			    for (i = 0; i < edp->ncols; i++)
				if (edp->tabs[i]) {
					n = snprintf(buf, sizeof buf, "%s%d",
					    (ps ? "/" : ""), i + 1);
					if (n == -1)
						n = 0;
					else if (n >= sizeof buf)
						n = sizeof buf - 1;
					wsdisplay_emulinput(edp->cbcookie,
					    buf, n);
					ps = 1;
				}
			wsdisplay_emulinput(edp->cbcookie, "\033\\", 2);
		    }
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d$w unknown\n", ARG(0));
#endif
			break;
		}
		break;
	/* gratuitous { for brace matching with the next line */
	case A2('$', '}'): /* DECSASD select active status display */
		switch (ARG(0)) {
		case 0: /* main display */
		case 1: /* status line */
#ifdef VT100_PRINTNOTIMPL	/* { */
			printf("CSI%d$} ignored\n", ARG(0));
#endif
			break;
		default:
#ifdef VT100_PRINTUNKNOWN	/* { */
			printf("CSI%d$} unknown\n", ARG(0));
#endif
			break;
		}
		break;
	case A2('$', '~'): /* DECSSDD select status line type */
		switch (ARG(0)) {
		case 0: /* none */
		case 1: /* indicator */
		case 2: /* host-writable */
#ifdef VT100_PRINTNOTIMPL
			printf("CSI%d$~ ignored\n", ARG(0));
#endif
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%d$~ unknown\n", ARG(0));
#endif
			break;
		}
		break;

	case A2('&', 'u'): /* DECRQUPSS request user preferred
				  supplemental set */
		wsdisplay_emulinput(edp->cbcookie, "\033P0!u%5\033\\", 9);
		break;

	case '@': /* ICH insert character VT300 only */
		n = min(DEF1_ARG(0), COLS_LEFT + 1);
		help = NCOLS - (edp->ccol + n);
		if (help > 0) {
			WSEMULOP(rc, edp, &edp->abortstate, copycols,
			    COPYCOLS(edp->ccol, edp->ccol + n, help));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(edp->ccol, n, edp->bkgdattr));
		break;
	case 'A': /* CUU */
		n = ROWS_ABOVE;
		if (n > 0)
			edp->crow -= min(DEF1_ARG(0), n);
		CHECK_DW;
		break;
	case 'B': /* CUD */
		n = ROWS_BELOW;
		if (n > 0)
			edp->crow += min(DEF1_ARG(0), n);
		CHECK_DW;
		break;
	case 'C': /* CUF */
		edp->ccol += min(DEF1_ARG(0), COLS_LEFT);
		break;
	case 'D': /* CUB */
		edp->ccol -= min(DEF1_ARG(0), edp->ccol);
		edp->flags &= ~VTFL_LASTCHAR;
		break;
	case 'G': /* CHA */
	case '`': /* HPA */
		edp->ccol = min(DEF1_ARG(0), edp->ncols) - 1;
		break;
	case 'H': /* CUP */
	case 'f': /* HVP */
		if (edp->flags & VTFL_DECOM)
			edp->crow = edp->scrreg_startrow +
			    min(DEF1_ARG(0), edp->scrreg_nrows) - 1;
		else
			edp->crow = min(DEF1_ARG(0), edp->nrows) - 1;
		CHECK_DW;
		edp->ccol = min(DEF1_ARG(1), NCOLS) - 1;
		edp->flags &= ~VTFL_LASTCHAR;
		break;
	case 'L': /* IL insert line */
	case 'M': /* DL delete line */
		if (edp->crow >= edp->scrreg_startrow &&
		    edp->crow < edp->scrreg_startrow + edp->scrreg_nrows) {
			int savscrstartrow, savscrnrows;

			n = min(DEF1_ARG(0), ROWS_BELOW + 1);
			savscrstartrow = edp->scrreg_startrow;
			savscrnrows = edp->scrreg_nrows;
			edp->scrreg_nrows -= ROWS_ABOVE;
			edp->scrreg_startrow = edp->crow;
			if (c == 'L')
				rc = wsemul_vt100_scrolldown(edp, n);
			else
				rc = wsemul_vt100_scrollup(edp, n);
			edp->scrreg_startrow = savscrstartrow;
			edp->scrreg_nrows = savscrnrows;
		} /* else not within scrolling region, ignore the sequence */
		break;
	case 'P': /* DCH delete character */
		n = min(DEF1_ARG(0), COLS_LEFT + 1);
		help = NCOLS - (edp->ccol + n);
		if (help > 0) {
			WSEMULOP(rc, edp, &edp->abortstate, copycols,
			    COPYCOLS(edp->ccol + n, edp->ccol, help));
			if (rc != 0)
				break;
		}
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(NCOLS - n, n, edp->bkgdattr));
		break;
	case 'S': /* SU scroll up */
		wsemul_vt100_scrollup(edp, DEF1_ARG(0));
		break;
	case 'T': /* SD scroll down */
		wsemul_vt100_scrolldown(edp, DEF1_ARG(0));
		break;
	case 'X': /* ECH erase character */
		n = min(DEF1_ARG(0), COLS_LEFT + 1);
		WSEMULOP(rc, edp, &edp->abortstate, erasecols,
		    ERASECOLS(edp->ccol, n, edp->bkgdattr));
		break;
	case 'Z': /* CBT */
		if (edp->ccol == 0)
			break;
		for (m = 0; m < DEF1_ARG(0); m++) {
			if (edp->tabs) {
				for (n = edp->ccol - 1; n > 0; n--) {
					if (edp->tabs[n])
						break;
				}
			} else
				n = (edp->ccol - 1) & ~7;
			edp->ccol = n;
			if (n == 0)
				break;
		}
		break;
	case 'b': /* REP */
		/*
		 * We arbitrarily limit the repeat count to 65535 to avoid
		 * uninterruptible flooding of the console. This matches
		 * current xterm behaviour.
		 */
		n = min(DEF1_ARG(0), 65535);
		instate->inchar = instate->last_output;
		rc = wsemul_vt100_output_normal(edp, instate, kernel, n);
		break;
	case 'c': /* DA primary */
		if (ARG(0) == 0)
			wsdisplay_emulinput(edp->cbcookie, WSEMUL_VT_ID1,
			    sizeof(WSEMUL_VT_ID1) - 1);
		break;
	case 'd': /* VPA */
		edp->crow = min(DEF1_ARG(0), edp->nrows) - 1;
		break;
	case 'g': /* TBC */
		if (edp->tabs != NULL)
			switch (ARG(0)) {
			case 0:
				edp->tabs[edp->ccol] = 0;
				break;
			case 3:
				memset(edp->tabs, 0, edp->ncols);
				break;
			default:
#ifdef VT100_PRINTUNKNOWN
				printf("CSI%dg unknown\n", ARG(0));
#endif
				break;
			}
		break;
	case 'm': /* SGR select graphic rendition */
		flags = edp->attrflags;
		fgcol = edp->fgcol;
		bgcol = edp->bgcol;
		for (n = 0; n < edp->nargs; n++) {
			switch (ARG(n)) {
			case 0: /* reset */
				if (n == edp->nargs - 1) {
					edp->bkgdattr = edp->curattr = edp->defattr;
					edp->attrflags = 0;
					edp->fgcol = WSCOL_WHITE;
					edp->bgcol = WSCOL_BLACK;
					return 0;
				}
				flags = 0;
				fgcol = WSCOL_WHITE;
				bgcol = WSCOL_BLACK;
				break;
			case 1: /* bold */
				flags |= WSATTR_HILIT;
				break;
			case 4: /* underline */
				flags |= WSATTR_UNDERLINE;
				break;
			case 5: /* blink */
				flags |= WSATTR_BLINK;
				break;
			case 7: /* reverse */
				flags |= WSATTR_REVERSE;
				break;
			case 22: /* ~bold VT300 only */
				flags &= ~WSATTR_HILIT;
				break;
			case 24: /* ~underline VT300 only */
				flags &= ~WSATTR_UNDERLINE;
				break;
			case 25: /* ~blink VT300 only */
				flags &= ~WSATTR_BLINK;
				break;
			case 27: /* ~reverse VT300 only */
				flags &= ~WSATTR_REVERSE;
				break;
			case 30: case 31: case 32: case 33:
			case 34: case 35: case 36: case 37:
				/* fg color */
				flags |= WSATTR_WSCOLORS;
				fgcol = ARG(n) - 30;
				break;
			case 39:
				/* reset fg color */
				fgcol = WSCOL_WHITE;
				if (bgcol == WSCOL_BLACK)
					flags &= ~WSATTR_WSCOLORS;
				break;
			case 40: case 41: case 42: case 43:
			case 44: case 45: case 46: case 47:
				/* bg color */
				flags |= WSATTR_WSCOLORS;
				bgcol = ARG(n) - 40;
				break;
			case 49:
				/* reset bg color */
				bgcol = WSCOL_BLACK;
				if (fgcol == WSCOL_WHITE)
					flags &= ~WSATTR_WSCOLORS;
				break;
			case 90: case 91: case 92: case 93:
			case 94: case 95: case 96: case 97:
				/* bright foreground color */
				flags |= WSATTR_WSCOLORS;
				fgcol = ARG(n) - 82;
				break;
			case 100: case 101: case 102: case 103:
			case 104: case 105: case 106: case 107:
				/* bright background color */
				flags |= WSATTR_WSCOLORS;
				bgcol = ARG(n) - 92;
				break;
			default:
#ifdef VT100_PRINTUNKNOWN
				printf("CSI%dm unknown\n", ARG(n));
#endif
				break;
			}
		}
		if (vt100_selectattribute(edp, flags, fgcol, bgcol, &attr,
		    &bkgdattr)) {
#ifdef VT100_DEBUG
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
	case 'n': /* reports */
		switch (ARG(0)) {
		case 5: /* DSR operating status */
			/* 0 = OK, 3 = malfunction */
			wsdisplay_emulinput(edp->cbcookie, "\033[0n", 4);
			break;
		case 6: /* DSR cursor position report */
		    {
			char buf[20];
			int row;
			if (edp->flags & VTFL_DECOM) {
				row = ROWS_ABOVE;
				if (row < 0)
					row = 0;
			} else
				row = edp->crow;
			n = snprintf(buf, sizeof buf, "\033[%d;%dR",
				    row + 1, edp->ccol + 1);
			if (n == -1)
				n = 0;
			else if (n >= sizeof buf)
				n = sizeof buf - 1;
			wsdisplay_emulinput(edp->cbcookie, buf, n);
		    }
			break;
		case 15: /* DSR printer status */
			/* 13 = no printer, 10 = ready, 11 = not ready */
			wsdisplay_emulinput(edp->cbcookie, "\033[?13n", 6);
			break;
		case 25: /* UDK status - VT300 only */
			/* 20 = locked, 21 = unlocked */
			wsdisplay_emulinput(edp->cbcookie, "\033[?21n", 6);
			break;
		case 26: /* keyboard dialect */
			/* 1 = north american , 7 = german */
			wsdisplay_emulinput(edp->cbcookie, "\033[?27;1n", 8);
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%dn unknown\n", ARG(0));
#endif
			break;
		}
		break;
	case 'r': /* DECSTBM set top/bottom margins */
		help = min(DEF1_ARG(0), edp->nrows) - 1;
		n = min(DEFx_ARG(1, edp->nrows), edp->nrows) - help;
		if (n < 2) {
			/* minimal scrolling region has 2 lines */
			return 0;
		} else {
			edp->scrreg_startrow = help;
			edp->scrreg_nrows = n;
		}
		edp->crow = ((edp->flags & VTFL_DECOM) ?
			     edp->scrreg_startrow : 0);
		edp->ccol = 0;
		break;
	case 's':
		edp->flags |= VTFL_SAVEDCURS;
		edp->savedcursor_row = edp->crow;
		edp->savedcursor_col = edp->ccol;
		break;
	case 'u':
		if (edp->flags & VTFL_SAVEDCURS) {
			edp->crow = edp->savedcursor_row;
			edp->ccol = edp->savedcursor_col;
		}
		break;
	case 'y':
		switch (ARG(0)) {
		case 4: /* DECTST invoke confidence test */
			/* ignore */
			break;
		default:
#ifdef VT100_PRINTUNKNOWN
			printf("CSI%dy unknown\n", ARG(0));
#endif
			break;
		}
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("CSI %x (%d, %d) unknown\n",
		    instate->inchar, ARG(0), ARG(1));
#endif
		break;
	}

	return rc;
}

/*
 * get an attribute from the graphics driver,
 * try to find replacements if the desired appearance
 * is not supported
 */
int
vt100_selectattribute(struct wsemul_vt100_emuldata *edp, int flags, int fgcol,
    int bgcol, uint32_t *attr, uint32_t *bkgdattr)
{
	int error;

	if ((flags & WSATTR_WSCOLORS) &&
	    !(edp->scrcapabilities & WSSCREEN_WSCOLORS)) {
		flags &= ~WSATTR_WSCOLORS;
#ifdef VT100_DEBUG
		printf("colors ignored (impossible)\n");
#endif
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
		} else {
#ifdef VT100_DEBUG
			printf("bold ignored (impossible)\n");
#endif
		}
	}
	if ((flags & WSATTR_UNDERLINE) &&
	    !(edp->scrcapabilities & WSSCREEN_UNDERLINE)) {
		flags &= ~WSATTR_UNDERLINE;
		if (edp->scrcapabilities & WSSCREEN_WSCOLORS) {
			fgcol = WSCOL_CYAN;
			flags &= ~WSATTR_UNDERLINE;
			flags |= WSATTR_WSCOLORS;
		} else {
#ifdef VT100_DEBUG
			printf("underline ignored (impossible)\n");
#endif
		}
	}
	if ((flags & WSATTR_BLINK) &&
	    !(edp->scrcapabilities & WSSCREEN_BLINK)) {
		flags &= ~WSATTR_BLINK;
#ifdef VT100_DEBUG
		printf("blink ignored (impossible)\n");
#endif
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
		} else {
#ifdef VT100_DEBUG
			printf("reverse ignored (impossible)\n");
#endif
		}
	}
	error = (*edp->emulops->pack_attr)(edp->emulcookie, fgcol, bgcol,
					    flags, attr);
	if (error)
		return (error);

	return (0);
}

/*
 * handle device control sequences if the main state machine
 * told so by setting edp->dcstype to a nonzero value
 */
void
wsemul_vt100_handle_dcs(struct wsemul_vt100_emuldata *edp)
{
	int i, pos;

	switch (edp->dcstype) {
	case 0: /* not handled */
		return;
	case DCSTYPE_TABRESTORE:
		if (edp->tabs != NULL) {
			memset(edp->tabs, 0, edp->ncols);
			pos = 0;
			if (edp->dcsarg == NULL)
				goto out;
			for (i = 0; i < edp->dcspos; i++) {
				char c = edp->dcsarg[i];
				switch (c) {
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
				case '8': case '9':
					pos = pos * 10 + (c - '0');
					if (pos > edp->ncols)
						goto out;
					break;
				case '/':
					if (pos > 0)
						edp->tabs[pos - 1] = 1;
					pos = 0;
					break;
				default:
#ifdef VT100_PRINTUNKNOWN
					printf("unknown char %c in DCS\n", c);
#endif
					break;
				}
			}
			if (pos > 0)
				edp->tabs[pos - 1] = 1;
		}
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("wsemul_vt100_handle_dcs: bad type %d\n", edp->dcstype);
#endif
		break;
	}
out:
	edp->dcstype = 0;
}

int
vt100_ansimode(struct wsemul_vt100_emuldata *edp, int nr, int op)
{
	int res = 0; /* default: unknown */

	switch (nr) {
	case 2: /* KAM keyboard locked/unlocked */
		break;
	case 3: /* CRM control representation */
		break;
	case 4: /* IRM insert/replace characters */
		if (op == VTMODE_SET)
			edp->flags |= VTFL_INSERTMODE;
		else if (op == VTMODE_RESET)
			edp->flags &= ~VTFL_INSERTMODE;
		res = ((edp->flags & VTFL_INSERTMODE) ? 1 : 2);
		break;
	case 10: /* HEM horizontal editing (permanently reset) */
		res = 4;
		break;
	case 12: /* SRM local echo off/on */
		res = 4; /* permanently reset ??? */
		break;
	case 20: /* LNM newline = newline/linefeed */
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("ANSI mode %d unknown\n", nr);
#endif
		break;
	}
	return (res);
}

int
vt100_decmode(struct wsemul_vt100_emuldata *edp, int nr, int op)
{
#if 0	/* res unused... return it by reference if ever necessary */
	int res = 0; /* default: unknown */
#endif
	int flags = edp->flags;
	int rc = 0;

	switch (nr) {
	case 1: /* DECCKM application/nomal cursor keys */
		if (op == VTMODE_SET)
			flags |= VTFL_APPLCURSOR;
		else if (op == VTMODE_RESET)
			flags &= ~VTFL_APPLCURSOR;
#if 0
		res = ((flags & VTFL_APPLCURSOR) ? 1 : 2);
#endif
		break;
	case 2: /* DECANM ANSI vt100/vt52 */
#if 0
		res = 3; /* permanently set ??? */
#endif
		break;
	case 3: /* DECCOLM 132/80 cols */
	case 4: /* DECSCLM smooth/jump scroll */
	case 5: /* DECSCNM light/dark background */
#if 0
		res = 4; /* all permanently reset ??? */
#endif
		break;
	case 6: /* DECOM move within/outside margins */
		if (op == VTMODE_SET)
			flags |= VTFL_DECOM;
		else if (op == VTMODE_RESET)
			flags &= ~VTFL_DECOM;
#if 0
		res = ((flags & VTFL_DECOM) ? 1 : 2);
#endif
		break;
	case 7: /* DECAWM autowrap */
		if (op == VTMODE_SET)
			flags |= VTFL_DECAWM;
		else if (op == VTMODE_RESET)
			flags &= ~VTFL_DECAWM;
#if 0
		res = ((flags & VTFL_DECAWM) ? 1 : 2);
#endif
		break;
	case 8: /* DECARM keyboard autorepeat */
		break;
	case 18: /* DECPFF print form feed */
		break;
	case 19: /* DECPEX printer extent: screen/scrolling region */
		break;
	case 25: /* DECTCEM text cursor on/off */
		if (op == VTMODE_SET)
			flags |= VTFL_CURSORON;
		else if (op == VTMODE_RESET)
			flags &= ~VTFL_CURSORON;
		if (flags != edp->flags)
			WSEMULOP(rc, edp, &edp->abortstate, cursor,
			    (edp->emulcookie, flags & VTFL_CURSORON, edp->crow,
			     edp->ccol));
#if 0
		res = ((flags & VTFL_CURSORON) ? 1 : 2);
#endif
		break;
	case 42: /* DECNRCM use 7-bit NRC /
		    7/8 bit from DEC multilingual or ISO-latin-1*/
		if (op == VTMODE_SET)
			flags |= VTFL_NATCHARSET;
		else if (op == VTMODE_RESET)
			flags &= ~VTFL_NATCHARSET;
#if 0
		res = ((flags & VTFL_NATCHARSET) ? 1 : 2);
#endif
		break;
	case 66: /* DECNKM numeric keypad */
		break;
	case 68: /* DECKBUM keyboard usage data processing/typewriter */
		break;
	default:
#ifdef VT100_PRINTUNKNOWN
		printf("DEC mode %d unknown\n", nr);
#endif
		break;
	}
	edp->flags = flags;

	return rc;
}
