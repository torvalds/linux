/*-
 * Copyright (c) 2015 Marcel Moolenaar
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <sys/sbuf.h>

#include <isa/isavar.h>
#include <isa/pnpvar.h>

#include <dev/proto/proto.h>

static int proto_isa_probe(device_t dev);
static int proto_isa_attach(device_t dev);

static device_method_t proto_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		proto_isa_probe),
	DEVMETHOD(device_attach,	proto_isa_attach),
	DEVMETHOD(device_detach,	proto_detach),
	DEVMETHOD_END
};

static driver_t proto_isa_driver = {
	proto_driver_name,
	proto_isa_methods,
	sizeof(struct proto_softc),
};

static char proto_isa_prefix[] = "isa";
static char **proto_isa_devnames;

static int
proto_isa_probe(device_t dev)
{
	struct sbuf *sb;
	struct resource *res;
	int rid, type;

	rid = 0;
	type = SYS_RES_IOPORT;
	res = bus_alloc_resource_any(dev, type, &rid, RF_ACTIVE);
	if (res == NULL) {
		type = SYS_RES_MEMORY;
		res = bus_alloc_resource_any(dev, type, &rid, RF_ACTIVE);
	}
	if (res == NULL)
		return (ENODEV);

	sb = sbuf_new_auto();
	sbuf_printf(sb, "%s:%#jx", proto_isa_prefix, rman_get_start(res));
	sbuf_finish(sb);
	device_set_desc_copy(dev, sbuf_data(sb));
	sbuf_delete(sb);
	bus_release_resource(dev, type, rid, res);
	return (proto_probe(dev, proto_isa_prefix, &proto_isa_devnames));
}

static int
proto_isa_alloc(device_t dev, int type, int nrids)
{
	struct resource *res;
	struct proto_softc *sc;
	int count, rid;

	sc = device_get_softc(dev);
	count = 0;
	for (rid = 0; rid < nrids; rid++) {
		res = bus_alloc_resource_any(dev, type, &rid, RF_ACTIVE);
		if (res == NULL)
			break;
		proto_add_resource(sc, type, rid, res);
		count++;
	}
	if (type == SYS_RES_DRQ && count > 0)
		proto_add_resource(sc, PROTO_RES_BUSDMA, 0, NULL);
	return (count);
}

static int
proto_isa_attach(device_t dev)
{

	proto_isa_alloc(dev, SYS_RES_IRQ, ISA_NIRQ);
	proto_isa_alloc(dev, SYS_RES_DRQ, ISA_NDRQ);
	proto_isa_alloc(dev, SYS_RES_IOPORT, ISA_NPORT);
	proto_isa_alloc(dev, SYS_RES_MEMORY, ISA_NMEM);
	return (proto_attach(dev));
}

DRIVER_MODULE(proto, acpi, proto_isa_driver, proto_devclass, NULL, NULL);
DRIVER_MODULE(proto, isa, proto_isa_driver, proto_devclass, NULL, NULL);
