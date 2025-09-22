/*	$OpenBSD: owctr.c,v 1.9 2022/04/06 18:59:29 naddy Exp $	*/
/*
 * Copyright (c) 2010 John L. Scarfone <john@scarfone.net>
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
 * DS2423 1-Wire 4kbit SRAM with Counter family type device driver.
 * Provides 4096 bits of SRAM and four 32-bit, read-only counters.
 * This driver provides access to the two externally triggered
 * counters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>

#include <dev/onewire/onewiredevs.h>
#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

/* Commands */
#define	DSCTR_CMD_READ_MEMCOUNTER	0xa5

/* External counter banks */
#define DS2423_COUNTER_BANK_A		0x1c0
#define DS2423_COUNTER_BANK_B		0x1e0

/* Buffer offsets */
#define DS2423_COUNTER_BUF_COUNTER	35
#define DS2423_COUNTER_BUF_CRC		43

#define DS2423_COUNTER_BUFSZ		45

struct owctr_softc {
	struct device		sc_dev;

	void *			sc_onewire;
	u_int64_t		sc_rom;

	struct ksensordev	sc_sensordev;

	struct ksensor		sc_counterA;
	struct ksensor		sc_counterB;

	struct sensor_task	*sc_sensortask;

	struct rwlock		sc_lock;
};

int	owctr_match(struct device *, void *, void *);
void	owctr_attach(struct device *, struct device *, void *);
int	owctr_detach(struct device *, int);
int	owctr_activate(struct device *, int);

void	owctr_update(void *);
void	owctr_update_counter(void *, int);

const struct cfattach owctr_ca = {
	sizeof(struct owctr_softc),
	owctr_match,
	owctr_attach,
	owctr_detach,
	owctr_activate
};

struct cfdriver owctr_cd = {
	NULL, "owctr", DV_DULL
};

static const struct onewire_matchfam owctr_fams[] = {
	{ ONEWIRE_FAMILY_DS2423 }
};

int
owctr_match(struct device *parent, void *match, void *aux)
{
	return (onewire_matchbyfam(aux, owctr_fams, nitems(owctr_fams)));
}

void
owctr_attach(struct device *parent, struct device *self, void *aux)
{
	struct owctr_softc *sc = (struct owctr_softc *)self;
	struct onewire_attach_args *oa = aux;

	sc->sc_onewire = oa->oa_onewire;
	sc->sc_rom = oa->oa_rom;

	/* Initialize counter sensors */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
		sizeof(sc->sc_sensordev.xname));
	sc->sc_counterA.type = SENSOR_INTEGER;
	snprintf(sc->sc_counterA.desc, sizeof(sc->sc_counterA.desc),
		"Counter A sn %012llx", ONEWIRE_ROM_SN(oa->oa_rom));
	sensor_attach(&sc->sc_sensordev, &sc->sc_counterA);
	sc->sc_counterB.type = SENSOR_INTEGER;
	snprintf(sc->sc_counterB.desc, sizeof(sc->sc_counterB.desc),
		"Counter B sn %012llx", ONEWIRE_ROM_SN(oa->oa_rom));
	sensor_attach(&sc->sc_sensordev, &sc->sc_counterB);

	sc->sc_sensortask = sensor_task_register(sc, owctr_update, 10);
	if (sc->sc_sensortask == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);
	printf("\n");
}

int
owctr_detach(struct device *self, int flags)
{
	struct owctr_softc *sc = (struct owctr_softc *)self;

	rw_enter_write(&sc->sc_lock);
	sensordev_deinstall(&sc->sc_sensordev);
	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);
	rw_exit_write(&sc->sc_lock);

	return (0);
}

int
owctr_activate(struct device *self, int act)
{
	return (0);
}

void
owctr_update(void *arg)
{
	owctr_update_counter(arg, DS2423_COUNTER_BANK_A);
	owctr_update_counter(arg, DS2423_COUNTER_BANK_B);
}

void
owctr_update_counter(void *arg, int bank)
{
	struct owctr_softc *sc = arg;
	u_int32_t counter;
	u_int16_t crc;
	u_int8_t *buf;

	rw_enter_write(&sc->sc_lock);
	onewire_lock(sc->sc_onewire, 0);
	if (onewire_reset(sc->sc_onewire) != 0)
		goto done;

	buf = malloc(DS2423_COUNTER_BUFSZ, M_DEVBUF, M_NOWAIT);
	if (buf == NULL) {
		printf("%s: malloc() failed\n", sc->sc_dev.dv_xname);
		goto done;
	}

	onewire_matchrom(sc->sc_onewire, sc->sc_rom);
	buf[0] = DSCTR_CMD_READ_MEMCOUNTER;
	buf[1] = bank;
	buf[2] = bank >> 8;
	onewire_write_byte(sc->sc_onewire, buf[0]);
	onewire_write_byte(sc->sc_onewire, buf[1]);
	onewire_write_byte(sc->sc_onewire, buf[2]);
	onewire_read_block(sc->sc_onewire, &buf[3], DS2423_COUNTER_BUFSZ-3);

	crc = onewire_crc16(buf, DS2423_COUNTER_BUFSZ-2);
	crc ^= buf[DS2423_COUNTER_BUF_CRC]
		| (buf[DS2423_COUNTER_BUF_CRC+1] << 8);
	if ( crc != 0xffff) {
		printf("%s: invalid CRC\n", sc->sc_dev.dv_xname);
		if (bank == DS2423_COUNTER_BANK_A) {
			sc->sc_counterA.value = 0;
			sc->sc_counterA.status = SENSOR_S_UNKNOWN;
			sc->sc_counterA.flags |= SENSOR_FUNKNOWN;
		} else {
			sc->sc_counterB.value = 0;
			sc->sc_counterB.status = SENSOR_S_UNKNOWN;
			sc->sc_counterB.flags |= SENSOR_FUNKNOWN;
		}
	} else {
		counter = buf[DS2423_COUNTER_BUF_COUNTER]
			| (buf[DS2423_COUNTER_BUF_COUNTER+1] << 8)
			| (buf[DS2423_COUNTER_BUF_COUNTER+2] << 16)
			| (buf[DS2423_COUNTER_BUF_COUNTER+3] << 24);
		if (bank == DS2423_COUNTER_BANK_A) {
			sc->sc_counterA.value = counter;
			sc->sc_counterA.status = SENSOR_S_UNSPEC;
			sc->sc_counterA.flags &= ~SENSOR_FUNKNOWN;
		} else {
			sc->sc_counterB.value = counter;
			sc->sc_counterB.status = SENSOR_S_UNSPEC;
			sc->sc_counterB.flags &= ~SENSOR_FUNKNOWN;
		}
	}

	onewire_reset(sc->sc_onewire);
	free(buf, M_DEVBUF, DS2423_COUNTER_BUFSZ);

done:
	onewire_unlock(sc->sc_onewire);
	rw_exit_write(&sc->sc_lock);
}
