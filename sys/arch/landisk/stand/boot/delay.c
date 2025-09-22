/*	$OpenBSD: delay.c,v 1.1 2006/10/06 21:48:50 mickey Exp $	*/
/*	$NetBSD: delay.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <libsa.h>

#include <sh/tmureg.h>

#ifndef	TICK_CH
#define	TICK_CH	0
#endif
#if TICK_CH == 0
#define	TSTR	SH4_TSTR
#define	TCOR	SH4_TCOR0
#define	TCNT	SH4_TCNT0
#define	TCR	SH4_TCR0
#define	TSTR_CH	TSTR_STR0
#elif TICK_CH == 1
#define	TSTR	SH4_TSTR
#define	TCOR	SH4_TCOR1
#define	TCNT	SH4_TCNT1
#define	TCR	SH4_TCR1
#define	TSTR_CH	TSTR_STR1
#elif TICK_CH == 2
#define	TSTR	SH4_TSTR
#define	TCOR	SH4_TCOR2
#define	TCNT	SH4_TCNT2
#define	TCR	SH4_TCR2
#define	TSTR_CH	TSTR_STR2
#elif TICK_CH == 3
#define	TSTR	SH4_TSTR2
#define	TCOR	SH4_TCOR3
#define	TCNT	SH4_TCNT3
#define	TCR	SH4_TCR3
#define	TSTR_CH	SH4_TSTR2_STR3
#elif TICK_CH == 4
#define	TSTR	SH4_TSTR2
#define	TCOR	SH4_TCOR4
#define	TCNT	SH4_TCNT4
#define	TCR	SH4_TCR4
#define	TSTR_CH	SH4_TSTR2_STR4
#else
#error	TICK_CH != [01234]
#endif

#ifndef	TICK_PRESC
#define	TICK_PRESC	1024
#endif
#if TICK_PRESC == 4
#define	TCR_TPSC	TCR_TPSC_P4
#elif TICK_PRESC == 16
#define	TCR_TPSC	TCR_TPSC_P16
#elif TICK_PRESC == 64
#define	TCR_TPSC	TCR_TPSC_P64
#elif TICK_PRESC == 256
#define	TCR_TPSC	TCR_TPSC_P256
#elif TICK_PRESC == 1024
#define	TCR_TPSC	SH4_TCR_TPSC_P1024
#else
#error	TICK_PRESC != 4, 16, 64, 256, 1024
#endif

#define	TICKS_PER_SEC	(PCLOCK / TICK_PRESC)
#define	MS_PER_TICK	(1000000 / TICKS_PER_SEC)

int
tick_init(void)
{

	_reg_bclr_1(TSTR, TSTR_CH);
	_reg_write_2(TCR, TCR_TPSC);
	_reg_write_4(TCOR, 0xffffffff);
	_reg_write_4(TCNT, 0xffffffff);
	_reg_bset_1(TSTR, TSTR_CH);

	return 0;
}

void
tick_stop(void)
{

	_reg_bclr_1(TSTR, TSTR_CH);
}

uint32_t
gettick(void)
{

	return ~(_reg_read_4(TCNT));
}

void
delay(int ms)
{
	uint32_t base, now;

	base = gettick();
	for (;;) {
		now = gettick();
		if (((now - base) / MS_PER_TICK) > ms) {
			break;
		}
	}
}
