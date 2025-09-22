/*	$OpenBSD: acpipci.c,v 1.11 2025/09/16 12:18:10 hshoexer Exp $	*/
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

/* 33DB4D5B-1FF7-401C-9657-7441C03DD766 */
#define ACPI_PCI_UUID \
  { 0x5b, 0x4d, 0xdb, 0x33, \
    0xf7, 0x1f, \
    0x1c, 0x40, \
    0x96, 0x57, \
    0x74, 0x41, 0xc0, 0x3d, 0xd7, 0x66 }

/* Support field. */
#define ACPI_PCI_PCIE_CONFIG	0x00000001
#define ACPI_PCI_ASPM		0x00000002
#define ACPI_PCI_CPMC		0x00000004
#define ACPI_PCI_SEGMENTS	0x00000008
#define ACPI_PCI_MSI		0x00000010

/* Control field. */
#define ACPI_PCI_PCIE_HOTPLUG	0x00000001

struct acpipci_softc {
	struct device	sc_dev;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t	sc_iot;
	bus_space_tag_t	sc_memt;
	bus_dma_tag_t	sc_dmat;

	struct extent	*sc_busex;
	struct extent	*sc_memex;
	struct extent	*sc_ioex;
	char		sc_busex_name[32];
	char		sc_ioex_name[32];
	char		sc_memex_name[32];
	int		sc_bus;
	uint32_t	sc_seg;
};

int	acpipci_match(struct device *, void *, void *);
void	acpipci_attach(struct device *, struct device *, void *);

const struct cfattach acpipci_ca = {
	sizeof(struct acpipci_softc), acpipci_match, acpipci_attach
};

struct cfdriver acpipci_cd = {
	NULL, "acpipci", DV_DULL, CD_COCOVM
};

const char *acpipci_hids[] = {
	"PNP0A08",
	"PNP0A03",
	NULL
};

int	acpipci_print(void *, const char *);
int	acpipci_parse_resources(int, union acpi_resource *, void *);
void	acpipci_osc(struct acpipci_softc *);

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
	struct aml_value res;
	uint64_t bbn = 0;
	uint64_t seg = 0;

	acpi_haspci = 1;

	sc->sc_iot = aaa->aaa_iot;
	sc->sc_memt = aaa->aaa_memt;
	sc->sc_dmat = aaa->aaa_dmat;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	acpipci_osc(sc);

	aml_evalinteger(sc->sc_acpi, sc->sc_node, "_BBN", 0, NULL, &bbn);
	sc->sc_bus = bbn;

	aml_evalinteger(sc->sc_acpi, sc->sc_node, "_SEG", 0, NULL, &seg);
	sc->sc_seg = seg;

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_CRS", 0, NULL, &res)) {
		printf(": can't find resources\n");

		pci_init_extents();
		sc->sc_busex = pcibus_ex;
		sc->sc_ioex = pciio_ex;
		sc->sc_memex = pcimem_ex;

		return;
	}

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

	if (sc->sc_acpi->sc_major < 5 && (cpu_ecxfeature & CPUIDECX_HV) == 0) {
		extent_destroy(sc->sc_ioex);
		extent_destroy(sc->sc_memex);

		pci_init_extents();
		sc->sc_ioex =  pciio_ex;
		sc->sc_memex = pcimem_ex;
	}

	printf("\n");

#ifdef ACPIPCI_DEBUG
	extent_print(sc->sc_busex);
	extent_print(sc->sc_ioex);
	extent_print(sc->sc_memex);
#endif
}

void
acpipci_attach_bus(struct device *parent, struct acpipci_softc *sc)
{
	struct pcibus_attach_args pba;
	pcitag_t tag;
	pcireg_t id, class;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = sc->sc_iot;
	pba.pba_memt = sc->sc_memt;
	pba.pba_dmat = sc->sc_dmat;
	pba.pba_busex = sc->sc_busex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pmemex = sc->sc_memex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;

	/* Enable MSI in ACPI 2.0 and above, unless we're told not to. */
	if (sc->sc_acpi->sc_fadt->hdr.revision >= 2 &&
	    (sc->sc_acpi->sc_fadt->iapc_boot_arch & FADT_NO_MSI) == 0)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	/* Enable MSI for QEMU claiming ACPI 1.0 */
	tag = pci_make_tag(pba.pba_pc, sc->sc_bus, 0, 0);
	id = pci_conf_read(pba.pba_pc, tag, PCI_SUBSYS_ID_REG);
	if (sc->sc_acpi->sc_fadt->hdr.revision == 1 &&
	    PCI_VENDOR(id) == PCI_VENDOR_QUMRANET)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	/*
	 * Don't enable MSI on chipsets from low-end manufacturers
	 * like VIA and SiS.  We do this by looking at the host
	 * bridge, which should be device 0 function 0.
	 */
	id = pci_conf_read(pba.pba_pc, tag, PCI_ID_REG);
	class = pci_conf_read(pba.pba_pc, tag, PCI_CLASS_REG);
	if (PCI_CLASS(class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(class) != PCI_SUBCLASS_BRIDGE_HOST &&
	    PCI_VENDOR(id) != PCI_VENDOR_AMD &&
	    PCI_VENDOR(id) != PCI_VENDOR_NVIDIA &&
	    PCI_VENDOR(id) != PCI_VENDOR_INTEL)
		pba.pba_flags &= ~PCI_FLAGS_MSI_ENABLED;

	/*
	 * Don't enable MSI on a HyperTransport bus.  In order to
	 * determine that a bus is a HyperTransport bus, we look at
	 * device 24 function 0, which is the HyperTransport
	 * host/primary interface integrated on most 64-bit AMD CPUs.
	 * If that device has a HyperTransport capability, this must
	 * be a HyperTransport bus and we disable MSI.
	 */
	tag = pci_make_tag(pba.pba_pc, sc->sc_bus, 24, 0);
	if (pci_get_capability(pba.pba_pc, tag, PCI_CAP_HT, NULL, NULL))
		pba.pba_flags &= ~PCI_FLAGS_MSI_ENABLED;

	config_found(parent, &pba, acpipci_print);
}

void
acpipci_attach_busses(struct device *parent)
{
	int i;

	for (i = 0; i < acpipci_cd.cd_ndevs; i++) {
		if (acpipci_cd.cd_devs[i])
			acpipci_attach_bus(parent, acpipci_cd.cd_devs[i]);
	}
}

int
acpipci_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

int
acpipci_parse_resources(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpipci_softc *sc = arg;
	int type = AML_CRSTYPE(crs);
	int restype, tflags = 0;
	u_long min, len = 0, tra = 0;

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
		/*
		 * Coreboot on the PC Engines apu2 incorrectly uses a
		 * Memory32Fixed resource descriptor to describe mmio
		 * address space forwarded to the PCI bus.
		 */
		restype = LR_TYPE_MEMORY;
		min = crs->lr_m32fixed._bas;
		len = crs->lr_m32fixed._len;
		break;
	}

	if (len == 0)
		return 0;

	switch (restype) {
	case LR_TYPE_MEMORY:
		if (tflags & LR_MEMORY_TTP)
			return 0;
		extent_free(sc->sc_memex, min, len, EX_WAITOK | EX_CONFLICTOK);
		break;
	case LR_TYPE_IO:
		if (tflags & LR_IO_TTP)
			return 0;
		extent_free(sc->sc_ioex, min, len, EX_WAITOK | EX_CONFLICTOK);
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
acpipci_osc(struct acpipci_softc *sc)
{
	struct aml_value args[4];
	struct aml_value res;
	static uint8_t uuid[16] = ACPI_PCI_UUID;
	uint32_t buf[3];
	
	memset(args, 0, sizeof(args));
	args[0].type = AML_OBJTYPE_BUFFER;
	args[0].v_buffer = uuid;
	args[0].length = sizeof(uuid);
	args[1].type = AML_OBJTYPE_INTEGER;
	args[1].v_integer = 1;
	args[2].type = AML_OBJTYPE_INTEGER;
	args[2].v_integer = 3;
	args[3].type = AML_OBJTYPE_BUFFER;
	args[3].v_buffer = (uint8_t *)buf;
	args[3].length = sizeof(buf);

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x0;
	buf[1] = ACPI_PCI_PCIE_CONFIG | ACPI_PCI_MSI;
	buf[2] = ACPI_PCI_PCIE_HOTPLUG;

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_OSC", 4, args, &res))
		return;

	if (res.type == AML_OBJTYPE_BUFFER) {
		size_t len = res.length;
		uint32_t *p = (uint32_t *)res.v_buffer;

		printf(":");
		while (len >= 4) {
			printf(" 0x%08x", *p);
			p++;
			len -= 4;
		}
	}
}
