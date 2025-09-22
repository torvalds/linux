/*	$OpenBSD: ds3231.c,v 1.3 2022/10/15 18:22:53 kettenis Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/i2c/i2cvar.h>

#include <dev/clock_subr.h>

#define DS3231_SC		0x00
#define DS3231_MN		0x01
#define DS3231_HR		0x02
#define  DS3231_HR_PM		(1 << 5)
#define  DS3231_HR_12		(1 << 6)
#define  DS3231_HR_12_MASK	~(DS3231_HR_12 | DS3231_HR_PM)
#define DS3231_DW		0x03
#define DS3231_DT		0x04
#define DS3231_MO		0x05
#define  DS3231_MO_MASK		0x3f
#define DS3231_YR		0x06
#define DS3231_SR		0x07
#define  DS3231_SR_OSF		(1 << 7)

#define DS3231_NRTC_REGS	7

struct dsxrtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	struct todr_chip_handle sc_todr;
};

int	dsxrtc_match(struct device *, void *, void *);
void	dsxrtc_attach(struct device *, struct device *, void *);

const struct cfattach dsxrtc_ca = {
	sizeof(struct dsxrtc_softc), dsxrtc_match, dsxrtc_attach
};

struct cfdriver dsxrtc_cd = {
	NULL, "dsxrtc", DV_DULL
};

uint8_t	dsxrtc_reg_read(struct dsxrtc_softc *, int);
void	dsxrtc_reg_write(struct dsxrtc_softc *, int, uint8_t);
int	dsxrtc_clock_read(struct dsxrtc_softc *, struct clock_ymdhms *);
int	dsxrtc_clock_write(struct dsxrtc_softc *, struct clock_ymdhms *);
int	dsxrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	dsxrtc_settime(struct todr_chip_handle *, struct timeval *);

int
dsxrtc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "maxim,ds3231") == 0 ||
	    strcmp(ia->ia_name, "maxim,ds3232") == 0)
		return 1;

	return 0;
}

void
dsxrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct dsxrtc_softc *sc = (struct dsxrtc_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = dsxrtc_gettime;
	sc->sc_todr.todr_settime = dsxrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
dsxrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct dsxrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int error;

	error = dsxrtc_clock_read(sc, &dt);
	if (error)
		return error;

	if (dt.dt_sec > 59 || dt.dt_min > 59 || dt.dt_hour > 23 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0 ||
	    dt.dt_year < POSIX_BASE_YEAR)
		return EINVAL;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
dsxrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct dsxrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	return dsxrtc_clock_write(sc, &dt);

}

uint8_t
dsxrtc_reg_read(struct dsxrtc_softc *sc, int reg)
{
	uint8_t cmd = reg;
	uint8_t val;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
		val = 0xff;
	}

	return val;
}

void
dsxrtc_reg_write(struct dsxrtc_softc *sc, int reg, uint8_t val)
{
	uint8_t cmd = reg;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't write register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
	}
}

int
dsxrtc_clock_read(struct dsxrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[DS3231_NRTC_REGS];
	uint8_t cmd = DS3231_SC;
	uint8_t status;
	int error;

	/* Consider the time to be invalid if the OSF bit is set. */
	status = dsxrtc_reg_read(sc, DS3231_SR);
	if (status & DS3231_SR_OSF)
		return EINVAL;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, DS3231_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Convert the DS3231's register values into something useable.
	 */
	dt->dt_sec = FROMBCD(regs[0]);
	dt->dt_min = FROMBCD(regs[1]);
	if (regs[2] & DS3231_HR_12) {
		dt->dt_hour = FROMBCD(regs[2] & DS3231_HR_12_MASK);
		if (regs[2] & DS3231_HR_PM)
			dt->dt_hour += 12;
	} else {
		dt->dt_hour = FROMBCD(regs[2]);
	}
	dt->dt_day = FROMBCD(regs[4]);
	dt->dt_mon = FROMBCD(regs[5] & DS3231_MO_MASK);
	dt->dt_year = FROMBCD(regs[6]) + 2000;

	return 0;
}

int
dsxrtc_clock_write(struct dsxrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[DS3231_NRTC_REGS];
	uint8_t cmd = DS3231_SC;
	uint8_t status;
	int error;

	/*
	 * Convert our time representation into something the DS3231
	 * can understand.
	 */
	regs[0] = TOBCD(dt->dt_sec);
	regs[1] = TOBCD(dt->dt_min);
	regs[2] = TOBCD(dt->dt_hour);
	regs[3] = TOBCD(dt->dt_wday + 1);
	regs[4] = TOBCD(dt->dt_day);
	regs[5] = TOBCD(dt->dt_mon);
	regs[6] = TOBCD(dt->dt_year - 2000);

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, DS3231_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't write RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Clear OSF flag.  */
	status = dsxrtc_reg_read(sc, DS3231_SR);
	dsxrtc_reg_write(sc, DS3231_SR, status & ~DS3231_SR_OSF);

	return 0;
}
