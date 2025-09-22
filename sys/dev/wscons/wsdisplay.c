/* $OpenBSD: wsdisplay.c,v 1.155 2025/08/04 15:00:57 kettenis Exp $ */
/* $NetBSD: wsdisplay.c,v 1.82 2005/02/27 00:27:52 perry Exp $ */

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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/task.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/timeout.h>

#include <dev/wscons/wscons_features.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsksymvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/wscons_callbacks.h>
#include <dev/cons.h>

#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmux.h"

#if NWSKBD > 0
#include <dev/wscons/wseventvar.h>
#include <dev/wscons/wsmuxvar.h>
#endif

#ifdef DDB
#include <ddb/db_output.h>
#endif

#include "wsmoused.h"

struct wsscreen_internal {
	const struct wsdisplay_emulops *emulops;
	void	*emulcookie;

	const struct wsscreen_descr *scrdata;

	const struct wsemul_ops *wsemul;
	void	*wsemulcookie;
};

struct wsscreen {
	struct wsscreen_internal *scr_dconf;

	struct task	scr_emulbell_task;

	struct tty *scr_tty;
	int	scr_hold_screen;		/* hold tty output */

	int scr_flags;
#define SCR_OPEN 1		/* is it open? */
#define SCR_WAITACTIVE 2	/* someone waiting on activation */
#define SCR_GRAPHICS 4		/* graphics mode, no text (emulation) output */
#define	SCR_DUMBFB 8		/* in use as dumb fb (iff SCR_GRAPHICS) */

#ifdef WSDISPLAY_COMPAT_USL
	const struct wscons_syncops *scr_syncops;
	void *scr_synccookie;
#endif

#ifdef WSDISPLAY_COMPAT_RAWKBD
	int scr_rawkbd;
#endif

	struct wsdisplay_softc *sc;

#ifdef HAVE_WSMOUSED_SUPPORT
	/* mouse console support via wsmoused(8) */
	u_int mouse;		/* mouse cursor position */
	u_int cursor;		/* selection cursor position (if
				   different from mouse cursor pos) */
	u_int cpy_start;	/* position of the copy start mark*/
	u_int cpy_end;		/* position of the copy end mark */
	u_int orig_start;	/* position of the original sel. start*/
	u_int orig_end;		/* position of the original sel. end */

	u_int mouse_flags;	/* flags, status of the mouse */
#define MOUSE_VISIBLE	0x01	/* flag, the mouse cursor is visible */
#define SEL_EXISTS	0x02	/* flag, a selection exists */
#define SEL_IN_PROGRESS 0x04	/* flag, a selection is in progress */
#define SEL_EXT_AFTER	0x08	/* flag, selection is extended after */
#define BLANK_TO_EOL	0x10	/* flag, there are only blanks
				   characters to eol */
#define SEL_BY_CHAR	0x20	/* flag, select character by character*/
#define SEL_BY_WORD	0x40	/* flag, select word by word */
#define SEL_BY_LINE	0x80	/* flag, select line by line */

#define IS_MOUSE_VISIBLE(scr)	((scr)->mouse_flags & MOUSE_VISIBLE)
#define IS_SEL_EXISTS(scr)	((scr)->mouse_flags & SEL_EXISTS)
#define IS_SEL_IN_PROGRESS(scr)	((scr)->mouse_flags & SEL_IN_PROGRESS)
#define IS_SEL_EXT_AFTER(scr)	((scr)->mouse_flags & SEL_EXT_AFTER)
#define IS_BLANK_TO_EOL(scr)	((scr)->mouse_flags & BLANK_TO_EOL)
#define IS_SEL_BY_CHAR(scr)	((scr)->mouse_flags & SEL_BY_CHAR)
#define IS_SEL_BY_WORD(scr)	((scr)->mouse_flags & SEL_BY_WORD)
#define IS_SEL_BY_LINE(scr)	((scr)->mouse_flags & SEL_BY_LINE)
#endif	/* HAVE_WSMOUSED_SUPPORT */
};

struct wsscreen *wsscreen_attach(struct wsdisplay_softc *, int, const char *,
	    const struct wsscreen_descr *, void *, int, int, uint32_t);
void	wsscreen_detach(struct wsscreen *);
int	wsdisplay_addscreen(struct wsdisplay_softc *, int, const char *,
	    const char *);
int	wsdisplay_getscreen(struct wsdisplay_softc *,
	    struct wsdisplay_addscreendata *);
void	wsdisplay_resume_device(struct device *);
void	wsdisplay_suspend_device(struct device *);
void	wsdisplay_addscreen_print(struct wsdisplay_softc *, int, int);
void	wsdisplay_closescreen(struct wsdisplay_softc *, struct wsscreen *);
int	wsdisplay_delscreen(struct wsdisplay_softc *, int, int);
int	wsdisplay_driver_ioctl(struct wsdisplay_softc *, u_long, caddr_t,
	    int, struct proc *);

void	wsdisplay_burner_setup(struct wsdisplay_softc *, struct wsscreen *);
void	wsdisplay_burner(void *v);

struct wsdisplay_softc {
	struct device sc_dv;

	const struct wsdisplay_accessops *sc_accessops;
	void	*sc_accesscookie;

	const struct wsscreen_list *sc_scrdata;

	struct wsscreen *sc_scr[WSDISPLAY_MAXSCREEN];
	int sc_focusidx;	/* available only if sc_focus isn't null */
	struct wsscreen *sc_focus;

	struct taskq *sc_taskq;

#ifdef HAVE_BURNER_SUPPORT
	struct timeout sc_burner;
	int	sc_burnoutintvl;	/* delay before blanking (ms) */
	int	sc_burninintvl;		/* delay before unblanking (ms) */
	int	sc_burnout;		/* current sc_burner delay (ms) */
	int	sc_burnman;		/* nonzero if screen blanked */
	int	sc_burnflags;
#endif

	int	sc_isconsole;

	int sc_flags;
#define SC_SWITCHPENDING	0x01
#define	SC_PASTE_AVAIL		0x02
	int sc_screenwanted, sc_oldscreen; /* valid with SC_SWITCHPENDING */
	int sc_resumescreen; /* if set, can't switch until resume. */

#if NWSKBD > 0
	struct wsevsrc *sc_input;
#ifdef WSDISPLAY_COMPAT_RAWKBD
	int sc_rawkbd;
#endif
#endif /* NWSKBD > 0 */

#ifdef HAVE_WSMOUSED_SUPPORT
	char *sc_copybuffer;
	u_int sc_copybuffer_size;
#endif
};

extern struct cfdriver wsdisplay_cd;

/* Autoconfiguration definitions. */
int	wsdisplay_match(struct device *, void *, void *);
void	wsdisplay_attach(struct device *, struct device *, void *);
int	wsdisplay_detach(struct device *, int);

int	wsdisplay_activate(struct device *, int);

void	wsdisplay_emulbell_task(void *);

struct cfdriver wsdisplay_cd = {
	NULL, "wsdisplay", DV_TTY
};

const struct cfattach wsdisplay_ca = {
	sizeof(struct wsdisplay_softc), wsdisplay_match,
	wsdisplay_attach, wsdisplay_detach, wsdisplay_activate
};

void	wsdisplaystart(struct tty *);
int	wsdisplayparam(struct tty *, struct termios *);

/* Internal macros, functions, and variables. */
#define	WSDISPLAYUNIT(dev)		(minor(dev) >> 8)
#define	WSDISPLAYSCREEN(dev)		(minor(dev) & 0xff)
#define ISWSDISPLAYCTL(dev)		(WSDISPLAYSCREEN(dev) == 255)
#define WSDISPLAYMINOR(unit, screen)	(((unit) << 8) | (screen))

#define	WSSCREEN_HAS_TTY(scr)		((scr)->scr_tty != NULL)

void	wsdisplay_kbdholdscr(struct wsscreen *, int);

#ifdef WSDISPLAY_COMPAT_RAWKBD
int	wsdisplay_update_rawkbd(struct wsdisplay_softc *, struct wsscreen *);
#endif

int	wsdisplay_console_initted;
struct wsdisplay_softc *wsdisplay_console_device;
struct wsscreen_internal wsdisplay_console_conf;

int	wsdisplay_getc_dummy(dev_t);
void	wsdisplay_pollc(dev_t, int);

int	wsdisplay_cons_pollmode;
void	(*wsdisplay_cons_kbd_pollc)(dev_t, int);

struct consdev wsdisplay_cons = {
	NULL, NULL, wsdisplay_getc_dummy, wsdisplay_cnputc,
	    wsdisplay_pollc, NULL, NODEV, CN_LOWPRI
};

/*
 * Function pointers for wsconsctl parameter handling.
 * These are used for firmware-provided display brightness control.
 */
int	(*ws_get_param)(struct wsdisplay_param *);
int	(*ws_set_param)(struct wsdisplay_param *);


#ifndef WSDISPLAY_DEFAULTSCREENS
#define WSDISPLAY_DEFAULTSCREENS	1
#endif
int	wsdisplay_defaultscreens = WSDISPLAY_DEFAULTSCREENS;

int	wsdisplay_switch1(void *, int, int);
int	wsdisplay_switch2(void *, int, int);
int	wsdisplay_switch3(void *, int, int);

int	wsdisplay_clearonclose;

struct wsscreen *
wsscreen_attach(struct wsdisplay_softc *sc, int console, const char *emul,
    const struct wsscreen_descr *type, void *cookie, int ccol, int crow,
    uint32_t defattr)
{
	struct wsscreen_internal *dconf;
	struct wsscreen *scr;

	scr = malloc(sizeof(*scr), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (!scr)
		return (NULL);

	if (console) {
		dconf = &wsdisplay_console_conf;
		/*
		 * Tell the emulation about the callback argument.
		 * The other stuff is already there.
		 */
		(void)(*dconf->wsemul->attach)(1, 0, 0, 0, 0, scr, 0);
	} else { /* not console */
		dconf = malloc(sizeof(*dconf), M_DEVBUF, M_NOWAIT);
		if (dconf == NULL)
			goto fail;
		dconf->emulops = type->textops;
		dconf->emulcookie = cookie;
		if (dconf->emulops == NULL ||
		    (dconf->wsemul = wsemul_pick(emul)) == NULL)
			goto fail;
		dconf->wsemulcookie = (*dconf->wsemul->attach)(0, type, cookie,
		    ccol, crow, scr, defattr);
		if (dconf->wsemulcookie == NULL)
			goto fail;
		dconf->scrdata = type;
	}

	task_set(&scr->scr_emulbell_task, wsdisplay_emulbell_task, scr);
	scr->scr_dconf = dconf;
	scr->scr_tty = ttymalloc(0);
	scr->sc = sc;
	return (scr);

fail:
	if (dconf != NULL)
		free(dconf, M_DEVBUF, sizeof(*dconf));
	free(scr, M_DEVBUF, sizeof(*scr));
	return (NULL);
}

void
wsscreen_detach(struct wsscreen *scr)
{
	int ccol, crow; /* XXX */

	if (WSSCREEN_HAS_TTY(scr)) {
		timeout_del(&scr->scr_tty->t_rstrt_to);
		ttyfree(scr->scr_tty);
	}
	(*scr->scr_dconf->wsemul->detach)(scr->scr_dconf->wsemulcookie,
	    &ccol, &crow);
	taskq_del_barrier(scr->sc->sc_taskq, &scr->scr_emulbell_task);
	free(scr->scr_dconf, M_DEVBUF, sizeof(*scr->scr_dconf));
	free(scr, M_DEVBUF, sizeof(*scr));
}

const struct wsscreen_descr *
wsdisplay_screentype_pick(const struct wsscreen_list *scrdata, const char *name)
{
	int i;
	const struct wsscreen_descr *scr;

	KASSERT(scrdata->nscreens > 0);

	if (name == NULL || *name == '\0')
		return (scrdata->screens[0]);

	for (i = 0; i < scrdata->nscreens; i++) {
		scr = scrdata->screens[i];
		if (!strncmp(name, scr->name, WSSCREEN_NAME_SIZE))
			return (scr);
	}

	return (0);
}

/*
 * print info about attached screen
 */
void
wsdisplay_addscreen_print(struct wsdisplay_softc *sc, int idx, int count)
{
	printf("%s: screen %d", sc->sc_dv.dv_xname, idx);
	if (count > 1)
		printf("-%d", idx + (count-1));
	printf(" added (%s, %s emulation)\n",
	    sc->sc_scr[idx]->scr_dconf->scrdata->name,
	    sc->sc_scr[idx]->scr_dconf->wsemul->name);
}

int
wsdisplay_addscreen(struct wsdisplay_softc *sc, int idx,
    const char *screentype, const char *emul)
{
	const struct wsscreen_descr *scrdesc;
	int error;
	void *cookie;
	int ccol, crow;
	uint32_t defattr;
	struct wsscreen *scr;
	int s;

	if (idx < 0 || idx >= WSDISPLAY_MAXSCREEN)
		return (EINVAL);
	if (sc->sc_scr[idx] != NULL)
		return (EBUSY);

	scrdesc = wsdisplay_screentype_pick(sc->sc_scrdata, screentype);
	if (!scrdesc)
		return (ENXIO);
	error = (*sc->sc_accessops->alloc_screen)(sc->sc_accesscookie,
	    scrdesc, &cookie, &ccol, &crow, &defattr);
	if (error)
		return (error);

	scr = wsscreen_attach(sc, 0, emul, scrdesc,
	    cookie, ccol, crow, defattr);
	if (scr == NULL) {
		(*sc->sc_accessops->free_screen)(sc->sc_accesscookie, cookie);
		return (ENXIO);
	}

	sc->sc_scr[idx] = scr;

	/* if no screen has focus yet, activate the first we get */
	s = spltty();
	if (!sc->sc_focus) {
		(*sc->sc_accessops->show_screen)(sc->sc_accesscookie,
		    scr->scr_dconf->emulcookie, 0, 0, 0);
		sc->sc_focusidx = idx;
		sc->sc_focus = scr;
	}
	splx(s);

#ifdef HAVE_WSMOUSED_SUPPORT
	allocate_copybuffer(sc); /* enlarge the copy buffer if necessary */
#endif
	return (0);
}

int
wsdisplay_getscreen(struct wsdisplay_softc *sc,
    struct wsdisplay_addscreendata *sd)
{
	struct wsscreen *scr;

	if (sd->idx < 0 && sc->sc_focus)
		sd->idx = sc->sc_focusidx;

	if (sd->idx < 0 || sd->idx >= WSDISPLAY_MAXSCREEN)
		return (EINVAL);

	scr = sc->sc_scr[sd->idx];
	if (scr == NULL)
		return (ENXIO);

	strlcpy(sd->screentype, scr->scr_dconf->scrdata->name,
	    WSSCREEN_NAME_SIZE);
	strlcpy(sd->emul, scr->scr_dconf->wsemul->name, WSEMUL_NAME_SIZE);

	return (0);
}

void
wsdisplay_closescreen(struct wsdisplay_softc *sc, struct wsscreen *scr)
{
	int maj, mn, idx;

	/* hangup */
	if (WSSCREEN_HAS_TTY(scr)) {
		struct tty *tp = scr->scr_tty;
		(*linesw[tp->t_line].l_modem)(tp, 0);
	}

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == wsdisplayopen)
			break;
	/* locate the screen index */
	for (idx = 0; idx < WSDISPLAY_MAXSCREEN; idx++)
		if (scr == sc->sc_scr[idx])
			break;
#ifdef DIAGNOSTIC
	if (idx == WSDISPLAY_MAXSCREEN)
		panic("wsdisplay_forceclose: bad screen");
#endif

	/* nuke the vnodes */
	mn = WSDISPLAYMINOR(sc->sc_dv.dv_unit, idx);
	vdevgone(maj, mn, mn, VCHR);
}

int
wsdisplay_delscreen(struct wsdisplay_softc *sc, int idx, int flags)
{
	struct wsscreen *scr;
	int s;
	void *cookie;

	if (idx < 0 || idx >= WSDISPLAY_MAXSCREEN)
		return (EINVAL);
	if ((scr = sc->sc_scr[idx]) == NULL)
		return (ENXIO);

	if (scr->scr_dconf == &wsdisplay_console_conf ||
#ifdef WSDISPLAY_COMPAT_USL
	    scr->scr_syncops ||
#endif
	    ((scr->scr_flags & SCR_OPEN) && !(flags & WSDISPLAY_DELSCR_FORCE)))
		return (EBUSY);

	wsdisplay_closescreen(sc, scr);

	/*
	 * delete pointers, so neither device entries
	 * nor keyboard input can reference it anymore
	 */
	s = spltty();
	if (sc->sc_focus == scr) {
		sc->sc_focus = NULL;
#ifdef WSDISPLAY_COMPAT_RAWKBD
		wsdisplay_update_rawkbd(sc, 0);
#endif
	}
	sc->sc_scr[idx] = NULL;
	splx(s);

	/*
	 * Wake up processes waiting for the screen to
	 * be activated. Sleepers must check whether
	 * the screen still exists.
	 */
	if (scr->scr_flags & SCR_WAITACTIVE)
		wakeup(scr);

	/* save a reference to the graphics screen */
	cookie = scr->scr_dconf->emulcookie;

	wsscreen_detach(scr);

	(*sc->sc_accessops->free_screen)(sc->sc_accesscookie, cookie);

	if ((flags & WSDISPLAY_DELSCR_QUIET) == 0)
		printf("%s: screen %d deleted\n", sc->sc_dv.dv_xname, idx);
	return (0);
}

/*
 * Autoconfiguration functions.
 */
int
wsdisplay_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct wsemuldisplaydev_attach_args *ap = aux;

	if (cf->wsemuldisplaydevcf_console != WSEMULDISPLAYDEVCF_CONSOLE_UNK) {
		/*
		 * If console-ness of device specified, either match
		 * exactly (at high priority), or fail.
		 */
		if (cf->wsemuldisplaydevcf_console != 0 && ap->console != 0)
			return (10);
		else
			return (0);
	}

	if (cf->wsemuldisplaydevcf_primary != WSEMULDISPLAYDEVCF_PRIMARY_UNK) {
		/*
		 * If primary-ness of device specified, either match
		 * exactly (at high priority), or fail.
		 */
		if (cf->wsemuldisplaydevcf_primary != 0 && ap->primary != 0)
			return (10);
		else
			return (0);
	}

	/* If console-ness and primary-ness unspecified, it wins. */
	return (1);
}

int
wsdisplay_activate(struct device *self, int act)
{
	int ret = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		wsdisplay_switchtoconsole();
		break;
	}

	return (ret);
}

/*
 * Detach a display.
 */
int
wsdisplay_detach(struct device *self, int flags)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)self;
	int i;
	int rc;

	/* We don't support detaching the console display yet. */
	if (sc->sc_isconsole)
		return (EBUSY);

	/* Delete all screens managed by this display */
	for (i = 0; i < WSDISPLAY_MAXSCREEN; i++)
		if (sc->sc_scr[i] != NULL) {
			if ((rc = wsdisplay_delscreen(sc, i,
			    WSDISPLAY_DELSCR_QUIET | (flags & DETACH_FORCE ?
			     WSDISPLAY_DELSCR_FORCE : 0))) != 0)
				return (rc);
		}

#ifdef HAVE_BURNER_SUPPORT
	timeout_del(&sc->sc_burner);
#endif

#if NWSKBD > 0
	if (sc->sc_input != NULL) {
#if NWSMUX > 0
		/*
		 * If we are the display of the mux we are attached to,
		 * disconnect all input devices from us.
		 */
		if (sc->sc_input->me_dispdv == &sc->sc_dv) {
			if ((rc = wsmux_set_display((struct wsmux_softc *)
						    sc->sc_input, NULL)) != 0)
				return (rc);
		}

		/*
		 * XXX
		 * If we created a standalone mux (dmux), we should destroy it
		 * there, but there is currently no support for this in wsmux.
		 */
#else
		if ((rc = wskbd_set_display((struct device *)sc->sc_input,
		    NULL)) != 0)
			return (rc);
#endif
	}
#endif

	taskq_destroy(sc->sc_taskq);

	return (0);
}

/* Print function (for parent devices). */
int
wsemuldisplaydevprint(void *aux, const char *pnp)
{
#if 0 /* -Wunused */
	struct wsemuldisplaydev_attach_args *ap = aux;
#endif

	if (pnp)
		printf("wsdisplay at %s", pnp);
#if 0 /* don't bother; it's ugly */
	printf(" console %d", ap->console);
#endif

	return (UNCONF);
}

/* Submatch function (for parent devices). */
int
wsemuldisplaydevsubmatch(struct device *parent, void *match, void *aux)
{
	extern struct cfdriver wsdisplay_cd;
	struct cfdata *cf = match;

	/* only allow wsdisplay to attach */
	if (cf->cf_driver == &wsdisplay_cd)
		return ((*cf->cf_attach->ca_match)(parent, match, aux));

	return (0);
}

void
wsdisplay_attach(struct device *parent, struct device *self, void *aux)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)self;
	struct wsemuldisplaydev_attach_args *ap = aux;
	u_int defaultscreens = ap->defaultscreens;
	int i, start = 0;
#if NWSKBD > 0
	struct wsevsrc *kme;
#if NWSMUX > 0
	int kbdmux = sc->sc_dv.dv_cfdata->wsemuldisplaydevcf_mux;
	struct wsmux_softc *mux;

	if (kbdmux >= 0)
		mux = wsmux_getmux(kbdmux);
	else
		mux = wsmux_create("dmux", sc->sc_dv.dv_unit);
	/* XXX panic()ing isn't nice, but attach cannot fail */
	if (mux == NULL)
		panic("wsdisplay_common_attach: no memory");
	sc->sc_input = &mux->sc_base;

	if (kbdmux >= 0)
		printf(" mux %d", kbdmux);
#else
#if 0	/* not worth keeping, especially since the default value is not -1... */
	if (kbdmux >= 0)
		printf(" (mux ignored)");
#endif
#endif	/* NWSMUX > 0 */
#endif	/* NWSKBD > 0 */

	sc->sc_isconsole = ap->console;
	sc->sc_resumescreen = WSDISPLAY_NULLSCREEN;

	sc->sc_taskq = taskq_create(sc->sc_dv.dv_xname, 1, IPL_TTY, 0);

	if (ap->console) {
		KASSERT(wsdisplay_console_initted);
		KASSERT(wsdisplay_console_device == NULL);

		sc->sc_scr[0] = wsscreen_attach(sc, 1, 0, 0, 0, 0, 0, 0);
		if (sc->sc_scr[0] == NULL)
			return;
		wsdisplay_console_device = sc;

		printf(": console (%s, %s emulation)",
		       wsdisplay_console_conf.scrdata->name,
		       wsdisplay_console_conf.wsemul->name);

#if NWSKBD > 0
		kme = wskbd_set_console_display(&sc->sc_dv, sc->sc_input);
		if (kme != NULL)
			printf(", using %s", kme->me_dv.dv_xname);
#if NWSMUX == 0
		sc->sc_input = kme;
#endif
#endif

		sc->sc_focusidx = 0;
		sc->sc_focus = sc->sc_scr[0];
		start = 1;
	}
	printf("\n");

#if NWSKBD > 0 && NWSMUX > 0
	/*
	 * If this mux did not have a display device yet, volunteer for
	 * the job.
	 */
	if (mux->sc_displaydv == NULL)
		wsmux_set_display(mux, &sc->sc_dv);
#endif

	sc->sc_accessops = ap->accessops;
	sc->sc_accesscookie = ap->accesscookie;
	sc->sc_scrdata = ap->scrdata;

	/*
	 * Set up a number of virtual screens if wanted. The
	 * WSDISPLAYIO_ADDSCREEN ioctl is more flexible, so this code
	 * is for special cases like installation kernels, as well as
	 * sane multihead defaults.
	 */
	if (defaultscreens == 0)
		defaultscreens = wsdisplay_defaultscreens;
	for (i = start; i < defaultscreens; i++) {
		if (wsdisplay_addscreen(sc, i, 0, 0))
			break;
	}

	if (i > start)
		wsdisplay_addscreen_print(sc, start, i-start);

#ifdef HAVE_BURNER_SUPPORT
	sc->sc_burnoutintvl = WSDISPLAY_DEFBURNOUT_MSEC;
	sc->sc_burninintvl = WSDISPLAY_DEFBURNIN_MSEC;
	sc->sc_burnflags = WSDISPLAY_BURN_OUTPUT | WSDISPLAY_BURN_KBD |
	    WSDISPLAY_BURN_MOUSE;
	timeout_set(&sc->sc_burner, wsdisplay_burner, sc);
	sc->sc_burnout = sc->sc_burnoutintvl;
	wsdisplay_burn(sc, sc->sc_burnflags);
#endif

#if NWSKBD > 0 && NWSMUX == 0
	if (ap->console == 0) {
		/*
		 * In the non-wsmux world, always connect wskbd0 and wsdisplay0
		 * together.
		 */
		extern struct cfdriver wskbd_cd;

		if (wskbd_cd.cd_ndevs != 0 && sc->sc_dv.dv_unit == 0) {
			if (wsdisplay_set_kbd(&sc->sc_dv,
			    (struct wsevsrc *)wskbd_cd.cd_devs[0]) == 0)
				wskbd_set_display(wskbd_cd.cd_devs[0],
				    &sc->sc_dv);
		}
	}
#endif

	if (ap->console && cn_tab == &wsdisplay_cons) {
		int maj;

		/* locate the major number */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == wsdisplayopen)
				break;

		cn_tab->cn_dev = makedev(maj, WSDISPLAYMINOR(self->dv_unit, 0));
	}
}

void
wsdisplay_cnattach(const struct wsscreen_descr *type, void *cookie, int ccol,
    int crow, uint32_t defattr)
{
	const struct wsemul_ops *wsemul;
	const struct wsdisplay_emulops *emulops;

	KASSERT(type->nrows > 0);
	KASSERT(type->ncols > 0);
	KASSERT(crow < type->nrows);
	KASSERT(ccol < type->ncols);

	wsdisplay_console_conf.emulops = emulops = type->textops;
	wsdisplay_console_conf.emulcookie = cookie;
	wsdisplay_console_conf.scrdata = type;

#ifdef WSEMUL_DUMB
	/*
	 * If the emulops structure is crippled, force a dumb emulation.
	 */
	if (emulops->cursor == NULL ||
	    emulops->copycols == NULL || emulops->copyrows == NULL ||
	    emulops->erasecols == NULL || emulops->eraserows == NULL)
		wsemul = wsemul_pick("dumb");
	else
#endif
		wsemul = wsemul_pick("");
	wsdisplay_console_conf.wsemul = wsemul;
	wsdisplay_console_conf.wsemulcookie =
	    (*wsemul->cnattach)(type, cookie, ccol, crow, defattr);

	if (!wsdisplay_console_initted)
		cn_tab = &wsdisplay_cons;

	wsdisplay_console_initted = 1;

#ifdef DDB
	db_resize(type->ncols, type->nrows);
#endif
}

/*
 * Tty and cdevsw functions.
 */
int
wsdisplayopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct wsdisplay_softc *sc;
	struct tty *tp;
	int unit, newopen, error;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	if (unit >= wsdisplay_cd.cd_ndevs ||	/* make sure it was attached */
	    (sc = wsdisplay_cd.cd_devs[unit]) == NULL)
		return (ENXIO);

	if (ISWSDISPLAYCTL(dev))
		return (0);

	if (WSDISPLAYSCREEN(dev) >= WSDISPLAY_MAXSCREEN)
		return (ENXIO);
	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (WSSCREEN_HAS_TTY(scr)) {
		tp = scr->scr_tty;
		tp->t_oproc = wsdisplaystart;
		tp->t_param = wsdisplayparam;
		tp->t_dev = dev;
		newopen = (tp->t_state & TS_ISOPEN) == 0;
		if (newopen) {
			ttychars(tp);
			tp->t_iflag = TTYDEF_IFLAG;
			tp->t_oflag = TTYDEF_OFLAG;
			tp->t_cflag = TTYDEF_CFLAG;
			tp->t_lflag = TTYDEF_LFLAG;
			tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
			wsdisplayparam(tp, &tp->t_termios);
			ttsetwater(tp);
		} else if ((tp->t_state & TS_XCLUDE) != 0 &&
			   suser(p) != 0)
			return (EBUSY);
		tp->t_state |= TS_CARR_ON;

		error = ((*linesw[tp->t_line].l_open)(dev, tp, p));
		if (error)
			return (error);

		if (newopen) {
			/* set window sizes as appropriate, and reset
			   the emulation */
			tp->t_winsize.ws_row = scr->scr_dconf->scrdata->nrows;
			tp->t_winsize.ws_col = scr->scr_dconf->scrdata->ncols;
		}
	}

	scr->scr_flags |= SCR_OPEN;
	return (0);
}

int
wsdisplayclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct wsdisplay_softc *sc;
	struct tty *tp;
	int unit;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	sc = wsdisplay_cd.cd_devs[unit];

	if (ISWSDISPLAYCTL(dev))
		return (0);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (WSSCREEN_HAS_TTY(scr)) {
		if (scr->scr_hold_screen) {
			int s;

			/* XXX RESET KEYBOARD LEDS, etc. */
			s = spltty();	/* avoid conflict with keyboard */
			wsdisplay_kbdholdscr(scr, 0);
			splx(s);
		}
		tp = scr->scr_tty;
		(*linesw[tp->t_line].l_close)(tp, flag, p);
		ttyclose(tp);
	}

#ifdef WSDISPLAY_COMPAT_USL
	if (scr->scr_syncops)
		(*scr->scr_syncops->destroy)(scr->scr_synccookie);
#endif

	scr->scr_flags &= ~SCR_GRAPHICS;
	(*scr->scr_dconf->wsemul->reset)(scr->scr_dconf->wsemulcookie,
					 WSEMUL_RESET);
	if (wsdisplay_clearonclose)
		(*scr->scr_dconf->wsemul->reset)
			(scr->scr_dconf->wsemulcookie, WSEMUL_CLEARSCREEN);

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (scr->scr_rawkbd) {
		int kbmode = WSKBD_TRANSLATED;
		(void) wsdisplay_internal_ioctl(sc, scr, WSKBDIO_SETMODE,
		    (caddr_t)&kbmode, FWRITE, p);
	}
#endif

	scr->scr_flags &= ~SCR_OPEN;

#ifdef HAVE_WSMOUSED_SUPPORT
	/* remove the selection at logout */
	if (sc->sc_copybuffer != NULL)
		explicit_bzero(sc->sc_copybuffer, sc->sc_copybuffer_size);
	CLR(sc->sc_flags, SC_PASTE_AVAIL);
#endif

	return (0);
}

int
wsdisplayread(dev_t dev, struct uio *uio, int flag)
{
	struct wsdisplay_softc *sc;
	struct tty *tp;
	int unit;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	sc = wsdisplay_cd.cd_devs[unit];

	if (ISWSDISPLAYCTL(dev))
		return (0);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (!WSSCREEN_HAS_TTY(scr))
		return (ENODEV);

	tp = scr->scr_tty;
	return ((*linesw[tp->t_line].l_read)(tp, uio, flag));
}

int
wsdisplaywrite(dev_t dev, struct uio *uio, int flag)
{
	struct wsdisplay_softc *sc;
	struct tty *tp;
	int unit;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	sc = wsdisplay_cd.cd_devs[unit];

	if (ISWSDISPLAYCTL(dev))
		return (0);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (!WSSCREEN_HAS_TTY(scr))
		return (ENODEV);

	tp = scr->scr_tty;
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

struct tty *
wsdisplaytty(dev_t dev)
{
	struct wsdisplay_softc *sc;
	int unit;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	sc = wsdisplay_cd.cd_devs[unit];

	if (ISWSDISPLAYCTL(dev))
		panic("wsdisplaytty() on ctl device");

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (NULL);

	return (scr->scr_tty);
}

int
wsdisplayioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct wsdisplay_softc *sc;
	struct tty *tp;
	int unit, error;
	struct wsscreen *scr;

	unit = WSDISPLAYUNIT(dev);
	sc = wsdisplay_cd.cd_devs[unit];

#ifdef WSDISPLAY_COMPAT_USL
	error = wsdisplay_usl_ioctl1(sc, cmd, data, flag, p);
	if (error >= 0)
		return (error);
#endif

	if (ISWSDISPLAYCTL(dev)) {
		switch (cmd) {
		case WSDISPLAYIO_GTYPE:
		case WSDISPLAYIO_GETSCREENTYPE:
			/* pass to the first screen */
			dev = makedev(major(dev), WSDISPLAYMINOR(unit, 0));
			break;
		default:
			return (wsdisplay_cfg_ioctl(sc, cmd, data, flag, p));
		}
	}

	if (WSDISPLAYSCREEN(dev) >= WSDISPLAY_MAXSCREEN)
		return (ENODEV);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (WSSCREEN_HAS_TTY(scr)) {
		tp = scr->scr_tty;

/* printf("disc\n"); */
		/* do the line discipline ioctls first */
		error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
		if (error >= 0)
			return (error);

/* printf("tty\n"); */
		/* then the tty ioctls */
		error = ttioctl(tp, cmd, data, flag, p);
		if (error >= 0)
			return (error);
	}

#ifdef WSDISPLAY_COMPAT_USL
	error = wsdisplay_usl_ioctl2(sc, scr, cmd, data, flag, p);
	if (error >= 0)
		return (error);
#endif

	error = wsdisplay_internal_ioctl(sc, scr, cmd, data, flag, p);
	return (error != -1 ? error : ENOTTY);
}

int
wsdisplay_param(struct device *dev, u_long cmd, struct wsdisplay_param *dp)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	return wsdisplay_driver_ioctl(sc, cmd, (caddr_t)dp, 0, NULL);
}

int
wsdisplay_internal_ioctl(struct wsdisplay_softc *sc, struct wsscreen *scr,
    u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int error;

#if NWSKBD > 0
	struct wsevsrc *inp;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	switch (cmd) {
	case WSKBDIO_SETMODE:
		if ((flag & FWRITE) == 0)
			return (EACCES);
		scr->scr_rawkbd = (*(int *)data == WSKBD_RAW);
		return (wsdisplay_update_rawkbd(sc, scr));
	case WSKBDIO_GETMODE:
		*(int *)data = (scr->scr_rawkbd ?
				WSKBD_RAW : WSKBD_TRANSLATED);
		return (0);
	}
#endif
	inp = sc->sc_input;
	if (inp != NULL) {
		error = wsevsrc_display_ioctl(inp, cmd, data, flag, p);
		if (error >= 0)
			return (error);
	}
#endif /* NWSKBD > 0 */

	switch (cmd) {
	case WSDISPLAYIO_SMODE:
	case WSDISPLAYIO_USEFONT:
#ifdef HAVE_BURNER_SUPPORT
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_SBURNER:
#endif
	case WSDISPLAYIO_SETSCREEN:
		if ((flag & FWRITE) == 0)
			return (EACCES);
	}

	switch (cmd) {
	case WSDISPLAYIO_GMODE:
		if (scr->scr_flags & SCR_GRAPHICS) {
			if (scr->scr_flags & SCR_DUMBFB)
				*(u_int *)data = WSDISPLAYIO_MODE_DUMBFB;
			else
				*(u_int *)data = WSDISPLAYIO_MODE_MAPPED;
		} else
			*(u_int *)data = WSDISPLAYIO_MODE_EMUL;
		return (0);

	case WSDISPLAYIO_SMODE:
#define d (*(int *)data)
		if (d != WSDISPLAYIO_MODE_EMUL &&
		    d != WSDISPLAYIO_MODE_MAPPED &&
		    d != WSDISPLAYIO_MODE_DUMBFB)
			return (EINVAL);

		scr->scr_flags &= ~SCR_GRAPHICS;
		if (d == WSDISPLAYIO_MODE_MAPPED ||
		    d == WSDISPLAYIO_MODE_DUMBFB) {
			scr->scr_flags |= SCR_GRAPHICS |
			    ((d == WSDISPLAYIO_MODE_DUMBFB) ?  SCR_DUMBFB : 0);

			/* clear cursor */
			(*scr->scr_dconf->wsemul->reset)
			    (scr->scr_dconf->wsemulcookie, WSEMUL_CLEARCURSOR);
		}

#ifdef HAVE_BURNER_SUPPORT
		wsdisplay_burner_setup(sc, scr);
#endif

		(void)(*sc->sc_accessops->ioctl)(sc->sc_accesscookie, cmd, data,
		    flag, p);

		return (0);
#undef d

	case WSDISPLAYIO_USEFONT:
#define d ((struct wsdisplay_font *)data)
		if (!sc->sc_accessops->load_font)
			return (EINVAL);
		d->data = NULL;
		error = (*sc->sc_accessops->load_font)(sc->sc_accesscookie,
		    scr->scr_dconf->emulcookie, d);
		if (!error)
			(*scr->scr_dconf->wsemul->reset)
			    (scr->scr_dconf->wsemulcookie, WSEMUL_SYNCFONT);
		return (error);
#undef d
#ifdef HAVE_BURNER_SUPPORT
	case WSDISPLAYIO_GVIDEO:
		*(u_int *)data = !sc->sc_burnman;
		break;

	case WSDISPLAYIO_SVIDEO:
		if (*(u_int *)data != WSDISPLAYIO_VIDEO_OFF &&
		    *(u_int *)data != WSDISPLAYIO_VIDEO_ON)
			return (EINVAL);
		if (sc->sc_accessops->burn_screen == NULL)
			return (EOPNOTSUPP);
		(*sc->sc_accessops->burn_screen)(sc->sc_accesscookie,
		     *(u_int *)data, sc->sc_burnflags);
		sc->sc_burnman = *(u_int *)data == WSDISPLAYIO_VIDEO_OFF;
		break;

	case WSDISPLAYIO_GBURNER:
#define d ((struct wsdisplay_burner *)data)
		d->on  = sc->sc_burninintvl;
		d->off = sc->sc_burnoutintvl;
		d->flags = sc->sc_burnflags;
		return (0);

	case WSDISPLAYIO_SBURNER:
	    {
		struct wsscreen *active;

		if (d->flags & ~(WSDISPLAY_BURN_VBLANK | WSDISPLAY_BURN_KBD |
		    WSDISPLAY_BURN_MOUSE | WSDISPLAY_BURN_OUTPUT))
			return EINVAL;

		error = 0;
		sc->sc_burnflags = d->flags;
		/* disable timeout if necessary */
		if (d->off==0 || (sc->sc_burnflags & (WSDISPLAY_BURN_OUTPUT |
		    WSDISPLAY_BURN_KBD | WSDISPLAY_BURN_MOUSE)) == 0) {
			if (sc->sc_burnout)
				timeout_del(&sc->sc_burner);
		}

		active = sc->sc_focus;
		if (active == NULL)
			active = scr;

		if (d->on) {
			sc->sc_burninintvl = d->on;
			if (sc->sc_burnman) {
				sc->sc_burnout = sc->sc_burninintvl;
				/* reinit timeout if changed */
				if ((active->scr_flags & SCR_GRAPHICS) == 0)
					wsdisplay_burn(sc, sc->sc_burnflags);
			}
		}
		sc->sc_burnoutintvl = d->off;
		if (!sc->sc_burnman) {
			sc->sc_burnout = sc->sc_burnoutintvl;
			/* reinit timeout if changed */
			if ((active->scr_flags & SCR_GRAPHICS) == 0)
				wsdisplay_burn(sc, sc->sc_burnflags);
		}
		return (error);
	    }
#undef d
#endif	/* HAVE_BURNER_SUPPORT */
	case WSDISPLAYIO_GETSCREEN:
		return (wsdisplay_getscreen(sc,
		    (struct wsdisplay_addscreendata *)data));

	case WSDISPLAYIO_SETSCREEN:
		return (wsdisplay_switch((void *)sc, *(int *)data, 1));

	case WSDISPLAYIO_GETSCREENTYPE:
#define d ((struct wsdisplay_screentype *)data)
		if (d->idx < 0 || d->idx >= sc->sc_scrdata->nscreens)
			return(EINVAL);

		d->nidx = sc->sc_scrdata->nscreens;
		strlcpy(d->name, sc->sc_scrdata->screens[d->idx]->name,
			WSSCREEN_NAME_SIZE);
		d->ncols = sc->sc_scrdata->screens[d->idx]->ncols;
		d->nrows = sc->sc_scrdata->screens[d->idx]->nrows;
		d->fontwidth = sc->sc_scrdata->screens[d->idx]->fontwidth;
		d->fontheight = sc->sc_scrdata->screens[d->idx]->fontheight;
		return (0);
#undef d
	case WSDISPLAYIO_GETEMULTYPE:
#define d ((struct wsdisplay_emultype *)data)
		if (wsemul_getname(d->idx) == NULL)
			return(EINVAL);
		strlcpy(d->name, wsemul_getname(d->idx), WSEMUL_NAME_SIZE);
		return (0);
#undef d
        }

	/* check ioctls for display */
	return wsdisplay_driver_ioctl(sc, cmd, data, flag, p);
}

int
wsdisplay_driver_ioctl(struct wsdisplay_softc *sc, u_long cmd, caddr_t data,
    int flag, struct proc *p)
{
	int error;

	error = ((*sc->sc_accessops->ioctl)(sc->sc_accesscookie, cmd, data,
	    flag, p));
	/* Do not report parameters with empty ranges to userland. */
	if (error == 0 && cmd == WSDISPLAYIO_GETPARAM) {
		struct wsdisplay_param *dp = (struct wsdisplay_param *)data;
		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BACKLIGHT:
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
		case WSDISPLAYIO_PARAM_CONTRAST:
			if (dp->min == dp->max)
				error = ENOTTY;
			break;
		}
	}

	return error;
}

int
wsdisplay_cfg_ioctl(struct wsdisplay_softc *sc, u_long cmd, caddr_t data,
    int flag, struct proc *p)
{
	int error;
	void *buf;
	size_t fontsz;
#if NWSKBD > 0
	struct wsevsrc *inp;
#endif

	switch (cmd) {
#ifdef HAVE_WSMOUSED_SUPPORT
	case WSDISPLAYIO_WSMOUSED:
		error = wsmoused(sc, data, flag, p);
		return (error);
#endif
	case WSDISPLAYIO_ADDSCREEN:
#define d ((struct wsdisplay_addscreendata *)data)
		if ((error = wsdisplay_addscreen(sc, d->idx,
		    d->screentype, d->emul)) == 0)
			wsdisplay_addscreen_print(sc, d->idx, 0);
		return (error);
#undef d
	case WSDISPLAYIO_DELSCREEN:
#define d ((struct wsdisplay_delscreendata *)data)
		return (wsdisplay_delscreen(sc, d->idx, d->flags));
#undef d
	case WSDISPLAYIO_GETSCREEN:
		return (wsdisplay_getscreen(sc,
		    (struct wsdisplay_addscreendata *)data));
	case WSDISPLAYIO_SETSCREEN:
		return (wsdisplay_switch((void *)sc, *(int *)data, 1));
	case WSDISPLAYIO_LDFONT:
#define d ((struct wsdisplay_font *)data)
		if (!sc->sc_accessops->load_font)
			return (EINVAL);
		if (d->fontheight > 64 || d->stride > 8) /* 64x64 pixels */
			return (EINVAL);
		if (d->numchars > 65536) /* unicode plane */
			return (EINVAL);
		fontsz = d->fontheight * d->stride * d->numchars;
		if (fontsz > WSDISPLAY_MAXFONTSZ)
			return (EINVAL);

		buf = malloc(fontsz, M_DEVBUF, M_WAITOK);
		error = copyin(d->data, buf, fontsz);
		if (error) {
			free(buf, M_DEVBUF, fontsz);
			return (error);
		}
		d->data = buf;
		error =
		  (*sc->sc_accessops->load_font)(sc->sc_accesscookie, 0, d);
		if (error)
			free(buf, M_DEVBUF, fontsz);
		return (error);

	case WSDISPLAYIO_LSFONT:
		if (!sc->sc_accessops->list_font)
			return (EINVAL);
		error =
		  (*sc->sc_accessops->list_font)(sc->sc_accesscookie, d);
		return (error);

	case WSDISPLAYIO_DELFONT:
		return (EINVAL);
#undef d

#if NWSKBD > 0
	case WSMUXIO_ADD_DEVICE:
#define d ((struct wsmux_device *)data)
		if (d->idx == -1 && d->type == WSMUX_KBD)
			d->idx = wskbd_pickfree();
#undef d
		/* FALLTHROUGH */
	case WSMUXIO_INJECTEVENT:
	case WSMUXIO_REMOVE_DEVICE:
	case WSMUXIO_LIST_DEVICES:
		inp = sc->sc_input;
		if (inp == NULL)
			return (ENXIO);
		return (wsevsrc_ioctl(inp, cmd, data, flag,p));
#endif /* NWSKBD > 0 */

	}
	return (EINVAL);
}

paddr_t
wsdisplaymmap(dev_t dev, off_t offset, int prot)
{
	struct wsdisplay_softc *sc = wsdisplay_cd.cd_devs[WSDISPLAYUNIT(dev)];
	struct wsscreen *scr;

	if (ISWSDISPLAYCTL(dev))
		return (-1);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (-1);

	if (!(scr->scr_flags & SCR_GRAPHICS))
		return (-1);

	/* pass mmap to display */
	return ((*sc->sc_accessops->mmap)(sc->sc_accesscookie, offset, prot));
}

int
wsdisplaykqfilter(dev_t dev, struct knote *kn)
{
	struct wsdisplay_softc *sc = wsdisplay_cd.cd_devs[WSDISPLAYUNIT(dev)];
	struct wsscreen *scr;

	if (ISWSDISPLAYCTL(dev))
		return (ENXIO);

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(dev)]) == NULL)
		return (ENXIO);

	if (!WSSCREEN_HAS_TTY(scr))
		return (ENXIO);

	return (ttkqfilter(dev, kn));
}

void
wsdisplaystart(struct tty *tp)
{
	struct wsdisplay_softc *sc;
	struct wsscreen *scr;
	int s, n, done, unit;
	u_char *buf;

	unit = WSDISPLAYUNIT(tp->t_dev);
	if (unit >= wsdisplay_cd.cd_ndevs ||
	    (sc = wsdisplay_cd.cd_devs[unit]) == NULL)
		return;

	s = spltty();
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	if (tp->t_outq.c_cc == 0)
		goto low;

	if ((scr = sc->sc_scr[WSDISPLAYSCREEN(tp->t_dev)]) == NULL) {
		splx(s);
		return;
	}
	if (scr->scr_hold_screen) {
		tp->t_state |= TS_TIMEOUT;
		splx(s);
		return;
	}
	tp->t_state |= TS_BUSY;
	splx(s);

	/*
	 * Drain output from ring buffer.
	 * The output will normally be in one contiguous chunk, but when the
	 * ring wraps, it will be in two pieces.. one at the end of the ring,
	 * the other at the start.  For performance, rather than loop here,
	 * we output one chunk, see if there's another one, and if so, output
	 * it too.
	 */

	n = ndqb(&tp->t_outq, 0);
	buf = tp->t_outq.c_cf;

	if (!(scr->scr_flags & SCR_GRAPHICS)) {
#ifdef HAVE_BURNER_SUPPORT
		wsdisplay_burn(sc, WSDISPLAY_BURN_OUTPUT);
#endif
#ifdef HAVE_WSMOUSED_SUPPORT
		if (scr == sc->sc_focus)
			mouse_remove(scr);
#endif
		done = (*scr->scr_dconf->wsemul->output)
		    (scr->scr_dconf->wsemulcookie, buf, n, 0);
	} else
		done = n;
	ndflush(&tp->t_outq, done);

	if (done == n) {
		if ((n = ndqb(&tp->t_outq, 0)) > 0) {
			buf = tp->t_outq.c_cf;

			if (!(scr->scr_flags & SCR_GRAPHICS)) {
				done = (*scr->scr_dconf->wsemul->output)
				    (scr->scr_dconf->wsemulcookie, buf, n, 0);
			} else
				done = n;
			ndflush(&tp->t_outq, done);
		}
	}

	s = spltty();
	tp->t_state &= ~TS_BUSY;
	/* Come back if there's more to do */
	if (tp->t_outq.c_cc) {
		tp->t_state |= TS_TIMEOUT;
		timeout_add(&tp->t_rstrt_to, (hz > 128) ? (hz / 128) : 1);
	}
low:
	ttwakeupwr(tp);
	splx(s);
}

int
wsdisplaystop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		if (!ISSET(tp->t_state, TS_TTSTOP))
			SET(tp->t_state, TS_FLUSH);
	splx(s);

	return (0);
}

/* Set line parameters. */
int
wsdisplayparam(struct tty *tp, struct termios *t)
{

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return (0);
}

/*
 * Callbacks for the emulation code.
 */
void
wsdisplay_emulbell(void *v)
{
	struct wsscreen *scr = v;

	if (scr == NULL)		/* console, before real attach */
		return;

	if (scr->scr_flags & SCR_GRAPHICS) /* can this happen? */
		return;

	task_add(scr->sc->sc_taskq, &scr->scr_emulbell_task);
}

void
wsdisplay_emulbell_task(void *v)
{
	struct wsscreen *scr = v;

	(void)wsdisplay_internal_ioctl(scr->sc, scr, WSKBDIO_BELL, NULL,
	    FWRITE, NULL);
}

#if !defined(WSEMUL_NO_VT100)
void
wsdisplay_emulinput(void *v, const u_char *data, u_int count)
{
	struct wsscreen *scr = v;
	struct tty *tp;

	if (v == NULL)			/* console, before real attach */
		return;

	if (scr->scr_flags & SCR_GRAPHICS) /* XXX can't happen */
		return;
	if (!WSSCREEN_HAS_TTY(scr))
		return;

	tp = scr->scr_tty;
	while (count-- > 0)
		(*linesw[tp->t_line].l_rint)(*data++, tp);
}
#endif

/*
 * Calls from the keyboard interface.
 */
void
wsdisplay_kbdinput(struct device *dev, kbd_t layout, keysym_t *ks, int num)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsscreen *scr;
	const u_char *dp;
	int count;
	struct tty *tp;

	scr = sc->sc_focus;
	if (!scr || !WSSCREEN_HAS_TTY(scr))
		return;


	tp = scr->scr_tty;
	for (; num > 0; num--) {
		count = (*scr->scr_dconf->wsemul->translate)
		    (scr->scr_dconf->wsemulcookie, layout, *ks++, &dp);
		while (count-- > 0)
			(*linesw[tp->t_line].l_rint)(*dp++, tp);
	}
}

#ifdef WSDISPLAY_COMPAT_RAWKBD
void
wsdisplay_rawkbdinput(struct device *dev, u_char *buf, int num)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsscreen *scr;
	struct tty *tp;

	scr = sc->sc_focus;
	if (!scr || !WSSCREEN_HAS_TTY(scr))
		return;

	tp = scr->scr_tty;
	while (num-- > 0)
		(*linesw[tp->t_line].l_rint)(*buf++, tp);
}
int
wsdisplay_update_rawkbd(struct wsdisplay_softc *sc, struct wsscreen *scr)
{
#if NWSKBD > 0
	int s, raw, data, error;
	struct wsevsrc *inp;

	s = spltty();

	raw = (scr ? scr->scr_rawkbd : 0);

	if (scr != sc->sc_focus || sc->sc_rawkbd == raw) {
		splx(s);
		return (0);
	}

	data = raw ? WSKBD_RAW : WSKBD_TRANSLATED;
	inp = sc->sc_input;
	if (inp == NULL) {
		splx(s);
		return (ENXIO);
	}
	error = wsevsrc_display_ioctl(inp, WSKBDIO_SETMODE, &data, FWRITE, 0);
	if (!error)
		sc->sc_rawkbd = raw;
	splx(s);
	return (error);
#else
	return (0);
#endif
}
#endif

int
wsdisplay_switch3(void *arg, int error, int waitok)
{
	struct wsdisplay_softc *sc = arg;
	int no;
	struct wsscreen *scr;

#ifdef WSDISPLAY_COMPAT_USL
	if (!ISSET(sc->sc_flags, SC_SWITCHPENDING)) {
		printf("wsdisplay_switch3: not switching\n");
		return (EINVAL);
	}

	no = sc->sc_screenwanted;
	if (no < 0 || no >= WSDISPLAY_MAXSCREEN)
		panic("wsdisplay_switch3: invalid screen %d", no);
	scr = sc->sc_scr[no];
	if (!scr) {
		printf("wsdisplay_switch3: screen %d disappeared\n", no);
		error = ENXIO;
	}

	if (error) {
		/* try to recover, avoid recursion */

		if (sc->sc_oldscreen == WSDISPLAY_NULLSCREEN) {
			printf("wsdisplay_switch3: giving up\n");
			sc->sc_focus = NULL;
#ifdef WSDISPLAY_COMPAT_RAWKBD
			wsdisplay_update_rawkbd(sc, 0);
#endif
			CLR(sc->sc_flags, SC_SWITCHPENDING);
			return (error);
		}

		sc->sc_screenwanted = sc->sc_oldscreen;
		sc->sc_oldscreen = WSDISPLAY_NULLSCREEN;
		return (wsdisplay_switch1(arg, 0, waitok));
	}
#else
	/*
	 * If we do not have syncops support, we come straight from
	 * wsdisplay_switch2 which has already validated our arguments
	 * and did not sleep.
	 */
	no = sc->sc_screenwanted;
	scr = sc->sc_scr[no];
#endif

	CLR(sc->sc_flags, SC_SWITCHPENDING);

#ifdef HAVE_BURNER_SUPPORT
	if (!error)
		wsdisplay_burner_setup(sc, scr);
#endif

	if (!error && (scr->scr_flags & SCR_WAITACTIVE))
		wakeup(scr);
	return (error);
}

int
wsdisplay_switch2(void *arg, int error, int waitok)
{
	struct wsdisplay_softc *sc = arg;
	int no;
	struct wsscreen *scr;

	if (!ISSET(sc->sc_flags, SC_SWITCHPENDING)) {
		printf("wsdisplay_switch2: not switching\n");
		return (EINVAL);
	}

	no = sc->sc_screenwanted;
	if (no < 0 || no >= WSDISPLAY_MAXSCREEN)
		panic("wsdisplay_switch2: invalid screen %d", no);
	scr = sc->sc_scr[no];
	if (!scr) {
		printf("wsdisplay_switch2: screen %d disappeared\n", no);
		error = ENXIO;
	}

	if (error) {
		/* try to recover, avoid recursion */

		if (sc->sc_oldscreen == WSDISPLAY_NULLSCREEN) {
			printf("wsdisplay_switch2: giving up\n");
			sc->sc_focus = NULL;
			CLR(sc->sc_flags, SC_SWITCHPENDING);
			return (error);
		}

		sc->sc_screenwanted = sc->sc_oldscreen;
		sc->sc_oldscreen = WSDISPLAY_NULLSCREEN;
		return (wsdisplay_switch1(arg, 0, waitok));
	}

	sc->sc_focusidx = no;
	sc->sc_focus = scr;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	(void) wsdisplay_update_rawkbd(sc, scr);
#endif
	/* keyboard map??? */

#ifdef WSDISPLAY_COMPAT_USL
#define wsswitch_cb3 ((void (*)(void *, int, int))wsdisplay_switch3)
	if (scr->scr_syncops) {
		error = (*scr->scr_syncops->attach)(scr->scr_synccookie, waitok,
		    sc->sc_isconsole && wsdisplay_cons_pollmode ?
		      0 : wsswitch_cb3, sc);
		if (error == EAGAIN) {
			/* switch will be done asynchronously */
			return (0);
		}
	}
#endif

	return (wsdisplay_switch3(sc, error, waitok));
}

int
wsdisplay_switch1(void *arg, int error, int waitok)
{
	struct wsdisplay_softc *sc = arg;
	int no;
	struct wsscreen *scr;

	if (!ISSET(sc->sc_flags, SC_SWITCHPENDING)) {
		printf("wsdisplay_switch1: not switching\n");
		return (EINVAL);
	}

	no = sc->sc_screenwanted;
	if (no == WSDISPLAY_NULLSCREEN) {
		CLR(sc->sc_flags, SC_SWITCHPENDING);
		if (!error) {
			sc->sc_focus = NULL;
		}
		wakeup(sc);
		return (error);
	}
	if (no < 0 || no >= WSDISPLAY_MAXSCREEN)
		panic("wsdisplay_switch1: invalid screen %d", no);
	scr = sc->sc_scr[no];
	if (!scr) {
		printf("wsdisplay_switch1: screen %d disappeared\n", no);
		error = ENXIO;
	}

	if (error) {
		CLR(sc->sc_flags, SC_SWITCHPENDING);
		return (error);
	}

#define wsswitch_cb2 ((void (*)(void *, int, int))wsdisplay_switch2)
	error = (*sc->sc_accessops->show_screen)(sc->sc_accesscookie,
	    scr->scr_dconf->emulcookie, waitok,
	    sc->sc_isconsole && wsdisplay_cons_pollmode ? 0 : wsswitch_cb2, sc);
	if (error == EAGAIN) {
		/* switch will be done asynchronously */
		return (0);
	}

	return (wsdisplay_switch2(sc, error, waitok));
}

int
wsdisplay_switch(struct device *dev, int no, int waitok)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	int s, res = 0;
	struct wsscreen *scr;

	if (no != WSDISPLAY_NULLSCREEN) {
		if (no < 0 || no >= WSDISPLAY_MAXSCREEN)
			return (EINVAL);
		if (sc->sc_scr[no] == NULL)
			return (ENXIO);
	}

	s = spltty();

	if (sc->sc_resumescreen != WSDISPLAY_NULLSCREEN && !waitok) {
		splx(s);
		return (EBUSY);
	}

	while (sc->sc_resumescreen != WSDISPLAY_NULLSCREEN && res == 0)
		res = tsleep_nsec(&sc->sc_resumescreen, PCATCH, "wsrestore",
		    INFSLP);
	if (res) {
		splx(s);
		return (res);
	}

	if ((sc->sc_focus && no == sc->sc_focusidx) ||
	    (sc->sc_focus == NULL && no == WSDISPLAY_NULLSCREEN)) {
		splx(s);
		return (0);
	}

	if (ISSET(sc->sc_flags, SC_SWITCHPENDING)) {
		splx(s);
		return (EBUSY);
	}

	SET(sc->sc_flags, SC_SWITCHPENDING);
	sc->sc_screenwanted = no;

	splx(s);

	scr = sc->sc_focus;
	if (!scr) {
		sc->sc_oldscreen = WSDISPLAY_NULLSCREEN;
		return (wsdisplay_switch1(sc, 0, waitok));
	} else
		sc->sc_oldscreen = sc->sc_focusidx;

#ifdef WSDISPLAY_COMPAT_USL
#define wsswitch_cb1 ((void (*)(void *, int, int))wsdisplay_switch1)
	if (scr->scr_syncops) {
		res = (*scr->scr_syncops->detach)(scr->scr_synccookie, waitok,
		    sc->sc_isconsole && wsdisplay_cons_pollmode ?
		      0 : wsswitch_cb1, sc);
		if (res == EAGAIN) {
			/* switch will be done asynchronously */
			return (0);
		}
	} else if (scr->scr_flags & SCR_GRAPHICS) {
		/* no way to save state */
		res = EBUSY;
	}
#endif

#ifdef HAVE_WSMOUSED_SUPPORT
	mouse_remove(scr);
#endif

	return (wsdisplay_switch1(sc, res, waitok));
}

void
wsdisplay_reset(struct device *dev, enum wsdisplay_resetops op)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsscreen *scr;

	scr = sc->sc_focus;

	if (!scr)
		return;

	switch (op) {
	case WSDISPLAY_RESETEMUL:
		(*scr->scr_dconf->wsemul->reset)(scr->scr_dconf->wsemulcookie,
		    WSEMUL_RESET);
		break;
	case WSDISPLAY_RESETCLOSE:
		wsdisplay_closescreen(sc, scr);
		break;
	}
}

#ifdef WSDISPLAY_COMPAT_USL
/*
 * Interface for (external) VT switch / process synchronization code
 */
int
wsscreen_attach_sync(struct wsscreen *scr, const struct wscons_syncops *ops,
    void *cookie)
{
	if (scr->scr_syncops) {
		/*
		 * The screen is already claimed.
		 * Check if the owner is still alive.
		 */
		if ((*scr->scr_syncops->check)(scr->scr_synccookie))
			return (EBUSY);
	}
	scr->scr_syncops = ops;
	scr->scr_synccookie = cookie;
	return (0);
}

int
wsscreen_detach_sync(struct wsscreen *scr)
{
	if (!scr->scr_syncops)
		return (EINVAL);
	scr->scr_syncops = NULL;
	return (0);
}

int
wsscreen_lookup_sync(struct wsscreen *scr,
    const struct wscons_syncops *ops, /* used as ID */
    void **cookiep)
{
	if (!scr->scr_syncops || ops != scr->scr_syncops)
		return (EINVAL);
	*cookiep = scr->scr_synccookie;
	return (0);
}
#endif

/*
 * Interface to virtual screen stuff
 */
int
wsdisplay_maxscreenidx(struct wsdisplay_softc *sc)
{
	return (WSDISPLAY_MAXSCREEN - 1);
}

int
wsdisplay_screenstate(struct wsdisplay_softc *sc, int idx)
{
	if (idx < 0 || idx >= WSDISPLAY_MAXSCREEN)
		return (EINVAL);
	if (!sc->sc_scr[idx])
		return (ENXIO);
	return ((sc->sc_scr[idx]->scr_flags & SCR_OPEN) ? EBUSY : 0);
}

int
wsdisplay_getactivescreen(struct wsdisplay_softc *sc)
{
	return (sc->sc_focus ? sc->sc_focusidx : WSDISPLAY_NULLSCREEN);
}

int
wsscreen_switchwait(struct wsdisplay_softc *sc, int no)
{
	struct wsscreen *scr;
	int s, res = 0;

	if (no == WSDISPLAY_NULLSCREEN) {
		s = spltty();
		while (sc->sc_focus && res == 0) {
			res = tsleep_nsec(sc, PCATCH, "wswait", INFSLP);
		}
		splx(s);
		return (res);
	}

	if (no < 0 || no >= WSDISPLAY_MAXSCREEN)
		return (ENXIO);
	scr = sc->sc_scr[no];
	if (!scr)
		return (ENXIO);

	s = spltty();
	if (scr != sc->sc_focus) {
		scr->scr_flags |= SCR_WAITACTIVE;
		res = tsleep_nsec(scr, PCATCH, "wswait2", INFSLP);
		if (scr != sc->sc_scr[no])
			res = ENXIO; /* disappeared in the meantime */
		else
			scr->scr_flags &= ~SCR_WAITACTIVE;
	}
	splx(s);
	return (res);
}

void
wsdisplay_kbdholdscr(struct wsscreen *scr, int hold)
{
	if (hold)
		scr->scr_hold_screen = 1;
	else {
		scr->scr_hold_screen = 0;
		timeout_add(&scr->scr_tty->t_rstrt_to, 0); /* "immediate" */
	}
}

void
wsdisplay_kbdholdscreen(struct device *dev, int hold)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsscreen *scr;

	scr = sc->sc_focus;
	if (scr != NULL && WSSCREEN_HAS_TTY(scr))
		wsdisplay_kbdholdscr(scr, hold);
}

#if NWSKBD > 0
void
wsdisplay_set_console_kbd(struct wsevsrc *src)
{
	if (wsdisplay_console_device == NULL) {
		src->me_dispdv = NULL;
		return;
	}
#if NWSMUX > 0
	if (wsmux_attach_sc((struct wsmux_softc *)
			    wsdisplay_console_device->sc_input, src)) {
		src->me_dispdv = NULL;
		return;
	}
#else
	wsdisplay_console_device->sc_input = src;
#endif
	src->me_dispdv = &wsdisplay_console_device->sc_dv;
}

#if NWSMUX == 0
int
wsdisplay_set_kbd(struct device *disp, struct wsevsrc *kbd)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)disp;

	if (sc->sc_input != NULL)
		return (EBUSY);

	sc->sc_input = kbd;

	return (0);
}
#endif

#endif /* NWSKBD > 0 */

/*
 * Console interface.
 */
void
wsdisplay_cnputc(dev_t dev, int i)
{
	struct wsscreen_internal *dc;
	char c = i;

	if (!wsdisplay_console_initted)
		return;

	if (wsdisplay_console_device != NULL &&
	    (wsdisplay_console_device->sc_scr[0] != NULL) &&
	    (wsdisplay_console_device->sc_scr[0]->scr_flags & SCR_GRAPHICS))
		return;

	dc = &wsdisplay_console_conf;
#ifdef HAVE_BURNER_SUPPORT
	/*wsdisplay_burn(wsdisplay_console_device, WSDISPLAY_BURN_OUTPUT);*/
#endif
	(void)(*dc->wsemul->output)(dc->wsemulcookie, &c, 1, 1);
}

int
wsdisplay_getc_dummy(dev_t dev)
{
	/* panic? */
	return (0);
}

void
wsdisplay_pollc(dev_t dev, int on)
{

	wsdisplay_cons_pollmode = on;

	/* notify to fb drivers */
	if (wsdisplay_console_device != NULL &&
	    wsdisplay_console_device->sc_accessops->pollc != NULL)
		(*wsdisplay_console_device->sc_accessops->pollc)
		    (wsdisplay_console_device->sc_accesscookie, on);

	/* notify to kbd drivers */
	if (wsdisplay_cons_kbd_pollc)
		(*wsdisplay_cons_kbd_pollc)(dev, on);
}

void
wsdisplay_set_cons_kbd(int (*get)(dev_t), void (*poll)(dev_t, int),
    void (*bell)(dev_t, u_int, u_int, u_int))
{
	wsdisplay_cons.cn_getc = get;
	wsdisplay_cons.cn_bell = bell;
	wsdisplay_cons_kbd_pollc = poll;
}

void
wsdisplay_unset_cons_kbd(void)
{
	wsdisplay_cons.cn_getc = wsdisplay_getc_dummy;
	wsdisplay_cons.cn_bell = NULL;
	wsdisplay_cons_kbd_pollc = NULL;
}

/*
 * Switch the console display to its first screen.
 */
void
wsdisplay_switchtoconsole(void)
{
	struct wsdisplay_softc *sc;
	struct wsscreen *scr;

	if (wsdisplay_console_device != NULL && cn_tab == &wsdisplay_cons) {
		sc = wsdisplay_console_device;
		if ((scr = sc->sc_scr[0]) == NULL)
			return;
		(*sc->sc_accessops->show_screen)(sc->sc_accesscookie,
		    scr->scr_dconf->emulcookie, 0, NULL, NULL);
	}
}

/*
 * Switch the console display to its ddb screen, avoiding locking
 * where we can.
 */
void
wsdisplay_enter_ddb(void)
{
	struct wsdisplay_softc *sc;
	struct wsscreen *scr;

	if (wsdisplay_console_device != NULL && cn_tab == &wsdisplay_cons) {
		sc = wsdisplay_console_device;
		if ((scr = sc->sc_scr[0]) == NULL)
			return;
		if (sc->sc_accessops->enter_ddb) {
			(*sc->sc_accessops->enter_ddb)(sc->sc_accesscookie,
			    scr->scr_dconf->emulcookie);
		} else {
			(*sc->sc_accessops->show_screen)(sc->sc_accesscookie,
			    scr->scr_dconf->emulcookie, 0, NULL, NULL);
		}
	}
}

/*
 * Deal with the xserver doing driver in userland and thus screwing up suspend
 * and resume by switching away from it at suspend/resume time.
 *
 * these functions must be called from the MD suspend callback, since we may
 * need to sleep if we have a user (probably an X server) on a vt. therefore
 * this can't be a config_suspend() hook.
 */
void
wsdisplay_suspend(void)
{
	int	i;

	for (i = 0; i < wsdisplay_cd.cd_ndevs; i++)
		if (wsdisplay_cd.cd_devs[i] != NULL)
			wsdisplay_suspend_device(wsdisplay_cd.cd_devs[i]);
}

void
wsdisplay_suspend_device(struct device *dev)
{
	struct wsdisplay_softc	*sc = (struct wsdisplay_softc *)dev;
	struct wsscreen		*scr;
	int			 active, idx, ret = 0, s;

	if ((active = wsdisplay_getactivescreen(sc)) == WSDISPLAY_NULLSCREEN)
		return;

	scr = sc->sc_scr[active];
	/*
	 * We want to switch out of graphics mode for the suspend
	 */
retry:
	idx = WSDISPLAY_MAXSCREEN;
	if (scr->scr_flags & SCR_GRAPHICS) {
		for (idx = 0; idx < WSDISPLAY_MAXSCREEN; idx++) {
			if (sc->sc_scr[idx] == NULL || sc->sc_scr[idx] == scr)
				continue;

			if ((sc->sc_scr[idx]->scr_flags & SCR_GRAPHICS) == 0)
				break;
		}
	}

	/* if we don't have anything to switch to, we can't do anything */
	if (idx == WSDISPLAY_MAXSCREEN)
		return;

	/*
	 * we do a lot of magic here because we need to know that the
	 * switch has completed before we return
	 */
	ret = wsdisplay_switch((struct device *)sc, idx, 1);
	if (ret == EBUSY) {
		/* XXX sleep on what's going on */
		goto retry;
	} else if (ret)
		return;

	s = spltty();
	sc->sc_resumescreen = active; /* block other vt switches until resume */
	splx(s);
	/*
	 * This will either return ENXIO (invalid (shouldn't happen) or
	 * wsdisplay disappeared (problem solved)), or EINTR/ERESTART.
	 * Not much we can do about the latter since we can't return to
	 * userland.
	 */
	(void)wsscreen_switchwait(sc, idx);
}

void
wsdisplay_resume(void)
{
	int	i;

	for (i = 0; i < wsdisplay_cd.cd_ndevs; i++)
		if (wsdisplay_cd.cd_devs[i] != NULL)
			wsdisplay_resume_device(wsdisplay_cd.cd_devs[i]);
}

void
wsdisplay_resume_device(struct device *dev)
{
	struct wsdisplay_softc	*sc = (struct wsdisplay_softc *)dev;
	int			 idx, s;

	if (sc->sc_resumescreen != WSDISPLAY_NULLSCREEN) {
		s = spltty();
		idx = sc->sc_resumescreen;
		sc->sc_resumescreen = WSDISPLAY_NULLSCREEN;
		wakeup(&sc->sc_resumescreen);
		splx(s);
		(void)wsdisplay_switch((struct device *)sc, idx, 1);
	}
}

#ifdef HAVE_SCROLLBACK_SUPPORT
void
wsscrollback(void *arg, int op)
{
	struct wsdisplay_softc *sc = arg;
	int lines;

	if (sc->sc_focus == NULL)
		return;

	if (op == WSDISPLAY_SCROLL_RESET)
		lines = 0;
	else {
		lines = sc->sc_focus->scr_dconf->scrdata->nrows - 1;
		if (op == WSDISPLAY_SCROLL_BACKWARD)
			lines = -lines;
	}

	if (sc->sc_accessops->scrollback) {
		(*sc->sc_accessops->scrollback)(sc->sc_accesscookie,
		    sc->sc_focus->scr_dconf->emulcookie, lines);
	}
}
#endif

#ifdef HAVE_BURNER_SUPPORT
/*
 * Update screen burner behaviour after either a screen focus change or
 * a screen mode change.
 * This is needed to allow X11 to manage screen blanking without any
 * interference from the kernel.
 */
void
wsdisplay_burner_setup(struct wsdisplay_softc *sc, struct wsscreen *scr)
{
	if (scr->scr_flags & SCR_GRAPHICS) {
		/* enable video _immediately_ if it needs to be... */
		if (sc->sc_burnman)
			wsdisplay_burner(sc);
		/* ...and disable the burner while X is running */
		if (sc->sc_burnout) {
			timeout_del(&sc->sc_burner);
			sc->sc_burnout = 0;
		}
	} else {
		/* reenable the burner after exiting from X */
		if (!sc->sc_burnman) {
			sc->sc_burnout = sc->sc_burnoutintvl;
			wsdisplay_burn(sc, sc->sc_burnflags);
		}
	}
}

void
wsdisplay_burn(void *v, u_int flags)
{
	struct wsdisplay_softc *sc = v;

	if ((flags & sc->sc_burnflags & (WSDISPLAY_BURN_OUTPUT |
	    WSDISPLAY_BURN_KBD | WSDISPLAY_BURN_MOUSE)) &&
	    sc->sc_accessops->burn_screen) {
		if (sc->sc_burnout)
			timeout_add_msec(&sc->sc_burner, sc->sc_burnout);
		if (sc->sc_burnman)
			sc->sc_burnout = 0;
	}
}

void
wsdisplay_burner(void *v)
{
	struct wsdisplay_softc *sc = v;
	int s;

	if (sc->sc_accessops->burn_screen) {
		(*sc->sc_accessops->burn_screen)(sc->sc_accesscookie,
		    sc->sc_burnman, sc->sc_burnflags);
		s = spltty();
		if (sc->sc_burnman) {
			sc->sc_burnout = sc->sc_burnoutintvl;
			timeout_add_msec(&sc->sc_burner, sc->sc_burnout);
		} else
			sc->sc_burnout = sc->sc_burninintvl;
		sc->sc_burnman = !sc->sc_burnman;
		splx(s);
	}
}
#endif

int
wsdisplay_get_param(struct wsdisplay_softc *sc, struct wsdisplay_param *dp)
{
	int error = ENXIO;
	int i;

	if (sc != NULL)
		return wsdisplay_param(&sc->sc_dv, WSDISPLAYIO_GETPARAM, dp);

	for (i = 0; i < wsdisplay_cd.cd_ndevs; i++) {
		sc = wsdisplay_cd.cd_devs[i];
		if (sc == NULL)
			continue;
		error = wsdisplay_param(&sc->sc_dv, WSDISPLAYIO_GETPARAM, dp);
		if (error == 0)
			break;
	}

	if (error && ws_get_param)
		error = ws_get_param(dp);

	return error;
}

int
wsdisplay_set_param(struct wsdisplay_softc *sc, struct wsdisplay_param *dp)
{
	int error = ENXIO;
	int i;

	if (sc != NULL)
		return wsdisplay_param(&sc->sc_dv, WSDISPLAYIO_SETPARAM, dp);

	for (i = 0; i < wsdisplay_cd.cd_ndevs; i++) {
		sc = wsdisplay_cd.cd_devs[i];
		if (sc == NULL)
			continue;
		error = wsdisplay_param(&sc->sc_dv, WSDISPLAYIO_SETPARAM, dp);
		if (error == 0)
			break;
	}

	if (error && ws_set_param)
		error = ws_set_param(dp);

	return error;
}

void
wsdisplay_brightness_step(struct device *dev, int dir)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsdisplay_param dp;
	int delta, new;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	if (wsdisplay_get_param(sc, &dp))
		return;

	/* Use a step size of approximately 5%. */
	delta = max(1, ((dp.max - dp.min) * 5) / 100);
	new = dp.curval;

	if (dir > 0) {
		if (delta > dp.max - dp.curval)
			new = dp.max;
		else
			new += delta;
	} else if (dir < 0) {
		if (delta > dp.curval - dp.min)
			new = dp.min;
		else
			new -= delta;
	}

	if (dp.curval == new)
		return;

	dp.curval = new;
	wsdisplay_set_param(sc, &dp);
}

void
wsdisplay_brightness_zero(struct device *dev)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	if (wsdisplay_get_param(sc, &dp))
		return;

	dp.curval = dp.min;
	wsdisplay_set_param(sc, &dp);
}

void
wsdisplay_brightness_cycle(struct device *dev)
{
	struct wsdisplay_softc *sc = (struct wsdisplay_softc *)dev;
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	if (wsdisplay_get_param(sc, &dp))
		return;

	if (dp.curval == dp.max)
		wsdisplay_brightness_zero(dev);
	else
		wsdisplay_brightness_step(dev, 1);
}

#ifdef HAVE_WSMOUSED_SUPPORT
/*
 * wsmoused(8) support functions
 */

/*
 * Main function, called from wsdisplay_cfg_ioctl.
 */
int
wsmoused(struct wsdisplay_softc *sc, caddr_t data, int flag, struct proc *p)
{
	struct wscons_event mouse_event = *(struct wscons_event *)data;

	if (IS_MOTION_EVENT(mouse_event.type)) {
		if (sc->sc_focus != NULL)
			motion_event(sc->sc_focus, mouse_event.type,
			    mouse_event.value);
		return 0;
	}
	if (IS_BUTTON_EVENT(mouse_event.type)) {
		if (sc->sc_focus != NULL) {
			/* XXX tv_sec contains the number of clicks */
			if (mouse_event.type ==
			    WSCONS_EVENT_MOUSE_DOWN) {
				button_event(sc->sc_focus,
				    mouse_event.value,
				    mouse_event.time.tv_sec);
			} else
				button_event(sc->sc_focus,
				    mouse_event.value, 0);
		}
		return (0);
	}
	if (IS_CTRL_EVENT(mouse_event.type)) {
		return ctrl_event(sc, mouse_event.type,
		    mouse_event.value, p);
	}
	return -1;
}

/*
 * Mouse motion events
 */
void
motion_event(struct wsscreen *scr, u_int type, int value)
{
	switch (type) {
	case WSCONS_EVENT_MOUSE_DELTA_X:
		mouse_moverel(scr, value, 0);
		break;
	case WSCONS_EVENT_MOUSE_DELTA_Y:
		mouse_moverel(scr, 0, -value);
		break;
#ifdef HAVE_SCROLLBACK_SUPPORT
	case WSCONS_EVENT_MOUSE_DELTA_Z:
		mouse_zaxis(scr, value);
		break;
#endif
	default:
		break;
	}
}

/*
 * Button clicks events
 */
void
button_event(struct wsscreen *scr, int button, int clicks)
{
	switch (button) {
	case MOUSE_COPY_BUTTON:
		switch (clicks % 4) {
		case 0: /* button is up */
			mouse_copy_end(scr);
			mouse_copy_selection(scr);
			break;
		case 1: /* single click */
			mouse_copy_start(scr);
			mouse_copy_selection(scr);
			break;
		case 2: /* double click */
			mouse_copy_word(scr);
			mouse_copy_selection(scr);
			break;
		case 3: /* triple click */
			mouse_copy_line(scr);
			mouse_copy_selection(scr);
			break;
		}
		break;
	case MOUSE_PASTE_BUTTON:
		if (clicks != 0)
			mouse_paste(scr);
		break;
	case MOUSE_EXTEND_BUTTON:
		if (clicks != 0)
			mouse_copy_extend_after(scr);
		break;
	default:
		break;
	}
}

/*
 * Control events
 */
int
ctrl_event(struct wsdisplay_softc *sc, u_int type, int value, struct proc *p)
{
	struct wsscreen *scr;
	int i;

	switch (type) {
	case WSCONS_EVENT_WSMOUSED_OFF:
		CLR(sc->sc_flags, SC_PASTE_AVAIL);
		return (0);
	case WSCONS_EVENT_WSMOUSED_ON:
		if (!sc->sc_accessops->getchar)
			/* no wsmoused(8) support in the display driver */
			return (1);
		allocate_copybuffer(sc);
		CLR(sc->sc_flags, SC_PASTE_AVAIL);

		for (i = 0 ; i < WSDISPLAY_DEFAULTSCREENS ; i++)
			if ((scr = sc->sc_scr[i]) != NULL) {
				scr->mouse =
				    (WS_NCOLS(scr) * WS_NROWS(scr)) / 2;
				scr->cursor = scr->mouse;
				scr->cpy_start = 0;
				scr->cpy_end = 0;
				scr->orig_start = 0;
				scr->orig_end = 0;
				scr->mouse_flags = 0;
			}
		return (0);
	default:	/* can't happen, really */
		return 0;
	}
}

void
mouse_moverel(struct wsscreen *scr, int dx, int dy)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	u_int old_mouse = scr->mouse;
	int mouse_col = scr->mouse % N_COLS(dconf);
	int mouse_row = scr->mouse / N_COLS(dconf);

	/* update position */
	if (mouse_col + dx >= MAXCOL(dconf))
		mouse_col = MAXCOL(dconf);
	else {
		if (mouse_col + dx <= 0)
			mouse_col = 0;
		else
			mouse_col += dx;
	}
	if (mouse_row + dy >= MAXROW(dconf))
		mouse_row = MAXROW(dconf);
	else {
		if (mouse_row + dy <= 0)
			mouse_row = 0;
		else
			mouse_row += dy;
	}
	scr->mouse = mouse_row * N_COLS(dconf) + mouse_col;

	/* if we have moved */
	if (old_mouse != scr->mouse) {
		/* XXX unblank screen if display.ms_act */
		if (ISSET(scr->mouse_flags, SEL_IN_PROGRESS)) {
			/* selection in progress */
			mouse_copy_extend(scr);
		} else {
			inverse_char(scr, scr->mouse);
			if (ISSET(scr->mouse_flags, MOUSE_VISIBLE))
				inverse_char(scr, old_mouse);
			else
				SET(scr->mouse_flags, MOUSE_VISIBLE);
		}
	}
}

void
inverse_char(struct wsscreen *scr, u_int pos)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	int fg, bg, ul;
	int flags;
	int tmp;
	uint32_t attr;

	GETCHAR(scr, pos, &cell);

	(*dconf->emulops->unpack_attr)(dconf->emulcookie, cell.attr, &fg,
	    &bg, &ul);

	/*
	 * Display the mouse cursor as a color inverted cell whenever
	 * possible. If this is not possible, ask for the video reverse
	 * attribute.
	 */
	flags = 0;
	if (dconf->scrdata->capabilities & WSSCREEN_WSCOLORS) {
		flags |= WSATTR_WSCOLORS;
		tmp = fg;
		fg = bg;
		bg = tmp;
	} else if (dconf->scrdata->capabilities & WSSCREEN_REVERSE) {
		flags |= WSATTR_REVERSE;
	}
	if ((*dconf->emulops->pack_attr)(dconf->emulcookie, fg, bg, flags |
	    (ul ? WSATTR_UNDERLINE : 0), &attr) == 0) {
		cell.attr = attr;
		PUTCHAR(dconf, pos, cell.uc, cell.attr);
	}
}

void
inverse_region(struct wsscreen *scr, u_int start, u_int end)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	u_int current_pos;
	u_int abs_end;

	/* sanity check, useful because 'end' can be (u_int)-1 */
	abs_end = N_COLS(dconf) * N_ROWS(dconf);
	if (end > abs_end)
		return;
	current_pos = start;
	while (current_pos <= end)
		inverse_char(scr, current_pos++);
}

/*
 * Return the number of contiguous blank characters between the right margin
 * if border == 1 or between the next non-blank character and the current mouse
 * cursor if border == 0
 */
u_int
skip_spc_right(struct wsscreen *scr, int border)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	u_int current = scr->cpy_end;
	u_int mouse_col = scr->cpy_end % N_COLS(dconf);
	u_int limit = current + (N_COLS(dconf) - mouse_col - 1);
	u_int res = 0;

	while (GETCHAR(scr, current, &cell) == 0 && cell.uc == ' ' &&
	    current <= limit) {
		current++;
		res++;
	}
	if (border == BORDER) {
		if (current > limit)
			return (res - 1);
		else
			return (0);
	} else {
		if (res != 0)
			return (res - 1);
		else
			return (res);
	}
}

/*
 * Return the number of contiguous blank characters between the first of the
 * contiguous blank characters and the current mouse cursor
 */
u_int
skip_spc_left(struct wsscreen *scr)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	u_int current = scr->cpy_start;
	u_int mouse_col = scr->mouse % N_COLS(dconf);
	u_int limit = current - mouse_col;
	u_int res = 0;

	while (GETCHAR(scr, current, &cell) == 0 && cell.uc == ' ' &&
	    current >= limit) {
		current--;
		res++;
	}
	if (res != 0)
		res--;
	return (res);
}

/*
 * Class of characters
 * Stolen from xterm sources of the Xfree project (see cvs tag below)
 * $TOG: button.c /main/76 1997/07/30 16:56:19 kaleb $
 */
static const int charClass[256] = {
/* NUL  SOH  STX  ETX  EOT  ENQ  ACK  BEL */
    32,   1,   1,   1,   1,   1,   1,   1,
/*  BS   HT   NL   VT   NP   CR   SO   SI */
     1,  32,   1,   1,   1,   1,   1,   1,
/* DLE  DC1  DC2  DC3  DC4  NAK  SYN  ETB */
     1,   1,   1,   1,   1,   1,   1,   1,
/* CAN   EM  SUB  ESC   FS   GS   RS   US */
     1,   1,   1,   1,   1,   1,   1,   1,
/*  SP    !    "    #    $    %    &    ' */
    32,  33,  34,  35,  36,  37,  38,  39,
/*   (    )    *    +    ,    -    .    / */
    40,  41,  42,  43,  44,  45,  46,  47,
/*   0    1    2    3    4    5    6    7 */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   8    9    :    ;    <    =    >    ? */
    48,  48,  58,  59,  60,  61,  62,  63,
/*   @    A    B    C    D    E    F    G */
    64,  48,  48,  48,  48,  48,  48,  48,
/*   H    I    J    K    L    M    N    O */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   P    Q    R    S    T    U    V    W */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   X    Y    Z    [    \    ]    ^    _ */
    48,  48,  48,  91,  92,  93,  94,  48,
/*   `    a    b    c    d    e    f    g */
    96,  48,  48,  48,  48,  48,  48,  48,
/*   h    i    j    k    l    m    n    o */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   p    q    r    s    t    u    v    w */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   x    y    z    {    |    }    ~  DEL */
    48,  48,  48, 123, 124, 125, 126,   1,
/* x80  x81  x82  x83  IND  NEL  SSA  ESA */
     1,   1,   1,   1,   1,   1,   1,   1,
/* HTS  HTJ  VTS  PLD  PLU   RI  SS2  SS3 */
     1,   1,   1,   1,   1,   1,   1,   1,
/* DCS  PU1  PU2  STS  CCH   MW  SPA  EPA */
     1,   1,   1,   1,   1,   1,   1,   1,
/* x98  x99  x9A  CSI   ST  OSC   PM  APC */
     1,   1,   1,   1,   1,   1,   1,   1,
/*   -    i   c/    L   ox   Y-    |   So */
   160, 161, 162, 163, 164, 165, 166, 167,
/*  ..   c0   ip   <<    _        R0    - */
   168, 169, 170, 171, 172, 173, 174, 175,
/*   o   +-    2    3    '    u   q|    . */
   176, 177, 178, 179, 180, 181, 182, 183,
/*   ,    1    2   >>  1/4  1/2  3/4    ? */
   184, 185, 186, 187, 188, 189, 190, 191,
/*  A`   A'   A^   A~   A:   Ao   AE   C, */
    48,  48,  48,  48,  48,  48,  48,  48,
/*  E`   E'   E^   E:   I`   I'   I^   I: */
    48,  48,  48,  48,  48,  48,  48,  48,
/*  D-   N~   O`   O'   O^   O~   O:    X */
    48,  48,  48,  48,  48,  48,  48, 216,
/*  O/   U`   U'   U^   U:   Y'    P    B */
    48,  48,  48,  48,  48,  48,  48,  48,
/*  a`   a'   a^   a~   a:   ao   ae   c, */
    48,  48,  48,  48,  48,  48,  48,  48,
/*  e`   e'   e^   e:    i`  i'   i^   i: */
    48,  48,  48,  48,  48,  48,  48,  48,
/*   d   n~   o`   o'   o^   o~   o:   -: */
    48,  48,  48,  48,  48,  48,  48,  248,
/*  o/   u`   u'   u^   u:   y'    P   y: */
    48,  48,  48,  48,  48,  48,  48,  48
};

/*
 * Find the first blank beginning after the current cursor position
 */
u_int
skip_char_right(struct wsscreen *scr, u_int offset)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	u_int current = offset;
	u_int limit = current +
	    (N_COLS(dconf) - (scr->mouse % N_COLS(dconf)) - 1);
	u_int class;
	u_int res = 0;

	GETCHAR(scr, current, &cell);
	class = charClass[cell.uc & 0xff];
	while (GETCHAR(scr, current, &cell) == 0 &&
	    charClass[cell.uc & 0xff] == class && current <= limit) {
		current++;
		res++;
	}
	if (res != 0)
		res--;
	return (res);
}

/*
 * Find the first non-blank character before the cursor position
 */
u_int
skip_char_left(struct wsscreen *scr, u_int offset)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	u_int current = offset;
	u_int limit = current - (scr->mouse % N_COLS(dconf));
	u_int class;
	u_int res = 0;

	GETCHAR(scr, current, &cell);
	class = charClass[cell.uc & 0xff];
	while (GETCHAR(scr, current, &cell) == 0 &&
	    charClass[cell.uc & 0xff] == class && current >= limit) {
		current--;
		res++;
	}
	if (res != 0)
		res--;
	return (res);
}

/*
 * Compare character classes
 */
u_int
class_cmp(struct wsscreen *scr, u_int first, u_int second)
{
	struct wsdisplay_charcell cell;
	u_int first_class;
	u_int second_class;

	if (GETCHAR(scr, first, &cell) != 0)
		return (1);
	first_class = charClass[cell.uc & 0xff];
	if (GETCHAR(scr, second, &cell) != 0)
		return (1);
	second_class = charClass[cell.uc & 0xff];

	if (first_class != second_class)
		return (1);
	else
		return (0);
}

/*
 * Beginning of a copy operation
 */
void
mouse_copy_start(struct wsscreen *scr)
{
	u_int right;

	/* if no selection, then that's the first one */
	SET(scr->sc->sc_flags, SC_PASTE_AVAIL);

	/* remove the previous selection */
	if (ISSET(scr->mouse_flags, SEL_EXISTS))
		remove_selection(scr);

	/* initial show of the cursor */
	if (!ISSET(scr->mouse_flags, MOUSE_VISIBLE))
		inverse_char(scr, scr->mouse);

	scr->cpy_start = scr->cpy_end = scr->mouse;
	scr->orig_start = scr->cpy_start;
	scr->orig_end = scr->cpy_end;
	scr->cursor = scr->cpy_end + 1; /* init value */

	/* useful later, in mouse_copy_extend */
	right = skip_spc_right(scr, BORDER);
	if (right)
		SET(scr->mouse_flags, BLANK_TO_EOL);

	SET(scr->mouse_flags, SEL_IN_PROGRESS | SEL_EXISTS | SEL_BY_CHAR);
	CLR(scr->mouse_flags, SEL_BY_WORD | SEL_BY_LINE);
	CLR(scr->mouse_flags, MOUSE_VISIBLE); /* cursor hidden in selection */
}

/*
 * Copy of the word under the cursor
 */
void
mouse_copy_word(struct wsscreen *scr)
{
	struct wsdisplay_charcell cell;
	u_int right;
	u_int left;

	if (ISSET(scr->mouse_flags, SEL_EXISTS))
		remove_selection(scr);

	if (ISSET(scr->mouse_flags, MOUSE_VISIBLE))
		inverse_char(scr, scr->mouse);

	scr->cpy_start = scr->cpy_end = scr->mouse;

	if (GETCHAR(scr, scr->mouse, &cell) == 0 &&
	    IS_ALPHANUM(cell.uc)) {
		right = skip_char_right(scr, scr->cpy_end);
		left = skip_char_left(scr, scr->cpy_start);
	} else {
		right = skip_spc_right(scr, NO_BORDER);
		left = skip_spc_left(scr);
	}

	scr->cpy_start -= left;
	scr->cpy_end += right;
	scr->orig_start = scr->cpy_start;
	scr->orig_end = scr->cpy_end;
	scr->cursor = scr->cpy_end + 1; /* init value, never happen */
	inverse_region(scr, scr->cpy_start, scr->cpy_end);

	SET(scr->mouse_flags, SEL_IN_PROGRESS | SEL_EXISTS | SEL_BY_WORD);
	CLR(scr->mouse_flags, SEL_BY_CHAR | SEL_BY_LINE);
	/* mouse cursor hidden in the selection */
	CLR(scr->mouse_flags, BLANK_TO_EOL | MOUSE_VISIBLE);
}

/*
 * Copy of the current line
 */
void
mouse_copy_line(struct wsscreen *scr)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	u_int row = scr->mouse / N_COLS(dconf);

	if (ISSET(scr->mouse_flags, SEL_EXISTS))
		remove_selection(scr);

	if (ISSET(scr->mouse_flags, MOUSE_VISIBLE))
		inverse_char(scr, scr->mouse);

	scr->cpy_start = row * N_COLS(dconf);
	scr->cpy_end = scr->cpy_start + (N_COLS(dconf) - 1);
	scr->orig_start = scr->cpy_start;
	scr->orig_end = scr->cpy_end;
	scr->cursor = scr->cpy_end + 1;
	inverse_region(scr, scr->cpy_start, scr->cpy_end);

	SET(scr->mouse_flags, SEL_IN_PROGRESS | SEL_EXISTS | SEL_BY_LINE);
	CLR(scr->mouse_flags, SEL_BY_CHAR | SEL_BY_WORD);
	/* mouse cursor hidden in the selection */
	CLR(scr->mouse_flags, BLANK_TO_EOL | MOUSE_VISIBLE);
}

/*
 * End of a copy operation
 */
void
mouse_copy_end(struct wsscreen *scr)
{
	CLR(scr->mouse_flags, SEL_IN_PROGRESS);
	if (ISSET(scr->mouse_flags, SEL_BY_WORD) ||
	    ISSET(scr->mouse_flags, SEL_BY_LINE)) {
		if (scr->cursor != scr->cpy_end + 1)
			inverse_char(scr, scr->cursor);
		scr->cursor = scr->cpy_end + 1;
	}
}


/*
 * Generic selection extend function
 */
void
mouse_copy_extend(struct wsscreen *scr)
{
	if (ISSET(scr->mouse_flags, SEL_BY_CHAR))
		mouse_copy_extend_char(scr);
	if (ISSET(scr->mouse_flags, SEL_BY_WORD))
		mouse_copy_extend_word(scr);
	if (ISSET(scr->mouse_flags, SEL_BY_LINE))
		mouse_copy_extend_line(scr);
}

/*
 * Extend a selected region, character by character
 */
void
mouse_copy_extend_char(struct wsscreen *scr)
{
	u_int right;

	if (!ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		if (ISSET(scr->mouse_flags, BLANK_TO_EOL)) {
			/*
			 * First extension of selection. We handle special
			 * cases of blank characters to eol
			 */

			right = skip_spc_right(scr, BORDER);
			if (scr->mouse > scr->orig_start) {
				/* the selection goes to the lower part of
				   the screen */

				/* remove the previous cursor, start of
				   selection is now next line */
				inverse_char(scr, scr->cpy_start);
				scr->cpy_start += (right + 1);
				scr->cpy_end = scr->cpy_start;
				scr->orig_start = scr->cpy_start;
				/* simulate the initial mark */
				inverse_char(scr, scr->cpy_start);
			} else {
				/* the selection goes to the upper part
				   of the screen */
				/* remove the previous cursor, start of
				   selection is now at the eol */
				inverse_char(scr, scr->cpy_start);
				scr->orig_start += (right + 1);
				scr->cpy_start = scr->orig_start - 1;
				scr->cpy_end = scr->orig_start - 1;
				/* simulate the initial mark */
				inverse_char(scr, scr->cpy_start);
			}
			CLR(scr->mouse_flags, BLANK_TO_EOL);
		}

		if (scr->mouse < scr->orig_start &&
		    scr->cpy_end >= scr->orig_start) {
			/* we go to the upper part of the screen */

			/* reverse the old selection region */
			remove_selection(scr);
			scr->cpy_end = scr->orig_start - 1;
			scr->cpy_start = scr->orig_start;
		}
		if (scr->cpy_start < scr->orig_start &&
		    scr->mouse >= scr->orig_start) {
			/* we go to the lower part of the screen */

			/* reverse the old selection region */

			remove_selection(scr);
			scr->cpy_start = scr->orig_start;
			scr->cpy_end = scr->orig_start - 1;
		}
		/* restore flags cleared in remove_selection() */
		SET(scr->mouse_flags, SEL_IN_PROGRESS | SEL_EXISTS);
	}

	if (scr->mouse >= scr->orig_start) {
		/* lower part of the screen */
		if (scr->mouse > scr->cpy_end) {
			/* extending selection */
			inverse_region(scr, scr->cpy_end + 1, scr->mouse);
		} else {
			/* reducing selection */
			inverse_region(scr, scr->mouse + 1, scr->cpy_end);
		}
		scr->cpy_end = scr->mouse;
	} else {
		/* upper part of the screen */
		if (scr->mouse < scr->cpy_start) {
			/* extending selection */
			inverse_region(scr, scr->mouse, scr->cpy_start - 1);
		} else {
			/* reducing selection */
			inverse_region(scr, scr->cpy_start, scr->mouse - 1);
		}
		scr->cpy_start = scr->mouse;
	}
}

/*
 * Extend a selected region, word by word
 */
void
mouse_copy_extend_word(struct wsscreen *scr)
{
	u_int old_cpy_end;
	u_int old_cpy_start;

	if (!ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		/* remove cursor in selection (black one) */
		if (scr->cursor != scr->cpy_end + 1)
			inverse_char(scr, scr->cursor);

		/* now, switch between lower and upper part of the screen */
		if (scr->mouse < scr->orig_start &&
		    scr->cpy_end >= scr->orig_start) {
			/* going to the upper part of the screen */
			inverse_region(scr, scr->orig_end + 1, scr->cpy_end);
			scr->cpy_end = scr->orig_end;
		}

		if (scr->mouse > scr->orig_end &&
		    scr->cpy_start <= scr->orig_start) {
			/* going to the lower part of the screen */
			inverse_region(scr, scr->cpy_start,
			    scr->orig_start - 1);
			scr->cpy_start = scr->orig_start;
		}
	}

	if (scr->mouse >= scr->orig_start) {
		/* lower part of the screen */
		if (scr->mouse > scr->cpy_end) {
			/* extending selection */
			old_cpy_end = scr->cpy_end;
			scr->cpy_end = scr->mouse +
			    skip_char_right(scr, scr->mouse);
			inverse_region(scr, old_cpy_end + 1, scr->cpy_end);
		} else {
			if (class_cmp(scr, scr->mouse, scr->mouse + 1)) {
				/* reducing selection (remove last word) */
				old_cpy_end = scr->cpy_end;
				scr->cpy_end = scr->mouse;
				inverse_region(scr, scr->cpy_end + 1,
				    old_cpy_end);
			} else {
				old_cpy_end = scr->cpy_end;
				scr->cpy_end = scr->mouse +
				    skip_char_right(scr, scr->mouse);
				if (scr->cpy_end != old_cpy_end) {
					/* reducing selection, from the end of
					 * next word */
					inverse_region(scr, scr->cpy_end + 1,
					    old_cpy_end);
				}
			}
		}
	} else {
		/* upper part of the screen */
		if (scr->mouse < scr->cpy_start) {
			/* extending selection */
			old_cpy_start = scr->cpy_start;
			scr->cpy_start = scr->mouse -
			    skip_char_left(scr, scr->mouse);
			inverse_region(scr, scr->cpy_start, old_cpy_start - 1);
		} else {
			if (class_cmp(scr, scr->mouse - 1, scr->mouse)) {
				/* reducing selection (remove last word) */
				old_cpy_start = scr->cpy_start;
				scr->cpy_start = scr->mouse;
				inverse_region(scr, old_cpy_start,
				    scr->cpy_start - 1);
			} else {
				old_cpy_start = scr->cpy_start;
				scr->cpy_start = scr->mouse -
				    skip_char_left(scr, scr->mouse);
				if (scr->cpy_start != old_cpy_start) {
					inverse_region(scr, old_cpy_start,
					    scr->cpy_start - 1);
				}
			}
		}
	}

	if (!ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		/* display new cursor */
		scr->cursor = scr->mouse;
		inverse_char(scr, scr->cursor);
	}
}

/*
 * Extend a selected region, line by line
 */
void
mouse_copy_extend_line(struct wsscreen *scr)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	u_int old_row;
	u_int new_row;
	u_int old_cpy_start;
	u_int old_cpy_end;

	if (!ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		/* remove cursor in selection (black one) */
		if (scr->cursor != scr->cpy_end + 1)
			inverse_char(scr, scr->cursor);

		/* now, switch between lower and upper part of the screen */
		if (scr->mouse < scr->orig_start &&
		    scr->cpy_end >= scr->orig_start) {
			/* going to the upper part of the screen */
			inverse_region(scr, scr->orig_end + 1, scr->cpy_end);
			scr->cpy_end = scr->orig_end;
		}

		if (scr->mouse > scr->orig_end &&
		    scr->cpy_start <= scr->orig_start) {
			/* going to the lower part of the screen */
			inverse_region(scr, scr->cpy_start,
			    scr->orig_start - 1);
			scr->cpy_start = scr->orig_start;
		}
	}

	if (scr->mouse >= scr->orig_start) {
		/* lower part of the screen */
		if (scr->cursor == scr->cpy_end + 1)
			scr->cursor = scr->cpy_end;
		old_row = scr->cursor / N_COLS(dconf);
		new_row = scr->mouse / N_COLS(dconf);
		old_cpy_end = scr->cpy_end;
		scr->cpy_end = new_row * N_COLS(dconf) + MAXCOL(dconf);
		if (new_row > old_row)
			inverse_region(scr, old_cpy_end + 1, scr->cpy_end);
		else if (new_row < old_row)
			inverse_region(scr, scr->cpy_end + 1, old_cpy_end);
	} else {
		/* upper part of the screen */
		old_row = scr->cursor / N_COLS(dconf);
		new_row = scr->mouse / N_COLS(dconf);
		old_cpy_start = scr->cpy_start;
		scr->cpy_start = new_row * N_COLS(dconf);
		if (new_row < old_row)
			inverse_region(scr, scr->cpy_start, old_cpy_start - 1);
		else if (new_row > old_row)
			inverse_region(scr, old_cpy_start, scr->cpy_start - 1);
	}

	if (!ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		/* display new cursor */
		scr->cursor = scr->mouse;
		inverse_char(scr, scr->cursor);
	}
}

/*
 * Add an extension to a selected region, word by word
 */
void
mouse_copy_extend_after(struct wsscreen *scr)
{
	u_int start_dist;
	u_int end_dist;

	if (ISSET(scr->mouse_flags, SEL_EXISTS)) {
		SET(scr->mouse_flags, SEL_EXT_AFTER);
		mouse_hide(scr); /* hide current cursor */

		if (scr->cpy_start > scr->mouse)
			start_dist = scr->cpy_start - scr->mouse;
		else
			start_dist = scr->mouse - scr->cpy_start;
		if (scr->mouse > scr->cpy_end)
			end_dist = scr->mouse - scr->cpy_end;
		else
			end_dist = scr->cpy_end - scr->mouse;
		if (start_dist < end_dist) {
			/* upper part of the screen*/
			scr->orig_start = scr->mouse + 1;
			/* only used in mouse_copy_extend_line() */
			scr->cursor = scr->cpy_start;
		} else {
			/* lower part of the screen */
			scr->orig_start = scr->mouse;
			/* only used in mouse_copy_extend_line() */
			scr->cursor = scr->cpy_end;
		}
		if (ISSET(scr->mouse_flags, SEL_BY_CHAR))
			mouse_copy_extend_char(scr);
		if (ISSET(scr->mouse_flags, SEL_BY_WORD))
			mouse_copy_extend_word(scr);
		if (ISSET(scr->mouse_flags, SEL_BY_LINE))
			mouse_copy_extend_line(scr);
		mouse_copy_selection(scr);
	}
}

void
mouse_hide(struct wsscreen *scr)
{
	if (ISSET(scr->mouse_flags, MOUSE_VISIBLE)) {
		inverse_char(scr, scr->mouse);
		CLR(scr->mouse_flags, MOUSE_VISIBLE);
	}
}

/*
 * Remove a previously selected region
 */
void
remove_selection(struct wsscreen *scr)
{
	if (ISSET(scr->mouse_flags, SEL_EXT_AFTER)) {
		/* reset the flag indicating an extension of selection */
		CLR(scr->mouse_flags, SEL_EXT_AFTER);
	}
	inverse_region(scr, scr->cpy_start, scr->cpy_end);
	CLR(scr->mouse_flags, SEL_IN_PROGRESS | SEL_EXISTS);
}

/*
 * Put the current visual selection in the selection buffer
 */
void
mouse_copy_selection(struct wsscreen *scr)
{
	struct wsscreen_internal *dconf = scr->scr_dconf;
	struct wsdisplay_charcell cell;
	u_int current = 0;
	u_int blank = current;
	u_int buf_end = (N_COLS(dconf) + 1) * N_ROWS(dconf);
	u_int sel_cur;
	u_int sel_end;

	sel_cur = scr->cpy_start;
	sel_end = scr->cpy_end;

	while (sel_cur <= sel_end && current < buf_end - 1) {
		if (GETCHAR(scr, sel_cur, &cell) != 0)
			break;
		scr->sc->sc_copybuffer[current] = cell.uc;
		if (!IS_SPACE(cell.uc))
			blank = current + 1; /* first blank after non-blank */
		current++;
		if (sel_cur % N_COLS(dconf) == MAXCOL(dconf)) {
			/*
			 * If we are on the last column of the screen,
			 * insert a carriage return.
			 */
			scr->sc->sc_copybuffer[blank] = '\r';
			current = ++blank;
		}
		sel_cur++;
	}

	scr->sc->sc_copybuffer[current] = '\0';
}

/*
 * Paste the current selection
 */
void
mouse_paste(struct wsscreen *scr)
{
	char *current = scr->sc->sc_copybuffer;
	struct tty *tp;
	u_int len;

	if (ISSET(scr->sc->sc_flags, SC_PASTE_AVAIL)) {
		if (!WSSCREEN_HAS_TTY(scr))
			return;

		tp = scr->scr_tty;
		for (len = strlen(scr->sc->sc_copybuffer); len != 0; len--)
			(*linesw[tp->t_line].l_rint)(*current++, tp);
	}
}

#ifdef HAVE_SCROLLBACK_SUPPORT
/*
 * Handle the z axis.
 * The z axis (roller or wheel) is mapped by default to scrollback.
 */
void
mouse_zaxis(struct wsscreen *scr, int z)
{
	if (z < 0)
		wsscrollback(scr->sc, WSDISPLAY_SCROLL_BACKWARD);
	else
		wsscrollback(scr->sc, WSDISPLAY_SCROLL_FORWARD);
}
#endif

/*
 * Allocate the copy buffer. The size is:
 * (cols + 1) * (rows)
 * (+1 for '\n' at the end of lines),
 * where cols and rows are the maximum of column and rows of all screens.
 */
void
allocate_copybuffer(struct wsdisplay_softc *sc)
{
	int nscreens = sc->sc_scrdata->nscreens;
	int i, s;
	const struct wsscreen_descr **screens_list = sc->sc_scrdata->screens;
	const struct wsscreen_descr *current;
	u_int size = sc->sc_copybuffer_size;

	s = spltty();
	for (i = 0; i < nscreens; i++) {
		current = *screens_list;
		if ((current->ncols + 1) * current->nrows > size)
			size = (current->ncols + 1) * current->nrows;
		screens_list++;
	}
	if (size != sc->sc_copybuffer_size && sc->sc_copybuffer_size != 0) {
		bzero(sc->sc_copybuffer, sc->sc_copybuffer_size);
		free(sc->sc_copybuffer, M_DEVBUF, sc->sc_copybuffer_size);
	}
	if ((sc->sc_copybuffer = (char *)malloc(size, M_DEVBUF, M_NOWAIT)) ==
	    NULL) {
		printf("%s: couldn't allocate copy buffer\n",
		    sc->sc_dv.dv_xname);
		size = 0;
	}
	sc->sc_copybuffer_size = size;
	splx(s);
}

/* Remove selection and cursor on current screen */
void
mouse_remove(struct wsscreen *scr)
{
	if (ISSET(scr->mouse_flags, SEL_EXISTS))
		remove_selection(scr);

	mouse_hide(scr);
}

#endif /* HAVE_WSMOUSED_SUPPORT */
