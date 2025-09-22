/* $OpenBSD: ampintc.c,v 1.32 2023/09/22 01:10:43 jsg Exp $ */
/*
 * Copyright (c) 2007,2009,2011 Dale Rahn <drahn@openbsd.org>
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
 * This driver implements the interrupt controller as specified in
 * DDI0407E_cortex_a9_mpcore_r2p0_trm with the
 * IHI0048A_gic_architecture_spec_v1_0 underlying specification
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <arm/cpufunc.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/simplebusvar.h>

/* registers */
#define	ICD_DCR			0x000
#define		ICD_DCR_ES		0x00000001
#define		ICD_DCR_ENS		0x00000002

#define ICD_ICTR			0x004
#define		ICD_ICTR_LSPI_SH	11
#define		ICD_ICTR_LSPI_M		0x1f
#define		ICD_ICTR_CPU_SH		5
#define		ICD_ICTR_CPU_M		0x07
#define		ICD_ICTR_ITL_SH		0
#define		ICD_ICTR_ITL_M		0x1f
#define ICD_IDIR			0x008
#define 	ICD_DIR_PROD_SH		24
#define 	ICD_DIR_PROD_M		0xff
#define 	ICD_DIR_REV_SH		12
#define 	ICD_DIR_REV_M		0xfff
#define 	ICD_DIR_IMP_SH		0
#define 	ICD_DIR_IMP_M		0xfff

#define IRQ_TO_REG32(i)		(((i) >> 5) & 0x1f)
#define IRQ_TO_REG32BIT(i)	((i) & 0x1f)
#define IRQ_TO_REG4(i)		(((i) >> 2) & 0xff)
#define IRQ_TO_REG4BIT(i)	((i) & 0x3)
#define IRQ_TO_REG16(i)		(((i) >> 4) & 0x3f)
#define IRQ_TO_REG16BIT(i)	((i) & 0xf)
#define IRQ_TO_REGBIT_S(i)	8
#define IRQ_TO_REG4BIT_M(i)	8

#define ICD_ISRn(i)		(0x080 + (IRQ_TO_REG32(i) * 4))
#define ICD_ISERn(i)		(0x100 + (IRQ_TO_REG32(i) * 4))
#define ICD_ICERn(i)		(0x180 + (IRQ_TO_REG32(i) * 4))
#define ICD_ISPRn(i)		(0x200 + (IRQ_TO_REG32(i) * 4))
#define ICD_ICPRn(i)		(0x280 + (IRQ_TO_REG32(i) * 4))
#define ICD_ABRn(i)		(0x300 + (IRQ_TO_REG32(i) * 4))
#define ICD_IPRn(i)		(0x400 + (i))
#define ICD_IPTRn(i)		(0x800 + (i))
#define ICD_ICRn(i)		(0xC00 + (IRQ_TO_REG16(i) * 4))
#define 	ICD_ICR_TRIG_LEVEL(i)	(0x0 << (IRQ_TO_REG16BIT(i) * 2))
#define 	ICD_ICR_TRIG_EDGE(i)	(0x2 << (IRQ_TO_REG16BIT(i) * 2))
#define 	ICD_ICR_TRIG_MASK(i)	(0x2 << (IRQ_TO_REG16BIT(i) * 2))

/*
 * what about (ppi|spi)_status
 */
#define ICD_PPI			0xD00
#define 	ICD_PPI_GTIMER	(1 << 11)
#define 	ICD_PPI_FIQ		(1 << 12)
#define 	ICD_PPI_PTIMER	(1 << 13)
#define 	ICD_PPI_PWDOG	(1 << 14)
#define 	ICD_PPI_IRQ		(1 << 15)
#define ICD_SPI_BASE		0xD04
#define ICD_SPIn(i)			(ICD_SPI_BASE + ((i) * 4))


#define ICD_SGIR			0xF00

#define ICD_PERIPH_ID_0			0xFD0
#define ICD_PERIPH_ID_1			0xFD4
#define ICD_PERIPH_ID_2			0xFD8
#define ICD_PERIPH_ID_3			0xFDC
#define ICD_PERIPH_ID_4			0xFE0
#define ICD_PERIPH_ID_5			0xFE4
#define ICD_PERIPH_ID_6			0xFE8
#define ICD_PERIPH_ID_7			0xFEC

#define ICD_COMP_ID_0			0xFEC
#define ICD_COMP_ID_1			0xFEC
#define ICD_COMP_ID_2			0xFEC
#define ICD_COMP_ID_3			0xFEC


#define ICPICR				0x00
#define ICPIPMR				0x04
/* XXX - must left justify bits to  0 - 7  */
#define 	ICMIPMR_SH 		4
#define ICPBPR				0x08
#define ICPIAR				0x0C
#define 	ICPIAR_IRQ_SH		0
#define 	ICPIAR_IRQ_M		0x3ff
#define 	ICPIAR_CPUID_SH		10
#define 	ICPIAR_CPUID_M		0x7
#define 	ICPIAR_NO_PENDING_IRQ	ICPIAR_IRQ_M
#define ICPEOIR				0x10
#define ICPPRP				0x14
#define ICPHPIR				0x18
#define ICPIIR				0xFC

/*
 * what about periph_id and component_id
 */

#define IRQ_ENABLE	1
#define IRQ_DISABLE	0

struct ampintc_softc {
	struct simplebus_softc	 sc_sbus;
	struct intrq 		*sc_handler;
	int			 sc_nintr;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_d_ioh, sc_p_ioh;
	uint8_t			 sc_cpu_mask[ICD_ICTR_CPU_M + 1];
	struct evcount		 sc_spur;
	struct interrupt_controller sc_ic;
	int			 sc_ipi_reason[ICD_ICTR_CPU_M + 1];
	int			 sc_ipi_num[2];
};
struct ampintc_softc *ampintc;


struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;	/* link on intrq list */
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_flags;
	int ih_irq;			/* IRQ number */
	struct evcount	ih_count;
	char *ih_name;
};

struct intrq {
	TAILQ_HEAD(, intrhand) iq_list;	/* handler list */
	int iq_irq_max;			/* IRQ to mask while handling */
	int iq_irq_min;			/* lowest IRQ when shared */
	int iq_ist;			/* share type */
};


int		 ampintc_match(struct device *, void *, void *);
void		 ampintc_attach(struct device *, struct device *, void *);
void		 ampintc_cpuinit(void);
int		 ampintc_spllower(int);
void		 ampintc_splx(int);
int		 ampintc_splraise(int);
void		 ampintc_setipl(int);
void		 ampintc_calc_mask(void);
void		*ampintc_intr_establish(int, int, int, struct cpu_info *,
		    int (*)(void *), void *, char *);
void		*ampintc_intr_establish_ext(int, int, struct cpu_info *,
		    int (*)(void *), void *, char *);
void		*ampintc_intr_establish_fdt(void *, int *, int,
		    struct cpu_info *, int (*)(void *), void *, char *);
void		 ampintc_intr_disestablish(void *);
void		 ampintc_irq_handler(void *);
const char	*ampintc_intr_string(void *);
uint32_t	 ampintc_iack(void);
void		 ampintc_eoi(uint32_t);
void		 ampintc_set_priority(int, int);
void		 ampintc_intr_enable(int);
void		 ampintc_intr_disable(int);
void		 ampintc_intr_config(int, int);
void		 ampintc_route(int, int, struct cpu_info *);
void		 ampintc_route_irq(void *, int, struct cpu_info *);

int		 ampintc_ipi_combined(void *);
int		 ampintc_ipi_nop(void *);
int		 ampintc_ipi_ddb(void *);
void		 ampintc_send_ipi(struct cpu_info *, int);

const struct cfattach	ampintc_ca = {
	sizeof (struct ampintc_softc), ampintc_match, ampintc_attach
};

struct cfdriver ampintc_cd = {
	NULL, "ampintc", DV_DULL
};

static char *ampintc_compatibles[] = {
	"arm,cortex-a7-gic",
	"arm,cortex-a9-gic",
	"arm,cortex-a15-gic",
	"arm,gic-400",
	NULL
};

int
ampintc_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; ampintc_compatibles[i]; i++)
		if (OF_is_compatible(faa->fa_node, ampintc_compatibles[i]))
			return (1);

	return (0);
}

void
ampintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct ampintc_softc *sc = (struct ampintc_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i, nintr, ncpu;
	uint32_t ictr;
#ifdef MULTIPROCESSOR
	int nipi, ipiirq[2];
#endif

	ampintc = sc;

	arm_init_smask();

	sc->sc_iot = faa->fa_iot;

	/* First row: ICD */
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_d_ioh))
		panic("%s: ICD bus_space_map failed!", __func__);

	/* Second row: ICP */
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_p_ioh))
		panic("%s: ICP bus_space_map failed!", __func__);

	evcount_attach(&sc->sc_spur, "irq1023/spur", NULL);

	ictr = bus_space_read_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICTR);
	nintr = 32 * ((ictr >> ICD_ICTR_ITL_SH) & ICD_ICTR_ITL_M);
	nintr += 32; /* ICD_ICTR + 1, irq 0-31 is SGI, 32+ is PPI */
	sc->sc_nintr = nintr;
	ncpu = ((ictr >> ICD_ICTR_CPU_SH) & ICD_ICTR_CPU_M) + 1;
	printf(" nirq %d, ncpu %d", nintr, ncpu);

	KASSERT(curcpu()->ci_cpuid <= ICD_ICTR_CPU_M);
	sc->sc_cpu_mask[curcpu()->ci_cpuid] =
	    bus_space_read_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPTRn(0));

	/* Disable all interrupts, clear all pending */
	for (i = 0; i < nintr/32; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_d_ioh,
		    ICD_ICERn(i*32), ~0);
		bus_space_write_4(sc->sc_iot, sc->sc_d_ioh,
		    ICD_ICPRn(i*32), ~0);
	}
	for (i = 0; i < nintr; i++) {
		/* lowest priority ?? */
		bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPRn(i), 0xff);
		/* target no cpus */
		bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPTRn(i), 0);
	}
	for (i = 2; i < nintr/16; i++) {
		/* irq 32 - N */
		bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICRn(i*16), 0);
	}

	/* software reset of the part? */
	/* set protection bit (kernel only)? */

	/* XXX - check power saving bit */

	sc->sc_handler = mallocarray(nintr, sizeof(*sc->sc_handler), M_DEVBUF,
	    M_ZERO | M_NOWAIT);
	for (i = 0; i < nintr; i++) {
		TAILQ_INIT(&sc->sc_handler[i].iq_list);
	}

	ampintc_setipl(IPL_HIGH);  /* XXX ??? */
	ampintc_calc_mask();

	/* insert self as interrupt handler */
	arm_set_intr_handler(ampintc_splraise, ampintc_spllower, ampintc_splx,
	    ampintc_setipl, ampintc_intr_establish_ext,
	    ampintc_intr_disestablish, ampintc_intr_string, ampintc_irq_handler);

#ifdef MULTIPROCESSOR
	/* setup IPI interrupts */

	/*
	 * Ideally we want two IPI interrupts, one for NOP and one for
	 * DDB, however we can survive if only one is available it is
	 * possible that most are not available to the non-secure OS.
	 */
	nipi = 0;
	for (i = 0; i < 16; i++) {
		int reg, oldreg;

		oldreg = bus_space_read_1(sc->sc_iot, sc->sc_d_ioh,
		    ICD_IPRn(i));
		bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPRn(i),
		    oldreg ^ 0x20);

		/* if this interrupt is not usable, route will be zero */
		reg = bus_space_read_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPRn(i));
		if (reg == oldreg)
			continue;

		/* return to original value, will be set when used */
		bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPRn(i),
		    oldreg);

		if (nipi == 0)
			printf(" ipi: %d", i);
		else
			printf(", %d", i);
		ipiirq[nipi++] = i;
		if (nipi == 2)
			break;
	}

	if (nipi == 0)
		panic ("no irq available for IPI");

	switch (nipi) {
	case 1:
		ampintc_intr_establish(ipiirq[0], IST_EDGE_RISING,
		    IPL_IPI|IPL_MPSAFE, ampintc_ipi_combined, sc, "ipi");
		sc->sc_ipi_num[ARM_IPI_NOP] = ipiirq[0];
		sc->sc_ipi_num[ARM_IPI_DDB] = ipiirq[0];
		break;
	case 2:
		ampintc_intr_establish(ipiirq[0], IST_EDGE_RISING,
		    IPL_IPI|IPL_MPSAFE, ampintc_ipi_nop, sc, "ipinop");
		sc->sc_ipi_num[ARM_IPI_NOP] = ipiirq[0];
		ampintc_intr_establish(ipiirq[1], IST_EDGE_RISING,
		    IPL_IPI|IPL_MPSAFE, ampintc_ipi_ddb, sc, "ipiddb");
		sc->sc_ipi_num[ARM_IPI_DDB] = ipiirq[1];
		break;
	default:
		panic("nipi unexpected number %d", nipi);
	}

	intr_send_ipi_func = ampintc_send_ipi;
#endif

	/* enable interrupts */
	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_DCR, 3);
	bus_space_write_4(sc->sc_iot, sc->sc_p_ioh, ICPICR, 1);
	enable_interrupts(PSR_I);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = self;
	sc->sc_ic.ic_establish = ampintc_intr_establish_fdt;
	sc->sc_ic.ic_disestablish = ampintc_intr_disestablish;
	sc->sc_ic.ic_route = ampintc_route_irq;
	sc->sc_ic.ic_cpu_enable = ampintc_cpuinit;
	arm_intr_register_fdt(&sc->sc_ic);

	/* attach GICv2M frame controller */
	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

void
ampintc_set_priority(int irq, int pri)
{
	struct ampintc_softc	*sc = ampintc;
	uint32_t		 prival;

	/*
	 * We only use 16 (13 really) interrupt priorities,
	 * and a CPU is only required to implement bit 4-7 of each field
	 * so shift into the top bits.
	 * also low values are higher priority thus IPL_HIGH - pri
	 */
	prival = (IPL_HIGH - pri) << ICMIPMR_SH;
	bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPRn(irq), prival);
}

void
ampintc_setipl(int new)
{
	struct cpu_info		*ci = curcpu();
	struct ampintc_softc	*sc = ampintc;
	int			 psw;

	/* disable here is only to keep hardware in sync with ci->ci_cpl */
	psw = disable_interrupts(PSR_I);
	ci->ci_cpl = new;

	/* low values are higher priority thus IPL_HIGH - pri */
	bus_space_write_4(sc->sc_iot, sc->sc_p_ioh, ICPIPMR,
	    (IPL_HIGH - new) << ICMIPMR_SH);
	restore_interrupts(psw);
}

void
ampintc_intr_enable(int irq)
{
	struct ampintc_softc	*sc = ampintc;

#ifdef DEBUG
	printf("enable irq %d register %x bitmask %08x\n",
	    irq, ICD_ISERn(irq), 1 << IRQ_TO_REG32BIT(irq));
#endif

	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_ISERn(irq),
	    1 << IRQ_TO_REG32BIT(irq));
}

void
ampintc_intr_disable(int irq)
{
	struct ampintc_softc	*sc = ampintc;

	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICERn(irq),
	    1 << IRQ_TO_REG32BIT(irq));
}

void
ampintc_intr_config(int irqno, int type)
{
	struct ampintc_softc	*sc = ampintc;
	uint32_t		 ctrl;

	ctrl = bus_space_read_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICRn(irqno));

	ctrl &= ~ICD_ICR_TRIG_MASK(irqno);
	if (type == IST_EDGE_RISING)
		ctrl |= ICD_ICR_TRIG_EDGE(irqno);
	else
		ctrl |= ICD_ICR_TRIG_LEVEL(irqno);

	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICRn(irqno), ctrl);
}

void
ampintc_calc_mask(void)
{
	struct cpu_info		*ci = curcpu();
        struct ampintc_softc	*sc = ampintc;
	struct intrhand		*ih;
	int			 irq;

	for (irq = 0; irq < sc->sc_nintr; irq++) {
		int max = IPL_NONE;
		int min = IPL_HIGH;
		TAILQ_FOREACH(ih, &sc->sc_handler[irq].iq_list, ih_list) {
			if (ih->ih_ipl > max)
				max = ih->ih_ipl;

			if (ih->ih_ipl < min)
				min = ih->ih_ipl;
		}

		if (max == IPL_NONE)
			min = IPL_NONE;

		if (sc->sc_handler[irq].iq_irq_max == max &&
		    sc->sc_handler[irq].iq_irq_min == min)
			continue;

		sc->sc_handler[irq].iq_irq_max = max;
		sc->sc_handler[irq].iq_irq_min = min;

		/* Enable interrupts at lower levels, clear -> enable */
		/* Set interrupt priority/enable */
		if (min != IPL_NONE) {
			ampintc_set_priority(irq, min);
			ampintc_intr_enable(irq);
			ampintc_route(irq, IRQ_ENABLE, ci);
		} else {
			ampintc_intr_disable(irq);
			ampintc_route(irq, IRQ_DISABLE, ci);
		}
	}
	ampintc_setipl(ci->ci_cpl);
}

void
ampintc_splx(int new)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_ipending & arm_smask[new])
		arm_do_pending_intr(new);

	ampintc_setipl(new);
}

int
ampintc_spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;
	ampintc_splx(new);
	return (old);
}

int
ampintc_splraise(int new)
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

	ampintc_setipl(new);

	return (old);
}


uint32_t
ampintc_iack(void)
{
	uint32_t intid;
	struct ampintc_softc	*sc = ampintc;

	intid = bus_space_read_4(sc->sc_iot, sc->sc_p_ioh, ICPIAR);

	return (intid);
}

void
ampintc_eoi(uint32_t eoi)
{
	struct ampintc_softc	*sc = ampintc;

	bus_space_write_4(sc->sc_iot, sc->sc_p_ioh, ICPEOIR, eoi);
}

void
ampintc_route(int irq, int enable, struct cpu_info *ci)
{
	struct ampintc_softc	*sc = ampintc;
	uint8_t			 mask, val;

	KASSERT(ci->ci_cpuid <= ICD_ICTR_CPU_M);
	mask = sc->sc_cpu_mask[ci->ci_cpuid];

	val = bus_space_read_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPTRn(irq));
	if (enable == IRQ_ENABLE)
		val |= mask;
	else
		val &= ~mask;
	bus_space_write_1(sc->sc_iot, sc->sc_d_ioh, ICD_IPTRn(irq), val);
}

void
ampintc_cpuinit(void)
{
	struct ampintc_softc    *sc = ampintc;
	int			 i;

	/* XXX - this is the only cpu specific call to set this */
	if (sc->sc_cpu_mask[cpu_number()] == 0) {
		for (i = 0; i < 32; i++) {
			int cpumask =
			    bus_space_read_1(sc->sc_iot, sc->sc_d_ioh,
			        ICD_IPTRn(i));

			if (cpumask != 0) {
				sc->sc_cpu_mask[cpu_number()] = cpumask;
				break;
			}
		}
	}

	if (sc->sc_cpu_mask[cpu_number()] == 0)
		panic("could not determine cpu target mask");
}

void
ampintc_route_irq(void *v, int enable, struct cpu_info *ci)
{
	struct ampintc_softc    *sc = ampintc;
	struct intrhand         *ih = v;

	bus_space_write_4(sc->sc_iot, sc->sc_p_ioh, ICPICR, 1);
	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_ICRn(ih->ih_irq), 0);
	if (enable) {
		ampintc_set_priority(ih->ih_irq,
		    sc->sc_handler[ih->ih_irq].iq_irq_min);
		ampintc_intr_enable(ih->ih_irq);
	}

	ampintc_route(ih->ih_irq, enable, ci);
}

void
ampintc_irq_handler(void *frame)
{
	struct ampintc_softc	*sc = ampintc;
	struct intrhand		*ih;
	void			*arg;
	uint32_t		 iack_val;
	int			 irq, pri, s, handled;

	iack_val = ampintc_iack();
#ifdef DEBUG_INTC
	if (iack_val != 27)
		printf("irq  %d fired\n", iack_val);
	else {
		static int cnt = 0;
		if ((cnt++ % 100) == 0) {
			printf("irq  %d fired * _100\n", iack_val);
#ifdef DDB
			db_enter();
#endif
		}

	}
#endif

	irq = iack_val & ICPIAR_IRQ_M;

	if (irq == 1023) {
		sc->sc_spur.ec_count++;
		return;
	}

	if (irq >= sc->sc_nintr)
		return;

	pri = sc->sc_handler[irq].iq_irq_max;
	s = ampintc_splraise(pri);
	TAILQ_FOREACH(ih, &sc->sc_handler[irq].iq_list, ih_list) {
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

		enable_interrupts(PSR_I);
		handled = ih->ih_func(arg);
		disable_interrupts(PSR_I);
		if (handled)
			ih->ih_count.ec_count++;

#ifdef MULTIPROCESSOR
		if (need_lock)
			KERNEL_UNLOCK();
#endif
	}
	ampintc_eoi(iack_val);

	ampintc_splx(s);
}

void *
ampintc_intr_establish_ext(int irqno, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	return ampintc_intr_establish(irqno+32, IST_LEVEL_HIGH, level,
	    ci, func, arg, name);
}

void *
ampintc_intr_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct ampintc_softc	*sc = (struct ampintc_softc *)cookie;
	int			 irq;
	int			 type;

	/* 2nd cell contains the interrupt number */
	irq = cell[1];

	/* 1st cell contains type: 0 SPI (32-X), 1 PPI (16-31) */
	if (cell[0] == 0)
		irq += 32;
	else if (cell[0] == 1)
		irq += 16;
	else
		panic("%s: bogus interrupt type", sc->sc_sbus.sc_dev.dv_xname);

	/* SPIs are only active-high level or low-to-high edge */
	if (cell[2] & 0x3)
		type = IST_EDGE_RISING;
	else
		type = IST_LEVEL_HIGH;

	return ampintc_intr_establish(irq, type, level, ci, func, arg, name);
}

void *
ampintc_intr_establish(int irqno, int type, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	struct ampintc_softc	*sc = ampintc;
	struct intrhand		*ih;
	int			 psw;

	if (irqno < 0 || irqno >= sc->sc_nintr)
		panic("ampintc_intr_establish: bogus irqnumber %d: %s",
		     irqno, name);

	if (ci == NULL)
		ci = &cpu_info_primary;
	else if (!CPU_IS_PRIMARY(ci))
		return NULL;

	if (irqno < 16) {
		/* SGI are only EDGE */
		type = IST_EDGE_RISING;
	} else if (irqno < 32) {
		/* PPI are only LEVEL */
		type = IST_LEVEL_HIGH;
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_flags = level & IPL_FLAGMASK;
	ih->ih_irq = irqno;
	ih->ih_name = name;

	psw = disable_interrupts(PSR_I);

	TAILQ_INSERT_TAIL(&sc->sc_handler[irqno].iq_list, ih, ih_list);

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

#ifdef DEBUG_INTC
	printf("ampintc_intr_establish irq %d level %d [%s]\n", irqno, level,
	    name);
#endif

	ampintc_intr_config(irqno, type);
	ampintc_calc_mask();

	restore_interrupts(psw);
	return (ih);
}

void
ampintc_intr_disestablish(void *cookie)
{
	struct ampintc_softc	*sc = ampintc;
	struct intrhand		*ih = cookie;
	int			 psw;

#ifdef DEBUG_INTC
	printf("ampintc_intr_disestablish irq %d level %d [%s]\n",
	    ih->ih_irq, ih->ih_ipl, ih->ih_name);
#endif

	psw = disable_interrupts(PSR_I);

	TAILQ_REMOVE(&sc->sc_handler[ih->ih_irq].iq_list, ih, ih_list);
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));

	ampintc_calc_mask();

	restore_interrupts(psw);
}

const char *
ampintc_intr_string(void *cookie)
{
	struct intrhand *ih = (struct intrhand *)cookie;
	static char irqstr[1 + sizeof("ampintc irq ") + 4];

	snprintf(irqstr, sizeof irqstr, "ampintc irq %d", ih->ih_irq);
	return irqstr;
}

/*
 * GICv2m frame controller for MSI interrupts.
 */
#define GICV2M_TYPER		0x008
#define  GICV2M_TYPER_SPI_BASE(x)	(((x) >> 16) & 0x3ff)
#define  GICV2M_TYPER_SPI_COUNT(x)	(((x) >> 0) & 0x3ff)
#define GICV2M_SETSPI_NS	0x040

int	 ampintc_msi_match(struct device *, void *, void *);
void	 ampintc_msi_attach(struct device *, struct device *, void *);
void	*ampintc_intr_establish_msi(void *, uint64_t *, uint64_t *,
	    int , struct cpu_info *, int (*)(void *), void *, char *);
void	 ampintc_intr_disestablish_msi(void *);

struct ampintc_msi_softc {
	struct device			 sc_dev;
	bus_space_tag_t			 sc_iot;
	bus_space_handle_t		 sc_ioh;
	paddr_t				 sc_addr;
	int				 sc_bspi;
	int				 sc_nspi;
	void				**sc_spi;
	struct interrupt_controller	 sc_ic;
};

const struct cfattach	ampintcmsi_ca = {
	sizeof (struct ampintc_msi_softc), ampintc_msi_match, ampintc_msi_attach
};

struct cfdriver ampintcmsi_cd = {
	NULL, "ampintcmsi", DV_DULL
};

int
ampintc_msi_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "arm,gic-v2m-frame");
}

void
ampintc_msi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ampintc_msi_softc *sc = (struct ampintc_msi_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t typer;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/* XXX: Hack to retrieve the physical address (from a CPU PoV). */
	if (!pmap_extract(pmap_kernel(), sc->sc_ioh, &sc->sc_addr)) {
		printf(": cannot retrieve msi addr\n");
		return;
	}

	typer = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GICV2M_TYPER);
	sc->sc_bspi = GICV2M_TYPER_SPI_BASE(typer);
	sc->sc_nspi = GICV2M_TYPER_SPI_COUNT(typer);

	sc->sc_bspi = OF_getpropint(faa->fa_node,
	    "arm,msi-base-spi", sc->sc_bspi);
	sc->sc_nspi = OF_getpropint(faa->fa_node,
	    "arm,msi-num-spis", sc->sc_nspi);

	printf(": nspi %d\n", sc->sc_nspi);

	sc->sc_spi = mallocarray(sc->sc_nspi, sizeof(void *), M_DEVBUF,
	    M_WAITOK|M_ZERO);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish_msi = ampintc_intr_establish_msi;
	sc->sc_ic.ic_disestablish = ampintc_intr_disestablish_msi;
	arm_intr_register_fdt(&sc->sc_ic);
}

void *
ampintc_intr_establish_msi(void *self, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct ampintc_msi_softc *sc = (struct ampintc_msi_softc *)self;
	void *cookie;
	int i;

	for (i = 0; i < sc->sc_nspi; i++) {
		if (sc->sc_spi[i] != NULL)
			continue;

		cookie = ampintc_intr_establish(sc->sc_bspi + i,
		    IST_EDGE_RISING, level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		*addr = sc->sc_addr + GICV2M_SETSPI_NS;
		*data = sc->sc_bspi + i;
		sc->sc_spi[i] = cookie;
		return &sc->sc_spi[i];
	}

	return NULL;
}

void
ampintc_intr_disestablish_msi(void *cookie)
{
	ampintc_intr_disestablish(*(void **)cookie);
	*(void **)cookie = NULL;
}

#ifdef MULTIPROCESSOR
int
ampintc_ipi_ddb(void *v)
{
	/* XXX */
	db_enter();
	return 1;
}

int
ampintc_ipi_nop(void *v)
{
	/* Nothing to do here, just enough to wake up from WFI */
	return 1;
}

int
ampintc_ipi_combined(void *v)
{
	struct ampintc_softc *sc = (struct ampintc_softc *)v;

	if (sc->sc_ipi_reason[cpu_number()] == ARM_IPI_DDB) {
		sc->sc_ipi_reason[cpu_number()] = ARM_IPI_NOP;
		return ampintc_ipi_ddb(v);
	} else {
		return ampintc_ipi_nop(v);
	}
}

void
ampintc_send_ipi(struct cpu_info *ci, int id)
{
	struct ampintc_softc	*sc = ampintc;
	int sendmask;

	if (ci == curcpu() && id == ARM_IPI_NOP)
		return;

	/* never overwrite IPI_DDB with IPI_NOP */
	if (id == ARM_IPI_DDB)
		sc->sc_ipi_reason[ci->ci_cpuid] = id;

	/* currently will only send to one cpu */
	sendmask = 1 << (16 + ci->ci_cpuid);
	sendmask |= sc->sc_ipi_num[id];

	bus_space_write_4(sc->sc_iot, sc->sc_d_ioh, ICD_SGIR, sendmask);
}
#endif
