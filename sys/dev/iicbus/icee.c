/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 M. Warner Losh.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/*
 * Generic IIC eeprom support, modeled after the AT24C family of products.
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <machine/bus.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbus_if.h"

/*
 * AT24 parts have a "write page size" that differs per-device, and a "read page
 * size" that is always equal to the full device size.  We define maximum values
 * here to limit how long we occupy the bus with a single transfer, and because
 * there are temporary buffers of these sizes allocated on the stack.
 */
#define	MAX_RD_SZ	256	/* Largest read size we support */
#define	MAX_WR_SZ	256	/* Largest write size we support */

struct icee_softc {
	device_t	dev;		/* Myself */
	struct cdev	*cdev;		/* user interface */
	int		addr;		/* Slave address on the bus */
	int		size;		/* How big am I? */
	int		type;		/* What address type 8 or 16 bit? */
	int		wr_sz;		/* What's the write page size */
};

#ifdef FDT
struct eeprom_desc {
	int	    type;
	int	    size;
	int	    wr_sz;
	const char *name;
};

static struct eeprom_desc type_desc[] = {
	{ 8,        128,   8, "AT24C01"},
	{ 8,        256,   8, "AT24C02"},
	{ 8,        512,  16, "AT24C04"},
	{ 8,       1024,  16, "AT24C08"},
	{ 8,   2 * 1024,  16, "AT24C16"},
	{16,   4 * 1024,  32, "AT24C32"},
	{16,   8 * 1024,  32, "AT24C64"},
	{16,  16 * 1024,  64, "AT24C128"},
	{16,  32 * 1024,  64, "AT24C256"},
	{16,  64 * 1024, 128, "AT24C512"},
	{16, 128 * 1024, 256, "AT24CM01"},
};

static struct ofw_compat_data compat_data[] = {
	{"atmel,24c01",	  (uintptr_t)(&type_desc[0])},
	{"atmel,24c02",	  (uintptr_t)(&type_desc[1])},
	{"atmel,24c04",	  (uintptr_t)(&type_desc[2])},
	{"atmel,24c08",	  (uintptr_t)(&type_desc[3])},
	{"atmel,24c16",	  (uintptr_t)(&type_desc[4])},
	{"atmel,24c32",	  (uintptr_t)(&type_desc[5])},
	{"atmel,24c64",	  (uintptr_t)(&type_desc[6])},
	{"atmel,24c128",  (uintptr_t)(&type_desc[7])},
	{"atmel,24c256",  (uintptr_t)(&type_desc[8])},
	{"atmel,24c512",  (uintptr_t)(&type_desc[9])},
	{"atmel,24c1024", (uintptr_t)(&type_desc[10])},
	{NULL,		  (uintptr_t)NULL},
};
#endif

#define CDEV2SOFTC(dev)		((dev)->si_drv1)

/* cdev routines */
static d_open_t icee_open;
static d_close_t icee_close;
static d_read_t icee_read;
static d_write_t icee_write;

static struct cdevsw icee_cdevsw =
{
	.d_version = D_VERSION,
	.d_flags = D_TRACKCLOSE,
	.d_open = icee_open,
	.d_close = icee_close,
	.d_read = icee_read,
	.d_write = icee_write
};

#ifdef FDT
static int
icee_probe(device_t dev)
{
	struct eeprom_desc *d;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	d = (struct eeprom_desc *)
	    ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (d == NULL)
		return (ENXIO);

	device_set_desc(dev, d->name);
	return (BUS_PROBE_DEFAULT);
}

static void
icee_init(struct icee_softc *sc)
{
	struct eeprom_desc *d;

	d = (struct eeprom_desc *)
	    ofw_bus_search_compatible(sc->dev, compat_data)->ocd_data;
	if (d == NULL)
		return; /* attach will see sc->size == 0 and return error */

	sc->size  = d->size;
	sc->type  = d->type;
	sc->wr_sz = d->wr_sz;
}
#else /* !FDT */
static int
icee_probe(device_t dev)
{

	device_set_desc(dev, "I2C EEPROM");
	return (BUS_PROBE_NOWILDCARD);
}

static void
icee_init(struct icee_softc *sc)
{
	const char *dname;
	int dunit;

	dname = device_get_name(sc->dev);
	dunit = device_get_unit(sc->dev);
	resource_int_value(dname, dunit, "size", &sc->size);
	resource_int_value(dname, dunit, "type", &sc->type);
	resource_int_value(dname, dunit, "wr_sz", &sc->wr_sz);
}
#endif /* FDT */

static int
icee_attach(device_t dev)
{
	struct icee_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;

	sc->dev = dev;
	sc->addr = iicbus_get_addr(dev);
	icee_init(sc);
	if (sc->size == 0 || sc->type == 0 || sc->wr_sz == 0) {
		device_printf(sc->dev, "Missing config data, "
		    "these cannot be zero: size %d type %d wr_sz %d\n",
		    sc->size, sc->type, sc->wr_sz);
		return (EINVAL);
	}
	if (bootverbose)
		device_printf(dev, "size: %d bytes, addressing: %d-bits\n",
		    sc->size, sc->type);
	sc->cdev = make_dev(&icee_cdevsw, device_get_unit(dev), UID_ROOT,
	    GID_WHEEL, 0600, "icee%d", device_get_unit(dev));
	if (sc->cdev == NULL) {
		return (ENOMEM);
	}
	sc->cdev->si_drv1 = sc;

	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "address_size", CTLFLAG_RD,
	    &sc->type, 0, "Memory array address size in bits");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "device_size", CTLFLAG_RD,
	    &sc->size, 0, "Memory array capacity in bytes");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "write_size", CTLFLAG_RD,
	    &sc->wr_sz, 0, "Memory array page write size in bytes");

	return (0);
}

static int
icee_detach(device_t dev)
{
	struct icee_softc *sc = device_get_softc(dev);

	destroy_dev(sc->cdev);
	return (0);
}

static int 
icee_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct icee_softc *sc;

	sc = CDEV2SOFTC(dev);
	if (device_get_state(sc->dev) < DS_BUSY)
		device_busy(sc->dev);

	return (0);
}

static int
icee_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct icee_softc *sc;

	sc = CDEV2SOFTC(dev);
	device_unbusy(sc->dev);
	return (0);
}

static int
icee_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct icee_softc *sc;
	uint8_t addr[2];
	uint8_t data[MAX_RD_SZ];
	int error, i, len, slave;
	struct iic_msg msgs[2] = {
	     { 0, IIC_M_WR, 1, addr },
	     { 0, IIC_M_RD, 0, data },
	};

	sc = CDEV2SOFTC(dev);
	if (uio->uio_offset == sc->size)
		return (0);
	if (uio->uio_offset > sc->size)
		return (EIO);
	if (sc->type != 8 && sc->type != 16)
		return (EINVAL);
	slave = error = 0;
	while (uio->uio_resid > 0) {
		if (uio->uio_offset >= sc->size)
			break;
		len = MIN(MAX_RD_SZ - (uio->uio_offset & (MAX_RD_SZ - 1)),
		    uio->uio_resid);
		switch (sc->type) {
		case 8:
			slave = (uio->uio_offset >> 7) | sc->addr;
			msgs[0].len = 1;
			msgs[1].len = len;
			addr[0] = uio->uio_offset & 0xff;
			break;
		case 16:
			slave = sc->addr | (uio->uio_offset >> 15);
			msgs[0].len = 2;
			msgs[1].len = len;
			addr[0] = (uio->uio_offset >> 8) & 0xff;
			addr[1] = uio->uio_offset & 0xff;
			break;
		}
		for (i = 0; i < 2; i++)
			msgs[i].slave = slave;
		error = iicbus_transfer_excl(sc->dev, msgs, 2, IIC_INTRWAIT);
		if (error) {
			error = iic2errno(error);
			break;
		}
		error = uiomove(data, len, uio);
		if (error)
			break;
	}
	return (error);
}

/*
 * Write to the part.  We use three transfers here since we're actually
 * doing a write followed by a read to make sure that the write finished.
 * It is easier to encode the dummy read here than to break things up
 * into smaller chunks...
 */
static int
icee_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct icee_softc *sc;
	int error, len, slave, waitlimit;
	uint8_t data[MAX_WR_SZ + 2];
	struct iic_msg wr[1] = {
	     { 0, IIC_M_WR, 0, data },
	};
	struct iic_msg rd[1] = {
	     { 0, IIC_M_RD, 1, data },
	};

	sc = CDEV2SOFTC(dev);
	if (uio->uio_offset >= sc->size)
		return (EIO);
	if (sc->type != 8 && sc->type != 16)
		return (EINVAL);

	slave = error = 0;
	while (uio->uio_resid > 0) {
		if (uio->uio_offset >= sc->size)
			break;
		len = MIN(sc->wr_sz - (uio->uio_offset & (sc->wr_sz - 1)),
		    uio->uio_resid);
		switch (sc->type) {
		case 8:
			slave = (uio->uio_offset >> 7) | sc->addr;
			wr[0].len = 1 + len;
			data[0] = uio->uio_offset & 0xff;
			break;
		case 16:
			slave = sc->addr | (uio->uio_offset >> 15);
			wr[0].len = 2 + len;
			data[0] = (uio->uio_offset >> 8) & 0xff;
			data[1] = uio->uio_offset & 0xff;
			break;
		}
		wr[0].slave = slave;
		error = uiomove(data + sc->type / 8, len, uio);
		if (error)
			break;
		error = iicbus_transfer_excl(sc->dev, wr, 1, IIC_INTRWAIT);
		if (error) {
			error = iic2errno(error);
			break;
		}
		/* Read after write to wait for write-done. */
		waitlimit = 10000;
		rd[0].slave = slave;
		do {
			error = iicbus_transfer_excl(sc->dev, rd, 1,
			    IIC_INTRWAIT);
		} while (waitlimit-- > 0 && error != 0);
		if (error) {
			error = iic2errno(error);
			break;
		}
	}
	return error;
}

static device_method_t icee_methods[] = {
	DEVMETHOD(device_probe,		icee_probe),
	DEVMETHOD(device_attach,	icee_attach),
	DEVMETHOD(device_detach,	icee_detach),

	DEVMETHOD_END
};

static driver_t icee_driver = {
	"icee",
	icee_methods,
	sizeof(struct icee_softc),
};
static devclass_t icee_devclass;

DRIVER_MODULE(icee, iicbus, icee_driver, icee_devclass, 0, 0);
MODULE_VERSION(icee, 1);
MODULE_DEPEND(icee, iicbus, 1, 1, 1);
