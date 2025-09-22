/*	$OpenBSD: nfs_kq.c,v 1.37 2024/05/01 13:15:59 jsg Exp $ */
/*	$NetBSD: nfs_kq.c,v 1.7 2003/10/30 01:43:10 simonb Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jaromir Dolecek.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/kthread.h>
#include <sys/rwlock.h>
#include <sys/queue.h>

#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>
#include <nfs/nfs_var.h>

void	nfs_kqpoll(void *);
int	nfs_kqwatch(struct vnode *);
void	nfs_kqunwatch(struct vnode *);

void	filt_nfsdetach(struct knote *);
int	filt_nfsread(struct knote *, long);
int	filt_nfswrite(struct knote *, long);
int	filt_nfsvnode(struct knote *, long);

struct kevq {
	SLIST_ENTRY(kevq)	kev_link;
	struct vnode		*vp;
	u_int			usecount;
	u_int			flags;
#define KEVQ_BUSY	0x01	/* currently being processed */
#define KEVQ_WANT	0x02	/* want to change this entry */
	struct timespec		omtime;	/* old modification time */
	struct timespec		octime;	/* old change time */
	nlink_t			onlink;	/* old number of references to file */
};
SLIST_HEAD(kevqlist, kevq);

struct rwlock nfskevq_lock = RWLOCK_INITIALIZER("nfskqlk");
struct proc *pnfskq;
struct kevqlist kevlist = SLIST_HEAD_INITIALIZER(kevlist);

/*
 * This quite simplistic routine periodically checks for server changes
 * of any of the watched files every NFS_MINATTRTIMO/2 seconds.
 * Only changes in size, modification time, change time and nlinks
 * are being checked, everything else is ignored.
 * The routine only calls VOP_GETATTR() when it's likely it would get
 * some new data, i.e. when the vnode expires from attrcache. This
 * should give same result as periodically running stat(2) from userland,
 * while keeping CPU/network usage low, and still provide proper kevent
 * semantics.
 * The poller thread is created when first vnode is added to watch list,
 * and exits when the watch list is empty. The overhead of thread creation
 * isn't really important, neither speed of attach and detach of knote.
 */
void
nfs_kqpoll(void *arg)
{
	struct kevq *ke;
	struct vattr attr;
	struct proc *p = pnfskq;
	u_quad_t osize;
	int error;

	for(;;) {
		rw_enter_write(&nfskevq_lock);
		SLIST_FOREACH(ke, &kevlist, kev_link) {
			struct nfsnode *np = VTONFS(ke->vp);

#ifdef DEBUG
			printf("nfs_kqpoll on: ");
			VOP_PRINT(ke->vp);
#endif
			/* skip if still in attrcache */
			if (nfs_getattrcache(ke->vp, &attr) != ENOENT)
				continue;

			/*
			 * Mark entry busy, release lock and check
			 * for changes.
			 */
			ke->flags |= KEVQ_BUSY;
			rw_exit_write(&nfskevq_lock);

			/* save v_size, nfs_getattr() updates it */
			osize = np->n_size;

			error = VOP_GETATTR(ke->vp, &attr, p->p_ucred, p);
			if (error == ESTALE) {
				NFS_INVALIDATE_ATTRCACHE(np);
				VN_KNOTE(ke->vp, NOTE_DELETE);
				goto next;
			}

			/* following is a bit fragile, but about best
			 * we can get */
			if (attr.va_size != osize) {
				int flags = NOTE_WRITE;

				if (attr.va_size > osize)
					flags |= NOTE_EXTEND;
				else
					flags |= NOTE_TRUNCATE;

				VN_KNOTE(ke->vp, flags);
				ke->omtime = attr.va_mtime;
			} else if (attr.va_mtime.tv_sec != ke->omtime.tv_sec
			    || attr.va_mtime.tv_nsec != ke->omtime.tv_nsec) {
				VN_KNOTE(ke->vp, NOTE_WRITE);
				ke->omtime = attr.va_mtime;
			}

			if (attr.va_ctime.tv_sec != ke->octime.tv_sec
			    || attr.va_ctime.tv_nsec != ke->octime.tv_nsec) {
				VN_KNOTE(ke->vp, NOTE_ATTRIB);
				ke->octime = attr.va_ctime;
			}

			if (attr.va_nlink != ke->onlink) {
				VN_KNOTE(ke->vp, NOTE_LINK);
				ke->onlink = attr.va_nlink;
			}

next:
			rw_enter_write(&nfskevq_lock);
			ke->flags &= ~KEVQ_BUSY;
			if (ke->flags & KEVQ_WANT) {
				ke->flags &= ~KEVQ_WANT;
				wakeup(ke);
			}
		}

		if (SLIST_EMPTY(&kevlist)) {
			/* Nothing more to watch, exit */
			pnfskq = NULL;
			rw_exit_write(&nfskevq_lock);
			kthread_exit(0);
		}
		rw_exit_write(&nfskevq_lock);

		/* wait a while before checking for changes again */
		tsleep_nsec(pnfskq, PSOCK, "nfskqpw",
		    SEC_TO_NSEC(NFS_MINATTRTIMO) / 2);
	}
}

void
filt_nfsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	klist_remove_locked(&vp->v_klist, kn);

	/* Remove the vnode from watch list */
	if ((kn->kn_flags & (__EV_POLL | __EV_SELECT)) == 0)
		nfs_kqunwatch(vp);
}

void
nfs_kqunwatch(struct vnode *vp)
{
	struct kevq *ke;

	rw_enter_write(&nfskevq_lock);
	SLIST_FOREACH(ke, &kevlist, kev_link) {
		if (ke->vp == vp) {
			while (ke->flags & KEVQ_BUSY) {
				ke->flags |= KEVQ_WANT;
				rw_exit_write(&nfskevq_lock);
				tsleep_nsec(ke, PSOCK, "nfskqdet", INFSLP);
				rw_enter_write(&nfskevq_lock);
			}

			if (ke->usecount > 1) {
				/* keep, other kevents need this */
				ke->usecount--;
			} else {
				/* last user, g/c */
				SLIST_REMOVE(&kevlist, ke, kevq, kev_link);
				free(ke, M_KEVENT, sizeof(*ke));
			}
			break;
		}
	}
	rw_exit_write(&nfskevq_lock);
}

int
filt_nfsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct nfsnode *np = VTONFS(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = np->n_size - foffset(kn->kn_fp);
#ifdef DEBUG
	printf("nfsread event. %lld\n", kn->kn_data);
#endif
	if (kn->kn_data == 0 && kn->kn_sfflags & NOTE_EOF) {
		kn->kn_fflags |= NOTE_EOF;
		return (1);
	}

	if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
		return (1);

        return (kn->kn_data != 0);
}

int
filt_nfswrite(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = 0;
	return (1);
}

int
filt_nfsvnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}

static const struct filterops nfsread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_nfsdetach,
	.f_event	= filt_nfsread,
};

static const struct filterops nfswrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_nfsdetach,
	.f_event	= filt_nfswrite,
};

static const struct filterops nfsvnode_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_nfsdetach,
	.f_event	= filt_nfsvnode,
};

int
nfs_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct vnode *vp;
	struct knote *kn;

	vp = ap->a_vp;
	kn = ap->a_kn;

#ifdef DEBUG
	printf("nfs_kqfilter(%d) on: ", kn->kn_filter);
	VOP_PRINT(vp);
#endif

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &nfsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &nfswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &nfsvnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = vp;

	/*
	 * Put the vnode to watched list.
	 */
	if ((kn->kn_flags & (__EV_POLL | __EV_SELECT)) == 0) {
		int error;

		error = nfs_kqwatch(vp);
		if (error)
			return (error);
	}

	klist_insert_locked(&vp->v_klist, kn);

	return (0);
}

int
nfs_kqwatch(struct vnode *vp)
{
	struct proc *p = curproc;	/* XXX */
	struct vattr attr;
	struct kevq *ke;
	int error = 0;

	/*
	 * Fetch current attributes. It's only needed when the vnode
	 * is not watched yet, but we need to do this without lock
	 * held. This is likely cheap due to attrcache, so do it now.
	 */ 
	memset(&attr, 0, sizeof(attr));
	(void) VOP_GETATTR(vp, &attr, p->p_ucred, p);

	rw_enter_write(&nfskevq_lock);

	/* ensure the poller is running */
	if (!pnfskq) {
		error = kthread_create(nfs_kqpoll, NULL, &pnfskq,
				"nfskqpoll");
		if (error)
			goto out;
	}

	SLIST_FOREACH(ke, &kevlist, kev_link)
		if (ke->vp == vp)
			break;

	if (ke) {
		/* already watched, so just bump usecount */
		ke->usecount++;
	} else {
		/* need a new one */
		ke = malloc(sizeof(*ke), M_KEVENT, M_WAITOK);
		ke->vp = vp;
		ke->usecount = 1;
		ke->flags = 0;
		ke->omtime = attr.va_mtime;
		ke->octime = attr.va_ctime;
		ke->onlink = attr.va_nlink;
		SLIST_INSERT_HEAD(&kevlist, ke, kev_link);
	}

	/* kick the poller */
	wakeup(pnfskq);

out:
	rw_exit_write(&nfskevq_lock);
	return (error);
}
