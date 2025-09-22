/*	$OpenBSD: reg.h,v 1.3 2013/07/17 19:19:30 miod Exp $ */
/*
 * Copyright (c) 1999 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _M88K_REG_H_
#define _M88K_REG_H_

struct reg {
	unsigned long	r[32];
	unsigned long	epsr;
	unsigned long	fpsr;
	unsigned long	fpcr;
	unsigned long	sxip;
#define exip sxip	/* mc88110 */
	unsigned long	snip;
#define enip snip	/* mc88110 */
	unsigned long	sfip;
	unsigned long	ssbr;
#define duap ssbr	/* mc88110 */
	unsigned long	dmt0;
#define dsr dmt0	/* mc88110 */
	unsigned long	dmd0;
#define dlar dmd0	/* mc88110 */
	unsigned long	dma0;
#define dpar dma0	/* mc88110 */
	unsigned long	dmt1;
#define isr dmt1	/* mc88110 */
	unsigned long	dmd1;
#define ilar dmd1	/* mc88110 */
	unsigned long	dma1;
#define ipar dma1	/* mc88110 */
	unsigned long	dmt2;
#define isap dmt2	/* mc88110 */
	unsigned long	dmd2;
#define dsap dmd2	/* mc88110 */
	unsigned long	dma2;
#define iuap dma2	/* mc88110 */
	unsigned long	fpecr;
	unsigned long	fphs1;
	unsigned long	fpls1;
	unsigned long	fphs2;
	unsigned long	fpls2;
	unsigned long	fppt;
	unsigned long	fprh;
	unsigned long	fprl;
	unsigned long	fpit;
};

#endif /* _M88K_REG_H_ */
