/*	$OpenBSD: log.h,v 1.2 2017/03/13 21:20:56 bluhm Exp $	*/

/*
 * Copyright (c) 20083, 2004 Henning Brauer <henning@openbsd.org>
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

void log_init(int);
void logit(int pri, const char *fmt, ...);

__dead void fatal(const char*);
__dead void fatalx(const char*);

void log_warn(const char*, ...) __attribute__((format(printf, 1, 2)));
void log_warnx(const char*, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char*, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char*, ...) __attribute__((format(printf, 1, 2)));
void log_enqueue(const char*, ...) __attribute__((format(printf, 1, 2)));
