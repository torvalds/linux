/*	$OpenBSD: iha_pci.c,v 1.21 2022/04/16 19:19:59 naddy Exp $ */
/*-------------------------------------------------------------------------
 *
 * Device driver for the INI-9XXXU/UW or INIC-940/950  PCI SCSI Controller.
 *
 * Written for 386bsd and FreeBSD by
 *	Winston Hung		<winstonh@initio.com>
 *
 * Copyright (c) 1997-1999 Initio Corp
 * Copyright (c) 2000-2002 Ken Westerback
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 *-------------------------------------------------------------------------
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/iha.h>

int  iha_pci_probe(struct device *, void *, void *);
void iha_pci_attach(struct device *, struct device *, void *);

const struct cfattach iha_pci_ca = {
	sizeof(struct iha_softc), iha_pci_probe, iha_pci_attach
};

struct cfdriver iha_cd = {
	NULL, "iha", DV_DULL
};

const struct scsi_adapter iha_switch = {
	iha_scsi_cmd, NULL, NULL, NULL, NULL
};

int
iha_pci_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INITIO)
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_INITIO_INIC940:
		case PCI_PRODUCT_INITIO_INIC941:
		case PCI_PRODUCT_INITIO_INIC950:
			return (1);
		}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DTCTECH)
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_DTCTECH_DMX3194U:
			return (1);
		}

	return (0);
}

void
iha_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct scsibus_attach_args saa;
	bus_space_handle_t ioh;
	pci_intr_handle_t ih;
	struct iha_softc *sc = (void *)self;
	bus_space_tag_t iot;
	const char *intrstr;
	int ioh_valid;

	/*
	 * XXX - Tried memory mapping (using code from adw and ahc)
	 *	 rather that IO mapping, but it didn't work at all..
	 */
	ioh_valid = pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, NULL, 0);

	if (ioh_valid != 0) {
		printf("%s: unable to map registers\n", sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_iot  = iot;
	sc->sc_ioh  = ioh;
	sc->sc_dmat = pa->pa_dmat;

	if (pci_intr_map(pa, &ih)) {
		printf("%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO, iha_intr, sc,
				       sc->sc_dev.dv_xname);

	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
	} else {
		if (intrstr != NULL)
			printf(": %s\n", intrstr);

		if (iha_init_tulip(sc) == 0) {
			saa.saa_adapter_softc    = sc;
			saa.saa_adapter	         = &iha_switch;
			saa.saa_adapter_target   = sc->sc_id;
			saa.saa_adapter_buswidth = sc->sc_maxtargets;
			saa.saa_luns		 = 8;
			saa.saa_openings	 = 4; /* # xs's allowed per device */
			saa.saa_pool             = &sc->sc_iopool;
			saa.saa_quirks = saa.saa_flags = 0;
			saa.saa_wwpn = saa.saa_wwnn = 0;

			config_found(&sc->sc_dev, &saa, scsiprint);
		}
	}
}
