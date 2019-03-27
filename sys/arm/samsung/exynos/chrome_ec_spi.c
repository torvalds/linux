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
 * Samsung Chromebook Embedded Controller (EC)
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

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include "spibus_if.h"
#include "gpio_if.h"

#include <arm/samsung/exynos/chrome_ec.h>

struct ec_softc {
	device_t	dev;
	device_t	dev_gpio;
};

struct ec_softc *ec_sc;

#define EC_SPI_CS	200

static int
assert_cs(struct ec_softc *sc, int enable)
{
	/* Get the GPIO device */
	sc->dev_gpio = devclass_get_device(devclass_find("gpio"), 0);
	if (sc->dev_gpio == NULL) {
		device_printf(sc->dev, "Error: failed to get the GPIO dev\n");
		return (1);
	}

	GPIO_PIN_SETFLAGS(sc->dev_gpio, EC_SPI_CS, GPIO_PIN_OUTPUT);

	if (enable) {
		GPIO_PIN_SET(sc->dev_gpio, EC_SPI_CS, GPIO_PIN_LOW);
	} else {
		GPIO_PIN_SET(sc->dev_gpio, EC_SPI_CS, GPIO_PIN_HIGH);
	}

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
	struct spi_command spi_cmd;
	struct ec_softc *sc;
	uint8_t *msg_dout;
	uint8_t *msg_dinp;
	int ret;
	int i;

	memset(&spi_cmd, 0, sizeof(spi_cmd));

	msg_dout = malloc(dout_len + 4, M_DEVBUF, M_NOWAIT | M_ZERO);
	msg_dinp = malloc(dinp_len + 4, M_DEVBUF, M_NOWAIT | M_ZERO);

	spi_cmd.tx_cmd = msg_dout;
	spi_cmd.rx_cmd = msg_dinp;

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

	assert_cs(sc, 1);
	spi_cmd.rx_cmd_sz = spi_cmd.tx_cmd_sz = dout_len + 4;
	ret = SPIBUS_TRANSFER(device_get_parent(sc->dev), sc->dev, &spi_cmd);

	/* Wait 0xec */
	for (i = 0; i < 1000; i++) {
		DELAY(10);
		msg_dout[0] = 0xff;
		spi_cmd.rx_cmd_sz = spi_cmd.tx_cmd_sz = 1;
		SPIBUS_TRANSFER(device_get_parent(sc->dev), sc->dev, &spi_cmd);
		if (msg_dinp[0] == 0xec)
			break;
	}

	/* Get the rest */
	for (i = 0; i < (dout_len + 4); i++)
		msg_dout[i] = 0xff;
	spi_cmd.rx_cmd_sz = spi_cmd.tx_cmd_sz = dout_len + 4 - 1;
	ret = SPIBUS_TRANSFER(device_get_parent(sc->dev), sc->dev, &spi_cmd);
	assert_cs(sc, 0);

	if (ret != 0) {
		device_printf(sc->dev, "spibus_transfer returned %d\n", ret);
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

static int
ec_attach(device_t dev)
{
	struct ec_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	ec_sc = sc;

	return (0);
}

static int
ec_detach(device_t dev)
{
	struct ec_softc *sc;

	sc = device_get_softc(dev);

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

DRIVER_MODULE(chrome_ec, spibus, ec_driver, ec_devclass, 0, 0);
MODULE_VERSION(chrome_ec, 1);
MODULE_DEPEND(chrome_ec, spibus, 1, 1, 1);
