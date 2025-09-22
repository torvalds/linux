/*	$OpenBSD: ahd_pci.c,v 1.30 2024/09/01 03:08:56 jsg Exp $	*/

/*
 * Copyright (c) 2004 Milos Urbanek, Kenneth R. Westerback & Marco Peereboom
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Product specific probe and attach routines for:
 *	aic7901 and aic7902 SCSI controllers
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <dev/ic/aic79xx_openbsd.h>
#include <dev/ic/aic79xx_inline.h>
#include <dev/ic/aic79xx.h>

#include <dev/pci/pcivar.h>

static inline uint64_t
ahd_compose_id(u_int device, u_int vendor, u_int subdevice, u_int subvendor)
{
	uint64_t id;

	id = subvendor
	   | (subdevice << 16)
	   | ((uint64_t)vendor << 32)
	   | ((uint64_t)device << 48);

	return (id);
}

#define ID_ALL_MASK			0xFFFFFFFFFFFFFFFFull
#define ID_ALL_IROC_MASK		0xFF7FFFFFFFFFFFFFull
#define ID_DEV_VENDOR_MASK		0xFFFFFFFF00000000ull
#define ID_9005_GENERIC_MASK		0xFFF0FFFF00000000ull
#define ID_9005_GENERIC_IROC_MASK	0xFF70FFFF00000000ull

#define ID_AIC7901			0x800F9005FFFF9005ull
#define ID_AHA_29320A			0x8000900500609005ull
#define ID_AHA_29320ALP			0x8017900500449005ull
#define ID_AHA_29320LPE			0x8017900500459005ull

#define ID_AIC7901A			0x801E9005FFFF9005ull
#define ID_AHA_29320LP			0x8014900500449005ull

#define ID_AIC7902			0x801F9005FFFF9005ull
#define ID_AIC7902_B			0x801D9005FFFF9005ull
#define ID_AHA_39320			0x8010900500409005ull
#define ID_AHA_29320			0x8012900500429005ull
#define ID_AHA_29320B			0x8013900500439005ull
#define ID_AHA_39320_B			0x8015900500409005ull
#define ID_AHA_39320_B_DELL		0x8015900501681028ull
#define ID_AHA_39320A			0x8016900500409005ull
#define ID_AHA_39320D			0x8011900500419005ull
#define ID_AHA_39320D_B			0x801C900500419005ull
#define ID_AHA_39320D_HP		0x8011900500AC0E11ull
#define ID_AHA_39320D_B_HP		0x801C900500AC0E11ull
#define ID_AIC7902_PCI_REV_A4		0x3
#define ID_AIC7902_PCI_REV_B0		0x10
#define SUBID_HP			0x0E11

#define DEVID_9005_HOSTRAID(id) ((id) & 0x80)

#define DEVID_9005_TYPE(id) ((id) & 0xF)
#define		DEVID_9005_TYPE_HBA		0x0	/* Standard Card */
#define		DEVID_9005_TYPE_HBA_2EXT	0x1	/* 2 External Ports */
#define		DEVID_9005_TYPE_MB		0xF	/* On Motherboard */

#define DEVID_9005_MFUNC(id) ((id) & 0x10)

#define DEVID_9005_PACKETIZED(id) ((id) & 0x8000)

#define SUBID_9005_TYPE(id) ((id) & 0xF)
#define		SUBID_9005_TYPE_HBA		0x0	/* Standard Card */
#define		SUBID_9005_TYPE_MB		0xF	/* On Motherboard */

#define SUBID_9005_AUTOTERM(id)	(((id) & 0x10) == 0)

#define SUBID_9005_LEGACYCONN_FUNC(id) ((id) & 0x20)

#define SUBID_9005_SEEPTYPE(id) ((id) & 0x0C0) >> 6)
#define		SUBID_9005_SEEPTYPE_NONE	0x0
#define		SUBID_9005_SEEPTYPE_4K		0x1

ahd_device_setup_t ahd_aic7901_setup;
ahd_device_setup_t ahd_aic7901A_setup;
ahd_device_setup_t ahd_aic7902_setup;
ahd_device_setup_t ahd_aic790X_setup;

const struct ahd_pci_identity ahd_pci_ident_table[] =
{
	/* aic7901 based controllers */
	{
		ID_AHA_29320A,
		ID_ALL_MASK,
		ahd_aic7901_setup
	},
	{
		ID_AHA_29320ALP,
		ID_ALL_MASK,
		ahd_aic7901_setup
	},
	{
		ID_AHA_29320LPE,
		ID_ALL_MASK,
		ahd_aic7901_setup
	},
	/* aic7901A based controllers */
	{
		ID_AHA_29320LP,
		ID_ALL_MASK,
		ahd_aic7901A_setup
	},
	/* aic7902 based controllers */	
	{
		ID_AHA_29320,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_29320B,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320_B,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320_B_DELL,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320A,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320D,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320D_HP,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320D_B,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	{
		ID_AHA_39320D_B_HP,
		ID_ALL_MASK,
		ahd_aic7902_setup
	},
	/* Generic chip probes for devices we don't know 'exactly' */
	{
		ID_AIC7901 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahd_aic7901_setup
	},
	{
		ID_AIC7901A & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahd_aic7901A_setup
	},
	{
		ID_AIC7902 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahd_aic7902_setup
	}
};

const u_int ahd_num_pci_devs = NUM_ELEMENTS(ahd_pci_ident_table);
		
#define			DEVCONFIG		0x40
#define			PCIXINITPAT		0x0000E000ul
#define			PCIXINIT_PCI33_66	0x0000E000ul
#define			PCIXINIT_PCIX50_66	0x0000C000ul
#define			PCIXINIT_PCIX66_100	0x0000A000ul
#define			PCIXINIT_PCIX100_133	0x00008000ul
#define	PCI_BUS_MODES_INDEX(devconfig)	\
	(((devconfig) & PCIXINITPAT) >> 13)

static const char *pci_bus_modes[] =
{
	"PCI bus mode unknown",
	"PCI bus mode unknown",
	"PCI bus mode unknown",
	"PCI bus mode unknown",
	"PCI-X 101-133MHz",
	"PCI-X 67-100MHz",
	"PCI-X 50-66MHz",
	"PCI 33 or 66MHz"
};

#define		TESTMODE	0x00000800ul
#define		IRDY_RST	0x00000200ul
#define		FRAME_RST	0x00000100ul
#define		PCI64BIT	0x00000080ul
#define		MRDCEN		0x00000040ul
#define		ENDIANSEL	0x00000020ul
#define		MIXQWENDIANEN	0x00000008ul
#define		DACEN		0x00000004ul
#define		STPWLEVEL	0x00000002ul
#define		QWENDIANSEL	0x00000001ul

#define	DEVCONFIG1		0x44
#define		PREQDIS		0x01

#define	CSIZE_LATTIME		0x0c
#define		CACHESIZE	0x000000fful
#define		LATTIME		0x0000ff00ul

int	ahd_pci_probe(struct device *, void *, void *);
void	ahd_pci_attach(struct device *, struct device *, void *);
int	ahd_activate(struct device *, int);

const struct cfattach ahd_pci_ca = {
        sizeof(struct ahd_softc), ahd_pci_probe, ahd_pci_attach,
	NULL, ahd_activate
};

int	ahd_check_extport(struct ahd_softc *ahd);
void	ahd_configure_termination(struct ahd_softc *ahd,
					  u_int adapter_control);
void	ahd_pci_split_intr(struct ahd_softc *ahd, u_int intstat);

const struct ahd_pci_identity *
ahd_find_pci_device(pcireg_t id, pcireg_t subid)
{
	const struct ahd_pci_identity *entry;
	u_int64_t full_id;
	u_int i;

	full_id = ahd_compose_id(PCI_PRODUCT(id), PCI_VENDOR(id),
	    PCI_PRODUCT(subid), PCI_VENDOR(subid));

	/*
	 * If we are configured to attach to HostRAID
	 * controllers, mask out the IROC/HostRAID bit
	 * in the
	 */
	if (ahd_attach_to_HostRAID_controllers)
		full_id &= ID_ALL_IROC_MASK;

	for (i = 0; i < ahd_num_pci_devs; i++) {
		entry = &ahd_pci_ident_table[i];
		if (entry->full_id == (full_id & entry->id_mask)) {
			return (entry);
		}
	}
	return (NULL);
}

int
ahd_pci_probe(struct device *parent, void *match, void *aux)
{
	const struct ahd_pci_identity *entry;
	struct pci_attach_args *pa = aux;
	pcireg_t subid;

	subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	entry = ahd_find_pci_device(pa->pa_id, subid);
	return entry != NULL ? 1 : 0;
}

void
ahd_pci_attach(struct device *parent, struct device *self, void *aux)
{
	const struct ahd_pci_identity *entry;
	struct pci_attach_args *pa = aux;
	struct ahd_softc *ahd = (void *)self;
	pci_intr_handle_t ih;
	const char *intrstr;
	pcireg_t devconfig, memtype, subid;
	uint16_t device, subvendor; 
	int error, ioh_valid, ioh2_valid, l, memh_valid;

	ahd->dev_softc = pa;
	ahd->parent_dmat = pa->pa_dmat;

	if (ahd_alloc(ahd, ahd->sc_dev.dv_xname) == NULL)
		return;

	subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	entry = ahd_find_pci_device(pa->pa_id, subid);
	if (entry == NULL)
		return;

	/*
	 * Record if this is a HostRAID board.
	 */
	device = PCI_PRODUCT(pa->pa_id);
	if (DEVID_9005_HOSTRAID(device))
		ahd->flags |= AHD_HOSTRAID_BOARD;

	/*
	 * Record if this is an HP board.
	 */
	subvendor = PCI_VENDOR(subid);
	if (subvendor == SUBID_HP)
		ahd->flags |= AHD_HP_BOARD;

	error = entry->setup(ahd, pa);
	if (error != 0)
		return;
	
	/* XXX ahc on sparc64 needs this twice */
	devconfig = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);
	
	if ((devconfig & PCIXINITPAT) == PCIXINIT_PCI33_66) {
		ahd->chip |= AHD_PCI;
		/* Disable PCIX workarounds when running in PCI mode. */
		ahd->bugs &= ~AHD_PCIX_BUG_MASK;
	} else {
		ahd->chip |= AHD_PCIX;
	}
	ahd->bus_description = pci_bus_modes[PCI_BUS_MODES_INDEX(devconfig)];

	memh_valid = ioh_valid = ioh2_valid = 0;

	if (!pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_PCIX,
	    &ahd->pcix_off, NULL)) {
		if (ahd->chip & AHD_PCIX)
			printf("%s: warning: can't find PCI-X capability\n",
			    ahd_name(ahd));
		ahd->chip &= ~AHD_PCIX;
		ahd->chip |= AHD_PCI;
		ahd->bugs &= ~AHD_PCIX_BUG_MASK;
	}

	/*
	 * Map PCI registers
	 */
	if ((ahd->bugs & AHD_PCIX_MMAPIO_BUG) == 0) {
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag,
		    AHD_PCI_MEMADDR);
		switch (memtype) {
		case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT:
		case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT:
			memh_valid = (pci_mapreg_map(pa, AHD_PCI_MEMADDR,
			    memtype, 0, &ahd->tags[0], &ahd->bshs[0], NULL,
			    NULL, 0) == 0);
			if (memh_valid) {
				ahd->tags[1] = ahd->tags[0];
				bus_space_subregion(ahd->tags[0], ahd->bshs[0],
				    /*offset*/0x100, /*size*/0x100,
				    &ahd->bshs[1]);
				if (ahd_pci_test_register_access(ahd) != 0)
					memh_valid = 0;
			}
			break;
		default:
			memh_valid = 0;
			printf("%s: unknown memory type: 0x%x\n",
			ahd_name(ahd), memtype);
			break;
		}

#ifdef AHD_DEBUG
		printf("%s: doing memory mapping tag0 %p, tag1 %p, shs0 "
		    "0x%lx, shs1 0x%lx\n", ahd_name(ahd), ahd->tags[0],
		    ahd->tags[1], ahd->bshs[0], ahd->bshs[1]);
#endif
	}

	if (!memh_valid) {
		/* First BAR */
		ioh_valid = (pci_mapreg_map(pa, AHD_PCI_IOADDR,
		    PCI_MAPREG_TYPE_IO, 0, &ahd->tags[0], &ahd->bshs[0], NULL,
		    NULL, 0) == 0);

		/* 2nd BAR */
		ioh2_valid = (pci_mapreg_map(pa, AHD_PCI_IOADDR1,
		    PCI_MAPREG_TYPE_IO, 0, &ahd->tags[1], &ahd->bshs[1], NULL,
		    NULL, 0) == 0);

#ifdef AHD_DEBUG
		printf("%s: doing io mapping tag0 %p, tag1 %p, shs0 0x%lx, "
		    "shs1 0x%lx\n", ahd_name(ahd), ahd->tags[0], ahd->tags[1],
		    ahd->bshs[0], ahd->bshs[1]);
#endif
	}

	if (memh_valid == 0 && (ioh_valid == 0 || ioh2_valid == 0)) {
		printf("%s: unable to map registers\n", ahd_name(ahd));
		return;
	}

	/*
	 * Set Power State D0.
	 */
	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Should we bother disabling 39Bit addressing
	 * based on installed memory?
	 */
	if (sizeof(bus_addr_t) > 4)
		ahd->flags |= AHD_39BIT_ADDRESSING;

	/*
	 * If we need to support high memory, enable dual
	 * address cycles.  This bit must be set to enable
	 * high address bit generation even if we are on a
	 * 64bit bus (PCI64BIT set in devconfig).
	 */
	if ((ahd->flags & (AHD_39BIT_ADDRESSING|AHD_64BIT_ADDRESSING)) != 0) {
		if (bootverbose)
			printf("%s: Enabling 39Bit Addressing\n",
			       ahd_name(ahd));
		devconfig = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);
		devconfig |= DACEN;
		pci_conf_write(pa->pa_pc, pa->pa_tag, DEVCONFIG, devconfig);
	}
	
	ahd_softc_init(ahd);

	/*
	 * Map the interrupts routines
	 */
	ahd->bus_intr = ahd_pci_intr;

	error = ahd_reset(ahd, /*reinit*/FALSE);
	if (error != 0) {
		ahd_free(ahd);
		return;
	}

	if (pci_intr_map(pa, &ih)) {
		printf("%s: couldn't map interrupt\n", ahd_name(ahd));
		ahd_free(ahd);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	ahd->ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
	    ahd_platform_intr, ahd, ahd->sc_dev.dv_xname);
	if (ahd->ih == NULL) {
		printf("%s: couldn't establish interrupt", ahd_name(ahd));
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		ahd_free(ahd);
		return;
	}
	if (intrstr != NULL)
		printf(": %s\n", intrstr);
	
	/* Get the size of the cache */
	ahd->pci_cachesize = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	ahd->pci_cachesize *= 4;

	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	/* See if we have a SEEPROM and perform auto-term */
	error = ahd_check_extport(ahd);
	if (error != 0)
		return;

	/* Core initialization */
	error = ahd_init(ahd);
	if (error != 0)
		return;

	ahd_list_lock(&l);
	/*
	 * Link this softc in with all other ahd instances.
	 */
	ahd_softc_insert(ahd);
	ahd_list_unlock(&l);

	/* complete the attach */
	ahd_attach(ahd);
}

int
ahd_activate(struct device *self, int act)
{
	int ret = 0;

	ret = config_activate_children(self, act);

	switch (act) {
	case DVACT_POWERDOWN:
		ahd_shutdown(self);
		break;
	}

	return (ret);
}

/*
 * Perform some simple tests that should catch situations where
 * our registers are invalidly mapped.
 */
int
ahd_pci_test_register_access(struct ahd_softc *ahd)
{
	const pci_chipset_tag_t pc = ahd->dev_softc->pa_pc;
	const pcitag_t tag = ahd->dev_softc->pa_tag;
	pcireg_t cmd;
	u_int	 targpcistat;
	pcireg_t pci_status1;
	int	 error;
	uint8_t	 hcntrl;

	error = EIO;

	/*
	 * Enable PCI error interrupt status, but suppress NMIs
	 * generated by SERR raised due to target aborts.
	 */
	cmd = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG,
	    cmd & ~PCI_COMMAND_SERR_ENABLE);

	/*
	 * First a simple test to see if any
	 * registers can be read.  Reading
	 * HCNTRL has no side effects and has
	 * at least one bit that is guaranteed to
	 * be zero so it is a good register to
	 * use for this test.
	 */
	hcntrl = ahd_inb(ahd, HCNTRL);
	if (hcntrl == 0xFF)
		goto fail;

	/*
	 * Next create a situation where write combining
	 * or read prefetching could be initiated by the
	 * CPU or host bridge.  Our device does not support
	 * either, so look for data corruption and/or flagged
	 * PCI errors.  First pause without causing another
	 * chip reset.
	 */
	hcntrl &= ~CHIPRST;
	ahd_outb(ahd, HCNTRL, hcntrl|PAUSE);
	while (ahd_is_paused(ahd) == 0)
		;

	/* Clear any PCI errors that occurred before our driver attached. */
	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	targpcistat = ahd_inb(ahd, TARGPCISTAT);
	ahd_outb(ahd, TARGPCISTAT, targpcistat);
	pci_status1 = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, pci_status1);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	ahd_outb(ahd, CLRINT, CLRPCIINT);

	ahd_outb(ahd, SEQCTL0, PERRORDIS);
	ahd_outl(ahd, SRAM_BASE, 0x5aa555aa);
	if (ahd_inl(ahd, SRAM_BASE) != 0x5aa555aa)
		goto fail;

	if ((ahd_inb(ahd, INTSTAT) & PCIINT) != 0) {
		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		targpcistat = ahd_inb(ahd, TARGPCISTAT);
		if ((targpcistat & STA) != 0)
			goto fail;
	}

	error = 0;

fail:
	if ((ahd_inb(ahd, INTSTAT) & PCIINT) != 0) {

		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		targpcistat = ahd_inb(ahd, TARGPCISTAT);

		/* Silently clear any latched errors. */
		ahd_outb(ahd, TARGPCISTAT, targpcistat);
		pci_status1 = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, pci_status1);
		ahd_outb(ahd, CLRINT, CLRPCIINT);
	}
	ahd_outb(ahd, SEQCTL0, PERRORDIS|FAILDIS);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, cmd);
	return (error);
}

/*
 * Check the external port logic for a serial eeprom
 * and termination/cable detection controls.
 */
int
ahd_check_extport(struct ahd_softc *ahd)
{
	struct	vpd_config vpd;
	struct	seeprom_config *sc;
	u_int	adapter_control;
	int	have_seeprom;
	int	error;

	sc = ahd->seep_config;
	have_seeprom = ahd_acquire_seeprom(ahd);
	if (have_seeprom) {
		u_int start_addr;

		/*
		 * Fetch VPD for this function and parse it.
		 */
		if (bootverbose) 
			printf("%s: Reading VPD from SEEPROM...",
			       ahd_name(ahd));

		/* Address is always in units of 16bit words */
		start_addr = ((2 * sizeof(*sc))
			    + (sizeof(vpd) * (ahd->channel - 'A'))) / 2;

		error = ahd_read_seeprom(ahd, (uint16_t *)&vpd,
					 start_addr, sizeof(vpd)/2,
					 /*bytestream*/TRUE);
		if (error == 0)
			error = ahd_parse_vpddata(ahd, &vpd);
		if (bootverbose) 
			printf("%s: VPD parsing %s\n",
			       ahd_name(ahd),
			       error == 0 ? "successful" : "failed");

		if (bootverbose) 
			printf("%s: Reading SEEPROM...", ahd_name(ahd));

		/* Address is always in units of 16bit words */
		start_addr = (sizeof(*sc) / 2) * (ahd->channel - 'A');

		error = ahd_read_seeprom(ahd, (uint16_t *)sc,
					 start_addr, sizeof(*sc)/2,
					 /*bytestream*/FALSE);

		if (error != 0) {
			printf("Unable to read SEEPROM\n");
			have_seeprom = 0;
		} else {
			have_seeprom = ahd_verify_cksum(sc);

			if (bootverbose) {
				if (have_seeprom == 0)
					printf ("checksum error\n");
				else
					printf ("done.\n");
			}
		}
		ahd_release_seeprom(ahd);
	}

	if (!have_seeprom) {
		u_int	  nvram_scb;

		/*
		 * Pull scratch ram settings and treat them as
		 * if they are the contents of an seeprom if
		 * the 'ADPT', 'BIOS', or 'ASPI' signature is found
		 * in SCB 0xFF.  We manually compose the data as 16bit
		 * values to avoid endian issues.
		 */
		ahd_set_scbptr(ahd, 0xFF);
		nvram_scb = ahd_inb_scbram(ahd, SCB_BASE + NVRAM_SCB_OFFSET);
		if (nvram_scb != 0xFF
		 && ((ahd_inb_scbram(ahd, SCB_BASE + 0) == 'A'
		   && ahd_inb_scbram(ahd, SCB_BASE + 1) == 'D'
		   && ahd_inb_scbram(ahd, SCB_BASE + 2) == 'P'
		   && ahd_inb_scbram(ahd, SCB_BASE + 3) == 'T')
		  || (ahd_inb_scbram(ahd, SCB_BASE + 0) == 'B'
		   && ahd_inb_scbram(ahd, SCB_BASE + 1) == 'I'
		   && ahd_inb_scbram(ahd, SCB_BASE + 2) == 'O'
		   && ahd_inb_scbram(ahd, SCB_BASE + 3) == 'S')
		  || (ahd_inb_scbram(ahd, SCB_BASE + 0) == 'A'
		   && ahd_inb_scbram(ahd, SCB_BASE + 1) == 'S'
		   && ahd_inb_scbram(ahd, SCB_BASE + 2) == 'P'
		   && ahd_inb_scbram(ahd, SCB_BASE + 3) == 'I'))) {
			uint16_t *sc_data;
			int	  i;

			ahd_set_scbptr(ahd, nvram_scb);
			sc_data = (uint16_t *)sc;
			for (i = 0; i < 64; i += 2)
				*sc_data++ = ahd_inw_scbram(ahd, SCB_BASE+i);
			have_seeprom = ahd_verify_cksum(sc);
			if (have_seeprom)
				ahd->flags |= AHD_SCB_CONFIG_USED;
		}
	}

#ifdef AHD_DEBUG
	if (have_seeprom != 0
	 && (ahd_debug & AHD_DUMP_SEEPROM) != 0) {
		uint16_t *sc_data;
		int	  i;

		printf("%s: Seeprom Contents:", ahd_name(ahd));
		sc_data = (uint16_t *)sc;
		for (i = 0; i < (sizeof(*sc)); i += 2)
			printf("\n\t0x%.4x", sc_data[i]);
		printf("\n");
	}
#endif

	if (!have_seeprom) {
		if (bootverbose)
			printf("%s: No SEEPROM available.\n", ahd_name(ahd));
		ahd->flags |= AHD_USEDEFAULTS;
		error = ahd_default_config(ahd);
		adapter_control = CFAUTOTERM|CFSEAUTOTERM;
		free(ahd->seep_config, M_DEVBUF, 0);
		ahd->seep_config = NULL;
	} else {
		error = ahd_parse_cfgdata(ahd, sc);
		adapter_control = sc->adapter_control;
	}
	if (error != 0)
		return (error);

	ahd_configure_termination(ahd, adapter_control);

	return (0);
}

void
ahd_configure_termination(struct ahd_softc *ahd, u_int adapter_control)
{
	const pci_chipset_tag_t pc = ahd->dev_softc->pa_pc;
	const pcitag_t tag = ahd->dev_softc->pa_tag;
	int	 error;
	u_int	 sxfrctl1;
	uint8_t	 termctl;
	pcireg_t devconfig;

	devconfig = pci_conf_read(pc, tag, DEVCONFIG);
	devconfig &= ~STPWLEVEL;
	if ((ahd->flags & AHD_STPWLEVEL_A) != 0)
		devconfig |= STPWLEVEL;
	if (bootverbose)
		printf("%s: STPWLEVEL is %s\n",
		       ahd_name(ahd), (devconfig & STPWLEVEL) ? "on" : "off");
	pci_conf_write(pc, tag, DEVCONFIG, devconfig);

	/* Make sure current sensing is off. */
	if ((ahd->flags & AHD_CURRENT_SENSING) != 0) {
		(void)ahd_write_flexport(ahd, FLXADDR_ROMSTAT_CURSENSECTL, 0);
	}

	/*
	 * Read to sense.  Write to set.
	 */
	error = ahd_read_flexport(ahd, FLXADDR_TERMCTL, &termctl);
	if ((adapter_control & CFAUTOTERM) == 0) {
		if (bootverbose)
			printf("%s: Manual Primary Termination\n",
			       ahd_name(ahd));
		termctl &= ~(FLX_TERMCTL_ENPRILOW|FLX_TERMCTL_ENPRIHIGH);
		if ((adapter_control & CFSTERM) != 0)
			termctl |= FLX_TERMCTL_ENPRILOW;
		if ((adapter_control & CFWSTERM) != 0)
			termctl |= FLX_TERMCTL_ENPRIHIGH;
	} else if (error != 0) {
		printf("%s: Primary Auto-Term Sensing failed! "
		       "Using Defaults.\n", ahd_name(ahd));
		termctl = FLX_TERMCTL_ENPRILOW|FLX_TERMCTL_ENPRIHIGH;
	}

	if ((adapter_control & CFSEAUTOTERM) == 0) {
		if (bootverbose)
			printf("%s: Manual Secondary Termination\n",
			       ahd_name(ahd));
		termctl &= ~(FLX_TERMCTL_ENSECLOW|FLX_TERMCTL_ENSECHIGH);
		if ((adapter_control & CFSELOWTERM) != 0)
			termctl |= FLX_TERMCTL_ENSECLOW;
		if ((adapter_control & CFSEHIGHTERM) != 0)
			termctl |= FLX_TERMCTL_ENSECHIGH;
	} else if (error != 0) {
		printf("%s: Secondary Auto-Term Sensing failed! "
		       "Using Defaults.\n", ahd_name(ahd));
		termctl |= FLX_TERMCTL_ENSECLOW|FLX_TERMCTL_ENSECHIGH;
	}

	/*
	 * Now set the termination based on what we found.
	 */
	sxfrctl1 = ahd_inb(ahd, SXFRCTL1) & ~STPWEN;
	ahd->flags &= ~AHD_TERM_ENB_A;
	if ((termctl & FLX_TERMCTL_ENPRILOW) != 0) {
		ahd->flags |= AHD_TERM_ENB_A;
		sxfrctl1 |= STPWEN;
	}
	/* Must set the latch once in order to be effective. */
	ahd_outb(ahd, SXFRCTL1, sxfrctl1|STPWEN);
	ahd_outb(ahd, SXFRCTL1, sxfrctl1);

	error = ahd_write_flexport(ahd, FLXADDR_TERMCTL, termctl);
	if (error != 0) {
		printf("%s: Unable to set termination settings!\n",
		       ahd_name(ahd));
	} else if (bootverbose) {
		printf("%s: Primary High byte termination %sabled\n",
		       ahd_name(ahd),
		       (termctl & FLX_TERMCTL_ENPRIHIGH) ? "En" : "Dis");

		printf("%s: Primary Low byte termination %sabled\n",
		       ahd_name(ahd),
		       (termctl & FLX_TERMCTL_ENPRILOW) ? "En" : "Dis");

		printf("%s: Secondary High byte termination %sabled\n",
		       ahd_name(ahd),
		       (termctl & FLX_TERMCTL_ENSECHIGH) ? "En" : "Dis");

		printf("%s: Secondary Low byte termination %sabled\n",
		       ahd_name(ahd),
		       (termctl & FLX_TERMCTL_ENSECLOW) ? "En" : "Dis");
	}
	return;
}

#define	DPE	0x80
#define SSE	0x40
#define	RMA	0x20
#define	RTA	0x10
#define STA	0x08
#define DPR	0x01

static const char *split_status_source[] =
{
	"DFF0",
	"DFF1",
	"OVLY",
	"CMC",
};

static const char *pci_status_source[] =
{
	"DFF0",
	"DFF1",
	"SG",
	"CMC",
	"OVLY",
	"NONE",
	"MSI",
	"TARG"
};

static const char *split_status_strings[] =
{
	"Received split response",
	"Received split completion error message",
	"Receive overrun",
	"Count not complete",
	"Split completion data bucket",
	"Split completion address error",
	"Split completion byte count error",
	"Signaled Target-abort to early terminate a split"
};

static const char *pci_status_strings[] =
{
	"Data Parity Error has been reported via PERR#",
	"Target initial wait state error",
	"Split completion read data parity error",
	"Split completion address attribute parity error",
	"Received a Target Abort",
	"Received a Master Abort",
	"Signal System Error Detected",
	"Address or Write Phase Parity Error Detected"
};

void
ahd_pci_intr(struct ahd_softc *ahd)
{
	const pci_chipset_tag_t pc = ahd->dev_softc->pa_pc;
	const pcitag_t tag = ahd->dev_softc->pa_tag;
	uint8_t		pci_status[8];
	ahd_mode_state	saved_modes;
	pcireg_t	pci_status1;
	u_int		intstat;
	u_int		i;
	u_int		reg;
	
	intstat = ahd_inb(ahd, INTSTAT);

	if ((intstat & SPLTINT) != 0)
		ahd_pci_split_intr(ahd, intstat);

	if ((intstat & PCIINT) == 0)
		return;

	printf("%s: PCI error Interrupt\n", ahd_name(ahd));
	saved_modes = ahd_save_modes(ahd);
	ahd_dump_card_state(ahd);
	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	for (i = 0, reg = DF0PCISTAT; i < 8; i++, reg++) {

		if (i == 5)
			continue;
		pci_status[i] = ahd_inb(ahd, reg);
		/* Clear latched errors.  So our interrupt deasserts. */
		ahd_outb(ahd, reg, pci_status[i]);
	}

	for (i = 0; i < 8; i++) {
		u_int bit;
	
		if (i == 5)
			continue;

		for (bit = 0; bit < 8; bit++) {

			if ((pci_status[i] & (0x1 << bit)) != 0) {
				if (i == 7/*TARG*/ && bit == 3)
					printf("%s: Signaled Target Abort\n",
					    ahd_name(ahd));
				else
					printf("%s: %s in %s\n", ahd_name(ahd),
					    pci_status_strings[bit],
					    pci_status_source[i]);
			}
		}	
	}
	pci_status1 = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG , pci_status1);

	ahd_restore_modes(ahd, saved_modes);
	ahd_outb(ahd, CLRINT, CLRPCIINT);
	ahd_unpause(ahd);

	return;
}

void
ahd_pci_split_intr(struct ahd_softc *ahd, u_int intstat)
{
	const pci_chipset_tag_t pc = ahd->dev_softc->pa_pc;
	const pcitag_t tag = ahd->dev_softc->pa_tag;
	uint8_t		split_status[4];
	uint8_t		split_status1[4];
	uint8_t		sg_split_status[2];
	uint8_t		sg_split_status1[2];
	ahd_mode_state	saved_modes;
	u_int		i;
	pcireg_t	pcix_status;

	/*
	 * Check for splits in all modes.  Modes 0 and 1
	 * additionally have SG engine splits to look at.
	 */
	pcix_status = pci_conf_read(pc, tag, ahd->pcix_off + 0x04);
	printf("%s: PCI Split Interrupt - PCI-X status = 0x%x\n",
	       ahd_name(ahd), pcix_status);

	saved_modes = ahd_save_modes(ahd);
	for (i = 0; i < 4; i++) {
		ahd_set_modes(ahd, i, i);

		split_status[i] = ahd_inb(ahd, DCHSPLTSTAT0);
		split_status1[i] = ahd_inb(ahd, DCHSPLTSTAT1);
		/* Clear latched errors.  So our interrupt deasserts. */
		ahd_outb(ahd, DCHSPLTSTAT0, split_status[i]);
		ahd_outb(ahd, DCHSPLTSTAT1, split_status1[i]);
		if (i > 1)
			continue;
		sg_split_status[i] = ahd_inb(ahd, SGSPLTSTAT0);
		sg_split_status1[i] = ahd_inb(ahd, SGSPLTSTAT1);
		/* Clear latched errors.  So our interrupt deasserts. */
		ahd_outb(ahd, SGSPLTSTAT0, sg_split_status[i]);
		ahd_outb(ahd, SGSPLTSTAT1, sg_split_status1[i]);
	}

	for (i = 0; i < 4; i++) {
		u_int bit;

		for (bit = 0; bit < 8; bit++) {

			if ((split_status[i] & (0x1 << bit)) != 0) {
				printf("%s: %s in %s\n", ahd_name(ahd),
				    split_status_strings[bit],
				    split_status_source[i]);
			}

			if (i > 1)
				continue;

			if ((sg_split_status[i] & (0x1 << bit)) != 0) {
				printf("%s: %s in %s\n", ahd_name(ahd),
				    split_status_strings[bit], "SG");
			}
		}
	}
	/*
	 * Clear PCI-X status bits.
	 */
	pci_conf_write(pc, tag, ahd->pcix_off + 0x04, pcix_status);
	ahd_outb(ahd, CLRINT, CLRSPLTINT);
	ahd_restore_modes(ahd, saved_modes);
}

int
ahd_aic7901_setup(struct ahd_softc *ahd, struct pci_attach_args *pa)
{

	ahd->chip = AHD_AIC7901;
	ahd->features = AHD_AIC7901_FE;
	return (ahd_aic790X_setup(ahd, pa));
}

int
ahd_aic7901A_setup(struct ahd_softc *ahd, struct pci_attach_args *pa)
{

	ahd->chip = AHD_AIC7901A;
	ahd->features = AHD_AIC7901A_FE;
	return (ahd_aic790X_setup(ahd, pa));
}

int
ahd_aic7902_setup(struct ahd_softc *ahd, struct pci_attach_args *pa)
{
	ahd->chip = AHD_AIC7902;
	ahd->features = AHD_AIC7902_FE;
	return (ahd_aic790X_setup(ahd, pa));
}

int
ahd_aic790X_setup(struct ahd_softc *ahd, struct pci_attach_args *pa)
{
	u_int rev;

	rev = PCI_REVISION(pa->pa_class);
#ifdef AHD_DEBUG
	printf("\n%s: aic7902 chip revision 0x%x\n", ahd_name(ahd), rev);
#endif
	if (rev < ID_AIC7902_PCI_REV_A4) {
		printf("%s: Unable to attach to unsupported chip revision %d\n",
		       ahd_name(ahd), rev);
		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, 0);
		return (ENXIO);
	}

	ahd->channel = (pa->pa_function == 1) ? 'B' : 'A';
	if (rev < ID_AIC7902_PCI_REV_B0) {
		/*
		 * Enable A series workarounds.
		 */
		ahd->bugs |= AHD_SENT_SCB_UPDATE_BUG|AHD_ABORT_LQI_BUG
			  |  AHD_PKT_BITBUCKET_BUG|AHD_LONG_SETIMO_BUG
			  |  AHD_NLQICRC_DELAYED_BUG|AHD_SCSIRST_BUG
			  |  AHD_LQO_ATNO_BUG|AHD_AUTOFLUSH_BUG
			  |  AHD_CLRLQO_AUTOCLR_BUG|AHD_PCIX_MMAPIO_BUG
			  |  AHD_PCIX_CHIPRST_BUG|AHD_PCIX_SCBRAM_RD_BUG
			  |  AHD_PKTIZED_STATUS_BUG|AHD_PKT_LUN_BUG
			  |  AHD_MDFF_WSCBPTR_BUG|AHD_REG_SLOW_SETTLE_BUG
			  |  AHD_SET_MODE_BUG|AHD_BUSFREEREV_BUG
			  |  AHD_NONPACKFIFO_BUG|AHD_PACED_NEGTABLE_BUG
			  |  AHD_FAINT_LED_BUG;

		/*
		 * IO Cell parameter setup.
		 */
		AHD_SET_PRECOMP(ahd, AHD_PRECOMP_CUTBACK_29);

		if ((ahd->flags & AHD_HP_BOARD) == 0)
			AHD_SET_SLEWRATE(ahd, AHD_SLEWRATE_DEF_REVA);
	} else {
		pcireg_t devconfig1;

		ahd->features |= AHD_RTI|AHD_NEW_IOCELL_OPTS
			      |  AHD_NEW_DFCNTRL_OPTS|AHD_FAST_CDB_DELIVERY;
		ahd->bugs |= AHD_LQOOVERRUN_BUG|AHD_EARLY_REQ_BUG
		    	  |  AHD_BUSFREEREV_BUG;

		/*
		 * Some issues have been resolved in the 7901B.
		 */
		if ((ahd->features & AHD_MULTI_FUNC) != 0)
			ahd->bugs |= AHD_INTCOLLISION_BUG|AHD_ABORT_LQI_BUG;

		/*
		 * IO Cell parameter setup.
		 */
		AHD_SET_PRECOMP(ahd, AHD_PRECOMP_CUTBACK_29);
		AHD_SET_SLEWRATE(ahd, AHD_SLEWRATE_DEF_REVB);
		AHD_SET_AMPLITUDE(ahd, AHD_AMPLITUDE_DEF);

		/*
		 * Set the PREQDIS bit for H2B which disables some workaround
		 * that doesn't work on regular PCI busses.
		 * XXX - Find out exactly what this does from the hardware
		 * 	 folks!
		 */
		devconfig1 = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG1);
		pci_conf_write(pa->pa_pc, pa->pa_tag, DEVCONFIG1, devconfig1|PREQDIS);
		devconfig1 = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG1);
	}

	return (0);
}
