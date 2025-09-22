/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *
 *	from: @(#)xutil.c	8.1 (Berkeley) 6/6/93
 *	$Id: xutil.c,v 1.20 2023/07/05 18:45:14 guenther Exp $
 */

#include "am.h"

#include <sys/stat.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

FILE *logfp;
int syslogging;
int xlog_level = XLOG_ALL & ~XLOG_MAP & ~XLOG_STATS & ~XLOG_INFO;
int xlog_level_init = ~0;

/*
 * List of log options
 */
struct opt_tab xlog_opt[] = {
	{ "all", XLOG_ALL },		/* All messages */
#ifdef DEBUG
	{ "debug", XLOG_DEBUG },	/* Debug messages */
#endif /* DEBUG */
	{ "error", XLOG_ERROR },	/* Non-fatal system errors */
	{ "fatal", XLOG_FATAL },	/* Fatal errors */
	{ "info", XLOG_INFO },		/* Information */
	{ "map", XLOG_MAP },		/* Map errors */
	{ "stats", XLOG_STATS },	/* Additional statistical information */
	{ "user", XLOG_USER },		/* Non-fatal user errors */
	{ "warn", XLOG_WARNING },	/* Warnings */
	{ "warning", XLOG_WARNING },	/* Warnings */
	{ 0, 0 }
};

__dead void
xmallocfailure(void)
{
	plog(XLOG_FATAL, "Out of memory");
	going_down(1);
	abort();
}

void *
xmalloc(size_t len)
{
	void *p;
	int retries = 600;

	do {
		if ((p = malloc(len)) != NULL)
			return p;

		if (retries > 0) {
			plog(XLOG_ERROR, "Retrying memory allocation");
			sleep(1);
		}
	} while (--retries);

	xmallocfailure();
}

void *
xreallocarray(void *ptr, size_t nmemb, size_t size)
{
	ptr = reallocarray(ptr, nmemb, size);

	if (ptr == NULL)
		xmallocfailure();
	return (ptr);
}


/*
 * Take a log format string and expand occurrences of %m
 * with the current error code taken from errno.  Make sure
 * 'e' never gets longer than maxlen characters.
 */
static void
expand_error(const char *f, char *e, int maxlen)
{
	const char *p;
	char *q;
	int error = errno;
	int len = 0;

	for (p = f, q = e; (*q = *p) && len < maxlen; len++, q++, p++) {
		if (p[0] == '%' && p[1] == 'm') {
			char *errstr;
			errstr = strerror(error);
			if (errstr)
				strlcpy(q, errstr, maxlen - (q - e));
			else
				snprintf(q, maxlen - (q - e),
				    "Error %d", error);
			len += strlen(q) - 1;
			q += strlen(q) - 1;
			p++;
		}
	}
	e[maxlen-1] = '\0';		/* null terminate, to be sure */
}

/*
 * Output the time of day and hostname to the logfile
 */
static void
show_time_host_and_name(int lvl)
{
	static time_t last_t = 0;
	static char *last_ctime = 0;
	time_t t = clocktime();
	char *sev;

#if defined(DEBUG) && defined(PARANOID)
extern char **gargv;
#endif /* defined(DEBUG) && defined(PARANOID) */

	if (t != last_t) {
		last_ctime = ctime(&t);
		last_t = t;
	}

	switch (lvl) {
	case XLOG_FATAL:	sev = "fatal:"; break;
	case XLOG_ERROR:	sev = "error:"; break;
	case XLOG_USER:		sev = "user: "; break;
	case XLOG_WARNING:	sev = "warn: "; break;
	case XLOG_INFO:		sev = "info: "; break;
	case XLOG_DEBUG:	sev = "debug:"; break;
	case XLOG_MAP:		sev = "map:  "; break;
	case XLOG_STATS:	sev = "stats:"; break;
	default:		sev = "hmm:  "; break;
	}
	fprintf(logfp, "%15.15s %s %s[%ld]/%s ",
		last_ctime+4, hostname,
#if defined(DEBUG) && defined(PARANOID)
		gargv[0],
#else
		__progname,
#endif /* defined(DEBUG) && defined(PARANOID) */
		(long)mypid,
		sev);
}

void
plog(int lvl, const char *fmt, ...)
{
	char efmt[1024];
	va_list ap;

	if (!(xlog_level & lvl))
		return;


	if (syslogging) {
		switch(lvl) {	/* from mike <mcooper@usc.edu> */
		case XLOG_FATAL:	lvl = LOG_CRIT; break;
		case XLOG_ERROR:	lvl = LOG_ERR; break;
		case XLOG_USER:		lvl = LOG_WARNING; break;
		case XLOG_WARNING:	lvl = LOG_WARNING; break;
		case XLOG_INFO:		lvl = LOG_INFO; break;
		case XLOG_DEBUG:	lvl = LOG_DEBUG; break;
		case XLOG_MAP:		lvl = LOG_DEBUG; break;
		case XLOG_STATS:	lvl = LOG_INFO; break;
		default:		lvl = LOG_ERR; break;
		}
		va_start(ap, fmt);
		vsyslog(lvl, fmt, ap);
		va_end(ap);
		return;
	}

	expand_error(fmt, efmt, sizeof(efmt));

	/*
	 * Mimic syslog header
	 */
	show_time_host_and_name(lvl);
	va_start(ap, fmt);
	vfprintf(logfp, efmt, ap);
	va_end(ap);
	fputc('\n', logfp);
	fflush(logfp);
}

void
show_opts(int ch, struct opt_tab *opts)
{
	/*
	 * Display current debug options
	 */
	int i;
	int s = '{';

	fprintf(stderr, "\t[-%c {no}", ch);
	for (i = 0; opts[i].opt; i++) {
		fprintf(stderr, "%c%s", s, opts[i].opt);
		s = ',';
	}
	fputs("}]\n", stderr);
}

int
cmdoption(char *s, struct opt_tab *optb, int *flags)
{
	char *p = s;
	int errs = 0;

	while (p && *p) {
		int neg;
		char *opt;
		struct opt_tab *dp, *dpn = 0;

		s = p;
		p = strchr(p, ',');
		if (p)
			*p = '\0';

		if (s[0] == 'n' && s[1] == 'o') {
			opt = s + 2;
			neg = 1;
		} else {
			opt = s;
			neg = 0;
		}

		/*
		 * Scan the array of debug options to find the
		 * corresponding flag value.  If it is found
		 * then set (or clear) the flag (depending on
		 * whether the option was prefixed with "no").
		 */
		for (dp = optb; dp->opt; dp++) {
			if (strcmp(opt, dp->opt) == 0)
				break;
			if (opt != s && !dpn && strcmp(s, dp->opt) == 0)
				dpn = dp;
		}

		if (dp->opt || dpn) {
			if (!dp->opt) {
				dp = dpn;
				neg = !neg;
			}
			if (neg)
				*flags &= ~dp->flag;
			else
				*flags |= dp->flag;
		} else {
			/*
			 * This will log to stderr when parsing the command line
			 * since any -l option will not yet have taken effect.
			 */
			plog(XLOG_USER, "option \"%s\" not recognised", s);
			errs++;
		}
		/*
		 * Put the comma back
		 */
		if (p)
			*p++ = ',';
	}

	return errs;
}

/*
 * Switch on/off logging options
 */
int
switch_option(char *opt)
{
	int xl = xlog_level;
	int rc = cmdoption(opt, xlog_opt, &xl);
	if (rc) {
		rc = EINVAL;
	} else {
		/*
		 * Keep track of initial log level, and
		 * don't allow options to be turned off.
		 */
		if (xlog_level_init == ~0)
			xlog_level_init = xl;
		else
			xl |= xlog_level_init;
		xlog_level = xl;
	}
	return rc;
}

/*
 * Change current logfile
 */
int
switch_to_logfile(char *logfile)
{
	FILE *new_logfp = stderr;

	if (logfile) {
		syslogging = 0;
		if (strcmp(logfile, "/dev/stderr") == 0)
			new_logfp = stderr;
		else if (strcmp(logfile, "syslog") == 0) {
			syslogging = 1;
			new_logfp = stderr;
#if defined(LOG_CONS) && defined(LOG_NOWAIT)
			openlog(__progname, LOG_PID|LOG_CONS|LOG_NOWAIT,
				LOG_DAEMON);
#else
			/* 4.2 compat mode - XXX */
			openlog(__progname, LOG_PID);
#endif /* LOG_CONS && LOG_NOWAIT */
		} else {
			(void) umask(orig_umask);
			new_logfp = fopen(logfile, "a");
			umask(0);
		}
	}

	/*
	 * If we couldn't open a new file, then continue using the old.
	 */
	if (!new_logfp && logfile) {
		plog(XLOG_USER, "%s: Can't open logfile: %m", logfile);
		return 1;
	}
	/*
	 * Close the previous file
	 */
	if (logfp && logfp != stderr)
		(void) fclose(logfp);
	logfp = new_logfp;
	return 0;
}

time_t clock_valid = 0;
time_t xclock_valid = 0;
#ifndef clocktime
time_t
clocktime(void)
{
	time_t now = time(&clock_valid);
	if (xclock_valid > now) {
		/*
		 * Someone set the clock back!
		 */
		plog(XLOG_WARNING, "system clock reset");
		reschedule_timeouts(now, xclock_valid);
	}
	return xclock_valid = now;
}
#endif /* clocktime */
