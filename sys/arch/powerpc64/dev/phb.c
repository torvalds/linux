/*	$OpenBSD: phb.c,v 1.23 2022/04/06 18:59:27 naddy Exp $	*/
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
#include <sys/extent.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

extern paddr_t physmax;		/* machdep.c */

#define IODA_TVE_SELECT		(1ULL << 59)

#define IODA_TCE_TABLE_SIZE_MAX	(1ULL << 42)

#define IODA_TCE_READ		(1ULL << 0)
#define IODA_TCE_WRITE		(1ULL << 1)

#define PHB_DMA_OFFSET		(1ULL << 32)

struct phb_dmamem;

struct phb_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct phb_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_dma_tag_t		sc_dmat;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct phb_range	*sc_ranges;
	int			sc_nranges;

	uint64_t		sc_phb_id;
	uint64_t		sc_pe_number;
	struct phb_dmamem	*sc_tce_table;
	uint32_t		sc_msi_ranges[2];
	uint32_t		sc_xive;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;
	struct machine_bus_dma_tag sc_bus_dmat;

	struct ppc64_pci_chipset sc_pc;
	struct extent		*sc_busex;
	struct extent		*sc_memex;
	struct extent		*sc_ioex;
	int			sc_bus;
};

struct phb_dmamem {
	bus_dmamap_t		pdm_map;
	bus_dma_segment_t	pdm_seg;
	size_t			pdm_size;
	caddr_t			pdm_kva;
};

#define PHB_DMA_MAP(_pdm)	((_pdm)->pdm_map)
#define PHB_DMA_LEN(_pdm)	((_pdm)->pdm_size)
#define PHB_DMA_DVA(_pdm)	((_pdm)->pdm_map->dm_segs[0].ds_addr)
#define PHB_DMA_KVA(_pdm)	((void *)(_pdm)->pdm_kva)

struct phb_dmamem *phb_dmamem_alloc(bus_dma_tag_t, bus_size_t,
	    bus_size_t);
void	phb_dmamem_free(bus_dma_tag_t, struct phb_dmamem *);

int	phb_match(struct device *, void *, void *);
void	phb_attach(struct device *, struct device *, void *);

const struct cfattach phb_ca = {
	sizeof (struct phb_softc), phb_match, phb_attach
};

struct cfdriver phb_cd = {
	NULL, "phb", DV_DULL
};

void	phb_setup_tce_table(struct phb_softc *sc);

void	phb_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	phb_bus_maxdevs(void *, int);
pcitag_t phb_make_tag(void *, int, int, int);
void	phb_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	phb_conf_size(void *, pcitag_t);
pcireg_t phb_conf_read(void *, pcitag_t, int);
void	phb_conf_write(void *, pcitag_t, int, pcireg_t);

int	phb_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *phb_intr_string(void *, pci_intr_handle_t);
void	*phb_intr_establish(void *, pci_intr_handle_t, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	phb_intr_disestablish(void *, void *);

int	phb_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	phb_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
paddr_t phb_bs_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);
int	phb_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int, paddr_t *, int *, int);
int	phb_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);

int
phb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "ibm,ioda2-phb") ||
	    OF_is_compatible(faa->fa_node, "ibm,ioda3-phb"));
}

void
phb_attach(struct device *parent, struct device *self, void *aux)
{
	struct phb_softc *sc = (struct phb_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	uint32_t bus_range[2];
	uint32_t *ranges;
	uint32_t m64window[6];
	uint32_t m64ranges[2];
	int i, j, nranges, rangeslen;
	uint32_t window;
	uint32_t chip_id;
	int64_t error;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;
	sc->sc_phb_id = OF_getpropint64(sc->sc_node, "ibm,opal-phbid", 0);
	sc->sc_pe_number = 0;

	if (OF_getproplen(sc->sc_node, "ibm,chip-id") == sizeof(chip_id)) {
		chip_id = OF_getpropint(sc->sc_node, "ibm,chip-id", 0);
		printf(": chip 0x%x", chip_id);
	}

	/*
	 * Reset the IODA tables.  Should clear any gunk left behind
	 * by Linux.
	 */
	error = opal_pci_reset(sc->sc_phb_id, OPAL_RESET_PCI_IODA_TABLE,
	    OPAL_ASSERT_RESET);
	if (error != OPAL_SUCCESS) {
		printf(": can't reset IODA table\n");
		return;
	}

	/*
	 * Keep things simple and use a single PE for everything below
	 * this host bridge.
	 */
	error = opal_pci_set_pe(sc->sc_phb_id, sc->sc_pe_number, 0,
	    OPAL_IGNORE_RID_BUS_NUMBER, OPAL_IGNORE_RID_DEVICE_NUMBER,
	    OPAL_IGNORE_RID_FUNCTION_NUMBER, OPAL_MAP_PE);
	if (error != OPAL_SUCCESS) {
		printf(": can't map PHB PE\n");
		return;
	}

	/* Enable bypass mode. */
	error = opal_pci_map_pe_dma_window_real(sc->sc_phb_id,
	    sc->sc_pe_number, (sc->sc_pe_number << 1) | 1,
	    IODA_TVE_SELECT, physmax);
	if (error != OPAL_SUCCESS) {
		printf(": can't enable DMA bypass\n");
		return;
	}

	/*
	 * Parse address ranges such that we can do the appropriate
	 * address translations.
	 */

	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    faa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    faa->fa_scells);
	sc->sc_pacells = faa->fa_acells;
	sc->sc_pscells = faa->fa_scells;

	rangeslen = OF_getproplen(sc->sc_node, "ranges");
	if (rangeslen <= 0 || (rangeslen % sizeof(uint32_t)) ||
	     (rangeslen / sizeof(uint32_t)) % (sc->sc_acells +
	     sc->sc_pacells + sc->sc_scells)) {
		printf(": invalid ranges property\n");
		return;
	}

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges, rangeslen);

	/*
	 * Reserve an extra slot here and make sure it is filled
	 * with zeroes.
	 */
	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_ranges = mallocarray(nranges + 1,
	    sizeof(struct phb_range), M_DEVBUF, M_ZERO | M_WAITOK);
	sc->sc_nranges = nranges + 1;

	for (i = 0, j = 0; i < sc->sc_nranges; i++) {
		sc->sc_ranges[i].flags = ranges[j++];
		sc->sc_ranges[i].pci_base = ranges[j++];
		if (sc->sc_acells - 1 == 2) {
			sc->sc_ranges[i].pci_base <<= 32;
			sc->sc_ranges[i].pci_base |= ranges[j++];
		}
		sc->sc_ranges[i].phys_base = ranges[j++];
		if (sc->sc_pacells == 2) {
			sc->sc_ranges[i].phys_base <<= 32;
			sc->sc_ranges[i].phys_base |= ranges[j++];
		}
		sc->sc_ranges[i].size = ranges[j++];
		if (sc->sc_scells == 2) {
			sc->sc_ranges[i].size <<= 32;
			sc->sc_ranges[i].size |= ranges[j++];
		}
	}

	free(ranges, M_TEMP, rangeslen);

	/*
	 * IBM has chosen a non-standard way to encode 64-bit mmio
	 * ranges.  Stick the information into the slot we reserved
	 * above.
	 */
	if (OF_getpropintarray(sc->sc_node, "ibm,opal-m64-window",
	    m64window, sizeof(m64window)) == sizeof(m64window)) {
		sc->sc_ranges[sc->sc_nranges - 1].flags = 0x03000000;
		sc->sc_ranges[sc->sc_nranges - 1].pci_base =
		    (uint64_t)m64window[0] << 32 | m64window[1];
		sc->sc_ranges[sc->sc_nranges - 1].phys_base =
		    (uint64_t)m64window[2] << 32 | m64window[3];
		sc->sc_ranges[sc->sc_nranges - 1].size =
		    (uint64_t)m64window[4] << 32 | m64window[5];
	}

	/*
	 * Enable all the 64-bit mmio windows we found.
	 */
	m64ranges[0] = 0; m64ranges[1] = 16;
	OF_getpropintarray(sc->sc_node, "ibm,opal-available-m64-ranges",
	    m64ranges, sizeof(m64ranges));
	window = m64ranges[0];
	for (i = 0; i < sc->sc_nranges; i++) {
		/* Skip non-64-bit ranges. */
		if ((sc->sc_ranges[i].flags & 0x03000000) != 0x03000000)
			continue;

		/* Bail if we're out of 64-bit mmio windows. */
		if (window > m64ranges[1]) {
			printf(": no 64-bit mmio window available\n");
			return;
		}

		error = opal_pci_set_phb_mem_window(sc->sc_phb_id,
		    OPAL_M64_WINDOW_TYPE, window, sc->sc_ranges[i].phys_base,
		    sc->sc_ranges[i].pci_base, sc->sc_ranges[i].size);
		if (error != OPAL_SUCCESS) {
			printf(": can't set 64-bit mmio window\n");
			return;
		}
		error = opal_pci_phb_mmio_enable(sc->sc_phb_id,
		    OPAL_M64_WINDOW_TYPE, window, OPAL_ENABLE_M64_SPLIT);
		if (error != OPAL_SUCCESS) {
			printf(": can't enable 64-bit mmio window\n");
			return;
		}

		window++;
	}

	OF_getpropintarray(sc->sc_node, "ibm,opal-msi-ranges",
	    sc->sc_msi_ranges, sizeof(sc->sc_msi_ranges));

	/* Create extents for our address spaces. */
	sc->sc_busex = extent_create("pcibus", 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create("pcimem", 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_ioex = extent_create("pciio", 0, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range) ||
	    bus_range[0] >= 256 || bus_range[1] >= 256) {
		bus_range[0] = 0;
		bus_range[1] = 255;
	}
	sc->sc_bus = bus_range[0];
	extent_free(sc->sc_busex, bus_range[0],
	    bus_range[1] - bus_range[0] + 1, EX_WAITOK);

	/* Set up mmio ranges. */
	for (i = 0; i < sc->sc_nranges; i++) {
		if ((sc->sc_ranges[i].flags & 0x02000000) != 0x02000000)
			continue;

		extent_free(sc->sc_memex, sc->sc_ranges[i].pci_base,
		    sc->sc_ranges[i].size, EX_WAITOK);
	}

	printf("\n");

	/*
	 * The DMA controllers of many PCI devices developed for the
	 * x86 architectures may not support the full 64-bit PCI
	 * address space.  Examples of such devices are Radeon GPUs
	 * that support only 36, 40 or 44 address lines.  This means
	 * they can't enable the TVE selection bit to request IODA
	 * no-translate (bypass) operation.
	 *
	 * To allow such devices to work, we set up a TCE table that
	 * provides a 1:1 mapping of all physical memory where the
	 * physical page at address zero as mapped at 4 GB in PCI
	 * address space.  If we fail to set up this TCE table we fall
	 * back on using no-translate operation, which means that
	 * devices that don't implement 64 address lines may not
	 * function properly.
	 */
	phb_setup_tce_table(sc);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = phb_bs_iomap;
	sc->sc_bus_iot._space_read_2 = little_space_read_2;
	sc->sc_bus_iot._space_read_4 = little_space_read_4;
	sc->sc_bus_iot._space_read_8 = little_space_read_8;
	sc->sc_bus_iot._space_write_2 = little_space_write_2;
	sc->sc_bus_iot._space_write_4 = little_space_write_4;
	sc->sc_bus_iot._space_write_8 = little_space_write_8;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = phb_bs_memmap;
	sc->sc_bus_memt._space_mmap = phb_bs_mmap;
	sc->sc_bus_memt._space_read_2 = little_space_read_2;
	sc->sc_bus_memt._space_read_4 = little_space_read_4;
	sc->sc_bus_memt._space_read_8 = little_space_read_8;
	sc->sc_bus_memt._space_write_2 = little_space_write_2;
	sc->sc_bus_memt._space_write_4 = little_space_write_4;
	sc->sc_bus_memt._space_write_8 = little_space_write_8;

	memcpy(&sc->sc_bus_dmat, sc->sc_dmat, sizeof(sc->sc_bus_dmat));
	sc->sc_bus_dmat._cookie = sc;
	sc->sc_bus_dmat._dmamap_load_buffer = phb_dmamap_load_buffer;
	sc->sc_bus_dmat._dmamap_load_raw = phb_dmamap_load_raw;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = phb_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = phb_bus_maxdevs;
	sc->sc_pc.pc_make_tag = phb_make_tag;
	sc->sc_pc.pc_decompose_tag = phb_decompose_tag;
	sc->sc_pc.pc_conf_size = phb_conf_size;
	sc->sc_pc.pc_conf_read = phb_conf_read;
	sc->sc_pc.pc_conf_write = phb_conf_write;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = phb_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = phb_intr_string;
	sc->sc_pc.pc_intr_establish = phb_intr_establish;
	sc->sc_pc.pc_intr_disestablish = phb_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = &sc->sc_bus_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
	pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

void
phb_setup_tce_table(struct phb_softc *sc)
{
	uint64_t tce_table_size, tce_page_size;
	uint64_t *tce;
	uint32_t *tce_sizes;
	int tce_page_shift;
	int i, len, offset, nentries;
	paddr_t pa;
	int64_t error;

	/* Determine the maximum supported TCE page size. */
	len = OF_getproplen(sc->sc_node, "ibm,supported-tce-sizes");
	if (len <= 0 || (len % sizeof(uint32_t)))
		return;
	tce_sizes = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ibm,supported-tce-sizes",
	    tce_sizes, len);
	tce_page_shift = 0;
	for (i = 0; i < len / sizeof(uint32_t); i++)
		tce_page_shift = MAX(tce_page_shift, tce_sizes[i]);
	free(tce_sizes, M_TEMP, len);

	/* Bail out if we don't support 1G pages. */
	if (tce_page_shift < 30)
		return;

	/* Calculate the appropriate size of the TCE table. */
	tce_page_size = (1ULL << tce_page_shift);
	tce_table_size = PAGE_SIZE;
	nentries = howmany(PHB_DMA_OFFSET + physmax, tce_page_size);
	while (tce_table_size < nentries * sizeof(*tce))
		tce_table_size *= 2;
	if (tce_table_size > IODA_TCE_TABLE_SIZE_MAX)
		return;

	/* Allocate the TCE table. */
	sc->sc_tce_table = phb_dmamem_alloc(sc->sc_dmat,
	    tce_table_size, tce_table_size);
	if (sc->sc_tce_table == NULL) {
		printf(": can't allocate DMA translation table\n");
		return;
	}

	/* Fill TCE table. */
	tce = PHB_DMA_KVA(sc->sc_tce_table);
	offset = PHB_DMA_OFFSET >> tce_page_shift;
	for (pa = 0, i = 0; pa < physmax; pa += tce_page_size, i++)
		tce[i + offset] = pa | IODA_TCE_READ | IODA_TCE_WRITE;

	/* Set up translations. */
	error = opal_pci_map_pe_dma_window(sc->sc_phb_id,
	    sc->sc_pe_number, sc->sc_pe_number << 1, 1,
	    PHB_DMA_DVA(sc->sc_tce_table), PHB_DMA_LEN(sc->sc_tce_table),
	    tce_page_size);
	if (error != OPAL_SUCCESS) {
		printf("%s: can't enable DMA window\n", sc->sc_dev.dv_xname);
		phb_dmamem_free(sc->sc_dmat, sc->sc_tce_table);
		sc->sc_tce_table = NULL;
		return;
	}
}

void
phb_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
phb_bus_maxdevs(void *v, int bus)
{
	struct phb_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

int
phb_find_node(int node, int bus, int device, int function)
{
	uint32_t reg[5];
	uint32_t phys_hi;
	int child;

	phys_hi = ((bus << 16) | (device << 11) | (function << 8));

	for (child = OF_child(node); child; child = OF_peer(child)) {
		if (OF_getpropintarray(child, "reg",
		    reg, sizeof(reg)) != sizeof(reg))
			continue;

		if (reg[0] == phys_hi)
			return child;

		node = phb_find_node(child, bus, device, function);
		if (node)
			return node;
	}

	return 0;
}

pcitag_t
phb_make_tag(void *v, int bus, int device, int function)
{
	struct phb_softc *sc = v;
	int node;

	node = phb_find_node(sc->sc_node, bus, device, function);
	return (((pcitag_t)node << 32) |
	    (bus << 8) | (device << 3) | (function << 0));
}

void
phb_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 8) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 3) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 0) & 0x7;
}

int
phb_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
phb_conf_read(void *v, pcitag_t tag, int reg)
{
	struct phb_softc *sc = v;
	int64_t error;
	uint32_t data;
	uint16_t pci_error_state;
	uint8_t freeze_state;

	tag = PCITAG_OFFSET(tag);
	error = opal_pci_config_read_word(sc->sc_phb_id,
	    tag, reg, opal_phys(&data));
	if (error == OPAL_SUCCESS && data != 0xffffffff)
		return data;

	/*
	 * Probing hardware that isn't there may ut the host bridge in
	 * an error state.  Clear the error.
	 */
	error = opal_pci_eeh_freeze_status(sc->sc_phb_id, sc->sc_pe_number,
	    opal_phys(&freeze_state), opal_phys(&pci_error_state), NULL);
	if (freeze_state)
		opal_pci_eeh_freeze_clear(sc->sc_phb_id, sc->sc_pe_number,
		    OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);

	return 0xffffffff;
}

void
phb_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct phb_softc *sc = v;

	tag = PCITAG_OFFSET(tag);
	opal_pci_config_write_word(sc->sc_phb_id, tag, reg, data);
}

int
phb_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int pin = pa->pa_rawintrpin;

	if (pin == 0 || pin > PCI_INTERRUPT_PIN_MAX)
		return -1;

	if (pa->pa_tag == 0)
		return -1;

	ihp->ih_pc = pa->pa_pc;
	ihp->ih_tag = pa->pa_intrtag;
	ihp->ih_intrpin = pa->pa_intrpin;
	ihp->ih_type = PCI_INTX;

	return 0;
}

const char *
phb_intr_string(void *v, pci_intr_handle_t ih)
{
	switch (ih.ih_type) {
	case PCI_MSI32:
	case PCI_MSI64:
		return "msi";
	case PCI_MSIX:
		return "msix";
	}

	return "intx";
}

void *
phb_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct phb_softc *sc = v;
	void *cookie = NULL;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		uint32_t addr32, data;
		uint64_t addr;
		uint32_t xive;
		int64_t error;

		if (sc->sc_xive >= sc->sc_msi_ranges[1])
			return NULL;

		/* Allocate an MSI. */
		xive = sc->sc_xive++;

		error = opal_pci_set_xive_pe(sc->sc_phb_id,
		    sc->sc_pe_number, xive);
		if (error != OPAL_SUCCESS)
			return NULL;

		/*
		 * Use a 32-bit MSI address whenever possible.  Some
		 * older Radeon graphics cards advertise 64-bit MSI
		 * but only implement 40 bits.
		 */
		error = opal_get_msi_32(sc->sc_phb_id, 0, xive,
		    1, opal_phys(&addr32), opal_phys(&data));
		if (error == OPAL_SUCCESS)
			addr = addr32;
		if (error != OPAL_SUCCESS && ih.ih_type != PCI_MSI32) {
			error = opal_get_msi_64(sc->sc_phb_id, 0, xive,
			    1, opal_phys(&addr), opal_phys(&data));
		}
		if (error != OPAL_SUCCESS)
			return NULL;

		cookie = intr_establish(sc->sc_msi_ranges[0] + xive,
		    IST_EDGE, level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];
		int node;

		phb_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		/* Host bridge child node holds the interrupt map. */
		node = OF_child(sc->sc_node);
		if (node == 0)
			return NULL;

		cookie = fdt_intr_establish_imap(node, reg, sizeof(reg),
		    level, func, arg, name);
	}

	return cookie;
}

void
phb_intr_disestablish(void *v, void *cookie)
{
}

int
phb_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct phb_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}

int
phb_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct phb_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x02000000) == 0x02000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}

	return ENXIO;
}

paddr_t
phb_bs_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off,
    int prot, int flags)
{
	struct phb_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x02000000) == 0x02000000 &&
		    addr >= pci_start && addr + PAGE_SIZE <= pci_end) {
			return bus_space_mmap(sc->sc_iot,
			    addr - pci_start + phys_start, off, prot, flags);
		}
	}

	return -1;
}

int
phb_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, paddr_t *lastaddrp,
    int *segp, int first)
{
	struct phb_softc *sc = t->_cookie;
	int seg, firstseg = *segp;
	int error;

	error = sc->sc_dmat->_dmamap_load_buffer(sc->sc_dmat, map, buf, buflen,
	    p, flags, lastaddrp, segp, first);
	if (error)
		return error;

	/* For each segment. */
	for (seg = firstseg; seg <= *segp; seg++) {
		map->dm_segs[seg].ds_addr = map->dm_segs[seg]._ds_paddr;
		if (sc->sc_tce_table) 
			map->dm_segs[seg].ds_addr += PHB_DMA_OFFSET;
		else
			map->dm_segs[seg].ds_addr |= IODA_TVE_SELECT;
	}

	return 0;
}

int
phb_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	struct phb_softc *sc = t->_cookie;
	int seg, error;

	error = sc->sc_dmat->_dmamap_load_raw(sc->sc_dmat, map,
	     segs, nsegs, size, flags);
	if (error)
		return error;

	/* For each segment. */
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		map->dm_segs[seg].ds_addr = map->dm_segs[seg]._ds_paddr;
		if (sc->sc_tce_table) 
			map->dm_segs[seg].ds_addr += PHB_DMA_OFFSET;
		else
			map->dm_segs[seg].ds_addr |= IODA_TVE_SELECT;
	}

	return 0;
}

struct phb_dmamem *
phb_dmamem_alloc(bus_dma_tag_t dmat, bus_size_t size, bus_size_t align)
{
	struct phb_dmamem *pdm;
	int nsegs;

	pdm = malloc(sizeof(*pdm), M_DEVBUF, M_WAITOK | M_ZERO);
	pdm->pdm_size = size;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &pdm->pdm_map) != 0)
		goto pdmfree;

	if (bus_dmamem_alloc(dmat, size, align, 0, &pdm->pdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, &pdm->pdm_seg, nsegs, size,
	    &pdm->pdm_kva, BUS_DMA_WAITOK | BUS_DMA_NOCACHE) != 0)
		goto free;

	if (bus_dmamap_load_raw(dmat, pdm->pdm_map, &pdm->pdm_seg,
	    nsegs, size, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return pdm;

unmap:
	bus_dmamem_unmap(dmat, pdm->pdm_kva, size);
free:
	bus_dmamem_free(dmat, &pdm->pdm_seg, 1);
destroy:
	bus_dmamap_destroy(dmat, pdm->pdm_map);
pdmfree:
	free(pdm, M_DEVBUF, sizeof(*pdm));

	return NULL;
}

void
phb_dmamem_free(bus_dma_tag_t dmat, struct phb_dmamem *pdm)
{
	bus_dmamem_unmap(dmat, pdm->pdm_kva, pdm->pdm_size);
	bus_dmamem_free(dmat, &pdm->pdm_seg, 1);
	bus_dmamap_destroy(dmat, pdm->pdm_map);
	free(pdm, M_DEVBUF, sizeof(*pdm));
}
