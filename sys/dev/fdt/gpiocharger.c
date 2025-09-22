/*	$OpenBSD: gpiocharger.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2021 Klemens Nanni <kn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/gpio/gpiovar.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <sys/sensors.h>

struct gpiocharger_softc {
	struct device		 sc_dev;
	int			 sc_node;
	uint32_t		*sc_charger_pin;
	struct ksensor		 sc_sensor;
	struct ksensordev	 sc_sensordev;
};

int	gpiocharger_match(struct device *, void *, void *);
void	gpiocharger_attach(struct device *, struct device *, void *);

const struct cfattach gpiocharger_ca = {
	sizeof (struct gpiocharger_softc), gpiocharger_match, gpiocharger_attach
};

struct cfdriver gpiocharger_cd = {
	NULL, "gpiocharger", DV_DULL
};

void	gpiocharger_update_charger(void *);

int
gpiocharger_match(struct device *parent, void *match, void *aux)
{
	const struct fdt_attach_args	*faa = aux;

	return OF_is_compatible(faa->fa_node, "gpio-charger");
}

void
gpiocharger_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpiocharger_softc	*sc = (struct gpiocharger_softc *)self;
	struct fdt_attach_args		*faa = aux;
	char				*charger_type, *gpios_property;
	int				 charger_type_len, gpios_len;
	int				 node = faa->fa_node;

	pinctrl_byname(node, "default");

	charger_type_len = OF_getproplen(node, "charger-type");
	if (charger_type_len <= 0)
		goto nocharger;
	gpios_property = "gpios";
	gpios_len = OF_getproplen(node, gpios_property);
	if (gpios_len <= 0) {
		gpios_property = "charger-status-gpios";
		gpios_len = OF_getproplen(node, gpios_property);
		if (gpios_len <= 0)
			goto nocharger;
	}

	charger_type = malloc(charger_type_len, M_TEMP, M_WAITOK);
	OF_getprop(node, "charger-type", charger_type, charger_type_len);
	sc->sc_charger_pin = malloc(gpios_len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(node, gpios_property, sc->sc_charger_pin, gpios_len);
	gpio_controller_config_pin(sc->sc_charger_pin, GPIO_CONFIG_INPUT);

	strlcpy(sc->sc_sensor.desc, charger_type, sizeof(sc->sc_sensor.desc));
	strlcat(sc->sc_sensor.desc, " power supply",
	    sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.type = SENSOR_INDICATOR;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, gpiocharger_update_charger, 5);

	printf(": \"%s\"\n", charger_type);
	free(charger_type, M_TEMP, charger_type_len);
	return;

nocharger:
	printf(": no charger\n");
}

void
gpiocharger_update_charger(void *arg)
{
	struct gpiocharger_softc	*sc = arg;

	sc->sc_sensor.value = gpio_controller_get_pin(sc->sc_charger_pin);
}
