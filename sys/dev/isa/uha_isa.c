/*	$OpenBSD: uha_isa.c,v 1.15 2022/04/06 18:59:29 naddy Exp $	*/
/*	$NetBSD: uha_isa.c,v 1.5 1996/10/21 22:41:21 thorpej Exp $	*/

/*
 * Copyright (c) 1994, 1996 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/uhareg.h>
#include <dev/ic/uhavar.h>

#define	UHA_ISA_IOSIZE	16

int	uha_isa_probe(struct device *, void *, void *);
void	uha_isa_attach(struct device *, struct device *, void *);

const struct cfattach uha_isa_ca = {
	sizeof(struct uha_softc), uha_isa_probe, uha_isa_attach
};

#define KVTOPHYS(x)	vtophys((vaddr_t)(x))

int u14_find(bus_space_tag_t, bus_space_handle_t, struct uha_softc *);
void u14_start_mbox(struct uha_softc *, struct uha_mscp *);
int u14_poll(struct uha_softc *, struct scsi_xfer *, int);
int u14_intr(void *);
void u14_init(struct uha_softc *);

/*
 * Check the slots looking for a board we recognise
 * If we find one, note its address (slot) and call
 * the actual probe routine to check it out.
 */
int
uha_isa_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct uha_softc sc;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int rv;

	if (bus_space_map(iot, ia->ia_iobase, UHA_ISA_IOSIZE, 0, &ioh))
		return (0);

	rv = u14_find(iot, ioh, &sc);

	bus_space_unmap(iot, ioh, UHA_ISA_IOSIZE);

	if (rv) {
		if (ia->ia_irq != -1 && ia->ia_irq != sc.sc_irq)
			return (0);
		if (ia->ia_drq != -1 && ia->ia_drq != sc.sc_drq)
			return (0);
		ia->ia_irq = sc.sc_irq;
		ia->ia_drq = sc.sc_drq;
		ia->ia_msize = 0;
		ia->ia_iosize = UHA_ISA_IOSIZE;
	}
	return (rv);
}

/*
 * Attach all the sub-devices we can find
 */
void
uha_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct uha_softc *sc = (void *)self;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	isa_chipset_tag_t ic = ia->ia_ic;

	printf("\n");

	if (bus_space_map(iot, ia->ia_iobase, UHA_ISA_IOSIZE, 0, &ioh))
		panic("uha_attach: bus_space_map failed!");

	sc->sc_iot = iot;
	sc->sc_ioh = ioh;
	if (!u14_find(iot, ioh, sc))
		panic("uha_attach: u14_find failed!");

	if (sc->sc_drq != -1)
		isadma_cascade(sc->sc_drq);

	sc->sc_ih = isa_intr_establish(ic, sc->sc_irq, IST_EDGE, IPL_BIO,
	    u14_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Save function pointers for later use. */
	sc->start_mbox = u14_start_mbox;
	sc->poll = u14_poll;
	sc->init = u14_init;

	uha_attach(sc);
}

/*
 * Start the board, ready for normal operation
 */
int
u14_find(bus_space_tag_t iot, bus_space_handle_t ioh, struct uha_softc *sc)
{
	u_int16_t model, config;
	int irq, drq;
	int resetcount = 4000;	/* 4 secs? */

	model = (bus_space_read_1(iot, ioh, U14_ID + 0) << 8) |
		(bus_space_read_1(iot, ioh, U14_ID + 1) << 0);
	if ((model & 0xfff0) != 0x5640)
		return (0);

	config = (bus_space_read_1(iot, ioh, U14_CONFIG + 0) << 8) |
		 (bus_space_read_1(iot, ioh, U14_CONFIG + 1) << 0);

	switch (model & 0x000f) {
	case 0x0000:
		switch (config & U14_DMA_MASK) {
		case U14_DMA_CH5:
			drq = 5;
			break;
		case U14_DMA_CH6:
			drq = 6;
			break;
		case U14_DMA_CH7:
			drq = 7;
			break;
		default:
			printf("u14_find: illegal drq setting %x\n",
			    config & U14_DMA_MASK);
			return (0);
		}
		break;
	case 0x0001:
		/* This is a 34f, and doesn't need an ISA DMA channel. */
		drq = -1;
		break;
	default:
		printf("u14_find: unknown model %x\n", model);
		return (0);
	}

	switch (config & U14_IRQ_MASK) {
	case U14_IRQ10:
		irq = 10;
		break;
	case U14_IRQ11:
		irq = 11;
		break;
	case U14_IRQ14:
		irq = 14;
		break;
	case U14_IRQ15:
		irq = 15;
		break;
	default:
		printf("u14_find: illegal irq setting %x\n",
		    config & U14_IRQ_MASK);
		return (0);
	}

	bus_space_write_1(iot, ioh, U14_LINT, UHA_ASRST);

	while (--resetcount) {
		if (bus_space_read_1(iot, ioh, U14_LINT))
			break;
		delay(1000);	/* 1 mSec per loop */
	}
	if (!resetcount) {
		printf("u14_find: board timed out during reset\n");
		return (0);
	}

	/* if we want to fill in softc, do so now */
	if (sc != NULL) {
		sc->sc_irq = irq;
		sc->sc_drq = drq;
		sc->sc_scsi_dev = config & U14_HOSTID_MASK;
	}

	return (1);
}

/*
 * Function to send a command out through a mailbox
 */
void
u14_start_mbox(struct uha_softc *sc, struct uha_mscp *mscp)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int spincount = 100000;	/* 1s should be enough */

	while (--spincount) {
		if ((bus_space_read_1(iot, ioh, U14_LINT) & U14_LDIP) == 0)
			break;
		delay(100);
	}
	if (!spincount)
		panic("%s: uha_start_mbox, board not responding",
		    sc->sc_dev.dv_xname);

	bus_space_write_4(iot, ioh, U14_OGMPTR, KVTOPHYS(mscp));
	if (mscp->flags & MSCP_ABORT)
		bus_space_write_1(iot, ioh, U14_LINT, U14_ABORT);
	else
		bus_space_write_1(iot, ioh, U14_LINT, U14_OGMFULL);

	if ((mscp->xs->flags & SCSI_POLL) == 0)
		timeout_add_msec(&mscp->xs->stimeout, mscp->timeout);
}

/*
 * Function to poll for command completion when in poll mode.
 *
 *	wait = timeout in msec
 */
int
u14_poll(struct uha_softc *sc, struct scsi_xfer *xs, int count)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	while (count) {
		/*
		 * If we had interrupts enabled, would we
		 * have got an interrupt?
		 */
		if (bus_space_read_1(iot, ioh, U14_SINT) & U14_SDIP)
			u14_intr(sc);
		if (xs->flags & ITSDONE)
			return (0);
		delay(1000);
		count--;
	}
	return (1);
}

/*
 * Catch an interrupt from the adaptor
 */
int
u14_intr(void *arg)
{
	struct uha_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct uha_mscp *mscp;
	u_char uhastat;
	u_long mboxval;

#ifdef	UHADEBUG
	printf("%s: uhaintr ", sc->sc_dev.dv_xname);
#endif /*UHADEBUG */

	if ((bus_space_read_1(iot, ioh, U14_SINT) & U14_SDIP) == 0)
		return (0);

	for (;;) {
		/*
		 * First get all the information and then
		 * acknowledge the interrupt
		 */
		uhastat = bus_space_read_1(iot, ioh, U14_SINT);
		mboxval = bus_space_read_4(iot, ioh, U14_ICMPTR);
		/* XXX Send an ABORT_ACK instead? */
		bus_space_write_1(iot, ioh, U14_SINT, U14_ICM_ACK);

#ifdef	UHADEBUG
		printf("status = 0x%x ", uhastat);
#endif /*UHADEBUG*/

		/*
		 * Process the completed operation
		 */
		mscp = uha_mscp_phys_kv(sc, mboxval);
		if (!mscp) {
			printf("%s: BAD MSCP RETURNED!\n",
			    sc->sc_dev.dv_xname);
			continue;	/* whatever it was, it'll timeout */
		}

		timeout_del(&mscp->xs->stimeout);
		uha_done(sc, mscp);

		if ((bus_space_read_1(iot, ioh, U14_SINT) & U14_SDIP) == 0)
			return (1);
	}
}

void
u14_init(struct uha_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	/* make sure interrupts are enabled */
#ifdef UHADEBUG
	printf("u14_init: lmask=%02x, smask=%02x\n",
	    bus_space_read_1(iot, ioh, U14_LMASK),
	    bus_space_read_1(iot, ioh, U14_SMASK));
#endif
	bus_space_write_1(iot, ioh, U14_LMASK, 0xd1);	/* XXX */
	bus_space_write_1(iot, ioh, U14_SMASK, 0x91);	/* XXX */
}
