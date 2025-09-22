/*	$OpenBSD: stsec.c,v 1.5 2016/12/05 15:04:15 fcambus Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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
 * Gdium ST7 Embedded Controller I2C interface
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/apmvar.h>
#include <dev/i2c/i2cvar.h>

#include "apm.h"

extern int gdium_revision;

#define	ST7_VERSION	0x00	/* only on later mobos */

#define	ST7_STATUS	0x00
#define	STS_LID_CLOSED		0x01
#define	STS_POWER_BTN_DOWN	0x02
#define	STS_BATTERY_PRESENT	0x04	/* not available on old mobo */
#define	STS_POWER_AVAILABLE	0x08
#define	STS_WAVELAN_BTN_DOWN	0x10	/* ``enable'' on old mobo */
#define	STS_AC_AVAILABLE	0x20
#define	ST7_CONTROL	0x01
#define	STC_DDR_CLOCK		0x01
#define	STC_CHARGE_LED_LIT	0x02
#define	STC_BEEP		0x04
#define	STC_DDR_POWER		0x08
#define	STC_TRICKLE		0x10	/* trickle charge rate */
#define	STC_RADIO_ENABLE	0x20	/* enable wavelan rf, later mobos */
#define	STC_MAIN_POWER		0x40
#define	STC_CHARGE_ENABLE	0x80
#define	ST7_BATTERY_L	0x02
#define	ST7_BATTERY_H	0x03
#define	STB_VALUE(h,l)	(((h) << 2) | ((l) & 0x03))
#define	ST7_SIGNATURE	0x04
#define	STSIG_EC_CONTROL	0x00
#define	STSIG_OS_CONTROL	0xae
/* rough battery operating state limits */
#define STSEC_BAT_MIN_VOLT	7000000	/* 7V */
#define STSEC_BAT_MAX_VOLT	8000000	/* 8V */

const struct {
	const char *desc;
	enum sensor_type type;
} stsec_sensors_template[] = {
#define	STSEC_SENSOR_AC_PRESENCE	0
	{
		.desc = "AC present",
		.type = SENSOR_INDICATOR,
	},
#define	STSEC_SENSOR_BATTERY_PRESENCE	1
	{
		.desc = "Battery present",
		.type = SENSOR_INDICATOR,
	},
#define	STSEC_SENSOR_BATTERY_STATE	2
	{
		.desc = "Battery charging",
		.type = SENSOR_INDICATOR,
	},
#define	STSEC_SENSOR_BATTERY_VOLTAGE	3
	{
		.desc = "Battery voltage",
		.type = SENSOR_VOLTS_DC,
	}
};

struct stsec_softc {
	struct device		 sc_dev;
	i2c_tag_t		 sc_tag;
	i2c_addr_t		 sc_addr;
	uint			 sc_base;

	struct ksensor		 sc_sensors[nitems(stsec_sensors_template)];
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensors_update_task;
};

int	stsec_match(struct device *, void *, void *);
void	stsec_attach(struct device *, struct device *, void *);

const struct cfattach stsec_ca = {
	sizeof(struct stsec_softc), stsec_match, stsec_attach
};

struct cfdriver stsec_cd = {
	NULL, "stsec", DV_DULL
};

int	stsec_apminfo(struct apm_power_info *);
int	stsec_read(struct stsec_softc *, uint, int *);
int	stsec_write(struct stsec_softc *, uint, int);
void	stsec_sensors_update(void *);

#if NAPM > 0
struct apm_power_info stsec_apmdata;
const char *stsec_batstate[] = {
	"high",
	"low",
	"critical",
	"charging",
	"unknown"
};
#define BATTERY_STRING(x) ((x) < nitems(stsec_batstate) ? \
	stsec_batstate[x] : stsec_batstate[4])
#endif

int
stsec_match(struct device *parent, void *vcf, void *aux)
{
	struct i2c_attach_args *ia = (struct i2c_attach_args *)aux;
	struct cfdata *cf = (struct cfdata *)vcf;

	return strcmp(ia->ia_name, cf->cf_driver->cd_name) == 0;
}

void
stsec_attach(struct device *parent, struct device *self, void *aux)
{
	struct stsec_softc *sc = (struct stsec_softc *)self;
	struct i2c_attach_args *ia = (struct i2c_attach_args *)aux;
	int rev, sig;
	int rc;
	uint i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	/*
	 * Figure out where to get our information, and display microcode
	 * version for geek value if available.
	 */

	sc->sc_base = 0;
	switch (gdium_revision) {
	case 0:
		break;
	default:
		/* read version before sc_base is set */
		rc = stsec_read(sc, ST7_VERSION, &rev);
		if (rc != 0) {
			printf(": can't read microcode revision\n");
			return;
		}
		printf(": revision %d.%d", (rev >> 4) & 0x0f, rev & 0x0f);
		sc->sc_base = ST7_VERSION + 1;
		break;
	}

	printf("\n");

	/*
	 * Better trust the ST7 firmware to control charge operation.
	 */

	rc = stsec_read(sc, ST7_SIGNATURE, &sig);
	if (rc != 0) {
		printf("%s: can't verify charge policy\n", self->dv_xname);
		/* not fatal */
	} else {
		if (sig != STSIG_EC_CONTROL)
			stsec_write(sc, ST7_SIGNATURE, STSIG_EC_CONTROL);
	}

	/*
	 * Setup sensors. We use a quite short refresh interval to react
	 * quickly enough to button presses.
	 * XXX but we don't do anything on lid or button presses... yet
	 */

	strlcpy(sc->sc_sensordev.xname, self->dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensors_update_task =
	    sensor_task_register(sc, stsec_sensors_update, 2);
	if (sc->sc_sensors_update_task == NULL) {
		printf("%s: can't initialize refresh task\n", self->dv_xname);
		return;
	}

	for (i = 0; i < nitems(sc->sc_sensors); i++) {
		sc->sc_sensors[i].type = stsec_sensors_template[i].type;
		strlcpy(sc->sc_sensors[i].desc, stsec_sensors_template[i].desc,
		    sizeof(sc->sc_sensors[i].desc));
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}
	sensordev_install(&sc->sc_sensordev);
#if NAPM > 0
	/* make sure we have the apm state initialized before apm attaches */
	stsec_sensors_update(sc);
	apm_setinfohook(stsec_apminfo);
#endif
}

int
stsec_read(struct stsec_softc *sc, uint reg, int *value)
{
	uint8_t regno, data;
	int rc;

	regno = sc->sc_base + reg;
	iic_acquire_bus(sc->sc_tag, 0);
	rc = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &regno, sizeof regno, &data, sizeof data, 0);
	iic_release_bus(sc->sc_tag, 0);

	if (rc == 0)
		*value = (int)data;
	return rc;
}

int
stsec_write(struct stsec_softc *sc, uint reg, int val)
{
	uint8_t regno, data[1];
	int rc;

	regno = sc->sc_base + reg;
	data[0] = (uint8_t)val;
	iic_acquire_bus(sc->sc_tag, 0);
	rc = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &regno, sizeof regno, data, sizeof data[0], 0);
	iic_release_bus(sc->sc_tag, 0);
	return rc;
}

void
stsec_sensors_update(void *vsc)
{
	struct stsec_softc *sc = (struct stsec_softc *)vsc;
	int status, control, batl, bath;
	ulong batuv;
	struct ksensor *ks;
	uint i;
#if NAPM > 0
	struct apm_power_info old;
	uint cap_pct;
#endif

	for (i = 0; i < nitems(sc->sc_sensors); i++)
		sc->sc_sensors[i].flags |= SENSOR_FINVALID;

	if (stsec_read(sc, ST7_STATUS, &status) != 0 ||
	    stsec_read(sc, ST7_CONTROL, &control) != 0 ||
	    stsec_read(sc, ST7_BATTERY_L, &batl) != 0 ||
	    stsec_read(sc, ST7_BATTERY_H, &bath) != 0)
		return;

	/*
	 * Battery voltage is in 10/1024V units, in the 0-1023 range.
	 */
	batuv = ((ulong)STB_VALUE(bath, batl) * 10 * 1000000) / 1024;
	
	ks = &sc->sc_sensors[STSEC_SENSOR_AC_PRESENCE];
	ks->value = !!ISSET(status, STS_AC_AVAILABLE);
	ks->flags &= ~SENSOR_FINVALID;

	/*
	 * Old mobo design does not have a battery presence bit; the Linux
	 * code decides there is no battery if the reported battery voltage
	 * is too low, we'll do the same.
	 */
	ks = &sc->sc_sensors[STSEC_SENSOR_BATTERY_PRESENCE];
	switch (gdium_revision) {
	case 0:
		if (ISSET(status, STS_AC_AVAILABLE))
			ks->value = batuv > 500000;
		else
			ks->value = 1;
		break;
	default:
		ks->value = !!ISSET(status, STS_BATTERY_PRESENT);
		break;
	}
	ks->flags &= ~SENSOR_FINVALID;
	
	ks = &sc->sc_sensors[STSEC_SENSOR_BATTERY_STATE];
	ks->value = !!ISSET(control, STC_CHARGE_ENABLE);
	ks->flags &= ~SENSOR_FINVALID;
	
	ks = &sc->sc_sensors[STSEC_SENSOR_BATTERY_VOLTAGE];
	ks->value = (int64_t)batuv;
	ks->flags &= ~SENSOR_FINVALID;

#if NAPM > 0
	bcopy(&stsec_apmdata, &old, sizeof(old));

	if (batuv < STSEC_BAT_MIN_VOLT)
		batuv = STSEC_BAT_MIN_VOLT;
	else if (batuv > STSEC_BAT_MAX_VOLT)
		batuv = STSEC_BAT_MAX_VOLT;
	cap_pct = (batuv - STSEC_BAT_MIN_VOLT) * 100 / (STSEC_BAT_MAX_VOLT -
	    STSEC_BAT_MIN_VOLT);
	stsec_apmdata.battery_life = cap_pct;

	stsec_apmdata.ac_state = ISSET(status, STS_AC_AVAILABLE) ? APM_AC_ON :
	    APM_AC_OFF;
	if (!sc->sc_sensors[STSEC_SENSOR_BATTERY_PRESENCE].value) {
		stsec_apmdata.battery_state = APM_BATTERY_ABSENT;
		stsec_apmdata.minutes_left = 0;
		stsec_apmdata.battery_life = 0;
	} else {
		if (ISSET(control, STC_CHARGE_ENABLE))
			stsec_apmdata.battery_state = APM_BATT_CHARGING;
		else if (cap_pct > 50)
			stsec_apmdata.battery_state = APM_BATT_HIGH;
		else if (cap_pct > 25)
			stsec_apmdata.battery_state = APM_BATT_LOW;
		else
			stsec_apmdata.battery_state = APM_BATT_CRITICAL;

		stsec_apmdata.minutes_left = -1; /* unknown */
	}
	if (old.ac_state != stsec_apmdata.ac_state) 
		apm_record_event(APM_POWER_CHANGE, "AC power",
			stsec_apmdata.ac_state ? "restored" : "lost");
	if (old.battery_state != stsec_apmdata.battery_state) 
		apm_record_event(APM_POWER_CHANGE, "battery",
		    BATTERY_STRING(stsec_apmdata.battery_state));
#endif
}

#if NAPM > 0
int
stsec_apminfo(struct apm_power_info *info)
{
	 bcopy(&stsec_apmdata, info, sizeof(struct apm_power_info));
	 return 0;
}
#endif
