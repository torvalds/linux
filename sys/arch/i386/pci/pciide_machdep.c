/*	$OpenBSD: pciide_machdep.c,v 1.8 2009/10/05 20:37:45 deraadt Exp $	*/
/*	$NetBSD: pciide_machdep.c,v 1.2 1999/02/19 18:01:27 mycroft Exp $	*/

/*-
 * Copyright (c) 2007 Juan Romero Pardines.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1998 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

/*
 * PCI IDE controller driver (i386 machine-dependent portion).
 *
 * Author: Christopher G. Demetriou, March 2, 1998 (derived from NetBSD
 * sys/dev/pci/ppb.c, revision 1.16).
 *
 * See "PCI IDE Controller Specification, Revision 1.0 3/4/94" from the
 * PCI SIG.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pciidereg.h>
#include <dev/pci/pciidevar.h>

#include <dev/isa/isavar.h>

#include <machine/cpufunc.h>
#include <i386/pci/pciide_gcsc_reg.h>

void gcsc_setup_channel(struct channel_softc *);

void *
pciide_machdep_compat_intr_establish(struct device *dev,
    struct pci_attach_args *pa, int chan, int (*func)(void *), void *arg)
{
	int irq;
	void *cookie;

	irq = PCIIDE_COMPAT_IRQ(chan);
	cookie = isa_intr_establish(NULL, irq, IST_EDGE, IPL_BIO, func, arg,
	    dev->dv_xname);

	return (cookie);
}

void
pciide_machdep_compat_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	isa_intr_disestablish(NULL, cookie);
}

void
gcsc_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface;
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}

	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 4;
	sc->sc_wdcdev.set_modes = gcsc_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 1;

	interface = PCI_INTERFACE(pa->pa_class);

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	cp = &sc->pciide_channels[0];

	if (pciide_chansetup(sc, 0, interface) == 0)
		return;

	pciide_map_compat_intr(pa, cp, 0, interface);
	if (cp->hw_ok == 0)
		return;

	pciide_mapchan(pa, cp, interface,
	    &cmdsize, &ctlsize, pciide_pci_intr);
	if (cp->hw_ok == 0) {
		pciide_unmap_compat_intr(pa, cp, 0, interface);
		return;
	}
	
	gcsc_setup_channel(&cp->wdc_channel);
}

void
gcsc_setup_channel(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct ata_drive_datas *drvp;
	uint64_t reg = 0;
	int drive, s;

	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		reg = rdmsr(drive ? GCSC_ATAC_CH0D1_DMA :
		    GCSC_ATAC_CH0D0_DMA);

		if (drvp->drive_flags & DRIVE_UDMA) {
			s = splbio();
			drvp->drive_flags &= ~DRIVE_DMA;
			splx(s);
			/* Enable the Ultra DMA mode bit */
			reg |= GCSC_ATAC_DMA_SEL;
			/* set the Ultra DMA mode */
			reg |= gcsc_udma_timings[drvp->UDMA_mode];

			wrmsr(drive ? GCSC_ATAC_CH0D1_DMA :
			    GCSC_ATAC_CH0D0_DMA, reg);

		} else if (drvp->drive_flags & DRIVE_DMA) {
			/* Enable the Multi-word DMA bit */
			reg &= ~GCSC_ATAC_DMA_SEL;
			/* set the Multi-word DMA mode */
			reg |= gcsc_mdma_timings[drvp->DMA_mode];

			wrmsr(drive ? GCSC_ATAC_CH0D1_DMA :
			    GCSC_ATAC_CH0D0_DMA, reg);
		}

		/* Always use PIO Format 1. */
		wrmsr(drive ? GCSC_ATAC_CH0D1_DMA :
		    GCSC_ATAC_CH0D0_DMA, reg | GCSC_ATAC_PIO_FORMAT);

		/* Set PIO mode */
		wrmsr(drive ? GCSC_ATAC_CH0D1_PIO : GCSC_ATAC_CH0D0_PIO,
		    gcsc_pio_timings[drvp->PIO_mode]);
	}

	pciide_print_modes(cp);
}
