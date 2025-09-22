/*	$OpenBSD: pchb.c,v 1.94 2023/01/30 10:49:05 jsg Exp $ */
/*	$NetBSD: pchb.c,v 1.65 2007/08/15 02:26:13 markd Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1996, 1998, 2000 The NetBSD Foundation, Inc.
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
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/agpvar.h>
#include <dev/pci/ppbreg.h>

#include <dev/ic/i82802reg.h>

#include "agp.h"

#define PCISET_INTEL_BRIDGETYPE_MASK	0x3
#define PCISET_INTEL_TYPE_COMPAT	0x1
#define PCISET_INTEL_TYPE_AUX		0x2

#define PCISET_INTEL_BUSCONFIG_REG	0x48
#define PCISET_INTEL_BRIDGE_NUMBER(reg)	(((reg) >> 8) & 0xff)
#define PCISET_INTEL_PCI_BUS_NUMBER(reg)	(((reg) >> 16) & 0xff)

#define PCISET_INTEL_SDRAMC_REG	0x74
#define PCISET_INTEL_SDRAMC_IPDLT	(1 << 24)

/* XXX should be in dev/ic/i82424{reg.var}.h */
#define I82424_CPU_BCTL_REG		0x53
#define I82424_PCI_BCTL_REG		0x54

#define I82424_BCTL_CPUMEM_POSTEN	0x01
#define I82424_BCTL_CPUPCI_POSTEN	0x02
#define I82424_BCTL_PCIMEM_BURSTEN	0x01
#define I82424_BCTL_PCI_BURSTEN		0x02

/* XXX should be in dev/ic/amd64htreg.h */
#define AMD64HT_LDT0_BUS	0x94
#define AMD64HT_LDT0_TYPE	0x98
#define AMD64HT_LDT1_BUS	0xb4
#define AMD64HT_LDT1_TYPE	0xb8
#define AMD64HT_LDT2_BUS	0xd4
#define AMD64HT_LDT2_TYPE	0xd8
#define AMD64HT_LDT3_BUS	0xf4
#define AMD64HT_LDT3_TYPE	0xf8

#define AMD64HT_NUM_LDT		4

#define AMD64HT_LDT_TYPE_MASK		0x0000001f
#define  AMD64HT_LDT_INIT_COMPLETE	0x00000002
#define  AMD64HT_LDT_NC			0x00000004

#define AMD64HT_LDT_SEC_BUS_NUM(reg)	(((reg) >> 8) & 0xff)

struct pchb_softc {
	struct device sc_dev;

	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_bh;

	/* rng stuff */
	int sc_rng_active;
	int sc_rng_ax;
	int sc_rng_i;
	struct timeout sc_rng_to;
};

int	pchbmatch(struct device *, void *, void *);
void	pchbattach(struct device *, struct device *, void *);
int	pchbactivate(struct device *, int);

const struct cfattach pchb_ca = {
	sizeof(struct pchb_softc), pchbmatch, pchbattach, NULL,
	pchbactivate
};

struct cfdriver pchb_cd = {
	NULL, "pchb", DV_DULL
};

int	pchb_print(void *, const char *);
void	pchb_rnd(void *);
void	pchb_amd64ht_attach(struct device *, struct pci_attach_args *, int);

int
pchbmatch(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

#ifdef __i386__
	/* XXX work around broken via82x866 chipsets */
	const struct pci_matchid via_devices[] = {
		{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C586_PWR },
		{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C596 },
		{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C596B_PM },
		{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C686A_SMB }
	};
	if (pci_matchbyid(pa, via_devices,
	    sizeof(via_devices) / sizeof(via_devices[0])))
		return (0);
#endif /* __i386__ */

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_HOST)
		return (1);

	return (0);
}

void
pchbattach(struct device *parent, struct device *self, void *aux)
{
	struct pchb_softc *sc = (struct pchb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct pcibus_attach_args pba;
	pcireg_t bcreg, bir;
	u_char bdnum, pbnum;
	pcitag_t tag;
	int i, r;
	int doattach = 0;

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_AMD:
		printf("\n");
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_AMD_0F_HT:
		case PCI_PRODUCT_AMD_10_HT:
			for (i = 0; i < AMD64HT_NUM_LDT; i++)
				pchb_amd64ht_attach(self, pa, i);
			break;
		}
		break;
#ifdef __i386__
	case PCI_VENDOR_RCC:
		{
			/*
			 * The variable below is a bit vector representing the
			 * Serverworks busses that have already been attached.
			 * Bit 0 represents bus 0 and so forth.  The initial
			 * value is 1 because we never actually want to
			 * attach bus 0 since bus 0 is the mainbus.
			 */
			static u_int32_t rcc_bus_visited = 1;

			printf("\n");
			bdnum = pci_conf_read(pa->pa_pc, pa->pa_tag, 0x44);
			if (bdnum >= (sizeof(rcc_bus_visited) * 8) ||
			    (rcc_bus_visited & (1 << bdnum)))
				break;

			rcc_bus_visited |= 1 << bdnum;

			/*
			 * This host bridge has a second PCI bus.
			 * Configure it.
			 */
			pbnum = bdnum;
			doattach = 1;
			break;
		}
#endif
	case PCI_VENDOR_INTEL:
		switch (PCI_PRODUCT(pa->pa_id)) {
#ifdef __i386__
		case PCI_PRODUCT_INTEL_82452_HB:
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag, 0x40);
			pbnum = PCISET_INTEL_BRIDGE_NUMBER(bcreg);
			if (pbnum != 0xff) {
				pbnum++;
				doattach = 1;
			}
			break;
		case PCI_PRODUCT_INTEL_82443BX_AGP:     /* 82443BX AGP (PAC) */
		case PCI_PRODUCT_INTEL_82443BX_NOAGP:   /* 82443BX Host-PCI (no AGP) */
			/*
			 * An incorrect address may be driven on the
			 * DRAM bus, resulting in memory data being
			 * fetched from the wrong location.  This is
			 * the workaround.
			 */
			if (PCI_REVISION(pa->pa_class) < 0x3) {
				bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
				    PCISET_INTEL_SDRAMC_REG);
				bcreg |= PCISET_INTEL_SDRAMC_IPDLT;
				pci_conf_write(pa->pa_pc, pa->pa_tag,
				    PCISET_INTEL_SDRAMC_REG, bcreg);
			}
			break;
		case PCI_PRODUCT_INTEL_PCI450_PB:
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    PCISET_INTEL_BUSCONFIG_REG);
			bdnum = PCISET_INTEL_BRIDGE_NUMBER(bcreg);
			pbnum = PCISET_INTEL_PCI_BUS_NUMBER(bcreg);
			switch (bdnum & PCISET_INTEL_BRIDGETYPE_MASK) {
			default:
				printf(": bdnum=%x (reserved)", bdnum);
				break;
			case PCISET_INTEL_TYPE_COMPAT:
				printf(": Compatibility PB (bus %d)", pbnum);
				break;
			case PCISET_INTEL_TYPE_AUX:
				printf(": Auxiliary PB (bus %d)", pbnum);
				doattach = 1;
			}
			break;
		case PCI_PRODUCT_INTEL_CDC:
			bcreg = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    I82424_CPU_BCTL_REG);
			if (bcreg & I82424_BCTL_CPUPCI_POSTEN) {
				bcreg &= ~I82424_BCTL_CPUPCI_POSTEN;
				pci_conf_write(pa->pa_pc, pa->pa_tag,
				    I82424_CPU_BCTL_REG, bcreg);
				printf(": disabled CPU-PCI write posting");
			}
			break;
		case PCI_PRODUCT_INTEL_82454NX:
			pbnum = 0;
			switch (pa->pa_device) {
			case 18: /* PXB 0 bus A - primary bus */
				break;
			case 19: /* PXB 0 bus B */
				/* read SUBA0 from MIOC */
				tag = pci_make_tag(pa->pa_pc, 0, 16, 0);
				bcreg = pci_conf_read(pa->pa_pc, tag, 0xd0);
				pbnum = ((bcreg & 0x0000ff00) >> 8) + 1;
				break;
			case 20: /* PXB 1 bus A */
				/* read BUSNO1 from MIOC */
				tag = pci_make_tag(pa->pa_pc, 0, 16, 0);
				bcreg = pci_conf_read(pa->pa_pc, tag, 0xd0);
				pbnum = (bcreg & 0xff000000) >> 24;
				break;
			case 21: /* PXB 1 bus B */
				/* read SUBA1 from MIOC */
				tag = pci_make_tag(pa->pa_pc, 0, 16, 0);
				bcreg = pci_conf_read(pa->pa_pc, tag, 0xd4);
				pbnum = (bcreg & 0x000000ff) + 1;
				break;
			}
			if (pbnum != 0)
				doattach = 1;
			break;
		/* RNG */
		case PCI_PRODUCT_INTEL_82810_HB:
		case PCI_PRODUCT_INTEL_82810_DC100_HB:
		case PCI_PRODUCT_INTEL_82810E_HB:
		case PCI_PRODUCT_INTEL_82815_HB:
		case PCI_PRODUCT_INTEL_82820_HB:
		case PCI_PRODUCT_INTEL_82840_HB:
		case PCI_PRODUCT_INTEL_82850_HB:
		case PCI_PRODUCT_INTEL_82860_HB:
#endif /* __i386__ */
		case PCI_PRODUCT_INTEL_82915G_HB:
		case PCI_PRODUCT_INTEL_82945G_HB:
		case PCI_PRODUCT_INTEL_82925X_HB:
		case PCI_PRODUCT_INTEL_82955X_HB:
			sc->sc_bt = pa->pa_memt;
			if (bus_space_map(sc->sc_bt, I82802_IOBASE,
			    I82802_IOSIZE, 0, &sc->sc_bh))
				break;

			/* probe and init rng */
			if (!(bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST) & I82802_RNG_HWST_PRESENT))
				break;

			/* enable RNG */
			bus_space_write_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST,
			    bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST) | I82802_RNG_HWST_ENABLE);

			/* see if we can read anything */
			for (i = 1000; i-- &&
			    !(bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_RNGST) & I82802_RNG_RNGST_DATAV); )
				DELAY(10);

			if (!(bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_RNGST) & I82802_RNG_RNGST_DATAV))
				break;

			r = bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_DATA);

			timeout_set(&sc->sc_rng_to, pchb_rnd, sc);
			sc->sc_rng_i = 4;
			pchb_rnd(sc);
			sc->sc_rng_active = 1;
			break;
		}
		printf("\n");
		break;
	case PCI_VENDOR_VIATECH:
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_VIATECH_VT8251_PCIE_0:
			/*
			 * Bump the host bridge into PCI-PCI bridge
			 * mode by clearing magic bit on the VLINK
			 * device.  This allows us to read the bus
			 * number for the PCI bus attached to this
			 * host bridge.
			 */
			tag = pci_make_tag(pa->pa_pc, 0, 17, 7);
			bcreg = pci_conf_read(pa->pa_pc, tag, 0xfc);
			bcreg &= ~0x00000004; /* XXX Magic */
			pci_conf_write(pa->pa_pc, tag, 0xfc, bcreg);

			bir = pci_conf_read(pa->pa_pc,
			    pa->pa_tag, PPB_REG_BUSINFO);
			pbnum = PPB_BUSINFO_PRIMARY(bir);
			if (pbnum > 0)
				doattach = 1;

			/* Switch back to host bridge mode. */
			bcreg |= 0x00000004; /* XXX Magic */
			pci_conf_write(pa->pa_pc, tag, 0xfc, bcreg);
			break;
		}
		printf("\n");
		break;
	default:
		printf("\n");
		break;
	}

#if NAGP > 0
	/*
	 * Intel IGD have an odd interface and attach at vga, however,
	 * in that mode they don't have the AGP cap bit, so this
	 * test should be sufficient
	 */
	if (pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_AGP,
	    NULL, NULL) != 0) {
		struct agp_attach_args	aa;
		aa.aa_busname = "agp";
		aa.aa_pa = pa;

		config_found(self, &aa, agpdev_print);
	}
#endif /* NAGP > 0 */

	if (doattach == 0)
		return;

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = pa->pa_iot;
	pba.pba_memt = pa->pa_memt;
	pba.pba_dmat = pa->pa_dmat;
	pba.pba_busex = pa->pa_busex;
	pba.pba_domain = pa->pa_domain;
	pba.pba_bus = pbnum;
	pba.pba_pc = pa->pa_pc;
	config_found(self, &pba, pchb_print);
}

int
pchbactivate(struct device *self, int act)
{
	struct pchb_softc *sc = (struct pchb_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		/* re-enable RNG, if we have it */
		if (sc->sc_rng_active)
			bus_space_write_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST,
			    bus_space_read_1(sc->sc_bt, sc->sc_bh,
			    I82802_RNG_HWST) | I82802_RNG_HWST_ENABLE);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}


int
pchb_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

/*
 * Should do FIPS testing as per:
 *	http://csrc.nist.gov/publications/fips/fips140-1/fips1401.pdf
 */
void
pchb_rnd(void *v)
{
	struct pchb_softc *sc = v;

	/*
	 * Don't wait for data to be ready. If it's not there, we'll check
	 * next time.
	 */
	if ((bus_space_read_1(sc->sc_bt, sc->sc_bh, I82802_RNG_RNGST) &
	    I82802_RNG_RNGST_DATAV)) {

		sc->sc_rng_ax = (sc->sc_rng_ax << 8) |
		    bus_space_read_1(sc->sc_bt, sc->sc_bh, I82802_RNG_DATA);

		if (!sc->sc_rng_i--) {
			sc->sc_rng_i = 4;
			enqueue_randomness(sc->sc_rng_ax);
		}
	}

	timeout_add(&sc->sc_rng_to, 1);
}

void
pchb_amd64ht_attach(struct device *self, struct pci_attach_args *pa, int i)
{
	struct pcibus_attach_args pba;
	pcireg_t type, bus;
	int reg;

	reg = AMD64HT_LDT0_TYPE + i * 0x20;
	type = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
	if ((type & AMD64HT_LDT_INIT_COMPLETE) == 0 ||
	    (type & AMD64HT_LDT_NC) == 0)
		return;

	reg = AMD64HT_LDT0_BUS + i * 0x20;
	bus = pci_conf_read(pa->pa_pc, pa->pa_tag, reg);
	if (AMD64HT_LDT_SEC_BUS_NUM(bus) > 0) {
		bzero(&pba, sizeof(pba));
		pba.pba_busname = "pci";
		pba.pba_iot = pa->pa_iot;
		pba.pba_memt = pa->pa_memt;
		pba.pba_dmat = pa->pa_dmat;
		pba.pba_busex = pa->pa_busex;
		pba.pba_domain = pa->pa_domain;
		pba.pba_bus = AMD64HT_LDT_SEC_BUS_NUM(bus);
		pba.pba_pc = pa->pa_pc;
		config_found(self, &pba, pchb_print);
	}
}
