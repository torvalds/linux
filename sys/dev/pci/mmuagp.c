/*-
 * Copyright (c) 2004, 2005 Jung-uk Kim <jkim@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/agpvar.h>
#include <dev/pci/agpreg.h>

#include <dev/pci/pcidevs.h>

#include <machine/bus.h>

/*
 * AMD64 GART registers
 */
#define	AGP_AMD64_APCTRL		0x90
#define	AGP_AMD64_APBASE		0x94
#define	AGP_AMD64_ATTBASE		0x98
#define	AGP_AMD64_CACHECTRL		0x9c
#define	AGP_AMD64_APCTRL_GARTEN		0x00000001
#define	AGP_AMD64_APCTRL_SIZE_MASK	0x0000000e
#define	AGP_AMD64_APCTRL_DISGARTCPU	0x00000010
#define	AGP_AMD64_APCTRL_DISGARTIO	0x00000020
#define	AGP_AMD64_APCTRL_DISWLKPRB	0x00000040
#define	AGP_AMD64_APBASE_MASK		0x00007fff
#define	AGP_AMD64_ATTBASE_MASK		0xfffffff0
#define	AGP_AMD64_CACHECTRL_INVGART	0x00000001
#define	AGP_AMD64_CACHECTRL_PTEERR	0x00000002

/*
 * NVIDIA nForce3 registers
 */
#define AGP_AMD64_NVIDIA_0_APBASE	0x10
#define AGP_AMD64_NVIDIA_1_APBASE1	0x50
#define AGP_AMD64_NVIDIA_1_APLIMIT1	0x54
#define AGP_AMD64_NVIDIA_1_APSIZE	0xa8
#define AGP_AMD64_NVIDIA_1_APBASE2	0xd8
#define AGP_AMD64_NVIDIA_1_APLIMIT2	0xdc

/*
 * ULi M1689 registers
 */
#define AGP_AMD64_ULI_APBASE		0x10
#define AGP_AMD64_ULI_HTT_FEATURE	0x50
#define AGP_AMD64_ULI_ENU_SCR		0x54


#define	AMD64_MAX_MCTRL		8

/* XXX nForce3 requires secondary AGP bridge at 0:11:0. */
#define AGP_AMD64_NVIDIA_PCITAG(pc)	pci_make_tag(pc, 0, 11, 0)
/* XXX Some VIA bridge requires secondary AGP bridge at 0:1:0. */
#define AGP_AMD64_VIA_PCITAG(pc)	pci_make_tag(pc, 0, 1, 0)


int	mmuagp_probe(struct device *, void *, void *);
void	mmuagp_attach(struct device *, struct device *, void *);
bus_size_t mmuagp_get_aperture(void *);
int	mmuagp_set_aperture(void *, bus_size_t);
void	mmuagp_bind_page(void *, bus_addr_t, paddr_t, int);
void	mmuagp_unbind_page(void *, bus_addr_t);
void	mmuagp_flush_tlb(void *);

void	mmuagp_apbase_fixup(void *);

void	mmuagp_uli_init(void *);
int	mmuagp_uli_set_aperture(void *, bus_size_t);

int	mmuagp_nvidia_match(const struct pci_attach_args *, uint16_t);
void	mmuagp_nvidia_init(void *);
int	mmuagp_nvidia_set_aperture(void *, bus_size_t);

int	mmuagp_via_match(const struct pci_attach_args *);
void	mmuagp_via_init(void *);
int	mmuagp_via_set_aperture(void *, bus_size_t);

struct mmuagp_softc {
	struct device		 dev;
	struct agp_softc	*agpdev;
	struct agp_gatt		*gatt;
	bus_addr_t		 msc_apaddr;
	bus_size_t		 msc_apsize;
	uint32_t		 apbase;
	pcitag_t		 ctrl_tag;	/* use NVIDIA and VIA */
	pcitag_t		 mctrl_tag[AMD64_MAX_MCTRL];
	pci_chipset_tag_t	 msc_pc;
	pcitag_t		 msc_tag;
	int			 n_mctrl;
};

const struct cfattach mmuagp_ca = {
        sizeof(struct mmuagp_softc), mmuagp_probe, mmuagp_attach
};

struct cfdriver mmuagp_cd = {
	NULL, "mmuagp", DV_DULL
};

const struct agp_methods mmuagp_methods = {
	mmuagp_bind_page,
	mmuagp_unbind_page,
	mmuagp_flush_tlb,
};

int
mmuagp_probe(struct device *parent, void *match, void *aux)
{
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;

	/* Must be a pchb, don't attach to iommu-style agp devs */
	if (agpbus_probe(aa) == 0)
		return (0);

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_ALI:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_ALI_M1689:
			return (1);
		}
		break;
	case PCI_VENDOR_AMD:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_AMD_8151_SC:
			return (1);
		}
		break;
	case PCI_VENDOR_NVIDIA:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_NVIDIA_NFORCE3_PCHB:
			return (mmuagp_nvidia_match(pa,
			    PCI_PRODUCT_NVIDIA_NFORCE3_PPB2));
			/* NOTREACHED */
		case PCI_PRODUCT_NVIDIA_NFORCE3_250_PCHB:
			return (mmuagp_nvidia_match(pa,
			    PCI_PRODUCT_NVIDIA_NFORCE3_250_AGP));
			/* NOTREACHED */
		}
		break;
	case PCI_VENDOR_SIS:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_SIS_755:
		case PCI_PRODUCT_SIS_760:
			return (1);
		}
		break;
	case PCI_VENDOR_VIATECH:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_VIATECH_K8M800_0:
		case PCI_PRODUCT_VIATECH_K8T890_0:
		case PCI_PRODUCT_VIATECH_K8HTB_0:
		case PCI_PRODUCT_VIATECH_K8HTB:
			return (1);
		}
		break;
	}

	return (0);
}

int
mmuagp_nvidia_match(const struct pci_attach_args *pa, uint16_t devid)
{
	pcitag_t	tag;
	pcireg_t	reg;

	tag = AGP_AMD64_NVIDIA_PCITAG(pa->pa_pc);

	reg = pci_conf_read(pa->pa_pc, tag, PCI_CLASS_REG);
	if (PCI_CLASS(reg) != PCI_CLASS_BRIDGE ||
	    PCI_SUBCLASS(reg) != PCI_SUBCLASS_BRIDGE_PCI)
		return 0;

	reg = pci_conf_read(pa->pa_pc, tag, PCI_ID_REG);
	if (PCI_VENDOR(reg) != PCI_VENDOR_NVIDIA || PCI_PRODUCT(reg) != devid)
		return 0;

	return 1;
}

int
mmuagp_via_match(const struct pci_attach_args *pa)
{
	pcitag_t tag;
	pcireg_t reg;

	tag = AGP_AMD64_VIA_PCITAG(pa->pa_pc);

	reg = pci_conf_read(pa->pa_pc, tag, PCI_CLASS_REG);
	if (PCI_CLASS(reg) != PCI_CLASS_BRIDGE ||
	    PCI_SUBCLASS(reg) != PCI_SUBCLASS_BRIDGE_PCI)
		return 0;

	reg = pci_conf_read(pa->pa_pc, tag, PCI_ID_REG);
	if (PCI_VENDOR(reg) != PCI_VENDOR_VIATECH ||
	    PCI_PRODUCT(reg) != PCI_PRODUCT_VIATECH_K8HTB_AGP)
		return 0;

	return 1;
}

void
mmuagp_attach(struct device *parent, struct device *self, void *aux)
{
	struct mmuagp_softc	*msc = (struct mmuagp_softc *)self ;
	struct agp_attach_args	*aa = aux;
	struct pci_attach_args	*pa = aa->aa_pa;
	struct agp_gatt		*gatt;
	int			 (*set_aperture)(void *, bus_size_t) = NULL;
	pcireg_t		 id, attbase, apctrl;
	pcitag_t		 tag;
	int			 maxdevs, i, n;

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, AGP_APBASE,
	    PCI_MAPREG_TYPE_MEM, &msc->msc_apaddr, NULL, NULL) != 0) {
		printf(": can't get aperture info\n");
		return;
	}

	msc->msc_pc = pa->pa_pc;
	msc->msc_tag = pa->pa_tag;

	maxdevs = pci_bus_maxdevs(pa->pa_pc, 0);
	for (i = 0, n = 0; i < maxdevs && n < AMD64_MAX_MCTRL; i++) {
		tag = pci_make_tag(pa->pa_pc, 0, i, 3);
		id = pci_conf_read(pa->pa_pc, tag, PCI_ID_REG);
		if (PCI_VENDOR(id) == PCI_VENDOR_AMD &&
		    PCI_PRODUCT(id) ==  PCI_PRODUCT_AMD_0F_MISC) {
			msc->mctrl_tag[n] = tag;
			n++;
		}
	}
	if (n == 0) {
		printf(": no Miscellaneous Control units found\n");
		return;
	}
	msc->n_mctrl = n;

	printf(": %d Miscellaneous Control unit(s) found", msc->n_mctrl);

	msc->msc_apsize = mmuagp_get_aperture(msc);

	for (;;) {
		gatt = agp_alloc_gatt(pa->pa_dmat, msc->msc_apsize);
		if (gatt != NULL)
			break;

		/*
		 * Probably failed to alloc contiguous memory. Try reducing the
		 * aperture so that the gatt size reduces.
		 */
		msc->msc_apsize /= 2;
		if (mmuagp_set_aperture(msc, msc->msc_apsize)) {
			printf(" can't set aperture size\n");
			return;
		}
	}
	msc->gatt = gatt;

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_ALI:
		mmuagp_uli_init(msc);
		set_aperture = mmuagp_uli_set_aperture;
		break;

	case PCI_VENDOR_NVIDIA:
		msc->ctrl_tag = AGP_AMD64_NVIDIA_PCITAG(pa->pa_pc);
		mmuagp_nvidia_init(msc);
		set_aperture = mmuagp_nvidia_set_aperture;
		break;

	case PCI_VENDOR_VIATECH:
		/* do we have to set the extra bridge too? */
		if (mmuagp_via_match(pa)) {
			msc->ctrl_tag = AGP_AMD64_VIA_PCITAG(pa->pa_pc);
			mmuagp_via_init(msc);
			set_aperture = mmuagp_via_set_aperture;
		}
		break;
	}

	if (set_aperture != NULL) {
		if ((*set_aperture)(msc, msc->msc_apsize)) {
			printf(", failed aperture set\n");
			return;
		}
	}

	/* Install the gatt and enable aperture. */
	attbase = (uint32_t)(gatt->ag_physical >> 8) & AGP_AMD64_ATTBASE_MASK;
	for (i = 0; i < msc->n_mctrl; i++) {
		pci_conf_write(pa->pa_pc, msc->mctrl_tag[i], AGP_AMD64_ATTBASE,
		    attbase);
		apctrl = pci_conf_read(pa->pa_pc, msc->mctrl_tag[i],
		    AGP_AMD64_APCTRL);
		apctrl |= AGP_AMD64_APCTRL_GARTEN;
		apctrl &=
		    ~(AGP_AMD64_APCTRL_DISGARTCPU | AGP_AMD64_APCTRL_DISGARTIO);
		pci_conf_write(pa->pa_pc, msc->mctrl_tag[i], AGP_AMD64_APCTRL,
		    apctrl);
	}

	agp_flush_cache();

	msc->agpdev = (struct agp_softc *)agp_attach_bus(pa, &mmuagp_methods,
	    msc->msc_apaddr, msc->msc_apsize, &msc->dev);
	return;
}


static bus_size_t mmuagp_table[] = {
	0x02000000,	/*   32 MB */
	0x04000000,	/*   64 MB */
	0x08000000,	/*  128 MB */
	0x10000000,	/*  256 MB */
	0x20000000,	/*  512 MB */
	0x40000000,	/* 1024 MB */
	0x80000000,	/* 2048 MB */
};

#define AGP_AMD64_TABLE_SIZE \
	(sizeof(mmuagp_table) / sizeof(mmuagp_table[0]))

bus_size_t
mmuagp_get_aperture(void *sc)
{
	struct mmuagp_softc *msc = sc;
	uint32_t i;

	i = (pci_conf_read(msc->msc_pc, msc->mctrl_tag[0], AGP_AMD64_APCTRL) &
		AGP_AMD64_APCTRL_SIZE_MASK) >> 1;

	if (i >= AGP_AMD64_TABLE_SIZE)
		return 0;

	return mmuagp_table[i];
}

int
mmuagp_set_aperture(void *sc, bus_size_t aperture)
{
	struct mmuagp_softc	*msc = sc;
	uint32_t		 i;
	pcireg_t		 apctrl;
	int			 j;

	for (i = 0; i < AGP_AMD64_TABLE_SIZE; i++)
		if (mmuagp_table[i] == aperture)
			break;
	if (i >= AGP_AMD64_TABLE_SIZE)
		return (EINVAL);

	for (j = 0; j < msc->n_mctrl; j++) {
		apctrl = pci_conf_read(msc->msc_pc, msc->mctrl_tag[0],
		    AGP_AMD64_APCTRL);
		pci_conf_write(msc->msc_pc, msc->mctrl_tag[0], AGP_AMD64_APCTRL,
		    (apctrl & ~(AGP_AMD64_APCTRL_SIZE_MASK)) | (i << 1));
	}

	return (0);
}

void
mmuagp_bind_page(void *sc, bus_addr_t offset, paddr_t physical, int flags)
{
	struct mmuagp_softc	*msc = sc;

	msc->gatt->ag_virtual[(offset - msc->msc_apaddr) >> AGP_PAGE_SHIFT] =
	    (physical & 0xfffff000) | ((physical >> 28) & 0x00000ff0) | 3;
}

void
mmuagp_unbind_page(void *sc, bus_addr_t offset)
{
	struct mmuagp_softc *msc = sc;

	msc->gatt->ag_virtual[(offset - msc->msc_apaddr) >> AGP_PAGE_SHIFT] = 0;
}

void
mmuagp_flush_tlb(void *sc)
{
	struct mmuagp_softc	*msc = sc;
	pcireg_t		 cachectrl;
	int			 i;

	for (i = 0; i < msc->n_mctrl; i++) {
		cachectrl = pci_conf_read(msc->msc_pc, msc->mctrl_tag[i],
		    AGP_AMD64_CACHECTRL);
		pci_conf_write(msc->msc_pc, msc->mctrl_tag[i],
		    AGP_AMD64_CACHECTRL,
		    cachectrl | AGP_AMD64_CACHECTRL_INVGART);
	}
}

void
mmuagp_apbase_fixup(void *sc)
{
	struct mmuagp_softc	*msc = sc;
	uint32_t		 apbase;
	int			 i;

	apbase = pci_conf_read(msc->msc_pc, msc->msc_tag, AGP_APBASE);
	msc->apbase = PCI_MAPREG_MEM_ADDR(apbase);
	apbase = (msc->apbase >> 25) & AGP_AMD64_APBASE_MASK;
	for (i = 0; i < msc->n_mctrl; i++)
		pci_conf_write(msc->msc_pc, msc->mctrl_tag[i], AGP_AMD64_APBASE,
		    apbase);
}

void
mmuagp_uli_init(void *sc)
{
	struct mmuagp_softc *msc = sc;
	pcireg_t apbase;

	mmuagp_apbase_fixup(msc);
	apbase = pci_conf_read(msc->msc_pc, msc->msc_tag,
	    AGP_AMD64_ULI_APBASE);
	pci_conf_write(msc->msc_pc, msc->msc_tag, AGP_AMD64_ULI_APBASE,
	    (apbase & 0x0000000f) | msc->apbase);
	pci_conf_write(msc->msc_pc, msc->msc_tag, AGP_AMD64_ULI_HTT_FEATURE,
	    msc->apbase);
}

int
mmuagp_uli_set_aperture(void *sc, bus_size_t aperture)
{
	struct mmuagp_softc	*msc = sc;

	switch (aperture) {
	case 0x02000000:	/*  32 MB */
	case 0x04000000:	/*  64 MB */
	case 0x08000000:	/* 128 MB */
	case 0x10000000:	/* 256 MB */
		break;
	default:
		return EINVAL;
	}

	pci_conf_write(msc->msc_pc, msc->msc_tag, AGP_AMD64_ULI_ENU_SCR,
	    msc->apbase + aperture - 1);

	return 0;
}

void
mmuagp_nvidia_init(void *sc)
{
	struct mmuagp_softc	*msc = sc;
	pcireg_t		 apbase;

	mmuagp_apbase_fixup(msc);
	apbase = pci_conf_read(msc->msc_pc, msc->msc_tag,
	    AGP_AMD64_NVIDIA_0_APBASE);
	pci_conf_write(msc->msc_pc, msc->msc_tag, AGP_AMD64_NVIDIA_0_APBASE,
	    (apbase & 0x0000000f) | msc->apbase);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP_AMD64_NVIDIA_1_APBASE1,
	    msc->apbase);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP_AMD64_NVIDIA_1_APBASE2,
	    msc->apbase);
}

int
mmuagp_nvidia_set_aperture(void *sc, bus_size_t aperture)
{
	struct mmuagp_softc	*msc = sc;
	bus_size_t		 apsize;

	switch (aperture) {
	case 0x02000000:	/*  32 MB */
		apsize = 0x0f;
		break;
	case 0x04000000:	/*  64 MB */
		apsize = 0x0e;
		break;
	case 0x08000000:	/* 128 MB */
		apsize = 0x0c;
		break;
	case 0x10000000:	/* 256 MB */
		apsize = 0x08;
		break;
	case 0x20000000:	/* 512 MB */
		apsize = 0x00;
		break;
	default:
		return (EINVAL);
	}

	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP_AMD64_NVIDIA_1_APSIZE,
	    (pci_conf_read(msc->msc_pc, msc->ctrl_tag,
	    AGP_AMD64_NVIDIA_1_APSIZE) & 0xfffffff0) | apsize);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP_AMD64_NVIDIA_1_APLIMIT1,
	    msc->apbase + aperture - 1);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP_AMD64_NVIDIA_1_APLIMIT2,
	    msc->apbase + aperture - 1);

	return (0);
}

void
mmuagp_via_init(void *sc)
{
	struct mmuagp_softc	*msc = sc;

	mmuagp_apbase_fixup(sc);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP3_VIA_ATTBASE,
	    msc->gatt->ag_physical);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP3_VIA_GARTCTRL,
	    pci_conf_read(msc->msc_pc, msc->ctrl_tag, AGP3_VIA_ATTBASE) |
	        0x180);
}

int
mmuagp_via_set_aperture(void *sc, bus_size_t aperture)
{
	struct mmuagp_softc	*msc = sc;
	bus_size_t		 apsize;

	apsize = ((aperture - 1) >> 20) ^ 0xff;
	if ((((apsize ^ 0xff) << 20) | ((1 << 20) - 1)) + 1 != aperture)
		return (EINVAL);
	pci_conf_write(msc->msc_pc, msc->ctrl_tag, AGP3_VIA_APSIZE,
	    (pci_conf_read(msc->msc_pc, msc->ctrl_tag, AGP3_VIA_APSIZE) &
	        ~0xff) | apsize);

	return 0;
}
