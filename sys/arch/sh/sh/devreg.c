/*	$OpenBSD: devreg.c,v 1.3 2016/03/05 17:16:33 tobiasu Exp $	*/
/*	$NetBSD: devreg.c,v 1.6 2006/03/04 01:13:35 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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

#include <sys/param.h>

#include <sh/cache_sh3.h>
#include <sh/cache_sh4.h>
#include <sh/mmu_sh3.h>
#include <sh/mmu_sh4.h>
#include <sh/trap.h>

#include <sh/ubcreg.h>
#include <sh/rtcreg.h>
#include <sh/tmureg.h>

/* MMU */
uint32_t __sh_PTEH;
uint32_t __sh_TTB;
uint32_t __sh_TEA;
uint32_t __sh_TRA;
uint32_t __sh_EXPEVT;
uint32_t __sh_INTEVT;

/* UBC */
uint32_t __sh_BARA;
uint32_t __sh_BAMRA;
uint32_t __sh_BASRA;
uint32_t __sh_BBRA;
uint32_t __sh_BARB;
uint32_t __sh_BAMRB;
uint32_t __sh_BASRB;
uint32_t __sh_BBRB;
uint32_t __sh_BDRB;
uint32_t __sh_BDMRB;
uint32_t __sh_BRCR;

/* RTC */
uint32_t __sh_R64CNT;
uint32_t __sh_RSECCNT;
uint32_t __sh_RMINCNT;
uint32_t __sh_RHRCNT;
uint32_t __sh_RWKCNT;
uint32_t __sh_RDAYCNT;
uint32_t __sh_RMONCNT;
uint32_t __sh_RYRCNT;
uint32_t __sh_RSECAR;
uint32_t __sh_RMINAR;
uint32_t __sh_RHRAR;
uint32_t __sh_RWKAR;
uint32_t __sh_RDAYAR;
uint32_t __sh_RMONAR;
uint32_t __sh_RCR1;
uint32_t __sh_RCR2;

/* TMU */
uint32_t __sh_TOCR;
uint32_t __sh_TSTR;
uint32_t __sh_TCOR0;
uint32_t __sh_TCNT0;
uint32_t __sh_TCR0;
uint32_t __sh_TCOR1;
uint32_t __sh_TCNT1;
uint32_t __sh_TCR1;
uint32_t __sh_TCOR2;
uint32_t __sh_TCNT2;
uint32_t __sh_TCR2;
uint32_t __sh_TCPR2;

#define	SH3REG(x)	__sh_ ## x = SH3_ ## x
#define	SH4REG(x)	__sh_ ## x = SH4_ ## x

#define	SHREG(x)							\
do {									\
/* Exception */								\
SH ## x ## REG(TRA);							\
SH ## x ## REG(EXPEVT);							\
SH ## x ## REG(INTEVT);							\
/* UBC */								\
SH ## x ## REG(BARA);							\
SH ## x ## REG(BAMRA);							\
SH ## x ## REG(BASRA);							\
SH ## x ## REG(BBRA);							\
SH ## x ## REG(BARB);							\
SH ## x ## REG(BAMRB);							\
SH ## x ## REG(BASRB);							\
SH ## x ## REG(BBRB);							\
SH ## x ## REG(BDRB);							\
SH ## x ## REG(BDMRB);							\
SH ## x ## REG(BRCR);							\
/* MMU */								\
SH ## x ## REG(PTEH);							\
SH ## x ## REG(TEA);							\
SH ## x ## REG(TTB);							\
/* RTC */								\
SH ## x ## REG(R64CNT);							\
SH ## x ## REG(RSECCNT);						\
SH ## x ## REG(RMINCNT);						\
SH ## x ## REG(RHRCNT);							\
SH ## x ## REG(RWKCNT);							\
SH ## x ## REG(RDAYCNT);						\
SH ## x ## REG(RMONCNT);						\
SH ## x ## REG(RYRCNT);							\
SH ## x ## REG(RSECAR);							\
SH ## x ## REG(RMINAR);							\
SH ## x ## REG(RHRAR);							\
SH ## x ## REG(RWKAR);							\
SH ## x ## REG(RDAYAR);							\
SH ## x ## REG(RMONAR);							\
SH ## x ## REG(RCR1);							\
SH ## x ## REG(RCR2);							\
/* TMU */								\
SH ## x ## REG(TOCR);							\
SH ## x ## REG(TSTR);							\
SH ## x ## REG(TCOR0);							\
SH ## x ## REG(TCNT0);							\
SH ## x ## REG(TCR0);							\
SH ## x ## REG(TCOR1);							\
SH ## x ## REG(TCNT1);							\
SH ## x ## REG(TCR1);							\
SH ## x ## REG(TCOR2);							\
SH ## x ## REG(TCNT2);							\
SH ## x ## REG(TCR2);							\
SH ## x ## REG(TCPR2);							\
} while (/*CONSTCOND*/0)

void
sh_devreg_init(void)
{
	if (CPU_IS_SH3)
		SHREG(3);

	if (CPU_IS_SH4)
		SHREG(4);
}
