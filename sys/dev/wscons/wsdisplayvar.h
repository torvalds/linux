/* $OpenBSD: wsdisplayvar.h,v 1.38 2020/09/13 10:05:46 fcambus Exp $ */
/* $NetBSD: wsdisplayvar.h,v 1.30 2005/02/04 02:10:49 perry Exp $ */

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
 * Default to white on black except on Sun hardware, where we default
 * to black on white to match the firmware console.
 */
#ifndef WS_DEFAULT_FG
#if defined(__sparc64__)
#define WS_DEFAULT_FG WSCOL_BLACK
#else
#define WS_DEFAULT_FG WSCOL_WHITE
#endif
#endif
#ifndef WS_DEFAULT_BG
#if defined(__sparc64__)
#define WS_DEFAULT_BG WSCOL_WHITE
#else
#define WS_DEFAULT_BG WSCOL_BLACK
#endif
#endif

struct device;

/*
 * WSDISPLAY interfaces
 */

#define WSDISPLAY_MAXSCREEN	12

/*
 * Emulation functions, for displays that can support glass-tty terminal
 * emulations.  These are character oriented, with row and column
 * numbers starting at zero in the upper left hand corner of the
 * screen.
 *
 * These are used only when emulating a terminal.  Therefore, displays
 * drivers which cannot emulate terminals do not have to provide them.
 *
 * There is a "void *" cookie provided by the display driver associated
 * with these functions, which is passed to them when they are invoked.
 */
struct wsdisplay_emulops {
	int	(*cursor)(void *c, int on, int row, int col);
	int	(*mapchar)(void *, int, unsigned int *);
	int	(*putchar)(void *c, int row, int col, u_int uc, uint32_t attr);
	int	(*copycols)(void *c, int row, int srccol, int dstcol,
		    int ncols);
	int	(*erasecols)(void *c, int row, int startcol, int ncols,
		    uint32_t);
	int	(*copyrows)(void *c, int srcrow, int dstrow, int nrows);
	int	(*eraserows)(void *c, int row, int nrows, uint32_t attr);
	int	(*pack_attr)(void *c, int fg, int bg, int flags,
		    uint32_t *attrp);
	void	(*unpack_attr)(void *c, uint32_t attr, int *fg, int *bg,
		    int *ul);
/* fg / bg values. Made identical to ANSI terminal color codes. */
#define WSCOL_BLACK	0
#define WSCOL_RED	1
#define WSCOL_GREEN	2
#define WSCOL_BROWN	3
#define WSCOL_BLUE	4
#define WSCOL_MAGENTA	5
#define WSCOL_CYAN	6
#define WSCOL_WHITE	7
/* flag values: */
#define WSATTR_REVERSE	1
#define WSATTR_HILIT	2
#define WSATTR_BLINK	4
#define WSATTR_UNDERLINE 8
#define WSATTR_WSCOLORS 16
};

#define	WSSCREEN_NAME_SIZE	16

struct wsscreen_descr {
	char name[WSSCREEN_NAME_SIZE];
	int ncols, nrows;
	const struct wsdisplay_emulops *textops;
	int fontwidth, fontheight;
	int capabilities;
#define WSSCREEN_WSCOLORS	1	/* minimal color capability */
#define WSSCREEN_REVERSE	2	/* can display reversed */
#define WSSCREEN_HILIT		4	/* can highlight (however) */
#define WSSCREEN_BLINK		8	/* can blink */
#define WSSCREEN_UNDERLINE	16	/* can underline */
};

/*
 * Character cell description (for emulation mode).
 */
struct wsdisplay_charcell {
	u_int		uc;
	uint32_t	attr;
};

struct wsdisplay_font;
/*
 * Display access functions, invoked by user-land programs which require
 * direct device access, such as X11.
 *
 * There is a "void *" cookie provided by the display driver associated
 * with these functions, which is passed to them when they are invoked.
 */
struct wsdisplay_accessops {
	int	(*ioctl)(void *v, u_long cmd, caddr_t data, int flag,
		    struct proc *p);
	paddr_t	(*mmap)(void *v, off_t off, int prot);
	int	(*alloc_screen)(void *, const struct wsscreen_descr *,
				     void **, int *, int *, uint32_t *);
	void	(*free_screen)(void *, void *);
	int	(*show_screen)(void *, void *, int,
			       void (*) (void *, int, int), void *);
	int	(*load_font)(void *, void *, struct wsdisplay_font *);
	int	(*list_font)(void *, struct wsdisplay_font *);
	void	(*scrollback)(void *, void *, int);
	int	(*getchar)(void *, int, int, struct wsdisplay_charcell *);
	void	(*burn_screen)(void *, u_int, u_int);
	void	(*pollc)(void *, int);
	void	(*enter_ddb)(void *, void *);
};

/* passed to wscons by the video driver to tell about its capabilities */
struct wsscreen_list {
	int nscreens;
	const struct wsscreen_descr **screens;
};

/*
 * Attachment information provided by wsemuldisplaydev devices when attaching
 * wsdisplay units.
 */
struct wsemuldisplaydev_attach_args {
	int	console;				/* is it console? */
	int	primary;				/* is it primary? */
	const struct wsscreen_list *scrdata;		/* screen cfg info */
	const struct wsdisplay_accessops *accessops;	/* access ops */
	void	*accesscookie;				/* access cookie */
	u_int	defaultscreens;				/* screens to create */
};

#define	WSEMULDISPLAYDEVCF_CONSOLE	0
#define	wsemuldisplaydevcf_console	cf_loc[WSEMULDISPLAYDEVCF_CONSOLE]	/* spec'd as console? */
#define	WSEMULDISPLAYDEVCF_CONSOLE_UNK	-1
#define WSEMULDISPLAYDEVCF_PRIMARY	1
#define	wsemuldisplaydevcf_primary	cf_loc[WSEMULDISPLAYDEVCF_PRIMARY]	/* spec'd as primary? */
#define	WSEMULDISPLAYDEVCF_PRIMARY_UNK	-1
#define	WSEMULDISPLAYDEVCF_MUX		2
#define	wsemuldisplaydevcf_mux		cf_loc[WSEMULDISPLAYDEVCF_MUX]
#define	WSDISPLAYDEVCF_MUX		0
#define	wsdisplaydevcf_mux		cf_loc[WSDISPLAYDEVCF_MUX]

struct wscons_syncops {
	int (*detach)(void *, int, void (*)(void *, int, int), void *);
	int (*attach)(void *, int, void (*)(void *, int, int), void *);
	int (*check)(void *);
	void (*destroy)(void *);
};

/*
 * Autoconfiguration helper functions.
 */
void	wsdisplay_cnattach(const struct wsscreen_descr *, void *,
				int, int, uint32_t);
int	wsemuldisplaydevprint(void *, const char *);
int	wsemuldisplaydevsubmatch(struct device *, void *, void *);

/*
 * Console interface.
 */
void	wsdisplay_cnputc(dev_t dev, int i);

/*
 * for use by compatibility code
 */
struct wsdisplay_softc;
struct wsscreen;
int wsscreen_attach_sync(struct wsscreen *,
			      const struct wscons_syncops *, void *);
int wsscreen_detach_sync(struct wsscreen *);
int wsscreen_lookup_sync(struct wsscreen *,
			      const struct wscons_syncops *, void **);

int wsdisplay_maxscreenidx(struct wsdisplay_softc *);
int wsdisplay_screenstate(struct wsdisplay_softc *, int);
int wsdisplay_getactivescreen(struct wsdisplay_softc *);
int wsscreen_switchwait(struct wsdisplay_softc *, int);

int wsdisplay_internal_ioctl(struct wsdisplay_softc *sc,
				  struct wsscreen *,
				  u_long cmd, caddr_t data,
				  int flag, struct proc *p);

int wsdisplay_usl_ioctl1(struct wsdisplay_softc *,
			     u_long, caddr_t, int, struct proc *);

int wsdisplay_usl_ioctl2(struct wsdisplay_softc *, struct wsscreen *,
			     u_long, caddr_t, int, struct proc *);

int wsdisplay_cfg_ioctl(struct wsdisplay_softc *sc,
			     u_long cmd, caddr_t data,
			     int flag, struct proc *p);

/*
 * for general use
 */
#define WSDISPLAY_NULLSCREEN	-1
void wsdisplay_switchtoconsole(void);
void wsdisplay_enter_ddb(void);
void wsdisplay_suspend(void);
void wsdisplay_resume(void);
const struct wsscreen_descr *
    wsdisplay_screentype_pick(const struct wsscreen_list *, const char *);

struct wsdisplay_param;
extern int (*ws_get_param)(struct wsdisplay_param *);
extern int (*ws_set_param)(struct wsdisplay_param *);

void wsdisplay_brightness_step(struct device *, int);
void wsdisplay_brightness_zero(struct device *);
void wsdisplay_brightness_cycle(struct device *);

/*
 * for use by wskbd
 */
void wsdisplay_burn(void *v, u_int flags);
void wsscrollback(void *v, int op);

#define WSDISPLAY_SCROLL_BACKWARD	0
#define WSDISPLAY_SCROLL_FORWARD	1
#define WSDISPLAY_SCROLL_RESET		2

/*
 * screen burner
 */
#define	WSDISPLAY_DEFBURNOUT_MSEC	0	/* disabled */
#define	WSDISPLAY_DEFBURNIN_MSEC	250	/* milliseconds */
