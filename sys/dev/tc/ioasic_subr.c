/*	$OpenBSD: ioasic_subr.c,v 1.3 2025/06/29 15:55:22 miod Exp $	*/
/*	$NetBSD: ioasic_subr.c,v 1.3 2001/11/13 06:26:10 lukem Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Keith Bostic, Chris G. Demetriou
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
#include <dev/tc/tcvar.h>
#include <dev/tc/ioasicvar.h>

int     ioasicprint(void *, const char *);

int
ioasicprint(void *aux, const char *pnp)
{
	struct ioasicdev_attach_args *d = aux;

	if (pnp)
		printf("%s at %s", d->iada_modname, pnp);
	printf(" offset 0x%lx", (long)d->iada_offset);
	return (UNCONF);
}

int
ioasic_submatch(void *vcf, struct ioasicdev_attach_args *d)
{
	struct cfdata *match = vcf;

	return ((match->ioasiccf_offset == d->iada_offset) ||
		(match->ioasiccf_offset == IOASIC_OFFSET_UNKNOWN));
}

void
ioasic_attach_devs(struct ioasic_softc *sc, struct ioasic_dev *ioasic_devs,
    int ioasic_ndevs)
{
	struct ioasicdev_attach_args idev;
	int i;

	/*
	 * Try to configure each device.
	 */
	for (i = 0; i < ioasic_ndevs; i++) {
		strncpy(idev.iada_modname, ioasic_devs[i].iad_modname,
			TC_ROM_LLEN);
		idev.iada_modname[TC_ROM_LLEN] = '\0';
		idev.iada_offset = ioasic_devs[i].iad_offset;
		idev.iada_addr = sc->sc_base + ioasic_devs[i].iad_offset;
		idev.iada_cookie = ioasic_devs[i].iad_cookie;

		/* Tell the autoconfig machinery we've found the hardware. */
		config_found(&sc->sc_dv, &idev, ioasicprint);
	}
}
