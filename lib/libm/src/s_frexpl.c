/*	$OpenBSD: s_frexpl.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/
/*-
 * Copyright (c) 2004-2005 David Schultz <das@FreeBSD.ORG>
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
 * $FreeBSD: src/lib/msun/src/s_frexpl.c,v 1.1 2005/03/07 04:54:51 das Exp $
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

#if LDBL_MAX_EXP != 0x4000
#error "Unsupported long double format"
#endif

long double
frexpl(long double x, int *ex)
{
	struct ieee_ext *p = (struct ieee_ext *)&x;

	switch (p->ext_exp) {
	case 0:		/* 0 or subnormal */
		if ((p->ext_fracl
#ifdef EXT_FRACLMBITS
			| p->ext_fraclm
#endif /* EXT_FRACLMBITS */
#ifdef EXT_FRACHMBITS
			| p->ext_frachm
#endif /* EXT_FRACHMBITS */
			| p->ext_frach) == 0) {
			*ex = 0;
		} else {
			x *= 0x1.0p514;
			*ex = p->ext_exp - 0x4200;
			p->ext_exp = 0x3ffe;
		}
		break;
	case 0x7fff:	/* infinity or NaN; value of *ex is unspecified */
		break;
	default:	/* normal */
		*ex = p->ext_exp - 0x3ffe;
		p->ext_exp = 0x3ffe;
		break;
	}

	return x;
}
DEF_STD(frexpl);
