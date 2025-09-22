/*	$OpenBSD: trm_pci.c,v 1.12 2022/04/16 19:19:59 naddy Exp $
 * ------------------------------------------------------------
 *       O.S     : OpenBSD
 *    FILE NAME  : trm_pci.c
 *         BY    : Erich Chen     (erich@tekram.com.tw)
 *    Description: Device Driver for Tekram DC395U/UW/F,DC315/U
 *                 PCI SCSI Bus Master Host Adapter
 *                 (SCSI chip set used Tekram ASIC TRM-S1040)
 * (C)Copyright 1995-1999 Tekram Technology Co., Ltd.
 * (C)Copyright 2001-2002 Ashley R. Martens and Kenneth R. Westerback
 * ------------------------------------------------------------
 *    HISTORY:
 *
 *  REV#   DATE          NAME           DESCRIPTION
 *  1.00   05/01/99      ERICH CHEN     First released for NetBSD 1.4.x
 *  1.01   00/00/00      MARTIN AKESSON Port to OpenBSD 2.8
 *  1.02   Sep 19, 2001  ASHLEY MARTENS Cleanup and formatting
 * ------------------------------------------------------------
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/trm.h>

int	trm_pci_probe (struct device *, void *, void *);
void	trm_pci_attach (struct device *, struct device *, void *);

const struct cfattach trm_pci_ca = {
	sizeof(struct trm_softc),
	trm_pci_probe,
	trm_pci_attach,
	NULL,			/* Detach */
	NULL,			/* Activate */
};

struct  cfdriver trm_cd = {
        NULL, "trm", DV_DULL
};

const struct scsi_adapter trm_switch = {
	trm_scsi_cmd, NULL, NULL, NULL, NULL
};


/*
 * ------------------------------------------------------------
 * Function : trm_pci_probe
 * Purpose  : Check the slots looking for a board we recognize.
 *            If we find one, note ti's address (slot) and call
 *            the actual probe routine to check it out.
 * Inputs   :
 * ------------------------------------------------------------
 */
int
trm_pci_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_TEKRAM2) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_TEKRAM2_DC3X5U:
			return 1;
		}
	}
	return 0;
}

/*
 * ------------------------------------------------------------
 * Function : trm_pci_attach
 * Purpose  :
 * Inputs   :
 * ------------------------------------------------------------
 */
void
trm_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct scsibus_attach_args saa;
	bus_space_handle_t ioh;/* bus space handle */
	pci_intr_handle_t ih;
	struct trm_softc *sc = (void *)self;
	bus_space_tag_t iot;   /* bus space tag    */
	const char *intrstr;
	int unit;

	unit = sc->sc_device.dv_unit;

	if (PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_TEKRAM2_DC3X5U)
		return;

	/*
	 * mask for get correct base address of pci IO port
	 */
	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, NULL, 0) != 0) {
		printf("%s: unable to map registers\n", sc->sc_device.dv_xname);
		return;
	}

	/*
	 * test checksum of eeprom & initial "ACB" adapter control block
	 */
	sc->sc_iotag    = iot;
	sc->sc_iohandle = ioh;
	sc->sc_dmatag   = pa->pa_dmat;

	if (trm_init(sc, unit) != 0) {
		printf("%s: trm_init failed", sc->sc_device.dv_xname);
		return;
	}

	/*
	 *  Map and establish interrupt
	 */
	if (pci_intr_map(pa, &ih)) {
		printf("%s: couldn't map interrupt\n", sc->sc_device.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);

	if (pci_intr_establish(pa->pa_pc, ih, IPL_BIO, trm_Interrupt, sc,
	    sc->sc_device.dv_xname) == NULL) {
		printf("\n%s: couldn't establish interrupt", sc->sc_device.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
	} else {
		if (intrstr != NULL)
			printf(": %s\n", intrstr);

		saa.saa_adapter_softc    = sc;
		saa.saa_adapter_target   = sc->sc_AdaptSCSIID;
		saa.saa_adapter          = &trm_switch;
		saa.saa_adapter_buswidth = ((sc->sc_config & HCC_WIDE_CARD) == 0) ? 8:16;
		saa.saa_luns		 = 8;
		saa.saa_openings         = 30; /* So TagMask (32 bit integer) always has space */
		saa.saa_pool		 = &sc->sc_iopool;
		saa.saa_quirks = saa.saa_flags = 0;
		saa.saa_wwpn = saa.saa_wwnn = 0;

		config_found(&sc->sc_device, &saa, scsiprint);
	}
}
