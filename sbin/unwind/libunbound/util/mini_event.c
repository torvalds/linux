/*
 * mini_event.c - implementation of part of libevent api, portably.
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
 * 
 */

/**
 * \file
 * fake libevent implementation. Less broad in functionality, and only
 * supports select(2).
 */

#include "config.h"
#include "util/mini_event.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>

#if defined(USE_MINI_EVENT) && !defined(USE_WINSOCK)
#include <signal.h>
#include "util/fptr_wlist.h"

/** compare events in tree, based on timevalue, ptr for uniqueness */
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

/** create event base */
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
	base->times = rbtree_create(mini_ev_cmp);
	if(!base->times) {
		event_base_free(base);
		return NULL;
	}
	base->capfd = MAX_FDS;
#ifdef FD_SETSIZE
	if((int)FD_SETSIZE < base->capfd)
		base->capfd = (int)FD_SETSIZE;
#endif
	base->fds = (struct event**)calloc((size_t)base->capfd, 
		sizeof(struct event*));
	if(!base->fds) {
		event_base_free(base);
		return NULL;
	}
	base->signals = (struct event**)calloc(MAX_SIG, sizeof(struct event*));
	if(!base->signals) {
		event_base_free(base);
		return NULL;
	}
#ifndef S_SPLINT_S
	FD_ZERO(&base->reads);
	FD_ZERO(&base->writes);
#endif
	return base;
}

/** get version */
const char *event_get_version(void)
{
	return "mini-event-"PACKAGE_VERSION;
}

/** get polling method, select */
const char *event_get_method(void)
{
	return "select";
}

/** call timeouts handlers, and return how long to wait for next one or -1 */
static void handle_timeouts(struct event_base* base, struct timeval* now, 
	struct timeval* wait)
{
	struct event* p;
#ifndef S_SPLINT_S
	wait->tv_sec = (time_t)-1;
#endif

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
			return;
		}
#endif
		/* event times out, remove it */
		(void)rbtree_delete(base->times, p);
		p->ev_events &= ~EV_TIMEOUT;
		fptr_ok(fptr_whitelist_event(p->ev_callback));
		(*p->ev_callback)(p->ev_fd, EV_TIMEOUT, p->ev_arg);
	}
}

/** call select and callbacks for that */
static int handle_select(struct event_base* base, struct timeval* wait)
{
	fd_set r, w;
	int ret, i;

#ifndef S_SPLINT_S
	if(wait->tv_sec==(time_t)-1)
		wait = NULL;
#endif
	memmove(&r, &base->reads, sizeof(fd_set));
	memmove(&w, &base->writes, sizeof(fd_set));
	memmove(&base->ready, &base->content, sizeof(fd_set));

	if((ret = select(base->maxfd+1, &r, &w, NULL, wait)) == -1) {
		ret = errno;
		if(settime(base) < 0)
			return -1;
		errno = ret;
		if(ret == EAGAIN || ret == EINTR)
			return 0;
		return -1;
	}
	if(settime(base) < 0)
		return -1;
	
	for(i=0; i<base->maxfd+1; i++) {
		short bits = 0;
		if(!base->fds[i] || !(FD_ISSET(i, &base->ready))) {
			continue;
		}
		if(FD_ISSET(i, &r)) {
			bits |= EV_READ;
			ret--;
		}
		if(FD_ISSET(i, &w)) {
			bits |= EV_WRITE;
			ret--;
		}
		bits &= base->fds[i]->ev_events;
		if(bits) {
			fptr_ok(fptr_whitelist_event(
				base->fds[i]->ev_callback));
			(*base->fds[i]->ev_callback)(base->fds[i]->ev_fd, 
				bits, base->fds[i]->ev_arg);
			if(ret==0)
				break;
		}
	}
	return 0;
}

/** run select in a loop */
int event_base_dispatch(struct event_base* base)
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

/** exit that loop */
int event_base_loopexit(struct event_base* base, 
	struct timeval* ATTR_UNUSED(tv))
{
	base->need_to_exit = 1;
	return 0;
}

/* free event base, free events yourself */
void event_base_free(struct event_base* base)
{
	if(!base)
		return;
	free(base->times);
	free(base->fds);
	free(base->signals);
	free(base);
}

/** set content of event */
void event_set(struct event* ev, int fd, short bits, 
	void (*cb)(int, short, void *), void* arg)
{
	ev->node.key = ev;
	ev->ev_fd = fd;
	ev->ev_events = bits;
	ev->ev_callback = cb;
	fptr_ok(fptr_whitelist_event(ev->ev_callback));
	ev->ev_arg = arg;
	ev->added = 0;
}

/* add event to a base */
int event_base_set(struct event_base* base, struct event* ev)
{
	ev->ev_base = base;
	ev->added = 0;
	return 0;
}

/* add event to make it active, you may not change it with event_set anymore */
int event_add(struct event* ev, struct timeval* tv)
{
	if(ev->added)
		event_del(ev);
	if(ev->ev_fd != -1 && ev->ev_fd >= ev->ev_base->capfd)
		return -1;
	if( (ev->ev_events&(EV_READ|EV_WRITE)) && ev->ev_fd != -1) {
		ev->ev_base->fds[ev->ev_fd] = ev;
		if(ev->ev_events&EV_READ) {
			FD_SET(FD_SET_T ev->ev_fd, &ev->ev_base->reads);
		}
		if(ev->ev_events&EV_WRITE) {
			FD_SET(FD_SET_T ev->ev_fd, &ev->ev_base->writes);
		}
		FD_SET(FD_SET_T ev->ev_fd, &ev->ev_base->content);
		FD_CLR(FD_SET_T ev->ev_fd, &ev->ev_base->ready);
		if(ev->ev_fd > ev->ev_base->maxfd)
			ev->ev_base->maxfd = ev->ev_fd;
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

/* remove event, you may change it again */
int event_del(struct event* ev)
{
	if(ev->ev_fd != -1 && ev->ev_fd >= ev->ev_base->capfd)
		return -1;
	if((ev->ev_events&EV_TIMEOUT))
		(void)rbtree_delete(ev->ev_base->times, &ev->node);
	if((ev->ev_events&(EV_READ|EV_WRITE)) && ev->ev_fd != -1) {
		ev->ev_base->fds[ev->ev_fd] = NULL;
		FD_CLR(FD_SET_T ev->ev_fd, &ev->ev_base->reads);
		FD_CLR(FD_SET_T ev->ev_fd, &ev->ev_base->writes);
		FD_CLR(FD_SET_T ev->ev_fd, &ev->ev_base->ready);
		FD_CLR(FD_SET_T ev->ev_fd, &ev->ev_base->content);
		if(ev->ev_fd == ev->ev_base->maxfd) {
                        int i = ev->ev_base->maxfd - 1;
                        for (; i > 3; i--) {
                                if (NULL != ev->ev_base->fds[i]) {
                                        break;
                                }
                        }
                        ev->ev_base->maxfd = i;
                }
	}
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

/** install signal handler */
int signal_add(struct event* ev, struct timeval* ATTR_UNUSED(tv))
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

/** remove signal handler */
int signal_del(struct event* ev)
{
	if(ev->ev_fd == -1 || ev->ev_fd >= MAX_SIG)
		return -1;
	ev->ev_base->signals[ev->ev_fd] = NULL;
	ev->added = 0;
	return 0;
}

#else /* USE_MINI_EVENT */
#ifndef USE_WINSOCK
int mini_ev_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	return 0;
}
#endif /* not USE_WINSOCK */
#endif /* USE_MINI_EVENT */
