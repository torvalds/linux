/*	$OpenBSD: debugutil.c,v 1.7 2024/02/26 08:25:51 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "debugutil.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

int debuglevel = 0;
FILE *debugfp = NULL;
static int prio_idx_inititialized = 0;

static void  set_prio_idx_init(void);

#ifndef countof
#define countof(x)	(sizeof((x)) / sizeof((x)[0]))
#endif
#define VAL_NAME(x)	{ (x), #x}

#ifndef LOG_PRI
#define LOG_PRI(p)	((p) & LOG_PRIMASK)
#endif

static int use_syslog = 1;
static int no_debuglog = 0;
static int syslog_level_adjust = 0;

static struct {
	int prio;
	const char *name;
} prio_name[] = {
	VAL_NAME(LOG_EMERG),
	VAL_NAME(LOG_ALERT),
	VAL_NAME(LOG_CRIT),
	VAL_NAME(LOG_ERR),
	VAL_NAME(LOG_WARNING),
	VAL_NAME(LOG_NOTICE),
	VAL_NAME(LOG_INFO),
	VAL_NAME(LOG_DEBUG)
};

static const char *prio_name_idx[16];

static void
set_prio_idx_init()
{
	int i;

	if (prio_idx_inititialized)
		return;
	for (i = 0; i < (int)countof(prio_name); i++) {
		ASSERT(prio_name[i].prio < countof(prio_name_idx));
		if (prio_name[i].prio >= (int)countof(prio_name_idx))
		    continue;
		prio_name_idx[prio_name[i].prio] = &prio_name[i].name[4];
	}
	prio_idx_inititialized = 1;
}

void
debug_set_debugfp(FILE *fp)
{
	debugfp = fp;
}

void
debug_use_syslog(int b)
{
	if (b)
		use_syslog = 1;
	else
		use_syslog = 0;
}

void
debug_set_no_debuglog(int no_debuglog0)
{
	if (no_debuglog0)
		no_debuglog = 1;
	else
		no_debuglog = 0;
}

FILE *
debug_get_debugfp()
{
	return debugfp;
}

#define	DL(p)		((p) >> 24 & 0xff)
int
vlog_printf(uint32_t prio, const char *format, va_list ap)
{
	int status = 0, i, fmtoff = 0, state = 0, fmtlen, saved_errno, level;
	char fmt[8192];
	struct tm *lt;
	time_t now;

	ASSERT(format != NULL);
	ASSERT(format[0] != '\0');
	if (DL(prio) > 0 && debuglevel < (int)DL(prio))
		return -1;
	if (no_debuglog &&  LOG_PRI(prio) >= LOG_DEBUG)
		return -1;

	if (!prio_idx_inititialized)
		set_prio_idx_init();
	if (use_syslog && DL(prio) == 0) {
		level = LOG_PRI(prio) + syslog_level_adjust;
		if (!no_debuglog || level < LOG_DEBUG) {
			level = MINIMUM(LOG_DEBUG, level);
			level = MAXIMUM(LOG_EMERG, level);
			level |= (prio & LOG_FACMASK);
			vsyslog(level, format, ap);
		}
	}

	if (debugfp == NULL)
		return -1;

	time(&now);
	lt = localtime(&now);

	fmtlen = strlen(format);
	for (i = 0; i < fmtlen; i++) {
		/* 2 chars in this block and 2 chars after this block */
		if (sizeof(fmt) - fmtoff < 4)
			break;
		switch(state) {
		case 0:
			switch(format[i]) {
			case '%':
				state = 1;
				goto copy_loop;
			case '\n':
				fmt[fmtoff++] = '\n';
				fmt[fmtoff++] = '\t';
				goto copy_loop;
			}
			break;
		case 1:
			switch(format[i]) {
			default:
			case '%':
				fmt[fmtoff++] = '%';
				state = 0;
				break;
			case 'm':
				fmt[fmtoff] = '\0';
				saved_errno = errno;
				/* -1 is to reserve for '\n' */
				strlcat(fmt, strerror(errno), sizeof(fmt) - 1);
				errno = saved_errno;
				fmtoff = strlen(fmt);
				state = 0;
				goto copy_loop;
			}
		}
		fmt[fmtoff++] = format[i];
copy_loop:
		continue;
	}
	/* remove trailing TAB */
	if (fmtoff > 0 && fmt[fmtoff - 1] == '\t')
		fmtoff--;
	/* append new line char */
	if (fmtoff == 0 || fmt[fmtoff-1] != '\n')
		fmt[fmtoff++] = '\n';

	fmt[fmtoff] = '\0';

	ASSERT(0 <= LOG_PRI(prio)
	    && LOG_PRI(prio) < countof(prio_name_idx)
	    && prio_name_idx[LOG_PRI(prio)] != NULL);
	ftell(debugfp);
	fprintf(debugfp,
	    "%04d-%02d-%02d %02d:%02d:%02d:%s: "
	    , lt->tm_year + 1900
	    , lt->tm_mon + 1
	    , lt->tm_mday
	    , lt->tm_hour
	    , lt->tm_min
	    , lt->tm_sec
	    , (prio & 0xff000000) ? "DEBUG" : prio_name_idx[LOG_PRI(prio)]
	);
	status = vfprintf(debugfp, fmt, ap);
	fflush(debugfp);

	return status;
}

int
log_printf(int prio, const char *fmt, ...)
{
	int status;
	va_list ap;

	va_start(ap, fmt);
	status = vlog_printf((uint32_t)prio, fmt, ap);
	va_end(ap);

	return status;
}

void
debug_set_syslog_level_adjust(int adjust)
{
	syslog_level_adjust = adjust;
}

int
debug_get_syslog_level_adjust(void)
{
	return syslog_level_adjust;
}


/*
 * show_hd -
 *	print hexadecimal/ascii dump for debug
 *
 * usage:
 *  show_hd(stderr, buf, sizeof(buf));
 */
void
show_hd(FILE *file, const u_char *buf, int len)
{
	int i, o = 0;
	int hd_cnt = 0;
	char linebuf[80];
	char asciibuf[17];

	memset(asciibuf, ' ', sizeof(asciibuf));
	asciibuf[sizeof(asciibuf)-1] = '\0';

	for (i = 0; i < len; i++) {
		if (0x20 <= *(buf+i)  && *(buf+i) <= 0x7e)
			asciibuf[hd_cnt % 16] = *(buf+i);
		else
			asciibuf[hd_cnt % 16] = '.';

		switch (hd_cnt % 16) {
		case 0:
			o += snprintf(linebuf + o, sizeof(linebuf) - o,
			    "%04x  %02x", hd_cnt,
			    (unsigned char)*(buf+i));
			break;
		case 15:
			o += snprintf(linebuf + o, sizeof(linebuf) - o,
			    "%02x", (unsigned char)*(buf+i));
			if (file)
				fprintf(file, "\t%-47s  |%s|\n", linebuf,
				    asciibuf);
			else
				syslog(LOG_ERR, "%-47s  |%s|\n", linebuf,
				    asciibuf);
			memset(asciibuf, ' ', sizeof(asciibuf));
			asciibuf[sizeof(asciibuf)-1] = '\0';
			o = 0;
			break;
		case 8:
			o += snprintf(linebuf + o, sizeof(linebuf) - o,
			    "- %02x", (unsigned char)*(buf+i));
			break;
		default:
			if (hd_cnt % 2 == 1)
				o += snprintf(linebuf + o, sizeof(linebuf) - o,
				    "%02x ", (unsigned char)*(buf+i));
			else
				o += snprintf(linebuf + o, sizeof(linebuf) - o,
				    "%02x", (unsigned char)*(buf+i));
			break;
		}
		hd_cnt++;
	}
	if (hd_cnt > 0 && (hd_cnt % 16) != 0) {
		if (file)
			fprintf(file, "\t%-47s  |%s|\n", linebuf, asciibuf);
		else
			syslog(LOG_ERR, "%-47s  |%s|\n", linebuf, asciibuf);
	}
	if (file)
		fflush(file);
}
