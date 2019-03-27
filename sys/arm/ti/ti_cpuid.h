/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _TI_CPUID_H_
#define	_TI_CPUID_H_

#define	OMAP_MAKEREV(d, a, b, c) \
	(uint32_t)(((d) << 16) | (((a) & 0xf) << 8) | (((b) & 0xf) << 4) | ((c) & 0xf))

#define	OMAP_REV_DEVICE(x)	(((x) >> 16) & 0xffff)
#define	OMAP_REV_MAJOR(x)	(((x) >> 8) & 0xf)
#define	OMAP_REV_MINOR(x)	(((x) >> 4) & 0xf)
#define	OMAP_REV_MINOR_MINOR(x)	(((x) >> 0) & 0xf)

#define	OMAP3350_DEV		0x3530
#define	OMAP3350_REV_ES1_0	OMAP_MAKEREV(OMAP3350_DEV, 1, 0, 0)
#define	OMAP3530_REV_ES2_0	OMAP_MAKEREV(OMAP3350_DEV, 2, 0, 0)
#define	OMAP3530_REV_ES2_1	OMAP_MAKEREV(OMAP3350_DEV, 2, 1, 0)
#define	OMAP3530_REV_ES3_0	OMAP_MAKEREV(OMAP3350_DEV, 3, 0, 0)
#define	OMAP3530_REV_ES3_1	OMAP_MAKEREV(OMAP3350_DEV, 3, 1, 0)
#define	OMAP3530_REV_ES3_1_2	OMAP_MAKEREV(OMAP3350_DEV, 3, 1, 2)

#define	OMAP4430_DEV		0x4430
#define	OMAP4430_REV_ES1_0	OMAP_MAKEREV(OMAP4430_DEV, 1, 0, 0)
#define	OMAP4430_REV_ES2_0	OMAP_MAKEREV(OMAP4430_DEV, 2, 0, 0)
#define	OMAP4430_REV_ES2_1	OMAP_MAKEREV(OMAP4430_DEV, 2, 1, 0)
#define	OMAP4430_REV_ES2_2	OMAP_MAKEREV(OMAP4430_DEV, 2, 2, 0)
#define	OMAP4430_REV_ES2_3	OMAP_MAKEREV(OMAP4430_DEV, 2, 3, 0)
#define	OMAP4430_REV_UNKNOWN	OMAP_MAKEREV(OMAP4430_DEV, 9, 9, 9)

#define	OMAP4460_DEV		0x4460
#define	OMAP4460_REV_ES1_0	OMAP_MAKEREV(OMAP4460_DEV, 1, 0, 0)
#define	OMAP4460_REV_ES1_1	OMAP_MAKEREV(OMAP4460_DEV, 1, 1, 0)
#define	OMAP4460_REV_UNKNOWN	OMAP_MAKEREV(OMAP4460_DEV, 9, 9, 9)

#define	OMAP4470_DEV		0x4470
#define	OMAP4470_REV_ES1_0	OMAP_MAKEREV(OMAP4470_DEV, 1, 0, 0)
#define	OMAP4470_REV_UNKNOWN	OMAP_MAKEREV(OMAP4470_DEV, 9, 9, 9)

#define	OMAP_UNKNOWN_DEV	OMAP_MAKEREV(0x9999, 9, 9, 9)

#define	AM335X_DEVREV(x)	((x) >> 28)

#define	CHIP_OMAP_4	0
#define	CHIP_AM335X	1

extern int _ti_chip;

static __inline int ti_chip(void)
{
	KASSERT(_ti_chip != -1, ("Can't determine TI Chip"));
	return _ti_chip;
}

uint32_t ti_revision(void);

static __inline bool ti_soc_is_supported(void)
{

	return (_ti_chip != -1);
}

#endif  /* _TI_CPUID_H_ */
