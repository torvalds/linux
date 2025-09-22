/*	$OpenBSD: schizo.c,v 1.71 2025/06/28 11:34:21 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2003 Henric Jungheim
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
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/reboot.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/psl.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/schizoreg.h>
#include <sparc64/dev/schizovar.h>
#include <sparc64/sparc64/cache.h>

#ifdef DEBUG
#define SDB_PROM        0x01
#define SDB_BUSMAP      0x02
#define SDB_INTR        0x04
#define SDB_CONF        0x08
int schizo_debug = ~0;
#define DPRINTF(l, s)   do { if (schizo_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

extern struct sparc_pci_chipset _sparc_pci_chipset;

int schizo_match(struct device *, void *, void *);
void schizo_attach(struct device *, struct device *, void *);
void schizo_init(struct schizo_softc *, int);
void schizo_init_iommu(struct schizo_softc *, struct schizo_pbm *);
int schizo_print(void *, const char *);

void schizo_set_intr(struct schizo_softc *, struct schizo_pbm *, int,
    int (*handler)(void *), void *, int, char *);
int schizo_ue(void *);
int schizo_ce(void *);
int schizo_safari_error(void *);
int schizo_pci_error(void *);

pci_chipset_tag_t schizo_alloc_chipset(struct schizo_pbm *, int,
    pci_chipset_tag_t);
bus_space_tag_t schizo_alloc_mem_tag(struct schizo_pbm *);
bus_space_tag_t schizo_alloc_io_tag(struct schizo_pbm *);
bus_space_tag_t schizo_alloc_config_tag(struct schizo_pbm *);
bus_space_tag_t schizo_alloc_bus_tag(struct schizo_pbm *, const char *,
    int, int, int);
bus_dma_tag_t schizo_alloc_dma_tag(struct schizo_pbm *);

int schizo_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t schizo_conf_read(pci_chipset_tag_t, pcitag_t, int);
void schizo_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);

int schizo_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int schizo_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t,
    bus_size_t, int, bus_space_handle_t *);
paddr_t schizo_bus_mmap(bus_space_tag_t, bus_space_tag_t, bus_addr_t, off_t,
    int, int);
bus_addr_t schizo_bus_addr(bus_space_tag_t, bus_space_tag_t,
    bus_space_handle_t);
void *schizo_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int, int,
    int (*)(void *), void *, const char *);

int schizo_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
    bus_size_t, bus_size_t, int, bus_dmamap_t *);

#ifdef DDB
void schizo_xir(void *, int);
#endif

int
schizo_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	char *str;

	if (strcmp(ma->ma_name, "pci") != 0)
		return (0);

	str = getpropstring(ma->ma_node, "model");
	if (strcmp(str, "schizo") == 0)
		return (1);

	str = getpropstring(ma->ma_node, "compatible");
	if (strcmp(str, "pci108e,8001") == 0)
		return (1);
	if (strcmp(str, "pci108e,8002") == 0)		/* XMITS */
		return (1);
	if (strcmp(str, "pci108e,a801") == 0)		/* Tomatillo */
		return (1);

	return (0);
}

void
schizo_attach(struct device *parent, struct device *self, void *aux)
{
	struct schizo_softc *sc = (struct schizo_softc *)self;
	struct mainbus_attach_args *ma = aux;
	int busa;
	char *str;

	str = getpropstring(ma->ma_node, "compatible");
	if (strcmp(str, "pci108e,a801") == 0)
		sc->sc_tomatillo = 1;

	sc->sc_node = ma->ma_node;
	sc->sc_dmat = ma->ma_dmatag;
	sc->sc_bust = ma->ma_bustag;
	sc->sc_ctrl = ma->ma_reg[1].ur_paddr - 0x10000UL;
	sc->sc_ign = INTIGN(ma->ma_upaid << INTMAP_IGN_SHIFT);

	if ((ma->ma_reg[0].ur_paddr & 0x00700000) == 0x00600000)
		busa = 1;
	else
		busa = 0;

	if (bus_space_map(sc->sc_bust, sc->sc_ctrl,
	    sizeof(struct schizo_regs), 0, &sc->sc_ctrlh)) {
		printf(": failed to map registers\n");
		return;
	}

	/* enable schizo ecc error interrupts */
	schizo_write(sc, SCZ_ECCCTRL, schizo_read(sc, SCZ_ECCCTRL) |
	    SCZ_ECCCTRL_EE_INTEN | SCZ_ECCCTRL_UE_INTEN |
	    SCZ_ECCCTRL_CE_INTEN);

	schizo_init(sc, busa);
}

void
schizo_init(struct schizo_softc *sc, int busa)
{
	struct schizo_pbm *pbm;
	struct pcibus_attach_args pba;
	int *busranges = NULL, nranges;
	u_int64_t match, reg;

	pbm = malloc(sizeof(*pbm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pbm == NULL)
		panic("schizo: can't alloc schizo pbm");

	pbm->sp_sc = sc;
	pbm->sp_bus_a = busa;
	pbm->sp_regt = sc->sc_bust;

	if (getprop(sc->sc_node, "ranges", sizeof(struct schizo_range),
	    &pbm->sp_nrange, (void **)&pbm->sp_range))
		panic("schizo: can't get ranges");

	if (getprop(sc->sc_node, "bus-range", sizeof(int), &nranges,
	    (void **)&busranges))
		panic("schizo: can't get bus-range");

	printf(": \"%s\", version %d, ign %x, bus %c %d to %d\n",
	    sc->sc_tomatillo ? "Tomatillo" : "Schizo",
	    getpropint(sc->sc_node, "version#", 0), sc->sc_ign,
	    busa ? 'A' : 'B', busranges[0], busranges[1]);

	if (bus_space_subregion(pbm->sp_regt, sc->sc_ctrlh,
	    busa ? offsetof(struct schizo_regs, pbm_a) :
	    offsetof(struct schizo_regs, pbm_b),
	    sizeof(struct schizo_pbm_regs),
	    &pbm->sp_regh)) {
		panic("schizo: unable to create PBM handle");
	}

	printf("%s: ", sc->sc_dv.dv_xname);
	schizo_init_iommu(sc, pbm);

	match = schizo_read(sc, busa ? SCZ_PCIA_IO_MATCH : SCZ_PCIB_IO_MATCH);
	pbm->sp_confpaddr = match & ~0x8000000000000000UL;

	pbm->sp_memt = schizo_alloc_mem_tag(pbm);
	pbm->sp_iot = schizo_alloc_io_tag(pbm);
	pbm->sp_cfgt = schizo_alloc_config_tag(pbm);
	pbm->sp_dmat = schizo_alloc_dma_tag(pbm);

	if (bus_space_map(pbm->sp_cfgt, 0, 0x1000000, 0, &pbm->sp_cfgh))
		panic("schizo: can't map config space");

	pbm->sp_pc = schizo_alloc_chipset(pbm, sc->sc_node,
	    &_sparc_pci_chipset);

	pbm->sp_pc->bustag = pbm->sp_cfgt;
	pbm->sp_pc->bushandle = pbm->sp_cfgh;

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = busranges[0];
	pba.pba_pc = pbm->sp_pc;
#if 0
	pba.pba_flags = pbm->sp_flags;
#endif
	pba.pba_dmat = pbm->sp_dmat;
	pba.pba_memt = pbm->sp_memt;
	pba.pba_iot = pbm->sp_iot;
	pba.pba_pc->conf_size = schizo_conf_size;
	pba.pba_pc->conf_read = schizo_conf_read;
	pba.pba_pc->conf_write = schizo_conf_write;
	pba.pba_pc->intr_map = schizo_intr_map;

	free(busranges, M_DEVBUF, 0);

	schizo_pbm_write(pbm, SCZ_PCI_INTR_RETRY, 5);

	/* clear out the bus errors */
	schizo_pbm_write(pbm, SCZ_PCI_CTRL, schizo_pbm_read(pbm, SCZ_PCI_CTRL));
	schizo_pbm_write(pbm, SCZ_PCI_AFSR, schizo_pbm_read(pbm, SCZ_PCI_AFSR));
	schizo_cfg_write(pbm, PCI_COMMAND_STATUS_REG,
	    schizo_cfg_read(pbm, PCI_COMMAND_STATUS_REG));

	reg = schizo_pbm_read(pbm, SCZ_PCI_CTRL);
	/* enable/disable error interrupts and arbiter */
	reg |= SCZ_PCICTRL_EEN | SCZ_PCICTRL_MMU_INT | SCZ_PCICTRL_ARB;
	reg &= ~SCZ_PCICTRL_SBH_INT;
	schizo_pbm_write(pbm, SCZ_PCI_CTRL, reg);

	reg = schizo_pbm_read(pbm, SCZ_PCI_DIAG);
	reg &= ~(SCZ_PCIDIAG_D_RTRYARB | SCZ_PCIDIAG_D_RETRY |
	    SCZ_PCIDIAG_D_INTSYNC);
	schizo_pbm_write(pbm, SCZ_PCI_DIAG, reg);

	if (busa)
		schizo_set_intr(sc, pbm, PIL_HIGH, schizo_pci_error,
		   pbm, SCZ_PCIERR_A_INO, "pci_a");
	else
		schizo_set_intr(sc, pbm, PIL_HIGH, schizo_pci_error,
		   pbm, SCZ_PCIERR_B_INO, "pci_b");

	/* double mapped */
	schizo_set_intr(sc, pbm, PIL_HIGH, schizo_ue, sc, SCZ_UE_INO,
	    "ue");
	schizo_set_intr(sc, pbm, PIL_HIGH, schizo_ce, sc, SCZ_CE_INO,
	    "ce");
	schizo_set_intr(sc, pbm, PIL_HIGH, schizo_safari_error, sc,
	    SCZ_SERR_INO, "safari");

#ifdef DDB
	/* 
	 * Only a master Tomatillo (the one with JPID[0:2] = 6)
	 * can/should generate XIR.
	 */
	if (sc->sc_tomatillo &&
	    ((schizo_read(sc, SCZ_CONTROL_STATUS) >> 20) & 0x7) == 6)
		db_register_xir(schizo_xir, sc);
#endif

	config_found(&sc->sc_dv, &pba, schizo_print);
}

int
schizo_ue(void *vsc)
{
	struct schizo_softc *sc = vsc;

	panic("%s: uncorrectable error", sc->sc_dv.dv_xname);
	return (1);
}

int
schizo_ce(void *vsc)
{
	struct schizo_softc *sc = vsc;

	panic("%s: correctable error", sc->sc_dv.dv_xname);
	return (1);
}

int
schizo_pci_error(void *vpbm)
{
	struct schizo_pbm *sp = vpbm;
	struct schizo_softc *sc = sp->sp_sc;
	u_int64_t afsr, afar, ctrl;
	u_int32_t csr;

	afsr = schizo_pbm_read(sp, SCZ_PCI_AFSR);
	afar = schizo_pbm_read(sp, SCZ_PCI_AFAR);
	ctrl = schizo_pbm_read(sp, SCZ_PCI_CTRL);
	csr = schizo_cfg_read(sp, PCI_COMMAND_STATUS_REG);

	printf("%s: pci bus %c error\n", sc->sc_dv.dv_xname,
	    sp->sp_bus_a ? 'A' : 'B');

	printf("PCIAFSR=%llb\n", afsr, SCZ_PCIAFSR_BITS);
	printf("PCIAFAR=%llx\n", afar);
	printf("PCICTRL=%llb\n", ctrl, SCZ_PCICTRL_BITS);
	printf("PCICSR=%b\n", csr, PCI_COMMAND_STATUS_BITS);

	if (ctrl & SCZ_PCICTRL_MMU_ERR) {
		u_int64_t ctrl, tfar;

		ctrl = schizo_pbm_read(sp, SCZ_PCI_IOMMU_CTRL);
		printf("IOMMUCTRL=%llx\n", ctrl);

		if ((ctrl & TOM_IOMMU_ERR) == 0)
			goto clear_error;

		if (sc->sc_tomatillo) {
			tfar = schizo_pbm_read(sp, TOM_PCI_IOMMU_TFAR);
			printf("IOMMUTFAR=%llx\n", tfar);
		}

		/* These are non-fatal if target abort was signalled. */
		if ((ctrl & TOM_IOMMU_ERR_MASK) == TOM_IOMMU_INV_ERR ||
		    ctrl & TOM_IOMMU_ILLTSBTBW_ERR ||
		    ctrl & TOM_IOMMU_BADVA_ERR) {
			if (csr & PCI_STATUS_TARGET_TARGET_ABORT) {
				schizo_pbm_write(sp, SCZ_PCI_IOMMU_CTRL, ctrl);
				goto clear_error;
			}
		}
	}

	panic("%s: fatal", sc->sc_dv.dv_xname);

 clear_error:
	schizo_cfg_write(sp, PCI_COMMAND_STATUS_REG, csr);
	schizo_pbm_write(sp, SCZ_PCI_CTRL, ctrl);
	schizo_pbm_write(sp, SCZ_PCI_AFSR, afsr);
	return (1);
}

int
schizo_safari_error(void *vsc)
{
	struct schizo_softc *sc = vsc;

	printf("%s: safari error\n", sc->sc_dv.dv_xname);

	printf("ERRLOG=%llx\n", schizo_read(sc, SCZ_SAFARI_ERRLOG));
	printf("UE_AFSR=%llx\n", schizo_read(sc, SCZ_UE_AFSR));
	printf("UE_AFAR=%llx\n", schizo_read(sc, SCZ_UE_AFAR));
	printf("CE_AFSR=%llx\n", schizo_read(sc, SCZ_CE_AFSR));
	printf("CE_AFAR=%llx\n", schizo_read(sc, SCZ_CE_AFAR));

	panic("%s: fatal", sc->sc_dv.dv_xname);
	return (1);
}

void
schizo_init_iommu(struct schizo_softc *sc, struct schizo_pbm *pbm)
{
	struct iommu_state *is = &pbm->sp_is;
	int *vdma = NULL, nitem, tsbsize = 7;
	u_int32_t iobase = -1;
	vaddr_t va;
	char *name;

	va = (vaddr_t)pbm->sp_flush[0x40];

	is->is_bustag = pbm->sp_regt;

	if (bus_space_subregion(is->is_bustag, pbm->sp_regh,
	    offsetof(struct schizo_pbm_regs, iommu),
	    sizeof(struct iommureg), &is->is_iommu)) {
		panic("schizo: unable to create iommu handle");
	} 

	is->is_sb[0] = &pbm->sp_sb;
	is->is_sb[0]->sb_bustag = is->is_bustag;
	is->is_sb[0]->sb_flush = (void *)(va & ~0x3f);

	if (bus_space_subregion(is->is_bustag, pbm->sp_regh,
	    offsetof(struct schizo_pbm_regs, strbuf),
	    sizeof(struct iommu_strbuf), &is->is_sb[0]->sb_sb)) {
		panic("schizo: unable to create streaming buffer handle");
		is->is_sb[0]->sb_flush = NULL;
	} 

#if 1
	/* XXX disable the streaming buffers for now */
	bus_space_write_8(is->is_bustag, is->is_sb[0]->sb_sb,
	    STRBUFREG(strbuf_ctl),
	    bus_space_read_8(is->is_bustag, is->is_sb[0]->sb_sb,
		STRBUFREG(strbuf_ctl)) & ~STRBUF_EN);
	is->is_sb[0]->sb_flush = NULL;
#endif

	name = (char *)malloc(32, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		panic("couldn't malloc iommu name");
	snprintf(name, 32, "%s dvma", sc->sc_dv.dv_xname);

	/*
	 * Separate the men from the boys.  If the `virtual-dma'
	 * property exists, use it.
	 */
	if (!getprop(sc->sc_node, "virtual-dma", sizeof(vdma), &nitem, 
	    (void **)&vdma)) {
		/* Damn.  Gotta use these values. */
		iobase = vdma[0];
#define	TSBCASE(x)	case 1 << ((x) + 23): tsbsize = (x); break
		switch (vdma[1]) { 
			TSBCASE(1); TSBCASE(2); TSBCASE(3);
			TSBCASE(4); TSBCASE(5); TSBCASE(6);
		default: 
			printf("bogus tsb size %x, using 7\n", vdma[1]);
			TSBCASE(7);
		}
#undef TSBCASE
		DPRINTF(SDB_BUSMAP, ("schizo_iommu_init: iobase=0x%x\n", iobase));
		free(vdma, M_DEVBUF, 0);
	} else {
		DPRINTF(SDB_BUSMAP, ("schizo_iommu_init: getprop failed, "
		    "using iobase=0x%x, tsbsize=%d\n", iobase, tsbsize));
	}

	iommu_init(name, &iommu_hw_default, is, tsbsize, iobase);
}

int
schizo_print(void *aux, const char *p)
{
	if (p == NULL)
		return (UNCONF);
	return (QUIET);
}

int
schizo_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
schizo_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	struct cpu_info *ci = curcpu();
	pcireg_t val;
	int s;

	s = splhigh();
	__membar("#Sync");
	ci->ci_pci_probe = 1;
	val = bus_space_read_4(pc->bustag, pc->bushandle,
	    PCITAG_OFFSET(tag) + reg);
	__membar("#Sync");
	if (ci->ci_pci_fault)
		val = 0xffffffff;
	ci->ci_pci_probe = ci->ci_pci_fault = 0;
	splx(s);

	return (val);
}

void
schizo_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
        bus_space_write_4(pc->bustag, pc->bushandle,
	    PCITAG_OFFSET(tag) + reg, data);
}

/*
 * Bus-specific interrupt mapping
 */
int
schizo_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct schizo_pbm *sp = pa->pa_pc->cookie;
	struct schizo_softc *sc = sp->sp_sc;
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

void
schizo_set_intr(struct schizo_softc *sc, struct schizo_pbm *pbm, int ipl,
    int (*handler)(void *), void *arg, int ino, char *what)
{
	struct intrhand *ih;
	volatile u_int64_t *map, *clr;
	struct schizo_pbm_regs *pbmreg;
	char *name;
	int nlen;

	pbmreg = bus_space_vaddr(pbm->sp_regt, pbm->sp_regh);
	map = &pbmreg->imap[ino];
	clr = &pbmreg->iclr[ino];
	ino |= sc->sc_ign;

	nlen = strlen(sc->sc_dv.dv_xname) + 1 + strlen(what) + 1;
	name = malloc(nlen, M_DEVBUF, M_WAITOK);
	snprintf(name, nlen, "%s:%s", sc->sc_dv.dv_xname, what);

	ih = bus_intr_allocate(pbm->sp_regt, handler, arg, ino, ipl,
	    map, clr, name);
	if (ih == NULL) {
		printf("set_intr failed...\n");
		free(name, M_DEVBUF, 0);
		return;
	}

	intr_establish(ih);
}

bus_space_tag_t
schizo_alloc_mem_tag(struct schizo_pbm *sp)
{
	return (schizo_alloc_bus_tag(sp, "mem",
	    0x02,       /* 32-bit mem space (where's the #define???) */
	    ASI_PRIMARY, ASI_PRIMARY_LITTLE));
}

bus_space_tag_t
schizo_alloc_io_tag(struct schizo_pbm *sp)
{
	return (schizo_alloc_bus_tag(sp, "io",
	    0x01,       /* IO space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
schizo_alloc_config_tag(struct schizo_pbm *sp)
{
	return (schizo_alloc_bus_tag(sp, "cfg",
	    0x00,       /* Config space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
schizo_alloc_bus_tag(struct schizo_pbm *pbm, const char *name, int ss,
    int asi, int sasi)
{
	struct schizo_softc *sc = pbm->sp_sc;
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("schizo: could not allocate bus tag");

	snprintf(bt->name, sizeof(bt->name), "%s-pbm_%s(%d/%2.2x)",
	    sc->sc_dv.dv_xname, name, ss, asi);

	bt->cookie = pbm;
	bt->parent = sc->sc_bust;
	bt->default_type = ss;
	bt->asi = asi;
	bt->sasi = sasi;
	bt->sparc_bus_map = schizo_bus_map;
	bt->sparc_bus_mmap = schizo_bus_mmap;
	bt->sparc_bus_addr = schizo_bus_addr;
	bt->sparc_intr_establish = schizo_intr_establish;
	return (bt);
}

bus_dma_tag_t
schizo_alloc_dma_tag(struct schizo_pbm *pbm)
{
	struct schizo_softc *sc = pbm->sp_sc;
	bus_dma_tag_t dt, pdt = sc->sc_dmat;

	dt = malloc(sizeof(*dt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dt == NULL)
		panic("schizo: could not alloc dma tag");

	dt->_cookie = pbm;
	dt->_parent = pdt;
	dt->_dmamap_create	= schizo_dmamap_create;
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
schizo_alloc_chipset(struct schizo_pbm *pbm, int node, pci_chipset_tag_t pc)
{
	pci_chipset_tag_t npc;

	npc = malloc(sizeof *npc, M_DEVBUF, M_NOWAIT);
	if (npc == NULL)
		panic("schizo: could not allocate pci_chipset_tag_t");
	memcpy(npc, pc, sizeof *pc);
	npc->cookie = pbm;
	npc->rootnode = node;
	return (npc);
}

int
schizo_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	struct schizo_pbm *sp = t->_cookie;

	return (iommu_dvmamap_create(t, t0, &sp->sp_sb, size, nsegments,
	    maxsegsz, boundary, flags, dmamp));
}

int
schizo_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct schizo_pbm *pbm = t->cookie;
	int i, ss;

	DPRINTF(SDB_BUSMAP,("schizo_bus_map: type %d off %llx sz %llx flags %d",
	    t->default_type,
	    (unsigned long long)offset,
	    (unsigned long long)size,
	    flags));

	ss = t->default_type;
	DPRINTF(SDB_BUSMAP, (" cspace %d", ss));

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\nschizo_bus_map: invalid parent");
		return (EINVAL);
	}

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->parent->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	for (i = 0; i < pbm->sp_nrange; i++) {
		bus_addr_t paddr;

		if (((pbm->sp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pbm->sp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pbm->sp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_map)
		    (t, t0, paddr, size, flags, hp));
	}

	return (EINVAL);
}

paddr_t
schizo_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	bus_addr_t offset = paddr;
	struct schizo_pbm *pbm = t->cookie;
	int i, ss;

	ss = t->default_type;

	DPRINTF(SDB_BUSMAP, ("schizo_bus_mmap: prot %d flags %d pa %llx\n",
	    prot, flags, (unsigned long long)paddr));

	if (t->parent == 0 || t->parent->sparc_bus_mmap == 0) {
		printf("\nschizo_bus_mmap: invalid parent");
		return (-1);
	}

	for (i = 0; i < pbm->sp_nrange; i++) {
		bus_addr_t paddr;

		if (((pbm->sp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pbm->sp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pbm->sp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_mmap)
		    (t, t0, paddr, off, prot, flags));
	}

	return (-1);
}

bus_addr_t
schizo_bus_addr(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t h)
{
	struct schizo_pbm *pbm = t->cookie;
	bus_addr_t addr;
	int i, ss;

	ss = t->default_type;

	if (t->parent == 0 || t->parent->sparc_bus_addr == 0) {
		printf("\nschizo_bus_addr: invalid parent");
		return (-1);
	}

	t = t->parent;

	addr = ((*t->sparc_bus_addr)(t, t0, h));
	if (addr == -1)
		return (-1);

	for (i = 0; i < pbm->sp_nrange; i++) {
		if (((pbm->sp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		return (BUS_ADDR_PADDR(addr) - pbm->sp_range[i].phys_lo);
	}

	return (-1);
}

void *
schizo_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct schizo_pbm *pbm = t->cookie;
	struct intrhand *ih = NULL;
	volatile u_int64_t *intrmapptr = NULL, *intrclrptr = NULL;
	int ino;
	long vec = INTVEC(ihandle);

	vec = INTVEC(ihandle);
	ino = INTINO(vec);

	if (level == IPL_NONE)
		level = INTLEV(vec);
	if (level == IPL_NONE) {
		printf(": no IPL, setting IPL 2.\n");
		level = 2;
	}

	if ((flags & BUS_INTR_ESTABLISH_SOFTINTR) == 0) {
		struct schizo_pbm_regs *pbmreg;

		pbmreg = bus_space_vaddr(pbm->sp_regt, pbm->sp_regh);
		intrmapptr = &pbmreg->imap[ino];
		intrclrptr = &pbmreg->iclr[ino];
		if (INTIGN(vec) == 0)
			ino |= (*intrmapptr) & INTMAP_IGN;
		else
			ino |= vec & INTMAP_IGN;
	}

	ih = bus_intr_allocate(t0, handler, arg, ino, level, intrmapptr,
	    intrclrptr, what);
	if (ih == NULL)
		return (NULL);

	if (flags & BUS_INTR_ESTABLISH_MPSAFE)
		ih->ih_mpsafe = 1;

	intr_establish(ih);

	if (intrmapptr != NULL) {
		u_int64_t intrmap;

		intrmap = *intrmapptr;
		intrmap |= INTMAP_V;
		*intrmapptr = intrmap;
		intrmap = *intrmapptr;
		ih->ih_number |= intrmap & INTMAP_INR;
	}

	return (ih);
}

#ifdef DDB
void
schizo_xir(void *arg, int cpu)
{
	struct schizo_softc *sc = arg;

	schizo_write(sc, TOM_RESET_GEN, TOM_RESET_GEN_XIR);
}
#endif

const struct cfattach schizo_ca = {
	sizeof(struct schizo_softc), schizo_match, schizo_attach
};

struct cfdriver schizo_cd = {
	NULL, "schizo", DV_DULL
};
