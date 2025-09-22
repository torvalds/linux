/*	$OpenBSD: octeon_pcibus.c,v 1.23 2021/03/04 16:44:07 visa Exp $	*/
/*	$NetBSD: bonito_mainbus.c,v 1.11 2008/04/28 20:23:10 martin Exp $	*/
/*	$NetBSD: bonito_pci.c,v 1.5 2008/04/28 20:23:28 martin Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
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
/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/octeonvar.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/octeon_pcibus.h>

#include <uvm/uvm_extern.h>

#ifdef DEBUG
#define OCTEON_PCIDEBUG(p) printf p
#else
#define OCTEON_PCIDEBUG(p)
#endif

#define REG_READ32(addr)	(*(volatile uint32_t *)(addr))
#define REG_WRITE32(addr, data)	(*(volatile uint32_t *)(addr) = (uint32_t)(data))

struct octeon_pcibus_softc {
	struct device sc_dev;
	struct mips_pci_chipset sc_pc;
	struct iobus_attach_args *sc_aa;
};

int	octeon_pcibus_match(struct device *, void *, void *);
void	octeon_pcibus_attach(struct device *, struct device *, void *);
int	octeon_pcibus_print(void *, const char *);

const struct cfattach pcibus_ca = {
	sizeof(struct octeon_pcibus_softc),
	octeon_pcibus_match, octeon_pcibus_attach
};

struct cfdriver pcibus_cd = {
	NULL, "pcibus", DV_DULL
};

bus_addr_t octeon_pcibus_pa_to_device(paddr_t);
paddr_t	octeon_pcibus_device_to_pa(bus_addr_t);
void	octeon_pcibus_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	octeon_pcibus_bus_maxdevs(void *, int);
pcitag_t octeon_pcibus_make_tag(void *, int, int, int);
void	octeon_pcibus_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	octeon_pcibus_pci_conf_size(void *, pcitag_t);
pcireg_t octeon_pcibus_pci_conf_read(void *, pcitag_t, int);
void	octeon_pcibus_pci_conf_write(void *, pcitag_t, int, pcireg_t);
int	octeon_pcibus_pci_intr_map(struct pci_attach_args *,
	    pci_intr_handle_t *);
const char *octeon_pcibus_pci_intr_string(void *, pci_intr_handle_t);
void	*octeon_pcibus_pci_intr_establish(void *, pci_intr_handle_t, int,
	    int (*)(void *), void *, char *);
void	octeon_pcibus_pci_intr_disestablish(void *, void *);
int	octeon_pcibus_intr_map(int dev, int fn, int pin);
int	octeon_pcibus_io_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	octeon_pcibus_mem_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
struct extent *octeon_pcibus_get_resource_extent(pci_chipset_tag_t, int);

struct machine_bus_dma_tag octeon_pcibus_bus_dma_tag = {
	._cookie = NULL,
	._dmamap_create =	_dmamap_create,
	._dmamap_destroy =	_dmamap_destroy,
	._dmamap_load =		_dmamap_load,
	._dmamap_load_mbuf =	_dmamap_load_mbuf,
	._dmamap_load_uio =	_dmamap_load_uio,
	._dmamap_load_raw =	_dmamap_load_raw,
	._dmamap_load_buffer =	_dmamap_load_buffer,
	._dmamap_unload =	_dmamap_unload,
	._dmamap_sync =		_dmamap_sync,
	._dmamem_alloc =	_dmamem_alloc,
	._dmamem_free =		_dmamem_free,
	._dmamem_map =		_dmamem_map,
	._dmamem_unmap =	_dmamem_unmap,
	._dmamem_mmap =		_dmamem_mmap,
	._pa_to_device =	octeon_pcibus_pa_to_device,
	._device_to_pa =	octeon_pcibus_device_to_pa
};

#define _OCTEON_PCIBUS_PCIIO_BASE	0x00001000
#define _OCTEON_PCIBUS_PCIIO_SIZE	0x08000000
#define _OCTEON_PCIBUS_PCIMEM_BASE	0x80000000
#define _OCTEON_PCIBUS_PCIMEM_SIZE	0x40000000

struct mips_bus_space octeon_pcibus_pci_io_space_tag = {
	.bus_base = PHYS_TO_XKPHYS(_OCTEON_PCIBUS_PCIIO_BASE, CCA_NC),
	.bus_private = NULL,
	._space_read_1 =	generic_space_read_1,
	._space_write_1 =	generic_space_write_1,
	._space_read_2 =	generic_space_read_2,
	._space_write_2 =	generic_space_write_2,
	._space_read_4 =	generic_space_read_4,
	._space_write_4 =	generic_space_write_4,
	._space_read_8 =	generic_space_read_8,
	._space_write_8 =	generic_space_write_8,
	._space_read_raw_2 =	generic_space_read_raw_2,
	._space_write_raw_2 =	generic_space_write_raw_2,
	._space_read_raw_4 =	generic_space_read_raw_4,
	._space_write_raw_4 =	generic_space_write_raw_4,
	._space_read_raw_8 =	generic_space_read_raw_8,
	._space_write_raw_8 =	generic_space_write_raw_8,
	._space_map =		octeon_pcibus_io_map,
	._space_unmap =		generic_space_unmap,
	._space_subregion =	generic_space_region,
	._space_vaddr =		generic_space_vaddr
};

struct mips_bus_space octeon_pcibus_pci_mem_space_tag = {
	.bus_base = PHYS_TO_XKPHYS(_OCTEON_PCIBUS_PCIMEM_BASE, CCA_NC),
	.bus_private = NULL,
	._space_read_1 =	generic_space_read_1,
	._space_write_1 =	generic_space_write_1,
	._space_read_2 =	generic_space_read_2,
	._space_write_2 =	generic_space_write_2,
	._space_read_4 =	generic_space_read_4,
	._space_write_4 =	generic_space_write_4,
	._space_read_8 =	generic_space_read_8,
	._space_write_8 =	generic_space_write_8,
	._space_read_raw_2 =	generic_space_read_raw_2,
	._space_write_raw_2 =	generic_space_write_raw_2,
	._space_read_raw_4 =	generic_space_read_raw_4,
	._space_write_raw_4 =	generic_space_write_raw_4,
	._space_read_raw_8 =	generic_space_read_raw_8,
	._space_write_raw_8 =	generic_space_write_raw_8,
	._space_map =		octeon_pcibus_mem_map,
	._space_unmap =		generic_space_unmap,
	._space_subregion =	generic_space_region,
	._space_vaddr =		generic_space_vaddr
};

int
octeon_pcibus_match(struct device *parent, void *vcf, void *aux)
{
	struct iobus_attach_args *aa = aux;

	if ((octeon_boot_info->config_flags & BOOTINFO_CFG_FLAG_PCI_HOST) == 0) {
		OCTEON_PCIDEBUG(("%s, no PCI host function detected.\n", __func__));
		return 0;
	}

	if (strcmp(aa->aa_name, pcibus_cd.cd_name) == 0)
		return 1;

	return 0;
}

void
octeon_pcibus_attach(struct device *parent, struct device *self, void *aux)
{
	struct octeon_pcibus_softc *sc;
	struct pcibus_attach_args pba;

	sc = (struct octeon_pcibus_softc *)self;
	sc->sc_aa = aux;

	printf("\n");

	/*
	 * Attach PCI bus.
	 */
	sc->sc_pc.pc_attach_hook = octeon_pcibus_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = octeon_pcibus_bus_maxdevs;
	sc->sc_pc.pc_make_tag = octeon_pcibus_make_tag;
	sc->sc_pc.pc_decompose_tag = octeon_pcibus_decompose_tag;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_conf_size = octeon_pcibus_pci_conf_size;
	sc->sc_pc.pc_conf_read = octeon_pcibus_pci_conf_read;
	sc->sc_pc.pc_conf_write = octeon_pcibus_pci_conf_write;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = octeon_pcibus_pci_intr_map;
	sc->sc_pc.pc_intr_string = octeon_pcibus_pci_intr_string;
	sc->sc_pc.pc_intr_establish = octeon_pcibus_pci_intr_establish;
	sc->sc_pc.pc_intr_disestablish = octeon_pcibus_pci_intr_disestablish;

	bzero(&pba, sizeof pba);
	pba.pba_busname = "pci";
	pba.pba_iot = &octeon_pcibus_pci_io_space_tag;
	pba.pba_memt = &octeon_pcibus_pci_mem_space_tag;
	pba.pba_dmat = &octeon_pcibus_bus_dma_tag;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
	pba.pba_ioex = octeon_pcibus_get_resource_extent(&sc->sc_pc, 1);
	pba.pba_memex = octeon_pcibus_get_resource_extent(&sc->sc_pc, 0);

	config_found(&sc->sc_dev, &pba, octeon_pcibus_print);
}

bus_addr_t
octeon_pcibus_pa_to_device(paddr_t pa)
{
	OCTEON_PCIDEBUG(("%s:%d: pa=%p\n", __func__, __LINE__, (void *)pa));

	return pa & 0x1ffffffffffffUL;
}

paddr_t
octeon_pcibus_device_to_pa(bus_addr_t addr)
{
	OCTEON_PCIDEBUG(("%s:%d: addr=%lx\n", __func__, __LINE__, addr));

	return PHYS_TO_XKPHYS(addr, CCA_NC);
}

int
octeon_pcibus_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);

	return UNCONF;
}

/*
 * various PCI helpers
 */
void
octeon_pcibus_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

/*
 * PCI configuration space access routines
 */
int
octeon_pcibus_bus_maxdevs(void *v, int busno)
{
	return (32);
}

pcitag_t
octeon_pcibus_make_tag(void *unused, int b, int d, int f)
{
	return (b << 16) | (d << 11) | (f << 8);
}

void
octeon_pcibus_decompose_tag(void *unused, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
octeon_pcibus_pci_conf_size(void *v, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
octeon_pcibus_pci_conf_read(void *v, pcitag_t tag, int offset)
{
	pcireg_t data;
	uint64_t cfgoff;

	if (tag == 0){
		if (offset & 0x4){
			cfgoff = OCTEON_PCI_CFG1 + (offset & 0xfff8);
		} else {
			cfgoff = OCTEON_PCI_CFG0 + (offset & 0xfff8);
		}
	} else {
		cfgoff = tag + offset;
		if (offset & 0x4) {
			cfgoff = OCTEON_PCI_CONFIG_BASE1 + (cfgoff & 0xfffffff8);
		} else {
			cfgoff = OCTEON_PCI_CONFIG_BASE0 + (cfgoff & 0xfffffff8);
		}
	}

	data = REG_READ32(cfgoff);
	return data;
}

void
octeon_pcibus_pci_conf_write(void *v, pcitag_t tag, int offset, pcireg_t data)
{
	uint64_t cfgoff;

	if (tag == 0){
		if (offset & 0x4){
			cfgoff = OCTEON_PCI_CFG1 + (offset & 0xfff8);
		} else {
			cfgoff = OCTEON_PCI_CFG0 + (offset & 0xfff8);
		}
	} else {
		cfgoff = tag + offset;
		if (offset & 0x4){
			cfgoff = OCTEON_PCI_CONFIG_BASE1 + (cfgoff & 0xfffffff8);
		} else {
			cfgoff = OCTEON_PCI_CONFIG_BASE0 + (cfgoff & 0xfffffff8);
		}
	}

	REG_WRITE32(cfgoff, data);
}


/*
 * PCI Interrupt handling
 */
int
octeon_pcibus_pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
#if 0
	struct octeon_pcibus_softc *sc = pa->pa_pc->pc_intr_v;
#endif
	int bus, dev, fn, pin;

	*ihp = (pci_intr_handle_t)-1;

	if (pa->pa_intrpin == 0)	/* no interrupt needed */
		return 1;

#ifdef DIAGNOSTIC
	if (pa->pa_intrpin > 4) {
		printf("%s: bad interrupt pin %d\n", __func__, pa->pa_intrpin);
		return 1;
	}
#endif

	pci_decompose_tag(pa->pa_pc, pa->pa_tag, &bus, &dev, &fn);
	if (pa->pa_bridgetag) {
		pin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin, dev);
		*ihp = pa->pa_bridgeih[pin - 1];
	} else {
		if (bus == 0)
			*ihp = octeon_pcibus_intr_map(dev, fn, pa->pa_intrpin);

		if (*ihp == (pci_intr_handle_t)-1)
			return 1;
	}

	return 0;
}

const char *
octeon_pcibus_pci_intr_string(void *cookie, pci_intr_handle_t ih)
{
	static char irqstr[sizeof("irq 0123456789")];

	snprintf(irqstr, sizeof irqstr, "irq %ld", ih);
	return irqstr;
}

void *
octeon_pcibus_pci_intr_establish(void *cookie, pci_intr_handle_t ih, int level,
    int (*cb)(void *), void *cbarg, char *name)
{
	return octeon_intr_establish(ih, level, cb, cbarg, name);
}

void
octeon_pcibus_pci_intr_disestablish(void *cookie, void *ihp)
{
	octeon_intr_disestablish(ihp);
}

/*
 * bus_space mapping routines.
 */
int
octeon_pcibus_io_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	if (ISSET(flags, BUS_SPACE_MAP_CACHEABLE)) {
		offs +=
		    PHYS_TO_XKPHYS(0, CCA_CACHED) - PHYS_TO_XKPHYS(0, CCA_NC);
	}
	*bshp = t->bus_base + offs;
	return 0;
}

int
octeon_pcibus_mem_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	if (ISSET(flags, BUS_SPACE_MAP_CACHEABLE)) {
		offs +=
		    PHYS_TO_XKPHYS(0, CCA_CACHED) - PHYS_TO_XKPHYS(0, CCA_NC);
	}
	*bshp = t->bus_base + offs;
	return 0;
}

/*
 * PCI resource handling
 */
struct extent *
octeon_pcibus_get_resource_extent(pci_chipset_tag_t pc, int io)
{
	struct octeon_pcibus_softc *sc = pc->pc_conf_v;
	struct extent *ex;
	char *exname;
	int exnamesz;

	exnamesz = 1 + 16 + 4;
	exname = malloc(exnamesz, M_DEVBUF, M_NOWAIT);
	if (exname == NULL)
		return NULL;
	snprintf(exname, exnamesz, "%s%s", sc->sc_dev.dv_xname,
	    io ? "_io" : "_mem");

	ex = extent_create(exname, 0, 0xffffffffffffffff, M_DEVBUF, NULL, 0,
	    EX_NOWAIT | EX_FILLED);
	if (ex == NULL)
		goto error;

	if (io) {
		if (extent_free(ex, _OCTEON_PCIBUS_PCIIO_BASE,
		    _OCTEON_PCIBUS_PCIIO_SIZE, EX_NOWAIT) != 0)
			goto error;
	} else {
		if (extent_free(ex, _OCTEON_PCIBUS_PCIMEM_BASE,
		    _OCTEON_PCIBUS_PCIMEM_SIZE, EX_NOWAIT) != 0)
			goto error;
	}

#if defined(DEBUG) && defined(DIAGNOSTIC)
	extent_print(ex);
#endif
	return ex;

error:
	if (ex != NULL)
		extent_destroy(ex);
	free(exname, M_DEVBUF, exnamesz);
	return NULL;
}

/*
 * PCI model specific routines
 */

int
octeon_pcibus_intr_map(int dev, int fn, int pin)
{
	return CIU_INT_PCI_INTA + ((pin - 1) & 3);
}
