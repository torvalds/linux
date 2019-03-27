/*-
 * Copyright (c) 2013 Nathan Whitehorn
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
 */

#ifndef	_FDT_SIMPLEBUS_H
#define	_FDT_SIMPLEBUS_H

#include <dev/ofw/ofw_bus.h>

/* FDT simplebus */
DECLARE_CLASS(simplebus_driver);

struct simplebus_range {
	uint64_t bus;
	uint64_t host;
	uint64_t size;
};

/* devinfo and softc */
struct simplebus_softc {
	device_t dev;
	phandle_t node;

	struct simplebus_range *ranges;
	int nranges;

	pcell_t acells, scells;
};

struct simplebus_devinfo {
	struct ofw_bus_devinfo	obdinfo;
	struct resource_list	rl;
};

void simplebus_init(device_t dev, phandle_t node);
device_t simplebus_add_device(device_t dev, phandle_t node, u_int order,
    const char *name, int unit, struct simplebus_devinfo *di);
struct simplebus_devinfo *simplebus_setup_dinfo(device_t dev, phandle_t node,
    struct simplebus_devinfo *di);
int simplebus_fill_ranges(phandle_t node,
    struct simplebus_softc *sc);
#endif	/* _FDT_SIMPLEBUS_H */
