/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016-2017 Netflix, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/mutex.h>
#include <sys/selinfo.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <machine/atomic.h>
#include <sys/counter.h>

#include <dev/tcp_log/tcp_log_dev.h>

#ifdef TCPLOG_DEBUG_COUNTERS
extern counter_u64_t tcp_log_que_read;
extern counter_u64_t tcp_log_que_freed;
#endif

static struct cdev *tcp_log_dev;
static struct selinfo tcp_log_sel;

static struct log_queueh tcp_log_dev_queue_head = STAILQ_HEAD_INITIALIZER(tcp_log_dev_queue_head);
static struct log_infoh tcp_log_dev_reader_head = STAILQ_HEAD_INITIALIZER(tcp_log_dev_reader_head);

MALLOC_DEFINE(M_TCPLOGDEV, "tcp_log_dev", "TCP log device data structures");

static int	tcp_log_dev_listeners = 0;

static struct mtx tcp_log_dev_queue_lock;

#define	TCP_LOG_DEV_QUEUE_LOCK()	mtx_lock(&tcp_log_dev_queue_lock)
#define	TCP_LOG_DEV_QUEUE_UNLOCK()	mtx_unlock(&tcp_log_dev_queue_lock)
#define	TCP_LOG_DEV_QUEUE_LOCK_ASSERT()	mtx_assert(&tcp_log_dev_queue_lock, MA_OWNED)
#define	TCP_LOG_DEV_QUEUE_UNLOCK_ASSERT() mtx_assert(&tcp_log_dev_queue_lock, MA_NOTOWNED)
#define	TCP_LOG_DEV_QUEUE_REF(tldq)	refcount_acquire(&((tldq)->tldq_refcnt))
#define	TCP_LOG_DEV_QUEUE_UNREF(tldq)	refcount_release(&((tldq)->tldq_refcnt))

static void	tcp_log_dev_clear_refcount(struct tcp_log_dev_queue *entry);
static void	tcp_log_dev_clear_cdevpriv(void *data);
static int	tcp_log_dev_open(struct cdev *dev __unused, int flags,
    int devtype __unused, struct thread *td __unused);
static int	tcp_log_dev_write(struct cdev *dev __unused,
    struct uio *uio __unused, int flags __unused);
static int	tcp_log_dev_read(struct cdev *dev __unused, struct uio *uio,
    int flags __unused);
static int	tcp_log_dev_ioctl(struct cdev *dev __unused, u_long cmd,
    caddr_t data, int fflag __unused, struct thread *td __unused);
static int	tcp_log_dev_poll(struct cdev *dev __unused, int events,
    struct thread *td);


enum tcp_log_dev_queue_lock_state {
	QUEUE_UNLOCKED = 0,
	QUEUE_LOCKED,
};

static struct cdevsw tcp_log_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	tcp_log_dev_read,
	.d_open =	tcp_log_dev_open,
	.d_write =	tcp_log_dev_write,
	.d_poll =	tcp_log_dev_poll,
	.d_ioctl =	tcp_log_dev_ioctl,
#ifdef NOTYET
	.d_mmap =	tcp_log_dev_mmap,
#endif
	.d_name =	"tcp_log",
};

static __inline void
tcp_log_dev_queue_validate_lock(int lockstate)
{

#ifdef INVARIANTS
	switch (lockstate) {
	case QUEUE_LOCKED:
		TCP_LOG_DEV_QUEUE_LOCK_ASSERT();
		break;
	case QUEUE_UNLOCKED:
		TCP_LOG_DEV_QUEUE_UNLOCK_ASSERT();
		break;
	default:
		kassert_panic("%s:%d: unknown queue lock state", __func__,
		    __LINE__);
	}
#endif
}

/*
 * Clear the refcount. If appropriate, it will remove the entry from the
 * queue and call the destructor.
 *
 * This must be called with the queue lock held.
 */
static void
tcp_log_dev_clear_refcount(struct tcp_log_dev_queue *entry)
{

	KASSERT(entry != NULL, ("%s: called with NULL entry", __func__));

	TCP_LOG_DEV_QUEUE_LOCK_ASSERT();

	if (TCP_LOG_DEV_QUEUE_UNREF(entry)) {
#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_freed, 1);
#endif
		/* Remove the entry from the queue and call the destructor. */
		STAILQ_REMOVE(&tcp_log_dev_queue_head, entry, tcp_log_dev_queue,
		    tldq_queue);
		(*entry->tldq_dtor)(entry);
	}
}

static void
tcp_log_dev_clear_cdevpriv(void *data)
{
	struct tcp_log_dev_info *priv;
	struct tcp_log_dev_queue *entry, *entry_tmp;

	priv = (struct tcp_log_dev_info *)data;
	if (priv == NULL)
		return;

	/*
	 * Lock the queue and drop our references. We hold references to all
	 * the entries starting with tldi_head (or, if tldi_head == NULL, all
	 * entries in the queue).
	 * 
	 * Because we don't want anyone adding addition things to the queue
	 * while we are doing this, we lock the queue.
	 */
	TCP_LOG_DEV_QUEUE_LOCK();
	if (priv->tldi_head != NULL) {
		entry = priv->tldi_head;
		STAILQ_FOREACH_FROM_SAFE(entry, &tcp_log_dev_queue_head,
		    tldq_queue, entry_tmp) {
			tcp_log_dev_clear_refcount(entry);
		}
	}
	tcp_log_dev_listeners--;
	KASSERT(tcp_log_dev_listeners >= 0,
	    ("%s: tcp_log_dev_listeners is unexpectedly negative", __func__));
	STAILQ_REMOVE(&tcp_log_dev_reader_head, priv, tcp_log_dev_info,
	    tldi_list);
	TCP_LOG_DEV_QUEUE_LOCK_ASSERT();
	TCP_LOG_DEV_QUEUE_UNLOCK();
	free(priv, M_TCPLOGDEV);
}

static int
tcp_log_dev_open(struct cdev *dev __unused, int flags, int devtype __unused,
    struct thread *td __unused)
{
	struct tcp_log_dev_info *priv;
	struct tcp_log_dev_queue *entry;
	int rv;

	/*
	 * Ideally, we shouldn't see these because of file system
	 * permissions.
	 */
	if (flags & (FWRITE | FEXEC | FAPPEND | O_TRUNC))
		return (ENODEV);

	/* Allocate space to hold information about where we are. */
	priv = malloc(sizeof(struct tcp_log_dev_info), M_TCPLOGDEV,
	    M_ZERO | M_WAITOK);

	/* Stash the private data away. */
	rv = devfs_set_cdevpriv((void *)priv, tcp_log_dev_clear_cdevpriv);
	if (!rv) {
		/*
		 * Increase the listener count, add this reader to the list, and
		 * take references on all current queues.
		 */
		TCP_LOG_DEV_QUEUE_LOCK();
		tcp_log_dev_listeners++;
		STAILQ_INSERT_HEAD(&tcp_log_dev_reader_head, priv, tldi_list);
		priv->tldi_head = STAILQ_FIRST(&tcp_log_dev_queue_head);
		if (priv->tldi_head != NULL)
			priv->tldi_cur = priv->tldi_head->tldq_buf;
		STAILQ_FOREACH(entry, &tcp_log_dev_queue_head, tldq_queue)
			TCP_LOG_DEV_QUEUE_REF(entry);
		TCP_LOG_DEV_QUEUE_UNLOCK();
	} else {
		/* Free the entry. */
		free(priv, M_TCPLOGDEV);
	}
	return (rv);
}

static int
tcp_log_dev_write(struct cdev *dev __unused, struct uio *uio __unused,
    int flags __unused)
{

	return (ENODEV);
}

static __inline void
tcp_log_dev_rotate_bufs(struct tcp_log_dev_info *priv, int *lockstate)
{
	struct tcp_log_dev_queue *entry;

	KASSERT(priv->tldi_head != NULL,
	    ("%s:%d: priv->tldi_head unexpectedly NULL",
	    __func__, __LINE__));
	KASSERT(priv->tldi_head->tldq_buf == priv->tldi_cur,
	    ("%s:%d: buffer mismatch (%p vs %p)",
	    __func__, __LINE__, priv->tldi_head->tldq_buf,
	    priv->tldi_cur));
	tcp_log_dev_queue_validate_lock(*lockstate);

	if (*lockstate == QUEUE_UNLOCKED) {
		TCP_LOG_DEV_QUEUE_LOCK();
		*lockstate = QUEUE_LOCKED;
	}
	entry = priv->tldi_head;
	priv->tldi_head = STAILQ_NEXT(entry, tldq_queue);
	tcp_log_dev_clear_refcount(entry);
	priv->tldi_cur = NULL;
}

static int
tcp_log_dev_read(struct cdev *dev __unused, struct uio *uio, int flags)
{
	struct tcp_log_common_header *buf;
	struct tcp_log_dev_info *priv;
	struct tcp_log_dev_queue *entry;
	ssize_t len;
	int lockstate, rv;

	/* Get our private info. */
	rv = devfs_get_cdevpriv((void **)&priv);
	if (rv)
		return (rv);

	lockstate = QUEUE_UNLOCKED;

	/* Do we need to get a new buffer? */
	while (priv->tldi_cur == NULL ||
	    priv->tldi_cur->tlch_length <= priv->tldi_off) {
		/* Did we somehow forget to rotate? */
		KASSERT(priv->tldi_cur == NULL,
		    ("%s:%d: tldi_cur is unexpectedly non-NULL", __func__,
		    __LINE__));
		if (priv->tldi_cur != NULL)
			tcp_log_dev_rotate_bufs(priv, &lockstate);

		/*
		 * Before we start looking at tldi_head, we need a lock on the
		 * queue to make sure tldi_head stays stable.
		 */
		if (lockstate == QUEUE_UNLOCKED) {
			TCP_LOG_DEV_QUEUE_LOCK();
			lockstate = QUEUE_LOCKED;
		}

		/* We need the next buffer. Do we have one? */
		if (priv->tldi_head == NULL && (flags & FNONBLOCK)) {
			rv = EAGAIN;
			goto done;
		}
		if (priv->tldi_head == NULL) {
			/* Sleep and wait for more things we can read. */
			rv = mtx_sleep(&tcp_log_dev_listeners,
			    &tcp_log_dev_queue_lock, PCATCH, "tcplogdev", 0);
			if (rv)
				goto done;
			if (priv->tldi_head == NULL)
				continue;
		}

		/*
		 * We have an entry to read. We want to try to create a
		 * buffer, if one doesn't already exist.
		 */
		entry = priv->tldi_head;
		if (entry->tldq_buf == NULL) {
			TCP_LOG_DEV_QUEUE_LOCK_ASSERT();
			buf = (*entry->tldq_xform)(entry);
			if (buf == NULL) {
				rv = EBUSY;
				goto done;
			}
			entry->tldq_buf = buf;
		}

		priv->tldi_cur = entry->tldq_buf;
		priv->tldi_off = 0;
	}

	/* Copy what we can from this buffer to the output buffer. */
	if (uio->uio_resid > 0) {
		/* Drop locks so we can take page faults. */
		if (lockstate == QUEUE_LOCKED)
			TCP_LOG_DEV_QUEUE_UNLOCK();
		lockstate = QUEUE_UNLOCKED;

		KASSERT(priv->tldi_cur != NULL,
		    ("%s: priv->tldi_cur is unexpectedly NULL", __func__));

		/* Copy as much as we can to this uio. */
		len = priv->tldi_cur->tlch_length - priv->tldi_off;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		rv = uiomove(((uint8_t *)priv->tldi_cur) + priv->tldi_off,
		    len, uio);
		if (rv != 0)
			goto done;
		priv->tldi_off += len;
#ifdef TCPLOG_DEBUG_COUNTERS
		counter_u64_add(tcp_log_que_read, len);
#endif
	}
	/* Are we done with this buffer? If so, find the next one. */
	if (priv->tldi_off >= priv->tldi_cur->tlch_length) {
		KASSERT(priv->tldi_off == priv->tldi_cur->tlch_length,
		    ("%s: offset (%ju) exceeds length (%ju)", __func__,
		    (uintmax_t)priv->tldi_off,
		    (uintmax_t)priv->tldi_cur->tlch_length));
		tcp_log_dev_rotate_bufs(priv, &lockstate);
	}
done:
	tcp_log_dev_queue_validate_lock(lockstate);
	if (lockstate == QUEUE_LOCKED)
		TCP_LOG_DEV_QUEUE_UNLOCK();
	return (rv);
}

static int
tcp_log_dev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td __unused)
{
	struct tcp_log_dev_info *priv;
	int rv;

	/* Get our private info. */
	rv = devfs_get_cdevpriv((void **)&priv);
	if (rv)
		return (rv);

	/*
	 * Set things. Here, we are most concerned about the non-blocking I/O
	 * flag.
	 */
	rv = 0;
	switch (cmd) {
	case FIONBIO:
		break;
	case FIOASYNC:
		if (*(int *)data != 0)
			rv = EINVAL;
		break;
	default:
		rv = ENOIOCTL;
	}
	return (rv);
}

static int
tcp_log_dev_poll(struct cdev *dev __unused, int events, struct thread *td)
{
	struct tcp_log_dev_info *priv;
	int revents;

	/*
	 * Get our private info. If this fails, claim that all events are
	 * ready. That should prod the user to do something that will
	 * make the error evident to them.
	 */
	if (devfs_get_cdevpriv((void **)&priv))
		return (events);

	revents = 0;
	if (events & (POLLIN | POLLRDNORM)) {
		/*
		 * We can (probably) read right now if we are partway through
		 * a buffer or if we are just about to start a buffer.
		 * Because we are going to read tldi_head, we should acquire
		 * a read lock on the queue.
		 */
		TCP_LOG_DEV_QUEUE_LOCK();
		if ((priv->tldi_head != NULL && priv->tldi_cur == NULL) ||
		    (priv->tldi_cur != NULL &&
		    priv->tldi_off < priv->tldi_cur->tlch_length))
			revents = events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &tcp_log_sel);
		TCP_LOG_DEV_QUEUE_UNLOCK();
	} else {
		/*
		 * It only makes sense to poll for reading. So, again, prod the
		 * user to do something that will make the error of their ways
		 * apparent.
		 */
		revents = events;
	}
	return (revents);
}

int
tcp_log_dev_add_log(struct tcp_log_dev_queue *entry)
{
	struct tcp_log_dev_info *priv;
	int rv;
	bool wakeup_needed;

	KASSERT(entry->tldq_buf != NULL || entry->tldq_xform != NULL,
	    ("%s: Called with both tldq_buf and tldq_xform set to NULL",
	    __func__));
	KASSERT(entry->tldq_dtor != NULL,
	    ("%s: Called with tldq_dtor set to NULL", __func__));

	/* Get a lock on the queue. */
	TCP_LOG_DEV_QUEUE_LOCK();

	/* If no one is listening, tell the caller to free the resources. */
	if (tcp_log_dev_listeners == 0) {
		rv = ENXIO;
		goto done;
	}

	/* Add this to the end of the tailq. */
	STAILQ_INSERT_TAIL(&tcp_log_dev_queue_head, entry, tldq_queue);

	/* Add references for all current listeners. */
	refcount_init(&entry->tldq_refcnt, tcp_log_dev_listeners);

	/*
	 * If any listener is currently stuck on NULL, that means they are
	 * waiting. Point their head to this new entry.
	 */
	wakeup_needed = false;
	STAILQ_FOREACH(priv, &tcp_log_dev_reader_head, tldi_list)
		if (priv->tldi_head == NULL) {
			priv->tldi_head = entry;
			wakeup_needed = true;
		}

	if (wakeup_needed) {
		selwakeup(&tcp_log_sel);
		wakeup(&tcp_log_dev_listeners);
	}

	rv = 0;

done:
	TCP_LOG_DEV_QUEUE_LOCK_ASSERT();
	TCP_LOG_DEV_QUEUE_UNLOCK();
	return (rv);
}

static int
tcp_log_dev_modevent(module_t mod __unused, int type, void *data __unused)
{

	/* TODO: Support intelligent unloading. */
	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("tcp_log: tcp_log device\n");
		memset(&tcp_log_sel, 0, sizeof(tcp_log_sel));
		memset(&tcp_log_dev_queue_lock, 0, sizeof(struct mtx));
		mtx_init(&tcp_log_dev_queue_lock, "tcp_log dev",
			 "tcp_log device queues", MTX_DEF);
		tcp_log_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD,
		    &tcp_log_cdevsw, 0, NULL, UID_ROOT, GID_WHEEL, 0400,
		    "tcp_log");
		break;
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(tcp_log_dev, tcp_log_dev_modevent, NULL);
MODULE_VERSION(tcp_log_dev, 1);
