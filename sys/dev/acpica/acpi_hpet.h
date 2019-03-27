/*-
 * Copyright (c) 2005 Poul-Henning Kamp
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __ACPI_HPET_H__
#define	__ACPI_HPET_H__

#define HPET_MEM_WIDTH		0x400	/* Expected memory region size */

/* General registers */
#define HPET_CAPABILITIES	0x0	/* General capabilities and ID */
#define	HPET_CAP_VENDOR_ID	0xffff0000
#define	HPET_CAP_LEG_RT		0x00008000
#define	HPET_CAP_COUNT_SIZE	0x00002000 /* 1 = 64-bit, 0 = 32-bit */
#define	HPET_CAP_NUM_TIM	0x00001f00
#define	HPET_CAP_REV_ID		0x000000ff
#define HPET_PERIOD		0x4	/* Period (1/hz) of timer */
#define HPET_CONFIG		0x10	/* General configuration register */
#define	HPET_CNF_LEG_RT		0x00000002
#define	HPET_CNF_ENABLE		0x00000001
#define	HPET_ISR		0x20	/* General interrupt status register */
#define HPET_MAIN_COUNTER	0xf0	/* Main counter register */

/* Timer registers */
#define	HPET_TIMER_CAP_CNF(x)	((x) * 0x20 + 0x100)
#define	HPET_TCAP_INT_ROUTE	0xffffffff00000000
#define	HPET_TCAP_FSB_INT_DEL	0x00008000
#define	HPET_TCNF_FSB_EN	0x00004000
#define	HPET_TCNF_INT_ROUTE	0x00003e00
#define	HPET_TCNF_32MODE	0x00000100
#define	HPET_TCNF_VAL_SET	0x00000040
#define	HPET_TCAP_SIZE		0x00000020 /* 1 = 64-bit, 0 = 32-bit */
#define	HPET_TCAP_PER_INT	0x00000010 /* Supports periodic interrupts */
#define	HPET_TCNF_TYPE		0x00000008 /* 1 = periodic, 0 = one-shot */
#define	HPET_TCNF_INT_ENB	0x00000004
#define	HPET_TCNF_INT_TYPE	0x00000002 /* 1 = level triggered, 0 = edge */
#define	HPET_TIMER_COMPARATOR(x) ((x) * 0x20 + 0x108)
#define	HPET_TIMER_FSB_VAL(x)	((x) * 0x20 + 0x110)
#define	HPET_TIMER_FSB_ADDR(x)	((x) * 0x20 + 0x114)

#define	HPET_MIN_CYCLES		128	/* Period considered reliable. */

#ifdef _KERNEL
struct timecounter;
struct vdso_timehands;
struct vdso_timehands32;

uint32_t hpet_vdso_timehands(struct vdso_timehands *vdso_th,
    struct timecounter *tc);
uint32_t hpet_vdso_timehands32(struct vdso_timehands32 *vdso_th32,
    struct timecounter *tc);
#endif

#endif /* !__ACPI_HPET_H__ */
