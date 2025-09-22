/* $OpenBSD: acpi.c,v 1.454 2025/09/20 17:43:28 kettenis Exp $ */
/*
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/fcntl.h>
#include <sys/event.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/reboot.h>
#include <sys/sched.h>

#include <machine/conf.h>
#include <machine/cpufunc.h>

#include <dev/pci/pcivar.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>
#include <dev/pci/pciidevar.h>

#include <machine/apmvar.h>

#include "wd.h"

extern int cpu_suspended;

#ifdef ACPI_DEBUG
int	acpi_debug = 16;
#endif

int	acpi_poll_enabled;
int	acpi_hasprocfvs;
int	acpi_haspci;
int	acpi_legacy_free;

struct pool acpiwqpool;

#define ACPIEN_RETRIES 15

struct aml_node *acpi_pci_match(struct device *, struct pci_attach_args *);
pcireg_t acpi_pci_min_powerstate(pci_chipset_tag_t, pcitag_t);
void	 acpi_pci_set_powerstate(pci_chipset_tag_t, pcitag_t, int, int);
int	acpi_pci_notify(struct aml_node *, int, void *);

int	acpi_submatch(struct device *, void *, void *);
int	acpi_noprint(void *, const char *);
int	acpi_print(void *, const char *);

void	acpi_map_pmregs(struct acpi_softc *);
void	acpi_unmap_pmregs(struct acpi_softc *);

int	acpi_loadtables(struct acpi_softc *, struct acpi_rsdp *);

int	_acpi_matchhids(const char *, const char *[]);

int	acpi_inidev(struct aml_node *, void *);
int	acpi_foundprt(struct aml_node *, void *);

int	acpi_enable(struct acpi_softc *);
void	acpi_init_states(struct acpi_softc *);

void 	acpi_gpe_task(void *, int);
void	acpi_sbtn_task(void *, int);
void	acpi_pbtn_task(void *, int);

int	acpi_enabled;

void	acpi_init_gpes(struct acpi_softc *);
void	acpi_disable_allgpes(struct acpi_softc *);
struct gpe_block *acpi_find_gpe(struct acpi_softc *, int);
void	acpi_enable_onegpe(struct acpi_softc *, int);
int	acpi_gpe(struct acpi_softc *, int, void *);

void	acpi_enable_rungpes(struct acpi_softc *);

#ifdef __arm64__
int	acpi_foundsectwo(struct aml_node *, void *);
#endif
int	acpi_foundec(struct aml_node *, void *);
int	acpi_foundsony(struct aml_node *node, void *arg);
int	acpi_foundhid(struct aml_node *, void *);
int	acpi_add_device(struct aml_node *node, void *arg);

void	acpi_thread(void *);
void	acpi_create_thread(void *);

#ifndef SMALL_KERNEL

void	acpi_init_pm(struct acpi_softc *);

int	acpi_founddock(struct aml_node *, void *);
int	acpi_foundprw(struct aml_node *, void *);
int	acpi_foundvideo(struct aml_node *, void *);
int	acpi_foundsbs(struct aml_node *node, void *);

int	acpi_foundide(struct aml_node *node, void *arg);
int	acpiide_notify(struct aml_node *, int, void *);
void	wdcattach(struct channel_softc *);
int	wdcdetach(struct channel_softc *, int);
int	is_ejectable_bay(struct aml_node *node);
int	is_ata(struct aml_node *node);
int	is_ejectable(struct aml_node *node);

struct idechnl {
	struct acpi_softc *sc;
	int64_t		addr;
	int64_t		chnl;
	int64_t		sta;
};

/*
 * This is a list of Synaptics devices with a 'top button area'
 * based on the list in Linux supplied by Synaptics
 * Synaptics clickpads with the following pnp ids will get a unique
 * wscons mouse type that is used to define trackpad regions that will
 * emulate mouse buttons
 */
static const char *sbtn_pnp[] = {
	"LEN0017",
	"LEN0018",
	"LEN0019",
	"LEN0023",
	"LEN002A",
	"LEN002B",
	"LEN002C",
	"LEN002D",
	"LEN002E",
	"LEN0033",
	"LEN0034",
	"LEN0035",
	"LEN0036",
	"LEN0037",
	"LEN0038",
	"LEN0039",
	"LEN0041",
	"LEN0042",
	"LEN0045",
	"LEN0047",
	"LEN0049",
	"LEN2000",
	"LEN2001",
	"LEN2002",
	"LEN2003",
	"LEN2004",
	"LEN2005",
	"LEN2006",
	"LEN2007",
	"LEN2008",
	"LEN2009",
	"LEN200A",
	"LEN200B",
};

int	mouse_has_softbtn;
#endif /* SMALL_KERNEL */

struct acpi_softc *acpi_softc;

extern struct aml_node aml_root;

struct cfdriver acpi_cd = {
	NULL, "acpi", DV_DULL, CD_COCOVM
};

uint8_t
acpi_pci_conf_read_1(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	uint32_t val = pci_conf_read(pc, tag, reg & ~0x3);
	return (val >> ((reg & 0x3) << 3));
}

uint16_t
acpi_pci_conf_read_2(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	uint32_t val = pci_conf_read(pc, tag, reg & ~0x2);
	return (val >> ((reg & 0x2) << 3));
}

uint32_t
acpi_pci_conf_read_4(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	return pci_conf_read(pc, tag, reg);
}

void
acpi_pci_conf_write_1(pci_chipset_tag_t pc, pcitag_t tag, int reg, uint8_t val)
{
	uint32_t tmp = pci_conf_read(pc, tag, reg & ~0x3);
	tmp &= ~(0xff << ((reg & 0x3) << 3));
	tmp |= (val << ((reg & 0x3) << 3));
	pci_conf_write(pc, tag, reg & ~0x3, tmp);
}

void
acpi_pci_conf_write_2(pci_chipset_tag_t pc, pcitag_t tag, int reg, uint16_t val)
{
	uint32_t tmp = pci_conf_read(pc, tag, reg & ~0x2);
	tmp &= ~(0xffff << ((reg & 0x2) << 3));
	tmp |= (val << ((reg & 0x2) << 3));
	pci_conf_write(pc, tag, reg & ~0x2, tmp);
}

void
acpi_pci_conf_write_4(pci_chipset_tag_t pc, pcitag_t tag, int reg, uint32_t val)
{
	pci_conf_write(pc, tag, reg, val);
}

int
acpi_gasio(struct acpi_softc *sc, int iodir, int iospace, uint64_t address,
    int access_size, int len, void *buffer)
{
	uint8_t *pb;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	pci_chipset_tag_t pc;
	pcitag_t tag;
	int reg, idx;

	dnprintf(50, "gasio: %.2x 0x%.8llx %s\n",
	    iospace, address, (iodir == ACPI_IOWRITE) ? "write" : "read");

	KASSERT((len % access_size) == 0);

	pb = (uint8_t *)buffer;
	switch (iospace) {
	case GAS_SYSTEM_MEMORY:
	case GAS_SYSTEM_IOSPACE:
		if (iospace == GAS_SYSTEM_MEMORY)
			iot = sc->sc_memt;
		else
			iot = sc->sc_iot;

		if (acpi_bus_space_map(iot, address, len, 0, &ioh) != 0) {
			printf("%s: unable to map iospace\n", DEVNAME(sc));
			return (-1);
		}
		for (reg = 0; reg < len; reg += access_size) {
			if (iodir == ACPI_IOREAD) {
				switch (access_size) {
				case 1:
					*(uint8_t *)(pb + reg) =
					    bus_space_read_1(iot, ioh, reg);
					dnprintf(80, "os_in8(%llx) = %x\n",
					    reg+address, *(uint8_t *)(pb+reg));
					break;
				case 2:
					*(uint16_t *)(pb + reg) =
					    bus_space_read_2(iot, ioh, reg);
					dnprintf(80, "os_in16(%llx) = %x\n",
					    reg+address, *(uint16_t *)(pb+reg));
					break;
				case 4:
					*(uint32_t *)(pb + reg) =
					    bus_space_read_4(iot, ioh, reg);
					break;
				default:
					printf("%s: rdio: invalid size %d\n",
					    DEVNAME(sc), access_size);
					return (-1);
				}
			} else {
				switch (access_size) {
				case 1:
					bus_space_write_1(iot, ioh, reg,
					    *(uint8_t *)(pb + reg));
					dnprintf(80, "os_out8(%llx,%x)\n",
					    reg+address, *(uint8_t *)(pb+reg));
					break;
				case 2:
					bus_space_write_2(iot, ioh, reg,
					    *(uint16_t *)(pb + reg));
					dnprintf(80, "os_out16(%llx,%x)\n",
					    reg+address, *(uint16_t *)(pb+reg));
					break;
				case 4:
					bus_space_write_4(iot, ioh, reg,
					    *(uint32_t *)(pb + reg));
					break;
				default:
					printf("%s: wrio: invalid size %d\n",
					    DEVNAME(sc), access_size);
					return (-1);
				}
			}
		}
		acpi_bus_space_unmap(iot, ioh, len);
		break;

	case GAS_PCI_CFG_SPACE:
		/*
		 * The ACPI standard says that a function number of
		 * FFFF can be used to refer to all functions on a
		 * device.  This makes no sense though in the context
		 * of accessing PCI config space.  Yet there is AML
		 * out there that does this.  We simulate a read from
		 * a nonexistent device here.  Writes will panic when
		 * we try to construct the tag below.
		 */
		if (ACPI_PCI_FN(address) == 0xffff && iodir == ACPI_IOREAD) {
			memset(buffer, 0xff, len);
			return (0);
		}

		pc = pci_lookup_segment(ACPI_PCI_SEG(address),
		    ACPI_PCI_BUS(address));
		tag = pci_make_tag(pc,
		    ACPI_PCI_BUS(address), ACPI_PCI_DEV(address),
		    ACPI_PCI_FN(address));

		reg = ACPI_PCI_REG(address);
		for (idx = 0; idx < len; idx += access_size) {
			if (iodir == ACPI_IOREAD) {
				switch (access_size) {
				case 1:
					*(uint8_t *)(pb + idx) =
					    acpi_pci_conf_read_1(pc, tag, reg + idx);
					break;
				case 2:
					*(uint16_t *)(pb + idx) =
					    acpi_pci_conf_read_2(pc, tag, reg + idx);
					break;
				case 4:
					*(uint32_t *)(pb + idx) =
					    acpi_pci_conf_read_4(pc, tag, reg + idx);
					break;
				default:
					printf("%s: rdcfg: invalid size %d\n",
					    DEVNAME(sc), access_size);
					return (-1);
				}
			} else {
				switch (access_size) {
				case 1:
					acpi_pci_conf_write_1(pc, tag, reg + idx,
					    *(uint8_t *)(pb + idx));
					break;
				case 2:
					acpi_pci_conf_write_2(pc, tag, reg + idx,
					    *(uint16_t *)(pb + idx));
					break;
				case 4:
					acpi_pci_conf_write_4(pc, tag, reg + idx,
					    *(uint32_t *)(pb + idx));
					break;
				default:
					printf("%s: wrcfg: invalid size %d\n",
					    DEVNAME(sc), access_size);
					return (-1);
				}
			}
		}
		break;

	case GAS_EMBEDDED:
		if (sc->sc_ec == NULL) {
			printf("%s: WARNING EC not initialized\n", DEVNAME(sc));
			return (-1);
		}
		if (iodir == ACPI_IOREAD)
			acpiec_read(sc->sc_ec, (uint8_t)address, len, buffer);
		else
			acpiec_write(sc->sc_ec, (uint8_t)address, len, buffer);
		break;
	}
	return (0);
}

int
acpi_inidev(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	int64_t sta;

	/*
	 * Per the ACPI spec 6.5.1, only run _INI when device is there or
	 * when there is no _STA.  We terminate the tree walk (with return 1)
	 * early if necessary.
	 */

	/* Evaluate _STA to decide _INI fate and walk fate */
	sta = acpi_getsta(sc, node->parent);

	/* Evaluate _INI if we are present */
	if (sta & STA_PRESENT)
		aml_evalnode(sc, node, 0, NULL, NULL);

	/* If we are functioning, we walk/search our children */
	if (sta & STA_DEV_OK)
		return 0;

	/* If we are not enabled, or not present, terminate search */
	if (!(sta & (STA_PRESENT|STA_ENABLED)))
		return 1;

	/* Default just continue search */
	return 0;
}

int
acpi_foundprt(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct device		*self = (struct device *)arg;
	struct acpi_attach_args	aaa;
	int64_t sta;

	dnprintf(10, "found prt entry: %s\n", node->parent->name);

	/* Evaluate _STA to decide _PRT fate and walk fate */
	sta = acpi_getsta(sc, node->parent);
	if (sta & STA_PRESENT) {
		memset(&aaa, 0, sizeof(aaa));
		aaa.aaa_iot = sc->sc_iot;
		aaa.aaa_memt = sc->sc_memt;
		aaa.aaa_node = node;
		aaa.aaa_name = "acpiprt";

		config_found(self, &aaa, acpi_print);
	}

	/* If we are functioning, we walk/search our children */
	if (sta & STA_DEV_OK)
		return 0;

	/* If we are not enabled, or not present, terminate search */
	if (!(sta & (STA_PRESENT|STA_ENABLED)))
		return 1;

	/* Default just continue search */
	return 0;
}

TAILQ_HEAD(, acpi_pci) acpi_pcidevs =
    TAILQ_HEAD_INITIALIZER(acpi_pcidevs);
TAILQ_HEAD(, acpi_pci) acpi_pcirootdevs =
    TAILQ_HEAD_INITIALIZER(acpi_pcirootdevs);

int acpi_getpci(struct aml_node *node, void *arg);
int acpi_getminbus(int crsidx, union acpi_resource *crs, void *arg);

int
acpi_getminbus(int crsidx, union acpi_resource *crs, void *arg)
{
	int *bbn = arg;
	int typ = AML_CRSTYPE(crs);

	/* Check for embedded bus number */
	if (typ == LR_WORD && crs->lr_word.type == 2) {
		/* If _MIN > _MAX, the resource is considered to be invalid. */
		if (crs->lr_word._min > crs->lr_word._max)
			return -1;
		*bbn = crs->lr_word._min;
	}
	return 0;
}

int
acpi_matchcls(struct acpi_attach_args *aaa, int class, int subclass,
    int interface)
{
	struct acpi_softc *sc = acpi_softc;
	struct aml_value res;

	if (aaa->aaa_dev == NULL || aaa->aaa_node == NULL)
		return (0);

	if (aml_evalname(sc, aaa->aaa_node, "_CLS", 0, NULL, &res))
		return (0);

	if (res.type != AML_OBJTYPE_PACKAGE || res.length != 3 ||
	    res.v_package[0]->type != AML_OBJTYPE_INTEGER ||
	    res.v_package[1]->type != AML_OBJTYPE_INTEGER ||
	    res.v_package[2]->type != AML_OBJTYPE_INTEGER)
		return (0);

	if (res.v_package[0]->v_integer == class &&
	    res.v_package[1]->v_integer == subclass &&
	    res.v_package[2]->v_integer == interface)
		return (1);

	return (0);
}

int
_acpi_matchhids(const char *hid, const char *hids[])
{
	int i;

	for (i = 0; hids[i]; i++)
		if (!strcmp(hid, hids[i]))
			return (1);
	return (0);
}

int
acpi_matchhids(struct acpi_attach_args *aa, const char *hids[],
    const char *driver)
{
	if (aa->aaa_dev == NULL || aa->aaa_node == NULL)
		return (0);

	if (_acpi_matchhids(aa->aaa_dev, hids)) {
		dnprintf(5, "driver %s matches at least one hid\n", driver);
		return (2);
	}
	if (aa->aaa_cdev && _acpi_matchhids(aa->aaa_cdev, hids)) {
		dnprintf(5, "driver %s matches at least one cid\n", driver);
		return (1);
	}

	return (0);
}

int64_t
acpi_getsta(struct acpi_softc *sc, struct aml_node *node)
{
	int64_t sta;

	if (aml_evalinteger(sc, node, "_STA", 0, NULL, &sta))
		sta = STA_PRESENT | STA_ENABLED | STA_SHOW_UI |
		    STA_DEV_OK | STA_BATTERY;

	return sta;
}

/* Map ACPI device node to PCI */
int
acpi_getpci(struct aml_node *node, void *arg)
{
	const char *pcihid[] = { ACPI_DEV_PCIB, ACPI_DEV_PCIEB, "HWP0002", 0 };
	struct acpi_pci *pci, *ppci;
	struct aml_value res;
	struct acpi_softc *sc = arg;
	pci_chipset_tag_t pc;
	pcitag_t tag;
	uint64_t val;
	int64_t sta;
	uint32_t reg;

	sta = acpi_getsta(sc, node);
	if ((sta & STA_PRESENT) == 0)
		return 0;

	if (!node->value || node->value->type != AML_OBJTYPE_DEVICE)
		return 0;
	if (!aml_evalhid(node, &res)) {
		/* Check if this is a PCI Root node */
		if (_acpi_matchhids(res.v_string, pcihid)) {
			aml_freevalue(&res);

			pci = malloc(sizeof(*pci), M_DEVBUF, M_WAITOK|M_ZERO);

			pci->bus = -1;
			if (!aml_evalinteger(sc, node, "_SEG", 0, NULL, &val))
				pci->seg = val;
			if (!aml_evalname(sc, node, "_CRS", 0, NULL, &res)) {
				aml_parse_resource(&res, acpi_getminbus,
				    &pci->bus);
				dnprintf(10, "%s post-crs: %d\n",
				    aml_nodename(node), pci->bus);
			}
			if (!aml_evalinteger(sc, node, "_BBN", 0, NULL, &val)) {
				dnprintf(10, "%s post-bbn: %d, %lld\n",
				    aml_nodename(node), pci->bus, val);
				if (pci->bus == -1)
					pci->bus = val;
			}
			pci->sub = pci->bus;
			node->pci = pci;
			dnprintf(10, "found PCI root: %s %d\n",
			    aml_nodename(node), pci->bus);
			TAILQ_INSERT_TAIL(&acpi_pcirootdevs, pci, next);
		}
		aml_freevalue(&res);
		return 0;
	}

	/* If parent is not PCI, or device does not have _ADR, return */
	if (!node->parent || (ppci = node->parent->pci) == NULL)
		return 0;
	if (aml_evalinteger(sc, node, "_ADR", 0, NULL, &val))
		return 0;

	pci = malloc(sizeof(*pci), M_DEVBUF, M_WAITOK|M_ZERO);
	pci->seg = ppci->seg;
	pci->bus = ppci->sub;
	pci->dev = ACPI_ADR_PCIDEV(val);
	pci->fun = ACPI_ADR_PCIFUN(val);
	pci->node = node;
	pci->sub = -1;

	dnprintf(10, "%.2x:%.2x.%x -> %s\n",
		pci->bus, pci->dev, pci->fun,
		aml_nodename(node));

	/* Collect device power state information. */
	if (aml_evalinteger(sc, node, "_S0W", 0, NULL, &val) == 0)
		pci->_s0w = val;
	else
		pci->_s0w = -1;
	if (aml_evalinteger(sc, node, "_S3D", 0, NULL, &val) == 0)
		pci->_s3d = val;
	else
		pci->_s3d = -1;
	if (aml_evalinteger(sc, node, "_S3W", 0, NULL, &val) == 0)
		pci->_s3w = val;
	else
		pci->_s3w = -1;
	if (aml_evalinteger(sc, node, "_S4D", 0, NULL, &val) == 0)
		pci->_s4d = val;
	else
		pci->_s4d = -1;
	if (aml_evalinteger(sc, node, "_S4W", 0, NULL, &val) == 0)
		pci->_s4w = val;
	else
		pci->_s4w = -1;

	/* Check if PCI device exists */
	if (pci->dev > 0x1F || pci->fun > 7) {
		free(pci, M_DEVBUF, sizeof(*pci));
		return (1);
	}
	pc = pci_lookup_segment(pci->seg, pci->bus);
	tag = pci_make_tag(pc, pci->bus, pci->dev, pci->fun);
	reg = pci_conf_read(pc, tag, PCI_ID_REG);
	if (PCI_VENDOR(reg) == PCI_VENDOR_INVALID) {
		free(pci, M_DEVBUF, sizeof(*pci));
		return (1);
	}
	node->pci = pci;

	TAILQ_INSERT_TAIL(&acpi_pcidevs, pci, next);

	/* Check if this is a PCI bridge */
	reg = pci_conf_read(pc, tag, PCI_CLASS_REG);
	if (PCI_CLASS(reg) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(reg) == PCI_SUBCLASS_BRIDGE_PCI) {
		reg = pci_conf_read(pc, tag, PPB_REG_BUSINFO);
		pci->sub = PPB_BUSINFO_SECONDARY(reg);

		dnprintf(10, "found PCI bridge: %s %d\n",
		    aml_nodename(node), pci->sub);

		/* Continue scanning */
		return (0);
	}

	/* Device does not have children, stop scanning */
	return (1);
}

struct aml_node *
acpi_find_pci(pci_chipset_tag_t pc, pcitag_t tag)
{
	struct acpi_pci *pdev;
	int bus, dev, fun;

	pci_decompose_tag(pc, tag, &bus, &dev, &fun);
	TAILQ_FOREACH(pdev, &acpi_pcidevs, next) {
		if (pdev->bus == bus && pdev->dev == dev && pdev->fun == fun)
			return pdev->node;
	}

	return NULL;
}

struct aml_node *
acpi_pci_match(struct device *dev, struct pci_attach_args *pa)
{
	struct acpi_pci *pdev;
	int state;

	TAILQ_FOREACH(pdev, &acpi_pcidevs, next) {
		if (pdev->bus != pa->pa_bus ||
		    pdev->dev != pa->pa_device ||
		    pdev->fun != pa->pa_function)
			continue;

		dnprintf(10,"%s at acpi0 %s\n", dev->dv_xname,
		    aml_nodename(pdev->node));

		pdev->device = dev;

		/*
		 * If some Power Resources are dependent on this device
		 * initialize them.
		 */
		state = pci_get_powerstate(pa->pa_pc, pa->pa_tag);
		acpi_pci_set_powerstate(pa->pa_pc, pa->pa_tag, state, 1);
		acpi_pci_set_powerstate(pa->pa_pc, pa->pa_tag, state, 0);

		aml_register_notify(pdev->node, NULL, acpi_pci_notify, pdev, 0);

		return pdev->node;
	}

	return NULL;
}

pcireg_t
acpi_pci_min_powerstate(pci_chipset_tag_t pc, pcitag_t tag)
{
	struct acpi_pci *pdev;
	int bus, dev, fun;
	int state = -1, defaultstate = pci_get_powerstate(pc, tag);

	pci_decompose_tag(pc, tag, &bus, &dev, &fun);
	TAILQ_FOREACH(pdev, &acpi_pcidevs, next) {
		if (pdev->bus == bus && pdev->dev == dev && pdev->fun == fun) {
			switch (acpi_softc->sc_state) {
			case ACPI_STATE_S0:
				if (boothowto & RB_POWERDOWN) {
					defaultstate = PCI_PMCSR_STATE_D3;
					state = pdev->_s0w;
				}
				break;
			case ACPI_STATE_S3:
				defaultstate = PCI_PMCSR_STATE_D3;
				state = MAX(pdev->_s3d, pdev->_s3w);
				break;
			case ACPI_STATE_S4:
				state = MAX(pdev->_s4d, pdev->_s4w);
				break;
			case ACPI_STATE_S5:
			default:
				break;
			}

			if (state >= PCI_PMCSR_STATE_D0 &&
			    state <= PCI_PMCSR_STATE_D3)
				return state;
		}
	}

	return defaultstate;
}

void
acpi_pci_set_powerstate(pci_chipset_tag_t pc, pcitag_t tag, int state, int pre)
{
	struct acpi_softc *sc = acpi_softc;
#if NACPIPWRRES > 0
	struct acpi_pwrres *pr;
#endif
	struct acpi_pci *pdev;
	int bus, dev, fun;
	char name[5];

	pci_decompose_tag(pc, tag, &bus, &dev, &fun);
	TAILQ_FOREACH(pdev, &acpi_pcidevs, next) {
		if (pdev->bus == bus && pdev->dev == dev && pdev->fun == fun)
			break;
	}

	if (pdev == NULL)
		return;

	if (state != ACPI_STATE_D0 && !pre) {
		snprintf(name, sizeof(name), "_PS%d", state);
		aml_evalname(sc, pdev->node, name, 0, NULL, NULL);
	}

#if NACPIPWRRES > 0
	SIMPLEQ_FOREACH(pr, &sc->sc_pwrresdevs, p_next) {
		if (pr->p_node != pdev->node)
			continue;

		/*
		 * If the firmware is already aware that the device
		 * is in the given state, there's nothing to do.
		 */
		if (pr->p_state == state)
			continue;

		if (pre) {
			/*
			 * If a Resource is dependent on this device for
			 * the given state, make sure it is turned "_ON".
			 */
			if (pr->p_res_state == state)
				acpipwrres_ref_incr(pr->p_res_sc, pr->p_node);
		} else {
			/*
			 * If a Resource was referenced for the state we
			 * left, drop a reference and turn it "_OFF" if
			 * it was the last one.
			 */
			if (pr->p_res_state == pr->p_state)
				acpipwrres_ref_decr(pr->p_res_sc, pr->p_node);

			if (pr->p_res_state == state) {
				snprintf(name, sizeof(name), "_PS%d", state);
				aml_evalname(sc, pr->p_node, name, 0,
				    NULL, NULL);
			}

			pr->p_state = state;
		}

	}
#endif /* NACPIPWRRES > 0 */

	if (state == ACPI_STATE_D0 && pre)
		aml_evalname(sc, pdev->node, "_PS0", 0, NULL, NULL);
}

int
acpi_pci_notify(struct aml_node *node, int ntype, void *arg)
{
	struct acpi_pci *pdev = arg;
	pci_chipset_tag_t pc;
	pcitag_t tag;
	pcireg_t reg;
	int offset;

	/* We're only interested in Device Wake notifications. */
	if (ntype != 2)
		return (0);

	pc = pci_lookup_segment(pdev->seg, pdev->bus);
	tag = pci_make_tag(pc, pdev->bus, pdev->dev, pdev->fun);
	if (pci_get_capability(pc, tag, PCI_CAP_PWRMGMT, &offset, 0)) {
		/* Clear the PME Status bit if it is set. */
		reg = pci_conf_read(pc, tag, offset + PCI_PMCSR);
		pci_conf_write(pc, tag, offset + PCI_PMCSR, reg);
	}

	return (0);
}

void
acpi_pciroots_attach(struct device *dev, void *aux, cfprint_t pr)
{
	struct acpi_pci			*pdev;
	struct pcibus_attach_args	*pba = aux;

	KASSERT(pba->pba_busex != NULL);

	TAILQ_FOREACH(pdev, &acpi_pcirootdevs, next) {
		if (extent_alloc_region(pba->pba_busex, pdev->bus,
		    1, EX_NOWAIT) != 0)
			continue;
		pba->pba_bus = pdev->bus;
		config_found(dev, pba, pr);
	}
}

/* GPIO support */

struct acpi_gpio_event {
	struct aml_node *node;
	uint16_t tflags;
	uint16_t pin;
};

void
acpi_gpio_event_task(void *arg0, int arg1)
{
	struct acpi_softc *sc = acpi_softc;
	struct acpi_gpio_event *ev = arg0;
	struct acpi_gpio *gpio = ev->node->gpio;
	struct aml_value evt;
	uint16_t pin = arg1;
	char name[5];

	if (pin < 256) {
		if ((ev->tflags & LR_GPIO_MODE) == LR_GPIO_LEVEL)
			snprintf(name, sizeof(name), "_L%.2X", pin);
		else
			snprintf(name, sizeof(name), "_E%.2X", pin);
		if (aml_evalname(sc, ev->node, name, 0, NULL, NULL) == 0)
			goto intr_enable;
	}

	memset(&evt, 0, sizeof(evt));
	evt.v_integer = pin;
	evt.type = AML_OBJTYPE_INTEGER;
	aml_evalname(sc, ev->node, "_EVT", 1, &evt, NULL);

intr_enable:
	if ((ev->tflags & LR_GPIO_MODE) == LR_GPIO_LEVEL) {
		if (gpio->intr_enable)
			gpio->intr_enable(gpio->cookie, pin);
	}
}

int
acpi_gpio_event(void *arg)
{
	struct acpi_softc *sc = acpi_softc;
	struct acpi_gpio_event *ev = arg;
	struct acpi_gpio *gpio = ev->node->gpio;

	if ((ev->tflags & LR_GPIO_MODE) == LR_GPIO_LEVEL) {
		if(gpio->intr_disable)
			gpio->intr_disable(gpio->cookie, ev->pin);
	}

	if (cpu_suspended) {
		cpu_suspended = 0;
		sc->sc_wakegpe = -3;
		sc->sc_wakegpio = ev->pin;
	}

	acpi_addtask(acpi_softc, acpi_gpio_event_task, ev, ev->pin);
	acpi_wakeup(acpi_softc);

	return 1;
}

int
acpi_gpio_parse_events(int crsidx, union acpi_resource *crs, void *arg)
{
	struct aml_node *devnode = arg;
	struct aml_node *node;
	uint16_t pin;

	switch (AML_CRSTYPE(crs)) {
	case LR_GPIO:
		node = aml_searchname(devnode,
		    (char *)&crs->pad[crs->lr_gpio.res_off]);
		pin = *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];
		if (crs->lr_gpio.type == LR_GPIO_INT &&
		    node && node->gpio && node->gpio->intr_establish) {
			struct acpi_gpio *gpio = node->gpio;
			struct acpi_gpio_event *ev;

			ev = malloc(sizeof(*ev), M_DEVBUF, M_WAITOK);
			ev->node = devnode;
			ev->tflags = crs->lr_gpio.tflags;
			ev->pin = pin;
			gpio->intr_establish(gpio->cookie, pin,
			    crs->lr_gpio.tflags, IPL_BIO | IPL_WAKEUP,
			    acpi_gpio_event, ev);
		}
		break;
	default:
		printf("%s: unknown resource type %d\n", __func__,
		    AML_CRSTYPE(crs));
	}

	return 0;
}

void
acpi_register_gpio(struct acpi_softc *sc, struct aml_node *devnode)
{
	struct aml_value arg[2];
	struct aml_node *node;
	struct aml_value res;

	/* Register GeneralPurposeIO address space. */
	memset(&arg, 0, sizeof(arg));
	arg[0].type = AML_OBJTYPE_INTEGER;
	arg[0].v_integer = ACPI_OPREG_GPIO;
	arg[1].type = AML_OBJTYPE_INTEGER;
	arg[1].v_integer = 1;
	node = aml_searchname(devnode, "_REG");
	if (node && aml_evalnode(sc, node, 2, arg, NULL))
		printf("%s: _REG failed\n", node->name);

	/* Register GPIO signaled ACPI events. */
	if (aml_evalname(sc, devnode, "_AEI", 0, NULL, &res))
		return;
	aml_parse_resource(&res, acpi_gpio_parse_events, devnode);
}

#ifndef SMALL_KERNEL

void
acpi_register_gsb(struct acpi_softc *sc, struct aml_node *devnode)
{
	struct aml_value arg[2];
	struct aml_node *node;

	/* Register GenericSerialBus address space. */
	memset(&arg, 0, sizeof(arg));
	arg[0].type = AML_OBJTYPE_INTEGER;
	arg[0].v_integer = ACPI_OPREG_GSB;
	arg[1].type = AML_OBJTYPE_INTEGER;
	arg[1].v_integer = 1;
	node = aml_searchname(devnode, "_REG");
	if (node && aml_evalnode(sc, node, 2, arg, NULL))
		printf("%s: _REG failed\n", node->name);
}

#endif

void
acpi_attach_common(struct acpi_softc *sc, paddr_t base)
{
	struct acpi_mem_map handle;
	struct acpi_rsdp *rsdp;
	struct acpi_q *entry;
	struct acpi_dsdt *p_dsdt;
#ifndef SMALL_KERNEL
	int wakeup_dev_ct;
	struct acpi_wakeq *wentry;
	struct device *dev;
#endif /* SMALL_KERNEL */
	paddr_t facspa;
	uint16_t pm1;
	int s;

	rw_init(&sc->sc_lck, "acpilk");

	acpi_softc = sc;
	sc->sc_root = &aml_root;

	if (acpi_map(base, sizeof(struct acpi_rsdp), &handle)) {
		printf(": can't map memory\n");
		return;
	}
	rsdp = (struct acpi_rsdp *)handle.va;

	pool_init(&acpiwqpool, sizeof(struct acpi_taskq), 0, IPL_BIO, 0,
	    "acpiwqpl", NULL);
	pool_setlowat(&acpiwqpool, 16);

	SIMPLEQ_INIT(&sc->sc_tables);
	SIMPLEQ_INIT(&sc->sc_wakedevs);
#if NACPIPWRRES > 0
	SIMPLEQ_INIT(&sc->sc_pwrresdevs);
#endif /* NACPIPWRRES > 0 */

	if (acpi_loadtables(sc, rsdp)) {
		printf(": can't load tables\n");
		acpi_unmap(&handle);
		return;
	}

	acpi_unmap(&handle);

	/*
	 * Find the FADT
	 */
	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		if (memcmp(entry->q_table, FADT_SIG,
		    sizeof(FADT_SIG) - 1) == 0) {
			sc->sc_fadt = entry->q_table;
			break;
		}
	}
	if (sc->sc_fadt == NULL) {
		printf(": no FADT\n");
		return;
	}

	sc->sc_major = sc->sc_fadt->hdr.revision;
	if (sc->sc_major > 4)
		sc->sc_minor = sc->sc_fadt->fadt_minor;
	printf(": ACPI %d.%d", sc->sc_major, sc->sc_minor);

	/*
	 * A bunch of things need to be done differently for
	 * Hardware-reduced ACPI.
	 */
	if (sc->sc_fadt->hdr_revision >= 5 &&
	    sc->sc_fadt->flags & FADT_HW_REDUCED_ACPI)
		sc->sc_hw_reduced = 1;

	/* Map Power Management registers */
	acpi_map_pmregs(sc);

	/*
	 * Check if we can and need to enable ACPI control.
	 */
	pm1 = acpi_read_pmreg(sc, ACPIREG_PM1_CNT, 0);
	if ((pm1 & ACPI_PM1_SCI_EN) == 0 && sc->sc_fadt->smi_cmd &&
	    (!sc->sc_fadt->acpi_enable && !sc->sc_fadt->acpi_disable)) {
		printf(", ACPI control unavailable\n");
		acpi_unmap_pmregs(sc);
		return;
	}

	/*
	 * Set up a pointer to the firmware control structure
	 */
	if (sc->sc_fadt->hdr_revision < 3 || sc->sc_fadt->x_firmware_ctl == 0)
		facspa = sc->sc_fadt->firmware_ctl;
	else
		facspa = sc->sc_fadt->x_firmware_ctl;

	if (acpi_map(facspa, sizeof(struct acpi_facs), &handle))
		printf(" !FACS");
	else
		sc->sc_facs = (struct acpi_facs *)handle.va;

	/* Create opcode hashtable */
	aml_hashopcodes();

	/* Create Default AML objects */
	aml_create_defaultobjects();

	/*
	 * Load the DSDT from the FADT pointer -- use the
	 * extended (64-bit) pointer if it exists
	 */
	if (sc->sc_fadt->hdr_revision < 3 || sc->sc_fadt->x_dsdt == 0)
		entry = acpi_maptable(sc, sc->sc_fadt->dsdt, NULL, NULL, NULL,
		    -1);
	else
		entry = acpi_maptable(sc, sc->sc_fadt->x_dsdt, NULL, NULL, NULL,
		    -1);

	if (entry == NULL)
		printf(" !DSDT");

	p_dsdt = entry->q_table;
	acpi_parse_aml(sc, NULL, p_dsdt->aml,
	    p_dsdt->hdr_length - sizeof(p_dsdt->hdr));

	/* Load SSDT's */
	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		if (memcmp(entry->q_table, SSDT_SIG,
		    sizeof(SSDT_SIG) - 1) == 0) {
			p_dsdt = entry->q_table;
			acpi_parse_aml(sc, NULL, p_dsdt->aml,
			    p_dsdt->hdr_length - sizeof(p_dsdt->hdr));
		}
	}

	/* Perform post-parsing fixups */
	aml_postparse();

#if 0
	if (sc->sc_fadt->hdr_revision > 2 &&
	    !ISSET(sc->sc_fadt->iapc_boot_arch, FADT_LEGACY_DEVICES))
		acpi_legacy_free = 1;
#endif

#ifndef SMALL_KERNEL
	/* Find available sleeping states */
	acpi_init_states(sc);

	/* Find available sleep/resume related methods. */
	acpi_init_pm(sc);
#endif /* SMALL_KERNEL */

	/* Initialize GPE handlers */
	s = splbio();
	acpi_init_gpes(sc);
	splx(s);

	/* some devices require periodic polling */
	timeout_set(&sc->sc_dev_timeout, acpi_poll, sc);

	acpi_enabled = 1;

	/*
	 * Take over ACPI control.  Note that once we do this, we
	 * effectively tell the system that we have ownership of
	 * the ACPI hardware registers, and that SMI should leave
	 * them alone
	 *
	 * This may prevent thermal control on some systems where
	 * that actually does work
	 */
	if ((pm1 & ACPI_PM1_SCI_EN) == 0 && sc->sc_fadt->smi_cmd) {
		if (acpi_enable(sc)) {
			printf(", can't enable ACPI\n");
			return;
		}
	}

	printf("\n%s: tables", DEVNAME(sc));
	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		printf(" %.4s", (char *)entry->q_table);
	}
	printf("\n");

#ifndef SMALL_KERNEL
	/* Display wakeup devices and lowest S-state */
	wakeup_dev_ct = 0;
	printf("%s: wakeup devices", DEVNAME(sc));
	SIMPLEQ_FOREACH(wentry, &sc->sc_wakedevs, q_next) {
		if (wakeup_dev_ct < 16)
			printf(" %.4s(S%d)", wentry->q_node->name,
			    wentry->q_state);
		else if (wakeup_dev_ct == 16)
			printf(" [...]");
		wakeup_dev_ct++;
	}
	printf("\n");

#ifdef SUSPEND
	if (wakeup_dev_ct > 0)
		device_register_wakeup(&sc->sc_dev);
#endif
#endif /* SMALL_KERNEL */

	/*
	 * ACPI is enabled now -- attach timer
	 */
	if (!sc->sc_hw_reduced &&
	    (sc->sc_fadt->pm_tmr_blk || sc->sc_fadt->x_pm_tmr_blk.address)) {
		struct acpi_attach_args aaa;

		memset(&aaa, 0, sizeof(aaa));
		aaa.aaa_name = "acpitimer";
		aaa.aaa_iot = sc->sc_iot;
		aaa.aaa_memt = sc->sc_memt;
		config_found(&sc->sc_dev, &aaa, acpi_print);
	}

	/*
	 * Attach table-defined devices
	 */
	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		struct acpi_attach_args aaa;

		memset(&aaa, 0, sizeof(aaa));
		aaa.aaa_iot = sc->sc_iot;
		aaa.aaa_memt = sc->sc_memt;
		aaa.aaa_dmat = sc->sc_ci_dmat;
		aaa.aaa_table = entry->q_table;
		config_found_sm(&sc->sc_dev, &aaa, acpi_print, acpi_submatch);
	}

	/* initialize runtime environment */
	aml_find_node(sc->sc_root, "_INI", acpi_inidev, sc);

#ifdef __arm64__
	aml_find_node(sc->sc_root, "ECTC", acpi_foundsectwo, sc);
#endif

	/* Get PCI mapping */
	aml_walknodes(sc->sc_root, AML_WALK_PRE, acpi_getpci, sc);

#if defined (__amd64__) || defined(__i386__)
	/* attach pci interrupt routing tables */
	aml_find_node(sc->sc_root, "_PRT", acpi_foundprt, sc);
#endif

	aml_find_node(sc->sc_root, "_HID", acpi_foundec, sc);

	/* check if we're running on a sony */
	aml_find_node(sc->sc_root, "GBRT", acpi_foundsony, sc);

#ifndef SMALL_KERNEL
	/* try to find smart battery first */
	aml_find_node(sc->sc_root, "_HID", acpi_foundsbs, sc);
#endif /* SMALL_KERNEL */

	/* attach battery, power supply and button devices */
	aml_find_node(sc->sc_root, "_HID", acpi_foundhid, sc);

	aml_walknodes(sc->sc_root, AML_WALK_PRE, acpi_add_device, sc);

#ifndef SMALL_KERNEL
#if NWD > 0
	/* Attach IDE bay */
	aml_walknodes(sc->sc_root, AML_WALK_PRE, acpi_foundide, sc);
#endif

	/* attach docks */
	aml_find_node(sc->sc_root, "_DCK", acpi_founddock, sc);

	/* attach video */
	aml_find_node(sc->sc_root, "_DOS", acpi_foundvideo, sc);

	/* create list of devices we want to query when APM comes in */
	SLIST_INIT(&sc->sc_ac);
	SLIST_INIT(&sc->sc_bat);
	TAILQ_FOREACH(dev, &alldevs, dv_list) {
		if (!strcmp(dev->dv_cfdata->cf_driver->cd_name, "acpiac")) {
			struct acpi_ac *ac;

			ac = malloc(sizeof(*ac), M_DEVBUF, M_WAITOK | M_ZERO);
			ac->aac_softc = (struct acpiac_softc *)dev;
			SLIST_INSERT_HEAD(&sc->sc_ac, ac, aac_link);
		} else if (!strcmp(dev->dv_cfdata->cf_driver->cd_name, "acpibat")) {
			struct acpi_bat *bat;

			bat = malloc(sizeof(*bat), M_DEVBUF, M_WAITOK | M_ZERO);
			bat->aba_softc = (struct acpibat_softc *)dev;
			SLIST_INSERT_HEAD(&sc->sc_bat, bat, aba_link);
		} else if (!strcmp(dev->dv_cfdata->cf_driver->cd_name, "acpisbs")) {
			struct acpi_sbs *sbs;

			sbs = malloc(sizeof(*sbs), M_DEVBUF, M_WAITOK | M_ZERO);
			sbs->asbs_softc = (struct acpisbs_softc *)dev;
			SLIST_INSERT_HEAD(&sc->sc_sbs, sbs, asbs_link);
		}
	}

#endif /* SMALL_KERNEL */

	/* Setup threads */
	sc->sc_thread = malloc(sizeof(struct acpi_thread), M_DEVBUF, M_WAITOK);
	sc->sc_thread->sc = sc;
	sc->sc_thread->running = 1;

	/* Enable PCI Power Management. */
	pci_dopm = 1;

	acpi_attach_machdep(sc);

	kthread_create_deferred(acpi_create_thread, sc);
}

int
acpi_submatch(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = (struct acpi_attach_args *)aux;
	struct cfdata *cf = match;

	if (aaa->aaa_table == NULL)
		return (0);
	return ((*cf->cf_attach->ca_match)(parent, match, aux));
}

int
acpi_noprint(void *aux, const char *pnp)
{
	return (QUIET);
}

int
acpi_print(void *aux, const char *pnp)
{
	struct acpi_attach_args *aa = aux;

	if (pnp) {
		if (aa->aaa_name)
			printf("%s at %s", aa->aaa_name, pnp);
		else if (aa->aaa_dev)
			printf("\"%s\" at %s", aa->aaa_dev, pnp);
		else
			return (QUIET);
	}

	return (UNCONF);
}

struct acpi_q *
acpi_maptable(struct acpi_softc *sc, paddr_t addr, const char *sig,
    const char *oem, const char *tbl, int flag)
{
	static int tblid;
	struct acpi_mem_map handle;
	struct acpi_table_header *hdr;
	struct acpi_q *entry;
	size_t len;

	/* Check if we can map address */
	if (addr == 0)
		return NULL;
	if (acpi_map(addr, sizeof(*hdr), &handle))
		return NULL;
	hdr = (struct acpi_table_header *)handle.va;
	len = hdr->length;
	acpi_unmap(&handle);

	/* Validate length/checksum */
	if (acpi_map(addr, len, &handle))
		return NULL;
	hdr = (struct acpi_table_header *)handle.va;
	if (acpi_checksum(hdr, len))
		printf("\n%s: %.4s checksum error",
		    DEVNAME(sc), hdr->signature);

	if ((sig && memcmp(sig, hdr->signature, 4)) ||
	    (oem && memcmp(oem, hdr->oemid, 6)) ||
	    (tbl && memcmp(tbl, hdr->oemtableid, 8))) {
		acpi_unmap(&handle);
		return NULL;
	}

	/* Allocate copy */
	entry = malloc(sizeof(*entry) + len, M_DEVBUF, M_NOWAIT);
	if (entry != NULL) {
		memcpy(entry->q_data, handle.va, len);
		entry->q_table = entry->q_data;
		entry->q_id = ++tblid;

		if (flag < 0)
			SIMPLEQ_INSERT_HEAD(&sc->sc_tables, entry,
			    q_next);
		else if (flag > 0)
			SIMPLEQ_INSERT_TAIL(&sc->sc_tables, entry,
			    q_next);
	}
	acpi_unmap(&handle);
	return entry;
}

int
acpi_loadtables(struct acpi_softc *sc, struct acpi_rsdp *rsdp)
{
	struct acpi_q *sdt;
	int i, ntables;
	size_t len;

	if (rsdp->rsdp_revision == 2 && rsdp->rsdp_xsdt) {
		struct acpi_xsdt *xsdt;

		sdt = acpi_maptable(sc, rsdp->rsdp_xsdt, NULL, NULL, NULL, 0);
		if (sdt == NULL) {
			printf("couldn't map xsdt\n");
			return (ENOMEM);
		}

		xsdt = (struct acpi_xsdt *)sdt->q_data;
		len  = xsdt->hdr.length;
		ntables = (len - sizeof(struct acpi_table_header)) /
		    sizeof(xsdt->table_offsets[0]);

		for (i = 0; i < ntables; i++)
			acpi_maptable(sc, xsdt->table_offsets[i], NULL, NULL,
			    NULL, 1);

		free(sdt, M_DEVBUF, sizeof(*sdt) + len);
	} else {
		struct acpi_rsdt *rsdt;

		sdt = acpi_maptable(sc, rsdp->rsdp_rsdt, NULL, NULL, NULL, 0);
		if (sdt == NULL) {
			printf("couldn't map rsdt\n");
			return (ENOMEM);
		}

		rsdt = (struct acpi_rsdt *)sdt->q_data;
		len  = rsdt->hdr.length;
		ntables = (len - sizeof(struct acpi_table_header)) /
		    sizeof(rsdt->table_offsets[0]);

		for (i = 0; i < ntables; i++)
			acpi_maptable(sc, rsdt->table_offsets[i], NULL, NULL,
			    NULL, 1);

		free(sdt, M_DEVBUF, sizeof(*sdt) + len);
	}

	return (0);
}

/* Read from power management register */
int
acpi_read_pmreg(struct acpi_softc *sc, int reg, int offset)
{
	bus_space_handle_t ioh;
	bus_size_t size;
	int regval;

	/*
	 * For Hardware-reduced ACPI we emulate PM1B_CNT to reflect
	 * that the system is always in ACPI mode.
	 */
	if (sc->sc_hw_reduced && reg == ACPIREG_PM1B_CNT) {
		KASSERT(offset == 0);
		return ACPI_PM1_SCI_EN;
	}

	/*
	 * For Hardware-reduced ACPI we also emulate PM1A_STS using
	 * SLEEP_STATUS_REG.
	 */
	if (sc->sc_hw_reduced && reg == ACPIREG_PM1A_STS &&
	    sc->sc_fadt->sleep_status_reg.register_bit_width > 0) {
		uint8_t value;

		KASSERT(offset == 0);
		acpi_gasio(sc, ACPI_IOREAD,
		    sc->sc_fadt->sleep_status_reg.address_space_id,
		    sc->sc_fadt->sleep_status_reg.address,
		    sc->sc_fadt->sleep_status_reg.register_bit_width / 8,
		    sc->sc_fadt->sleep_status_reg.access_size, &value);
		return ((int)value << 8);
	}

	/* Special cases: 1A/1B blocks can be OR'ed together */
	switch (reg) {
	case ACPIREG_PM1_EN:
		return (acpi_read_pmreg(sc, ACPIREG_PM1A_EN, offset) |
		    acpi_read_pmreg(sc, ACPIREG_PM1B_EN, offset));
	case ACPIREG_PM1_STS:
		return (acpi_read_pmreg(sc, ACPIREG_PM1A_STS, offset) |
		    acpi_read_pmreg(sc, ACPIREG_PM1B_STS, offset));
	case ACPIREG_PM1_CNT:
		return (acpi_read_pmreg(sc, ACPIREG_PM1A_CNT, offset) |
		    acpi_read_pmreg(sc, ACPIREG_PM1B_CNT, offset));
	case ACPIREG_GPE_STS:
		dnprintf(50, "read GPE_STS  offset: %.2x %.2x %.2x\n", offset,
		    sc->sc_fadt->gpe0_blk_len>>1, sc->sc_fadt->gpe1_blk_len>>1);
		if (offset < (sc->sc_fadt->gpe0_blk_len >> 1)) {
			reg = ACPIREG_GPE0_STS;
		}
		break;
	case ACPIREG_GPE_EN:
		dnprintf(50, "read GPE_EN   offset: %.2x %.2x %.2x\n",
		    offset, sc->sc_fadt->gpe0_blk_len>>1,
		    sc->sc_fadt->gpe1_blk_len>>1);
		if (offset < (sc->sc_fadt->gpe0_blk_len >> 1)) {
			reg = ACPIREG_GPE0_EN;
		}
		break;
	}

	if (reg >= ACPIREG_MAXREG || sc->sc_pmregs[reg].size == 0)
		return (0);

	regval = 0;
	ioh = sc->sc_pmregs[reg].ioh;
	size = sc->sc_pmregs[reg].size;
	if (size > sc->sc_pmregs[reg].access)
		size = sc->sc_pmregs[reg].access;

	switch (size) {
	case 1:
		regval = bus_space_read_1(sc->sc_iot, ioh, offset);
		break;
	case 2:
		regval = bus_space_read_2(sc->sc_iot, ioh, offset);
		break;
	case 4:
		regval = bus_space_read_4(sc->sc_iot, ioh, offset);
		break;
	}

	dnprintf(30, "acpi_readpm: %s = %.4x:%.4x %x\n",
	    sc->sc_pmregs[reg].name,
	    sc->sc_pmregs[reg].addr, offset, regval);
	return (regval);
}

/* Write to power management register */
void
acpi_write_pmreg(struct acpi_softc *sc, int reg, int offset, int regval)
{
	bus_space_handle_t ioh;
	bus_size_t size;

	/*
	 * For Hardware-reduced ACPI we also emulate PM1A_STS using
	 * SLEEP_STATUS_REG.
	 */
	if (sc->sc_hw_reduced && reg == ACPIREG_PM1A_STS &&
	    sc->sc_fadt->sleep_status_reg.register_bit_width > 0) {
		uint8_t value = (regval >> 8);

		KASSERT(offset == 0);
		acpi_gasio(sc, ACPI_IOWRITE,
		    sc->sc_fadt->sleep_status_reg.address_space_id,
		    sc->sc_fadt->sleep_status_reg.address,
		    sc->sc_fadt->sleep_status_reg.register_bit_width / 8,
		    sc->sc_fadt->sleep_status_reg.access_size, &value);
		return;
	}

	/*
	 * For Hardware-reduced ACPI we also emulate PM1A_CNT using
	 * SLEEP_CONTROL_REG.
	 */
	if (sc->sc_hw_reduced && reg == ACPIREG_PM1A_CNT &&
	    sc->sc_fadt->sleep_control_reg.register_bit_width > 0) {
		uint8_t value = (regval >> 8);

		KASSERT(offset == 0);
		acpi_gasio(sc, ACPI_IOWRITE,
		    sc->sc_fadt->sleep_control_reg.address_space_id,
		    sc->sc_fadt->sleep_control_reg.address,
		    sc->sc_fadt->sleep_control_reg.register_bit_width / 8,
		    sc->sc_fadt->sleep_control_reg.access_size, &value);
		return;
	}

	/* Special cases: 1A/1B blocks can be written with same value */
	switch (reg) {
	case ACPIREG_PM1_EN:
		acpi_write_pmreg(sc, ACPIREG_PM1A_EN, offset, regval);
		acpi_write_pmreg(sc, ACPIREG_PM1B_EN, offset, regval);
		break;
	case ACPIREG_PM1_STS:
		acpi_write_pmreg(sc, ACPIREG_PM1A_STS, offset, regval);
		acpi_write_pmreg(sc, ACPIREG_PM1B_STS, offset, regval);
		break;
	case ACPIREG_PM1_CNT:
		acpi_write_pmreg(sc, ACPIREG_PM1A_CNT, offset, regval);
		acpi_write_pmreg(sc, ACPIREG_PM1B_CNT, offset, regval);
		break;
	case ACPIREG_GPE_STS:
		dnprintf(50, "write GPE_STS offset: %.2x %.2x %.2x %.2x\n",
		    offset, sc->sc_fadt->gpe0_blk_len>>1,
		    sc->sc_fadt->gpe1_blk_len>>1, regval);
		if (offset < (sc->sc_fadt->gpe0_blk_len >> 1)) {
			reg = ACPIREG_GPE0_STS;
		}
		break;
	case ACPIREG_GPE_EN:
		dnprintf(50, "write GPE_EN  offset: %.2x %.2x %.2x %.2x\n",
		    offset, sc->sc_fadt->gpe0_blk_len>>1,
		    sc->sc_fadt->gpe1_blk_len>>1, regval);
		if (offset < (sc->sc_fadt->gpe0_blk_len >> 1)) {
			reg = ACPIREG_GPE0_EN;
		}
		break;
	}

	/* All special case return here */
	if (reg >= ACPIREG_MAXREG)
		return;

	ioh = sc->sc_pmregs[reg].ioh;
	size = sc->sc_pmregs[reg].size;
	if (size > sc->sc_pmregs[reg].access)
		size = sc->sc_pmregs[reg].access;

	switch (size) {
	case 1:
		bus_space_write_1(sc->sc_iot, ioh, offset, regval);
		break;
	case 2:
		bus_space_write_2(sc->sc_iot, ioh, offset, regval);
		break;
	case 4:
		bus_space_write_4(sc->sc_iot, ioh, offset, regval);
		break;
	}

	dnprintf(30, "acpi_writepm: %s = %.4x:%.4x %x\n",
	    sc->sc_pmregs[reg].name, sc->sc_pmregs[reg].addr, offset, regval);
}

/* Map Power Management registers */
void
acpi_map_pmregs(struct acpi_softc *sc)
{
	struct acpi_fadt *fadt = sc->sc_fadt;
	bus_addr_t addr;
	bus_size_t size, access;
	const char *name;
	int reg;

	for (reg = 0; reg < ACPIREG_MAXREG; reg++) {
		size = 0;
		access = 0;
		switch (reg) {
		case ACPIREG_SMICMD:
			name = "smi";
			size = access = 1;
			addr = fadt->smi_cmd;
			break;
		case ACPIREG_PM1A_STS:
		case ACPIREG_PM1A_EN:
			name = "pm1a_sts";
			size = fadt->pm1_evt_len >> 1;
			if (fadt->pm1a_evt_blk) {
				addr = fadt->pm1a_evt_blk;
				access = 2;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm1a_evt_blk.address;
				access = 1 << fadt->x_pm1a_evt_blk.access_size;
			}
			if (reg == ACPIREG_PM1A_EN && addr) {
				addr += size;
				name = "pm1a_en";
			}
			break;
		case ACPIREG_PM1A_CNT:
			name = "pm1a_cnt";
			size = fadt->pm1_cnt_len;
			if (fadt->pm1a_cnt_blk) {
				addr = fadt->pm1a_cnt_blk;
				access = 2;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm1a_cnt_blk.address;
				access = 1 << fadt->x_pm1a_cnt_blk.access_size;
			}
			break;
		case ACPIREG_PM1B_STS:
		case ACPIREG_PM1B_EN:
			name = "pm1b_sts";
			size = fadt->pm1_evt_len >> 1;
			if (fadt->pm1b_evt_blk) {
				addr = fadt->pm1b_evt_blk;
				access = 2;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm1b_evt_blk.address;
				access = 1 << fadt->x_pm1b_evt_blk.access_size;
			}
			if (reg == ACPIREG_PM1B_EN && addr) {
				addr += size;
				name = "pm1b_en";
			}
			break;
		case ACPIREG_PM1B_CNT:
			name = "pm1b_cnt";
			size = fadt->pm1_cnt_len;
			if (fadt->pm1b_cnt_blk) {
				addr = fadt->pm1b_cnt_blk;
				access = 2;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm1b_cnt_blk.address;
				access = 1 << fadt->x_pm1b_cnt_blk.access_size;
			}
			break;
		case ACPIREG_PM2_CNT:
			name = "pm2_cnt";
			size = fadt->pm2_cnt_len;
			if (fadt->pm2_cnt_blk) {
				addr = fadt->pm2_cnt_blk;
				access = size;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm2_cnt_blk.address;
				access = 1 << fadt->x_pm2_cnt_blk.access_size;
			}
			break;
#if 0
		case ACPIREG_PM_TMR:
			/* Allocated in acpitimer */
			name = "pm_tmr";
			size = fadt->pm_tmr_len;
			if (fadt->pm_tmr_blk) {
				addr = fadt->pm_tmr_blk;
				access = 4;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_pm_tmr_blk.address;
				access = 1 << fadt->x_pm_tmr_blk.access_size;
			}
			break;
#endif
		case ACPIREG_GPE0_STS:
		case ACPIREG_GPE0_EN:
			name = "gpe0_sts";
			size = fadt->gpe0_blk_len >> 1;
			if (fadt->gpe0_blk) {
				addr = fadt->gpe0_blk;
				access = 1;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_gpe0_blk.address;
				access = 1 << fadt->x_gpe0_blk.access_size;
			}

			dnprintf(20, "gpe0 block len : %x\n",
			    fadt->gpe0_blk_len >> 1);
			dnprintf(20, "gpe0 block addr: %x\n",
			    fadt->gpe0_blk);
			if (reg == ACPIREG_GPE0_EN && addr) {
				addr += size;
				name = "gpe0_en";
			}
			break;
		case ACPIREG_GPE1_STS:
		case ACPIREG_GPE1_EN:
			name = "gpe1_sts";
			size = fadt->gpe1_blk_len >> 1;
			if (fadt->gpe1_blk) {
				addr = fadt->gpe1_blk;
				access = 1;
			} else if (fadt->hdr_revision >= 3) {
				addr = fadt->x_gpe1_blk.address;
				access = 1 << fadt->x_gpe1_blk.access_size;
			}

			dnprintf(20, "gpe1 block len : %x\n",
			    fadt->gpe1_blk_len >> 1);
			dnprintf(20, "gpe1 block addr: %x\n",
			    fadt->gpe1_blk);
			if (reg == ACPIREG_GPE1_EN && addr) {
				addr += size;
				name = "gpe1_en";
			}
			break;
		}
		if (size && addr) {
			dnprintf(50, "mapping: %.4lx %.4lx %s\n",
			    addr, size, name);

			/* Size and address exist; map register space */
			bus_space_map(sc->sc_iot, addr, size, 0,
			    &sc->sc_pmregs[reg].ioh);

			sc->sc_pmregs[reg].name = name;
			sc->sc_pmregs[reg].size = size;
			sc->sc_pmregs[reg].addr = addr;
			sc->sc_pmregs[reg].access = min(access, 4);
		}
	}
}

void
acpi_unmap_pmregs(struct acpi_softc *sc)
{
	int reg;

	for (reg = 0; reg < ACPIREG_MAXREG; reg++) {
		if (sc->sc_pmregs[reg].size && sc->sc_pmregs[reg].addr)
			bus_space_unmap(sc->sc_iot, sc->sc_pmregs[reg].ioh,
			    sc->sc_pmregs[reg].size);
	}
}

int
acpi_enable(struct acpi_softc *sc)
{
	int idx;

	acpi_write_pmreg(sc, ACPIREG_SMICMD, 0, sc->sc_fadt->acpi_enable);
	idx = 0;
	do {
		if (idx++ > ACPIEN_RETRIES) {
			return ETIMEDOUT;
		}
	} while (!(acpi_read_pmreg(sc, ACPIREG_PM1_CNT, 0) & ACPI_PM1_SCI_EN));

	return 0;
}

/* ACPI Workqueue support */
SIMPLEQ_HEAD(,acpi_taskq) acpi_taskq =
    SIMPLEQ_HEAD_INITIALIZER(acpi_taskq);

void
acpi_addtask(struct acpi_softc *sc, void (*handler)(void *, int),
    void *arg0, int arg1)
{
	struct acpi_taskq *wq;
	int s;

	wq = pool_get(&acpiwqpool, PR_ZERO | PR_NOWAIT);
	if (wq == NULL) {
		printf("unable to create task");
		return;
	}
	wq->handler = handler;
	wq->arg0 = arg0;
	wq->arg1 = arg1;

	s = splbio();
	SIMPLEQ_INSERT_TAIL(&acpi_taskq, wq, next);
	splx(s);
}

int
acpi_dotask(struct acpi_softc *sc)
{
	struct acpi_taskq *wq;
	int s;

	s = splbio();
	if (SIMPLEQ_EMPTY(&acpi_taskq)) {
		splx(s);

		/* we don't have anything to do */
		return (0);
	}
	wq = SIMPLEQ_FIRST(&acpi_taskq);
	SIMPLEQ_REMOVE_HEAD(&acpi_taskq, next);
	splx(s);

	wq->handler(wq->arg0, wq->arg1);

	pool_put(&acpiwqpool, wq);

	/* We did something */
	return (1);
}

#ifndef SMALL_KERNEL

int
is_ata(struct aml_node *node)
{
	return (aml_searchname(node, "_GTM") != NULL ||
	    aml_searchname(node, "_GTF") != NULL ||
	    aml_searchname(node, "_STM") != NULL ||
	    aml_searchname(node, "_SDD") != NULL);
}

int
is_ejectable(struct aml_node *node)
{
	return (aml_searchname(node, "_EJ0") != NULL);
}

int
is_ejectable_bay(struct aml_node *node)
{
	return ((is_ata(node) || is_ata(node->parent)) && is_ejectable(node));
}

#if NWD > 0
int
acpiide_notify(struct aml_node *node, int ntype, void *arg)
{
	struct idechnl 		*ide = arg;
	struct acpi_softc 	*sc = ide->sc;
	struct pciide_softc 	*wsc;
	struct device 		*dev;
	int 			b,d,f;
	int64_t 		sta;

	if (aml_evalinteger(sc, node, "_STA", 0, NULL, &sta) != 0)
		return (0);

	dnprintf(10, "IDE notify! %s %d status:%llx\n", aml_nodename(node),
	    ntype, sta);

	/* Walk device list looking for IDE device match */
	TAILQ_FOREACH(dev, &alldevs, dv_list) {
		if (strcmp(dev->dv_cfdata->cf_driver->cd_name, "pciide"))
			continue;

		wsc = (struct pciide_softc *)dev;
		pci_decompose_tag(NULL, wsc->sc_tag, &b, &d, &f);
		if (b != ACPI_PCI_BUS(ide->addr) ||
		    d != ACPI_PCI_DEV(ide->addr) ||
		    f != ACPI_PCI_FN(ide->addr))
			continue;
		dnprintf(10, "Found pciide: %s %x.%x.%x channel:%llx\n",
		    dev->dv_xname, b,d,f, ide->chnl);

		if (sta == 0 && ide->sta)
			wdcdetach(
			    &wsc->pciide_channels[ide->chnl].wdc_channel, 0);
		else if (sta && !ide->sta)
			wdcattach(
			    &wsc->pciide_channels[ide->chnl].wdc_channel);
		ide->sta = sta;
	}
	return (0);
}

int
acpi_foundide(struct aml_node *node, void *arg)
{
	struct acpi_softc 	*sc = arg;
	struct aml_node 	*pp;
	struct idechnl 		*ide;
	union amlpci_t 		pi;
	int 			lvl;

	/* Check if this is an ejectable bay */
	if (!is_ejectable_bay(node))
		return (0);

	ide = malloc(sizeof(struct idechnl), M_DEVBUF, M_NOWAIT | M_ZERO);
	ide->sc = sc;

	/* GTM/GTF can be at 2/3 levels:  pciX.ideX.channelX[.driveX] */
	lvl = 0;
	for (pp=node->parent; pp; pp=pp->parent) {
		lvl++;
		if (aml_searchname(pp, "_HID"))
			break;
	}

	/* Get PCI address and channel */
	if (lvl == 3) {
		aml_evalinteger(sc, node->parent, "_ADR", 0, NULL,
		    &ide->chnl);
		aml_rdpciaddr(node->parent->parent, &pi);
		ide->addr = pi.addr;
	} else if (lvl == 4) {
		aml_evalinteger(sc, node->parent->parent, "_ADR", 0, NULL,
		    &ide->chnl);
		aml_rdpciaddr(node->parent->parent->parent, &pi);
		ide->addr = pi.addr;
	}
	dnprintf(10, "%s %llx channel:%llx\n",
	    aml_nodename(node), ide->addr, ide->chnl);

	aml_evalinteger(sc, node, "_STA", 0, NULL, &ide->sta);
	dnprintf(10, "Got Initial STA: %llx\n", ide->sta);

	aml_register_notify(node, "acpiide", acpiide_notify, ide, 0);
	return (0);
}
#endif /* NWD > 0 */

void
acpi_sleep_task(void *arg0, int sleepmode)
{
	struct acpi_softc *sc = arg0;

#ifdef SUSPEND
	sleep_state(sc, sleepmode);
#endif
	/* Tell userland to recheck A/C and battery status */
	acpi_record_event(sc, APM_POWER_CHANGE);
}

#endif /* SMALL_KERNEL */

void
acpi_reset(void)
{
	uint32_t		 reset_as, reset_len;
	uint32_t		 value;
	struct acpi_softc	*sc = acpi_softc;
	struct acpi_fadt	*fadt = sc->sc_fadt;

	if (acpi_enabled == 0)
		return;

	/*
	 * RESET_REG_SUP is not properly set in some implementations,
	 * but not testing against it breaks more machines than it fixes
	 */
	if (fadt->hdr_revision <= 1 ||
	    !(fadt->flags & FADT_RESET_REG_SUP) || fadt->reset_reg.address == 0)
		return;

	value = fadt->reset_value;

	reset_as = fadt->reset_reg.register_bit_width / 8;
	if (reset_as == 0)
		reset_as = 1;

	reset_len = fadt->reset_reg.access_size;
	if (reset_len == 0)
		reset_len = reset_as;

	acpi_gasio(sc, ACPI_IOWRITE,
	    fadt->reset_reg.address_space_id,
	    fadt->reset_reg.address, reset_as, reset_len, &value);

	delay(100000);
}

void
acpi_gpe_task(void *arg0, int gpe)
{
	struct acpi_softc *sc = acpi_softc;
	struct gpe_block *pgpe = &sc->gpe_table[gpe];

	dnprintf(10, "handle gpe: %x\n", gpe);
	if (pgpe->handler && pgpe->active) {
		pgpe->active = 0;
		pgpe->handler(sc, gpe, pgpe->arg);
	}
}

void
acpi_pbtn_task(void *arg0, int dummy)
{
	struct acpi_softc *sc = arg0;
	extern int pwr_action;
	uint16_t en;
	int s;

	dnprintf(1,"power button pressed\n");

	/* Reset the latch and re-enable the GPE */
	s = splbio();
	en = acpi_read_pmreg(sc, ACPIREG_PM1_EN, 0);
	acpi_write_pmreg(sc, ACPIREG_PM1_EN,  0,
	    en | ACPI_PM1_PWRBTN_EN);
	splx(s);

#ifdef SUSPEND
	/* Ignore button events if we're resuming. */
	if (resuming())
		return;
#endif	/* SUSPEND */

	switch (pwr_action) {
	case 0:
		break;
	case 1:
		powerbutton_event();
		break;
#ifndef SMALL_KERNEL
	case 2:
		acpi_addtask(sc, acpi_sleep_task, sc, SLEEP_SUSPEND);
		break;
#endif
	}
}

void
acpi_sbtn_task(void *arg0, int dummy)
{
	struct acpi_softc *sc = arg0;
	uint16_t en;
	int s;

	dnprintf(1,"sleep button pressed\n");
	aml_notify_dev(ACPI_DEV_SBD, 0x80);

	/* Reset the latch and re-enable the GPE */
	s = splbio();
	en = acpi_read_pmreg(sc, ACPIREG_PM1_EN, 0);
	acpi_write_pmreg(sc, ACPIREG_PM1_EN,  0,
	    en | ACPI_PM1_SLPBTN_EN);
	splx(s);
}

int
acpi_interrupt(void *arg)
{
	struct acpi_softc *sc = (struct acpi_softc *)arg;
	uint32_t processed = 0, idx, jdx;
	uint16_t sts, en;
	int gpe;

	dnprintf(40, "ACPI Interrupt\n");
	for (idx = 0; idx < sc->sc_lastgpe; idx += 8) {
		sts = acpi_read_pmreg(sc, ACPIREG_GPE_STS, idx>>3);
		en  = acpi_read_pmreg(sc, ACPIREG_GPE_EN,  idx>>3);
		if (en & sts) {
			dnprintf(10, "GPE block: %.2x %.2x %.2x\n", idx, sts,
			    en);
			/* Mask the GPE until it is serviced */
			acpi_write_pmreg(sc, ACPIREG_GPE_EN, idx>>3, en & ~sts);
			for (jdx = 0; jdx < 8; jdx++) {
				if (!(en & sts & (1L << jdx)))
					continue;

				if (cpu_suspended) {
					cpu_suspended = 0;
					sc->sc_wakegpe = idx + jdx;
				}

				/* Signal this GPE */
				gpe = idx + jdx;
				sc->gpe_table[gpe].active = 1;
				dnprintf(10, "queue gpe: %x\n", gpe);
				acpi_addtask(sc, acpi_gpe_task, NULL, gpe);

				/*
				 * Edge interrupts need their STS bits cleared
				 * now.  Level interrupts will have their STS
				 * bits cleared just before they are
				 * re-enabled.
				 */
				if (sc->gpe_table[gpe].flags & GPE_EDGE)
					acpi_write_pmreg(sc,
					    ACPIREG_GPE_STS, idx>>3, 1L << jdx);

				processed = 1;
			}
		}
	}

	sts = acpi_read_pmreg(sc, ACPIREG_PM1_STS, 0);
	en  = acpi_read_pmreg(sc, ACPIREG_PM1_EN, 0);
	if (sts & en) {
		dnprintf(10,"GEN interrupt: %.4x\n", sts & en);
		sts &= en;
		if (sts & ACPI_PM1_PWRBTN_STS) {
			/* Mask and acknowledge */
			en &= ~ACPI_PM1_PWRBTN_EN;
			acpi_write_pmreg(sc, ACPIREG_PM1_EN, 0, en);
			acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0,
			    ACPI_PM1_PWRBTN_STS);
			sts &= ~ACPI_PM1_PWRBTN_STS;

			if (cpu_suspended) {
				cpu_suspended = 0;
				sc->sc_wakegpe = -1;
			}

			acpi_addtask(sc, acpi_pbtn_task, sc, 0);
		}
		if (sts & ACPI_PM1_SLPBTN_STS) {
			/* Mask and acknowledge */
			en &= ~ACPI_PM1_SLPBTN_EN;
			acpi_write_pmreg(sc, ACPIREG_PM1_EN, 0, en);
			acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0,
			    ACPI_PM1_SLPBTN_STS);
			sts &= ~ACPI_PM1_SLPBTN_STS;

			if (cpu_suspended) {
				cpu_suspended = 0;
				sc->sc_wakegpe = -2;
			}

			acpi_addtask(sc, acpi_sbtn_task, sc, 0);
		}
		if (sts) {
			printf("%s: PM1 stuck (en 0x%x st 0x%x), clearing\n",
			    sc->sc_dev.dv_xname, en, sts);
			acpi_write_pmreg(sc, ACPIREG_PM1_EN, 0, en & ~sts);
			acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0, sts);
		}
		processed = 1;
	}

	if (processed) {
		acpi_wakeup(sc);
	}

	return (processed);
}

int
acpi_add_device(struct aml_node *node, void *arg)
{
	static int nacpicpus = 0;
	struct device *self = arg;
	struct acpi_softc *sc = arg;
	struct acpi_attach_args aaa;
	struct aml_value res;
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	int proc_id = -1;
	int64_t sta;

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_node = node;
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	if (node == NULL || node->value == NULL)
		return 0;

	switch (node->value->type) {
	case AML_OBJTYPE_PROCESSOR:
		if (sc->sc_skip_processor != 0)
			return 0;
		if (nacpicpus >= ncpus)
			return 0;
		if (aml_evalnode(sc, aaa.aaa_node, 0, NULL, &res) == 0) {
			if (res.type == AML_OBJTYPE_PROCESSOR)
				proc_id = res.v_processor.proc_id;
			aml_freevalue(&res);
		}
		CPU_INFO_FOREACH(cii, ci) {
			if (ci->ci_acpi_proc_id == proc_id)
				break;
		}
		if (ci == NULL)
			return 0;
		nacpicpus++;

		aaa.aaa_name = "acpicpu";
		break;
	case AML_OBJTYPE_THERMZONE:
		sta = acpi_getsta(sc, node);
		if ((sta & STA_PRESENT) == 0)
			return 0;

		aaa.aaa_name = "acpitz";
		break;
	case AML_OBJTYPE_POWERRSRC:
		aaa.aaa_name = "acpipwrres";
		break;
	default:
		return 0;
	}
	config_found(self, &aaa, acpi_print);
	return 0;
}

void
acpi_enable_onegpe(struct acpi_softc *sc, int gpe)
{
	uint8_t mask, en;

	/* Read enabled register */
	mask = (1L << (gpe & 7));
	en = acpi_read_pmreg(sc, ACPIREG_GPE_EN, gpe>>3);
	dnprintf(50, "enabling GPE %.2x (current: %sabled) %.2x\n",
	    gpe, (en & mask) ? "en" : "dis", en);
	acpi_write_pmreg(sc, ACPIREG_GPE_EN, gpe>>3, en | mask);
}

/* Clear all GPEs */
void
acpi_disable_allgpes(struct acpi_softc *sc)
{
	int idx;

	for (idx = 0; idx < sc->sc_lastgpe; idx += 8) {
		acpi_write_pmreg(sc, ACPIREG_GPE_EN, idx >> 3, 0);
		acpi_write_pmreg(sc, ACPIREG_GPE_STS, idx >> 3, -1);
	}
}

/* Enable runtime GPEs */
void
acpi_enable_rungpes(struct acpi_softc *sc)
{
	int idx;

	for (idx = 0; idx < sc->sc_lastgpe; idx++)
		if (sc->gpe_table[idx].handler)
			acpi_enable_onegpe(sc, idx);
}

/* Enable wakeup GPEs */
void
acpi_enable_wakegpes(struct acpi_softc *sc, int state)
{
	struct acpi_wakeq *wentry;

	SIMPLEQ_FOREACH(wentry, &sc->sc_wakedevs, q_next) {
		dnprintf(10, "%.4s(S%d) gpe %.2x\n", wentry->q_node->name,
		    wentry->q_state,
		    wentry->q_gpe);
		if (wentry->q_enabled && state <= wentry->q_state)
			acpi_enable_onegpe(sc, wentry->q_gpe);
	}
}

int
acpi_set_gpehandler(struct acpi_softc *sc, int gpe, int (*handler)
    (struct acpi_softc *, int, void *), void *arg, int flags)
{
	struct gpe_block *ptbl;

	ptbl = acpi_find_gpe(sc, gpe);
	if (ptbl == NULL || handler == NULL)
		return -EINVAL;
	if ((flags & GPE_LEVEL) && (flags & GPE_EDGE))
		return -EINVAL;
	if (!(flags & (GPE_LEVEL | GPE_EDGE)))
		return -EINVAL;
	if (ptbl->handler != NULL)
		printf("%s: GPE 0x%.2x already enabled\n", DEVNAME(sc), gpe);

	dnprintf(50, "Adding GPE handler 0x%.2x (%s)\n", gpe,
	    (flags & GPE_EDGE ? "edge" : "level"));
	ptbl->handler = handler;
	ptbl->arg = arg;
	ptbl->flags = flags;

	return (0);
}

int
acpi_gpe(struct acpi_softc *sc, int gpe, void *arg)
{
	struct aml_node *node = arg;
	uint8_t mask, en;

	dnprintf(10, "handling GPE %.2x\n", gpe);
	aml_evalnode(sc, node, 0, NULL, NULL);

	mask = (1L << (gpe & 7));
	if (sc->gpe_table[gpe].flags & GPE_LEVEL)
		acpi_write_pmreg(sc, ACPIREG_GPE_STS, gpe>>3, mask);
	en = acpi_read_pmreg(sc, ACPIREG_GPE_EN,  gpe>>3);
	acpi_write_pmreg(sc, ACPIREG_GPE_EN,  gpe>>3, en | mask);
	return (0);
}

/* Discover Devices that can wakeup the system
 * _PRW returns a package
 *  pkg[0] = integer (FADT gpe bit) or package (gpe block,gpe bit)
 *  pkg[1] = lowest sleep state
 *  pkg[2+] = power resource devices (optional)
 *
 * To enable wakeup devices:
 *    Evaluate _ON method in each power resource device
 *    Evaluate _PSW method
 */
int
acpi_foundprw(struct aml_node *node, void *arg)
{
	struct acpi_softc *sc = arg;
	struct acpi_wakeq *wq;
	int64_t sta;

	sta = acpi_getsta(sc, node->parent);
	if ((sta & STA_PRESENT) == 0)
		return 0;

	wq = malloc(sizeof(struct acpi_wakeq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (wq == NULL)
		return 0;

	wq->q_wakepkg = malloc(sizeof(struct aml_value), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (wq->q_wakepkg == NULL) {
		free(wq, M_DEVBUF, sizeof(*wq));
		return 0;
	}
	dnprintf(10, "Found _PRW (%s)\n", node->parent->name);
	aml_evalnode(sc, node, 0, NULL, wq->q_wakepkg);
	wq->q_node = node->parent;
	wq->q_gpe = -1;

	/* Get GPE of wakeup device, and lowest sleep level */
	if (wq->q_wakepkg->type == AML_OBJTYPE_PACKAGE &&
	    wq->q_wakepkg->length >= 2) {
		if (wq->q_wakepkg->v_package[0]->type == AML_OBJTYPE_INTEGER)
			wq->q_gpe = wq->q_wakepkg->v_package[0]->v_integer;
		if (wq->q_wakepkg->v_package[1]->type == AML_OBJTYPE_INTEGER)
			wq->q_state = wq->q_wakepkg->v_package[1]->v_integer;
		wq->q_enabled = 0;
	}
	SIMPLEQ_INSERT_TAIL(&sc->sc_wakedevs, wq, q_next);
	return 0;
}

int
acpi_toggle_wakedev(struct acpi_softc *sc, struct aml_node *node, int enable)
{
	struct acpi_wakeq *wentry;
	int ret = -1;

	SIMPLEQ_FOREACH(wentry, &sc->sc_wakedevs, q_next) {
		if (wentry->q_node == node) {
			wentry->q_enabled = enable ? 1 : 0;
			dnprintf(10, "%.4s(S%d) gpe %.2x %sabled\n",
			    wentry->q_node->name, wentry->q_state,
			    wentry->q_gpe, enable ? "en" : "dis");
			ret = 0;
			break;
		}
	}

	return ret;
}

struct gpe_block *
acpi_find_gpe(struct acpi_softc *sc, int gpe)
{
	if (gpe >= sc->sc_lastgpe)
		return NULL;
	return &sc->gpe_table[gpe];
}

void
acpi_init_gpes(struct acpi_softc *sc)
{
	struct aml_node *gpe;
	char name[12];
	int  idx;

	sc->sc_lastgpe = sc->sc_fadt->gpe0_blk_len << 2;
	dnprintf(50, "Last GPE: %.2x\n", sc->sc_lastgpe);

	/* Allocate GPE table */
	sc->gpe_table = mallocarray(sc->sc_lastgpe, sizeof(struct gpe_block),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	/* Clear GPE status */
	acpi_disable_allgpes(sc);
	for (idx = 0; idx < sc->sc_lastgpe; idx++) {
		/* Search Level-sensitive GPES */
		snprintf(name, sizeof(name), "\\_GPE._L%.2X", idx);
		gpe = aml_searchname(sc->sc_root, name);
		if (gpe != NULL)
			acpi_set_gpehandler(sc, idx, acpi_gpe, gpe, GPE_LEVEL);
		if (gpe == NULL) {
			/* Search Edge-sensitive GPES */
			snprintf(name, sizeof(name), "\\_GPE._E%.2X", idx);
			gpe = aml_searchname(sc->sc_root, name);
			if (gpe != NULL)
				acpi_set_gpehandler(sc, idx, acpi_gpe, gpe,
				    GPE_EDGE);
		}
	}
	aml_find_node(sc->sc_root, "_PRW", acpi_foundprw, sc);
}

void
acpi_init_pm(struct acpi_softc *sc)
{
	sc->sc_tts = aml_searchname(sc->sc_root, "_TTS");
	sc->sc_pts = aml_searchname(sc->sc_root, "_PTS");
	sc->sc_wak = aml_searchname(sc->sc_root, "_WAK");
	sc->sc_bfs = aml_searchname(sc->sc_root, "_BFS");
	sc->sc_gts = aml_searchname(sc->sc_root, "_GTS");
	sc->sc_sst = aml_searchname(sc->sc_root, "_SI_._SST");
}

#ifndef SMALL_KERNEL

void
acpi_init_states(struct acpi_softc *sc)
{
	struct aml_value res;
	char name[8];
	int i;

	printf("\n%s: sleep states", DEVNAME(sc));
	for (i = ACPI_STATE_S0; i <= ACPI_STATE_S5; i++) {
		snprintf(name, sizeof(name), "_S%d_", i);
		sc->sc_sleeptype[i].slp_typa = -1;
		sc->sc_sleeptype[i].slp_typb = -1;
		if (aml_evalname(sc, sc->sc_root, name, 0, NULL, &res) != 0)
			continue;
		if (res.type != AML_OBJTYPE_PACKAGE) {
			aml_freevalue(&res);
			continue;
		}
		sc->sc_sleeptype[i].slp_typa = aml_val2int(res.v_package[0]);
		sc->sc_sleeptype[i].slp_typb = aml_val2int(res.v_package[1]);
		aml_freevalue(&res);

		printf(" S%d", i);
		if (i == 0 && (sc->sc_fadt->flags & FADT_POWER_S0_IDLE_CAPABLE))
			printf("ix");
	}
}

void
acpi_sleep_pm(struct acpi_softc *sc, int state)
{
	uint16_t rega, regb, regra, regrb;
	int retry = 0;

	intr_disable();

	/* Clear WAK_STS bit */
	acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0, ACPI_PM1_WAK_STS);

	/* Disable BM arbitration at deep sleep and beyond */
	if (state >= ACPI_STATE_S3 &&
	    sc->sc_fadt->pm2_cnt_blk && sc->sc_fadt->pm2_cnt_len)
		acpi_write_pmreg(sc, ACPIREG_PM2_CNT, 0, ACPI_PM2_ARB_DIS);

	/* Write SLP_TYPx values */
	rega = acpi_read_pmreg(sc, ACPIREG_PM1A_CNT, 0);
	regb = acpi_read_pmreg(sc, ACPIREG_PM1B_CNT, 0);
	rega &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
	regb &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
	rega |= ACPI_PM1_SLP_TYPX(sc->sc_sleeptype[state].slp_typa);
	regb |= ACPI_PM1_SLP_TYPX(sc->sc_sleeptype[state].slp_typb);
	acpi_write_pmreg(sc, ACPIREG_PM1A_CNT, 0, rega);
	acpi_write_pmreg(sc, ACPIREG_PM1B_CNT, 0, regb);

	/* Loop on WAK_STS, setting the SLP_EN bits once in a while */
	rega |= ACPI_PM1_SLP_EN;
	regb |= ACPI_PM1_SLP_EN;
	while (1) {
		if (retry == 0) {
			acpi_write_pmreg(sc, ACPIREG_PM1A_CNT, 0, rega);
			acpi_write_pmreg(sc, ACPIREG_PM1B_CNT, 0, regb);
		}
		retry = (retry + 1) % 100000;

		regra = acpi_read_pmreg(sc, ACPIREG_PM1A_STS, 0);
		regrb = acpi_read_pmreg(sc, ACPIREG_PM1B_STS, 0);
		if ((regra & ACPI_PM1_WAK_STS) ||
		    (regrb & ACPI_PM1_WAK_STS))
			break;
	}
}

uint32_t acpi_force_bm;

void
acpi_resume_pm(struct acpi_softc *sc, int fromstate)
{
	uint16_t rega, regb, en;

	/* Write SLP_TYPx values */
	rega = acpi_read_pmreg(sc, ACPIREG_PM1A_CNT, 0);
	regb = acpi_read_pmreg(sc, ACPIREG_PM1B_CNT, 0);
	rega &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
	regb &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
	rega |= ACPI_PM1_SLP_TYPX(sc->sc_sleeptype[ACPI_STATE_S0].slp_typa);
	regb |= ACPI_PM1_SLP_TYPX(sc->sc_sleeptype[ACPI_STATE_S0].slp_typb);
	acpi_write_pmreg(sc, ACPIREG_PM1A_CNT, 0, rega);
	acpi_write_pmreg(sc, ACPIREG_PM1B_CNT, 0, regb);

	/* Force SCI_EN on resume to fix horribly broken machines */
	acpi_write_pmreg(sc, ACPIREG_PM1_CNT, 0,
	    ACPI_PM1_SCI_EN | acpi_force_bm);

	/* Clear fixed event status */
	acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0, ACPI_PM1_ALL_STS);

	/* acpica-reference.pdf page 148 says do not call _BFS */
	/* 1st resume AML step: _BFS(fromstate) */
	aml_node_setval(sc, sc->sc_bfs, fromstate);

	/* Enable runtime GPEs */
	acpi_disable_allgpes(sc);
	acpi_enable_rungpes(sc);

	/* 2nd resume AML step: _WAK(fromstate) */
	aml_node_setval(sc, sc->sc_wak, fromstate);

	/* Clear WAK_STS bit */
	acpi_write_pmreg(sc, ACPIREG_PM1_STS, 0, ACPI_PM1_WAK_STS);

	en = acpi_read_pmreg(sc, ACPIREG_PM1_EN, 0);
	if (!(sc->sc_fadt->flags & FADT_PWR_BUTTON))
		en |= ACPI_PM1_PWRBTN_EN;
	if (!(sc->sc_fadt->flags & FADT_SLP_BUTTON))
		en |= ACPI_PM1_SLPBTN_EN;
	acpi_write_pmreg(sc, ACPIREG_PM1_EN, 0, en);

	/*
	 * If PM2 exists, re-enable BM arbitration (reportedly some
	 * BIOS forget to)
	 */
	if (sc->sc_fadt->pm2_cnt_blk && sc->sc_fadt->pm2_cnt_len) {
		rega = acpi_read_pmreg(sc, ACPIREG_PM2_CNT, 0);
		rega &= ~ACPI_PM2_ARB_DIS;
		acpi_write_pmreg(sc, ACPIREG_PM2_CNT, 0, rega);
	}
}

/* Set the indicator light to some state */
void
acpi_indicator(struct acpi_softc *sc, int led_state)
{
	static int save_led_state = -1;

	if (save_led_state != led_state) {
		aml_node_setval(sc, sc->sc_sst, led_state);
		save_led_state = led_state;
	}
}

/* XXX
 * We are going to do AML execution but are not in the acpi thread.
 * We do not know if the acpi thread is sleeping on acpiec in some
 * intermediate context.  Wish us luck.
 */
void
acpi_powerdown(void)
{
	int state = ACPI_STATE_S5, s;
	struct acpi_softc *sc = acpi_softc;

	if (acpi_enabled == 0)
		return;

	s = splhigh();
	intr_disable();
	cold = 1;

	/* 1st powerdown AML step: _PTS(tostate) */
	aml_node_setval(sc, sc->sc_pts, state);

	acpi_disable_allgpes(sc);
	acpi_enable_wakegpes(sc, state);

	/* 2nd powerdown AML step: _GTS(tostate) */
	aml_node_setval(sc, sc->sc_gts, state);

	acpi_sleep_pm(sc, state);
	panic("acpi S5 transition did not happen");
	while (1)
		;
}

#endif /* SMALL_KERNEL */

int
acpi_map_address(struct acpi_softc *sc, struct acpi_gas *gas, bus_addr_t base,
    bus_size_t size, bus_space_handle_t *pioh, bus_space_tag_t *piot)
{
	int iospace = GAS_SYSTEM_IOSPACE;

	/* No GAS structure, default to I/O space */
	if (gas != NULL) {
		base += gas->address;
		iospace = gas->address_space_id;
	}
	switch (iospace) {
	case GAS_SYSTEM_MEMORY:
		*piot = sc->sc_memt;
		break;
	case GAS_SYSTEM_IOSPACE:
		*piot = sc->sc_iot;
		break;
	default:
		return -1;
	}
	if (bus_space_map(*piot, base, size, 0, pioh))
		return -1;

	return 0;
}

void
acpi_wakeup(void *arg)
{
	struct acpi_softc  *sc = (struct acpi_softc *)arg;

	sc->sc_threadwaiting = 0;
	wakeup(sc);
}


void
acpi_thread(void *arg)
{
	struct acpi_thread *thread = arg;
	struct acpi_softc  *sc = thread->sc;
	extern int aml_busy;
	int s;

	/* AML/SMI cannot be trusted -- only run on the BSP */
	sched_peg_curproc(&cpu_info_primary);

	rw_enter_write(&sc->sc_lck);

	/*
	 * If we have an interrupt handler, we can get notification
	 * when certain status bits changes in the ACPI registers,
	 * so let us enable some events we can forward to userland
	 */
	if (sc->sc_interrupt) {
		int16_t en;

		dnprintf(1,"slpbtn:%c  pwrbtn:%c\n",
		    sc->sc_fadt->flags & FADT_SLP_BUTTON ? 'n' : 'y',
		    sc->sc_fadt->flags & FADT_PWR_BUTTON ? 'n' : 'y');
		dnprintf(10, "Enabling acpi interrupts...\n");
		sc->sc_threadwaiting = 1;

		/* Enable Sleep/Power buttons if they exist */
		s = splbio();
		en = acpi_read_pmreg(sc, ACPIREG_PM1_EN, 0);
		if (!(sc->sc_fadt->flags & FADT_PWR_BUTTON))
			en |= ACPI_PM1_PWRBTN_EN;
		if (!(sc->sc_fadt->flags & FADT_SLP_BUTTON))
			en |= ACPI_PM1_SLPBTN_EN;
		acpi_write_pmreg(sc, ACPIREG_PM1_EN, 0, en);

		/* Enable handled GPEs here */
		acpi_enable_rungpes(sc);
		splx(s);
	}

	while (thread->running) {
		s = splbio();
		while (sc->sc_threadwaiting) {
			dnprintf(10, "acpi thread going to sleep...\n");
			rw_exit_write(&sc->sc_lck);
			tsleep_nsec(sc, PWAIT, "acpi0", INFSLP);
			rw_enter_write(&sc->sc_lck);
		}
		sc->sc_threadwaiting = 1;
		splx(s);
		if (aml_busy) {
			panic("thread woke up to find aml was busy");
			continue;
		}

		/* Run ACPI taskqueue */
		while(acpi_dotask(acpi_softc))
			;
	}
	free(thread, M_DEVBUF, sizeof(*thread));

	kthread_exit(0);
}

void
acpi_create_thread(void *arg)
{
	struct acpi_softc *sc = arg;

	if (kthread_create(acpi_thread, sc->sc_thread, NULL, DEVNAME(sc))
	    != 0)
		printf("%s: unable to create isr thread, GPEs disabled\n",
		    DEVNAME(sc));
}

#ifdef __arm64__
int
acpi_foundsectwo(struct aml_node *node, void *arg)
{
	struct acpi_softc *sc = (struct acpi_softc *)arg;
	struct device *self = (struct device *)arg;
	struct acpi_attach_args aaa;

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_name = "acpisectwo";

	config_found(self, &aaa, acpi_print);

	return 0;
}
#endif

int
acpi_foundec(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct device		*self = (struct device *)arg;
	const char		*dev;
	struct aml_value	 res;
	struct acpi_attach_args	aaa;

	if (aml_evalnode(sc, node, 0, NULL, &res) != 0)
		return 0;

	switch (res.type) {
	case AML_OBJTYPE_STRING:
		dev = res.v_string;
		break;
	case AML_OBJTYPE_INTEGER:
		dev = aml_eisaid(aml_val2int(&res));
		break;
	default:
		dev = "unknown";
		break;
	}

	if (strcmp(dev, ACPI_DEV_ECD))
		return 0;

	/* Check if we're already attached */
	if (sc->sc_ec && sc->sc_ec->sc_devnode == node->parent)
		return 0;

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_dev = dev;
	aaa.aaa_name = "acpiec";
	config_found(self, &aaa, acpi_print);
	aml_freevalue(&res);

	return 0;
}

int
acpi_foundsony(struct aml_node *node, void *arg)
{
	struct acpi_softc *sc = (struct acpi_softc *)arg;
	struct device *self = (struct device *)arg;
	struct acpi_attach_args aaa;

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_name = "acpisony";

	config_found(self, &aaa, acpi_print);

	return 0;
}

/* Support for _DSD Device Properties. */

int
acpi_getprop(struct aml_node *node, const char *prop, void *buf, int buflen)
{
	struct aml_value dsd;
	int i;

	/* daffd814-6eba-4d8c-8a91-bc9bbf4aa301 */
	static uint8_t prop_guid[] = {
		0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
		0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01,
	};

	if (aml_evalname(acpi_softc, node, "_DSD", 0, NULL, &dsd))
		return -1;

	if (dsd.type != AML_OBJTYPE_PACKAGE || dsd.length != 2 ||
	    dsd.v_package[0]->type != AML_OBJTYPE_BUFFER ||
	    dsd.v_package[1]->type != AML_OBJTYPE_PACKAGE)
		return -1;

	/* Check UUID. */
	if (dsd.v_package[0]->length != sizeof(prop_guid) ||
	    memcmp(dsd.v_package[0]->v_buffer, prop_guid,
	    sizeof(prop_guid)) != 0)
		return -1;

	/* Check properties. */
	for (i = 0; i < dsd.v_package[1]->length; i++) {
		struct aml_value *res = dsd.v_package[1]->v_package[i];
		struct aml_value *val;
		int len;

		if (res->type != AML_OBJTYPE_PACKAGE || res->length != 2 ||
		    res->v_package[0]->type != AML_OBJTYPE_STRING ||
		    strcmp(res->v_package[0]->v_string, prop) != 0)
			continue;

		val = res->v_package[1];
		if (val->type == AML_OBJTYPE_OBJREF)
			val = val->v_objref.ref;

		len = val->length;
		switch (val->type) {
		case AML_OBJTYPE_BUFFER:
			memcpy(buf, val->v_buffer, min(len, buflen));
			return len;
		case AML_OBJTYPE_STRING:
			memcpy(buf, val->v_string, min(len, buflen));
			return len;
		}
	}

	return -1;
}

uint64_t
acpi_getpropint(struct aml_node *node, const char *prop, uint64_t defval)
{
	struct aml_value dsd;
	int i;

	/* daffd814-6eba-4d8c-8a91-bc9bbf4aa301 */
	static uint8_t prop_guid[] = {
		0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
		0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01,
	};

	if (aml_evalname(acpi_softc, node, "_DSD", 0, NULL, &dsd))
		return defval;

	if (dsd.type != AML_OBJTYPE_PACKAGE || dsd.length != 2 ||
	    dsd.v_package[0]->type != AML_OBJTYPE_BUFFER ||
	    dsd.v_package[1]->type != AML_OBJTYPE_PACKAGE)
		return defval;

	/* Check UUID. */
	if (dsd.v_package[0]->length != sizeof(prop_guid) ||
	    memcmp(dsd.v_package[0]->v_buffer, prop_guid,
	    sizeof(prop_guid)) != 0)
		return defval;

	/* Check properties. */
	for (i = 0; i < dsd.v_package[1]->length; i++) {
		struct aml_value *res = dsd.v_package[1]->v_package[i];
		struct aml_value *val;

		if (res->type != AML_OBJTYPE_PACKAGE || res->length != 2 ||
		    res->v_package[0]->type != AML_OBJTYPE_STRING ||
		    strcmp(res->v_package[0]->v_string, prop) != 0)
			continue;

		val = res->v_package[1];
		if (val->type == AML_OBJTYPE_OBJREF)
			val = val->v_objref.ref;

		if (val->type == AML_OBJTYPE_INTEGER)
			return val->v_integer;
	}

	return defval;
}

int
acpi_parsehid(struct aml_node *node, void *arg, char *outcdev, char *outdev,
    size_t devlen)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct aml_value	 res, *cid;
	const char		*dev;

	/* NB aml_eisaid returns a static buffer, this must come first */
	if (aml_evalname(acpi_softc, node->parent, "_CID", 0, NULL, &res) == 0) {
		if (res.type == AML_OBJTYPE_PACKAGE && res.length >= 1) {
			cid = res.v_package[0];
		} else {
			cid = &res;
		}
		switch (cid->type) {
		case AML_OBJTYPE_STRING:
			dev = cid->v_string;
			break;
		case AML_OBJTYPE_INTEGER:
			dev = aml_eisaid(aml_val2int(cid));
			break;
		default:
			dev = "unknown";
			break;
		}
		strlcpy(outcdev, dev, devlen);
		aml_freevalue(&res);

		dnprintf(10, "compatible with device: %s\n", outcdev);
	} else {
		outcdev[0] = '\0';
	}

	dnprintf(10, "found hid device: %s ", node->parent->name);
	if (aml_evalnode(sc, node, 0, NULL, &res) != 0)
		return (1);

	switch (res.type) {
	case AML_OBJTYPE_STRING:
		dev = res.v_string;
		break;
	case AML_OBJTYPE_INTEGER:
		dev = aml_eisaid(aml_val2int(&res));
		break;
	default:
		dev = "unknown";
		break;
	}
	dnprintf(10, "	device: %s\n", dev);

	strlcpy(outdev, dev, devlen);

	aml_freevalue(&res);

	return (0);
}

/* Devices for which we don't want to attach a driver */
const char *acpi_skip_hids[] = {
	"INT0800",	/* Intel 82802Firmware Hub Device */
	"PNP0000",	/* 8259-compatible Programmable Interrupt Controller */
	"PNP0001",	/* EISA Interrupt Controller */
	"PNP0100",	/* PC-class System Timer */
	"PNP0103",	/* HPET System Timer */
	"PNP0200",	/* PC-class DMA Controller */
	"PNP0201",	/* EISA DMA Controller */
	"PNP0800",	/* Microsoft Sound System Compatible Device */
	"PNP0C01",	/* System Board */
	"PNP0C02",	/* PNP Motherboard Resources */
	"PNP0C04",	/* x87-compatible Floating Point Processing Unit */
	"PNP0C09",	/* Embedded Controller Device */
	"PNP0C0F",	/* PCI Interrupt Link Device */
	NULL
};

/* ISA devices for which we attach a driver later */
const char *acpi_isa_hids[] = {
	"PNP0400",	/* Standard LPT Parallel Port */
	"PNP0401",	/* ECP Parallel Port */
	"PNP0700",	/* PC-class Floppy Disk Controller */
	NULL
};

/* Overly abundant devices to avoid printing details for */
const char *acpi_quiet_hids[] = {
	"ACPI0007",
	NULL
};

void
acpi_attach_deps(struct acpi_softc *sc, struct aml_node *node)
{
	struct aml_value res, *val;
	struct aml_node *dep;
	int i;

	if (aml_evalname(sc, node, "_DEP", 0, NULL, &res))
		return;

	if (res.type != AML_OBJTYPE_PACKAGE)
		return;

	for (i = 0; i < res.length; i++) {
		val = res.v_package[i];
		if (val->type == AML_OBJTYPE_NAMEREF) {
			node = aml_searchrel(node,
			    aml_getname(val->v_nameref));
			if (node)
				val = node->value;
		}
		if (val->type == AML_OBJTYPE_OBJREF)
			val = val->v_objref.ref;
		if (val->type != AML_OBJTYPE_DEVICE)
			continue;
		dep = val->node;
		if (dep == NULL || dep->attached)
			continue;
		dep = aml_searchname(dep, "_HID");
		if (dep)
			acpi_foundhid(dep, sc);
	}

	aml_freevalue(&res);
}

int
acpi_parse_resources(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpi_attach_args *aaa = arg;
	int type = AML_CRSTYPE(crs);
	uint8_t flags;

	switch (type) {
	case SR_IOPORT:
	case SR_FIXEDPORT:
	case LR_MEM24:
	case LR_MEM32:
	case LR_MEM32FIXED:
	case LR_WORD:
	case LR_DWORD:
	case LR_QWORD:
		if (aaa->aaa_naddr >= nitems(aaa->aaa_addr))
			return 0;
		break;
	case SR_IRQ:
	case LR_EXTIRQ:
		if (aaa->aaa_nirq >= nitems(aaa->aaa_irq))
			return 0;
	}

	switch (type) {
	case SR_IOPORT:
	case SR_FIXEDPORT:
		aaa->aaa_bst[aaa->aaa_naddr] = aaa->aaa_iot;
		break;
	case LR_MEM24:
	case LR_MEM32:
	case LR_MEM32FIXED:
		aaa->aaa_bst[aaa->aaa_naddr] = aaa->aaa_memt;
		break;
	case LR_WORD:
	case LR_DWORD:
	case LR_QWORD:
		switch (crs->lr_word.type) {
		case LR_TYPE_MEMORY:
			aaa->aaa_bst[aaa->aaa_naddr] = aaa->aaa_memt;
			break;
		case LR_TYPE_IO:
			aaa->aaa_bst[aaa->aaa_naddr] = aaa->aaa_iot;
			break;
		default:
			/* Bus number range or something else; skip. */
			return 0;
		}
	}

	switch (type) {
	case SR_IOPORT:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->sr_ioport._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->sr_ioport._len;
		aaa->aaa_naddr++;
		break;
	case SR_FIXEDPORT:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->sr_fioport._bas;
		aaa->aaa_size[aaa->aaa_naddr] = crs->sr_fioport._len;
		aaa->aaa_naddr++;
		break;
	case LR_MEM24:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_m24._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_m24._len;
		aaa->aaa_naddr++;
		break;
	case LR_MEM32:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_m32._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_m32._len;
		aaa->aaa_naddr++;
		break;
	case LR_MEM32FIXED:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_m32fixed._bas;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_m32fixed._len;
		aaa->aaa_naddr++;
		break;
	case LR_WORD:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_word._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_word._len;
		aaa->aaa_naddr++;
		break;
	case LR_DWORD:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_dword._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_dword._len;
		aaa->aaa_naddr++;
		break;
	case LR_QWORD:
		aaa->aaa_addr[aaa->aaa_naddr] = crs->lr_qword._min;
		aaa->aaa_size[aaa->aaa_naddr] = crs->lr_qword._len;
		aaa->aaa_naddr++;
		break;
	case SR_IRQ:
		aaa->aaa_irq[aaa->aaa_nirq] = ffs(crs->sr_irq.irq_mask) - 1;
		/* Default is exclusive, active-high, edge triggered. */
		if (AML_CRSLEN(crs) < 4)
			flags = SR_IRQ_MODE;
		else
			flags = crs->sr_irq.irq_flags;
		/* Map flags to those of the extended interrupt descriptor. */
		if (flags & SR_IRQ_SHR)
			aaa->aaa_irq_flags[aaa->aaa_nirq] |= LR_EXTIRQ_SHR;
		if (flags & SR_IRQ_POLARITY)
			aaa->aaa_irq_flags[aaa->aaa_nirq] |= LR_EXTIRQ_POLARITY;
		if (flags & SR_IRQ_MODE)
			aaa->aaa_irq_flags[aaa->aaa_nirq] |= LR_EXTIRQ_MODE;
		aaa->aaa_nirq++;
		break;
	case LR_EXTIRQ:
		aaa->aaa_irq[aaa->aaa_nirq] = crs->lr_extirq.irq[0];
		aaa->aaa_irq_flags[aaa->aaa_nirq] = crs->lr_extirq.flags;
		aaa->aaa_nirq++;
		break;
	}

	return 0;
}

void
acpi_parse_crs(struct acpi_softc *sc, struct acpi_attach_args *aaa)
{
	struct aml_value res;

	if (aml_evalname(sc, aaa->aaa_node, "_CRS", 0, NULL, &res))
		return;

	aml_parse_resource(&res, acpi_parse_resources, aaa);
}

int
acpi_foundhid(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct device		*self = (struct device *)arg;
	char		 	 cdev[32];
	char		 	 dev[32];
	struct acpi_attach_args	 aaa;
	int64_t			 sta;
	int64_t			 cca;
#ifndef SMALL_KERNEL
	int			 i;
#endif

	if (acpi_parsehid(node, arg, cdev, dev, sizeof(dev)) != 0)
		return (0);

	sta = acpi_getsta(sc, node->parent);
	if ((sta & STA_PRESENT) == 0 && (sta & STA_DEV_OK) == 0)
		return (1);
	if ((sta & STA_ENABLED) == 0)
		return (0);

	if (aml_evalinteger(sc, node->parent, "_CCA", 0, NULL, &cca))
		cca = 1;

	acpi_attach_deps(sc, node->parent);

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_dmat = cca ? sc->sc_cc_dmat : sc->sc_ci_dmat;
	aaa.aaa_node = node->parent;
	aaa.aaa_dev = dev;
	aaa.aaa_cdev = cdev;

#ifndef SMALL_KERNEL
	if (!strcmp(cdev, ACPI_DEV_MOUSE)) {
		for (i = 0; i < nitems(sbtn_pnp); i++) {
			if (!strcmp(dev, sbtn_pnp[i])) {
				mouse_has_softbtn = 1;
				break;
			}
		}
	}
#endif

	if (acpi_matchhids(&aaa, acpi_skip_hids, "none") ||
	    acpi_matchhids(&aaa, acpi_isa_hids, "none"))
		return (0);

	acpi_parse_crs(sc, &aaa);

	aaa.aaa_dmat = acpi_iommu_device_map(node->parent, aaa.aaa_dmat);

	if (!node->parent->attached) {
		node->parent->attached = 1;
		if (acpi_matchhids(&aaa, acpi_quiet_hids, "none"))
			config_found(self, &aaa, acpi_noprint);
		else
			config_found(self, &aaa, acpi_print);
	}

	return (0);
}

#ifndef SMALL_KERNEL
int
acpi_founddock(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct device		*self = (struct device *)arg;
	struct acpi_attach_args	aaa;

	dnprintf(10, "found dock entry: %s\n", node->parent->name);

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_name = "acpidock";

	config_found(self, &aaa, acpi_print);

	return 0;
}

int
acpi_foundvideo(struct aml_node *node, void *arg)
{
	struct acpi_softc *sc = (struct acpi_softc *)arg;
	struct device *self = (struct device *)arg;
	struct acpi_attach_args	aaa;

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_name = "acpivideo";

	config_found(self, &aaa, acpi_print);

	return (0);
}

int
acpi_foundsbs(struct aml_node *node, void *arg)
{
	struct acpi_softc	*sc = (struct acpi_softc *)arg;
	struct device		*self = (struct device *)arg;
	char		 	 cdev[32], dev[32];
	struct acpi_attach_args	 aaa;
	int64_t			 sta;

	if (acpi_parsehid(node, arg, cdev, dev, sizeof(dev)) != 0)
		return (0);

	sta = acpi_getsta(sc, node->parent);
	if ((sta & STA_PRESENT) == 0)
		return (0);

	acpi_attach_deps(sc, node->parent);

	if (strcmp(dev, ACPI_DEV_SBS) != 0)
		return (0);

	if (node->parent->attached)
		return (0);

	memset(&aaa, 0, sizeof(aaa));
	aaa.aaa_iot = sc->sc_iot;
	aaa.aaa_memt = sc->sc_memt;
	aaa.aaa_node = node->parent;
	aaa.aaa_dev = dev;
	aaa.aaa_cdev = cdev;

	config_found(self, &aaa, acpi_print);
	node->parent->attached = 1;

	return (0);
}

int
acpi_batcount(struct acpi_softc *sc)
{
	struct acpi_bat *bat;
	int count = 0;

	SLIST_FOREACH(bat, &sc->sc_bat, aba_link)
		count++;
	return count;
}

int
acpi_apminfo(struct apm_power_info *pi)
{
	struct acpi_softc *sc = acpi_softc;
	struct acpi_ac *ac;
	struct acpi_bat *bat;
	struct acpi_sbs *sbs;
	int bats;
	unsigned int capacity, remaining, minutes, rate;

	/* A/C */
	pi->ac_state = APM_AC_UNKNOWN;
// XXX replace with new power code
	SLIST_FOREACH(ac, &sc->sc_ac, aac_link) {
		if (ac->aac_softc->sc_ac_stat == PSR_ONLINE)
			pi->ac_state = APM_AC_ON;
		else if (ac->aac_softc->sc_ac_stat == PSR_OFFLINE)
			if (pi->ac_state == APM_AC_UNKNOWN)
				pi->ac_state = APM_AC_OFF;
	}

	/* battery */
	pi->battery_state = APM_BATT_UNKNOWN;
	pi->battery_life = 0;
	pi->minutes_left = 0;
	bats = 0;
	capacity = 0;
	remaining = 0;
	minutes = 0;
	rate = 0;
	SLIST_FOREACH(bat, &sc->sc_bat, aba_link) {
		if (bat->aba_softc->sc_bat_present == 0)
			continue;

		if (bat->aba_softc->sc_bix.bix_last_capacity == 0)
			continue;

		bats++;
		capacity += bat->aba_softc->sc_bix.bix_last_capacity;
		remaining += min(bat->aba_softc->sc_bst.bst_capacity,
		    bat->aba_softc->sc_bix.bix_last_capacity);

		if (bat->aba_softc->sc_bst.bst_state & BST_CHARGE)
			pi->battery_state = APM_BATT_CHARGING;

		if (bat->aba_softc->sc_bst.bst_rate == BST_UNKNOWN)
			continue;
		else if (bat->aba_softc->sc_bst.bst_rate > 1)
			rate = bat->aba_softc->sc_bst.bst_rate;

		minutes += bat->aba_softc->sc_bst.bst_capacity;
	}

	SLIST_FOREACH(sbs, &sc->sc_sbs, asbs_link) {
		if (sbs->asbs_softc->sc_batteries_present == 0)
			continue;

		if (sbs->asbs_softc->sc_battery.rel_charge == 0)
			continue;

		bats++;
		capacity += 100;
		remaining += min(100,
		    sbs->asbs_softc->sc_battery.rel_charge);

		if (sbs->asbs_softc->sc_battery.run_time ==
		    ACPISBS_VALUE_UNKNOWN)
			continue;

		rate = 60; /* XXX */
		minutes += sbs->asbs_softc->sc_battery.run_time;
	}

	if (bats == 0) {
		pi->battery_state = APM_BATTERY_ABSENT;
		pi->battery_life = 0;
		pi->minutes_left = (unsigned int)-1;
		return 0;
	}

	if (rate == 0)
		pi->minutes_left = (unsigned int)-1;
	else if (pi->battery_state == APM_BATT_CHARGING)
		pi->minutes_left = 60 * (capacity - remaining) / rate;
	else
		pi->minutes_left = 60 * minutes / rate;

	pi->battery_life = remaining * 100 / capacity;

	if (pi->battery_state == APM_BATT_CHARGING)
		return 0;

	/* running on battery */
	if (pi->battery_life > 50)
		pi->battery_state = APM_BATT_HIGH;
	else if (pi->battery_life > 25)
		pi->battery_state = APM_BATT_LOW;
	else
		pi->battery_state = APM_BATT_CRITICAL;

	return 0;
}

int acpi_evindex;

int
acpi_record_event(struct acpi_softc *sc, u_int type)
{
	if ((sc->sc_flags & SCFLAG_OPEN) == 0)
		return (1);

	acpi_evindex++;
	knote_locked(&sc->sc_note, APM_EVENT_COMPOSE(type, acpi_evindex));
	return (0);
}

#endif /* SMALL_KERNEL */
