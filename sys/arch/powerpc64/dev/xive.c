/*	$OpenBSD: xive.c,v 1.17 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define XIVE_NUM_PRIORITIES	8
#define XIVE_NUM_IRQS		1024

#define XIVE_EQ_SIZE		PAGE_SHIFT
#define XIVE_EQ_IDX_MASK	((1 << (PAGE_SHIFT - 2)) - 1)
#define XIVE_EQ_GEN_MASK	0x80000000

#define XIVE_TM_CPPR_HV		0x031

#define XIVE_TM_SPC_ACK_HV	0x830
#define  XIVE_TM_SPC_ACK_HE_MASK	0xc000
#define  XIVE_TM_SPC_ACK_HE_NONE	0x0000
#define  XIVE_TM_SPC_ACK_HE_PHYS	0x8000

#define XIVE_ESB_STORE_TRIGGER	0x000
#define XIVE_ESB_LOAD_EOI	0x000
#define XIVE_ESB_STORE_EOI	0x400
#define XIVE_ESB_SET_PQ_00	0xc00
#define XIVE_ESB_SET_PQ_01	0xd00
#define XIVE_ESB_SET_PQ_10	0xe00
#define XIVE_ESB_SET_PQ_11	0xf00

#define XIVE_ESB_VAL_P	0x2
#define XIVE_ESB_VAL_Q	0x1

static inline uint8_t
xive_prio(int ipl)
{
	return ((IPL_IPI - ipl) > 7 ? 0xff : IPL_IPI - ipl);
}

static inline int
xive_ipl(uint8_t prio)
{
	return (IPL_IPI - prio);
}

struct intrhand {
	TAILQ_ENTRY(intrhand)	ih_list;
	int			(*ih_func)(void *);
	void			*ih_arg;
	int			ih_ipl;
	int			ih_flags;
	uint32_t		ih_girq;
	struct evcount		ih_count;
	const char		*ih_name;

	bus_space_handle_t	ih_esb_eoi;
	bus_space_handle_t	ih_esb_trig;
	uint64_t		ih_xive_flags;
};

struct xive_eq {
	struct xive_dmamem	*eq_queue;
	uint32_t		eq_idx;
	uint32_t		eq_gen;
};

struct xive_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	struct intrhand		*sc_handler[XIVE_NUM_IRQS];
	struct xive_eq		sc_eq[MAXCPUS][XIVE_NUM_PRIORITIES];

	uint32_t		sc_page_size;
	uint32_t		sc_lirq;
};

struct xive_softc *xive_sc;

struct xive_dmamem {
	bus_dmamap_t		xdm_map;
	bus_dma_segment_t	xdm_seg;
	size_t			xdm_size;
	caddr_t			xdm_kva;
};

#define XIVE_DMA_MAP(_xdm)	((_xdm)->xdm_map)
#define XIVE_DMA_LEN(_xdm)	((_xdm)->xdm_size)
#define XIVE_DMA_DVA(_xdm)	((_xdm)->xdm_map->dm_segs[0].ds_addr)
#define XIVE_DMA_KVA(_xdm)	((void *)(_xdm)->xdm_kva)

struct xive_dmamem *xive_dmamem_alloc(bus_dma_tag_t, bus_size_t,
	    bus_size_t);
void	xive_dmamem_free(bus_dma_tag_t, struct xive_dmamem *);

static inline void
xive_write_1(struct xive_softc *sc, bus_size_t off, uint8_t val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, val);
}

static inline uint16_t
xive_read_2(struct xive_softc *sc, bus_size_t off)
{
	return bus_space_read_2(sc->sc_iot, sc->sc_ioh, off);
}

static inline void
xive_unmask(struct xive_softc *sc, struct intrhand *ih)
{
	bus_space_read_8(sc->sc_iot, ih->ih_esb_eoi, XIVE_ESB_SET_PQ_00);
}

int	xive_match(struct device *, void *, void *);
void	xive_attach(struct device *, struct device *, void *);
int	xive_activate(struct device *, int);

const struct cfattach xive_ca = {
	sizeof (struct xive_softc), xive_match, xive_attach, NULL,
	xive_activate
};

struct cfdriver xive_cd = {
	NULL, "xive", DV_DULL
};

void	xive_hvi(struct trapframe *);
void 	*xive_intr_establish(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *);
void	xive_intr_send_ipi(void *);
void	xive_setipl(int);

int
xive_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ibm,opal-xive-pe");
}

void
xive_attach(struct device *parent, struct device *self, void *aux)
{
	struct xive_softc *sc = (struct xive_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int64_t error;
	int i;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_dmat = faa->fa_dmat;

	sc->sc_page_size = OF_getpropint(faa->fa_node,
	    "ibm,xive-provision-page-size", 0);

	error = opal_xive_reset(OPAL_XIVE_MODE_EXPL);
	if (error != OPAL_SUCCESS)
		printf(": can't enable exploitation mode\n");

	printf("\n");

	CPU_INFO_FOREACH(cii, ci) {
		for (i = 0; i < XIVE_NUM_PRIORITIES; i++) {
			sc->sc_eq[ci->ci_cpuid][i].eq_queue =
			    xive_dmamem_alloc(sc->sc_dmat,
			    1 << XIVE_EQ_SIZE, 1 << XIVE_EQ_SIZE);
			if (sc->sc_eq[ci->ci_cpuid][i].eq_queue == NULL) {
				printf("%s: can't allocate event queue\n",
				    sc->sc_dev.dv_xname);
				return;
			}

			error = opal_xive_set_queue_info(ci->ci_pir, i,
			    XIVE_DMA_DVA(sc->sc_eq[ci->ci_cpuid][i].eq_queue),
			    XIVE_EQ_SIZE, OPAL_XIVE_EQ_ENABLED |
			    OPAL_XIVE_EQ_ALWAYS_NOTIFY);
			if (error != OPAL_SUCCESS) {
				printf("%s: can't enable event queue\n",
				    sc->sc_dev.dv_xname);
				return;
			}

			sc->sc_eq[ci->ci_cpuid][i].eq_gen = XIVE_EQ_GEN_MASK;
		}
	}

	/* There can be only one. */
	KASSERT(xive_sc == NULL);
	xive_sc = sc;

	_hvi = xive_hvi;
	_intr_establish = xive_intr_establish;
	_intr_send_ipi = xive_intr_send_ipi;
	_setipl = xive_setipl;

	/* Synchronize hardware state to software state. */
	xive_write_1(sc, XIVE_TM_CPPR_HV, xive_prio(curcpu()->ci_cpl));
	eieio();
}

int
xive_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		opal_xive_reset(OPAL_XIVE_MODE_EMU);
		break;
	}

	return 0;
}

void *
xive_intr_establish(uint32_t girq, int type, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *name)
{
	struct xive_softc *sc = xive_sc;
	struct intrhand *ih;
	bus_space_handle_t eoi, trig;
	bus_size_t page_size;
	uint64_t flags, eoi_page, trig_page;
	uint32_t esb_shift, lirq;
	int64_t error;

	if (ci == NULL)
		ci = cpu_info_primary;

	/* Allocate a logical IRQ. */
	if (sc->sc_lirq >= XIVE_NUM_IRQS)
		return NULL;
	lirq = sc->sc_lirq++;

	error = opal_xive_get_irq_info(girq, opal_phys(&flags),
	    opal_phys(&eoi_page), opal_phys(&trig_page),
	    opal_phys(&esb_shift), NULL);
	if (error != OPAL_SUCCESS)
		return NULL;
	page_size = 1 << esb_shift;

	/* Map EOI page. */
	if (bus_space_map(sc->sc_iot, eoi_page, page_size, 0, &eoi))
		return NULL;

	/* Map trigger page. */
	if (trig_page == eoi_page)
		trig = eoi;
	else if (trig_page == 0)
		trig = 0;
	else if (bus_space_map(sc->sc_iot, trig_page, page_size, 0, &trig)) {
		bus_space_unmap(sc->sc_iot, trig, page_size);
		return NULL;
	}

	error = opal_xive_set_irq_config(girq, ci->ci_pir,
	    xive_prio(level & IPL_IRQMASK), lirq);
	if (error != OPAL_SUCCESS) {
		if (trig != eoi && trig != 0)
			bus_space_unmap(sc->sc_iot, trig, page_size);
		bus_space_unmap(sc->sc_iot, eoi, page_size);
		return NULL;
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_flags = level & IPL_FLAGMASK;
	ih->ih_girq = girq;
	ih->ih_name = name;
	ih->ih_esb_eoi = eoi;
	ih->ih_esb_trig = trig;
	ih->ih_xive_flags = flags;
	sc->sc_handler[lirq] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_girq);

	xive_unmask(sc, ih);

	return ih;
}

void
xive_intr_send_ipi(void *cookie)
{
	struct xive_softc *sc = xive_sc;
	struct intrhand *ih = cookie;

	if (ih && ih->ih_esb_trig)
		bus_space_write_8(sc->sc_iot, ih->ih_esb_trig,
		    XIVE_ESB_STORE_TRIGGER, 0);
}

void
xive_eoi(struct xive_softc *sc, struct intrhand *ih)
{
	uint64_t eoi;

	if (ih->ih_xive_flags & OPAL_XIVE_IRQ_STORE_EOI) {
		bus_space_write_8(sc->sc_iot, ih->ih_esb_eoi,
		    XIVE_ESB_STORE_EOI, 0);
	} else if (ih->ih_xive_flags & OPAL_XIVE_IRQ_LSI) {
		eoi = bus_space_read_8(sc->sc_iot, ih->ih_esb_eoi,
		    XIVE_ESB_LOAD_EOI);
	} else {
		eoi = bus_space_read_8(sc->sc_iot, ih->ih_esb_eoi,
		    XIVE_ESB_SET_PQ_00);
		if ((eoi & XIVE_ESB_VAL_Q) && ih->ih_esb_trig != 0)
			bus_space_write_8(sc->sc_iot, ih->ih_esb_trig,
			    XIVE_ESB_STORE_TRIGGER, 0);
	}
}

void
xive_setipl(int new)
{
	struct xive_softc *sc = xive_sc;
	struct cpu_info *ci = curcpu();
	uint8_t oldprio = xive_prio(ci->ci_cpl);
	uint8_t newprio = xive_prio(new);
	u_long msr;

	msr = intr_disable();
	ci->ci_cpl = new;
	if (newprio != oldprio) {
		xive_write_1(sc, XIVE_TM_CPPR_HV, newprio);
		eieio();
	}
	intr_restore(msr);
}

void
xive_run_handler(struct intrhand *ih)
{
	int handled;

#ifdef MULTIPROCESSOR
	int need_lock;

	if (ih->ih_flags & IPL_MPSAFE)
		need_lock = 0;
	else
		need_lock = (ih->ih_ipl < IPL_SCHED);

	if (need_lock)
		KERNEL_LOCK();
#endif
	handled = ih->ih_func(ih->ih_arg);
	if (handled)
		ih->ih_count.ec_count++;
#ifdef MULTIPROCESSOR
	if (need_lock)
		KERNEL_UNLOCK();
#endif
}

void
xive_hvi(struct trapframe *frame)
{
	struct xive_softc *sc = xive_sc;
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	struct xive_eq *eq;
	uint32_t *event;
	uint32_t lirq;
	int old, new;
	uint16_t ack, he;
	uint8_t cppr;

	old = ci->ci_cpl;

	while (1) {
		ack = xive_read_2(sc, XIVE_TM_SPC_ACK_HV);

		he = (ack & XIVE_TM_SPC_ACK_HE_MASK);
		if (he == XIVE_TM_SPC_ACK_HE_NONE)
			break;
		KASSERT(he == XIVE_TM_SPC_ACK_HE_PHYS);

		eieio();

		/* Synchronize software state to hardware state. */
		cppr = ack;
		new = xive_ipl(cppr);
		if (new <= old) {
			/*
			 * QEMU generates spurious interrupts.  It is
			 * unclear whether this can happen on real
			 * hardware as well.  We just ignore the
			 * interrupt, but we need to reset the CPPR
			 * register since we did accept the interrupt.
			 */
			goto spurious;
		}
		ci->ci_cpl = new;

		KASSERT(cppr < XIVE_NUM_PRIORITIES);
		eq = &sc->sc_eq[ci->ci_cpuid][cppr];
		event = XIVE_DMA_KVA(eq->eq_queue);
		while ((event[eq->eq_idx] & XIVE_EQ_GEN_MASK) == eq->eq_gen) {
			lirq = event[eq->eq_idx] & ~XIVE_EQ_GEN_MASK;
			KASSERT(lirq < XIVE_NUM_IRQS);
			ih = sc->sc_handler[lirq];
			if (ih != NULL) {
				intr_enable();
				xive_run_handler(ih);
				intr_disable();
				xive_eoi(sc, ih);
			}
			eq->eq_idx = (eq->eq_idx + 1) & XIVE_EQ_IDX_MASK;

			/* Toggle generation on wrap around. */
			if (eq->eq_idx == 0)
				eq->eq_gen ^= XIVE_EQ_GEN_MASK;
		}

		ci->ci_cpl = old;
	spurious:
		xive_write_1(sc, XIVE_TM_CPPR_HV, xive_prio(old));
		eieio();
	}
}

struct xive_dmamem *
xive_dmamem_alloc(bus_dma_tag_t dmat, bus_size_t size, bus_size_t align)
{
	struct xive_dmamem *xdm;
	int nsegs;

	xdm = malloc(sizeof(*xdm), M_DEVBUF, M_WAITOK | M_ZERO);
	xdm->xdm_size = size;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &xdm->xdm_map) != 0)
		goto xdmfree;

	if (bus_dmamem_alloc(dmat, size, align, 0, &xdm->xdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, &xdm->xdm_seg, nsegs, size,
	    &xdm->xdm_kva, BUS_DMA_WAITOK | BUS_DMA_NOCACHE) != 0)
		goto free;

	if (bus_dmamap_load_raw(dmat, xdm->xdm_map, &xdm->xdm_seg,
	    nsegs, size, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return xdm;

unmap:
	bus_dmamem_unmap(dmat, xdm->xdm_kva, size);
free:
	bus_dmamem_free(dmat, &xdm->xdm_seg, 1);
destroy:
	bus_dmamap_destroy(dmat, xdm->xdm_map);
xdmfree:
	free(xdm, M_DEVBUF, sizeof(*xdm));

	return NULL;
}

void
xive_dmamem_free(bus_dma_tag_t dmat, struct xive_dmamem *xdm)
{
	bus_dmamem_unmap(dmat, xdm->xdm_kva, xdm->xdm_size);
	bus_dmamem_free(dmat, &xdm->xdm_seg, 1);
	bus_dmamap_destroy(dmat, xdm->xdm_map);
	free(xdm, M_DEVBUF, sizeof(*xdm));
}
