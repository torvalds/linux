/*	$OpenBSD: termios.h,v 1.2 2015/09/14 08:13:01 guenther Exp $	*/
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

#ifndef _LIBC_TERMIOS_H_
#define _LIBC_TERMIOS_H_

#include_next <termios.h>

PROTO_DEPRECATED(cfgetispeed);
PROTO_DEPRECATED(cfgetospeed);
PROTO_DEPRECATED(cfmakeraw);
PROTO_DEPRECATED(cfsetispeed);
PROTO_DEPRECATED(cfsetospeed);
PROTO_DEPRECATED(cfsetspeed);
PROTO_DEPRECATED/*PROTO_CANCEL*/(tcdrain);
PROTO_DEPRECATED(tcflow);
PROTO_DEPRECATED(tcflush);
PROTO_NORMAL(tcgetattr);
PROTO_DEPRECATED(tcgetsid);
PROTO_DEPRECATED(tcsendbreak);
PROTO_NORMAL(tcsetattr);

#endif /* !_LIBC_TERMIOS_H_ */
