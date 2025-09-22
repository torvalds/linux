/*	$OpenBSD: loongson3_intr.c,v 1.8 2022/08/22 00:35:07 cheloha Exp $	*/

/*
 * Copyright (c) 2016 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Loongson 3A interrupt handling.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/intr.h>
#include <machine/loongson3.h>

#include <mips64/mips_cpu.h>

uint32_t loongson3_ht_intr(uint32_t, struct trapframe *);
uint32_t loongson3_intr(uint32_t, struct trapframe *);
void	 loongson3_splx(int);

const struct pic	*loongson3_ht_pic;
paddr_t			 loongson3_ht_cfg_base;

#define HT_REGVAL(offset)	REGVAL32(loongson3_ht_cfg_base + (offset))

/*
 * Interrupt handlers are sorted by interrupt priority level.
 */

struct intrhand		*loongson3_ht_intrhand[LS3_HT_IRQ_NUM];
uint32_t		 loongson3_ht_imask[NIPLS];
uint32_t		 loongson3_ht_intem;

struct intrhand		*loongson3_intrhand[LS3_IRQ_NUM];
uint32_t		 loongson3_imask[NIPLS];
uint32_t		 loongson3_intem;

static inline int
next_irq(uint32_t *isr)
{
	uint64_t tmp = *isr;
	uint32_t irq;

	if (tmp == 0)
		return -1;

	asm volatile (
	"	.set push\n"
	"	.set mips64\n"
	"	dclz	%0, %0\n"
	"	.set pop\n"
	: "=r" (tmp) : "0" (tmp));

	irq = 63u - tmp;
	*isr &= ~(1u << irq);
	return irq;
}

void
loongson3_intr_init(void)
{
	uint32_t boot_core = LS3_COREID(loongson3_get_cpuid());
	int ht_node = 0;
	int core, node;
	int i, ipl, irq;

	if (loongson_ver == 0x3b)
		ht_node = 1;
	loongson3_ht_cfg_base = LS3_HT1_CFG_BASE(ht_node);

	for (node = 0; node < nnodes; node++) {
		/* Disable all interrupts. */
		REGVAL(LS3_IRT_INTENCLR(node)) = ~0u;

		/* Disable all HT interrupts and ack any pending ones. */
		for (i = 0; i < 8; i++) {
			HT_REGVAL(LS3_HT_IMR_OFFSET(node)) = 0;
			HT_REGVAL(LS3_HT_ISR_OFFSET(node)) = ~0u;
		}

		/* Disable IPIs on every core. */
		for (core = 0; core < 4; core++)
			REGVAL32(LS3_IPI_BASE(node, core) + LS3_IPI_IMR) = 0u;
	}

	/*
	 * On the boot node, route all interrupts to the boot core.
	 * Assign a separate priority level to HT interrupts to process them
	 * in a dedicated handler.
	 */
	for (irq = 0; irq < LS3_IRQ_NUM; irq++) {
		if (LS3_IRQ_IS_HT(irq))
			ipl = 0;
		else
			ipl = 1;
		REGVAL8(LS3_IRT_ENTRY(0, irq)) = LS3_IRT_ROUTE(boot_core, ipl);
	}

	/* Enable HT interrupt vectors 0-63 on the boot node's router. */
	loongson3_intem |= 1u << LS3_IRQ_HT1(0);

	register_splx_handler(loongson3_splx);
	set_intr(INTPRI_CLOCK + 1, CR_INT_1, loongson3_intr);
	set_intr(INTPRI_CLOCK + 2, CR_INT_0, loongson3_ht_intr);
}

void
loongson3_prop_imask(uint32_t *imask)
{
	imask[IPL_NONE] = 0;
	imask[IPL_NET] |= imask[IPL_BIO];
	imask[IPL_TTY] |= imask[IPL_NET];
	imask[IPL_VM] |= imask[IPL_TTY];
	imask[IPL_CLOCK] |= imask[IPL_VM];
	imask[IPL_HIGH] |= imask[IPL_CLOCK];
	imask[IPL_IPI] |= imask[IPL_HIGH];
}

void
loongson3_update_imask(void)
{
	uint32_t ipls[LS3_IRQ_NUM];
	uint32_t ipls_ht[LS3_HT_IRQ_NUM];
	struct intrhand *ih;
	register_t sr;
	int irq, level;

	sr = disableintr();

	for (irq = 0; irq < LS3_IRQ_NUM; irq++) {
		ipls[irq] = 0;
		for (ih = loongson3_intrhand[irq]; ih != NULL; ih = ih->ih_next)
			ipls[irq] |= 1u << ih->ih_level;
	}
	for (irq = 0; irq < LS3_HT_IRQ_NUM; irq++) {
		ipls_ht[irq] = 0;
		for (ih = loongson3_ht_intrhand[irq]; ih != NULL;
		    ih = ih->ih_next) {
			ipls_ht[irq] |= 1u << ih->ih_level;
			ipls[LS3_IRQ_HT1(0)] |= 1u << ih->ih_level;
		}
	}

	for (level = IPL_NONE; level < NIPLS; level++) {
		loongson3_imask[level] = 0;
		for (irq = 0; irq < LS3_IRQ_NUM; irq++) {
			if (ipls[irq] & (1u << level))
				loongson3_imask[level] |= 1u << irq;
		}

		loongson3_ht_imask[level] = 0;
		for (irq = 0; irq < LS3_HT_IRQ_NUM; irq++) {
			if (ipls_ht[irq] & (1u << level))
				loongson3_ht_imask[level] |= 1u << irq;
		}
	}

	loongson3_prop_imask(loongson3_imask);
	loongson3_prop_imask(loongson3_ht_imask);

	setsr(sr);
}

void
loongson3_intr_insert(struct intrhand **list, struct intrhand *ih, int level)
{
	struct intrhand *next, *prev;

	if (*list != NULL) {
		for (prev = NULL, next = *list;
		    next != NULL && next->ih_level >= level;
		    prev = next, next = next->ih_next)
			continue;
		if (prev != NULL)
			prev->ih_next = ih;
		else
			*list = ih;
		ih->ih_next = next;
	} else
		*list = ih;
}

void
loongson3_intr_remove(struct intrhand **list, struct intrhand *ih)
{
	struct intrhand *prev;

	if (*list == ih) {
		*list = (*list)->ih_next;
	} else {
		for (prev = *list; prev != NULL; prev = prev->ih_next) {
			if (prev->ih_next == ih) {
				prev->ih_next = ih->ih_next;
				break;
			}
		}
		if (prev == NULL)
			panic("%s: intrhand %p has not been registered",
			    __func__, ih);
	}
}

void *
loongson3_intr_establish(int irq, int level, int (*func)(void *), void *arg,
    const char *name)
{
	struct intrhand *ih;
	register_t sr;
	int flags, s;

	if ((unsigned int)irq >= LS3_IRQ_NUM || LS3_IRQ_IS_HT(irq))
		return NULL;

	flags = (level & IPL_MPSAFE) ? IH_MPSAFE : 0;
	level &= ~IPL_MPSAFE;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return NULL;

	ih->ih_next = NULL;
	ih->ih_fun = func;
	ih->ih_arg = arg;
	ih->ih_level = level;
	ih->ih_irq = irq;
	ih->ih_flags = flags;
	evcount_attach(&ih->ih_count, name, (void *)&ih->ih_irq);

	sr = disableintr();
	s = splhigh();

	loongson3_intr_insert(&loongson3_intrhand[irq], ih, level);

	loongson3_intem |= 1u << irq;
	loongson3_update_imask();

	HT_REGVAL(LS3_HT_IMR_OFFSET(0)) = loongson3_ht_intem;

	splx(s);
	setsr(sr);

	return ih;
}

void
loongson3_intr_disestablish(void *ihp)
{
	struct intrhand *ih = ihp;
	register_t sr;
	int s;

	sr = disableintr();
	s = splhigh();

	loongson3_intr_remove(&loongson3_intrhand[ih->ih_irq], ih);
	free(ih, M_DEVBUF, sizeof(*ih));

	loongson3_update_imask();
	splx(s);
	setsr(sr);
}

void *
loongson3_ht_intr_establish(int irq, int level, int (*func)(void *), void *arg,
    const char *name)
{
	struct intrhand *ih;
	register_t sr;
	int flags, s;

	if ((unsigned int)irq >= LS3_HT_IRQ_NUM)
		return NULL;

	flags = (level & IPL_MPSAFE) ? IH_MPSAFE : 0;
	level &= ~IPL_MPSAFE;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return NULL;

	ih->ih_next = NULL;
	ih->ih_fun = func;
	ih->ih_arg = arg;
	ih->ih_level = level;
	ih->ih_irq = irq;
	ih->ih_flags = flags;
	evcount_attach(&ih->ih_count, name, (void *)&ih->ih_irq);

	sr = disableintr();
	s = splhigh();

	loongson3_intr_insert(&loongson3_ht_intrhand[irq], ih, level);

	loongson3_ht_intem |= 1u << irq;
	loongson3_update_imask();

	loongson3_ht_pic->pic_unmask(irq);

	splx(s);
	setsr(sr);

	return ih;
}

void
loongson3_ht_intr_disestablish(void *ihp)
{
	struct intrhand *ih = ihp;
	register_t sr;
	int irq = ih->ih_irq;
	int s;

	sr = disableintr();
	s = splhigh();

	loongson3_intr_remove(&loongson3_ht_intrhand[irq], ih);
	free(ih, M_DEVBUF, sizeof(*ih));

	if (loongson3_ht_intrhand[irq] == NULL) {
		loongson3_ht_pic->pic_mask(irq);
		loongson3_ht_intem &= ~(1u << irq);
	}

	loongson3_update_imask();

	splx(s);
	setsr(sr);
}

void
loongson3_splx(int newipl)
{
	struct cpu_info *ci = curcpu();

	ci->ci_ipl = newipl;

	if (CPU_IS_PRIMARY(ci))
		REGVAL(LS3_IRT_INTENSET(0)) =
		    loongson3_intem & ~loongson3_imask[newipl];

	/* Trigger deferred clock interrupt if it is now unmasked. */
	if (ci->ci_clock_deferred && newipl < IPL_CLOCK)
		md_triggerclock();

	if (ci->ci_softpending != 0 && newipl < IPL_SOFTINT)
		setsoftintr0();
}

uint32_t
loongson3_intr(uint32_t pending, struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	uint32_t imr, isr, mask;
	int handled;
	int ipl, irq;
#ifdef MULTIPROCESSOR
	register_t sr;
	int need_lock;
#endif

	isr = REGVAL(LS3_IRT_INTISR(0));
	imr = REGVAL(LS3_IRT_INTEN(0)) & ~LS3_IRQ_HT_MASK;

	isr &= imr;
	if (isr == 0)
		return 0;

	/* Mask pending interrupts. */
	REGVAL(LS3_IRT_INTENCLR(0)) = isr;

	if ((mask = isr & loongson3_imask[frame->ipl]) != 0) {
		isr &= ~mask;
		imr &= ~mask;
	}
	if (isr == 0)
		return pending;

	ipl = ci->ci_ipl;

	while ((irq = next_irq(&isr)) >= 0) {
		handled = 0;
		for (ih = loongson3_intrhand[irq]; ih != NULL;
		    ih = ih->ih_next) {
			splraise(ih->ih_level);
#ifdef MULTIPROCESSOR
			if (ih->ih_level < IPL_IPI) {
				sr = getsr();
				ENABLEIPI();
			}
			if (ih->ih_flags & IH_MPSAFE)
				need_lock = 0;
			else
				need_lock = 1;
			if (need_lock)
				__mp_lock(&kernel_lock);
#endif
			if (ih->ih_fun(ih->ih_arg) != 0) {
				atomic_inc_long((unsigned long *)
				    &ih->ih_count.ec_count);
				handled = 1;
			}
#ifdef MULTIPROCESSOR
			if (need_lock)
				__mp_unlock(&kernel_lock);
			if (ih->ih_level < IPL_IPI)
				setsr(sr);
#endif
		}
		if (!handled)
			printf("spurious interrupt %d\n", irq);
	}

	ci->ci_ipl = ipl;

	/* Re-enable processed interrupts. */
	REGVAL(LS3_IRT_INTENSET(0)) = imr;

	return pending;
}

uint32_t
loongson3_ht_intr(uint32_t pending, struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	uint32_t imr, isr, mask;
	int handled;
	int ipl, irq;
#ifdef MULTIPROCESSOR
	register_t sr;
	int need_lock;
#endif

	isr = HT_REGVAL(LS3_HT_ISR_OFFSET(0));
	imr = HT_REGVAL(LS3_HT_IMR_OFFSET(0));

	isr &= imr;
	if (isr == 0)
		return 0;

	/* Mask pending HT interrupts. */
	REGVAL(LS3_IRT_INTENCLR(0)) = 1u << LS3_IRQ_HT1(0);

	if ((mask = isr & loongson3_ht_imask[frame->ipl]) != 0) {
		isr &= ~mask;
		imr &= ~mask;
	}
	if (isr == 0)
		return pending;

	/* Acknowledge HT interrupts that will be processed. */
	HT_REGVAL(LS3_HT_ISR_OFFSET(0)) = isr;

	ipl = ci->ci_ipl;

	while ((irq = next_irq(&isr)) >= 0) {
		handled = 0;
		for (ih = loongson3_ht_intrhand[irq]; ih != NULL;
		    ih = ih->ih_next) {
			splraise(ih->ih_level);
#ifdef MULTIPROCESSOR
			if (ih->ih_level < IPL_IPI) {
				sr = getsr();
				ENABLEIPI();
			}
			if (ih->ih_flags & IH_MPSAFE)
				need_lock = 0;
			else
				need_lock = 1;
			if (need_lock)
				__mp_lock(&kernel_lock);
#endif
			if (ih->ih_fun(ih->ih_arg) != 0) {
				atomic_inc_long((unsigned long *)
				    &ih->ih_count.ec_count);
				handled = 1;
			}
#ifdef MULTIPROCESSOR
			if (need_lock)
				__mp_unlock(&kernel_lock);
			if (ih->ih_level < IPL_IPI)
				setsr(sr);
#endif
		}
#ifdef notyet
		if (!handled)
			printf("spurious HT interrupt %d\n", irq);
#endif

		loongson3_ht_pic->pic_eoi(irq);
	}

	ci->ci_ipl = ipl;

	/* Re-enable HT interrupts. */
	REGVAL(LS3_IRT_INTENSET(0)) = 1u << LS3_IRQ_HT1(0);

	return pending;
}

void
loongson3_register_ht_pic(const struct pic *pic)
{
	loongson3_ht_pic = pic;
}
