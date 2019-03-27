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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <libutil.h>
#include <limits.h>
#include <printf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifdef notyet
#include <robustio.h>
#endif

#include "pjdlog.h"

#ifndef	MAX
#define	MAX(a, b)	((a) > (b) ? (a) : (b))
#endif

#define	PJDLOG_MAX_MSGSIZE	4096

#define	PJDLOG_PREFIX_STACK	4
#define	PJDLOG_PREFIX_MAXSIZE	128

#define	PJDLOG_NEVER_INITIALIZED	0
#define	PJDLOG_NOT_INITIALIZED		1
#define	PJDLOG_INITIALIZED		2

static int pjdlog_initialized = PJDLOG_NEVER_INITIALIZED;
static int pjdlog_mode, pjdlog_debug_level, pjdlog_sock;
static int pjdlog_prefix_current;
static char pjdlog_prefix[PJDLOG_PREFIX_STACK][PJDLOG_PREFIX_MAXSIZE];

static int
pjdlog_printf_arginfo_humanized_number(const struct printf_info *pi __unused,
    size_t n, int *argt)
{

	assert(n >= 1);
	argt[0] = PA_INT | PA_FLAG_INTMAX;
	return (1);
}

static int
pjdlog_printf_render_humanized_number(struct __printf_io *io,
    const struct printf_info *pi, const void * const *arg)
{
	char buf[5];
	intmax_t num;
	int ret;

	num = *(const intmax_t *)arg[0];
	humanize_number(buf, sizeof(buf), (int64_t)num, "", HN_AUTOSCALE,
	    HN_NOSPACE | HN_DECIMAL);
	ret = __printf_out(io, pi, buf, strlen(buf));
	__printf_flush(io);
	return (ret);
}

static int
pjdlog_printf_arginfo_sockaddr(const struct printf_info *pi __unused,
    size_t n, int *argt)
{

	assert(n >= 1);
	argt[0] = PA_POINTER;
	return (1);
}

static int
pjdlog_printf_render_sockaddr_ip(struct __printf_io *io,
    const struct printf_info *pi, const void * const *arg)
{
	const struct sockaddr_storage *ss;
	char addr[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
	int ret;

	ss = *(const struct sockaddr_storage * const *)arg[0];
	switch (ss->ss_family) {
	case AF_INET:
	    {
		const struct sockaddr_in *sin;

		sin = (const struct sockaddr_in *)ss;
		if (inet_ntop(ss->ss_family, &sin->sin_addr, addr,
		    sizeof(addr)) == NULL) {
			PJDLOG_ABORT("inet_ntop(AF_INET) failed: %s.",
			    strerror(errno));
		}
		break;
	    }
	case AF_INET6:
	    {
		const struct sockaddr_in6 *sin;

		sin = (const struct sockaddr_in6 *)ss;
		if (inet_ntop(ss->ss_family, &sin->sin6_addr, addr,
		    sizeof(addr)) == NULL) {
			PJDLOG_ABORT("inet_ntop(AF_INET6) failed: %s.",
			    strerror(errno));
		}
		break;
	    }
	default:
		snprintf(addr, sizeof(addr), "[unsupported family %hhu]",
		    ss->ss_family);
		break;
	}
	ret = __printf_out(io, pi, addr, strlen(addr));
	__printf_flush(io);
	return (ret);
}

static int
pjdlog_printf_render_sockaddr(struct __printf_io *io,
    const struct printf_info *pi, const void * const *arg)
{
	const struct sockaddr_storage *ss;
	char buf[PATH_MAX];
	int ret;

	ss = *(const struct sockaddr_storage * const *)arg[0];
	switch (ss->ss_family) {
	case AF_UNIX:
	    {
		const struct sockaddr_un *sun;

		sun = (const struct sockaddr_un *)ss;
		if (sun->sun_path[0] == '\0')
			snprintf(buf, sizeof(buf), "N/A");
		else
			snprintf(buf, sizeof(buf), "%s", sun->sun_path);
		break;
	    }
	case AF_INET:
	    {
		char addr[INET_ADDRSTRLEN];
		const struct sockaddr_in *sin;
		unsigned int port;

		sin = (const struct sockaddr_in *)ss;
		port = ntohs(sin->sin_port);
		if (inet_ntop(ss->ss_family, &sin->sin_addr, addr,
		    sizeof(addr)) == NULL) {
			PJDLOG_ABORT("inet_ntop(AF_INET) failed: %s.",
			    strerror(errno));
		}
		snprintf(buf, sizeof(buf), "%s:%u", addr, port);
		break;
	    }
	case AF_INET6:
	    {
		char addr[INET6_ADDRSTRLEN];
		const struct sockaddr_in6 *sin;
		unsigned int port;

		sin = (const struct sockaddr_in6 *)ss;
		port = ntohs(sin->sin6_port);
		if (inet_ntop(ss->ss_family, &sin->sin6_addr, addr,
		    sizeof(addr)) == NULL) {
			PJDLOG_ABORT("inet_ntop(AF_INET6) failed: %s.",
			    strerror(errno));
		}
		snprintf(buf, sizeof(buf), "[%s]:%u", addr, port);
		break;
	    }
	default:
		snprintf(buf, sizeof(buf), "[unsupported family %hhu]",
		    ss->ss_family);
		break;
	}
	ret = __printf_out(io, pi, buf, strlen(buf));
	__printf_flush(io);
	return (ret);
}

void
pjdlog_init(int mode)
{
	int saved_errno;

	assert(pjdlog_initialized == PJDLOG_NEVER_INITIALIZED ||
	    pjdlog_initialized == PJDLOG_NOT_INITIALIZED);
#ifdef notyet
	assert(mode == PJDLOG_MODE_STD || mode == PJDLOG_MODE_SYSLOG ||
	    mode == PJDLOG_MODE_SOCK);
#else
	assert(mode == PJDLOG_MODE_STD || mode == PJDLOG_MODE_SYSLOG);
#endif

	saved_errno = errno;

	if (pjdlog_initialized == PJDLOG_NEVER_INITIALIZED) {
		__use_xprintf = 1;
		register_printf_render_std("T");
		register_printf_render('N',
		    pjdlog_printf_render_humanized_number,
		    pjdlog_printf_arginfo_humanized_number);
		register_printf_render('I',
		    pjdlog_printf_render_sockaddr_ip,
		    pjdlog_printf_arginfo_sockaddr);
		register_printf_render('S',
		    pjdlog_printf_render_sockaddr,
		    pjdlog_printf_arginfo_sockaddr);
	}

	if (mode == PJDLOG_MODE_SYSLOG)
		openlog(NULL, LOG_PID | LOG_NDELAY, LOG_LOCAL0);
	pjdlog_mode = mode;
	pjdlog_debug_level = 0;
	pjdlog_prefix_current = 0;
	pjdlog_prefix[0][0] = '\0';

	pjdlog_initialized = PJDLOG_INITIALIZED;
	pjdlog_sock = -1;

	errno = saved_errno;
}

void
pjdlog_fini(void)
{
	int saved_errno;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	saved_errno = errno;

	if (pjdlog_mode == PJDLOG_MODE_SYSLOG)
		closelog();

	pjdlog_initialized = PJDLOG_NOT_INITIALIZED;
	pjdlog_sock = -1;

	errno = saved_errno;
}

/*
 * Configure where the logs should go.
 * By default they are send to stdout/stderr, but after going into background
 * (eg. by calling daemon(3)) application is responsible for changing mode to
 * PJDLOG_MODE_SYSLOG, so logs will be send to syslog.
 */
void
pjdlog_mode_set(int mode)
{
	int saved_errno;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
#ifdef notyet
	assert(mode == PJDLOG_MODE_STD || mode == PJDLOG_MODE_SYSLOG ||
	    mode == PJDLOG_MODE_SOCK);
#else
	assert(mode == PJDLOG_MODE_STD || mode == PJDLOG_MODE_SYSLOG);
#endif

	if (pjdlog_mode == mode)
		return;

	saved_errno = errno;

	if (mode == PJDLOG_MODE_SYSLOG)
		openlog(NULL, LOG_PID | LOG_NDELAY, LOG_DAEMON);
	else if (mode == PJDLOG_MODE_STD)
		closelog();

	if (mode != PJDLOG_MODE_SOCK)
		pjdlog_sock = -1;

	pjdlog_mode = mode;

	errno = saved_errno;
}


/*
 * Return current mode.
 */
int
pjdlog_mode_get(void)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	return (pjdlog_mode);
}

#ifdef notyet
/*
 * Sets socket number to use for PJDLOG_MODE_SOCK mode.
 */
void
pjdlog_sock_set(int sock)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(pjdlog_mode == PJDLOG_MODE_SOCK);
	assert(sock >= 0);

	pjdlog_sock = sock;
}
#endif

#ifdef notyet
/*
 * Returns socket number used for PJDLOG_MODE_SOCK mode.
 */
int
pjdlog_sock_get(void)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(pjdlog_mode == PJDLOG_MODE_SOCK);
	assert(pjdlog_sock >= 0);

	return (pjdlog_sock);
}
#endif

/*
 * Set debug level. All the logs above the level specified here will be
 * ignored.
 */
void
pjdlog_debug_set(int level)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(level >= 0);
	assert(level <= 127);

	pjdlog_debug_level = level;
}

/*
 * Return current debug level.
 */
int
pjdlog_debug_get(void)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	return (pjdlog_debug_level);
}

/*
 * Set prefix that will be used before each log.
 */
void
pjdlog_prefix_set(const char *fmt, ...)
{
	va_list ap;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	va_start(ap, fmt);
	pjdlogv_prefix_set(fmt, ap);
	va_end(ap);
}

/*
 * Set prefix that will be used before each log.
 */
void
pjdlogv_prefix_set(const char *fmt, va_list ap)
{
	int saved_errno;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(fmt != NULL);

	saved_errno = errno;

	vsnprintf(pjdlog_prefix[pjdlog_prefix_current],
	    sizeof(pjdlog_prefix[pjdlog_prefix_current]), fmt, ap);

	errno = saved_errno;
}

/*
 * Get current prefix.
 */
const char *
pjdlog_prefix_get(void)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	return (pjdlog_prefix[pjdlog_prefix_current]);
}

/*
 * Set new prefix and put the current one on the stack.
 */
void
pjdlog_prefix_push(const char *fmt, ...)
{
	va_list ap;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	va_start(ap, fmt);
	pjdlogv_prefix_push(fmt, ap);
	va_end(ap);
}

/*
 * Set new prefix and put the current one on the stack.
 */
void
pjdlogv_prefix_push(const char *fmt, va_list ap)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(pjdlog_prefix_current < PJDLOG_PREFIX_STACK - 1);

	pjdlog_prefix_current++;

	pjdlogv_prefix_set(fmt, ap);
}

/*
 * Removes current prefix and recovers previous one from the stack.
 */
void
pjdlog_prefix_pop(void)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(pjdlog_prefix_current > 0);

	pjdlog_prefix_current--;
}

/*
 * Convert log level into string.
 */
static const char *
pjdlog_level_to_string(int loglevel)
{

	switch (loglevel) {
	case LOG_EMERG:
		return ("EMERG");
	case LOG_ALERT:
		return ("ALERT");
	case LOG_CRIT:
		return ("CRIT");
	case LOG_ERR:
		return ("ERROR");
	case LOG_WARNING:
		return ("WARNING");
	case LOG_NOTICE:
		return ("NOTICE");
	case LOG_INFO:
		return ("INFO");
	case LOG_DEBUG:
		return ("DEBUG");
	}
	assert(!"Invalid log level.");
	abort();	/* XXX: gcc */
}

static int
vsnprlcat(char *str, size_t size, const char *fmt, va_list ap)
{
	size_t len;

	len = strlen(str);
	assert(len < size);
	return (vsnprintf(str + len, size - len, fmt, ap));
}

static int
snprlcat(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = vsnprlcat(str, size, fmt, ap);
	va_end(ap);
	return (result);
}

static void
pjdlogv_common_single_line(const char *func, const char *file, int line,
    int loglevel, int debuglevel, int error, const char *msg)
{
	static char log[2 * PJDLOG_MAX_MSGSIZE];
	char *logp;
	size_t logs;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
#ifdef notyet
	assert(pjdlog_mode == PJDLOG_MODE_STD ||
	    pjdlog_mode == PJDLOG_MODE_SYSLOG ||
	    pjdlog_mode == PJDLOG_MODE_SOCK);
#else
	assert(pjdlog_mode == PJDLOG_MODE_STD ||
	    pjdlog_mode == PJDLOG_MODE_SYSLOG);
#endif
	assert(pjdlog_mode != PJDLOG_MODE_SOCK || pjdlog_sock >= 0);
	assert(loglevel == LOG_EMERG || loglevel == LOG_ALERT ||
	    loglevel == LOG_CRIT || loglevel == LOG_ERR ||
	    loglevel == LOG_WARNING || loglevel == LOG_NOTICE ||
	    loglevel == LOG_INFO || loglevel == LOG_DEBUG);
	assert(loglevel != LOG_DEBUG || debuglevel > 0);
	assert(loglevel != LOG_DEBUG || debuglevel <= pjdlog_debug_level);
	assert(debuglevel <= 127);
	assert(error >= -1);
	assert((file != NULL && line > 0) ||
	    (func == NULL && file == NULL && line == 0));

	switch (pjdlog_mode) {
	case PJDLOG_MODE_STD:
	case PJDLOG_MODE_SYSLOG:
		logp = log;
		logs = sizeof(log);
		break;
	case PJDLOG_MODE_SOCK:
		logp = log + 4;
		logs = sizeof(log) - 4;
		break;
	default:
		assert(!"Invalid mode.");
	}

	*logp = '\0';

	if (pjdlog_mode != PJDLOG_MODE_SOCK) {
		if (loglevel == LOG_DEBUG) {
			/* Attach debuglevel if this is debug log. */
			snprlcat(logp, logs, "[%s%d] ",
			    pjdlog_level_to_string(loglevel), debuglevel);
		} else {
			snprlcat(logp, logs, "[%s] ",
			    pjdlog_level_to_string(loglevel));
		}
		if (pjdlog_mode != PJDLOG_MODE_SYSLOG &&
		    pjdlog_debug_level >= 1) {
			snprlcat(logp, logs, "(pid=%d) ", getpid());
		}
	}
	/* Attach file, func, line if debuglevel is 2 or more. */
	if (pjdlog_debug_level >= 2 && file != NULL) {
		if (func == NULL)
			snprlcat(logp, logs, "(%s:%d) ", file, line);
		else
			snprlcat(logp, logs, "(%s:%d:%s) ", file, line, func);
	}

	if (pjdlog_mode != PJDLOG_MODE_SOCK) {
		snprlcat(logp, logs, "%s",
		    pjdlog_prefix[pjdlog_prefix_current]);
	}

	strlcat(logp, msg, logs);

	/* Attach error description. */
	if (error != -1)
		snprlcat(logp, logs, ": %s.", strerror(error));

	switch (pjdlog_mode) {
	case PJDLOG_MODE_STD:
		fprintf(stderr, "%s\n", logp);
		fflush(stderr);
		break;
	case PJDLOG_MODE_SYSLOG:
		syslog(loglevel, "%s", logp);
		break;
#ifdef notyet
	case PJDLOG_MODE_SOCK:
	    {
		char ack[2];
		uint16_t dlen;

		log[2] = loglevel;
		log[3] = debuglevel;
		dlen = strlen(logp) + 3;	/* +3 = loglevel, debuglevel and terminating \0 */
		bcopy(&dlen, log, sizeof(dlen));
		if (robust_send(pjdlog_sock, log, (size_t)dlen + 2) == -1)	/* +2 for size */
			assert(!"Unable to send log.");
		if (robust_recv(pjdlog_sock, ack, sizeof(ack)) == -1)
			assert(!"Unable to send log.");
		break;
	    }
#endif
	default:
		assert(!"Invalid mode.");
	}
}

/*
 * Common log routine, which can handle regular log level as well as debug
 * level. We decide here where to send the logs (stdout/stderr or syslog).
 */
void
_pjdlogv_common(const char *func, const char *file, int line, int loglevel,
    int debuglevel, int error, const char *fmt, va_list ap)
{
	char log[PJDLOG_MAX_MSGSIZE];
	char *logp, *curline;
	const char *prvline;
	int saved_errno;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);
	assert(pjdlog_mode == PJDLOG_MODE_STD ||
	    pjdlog_mode == PJDLOG_MODE_SYSLOG ||
	    pjdlog_mode == PJDLOG_MODE_SOCK);
	assert(pjdlog_mode != PJDLOG_MODE_SOCK || pjdlog_sock >= 0);
	assert(loglevel == LOG_EMERG || loglevel == LOG_ALERT ||
	    loglevel == LOG_CRIT || loglevel == LOG_ERR ||
	    loglevel == LOG_WARNING || loglevel == LOG_NOTICE ||
	    loglevel == LOG_INFO || loglevel == LOG_DEBUG);
	assert(loglevel != LOG_DEBUG || debuglevel > 0);
	assert(debuglevel <= 127);
	assert(error >= -1);

	/* Ignore debug above configured level. */
	if (loglevel == LOG_DEBUG && debuglevel > pjdlog_debug_level)
		return;

	saved_errno = errno;

	vsnprintf(log, sizeof(log), fmt, ap);
	logp = log;
	prvline = NULL;

	while ((curline = strsep(&logp, "\n")) != NULL) {
		if (*curline == '\0')
			continue;
		if (prvline != NULL) {
			pjdlogv_common_single_line(func, file, line, loglevel,
			    debuglevel, -1, prvline);
		}
		prvline = curline;
	}
	if (prvline == NULL)
		prvline = "";
	pjdlogv_common_single_line(func, file, line, loglevel, debuglevel,
	    error, prvline);

	errno = saved_errno;
}

/*
 * Common log routine.
 */
void
_pjdlog_common(const char *func, const char *file, int line, int loglevel,
    int debuglevel, int error, const char *fmt, ...)
{
	va_list ap;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	va_start(ap, fmt);
	_pjdlogv_common(func, file, line, loglevel, debuglevel, error, fmt, ap);
	va_end(ap);
}

/*
 * Log error, errno and exit.
 */
void
_pjdlogv_exit(const char *func, const char *file, int line, int exitcode,
    int error, const char *fmt, va_list ap)
{

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	_pjdlogv_common(func, file, line, exitcode == 0 ? LOG_INFO : LOG_ERR, 0,
	    error, fmt, ap);
	exit(exitcode);
	/* NOTREACHED */
}

/*
 * Log error, errno and exit.
 */
void
_pjdlog_exit(const char *func, const char *file, int line, int exitcode,
    int error, const char *fmt, ...)
{
	va_list ap;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	va_start(ap, fmt);
	_pjdlogv_exit(func, file, line, exitcode, error, fmt, ap);
	/* NOTREACHED */
	va_end(ap);
}

/*
 * Log failure message and exit.
 */
void
_pjdlog_abort(const char *func, const char *file, int line,
    int error, const char *failedexpr, const char *fmt, ...)
{
	va_list ap;

	assert(pjdlog_initialized == PJDLOG_INITIALIZED);

	/*
	 * Set pjdlog_debug_level to 2, so that file, line and func are
	 * included in log. This is fine as we will exit anyway.
	 */
	if (pjdlog_debug_level < 2)
		pjdlog_debug_level = 2;

	/*
	 * When there is no message we pass __func__ as 'fmt'.
	 * It would be cleaner to pass NULL or "", but gcc generates a warning
	 * for both of those.
	 */
	if (fmt != func) {
		va_start(ap, fmt);
		_pjdlogv_common(func, file, line, LOG_CRIT, 0, -1, fmt, ap);
		va_end(ap);
	}
	if (failedexpr == NULL) {
		_pjdlog_common(func, file, line, LOG_CRIT, 0, -1, "Aborted.");
	} else {
		_pjdlog_common(func, file, line, LOG_CRIT, 0, -1,
		    "Assertion failed: (%s).", failedexpr);
	}
	if (error != -1)
		_pjdlog_common(func, file, line, LOG_CRIT, 0, error, "Errno");
	abort();
}

#ifdef notyet
/*
 * Receive log from the given socket.
 */
int
pjdlog_receive(int sock)
{
	char log[PJDLOG_MAX_MSGSIZE];
	int loglevel, debuglevel;
	uint16_t dlen;

	if (robust_recv(sock, &dlen, sizeof(dlen)) == -1)
		return (-1);

	PJDLOG_ASSERT(dlen > 0);
	PJDLOG_ASSERT(dlen <= PJDLOG_MAX_MSGSIZE - 3);

	if (robust_recv(sock, log, (size_t)dlen) == -1)
		return (-1);

	log[dlen - 1] = '\0';
	loglevel = log[0];
	debuglevel = log[1];
	_pjdlog_common(NULL, NULL, 0, loglevel, debuglevel, -1, "%s", log + 2);

	if (robust_send(sock, "ok", 2) == -1)
		return (-1);

	return (0);
}
#endif
