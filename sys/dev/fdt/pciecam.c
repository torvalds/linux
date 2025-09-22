/* $OpenBSD: pciecam.c,v 1.5 2024/02/03 10:37:26 kettenis Exp $ */
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
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/extent.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/pci/pcivar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_misc.h>

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

	int				 sc_dw_quirk;

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
	struct machine_pci_chipset	 sc_pc;
};

struct pciecam_intr_handle {
	struct machine_intr_handle	 pih_ih;
	bus_dma_tag_t			 pih_dmat;
	bus_dmamap_t			 pih_map;
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
const char *pciecam_intr_string(void *, pci_intr_handle_t);
void *pciecam_intr_establish(void *, pci_intr_handle_t, int,
    struct cpu_info *, int (*func)(void *), void *, char *);
void pciecam_intr_disestablish(void *, void *);
int pciecam_bs_map(bus_space_tag_t, bus_addr_t, bus_size_t, int, bus_space_handle_t *);
paddr_t pciecam_bs_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);

struct interrupt_controller pciecam_ic = {
	.ic_barrier = intr_barrier
};

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

	return (OF_is_compatible(faa->fa_node, "pci-host-ecam-generic") ||
	    OF_is_compatible(faa->fa_node, "snps,dw-pcie-ecam"));
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

	if (OF_is_compatible(faa->fa_node, "snps,dw-pcie-ecam"))
		sc->sc_dw_quirk = 1;

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
		if (sc->sc_pciranges[i].flags >> 24 == 0)
			continue;
		if (sc->sc_pciranges[i].flags >> 24 == 1)
			extent_free(sc->sc_ioex, sc->sc_pciranges[i].pci_base,
			    sc->sc_pciranges[i].size, EX_NOWAIT);
		else
			extent_free(sc->sc_memex, sc->sc_pciranges[i].pci_base,
			    sc->sc_pciranges[i].size, EX_NOWAIT);
	}

	memcpy(&sc->sc_bus, sc->sc_iot, sizeof(sc->sc_bus));
	sc->sc_bus.bus_private = sc;
	sc->sc_bus._space_map = pciecam_bs_map;
	sc->sc_bus._space_mmap = pciecam_bs_mmap;

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
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
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
	pba.pba_pmemex = sc->sc_memex;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;

	if (OF_getproplen(sc->sc_node, "msi-map") > 0 ||
	    OF_getproplen(sc->sc_node, "msi-parent") > 0)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

void
pciecam_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
pciecam_bus_maxdevs(void *v, int bus)
{
	struct pciecam_softc *sc = (struct pciecam_softc *)v;

	if (bus == 0 && sc->sc_dw_quirk)
		return 1;
	return 32;
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
	struct pciecam_softc *sc = (struct pciecam_softc *)v;
	uint16_t rid;
	int i;

	rid = pci_requester_id(pa->pa_pc, pa->pa_tag);
	pa->pa_dmat = iommu_device_map_pci(sc->sc_node, rid, pa->pa_dmat);

	for (i = 0; i < sc->sc_pcirangeslen; i++) {
		if (sc->sc_pciranges[i].flags >> 24 == 0)
			continue;
		iommu_reserve_region_pci(sc->sc_node, rid,
		    sc->sc_pciranges[i].pci_base, sc->sc_pciranges[i].size);
	}

	return 0;
}

int
pciecam_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	ihp->ih_pc = pa->pa_pc;
	ihp->ih_tag = pa->pa_intrtag;
	ihp->ih_intrpin = pa->pa_intrpin;
	ihp->ih_type = PCI_INTX;

	return 0;
}

const char *
pciecam_intr_string(void *sc, pci_intr_handle_t ih)
{
	switch (ih.ih_type) {
	case PCI_MSI:
		return "msi";
	case PCI_MSIX:
		return "msix";
	}

	return "irq";
}

void *
pciecam_intr_establish(void *self, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct pciecam_softc *sc = (struct pciecam_softc *)self;
	struct pciecam_intr_handle *pih;
	bus_dma_segment_t seg;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		uint64_t addr = 0, data;

		/* Assume hardware passes Requester ID as sideband data. */
		data = pci_requester_id(ih.ih_pc, ih.ih_tag);
		cookie = fdt_intr_establish_msi_cpu(sc->sc_node, &addr,
		    &data, level, ci, func, arg, (void *)name);
		if (cookie == NULL)
			return NULL;

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK);
		pih->pih_ih.ih_ic = &pciecam_ic;
		pih->pih_ih.ih_ih = cookie;
		pih->pih_dmat = ih.ih_dmat;

		if (bus_dmamap_create(pih->pih_dmat, sizeof(uint32_t), 1,
		    sizeof(uint32_t), 0, BUS_DMA_WAITOK, &pih->pih_map)) {
			free(pih, M_DEVBUF, sizeof(*pih));
			fdt_intr_disestablish(cookie);
			return NULL;
		}

		memset(&seg, 0, sizeof(seg));
		seg.ds_addr = addr;
		seg.ds_len = sizeof(uint32_t);

		if (bus_dmamap_load_raw(pih->pih_dmat, pih->pih_map,
		    &seg, 1, sizeof(uint32_t), BUS_DMA_WAITOK)) {
			bus_dmamap_destroy(pih->pih_dmat, pih->pih_map);
			free(pih, M_DEVBUF, sizeof(*pih));
			fdt_intr_disestablish(cookie);
			return NULL;
		}

		addr = pih->pih_map->dm_segs[0].ds_addr;
		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		pciecam_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK);
		pih->pih_ih.ih_ic = &pciecam_ic;
		pih->pih_ih.ih_ih = cookie;
		pih->pih_dmat = NULL;
	}

	return pih;
}

void
pciecam_intr_disestablish(void *sc, void *cookie)
{
	struct pciecam_intr_handle *pih = cookie;

	fdt_intr_disestablish(pih->pih_ih.ih_ih);
	if (pih->pih_dmat) {
		bus_dmamap_unload(pih->pih_dmat, pih->pih_map);
		bus_dmamap_destroy(pih->pih_dmat, pih->pih_map);
	}
	free(pih, M_DEVBUF, sizeof(*pih));
}

/*
 * Translate memory address if needed.
 */
int
pciecam_bs_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flag, bus_space_handle_t *bshp)
{
	struct pciecam_softc *sc = t->bus_private;
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

paddr_t
pciecam_bs_mmap(bus_space_tag_t t, bus_addr_t bpa, off_t off,
    int prot, int flags)
{
	struct pciecam_softc *sc = t->bus_private;
	uint64_t physbase, pcibase, psize;
	int i;

	for (i = 0; i < sc->sc_pcirangeslen; i++) {
		physbase = sc->sc_pciranges[i].phys_base;
		pcibase = sc->sc_pciranges[i].pci_base;
		psize = sc->sc_pciranges[i].size;

		if (bpa >= pcibase && bpa < pcibase + psize)
			return bus_space_mmap(sc->sc_iot,
			    bpa - pcibase + physbase, off, prot, flags);
	}

	return -1;
}
