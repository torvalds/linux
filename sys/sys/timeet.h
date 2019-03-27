/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2013 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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

#ifndef _SYS_TIMEEC_H_
#define	_SYS_TIMEEC_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/time.h>

/*
 * `struct eventtimer' is the interface between the hardware which implements
 * a event timer and the MI code which uses this to receive time events.
 */

struct eventtimer;
typedef int et_start_t(struct eventtimer *et,
    sbintime_t first, sbintime_t period);
typedef int et_stop_t(struct eventtimer *et);
typedef void et_event_cb_t(struct eventtimer *et, void *arg);
typedef int et_deregister_cb_t(struct eventtimer *et, void *arg);

struct eventtimer {
	SLIST_ENTRY(eventtimer)	et_all;
		/* Pointer to the next event timer. */
	const char		*et_name;
		/* Name of the event timer. */
	int			et_flags;
		/* Set of capabilities flags: */
#define ET_FLAGS_PERIODIC	1
#define ET_FLAGS_ONESHOT	2
#define ET_FLAGS_PERCPU		4
#define ET_FLAGS_C3STOP		8
#define ET_FLAGS_POW2DIV	16
	int			et_quality;
		/*
		 * Used to determine if this timecounter is better than
		 * another timecounter. Higher means better.
		 */
	int			et_active;
	u_int64_t		et_frequency;
		/* Base frequency in Hz. */
	sbintime_t		et_min_period;
	sbintime_t		et_max_period;
	et_start_t		*et_start;
	et_stop_t		*et_stop;
	et_event_cb_t		*et_event_cb;
	et_deregister_cb_t	*et_deregister_cb;
	void 			*et_arg;
	void			*et_priv;
	struct sysctl_oid	*et_sysctl;
		/* Pointer to the event timer's private parts. */
};

extern struct mtx	et_eventtimers_mtx;
#define	ET_LOCK()	mtx_lock(&et_eventtimers_mtx)
#define	ET_UNLOCK()	mtx_unlock(&et_eventtimers_mtx)

/* Driver API */
int	et_register(struct eventtimer *et);
int	et_deregister(struct eventtimer *et);
void	et_change_frequency(struct eventtimer *et, uint64_t newfreq);
/* Consumer API  */
struct eventtimer *et_find(const char *name, int check, int want);
int	et_init(struct eventtimer *et, et_event_cb_t *event,
    et_deregister_cb_t *deregister, void *arg);
int	et_start(struct eventtimer *et, sbintime_t first, sbintime_t period);
int	et_stop(struct eventtimer *et);
int	et_ban(struct eventtimer *et);
int	et_free(struct eventtimer *et);

#ifdef SYSCTL_DECL
SYSCTL_DECL(_kern_eventtimer);
#endif
#endif /* !_SYS_TIMETC_H_ */

