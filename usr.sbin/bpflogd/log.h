/*	$OpenBSD: log.h,v 1.2 2025/05/15 12:49:05 kn Exp $	*/

/*
 * Copyright (c) 2008 David Gwynne <loki@animata.net>
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

#ifndef _LOG_H_
#define _LOG_H_

struct __logger {
	__dead void (*err)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	__dead void (*errx)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	void (*warn)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*warnx)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*info)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*debug)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
};

extern const struct __logger *__logger;

#define lerr(_e, _f...) __logger->err((_e), _f)
#define lerrx(_e, _f...) __logger->errx((_e), _f)
#define lwarn(_f...) __logger->warn(_f)
#define lwarnx(_f...) __logger->warnx(_f)
#define linfo(_f...) __logger->info(_f)
#define ldebug(_f...) __logger->debug(_f)

void	logger_syslog(const char *);

#endif /* _LOG_H_ */
