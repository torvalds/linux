/* $OpenBSD: bcm2836_intr.c,v 1.17 2025/08/11 17:37:04 kettenis Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2015 Patrick Wildt <patrick@blueri.se>
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* registers */
#define	INTC_PENDING_BANK0	0x00
#define	INTC_PENDING_BANK1	0x04
#define	INTC_PENDING_BANK2	0x08
#define	INTC_FIQ_CONTROL	0x0C
#define	INTC_ENABLE_BANK1	0x10
#define	INTC_ENABLE_BANK2	0x14
#define	INTC_ENABLE_BANK0	0x18
#define	INTC_DISABLE_BANK1	0x1C
#define	INTC_DISABLE_BANK2	0x20
#define	INTC_DISABLE_BANK0	0x24

/* arm local */
#define	ARM_LOCAL_CONTROL		0x00
#define	ARM_LOCAL_PRESCALER		0x08
#define	 PRESCALER_19_2			0x80000000 /* 19.2 MHz */
#define	ARM_LOCAL_INT_TIMER(n)		(0x40 + (n) * 4)
#define	ARM_LOCAL_INT_MAILBOX(n)	(0x50 + (n) * 4)
#define	ARM_LOCAL_INT_PENDING(n)	(0x60 + (n) * 4)
#define	 ARM_LOCAL_INT_PENDING_MASK	0x0f
#define	ARM_LOCAL_INT_MAILBOX_SET(n)	(0x80 + (n) * 16)
#define	ARM_LOCAL_INT_MAILBOX_CLR(n)	(0xc0 + (n) * 16)

#define	BANK0_START	64
#define	BANK0_END	(BANK0_START + 32 - 1)
#define	BANK1_START	0
#define	BANK1_END	(BANK1_START + 32 - 1)
#define	BANK2_START	32
#define	BANK2_END	(BANK2_START + 32 - 1)
#define	LOCAL_START	96
#define	LOCAL_END	(LOCAL_START + 32 - 1)

#define	IS_IRQ_BANK0(n)	(((n) >= BANK0_START) && ((n) <= BANK0_END))
#define	IS_IRQ_BANK1(n)	(((n) >= BANK1_START) && ((n) <= BANK1_END))
#define	IS_IRQ_BANK2(n)	(((n) >= BANK2_START) && ((n) <= BANK2_END))
#define	IS_IRQ_LOCAL(n)	(((n) >= LOCAL_START) && ((n) <= LOCAL_END))
#define	IRQ_BANK0(n)	((n) - BANK0_START)
#define	IRQ_BANK1(n)	((n) - BANK1_START)
#define	IRQ_BANK2(n)	((n) - BANK2_START)
#define	IRQ_LOCAL(n)	((n) - LOCAL_START)

#define ARM_LOCAL_IRQ_MAILBOX(n) (4 + (n))

#define	INTC_NIRQ	128
#define	INTC_NBANK	4

#define INTC_IRQ_TO_REG(i)	(((i) >> 5) & 0x3)
#define INTC_IRQ_TO_REGi(i)	((i) & 0x1f)

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;	/* link on intrq list */
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_flags;
	int ih_irq;			/* IRQ number */
	struct evcount ih_count;	/* interrupt counter */
	char *ih_name;			/* device name */
};

struct intrsource {
	TAILQ_HEAD(, intrhand) is_list;	/* handler list */
	int is_irq;			/* IRQ to mask while handling */
};

struct bcm_intc_softc {
	struct device		 sc_dev;
	struct intrsource	 sc_handler[INTC_NIRQ];
	uint32_t		 sc_imask[INTC_NBANK][NIPL];
	int32_t			 sc_localcoremask[MAXCPUS];
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_space_handle_t	 sc_lioh;
	struct interrupt_controller sc_intc;
	struct interrupt_controller sc_l1_intc;
};
struct bcm_intc_softc *bcm_intc;

int	 bcm_intc_match(struct device *, void *, void *);
void	 bcm_intc_attach(struct device *, struct device *, void *);
void	 bcm_intc_splx(int new);
int	 bcm_intc_spllower(int new);
int	 bcm_intc_splraise(int new);
void	 bcm_intc_setipl(int new);
void	 bcm_intc_calc_mask(void);
void	*bcm_intc_intr_establish(int, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	*bcm_intc_intr_establish_fdt(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	*l1_intc_intr_establish_fdt(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	 bcm_intc_intr_disestablish(void *);
void	 bcm_intc_irq_handler(void *);
void	 bcm_intc_intr_route(void *, int , struct cpu_info *);
void	 bcm_intc_handle_ipi(void);
void	 bcm_intc_send_ipi(struct cpu_info *, int);

const struct cfattach	bcmintc_ca = {
	sizeof (struct bcm_intc_softc), bcm_intc_match, bcm_intc_attach
};

struct cfdriver bcmintc_cd = {
	NULL, "bcmintc", DV_DULL
};

int
bcm_intc_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2836-armctrl-ic"))
		return 1;

	return 0;
}

void
bcm_intc_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcm_intc_softc *sc = (struct bcm_intc_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t phandle, reg[2];
	int node;
	int i;

	if (faa->fa_nreg < 1)
		return;

	bcm_intc = sc;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/*
	 * ARM control logic.
	 *
	 * XXX Should really be implemented as a separate interrupt
	 * controller, but for now it is easier to handle it together
	 * with its BCM2835 partner.
	 */
	phandle = OF_getpropint(faa->fa_node, "interrupt-parent", 0);
	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		panic("%s: can't find ARM control logic", __func__);

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) != sizeof(reg))
		panic("%s: can't map ARM control logic", __func__);

	if (bus_space_map(sc->sc_iot, reg[0], reg[1], 0, &sc->sc_lioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	/* mask all interrupts */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK0,
	    0xffffffff);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK1,
	    0xffffffff);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK2,
	    0xffffffff);

	/* ARM control specific */
	bus_space_write_4(sc->sc_iot, sc->sc_lioh, ARM_LOCAL_CONTROL, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_lioh, ARM_LOCAL_PRESCALER,
	    PRESCALER_19_2);
	for (i = 0; i < 4; i++)
		bus_space_write_4(sc->sc_iot, sc->sc_lioh,
		    ARM_LOCAL_INT_TIMER(i), 0);
	for (i = 0; i < 4; i++)
		bus_space_write_4(sc->sc_iot, sc->sc_lioh,
		    ARM_LOCAL_INT_MAILBOX(i), 0);

	for (i = 0; i < INTC_NIRQ; i++) {
		TAILQ_INIT(&sc->sc_handler[i].is_list);
	}

	bcm_intc_calc_mask();

	/* insert self as interrupt handler */
	arm_set_intr_handler(bcm_intc_splraise, bcm_intc_spllower,
	    bcm_intc_splx, bcm_intc_setipl, bcm_intc_irq_handler, NULL,
	    NULL, NULL);

	sc->sc_intc.ic_node = faa->fa_node;
	sc->sc_intc.ic_cookie = sc;
	sc->sc_intc.ic_establish = bcm_intc_intr_establish_fdt;
	sc->sc_intc.ic_disestablish = bcm_intc_intr_disestablish;
	sc->sc_intc.ic_route = bcm_intc_intr_route;
	arm_intr_register_fdt(&sc->sc_intc);

	sc->sc_l1_intc.ic_node = node;
	sc->sc_l1_intc.ic_cookie = sc;
	sc->sc_l1_intc.ic_establish = l1_intc_intr_establish_fdt;
	sc->sc_l1_intc.ic_disestablish = bcm_intc_intr_disestablish;
	sc->sc_l1_intc.ic_route = bcm_intc_intr_route;
	arm_intr_register_fdt(&sc->sc_l1_intc);

	intr_send_ipi_func = bcm_intc_send_ipi;
	
	bcm_intc_setipl(IPL_HIGH);  /* XXX ??? */
	intr_enable();
}

void
bcm_intc_intr_enable(int irq, int ipl)
{
	struct bcm_intc_softc	*sc = bcm_intc;

	if (IS_IRQ_BANK0(irq))
		sc->sc_imask[0][ipl] |= (1 << IRQ_BANK0(irq));
	else if (IS_IRQ_BANK1(irq))
		sc->sc_imask[1][ipl] |= (1 << IRQ_BANK1(irq));
	else if (IS_IRQ_BANK2(irq))
		sc->sc_imask[2][ipl] |= (1 << IRQ_BANK2(irq));
	else if (IS_IRQ_LOCAL(irq))
		sc->sc_imask[3][ipl] |= (1 << IRQ_LOCAL(irq));
	else
		printf("%s: invalid irq number: %d\n", __func__, irq);
}

void
bcm_intc_intr_disable(int irq, int ipl)
{
	struct bcm_intc_softc	*sc = bcm_intc;

	if (IS_IRQ_BANK0(irq))
		sc->sc_imask[0][ipl] &= ~(1 << IRQ_BANK0(irq));
	else if (IS_IRQ_BANK1(irq))
		sc->sc_imask[1][ipl] &= ~(1 << IRQ_BANK1(irq));
	else if (IS_IRQ_BANK2(irq))
		sc->sc_imask[2][ipl] &= ~(1 << IRQ_BANK2(irq));
	else if (IS_IRQ_LOCAL(irq))
		sc->sc_imask[3][ipl] &= ~(1 << IRQ_LOCAL(irq));
	else
		printf("%s: invalid irq number: %d\n", __func__, irq);
}

void
bcm_intc_calc_mask(void)
{
	struct cpu_info *ci = curcpu();
	struct bcm_intc_softc *sc = bcm_intc;
	int irq;
	struct intrhand *ih;
	int i;

	for (irq = 0; irq < INTC_NIRQ; irq++) {
		int max = IPL_NONE;
		int min = IPL_HIGH;
		TAILQ_FOREACH(ih, &sc->sc_handler[irq].is_list, ih_list) {
			if (ih->ih_ipl > max)
				max = ih->ih_ipl;

			if (ih->ih_ipl < min)
				min = ih->ih_ipl;
		}

		sc->sc_handler[irq].is_irq = max;

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
			bcm_intc_intr_enable(irq, i);
		for (; i <= IPL_HIGH; i++)
			bcm_intc_intr_disable(irq, i);
	}
	arm_init_smask();
	bcm_intc_setipl(ci->ci_cpl);
}

void
bcm_intc_splx(int new)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_ipending & arm_smask[new])
		arm_do_pending_intr(new);

	bcm_intc_setipl(new);
}

int
bcm_intc_spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;
	bcm_intc_splx(new);
	return (old);
}

int
bcm_intc_splraise(int new)
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

	bcm_intc_setipl(new);

	return (old);
}

void
bcm_intc_setipl(int new)
{
	struct cpu_info *ci = curcpu();
	struct bcm_intc_softc *sc = bcm_intc;
	u_long psw;

	psw = intr_disable();
	ci->ci_cpl = new;
	if (cpu_number() == 0) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK0,
		    0xffffffff);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK1,
		    0xffffffff);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_DISABLE_BANK2,
		    0xffffffff);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_ENABLE_BANK0,
		    sc->sc_imask[0][new]);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_ENABLE_BANK1,
		    sc->sc_imask[1][new]);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, INTC_ENABLE_BANK2,
		    sc->sc_imask[2][new]);
	}
	/* timer for current core */
	bus_space_write_4(sc->sc_iot, sc->sc_lioh,
	    ARM_LOCAL_INT_TIMER(cpu_number()),
	    sc->sc_imask[3][ci->ci_cpl] &
	    sc->sc_localcoremask[cpu_number()]);
	intr_restore(psw);
}

int
bcm_intc_get_next_irq(int last_irq)
{
	struct bcm_intc_softc *sc = bcm_intc;
	uint32_t pending;
	int32_t irq = last_irq + 1;

	/* Sanity check */
	if (irq < 0)
		irq = 0;

	/* We need to keep this order. */
	/* TODO: should we mask last_irq? */
	if (IS_IRQ_BANK1(irq)) {
		pending = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    INTC_PENDING_BANK1);
		if (pending == 0) {
			irq = BANK2_START;	/* skip to next bank */
		} else do {
			if (pending & (1 << IRQ_BANK1(irq)))
				return irq;
			irq++;
		} while (IS_IRQ_BANK1(irq));
	}
	if (IS_IRQ_BANK2(irq)) {
		pending = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    INTC_PENDING_BANK2);
		if (pending == 0) {
			irq = BANK0_START;	/* skip to next bank */
		} else do {
			if (pending & (1 << IRQ_BANK2(irq)))
				return irq;
			irq++;
		} while (IS_IRQ_BANK2(irq));
	}
	if (IS_IRQ_BANK0(irq)) {
		pending = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    INTC_PENDING_BANK0);
		if (pending == 0) {
			irq = LOCAL_START;	/* skip to next bank */
		} else do {
			if (pending & (1 << IRQ_BANK0(irq)))
				return irq;
			irq++;
		} while (IS_IRQ_BANK0(irq));
	}
	if (IS_IRQ_LOCAL(irq)) {
		pending = bus_space_read_4(sc->sc_iot, sc->sc_lioh,
		    ARM_LOCAL_INT_PENDING(cpu_number()));
		pending &= ARM_LOCAL_INT_PENDING_MASK;
		if (pending != 0) do {
			if (pending & (1 << IRQ_LOCAL(irq)))
				return irq;
			irq++;
		} while (IS_IRQ_LOCAL(irq));
	}
	return (-1);
}

void
bcm_intc_run_handler(struct intrhand *ih, void *frame, int s)
{
	int handled;
	void *arg;

#ifdef MULTIPROCESSOR
	int need_lock;

	if (ih->ih_flags & IPL_MPSAFE)
		need_lock = 0;
	else
		need_lock = s < IPL_SCHED;

	if (need_lock)
		KERNEL_LOCK();
#endif

	if (ih->ih_arg)
		arg = ih->ih_arg;
	else
		arg = frame;

	handled = ih->ih_func(arg);
	if (handled)
		ih->ih_count.ec_count++;

#ifdef MULTIPROCESSOR
	if (need_lock)
		KERNEL_UNLOCK();
#endif
}

void
bcm_intc_irq_handler(void *frame)
{
	struct bcm_intc_softc *sc = bcm_intc;
	struct intrhand *ih;
	int irq, pri, s;

	irq = (cpu_number() == 0 ? 0 : LOCAL_START) - 1;
	while ((irq = bcm_intc_get_next_irq(irq)) != -1) {
#ifdef MULTIPROCESSOR
		if (irq == ARM_LOCAL_IRQ_MAILBOX(cpu_number())) {
			bcm_intc_handle_ipi();
			continue;
		}
#endif

		pri = sc->sc_handler[irq].is_irq;
		s = bcm_intc_splraise(pri);
		TAILQ_FOREACH(ih, &sc->sc_handler[irq].is_list, ih_list) {
			intr_enable();
			bcm_intc_run_handler(ih, frame, s);
			intr_disable();
		}
		bcm_intc_splx(s);
	}
}

void *
bcm_intc_intr_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcm_intc_softc	*sc = (struct bcm_intc_softc *)cookie;
	int irq;

	irq = cell[1];
	if (cell[0] == 0)
		irq += BANK0_START;
	else if (cell[0] == 1)
		irq += BANK1_START;
	else if (cell[0] == 2)
		irq += BANK2_START;
	else if (cell[0] == 3)
		irq += LOCAL_START;
	else
		panic("%s: bogus interrupt type", sc->sc_dev.dv_xname);

	return bcm_intc_intr_establish(irq, level, ci, func, arg, name);
}

void *
l1_intc_intr_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	int irq;

	irq = cell[0] + LOCAL_START;
	return bcm_intc_intr_establish(irq, level, ci, func, arg, name);
}

void *
bcm_intc_intr_establish(int irqno, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	struct bcm_intc_softc *sc = bcm_intc;
	struct intrhand *ih;
	u_long psw;

	if (irqno < 0 || irqno >= INTC_NIRQ)
		panic("bcm_intc_intr_establish: bogus irqnumber %d: %s",
		     irqno, name);

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	psw = intr_disable();

	ih = malloc(sizeof *ih, M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_flags = level & IPL_FLAGMASK;
	ih->ih_irq = irqno;
	ih->ih_name = name;

	if (IS_IRQ_LOCAL(irqno))
		sc->sc_localcoremask[0] |= (1 << IRQ_LOCAL(irqno));

	TAILQ_INSERT_TAIL(&sc->sc_handler[irqno].is_list, ih, ih_list);

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

#ifdef DEBUG_INTC
	printf("%s irq %d level %d [%s]\n", __func__, irqno, level,
	    name);
#endif
	bcm_intc_calc_mask();

	intr_restore(psw);
	return (ih);
}

void
bcm_intc_intr_disestablish(void *cookie)
{
	struct bcm_intc_softc *sc = bcm_intc;
	struct intrhand *ih = cookie;
	int irqno = ih->ih_irq;
	u_long psw;

	psw = intr_disable();
	TAILQ_REMOVE(&sc->sc_handler[irqno].is_list, ih, ih_list);
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	intr_restore(psw);

	free(ih, M_DEVBUF, 0);
}

void
bcm_intc_intr_route(void *cookie, int enable, struct cpu_info *ci)
{
	struct bcm_intc_softc *sc = bcm_intc;
	struct intrhand *ih = cookie;
	int lirq = IRQ_LOCAL(ih->ih_irq);

	if (enable)
		sc->sc_localcoremask[ci->ci_cpuid] |= (1 << lirq);
	else
		sc->sc_localcoremask[ci->ci_cpuid] &= ~(1 << lirq);
 
	if (ci == curcpu()) {
		bus_space_write_4(sc->sc_iot, sc->sc_lioh,
		    ARM_LOCAL_INT_TIMER(cpu_number()),
		    sc->sc_imask[3][ci->ci_cpl] &
		    sc->sc_localcoremask[cpu_number()]);
#ifdef MULTIPROCESSOR
		bus_space_write_4(sc->sc_iot, sc->sc_lioh,
		    ARM_LOCAL_INT_MAILBOX(cpu_number()),
		    sc->sc_imask[3][ci->ci_cpl] &
		    sc->sc_localcoremask[cpu_number()]);
#endif
	}
}

void
bcm_intc_handle_ipi(void)
{
	struct bcm_intc_softc *sc = bcm_intc;
	int cpuno = cpu_number();
	uint32_t mbox_val;

	mbox_val = bus_space_read_4(sc->sc_iot, sc->sc_lioh,
	    ARM_LOCAL_INT_MAILBOX_CLR(cpuno));
	bus_space_write_4(sc->sc_iot, sc->sc_lioh,
	    ARM_LOCAL_INT_MAILBOX_CLR(cpuno), mbox_val);

#ifdef DDB
	if (ISSET(mbox_val, 1 << ARM_IPI_DDB)) {
		/* XXX */
		db_enter();
	}
#endif
}

void
bcm_intc_send_ipi(struct cpu_info *ci, int id)
{
	struct bcm_intc_softc *sc = bcm_intc;

	__asm volatile("dsb sy"); /* XXX */

	bus_space_write_4(sc->sc_iot, sc->sc_lioh,
	    ARM_LOCAL_INT_MAILBOX_SET(ci->ci_cpuid), 1 << id);
}
