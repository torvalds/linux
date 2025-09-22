/*	$OpenBSD: interrupt.c,v 1.21 2025/04/30 12:30:54 visa Exp $	*/
/*	$NetBSD: interrupt.c,v 1.18 2006/01/25 00:02:57 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
#include <sys/systm.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>	/* uvmexp.intrs */

#include <sh/clock.h>
#include <sh/trap.h>
#include <sh/intcreg.h>
#include <sh/tmureg.h>
#include <machine/atomic.h>
#include <machine/intr.h>

void intc_intr_priority(int, int);
struct intc_intrhand *intc_alloc_ih(void);
void intc_free_ih(struct intc_intrhand *);
int intc_unknown_intr(void *);

#ifdef SH4
void intpri_intr_enable(int);
void intpri_intr_disable(int);
#endif

void tmu1_oneshot(void);
int tmu1_intr(void *);
void setsoft(int);

/*
 * EVTCODE to intc_intrhand mapper.
 * max #76 is SH4_INTEVT_TMU4 (0xb80)
 */
int8_t __intc_evtcode_to_ih[128];

struct intc_intrhand __intc_intrhand[_INTR_N + 1] = {
	/* Place holder interrupt handler for unregistered interrupt. */
	[0] = { .ih_func = intc_unknown_intr, .ih_level = 0xf0 }
};

/*
 * SH INTC support.
 */
void
intc_init(void)
{

	switch (cpu_product) {
#ifdef SH3
	case CPU_PRODUCT_7709:
	case CPU_PRODUCT_7709A:
		_reg_write_2(SH7709_IPRC, 0);
		_reg_write_2(SH7709_IPRD, 0);
		_reg_write_2(SH7709_IPRE, 0);
		/* FALLTHROUGH */
	case CPU_PRODUCT_7708:
	case CPU_PRODUCT_7708S:
	case CPU_PRODUCT_7708R:
		_reg_write_2(SH3_IPRA, 0);
		_reg_write_2(SH3_IPRB, 0);
		break;
#endif /* SH3 */

#ifdef SH4
	case CPU_PRODUCT_7751:
	case CPU_PRODUCT_7751R: 
		_reg_write_4(SH4_INTPRI00, 0);
		_reg_write_4(SH4_INTMSK00, INTMSK00_MASK_ALL);
		/* FALLTHROUGH */
	case CPU_PRODUCT_7750S:
	case CPU_PRODUCT_7750R:
		_reg_write_2(SH4_IPRD, 0);
		/* FALLTHROUGH */
	case CPU_PRODUCT_7750:
		_reg_write_2(SH4_IPRA, 0);
		_reg_write_2(SH4_IPRB, 0);
		_reg_write_2(SH4_IPRC, 0);
		break;
#endif /* SH4 */
	}

	intc_intr_establish(SH_INTEVT_TMU1_TUNI1, IST_LEVEL, IPL_SOFTNET,
	    tmu1_intr, NULL, "tmu1");
}

void *
intc_intr_establish(int evtcode, int trigger, int level,
    int (*ih_func)(void *), void *ih_arg, const char *name)
{
	struct intc_intrhand *ih;

	KDASSERT(evtcode >= 0x200 && level > 0);

	ih = intc_alloc_ih();
	ih->ih_func	= ih_func;
	ih->ih_arg	= ih_arg;
	ih->ih_level	= level << 4;	/* convert to SR.IMASK format. */
	ih->ih_evtcode	= evtcode;
	ih->ih_irq	= evtcode >> 5;
	ih->ih_name	= name;
	if (name)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	/* Map interrupt handler */
	EVTCODE_TO_IH_INDEX(evtcode) = ih->ih_idx;

	/* Priority */
	intc_intr_priority(evtcode, level);

	/* Sense select (SH7709, SH7709A only) XXX notyet */

	return (ih);
}

void
intc_intr_disestablish(void *arg)
{
	struct intc_intrhand *ih = arg;
	int evtcode = ih->ih_evtcode;

	/* Mask interrupt if IPR can manage it. if not, cascaded ICU will do */
	intc_intr_priority(evtcode, 0);

	/* Unmap interrupt handler */
	EVTCODE_TO_IH_INDEX(evtcode) = 0;

	if (ih->ih_name)
		evcount_detach(&ih->ih_count);
	intc_free_ih(ih);
}

void
intc_intr_disable(int evtcode)
{
	int s;

	s = _cpu_intr_suspend();
	KASSERT(EVTCODE_TO_IH_INDEX(evtcode) != 0); /* there is a handler */
	switch (evtcode) {
	default:
		intc_intr_priority(evtcode, 0);
		break;

#ifdef SH4
	case SH4_INTEVT_PCISERR:
	case SH4_INTEVT_PCIDMA3:
	case SH4_INTEVT_PCIDMA2:
	case SH4_INTEVT_PCIDMA1:
	case SH4_INTEVT_PCIDMA0:
	case SH4_INTEVT_PCIPWON:
	case SH4_INTEVT_PCIPWDWN:
	case SH4_INTEVT_PCIERR:
		intpri_intr_disable(evtcode);
		break;
#endif
	}
	_cpu_intr_resume(s);
}

void
intc_intr_enable(int evtcode)
{
	struct intc_intrhand *ih;
	int s;

	s = _cpu_intr_suspend();
	KASSERT(EVTCODE_TO_IH_INDEX(evtcode) != 0); /* there is a handler */
	switch (evtcode) {
	default:
		ih = EVTCODE_IH(evtcode);
		/* ih_level is in the SR.IMASK format */
		intc_intr_priority(evtcode, (ih->ih_level >> 4));
		break;

#ifdef SH4
	case SH4_INTEVT_PCISERR:
	case SH4_INTEVT_PCIDMA3:
	case SH4_INTEVT_PCIDMA2:
	case SH4_INTEVT_PCIDMA1:
	case SH4_INTEVT_PCIDMA0:
	case SH4_INTEVT_PCIPWON:
	case SH4_INTEVT_PCIPWDWN:
	case SH4_INTEVT_PCIERR:
		intpri_intr_enable(evtcode);
		break;
#endif
	}
	_cpu_intr_resume(s);
}


/*
 * int intc_intr_priority(int evtcode, int level)
 *	Setup interrupt priority register.
 *	SH7708, SH7708S, SH7708R, SH7750, SH7750S ... evtcode is INTEVT
 *	SH7709, SH7709A				  ... evtcode is INTEVT2
 */
void
intc_intr_priority(int evtcode, int level)
{
	volatile uint16_t *iprreg;
	int pos;
	uint16_t r;

#define	__SH_IPR(_sh, _ipr, _pos)					   \
	do {								   \
		iprreg = (volatile uint16_t *)(SH ## _sh ## _IPR ## _ipr); \
		pos = (_pos);						   \
	} while (/*CONSTCOND*/0)

#define	SH3_IPR(_ipr, _pos)		__SH_IPR(3, _ipr, _pos)
#define	SH4_IPR(_ipr, _pos)		__SH_IPR(4, _ipr, _pos)
#define	SH7709_IPR(_ipr, _pos)		__SH_IPR(7709, _ipr, _pos)

#define	SH_IPR(_ipr, _pos)						\
	do {								\
		if (CPU_IS_SH3)						\
			SH3_IPR(_ipr, _pos);				\
		else							\
			SH4_IPR(_ipr, _pos);				\
	} while (/*CONSTCOND*/0)

	iprreg = 0;
	pos = -1;

	switch (evtcode) {
	case SH_INTEVT_TMU0_TUNI0:
		SH_IPR(A, 12);
		break;
	case SH_INTEVT_TMU1_TUNI1:
		SH_IPR(A, 8);
		break;
	case SH_INTEVT_TMU2_TUNI2:
		SH_IPR(A, 4);
		break;
	case SH_INTEVT_WDT_ITI:
		SH_IPR(B, 12);
		break;
	case SH_INTEVT_SCI_ERI:
	case SH_INTEVT_SCI_RXI:
	case SH_INTEVT_SCI_TXI:
	case SH_INTEVT_SCI_TEI:
		SH_IPR(B, 4);
		break;
	}

#ifdef SH3
	if (CPU_IS_SH3) {
		switch (evtcode) {
		case SH7709_INTEVT2_IRQ3:
			SH7709_IPR(C, 12);
			break;
		case SH7709_INTEVT2_IRQ2:
			SH7709_IPR(C, 8);
			break;
		case SH7709_INTEVT2_IRQ1:
			SH7709_IPR(C, 4);
			break;
		case SH7709_INTEVT2_IRQ0:
			SH7709_IPR(C, 0);
			break;
		case SH7709_INTEVT2_PINT07:
			SH7709_IPR(D, 12);
			break;
		case SH7709_INTEVT2_PINT8F:
			SH7709_IPR(D, 8);
			break;
		case SH7709_INTEVT2_IRQ5:
			SH7709_IPR(D, 4);
			break;
		case SH7709_INTEVT2_IRQ4:
			SH7709_IPR(D, 0);
			break;
		case SH7709_INTEVT2_DEI0:
		case SH7709_INTEVT2_DEI1:
		case SH7709_INTEVT2_DEI2:
		case SH7709_INTEVT2_DEI3:
			SH7709_IPR(E, 12);
			break;
		case SH7709_INTEVT2_IRDA_ERI:
		case SH7709_INTEVT2_IRDA_RXI:
		case SH7709_INTEVT2_IRDA_BRI:
		case SH7709_INTEVT2_IRDA_TXI:
			SH7709_IPR(E, 8);
			break;
		case SH7709_INTEVT2_SCIF_ERI:
		case SH7709_INTEVT2_SCIF_RXI:
		case SH7709_INTEVT2_SCIF_BRI:
		case SH7709_INTEVT2_SCIF_TXI:
			SH7709_IPR(E, 4);
			break;
		case SH7709_INTEVT2_ADC:
			SH7709_IPR(E, 0);
			break;
		}
	}
#endif /* SH3 */

#ifdef SH4
	if (CPU_IS_SH4) {
		switch (evtcode) {
		case SH4_INTEVT_SCIF_ERI:
		case SH4_INTEVT_SCIF_RXI:
		case SH4_INTEVT_SCIF_BRI:
		case SH4_INTEVT_SCIF_TXI:
			SH4_IPR(C, 4);
			break;

#if 0
		case SH4_INTEVT_PCISERR:
		case SH4_INTEVT_PCIDMA3:
		case SH4_INTEVT_PCIDMA2:
		case SH4_INTEVT_PCIDMA1:
		case SH4_INTEVT_PCIDMA0:
		case SH4_INTEVT_PCIPWON:
		case SH4_INTEVT_PCIPWDWN:
		case SH4_INTEVT_PCIERR:
#endif
		case SH4_INTEVT_TMU3:
		case SH4_INTEVT_TMU4:
			intpri_intr_priority(evtcode, level);
			break;
		}
	}
#endif /* SH4 */

	/*
	 * XXX: This function gets called even for interrupts that
	 * don't have their priority defined by IPR registers.
	 */
	if (pos < 0)
		return;

	r = _reg_read_2(iprreg);
	r = (r & ~(0xf << (pos))) | (level << (pos));
	_reg_write_2(iprreg, r);
}

/*
 * Interrupt handler holder allocator.
 */
struct intc_intrhand *
intc_alloc_ih(void)
{
	/* #0 is reserved for unregistered interrupt. */
	struct intc_intrhand *ih = &__intc_intrhand[1];
	int i;

	for (i = 1; i <= _INTR_N; i++, ih++)
		if (ih->ih_idx == 0) {	/* no driver uses this. */
			ih->ih_idx = i;	/* register myself */
			return (ih);
		}

	panic("increase _INTR_N greater than %d", _INTR_N);
	return (NULL);
}

void
intc_free_ih(struct intc_intrhand *ih)
{
	ih->ih_idx = 0;
	memset(ih, 0, sizeof(*ih));
}

/* Place-holder for debugging */
int
intc_unknown_intr(void *arg)
{
	printf("INTEVT=0x%x", _reg_read_4(SH_(INTEVT)));
	if (cpu_product == CPU_PRODUCT_7709 || cpu_product == CPU_PRODUCT_7709A)
		printf(" INTEVT2=0x%x", _reg_read_4(SH7709_INTEVT2));
	printf("\n");

	panic("unknown interrupt");
	/* NOTREACHED */
	return (0);
}

#ifdef SH4 /* SH7751 support */

/*
 * INTPRIxx
 */
void
intpri_intr_priority(int evtcode, int level)
{
	volatile uint32_t *iprreg;
	uint32_t r;
	int pos;

	if (!CPU_IS_SH4)
		return;

	switch (cpu_product) {
	default:
		return;

	case CPU_PRODUCT_7751:
	case CPU_PRODUCT_7751R:
		break;
	}

	iprreg = (volatile uint32_t *)SH4_INTPRI00;
	pos = -1;

	switch (evtcode) {
	case SH4_INTEVT_PCIDMA3:
	case SH4_INTEVT_PCIDMA2:
	case SH4_INTEVT_PCIDMA1:
	case SH4_INTEVT_PCIDMA0:
	case SH4_INTEVT_PCIPWDWN:
	case SH4_INTEVT_PCIPWON:
	case SH4_INTEVT_PCIERR:
		pos = 0;
		break;

	case SH4_INTEVT_PCISERR:
		pos = 4;
		break;

	case SH4_INTEVT_TMU3:
		pos = 8;
		break;

	case SH4_INTEVT_TMU4:
		pos = 12;
		break;
	}

	if (pos < 0) {
		return;
	}

	r = _reg_read_4(iprreg);
	r = (r & ~(0xf << pos)) | (level << pos);
	_reg_write_4(iprreg, r);
}

void
intpri_intr_enable(int evtcode)
{
	volatile uint32_t *iprreg;
	uint32_t bit;

	if (!CPU_IS_SH4)
		return;

	switch (cpu_product) {
	default:
		return;

	case CPU_PRODUCT_7751:
	case CPU_PRODUCT_7751R:
		break;
	}

	iprreg = (volatile uint32_t *)SH4_INTMSKCLR00;
	bit = 0;

	switch (evtcode) {
	case SH4_INTEVT_PCISERR:
	case SH4_INTEVT_PCIDMA3:
	case SH4_INTEVT_PCIDMA2:
	case SH4_INTEVT_PCIDMA1:
	case SH4_INTEVT_PCIDMA0:
	case SH4_INTEVT_PCIPWON:
	case SH4_INTEVT_PCIPWDWN:
	case SH4_INTEVT_PCIERR:
		bit = (1 << ((evtcode - SH4_INTEVT_PCISERR) >> 5));
		break;

	case SH4_INTEVT_TMU3:
		bit = INTREQ00_TUNI3;
		break;

	case SH4_INTEVT_TMU4:
		bit = INTREQ00_TUNI4;
		break;
	}

	if ((bit == 0) || (iprreg == NULL)) {
		return;
	}

	_reg_write_4(iprreg, bit);
}

void
intpri_intr_disable(int evtcode)
{
	volatile uint32_t *iprreg;
	uint32_t bit;

	if (!CPU_IS_SH4)
		return;

	switch (cpu_product) {
	default:
		return;

	case CPU_PRODUCT_7751:
	case CPU_PRODUCT_7751R:
		break;
	}

	iprreg = (volatile uint32_t *)SH4_INTMSK00;
	bit = 0;

	switch (evtcode) {
	case SH4_INTEVT_PCISERR:
	case SH4_INTEVT_PCIDMA3:
	case SH4_INTEVT_PCIDMA2:
	case SH4_INTEVT_PCIDMA1:
	case SH4_INTEVT_PCIDMA0:
	case SH4_INTEVT_PCIPWON:
	case SH4_INTEVT_PCIPWDWN:
	case SH4_INTEVT_PCIERR:
		bit = (1 << ((evtcode - SH4_INTEVT_PCISERR) >> 5));
		break;

	case SH4_INTEVT_TMU3:
		bit = INTREQ00_TUNI3;
		break;

	case SH4_INTEVT_TMU4:
		bit = INTREQ00_TUNI4;
		break;
	}

	if ((bit == 0) || (iprreg == NULL)) {
		return;
	}

	_reg_write_4(iprreg, bit);
}
#endif /* SH4 */

void
softintr(int si)
{
	tmu1_oneshot();
}

void
intr_barrier(void *cookie)
{
}

/*
 * Software interrupt is simulated with TMU one-shot timer.
 */
void
tmu1_oneshot(void)
{
	_reg_bclr_1(SH_(TSTR), TSTR_STR1);
	_reg_write_4(SH_(TCNT1), 0);
	_reg_bset_1(SH_(TSTR), TSTR_STR1);
}

int
tmu1_intr(void *arg)
{
	_reg_bclr_1(SH_(TSTR), TSTR_STR1);
	_reg_bclr_2(SH_(TCR1), TCR_UNF);

	softintr_dispatch(SOFTINTR_TTY);
	softintr_dispatch(SOFTINTR_NET);
	softintr_dispatch(SOFTINTR_CLOCK);

	return (0);
}
