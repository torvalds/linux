/*	$OpenBSD: vbus.c,v 1.13 2024/03/29 21:29:33 miod Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>

#include <sparc64/dev/vbusvar.h>

#include <dev/clock_subr.h>
extern todr_chip_handle_t todr_handle;

struct vbus_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_bustag;
	bus_dma_tag_t		sc_dmatag;

	uint64_t		sc_devhandle;
};

int	vbus_cmp_cells(int *, int *, int *, int);
int	vbus_match(struct device *, void *, void *);
void	vbus_attach(struct device *, struct device *, void *);
int	vbus_print(void *, const char *);

const struct cfattach vbus_ca = {
	sizeof(struct vbus_softc), vbus_match, vbus_attach
};

struct cfdriver vbus_cd = {
	NULL, "vbus", DV_DULL
};

void	*vbus_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int, int,
    int (*)(void *), void *, const char *);
void	vbus_intr_ack(struct intrhand *);
bus_space_tag_t vbus_alloc_bus_tag(struct vbus_softc *, bus_space_tag_t);

int
vbus_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "virtual-devices") == 0)
		return (1);

	return (0);
}

void
vbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct vbus_softc *sc = (struct vbus_softc *)self;
	struct mainbus_attach_args *ma = aux;
	int node;

	sc->sc_bustag = vbus_alloc_bus_tag(sc, ma->ma_bustag);
	sc->sc_dmatag = ma->ma_dmatag;
	sc->sc_devhandle = (ma->ma_reg[0].ur_paddr >> 32) & 0x0fffffff;
	printf("\n");

	for (node = OF_child(ma->ma_node); node; node = OF_peer(node)) {
		struct vbus_attach_args va;
		char buf[32];

		bzero(&va, sizeof(va));
		va.va_node = node;
		if (OF_getprop(node, "name", buf, sizeof(buf)) <= 0)
			continue;
		va.va_name = buf;
		va.va_bustag = sc->sc_bustag;
		va.va_dmatag = sc->sc_dmatag;
		getprop(node, "reg", sizeof(*va.va_reg),
		    &va.va_nreg, (void **)&va.va_reg);
		getprop(node, "interrupts", sizeof(*va.va_intr),
		    &va.va_nintr, (void **)&va.va_intr);
		config_found(self, &va, vbus_print);
	}

	if (todr_handle == NULL) {
		struct vbus_attach_args va;

		bzero(&va, sizeof(va));
		va.va_name = "rtc";
		config_found(self, &va, vbus_print);
	}
}

int
vbus_print(void *aux, const char *name)
{
	struct vbus_attach_args *va = aux;

	if (name)
		printf("\"%s\" at %s", va->va_name, name);
	return (UNCONF);
}

/*
 * Compare a sequence of cells with a mask, return 1 if they match and
 * 0 if they don't.
 */
int
vbus_cmp_cells(int *cell1, int *cell2, int *mask, int ncells)
{
	int i;

	for (i = 0; i < ncells; i++) {
		if (((cell1[i] ^ cell2[i]) & mask[i]) != 0)
			return (0);
	}
	return (1);
}

int
vbus_intr_map(int node, int ino, uint64_t *sysino)
{
	int *imap = NULL, nimap;
	int *reg = NULL, nreg;
	int *imap_mask;
	int parent;
	int address_cells, interrupt_cells;
	uint64_t devhandle;
	uint64_t devino;
	int len;
	int err;

	parent = OF_parent(node);

	address_cells = getpropint(parent, "#address-cells", 2);
	interrupt_cells = getpropint(parent, "#interrupt-cells", 1);
	KASSERT(interrupt_cells == 1);

	len = OF_getproplen(parent, "interrupt-map-mask");
	if (len < (address_cells + interrupt_cells) * sizeof(int))
		return (-1);
	imap_mask = malloc(len, M_DEVBUF, M_NOWAIT);
	if (imap_mask == NULL)
		return (-1);
	if (OF_getprop(parent, "interrupt-map-mask", imap_mask, len) != len)
		return (-1);

	getprop(parent, "interrupt-map", sizeof(int), &nimap, (void **)&imap);
	getprop(node, "reg", sizeof(*reg), &nreg, (void **)&reg);
	if (nreg < address_cells)
		return (-1);

	while (nimap >= address_cells + interrupt_cells + 2) {
		if (vbus_cmp_cells(imap, reg, imap_mask, address_cells) &&
		    vbus_cmp_cells(&imap[address_cells], &ino,
		    &imap_mask[address_cells], interrupt_cells)) {
			node = imap[address_cells + interrupt_cells];
			devino = imap[address_cells + interrupt_cells + 1];

			free(reg, M_DEVBUF, 0);
			reg = NULL;

			getprop(node, "reg", sizeof(*reg), &nreg, (void **)&reg);
			devhandle = reg[0] & 0x0fffffff;

			err = sun4v_intr_devino_to_sysino(devhandle, devino, sysino);
			if (err != H_EOK)
				return (-1);

			return (0);
		}
		imap += address_cells + interrupt_cells + 2;
		nimap -= address_cells + interrupt_cells + 2;
	}

	return (-1);
}

int
vbus_intr_setstate(bus_space_tag_t t, uint64_t sysino, uint64_t state)
{
	struct vbus_softc *sc = t->cookie;
	uint64_t devhandle = sc->sc_devhandle;
	int err;

	err = sun4v_intr_setstate(devhandle, sysino, state);
	if (err != H_EOK)
		return (-1);

	return (0);
}

int
vbus_intr_setenabled(bus_space_tag_t t, uint64_t sysino, uint64_t enabled)
{
	struct vbus_softc *sc = t->cookie;
	uint64_t devhandle = sc->sc_devhandle;
	int err;

	err = sun4v_intr_setenabled(devhandle, sysino, enabled);
	if (err != H_EOK)
		return (-1);

	return (0);
}

void *
vbus_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct vbus_softc *sc = t->cookie;
	uint64_t devhandle = sc->sc_devhandle;
	uint64_t sysino = INTVEC(ihandle);
	struct intrhand *ih;
	int err;

	ih = bus_intr_allocate(t0, handler, arg, ihandle, level,
	    NULL, NULL, what);
	if (ih == NULL)
		return (NULL);

	if (flags & BUS_INTR_ESTABLISH_MPSAFE)
		ih->ih_mpsafe = 1;

	err = sun4v_intr_setenabled(devhandle, sysino, INTR_DISABLED);
	if (err != H_EOK)
		return (NULL);

	err = sun4v_intr_setcookie(devhandle, sysino, (vaddr_t)ih);
	if (err != H_EOK)
		return (NULL);

	intr_establish(ih);
	ih->ih_ack = vbus_intr_ack;

	err = sun4v_intr_settarget(devhandle, sysino, ih->ih_cpu->ci_upaid);
	if (err != H_EOK)
		return (NULL);

	/* Clear pending interrupts. */
	err = sun4v_intr_setstate(devhandle, sysino, INTR_IDLE);
	if (err != H_EOK)
		return (NULL);

	return (ih);
}

void
vbus_intr_ack(struct intrhand *ih)
{
	/* Drivers explicitly ack interrupts. */
}

bus_space_tag_t
vbus_alloc_bus_tag(struct vbus_softc *sc, bus_space_tag_t parent)
{
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("could not allocate vbus bus tag");

	strlcpy(bt->name, sc->sc_dv.dv_xname, sizeof(bt->name));
	bt->cookie = sc;
	bt->parent = parent;
	bt->asi = parent->asi;
	bt->sasi = parent->sasi;
	bt->sparc_bus_map = parent->sparc_bus_map;
	bt->sparc_intr_establish = vbus_intr_establish;

	return (bt);
}
