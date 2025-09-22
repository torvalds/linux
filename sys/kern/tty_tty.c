/*	$OpenBSD: tty_tty.c,v 1.34 2025/04/17 12:01:26 jsg Exp $	*/
/*	$NetBSD: tty_tty.c,v 1.13 1996/03/30 22:24:46 christos Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)tty_tty.c	8.2 (Berkeley) 9/23/93
 */

/*
 * Indirect driver for controlling tty.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/fcntl.h>


#define cttyvp(p) \
	((p)->p_p->ps_flags & PS_CONTROLT ? \
	    (p)->p_p->ps_session->s_ttyvp : NULL)

int
cttyopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vnode *ttyvp = cttyvp(p);
	int error;

	if (ttyvp == NULL)
		return (ENXIO);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(ttyvp, flag, NOCRED, p);
	VOP_UNLOCK(ttyvp);
	return (error);
}

int
cttyread(dev_t dev, struct uio *uio, int flag)
{
	struct vnode *ttyvp = cttyvp(uio->uio_procp);
	int error;

	if (ttyvp == NULL)
		return (EIO);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_READ(ttyvp, uio, flag, NOCRED);
	VOP_UNLOCK(ttyvp);
	return (error);
}

int
cttywrite(dev_t dev, struct uio *uio, int flag)
{
	struct vnode *ttyvp = cttyvp(uio->uio_procp);
	int error;

	if (ttyvp == NULL)
		return (EIO);
	vn_lock(ttyvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_WRITE(ttyvp, uio, flag, NOCRED);
	VOP_UNLOCK(ttyvp);
	return (error);
}

int
cttyioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct vnode *ttyvp = cttyvp(p);
	struct session *sess;
	int error, secs;

	if (ttyvp == NULL)
		return (EIO);
	switch (cmd) {
	case TIOCSCTTY:			/* XXX */
		return (EINVAL);
	case TIOCNOTTY:
		if (!SESS_LEADER(p->p_p)) {
			atomic_clearbits_int(&p->p_p->ps_flags, PS_CONTROLT);
			return (0);
		} else
			return (EINVAL);
	case TIOCSETVERAUTH:
		if ((error = suser(p)))
			return error;
		secs = *(int *)addr;
		if (secs < 1 || secs > 3600)
			return EINVAL;
		sess = p->p_p->ps_pgrp->pg_session;
		sess->s_verauthuid = p->p_ucred->cr_ruid;
		sess->s_verauthppid = p->p_p->ps_ppid;
		timeout_add_sec(&sess->s_verauthto, secs);
		return 0;
	case TIOCCLRVERAUTH:
		sess = p->p_p->ps_pgrp->pg_session;
		timeout_del(&sess->s_verauthto);
		zapverauth(sess);
		return 0;
	case TIOCCHKVERAUTH:
		/*
		 * It's not clear when or what these checks are for.
		 * How can we reach this code with a different ruid?
		 * The ppid check is also more porous than desired.
		 * Nevertheless, the checks reflect the original intention;
		 * namely, that it be the same user using the same shell.
		 */
		sess = p->p_p->ps_pgrp->pg_session;
		if (sess->s_verauthuid == p->p_ucred->cr_ruid &&
		    sess->s_verauthppid == p->p_p->ps_ppid)
			return 0;
		return EPERM;
	}
	return (VOP_IOCTL(ttyvp, cmd, addr, flag, NOCRED, p));
}

int
cttykqfilter(dev_t dev, struct knote *kn)
{
	struct vnode *ttyvp = cttyvp(curproc);

	if (ttyvp == NULL) {
		if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
			return (seltrue_kqfilter(dev, kn));
		return (ENXIO);
	}
	return (VOP_KQFILTER(ttyvp, FREAD|FWRITE, kn));
}
