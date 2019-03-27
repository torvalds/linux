/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, Jeffrey Roberson <jeff@freebsd.org>
 * Copyright (c) 2008-2009, Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2009-2010, The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by Lawrence Stewart under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/alq.h>
#include <sys/malloc.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <sys/eventhandler.h>

#include <security/mac/mac_framework.h>

/* Async. Logging Queue */
struct alq {
	char	*aq_entbuf;		/* Buffer for stored entries */
	int	aq_entmax;		/* Max entries */
	int	aq_entlen;		/* Entry length */
	int	aq_freebytes;		/* Bytes available in buffer */
	int	aq_buflen;		/* Total length of our buffer */
	int	aq_writehead;		/* Location for next write */
	int	aq_writetail;		/* Flush starts at this location */
	int	aq_wrapearly;		/* # bytes left blank at end of buf */
	int	aq_flags;		/* Queue flags */
	int	aq_waiters;		/* Num threads waiting for resources
					 * NB: Used as a wait channel so must
					 * not be first field in the alq struct
					 */
	struct	ale	aq_getpost;	/* ALE for use by get/post */
	struct mtx	aq_mtx;		/* Queue lock */
	struct vnode	*aq_vp;		/* Open vnode handle */
	struct ucred	*aq_cred;	/* Credentials of the opening thread */
	LIST_ENTRY(alq)	aq_act;		/* List of active queues */
	LIST_ENTRY(alq)	aq_link;	/* List of all queues */
};

#define	AQ_WANTED	0x0001		/* Wakeup sleeper when io is done */
#define	AQ_ACTIVE	0x0002		/* on the active list */
#define	AQ_FLUSHING	0x0004		/* doing IO */
#define	AQ_SHUTDOWN	0x0008		/* Queue no longer valid */
#define	AQ_ORDERED	0x0010		/* Queue enforces ordered writes */
#define	AQ_LEGACY	0x0020		/* Legacy queue (fixed length writes) */

#define	ALQ_LOCK(alq)	mtx_lock_spin(&(alq)->aq_mtx)
#define	ALQ_UNLOCK(alq)	mtx_unlock_spin(&(alq)->aq_mtx)

#define HAS_PENDING_DATA(alq) ((alq)->aq_freebytes != (alq)->aq_buflen)

static MALLOC_DEFINE(M_ALD, "ALD", "ALD");

/*
 * The ald_mtx protects the ald_queues list and the ald_active list.
 */
static struct mtx ald_mtx;
static LIST_HEAD(, alq) ald_queues;
static LIST_HEAD(, alq) ald_active;
static int ald_shutingdown = 0;
struct thread *ald_thread;
static struct proc *ald_proc;
static eventhandler_tag alq_eventhandler_tag = NULL;

#define	ALD_LOCK()	mtx_lock(&ald_mtx)
#define	ALD_UNLOCK()	mtx_unlock(&ald_mtx)

/* Daemon functions */
static int ald_add(struct alq *);
static int ald_rem(struct alq *);
static void ald_startup(void *);
static void ald_daemon(void);
static void ald_shutdown(void *, int);
static void ald_activate(struct alq *);
static void ald_deactivate(struct alq *);

/* Internal queue functions */
static void alq_shutdown(struct alq *);
static void alq_destroy(struct alq *);
static int alq_doio(struct alq *);


/*
 * Add a new queue to the global list.  Fail if we're shutting down.
 */
static int
ald_add(struct alq *alq)
{
	int error;

	error = 0;

	ALD_LOCK();
	if (ald_shutingdown) {
		error = EBUSY;
		goto done;
	}
	LIST_INSERT_HEAD(&ald_queues, alq, aq_link);
done:
	ALD_UNLOCK();
	return (error);
}

/*
 * Remove a queue from the global list unless we're shutting down.  If so,
 * the ald will take care of cleaning up it's resources.
 */
static int
ald_rem(struct alq *alq)
{
	int error;

	error = 0;

	ALD_LOCK();
	if (ald_shutingdown) {
		error = EBUSY;
		goto done;
	}
	LIST_REMOVE(alq, aq_link);
done:
	ALD_UNLOCK();
	return (error);
}

/*
 * Put a queue on the active list.  This will schedule it for writing.
 */
static void
ald_activate(struct alq *alq)
{
	LIST_INSERT_HEAD(&ald_active, alq, aq_act);
	wakeup(&ald_active);
}

static void
ald_deactivate(struct alq *alq)
{
	LIST_REMOVE(alq, aq_act);
	alq->aq_flags &= ~AQ_ACTIVE;
}

static void
ald_startup(void *unused)
{
	mtx_init(&ald_mtx, "ALDmtx", NULL, MTX_DEF|MTX_QUIET);
	LIST_INIT(&ald_queues);
	LIST_INIT(&ald_active);
}

static void
ald_daemon(void)
{
	int needwakeup;
	struct alq *alq;

	ald_thread = FIRST_THREAD_IN_PROC(ald_proc);

	alq_eventhandler_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    ald_shutdown, NULL, SHUTDOWN_PRI_FIRST);

	ALD_LOCK();

	for (;;) {
		while ((alq = LIST_FIRST(&ald_active)) == NULL &&
		    !ald_shutingdown)
			mtx_sleep(&ald_active, &ald_mtx, PWAIT, "aldslp", 0);

		/* Don't shutdown until all active ALQs are flushed. */
		if (ald_shutingdown && alq == NULL) {
			ALD_UNLOCK();
			break;
		}

		ALQ_LOCK(alq);
		ald_deactivate(alq);
		ALD_UNLOCK();
		needwakeup = alq_doio(alq);
		ALQ_UNLOCK(alq);
		if (needwakeup)
			wakeup_one(alq);
		ALD_LOCK();
	}

	kproc_exit(0);
}

static void
ald_shutdown(void *arg, int howto)
{
	struct alq *alq;

	ALD_LOCK();

	/* Ensure no new queues can be created. */
	ald_shutingdown = 1;

	/* Shutdown all ALQs prior to terminating the ald_daemon. */
	while ((alq = LIST_FIRST(&ald_queues)) != NULL) {
		LIST_REMOVE(alq, aq_link);
		ALD_UNLOCK();
		alq_shutdown(alq);
		ALD_LOCK();
	}

	/* At this point, all ALQs are flushed and shutdown. */

	/*
	 * Wake ald_daemon so that it exits. It won't be able to do
	 * anything until we mtx_sleep because we hold the ald_mtx.
	 */
	wakeup(&ald_active);

	/* Wait for ald_daemon to exit. */
	mtx_sleep(ald_proc, &ald_mtx, PWAIT, "aldslp", 0);

	ALD_UNLOCK();
}

static void
alq_shutdown(struct alq *alq)
{
	ALQ_LOCK(alq);

	/* Stop any new writers. */
	alq->aq_flags |= AQ_SHUTDOWN;

	/*
	 * If the ALQ isn't active but has unwritten data (possible if
	 * the ALQ_NOACTIVATE flag has been used), explicitly activate the
	 * ALQ here so that the pending data gets flushed by the ald_daemon.
	 */
	if (!(alq->aq_flags & AQ_ACTIVE) && HAS_PENDING_DATA(alq)) {
		alq->aq_flags |= AQ_ACTIVE;
		ALQ_UNLOCK(alq);
		ALD_LOCK();
		ald_activate(alq);
		ALD_UNLOCK();
		ALQ_LOCK(alq);
	}

	/* Drain IO */
	while (alq->aq_flags & AQ_ACTIVE) {
		alq->aq_flags |= AQ_WANTED;
		msleep_spin(alq, &alq->aq_mtx, "aldclose", 0);
	}
	ALQ_UNLOCK(alq);

	vn_close(alq->aq_vp, FWRITE, alq->aq_cred,
	    curthread);
	crfree(alq->aq_cred);
}

void
alq_destroy(struct alq *alq)
{
	/* Drain all pending IO. */
	alq_shutdown(alq);

	mtx_destroy(&alq->aq_mtx);
	free(alq->aq_entbuf, M_ALD);
	free(alq, M_ALD);
}

/*
 * Flush all pending data to disk.  This operation will block.
 */
static int
alq_doio(struct alq *alq)
{
	struct thread *td;
	struct mount *mp;
	struct vnode *vp;
	struct uio auio;
	struct iovec aiov[2];
	int totlen;
	int iov;
	int wrapearly;

	KASSERT((HAS_PENDING_DATA(alq)), ("%s: queue empty!", __func__));

	vp = alq->aq_vp;
	td = curthread;
	totlen = 0;
	iov = 1;
	wrapearly = alq->aq_wrapearly;

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));

	/* Start the write from the location of our buffer tail pointer. */
	aiov[0].iov_base = alq->aq_entbuf + alq->aq_writetail;

	if (alq->aq_writetail < alq->aq_writehead) {
		/* Buffer not wrapped. */
		totlen = aiov[0].iov_len = alq->aq_writehead - alq->aq_writetail;
	} else if (alq->aq_writehead == 0) {
		/* Buffer not wrapped (special case to avoid an empty iov). */
		totlen = aiov[0].iov_len = alq->aq_buflen - alq->aq_writetail -
		    wrapearly;
	} else {
		/*
		 * Buffer wrapped, requires 2 aiov entries:
		 * - first is from writetail to end of buffer
		 * - second is from start of buffer to writehead
		 */
		aiov[0].iov_len = alq->aq_buflen - alq->aq_writetail -
		    wrapearly;
		iov++;
		aiov[1].iov_base = alq->aq_entbuf;
		aiov[1].iov_len =  alq->aq_writehead;
		totlen = aiov[0].iov_len + aiov[1].iov_len;
	}

	alq->aq_flags |= AQ_FLUSHING;
	ALQ_UNLOCK(alq);

	auio.uio_iov = &aiov[0];
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_iovcnt = iov;
	auio.uio_resid = totlen;
	auio.uio_td = td;

	/*
	 * Do all of the junk required to write now.
	 */
	vn_start_write(vp, &mp, V_WAIT);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	/*
	 * XXX: VOP_WRITE error checks are ignored.
	 */
#ifdef MAC
	if (mac_vnode_check_write(alq->aq_cred, NOCRED, vp) == 0)
#endif
		VOP_WRITE(vp, &auio, IO_UNIT | IO_APPEND, alq->aq_cred);
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);

	ALQ_LOCK(alq);
	alq->aq_flags &= ~AQ_FLUSHING;

	/* Adjust writetail as required, taking into account wrapping. */
	alq->aq_writetail = (alq->aq_writetail + totlen + wrapearly) %
	    alq->aq_buflen;
	alq->aq_freebytes += totlen + wrapearly;

	/*
	 * If we just flushed part of the buffer which wrapped, reset the
	 * wrapearly indicator.
	 */
	if (wrapearly)
		alq->aq_wrapearly = 0;

	/*
	 * If we just flushed the buffer completely, reset indexes to 0 to
	 * minimise buffer wraps.
	 * This is also required to ensure alq_getn() can't wedge itself.
	 */
	if (!HAS_PENDING_DATA(alq))
		alq->aq_writehead = alq->aq_writetail = 0;

	KASSERT((alq->aq_writetail >= 0 && alq->aq_writetail < alq->aq_buflen),
	    ("%s: aq_writetail < 0 || aq_writetail >= aq_buflen", __func__));

	if (alq->aq_flags & AQ_WANTED) {
		alq->aq_flags &= ~AQ_WANTED;
		return (1);
	}

	return(0);
}

static struct kproc_desc ald_kp = {
        "ALQ Daemon",
        ald_daemon,
        &ald_proc
};

SYSINIT(aldthread, SI_SUB_KTHREAD_IDLE, SI_ORDER_ANY, kproc_start, &ald_kp);
SYSINIT(ald, SI_SUB_LOCK, SI_ORDER_ANY, ald_startup, NULL);


/* User visible queue functions */

/*
 * Create the queue data structure, allocate the buffer, and open the file.
 */

int
alq_open_flags(struct alq **alqp, const char *file, struct ucred *cred, int cmode,
    int size, int flags)
{
	struct thread *td;
	struct nameidata nd;
	struct alq *alq;
	int oflags;
	int error;

	KASSERT((size > 0), ("%s: size <= 0", __func__));

	*alqp = NULL;
	td = curthread;

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, file, td);
	oflags = FWRITE | O_NOFOLLOW | O_CREAT;

	error = vn_open_cred(&nd, &oflags, cmode, 0, cred, NULL);
	if (error)
		return (error);

	NDFREE(&nd, NDF_ONLY_PNBUF);
	/* We just unlock so we hold a reference */
	VOP_UNLOCK(nd.ni_vp, 0);

	alq = malloc(sizeof(*alq), M_ALD, M_WAITOK|M_ZERO);
	alq->aq_vp = nd.ni_vp;
	alq->aq_cred = crhold(cred);

	mtx_init(&alq->aq_mtx, "ALD Queue", NULL, MTX_SPIN|MTX_QUIET);

	alq->aq_buflen = size;
	alq->aq_entmax = 0;
	alq->aq_entlen = 0;

	alq->aq_freebytes = alq->aq_buflen;
	alq->aq_entbuf = malloc(alq->aq_buflen, M_ALD, M_WAITOK|M_ZERO);
	alq->aq_writehead = alq->aq_writetail = 0;
	if (flags & ALQ_ORDERED)
		alq->aq_flags |= AQ_ORDERED;

	if ((error = ald_add(alq)) != 0) {
		alq_destroy(alq);
		return (error);
	}

	*alqp = alq;

	return (0);
}

int
alq_open(struct alq **alqp, const char *file, struct ucred *cred, int cmode,
    int size, int count)
{
	int ret;

	KASSERT((count >= 0), ("%s: count < 0", __func__));

	if (count > 0) {
		if ((ret = alq_open_flags(alqp, file, cred, cmode,
		    size*count, 0)) == 0) {
			(*alqp)->aq_flags |= AQ_LEGACY;
			(*alqp)->aq_entmax = count;
			(*alqp)->aq_entlen = size;
		}
	} else
		ret = alq_open_flags(alqp, file, cred, cmode, size, 0);

	return (ret);
}


/*
 * Copy a new entry into the queue.  If the operation would block either
 * wait or return an error depending on the value of waitok.
 */
int
alq_writen(struct alq *alq, void *data, int len, int flags)
{
	int activate, copy, ret;
	void *waitchan;

	KASSERT((len > 0 && len <= alq->aq_buflen),
	    ("%s: len <= 0 || len > aq_buflen", __func__));

	activate = ret = 0;
	copy = len;
	waitchan = NULL;

	ALQ_LOCK(alq);

	/*
	 * Fail to perform the write and return EWOULDBLOCK if:
	 * - The message is larger than our underlying buffer.
	 * - The ALQ is being shutdown.
	 * - There is insufficient free space in our underlying buffer
	 *   to accept the message and the user can't wait for space.
	 * - There is insufficient free space in our underlying buffer
	 *   to accept the message and the alq is inactive due to prior
	 *   use of the ALQ_NOACTIVATE flag (which would lead to deadlock).
	 */
	if (len > alq->aq_buflen ||
	    alq->aq_flags & AQ_SHUTDOWN ||
	    (((flags & ALQ_NOWAIT) || (!(alq->aq_flags & AQ_ACTIVE) &&
	    HAS_PENDING_DATA(alq))) && alq->aq_freebytes < len)) {
		ALQ_UNLOCK(alq);
		return (EWOULDBLOCK);
	}

	/*
	 * If we want ordered writes and there is already at least one thread
	 * waiting for resources to become available, sleep until we're woken.
	 */
	if (alq->aq_flags & AQ_ORDERED && alq->aq_waiters > 0) {
		KASSERT(!(flags & ALQ_NOWAIT),
		    ("%s: ALQ_NOWAIT set but incorrectly ignored!", __func__));
		alq->aq_waiters++;
		msleep_spin(&alq->aq_waiters, &alq->aq_mtx, "alqwnord", 0);
		alq->aq_waiters--;
	}

	/*
	 * (ALQ_WAITOK && aq_freebytes < len) or aq_freebytes >= len, either
	 * enter while loop and sleep until we have enough free bytes (former)
	 * or skip (latter). If AQ_ORDERED is set, only 1 thread at a time will
	 * be in this loop. Otherwise, multiple threads may be sleeping here
	 * competing for ALQ resources.
	 */
	while (alq->aq_freebytes < len && !(alq->aq_flags & AQ_SHUTDOWN)) {
		KASSERT(!(flags & ALQ_NOWAIT),
		    ("%s: ALQ_NOWAIT set but incorrectly ignored!", __func__));
		alq->aq_flags |= AQ_WANTED;
		alq->aq_waiters++;
		if (waitchan)
			wakeup(waitchan);
		msleep_spin(alq, &alq->aq_mtx, "alqwnres", 0);
		alq->aq_waiters--;

		/*
		 * If we're the first thread to wake after an AQ_WANTED wakeup
		 * but there isn't enough free space for us, we're going to loop
		 * and sleep again. If there are other threads waiting in this
		 * loop, schedule a wakeup so that they can see if the space
		 * they require is available.
		 */
		if (alq->aq_waiters > 0 && !(alq->aq_flags & AQ_ORDERED) &&
		    alq->aq_freebytes < len && !(alq->aq_flags & AQ_WANTED))
			waitchan = alq;
		else
			waitchan = NULL;
	}

	/*
	 * If there are waiters, we need to signal the waiting threads after we
	 * complete our work. The alq ptr is used as a wait channel for threads
	 * requiring resources to be freed up. In the AQ_ORDERED case, threads
	 * are not allowed to concurrently compete for resources in the above
	 * while loop, so we use a different wait channel in this case.
	 */
	if (alq->aq_waiters > 0) {
		if (alq->aq_flags & AQ_ORDERED)
			waitchan = &alq->aq_waiters;
		else
			waitchan = alq;
	} else
		waitchan = NULL;

	/* Bail if we're shutting down. */
	if (alq->aq_flags & AQ_SHUTDOWN) {
		ret = EWOULDBLOCK;
		goto unlock;
	}

	/*
	 * If we need to wrap the buffer to accommodate the write,
	 * we'll need 2 calls to bcopy.
	 */
	if ((alq->aq_buflen - alq->aq_writehead) < len)
		copy = alq->aq_buflen - alq->aq_writehead;

	/* Copy message (or part thereof if wrap required) to the buffer. */
	bcopy(data, alq->aq_entbuf + alq->aq_writehead, copy);
	alq->aq_writehead += copy;

	if (alq->aq_writehead >= alq->aq_buflen) {
		KASSERT((alq->aq_writehead == alq->aq_buflen),
		    ("%s: alq->aq_writehead (%d) > alq->aq_buflen (%d)",
		    __func__,
		    alq->aq_writehead,
		    alq->aq_buflen));
		alq->aq_writehead = 0;
	}

	if (copy != len) {
		/*
		 * Wrap the buffer by copying the remainder of our message
		 * to the start of the buffer and resetting aq_writehead.
		 */
		bcopy(((uint8_t *)data)+copy, alq->aq_entbuf, len - copy);
		alq->aq_writehead = len - copy;
	}

	KASSERT((alq->aq_writehead >= 0 && alq->aq_writehead < alq->aq_buflen),
	    ("%s: aq_writehead < 0 || aq_writehead >= aq_buflen", __func__));

	alq->aq_freebytes -= len;

	if (!(alq->aq_flags & AQ_ACTIVE) && !(flags & ALQ_NOACTIVATE)) {
		alq->aq_flags |= AQ_ACTIVE;
		activate = 1;
	}

	KASSERT((HAS_PENDING_DATA(alq)), ("%s: queue empty!", __func__));

unlock:
	ALQ_UNLOCK(alq);

	if (activate) {
		ALD_LOCK();
		ald_activate(alq);
		ALD_UNLOCK();
	}

	/* NB: We rely on wakeup_one waking threads in a FIFO manner. */
	if (waitchan != NULL)
		wakeup_one(waitchan);

	return (ret);
}

int
alq_write(struct alq *alq, void *data, int flags)
{
	/* Should only be called in fixed length message (legacy) mode. */
	KASSERT((alq->aq_flags & AQ_LEGACY),
	    ("%s: fixed length write on variable length queue", __func__));
	return (alq_writen(alq, data, alq->aq_entlen, flags));
}

/*
 * Retrieve a pointer for the ALQ to write directly into, avoiding bcopy.
 */
struct ale *
alq_getn(struct alq *alq, int len, int flags)
{
	int contigbytes;
	void *waitchan;

	KASSERT((len > 0 && len <= alq->aq_buflen),
	    ("%s: len <= 0 || len > alq->aq_buflen", __func__));

	waitchan = NULL;

	ALQ_LOCK(alq);

	/*
	 * Determine the number of free contiguous bytes.
	 * We ensure elsewhere that if aq_writehead == aq_writetail because
	 * the buffer is empty, they will both be set to 0 and therefore
	 * aq_freebytes == aq_buflen and is fully contiguous.
	 * If they are equal and the buffer is not empty, aq_freebytes will
	 * be 0 indicating the buffer is full.
	 */
	if (alq->aq_writehead <= alq->aq_writetail)
		contigbytes = alq->aq_freebytes;
	else {
		contigbytes = alq->aq_buflen - alq->aq_writehead;

		if (contigbytes < len) {
			/*
			 * Insufficient space at end of buffer to handle a
			 * contiguous write. Wrap early if there's space at
			 * the beginning. This will leave a hole at the end
			 * of the buffer which we will have to skip over when
			 * flushing the buffer to disk.
			 */
			if (alq->aq_writetail >= len || flags & ALQ_WAITOK) {
				/* Keep track of # bytes left blank. */
				alq->aq_wrapearly = contigbytes;
				/* Do the wrap and adjust counters. */
				contigbytes = alq->aq_freebytes =
				    alq->aq_writetail;
				alq->aq_writehead = 0;
			}
		}
	}

	/*
	 * Return a NULL ALE if:
	 * - The message is larger than our underlying buffer.
	 * - The ALQ is being shutdown.
	 * - There is insufficient free space in our underlying buffer
	 *   to accept the message and the user can't wait for space.
	 * - There is insufficient free space in our underlying buffer
	 *   to accept the message and the alq is inactive due to prior
	 *   use of the ALQ_NOACTIVATE flag (which would lead to deadlock).
	 */
	if (len > alq->aq_buflen ||
	    alq->aq_flags & AQ_SHUTDOWN ||
	    (((flags & ALQ_NOWAIT) || (!(alq->aq_flags & AQ_ACTIVE) &&
	    HAS_PENDING_DATA(alq))) && contigbytes < len)) {
		ALQ_UNLOCK(alq);
		return (NULL);
	}

	/*
	 * If we want ordered writes and there is already at least one thread
	 * waiting for resources to become available, sleep until we're woken.
	 */
	if (alq->aq_flags & AQ_ORDERED && alq->aq_waiters > 0) {
		KASSERT(!(flags & ALQ_NOWAIT),
		    ("%s: ALQ_NOWAIT set but incorrectly ignored!", __func__));
		alq->aq_waiters++;
		msleep_spin(&alq->aq_waiters, &alq->aq_mtx, "alqgnord", 0);
		alq->aq_waiters--;
	}

	/*
	 * (ALQ_WAITOK && contigbytes < len) or contigbytes >= len, either enter
	 * while loop and sleep until we have enough contiguous free bytes
	 * (former) or skip (latter). If AQ_ORDERED is set, only 1 thread at a
	 * time will be in this loop. Otherwise, multiple threads may be
	 * sleeping here competing for ALQ resources.
	 */
	while (contigbytes < len && !(alq->aq_flags & AQ_SHUTDOWN)) {
		KASSERT(!(flags & ALQ_NOWAIT),
		    ("%s: ALQ_NOWAIT set but incorrectly ignored!", __func__));
		alq->aq_flags |= AQ_WANTED;
		alq->aq_waiters++;
		if (waitchan)
			wakeup(waitchan);
		msleep_spin(alq, &alq->aq_mtx, "alqgnres", 0);
		alq->aq_waiters--;

		if (alq->aq_writehead <= alq->aq_writetail)
			contigbytes = alq->aq_freebytes;
		else
			contigbytes = alq->aq_buflen - alq->aq_writehead;

		/*
		 * If we're the first thread to wake after an AQ_WANTED wakeup
		 * but there isn't enough free space for us, we're going to loop
		 * and sleep again. If there are other threads waiting in this
		 * loop, schedule a wakeup so that they can see if the space
		 * they require is available.
		 */
		if (alq->aq_waiters > 0 && !(alq->aq_flags & AQ_ORDERED) &&
		    contigbytes < len && !(alq->aq_flags & AQ_WANTED))
			waitchan = alq;
		else
			waitchan = NULL;
	}

	/*
	 * If there are waiters, we need to signal the waiting threads after we
	 * complete our work. The alq ptr is used as a wait channel for threads
	 * requiring resources to be freed up. In the AQ_ORDERED case, threads
	 * are not allowed to concurrently compete for resources in the above
	 * while loop, so we use a different wait channel in this case.
	 */
	if (alq->aq_waiters > 0) {
		if (alq->aq_flags & AQ_ORDERED)
			waitchan = &alq->aq_waiters;
		else
			waitchan = alq;
	} else
		waitchan = NULL;

	/* Bail if we're shutting down. */
	if (alq->aq_flags & AQ_SHUTDOWN) {
		ALQ_UNLOCK(alq);
		if (waitchan != NULL)
			wakeup_one(waitchan);
		return (NULL);
	}

	/*
	 * If we are here, we have a contiguous number of bytes >= len
	 * available in our buffer starting at aq_writehead.
	 */
	alq->aq_getpost.ae_data = alq->aq_entbuf + alq->aq_writehead;
	alq->aq_getpost.ae_bytesused = len;

	return (&alq->aq_getpost);
}

struct ale *
alq_get(struct alq *alq, int flags)
{
	/* Should only be called in fixed length message (legacy) mode. */
	KASSERT((alq->aq_flags & AQ_LEGACY),
	    ("%s: fixed length get on variable length queue", __func__));
	return (alq_getn(alq, alq->aq_entlen, flags));
}

void
alq_post_flags(struct alq *alq, struct ale *ale, int flags)
{
	int activate;
	void *waitchan;

	activate = 0;

	if (ale->ae_bytesused > 0) {
		if (!(alq->aq_flags & AQ_ACTIVE) &&
		    !(flags & ALQ_NOACTIVATE)) {
			alq->aq_flags |= AQ_ACTIVE;
			activate = 1;
		}

		alq->aq_writehead += ale->ae_bytesused;
		alq->aq_freebytes -= ale->ae_bytesused;

		/* Wrap aq_writehead if we filled to the end of the buffer. */
		if (alq->aq_writehead == alq->aq_buflen)
			alq->aq_writehead = 0;

		KASSERT((alq->aq_writehead >= 0 &&
		    alq->aq_writehead < alq->aq_buflen),
		    ("%s: aq_writehead < 0 || aq_writehead >= aq_buflen",
		    __func__));

		KASSERT((HAS_PENDING_DATA(alq)), ("%s: queue empty!", __func__));
	}

	/*
	 * If there are waiters, we need to signal the waiting threads after we
	 * complete our work. The alq ptr is used as a wait channel for threads
	 * requiring resources to be freed up. In the AQ_ORDERED case, threads
	 * are not allowed to concurrently compete for resources in the
	 * alq_getn() while loop, so we use a different wait channel in this case.
	 */
	if (alq->aq_waiters > 0) {
		if (alq->aq_flags & AQ_ORDERED)
			waitchan = &alq->aq_waiters;
		else
			waitchan = alq;
	} else
		waitchan = NULL;

	ALQ_UNLOCK(alq);

	if (activate) {
		ALD_LOCK();
		ald_activate(alq);
		ALD_UNLOCK();
	}

	/* NB: We rely on wakeup_one waking threads in a FIFO manner. */
	if (waitchan != NULL)
		wakeup_one(waitchan);
}

void
alq_flush(struct alq *alq)
{
	int needwakeup = 0;

	ALD_LOCK();
	ALQ_LOCK(alq);

	/*
	 * Pull the lever iff there is data to flush and we're
	 * not already in the middle of a flush operation.
	 */
	if (HAS_PENDING_DATA(alq) && !(alq->aq_flags & AQ_FLUSHING)) {
		if (alq->aq_flags & AQ_ACTIVE)
			ald_deactivate(alq);

		ALD_UNLOCK();
		needwakeup = alq_doio(alq);
	} else
		ALD_UNLOCK();

	ALQ_UNLOCK(alq);

	if (needwakeup)
		wakeup_one(alq);
}

/*
 * Flush remaining data, close the file and free all resources.
 */
void
alq_close(struct alq *alq)
{
	/* Only flush and destroy alq if not already shutting down. */
	if (ald_rem(alq) == 0)
		alq_destroy(alq);
}

static int
alq_load_handler(module_t mod, int what, void *arg)
{
	int ret;
	
	ret = 0;

	switch (what) {
	case MOD_LOAD:
	case MOD_SHUTDOWN:
		break;

	case MOD_QUIESCE:
		ALD_LOCK();
		/* Only allow unload if there are no open queues. */
		if (LIST_FIRST(&ald_queues) == NULL) {
			ald_shutingdown = 1;
			ALD_UNLOCK();
			EVENTHANDLER_DEREGISTER(shutdown_pre_sync,
			    alq_eventhandler_tag);
			ald_shutdown(NULL, 0);
			mtx_destroy(&ald_mtx);
		} else {
			ALD_UNLOCK();
			ret = EBUSY;
		}
		break;

	case MOD_UNLOAD:
		/* If MOD_QUIESCE failed we must fail here too. */
		if (ald_shutingdown == 0)
			ret = EBUSY;
		break;

	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}

static moduledata_t alq_mod =
{
	"alq",
	alq_load_handler,
	NULL
};

DECLARE_MODULE(alq, alq_mod, SI_SUB_LAST, SI_ORDER_ANY);
MODULE_VERSION(alq, 1);
