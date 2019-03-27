/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/pci/pcireg.h>

#include <machine/vmparam.h>
#include <contrib/dev/acpica/include/acpi.h>

#include "io/iommu.h"

/*
 * Documented in the "Intel Virtualization Technology for Directed I/O",
 * Architecture Spec, September 2008.
 */

/* Section 10.4 "Register Descriptions" */
struct vtdmap {
	volatile uint32_t	version;
	volatile uint32_t	res0;
	volatile uint64_t	cap;
	volatile uint64_t	ext_cap;
	volatile uint32_t	gcr;
	volatile uint32_t	gsr;
	volatile uint64_t	rta;
	volatile uint64_t	ccr;
};

#define	VTD_CAP_SAGAW(cap)	(((cap) >> 8) & 0x1F)
#define	VTD_CAP_ND(cap)		((cap) & 0x7)
#define	VTD_CAP_CM(cap)		(((cap) >> 7) & 0x1)
#define	VTD_CAP_SPS(cap)	(((cap) >> 34) & 0xF)
#define	VTD_CAP_RWBF(cap)	(((cap) >> 4) & 0x1)

#define	VTD_ECAP_DI(ecap)	(((ecap) >> 2) & 0x1)
#define	VTD_ECAP_COHERENCY(ecap) ((ecap) & 0x1)
#define	VTD_ECAP_IRO(ecap)	(((ecap) >> 8) & 0x3FF)

#define	VTD_GCR_WBF		(1 << 27)
#define	VTD_GCR_SRTP		(1 << 30)
#define	VTD_GCR_TE		(1U << 31)

#define	VTD_GSR_WBFS		(1 << 27)
#define	VTD_GSR_RTPS		(1 << 30)
#define	VTD_GSR_TES		(1U << 31)

#define	VTD_CCR_ICC		(1UL << 63)	/* invalidate context cache */
#define	VTD_CCR_CIRG_GLOBAL	(1UL << 61)	/* global invalidation */

#define	VTD_IIR_IVT		(1UL << 63)	/* invalidation IOTLB */
#define	VTD_IIR_IIRG_GLOBAL	(1ULL << 60)	/* global IOTLB invalidation */
#define	VTD_IIR_IIRG_DOMAIN	(2ULL << 60)	/* domain IOTLB invalidation */
#define	VTD_IIR_IIRG_PAGE	(3ULL << 60)	/* page IOTLB invalidation */
#define	VTD_IIR_DRAIN_READS	(1ULL << 49)	/* drain pending DMA reads */
#define	VTD_IIR_DRAIN_WRITES	(1ULL << 48)	/* drain pending DMA writes */
#define	VTD_IIR_DOMAIN_P	32

#define	VTD_ROOT_PRESENT	0x1
#define	VTD_CTX_PRESENT		0x1
#define	VTD_CTX_TT_ALL		(1UL << 2)

#define	VTD_PTE_RD		(1UL << 0)
#define	VTD_PTE_WR		(1UL << 1)
#define	VTD_PTE_SUPERPAGE	(1UL << 7)
#define	VTD_PTE_ADDR_M		(0x000FFFFFFFFFF000UL)

#define VTD_RID2IDX(rid)	(((rid) & 0xff) * 2)

struct domain {
	uint64_t	*ptp;		/* first level page table page */
	int		pt_levels;	/* number of page table levels */
	int		addrwidth;	/* 'AW' field in context entry */
	int		spsmask;	/* supported super page sizes */
	u_int		id;		/* domain id */
	vm_paddr_t	maxaddr;	/* highest address to be mapped */
	SLIST_ENTRY(domain) next;
};

static SLIST_HEAD(, domain) domhead;

#define	DRHD_MAX_UNITS	8
static int		drhd_num;
static struct vtdmap	*vtdmaps[DRHD_MAX_UNITS];
static int		max_domains;
typedef int		(*drhd_ident_func_t)(void);

static uint64_t root_table[PAGE_SIZE / sizeof(uint64_t)] __aligned(4096);
static uint64_t ctx_tables[256][PAGE_SIZE / sizeof(uint64_t)] __aligned(4096);

static MALLOC_DEFINE(M_VTD, "vtd", "vtd");

static int
vtd_max_domains(struct vtdmap *vtdmap)
{
	int nd;

	nd = VTD_CAP_ND(vtdmap->cap);

	switch (nd) {
	case 0:
		return (16);
	case 1:
		return (64);
	case 2:
		return (256);
	case 3:
		return (1024);
	case 4:
		return (4 * 1024);
	case 5:
		return (16 * 1024);
	case 6:
		return (64 * 1024);
	default:
		panic("vtd_max_domains: invalid value of nd (0x%0x)", nd);
	}
}

static u_int
domain_id(void)
{
	u_int id;
	struct domain *dom;

	/* Skip domain id 0 - it is reserved when Caching Mode field is set */
	for (id = 1; id < max_domains; id++) {
		SLIST_FOREACH(dom, &domhead, next) {
			if (dom->id == id)
				break;
		}
		if (dom == NULL)
			break;		/* found it */
	}
	
	if (id >= max_domains)
		panic("domain ids exhausted");

	return (id);
}

static void
vtd_wbflush(struct vtdmap *vtdmap)
{

	if (VTD_ECAP_COHERENCY(vtdmap->ext_cap) == 0)
		pmap_invalidate_cache();

	if (VTD_CAP_RWBF(vtdmap->cap)) {
		vtdmap->gcr = VTD_GCR_WBF;
		while ((vtdmap->gsr & VTD_GSR_WBFS) != 0)
			;
	}
}

static void
vtd_ctx_global_invalidate(struct vtdmap *vtdmap)
{

	vtdmap->ccr = VTD_CCR_ICC | VTD_CCR_CIRG_GLOBAL;
	while ((vtdmap->ccr & VTD_CCR_ICC) != 0)
		;
}

static void
vtd_iotlb_global_invalidate(struct vtdmap *vtdmap)
{
	int offset;
	volatile uint64_t *iotlb_reg, val;

	vtd_wbflush(vtdmap);

	offset = VTD_ECAP_IRO(vtdmap->ext_cap) * 16;
	iotlb_reg = (volatile uint64_t *)((caddr_t)vtdmap + offset + 8);
	
	*iotlb_reg =  VTD_IIR_IVT | VTD_IIR_IIRG_GLOBAL |
		      VTD_IIR_DRAIN_READS | VTD_IIR_DRAIN_WRITES;

	while (1) {
		val = *iotlb_reg;
		if ((val & VTD_IIR_IVT) == 0)
			break;
	}
}

static void
vtd_translation_enable(struct vtdmap *vtdmap)
{

	vtdmap->gcr = VTD_GCR_TE;
	while ((vtdmap->gsr & VTD_GSR_TES) == 0)
		;
}

static void
vtd_translation_disable(struct vtdmap *vtdmap)
{

	vtdmap->gcr = 0;
	while ((vtdmap->gsr & VTD_GSR_TES) != 0)
		;
}

static int
vtd_init(void)
{
	int i, units, remaining;
	struct vtdmap *vtdmap;
	vm_paddr_t ctx_paddr;
	char *end, envname[32];
	unsigned long mapaddr;
	ACPI_STATUS status;
	ACPI_TABLE_DMAR *dmar;
	ACPI_DMAR_HEADER *hdr;
	ACPI_DMAR_HARDWARE_UNIT *drhd;

	/*
	 * Allow the user to override the ACPI DMAR table by specifying the
	 * physical address of each remapping unit.
	 *
	 * The following example specifies two remapping units at
	 * physical addresses 0xfed90000 and 0xfeda0000 respectively.
	 * set vtd.regmap.0.addr=0xfed90000
	 * set vtd.regmap.1.addr=0xfeda0000
	 */
	for (units = 0; units < DRHD_MAX_UNITS; units++) {
		snprintf(envname, sizeof(envname), "vtd.regmap.%d.addr", units);
		if (getenv_ulong(envname, &mapaddr) == 0)
			break;
		vtdmaps[units] = (struct vtdmap *)PHYS_TO_DMAP(mapaddr);
	}

	if (units > 0)
		goto skip_dmar;

	/* Search for DMAR table. */
	status = AcpiGetTable(ACPI_SIG_DMAR, 0, (ACPI_TABLE_HEADER **)&dmar);
	if (ACPI_FAILURE(status))
		return (ENXIO);

	end = (char *)dmar + dmar->Header.Length;
	remaining = dmar->Header.Length - sizeof(ACPI_TABLE_DMAR);
	while (remaining > sizeof(ACPI_DMAR_HEADER)) {
		hdr = (ACPI_DMAR_HEADER *)(end - remaining);
		if (hdr->Length > remaining)
			break;
		/*
		 * From Intel VT-d arch spec, version 1.3:
		 * BIOS implementations must report mapping structures
		 * in numerical order, i.e. All remapping structures of
		 * type 0 (DRHD) enumerated before remapping structures of
		 * type 1 (RMRR) and so forth.
		 */
		if (hdr->Type != ACPI_DMAR_TYPE_HARDWARE_UNIT)
			break;

		drhd = (ACPI_DMAR_HARDWARE_UNIT *)hdr;
		vtdmaps[units++] = (struct vtdmap *)PHYS_TO_DMAP(drhd->Address);
		if (units >= DRHD_MAX_UNITS)
			break;
		remaining -= hdr->Length;
	}

	if (units <= 0)
		return (ENXIO);

skip_dmar:
	drhd_num = units;
	vtdmap = vtdmaps[0];

	if (VTD_CAP_CM(vtdmap->cap) != 0)
		panic("vtd_init: invalid caching mode");

	max_domains = vtd_max_domains(vtdmap);

	/*
	 * Set up the root-table to point to the context-entry tables
	 */
	for (i = 0; i < 256; i++) {
		ctx_paddr = vtophys(ctx_tables[i]);
		if (ctx_paddr & PAGE_MASK)
			panic("ctx table (0x%0lx) not page aligned", ctx_paddr);

		root_table[i * 2] = ctx_paddr | VTD_ROOT_PRESENT;
	}

	return (0);
}

static void
vtd_cleanup(void)
{
}

static void
vtd_enable(void)
{
	int i;
	struct vtdmap *vtdmap;

	for (i = 0; i < drhd_num; i++) {
		vtdmap = vtdmaps[i];
		vtd_wbflush(vtdmap);

		/* Update the root table address */
		vtdmap->rta = vtophys(root_table);
		vtdmap->gcr = VTD_GCR_SRTP;
		while ((vtdmap->gsr & VTD_GSR_RTPS) == 0)
			;

		vtd_ctx_global_invalidate(vtdmap);
		vtd_iotlb_global_invalidate(vtdmap);

		vtd_translation_enable(vtdmap);
	}
}

static void
vtd_disable(void)
{
	int i;
	struct vtdmap *vtdmap;

	for (i = 0; i < drhd_num; i++) {
		vtdmap = vtdmaps[i];
		vtd_translation_disable(vtdmap);
	}
}

static void
vtd_add_device(void *arg, uint16_t rid)
{
	int idx;
	uint64_t *ctxp;
	struct domain *dom = arg;
	vm_paddr_t pt_paddr;
	struct vtdmap *vtdmap;
	uint8_t bus;

	vtdmap = vtdmaps[0];
	bus = PCI_RID2BUS(rid);
	ctxp = ctx_tables[bus];
	pt_paddr = vtophys(dom->ptp);
	idx = VTD_RID2IDX(rid);

	if (ctxp[idx] & VTD_CTX_PRESENT) {
		panic("vtd_add_device: device %x is already owned by "
		      "domain %d", rid,
		      (uint16_t)(ctxp[idx + 1] >> 8));
	}

	/*
	 * Order is important. The 'present' bit is set only after all fields
	 * of the context pointer are initialized.
	 */
	ctxp[idx + 1] = dom->addrwidth | (dom->id << 8);

	if (VTD_ECAP_DI(vtdmap->ext_cap))
		ctxp[idx] = VTD_CTX_TT_ALL;
	else
		ctxp[idx] = 0;

	ctxp[idx] |= pt_paddr | VTD_CTX_PRESENT;

	/*
	 * 'Not Present' entries are not cached in either the Context Cache
	 * or in the IOTLB, so there is no need to invalidate either of them.
	 */
}

static void
vtd_remove_device(void *arg, uint16_t rid)
{
	int i, idx;
	uint64_t *ctxp;
	struct vtdmap *vtdmap;
	uint8_t bus;

	bus = PCI_RID2BUS(rid);
	ctxp = ctx_tables[bus];
	idx = VTD_RID2IDX(rid);

	/*
	 * Order is important. The 'present' bit is must be cleared first.
	 */
	ctxp[idx] = 0;
	ctxp[idx + 1] = 0;

	/*
	 * Invalidate the Context Cache and the IOTLB.
	 *
	 * XXX use device-selective invalidation for Context Cache
	 * XXX use domain-selective invalidation for IOTLB
	 */
	for (i = 0; i < drhd_num; i++) {
		vtdmap = vtdmaps[i];
		vtd_ctx_global_invalidate(vtdmap);
		vtd_iotlb_global_invalidate(vtdmap);
	}
}

#define	CREATE_MAPPING	0
#define	REMOVE_MAPPING	1

static uint64_t
vtd_update_mapping(void *arg, vm_paddr_t gpa, vm_paddr_t hpa, uint64_t len,
		   int remove)
{
	struct domain *dom;
	int i, spshift, ptpshift, ptpindex, nlevels;
	uint64_t spsize, *ptp;

	dom = arg;
	ptpindex = 0;
	ptpshift = 0;

	KASSERT(gpa + len > gpa, ("%s: invalid gpa range %#lx/%#lx", __func__,
	    gpa, len));
	KASSERT(gpa + len <= dom->maxaddr, ("%s: gpa range %#lx/%#lx beyond "
	    "domain maxaddr %#lx", __func__, gpa, len, dom->maxaddr));

	if (gpa & PAGE_MASK)
		panic("vtd_create_mapping: unaligned gpa 0x%0lx", gpa);

	if (hpa & PAGE_MASK)
		panic("vtd_create_mapping: unaligned hpa 0x%0lx", hpa);

	if (len & PAGE_MASK)
		panic("vtd_create_mapping: unaligned len 0x%0lx", len);

	/*
	 * Compute the size of the mapping that we can accommodate.
	 *
	 * This is based on three factors:
	 * - supported super page size
	 * - alignment of the region starting at 'gpa' and 'hpa'
	 * - length of the region 'len'
	 */
	spshift = 48;
	for (i = 3; i >= 0; i--) {
		spsize = 1UL << spshift;
		if ((dom->spsmask & (1 << i)) != 0 &&
		    (gpa & (spsize - 1)) == 0 &&
		    (hpa & (spsize - 1)) == 0 &&
		    (len >= spsize)) {
			break;
		}
		spshift -= 9;
	}

	ptp = dom->ptp;
	nlevels = dom->pt_levels;
	while (--nlevels >= 0) {
		ptpshift = 12 + nlevels * 9;
		ptpindex = (gpa >> ptpshift) & 0x1FF;

		/* We have reached the leaf mapping */
		if (spshift >= ptpshift) {
			break;
		}

		/*
		 * We are working on a non-leaf page table page.
		 *
		 * Create a downstream page table page if necessary and point
		 * to it from the current page table.
		 */
		if (ptp[ptpindex] == 0) {
			void *nlp = malloc(PAGE_SIZE, M_VTD, M_WAITOK | M_ZERO);
			ptp[ptpindex] = vtophys(nlp)| VTD_PTE_RD | VTD_PTE_WR;
		}

		ptp = (uint64_t *)PHYS_TO_DMAP(ptp[ptpindex] & VTD_PTE_ADDR_M);
	}

	if ((gpa & ((1UL << ptpshift) - 1)) != 0)
		panic("gpa 0x%lx and ptpshift %d mismatch", gpa, ptpshift);

	/*
	 * Update the 'gpa' -> 'hpa' mapping
	 */
	if (remove) {
		ptp[ptpindex] = 0;
	} else {
		ptp[ptpindex] = hpa | VTD_PTE_RD | VTD_PTE_WR;

		if (nlevels > 0)
			ptp[ptpindex] |= VTD_PTE_SUPERPAGE;
	}

	return (1UL << ptpshift);
}

static uint64_t
vtd_create_mapping(void *arg, vm_paddr_t gpa, vm_paddr_t hpa, uint64_t len)
{

	return (vtd_update_mapping(arg, gpa, hpa, len, CREATE_MAPPING));
}

static uint64_t
vtd_remove_mapping(void *arg, vm_paddr_t gpa, uint64_t len)
{

	return (vtd_update_mapping(arg, gpa, 0, len, REMOVE_MAPPING));
}

static void
vtd_invalidate_tlb(void *dom)
{
	int i;
	struct vtdmap *vtdmap;

	/*
	 * Invalidate the IOTLB.
	 * XXX use domain-selective invalidation for IOTLB
	 */
	for (i = 0; i < drhd_num; i++) {
		vtdmap = vtdmaps[i];
		vtd_iotlb_global_invalidate(vtdmap);
	}
}

static void *
vtd_create_domain(vm_paddr_t maxaddr)
{
	struct domain *dom;
	vm_paddr_t addr;
	int tmp, i, gaw, agaw, sagaw, res, pt_levels, addrwidth;
	struct vtdmap *vtdmap;

	if (drhd_num <= 0)
		panic("vtd_create_domain: no dma remapping hardware available");

	vtdmap = vtdmaps[0];

	/*
	 * Calculate AGAW.
	 * Section 3.4.2 "Adjusted Guest Address Width", Architecture Spec.
	 */
	addr = 0;
	for (gaw = 0; addr < maxaddr; gaw++)
		addr = 1ULL << gaw;

	res = (gaw - 12) % 9;
	if (res == 0)
		agaw = gaw;
	else
		agaw = gaw + 9 - res;

	if (agaw > 64)
		agaw = 64;

	/*
	 * Select the smallest Supported AGAW and the corresponding number
	 * of page table levels.
	 */
	pt_levels = 2;
	sagaw = 30;
	addrwidth = 0;
	tmp = VTD_CAP_SAGAW(vtdmap->cap);
	for (i = 0; i < 5; i++) {
		if ((tmp & (1 << i)) != 0 && sagaw >= agaw)
			break;
		pt_levels++;
		addrwidth++;
		sagaw += 9;
		if (sagaw > 64)
			sagaw = 64;
	}

	if (i >= 5) {
		panic("vtd_create_domain: SAGAW 0x%lx does not support AGAW %d",
		      VTD_CAP_SAGAW(vtdmap->cap), agaw);
	}

	dom = malloc(sizeof(struct domain), M_VTD, M_ZERO | M_WAITOK);
	dom->pt_levels = pt_levels;
	dom->addrwidth = addrwidth;
	dom->id = domain_id();
	dom->maxaddr = maxaddr;
	dom->ptp = malloc(PAGE_SIZE, M_VTD, M_ZERO | M_WAITOK);
	if ((uintptr_t)dom->ptp & PAGE_MASK)
		panic("vtd_create_domain: ptp (%p) not page aligned", dom->ptp);

#ifdef notyet
	/*
	 * XXX superpage mappings for the iommu do not work correctly.
	 *
	 * By default all physical memory is mapped into the host_domain.
	 * When a VM is allocated wired memory the pages belonging to it
	 * are removed from the host_domain and added to the vm's domain.
	 *
	 * If the page being removed was mapped using a superpage mapping
	 * in the host_domain then we need to demote the mapping before
	 * removing the page.
	 *
	 * There is not any code to deal with the demotion at the moment
	 * so we disable superpage mappings altogether.
	 */
	dom->spsmask = VTD_CAP_SPS(vtdmap->cap);
#endif

	SLIST_INSERT_HEAD(&domhead, dom, next);

	return (dom);
}

static void
vtd_free_ptp(uint64_t *ptp, int level)
{
	int i;
	uint64_t *nlp;

	if (level > 1) {
		for (i = 0; i < 512; i++) {
			if ((ptp[i] & (VTD_PTE_RD | VTD_PTE_WR)) == 0)
				continue;
			if ((ptp[i] & VTD_PTE_SUPERPAGE) != 0)
				continue;
			nlp = (uint64_t *)PHYS_TO_DMAP(ptp[i] & VTD_PTE_ADDR_M);
			vtd_free_ptp(nlp, level - 1);
		}
	}

	bzero(ptp, PAGE_SIZE);
	free(ptp, M_VTD);
}

static void
vtd_destroy_domain(void *arg)
{
	struct domain *dom;
	
	dom = arg;

	SLIST_REMOVE(&domhead, dom, domain, next);
	vtd_free_ptp(dom->ptp, dom->pt_levels);
	free(dom, M_VTD);
}

struct iommu_ops iommu_ops_intel = {
	vtd_init,
	vtd_cleanup,
	vtd_enable,
	vtd_disable,
	vtd_create_domain,
	vtd_destroy_domain,
	vtd_create_mapping,
	vtd_remove_mapping,
	vtd_add_device,
	vtd_remove_device,
	vtd_invalidate_tlb,
};
