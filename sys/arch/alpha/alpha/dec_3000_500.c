/* $OpenBSD: dec_3000_500.c,v 1.16 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: dec_3000_500.c,v 1.29 2000/05/22 20:13:32 thorpej Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
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
#include <sys/conf.h>

#include <machine/rpb.h>
#include <machine/autoconf.h>
#include <machine/cpuconf.h>

#include <dev/tc/tcvar.h>
#include <dev/tc/tcdsvar.h>
#include <alpha/tc/tc_3000_500.h>

#include <machine/z8530var.h>
#include <dev/tc/zs_ioasicvar.h>
#if 0
#include <dev/dec/zskbdvar.h>
#endif

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include "wsdisplay.h"

void dec_3000_500_init(void);
static void dec_3000_500_cons_init(void);
static void dec_3000_500_device_register(struct device *, void *);

static const char dec_3000_500_sp[] = "DEC 3000/400 (\"Sandpiper\")";
static const char dec_3000_500_sf[] = "DEC 3000/500 (\"Flamingo\")";

const struct alpha_variation_table dec_3000_500_variations[] = {
	{ SV_ST_SANDPIPER, dec_3000_500_sp },
	{ SV_ST_FLAMINGO, dec_3000_500_sf },
	{ SV_ST_HOTPINK, "DEC 3000/500X (\"Hot Pink\")" },
	{ SV_ST_FLAMINGOPLUS, "DEC 3000/800 (\"Flamingo+\")" },
	{ SV_ST_SANDPLUS, "DEC 3000/600 (\"Sandpiper+\")" },
	{ SV_ST_SANDPIPER45, "DEC 3000/700 (\"Sandpiper45\")" },
	{ SV_ST_FLAMINGO45, "DEC 3000/900 (\"Flamingo45\")" },
	{ 0, NULL },
};

void
dec_3000_500_init(void)
{
	u_int64_t variation;

	platform.family = "DEC 3000/500 (\"Flamingo\")";

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		variation = hwrpb->rpb_variation & SV_ST_MASK;
		if (variation == SV_ST_ULTRA) {
			/* These are really the same. */
			variation = SV_ST_FLAMINGOPLUS;
		}
		if ((platform.model = alpha_variation_name(variation,
		    dec_3000_500_variations)) == NULL) {
			/*
			 * This is how things used to be done.
			 */
			if (variation == SV_ST_RESERVED) {
				if (hwrpb->rpb_variation & SV_GRAPHICS)
					platform.model = dec_3000_500_sf;
				else
					platform.model = dec_3000_500_sp;
			} else
				platform.model = alpha_unknown_sysname();
		}
	}

	platform.iobus = "tcasic";
	platform.cons_init = dec_3000_500_cons_init;
	platform.device_register = dec_3000_500_device_register;
}

static void
dec_3000_500_cons_init(void)
{
	struct ctb *ctb;

	ctb = (struct ctb *)(((caddr_t)hwrpb) + hwrpb->rpb_ctb_off);

	switch (ctb->ctb_term_type) {
	case CTB_GRAPHICS:
#if NWSDISPLAY > 0
#ifdef notyet
		/* display console ... */
		if (zs_ioasic_lk201_cnattach(0x1e0000000, 0x00180000, 0) == 0 &&
		    tc_3000_500_fb_cnattach(
		     CTB_TURBOSLOT_SLOT(ctb->ctb_turboslot)) == 0) {
			break;
		}
#endif
#endif
		printf("consinit: Unable to init console on keyboard and ");
		printf("TURBOchannel slot 0x%lx.\n",
		    (unsigned long)ctb->ctb_turboslot);
		printf("Using serial console.\n");
		/* FALLTHROUGH */

	case CTB_PRINTERPORT:
		/* serial console ... */
		/*
		 * XXX This could stand some cleanup...
		 */
		{
			/*
			 * Delay to allow PROM putchars to complete.
			 * FIFO depth * character time,
			 * character time = (1000000 / (defaultrate / 10))
			 */
			DELAY(160000000 / 9600);	/* XXX */

			/*
			 * Console is channel B of the second SCC.
			 * XXX Should use ctb_line_off to get the
			 * XXX line parameters--these are the defaults.
			 */
			zs_ioasic_cnattach(0x1e0000000, 0x00180000, 1);
			break;
		}
	default:
		printf("ctb->ctb_term_type = 0x%lx\n",
		    (unsigned long)ctb->ctb_term_type);
		printf("ctb->ctb_turboslot = 0x%lx\n",
		    (unsigned long)ctb->ctb_turboslot);
		panic("consinit: unknown console type %lu",
		    (unsigned long)ctb->ctb_term_type);
		/* NOTREACHED */
	}
}

static void
dec_3000_500_device_register(struct device *dev, void *aux)
{
	static int found, initted, scsiboot, netboot;
	static struct device *scsidev;
	static struct device *tcdsdev;
	struct bootdev_data *b = bootdev_data;
	struct device *parent = dev->dv_parent;
	struct cfdata *cf = dev->dv_cfdata;
	struct cfdriver *cd = cf->cf_driver;

	if (found)
		return;

	if (!initted) {
		scsiboot = (strcmp(b->protocol, "SCSI") == 0);
		netboot = (strcmp(b->protocol, "BOOTP") == 0) ||
		    (strcmp(b->protocol, "MOP") == 0);
#if 0
		printf("scsiboot = %d, netboot = %d\n", scsiboot, netboot);
#endif
		initted = 1;
	}

	/*
	 * for scsi boot, we look for "tcds", make sure it has the
	 * right slot number, then find the "asc" on this tcds that
	 * as the right channel.  then we find the actual scsi
	 * device we came from.  note: no SCSI LUN support (yet).
	 */
	if (scsiboot && (strcmp(cd->cd_name, "tcds") == 0)) {
		struct tc_attach_args *tcargs = aux;

		if (b->slot != tcargs->ta_slot)
			return;

		tcdsdev = dev;
#if 0
		printf("\ntcdsdev = %s\n", dev->dv_xname);
#endif
	}
	if (scsiboot && tcdsdev &&
	    (strcmp(cd->cd_name, "asc") == 0)) {
		struct tcdsdev_attach_args *ta = aux;

		if (parent != (struct device *)tcdsdev)
			return;

		if (ta->tcdsda_chip != b->channel)
			return;

		scsidev = dev;
#if 0
		printf("\nscsidev = %s\n", dev->dv_xname);
#endif
	}

	if (scsiboot && scsidev &&
	    (strcmp(cd->cd_name, "sd") == 0 ||
	     strcmp(cd->cd_name, "st") == 0 ||
	     strcmp(cd->cd_name, "cd") == 0)) {
		struct scsi_attach_args *sa = aux;

		if (parent->dv_parent != scsidev)
			return;

		if (b->unit / 100 != sa->sa_sc_link->target)
			return;

		/* XXX LUN! */

		switch (b->boot_dev_type) {
		case 0:
			if (strcmp(cd->cd_name, "sd") &&
			    strcmp(cd->cd_name, "cd"))
				return;
			break;
		case 1:
			if (strcmp(cd->cd_name, "st"))
				return;
			break;
		default:
			return;
		}

		/* we've found it! */
		booted_device = dev;
#if 0
		printf("\nbooted_device = %s\n", booted_device->dv_xname);
#endif
		found = 1;
	}

	if (netboot) {
		if (b->slot == 7 && strcmp(cd->cd_name, "le") == 0 &&
		    strcmp(parent->dv_cfdata->cf_driver->cd_name, "ioasic")
		     == 0) {
			/*
			 * no need to check ioasic_attach_args, since only
			 * one le on ioasic.
			 */

			booted_device = dev;
#if 0
			printf("\nbooted_device = %s\n", booted_device->dv_xname);
#endif
			found = 1;
			return;
		}

		/*
		 * XXX GENERIC SUPPORT FOR TC NETWORK BOARDS
		 */
	}
}
