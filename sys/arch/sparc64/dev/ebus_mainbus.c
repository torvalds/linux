/*	$OpenBSD: ebus_mainbus.c,v 1.14 2025/06/28 11:34:21 miod Exp $	*/

/*
 * Copyright (c) 2007 Mark Kettenis
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

#ifdef DEBUG
#define	EDB_PROM	0x01
#define EDB_CHILD	0x02
#define	EDB_INTRMAP	0x04
#define EDB_BUSMAP	0x08
#define EDB_BUSDMA	0x10
#define EDB_INTR	0x20
extern int ebus_debug;
#define DPRINTF(l, s)   do { if (ebus_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>

#include <dev/pci/pcivar.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/pyrovar.h>

extern struct cfdriver pyro_cd;

int	ebus_mainbus_match(struct device *, void *, void *);
void	ebus_mainbus_attach(struct device *, struct device *, void *);

const struct cfattach ebus_mainbus_ca = {
	sizeof(struct ebus_softc), ebus_mainbus_match, ebus_mainbus_attach
};


int ebus_mainbus_bus_map(bus_space_tag_t, bus_space_tag_t,
    bus_addr_t, bus_size_t, int, bus_space_handle_t *);
void *ebus_mainbus_intr_establish(bus_space_tag_t, bus_space_tag_t,
    int, int, int, int (*)(void *), void *, const char *);
bus_space_tag_t ebus_alloc_bus_tag(struct ebus_softc *, bus_space_tag_t);
void ebus_mainbus_intr_ack(struct intrhand *);

int
ebus_mainbus_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "ebus") == 0)
		return (1);
	return (0);
}

void
ebus_mainbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct ebus_softc *sc = (struct ebus_softc *)self;
	struct mainbus_attach_args *ma = aux;
	struct ebus_attach_args eba;
	struct ebus_interrupt_map_mask *immp;
	int node, nmapmask, error;
	struct pyro_softc *psc;
	int i, j;

	printf("\n");

	sc->sc_memtag = ebus_alloc_bus_tag(sc, ma->ma_bustag);
	sc->sc_iotag = ebus_alloc_bus_tag(sc, ma->ma_bustag);
	sc->sc_dmatag = ebus_alloc_dma_tag(sc, ma->ma_dmatag);

	sc->sc_node = node = ma->ma_node;

	/*
	 * fill in our softc with information from the prom
	 */
	sc->sc_intmap = NULL;
	sc->sc_range = NULL;
	error = getprop(node, "interrupt-map",
			sizeof(struct ebus_interrupt_map),
			&sc->sc_nintmap, (void **)&sc->sc_intmap);
	switch (error) {
	case 0:
		immp = &sc->sc_intmapmask;
		error = getprop(node, "interrupt-map-mask",
			    sizeof(struct ebus_interrupt_map_mask), &nmapmask,
			    (void **)&immp);
		if (error)
			panic("could not get ebus interrupt-map-mask");
		if (nmapmask != 1)
			panic("ebus interrupt-map-mask is broken");
		break;
	case ENOENT:
		break;
	default:
		panic("ebus interrupt-map: error %d", error);
		break;
	}

	/*
	 * Ebus interrupts may be connected to any of the PCI Express
	 * leafs.  Here we add the appropriate IGN to the interrupt
	 * mappings such that we can use it to distinguish between
	 * interrupts connected to PCIE-A and PCIE-B.
	 */
	for (i = 0; i < sc->sc_nintmap; i++) {
		for (j = 0; j < pyro_cd.cd_ndevs; j++) {
			psc = pyro_cd.cd_devs[j];
			if (psc && psc->sc_node == sc->sc_intmap[i].cnode) {
				sc->sc_intmap[i].cintr |= psc->sc_ign;
				break;
			}
		}
	}

	error = getprop(node, "ranges", sizeof(struct ebus_mainbus_ranges),
	    &sc->sc_nrange, (void **)&sc->sc_range);
	if (error)
		panic("ebus ranges: error %d", error);

	/*
	 * now attach all our children
	 */
	DPRINTF(EDB_CHILD, ("ebus node %08x, searching children...\n", node));
	for (node = firstchild(node); node; node = nextsibling(node)) {
		if (ebus_setup_attach_args(sc, node, &eba) != 0) {
			DPRINTF(EDB_CHILD,
			    ("ebus_mainbus_attach: %s: incomplete\n",
			    getpropstring(node, "name")));
			continue;
		} else {
			DPRINTF(EDB_CHILD, ("- found child `%s', attaching\n",
			    eba.ea_name));
			(void)config_found(self, &eba, ebus_print);
		}
		ebus_destroy_attach_args(&eba);
	}
}

bus_space_tag_t
ebus_alloc_bus_tag(struct ebus_softc *sc, bus_space_tag_t parent)
{
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("could not allocate ebus bus tag");

	strlcpy(bt->name, sc->sc_dev.dv_xname, sizeof(bt->name));
	bt->cookie = sc;
	bt->parent = parent;
	bt->asi = parent->asi;
	bt->sasi = parent->sasi;
	bt->sparc_bus_map = ebus_mainbus_bus_map;
	bt->sparc_intr_establish = ebus_mainbus_intr_establish;

	return (bt);
}

int
ebus_mainbus_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct ebus_softc *sc = t->cookie;
	struct ebus_mainbus_ranges *range;
	bus_addr_t hi, lo;
	int i;

	DPRINTF(EDB_BUSMAP,
	    ("\n_ebus_mainbus_bus_map: off %016llx sz %x flags %d",
	    (unsigned long long)offset, (int)size, (int)flags));

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\n_ebus_mainbus_bus_map: invalid parent");
		return (EINVAL);
	}

	t = t->parent;

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	hi = offset >> 32UL;
	lo = offset & 0xffffffff;
	range = (struct ebus_mainbus_ranges *)sc->sc_range;

	DPRINTF(EDB_BUSMAP, (" (hi %08x lo %08x)", (u_int)hi, (u_int)lo));
	for (i = 0; i < sc->sc_nrange; i++) {
		bus_addr_t addr;

		if (hi != range[i].child_hi)
			continue;
		if (lo < range[i].child_lo ||
		    (lo + size) > (range[i].child_lo + range[i].size))
			continue;

		addr = ((bus_addr_t)range[i].phys_hi << 32UL) |
				    range[i].phys_lo;
		addr += lo;
		DPRINTF(EDB_BUSMAP,
		    ("\n_ebus_mainbus_bus_map: paddr offset %llx addr %llx\n", 
		    (unsigned long long)offset, (unsigned long long)addr));
                return ((*t->sparc_bus_map)(t, t0, addr, size, flags, hp));
	}
	DPRINTF(EDB_BUSMAP, (": FAILED\n"));
	return (EINVAL);
}

void *
ebus_mainbus_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct ebus_softc *sc = t->cookie;
	struct intrhand *ih = NULL;
	volatile u_int64_t *intrmapptr = NULL, *intrclrptr = NULL;
	int ino;

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		struct upa_reg reg;
		u_int64_t devhandle, devino = INTINO(ihandle);
		u_int64_t sysino;
		int node = -1;
		int i, err;

		for (i = 0; i < sc->sc_nintmap; i++) {
			if (sc->sc_intmap[i].cintr == ihandle) {
				node = sc->sc_intmap[i].cnode;
				break;
			}
		}
		if (node == -1)
			return (NULL);

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			return (NULL);
		devhandle = (reg.ur_paddr >> 32) & 0x0fffffff;

		err = hv_intr_devino_to_sysino(devhandle, devino, &sysino);
		if (err != H_EOK)
			return (NULL);

		KASSERT(sysino == INTVEC(sysino));
		ih = bus_intr_allocate(t0, handler, arg, sysino, level,
		    NULL, NULL, what);
		if (ih == NULL)
			return (NULL);

		intr_establish(ih);
		ih->ih_ack = ebus_mainbus_intr_ack;

		err = hv_intr_settarget(sysino, ih->ih_cpu->ci_upaid);
		if (err != H_EOK)
			return (NULL);

		/* Clear pending interrupts. */
		err = hv_intr_setstate(sysino, INTR_IDLE);
		if (err != H_EOK)
			return (NULL);

		err = hv_intr_setenabled(sysino, INTR_ENABLED);
		if (err != H_EOK)
			return (NULL);

		return (ih);
	}
#endif

	ino = INTINO(ihandle);

	if ((flags & BUS_INTR_ESTABLISH_SOFTINTR) == 0) {
		struct pyro_softc *psc = NULL;
		u_int64_t *imap, *iclr;
		int i;

		for (i = 0; i < pyro_cd.cd_ndevs; i++) {
			psc = pyro_cd.cd_devs[i];
			if (psc && psc->sc_ign == INTIGN(ihandle)) {
				break;
			}
		}
		if (psc == NULL)
			return (NULL);

		imap = bus_space_vaddr(psc->sc_bust, psc->sc_csrh) + 0x1000;
		iclr = bus_space_vaddr(psc->sc_bust, psc->sc_csrh) + 0x1400;
		intrmapptr = &imap[ino];
		intrclrptr = &iclr[ino];
		ino |= INTVEC(ihandle);
	}

	ih = bus_intr_allocate(t0, handler, arg, ino, level, intrmapptr,
	    intrclrptr, what);
	if (ih == NULL)
		return (NULL);

	intr_establish(ih);

	if (intrmapptr != NULL) {
		u_int64_t intrmap;

		intrmap = *intrmapptr;
		intrmap |= (1LL << 6);
		intrmap |= INTMAP_V;
		*intrmapptr = intrmap;
		intrmap = *intrmapptr;
		ih->ih_number |= intrmap & INTMAP_INR;
	}

	return (ih);
}

#ifdef SUN4V

void
ebus_mainbus_intr_ack(struct intrhand *ih)
{
	hv_intr_setstate(ih->ih_number, INTR_IDLE);
}

#endif
