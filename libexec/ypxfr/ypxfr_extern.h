/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
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
#include <sys/types.h>
#include <limits.h>
#include <paths.h>
#include <db.h>
#include <rpcsvc/yp.h>

extern HASHINFO	openinfo;
extern BTREEINFO openinfo_b;

#ifndef _PATH_YP
#define _PATH_YP "/var/yp/"
#endif

extern char	*yp_dir;
extern int	debug;
extern enum ypstat	yp_errno;
extern void	yp_error(const char *, ...);
extern int	_yp_check(char **);
extern const char *ypxfrerr_string(ypxfrstat);
extern DB	*yp_open_db_rw(const char *, const char *, const int);
extern void	yp_init_dbs(void);
extern int	yp_put_record(DB *, DBT *, DBT *, int);
extern int	yp_get_record(const char *, const char *, const DBT *, DBT *, int);
extern int	ypxfr_get_map(char *, char *, char *, int (*)(int, char *, int, char *, int, char*));
extern char	*ypxfr_get_master(char *, char *, char *, const int);
extern unsigned	long ypxfr_get_order(char *, char *, char *, const int);
extern int	ypxfr_match(char *, char *, char *, char *, unsigned long);
extern char	*ypxfxerr_string(ypxfrstat);
extern int	ypxfrd_get_map(char  *, char *, char *, char *);
