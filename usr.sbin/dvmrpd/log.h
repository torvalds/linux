/*	$OpenBSD: log.h,v 1.5 2021/12/13 18:28:40 deraadt Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

extern const char	*log_procname;

void	 log_init(int);
void	 log_verbose(int);
void	 logit(int, const char *, ...);
void	 vlog(int, const char *, va_list);
void	 log_warn(const char *, ...);
void	 log_warnx(const char *, ...);
void	 log_info(const char *, ...);
void	 log_debug(const char *, ...);
void	 fatal(const char *) __dead;
void	 fatalx(const char *) __dead;

#endif /* LOG_H */
