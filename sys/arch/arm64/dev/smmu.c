/* $OpenBSD: smmu.c,v 1.24 2025/08/24 19:49:16 patrick Exp $ */
/*
 * Copyright (c) 2008-2009,2014-2016 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2021 Patrick Wildt <patrick@blueri.se>
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
#include <sys/pool.h>
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>

#include <uvm/uvm_extern.h>
#include <arm64/vmparam.h>
#include <arm64/pmap.h>

#include <dev/pci/pcivar.h>
#include <arm64/dev/smmuvar.h>
#include <arm64/dev/smmureg.h>

struct smmu_map_state {
	struct extent_region	sms_er;
	bus_addr_t		sms_dva;
	bus_size_t		sms_len;
	bus_size_t		sms_loaded;
};

struct smmuvp0 {
	uint64_t l0[VP_IDX0_CNT];
	struct smmuvp1 *vp[VP_IDX0_CNT];
};

struct smmuvp1 {
	uint64_t l1[VP_IDX1_CNT];
	struct smmuvp2 *vp[VP_IDX1_CNT];
};

struct smmuvp2 {
	uint64_t l2[VP_IDX2_CNT];
	struct smmuvp3 *vp[VP_IDX2_CNT];
};

struct smmuvp3 {
	uint64_t l3[VP_IDX3_CNT];
};

CTASSERT(sizeof(struct smmuvp0) == sizeof(struct smmuvp1));
CTASSERT(sizeof(struct smmuvp0) == sizeof(struct smmuvp2));
CTASSERT(sizeof(struct smmuvp0) != sizeof(struct smmuvp3));

uint32_t smmu_gr0_read_4(struct smmu_softc *, bus_size_t);
void smmu_gr0_write_4(struct smmu_softc *, bus_size_t, uint32_t);
uint32_t smmu_gr1_read_4(struct smmu_softc *, bus_size_t);
void smmu_gr1_write_4(struct smmu_softc *, bus_size_t, uint32_t);
uint32_t smmu_cb_read_4(struct smmu_softc *, int, bus_size_t);
void smmu_cb_write_4(struct smmu_softc *, int, bus_size_t, uint32_t);
uint64_t smmu_cb_read_8(struct smmu_softc *, int, bus_size_t);
void smmu_cb_write_8(struct smmu_softc *, int, bus_size_t, uint64_t);

int smmu_v2_domain_create(struct smmu_domain *);
void smmu_v2_tlbi_va(struct smmu_domain *, vaddr_t);
void smmu_v2_tlb_sync_global(struct smmu_softc *);
void smmu_v2_tlb_sync_context(struct smmu_domain *);

struct smmu_domain *smmu_domain_lookup(struct smmu_softc *, uint32_t);
struct smmu_domain *smmu_domain_create(struct smmu_softc *, uint32_t);

void smmu_set_l1(struct smmu_domain *, uint64_t, struct smmuvp1 *);
void smmu_set_l2(struct smmu_domain *, uint64_t, struct smmuvp1 *,
    struct smmuvp2 *);
void smmu_set_l3(struct smmu_domain *, uint64_t, struct smmuvp2 *,
    struct smmuvp3 *);

int smmu_vp_lookup(struct smmu_domain *, vaddr_t, uint64_t **);
int smmu_vp_enter(struct smmu_domain *, vaddr_t, uint64_t **, int);

uint64_t smmu_fill_pte(struct smmu_domain *, vaddr_t, paddr_t,
    vm_prot_t, int, int);
void smmu_pte_update(struct smmu_domain *, uint64_t, uint64_t *);
void smmu_pte_remove(struct smmu_domain *, vaddr_t);

int smmu_enter(struct smmu_domain *, vaddr_t, paddr_t, vm_prot_t, int, int);
void smmu_map(struct smmu_domain *, vaddr_t, paddr_t, vm_prot_t, int, int);
void smmu_unmap(struct smmu_domain *, vaddr_t);
void smmu_remove(struct smmu_domain *, vaddr_t);

int smmu_load_map(struct smmu_domain *, bus_dmamap_t);
void smmu_unload_map(struct smmu_domain *, bus_dmamap_t);

int smmu_dmamap_create(bus_dma_tag_t , bus_size_t, int,
     bus_size_t, bus_size_t, int, bus_dmamap_t *);
void smmu_dmamap_destroy(bus_dma_tag_t , bus_dmamap_t);
int smmu_dmamap_load(bus_dma_tag_t , bus_dmamap_t, void *,
     bus_size_t, struct proc *, int);
int smmu_dmamap_load_mbuf(bus_dma_tag_t , bus_dmamap_t,
     struct mbuf *, int);
int smmu_dmamap_load_uio(bus_dma_tag_t , bus_dmamap_t,
     struct uio *, int);
int smmu_dmamap_load_raw(bus_dma_tag_t , bus_dmamap_t,
     bus_dma_segment_t *, int, bus_size_t, int);
void smmu_dmamap_unload(bus_dma_tag_t , bus_dmamap_t);

uint32_t smmu_v3_read_4(struct smmu_softc *, bus_size_t);
void smmu_v3_write_4(struct smmu_softc *, bus_size_t, uint32_t);
uint64_t smmu_v3_read_8(struct smmu_softc *, bus_size_t);
void smmu_v3_write_8(struct smmu_softc *, bus_size_t, uint64_t);
int smmu_v3_write_ack(struct smmu_softc *, bus_size_t, bus_size_t,
     uint32_t);

int smmu_v3_domain_create(struct smmu_domain *);
void smmu_v3_cfgi_all(struct smmu_softc *);
void smmu_v3_cfgi_cd(struct smmu_domain *);
void smmu_v3_cfgi_ste(struct smmu_domain *);
void smmu_v3_tlbi_all(struct smmu_softc *, uint64_t);
void smmu_v3_tlbi_asid(struct smmu_domain *);
void smmu_v3_tlbi_va(struct smmu_domain *, vaddr_t);
void smmu_v3_tlb_sync_context(struct smmu_domain *);

struct cfdriver smmu_cd = {
	NULL, "smmu", DV_DULL
};

int
smmu_attach(struct smmu_softc *sc)
{
	SIMPLEQ_INIT(&sc->sc_domains);

	pool_init(&sc->sc_vp_pool, sizeof(struct smmuvp0), PAGE_SIZE, IPL_VM, 0,
	    "smmu_vp", NULL);
	pool_setlowat(&sc->sc_vp_pool, 20);
	pool_init(&sc->sc_vp3_pool, sizeof(struct smmuvp3), PAGE_SIZE, IPL_VM, 0,
	    "smmu_vp3", NULL);
	pool_setlowat(&sc->sc_vp3_pool, 20);

	return 0;
}

int
smmu_v2_attach(struct smmu_softc *sc)
{
	uint32_t reg;
	int i;

	if (smmu_attach(sc) != 0)
		return ENXIO;

	reg = smmu_gr0_read_4(sc, SMMU_IDR0);
	if (reg & SMMU_IDR0_S1TS)
		sc->sc_has_s1 = 1;
	/*
	 * Marvell's 8040 does not support 64-bit writes, hence it
	 * is not possible to invalidate stage-2, because the ASID
	 * is part of the upper 32-bits and they'd be ignored.
	 */
	if (sc->sc_is_ap806)
		sc->sc_has_s1 = 0;
	if (reg & SMMU_IDR0_S2TS)
		sc->sc_has_s2 = 1;
	if (!sc->sc_has_s1 && !sc->sc_has_s2)
		return 1;
	if (reg & SMMU_IDR0_EXIDS)
		sc->sc_has_exids = 1;

	sc->sc_num_streams = 1 << SMMU_IDR0_NUMSIDB(reg);
	if (sc->sc_has_exids)
		sc->sc_num_streams = 1 << 16;
	sc->sc_stream_mask = sc->sc_num_streams - 1;
	if (reg & SMMU_IDR0_SMS) {
		sc->sc_num_streams = SMMU_IDR0_NUMSMRG(reg);
		if (sc->sc_num_streams == 0)
			return 1;
		sc->sc_smr = mallocarray(sc->sc_num_streams,
		    sizeof(*sc->sc_smr), M_DEVBUF, M_WAITOK | M_ZERO);
	}

	reg = smmu_gr0_read_4(sc, SMMU_IDR1);
	sc->sc_pagesize = 4 * 1024;
	if (reg & SMMU_IDR1_PAGESIZE_64K)
		sc->sc_pagesize = 64 * 1024;
	sc->sc_numpage = 1 << (SMMU_IDR1_NUMPAGENDXB(reg) + 1);

	/* 0 to NUMS2CB == stage-2, NUMS2CB to NUMCB == stage-1 */
	sc->sc_num_context_banks = SMMU_IDR1_NUMCB(reg);
	sc->sc_num_s2_context_banks = SMMU_IDR1_NUMS2CB(reg);
	if (sc->sc_num_s2_context_banks > sc->sc_num_context_banks)
		return 1;
	sc->sc_cb = mallocarray(sc->sc_num_context_banks,
	    sizeof(*sc->sc_cb), M_DEVBUF, M_WAITOK | M_ZERO);

	reg = smmu_gr0_read_4(sc, SMMU_IDR2);
	if (reg & SMMU_IDR2_VMID16S)
		sc->sc_has_vmid16s = 1;

	switch (SMMU_IDR2_IAS(reg)) {
	case SMMU_IDR2_IAS_32BIT:
		sc->sc_ipa_bits = 32;
		break;
	case SMMU_IDR2_IAS_36BIT:
		sc->sc_ipa_bits = 36;
		break;
	case SMMU_IDR2_IAS_40BIT:
		sc->sc_ipa_bits = 40;
		break;
	case SMMU_IDR2_IAS_42BIT:
		sc->sc_ipa_bits = 42;
		break;
	case SMMU_IDR2_IAS_44BIT:
		sc->sc_ipa_bits = 44;
		break;
	case SMMU_IDR2_IAS_48BIT:
	default:
		sc->sc_ipa_bits = 48;
		break;
	}
	switch (SMMU_IDR2_OAS(reg)) {
	case SMMU_IDR2_OAS_32BIT:
		sc->sc_pa_bits = 32;
		break;
	case SMMU_IDR2_OAS_36BIT:
		sc->sc_pa_bits = 36;
		break;
	case SMMU_IDR2_OAS_40BIT:
		sc->sc_pa_bits = 40;
		break;
	case SMMU_IDR2_OAS_42BIT:
		sc->sc_pa_bits = 42;
		break;
	case SMMU_IDR2_OAS_44BIT:
		sc->sc_pa_bits = 44;
		break;
	case SMMU_IDR2_OAS_48BIT:
	default:
		sc->sc_pa_bits = 48;
		break;
	}
	switch (SMMU_IDR2_UBS(reg)) {
	case SMMU_IDR2_UBS_32BIT:
		sc->sc_va_bits = 32;
		break;
	case SMMU_IDR2_UBS_36BIT:
		sc->sc_va_bits = 36;
		break;
	case SMMU_IDR2_UBS_40BIT:
		sc->sc_va_bits = 40;
		break;
	case SMMU_IDR2_UBS_42BIT:
		sc->sc_va_bits = 42;
		break;
	case SMMU_IDR2_UBS_44BIT:
		sc->sc_va_bits = 44;
		break;
	case SMMU_IDR2_UBS_49BIT:
	default:
		sc->sc_va_bits = 48;
		break;
	}

	printf(": %u CBs (%u S2-only)",
	    sc->sc_num_context_banks, sc->sc_num_s2_context_banks);
	if (sc->sc_is_qcom) {
		/*
		 * In theory we should check if bypass quirk is needed by
		 * modifying S2CR and re-checking if the value is different.
		 * This does not work on the last S2CR, but on the first,
		 * which is in use.  Revisit this once we have other QCOM HW.
		 */
		sc->sc_bypass_quirk = 1;
		printf(", bypass quirk");
		/*
		 * Create special context that is turned off.  This allows us
		 * to map a stream to a context bank where translation is not
		 * happening, and hence bypassed.
		 */
		sc->sc_cb[sc->sc_num_context_banks - 1] =
		    malloc(sizeof(struct smmu_cb), M_DEVBUF, M_WAITOK | M_ZERO);
		smmu_cb_write_4(sc, sc->sc_num_context_banks - 1,
		    SMMU_CB_SCTLR, 0);
		smmu_gr1_write_4(sc, SMMU_CBAR(sc->sc_num_context_banks - 1),
		    SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS);
	}
	printf("\n");

	/* Clear Global Fault Status Register */
	smmu_gr0_write_4(sc, SMMU_SGFSR, smmu_gr0_read_4(sc, SMMU_SGFSR));

	for (i = 0; i < sc->sc_num_streams; i++) {
		/* On QCOM HW we need to keep current streams running. */
		if (sc->sc_is_qcom && sc->sc_smr &&
		    smmu_gr0_read_4(sc, SMMU_SMR(i)) & SMMU_SMR_VALID) {
			reg = smmu_gr0_read_4(sc, SMMU_SMR(i));
			sc->sc_smr[i] = malloc(sizeof(struct smmu_smr),
			    M_DEVBUF, M_WAITOK | M_ZERO);
			sc->sc_smr[i]->ss_id = (reg >> SMMU_SMR_ID_SHIFT) &
			    SMMU_SMR_ID_MASK;
			sc->sc_smr[i]->ss_mask = (reg >> SMMU_SMR_MASK_SHIFT) &
			    SMMU_SMR_MASK_MASK;
			if (sc->sc_bypass_quirk) {
				smmu_gr0_write_4(sc, SMMU_S2CR(i),
				    SMMU_S2CR_TYPE_TRANS |
				    sc->sc_num_context_banks - 1);
			} else {
				smmu_gr0_write_4(sc, SMMU_S2CR(i),
				    SMMU_S2CR_TYPE_BYPASS | 0xff);
			}
			continue;
		}
#if 1
		/* Setup all streams to fault by default */
		smmu_gr0_write_4(sc, SMMU_S2CR(i), SMMU_S2CR_TYPE_FAULT);
#else
		/* For stream indexing, USFCFG bypass isn't enough! */
		smmu_gr0_write_4(sc, SMMU_S2CR(i), SMMU_S2CR_TYPE_BYPASS);
#endif
		/*  Disable all stream map registers */
		if (sc->sc_smr)
			smmu_gr0_write_4(sc, SMMU_SMR(i), 0);
	}

	for (i = 0; i < sc->sc_num_context_banks; i++) {
		/* Disable Context Bank */
		smmu_cb_write_4(sc, i, SMMU_CB_SCTLR, 0);
		/* Clear Context Bank Fault Status Register */
		smmu_cb_write_4(sc, i, SMMU_CB_FSR, SMMU_CB_FSR_MASK);
	}

	/* Invalidate TLB */
	smmu_gr0_write_4(sc, SMMU_TLBIALLH, ~0);
	smmu_gr0_write_4(sc, SMMU_TLBIALLNSNH, ~0);

	if (sc->sc_is_mmu500) {
		reg = smmu_gr0_read_4(sc, SMMU_SACR);
		if (SMMU_IDR7_MAJOR(smmu_gr0_read_4(sc, SMMU_IDR7)) >= 2)
			reg &= ~SMMU_SACR_MMU500_CACHE_LOCK;
		reg |= SMMU_SACR_MMU500_SMTNMB_TLBEN |
		    SMMU_SACR_MMU500_S2CRB_TLBEN;
		smmu_gr0_write_4(sc, SMMU_SACR, reg);
		for (i = 0; i < sc->sc_num_context_banks; i++) {
			reg = smmu_cb_read_4(sc, i, SMMU_CB_ACTLR);
			reg &= ~SMMU_CB_ACTLR_CPRE;
			smmu_cb_write_4(sc, i, SMMU_CB_ACTLR, reg);
		}
	}

	/* Enable SMMU */
	reg = smmu_gr0_read_4(sc, SMMU_SCR0);
	reg &= ~(SMMU_SCR0_CLIENTPD |
	    SMMU_SCR0_FB | SMMU_SCR0_BSU_MASK);
#if 1
	/* Disable bypass for unknown streams */
	reg |= SMMU_SCR0_USFCFG;
#else
	/* Enable bypass for unknown streams */
	reg &= ~SMMU_SCR0_USFCFG;
#endif
	reg |= SMMU_SCR0_GFRE | SMMU_SCR0_GFIE |
	    SMMU_SCR0_GCFGFRE | SMMU_SCR0_GCFGFIE |
	    SMMU_SCR0_VMIDPNE | SMMU_SCR0_PTM;
	if (sc->sc_has_exids)
		reg |= SMMU_SCR0_EXIDENABLE;
	if (sc->sc_has_vmid16s)
		reg |= SMMU_SCR0_VMID16EN;

	smmu_v2_tlb_sync_global(sc);
	smmu_gr0_write_4(sc, SMMU_SCR0, reg);

	sc->sc_domain_create = smmu_v2_domain_create;
	sc->sc_tlbi_va = smmu_v2_tlbi_va;
	sc->sc_tlb_sync_context = smmu_v2_tlb_sync_context;
	return 0;
}

int
smmu_v2_global_irq(void *cookie)
{
	struct smmu_softc *sc = cookie;
	uint32_t reg;

	reg = smmu_gr0_read_4(sc, SMMU_SGFSR);
	if (reg == 0)
		return 0;

	printf("%s: SGFSR 0x%08x SGFSYNR0 0x%08x SGFSYNR1 0x%08x "
	    "SGFSYNR2 0x%08x\n", sc->sc_dev.dv_xname, reg,
	    smmu_gr0_read_4(sc, SMMU_SGFSYNR0),
	    smmu_gr0_read_4(sc, SMMU_SGFSYNR1),
	    smmu_gr0_read_4(sc, SMMU_SGFSYNR2));

	smmu_gr0_write_4(sc, SMMU_SGFSR, reg);

	return 1;
}

int
smmu_v2_context_irq(void *cookie)
{
	struct smmu_cb_irq *cbi = cookie;
	struct smmu_softc *sc = cbi->cbi_sc;
	uint32_t reg;

	reg = smmu_cb_read_4(sc, cbi->cbi_idx, SMMU_CB_FSR);
	if ((reg & SMMU_CB_FSR_MASK) == 0)
		return 0;

	printf("%s: FSR 0x%08x FSYNR0 0x%08x FAR 0x%llx "
	    "CBFRSYNRA 0x%08x\n", sc->sc_dev.dv_xname, reg,
	    smmu_cb_read_4(sc, cbi->cbi_idx, SMMU_CB_FSYNR0),
	    smmu_cb_read_8(sc, cbi->cbi_idx, SMMU_CB_FAR),
	    smmu_gr1_read_4(sc, SMMU_CBFRSYNRA(cbi->cbi_idx)));

	smmu_cb_write_4(sc, cbi->cbi_idx, SMMU_CB_FSR, reg);

	return 1;
}

void
smmu_v2_tlb_sync_global(struct smmu_softc *sc)
{
	int i;

	smmu_gr0_write_4(sc, SMMU_STLBGSYNC, ~0);
	for (i = 1000; i > 0; i--) {
		if ((smmu_gr0_read_4(sc, SMMU_STLBGSTATUS) &
		    SMMU_STLBGSTATUS_GSACTIVE) == 0)
			return;
	}

	printf("%s: global TLB sync timeout\n",
	    sc->sc_dev.dv_xname);
}

void
smmu_v2_tlb_sync_context(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	int i;

	smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_TLBSYNC, ~0);
	for (i = 1000; i > 0; i--) {
		if ((smmu_cb_read_4(sc, dom->sd_cb_idx, SMMU_CB_TLBSTATUS) &
		    SMMU_CB_TLBSTATUS_SACTIVE) == 0)
			return;
	}

	printf("%s: context TLB sync timeout\n",
	    sc->sc_dev.dv_xname);
}

uint32_t
smmu_gr0_read_4(struct smmu_softc *sc, bus_size_t off)
{
	uint32_t base = 0 * sc->sc_pagesize;

	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, base + off);
}

void
smmu_gr0_write_4(struct smmu_softc *sc, bus_size_t off, uint32_t val)
{
	uint32_t base = 0 * sc->sc_pagesize;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, base + off, val);
}

uint32_t
smmu_gr1_read_4(struct smmu_softc *sc, bus_size_t off)
{
	uint32_t base = 1 * sc->sc_pagesize;

	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, base + off);
}

void
smmu_gr1_write_4(struct smmu_softc *sc, bus_size_t off, uint32_t val)
{
	uint32_t base = 1 * sc->sc_pagesize;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, base + off, val);
}

uint32_t
smmu_cb_read_4(struct smmu_softc *sc, int idx, bus_size_t off)
{
	uint32_t base;

	base = sc->sc_numpage * sc->sc_pagesize; /* SMMU_CB_BASE */
	base += idx * sc->sc_pagesize; /* SMMU_CBn_BASE */

	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, base + off);
}

void
smmu_cb_write_4(struct smmu_softc *sc, int idx, bus_size_t off, uint32_t val)
{
	uint32_t base;

	base = sc->sc_numpage * sc->sc_pagesize; /* SMMU_CB_BASE */
	base += idx * sc->sc_pagesize; /* SMMU_CBn_BASE */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, base + off, val);
}

uint64_t
smmu_cb_read_8(struct smmu_softc *sc, int idx, bus_size_t off)
{
	uint64_t reg;
	uint32_t base;

	base = sc->sc_numpage * sc->sc_pagesize; /* SMMU_CB_BASE */
	base += idx * sc->sc_pagesize; /* SMMU_CBn_BASE */

	if (sc->sc_is_ap806) {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, base + off + 4);
		reg <<= 32;
		reg |= bus_space_read_4(sc->sc_iot, sc->sc_ioh, base + off + 0);
		return reg;
	}

	return bus_space_read_8(sc->sc_iot, sc->sc_ioh, base + off);
}

void
smmu_cb_write_8(struct smmu_softc *sc, int idx, bus_size_t off, uint64_t val)
{
	uint32_t base;

	base = sc->sc_numpage * sc->sc_pagesize; /* SMMU_CB_BASE */
	base += idx * sc->sc_pagesize; /* SMMU_CBn_BASE */

	if (sc->sc_is_ap806) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, base + off + 4,
		    val >> 32);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, base + off + 0,
		    val & 0xffffffff);
		return;
	}

	bus_space_write_8(sc->sc_iot, sc->sc_ioh, base + off, val);
}

bus_dma_tag_t
smmu_device_map(void *cookie, uint32_t sid, bus_dma_tag_t dmat)
{
	struct smmu_softc *sc = cookie;
	struct smmu_domain *dom;

	dom = smmu_domain_lookup(sc, sid);
	if (dom == NULL)
		return dmat;

	if (dom->sd_dmat == NULL) {
		dom->sd_dmat = malloc(sizeof(*dom->sd_dmat),
		    M_DEVBUF, M_WAITOK);
		memcpy(dom->sd_dmat, sc->sc_dmat,
		    sizeof(*dom->sd_dmat));
		dom->sd_dmat->_cookie = dom;
		dom->sd_dmat->_dmamap_create = smmu_dmamap_create;
		dom->sd_dmat->_dmamap_destroy = smmu_dmamap_destroy;
		dom->sd_dmat->_dmamap_load = smmu_dmamap_load;
		dom->sd_dmat->_dmamap_load_mbuf = smmu_dmamap_load_mbuf;
		dom->sd_dmat->_dmamap_load_uio = smmu_dmamap_load_uio;
		dom->sd_dmat->_dmamap_load_raw = smmu_dmamap_load_raw;
		dom->sd_dmat->_dmamap_unload = smmu_dmamap_unload;
		dom->sd_dmat->_flags |= BUS_DMA_COHERENT;
	}

	return dom->sd_dmat;
}

struct smmu_domain *
smmu_domain_lookup(struct smmu_softc *sc, uint32_t sid)
{
	struct smmu_domain *dom;

	SIMPLEQ_FOREACH(dom, &sc->sc_domains, sd_list) {
		if (dom->sd_sid == sid)
			return dom;
	}

	return smmu_domain_create(sc, sid);
}

struct smmu_domain *
smmu_domain_create(struct smmu_softc *sc, uint32_t sid)
{
	struct smmu_domain *dom;

	dom = malloc(sizeof(*dom), M_DEVBUF, M_WAITOK | M_ZERO);
	mtx_init(&dom->sd_iova_mtx, IPL_VM);
	mtx_init(&dom->sd_pmap_mtx, IPL_VM);
	dom->sd_sc = sc;
	dom->sd_sid = sid;

	/* Prefer stage 1 if possible! */
	if (sc->sc_has_s1)
		dom->sd_stage = 1;
	else
		dom->sd_stage = 2;

	if (sc->sc_domain_create(dom) != 0) {
		free(dom, M_DEVBUF, sizeof(*dom));
		return NULL;
	}

	/* Reserve first page (to catch NULL access) */
	extent_alloc_region(dom->sd_iovamap, 0, PAGE_SIZE, EX_WAITOK);

	SIMPLEQ_INSERT_TAIL(&sc->sc_domains, dom, sd_list);
	return dom;
}

int
smmu_v2_domain_create(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint32_t iovabits, reg;
	paddr_t pa;
	vaddr_t l0va;
	int i, start, end;

	if (dom->sd_stage == 1) {
		start = sc->sc_num_s2_context_banks;
		end = sc->sc_num_context_banks;
	} else {
		start = 0;
		end = sc->sc_num_context_banks;
	}

	for (i = start; i < end; i++) {
		if (sc->sc_cb[i] != NULL)
			continue;
		sc->sc_cb[i] = malloc(sizeof(struct smmu_cb),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		dom->sd_cb_idx = i;
		break;
	}
	if (i >= end) {
		printf("%s: out of context blocks, I/O device will fail\n",
		    sc->sc_dev.dv_xname);
		return ENXIO;
	}

	/* Stream indexing is easy */
	dom->sd_smr_idx = dom->sd_sid;

	/* Stream mapping is a bit more effort */
	if (sc->sc_smr) {
		for (i = 0; i < sc->sc_num_streams; i++) {
			/* Take over QCOM SMRs */
			if (sc->sc_is_qcom && sc->sc_smr[i] != NULL &&
			    sc->sc_smr[i]->ss_dom == NULL &&
			    !((sc->sc_smr[i]->ss_id ^ dom->sd_sid) &
			    ~sc->sc_smr[i]->ss_mask)) {
				sc->sc_smr[i]->ss_dom = dom;
				dom->sd_smr_idx = i;
				break;
			}
			if (sc->sc_smr[i] != NULL)
				continue;
			sc->sc_smr[i] = malloc(sizeof(struct smmu_smr),
			    M_DEVBUF, M_WAITOK | M_ZERO);
			sc->sc_smr[i]->ss_dom = dom;
			sc->sc_smr[i]->ss_id = dom->sd_sid;
			sc->sc_smr[i]->ss_mask = 0;
			dom->sd_smr_idx = i;
			break;
		}

		if (i >= sc->sc_num_streams) {
			free(sc->sc_cb[dom->sd_cb_idx], M_DEVBUF,
			    sizeof(struct smmu_cb));
			sc->sc_cb[dom->sd_cb_idx] = NULL;
			printf("%s: out of streams, I/O device will fail\n",
			    sc->sc_dev.dv_xname);
			return ENXIO;
		}
	}

	reg = SMMU_CBA2R_VA64;
	if (sc->sc_has_vmid16s)
		reg |= (dom->sd_cb_idx + 1) << SMMU_CBA2R_VMID16_SHIFT;
	smmu_gr1_write_4(sc, SMMU_CBA2R(dom->sd_cb_idx), reg);

	if (dom->sd_stage == 1) {
		reg = SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS |
		    SMMU_CBAR_BPSHCFG_NSH | SMMU_CBAR_MEMATTR_WB;
	} else {
		reg = SMMU_CBAR_TYPE_S2_TRANS;
		if (!sc->sc_has_vmid16s)
			reg |= (dom->sd_cb_idx + 1) << SMMU_CBAR_VMID_SHIFT;
	}
	smmu_gr1_write_4(sc, SMMU_CBAR(dom->sd_cb_idx), reg);

	if (dom->sd_stage == 1) {
		reg = SMMU_CB_TCR2_AS | SMMU_CB_TCR2_SEP_UPSTREAM;
		switch (sc->sc_ipa_bits) {
		case 32:
			reg |= SMMU_CB_TCR2_PASIZE_32BIT;
			break;
		case 36:
			reg |= SMMU_CB_TCR2_PASIZE_36BIT;
			break;
		case 40:
			reg |= SMMU_CB_TCR2_PASIZE_40BIT;
			break;
		case 42:
			reg |= SMMU_CB_TCR2_PASIZE_42BIT;
			break;
		case 44:
			reg |= SMMU_CB_TCR2_PASIZE_44BIT;
			break;
		case 48:
			reg |= SMMU_CB_TCR2_PASIZE_48BIT;
			break;
		}
		smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_TCR2, reg);
	}

	if (dom->sd_stage == 1)
		iovabits = sc->sc_va_bits;
	else
		iovabits = sc->sc_ipa_bits;
	/*
	 * Marvell's 8040 does not support 64-bit writes, hence we
	 * can only address 44-bits of VA space for TLB invalidation.
	 */
	if (sc->sc_is_ap806)
		iovabits = min(44, iovabits);
	if (iovabits >= 40)
		dom->sd_4level = 1;

	reg = SMMU_CB_TCR_TG0_4KB | SMMU_CB_TCR_T0SZ(64 - iovabits);
	if (dom->sd_stage == 1) {
		reg |= SMMU_CB_TCR_EPD1;
	} else {
		if (dom->sd_4level)
			reg |= SMMU_CB_TCR_S2_SL0_4KB_L0;
		else
			reg |= SMMU_CB_TCR_S2_SL0_4KB_L1;
		switch (sc->sc_pa_bits) {
		case 32:
			reg |= SMMU_CB_TCR_S2_PASIZE_32BIT;
			break;
		case 36:
			reg |= SMMU_CB_TCR_S2_PASIZE_36BIT;
			break;
		case 40:
			reg |= SMMU_CB_TCR_S2_PASIZE_40BIT;
			break;
		case 42:
			reg |= SMMU_CB_TCR_S2_PASIZE_42BIT;
			break;
		case 44:
			reg |= SMMU_CB_TCR_S2_PASIZE_44BIT;
			break;
		case 48:
			reg |= SMMU_CB_TCR_S2_PASIZE_48BIT;
			break;
		}
	}
	if (sc->sc_coherent)
		reg |= SMMU_CB_TCR_IRGN0_WBWA | SMMU_CB_TCR_ORGN0_WBWA |
		    SMMU_CB_TCR_SH0_ISH;
	else
		reg |= SMMU_CB_TCR_IRGN0_NC | SMMU_CB_TCR_ORGN0_NC |
		    SMMU_CB_TCR_SH0_OSH;
	smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_TCR, reg);

	if (dom->sd_4level) {
		while (dom->sd_vp.l0 == NULL) {
			dom->sd_vp.l0 = pool_get(&sc->sc_vp_pool,
			    PR_WAITOK | PR_ZERO);
		}
		l0va = (vaddr_t)dom->sd_vp.l0->l0; /* top level is l0 */
	} else {
		while (dom->sd_vp.l1 == NULL) {
			dom->sd_vp.l1 = pool_get(&sc->sc_vp_pool,
			    PR_WAITOK | PR_ZERO);
		}
		l0va = (vaddr_t)dom->sd_vp.l1->l1; /* top level is l1 */
	}
	pmap_extract(pmap_kernel(), l0va, &pa);

	if (dom->sd_stage == 1) {
		smmu_cb_write_8(sc, dom->sd_cb_idx, SMMU_CB_TTBR0,
		    (uint64_t)dom->sd_cb_idx << SMMU_CB_TTBR_ASID_SHIFT | pa);
		smmu_cb_write_8(sc, dom->sd_cb_idx, SMMU_CB_TTBR1,
		    (uint64_t)dom->sd_cb_idx << SMMU_CB_TTBR_ASID_SHIFT);
	} else
		smmu_cb_write_8(sc, dom->sd_cb_idx, SMMU_CB_TTBR0, pa);

	if (dom->sd_stage == 1) {
		smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_MAIR0,
		    SMMU_CB_MAIR_MAIR_ATTR(SMMU_CB_MAIR_DEVICE_nGnRnE, 0) |
		    SMMU_CB_MAIR_MAIR_ATTR(SMMU_CB_MAIR_DEVICE_nGnRE, 1) |
		    SMMU_CB_MAIR_MAIR_ATTR(SMMU_CB_MAIR_DEVICE_NC, 2) |
		    SMMU_CB_MAIR_MAIR_ATTR(SMMU_CB_MAIR_DEVICE_WB, 3));
		smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_MAIR1,
		    SMMU_CB_MAIR_MAIR_ATTR(SMMU_CB_MAIR_DEVICE_WT, 0));
	}

	reg = SMMU_CB_SCTLR_M | SMMU_CB_SCTLR_TRE | SMMU_CB_SCTLR_AFE |
	    SMMU_CB_SCTLR_CFRE | SMMU_CB_SCTLR_CFIE;
	if (dom->sd_stage == 1)
		reg |= SMMU_CB_SCTLR_ASIDPNE;
	smmu_cb_write_4(sc, dom->sd_cb_idx, SMMU_CB_SCTLR, reg);

	/* Point stream to context block */
	reg = SMMU_S2CR_TYPE_TRANS | dom->sd_cb_idx;
	if (sc->sc_has_exids && sc->sc_smr)
		reg |= SMMU_S2CR_EXIDVALID;
	smmu_gr0_write_4(sc, SMMU_S2CR(dom->sd_smr_idx), reg);

	/* Map stream idx to S2CR idx */
	if (sc->sc_smr) {
		reg = sc->sc_smr[dom->sd_smr_idx]->ss_id << SMMU_SMR_ID_SHIFT |
		    sc->sc_smr[dom->sd_smr_idx]->ss_mask << SMMU_SMR_MASK_SHIFT;
		if (!sc->sc_has_exids)
			reg |= SMMU_SMR_VALID;
		smmu_gr0_write_4(sc, SMMU_SMR(dom->sd_smr_idx), reg);
	}

	snprintf(dom->sd_exname, sizeof(dom->sd_exname), "%s:%x",
	    sc->sc_dev.dv_xname, dom->sd_sid);
	dom->sd_iovamap = extent_create(dom->sd_exname, 0,
	    (1LL << iovabits) - 1, M_DEVBUF, NULL, 0, EX_WAITOK |
	    EX_NOCOALESCE);

	return 0;
}

void
smmu_reserve_region(void *cookie, uint32_t sid, bus_addr_t addr,
    bus_size_t size)
{
	struct smmu_softc *sc = cookie;
	struct smmu_domain *dom;

	dom = smmu_domain_lookup(sc, sid);
	if (dom == NULL)
		return;

	extent_alloc_region(dom->sd_iovamap, addr, size,
	    EX_WAITOK | EX_CONFLICTOK);
}

/* basically pmap follows */

/* virtual to physical helpers */
static inline int
VP_IDX0(vaddr_t va)
{
	return (va >> VP_IDX0_POS) & VP_IDX0_MASK;
}

static inline int
VP_IDX1(vaddr_t va)
{
	return (va >> VP_IDX1_POS) & VP_IDX1_MASK;
}

static inline int
VP_IDX2(vaddr_t va)
{
	return (va >> VP_IDX2_POS) & VP_IDX2_MASK;
}

static inline int
VP_IDX3(vaddr_t va)
{
	return (va >> VP_IDX3_POS) & VP_IDX3_MASK;
}

static inline uint64_t
VP_Lx(paddr_t pa)
{
	/*
	 * This function takes the pa address given and manipulates it
	 * into the form that should be inserted into the VM table.
	 */
	return pa | Lx_TYPE_PT;
}

void
smmu_set_l1(struct smmu_domain *dom, uint64_t va, struct smmuvp1 *l1_va)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint64_t pg_entry;
	paddr_t l1_pa;
	int idx0;

	if (pmap_extract(pmap_kernel(), (vaddr_t)l1_va, &l1_pa) == 0)
		panic("%s: unable to find vp pa mapping %p", __func__, l1_va);

	if (l1_pa & (Lx_TABLE_ALIGN-1))
		panic("%s: misaligned L2 table", __func__);

	pg_entry = VP_Lx(l1_pa);

	idx0 = VP_IDX0(va);
	dom->sd_vp.l0->vp[idx0] = l1_va;
	dom->sd_vp.l0->l0[idx0] = pg_entry;
	membar_producer(); /* XXX bus dma sync? */
	if (!sc->sc_coherent)
		cpu_dcache_wb_range((vaddr_t)&dom->sd_vp.l0->l0[idx0],
		    sizeof(dom->sd_vp.l0->l0[idx0]));
}

void
smmu_set_l2(struct smmu_domain *dom, uint64_t va, struct smmuvp1 *vp1,
    struct smmuvp2 *l2_va)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint64_t pg_entry;
	paddr_t l2_pa;
	int idx1;

	if (pmap_extract(pmap_kernel(), (vaddr_t)l2_va, &l2_pa) == 0)
		panic("%s: unable to find vp pa mapping %p", __func__, l2_va);

	if (l2_pa & (Lx_TABLE_ALIGN-1))
		panic("%s: misaligned L2 table", __func__);

	pg_entry = VP_Lx(l2_pa);

	idx1 = VP_IDX1(va);
	vp1->vp[idx1] = l2_va;
	vp1->l1[idx1] = pg_entry;
	membar_producer(); /* XXX bus dma sync? */
	if (!sc->sc_coherent)
		cpu_dcache_wb_range((vaddr_t)&vp1->l1[idx1],
		    sizeof(vp1->l1[idx1]));
}

void
smmu_set_l3(struct smmu_domain *dom, uint64_t va, struct smmuvp2 *vp2,
    struct smmuvp3 *l3_va)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint64_t pg_entry;
	paddr_t l3_pa;
	int idx2;

	if (pmap_extract(pmap_kernel(), (vaddr_t)l3_va, &l3_pa) == 0)
		panic("%s: unable to find vp pa mapping %p", __func__, l3_va);

	if (l3_pa & (Lx_TABLE_ALIGN-1))
		panic("%s: misaligned L2 table", __func__);

	pg_entry = VP_Lx(l3_pa);

	idx2 = VP_IDX2(va);
	vp2->vp[idx2] = l3_va;
	vp2->l2[idx2] = pg_entry;
	membar_producer(); /* XXX bus dma sync? */
	if (!sc->sc_coherent)
		cpu_dcache_wb_range((vaddr_t)&vp2->l2[idx2],
		    sizeof(vp2->l2[idx2]));
}

int
smmu_vp_lookup(struct smmu_domain *dom, vaddr_t va, uint64_t **pl3entry)
{
	struct smmuvp1 *vp1;
	struct smmuvp2 *vp2;
	struct smmuvp3 *vp3;

	if (dom->sd_4level) {
		if (dom->sd_vp.l0 == NULL) {
			return ENXIO;
		}
		vp1 = dom->sd_vp.l0->vp[VP_IDX0(va)];
	} else {
		vp1 = dom->sd_vp.l1;
	}
	if (vp1 == NULL) {
		return ENXIO;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		return ENXIO;
	}

	vp3 = vp2->vp[VP_IDX2(va)];
	if (vp3 == NULL) {
		return ENXIO;
	}

	if (pl3entry != NULL)
		*pl3entry = &(vp3->l3[VP_IDX3(va)]);

	return 0;
}

int
smmu_vp_enter(struct smmu_domain *dom, vaddr_t va, uint64_t **pl3entry,
    int flags)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmuvp1 *vp1;
	struct smmuvp2 *vp2;
	struct smmuvp3 *vp3;

	if (dom->sd_4level) {
		vp1 = dom->sd_vp.l0->vp[VP_IDX0(va)];
		if (vp1 == NULL) {
			mtx_enter(&dom->sd_pmap_mtx);
			vp1 = dom->sd_vp.l0->vp[VP_IDX0(va)];
			if (vp1 == NULL) {
				vp1 = pool_get(&sc->sc_vp_pool,
				    PR_NOWAIT | PR_ZERO);
				if (vp1 == NULL) {
					mtx_leave(&dom->sd_pmap_mtx);
					return ENOMEM;
				}
				smmu_set_l1(dom, va, vp1);
			}
			mtx_leave(&dom->sd_pmap_mtx);
		}
	} else {
		vp1 = dom->sd_vp.l1;
	}

	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		mtx_enter(&dom->sd_pmap_mtx);
		vp2 = vp1->vp[VP_IDX1(va)];
		if (vp2 == NULL) {
			vp2 = pool_get(&sc->sc_vp_pool, PR_NOWAIT | PR_ZERO);
			if (vp2 == NULL) {
				mtx_leave(&dom->sd_pmap_mtx);
				return ENOMEM;
			}
			smmu_set_l2(dom, va, vp1, vp2);
		}
		mtx_leave(&dom->sd_pmap_mtx);
	}

	vp3 = vp2->vp[VP_IDX2(va)];
	if (vp3 == NULL) {
		mtx_enter(&dom->sd_pmap_mtx);
		vp3 = vp2->vp[VP_IDX2(va)];
		if (vp3 == NULL) {
			vp3 = pool_get(&sc->sc_vp3_pool, PR_NOWAIT | PR_ZERO);
			if (vp3 == NULL) {
				mtx_leave(&dom->sd_pmap_mtx);
				return ENOMEM;
			}
			smmu_set_l3(dom, va, vp2, vp3);
		}
		mtx_leave(&dom->sd_pmap_mtx);
	}

	if (pl3entry != NULL)
		*pl3entry = &(vp3->l3[VP_IDX3(va)]);

	return 0;
}

uint64_t
smmu_fill_pte(struct smmu_domain *dom, vaddr_t va, paddr_t pa,
    vm_prot_t prot, int flags, int cache)
{
	uint64_t pted;

	pted = pa & PTE_RPGN;

	switch (cache) {
	case PMAP_CACHE_WB:
		break;
	case PMAP_CACHE_WT:
		break;
	case PMAP_CACHE_CI:
		break;
	case PMAP_CACHE_DEV_NGNRNE:
		break;
	case PMAP_CACHE_DEV_NGNRE:
		break;
	default:
		panic("%s: invalid cache mode", __func__);
	}

	pted |= cache;
	pted |= flags & (PROT_READ|PROT_WRITE|PROT_EXEC);
	return pted;
}

void
smmu_pte_update(struct smmu_domain *dom, uint64_t pted, uint64_t *pl3)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint64_t pte, access_bits;
	uint64_t attr = 0;

	/* see mair in locore.S */
	switch (pted & PMAP_CACHE_BITS) {
	case PMAP_CACHE_WB:
		/* inner and outer writeback */
		if (dom->sd_stage == 1)
			attr |= ATTR_IDX(PTE_ATTR_WB);
		else
			attr |= ATTR_IDX(PTE_MEMATTR_WB);
		attr |= ATTR_SH(SH_INNER);
		break;
	case PMAP_CACHE_WT:
		/* inner and outer writethrough */
		if (dom->sd_stage == 1)
			attr |= ATTR_IDX(PTE_ATTR_WT);
		else
			attr |= ATTR_IDX(PTE_MEMATTR_WT);
		attr |= ATTR_SH(SH_INNER);
		break;
	case PMAP_CACHE_CI:
		if (dom->sd_stage == 1)
			attr |= ATTR_IDX(PTE_ATTR_CI);
		else
			attr |= ATTR_IDX(PTE_MEMATTR_CI);
		attr |= ATTR_SH(SH_INNER);
		break;
	case PMAP_CACHE_DEV_NGNRNE:
		if (dom->sd_stage == 1)
			attr |= ATTR_IDX(PTE_ATTR_DEV_NGNRNE);
		else
			attr |= ATTR_IDX(PTE_MEMATTR_DEV_NGNRNE);
		attr |= ATTR_SH(SH_INNER);
		break;
	case PMAP_CACHE_DEV_NGNRE:
		if (dom->sd_stage == 1)
			attr |= ATTR_IDX(PTE_ATTR_DEV_NGNRE);
		else
			attr |= ATTR_IDX(PTE_MEMATTR_DEV_NGNRE);
		attr |= ATTR_SH(SH_INNER);
		break;
	default:
		panic("%s: invalid cache mode", __func__);
	}

	access_bits = ATTR_PXN | ATTR_AF;
	if (dom->sd_stage == 1) {
		attr |= ATTR_nG;
		access_bits |= ATTR_AP(1);
		if ((pted & PROT_READ) &&
		    !(pted & PROT_WRITE))
			access_bits |= ATTR_AP(2);
	} else {
		if (pted & PROT_READ)
			access_bits |= ATTR_AP(1);
		if (pted & PROT_WRITE)
			access_bits |= ATTR_AP(2);
	}

	pte = (pted & PTE_RPGN) | attr | access_bits | L3_P;
	*pl3 = pte;
	membar_producer(); /* XXX bus dma sync? */
	if (!sc->sc_coherent)
		cpu_dcache_wb_range((vaddr_t)pl3, sizeof(*pl3));
}

void
smmu_pte_remove(struct smmu_domain *dom, vaddr_t va)
{
	/* put entry into table */
	/* need to deal with ref/change here */
	struct smmu_softc *sc = dom->sd_sc;
	struct smmuvp1 *vp1;
	struct smmuvp2 *vp2;
	struct smmuvp3 *vp3;

	if (dom->sd_4level)
		vp1 = dom->sd_vp.l0->vp[VP_IDX0(va)];
	else
		vp1 = dom->sd_vp.l1;
	if (vp1 == NULL) {
		panic("%s: missing the l1 for va %lx domain %p", __func__,
		    va, dom);
	}
	vp2 = vp1->vp[VP_IDX1(va)];
	if (vp2 == NULL) {
		panic("%s: missing the l2 for va %lx domain %p", __func__,
		    va, dom);
	}
	vp3 = vp2->vp[VP_IDX2(va)];
	if (vp3 == NULL) {
		panic("%s: missing the l3 for va %lx domain %p", __func__,
		    va, dom);
	}
	vp3->l3[VP_IDX3(va)] = 0;
	membar_producer(); /* XXX bus dma sync? */
	if (!sc->sc_coherent)
		cpu_dcache_wb_range((vaddr_t)&vp3->l3[VP_IDX3(va)],
		    sizeof(vp3->l3[VP_IDX3(va)]));
}

int
smmu_enter(struct smmu_domain *dom, vaddr_t va, paddr_t pa, vm_prot_t prot,
    int flags, int cache)
{
	uint64_t *pl3;

	if (smmu_vp_lookup(dom, va, &pl3) != 0) {
		if (smmu_vp_enter(dom, va, &pl3, flags))
			return ENOMEM;
	}

	if (flags & (PROT_READ|PROT_WRITE|PROT_EXEC))
		smmu_map(dom, va, pa, prot, flags, cache);

	return 0;
}

void
smmu_map(struct smmu_domain *dom, vaddr_t va, paddr_t pa, vm_prot_t prot,
    int flags, int cache)
{
	uint64_t *pl3;
	uint64_t pted;
	int ret;

	/* IOVA must already be allocated */
	ret = smmu_vp_lookup(dom, va, &pl3);
	KASSERT(ret == 0);

	/* Update PTED information for physical address */
	pted = smmu_fill_pte(dom, va, pa, prot, flags, cache);

	/* Insert updated information */
	smmu_pte_update(dom, pted, pl3);
}

void
smmu_unmap(struct smmu_domain *dom, vaddr_t va)
{
	struct smmu_softc *sc = dom->sd_sc;
	int ret;

	/* IOVA must already be allocated */
	ret = smmu_vp_lookup(dom, va, NULL);
	KASSERT(ret == 0);

	/* Remove mapping from pagetable */
	smmu_pte_remove(dom, va);

	sc->sc_tlbi_va(dom, va);
}

void
smmu_v2_tlbi_va(struct smmu_domain *dom, vaddr_t va)
{
	struct smmu_softc *sc = dom->sd_sc;

	/* Invalidate IOTLB */
	if (dom->sd_stage == 1)
		smmu_cb_write_8(sc, dom->sd_cb_idx, SMMU_CB_TLBIVAL,
		    (uint64_t)dom->sd_cb_idx << 48 | va >> PAGE_SHIFT);
	else
		smmu_cb_write_8(sc, dom->sd_cb_idx, SMMU_CB_TLBIIPAS2L,
		    va >> PAGE_SHIFT);
}

void
smmu_remove(struct smmu_domain *dom, vaddr_t va)
{
	/* TODO: garbage collect page tables? */
}

int
smmu_load_map(struct smmu_domain *dom, bus_dmamap_t map)
{
	struct smmu_map_state *sms = map->_dm_cookie;
	u_long dva, maplen;
	int seg;

	maplen = 0;
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		paddr_t pa = map->dm_segs[seg]._ds_paddr;
		psize_t off = pa - trunc_page(pa);
		maplen += round_page(map->dm_segs[seg].ds_len + off);
	}
	KASSERT(maplen <= sms->sms_len);

	dva = sms->sms_dva;
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		paddr_t pa = map->dm_segs[seg]._ds_paddr;
		psize_t off = pa - trunc_page(pa);
		u_long len = round_page(map->dm_segs[seg].ds_len + off);

		map->dm_segs[seg].ds_addr = dva + off;

		pa = trunc_page(pa);
		while (len > 0) {
			smmu_map(dom, dva, pa,
			    PROT_READ | PROT_WRITE,
			    PROT_READ | PROT_WRITE, PMAP_CACHE_WB);

			dva += PAGE_SIZE;
			pa += PAGE_SIZE;
			len -= PAGE_SIZE;
			sms->sms_loaded += PAGE_SIZE;
		}
	}

	return 0;
}

void
smmu_unload_map(struct smmu_domain *dom, bus_dmamap_t map)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_map_state *sms = map->_dm_cookie;
	u_long len, dva;

	if (sms->sms_loaded == 0)
		return;

	dva = sms->sms_dva;
	len = sms->sms_loaded;

	while (len > 0) {
		smmu_unmap(dom, dva);

		dva += PAGE_SIZE;
		len -= PAGE_SIZE;
	}

	sms->sms_loaded = 0;

	sc->sc_tlb_sync_context(dom);
}

int
smmu_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamap)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_map_state *sms;
	bus_dmamap_t map;
	u_long dva, len;
	int error;

	error = sc->sc_dmat->_dmamap_create(sc->sc_dmat, size,
	    nsegments, maxsegsz, boundary, flags, &map);
	if (error)
		return error;

	sms = malloc(sizeof(*sms), M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	     (M_NOWAIT|M_ZERO) : (M_WAITOK|M_ZERO));
	if (sms == NULL) {
		sc->sc_dmat->_dmamap_destroy(sc->sc_dmat, map);
		return ENOMEM;
	}

	/* Approximation of maximum pages needed. */
	len = round_page(size) + nsegments * PAGE_SIZE;

	/* Allocate IOVA, and a guard page at the end. */
	mtx_enter(&dom->sd_iova_mtx);
	error = extent_alloc_with_descr(dom->sd_iovamap, len + PAGE_SIZE,
	    PAGE_SIZE, 0, 0, EX_NOWAIT, &sms->sms_er, &dva);
	mtx_leave(&dom->sd_iova_mtx);
	if (error) {
		sc->sc_dmat->_dmamap_destroy(sc->sc_dmat, map);
		free(sms, M_DEVBUF, sizeof(*sms));
		return error;
	}

	sms->sms_dva = dva;
	sms->sms_len = len;

	while (len > 0) {
		error = smmu_enter(dom, dva, dva, PROT_READ | PROT_WRITE,
		    PROT_NONE, PMAP_CACHE_WB);
		KASSERT(error == 0); /* FIXME: rollback smmu_enter() */
		dva += PAGE_SIZE;
		len -= PAGE_SIZE;
	}

	map->_dm_cookie = sms;
	*dmamap = map;
	return 0;
}

void
smmu_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_map_state *sms = map->_dm_cookie;
	u_long dva, len;
	int error;

	if (sms->sms_loaded)
		smmu_dmamap_unload(t, map);

	dva = sms->sms_dva;
	len = sms->sms_len;

	while (len > 0) {
		smmu_remove(dom, dva);
		dva += PAGE_SIZE;
		len -= PAGE_SIZE;
	}

	mtx_enter(&dom->sd_iova_mtx);
	error = extent_free(dom->sd_iovamap, sms->sms_dva,
	    sms->sms_len + PAGE_SIZE, EX_NOWAIT);
	mtx_leave(&dom->sd_iova_mtx);
	KASSERT(error == 0);

	free(sms, M_DEVBUF, sizeof(*sms));
	sc->sc_dmat->_dmamap_destroy(sc->sc_dmat, map);
}

int
smmu_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load(sc->sc_dmat, map,
	    buf, buflen, p, flags);
	if (error)
		return error;

	error = smmu_load_map(dom, map);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
smmu_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load_mbuf(sc->sc_dmat, map,
	    m0, flags);
	if (error)
		return error;

	error = smmu_load_map(dom, map);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
smmu_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load_uio(sc->sc_dmat, map,
	    uio, flags);
	if (error)
		return error;

	error = smmu_load_map(dom, map);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
smmu_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load_raw(sc->sc_dmat, map,
	    segs, nsegs, size, flags);
	if (error)
		return error;

	error = smmu_load_map(dom, map);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

void
smmu_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct smmu_domain *dom = t->_cookie;
	struct smmu_softc *sc = dom->sd_sc;

	smmu_unload_map(dom, map);
	sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);
}

#define SMMU_DMA_MAP(_sdm)	((_sdm)->sdm_map)
#define SMMU_DMA_LEN(_sdm)	((_sdm)->sdm_size)
#define SMMU_DMA_DVA(_sdm)	((_sdm)->sdm_map->dm_segs[0].ds_addr)
#define SMMU_DMA_KVA(_sdm)	((void *)(_sdm)->sdm_kva)

struct smmu_dmamem *
smmu_dmamem_alloc(bus_dma_tag_t dmat, bus_size_t size, bus_size_t align)
{
	struct smmu_dmamem *sdm;
	int nsegs;

	sdm = malloc(sizeof(*sdm), M_DEVBUF, M_WAITOK | M_ZERO);
	sdm->sdm_size = size;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &sdm->sdm_map) != 0)
		goto sdmfree;

	if (bus_dmamem_alloc(dmat, size, align, 0, &sdm->sdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, &sdm->sdm_seg, nsegs, size,
	    &sdm->sdm_kva, BUS_DMA_WAITOK) != 0)
		goto free;

	if (bus_dmamap_load(dmat, sdm->sdm_map, sdm->sdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(sdm->sdm_kva, size);
	bus_dmamap_sync(dmat, sdm->sdm_map, 0, size,
	    BUS_DMASYNC_PREWRITE);

	return sdm;

unmap:
	bus_dmamem_unmap(dmat, sdm->sdm_kva, size);
free:
	bus_dmamem_free(dmat, &sdm->sdm_seg, 1);
destroy:
	bus_dmamap_destroy(dmat, sdm->sdm_map);
sdmfree:
	free(sdm, M_DEVBUF, sizeof(*sdm));

	return NULL;
}

void
smmu_dmamem_free(bus_dma_tag_t dmat, struct smmu_dmamem *sdm)
{
	bus_dmamem_unmap(dmat, sdm->sdm_kva, sdm->sdm_size);
	bus_dmamem_free(dmat, &sdm->sdm_seg, 1);
	bus_dmamap_destroy(dmat, sdm->sdm_map);
	free(sdm, M_DEVBUF, sizeof(*sdm));
}

/* SMMU v3 */
int
smmu_v3_attach(struct smmu_softc *sc)
{
	uint32_t reg;
	int i;

	if (smmu_attach(sc) != 0)
		return ENXIO;

	reg = smmu_v3_read_4(sc, SMMU_V3_IDR0);
	if (!(reg & SMMU_V3_TTF_AA64)) {
		printf(": no support for AA64\n");
		return ENXIO;
	}
	if (reg & SMMU_V3_IDR0_S1P)
		sc->sc_has_s1 = 1;
	if (reg & SMMU_V3_IDR0_S2P)
		sc->sc_has_s2 = 1;
	if (reg & SMMU_V3_IDR0_ASID16)
		sc->v3.sc_has_asid16s = 1;
	if (reg & SMMU_V3_IDR0_PRI)
		sc->v3.sc_has_pri = 1;
	if (reg & SMMU_V3_IDR0_VMID16)
		sc->sc_has_vmid16s = 1;
	if (reg & SMMU_V3_IDR0_CD2L)
		sc->v3.sc_2lvl_cdtab = 1;
	if (SMMU_V3_IDR0_ST_LEVEL(reg) == SMMU_V3_IDR0_ST_LEVEL_2)
		sc->v3.sc_2lvl_strtab = 1;

	reg = smmu_v3_read_4(sc, SMMU_V3_IDR1);
	sc->v3.sc_cmdq.sq_size_log2 = SMMU_V3_IDR1_CMDQS(reg);
	sc->v3.sc_eventq.sq_size_log2 = SMMU_V3_IDR1_EVENTQS(reg);
	sc->v3.sc_priq.sq_size_log2 = SMMU_V3_IDR1_PRIQS(reg);
	sc->v3.sc_sidsize = SMMU_V3_IDR1_SIDSIZE(reg);
	if (sc->v3.sc_sidsize <= 8)
		sc->v3.sc_2lvl_strtab = 0;

	reg = smmu_v3_read_4(sc, SMMU_V3_IDR5);
	switch (SMMU_V3_IDR5_OAS(reg)) {
	case SMMU_V3_IDR5_OAS_32BIT:
		sc->sc_pa_bits = 32;
		break;
	case SMMU_V3_IDR5_OAS_36BIT:
		sc->sc_pa_bits = 36;
		break;
	case SMMU_V3_IDR5_OAS_40BIT:
		sc->sc_pa_bits = 40;
		break;
	case SMMU_V3_IDR5_OAS_42BIT:
		sc->sc_pa_bits = 42;
		break;
	case SMMU_V3_IDR5_OAS_44BIT:
		sc->sc_pa_bits = 44;
		break;
	case SMMU_V3_IDR5_OAS_48BIT:
		sc->sc_pa_bits = 48;
		break;
	case SMMU_V3_IDR5_OAS_52BIT:
	default:
		sc->sc_pa_bits = 52;
		break;
	}
	sc->sc_va_bits = 48;
#if notyet
	if (reg & SMMU_V3_IDR5_VAX)
		sc->sc_va_bits = 52;
#endif
	/* Unless there's no AA64, then it's 40. */
	sc->sc_ipa_bits = sc->sc_pa_bits;

	/* If IDR3.STT=0, maximum is 39. */
	reg = smmu_v3_read_4(sc, SMMU_V3_IDR3);
	if ((reg & SMMU_V3_IDR3_STT) == 0) {
		sc->sc_va_bits = min(sc->sc_va_bits, 39);
		sc->sc_ipa_bits = min(sc->sc_ipa_bits, 39);
	}

	sc->v3.sc_cmdq.sq_sdm = smmu_dmamem_alloc(sc->sc_dmat,
	     (1ULL << sc->v3.sc_cmdq.sq_size_log2) * 2 * sizeof(uint64_t),
	     (1ULL << sc->v3.sc_cmdq.sq_size_log2) * 2 * sizeof(uint64_t));
	if (sc->v3.sc_cmdq.sq_sdm == NULL) {
		printf(": can't allocate command queue\n");
		goto out;
	}
	sc->v3.sc_eventq.sq_sdm = smmu_dmamem_alloc(sc->sc_dmat,
	     (1ULL << sc->v3.sc_eventq.sq_size_log2) * 4 * sizeof(uint64_t),
	     (1ULL << sc->v3.sc_eventq.sq_size_log2) * 4 * sizeof(uint64_t));
	if (sc->v3.sc_eventq.sq_sdm == NULL) {
		printf(": can't allocate event queue\n");
		goto free_cmdq;
	}
	if (sc->v3.sc_has_pri) {
		sc->v3.sc_priq.sq_sdm = smmu_dmamem_alloc(sc->sc_dmat,
		     (1ULL << sc->v3.sc_priq.sq_size_log2) * 2 * sizeof(uint64_t),
		     (1ULL << sc->v3.sc_priq.sq_size_log2) * 2 * sizeof(uint64_t));
		if (sc->v3.sc_priq.sq_sdm == NULL) {
			printf(": can't allocate pri queue\n");
			goto free_evtq;
		}
	}

	/* Abort transaction if already enabled. */
	if (smmu_v3_read_4(sc, SMMU_V3_CR0) & SMMU_V3_CR0_SMMUEN) {
		reg = smmu_v3_read_4(sc, SMMU_V3_GBPA);
		reg |= SMMU_V3_GBPA_ABORT;
		smmu_v3_write_4(sc, SMMU_V3_GBPA, reg | SMMU_V3_GBPA_UPDATE);
		for (i = 100000; i > 0; i--) {
			if (!(smmu_v3_read_4(sc, SMMU_V3_GBPA) & SMMU_V3_GBPA_UPDATE))
				break;
		}
		if (i == 0) {
			printf(": failed waiting for update\n");
			goto free_priq;
		}
	}

	/* Disable SMMU */
	smmu_v3_write_ack(sc, SMMU_V3_CR0, SMMU_V3_CR0ACK, 0);

	smmu_v3_write_4(sc, SMMU_V3_CR1,
	    SMMU_V3_CR1_TABLE_SH(SMMU_V3_CR1_SHARE_ISH) |
	    SMMU_V3_CR1_TABLE_OC(SMMU_V3_CR1_CACHE_WB) |
	    SMMU_V3_CR1_TABLE_IC(SMMU_V3_CR1_CACHE_WB) |
	    SMMU_V3_CR1_QUEUE_SH(SMMU_V3_CR1_SHARE_ISH) |
	    SMMU_V3_CR1_QUEUE_OC(SMMU_V3_CR1_CACHE_WB) |
	    SMMU_V3_CR1_QUEUE_IC(SMMU_V3_CR1_CACHE_WB));
	smmu_v3_write_4(sc, SMMU_V3_CR2, SMMU_V3_CR2_PTM |
	    SMMU_V3_CR2_RECINVSID | SMMU_V3_CR2_E2H);

	if (sc->v3.sc_2lvl_strtab) {
		sc->v3.sc_strtab_l1 = smmu_dmamem_alloc(sc->sc_dmat,
		    ((1ULL << sc->v3.sc_sidsize) / 256) * sizeof(uint64_t),
		    ((1ULL << sc->v3.sc_sidsize) / 256) * sizeof(uint64_t));
		if (sc->v3.sc_strtab_l1 == NULL) {
			printf(": can't allocate strtab\n");
			goto free_priq;
		}
		smmu_v3_write_8(sc, SMMU_V3_STRTAB_BASE,
		    SMMU_V3_STRTAB_BASE_RA |
		    SMMU_DMA_DVA(sc->v3.sc_strtab_l1));
		smmu_v3_write_4(sc, SMMU_V3_STRTAB_BASE_CFG,
		    SMMU_V3_STRTAB_BASE_CFG_FMT_L2 |
		    SMMU_V3_STRTAB_BASE_CFG_SPLIT(8) |
		    SMMU_V3_STRTAB_BASE_CFG_LOG2SIZE(sc->v3.sc_sidsize));
		sc->v3.sc_strtab_l2 = mallocarray(
		    ((1ULL << sc->v3.sc_sidsize) / 256), sizeof(void *),
		    M_DEVBUF, M_WAITOK | M_ZERO);
	} else {
		sc->v3.sc_strtab_l1 = smmu_dmamem_alloc(sc->sc_dmat,
		    (1ULL << sc->v3.sc_sidsize) * 8 * sizeof(uint64_t),
		    (1ULL << sc->v3.sc_sidsize) * 8 * sizeof(uint64_t));
		if (sc->v3.sc_strtab_l1 == NULL) {
			printf(": can't allocate strtab\n");
			goto free_priq;
		}
		smmu_v3_write_8(sc, SMMU_V3_STRTAB_BASE,
		    SMMU_V3_STRTAB_BASE_RA |
		    SMMU_DMA_DVA(sc->v3.sc_strtab_l1));
		smmu_v3_write_4(sc, SMMU_V3_STRTAB_BASE_CFG,
		    SMMU_V3_STRTAB_BASE_CFG_FMT_L1 |
		    SMMU_V3_STRTAB_BASE_CFG_LOG2SIZE(sc->v3.sc_sidsize));
	}

	smmu_v3_write_8(sc, SMMU_V3_CMDQ_BASE,
	    SMMU_V3_CMDQ_BASE_RA |
	    SMMU_DMA_DVA(sc->v3.sc_cmdq.sq_sdm) |
	    SMMU_V3_CMDQ_BASE_LOG2SIZE(sc->v3.sc_cmdq.sq_size_log2));
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, 0);
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_CONS, 0);
	smmu_v3_write_ack(sc, SMMU_V3_CR0, SMMU_V3_CR0ACK,
	    SMMU_V3_CR0_CMDQEN);

	smmu_v3_cfgi_all(sc);
	smmu_v3_tlbi_all(sc, SMMU_V3_CMD_TLBI_EL2_ALL);
	smmu_v3_tlbi_all(sc, SMMU_V3_CMD_TLBI_NSNH_ALL);

	smmu_v3_write_8(sc, SMMU_V3_EVENTQ_BASE,
	    SMMU_V3_EVENTQ_BASE_WA |
	    SMMU_DMA_DVA(sc->v3.sc_eventq.sq_sdm) |
	    SMMU_V3_EVENTQ_BASE_LOG2SIZE(sc->v3.sc_eventq.sq_size_log2));
	smmu_v3_write_4(sc, SMMU_V3_EVENTQ_PROD, 0);
	smmu_v3_write_4(sc, SMMU_V3_EVENTQ_CONS, 0);
	smmu_v3_write_ack(sc, SMMU_V3_CR0, SMMU_V3_CR0ACK,
	    smmu_v3_read_4(sc, SMMU_V3_CR0) | SMMU_V3_CR0_EVENTQEN);

	if (sc->v3.sc_has_pri) {
		smmu_v3_write_8(sc, SMMU_V3_PRIQ_BASE,
		    SMMU_V3_PRIQ_BASE_WA |
		    SMMU_DMA_DVA(sc->v3.sc_priq.sq_sdm) |
		    SMMU_V3_PRIQ_BASE_LOG2SIZE(sc->v3.sc_priq.sq_size_log2));
		smmu_v3_write_4(sc, SMMU_V3_PRIQ_PROD, 0);
		smmu_v3_write_4(sc, SMMU_V3_PRIQ_CONS, 0);
		smmu_v3_write_ack(sc, SMMU_V3_CR0, SMMU_V3_CR0ACK,
		    smmu_v3_read_4(sc, SMMU_V3_CR0) | SMMU_V3_CR0_PRIQEN);
	}

	/* Disable MSIs, use wired IRQs, re-enable IRQs. */
	smmu_v3_write_ack(sc, SMMU_V3_IRQ_CTRL, SMMU_V3_IRQ_CTRLACK, 0);
	smmu_v3_write_8(sc, SMMU_V3_GERROR_IRQ_CFG0, 0);
	smmu_v3_write_8(sc, SMMU_V3_EVENTQ_IRQ_CFG0, 0);
	if (sc->v3.sc_has_pri)
		smmu_v3_write_8(sc, SMMU_V3_PRIQ_IRQ_CFG0, 0);
	smmu_v3_write_ack(sc, SMMU_V3_IRQ_CTRL, SMMU_V3_IRQ_CTRLACK,
	    SMMU_V3_IRQ_CTRL_GERROR | SMMU_V3_IRQ_CTRL_EVENTQ |
	    (sc->v3.sc_has_pri ? SMMU_V3_IRQ_CTRL_PRIQ : 0));

	smmu_v3_write_ack(sc, SMMU_V3_CR0, SMMU_V3_CR0ACK,
	    smmu_v3_read_4(sc, SMMU_V3_CR0) | SMMU_V3_CR0_SMMUEN);

	printf("\n");

	sc->sc_domain_create = smmu_v3_domain_create;
	sc->sc_tlbi_va = smmu_v3_tlbi_va;
	sc->sc_tlb_sync_context = smmu_v3_tlb_sync_context;
	return 0;

//free_strtab:
//	smmu_dmamem_free(sc->sc_dmat, sc->v3.sc_strtab_l1);
free_priq:
	if (sc->v3.sc_has_pri)
		smmu_dmamem_free(sc->sc_dmat, sc->v3.sc_priq.sq_sdm);
free_evtq:
	smmu_dmamem_free(sc->sc_dmat, sc->v3.sc_eventq.sq_sdm);
free_cmdq:
	smmu_dmamem_free(sc->sc_dmat, sc->v3.sc_cmdq.sq_sdm);
out:
	return ENXIO;
}

int
smmu_v3_event_irq(void *cookie)
{
	struct smmu_softc *sc = cookie;
	struct smmu_v3_queue *sq = &sc->v3.sc_eventq;
	uint64_t *event;
	uint32_t cons, prod;
	bus_size_t off;
	int handled = 0;

	for (;;) {
		prod = smmu_v3_read_4(sc, SMMU_V3_EVENTQ_PROD);
		if (SMMU_V3_Q_OVF(sq->sq_prod) != SMMU_V3_Q_OVF(prod))
			printf("%s: event queue overflow\n",
			    sc->sc_dev.dv_xname);
		sq->sq_prod = prod;

		/* Stop if empty. */
		if (SMMU_V3_Q_IDX(sq, sq->sq_cons) ==
		    SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
		    SMMU_V3_Q_WRP(sq, sq->sq_cons) ==
		    SMMU_V3_Q_WRP(sq, sq->sq_prod))
			break;

		/* Print event information. */
		off = SMMU_V3_Q_IDX(sq, sq->sq_cons) * 4 * sizeof(uint64_t);
		bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
		    4 * sizeof(uint64_t), BUS_DMASYNC_POSTWRITE);
		event = SMMU_DMA_KVA(sq->sq_sdm) + off;
		printf("%s: event 0x%llx 0x%llx 0x%llx 0x%llx\n",
		    sc->sc_dev.dv_xname, event[0], event[1], event[2], event[3]);

		/* Let HW know we consumed */
		cons = (SMMU_V3_Q_WRP(sq, sq->sq_cons) |
		    SMMU_V3_Q_IDX(sq, sq->sq_cons)) + 1;
		sq->sq_cons = SMMU_V3_Q_OVF(sq->sq_cons) |
		    SMMU_V3_Q_WRP(sq, cons) |
		    SMMU_V3_Q_IDX(sq, cons);
		membar_sync();
		smmu_v3_write_4(sc, SMMU_V3_EVENTQ_CONS, sq->sq_cons);

		handled = 1;
	}

	/* Sync overflow flag */
	if (SMMU_V3_Q_OVF(sq->sq_prod) != SMMU_V3_Q_OVF(sq->sq_cons)) {
		sq->sq_cons = SMMU_V3_Q_OVF(sq->sq_prod) |
		    SMMU_V3_Q_WRP(sq, sq->sq_cons) |
		    SMMU_V3_Q_IDX(sq, sq->sq_cons);
		membar_sync();
		smmu_v3_write_4(sc, SMMU_V3_EVENTQ_CONS, sq->sq_cons);
	}

	return handled;
}

int
smmu_v3_gerr_irq(void *cookie)
{
	struct smmu_softc *sc = cookie;
	uint32_t gerror, gerrorn;

	gerror = smmu_v3_read_4(sc, SMMU_V3_GERROR);
	gerrorn = smmu_v3_read_4(sc, SMMU_V3_GERRORN);
	smmu_v3_write_4(sc, SMMU_V3_GERRORN, gerror);

	gerror = (gerror ^ gerrorn) & SMMU_V3_GERROR_MASK;

	if (gerror & SMMU_V3_GERROR_CMDQ_ERR) {
		uint32_t cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
		printf("%s: cmdq error 0x%x on cmd idx %u\n",
		    sc->sc_dev.dv_xname, SMMU_V3_CMDQ_CONS_ERR(cons),
		    SMMU_V3_Q_IDX(&sc->v3.sc_cmdq, cons));
	} else {
		printf("%s: gerror 0x%x\n", sc->sc_dev.dv_xname, gerror);
	}

	return 1;
}

int
smmu_v3_priq_irq(void *cookie)
{
	struct smmu_softc *sc = cookie;
	struct smmu_v3_queue *sq = &sc->v3.sc_priq;
	uint64_t *pri;
	uint32_t cons, prod;
	int handled = 0;

	for (;;) {
		prod = smmu_v3_read_4(sc, SMMU_V3_PRIQ_PROD);
		if (SMMU_V3_Q_OVF(sq->sq_prod) != SMMU_V3_Q_OVF(prod))
			printf("%s: event queue overflow\n",
			    sc->sc_dev.dv_xname);
		sq->sq_prod = prod;

		/* Stop if empty. */
		if (SMMU_V3_Q_IDX(sq, sq->sq_cons) ==
		    SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
		    SMMU_V3_Q_WRP(sq, sq->sq_cons) ==
		    SMMU_V3_Q_WRP(sq, sq->sq_prod))
			break;

		/* Print pri information. */
		bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm),
		    SMMU_V3_Q_IDX(sq, sq->sq_cons) * 2 * sizeof(uint64_t),
		    2 * sizeof(uint64_t),
		    BUS_DMASYNC_POSTWRITE);
		pri = SMMU_DMA_KVA(sq->sq_sdm) +
		    SMMU_V3_Q_IDX(sq, sq->sq_cons) * 2 * sizeof(uint64_t);
		printf("%s: pri 0x%llx 0x%llx\n", sc->sc_dev.dv_xname,
		    pri[0], pri[1]);

		/* Increase consumed */
		cons = (SMMU_V3_Q_WRP(sq, sq->sq_cons) |
		    SMMU_V3_Q_IDX(sq, sq->sq_cons)) + 1;
		sq->sq_cons = SMMU_V3_Q_OVF(sq->sq_cons) |
		    SMMU_V3_Q_WRP(sq, cons) |
		    SMMU_V3_Q_IDX(sq, cons);
		membar_sync();
		smmu_v3_write_4(sc, SMMU_V3_PRIQ_CONS, sq->sq_cons);

		handled = 1;
	}

	/* Sync overflow flag */
	if (SMMU_V3_Q_OVF(sq->sq_prod) != SMMU_V3_Q_OVF(sq->sq_cons)) {
		sq->sq_cons = SMMU_V3_Q_OVF(sq->sq_prod) |
		    SMMU_V3_Q_WRP(sq, sq->sq_cons) |
		    SMMU_V3_Q_IDX(sq, sq->sq_cons);
		membar_sync();
		smmu_v3_write_4(sc, SMMU_V3_PRIQ_CONS, sq->sq_cons);
	}

	return handled;
}

uint32_t
smmu_v3_read_4(struct smmu_softc *sc, bus_size_t off)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, off);
}

void
smmu_v3_write_4(struct smmu_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, val);
}

uint64_t
smmu_v3_read_8(struct smmu_softc *sc, bus_size_t off)
{
	return bus_space_read_8(sc->sc_iot, sc->sc_ioh, off);
}

void
smmu_v3_write_8(struct smmu_softc *sc, bus_size_t off, uint64_t val)
{
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, off, val);
}

int
smmu_v3_write_ack(struct smmu_softc *sc, bus_size_t off, bus_size_t ack_off,
    uint32_t val)
{
	int i;

	smmu_v3_write_4(sc, off, val);

	for (i = 100000; i > 0; i--) {
		if (smmu_v3_read_4(sc, ack_off) == val)
			break;
	}
	if (i == 0) {
		printf("%s: failed waiting for ack\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	return 0;
}

int
smmu_v3_domain_create(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	uint64_t *cd, *ste;
	paddr_t pa;
	vaddr_t l0va;
	uint32_t iovabits;

	if (dom->sd_sid >= (1 << sc->v3.sc_sidsize))
		return EINVAL;

	if (dom->sd_stage != 1)
		return EINVAL;

	if (sc->v3.sc_has_asid16s) {
		if (sc->v3.sc_next_asid == (1 << 16) - 1)
			return EINVAL;
	} else {
		if (sc->v3.sc_next_asid == (1 << 8) - 1)
			return EINVAL;
	}
	dom->v3.sd_asid = sc->v3.sc_next_asid++;

	dom->v3.sd_cd = smmu_dmamem_alloc(sc->sc_dmat, 64, 64);
	if (dom->v3.sd_cd == NULL) {
		printf(": can't allocate context descriptor\n");
		return ENOMEM;
	}

	if (dom->sd_stage == 1)
		iovabits = sc->sc_va_bits;
	else
		iovabits = sc->sc_ipa_bits;
	if (iovabits >= 40)
		dom->sd_4level = 1;

	if (dom->sd_4level) {
		while (dom->sd_vp.l0 == NULL) {
			dom->sd_vp.l0 = pool_get(&sc->sc_vp_pool,
			    PR_WAITOK | PR_ZERO);
		}
		l0va = (vaddr_t)dom->sd_vp.l0->l0; /* top level is l0 */
	} else {
		while (dom->sd_vp.l1 == NULL) {
			dom->sd_vp.l1 = pool_get(&sc->sc_vp_pool,
			    PR_WAITOK | PR_ZERO);
		}
		l0va = (vaddr_t)dom->sd_vp.l1->l1; /* top level is l1 */
	}
	pmap_extract(pmap_kernel(), l0va, &pa);

	cd = SMMU_DMA_KVA(dom->v3.sd_cd);
	cd[0] =
	    SMMU_V3_CD_0_TCR_T0SZ(64 - iovabits) |
	    SMMU_V3_CD_0_TCR_TG0_4KB |
	    SMMU_V3_CD_0_TCR_IRGN0_WBWA |
	    SMMU_V3_CD_0_TCR_ORGN0_WBWA |
	    SMMU_V3_CD_0_TCR_SH0_ISH |
	    SMMU_V3_CD_0_TCR_EPD1 |
	    SMMU_V3_CD_0_V |
	    SMMU_V3_CD_0_TCR_IPS_48BIT |
	    SMMU_V3_CD_0_AA64 |
	    SMMU_V3_CD_0_R |
	    SMMU_V3_CD_0_A |
	    SMMU_V3_CD_0_ASET |
	    SMMU_V3_CD_0_ASID(dom->v3.sd_asid);
	cd[1] = pa;
	cd[3] =
	    SMMU_V3_CD_3_MAIR_ATTR(SMMU_V3_CD_3_MAIR_DEVICE_nGnRnE, 0) |
	    SMMU_V3_CD_3_MAIR_ATTR(SMMU_V3_CD_3_MAIR_DEVICE_nGnRE, 1) |
	    SMMU_V3_CD_3_MAIR_ATTR(SMMU_V3_CD_3_MAIR_DEVICE_NC, 2) |
	    SMMU_V3_CD_3_MAIR_ATTR(SMMU_V3_CD_3_MAIR_DEVICE_WB, 3) |
	    SMMU_V3_CD_3_MAIR_ATTR(SMMU_V3_CD_3_MAIR_DEVICE_WT, 4);
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(dom->v3.sd_cd), 0,
	    SMMU_DMA_LEN(dom->v3.sd_cd), BUS_DMASYNC_PREWRITE);
	smmu_v3_cfgi_cd(dom);

	snprintf(dom->sd_exname, sizeof(dom->sd_exname), "%s:%x",
	    sc->sc_dev.dv_xname, dom->sd_sid);
	dom->sd_iovamap = extent_create(dom->sd_exname, 0,
	    (1LL << iovabits) - 1, M_DEVBUF, NULL, 0, EX_WAITOK |
	    EX_NOCOALESCE);

	if (sc->v3.sc_2lvl_strtab) {
		if (sc->v3.sc_strtab_l2[dom->sd_sid / 256] == NULL) {
			sc->v3.sc_strtab_l2[dom->sd_sid / 256] =
			    smmu_dmamem_alloc(sc->sc_dmat,
			    256 * 8 * sizeof(uint64_t),
			    64 * 1024);
			if (sc->v3.sc_strtab_l2[dom->sd_sid / 256] == NULL) {
				printf("%s: can't allocate strtab\n",
				    sc->sc_dev.dv_xname);
				return ENXIO;
			}
			ste = SMMU_DMA_KVA(sc->v3.sc_strtab_l1) +
			    (dom->sd_sid / 256) * sizeof(uint64_t);
			*ste = SMMU_DMA_DVA(sc->v3.sc_strtab_l2[dom->sd_sid / 256]) |
			    (8 /* split */ + 1);
			bus_dmamap_sync(sc->sc_dmat,
			    SMMU_DMA_MAP(sc->v3.sc_strtab_l1),
			    (dom->sd_sid / 256) * sizeof(uint64_t),
			    sizeof(uint64_t), BUS_DMASYNC_PREWRITE);
		}
		ste = SMMU_DMA_KVA(sc->v3.sc_strtab_l2[dom->sd_sid / 256]) +
		    (dom->sd_sid % 256) * 8 * sizeof(uint64_t);
	} else {
		ste = SMMU_DMA_KVA(sc->v3.sc_strtab_l1) +
		    dom->sd_sid * 8 * sizeof(uint64_t);
	}

	ste[1] =  SMMU_V3_STE_1_S1CIR_WBRA | SMMU_V3_STE_1_S1COR_WBRA |
	    SMMU_V3_STE_1_S1CSH_ISH | SMMU_V3_STE_1_EATS_TRANS |
//	    SMMU_V3_STE_1_STRW_NSEL1 | SMMU_V3_STE_1_S1DSS_SSID0;
	    SMMU_V3_STE_1_STRW_EL2 | SMMU_V3_STE_1_S1DSS_SSID0;
	ste[0] = SMMU_V3_STE_0_V | SMMU_V3_STE_0_CFG_S1_TRANS |
	    SMMU_V3_STE_0_S1FMT_LINEAR | SMMU_DMA_DVA(dom->v3.sd_cd);
	if (sc->v3.sc_2lvl_strtab) {
		bus_dmamap_sync(sc->sc_dmat,
		    SMMU_DMA_MAP(sc->v3.sc_strtab_l2[dom->sd_sid / 256]),
		    (dom->sd_sid % 256) * 8 * sizeof(uint64_t),
		    8 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->sc_dmat,
		    SMMU_DMA_MAP(sc->v3.sc_strtab_l1),
		    dom->sd_sid * 8 * sizeof(uint64_t),
		    8 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);
	}
	smmu_v3_cfgi_ste(dom);

	smmu_v3_tlbi_asid(dom);
	return 0;
}

int
smmu_v3_sync(struct smmu_softc *sc)
{
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;
	int i;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
	cmd[0] = SMMU_V3_CMD_SYNC | SMMU_V3_CMD_SYNC_0_CS_SEV;
	cmd[1] = 0;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	/*
	 * TODO: In a better world, where CPUs could concurrently put in commands,
	 * TODO: we should be able to wait until it has *passed* the prod we set.
	 */
	for (i = 100000; i > 0; i--) {
		/* Wait until HW processing caught up with us. */
		sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
		if ((SMMU_V3_Q_WRP(sq, sq->sq_cons) ==
		     SMMU_V3_Q_WRP(sq, sq->sq_prod)) &&
		    (SMMU_V3_Q_IDX(sq, sq->sq_cons) ==
		     SMMU_V3_Q_IDX(sq, sq->sq_prod)))
			break;
	}
	if (i == 0) {
		printf("%s: timeout waiting for SYNC\n", sc->sc_dev.dv_xname);
		sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
		return ETIMEDOUT;
	}

	return 0;
}

void
smmu_v3_cfgi_all(struct smmu_softc *sc)
{
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
	cmd[0] = SMMU_V3_CMD_CFGI_STE_RANGE;
	cmd[1] = SMMU_V3_CMD_CFGI_1_RANGE(31);
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	smmu_v3_sync(sc);
}

void
smmu_v3_cfgi_cd(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
	cmd[0] = SMMU_V3_CMD_CFGI_CD |
	    SMMU_V3_CMD_CFGI_0_SID(dom->sd_sid);
	cmd[1] = SMMU_V3_CMD_CFGI_1_LEAF;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	smmu_v3_sync(sc);
}

void
smmu_v3_cfgi_ste(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
	cmd[0] = SMMU_V3_CMD_CFGI_STE |
	    SMMU_V3_CMD_CFGI_0_SID(dom->sd_sid);
	cmd[1] = SMMU_V3_CMD_CFGI_1_LEAF;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	smmu_v3_sync(sc);
}

void
smmu_v3_tlbi_all(struct smmu_softc *sc, uint64_t op)
{
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
	cmd[0] = op;
	cmd[1] = 0;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	smmu_v3_sync(sc);
}

void
smmu_v3_tlbi_asid(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
//	cmd[0] = SMMU_V3_CMD_TLBI_NH_ASID |
//	    SMMU_V3_CMD_TLBI_0_ASID(dom->v3.sd_asid);
	cmd[0] = SMMU_V3_CMD_TLBI_EL2_ASID |
	    SMMU_V3_CMD_TLBI_0_ASID(dom->v3.sd_asid);
	cmd[1] = 0;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);

	smmu_v3_sync(sc);
}

void
smmu_v3_tlbi_va(struct smmu_domain *dom, vaddr_t va)
{
	struct smmu_softc *sc = dom->sd_sc;
	struct smmu_v3_queue *sq = &sc->v3.sc_cmdq;
	volatile uint64_t *cmd;
	uint32_t prod;
	bus_size_t off;

	/* TODO: Handle this more properly. */
	sq->sq_cons = smmu_v3_read_4(sc, SMMU_V3_CMDQ_CONS);
	if (SMMU_V3_Q_IDX(sq, sq->sq_cons) == SMMU_V3_Q_IDX(sq, sq->sq_prod) &&
	    SMMU_V3_Q_WRP(sq, sq->sq_cons) != SMMU_V3_Q_WRP(sq, sq->sq_prod)) {
		printf("%s: CMDQ ran out of space\n", sc->sc_dev.dv_xname);
		return;
	}

	off = SMMU_V3_Q_IDX(sq, sq->sq_prod) * 2 * sizeof(uint64_t);
	cmd = SMMU_DMA_KVA(sq->sq_sdm) + off;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_POSTREAD);
//	cmd[0] = SMMU_V3_CMD_TLBI_NH_VA | SMMU_V3_CMD_TLBI_0_VMID(0) |
//	    SMMU_V3_CMD_TLBI_0_ASID(dom->v3.sd_asid);
//	cmd[1] = va | SMMU_V3_CMD_TLBI_1_LEAF;
	cmd[0] = SMMU_V3_CMD_TLBI_EL2_VA | SMMU_V3_CMD_TLBI_0_VMID(0) |
	    SMMU_V3_CMD_TLBI_0_ASID(dom->v3.sd_asid);
	cmd[1] = va | SMMU_V3_CMD_TLBI_1_LEAF;
	bus_dmamap_sync(sc->sc_dmat, SMMU_DMA_MAP(sq->sq_sdm), off,
	    2 * sizeof(uint64_t), BUS_DMASYNC_PREWRITE);

	/* Let HW know we produced */
	prod = (SMMU_V3_Q_WRP(sq, sq->sq_prod) |
	    SMMU_V3_Q_IDX(sq, sq->sq_prod)) + 1;
	sq->sq_prod = SMMU_V3_Q_OVF(sq->sq_prod) |
	    SMMU_V3_Q_WRP(sq, prod) | SMMU_V3_Q_IDX(sq, prod);
	membar_sync();
	smmu_v3_write_4(sc, SMMU_V3_CMDQ_PROD, sq->sq_prod);
}

void
smmu_v3_tlb_sync_context(struct smmu_domain *dom)
{
	struct smmu_softc *sc = dom->sd_sc;

	smmu_v3_sync(sc);
}
