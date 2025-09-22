/*	$OpenBSD: owsbm.c,v 1.12 2025/07/15 13:40:02 jsg Exp $	*/

/*
 * Copyright (c) 2007 Aaron Linville <aaron@linville.org>
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
 * 1-Wire Smart Battery Monitor family type device driver.
 * Provides on-board temperature, an A/D converter for voltage/current,
 * current accumulator, elapsed time meter, and 40 bytes of nonvolatile
 * memory.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>

#include <dev/onewire/onewiredevs.h>
#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

/* Commands */
#define	DSSBM_CMD_READ_SCRATCHPAD	0xbe
#define	DSSBM_CMD_WRITE_SCRATCHPAD	0x4e
#define	DSSBM_CMD_COPY_SCRATCHPAD	0x48

#define	DSSBM_CMD_RECALL_MEMORY		0xb8

#define	DSSBM_CMD_CONVERT_T		0x44
#define	DSSBM_CMD_CONVERT_V		0xb4	

/* Scratchpad layout */
#define DS2438_SP_STATUS		0
#define DS2438_SP_TEMP_LSB		1
#define DS2438_SP_TEMP_MSB		2
#define DS2438_SP_VOLT_LSB		3
#define DS2438_SP_VOLT_MSB		4
#define DS2438_SP_CURRENT_LSB		5
#define DS2438_SP_CURRENT_MSB		6
#define DS2438_SP_THRESHOLD		7
#define DS2438_SP_CRC			8

struct owsbm_softc {
	struct device		sc_dev;

	void *			sc_onewire;
	u_int64_t		sc_rom;

	struct ksensordev	sc_sensordev;

	struct ksensor		sc_temp;
	struct ksensor		sc_voltage_vdd; /* Battery, AD = 1*/
	struct ksensor		sc_voltage_vad; /* General purpose, AD = 0 */
	struct ksensor		sc_voltage_cr; /* Current Register */

	struct sensor_task	*sc_sensortask;

	struct rwlock		sc_lock;
};

int	owsbm_match(struct device *, void *, void *);
void	owsbm_attach(struct device *, struct device *, void *);
int	owsbm_detach(struct device *, int);
int	owsbm_activate(struct device *, int);

void	owsbm_update(void *);

const struct cfattach owsbm_ca = {
	sizeof(struct owsbm_softc),
	owsbm_match,
	owsbm_attach,
	owsbm_detach,
	owsbm_activate
};

struct cfdriver owsbm_cd = {
	NULL, "owsbm", DV_DULL
};

static const struct onewire_matchfam owsbm_fams[] = {
	{ ONEWIRE_FAMILY_DS2438 }
};

int
owsbm_match(struct device *parent, void *match, void *aux)
{
	return (onewire_matchbyfam(aux, owsbm_fams, nitems(owsbm_fams)));
}

void
owsbm_attach(struct device *parent, struct device *self, void *aux)
{
	struct owsbm_softc *sc = (struct owsbm_softc *)self;
	struct onewire_attach_args *oa = aux;

	sc->sc_onewire = oa->oa_onewire;
	sc->sc_rom = oa->oa_rom;

	/* Initialize temp sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_temp.type = SENSOR_TEMP;
	snprintf(sc->sc_temp.desc, sizeof(sc->sc_temp.desc), "sn %012llx",
	    ONEWIRE_ROM_SN(oa->oa_rom));
	sensor_attach(&sc->sc_sensordev, &sc->sc_temp);

	/* Initialize voltage sensor */
	sc->sc_voltage_vdd.type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_voltage_vdd.desc, "VDD", sizeof(sc->sc_voltage_vdd.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_voltage_vdd);

	/* Initialize voltage sensor */
	sc->sc_voltage_vad.type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_voltage_vad.desc, "VAD", sizeof(sc->sc_voltage_vad.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_voltage_vad);

	/* Initialize the current sensor */
	sc->sc_voltage_cr.type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_voltage_cr.desc, "CR", sizeof(sc->sc_voltage_cr.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_voltage_cr);

	sc->sc_sensortask = sensor_task_register(sc, owsbm_update, 10);
	if (sc->sc_sensortask == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);
	printf("\n");
}

int
owsbm_detach(struct device *self, int flags)
{
	struct owsbm_softc *sc = (struct owsbm_softc *)self;

	rw_enter_write(&sc->sc_lock);
	sensordev_deinstall(&sc->sc_sensordev);
	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);
	rw_exit_write(&sc->sc_lock);

	return (0);
}

int
owsbm_activate(struct device *self, int act)
{
	return (0);
}

void
owsbm_update(void *arg)
{
	struct owsbm_softc *sc = arg;
	u_int8_t data[9];

	rw_enter_write(&sc->sc_lock);
	onewire_lock(sc->sc_onewire, 0);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_CONVERT_T);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;
	
	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_CONVERT_V);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	/* Issue Recall Memory page 00h cmd */
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_RECALL_MEMORY);
	onewire_write_byte(sc->sc_onewire, 0);

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	/* Read page 0 of Memory Map from Scratchpad */
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_READ_SCRATCHPAD);
	onewire_write_byte(sc->sc_onewire, 0);
	onewire_read_block(sc->sc_onewire, data, 9);
	if (onewire_crc(data, 8) == data[DS2438_SP_CRC]) {
		sc->sc_temp.value = 273150000 +
		     (int)(((u_int16_t)data[DS2438_SP_TEMP_MSB] << 5) |
		     ((u_int16_t)data[DS2438_SP_TEMP_LSB] >> 3)) * 31250;
		sc->sc_voltage_vdd.value =
		     (int)(((u_int16_t)data[DS2438_SP_VOLT_MSB] << 8) |
		     data[DS2438_SP_VOLT_LSB]) * 10000;

		sc->sc_voltage_cr.value =
		    (int)(((u_int16_t)data[DS2438_SP_CURRENT_MSB] << 8) |
		    data[DS2438_SP_CURRENT_LSB]) * 244;
	}

	/* Reconfigure DS2438 to measure VAD */

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_WRITE_SCRATCHPAD);
	onewire_write_byte(sc->sc_onewire, 0);
	onewire_write_byte(sc->sc_onewire, 0x7); /* AD = 0 */

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_CONVERT_V);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	/* Issue Recall Memory page 00h cmd */
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_RECALL_MEMORY);
	onewire_write_byte(sc->sc_onewire, 0);

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_READ_SCRATCHPAD);
	onewire_write_byte(sc->sc_onewire, 0);
	onewire_read_block(sc->sc_onewire, data, 9);
	if (onewire_crc(data, 8) == data[8]) {
		sc->sc_voltage_vad.value =
		    (int)(((u_int16_t)data[DS2438_SP_VOLT_MSB] << 8) |
		    data[DS2438_SP_VOLT_LSB]) * 10000;
	}

	/* Reconfigure back DS2438 to measure VDD (default) */

	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	onewire_write_byte(sc->sc_onewire, DSSBM_CMD_WRITE_SCRATCHPAD);
	onewire_write_byte(sc->sc_onewire, 0);
	onewire_write_byte(sc->sc_onewire, 0xf); /* AD = 1 */
	onewire_reset(sc->sc_onewire);

done:
	onewire_unlock(sc->sc_onewire);
	rw_exit_write(&sc->sc_lock);
}
