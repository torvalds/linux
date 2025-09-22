/*	$OpenBSD: sys_socket.c,v 1.68 2025/02/13 12:39:15 bluhm Exp $	*/
/*	$NetBSD: sys_socket.c,v 1.13 1995/08/12 23:59:09 mycroft Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)sys_socket.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#include <net/if.h>

const struct fileops socketops = {
	.fo_read	= soo_read,
	.fo_write	= soo_write,
	.fo_ioctl	= soo_ioctl,
	.fo_kqfilter	= soo_kqfilter,
	.fo_stat	= soo_stat,
	.fo_close	= soo_close
};

int
soo_read(struct file *fp, struct uio *uio, int fflags)
{
	struct socket *so = (struct socket *)fp->f_data;
	int flags = 0;

	if (fp->f_flag & FNONBLOCK)
		flags |= MSG_DONTWAIT;

	return (soreceive(so, NULL, uio, NULL, NULL, &flags, 0));
}

int
soo_write(struct file *fp, struct uio *uio, int fflags)
{
	struct socket *so = (struct socket *)fp->f_data;
	int flags = 0;

	if (fp->f_flag & FNONBLOCK)
		flags |= MSG_DONTWAIT;

	return (sosend(so, NULL, uio, NULL, NULL, flags));
}

int
soo_ioctl(struct file *fp, u_long cmd, caddr_t data, struct proc *p)
{
	struct socket *so = (struct socket *)fp->f_data;

	switch (cmd) {

	case FIOASYNC:
		mtx_enter(&so->so_rcv.sb_mtx);
		mtx_enter(&so->so_snd.sb_mtx);
		if (*(int *)data) {
			so->so_rcv.sb_flags |= SB_ASYNC;
			so->so_snd.sb_flags |= SB_ASYNC;
		} else {
			so->so_rcv.sb_flags &= ~SB_ASYNC;
			so->so_snd.sb_flags &= ~SB_ASYNC;
		}
		mtx_leave(&so->so_snd.sb_mtx);
		mtx_leave(&so->so_rcv.sb_mtx);
		break;

	case FIONREAD:
		*(int *)data = so->so_rcv.sb_datacc;
		break;

	case FIOSETOWN:
	case SIOCSPGRP:
	case TIOCSPGRP:
		return sigio_setown(&so->so_sigio, cmd, data);

	case FIOGETOWN:
	case SIOCGPGRP:
	case TIOCGPGRP:
		sigio_getown(&so->so_sigio, cmd, data);
		break;

	case SIOCATMARK:
		*(int *)data = (so->so_rcv.sb_state & SS_RCVATMARK) != 0;
		break;

	default:
		/*
		 * Interface/routing/protocol specific ioctls:
		 * interface and routing ioctls should have a
		 * different entry since a socket's unnecessary
		 */
		if (IOCGROUP(cmd) == 'i')
			return ifioctl(so, cmd, data, p);
		if (IOCGROUP(cmd) == 'r')
			return (EOPNOTSUPP);
		return pru_control(so, cmd, data, NULL);
	}

	return (0);
}

int
soo_stat(struct file *fp, struct stat *ub, struct proc *p)
{
	struct socket *so = fp->f_data;

	memset(ub, 0, sizeof (*ub));
	ub->st_mode = S_IFSOCK;
	solock_shared(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	if ((so->so_rcv.sb_state & SS_CANTRCVMORE) == 0 ||
	    so->so_rcv.sb_cc != 0)
		ub->st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
	mtx_leave(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	if ((so->so_snd.sb_state & SS_CANTSENDMORE) == 0)
		ub->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	mtx_leave(&so->so_snd.sb_mtx);
	ub->st_uid = so->so_euid;
	ub->st_gid = so->so_egid;
	(void)pru_sense(so, ub);
	sounlock_shared(so);
	return (0);
}

int
soo_close(struct file *fp, struct proc *p)
{
	int flags, error = 0;

	if (fp->f_data) {
		flags = (fp->f_flag & FNONBLOCK) ? MSG_DONTWAIT : 0;
		error = soclose(fp->f_data, flags);
	}
	fp->f_data = NULL;
	return (error);
}
