/*	$OpenBSD: syslog.h,v 1.19 2023/04/27 23:16:18 gnezdo Exp $	*/
/*	$NetBSD: syslog.h,v 1.14 1996/04/03 20:46:44 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *
 *	@(#)syslog.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _SYS_SYSLOG_H_
#define _SYS_SYSLOG_H_

#define	_PATH_LOG	"/dev/log"

#define	LIOCSFD		_IOW('l', 127, int)	/* set sendsyslog() fd */

#define LOG_MAXLINE	8192			/* maximum line length */

/*
 * priorities/facilities are encoded into a single 32-bit quantity, where the
 * bottom 3 bits are the priority (0-7) and the top 28 bits are the facility
 * (0-big number).  Both the priorities and the facilities map roughly
 * one-to-one to strings in the syslogd(8) source code.  This mapping is
 * included in this file.
 *
 * priorities (these are ordered)
 */
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

#define	LOG_PRIMASK	0x07	/* mask to extract priority part (internal) */
				/* extract priority */
#define	LOG_PRI(p)	((p) & LOG_PRIMASK)

#ifdef SYSLOG_NAMES
#define	INTERNAL_NOPRI	0x10	/* the "no priority" priority */
				/* mark "facility" */
#define	INTERNAL_MARK	(LOG_NFACILITIES<<3)
typedef struct _code {
	char	*c_name;
	int	c_val;
} CODE;

CODE prioritynames[] = {
	{ "alert",	LOG_ALERT },
	{ "crit",	LOG_CRIT },
	{ "debug",	LOG_DEBUG },
	{ "emerg",	LOG_EMERG },
	{ "err",	LOG_ERR },
	{ "error",	LOG_ERR },		/* DEPRECATED */
	{ "info",	LOG_INFO },
	{ "none",	INTERNAL_NOPRI },	/* INTERNAL */
	{ "notice",	LOG_NOTICE },
	{ "panic", 	LOG_EMERG },		/* DEPRECATED */
	{ "warn",	LOG_WARNING },		/* DEPRECATED */
	{ "warning",	LOG_WARNING },
	{ NULL,		-1 },
};
#endif

/* facility codes */
#define	LOG_KERN	(0<<3)	/* kernel messages */
#define	LOG_USER	(1<<3)	/* random user-level messages */
#define	LOG_MAIL	(2<<3)	/* mail system */
#define	LOG_DAEMON	(3<<3)	/* system daemons */
#define	LOG_AUTH	(4<<3)	/* security/authorization messages */
#define	LOG_SYSLOG	(5<<3)	/* messages generated internally by syslogd */
#define	LOG_LPR		(6<<3)	/* line printer subsystem */
#define	LOG_NEWS	(7<<3)	/* network news subsystem */
#define	LOG_UUCP	(8<<3)	/* UUCP subsystem */
#define	LOG_CRON	(9<<3)	/* clock daemon */
#define	LOG_AUTHPRIV	(10<<3)	/* security/authorization messages (private) */
#define	LOG_FTP		(11<<3)	/* ftp daemon */

	/* other codes through 15 reserved for system use */
#define	LOG_LOCAL0	(16<<3)	/* reserved for local use */
#define	LOG_LOCAL1	(17<<3)	/* reserved for local use */
#define	LOG_LOCAL2	(18<<3)	/* reserved for local use */
#define	LOG_LOCAL3	(19<<3)	/* reserved for local use */
#define	LOG_LOCAL4	(20<<3)	/* reserved for local use */
#define	LOG_LOCAL5	(21<<3)	/* reserved for local use */
#define	LOG_LOCAL6	(22<<3)	/* reserved for local use */
#define	LOG_LOCAL7	(23<<3)	/* reserved for local use */

#define	LOG_NFACILITIES	24	/* current number of facilities */
#define	LOG_FACMASK	0x03f8	/* mask to extract facility part */
				/* facility of pri */
#define	LOG_FAC(p)	(((p) & LOG_FACMASK) >> 3)

#ifdef SYSLOG_NAMES
CODE facilitynames[] = {
	{ "auth",	LOG_AUTH },
	{ "authpriv",	LOG_AUTHPRIV },
	{ "cron", 	LOG_CRON },
	{ "daemon",	LOG_DAEMON },
	{ "ftp",	LOG_FTP },
	{ "kern",	LOG_KERN },
	{ "lpr",	LOG_LPR },
	{ "mail",	LOG_MAIL },
	{ "mark", 	INTERNAL_MARK },	/* INTERNAL */
	{ "news",	LOG_NEWS },
	{ "security",	LOG_AUTH },		/* DEPRECATED */
	{ "syslog",	LOG_SYSLOG },
	{ "user",	LOG_USER },
	{ "uucp",	LOG_UUCP },
	{ "local0",	LOG_LOCAL0 },
	{ "local1",	LOG_LOCAL1 },
	{ "local2",	LOG_LOCAL2 },
	{ "local3",	LOG_LOCAL3 },
	{ "local4",	LOG_LOCAL4 },
	{ "local5",	LOG_LOCAL5 },
	{ "local6",	LOG_LOCAL6 },
	{ "local7",	LOG_LOCAL7 },
	{ NULL,		-1 },
};
#endif

/* Used by reentrant functions */

struct syslog_data {
	int	log_stat;
	const char 	*log_tag;
	int 	log_fac;
	int 	log_mask;
};

#define SYSLOG_DATA_INIT {0, (const char *)0, LOG_USER, 0xff}

#ifdef _KERNEL
#define	LOG_PRINTF	-1	/* pseudo-priority to indicate use of printf */
#endif

/*
 * arguments to setlogmask.
 */
#define	LOG_MASK(pri)	(1 << (pri))		/* mask for one priority */
#define	LOG_UPTO(pri)	((1 << ((pri)+1)) - 1)	/* all priorities through pri */

/*
 * Option flags for openlog.
 *
 * LOG_ODELAY no longer does anything.
 * LOG_NDELAY is the inverse of what it used to be.
 */
#define	LOG_PID		0x01	/* log the pid with each message */
#define	LOG_CONS	0x02	/* log on the console if errors in sending */
#define	LOG_ODELAY	0x04	/* delay open until first syslog() (default) */
#define	LOG_NDELAY	0x08	/* don't delay open */
#define	LOG_NOWAIT	0x10	/* don't wait for console forks: DEPRECATED */
#define	LOG_PERROR	0x20	/* log to stderr as well */

#ifndef _KERNEL

/*
 * Don't use va_list in the vsyslog() prototype.   Va_list is typedef'd
 * in <stdarg.h>.  Including it here may collide with the utility's includes.
 * It's unreasonable for utilities to have to include it to include <syslog.h>,
 * so we get __va_list from <machine/_types.h> and use it.
 */
#include <sys/cdefs.h>
#include <machine/_types.h>

__BEGIN_DECLS
void	closelog(void);
void	openlog(const char *, int, int);
int	setlogmask(int);
void	syslog(int, const char *, ...)
    __attribute__((__format__(__syslog__,2,3)));
void	vsyslog(int, const char *, __va_list);
void	closelog_r(struct syslog_data *);
void	openlog_r(const char *, int, int, struct syslog_data *);
int	setlogmask_r(int, struct syslog_data *);
void	syslog_r(int, struct syslog_data *, const char *, ...)
     __attribute__((__format__(__syslog__,3,4)));
void	vsyslog_r(int, struct syslog_data *, const char *, __va_list);
int	sendsyslog(const char *, __size_t, int);
__END_DECLS

#else /* !_KERNEL */

void	logpri(int);
void	log(int, const char *, ...)
    __attribute__((__format__(__kprintf__,2,3)));
int	addlog(const char *, ...)
    __attribute__((__format__(__kprintf__,1,2)));
void	logwakeup(void);

#endif /* !_KERNEL */
#endif /* !_SYS_SYSLOG_H_ */

