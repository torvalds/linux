/*	$OpenBSD: slowcgi.h,v 1.1 2022/06/28 16:11:30 claudio Exp $ */
/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2013 Florian Obser <florian@openbsd.org>
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

#define FCGI_BEGIN_REQUEST	 1
#define FCGI_ABORT_REQUEST	 2
#define FCGI_END_REQUEST	 3
#define FCGI_PARAMS		 4
#define FCGI_STDIN		 5
#define FCGI_STDOUT		 6
#define FCGI_STDERR		 7
#define FCGI_DATA		 8
#define FCGI_GET_VALUES		 9
#define FCGI_GET_VALUES_RESULT	10
#define FCGI_UNKNOWN_TYPE	11
#define FCGI_MAXTYPE		(FCGI_UNKNOWN_TYPE)

struct loggers {
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

extern const struct loggers *logger;

#define lerr(_e, _f...) logger->err((_e), _f)
#define lerrx(_e, _f...) logger->errx((_e), _f)
#define lwarn(_f...) logger->warn(_f)
#define lwarnx(_f...) logger->warnx(_f)
#define linfo(_f...) logger->info(_f)
#define ldebug(_f...) logger->debug(_f)

