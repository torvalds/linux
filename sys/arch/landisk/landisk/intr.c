/*	$OpenBSD: intr.c,v 1.13 2024/11/06 18:59:09 miod Exp $	*/
/*	$NetBSD: intr.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>

#include <sh/trap.h>

#include <machine/intr.h>

#define	_N_EXTINTR		8

#define	LANDISK_INTEN		0xb0000005
#define	INTEN_ALL_MASK		0x00

struct intrhand {
	int	(*ih_fun)(void *);
	void	*ih_arg;
	struct	intrhand *ih_next;
	int	ih_enable;
	int	ih_level;
	int	ih_irq;
	struct evcount	ih_count;
	const char	*ih_name;
};

struct extintr_handler {
	int		(*eih_func)(void *eih_arg);
	void		*eih_arg;
	struct intrhand	*eih_ih;
	int		eih_nih;
};

static struct extintr_handler extintr_handler[_N_EXTINTR];

static int fakeintr(void *arg);
static int extintr_intr_handler(void *arg);

void
intc_intr(int ssr, int spc, int ssp)
{
	struct intc_intrhand *ih;
	struct clockframe cf;
	int evtcode;

	curcpu()->ci_idepth++;

	evtcode = _reg_read_4(SH4_INTEVT);
	ih = EVTCODE_IH(evtcode);
	KDASSERT(ih->ih_func);

	switch (evtcode) {
#if 0
#define	IRL(irq)	(0x200 + ((irq) << 5))
	case IRL(5): case IRL(6): case IRL(7): case IRL(8):
	case IRL(9): case IRL(10): case IRL(11): case IRL(12):
	{
		int level;
		uint8_t inten, bit;

		bit = 1 << (EVTCODE_TO_MAP_INDEX(evtcode) - 5);
		inten = _reg_read_1(LANDISK_INTEN);
		_reg_write_1(LANDISK_INTEN, inten & ~bit);
		level = (_IPL_NSOFT + 1) << 4;	/* disable softintr */
		ssr &= 0xf0;
		if (level < ssr)
			level = ssr;
		(void)_cpu_intr_resume(level);
		if ((*ih->ih_func)(ih->ih_arg) != 0)
			ih->ih_count.ec_count++;
		_reg_write_1(LANDISK_INTEN, inten);
		break;
	}
#endif
	default:
		(void)_cpu_intr_resume(ih->ih_level);
		if ((*ih->ih_func)(ih->ih_arg) != 0)
			ih->ih_count.ec_count++;
		break;

	case SH_INTEVT_TMU0_TUNI0:
		(void)_cpu_intr_resume(ih->ih_level);
		cf.ssr = ssr;
		cf.spc = spc;
		if ((*ih->ih_func)(&cf) != 0)
			ih->ih_count.ec_count++;
		break;

	case SH_INTEVT_NMI:
		printf("NMI ignored.\n");
		break;
	}

	curcpu()->ci_idepth--;
}

void
intr_init(void)
{
	_reg_write_1(LANDISK_INTEN, INTEN_ALL_MASK);
}

void *
extintr_establish(int irq, int level, int (*ih_fun)(void *), void *ih_arg,
    const char *ih_name)
{
	static struct intrhand fakehand = {fakeintr};
	struct extintr_handler *eih;
	struct intrhand **p, *q, *ih;
	int evtcode;
	int s;

	KDASSERT(irq >= 5 && irq < 13);

	ih = malloc(sizeof(*ih), M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL)
		panic("intr_establish: can't malloc handler info");

	s = _cpu_intr_suspend();

	switch (level) {
	default:
#if defined(DEBUG)
		panic("extintr_establish: unknown level %d", level);
		/*NOTREACHED*/
#endif
	case IPL_BIO:
	case IPL_NET:
	case IPL_TTY:
		break;
	}

	eih = &extintr_handler[irq - 5];
	if (eih->eih_func == NULL) {
		evtcode = 0x200 + (irq << 5);
		eih->eih_func = intc_intr_establish(evtcode, IST_LEVEL, level,
		    extintr_intr_handler, eih, NULL);
	}

	/*
	 * Figure out where to put the handler.
	 * This is O(N^2), but we want to preserve the order, and N is
	 * generally small.
	 */
	for (p = &eih->eih_ih; (q = *p) != NULL; p = &q->ih_next)
		continue;

	/*
	 * Actually install a fake handler momentarily, since we might be doing
	 * this with interrupts enabled and don't want the real routine called
	 * until masking is set up.
	 */
	fakehand.ih_level = level;
	*p = &fakehand;

	/*
	 * Poke the real handler in now.
	 */
	memset(ih, 0, sizeof(*ih));
	ih->ih_fun = ih_fun;
	ih->ih_arg = ih_arg;
	ih->ih_next = NULL;
	ih->ih_enable = 1;
	ih->ih_level = level;
	ih->ih_irq = irq;
	ih->ih_name = ih_name;

	if (ih_name != NULL)
		evcount_attach(&ih->ih_count, ih_name, &ih->ih_irq);
	*p = ih;

	if (++eih->eih_nih == 1) {
		/* Unmask interrupt */
		_reg_bset_1(LANDISK_INTEN, (1 << (irq - 5)));
	}

	_cpu_intr_resume(s);

	return (ih);
}

void
extintr_disestablish(void *aux)
{
	struct intrhand *ih = aux;
	struct intrhand **p, *q;
	struct extintr_handler *eih;
	int irq;
	int s;

	KDASSERT(ih != NULL);

	s = _cpu_intr_suspend();

	irq = ih->ih_irq - 5;
	eih = &extintr_handler[irq];
	/*
	 * Remove the handler from the chain.
	 * This is O(n^2), too.
	 */
	for (p = &eih->eih_ih; (q = *p) != NULL && q != ih; p = &q->ih_next)
		continue;
	if (q == NULL)
		panic("extintr_disestablish: handler not registered");

	*p = q->ih_next;

#if 0
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
#endif

	free(ih, M_DEVBUF, sizeof *ih);

	if (--eih->eih_nih == 0) {
		intc_intr_disestablish(eih->eih_func);

		/* Mask interrupt */
		_reg_bclr_1(LANDISK_INTEN, (1 << irq));
	}

	_cpu_intr_resume(s);
}

void
extintr_enable(void *aux)
{
	struct intrhand *ih = aux;
	struct intrhand *p, *q;
	struct extintr_handler *eih;
	int irq;
	int cnt;
	int s;

	KDASSERT(ih != NULL);

	s = _cpu_intr_suspend();

	irq = ih->ih_irq - 5;
	KDASSERT(irq >= 0 && irq < 8);
	eih = &extintr_handler[irq];
	for (cnt = 0, p = eih->eih_ih, q = NULL; p != NULL; p = p->ih_next) {
		if (p->ih_enable) {
			cnt++;
		}
		if (p == ih) {
			q = p;
			p->ih_enable = 1;
		}
	}
	KDASSERT(q != NULL);

	if (cnt == 0) {
		/* Unmask interrupt */
		_reg_bset_1(LANDISK_INTEN, (1 << irq));
	}

	_cpu_intr_resume(s);
}

void
extintr_disable(void *aux)
{
	struct intrhand *ih = aux;
	struct intrhand *p, *q;
	struct extintr_handler *eih;
	int irq;
	int cnt;
	int s;

	KDASSERT(ih != NULL);

	s = _cpu_intr_suspend();

	irq = ih->ih_irq - 5;
	KDASSERT(irq >= 0 && irq < 8);
	eih = &extintr_handler[irq];
	for (cnt = 0, p = eih->eih_ih, q = NULL; p != NULL; p = p->ih_next) {
		if (p == ih) {
			q = p;
			p->ih_enable = 0;
		}
		if (!ih->ih_enable) {
			cnt++;
		}
	}
	KDASSERT(q != NULL);

	if (cnt == 0) {
		/* Mask interrupt */
		_reg_bclr_1(LANDISK_INTEN, (1 << irq));
	}

	_cpu_intr_resume(s);
}

void
extintr_disable_by_num(int irq)
{
	struct extintr_handler *eih;
	struct intrhand *ih;
	int s;

	irq -= 5;
	KDASSERT(irq >= 0 && irq < 8);

	s = _cpu_intr_suspend();
	eih = &extintr_handler[irq];
	for (ih = eih->eih_ih; ih != NULL; ih = ih->ih_next) {
		ih->ih_enable = 0;
	}
	/* Mask interrupt */
	_reg_bclr_1(LANDISK_INTEN, (1 << irq));
	_cpu_intr_resume(s);
}

static int
fakeintr(void *arg)
{
	return 0;
}

static int
extintr_intr_handler(void *arg)
{
	struct extintr_handler *eih = arg;
	struct intrhand *ih;
	int r;

	if (__predict_true(eih != NULL)) {
		for (ih = eih->eih_ih; ih != NULL; ih = ih->ih_next) {
			if (__predict_true(ih->ih_enable)) {
				r = (*ih->ih_fun)(ih->ih_arg);
				if (__predict_true(r != 0)) {
					ih->ih_count.ec_count++;
					if (r == 1)
						break;
				}
			}
		}
		return 1;
	}
	return 0;
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	register_t sr;
        int oldipl;

	__asm__ volatile ("stc sr,%0" : "=r" (sr));

	oldipl = (sr & 0xf0) >> 4;
        if (oldipl < wantipl) {
                splassert_fail(wantipl, oldipl, func);
                /*
                 * If the splassert_ctl is set to not panic, raise the ipl
                 * in a feeble attempt to reduce damage.
                 */
		_cpu_intr_raise(wantipl << 4);
        }
}
#endif
