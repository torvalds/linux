/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005 TAKAHASHI Yoshihiro. All rights reserved.
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

/*
 * The outputs of the three timers are connected as follows:
 *
 *	 timer 0 -> irq 0
 *	 timer 1 -> dma chan 0 (for dram refresh)
 * 	 timer 2 -> speaker (via keyboard controller)
 *
 * Timer 0 is used to call hardclock.
 * Timer 2 is used to generate console beeps.
 */

#ifndef _MACHINE_TIMERREG_H_
#define _MACHINE_TIMERREG_H_

#ifdef _KERNEL

#include <dev/ic/i8253reg.h>

#define	IO_TIMER1	0x40		/* 8253 Timer #1 */
#define	TIMER_CNTR0	(IO_TIMER1 + TIMER_REG_CNTR0)
#define	TIMER_CNTR1	(IO_TIMER1 + TIMER_REG_CNTR1)
#define	TIMER_CNTR2	(IO_TIMER1 + TIMER_REG_CNTR2)
#define	TIMER_MODE	(IO_TIMER1 + TIMER_REG_MODE)

#endif /* _KERNEL */

#endif /* _MACHINE_TIMERREG_H_ */
