/* $OpenBSD: mvmpic.c,v 1.7 2023/04/10 04:21:20 jsg Exp $ */
/*
 * Copyright (c) 2007,2009,2011 Dale Rahn <drahn@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>

#include <arm/cpufunc.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define	MPIC_CTRL		0x000 /* control register */
#define	 MPIC_CTRL_PRIO_EN		0x1
#define	MPIC_SOFTINT		0x004 /* software triggered interrupt register */
#define	MPIC_INTERR		0x020 /* SOC main interrupt error cause register */
#define	MPIC_ISE		0x030 /* interrupt set enable */
#define	MPIC_ICE		0x034 /* interrupt clear enable */
#define	MPIC_ISCR(x)		(0x100 + (4 * x)) /* interrupt x source control register */
#define	 MPIC_ISCR_PRIO_SHIFT		24
#define	 MPIC_ISCR_INTEN		(1 << 28)

#define	MPIC_DOORBELL_CAUSE	0x008
#define	MPIC_CTP		0x040 /* current task priority */
#define	 MPIC_CTP_SHIFT			28
#define	MPIC_IACK		0x044 /* interrupt acknowledge */
#define	MPIC_ISM		0x048 /* set mask */
#define	MPIC_ICM		0x04c /* clear mask */

struct mpic_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_m_ioh, sc_c_ioh;
	int			 sc_node;

	struct intrhand		**sc_handlers;
	int			 sc_ipl;
	int			 sc_nintr;
	struct evcount		 sc_spur;
	struct interrupt_controller sc_intc;
	void 			*sc_ih;
};

struct intrhand {
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_irq;			/* IRQ number */
	struct evcount	ih_count;
	char *ih_name;
	void *ih_sc;
};

int		 mpic_match(struct device *, void *, void *);
void		 mpic_attach(struct device *, struct device *, void *);
void		 mpic_calc_mask(struct mpic_softc *);
void		*mpic_intr_establish(void *, int *, int, struct cpu_info *,
		    int (*)(void *), void *, char *);
void		 mpic_intr_disestablish(void *);
int		 mpic_intr(void *);
void		 mpic_set_priority(struct mpic_softc *, int, int);
void		 mpic_intr_enable(struct mpic_softc *, int);
void		 mpic_intr_disable(struct mpic_softc *, int);

const struct cfattach	mvmpic_ca = {
	sizeof (struct mpic_softc), mpic_match, mpic_attach
};

struct cfdriver mvmpic_cd = {
	NULL, "mvmpic", DV_DULL
};

int
mpic_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,mpic");
}

void
mpic_attach(struct device *parent, struct device *self, void *args)
{
	struct mpic_softc *sc = (struct mpic_softc *)self;
	struct fdt_attach_args *faa = args;
	int i;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_m_ioh))
		panic("%s: main bus_space_map failed!", __func__);

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_c_ioh))
		panic("%s: cpu bus_space_map failed!", __func__);

	evcount_attach(&sc->sc_spur, "irq1023/spur", NULL);

	sc->sc_nintr = (bus_space_read_4(sc->sc_iot, sc->sc_m_ioh,
	    MPIC_CTRL) >> 2) & 0x3ff;
	printf(" nirq %d\n", sc->sc_nintr);

	/* Disable all interrupts */
	for (i = 0; i < sc->sc_nintr; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_m_ioh, MPIC_ICE, i);
		bus_space_write_4(sc->sc_iot, sc->sc_c_ioh, MPIC_ISM, i);
	}

	/* Clear pending IPIs */
	bus_space_write_4(sc->sc_iot, sc->sc_c_ioh, MPIC_DOORBELL_CAUSE, 0);

	/* Enable hardware prioritization selection */
	bus_space_write_4(sc->sc_iot, sc->sc_m_ioh, MPIC_CTRL,
	    MPIC_CTRL_PRIO_EN);

	/* Always allow everything. */
	bus_space_write_4(sc->sc_iot, sc->sc_c_ioh, MPIC_CTP,
	    (bus_space_read_4(sc->sc_iot, sc->sc_c_ioh, MPIC_CTP) &
	    ~(0xf << MPIC_CTP_SHIFT)) | (IPL_NONE << MPIC_CTP_SHIFT));

	sc->sc_handlers = mallocarray(sc->sc_nintr,
	    sizeof(*sc->sc_handlers), M_DEVBUF, M_ZERO | M_NOWAIT);

	sc->sc_ipl = IPL_NONE;
	mpic_calc_mask(sc);

	sc->sc_intc.ic_node = faa->fa_node;
	sc->sc_intc.ic_cookie = sc;
	sc->sc_intc.ic_establish = mpic_intr_establish;
	arm_intr_register_fdt(&sc->sc_intc);
}

void
mpic_set_priority(struct mpic_softc *sc, int irq, int pri)
{
	bus_space_write_4(sc->sc_iot, sc->sc_m_ioh, MPIC_ISCR(irq),
	    (bus_space_read_4(sc->sc_iot, sc->sc_m_ioh, MPIC_ISCR(irq)) &
	    ~(0xf << MPIC_ISCR_PRIO_SHIFT)) | (pri << MPIC_ISCR_PRIO_SHIFT));
}

void
mpic_intr_enable(struct mpic_softc *sc, int irq)
{
	bus_space_write_4(sc->sc_iot, sc->sc_m_ioh, MPIC_ISE, irq);
	bus_space_write_4(sc->sc_iot, sc->sc_c_ioh, MPIC_ICM, irq);
}

void
mpic_intr_disable(struct mpic_softc *sc, int irq)
{
	bus_space_write_4(sc->sc_iot, sc->sc_m_ioh, MPIC_ICE, irq);
	bus_space_write_4(sc->sc_iot, sc->sc_c_ioh, MPIC_ISM, irq);
}

void
mpic_calc_mask(struct mpic_softc *sc)
{
	struct intrhand		*ih;
	int			 irq;
	int			 max = IPL_NONE;
	int			 min = IPL_HIGH;

	for (irq = 0; irq < sc->sc_nintr; irq++) {
		ih = sc->sc_handlers[irq];
		if (ih == NULL)
			continue;

		if (ih->ih_ipl > max)
			max = ih->ih_ipl;

		if (ih->ih_ipl < min)
			min = ih->ih_ipl;
	}

	if (max == IPL_NONE)
		min = IPL_NONE;

	if (sc->sc_ipl != min) {
		sc->sc_ipl = min;

		if (sc->sc_ih != NULL)
			arm_intr_disestablish_fdt(sc->sc_ih);

		if (sc->sc_ipl != IPL_NONE)
			sc->sc_ih = arm_intr_establish_fdt(sc->sc_node,
			    sc->sc_ipl, mpic_intr, sc, sc->sc_dev.dv_xname);
	}
}

int
mpic_intr(void *cookie)
{
	struct mpic_softc	*sc = cookie;
	struct intrhand		*ih;
	int			 irq, s;

	irq = bus_space_read_4(sc->sc_iot, sc->sc_c_ioh, MPIC_IACK) & 0x3ff;

	if (irq == 1023) {
		sc->sc_spur.ec_count++;
		return 1;
	}

	if (irq >= sc->sc_nintr)
		return 1;

	if ((ih = sc->sc_handlers[irq]) != NULL) {
		s = splraise(ih->ih_ipl);
		if (ih->ih_func(ih->ih_arg))
			ih->ih_count.ec_count++;
		splx(s);
	}

	return 1;
}

void *
mpic_intr_establish(void *cookie, int *cells, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mpic_softc	*sc = cookie;
	struct intrhand		*ih;
	int			 psw;
	int			 irqno = cells[0];

	if (irqno < 0 || irqno >= sc->sc_nintr)
		panic("%s: bogus irqnumber %d: %s", __func__,
		    irqno, name);

	if (sc->sc_handlers[irqno] != NULL)
		panic("%s: irq %d already registered" , __func__,
		    irqno);

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = irqno;
	ih->ih_name = name;
	ih->ih_sc = sc;

	psw = disable_interrupts(PSR_I);

	sc->sc_handlers[irqno] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

#ifdef DEBUG_INTC
	printf("%s: irq %d level %d [%s]\n", __func__, irqno, level, name);
#endif

	mpic_calc_mask(sc);
	mpic_set_priority(sc, irqno, level);
	mpic_intr_enable(sc, irqno);

	restore_interrupts(psw);
	return (ih);
}

void
mpic_intr_disestablish(void *cookie)
{
	struct intrhand		*ih = cookie;
	struct mpic_softc	*sc = ih->ih_sc;
	int			 psw;

	psw = disable_interrupts(PSR_I);

#ifdef DEBUG_INTC
	printf("%s: irq %d ipl %d [%s]\n", __func__, ih->ih_irq, ih->ih_ipl,
	    ih->ih_name);
#endif

	mpic_intr_disable(sc, ih->ih_irq);

	sc->sc_handlers[ih->ih_irq] = NULL;
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));

	mpic_calc_mask(sc);

	restore_interrupts(psw);
}
