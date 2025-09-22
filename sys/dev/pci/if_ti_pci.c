/*	$OpenBSD: if_ti_pci.c,v 1.8 2024/05/24 06:02:57 jsg Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_ti.c,v 1.25 2000/01/18 00:26:29 wpaul Exp $
 */

/*
 * Alteon Networks Tigon PCI gigabit ethernet driver for OpenBSD.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Alteon Networks Tigon chip contains an embedded R4000 CPU,
 * gigabit MAC, dual DMA channels and a PCI interface unit. NICs
 * using the Tigon may have anywhere from 512K to 2MB of SRAM. The
 * Tigon supports hardware IP, TCP and UCP checksumming, multicast
 * filtering and jumbo (9014 byte) frames. The hardware is largely
 * controlled by firmware, which must be loaded into the NIC during
 * initialization.
 *
 * The Tigon 2 contains 2 R4000 CPUs and requires a newer firmware
 * revision, which supports new features such as extended commands,
 * extended jumbo receive ring descriptors and a mini receive ring.
 *
 * Alteon Networks is to be commended for releasing such a vast amount
 * of development material for the Tigon NIC without requiring an NDA
 * (although they really should have done it a long time ago). With
 * any luck, the other vendors will finally wise up and follow Alteon's
 * stellar example.
 *
 * The following people deserve special thanks:
 * - Terry Murphy of 3Com, for providing a 3c985 Tigon 1 board
 *   for testing
 * - Raymond Lee of Netgear, for providing a pair of Netgear
 *   GA620 Tigon 2 boards for testing
 * - Ulf Zimmermann, for bringing the GA260 to my attention and
 *   convincing me to write this driver.
 * - Andrew Gallatin for providing FreeBSD/Alpha support.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/tireg.h>
#include <dev/ic/tivar.h>

int	ti_pci_match(struct device *, void *, void *);
void	ti_pci_attach(struct device *, struct device *, void *);

const struct cfattach ti_pci_ca = {
	sizeof(struct ti_softc), ti_pci_match, ti_pci_attach
};

const struct pci_matchid ti_devices[] = {
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_GA620 },
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_GA620T },
	{ PCI_VENDOR_ALTEON, PCI_PRODUCT_ALTEON_ACENIC },
	{ PCI_VENDOR_ALTEON, PCI_PRODUCT_ALTEON_ACENICT },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C985 },
	{ PCI_VENDOR_SGI, PCI_PRODUCT_SGI_TIGON },
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_PN9000SX }
};

int
ti_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, ti_devices, nitems(ti_devices)));
}

void
ti_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ti_softc *sc = (struct ti_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_size_t size;

	/*
	 * Map control/status registers.
	 */
	if (pci_mapreg_map(pa, TI_PCI_LOMEM,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->ti_btag, &sc->ti_bhandle, NULL, &size, 0)) {
 		printf(": can't map registers\n");
		return;
 	}

	sc->sc_dmatag = pa->pa_dmat;

	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		goto unmap;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->ti_intrhand = pci_intr_establish(pc, ih, IPL_NET, ti_intr, sc,
	    self->dv_xname);
	if (sc->ti_intrhand == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto unmap;
	}
	printf(": %s", intrstr);

	/*
	 * We really need a better way to tell a 1000baseTX card
	 * from a 1000baseSX one, since in theory there could be
	 * OEMed 1000baseTX cards from lame vendors who aren't
	 * clever enough to change the PCI ID. For the moment
	 * though, the AceNIC is the only copper card available.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ALTEON &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ALTEON_ACENICT)
		sc->ti_copper = 1;
	/* Ok, it's not the only copper card available */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_NETGEAR &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NETGEAR_GA620T)
		sc->ti_copper = 1;

	if (ti_attach(sc) == 0)
		return;

	pci_intr_disestablish(pc, sc->ti_intrhand);

unmap:
	bus_space_unmap(sc->ti_btag, sc->ti_bhandle, size);
}
