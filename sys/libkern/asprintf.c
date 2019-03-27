/*-
 * Copyright (c) 2013-2015 Sandvine Inc.  All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/stdarg.h>

int
vasprintf(char **buf, struct malloc_type *mtp, const char *format, va_list va)
{
	int len, ret;
	va_list tmp_va;
	char dummy;

	va_copy(tmp_va, va);
	len = vsnprintf(&dummy, 0, format, tmp_va);
	va_end(tmp_va);
	if (len < 0) {
		*buf = NULL;
		return (len);
	}

	/* Account for null terminator. */
	len += 1;
	*buf = malloc(len, mtp, M_NOWAIT);
	if (*buf == NULL)
		return (-1);

	ret = vsnprintf(*buf, len, format, va);
	if (ret < 0) {
		free(*buf, mtp);
		*buf = NULL;
	}

	return (ret);
}

int
asprintf(char **buf, struct malloc_type *mtp, const char *format, ...)
{
	int ret;
	va_list va;

	va_start(va, format);
	ret = vasprintf(buf, mtp, format, va);
	va_end(va);

	return (ret);
}
