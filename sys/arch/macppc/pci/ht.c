/*	$OpenBSD: ht.c,v 1.19 2022/03/13 12:33:01 mpi Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis
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

#include <machine/autoconf.h>
#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>

int	 ht_match(struct device *, void *, void *);
void	 ht_attach(struct device *, struct device *, void *);

pcireg_t ht_conf_read(void *, pcitag_t, int);
void	 ht_conf_write(void *, pcitag_t, int, pcireg_t);

int	 ht_print(void *, const char *);

struct ht_softc {
	struct device	sc_dev;
	int		sc_maxdevs;
	struct ppc_bus_space sc_mem_bus_space;
	struct ppc_bus_space sc_io_bus_space;
	struct ppc_pci_chipset sc_pc;
	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_config0_memh;
	bus_space_handle_t sc_config1_memh;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_config0_ioh;
};

const struct cfattach ht_ca = {
	sizeof(struct ht_softc), ht_match, ht_attach
};

struct cfdriver ht_cd = {
	NULL, "ht", DV_DULL,
};

int
ht_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "ht") == 0)
		return (1);
	return (0);
}

void
ht_attach(struct device *parent, struct device *self, void *aux)
{
	struct ht_softc *sc = (struct ht_softc *)self;
	struct confargs *ca = aux;
	struct pcibus_attach_args pba;
	u_int32_t regs[6];
	char compat[32];
	int node, len;

	if (ca->ca_node == 0) {
		printf(": invalid node on ht config\n");
		return;
	}

	len = OF_getprop(ca->ca_node, "reg", regs, sizeof(regs));
	if (len < 0 || len < sizeof(regs)) {
		printf(": regs lookup failed, node %x\n", ca->ca_node);
		return;
	}

	sc->sc_mem_bus_space.bus_base = 0x80000000;
	sc->sc_mem_bus_space.bus_size = 0;
	sc->sc_mem_bus_space.bus_io = 0;
	sc->sc_memt = &sc->sc_mem_bus_space;

	sc->sc_io_bus_space.bus_base = 0x80000000;
	sc->sc_io_bus_space.bus_size = 0;
	sc->sc_io_bus_space.bus_io = 1;
	sc->sc_iot = &sc->sc_io_bus_space;

	sc->sc_maxdevs = 1;
	for (node = OF_child(ca->ca_node); node; node = OF_peer(node))
		sc->sc_maxdevs++;

	if (bus_space_map(sc->sc_memt, regs[1],
	    (1 << 11)*sc->sc_maxdevs, 0, &sc->sc_config0_memh)) {
		printf(": can't map PCI config0 memory\n");
		return;
	}

	if (bus_space_map(sc->sc_memt, regs[1] + 0x01000000,
	    regs[2] - 0x01000000, 0, &sc->sc_config1_memh)) {
		printf(": can't map PCI config1 memory\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, regs[4], 0x1000, 0,
	    &sc->sc_config0_ioh)) {
		printf(": can't map PCI config0 io\n");
		return;
	}

	len = OF_getprop(ca->ca_node, "compatible", compat, sizeof(compat));
	if (len <= 0)
		printf(": unknown");
	else
		printf(": %s", compat);

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_node = ca->ca_node;
	sc->sc_pc.pc_conf_read = ht_conf_read;
	sc->sc_pc.pc_conf_write = ht_conf_write;

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = sc->sc_iot;
	pba.pba_memt = sc->sc_memt;
	pba.pba_dmat = &pci_bus_dma_tag;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;

	printf(", %d devices\n", sc->sc_maxdevs);

	config_found(self, &pba, ht_print);
}

pcireg_t
ht_conf_read(void *cpv, pcitag_t tag, int offset)
{
	struct ht_softc *sc = cpv;
	int bus, dev, fcn;
	pcireg_t reg;
	uint32_t val;

	val = PCITAG_OFFSET(tag);
#ifdef DEBUG
	printf("ht_conf_read: tag=%x, offset=%x\n", val, offset);
#endif
	pci_decompose_tag(NULL, tag, &bus, &dev, &fcn);
	if (bus == 0 && dev == 0) {
		val |= (offset << 2);
		reg = bus_space_read_4(sc->sc_iot, sc->sc_config0_ioh, val);
		reg = letoh32(reg);
	} else if (bus == 0) {
		/* XXX Why can we only access function 0? */
		if (fcn > 0)
			return ~0;
		val |= offset;
		reg = bus_space_read_4(sc->sc_memt, sc->sc_config0_memh, val);
	} else {
		val |= offset;
		reg = bus_space_read_4(sc->sc_memt, sc->sc_config1_memh, val);
	}
#ifdef DEBUG
	printf("ht_conf_read: reg=%x\n", reg);
#endif
	return reg;
}

void
ht_conf_write(void *cpv, pcitag_t tag, int offset, pcireg_t data)
{
	struct ht_softc *sc = cpv;
	int bus, dev, fcn;
	uint32_t val;

	val = PCITAG_OFFSET(tag);
#ifdef DEBUG
	printf("ht_conf_write: tag=%x, offset=%x, data = %x\n",
	       val, offset, data);
#endif
	pci_decompose_tag(NULL, tag, &bus, &dev, &fcn);
	if (bus == 0 && dev == 0) {
		val |= (offset << 2);
		data = htole32(data);
		bus_space_write_4(sc->sc_iot, sc->sc_config0_ioh, val, data);
		bus_space_read_4(sc->sc_iot, sc->sc_config0_ioh, val);
	} else if (bus == 0) {
		/* XXX Why can we only access function 0? */
		if (fcn > 0)
			return;
		val |= offset;
		bus_space_write_4(sc->sc_memt, sc->sc_config0_memh, val, data);
		bus_space_read_4(sc->sc_memt, sc->sc_config0_memh, val);
	} else {
		val |= offset;
		bus_space_write_4(sc->sc_memt, sc->sc_config1_memh, val, data);
		bus_space_read_4(sc->sc_memt, sc->sc_config1_memh, val);
	}
}

int
ht_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}
