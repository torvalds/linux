/*	$OpenBSD: octcib.c,v 1.5 2019/09/01 12:16:01 visa Exp $	*/

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
 * Driver for Cavium Interrupt Bus (CIB) widget.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/fdt.h>

#define CIB_HIGHIPL		IPL_BIO
#define CIB_MAXBITS		64
#define CIB_IRQNUM(sc, bit)	(256 + (sc)->sc_dev.dv_unit * CIB_MAXBITS + \
				    (bit))

#define CIB_EN_RD(sc) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_en_ioh, 0)
#define CIB_EN_WR(sc, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_en_ioh, 0, (val))
#define CIB_RAW_RD(sc) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_raw_ioh, 0)
#define CIB_RAW_WR(sc, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_raw_ioh, 0, (val))

struct octcib_softc;

struct octcib_intrhand {
	LIST_ENTRY(octcib_intrhand) cih_list;
	int			(*cih_func)(void *);
	void			*cih_arg;
	uint32_t		 cih_bit;
	uint32_t		 cih_flags;
#define CIH_MPSAFE			0x01
#define CIH_EDGE			0x02	/* edge-triggered */
	struct evcount		 cih_count;
	unsigned int		 cih_irq;	/* for cih_count */
	struct octcib_softc	*cih_sc;
};

struct octcib_softc {
	struct device		 sc_dev;
	void			*sc_ih;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_en_ioh;
	bus_space_handle_t	 sc_raw_ioh;

	LIST_HEAD(, octcib_intrhand) sc_bits[CIB_MAXBITS];
	uint32_t		 sc_maxbits;

	struct intr_controller	 sc_ic;
};

int	 octcib_match(struct device *, void *, void *);
void	 octcib_attach(struct device *, struct device *, void *);

void	*octcib_establish(void *, int, int, int, int (*func)(void *),
	    void *, const char *);
void	 octcib_disestablish(void *);
void	 octcib_intr_barrier(void *);
int	 octcib_intr(void *);

const struct cfattach octcib_ca = {
	sizeof(struct octcib_softc), octcib_match, octcib_attach
};

struct cfdriver octcib_cd = {
	NULL, "octcib", DV_DULL
};

int
octcib_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-7130-cib");
}

void
octcib_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct octcib_softc *sc = (struct octcib_softc *)self;
	unsigned int i;

	if (faa->fa_nreg != 2) {
		printf(": expected 2 IO spaces, got %d\n", faa->fa_nreg);
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_maxbits = OF_getpropint(faa->fa_node, "cavium,max-bits", 0);

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_raw_ioh)) {
		printf(": could not map RAW\n");
		goto error;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr, faa->fa_reg[1].size,
	    0, &sc->sc_en_ioh)) {
		printf(": could not map EN\n");
		goto error;
	}

	/* Disable all interrupts. */
	CIB_EN_WR(sc, 0);
	/* Acknowledge any pending interrupts. */
	CIB_RAW_WR(sc, ~0ul);

	sc->sc_ih = octeon_intr_establish_fdt(faa->fa_node,
	    CIB_HIGHIPL | IPL_MPSAFE, octcib_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": failed to register interrupt\n");
		goto error;
	}

	printf(": max-bits %u\n", sc->sc_maxbits);

	for (i = 0; i < CIB_MAXBITS; i++)
		LIST_INIT(&sc->sc_bits[i]);

	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_establish_fdt_idx = octcib_establish;
	sc->sc_ic.ic_disestablish = octcib_disestablish;
	sc->sc_ic.ic_intr_barrier = octcib_intr_barrier;
	octeon_intr_register(&sc->sc_ic);
	return;

error:
	if (sc->sc_en_ioh != 0)
		bus_space_unmap(sc->sc_iot, sc->sc_en_ioh,
		    faa->fa_reg[1].size);
	if (sc->sc_raw_ioh != 0)
		bus_space_unmap(sc->sc_iot, sc->sc_raw_ioh,
		    faa->fa_reg[0].size);
}

void *
octcib_establish(void *cookie, int node, int idx, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct octcib_intrhand *cih;
	struct octcib_softc *sc = cookie;
	uint64_t en;
	uint32_t *cells;
	uint32_t bit, type;
	int flags, len, s;

	flags = (level & IPL_MPSAFE) ? CIH_MPSAFE : 0;
	level &= ~IPL_MPSAFE;

	if (level > CIB_HIGHIPL)
		return NULL;

	len = OF_getproplen(node, "interrupts");
	if (len / (sizeof(uint32_t) * 2) <= idx ||
	    len % (sizeof(uint32_t) * 2) != 0)
		return NULL;

	cells = malloc(len, M_TEMP, M_NOWAIT);
	if (cells == NULL)
		return NULL;
	OF_getpropintarray(node, "interrupts", cells, len);
	bit = cells[idx * 2];
	type = cells[idx * 2 + 1];
	free(cells, M_TEMP, len);

	if (bit >= sc->sc_maxbits)
		return NULL;
	if (type != 4)
		flags |= CIH_EDGE;

	cih = malloc(sizeof(*cih), M_DEVBUF, M_NOWAIT);
	if (cih == NULL)
		return NULL;
	cih->cih_func = func;
	cih->cih_arg = arg;
	cih->cih_bit = bit;
	cih->cih_flags = flags;
	cih->cih_irq = CIB_IRQNUM(sc, bit);
	cih->cih_sc = sc;

	s = splhigh();

	evcount_attach(&cih->cih_count, name, &cih->cih_irq);
	LIST_INSERT_HEAD(&sc->sc_bits[bit], cih, cih_list);

	/* Enable the interrupt. */
	en = CIB_EN_RD(sc);
	en |= 1ul << bit;
	CIB_EN_WR(sc, en);

	splx(s);

	return cih;
}

void
octcib_disestablish(void *cookie)
{
	struct octcib_intrhand *cih = cookie;
	struct octcib_softc *sc = cih->cih_sc;
	uint64_t val;
	uint32_t bit = cih->cih_bit;
	int s;
#ifdef DIAGNOSTIC
	struct octcib_intrhand *tmp;
	int found;
#endif

	s = splhigh();

#ifdef DIAGNOSTIC
	found = 0;
	LIST_FOREACH(tmp, &sc->sc_bits[bit], cih_list) {
		if (tmp == cih) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		panic("%s: intrhand %p not registered", __func__, cih);
#endif

	LIST_REMOVE(cih, cih_list);
	evcount_detach(&cih->cih_count);

	if (LIST_EMPTY(&sc->sc_bits[bit])) {
		/* Disable the interrupt. */
		val = CIB_EN_RD(sc);
		val &= ~(1ul << bit);
		CIB_EN_WR(sc, val);
	}

	splx(s);

	free(cih, M_DEVBUF, sizeof(*cih));
}

void
octcib_intr_barrier(void *cookie)
{
	struct octcib_intrhand *cih = cookie;
	struct octcib_softc *sc = cih->cih_sc;

	intr_barrier(sc->sc_ih);
}

int
octcib_intr(void *arg)
{
	struct octcib_intrhand *cih;
	struct octcib_softc *sc = arg;
	uint64_t en, isr, mask;
	uint32_t bit;
	int handled = 0;
#ifdef MULTIPROCESSOR
	int need_lock;
#endif

	en = CIB_EN_RD(sc);
	isr = CIB_RAW_RD(sc);
	isr &= en;

	for (bit = 0; isr != 0 && bit < sc->sc_maxbits; bit++) {
		mask = 1ul << bit;

		if ((isr & mask) == 0)
			continue;
		isr &= ~mask;

		handled = 0;
		LIST_FOREACH(cih, &sc->sc_bits[bit], cih_list) {
			/* Acknowledge the interrupt. */
			if (ISSET(cih->cih_flags, CIH_EDGE))
				CIB_RAW_WR(sc, mask);

#ifdef MULTIPROCESSOR
			if (!ISSET(cih->cih_flags, CIH_MPSAFE))
				need_lock = 1;
			else
				need_lock = 0;
			if (need_lock)
				__mp_lock(&kernel_lock);
#endif
			if (cih->cih_func(cih->cih_arg)) {
				handled = 1;
				cih->cih_count.ec_count++;
			}
#ifdef MULTIPROCESSOR
			if (need_lock)
				__mp_unlock(&kernel_lock);
#endif
		}

		if (handled == 0)
			printf("%s: spurious interrupt %u (bit %u) "
			    "on cpu %lu\n",
			    sc->sc_dev.dv_xname, CIB_IRQNUM(sc, bit), bit,
			    cpu_number());
	}

	return 1;
}
