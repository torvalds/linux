/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999 Nicolas Souchu, Michael Smith
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include "opt_ppb_1284.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <sys/fcntl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>

#ifdef PERIPH_1284
#include <sys/malloc.h>
#include <dev/ppbus/ppb_1284.h>
#endif

#include <dev/ppbus/ppi.h>

#include "ppbus_if.h"

#include <dev/ppbus/ppbio.h>

#define BUFSIZE		512

struct ppi_data {
    device_t	ppi_device;
    struct cdev *ppi_cdev;
    struct sx	ppi_lock;
    int		ppi_flags;
#define HAVE_PPBUS	(1<<0)

    int		ppi_mode;			/* IEEE1284 mode */
    char	ppi_buffer[BUFSIZE];

#ifdef PERIPH_1284
    struct resource *intr_resource;	/* interrupt resource */
    void *intr_cookie;			/* interrupt registration cookie */
#endif /* PERIPH_1284 */
};

#define DEVTOSOFTC(dev) \
	((struct ppi_data *)device_get_softc(dev))

static devclass_t ppi_devclass;

#ifdef PERIPH_1284
static void	ppiintr(void *arg);
#endif

static	d_open_t	ppiopen;
static	d_close_t	ppiclose;
static	d_ioctl_t	ppiioctl;
static	d_write_t	ppiwrite;
static	d_read_t	ppiread;

static struct cdevsw ppi_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	ppiopen,
	.d_close =	ppiclose,
	.d_read =	ppiread,
	.d_write =	ppiwrite,
	.d_ioctl =	ppiioctl,
	.d_name =	"ppi",
};

#ifdef PERIPH_1284

static void
ppi_enable_intr(device_t ppidev)
{
	char r;
	device_t ppbus = device_get_parent(ppidev);

	r = ppb_rctr(ppbus);
	ppb_wctr(ppbus, r | IRQENABLE);

	return;
}

static void
ppi_disable_intr(device_t ppidev)
{
	char r;
	device_t ppbus = device_get_parent(ppidev);

	r = ppb_rctr(ppbus);
	ppb_wctr(ppbus, r & ~IRQENABLE);

	return;
}

#endif /* PERIPH_1284 */

static void
ppi_identify(driver_t *driver, device_t parent)
{

	device_t dev;

	dev = device_find_child(parent, "ppi", -1);
	if (!dev)
		BUS_ADD_CHILD(parent, 0, "ppi", -1);
}

/*
 * ppi_probe()
 */
static int
ppi_probe(device_t dev)
{
	struct ppi_data *ppi;

	/* probe is always ok */
	device_set_desc(dev, "Parallel I/O");

	ppi = DEVTOSOFTC(dev);

	return (0);
}

/*
 * ppi_attach()
 */
static int
ppi_attach(device_t dev)
{
	struct ppi_data *ppi = DEVTOSOFTC(dev);
#ifdef PERIPH_1284
	int error, rid = 0;

	/* declare our interrupt handler */
	ppi->intr_resource = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (ppi->intr_resource) {
		/* register our interrupt handler */
		error = bus_setup_intr(dev, ppi->intr_resource,
		    INTR_TYPE_TTY | INTR_MPSAFE, NULL, ppiintr, dev,
		    &ppi->intr_cookie);
		if (error) {
			bus_release_resource(dev, SYS_RES_IRQ, rid,
			    ppi->intr_resource);
			device_printf(dev,
			    "Unable to register interrupt handler\n");
			return (error);
		}
	}
#endif /* PERIPH_1284 */

	sx_init(&ppi->ppi_lock, "ppi");
	ppi->ppi_cdev = make_dev(&ppi_cdevsw, device_get_unit(dev),
		 UID_ROOT, GID_WHEEL,
		 0600, "ppi%d", device_get_unit(dev));
	if (ppi->ppi_cdev == NULL) {
		device_printf(dev, "Failed to create character device\n");
		return (ENXIO);
	}
	ppi->ppi_cdev->si_drv1 = ppi;
	ppi->ppi_device = dev;

	return (0);
}

static int
ppi_detach(device_t dev)
{
	struct ppi_data *ppi = DEVTOSOFTC(dev);

	destroy_dev(ppi->ppi_cdev);
#ifdef PERIPH_1284
	if (ppi->intr_resource != NULL) {
		bus_teardown_intr(dev, ppi->intr_resource, ppi->intr_cookie);
		bus_release_resource(dev, SYS_RES_IRQ, 0, ppi->intr_resource);
	}
#endif
	sx_destroy(&ppi->ppi_lock);
	return (0);
}

#ifdef PERIPH_1284
/*
 * Cable
 * -----
 *
 * Use an IEEE1284 compliant (DB25/DB25) cable with the following tricks:
 *
 * nStrobe   <-> nAck		1  <-> 10
 * nAutofd   <-> Busy		11 <-> 14
 * nSelectin <-> Select		17 <-> 13
 * nInit     <-> nFault		15 <-> 16
 *
 */
static void
ppiintr(void *arg)
{
	device_t ppidev = (device_t)arg;
	device_t ppbus = device_get_parent(ppidev);
	struct ppi_data *ppi = DEVTOSOFTC(ppidev);

	ppb_assert_locked(ppbus);
	ppi_disable_intr(ppidev);

	switch (ppb_1284_get_state(ppbus)) {

	/* accept IEEE1284 negotiation then wakeup a waiting process to
	 * continue negotiation at process level */
	case PPB_FORWARD_IDLE:
		/* Event 1 */
		if ((ppb_rstr(ppbus) & (SELECT | nBUSY)) ==
							(SELECT | nBUSY)) {
			/* IEEE1284 negotiation */
#ifdef DEBUG_1284
			printf("N");
#endif

			/* Event 2 - prepare for reading the ext. value */
			ppb_wctr(ppbus, (PCD | STROBE | nINIT) & ~SELECTIN);

			ppb_1284_set_state(ppbus, PPB_NEGOCIATION);

		} else {
#ifdef DEBUG_1284
			printf("0x%x", ppb_rstr(ppbus));
#endif
			ppb_peripheral_terminate(ppbus, PPB_DONTWAIT);
			break;
		}

		/* wake up any process waiting for negotiation from
		 * remote master host */

		/* XXX should set a variable to warn the process about
		 * the interrupt */

		wakeup(ppi);
		break;
	default:
#ifdef DEBUG_1284
		printf("?%d", ppb_1284_get_state(ppbus));
#endif
		ppb_1284_set_state(ppbus, PPB_FORWARD_IDLE);
		ppb_set_mode(ppbus, PPB_COMPATIBLE);
		break;
	}

	ppi_enable_intr(ppidev);

	return;
}
#endif /* PERIPH_1284 */

static int
ppiopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct ppi_data *ppi = dev->si_drv1;
	device_t ppidev = ppi->ppi_device;
	device_t ppbus = device_get_parent(ppidev);
	int res;

	sx_xlock(&ppi->ppi_lock);
	if (!(ppi->ppi_flags & HAVE_PPBUS)) {
		ppb_lock(ppbus);
		res = ppb_request_bus(ppbus, ppidev,
		    (flags & O_NONBLOCK) ? PPB_DONTWAIT : PPB_WAIT | PPB_INTR);
		ppb_unlock(ppbus);
		if (res) {
			sx_xunlock(&ppi->ppi_lock);
			return (res);
		}

		ppi->ppi_flags |= HAVE_PPBUS;
	}
	sx_xunlock(&ppi->ppi_lock);

	return (0);
}

static int
ppiclose(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct ppi_data *ppi = dev->si_drv1;
	device_t ppidev = ppi->ppi_device;
	device_t ppbus = device_get_parent(ppidev);

	sx_xlock(&ppi->ppi_lock);
	ppb_lock(ppbus);
#ifdef PERIPH_1284
	switch (ppb_1284_get_state(ppbus)) {
	case PPB_PERIPHERAL_IDLE:
		ppb_peripheral_terminate(ppbus, 0);
		break;
	case PPB_REVERSE_IDLE:
	case PPB_EPP_IDLE:
	case PPB_ECP_FORWARD_IDLE:
	default:
		ppb_1284_terminate(ppbus);
		break;
	}
#endif /* PERIPH_1284 */

	/* unregistration of interrupt forced by release */
	ppb_release_bus(ppbus, ppidev);
	ppb_unlock(ppbus);

	ppi->ppi_flags &= ~HAVE_PPBUS;
	sx_xunlock(&ppi->ppi_lock);

	return (0);
}

/*
 * ppiread()
 *
 * IEEE1284 compliant read.
 *
 * First, try negotiation to BYTE then NIBBLE mode
 * If no data is available, wait for it otherwise transfer as much as possible
 */
static int
ppiread(struct cdev *dev, struct uio *uio, int ioflag)
{
#ifdef PERIPH_1284
	struct ppi_data *ppi = dev->si_drv1;
	device_t ppidev = ppi->ppi_device;
	device_t ppbus = device_get_parent(ppidev);
	int len, error = 0;
	char *buffer;

	buffer = malloc(BUFSIZE, M_DEVBUF, M_WAITOK);

	ppb_lock(ppbus);
	switch (ppb_1284_get_state(ppbus)) {
	case PPB_PERIPHERAL_IDLE:
		ppb_peripheral_terminate(ppbus, 0);
		/* FALLTHROUGH */

	case PPB_FORWARD_IDLE:
		/* if can't negotiate NIBBLE mode then try BYTE mode,
		 * the peripheral may be a computer
		 */
		if ((ppb_1284_negociate(ppbus,
			ppi->ppi_mode = PPB_NIBBLE, 0))) {

			/* XXX Wait 2 seconds to let the remote host some
			 * time to terminate its interrupt
			 */
			ppb_sleep(ppbus, ppi, PPBPRI, "ppiread", 2 * hz);

			if ((error = ppb_1284_negociate(ppbus,
			    ppi->ppi_mode = PPB_BYTE, 0))) {
				ppb_unlock(ppbus);
				free(buffer, M_DEVBUF);
				return (error);
			}
		}
		break;

	case PPB_REVERSE_IDLE:
	case PPB_EPP_IDLE:
	case PPB_ECP_FORWARD_IDLE:
	default:
		break;
	}

#ifdef DEBUG_1284
	printf("N");
#endif
	/* read data */
	len = 0;
	while (uio->uio_resid) {
		error = ppb_1284_read(ppbus, ppi->ppi_mode,
		    buffer, min(BUFSIZE, uio->uio_resid), &len);
		ppb_unlock(ppbus);
		if (error)
			goto error;

		if (!len)
			goto error;		/* no more data */

#ifdef DEBUG_1284
		printf("d");
#endif
		if ((error = uiomove(buffer, len, uio)))
			goto error;
		ppb_lock(ppbus);
	}
	ppb_unlock(ppbus);

error:
	free(buffer, M_DEVBUF);
#else /* PERIPH_1284 */
	int error = ENODEV;
#endif

	return (error);
}

/*
 * ppiwrite()
 *
 * IEEE1284 compliant write
 *
 * Actually, this is the peripheral side of a remote IEEE1284 read
 *
 * The first part of the negotiation (IEEE1284 device detection) is
 * done at interrupt level, then the remaining is done by the writing
 * process
 *
 * Once negotiation done, transfer data
 */
static int
ppiwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
#ifdef PERIPH_1284
	struct ppi_data *ppi = dev->si_drv1;
	device_t ppidev = ppi->ppi_device;
	device_t ppbus = device_get_parent(ppidev);
	int len, error = 0, sent;
	char *buffer;

#if 0
	int ret;

	#define ADDRESS		MS_PARAM(0, 0, MS_TYP_PTR)
	#define LENGTH		MS_PARAM(0, 1, MS_TYP_INT)

	struct ppb_microseq msq[] = {
		  { MS_OP_PUT, { MS_UNKNOWN, MS_UNKNOWN, MS_UNKNOWN } },
		  MS_RET(0)
	};

	buffer = malloc(BUFSIZE, M_DEVBUF, M_WAITOK);
	ppb_lock(ppbus);

	/* negotiate ECP mode */
	if (ppb_1284_negociate(ppbus, PPB_ECP, 0)) {
		printf("ppiwrite: ECP negotiation failed\n");
	}

	while (!error && (len = min(uio->uio_resid, BUFSIZE))) {
		ppb_unlock(ppbus);
		uiomove(buffer, len, uio);

		ppb_MS_init_msq(msq, 2, ADDRESS, buffer, LENGTH, len);

		ppb_lock(ppbus);
		error = ppb_MS_microseq(ppbus, msq, &ret);
	}
#else
	buffer = malloc(BUFSIZE, M_DEVBUF, M_WAITOK);
	ppb_lock(ppbus);
#endif

	/* we have to be peripheral to be able to send data, so
	 * wait for the appropriate state
	 */
 	if (ppb_1284_get_state(ppbus) < PPB_PERIPHERAL_NEGOCIATION)
		ppb_1284_terminate(ppbus);

 	while (ppb_1284_get_state(ppbus) != PPB_PERIPHERAL_IDLE) {
		/* XXX should check a variable before sleeping */
#ifdef DEBUG_1284
		printf("s");
#endif

		ppi_enable_intr(ppidev);

		/* sleep until IEEE1284 negotiation starts */
		error = ppb_sleep(ppbus, ppi, PCATCH | PPBPRI, "ppiwrite", 0);

		switch (error) {
		case 0:
			/* negotiate peripheral side with BYTE mode */
			ppb_peripheral_negociate(ppbus, PPB_BYTE, 0);
			break;
		case EWOULDBLOCK:
			break;
		default:
			goto error;
		}
	}
#ifdef DEBUG_1284
	printf("N");
#endif

	/* negotiation done, write bytes to master host */
	while ((len = min(uio->uio_resid, BUFSIZE)) != 0) {
		ppb_unlock(ppbus);
		uiomove(buffer, len, uio);
		ppb_lock(ppbus);
		if ((error = byte_peripheral_write(ppbus,
						buffer, len, &sent)))
			goto error;
#ifdef DEBUG_1284
		printf("d");
#endif
	}

error:
	ppb_unlock(ppbus);
	free(buffer, M_DEVBUF);
#else /* PERIPH_1284 */
	int error = ENODEV;
#endif

	return (error);
}

static int
ppiioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	struct ppi_data *ppi = dev->si_drv1;
	device_t ppidev = ppi->ppi_device;
	device_t ppbus = device_get_parent(ppidev);
	int error = 0;
	u_int8_t *val = (u_int8_t *)data;

	ppb_lock(ppbus);
	switch (cmd) {

	case PPIGDATA:			/* get data register */
		*val = ppb_rdtr(ppbus);
		break;
	case PPIGSTATUS:		/* get status bits */
		*val = ppb_rstr(ppbus);
		break;
	case PPIGCTRL:			/* get control bits */
		*val = ppb_rctr(ppbus);
		break;
	case PPIGEPPD:			/* get EPP data bits */
		*val = ppb_repp_D(ppbus);
		break;
	case PPIGECR:			/* get ECP bits */
		*val = ppb_recr(ppbus);
		break;
	case PPIGFIFO:			/* read FIFO */
		*val = ppb_rfifo(ppbus);
		break;
	case PPISDATA:			/* set data register */
		ppb_wdtr(ppbus, *val);
		break;
	case PPISSTATUS:		/* set status bits */
		ppb_wstr(ppbus, *val);
		break;
	case PPISCTRL:			/* set control bits */
		ppb_wctr(ppbus, *val);
		break;
	case PPISEPPD:			/* set EPP data bits */
		ppb_wepp_D(ppbus, *val);
		break;
	case PPISECR:			/* set ECP bits */
		ppb_wecr(ppbus, *val);
		break;
	case PPISFIFO:			/* write FIFO */
		ppb_wfifo(ppbus, *val);
		break;
	case PPIGEPPA:			/* get EPP address bits */
		*val = ppb_repp_A(ppbus);
		break;
	case PPISEPPA:			/* set EPP address bits */
		ppb_wepp_A(ppbus, *val);
		break;
	default:
		error = ENOTTY;
		break;
	}
	ppb_unlock(ppbus);

	return (error);
}

static device_method_t ppi_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	ppi_identify),
	DEVMETHOD(device_probe,		ppi_probe),
	DEVMETHOD(device_attach,	ppi_attach),
	DEVMETHOD(device_detach,	ppi_detach),

	{ 0, 0 }
};

static driver_t ppi_driver = {
	"ppi",
	ppi_methods,
	sizeof(struct ppi_data),
};
DRIVER_MODULE(ppi, ppbus, ppi_driver, ppi_devclass, 0, 0);
MODULE_DEPEND(ppi, ppbus, 1, 1, 1);
