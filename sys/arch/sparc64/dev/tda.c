/*	$OpenBSD: tda.c,v 1.9 2021/10/24 17:05:04 mpi Exp $ */

/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/i2c/i2cvar.h>

/* fan control registers */
#define TDA_SYSFAN_REG		0xf0
#define TDA_CPUFAN_REG		0xf2
#define TDA_PSFAN_REG		0xf4

#define TDA_FANSPEED_MIN        0x0c
#define TDA_FANSPEED_MAX        0x3f

#define TDA_PSFAN_ON            0x1f
#define TDA_PSFAN_OFF           0x00

/* Internal and External temperature sensor numbers */
#define SENSOR_TEMP_EXT		0
#define SENSOR_TEMP_INT		1

#define CPU_TEMP_MAX		(67 * 1000000 + 273150000)
#define CPU_TEMP_MIN		(57 * 1000000 + 273150000)
#define SYS_TEMP_MAX		(35 * 1000000 + 273150000)
#define SYS_TEMP_MIN		(25 * 1000000 + 273150000)

struct tda_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;

	u_int16_t		sc_cfan_speed;	/* current CPU fan speed */
	u_int16_t		sc_sfan_speed;	/* current SYS fan speed */

	int			sc_nsensors;
};
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

int	tda_match(struct device *, void *, void *);
void	tda_attach(struct device *, struct device *, void *);

void	tda_setspeed(struct tda_softc *);
void	tda_adjust(void *);

const struct cfattach tda_ca = {
	sizeof(struct tda_softc), tda_match, tda_attach
};

struct cfdriver tda_cd = {
	NULL, "tda", DV_DULL
};

void *tda_cookie;

int
tda_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;
	char name[32];

	if (strcmp(ia->ia_name, "tda8444") != 0)
		return (0);

	/* Only attach on the Sun Blade 1000/2000. */
	if (OF_getprop(findroot(), "name", name, sizeof(name)) <= 0)
		return (0);
	if (strcmp(name, "SUNW,Sun-Blade-1000") != 0)
		return (0);

	return (1);
}

void
tda_attach(struct device *parent, struct device *self, void *aux)
{
	struct tda_softc *sc = (struct tda_softc *)self;
	struct i2c_attach_args *ia = aux;
	struct ksensordev *ksens;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf("\n");

	/*
	 * Set the fans to maximum speed and save the power levels;
	 * the controller is write-only.
	 */
	sc->sc_cfan_speed = sc->sc_sfan_speed = TDA_FANSPEED_MAX;
	tda_setspeed(sc);

	/* Get the number of sensor devices. */
	for (i = 0; ; i++) {
		if (sensordev_get(i, &ksens) == ENOENT)
			break;
	}
	sc->sc_nsensors = i;

	if (!sc->sc_nsensors) {
		printf("%s: no temperature sensors found\n", DEVNAME(sc));
		return;
	}

	if (sensor_task_register(sc, tda_adjust, 10) == NULL) {
		printf("%s: unable to register update task\n", DEVNAME(sc));
		return;
	}

	tda_cookie = sc;
}

void
tda_setspeed(struct tda_softc *sc)
{
	u_int8_t cmd[2];

	if (sc->sc_cfan_speed < TDA_FANSPEED_MIN)
		sc->sc_cfan_speed = TDA_FANSPEED_MIN;
	if (sc->sc_sfan_speed < TDA_FANSPEED_MIN)
		sc->sc_sfan_speed = TDA_FANSPEED_MIN;
	if (sc->sc_cfan_speed > TDA_FANSPEED_MAX)
		sc->sc_cfan_speed = TDA_FANSPEED_MAX;
	if (sc->sc_sfan_speed > TDA_FANSPEED_MAX)
		sc->sc_sfan_speed = TDA_FANSPEED_MAX;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd[0] = TDA_CPUFAN_REG;
	cmd[1] = sc->sc_cfan_speed;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof(cmd), NULL, 0, 0)) {
		printf("%s: cannot write cpu-fan register\n",
		    DEVNAME(sc));
		iic_release_bus(sc->sc_tag, 0);
		return;
        }

	cmd[0] = TDA_SYSFAN_REG;
	cmd[1] = sc->sc_sfan_speed;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof(cmd), NULL, 0, 0)) {
		printf("%s: cannot write system-fan register\n",
		    DEVNAME(sc));
		iic_release_bus(sc->sc_tag, 0);
		return;
        }

	iic_release_bus(sc->sc_tag, 0);

#if 0
        printf("%s: changed fan speed to cpu=%d system=%d\n",
               DEVNAME(sc), sc->sc_cfan_speed, sc->sc_sfan_speed); 
#endif

}

void
tda_adjust(void *v)
{
	struct tda_softc *sc = v;
	struct ksensor *ks;
	u_int64_t ctemp, stemp;
	int i, err;

	/* Default to running the fans at maximum speed. */
	sc->sc_cfan_speed = sc->sc_sfan_speed = TDA_FANSPEED_MAX;

	ctemp = stemp = 0;
	for (i = 0; i < sc->sc_nsensors; i++) {
		err = sensor_find(i, SENSOR_TEMP, SENSOR_TEMP_EXT, &ks);
		if (err) {
			printf("%s: failed to get external sensor\n",
			    DEVNAME(sc));
			goto out;
		}
		ctemp = MAX(ctemp, ks->value);

		err = sensor_find(i, SENSOR_TEMP, SENSOR_TEMP_INT, &ks);
		if (err) {
			printf("%s: failed to get internal sensors\n",
			    DEVNAME(sc));
			goto out;
		}
		stemp = MAX(stemp, ks->value);
	}

	if (ctemp < CPU_TEMP_MIN)
		sc->sc_cfan_speed = TDA_FANSPEED_MIN;
	else if (ctemp < CPU_TEMP_MAX)
		sc->sc_cfan_speed = TDA_FANSPEED_MIN +
			(ctemp - CPU_TEMP_MIN) * 
			(TDA_FANSPEED_MAX - TDA_FANSPEED_MIN) / 
			(CPU_TEMP_MAX - CPU_TEMP_MIN);

	if (stemp < SYS_TEMP_MIN)
		sc->sc_sfan_speed = TDA_FANSPEED_MIN;
	else if (stemp < SYS_TEMP_MAX)
		sc->sc_sfan_speed = TDA_FANSPEED_MIN +
			(stemp - SYS_TEMP_MIN) * 
			(TDA_FANSPEED_MAX - TDA_FANSPEED_MIN) / 
			(SYS_TEMP_MAX - SYS_TEMP_MIN);

out:
	tda_setspeed(sc);
}

/* This code gets called when we are about to drop to ddb,
 * in order to operate the fans at full speed during the
 * timeouts are not working.
 */
void
tda_full_blast(void)
{
	struct tda_softc *sc = tda_cookie;
	u_int8_t cmd[2];

	if (sc == NULL)
		return;

	sc->sc_cfan_speed = sc->sc_sfan_speed = TDA_FANSPEED_MAX;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);

	cmd[0] = TDA_CPUFAN_REG;
	cmd[1] = sc->sc_cfan_speed;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof(cmd), NULL, 0, 0)) {
		printf("%s: cannot write cpu-fan register\n",
		    DEVNAME(sc));
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		return;
        }

	cmd[0] = TDA_SYSFAN_REG;
	cmd[1] = sc->sc_sfan_speed;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof(cmd), NULL, 0, 0)) {
		printf("%s: cannot write system-fan register\n",
		    DEVNAME(sc));
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		return;
	}

	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}
