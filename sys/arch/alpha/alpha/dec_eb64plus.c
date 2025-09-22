/* $OpenBSD: dec_eb64plus.c,v 1.11 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: dec_eb64plus.c,v 1.25 2001/06/05 04:53:11 thorpej Exp $ */

/*
 * Copyright (c) 1995, 1996, 1997 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 * Additional Copyright (c) 1997 by Matthew Jacob for NASA/Ames Research Center
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>
#include <dev/cons.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/cpuconf.h>
#include <machine/conf.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <alpha/pci/apecsreg.h>
#include <alpha/pci/apecsvar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <dev/ata/atavar.h>

#include "pckbd.h"

#ifndef CONSPEED
#define CONSPEED TTYDEF_SPEED
#endif
static int comcnrate = CONSPEED;

void dec_eb64plus_init(void);
static void dec_eb64plus_cons_init(void);
static void dec_eb64plus_device_register(struct device *, void *);

const struct alpha_variation_table dec_eb64plus_variations[] = {
	{ 0, "DEC EB64+" },
	{ 0, NULL },
};

void
dec_eb64plus_init(void)
{
	u_int64_t variation;

	platform.family = "EB64+";

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		variation = hwrpb->rpb_variation & SV_ST_MASK;
		if ((platform.model = alpha_variation_name(variation,
		    dec_eb64plus_variations)) == NULL)
			platform.model = alpha_unknown_sysname();
	}

	platform.iobus = "apecs";
	platform.cons_init = dec_eb64plus_cons_init;
	platform.device_register = dec_eb64plus_device_register;
}

static void
dec_eb64plus_cons_init(void)
{
	struct ctb *ctb;
	struct apecs_config *acp;
	extern struct apecs_config apecs_configuration;

	acp = &apecs_configuration;
	apecs_init(acp, 0);

	ctb = (struct ctb *)(((caddr_t)hwrpb) + hwrpb->rpb_ctb_off);

	switch (ctb->ctb_term_type) {
	case CTB_PRINTERPORT:
		/* serial console ... */
		/* XXX */
		{
			/*
			 * Delay to allow PROM putchars to complete.
			 * FIFO depth * character time,
			 * character time = (1000000 / (defaultrate / 10))
			 */
			DELAY(160000000 / comcnrate);

			if(comcnattach(&acp->ac_iot, 0x3f8, comcnrate,
			    COM_FREQ,
			    (TTYDEF_CFLAG & ~(CSIZE | PARENB)) | CS8))
				panic("can't init serial console");

			break;
		}

	case CTB_GRAPHICS:
#if NPCKBD > 0
		/* display console ... */
		/* XXX */
		(void) pckbc_cnattach(&acp->ac_iot, IO_KBD, KBCMDP, 0);

		if (CTB_TURBOSLOT_TYPE(ctb->ctb_turboslot) ==
		    CTB_TURBOSLOT_TYPE_ISA)
			isa_display_console(&acp->ac_iot, &acp->ac_memt);
		else
			pci_display_console(&acp->ac_iot, &acp->ac_memt,
			    &acp->ac_pc, CTB_TURBOSLOT_BUS(ctb->ctb_turboslot),
			    CTB_TURBOSLOT_SLOT(ctb->ctb_turboslot), 0);
#else
		panic("not configured to use display && keyboard console");
#endif
		break;

	default:
		printf("ctb->ctb_term_type = 0x%lx\n",
		    (unsigned long)ctb->ctb_term_type);
		printf("ctb->ctb_turboslot = 0x%lx\n",
		    (unsigned long)ctb->ctb_turboslot);

		panic("consinit: unknown console type %lu",
		    (unsigned long)ctb->ctb_term_type);
	}
}

static void
dec_eb64plus_device_register(struct device *dev, void *aux)
{
	static int found, initted, diskboot, netboot;
	static struct device *pcidev, *ctrlrdev;
	struct bootdev_data *b = bootdev_data;
	struct device *parent = dev->dv_parent;
	struct cfdata *cf = dev->dv_cfdata;
	struct cfdriver *cd = cf->cf_driver;

	if (found)
		return;

	if (!initted) {
		diskboot = (strncasecmp(b->protocol, "SCSI", 4) == 0);
		netboot = (strncasecmp(b->protocol, "BOOTP", 5) == 0) ||
		    (strncasecmp(b->protocol, "MOP", 3) == 0);
#if 0
		printf("diskboot = %d, netboot = %d\n", diskboot, netboot);
#endif
		initted =1;
	}

	if (pcidev == NULL) {
		if (strcmp(cd->cd_name, "pci"))
			return;
		else {
			struct pcibus_attach_args *pba = aux;

			if ((b->slot / 1000) != pba->pba_bus)
				return;
	
			pcidev = dev;
#if 0
			printf("\npcidev = %s\n", dev->dv_xname);
#endif
			return;
		}
	}

	if (ctrlrdev == NULL) {
		if (parent != pcidev)
			return;
		else {
			struct pci_attach_args *pa = aux;
			int slot;

			slot = pa->pa_bus * 1000 + pa->pa_function * 100 +
			    pa->pa_device;
			if (b->slot != slot)
				return;
	
			if (netboot) {
				booted_device = dev;
#if 0
				printf("\nbooted_device = %s\n", dev->dv_xname);
#endif
				found = 1;
			} else {
				ctrlrdev = dev;
#if 0
				printf("\nctrlrdev = %s\n", dev->dv_xname);
#endif
			}
			return;
		}
	}

	if (!diskboot)
		return;

	if (!strcmp(cd->cd_name, "sd") || !strcmp(cd->cd_name, "st") ||
	    !strcmp(cd->cd_name, "cd")) {
		struct scsi_attach_args *sa = aux;
		struct scsi_link *periph = sa->sa_sc_link;
		int unit;

		if (parent->dv_parent != ctrlrdev)
			return;

		unit = periph->target * 100 + periph->lun;
		if (b->unit != unit)
			return;

		/* we've found it! */
		booted_device = dev;
#if 0
		printf("\nbooted_device = %s\n", dev->dv_xname);
#endif
		found = 1;
	}
}
