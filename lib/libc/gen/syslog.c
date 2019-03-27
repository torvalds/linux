/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__SCCSID("@(#)syslog.c	8.5 (Berkeley) 4/29/95");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <stdarg.h>
#include "un-namespace.h"

#include "libc_private.h"

static int	LogFile = -1;		/* fd for log */
static int	status;			/* connection status */
static int	opened;			/* have done openlog() */
static int	LogStat = 0;		/* status bits, set by openlog() */
static const char *LogTag = NULL;	/* string to tag the entry with */
static int	LogFacility = LOG_USER;	/* default facility code */
static int	LogMask = 0xff;		/* mask of priorities to be logged */
static pthread_mutex_t	syslog_mutex = PTHREAD_MUTEX_INITIALIZER;

#define	THREAD_LOCK()							\
	do { 								\
		if (__isthreaded) _pthread_mutex_lock(&syslog_mutex);	\
	} while(0)
#define	THREAD_UNLOCK()							\
	do {								\
		if (__isthreaded) _pthread_mutex_unlock(&syslog_mutex);	\
	} while(0)

static void	disconnectlog(void); /* disconnect from syslogd */
static void	connectlog(void);	/* (re)connect to syslogd */
static void	openlog_unlocked(const char *, int, int);

enum {
	NOCONN = 0,
	CONNDEF,
	CONNPRIV,
};

/*
 * Format of the magic cookie passed through the stdio hook
 */
struct bufcookie {
	char	*base;	/* start of buffer */
	int	left;
};

/*
 * stdio write hook for writing to a static string buffer
 * XXX: Maybe one day, dynamically allocate it so that the line length
 *      is `unlimited'.
 */
static int
writehook(void *cookie, const char *buf, int len)
{
	struct bufcookie *h;	/* private `handle' */

	h = (struct bufcookie *)cookie;
	if (len > h->left) {
		/* clip in case of wraparound */
		len = h->left;
	}
	if (len > 0) {
		(void)memcpy(h->base, buf, len); /* `write' it. */
		h->base += len;
		h->left -= len;
	}
	return len;
}

/*
 * syslog, vsyslog --
 *	print message on log file; output is intended for syslogd(8).
 */
void
syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}

static void
vsyslog1(int pri, const char *fmt, va_list ap)
{
	struct timeval now;
	struct tm tm;
	char ch, *p;
	long tz_offset;
	int cnt, fd, saved_errno;
	char hostname[MAXHOSTNAMELEN], *stdp, tbuf[2048], fmt_cpy[1024],
	    errstr[64], tz_sign;
	FILE *fp, *fmt_fp;
	struct bufcookie tbuf_cookie;
	struct bufcookie fmt_cookie;

#define	INTERNALLOG	LOG_ERR|LOG_CONS|LOG_PERROR|LOG_PID
	/* Check for invalid bits. */
	if (pri & ~(LOG_PRIMASK|LOG_FACMASK)) {
		syslog(INTERNALLOG,
		    "syslog: unknown facility/priority: %x", pri);
		pri &= LOG_PRIMASK|LOG_FACMASK;
	}

	saved_errno = errno;

	/* Check priority against setlogmask values. */
	if (!(LOG_MASK(LOG_PRI(pri)) & LogMask))
		return;

	/* Set default facility if none specified. */
	if ((pri & LOG_FACMASK) == 0)
		pri |= LogFacility;

	/* Create the primary stdio hook */
	tbuf_cookie.base = tbuf;
	tbuf_cookie.left = sizeof(tbuf);
	fp = fwopen(&tbuf_cookie, writehook);
	if (fp == NULL)
		return;

	/* Build the message according to RFC 5424. Tag and version. */
	(void)fprintf(fp, "<%d>1 ", pri);
	/* Timestamp similar to RFC 3339. */
	if (gettimeofday(&now, NULL) == 0 &&
	    localtime_r(&now.tv_sec, &tm) != NULL) {
		if (tm.tm_gmtoff < 0) {
			tz_sign = '-';
			tz_offset = -tm.tm_gmtoff;
		} else {
			tz_sign = '+';
			tz_offset = tm.tm_gmtoff;
		}

		(void)fprintf(fp,
		    "%04d-%02d-%02d"		/* Date. */
		    "T%02d:%02d:%02d.%06ld"	/* Time. */
		    "%c%02ld:%02ld ",		/* Time zone offset. */
		    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		    tm.tm_hour, tm.tm_min, tm.tm_sec, now.tv_usec,
		    tz_sign, tz_offset / 3600, (tz_offset % 3600) / 60);
	} else
		(void)fprintf(fp, "- ");
	/* Hostname. */
	(void)gethostname(hostname, sizeof(hostname));
	(void)fprintf(fp, "%s ", hostname);
	if (LogStat & LOG_PERROR) {
		/* Transfer to string buffer */
		(void)fflush(fp);
		stdp = tbuf + (sizeof(tbuf) - tbuf_cookie.left);
	}
	/*
	 * Application name, process ID, message ID and structured data.
	 * Provide the process ID regardless of whether LOG_PID has been
	 * specified, as it provides valuable information. Many
	 * applications tend not to use this, even though they should.
	 */
	if (LogTag == NULL)
		LogTag = _getprogname();
	(void)fprintf(fp, "%s %d - - ",
	    LogTag == NULL ? "-" : LogTag, getpid());

	/* Check to see if we can skip expanding the %m */
	if (strstr(fmt, "%m")) {

		/* Create the second stdio hook */
		fmt_cookie.base = fmt_cpy;
		fmt_cookie.left = sizeof(fmt_cpy) - 1;
		fmt_fp = fwopen(&fmt_cookie, writehook);
		if (fmt_fp == NULL) {
			fclose(fp);
			return;
		}

		/*
		 * Substitute error message for %m.  Be careful not to
		 * molest an escaped percent "%%m".  We want to pass it
		 * on untouched as the format is later parsed by vfprintf.
		 */
		for ( ; (ch = *fmt); ++fmt) {
			if (ch == '%' && fmt[1] == 'm') {
				++fmt;
				strerror_r(saved_errno, errstr, sizeof(errstr));
				fputs(errstr, fmt_fp);
			} else if (ch == '%' && fmt[1] == '%') {
				++fmt;
				fputc(ch, fmt_fp);
				fputc(ch, fmt_fp);
			} else {
				fputc(ch, fmt_fp);
			}
		}

		/* Null terminate if room */
		fputc(0, fmt_fp);
		fclose(fmt_fp);

		/* Guarantee null termination */
		fmt_cpy[sizeof(fmt_cpy) - 1] = '\0';

		fmt = fmt_cpy;
	}

	(void)vfprintf(fp, fmt, ap);
	(void)fclose(fp);

	cnt = sizeof(tbuf) - tbuf_cookie.left;

	/* Remove a trailing newline */
	if (tbuf[cnt - 1] == '\n')
		cnt--;

	/* Output to stderr if requested. */
	if (LogStat & LOG_PERROR) {
		struct iovec iov[2];
		struct iovec *v = iov;

		v->iov_base = stdp;
		v->iov_len = cnt - (stdp - tbuf);
		++v;
		v->iov_base = "\n";
		v->iov_len = 1;
		(void)_writev(STDERR_FILENO, iov, 2);
	}

	/* Get connected, output the message to the local logger. */
	if (!opened)
		openlog_unlocked(LogTag, LogStat | LOG_NDELAY, 0);
	connectlog();

	/*
	 * If the send() fails, there are two likely scenarios: 
	 *  1) syslogd was restarted
	 *  2) /var/run/log is out of socket buffer space, which
	 *     in most cases means local DoS.
	 * If the error does not indicate a full buffer, we address
	 * case #1 by attempting to reconnect to /var/run/log[priv]
	 * and resending the message once.
	 *
	 * If we are working with a privileged socket, the retry
	 * attempts end there, because we don't want to freeze a
	 * critical application like su(1) or sshd(8).
	 *
	 * Otherwise, we address case #2 by repeatedly retrying the
	 * send() to give syslogd a chance to empty its socket buffer.
	 */

	if (send(LogFile, tbuf, cnt, 0) < 0) {
		if (errno != ENOBUFS) {
			/*
			 * Scenario 1: syslogd was restarted
			 * reconnect and resend once
			 */
			disconnectlog();
			connectlog();
			if (send(LogFile, tbuf, cnt, 0) >= 0)
				return;
			/*
			 * if the resend failed, fall through to
			 * possible scenario 2
			 */
		}
		while (errno == ENOBUFS) {
			/*
			 * Scenario 2: out of socket buffer space
			 * possible DoS, fail fast on a privileged
			 * socket
			 */
			if (status == CONNPRIV)
				break;
			_usleep(1);
			if (send(LogFile, tbuf, cnt, 0) >= 0)
				return;
		}
	} else
		return;

	/*
	 * Output the message to the console; try not to block
	 * as a blocking console should not stop other processes.
	 * Make sure the error reported is the one from the syslogd failure.
	 */
	if (LogStat & LOG_CONS &&
	    (fd = _open(_PATH_CONSOLE, O_WRONLY|O_NONBLOCK|O_CLOEXEC, 0)) >=
	    0) {
		struct iovec iov[2];
		struct iovec *v = iov;

		p = strchr(tbuf, '>') + 3;
		v->iov_base = p;
		v->iov_len = cnt - (p - tbuf);
		++v;
		v->iov_base = "\r\n";
		v->iov_len = 2;
		(void)_writev(fd, iov, 2);
		(void)_close(fd);
	}
}

static void
syslog_cancel_cleanup(void *arg __unused)
{

	THREAD_UNLOCK();
}

void
vsyslog(int pri, const char *fmt, va_list ap)
{

	THREAD_LOCK();
	pthread_cleanup_push(syslog_cancel_cleanup, NULL);
	vsyslog1(pri, fmt, ap);
	pthread_cleanup_pop(1);
}

/* Should be called with mutex acquired */
static void
disconnectlog(void)
{
	/*
	 * If the user closed the FD and opened another in the same slot,
	 * that's their problem.  They should close it before calling on
	 * system services.
	 */
	if (LogFile != -1) {
		_close(LogFile);
		LogFile = -1;
	}
	status = NOCONN;			/* retry connect */
}

/* Should be called with mutex acquired */
static void
connectlog(void)
{
	struct sockaddr_un SyslogAddr;	/* AF_UNIX address of local logger */

	if (LogFile == -1) {
		if ((LogFile = _socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC,
		    0)) == -1)
			return;
	}
	if (LogFile != -1 && status == NOCONN) {
		SyslogAddr.sun_len = sizeof(SyslogAddr);
		SyslogAddr.sun_family = AF_UNIX;

		/*
		 * First try privileged socket. If no success,
		 * then try default socket.
		 */
		(void)strncpy(SyslogAddr.sun_path, _PATH_LOG_PRIV,
		    sizeof SyslogAddr.sun_path);
		if (_connect(LogFile, (struct sockaddr *)&SyslogAddr,
		    sizeof(SyslogAddr)) != -1)
			status = CONNPRIV;

		if (status == NOCONN) {
			(void)strncpy(SyslogAddr.sun_path, _PATH_LOG,
			    sizeof SyslogAddr.sun_path);
			if (_connect(LogFile, (struct sockaddr *)&SyslogAddr,
			    sizeof(SyslogAddr)) != -1)
				status = CONNDEF;
		}

		if (status == NOCONN) {
			/*
			 * Try the old "/dev/log" path, for backward
			 * compatibility.
			 */
			(void)strncpy(SyslogAddr.sun_path, _PATH_OLDLOG,
			    sizeof SyslogAddr.sun_path);
			if (_connect(LogFile, (struct sockaddr *)&SyslogAddr,
			    sizeof(SyslogAddr)) != -1)
				status = CONNDEF;
		}

		if (status == NOCONN) {
			(void)_close(LogFile);
			LogFile = -1;
		}
	}
}

static void
openlog_unlocked(const char *ident, int logstat, int logfac)
{
	if (ident != NULL)
		LogTag = ident;
	LogStat = logstat;
	if (logfac != 0 && (logfac &~ LOG_FACMASK) == 0)
		LogFacility = logfac;

	if (LogStat & LOG_NDELAY)	/* open immediately */
		connectlog();

	opened = 1;	/* ident and facility has been set */
}

void
openlog(const char *ident, int logstat, int logfac)
{

	THREAD_LOCK();
	pthread_cleanup_push(syslog_cancel_cleanup, NULL);
	openlog_unlocked(ident, logstat, logfac);
	pthread_cleanup_pop(1);
}


void
closelog(void)
{
	THREAD_LOCK();
	if (LogFile != -1) {
		(void)_close(LogFile);
		LogFile = -1;
	}
	LogTag = NULL;
	status = NOCONN;
	THREAD_UNLOCK();
}

/* setlogmask -- set the log mask level */
int
setlogmask(int pmask)
{
	int omask;

	THREAD_LOCK();
	omask = LogMask;
	if (pmask != 0)
		LogMask = pmask;
	THREAD_UNLOCK();
	return (omask);
}
