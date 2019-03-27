/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

#ifndef _THREAD_DB_H_
#define	_THREAD_DB_H_

#include <sys/procfs.h>
#include <pthread.h>

typedef enum {
	TD_ERR = -1,		/* Unspecified error. */
	TD_OK = 0,		/* No error. */
	TD_BADKEY,
	TD_BADPH,
	TD_BADSH,
	TD_BADTA,
	TD_BADTH,
	TD_DBERR,
	TD_MALLOC,
	TD_NOAPLIC,
	TD_NOCAPAB,
	TD_NOEVENT,
	TD_NOFPREGS,
	TD_NOLIBTHREAD,
	TD_NOLWP,
	TD_NOMSG,
	TD_NOSV,
	TD_NOTHR,
	TD_NOTSD,
	TD_NOXREGS,
	TD_PARTIALREG
} td_err_e;

struct ps_prochandle;
typedef struct td_thragent td_thragent_t;
typedef long thread_t;			/* Must be an integral type. */

typedef struct {
	const td_thragent_t *th_ta;
	psaddr_t	th_thread;
	thread_t	th_tid;
} td_thrhandle_t;			/* Used non-opaguely. */

/*
 * Events.
 */

typedef enum {
	TD_EVENT_NONE = 0,
	TD_CATCHSIG =	0x0001,
	TD_CONCURRENCY=	0x0002,
	TD_CREATE =	0x0004,
	TD_DEATH =	0x0008,
	TD_IDLE =	0x0010,
	TD_LOCK_TRY =	0x0020,
	TD_PREEMPT =	0x0040,
	TD_PRI_INHERIT=	0x0080,
	TD_READY =	0x0100,
	TD_REAP =	0x0200,
	TD_SLEEP =	0x0400,
	TD_SWITCHFROM =	0x0800,
	TD_SWITCHTO =	0x1000,
	TD_TIMEOUT =	0x2000,
	TD_ALL_EVENTS = ~0
} td_thr_events_e;

/* Compatibility with Linux. */
#define	td_event_e	td_thr_events_e

typedef struct {
	td_thr_events_e	event;
	psaddr_t	th_p;
	uintptr_t	data;
} td_event_msg_t;

typedef unsigned int td_thr_events_t;

typedef enum {
	NOTIFY_BPT,		/* User inserted breakpoint. */
	NOTIFY_AUTOBPT,		/* Automatic breakpoint. */
	NOTIFY_SYSCALL		/* Invocation of system call. */
} td_notify_e;

typedef struct {
	td_notify_e	type;
	union {
		psaddr_t bptaddr;
		int syscallno;
	} u;
} td_notify_t;

static __inline void
td_event_addset(td_thr_events_t *es, td_thr_events_e e)
{
	*es |= e;
}

static __inline void
td_event_delset(td_thr_events_t *es, td_thr_events_e e)
{
	*es &= ~e;
}

static __inline void
td_event_emptyset(td_thr_events_t *es)
{
	*es = TD_EVENT_NONE;
}

static __inline void
td_event_fillset(td_thr_events_t *es)
{
	*es = TD_ALL_EVENTS;
}

static __inline int
td_eventisempty(td_thr_events_t *es)
{
	return ((*es == TD_EVENT_NONE) ? 1 : 0);
}

static __inline int
td_eventismember(td_thr_events_t *es, td_thr_events_e e)
{
	return ((*es & e) ? 1 : 0);
}

/*
 * Thread info.
 */

typedef enum {
	TD_THR_UNKNOWN = -1,
	TD_THR_ANY_STATE = 0,
	TD_THR_ACTIVE,
	TD_THR_RUN,
	TD_THR_SLEEP,
	TD_THR_STOPPED,
	TD_THR_STOPPED_ASLEEP,
	TD_THR_ZOMBIE
} td_thr_state_e;

typedef enum
{
	TD_THR_SYSTEM = 1,
	TD_THR_USER
} td_thr_type_e;

typedef pthread_key_t thread_key_t;

typedef struct {
	const td_thragent_t *ti_ta_p;
	thread_t	ti_tid;
	psaddr_t	ti_thread;
	td_thr_state_e	ti_state;
	td_thr_type_e	ti_type;
	td_thr_events_t	ti_events;
	int		ti_pri;
	lwpid_t		ti_lid;
	char		ti_db_suspended;
	char		ti_traceme;
	sigset_t	ti_sigmask;
	sigset_t	ti_pending;
	psaddr_t	ti_tls;
	psaddr_t	ti_startfunc;
	psaddr_t	ti_stkbase;
	size_t		ti_stksize;
	siginfo_t	ti_siginfo;
} td_thrinfo_t;

/*
 * Prototypes.
 */

typedef int td_key_iter_f(thread_key_t, void (*)(void *), void *);
typedef int td_thr_iter_f(const td_thrhandle_t *, void *);

/* Flags for `td_ta_thr_iter'. */
#define	TD_THR_ANY_USER_FLAGS	0xffffffff
#define	TD_THR_LOWEST_PRIORITY	-20
#define	TD_SIGNO_MASK		NULL

__BEGIN_DECLS
td_err_e td_init(void);

td_err_e td_ta_clear_event(const td_thragent_t *, td_thr_events_t *);
td_err_e td_ta_delete(td_thragent_t *);
td_err_e td_ta_event_addr(const td_thragent_t *, td_thr_events_e,
    td_notify_t *);
td_err_e td_ta_event_getmsg(const td_thragent_t *, td_event_msg_t *);
td_err_e td_ta_map_id2thr(const td_thragent_t *, thread_t, td_thrhandle_t *);
td_err_e td_ta_map_lwp2thr(const td_thragent_t *, lwpid_t, td_thrhandle_t *);
td_err_e td_ta_new(struct ps_prochandle *, td_thragent_t **);
td_err_e td_ta_set_event(const td_thragent_t *, td_thr_events_t *);
td_err_e td_ta_thr_iter(const td_thragent_t *, td_thr_iter_f *, void *,
    td_thr_state_e, int, sigset_t *, unsigned int);
td_err_e td_ta_tsd_iter(const td_thragent_t *, td_key_iter_f *, void *);

td_err_e td_thr_clear_event(const td_thrhandle_t *, td_thr_events_t *);
td_err_e td_thr_dbresume(const td_thrhandle_t *);
td_err_e td_thr_dbsuspend(const td_thrhandle_t *);
td_err_e td_thr_event_enable(const td_thrhandle_t *, int);
td_err_e td_thr_event_getmsg(const td_thrhandle_t *, td_event_msg_t *);
td_err_e td_thr_get_info(const td_thrhandle_t *, td_thrinfo_t *);
#ifdef __i386__
td_err_e td_thr_getxmmregs(const td_thrhandle_t *, char *);
#endif
td_err_e td_thr_getfpregs(const td_thrhandle_t *, prfpregset_t *);
td_err_e td_thr_getgregs(const td_thrhandle_t *, prgregset_t);
td_err_e td_thr_set_event(const td_thrhandle_t *, td_thr_events_t *);
#ifdef __i386__
td_err_e td_thr_setxmmregs(const td_thrhandle_t *, const char *);
#endif
td_err_e td_thr_setfpregs(const td_thrhandle_t *, const prfpregset_t *);
td_err_e td_thr_setgregs(const td_thrhandle_t *, const prgregset_t);
td_err_e td_thr_validate(const td_thrhandle_t *);
td_err_e td_thr_tls_get_addr(const td_thrhandle_t *, psaddr_t, size_t,
    psaddr_t *);

/* FreeBSD specific extensions. */
td_err_e td_thr_sstep(const td_thrhandle_t *, int);
__END_DECLS

#endif /* _THREAD_DB_H_ */
