/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include "iicbus_if.h"

/*
 * Translate IIC_Exxxxx status values to vaguely-equivelent errno values.
 */
int
iic2errno(int iic_status)
{
	switch (iic_status) {
	case IIC_NOERR:         return (0);
	case IIC_EBUSERR:       return (EALREADY);
	case IIC_ENOACK:        return (EIO);
	case IIC_ETIMEOUT:      return (ETIMEDOUT);
	case IIC_EBUSBSY:       return (EWOULDBLOCK);
	case IIC_ESTATUS:       return (EPROTO);
	case IIC_EUNDERFLOW:    return (EIO);
	case IIC_EOVERFLOW:     return (EOVERFLOW);
	case IIC_ENOTSUPP:      return (EOPNOTSUPP);
	case IIC_ENOADDR:       return (EADDRNOTAVAIL);
	case IIC_ERESOURCE:     return (ENOMEM);
	default:                return (EIO);
	}
}

/*
 * iicbus_intr()
 */
void
iicbus_intr(device_t bus, int event, char *buf)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);

	/* call owner's intr routine */
	if (sc->owner)
		IICBUS_INTR(sc->owner, event, buf);

	return;
}

static int
iicbus_poll(struct iicbus_softc *sc, int how)
{
	int error;

	IICBUS_ASSERT_LOCKED(sc);
	switch (how & IIC_INTRWAIT) {
	case IIC_WAIT | IIC_INTR:
		error = mtx_sleep(sc, &sc->lock, IICPRI|PCATCH, "iicreq", 0);
		break;

	case IIC_WAIT | IIC_NOINTR:
		error = mtx_sleep(sc, &sc->lock, IICPRI, "iicreq", 0);
		break;

	default:
		return (IIC_EBUSBSY);
	}

	return (error);
}

/*
 * iicbus_request_bus()
 *
 * Allocate the device to perform transfers.
 *
 * how	: IIC_WAIT or IIC_DONTWAIT
 */
int
iicbus_request_bus(device_t bus, device_t dev, int how)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	int error = 0;

	IICBUS_LOCK(sc);

	for (;;) {
		if (sc->owner == NULL)
			break;
		if ((how & IIC_RECURSIVE) && sc->owner == dev)
			break;
		if ((error = iicbus_poll(sc, how)) != 0)
			break;
	}

	if (error == 0) {
		++sc->owncount;
		if (sc->owner == NULL) {
			sc->owner = dev;
			/* 
			 * Drop the lock around the call to the bus driver, it
			 * should be allowed to sleep in the IIC_WAIT case.
			 * Drivers might also need to grab locks that would
			 * cause a LOR if our lock is held.
			 */
			IICBUS_UNLOCK(sc);
			/* Ask the underlying layers if the request is ok */
			error = IICBUS_CALLBACK(device_get_parent(bus),
			    IIC_REQUEST_BUS, (caddr_t)&how);
			IICBUS_LOCK(sc);
	
			if (error != 0) {
				sc->owner = NULL;
				sc->owncount = 0;
				wakeup_one(sc);
			}
		}
	}

	IICBUS_UNLOCK(sc);

	return (error);
}

/*
 * iicbus_release_bus()
 *
 * Release the device allocated with iicbus_request_dev()
 */
int
iicbus_release_bus(device_t bus, device_t dev)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);

	IICBUS_LOCK(sc);

	if (sc->owner != dev) {
		IICBUS_UNLOCK(sc);
		return (IIC_EBUSBSY);
	}

	if (--sc->owncount == 0) {
		/* Drop the lock while informing the low-level driver. */
		IICBUS_UNLOCK(sc);
		IICBUS_CALLBACK(device_get_parent(bus), IIC_RELEASE_BUS, NULL);
		IICBUS_LOCK(sc);
		sc->owner = NULL;
		wakeup_one(sc);
	}
	IICBUS_UNLOCK(sc);
	return (0);
}

/*
 * iicbus_started()
 *
 * Test if the iicbus is started by the controller
 */
int
iicbus_started(device_t bus)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);

	return (sc->started);
}

/*
 * iicbus_start()
 *
 * Send start condition to the slave addressed by 'slave'
 */
int
iicbus_start(device_t bus, u_char slave, int timeout)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	int error = 0;

	if (sc->started)
		return (IIC_ESTATUS); /* protocol error, bus already started */

	if (!(error = IICBUS_START(device_get_parent(bus), slave, timeout)))
		sc->started = slave;
	else
		sc->started = 0;

	return (error);
}

/*
 * iicbus_repeated_start()
 *
 * Send start condition to the slave addressed by 'slave'
 */
int
iicbus_repeated_start(device_t bus, u_char slave, int timeout)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	int error = 0;

	if (!sc->started)
		return (IIC_ESTATUS); /* protocol error, bus not started */

	if (!(error = IICBUS_REPEATED_START(device_get_parent(bus), slave, timeout)))
		sc->started = slave;
	else
		sc->started = 0;

	return (error);
}

/*
 * iicbus_stop()
 *
 * Send stop condition to the bus
 */
int
iicbus_stop(device_t bus)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	int error = 0;

	if (!sc->started)
		return (IIC_ESTATUS); /* protocol error, bus not started */

	error = IICBUS_STOP(device_get_parent(bus));

	/* refuse any further access */
	sc->started = 0;

	return (error);
}

/*
 * iicbus_write()
 *
 * Write a block of data to the slave previously started by
 * iicbus_start() call
 */
int
iicbus_write(device_t bus, const char *buf, int len, int *sent, int timeout)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	
	/* a slave must have been started for writing */
	if (sc->started == 0 || (sc->strict != 0 && (sc->started & LSB) != 0))
		return (IIC_ESTATUS);

	return (IICBUS_WRITE(device_get_parent(bus), buf, len, sent, timeout));
}

/*
 * iicbus_read()
 *
 * Read a block of data from the slave previously started by
 * iicbus_read() call
 */
int 
iicbus_read(device_t bus, char *buf, int len, int *read, int last, int delay)
{
	struct iicbus_softc *sc = (struct iicbus_softc *)device_get_softc(bus);
	
	/* a slave must have been started for reading */
	if (sc->started == 0 || (sc->strict != 0 && (sc->started & LSB) == 0))
		return (IIC_ESTATUS);

	return (IICBUS_READ(device_get_parent(bus), buf, len, read, last, delay));
}

/*
 * iicbus_write_byte()
 *
 * Write a byte to the slave previously started by iicbus_start() call
 */
int
iicbus_write_byte(device_t bus, char byte, int timeout)
{
	struct iicbus_softc *sc = device_get_softc(bus);
	char data = byte;
	int sent;

	/* a slave must have been started for writing */
	if (sc->started == 0 || (sc->strict != 0 && (sc->started & LSB) != 0))
		return (IIC_ESTATUS);

	return (iicbus_write(bus, &data, 1, &sent, timeout));
}

/*
 * iicbus_read_byte()
 *
 * Read a byte from the slave previously started by iicbus_start() call
 */
int
iicbus_read_byte(device_t bus, char *byte, int timeout)
{
	struct iicbus_softc *sc = device_get_softc(bus);
	int read;

	/* a slave must have been started for reading */
	if (sc->started == 0 || (sc->strict != 0 && (sc->started & LSB) == 0))
		return (IIC_ESTATUS);

	return (iicbus_read(bus, byte, 1, &read, IIC_LAST_READ, timeout));
}

/*
 * iicbus_block_write()
 *
 * Write a block of data to slave ; start/stop protocol managed
 */
int
iicbus_block_write(device_t bus, u_char slave, char *buf, int len, int *sent)
{
	u_char addr = slave & ~LSB;
	int error;

	if ((error = iicbus_start(bus, addr, 0)))
		return (error);

	error = iicbus_write(bus, buf, len, sent, 0);

	iicbus_stop(bus);

	return (error);
}

/*
 * iicbus_block_read()
 *
 * Read a block of data from slave ; start/stop protocol managed
 */
int
iicbus_block_read(device_t bus, u_char slave, char *buf, int len, int *read)
{
	u_char addr = slave | LSB;
	int error;

	if ((error = iicbus_start(bus, addr, 0)))
		return (error);

	error = iicbus_read(bus, buf, len, read, IIC_LAST_READ, 0);

	iicbus_stop(bus);

	return (error);
}

/*
 * iicbus_transfer()
 *
 * Do an aribtrary number of transfers on the iicbus.  We pass these
 * raw requests to the bridge driver.  If the bridge driver supports
 * them directly, then it manages all the details.  If not, it can use
 * the helper function iicbus_transfer_gen() which will do the
 * transfers at a low level.
 *
 * Pointers passed in as part of iic_msg must be kernel pointers.
 * Callers that have user addresses to manage must do so on their own.
 */
int
iicbus_transfer(device_t bus, struct iic_msg *msgs, uint32_t nmsgs)
{

	return (IICBUS_TRANSFER(device_get_parent(bus), msgs, nmsgs));
}

int
iicbus_transfer_excl(device_t dev, struct iic_msg *msgs, uint32_t nmsgs,
    int how)
{
	device_t bus;
	int error;

	bus = device_get_parent(dev);
	error = iicbus_request_bus(bus, dev, how);
	if (error == 0)
		error = IICBUS_TRANSFER(bus, msgs, nmsgs);
	iicbus_release_bus(bus, dev);
	return (error);
}

/*
 * Generic version of iicbus_transfer that calls the appropriate
 * routines to accomplish this.  See note above about acceptable
 * buffer addresses.
 */
int
iicbus_transfer_gen(device_t dev, struct iic_msg *msgs, uint32_t nmsgs)
{
	int i, error, lenread, lenwrote, nkid, rpstart, addr;
	device_t *children, bus;
	bool nostop, started;

	if ((error = device_get_children(dev, &children, &nkid)) != 0)
		return (IIC_ERESOURCE);
	if (nkid != 1) {
		free(children, M_TEMP);
		return (IIC_ENOTSUPP);
	}
	bus = children[0];
	rpstart = 0;
	free(children, M_TEMP);
	nostop = iicbus_get_nostop(dev);
	started = false;
	for (i = 0, error = 0; i < nmsgs && error == 0; i++) {
		addr = msgs[i].slave;
		if (msgs[i].flags & IIC_M_RD)
			addr |= LSB;
		else
			addr &= ~LSB;

		if (!(msgs[i].flags & IIC_M_NOSTART)) {
			if (rpstart)
				error = iicbus_repeated_start(bus, addr, 0);
			else
				error = iicbus_start(bus, addr, 0);
			if (error != 0)
				break;
			started = true;
		}

		if (msgs[i].flags & IIC_M_RD)
			error = iicbus_read(bus, msgs[i].buf, msgs[i].len,
			    &lenread, IIC_LAST_READ, 0);
		else
			error = iicbus_write(bus, msgs[i].buf, msgs[i].len,
			    &lenwrote, 0);
		if (error != 0)
			break;

		if ((msgs[i].flags & IIC_M_NOSTOP) != 0 ||
		    (nostop && i + 1 < nmsgs)) {
			rpstart = 1;	/* Next message gets repeated start */
		} else {
			rpstart = 0;
			iicbus_stop(bus);
		}
	}
	if (error != 0 && started)
		iicbus_stop(bus);
	return (error);
}

int
iicdev_readfrom(device_t slavedev, uint8_t regaddr, void *buffer,
    uint16_t buflen, int waithow)
{
	struct iic_msg msgs[2];
	uint8_t slaveaddr;

	/*
	 * Two transfers back to back with a repeat-start between them; first we
	 * write the address-within-device, then we read from the device.
	 */
	slaveaddr = iicbus_get_addr(slavedev);

	msgs[0].slave = slaveaddr;
	msgs[0].flags = IIC_M_WR | IIC_M_NOSTOP;
	msgs[0].len   = 1;
	msgs[0].buf   = &regaddr;

	msgs[1].slave = slaveaddr;
	msgs[1].flags = IIC_M_RD;
	msgs[1].len   = buflen;
	msgs[1].buf   = buffer;

	return (iicbus_transfer_excl(slavedev, msgs, nitems(msgs), waithow));
}

int iicdev_writeto(device_t slavedev, uint8_t regaddr, void *buffer,
    uint16_t buflen, int waithow)
{
	struct iic_msg msgs[2];
	uint8_t slaveaddr;

	/*
	 * Two transfers back to back with no stop or start between them; first
	 * we write the address then we write the data to that address, all in a
	 * single transfer from two scattered buffers.
	 */
	slaveaddr = iicbus_get_addr(slavedev);

	msgs[0].slave = slaveaddr;
	msgs[0].flags = IIC_M_WR | IIC_M_NOSTOP;
	msgs[0].len   = 1;
	msgs[0].buf   = &regaddr;

	msgs[1].slave = slaveaddr;
	msgs[1].flags = IIC_M_WR | IIC_M_NOSTART;
	msgs[1].len   = buflen;
	msgs[1].buf   = buffer;

	return (iicbus_transfer_excl(slavedev, msgs, nitems(msgs), waithow));
}
