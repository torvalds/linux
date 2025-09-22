/*	$OpenBSD: wsmuxvar.h,v 1.11 2019/02/18 17:39:14 anton Exp $	*/
/*      $NetBSD: wsmuxvar.h,v 1.10 2005/04/30 03:47:12 augustss Exp $   */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Lennart Augustsson <augustss@carlstedt.se>
 *         Carlstedt Research & Technology
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A ws event source, i.e., wskbd, wsmouse, or wsmux.
 */
struct wsevsrc {
	struct device me_dv;
	const struct wssrcops *me_ops;	/* method pointers */
	struct wseventvar me_evar;	/* wseventvar opened directly */
	struct wseventvar *me_evp;	/* our wseventvar when open */
#if NWSDISPLAY > 0
	struct device *me_dispdv;	/* our display if part of one */
#define	sc_displaydv	sc_base.me_dispdv
#endif
#if NWSMUX > 0
	struct wsmux_softc *me_parent;	/* parent mux device */
	TAILQ_ENTRY(wsevsrc) me_next;	/* sibling pointers */
#endif
};

/*
 * Methods that can be performed on an events source.  Usually called
 * from a wsmux.
 */
struct wssrcops {
	int type;			/* device type: WSMUX_{MOUSE,KBD,MUX} */
	int (*dopen)(struct wsevsrc *, struct wseventvar *);
	int (*dclose)(struct wsevsrc *);
	int (*dioctl)(struct device *, u_long, caddr_t, int, struct proc *);
	int (*ddispioctl)(struct device *, u_long, caddr_t, int, struct proc *);
	int (*dsetdisplay)(struct device *, struct device *);
};

#define wsevsrc_open(me, evp) \
	((me)->me_ops->dopen((me), evp))
#define wsevsrc_close(me) \
	((me)->me_ops->dclose((me)))
#define wsevsrc_ioctl(me, cmd, data, flag, p) \
	((me)->me_ops->dioctl(&(me)->me_dv, cmd, (caddr_t)data, flag, p))
#define wsevsrc_display_ioctl(me, cmd, data, flag, p) \
	((me)->me_ops->ddispioctl(&(me)->me_dv, cmd, (caddr_t)data, flag, p))
#define wsevsrc_set_display(me, arg) \
	((me)->me_ops->dsetdisplay(&(me)->me_dv, arg))

#if NWSMUX > 0
struct wsmux_softc {
	struct wsevsrc sc_base;
	struct proc *sc_p;		/* open proc */
	TAILQ_HEAD(, wsevsrc) sc_cld;	/* list of children */
	struct rwlock sc_lock;		/* lock for sc_cld */
	u_int32_t sc_kbd_layout;	/* current layout of keyboard */
#ifdef WSDISPLAY_COMPAT_RAWKBD
	int sc_rawkbd;			/* A hack to remember the kbd mode */
#endif
};

/*
 * configure defines
 */
#define WSMOUSEDEVCF_MUX		0

struct	wsmux_softc *wsmux_getmux(int);
struct	wsmux_softc *wsmux_create(const char *, int);
int	wsmux_attach_sc(struct wsmux_softc *, struct wsevsrc *);
void	wsmux_detach_sc(struct wsevsrc *);
int	wsmux_set_display(struct wsmux_softc *, struct device *);
uint32_t wsmux_get_layout(struct wsmux_softc *);
void	wsmux_set_layout(struct wsmux_softc *, uint32_t);

int	wskbd_add_mux(int, struct wsmux_softc *);
int	wsmouse_add_mux(int, struct wsmux_softc *);

#endif	/* NWSMUX > 0 */
