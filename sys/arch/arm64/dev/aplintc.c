/*	$OpenBSD: aplintc.c,v 1.19 2025/07/06 12:22:31 dlg Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis
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
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/armreg.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <ddb/db_output.h>

#define APL_IRQ_CR_EL1		s3_4_c15_c10_4
#define  APL_IRQ_CR_EL1_DISABLE	(3 << 0)

#define APL_IPI_LOCAL_RR_EL1	s3_5_c15_c0_0
#define APL_IPI_GLOBAL_RR_EL1	s3_5_c15_c0_1
#define APL_IPI_SR_EL1		s3_5_c15_c1_1
#define  APL_IPI_SR_EL1_PENDING	(1 << 0)

#define AIC_INFO		0x0004
#define  AIC_INFO_NDIE(val)	(((val) >> 24) & 0xf)
#define  AIC_INFO_NIRQ(val)	((val) & 0xffff)
#define AIC_WHOAMI		0x2000
#define AIC_EVENT		0x2004
#define  AIC_EVENT_DIE(val)	(((val) >> 24) & 0xff)
#define  AIC_EVENT_TYPE(val)	(((val) >> 16) & 0xff)
#define  AIC_EVENT_TYPE_NONE	0
#define  AIC_EVENT_TYPE_IRQ	1
#define  AIC_EVENT_TYPE_IPI	4
#define  AIC_EVENT_IRQ(val)	((val) & 0xffff)
#define  AIC_EVENT_IPI_OTHER	1
#define  AIC_EVENT_IPI_SELF	2
#define AIC_IPI_SEND		0x2008
#define AIC_IPI_ACK		0x200c
#define AIC_IPI_MASK_SET	0x2024
#define AIC_IPI_MASK_CLR	0x2028
#define  AIC_IPI_OTHER		(1U << 0)
#define  AIC_IPI_SELF		(1U << 31)
#define AIC_TARGET_CPU(irq)	(0x3000 + ((irq) << 2))
#define AIC_SW_SET(irq)		(0x4000 + (((irq) >> 5) << 2))
#define AIC_SW_CLR(irq)		(0x4080 + (((irq) >> 5) << 2))
#define  AIC_SW_BIT(irq)	(1U << ((irq) & 0x1f))
#define AIC_MASK_SET(irq)	(0x4100 + (((irq) >> 5) << 2))
#define AIC_MASK_CLR(irq)	(0x4180 + (((irq) >> 5) << 2))
#define  AIC_MASK_BIT(irq)	(1U << ((irq) & 0x1f))

#define AIC2_CONFIG		0x0014
#define  AIC2_CONFIG_ENABLE	(1 << 0)
#define AIC2_SW_SET(die, irq)	(0x6000 + (die) * 0x4a00 + (((irq) >> 5) << 2))
#define AIC2_SW_CLR(die, irq)	(0x6200 + (die) * 0x4a00 + (((irq) >> 5) << 2))
#define AIC2_MASK_SET(die, irq)	(0x6400 + (die) * 0x4a00 + (((irq) >> 5) << 2))
#define AIC2_MASK_CLR(die, irq)	(0x6600 + (die) * 0x4a00 + (((irq) >> 5) << 2))
#define AIC2_EVENT		0xc000

#define AIC_MAXCPUS		32
#define AIC_MAXDIES		4

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;
	int		(*ih_func)(void *);
	void		*ih_arg;
	int		ih_ipl;
	int		ih_flags;
	int		ih_die;
	int		ih_irq;
	struct evcount	ih_count;
	const char	*ih_name;
	struct cpu_info *ih_ci;
};

struct aplintc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_event_ioh;

	int			sc_version;

	struct interrupt_controller sc_ic;

	struct intrhand		*sc_fiq_handler;
	int			sc_fiq_pending[AIC_MAXCPUS];
	struct intrhand		**sc_irq_handler[AIC_MAXDIES];
	int 			sc_nirq;
	int			sc_ndie;
	int			sc_ncells;
	TAILQ_HEAD(, intrhand)	sc_irq_list[NIPL];

	uint32_t		sc_cpuremap[AIC_MAXCPUS];
	u_int			sc_ipi_reason[AIC_MAXCPUS];
	struct evcount		sc_ipi_count;
};

static inline void
aplintc_sw_clr(struct aplintc_softc *sc, int die, int irq)
{
	if (sc->sc_version == 1)
		HWRITE4(sc, AIC_SW_CLR(irq), AIC_SW_BIT(irq));
	else
		HWRITE4(sc, AIC2_SW_CLR(die, irq), AIC_SW_BIT(irq));
}

static inline void
aplintc_sw_set(struct aplintc_softc *sc, int die, int irq)
{
	if (sc->sc_version == 1)
		HWRITE4(sc, AIC_SW_SET(irq), AIC_SW_BIT(irq));
	else
		HWRITE4(sc, AIC2_SW_SET(die, irq), AIC_SW_BIT(irq));
}

static inline void
aplintc_mask_clr(struct aplintc_softc *sc, int die, int irq)
{
	if (sc->sc_version == 1)
		HWRITE4(sc, AIC_MASK_CLR(irq), AIC_MASK_BIT(irq));
	else
		HWRITE4(sc, AIC2_MASK_CLR(die, irq), AIC_MASK_BIT(irq));
}

static inline void
aplintc_mask_set(struct aplintc_softc *sc, int die, int irq)
{
	if (sc->sc_version == 1)
		HWRITE4(sc, AIC_MASK_SET(irq), AIC_MASK_BIT(irq));
	else
		HWRITE4(sc, AIC2_MASK_SET(die, irq), AIC_MASK_BIT(irq));
}

struct aplintc_softc *aplintc_sc;

int	aplintc_match(struct device *, void *, void *);
void	aplintc_attach(struct device *, struct device *, void *);

const struct cfattach aplintc_ca = {
	sizeof (struct aplintc_softc), aplintc_match, aplintc_attach
};

struct cfdriver aplintc_cd = {
	NULL, "aplintc", DV_DULL
};

void	aplintc_cpuinit(void);
void	aplintc_irq_handler(void *);
void	aplintc_fiq_handler(void *);
void	aplintc_intr_barrier(void *);
int	aplintc_splraise(int);
int	aplintc_spllower(int);
void	aplintc_splx(int);
void	aplintc_setipl(int);
void	aplintc_enable_wakeup(void);
void	aplintc_disable_wakeup(void);

void 	*aplintc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	aplintc_intr_disestablish(void *);
void	aplintc_intr_set_wakeup(void *);

void	aplintc_send_ipi(struct cpu_info *, int);
void	aplintc_handle_ipi(struct aplintc_softc *);

int
aplintc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,aic") ||
	    OF_is_compatible(faa->fa_node, "apple,aic2");
}

void
aplintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplintc_softc *sc = (struct aplintc_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t info;
	int die, ipl;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "apple,aic2"))
		sc->sc_version = 2;
	else
		sc->sc_version = 1;

	sc->sc_ncells = OF_getpropint(faa->fa_node, "#interrupt-cells", 3);
	if (sc->sc_ncells < 3 || sc->sc_ncells > 4) {
		printf(": invalid number of cells\n");
		return;
	}

	/*
	 * AIC2 has the event register specified separately.  However
	 * a preliminary device tree binding for AIC2 had it included
	 * in the main register area, like with AIC1.  Support both
	 * for now.
	 */
	if (faa->fa_nreg > 1) {
		if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
		    faa->fa_reg[1].size, 0, &sc->sc_event_ioh)) {
			printf(": can't map event register\n");
			return;
		}
	} else {
		if (sc->sc_version == 1) {
			bus_space_subregion(sc->sc_iot, sc->sc_ioh,
			    AIC_EVENT, 4, &sc->sc_event_ioh);
		} else {
			bus_space_subregion(sc->sc_iot, sc->sc_ioh,
			    AIC2_EVENT, 4, &sc->sc_event_ioh);
		}
	}

	info = HREAD4(sc, AIC_INFO);
	sc->sc_nirq = AIC_INFO_NIRQ(info);
	sc->sc_ndie = AIC_INFO_NDIE(info) + 1;
	for (die = 0; die < sc->sc_ndie; die++) {
		sc->sc_irq_handler[die] = mallocarray(sc->sc_nirq,
		    sizeof(struct intrhand), M_DEVBUF, M_WAITOK | M_ZERO);
	}
	for (ipl = 0; ipl < NIPL; ipl++)
		TAILQ_INIT(&sc->sc_irq_list[ipl]);

	printf(" nirq %d ndie %d\n", sc->sc_nirq, sc->sc_ndie);

	arm_init_smask();

	aplintc_sc = sc;
	aplintc_cpuinit();

	evcount_attach(&sc->sc_ipi_count, "ipi", NULL);
	arm_set_intr_handler(aplintc_splraise, aplintc_spllower, aplintc_splx,
	    aplintc_setipl, aplintc_irq_handler, aplintc_fiq_handler,
	    aplintc_enable_wakeup, aplintc_disable_wakeup);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = self;
	sc->sc_ic.ic_establish = aplintc_intr_establish;
	sc->sc_ic.ic_disestablish = aplintc_intr_disestablish;
	sc->sc_ic.ic_cpu_enable = aplintc_cpuinit;
	sc->sc_ic.ic_barrier = aplintc_intr_barrier;
	sc->sc_ic.ic_set_wakeup = aplintc_intr_set_wakeup;
	arm_intr_register_fdt(&sc->sc_ic);

#ifdef MULTIPROCESSOR
	intr_send_ipi_func = aplintc_send_ipi;
#endif

	if (sc->sc_version == 2)
		HSET4(sc, AIC2_CONFIG, AIC2_CONFIG_ENABLE);
}

void
aplintc_cpuinit(void)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct cpu_info *ci = curcpu();
	uint32_t hwid;

	KASSERT(ci->ci_cpuid < AIC_MAXCPUS);

	/*
	 * AIC2 does not provide us with a way to target external
	 * interrupts to a particular core.  Therefore, disable IRQ
	 * delivery to the secondary CPUs which makes sure all
	 * external interrupts are delivered to the primary CPU.
	 */
	if (!CPU_IS_PRIMARY(ci))
		WRITE_SPECIALREG(APL_IRQ_CR_EL1, APL_IRQ_CR_EL1_DISABLE);

	if (sc->sc_version == 1) {
		hwid = HREAD4(sc, AIC_WHOAMI);
		KASSERT(hwid < AIC_MAXCPUS);
		sc->sc_cpuremap[ci->ci_cpuid] = hwid;
	}
}

void
aplintc_run_handler(struct intrhand *ih, void *frame, int s)
{
	void *arg;
	int handled;

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
aplintc_irq_handler(void *frame)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	uint32_t event;
	uint32_t die, irq, type;
	int s;

	event = bus_space_read_4(sc->sc_iot, sc->sc_event_ioh, 0);
	die = AIC_EVENT_DIE(event);
	irq = AIC_EVENT_IRQ(event);
	type = AIC_EVENT_TYPE(event);

	if (type != AIC_EVENT_TYPE_IRQ) {
		if (type != AIC_EVENT_TYPE_NONE) {
			printf("%s: unexpected event type %d\n",
			    __func__, type);
		}
		return;
	}

	if (die >= sc->sc_ndie)
		panic("%s: unexpected die %d", __func__, die);
	if (irq >= sc->sc_nirq)
		panic("%s: unexpected irq %d", __func__, irq);

	if (sc->sc_irq_handler[die][irq] == NULL)
		return;

	aplintc_sw_clr(sc, die, irq);
	ih = sc->sc_irq_handler[die][irq];

	if (ci->ci_cpl >= ih->ih_ipl) {
		/* Queue interrupt as pending. */
		TAILQ_INSERT_TAIL(&sc->sc_irq_list[ih->ih_ipl], ih, ih_list);
	} else {
		s = aplintc_splraise(ih->ih_ipl);
		intr_enable();
		aplintc_run_handler(ih, frame, s);
		intr_disable();
		aplintc_splx(s);

		aplintc_mask_clr(sc, die, irq);
	}
}

void
aplintc_fiq_handler(void *frame)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct cpu_info *ci = curcpu();
	uint64_t reg;
	int s;

#ifdef MULTIPROCESSOR
	/* Handle IPIs. */
	reg = READ_SPECIALREG(APL_IPI_SR_EL1);
	if (reg & APL_IPI_SR_EL1_PENDING) {
		WRITE_SPECIALREG(APL_IPI_SR_EL1, APL_IPI_SR_EL1_PENDING);
		aplintc_handle_ipi(sc);
	}
#endif

	/* Handle timer interrupts. */
	reg = READ_SPECIALREG(cntv_ctl_el0);
	if ((reg & (CNTV_CTL_ENABLE | CNTV_CTL_IMASK | CNTV_CTL_ISTATUS)) ==
	    (CNTV_CTL_ENABLE | CNTV_CTL_ISTATUS)) {
		if (ci->ci_cpl >= IPL_CLOCK) {
			/* Mask timer interrupt and mark as pending. */
			WRITE_SPECIALREG(cntv_ctl_el0, reg | CNTV_CTL_IMASK);
			sc->sc_fiq_pending[ci->ci_cpuid] = 1;
		} else {
			s = aplintc_splraise(IPL_CLOCK);
			sc->sc_fiq_handler->ih_func(frame);
			sc->sc_fiq_handler->ih_count.ec_count++;
			aplintc_splx(s);
		}
	}
}

void
aplintc_intr_barrier(void *cookie)
{
	struct intrhand	*ih = cookie;

	sched_barrier(ih->ih_ci);
}

int
aplintc_splraise(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;

	if (old > new)
		new = old;

	aplintc_setipl(new);
	return old;
}

int
aplintc_spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;

	aplintc_splx(new);
	return old;
}

void
aplintc_splx(int new)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	uint64_t reg;
	u_long daif;
	int ipl;

	daif = intr_disable();

	/* Process pending FIQs. */
	if (sc->sc_fiq_pending[ci->ci_cpuid] && new < IPL_CLOCK) {
		sc->sc_fiq_pending[ci->ci_cpuid] = 0;
		reg = READ_SPECIALREG(cntv_ctl_el0);
		WRITE_SPECIALREG(cntv_ctl_el0, reg & ~CNTV_CTL_IMASK);
	}

	/* Process pending IRQs. */
	if (CPU_IS_PRIMARY(ci)) {
		for (ipl = ci->ci_cpl; ipl > new; ipl--) {
			while (!TAILQ_EMPTY(&sc->sc_irq_list[ipl])) {
				ih = TAILQ_FIRST(&sc->sc_irq_list[ipl]);
				TAILQ_REMOVE(&sc->sc_irq_list[ipl],
				    ih, ih_list);

				aplintc_sw_set(sc, ih->ih_die, ih->ih_irq);
				aplintc_mask_clr(sc, ih->ih_die, ih->ih_irq);
			}
		}
	}

	aplintc_setipl(new);
	intr_restore(daif);

	if (ci->ci_ipending & arm_smask[new])
		arm_do_pending_intr(new);
}

void
aplintc_setipl(int ipl)
{
	struct cpu_info *ci = curcpu();

	ci->ci_cpl = ipl;
}

void
aplintc_enable_wakeup(void)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct intrhand *ih;
	int die, irq;

	for (die = 0; die < sc->sc_ndie; die++) {
		for (irq = 0; irq < sc->sc_nirq; irq++) {
			ih = sc->sc_irq_handler[die][irq];
			if (ih == NULL || (ih->ih_flags & IPL_WAKEUP))
				continue;
			aplintc_mask_set(sc, die, irq);
		}
	}
}

void
aplintc_disable_wakeup(void)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct intrhand *ih;
	int die, irq;

	for (die = 0; die < sc->sc_ndie; die++) {
		for (irq = 0; irq < sc->sc_nirq; irq++) {
			ih = sc->sc_irq_handler[die][irq];
			if (ih == NULL || (ih->ih_flags & IPL_WAKEUP))
				continue;
			aplintc_mask_clr(sc, die, irq);
		}
	}
}

void *
aplintc_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct aplintc_softc *sc = cookie;
	struct intrhand *ih;
	uint32_t type = cell[0];
	uint32_t die, irq;

	if (sc->sc_ncells == 3) {
		die = 0;
		irq = cell[1];
	} else {
		die = cell[1];
		irq = cell[2];
	}

	if (type == 0) {
		KASSERT(level != (IPL_CLOCK | IPL_MPSAFE));
		if (die >= sc->sc_ndie) {
			panic("%s: bogus die number %d",
			    sc->sc_dev.dv_xname, die);
		}
		if (irq >= sc->sc_nirq) {
			panic("%s: bogus irq number %d",
			    sc->sc_dev.dv_xname, irq);
		}
	} else if (type == 1) {
		KASSERT(level == (IPL_CLOCK | IPL_MPSAFE));
		if (irq >= 4)
			panic("%s: bogus fiq number %d",
			    sc->sc_dev.dv_xname, irq);
	} else {
		panic("%s: bogus irq type %d",
		    sc->sc_dev.dv_xname, cell[0]);
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_flags = level & IPL_FLAGMASK;
	ih->ih_die = die;
	ih->ih_irq = irq;
	ih->ih_name = name;
	ih->ih_ci = ci;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	if (type == 0) {
		sc->sc_irq_handler[die][irq] = ih;
		if (sc->sc_version == 1)
			HWRITE4(sc, AIC_TARGET_CPU(irq), 1);
		aplintc_mask_clr(sc, die, irq);
	} else
		sc->sc_fiq_handler = ih;

	return ih;
}

void
aplintc_intr_disestablish(void *cookie)
{
	struct aplintc_softc *sc = aplintc_sc;
	struct intrhand *ih = cookie;
	struct intrhand *tmp;
	u_long daif;

	KASSERT(ih->ih_ipl < IPL_CLOCK);

	daif = intr_disable();

	aplintc_sw_clr(sc, ih->ih_die, ih->ih_irq);
	aplintc_mask_set(sc, ih->ih_die, ih->ih_irq);

	/* Remove ourselves from the list of pending IRQs. */
	TAILQ_FOREACH(tmp, &sc->sc_irq_list[ih->ih_ipl], ih_list) {
		if (tmp == ih) {
			TAILQ_REMOVE(&sc->sc_irq_list[ih->ih_ipl],
			    ih, ih_list);
			break;
		}
	}

	sc->sc_irq_handler[ih->ih_die][ih->ih_irq] = NULL;
	if (ih->ih_name)
		evcount_detach(&ih->ih_count);

	intr_restore(daif);

	free(ih, M_DEVBUF, sizeof(*ih));
}

void
aplintc_intr_set_wakeup(void *cookie)
{
	struct intrhand *ih = cookie;

	ih->ih_flags |= IPL_WAKEUP;
}

#ifdef MULTIPROCESSOR

void
aplintc_send_ipi(struct cpu_info *ci, int reason)
{
	struct aplintc_softc *sc = aplintc_sc;
	uint64_t sendmask;

	if (reason == ARM_IPI_NOP) {
		if (ci == curcpu())
			return;
	} else {
		atomic_setbits_int(&sc->sc_ipi_reason[ci->ci_cpuid],
		    1 << reason);
	}

	sendmask = (ci->ci_mpidr & MPIDR_AFF0);
	if ((curcpu()->ci_mpidr & MPIDR_AFF1) == (ci->ci_mpidr & MPIDR_AFF1)) {
		/* Same cluster, so request local delivery. */
		WRITE_SPECIALREG(APL_IPI_LOCAL_RR_EL1, sendmask);
	} else {
		/* Different cluster, so request global delivery. */
		sendmask |= (ci->ci_mpidr & MPIDR_AFF1) << 8;
		WRITE_SPECIALREG(APL_IPI_GLOBAL_RR_EL1, sendmask);
	}
}

void
aplintc_handle_ipi(struct aplintc_softc *sc)
{
	struct cpu_info *ci = curcpu();
	u_int reasons;

	reasons = sc->sc_ipi_reason[ci->ci_cpuid];
	if (reasons) {
		reasons = atomic_swap_uint(&sc->sc_ipi_reason[ci->ci_cpuid], 0);
#ifdef DDB
		if (ISSET(reasons, 1 << ARM_IPI_DDB))
			db_enter();
#endif
		if (ISSET(reasons, 1 << ARM_IPI_HALT))
			cpu_halt();
	}

	sc->sc_ipi_count.ec_count++;
}

#endif
