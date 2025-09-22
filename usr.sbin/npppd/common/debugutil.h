/*	$OpenBSD: debugutil.h,v 1.4 2015/12/17 08:01:55 tb Exp $ */
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
#ifndef	DEBUG_UTIL_H
#define	DEBUG_UTIL_H 1

#include <stdio.h>	/* for FILE * */
#include "debugmacro.h"

#define	DEBUG_LEVEL_1	( 1 << 24)
#define	DEBUG_LEVEL_2	( 2 << 24)
#define	DEBUG_LEVEL_3	( 3 << 24)
#define	DEBUG_LEVEL_4	( 4 << 24)
#define	DEBUG_LEVEL_5	( 5 << 24)
#define	DEBUG_LEVEL_6	( 6 << 24)
#define	DEBUG_LEVEL_7	( 7 << 24)
#define	DEBUG_LEVEL_8	( 8 << 24)
#define	DEBUG_LEVEL_9	( 9 << 24)
#define	DEBUG_LEVEL_10	(10 << 24)
#define	DEBUG_LEVEL_11	(11 << 24)
#define	DEBUG_LEVEL_12	(12 << 24)
#define	DEBUG_LEVEL_13	(13 << 24)
#define	DEBUG_LEVEL_14	(14 << 24)
#define	DEBUG_LEVEL_15	(15 << 24)

extern int debuglevel;

/* adapted from FreeBSD:/usr/include/sys/cdefs */
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
		__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

void  debug_set_debugfp (FILE *);
FILE  *debug_get_debugfp (void);
int   vlog_printf (uint32_t, const char *, va_list);
int   log_printf (int, const char *, ...) __printflike(2, 3);
void  show_hd (FILE *, const u_char *, int);
void  debug_use_syslog (int);
void  debug_set_syslog_level_adjust (int);
int   debug_get_syslog_level_adjust (void);
void  debug_set_no_debuglog (int);

#ifdef __cplusplus
}
#endif


#endif
