/*	$OpenBSD: m41t8x.c,v 1.3 2022/10/12 13:39:50 kettenis Exp $	*/

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
 * M41T8x clock connected to an I2C bus
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/clock_subr.h>
#include <dev/i2c/i2cvar.h>
#include <dev/ic/m41t8xreg.h>

struct m41t8xrtc_softc {
	struct device		sc_dev;
	struct todr_chip_handle	sc_todr;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;
};

int	m41t8xrtc_match(struct device *, void *, void *);
void	m41t8xrtc_attach(struct device *, struct device *, void *);

const struct cfattach mfokrtc_ca = {
	sizeof(struct m41t8xrtc_softc),
	m41t8xrtc_match, m41t8xrtc_attach
};

struct cfdriver mfokrtc_cd = {
	NULL, "mfokrtc", DV_DULL
};

int	m41t8xrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	m41t8xrtc_settime(struct todr_chip_handle *, struct timeval *);

int
m41t8xrtc_match(struct device *parent, void *vcf, void *aux)
{
	struct i2c_attach_args *ia = (struct i2c_attach_args *)aux;

	if (strcmp(ia->ia_name, "st,m41t83") == 0 ||
	    strcmp(ia->ia_name, "microcrystal,rv4162") == 0)
		return (1);
	return (0);

}

void
m41t8xrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct m41t8xrtc_softc *sc = (struct m41t8xrtc_softc *)self;
	struct i2c_attach_args *ia = (struct i2c_attach_args *)aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = m41t8xrtc_gettime;
	sc->sc_todr.todr_settime = m41t8xrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
m41t8xrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct m41t8xrtc_softc *sc = handle->cookie;
	uint8_t regno, data[M41T8X_TOD_LENGTH];
	int s;

	iic_acquire_bus(sc->sc_tag, 0);
	s = splclock();
	for (regno = M41T8X_TOD_START;
	    regno < M41T8X_TOD_START + M41T8X_TOD_LENGTH; regno++)
		iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &regno, sizeof regno, data + regno - M41T8X_TOD_START,
		    sizeof data[0], 0);
	splx(s);
	iic_release_bus(sc->sc_tag, 0);

	dt.dt_sec = FROMBCD(data[M41T8X_SEC] & ~M41T8X_STOP);
	dt.dt_min = FROMBCD(data[M41T8X_MIN]);
	dt.dt_hour = FROMBCD(data[M41T8X_HR] & ~(M41T8X_CEB | M41T8X_CB));
	dt.dt_day = FROMBCD(data[M41T8X_DAY]);
	dt.dt_mon = FROMBCD(data[M41T8X_MON]);
	dt.dt_year = FROMBCD(data[M41T8X_YEAR]) + 2000;
	if (data[M41T8X_HR] & M41T8X_CB)
		dt.dt_year += 100;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
m41t8xrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct m41t8xrtc_softc *sc = handle->cookie;
	uint8_t regno, data[M41T8X_TOD_LENGTH];
	int s;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	iic_acquire_bus(sc->sc_tag, 0);
	s = splclock();
	/* read current state */
	for (regno = M41T8X_TOD_START;
	    regno < M41T8X_TOD_START + M41T8X_TOD_LENGTH; regno++)
		iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &regno, sizeof regno, data + regno - M41T8X_TOD_START,
		    sizeof data[0], 0);
	/* compute new state */
	data[M41T8X_HSEC] = 0;
	data[M41T8X_SEC] = TOBCD(dt.dt_sec);
	data[M41T8X_MIN] = TOBCD(dt.dt_min);
	data[M41T8X_HR] &= M41T8X_CEB;
	if (dt.dt_year >= 2100)
		data[M41T8X_HR] |= M41T8X_CB;
	data[M41T8X_HR] |= TOBCD(dt.dt_hour);
	data[M41T8X_DOW] &= ~M41T8X_DOW_MASK;
	data[M41T8X_DOW] |= TOBCD(dt.dt_wday + 1);
	data[M41T8X_DAY] = TOBCD(dt.dt_day);
	data[M41T8X_MON] = TOBCD(dt.dt_mon);
	data[M41T8X_YEAR] = TOBCD(dt.dt_year % 100);
	/* write new state */
	for (regno = M41T8X_TOD_START;
	    regno < M41T8X_TOD_START + M41T8X_TOD_LENGTH; regno++)
		iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		    &regno, sizeof regno, data + regno, sizeof data[0], 0);
	splx(s);
	iic_release_bus(sc->sc_tag, 0);

	return 0;
}
