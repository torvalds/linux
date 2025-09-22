/*	$OpenBSD: ahc_eisa.c,v 1.25 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: ahc_eisa.c,v 1.10 1996/10/21 22:30:58 thorpej Exp $	*/

/*
 * Product specific probe and attach routines for:
 * 	27/284X and aic7770 motherboard SCSI controllers
 *
 * Copyright (c) 1994, 1995, 1996 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: ahc_eisa.c,v 1.25 2022/04/06 18:59:28 naddy Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>
#include <dev/eisa/eisadevs.h>
#include <dev/ic/aic7xxx_openbsd.h>
#include <dev/ic/aic7xxx_inline.h>

#define AHC_EISA_SLOT_OFFSET	0xc00
#define AHC_EISA_IOSIZE		0x100

int   ahc_eisa_irq(bus_space_tag_t, bus_space_handle_t);
int   ahc_eisa_match(struct device *, void *, void *);
void  ahc_eisa_attach(struct device *, struct device *, void *);


const struct cfattach ahc_eisa_ca = {
	sizeof(struct ahc_softc), ahc_eisa_match, ahc_eisa_attach
};

/*
 * Return irq setting of the board, otherwise -1.
 */
int
ahc_eisa_irq(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int irq;
	u_char intdef;
	u_char hcntrl;
	
	/* Pause the card preserving the IRQ type */
	hcntrl = bus_space_read_1(iot, ioh, HCNTRL) & IRQMS;
	bus_space_write_1(iot, ioh, HCNTRL, hcntrl | PAUSE);
	
	intdef = bus_space_read_1(iot, ioh, INTDEF);
	switch (irq = (intdef & VECTOR)) {
	case 9:
	case 10:
	case 11:
	case 12:
	case 14:
	case 15:
		break;
	default:
		printf("ahc_eisa_irq: illegal irq setting %d\n", intdef);
		return -1;
	}

	/* Note that we are going and return (to probe) */
	return irq;
}

/*
 * Check the slots looking for a board we recognise
 * If we find one, note its address (slot) and call
 * the actual probe routine to check it out.
 */
int
ahc_eisa_match(struct device *parent, void *match, void *aux)
{
	struct eisa_attach_args *ea = aux;
	bus_space_tag_t iot = ea->ea_iot;
	bus_space_handle_t ioh;
	int irq;

	/* must match one of our known ID strings */
	if (strcmp(ea->ea_idstring, "ADP7770") &&
		 strcmp(ea->ea_idstring, "ADP7771")
#if 0
		 && strcmp(ea->ea_idstring, "ADP7756")	/* not EISA, but VL */
		 && strcmp(ea->ea_idstring, "ADP7757")	/* not EISA, but VL */
#endif
		)
		return (0);

	if (bus_space_map(iot, EISA_SLOT_ADDR(ea->ea_slot) +
			  AHC_EISA_SLOT_OFFSET, AHC_EISA_IOSIZE, 0, &ioh))
		return (0);

	irq = ahc_eisa_irq(iot, ioh);

	bus_space_unmap(iot, ioh, AHC_EISA_IOSIZE);

	return (irq >= 0);
}

void
ahc_eisa_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahc_softc *ahc = (void *)self;
	struct eisa_attach_args *ea = aux;
	bus_space_tag_t iot = ea->ea_iot;
	bus_space_handle_t ioh;
	int irq;
	eisa_chipset_tag_t ec = ea->ea_ec;
	eisa_intr_handle_t ih;
	const char *model, *intrstr;
	u_int biosctrl;
	u_int scsiconf;
	u_int scsiconf1;
	u_int intdef;
	int i;
	
	ahc_set_name(ahc, ahc->sc_dev.dv_xname);
	ahc_set_unit(ahc, ahc->sc_dev.dv_unit);
	
	/* set dma tags */
	ahc->parent_dmat = ea->ea_dmat;

	if (bus_space_map(iot, EISA_SLOT_ADDR(ea->ea_slot) +
			  AHC_EISA_SLOT_OFFSET, AHC_EISA_IOSIZE, 0, &ioh))
		panic("ahc_eisa_attach: can't map i/o addresses");
	if ((irq = ahc_eisa_irq(iot, ioh)) < 0)
		panic("ahc_eisa_attach: ahc_eisa_irq failed!");

	if (strcmp(ea->ea_idstring, "ADP7770") == 0) {
		model = EISA_PRODUCT_ADP7770;
	} else if (strcmp(ea->ea_idstring, "ADP7771") == 0) {
		model = EISA_PRODUCT_ADP7771;
	} else {
		panic("ahc_eisa_attach: Unknown device type %s",
				ea->ea_idstring);
	}
	printf(": %s\n", model);
	
	/*
	 * Instead of ahc_alloc() as in FreeBSD, do the few relevant
	 * initializations manually.
	 */
	LIST_INIT(&ahc->pending_scbs);
	for (i = 0; i < AHC_NUM_TARGETS; i++)
		TAILQ_INIT(&ahc->untagged_queues[i]);

	/*
	 * SCSI_IS_SCSIBUS_B() must returns false until sc_channel_b
	 * has been properly initialized.
	 */
	ahc->sc_child_b = NULL;

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7770|AHC_EISA;
	ahc->features = AHC_AIC7770_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG;
	ahc->flags |= AHC_PAGESCBS;
	ahc->tag = iot;
	ahc->bsh = ioh;
	ahc->bus_chip_init = ahc_chip_init;
	ahc->instruction_ram_size = 512;

	if (ahc_softc_init(ahc) != 0)
		return;
	
	if (ahc_reset(ahc, /*reinit*/FALSE) != 0)
		return;
	
	/* See if we are edge triggered */
	intdef = ahc_inb(ahc, INTDEF);
	if ((intdef & EDGE_TRIG) != 0)
		ahc->flags |= AHC_EDGE_INTERRUPT;
	
	if (eisa_intr_map(ec, irq, &ih)) {
		printf("%s: couldn't map interrupt (%d)\n",
		       ahc->sc_dev.dv_xname, irq);
		return;
	}

	/*
	 * Tell the user what type of interrupts we're using.
	 * useful for debugging irq problems
	 */
	if (bootverbose) {
		printf("%s: Using %s Interrupts\n",
		       ahc_name(ahc),
		       ahc->pause & IRQMS ?
		       "Level Sensitive" : "Edge Triggered");
	}

	/*
	 * Now that we know we own the resources we need, do the 
	 * card initialization.
	 *
	 * First, the aic7770 card specific setup.
	 */
	biosctrl = ahc_inb(ahc, HA_274_BIOSCTRL);
	scsiconf = ahc_inb(ahc, SCSICONF);
	scsiconf1 = ahc_inb(ahc, SCSICONF + 1);
	
	/* Get the primary channel information */
	if ((biosctrl & CHANNEL_B_PRIMARY) != 0)
		ahc->flags |= AHC_PRIMARY_CHANNEL;

	if ((biosctrl & BIOSMODE) == BIOSDISABLED) {
		ahc->flags |= AHC_USEDEFAULTS;
	} else if ((ahc->features & AHC_WIDE) != 0) {
		ahc->our_id = scsiconf1 & HWSCSIID;
		if (scsiconf & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_A;
	} else {
		ahc->our_id = scsiconf & HSCSIID;
		ahc->our_id_b = scsiconf1 & HSCSIID;
		if (scsiconf & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_A;
		if (scsiconf1 & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_B;
	}
	/*
	 * We have no way to tell, so assume extended
	 * translation is enabled.
	 */
	
	ahc->flags |= AHC_EXTENDED_TRANS_A|AHC_EXTENDED_TRANS_B;
	
	/*      
	 * See if we have a Rev E or higher aic7770. Anything below a
	 * Rev E will have a R/O autoflush disable configuration bit.
	 * It's still not clear exactly what is different about the Rev E.
	 * We think it allows 8 bit entries in the QOUTFIFO to support
	 * "paging" SCBs so you can have more than 4 commands active at
	 * once.
	 */
	{
		char *id_string;
		u_char sblkctl;
		u_char sblkctl_orig;

		sblkctl_orig = ahc_inb(ahc, SBLKCTL);
		sblkctl = sblkctl_orig ^ AUTOFLUSHDIS;
		ahc_outb(ahc, SBLKCTL, sblkctl);
		sblkctl = ahc_inb(ahc, SBLKCTL);
		if (sblkctl != sblkctl_orig) {
			id_string = "aic7770 >= Rev E";
			/*
			 * Ensure autoflush is enabled
			 */
			sblkctl &= ~AUTOFLUSHDIS;
			ahc_outb(ahc, SBLKCTL, sblkctl);

			/* Allow paging on this adapter */
			ahc->flags |= AHC_PAGESCBS;
		} else
			id_string = "aic7770 <= Rev C";

		if (bootverbose)
			printf("%s: %s\n", ahc_name(ahc), id_string);
	}

	/* Setup the FIFO threshold and the bus off time */
	{
		u_char hostconf = ahc_inb(ahc, HOSTCONF);
		ahc_outb(ahc, BUSSPD, hostconf & DFTHRSH);
		ahc_outb(ahc, BUSTIME, (hostconf << 2) & BOFF);
	}

	/*
	 * Generic aic7xxx initialization.
	 */
	if (ahc_init(ahc)) {
		ahc_free(ahc);
		return;
	}
 
	/*
	 * Link this softc in with all other ahc instances.
	 */
	ahc_softc_insert(ahc);
	
	/*
	 * Enable the board's BUS drivers
	 */
	ahc_outb(ahc, BCTL, ENABLE);

	intrstr = eisa_intr_string(ec, ih);
	/*
	 * The IRQMS bit enables level sensitive interrupts only allow
	 * IRQ sharing if its set.
	 */
	ahc->ih = eisa_intr_establish(ec, ih,
	    ahc->pause & IRQMS ? IST_LEVEL : IST_EDGE, IPL_BIO,
	    ahc_platform_intr, ahc, ahc->sc_dev.dv_xname);
	if (ahc->ih == NULL) {
		printf("%s: couldn't establish interrupt",
		       ahc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		ahc_free(ahc);
		return;
	}
	if (intrstr != NULL)
		printf("%s: interrupting at %s\n", ahc->sc_dev.dv_xname,
		    intrstr);
	
	ahc_intr_enable(ahc, TRUE);

	/* Attach sub-devices - always succeeds */
	ahc_attach(ahc);

}
