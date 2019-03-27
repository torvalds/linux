/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * Copyright (c) 2011 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_PJDLOG_H_
#define	_PJDLOG_H_

#include <sys/cdefs.h>

#include <errno.h>
#include <stdarg.h>
#include <sysexits.h>
#include <syslog.h>

#define	PJDLOG_MODE_STD		0
#define	PJDLOG_MODE_SYSLOG	1
#define	PJDLOG_MODE_SOCK	2

void pjdlog_init(int mode);
void pjdlog_fini(void);

void pjdlog_mode_set(int mode);
int pjdlog_mode_get(void);

#ifdef notyet
void pjdlog_sock_set(int sock);
int pjdlog_sock_get(void);
#endif

void pjdlog_debug_set(int level);
int pjdlog_debug_get(void);

void pjdlog_prefix_set(const char *fmt, ...) __printflike(1, 2);
void pjdlogv_prefix_set(const char *fmt, va_list ap) __printflike(1, 0);
const char *pjdlog_prefix_get(void);
void pjdlog_prefix_push(const char *fmt, ...) __printflike(1, 2);
void pjdlogv_prefix_push(const char *fmt, va_list ap) __printflike(1, 0);
void pjdlog_prefix_pop(void);

void _pjdlogv_common(const char *func, const char *file, int line, int loglevel,
    int debuglevel, int error, const char *fmt, va_list ap) __printflike(7, 0);
void _pjdlog_common(const char *func, const char *file, int line, int loglevel,
    int debuglevel, int error, const char *fmt, ...) __printflike(7, 8);

void _pjdlogv_exit(const char *func, const char *file, int line, int exitcode,
    int error, const char *fmt, va_list ap) __printflike(6, 0) __dead2;
void _pjdlog_exit(const char *func, const char *file, int line, int exitcode,
    int error, const char *fmt, ...) __printflike(6, 7) __dead2;

void _pjdlog_abort(const char *func, const char *file, int line, int error,
    const char *failedexpr, const char *fmt, ...) __printflike(6, 7) __dead2;

#ifdef notyet
int pjdlog_receive(int sock);
#endif

#define	pjdlogv_common(loglevel, debuglevel, error, fmt, ap)		\
	_pjdlogv_common(__func__, __FILE__, __LINE__, (loglevel),	\
	    (debuglevel), (error), (fmt), (ap))
#define	pjdlog_common(loglevel, debuglevel, error, ...)			\
	_pjdlog_common(__func__, __FILE__, __LINE__, (loglevel),	\
	    (debuglevel), (error), __VA_ARGS__)

#define	pjdlogv(loglevel, fmt, ap)					\
	pjdlogv_common((loglevel), 0, -1, (fmt), (ap))
#define	pjdlog(loglevel, ...)						\
	pjdlog_common((loglevel), 0, -1, __VA_ARGS__)

#define	pjdlogv_emergency(fmt, ap)	pjdlogv(LOG_EMERG, (fmt), (ap))
#define	pjdlog_emergency(...)		pjdlog(LOG_EMERG, __VA_ARGS__)
#define	pjdlogv_alert(fmt, ap)		pjdlogv(LOG_ALERT, (fmt), (ap))
#define	pjdlog_alert(...)		pjdlog(LOG_ALERT, __VA_ARGS__)
#define	pjdlogv_critical(fmt, ap)	pjdlogv(LOG_CRIT, (fmt), (ap))
#define	pjdlog_critical(...)		pjdlog(LOG_CRIT, __VA_ARGS__)
#define	pjdlogv_error(fmt, ap)		pjdlogv(LOG_ERR, (fmt), (ap))
#define	pjdlog_error(...)		pjdlog(LOG_ERR, __VA_ARGS__)
#define	pjdlogv_warning(fmt, ap)	pjdlogv(LOG_WARNING, (fmt), (ap))
#define	pjdlog_warning(...)		pjdlog(LOG_WARNING, __VA_ARGS__)
#define	pjdlogv_notice(fmt, ap)		pjdlogv(LOG_NOTICE, (fmt), (ap))
#define	pjdlog_notice(...)		pjdlog(LOG_NOTICE, __VA_ARGS__)
#define	pjdlogv_info(fmt, ap)		pjdlogv(LOG_INFO, (fmt), (ap))
#define	pjdlog_info(...)		pjdlog(LOG_INFO, __VA_ARGS__)

#define	pjdlog_debug(debuglevel, ...)					\
	pjdlog_common(LOG_DEBUG, (debuglevel), -1, __VA_ARGS__)
#define	pjdlogv_debug(debuglevel, fmt, ap)				\
	pjdlogv_common(LOG_DEBUG, (debuglevel), -1, (fmt), (ap))

#define	pjdlog_errno(loglevel, ...)					\
	pjdlog_common((loglevel), 0, (errno), __VA_ARGS__)
#define	pjdlogv_errno(loglevel, fmt, ap)				\
	pjdlogv_common((loglevel), 0, (errno), (fmt), (ap))

#define	pjdlogv_exit(exitcode, fmt, ap)					\
	_pjdlogv_exit(__func__, __FILE__, __LINE__, (exitcode),		\
	    (errno), (fmt), (ap))
#define	pjdlog_exit(exitcode, ...)					\
	_pjdlog_exit(__func__, __FILE__, __LINE__, (exitcode), (errno),	\
	    __VA_ARGS__)

#define	pjdlogv_exitx(exitcode, fmt, ap)				\
	_pjdlogv_exit(__func__, __FILE__, __LINE__, (exitcode), -1,	\
	    (fmt), (ap))
#define	pjdlog_exitx(exitcode, ...)					\
	_pjdlog_exit(__func__, __FILE__, __LINE__, (exitcode), -1,	\
	    __VA_ARGS__)

#define	PJDLOG_VERIFY(expr)	do {					\
	if (!(expr)) {							\
		_pjdlog_abort(__func__, __FILE__, __LINE__, -1, #expr,	\
		    __func__);						\
	}								\
} while (0)
#define	PJDLOG_RVERIFY(expr, ...)	do {				\
	if (!(expr)) {							\
		_pjdlog_abort(__func__, __FILE__, __LINE__, -1, #expr,	\
		    __VA_ARGS__);					\
	}								\
} while (0)
#define	PJDLOG_EVERIFY(expr)		do {				\
	if (!(expr)) {							\
		_pjdlog_abort(__func__, __FILE__, __LINE__, errno,	\
		    #expr, __func__);					\
	}								\
} while (0)
#define	PJDLOG_ABORT(...)	_pjdlog_abort(__func__, __FILE__,	\
				    __LINE__, -1, NULL, __VA_ARGS__)
#ifdef NDEBUG
#define	PJDLOG_ASSERT(expr)	do { } while (0)
#define	PJDLOG_RASSERT(...)	do { } while (0)
#else
#define	PJDLOG_ASSERT(expr)	do {					\
	if (!(expr)) {							\
		_pjdlog_abort(__func__, __FILE__, __LINE__, -1, #expr,	\
		    __func__);						\
	}								\
} while (0)
#define	PJDLOG_RASSERT(expr, ...)	do {				\
	if (!(expr)) {							\
		_pjdlog_abort(__func__, __FILE__, __LINE__, -1, #expr,	\
		    __VA_ARGS__);					\
	}								\
} while (0)
#endif

#endif	/* !_PJDLOG_H_ */
