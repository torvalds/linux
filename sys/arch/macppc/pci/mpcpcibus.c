/*	$OpenBSD: mpcpcibus.c,v 1.49 2022/03/13 12:33:01 mpi Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <machine/autoconf.h>
#include <machine/pcb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>

int	mpcpcibrmatch(struct device *, void *, void *);
void	mpcpcibrattach(struct device *, struct device *, void *);

pcireg_t mpc_conf_read(void *, pcitag_t, int);
void	mpc_conf_write(void *, pcitag_t, int, pcireg_t);

u_int32_t mpc_gen_config_reg(void *cpv, pcitag_t tag, int offset);

struct pcibr_config {
	bus_space_tag_t		lc_memt;
	bus_space_tag_t		lc_iot;
	bus_space_handle_t	ioh_cf8;
	bus_space_handle_t	ioh_cfc;
	struct ppc_pci_chipset	lc_pc;
	int			config_type;
};

struct pcibr_softc {
	struct device		sc_dev;
	struct ppc_bus_space	sc_membus_space;
	struct ppc_bus_space	sc_iobus_space;
	struct pcibr_config	pcibr_config;
	struct extent 		*sc_ioex;
	struct extent		*sc_memex;
	char			sc_ioex_name[32];
	char			sc_memex_name[32];
};

const struct cfattach mpcpcibr_ca = {
        sizeof(struct pcibr_softc), mpcpcibrmatch, mpcpcibrattach,
};

struct cfdriver mpcpcibr_cd = {
	NULL, "mpcpcibr", DV_DULL,
};

static int      mpcpcibrprint(void *, const char *pnp);

void	mpcpcibus_find_ranges_32(struct pcibr_softc *, u_int32_t *, int);
void	mpcpcibus_find_ranges_64(struct pcibr_softc *, u_int32_t *, int);

/*
 * config types
 * bit meanings
 * 0 - standard cf8/cfc type configurations,
 *     sometimes the base addresses for these are different
 * 1 - Config Method #2 configuration - uni-north
 *
 * 2 - 64 bit config bus, data for accesses &4 is at daddr+4;
 */
struct config_type{
	char * compat;
	u_int32_t addr;	/* offset */
	u_int32_t data;	/* offset */
	int config_type;
};
struct config_type config_offsets[] = {
	{"grackle",		0x00c00cf8, 0x00e00cfc, 0 },
	{"bandit",		0x00800000, 0x00c00000, 1 },
	{"uni-north",		0x00800000, 0x00c00000, 3 },
	{"u3-agp",		0x00800000, 0x00c00000, 3 },
	{"u3-ht",		0x00000cf8, 0x00000cfc, 3 },
	{"u4-pcie",		0x00800000, 0x00c00000, 7 },
	{"legacy",		0x00000cf8, 0x00000cfc, 0 },
	{"IBM,27-82660",	0x00000cf8, 0x00000cfc, 0 },
	{NULL,			0x00000000, 0x00000000, 0 },
};

int
mpcpcibrmatch(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int found = 0;

	if (strcmp(ca->ca_name, mpcpcibr_cd.cd_name) != 0)
		return (found);

	found = 1;

	return found;
}

struct ranges_32 {
	u_int32_t cspace;
	u_int32_t child_hi;
	u_int32_t child_lo;
	u_int32_t phys;
	u_int32_t size_hi;
	u_int32_t size_lo;
};

void
mpcpcibus_find_ranges_32(struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize)
{
	int i, found;
	unsigned int base = 0;
	unsigned int size = 0;
	struct ranges_32 *prange = (void *)range_store;
	int rangelen;

	rangelen = rangesize / sizeof(struct ranges_32);

	/* mac configs */
	sc->sc_membus_space.bus_base = 0;
	sc->sc_membus_space.bus_io = 0;
	sc->sc_iobus_space.bus_base = 0;
	sc->sc_iobus_space.bus_io = 1;

	/* find io(config) base, flag == 0x01000000 */
	found = 0;
	for (i = 0; i < rangelen; i++) {
		if (prange[i].cspace == 0x01000000) {
			/* find last? */
			found = i;

			if (sc->sc_ioex)
				extent_free(sc->sc_ioex, prange[i].child_lo,
				    prange[i].size_lo, EX_NOWAIT);
		}
	}
	/* found the io space ranges */
	if (prange[found].cspace == 0x01000000) {
		sc->sc_iobus_space.bus_base = prange[found].phys;
		sc->sc_iobus_space.bus_size = prange[found].size_lo;
	}

	/* the mem space ranges
	 * apple openfirmware always puts full
	 * addresses in config information,
	 * it is not necessary to have correct bus
	 * base address, but since 0 is reserved
	 * and all IO and device memory will be in
	 * upper 2G of address space, set to
	 * 0x80000000
	 */
	for (i = 0; i < rangelen; i++) {
		if (prange[i].cspace == 0x02000000) {
#ifdef DEBUG_PCI
			printf("\nfound mem %x %x",
				prange[i].phys,
				prange[i].size_lo);
#endif
			if (base != 0) {
				if ((base + size) == prange[i].phys)
					size += prange[i].size_lo;
				else {
					base = prange[i].phys;
					size = prange[i].size_lo;
				}
			} else {
				base = prange[i].phys;
				size = prange[i].size_lo;
			}

			if (sc->sc_memex)
				extent_free(sc->sc_memex, prange[i].child_lo,
				    prange[i].size_lo, EX_NOWAIT);
		}
	}
	sc->sc_membus_space.bus_base = base;
	sc->sc_membus_space.bus_size = size;
}

struct ranges_64 {
	u_int32_t cspace;
	u_int32_t child_hi;
	u_int32_t child_lo;
	u_int32_t phys_hi;
	u_int32_t phys_lo;
	u_int32_t size_hi;
	u_int32_t size_lo;
};

void
mpcpcibus_find_ranges_64(struct pcibr_softc *sc, u_int32_t *range_store,
    int rangesize)
{
	int i, found;
	unsigned int base = 0;
	unsigned int size = 0;
	struct ranges_64 *prange = (void *)range_store;
	int rangelen;

	rangelen = rangesize / sizeof(struct ranges_64);

	/* mac configs */
	sc->sc_membus_space.bus_base = 0;
	sc->sc_membus_space.bus_io = 0;
	sc->sc_iobus_space.bus_base = 0;
	sc->sc_iobus_space.bus_io = 1;

	if (prange[0].cspace == 0xabb10113) { /* appl U3; */
		prange[0].cspace = 0x01000000;
		prange[0].child_lo = 0x00000000;
		prange[0].phys_lo = 0xf8070000;
		prange[0].size_lo = 0x00001000;
		prange[1].cspace = 0x02000000;
		prange[1].child_lo = 0xf2000000;
		prange[1].phys_lo = 0xf2000000;
		prange[1].size_lo = 0x02800000;
		rangelen = 2;
	}

	/* find io(config) base, flag == 0x01000000 */
	found = 0;
	for (i = 0; i < rangelen; i++) {
		if (prange[i].cspace == 0x01000000) {
			/* find last? */
			found = i;

			if (sc->sc_ioex)
				extent_free(sc->sc_ioex, prange[i].child_lo,
				    prange[i].size_lo, EX_NOWAIT);
		}
	}
	/* found the io space ranges */
	if (prange[found].cspace == 0x01000000) {
		sc->sc_iobus_space.bus_base = prange[found].phys_lo;
		sc->sc_iobus_space.bus_size = prange[found].size_lo;
	}

	/* the mem space ranges
	 * apple openfirmware always puts full
	 * addresses in config information,
	 * it is not necessary to have correct bus
	 * base address, but since 0 is reserved
	 * and all IO and device memory will be in
	 * upper 2G of address space, set to
	 * 0x80000000
	 */
	for (i = 0; i < rangelen; i++) {
		if (prange[i].cspace == 0x02000000) {
#ifdef DEBUG_PCI
			printf("\nfound mem %x %x",
				prange[i].phys_lo,
				prange[i].size_lo);
#endif
			if (base != 0) {
				if ((base + size) == prange[i].phys_lo) {
					size += prange[i].size_lo;
				} else {
					base = prange[i].phys_lo;
					size = prange[i].size_lo;
				}
			} else {
				base = prange[i].phys_lo;
				size = prange[i].size_lo;
			}

			if (sc->sc_memex)
				extent_free(sc->sc_memex, prange[i].child_lo,
				    prange[i].size_lo, EX_NOWAIT);
		}
	}
	sc->sc_membus_space.bus_base = base;
	sc->sc_membus_space.bus_size = size;
}

void
mpcpcibrattach(struct device *parent, struct device *self, void *aux)
{
	struct pcibr_softc *sc = (struct pcibr_softc *)self;
	struct confargs *ca = aux;
	struct pcibr_config *lcp;
	struct pcibus_attach_args pba;
	char compat[32];
	u_int32_t addr_offset;
	u_int32_t data_offset;
	int i;
	int len;
	int rangesize;
	u_int32_t range_store[32];
	int busrange[2];

	if (ca->ca_node == 0) {
		printf("invalid node on mpcpcibr config\n");
		return;
	}
	len = OF_getprop(ca->ca_node, "name", compat, sizeof(compat));
	compat[len] = '\0';
	if (len > 0)
		printf(" %s", compat);

	len = OF_getprop(ca->ca_node, "compatible", compat, sizeof(compat));
	if (len <= 0) {
		len = OF_getprop(ca->ca_node, "name", compat, sizeof(compat));
		if (len <= 0) {
			printf(" compatible and name not found\n");
			return;
		}
		compat[len] = 0;
		if (strcmp(compat, "bandit") != 0) {
			printf(" compatible not found and name %s found\n",
			    compat);
			return;
		}
	}
	compat[len] = 0;
	if ((rangesize = OF_getprop(ca->ca_node, "ranges",
	    range_store, sizeof (range_store))) <= 0) {
		if (strcmp(compat, "u3-ht") == 0) {
			range_store[0] = 0xabb10113; /* appl U3; */
		} else
			printf("range lookup failed, node %x\n", ca->ca_node);
	}

	lcp = &sc->pcibr_config;

	snprintf(sc->sc_ioex_name, sizeof(sc->sc_ioex_name),
	    "%s pciio", sc->sc_dev.dv_xname);
	sc->sc_ioex = extent_create(sc->sc_ioex_name, 0x00000000, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);
	snprintf(sc->sc_memex_name, sizeof(sc->sc_memex_name),
	    "%s pcimem", sc->sc_dev.dv_xname);
	sc->sc_memex = extent_create(sc->sc_memex_name, 0x00000000, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);

	if (ppc_proc_is_64b)
		mpcpcibus_find_ranges_64(sc, range_store, rangesize);
	else
		mpcpcibus_find_ranges_32(sc, range_store, rangesize);

	addr_offset = 0;
	for (i = 0; config_offsets[i].compat != NULL; i++) {
		struct config_type *co = &config_offsets[i];
		if (strcmp(co->compat, compat) == 0) {
			addr_offset = co->addr;
			data_offset = co->data;
			lcp->config_type = co->config_type;
			break;
		}
	}
	if (addr_offset == 0) {
		printf("unable to find match for"
		    " compatible %s\n", compat);
		return;
	}
#ifdef DEBUG_FIXUP
	printf(" mem base %x sz %x io base %x sz %x\n"
	    " config addr %x config data %x\n",
	    sc->sc_membus_space.bus_base,
	    sc->sc_membus_space.bus_size,
	    sc->sc_iobus_space.bus_base,
	    sc->sc_iobus_space.bus_size,
	    addr_offset, data_offset);
#endif

	if (bus_space_map(&(sc->sc_iobus_space), addr_offset,
		NBPG, 0, &lcp->ioh_cf8) != 0)
		panic("mpcpcibus: unable to map self");

	if (bus_space_map(&(sc->sc_iobus_space), data_offset,
		NBPG, 0, &lcp->ioh_cfc) != 0)
		panic("mpcpcibus: unable to map self");

	lcp->lc_pc.pc_conf_v = lcp;
	lcp->lc_pc.pc_node = ca->ca_node;
	lcp->lc_pc.pc_conf_read = mpc_conf_read;
	lcp->lc_pc.pc_conf_write = mpc_conf_write;
	lcp->lc_iot = &sc->sc_iobus_space;
	lcp->lc_memt = &sc->sc_membus_space;

	printf(": %s\n", compat);

	bzero(&pba, sizeof(pba));
	pba.pba_dmat = &pci_bus_dma_tag;

	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_iobus_space;
	pba.pba_memt = &sc->sc_membus_space;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pc = &lcp->lc_pc;
	pba.pba_domain = pci_ndomains++;
	if (OF_getprop(ca->ca_node, "bus-range", &busrange, sizeof(busrange)) <
	    0)
		pba.pba_bus = 0;
	else
		pba.pba_bus = busrange[0];

	config_found(self, &pba, mpcpcibrprint);
}

static int
mpcpcibrprint(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return(UNCONF);
}

u_int32_t
mpc_gen_config_reg(void *cpv, pcitag_t tag, int offset)
{
	struct pcibr_config *cp = cpv;
	unsigned int bus, dev, fcn;
	u_int32_t reg, val = PCITAG_OFFSET(tag);

	pci_decompose_tag(cpv, tag, &bus, &dev, &fcn);

	if (cp->config_type & 4) {
		reg = val | offset | 1;
		reg |= (offset >> 8) << 28;
	} else if (cp->config_type & 1) {
		/* Config Mechanism #2 */
		if (bus == 0) {
			if (dev < 11)
				return 0xffffffff;
			/*
			 * Need to do config type 0 operation
			 *  1 << (11?+dev) | fcn << 8 | reg
			 * 11? is because pci spec states
			 * that 11-15 is reserved.
			 */
			reg = 1 << (dev) | fcn << 8 | offset;
		} else {
			if (dev > 15)
				return 0xffffffff;
			/*
			 * config type 1
			 */
			reg = val | offset | 1;
		}
	} else {
		/* config mechanism #2, type 0
		 * standard cf8/cfc config
		 */
		reg =  0x80000000 | val | offset;
	}

	return reg;
}

/* #define DEBUG_CONFIG  */
pcireg_t
mpc_conf_read(void *cpv, pcitag_t tag, int offset)
{
	struct pcibr_config *cp = cpv;
	pcireg_t data;
	u_int32_t reg;
	int s;
	int daddr = 0;
	faultbuf env;
	void *oldh;

	if (offset & 3 ||
	    offset < 0 || offset >= PCI_CONFIG_SPACE_SIZE) {
#ifdef DEBUG_CONFIG
		printf ("pci_conf_read: bad reg %x\n", offset);
#endif /* DEBUG_CONFIG */
		return(~0);
	}

	reg = mpc_gen_config_reg(cpv, tag, offset);
	/* if invalid tag, return -1 */
	if (reg == 0xffffffff)
		return(~0);

	if ((cp->config_type & 2) && (offset & 0x04))
		daddr += 4;

	s = splhigh();

	oldh = curpcb->pcb_onfault;
	if (setfault(&env)) {
		/* we faulted during the read? */
		curpcb->pcb_onfault = oldh;
		splx(s);
		return 0xffffffff;
	}

	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, reg);
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */
	data = bus_space_read_4(cp->lc_iot, cp->ioh_cfc, daddr);
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, 0); /* disable */
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */

	curpcb->pcb_onfault = oldh;

	splx(s);
#ifdef DEBUG_CONFIG
	if (!((offset == 0) && (data == 0xffffffff))) {
		unsigned int bus, dev, fcn;
		pci_decompose_tag(cpv, tag, &bus, &dev, &fcn);
		printf("mpc_conf_read bus %x dev %x fcn %x offset %x", bus, dev, fcn,
			offset);
		printf(" daddr %x reg %x",daddr, reg);
		printf(" data %x\n", data);
	}
#endif

	return(data);
}

void
mpc_conf_write(void *cpv, pcitag_t tag, int offset, pcireg_t data)
{
	struct pcibr_config *cp = cpv;
	u_int32_t reg;
	int s;
	int daddr = 0;

	reg = mpc_gen_config_reg(cpv, tag, offset);

	/* if invalid tag, return ??? */
	if (reg == 0xffffffff)
		return;

	if ((cp->config_type & 2) && (offset & 0x04))
		daddr += 4;

#ifdef DEBUG_CONFIG
	{
		unsigned int bus, dev, fcn;
		pci_decompose_tag(cpv, tag, &bus, &dev, &fcn);
		printf("mpc_conf_write bus %x dev %x fcn %x offset %x", bus,
			dev, fcn, offset);
		printf(" daddr %x reg %x",daddr, reg);
		printf(" data %x\n", data);
	}
#endif

	s = splhigh();

	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, reg);
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */
	bus_space_write_4(cp->lc_iot, cp->ioh_cfc, daddr, data);
	bus_space_write_4(cp->lc_iot, cp->ioh_cf8, 0, 0); /* disable */
	bus_space_read_4(cp->lc_iot, cp->ioh_cf8, 0); /* XXX */

	splx(s);
}
