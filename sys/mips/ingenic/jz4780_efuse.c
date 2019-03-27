/*-
 * Copyright (c) 2015 Alexander Kabaev <kan@freebsd.org>
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
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_regs.h>

static struct ofw_compat_data compat_data[] = {
	{"ingenic,jz4780-efuse",	1},
	{NULL,				0}
};

struct jz4780_efuse_data {
	uint32_t serial_num;
	uint32_t date;
	uint8_t  nanufacturer[2];
	uint8_t	 macaddr[6];
} __packed;

static struct resource_spec jz4780_efuse_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

struct jz4780_efuse_softc {
	device_t		dev;
	struct resource		*res[1];
	struct jz4780_efuse_data data;
};

#define CSR_WRITE_4(sc, reg, val) \
    bus_write_4((sc)->res[0], (reg), (val))
#define CSR_READ_4(sc, reg) \
    bus_read_4((sc)->res[0], (reg))

#define JZ_EFUSE_BANK_SIZE	(4096 / 8) /* Bank size is 4096 bits */

static int
jz4780_efuse_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static void
jz4780_efuse_read_chunk(struct jz4780_efuse_softc *sc, int addr, uint8_t *buf, int len)
{
	uint32_t abuf;
	int i, count;

	/* Setup to read proper bank */
	CSR_WRITE_4(sc, JZ_EFUCTRL, JZ_EFUSE_READ |
	    (addr < JZ_EFUSE_BANK_SIZE ? 0: JZ_EFUSE_BANK) |
	    (addr << JZ_EFUSE_ADDR_SHIFT) |
	    ((len - 1) << JZ_EFUSE_SIZE_SHIFT));
	/* Wait for read to complete */
	while ((CSR_READ_4(sc, JZ_EFUSTATE) & JZ_EFUSE_RD_DONE) == 0)
		DELAY(1000);

	/* Round to 4 bytes for the simple loop below */
	count = len & ~3;

	for (i = 0; i < count; i += 4) {
		abuf = CSR_READ_4(sc, JZ_EFUDATA0 + i);
		memcpy(buf, &abuf, 4);
		buf += 4;
	}

	/* Read partial word and assign it byte-by-byte */
	if (i < len) {
		abuf = CSR_READ_4(sc, JZ_EFUDATA0 + i);
		for (/* none */; i < len; i++) {
			buf[i] = abuf & 0xff;
			abuf >>= 8;
		}
	}
}

static void
jz4780_efuse_read(struct jz4780_efuse_softc *sc, int addr, void *buf, int len)
{
	int chunk;

	while (len > 0) {
		chunk = (len > 32) ? 32 : len;
		jz4780_efuse_read_chunk(sc, addr, buf, chunk);
		len -= chunk;
		buf = (void *)((uintptr_t)buf + chunk);
		addr += chunk;
	}
}

static void
jz4780_efuse_update_kenv(struct jz4780_efuse_softc *sc)
{
	char macstr[sizeof("xx:xx:xx:xx:xx:xx")];

	/*
	 * Update hint in kernel env only if none is available yet.
	 * It is quite possible one was set by command line already.
	 */
	if (kern_getenv("hint.dme.0.macaddr") == NULL) {
		snprintf(macstr, sizeof(macstr), "%6D",
		    sc->data.macaddr, ":");
		kern_setenv("hint.dme.0.macaddr", macstr);
	}
}

static int
jz4780_efuse_attach(device_t dev)
{
	struct jz4780_efuse_softc *sc;

 	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_efuse_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	/*
	 * Default RD_STROBE to 4 h2clk cycles, should already be set to 4 by  reset
	 * but configure it anyway.
	 */
	CSR_WRITE_4(sc, JZ_EFUCFG, 0x00040000);

	/* Read user-id segment */
	jz4780_efuse_read(sc, 0x18, &sc->data, sizeof(sc->data));

	/*
	 * Set resource hints for the dme device to discover its
	 * MAC address, if not set already.
	 */
	jz4780_efuse_update_kenv(sc);

	/* Resource conflicts with NEMC, release early */
	bus_release_resources(dev, jz4780_efuse_spec, sc->res);
	return (0);
}

static int
jz4780_efuse_detach(device_t dev)
{

	return (0);
}

static device_method_t jz4780_efuse_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_efuse_probe),
	DEVMETHOD(device_attach,	jz4780_efuse_attach),
	DEVMETHOD(device_detach,	jz4780_efuse_detach),

	DEVMETHOD_END
};

static driver_t jz4780_efuse_driver = {
	"efuse",
	jz4780_efuse_methods,
	sizeof(struct jz4780_efuse_softc),
};

static devclass_t jz4780_efuse_devclass;
EARLY_DRIVER_MODULE(jz4780_efuse, simplebus, jz4780_efuse_driver,
    jz4780_efuse_devclass, 0, 0, BUS_PASS_TIMER);
