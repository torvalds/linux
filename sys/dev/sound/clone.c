/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Ariff Abdullah <ariff@FreeBSD.org>
 * All rights reserved.
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#if defined(SND_DIAGNOSTIC) || defined(SND_DEBUG)
#include <dev/sound/pcm/sound.h>
#endif

#include <dev/sound/clone.h>

/*
 * So here we go again, another clonedevs manager. Unlike default clonedevs,
 * this clone manager is designed to withstand various abusive behavior
 * (such as 'while : ; do ls /dev/whatever ; done', etc.), reusable object
 * after reaching certain expiration threshold, aggressive garbage collector,
 * transparent device allocator and concurrency handling across multiple
 * thread/proc. Due to limited information given by dev_clone EVENTHANDLER,
 * we don't have much clues whether the caller wants a real open() or simply
 * making fun of us with things like stat(), mtime() etc. Assuming that:
 * 1) Time window between dev_clone EH <-> real open() should be small
 * enough and 2) mtime()/stat() etc. always looks like a half way / stalled
 * operation, we can decide whether a new cdev must be created, old
 * (expired) cdev can be reused or an existing cdev can be shared.
 *
 * Most of the operations and logics are generic enough and can be applied
 * on other places (such as if_tap, snp, etc).  Perhaps this can be
 * rearranged to complement clone_*(). However, due to this still being
 * specific to the sound driver (and as a proof of concept on how it can be
 * done), si_drv2 is used to keep the pointer of the clone list entry to
 * avoid expensive lookup.
 */

/* clone entry */
struct snd_clone_entry {
	TAILQ_ENTRY(snd_clone_entry) link;
	struct snd_clone *parent;
	struct cdev *devt;
	struct timespec tsp;
	uint32_t flags;
	pid_t pid;
	int unit;
};

/* clone manager */
struct snd_clone {
	TAILQ_HEAD(link_head, snd_clone_entry) head;
	struct timespec tsp;
	int refcount;
	int size;
	int typemask;
	int maxunit;
	int deadline;
	uint32_t flags;
};

#ifdef SND_DIAGNOSTIC
#define SND_CLONE_ASSERT(x, y)		do {			\
	if (!(x))						\
		panic y;					\
} while (0)
#else
#define SND_CLONE_ASSERT(...)		KASSERT(__VA_ARGS__)
#endif

/*
 * Shamelessly ripped off from vfs_subr.c
 * We need at least 1/HZ precision as default timestamping.
 */
enum { SND_TSP_SEC, SND_TSP_HZ, SND_TSP_USEC, SND_TSP_NSEC };

static int snd_timestamp_precision = SND_TSP_HZ;
TUNABLE_INT("hw.snd.timestamp_precision", &snd_timestamp_precision);

void
snd_timestamp(struct timespec *tsp)
{
	struct timeval tv;

	switch (snd_timestamp_precision) {
	case SND_TSP_SEC:
		tsp->tv_sec = time_second;
		tsp->tv_nsec = 0;
		break;
	case SND_TSP_HZ:
		getnanouptime(tsp);
		break;
	case SND_TSP_USEC:
		microuptime(&tv);
		TIMEVAL_TO_TIMESPEC(&tv, tsp);
		break;
	case SND_TSP_NSEC:
		nanouptime(tsp);
		break;
	default:
		snd_timestamp_precision = SND_TSP_HZ;
		getnanouptime(tsp);
		break;
	}
}

#if defined(SND_DIAGNOSTIC) || defined(SND_DEBUG)
static int
sysctl_hw_snd_timestamp_precision(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = snd_timestamp_precision;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err == 0 && req->newptr != NULL) {
		switch (val) {
		case SND_TSP_SEC:
		case SND_TSP_HZ:
		case SND_TSP_USEC:
		case SND_TSP_NSEC:
			snd_timestamp_precision = val;
			break;
		default:
			break;
		}
	}

	return (err);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, timestamp_precision, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(int), sysctl_hw_snd_timestamp_precision, "I",
    "timestamp precision (0=s 1=hz 2=us 3=ns)");
#endif

/*
 * snd_clone_create() : Return opaque allocated clone manager.
 */
struct snd_clone *
snd_clone_create(int typemask, int maxunit, int deadline, uint32_t flags)
{
	struct snd_clone *c;

	SND_CLONE_ASSERT(!(typemask & ~SND_CLONE_MAXUNIT),
	    ("invalid typemask: 0x%08x", typemask));
	SND_CLONE_ASSERT(maxunit == -1 ||
	    !(maxunit & ~(~typemask & SND_CLONE_MAXUNIT)),
	    ("maxunit overflow: typemask=0x%08x maxunit=%d",
	    typemask, maxunit));
	SND_CLONE_ASSERT(!(flags & ~SND_CLONE_MASK),
	    ("invalid clone flags=0x%08x", flags));

	c = malloc(sizeof(*c), M_DEVBUF, M_WAITOK | M_ZERO);
	c->refcount = 0;
	c->size = 0;
	c->typemask = typemask;
	c->maxunit = (maxunit == -1) ? (~typemask & SND_CLONE_MAXUNIT) :
	    maxunit;
	c->deadline = deadline;
	c->flags = flags;
	snd_timestamp(&c->tsp);
	TAILQ_INIT(&c->head);

	return (c);
}

int
snd_clone_busy(struct snd_clone *c)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	if (c->size == 0)
		return (0);

	TAILQ_FOREACH(ce, &c->head, link) {
		if ((ce->flags & SND_CLONE_BUSY) ||
		    (ce->devt != NULL && ce->devt->si_threadcount != 0))
			return (EBUSY);
	}

	return (0);
}

/*
 * snd_clone_enable()/disable() : Suspend/resume clone allocation through
 * snd_clone_alloc(). Everything else will not be affected by this.
 */
int
snd_clone_enable(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	if (c->flags & SND_CLONE_ENABLE)
		return (EINVAL);

	c->flags |= SND_CLONE_ENABLE;

	return (0);
}

int
snd_clone_disable(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	if (!(c->flags & SND_CLONE_ENABLE))
		return (EINVAL);

	c->flags &= ~SND_CLONE_ENABLE;

	return (0);
}

/*
 * Getters / Setters. Not worth explaining :)
 */
int
snd_clone_getsize(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	return (c->size);
}

int
snd_clone_getmaxunit(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	return (c->maxunit);
}

int
snd_clone_setmaxunit(struct snd_clone *c, int maxunit)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));
	SND_CLONE_ASSERT(maxunit == -1 ||
	    !(maxunit & ~(~c->typemask & SND_CLONE_MAXUNIT)),
	    ("maxunit overflow: typemask=0x%08x maxunit=%d",
	    c->typemask, maxunit));

	c->maxunit = (maxunit == -1) ? (~c->typemask & SND_CLONE_MAXUNIT) :
	    maxunit;

	return (c->maxunit);
}

int
snd_clone_getdeadline(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	return (c->deadline);
}

int
snd_clone_setdeadline(struct snd_clone *c, int deadline)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	c->deadline = deadline;

	return (c->deadline);
}

int
snd_clone_gettime(struct snd_clone *c, struct timespec *tsp)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));
	SND_CLONE_ASSERT(tsp != NULL, ("NULL timespec"));

	*tsp = c->tsp;

	return (0);
}

uint32_t
snd_clone_getflags(struct snd_clone *c)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	return (c->flags);
}

uint32_t
snd_clone_setflags(struct snd_clone *c, uint32_t flags)
{
	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));
	SND_CLONE_ASSERT(!(flags & ~SND_CLONE_MASK),
	    ("invalid clone flags=0x%08x", flags));

	c->flags = flags;

	return (c->flags);
}

int
snd_clone_getdevtime(struct cdev *dev, struct timespec *tsp)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));
	SND_CLONE_ASSERT(tsp != NULL, ("NULL timespec"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (ENODEV);

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	*tsp = ce->tsp;

	return (0);
}

uint32_t
snd_clone_getdevflags(struct cdev *dev)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (0xffffffff);

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	return (ce->flags);
}

uint32_t
snd_clone_setdevflags(struct cdev *dev, uint32_t flags)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));
	SND_CLONE_ASSERT(!(flags & ~SND_CLONE_DEVMASK),
	    ("invalid clone dev flags=0x%08x", flags));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (0xffffffff);

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	ce->flags = flags;

	return (ce->flags);
}

/* Elapsed time conversion to ms */
#define SND_CLONE_ELAPSED(x, y)						\
	((((x)->tv_sec - (y)->tv_sec) * 1000) +				\
	(((y)->tv_nsec > (x)->tv_nsec) ?				\
	(((1000000000L + (x)->tv_nsec -					\
	(y)->tv_nsec) / 1000000) - 1000) :				\
	(((x)->tv_nsec - (y)->tv_nsec) / 1000000)))

#define SND_CLONE_EXPIRED(x, y, z)					\
	((x)->deadline < 1 ||						\
	((y)->tv_sec - (z)->tv_sec) > ((x)->deadline / 1000) ||		\
	SND_CLONE_ELAPSED(y, z) > (x)->deadline)

/*
 * snd_clone_gc() : Garbage collector for stalled, expired objects. Refer to
 * clone.h for explanations on GC settings.
 */
int
snd_clone_gc(struct snd_clone *c)
{
	struct snd_clone_entry *ce, *tce;
	struct timespec now;
	int pruned;

	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	if (!(c->flags & SND_CLONE_GC_ENABLE) || c->size == 0)
		return (0);

	snd_timestamp(&now);

	/*
	 * Bail out if the last clone handler was invoked below the deadline
	 * threshold.
	 */
	if ((c->flags & SND_CLONE_GC_EXPIRED) &&
	    !SND_CLONE_EXPIRED(c, &now, &c->tsp))
		return (0);

	pruned = 0;

	/*
	 * Visit each object in reverse order. If the object is still being
	 * referenced by a valid open(), skip it. Look for expired objects
	 * and either revoke its clone invocation status or mercilessly
	 * throw it away.
	 */
	TAILQ_FOREACH_REVERSE_SAFE(ce, &c->head, link_head, link, tce) {
		if (!(ce->flags & SND_CLONE_BUSY) &&
		    (!(ce->flags & SND_CLONE_INVOKE) ||
		    SND_CLONE_EXPIRED(c, &now, &ce->tsp))) {
			if ((c->flags & SND_CLONE_GC_REVOKE) ||
			    ce->devt->si_threadcount != 0) {
				ce->flags &= ~SND_CLONE_INVOKE;
				ce->pid = -1;
			} else {
				TAILQ_REMOVE(&c->head, ce, link);
				destroy_dev(ce->devt);
				free(ce, M_DEVBUF);
				c->size--;
			}
			pruned++;
		}
	}

	/* return total pruned objects */
	return (pruned);
}

void
snd_clone_destroy(struct snd_clone *c)
{
	struct snd_clone_entry *ce, *tmp;

	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));

	ce = TAILQ_FIRST(&c->head);
	while (ce != NULL) {
		tmp = TAILQ_NEXT(ce, link);
		if (ce->devt != NULL)
			destroy_dev(ce->devt);
		free(ce, M_DEVBUF);
		ce = tmp;
	}

	free(c, M_DEVBUF);
}

/*
 * snd_clone_acquire() : The vital part of concurrency management. Must be
 * called somewhere at the beginning of open() handler. ENODEV is not really
 * fatal since it just tell the caller that this is not cloned stuff.
 * EBUSY is *real*, don't forget that!
 */
int
snd_clone_acquire(struct cdev *dev)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (ENODEV);

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	ce->flags &= ~SND_CLONE_INVOKE;

	if (ce->flags & SND_CLONE_BUSY)
		return (EBUSY);

	ce->flags |= SND_CLONE_BUSY;

	return (0);
}

/*
 * snd_clone_release() : Release busy status. Must be called somewhere at
 * the end of close() handler, or somewhere after fail open().
 */
int
snd_clone_release(struct cdev *dev)
{
	struct snd_clone_entry *ce;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (ENODEV);

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	ce->flags &= ~SND_CLONE_INVOKE;

	if (!(ce->flags & SND_CLONE_BUSY))
		return (EBADF);

	ce->flags &= ~SND_CLONE_BUSY;
	ce->pid = -1;

	return (0);
}

/*
 * snd_clone_ref/unref() : Garbage collector reference counter. To make
 * garbage collector run automatically, the sequence must be something like
 * this (both in open() and close() handlers):
 *
 *  open() - 1) snd_clone_acquire()
 *           2) .... check check ... if failed, snd_clone_release()
 *           3) Success. Call snd_clone_ref()
 *
 * close() - 1) .... check check check ....
 *           2) Success. snd_clone_release()
 *           3) snd_clone_unref() . Garbage collector will run at this point
 *              if this is the last referenced object.
 */
int
snd_clone_ref(struct cdev *dev)
{
	struct snd_clone_entry *ce;
	struct snd_clone *c;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (0);

	c = ce->parent;
	SND_CLONE_ASSERT(c != NULL, ("NULL parent"));
	SND_CLONE_ASSERT(c->refcount >= 0, ("refcount < 0"));

	return (++c->refcount);
}

int
snd_clone_unref(struct cdev *dev)
{
	struct snd_clone_entry *ce;
	struct snd_clone *c;

	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));

	ce = dev->si_drv2;
	if (ce == NULL)
		return (0);

	c = ce->parent;
	SND_CLONE_ASSERT(c != NULL, ("NULL parent"));
	SND_CLONE_ASSERT(c->refcount > 0, ("refcount <= 0"));

	c->refcount--;

	/* 
	 * Run automatic garbage collector, if needed.
	 */
	if ((c->flags & SND_CLONE_GC_UNREF) &&
	    (!(c->flags & SND_CLONE_GC_LASTREF) ||
	    (c->refcount == 0 && (c->flags & SND_CLONE_GC_LASTREF))))
		(void)snd_clone_gc(c);

	return (c->refcount);
}

void
snd_clone_register(struct snd_clone_entry *ce, struct cdev *dev)
{
	SND_CLONE_ASSERT(ce != NULL, ("NULL snd_clone_entry"));
	SND_CLONE_ASSERT(dev != NULL, ("NULL dev"));
	SND_CLONE_ASSERT(dev->si_drv2 == NULL, ("dev->si_drv2 not NULL"));
	SND_CLONE_ASSERT((ce->flags & SND_CLONE_ALLOC) == SND_CLONE_ALLOC,
	    ("invalid clone alloc flags=0x%08x", ce->flags));
	SND_CLONE_ASSERT(ce->devt == NULL, ("ce->devt not NULL"));
	SND_CLONE_ASSERT(ce->unit == dev2unit(dev),
	    ("invalid unit ce->unit=0x%08x dev2unit=0x%08x",
	    ce->unit, dev2unit(dev)));

	SND_CLONE_ASSERT(ce->parent != NULL, ("NULL parent"));

	dev->si_drv2 = ce;
	ce->devt = dev;
	ce->flags &= ~SND_CLONE_ALLOC;
	ce->flags |= SND_CLONE_INVOKE;
}

struct snd_clone_entry *
snd_clone_alloc(struct snd_clone *c, struct cdev **dev, int *unit, int tmask)
{
	struct snd_clone_entry *ce, *after, *bce, *cce, *nce, *tce;
	struct timespec now;
	int cunit, allocunit;
	pid_t curpid;

	SND_CLONE_ASSERT(c != NULL, ("NULL snd_clone"));
	SND_CLONE_ASSERT(dev != NULL, ("NULL dev pointer"));
	SND_CLONE_ASSERT((c->typemask & tmask) == tmask,
	    ("invalid tmask: typemask=0x%08x tmask=0x%08x",
	    c->typemask, tmask));
	SND_CLONE_ASSERT(unit != NULL, ("NULL unit pointer"));
	SND_CLONE_ASSERT(*unit == -1 || !(*unit & (c->typemask | tmask)),
	    ("typemask collision: typemask=0x%08x tmask=0x%08x *unit=%d",
	    c->typemask, tmask, *unit));

	if (!(c->flags & SND_CLONE_ENABLE) ||
	    (*unit != -1 && *unit > c->maxunit))
		return (NULL);

	ce = NULL;
	after = NULL;
	bce = NULL;	/* "b"usy candidate */
	cce = NULL;	/* "c"urthread/proc candidate */
	nce = NULL;	/* "n"ull, totally unbusy candidate */
	tce = NULL;	/* Last "t"ry candidate */
	cunit = 0;
	allocunit = (*unit == -1) ? 0 : *unit;
	curpid = curthread->td_proc->p_pid;

	snd_timestamp(&now);

	TAILQ_FOREACH(ce, &c->head, link) {
		/*
		 * Sort incrementally according to device type.
		 */
		if (tmask > (ce->unit & c->typemask)) {
			if (cunit == 0)
				after = ce;
			continue;
		} else if (tmask < (ce->unit & c->typemask))
			break;

		/*
		 * Shoot.. this is where the grumpiness begin. Just
		 * return immediately.
		 */
		if (*unit != -1 && *unit == (ce->unit & ~tmask))
			goto snd_clone_alloc_out;

		cunit++;
		/*
		 * Simmilar device type. Sort incrementally according
		 * to allocation unit. While here, look for free slot
		 * and possible collision for new / future allocation.
		 */
		if (*unit == -1 && (ce->unit & ~tmask) == allocunit)
			allocunit++;
		if ((ce->unit & ~tmask) < allocunit)
			after = ce;
		/*
		 * Clone logic:
		 *   1. Look for non busy, but keep track of the best
		 *      possible busy cdev.
		 *   2. Look for the best (oldest referenced) entry that is
		 *      in a same process / thread.
		 *   3. Look for the best (oldest referenced), absolute free
		 *      entry.
		 *   4. Lastly, look for the best (oldest referenced)
		 *      any entries that doesn't fit with anything above.
		 */
		if (ce->flags & SND_CLONE_BUSY) {
			if (ce->devt != NULL && (bce == NULL ||
			    timespeccmp(&ce->tsp, &bce->tsp, <)))
				bce = ce;
			continue;
		}
		if (ce->pid == curpid &&
		    (cce == NULL || timespeccmp(&ce->tsp, &cce->tsp, <)))
			cce = ce;
		else if (!(ce->flags & SND_CLONE_INVOKE) &&
		    (nce == NULL || timespeccmp(&ce->tsp, &nce->tsp, <)))
			nce = ce;
		else if (tce == NULL || timespeccmp(&ce->tsp, &tce->tsp, <))
			tce = ce;
	}
	if (*unit != -1)
		goto snd_clone_alloc_new;
	else if (cce != NULL) {
		/* Same proc entry found, go for it */
		ce = cce;
		goto snd_clone_alloc_out;
	} else if (nce != NULL) {
		/*
		 * Next, try absolute free entry. If the calculated
		 * allocunit is smaller, create new entry instead.
		 */
		if (allocunit < (nce->unit & ~tmask))
			goto snd_clone_alloc_new;
		ce = nce;
		goto snd_clone_alloc_out;
	} else if (allocunit > c->maxunit) {
		/*
		 * Maximum allowable unit reached. Try returning any
		 * available cdev and hope for the best. If the lookup is
		 * done for things like stat(), mtime() etc. , things should
		 * be ok. Otherwise, open() handler should do further checks
		 * and decide whether to return correct error code or not.
		 */
		if (tce != NULL) {
			ce = tce;
			goto snd_clone_alloc_out;
		} else if (bce != NULL) {
			ce = bce;
			goto snd_clone_alloc_out;
		}
		return (NULL);
	}

snd_clone_alloc_new:
	/*
	 * No free entries found, and we still haven't reached maximum
	 * allowable units. Allocate, setup a minimal unique entry with busy
	 * status so nobody will monkey on this new entry. Unit magic is set
	 * right here to avoid collision with other contesting handler.
	 * The caller must be carefull here to maintain its own
	 * synchronization, as long as it will not conflict with malloc(9)
	 * operations.
	 *
	 * That said, go figure.
	 */
	ce = malloc(sizeof(*ce), M_DEVBUF,
	    ((c->flags & SND_CLONE_WAITOK) ? M_WAITOK : M_NOWAIT) | M_ZERO);
	if (ce == NULL) {
		if (*unit != -1)
			return (NULL);
		/*
		 * We're being dense, ignorance is bliss,
		 * Super Regulatory Measure (TM).. TRY AGAIN!
		 */
		if (nce != NULL) {
			ce = nce;
			goto snd_clone_alloc_out;
		} else if (tce != NULL) {
			ce = tce;
			goto snd_clone_alloc_out;
		} else if (bce != NULL) {
			ce = bce;
			goto snd_clone_alloc_out;
		}
		return (NULL);
	}
	/* Setup new entry */
	ce->parent = c;
	ce->unit = tmask | allocunit;
	ce->pid = curpid;
	ce->tsp = now;
	ce->flags |= SND_CLONE_ALLOC;
	if (after != NULL) {
		TAILQ_INSERT_AFTER(&c->head, after, ce, link);
	} else {
		TAILQ_INSERT_HEAD(&c->head, ce, link);
	}
	c->size++;
	c->tsp = now;
	/*
	 * Save new allocation unit for caller which will be used
	 * by make_dev().
	 */
	*unit = allocunit;

	return (ce);

snd_clone_alloc_out:
	/*
	 * Set, mark, timestamp the entry if this is a truly free entry.
	 * Leave busy entry alone.
	 */
	if (!(ce->flags & SND_CLONE_BUSY)) {
		ce->pid = curpid;
		ce->tsp = now;
		ce->flags |= SND_CLONE_INVOKE;
	}
	c->tsp = now;
	*dev = ce->devt;

	return (NULL);
}
