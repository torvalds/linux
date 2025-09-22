/*	$OpenBSD: adw_pci.c,v 1.21 2024/05/24 06:02:53 jsg Exp $ */
/* $NetBSD: adw_pci.c,v 1.7 2000/05/26 15:13:46 dante Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Baldassare Dante Profeta <dante@mclink.it>
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
/*
 * Device probe and attach routines for the following
 * Advanced Systems Inc. SCSI controllers:
 *
 *      ABP-940UW	- Bus-Master PCI Ultra-Wide (253 CDB)
 *	ABP-940UW (68)	- Bus-Master PCI Ultra-Wide (253 CDB)
 *	ABP-940UWD	- Bus-Master PCI Ultra-Wide (253 CDB)
 *	ABP-970UW	- Bus-Master PCI Ultra-Wide (253 CDB)
 *	ASB-3940UW	- Bus-Master PCI Ultra-Wide (253 CDB)
 *	ASB-3940U2W-00	- Bus-Master PCI Ultra2-Wide (253 CDB)
 *	ASB-3940U3W-00	- Bus-Master PCI Ultra3-Wide (253 CDB)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/adwlib.h>
#include <dev/microcode/adw/adwmcode.h>
#include <dev/ic/adw.h>

/******************************************************************************/

#define PCI_BASEADR_IO        0x10

/******************************************************************************/

int adw_pci_match(struct device *, void *, void *);
void adw_pci_attach(struct device *, struct device *, void *);

const struct cfattach adw_pci_ca =
{
	sizeof(ADW_SOFTC), adw_pci_match, adw_pci_attach
};

const struct pci_matchid adw_pci_devices[] = {
	{ PCI_VENDOR_ADVSYS, PCI_PRODUCT_ADVSYS_WIDE },
	{ PCI_VENDOR_ADVSYS, PCI_PRODUCT_ADVSYS_U2W },
	{ PCI_VENDOR_ADVSYS, PCI_PRODUCT_ADVSYS_U3W },
};

/******************************************************************************/
/*
 * Check the slots looking for a board we recognise
 * If we find one, note its address (slot) and call
 * the actual probe routine to check it out.
 */
int
adw_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, adw_pci_devices,
	    nitems(adw_pci_devices)));
}


void
adw_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	ADW_SOFTC      *sc = (void *) self;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	pci_intr_handle_t ih;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t	command;
	const char     *intrstr;

	/*
	 * Set chip type
	 */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_ADVSYS_WIDE:
		sc->chip_type = ADW_CHIP_ASC3550;
		break;

	case PCI_PRODUCT_ADVSYS_U2W:
		sc->chip_type = ADW_CHIP_ASC38C0800;
		break;

	case PCI_PRODUCT_ADVSYS_U3W:
		sc->chip_type = ADW_CHIP_ASC38C1600;
		break;

	default:
		printf("\n%s: unknown model: %d\n", sc->sc_dev.dv_xname,
		       PCI_PRODUCT(pa->pa_id));
		return;
	}

	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	if ( (command & PCI_COMMAND_PARITY_ENABLE) == 0)
		sc->cfg.control_flag |= CONTROL_FLAG_IGNORE_PERR;

	/*
	 * Map Device Registers for I/O
	 */
	if (pci_mapreg_map(pa, PCI_BASEADR_IO, PCI_MAPREG_TYPE_IO, 0,
			   &iot, &ioh, NULL, NULL, 0)) {
		printf("\n%s: unable to map device registers\n",
		       sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_iot = iot;
	sc->sc_ioh = ioh;
	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Initialize the board
	 */
	if (adw_init(sc)) {
		printf("%s: adw_init failed", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Map Interrupt line
	 */
	if (pci_intr_map(pa, &ih)) {
		printf("\n%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pc, ih);

	/*
	 * Establish Interrupt handler
	 */
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_BIO, adw_intr, sc,
				       sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("\n%s: couldn't establish interrupt", sc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	/*
	 * Attach all the sub-devices we can find
	 */
	adw_attach(sc);
}
/******************************************************************************/
