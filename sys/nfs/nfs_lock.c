/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      from BSDI nfs_lock.c,v 2.4 1998/12/14 23:49:56 jch Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>		/* for hz */
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/lockf.h>		/* for hz */ /* Must come after sys/malloc.h */
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <net/if.h>

#include <nfs/nfsproto.h>
#include <nfs/nfs_lock.h>
#include <nfsclient/nfs.h>
#include <nfsclient/nfsmount.h>
#include <nfsclient/nfsnode.h>
#include <nfsclient/nlminfo.h>

extern void (*nlminfo_release_p)(struct proc *p);

vop_advlock_t	*nfs_advlock_p = nfs_dolock;
vop_reclaim_t	*nfs_reclaim_p = NULL;

static MALLOC_DEFINE(M_NFSLOCK, "nfsclient_lock", "NFS lock request");
static MALLOC_DEFINE(M_NLMINFO, "nfsclient_nlminfo",
    "NFS lock process structure");

static int nfslockdans(struct thread *td, struct lockd_ans *ansp);
static void nlminfo_release(struct proc *p);
/*
 * --------------------------------------------------------------------
 * A miniature device driver which the userland uses to talk to us.
 *
 */

static struct cdev *nfslock_dev;
static struct mtx nfslock_mtx;
static int nfslock_isopen;
static TAILQ_HEAD(,__lock_msg)	nfslock_list;

static int
nfslock_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	int error;

	error = priv_check(td, PRIV_NFS_LOCKD);
	if (error)
		return (error);

	mtx_lock(&nfslock_mtx);
	if (!nfslock_isopen) {
		error = 0;
		nfslock_isopen = 1;
	} else {
		error = EOPNOTSUPP;
	}
	mtx_unlock(&nfslock_mtx);
		
	return (error);
}

static int
nfslock_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct __lock_msg *lm;

	mtx_lock(&nfslock_mtx);
	nfslock_isopen = 0;
	while (!TAILQ_EMPTY(&nfslock_list)) {
		lm = TAILQ_FIRST(&nfslock_list);
		/* XXX: answer request */
		TAILQ_REMOVE(&nfslock_list, lm, lm_link);
		free(lm, M_NFSLOCK);
	}
	mtx_unlock(&nfslock_mtx);
	return (0);
}

static int
nfslock_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error;
	struct __lock_msg *lm;

	if (uio->uio_resid != sizeof *lm)
		return (EOPNOTSUPP);
	lm = NULL;
	error = 0;
	mtx_lock(&nfslock_mtx);
	while (TAILQ_EMPTY(&nfslock_list)) {
		error = msleep(&nfslock_list, &nfslock_mtx, PSOCK | PCATCH,
		    "nfslockd", 0);
		if (error)
			break;
	}
	if (!error) {
		lm = TAILQ_FIRST(&nfslock_list);
		TAILQ_REMOVE(&nfslock_list, lm, lm_link);
	}
	mtx_unlock(&nfslock_mtx);
	if (!error) {
		error = uiomove(lm, sizeof *lm, uio);
		free(lm, M_NFSLOCK);
	}
	return (error);
}

static int
nfslock_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct lockd_ans la;
	int error;

	if (uio->uio_resid != sizeof la)
		return (EOPNOTSUPP);
	error = uiomove(&la, sizeof la, uio);
	if (!error)
		error = nfslockdans(curthread, &la);
	return (error);
}

static int
nfslock_send(struct __lock_msg *lm)
{
	struct __lock_msg *lm2;
	int error;

	error = 0;
	lm2 = malloc(sizeof *lm2, M_NFSLOCK, M_WAITOK);
	mtx_lock(&nfslock_mtx);
	if (nfslock_isopen) {
		memcpy(lm2, lm, sizeof *lm2);
		TAILQ_INSERT_TAIL(&nfslock_list, lm2, lm_link);
		wakeup(&nfslock_list);
	} else {
		error = EOPNOTSUPP;
	}
	mtx_unlock(&nfslock_mtx);
	if (error)
		free(lm2, M_NFSLOCK);
	return (error);
}

static struct cdevsw nfslock_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	nfslock_open,
	.d_close =	nfslock_close,
	.d_read =	nfslock_read,
	.d_write =	nfslock_write,
	.d_name =	"nfslock"
};

static int
nfslock_modevent(module_t mod __unused, int type, void *data __unused)
{

	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("nfslock: pseudo-device\n");
		mtx_init(&nfslock_mtx, "nfslock", NULL, MTX_DEF);
		TAILQ_INIT(&nfslock_list);
		nlminfo_release_p = nlminfo_release;
		nfslock_dev = make_dev(&nfslock_cdevsw, 0,
		    UID_ROOT, GID_KMEM, 0600, _PATH_NFSLCKDEV);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

DEV_MODULE(nfslock, nfslock_modevent, NULL);
MODULE_VERSION(nfslock, 1);


/*
 * XXX
 * We have to let the process know if the call succeeded.  I'm using an extra
 * field in the p_nlminfo field in the proc structure, as it is already for
 * lockd stuff.
 */

/*
 * nfs_advlock --
 *      NFS advisory byte-level locks.
 *
 * The vnode shall be (shared) locked on the entry, it is
 * unconditionally unlocked after.
 */
int
nfs_dolock(struct vop_advlock_args *ap)
{
	LOCKD_MSG msg;
	struct thread *td;
	struct vnode *vp;
	int error;
	struct flock *fl;
	struct proc *p;
	struct nfsmount *nmp;
	struct timeval boottime;

	td = curthread;
	p = td->td_proc;

	vp = ap->a_vp;
	fl = ap->a_fl;
	nmp = VFSTONFS(vp->v_mount);

	ASSERT_VOP_LOCKED(vp, "nfs_dolock");

	nmp->nm_getinfo(vp, msg.lm_fh, &msg.lm_fh_len, &msg.lm_addr,
	    &msg.lm_nfsv3, NULL, NULL);
	VOP_UNLOCK(vp, 0);

	/*
	 * the NLM protocol doesn't allow the server to return an error
	 * on ranges, so we do it.
	 */
	if (fl->l_whence != SEEK_END) {
		if ((fl->l_whence != SEEK_CUR && fl->l_whence != SEEK_SET) ||
		    fl->l_start < 0 ||
		    (fl->l_len < 0 &&
		     (fl->l_start == 0 || fl->l_start + fl->l_len < 0)))
			return (EINVAL);
		if (fl->l_len > 0 &&
			 (fl->l_len - 1 > OFF_MAX - fl->l_start))
			return (EOVERFLOW);
	}

	/*
	 * Fill in the information structure.
	 */
	msg.lm_version = LOCKD_MSG_VERSION;
	msg.lm_msg_ident.pid = p->p_pid;

	mtx_lock(&Giant);
	/*
	 * if there is no nfsowner table yet, allocate one.
	 */
	if (p->p_nlminfo == NULL) {
		p->p_nlminfo = malloc(sizeof(struct nlminfo),
		    M_NLMINFO, M_WAITOK | M_ZERO);
		p->p_nlminfo->pid_start = p->p_stats->p_start;
		getboottime(&boottime);
		timevaladd(&p->p_nlminfo->pid_start, &boottime);
	}
	msg.lm_msg_ident.pid_start = p->p_nlminfo->pid_start;
	msg.lm_msg_ident.msg_seq = ++(p->p_nlminfo->msg_seq);

	msg.lm_fl = *fl;
	msg.lm_wait = ap->a_flags & F_WAIT;
	msg.lm_getlk = ap->a_op == F_GETLK;
	cru2x(td->td_ucred, &msg.lm_cred);

	for (;;) {
		error = nfslock_send(&msg);
		if (error)
			goto out;

		/* Unlocks succeed immediately.  */
		if (fl->l_type == F_UNLCK)
			goto out;

		/*
		 * Retry after 20 seconds if we haven't gotten a response yet.
		 * This number was picked out of thin air... but is longer
		 * then even a reasonably loaded system should take (at least
		 * on a local network).  XXX Probably should use a back-off
		 * scheme.
		 *
		 * XXX: No PCATCH here since we currently have no useful
		 * way to signal to the userland rpc.lockd that the request
		 * has been aborted.  Once the rpc.lockd implementation
		 * can handle aborts, and we report them properly,
		 * PCATCH can be put back.  In the mean time, if we did
		 * permit aborting, the lock attempt would "get lost"
		 * and the lock would get stuck in the locked state.
		 */
		error = tsleep(p->p_nlminfo, PUSER, "lockd", 20*hz);
		if (error != 0) {
			if (error == EWOULDBLOCK) {
				/*
				 * We timed out, so we rewrite the request
				 * to the fifo.
				 */
				continue;
			}

			break;
		}

		if (msg.lm_getlk && p->p_nlminfo->retcode == 0) {
			if (p->p_nlminfo->set_getlk_pid) {
				fl->l_sysid = 0; /* XXX */
				fl->l_pid = p->p_nlminfo->getlk_pid;
			} else {
				fl->l_type = F_UNLCK;
			}
		}
		error = p->p_nlminfo->retcode;
		break;
	}
 out:
	mtx_unlock(&Giant);
	return (error);
}

/*
 * nfslockdans --
 *      NFS advisory byte-level locks answer from the lock daemon.
 */
static int
nfslockdans(struct thread *td, struct lockd_ans *ansp)
{
	struct proc *targetp;

	/* the version should match, or we're out of sync */
	if (ansp->la_vers != LOCKD_ANS_VERSION)
		return (EINVAL);

	/* Find the process, set its return errno and wake it up. */
	if ((targetp = pfind(ansp->la_msg_ident.pid)) == NULL)
		return (ESRCH);

	/* verify the pid hasn't been reused (if we can), and it isn't waiting
	 * for an answer from a more recent request.  We return an EPIPE if
	 * the match fails, because we've already used ESRCH above, and this
	 * is sort of like writing on a pipe after the reader has closed it.
	 */
	if (targetp->p_nlminfo == NULL ||
	    ((ansp->la_msg_ident.msg_seq != -1) &&
	      (timevalcmp(&targetp->p_nlminfo->pid_start,
			&ansp->la_msg_ident.pid_start, !=) ||
	       targetp->p_nlminfo->msg_seq != ansp->la_msg_ident.msg_seq))) {
		PROC_UNLOCK(targetp);
		return (EPIPE);
	}

	targetp->p_nlminfo->retcode = ansp->la_errno;
	targetp->p_nlminfo->set_getlk_pid = ansp->la_set_getlk_pid;
	targetp->p_nlminfo->getlk_pid = ansp->la_getlk_pid;

	wakeup(targetp->p_nlminfo);

	PROC_UNLOCK(targetp);
	return (0);
}

/*
 * Free nlminfo attached to process.
 */
void        
nlminfo_release(struct proc *p)
{  
	free(p->p_nlminfo, M_NLMINFO);
	p->p_nlminfo = NULL;
}
