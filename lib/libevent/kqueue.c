/*	$OpenBSD: kqueue.c,v 1.43 2024/03/23 22:51:49 yasuoka Exp $	*/

/*
 * Copyright 2000-2002 Niels Provos <provos@citi.umich.edu>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/event.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "event.h"
#include "event-internal.h"
#include "log.h"
#include "evsignal.h"

#define EVLIST_X_KQINKERNEL	0x1000

#define NEVENT		64

struct kqop {
	struct kevent *changes;
	int nchanges;
	struct kevent *events;
	struct event_list evsigevents[NSIG];
	int nevents;
	int kq;
	pid_t pid;
};

static void *kq_init	(struct event_base *);
static int kq_add	(void *, struct event *);
static int kq_del	(void *, struct event *);
static int kq_dispatch	(struct event_base *, void *, struct timeval *);
static int kq_insert	(struct kqop *, struct kevent *);
static void kq_dealloc (struct event_base *, void *);

const struct eventop kqops = {
	"kqueue",
	kq_init,
	kq_add,
	kq_del,
	kq_dispatch,
	kq_dealloc,
	1 /* need reinit */
};

static void *
kq_init(struct event_base *base)
{
	int i, kq;
	struct kqop *kqueueop;

	/* Disable kqueue when this environment variable is set */
	if (!issetugid() && getenv("EVENT_NOKQUEUE"))
		return (NULL);

	if (!(kqueueop = calloc(1, sizeof(struct kqop))))
		return (NULL);

	/* Initialize the kernel queue */

	if ((kq = kqueue()) == -1) {
		event_warn("kqueue");
		free (kqueueop);
		return (NULL);
	}

	kqueueop->kq = kq;

	kqueueop->pid = getpid();

	/* Initialize fields */
	kqueueop->changes = calloc(NEVENT, sizeof(struct kevent));
	if (kqueueop->changes == NULL) {
		free (kqueueop);
		return (NULL);
	}
	kqueueop->events = calloc(NEVENT, sizeof(struct kevent));
	if (kqueueop->events == NULL) {
		free (kqueueop->changes);
		free (kqueueop);
		return (NULL);
	}
	kqueueop->nevents = NEVENT;

	/* we need to keep track of multiple events per signal */
	for (i = 0; i < NSIG; ++i) {
		TAILQ_INIT(&kqueueop->evsigevents[i]);
	}

	return (kqueueop);
}

static int
kq_insert(struct kqop *kqop, struct kevent *kev)
{
	int nevents = kqop->nevents;

	if (kqop->nchanges == nevents) {
		struct kevent *newchange;
		struct kevent *newresult;

		if (nevents > INT_MAX / 2) {
			event_warnx("%s: integer overflow", __func__);
			return (-1);
		}
		nevents *= 2;

		newchange = recallocarray(kqop->changes,
		    kqop->nevents, nevents, sizeof(struct kevent));
		if (newchange == NULL) {
			event_warn("%s: recallocarray", __func__);
			return (-1);
		}
		kqop->changes = newchange;

		newresult = recallocarray(kqop->events,
		    kqop->nevents, nevents, sizeof(struct kevent));

		/*
		 * If we fail, we don't have to worry about freeing,
		 * the next realloc will pick it up.
		 */
		if (newresult == NULL) {
			event_warn("%s: recallocarray", __func__);
			return (-1);
		}
		kqop->events = newresult;

		kqop->nevents = nevents;
	}

	memcpy(&kqop->changes[kqop->nchanges++], kev, sizeof(struct kevent));

	event_debug(("%s: fd %d %s%s",
		__func__, (int)kev->ident,
		kev->filter == EVFILT_READ ? "EVFILT_READ" : "EVFILT_WRITE",
		kev->flags == EV_DELETE ? " (del)" : ""));

	return (0);
}

static void
kq_sighandler(int sig)
{
	/* Do nothing here */
}

static int
kq_dispatch(struct event_base *base, void *arg, struct timeval *tv)
{
	struct kqop *kqop = arg;
	struct kevent *changes = kqop->changes;
	struct kevent *events = kqop->events;
	struct event *ev;
	struct timespec ts, *ts_p = NULL;
	int i, res;

	if (tv != NULL) {
		TIMEVAL_TO_TIMESPEC(tv, &ts);
		ts_p = &ts;
	}

	res = kevent(kqop->kq, kqop->nchanges ? changes : NULL, kqop->nchanges,
	    events, kqop->nevents, ts_p);
	kqop->nchanges = 0;
	if (res == -1) {
		if (errno != EINTR) {
			event_warn("kevent");
			return (-1);
		}

		return (0);
	}

	event_debug(("%s: kevent reports %d", __func__, res));

	for (i = 0; i < res; i++) {
		int which = 0;

		if (events[i].flags & EV_ERROR) {
			switch (events[i].data) {

			/* Can occur on delete if we are not currently
			 * watching any events on this fd.  That can
			 * happen when the fd was closed and another
			 * file was opened with that fd. */
			case ENOENT:
			/* Can occur for reasons not fully understood
			 * on FreeBSD. */
			case EINVAL:
				continue;
			/* Can occur on a delete if the fd is closed.  Can
			 * occur on an add if the fd was one side of a pipe,
			 * and the other side was closed. */
			case EBADF:
				continue;
			/* These two can occur on an add if the fd was one side
			 * of a pipe, and the other side was closed. */
			case EPERM:
			case EPIPE:
				/* Report read events, if we're listening for
				 * them, so that the user can learn about any
				 * add errors.  (If the operation was a
				 * delete, then udata should be cleared.) */
				if (events[i].udata) {
					/* The operation was an add:
					 * report the error as a read. */
					which |= EV_READ;
					break;
				} else {
					/* The operation was a del:
					 * report nothing. */
					continue;
				}

			/* Other errors shouldn't occur. */
			default:
				errno = events[i].data;
				return (-1);
			}
		} else if (events[i].filter == EVFILT_READ) {
			which |= EV_READ;
		} else if (events[i].filter == EVFILT_WRITE) {
			which |= EV_WRITE;
		} else if (events[i].filter == EVFILT_SIGNAL) {
			which |= EV_SIGNAL;
		}

		if (!which)
			continue;

		if (events[i].filter == EVFILT_SIGNAL) {
			struct event_list *head =
			    (struct event_list *)events[i].udata;
			TAILQ_FOREACH(ev, head, ev_signal_next) {
				event_active(ev, which, events[i].data);
			}
		} else {
			ev = (struct event *)events[i].udata;

			if (!(ev->ev_events & EV_PERSIST))
				ev->ev_flags &= ~EVLIST_X_KQINKERNEL;

			event_active(ev, which, 1);
		}
	}

	return (0);
}


static int
kq_add(void *arg, struct event *ev)
{
	struct kqop *kqop = arg;
	struct kevent kev;

	if (ev->ev_events & EV_SIGNAL) {
		int nsignal = EVENT_SIGNAL(ev);

		assert(nsignal >= 0 && nsignal < NSIG);
		if (TAILQ_EMPTY(&kqop->evsigevents[nsignal])) {
			struct timespec timeout = { 0, 0 };

			memset(&kev, 0, sizeof(kev));
			kev.ident = nsignal;
			kev.filter = EVFILT_SIGNAL;
			kev.flags = EV_ADD;
			kev.udata = &kqop->evsigevents[nsignal];

			/* Be ready for the signal if it is sent any
			 * time between now and the next call to
			 * kq_dispatch. */
			if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1)
				return (-1);

			if (_evsignal_set_handler(ev->ev_base, nsignal,
				kq_sighandler) == -1)
				return (-1);
		}

		TAILQ_INSERT_TAIL(&kqop->evsigevents[nsignal], ev,
		    ev_signal_next);
		ev->ev_flags |= EVLIST_X_KQINKERNEL;
		return (0);
	}

	if (ev->ev_events & EV_READ) {
		memset(&kev, 0, sizeof(kev));
		kev.ident = ev->ev_fd;
		kev.filter = EVFILT_READ;
		/* Make it behave like select() and poll() */
		kev.fflags = NOTE_EOF;
		kev.flags = EV_ADD;
		if (!(ev->ev_events & EV_PERSIST))
			kev.flags |= EV_ONESHOT;
		kev.udata = ev;

		if (kq_insert(kqop, &kev) == -1)
			return (-1);

		ev->ev_flags |= EVLIST_X_KQINKERNEL;
	}

	if (ev->ev_events & EV_WRITE) {
		memset(&kev, 0, sizeof(kev));
		kev.ident = ev->ev_fd;
		kev.filter = EVFILT_WRITE;
		kev.flags = EV_ADD;
		if (!(ev->ev_events & EV_PERSIST))
			kev.flags |= EV_ONESHOT;
		kev.udata = ev;

		if (kq_insert(kqop, &kev) == -1)
			return (-1);

		ev->ev_flags |= EVLIST_X_KQINKERNEL;
	}

	return (0);
}

static int
kq_del(void *arg, struct event *ev)
{
	int i, j;
	struct kqop *kqop = arg;
	struct kevent kev;

	if (!(ev->ev_flags & EVLIST_X_KQINKERNEL))
		return (0);

	if (ev->ev_events & EV_SIGNAL) {
		int nsignal = EVENT_SIGNAL(ev);
		struct timespec timeout = { 0, 0 };

		assert(nsignal >= 0 && nsignal < NSIG);
		TAILQ_REMOVE(&kqop->evsigevents[nsignal], ev, ev_signal_next);
		if (TAILQ_EMPTY(&kqop->evsigevents[nsignal])) {
			memset(&kev, 0, sizeof(kev));
			kev.ident = nsignal;
			kev.filter = EVFILT_SIGNAL;
			kev.flags = EV_DELETE;

			/* Because we insert signal events
			 * immediately, we need to delete them
			 * immediately, too */
			if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1)
				return (-1);

			if (_evsignal_restore_handler(ev->ev_base,
				nsignal) == -1)
				return (-1);
		}

		ev->ev_flags &= ~EVLIST_X_KQINKERNEL;
		return (0);
	}

	for (i = j = 0; i < kqop->nchanges; i++) {
		if (kqop->changes[i].udata == ev &&
		    (kqop->changes[i].flags & EV_ADD) != 0)
			continue;	/* delete this */
		if (i != j)
			memcpy(&kqop->changes[j], &kqop->changes[i],
			    sizeof(struct kevent));
		j++;
	}
	if (kqop->nchanges != j) {
		kqop->nchanges = j;
		ev->ev_flags &= ~EVLIST_X_KQINKERNEL;
		return (0);
	}

	if (ev->ev_events & EV_READ) {
		memset(&kev, 0, sizeof(kev));
		kev.ident = ev->ev_fd;
		kev.filter = EVFILT_READ;
		kev.flags = EV_DELETE;

		if (kq_insert(kqop, &kev) == -1)
			return (-1);

		ev->ev_flags &= ~EVLIST_X_KQINKERNEL;
	}

	if (ev->ev_events & EV_WRITE) {
		memset(&kev, 0, sizeof(kev));
		kev.ident = ev->ev_fd;
		kev.filter = EVFILT_WRITE;
		kev.flags = EV_DELETE;

		if (kq_insert(kqop, &kev) == -1)
			return (-1);

		ev->ev_flags &= ~EVLIST_X_KQINKERNEL;
	}

	return (0);
}

static void
kq_dealloc(struct event_base *base, void *arg)
{
	struct kqop *kqop = arg;

	evsignal_dealloc(base);

	free(kqop->changes);
	free(kqop->events);
	if (kqop->kq >= 0 && kqop->pid == getpid())
		close(kqop->kq);

	memset(kqop, 0, sizeof(struct kqop));
	free(kqop);
}
