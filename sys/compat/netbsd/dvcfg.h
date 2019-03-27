/* $FreeBSD$ */
/*	$NetBSD$	*/
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * [NetBSD for NEC PC98 series]
 *  Copyright (c) 1996 NetBSD/pc98 porting staff.
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1996 Naofumi HONDA.  All rights reserved.
 */

#ifndef _COMPAT_NETBSD_DVCFG_H_
#define _COMPAT_NETBSD_DVCFG_H_

typedef void *dvcfg_hw_t;

struct dvcfg_hwsel {
	int cfg_max;

	dvcfg_hw_t *cfg_sel;
};

#define	DVCFG_MAJOR(dvcfg)	(((u_int)(dvcfg)) >> 16)
#define	DVCFG_MINOR(dvcfg)	(((u_int)(dvcfg)) & 0xffff)

#define	DVCFG_MKCFG(major, minor) ((((u_int)(major)) << 16) | ((minor) & 0xffff))

#define	DVCFG_HWSEL_SZ(array)	(sizeof(array) / sizeof(dvcfg_hw_t))

static __inline dvcfg_hw_t dvcfg_hw(struct dvcfg_hwsel *, u_int);

static __inline dvcfg_hw_t
dvcfg_hw(selp, num)
	struct dvcfg_hwsel *selp;
	u_int num;
{

	return ((num >= selp->cfg_max) ? 0 : selp->cfg_sel[num]);
}

#define	DVCFG_HW(SELP, NUM)	dvcfg_hw((SELP), (NUM))
#endif	/* _COMPAT_NETBSD_DVCFG_H_ */
