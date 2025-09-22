/*	$OpenBSD: bat.h,v 1.6 2015/03/31 16:00:38 mpi Exp $	*/

/*
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
 */

#ifndef	_POWERPC_BAT_H_
#define	_POWERPC_BAT_H_

struct bat {
	u_int32_t batu;
	u_int32_t batl;
};

#define	BATU(vaddr,len)		(((vaddr)&0xf0000000)|(len)|0x2)
#define	BATL(raddr,wimg)	(((raddr)&0xf0000000)|(wimg)|0x2)

#define	BAT_W		0x40
#define	BAT_I		0x20
#define	BAT_M		0x10
#define	BAT_G		0x08

#define BAT_BL_128K     0x00000000
#define BAT_BL_256K     0x00000004
#define BAT_BL_512K     0x0000000c
#define BAT_BL_1M       0x0000001c
#define BAT_BL_2M       0x0000003c
#define BAT_BL_4M       0x0000007c
#define BAT_BL_8M       0x000000fc
#define BAT_BL_16M      0x000001fc
#define BAT_BL_32M      0x000003fc
#define BAT_BL_64M      0x000007fc
#define BAT_BL_128M     0x00000ffc
#define BAT_BL_256M     0x00001ffc
/* Extended Block Lengths (7455+) */
#define	BAT_BL_512M	0x00003ffc
#define	BAT_BL_1G	0x00007ffc
#define	BAT_BL_2G	0x0000fffc
#define	BAT_BL_4G	0x0001fffc

#ifdef	_KERNEL
extern struct bat battable[16];
#endif

#endif	/* _POWERPC_BAT_H_ */
