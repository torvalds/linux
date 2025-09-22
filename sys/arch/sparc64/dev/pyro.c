/*	$OpenBSD: pyro.c,v 1.39 2025/06/28 11:34:21 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2003 Henric Jungheim
 * Copyright (c) 2007 Mark Kettenis
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/msivar.h>
#include <sparc64/dev/pyrovar.h>

#ifdef DEBUG
#define PDB_PROM        0x01
#define PDB_BUSMAP      0x02
#define PDB_INTR        0x04
#define PDB_CONF        0x08
int pyro_debug = ~0;
#define DPRINTF(l, s)   do { if (pyro_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

#define FIRE_INTR_MAP(_n)		0x01000 + ((_n) * 8)
#define FIRE_INTR_CLR(_n)		0x01400 + ((_n) * 8)

#define FIRE_EQ_BASE_ADDR		0x10000
#define FIRE_EQ_CNTRL_SET(_n)		0x11000 + ((_n) * 8)
#define  FIRE_EQ_CTRL_SET_EN		0x0000100000000000UL
#define FIRE_EQ_CNTRL_CLEAR(_n)		0x11200 + ((_n) * 8)
#define FIRE_EQ_STATE(_n)		0x11400 + ((_n) * 8)
#define FIRE_EQ_TAIL(_n)		0x11600 + ((_n) * 8)
#define FIRE_EQ_HEAD(_n)		0x11800 + ((_n) * 8)
#define FIRE_MSI_MAP(_n)		0x20000 + ((_n) * 8)
#define  FIRE_MSI_MAP_V			0x8000000000000000UL
#define  FIRE_MSI_MAP_EQWR_N		0x4000000000000000UL
#define  FIRE_MSI_MAP_EQNUM		0x000000000000003fUL
#define FIRE_MSI_CLEAR(_n)		0x28000 + ((_n) * 8)
#define  FIRE_MSI_CLEAR_EQWR_N		0x4000000000000000UL
#define FIRE_INTRMONDO_DATA0		0x2c000
#define FIRE_INTRMONDO_DATA1		0x2c008
#define FIRE_MSI32_ADDR			0x34000
#define FIRE_MSI64_ADDR			0x34008

#define FIRE_RESET_GEN			0x7010

#define FIRE_RESET_GEN_XIR		0x0000000000000002UL

#define FIRE_INTRMAP_INT_CNTRL_NUM_MASK	0x000003c0
#define FIRE_INTRMAP_INT_CNTRL_NUM0	0x00000040
#define FIRE_INTRMAP_INT_CNTRL_NUM1	0x00000080
#define FIRE_INTRMAP_INT_CNTRL_NUM2	0x00000100
#define FIRE_INTRMAP_INT_CNTRL_NUM3	0x00000200
#define FIRE_INTRMAP_T_JPID_SHIFT	26
#define FIRE_INTRMAP_T_JPID_MASK	0x7c000000

#define OBERON_INTRMAP_T_DESTID_SHIFT	21
#define OBERON_INTRMAP_T_DESTID_MASK	0x7fe00000

extern struct sparc_pci_chipset _sparc_pci_chipset;

int pyro_match(struct device *, void *, void *);
void pyro_attach(struct device *, struct device *, void *);
void pyro_init(struct pyro_softc *, int);
void pyro_init_iommu(struct pyro_softc *, struct pyro_pbm *);
void pyro_init_msi(struct pyro_softc *, struct pyro_pbm *);
int pyro_print(void *, const char *);

pci_chipset_tag_t pyro_alloc_chipset(struct pyro_pbm *, int,
    pci_chipset_tag_t);
bus_space_tag_t pyro_alloc_mem_tag(struct pyro_pbm *);
bus_space_tag_t pyro_alloc_io_tag(struct pyro_pbm *);
bus_space_tag_t pyro_alloc_config_tag(struct pyro_pbm *);
bus_space_tag_t pyro_alloc_bus_tag(struct pyro_pbm *, const char *,
    int, int, int);
bus_dma_tag_t pyro_alloc_dma_tag(struct pyro_pbm *);

int pyro_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t pyro_conf_read(pci_chipset_tag_t, pcitag_t, int);
void pyro_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);

int pyro_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int pyro_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t,
    bus_size_t, int, bus_space_handle_t *);
paddr_t pyro_bus_mmap(bus_space_tag_t, bus_space_tag_t, bus_addr_t, off_t,
    int, int);
void *pyro_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int, int,
    int (*)(void *), void *, const char *);
void *pyro_intr_establish_cpu(bus_space_tag_t, bus_space_tag_t, int, int, int,
    struct cpu_info *, int (*)(void *), void *, const char *);
void pyro_msi_ack(struct intrhand *);

int pyro_msi_eq_intr(void *);

int pyro_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
    bus_size_t, bus_size_t, int, bus_dmamap_t *);

void pyro_iommu_enable(struct iommu_state *);

const struct iommu_hw iommu_hw_fire = {
	.ihw_enable	= pyro_iommu_enable,

	.ihw_dvma_pa	= 0x000007ffffffffffUL,

	.ihw_bypass	= 0xfffc000000000000UL,
	.ihw_bypass_nc	= 0x0000080000000000UL,
	.ihw_bypass_ro	= 0,
};

const struct iommu_hw iommu_hw_oberon = {
	.ihw_enable	= pyro_iommu_enable,

	.ihw_dvma_pa	= 0x00007fffffffffffUL,

	.ihw_bypass	= 0x7ffc000000000000UL,
	.ihw_bypass_nc	= 0x0000800000000000UL,
	.ihw_bypass_ro	= 0x8000000000000000UL,

	.ihw_flags	= IOMMU_HW_FLUSH_CACHE,
};

#ifdef DDB
void pyro_xir(void *, int);
#endif

int
pyro_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	char *str;

	if (strcmp(ma->ma_name, "pci") != 0)
		return (0);

	str = getpropstring(ma->ma_node, "compatible");
	if (strcmp(str, "pciex108e,80f0") == 0 ||
	    strcmp(str, "pciex108e,80f8") == 0)
		return (1);

	return (0);
}

void
pyro_attach(struct device *parent, struct device *self, void *aux)
{
	struct pyro_softc *sc = (struct pyro_softc *)self;
	struct mainbus_attach_args *ma = aux;
	char *str;
	int busa;

	sc->sc_node = ma->ma_node;
	sc->sc_dmat = ma->ma_dmatag;
	sc->sc_bust = ma->ma_bustag;
	sc->sc_csr = ma->ma_reg[0].ur_paddr;
	sc->sc_xbc = ma->ma_reg[1].ur_paddr;
	sc->sc_ign = INTIGN(ma->ma_upaid << INTMAP_IGN_SHIFT);

	if ((ma->ma_reg[0].ur_paddr & 0x00700000) == 0x00600000)
		busa = 1;
	else
		busa = 0;

	if (bus_space_map(sc->sc_bust, sc->sc_csr,
	    ma->ma_reg[0].ur_len, 0, &sc->sc_csrh)) {
		printf(": failed to map csr registers\n");
		return;
	}

	if (bus_space_map(sc->sc_bust, sc->sc_xbc,
	    ma->ma_reg[1].ur_len, 0, &sc->sc_xbch)) {
		printf(": failed to map xbc registers\n");
		return;
	}

	str = getpropstring(ma->ma_node, "compatible");
	if (strcmp(str, "pciex108e,80f8") == 0)
		sc->sc_oberon = 1;

	pyro_init(sc, busa);
}

void
pyro_init(struct pyro_softc *sc, int busa)
{
	struct pyro_pbm *pbm;
	struct pcibus_attach_args pba;
	int *busranges = NULL, nranges;

	pbm = malloc(sizeof(*pbm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pbm == NULL)
		panic("pyro: can't alloc pyro pbm");

	pbm->pp_sc = sc;
	pbm->pp_bus_a = busa;

	if (getprop(sc->sc_node, "ranges", sizeof(struct pyro_range),
	    &pbm->pp_nrange, (void **)&pbm->pp_range))
		panic("pyro: can't get ranges");

	if (getprop(sc->sc_node, "bus-range", sizeof(int), &nranges,
	    (void **)&busranges))
		panic("pyro: can't get bus-range");

	printf(": \"%s\", rev %d, ign %x, bus %c %d to %d\n",
	    sc->sc_oberon ? "Oberon" : "Fire",
	    getpropint(sc->sc_node, "module-revision#", 0), sc->sc_ign,
	    busa ? 'A' : 'B', busranges[0], busranges[1]);

	printf("%s: ", sc->sc_dv.dv_xname);
	pyro_init_iommu(sc, pbm);

	pbm->pp_memt = pyro_alloc_mem_tag(pbm);
	pbm->pp_iot = pyro_alloc_io_tag(pbm);
	pbm->pp_cfgt = pyro_alloc_config_tag(pbm);
	pbm->pp_dmat = pyro_alloc_dma_tag(pbm);

	pyro_init_msi(sc, pbm);

	if (bus_space_map(pbm->pp_cfgt, 0, 0x10000000, 0, &pbm->pp_cfgh))
		panic("pyro: can't map config space");

	pbm->pp_pc = pyro_alloc_chipset(pbm, sc->sc_node, &_sparc_pci_chipset);

	pbm->pp_pc->bustag = pbm->pp_cfgt;
	pbm->pp_pc->bushandle = pbm->pp_cfgh;

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = busranges[0];
	pba.pba_pc = pbm->pp_pc;
	pba.pba_flags = pbm->pp_flags;
	pba.pba_dmat = pbm->pp_dmat;
	pba.pba_memt = pbm->pp_memt;
	pba.pba_iot = pbm->pp_iot;
	pba.pba_pc->conf_size = pyro_conf_size;
	pba.pba_pc->conf_read = pyro_conf_read;
	pba.pba_pc->conf_write = pyro_conf_write;
	pba.pba_pc->intr_map = pyro_intr_map;

	free(busranges, M_DEVBUF, 0);

#ifdef DDB
	db_register_xir(pyro_xir, sc);
#endif

	config_found(&sc->sc_dv, &pba, pyro_print);
}

void
pyro_init_iommu(struct pyro_softc *sc, struct pyro_pbm *pbm)
{
	struct iommu_state *is = &pbm->pp_is;
	int tsbsize = 7;
	u_int32_t iobase = -1;
	char *name;
	const struct iommu_hw *ihw = &iommu_hw_fire;

	is->is_bustag = sc->sc_bust;

	if (bus_space_subregion(is->is_bustag, sc->sc_csrh,
	    0x40000, 0x100, &is->is_iommu)) {
		panic("pyro: unable to create iommu handle");
	}

	is->is_sb[0] = &pbm->pp_sb;
	is->is_sb[0]->sb_bustag = is->is_bustag;

	name = (char *)malloc(32, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		panic("couldn't malloc iommu name");
	snprintf(name, 32, "%s dvma", sc->sc_dv.dv_xname);

	if (sc->sc_oberon)
		ihw = &iommu_hw_oberon;

	iommu_init(name, ihw, is, tsbsize, iobase);
}

void
pyro_iommu_enable(struct iommu_state *is)
{
	unsigned long cr;

	cr = IOMMUREG_READ(is, iommu_cr);
	cr |= IOMMUCR_FIRE_BE | IOMMUCR_FIRE_SE | IOMMUCR_FIRE_CM_EN |
	    IOMMUCR_FIRE_TE;

	IOMMUREG_WRITE(is, iommu_tsb, is->is_ptsb | is->is_tsbsize);
	IOMMUREG_WRITE(is, iommu_cr, cr);
}

void
pyro_init_msi(struct pyro_softc *sc, struct pyro_pbm *pbm)
{
	u_int32_t msi_addr_range[3];
	u_int32_t msi_eq_devino[3] = { 0, 36, 24 };
	int ihandle;
	int msis, msi_eq_size, num_eq;
	struct pyro_eq *eq;
	struct msi_eq *meq;
	struct pyro_msi_msg *msgs;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	/* One queue per cpu. */
	num_eq = ncpus;

	if (OF_getprop(sc->sc_node, "msi-address-ranges",
	    msi_addr_range, sizeof(msi_addr_range)) <= 0)
		return;
	pbm->pp_msiaddr = msi_addr_range[1];
	pbm->pp_msiaddr |= ((bus_addr_t)msi_addr_range[0]) << 32;

	msis = getpropint(sc->sc_node, "#msi", 256);
	pbm->pp_msi = mallocarray(msis, sizeof(*pbm->pp_msi), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (pbm->pp_msi == NULL)
		return;

	msi_eq_size = getpropint(sc->sc_node, "msi-eq-size", 256);
	pbm->pp_meq = msi_eq_alloc(pbm->pp_dmat, msi_eq_size, num_eq);
	if (pbm->pp_meq == NULL)
		goto free_table;

	bzero(pbm->pp_meq->meq_va,
	    pbm->pp_meq->meq_nentries * sizeof(struct pyro_msi_msg) *
	    num_eq);

	bus_space_write_8(sc->sc_bust, sc->sc_csrh, FIRE_EQ_BASE_ADDR,
	    pbm->pp_meq->meq_map->dm_segs[0].ds_addr);

	bus_space_write_8(sc->sc_bust, sc->sc_csrh,
	    FIRE_INTRMONDO_DATA0, sc->sc_ign);
	bus_space_write_8(sc->sc_bust, sc->sc_csrh,
	    FIRE_INTRMONDO_DATA1, 0);

	bus_space_write_8(sc->sc_bust, sc->sc_csrh, FIRE_MSI32_ADDR,
	    pbm->pp_msiaddr);

	if (OF_getprop(sc->sc_node, "msi-eq-to-devino",
	    msi_eq_devino, sizeof(msi_eq_devino)) == -1) {
		OF_getprop(sc->sc_node, "msi-eq-devino",
		    msi_eq_devino, sizeof(msi_eq_devino));
	}

	pbm->pp_eq = mallocarray(num_eq, sizeof(*eq), M_DEVBUF, M_WAITOK);
	pbm->pp_neq = num_eq;

	meq = pbm->pp_meq;
	msgs = (struct pyro_msi_msg *)meq->meq_va;

	CPU_INFO_FOREACH(cii, ci) {
		int unit = CPU_INFO_UNIT(ci);
		eq = &pbm->pp_eq[unit];

		eq->eq_id = unit;
		eq->eq_intr = msi_eq_devino[2] + unit;
		eq->eq_pbm = pbm;
		snprintf(eq->eq_name, sizeof(eq->eq_name), "%s:%d",
		    sc->sc_dv.dv_xname, unit);
		eq->eq_head = FIRE_EQ_HEAD(unit);
		eq->eq_tail = FIRE_EQ_TAIL(unit);
		eq->eq_ring = msgs + (unit * meq->meq_nentries);
		eq->eq_mask = (meq->meq_nentries - 1);

		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_EQ_HEAD(unit), 0);
		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_EQ_TAIL(unit), 0);

		ihandle = eq->eq_intr | sc->sc_ign;
		eq->eq_ih = pyro_intr_establish_cpu(pbm->pp_memt, sc->sc_bust,
		    ihandle, IPL_HIGH, BUS_INTR_ESTABLISH_MPSAFE, ci,
		    pyro_msi_eq_intr, eq, eq->eq_name);
		if (eq->eq_ih == NULL) {
			/* XXX */
			goto free_table;
		}

		/* Enable EQ. */
		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_EQ_CNTRL_SET(unit), FIRE_EQ_CTRL_SET_EN);
	}

	pbm->pp_flags |= PCI_FLAGS_MSI_ENABLED;

	return;

free_table:
	free(pbm->pp_msi, M_DEVBUF, 0);
}

int
pyro_print(void *aux, const char *p)
{
	if (p == NULL)
		return (UNCONF);
	return (QUIET);
}

int
pyro_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
pyro_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	struct cpu_info *ci = curcpu();
	pcireg_t val;
	int s;

	s = splhigh();
	__membar("#Sync");
	ci->ci_pci_probe = 1;
	val = bus_space_read_4(pc->bustag, pc->bushandle,
	    (PCITAG_OFFSET(tag) << 4) + reg);
	__membar("#Sync");
	if (ci->ci_pci_fault)
		val = 0xffffffff;
	ci->ci_pci_probe = ci->ci_pci_fault = 0;
	splx(s);

	return (val);
}

void
pyro_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
        bus_space_write_4(pc->bustag, pc->bushandle,
	    (PCITAG_OFFSET(tag) << 4) + reg, data);
}

/*
 * Bus-specific interrupt mapping
 */
int
pyro_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct pyro_pbm *pp = pa->pa_pc->cookie;
	struct pyro_softc *sc = pp->pp_sc;
	u_int dev;

	if (*ihp != (pci_intr_handle_t)-1) {
		*ihp |= sc->sc_ign;
		return (0);
	}

	/*
	 * We didn't find a PROM mapping for this interrupt.  Try to
	 * construct one ourselves based on the swizzled interrupt pin
	 * and the interrupt mapping for PCI slots documented in the
	 * UltraSPARC-IIi User's Manual.
	 */

	if (pa->pa_intrpin == 0)
		return (-1);

	/*
	 * This deserves some documentation.  Should anyone
	 * have anything official looking, please speak up.
	 */
	dev = pa->pa_device - 1;

	*ihp = (pa->pa_intrpin - 1) & INTMAP_PCIINT;
	*ihp |= (dev << 2) & INTMAP_PCISLOT;
	*ihp |= sc->sc_ign;

	return (0);
}

bus_space_tag_t
pyro_alloc_mem_tag(struct pyro_pbm *pp)
{
	return (pyro_alloc_bus_tag(pp, "mem",
	    0x02,       /* 32-bit mem space (where's the #define???) */
	    ASI_PRIMARY, ASI_PRIMARY_LITTLE));
}

bus_space_tag_t
pyro_alloc_io_tag(struct pyro_pbm *pp)
{
	return (pyro_alloc_bus_tag(pp, "io",
	    0x01,       /* IO space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
pyro_alloc_config_tag(struct pyro_pbm *pp)
{
	return (pyro_alloc_bus_tag(pp, "cfg",
	    0x00,       /* Config space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
pyro_alloc_bus_tag(struct pyro_pbm *pbm, const char *name, int ss,
    int asi, int sasi)
{
	struct pyro_softc *sc = pbm->pp_sc;
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("pyro: could not allocate bus tag");

	snprintf(bt->name, sizeof(bt->name), "%s-pbm_%s(%d/%2.2x)",
	    sc->sc_dv.dv_xname, name, ss, asi);

	bt->cookie = pbm;
	bt->parent = sc->sc_bust;
	bt->default_type = ss;
	bt->asi = asi;
	bt->sasi = sasi;
	bt->sparc_bus_map = pyro_bus_map;
	bt->sparc_bus_mmap = pyro_bus_mmap;
	bt->sparc_intr_establish = pyro_intr_establish;
	bt->sparc_intr_establish_cpu = pyro_intr_establish_cpu;
	return (bt);
}

bus_dma_tag_t
pyro_alloc_dma_tag(struct pyro_pbm *pbm)
{
	struct pyro_softc *sc = pbm->pp_sc;
	bus_dma_tag_t dt, pdt = sc->sc_dmat;

	dt = malloc(sizeof(*dt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dt == NULL)
		panic("pyro: could not alloc dma tag");

	dt->_cookie = pbm;
	dt->_parent = pdt;
	dt->_dmamap_create	= pyro_dmamap_create;
	dt->_dmamap_destroy	= iommu_dvmamap_destroy;
	dt->_dmamap_load	= iommu_dvmamap_load;
	dt->_dmamap_load_raw	= iommu_dvmamap_load_raw;
	dt->_dmamap_unload	= iommu_dvmamap_unload;
	dt->_dmamap_sync	= iommu_dvmamap_sync;
	dt->_dmamem_alloc	= iommu_dvmamem_alloc;
	dt->_dmamem_free	= iommu_dvmamem_free;
	return (dt);
}

pci_chipset_tag_t
pyro_alloc_chipset(struct pyro_pbm *pbm, int node, pci_chipset_tag_t pc)
{
	pci_chipset_tag_t npc;

	npc = malloc(sizeof *npc, M_DEVBUF, M_NOWAIT);
	if (npc == NULL)
		panic("pyro: could not allocate pci_chipset_tag_t");
	memcpy(npc, pc, sizeof *pc);
	npc->cookie = pbm;
	npc->rootnode = node;
	return (npc);
}

int
pyro_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	struct pyro_pbm *pp = t->_cookie;

	return (iommu_dvmamap_create(t, t0, &pp->pp_sb, size, nsegments,
	    maxsegsz, boundary, flags, dmamp));
}

int
pyro_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct pyro_pbm *pbm = t->cookie;
	int i, ss;

	DPRINTF(PDB_BUSMAP, ("pyro_bus_map: type %d off %llx sz %llx flags %d",
	    t->default_type,
	    (unsigned long long)offset,
	    (unsigned long long)size,
	    flags));

	ss = t->default_type;
	DPRINTF(PDB_BUSMAP, (" cspace %d", ss));

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\npyro_bus_map: invalid parent");
		return (EINVAL);
	}

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->parent->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	for (i = 0; i < pbm->pp_nrange; i++) {
		bus_addr_t paddr;

		if (((pbm->pp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pbm->pp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pbm->pp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_map)
		    (t, t0, paddr, size, flags, hp));
	}

	return (EINVAL);
}

paddr_t
pyro_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	bus_addr_t offset = paddr;
	struct pyro_pbm *pbm = t->cookie;
	int i, ss;

	ss = t->default_type;

	DPRINTF(PDB_BUSMAP, ("pyro_bus_mmap: prot %d flags %d pa %llx\n",
	    prot, flags, (unsigned long long)paddr));

	if (t->parent == 0 || t->parent->sparc_bus_mmap == 0) {
		printf("\npyro_bus_mmap: invalid parent");
		return (-1);
	}

	for (i = 0; i < pbm->pp_nrange; i++) {
		bus_addr_t paddr;

		if (((pbm->pp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pbm->pp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pbm->pp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_mmap)
		    (t, t0, paddr, off, prot, flags));
	}

	return (-1);
}

void *
pyro_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	return (pyro_intr_establish_cpu(t, t0, ihandle, level, flags, NULL,
	    handler, arg, what));
}

void *
pyro_intr_establish_cpu(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, struct cpu_info *ci,
    int (*handler)(void *), void *arg, const char *what)
{
	struct pyro_pbm *pbm = t->cookie;
	struct pyro_softc *sc = pbm->pp_sc;
	struct intrhand *ih = NULL;
	volatile u_int64_t *intrmapptr = NULL, *intrclrptr = NULL;
	int ino;

	if (PCI_INTR_TYPE(ihandle) != PCI_INTR_INTX) {
		pci_chipset_tag_t pc = pbm->pp_pc;
		pcitag_t tag = PCI_INTR_TAG(ihandle);
		int msinum = pbm->pp_msinum++;
		u_int64_t reg;

		ih = bus_intr_allocate(t0, handler, arg, ihandle, level,
		     NULL, NULL, what);
		if (ih == NULL)
			return (NULL);

		evcount_attach(&ih->ih_count, ih->ih_name, NULL);

		if (ci == NULL)
			ci = cpus; /* Default to the boot cpu. */

		ih->ih_cpu = ci;
		ih->ih_ack = pyro_msi_ack;

		pbm->pp_msi[msinum] = ih;
		ih->ih_number = msinum;

		if (flags & BUS_INTR_ESTABLISH_MPSAFE)
			ih->ih_mpsafe = 1;

		switch (PCI_INTR_TYPE(ihandle)) {
		case PCI_INTR_MSI:
			pci_msi_enable(pc, tag, pbm->pp_msiaddr, msinum);
			break;
		case PCI_INTR_MSIX:
			pci_msix_enable(pc, tag, pbm->pp_memt,
			    PCI_INTR_VEC(ihandle), pbm->pp_msiaddr, msinum);
			break;
		}

		/* Map MSI to the right EQ and mark it as valid. */
		reg = bus_space_read_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_MAP(msinum));
		CLR(reg, FIRE_MSI_MAP_EQNUM);
		SET(reg, CPU_INFO_UNIT(ci)); /* There's an eq per cpu. */
		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_MAP(msinum), reg);

		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_CLEAR(msinum), FIRE_MSI_CLEAR_EQWR_N);

		reg = bus_space_read_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_MAP(msinum));
		SET(reg, FIRE_MSI_MAP_V);
		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_MAP(msinum), reg);

		return (ih);
	}

	ino = INTINO(ihandle);

	if (level == IPL_NONE)
		level = INTLEV(ihandle);
	if (level == IPL_NONE) {
		printf(": no IPL, setting IPL 2.\n");
		level = 2;
	}

	if ((flags & BUS_INTR_ESTABLISH_SOFTINTR) == 0) {
		u_int64_t *imap, *iclr;

		imap = bus_space_vaddr(sc->sc_bust, sc->sc_csrh) + 0x1000;
		iclr = bus_space_vaddr(sc->sc_bust, sc->sc_csrh) + 0x1400;
		intrmapptr = &imap[ino];
		intrclrptr = &iclr[ino];
		ino |= INTVEC(ihandle);
	}

	ih = bus_intr_allocate(t0, handler, arg, ino, level, NULL,
	    intrclrptr, what);
	if (ih == NULL)
		return (NULL);

	ih->ih_cpu = ci;
	if (flags & BUS_INTR_ESTABLISH_MPSAFE)
		ih->ih_mpsafe = 1;

	intr_establish(ih);

	if (intrmapptr != NULL) {
		u_int64_t intrmap;

		ci = ih->ih_cpu;

		intrmap = *intrmapptr;
		intrmap &= ~FIRE_INTRMAP_INT_CNTRL_NUM_MASK;
		intrmap |= FIRE_INTRMAP_INT_CNTRL_NUM0;
		if (sc->sc_oberon) {
			intrmap &= ~OBERON_INTRMAP_T_DESTID_MASK;
			intrmap |= ci->ci_upaid <<
			    OBERON_INTRMAP_T_DESTID_SHIFT;
		} else {
			intrmap &= ~FIRE_INTRMAP_T_JPID_MASK;
			intrmap |= ci->ci_upaid <<
			    FIRE_INTRMAP_T_JPID_SHIFT;
		}
		intrmap |= INTMAP_V;
		membar_producer();
		*intrmapptr = intrmap;
		intrmap = *intrmapptr;
		ih->ih_number |= intrmap & INTMAP_INR;
	}

	return (ih);
}

void
pyro_msi_ack(struct intrhand *ih)
{
}

int
pyro_msi_eq_intr(void *arg)
{
	struct pyro_eq *eq = arg;
	struct pyro_pbm *pbm = eq->eq_pbm;
	struct pyro_softc *sc = pbm->pp_sc;
	struct pyro_msi_msg *msg;
	uint64_t head, tail;
	struct intrhand *ih;
	int msinum;

	head = bus_space_read_8(sc->sc_bust, sc->sc_csrh, eq->eq_head);
	tail = bus_space_read_8(sc->sc_bust, sc->sc_csrh, eq->eq_tail);

	if (head == tail)
		return (0);

	do {
		msg = &eq->eq_ring[head];
		if (msg->mm_type == 0)
			break;

		msg->mm_type = 0;

		msinum = msg->mm_data;
		ih = pbm->pp_msi[msinum];
		bus_space_write_8(sc->sc_bust, sc->sc_csrh,
		    FIRE_MSI_CLEAR(msinum), FIRE_MSI_CLEAR_EQWR_N);

		send_softint(ih->ih_pil, ih);

		head += 1;
		head &= eq->eq_mask;
	} while (head != tail);

	bus_space_write_8(sc->sc_bust, sc->sc_csrh, eq->eq_head, head);

	return (1);
}

#ifdef DDB
void
pyro_xir(void *arg, int cpu)
{
	struct pyro_softc *sc = arg;

	bus_space_write_8(sc->sc_bust, sc->sc_xbch, FIRE_RESET_GEN,
	    FIRE_RESET_GEN_XIR);
}
#endif

const struct cfattach pyro_ca = {
	sizeof(struct pyro_softc), pyro_match, pyro_attach
};

struct cfdriver pyro_cd = {
	NULL, "pyro", DV_DULL
};
