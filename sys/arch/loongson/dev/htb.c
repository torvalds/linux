/*	$OpenBSD: htb.c,v 1.3 2017/05/10 15:21:02 visa Exp $	*/

/*
 * Copyright (c) 2016 Visa Hankala
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

/*
 * PCI host bridge driver for Loongson 3A.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/loongson3.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <loongson/dev/htbreg.h>
#include <loongson/dev/htbvar.h>

struct htb_softc {
	struct device		 sc_dev;
	struct mips_pci_chipset	 sc_pc;
	const struct htb_config	*sc_config;
};

int	 htb_match(struct device *, void *, void *);
void	 htb_attach(struct device *, struct device *, void *);
int	 htb_print(void *, const char *);

void	 htb_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *pba);
int	 htb_bus_maxdevs(void *, int);
pcitag_t htb_make_tag(void *, int, int, int);
void	 htb_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	 htb_conf_addr(const struct bonito_config *, pcitag_t, int,
	    u_int32_t *, u_int32_t *);
int	 htb_conf_size(void *, pcitag_t);
pcireg_t htb_conf_read(void *, pcitag_t, int);
void	 htb_conf_write(void *, pcitag_t, int, pcireg_t);
int	 htb_pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *
	 htb_pci_intr_string(void *, pci_intr_handle_t);
void	*htb_pci_intr_establish(void *, pci_intr_handle_t, int,
	    int (*)(void *), void *, char *);
void	 htb_pci_intr_disestablish(void *, void *);

bus_addr_t htb_pa_to_device(paddr_t);
paddr_t	 htb_device_to_pa(bus_addr_t);

int	 htb_io_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	 htb_mem_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
paddr_t	 htb_mem_mmap(bus_space_tag_t, bus_addr_t, off_t, int, int);

paddr_t	 htb_cfg_space_addr(pcitag_t, int);

pcireg_t htb_conf_read_early(pcitag_t, int);
pcitag_t htb_make_tag_early(int, int, int);

const struct cfattach htb_ca = {
	sizeof(struct htb_softc), htb_match, htb_attach
};

struct cfdriver htb_cd = {
	NULL, "htb", DV_DULL
};

struct machine_bus_dma_tag htb_bus_dma_tag = {
	._dmamap_create = _dmamap_create,
	._dmamap_destroy = _dmamap_destroy,
	._dmamap_load = _dmamap_load,
	._dmamap_load_mbuf = _dmamap_load_mbuf,
	._dmamap_load_uio = _dmamap_load_uio,
	._dmamap_load_raw = _dmamap_load_raw,
	._dmamap_load_buffer = _dmamap_load_buffer,
	._dmamap_unload = _dmamap_unload,
	._dmamap_sync = _dmamap_sync,
	._dmamem_alloc = _dmamem_alloc,
	._dmamem_free = _dmamem_free,
	._dmamem_map = _dmamem_map,
	._dmamem_unmap = _dmamem_unmap,
	._dmamem_mmap = _dmamem_mmap,

	._pa_to_device = htb_pa_to_device,
	._device_to_pa = htb_device_to_pa
};

struct mips_bus_space htb_pci_io_space_tag = {
	.bus_base = PHYS_TO_XKPHYS(HTB_IO_BASE, CCA_NC),
	._space_read_1 = generic_space_read_1,
	._space_write_1 = generic_space_write_1,
	._space_read_2 = generic_space_read_2,
	._space_write_2 = generic_space_write_2,
	._space_read_4 = generic_space_read_4,
	._space_write_4 = generic_space_write_4,
	._space_read_8 = generic_space_read_8,
	._space_write_8 = generic_space_write_8,
	._space_read_raw_2 = generic_space_read_raw_2,
	._space_write_raw_2 = generic_space_write_raw_2,
	._space_read_raw_4 = generic_space_read_raw_4,
	._space_write_raw_4 = generic_space_write_raw_4,
	._space_read_raw_8 = generic_space_read_raw_8,
	._space_write_raw_8 = generic_space_write_raw_8,
	._space_map = htb_io_map,
	._space_unmap = generic_space_unmap,
	._space_subregion = generic_space_region,
	._space_vaddr = generic_space_vaddr,
	._space_mmap = generic_space_mmap
};

struct mips_bus_space htb_pci_mem_space_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	._space_read_1 = generic_space_read_1,
	._space_write_1 = generic_space_write_1,
	._space_read_2 = generic_space_read_2,
	._space_write_2 = generic_space_write_2,
	._space_read_4 = generic_space_read_4,
	._space_write_4 = generic_space_write_4,
	._space_read_8 = generic_space_read_8,
	._space_write_8 = generic_space_write_8,
	._space_read_raw_2 = generic_space_read_raw_2,
	._space_write_raw_2 = generic_space_write_raw_2,
	._space_read_raw_4 = generic_space_read_raw_4,
	._space_write_raw_4 = generic_space_write_raw_4,
	._space_read_raw_8 = generic_space_read_raw_8,
	._space_write_raw_8 = generic_space_write_raw_8,
	._space_map = htb_mem_map,
	._space_unmap = generic_space_unmap,
	._space_subregion = generic_space_region,
	._space_vaddr = generic_space_vaddr,
	._space_mmap = htb_mem_mmap
};

int
htb_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *maa = aux;

	if (loongson_ver != 0x3a && loongson_ver != 0x3b)
		return 0;

	if (strcmp(maa->maa_name, htb_cd.cd_name) != 0)
		return 0;

	return 1;
}

void
htb_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcibus_attach_args pba;
	struct htb_softc *sc = (struct htb_softc *)self;
	pci_chipset_tag_t pc = &sc->sc_pc;

	printf("\n");

	sc->sc_config = sys_platform->htb_config;

	pc->pc_conf_v = sc;
	pc->pc_attach_hook = htb_attach_hook;
	pc->pc_bus_maxdevs = htb_bus_maxdevs;
	pc->pc_make_tag = htb_make_tag;
	pc->pc_decompose_tag = htb_decompose_tag;
	pc->pc_conf_size = htb_conf_size;
	pc->pc_conf_read = htb_conf_read;
	pc->pc_conf_write = htb_conf_write;

	pc->pc_intr_v = sc;
	pc->pc_intr_map = htb_pci_intr_map;
	pc->pc_intr_string = htb_pci_intr_string;
	pc->pc_intr_establish = htb_pci_intr_establish;
	pc->pc_intr_disestablish = htb_pci_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &htb_pci_io_space_tag;
	pba.pba_memt = &htb_pci_mem_space_tag;
	pba.pba_dmat = &htb_bus_dma_tag;
	pba.pba_pc = pc;
	pba.pba_ioex = extent_create("htb_io", 0, 0xffffffff, M_DEVBUF,
	    NULL, 0, EX_NOWAIT | EX_FILLED);
	if (pba.pba_ioex != NULL) {
		extent_free(pba.pba_ioex, 0, HTB_IO_SIZE, EX_NOWAIT);
	}
	pba.pba_memex = extent_create("htb_mem", 0, 0xffffffff, M_DEVBUF,
	    NULL, 0, EX_NOWAIT | EX_FILLED);
	if (pba.pba_memex != NULL) {
		extent_free(pba.pba_memex, HTB_MEM_BASE, HTB_MEM_SIZE,
		    EX_NOWAIT);
	}
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
	config_found(&sc->sc_dev, &pba, htb_print);
}

int
htb_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);

	return UNCONF;
}

void
htb_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
	pci_chipset_tag_t pc = pba->pba_pc;
	struct htb_softc *sc = pc->pc_conf_v;
	const struct htb_config *hc = sc->sc_config;

	if (pba->pba_bus == 0)
		hc->hc_attach_hook(pc);
}

int
htb_bus_maxdevs(void *v, int busno)
{
	return 32;
}

pcitag_t
htb_make_tag(void *unused, int b, int d, int f)
{
	return (b << 16) | (d << 11) | (f << 8);
}

void
htb_decompose_tag(void *unused, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
htb_conf_addr(const struct bonito_config *bc, pcitag_t tag, int offset,
    u_int32_t *cfgoff, u_int32_t *pcimap_cfg)
{
	return -1;
}

int
htb_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
htb_conf_read(void *v, pcitag_t tag, int offset)
{
	return REGVAL(htb_cfg_space_addr(tag, offset));
}

void
htb_conf_write(void *v, pcitag_t tag, int offset, pcireg_t data)
{
	REGVAL(htb_cfg_space_addr(tag, offset)) = data;
}

paddr_t
htb_cfg_space_addr(pcitag_t tag, int offset)
{
	paddr_t pa;
	int bus;

	htb_decompose_tag(NULL, tag, &bus, NULL, NULL);
	if (bus == 0)
		pa = HTB_CFG_TYPE0_BASE;
	else
		pa = HTB_CFG_TYPE1_BASE;
	return pa + tag + (offset & 0xfffc);
}

/*
 * PCI interrupt handling
 */

int
htb_pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int dev, pin;

	*ihp = (pci_intr_handle_t)-1;

	if (pa->pa_intrpin == 0)
		return 1;

	if (pa->pa_intrpin > PCI_INTERRUPT_PIN_MAX) {
		printf(": bad interrupt pin %d\n", pa->pa_intrpin);
		return 1;
	}

	pci_decompose_tag(pa->pa_pc, pa->pa_tag, NULL, &dev, NULL);
	if (pa->pa_bridgetag != NULL) {
		pin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin, dev);
		if (pa->pa_bridgeih[pin - 1] != (pci_intr_handle_t)-1) {
			*ihp = pa->pa_bridgeih[pin - 1];
			return 0;
		}
	}

	if (pa->pa_intrline != 0) {
		*ihp = pa->pa_intrline;
		return 0;
	}

	return 1;
}

const char *
htb_pci_intr_string(void *cookie, pci_intr_handle_t ih)
{
	static char irqstr[16];

	snprintf(irqstr, sizeof(irqstr), "irq %lu", ih);
	return irqstr;
}

void *
htb_pci_intr_establish(void *cookie, pci_intr_handle_t ih, int level,
    int (*cb)(void *), void *cbarg, char *name)
{
	return loongson3_ht_intr_establish(ih, level, cb, cbarg, name);
}

void
htb_pci_intr_disestablish(void *cookie, void *ihp)
{
	loongson3_ht_intr_disestablish(ihp);
}

bus_addr_t
htb_pa_to_device(paddr_t pa)
{
	return pa ^ loongson_dma_base;
}

paddr_t
htb_device_to_pa(bus_addr_t addr)
{
	return addr ^ loongson_dma_base;
}

/*
 * bus_space(9) mapping routines
 */

int
htb_io_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	const struct legacy_io_range *r;
	bus_addr_t end;

	if (offs >= HTB_IO_SIZE)
		return EINVAL;

	if (offs < HTB_IO_LEGACY) {
		end = offs + size - 1;
		if ((r = sys_platform->legacy_io_ranges) == NULL)
			return ENXIO;
		for ( ; r->start != 0; r++) {
			if (offs >= r->start && end <= r->end)
				break;
		}
		if (r->end == 0)
			return ENXIO;
	}

	*bshp = t->bus_base + offs;
	return 0;
}

int
htb_mem_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	if (offs < HTB_MEM_BASE || offs + size > HTB_MEM_BASE + HTB_MEM_SIZE)
		return EINVAL;

	*bshp = t->bus_base + offs;
	return 0;
}

paddr_t
htb_mem_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off, int prot,
    int flags)
{
	return addr + off;
}

/*
 * Functions for system setup
 */

void
htb_early_setup(void)
{
	pci_make_tag_early = htb_make_tag_early;
	pci_conf_read_early = htb_conf_read_early;

	early_mem_t = &htb_pci_mem_space_tag;
	early_io_t = &htb_pci_io_space_tag;
}

pcitag_t
htb_make_tag_early(int b, int d, int f)
{
	return htb_make_tag(NULL, b, d, f);
}

pcireg_t
htb_conf_read_early(pcitag_t tag, int reg)
{
	return htb_conf_read(NULL, tag, reg);
}
