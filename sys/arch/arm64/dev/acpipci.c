/*	$OpenBSD: acpipci.c,v 1.43 2025/01/23 11:24:34 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis
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
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <arm64/dev/acpiiort.h>

struct acpipci_mcfg {
	SLIST_ENTRY(acpipci_mcfg) am_list;

	uint16_t	am_segment;
	uint8_t		am_min_bus;
	uint8_t		am_max_bus;

	bus_space_tag_t	am_iot;
	bus_space_handle_t am_ioh;

	struct machine_pci_chipset am_pc;
};

struct acpipci_trans {
	struct acpipci_trans *at_next;
	bus_space_tag_t	at_iot;
	bus_addr_t	at_base;
	bus_size_t	at_size;
	bus_size_t	at_offset;
};

struct acpipci_softc {
	struct device	sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	bus_space_tag_t	sc_iot;
	pci_chipset_tag_t sc_pc;

	struct bus_space sc_bus_iot;
	struct bus_space sc_bus_memt;
	struct acpipci_trans *sc_io_trans;
	struct acpipci_trans *sc_mem_trans;

	struct extent	*sc_busex;
	struct extent	*sc_memex;
	struct extent	*sc_ioex;
	char		sc_busex_name[32];
	char		sc_ioex_name[32];
	char		sc_memex_name[32];
	int		sc_bus;
	uint32_t	sc_seg;

	struct interrupt_controller *sc_msi_ic;
};

struct acpipci_intr_handle {
	struct machine_intr_handle aih_ih;
	bus_dma_tag_t		aih_dmat;
	bus_dmamap_t		aih_map;
};

int	acpipci_match(struct device *, void *, void *);
void	acpipci_attach(struct device *, struct device *, void *);

const struct cfattach acpipci_ca = {
	sizeof(struct acpipci_softc), acpipci_match, acpipci_attach
};

struct cfdriver acpipci_cd = {
	NULL, "acpipci", DV_DULL
};

const char *acpipci_hids[] = {
	"PNP0A08",
	NULL
};

int	acpipci_parse_resources(int, union acpi_resource *, void *);
int	acpipci_bs_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
paddr_t acpipci_bs_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);

void	acpipci_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	acpipci_bus_maxdevs(void *, int);
pcitag_t acpipci_make_tag(void *, int, int, int);
void	acpipci_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	acpipci_conf_size(void *, pcitag_t);
pcireg_t acpipci_conf_read(void *, pcitag_t, int);
void	acpipci_conf_write(void *, pcitag_t, int, pcireg_t);
int	acpipci_probe_device_hook(void *, struct pci_attach_args *);

int	acpipci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *acpipci_intr_string(void *, pci_intr_handle_t);
void	*acpipci_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	acpipci_intr_disestablish(void *, void *);

uint32_t acpipci_iort_map_msi(pci_chipset_tag_t, pcitag_t,
	    struct interrupt_controller **);
	
extern LIST_HEAD(, interrupt_controller) interrupt_controllers;

int
acpipci_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpipci_hids, cf->cf_driver->cd_name);
}

void
acpipci_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpipci_softc *sc = (struct acpipci_softc *)self;
	struct interrupt_controller *ic;
	struct pcibus_attach_args pba;
	struct aml_value res;
	uint64_t bbn = 0;
	uint64_t seg = 0;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_CRS", 0, NULL, &res)) {
		printf(": can't find resources\n");
		return;
	}

	aml_evalinteger(sc->sc_acpi, sc->sc_node, "_BBN", 0, NULL, &bbn);
	sc->sc_bus = bbn;

	aml_evalinteger(sc->sc_acpi, sc->sc_node, "_SEG", 0, NULL, &seg);
	sc->sc_seg = seg;

	sc->sc_iot = aaa->aaa_memt;
	
	printf("\n");

	/* Create extents for our address spaces. */
	snprintf(sc->sc_busex_name, sizeof(sc->sc_busex_name),
	    "%s pcibus", sc->sc_dev.dv_xname);
	snprintf(sc->sc_ioex_name, sizeof(sc->sc_ioex_name),
	    "%s pciio", sc->sc_dev.dv_xname);
	snprintf(sc->sc_memex_name, sizeof(sc->sc_memex_name),
	    "%s pcimem", sc->sc_dev.dv_xname);
	sc->sc_busex = extent_create(sc->sc_busex_name, 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_ioex = extent_create(sc->sc_ioex_name, 0, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create(sc->sc_memex_name, 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);

	aml_parse_resource(&res, acpipci_parse_resources, sc);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc->sc_io_trans;
	sc->sc_bus_iot._space_map = acpipci_bs_map;
	sc->sc_bus_iot._space_mmap = acpipci_bs_mmap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc->sc_mem_trans;
	sc->sc_bus_memt._space_map = acpipci_bs_map;
	sc->sc_bus_memt._space_mmap = acpipci_bs_mmap;

	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_establish_msi)
			break;
	}
	sc->sc_msi_ic = ic;

	sc->sc_pc = pci_lookup_segment(sc->sc_seg, sc->sc_bus);
	KASSERT(sc->sc_pc->pc_intr_v == NULL);

	sc->sc_pc->pc_probe_device_hook = acpipci_probe_device_hook;

	sc->sc_pc->pc_intr_v = sc;
	sc->sc_pc->pc_intr_map = acpipci_intr_map;
	sc->sc_pc->pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc->pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc->pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc->pc_intr_string = acpipci_intr_string;
	sc->sc_pc->pc_intr_establish = acpipci_intr_establish;
	sc->sc_pc->pc_intr_disestablish = acpipci_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = aaa->aaa_dmat;
	pba.pba_pc = sc->sc_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pmemex = sc->sc_memex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
	if (sc->sc_msi_ic)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

int
acpipci_parse_resources(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpipci_softc *sc = arg;
	struct acpipci_trans *at;
	int type = AML_CRSTYPE(crs);
	int restype, tflags;
	u_long min, len = 0, tra;

	switch (type) {
	case LR_WORD:
		restype = crs->lr_word.type;
		tflags = crs->lr_word.tflags;
		min = crs->lr_word._min;
		len = crs->lr_word._len;
		tra = crs->lr_word._tra;
		break;
	case LR_DWORD:
		restype = crs->lr_dword.type;
		tflags = crs->lr_dword.tflags;
		min = crs->lr_dword._min;
		len = crs->lr_dword._len;
		tra = crs->lr_dword._tra;
		break;
	case LR_QWORD:
		restype = crs->lr_qword.type;
		tflags = crs->lr_qword.tflags;
		min = crs->lr_qword._min;
		len = crs->lr_qword._len;
		tra = crs->lr_qword._tra;
		break;
	case LR_MEM32FIXED:
		restype = LR_TYPE_MEMORY;
		tflags = 0;
		min = crs->lr_m32fixed._bas;
		len = crs->lr_m32fixed._len;
		tra = 0;
		break;
	}

	if (len == 0)
		return 0;

	switch (restype) {
	case LR_TYPE_MEMORY:
		if (tflags & LR_MEMORY_TTP)
			return 0;
		extent_free(sc->sc_memex, min, len, EX_WAITOK);
		at = malloc(sizeof(struct acpipci_trans), M_DEVBUF, M_WAITOK);
		at->at_iot = sc->sc_iot;
		at->at_base = min;
		at->at_size = len;
		at->at_offset = tra;
		at->at_next = sc->sc_mem_trans;
		sc->sc_mem_trans = at;
		break;
	case LR_TYPE_IO:
		/*
		 * Don't check _TTP as various firmwares don't set it,
		 * even though they should!!
		 */
		extent_free(sc->sc_ioex, min, len, EX_WAITOK);
		at = malloc(sizeof(struct acpipci_trans), M_DEVBUF, M_WAITOK);
		at->at_iot = sc->sc_iot;
		at->at_base = min;
		at->at_size = len;
		at->at_offset = tra;
		at->at_next = sc->sc_io_trans;
		sc->sc_io_trans = at;
		break;
	case LR_TYPE_BUS:
		extent_free(sc->sc_busex, min, len, EX_WAITOK);
		/*
		 * Let _CRS minimum bus number override _BBN.
		 */
		sc->sc_bus = min;
		break;
	}

	return 0;
}

void
acpipci_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
acpipci_bus_maxdevs(void *v, int bus)
{
	return 32;
}

pcitag_t
acpipci_make_tag(void *v, int bus, int device, int function)
{
	return ((bus << 20) | (device << 15) | (function << 12));
}

void
acpipci_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
acpipci_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
acpipci_conf_read(void *v, pcitag_t tag, int reg)
{
	struct acpipci_mcfg *am = v;

	if (tag < (am->am_min_bus << 20) ||
	    tag >= ((am->am_max_bus + 1) << 20))
		return 0xffffffff;

	return bus_space_read_4(am->am_iot, am->am_ioh, tag | reg);
}

void
acpipci_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct acpipci_mcfg *am = v;

	if (tag < (am->am_min_bus << 20) ||
	    tag >= ((am->am_max_bus + 1) << 20))
		return;

	bus_space_write_4(am->am_iot, am->am_ioh, tag | reg, data);
}

int
acpipci_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	struct acpipci_mcfg *am = v;
	struct acpipci_trans *at;
	struct acpi_table_header *hdr;
	struct acpi_iort *iort = NULL;
	struct acpi_iort_node *node;
	struct acpi_iort_mapping *map;
	struct acpi_iort_rc_node *rc;
	struct acpi_q *entry;
	uint32_t rid, offset;
	int i;

	rid = pci_requester_id(pa->pa_pc, pa->pa_tag);

	/* Look for IORT table. */
	SIMPLEQ_FOREACH(entry, &acpi_softc->sc_tables, q_next) {
		hdr = entry->q_table;
		if (strncmp(hdr->signature, IORT_SIG,
		    sizeof(hdr->signature)) == 0) {
			iort = entry->q_table;
			break;
		}
	}
	if (iort == NULL)
		return 0;

	/* Find our root complex. */
	offset = iort->offset;
	for (i = 0; i < iort->number_of_nodes; i++) {
		node = (struct acpi_iort_node *)((char *)iort + offset);
		if (node->type == ACPI_IORT_ROOT_COMPLEX) {
			rc = (struct acpi_iort_rc_node *)&node[1];
			if (rc->segment == am->am_segment)
				break;
		}
		offset += node->length;
	}

	/* No RC found? Weird. */
	if (i >= iort->number_of_nodes)
		return 0;

	/* Find our output base towards SMMU. */
	map = (struct acpi_iort_mapping *)((char *)node + node->mapping_offset);
	for (i = 0; i < node->number_of_mappings; i++) {
		offset = map[i].output_reference;

		if (map[i].flags & ACPI_IORT_MAPPING_SINGLE) {
			rid = map[i].output_base;
			break;
		}

		/* Mapping encodes number of IDs in the range minus one. */
		if (map[i].input_base <= rid &&
		    rid <= map[i].input_base + map[i].number_of_ids) {
			rid = map[i].output_base + (rid - map[i].input_base);
			break;
		}
	}

	/* No mapping found? Even weirder. */
	if (i >= node->number_of_mappings)
		return 0;

	node = (struct acpi_iort_node *)((char *)iort + offset);
	if (node->type == ACPI_IORT_SMMU || node->type == ACPI_IORT_SMMU_V3) {
		pa->pa_dmat = acpiiort_smmu_map(node, rid, pa->pa_dmat);
		for (at = pa->pa_iot->bus_private; at; at = at->at_next) {
			acpiiort_smmu_reserve_region(node, rid,
			    at->at_base, at->at_size);
		}
		for (at = pa->pa_memt->bus_private; at; at = at->at_next) {
			acpiiort_smmu_reserve_region(node, rid,
			    at->at_base, at->at_size);
		}
	}

	return 0;
}

int
acpipci_intr_swizzle(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int dev, swizpin;
	pcireg_t id;

	if (pa->pa_bridgeih == NULL)
		return -1;

	pci_decompose_tag(pa->pa_pc, pa->pa_tag, NULL, &dev, NULL);
	swizpin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin, dev);

	/*
	 * Qualcomm SC8280XP Root Complex violates PCI bridge
	 * interrupt swizzling rules.
	 */
	if (pa->pa_bridgetag) {
		id = pci_conf_read(pa->pa_pc, *pa->pa_bridgetag, PCI_ID_REG);
		if (PCI_VENDOR(id) == PCI_VENDOR_QUALCOMM &&
		    PCI_PRODUCT(id) == PCI_PRODUCT_QUALCOMM_SC8280XP_PCIE) {
			swizpin = (((swizpin - 1) + 3) % 4) + 1;
		}
	}

	if (pa->pa_bridgeih[swizpin - 1].ih_type == PCI_NONE)
		return -1;

	*ihp = pa->pa_bridgeih[swizpin - 1];
	return 0;
}

int
acpipci_getirq(int crsidx, union acpi_resource *crs, void *arg)
{
	int *irq = arg;

	switch (AML_CRSTYPE(crs)) {
	case SR_IRQ:
		*irq = ffs(letoh16(crs->sr_irq.irq_mask)) - 1;
		break;
	case LR_EXTIRQ:
		*irq = letoh32(crs->lr_extirq.irq[0]);
		break;
	default:
		break;
	}

	return 0;
}

int
acpipci_intr_link(struct acpipci_softc *sc, struct aml_node *node,
    struct aml_value *val)
{
	struct aml_value res;
	int64_t sta;
	int irq = -1;

	if (val->type == AML_OBJTYPE_NAMEREF) {
		node = aml_searchrel(node, aml_getname(val->v_nameref));
		if (node)
			val = node->value;
	}
	if (val->type == AML_OBJTYPE_OBJREF)
		val = val->v_objref.ref;
	if (val->type != AML_OBJTYPE_DEVICE)
		return -1;

	sta = acpi_getsta(sc->sc_acpi, val->node);
	if ((sta & STA_PRESENT) == 0)
		return -1;

	if (aml_evalname(sc->sc_acpi, val->node, "_CRS", 0, NULL, &res))
		return -1;
	aml_parse_resource(&res, acpipci_getirq, &irq);
	aml_freevalue(&res);

	return irq;
}

int
acpipci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct acpipci_softc *sc = pa->pa_pc->pc_intr_v;
	struct aml_node *node = sc->sc_node;
	struct aml_value res;
	uint64_t addr, pin, source, index;
	int i;

	/*
	 * If we're behind a bridge, we need to look for a _PRT for
	 * it.  If we don't find a _PRT, we need to swizzle.  If we're
	 * not behind a bridge we need to look for a _PRT on the host
	 * bridge node itself.
	 */
	if (pa->pa_bridgetag) {
		node = acpi_find_pci(pa->pa_pc, *pa->pa_bridgetag);
		if (node == NULL)
			return acpipci_intr_swizzle(pa, ihp);
	}

	if (aml_evalname(sc->sc_acpi, node, "_PRT", 0, NULL, &res))
		return acpipci_intr_swizzle(pa, ihp);

	if (res.type != AML_OBJTYPE_PACKAGE)
		return -1;

	for (i = 0; i < res.length; i++) {
		struct aml_value *val = res.v_package[i];

		if (val->type != AML_OBJTYPE_PACKAGE)
			continue;
		if (val->length != 4)
			continue;
		if (val->v_package[0]->type != AML_OBJTYPE_INTEGER ||
		    val->v_package[1]->type != AML_OBJTYPE_INTEGER ||
		    val->v_package[3]->type != AML_OBJTYPE_INTEGER)
			continue;

		addr = val->v_package[0]->v_integer;
		pin = val->v_package[1]->v_integer;
		if (ACPI_ADR_PCIDEV(addr) != pa->pa_device ||
		    ACPI_ADR_PCIFUN(addr) != 0xffff ||
		    pin != pa->pa_intrpin - 1)
			continue;

		if (val->v_package[2]->type == AML_OBJTYPE_INTEGER) {
			source = val->v_package[2]->v_integer;
			index = val->v_package[3]->v_integer;
		} else {
			source = 0;
			index = acpipci_intr_link(sc, node, val->v_package[2]);
		}
		if (source != 0 || index == -1)
			continue;

		ihp->ih_pc = pa->pa_pc;
		ihp->ih_tag = pa->pa_tag;
		ihp->ih_intrpin = index;
		ihp->ih_type = PCI_INTX;

		return 0;
	}

	return -1;
}

const char *
acpipci_intr_string(void *v, pci_intr_handle_t ih)
{
	static char irqstr[32];

	switch (ih.ih_type) {
	case PCI_MSI:
		return "msi";
	case PCI_MSIX:
		return "msix";
	}

	snprintf(irqstr, sizeof(irqstr), "irq %d", ih.ih_intrpin);
	return irqstr;
}

void *
acpipci_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct acpipci_softc *sc = v;
	struct acpipci_intr_handle *aih;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		struct interrupt_controller *ic = sc->sc_msi_ic;
		bus_dma_segment_t seg;
		uint64_t addr = 0, data;

		KASSERT(ic);

		/* Map Requester ID through IORT to get sideband data. */
		data = acpipci_iort_map_msi(ih.ih_pc, ih.ih_tag, &ic);
		cookie = ic->ic_establish_msi(ic->ic_cookie, &addr,
		    &data, level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		aih = malloc(sizeof(*aih), M_DEVBUF, M_WAITOK);
		aih->aih_ih.ih_ic = ic;
		aih->aih_ih.ih_ih = cookie;
		aih->aih_dmat = ih.ih_dmat;

		if (bus_dmamap_create(aih->aih_dmat, sizeof(uint32_t), 1,
		    sizeof(uint32_t), 0, BUS_DMA_WAITOK, &aih->aih_map)) {
			free(aih, M_DEVBUF, sizeof(*aih));
			ic->ic_disestablish(cookie);
			return NULL;
		}

		memset(&seg, 0, sizeof(seg));
		seg.ds_addr = addr;
		seg.ds_len = sizeof(uint32_t);

		if (bus_dmamap_load_raw(aih->aih_dmat, aih->aih_map,
		    &seg, 1, sizeof(uint32_t), BUS_DMA_WAITOK)) {
			bus_dmamap_destroy(aih->aih_dmat, aih->aih_map);
			free(aih, M_DEVBUF, sizeof(*aih));
			ic->ic_disestablish(cookie);
			return NULL;
		}

		addr = aih->aih_map->dm_segs[0].ds_addr;
		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);

		cookie = aih;
	} else {
		if (ci != NULL && !CPU_IS_PRIMARY(ci))
			return NULL;
		cookie = acpi_intr_establish(ih.ih_intrpin, 0, level,
		    func, arg, name);
	}

	return cookie;
}

void
acpipci_intr_disestablish(void *v, void *cookie)
{
	struct acpipci_intr_handle *aih = cookie;
	struct interrupt_controller *ic = aih->aih_ih.ih_ic;

	if (ic->ic_establish_msi) {
		ic->ic_disestablish(aih->aih_ih.ih_ih);
		bus_dmamap_unload(aih->aih_dmat, aih->aih_map);
		bus_dmamap_destroy(aih->aih_dmat, aih->aih_map);
		free(aih, M_DEVBUF, sizeof(*aih));
	} else
		acpi_intr_disestablish(cookie);
}

/*
 * Translate memory address if needed.
 */
int
acpipci_bs_map(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct acpipci_trans *at;

	for (at = t->bus_private; at; at = at->at_next) {
		if (addr >= at->at_base && addr < at->at_base + at->at_size) {
			return bus_space_map(at->at_iot,
			    addr + at->at_offset, size, flags, bshp);
		}
	}
	
	return ENXIO;
}

paddr_t
acpipci_bs_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off,
    int prot, int flags)
{
	struct acpipci_trans *at;

	for (at = t->bus_private; at; at = at->at_next) {
		if (addr >= at->at_base && addr < at->at_base + at->at_size) {
			return bus_space_mmap(at->at_iot,
			    addr + at->at_offset, off, prot, flags);
		}
	}

	return -1;
}

SLIST_HEAD(,acpipci_mcfg) acpipci_mcfgs =
    SLIST_HEAD_INITIALIZER(acpipci_mcfgs);

void
pci_mcfg_init(bus_space_tag_t iot, bus_addr_t addr, int segment,
    int min_bus, int max_bus)
{
	struct acpipci_mcfg *am;

	am = malloc(sizeof(struct acpipci_mcfg), M_DEVBUF, M_WAITOK | M_ZERO);
	am->am_segment = segment;
	am->am_min_bus = min_bus;
	am->am_max_bus = max_bus;

	am->am_iot = iot;
	if (bus_space_map(iot, addr, (max_bus + 1) << 20, 0, &am->am_ioh))
		panic("%s: can't map config space", __func__);

	am->am_pc.pc_conf_v = am;
	am->am_pc.pc_attach_hook = acpipci_attach_hook;
	am->am_pc.pc_bus_maxdevs = acpipci_bus_maxdevs;
	am->am_pc.pc_make_tag = acpipci_make_tag;
	am->am_pc.pc_decompose_tag = acpipci_decompose_tag;
	am->am_pc.pc_conf_size = acpipci_conf_size;
	am->am_pc.pc_conf_read = acpipci_conf_read;
	am->am_pc.pc_conf_write = acpipci_conf_write;
	SLIST_INSERT_HEAD(&acpipci_mcfgs, am, am_list);
}

pcireg_t
acpipci_dummy_conf_read(void *v, pcitag_t tag, int reg)
{
	return 0xffffffff;
}

void
acpipci_dummy_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
}

struct machine_pci_chipset acpipci_dummy_chipset = {
	.pc_attach_hook = acpipci_attach_hook,
	.pc_bus_maxdevs = acpipci_bus_maxdevs,
	.pc_make_tag = acpipci_make_tag,
	.pc_decompose_tag = acpipci_decompose_tag,
	.pc_conf_size = acpipci_conf_size,
	.pc_conf_read = acpipci_dummy_conf_read,
	.pc_conf_write = acpipci_dummy_conf_write,
};

pci_chipset_tag_t
pci_lookup_segment(int segment, int bus)
{
	struct acpipci_mcfg *am;

	SLIST_FOREACH(am, &acpipci_mcfgs, am_list) {
		if (segment == am->am_segment &&
		    bus >= am->am_min_bus && bus <= am->am_max_bus)
			return &am->am_pc;
	}

	return &acpipci_dummy_chipset;
}

/*
 * IORT support.
 */

uint32_t acpipci_iort_map(struct acpi_iort *, uint32_t, uint32_t,
    struct interrupt_controller **);

uint32_t
acpipci_iort_map_node(struct acpi_iort *iort,
    struct acpi_iort_node *node, uint32_t id, struct interrupt_controller **ic)
{
	struct acpi_iort_mapping *map =
	    (struct acpi_iort_mapping *)((char *)node + node->mapping_offset);
	int i;

	for (i = 0; i < node->number_of_mappings; i++) {
		uint32_t offset = map[i].output_reference;

		if (map[i].flags & ACPI_IORT_MAPPING_SINGLE) {
			id = map[i].output_base;
			return acpipci_iort_map(iort, offset, id, ic);
		}

		/* Mapping encodes number of IDs in the range minus one. */
		if (map[i].input_base <= id &&
		    id <= map[i].input_base + map[i].number_of_ids) {
			id = map[i].output_base + (id - map[i].input_base);
			return acpipci_iort_map(iort, offset, id, ic);
		}
	}

	return id;
}

uint32_t
acpipci_iort_map(struct acpi_iort *iort, uint32_t offset, uint32_t id,
    struct interrupt_controller **ic)
{
	struct acpi_iort_node *node =
	    (struct acpi_iort_node *)((char *)iort + offset);
	struct interrupt_controller *icl;
	struct acpi_iort_its_node *itsn;
	int i;

	switch (node->type) {
	case ACPI_IORT_ITS:
		itsn = (struct acpi_iort_its_node *)&node[1];
		LIST_FOREACH(icl, &interrupt_controllers, ic_list) {
			for (i = 0; i < itsn->number_of_itss; i++) {
				if (icl->ic_establish_msi != NULL &&
				    icl->ic_gic_its_id == itsn->its_ids[i]) {
					*ic = icl;
					break;
				}
			}
		}

		return id;
	case ACPI_IORT_SMMU:
	case ACPI_IORT_SMMU_V3:
		return acpipci_iort_map_node(iort, node, id, ic);
	}

	return id;
}

uint32_t
acpipci_iort_map_msi(pci_chipset_tag_t pc, pcitag_t tag,
    struct interrupt_controller **ic)
{
	struct acpipci_softc *sc = pc->pc_intr_v;
	struct acpi_table_header *hdr;
	struct acpi_iort *iort = NULL;
	struct acpi_iort_node *node;
	struct acpi_iort_rc_node *rc;
	struct acpi_q *entry;
	uint32_t rid, offset;
	int i;

	rid = pci_requester_id(pc, tag);

	/* Look for IORT table. */
	SIMPLEQ_FOREACH(entry, &sc->sc_acpi->sc_tables, q_next) {
		hdr = entry->q_table;
		if (strncmp(hdr->signature, IORT_SIG,
		    sizeof(hdr->signature)) == 0) {
			iort = entry->q_table;
			break;
		}
	}
	if (iort == NULL)
		return rid;

	/* Find our root complex and map. */
	offset = iort->offset;
	for (i = 0; i < iort->number_of_nodes; i++) {
		node = (struct acpi_iort_node *)((char *)iort + offset);
		switch (node->type) {
		case ACPI_IORT_ROOT_COMPLEX:
			rc = (struct acpi_iort_rc_node *)&node[1];
			if (rc->segment == sc->sc_seg)
				return acpipci_iort_map_node(iort, node, rid,
				    ic);
			break;
		}
		offset += node->length;
	}

	return rid;
}
