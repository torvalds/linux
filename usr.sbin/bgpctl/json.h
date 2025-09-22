/*	$OpenBSD: json.h,v 1.8 2023/06/05 16:24:05 claudio Exp $ */

/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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

#include <stdarg.h>
#include <stdio.h>

void	json_do_start(FILE *);
int	json_do_finish(void);
void	json_do_array(const char *);
void	json_do_object(const char *, int);
void	json_do_end(void);
void	json_do_printf(const char *, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
void	json_do_string(const char *, const char *);
void	json_do_hexdump(const char *, void *, size_t);
void	json_do_bool(const char *, int);
void	json_do_uint(const char *, unsigned long long);
void	json_do_int(const char *, long long);
void	json_do_double(const char *, double);
