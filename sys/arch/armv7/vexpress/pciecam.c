/* $OpenBSD: pciecam.c,v 1.6 2023/10/10 18:40:34 miod Exp $ */
/*
 * Copyright (c) 2013,2017 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/extent.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/pci/pcivar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/openfirm.h>

/* Assembling ECAM Configuration Address */
#define PCIE_BUS_SHIFT			20
#define PCIE_SLOT_SHIFT			15
#define PCIE_FUNC_SHIFT			12
#define PCIE_BUS_MASK			0xff
#define PCIE_SLOT_MASK			0x1f
#define PCIE_FUNC_MASK			0x7
#define PCIE_REG_MASK			0xfff

#define PCIE_ADDR_OFFSET(bus, slot, func, reg)			\
	((((bus) & PCIE_BUS_MASK) << PCIE_BUS_SHIFT)	|	\
	(((slot) & PCIE_SLOT_MASK) << PCIE_SLOT_SHIFT)	|	\
	(((func) & PCIE_FUNC_MASK) << PCIE_FUNC_SHIFT)	|	\
	((reg) & PCIE_REG_MASK))

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct pciecam_range {
	uint32_t			 flags;
	uint64_t			 pci_base;
	uint64_t			 phys_base;
	uint64_t			 size;
};

struct pciecam_softc {
	struct device			 sc_dev;
	int				 sc_node;
	bus_space_tag_t			 sc_iot;
	bus_space_handle_t		 sc_ioh;
	bus_dma_tag_t			 sc_dmat;

	int				 sc_acells;
	int				 sc_scells;
	int				 sc_pacells;
	int				 sc_pscells;

	struct bus_space		 sc_bus;
	struct pciecam_range		*sc_pciranges;
	int				 sc_pcirangeslen;
	struct extent			*sc_ioex;
	struct extent			*sc_memex;
	char				 sc_ioex_name[32];
	char				 sc_memex_name[32];
	struct arm32_pci_chipset	 sc_pc;
};

int pciecam_match(struct device *, void *, void *);
void pciecam_attach(struct device *, struct device *, void *);
void pciecam_attach_hook(struct device *, struct device *, struct pcibus_attach_args *);
int pciecam_bus_maxdevs(void *, int);
pcitag_t pciecam_make_tag(void *, int, int, int);
void pciecam_decompose_tag(void *, pcitag_t, int *, int *, int *);
int pciecam_conf_size(void *, pcitag_t);
pcireg_t pciecam_conf_read(void *, pcitag_t, int);
void pciecam_conf_write(void *, pcitag_t, int, pcireg_t);
int pciecam_probe_device_hook(void *, struct pci_attach_args *);
int pciecam_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int pciecam_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int pciecam_intr_map_msix(struct pci_attach_args *, int, pci_intr_handle_t *);
const char *pciecam_intr_string(void *, pci_intr_handle_t);
void *pciecam_intr_establish(void *, pci_intr_handle_t, int, struct cpu_info *,
    int (*func)(void *), void *, char *);
void pciecam_intr_disestablish(void *, void *);
int pciecam_bs_map(void *, uint64_t, bus_size_t, int, bus_space_handle_t *);

const struct cfattach pciecam_ca = {
	sizeof (struct pciecam_softc), pciecam_match, pciecam_attach
};

struct cfdriver pciecam_cd = {
	NULL, "pciecam", DV_DULL
};

int
pciecam_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "pci-host-ecam-generic");
}

void
pciecam_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct pciecam_softc *sc = (struct pciecam_softc *) self;
	struct pcibus_attach_args pba;
	uint32_t *ranges;
	int i, j, nranges, rangeslen;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;

	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    faa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    faa->fa_scells);
	sc->sc_pacells = faa->fa_acells;
	sc->sc_pscells = faa->fa_scells;

	rangeslen = OF_getproplen(sc->sc_node, "ranges");
	if (rangeslen <= 0 || (rangeslen % sizeof(uint32_t)) ||
	     (rangeslen / sizeof(uint32_t)) % (sc->sc_acells +
	     sc->sc_pacells + sc->sc_scells))
		panic("pciecam_attach: invalid ranges property");

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges,
	    rangeslen);

	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_pciranges = mallocarray(nranges,
	    sizeof(struct pciecam_range), M_TEMP, M_WAITOK);
	sc->sc_pcirangeslen = nranges;

	for (i = 0, j = 0; i < nranges; i++) {
		sc->sc_pciranges[i].flags = ranges[j++];
		sc->sc_pciranges[i].pci_base = ranges[j++];
		if (sc->sc_acells - 1 == 2) {
			sc->sc_pciranges[i].pci_base <<= 32;
			sc->sc_pciranges[i].pci_base |= ranges[j++];
		}
		sc->sc_pciranges[i].phys_base = ranges[j++];
		if (sc->sc_pacells == 2) {
			sc->sc_pciranges[i].phys_base <<= 32;
			sc->sc_pciranges[i].phys_base |= ranges[j++];
		}
		sc->sc_pciranges[i].size = ranges[j++];
		if (sc->sc_scells == 2) {
			sc->sc_pciranges[i].size <<= 32;
			sc->sc_pciranges[i].size |= ranges[j++];
		}
	}

	free(ranges, M_TEMP, rangeslen);

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("pciecam_attach: bus_space_map failed!");

	printf("\n");

	/*
	 * Map PCIe address space.
	 */
	snprintf(sc->sc_ioex_name, sizeof(sc->sc_ioex_name),
	    "%s pciio", sc->sc_dev.dv_xname);
	sc->sc_ioex = extent_create(sc->sc_ioex_name, 0, (u_long)-1L,
	    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);

	snprintf(sc->sc_memex_name, sizeof(sc->sc_memex_name),
	    "%s pcimem", sc->sc_dev.dv_xname);
	sc->sc_memex = extent_create(sc->sc_memex_name, 0, (u_long)-1L,
	    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_FILLED);

	for (i = 0; i < nranges; i++) {
		switch (sc->sc_pciranges[i].flags & OFW_PCI_PHYS_HI_SPACEMASK) {
		case OFW_PCI_PHYS_HI_SPACE_IO:
			extent_free(sc->sc_ioex, sc->sc_pciranges[i].pci_base,
			    sc->sc_pciranges[i].size, EX_NOWAIT);
			break;
		case OFW_PCI_PHYS_HI_SPACE_MEM64:
			if (sc->sc_pciranges[i].pci_base +
			    sc->sc_pciranges[i].size >= (1ULL << 32))
				break;
			/* FALLTHROUGH */
		case OFW_PCI_PHYS_HI_SPACE_MEM32:
			extent_free(sc->sc_memex, sc->sc_pciranges[i].pci_base,
			    sc->sc_pciranges[i].size, EX_NOWAIT);
			break;
		}
	}

	memcpy(&sc->sc_bus, sc->sc_iot, sizeof(sc->sc_bus));
	sc->sc_bus.bs_cookie = sc;
	sc->sc_bus.bs_map = pciecam_bs_map;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = pciecam_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = pciecam_bus_maxdevs;
	sc->sc_pc.pc_make_tag = pciecam_make_tag;
	sc->sc_pc.pc_decompose_tag = pciecam_decompose_tag;
	sc->sc_pc.pc_conf_size = pciecam_conf_size;
	sc->sc_pc.pc_conf_read = pciecam_conf_read;
	sc->sc_pc.pc_conf_write = pciecam_conf_write;
	sc->sc_pc.pc_probe_device_hook = pciecam_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = pciecam_intr_map;
	sc->sc_pc.pc_intr_map_msi = pciecam_intr_map_msi;
	sc->sc_pc.pc_intr_map_msix = pciecam_intr_map_msix;
	sc->sc_pc.pc_intr_string = pciecam_intr_string;
	sc->sc_pc.pc_intr_establish = pciecam_intr_establish;
	sc->sc_pc.pc_intr_disestablish = pciecam_intr_disestablish;

	bzero(&pba, sizeof(pba));
	pba.pba_dmat = sc->sc_dmat;

	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus;
	pba.pba_memt = &sc->sc_bus;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;

	config_found(self, &pba, NULL);
}

void
pciecam_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
pciecam_bus_maxdevs(void *sc, int busno) {
	return (32);
}

#define BUS_SHIFT 24
#define DEVICE_SHIFT 19
#define FNC_SHIFT 16

pcitag_t
pciecam_make_tag(void *sc, int bus, int dev, int fnc)
{
	return (bus << BUS_SHIFT) | (dev << DEVICE_SHIFT) | (fnc << FNC_SHIFT);
}

void
pciecam_decompose_tag(void *sc, pcitag_t tag, int *busp, int *devp, int *fncp)
{
	if (busp != NULL)
		*busp = (tag >> BUS_SHIFT) & 0xff;
	if (devp != NULL)
		*devp = (tag >> DEVICE_SHIFT) & 0x1f;
	if (fncp != NULL)
		*fncp = (tag >> FNC_SHIFT) & 0x7;
}

int
pciecam_conf_size(void *sc, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
pciecam_conf_read(void *v, pcitag_t tag, int reg)
{
	struct pciecam_softc *sc = (struct pciecam_softc *)v;
	int bus, dev, fn;

	pciecam_decompose_tag(sc, tag, &bus, &dev, &fn);

	return HREAD4(sc, PCIE_ADDR_OFFSET(bus, dev, fn, reg & ~0x3));
}

void
pciecam_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct pciecam_softc *sc = (struct pciecam_softc *)v;
	int bus, dev, fn;

	pciecam_decompose_tag(sc, tag, &bus, &dev, &fn);

	HWRITE4(sc, PCIE_ADDR_OFFSET(bus, dev, fn, reg & ~0x3), data);
}

int
pciecam_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	return 0;
}

struct pciecam_intr_handle {
	pci_chipset_tag_t	ih_pc;
	pcitag_t		ih_tag;
	int			ih_intrpin;
	int			ih_msi;
};

int
pciecam_intr_map(struct pci_attach_args *pa,
    pci_intr_handle_t *ihp)
{
	struct pciecam_intr_handle *ih;
	ih = malloc(sizeof(struct pciecam_intr_handle), M_DEVBUF, M_WAITOK);
	ih->ih_pc = pa->pa_pc;
	ih->ih_tag = pa->pa_intrtag;
	ih->ih_intrpin = pa->pa_intrpin;
	ih->ih_msi = 0;
	*ihp = (pci_intr_handle_t)ih;
	return 0;
}

int
pciecam_intr_map_msi(struct pci_attach_args *pa,
    pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	struct pciecam_intr_handle *ih;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, NULL, NULL) == 0)
		return 1;

	ih = malloc(sizeof(struct pciecam_intr_handle), M_DEVBUF, M_WAITOK);
	ih->ih_pc = pa->pa_pc;
	ih->ih_tag = pa->pa_tag;
	ih->ih_intrpin = pa->pa_intrpin;
	ih->ih_msi = 1;
	*ihp = (pci_intr_handle_t)ih;

	return 0;
}

int
pciecam_intr_map_msix(struct pci_attach_args *pa,
    int vec, pci_intr_handle_t *ihp)
{
	*ihp = (pci_intr_handle_t) pa->pa_pc;
	return -1;
}

const char *
pciecam_intr_string(void *sc, pci_intr_handle_t ihp)
{
	struct pciecam_intr_handle *ih = (struct pciecam_intr_handle *)ihp;

	if (ih->ih_msi)
		return "msi";

	return "irq";
}

void *
pciecam_intr_establish(void *self, pci_intr_handle_t ihp, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct pciecam_softc *sc = (struct pciecam_softc *)self;
	struct pciecam_intr_handle *ih = (struct pciecam_intr_handle *)ihp;
	void *cookie;

	if (ih->ih_msi) {
		uint64_t addr, data;
		pcireg_t reg;
		int off;

		cookie = arm_intr_establish_fdt_msi_cpu(sc->sc_node, &addr,
		    &data, level, ci, func, arg, (void *)name);
		if (cookie == NULL)
			return NULL;

		/* TODO: translate address to the PCI device's view */

		if (pci_get_capability(ih->ih_pc, ih->ih_tag, PCI_CAP_MSI,
		    &off, &reg) == 0)
			panic("%s: no msi capability", __func__);

		if (reg & PCI_MSI_MC_C64) {
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MA, addr);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MAU32, addr >> 32);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MD64, data);
		} else {
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MA, addr);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MD32, data);
		}
		pci_conf_write(ih->ih_pc, ih->ih_tag,
		    off, reg | PCI_MSI_MC_MSIE);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		pciecam_decompose_tag(sc, ih->ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih->ih_intrpin;

		cookie = arm_intr_establish_fdt_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
	}

	free(ih, M_DEVBUF, sizeof(struct pciecam_intr_handle));
	return cookie;
}

void
pciecam_intr_disestablish(void *sc, void *cookie)
{
	/* do something */
}

/*
 * Translate memory address if needed.
 */
int
pciecam_bs_map(void *t, uint64_t bpa, bus_size_t size,
    int flag, bus_space_handle_t *bshp)
{
	struct pciecam_softc *sc = t;
	uint64_t physbase, pcibase, psize;
	int i;

	for (i = 0; i < sc->sc_pcirangeslen; i++) {
		physbase = sc->sc_pciranges[i].phys_base;
		pcibase = sc->sc_pciranges[i].pci_base;
		psize = sc->sc_pciranges[i].size;

		if (bpa >= pcibase && bpa + size <= pcibase + psize)
			return bus_space_map(sc->sc_iot,
			    bpa - pcibase + physbase, size, flag, bshp);
	}

	return ENXIO;
}
