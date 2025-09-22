/*	$OpenBSD: gpiokeys.c,v 1.7 2025/09/08 19:32:57 kettenis Exp $	*/
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
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/gpio/gpiovar.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <sys/sensors.h>

#ifdef SUSPEND
extern int cpu_suspended;
#endif

extern int lid_action;
extern void (*simplefb_burn_hook)(u_int);

#define	DEVNAME(_s)	((_s)->sc_dev.dv_xname)

/*
 * Defines from Linux, see:
 *	Documentation/input/event-codes.rst
 *	include/dt-bindings/input/linux-event-codes.h
 */
enum gpiokeys_event_type {
	GPIOKEYS_EV_KEY = 1,
	GPIOKEYS_EV_SW = 5,
};

enum gpiokeys_switch_event {
	GPIOKEYS_SW_LID = 0,	/* set = lid closed */
};

enum gpiokeys_key_event {
	GPIOKEYS_KEY_POWER = 116,
};

struct gpiokeys_key {
	uint32_t			*key_pin;
	uint32_t			 key_input_type;
	uint32_t			 key_code;
	int				 key_state;
	struct ksensor			 key_sensor;
	SLIST_ENTRY(gpiokeys_key)	 key_next;
	void				 (*key_func)(void *);
	void				*key_ih;
	int				 key_wakeup;
};

struct gpiokeys_softc {
	struct device			 sc_dev;
	struct ksensordev		 sc_sensordev;
	SLIST_HEAD(, gpiokeys_key)	 sc_keys;
};

int	 gpiokeys_match(struct device *, void *, void *);
void	 gpiokeys_attach(struct device *, struct device *, void *);

const struct cfattach gpiokeys_ca = {
	sizeof (struct gpiokeys_softc), gpiokeys_match, gpiokeys_attach
};

struct cfdriver gpiokeys_cd = {
	NULL, "gpiokeys", DV_DULL
};

void	 gpiokeys_update_key(void *);
void	 gpiokeys_power_button(void *);
int	 gpiokeys_intr(void *);

int
gpiokeys_match(struct device *parent, void *match, void *aux)
{
	const struct fdt_attach_args	*faa = aux;

	return OF_is_compatible(faa->fa_node, "gpio-keys") ||
	    OF_is_compatible(faa->fa_node, "gpio-keys-polled");
}

void
gpiokeys_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpiokeys_softc	*sc = (struct gpiokeys_softc *)self;
	struct fdt_attach_args	*faa = aux;
	struct gpiokeys_key	*key;
	char			*label;
	uint32_t		 code;
	int			 node, len, gpios_len;
	int			 have_labels = 0, have_sensors = 0;

	SLIST_INIT(&sc->sc_keys);

	pinctrl_byname(faa->fa_node, "default");

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		code = OF_getpropint(node, "linux,code", -1);
		if (code == -1)
			continue;
		gpios_len = OF_getproplen(node, "gpios");
		if (gpios_len <= 0)
			continue;
		label = NULL;
		len = OF_getproplen(node, "label");
		if (len > 0) {
			label = malloc(len, M_TEMP, M_WAITOK);
			if (OF_getprop(node, "label", label, len) != len) {
				free(label, M_TEMP, len);
				continue;
			}
		}
		key = malloc(sizeof(*key), M_DEVBUF, M_WAITOK | M_ZERO);
		key->key_input_type = OF_getpropint(node, "linux,input-type",
		    GPIOKEYS_EV_KEY);
		key->key_code = code;
		key->key_pin = malloc(gpios_len, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(node, "gpios", key->key_pin, gpios_len);
		gpio_controller_config_pin(key->key_pin, GPIO_CONFIG_INPUT);

		switch (key->key_input_type) {
		case GPIOKEYS_EV_SW:
			switch (key->key_code) {
			case GPIOKEYS_SW_LID:
				strlcpy(key->key_sensor.desc, "lid open",
				    sizeof(key->key_sensor.desc));
				key->key_sensor.type = SENSOR_INDICATOR;
				sensor_attach(&sc->sc_sensordev,
				    &key->key_sensor);
				key->key_func = gpiokeys_update_key;
				key->key_wakeup = 1;
				have_sensors = 1;
				break;
			}
			break;
		case GPIOKEYS_EV_KEY:
			switch (key->key_code) {
			case GPIOKEYS_KEY_POWER:
				key->key_func = gpiokeys_power_button;
				break;
			}
			break;
		}

		if (label) {
			printf("%s \"%s\"", have_labels ? "," : ":", label);
			free(label, M_TEMP, len);
			have_labels = 1;
		}

		SLIST_INSERT_HEAD(&sc->sc_keys, key, key_next);
	}

	SLIST_FOREACH(key, &sc->sc_keys, key_next) {
		if (!key->key_func)
			continue;

		if (OF_is_compatible(faa->fa_node, "gpio-keys")) {
			int wakeup = key->key_wakeup ? IPL_WAKEUP : 0;
			key->key_ih =
			    gpio_controller_intr_establish(key->key_pin,
				IPL_BIO | wakeup, NULL, gpiokeys_intr, key,
				DEVNAME(sc));
		}
#ifdef SUSPEND
		if (key->key_wakeup && key->key_ih)
			device_register_wakeup(&sc->sc_dev);
#endif
		if (key->key_ih == NULL)
			sensor_task_register(key, gpiokeys_update_key, 1);
		else
			gpiokeys_update_key(key);
	}

	if (have_sensors) {
		strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
		    sizeof(sc->sc_sensordev.xname));
		sensordev_install(&sc->sc_sensordev);
	}

	if (SLIST_EMPTY(&sc->sc_keys))
		printf(": no keys");
	printf("\n");
}

void
gpiokeys_update_key(void *arg)
{
	struct gpiokeys_key	*key = arg;
	int			 val;

	val = gpio_controller_get_pin(key->key_pin);

	switch (key->key_input_type) {
	case GPIOKEYS_EV_SW:
		switch (key->key_code) {
		case GPIOKEYS_SW_LID:
#ifdef SUSPEND
			/*
			 * If we're suspended and the lid is now open,
			 * resume.  Otherwise, ignore the event and
			 * stay suspended.
			 */
			if (cpu_suspended) {
				if (!val)
					cpu_suspended = 0;
				return;
			}
#endif

			/*
			 * Match acpibtn(4), i.e. closed ThinkPad lid yields
			 * hw.sensors.acpibtn1.indicator0=Off (lid open)
			 */
			key->key_sensor.value = !val;

			switch (lid_action) {
			case 0:
				if (simplefb_burn_hook)
					simplefb_burn_hook(!val);
				break;
			case 1:
#ifdef SUSPEND
				if (val)
					request_sleep(SLEEP_SUSPEND);
#endif
				break;
			case 2:
				/* XXX: hibernate */
				break;
			}
			break;
		}
		break;
	}
}

void
gpiokeys_power_button(void *arg)
{
	struct gpiokeys_key *key = arg;
	int state;

	state = gpio_controller_get_pin(key->key_pin);

	if (state != key->key_state) {
		/* Ignore presses, handle releases. */
		if (!state)
			powerbutton_event();
		key->key_state = state;
	}
}

int
gpiokeys_intr(void *arg)
{
	struct gpiokeys_key *key = arg;

	key->key_func(key);
	return 1;
}
