/*	$OpenBSD: owtemp.c,v 1.18 2022/04/06 18:59:29 naddy Exp $	*/

/*
 * Copyright (c) 2006, 2009 Alexander Yurchenko <grange@openbsd.org>
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

/*
 * 1-Wire temperature family type device driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>

#include <dev/onewire/onewiredevs.h>
#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

/* Commands */
#define DS1920_CMD_CONVERT		0x44
#define DS1920_CMD_READ_SCRATCHPAD	0xbe

/* Scratchpad layout */
#define DS1920_SP_TEMP_LSB		0
#define DS1920_SP_TEMP_MSB		1
#define DS1920_SP_TH			2
#define DS1920_SP_TL			3
#define DS18B20_SP_CONFIG		4
#define DS1920_SP_COUNT_REMAIN		6
#define DS1920_SP_COUNT_PERC		7
#define DS1920_SP_CRC			8

struct owtemp_softc {
	struct device		sc_dev;

	void *			sc_onewire;
	u_int64_t		sc_rom;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
	struct sensor_task	*sc_sensortask;
	struct rwlock		sc_lock;
};

int	owtemp_match(struct device *, void *, void *);
void	owtemp_attach(struct device *, struct device *, void *);
int	owtemp_detach(struct device *, int);
int	owtemp_activate(struct device *, int);

void	owtemp_update(void *);

const struct cfattach owtemp_ca = {
	sizeof(struct owtemp_softc),
	owtemp_match,
	owtemp_attach,
	owtemp_detach,
	owtemp_activate
};

struct cfdriver owtemp_cd = {
	NULL, "owtemp", DV_DULL
};

static const struct onewire_matchfam owtemp_fams[] = {
	{ ONEWIRE_FAMILY_DS1920 },
	{ ONEWIRE_FAMILY_DS18B20 },
	{ ONEWIRE_FAMILY_DS1822 }
};

int
owtemp_match(struct device *parent, void *match, void *aux)
{
	return (onewire_matchbyfam(aux, owtemp_fams, nitems(owtemp_fams)));
}

void
owtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct owtemp_softc *sc = (struct owtemp_softc *)self;
	struct onewire_attach_args *oa = aux;

	sc->sc_onewire = oa->oa_onewire;
	sc->sc_rom = oa->oa_rom;

	/* Initialize sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;
	snprintf(sc->sc_sensor.desc, sizeof(sc->sc_sensor.desc), "sn %012llx",
	    ONEWIRE_ROM_SN(oa->oa_rom));

	sc->sc_sensortask = sensor_task_register(sc, owtemp_update, 5);
	if (sc->sc_sensortask == NULL) {
		printf(": unable to register update task\n");
		return;
	}
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);

	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);
	printf("\n");
}

int
owtemp_detach(struct device *self, int flags)
{
	struct owtemp_softc *sc = (struct owtemp_softc *)self;

	rw_enter_write(&sc->sc_lock);
	sensordev_deinstall(&sc->sc_sensordev);
	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);
	rw_exit_write(&sc->sc_lock);

	return (0);
}

int
owtemp_activate(struct device *self, int act)
{
	return (0);
}

void
owtemp_update(void *arg)
{
	struct owtemp_softc *sc = arg;
	u_int8_t data[9];
	int16_t temp;
	int count_perc, count_remain, val;

	rw_enter_write(&sc->sc_lock);
	onewire_lock(sc->sc_onewire, 0);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;
	onewire_matchrom(sc->sc_onewire, sc->sc_rom);

	/*
	 * Start temperature conversion. The conversion takes up to 750ms.
	 * After sending the command, the data line must be held high for
	 * at least 750ms to provide power during the conversion process.
	 * As such, no other activity may take place on the 1-Wire bus for
	 * at least this period.
	 */
	onewire_write_byte(sc->sc_onewire, DS1920_CMD_CONVERT);
	tsleep_nsec(sc, PRIBIO, "owtemp", SEC_TO_NSEC(1));

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;
	onewire_matchrom(sc->sc_onewire, sc->sc_rom);

	/*
	 * The result of the temperature measurement is placed in the
	 * first two bytes of the scratchpad.
	 */
	onewire_write_byte(sc->sc_onewire, DS1920_CMD_READ_SCRATCHPAD);
	onewire_read_block(sc->sc_onewire, data, 9);
	if (onewire_crc(data, 8) == data[DS1920_SP_CRC]) {
		temp = data[DS1920_SP_TEMP_MSB] << 8 |
		    data[DS1920_SP_TEMP_LSB];
		if (ONEWIRE_ROM_FAMILY(sc->sc_rom) == ONEWIRE_FAMILY_DS18B20 ||
		    ONEWIRE_ROM_FAMILY(sc->sc_rom) == ONEWIRE_FAMILY_DS1822) {
			/*
			 * DS18B20 decoding
			 * default 12 bit 0.0625 C resolution
			 */
			val = temp * (1000000 / 16);
		} else {
			/* DS1920 decoding */
			count_perc = data[DS1920_SP_COUNT_PERC];
			count_remain = data[DS1920_SP_COUNT_REMAIN];

			if (count_perc != 0) {
				/* High resolution algorithm */
				temp &= ~0x0001;
				val = temp * 500000 - 250000 +
				    ((count_perc - count_remain) * 1000000) /
				    count_perc;
			} else {
				val = temp * 500000;
			}
		}
		sc->sc_sensor.value = 273150000 + val;
	}

done:
	onewire_unlock(sc->sc_onewire);
	rw_exit_write(&sc->sc_lock);
}
