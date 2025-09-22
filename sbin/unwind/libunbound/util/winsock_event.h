/*
 * util/winsock_event.h - unbound event handling for winsock on windows
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 *
 * This file contains interface functions with the WinSock2 API on Windows.
 * It uses the winsock WSAWaitForMultipleEvents interface on a number of
 * sockets.
 *
 * Note that windows can only wait for max 64 events at one time.
 * 
 * Also, file descriptors cannot be waited for.
 *
 * Named pipes are not easily available (and are not usable in select() ).
 * For interprocess communication, it is possible to wait for a hEvent to
 * be signaled by another thread.
 *
 * When a socket becomes readable, then it will not be flagged as 
 * readable again until you have gotten WOULDBLOCK from a recv routine.
 * That means the event handler must store the readability (edge notify)
 * and process the incoming data until it blocks. 
 * The function performing recv then has to inform the event handler that
 * the socket has blocked, and the event handler can mark it as such.
 * Thus, this file transforms the edge notify from windows to a level notify
 * that is compatible with UNIX.
 * The WSAEventSelect page says that it does do level notify, as long
 * as you call a recv/write/accept at least once when it is signalled.
 * This last bit is not true, even though documented in server2008 api docs
 * from microsoft, it does not happen at all. Instead you have to test for
 * WSAEWOULDBLOCK on a tcp stream, and only then retest the socket.
 * And before that remember the previous result as still valid.
 *
 * To stay 'fair', instead of emptying a socket completely, the event handler
 * can test the other (marked as blocking) sockets for new events.
 *
 * Additionally, TCP accept sockets get special event support.
 *
 * Socket numbers are not starting small, they can be any number (say 33060).
 * Therefore, bitmaps are not used, but arrays.
 *
 * on winsock, you must use recv() and send() for TCP reads and writes,
 * not read() and write(), those work only on files.
 *
 * Also fseek and fseeko do not work if a FILE is not fopen-ed in binary mode.
 *
 * When under a high load windows gives out lots of errors, from recvfrom
 * on udp sockets for example (WSAECONNRESET). Even though the udp socket
 * has no connection per se.
 */

#ifndef UTIL_WINSOCK_EVENT_H
#define UTIL_WINSOCK_EVENT_H

#ifdef USE_WINSOCK

#ifndef HAVE_EVENT_BASE_FREE
#define HAVE_EVENT_BASE_FREE
#endif

/* redefine the calls to different names so that there is no name
 * collision with other code that uses libevent names. (that uses libunbound)*/
#define event_init winsockevent_init
#define event_get_version winsockevent_get_version
#define event_get_method winsockevent_get_method
#define event_base_dispatch winsockevent_base_dispatch
#define event_base_loopexit winsockevent_base_loopexit
#define event_base_free winsockevent_base_free
#define event_set winsockevent_set
#define event_base_set winsockevent_base_set
#define event_add winsockevent_add
#define event_del winsockevent_del
#define signal_add winsocksignal_add
#define signal_del winsocksignal_del

/** event timeout */
#define EV_TIMEOUT      0x01
/** event fd readable */
#define EV_READ         0x02
/** event fd writable */
#define EV_WRITE        0x04
/** event signal */
#define EV_SIGNAL       0x08
/** event must persist */
#define EV_PERSIST      0x10

/* needs our redblack tree */
#include "rbtree.h"

/** max number of signals to support */
#define MAX_SIG 32

/** The number of items that the winsock event handler can service.
 * Windows cannot handle more anyway */
#define WSK_MAX_ITEMS 64

/**
 * event base for winsock event handler
 */
struct event_base
{
	/** sorted by timeout (absolute), ptr */
	rbtree_type* times;
	/** array (first part in use) of handles to work on */
	struct event** items;
	/** number of items in use in array */
	int max;
	/** capacity of array, size of array in items */
	int cap;
	/** array of 0 - maxsig of ptr to event for it */
        struct event** signals;
	/** if we need to exit */
	int need_to_exit;
	/** where to store time in seconds */
	time_t* time_secs;
	/** where to store time in microseconds */
	struct timeval* time_tv;
	/** 
	 * TCP streams have sticky events to them, these are not
	 * reported by the windows event system anymore, we have to
	 * keep reporting those events as present until wouldblock() is
	 * signalled by the handler back to use.
	 */
	int tcp_stickies;
	/**
	 * should next cycle process reinvigorated stickies,
	 * these are stickies that have been stored, but due to a new
	 * event_add a sudden interest in the event has incepted.
	 */
	int tcp_reinvigorated;
	/** The list of events that is currently being processed. */
	WSAEVENT waitfor[WSK_MAX_ITEMS];
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
        void (*ev_callback)(int, short, void *);
        /** callback user arg */
        void *ev_arg;

	/* ----- nonpublic part, for winsock_event only ----- */
	/** index of this event in the items array (if added) */
	int idx;
	/** the event handle to wait for new events to become ready */
	WSAEVENT hEvent;
	/** true if this filedes is a TCP socket and needs special attention */
	int is_tcp;
	/** remembered EV_ values */
	short old_events;
	/** should remembered EV_ values be used for TCP streams. 
	 * Reset after WOULDBLOCK is signaled using the function. */
	int stick_events;

	/** true if this event is a signaling WSAEvent by the user. 
	 * User created and user closed WSAEvent. Only signaled/unsignaled,
	 * no read/write/distinctions needed. */
	int is_signal;
	/** used during callbacks to see which events were just checked */
	int just_checked;
};

/** create event base */
void *event_init(time_t* time_secs, struct timeval* time_tv);
/** get version */
const char *event_get_version(void);
/** get polling method (select,epoll) */
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

#define evtimer_add(ev, tv)             event_add(ev, tv)
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

/** compare events in tree, based on timevalue, ptr for uniqueness */
int mini_ev_cmp(const void* a, const void* b);

/**
 * Routine for windows only, where the handling layer can signal that
 * a TCP stream encountered WSAEWOULDBLOCK for a stream and thus needs
 * retesting the event.
 * Pass if EV_READ or EV_WRITE gave wouldblock.
 */
void winsock_tcp_wouldblock(struct event* ev, int eventbit);

/**
 * Routine for windows only. where you pass a signal WSAEvent that
 * you wait for. When the event is signaled, the callback gets called.
 * The callback has to WSAResetEvent to disable the signal. 
 * @param base: the event base.
 * @param ev: the event structure for data storage
 * 	can be passed uninitialised.
 * @param wsaevent: the WSAEvent that gets signaled.
 * @param cb: callback routine.
 * @param arg: user argument to callback routine.
 * @return false on error.
 */
int winsock_register_wsaevent(struct event_base* base, struct event* ev,
	WSAEVENT wsaevent, void (*cb)(int, short, void*), void* arg);

/**
 * Unregister a wsaevent. User has to close the WSAEVENT itself.
 * @param ev: event data storage.
 */
void winsock_unregister_wsaevent(struct event* ev);

#endif /* USE_WINSOCK */
#endif /* UTIL_WINSOCK_EVENT_H */
