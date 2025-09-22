/*	$OpenBSD: log.c,v 1.3 2019/03/31 03:53:42 yasuoka Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "log.h"

int		 log_debug_use_syslog = 0;
static int	 log_initialized = 0;
static int	 debug = 0;

void	 logit(int, const char *, ...);

void
log_init(int n_debug)
{
	extern char	*__progname;

	debug = n_debug;

	if (!debug)
		openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	tzset();
	log_initialized++;
}

void
logit(int pri, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vlog(pri, fmt, ap);
	va_end(ap);
}

void
vlog(int pri, const char *fmt, va_list ap)
{
	char			 fmtbuf[1024];
	time_t			 curr;
	struct tm		 tm;
	u_int			 i = 0, j;
	int			 saved_errno = errno;
	static struct {
		int		 v;
		const char	*l;
	} syslog_prionames[] = {
#define	NV(_l)	{ _l, #_l }
		NV(LOG_DEBUG),
		NV(LOG_INFO),
		NV(LOG_NOTICE),
		NV(LOG_WARNING),
		NV(LOG_ERR),
		NV(LOG_ALERT),
		NV(LOG_CRIT),
#undef NV
		{ -1, NULL }
	};

	if (log_initialized && !debug) {
		vsyslog(pri, fmt, ap);
		return;
	}
	if (log_initialized) {
		time(&curr);
		localtime_r(&curr, &tm);
		strftime(fmtbuf, sizeof(fmtbuf), "%Y-%m-%d %H:%M:%S:", &tm);
		for (i = 0; syslog_prionames[i].v != -1; i++) {
			if (syslog_prionames[i].v == LOG_PRI(pri)) {
				strlcat(fmtbuf, syslog_prionames[i].l + 4,
				    sizeof(fmtbuf));
				strlcat(fmtbuf, ": ", sizeof(fmtbuf));
				break;
			}
		}
		i = strlen(fmtbuf);
	}
	for (j = 0; i < sizeof(fmtbuf) - 2 && fmt[j] != '\0'; j++) {
		if (fmt[j] == '%' && fmt[j + 1] == 'm') {
			++j;
			fmtbuf[i] = '\0';
			strlcat(fmtbuf, strerror(saved_errno),
			    sizeof(fmtbuf) - 1);
			i = strlen(fmtbuf);
		} else
			fmtbuf[i++] = fmt[j];
	}
	fmtbuf[i++] = '\n';
	fmtbuf[i++] = '\0';

	vfprintf(stderr, fmtbuf, ap);
}


void
log_warn(const char *emsg, ...)
{
	char	*nfmt;
	va_list	 ap;
	int	 saved_errno = errno;

	/* best effort to even work in out of memory situations */
	if (emsg == NULL)
		logit(LOG_WARNING, "%s", strerror(saved_errno));
	else {
		va_start(ap, emsg);

		if (asprintf(&nfmt, "%s: %s", emsg, strerror(saved_errno))
		    == -1) {
			/* we tried it... */
			vlog(LOG_WARNING, emsg, ap);
			logit(LOG_WARNING, "%s", strerror(saved_errno));
		} else {
			vlog(LOG_WARNING, nfmt, ap);
			free(nfmt);
		}
		va_end(ap);
	}
}

void
log_warnx(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_WARNING, emsg, ap);
	va_end(ap);
}

void
log_info(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_INFO, emsg, ap);
	va_end(ap);
}

void
log_debug(const char *emsg, ...)
{
	va_list	 ap;

	if (debug || log_debug_use_syslog) {
		va_start(ap, emsg);
		vlog(LOG_DEBUG, emsg, ap);
		va_end(ap);
	}
}

void
fatal(const char *emsg)
{
	if (emsg == NULL)
		logit(LOG_CRIT, "fatal: %s", strerror(errno));
	else
		if (errno)
			logit(LOG_CRIT, "fatal: %s: %s",
			    emsg, strerror(errno));
		else
			logit(LOG_CRIT, "fatal: %s", emsg);

	exit(1);
}

void
fatalx(const char *emsg)
{
	errno = 0;
	fatal(emsg);
}
