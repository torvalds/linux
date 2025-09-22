/*	$OpenBSD: filter.h,v 1.1.1.1 2012/07/11 11:43:27 dlg Exp $ */

/*
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
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

#define	FTP_PROXY_ANCHOR "tftp-proxy"

int add_filter(u_int32_t, u_int8_t, struct sockaddr *, struct sockaddr *,
    u_int16_t, u_int8_t);
int add_rdr(u_int32_t, struct sockaddr *, struct sockaddr *, u_int16_t,
    struct sockaddr *, u_int16_t, u_int8_t);
int do_commit(void);
int do_rollback(void);
void init_filter(char *, int);
int prepare_commit(u_int32_t);
