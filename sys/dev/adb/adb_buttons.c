/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2011, Justin Hibbits.
 * Copyright (c) 2002, Miodrag Vallat.
 * Copyright (C) 1999 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * OpenBSD: abtn.c,v 1.12 2009/01/10 18:00:59 robert Exp
 * NetBSD: abtn.c,v 1.1 1999/07/12 17:48:26 tsubai Exp
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>

#include <dev/adb/adb.h>

#define ABTN_HANDLER_ID 31

struct abtn_softc {
	device_t sc_dev;

	int handler_id;
};

static int abtn_probe(device_t dev);
static int abtn_attach(device_t dev);
static u_int abtn_receive_packet(device_t dev, u_char status, 
    u_char command, u_char reg, int len, u_char *data);

static device_method_t abtn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         abtn_probe),
        DEVMETHOD(device_attach,        abtn_attach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),

	/* ADB interface */
	DEVMETHOD(adb_receive_packet,	abtn_receive_packet),

	{ 0, 0 }
};

static driver_t abtn_driver = {
	"abtn",
	abtn_methods,
	sizeof(struct abtn_softc),
};

static devclass_t abtn_devclass;

DRIVER_MODULE(abtn, adb, abtn_driver, abtn_devclass, 0, 0);

static int
abtn_probe(device_t dev)
{
	uint8_t type;

	type = adb_get_device_type(dev);

	if (type != ADB_DEVICE_MISC)
		return (ENXIO);

	device_set_desc(dev, "ADB Brightness/Volume/Eject Buttons");
	return (0);
}

static int
abtn_attach(device_t dev) 
{
	struct abtn_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->handler_id = adb_get_device_handler(dev);

	return 0;
}

static u_int
abtn_receive_packet(device_t dev, u_char status, 
    u_char command, u_char reg, int len, u_char *data)
{
	u_int cmd;

	cmd = data[0];

	switch (cmd) {
	case 0x0a:	/* decrease brightness */
		devctl_notify("PMU", "keys", "brightness",
		    "notify=down");
		break;

	case 0x09:	/* increase brightness */
		devctl_notify("PMU", "keys", "brightness", "notify=up");
		break;

	case 0x08:	/* mute */
	case 0x01:	/* mute, AV hardware */
		devctl_notify("PMU", "keys", "mute", NULL);
		break;
	case 0x07:	/* decrease volume */
	case 0x02:	/* decrease volume, AV hardware */
		devctl_notify("PMU", "keys", "volume", "notify=down");
		break;
	case 0x06:	/* increase volume */
	case 0x03:	/* increase volume, AV hardware */
		devctl_notify("PMU", "keys", "volume", "notify=up");
		break;
	case 0x0c:	/* mirror display key */
		/* Need callback to do something with this */
		break;
	case 0x0b:	/* eject tray */
		devctl_notify("PMU", "keys", "eject", NULL);
		break;
	case 0x7f:	/* numlock */
		/* Need callback to do something with this */
		break;

	default:
#ifdef DEBUG
		if ((cmd & ~0x7f) == 0)
			device_printf(dev, "unknown ADB button 0x%x\n", cmd);
#endif
		break;
	}
	return 0;
}

