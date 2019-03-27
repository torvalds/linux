/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu, Marc Bouget
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

/*
 * I2C Bit-Banging over parallel port
 *
 * See the Official Philips interface description in lpbb(4)
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/uio.h>


#include <dev/ppbus/ppbconf.h>
#include "ppbus_if.h"
#include <dev/ppbus/ppbio.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "iicbb_if.h"

static int lpbb_detect(device_t dev);

static void
lpbb_identify(driver_t *driver, device_t parent)
{

	device_t dev;

	dev = device_find_child(parent, "lpbb", -1);
	if (!dev)
		BUS_ADD_CHILD(parent, 0, "lpbb", -1);
}

static int
lpbb_probe(device_t dev)
{

	/* Perhaps call this during identify instead? */
	if (!lpbb_detect(dev))
		return (ENXIO);

	device_set_desc(dev, "Parallel I2C bit-banging interface");

	return (0);
}

static int
lpbb_attach(device_t dev)
{
	device_t bitbang;

	/* add generic bit-banging code */
	bitbang = device_add_child(dev, "iicbb", -1);
	device_probe_and_attach(bitbang);

	return (0);
}

static int
lpbb_callback(device_t dev, int index, caddr_t data)
{
	device_t ppbus = device_get_parent(dev);
	int error = 0;
	int how;

	switch (index) {
	case IIC_REQUEST_BUS:
		/* request the ppbus */
		how = *(int *)data;
		ppb_lock(ppbus);
		error = ppb_request_bus(ppbus, dev, how);
		ppb_unlock(ppbus);
		break;

	case IIC_RELEASE_BUS:
		/* release the ppbus */
		ppb_lock(ppbus);
		error = ppb_release_bus(ppbus, dev);
		ppb_unlock(ppbus);
		break;

	default:
		error = EINVAL;
	}

	return (error);
}

#define SDA_out 0x80
#define SCL_out 0x08
#define SDA_in  0x80
#define SCL_in  0x08
#define ALIM    0x20
#define I2CKEY  0x50

/* Reset bus by setting SDA first and then SCL. */
static void
lpbb_reset_bus(device_t dev)
{
	device_t ppbus = device_get_parent(dev);

	ppb_assert_locked(ppbus);
	ppb_wdtr(ppbus, (u_char)~SDA_out);
	ppb_wctr(ppbus, (u_char)(ppb_rctr(ppbus) | SCL_out));
}

static int
lpbb_getscl(device_t dev)
{
	device_t ppbus = device_get_parent(dev);
	int rval;

	ppb_lock(ppbus);
	rval = ((ppb_rstr(ppbus) & SCL_in) == SCL_in);
	ppb_unlock(ppbus);
	return (rval);
}

static int
lpbb_getsda(device_t dev)
{
	device_t ppbus = device_get_parent(dev);
	int rval;

	ppb_lock(ppbus);
	rval = ((ppb_rstr(ppbus) & SDA_in) == SDA_in);
	ppb_unlock(ppbus);
	return (rval);
}

static void
lpbb_setsda(device_t dev, int val)
{
	device_t ppbus = device_get_parent(dev);

	ppb_lock(ppbus);
	if (val == 0)
		ppb_wdtr(ppbus, (u_char)SDA_out);
	else
		ppb_wdtr(ppbus, (u_char)~SDA_out);
	ppb_unlock(ppbus);
}

static void
lpbb_setscl(device_t dev, int val)
{
	device_t ppbus = device_get_parent(dev);

	ppb_lock(ppbus);
	if (val == 0)
		ppb_wctr(ppbus, (u_char)(ppb_rctr(ppbus) & ~SCL_out));
	else
		ppb_wctr(ppbus, (u_char)(ppb_rctr(ppbus) | SCL_out));
	ppb_unlock(ppbus);
}

static int
lpbb_detect(device_t dev)
{
	device_t ppbus = device_get_parent(dev);

	ppb_lock(ppbus);
	if (ppb_request_bus(ppbus, dev, PPB_DONTWAIT)) {
		ppb_unlock(ppbus);
		device_printf(dev, "can't allocate ppbus\n");
		return (0);
	}

	lpbb_reset_bus(dev);

	if ((ppb_rstr(ppbus) & I2CKEY) ||
		((ppb_rstr(ppbus) & ALIM) != ALIM)) {
		ppb_release_bus(ppbus, dev);
		ppb_unlock(ppbus);
		return (0);
	}

	ppb_release_bus(ppbus, dev);
	ppb_unlock(ppbus);

	return (1);
}

static int
lpbb_reset(device_t dev, u_char speed, u_char addr, u_char * oldaddr)
{
	device_t ppbus = device_get_parent(dev);

	ppb_lock(ppbus);
	if (ppb_request_bus(ppbus, dev, PPB_DONTWAIT)) {
		ppb_unlock(ppbus);
		device_printf(dev, "can't allocate ppbus\n");
		return (0);
	}

	lpbb_reset_bus(dev);

	ppb_release_bus(ppbus, dev);
	ppb_unlock(ppbus);

	return (IIC_ENOADDR);
}

static devclass_t lpbb_devclass;

static device_method_t lpbb_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	lpbb_identify),
	DEVMETHOD(device_probe,		lpbb_probe),
	DEVMETHOD(device_attach,	lpbb_attach),

	/* iicbb interface */
	DEVMETHOD(iicbb_callback,	lpbb_callback),
	DEVMETHOD(iicbb_setsda,		lpbb_setsda),
	DEVMETHOD(iicbb_setscl,		lpbb_setscl),
	DEVMETHOD(iicbb_getsda,		lpbb_getsda),
	DEVMETHOD(iicbb_getscl,		lpbb_getscl),
	DEVMETHOD(iicbb_reset,		lpbb_reset),

	DEVMETHOD_END
};

static driver_t lpbb_driver = {
	"lpbb",
	lpbb_methods,
	1,
};

DRIVER_MODULE(lpbb, ppbus, lpbb_driver, lpbb_devclass, 0, 0);
DRIVER_MODULE(iicbb, lpbb, iicbb_driver, iicbb_devclass, 0, 0);
MODULE_DEPEND(lpbb, ppbus, 1, 1, 1);
MODULE_DEPEND(lpbb, iicbb, IICBB_MINVER, IICBB_PREFVER, IICBB_MAXVER);
MODULE_VERSION(lpbb, 1);
