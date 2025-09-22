/*	$OpenBSD: shpcic.c,v 1.15 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: shpcic.c,v 1.10 2005/12/24 20:07:32 perry Exp $	*/

/*
 * Copyright (c) 2005 NONAKA Kimihiro
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <sh/bscreg.h>
#include <sh/cache.h>
#include <sh/trap.h>
#include <sh/dev/pcicreg.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>

#if defined(SHPCIC_DEBUG)
int shpcic_debug = 0;
#define	DPRINTF(arg)	if (shpcic_debug) printf arg
#else
#define	DPRINTF(arg)
#endif

#define	PCI_MODE1_ENABLE	0x80000000UL

static const struct shpcic_product {
	uint32_t	sp_product;
	const char	*sp_name;
} shpcic_products[] = {
	{ PCI_PRODUCT_HITACHI_SH7751,	"SH7751" },
	{ PCI_PRODUCT_HITACHI_SH7751R,	"SH7751R" },

	{ 0, NULL },
};

int	shpcic_match(struct device *, void *, void *);
void	shpcic_attach(struct device *, struct device *, void *);

const struct cfattach shpcic_ca = {
	sizeof(struct shpcic_softc), shpcic_match, shpcic_attach
};

struct cfdriver shpcic_cd = {
	NULL, "shpcic", DV_DULL
};

/* There can be only one. */
int shpcic_found = 0;

static const struct shpcic_product *shpcic_lookup(void);

static const struct shpcic_product *
shpcic_lookup(void)
{
	const struct shpcic_product *spp;
	pcireg_t id;

	id = _reg_read_4(SH4_PCICONF0);
	switch (PCI_VENDOR(id)) {
	case PCI_VENDOR_HITACHI:
		break;

	default:
		return (NULL);
	}

	for (spp = shpcic_products; spp->sp_name != NULL; spp++) {
		if (PCI_PRODUCT(id) == spp->sp_product) {
			return (spp);
		}
	}
	return (NULL);
}

int
shpcic_match(struct device *parent, void *vcf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, shpcic_cd.cd_name) != 0)
		return (0);

	if (!CPU_IS_SH4)
		return (0);

	switch (cpu_product) {
	default:
		return (0);

	case CPU_PRODUCT_7751:
	case CPU_PRODUCT_7751R:
		break;
	}

	if (shpcic_found)
		return (0);

	if (shpcic_lookup() == NULL)
		return (0);

	if (_reg_read_2(SH4_BCR2) & BCR2_PORTEN)
		return (0);

	return (1);
}

void
shpcic_attach(struct device *parent, struct device *self, void *aux)
{
	const struct shpcic_product *spp;
	struct shpcic_softc *sc = (struct shpcic_softc *)self;
	struct pcibus_attach_args pba;
	struct extent *io_ex;
	struct extent *mem_ex;

	shpcic_found = 1;

	spp = shpcic_lookup();
	if (spp == NULL) {
		printf("\n");
		panic("shpcic_attach: impossible");
	}

	if (_reg_read_2(SH4_BCR2) & BCR2_PORTEN) {
		printf("\n");
		panic("shpcic_attach: port enabled");
	}

	printf(": HITACHI %s\n", spp->sp_name);

	/* allow PCIC request */
	_reg_write_4(SH4_BCR1, _reg_read_4(SH4_BCR1) | BCR1_BREQEN);

	/* Initialize PCIC */
	_reg_write_4(SH4_PCICR, PCICR_BASE | PCICR_RSTCTL);
	delay(10 * 1000);
	_reg_write_4(SH4_PCICR, PCICR_BASE);

	/* Class: Host-Bridge */
	_reg_write_4(SH4_PCICONF2,
	    (PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT) |
	    (PCI_SUBCLASS_BRIDGE_HOST << PCI_SUBCLASS_SHIFT));

#if !defined(DONT_INIT_PCIBSC)
#if defined(PCIBCR_BCR1_VAL)
	_reg_write_4(SH4_PCIBCR1, PCIBCR_BCR1_VAL);
#else
	_reg_write_4(SH4_PCIBCR1, _reg_read_4(SH4_BCR1) | BCR1_MASTER);
#endif
#if defined(PCIBCR_BCR2_VAL)
	_reg_write_4(SH4_PCIBCR2, PCIBCR_BCR2_VAL);
#else
	_reg_write_4(SH4_PCIBCR2, _reg_read_2(SH4_BCR2));
#endif
#if defined(SH4) && defined(SH7751R)
	if (cpu_product == CPU_PRODUCT_7751R) {
#if defined(PCIBCR_BCR3_VAL)
		_reg_write_4(SH4_PCIBCR3, PCIBCR_BCR3_VAL);
#else
		_reg_write_4(SH4_PCIBCR3, _reg_read_2(SH4_BCR3));
#endif
	}
#endif	/* SH4 && SH7751R && PCIBCR_BCR3_VAL */
#if defined(PCIBCR_WCR1_VAL)
	_reg_write_4(SH4_PCIWCR1, PCIBCR_WCR1_VAL);
#else
	_reg_write_4(SH4_PCIWCR1, _reg_read_4(SH4_WCR1));
#endif
#if defined(PCIBCR_WCR2_VAL)
	_reg_write_4(SH4_PCIWCR2, PCIBCR_WCR2_VAL);
#else
	_reg_write_4(SH4_PCIWCR2, _reg_read_4(SH4_WCR2));
#endif
#if defined(PCIBCR_WCR3_VAL)
	_reg_write_4(SH4_PCIWCR3, PCIBCR_WCR3_VAL);
#else
	_reg_write_4(SH4_PCIWCR3, _reg_read_4(SH4_WCR3));
#endif
#if defined(PCIBCR_MCR_VAL)
	_reg_write_4(SH4_PCIMCR, PCIBCR_MCR_VAL);
#else
	_reg_write_4(SH4_PCIMCR, _reg_read_4(SH4_MCR));
#endif
#endif	/* !DONT_INIT_PCIBSC */

	/* set PCI I/O, memory base address */
	_reg_write_4(SH4_PCIIOBR, SH4_PCIC_IO);
	_reg_write_4(SH4_PCIMBR, SH4_PCIC_MEM);

	/* set PCI local address 0 */
	_reg_write_4(SH4_PCILSR0, (64 - 1) << 20);
	_reg_write_4(SH4_PCILAR0, 0xac000000);
	_reg_write_4(SH4_PCICONF5, 0xac000000);

	/* set PCI local address 1 */
	_reg_write_4(SH4_PCILSR1, (64 - 1) << 20);
	_reg_write_4(SH4_PCILAR1, 0xac000000);
	_reg_write_4(SH4_PCICONF6, 0x8c000000);

	/* Enable I/O, memory, bus-master */
	_reg_write_4(SH4_PCICONF1, PCI_COMMAND_IO_ENABLE
	                           | PCI_COMMAND_MEM_ENABLE
	                           | PCI_COMMAND_MASTER_ENABLE
	                           | PCI_COMMAND_STEPPING_ENABLE
				   | PCI_STATUS_DEVSEL_MEDIUM);

	/* Initialize done. */
	_reg_write_4(SH4_PCICR, PCICR_BASE | PCICR_CFINIT);

	/* set PCI controller interrupt priority */
	intpri_intr_priority(SH4_INTEVT_PCIERR, IPL_BIO);	/* IPL_HIGH? */
	intpri_intr_priority(SH4_INTEVT_PCISERR, IPL_BIO);	/* IPL_HIGH? */

	sc->sc_membus_space.bus_base = SH4_PCIC_MEM;
	sc->sc_membus_space.bus_size = SH4_PCIC_MEM_SIZE;
	sc->sc_membus_space.bus_io = 0;
	sc->sc_iobus_space.bus_base = SH4_PCIC_IO; /* XXX */
	sc->sc_iobus_space.bus_size =  SH4_PCIC_IO_SIZE;
	sc->sc_iobus_space.bus_io = 1;

	io_ex = extent_create("pciio", 0, 0xffffffff, M_DEVBUF, NULL, 0,
	    EX_NOWAIT | EX_FILLED);
	if (io_ex != NULL)
		extent_free(io_ex, SH4_PCIC_IO, SH4_PCIC_IO_SIZE, EX_NOWAIT);
	mem_ex = extent_create("pcimem", 0, 0xffffffff, M_DEVBUF, NULL, 0,
	    EX_NOWAIT | EX_FILLED);
	if (mem_ex != NULL)
		extent_free(mem_ex, SH4_PCIC_MEM, SH4_PCIC_MEM_SIZE, EX_NOWAIT);

	sc->sc_pci_chipset = shpcic_get_bus_mem_tag();

	/* PCI bus */
	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = shpcic_get_bus_io_tag();
	pba.pba_memt = shpcic_get_bus_mem_tag();
	pba.pba_dmat = shpcic_get_bus_dma_tag();
	pba.pba_ioex = io_ex;
	pba.pba_memex = mem_ex;
	pba.pba_pc = NULL;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
	pba.pba_bridgetag = NULL;
	config_found(self, &pba, NULL);
}

int
shpcic_bus_maxdevs(void *v, int busno)
{
	/*
	 * Bus number is irrelevant.  Configuration Mechanism 1 is in
	 * use, can have devices 0-32 (i.e. the `normal' range).
	 */
	return (32);
}

pcitag_t
shpcic_make_tag(void *v, int bus, int device, int function)
{
	pcitag_t tag;

	if (bus >= 256 || device >= 32 || function >= 8)
		panic("pci_make_tag: bad request");

	tag = PCI_MODE1_ENABLE |
		    (bus << 16) | (device << 11) | (function << 8);

	return (tag);
}

void
shpcic_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
shpcic_conf_size(void *v, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
shpcic_conf_read(void *v, pcitag_t tag, int reg)
{
	pcireg_t data;
	int s;

	s = splhigh();
	_reg_write_4(SH4_PCIPAR, tag | reg);
	data = _reg_read_4(SH4_PCIPDR);
	_reg_write_4(SH4_PCIPAR, 0);
	splx(s);

	return data;
}

void
shpcic_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	int s;

	s = splhigh();
	_reg_write_4(SH4_PCIPAR, tag | reg);
	_reg_write_4(SH4_PCIPDR, data);
	_reg_write_4(SH4_PCIPAR, 0);
	splx(s);
}

/*
 * shpcic bus space
 */
int
shpcic_iomem_map(void *v, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	*bshp = (bus_space_handle_t)bpa;

	return (0);
}

void
shpcic_iomem_unmap(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	/* Nothing to do */
}

int
shpcic_iomem_subregion(void *v, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;

	return (0);
}

int
shpcic_iomem_alloc(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp)
{
	*bshp = *bpap = rstart;

	return (0);
}

void
shpcic_iomem_free(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	/* Nothing to do */
}

void *
shpcic_iomem_vaddr(void *v, bus_space_handle_t bsh)
{
	return ((void *)bsh);
}

/*
 * shpcic bus space io/mem read/write
 */
/* read */
static inline uint8_t __shpcic_io_read_1(bus_space_handle_t bsh,
    bus_size_t offset);
static inline uint16_t __shpcic_io_read_2(bus_space_handle_t bsh,
    bus_size_t offset);
static inline uint32_t __shpcic_io_read_4(bus_space_handle_t bsh,
    bus_size_t offset);
static inline uint8_t __shpcic_mem_read_1(bus_space_handle_t bsh,
    bus_size_t offset);
static inline uint16_t __shpcic_mem_read_2(bus_space_handle_t bsh,
    bus_size_t offset);
static inline uint32_t __shpcic_mem_read_4(bus_space_handle_t bsh,
    bus_size_t offset);

static inline uint8_t
__shpcic_io_read_1(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	return *(volatile uint8_t *)(SH4_PCIC_IO + adr);
}

static inline uint16_t
__shpcic_io_read_2(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	return *(volatile uint16_t *)(SH4_PCIC_IO + adr);
}

static inline uint32_t
__shpcic_io_read_4(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	return *(volatile uint32_t *)(SH4_PCIC_IO + adr);
}

static inline uint8_t
__shpcic_mem_read_1(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	return *(volatile uint8_t *)(SH4_PCIC_MEM + adr);
}

static inline uint16_t
__shpcic_mem_read_2(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	return *(volatile uint16_t *)(SH4_PCIC_MEM + adr);
}

static inline uint32_t
__shpcic_mem_read_4(bus_space_handle_t bsh, bus_size_t offset)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	return *(volatile uint32_t *)(SH4_PCIC_MEM + adr);
}

/*
 * read single
 */
uint8_t
shpcic_io_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint8_t value;

	value = __shpcic_io_read_1(bsh, offset);

	return value;
}

uint16_t
shpcic_io_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint16_t value;

	value = __shpcic_io_read_2(bsh, offset);

	return value;
}

uint32_t
shpcic_io_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint32_t value;

	value = __shpcic_io_read_4(bsh, offset);

	return value;
}

uint8_t
shpcic_mem_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint8_t value;

	value = __shpcic_mem_read_1(bsh, offset);

	return value;
}

uint16_t
shpcic_mem_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint16_t value;

	value = __shpcic_mem_read_2(bsh, offset);

	return value;
}

uint32_t
shpcic_mem_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	uint32_t value;

	value = __shpcic_mem_read_4(bsh, offset);

	return value;
}

/*
 * read multi
 */
void
shpcic_io_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_1(bsh, offset);
	}
}

void
shpcic_io_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_2(bsh, offset);
	}
}

void
shpcic_io_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_4(bsh, offset);
	}
}

void
shpcic_mem_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_1(bsh, offset);
	}
}

void
shpcic_mem_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_2(bsh, offset);
	}
}

void
shpcic_mem_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_4(bsh, offset);
	}
}

/*
 * read raw multi
 */

void
shpcic_io_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = __shpcic_io_read_2(bsh, offset);
		addr += 2;
	}
}

void
shpcic_io_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = __shpcic_io_read_4(bsh, offset);
		addr += 4;
	}
}

void
shpcic_mem_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = __shpcic_mem_read_2(bsh, offset);
		addr += 2;
	}
}

void
shpcic_mem_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = __shpcic_mem_read_4(bsh, offset);
		addr += 4;
	}
}

/*
 * read region
 */
void
shpcic_io_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_1(bsh, offset);
		offset += 1;
	}
}

void
shpcic_io_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_2(bsh, offset);
		offset += 2;
	}
}

void
shpcic_io_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_io_read_4(bsh, offset);
		offset += 4;
	}
}

void
shpcic_mem_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_1(bsh, offset);
		offset += 1;
	}
}

void
shpcic_mem_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_2(bsh, offset);
		offset += 2;
	}
}

void
shpcic_mem_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	while (count--) {
		*addr++ = __shpcic_mem_read_4(bsh, offset);
		offset += 4;
	}
}

/*
 * read raw region
 */

void
shpcic_io_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = __shpcic_io_read_2(bsh, offset);
		addr += 2;
		offset += 2;
	}
}

void
shpcic_io_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = __shpcic_io_read_4(bsh, offset);
		addr += 4;
		offset += 4;
	}
}

void
shpcic_mem_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = __shpcic_mem_read_2(bsh, offset);
		addr += 2;
		offset += 2;
	}
}

void
shpcic_mem_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = __shpcic_mem_read_4(bsh, offset);
		addr += 4;
		offset += 4;
	}
}

/* write */
static inline void __shpcic_io_write_1(bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value);
static inline void __shpcic_io_write_2(bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value);
static inline void __shpcic_io_write_4(bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value);
static inline void __shpcic_mem_write_1(bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value);
static inline void __shpcic_mem_write_2(bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value);
static inline void __shpcic_mem_write_4(bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value);

static inline void
__shpcic_io_write_1(bus_space_handle_t bsh, bus_size_t offset,
    uint8_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	*(volatile uint8_t *)(SH4_PCIC_IO + adr) = value;
}

static inline void
__shpcic_io_write_2(bus_space_handle_t bsh, bus_size_t offset,
    uint16_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	*(volatile uint16_t *)(SH4_PCIC_IO + adr) = value;
}

static inline void
__shpcic_io_write_4(bus_space_handle_t bsh, bus_size_t offset,
    uint32_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_IO_MASK;

	*(volatile uint32_t *)(SH4_PCIC_IO + adr) = value;
}

static inline void
__shpcic_mem_write_1(bus_space_handle_t bsh, bus_size_t offset,
    uint8_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	*(volatile uint8_t *)(SH4_PCIC_MEM + adr) = value;
}

static inline void
__shpcic_mem_write_2(bus_space_handle_t bsh, bus_size_t offset,
    uint16_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	*(volatile uint16_t *)(SH4_PCIC_MEM + adr) = value;
}

static inline void
__shpcic_mem_write_4(bus_space_handle_t bsh, bus_size_t offset,
    uint32_t value)
{
	u_long adr = (u_long)(bsh + offset) & SH4_PCIC_MEM_MASK;

	*(volatile uint32_t *)(SH4_PCIC_MEM + adr) = value;
}

/*
 * write single
 */
void
shpcic_io_write_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value)
{
	__shpcic_io_write_1(bsh, offset, value);
}

void
shpcic_io_write_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value)
{
	__shpcic_io_write_2(bsh, offset, value);
}

void
shpcic_io_write_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value)
{
	__shpcic_io_write_4(bsh, offset, value);
}

void
shpcic_mem_write_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value)
{
	__shpcic_mem_write_1(bsh, offset, value);
}

void
shpcic_mem_write_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value)
{
	__shpcic_mem_write_2(bsh, offset, value);
}

void
shpcic_mem_write_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value)
{
	__shpcic_mem_write_4(bsh, offset, value);
}

/*
 * write multi
 */
void
shpcic_io_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_1(bsh, offset, *addr++);
	}
}

void
shpcic_io_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_2(bsh, offset, *addr++);
	}
}

void
shpcic_io_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_4(bsh, offset, *addr++);
	}
}

void
shpcic_mem_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_1(bsh, offset, *addr++);
	}
}

void
shpcic_mem_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, *addr++);
	}
}

void
shpcic_mem_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, *addr++);
	}
}

/*
 * write raw multi
 */

void
shpcic_io_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		__shpcic_io_write_2(bsh, offset, *(uint16_t *)addr);
		addr += 2;
	}
}

void
shpcic_io_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		__shpcic_io_write_4(bsh, offset, *(uint32_t *)addr);
		addr += 4;
	}
}

void
shpcic_mem_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, *(uint16_t *)addr);
		addr += 2;
	}
}

void
shpcic_mem_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, *(uint32_t *)addr);
		addr += 4;
	}
}

/*
 * write region
 */
void
shpcic_io_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_1(bsh, offset, *addr++);
		offset += 1;
	}
}

void
shpcic_io_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_2(bsh, offset, *addr++);
		offset += 2;
	}
}

void
shpcic_io_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_4(bsh, offset, *addr++);
		offset += 4;
	}
}

void
shpcic_mem_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_1(bsh, offset, *addr++);
		offset += 1;
	}
}

void
shpcic_mem_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, *addr++);
		offset += 2;
	}
}

void
shpcic_mem_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, *addr++);
		offset += 4;
	}
}

/*
 * write raw region
 */

void
shpcic_io_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		__shpcic_io_write_2(bsh, offset, *(uint16_t *)addr);
		addr += 2;
		offset += 2;
	}
}

void
shpcic_io_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		__shpcic_io_write_4(bsh, offset, *(uint32_t *)addr);
		addr += 4;
		offset += 4;
	}
}

void
shpcic_mem_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 1;
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, *(uint16_t *)addr);
		addr += 2;
		offset += 2;
	}
}

void
shpcic_mem_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	count >>= 2;
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, *(uint32_t *)addr);
		addr += 4;
		offset += 4;
	}
}

/*
 * set multi
 */
void
shpcic_io_set_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_1(bsh, offset, value);
	}
}

void
shpcic_io_set_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_2(bsh, offset, value);
	}
}

void
shpcic_io_set_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_4(bsh, offset, value);
	}
}

void
shpcic_mem_set_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_1(bsh, offset, value);
	}
}

void
shpcic_mem_set_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, value);
	}
}

void
shpcic_mem_set_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, value);
	}
}

/*
 * set region
 */
void
shpcic_io_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_1(bsh, offset, value);
		offset += 1;
	}
}

void
shpcic_io_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_2(bsh, offset, value);
		offset += 2;
	}
}

void
shpcic_io_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_io_write_4(bsh, offset, value);
		offset += 4;
	}
}

void
shpcic_mem_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_1(bsh, offset, value);
		offset += 1;
	}
}

void
shpcic_mem_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_2(bsh, offset, value);
		offset += 2;
	}
}

void
shpcic_mem_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, bus_size_t count)
{
	while (count--) {
		__shpcic_mem_write_4(bsh, offset, value);
		offset += 4;
	}
}

/*
 * copy region
 */
void
shpcic_io_copy_1(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint8_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_io_read_1(bsh1, off1);
			__shpcic_io_write_1(bsh2, off2, value);
			off1 += 1;
			off2 += 1;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 1;
		off2 += (count - 1) * 1;
		while (count--) {
			value = __shpcic_io_read_1(bsh1, off1);
			__shpcic_io_write_1(bsh2, off2, value);
			off1 -= 1;
			off2 -= 1;
		}
	}
}

void
shpcic_io_copy_2(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint16_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_io_read_2(bsh1, off1);
			__shpcic_io_write_2(bsh2, off2, value);
			off1 += 2;
			off2 += 2;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 2;
		off2 += (count - 1) * 2;
		while (count--) {
			value = __shpcic_io_read_2(bsh1, off1);
			__shpcic_io_write_2(bsh2, off2, value);
			off1 -= 2;
			off2 -= 2;
		}
	}
}

void
shpcic_io_copy_4(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint32_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_io_read_4(bsh1, off1);
			__shpcic_io_write_4(bsh2, off2, value);
			off1 += 4;
			off2 += 4;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 4;
		off2 += (count - 1) * 4;
		while (count--) {
			value = __shpcic_io_read_4(bsh1, off1);
			__shpcic_io_write_4(bsh2, off2, value);
			off1 -= 4;
			off2 -= 4;
		}
	}
}

void
shpcic_mem_copy_1(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint8_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_mem_read_1(bsh1, off1);
			__shpcic_mem_write_1(bsh2, off2, value);
			off1 += 1;
			off2 += 1;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 1;
		off2 += (count - 1) * 1;
		while (count--) {
			value = __shpcic_mem_read_1(bsh1, off1);
			__shpcic_mem_write_1(bsh2, off2, value);
			off1 -= 1;
			off2 -= 1;
		}
	}
}

void
shpcic_mem_copy_2(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint16_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_mem_read_2(bsh1, off1);
			__shpcic_mem_write_2(bsh2, off2, value);
			off1 += 2;
			off2 += 2;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 2;
		off2 += (count - 1) * 2;
		while (count--) {
			value = __shpcic_mem_read_2(bsh1, off1);
			__shpcic_mem_write_2(bsh2, off2, value);
			off1 -= 2;
			off2 -= 2;
		}
	}
}

void
shpcic_mem_copy_4(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2, bus_size_t count)
{
	u_long addr1 = bsh1 + off1;
	u_long addr2 = bsh2 + off2;
	uint32_t value;

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			value = __shpcic_mem_read_4(bsh1, off1);
			__shpcic_mem_write_4(bsh2, off2, value);
			off1 += 4;
			off2 += 4;
		}
	} else {		/* dest after src: copy backwards */
		off1 += (count - 1) * 4;
		off2 += (count - 1) * 4;
		while (count--) {
			value = __shpcic_mem_read_4(bsh1, off1);
			__shpcic_mem_write_4(bsh2, off2, value);
			off1 -= 4;
			off2 -= 4;
		}
	}
}
