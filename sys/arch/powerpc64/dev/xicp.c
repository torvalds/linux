/*	$OpenBSD: xicp.c,v 1.5 2022/04/06 18:59:27 naddy Exp $	*/
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

#define XICP_NUM_IRQS	1024

#define XICP_CPPR		0x04
#define XICP_XIRR		0x04
#define  XICP_XIRR_XISR_MASK	0x00ffffff
#define  XICP_XIRR_CPPR_SHIFT	24
#define XICP_MFRR		0x0c

static inline uint8_t
xicp_prio(int ipl)
{
	return ((IPL_IPI - ipl) > 7 ? 0xff : IPL_IPI - ipl);
}

struct intrhand {
	LIST_ENTRY(intrhand)	ih_hash;
	int			(*ih_func)(void *);
	void			*ih_arg;
	int			ih_ipl;
	int			ih_flags;
	uint32_t		ih_girq;
	struct evcount		ih_count;
	const char		*ih_name;
};

struct xicp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

struct xicp_softc *xicp_sc[MAXCPUS];

/* Hash table for interrupt handlers. */
#define XICP_GIRQHASH(girq)	(&xicp_girqhashtbl[(girq) & xicp_girqhash])
LIST_HEAD(,intrhand) *xicp_girqhashtbl;
u_long	xicp_girqhash;

static inline void
xicp_write_1(struct xicp_softc *sc, bus_size_t off, uint8_t val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, val);
}

static inline uint32_t
xicp_read_4(struct xicp_softc *sc, bus_size_t off)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, off);
}

static inline void
xicp_write_4(struct xicp_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, val);
}

int	xicp_match(struct device *, void *, void *);
void	xicp_attach(struct device *, struct device *, void *);

const struct cfattach xicp_ca = {
	sizeof (struct xicp_softc), xicp_match, xicp_attach
};

struct cfdriver xicp_cd = {
	NULL, "xicp", DV_DULL
};

void	xicp_exi(struct trapframe *);
void 	*xicp_intr_establish(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *);
void	xicp_intr_send_ipi(void *);
void	xicp_setipl(int);

int
xicp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "ibm,ppc-xicp") ||
	    OF_is_compatible(faa->fa_node, "IBM,ppc-xicp"));
}

void
xicp_attach(struct device *parent, struct device *self, void *aux)
{
	struct xicp_softc *sc = (struct xicp_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	uint32_t ranges[2];

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	ranges[0] = ranges[1] = 0;
	OF_getpropintarray(faa->fa_node, "ibm,interrupt-server-ranges",
	    ranges, sizeof(ranges));
	if (ranges[1] == 0)
		return;

	/*
	 * There is supposed to be one ICP node for each core.  Since
	 * we only support a single thread, we only need to map the
	 * first set of registers.
	 */
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	/*
	 * Allocate global hash table for interrupt handlers if we
	 * haven't done so already.
	 */
	if (xicp_girqhash == 0) {
		xicp_girqhashtbl = hashinit(XICP_NUM_IRQS,
		    M_DEVBUF, M_WAITOK, &xicp_girqhash);
	}

	CPU_INFO_FOREACH(cii, ci) {
		if (ranges[0] == ci->ci_pir)
			xicp_sc[ci->ci_cpuid] = sc;
	}

	_exi = xicp_exi;
	_intr_establish = xicp_intr_establish;
	_intr_send_ipi = xicp_intr_send_ipi;
	_setipl = xicp_setipl;

	/* Synchronize hardware state to software state. */
	xicp_write_1(sc, XICP_CPPR, xicp_prio(curcpu()->ci_cpl));
}

void
xicp_intr_send_ipi(void *cookie)
{
	panic("%s", __func__);
}

void *
xicp_intr_establish(uint32_t girq, int type, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *name)
{
	struct intrhand *ih;
	int64_t error;
	uint16_t server;

	if (ci == NULL)
		ci = cpu_info_primary;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_flags = level & IPL_FLAGMASK;
	ih->ih_girq = girq;
	ih->ih_name = name;
	LIST_INSERT_HEAD(XICP_GIRQHASH(girq), ih, ih_hash);

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_girq);

	server = ci->ci_pir << 2;
	error = opal_set_xive(girq, server, xicp_prio(level & IPL_IRQMASK));
	if (error != OPAL_SUCCESS) {
		if (name)
			evcount_detach(&ih->ih_count);
		LIST_REMOVE(ih, ih_hash);
		free(ih, M_DEVBUF, sizeof(*ih));
		return NULL;
	}

	return ih;
}

void
xicp_setipl(int new)
{
	struct xicp_softc *sc = xicp_sc[cpu_number()];
	struct cpu_info *ci = curcpu();
	uint8_t oldprio = xicp_prio(ci->ci_cpl);
	uint8_t newprio = xicp_prio(new);
	u_long msr;

	msr = intr_disable();
	ci->ci_cpl = new;
	if (newprio != oldprio)
		xicp_write_1(sc, XICP_CPPR, newprio);
	intr_restore(msr);
}

void
xicp_exi(struct trapframe *frame)
{
	struct xicp_softc *sc = xicp_sc[cpu_number()];
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	uint32_t xirr, xisr;
	int handled, old;

	KASSERT(sc);

	old = ci->ci_cpl;

	while (1) {
		xirr = xicp_read_4(sc, XICP_XIRR);
		xisr = xirr & XICP_XIRR_XISR_MASK;

		if (xisr == 0)
			break;

		/* Lookup the interrupt handle in the has table. */
		LIST_FOREACH(ih, XICP_GIRQHASH(xisr), ih_hash) {
			if (ih->ih_girq == xisr)
				break;
		}

		if (ih != NULL) {
#ifdef MULTIPROCESSOR
			int need_lock;

			if (ih->ih_flags & IPL_MPSAFE)
				need_lock = 0;
			else
				need_lock = (ih->ih_ipl < IPL_SCHED);

			if (need_lock)
				KERNEL_LOCK();
#endif
			ci->ci_cpl = ih->ih_ipl;
			xicp_write_1(sc, XICP_CPPR, xicp_prio(ih->ih_ipl));

			intr_enable();
			handled = ih->ih_func(ih->ih_arg);
			intr_disable();
			if (handled)
				ih->ih_count.ec_count++;
#ifdef MULTIPROCESSOR
			if (need_lock)
				KERNEL_UNLOCK();
#endif
		}

		/* Signal EOI. */
		xicp_write_4(sc, XICP_XIRR, xirr);
		ci->ci_cpl = old;
	}
}
