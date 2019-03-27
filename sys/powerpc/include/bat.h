/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-4-Clause
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: bat.h,v 1.2 1999/12/18 01:36:06 thorpej Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_BAT_H_
#define	_MACHINE_BAT_H_

#ifndef LOCORE
struct bat {
	u_int32_t batu;
	u_int32_t batl;
};
#endif

/* Lower BAT bits (all but PowerPC 601): */
#define	BAT_PBS		0xfffe0000	/* physical block start */
#define	BAT_W		0x00000040	/* 1 = write-through, 0 = write-back */
#define	BAT_I		0x00000020	/* cache inhibit */
#define	BAT_M		0x00000010	/* memory coherency enable */
#define	BAT_G		0x00000008	/* guarded region */

#define	BAT_PP_NONE	0x00000000	/* no access permission */
#define	BAT_PP_RO_S	0x00000001	/* read-only (soft) */
#define	BAT_PP_RW	0x00000002	/* read/write */
#define	BAT_PP_RO	0x00000003	/* read-only */

/* Upper BAT bits (all but PowerPC 601): */
#define	BAT_EBS		0xfffe0000	/* effective block start */
#define	BAT_BL		0x00001ffc	/* block length */
#define	BAT_Vs		0x00000002	/* valid in supervisor mode */
#define	BAT_Vu		0x00000001	/* valid in user mode */

#define	BAT_V		(BAT_Vs|BAT_Vu)

/* Block Length encoding (all but PowerPC 601): */
#define	BAT_BL_128K	0x00000000
#define	BAT_BL_256K	0x00000004
#define	BAT_BL_512K	0x0000000c
#define	BAT_BL_1M	0x0000001c
#define	BAT_BL_2M	0x0000003c
#define	BAT_BL_4M	0x0000007c
#define	BAT_BL_8M	0x000000fc
#define	BAT_BL_16M	0x000001fc
#define	BAT_BL_32M	0x000003fc
#define	BAT_BL_64M	0x000007fc
#define	BAT_BL_128M	0x00000ffc
#define	BAT_BL_256M	0x00001ffc

#define	BATU(va, len, v)						\
	(((va) & BAT_EBS) | ((len) & BAT_BL) | ((v) & BAT_V))

#define	BATL(pa, wimg, pp)						\
	(((pa) & BAT_PBS) | (wimg) | (pp))


/* Lower BAT bits (PowerPC 601): */
#define	BAT601_PBN	0xfffe0000	/* physical block number */
#define	BAT601_V	0x00000040	/* valid */
#define	BAT601_BSM	0x0000003f	/* block size mask */

/* Upper BAT bits (PowerPC 601): */
#define	BAT601_BLPI	0xfffe0000	/* block logical page index */
#define	BAT601_W	0x00000040	/* 1 = write-through, 0 = write-back */
#define	BAT601_I	0x00000020	/* cache inhibit */
#define	BAT601_M	0x00000010	/* memory coherency enable */
#define	BAT601_Ks	0x00000008	/* key-supervisor */
#define	BAT601_Ku	0x00000004	/* key-user */

/*
 * Permission bits on the PowerPC 601 are modified by the appropriate
 * Key bit:
 *
 *	Key	PP	Access
 *	0	NONE	read/write
 *	0	RO_S	read/write
 *	0	RW	read/write
 *	0	RO	read-only
 *
 *	1	NONE	none
 *	1	RO_S	read-only
 *	1	RW	read/write
 *	1	RO	read-only
 */
#define	BAT601_PP_NONE	0x00000000	/* no access permission */
#define	BAT601_PP_RO_S	0x00000001	/* read-only (soft) */
#define	BAT601_PP_RW	0x00000002	/* read/write */
#define	BAT601_PP_RO	0x00000003	/* read-only */

/* Block Size Mask encoding (PowerPC 601): */
#define	BAT601_BSM_128K	0x00000000
#define	BAT601_BSM_256K	0x00000001
#define	BAT601_BSM_512K	0x00000003
#define	BAT601_BSM_1M	0x00000007
#define	BAT601_BSM_2M	0x0000000f
#define	BAT601_BSM_4M	0x0000001f
#define	BAT601_BSM_8M	0x0000003f

#define	BATU601(va, wim, key, pp)					\
	(((va) & BAT601_BLPI) | (wim) | (key) | (pp))

#define	BATL601(pa, size, v)						\
	(((pa) & BAT601_PBN) | (v) | (size))

#if defined(_KERNEL) && !defined(LOCORE)
extern struct bat battable[16];
#endif

#endif	/* _MACHINE_BAT_H_ */
