/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, Boris Popov
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $FreeBSD$
 *
 * from: FreeBSD: src/lib/libncp/ncpl_rcfile.c,v 1.5 2007/01/09 23:27:39 imp Exp
 */

#ifndef _SIMRC_H_
#define _SIMRC_H_

#include <sys/queue.h>

struct rckey {
	SLIST_ENTRY(rckey)	rk_next;
	char			*rk_name;	/* key name */
	char			*rk_value;	/* key value */
};

struct rcsection {
	SLIST_ENTRY(rcsection)	rs_next;
	SLIST_HEAD(rckey_head,rckey) rs_keys;	/* key list */
	char			*rs_name;	/* section name */
	int			rs_id;		/* allow few same named */
};

struct rcfile {
	SLIST_ENTRY(rcfile)	rf_next;
	SLIST_HEAD(rcsec_head, rcsection) rf_sect;	/* sections list */
	char			*rf_name;		/* file name */
	FILE			*rf_f;			/* file desc */
};

int rc_open(const char *, const char *,struct rcfile **);
int rc_close(struct rcfile *);
int rc_getstringptr(struct rcfile *, const char *, int, const char *,
    char **);
int rc_getstring(struct rcfile *, const char *, int, const char *,
    unsigned int, char *);
int rc_getint(struct rcfile *, const char *, int, const char *, int *);
int rc_getbool(struct rcfile *, const char *, int, const char *, int *);
int rc_getsectionscount(struct rcfile *, const char *);
char **rc_getkeys(struct rcfile *, const char *, int);

#endif /* _SIMRC_H_ */
