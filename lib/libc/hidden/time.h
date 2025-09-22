/*	$OpenBSD: time.h,v 1.7 2020/07/06 13:33:06 pirofti Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#ifndef _LIBC_TIME_H_
#define	_LIBC_TIME_H_

#include_next <time.h>

#if 0
extern PROTO_NORMAL(tzname);
#endif

PROTO_NORMAL(asctime);
PROTO_NORMAL(asctime_r);
PROTO_STD_DEPRECATED(clock);
PROTO_DEPRECATED(clock_getcpuclockid);
PROTO_NORMAL(clock_getres);
PROTO_WRAP(clock_gettime);
PROTO_NORMAL(clock_settime);
PROTO_STD_DEPRECATED(ctime);
PROTO_DEPRECATED(ctime_r);
PROTO_STD_DEPRECATED(difftime);
PROTO_NORMAL(gmtime);
PROTO_NORMAL(gmtime_r);
PROTO_NORMAL(localtime);
PROTO_NORMAL(localtime_r);
PROTO_NORMAL(mktime);
PROTO_CANCEL(nanosleep);
PROTO_NORMAL(strftime);
PROTO_DEPRECATED(strftime_l);
PROTO_NORMAL(strptime);
PROTO_NORMAL(time);
PROTO_DEPRECATED(timegm);
PROTO_DEPRECATED(timelocal);
PROTO_DEPRECATED(timeoff);
PROTO_STD_DEPRECATED(timespec_get);
PROTO_NORMAL(tzset);
PROTO_DEPRECATED(tzsetwall);

#endif /* !_LIBC_TIME_H_ */
