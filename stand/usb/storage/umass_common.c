/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include USB_GLOBAL_INCLUDE_FILE

#include "umass_common.h"

struct usb_attach_arg umass_uaa;

static device_probe_t umass_probe;
static device_attach_t umass_attach;
static device_detach_t umass_detach;

static devclass_t umass_devclass;

static device_method_t umass_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, umass_probe),
	DEVMETHOD(device_attach, umass_attach),
	DEVMETHOD(device_detach, umass_detach),

	DEVMETHOD_END
};

static driver_t umass_driver = {
	.name = "umass",
	.methods = umass_methods,
};

DRIVER_MODULE(umass, uhub, umass_driver, umass_devclass, NULL, 0);

static int
umass_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST ||
	    uaa->info.bInterfaceClass != UICLASS_MASS ||
	    uaa->info.bInterfaceSubClass != UISUBCLASS_SCSI ||
	    uaa->info.bInterfaceProtocol != UIPROTO_MASS_BBB ||
	    device_get_unit(dev) != 0)
		return (ENXIO);
	return (0);
}

static int
umass_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	umass_uaa = *uaa;
	return (0);			/* success */
}

static int
umass_detach(device_t dev)
{

#ifdef USB_DEBUG
	memset(&umass_uaa, 0, sizeof(umass_uaa));
#endif
	return (0);
}
