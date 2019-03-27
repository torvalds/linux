/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Nikolai Saoukh
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
 * Driver to take care of holes in ISA I/O memory occupied
 * by option rom(s)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <isa/isavar.h>
#include <isa/pnpvar.h>

#define	IOMEM_START	0x0a0000
#define	IOMEM_STEP	0x000800
#define	IOMEM_END	0x100000

#define	ORM_ID	0x00004d3e

static struct isa_pnp_id orm_ids[] = {
	{ ORM_ID,	NULL },		/* ORM0000 */
	{ 0,		NULL },
};

#define MAX_ROMS	32

struct orm_softc {
	int		rnum;
	int		rid[MAX_ROMS];
	struct resource *res[MAX_ROMS];
};

static int
orm_probe(device_t dev)
{
	return (ISA_PNP_PROBE(device_get_parent(dev), dev, orm_ids));
}

static int
orm_attach(device_t dev)
{
	return (0);
}

static void
orm_identify(driver_t* driver, device_t parent)
{
	bus_space_handle_t	bh;
	bus_space_tag_t		bt;
	device_t		child;
	u_int32_t		chunk = IOMEM_START;
	struct resource		*res;
	int			rid;
	u_int32_t		rom_size;
	struct orm_softc	*sc;
	u_int8_t		buf[3];

	if (resource_disabled("orm", 0))
		return;

	child = BUS_ADD_CHILD(parent, ISA_ORDER_SENSITIVE, "orm", -1);
	device_set_driver(child, driver);
	isa_set_logicalid(child, ORM_ID);
	isa_set_vendorid(child, ORM_ID);
	sc = device_get_softc(child);
	sc->rnum = 0;
	while (sc->rnum < MAX_ROMS && chunk < IOMEM_END) {
		bus_set_resource(child, SYS_RES_MEMORY, sc->rnum, chunk,
		    IOMEM_STEP);
		rid = sc->rnum;
		res = bus_alloc_resource_any(child, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (res == NULL) {
			bus_delete_resource(child, SYS_RES_MEMORY, sc->rnum);
			chunk += IOMEM_STEP;
			continue;
		}
		bt = rman_get_bustag(res);
		bh = rman_get_bushandle(res);
		bus_space_read_region_1(bt, bh, 0, buf, sizeof(buf));

		/*
		 * We need to release and delete the resource since we're
		 * changing its size, or the rom isn't there.  There
		 * is a checksum field in the ROM to prevent false
		 * positives.  However, some common hardware (IBM thinkpads)
		 * neglects to put a valid checksum in the ROM, so we do
		 * not double check the checksum here.  On the ISA bus
		 * areas that have no hardware read back as 0xff, so the
		 * tests to see if we have 0x55 followed by 0xaa are
		 * generally sufficient.
		 */
		bus_release_resource(child, SYS_RES_MEMORY, rid, res);
		bus_delete_resource(child, SYS_RES_MEMORY, sc->rnum);
		if (buf[0] != 0x55 || buf[1] != 0xAA || (buf[2] & 0x03) != 0) {
			chunk += IOMEM_STEP;
			continue;
		}
		rom_size = buf[2] << 9;
		bus_set_resource(child, SYS_RES_MEMORY, sc->rnum, chunk,
		    rom_size);
		rid = sc->rnum;
		res = bus_alloc_resource_any(child, SYS_RES_MEMORY, &rid, 0);
		if (res == NULL) {
			bus_delete_resource(child, SYS_RES_MEMORY, sc->rnum);
			chunk += IOMEM_STEP;
			continue;
		}
		sc->rid[sc->rnum] = rid;
		sc->res[sc->rnum] = res;
		sc->rnum++;
		chunk += rom_size;
	}

	if (sc->rnum == 0)
		device_delete_child(parent, child);
	else if (sc->rnum == 1)
		device_set_desc(child, "ISA Option ROM");
	else
		device_set_desc(child, "ISA Option ROMs");
}

static int
orm_detach(device_t dev)
{
	int			i;
	struct orm_softc	*sc = device_get_softc(dev);
	
	for (i = 0; i < sc->rnum; i++)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid[i],
		    sc->res[i]);
	return (0);
}

static device_method_t orm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	orm_identify),
	DEVMETHOD(device_probe,		orm_probe),
	DEVMETHOD(device_attach,	orm_attach),
	DEVMETHOD(device_detach,	orm_detach),
	{ 0, 0 }
};

static driver_t orm_driver = {
	"orm",
	orm_methods,
	sizeof (struct orm_softc)
};

static devclass_t orm_devclass;

DRIVER_MODULE(orm, isa, orm_driver, orm_devclass, 0, 0);
ISA_PNP_INFO(orm_ids);
