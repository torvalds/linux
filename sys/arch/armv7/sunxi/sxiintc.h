/*	$OpenBSD: sxiintc.h,v 1.5 2025/05/10 10:11:02 visa Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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

#ifndef _SXIINTC_H_
#define _SXIINTC_H_

#ifndef _LOCORE

#include <arm/armreg.h>
#include <arm/cpufunc.h>
#include <machine/intr.h>

void sxiintc_setipl(int);
void sxiintc_splx(int);
int sxiintc_splraise(int);
int sxiintc_spllower(int);

void sxiintc_irq_handler(void *);
void *sxiintc_intr_establish(int, int, struct cpu_info *,
    int (*)(void *), void *, char *);
void sxiintc_intr_disestablish(void *);
const char *sxiintc_intr_string(void *);

#endif /* ! _LOCORE */

#endif /* _SXIINTC_H_ */

