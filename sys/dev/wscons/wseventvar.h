/* $OpenBSD: wseventvar.h,v 1.15 2025/07/18 17:34:29 mvs Exp $ */
/* $NetBSD: wseventvar.h,v 1.1 1998/03/22 14:24:03 drochner Exp $ */

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
 *	@(#)event_var.h	8.1 (Berkeley) 6/11/93
 */

#include <sys/event.h>
#include <sys/sigio.h>
#include <sys/signalvar.h>

/*
 * Internal "wscons_event" queue interface for the keyboard and mouse drivers.
 * The drivers are expected not to place events in the queue above spltty(),
 * i.e., are expected to run off serial ports or similar devices.
 */

/*
 * Locks used to protect data
 *	I	immutable
 *	m	ws_mtx
 */

/* WSEVENT_QSIZE should be a power of two so that `%' is fast */
#define	WSEVENT_QSIZE	256	/* may need tuning; this uses 2k */

struct wseventvar {
	u_int	ws_get;			/* [m] get (read) index (modified
					    synchronously) */
	volatile u_int ws_put;		/* [m] put (write) index (modified by
					    interrupt) */
	struct mutex ws_mtx;
	struct klist ws_klist;		/* [m] list of knotes */
	struct sigio_ref ws_sigio;	/* async I/O registration */
	int	ws_wanted;		/* [m] wake up on input ready */
	int	ws_async;		/* [m] send SIGIO on input ready */
	struct wscons_event *ws_q;	/* [m] circular buffer (queue) of
					    events */
};

static inline void
wsevent_wakeup(struct wseventvar *ev)
{
	int dosigio = 0;

	knote(&ev->ws_klist, 0);

	mtx_enter(&ev->ws_mtx);
	if (ev->ws_wanted) {
		ev->ws_wanted = 0;
		wakeup(ev);
	}
	if (ev->ws_async)
		dosigio = 1;
	mtx_leave(&ev->ws_mtx);

	if (dosigio)
		pgsigio(&ev->ws_sigio, SIGIO, 0);
}

int	wsevent_init(struct wseventvar *);
void	wsevent_fini(struct wseventvar *);
int	wsevent_read(struct wseventvar *, struct uio *, int);
int	wsevent_kqfilter(struct wseventvar *, struct knote *);

/*
 * PWSEVENT is set just above PSOCK, which is just above TTIPRI, on the
 * theory that mouse and keyboard `user' input should be quick.
 */
#define	PWSEVENT	23
