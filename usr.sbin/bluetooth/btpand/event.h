/*
 * event.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

/*
 * Hack to provide libevent (see devel/libevent port) like API.
 * Should be removed if FreeBSD ever decides to import libevent into base.
 */

#ifndef	_EVENT_H_
#define	_EVENT_H_	1

#define	EV_READ         0x02
#define	EV_WRITE        0x04
#define	EV_PERSIST      0x10		/* Persistent event */
#define	EV_PENDING	(1 << 13)	/* internal use only! */
#define	EV_HAS_TIMEOUT	(1 << 14)	/* internal use only! */
#define	EV_CURRENT	(1 << 15)	/* internal use only! */

struct event
{
	int			fd;
	short			flags;
	void			(*cb)(int, short, void *);
	void			*cbarg;
	struct timeval		timeout;
	struct timeval		expire;

#ifdef	EVENT_DEBUG
	char const		*files[3];
	int			lines[3];
#endif

	TAILQ_ENTRY(event)	next;
};

void	event_init	(void);
int	event_dispatch	(void);

void	__event_set	(struct event *, int, short,
			 void (*)(int, short, void *), void *);
int	__event_add	(struct event *, struct timeval const *);
int	__event_del	(struct event *);

#ifdef	EVENT_DEBUG
#define event_log_err(fmt, args...)	syslog(LOG_ERR, fmt, ##args)
#define event_log_info(fmt, args...)	syslog(LOG_INFO, fmt, ##args)
#define event_log_notice(fmt, args...)	syslog(LOG_NOTICE, fmt, ##args)
#define event_log_debug(fmt, args...)	syslog(LOG_DEBUG, fmt, ##args)

#define	event_set(ev, fd, flags, cb, cbarg) \
	_event_set(__FILE__, __LINE__, ev, fd, flags, cb, cbarg)
#define	event_add(ev, timeout) \
	_event_add(__FILE__, __LINE__, ev, timeout)
#define	event_del(ev) \
	_event_del(__FILE__, __LINE__, ev)

#define	evtimer_set(ev, cb, cbarg) \
	_event_set(__FILE__, __LINE__, ev, -1, 0, cb, cbarg)
#define	evtimer_add(ev, timeout) \
	_event_add(__FILE__, __LINE__, ev, timeout)

static inline void
_event_set(char const *file, int line, struct event *ev, int fd, short flags,
		void (*cb)(int, short, void *), void *cbarg)
{
	event_log_debug("set %s:%d ev=%p, fd=%d, flags=%#x, cb=%p, cbarg=%p",
		file, line, ev, fd, flags, cb, cbarg);

	ev->files[0] = file;
	ev->lines[0] = line;

	__event_set(ev, fd, flags, cb, cbarg);
}

static inline int
_event_add(char const *file, int line, struct event *ev,
		struct timeval const *timeout) {
	event_log_debug("add %s:%d ev=%p, fd=%d, flags=%#x, cb=%p, cbarg=%p, " \
		"timeout=%p", file, line, ev, ev->fd, ev->flags, ev->cb,
		ev->cbarg, timeout);

	ev->files[1] = file;
	ev->lines[1] = line;

	return (__event_add(ev, timeout));
}

static inline int
_event_del(char const *file, int line, struct event *ev)
{
	event_log_debug("del %s:%d ev=%p, fd=%d, flags=%#x, cb=%p, cbarg=%p",
		file, line, ev, ev->fd, ev->flags, ev->cb, ev->cbarg);

	ev->files[2] = file;
	ev->lines[2] = line;

	return (__event_del(ev));
}
#else
#define event_log_err(fmt, args...)
#define event_log_info(fmt, args...)
#define event_log_notice(fmt, args...)
#define event_log_debug(fmt, args...)

#define	event_set(ev, fd, flags, cb, cbarg) \
	__event_set(ev, fd, flags, cb, cbarg)
#define	event_add(ev, timeout) \
	__event_add(ev, timeout)
#define	event_del(ev) \
	__event_del(ev)

#define	evtimer_set(ev, cb, cbarg) \
	__event_set(ev, -1, 0, cb, cbarg)
#define	evtimer_add(ev, timeout) \
	__event_add(ev, timeout)
#endif	/* EVENT_DEBUG */

#endif	/* ndef _EVENT_H_ */
