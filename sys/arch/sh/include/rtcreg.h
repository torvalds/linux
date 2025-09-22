/*	$OpenBSD: rtcreg.h,v 1.1.1.1 2006/10/06 21:02:55 miod Exp $	*/
/*	$NetBSD: rtcreg.h,v 1.10 2006/09/03 12:38:34 uwe Exp $	*/

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

#ifndef _SH_RTCREG_H_
#define	_SH_RTCREG_H_
#include <sh/devreg.h>

/*
 * RTC
 */
#define	SH3_R64CNT			0xfffffec0
#define	SH3_RSECCNT			0xfffffec2
#define	SH3_RMINCNT			0xfffffec4
#define	SH3_RHRCNT			0xfffffec6
#define	SH3_RWKCNT			0xfffffec8
#define	SH3_RDAYCNT			0xfffffeca
#define	SH3_RMONCNT			0xfffffecc
#define	SH3_RYRCNT			0xfffffece
#define	SH3_RSECAR			0xfffffed0
#define	SH3_RMINAR			0xfffffed2
#define	SH3_RHRAR			0xfffffed4
#define	SH3_RWKAR			0xfffffed6
#define	SH3_RDAYAR			0xfffffed8
#define	SH3_RMONAR			0xfffffeda
#define	SH3_RCR1			0xfffffedc
#define	SH3_RCR2			0xfffffede

#define	SH4_R64CNT			0xffc80000
#define	SH4_RSECCNT			0xffc80004
#define	SH4_RMINCNT			0xffc80008
#define	SH4_RHRCNT			0xffc8000c
#define	SH4_RWKCNT			0xffc80010
#define	SH4_RDAYCNT			0xffc80014
#define	SH4_RMONCNT			0xffc80018
#define	SH4_RYRCNT			0xffc8001c	/* 16 bit */
#define	SH4_RSECAR			0xffc80020
#define	SH4_RMINAR			0xffc80024
#define	SH4_RHRAR			0xffc80028
#define	SH4_RWKAR			0xffc8002c
#define	SH4_RDAYAR			0xffc80030
#define	SH4_RMONAR			0xffc80034
#define	SH4_RCR1			0xffc80038
#define	SH4_RCR2			0xffc8003c

#define	  SH_RCR1_CF			  0x80 /* carry flag */
#define	  SH_RCR1_CIE			  0x10 /* carry interrupt enable */
#define	  SH_RCR1_AIE			  0x08 /* alarm interrupt enable */
#define	  SH_RCR1_AF			  0x01 /* alarm flag */

#define	  SH_RCR2_PEF			  0x80 /* periodic interrupt flag */
#define	  SH_RCR2_PES2			  0x40 /* periodic interrupt freq */
#define	  SH_RCR2_PES1			  0x20 /* -//- */
#define	  SH_RCR2_PES0			  0x10 /* -//- */
#define	  SH_RCR2_ENABLE		  0x08
#define	  SH_RCR2_ADJ			  0x04 /* second adjustment */
#define	  SH_RCR2_RESET			  0x02
#define	  SH_RCR2_START			  0x01

#ifndef _LOCORE
#if defined(SH3) && defined(SH4)
extern uint32_t __sh_R64CNT;
extern uint32_t __sh_RSECCNT;
extern uint32_t __sh_RMINCNT;
extern uint32_t __sh_RHRCNT;
extern uint32_t __sh_RWKCNT;
extern uint32_t __sh_RDAYCNT;
extern uint32_t __sh_RMONCNT;
extern uint32_t __sh_RYRCNT;
extern uint32_t __sh_RSECAR;
extern uint32_t __sh_RMINAR;
extern uint32_t __sh_RHRAR;
extern uint32_t __sh_RWKAR;
extern uint32_t __sh_RDAYAR;
extern uint32_t __sh_RMONAR;
extern uint32_t __sh_RCR1;
extern uint32_t __sh_RCR2;
#endif /* SH3 && SH4 */
#endif /* !_LOCORE */

#endif /* !_SH_RTCREG_H_ */
