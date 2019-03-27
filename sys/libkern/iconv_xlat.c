/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/iconv.h>

#include "iconv_converter_if.h"

/*
 * "XLAT" converter
 */

#ifdef MODULE_DEPEND
MODULE_DEPEND(iconv_xlat, libiconv, 2, 2, 2);
#endif

/*
 * XLAT converter instance
 */
struct iconv_xlat {
	KOBJ_FIELDS;
	u_char *		d_table;
	struct iconv_cspair *	d_csp;
};

static int
iconv_xlat_open(struct iconv_converter_class *dcp,
	struct iconv_cspair *csp, struct iconv_cspair *cspf, void **dpp)
{
	struct iconv_xlat *dp;

	dp = (struct iconv_xlat *)kobj_create((struct kobj_class*)dcp, M_ICONV, M_WAITOK);
	dp->d_table = csp->cp_data;
	dp->d_csp = csp;
	csp->cp_refcount++;
	*dpp = (void*)dp;
	return 0;
}

static int
iconv_xlat_close(void *data)
{
	struct iconv_xlat *dp = data;

	dp->d_csp->cp_refcount--;
	kobj_delete((struct kobj*)data, M_ICONV);
	return 0;
}

static int
iconv_xlat_conv(void *d2p, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft,
	int convchar, int casetype)
{
	struct iconv_xlat *dp = (struct iconv_xlat*)d2p;
	const char *src;
	char *dst;
	int n, r;

	if (inbuf == NULL || *inbuf == NULL || outbuf == NULL || *outbuf == NULL)
		return 0;
	if (casetype != 0)
		return -1;
	if (convchar == 1)
		r = n = 1;
	else
		r = n = min(*inbytesleft, *outbytesleft);
	src = *inbuf;
	dst = *outbuf;
	while(r--)
		*dst++ = dp->d_table[(u_char)*src++];
	*inbuf += n;
	*outbuf += n;
	*inbytesleft -= n;
	*outbytesleft -= n;
	return 0;
}

static const char *
iconv_xlat_name(struct iconv_converter_class *dcp)
{
	return "xlat";
}

static kobj_method_t iconv_xlat_methods[] = {
	KOBJMETHOD(iconv_converter_open,	iconv_xlat_open),
	KOBJMETHOD(iconv_converter_close,	iconv_xlat_close),
	KOBJMETHOD(iconv_converter_conv,	iconv_xlat_conv),
#if 0
	KOBJMETHOD(iconv_converter_init,	iconv_xlat_init),
	KOBJMETHOD(iconv_converter_done,	iconv_xlat_done),
#endif
	KOBJMETHOD(iconv_converter_name,	iconv_xlat_name),
	{0, 0}
};

KICONV_CONVERTER(xlat, sizeof(struct iconv_xlat));
