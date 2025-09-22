/* $OpenBSD: cwfg.c,v 1.7 2022/04/06 18:59:28 naddy Exp $ */
/* $NetBSD: cwfg.c,v 1.1 2020/01/03 18:00:05 jmcneill Exp $ */
/*-
 * Copyright (c) 2020 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <machine/apmvar.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>

#include <dev/i2c/i2cvar.h>

#include "apm.h"

#define	VERSION_REG		0x00
#define	VCELL_HI_REG		0x02
#define	 VCELL_HI_MASK			0x3f
#define	 VCELL_HI_SHIFT			0
#define	VCELL_LO_REG		0x03
#define	 VCELL_LO_MASK			0xff
#define	 VCELL_LO_SHIFT			0
#define	SOC_HI_REG		0x04
#define	SOC_LO_REG		0x05
#define	RTT_ALRT_HI_REG		0x06
#define	 RTT_ALRT			(1 << 7)
#define	 RTT_HI_MASK			0x1f
#define	 RTT_HI_SHIFT			0
#define	RTT_ALRT_LO_REG		0x07
#define	 RTT_LO_MASK			0xff
#define	 RTT_LO_SHIFT			0
#define	CONFIG_REG		0x08
#define	 CONFIG_UFG			(1 << 1)
#define	MODE_REG		0x0a
#define	 MODE_SLEEP_MASK		(0x3 << 6)
#define	 MODE_SLEEP_WAKE		(0x0 << 6)
#define	 MODE_SLEEP_SLEEP		(0x3 << 6)
#define	 MODE_QSTRT_MASK		0x3
#define	 MODE_QSTRT_SHIFT		4
#define	 MODE_POR			(0xf << 0)
#define	BATINFO_REG(n)		(0x10 + (n))

#define	VCELL_STEP	312
#define	VCELL_DIV	1024
#define	BATINFO_SIZE	64
#define	RESET_COUNT	30
#define	RESET_DELAY	100000

enum cwfg_sensor {
	CWFG_SENSOR_VCELL,
	CWFG_SENSOR_SOC,
	CWFG_SENSOR_RTT,
	CWFG_NSENSORS
};

struct cwfg_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	int		sc_node;

	uint8_t		sc_batinfo[BATINFO_SIZE];

	struct ksensor	sc_sensor[CWFG_NSENSORS];
	struct ksensordev sc_sensordev;
};

int cwfg_match(struct device *, void *, void *);
void cwfg_attach(struct device *, struct device *, void *);

int cwfg_init(struct cwfg_softc *);
int cwfg_set_config(struct cwfg_softc *);
int cwfg_lock(struct cwfg_softc *);
void cwfg_unlock(struct cwfg_softc *);
int cwfg_read(struct cwfg_softc *, uint8_t, uint8_t *);
int cwfg_write(struct cwfg_softc *, uint8_t, uint8_t);
void cwfg_update_sensors(void *);

const struct cfattach cwfg_ca = {
	sizeof(struct cwfg_softc), cwfg_match, cwfg_attach
};

struct cfdriver cwfg_cd = {
	NULL, "cwfg", DV_DULL
};

#if NAPM > 0
struct apm_power_info cwfg_power = {
	.battery_state = APM_BATT_UNKNOWN,
	.ac_state = APM_AC_UNKNOWN,
	.battery_life = 0,
	.minutes_left = -1,
};

int
cwfg_apminfo(struct apm_power_info *info)
{
	memcpy(info, &cwfg_power, sizeof(*info));
	return 0;
}
#endif

int
cwfg_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "cellwise,cw2015") == 0)
		return 1;

	return 0;
}

void
cwfg_attach(struct device *parent, struct device *self, void *aux)
{
	struct cwfg_softc *sc = (struct cwfg_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint32_t *batinfo;
	ssize_t len;
	int n;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	len = OF_getproplen(sc->sc_node, "cellwise,battery-profile");
	if (len <= 0) {
		printf(": missing or invalid battery info\n");
		return;
	}

	batinfo = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(sc->sc_node, "cellwise,battery-profile", batinfo, len);
	switch (len) {
	case BATINFO_SIZE:
		memcpy(sc->sc_batinfo, batinfo, BATINFO_SIZE);
		break;
	case BATINFO_SIZE * 4:
		for (n = 0; n < BATINFO_SIZE; n++)
			sc->sc_batinfo[n] = be32toh(batinfo[n]);
		break;
	default:
		printf(": invalid battery info\n");
		free(batinfo, M_TEMP, len);
		return;
	}
	free(batinfo, M_TEMP, len);

	if (cwfg_init(sc) != 0) {
		printf(": failed to initialize device\n");
		return;
	}

	strlcpy(sc->sc_sensor[CWFG_SENSOR_VCELL].desc, "battery voltage",
	    sizeof(sc->sc_sensor[CWFG_SENSOR_VCELL].desc));
	sc->sc_sensor[CWFG_SENSOR_VCELL].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[CWFG_SENSOR_VCELL].flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[CWFG_SENSOR_VCELL]);

	strlcpy(sc->sc_sensor[CWFG_SENSOR_SOC].desc, "battery percent",
	    sizeof(sc->sc_sensor[CWFG_SENSOR_SOC].desc));
	sc->sc_sensor[CWFG_SENSOR_SOC].type = SENSOR_PERCENT;
	sc->sc_sensor[CWFG_SENSOR_SOC].flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[CWFG_SENSOR_SOC]);

	strlcpy(sc->sc_sensor[CWFG_SENSOR_RTT].desc, "battery remaining "
	    "minutes", sizeof(sc->sc_sensor[CWFG_SENSOR_RTT].desc));
	sc->sc_sensor[CWFG_SENSOR_RTT].type = SENSOR_INTEGER;
	sc->sc_sensor[CWFG_SENSOR_RTT].flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[CWFG_SENSOR_RTT]);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);

	sensor_task_register(sc, cwfg_update_sensors, 5);

#if NAPM > 0
	apm_setinfohook(cwfg_apminfo);
#endif

	printf("\n");
}

int
cwfg_init(struct cwfg_softc *sc)
{
	uint8_t mode, soc;
	int error, retry;

	cwfg_lock(sc);

	/* If the device is in sleep mode, wake it up */
	if ((error = cwfg_read(sc, MODE_REG, &mode)) != 0)
		goto done;
	if ((mode & MODE_SLEEP_MASK) == MODE_SLEEP_SLEEP) {
		mode &= ~MODE_SLEEP_MASK;
		mode |= MODE_SLEEP_WAKE;
		if ((error = cwfg_write(sc, MODE_REG, mode)) != 0)
			goto done;
	}

	/* Load battery profile */
	if ((error = cwfg_set_config(sc)) != 0)
		goto done;

	/* Wait for chip to become ready */
	for (retry = RESET_COUNT; retry > 0; retry--) {
		if ((error = cwfg_read(sc, SOC_HI_REG, &soc)) != 0)
			goto done;
		if (soc != 0xff)
			break;
		delay(RESET_DELAY);
	}
	if (retry == 0)
		printf("%s: timeout waiting for chip ready\n",
		    sc->sc_dev.dv_xname);

done:
	cwfg_unlock(sc);

	return error;
}

int
cwfg_set_config(struct cwfg_softc *sc)
{
	uint8_t config, mode, val;
	int need_update;
	int error, n;

	/* Read current config */
	if ((error = cwfg_read(sc, CONFIG_REG, &config)) != 0)
		return error;

	/*
	 * We need to upload a battery profile if either the UFG flag
	 * is unset, or the current battery profile differs from the
	 * one in the DT.
	 */
	need_update = !(config & CONFIG_UFG);
	if (!need_update) {
		for (n = 0; n < BATINFO_SIZE; n++) {
			if ((error = cwfg_read(sc, BATINFO_REG(n), &val)) != 0)
				return error;
			if (sc->sc_batinfo[n] != val) {
				need_update = 1;
				break;
			}
		}
	}
	if (!need_update)
		return 0;

	/* Update battery profile */
	for (n = 0; n < BATINFO_SIZE; n++) {
		val = sc->sc_batinfo[n];
		if ((error = cwfg_write(sc, BATINFO_REG(n), val)) != 0)
			return error;
	}

	/* Set UFG flag to switch to new profile */
	if ((error = cwfg_read(sc, CONFIG_REG, &config)) != 0)
		return error;
	config |= CONFIG_UFG;
	if ((error = cwfg_write(sc, CONFIG_REG, config)) != 0)
		return error;

	/* Restart the IC with new profile */
	if ((error = cwfg_read(sc, MODE_REG, &mode)) != 0)
		return error;
	mode |= MODE_POR;
	if ((error = cwfg_write(sc, MODE_REG, mode)) != 0)
		return error;
	delay(20000);
	mode &= ~MODE_POR;
	if ((error = cwfg_write(sc, MODE_REG, mode)) != 0)
		return error;

	return error;
}

void
cwfg_update_sensors(void *arg)
{
	struct cwfg_softc *sc = arg;
	uint32_t vcell, rtt, tmp;
	uint8_t val;
	int error, n;

	/* invalidate all previous reads to avoid stale/incoherent values
	 * in case of transient cwfg_read() failures below */
	sc->sc_sensor[CWFG_SENSOR_VCELL].flags |= SENSOR_FINVALID;
	sc->sc_sensor[CWFG_SENSOR_SOC].flags |= SENSOR_FINVALID;
	sc->sc_sensor[CWFG_SENSOR_RTT].flags |= SENSOR_FINVALID;

#if NAPM > 0
	cwfg_power.battery_state = APM_BATT_UNKNOWN;
	cwfg_power.ac_state = APM_AC_UNKNOWN;
	cwfg_power.battery_life = 0;
	cwfg_power.minutes_left = -1;
#endif

	if ((error = cwfg_lock(sc)) != 0)
		return;

	/* VCELL: Take the average of three readings */
	vcell = 0;
	for (n = 0; n < 3; n++) {
		if ((error = cwfg_read(sc, VCELL_HI_REG, &val)) != 0)
			goto done;
		tmp = ((val >> VCELL_HI_SHIFT) & VCELL_HI_MASK) << 8;
		if ((error = cwfg_read(sc, VCELL_LO_REG, &val)) != 0)
			goto done;
		tmp |= ((val >> VCELL_LO_SHIFT) & VCELL_LO_MASK);
		vcell += tmp;
	}
	vcell /= 3;
	sc->sc_sensor[CWFG_SENSOR_VCELL].value =
	    ((vcell * VCELL_STEP) / VCELL_DIV) * 1000;
	sc->sc_sensor[CWFG_SENSOR_VCELL].flags &= ~SENSOR_FINVALID;

	/* SOC */
	if ((error = cwfg_read(sc, SOC_HI_REG, &val)) != 0)
		goto done;
	if (val != 0xff) {
		sc->sc_sensor[CWFG_SENSOR_SOC].value = val * 1000;
		sc->sc_sensor[CWFG_SENSOR_SOC].flags &= ~SENSOR_FINVALID;
#if NAPM > 0
		cwfg_power.battery_life = val;
		if (val > 50)
			cwfg_power.battery_state = APM_BATT_HIGH;
		else if (val > 25)
			cwfg_power.battery_state = APM_BATT_LOW;
		else
			cwfg_power.battery_state = APM_BATT_CRITICAL;
#endif
	}

	/* RTT */
	if ((error = cwfg_read(sc, RTT_ALRT_HI_REG, &val)) != 0)
		goto done;
	rtt = ((val >> RTT_HI_SHIFT) & RTT_HI_MASK) << 8;
	if ((error = cwfg_read(sc, RTT_ALRT_LO_REG, &val)) != 0)
		goto done;
	rtt |= ((val >> RTT_LO_SHIFT) & RTT_LO_MASK);
	if (rtt != 0x1fff) {
		sc->sc_sensor[CWFG_SENSOR_RTT].value = rtt;
		sc->sc_sensor[CWFG_SENSOR_RTT].flags &= ~SENSOR_FINVALID;
#if NAPM > 0
		cwfg_power.minutes_left = rtt;
#endif
	}

done:
	cwfg_unlock(sc);
}

int
cwfg_lock(struct cwfg_softc *sc)
{
	return iic_acquire_bus(sc->sc_tag, 0);
}

void
cwfg_unlock(struct cwfg_softc *sc)
{
	iic_release_bus(sc->sc_tag, 0);
}

int
cwfg_read(struct cwfg_softc *sc, uint8_t reg, uint8_t *val)
{
	return iic_smbus_read_byte(sc->sc_tag, sc->sc_addr, reg, val, 0);
}

int
cwfg_write(struct cwfg_softc *sc, uint8_t reg, uint8_t val)
{
	return iic_smbus_write_byte(sc->sc_tag, sc->sc_addr, reg, val, 0);
}
