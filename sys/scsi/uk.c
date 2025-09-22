/*	$OpenBSD: uk.c,v 1.26 2021/10/24 16:57:30 mpi Exp $	*/
/*	$NetBSD: uk.c,v 1.15 1996/03/17 00:59:57 thorpej Exp $	*/

/*
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
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

/*
 * Dummy driver for a device we can't identify.
 * Originally by Julian Elischer (julian@tfs.com)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/vnode.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsiconf.h>

#define	UKUNIT(z)	(minor(z))

struct uk_softc {
	struct device		sc_dev;
	struct scsi_link	*sc_link; /* all the inter level info */
};

int	ukmatch(struct device *, void *, void *);
void	ukattach(struct device *, struct device *, void *);
int	ukdetach(struct device *, int);

const struct cfattach uk_ca = {
	sizeof(struct uk_softc), ukmatch, ukattach, ukdetach
};

struct cfdriver uk_cd = {
	NULL, "uk", DV_DULL
};

#define uklookup(unit) (struct uk_softc *)device_lookup(&uk_cd, (unit))

int
ukmatch(struct device *parent, void *match, void *aux)
{
	return 1;
}

/*
 * The routine called by the low level scsi routine when it discovers
 * a device suitable for this driver.
 */
void
ukattach(struct device *parent, struct device *self, void *aux)
{
	struct uk_softc			*sc = (void *)self;
	struct scsi_attach_args		*sa = aux;
	struct scsi_link		*link = sa->sa_sc_link;

	SC_DEBUG(link, SDEV_DB2, ("ukattach: "));

	/* Store information needed to contact our base driver. */
	sc->sc_link = link;
	link->device_softc = sc;
	link->openings = 1;

	printf("\n");
}

int
ukdetach(struct device *self, int flags)
{
	int				bmaj, cmaj, mn;

	mn = self->dv_unit;

	for (bmaj = 0; bmaj < nblkdev; bmaj++)
		if (bdevsw[bmaj].d_open == ukopen)
			vdevgone(bmaj, mn, mn, VBLK);
	for (cmaj = 0; cmaj < nchrdev; cmaj++)
		if (cdevsw[cmaj].d_open == ukopen)
			vdevgone(cmaj, mn, mn, VCHR);

	return 0;
}

/*
 * Open the device.
 */
int
ukopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct uk_softc			*sc;
	struct scsi_link		*link;
	int				 unit;

	unit = UKUNIT(dev);
	sc = uklookup(unit);
	if (sc == NULL)
		return ENXIO;

	link = sc->sc_link;

	SC_DEBUG(link, SDEV_DB1, ("ukopen: dev=0x%x (unit %d (of %d))\n",
	    dev, unit, uk_cd.cd_ndevs));

	/* Only allow one at a time. */
	if (ISSET(link->flags, SDEV_OPEN)) {
		device_unref(&sc->sc_dev);
		return EBUSY;
	}

	SET(link->flags, SDEV_OPEN);

	SC_DEBUG(link, SDEV_DB3, ("open complete\n"));

	device_unref(&sc->sc_dev);
	return 0;
}

/*
 * Close the device. Called only if we are the LAST
 * occurrence of an open device.
 */
int
ukclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct uk_softc			*sc;

	sc = uklookup(UKUNIT(dev));
	if (sc == NULL)
		return ENXIO;

	SC_DEBUG(sc->sc_link, SDEV_DB1, ("closing\n"));
	CLR(sc->sc_link->flags, SDEV_OPEN);

	device_unref(&sc->sc_dev);
	return 0;
}

/*
 * Perform special action on behalf of the user
 * Only does generic scsi ioctls.
 */
int
ukioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct uk_softc			*sc;
	int				 rv;

	sc = uklookup(UKUNIT(dev));
	if (sc == NULL)
		return ENXIO;

	rv = scsi_do_ioctl(sc->sc_link, cmd, addr, flag);

	device_unref(&sc->sc_dev);
	return rv;
}
