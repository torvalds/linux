/*	$OpenBSD: fenv.h,v 1.2 2018/03/12 04:25:08 guenther Exp $	*/
/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
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

#ifndef	_LIBM_FENV_H_
#define	_LIBM_FENV_H_

#include_next <fenv.h>

PROTO_NORMAL(feclearexcept);
PROTO_STD_DEPRECATED(fedisableexcept);
PROTO_STD_DEPRECATED(feenableexcept);
PROTO_NORMAL(fegetenv);
PROTO_STD_DEPRECATED(fegetexcept);
PROTO_STD_DEPRECATED(fegetexceptflag);
PROTO_NORMAL(fegetround);
PROTO_NORMAL(feholdexcept);
PROTO_NORMAL(feraiseexcept);
PROTO_NORMAL(fesetenv);
PROTO_NORMAL(fesetexceptflag);
PROTO_NORMAL(fesetround);
PROTO_NORMAL(fetestexcept);
PROTO_NORMAL(feupdateenv);

#endif	/* ! _LIBM_FENV_H_ */
