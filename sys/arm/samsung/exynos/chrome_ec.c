/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * ANY EXPREC OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNEC FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINEC INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Samsung Chromebook Embedded Controller
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <sys/gpio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/iicbus/iiconf.h>

#include "iicbus_if.h"
#include "gpio_if.h"

#include <arm/samsung/exynos/chrome_ec.h>

struct ec_softc {
	device_t	dev;
	int		have_arbitrator;
	pcell_t		our_gpio;
	pcell_t		ec_gpio;
};

struct ec_softc *ec_sc;

/*
 * bus_claim, bus_release
 * both functions used for bus arbitration
 * in multi-master mode
 */

static int
bus_claim(struct ec_softc *sc)
{
	device_t gpio_dev;
	int status;

	if (sc->our_gpio == 0 || sc->ec_gpio == 0) {
		device_printf(sc->dev, "i2c arbitrator is not configured\n");
		return (1);
	}

	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		device_printf(sc->dev, "cant find gpio_dev\n");
		return (1);
	}

	/* Say we want the bus */
	GPIO_PIN_SET(gpio_dev, sc->our_gpio, GPIO_PIN_LOW);

	/* TODO: insert a delay to allow EC to react. */

	/* Check EC decision */
	GPIO_PIN_GET(gpio_dev, sc->ec_gpio, &status);

	if (status == 1) {
		/* Okay. We have bus */
		return (0);
	}

	/* EC is master */
	return (-1);
}

static int
bus_release(struct ec_softc *sc)
{
	device_t gpio_dev;

	if (sc->our_gpio == 0 || sc->ec_gpio == 0) {
		device_printf(sc->dev, "i2c arbitrator is not configured\n");
		return (1);
	}

	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		device_printf(sc->dev, "cant find gpio_dev\n");
		return (1);
	}

	GPIO_PIN_SET(gpio_dev, sc->our_gpio, GPIO_PIN_HIGH);

	return (0);
}

static int
ec_probe(device_t dev)
{

	device_set_desc(dev, "Chromebook Embedded Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
fill_checksum(uint8_t *data_out, int len)
{
	int res;
	int i;

	res = 0;
	for (i = 0; i < len; i++) {
		res += data_out[i];
	}

	data_out[len] = (res & 0xff);

	return (0);
}

int
ec_command(uint8_t cmd, uint8_t *dout, uint8_t dout_len,
    uint8_t *dinp, uint8_t dinp_len)
{
	struct ec_softc *sc;
	uint8_t *msg_dout;
	uint8_t *msg_dinp;
	int ret;
	int i;

	msg_dout = malloc(dout_len + 4, M_DEVBUF, M_NOWAIT);
	msg_dinp = malloc(dinp_len + 3, M_DEVBUF, M_NOWAIT);

	if (ec_sc == NULL)
		return (-1);

	sc = ec_sc;

	msg_dout[0] = EC_CMD_VERSION0;
	msg_dout[1] = cmd;
	msg_dout[2] = dout_len;

	for (i = 0; i < dout_len; i++) {
		msg_dout[i + 3] = dout[i];
	}

	fill_checksum(msg_dout, dout_len + 3);

	struct iic_msg msgs[] = {
		{ 0x1e, IIC_M_WR, dout_len + 4, msg_dout, },
		{ 0x1e, IIC_M_RD, dinp_len + 3, msg_dinp, },
	};

	ret = iicbus_transfer(sc->dev, msgs, 2);
	if (ret != 0) {
		device_printf(sc->dev, "i2c transfer returned %d\n", ret);
		free(msg_dout, M_DEVBUF);
		free(msg_dinp, M_DEVBUF);
		return (-1);
	}

	for (i = 0; i < dinp_len; i++) {
		dinp[i] = msg_dinp[i + 2];
	}

	free(msg_dout, M_DEVBUF);
	free(msg_dinp, M_DEVBUF);
	return (0);
}

int ec_hello(void)
{
	uint8_t data_in[4];
	uint8_t data_out[4];

	data_in[0] = 0x40;
	data_in[1] = 0x30;
	data_in[2] = 0x20;
	data_in[3] = 0x10;

	ec_command(EC_CMD_HELLO, data_in, 4,
	    data_out, 4);

	return (0);
}

static void
configure_i2c_arbitrator(struct ec_softc *sc)
{
	phandle_t arbitrator;

	/* TODO: look for compatible entry instead of hard-coded path */
	arbitrator = OF_finddevice("/i2c-arbitrator");
	if (arbitrator != -1 &&
	    OF_hasprop(arbitrator, "freebsd,our-gpio") &&
	    OF_hasprop(arbitrator, "freebsd,ec-gpio")) {
		sc->have_arbitrator = 1;
		OF_getencprop(arbitrator, "freebsd,our-gpio",
		    &sc->our_gpio, sizeof(sc->our_gpio));
		OF_getencprop(arbitrator, "freebsd,ec-gpio",
		    &sc->ec_gpio, sizeof(sc->ec_gpio));
	} else {
		sc->have_arbitrator = 0;
		sc->our_gpio = 0;
		sc->ec_gpio = 0;
	}
}

static int
ec_attach(device_t dev)
{
	struct ec_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	ec_sc = sc;

	configure_i2c_arbitrator(sc);

	/*
	 * Claim the bus.
	 *
	 * We don't know cases when EC is master,
	 * so hold the bus forever for us.
	 *
	 */

	if (sc->have_arbitrator && bus_claim(sc) != 0) {
		return (ENXIO);
	}

	return (0);
}

static int
ec_detach(device_t dev)
{
	struct ec_softc *sc;

	sc = device_get_softc(dev);

	if (sc->have_arbitrator) {
		bus_release(sc);
	}

	return (0);
}

static device_method_t ec_methods[] = {
	DEVMETHOD(device_probe,		ec_probe),
	DEVMETHOD(device_attach,	ec_attach),
	DEVMETHOD(device_detach,	ec_detach),
	{ 0, 0 }
};

static driver_t ec_driver = {
	"chrome_ec",
	ec_methods,
	sizeof(struct ec_softc),
};

static devclass_t ec_devclass;

DRIVER_MODULE(chrome_ec, iicbus, ec_driver, ec_devclass, 0, 0);
MODULE_VERSION(chrome_ec, 1);
MODULE_DEPEND(chrome_ec, iicbus, 1, 1, 1);
