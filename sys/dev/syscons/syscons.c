/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992-1998 Søren Schmidt
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"
#include "opt_splash.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/kdb.h>
#include <sys/eventhandler.h>
#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/reboot.h>
#include <sys/serial.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/power.h>

#include <machine/clock.h>
#if defined(__arm__) || defined(__mips__) || \
	defined(__powerpc__) || defined(__sparc64__)
#include <machine/sc_machdep.h>
#else
#include <machine/pc/display.h>
#endif
#if defined( __i386__) || defined(__amd64__)
#include <machine/psl.h>
#include <machine/frame.h>
#endif
#include <machine/stdarg.h>

#if defined(__amd64__) || defined(__i386__)
#include <machine/vmparam.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#endif

#include <dev/kbd/kbdreg.h>
#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>
#include <dev/syscons/syscons.h>

#define COLD 0
#define WARM 1

#define DEFAULT_BLANKTIME	(5*60)		/* 5 minutes */
#define MAX_BLANKTIME		(7*24*60*60)	/* 7 days!? */

#define KEYCODE_BS		0x0e		/* "<-- Backspace" key, XXX */

/* NULL-safe version of "tty_opened()" */
#define	tty_opened_ns(tp)	((tp) != NULL && tty_opened(tp))

static	u_char		sc_kattrtab[MAXCPU];

static	int		sc_console_unit = -1;
static	int		sc_saver_keyb_only = 1;
static  scr_stat    	*sc_console;
static  struct consdev	*sc_consptr;
static	void		*sc_kts[MAXCPU];
static	struct sc_term_sw *sc_ktsw;
static	scr_stat	main_console;
static	struct tty 	*main_devs[MAXCONS];

static  char        	init_done = COLD;
static	int		shutdown_in_progress = FALSE;
static	int		suspend_in_progress = FALSE;
static	char		sc_malloc = FALSE;

static	int		saver_mode = CONS_NO_SAVER; /* LKM/user saver */
static	int		run_scrn_saver = FALSE;	/* should run the saver? */
static	int		enable_bell = TRUE; /* enable beeper */

#ifndef SC_DISABLE_REBOOT
static  int		enable_reboot = TRUE; /* enable keyboard reboot */
#endif

#ifndef SC_DISABLE_KDBKEY
static  int		enable_kdbkey = TRUE; /* enable keyboard debug */
#endif

static	long        	scrn_blank_time = 0;    /* screen saver timeout value */
#ifdef DEV_SPLASH
static	int     	scrn_blanked;		/* # of blanked screen */
static	int		sticky_splash = FALSE;

static	void		none_saver(sc_softc_t *sc, int blank) { }
static	void		(*current_saver)(sc_softc_t *, int) = none_saver;
#endif

#ifdef SC_NO_SUSPEND_VTYSWITCH
static	int		sc_no_suspend_vtswitch = 1;
#else
static	int		sc_no_suspend_vtswitch = 0;
#endif
static	int		sc_susp_scr;

static SYSCTL_NODE(_hw, OID_AUTO, syscons, CTLFLAG_RD, 0, "syscons");
static SYSCTL_NODE(_hw_syscons, OID_AUTO, saver, CTLFLAG_RD, 0, "saver");
SYSCTL_INT(_hw_syscons_saver, OID_AUTO, keybonly, CTLFLAG_RW,
    &sc_saver_keyb_only, 0, "screen saver interrupted by input only");
SYSCTL_INT(_hw_syscons, OID_AUTO, bell, CTLFLAG_RW, &enable_bell, 
    0, "enable bell");
#ifndef SC_DISABLE_REBOOT
SYSCTL_INT(_hw_syscons, OID_AUTO, kbd_reboot, CTLFLAG_RW|CTLFLAG_SECURE, &enable_reboot,
    0, "enable keyboard reboot");
#endif
#ifndef SC_DISABLE_KDBKEY
SYSCTL_INT(_hw_syscons, OID_AUTO, kbd_debug, CTLFLAG_RW|CTLFLAG_SECURE, &enable_kdbkey,
    0, "enable keyboard debug");
#endif
SYSCTL_INT(_hw_syscons, OID_AUTO, sc_no_suspend_vtswitch, CTLFLAG_RWTUN,
    &sc_no_suspend_vtswitch, 0, "Disable VT switch before suspend.");
#if !defined(SC_NO_FONT_LOADING) && defined(SC_DFLT_FONT)
#include "font.h"
#endif

	tsw_ioctl_t	*sc_user_ioctl;

static	bios_values_t	bios_value;

static	int		enable_panic_key;
SYSCTL_INT(_machdep, OID_AUTO, enable_panic_key, CTLFLAG_RW, &enable_panic_key,
	   0, "Enable panic via keypress specified in kbdmap(5)");

#define SC_CONSOLECTL	255

#define VTY_WCHAN(sc, vty) (&SC_DEV(sc, vty))

/* prototypes */
static int sc_allocate_keyboard(sc_softc_t *sc, int unit);
static int scvidprobe(int unit, int flags, int cons);
static int sckbdprobe(int unit, int flags, int cons);
static void scmeminit(void *arg);
static int scdevtounit(struct tty *tp);
static kbd_callback_func_t sckbdevent;
static void scinit(int unit, int flags);
static scr_stat *sc_get_stat(struct tty *tp);
static void scterm(int unit, int flags);
static void scshutdown(void *, int);
static void scsuspend(void *);
static void scresume(void *);
static u_int scgetc(sc_softc_t *sc, u_int flags, struct sc_cnstate *sp);
static void sc_puts(scr_stat *scp, u_char *buf, int len);
#define SCGETC_CN	1
#define SCGETC_NONBLOCK	2
static void sccnupdate(scr_stat *scp);
static scr_stat *alloc_scp(sc_softc_t *sc, int vty);
static void init_scp(sc_softc_t *sc, int vty, scr_stat *scp);
static timeout_t scrn_timer;
static int and_region(int *s1, int *e1, int s2, int e2);
static void scrn_update(scr_stat *scp, int show_cursor);

#ifdef DEV_SPLASH
static int scsplash_callback(int event, void *arg);
static void scsplash_saver(sc_softc_t *sc, int show);
static int add_scrn_saver(void (*this_saver)(sc_softc_t *, int));
static int remove_scrn_saver(void (*this_saver)(sc_softc_t *, int));
static int set_scrn_saver_mode(scr_stat *scp, int mode, u_char *pal, int border);
static int restore_scrn_saver_mode(scr_stat *scp, int changemode);
static void stop_scrn_saver(sc_softc_t *sc, void (*saver)(sc_softc_t *, int));
static int wait_scrn_saver_stop(sc_softc_t *sc);
#define scsplash_stick(stick)		(sticky_splash = (stick))
#else /* !DEV_SPLASH */
#define scsplash_stick(stick)
#endif /* DEV_SPLASH */

static int do_switch_scr(sc_softc_t *sc, int s);
static int vt_proc_alive(scr_stat *scp);
static int signal_vt_rel(scr_stat *scp);
static int signal_vt_acq(scr_stat *scp);
static int finish_vt_rel(scr_stat *scp, int release, int *s);
static int finish_vt_acq(scr_stat *scp);
static void exchange_scr(sc_softc_t *sc);
static void update_cursor_image(scr_stat *scp);
static void change_cursor_shape(scr_stat *scp, int flags, int base, int height);
static void update_font(scr_stat *);
static int save_kbd_state(scr_stat *scp);
static int update_kbd_state(scr_stat *scp, int state, int mask);
static int update_kbd_leds(scr_stat *scp, int which);
static int sc_kattr(void);
static timeout_t blink_screen;
static struct tty *sc_alloc_tty(int, int);

static cn_probe_t	sc_cnprobe;
static cn_init_t	sc_cninit;
static cn_term_t	sc_cnterm;
static cn_getc_t	sc_cngetc;
static cn_putc_t	sc_cnputc;
static cn_grab_t	sc_cngrab;
static cn_ungrab_t	sc_cnungrab;

CONSOLE_DRIVER(sc);

static	tsw_open_t	sctty_open;
static	tsw_close_t	sctty_close;
static	tsw_outwakeup_t	sctty_outwakeup;
static	tsw_ioctl_t	sctty_ioctl;
static	tsw_mmap_t	sctty_mmap;

static struct ttydevsw sc_ttydevsw = {
	.tsw_open	= sctty_open,
	.tsw_close	= sctty_close,
	.tsw_outwakeup	= sctty_outwakeup,
	.tsw_ioctl	= sctty_ioctl,
	.tsw_mmap	= sctty_mmap,
};

static d_ioctl_t	consolectl_ioctl;
static d_close_t	consolectl_close;

static struct cdevsw consolectl_devsw = {
	.d_version	= D_VERSION,
	.d_flags	= D_NEEDGIANT | D_TRACKCLOSE,
	.d_ioctl	= consolectl_ioctl,
	.d_close	= consolectl_close,
	.d_name		= "consolectl",
};

/* ec -- emergency console. */

static	u_int	ec_scroffset;

static void
ec_putc(int c)
{
	uintptr_t fb;
	u_short *scrptr;
	u_int ind;
	int attr, column, mysize, width, xsize, yborder, ysize;

	if (c < 0 || c > 0xff || c == '\a')
		return;
	if (sc_console == NULL) {
#if !defined(__amd64__) && !defined(__i386__)
		return;
#else
		/*
		 * This is enough for ec_putc() to work very early on x86
		 * if the kernel starts in normal color text mode.
		 */
#ifdef __amd64__
		fb = KERNBASE + 0xb8000;
#else /* __i386__ */
		fb = pmap_get_map_low() + 0xb8000;
#endif
		xsize = 80;
		ysize = 25;
#endif
	} else {
		if (!ISTEXTSC(&main_console))
			return;
		fb = main_console.sc->adp->va_window;
		xsize = main_console.xsize;
		ysize = main_console.ysize;
	}
	yborder = ysize / 5;
	scrptr = (u_short *)(void *)fb + xsize * yborder;
	mysize = xsize * (ysize - 2 * yborder);
	do {
		ind = ec_scroffset;
		column = ind % xsize;
		width = (c == '\b' ? -1 : c == '\t' ? (column + 8) & ~7 :
		    c == '\r' ? -column : c == '\n' ? xsize - column : 1);
		if (width == 0 || (width < 0 && ind < -width))
			return;
	} while (atomic_cmpset_rel_int(&ec_scroffset, ind, ind + width) == 0);
	if (c == '\b' || c == '\r')
		return;
	if (c == '\n')
		ind += xsize;	/* XXX clearing from new pos is not atomic */

	attr = sc_kattr();
	if (c == '\t' || c == '\n')
		c = ' ';
	do
		scrptr[ind++ % mysize] = (attr << 8) | c;
	while (--width != 0);
}

int
sc_probe_unit(int unit, int flags)
{
    if (!vty_enabled(VTY_SC))
        return ENXIO;
    if (!scvidprobe(unit, flags, FALSE)) {
	if (bootverbose)
	    printf("%s%d: no video adapter found.\n", SC_DRIVER_NAME, unit);
	return ENXIO;
    }

    /* syscons will be attached even when there is no keyboard */
    sckbdprobe(unit, flags, FALSE);

    return 0;
}

/* probe video adapters, return TRUE if found */ 
static int
scvidprobe(int unit, int flags, int cons)
{
    /*
     * Access the video adapter driver through the back door!
     * Video adapter drivers need to be configured before syscons.
     * However, when syscons is being probed as the low-level console,
     * they have not been initialized yet.  We force them to initialize
     * themselves here. XXX
     */
    vid_configure(cons ? VIO_PROBE_ONLY : 0);

    return (vid_find_adapter("*", unit) >= 0);
}

/* probe the keyboard, return TRUE if found */
static int
sckbdprobe(int unit, int flags, int cons)
{
    /* access the keyboard driver through the backdoor! */
    kbd_configure(cons ? KB_CONF_PROBE_ONLY : 0);

    return (kbd_find_keyboard("*", unit) >= 0);
}

static char
*adapter_name(video_adapter_t *adp)
{
    static struct {
	int type;
	char *name[2];
    } names[] = {
	{ KD_MONO,	{ "MDA",	"MDA" } },
	{ KD_HERCULES,	{ "Hercules",	"Hercules" } },
	{ KD_CGA,	{ "CGA",	"CGA" } },
	{ KD_EGA,	{ "EGA",	"EGA (mono)" } },
	{ KD_VGA,	{ "VGA",	"VGA (mono)" } },
	{ KD_TGA,	{ "TGA",	"TGA" } },
	{ -1,		{ "Unknown",	"Unknown" } },
    };
    int i;

    for (i = 0; names[i].type != -1; ++i)
	if (names[i].type == adp->va_type)
	    break;
    return names[i].name[(adp->va_flags & V_ADP_COLOR) ? 0 : 1];
}

static void
sctty_outwakeup(struct tty *tp)
{
    size_t len;
    u_char buf[PCBURST];
    scr_stat *scp = sc_get_stat(tp);

    if (scp->status & SLKED ||
	(scp == scp->sc->cur_scp && scp->sc->blink_in_progress))
	return;

    for (;;) {
	len = ttydisc_getc(tp, buf, sizeof buf);
	if (len == 0)
	    break;
	SC_VIDEO_LOCK(scp->sc);
	sc_puts(scp, buf, len);
	SC_VIDEO_UNLOCK(scp->sc);
    }
}

static struct tty *
sc_alloc_tty(int index, int devnum)
{
	struct sc_ttysoftc *stc;
	struct tty *tp;

	/* Allocate TTY object and softc to store unit number. */
	stc = malloc(sizeof(struct sc_ttysoftc), M_DEVBUF, M_WAITOK);
	stc->st_index = index;
	stc->st_stat = NULL;
	tp = tty_alloc_mutex(&sc_ttydevsw, stc, &Giant);

	/* Create device node. */
	tty_makedev(tp, NULL, "v%r", devnum);

	return (tp);
}

#ifdef SC_PIXEL_MODE
static void
sc_set_vesa_mode(scr_stat *scp, sc_softc_t *sc, int unit)
{
	video_info_t info;
	u_char *font;
	int depth;
	int fontsize;
	int i;
	int vmode;

	vmode = 0;
	(void)resource_int_value("sc", unit, "vesa_mode", &vmode);
	if (vmode < M_VESA_BASE || vmode > M_VESA_MODE_MAX ||
	    vidd_get_info(sc->adp, vmode, &info) != 0 ||
	    !sc_support_pixel_mode(&info))
		vmode = 0;

	/*
	 * If the mode is unset or unsupported, search for an available
	 * 800x600 graphics mode with the highest color depth.
	 */
	if (vmode == 0) {
		for (depth = 0, i = M_VESA_BASE; i <= M_VESA_MODE_MAX; i++)
			if (vidd_get_info(sc->adp, i, &info) == 0 &&
			    info.vi_width == 800 && info.vi_height == 600 &&
			    sc_support_pixel_mode(&info) &&
			    info.vi_depth > depth) {
				vmode = i;
				depth = info.vi_depth;
			}
		if (vmode == 0)
			return;
		vidd_get_info(sc->adp, vmode, &info);
	}

#if !defined(SC_NO_FONT_LOADING) && defined(SC_DFLT_FONT)
	fontsize = info.vi_cheight;
#else
	fontsize = scp->font_size;
#endif
	if (fontsize < 14)
		fontsize = 8;
	else if (fontsize >= 16)
		fontsize = 16;
	else
		fontsize = 14;
#ifndef SC_NO_FONT_LOADING
	switch (fontsize) {
	case 8:
		if ((sc->fonts_loaded & FONT_8) == 0)
			return;
		font = sc->font_8;
		break;
	case 14:
		if ((sc->fonts_loaded & FONT_14) == 0)
			return;
		font = sc->font_14;
		break;
	case 16:
		if ((sc->fonts_loaded & FONT_16) == 0)
			return;
		font = sc->font_16;
		break;
	}
#else
	font = NULL;
#endif
#ifdef DEV_SPLASH
	if ((sc->flags & SC_SPLASH_SCRN) != 0)
		splash_term(sc->adp);
#endif
#ifndef SC_NO_HISTORY
	if (scp->history != NULL) {
		sc_vtb_append(&scp->vtb, 0, scp->history,
		    scp->ypos * scp->xsize + scp->xpos);
		scp->history_pos = sc_vtb_tail(scp->history);
	}
#endif
	vidd_set_mode(sc->adp, vmode);
	scp->status |= (UNKNOWN_MODE | PIXEL_MODE | MOUSE_HIDDEN);
	scp->status &= ~(GRAPHICS_MODE | MOUSE_VISIBLE);
	scp->xpixel = info.vi_width;
	scp->ypixel = info.vi_height;
	scp->xsize = scp->xpixel / 8;
	scp->ysize = scp->ypixel / fontsize;
	scp->xpos = 0;
	scp->ypos = scp->ysize - 1;
	scp->xoff = scp->yoff = 0;
	scp->font = font;
	scp->font_size = fontsize;
	scp->font_width = 8;
	scp->start = scp->xsize * scp->ysize - 1;
	scp->end = 0;
	scp->cursor_pos = scp->cursor_oldpos = scp->xsize * scp->xsize;
	scp->mode = sc->initial_mode = vmode;
#ifndef __sparc64__
	sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
	    (void *)sc->adp->va_window, FALSE);
#endif
	sc_alloc_scr_buffer(scp, FALSE, FALSE);
	sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
	sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
	sc_alloc_history_buffer(scp, 0, 0, FALSE);
#endif
	sc_set_border(scp, scp->border);
	sc_set_cursor_image(scp);
	scp->status &= ~UNKNOWN_MODE;
#ifdef DEV_SPLASH
	if ((sc->flags & SC_SPLASH_SCRN) != 0)
		splash_init(sc->adp, scsplash_callback, sc);
#endif
}
#endif

int
sc_attach_unit(int unit, int flags)
{
    sc_softc_t *sc;
    scr_stat *scp;
    struct cdev *dev;
    void *oldts, *ts;
    int i, vc;

    if (!vty_enabled(VTY_SC))
        return ENXIO;

    flags &= ~SC_KERNEL_CONSOLE;

    if (sc_console_unit == unit) {
	/*
	 * If this unit is being used as the system console, we need to
	 * adjust some variables and buffers before and after scinit().
	 */
	/* assert(sc_console != NULL) */
	flags |= SC_KERNEL_CONSOLE;
	scmeminit(NULL);

	scinit(unit, flags);

	if (sc_console->tsw->te_size > 0) {
	    sc_ktsw = sc_console->tsw;
	    /* assert(sc_console->ts != NULL); */
	    oldts = sc_console->ts;
	    for (i = 0; i <= mp_maxid; i++) {
		ts = malloc(sc_console->tsw->te_size, M_DEVBUF,
			    M_WAITOK | M_ZERO);
		(*sc_console->tsw->te_init)(sc_console, &ts, SC_TE_COLD_INIT);
		sc_console->ts = ts;
		(*sc_console->tsw->te_default_attr)(sc_console, sc_kattrtab[i],
						    SC_KERNEL_CONS_REV_ATTR);
		sc_kts[i] = ts;
	    }
	    sc_console->ts = oldts;
    	    (*sc_console->tsw->te_default_attr)(sc_console, SC_NORM_ATTR,
						SC_NORM_REV_ATTR);
	}
    } else {
	scinit(unit, flags);
    }

    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);
    sc->config = flags;
    callout_init(&sc->ctimeout, 0);
    callout_init(&sc->cblink, 0);
    scp = sc_get_stat(sc->dev[0]);
    if (sc_console == NULL)	/* sc_console_unit < 0 */
	sc_console = scp;

#ifdef SC_PIXEL_MODE
    if ((sc->config & SC_VESAMODE) != 0)
	sc_set_vesa_mode(scp, sc, unit);
#endif /* SC_PIXEL_MODE */

    /* initialize cursor */
    if (!ISGRAPHSC(scp))
    	update_cursor_image(scp);

    /* get screen update going */
    scrn_timer(sc);

    /* set up the keyboard */
    (void)kbdd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
    update_kbd_state(scp, scp->status, LOCK_MASK);

    printf("%s%d: %s <%d virtual consoles, flags=0x%x>\n",
	   SC_DRIVER_NAME, unit, adapter_name(sc->adp), sc->vtys, sc->config);
    if (bootverbose) {
	printf("%s%d:", SC_DRIVER_NAME, unit);
    	if (sc->adapter >= 0)
	    printf(" fb%d", sc->adapter);
	if (sc->keyboard >= 0)
	    printf(", kbd%d", sc->keyboard);
	if (scp->tsw)
	    printf(", terminal emulator: %s (%s)",
		   scp->tsw->te_name, scp->tsw->te_desc);
	printf("\n");
    }

    /* Register suspend/resume/shutdown callbacks for the kernel console. */
    if (sc_console_unit == unit) {
	EVENTHANDLER_REGISTER(power_suspend_early, scsuspend, NULL,
			      EVENTHANDLER_PRI_ANY);
	EVENTHANDLER_REGISTER(power_resume, scresume, NULL,
			      EVENTHANDLER_PRI_ANY);
	EVENTHANDLER_REGISTER(shutdown_pre_sync, scshutdown, NULL,
			      SHUTDOWN_PRI_DEFAULT);
    }

    for (vc = 0; vc < sc->vtys; vc++) {
	if (sc->dev[vc] == NULL) {
		sc->dev[vc] = sc_alloc_tty(vc, vc + unit * MAXCONS);
		if (vc == 0 && sc->dev == main_devs)
			SC_STAT(sc->dev[0]) = &main_console;
	}
	/*
	 * The first vty already has struct tty and scr_stat initialized
	 * in scinit().  The other vtys will have these structs when
	 * first opened.
	 */
    }

    dev = make_dev(&consolectl_devsw, 0, UID_ROOT, GID_WHEEL, 0600,
        "consolectl");
    dev->si_drv1 = sc->dev[0];

    return 0;
}

static void
scmeminit(void *arg)
{
    if (!vty_enabled(VTY_SC))
        return;
    if (sc_malloc)
	return;
    sc_malloc = TRUE;

    /*
     * As soon as malloc() becomes functional, we had better allocate
     * various buffers for the kernel console.
     */

    if (sc_console_unit < 0)	/* sc_console == NULL */
	return;

    /* copy the temporary buffer to the final buffer */
    sc_alloc_scr_buffer(sc_console, FALSE, FALSE);

#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(sc_console, FALSE);
#endif

#ifndef SC_NO_HISTORY
    /* initialize history buffer & pointers */
    sc_alloc_history_buffer(sc_console, 0, 0, FALSE);
#endif
}

/* XXX */
SYSINIT(sc_mem, SI_SUB_KMEM, SI_ORDER_ANY, scmeminit, NULL);

static int
scdevtounit(struct tty *tp)
{
    int vty = SC_VTY(tp);

    if (vty == SC_CONSOLECTL)
	return ((sc_console != NULL) ? sc_console->sc->unit : -1);
    else if ((vty < 0) || (vty >= MAXCONS*sc_max_unit()))
	return -1;
    else
	return vty/MAXCONS;
}

static int
sctty_open(struct tty *tp)
{
    int unit = scdevtounit(tp);
    sc_softc_t *sc;
    scr_stat *scp;
#ifndef __sparc64__
    keyarg_t key;
#endif

    DPRINTF(5, ("scopen: dev:%s, unit:%d, vty:%d\n",
		devtoname(tp->t_dev), unit, SC_VTY(tp)));

    sc = sc_get_softc(unit, (sc_console_unit == unit) ? SC_KERNEL_CONSOLE : 0);
    if (sc == NULL)
	return ENXIO;

    if (!tty_opened(tp)) {
        /* Use the current setting of the <-- key as default VERASE. */  
        /* If the Delete key is preferable, an stty is necessary     */
#ifndef __sparc64__
	if (sc->kbd != NULL) {
	    key.keynum = KEYCODE_BS;
	    (void)kbdd_ioctl(sc->kbd, GIO_KEYMAPENT, (caddr_t)&key);
            tp->t_termios.c_cc[VERASE] = key.key.map[0];
	}
#endif
    }

    scp = sc_get_stat(tp);
    if (scp == NULL) {
	scp = SC_STAT(tp) = alloc_scp(sc, SC_VTY(tp));
	if (ISGRAPHSC(scp))
	    sc_set_pixel_mode(scp, NULL, 0, 0, 16, 8);
    }
    if (!tp->t_winsize.ws_col && !tp->t_winsize.ws_row) {
	tp->t_winsize.ws_col = scp->xsize;
	tp->t_winsize.ws_row = scp->ysize;
    }

    return (0);
}

static void
sctty_close(struct tty *tp)
{
    scr_stat *scp;
    int s;

    if (SC_VTY(tp) != SC_CONSOLECTL) {
	scp = sc_get_stat(tp);
	/* were we in the middle of the VT switching process? */
	DPRINTF(5, ("sc%d: scclose(), ", scp->sc->unit));
	s = spltty();
	if ((scp == scp->sc->cur_scp) && (scp->sc->unit == sc_console_unit))
	    cnavailable(sc_consptr, TRUE);
	if (finish_vt_rel(scp, TRUE, &s) == 0)	/* force release */
	    DPRINTF(5, ("reset WAIT_REL, "));
	if (finish_vt_acq(scp) == 0)		/* force acknowledge */
	    DPRINTF(5, ("reset WAIT_ACQ, "));
#ifdef not_yet_done
	if (scp == &main_console) {
	    scp->pid = 0;
	    scp->proc = NULL;
	    scp->smode.mode = VT_AUTO;
	}
	else {
	    sc_vtb_destroy(&scp->vtb);
#ifndef __sparc64__
	    sc_vtb_destroy(&scp->scr);
#endif
	    sc_free_history_buffer(scp, scp->ysize);
	    SC_STAT(tp) = NULL;
	    free(scp, M_DEVBUF);
	}
#else
	scp->pid = 0;
	scp->proc = NULL;
	scp->smode.mode = VT_AUTO;
#endif
	scp->kbd_mode = K_XLATE;
	if (scp == scp->sc->cur_scp)
	    (void)kbdd_ioctl(scp->sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
	DPRINTF(5, ("done.\n"));
    }
}

#if 0 /* XXX mpsafetty: fix screensaver. What about outwakeup? */
static int
scread(struct cdev *dev, struct uio *uio, int flag)
{
    if (!sc_saver_keyb_only)
	sc_touch_scrn_saver();
    return ttyread(dev, uio, flag);
}
#endif

static int
sckbdevent(keyboard_t *thiskbd, int event, void *arg)
{
    sc_softc_t *sc;
    struct tty *cur_tty;
    int c, error = 0; 
    size_t len;
    const u_char *cp;

    sc = (sc_softc_t *)arg;
    /* assert(thiskbd == sc->kbd) */

    mtx_lock(&Giant);

    switch (event) {
    case KBDIO_KEYINPUT:
	break;
    case KBDIO_UNLOADING:
	sc->kbd = NULL;
	sc->keyboard = -1;
	kbd_release(thiskbd, (void *)&sc->keyboard);
	goto done;
    default:
	error = EINVAL;
	goto done;
    }

    /* 
     * Loop while there is still input to get from the keyboard.
     * I don't think this is nessesary, and it doesn't fix
     * the Xaccel-2.1 keyboard hang, but it can't hurt.		XXX
     */
    while ((c = scgetc(sc, SCGETC_NONBLOCK, NULL)) != NOKEY) {

	cur_tty = SC_DEV(sc, sc->cur_scp->index);
	if (!tty_opened_ns(cur_tty))
	    continue;

	if ((*sc->cur_scp->tsw->te_input)(sc->cur_scp, c, cur_tty))
	    continue;

	switch (KEYFLAGS(c)) {
	case 0x0000: /* normal key */
	    ttydisc_rint(cur_tty, KEYCHAR(c), 0);
	    break;
	case FKEY:  /* function key, return string */
	    cp = (*sc->cur_scp->tsw->te_fkeystr)(sc->cur_scp, c);
	    if (cp != NULL) {
	    	ttydisc_rint_simple(cur_tty, cp, strlen(cp));
		break;
	    }
	    cp = kbdd_get_fkeystr(thiskbd, KEYCHAR(c), &len);
	    if (cp != NULL)
	    	ttydisc_rint_simple(cur_tty, cp, len);
	    break;
	case MKEY:  /* meta is active, prepend ESC */
	    ttydisc_rint(cur_tty, 0x1b, 0);
	    ttydisc_rint(cur_tty, KEYCHAR(c), 0);
	    break;
	case BKEY:  /* backtab fixed sequence (esc [ Z) */
	    ttydisc_rint_simple(cur_tty, "\x1B[Z", 3);
	    break;
	}

	ttydisc_rint_done(cur_tty);
    }

    sc->cur_scp->status |= MOUSE_HIDDEN;

done:
    mtx_unlock(&Giant);
    return (error);
}

static int
sctty_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
    int error;
    int i;
    struct cursor_attr *cap;
    sc_softc_t *sc;
    scr_stat *scp;
    int s;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    int ival;
#endif

    /* If there is a user_ioctl function call that first */
    if (sc_user_ioctl) {
	error = (*sc_user_ioctl)(tp, cmd, data, td);
	if (error != ENOIOCTL)
	    return error;
    }

    error = sc_vid_ioctl(tp, cmd, data, td);
    if (error != ENOIOCTL)
	return error;

#ifndef SC_NO_HISTORY
    error = sc_hist_ioctl(tp, cmd, data, td);
    if (error != ENOIOCTL)
	return error;
#endif

#ifndef SC_NO_SYSMOUSE
    error = sc_mouse_ioctl(tp, cmd, data, td);
    if (error != ENOIOCTL)
	return error;
#endif

    scp = sc_get_stat(tp);
    /* assert(scp != NULL) */
    /* scp is sc_console, if SC_VTY(dev) == SC_CONSOLECTL. */
    sc = scp->sc;

    if (scp->tsw) {
	error = (*scp->tsw->te_ioctl)(scp, tp, cmd, data, td);
	if (error != ENOIOCTL)
	    return error;
    }

    switch (cmd) {  		/* process console hardware related ioctl's */

    case GIO_ATTR:      	/* get current attributes */
	/* this ioctl is not processed here, but in the terminal emulator */
	return ENOTTY;

    case GIO_COLOR:     	/* is this a color console ? */
	*(int *)data = (sc->adp->va_flags & V_ADP_COLOR) ? 1 : 0;
	return 0;

    case CONS_BLANKTIME:    	/* set screen saver timeout (0 = no saver) */
	if (*(int *)data < 0 || *(int *)data > MAX_BLANKTIME)
            return EINVAL;
	s = spltty();
	scrn_blank_time = *(int *)data;
	run_scrn_saver = (scrn_blank_time != 0);
	splx(s);
	return 0;

    case CONS_CURSORTYPE:   	/* set cursor type (old interface + HIDDEN) */
	s = spltty();
	*(int *)data &= CONS_CURSOR_ATTRS;
	sc_change_cursor_shape(scp, *(int *)data, -1, -1);
	splx(s);
	return 0;

    case CONS_GETCURSORSHAPE:   /* get cursor shape (new interface) */
	switch (((int *)data)[0] & (CONS_DEFAULT_CURSOR | CONS_LOCAL_CURSOR)) {
	case 0:
	    cap = &sc->curs_attr;
	    break;
	case CONS_LOCAL_CURSOR:
	    cap = &scp->base_curs_attr;
	    break;
	case CONS_DEFAULT_CURSOR:
	    cap = &sc->dflt_curs_attr;
	    break;
	case CONS_DEFAULT_CURSOR | CONS_LOCAL_CURSOR:
	    cap = &scp->dflt_curs_attr;
	    break;
	}
	if (((int *)data)[0] & CONS_CHARCURSOR_COLORS) {
	    ((int *)data)[1] = cap->bg[0];
	    ((int *)data)[2] = cap->bg[1];
	} else if (((int *)data)[0] & CONS_MOUSECURSOR_COLORS) {
	    ((int *)data)[1] = cap->mouse_ba;
	    ((int *)data)[2] = cap->mouse_ia;
	} else {
	    ((int *)data)[1] = cap->base;
	    ((int *)data)[2] = cap->height;
	}
	((int *)data)[0] = cap->flags;
	return 0;

    case CONS_SETCURSORSHAPE:   /* set cursor shape (new interface) */
	s = spltty();
	sc_change_cursor_shape(scp, ((int *)data)[0],
	    ((int *)data)[1], ((int *)data)[2]);
	splx(s);
	return 0;

    case CONS_BELLTYPE: 	/* set bell type sound/visual */
	if ((*(int *)data) & CONS_VISUAL_BELL)
	    sc->flags |= SC_VISUAL_BELL;
	else
	    sc->flags &= ~SC_VISUAL_BELL;
	if ((*(int *)data) & CONS_QUIET_BELL)
	    sc->flags |= SC_QUIET_BELL;
	else
	    sc->flags &= ~SC_QUIET_BELL;
	return 0;

    case CONS_GETINFO:  	/* get current (virtual) console info */
    {
	vid_info_t *ptr = (vid_info_t*)data;
	if (ptr->size == sizeof(struct vid_info)) {
	    ptr->m_num = sc->cur_scp->index;
	    ptr->font_size = scp->font_size;
	    ptr->mv_col = scp->xpos;
	    ptr->mv_row = scp->ypos;
	    ptr->mv_csz = scp->xsize;
	    ptr->mv_rsz = scp->ysize;
	    ptr->mv_hsz = (scp->history != NULL) ? scp->history->vtb_rows : 0;
	    /*
	     * The following fields are filled by the terminal emulator. XXX
	     *
	     * ptr->mv_norm.fore
	     * ptr->mv_norm.back
	     * ptr->mv_rev.fore
	     * ptr->mv_rev.back
	     */
	    ptr->mv_grfc.fore = 0;      /* not supported */
	    ptr->mv_grfc.back = 0;      /* not supported */
	    ptr->mv_ovscan = scp->border;
	    if (scp == sc->cur_scp)
		save_kbd_state(scp);
	    ptr->mk_keylock = scp->status & LOCK_MASK;
	    return 0;
	}
	return EINVAL;
    }

    case CONS_GETVERS:  	/* get version number */
	*(int*)data = 0x200;    /* version 2.0 */
	return 0;

    case CONS_IDLE:		/* see if the screen has been idle */
	/*
	 * When the screen is in the GRAPHICS_MODE or UNKNOWN_MODE,
	 * the user process may have been writing something on the
	 * screen and syscons is not aware of it. Declare the screen
	 * is NOT idle if it is in one of these modes. But there is
	 * an exception to it; if a screen saver is running in the 
	 * graphics mode in the current screen, we should say that the
	 * screen has been idle.
	 */
	*(int *)data = (sc->flags & SC_SCRN_IDLE)
		       && (!ISGRAPHSC(sc->cur_scp)
			   || (sc->cur_scp->status & SAVER_RUNNING));
	return 0;

    case CONS_SAVERMODE:	/* set saver mode */
	switch(*(int *)data) {
	case CONS_NO_SAVER:
	case CONS_USR_SAVER:
	    /* if a LKM screen saver is running, stop it first. */
	    scsplash_stick(FALSE);
	    saver_mode = *(int *)data;
	    s = spltty();
#ifdef DEV_SPLASH
	    if ((error = wait_scrn_saver_stop(NULL))) {
		splx(s);
		return error;
	    }
#endif
	    run_scrn_saver = TRUE;
	    if (saver_mode == CONS_USR_SAVER)
		scp->status |= SAVER_RUNNING;
	    else
		scp->status &= ~SAVER_RUNNING;
	    scsplash_stick(TRUE);
	    splx(s);
	    break;
	case CONS_LKM_SAVER:
	    s = spltty();
	    if ((saver_mode == CONS_USR_SAVER) && (scp->status & SAVER_RUNNING))
		scp->status &= ~SAVER_RUNNING;
	    saver_mode = *(int *)data;
	    splx(s);
	    break;
	default:
	    return EINVAL;
	}
	return 0;

    case CONS_SAVERSTART:	/* immediately start/stop the screen saver */
	/*
	 * Note that this ioctl does not guarantee the screen saver 
	 * actually starts or stops. It merely attempts to do so...
	 */
	s = spltty();
	run_scrn_saver = (*(int *)data != 0);
	if (run_scrn_saver)
	    sc->scrn_time_stamp -= scrn_blank_time;
	splx(s);
	return 0;

    case CONS_SCRSHOT:		/* get a screen shot */
    {
	int retval, hist_rsz;
	size_t lsize, csize;
	vm_offset_t frbp, hstp;
	unsigned lnum;
	scrshot_t *ptr = (scrshot_t *)data;
	void *outp = ptr->buf;

	if (ptr->x < 0 || ptr->y < 0 || ptr->xsize < 0 || ptr->ysize < 0)
		return EINVAL;
	s = spltty();
	if (ISGRAPHSC(scp)) {
	    splx(s);
	    return EOPNOTSUPP;
	}
	hist_rsz = (scp->history != NULL) ? scp->history->vtb_rows : 0;
	if (((u_int)ptr->x + ptr->xsize) > scp->xsize ||
	    ((u_int)ptr->y + ptr->ysize) > (scp->ysize + hist_rsz)) {
	    splx(s);
	    return EINVAL;
	}

	lsize = scp->xsize * sizeof(u_int16_t);
	csize = ptr->xsize * sizeof(u_int16_t);
	/* Pointer to the last line of framebuffer */
	frbp = scp->vtb.vtb_buffer + scp->ysize * lsize + ptr->x *
	       sizeof(u_int16_t);
	/* Pointer to the last line of target buffer */
	outp = (char *)outp + ptr->ysize * csize;
	/* Pointer to the last line of history buffer */
	if (scp->history != NULL)
	    hstp = scp->history->vtb_buffer + sc_vtb_tail(scp->history) *
		sizeof(u_int16_t) + ptr->x * sizeof(u_int16_t);
	else
	    hstp = 0;

	retval = 0;
	for (lnum = 0; lnum < (ptr->y + ptr->ysize); lnum++) {
	    if (lnum < scp->ysize) {
		frbp -= lsize;
	    } else {
		hstp -= lsize;
		if (hstp < scp->history->vtb_buffer)
		    hstp += scp->history->vtb_rows * lsize;
		frbp = hstp;
	    }
	    if (lnum < ptr->y)
		continue;
	    outp = (char *)outp - csize;
	    retval = copyout((void *)frbp, outp, csize);
	    if (retval != 0)
		break;
	}
	splx(s);
	return retval;
    }

    case VT_SETMODE:    	/* set screen switcher mode */
    {
	struct vt_mode *mode;
	struct proc *p1;

	mode = (struct vt_mode *)data;
	DPRINTF(5, ("%s%d: VT_SETMODE ", SC_DRIVER_NAME, sc->unit));
	if (scp->smode.mode == VT_PROCESS) {
	    p1 = pfind(scp->pid);
    	    if (scp->proc == p1 && scp->proc != td->td_proc) {
		if (p1)
		    PROC_UNLOCK(p1);
		DPRINTF(5, ("error EPERM\n"));
		return EPERM;
	    }
	    if (p1)
		PROC_UNLOCK(p1);
	}
	s = spltty();
	if (mode->mode == VT_AUTO) {
	    scp->smode.mode = VT_AUTO;
	    scp->proc = NULL;
	    scp->pid = 0;
	    DPRINTF(5, ("VT_AUTO, "));
	    if ((scp == sc->cur_scp) && (sc->unit == sc_console_unit))
		cnavailable(sc_consptr, TRUE);
	    /* were we in the middle of the vty switching process? */
	    if (finish_vt_rel(scp, TRUE, &s) == 0)
		DPRINTF(5, ("reset WAIT_REL, "));
	    if (finish_vt_acq(scp) == 0)
		DPRINTF(5, ("reset WAIT_ACQ, "));
	} else {
	    if (!ISSIGVALID(mode->relsig) || !ISSIGVALID(mode->acqsig)
		|| !ISSIGVALID(mode->frsig)) {
		splx(s);
		DPRINTF(5, ("error EINVAL\n"));
		return EINVAL;
	    }
	    DPRINTF(5, ("VT_PROCESS %d, ", td->td_proc->p_pid));
	    bcopy(data, &scp->smode, sizeof(struct vt_mode));
	    scp->proc = td->td_proc;
	    scp->pid = scp->proc->p_pid;
	    if ((scp == sc->cur_scp) && (sc->unit == sc_console_unit))
		cnavailable(sc_consptr, FALSE);
	}
	splx(s);
	DPRINTF(5, ("\n"));
	return 0;
    }

    case VT_GETMODE:    	/* get screen switcher mode */
	bcopy(&scp->smode, data, sizeof(struct vt_mode));
	return 0;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('v', 4):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case VT_RELDISP:    	/* screen switcher ioctl */
	s = spltty();
	/*
	 * This must be the current vty which is in the VT_PROCESS
	 * switching mode...
	 */
	if ((scp != sc->cur_scp) || (scp->smode.mode != VT_PROCESS)) {
	    splx(s);
	    return EINVAL;
	}
	/* ...and this process is controlling it. */
	if (scp->proc != td->td_proc) {
	    splx(s);
	    return EPERM;
	}
	error = EINVAL;
	switch(*(int *)data) {
	case VT_FALSE:  	/* user refuses to release screen, abort */
	    if ((error = finish_vt_rel(scp, FALSE, &s)) == 0)
		DPRINTF(5, ("%s%d: VT_FALSE\n", SC_DRIVER_NAME, sc->unit));
	    break;
	case VT_TRUE:   	/* user has released screen, go on */
	    if ((error = finish_vt_rel(scp, TRUE, &s)) == 0)
		DPRINTF(5, ("%s%d: VT_TRUE\n", SC_DRIVER_NAME, sc->unit));
	    break;
	case VT_ACKACQ: 	/* acquire acknowledged, switch completed */
	    if ((error = finish_vt_acq(scp)) == 0)
		DPRINTF(5, ("%s%d: VT_ACKACQ\n", SC_DRIVER_NAME, sc->unit));
	    break;
	default:
	    break;
	}
	splx(s);
	return error;

    case VT_OPENQRY:    	/* return free virtual console */
	for (i = sc->first_vty; i < sc->first_vty + sc->vtys; i++) {
	    tp = SC_DEV(sc, i);
	    if (!tty_opened_ns(tp)) {
		*(int *)data = i + 1;
		return 0;
	    }
	}
	return EINVAL;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('v', 5):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case VT_ACTIVATE:   	/* switch to screen *data */
	i = (*(int *)data == 0) ? scp->index : (*(int *)data - 1);
	s = spltty();
	error = sc_clean_up(sc->cur_scp);
	splx(s);
	if (error)
	    return error;
	error = sc_switch_scr(sc, i);
	return (error);

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('v', 6):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case VT_WAITACTIVE: 	/* wait for switch to occur */
	i = (*(int *)data == 0) ? scp->index : (*(int *)data - 1);
	if ((i < sc->first_vty) || (i >= sc->first_vty + sc->vtys))
	    return EINVAL;
	if (i == sc->cur_scp->index)
	    return 0;
	error = tsleep(VTY_WCHAN(sc, i), (PZERO + 1) | PCATCH, "waitvt", 0);
	return error;

    case VT_GETACTIVE:		/* get active vty # */
	*(int *)data = sc->cur_scp->index + 1;
	return 0;

    case VT_GETINDEX:		/* get this vty # */
	*(int *)data = scp->index + 1;
	return 0;

    case VT_LOCKSWITCH:		/* prevent vty switching */
	if ((*(int *)data) & 0x01)
	    sc->flags |= SC_SCRN_VTYLOCK;
	else
	    sc->flags &= ~SC_SCRN_VTYLOCK;
	return 0;

    case KDENABIO:      	/* allow io operations */
	error = priv_check(td, PRIV_IO);
	if (error != 0)
	    return error;
	error = securelevel_gt(td->td_ucred, 0);
	if (error != 0)
		return error;
#ifdef __i386__
	td->td_frame->tf_eflags |= PSL_IOPL;
#elif defined(__amd64__)
	td->td_frame->tf_rflags |= PSL_IOPL;
#endif
	return 0;

    case KDDISABIO:     	/* disallow io operations (default) */
#ifdef __i386__
	td->td_frame->tf_eflags &= ~PSL_IOPL;
#elif defined(__amd64__)
	td->td_frame->tf_rflags &= ~PSL_IOPL;
#endif
	return 0;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 20):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSKBSTATE:    	/* set keyboard state (locks) */
	if (*(int *)data & ~LOCK_MASK)
	    return EINVAL;
	scp->status &= ~LOCK_MASK;
	scp->status |= *(int *)data;
	if (scp == sc->cur_scp)
	    update_kbd_state(scp, scp->status, LOCK_MASK);
	return 0;

    case KDGKBSTATE:    	/* get keyboard state (locks) */
	if (scp == sc->cur_scp)
	    save_kbd_state(scp);
	*(int *)data = scp->status & LOCK_MASK;
	return 0;

    case KDGETREPEAT:      	/* get keyboard repeat & delay rates */
    case KDSETREPEAT:      	/* set keyboard repeat & delay rates (new) */
	error = kbdd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	return error;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 67):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSETRAD:      	/* set keyboard repeat & delay rates (old) */
	if (*(int *)data & ~0x7f)
	    return EINVAL;
	error = kbdd_ioctl(sc->kbd, KDSETRAD, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	return error;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 7):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSKBMODE:     	/* set keyboard mode */
	switch (*(int *)data) {
	case K_XLATE:   	/* switch to XLT ascii mode */
	case K_RAW: 		/* switch to RAW scancode mode */
	case K_CODE: 		/* switch to CODE mode */
	    scp->kbd_mode = *(int *)data;
	    if (scp == sc->cur_scp)
		(void)kbdd_ioctl(sc->kbd, KDSKBMODE, data);
	    return 0;
	default:
	    return EINVAL;
	}
	/* NOT REACHED */

    case KDGKBMODE:     	/* get keyboard mode */
	*(int *)data = scp->kbd_mode;
	return 0;

    case KDGKBINFO:
	error = kbdd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	return error;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 8):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDMKTONE:      	/* sound the bell */
	if (*(int*)data)
	    sc_bell(scp, (*(int*)data)&0xffff,
		    (((*(int*)data)>>16)&0xffff)*hz/1000);
	else
	    sc_bell(scp, scp->bell_pitch, scp->bell_duration);
	return 0;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 63):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KIOCSOUND:     	/* make tone (*data) hz */
	if (scp == sc->cur_scp) {
	    if (*(int *)data)
		return sc_tone(*(int *)data);
	    else
		return sc_tone(0);
	}
	return 0;

    case KDGKBTYPE:     	/* get keyboard type */
	error = kbdd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL) {
	    /* always return something? XXX */
	    *(int *)data = 0;
	}
	return 0;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 66):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSETLED:      	/* set keyboard LED status */
	if (*(int *)data & ~LED_MASK)	/* FIXME: LOCK_MASK? */
	    return EINVAL;
	scp->status &= ~LED_MASK;
	scp->status |= *(int *)data;
	if (scp == sc->cur_scp)
	    update_kbd_leds(scp, scp->status);
	return 0;

    case KDGETLED:      	/* get keyboard LED status */
	if (scp == sc->cur_scp)
	    save_kbd_state(scp);
	*(int *)data = scp->status & LED_MASK;
	return 0;

    case KBADDKBD:		/* add/remove keyboard to/from mux */
    case KBRELKBD:
	error = kbdd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	return error;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('c', 110):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case CONS_SETKBD: 		/* set the new keyboard */
	{
	    keyboard_t *newkbd;

	    s = spltty();
	    newkbd = kbd_get_keyboard(*(int *)data);
	    if (newkbd == NULL) {
		splx(s);
		return EINVAL;
	    }
	    error = 0;
	    if (sc->kbd != newkbd) {
		i = kbd_allocate(newkbd->kb_name, newkbd->kb_unit,
				 (void *)&sc->keyboard, sckbdevent, sc);
		/* i == newkbd->kb_index */
		if (i >= 0) {
		    if (sc->kbd != NULL) {
			save_kbd_state(sc->cur_scp);
			kbd_release(sc->kbd, (void *)&sc->keyboard);
		    }
		    sc->kbd = kbd_get_keyboard(i); /* sc->kbd == newkbd */
		    sc->keyboard = i;
		    (void)kbdd_ioctl(sc->kbd, KDSKBMODE,
			      (caddr_t)&sc->cur_scp->kbd_mode);
		    update_kbd_state(sc->cur_scp, sc->cur_scp->status,
				     LOCK_MASK);
		} else {
		    error = EPERM;	/* XXX */
		}
	    }
	    splx(s);
	    return error;
	}

    case CONS_RELKBD: 		/* release the current keyboard */
	s = spltty();
	error = 0;
	if (sc->kbd != NULL) {
	    save_kbd_state(sc->cur_scp);
	    error = kbd_release(sc->kbd, (void *)&sc->keyboard);
	    if (error == 0) {
		sc->kbd = NULL;
		sc->keyboard = -1;
	    }
	}
	splx(s);
	return error;

    case CONS_GETTERM:		/* get the current terminal emulator info */
	{
	    sc_term_sw_t *sw;

	    if (((term_info_t *)data)->ti_index == 0) {
		sw = scp->tsw;
	    } else {
		sw = sc_term_match_by_number(((term_info_t *)data)->ti_index);
	    }
	    if (sw != NULL) {
		strncpy(((term_info_t *)data)->ti_name, sw->te_name, 
			sizeof(((term_info_t *)data)->ti_name));
		strncpy(((term_info_t *)data)->ti_desc, sw->te_desc, 
			sizeof(((term_info_t *)data)->ti_desc));
		((term_info_t *)data)->ti_flags = 0;
		return 0;
	    } else {
		((term_info_t *)data)->ti_name[0] = '\0';
		((term_info_t *)data)->ti_desc[0] = '\0';
		((term_info_t *)data)->ti_flags = 0;
		return EINVAL;
	    }
	}

    case CONS_SETTERM:		/* set the current terminal emulator */
	s = spltty();
	error = sc_init_emulator(scp, ((term_info_t *)data)->ti_name);
	/* FIXME: what if scp == sc_console! XXX */
	splx(s);
	return error;

    case GIO_SCRNMAP:   	/* get output translation table */
	bcopy(&sc->scr_map, data, sizeof(sc->scr_map));
	return 0;

    case PIO_SCRNMAP:   	/* set output translation table */
	bcopy(data, &sc->scr_map, sizeof(sc->scr_map));
	for (i=0; i<sizeof(sc->scr_map); i++) {
	    sc->scr_rmap[sc->scr_map[i]] = i;
	}
	return 0;

    case GIO_KEYMAP:		/* get keyboard translation table */
    case PIO_KEYMAP:		/* set keyboard translation table */
    case OGIO_KEYMAP:		/* get keyboard translation table (compat) */
    case OPIO_KEYMAP:		/* set keyboard translation table (compat) */
    case GIO_DEADKEYMAP:	/* get accent key translation table */
    case PIO_DEADKEYMAP:	/* set accent key translation table */
    case GETFKEY:		/* get function key string */
    case SETFKEY:		/* set function key string */
	error = kbdd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	return error;

#ifndef SC_NO_FONT_LOADING

    case PIO_FONT8x8:   	/* set 8x8 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	bcopy(data, sc->font_8, 8*256);
	sc->fonts_loaded |= FONT_8;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x8.
	 */
	if (ISTEXTSC(sc->cur_scp) && (sc->cur_scp->font_size < 14))
	    sc_load_font(sc->cur_scp, 0, 8, 8, sc->font_8, 0, 256);
	return 0;

    case GIO_FONT8x8:   	/* get 8x8 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	if (sc->fonts_loaded & FONT_8) {
	    bcopy(sc->font_8, data, 8*256);
	    return 0;
	}
	else
	    return ENXIO;

    case PIO_FONT8x14:  	/* set 8x14 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	bcopy(data, sc->font_14, 14*256);
	sc->fonts_loaded |= FONT_14;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x14.
	 */
	if (ISTEXTSC(sc->cur_scp)
	    && (sc->cur_scp->font_size >= 14)
	    && (sc->cur_scp->font_size < 16))
	    sc_load_font(sc->cur_scp, 0, 14, 8, sc->font_14, 0, 256);
	return 0;

    case GIO_FONT8x14:  	/* get 8x14 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	if (sc->fonts_loaded & FONT_14) {
	    bcopy(sc->font_14, data, 14*256);
	    return 0;
	}
	else
	    return ENXIO;

    case PIO_FONT8x16:  	/* set 8x16 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	bcopy(data, sc->font_16, 16*256);
	sc->fonts_loaded |= FONT_16;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x16.
	 */
	if (ISTEXTSC(sc->cur_scp) && (sc->cur_scp->font_size >= 16))
	    sc_load_font(sc->cur_scp, 0, 16, 8, sc->font_16, 0, 256);
	return 0;

    case GIO_FONT8x16:  	/* get 8x16 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags))
	    return ENXIO;
	if (sc->fonts_loaded & FONT_16) {
	    bcopy(sc->font_16, data, 16*256);
	    return 0;
	}
	else
	    return ENXIO;

#endif /* SC_NO_FONT_LOADING */

    default:
	break;
    }

    return (ENOIOCTL);
}

static int
consolectl_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{

	return sctty_ioctl(dev->si_drv1, cmd, data, td);
}

static int
consolectl_close(struct cdev *dev, int flags, int mode, struct thread *td)
{
#ifndef SC_NO_SYSMOUSE
	mouse_info_t info;
	memset(&info, 0, sizeof(info));
	info.operation = MOUSE_ACTION;

	/*
	 * Make sure all buttons are released when moused and other
	 * console daemons exit, so that no buttons are left pressed.
	 */
	(void) sctty_ioctl(dev->si_drv1, CONS_MOUSECTL, (caddr_t)&info, td);
#endif
	return (0);
}

static void
sc_cnprobe(struct consdev *cp)
{
    int unit;
    int flags;

    if (!vty_enabled(VTY_SC)) {
	cp->cn_pri = CN_DEAD;
	return;
    }

    cp->cn_pri = sc_get_cons_priority(&unit, &flags);

    /* a video card is always required */
    if (!scvidprobe(unit, flags, TRUE))
	cp->cn_pri = CN_DEAD;

    /* syscons will become console even when there is no keyboard */
    sckbdprobe(unit, flags, TRUE);

    if (cp->cn_pri == CN_DEAD)
	return;

    /* initialize required fields */
    strcpy(cp->cn_name, "ttyv0");
}

static void
sc_cninit(struct consdev *cp)
{
    int unit;
    int flags;

    sc_get_cons_priority(&unit, &flags);
    scinit(unit, flags | SC_KERNEL_CONSOLE);
    sc_console_unit = unit;
    sc_console = sc_get_stat(sc_get_softc(unit, SC_KERNEL_CONSOLE)->dev[0]);
    sc_consptr = cp;
}

static void
sc_cnterm(struct consdev *cp)
{
    void *ts;
    int i;

    /* we are not the kernel console any more, release everything */

    if (sc_console_unit < 0)
	return;			/* shouldn't happen */

#if 0 /* XXX */
    sc_clear_screen(sc_console);
    sccnupdate(sc_console);
#endif

    if (sc_ktsw != NULL) {
	for (i = 0; i <= mp_maxid; i++) {
	    ts = sc_kts[i];
	    sc_kts[i] = NULL;
	    (*sc_ktsw->te_term)(sc_console, &ts);
	    free(ts, M_DEVBUF);
	}
	sc_ktsw = NULL;
    }
    scterm(sc_console_unit, SC_KERNEL_CONSOLE);
    sc_console_unit = -1;
    sc_console = NULL;
}

static void sccnclose(sc_softc_t *sc, struct sc_cnstate *sp);
static int sc_cngetc_locked(struct sc_cnstate *sp);
static void sccnkbdlock(sc_softc_t *sc, struct sc_cnstate *sp);
static void sccnkbdunlock(sc_softc_t *sc, struct sc_cnstate *sp);
static void sccnopen(sc_softc_t *sc, struct sc_cnstate *sp, int flags);
static void sccnscrlock(sc_softc_t *sc, struct sc_cnstate *sp);
static void sccnscrunlock(sc_softc_t *sc, struct sc_cnstate *sp);

static void
sccnkbdlock(sc_softc_t *sc, struct sc_cnstate *sp)
{
    /*
     * Locking method: hope for the best.
     * The keyboard is supposed to be Giant locked.  We can't handle that
     * in general.  The kdb_active case here is not safe, and we will
     * proceed without the lock in all cases.
     */
    sp->kbd_locked = !kdb_active && mtx_trylock(&Giant);
}

static void
sccnkbdunlock(sc_softc_t *sc, struct sc_cnstate *sp)
{
    if (sp->kbd_locked)
	mtx_unlock(&Giant);
    sp->kbd_locked = FALSE;
}

static void
sccnscrlock(sc_softc_t *sc, struct sc_cnstate *sp)
{
    int retries;

    /**
     * Locking method:
     * - if kdb_active and video_mtx is not owned by anyone, then lock
     *   by kdb remaining active
     * - if !kdb_active, try to acquire video_mtx without blocking or
     *   recursing; if we get it then it works normally.
     * Note that video_mtx is especially unusable if we already own it,
     * since then it is protecting something and syscons is not reentrant
     * enough to ignore the protection even in the kdb_active case.
     */
    if (kdb_active) {
	sp->kdb_locked = sc->video_mtx.mtx_lock == MTX_UNOWNED ||
			 SCHEDULER_STOPPED();
	sp->mtx_locked = FALSE;
    } else {
	sp->kdb_locked = FALSE;
	for (retries = 0; retries < 1000; retries++) {
	    sp->mtx_locked = mtx_trylock_spin_flags(&sc->video_mtx,
						    MTX_QUIET) != 0;
	    if (SCHEDULER_STOPPED()) {
		sp->kdb_locked = TRUE;
		sp->mtx_locked = FALSE;
		break;
	    }
	    if (sp->mtx_locked)
		break;
	    DELAY(1);
	}
    }
}

static void
sccnscrunlock(sc_softc_t *sc, struct sc_cnstate *sp)
{
    if (sp->mtx_locked)
	mtx_unlock_spin(&sc->video_mtx);
    sp->mtx_locked = sp->kdb_locked = FALSE;
}

static void
sccnopen(sc_softc_t *sc, struct sc_cnstate *sp, int flags)
{
    int kbd_mode;

    /* assert(sc_console_unit >= 0) */

    sp->kbd_opened = FALSE;
    sp->scr_opened = FALSE;
    sp->kbd_locked = FALSE;

    /* Opening the keyboard is optional. */
    if (!(flags & 1) || sc->kbd == NULL)
	goto over_keyboard;

    sccnkbdlock(sc, sp);

    /*
     * Make sure the keyboard is accessible even when the kbd device
     * driver is disabled.
     */
    kbdd_enable(sc->kbd);

    /* Switch the keyboard to console mode (K_XLATE, polled) on all scp's. */
    kbd_mode = K_XLATE;
    (void)kbdd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&kbd_mode);
    sc->kbd_open_level++;
    kbdd_poll(sc->kbd, TRUE);

    sp->kbd_opened = TRUE;
over_keyboard: ;

    /* The screen is opened iff locking it succeeds. */
    sccnscrlock(sc, sp);
    if (!sp->kdb_locked && !sp->mtx_locked)
	return;
    sp->scr_opened = TRUE;

    /* The screen switch is optional. */
    if (!(flags & 2))
	return;

    /* try to switch to the kernel console screen */
    if (!cold &&
	sc->cur_scp->index != sc_console->index &&
	sc->cur_scp->smode.mode == VT_AUTO &&
	sc_console->smode.mode == VT_AUTO)
	    sc_switch_scr(sc, sc_console->index);
}

static void
sccnclose(sc_softc_t *sc, struct sc_cnstate *sp)
{
    sp->scr_opened = FALSE;
    sccnscrunlock(sc, sp);

    if (!sp->kbd_opened)
	return;

    /* Restore keyboard mode (for the current, possibly-changed scp). */
    kbdd_poll(sc->kbd, FALSE);
    if (--sc->kbd_open_level == 0)
	(void)kbdd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&sc->cur_scp->kbd_mode);

    kbdd_disable(sc->kbd);
    sp->kbd_opened = FALSE;
    sccnkbdunlock(sc, sp);
}

/*
 * Grabbing switches the screen and keyboard focus to sc_console and the
 * keyboard mode to (K_XLATE, polled).  Only switching to polled mode is
 * essential (for preventing the interrupt handler from eating input
 * between polls).  Focus is part of the UI, and the other switches are
 * work just was well when they are done on every entry and exit.
 *
 * Screen switches while grabbed are supported, and to maintain focus for
 * this ungrabbing and closing only restore the polling state and then
 * the keyboard mode if on the original screen.
 */

static void
sc_cngrab(struct consdev *cp)
{
    sc_softc_t *sc;
    int lev;

    sc = sc_console->sc;
    lev = atomic_fetchadd_int(&sc->grab_level, 1);
    if (lev >= 0 && lev < 2) {
	sccnopen(sc, &sc->grab_state[lev], 1 | 2);
	sccnscrunlock(sc, &sc->grab_state[lev]);
	sccnkbdunlock(sc, &sc->grab_state[lev]);
    }
}

static void
sc_cnungrab(struct consdev *cp)
{
    sc_softc_t *sc;
    int lev;

    sc = sc_console->sc;
    lev = atomic_load_acq_int(&sc->grab_level) - 1;
    if (lev >= 0 && lev < 2) {
	sccnkbdlock(sc, &sc->grab_state[lev]);
	sccnscrlock(sc, &sc->grab_state[lev]);
	sccnclose(sc, &sc->grab_state[lev]);
    }
    atomic_add_int(&sc->grab_level, -1);
}

static char sc_cnputc_log[0x1000];
static u_int sc_cnputc_loghead;
static u_int sc_cnputc_logtail;

static void
sc_cnputc(struct consdev *cd, int c)
{
    struct sc_cnstate st;
    u_char buf[1];
    scr_stat *scp = sc_console;
    void *oldts, *ts;
    struct sc_term_sw *oldtsw;
#ifndef SC_NO_HISTORY
#if 0
    struct tty *tp;
#endif
#endif /* !SC_NO_HISTORY */
    u_int head;
    int s;

    /* assert(sc_console != NULL) */

    sccnopen(scp->sc, &st, 0);

    /*
     * Log the output.
     *
     * In the unlocked case, the logging is intentionally only
     * perfectly atomic for the indexes.
     */
    head = atomic_fetchadd_int(&sc_cnputc_loghead, 1);
    sc_cnputc_log[head % sizeof(sc_cnputc_log)] = c;

    /*
     * If we couldn't open, do special reentrant output and return to defer
     * normal output.
     */
    if (!st.scr_opened) {
	ec_putc(c);
	return;
    }

#ifndef SC_NO_HISTORY
    if (scp == scp->sc->cur_scp && scp->status & SLKED) {
	scp->status &= ~SLKED;
	update_kbd_state(scp, scp->status, SLKED);
	if (scp->status & BUFFER_SAVED) {
	    if (!sc_hist_restore(scp))
		sc_remove_cutmarking(scp);
	    scp->status &= ~BUFFER_SAVED;
	    scp->status |= CURSOR_ENABLED;
	    sc_draw_cursor_image(scp);
	}
#if 0
	/*
	 * XXX: Now that TTY's have their own locks, we cannot process
	 * any data after disabling scroll lock. cnputs already holds a
	 * spinlock.
	 */
	tp = SC_DEV(scp->sc, scp->index);
	/* XXX "tp" can be NULL */
	tty_lock(tp);
	if (tty_opened(tp))
	    sctty_outwakeup(tp);
	tty_unlock(tp);
#endif
    }
#endif /* !SC_NO_HISTORY */

    /* Play any output still in the log (our char may already be done). */
    while (sc_cnputc_logtail != atomic_load_acq_int(&sc_cnputc_loghead)) {
	buf[0] = sc_cnputc_log[sc_cnputc_logtail++ % sizeof(sc_cnputc_log)];
	if (atomic_load_acq_int(&sc_cnputc_loghead) - sc_cnputc_logtail >=
	    sizeof(sc_cnputc_log))
	    continue;
	/* Console output has a per-CPU "input" state.  Switch for it. */
	ts = sc_kts[curcpu];
	if (ts != NULL) {
	    oldtsw = scp->tsw;
	    oldts = scp->ts;
	    scp->tsw = sc_ktsw;
	    scp->ts = ts;
	    (*scp->tsw->te_sync)(scp);
	} else {
	    /* Only 1 tsw early.  Switch only its attr. */
	    (*scp->tsw->te_default_attr)(scp, sc_kattrtab[curcpu],
					 SC_KERNEL_CONS_REV_ATTR);
	}
	sc_puts(scp, buf, 1);
	if (ts != NULL) {
	    scp->tsw = oldtsw;
	    scp->ts = oldts;
	    (*scp->tsw->te_sync)(scp);
	} else {
	    (*scp->tsw->te_default_attr)(scp, SC_KERNEL_CONS_ATTR,
					 SC_KERNEL_CONS_REV_ATTR);
	}
    }

    s = spltty();	/* block sckbdevent and scrn_timer */
    sccnupdate(scp);
    splx(s);
    sccnclose(scp->sc, &st);
}

static int
sc_cngetc(struct consdev *cd)
{
    struct sc_cnstate st;
    int c, s;

    /* assert(sc_console != NULL) */
    sccnopen(sc_console->sc, &st, 1);
    s = spltty();	/* block sckbdevent and scrn_timer while we poll */
    if (!st.kbd_opened) {
	splx(s);
	sccnclose(sc_console->sc, &st);
	return -1;	/* means no keyboard since we fudged the locking */
    }
    c = sc_cngetc_locked(&st);
    splx(s);
    sccnclose(sc_console->sc, &st);
    return c;
}

static int
sc_cngetc_locked(struct sc_cnstate *sp)
{
    static struct fkeytab fkey;
    static int fkeycp;
    scr_stat *scp;
    const u_char *p;
    int c;

    /* 
     * Stop the screen saver and update the screen if necessary.
     * What if we have been running in the screen saver code... XXX
     */
    if (sp->scr_opened)
	sc_touch_scrn_saver();
    scp = sc_console->sc->cur_scp;	/* XXX */
    if (sp->scr_opened)
	sccnupdate(scp);

    if (fkeycp < fkey.len)
	return fkey.str[fkeycp++];

    c = scgetc(scp->sc, SCGETC_CN | SCGETC_NONBLOCK, sp);

    switch (KEYFLAGS(c)) {
    case 0:	/* normal char */
	return KEYCHAR(c);
    case FKEY:	/* function key */
	p = (*scp->tsw->te_fkeystr)(scp, c);
	if (p != NULL) {
	    fkey.len = strlen(p);
	    bcopy(p, fkey.str, fkey.len);
	    fkeycp = 1;
	    return fkey.str[0];
	}
	p = kbdd_get_fkeystr(scp->sc->kbd, KEYCHAR(c), (size_t *)&fkeycp);
	fkey.len = fkeycp;
	if ((p != NULL) && (fkey.len > 0)) {
	    bcopy(p, fkey.str, fkey.len);
	    fkeycp = 1;
	    return fkey.str[0];
	}
	return c;	/* XXX */
    case NOKEY:
    case ERRKEY:
    default:
	return -1;
    }
    /* NOT REACHED */
}

static void
sccnupdate(scr_stat *scp)
{
    /* this is a cut-down version of scrn_timer()... */

    if (suspend_in_progress || scp->sc->font_loading_in_progress)
	return;

    if (kdb_active || panicstr || shutdown_in_progress) {
	sc_touch_scrn_saver();
    } else if (scp != scp->sc->cur_scp) {
	return;
    }

    if (!run_scrn_saver)
	scp->sc->flags &= ~SC_SCRN_IDLE;
#ifdef DEV_SPLASH
    if ((saver_mode != CONS_LKM_SAVER) || !(scp->sc->flags & SC_SCRN_IDLE))
	if (scp->sc->flags & SC_SCRN_BLANKED)
            stop_scrn_saver(scp->sc, current_saver);
#endif

    if (scp != scp->sc->cur_scp || scp->sc->blink_in_progress
	|| scp->sc->switch_in_progress)
	return;
    /*
     * FIXME: unlike scrn_timer(), we call scrn_update() from here even
     * when write_in_progress is non-zero.  XXX
     */

    if (!ISGRAPHSC(scp) && !(scp->sc->flags & SC_SCRN_BLANKED))
	scrn_update(scp, TRUE);
}

static void
scrn_timer(void *arg)
{
    static time_t kbd_time_stamp = 0;
    sc_softc_t *sc;
    scr_stat *scp;
    int again, rate;

    again = (arg != NULL);
    if (arg != NULL)
	sc = (sc_softc_t *)arg;
    else if (sc_console != NULL)
	sc = sc_console->sc;
    else
	return;

    /* find the vty to update */
    scp = sc->cur_scp;

    /* don't do anything when we are performing some I/O operations */
    if (suspend_in_progress || sc->font_loading_in_progress)
	goto done;

    if ((sc->kbd == NULL) && (sc->config & SC_AUTODETECT_KBD)) {
	/* try to allocate a keyboard automatically */
	if (kbd_time_stamp != time_uptime) {
	    kbd_time_stamp = time_uptime;
	    sc->keyboard = sc_allocate_keyboard(sc, -1);
	    if (sc->keyboard >= 0) {
		sc->kbd = kbd_get_keyboard(sc->keyboard);
		(void)kbdd_ioctl(sc->kbd, KDSKBMODE,
			  (caddr_t)&sc->cur_scp->kbd_mode);
		update_kbd_state(sc->cur_scp, sc->cur_scp->status,
				 LOCK_MASK);
	    }
	}
    }

    /* should we stop the screen saver? */
    if (kdb_active || panicstr || shutdown_in_progress)
	sc_touch_scrn_saver();
    if (run_scrn_saver) {
	if (time_uptime > sc->scrn_time_stamp + scrn_blank_time)
	    sc->flags |= SC_SCRN_IDLE;
	else
	    sc->flags &= ~SC_SCRN_IDLE;
    } else {
	sc->scrn_time_stamp = time_uptime;
	sc->flags &= ~SC_SCRN_IDLE;
	if (scrn_blank_time > 0)
	    run_scrn_saver = TRUE;
    }
#ifdef DEV_SPLASH
    if ((saver_mode != CONS_LKM_SAVER) || !(sc->flags & SC_SCRN_IDLE))
	if (sc->flags & SC_SCRN_BLANKED)
            stop_scrn_saver(sc, current_saver);
#endif

    /* should we just return ? */
    if (sc->blink_in_progress || sc->switch_in_progress
	|| sc->write_in_progress)
	goto done;

    /* Update the screen */
    scp = sc->cur_scp;		/* cur_scp may have changed... */
    if (!ISGRAPHSC(scp) && !(sc->flags & SC_SCRN_BLANKED))
	scrn_update(scp, TRUE);

#ifdef DEV_SPLASH
    /* should we activate the screen saver? */
    if ((saver_mode == CONS_LKM_SAVER) && (sc->flags & SC_SCRN_IDLE))
	if (!ISGRAPHSC(scp) || (sc->flags & SC_SCRN_BLANKED))
	    (*current_saver)(sc, TRUE);
#endif

done:
    if (again) {
	/*
	 * Use reduced "refresh" rate if we are in graphics and that is not a
	 * graphical screen saver.  In such case we just have nothing to do.
	 */
	if (ISGRAPHSC(scp) && !(sc->flags & SC_SCRN_BLANKED))
	    rate = 2;
	else
	    rate = 30;
	callout_reset_sbt(&sc->ctimeout, SBT_1S / rate, 0,
	    scrn_timer, sc, C_PREL(1));
    }
}

static int
and_region(int *s1, int *e1, int s2, int e2)
{
    if (*e1 < s2 || e2 < *s1)
	return FALSE;
    *s1 = imax(*s1, s2);
    *e1 = imin(*e1, e2);
    return TRUE;
}

static void 
scrn_update(scr_stat *scp, int show_cursor)
{
    int start;
    int end;
    int s;
    int e;

    /* assert(scp == scp->sc->cur_scp) */

    SC_VIDEO_LOCK(scp->sc);

#ifndef SC_NO_CUTPASTE
    /* remove the previous mouse pointer image if necessary */
    if (scp->status & MOUSE_VISIBLE) {
	s = scp->mouse_pos;
	e = scp->mouse_pos + scp->xsize + 1;
	if ((scp->status & (MOUSE_MOVED | MOUSE_HIDDEN))
	    || and_region(&s, &e, scp->start, scp->end)
	    || ((scp->status & CURSOR_ENABLED) && 
		(scp->cursor_pos != scp->cursor_oldpos) &&
		(and_region(&s, &e, scp->cursor_pos, scp->cursor_pos)
		 || and_region(&s, &e, scp->cursor_oldpos, scp->cursor_oldpos)))) {
	    sc_remove_mouse_image(scp);
	    if (scp->end >= scp->xsize*scp->ysize)
		scp->end = scp->xsize*scp->ysize - 1;
	}
    }
#endif /* !SC_NO_CUTPASTE */

#if 1
    /* debug: XXX */
    if (scp->end >= scp->xsize*scp->ysize) {
	printf("scrn_update(): scp->end %d > size_of_screen!!\n", scp->end);
	scp->end = scp->xsize*scp->ysize - 1;
    }
    if (scp->start < 0) {
	printf("scrn_update(): scp->start %d < 0\n", scp->start);
	scp->start = 0;
    }
#endif

    /* update screen image */
    if (scp->start <= scp->end)  {
	if (scp->mouse_cut_end >= 0) {
	    /* there is a marked region for cut & paste */
	    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
		start = scp->mouse_cut_start;
		end = scp->mouse_cut_end;
	    } else {
		start = scp->mouse_cut_end;
		end = scp->mouse_cut_start - 1;
	    }
	    s = start;
	    e = end;
	    /* does the cut-mark region overlap with the update region? */
	    if (and_region(&s, &e, scp->start, scp->end)) {
		(*scp->rndr->draw)(scp, s, e - s + 1, TRUE);
		s = 0;
		e = start - 1;
		if (and_region(&s, &e, scp->start, scp->end))
		    (*scp->rndr->draw)(scp, s, e - s + 1, FALSE);
		s = end + 1;
		e = scp->xsize*scp->ysize - 1;
		if (and_region(&s, &e, scp->start, scp->end))
		    (*scp->rndr->draw)(scp, s, e - s + 1, FALSE);
	    } else {
		(*scp->rndr->draw)(scp, scp->start,
				   scp->end - scp->start + 1, FALSE);
	    }
	} else {
	    (*scp->rndr->draw)(scp, scp->start,
			       scp->end - scp->start + 1, FALSE);
	}
    }

    /* we are not to show the cursor and the mouse pointer... */
    if (!show_cursor) {
        scp->end = 0;
        scp->start = scp->xsize*scp->ysize - 1;
	SC_VIDEO_UNLOCK(scp->sc);
	return;
    }

    /* update cursor image */
    if (scp->status & CURSOR_ENABLED) {
	s = scp->start;
	e = scp->end;
        /* did cursor move since last time ? */
        if (scp->cursor_pos != scp->cursor_oldpos) {
            /* do we need to remove old cursor image ? */
            if (!and_region(&s, &e, scp->cursor_oldpos, scp->cursor_oldpos))
                sc_remove_cursor_image(scp);
            sc_draw_cursor_image(scp);
        } else {
            if (and_region(&s, &e, scp->cursor_pos, scp->cursor_pos))
		/* cursor didn't move, but has been overwritten */
		sc_draw_cursor_image(scp);
	    else if (scp->curs_attr.flags & CONS_BLINK_CURSOR)
		/* if it's a blinking cursor, update it */
		(*scp->rndr->blink_cursor)(scp, scp->cursor_pos,
					   sc_inside_cutmark(scp,
					       scp->cursor_pos));
        }
    }

#ifndef SC_NO_CUTPASTE
    /* update "pseudo" mouse pointer image */
    if (scp->sc->flags & SC_MOUSE_ENABLED) {
	if (!(scp->status & (MOUSE_VISIBLE | MOUSE_HIDDEN))) {
	    scp->status &= ~MOUSE_MOVED;
	    sc_draw_mouse_image(scp);
	}
    }
#endif /* SC_NO_CUTPASTE */

    scp->end = 0;
    scp->start = scp->xsize*scp->ysize - 1;

    SC_VIDEO_UNLOCK(scp->sc);
}

#ifdef DEV_SPLASH
static int
scsplash_callback(int event, void *arg)
{
    sc_softc_t *sc;
    int error;

    sc = (sc_softc_t *)arg;

    switch (event) {
    case SPLASH_INIT:
	if (add_scrn_saver(scsplash_saver) == 0) {
	    sc->flags &= ~SC_SAVER_FAILED;
	    run_scrn_saver = TRUE;
	    if (cold && !(boothowto & RB_VERBOSE)) {
		scsplash_stick(TRUE);
		(*current_saver)(sc, TRUE);
	    }
	}
	return 0;

    case SPLASH_TERM:
	if (current_saver == scsplash_saver) {
	    scsplash_stick(FALSE);
	    error = remove_scrn_saver(scsplash_saver);
	    if (error)
		return error;
	}
	return 0;

    default:
	return EINVAL;
    }
}

static void
scsplash_saver(sc_softc_t *sc, int show)
{
    static int busy = FALSE;
    scr_stat *scp;

    if (busy)
	return;
    busy = TRUE;

    scp = sc->cur_scp;
    if (show) {
	if (!(sc->flags & SC_SAVER_FAILED)) {
	    if (!(sc->flags & SC_SCRN_BLANKED))
		set_scrn_saver_mode(scp, -1, NULL, 0);
	    switch (splash(sc->adp, TRUE)) {
	    case 0:		/* succeeded */
		break;
	    case EAGAIN:	/* try later */
		restore_scrn_saver_mode(scp, FALSE);
		sc_touch_scrn_saver();		/* XXX */
		break;
	    default:
		sc->flags |= SC_SAVER_FAILED;
		scsplash_stick(FALSE);
		restore_scrn_saver_mode(scp, TRUE);
		printf("scsplash_saver(): failed to put up the image\n");
		break;
	    }
	}
    } else if (!sticky_splash) {
	if ((sc->flags & SC_SCRN_BLANKED) && (splash(sc->adp, FALSE) == 0))
	    restore_scrn_saver_mode(scp, TRUE);
    }
    busy = FALSE;
}

static int
add_scrn_saver(void (*this_saver)(sc_softc_t *, int))
{
#if 0
    int error;

    if (current_saver != none_saver) {
	error = remove_scrn_saver(current_saver);
	if (error)
	    return error;
    }
#endif
    if (current_saver != none_saver)
	return EBUSY;

    run_scrn_saver = FALSE;
    saver_mode = CONS_LKM_SAVER;
    current_saver = this_saver;
    return 0;
}

static int
remove_scrn_saver(void (*this_saver)(sc_softc_t *, int))
{
    if (current_saver != this_saver)
	return EINVAL;

#if 0
    /*
     * In order to prevent `current_saver' from being called by
     * the timeout routine `scrn_timer()' while we manipulate 
     * the saver list, we shall set `current_saver' to `none_saver' 
     * before stopping the current saver, rather than blocking by `splXX()'.
     */
    current_saver = none_saver;
    if (scrn_blanked)
        stop_scrn_saver(this_saver);
#endif

    /* unblank all blanked screens */
    wait_scrn_saver_stop(NULL);
    if (scrn_blanked)
	return EBUSY;

    current_saver = none_saver;
    return 0;
}

static int
set_scrn_saver_mode(scr_stat *scp, int mode, u_char *pal, int border)
{
    int s;

    /* assert(scp == scp->sc->cur_scp) */
    s = spltty();
    if (!ISGRAPHSC(scp))
	sc_remove_cursor_image(scp);
    scp->splash_save_mode = scp->mode;
    scp->splash_save_status = scp->status & (GRAPHICS_MODE | PIXEL_MODE);
    scp->status &= ~(GRAPHICS_MODE | PIXEL_MODE);
    scp->status |= (UNKNOWN_MODE | SAVER_RUNNING);
    scp->sc->flags |= SC_SCRN_BLANKED;
    ++scrn_blanked;
    splx(s);
    if (mode < 0)
	return 0;
    scp->mode = mode;
    if (set_mode(scp) == 0) {
	if (scp->sc->adp->va_info.vi_flags & V_INFO_GRAPHICS)
	    scp->status |= GRAPHICS_MODE;
#ifndef SC_NO_PALETTE_LOADING
	if (pal != NULL)
	    vidd_load_palette(scp->sc->adp, pal);
#endif
	sc_set_border(scp, border);
	return 0;
    } else {
	s = spltty();
	scp->mode = scp->splash_save_mode;
	scp->status &= ~(UNKNOWN_MODE | SAVER_RUNNING);
	scp->status |= scp->splash_save_status;
	splx(s);
	return 1;
    }
}

static int
restore_scrn_saver_mode(scr_stat *scp, int changemode)
{
    int mode;
    int status;
    int s;

    /* assert(scp == scp->sc->cur_scp) */
    s = spltty();
    mode = scp->mode;
    status = scp->status;
    scp->mode = scp->splash_save_mode;
    scp->status &= ~(UNKNOWN_MODE | SAVER_RUNNING);
    scp->status |= scp->splash_save_status;
    scp->sc->flags &= ~SC_SCRN_BLANKED;
    if (!changemode) {
	if (!ISGRAPHSC(scp))
	    sc_draw_cursor_image(scp);
	--scrn_blanked;
	splx(s);
	return 0;
    }
    if (set_mode(scp) == 0) {
#ifndef SC_NO_PALETTE_LOADING
#ifdef SC_PIXEL_MODE
	if (scp->sc->adp->va_info.vi_mem_model == V_INFO_MM_DIRECT)
	    vidd_load_palette(scp->sc->adp, scp->sc->palette2);
	else
#endif
	vidd_load_palette(scp->sc->adp, scp->sc->palette);
#endif
	--scrn_blanked;
	splx(s);
	return 0;
    } else {
	scp->mode = mode;
	scp->status = status;
	splx(s);
	return 1;
    }
}

static void
stop_scrn_saver(sc_softc_t *sc, void (*saver)(sc_softc_t *, int))
{
    (*saver)(sc, FALSE);
    run_scrn_saver = FALSE;
    /* the screen saver may have chosen not to stop after all... */
    if (sc->flags & SC_SCRN_BLANKED)
	return;

    mark_all(sc->cur_scp);
    if (sc->delayed_next_scr)
	sc_switch_scr(sc, sc->delayed_next_scr - 1);
    if (!kdb_active)
	wakeup(&scrn_blanked);
}

static int
wait_scrn_saver_stop(sc_softc_t *sc)
{
    int error = 0;

    while (scrn_blanked > 0) {
	run_scrn_saver = FALSE;
	if (sc && !(sc->flags & SC_SCRN_BLANKED)) {
	    error = 0;
	    break;
	}
	error = tsleep(&scrn_blanked, PZERO | PCATCH, "scrsav", 0);
	if ((error != 0) && (error != ERESTART))
	    break;
    }
    run_scrn_saver = FALSE;
    return error;
}
#endif /* DEV_SPLASH */

void
sc_touch_scrn_saver(void)
{
    scsplash_stick(FALSE);
    run_scrn_saver = FALSE;
}

int
sc_switch_scr(sc_softc_t *sc, u_int next_scr)
{
    scr_stat *cur_scp;
    struct tty *tp;
    struct proc *p;
    int s;

    DPRINTF(5, ("sc0: sc_switch_scr() %d ", next_scr + 1));

    if (sc->cur_scp == NULL)
	return (0);

    /* prevent switch if previously requested */
    if (sc->flags & SC_SCRN_VTYLOCK) {
	    sc_bell(sc->cur_scp, sc->cur_scp->bell_pitch,
		sc->cur_scp->bell_duration);
	    return EPERM;
    }

    /* delay switch if the screen is blanked or being updated */
    if ((sc->flags & SC_SCRN_BLANKED) || sc->write_in_progress
	|| sc->blink_in_progress) {
	sc->delayed_next_scr = next_scr + 1;
	sc_touch_scrn_saver();
	DPRINTF(5, ("switch delayed\n"));
	return 0;
    }
    sc->delayed_next_scr = 0;

    s = spltty();
    cur_scp = sc->cur_scp;

    /* we are in the middle of the vty switching process... */
    if (sc->switch_in_progress
	&& (cur_scp->smode.mode == VT_PROCESS)
	&& cur_scp->proc) {
	p = pfind(cur_scp->pid);
	if (cur_scp->proc != p) {
	    if (p)
		PROC_UNLOCK(p);
	    /* 
	     * The controlling process has died!!.  Do some clean up.
	     * NOTE:`cur_scp->proc' and `cur_scp->smode.mode' 
	     * are not reset here yet; they will be cleared later.
	     */
	    DPRINTF(5, ("cur_scp controlling process %d died, ",
	       cur_scp->pid));
	    if (cur_scp->status & SWITCH_WAIT_REL) {
		/*
		 * Force the previous switch to finish, but return now 
		 * with error.
		 */
		DPRINTF(5, ("reset WAIT_REL, "));
		finish_vt_rel(cur_scp, TRUE, &s);
		splx(s);
		DPRINTF(5, ("finishing previous switch\n"));
		return EINVAL;
	    } else if (cur_scp->status & SWITCH_WAIT_ACQ) {
		/* let's assume screen switch has been completed. */
		DPRINTF(5, ("reset WAIT_ACQ, "));
		finish_vt_acq(cur_scp);
	    } else {
		/* 
	 	 * We are in between screen release and acquisition, and
		 * reached here via scgetc() or scrn_timer() which has 
		 * interrupted exchange_scr(). Don't do anything stupid.
		 */
		DPRINTF(5, ("waiting nothing, "));
	    }
	} else {
	    if (p)
		PROC_UNLOCK(p);
	    /*
	     * The controlling process is alive, but not responding... 
	     * It is either buggy or it may be just taking time.
	     * The following code is a gross kludge to cope with this
	     * problem for which there is no clean solution. XXX
	     */
	    if (cur_scp->status & SWITCH_WAIT_REL) {
		switch (sc->switch_in_progress++) {
		case 1:
		    break;
		case 2:
		    DPRINTF(5, ("sending relsig again, "));
		    signal_vt_rel(cur_scp);
		    break;
		case 3:
		    break;
		case 4:
		default:
		    /*
		     * Act as if the controlling program returned
		     * VT_FALSE.
		     */
		    DPRINTF(5, ("force reset WAIT_REL, "));
		    finish_vt_rel(cur_scp, FALSE, &s);
		    splx(s);
		    DPRINTF(5, ("act as if VT_FALSE was seen\n"));
		    return EINVAL;
		}
	    } else if (cur_scp->status & SWITCH_WAIT_ACQ) {
		switch (sc->switch_in_progress++) {
		case 1:
		    break;
		case 2:
		    DPRINTF(5, ("sending acqsig again, "));
		    signal_vt_acq(cur_scp);
		    break;
		case 3:
		    break;
		case 4:
		default:
		     /* clear the flag and finish the previous switch */
		    DPRINTF(5, ("force reset WAIT_ACQ, "));
		    finish_vt_acq(cur_scp);
		    break;
		}
	    }
	}
    }

    /*
     * Return error if an invalid argument is given, or vty switch
     * is still in progress.
     */
    if ((next_scr < sc->first_vty) || (next_scr >= sc->first_vty + sc->vtys)
	|| sc->switch_in_progress) {
	splx(s);
	sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	DPRINTF(5, ("error 1\n"));
	return EINVAL;
    }

    /*
     * Don't allow switching away from the graphics mode vty
     * if the switch mode is VT_AUTO, unless the next vty is the same 
     * as the current or the current vty has been closed (but showing).
     */
    tp = SC_DEV(sc, cur_scp->index);
    if ((cur_scp->index != next_scr)
	&& tty_opened_ns(tp)
	&& (cur_scp->smode.mode == VT_AUTO)
	&& ISGRAPHSC(cur_scp)) {
	splx(s);
	sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	DPRINTF(5, ("error, graphics mode\n"));
	return EINVAL;
    }

    /*
     * Is the wanted vty open? Don't allow switching to a closed vty.
     * If we are in DDB, don't switch to a vty in the VT_PROCESS mode.
     * Note that we always allow the user to switch to the kernel 
     * console even if it is closed.
     */
    if ((sc_console == NULL) || (next_scr != sc_console->index)) {
	tp = SC_DEV(sc, next_scr);
	if (!tty_opened_ns(tp)) {
	    splx(s);
	    sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	    DPRINTF(5, ("error 2, requested vty isn't open!\n"));
	    return EINVAL;
	}
	if (kdb_active && SC_STAT(tp)->smode.mode == VT_PROCESS) {
	    splx(s);
	    DPRINTF(5, ("error 3, requested vty is in the VT_PROCESS mode\n"));
	    return EINVAL;
	}
    }

    /* this is the start of vty switching process... */
    ++sc->switch_in_progress;
    sc->old_scp = cur_scp;
    sc->new_scp = sc_get_stat(SC_DEV(sc, next_scr));
    if (sc->new_scp == sc->old_scp) {
	sc->switch_in_progress = 0;
	/*
	 * XXX wakeup() locks the scheduler lock which will hang if
	 * the lock is in an in-between state, e.g., when we stop at
	 * a breakpoint at fork_exit.  It has always been wrong to call
	 * wakeup() when the debugger is active.  In RELENG_4, wakeup()
	 * is supposed to be locked by splhigh(), but the debugger may
	 * be invoked at splhigh().
	 */
	if (!kdb_active)
	    wakeup(VTY_WCHAN(sc,next_scr));
	splx(s);
	DPRINTF(5, ("switch done (new == old)\n"));
	return 0;
    }

    /* has controlling process died? */
    vt_proc_alive(sc->old_scp);
    vt_proc_alive(sc->new_scp);

    /* wait for the controlling process to release the screen, if necessary */
    if (signal_vt_rel(sc->old_scp)) {
	splx(s);
	return 0;
    }

    /* go set up the new vty screen */
    splx(s);
    exchange_scr(sc);
    s = spltty();

    /* wake up processes waiting for this vty */
    if (!kdb_active)
	wakeup(VTY_WCHAN(sc,next_scr));

    /* wait for the controlling process to acknowledge, if necessary */
    if (signal_vt_acq(sc->cur_scp)) {
	splx(s);
	return 0;
    }

    sc->switch_in_progress = 0;
    if (sc->unit == sc_console_unit)
	cnavailable(sc_consptr,  TRUE);
    splx(s);
    DPRINTF(5, ("switch done\n"));

    return 0;
}

static int
do_switch_scr(sc_softc_t *sc, int s)
{
    vt_proc_alive(sc->new_scp);

    splx(s);
    exchange_scr(sc);
    s = spltty();
    /* sc->cur_scp == sc->new_scp */
    wakeup(VTY_WCHAN(sc,sc->cur_scp->index));

    /* wait for the controlling process to acknowledge, if necessary */
    if (!signal_vt_acq(sc->cur_scp)) {
	sc->switch_in_progress = 0;
	if (sc->unit == sc_console_unit)
	    cnavailable(sc_consptr,  TRUE);
    }

    return s;
}

static int
vt_proc_alive(scr_stat *scp)
{
    struct proc *p;

    if (scp->proc) {
	if ((p = pfind(scp->pid)) != NULL)
	    PROC_UNLOCK(p);
	if (scp->proc == p)
	    return TRUE;
	scp->proc = NULL;
	scp->smode.mode = VT_AUTO;
	DPRINTF(5, ("vt controlling process %d died\n", scp->pid));
    }
    return FALSE;
}

static int
signal_vt_rel(scr_stat *scp)
{
    if (scp->smode.mode != VT_PROCESS)
	return FALSE;
    scp->status |= SWITCH_WAIT_REL;
    PROC_LOCK(scp->proc);
    kern_psignal(scp->proc, scp->smode.relsig);
    PROC_UNLOCK(scp->proc);
    DPRINTF(5, ("sending relsig to %d\n", scp->pid));
    return TRUE;
}

static int
signal_vt_acq(scr_stat *scp)
{
    if (scp->smode.mode != VT_PROCESS)
	return FALSE;
    if (scp->sc->unit == sc_console_unit)
	cnavailable(sc_consptr,  FALSE);
    scp->status |= SWITCH_WAIT_ACQ;
    PROC_LOCK(scp->proc);
    kern_psignal(scp->proc, scp->smode.acqsig);
    PROC_UNLOCK(scp->proc);
    DPRINTF(5, ("sending acqsig to %d\n", scp->pid));
    return TRUE;
}

static int
finish_vt_rel(scr_stat *scp, int release, int *s)
{
    if (scp == scp->sc->old_scp && scp->status & SWITCH_WAIT_REL) {
	scp->status &= ~SWITCH_WAIT_REL;
	if (release)
	    *s = do_switch_scr(scp->sc, *s);
	else
	    scp->sc->switch_in_progress = 0;
	return 0;
    }
    return EINVAL;
}

static int
finish_vt_acq(scr_stat *scp)
{
    if (scp == scp->sc->new_scp && scp->status & SWITCH_WAIT_ACQ) {
	scp->status &= ~SWITCH_WAIT_ACQ;
	scp->sc->switch_in_progress = 0;
	return 0;
    }
    return EINVAL;
}

static void
exchange_scr(sc_softc_t *sc)
{
    scr_stat *scp;

    /* save the current state of video and keyboard */
    sc_move_cursor(sc->old_scp, sc->old_scp->xpos, sc->old_scp->ypos);
    if (!ISGRAPHSC(sc->old_scp))
	sc_remove_cursor_image(sc->old_scp);
    if (sc->old_scp->kbd_mode == K_XLATE)
	save_kbd_state(sc->old_scp);

    /* set up the video for the new screen */
    scp = sc->cur_scp = sc->new_scp;
    if (sc->old_scp->mode != scp->mode || ISUNKNOWNSC(sc->old_scp))
	set_mode(scp);
#ifndef __sparc64__
    else
	sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		    (void *)sc->adp->va_window, FALSE);
#endif
    scp->status |= MOUSE_HIDDEN;
    sc_move_cursor(scp, scp->xpos, scp->ypos);
    if (!ISGRAPHSC(scp))
	sc_set_cursor_image(scp);
#ifndef SC_NO_PALETTE_LOADING
    if (ISGRAPHSC(sc->old_scp)) {
#ifdef SC_PIXEL_MODE
	if (sc->adp->va_info.vi_mem_model == V_INFO_MM_DIRECT)
	    vidd_load_palette(sc->adp, sc->palette2);
	else
#endif
	vidd_load_palette(sc->adp, sc->palette);
    }
#endif
    sc_set_border(scp, scp->border);

    /* set up the keyboard for the new screen */
    if (sc->kbd_open_level == 0 && sc->old_scp->kbd_mode != scp->kbd_mode)
	(void)kbdd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
    update_kbd_state(scp, scp->status, LOCK_MASK);

    mark_all(scp);
}

static void
sc_puts(scr_stat *scp, u_char *buf, int len)
{
#ifdef DEV_SPLASH
    /* make screensaver happy */
    if (!sticky_splash && scp == scp->sc->cur_scp && !sc_saver_keyb_only)
	run_scrn_saver = FALSE;
#endif

    if (scp->tsw)
	(*scp->tsw->te_puts)(scp, buf, len);
    if (scp->sc->delayed_next_scr)
	sc_switch_scr(scp->sc, scp->sc->delayed_next_scr - 1);
}

void
sc_draw_cursor_image(scr_stat *scp)
{
    /* assert(scp == scp->sc->cur_scp); */
    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->draw_cursor)(scp, scp->cursor_pos,
			      scp->curs_attr.flags & CONS_BLINK_CURSOR, TRUE,
			      sc_inside_cutmark(scp, scp->cursor_pos));
    scp->cursor_oldpos = scp->cursor_pos;
    SC_VIDEO_UNLOCK(scp->sc);
}

void
sc_remove_cursor_image(scr_stat *scp)
{
    /* assert(scp == scp->sc->cur_scp); */
    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->draw_cursor)(scp, scp->cursor_oldpos,
			      scp->curs_attr.flags & CONS_BLINK_CURSOR, FALSE,
			      sc_inside_cutmark(scp, scp->cursor_oldpos));
    SC_VIDEO_UNLOCK(scp->sc);
}

static void
update_cursor_image(scr_stat *scp)
{
    /* assert(scp == scp->sc->cur_scp); */
    sc_remove_cursor_image(scp);
    sc_set_cursor_image(scp);
    sc_draw_cursor_image(scp);
}

void
sc_set_cursor_image(scr_stat *scp)
{
    scp->curs_attr = scp->base_curs_attr;
    if (scp->curs_attr.flags & CONS_HIDDEN_CURSOR) {
	/* hidden cursor is internally represented as zero-height underline */
	scp->curs_attr.flags = CONS_CHAR_CURSOR;
	scp->curs_attr.base = scp->curs_attr.height = 0;
    } else if (scp->curs_attr.flags & CONS_CHAR_CURSOR) {
	scp->curs_attr.base = imin(scp->base_curs_attr.base,
				  scp->font_size - 1);
	scp->curs_attr.height = imin(scp->base_curs_attr.height,
				    scp->font_size - scp->curs_attr.base);
    } else {	/* block cursor */
	scp->curs_attr.base = 0;
	scp->curs_attr.height = scp->font_size;
    }

    /* assert(scp == scp->sc->cur_scp); */
    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->set_cursor)(scp, scp->curs_attr.base, scp->curs_attr.height,
			     scp->curs_attr.flags & CONS_BLINK_CURSOR);
    SC_VIDEO_UNLOCK(scp->sc);
}

static void
sc_adjust_ca(struct cursor_attr *cap, int flags, int base, int height)
{
    if (flags & CONS_CHARCURSOR_COLORS) {
	cap->bg[0] = base & 0xff;
	cap->bg[1] = height & 0xff;
    } else if (flags & CONS_MOUSECURSOR_COLORS) {
	cap->mouse_ba = base & 0xff;
	cap->mouse_ia = height & 0xff;
    } else {
	if (base >= 0)
	    cap->base = base;
	if (height >= 0)
	    cap->height = height;
	if (!(flags & CONS_SHAPEONLY_CURSOR))
		cap->flags = flags & CONS_CURSOR_ATTRS;
    }
}

static void
change_cursor_shape(scr_stat *scp, int flags, int base, int height)
{
    if ((scp == scp->sc->cur_scp) && !ISGRAPHSC(scp))
	sc_remove_cursor_image(scp);

    if (flags & CONS_RESET_CURSOR)
	scp->base_curs_attr = scp->dflt_curs_attr;
    else if (flags & CONS_DEFAULT_CURSOR) {
	sc_adjust_ca(&scp->dflt_curs_attr, flags, base, height);
	scp->base_curs_attr = scp->dflt_curs_attr;
    } else
	sc_adjust_ca(&scp->base_curs_attr, flags, base, height);

    if ((scp == scp->sc->cur_scp) && !ISGRAPHSC(scp)) {
	sc_set_cursor_image(scp);
	sc_draw_cursor_image(scp);
    }
}

void
sc_change_cursor_shape(scr_stat *scp, int flags, int base, int height)
{
    sc_softc_t *sc;
    struct tty *tp;
    int s;
    int i;

    if (flags == -1)
	flags = CONS_SHAPEONLY_CURSOR;

    s = spltty();
    if (flags & CONS_LOCAL_CURSOR) {
	/* local (per vty) change */
	change_cursor_shape(scp, flags, base, height);
	splx(s);
	return;
    }

    /* global change */
    sc = scp->sc;
    if (flags & CONS_RESET_CURSOR)
	sc->curs_attr = sc->dflt_curs_attr;
    else if (flags & CONS_DEFAULT_CURSOR) {
	sc_adjust_ca(&sc->dflt_curs_attr, flags, base, height);
	sc->curs_attr = sc->dflt_curs_attr;
    } else
	sc_adjust_ca(&sc->curs_attr, flags, base, height);

    for (i = sc->first_vty; i < sc->first_vty + sc->vtys; ++i) {
	if ((tp = SC_DEV(sc, i)) == NULL)
	    continue;
	if ((scp = sc_get_stat(tp)) == NULL)
	    continue;
	scp->dflt_curs_attr = sc->curs_attr;
	change_cursor_shape(scp, CONS_RESET_CURSOR, -1, -1);
    }
    splx(s);
}

static void
scinit(int unit, int flags)
{

    /*
     * When syscons is being initialized as the kernel console, malloc()
     * is not yet functional, because various kernel structures has not been
     * fully initialized yet.  Therefore, we need to declare the following
     * static buffers for the console.  This is less than ideal, 
     * but is necessry evil for the time being.  XXX
     */
    static u_short sc_buffer[ROW*COL];	/* XXX */
#ifndef SC_NO_FONT_LOADING
    static u_char font_8[256*8];
    static u_char font_14[256*14];
    static u_char font_16[256*16];
#endif

#ifdef SC_KERNEL_CONS_ATTRS
    static const u_char dflt_kattrtab[] = SC_KERNEL_CONS_ATTRS;
#elif SC_KERNEL_CONS_ATTR == FG_WHITE
    static const u_char dflt_kattrtab[] = {
	FG_WHITE, FG_YELLOW, FG_LIGHTMAGENTA, FG_LIGHTRED,
	FG_LIGHTCYAN, FG_LIGHTGREEN, FG_LIGHTBLUE, FG_GREEN,
	0,
    };
#else
    static const u_char dflt_kattrtab[] = { FG_WHITE, 0, };
#endif
    sc_softc_t *sc;
    scr_stat *scp;
    video_adapter_t *adp;
    int col;
    int row;
    int i;

    /* one time initialization */
    if (init_done == COLD) {
	sc_get_bios_values(&bios_value);
	for (i = 0; i < nitems(sc_kattrtab); i++)
	    sc_kattrtab[i] = dflt_kattrtab[i % (nitems(dflt_kattrtab) - 1)];
    }
    init_done = WARM;

    /*
     * Allocate resources.  Even if we are being called for the second
     * time, we must allocate them again, because they might have 
     * disappeared...
     */
    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);
    if ((sc->flags & SC_INIT_DONE) == 0)
	SC_VIDEO_LOCKINIT(sc);

    adp = NULL;
    if (sc->adapter >= 0) {
	vid_release(sc->adp, (void *)&sc->adapter);
	adp = sc->adp;
	sc->adp = NULL;
    }
    if (sc->keyboard >= 0) {
	DPRINTF(5, ("sc%d: releasing kbd%d\n", unit, sc->keyboard));
	i = kbd_release(sc->kbd, (void *)&sc->keyboard);
	DPRINTF(5, ("sc%d: kbd_release returned %d\n", unit, i));
	if (sc->kbd != NULL) {
	    DPRINTF(5, ("sc%d: kbd != NULL!, index:%d, unit:%d, flags:0x%x\n",
		unit, sc->kbd->kb_index, sc->kbd->kb_unit, sc->kbd->kb_flags));
	}
	sc->kbd = NULL;
    }
    sc->adapter = vid_allocate("*", unit, (void *)&sc->adapter);
    sc->adp = vid_get_adapter(sc->adapter);
    /* assert((sc->adapter >= 0) && (sc->adp != NULL)) */

    sc->keyboard = sc_allocate_keyboard(sc, unit);
    DPRINTF(1, ("sc%d: keyboard %d\n", unit, sc->keyboard));

    sc->kbd = kbd_get_keyboard(sc->keyboard);
    if (sc->kbd != NULL) {
	DPRINTF(1, ("sc%d: kbd index:%d, unit:%d, flags:0x%x\n",
		unit, sc->kbd->kb_index, sc->kbd->kb_unit, sc->kbd->kb_flags));
    }

    if (!(sc->flags & SC_INIT_DONE) || (adp != sc->adp)) {

	sc->initial_mode = sc->adp->va_initial_mode;

#ifndef SC_NO_FONT_LOADING
	if (flags & SC_KERNEL_CONSOLE) {
	    sc->font_8 = font_8;
	    sc->font_14 = font_14;
	    sc->font_16 = font_16;
	} else if (sc->font_8 == NULL) {
	    /* assert(sc_malloc) */
	    sc->font_8 = malloc(sizeof(font_8), M_DEVBUF, M_WAITOK);
	    sc->font_14 = malloc(sizeof(font_14), M_DEVBUF, M_WAITOK);
	    sc->font_16 = malloc(sizeof(font_16), M_DEVBUF, M_WAITOK);
	}
#endif

	/* extract the hardware cursor location and hide the cursor for now */
	vidd_read_hw_cursor(sc->adp, &col, &row);
	vidd_set_hw_cursor(sc->adp, -1, -1);

	/* set up the first console */
	sc->first_vty = unit*MAXCONS;
	sc->vtys = MAXCONS;		/* XXX: should be configurable */
	if (flags & SC_KERNEL_CONSOLE) {
	    /*
	     * Set up devs structure but don't use it yet, calling make_dev()
	     * might panic kernel.  Wait for sc_attach_unit() to actually
	     * create the devices.
	     */
	    sc->dev = main_devs;
	    scp = &main_console;
	    init_scp(sc, sc->first_vty, scp);
	    sc_vtb_init(&scp->vtb, VTB_MEMORY, scp->xsize, scp->ysize,
			(void *)sc_buffer, FALSE);
	    if (sc_init_emulator(scp, SC_DFLT_TERM))
		sc_init_emulator(scp, "*");
	    (*scp->tsw->te_default_attr)(scp, SC_KERNEL_CONS_ATTR,
					 SC_KERNEL_CONS_REV_ATTR);
	} else {
	    /* assert(sc_malloc) */
	    sc->dev = malloc(sizeof(struct tty *)*sc->vtys, M_DEVBUF,
	        M_WAITOK|M_ZERO);
	    sc->dev[0] = sc_alloc_tty(0, unit * MAXCONS);
	    scp = alloc_scp(sc, sc->first_vty);
	    SC_STAT(sc->dev[0]) = scp;
	}
	sc->cur_scp = scp;

#ifndef __sparc64__
	/* copy screen to temporary buffer */
	sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		    (void *)scp->sc->adp->va_window, FALSE);
	if (ISTEXTSC(scp))
	    sc_vtb_copy(&scp->scr, 0, &scp->vtb, 0, scp->xsize*scp->ysize);
#endif

	/* Sync h/w cursor position to s/w (sc and teken). */
	if (col >= scp->xsize)
	    col = 0;
	if (row >= scp->ysize)
	    row = scp->ysize - 1;
	scp->xpos = col;
	scp->ypos = row;
	scp->cursor_pos = scp->cursor_oldpos = row*scp->xsize + col;
	(*scp->tsw->te_sync)(scp);

	sc->dflt_curs_attr.base = 0;
	sc->dflt_curs_attr.height = howmany(scp->font_size, 8);
	sc->dflt_curs_attr.flags = 0;
	sc->dflt_curs_attr.bg[0] = FG_RED;
	sc->dflt_curs_attr.bg[1] = FG_LIGHTGREY;
	sc->dflt_curs_attr.bg[2] = FG_BLUE;
	sc->dflt_curs_attr.mouse_ba = FG_WHITE;
	sc->dflt_curs_attr.mouse_ia = FG_RED;
	sc->curs_attr = sc->dflt_curs_attr;
	scp->base_curs_attr = scp->dflt_curs_attr = sc->curs_attr;
	scp->curs_attr = scp->base_curs_attr;

#ifndef SC_NO_SYSMOUSE
	sc_mouse_move(scp, scp->xpixel/2, scp->ypixel/2);
#endif
	if (!ISGRAPHSC(scp)) {
    	    sc_set_cursor_image(scp);
    	    sc_draw_cursor_image(scp);
	}

	/* save font and palette */
#ifndef SC_NO_FONT_LOADING
	sc->fonts_loaded = 0;
	if (ISFONTAVAIL(sc->adp->va_flags)) {
#ifdef SC_DFLT_FONT
	    bcopy(dflt_font_8, sc->font_8, sizeof(dflt_font_8));
	    bcopy(dflt_font_14, sc->font_14, sizeof(dflt_font_14));
	    bcopy(dflt_font_16, sc->font_16, sizeof(dflt_font_16));
	    sc->fonts_loaded = FONT_16 | FONT_14 | FONT_8;
	    if (scp->font_size < 14) {
		sc_load_font(scp, 0, 8, 8, sc->font_8, 0, 256);
	    } else if (scp->font_size >= 16) {
		sc_load_font(scp, 0, 16, 8, sc->font_16, 0, 256);
	    } else {
		sc_load_font(scp, 0, 14, 8, sc->font_14, 0, 256);
	    }
#else /* !SC_DFLT_FONT */
	    if (scp->font_size < 14) {
		sc_save_font(scp, 0, 8, 8, sc->font_8, 0, 256);
		sc->fonts_loaded = FONT_8;
	    } else if (scp->font_size >= 16) {
		sc_save_font(scp, 0, 16, 8, sc->font_16, 0, 256);
		sc->fonts_loaded = FONT_16;
	    } else {
		sc_save_font(scp, 0, 14, 8, sc->font_14, 0, 256);
		sc->fonts_loaded = FONT_14;
	    }
#endif /* SC_DFLT_FONT */
	    /* FONT KLUDGE: always use the font page #0. XXX */
	    sc_show_font(scp, 0);
	}
#endif /* !SC_NO_FONT_LOADING */

#ifndef SC_NO_PALETTE_LOADING
	vidd_save_palette(sc->adp, sc->palette);
#ifdef SC_PIXEL_MODE
	for (i = 0; i < sizeof(sc->palette2); i++)
		sc->palette2[i] = i / 3;
#endif
#endif

#ifdef DEV_SPLASH
	if (!(sc->flags & SC_SPLASH_SCRN)) {
	    /* we are ready to put up the splash image! */
	    splash_init(sc->adp, scsplash_callback, sc);
	    sc->flags |= SC_SPLASH_SCRN;
	}
#endif
    }

    /* the rest is not necessary, if we have done it once */
    if (sc->flags & SC_INIT_DONE)
	return;

    /* initialize mapscrn arrays to a one to one map */
    for (i = 0; i < sizeof(sc->scr_map); i++)
	sc->scr_map[i] = sc->scr_rmap[i] = i;

    sc->flags |= SC_INIT_DONE;
}

static void
scterm(int unit, int flags)
{
    sc_softc_t *sc;
    scr_stat *scp;

    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);
    if (sc == NULL)
	return;			/* shouldn't happen */

#ifdef DEV_SPLASH
    /* this console is no longer available for the splash screen */
    if (sc->flags & SC_SPLASH_SCRN) {
	splash_term(sc->adp);
	sc->flags &= ~SC_SPLASH_SCRN;
    }
#endif

#if 0 /* XXX */
    /* move the hardware cursor to the upper-left corner */
    vidd_set_hw_cursor(sc->adp, 0, 0);
#endif

    /* release the keyboard and the video card */
    if (sc->keyboard >= 0)
	kbd_release(sc->kbd, &sc->keyboard);
    if (sc->adapter >= 0)
	vid_release(sc->adp, &sc->adapter);

    /* stop the terminal emulator, if any */
    scp = sc_get_stat(sc->dev[0]);
    if (scp->tsw)
	(*scp->tsw->te_term)(scp, &scp->ts);
    mtx_destroy(&sc->video_mtx);

    /* clear the structure */
    if (!(flags & SC_KERNEL_CONSOLE)) {
	free(scp->ts, M_DEVBUF);
	/* XXX: We need delete_dev() for this */
	free(sc->dev, M_DEVBUF);
#if 0
	/* XXX: We need a ttyunregister for this */
	free(sc->tty, M_DEVBUF);
#endif
#ifndef SC_NO_FONT_LOADING
	free(sc->font_8, M_DEVBUF);
	free(sc->font_14, M_DEVBUF);
	free(sc->font_16, M_DEVBUF);
#endif
	/* XXX vtb, history */
    }
    bzero(sc, sizeof(*sc));
    sc->keyboard = -1;
    sc->adapter = -1;
}

static void
scshutdown(__unused void *arg, __unused int howto)
{

	KASSERT(sc_console != NULL, ("sc_console != NULL"));
	KASSERT(sc_console->sc != NULL, ("sc_console->sc != NULL"));
	KASSERT(sc_console->sc->cur_scp != NULL,
	    ("sc_console->sc->cur_scp != NULL"));

	sc_touch_scrn_saver();
	if (!cold &&
	    sc_console->sc->cur_scp->index != sc_console->index &&
	    sc_console->sc->cur_scp->smode.mode == VT_AUTO &&
	    sc_console->smode.mode == VT_AUTO)
		sc_switch_scr(sc_console->sc, sc_console->index);
	shutdown_in_progress = TRUE;
}

static void
scsuspend(__unused void *arg)
{
	int retry;

	KASSERT(sc_console != NULL, ("sc_console != NULL"));
	KASSERT(sc_console->sc != NULL, ("sc_console->sc != NULL"));
	KASSERT(sc_console->sc->cur_scp != NULL,
	    ("sc_console->sc->cur_scp != NULL"));

	sc_susp_scr = sc_console->sc->cur_scp->index;
	if (sc_no_suspend_vtswitch ||
	    sc_susp_scr == sc_console->index) {
		sc_touch_scrn_saver();
		sc_susp_scr = -1;
		return;
	}
	for (retry = 0; retry < 10; retry++) {
		sc_switch_scr(sc_console->sc, sc_console->index);
		if (!sc_console->sc->switch_in_progress)
			break;
		pause("scsuspend", hz);
	}
	suspend_in_progress = TRUE;
}

static void
scresume(__unused void *arg)
{

	KASSERT(sc_console != NULL, ("sc_console != NULL"));
	KASSERT(sc_console->sc != NULL, ("sc_console->sc != NULL"));
	KASSERT(sc_console->sc->cur_scp != NULL,
	    ("sc_console->sc->cur_scp != NULL"));

	suspend_in_progress = FALSE;
	if (sc_susp_scr < 0) {
		update_font(sc_console->sc->cur_scp);
		return;
	}
	sc_switch_scr(sc_console->sc, sc_susp_scr);
}

int
sc_clean_up(scr_stat *scp)
{
#ifdef DEV_SPLASH
    int error;
#endif

    if (scp->sc->flags & SC_SCRN_BLANKED) {
	sc_touch_scrn_saver();
#ifdef DEV_SPLASH
	if ((error = wait_scrn_saver_stop(scp->sc)))
	    return error;
#endif
    }
    scp->status |= MOUSE_HIDDEN;
    sc_remove_mouse_image(scp);
    sc_remove_cutmarking(scp);
    return 0;
}

void
sc_alloc_scr_buffer(scr_stat *scp, int wait, int discard)
{
    sc_vtb_t new;
    sc_vtb_t old;

    old = scp->vtb;
    sc_vtb_init(&new, VTB_MEMORY, scp->xsize, scp->ysize, NULL, wait);
    if (!discard && (old.vtb_flags & VTB_VALID)) {
	/* retain the current cursor position and buffer contants */
	scp->cursor_oldpos = scp->cursor_pos;
	/* 
	 * This works only if the old buffer has the same size as or larger 
	 * than the new one. XXX
	 */
	sc_vtb_copy(&old, 0, &new, 0, scp->xsize*scp->ysize);
	scp->vtb = new;
    } else {
	scp->vtb = new;
	sc_vtb_destroy(&old);
    }

#ifndef SC_NO_SYSMOUSE
    /* move the mouse cursor at the center of the screen */
    sc_mouse_move(scp, scp->xpixel / 2, scp->ypixel / 2);
#endif
}

static scr_stat
*alloc_scp(sc_softc_t *sc, int vty)
{
    scr_stat *scp;

    /* assert(sc_malloc) */

    scp = (scr_stat *)malloc(sizeof(scr_stat), M_DEVBUF, M_WAITOK);
    init_scp(sc, vty, scp);

    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    if (sc_init_emulator(scp, SC_DFLT_TERM))
	sc_init_emulator(scp, "*");

#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, TRUE);
#endif

#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, 0, 0, TRUE);
#endif

    return scp;
}

static void
init_scp(sc_softc_t *sc, int vty, scr_stat *scp)
{
    video_info_t info;

    bzero(scp, sizeof(*scp));

    scp->index = vty;
    scp->sc = sc;
    scp->status = 0;
    scp->mode = sc->initial_mode;
    vidd_get_info(sc->adp, scp->mode, &info);
    if (info.vi_flags & V_INFO_GRAPHICS) {
	scp->status |= GRAPHICS_MODE;
	scp->xpixel = info.vi_width;
	scp->ypixel = info.vi_height;
	scp->xsize = info.vi_width/info.vi_cwidth;
	scp->ysize = info.vi_height/info.vi_cheight;
	scp->font_size = 0;
	scp->font = NULL;
    } else {
	scp->xsize = info.vi_width;
	scp->ysize = info.vi_height;
	scp->xpixel = scp->xsize*info.vi_cwidth;
	scp->ypixel = scp->ysize*info.vi_cheight;
    }

    scp->font_size = info.vi_cheight;
    scp->font_width = info.vi_cwidth;
#ifndef SC_NO_FONT_LOADING
    if (info.vi_cheight < 14)
	scp->font = sc->font_8;
    else if (info.vi_cheight >= 16)
	scp->font = sc->font_16;
    else
	scp->font = sc->font_14;
#else
    scp->font = NULL;
#endif

    sc_vtb_init(&scp->vtb, VTB_MEMORY, 0, 0, NULL, FALSE);
#ifndef __sparc64__
    sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, 0, 0, NULL, FALSE);
#endif
    scp->xoff = scp->yoff = 0;
    scp->xpos = scp->ypos = 0;
    scp->start = scp->xsize * scp->ysize - 1;
    scp->end = 0;
    scp->tsw = NULL;
    scp->ts = NULL;
    scp->rndr = NULL;
    scp->border = (SC_NORM_ATTR >> 4) & 0x0f;
    scp->base_curs_attr = scp->dflt_curs_attr = sc->curs_attr;
    scp->mouse_cut_start = scp->xsize*scp->ysize;
    scp->mouse_cut_end = -1;
    scp->mouse_signal = 0;
    scp->mouse_pid = 0;
    scp->mouse_proc = NULL;
    scp->kbd_mode = K_XLATE;
    scp->bell_pitch = bios_value.bell_pitch;
    scp->bell_duration = BELL_DURATION;
    scp->status |= (bios_value.shift_state & NLKED);
    scp->status |= CURSOR_ENABLED | MOUSE_HIDDEN;
    scp->pid = 0;
    scp->proc = NULL;
    scp->smode.mode = VT_AUTO;
    scp->history = NULL;
    scp->history_pos = 0;
    scp->history_size = 0;
}

int
sc_init_emulator(scr_stat *scp, char *name)
{
    sc_term_sw_t *sw;
    sc_rndr_sw_t *rndr;
    void *p;
    int error;

    if (name == NULL)	/* if no name is given, use the current emulator */
	sw = scp->tsw;
    else		/* ...otherwise find the named emulator */
	sw = sc_term_match(name);
    if (sw == NULL)
	return EINVAL;

    rndr = NULL;
    if (strcmp(sw->te_renderer, "*") != 0) {
	rndr = sc_render_match(scp, sw->te_renderer,
			       scp->status & (GRAPHICS_MODE | PIXEL_MODE));
    }
    if (rndr == NULL) {
	rndr = sc_render_match(scp, scp->sc->adp->va_name,
			       scp->status & (GRAPHICS_MODE | PIXEL_MODE));
	if (rndr == NULL)
	    return ENODEV;
    }

    if (sw == scp->tsw) {
	error = (*sw->te_init)(scp, &scp->ts, SC_TE_WARM_INIT);
	scp->rndr = rndr;
	scp->rndr->init(scp);
	sc_clear_screen(scp);
	/* assert(error == 0); */
	return error;
    }

    if (sc_malloc && (sw->te_size > 0))
	p = malloc(sw->te_size, M_DEVBUF, M_NOWAIT);
    else
	p = NULL;
    error = (*sw->te_init)(scp, &p, SC_TE_COLD_INIT);
    if (error)
	return error;

    if (scp->tsw)
	(*scp->tsw->te_term)(scp, &scp->ts);
    if (scp->ts != NULL)
	free(scp->ts, M_DEVBUF);
    scp->tsw = sw;
    scp->ts = p;
    scp->rndr = rndr;
    scp->rndr->init(scp);

    (*sw->te_default_attr)(scp, SC_NORM_ATTR, SC_NORM_REV_ATTR);
    sc_clear_screen(scp);

    return 0;
}

/*
 * scgetc(flags) - get character from keyboard.
 * If flags & SCGETC_CN, then avoid harmful side effects.
 * If flags & SCGETC_NONBLOCK, then wait until a key is pressed, else
 * return NOKEY if there is nothing there.
 */
static u_int
scgetc(sc_softc_t *sc, u_int flags, struct sc_cnstate *sp)
{
    scr_stat *scp;
#ifndef SC_NO_HISTORY
    struct tty *tp;
#endif
    u_int c;
    int this_scr;
    int f;
    int i;

    if (sc->kbd == NULL)
	return NOKEY;

next_code:
#if 1
    /* I don't like this, but... XXX */
    if (flags & SCGETC_CN)
	sccnupdate(sc->cur_scp);
#endif
    scp = sc->cur_scp;
    /* first see if there is something in the keyboard port */
    for (;;) {
	if (flags & SCGETC_CN)
	    sccnscrunlock(sc, sp);
	c = kbdd_read_char(sc->kbd, !(flags & SCGETC_NONBLOCK));
	if (flags & SCGETC_CN)
	    sccnscrlock(sc, sp);
	if (c == ERRKEY) {
	    if (!(flags & SCGETC_CN))
		sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	} else if (c == NOKEY)
	    return c;
	else
	    break;
    }

    /* make screensaver happy */
    if (!(c & RELKEY))
	sc_touch_scrn_saver();

    if (!(flags & SCGETC_CN))
	random_harvest_queue(&c, sizeof(c), RANDOM_KEYBOARD);

    if (sc->kbd_open_level == 0 && scp->kbd_mode != K_XLATE)
	return KEYCHAR(c);

    /* if scroll-lock pressed allow history browsing */
    if (!ISGRAPHSC(scp) && scp->history && scp->status & SLKED) {

	scp->status &= ~CURSOR_ENABLED;
	sc_remove_cursor_image(scp);

#ifndef SC_NO_HISTORY
	if (!(scp->status & BUFFER_SAVED)) {
	    scp->status |= BUFFER_SAVED;
	    sc_hist_save(scp);
	}
	switch (c) {
	/* FIXME: key codes */
	case SPCLKEY | FKEY | F(49):  /* home key */
	    sc_remove_cutmarking(scp);
	    sc_hist_home(scp);
	    goto next_code;

	case SPCLKEY | FKEY | F(57):  /* end key */
	    sc_remove_cutmarking(scp);
	    sc_hist_end(scp);
	    goto next_code;

	case SPCLKEY | FKEY | F(50):  /* up arrow key */
	    sc_remove_cutmarking(scp);
	    if (sc_hist_up_line(scp))
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	    goto next_code;

	case SPCLKEY | FKEY | F(58):  /* down arrow key */
	    sc_remove_cutmarking(scp);
	    if (sc_hist_down_line(scp))
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	    goto next_code;

	case SPCLKEY | FKEY | F(51):  /* page up key */
	    sc_remove_cutmarking(scp);
	    for (i=0; i<scp->ysize; i++)
	    if (sc_hist_up_line(scp)) {
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
		break;
	    }
	    goto next_code;

	case SPCLKEY | FKEY | F(59):  /* page down key */
	    sc_remove_cutmarking(scp);
	    for (i=0; i<scp->ysize; i++)
	    if (sc_hist_down_line(scp)) {
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
		break;
	    }
	    goto next_code;
	}
#endif /* SC_NO_HISTORY */
    }

    /* 
     * Process and consume special keys here.  Return a plain char code
     * or a char code with the META flag or a function key code.
     */
    if (c & RELKEY) {
	/* key released */
	/* goto next_code */
    } else {
	/* key pressed */
	if (c & SPCLKEY) {
	    c &= ~SPCLKEY;
	    switch (KEYCHAR(c)) {
	    /* LOCKING KEYS */
	    case NLK: case CLK: case ALK:
		break;
	    case SLK:
		(void)kbdd_ioctl(sc->kbd, KDGKBSTATE, (caddr_t)&f);
		if (f & SLKED) {
		    scp->status |= SLKED;
		} else {
		    if (scp->status & SLKED) {
			scp->status &= ~SLKED;
#ifndef SC_NO_HISTORY
			if (scp->status & BUFFER_SAVED) {
			    if (!sc_hist_restore(scp))
				sc_remove_cutmarking(scp);
			    scp->status &= ~BUFFER_SAVED;
			    scp->status |= CURSOR_ENABLED;
			    sc_draw_cursor_image(scp);
			}
			/* Only safe in Giant-locked context. */
			tp = SC_DEV(sc, scp->index);
			if (!(flags & SCGETC_CN) && tty_opened_ns(tp))
			    sctty_outwakeup(tp);
#endif
		    }
		}
		break;

	    case PASTE:
#ifndef SC_NO_CUTPASTE
		sc_mouse_paste(scp);
#endif
		break;

	    /* NON-LOCKING KEYS */
	    case NOP:
	    case LSH:  case RSH:  case LCTR: case RCTR:
	    case LALT: case RALT: case ASH:  case META:
		break;

	    case BTAB:
		if (!(sc->flags & SC_SCRN_BLANKED))
		    return c;
		break;

	    case SPSC:
#ifdef DEV_SPLASH
		/* force activatation/deactivation of the screen saver */
		if (!(sc->flags & SC_SCRN_BLANKED)) {
		    run_scrn_saver = TRUE;
		    sc->scrn_time_stamp -= scrn_blank_time;
		}
		if (cold) {
		    /*
		     * While devices are being probed, the screen saver need
		     * to be invoked explicitly. XXX
		     */
		    if (sc->flags & SC_SCRN_BLANKED) {
			scsplash_stick(FALSE);
			stop_scrn_saver(sc, current_saver);
		    } else {
			if (!ISGRAPHSC(scp)) {
			    scsplash_stick(TRUE);
			    (*current_saver)(sc, TRUE);
			}
		    }
		}
#endif /* DEV_SPLASH */
		break;

	    case RBT:
#ifndef SC_DISABLE_REBOOT
		if (enable_reboot && !(flags & SCGETC_CN))
			shutdown_nice(0);
#endif
		break;

	    case HALT:
#ifndef SC_DISABLE_REBOOT
		if (enable_reboot && !(flags & SCGETC_CN))
			shutdown_nice(RB_HALT);
#endif
		break;

	    case PDWN:
#ifndef SC_DISABLE_REBOOT
		if (enable_reboot && !(flags & SCGETC_CN))
			shutdown_nice(RB_HALT|RB_POWEROFF);
#endif
		break;

	    case SUSP:
		power_pm_suspend(POWER_SLEEP_STATE_SUSPEND);
		break;
	    case STBY:
		power_pm_suspend(POWER_SLEEP_STATE_STANDBY);
		break;

	    case DBG:
#ifndef SC_DISABLE_KDBKEY
		if (enable_kdbkey)
			kdb_break();
#endif
		break;

	    case PNC:
		if (enable_panic_key)
			panic("Forced by the panic key");
		break;

	    case NEXT:
		this_scr = scp->index;
		for (i = (this_scr - sc->first_vty + 1)%sc->vtys;
			sc->first_vty + i != this_scr; 
			i = (i + 1)%sc->vtys) {
		    struct tty *tp = SC_DEV(sc, sc->first_vty + i);
		    if (tty_opened_ns(tp)) {
			sc_switch_scr(scp->sc, sc->first_vty + i);
			break;
		    }
		}
		break;

	    case PREV:
		this_scr = scp->index;
		for (i = (this_scr - sc->first_vty + sc->vtys - 1)%sc->vtys;
			sc->first_vty + i != this_scr;
			i = (i + sc->vtys - 1)%sc->vtys) {
		    struct tty *tp = SC_DEV(sc, sc->first_vty + i);
		    if (tty_opened_ns(tp)) {
			sc_switch_scr(scp->sc, sc->first_vty + i);
			break;
		    }
		}
		break;

	    default:
		if (KEYCHAR(c) >= F_SCR && KEYCHAR(c) <= L_SCR) {
		    sc_switch_scr(scp->sc, sc->first_vty + KEYCHAR(c) - F_SCR);
		    break;
		}
		/* assert(c & FKEY) */
		if (!(sc->flags & SC_SCRN_BLANKED))
		    return c;
		break;
	    }
	    /* goto next_code */
	} else {
	    /* regular keys (maybe MKEY is set) */
#if !defined(SC_DISABLE_KDBKEY) && defined(KDB)
	    if (enable_kdbkey)
		kdb_alt_break(c, &sc->sc_altbrk);
#endif
	    if (!(sc->flags & SC_SCRN_BLANKED))
		return c;
	}
    }

    goto next_code;
}

static int
sctty_mmap(struct tty *tp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
    scr_stat *scp;

    scp = sc_get_stat(tp);
    if (scp != scp->sc->cur_scp)
	return -1;
    return vidd_mmap(scp->sc->adp, offset, paddr, nprot, memattr);
}

static void
update_font(scr_stat *scp)
{
#ifndef SC_NO_FONT_LOADING
    /* load appropriate font */
    if (!(scp->status & GRAPHICS_MODE)) {
	if (!(scp->status & PIXEL_MODE) && ISFONTAVAIL(scp->sc->adp->va_flags)) {
	    if (scp->font_size < 14) {
		if (scp->sc->fonts_loaded & FONT_8)
		    sc_load_font(scp, 0, 8, 8, scp->sc->font_8, 0, 256);
	    } else if (scp->font_size >= 16) {
		if (scp->sc->fonts_loaded & FONT_16)
		    sc_load_font(scp, 0, 16, 8, scp->sc->font_16, 0, 256);
	    } else {
		if (scp->sc->fonts_loaded & FONT_14)
		    sc_load_font(scp, 0, 14, 8, scp->sc->font_14, 0, 256);
	    }
	    /*
	     * FONT KLUDGE:
	     * This is an interim kludge to display correct font.
	     * Always use the font page #0 on the video plane 2.
	     * Somehow we cannot show the font in other font pages on
	     * some video cards... XXX
	     */ 
	    sc_show_font(scp, 0);
	}
	mark_all(scp);
    }
#endif /* !SC_NO_FONT_LOADING */
}

static int
save_kbd_state(scr_stat *scp)
{
    int state;
    int error;

    error = kbdd_ioctl(scp->sc->kbd, KDGKBSTATE, (caddr_t)&state);
    if (error == ENOIOCTL)
	error = ENODEV;
    if (error == 0) {
	scp->status &= ~LOCK_MASK;
	scp->status |= state;
    }
    return error;
}

static int
update_kbd_state(scr_stat *scp, int new_bits, int mask)
{
    int state;
    int error;

    if (mask != LOCK_MASK) {
	error = kbdd_ioctl(scp->sc->kbd, KDGKBSTATE, (caddr_t)&state);
	if (error == ENOIOCTL)
	    error = ENODEV;
	if (error)
	    return error;
	state &= ~mask;
	state |= new_bits & mask;
    } else {
	state = new_bits & LOCK_MASK;
    }
    error = kbdd_ioctl(scp->sc->kbd, KDSKBSTATE, (caddr_t)&state);
    if (error == ENOIOCTL)
	error = ENODEV;
    return error;
}

static int
update_kbd_leds(scr_stat *scp, int which)
{
    int error;

    which &= LOCK_MASK;
    error = kbdd_ioctl(scp->sc->kbd, KDSETLED, (caddr_t)&which);
    if (error == ENOIOCTL)
	error = ENODEV;
    return error;
}

int
set_mode(scr_stat *scp)
{
    video_info_t info;

    /* reject unsupported mode */
    if (vidd_get_info(scp->sc->adp, scp->mode, &info))
	return 1;

    /* if this vty is not currently showing, do nothing */
    if (scp != scp->sc->cur_scp)
	return 0;

    /* setup video hardware for the given mode */
    vidd_set_mode(scp->sc->adp, scp->mode);
    scp->rndr->init(scp);
#ifndef __sparc64__
    sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		(void *)scp->sc->adp->va_window, FALSE);
#endif

    update_font(scp);

    sc_set_border(scp, scp->border);
    sc_set_cursor_image(scp);

    return 0;
}

void
sc_set_border(scr_stat *scp, int color)
{
    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->draw_border)(scp, color);
    SC_VIDEO_UNLOCK(scp->sc);
}

#ifndef SC_NO_FONT_LOADING
void
sc_load_font(scr_stat *scp, int page, int size, int width, u_char *buf,
	     int base, int count)
{
    sc_softc_t *sc;

    sc = scp->sc;
    sc->font_loading_in_progress = TRUE;
    vidd_load_font(sc->adp, page, size, width, buf, base, count);
    sc->font_loading_in_progress = FALSE;
}

void
sc_save_font(scr_stat *scp, int page, int size, int width, u_char *buf,
	     int base, int count)
{
    sc_softc_t *sc;

    sc = scp->sc;
    sc->font_loading_in_progress = TRUE;
    vidd_save_font(sc->adp, page, size, width, buf, base, count);
    sc->font_loading_in_progress = FALSE;
}

void
sc_show_font(scr_stat *scp, int page)
{
    vidd_show_font(scp->sc->adp, page);
}
#endif /* !SC_NO_FONT_LOADING */

void
sc_paste(scr_stat *scp, const u_char *p, int count) 
{
    struct tty *tp;
    u_char *rmap;

    tp = SC_DEV(scp->sc, scp->sc->cur_scp->index);
    if (!tty_opened_ns(tp))
	return;
    rmap = scp->sc->scr_rmap;
    for (; count > 0; --count)
	ttydisc_rint(tp, rmap[*p++], 0);
    ttydisc_rint_done(tp);
}

void
sc_respond(scr_stat *scp, const u_char *p, int count, int wakeup) 
{
    struct tty *tp;

    tp = SC_DEV(scp->sc, scp->sc->cur_scp->index);
    if (!tty_opened_ns(tp))
	return;
    ttydisc_rint_simple(tp, p, count);
    if (wakeup) {
	/* XXX: we can't always call ttydisc_rint_done() here! */
	ttydisc_rint_done(tp);
    }
}

void
sc_bell(scr_stat *scp, int pitch, int duration)
{
    if (cold || kdb_active || shutdown_in_progress || !enable_bell)
	return;

    if (scp != scp->sc->cur_scp && (scp->sc->flags & SC_QUIET_BELL))
	return;

    if (scp->sc->flags & SC_VISUAL_BELL) {
	if (scp->sc->blink_in_progress)
	    return;
	scp->sc->blink_in_progress = 3;
	if (scp != scp->sc->cur_scp)
	    scp->sc->blink_in_progress += 2;
	blink_screen(scp->sc->cur_scp);
    } else if (duration != 0 && pitch != 0) {
	if (scp != scp->sc->cur_scp)
	    pitch *= 2;
	sysbeep(1193182 / pitch, duration);
    }
}

static int
sc_kattr(void)
{
    if (sc_console == NULL)
	return (SC_KERNEL_CONS_ATTR);	/* for very early, before pcpu */
    return (sc_kattrtab[curcpu % nitems(sc_kattrtab)]);
}

static void
blink_screen(void *arg)
{
    scr_stat *scp = arg;
    struct tty *tp;

    if (ISGRAPHSC(scp) || (scp->sc->blink_in_progress <= 1)) {
	scp->sc->blink_in_progress = 0;
    	mark_all(scp);
	tp = SC_DEV(scp->sc, scp->index);
	if (tty_opened_ns(tp))
	    sctty_outwakeup(tp);
	if (scp->sc->delayed_next_scr)
	    sc_switch_scr(scp->sc, scp->sc->delayed_next_scr - 1);
    }
    else {
	(*scp->rndr->draw)(scp, 0, scp->xsize*scp->ysize, 
			   scp->sc->blink_in_progress & 1);
	scp->sc->blink_in_progress--;
	callout_reset_sbt(&scp->sc->cblink, SBT_1S / 15, 0,
	    blink_screen, scp, C_PREL(0));
    }
}

/*
 * Until sc_attach_unit() gets called no dev structures will be available
 * to store the per-screen current status.  This is the case when the
 * kernel is initially booting and needs access to its console.  During
 * this early phase of booting the console's current status is kept in
 * one statically defined scr_stat structure, and any pointers to the
 * dev structures will be NULL.
 */

static scr_stat *
sc_get_stat(struct tty *tp)
{
	if (tp == NULL)
		return (&main_console);
	return (SC_STAT(tp));
}

/*
 * Allocate active keyboard. Try to allocate "kbdmux" keyboard first, and,
 * if found, add all non-busy keyboards to "kbdmux". Otherwise look for
 * any keyboard.
 */

static int
sc_allocate_keyboard(sc_softc_t *sc, int unit)
{
	int		 idx0, idx;
	keyboard_t	*k0, *k;
	keyboard_info_t	 ki;

	idx0 = kbd_allocate("kbdmux", -1, (void *)&sc->keyboard, sckbdevent, sc);
	if (idx0 != -1) {
		k0 = kbd_get_keyboard(idx0);

		for (idx = kbd_find_keyboard2("*", -1, 0);
		     idx != -1;
		     idx = kbd_find_keyboard2("*", -1, idx + 1)) {
			k = kbd_get_keyboard(idx);

			if (idx == idx0 || KBD_IS_BUSY(k))
				continue;

			bzero(&ki, sizeof(ki));
			strcpy(ki.kb_name, k->kb_name);
			ki.kb_unit = k->kb_unit;

			(void)kbdd_ioctl(k0, KBADDKBD, (caddr_t) &ki);
		}
	} else
		idx0 = kbd_allocate("*", unit, (void *)&sc->keyboard, sckbdevent, sc);

	return (idx0);
}
