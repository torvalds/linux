/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
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

#ifndef _THREAD_DB_INT_H_
#define	_THREAD_DB_INT_H_

#include <sys/types.h>
#include <sys/queue.h>

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
} td_old_thrinfo_t;

#define	TD_THRAGENT_FIELDS			\
	struct ta_ops		*ta_ops;	\
	TAILQ_ENTRY(td_thragent) ta_next;	\
	struct ps_prochandle	*ph

struct ta_ops {
	td_err_e (*to_init)(void);

	td_err_e (*to_ta_clear_event)(const td_thragent_t *,
	    td_thr_events_t *);
	td_err_e (*to_ta_delete)(td_thragent_t *);
	td_err_e (*to_ta_event_addr)(const td_thragent_t *, td_thr_events_e,
	    td_notify_t *);
	td_err_e (*to_ta_event_getmsg)(const td_thragent_t *,
	    td_event_msg_t *);
	td_err_e (*to_ta_map_id2thr)(const td_thragent_t *, thread_t,
	    td_thrhandle_t *);
	td_err_e (*to_ta_map_lwp2thr)(const td_thragent_t *, lwpid_t,
	    td_thrhandle_t *);
	td_err_e (*to_ta_new)(struct ps_prochandle *, td_thragent_t **);
	td_err_e (*to_ta_set_event)(const td_thragent_t *, td_thr_events_t *);
	td_err_e (*to_ta_thr_iter)(const td_thragent_t *, td_thr_iter_f *,
	    void *, td_thr_state_e, int, sigset_t *, unsigned int);
	td_err_e (*to_ta_tsd_iter)(const td_thragent_t *, td_key_iter_f *,
	    void *);

	td_err_e (*to_thr_clear_event)(const td_thrhandle_t *,
	    td_thr_events_t *);
	td_err_e (*to_thr_dbresume)(const td_thrhandle_t *);
	td_err_e (*to_thr_dbsuspend)(const td_thrhandle_t *);
	td_err_e (*to_thr_event_enable)(const td_thrhandle_t *, int);
	td_err_e (*to_thr_event_getmsg)(const td_thrhandle_t *,
	    td_event_msg_t *);
	td_err_e (*to_thr_old_get_info)(const td_thrhandle_t *,
	    td_old_thrinfo_t *);
	td_err_e (*to_thr_get_info)(const td_thrhandle_t *, td_thrinfo_t *);
	td_err_e (*to_thr_getfpregs)(const td_thrhandle_t *, prfpregset_t *);
	td_err_e (*to_thr_getgregs)(const td_thrhandle_t *, prgregset_t);
	td_err_e (*to_thr_set_event)(const td_thrhandle_t *,
	    td_thr_events_t *);
	td_err_e (*to_thr_setfpregs)(const td_thrhandle_t *,
	    const prfpregset_t *);
	td_err_e (*to_thr_setgregs)(const td_thrhandle_t *, const prgregset_t);
	td_err_e (*to_thr_validate)(const td_thrhandle_t *);
	td_err_e (*to_thr_tls_get_addr)(const td_thrhandle_t *, psaddr_t,
	    size_t, psaddr_t *);

	/* FreeBSD specific extensions. */
	td_err_e (*to_thr_sstep)(const td_thrhandle_t *, int);
#if defined(__i386__)
	td_err_e (*to_thr_getxmmregs)(const td_thrhandle_t *, char *);
	td_err_e (*to_thr_setxmmregs)(const td_thrhandle_t *, const char *);
#endif
};

#ifdef TD_DEBUG
#define TDBG(...) ps_plog(__VA_ARGS__)
#define TDBG_FUNC() ps_plog("%s\n", __func__)
#else
#define TDBG(...)
#define TDBG_FUNC()
#endif

struct td_thragent;

int thr_pread_int(const struct td_thragent *, psaddr_t, uint32_t *);
int thr_pread_long(const struct td_thragent *, psaddr_t, uint64_t *);
int thr_pread_ptr(const struct td_thragent *, psaddr_t, psaddr_t *);

int thr_pwrite_int(const struct td_thragent *, psaddr_t, uint32_t);
int thr_pwrite_long(const struct td_thragent *, psaddr_t, uint64_t);
int thr_pwrite_ptr(const struct td_thragent *, psaddr_t, psaddr_t);

td_err_e td_thr_old_get_info(const td_thrhandle_t *th, td_old_thrinfo_t *info);

#endif /* _THREAD_DB_INT_H_ */
