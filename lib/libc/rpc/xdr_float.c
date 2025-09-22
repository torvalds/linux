/*	$OpenBSD: xdr_float.c,v 1.23 2016/03/09 16:28:47 deraadt Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xdr_float.c, Generic XDR routines implementation.
 *
 * These are the "floating point" xdr routines used to (de)serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */

#include <stdio.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

/*
 * NB: Not portable.
 * This routine works on machines with IEEE754 FP.
 */

#include <endian.h>

bool_t
xdr_float(XDR *xdrs, float *fp)
{
	bool_t rv;
	long tmpl;
	switch (xdrs->x_op) {

	case XDR_ENCODE:
		tmpl = *(int32_t *)fp;
		return (XDR_PUTLONG(xdrs, &tmpl));

	case XDR_DECODE:
		rv = XDR_GETLONG(xdrs, &tmpl);
		*(int32_t *)fp = tmpl;
		return (rv);

	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

bool_t
xdr_double(XDR *xdrs, double *dp)
{
	int32_t *i32p;
	bool_t rv;
	long tmpl;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		i32p = (int32_t *)dp;
#if (BYTE_ORDER == BIG_ENDIAN) || (defined(__arm__) && !defined(__VFP_FP__))
		tmpl = *i32p++;
		rv = XDR_PUTLONG(xdrs, &tmpl);
		if (!rv)
			return (rv);
		tmpl = *i32p;
		rv = XDR_PUTLONG(xdrs, &tmpl);
#else
		tmpl = *(i32p+1);
		rv = XDR_PUTLONG(xdrs, &tmpl);
		if (!rv)
			return (rv);
		tmpl = *i32p;
		rv = XDR_PUTLONG(xdrs, &tmpl);
#endif
		return (rv);

	case XDR_DECODE:
		i32p = (int32_t *)dp;
#if (BYTE_ORDER == BIG_ENDIAN) || (defined(__arm__) && !defined(__VFP_FP__))
		rv = XDR_GETLONG(xdrs, &tmpl);
		*i32p++ = tmpl;
		if (!rv)
			return (rv);
		rv = XDR_GETLONG(xdrs, &tmpl);
		*i32p = tmpl;
#else
		rv = XDR_GETLONG(xdrs, &tmpl);
		*(i32p+1) = tmpl;
		if (!rv)
			return (rv);
		rv = XDR_GETLONG(xdrs, &tmpl);
		*i32p = tmpl;
#endif
		return (rv);

	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}
