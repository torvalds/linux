/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2000-2004
 *          Diomidis D. Spinellis, Athens, Greece
 *      All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer as
 *     the first lines of this file unmodified.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY Diomidis D. Spinellis ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL Diomidis D. Spinellis BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: pbio.c,v 1.12 2003/10/11 13:05:08 dds Exp dds $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>		/* SYSINIT stuff */
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/conf.h>		/* cdevsw stuff */
#include <sys/malloc.h>		/* malloc region definitions */
#include <sys/module.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <dev/pbio/pbioio.h>		/* pbio IOCTL definitions */
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <isa/isavar.h>

/* Function prototypes (these should all be static) */
static	d_open_t	pbioopen;
static	d_close_t	pbioclose;
static	d_read_t	pbioread;
static	d_write_t	pbiowrite;
static	d_ioctl_t	pbioioctl;
static	d_poll_t	pbiopoll;
static	int		pbioprobe(device_t);
static	int		pbioattach(device_t);

/* Device registers */
#define	PBIO_PORTA	0
#define	PBIO_PORTB	1
#define	PBIO_PORTC	2
#define	PBIO_CFG	3
#define	PBIO_IOSIZE	4

/* Per-port buffer size */
#define	PBIO_BUFSIZ 64

/* Number of /dev entries */
#define	PBIO_NPORTS 4

/* I/O port range */
#define	IO_PBIOSIZE 4

static char *port_names[] = {"a", "b", "ch", "cl"};

#define	PBIO_PNAME(n)		(port_names[(n)])

#define	UNIT(dev)		(dev2unit(dev) >> 2)
#define	PORT(dev)		(dev2unit(dev) & 0x3)

#define	PBIOPRI	((PZERO + 5) | PCATCH)

static struct cdevsw pbio_cdevsw = {
	.d_version = D_VERSION,
	.d_flags = D_NEEDGIANT,
	.d_open = pbioopen,
	.d_close = pbioclose,
	.d_read = pbioread,
	.d_write = pbiowrite,
	.d_ioctl = pbioioctl,
	.d_poll = pbiopoll,
	.d_name = "pbio"
};

/*
 * Data specific to each I/O port
 */
struct portdata {
	struct cdev *port;
	int	diff;			/* When true read only differences */
	int	ipace;			/* Input pace */
	int	opace;			/* Output pace */
	char	oldval;			/* Last value read */
	char	buff[PBIO_BUFSIZ];	/* Per-port data buffer */
};

/*
 * One of these per allocated device
 */
struct pbio_softc {
	struct portdata pd[PBIO_NPORTS];/* Per port data */
	int	iomode;			/* Virtualized I/O mode port value */
					/* The real port is write-only */
	struct resource *res;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
};

typedef	struct pbio_softc *sc_p;

static device_method_t pbio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pbioprobe),
	DEVMETHOD(device_attach,	pbioattach),
	{ 0, 0 }
};

static	devclass_t	pbio_devclass;
#define	pbio_addr(unit) \
	    ((struct pbio_softc *) devclass_get_softc(pbio_devclass, unit))

static char driver_name[] = "pbio";

static driver_t pbio_driver = {
	driver_name,
	pbio_methods,
	sizeof(struct pbio_softc),
};

DRIVER_MODULE(pbio, isa, pbio_driver, pbio_devclass, 0, 0);

static __inline uint8_t
pbinb(struct pbio_softc *scp, int off)
{

	return bus_space_read_1(scp->bst, scp->bsh, off);
}

static __inline void
pboutb(struct pbio_softc *scp, int off, uint8_t val)
{

	bus_space_write_1(scp->bst, scp->bsh, off, val);
}


static int
pbioprobe(device_t dev)
{
	int		rid;
	struct pbio_softc *scp = device_get_softc(dev);
#ifdef GENERIC_PBIO_PROBE
	unsigned char val;
#endif

	if (isa_get_logicalid(dev))		/* skip PnP probes */
		return (ENXIO);
	rid = 0;
	scp->res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
	    IO_PBIOSIZE, RF_ACTIVE);
	if (scp->res == NULL)
		return (ENXIO);

#ifdef GENERIC_PBIO_PROBE
	scp->bst = rman_get_bustag(scp->res);
	scp->bsh = rman_get_bushandle(scp->res);
	/*
	 * try see if the device is there.
	 * This probe works only if the device has no I/O attached to it
	 * XXX Better allow flags to abort testing
	 */
	/* Set all ports to output */
	pboutb(scp, PBIO_CFG, 0x80);
	printf("pbio val(CFG: 0x%03x)=0x%02x (should be 0x80)\n",
		rman_get_start(scp->res), pbinb(scp, PBIO_CFG));
	pboutb(scp, PBIO_PORTA, 0xa5);
	val = pbinb(scp, PBIO_PORTA);
	printf("pbio val=0x%02x (should be 0xa5)\n", val);
	if (val != 0xa5) {
		bus_release_resource(dev, SYS_RES_IOPORT, rid, sc->res);
		return (ENXIO);
	}
	pboutb(scp, PBIO_PORTA, 0x5a);
	val = pbinb(scp, PBIO_PORTA);
	printf("pbio val=0x%02x (should be 0x5a)\n", val);
	if (val != 0x5a) {
		bus_release_resource(dev, SYS_RES_IOPORT, rid, sc->res);
		return (ENXIO);
	}
#endif
	device_set_desc(dev, "Intel 8255 PPI (basic mode)");
	/* Set all ports to input */
	/* pboutb(scp, PBIO_CFG, 0x9b); */
	bus_release_resource(dev, SYS_RES_IOPORT, rid, scp->res);
	return (0);
}

/*
 * Called if the probe succeeded.
 * We can be destructive here as we know we have the device.
 * we can also trust the unit number.
 */
static int
pbioattach (device_t dev)
{
	int unit;
	int i;
	int		rid;
	struct pbio_softc *sc;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	rid = 0;
	sc->res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
	    IO_PBIOSIZE, RF_ACTIVE);
	if (sc->res == NULL)
		return (ENXIO);
	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	/*
	 * Store whatever seems wise.
	 */
	sc->iomode = 0x9b;		/* All ports to input */

	for (i = 0; i < PBIO_NPORTS; i++)
		sc->pd[i].port = make_dev(&pbio_cdevsw, (unit << 2) + i, 0, 0,
		    0600, "pbio%d%s", unit, PBIO_PNAME(i));
	return (0);
}

static int
pbioioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct pbio_softc *scp;
	int port, unit;
	
	unit = UNIT(dev);
	port = PORT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);
	switch (cmd) {
	case PBIO_SETDIFF:
		scp->pd[port].diff = *(int *)data;
		break;
	case PBIO_SETIPACE:
		scp->pd[port].ipace = *(int *)data;
		break;
	case PBIO_SETOPACE:
		scp->pd[port].opace = *(int *)data;
		break;
	case PBIO_GETDIFF:
		*(int *)data = scp->pd[port].diff;
		break;
	case PBIO_GETIPACE:
		*(int *)data = scp->pd[port].ipace;
		break;
	case PBIO_GETOPACE:
		*(int *)data = scp->pd[port].opace;
		break;
	default:
		return ENXIO;
	}
	return (0);
}

static  int
pbioopen(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct pbio_softc *scp;
	int ocfg, port, unit;
	int portbit;			/* Port configuration bit */
	
	unit = UNIT(dev);
	port = PORT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);
	
	switch (port) {
	case 0: portbit = 0x10; break;	/* Port A */
	case 1: portbit = 0x02; break;	/* Port B */
	case 2: portbit = 0x08; break;	/* Port CH */
	case 3: portbit = 0x01; break;	/* Port CL */
	default: return (ENODEV);
	}
	ocfg = scp->iomode;

	if (oflags & FWRITE)
		/* Writing == output; zero the bit */
		pboutb(scp, PBIO_CFG, scp->iomode = (ocfg & (~portbit)));
	else if (oflags & FREAD)
		/* Reading == input; set the bit */
		pboutb(scp, PBIO_CFG, scp->iomode = (ocfg | portbit));
	else
		return (EACCES);

	return (0);
}

static  int
pbioclose(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct pbio_softc *scp;
	int unit;
	
	unit = UNIT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);
	
	return (0);
}

/*
 * Return the value of a given port on a given I/O base address
 * Handles the split C port nibbles and blocking
 */
static int
portval(int port, struct pbio_softc *scp, char *val)
{
	int err;

	for (;;) {
		switch (port) {
		case 0:
			*val = pbinb(scp, PBIO_PORTA);
			break;
		case 1:
			*val = pbinb(scp, PBIO_PORTB);
			break;
		case 2:
			*val = (pbinb(scp, PBIO_PORTC) >> 4) & 0xf;
			break;
		case 3:
			*val = pbinb(scp, PBIO_PORTC) & 0xf;
			break;
		default:
			*val = 0;
			break;
		}
		if (scp->pd[port].diff) {
			if (*val != scp->pd[port].oldval) {
				scp->pd[port].oldval = *val;
				return (0);
			}
			err = tsleep((caddr_t)&(scp->pd[port].diff), PBIOPRI,
				     "pbiopl", max(1, scp->pd[port].ipace));
			if (err == EINTR)
				return (EINTR);
		} else
			return (0);
	}
}

static  int
pbioread(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct pbio_softc *scp;
	int err, i, port, ret, toread, unit;
	char val;
	
	unit = UNIT(dev);
	port = PORT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);

	while (uio->uio_resid > 0) {
		toread = min(uio->uio_resid, PBIO_BUFSIZ);
		if ((ret = uiomove(scp->pd[port].buff, toread, uio)) != 0)
			return (ret);
		for (i = 0; i < toread; i++) {
			if ((err = portval(port, scp, &val)) != 0)
				return (err);
			scp->pd[port].buff[i] = val;
			if (!scp->pd[port].diff && scp->pd[port].ipace)
				tsleep((caddr_t)&(scp->pd[port].ipace), PBIOPRI,
					"pbioip", scp->pd[port].ipace);
		}
	}
	return 0;
}

static int
pbiowrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct pbio_softc *scp;
	int i, port, ret, towrite, unit;
	char val, oval;
	
	unit = UNIT(dev);
	port = PORT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);

	while (uio->uio_resid > 0) {
		towrite = min(uio->uio_resid, PBIO_BUFSIZ);
		if ((ret = uiomove(scp->pd[port].buff, towrite, uio)) != 0)
			return (ret);
		for (i = 0; i < towrite; i++) {
			val = scp->pd[port].buff[i];
			switch (port) {
			case 0:
				pboutb(scp, PBIO_PORTA, val);
				break;
			case 1:
				pboutb(scp, PBIO_PORTB, val);
				break;
			case 2:
				oval = pbinb(scp, PBIO_PORTC);
				oval &= 0xf;
				val <<= 4;
				pboutb(scp, PBIO_PORTC, val | oval);
				break;
			case 3:
				oval = pbinb(scp, PBIO_PORTC);
				oval &= 0xf0;
				val &= 0xf;
				pboutb(scp, PBIO_PORTC, oval | val);
				break;
			}
			if (scp->pd[port].opace)
				tsleep((caddr_t)&(scp->pd[port].opace),
					PBIOPRI, "pbioop",
					scp->pd[port].opace);
		}
	}
	return (0);
}

static  int
pbiopoll(struct cdev *dev, int which, struct thread *td)
{
	struct pbio_softc *scp;
	int unit;
	
	unit = UNIT(dev);
	scp = pbio_addr(unit);
	if (scp == NULL)
		return (ENODEV);
	
	/*
	 * Do processing
	 */
	return (0); /* this is the wrong value I'm sure */
}
