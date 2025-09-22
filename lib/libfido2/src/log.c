/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#undef _GNU_SOURCE /* XSI strerror_r() */

#include <stdarg.h>
#include <stdio.h>

#include "fido.h"

#ifndef FIDO_NO_DIAGNOSTIC

#define XXDLEN	32
#define XXDROW	128
#define LINELEN	256

#ifndef TLS
#define TLS
#endif

static TLS int logging;
static TLS fido_log_handler_t *log_handler;

static void
log_on_stderr(const char *str)
{
	fprintf(stderr, "%s", str);
}

static void
do_log(const char *suffix, const char *fmt, va_list args)
{
	char line[LINELEN], body[LINELEN];

	vsnprintf(body, sizeof(body), fmt, args);

	if (suffix != NULL)
		snprintf(line, sizeof(line), "%.180s: %.70s\n", body, suffix);
	else
		snprintf(line, sizeof(line), "%.180s\n", body);

	log_handler(line);
}

void
fido_log_init(void)
{
	logging = 1;
	log_handler = log_on_stderr;
}

void
fido_log_debug(const char *fmt, ...)
{
	va_list args;

	if (!logging || log_handler == NULL)
		return;

	va_start(args, fmt);
	do_log(NULL, fmt, args);
	va_end(args);
}

void
fido_log_xxd(const void *buf, size_t count, const char *fmt, ...)
{
	const uint8_t *ptr = buf;
	char row[XXDROW], xxd[XXDLEN];
	va_list args;

	if (!logging || log_handler == NULL)
		return;

	snprintf(row, sizeof(row), "buf=%p, len=%zu", buf, count);
	va_start(args, fmt);
	do_log(row, fmt, args);
	va_end(args);
	*row = '\0';

	for (size_t i = 0; i < count; i++) {
		*xxd = '\0';
		if (i % 16 == 0)
			snprintf(xxd, sizeof(xxd), "%04zu: %02x", i, *ptr++);
		else
			snprintf(xxd, sizeof(xxd), " %02x", *ptr++);
		strlcat(row, xxd, sizeof(row));
		if (i % 16 == 15 || i == count - 1) {
			fido_log_debug("%s", row);
			*row = '\0';
		}
	}
}

void
fido_log_error(int errnum, const char *fmt, ...)
{
	char errstr[LINELEN];
	va_list args;

	if (!logging || log_handler == NULL)
		return;
	if (strerror_r(errnum, errstr, sizeof(errstr)) != 0)
		snprintf(errstr, sizeof(errstr), "error %d", errnum);

	va_start(args, fmt);
	do_log(errstr, fmt, args);
	va_end(args);
}

void
fido_set_log_handler(fido_log_handler_t *handler)
{
	if (handler != NULL)
		log_handler = handler;
}

#endif /* !FIDO_NO_DIAGNOSTIC */
