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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * I2C to SMB bridge
 *
 * Example:
 *
 *     smb bttv
 *       \ /
 *      smbus
 *       /  \
 *    iicsmb bti2c
 *       |
 *     iicbus
 *     /  |  \
 *  iicbb pcf ...
 *    |
 *  lpbb
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include <dev/smbus/smb.h>
#include <dev/smbus/smbconf.h>

#include "iicbus_if.h"
#include "smbus_if.h"

struct iicsmb_softc {

#define SMB_WAITING_ADDR	0x0
#define SMB_WAITING_LOW		0x1
#define SMB_WAITING_HIGH	0x2
#define SMB_DONE		0x3
	int state;

	u_char devaddr;			/* slave device address */

	char low;			/* low byte received first */
	char high;			/* high byte */

	struct mtx lock;
	device_t smbus;
};

static int iicsmb_probe(device_t);
static int iicsmb_attach(device_t);
static int iicsmb_detach(device_t);
static void iicsmb_identify(driver_t *driver, device_t parent);

static int iicsmb_intr(device_t dev, int event, char *buf);
static int iicsmb_callback(device_t dev, int index, void *data);
static int iicsmb_quick(device_t dev, u_char slave, int how);
static int iicsmb_sendb(device_t dev, u_char slave, char byte);
static int iicsmb_recvb(device_t dev, u_char slave, char *byte);
static int iicsmb_writeb(device_t dev, u_char slave, char cmd, char byte);
static int iicsmb_writew(device_t dev, u_char slave, char cmd, short word);
static int iicsmb_readb(device_t dev, u_char slave, char cmd, char *byte);
static int iicsmb_readw(device_t dev, u_char slave, char cmd, short *word);
static int iicsmb_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata);
static int iicsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf);
static int iicsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf);

static devclass_t iicsmb_devclass;

static device_method_t iicsmb_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	iicsmb_identify),
	DEVMETHOD(device_probe,		iicsmb_probe),
	DEVMETHOD(device_attach,	iicsmb_attach),
	DEVMETHOD(device_detach,	iicsmb_detach),

	/* iicbus interface */
	DEVMETHOD(iicbus_intr,		iicsmb_intr),

	/* smbus interface */
	DEVMETHOD(smbus_callback,	iicsmb_callback),
	DEVMETHOD(smbus_quick,		iicsmb_quick),
	DEVMETHOD(smbus_sendb,		iicsmb_sendb),
	DEVMETHOD(smbus_recvb,		iicsmb_recvb),
	DEVMETHOD(smbus_writeb,		iicsmb_writeb),
	DEVMETHOD(smbus_writew,		iicsmb_writew),
	DEVMETHOD(smbus_readb,		iicsmb_readb),
	DEVMETHOD(smbus_readw,		iicsmb_readw),
	DEVMETHOD(smbus_pcall,		iicsmb_pcall),
	DEVMETHOD(smbus_bwrite,		iicsmb_bwrite),
	DEVMETHOD(smbus_bread,		iicsmb_bread),

	DEVMETHOD_END
};

static driver_t iicsmb_driver = {
	"iicsmb",
	iicsmb_methods,
	sizeof(struct iicsmb_softc),
};

static void
iicsmb_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "iicsmb", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "iicsmb", -1);
}

static int
iicsmb_probe(device_t dev)
{
	device_set_desc(dev, "SMBus over I2C bridge");
	return (BUS_PROBE_NOWILDCARD);
}

static int
iicsmb_attach(device_t dev)
{
	struct iicsmb_softc *sc = (struct iicsmb_softc *)device_get_softc(dev);

	mtx_init(&sc->lock, "iicsmb", NULL, MTX_DEF);

	sc->smbus = device_add_child(dev, "smbus", -1);

	/* probe and attach the smbus */
	bus_generic_attach(dev);

	return (0);
}

static int
iicsmb_detach(device_t dev)
{
	struct iicsmb_softc *sc = (struct iicsmb_softc *)device_get_softc(dev);

	bus_generic_detach(dev);
	device_delete_children(dev);
	mtx_destroy(&sc->lock);

	return (0);
}

/*
 * iicsmb_intr()
 *
 * iicbus interrupt handler
 */
static int
iicsmb_intr(device_t dev, int event, char *buf)
{
	struct iicsmb_softc *sc = (struct iicsmb_softc *)device_get_softc(dev);

	mtx_lock(&sc->lock);
	switch (event) {
	case INTR_GENERAL:
	case INTR_START:
		sc->state = SMB_WAITING_ADDR;
		break;

	case INTR_STOP:
		/* call smbus intr handler */
		smbus_intr(sc->smbus, sc->devaddr,
				sc->low, sc->high, SMB_ENOERR);
		break;

	case INTR_RECEIVE:
		switch (sc->state) {
		case SMB_DONE:
			/* XXX too much data, discard */
			printf("%s: too much data from 0x%x\n", __func__,
				sc->devaddr & 0xff);
			goto end;

		case SMB_WAITING_ADDR:
			sc->devaddr = (u_char)*buf;
			sc->state = SMB_WAITING_LOW;
			break;

		case SMB_WAITING_LOW:
			sc->low = *buf;
			sc->state = SMB_WAITING_HIGH;
			break;

		case SMB_WAITING_HIGH:
			sc->high = *buf;
			sc->state = SMB_DONE;
			break;
		}
end:
		break;

	case INTR_TRANSMIT:
	case INTR_NOACK:
		break;

	case INTR_ERROR:
		switch (*buf) {
		case IIC_EBUSERR:
			smbus_intr(sc->smbus, sc->devaddr, 0, 0, SMB_EBUSERR);
			break;

		default:
			printf("%s unknown error 0x%x!\n", __func__,
								(int)*buf);
			break;
		}
		break;

	default:
		panic("%s: unknown event (%d)!", __func__, event);
	}
	mtx_unlock(&sc->lock);

	return (0);
}

static int
iicsmb_callback(device_t dev, int index, void *data)
{
	device_t parent = device_get_parent(dev);
	int error = 0;
	int how;

	switch (index) {
	case SMB_REQUEST_BUS:
		/* request underlying iicbus */
		how = *(int *)data;
		error = iicbus_request_bus(parent, dev, how);
		break;

	case SMB_RELEASE_BUS:
		/* release underlying iicbus */
		error = iicbus_release_bus(parent, dev);
		break;

	default:
		error = EINVAL;
	}

	return (error);
}

static int
iic2smb_error(int error)
{
	switch (error) {
	case IIC_NOERR:
		return (SMB_ENOERR);
	case IIC_EBUSERR:
		return (SMB_EBUSERR);
	case IIC_ENOACK:
		return (SMB_ENOACK);
	case IIC_ETIMEOUT:
		return (SMB_ETIMEOUT);
	case IIC_EBUSBSY:
		return (SMB_EBUSY);
	case IIC_ESTATUS:
		return (SMB_EBUSERR);
	case IIC_EUNDERFLOW:
		return (SMB_EBUSERR);
	case IIC_EOVERFLOW:
		return (SMB_EBUSERR);
	case IIC_ENOTSUPP:
		return (SMB_ENOTSUPP);
	case IIC_ENOADDR:
		return (SMB_EBUSERR);
	case IIC_ERESOURCE:
		return (SMB_EBUSERR);
	default:
		return (SMB_EBUSERR);
	}
}

#define	TRANSFER_MSGS(dev, msgs)	iicbus_transfer(dev, msgs, nitems(msgs))

static int
iicsmb_quick(device_t dev, u_char slave, int how)
{
	struct iic_msg msgs[] = {
	     { slave, how == SMB_QWRITE ? IIC_M_WR : IIC_M_RD, 0, NULL },
	};
	int error;

	switch (how) {
	case SMB_QWRITE:
	case SMB_QREAD:
		break;
	default:
		return (SMB_EINVAL);
	}

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_sendb(device_t dev, u_char slave, char byte)
{
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR, 1, &byte },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_recvb(device_t dev, u_char slave, char *byte)
{
	struct iic_msg msgs[] = {
	     { slave, IIC_M_RD, 1, byte },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	uint8_t bytes[] = { cmd, byte };
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR, nitems(bytes), bytes },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_writew(device_t dev, u_char slave, char cmd, short word)
{
	uint8_t bytes[] = { cmd, word & 0xff, word >> 8 };
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR, nitems(bytes), bytes },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR | IIC_M_NOSTOP, 1, &cmd },
	     { slave, IIC_M_RD, 1, byte },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	uint8_t buf[2];
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR | IIC_M_NOSTOP, 1, &cmd },
	     { slave, IIC_M_RD, nitems(buf), buf },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	if (error == 0)
		*word = ((uint16_t)buf[1] << 8) | buf[0];
	return (iic2smb_error(error));
}

static int
iicsmb_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata)
{
	uint8_t in[3] = { cmd, sdata & 0xff, sdata >> 8 };
	uint8_t out[2];
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR | IIC_M_NOSTOP, nitems(in), in },
	     { slave, IIC_M_RD, nitems(out), out },
	};
	int error;

	error = TRANSFER_MSGS(dev, msgs);
	if (error == 0)
		*rdata = ((uint16_t)out[1] << 8) | out[0];
	return (iic2smb_error(error));
}

static int
iicsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	uint8_t bytes[2] = { cmd, count };
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR | IIC_M_NOSTOP, nitems(bytes), bytes },
	     { slave, IIC_M_WR | IIC_M_NOSTART, count, buf },
	};
	int error;

	if (count > SMB_MAXBLOCKSIZE || count == 0)
		return (SMB_EINVAL);
	error = TRANSFER_MSGS(dev, msgs);
	return (iic2smb_error(error));
}

static int
iicsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct iic_msg msgs[] = {
	     { slave, IIC_M_WR | IIC_M_NOSTOP, 1, &cmd },
	     { slave, IIC_M_RD | IIC_M_NOSTOP, 1, count },
	};
	struct iic_msg block_msg[] = {
	     { slave, IIC_M_RD | IIC_M_NOSTART, 0, buf },
	};
	device_t parent = device_get_parent(dev);
	int error;

	/* Have to do this because the command is split in two transfers. */
	error = iicbus_request_bus(parent, dev, IIC_WAIT);
	if (error == 0)
		error = TRANSFER_MSGS(dev, msgs);
	if (error == 0) {
		/*
		 * If the slave offers an empty or a too long reply,
		 * read one byte to generate the stop or abort.
		 */
		if (*count > SMB_MAXBLOCKSIZE || *count == 0)
			block_msg[0].len = 1;
		else
			block_msg[0].len = *count;
		error = TRANSFER_MSGS(dev, block_msg);
		if (*count > SMB_MAXBLOCKSIZE || *count == 0)
			error = SMB_EINVAL;
	}
	(void)iicbus_release_bus(parent, dev);
	return (iic2smb_error(error));
}

DRIVER_MODULE(iicsmb, iicbus, iicsmb_driver, iicsmb_devclass, 0, 0);
DRIVER_MODULE(smbus, iicsmb, smbus_driver, smbus_devclass, 0, 0);
MODULE_DEPEND(iicsmb, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_DEPEND(iicsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(iicsmb, 1);
