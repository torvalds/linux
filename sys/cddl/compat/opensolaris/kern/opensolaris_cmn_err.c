/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2007 John Birrell <jb@FreeBSD.org>. All rights reserved.
 * Copyright 2012 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */

#include <sys/assfail.h>
#include <sys/cmn_err.h>

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	char buf[256];
	const char *prefix;

	prefix = NULL; /* silence unwitty compilers */
	switch (ce) {
	case CE_CONT:
		prefix = "Solaris(cont): ";
		break;
	case CE_NOTE:
		prefix = "Solaris: NOTICE: ";
		break;
	case CE_WARN:
		prefix = "Solaris: WARNING: ";
		break;
	case CE_PANIC:
		prefix = "Solaris(panic): ";
		break;
	case CE_IGNORE:
		break;
	default:
		panic("Solaris: unknown severity level");
	}
	if (ce == CE_PANIC) {
		vsnprintf(buf, sizeof(buf), fmt, adx);
		panic("%s%s", prefix, buf);
	}
	if (ce != CE_IGNORE) {
		printf("%s", prefix);
		vprintf(fmt, adx);
		printf("\n");
	}
}

void
cmn_err(int type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(type, fmt, ap);
	va_end(ap);
}

int
assfail(const char *a, const char *f, int l)
{

	panic("solaris assert: %s, file: %s, line: %d", a, f, l);

	return (0);
}

void
assfail3(const char *a, uintmax_t lv, const char *op, uintmax_t rv,
    const char *f, int l)
{

	panic("solaris assert: %s (0x%jx %s 0x%jx), file: %s, line: %d",
	    a, lv, op, rv, f, l);
}
