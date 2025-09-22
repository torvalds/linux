/*
 * mini-event.h - micro implementation of libevent api, using select() only.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 * 
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * This file implements part of the event(3) libevent api.
 * The back end is only select. Max number of fds is limited.
 * Max number of signals is limited, one handler per signal only.
 * And one handler per fd.
 *
 * Although limited to select() and a max (1024) open fds, it
 * is efficient:
 * o dispatch call caches fd_sets to use. 
 * o handler calling takes time ~ to the number of fds.
 * o timeouts are stored in a redblack tree, sorted, so take log(n).
 * Timeouts are only accurate to the second (no subsecond accuracy).
 * To avoid cpu hogging, fractional timeouts are rounded up to a whole second.
 */

#ifndef MINI_EVENT_H
#define MINI_EVENT_H

#if defined(USE_MINI_EVENT) && !defined(USE_WINSOCK)

#ifdef	HAVE_SYS_SELECT_H
/* for fd_set on OpenBSD */
#include <sys/select.h>
#endif
#include <sys/time.h>

#ifndef HAVE_EVENT_BASE_FREE
#define HAVE_EVENT_BASE_FREE
#endif 

/* redefine to use our own namespace so that on platforms where
 * linkers crosslink library-private symbols with other symbols, it works */
#define event_init minievent_init
#define event_get_version minievent_get_version
#define event_get_method minievent_get_method
#define event_base_dispatch minievent_base_dispatch
#define event_base_loopexit minievent_base_loopexit
#define event_base_free minievent_base_free
#define event_set minievent_set
#define event_base_set minievent_base_set
#define event_add minievent_add
#define event_del minievent_del
#define signal_add minisignal_add
#define signal_del minisignal_del

/** event timeout */
#define EV_TIMEOUT	0x01
/** event fd readable */
#define EV_READ		0x02
/** event fd writable */
#define EV_WRITE	0x04
/** event signal */
#define EV_SIGNAL	0x08
/** event must persist */
#define EV_PERSIST	0x10

/* needs our redblack tree */
#include "rbtree.h"

/** max number of file descriptors to support */
#define MAX_FDS 1024
/** max number of signals to support */
#define MAX_SIG 32

/** event base */
struct event_base
{
	/** sorted by timeout (absolute), ptr */
	rbtree_type* times;
	/** array of 0 - maxfd of ptr to event for it */
	struct event** fds;
	/** max fd in use */
	int maxfd;
	/** capacity - size of the fds array */
	int capfd;
	/* fdset for read write, for fds ready, and added */
	fd_set 
		/** fds for reading */
		reads, 
		/** fds for writing */
		writes, 
		/** fds determined ready for use */
		ready, 
		/** ready plus newly added events. */
		content;
	/** array of 0 - maxsig of ptr to event for it */
	struct event** signals;
	/** if we need to exit */
	int need_to_exit;
	/** where to store time in seconds */
	time_t* time_secs;
	/** where to store time in microseconds */
	struct timeval* time_tv;
};

/**
 * Event structure. Has some of the event elements.
 */
struct event {
	/** node in timeout rbtree */
	rbnode_type node;
	/** is event already added */
	int added;

	/** event base it belongs to */
	struct event_base *ev_base;
	/** fd to poll or -1 for timeouts. signal number for sigs. */
	int ev_fd;
	/** what events this event is interested in, see EV_.. above. */
	short ev_events;
	/** timeout value */
	struct timeval ev_timeout;

	/** callback to call: fd, eventbits, userarg */
	void (*ev_callback)(int, short, void *arg);
	/** callback user arg */
	void *ev_arg;
};

/* function prototypes (some are as they appear in event.h) */
/** create event base */
void *event_init(time_t* time_secs, struct timeval* time_tv);
/** get version */
const char *event_get_version(void);
/** get polling method, select */
const char *event_get_method(void);
/** run select in a loop */
int event_base_dispatch(struct event_base *);
/** exit that loop */
int event_base_loopexit(struct event_base *, struct timeval *);
/** free event base. Free events yourself */
void event_base_free(struct event_base *);
/** set content of event */
void event_set(struct event *, int, short, void (*)(int, short, void *), void *);
/** add event to a base. You *must* call this for every event. */
int event_base_set(struct event_base *, struct event *);
/** add event to make it active. You may not change it with event_set anymore */
int event_add(struct event *, struct timeval *);
/** remove event. You may change it again */
int event_del(struct event *);

/** add a timer */
#define evtimer_add(ev, tv)             event_add(ev, tv)
/** remove a timer */
#define evtimer_del(ev)                 event_del(ev)

/* uses different implementation. Cannot mix fd/timeouts and signals inside
 * the same struct event. create several event structs for that.  */
/** install signal handler */
int signal_add(struct event *, struct timeval *);
/** set signal event contents */
#define signal_set(ev, x, cb, arg)      \
        event_set(ev, x, EV_SIGNAL|EV_PERSIST, cb, arg)
/** remove signal handler */
int signal_del(struct event *);

#endif /* USE_MINI_EVENT and not USE_WINSOCK */

/** compare events in tree, based on timevalue, ptr for uniqueness */
int mini_ev_cmp(const void* a, const void* b);

#endif /* MINI_EVENT_H */
