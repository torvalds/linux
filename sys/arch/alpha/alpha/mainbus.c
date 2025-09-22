/* $OpenBSD: mainbus.c,v 1.16 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: mainbus.c,v 1.27 1998/06/24 01:10:35 ross Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>
#include <sys/conf.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>
#include <machine/cpuconf.h>

/* Definition of the mainbus driver. */
static int	mbmatch(struct device *, void *, void *);
static void	mbattach(struct device *, struct device *, void *);
static int	mbprint(void *, const char *);

const struct cfattach mainbus_ca = {
	sizeof(struct device), mbmatch, mbattach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

/* There can be only one. */
int	mainbus_found;

static int
mbmatch(struct device *parent, void *cf, void *aux)
{

	if (mainbus_found)
		return (0);

	return (1);
}

static void
mbattach(struct device *parent, struct device *self, void *aux)
{
	struct mainbus_attach_args ma;
	struct pcs *pcsp;
	int i;

	mainbus_found = 1;

	printf("\n");

	/*
	 * Try to find and attach all of the CPUs in the machine.
	 */
	for (i = 0; i < hwrpb->rpb_pcs_cnt; i++) {
		pcsp = LOCATE_PCS(hwrpb, i);
		if ((pcsp->pcs_flags & PCS_PP) == 0)
			continue;

		ma.ma_name = "cpu";
		ma.ma_slot = i;
		config_found(self, &ma, mbprint);
	}

	if (platform.iobus != NULL) {
		ma.ma_name = platform.iobus;
		ma.ma_slot = 0;			/* meaningless */
		config_found(self, &ma, mbprint);
	}
}

static int
mbprint(void *aux, const char *pnp)
{
	struct mainbus_attach_args *ma = aux;

	if (pnp)
		printf("%s at %s", ma->ma_name, pnp);

	return (UNCONF);
}
