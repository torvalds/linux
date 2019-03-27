/*-
 * Copyright (C) 2015 Justin Hibbits
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

/* RouterBoard 600/800 NAND controller driver. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/slicer.h>

#include <geom/geom_disk.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "nfc_if.h"
#include "gpio_if.h"

#define RB_NAND_DATA	(0x00)

struct rb_nand_softc {
	struct nand_softc 	nand_dev;
	struct resource		*sc_mem;
	int			rid;
	device_t		sc_gpio;
	uint32_t		sc_rdy_pin;
	uint32_t		sc_nce_pin;
	uint32_t		sc_cle_pin;
	uint32_t		sc_ale_pin;
};

static int	rb_nand_attach(device_t);
static int	rb_nand_probe(device_t);
static int	rb_nand_send_command(device_t, uint8_t);
static int	rb_nand_send_address(device_t, uint8_t);
static uint8_t	rb_nand_read_byte(device_t);
static void	rb_nand_read_buf(device_t, void *, uint32_t);
static void	rb_nand_write_buf(device_t, void *, uint32_t);
static int	rb_nand_select_cs(device_t, uint8_t);
static int	rb_nand_read_rnb(device_t);

static device_method_t rb_nand_methods[] = {
	DEVMETHOD(device_probe,		rb_nand_probe),
	DEVMETHOD(device_attach,	rb_nand_attach),

	DEVMETHOD(nfc_send_command,	rb_nand_send_command),
	DEVMETHOD(nfc_send_address,	rb_nand_send_address),
	DEVMETHOD(nfc_read_byte,	rb_nand_read_byte),
	DEVMETHOD(nfc_read_buf,		rb_nand_read_buf),
	DEVMETHOD(nfc_write_buf,	rb_nand_write_buf),
	DEVMETHOD(nfc_select_cs,	rb_nand_select_cs),
	DEVMETHOD(nfc_read_rnb,		rb_nand_read_rnb),

	{ 0, 0 },
};

static driver_t rb_nand_driver = {
	"nand",
	rb_nand_methods,
	sizeof(struct rb_nand_softc),
};

static devclass_t rb_nand_devclass;
DRIVER_MODULE(rb_nand, ofwbus, rb_nand_driver, rb_nand_devclass, 0, 0);

#if 0
static const struct nand_ecc_data rb_ecc = {
	.eccsize = 6,
	.eccmode = NAND_ECC_SOFT,
	.eccbytes = 6,
	.eccpositions = { 8, 9, 10, 13, 14, 15 },
};
#endif

/* Slicer operates on the NAND controller, so we have to find the chip. */
static int
rb_nand_slicer(device_t dev, const char *provider __unused,
    struct flash_slice *slices, int *nslices)
{
	struct nand_chip *chip;
	device_t *children;
	int n;

	if (device_get_children(dev, &children, &n) != 0) {
		panic("Slicer called on controller with no child!");
	}
	dev = children[0];
	free(children, M_TEMP);

	if (device_get_children(dev, &children, &n) != 0) {
		panic("Slicer called on controller with nandbus but no child!");
	}
	dev = children[0];
	free(children, M_TEMP);

	chip = device_get_softc(dev);
	*nslices = 2;
	slices[0].base = 0;
	slices[0].size = 4 * 1024 * 1024;
	slices[0].label = "boot";

	slices[1].base = 4 * 1024 * 1024;
	slices[1].size = chip->ndisk->d_mediasize - slices[0].size;
	slices[1].label = "rootfs";

	return (0);
}

static int
rb_nand_probe(device_t dev)
{
	const char *device_type;

	device_type = ofw_bus_get_type(dev);

	if (!device_type || strcmp(device_type, "rb,nand"))
		return (ENXIO);

	device_set_desc(dev, "RouterBoard 333/600/800 NAND controller");
	return (BUS_PROBE_DEFAULT);
}

static int
rb_nand_attach(device_t dev)
{
	struct rb_nand_softc *sc;
	phandle_t node;
	uint32_t ale[2],cle[2],nce[2],rdy[2];
	u_long size,start;
	int err;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	if (OF_getprop(node, "ale", ale, sizeof(ale)) <= 0) {
		return (ENXIO);
	}
	if (OF_getprop(node, "cle", cle, sizeof(cle)) <= 0) {
		return (ENXIO);
	}
	if (OF_getprop(node, "nce", nce, sizeof(nce)) <= 0) {
		return (ENXIO);
	}
	if (OF_getprop(node, "rdy", rdy, sizeof(rdy)) <= 0) {
		return (ENXIO);
	}

	if (ale[0] != cle[0] || ale[0] != nce[0] || ale[0] != rdy[0]) {
		device_printf(dev, "GPIO handles for signals must match.\n");
		return (ENXIO);
	}
	sc->sc_ale_pin = ale[1];
	sc->sc_cle_pin = cle[1];
	sc->sc_nce_pin = nce[1];
	sc->sc_rdy_pin = rdy[1];

	sc->sc_gpio = OF_device_from_xref(ale[0]);
	if (sc->sc_gpio == NULL) {
		device_printf(dev, "No GPIO resource found!\n");
		return (ENXIO);
	}

	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(dev, "could not allocate resources!\n");
		return (ENXIO);
	}

	start = rman_get_start(sc->sc_mem);
	size = rman_get_size(sc->sc_mem);
	if (law_enable(OCP85XX_TGTIF_LBC, start, size) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->sc_mem);
		device_printf(dev, "could not allocate local address window.\n");
		return (ENXIO);
	}

	flash_register_slicer(rb_nand_slicer, FLASH_SLICES_TYPE_NAND, TRUE);

	nand_init(&sc->nand_dev, dev, NAND_ECC_SOFT, 0, 0, NULL, NULL);

	err = nandbus_create(dev);

	return (err);
}

static int
rb_nand_send_command(device_t dev, uint8_t command)
{
	struct rb_nand_softc *sc;

	nand_debug(NDBG_DRV,"rb_nand: send command %x", command);

	sc = device_get_softc(dev);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_cle_pin, 1);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_ale_pin, 0);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_nce_pin, 0);
	bus_write_1(sc->sc_mem, RB_NAND_DATA, command);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_cle_pin, 0);
	return (0);
}

static int
rb_nand_send_address(device_t dev, uint8_t addr)
{
	struct rb_nand_softc *sc;

	nand_debug(NDBG_DRV,"rb_nand: send address %x", addr);

	sc = device_get_softc(dev);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_cle_pin, 0);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_ale_pin, 1);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_nce_pin, 0);
	bus_write_1(sc->sc_mem, RB_NAND_DATA, addr);
	GPIO_PIN_SET(sc->sc_gpio, sc->sc_ale_pin, 0);
	return (0);
}

static uint8_t
rb_nand_read_byte(device_t dev)
{
	struct rb_nand_softc *sc;
	uint8_t data;

	sc = device_get_softc(dev);
	data = bus_read_1(sc->sc_mem, RB_NAND_DATA);

	nand_debug(NDBG_DRV,"rb_nand: read %x", data);

	return (data);
}

static void
rb_nand_read_buf(device_t dev, void* buf, uint32_t len)
{
	struct rb_nand_softc *sc;

	sc = device_get_softc(dev);

	bus_read_region_1(sc->sc_mem, RB_NAND_DATA, buf, len);
}

static void
rb_nand_write_buf(device_t dev, void* buf, uint32_t len)
{
	struct rb_nand_softc *sc;
	int i;
	uint8_t *b = (uint8_t*)buf;

	sc = device_get_softc(dev);

	for (i = 0; i < len; i++) {
#ifdef NAND_DEBUG
		if (!(i % 16))
			printf("%s", i == 0 ? "rb_nand:\n" : "\n");
		printf(" %x", b[i]);
		if (i == len - 1)
			printf("\n");
#endif
		bus_write_1(sc->sc_mem, RB_NAND_DATA, b[i]);
	}
}

static int
rb_nand_select_cs(device_t dev, uint8_t cs)
{

	if (cs > 0)
		return (ENODEV);

	return (0);
}

static int
rb_nand_read_rnb(device_t dev)
{
	struct rb_nand_softc *sc;
	uint32_t rdy_bit;

	sc = device_get_softc(dev);
	GPIO_PIN_GET(sc->sc_gpio, sc->sc_rdy_pin, &rdy_bit);

	return (rdy_bit); /* ready */
}
