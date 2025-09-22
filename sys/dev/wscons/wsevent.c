/* $OpenBSD: wsevent.c,v 1.30 2025/07/18 17:34:29 mvs Exp $ */
/* $NetBSD: wsevent.c,v 1.16 2003/08/07 16:31:29 agc Exp $ */

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
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)event.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Internal "wscons_event" queue interface for the keyboard and mouse drivers.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wseventvar.h>

void	filt_wseventdetach(struct knote *);
int	filt_wseventread(struct knote *, long);
int	filt_wseventmodify(struct kevent *, struct knote *);
int	filt_wseventprocess(struct knote *, struct kevent *);

const struct filterops wsevent_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_wseventdetach,
	.f_event	= filt_wseventread,
	.f_modify	= filt_wseventmodify,
	.f_process	= filt_wseventprocess,
};

/*
 * Initialize a wscons_event queue.
 */
int
wsevent_init(struct wseventvar *ev)
{
	struct wscons_event *queue;

	if (ev->ws_q != NULL)
		return (0);

        queue = mallocarray(WSEVENT_QSIZE, sizeof(struct wscons_event),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (ev->ws_q != NULL) {
		free(queue, M_DEVBUF,
		    WSEVENT_QSIZE * sizeof(struct wscons_event));
		return (1);
	}

	mtx_init_flags(&ev->ws_mtx, IPL_TTY, "wsmtx", 0);
	klist_init_mutex(&ev->ws_klist, &ev->ws_mtx);

	ev->ws_q = queue;
	ev->ws_get = ev->ws_put = 0;

	sigio_init(&ev->ws_sigio);

	return (0);
}

/*
 * Tear down a wscons_event queue.
 */
void
wsevent_fini(struct wseventvar *ev)
{
	if (ev->ws_q == NULL) {
#ifdef DIAGNOSTIC
		printf("wsevent_fini: already invoked\n");
#endif
		return;
	}
	free(ev->ws_q, M_DEVBUF, WSEVENT_QSIZE * sizeof(struct wscons_event));
	ev->ws_q = NULL;

	klist_invalidate(&ev->ws_klist);

	sigio_free(&ev->ws_sigio);
}

/*
 * User-level interface: read, kqueue.
 * (User cannot write an event queue.)
 */
int
wsevent_read(struct wseventvar *ev, struct uio *uio, int flags)
{
	int error, wrap = 0;
	u_int cnt, tcnt, get;
	size_t n;

	/*
	 * Make sure we can return at least 1.
	 */
	if (uio->uio_resid < sizeof(struct wscons_event))
		return (EMSGSIZE);	/* ??? */
	n = howmany(uio->uio_resid, sizeof(struct wscons_event));

	mtx_enter(&ev->ws_mtx);

	while (ev->ws_get == ev->ws_put) {
		if (flags & IO_NDELAY) {
			mtx_leave(&ev->ws_mtx);
			return (EWOULDBLOCK);
		}
		ev->ws_wanted = 1;
		error = msleep_nsec(ev, &ev->ws_mtx, PWSEVENT | PCATCH,
		    "wsevent_read", INFSLP);
		if (error) {
			mtx_leave(&ev->ws_mtx);
			return (error);
		}
	}
	/*
	 * Move wscons_event from tail end of queue (there is at least one
	 * there).
	 */
	if (ev->ws_put < ev->ws_get)
		cnt = WSEVENT_QSIZE - ev->ws_get; /* events in [get..QSIZE) */
	else
		cnt = ev->ws_put - ev->ws_get;    /* events in [get..put) */

	if (cnt > n)
		cnt = n;

	get = ev->ws_get;
	tcnt = ev->ws_put;
	n -= cnt;

	ev->ws_get = (get + cnt) % WSEVENT_QSIZE;
	if (!(ev->ws_get != 0 || n == 0 || tcnt == 0)) {
		wrap = 1;

		if (tcnt > n)
			tcnt = n;
		ev->ws_get = tcnt;
	}

	mtx_leave(&ev->ws_mtx);

	error = uiomove((caddr_t)&ev->ws_q[get],
	    cnt * sizeof(struct wscons_event), uio);

	/*
	 * If we do wrap to 0, move from front of queue to put index, if
	 * there is anything there to move.
	 */
	if (wrap && error == 0) {
		error = uiomove((caddr_t)&ev->ws_q[0],
		    tcnt * sizeof(struct wscons_event), uio);
	}

	return (error);
}

int
wsevent_kqfilter(struct wseventvar *ev, struct knote *kn)
{
	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &wsevent_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = ev;
	klist_insert(&ev->ws_klist, kn);

	return (0);
}

void
filt_wseventdetach(struct knote *kn)
{
	struct wseventvar *ev = kn->kn_hook;

	klist_remove(&ev->ws_klist, kn);
}

int
filt_wseventread(struct knote *kn, long hint)
{
	struct wseventvar *ev = kn->kn_hook;

	MUTEX_ASSERT_LOCKED(&ev->ws_mtx);

	if (ev->ws_get == ev->ws_put)
		return (0);

	if (ev->ws_get < ev->ws_put)
		kn->kn_data = ev->ws_put - ev->ws_get;
	else
		kn->kn_data = (WSEVENT_QSIZE - ev->ws_get) + ev->ws_put;

	return (1);
}

int
filt_wseventmodify(struct kevent *kev, struct knote *kn)
{
	struct wseventvar *ev = kn->kn_hook;
	int active;

	mtx_enter(&ev->ws_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&ev->ws_mtx);

	return (active);
}

int
filt_wseventprocess(struct knote *kn, struct kevent *kev)
{
	struct wseventvar *ev = kn->kn_hook;
	int active;

	mtx_enter(&ev->ws_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&ev->ws_mtx);

	return (active);
}
