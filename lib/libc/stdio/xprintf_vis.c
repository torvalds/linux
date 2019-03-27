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
#include <wchar.h>
#include <vis.h>
#include <assert.h>
#include <sys/time.h>
#include "printf.h"

int
__printf_arginfo_vis(const struct printf_info *pi, size_t n, int *argt)
{

	assert(n >= 1);
	argt[0] = PA_POINTER;
	return (1);
}

int
__printf_render_vis(struct __printf_io *io, const struct printf_info *pi, const void *const *arg)
{
	char *p, *buf;
	unsigned l;
	int ret;

	ret = 0;
	p = *((char **)arg[0]);
	if (p == NULL)
		return (__printf_out(io, pi, "(null)", 6));
	if (pi->prec >= 0)
		l = pi->prec;
	else
		l = strlen(p);
	buf = malloc(l * 4 + 1);
	if (buf == NULL)
		return (-1);
	if (pi->showsign)
		ret = strvisx(buf, p, l, VIS_WHITE | VIS_HTTPSTYLE);
	else if (pi->pad == '0')
		ret = strvisx(buf, p, l, VIS_WHITE | VIS_OCTAL);
	else if (pi->alt)
		ret = strvisx(buf, p, l, VIS_WHITE);
	else
		ret = strvisx(buf, p, l, VIS_WHITE | VIS_CSTYLE | VIS_OCTAL);
	ret += __printf_out(io, pi, buf, ret);
	__printf_flush(io);
	free(buf);
	return(ret);
}
