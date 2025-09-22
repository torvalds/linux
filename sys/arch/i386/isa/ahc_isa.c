/*	$OpenBSD: ahc_isa.c,v 1.21 2022/02/21 12:53:39 jsg Exp $	*/
/*	$NetBSD: ahc_isa.c,v 1.5 1996/10/21 22:27:39 thorpej Exp $	*/

/*
 * Product specific probe and attach routines for:
 * 	284X VLbus SCSI controllers
 *
 * Copyright (c) 1996 Jason R. Thorpe.
 * All rights reserved.
 *
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.
 * All rights reserved.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
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
 */

/*
 * This front-end driver is really sort of a hack.  The AHA-284X likes
 * to masquerade as an EISA device.  However, on VLbus machines with
 * no EISA signature in the BIOS, the EISA bus will never be scanned.
 * This is intended to catch the 284X controllers on those systems
 * by looking in "EISA i/o space" for 284X controllers.
 *
 * This relies heavily on i/o port accounting.  We also just use the
 * EISA macros for everything ... it's a real waste to redefine them.
 *
 * Note: there isn't any #ifdef for FreeBSD in this file, since the
 * FreeBSD EISA driver handles all cases of the 284X.
 *
 *	-- Jason R. Thorpe <thorpej@NetBSD.ORG>
 *	   July 12, 1996
 *
 * TODO: some code could be shared with ahc_eisa.c, but it would probably
 * be a logistical mightmare to even try.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/isa/isavar.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>
#include <dev/eisa/eisadevs.h>

#include <dev/ic/aic7xxx_openbsd.h>
#include <dev/ic/aic7xxx_inline.h>
#include <dev/ic/smc93cx6var.h>

#ifdef DEBUG
#define bootverbose	1
#else
#define bootverbose	0
#endif

/* IO port address setting range as EISA slot number */
#define AHC_ISA_MIN_SLOT	0x1	/* from iobase = 0x1c00 */
#define AHC_ISA_MAX_SLOT	0xe	/* to   iobase = 0xec00 */

#define AHC_ISA_SLOT_OFFSET	0xc00	/* offset from EISA IO space */
#define AHC_ISA_IOSIZE		0x100

/*
 * I/O port offsets
 */
#define	AHC_ISA_VID		(EISA_SLOTOFF_VID - AHC_ISA_SLOT_OFFSET)
#define	AHC_ISA_PID		(EISA_SLOTOFF_PID - AHC_ISA_SLOT_OFFSET)
#define	AHC_ISA_PRIMING		AHC_ISA_VID	/* enable vendor/product ID */

/*
 * AHC_ISA_PRIMING register values (write)
 */
#define	AHC_ISA_PRIMING_VID(index)	(AHC_ISA_VID + (index))
#define	AHC_ISA_PRIMING_PID(index)	(AHC_ISA_PID + (index))

int	ahc_isa_irq(bus_space_tag_t, bus_space_handle_t);
int	ahc_isa_idstring(bus_space_tag_t, bus_space_handle_t, char *);
int	ahc_isa_match(struct isa_attach_args *, bus_addr_t);

int	ahc_isa_probe(struct device *, void *, void *);
void	ahc_isa_attach(struct device *, struct device *, void *);
void	aha2840_load_seeprom(struct ahc_softc *ahc);

const struct cfattach ahc_isa_ca = {
	sizeof(struct ahc_softc), ahc_isa_probe, ahc_isa_attach
};

/*
 * This keeps track of which slots are to be checked next if the
 * iobase locator is a wildcard.  A simple static variable isn't enough,
 * since it's conceivable that a system might have more than one ISA
 * bus.
 *
 * The "bus" member is the unit number of the parent ISA bus, e.g. "0"
 * for "isa0".
 */
struct ahc_isa_slot {
	LIST_ENTRY(ahc_isa_slot)	link;
	int				bus;
	int				slot;
};
static LIST_HEAD(, ahc_isa_slot) ahc_isa_all_slots;
static int ahc_isa_slot_initialized;

/*
 * Return irq setting of the board, otherwise -1.
 */
int
ahc_isa_irq(bus_space_tag_t iot, bus_space_handle_t ioh)
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
		printf("ahc_isa_irq: illegal irq setting %d\n", intdef);
		return -1;
	}

	/* Note that we are going and return (to probe) */
	return irq;
}

int
ahc_isa_idstring(bus_space_tag_t iot, bus_space_handle_t ioh, char *idstring)
{
	u_int8_t vid[EISA_NVIDREGS], pid[EISA_NPIDREGS];
	int i;

	/* Get the vendor ID bytes */
	for (i = 0; i < EISA_NVIDREGS; i++) {
		bus_space_write_1(iot, ioh, AHC_ISA_PRIMING,
		    AHC_ISA_PRIMING_VID(i));
		vid[i] = bus_space_read_1(iot, ioh, AHC_ISA_VID + i);
	}

	/* Check for device existence */
	if (EISA_VENDID_NODEV(vid)) {
#if 0
		printf("ahc_isa_idstring: no device at 0x%lx\n",
		    ioh); /* XXX knows about ioh guts */
		printf("\t(0x%x, 0x%x)\n", vid[0], vid[1]);
#endif
		return (0);
	}

	/* And check that the firmware didn't biff something badly */
	if (EISA_VENDID_IDDELAY(vid)) {
		printf("ahc_isa_idstring: BIOS biffed it at 0x%lx\n",
		    ioh);	/* XXX knows about ioh guts */
		return (0);
	}

	/* Get the product ID bytes */
	for (i = 0; i < EISA_NPIDREGS; i++) {
		bus_space_write_1(iot, ioh, AHC_ISA_PRIMING,
		    AHC_ISA_PRIMING_PID(i));
		pid[i] = bus_space_read_1(iot, ioh, AHC_ISA_PID + i);
	}

	/* Create the ID string from the vendor and product IDs */
	idstring[0] = EISA_VENDID_0(vid);
	idstring[1] = EISA_VENDID_1(vid);
	idstring[2] = EISA_VENDID_2(vid);
	idstring[3] = EISA_PRODID_0(pid);
	idstring[4] = EISA_PRODID_1(pid);
	idstring[5] = EISA_PRODID_2(pid);
	idstring[6] = EISA_PRODID_3(pid);
	idstring[7] = '\0';		/* sanity */

	return (1);
}

int
ahc_isa_match(struct isa_attach_args *ia, bus_addr_t iobase)
{
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int irq;
	char idstring[EISA_IDSTRINGLEN];

	/*
	 * Get a mapping for the while slot-specific address
	 * space.  If we can't, assume nothing's there, but
	 * warn about it.
	 */
	if (bus_space_map(iot, iobase, AHC_ISA_IOSIZE, 0, &ioh)) {
#if 0
		/*
		 * Don't print anything out here, since this could
		 * be common on machines configured to look for
		 * ahc_eisa and ahc_isa.
		 */
		printf("ahc_isa_match: can't map i/o space for 0x%x\n",
		    iobase);
#endif
		return (0);
	}

	if (!ahc_isa_idstring(iot, ioh, idstring))
		irq = -1;	/* cannot get the ID string */
	else if (strcmp(idstring, "ADP7756") &&
	    strcmp(idstring, "ADP7757"))
		irq = -1;	/* unknown ID strings */
	else
		irq = ahc_isa_irq(iot, ioh);

	bus_space_unmap(iot, ioh, AHC_ISA_IOSIZE);

	if (irq < 0)
		return (0);

	if (ia->ia_irq != IRQUNK &&
	    ia->ia_irq != irq) {
		printf("ahc_isa_match: irq mismatch (kernel %d, card %d)\n",
		       ia->ia_irq, irq);
		return (0);
	}

	/* We have a match */
	ia->ia_iobase = iobase;
	ia->ia_irq = irq;
	ia->ia_iosize = AHC_ISA_IOSIZE;
	ia->ia_msize = 0;
	return (1);
}

/*
 * Check the slots looking for a board we recognise
 * If we find one, note its address (slot) and call
 * the actual probe routine to check it out.
 */
int
ahc_isa_probe(struct device *parent, void *match, void *aux)
{       
	struct isa_attach_args *ia = aux;
	struct ahc_isa_slot *as;

	if (ahc_isa_slot_initialized == 0) {
		LIST_INIT(&ahc_isa_all_slots);
		ahc_isa_slot_initialized = 1;
	}

	if (ia->ia_iobase != IOBASEUNK)
		return (ahc_isa_match(ia, ia->ia_iobase));

	/*
	 * Find this bus's state.  If we don't yet have a slot
	 * marker, allocate and initialize one.
	 */
	LIST_FOREACH(as, &ahc_isa_all_slots, link)
		if (as->bus == parent->dv_unit)
			goto found_slot_marker;

	/*
	 * Don't have one, so make one.
	 */
	as = (struct ahc_isa_slot *)
	    malloc(sizeof(struct ahc_isa_slot), M_DEVBUF, M_NOWAIT);
	if (as == NULL)
		panic("ahc_isa_probe: can't allocate slot marker");

	as->bus = parent->dv_unit;
	as->slot = AHC_ISA_MIN_SLOT;
	LIST_INSERT_HEAD(&ahc_isa_all_slots, as, link);

 found_slot_marker:

	for (; as->slot <= AHC_ISA_MAX_SLOT; as->slot++) {
		if (ahc_isa_match(ia, EISA_SLOT_ADDR(as->slot) +
		    AHC_ISA_SLOT_OFFSET)) {
			as->slot++; /* next slot to search */
			return (1);
		}
	}

	/* No matching cards were found. */
	return (0);
}

void
ahc_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahc_softc *ahc = (void *)self;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int irq;
	char idstring[EISA_IDSTRINGLEN];
	const char *model;
	u_int intdef;
	
	ahc_set_name(ahc, ahc->sc_dev.dv_xname);
	ahc_set_unit(ahc, ahc->sc_dev.dv_unit);
	
	/* set dma tags */
	ahc->parent_dmat = ia->ia_dmat;
	
	ahc->chip = AHC_VL; /* We are a VL Bus Controller */  
	
	if (bus_space_map(iot, ia->ia_iobase, ia->ia_iosize, 0, &ioh))
		panic("ahc_isa_attach: can't map slot i/o addresses");
	if (!ahc_isa_idstring(iot, ioh, idstring))
		panic("ahc_isa_attach: could not read ID string");
	if ((irq = ahc_isa_irq(iot, ioh)) < 0)
		panic("ahc_isa_attach: ahc_isa_irq failed!");

	if (strcmp(idstring, "ADP7756") == 0) {
		model = EISA_PRODUCT_ADP7756;
	} else if (strcmp(idstring, "ADP7757") == 0) {
		model = EISA_PRODUCT_ADP7757;
	} else {
		panic("ahc_isa_attach: Unknown device type %s", idstring);
	}
	printf(": %s\n", model);
	
	ahc->channel = 'A';
	ahc->chip = AHC_AIC7770;
	ahc->features = AHC_AIC7770_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG;
	ahc->flags |= AHC_PAGESCBS;
	
	/* set tag and handle */
	ahc->tag = iot;
	ahc->bsh = ioh;

#ifdef DEBUG
	/*
	 * Tell the user what type of interrupts we're using.
	 * useful for debugging irq problems
	 */
	printf( "%s: Using %s Interrupts\n", ahc_name(ahc),
	    ahc->pause & IRQMS ?  "Level Sensitive" : "Edge Triggered");
#endif

	if (ahc_reset(ahc, /*reinit*/FALSE) != 0)
		return;
	
	/* See if we are edge triggered */
	intdef = ahc_inb(ahc, INTDEF);
	if ((intdef & EDGE_TRIG) != 0)
		ahc->flags |= AHC_EDGE_INTERRUPT;

	/*
	 * Now that we know we own the resources we need, do the 
	 * card initialization.
	 */
	aha2840_load_seeprom(ahc);

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
		if(sblkctl != sblkctl_orig)
		{
			id_string = "aic7770 >= Rev E, ";
			/*
			 * Ensure autoflush is enabled
			 */
			sblkctl &= ~AUTOFLUSHDIS;
			ahc_outb(ahc, SBLKCTL, sblkctl);

			/* Allow paging on this adapter */
			ahc->flags |= AHC_PAGESCBS;
		}
		else
			id_string = "aic7770 <= Rev C, ";

		printf("%s: %s", ahc_name(ahc), id_string);
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
	if(ahc_init(ahc)){
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

	/*
	 * The IRQMS bit enables level sensitive interrupts only allow
	 * IRQ sharing if its set.
	 */
	ahc->ih = isa_intr_establish(ia->ia_ic, irq,
	    ahc->pause & IRQMS ? IST_LEVEL : IST_EDGE, IPL_BIO, ahc_platform_intr,
	    ahc, ahc->sc_dev.dv_xname);
	if (ahc->ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		       ahc->sc_dev.dv_xname);
		ahc_free(ahc);
		return;
	}

	ahc_intr_enable(ahc, TRUE);
	
	/* Attach sub-devices - always succeeds */
	ahc_attach(ahc);
}

/*
 * Read the 284x SEEPROM.
 */
void
aha2840_load_seeprom(struct ahc_softc *ahc)
{
	struct	  seeprom_descriptor sd;
	struct	  seeprom_config sc;
	u_int16_t checksum = 0;
	u_int8_t  scsi_conf;
	int	  have_seeprom;

	sd.sd_tag = ahc->tag;
	sd.sd_bsh = ahc->bsh;
	sd.sd_regsize = 1;
	sd.sd_control_offset = SEECTL_2840;
	sd.sd_status_offset = STATUS_2840;
	sd.sd_dataout_offset = STATUS_2840;		
	sd.sd_chip = C46;
	sd.sd_MS = 0;
	sd.sd_RDY = EEPROM_TF;
	sd.sd_CS = CS_2840;
	sd.sd_CK = CK_2840;
	sd.sd_DO = DO_2840;
	sd.sd_DI = DI_2840;

	if (bootverbose)
		printf("%s: Reading SEEPROM...", ahc_name(ahc));
	have_seeprom = read_seeprom(&sd, 
				    (u_int16_t *)&sc, 
				    /*start_addr*/0,
				    sizeof(sc)/2);

	if (have_seeprom) {
		/* Check checksum */
		int i;
		int maxaddr = (sizeof(sc)/2) - 1;
		u_int16_t *scarray = (u_int16_t *)&sc;

		for (i = 0; i < maxaddr; i++)
			checksum = checksum + scarray[i];
		if (checksum != sc.checksum) {
			if(bootverbose)
				printf ("checksum error\n");
			have_seeprom = 0;
		} else if (bootverbose) {
			printf("done.\n");
		}
	}

	if (!have_seeprom) {
		if (bootverbose)
			printf("%s: No SEEPROM available\n", ahc_name(ahc));
		ahc->flags |= AHC_USEDEFAULTS;
	} else {
		/*
		 * Put the data we've collected down into SRAM
		 * where ahc_init will find it.
		 */
		int i;
		int max_targ = (ahc->features & AHC_WIDE) != 0 ? 16 : 8;
		u_int16_t discenable;

		discenable = 0;
		for (i = 0; i < max_targ; i++){
	                u_int8_t target_settings;
			target_settings = (sc.device_flags[i] & CFXFER) << 4;
			if (sc.device_flags[i] & CFSYNCH)
				target_settings |= SOFS;
			if (sc.device_flags[i] & CFWIDEB)
				target_settings |= WIDEXFER;
			if (sc.device_flags[i] & CFDISC)
				discenable |= (0x01 << i);
			ahc_outb(ahc, TARG_SCSIRATE + i, target_settings);
		}
		ahc_outb(ahc, DISC_DSB, ~(discenable & 0xff));
		ahc_outb(ahc, DISC_DSB + 1, ~((discenable >> 8) & 0xff));

		ahc->our_id = sc.brtime_id & CFSCSIID;

		scsi_conf = (ahc->our_id & 0x7);
		if (sc.adapter_control & CFSPARITY)
			scsi_conf |= ENSPCHK;
		if (sc.adapter_control & CFRESETB)
			scsi_conf |= RESET_SCSI;

		if (sc.bios_control & CF284XEXTEND)		
			ahc->flags |= AHC_EXTENDED_TRANS_A;
		/* Set SCSICONF info */
		ahc_outb(ahc, SCSICONF, scsi_conf);

		if (sc.adapter_control & CF284XSTERM)
			ahc->flags |= AHC_TERM_ENB_A;
	}
}
