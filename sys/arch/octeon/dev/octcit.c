/*	$OpenBSD: octcit.c,v 1.14 2022/12/11 05:31:05 visa Exp $	*/

/*
 * Copyright (c) 2017, 2019 Visa Hankala
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
 * Driver for OCTEON Central Interrupt Unit version 3 (CIU3).
 *
 * CIU3 is present on CN72xx, CN73xx, CN77xx, and CN78xx.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <mips64/mips_cpu.h>

#include <machine/autoconf.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/octeonreg.h>

#define CIU3_IDT(core, ipl)		((core) * 4 + (ipl))
#define CIU3_IDT_CTL(idt)		((idt) *  8 + 0x110000u)
#define CIU3_IDT_PP(idt)		((idt) * 32 + 0x120000u)
#define CIU3_IDT_IO(idt)		((idt) *  8 + 0x130000u)
#define CIU3_DEST_PP_INT(core)		((core) * 8 + 0x200000u)
#define   CIU3_DEST_PP_INT_INTSN		0x000fffff00000000ull
#define   CIU3_DEST_PP_INT_INTSN_SHIFT		32
#define   CIU3_DEST_PP_INT_INTR			0x0000000000000001ull
#define CIU3_ISC_CTL(intsn)		((intsn) * 8 + 0x80000000u)
#define   CIU3_ISC_CTL_IDT			0x0000000000ff0000ull
#define   CIU3_ISC_CTL_IDT_SHIFT		16
#define   CIU3_ISC_CTL_IMP			0x0000000000008000ull
#define   CIU3_ISC_CTL_EN			0x0000000000000002ull
#define   CIU3_ISC_CTL_RAW			0x0000000000000001ull
#define CIU3_ISC_W1C(intsn)		((intsn) * 8 + 0x90000000u)
#define   CIU3_ISC_W1C_EN			0x0000000000000002ull
#define   CIU3_ISC_W1C_RAW			0x0000000000000001ull
#define CIU3_ISC_W1S(intsn)		((intsn) * 8 + 0xa0000000u)
#define   CIU3_ISC_W1S_EN			0x0000000000000002ull
#define   CIU3_ISC_W1S_RAW			0x0000000000000001ull
#define CIU3_NINTSN			(1u << 20)

#define IS_MBOX(intsn)			(((intsn) >> 12) == 4)
#define MBOX_INTSN(core)		((core) + 0x4000u)

#define CIU3_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define CIU3_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

#define INTPRI_CIU_0	(INTPRI_CLOCK + 1)

#define HASH_SIZE			64

struct octcit_intrhand {
	SLIST_ENTRY(octcit_intrhand)
				 ih_list;
	int			(*ih_func)(void *);
	void			*ih_arg;
	int			 ih_intsn;
	int			 ih_flags;
#define CIH_MPSAFE			0x01
#define CIH_EDGE			0x02	/* edge-triggered */
	int			 ih_level;
	struct evcount		 ih_count;
};

struct octcit_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	SLIST_HEAD(, octcit_intrhand)
				 sc_handlers[HASH_SIZE];
	int			 sc_minipl[MAXCPUS];
	int			(*sc_ipi_handler)(void *);

	struct intr_controller	 sc_ic;
};

int	 octcit_match(struct device *, void *, void *);
void	 octcit_attach(struct device *, struct device *, void *);

void	 octcit_init(void);
uint32_t octcit_intr(uint32_t, struct trapframe *);
void	*octcit_intr_establish(int, int, int (*)(void *), void *,
	    const char *);
void	*octcit_intr_establish_intsn(int, int, int, int (*)(void *),
	    void *, const char *);
void	*octcit_intr_establish_fdt_idx(void *, int, int, int,
	    int (*)(void *), void *, const char *);
void	 octcit_intr_disestablish(void *);
void	 octcit_intr_barrier(void *);
void	 octcit_splx(int);

uint32_t octcit_ipi_intr(uint32_t, struct trapframe *);
int	 octcit_ipi_establish(int (*)(void *), cpuid_t);
void	 octcit_ipi_set(cpuid_t);
void	 octcit_ipi_clear(cpuid_t);

const struct cfattach octcit_ca = {
	sizeof(struct octcit_softc), octcit_match, octcit_attach
};

struct cfdriver octcit_cd = {
	NULL, "octcit", DV_DULL
};

struct octcit_softc	*octcit_sc;

int
octcit_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-7890-ciu3");
}

void
octcit_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct octcit_softc *sc = (struct octcit_softc *)self;
	uint64_t val;
	int hash, intsn;

	if (faa->fa_nreg != 1) {
		printf(": expected one IO space, got %d\n", faa->fa_nreg);
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": could not map IO space\n");
		return;
	}

	for (hash = 0; hash < HASH_SIZE; hash++)
		SLIST_INIT(&sc->sc_handlers[hash]);

	/* Disable all interrupts and acknowledge any pending ones. */
	for (intsn = 0; intsn < CIU3_NINTSN; intsn++) {
		val = CIU3_RD_8(sc, CIU3_ISC_CTL(intsn));
		if (ISSET(val, CIU3_ISC_CTL_IMP)) {
			CIU3_WR_8(sc, CIU3_ISC_W1C(intsn), CIU3_ISC_CTL_RAW);
			CIU3_WR_8(sc, CIU3_ISC_CTL(intsn), 0);
			(void)CIU3_RD_8(sc, CIU3_ISC_CTL(intsn));
		}
	}

	printf("\n");

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_init = octcit_init;
	sc->sc_ic.ic_establish = octcit_intr_establish;
	sc->sc_ic.ic_establish_fdt_idx = octcit_intr_establish_fdt_idx;
	sc->sc_ic.ic_disestablish = octcit_intr_disestablish;
	sc->sc_ic.ic_intr_barrier = octcit_intr_barrier;
#ifdef MULTIPROCESSOR
	sc->sc_ic.ic_ipi_establish = octcit_ipi_establish;
	sc->sc_ic.ic_ipi_set = octcit_ipi_set;
	sc->sc_ic.ic_ipi_clear = octcit_ipi_clear;
#endif

	octcit_sc = sc;

	set_intr(INTPRI_CIU_0, CR_INT_0, octcit_intr);
#ifdef MULTIPROCESSOR
	set_intr(INTPRI_IPI, CR_INT_1, octcit_ipi_intr);
#endif

	octcit_init();

	register_splx_handler(octcit_splx);
	octeon_intr_register(&sc->sc_ic);
}

static inline int
intsn_hash(int intsn)
{
	int tmp;

	tmp = intsn * 0xffb;
	return ((tmp >> 14) ^ tmp) & (HASH_SIZE - 1);
}

void
octcit_init(void)
{
	struct cpu_info *ci = curcpu();
	struct octcit_softc *sc = octcit_sc;
	int core = ci->ci_cpuid;

	sc->sc_minipl[ci->ci_cpuid] = IPL_HIGH;

	/*
	 * Set up interrupt routing.
	 */

	/* Route IP2. */
	CIU3_WR_8(sc, CIU3_IDT_CTL(CIU3_IDT(core, 0)), 0);
	CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 0)), 1ul << core);
	CIU3_WR_8(sc, CIU3_IDT_IO(CIU3_IDT(core, 0)), 0);

	/* Route IP3. */
	CIU3_WR_8(sc, CIU3_IDT_CTL(CIU3_IDT(core , 1)), 1);
	CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 1)), 1ul << core);
	CIU3_WR_8(sc, CIU3_IDT_IO(CIU3_IDT(core, 1)), 0);

	/* Disable IP4. */
	CIU3_WR_8(sc, CIU3_IDT_CTL(CIU3_IDT(core, 2)), 0);
	CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 2)), 0);
	CIU3_WR_8(sc, CIU3_IDT_IO(CIU3_IDT(core, 2)), 0);

	/* Disable IP5. */
	CIU3_WR_8(sc, CIU3_IDT_CTL(CIU3_IDT(core, 3)), 0);
	CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 3)), 0);
	CIU3_WR_8(sc, CIU3_IDT_IO(CIU3_IDT(core, 3)), 0);
}

void *
octcit_intr_establish(int irq, int level, int (*func)(void *), void *arg,
    const char *name)
{
	return octcit_intr_establish_intsn(irq, level, CIH_EDGE, func, arg,
	    name);
}

void *
octcit_intr_establish_intsn(int intsn, int level, int flags,
    int (*func)(void *), void *arg, const char *name)
{
	struct cpu_info *ci = curcpu();
	struct octcit_intrhand *ih;
	struct octcit_softc *sc = octcit_sc;
	uint64_t val;
	int s;

	if ((unsigned int)intsn > CIU3_NINTSN)
		panic("%s: illegal intsn 0x%x", __func__, intsn);

	if (IS_MBOX(intsn))
		panic("%s: mbox intsn 0x%x not allowed", __func__, intsn);

	if (ISSET(level, IPL_MPSAFE))
		flags |= CIH_MPSAFE;
	level &= ~IPL_MPSAFE;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return NULL;

	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_level = level;
	ih->ih_flags = flags;
	ih->ih_intsn = intsn;
	evcount_attach(&ih->ih_count, name, &ih->ih_intsn);
	evcount_percpu(&ih->ih_count);

	s = splhigh();

	SLIST_INSERT_HEAD(&sc->sc_handlers[intsn_hash(intsn)], ih, ih_list);
	if (sc->sc_minipl[ci->ci_cpuid] > level)
		sc->sc_minipl[ci->ci_cpuid] = level;

	val = CIU3_ISC_CTL_EN | (CIU3_IDT(ci->ci_cpuid, 0) <<
	    CIU3_ISC_CTL_IDT_SHIFT);
	CIU3_WR_8(sc, CIU3_ISC_W1C(intsn), CIU3_ISC_W1C_EN);
	CIU3_WR_8(sc, CIU3_ISC_CTL(intsn), val);
	(void)CIU3_RD_8(sc, CIU3_ISC_CTL(intsn));

	splx(s);

	return ih;
}

void *
octcit_intr_establish_fdt_idx(void *cookie, int node, int idx, int level,
    int (*func)(void *), void *arg, const char *name)
{
	uint32_t *cells;
	int flags = 0;
	int intsn, len, type;

	len = OF_getproplen(node, "interrupts");
	if (len / (sizeof(uint32_t) * 2) <= idx ||
	    len % (sizeof(uint32_t) * 2) != 0)
		return NULL;

	cells = malloc(len, M_TEMP, M_NOWAIT);
	if (cells == NULL)
		return NULL;

	OF_getpropintarray(node, "interrupts", cells, len);
	intsn = cells[idx * 2];
	type = cells[idx * 2 + 1];

	free(cells, M_TEMP, len);

	if (type != 4)
		flags |= CIH_EDGE;

	return octcit_intr_establish_intsn(intsn, level, flags, func, arg,
	    name);
}

void
octcit_intr_disestablish(void *_ih)
{
	struct cpu_info *ci = curcpu();
	struct octcit_intrhand *ih = _ih;
	struct octcit_intrhand *tmp;
	struct octcit_softc *sc = octcit_sc;
	unsigned int count;
	int found = 0;
	int hash = intsn_hash(ih->ih_intsn);
	int i, s;

	count = 0;
	SLIST_FOREACH(tmp, &sc->sc_handlers[hash], ih_list) {
		if (tmp->ih_intsn == ih->ih_intsn)
			count++;
		if (tmp == ih)
			found = 1;
	}
	if (found == 0)
		panic("%s: intrhand %p not registered", __func__, ih);

	s = splhigh();

	if (count == 0) {
		CIU3_WR_8(sc, CIU3_ISC_W1C(ih->ih_intsn), CIU3_ISC_W1C_EN);
		CIU3_WR_8(sc, CIU3_ISC_CTL(ih->ih_intsn), 0);
		(void)CIU3_RD_8(sc, CIU3_ISC_CTL(ih->ih_intsn));
	}

	SLIST_REMOVE(&sc->sc_handlers[hash], ih, octcit_intrhand, ih_list);
	evcount_detach(&ih->ih_count);

	/* Recompute IPL floor if necessary. */
	if (sc->sc_minipl[ci->ci_cpuid] == ih->ih_level) {
		sc->sc_minipl[ci->ci_cpuid] = IPL_HIGH;
		for (i = 0; i < HASH_SIZE; i++) {
			SLIST_FOREACH(tmp, &sc->sc_handlers[i], ih_list) {
				if (sc->sc_minipl[ci->ci_cpuid] >
				    tmp->ih_level)
					sc->sc_minipl[ci->ci_cpuid] =
					    tmp->ih_level;
			}
		}
	}

	splx(s);

	free(ih, M_DEVBUF, sizeof(*ih));
}

void
octcit_intr_barrier(void *_ih)
{
	sched_barrier(NULL);
}

uint32_t
octcit_intr(uint32_t hwpend, struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	struct octcit_intrhand *ih;
	struct octcit_softc *sc = octcit_sc;
	uint64_t destpp;
	uint64_t intsn;
	unsigned int core = ci->ci_cpuid;
	int handled = 0;
	int ipl;
	int ret;
#ifdef MULTIPROCESSOR
	register_t sr;
	int need_lock;
#endif

	if (frame->ipl >= sc->sc_minipl[ci->ci_cpuid]) {
		/* Disable IP2. */
		CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 0)), 0);
		(void)CIU3_RD_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 0)));
		return hwpend;
	}

	destpp = CIU3_RD_8(sc, CIU3_DEST_PP_INT(core));
	if (!ISSET(destpp, CIU3_DEST_PP_INT_INTR))
		goto spurious;

	ipl = ci->ci_ipl;

	intsn = (destpp & CIU3_DEST_PP_INT_INTSN) >>
	    CIU3_DEST_PP_INT_INTSN_SHIFT;
	SLIST_FOREACH(ih, &sc->sc_handlers[intsn_hash(intsn)], ih_list) {
		if (ih->ih_intsn != intsn)
			continue;

		splraise(ih->ih_level);

		/* Acknowledge the interrupt. */
		if (ISSET(ih->ih_flags, CIH_EDGE)) {
			CIU3_WR_8(sc, CIU3_ISC_W1C(intsn), CIU3_ISC_CTL_RAW);
			(void)CIU3_RD_8(sc, CIU3_ISC_W1C(intsn));
		}

#ifdef MULTIPROCESSOR
		if (ih->ih_level < IPL_IPI) {
			sr = getsr();
			ENABLEIPI();
		}
		if (ISSET(ih->ih_flags, CIH_MPSAFE))
			need_lock = 0;
		else
			need_lock = 1;
		if (need_lock)
			__mp_lock(&kernel_lock);
#endif
		ret = (*ih->ih_func)(ih->ih_arg);
#ifdef MULTIPROCESSOR
		if (need_lock)
			__mp_unlock(&kernel_lock);
		if (ih->ih_level < IPL_IPI)
			setsr(sr);
#endif

		if (ret != 0) {
			handled = 1;
			evcount_inc(&ih->ih_count);
		}

		/*
		 * Stop processing when one handler has claimed the interrupt.
		 * This saves cycles because interrupt sharing should not
		 * happen on this hardware.
		 */
		if (ret == 1)
			break;
	}

	ci->ci_ipl = ipl;

spurious:
	if (handled == 0)
		printf("%s: spurious interrupt 0x%016llx on cpu %lu\n",
		    sc->sc_dev.dv_xname, destpp, ci->ci_cpuid);

	return hwpend;
}

void
octcit_splx(int newipl)
{
	struct octcit_softc *sc = octcit_sc;
	struct cpu_info *ci = curcpu();
	unsigned int core = ci->ci_cpuid;

	ci->ci_ipl = newipl;

	if (newipl < sc->sc_minipl[ci->ci_cpuid]) {
		CIU3_WR_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 0)), 1ul << core);
		(void)CIU3_RD_8(sc, CIU3_IDT_PP(CIU3_IDT(core, 0)));
	}

	/* Trigger deferred clock interrupt if it is now unmasked. */
	if (ci->ci_clock_deferred && newipl < IPL_CLOCK)
		md_triggerclock();

	/* If we still have softints pending trigger processing. */
	if (ci->ci_softpending != 0 && newipl < IPL_SOFTINT)
		setsoftintr0();
}

#ifdef MULTIPROCESSOR
uint32_t
octcit_ipi_intr(uint32_t hwpend, struct trapframe *frame)
{
	struct octcit_softc *sc = octcit_sc;
	u_long cpuid = cpu_number();

	if (sc->sc_ipi_handler != NULL)
		sc->sc_ipi_handler((void *)cpuid);

	return hwpend;
}

int
octcit_ipi_establish(int (*func)(void *), cpuid_t cpuid)
{
	struct octcit_softc *sc = octcit_sc;
	uint64_t val;
	int intsn;

	if (cpuid == 0)
		sc->sc_ipi_handler = func;

	intsn = MBOX_INTSN(cpuid);
	val = CIU3_ISC_CTL_EN | (CIU3_IDT(cpuid, 1) << CIU3_ISC_CTL_IDT_SHIFT);
	CIU3_WR_8(sc, CIU3_ISC_W1C(intsn), CIU3_ISC_W1C_EN);
	CIU3_WR_8(sc, CIU3_ISC_CTL(intsn), val);
	(void)CIU3_RD_8(sc, CIU3_ISC_CTL(intsn));

	return 0;
}

void
octcit_ipi_set(cpuid_t cpuid)
{
	struct octcit_softc *sc = octcit_sc;
	uint64_t reg = CIU3_ISC_W1S(MBOX_INTSN(cpuid));

	CIU3_WR_8(sc, reg, CIU3_ISC_W1S_RAW);
	(void)CIU3_RD_8(sc, reg);
}

void
octcit_ipi_clear(cpuid_t cpuid)
{
	struct octcit_softc *sc = octcit_sc;
	uint64_t reg = CIU3_ISC_W1C(MBOX_INTSN(cpuid));

	CIU3_WR_8(sc, reg, CIU3_ISC_W1C_RAW);
	(void)CIU3_RD_8(sc, reg);
}
#endif /* MULTIPROCESSOR */
