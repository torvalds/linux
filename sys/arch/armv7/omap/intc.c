/* $OpenBSD: intc.c,v 1.15 2024/06/26 01:40:49 jsg Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <armv7/armv7/armv7var.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include "intc.h"

#define INTC_NUM_IRQ intc_nirq
#define INTC_NUM_BANKS (intc_nirq/32)
#define INTC_MAX_IRQ 128
#define INTC_MAX_BANKS (INTC_MAX_IRQ/32)

/* registers */
#define	INTC_REVISION		0x00	/* R */
#define	INTC_SYSCONFIG		0x10	/* RW */
#define		INTC_SYSCONFIG_AUTOIDLE		0x1
#define		INTC_SYSCONFIG_SOFTRESET	0x2
#define	INTC_SYSSTATUS		0x14	/* R */
#define		INTC_SYSSYSTATUS_RESETDONE	0x1
#define	INTC_SIR_IRQ		0x40	/* R */	
#define	INTC_SIR_FIQ		0x44	/* R */
#define	INTC_CONTROL		0x48	/* RW */
#define		INTC_CONTROL_NEWIRQ	0x1
#define		INTC_CONTROL_NEWFIQ	0x2
#define		INTC_CONTROL_GLOBALMASK	0x1
#define	INTC_PROTECTION		0x4c	/* RW */
#define		INTC_PROTECTION_PROT 1	/* only privileged mode */
#define	INTC_IDLE		0x50	/* RW */

#define INTC_IRQ_TO_REG(i)	(((i) >> 5) & 0x3)
#define INTC_IRQ_TO_REGi(i)	((i) & 0x1f)
#define	INTC_ITRn(i)		0x80+(0x20*i)+0x00	/* R */
#define	INTC_MIRn(i)		0x80+(0x20*i)+0x04	/* RW */
#define	INTC_CLEARn(i)		0x80+(0x20*i)+0x08	/* RW */
#define	INTC_SETn(i)		0x80+(0x20*i)+0x0c	/* RW */
#define	INTC_ISR_SETn(i)	0x80+(0x20*i)+0x10	/* RW */
#define	INTC_ISR_CLEARn(i)	0x80+(0x20*i)+0x14	/* RW */
#define INTC_PENDING_IRQn(i)	0x80+(0x20*i)+0x18	/* R */
#define INTC_PENDING_FIQn(i)	0x80+(0x20*i)+0x1c	/* R */

#define INTC_ILRn(i)		0x100+(4*i)
#define		INTC_ILR_IRQ	0x0		/* not of FIQ */
#define		INTC_ILR_FIQ	0x1
#define		INTC_ILR_PRIs(pri)	((pri) << 2)
#define		INTC_ILR_PRI(reg)	(((reg) >> 2) & 0x2f)
#define		INTC_MIN_PRI	63
#define		INTC_STD_PRI	32
#define		INTC_MAX_PRI	0

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;	/* link on intrq list */
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_irq;			/* IRQ number */
	struct evcount	ih_count;
	char *ih_name;
};

struct intrq {
	TAILQ_HEAD(, intrhand) iq_list;	/* handler list */
	int iq_irq;			/* IRQ to mask while handling */
	int iq_levels;			/* IPL_*'s this IRQ has */
	int iq_ist;			/* share type */
};

struct intrq intc_handler[INTC_MAX_IRQ];
u_int32_t intc_smask[NIPL];
u_int32_t intc_imask[INTC_MAX_BANKS][NIPL];
struct interrupt_controller intc_ic;

bus_space_tag_t		intc_iot;
bus_space_handle_t	intc_ioh;
int			intc_nirq;

int	intc_match(struct device *, void *, void *);
void	intc_attach(struct device *, struct device *, void *);
int	intc_spllower(int new);
int	intc_splraise(int new);
void	intc_setipl(int new);
void	intc_calc_mask(void);
void	*intc_intr_establish_fdt(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);

const struct cfattach	intc_ca = {
	sizeof (struct device), intc_match, intc_attach
};

struct cfdriver intc_cd = {
	NULL, "intc", DV_DULL
};

int intc_attached = 0;

int
intc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "ti,omap3-intc") ||
	    OF_is_compatible(faa->fa_node, "ti,am33xx-intc"));
}

void
intc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;
	u_int32_t rev;

	intc_iot = faa->fa_iot;
	if (bus_space_map(intc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &intc_ioh))
		panic("intc_attach: bus_space_map failed!");

	rev = bus_space_read_4(intc_iot, intc_ioh, INTC_REVISION);

	printf(" rev %d.%d\n", rev >> 4 & 0xf, rev & 0xf);

	/* software reset of the part? */
	/* set protection bit (kernel only)? */
#if 0
	bus_space_write_4(intc_iot, intc_ioh, INTC_PROTECTION,
	     INTC_PROTECTION_PROT);
#endif

	/* enable interface clock power saving mode */
	bus_space_write_4(intc_iot, intc_ioh, INTC_SYSCONFIG,
	    INTC_SYSCONFIG_AUTOIDLE);

	if (OF_is_compatible(faa->fa_node, "ti,am33xx-intc"))
		intc_nirq = 128;
	else
		intc_nirq = 96;

	/* mask all interrupts */
	for (i = 0; i < INTC_NUM_BANKS; i++)
		bus_space_write_4(intc_iot, intc_ioh, INTC_MIRn(i), 0xffffffff);

	for (i = 0; i < INTC_NUM_IRQ; i++) {
		bus_space_write_4(intc_iot, intc_ioh, INTC_ILRn(i),
		    INTC_ILR_PRIs(INTC_MIN_PRI)|INTC_ILR_IRQ);

		TAILQ_INIT(&intc_handler[i].iq_list);
	}

	intc_calc_mask();
	bus_space_write_4(intc_iot, intc_ioh, INTC_CONTROL,
	    INTC_CONTROL_NEWIRQ);

	intc_attached = 1;

	/* insert self as interrupt handler */
	arm_set_intr_handler(intc_splraise, intc_spllower, intc_splx,
	    intc_setipl,
	    intc_intr_establish, intc_intr_disestablish, intc_intr_string,
	    intc_irq_handler);

	intc_setipl(IPL_HIGH);  /* XXX ??? */
	enable_interrupts(PSR_I);

	intc_ic.ic_node = faa->fa_node;
	intc_ic.ic_establish = intc_intr_establish_fdt;
	arm_intr_register_fdt(&intc_ic);
}

void
intc_calc_mask(void)
{
	struct cpu_info *ci = curcpu();
	int irq;
	struct intrhand *ih;
	int i;

	for (irq = 0; irq < INTC_NUM_IRQ; irq++) {
		int max = IPL_NONE;
		int min = IPL_HIGH;
		TAILQ_FOREACH(ih, &intc_handler[irq].iq_list, ih_list) {
			if (ih->ih_ipl > max)
				max = ih->ih_ipl;

			if (ih->ih_ipl < min)
				min = ih->ih_ipl;
		}

		intc_handler[irq].iq_irq = max;

		if (max == IPL_NONE)
			min = IPL_NONE;

#ifdef DEBUG_INTC
		if (min != IPL_NONE) {
			printf("irq %d to block at %d %d reg %d bit %d\n",
			    irq, max, min, INTC_IRQ_TO_REG(irq),
			    INTC_IRQ_TO_REGi(irq));
		}
#endif
		/* Enable interrupts at lower levels, clear -> enable */
		for (i = 0; i < min; i++)
			intc_imask[INTC_IRQ_TO_REG(irq)][i] &=
			    ~(1 << INTC_IRQ_TO_REGi(irq));
		for (; i <= IPL_HIGH; i++)
			intc_imask[INTC_IRQ_TO_REG(irq)][i] |=
			    1 << INTC_IRQ_TO_REGi(irq);
		/* XXX - set enable/disable, priority */
		bus_space_write_4(intc_iot, intc_ioh, INTC_ILRn(irq),
		    INTC_ILR_PRIs(NIPL-max)|INTC_ILR_IRQ);
	}
	arm_init_smask();
	intc_setipl(ci->ci_cpl);
}

void
intc_splx(int new)
{
	struct cpu_info *ci = curcpu();
	intc_setipl(new);

	if (ci->ci_ipending & arm_smask[ci->ci_cpl])
		arm_do_pending_intr(ci->ci_cpl);
}

int
intc_spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;
	intc_splx(new);
	return (old);
}

int
intc_splraise(int new)
{
	struct cpu_info *ci = curcpu();
	int old;
	old = ci->ci_cpl;

	/*
	 * setipl must always be called because there is a race window
	 * where the variable is updated before the mask is set
	 * an interrupt occurs in that window without the mask always
	 * being set, the hardware might not get updated on the next
	 * splraise completely messing up spl protection.
	 */
	if (old > new)
		new = old;

	intc_setipl(new);
 
	return (old);
}

void
intc_setipl(int new)
{
	struct cpu_info *ci = curcpu();
	int i;
	int psw;
	if (intc_attached == 0)
		return;

	psw = disable_interrupts(PSR_I);
#if 0
	{
		volatile static int recursed = 0;
		if (recursed == 0) {
			recursed = 1;
			if (new != 12)
				printf("setipl %d\n", new);
			recursed = 0;
		}
	}
#endif
	ci->ci_cpl = new;
	for (i = 0; i < INTC_NUM_BANKS; i++)
		bus_space_write_4(intc_iot, intc_ioh,
		    INTC_MIRn(i), intc_imask[i][new]);
	bus_space_write_4(intc_iot, intc_ioh, INTC_CONTROL,
	    INTC_CONTROL_NEWIRQ);
	restore_interrupts(psw);
}

void
intc_irq_handler(void *frame)
{
	int irq, pri, s;
	struct intrhand *ih;
	void *arg;

	irq = bus_space_read_4(intc_iot, intc_ioh, INTC_SIR_IRQ);
#ifdef DEBUG_INTC
	printf("irq %d fired\n", irq);
#endif

	pri = intc_handler[irq].iq_irq;
	s = intc_splraise(pri);
	TAILQ_FOREACH(ih, &intc_handler[irq].iq_list, ih_list) {
		if (ih->ih_arg)
			arg = ih->ih_arg;
		else
			arg = frame;

		if (ih->ih_func(arg))
			ih->ih_count.ec_count++;

	}
	bus_space_write_4(intc_iot, intc_ioh, INTC_CONTROL,
	    INTC_CONTROL_NEWIRQ);

	intc_splx(s);
}

void *
intc_intr_establish(int irqno, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	int psw;
	struct intrhand *ih;

	if (irqno < 0 || irqno >= INTC_NUM_IRQ)
		panic("intc_intr_establish: bogus irqnumber %d: %s",
		     irqno, name);

	if (ci == NULL)
		ci = &cpu_info_primary;
	else if (!CPU_IS_PRIMARY(ci))
		return NULL;

	psw = disable_interrupts(PSR_I);

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = irqno;
	ih->ih_name = name;

	TAILQ_INSERT_TAIL(&intc_handler[irqno].iq_list, ih, ih_list);

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

#ifdef DEBUG_INTC
	printf("intc_intr_establish irq %d level %d [%s]\n", irqno, level,
	    name);
#endif
	intc_calc_mask();
	
	restore_interrupts(psw);
	return (ih);
}

void *
intc_intr_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	return intc_intr_establish(cell[0], level, ci, func, arg, name);
}

void
intc_intr_disestablish(void *cookie)
{
	int psw;
	struct intrhand *ih = cookie;
	int irqno = ih->ih_irq;
	psw = disable_interrupts(PSR_I);
	TAILQ_REMOVE(&intc_handler[irqno].iq_list, ih, ih_list);
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, 0);
	restore_interrupts(psw);
}

const char *
intc_intr_string(void *cookie)
{
	return "huh?";
}


#if 0
int intc_tst(void *a);

int
intc_tst(void *a)
{
	printf("inct_tst called\n");
	bus_space_write_4(intc_iot, intc_ioh, INTC_ISR_CLEARn(0), 2);
	return 1;
}

void intc_test(void);
void
intc_test(void)
{
	void * ih;
	printf("about to register handler\n");
	ih = intc_intr_establish(1, IPL_BIO, intc_tst, NULL, "intctst");

	printf("about to set bit\n");
	bus_space_write_4(intc_iot, intc_ioh, INTC_ISR_SETn(0), 2);

	printf("about to clear bit\n");
	bus_space_write_4(intc_iot, intc_ioh, INTC_ISR_CLEARn(0), 2);

	printf("about to remove handler\n");
	intc_intr_disestablish(ih);

	printf("done\n");
}
#endif
