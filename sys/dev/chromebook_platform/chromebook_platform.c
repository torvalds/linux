/*-
 * Copyright (c) 2016 The FreeBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

/*
 * Driver that attaches I2C devices.
 */
static struct {
	uint32_t	pci_id;
	const char	*name;
	uint8_t		addr;
} slaves[] = {
	{ 0x9c628086,	"isl",		0x88 },
	{ 0x9c618086,	"cyapa",	0xce },
};

static void
chromebook_i2c_identify(driver_t *driver, device_t bus)
{
	device_t controller;
	device_t child;
	int i;

	/*
	 * A stopgap approach to preserve the status quo.
	 * A more intelligent approach is required to correctly
	 * identify a machine model and hardware available on it.
	 * For instance, DMI could be used.
	 * See http://lxr.free-electrons.com/source/drivers/platform/chrome/chromeos_laptop.c
	 */
	controller = device_get_parent(bus);
	if (strcmp(device_get_name(controller), "ig4iic_pci") != 0)
		return;

	for (i = 0; i < nitems(slaves); i++) {
		if (device_find_child(bus, slaves[i].name, -1) != NULL)
			continue;
		if (slaves[i].pci_id != pci_get_devid(controller))
			continue;
		child = BUS_ADD_CHILD(bus, 0, slaves[i].name, -1);
		if (child != NULL)
			iicbus_set_addr(child, slaves[i].addr);
	}
}

static device_method_t chromebook_i2c_methods[] = {
	DEVMETHOD(device_identify,	chromebook_i2c_identify),
	{ 0, 0 }
};

static driver_t chromebook_i2c_driver = {
	"chromebook_i2c",
	chromebook_i2c_methods,
	0	/* no softc */
};

static devclass_t chromebook_i2c_devclass;

DRIVER_MODULE(chromebook_i2c, iicbus, chromebook_i2c_driver,
    chromebook_i2c_devclass, 0, 0);
MODULE_VERSION(chromebook_i2c, 1);

