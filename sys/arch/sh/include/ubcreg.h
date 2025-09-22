/*	$OpenBSD: ubcreg.h,v 1.1.1.1 2006/10/06 21:02:55 miod Exp $	*/
/*	$NetBSD: ubcreg.h,v 1.4 2006/03/04 01:55:03 uwe Exp $	*/

/*-
 * Copyright (C) 1999 SAITOH Masanobu.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SH_UBCREG_H_
#define	_SH_UBCREG_H_
#include <sh/devreg.h>

/*
 * User Break Controller
 */

/* ch-A */
#define	SH3_BARA		0xffffffb0
#define	SH3_BAMRA		0xffffffb4
#define	SH3_BASRA		0xffffffe4
#define	SH3_BBRA		0xffffffb8
/* ch-B */
#define	SH3_BARB		0xffffffa0
#define	SH3_BAMRB		0xffffffa4
#define	SH3_BASRB		0xffffffe8
#define	SH3_BBRB		0xffffffa8
#define	SH3_BDRB		0xffffff90
#define	SH3_BDMRB		0xffffff94
/* common */
#define	SH3_BRCR		0xffffff98


/* ch-A */
#define	SH4_BARA		0xff200000
#define	SH4_BAMRA		0xff200004
#define	SH4_BASRA		0xff000014
#define	SH4_BBRA		0xff200008

/* ch-B */
#define	SH4_BARB		0xff20000c
#define	SH4_BAMRB		0xff200010
#define	SH4_BASRB		0xff000018
#define	SH4_BBRB		0xff200014
#define	SH4_BDRB		0xff200018
#define	SH4_BDMRB		0xff20001c
/* common */
#define	SH4_BRCR		0xff200020

#ifndef _LOCORE
#if defined(SH3) && defined(SH4)
extern uint32_t __sh_BARA;
extern uint32_t __sh_BAMRA;
extern uint32_t __sh_BASRA;
extern uint32_t __sh_BBRA;
extern uint32_t __sh_BARB;
extern uint32_t __sh_BAMRB;
extern uint32_t __sh_BASRB;
extern uint32_t __sh_BBRB;
extern uint32_t __sh_BDRB;
extern uint32_t __sh_BDMRB;
extern uint32_t __sh_BRCR;
#endif /* SH3 && SH4 */
#endif /* !_LOCORE */

#endif	/* !_SH_UBCREG_H_ */
