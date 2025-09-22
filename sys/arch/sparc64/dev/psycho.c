/*	$OpenBSD: psycho.c,v 1.85 2025/06/28 11:34:21 miod Exp $	*/
/*	$NetBSD: psycho.c,v 1.39 2001/10/07 20:30:41 eeh Exp $	*/

/*
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Support for `psycho' and `psycho+' UPA to PCI bridge and 
 * UltraSPARC IIi and IIe `sabre' PCI controllers.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timetc.h>

#include <uvm/uvm_extern.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/psl.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/psychoreg.h>
#include <sparc64/dev/psychovar.h>
#include <sparc64/dev/starfire.h>
#include <sparc64/sparc64/cache.h>

#ifdef DEBUG
#define PDB_PROM	0x01
#define PDB_BUSMAP	0x02
#define PDB_INTR	0x04
#define PDB_CONF	0x08
int psycho_debug = ~0;
#define DPRINTF(l, s)   do { if (psycho_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

pci_chipset_tag_t psycho_alloc_chipset(struct psycho_pbm *, int,
    pci_chipset_tag_t);
void psycho_get_bus_range(int, int *);
void psycho_get_ranges(int, struct psycho_ranges **, int *);
void psycho_set_intr(struct psycho_softc *, int, void *, 
    u_int64_t *, u_int64_t *, const char *);
bus_space_tag_t psycho_alloc_bus_tag(struct psycho_pbm *,
    const char *, int, int, int);

/* Interrupt handlers */
int psycho_ue(void *);
int psycho_ce(void *);
int psycho_bus_a(void *);
int psycho_bus_b(void *);
int psycho_bus_error(struct psycho_softc *, int);
int psycho_powerfail(void *);
int psycho_wakeup(void *);

/* IOMMU support */
void psycho_iommu_init(struct psycho_softc *, int);

/*
 * bus space and bus dma support for UltraSPARC `psycho'.  note that most
 * of the bus dma support is provided by the iommu dvma controller.
 */
int psycho_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t,
    bus_size_t, int, bus_space_handle_t *);
paddr_t psycho_bus_mmap(bus_space_tag_t, bus_space_tag_t, bus_addr_t, off_t,
    int, int);
bus_addr_t psycho_bus_addr(bus_space_tag_t, bus_space_tag_t,
    bus_space_handle_t);
void *psycho_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int, int,
    int (*)(void *), void *, const char *);

int psycho_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
    bus_size_t, bus_size_t, int, bus_dmamap_t *);
void psycho_sabre_dvmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
    bus_size_t, bus_size_t, int);
void psycho_map_psycho(struct psycho_softc *, int, bus_addr_t, bus_size_t,
    bus_addr_t, bus_size_t);
int psycho_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
void psycho_identify_pbm(struct psycho_softc *sc, struct psycho_pbm *pp,
    struct pcibus_attach_args *pa);

int psycho_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t psycho_conf_read(pci_chipset_tag_t, pcitag_t, int);
void psycho_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);

/* base pci_chipset */
extern struct sparc_pci_chipset _sparc_pci_chipset;

u_int stick_get_timecount(struct timecounter *);

struct timecounter stick_timecounter = {
	.tc_get_timecount = stick_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = 0,
	.tc_name = "stick",
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = 0,
};

/*
 * autoconfiguration
 */
int	psycho_match(struct device *, void *, void *);
void	psycho_attach(struct device *, struct device *, void *);
int	psycho_print(void *aux, const char *p);


const struct cfattach psycho_ca = {
        sizeof(struct psycho_softc), psycho_match, psycho_attach
};

struct cfdriver psycho_cd = {
	NULL, "psycho", DV_DULL
};

/*
 * "sabre" is the UltraSPARC IIi onboard UPA to PCI bridge.  It manages a
 * single PCI bus and does not have a streaming buffer.  It often has an APB
 * (advanced PCI bridge) connected to it, which was designed specifically for
 * the IIi.  The APB lets the IIi handle two independent PCI buses, and
 * appears as two "simba"'s underneath the sabre.
 *
 * "psycho" and "psycho+" is a dual UPA to PCI bridge.  It sits on the UPA bus
 * and manages two PCI buses.  "psycho" has two 64-bit 33MHz buses, while
 * "psycho+" controls both a 64-bit 33MHz and a 64-bit 66MHz PCI bus.  You
 * will usually find a "psycho+" since I don't think the original "psycho"
 * ever shipped, and if it did it would be in the U30.  
 *
 * Each "psycho" PCI bus appears as a separate OFW node, but since they are
 * both part of the same IC, they only have a single register space.  As such,
 * they need to be configured together, even though the autoconfiguration will
 * attach them separately.
 *
 * On UltraIIi machines, "sabre" itself usually takes pci0, with "simba" often
 * as pci1 and pci2, although they have been implemented with other PCI bus
 * numbers on some machines.
 *
 * On UltraII machines, there can be any number of "psycho+" ICs, each
 * providing two PCI buses.  
 *
 *
 * XXXX The psycho/sabre node has an `interrupts' attribute.  They contain
 * the values of the following interrupts in this order:
 *
 * PCI Bus Error	(30)
 * DMA UE		(2e)
 * DMA CE		(2f)
 * Power Fail		(25)
 *
 * We really should attach handlers for each.
 *
 */
#define	ROM_PCI_NAME		"pci"

struct psycho_type {
	char *p_name;
	int p_type;
} psycho_types[] = {
	{ "SUNW,psycho",        PSYCHO_MODE_PSYCHO      },
	{ "pci108e,8000",       PSYCHO_MODE_PSYCHO      },
	{ "SUNW,sabre",         PSYCHO_MODE_SABRE       },
	{ "pci108e,a000",       PSYCHO_MODE_SABRE       },
	{ "pci108e,a001",       PSYCHO_MODE_SABRE       },
	{ "pci10cf,138f",	PSYCHO_MODE_CMU_CH	},
	{ "pci10cf,1390",	PSYCHO_MODE_CMU_CH	},
	{ NULL, 0 }
};

int
psycho_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	struct psycho_type *ptype;
	char *str;

	/* match on a name of "pci" and a sabre or a psycho */
	if (strcmp(ma->ma_name, ROM_PCI_NAME) != 0)
		return (0);

	for (ptype = psycho_types; ptype->p_name != NULL; ptype++) {
		str = getpropstring(ma->ma_node, "model");
		if (strcmp(str, ptype->p_name) == 0)
			return (1);
		str = getpropstring(ma->ma_node, "compatible");
		if (strcmp(str, ptype->p_name) == 0)
			return (1);
	}
	return (0);
}

/*
 * SUNW,psycho initialization ...
 *	- find the per-psycho registers
 *	- figure out the IGN.
 *	- find our partner psycho
 *	- configure ourselves
 *	- bus range, bus, 
 *	- get interrupt-map and interrupt-map-mask
 *	- setup the chipsets.
 *	- if we're the first of the pair, initialise the IOMMU, otherwise
 *	  just copy its tags and addresses.
 */
void
psycho_attach(struct device *parent, struct device *self, void *aux)
{
	struct psycho_softc *sc = (struct psycho_softc *)self;
	struct psycho_softc *osc = NULL;
	struct psycho_pbm *pp;
	struct pcibus_attach_args pba;
	struct mainbus_attach_args *ma = aux;
	u_int64_t csr;
	int psycho_br[2], n;
	struct psycho_type *ptype;
	char buf[32];
	u_int stick_rate;

	sc->sc_node = ma->ma_node;
	sc->sc_bustag = ma->ma_bustag;
	sc->sc_dmatag = ma->ma_dmatag;

	/*
	 * call the model-specific initialization routine.
	 */

	for (ptype = psycho_types; ptype->p_name != NULL; ptype++) {
		char *str;

		str = getpropstring(ma->ma_node, "model");
		if (strcmp(str, ptype->p_name) == 0)
			break;
		str = getpropstring(ma->ma_node, "compatible");
		if (strcmp(str, ptype->p_name) == 0)
			break;
	}
	if (ptype->p_name == NULL)
		panic("psycho_attach: unknown model?");
	sc->sc_mode = ptype->p_type;

	/*
	 * The psycho gets three register banks:
	 * (0) per-PBM configuration and status registers
	 * (1) per-PBM PCI configuration space, containing only the
	 *     PBM 256-byte PCI header
	 * (2) the shared psycho configuration registers (struct psychoreg)
	 *
	 * XXX use the prom address for the psycho registers?  we do so far.
	 */

	/* Register layouts are different.  stuupid. */
	if (sc->sc_mode == PSYCHO_MODE_PSYCHO ||
	    sc->sc_mode == PSYCHO_MODE_CMU_CH) {
		sc->sc_basepaddr = (paddr_t)ma->ma_reg[2].ur_paddr;

		if (ma->ma_naddress > 2) {
			psycho_map_psycho(sc, 0,
			    ma->ma_address[2], sizeof(struct psychoreg),
			    ma->ma_address[0], sizeof(struct pci_ctl));
		} else if (ma->ma_nreg > 2) {
			psycho_map_psycho(sc, 1,
			    ma->ma_reg[2].ur_paddr, ma->ma_reg[2].ur_len,
			    ma->ma_reg[0].ur_paddr, ma->ma_reg[0].ur_len);
		} else
			panic("psycho_attach: %d not enough registers",
			    ma->ma_nreg);
	} else {
		sc->sc_basepaddr = (paddr_t)ma->ma_reg[0].ur_paddr;

		if (ma->ma_naddress) {
			psycho_map_psycho(sc, 0,
			    ma->ma_address[0], sizeof(struct psychoreg),
			    ma->ma_address[0] +
				offsetof(struct psychoreg, psy_pcictl[0]),
			    sizeof(struct pci_ctl));
		} else if (ma->ma_nreg) {
			psycho_map_psycho(sc, 1,
			    ma->ma_reg[0].ur_paddr, ma->ma_reg[0].ur_len,
			    ma->ma_reg[0].ur_paddr +
				offsetof(struct psychoreg, psy_pcictl[0]),
			    sizeof(struct pci_ctl));
		} else
			panic("psycho_attach: %d not enough registers",
			    ma->ma_nreg);
	}

	csr = psycho_psychoreg_read(sc, psy_csr);
	sc->sc_ign = INTMAP_IGN; /* APB IGN is always 0x1f << 6 = 0x7c */
	if (sc->sc_mode == PSYCHO_MODE_PSYCHO ||
	    sc->sc_mode == PSYCHO_MODE_CMU_CH)
		sc->sc_ign = PSYCHO_GCSR_IGN(csr) << 6;

	printf(": %s, impl %d, version %d, ign %x\n", ptype->p_name,
	    PSYCHO_GCSR_IMPL(csr), PSYCHO_GCSR_VERS(csr), sc->sc_ign);

	/*
	 * Match other psycho's that are already configured against
	 * the base physical address. This will be the same for a
	 * pair of devices that share register space.
	 */
	for (n = 0; n < psycho_cd.cd_ndevs; n++) {
		struct psycho_softc *asc =
		    (struct psycho_softc *)psycho_cd.cd_devs[n];

		if (asc == NULL || asc == sc)
			/* This entry is not there or it is me */
			continue;

		if (asc->sc_basepaddr != sc->sc_basepaddr)
			/* This is an unrelated psycho */
			continue;

		/* Found partner */
		osc = asc;
		break;
	}

	/* Oh, dear.  OK, lets get started */

	/*
	 * Setup the PCI control register
	 */
	csr = psycho_pcictl_read(sc, pci_csr);
	csr |= PCICTL_MRLM | PCICTL_ARB_PARK | PCICTL_ERRINTEN |
	    PCICTL_4ENABLE;
	csr &= ~(PCICTL_SERR | PCICTL_CPU_PRIO | PCICTL_ARB_PRIO |
	    PCICTL_RTRYWAIT);
	psycho_pcictl_write(sc, pci_csr, csr);

	/*
	 * Allocate our psycho_pbm
	 */
	pp = sc->sc_psycho_this = malloc(sizeof *pp, M_DEVBUF,
		M_NOWAIT | M_ZERO);
	if (pp == NULL)
		panic("could not allocate psycho pbm");

	pp->pp_sc = sc;

	/* grab the psycho ranges */
	psycho_get_ranges(sc->sc_node, &pp->pp_range, &pp->pp_nrange);

	/* get the bus-range for the psycho */
	psycho_get_bus_range(sc->sc_node, psycho_br);

	bzero(&pba, sizeof(pba));
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = psycho_br[0];

	printf("%s: bus range %u-%u, PCI bus %d\n", sc->sc_dev.dv_xname,
	    psycho_br[0], psycho_br[1], psycho_br[0]);

	pp->pp_pcictl = sc->sc_pcictl;

	/* allocate our tags */
	pp->pp_memt = psycho_alloc_mem_tag(pp);
	pp->pp_iot = psycho_alloc_io_tag(pp);
	if (sc->sc_mode == PSYCHO_MODE_CMU_CH)
		pp->pp_dmat = ma->ma_dmatag;
	else
		pp->pp_dmat = psycho_alloc_dma_tag(pp);
	pp->pp_flags = (pp->pp_memt ? PCI_FLAGS_MEM_ENABLED : 0) |
	                (pp->pp_iot ? PCI_FLAGS_IO_ENABLED  : 0);

	/* allocate a chipset for this */
	pp->pp_pc = psycho_alloc_chipset(pp, sc->sc_node, &_sparc_pci_chipset);

	/* setup the rest of the psycho pbm */
	pba.pba_pc = pp->pp_pc;

	/*
	 * And finally, if we're a sabre or the first of a pair of psycho's to
	 * arrive here, start up the IOMMU and get a config space tag.
	 */

	if (osc == NULL) {
		uint64_t timeo;

		/* Initialize Starfire PC interrupt translation. */
		if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
		    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0)
			starfire_pc_ittrans_init(ma->ma_upaid);

		/*
		 * Establish handlers for interesting interrupts....
		 *
		 * XXX We need to remember these and remove this to support
		 * hotplug on the UPA/FHC bus.
		 *
		 * XXX Not all controllers have these, but installing them
		 * is better than trying to sort through this mess.
		 */
		psycho_set_intr(sc, 15, psycho_ue,
		    psycho_psychoreg_vaddr(sc, ue_int_map),
		    psycho_psychoreg_vaddr(sc, ue_clr_int), "ue");
		if (sc->sc_mode == PSYCHO_MODE_PSYCHO ||
		    sc->sc_mode == PSYCHO_MODE_SABRE) {
			psycho_set_intr(sc, 1, psycho_ce,
			    psycho_psychoreg_vaddr(sc, ce_int_map),
			    psycho_psychoreg_vaddr(sc, ce_clr_int), "ce");
			psycho_set_intr(sc, 15, psycho_bus_a,
			    psycho_psychoreg_vaddr(sc, pciaerr_int_map),
			    psycho_psychoreg_vaddr(sc, pciaerr_clr_int),
			    "bus_a");
		}
#if 0
		psycho_set_intr(sc, 15, psycho_powerfail,
		    psycho_psychoreg_vaddr(sc, power_int_map),
		    psycho_psychoreg_vaddr(sc, power_clr_int), "powerfail");
#endif
		if (sc->sc_mode == PSYCHO_MODE_PSYCHO ||
		    sc->sc_mode == PSYCHO_MODE_CMU_CH) {
			psycho_set_intr(sc, 15, psycho_bus_b,
			    psycho_psychoreg_vaddr(sc, pciberr_int_map),
			    psycho_psychoreg_vaddr(sc, pciberr_clr_int),
			    "bus_b");
		}
		if (sc->sc_mode == PSYCHO_MODE_PSYCHO) {
			psycho_set_intr(sc, 1, psycho_wakeup,
			    psycho_psychoreg_vaddr(sc, pwrmgt_int_map),
			    psycho_psychoreg_vaddr(sc, pwrmgt_clr_int),
			    "wakeup");
		}

		/*
		 * Apparently a number of machines with psycho and psycho+
		 * controllers have interrupt latency issues.  We'll try
		 * setting the interrupt retry timeout to 0xff which gives us
		 * a retry of 3-6 usec (which is what sysio is set to) for the
		 * moment, which seems to help alleviate this problem.
		 */
		timeo = psycho_psychoreg_read(sc, intr_retry_timer);
		if (timeo > 0xfff) {
#ifdef DEBUG
			printf("decreasing interrupt retry timeout "
			    "from %lx to 0xff\n", (long)timeo);
#endif
			psycho_psychoreg_write(sc, intr_retry_timer, 0xff);
		}

		/*
		 * Setup IOMMU and PCI configuration if we're the first
		 * of a pair of psycho's to arrive here.
		 *
		 * We should calculate a TSB size based on the amount of RAM,
		 * number of bus controllers, and number and type of child
		 * devices.
		 *
		 * For the moment, 32KB should be more than enough.
		 */
		sc->sc_is = malloc(sizeof(struct iommu_state),
			M_DEVBUF, M_NOWAIT | M_ZERO);
		if (sc->sc_is == NULL)
			panic("psycho_attach: malloc iommu_state");

		if (getproplen(sc->sc_node, "no-streaming-cache") < 0) {
			struct strbuf_ctl *sb = &pp->pp_sb;
			vaddr_t va = (vaddr_t)&pp->pp_flush[0x40];

			/*
			 * Initialize the strbuf_ctl.
			 *
			 * The flush sync buffer must be 64-byte aligned.
			 */

			sb->sb_flush = (void *)(va & ~0x3f);

			sb->sb_bustag = sc->sc_bustag;
			if (bus_space_subregion(sc->sc_bustag, sc->sc_pcictl,
			    offsetof(struct pci_ctl, pci_strbuf),
			    sizeof(struct iommu_strbuf),
			    &sb->sb_sb)) {
				printf("STC0 subregion failed\n");
				sb->sb_flush = 0;
			}
		}

		/* Point out iommu at the strbuf_ctl. */
		sc->sc_is->is_sb[0] = &pp->pp_sb;

		/* CMU-CH doesn't have an IOMMU. */
		if (sc->sc_mode != PSYCHO_MODE_CMU_CH) {
			printf("%s: ", sc->sc_dev.dv_xname);
			psycho_iommu_init(sc, 2);
		}

		sc->sc_configtag = psycho_alloc_config_tag(sc->sc_psycho_this);
		if (bus_space_map(sc->sc_configtag,
		    sc->sc_basepaddr, 0x01000000, 0, &sc->sc_configaddr))
			panic("can't map psycho PCI configuration space");
	} else {
		/* Just copy IOMMU state, config tag and address */
		sc->sc_is = osc->sc_is;
		sc->sc_configtag = osc->sc_configtag;
		sc->sc_configaddr = osc->sc_configaddr;

		if (getproplen(sc->sc_node, "no-streaming-cache") < 0) {
			struct strbuf_ctl *sb = &pp->pp_sb;
			vaddr_t va = (vaddr_t)&pp->pp_flush[0x40];

			/*
			 * Initialize the strbuf_ctl.
			 *
			 * The flush sync buffer must be 64-byte aligned.
			 */

			sb->sb_flush = (void *)(va & ~0x3f);

			sb->sb_bustag = sc->sc_bustag;
			if (bus_space_subregion(sc->sc_bustag, sc->sc_pcictl,
			    offsetof(struct pci_ctl, pci_strbuf),
			    sizeof(struct iommu_strbuf),
			    &sb->sb_sb)) {
				printf("STC1 subregion failed\n");
				sb->sb_flush = 0;
			}

			/* Point out iommu at the strbuf_ctl. */
			sc->sc_is->is_sb[1] = sb;
		}

		/* Point out iommu at the strbuf_ctl. */
		sc->sc_is->is_sb[1] = &pp->pp_sb;

		printf("%s: ", sc->sc_dev.dv_xname);
		printf("dvma map %x-%x", sc->sc_is->is_dvmabase,
		    sc->sc_is->is_dvmaend);
#ifdef DEBUG
		printf(", iotdb %llx-%llx",
		    (unsigned long long)sc->sc_is->is_ptsb,
		    (unsigned long long)(sc->sc_is->is_ptsb +
		    (PAGE_SIZE << sc->sc_is->is_tsbsize)));
#endif
		iommu_reset(sc->sc_is);
		printf("\n");
	}

	/*
	 * The UltraSPARC IIe has new STICK logic that provides a
	 * timebase counter that doesn't scale with processor
	 * frequency.  Use it to provide a timecounter.
	 */
	stick_rate = getpropint(findroot(), "stick-frequency", 0);
	if (stick_rate > 0 && sc->sc_mode == PSYCHO_MODE_SABRE) {
		stick_timecounter.tc_frequency = stick_rate;
		stick_timecounter.tc_priv = sc;
		tc_init(&stick_timecounter);
	}

	/*
	 * attach the pci.. note we pass PCI A tags, etc., for the sabre here.
	 */
	pba.pba_busname = "pci";
#if 0
	pba.pba_flags = sc->sc_psycho_this->pp_flags;
#endif
	pba.pba_dmat = sc->sc_psycho_this->pp_dmat;
	pba.pba_iot = sc->sc_psycho_this->pp_iot;
	pba.pba_memt = sc->sc_psycho_this->pp_memt;
	pba.pba_pc->bustag = sc->sc_configtag;
	pba.pba_pc->bushandle = sc->sc_configaddr;
	pba.pba_pc->conf_size = psycho_conf_size;
	pba.pba_pc->conf_read = psycho_conf_read;
	pba.pba_pc->conf_write = psycho_conf_write;
	pba.pba_pc->intr_map = psycho_intr_map;

	if (sc->sc_mode == PSYCHO_MODE_PSYCHO ||
	    sc->sc_mode == PSYCHO_MODE_CMU_CH)
		psycho_identify_pbm(sc, pp, &pba);
	else
		pp->pp_id = PSYCHO_PBM_UNKNOWN;

	config_found(self, &pba, psycho_print);
}

void
psycho_identify_pbm(struct psycho_softc *sc, struct psycho_pbm *pp,
    struct pcibus_attach_args *pa)
{
	vaddr_t pci_va = (vaddr_t)bus_space_vaddr(sc->sc_bustag, sc->sc_pcictl);
	paddr_t pci_pa;

	if (pmap_extract(pmap_kernel(), pci_va, &pci_pa) == 0)
	    pp->pp_id = PSYCHO_PBM_UNKNOWN;
	else switch(pci_pa & 0xffff) {
		case 0x2000:
			pp->pp_id = PSYCHO_PBM_A;
			break;
		case 0x4000:
			pp->pp_id = PSYCHO_PBM_B;
			break;
		default:
			pp->pp_id = PSYCHO_PBM_UNKNOWN;
			break;
	}
}

void
psycho_map_psycho(struct psycho_softc* sc, int do_map, bus_addr_t reg_addr,
    bus_size_t reg_size, bus_addr_t pci_addr, bus_size_t pci_size)
{
	if (do_map) {
		if (bus_space_map(sc->sc_bustag,
		    reg_addr, reg_size, 0, &sc->sc_regsh))
			panic("psycho_attach: cannot map regs");

		if (pci_addr >= reg_addr &&
		    pci_addr + pci_size <= reg_addr + reg_size) {
			if (bus_space_subregion(sc->sc_bustag, sc->sc_regsh,
			    pci_addr - reg_addr, pci_size, &sc->sc_pcictl))
				panic("psycho_map_psycho: map ctl");
		}
		else if (bus_space_map(sc->sc_bustag, pci_addr, pci_size,
		    0, &sc->sc_pcictl))
			panic("psycho_map_psycho: cannot map pci");
	} else {
		if (bus_space_map(sc->sc_bustag, reg_addr, reg_size,
		    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_regsh))
			panic("psycho_map_psycho: cannot map ctl");
		if (bus_space_map(sc->sc_bustag, pci_addr, pci_size,
		    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_pcictl))
			panic("psycho_map_psycho: cannot map pci");
	}
}

int
psycho_print(void *aux, const char *p)
{
	if (p == NULL)
		return (UNCONF);
	return (QUIET);
}

void
psycho_set_intr(struct psycho_softc *sc, int ipl, void *handler,
    u_int64_t *mapper, u_int64_t *clearer, const char *suffix)
{
	struct intrhand *ih;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ih == NULL)
		panic("couldn't malloc intrhand");
	ih->ih_arg = sc;
	ih->ih_map = mapper;
	ih->ih_clr = clearer;
	ih->ih_fun = handler;
	ih->ih_pil = ipl;
	ih->ih_number = INTVEC(*(ih->ih_map));
	snprintf(ih->ih_name, sizeof(ih->ih_name),
	    "%s:%s", sc->sc_dev.dv_xname, suffix);

	DPRINTF(PDB_INTR, (
	    "\ninstalling handler %p arg %p for %s with number %x pil %u",
	    ih->ih_fun, ih->ih_arg, sc->sc_dev.dv_xname, ih->ih_number,
	    ih->ih_pil));

	intr_establish(ih);
}

/*
 * PCI bus support
 */

/*
 * allocate a PCI chipset tag and set its cookie.
 */
pci_chipset_tag_t
psycho_alloc_chipset(struct psycho_pbm *pp, int node, pci_chipset_tag_t pc)
{
	pci_chipset_tag_t npc;
	
	npc = malloc(sizeof *npc, M_DEVBUF, M_NOWAIT);
	if (npc == NULL)
		panic("could not allocate pci_chipset_tag_t");
	memcpy(npc, pc, sizeof *pc);
	npc->cookie = pp;
	npc->rootnode = node;

	return (npc);
}

/*
 * grovel the OBP for various psycho properties
 */
void
psycho_get_bus_range(int node, int *brp)
{
	int n, error;

	error = getprop(node, "bus-range", sizeof(*brp), &n, (void **)&brp);
	if (error)
		panic("could not get psycho bus-range, error %d", error);
	if (n != 2)
		panic("broken psycho bus-range");
	DPRINTF(PDB_PROM,
	    ("psycho debug: got `bus-range' for node %08x: %u - %u\n",
	    node, brp[0], brp[1]));
}

void
psycho_get_ranges(int node, struct psycho_ranges **rp, int *np)
{

	if (getprop(node, "ranges", sizeof(**rp), np, (void **)rp))
		panic("could not get psycho ranges");
	DPRINTF(PDB_PROM,
	    ("psycho debug: got `ranges' for node %08x: %d entries\n",
	    node, *np));
}

/*
 * Interrupt handlers.
 */

int
psycho_ue(void *arg)
{
	struct psycho_softc *sc = arg;
	unsigned long long afsr = psycho_psychoreg_read(sc, psy_ue_afsr);
	unsigned long long afar = psycho_psychoreg_read(sc, psy_ue_afar);

	/*
	 * It's uncorrectable.  Dump the regs and panic.
	 */
	panic("%s: uncorrectable DMA error AFAR %llx (pa=%lx tte=%llx/%llx) "
	    "AFSR %llx", sc->sc_dev.dv_xname, afar,
	    iommu_extract(sc->sc_is, (vaddr_t)afar),
	    iommu_lookup_tte(sc->sc_is, (vaddr_t)afar),
	    iommu_fetch_tte(sc->sc_is, (paddr_t)afar),
	    afsr);
	return (1);
}

int 
psycho_ce(void *arg)
{
	struct psycho_softc *sc = arg;
	u_int64_t afar, afsr;

	/*
	 * It's correctable.  Dump the regs and continue.
	 */

	afar = psycho_psychoreg_read(sc, psy_ce_afar);
	afsr = psycho_psychoreg_read(sc, psy_ce_afsr);

	printf("%s: correctable DMA error AFAR %llx AFSR %llx\n",
	    sc->sc_dev.dv_xname, afar, afsr);

	/* Clear error. */
	psycho_psychoreg_write(sc, psy_ce_afsr,
	    afsr & (PSY_CEAFSR_PDRD | PSY_CEAFSR_PDWR |
	    PSY_CEAFSR_SDRD | PSY_CEAFSR_SDWR));
			       
	return (1);
}

int
psycho_bus_error(struct psycho_softc *sc, int bus)
{
	u_int64_t afsr, afar, bits;

	afar = psycho_psychoreg_read(sc, psy_pcictl[bus].pci_afar);
	afsr = psycho_psychoreg_read(sc, psy_pcictl[bus].pci_afsr);

	bits = afsr & (PSY_PCIAFSR_PMA | PSY_PCIAFSR_PTA | PSY_PCIAFSR_PTRY |
	    PSY_PCIAFSR_PPERR | PSY_PCIAFSR_SMA | PSY_PCIAFSR_STA |
	    PSY_PCIAFSR_STRY | PSY_PCIAFSR_SPERR);

	if (bits == 0)
		return (0);

	/*
	 * It's uncorrectable.  Dump the regs and panic.
	 */
	printf("%s: PCI bus %c error AFAR %llx (pa=%llx) AFSR %llx\n",
	    sc->sc_dev.dv_xname, 'A' + bus, (long long)afar,
	    (long long)iommu_extract(sc->sc_is, (vaddr_t)afar),
	    (long long)afsr);

	psycho_psychoreg_write(sc, psy_pcictl[bus].pci_afsr, bits);
	return (1);
}

int 
psycho_bus_a(void *arg)
{
	struct psycho_softc *sc = arg;

	return (psycho_bus_error(sc, 0));
}

int 
psycho_bus_b(void *arg)
{
	struct psycho_softc *sc = arg;

	return (psycho_bus_error(sc, 1));
}

int 
psycho_powerfail(void *arg)
{
	/*
	 * We lost power.  Try to shut down NOW.
	 */
	panic("Power Failure Detected");
	/* NOTREACHED */
	return (1);
}

int
psycho_wakeup(void *arg)
{
	struct psycho_softc *sc = arg;

	/*
	 * Gee, we don't really have a framework to deal with this
	 * properly.
	 */
	printf("%s: power management wakeup\n",	sc->sc_dev.dv_xname);
	return (1);
}

/*
 * initialise the IOMMU..
 */
void
psycho_iommu_init(struct psycho_softc *sc, int tsbsize)
{
	struct iommu_state *is = sc->sc_is;
	int *vdma = NULL, nitem;
	u_int32_t iobase = -1;
	char *name;

	/* punch in our copies */
	is->is_bustag = sc->sc_bustag;
	bus_space_subregion(sc->sc_bustag, sc->sc_regsh,
	    offsetof(struct psychoreg, psy_iommu), sizeof(struct iommureg),
	    &is->is_iommu);

	/*
	 * Separate the men from the boys.  If it has a `virtual-dma'
	 * property, use it.
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
		DPRINTF(PDB_CONF, ("psycho_iommu_init: iobase=0x%x\n", iobase));
		free(vdma, M_DEVBUF, 0);
	} else {
		DPRINTF(PDB_CONF, ("psycho_iommu_init: getprop failed, "
		    "iobase=0x%x, tsbsize=%d\n", iobase, tsbsize));
	}

	/* give us a nice name.. */
	name = (char *)malloc(32, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		panic("couldn't malloc iommu name");
	snprintf(name, 32, "%s dvma", sc->sc_dev.dv_xname);

	iommu_init(name, &iommu_hw_default, is, tsbsize, iobase);
}

/*
 * below here is bus space and bus dma support
 */

bus_space_tag_t
psycho_alloc_mem_tag(struct psycho_pbm *pp)
{
	return (psycho_alloc_bus_tag(pp, "mem",
	    0x02,	/* 32-bit mem space (where's the #define???) */
	    ASI_PRIMARY, ASI_PRIMARY_LITTLE));
}

bus_space_tag_t
psycho_alloc_io_tag(struct psycho_pbm *pp)
{
	return (psycho_alloc_bus_tag(pp, "io",
	    0x01,	/* IO space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
psycho_alloc_config_tag(struct psycho_pbm *pp)
{
	return (psycho_alloc_bus_tag(pp, "cfg",
	    0x00,	/* Config space (where's the #define???) */
	    ASI_PHYS_NON_CACHED_LITTLE, ASI_PHYS_NON_CACHED));
}

bus_space_tag_t
psycho_alloc_bus_tag(struct psycho_pbm *pp,
    const char *name, int ss, int asi, int sasi)
{
	struct psycho_softc *sc = pp->pp_sc;
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("could not allocate psycho bus tag");

	snprintf(bt->name, sizeof(bt->name), "%s-pbm_%s(%d-%2.2x)",
	    sc->sc_dev.dv_xname, name, ss, asi); 

	bt->cookie = pp;
	bt->parent = sc->sc_bustag;
	bt->default_type = ss;
	bt->asi = asi;
	bt->sasi = sasi;
	bt->sparc_bus_map = psycho_bus_map;
	bt->sparc_bus_mmap = psycho_bus_mmap;
	bt->sparc_bus_addr = psycho_bus_addr;
	bt->sparc_intr_establish = psycho_intr_establish;

	return (bt);
}

bus_dma_tag_t
psycho_alloc_dma_tag(struct psycho_pbm *pp)
{
	struct psycho_softc *sc = pp->pp_sc;
	bus_dma_tag_t dt, pdt = sc->sc_dmatag;

	dt = (bus_dma_tag_t)malloc(sizeof(struct sparc_bus_dma_tag),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dt == NULL)
		panic("could not allocate psycho dma tag");

	dt->_cookie = pp;
	dt->_parent = pdt;
	dt->_dmamap_create	= psycho_dmamap_create;
	dt->_dmamap_destroy	= iommu_dvmamap_destroy;
	dt->_dmamap_load	= iommu_dvmamap_load;
	dt->_dmamap_load_raw	= iommu_dvmamap_load_raw;
	dt->_dmamap_unload	= iommu_dvmamap_unload;
	if (sc->sc_mode == PSYCHO_MODE_PSYCHO)
		dt->_dmamap_sync = iommu_dvmamap_sync;
	else
		dt->_dmamap_sync = psycho_sabre_dvmamap_sync;
	dt->_dmamem_alloc	= iommu_dvmamem_alloc;
	dt->_dmamem_free	= iommu_dvmamem_free;

	return (dt);
}

/*
 * bus space support.  <sparc64/dev/psychoreg.h> has a discussion about
 * PCI physical addresses.
 */

int
psycho_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct psycho_pbm *pp = t->cookie;
	int i, ss;

	DPRINTF(PDB_BUSMAP, ("\npsycho_bus_map: type %d off %llx sz %llx "
	    "flags %d", t->default_type, (unsigned long long)offset,
	    (unsigned long long)size, flags));

	ss = t->default_type;
	DPRINTF(PDB_BUSMAP, (" cspace %d", ss));

	if (t->parent == 0 || t->parent->sparc_bus_map == 0) {
		printf("\npsycho_bus_map: invalid parent");
		return (EINVAL);
	}

	t = t->parent;

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	for (i = 0; i < pp->pp_nrange; i++) {
		bus_addr_t paddr;

		if (((pp->pp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pp->pp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pp->pp_range[i].phys_hi) << 32;
		DPRINTF(PDB_BUSMAP,
		    ("\n_psycho_bus_map: mapping paddr space %lx offset %lx "
			"paddr %llx",
		    (long)ss, (long)offset,
		    (unsigned long long)paddr));
		return ((*t->sparc_bus_map)(t, t0, paddr, size, flags, hp));
	}
	DPRINTF(PDB_BUSMAP, (" FAILED\n"));
	return (EINVAL);
}

paddr_t
psycho_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	bus_addr_t offset = paddr;
	struct psycho_pbm *pp = t->cookie;
	int i, ss;

	ss = t->default_type;

	DPRINTF(PDB_BUSMAP, ("\n_psycho_bus_mmap: prot %d flags %d pa %llx",
	    prot, flags, (unsigned long long)paddr));

	if (t->parent == 0 || t->parent->sparc_bus_mmap == 0) {
		printf("\npsycho_bus_mmap: invalid parent");
		return (-1);
	}

	t = t->parent;

	for (i = 0; i < pp->pp_nrange; i++) {
		bus_addr_t paddr;

		if (((pp->pp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pp->pp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pp->pp_range[i].phys_hi) << 32;
		DPRINTF(PDB_BUSMAP, ("\npsycho_bus_mmap: mapping paddr "
		    "space %lx offset %lx paddr %llx",
		    (long)ss, (long)offset,
		    (unsigned long long)paddr));
		return ((*t->sparc_bus_mmap)(t, t0, paddr, off, prot, flags));
	}

	return (-1);
}

bus_addr_t
psycho_bus_addr(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t h)
{
	struct psycho_pbm *pp = t->cookie;
	bus_addr_t addr;
	int i, ss;

	ss = t->default_type;

	if (t->parent == 0 || t->parent->sparc_bus_addr == 0) {
		printf("\npsycho_bus_addr: invalid parent");
		return (-1);
	}

	t = t->parent;

	addr = ((*t->sparc_bus_addr)(t, t0, h));
	if (addr == -1)
		return (-1);

	for (i = 0; i < pp->pp_nrange; i++) {
		if (((pp->pp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		return (BUS_ADDR_PADDR(addr) - pp->pp_range[i].phys_lo);
	}

	return (-1);
}

int
psycho_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	return PCI_CONFIG_SPACE_SIZE;
}

pcireg_t
psycho_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
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
psycho_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
        bus_space_write_4(pc->bustag, pc->bushandle,
	    PCITAG_OFFSET(tag) + reg, data);
}

/*
 * Bus-specific interrupt mapping
 */ 
int
psycho_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct psycho_pbm *pp = pa->pa_pc->cookie;
	struct psycho_softc *sc = pp->pp_sc;
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
	if (sc->sc_mode == PSYCHO_MODE_PSYCHO &&
	    pp->pp_id == PSYCHO_PBM_B)
		dev = PCITAG_DEV(pa->pa_intrtag) - 2;
	else
		dev = PCITAG_DEV(pa->pa_intrtag) - 1;

	*ihp = (pa->pa_intrpin - 1) & INTMAP_PCIINT;
	*ihp |= ((pp->pp_id == PSYCHO_PBM_B) ? INTMAP_PCIBUS : 0);
	*ihp |= (dev << 2) & INTMAP_PCISLOT;
	*ihp |= sc->sc_ign;

	return (0);
}

/*
 * install an interrupt handler for a PCI device
 */
void *
psycho_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct psycho_pbm *pp = t->cookie;
	struct psycho_softc *sc = pp->pp_sc;
	struct intrhand *ih;
	volatile u_int64_t *intrmapptr = NULL, *intrclrptr = NULL;
	int64_t intrmap = 0;
	int ino;
	long vec = INTVEC(ihandle); 

	/*
	 * Hunt through all the interrupt mapping regs to look for our
	 * interrupt vector.
	 *
	 * XXX We only compare INOs rather than IGNs since the firmware may
	 * not provide the IGN and the IGN is constant for all device on that
	 * PCI controller.  This could cause problems for the FFB/external
	 * interrupt which has a full vector that can be set arbitrarily.  
	 */

	DPRINTF(PDB_INTR,
	    ("\npsycho_intr_establish: ihandle %x vec %lx", ihandle, vec));
	ino = INTINO(vec);
	DPRINTF(PDB_INTR, (" ino %x", ino));

	/* If the device didn't ask for an IPL, use the one encoded. */
	if (level == IPL_NONE)
		level = INTLEV(vec);
	/* If it still has no level, print a warning and assign IPL 2 */
	if (level == IPL_NONE) {
		printf("ERROR: no IPL, setting IPL 2.\n");
		level = 2;
	}

	if (flags & BUS_INTR_ESTABLISH_SOFTINTR)
		goto found;

	DPRINTF(PDB_INTR,
	    ("\npsycho: intr %lx: %p\nHunting for IRQ...\n",
	    (long)ino, intrlev[ino]));

	/* 
	 * First look for PCI interrupts, otherwise the PCI A slot 0
	 * INTA# interrupt might match an unused non-PCI (obio)
	 * interrupt.
	 */

	for (intrmapptr = psycho_psychoreg_vaddr(sc, pcia_slot0_int),
	    intrclrptr = psycho_psychoreg_vaddr(sc, pcia0_clr_int[0]);
	    intrmapptr <= (volatile u_int64_t *)
		psycho_psychoreg_vaddr(sc, pcib_slot3_int);
	    intrmapptr++, intrclrptr += 4) {
		/* Skip PCI-A Slot 2 and PCI-A Slot 3 on psycho's */
		if (sc->sc_mode == PSYCHO_MODE_PSYCHO &&
		    (intrmapptr ==
			psycho_psychoreg_vaddr(sc, pcia_slot2_int) ||
		    intrmapptr ==
			psycho_psychoreg_vaddr(sc, pcia_slot3_int)))
			continue;

		if (((*intrmapptr ^ vec) & 0x3c) == 0) {
			intrclrptr += vec & 0x3;
			goto found;
		}
	}

	/* Now hunt through obio.  */
	for (intrmapptr = psycho_psychoreg_vaddr(sc, scsi_int_map),
	    intrclrptr = psycho_psychoreg_vaddr(sc, scsi_clr_int);
	    intrmapptr < (volatile u_int64_t *)
		psycho_psychoreg_vaddr(sc, ffb0_int_map);
	    intrmapptr++, intrclrptr++) {
		if (INTINO(*intrmapptr) == ino)
			goto found;
	}

	printf("Cannot find interrupt vector %lx\n", vec);
	return (NULL);

found:
	ih = bus_intr_allocate(t0, handler, arg, ino | sc->sc_ign, level,
	    intrmapptr, intrclrptr, what);
	if (ih == NULL) {
		printf("Cannot allocate interrupt vector %lx\n", vec);
		return (NULL);
	}

	DPRINTF(PDB_INTR, (
	    "\ninstalling handler %p arg %p with number %x pil %u",
	    ih->ih_fun, ih->ih_arg, ih->ih_number, ih->ih_pil));

	if (flags & BUS_INTR_ESTABLISH_MPSAFE)
		ih->ih_mpsafe = 1;

	intr_establish(ih);

	/*
	 * Enable the interrupt now we have the handler installed.
	 * Read the current value as we can't change it besides the
	 * valid bit so make sure only this bit is changed.
	 *
	 * XXXX --- we really should use bus_space for this.
	 */
	if (intrmapptr) {
		intrmap = *intrmapptr;
		DPRINTF(PDB_INTR, ("; read intrmap = %016llx",
			(unsigned long long)intrmap));

		/* Enable the interrupt */
		intrmap |= INTMAP_V;
		DPRINTF(PDB_INTR, ("; addr of intrmapptr = %p", intrmapptr));
		DPRINTF(PDB_INTR, ("; writing intrmap = %016llx",
			(unsigned long long)intrmap));
		*intrmapptr = intrmap;
		DPRINTF(PDB_INTR, ("; reread intrmap = %016llx",
			(unsigned long long)(intrmap = *intrmapptr)));
	}
	return (ih);
}

/*
 * hooks into the iommu dvma calls.
 */
int
psycho_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	struct psycho_pbm *pp = t->_cookie;

	return (iommu_dvmamap_create(t, t0, &pp->pp_sb, size, nsegments,
	    maxsegsz, boundary, flags, dmamp));
}

void
psycho_sabre_dvmamap_sync(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_size_t offset, bus_size_t len, int ops)
{
	struct psycho_pbm *pp = t->_cookie;
	struct psycho_softc *sc = pp->pp_sc;

	if (ops & BUS_DMASYNC_POSTREAD)
		psycho_psychoreg_read(sc, pci_dma_write_sync);

	if (ops & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE))
		__membar("#MemIssue");
}

u_int
stick_get_timecount(struct timecounter *tc)
{
	struct psycho_softc *sc = tc->tc_priv;

	return psycho_psychoreg_read(sc, stick_reg_low);
}
