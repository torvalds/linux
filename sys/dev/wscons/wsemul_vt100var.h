/* $OpenBSD: wsemul_vt100var.h,v 1.14 2024/11/05 08:12:08 miod Exp $ */
/* $NetBSD: wsemul_vt100var.h,v 1.5 2000/04/28 21:56:17 mycroft Exp $ */

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

#define	VT100_EMUL_NARGS	10	/* max # of args to a command */

struct wsemul_vt100_emuldata {
	const struct wsdisplay_emulops *emulops;
	struct wsemul_abortstate abortstate;
	void *emulcookie;
	int scrcapabilities;
	u_int nrows, ncols, crow, ccol;
	uint32_t defattr;		/* default attribute */

	uint32_t kernattr;		/* attribute for kernel output */
	void *cbcookie;
#ifdef DIAGNOSTIC
	int console;
#endif

	u_int state;			/* processing state */
	int flags;
#define VTFL_LASTCHAR	0x001	/* printed last char on line (below cursor) */
#define VTFL_INSERTMODE	0x002
#define VTFL_APPLKEYPAD	0x004
#define VTFL_APPLCURSOR	0x008
#define VTFL_DECOM	0x010	/* origin mode */
#define VTFL_DECAWM	0x020	/* auto wrap */
#define VTFL_CURSORON	0x040
#define VTFL_NATCHARSET	0x080	/* national replacement charset mode */
#define VTFL_SAVEDCURS	0x100	/* we have a saved cursor state */
#define VTFL_UTF8	0x200	/* utf-8 character set */
	uint32_t curattr, bkgdattr;	/* currently used attribute */
	int attrflags, fgcol, bgcol;	/* properties of curattr */
	u_int scrreg_startrow;
	u_int scrreg_nrows;
	char *tabs;
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
	char *dblwid;
	int dw;
#endif

	int chartab0, chartab1;
	u_int *chartab_G[4];
	u_int *isolatin1tab, *decgraphtab, *dectechtab;
	u_int *nrctab;
	int sschartab; /* single shift */

	int nargs;
	u_int args[VT100_EMUL_NARGS]; /* numeric command args (CSI/DCS) */

	char modif1;	/* {>?} in VT100_EMUL_STATE_CSI */
	char modif2;	/* {!"$&} in VT100_EMUL_STATE_CSI */

	int designating;	/* substate in VT100_EMUL_STATE_SCS* */

	int dcstype;		/* substate in VT100_EMUL_STATE_STRING */
	char *dcsarg;
	int dcspos;
#define DCS_MAXLEN 256 /* ??? */
#define DCSTYPE_TABRESTORE 1 /* DCS2$t */

	u_int savedcursor_row, savedcursor_col;
	uint32_t savedattr, savedbkgdattr;
	int savedattrflags, savedfgcol, savedbgcol;
	int savedchartab0, savedchartab1;
	u_int *savedchartab_G[4];

	struct wsemul_inputstate instate;	/* userland input state */
	struct wsemul_inputstate kstate;	/* kernel input state */

#ifdef HAVE_UTF8_SUPPORT
	u_char translatebuf[4];
#else
	u_char translatebuf[1];
#endif
};

/* some useful utility macros */
#define	ARG(n)			(edp->args[(n)])
#define	DEF1_ARG(n)		(ARG(n) ? ARG(n) : 1)
#define	DEFx_ARG(n, x)		(ARG(n) ? ARG(n) : (x))
/* the following two can be negative if we are outside the scrolling region */
#define ROWS_ABOVE	((int)edp->crow - (int)edp->scrreg_startrow)
#define ROWS_BELOW	((int)(edp->scrreg_startrow + edp->scrreg_nrows) \
					- (int)edp->crow - 1)
#ifdef HAVE_DOUBLE_WIDTH_HEIGHT
#define CHECK_DW do { \
	if (edp->dblwid && edp->dblwid[edp->crow]) { \
		edp->dw = 1; \
		if (edp->ccol > (edp->ncols >> 1) - 1) \
			edp->ccol = (edp->ncols >> 1) - 1; \
	} else \
		edp->dw = 0; \
} while (0)
#define NCOLS		(edp->ncols >> edp->dw)
#define COPYCOLS(f, t, n)	(edp->emulcookie, edp->crow, (f) << edp->dw, \
				 (t) << edp->dw, (n) << edp->dw)
#define ERASECOLS(f, n, a)	(edp->emulcookie, edp->crow, (f) << edp->dw, \
				 (n) << edp->dw, (a))
#else
#define CHECK_DW do { } while (0)
#define NCOLS		(edp->ncols)
#define COPYCOLS(f, t, n)	(edp->emulcookie, edp->crow, (f), (t), (n))
#define ERASECOLS(f, n, a)	(edp->emulcookie, edp->crow, (f), (n), (a))
#endif
#define	COLS_LEFT	(NCOLS - edp->ccol - 1)

/*
 * response to primary DA request
 * operating level: 61 = VT100, 62 = VT200, 63 = VT300
 * extensions: 1 = 132 cols, 2 = printer port, 6 = selective erase,
 *	7 = soft charset, 8 = UDKs, 9 = NRC sets
 * VT100 = "033[?1;2c"
 */
#define WSEMUL_VT_ID1 "\033[?62;6c"
/*
 * response to secondary DA request
 * ident code: 24 = VT320
 * firmware version
 * hardware options: 0 = no options
 */
#define WSEMUL_VT_ID2 "\033[>24;20;0c"

void	wsemul_vt100_reset(struct wsemul_vt100_emuldata *);
int	wsemul_vt100_scrollup(struct wsemul_vt100_emuldata *, int);
int	wsemul_vt100_scrolldown(struct wsemul_vt100_emuldata *, int);
int	wsemul_vt100_ed(struct wsemul_vt100_emuldata *, int);
int	wsemul_vt100_el(struct wsemul_vt100_emuldata *, int);
int	wsemul_vt100_handle_csi(struct wsemul_vt100_emuldata *,
	    struct wsemul_inputstate *, int);
void	wsemul_vt100_handle_dcs(struct wsemul_vt100_emuldata *);

int	wsemul_vt100_translate(void *cookie, kbd_t, keysym_t, const u_char **);

void	vt100_initchartables(struct wsemul_vt100_emuldata *);
int	vt100_setnrc(struct wsemul_vt100_emuldata *, int);

int	wsemul_vt100_output_normal(struct wsemul_vt100_emuldata *,
	    struct wsemul_inputstate *, int, int);
