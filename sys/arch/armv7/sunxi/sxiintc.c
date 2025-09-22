/*	$OpenBSD: sxiintc.c,v 1.13 2025/05/10 10:11:02 visa Exp $	*/
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2013 Artturi Alm
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

#include <armv7/sunxi/sxiintc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#ifdef DEBUG_INTC
#define DPRINTF(x)	do { if (sxiintcdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (sxiintcdebug>(n)) printf x; } while (0)
int	sxiintcdebug = 10;
char *ipl_strtbl[NIPL] = {
	"IPL_NONE",
	"IPL_SOFT",		/* unused */
	"IPL_SOFTCLOCK",
	"IPL_SOFTNET",
	"IPL_SOFTTTY",
	"IPL_BIO|IPL_USB",
	"IPL_NET",
	"IPL_TTY",
	"IPL_VM",
	"IPL_AUDIO",
	"IPL_CLOCK",
	"IPL_STATCLOCK",
	"IPL_SCHED|IPL_HIGH"
};
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define NIRQ			96
#define NBANKS			3
#define NIRQPRIOREGS		5

/* registers */
#define INTC_VECTOR_REG		0x00
#define INTC_BASE_ADR_REG	0x04
#define INTC_PROTECTION_REG	0x08
#define INTC_NMI_CTRL_REG	0x0c

#define INTC_IRQ_PENDING_REG0	0x10
#define INTC_IRQ_PENDING_REG1	0x14
#define INTC_IRQ_PENDING_REG2	0x18

#define INTC_SELECT_REG0	0x30
#define INTC_SELECT_REG1	0x34
#define INTC_SELECT_REG2	0x38

#define INTC_ENABLE_REG0	0x40
#define INTC_ENABLE_REG1	0x44
#define INTC_ENABLE_REG2	0x48

#define INTC_MASK_REG0		0x50
#define INTC_MASK_REG1		0x54
#define INTC_MASK_REG2		0x58

#define INTC_RESP_REG0		0x60
#define INTC_RESP_REG1		0x64
#define INTC_RESP_REG2		0x68

#define INTC_PRIO_REG0		0x80
#define INTC_PRIO_REG1		0x84
#define INTC_PRIO_REG2		0x88
#define INTC_PRIO_REG3		0x8c
#define INTC_PRIO_REG4		0x90

#define INTC_IRQ_PENDING_REG(_b)	(0x10 + ((_b) * 4))
#define INTC_FIQ_PENDING_REG(_b)	(0x20 + ((_b) * 4))
#define INTC_SELECT_REG(_b)		(0x30 + ((_b) * 4))
#define INTC_ENABLE_REG(_b)		(0x40 + ((_b) * 4))
#define INTC_MASK_REG(_b)		(0x50 + ((_b) * 4))
#define INTC_RESP_REG(_b)		(0x60 + ((_b) * 4))
#define INTC_PRIO_REG(_b)		(0x80 + ((_b) * 4))

#define IRQ2REG32(i)		(((i) >> 5) & 0x3)
#define IRQ2BIT32(i)		((i) & 0x1f)

#define IRQ2REG16(i)		(((i) >> 4) & 0x5)
#define IRQ2BIT16(i)		(((i) & 0x0f) * 2)

#define INTC_IRQ_HIPRIO		0x3
#define INTC_IRQ_ENABLED	0x2
#define INTC_IRQ_DISABLED	0x1
#define INTC_IRQ_LOWPRIO	0x0
#define INTC_PRIOCLEAR(i)	(~(INTC_IRQ_HIPRIO << IRQ2BIT16((i))))
#define INTC_PRIOENABLE(i)	(INTC_IRQ_ENABLED << IRQ2BIT16((i)))
#define INTC_PRIOHI(i)		(INTC_IRQ_HIPRIO << IRQ2BIT16((i)))


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

struct intrq sxiintc_handler[NIRQ];
u_int32_t sxiintc_smask[NIPL];
u_int32_t sxiintc_imask[NBANKS][NIPL];
struct interrupt_controller sxiintc_ic;

bus_space_tag_t		sxiintc_iot;
bus_space_handle_t	sxiintc_ioh;
int			sxiintc_nirq;

int	sxiintc_match(struct device *, void *, void *);
void	sxiintc_attach(struct device *, struct device *, void *);
int	sxiintc_spllower(int);
int	sxiintc_splraise(int);
void	sxiintc_setipl(int);
void	sxiintc_calc_masks(void);
void	*sxiintc_intr_establish_fdt(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);

const struct cfattach	sxiintc_ca = {
	sizeof (struct device), sxiintc_match, sxiintc_attach
};

struct cfdriver sxiintc_cd = {
	NULL, "sxiintc", DV_DULL
};

int sxiintc_attached = 0;

int
sxiintc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-ic");
}

void
sxiintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i, j;

	sxiintc_iot = faa->fa_iot;
	if (bus_space_map(sxiintc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sxiintc_ioh))
		panic("sxiintc_attach: bus_space_map failed!");

	/* disable/mask/clear all interrupts */
	for (i = 0; i < NBANKS; i++) {
		bus_space_write_4(sxiintc_iot, sxiintc_ioh, INTC_ENABLE_REG(i), 0);
		bus_space_write_4(sxiintc_iot, sxiintc_ioh, INTC_MASK_REG(i), 0);
		bus_space_write_4(sxiintc_iot, sxiintc_ioh, INTC_IRQ_PENDING_REG(i),
		    0xffffffff);
		for (j = 0; j < NIPL; j++)
			sxiintc_imask[i][j] = 0;
	}

	/* XXX */
	bus_space_write_4(sxiintc_iot, sxiintc_ioh, INTC_PROTECTION_REG, 1);
	bus_space_write_4(sxiintc_iot, sxiintc_ioh, INTC_NMI_CTRL_REG, 0);

	for (i = 0; i < NIRQ; i++)
		TAILQ_INIT(&sxiintc_handler[i].iq_list);

	sxiintc_calc_masks();

	arm_init_smask();
	sxiintc_attached = 1;

	/* insert self as interrupt handler */
	arm_set_intr_handler(sxiintc_splraise, sxiintc_spllower, sxiintc_splx,
	    sxiintc_setipl,
	    sxiintc_intr_establish, sxiintc_intr_disestablish, sxiintc_intr_string,
	    sxiintc_irq_handler);
	sxiintc_setipl(IPL_HIGH);  /* XXX ??? */
	enable_interrupts(PSR_I);
	printf("\n");

	sxiintc_ic.ic_node = faa->fa_node;
	sxiintc_ic.ic_establish = sxiintc_intr_establish_fdt;
	arm_intr_register_fdt(&sxiintc_ic);
}

void
sxiintc_calc_masks(void)
{
	struct cpu_info *ci = curcpu();
	int irq;
	struct intrhand *ih;
	int i;

	for (irq = 0; irq < NIRQ; irq++) {
		int max = IPL_NONE;
		int min = IPL_HIGH;
		TAILQ_FOREACH(ih, &sxiintc_handler[irq].iq_list, ih_list) {
			if (ih->ih_ipl > max)
				max = ih->ih_ipl;
			if (ih->ih_ipl < min)
				min = ih->ih_ipl;
		}

		sxiintc_handler[irq].iq_irq = max;

		if (max == IPL_NONE)
			min = IPL_NONE;

#ifdef DEBUG_INTC
		if (min != IPL_NONE) {
			printf("irq %d to block at %d %d reg %d bit %d\n",
			    irq, max, min, IRQ2REG32(irq),
			    IRQ2BIT32(irq));
		}
#endif
		/* Enable interrupts at lower levels, clear -> enable */
		for (i = 0; i < min; i++)
			sxiintc_imask[IRQ2REG32(irq)][i] &=
			    ~(1 << IRQ2BIT32(irq));
		for (; i < NIPL; i++)
			sxiintc_imask[IRQ2REG32(irq)][i] |=
			    (1 << IRQ2BIT32(irq));
		/* XXX - set enable/disable, priority */
	}

	sxiintc_setipl(ci->ci_cpl);
}

void
sxiintc_splx(int new)
{
	struct cpu_info *ci = curcpu();
	sxiintc_setipl(new);

	if (ci->ci_ipending & arm_smask[ci->ci_cpl])
		arm_do_pending_intr(ci->ci_cpl);
}

int
sxiintc_spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;
	sxiintc_splx(new);
	return (old);
}

int
sxiintc_splraise(int new)
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

	sxiintc_setipl(new);

	return (old);
}

void
sxiintc_setipl(int new)
{
	struct cpu_info *ci = curcpu();
	int i, psw;
#if 1
	/*
	 * XXX not needed, because all interrupts are disabled
	 * by default, so touching maskregs has no effect, i hope.
	 */
	if (sxiintc_attached == 0) {
		ci->ci_cpl = new;
		return;
	}
#endif
	psw = disable_interrupts(PSR_I);
	ci->ci_cpl = new;
	for (i = 0; i < NBANKS; i++)
		bus_space_write_4(sxiintc_iot, sxiintc_ioh,
		    INTC_MASK_REG(i), sxiintc_imask[i][new]);
	restore_interrupts(psw);
}

void
sxiintc_irq_handler(void *frame)
{
	struct intrhand *ih;
	void *arg;
	uint32_t pr;
	int irq, prio, s;

	irq = bus_space_read_4(sxiintc_iot, sxiintc_ioh, INTC_VECTOR_REG) >> 2;
	if (irq == 0)
		return;

	prio = sxiintc_handler[irq].iq_irq;
	s = sxiintc_splraise(prio);
	splassert(prio);

	pr = bus_space_read_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)));
	bus_space_write_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)),
	    pr & ~(1 << IRQ2BIT32(irq)));

	/* clear pending */
	pr = bus_space_read_4(sxiintc_iot, sxiintc_ioh,
	    INTC_IRQ_PENDING_REG(IRQ2REG32(irq)));
	bus_space_write_4(sxiintc_iot, sxiintc_ioh,
	    INTC_IRQ_PENDING_REG(IRQ2REG32(irq)),
	    pr | (1 << IRQ2BIT32(irq)));

	pr = bus_space_read_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)));
	bus_space_write_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)),
	    pr | (1 << IRQ2BIT32(irq)));

	TAILQ_FOREACH(ih, &sxiintc_handler[irq].iq_list, ih_list) {
		if (ih->ih_arg)
			arg = ih->ih_arg;
		else
			arg = frame;

		if (ih->ih_func(arg))
			ih->ih_count.ec_count++;
	}
	sxiintc_splx(s);
}

void *
sxiintc_intr_establish(int irq, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	int psw;
	struct intrhand *ih;
	uint32_t er;

	if (irq <= 0 || irq >= NIRQ)
		panic("intr_establish: bogus irq %d %s", irq, name);

	if (ci == NULL)
		ci = &cpu_info_primary;
	else if (!CPU_IS_PRIMARY(ci))
		return NULL;

	DPRINTF(("intr_establish: irq %d level %d [%s]\n", irq, level,
	    name != NULL ? name : "NULL"));

	psw = disable_interrupts(PSR_I);

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = irq;
	ih->ih_name = name;

	TAILQ_INSERT_TAIL(&sxiintc_handler[irq].iq_list, ih, ih_list);

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	er = bus_space_read_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)));
	bus_space_write_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)),
	    er | (1 << IRQ2BIT32(irq)));

	sxiintc_calc_masks();
	
	restore_interrupts(psw);
	return (ih);
}

void *
sxiintc_intr_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	return sxiintc_intr_establish(cell[0], level, ci, func, arg, name);
}

void
sxiintc_intr_disestablish(void *cookie)
{
	struct intrhand *ih = cookie;
	int irq = ih->ih_irq;
	int psw;
	uint32_t er;

	psw = disable_interrupts(PSR_I);

	TAILQ_REMOVE(&sxiintc_handler[irq].iq_list, ih, ih_list);

	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);

	free(ih, M_DEVBUF, 0);

	er = bus_space_read_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)));
	bus_space_write_4(sxiintc_iot, sxiintc_ioh,
	    INTC_ENABLE_REG(IRQ2REG32(irq)),
	    er & ~(1 << IRQ2BIT32(irq)));

	sxiintc_calc_masks();

	restore_interrupts(psw);
}

const char *
sxiintc_intr_string(void *cookie)
{
	return "asd?";
}
