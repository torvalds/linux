/*	$OpenBSD: privsep.h,v 1.7 2015/10/11 07:32:06 guenther Exp $ */

/*
 * Copyright (c) 2010 Yasuoka Masahiko <yasuoka@openbsd.org>
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
#ifndef PRIVSEP_H
#define PRIVSEP_H 1

#include <sys/socket.h>
#include <stdio.h>

#define PRIVSEP_BUFSIZE		4092

#include "npppd_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

int   privsep_init (void);
void  privsep_fini (void);
pid_t privsep_priv_pid (void);
FILE  *priv_fopen (const char *);
int   priv_bind (int, const struct sockaddr *, socklen_t);
int   priv_unlink (const char *);
int   priv_socket (int, int, int);
int   priv_open (const char *, int);
int   priv_send (int, const void *, int, int);
int   priv_sendto (int, const void *, int, int, const struct sockaddr *, socklen_t);
int   priv_get_user_info(const char *, const char *, npppd_auth_user **);
int   priv_set_if_addr(const char *, struct in_addr *);
int   priv_get_if_addr(const char *, struct in_addr *);
int   priv_delete_if_addr(const char *);
int   priv_set_if_flags(const char *, int);
int   priv_get_if_flags(const char *, int *);

#ifdef __cplusplus
}
#endif

#endif
