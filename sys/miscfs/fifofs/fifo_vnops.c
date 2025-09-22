/*	$OpenBSD: fifo_vnops.c,v 1.110 2025/01/30 14:40:50 mvs Exp $	*/
/*	$NetBSD: fifo_vnops.c,v 1.18 1996/03/16 23:52:42 christos Exp $	*/

/*
 * Copyright (c) 1990, 1993
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
 *	@(#)fifo_vnops.c	8.4 (Berkeley) 8/10/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/event.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/unistd.h>

#include <miscfs/fifofs/fifo.h>

/*
 * This structure is associated with the FIFO vnode and stores
 * the state associated with the FIFO.
 */
struct fifoinfo {
	struct socket	*fi_readsock;
	struct socket	*fi_writesock;
	long		fi_readers;
	long		fi_writers;
};

const struct vops fifo_vops = {
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= fifo_open,
	.vop_close	= fifo_close,
	.vop_access	= fifo_ebadf,
	.vop_getattr	= fifo_ebadf,
	.vop_setattr	= fifo_ebadf,
	.vop_read	= fifo_read,
	.vop_write	= fifo_write,
	.vop_ioctl	= fifo_ioctl,
	.vop_kqfilter	= fifo_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= nullop,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_inactive	= fifo_inactive,
	.vop_reclaim	= fifo_reclaim,
	.vop_lock	= nullop,
	.vop_unlock	= nullop,
	.vop_islocked	= nullop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= vop_generic_badop,
	.vop_print	= fifo_print,
	.vop_pathconf	= fifo_pathconf,
	.vop_advlock	= fifo_advlock,
	.vop_bwrite	= nullop
};

void	filt_fifordetach(struct knote *kn);
int	filt_fiforead(struct knote *kn, long hint);
void	filt_fifowdetach(struct knote *kn);
int	filt_fifowrite(struct knote *kn, long hint);
int	filt_fifoexcept(struct knote *kn, long hint);
int	filt_fiformodify(struct kevent *kev, struct knote *kn);
int	filt_fiforprocess(struct knote *kn, struct kevent *kev);
int	filt_fifowmodify(struct kevent *kev, struct knote *kn);
int	filt_fifowprocess(struct knote *kn, struct kevent *kev);

const struct filterops fiforead_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_fifordetach,
	.f_event	= filt_fiforead,
	.f_modify	= filt_fiformodify,
	.f_process	= filt_fiforprocess,
};

const struct filterops fifowrite_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_fifowdetach,
	.f_event	= filt_fifowrite,
	.f_modify	= filt_fifowmodify,
	.f_process	= filt_fifowprocess,
};

const struct filterops fifoexcept_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_fifordetach,
	.f_event	= filt_fifoexcept,
	.f_modify	= filt_fiformodify,
	.f_process	= filt_fiforprocess,
};

/*
 * Open called to set up a new instance of a fifo or
 * to find an active instance of a fifo.
 */
int
fifo_open(void *v)
{
	struct vop_open_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip;
	struct socket *rso, *wso;
	int error;

	if ((fip = vp->v_fifoinfo) == NULL) {
		fip = malloc(sizeof(*fip), M_VNODE, M_WAITOK);
		vp->v_fifoinfo = fip;
		if ((error = socreate(AF_UNIX, &rso, SOCK_STREAM, 0)) != 0) {
			free(fip, M_VNODE, sizeof *fip);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readsock = rso;
		if ((error = socreate(AF_UNIX, &wso, SOCK_STREAM, 0)) != 0) {
			(void)soclose(rso, 0);
			free(fip, M_VNODE, sizeof *fip);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_writesock = wso;
		if ((error = soconnect2(wso, rso)) != 0) {
			(void)soclose(wso, 0);
			(void)soclose(rso, 0);
			free(fip, M_VNODE, sizeof *fip);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readers = fip->fi_writers = 0;
		/*
		 * Should take both solock() and `sb_mtx' mutex for
		 * SS_CANTSENDMORE flag modifications.
		 */
		solock(wso);
		mtx_enter(&wso->so_snd.sb_mtx);
		wso->so_snd.sb_state |= SS_CANTSENDMORE;
		wso->so_snd.sb_lowat = PIPE_BUF;
		mtx_leave(&wso->so_snd.sb_mtx);
		sounlock(wso);
	} else {
		rso = fip->fi_readsock;
		wso = fip->fi_writesock;
	}
	if (ap->a_mode & FREAD) {
		fip->fi_readers++;
		if (fip->fi_readers == 1) {
			solock(wso);
			mtx_enter(&wso->so_snd.sb_mtx);
			wso->so_snd.sb_state &= ~SS_CANTSENDMORE;
			mtx_leave(&wso->so_snd.sb_mtx);
			sounlock(wso);
			if (fip->fi_writers > 0)
				wakeup(&fip->fi_writers);
		}
	}
	if (ap->a_mode & FWRITE) {
		fip->fi_writers++;
		if ((ap->a_mode & O_NONBLOCK) && fip->fi_readers == 0) {
			error = ENXIO;
			goto bad;
		}
		if (fip->fi_writers == 1) {
			solock(rso);
			rso->so_state &= ~SS_ISDISCONNECTED;
			mtx_enter(&rso->so_rcv.sb_mtx);
			rso->so_rcv.sb_state &= ~SS_CANTRCVMORE;
			mtx_leave(&rso->so_rcv.sb_mtx);
			sounlock(rso);
			if (fip->fi_readers > 0)
				wakeup(&fip->fi_readers);
		}
	}
	if ((ap->a_mode & O_NONBLOCK) == 0) {
		if ((ap->a_mode & FREAD) && fip->fi_writers == 0) {
			VOP_UNLOCK(vp);
			error = tsleep_nsec(&fip->fi_readers,
			    PCATCH | PSOCK, "fifor", INFSLP);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error)
				goto bad;
		}
		if ((ap->a_mode & FWRITE) && fip->fi_readers == 0) {
			VOP_UNLOCK(vp);
			error = tsleep_nsec(&fip->fi_writers,
			    PCATCH | PSOCK, "fifow", INFSLP);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			if (error)
				goto bad;
		}
	}
	return (0);
bad:
	VOP_CLOSE(vp, ap->a_mode, ap->a_cred, ap->a_p);
	return (error);
}

/*
 * Vnode op for read
 */
int
fifo_read(void *v)
{
	struct vop_read_args *ap = v;
	struct uio *uio = ap->a_uio;
	struct socket *rso = ap->a_vp->v_fifoinfo->fi_readsock;
	int error, flags = 0;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("fifo_read mode");
#endif
	if (uio->uio_resid == 0)
		return (0);
	if (ap->a_ioflag & IO_NDELAY)
		flags |= MSG_DONTWAIT;
	VOP_UNLOCK(ap->a_vp);
	error = soreceive(rso, NULL, uio, NULL, NULL, &flags, 0);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY);
	if (ap->a_ioflag & IO_NDELAY) {
		if (error == EWOULDBLOCK &&
		    ap->a_vp->v_fifoinfo->fi_writers == 0)
			error = 0;
	}
	return (error);
}

/*
 * Vnode op for write
 */
int
fifo_write(void *v)
{
	struct vop_write_args *ap = v;
	struct socket *wso = ap->a_vp->v_fifoinfo->fi_writesock;
	int error, flags = 0;

#ifdef DIAGNOSTIC
	if (ap->a_uio->uio_rw != UIO_WRITE)
		panic("fifo_write mode");
#endif
	if (ap->a_ioflag & IO_NDELAY)
		flags |= MSG_DONTWAIT;
	VOP_UNLOCK(ap->a_vp);
	error = sosend(wso, NULL, ap->a_uio, NULL, NULL, flags);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY);
	return (error);
}

/*
 * Device ioctl operation.
 */
int
fifo_ioctl(void *v)
{
	struct vop_ioctl_args *ap = v;
	struct file filetmp;
	int error;

	if (ap->a_fflag & FREAD) {
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_readsock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_p);
		if (error)
			return (error);
	}
	if (ap->a_fflag & FWRITE) {
		filetmp.f_data = ap->a_vp->v_fifoinfo->fi_writesock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_p);
		if (error)
			return (error);
	}
	return (0);
}

int
fifo_inactive(void *v)
{
	struct vop_inactive_args *ap = v;

	VOP_UNLOCK(ap->a_vp);
	return (0);
}


/*
 * Device close routine
 */
int
fifo_close(void *v)
{
	struct vop_close_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip = vp->v_fifoinfo;
	int error1 = 0, error2 = 0;

	if (fip == NULL)
		return (0);

	if (ap->a_fflag & FREAD) {
		if (--fip->fi_readers == 0) {
			struct socket *wso = fip->fi_writesock;

			solock(wso);
			socantsendmore(wso);
			sounlock(wso);
		}
	}
	if (ap->a_fflag & FWRITE) {
		if (--fip->fi_writers == 0) {
			struct socket *rso = fip->fi_readsock;

			solock(rso);
			/* SS_ISDISCONNECTED will result in POLLHUP */
			rso->so_state |= SS_ISDISCONNECTED;
			socantrcvmore(rso);
			sounlock(rso);
		}
	}
	if (fip->fi_readers == 0 && fip->fi_writers == 0) {
		error1 = soclose(fip->fi_readsock, 0);
		error2 = soclose(fip->fi_writesock, 0);
		free(fip, M_VNODE, sizeof *fip);
		vp->v_fifoinfo = NULL;
	}
	return (error1 ? error1 : error2);
}

int
fifo_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip = vp->v_fifoinfo;

	if (fip == NULL)
		return (0);

	soclose(fip->fi_readsock, 0);
	soclose(fip->fi_writesock, 0);
	free(fip, M_VNODE, sizeof *fip);
	vp->v_fifoinfo = NULL;

	return (0);
}

/*
 * Print out the contents of a fifo vnode.
 */
int
fifo_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;

	printf("tag VT_NON");
	fifo_printinfo(ap->a_vp);
	printf("\n");
#endif
	return 0;
}

#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
/*
 * Print out internal contents of a fifo vnode.
 */
void
fifo_printinfo(struct vnode *vp)
{
	struct fifoinfo *fip = vp->v_fifoinfo;

	printf(", fifo with %ld readers and %ld writers",
		fip->fi_readers, fip->fi_writers);
}
#endif

/*
 * Return POSIX pathconf information applicable to fifo's.
 */
int
fifo_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*ap->a_retval = 1;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Fifo failed operation
 */
int
fifo_ebadf(void *v)
{

	return (EBADF);
}

/*
 * Fifo advisory byte-level locks.
 */
int
fifo_advlock(void *v)
{
	return (EOPNOTSUPP);
}

int
fifo_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct fifoinfo *fip = ap->a_vp->v_fifoinfo;
	struct sockbuf *sb;
	struct socket *so;

	switch (ap->a_kn->kn_filter) {
	case EVFILT_READ:
		if (!(ap->a_fflag & FREAD))
			return (EINVAL);
		ap->a_kn->kn_fop = &fiforead_filtops;
		so = fip->fi_readsock;
		sb = &so->so_rcv;
		break;
	case EVFILT_WRITE:
		if (!(ap->a_fflag & FWRITE)) {
			/* Tell upper layer to ask for POLLUP only */
			if (ap->a_kn->kn_flags & (__EV_POLL | __EV_SELECT))
				return (EPERM);
			return (EINVAL);
		}
		ap->a_kn->kn_fop = &fifowrite_filtops;
		so = fip->fi_writesock;
		sb = &so->so_snd;
		break;
	case EVFILT_EXCEPT:
		if (ap->a_kn->kn_flags & __EV_SELECT) {
			/* Prevent triggering exceptfds. */
			return (EPERM);
		}
		if ((ap->a_kn->kn_flags & __EV_POLL) == 0) {
			/* Disallow usage through kevent(2). */
			return (EINVAL);
		}
		ap->a_kn->kn_fop = &fifoexcept_filtops;
		so = fip->fi_readsock;
		sb = &so->so_rcv;
		break;
	default:
		return (EINVAL);
	}

	ap->a_kn->kn_hook = so;

	klist_insert(&sb->sb_klist, ap->a_kn);

	return (0);
}

void
filt_fifordetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	klist_remove(&so->so_rcv.sb_klist, kn);
}

int
filt_fiforead(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_hook;
	int rv;

	MUTEX_ASSERT_LOCKED(&so->so_rcv.sb_mtx);

	kn->kn_data = so->so_rcv.sb_cc;
	if (so->so_rcv.sb_state & SS_CANTRCVMORE) {
		kn->kn_flags |= EV_EOF;
		if (kn->kn_flags & __EV_POLL) {
			if (so->so_state & SS_ISDISCONNECTED)
				kn->kn_flags |= __EV_HUP;
			else
				kn->kn_flags &= ~__EV_HUP;
		}
		rv = 1;
	} else {
		kn->kn_flags &= ~(EV_EOF | __EV_HUP);
		rv = (kn->kn_data > 0);
	}

	return (rv);
}

void
filt_fifowdetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_hook;

	klist_remove(&so->so_snd.sb_klist, kn);
}

int
filt_fifowrite(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_hook;
	int rv;

	MUTEX_ASSERT_LOCKED(&so->so_snd.sb_mtx);

	kn->kn_data = sbspace_locked(&so->so_snd);
	if (so->so_snd.sb_state & SS_CANTSENDMORE) {
		kn->kn_flags |= EV_EOF;
		rv = 1;
	} else {
		kn->kn_flags &= ~EV_EOF;
		rv = (kn->kn_data >= so->so_snd.sb_lowat);
	}

	return (rv);
}

int
filt_fifoexcept(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_hook;
	int rv = 0;

	MUTEX_ASSERT_LOCKED(&so->so_rcv.sb_mtx);

	if (kn->kn_flags & __EV_POLL) {
		if (so->so_state & SS_ISDISCONNECTED) {
			kn->kn_flags |= __EV_HUP;
			rv = 1;
		} else {
			kn->kn_flags &= ~__EV_HUP;
		}
	}

	return (rv);
}

int
filt_fiformodify(struct kevent *kev, struct knote *kn)
{
	struct socket *so = kn->kn_hook;
	int rv;

	solock(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_modify(kev, kn);
	mtx_leave(&so->so_rcv.sb_mtx);
	sounlock(so);

	return (rv);
}

int
filt_fiforprocess(struct knote *kn, struct kevent *kev)
{
	struct socket *so = kn->kn_hook;
	int rv;

	solock(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_process(kn, kev);
	mtx_leave(&so->so_rcv.sb_mtx);
	sounlock(so);

	return (rv);
}

int
filt_fifowmodify(struct kevent *kev, struct knote *kn)
{
	struct socket *so = kn->kn_hook;
	int rv;

	mtx_enter(&so->so_snd.sb_mtx);
	rv = knote_modify(kev, kn);
	mtx_leave(&so->so_snd.sb_mtx);

	return (rv);
}

int
filt_fifowprocess(struct knote *kn, struct kevent *kev)
{
	struct socket *so = kn->kn_hook;
	int rv;

	mtx_enter(&so->so_snd.sb_mtx);
	rv = knote_process(kn, kev);
	mtx_leave(&so->so_snd.sb_mtx);

	return (rv);
}
