/*
 * util/winsock_event.c - implementation of the unbound winsock event handler. 
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
 * Implementation of the unbound WinSock2 API event notification handler
 * for the Windows port.
 */

#include "config.h"
#ifdef USE_WINSOCK
#include <signal.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>
#include "util/winsock_event.h"
#include "util/fptr_wlist.h"

int mini_ev_cmp(const void* a, const void* b)
{
        const struct event *e = (const struct event*)a;
        const struct event *f = (const struct event*)b;
        if(e->ev_timeout.tv_sec < f->ev_timeout.tv_sec)
                return -1;
        if(e->ev_timeout.tv_sec > f->ev_timeout.tv_sec)
                return 1;
        if(e->ev_timeout.tv_usec < f->ev_timeout.tv_usec)
                return -1;
        if(e->ev_timeout.tv_usec > f->ev_timeout.tv_usec)
                return 1;
        if(e < f)
                return -1;
        if(e > f)
                return 1;
	return 0;
}

/** set time */
static int
settime(struct event_base* base)
{
        if(gettimeofday(base->time_tv, NULL) < 0) {
                return -1;
        }
#ifndef S_SPLINT_S
        *base->time_secs = (time_t)base->time_tv->tv_sec;
#endif
        return 0;
}

#ifdef UNBOUND_DEBUG
/**
 * Find a fd in the list of items.
 * Note that not all items have a fd associated (those are -1).
 * Signals are stored separately, and not searched.
 * @param base: event base to look in.
 * @param fd: what socket to look for.
 * @return the index in the array, or -1 on failure.
 */
static int
find_fd(struct event_base* base, int fd)
{
	int i;
	for(i=0; i<base->max; i++) {
		if(base->items[i]->ev_fd == fd)
			return i;
	}
	return -1;
}
#endif

/** Find ptr in base array */
static void
zero_waitfor(WSAEVENT waitfor[], WSAEVENT x)
{
	int i;
	for(i=0; i<WSK_MAX_ITEMS; i++) {
		if(waitfor[i] == x)
			waitfor[i] = 0;
	}
}

void *event_init(time_t* time_secs, struct timeval* time_tv)
{
        struct event_base* base = (struct event_base*)malloc(
		sizeof(struct event_base));
        if(!base)
                return NULL;
        memset(base, 0, sizeof(*base));
        base->time_secs = time_secs;
        base->time_tv = time_tv;
        if(settime(base) < 0) {
                event_base_free(base);
                return NULL;
        }
	base->items = (struct event**)calloc(WSK_MAX_ITEMS, 
		sizeof(struct event*));
	if(!base->items) {
                event_base_free(base);
                return NULL;
	}
	base->cap = WSK_MAX_ITEMS;
	base->max = 0;
        base->times = rbtree_create(mini_ev_cmp);
        if(!base->times) {
                event_base_free(base);
                return NULL;
        }
        base->signals = (struct event**)calloc(MAX_SIG, sizeof(struct event*));
        if(!base->signals) {
                event_base_free(base);
                return NULL;
        }
	base->tcp_stickies = 0;
	base->tcp_reinvigorated = 0;
	verbose(VERB_CLIENT, "winsock_event inited");
        return base;
}

const char *event_get_version(void)
{
	return "winsock-event-"PACKAGE_VERSION;
}

const char *event_get_method(void)
{
	return "WSAWaitForMultipleEvents";
}

/** call timeouts handlers, and return how long to wait for next one or -1 */
static void handle_timeouts(struct event_base* base, struct timeval* now,
        struct timeval* wait)
{
        struct event* p;
#ifndef S_SPLINT_S
        wait->tv_sec = (time_t)-1;
#endif
	verbose(VERB_CLIENT, "winsock_event handle_timeouts");

        while((rbnode_type*)(p = (struct event*)rbtree_first(base->times))
                !=RBTREE_NULL) {
#ifndef S_SPLINT_S
                if(p->ev_timeout.tv_sec > now->tv_sec ||
                        (p->ev_timeout.tv_sec==now->tv_sec &&
                        p->ev_timeout.tv_usec > now->tv_usec)) {
                        /* there is a next larger timeout. wait for it */
                        wait->tv_sec = p->ev_timeout.tv_sec - now->tv_sec;
                        if(now->tv_usec > p->ev_timeout.tv_usec) {
                                wait->tv_sec--;
                                wait->tv_usec = 1000000 - (now->tv_usec -
                                        p->ev_timeout.tv_usec);
                        } else {
                                wait->tv_usec = p->ev_timeout.tv_usec
                                        - now->tv_usec;
                        }
			verbose(VERB_CLIENT, "winsock_event wait=" ARG_LL "d.%6.6d",
				(long long)wait->tv_sec, (int)wait->tv_usec);
                        return;
                }
#endif
                /* event times out, remove it */
                (void)rbtree_delete(base->times, p);
                p->ev_events &= ~EV_TIMEOUT;
                fptr_ok(fptr_whitelist_event(p->ev_callback));
                (*p->ev_callback)(p->ev_fd, EV_TIMEOUT, p->ev_arg);
        }
	verbose(VERB_CLIENT, "winsock_event wait=(-1)");
}

/** handle is_signal events and see if signalled */
static void handle_signal(struct event* ev)
{
	DWORD ret;
	log_assert(ev->is_signal && ev->hEvent);
	/* see if the event is signalled */
	ret = WSAWaitForMultipleEvents(1, &ev->hEvent, 0 /* any object */,
		0 /* return immediately */, 0 /* not alertable for IOcomple*/);
	if(ret == WSA_WAIT_IO_COMPLETION || ret == WSA_WAIT_FAILED) {
		log_err("WSAWaitForMultipleEvents(signal) failed: %s",
			wsa_strerror(WSAGetLastError()));
		return;
	}
	if(ret == WSA_WAIT_TIMEOUT) {
		/* not signalled */
		return;
	}

	/* reset the signal */
	if(!WSAResetEvent(ev->hEvent))
		log_err("WSAResetEvent failed: %s",
			wsa_strerror(WSAGetLastError()));
	/* do the callback (which may set the signal again) */
	fptr_ok(fptr_whitelist_event(ev->ev_callback));
	(*ev->ev_callback)(ev->ev_fd, ev->ev_events, ev->ev_arg);
}

/** call select and callbacks for that */
static int handle_select(struct event_base* base, struct timeval* wait)
{
	DWORD timeout = 0; /* in milliseconds */	
	DWORD ret;
	struct event* eventlist[WSK_MAX_ITEMS];
	WSANETWORKEVENTS netev;
	int i, numwait = 0, startidx = 0, was_timeout = 0;
	int newstickies = 0;
	struct timeval nultm;

	verbose(VERB_CLIENT, "winsock_event handle_select");

#ifndef S_SPLINT_S
        if(wait->tv_sec==(time_t)-1)
                wait = NULL;
	if(wait)
		timeout = wait->tv_sec*1000 + wait->tv_usec/1000;
	if(base->tcp_stickies) {
		wait = &nultm;
		nultm.tv_sec = 0;
		nultm.tv_usec = 0;
		timeout = 0; /* no waiting, we have sticky events */
	}
#endif

	/* prepare event array */
	for(i=0; i<base->max; i++) {
		if(base->items[i]->ev_fd == -1 && !base->items[i]->is_signal)
			continue; /* skip timer only events */
		eventlist[numwait] = base->items[i];
		base->waitfor[numwait++] = base->items[i]->hEvent;
		if(numwait == WSK_MAX_ITEMS)
			break; /* sanity check */
	}
	log_assert(numwait <= WSA_MAXIMUM_WAIT_EVENTS);
	verbose(VERB_CLIENT, "winsock_event bmax=%d numwait=%d wait=%s "
		"timeout=%d", base->max, numwait, (wait?"<wait>":"<null>"),
		(int)timeout);

	/* do the wait */
	if(numwait == 0) {
		/* WSAWaitFor.. doesn't like 0 event objects */
		if(wait) {
			Sleep(timeout);
		}
		was_timeout = 1;
	} else {
		ret = WSAWaitForMultipleEvents(numwait, base->waitfor,
			0 /* do not wait for all, just one will do */,
			wait?timeout:WSA_INFINITE,
			0); /* we are not alertable (IO completion events) */
		if(ret == WSA_WAIT_IO_COMPLETION) {
			log_err("WSAWaitForMultipleEvents failed: WSA_WAIT_IO_COMPLETION");
			return -1;
		} else if(ret == WSA_WAIT_FAILED) {
			log_err("WSAWaitForMultipleEvents failed: %s", 
				wsa_strerror(WSAGetLastError()));
			return -1;
		} else if(ret == WSA_WAIT_TIMEOUT) {
			was_timeout = 1;
		} else
			startidx = ret - WSA_WAIT_EVENT_0;
	}
	verbose(VERB_CLIENT, "winsock_event wake was_timeout=%d startidx=%d", 
		was_timeout, startidx);

	/* get new time after wait */
        if(settime(base) < 0)
               return -1;

	/* callbacks */
	if(base->tcp_stickies)
		startidx = 0; /* process all events, some are sticky */
	for(i=startidx; i<numwait; i++)
		eventlist[i]->just_checked = 1;

	verbose(VERB_CLIENT, "winsock_event signals");
	for(i=startidx; i<numwait; i++) {
		if(!base->waitfor[i])
			continue; /* was deleted */
		if(eventlist[i]->is_signal) {
			eventlist[i]->just_checked = 0;
			handle_signal(eventlist[i]);
		}
	}
	/* early exit - do not process network, exit quickly */
	if(base->need_to_exit)
		return 0;

	verbose(VERB_CLIENT, "winsock_event net");
	for(i=startidx; i<numwait; i++) {
		short bits = 0;
		/* eventlist[i] fired */
		/* see if eventlist[i] is still valid and just checked from
		 * WSAWaitForEvents */
		if(!base->waitfor[i])
			continue; /* was deleted */
		if(!eventlist[i]->just_checked)
			continue; /* added by other callback */
		if(eventlist[i]->is_signal)
			continue; /* not a network event at all */
		eventlist[i]->just_checked = 0;

		if(WSAEnumNetworkEvents(eventlist[i]->ev_fd, 
			base->waitfor[i], /* reset the event handle */
			/*NULL,*/ /* do not reset the event handle */
			&netev) != 0) {
			log_err("WSAEnumNetworkEvents failed: %s", 
				wsa_strerror(WSAGetLastError()));
			return -1;
		}
		if((netev.lNetworkEvents & FD_READ)) {
			if(netev.iErrorCode[FD_READ_BIT] != 0)
				verbose(VERB_ALGO, "FD_READ_BIT error: %s",
				wsa_strerror(netev.iErrorCode[FD_READ_BIT]));
			bits |= EV_READ;
		}
		if((netev.lNetworkEvents & FD_WRITE)) {
			if(netev.iErrorCode[FD_WRITE_BIT] != 0)
				verbose(VERB_ALGO, "FD_WRITE_BIT error: %s",
				wsa_strerror(netev.iErrorCode[FD_WRITE_BIT]));
			bits |= EV_WRITE;
		}
		if((netev.lNetworkEvents & FD_CONNECT)) {
			if(netev.iErrorCode[FD_CONNECT_BIT] != 0)
				verbose(VERB_ALGO, "FD_CONNECT_BIT error: %s",
				wsa_strerror(netev.iErrorCode[FD_CONNECT_BIT]));
			bits |= EV_READ;
			bits |= EV_WRITE;
		}
		if((netev.lNetworkEvents & FD_ACCEPT)) {
			if(netev.iErrorCode[FD_ACCEPT_BIT] != 0)
				verbose(VERB_ALGO, "FD_ACCEPT_BIT error: %s",
				wsa_strerror(netev.iErrorCode[FD_ACCEPT_BIT]));
			bits |= EV_READ;
		}
		if((netev.lNetworkEvents & FD_CLOSE)) {
			if(netev.iErrorCode[FD_CLOSE_BIT] != 0)
				verbose(VERB_ALGO, "FD_CLOSE_BIT error: %s",
				wsa_strerror(netev.iErrorCode[FD_CLOSE_BIT]));
			bits |= EV_READ;
			bits |= EV_WRITE;
		}
		if(eventlist[i]->is_tcp && eventlist[i]->stick_events) {
			verbose(VERB_ALGO, "winsock %d pass sticky %s%s",
				eventlist[i]->ev_fd,
				(eventlist[i]->old_events&EV_READ)?"EV_READ":"",
				(eventlist[i]->old_events&EV_WRITE)?"EV_WRITE":"");
			bits |= eventlist[i]->old_events;
		}
		if(eventlist[i]->is_tcp && bits) {
			eventlist[i]->old_events = bits;
			eventlist[i]->stick_events = 1;
			if((eventlist[i]->ev_events & bits)) {
				newstickies = 1;
			}
			verbose(VERB_ALGO, "winsock %d store sticky %s%s",
				eventlist[i]->ev_fd,
				(eventlist[i]->old_events&EV_READ)?"EV_READ":"",
				(eventlist[i]->old_events&EV_WRITE)?"EV_WRITE":"");
		}
		if((bits & eventlist[i]->ev_events)) {
			verbose(VERB_ALGO, "winsock event callback %p fd=%d "
				"%s%s%s%s%s ; %s%s%s", 
				eventlist[i], eventlist[i]->ev_fd,
				(netev.lNetworkEvents&FD_READ)?" FD_READ":"",
				(netev.lNetworkEvents&FD_WRITE)?" FD_WRITE":"",
				(netev.lNetworkEvents&FD_CONNECT)?
					" FD_CONNECT":"",
				(netev.lNetworkEvents&FD_ACCEPT)?
					" FD_ACCEPT":"",
				(netev.lNetworkEvents&FD_CLOSE)?" FD_CLOSE":"",
				(bits&EV_READ)?" EV_READ":"",
				(bits&EV_WRITE)?" EV_WRITE":"",
				(bits&EV_TIMEOUT)?" EV_TIMEOUT":"");
				
                        fptr_ok(fptr_whitelist_event(
                                eventlist[i]->ev_callback));
                        (*eventlist[i]->ev_callback)(eventlist[i]->ev_fd,
                                bits & eventlist[i]->ev_events, 
				eventlist[i]->ev_arg);
		}
		if(eventlist[i]->is_tcp && bits)
			verbose(VERB_ALGO, "winsock %d got sticky %s%s",
				eventlist[i]->ev_fd,
				(eventlist[i]->old_events&EV_READ)?"EV_READ":"",
				(eventlist[i]->old_events&EV_WRITE)?"EV_WRITE":"");
	}
	verbose(VERB_CLIENT, "winsock_event net");
	if(base->tcp_reinvigorated) {
		verbose(VERB_CLIENT, "winsock_event reinvigorated");
		base->tcp_reinvigorated = 0;
		newstickies = 1;
	}
	base->tcp_stickies = newstickies;
	verbose(VERB_CLIENT, "winsock_event handle_select end");
        return 0;
}

int event_base_dispatch(struct event_base *base)
{
        struct timeval wait;
        if(settime(base) < 0)
                return -1;
        while(!base->need_to_exit)
        {
                /* see if timeouts need handling */
                handle_timeouts(base, base->time_tv, &wait);
                if(base->need_to_exit)
                        return 0;
                /* do select */
                if(handle_select(base, &wait) < 0) {
                        if(base->need_to_exit)
                                return 0;
                        return -1;
                }
        }
        return 0;
}

int event_base_loopexit(struct event_base *base, 
	struct timeval * ATTR_UNUSED(tv))
{
	verbose(VERB_CLIENT, "winsock_event loopexit");
        base->need_to_exit = 1;
        return 0;
}

void event_base_free(struct event_base *base)
{
	verbose(VERB_CLIENT, "winsock_event event_base_free");
        if(!base)
                return;
	free(base->items);
        free(base->times);
        free(base->signals);
        free(base);
}

void event_set(struct event *ev, int fd, short bits, 
	void (*cb)(int, short, void *), void *arg)
{
        ev->node.key = ev;
        ev->ev_fd = fd;
        ev->ev_events = bits;
        ev->ev_callback = cb;
        fptr_ok(fptr_whitelist_event(ev->ev_callback));
        ev->ev_arg = arg;
	ev->just_checked = 0;
        ev->added = 0;
}

int event_base_set(struct event_base *base, struct event *ev)
{
        ev->ev_base = base;
	ev->old_events = 0;
	ev->stick_events = 0;
        ev->added = 0;
        return 0;
}

int event_add(struct event *ev, struct timeval *tv)
{
	verbose(VERB_ALGO, "event_add %p added=%d fd=%d tv=" ARG_LL "d %s%s%s", 
		ev, ev->added, ev->ev_fd, 
		(tv?(long long)tv->tv_sec*1000+(long long)tv->tv_usec/1000:-1),
		(ev->ev_events&EV_READ)?" EV_READ":"",
		(ev->ev_events&EV_WRITE)?" EV_WRITE":"",
		(ev->ev_events&EV_TIMEOUT)?" EV_TIMEOUT":"");
        if(ev->added)
                event_del(ev);
	log_assert(ev->ev_fd==-1 || find_fd(ev->ev_base, ev->ev_fd) == -1);
	ev->is_tcp = 0;
	ev->is_signal = 0;
	ev->just_checked = 0;

        if((ev->ev_events&(EV_READ|EV_WRITE)) && ev->ev_fd != -1) {
		BOOL b=0;
		int t, l;
		long events = 0;

		if(ev->ev_base->max == ev->ev_base->cap)
			return -1;
		ev->idx = ev->ev_base->max++;
		ev->ev_base->items[ev->idx] = ev;

		if( (ev->ev_events&EV_READ) )
			events |= FD_READ;
		if( (ev->ev_events&EV_WRITE) )
			events |= FD_WRITE;
		l = sizeof(t);
		if(getsockopt(ev->ev_fd, SOL_SOCKET, SO_TYPE,
			(void*)&t, &l) != 0)
			log_err("getsockopt(SO_TYPE) failed: %s",
				wsa_strerror(WSAGetLastError()));
		if(t == SOCK_STREAM) {
			/* TCP socket */
			ev->is_tcp = 1;
			events |= FD_CLOSE;
			if( (ev->ev_events&EV_WRITE) )
				events |= FD_CONNECT;
			l = sizeof(b);
			if(getsockopt(ev->ev_fd, SOL_SOCKET, SO_ACCEPTCONN,
				(void*)&b, &l) != 0)
				log_err("getsockopt(SO_ACCEPTCONN) failed: %s",
					wsa_strerror(WSAGetLastError()));
			if(b) /* TCP accept socket */
				events |= FD_ACCEPT;
		}
		ev->hEvent = WSACreateEvent();
		if(ev->hEvent == WSA_INVALID_EVENT)
			log_err("WSACreateEvent failed: %s",
				wsa_strerror(WSAGetLastError()));
		/* automatically sets fd to nonblocking mode.
		 * nonblocking cannot be disabled, until wsaES(fd, NULL, 0) */
		if(WSAEventSelect(ev->ev_fd, ev->hEvent, events) != 0) {
			log_err("WSAEventSelect failed: %s",
				wsa_strerror(WSAGetLastError()));
		}
		if(ev->is_tcp && ev->stick_events && 
			(ev->ev_events & ev->old_events)) {
			/* go to processing the sticky event right away */
			ev->ev_base->tcp_reinvigorated = 1;
		}
	}

	if(tv && (ev->ev_events&EV_TIMEOUT)) {
#ifndef S_SPLINT_S
                struct timeval *now = ev->ev_base->time_tv;
                ev->ev_timeout.tv_sec = tv->tv_sec + now->tv_sec;
                ev->ev_timeout.tv_usec = tv->tv_usec + now->tv_usec;
                while(ev->ev_timeout.tv_usec >= 1000000) {
                        ev->ev_timeout.tv_usec -= 1000000;
                        ev->ev_timeout.tv_sec++;
                }
#endif
                (void)rbtree_insert(ev->ev_base->times, &ev->node);
        }
        ev->added = 1;
	return 0;
}

int event_del(struct event *ev)
{
	verbose(VERB_ALGO, "event_del %p added=%d fd=%d tv=" ARG_LL "d %s%s%s", 
		ev, ev->added, ev->ev_fd, 
		(ev->ev_events&EV_TIMEOUT)?(long long)ev->ev_timeout.tv_sec*1000+
		(long long)ev->ev_timeout.tv_usec/1000:-1,
		(ev->ev_events&EV_READ)?" EV_READ":"",
		(ev->ev_events&EV_WRITE)?" EV_WRITE":"",
		(ev->ev_events&EV_TIMEOUT)?" EV_TIMEOUT":"");
	if(!ev->added)
		return 0;
	log_assert(ev->added);
        if((ev->ev_events&EV_TIMEOUT))
                (void)rbtree_delete(ev->ev_base->times, &ev->node);
        if((ev->ev_events&(EV_READ|EV_WRITE)) && ev->ev_fd != -1) {
		log_assert(ev->ev_base->max > 0);
		/* remove item and compact the list */
		ev->ev_base->items[ev->idx] = 
			ev->ev_base->items[ev->ev_base->max-1];
		ev->ev_base->items[ev->ev_base->max-1] = NULL;
		ev->ev_base->max--;
		if(ev->idx < ev->ev_base->max)
			ev->ev_base->items[ev->idx]->idx = ev->idx;
		zero_waitfor(ev->ev_base->waitfor, ev->hEvent);

		if(WSAEventSelect(ev->ev_fd, ev->hEvent, 0) != 0)
			log_err("WSAEventSelect(disable) failed: %s",
				wsa_strerror(WSAGetLastError()));
		if(!WSACloseEvent(ev->hEvent))
			log_err("WSACloseEvent failed: %s",
				wsa_strerror(WSAGetLastError()));
	}
	ev->just_checked = 0;
        ev->added = 0;
        return 0;
}

/** which base gets to handle signals */
static struct event_base* signal_base = NULL;
/** signal handler */
static RETSIGTYPE sigh(int sig)
{
        struct event* ev;
        if(!signal_base || sig < 0 || sig >= MAX_SIG)
                return;
        ev = signal_base->signals[sig];
        if(!ev)
                return;
        fptr_ok(fptr_whitelist_event(ev->ev_callback));
        (*ev->ev_callback)(sig, EV_SIGNAL, ev->ev_arg);
}

int signal_add(struct event *ev, struct timeval * ATTR_UNUSED(tv))
{
        if(ev->ev_fd == -1 || ev->ev_fd >= MAX_SIG)
                return -1;
        signal_base = ev->ev_base;
        ev->ev_base->signals[ev->ev_fd] = ev;
        ev->added = 1;
        if(signal(ev->ev_fd, sigh) == SIG_ERR) {
                return -1;
        }
        return 0;
}

int signal_del(struct event *ev)
{
        if(ev->ev_fd == -1 || ev->ev_fd >= MAX_SIG)
                return -1;
        ev->ev_base->signals[ev->ev_fd] = NULL;
        ev->added = 0;
        return 0;
}

void winsock_tcp_wouldblock(struct event* ev, int eventbits)
{
	verbose(VERB_ALGO, "winsock: tcp wouldblock %s", 
		eventbits==EV_READ?"EV_READ":"EV_WRITE");
	ev->old_events &= (~eventbits);
	if(ev->old_events == 0)
		ev->stick_events = 0;
		/* in case this is the last sticky event, we could
		 * possibly run an empty handler loop to reset the base
		 * tcp_stickies variable 
		 */
}

int winsock_register_wsaevent(struct event_base* base, struct event* ev,
	WSAEVENT wsaevent, void (*cb)(int, short, void*), void* arg)
{
	if(base->max == base->cap)
		return 0;
	memset(ev, 0, sizeof(*ev));
	ev->ev_fd = -1;
	ev->ev_events = EV_READ;
	ev->ev_callback = cb;
	ev->ev_arg = arg;
	ev->is_signal = 1;
	ev->hEvent = wsaevent;
	ev->added = 1;
	ev->ev_base = base;
	ev->idx = ev->ev_base->max++;
	ev->ev_base->items[ev->idx] = ev;
	return 1;
}

void winsock_unregister_wsaevent(struct event* ev)
{
	if(!ev || !ev->added) return;
	log_assert(ev->added && ev->ev_base->max > 0)
	/* remove item and compact the list */
	ev->ev_base->items[ev->idx] = ev->ev_base->items[ev->ev_base->max-1];
	ev->ev_base->items[ev->ev_base->max-1] = NULL;
	ev->ev_base->max--;
	if(ev->idx < ev->ev_base->max)
		ev->ev_base->items[ev->idx]->idx = ev->idx;
	ev->added = 0;
}

#else /* USE_WINSOCK */
/** symbol so this codefile defines symbols. pleasing ranlib on OSX 10.5 */
int winsock_unused_symbol = 1;
#endif /* USE_WINSOCK */
