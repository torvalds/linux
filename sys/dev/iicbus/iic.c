/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu
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
 * $FreeBSD$
 *
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/errno.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iic.h>

#include "iicbus_if.h"

struct iic_softc {
	device_t sc_dev;
	struct cdev *sc_devnode;
};

struct iic_cdevpriv {
	struct sx lock;
	struct iic_softc *sc;
	bool started;
	uint8_t addr;
};


#define	IIC_LOCK(cdp)			sx_xlock(&(cdp)->lock)
#define	IIC_UNLOCK(cdp)			sx_xunlock(&(cdp)->lock)

static MALLOC_DEFINE(M_IIC, "iic", "I2C device data");

static int iic_probe(device_t);
static int iic_attach(device_t);
static int iic_detach(device_t);
static void iic_identify(driver_t *driver, device_t parent);
static void iicdtor(void *data);
static int iicuio_move(struct iic_cdevpriv *priv, struct uio *uio, int last);
static int iicuio(struct cdev *dev, struct uio *uio, int ioflag);
static int iicrdwr(struct iic_cdevpriv *priv, struct iic_rdwr_data *d, int flags);

static devclass_t iic_devclass;

static device_method_t iic_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	iic_identify),
	DEVMETHOD(device_probe,		iic_probe),
	DEVMETHOD(device_attach,	iic_attach),
	DEVMETHOD(device_detach,	iic_detach),

	/* iicbus interface */
	DEVMETHOD(iicbus_intr,		iicbus_generic_intr),

	{ 0, 0 }
};

static driver_t iic_driver = {
	"iic",
	iic_methods,
	sizeof(struct iic_softc),
};

static	d_open_t	iicopen;
static	d_ioctl_t	iicioctl;

static struct cdevsw iic_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	iicopen,
	.d_read =	iicuio,
	.d_write =	iicuio,
	.d_ioctl =	iicioctl,
	.d_name =	"iic",
};

static void
iic_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "iic", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "iic", -1);
}

static int
iic_probe(device_t dev)
{
	if (iicbus_get_addr(dev) > 0)
		return (ENXIO);

	device_set_desc(dev, "I2C generic I/O");

	return (0);
}
	
static int
iic_attach(device_t dev)
{
	struct iic_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_devnode = make_dev(&iic_cdevsw, device_get_unit(dev),
			UID_ROOT, GID_WHEEL,
			0600, "iic%d", device_get_unit(dev));
	if (sc->sc_devnode == NULL) {
		device_printf(dev, "failed to create character device\n");
		return (ENXIO);
	}
	sc->sc_devnode->si_drv1 = sc;

	return (0);
}

static int
iic_detach(device_t dev)
{
	struct iic_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_devnode)
		destroy_dev(sc->sc_devnode);

	return (0);
}

static int
iicopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct iic_cdevpriv *priv;
	int error;

	priv = malloc(sizeof(*priv), M_IIC, M_WAITOK | M_ZERO);

	sx_init(&priv->lock, "iic");
	priv->sc = dev->si_drv1;

	error = devfs_set_cdevpriv(priv, iicdtor); 
	if (error != 0)
		free(priv, M_IIC);

	return (error);
}

static void
iicdtor(void *data)
{
	device_t iicdev, parent;
	struct iic_cdevpriv *priv;

	priv = data;
	KASSERT(priv != NULL, ("iic cdevpriv should not be NULL!"));

	iicdev = priv->sc->sc_dev;
	parent = device_get_parent(iicdev);

	if (priv->started) {
		iicbus_stop(parent);
		iicbus_reset(parent, IIC_UNKNOWN, 0, NULL);
		iicbus_release_bus(parent, iicdev);
	}

	sx_destroy(&priv->lock);
	free(priv, M_IIC);
}

static int
iicuio_move(struct iic_cdevpriv *priv, struct uio *uio, int last)
{
	device_t parent;
	int error, num_bytes, transferred_bytes, written_bytes;
	char buffer[128];

	parent = device_get_parent(priv->sc->sc_dev);
	error = 0;

	/*
	 * We can only transfer up to sizeof(buffer) bytes in 1 shot, so loop until
	 * everything has been transferred.
	*/
	while ((error == 0) && (uio->uio_resid > 0)) {

		num_bytes = MIN(uio->uio_resid, sizeof(buffer));
		transferred_bytes = 0;

		if (uio->uio_rw == UIO_WRITE) {
			error = uiomove(buffer, num_bytes, uio);

			while ((error == 0) && (transferred_bytes < num_bytes)) {
				written_bytes = 0;
				error = iicbus_write(parent, &buffer[transferred_bytes],
				    num_bytes - transferred_bytes, &written_bytes, 0);
				transferred_bytes += written_bytes;
			}
				
		} else if (uio->uio_rw == UIO_READ) {
			error = iicbus_read(parent, buffer,
			    num_bytes, &transferred_bytes,
			    ((uio->uio_resid <= sizeof(buffer)) ? last : 0), 0);
			if (error == 0)
				error = uiomove(buffer, transferred_bytes, uio);
		}
	}

	return (error);
}

static int
iicuio(struct cdev *dev, struct uio *uio, int ioflag)
{
	device_t parent;
	struct iic_cdevpriv *priv;
	int error;
	uint8_t addr;

	priv = NULL;
	error = devfs_get_cdevpriv((void**)&priv);

	if (error != 0)
		return (error);
	KASSERT(priv != NULL, ("iic cdevpriv should not be NULL!"));

	IIC_LOCK(priv);
	if (priv->started || (priv->addr == 0)) {
		IIC_UNLOCK(priv);
		return (ENXIO);
	}
	parent = device_get_parent(priv->sc->sc_dev);

	error = iicbus_request_bus(parent, priv->sc->sc_dev,
	    (ioflag & O_NONBLOCK) ? IIC_DONTWAIT : (IIC_WAIT | IIC_INTR));
	if (error != 0) {
		IIC_UNLOCK(priv);
		return (error);
	}

	if (uio->uio_rw == UIO_READ)
		addr = priv->addr | LSB;
	else
		addr = priv->addr & ~LSB;

	error = iicbus_start(parent, addr, 0);
	if (error != 0)
	{
		iicbus_release_bus(parent, priv->sc->sc_dev);
		IIC_UNLOCK(priv);
		return (error);
	}

	error = iicuio_move(priv, uio, IIC_LAST_READ);

	iicbus_stop(parent);
	iicbus_release_bus(parent, priv->sc->sc_dev);
	IIC_UNLOCK(priv);
	return (error);
}

static int
iicrdwr(struct iic_cdevpriv *priv, struct iic_rdwr_data *d, int flags)
{
	struct iic_msg *buf, *m;
	void **usrbufs;
	device_t iicdev, parent;
	int error;
	uint32_t i;

	iicdev = priv->sc->sc_dev;
	parent = device_get_parent(iicdev);
	error = 0;

	if (d->nmsgs > IIC_RDRW_MAX_MSGS)
		return (EINVAL);

	buf = malloc(sizeof(*d->msgs) * d->nmsgs, M_IIC, M_WAITOK);

	error = copyin(d->msgs, buf, sizeof(*d->msgs) * d->nmsgs);
	if (error != 0) {
		free(buf, M_IIC);
		return (error);
	}

	/* Alloc kernel buffers for userland data, copyin write data */
	usrbufs = malloc(sizeof(void *) * d->nmsgs, M_IIC, M_WAITOK | M_ZERO);

	for (i = 0; i < d->nmsgs; i++) {
		m = &(buf[i]);
		usrbufs[i] = m->buf;

		/*
		 * At least init the buffer to NULL so we can safely free() it later.
		 * If the copyin() to buf failed, don't try to malloc bogus m->len.
		 */
		m->buf = NULL;
		if (error != 0)
			continue;

		/* m->len is uint16_t, so allocation size is capped at 64K. */
		m->buf = malloc(m->len, M_IIC, M_WAITOK);
		if (!(m->flags & IIC_M_RD))
			error = copyin(usrbufs[i], m->buf, m->len);
	}

	if (error == 0)
		error = iicbus_request_bus(parent, iicdev,
		    (flags & O_NONBLOCK) ? IIC_DONTWAIT : (IIC_WAIT | IIC_INTR));

	if (error == 0) {
		error = iicbus_transfer(iicdev, buf, d->nmsgs);
		iicbus_release_bus(parent, iicdev);
	}

	/* Copyout all read segments, free up kernel buffers */
	for (i = 0; i < d->nmsgs; i++) {
		m = &(buf[i]);
		if ((error == 0) && (m->flags & IIC_M_RD))
			error = copyout(m->buf, usrbufs[i], m->len);
		free(m->buf, M_IIC);
	}

	free(usrbufs, M_IIC);
	free(buf, M_IIC);
	return (error);
}

static int
iicioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	device_t parent, iicdev;
	struct iiccmd *s;
	struct uio ubuf;
	struct iovec uvec;
	struct iic_cdevpriv *priv;
	int error;

	s = (struct iiccmd *)data;
	error = devfs_get_cdevpriv((void**)&priv);
	if (error != 0)
		return (error);

	KASSERT(priv != NULL, ("iic cdevpriv should not be NULL!"));

	iicdev = priv->sc->sc_dev;
	parent = device_get_parent(iicdev);
	IIC_LOCK(priv);


	switch (cmd) {
	case I2CSTART:
		if (priv->started) {
			error = EINVAL;
			break;
		}
		error = iicbus_request_bus(parent, iicdev,
		    (flags & O_NONBLOCK) ? IIC_DONTWAIT : (IIC_WAIT | IIC_INTR));

		if (error == 0)
			error = iicbus_start(parent, s->slave, 0);

		if (error == 0) {
			priv->addr = s->slave;
			priv->started = true;
		} else
			iicbus_release_bus(parent, iicdev);

		break;

	case I2CSTOP:
		if (priv->started) {
			error = iicbus_stop(parent);
			iicbus_release_bus(parent, iicdev);
			priv->started = false;
		}

		break;

	case I2CRSTCARD:
		/*
		 * Bus should be owned before we reset it.
		 * We allow the bus to be already owned as the result of an in-progress
		 * sequence; however, bus reset will always be followed by release
		 * (a new start is presumably needed for I/O anyway). */ 
		if (!priv->started)	
			error = iicbus_request_bus(parent, iicdev,
			    (flags & O_NONBLOCK) ? IIC_DONTWAIT : (IIC_WAIT | IIC_INTR));

		if (error == 0) {
			error = iicbus_reset(parent, IIC_UNKNOWN, 0, NULL);
			/*
			 * Ignore IIC_ENOADDR as it only means we have a master-only
			 * controller.
			 */
			if (error == IIC_ENOADDR)
				error = 0;

			iicbus_release_bus(parent, iicdev);
			priv->started = false;
		}
		break;

	case I2CWRITE:
		if (!priv->started) {
			error = EINVAL;
			break;
		}
		uvec.iov_base = s->buf;
		uvec.iov_len = s->count;
		ubuf.uio_iov = &uvec;
		ubuf.uio_iovcnt = 1;
		ubuf.uio_segflg = UIO_USERSPACE;
		ubuf.uio_td = td;
		ubuf.uio_resid = s->count;
		ubuf.uio_offset = 0;
		ubuf.uio_rw = UIO_WRITE;
		error = iicuio_move(priv, &ubuf, 0);
		break;

	case I2CREAD:
		if (!priv->started) {
			error = EINVAL;
			break;
		}
		uvec.iov_base = s->buf;
		uvec.iov_len = s->count;
		ubuf.uio_iov = &uvec;
		ubuf.uio_iovcnt = 1;
		ubuf.uio_segflg = UIO_USERSPACE;
		ubuf.uio_td = td;
		ubuf.uio_resid = s->count;
		ubuf.uio_offset = 0;
		ubuf.uio_rw = UIO_READ;
		error = iicuio_move(priv, &ubuf, s->last);
		break;

	case I2CRDWR:
		/*
		 * The rdwr list should be a self-contained set of
		 * transactions.  Fail if another transaction is in progress.
                 */
		if (priv->started) {
			error = EINVAL;
			break;
		}

		error = iicrdwr(priv, (struct iic_rdwr_data *)data, flags);

		break;

	case I2CRPTSTART:
		if (!priv->started) {
			error = EINVAL;
			break;
		}
		error = iicbus_repeated_start(parent, s->slave, 0);
		break;

	case I2CSADDR:
		priv->addr = *((uint8_t*)data);
		break;

	default:
		error = ENOTTY;
	}

	IIC_UNLOCK(priv);
	return (error);
}

DRIVER_MODULE(iic, iicbus, iic_driver, iic_devclass, 0, 0);
MODULE_DEPEND(iic, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(iic, 1);
