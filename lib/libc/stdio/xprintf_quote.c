/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Poul-Henning Kamp
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
 *
 * $FreeBSD$
 */

#include <namespace.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <wchar.h>
#include <vis.h>
#include <assert.h>
#include <sys/time.h>
#include "printf.h"

int
__printf_arginfo_quote(const struct printf_info *pi __unused, size_t n, int *argt)
{

	assert(n >= 1);
	argt[0] = PA_POINTER;
	return (1);
}

int
__printf_render_quote(struct __printf_io *io, const struct printf_info *pi __unused, const void *const *arg)
{
	const char *str, *p, *t, *o;
	char r[5];
	int i, ret;

	str = *((const char *const *)arg[0]);
	if (str == NULL)
		return (__printf_out(io, pi, "\"(null)\"", 8));
	if (*str == '\0')
		return (__printf_out(io, pi, "\"\"", 2));

	for (i = 0, p = str; *p; p++)
		if (isspace(*p) || *p == '\\' || *p == '"')
			i++;
	if (!i) 
		return (__printf_out(io, pi, str, strlen(str)));
	
	ret = __printf_out(io, pi, "\"", 1);
	for (t = p = str; *p; p++) {
		o = NULL;
		if (*p == '\\')
			o = "\\\\";
		else if (*p == '\n')
			o = "\\n";
		else if (*p == '\r')
			o = "\\r";
		else if (*p == '\t')
			o = "\\t";
		else if (*p == ' ')
			o = " ";
		else if (*p == '"')
			o = "\\\"";
		else if (isspace(*p)) {
			sprintf(r, "\\%03o", *p);
			o = r;
		} else
			continue;
		if (p != t)
			ret += __printf_out(io, pi, t, p - t);
		ret += __printf_out(io, pi, o, strlen(o));
		t = p + 1;
	}
	if (p != t)
		ret += __printf_out(io, pi, t, p - t);
	ret += __printf_out(io, pi, "\"", 1);
	__printf_flush(io);
	return(ret);
}
