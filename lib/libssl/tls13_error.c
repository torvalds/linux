/* $OpenBSD: tls13_error.c,v 1.1 2020/01/20 13:10:37 jsing Exp $ */
/*
 * Copyright (c) 2014,2019 Joel Sing <jsing@openbsd.org>
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

#include <errno.h>

#include "tls13_internal.h"

void
tls13_error_clear(struct tls13_error *error)
{
	error->code = 0;
	error->subcode = 0;
	error->errnum = 0;
	error->file = NULL;
	error->line = 0;
	free(error->msg);
	error->msg = NULL;
}

static int
tls13_error_vset(struct tls13_error *error, int code, int subcode, int errnum,
    const char *file, int line, const char *fmt, va_list ap)
{
	char *errmsg = NULL;
	int rv = -1;

	tls13_error_clear(error);

	error->code = code;
	error->subcode = subcode;
	error->errnum = errnum;
	error->file = file;
	error->line = line;

	if (vasprintf(&errmsg, fmt, ap) == -1) {
		errmsg = NULL;
		goto err;
	}

	if (errnum == -1) {
		error->msg = errmsg;
		return 0;
	}

	if (asprintf(&error->msg, "%s: %s", errmsg, strerror(errnum)) == -1) {
		error->msg = NULL;
		goto err;
	}
	rv = 0;

 err:
	free(errmsg);

	return rv;
}

int
tls13_error_set(struct tls13_error *error, int code, int subcode,
    const char *file, int line, const char *fmt, ...)
{
	va_list ap;
	int errnum, rv;

	errnum = errno;

	va_start(ap, fmt);
	rv = tls13_error_vset(error, code, subcode, errnum, file, line, fmt, ap);
	va_end(ap);

	return (rv);
}

int
tls13_error_setx(struct tls13_error *error, int code, int subcode,
    const char *file, int line, const char *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = tls13_error_vset(error, code, subcode, -1, file, line, fmt, ap);
	va_end(ap);

	return (rv);
}
