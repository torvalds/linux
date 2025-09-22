/*	$OpenBSD: obio.c,v 1.12 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: obio.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

#include <uvm/uvm_extern.h>

#include <sh/devreg.h>
#include <sh/mmu.h>
#include <sh/pmap.h>
#include <sh/pte.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <landisk/dev/obiovar.h>

int	obio_match(struct device *, void *, void *);
void	obio_attach(struct device *, struct device *, void *);
int	obio_print(void *, const char *);
int	obio_search(struct device *, void *, void *);

const struct cfattach obio_ca = {
	sizeof(struct obio_softc), obio_match, obio_attach
};

struct cfdriver obio_cd = {
	NULL, "obio", DV_DULL
};

int
obio_match(struct device *parent, void *vcf, void *aux)
{
	struct obiobus_attach_args *oba = aux;

	if (strcmp(oba->oba_busname, obio_cd.cd_name) != 0)
		return (0);

	return (1);
}

void
obio_attach(struct device *parent, struct device *self, void *aux)
{
	struct obio_softc *sc = (struct obio_softc *)self;
	struct obiobus_attach_args *oba = aux;

	printf("\n");

	sc->sc_iot = oba->oba_iot;
	sc->sc_memt = oba->oba_memt;

	config_search(obio_search, self, NULL);
}

int
obio_search(struct device *parent, void *vcf, void *aux)
{
	struct obio_softc *sc = (struct obio_softc *)parent;
	struct cfdata *cf = vcf;
	struct obio_attach_args oa;
	struct obio_io res_io[1];
	struct obio_iomem res_mem[1];
	struct obio_irq res_irq[1];

	oa.oa_iot = sc->sc_iot;
	oa.oa_memt = sc->sc_memt;
	oa.oa_nio = oa.oa_niomem = oa.oa_nirq = 0;

	if (cf->cf_iobase != IOBASEUNK) {
		res_io[0].or_addr = cf->cf_iobase;
		res_io[0].or_size = cf->cf_iosize;
		oa.oa_io = res_io;
		oa.oa_nio = 1;
	}

	if (cf->cf_maddr != MADDRUNK) {
		res_mem[0].or_addr = cf->cf_maddr;
		res_mem[0].or_size = cf->cf_msize;
		oa.oa_iomem = res_mem;
		oa.oa_niomem = 1;
	}

	if (cf->cf_irq != IRQUNK) {
		res_irq[0].or_irq = cf->cf_irq;
		oa.oa_irq = res_irq;
		oa.oa_nirq = 1;
	}

	if ((*cf->cf_attach->ca_match)(parent, cf, &oa) == 0)
		return (0);

	config_attach(parent, cf, &oa, obio_print);
	return (1);
}

int
obio_print(void *args, const char *name)
{
	struct obio_attach_args *oa = args;
	const char *sep;
	int i;

	if (oa->oa_nio) {
		sep = "";
		printf(" port ");
		for (i = 0; i < oa->oa_nio; i++) {
			if (oa->oa_io[i].or_size == 0)
				continue;
			printf("%s0x%x", sep, oa->oa_io[i].or_addr);
			if (oa->oa_io[i].or_size > 1)
				printf("-0x%x", oa->oa_io[i].or_addr +
				    oa->oa_io[i].or_size - 1);
			sep = ",";
		}
	}

	if (oa->oa_niomem) {
		sep = "";
		printf(" iomem ");
		for (i = 0; i < oa->oa_niomem; i++) {
			if (oa->oa_iomem[i].or_size == 0)
				continue;
			printf("%s0x%x", sep, oa->oa_iomem[i].or_addr);
			if (oa->oa_iomem[i].or_size > 1)
				printf("-0x%x", oa->oa_iomem[i].or_addr +
				    oa->oa_iomem[i].or_size - 1);
			sep = ",";
		}
	}

	if (oa->oa_nirq) {
		sep = "";
		printf(" irq ");
		for (i = 0; i < oa->oa_nirq; i++) {
			if (oa->oa_irq[i].or_irq == IRQUNK)
				continue;
			printf("%s%d", sep, oa->oa_irq[i].or_irq);
			sep = ",";
		}
	}

	return (UNCONF);
}

/*
 * Set up an interrupt handler to start being called.
 */
void *
obio_intr_establish(int irq, int level, int (*ih_fun)(void *), void *ih_arg,
    const char *ih_name)
{
	return extintr_establish(irq, level, ih_fun, ih_arg, ih_name);
}

/*
 * Deregister an interrupt handler.
 */
void
obio_intr_disestablish(void *arg)
{
	extintr_disestablish(arg);
}

/*
 * on-board I/O bus space
 */
#define	OBIO_IOMEM_IO		0	/* space is i/o space */
#define	OBIO_IOMEM_MEM		1	/* space is mem space */
#define	OBIO_IOMEM_PCMCIA_IO	2	/* PCMCIA IO space */
#define	OBIO_IOMEM_PCMCIA_MEM	3	/* PCMCIA Mem space */
#define	OBIO_IOMEM_PCMCIA_ATT	4	/* PCMCIA Attr space */
#define	OBIO_IOMEM_PCMCIA_8BIT	0x8000	/* PCMCIA BUS 8 BIT WIDTH */
#define	OBIO_IOMEM_PCMCIA_IO8 \
	    (OBIO_IOMEM_PCMCIA_IO|OBIO_IOMEM_PCMCIA_8BIT)
#define	OBIO_IOMEM_PCMCIA_MEM8 \
	    (OBIO_IOMEM_PCMCIA_MEM|OBIO_IOMEM_PCMCIA_8BIT)
#define	OBIO_IOMEM_PCMCIA_ATT8 \
	    (OBIO_IOMEM_PCMCIA_ATT|OBIO_IOMEM_PCMCIA_8BIT)

int obio_iomem_map(void *v, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp);
void obio_iomem_unmap(void *v, bus_space_handle_t bsh, bus_size_t size);
int obio_iomem_subregion(void *v, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp);
int obio_iomem_alloc(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp);
void obio_iomem_free(void *v, bus_space_handle_t bsh, bus_size_t size);
void *obio_iomem_vaddr(void *v, bus_space_handle_t bsh);

int obio_iomem_add_mapping(bus_addr_t, bus_size_t, int,
    bus_space_handle_t *);

int
obio_iomem_add_mapping(bus_addr_t bpa, bus_size_t size, int type,
    bus_space_handle_t *bshp)
{
	u_long pa, endpa;
	vaddr_t va;
	pt_entry_t *pte;
	unsigned int m = 0;
	int io_type = type & ~OBIO_IOMEM_PCMCIA_8BIT;

	pa = trunc_page(bpa);
	endpa = round_page(bpa + size);

#ifdef DIAGNOSTIC
	if (endpa <= pa)
		panic("obio_iomem_add_mapping: overflow");
#endif

	va = (vaddr_t)km_alloc(endpa - pa, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return (ENOMEM);

	*bshp = (bus_space_handle_t)(va + (bpa & PGOFSET));

#define MODE(t, s)							\
	((t) & OBIO_IOMEM_PCMCIA_8BIT) ?				\
		_PG_PCMCIA_ ## s ## 8 :					\
		_PG_PCMCIA_ ## s ## 16
	switch (io_type) {
	default:
		panic("unknown pcmcia space.");
		/* NOTREACHED */
	case OBIO_IOMEM_PCMCIA_IO:
		m = MODE(type, IO);
		break;
	case OBIO_IOMEM_PCMCIA_MEM:
		m = MODE(type, MEM);
		break;
	case OBIO_IOMEM_PCMCIA_ATT:
		m = MODE(type, ATTR);
		break;
	}
#undef MODE

	for (; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE) {
		pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);
		pte = __pmap_kpte_lookup(va);
		KDASSERT(pte);
		*pte |= m;  /* PTEA PCMCIA assistant bit */
		sh_tlb_update(0, va, *pte);
	}

	return (0);
}

int
obio_iomem_map(void *v, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	bus_addr_t addr = SH3_PHYS_TO_P2SEG(bpa);
	int error;

	KASSERT((bpa & SH3_PHYS_MASK) == bpa);

	if (bpa < 0x14000000 || bpa >= 0x1c000000) {
		/* CS0,1,2,3,4,7 */
		*bshp = (bus_space_handle_t)addr;
		return (0);
	}

	/* CS5,6 */
	error = obio_iomem_add_mapping(addr, size, (int)(u_long)v, bshp);

	return (error);
}

void
obio_iomem_unmap(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	u_long va, endva;
	bus_addr_t bpa;

	if (bsh >= SH3_P2SEG_BASE && bsh <= SH3_P2SEG_END) {
		/* maybe CS0,1,2,3,4,7 */
		return;
	}

	/* CS5,6 */
	va = trunc_page(bsh);
	endva = round_page(bsh + size);

#ifdef DIAGNOSTIC
	if (endva <= va)
		panic("obio_io_unmap: overflow");
#endif

	pmap_extract(pmap_kernel(), va, &bpa);
	bpa += bsh & PGOFSET;

	pmap_kremove(va, endva - va);

	/*
	 * Free the kernel virtual mapping.
	 */
	km_free((void *)va, endva - va, &kv_any, &kp_none);
}

int
obio_iomem_subregion(void *v, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;

	return (0);
}

int
obio_iomem_alloc(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp)
{
	*bshp = *bpap = rstart;

	return (0);
}

void
obio_iomem_free(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	obio_iomem_unmap(v, bsh, size);
}

void *
obio_iomem_vaddr(void *v, bus_space_handle_t bsh)
{
	return ((void *)bsh);
}

/*
 * on-board I/O bus space read/write
 */
uint8_t obio_iomem_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint16_t obio_iomem_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint32_t obio_iomem_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset);
void obio_iomem_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void obio_iomem_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);
void obio_iomem_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void obio_iomem_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);
void obio_iomem_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void obio_iomem_write_1(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint8_t value);
void obio_iomem_write_2(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint16_t value);
void obio_iomem_write_4(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint32_t value);
void obio_iomem_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void obio_iomem_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);
void obio_iomem_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void obio_iomem_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);
void obio_iomem_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void obio_iomem_set_multi_1(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint8_t val, bus_size_t count);
void obio_iomem_set_multi_2(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint16_t val, bus_size_t count);
void obio_iomem_set_multi_4(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint32_t val, bus_size_t count);
void obio_iomem_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count);
void obio_iomem_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count);
void obio_iomem_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count);
void obio_iomem_copy_1(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count);
void obio_iomem_copy_2(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count);
void obio_iomem_copy_4(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count);

struct _bus_space obio_bus_io =
{
	.bs_cookie = (void *)OBIO_IOMEM_PCMCIA_IO,

	.bs_map = obio_iomem_map,
	.bs_unmap = obio_iomem_unmap,
	.bs_subregion = obio_iomem_subregion,

	.bs_alloc = obio_iomem_alloc,
	.bs_free = obio_iomem_free,

	.bs_vaddr = obio_iomem_vaddr,

	.bs_r_1 = obio_iomem_read_1,
	.bs_r_2 = obio_iomem_read_2,
	.bs_r_4 = obio_iomem_read_4,

	.bs_rm_1 = obio_iomem_read_multi_1,
	.bs_rm_2 = obio_iomem_read_multi_2,
	.bs_rm_4 = obio_iomem_read_multi_4,

	.bs_rrm_2 = obio_iomem_read_raw_multi_2,
	.bs_rrm_4 = obio_iomem_read_raw_multi_4,

	.bs_rr_1 = obio_iomem_read_region_1,
	.bs_rr_2 = obio_iomem_read_region_2,
	.bs_rr_4 = obio_iomem_read_region_4,

	.bs_rrr_2 = obio_iomem_read_raw_region_2,
	.bs_rrr_4 = obio_iomem_read_raw_region_4,

	.bs_w_1 = obio_iomem_write_1,
	.bs_w_2 = obio_iomem_write_2,
	.bs_w_4 = obio_iomem_write_4,

	.bs_wm_1 = obio_iomem_write_multi_1,
	.bs_wm_2 = obio_iomem_write_multi_2,
	.bs_wm_4 = obio_iomem_write_multi_4,

	.bs_wrm_2 = obio_iomem_write_raw_multi_2,
	.bs_wrm_4 = obio_iomem_write_raw_multi_4,

	.bs_wr_1 = obio_iomem_write_region_1,
	.bs_wr_2 = obio_iomem_write_region_2,
	.bs_wr_4 = obio_iomem_write_region_4,

	.bs_wrr_2 = obio_iomem_write_raw_region_2,
	.bs_wrr_4 = obio_iomem_write_raw_region_4,

	.bs_sm_1 = obio_iomem_set_multi_1,
	.bs_sm_2 = obio_iomem_set_multi_2,
	.bs_sm_4 = obio_iomem_set_multi_4,

	.bs_sr_1 = obio_iomem_set_region_1,
	.bs_sr_2 = obio_iomem_set_region_2,
	.bs_sr_4 = obio_iomem_set_region_4,

	.bs_c_1 = obio_iomem_copy_1,
	.bs_c_2 = obio_iomem_copy_2,
	.bs_c_4 = obio_iomem_copy_4,
};

struct _bus_space obio_bus_mem =
{
	.bs_cookie = (void *)OBIO_IOMEM_PCMCIA_MEM,

	.bs_map = obio_iomem_map,
	.bs_unmap = obio_iomem_unmap,
	.bs_subregion = obio_iomem_subregion,

	.bs_alloc = obio_iomem_alloc,
	.bs_free = obio_iomem_free,

	.bs_vaddr = obio_iomem_vaddr,

	.bs_r_1 = obio_iomem_read_1,
	.bs_r_2 = obio_iomem_read_2,
	.bs_r_4 = obio_iomem_read_4,

	.bs_rm_1 = obio_iomem_read_multi_1,
	.bs_rm_2 = obio_iomem_read_multi_2,
	.bs_rm_4 = obio_iomem_read_multi_4,

	.bs_rrm_2 = obio_iomem_read_raw_multi_2,
	.bs_rrm_4 = obio_iomem_read_raw_multi_4,

	.bs_rr_1 = obio_iomem_read_region_1,
	.bs_rr_2 = obio_iomem_read_region_2,
	.bs_rr_4 = obio_iomem_read_region_4,

	.bs_rrr_2 = obio_iomem_read_raw_region_2,
	.bs_rrr_4 = obio_iomem_read_raw_region_4,

	.bs_w_1 = obio_iomem_write_1,
	.bs_w_2 = obio_iomem_write_2,
	.bs_w_4 = obio_iomem_write_4,

	.bs_wm_1 = obio_iomem_write_multi_1,
	.bs_wm_2 = obio_iomem_write_multi_2,
	.bs_wm_4 = obio_iomem_write_multi_4,

	.bs_wrm_2 = obio_iomem_write_raw_multi_2,
	.bs_wrm_4 = obio_iomem_write_raw_multi_4,

	.bs_wr_1 = obio_iomem_write_region_1,
	.bs_wr_2 = obio_iomem_write_region_2,
	.bs_wr_4 = obio_iomem_write_region_4,

	.bs_wrr_2 = obio_iomem_write_raw_region_2,
	.bs_wrr_4 = obio_iomem_write_raw_region_4,

	.bs_sm_1 = obio_iomem_set_multi_1,
	.bs_sm_2 = obio_iomem_set_multi_2,
	.bs_sm_4 = obio_iomem_set_multi_4,

	.bs_sr_1 = obio_iomem_set_region_1,
	.bs_sr_2 = obio_iomem_set_region_2,
	.bs_sr_4 = obio_iomem_set_region_4,

	.bs_c_1 = obio_iomem_copy_1,
	.bs_c_2 = obio_iomem_copy_2,
	.bs_c_4 = obio_iomem_copy_4,
};

/* read */
uint8_t
obio_iomem_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	return *(volatile uint8_t *)(bsh + offset);
}

uint16_t
obio_iomem_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	return *(volatile uint16_t *)(bsh + offset);
}

uint32_t
obio_iomem_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset)
{
	return *(volatile uint32_t *)(bsh + offset);
}

void
obio_iomem_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint8_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p;
	}
}

void
obio_iomem_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p;
	}
}

void
obio_iomem_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p;
	}
}

void
obio_iomem_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = *p;
		addr += 2;
	}
}

void
obio_iomem_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = *p;
		addr += 4;
	}
}

void
obio_iomem_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint8_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p++;
	}
}

void
obio_iomem_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p++;
	}
}

void
obio_iomem_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	while (count--) {
		*addr++ = *p++;
	}
}

void
obio_iomem_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	count >>= 1;
	while (count--) {
		*(uint16_t *)addr = *p++;
		addr += 2;
	}
}

void
obio_iomem_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	count >>= 2;
	while (count--) {
		*(uint32_t *)addr = *p++;
		addr += 4;
	}
}

/* write */
void
obio_iomem_write_1(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint8_t value)
{
	*(volatile uint8_t *)(bsh + offset) = value;
}

void
obio_iomem_write_2(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint16_t value)
{
	*(volatile uint16_t *)(bsh + offset) = value;
}

void
obio_iomem_write_4(void *v, bus_space_handle_t bsh, bus_size_t offset,
    uint32_t value)
{
	*(volatile uint32_t *)(bsh + offset) = value;
}

void
obio_iomem_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint8_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = *addr++;
	}
}

void
obio_iomem_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = *addr++;
	}
}

void
obio_iomem_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = *addr++;
	}
}

void
obio_iomem_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	count >>= 1;
	while (count--) {
		*p = *(uint16_t *)addr;
		addr += 2;
	}
}

void
obio_iomem_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	count >>= 2;
	while (count--) {
		*p = *(uint32_t *)addr;
		addr += 4;
	}
}

void
obio_iomem_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint8_t *p = (void *)(bsh + offset);

	while (count--) {
		*p++ = *addr++;
	}
}

void
obio_iomem_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	while (count--) {
		*p++ = *addr++;
	}
}

void
obio_iomem_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	while (count--) {
		*p++ = *addr++;
	}
}

void
obio_iomem_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	count >>= 1;
	while (count--) {
		*p++ = *(uint16_t *)addr;
		addr += 2;
	}
}

void
obio_iomem_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	count >>= 2;
	while (count--) {
		*p++ = *(uint32_t *)addr;
		addr += 4;
	}
}

void
obio_iomem_set_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count)
{
	volatile uint8_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = val;
	}
}

void
obio_iomem_set_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count)
{
	volatile uint16_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = val;
	}
}

void
obio_iomem_set_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count)
{
	volatile uint32_t *p = (void *)(bsh + offset);

	while (count--) {
		*p = val;
	}
}

void
obio_iomem_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count)
{
	volatile uint8_t *addr = (void *)(bsh + offset);

	while (count--) {
		*addr++ = val;
	}
}

void
obio_iomem_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count)
{
	volatile uint16_t *addr = (void *)(bsh + offset);

	while (count--) {
		*addr++ = val;
	}
}

void
obio_iomem_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count)
{
	volatile uint32_t *addr = (void *)(bsh + offset);

	while (count--) {
		*addr++ = val;
	}
}

void
obio_iomem_copy_1(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count)
{
	volatile uint8_t *addr1 = (void *)(h1 + o1);
	volatile uint8_t *addr2 = (void *)(h2 + o2);

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			*addr2++ = *addr1++;
		}
	} else {		/* dest after src: copy backwards */
		addr1 += count - 1;
		addr2 += count - 1;
		while (count--) {
			*addr2-- = *addr1--;
		}
	}
}

void
obio_iomem_copy_2(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count)
{
	volatile uint16_t *addr1 = (void *)(h1 + o1);
	volatile uint16_t *addr2 = (void *)(h2 + o2);

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			*addr2++ = *addr1++;
		}
	} else {		/* dest after src: copy backwards */
		addr1 += count - 1;
		addr2 += count - 1;
		while (count--) {
			*addr2-- = *addr1--;
		}
	}
}

void
obio_iomem_copy_4(void *v, bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, bus_size_t count)
{
	volatile uint32_t *addr1 = (void *)(h1 + o1);
	volatile uint32_t *addr2 = (void *)(h2 + o2);

	if (addr1 >= addr2) {	/* src after dest: copy forward */
		while (count--) {
			*addr2++ = *addr1++;
		}
	} else {		/* dest after src: copy backwards */
		addr1 += count - 1;
		addr2 += count - 1;
		while (count--) {
			*addr2-- = *addr1--;
		}
	}
}
