/*	$OpenBSD: isl1208.c,v 1.5 2022/10/15 18:22:53 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

#define ISL1208_SC		0x00
#define ISL1208_MN		0x01
#define ISL1208_HR		0x02
#define  ISL1208_HR_HR21	(1 << 5)
#define  ISL1208_HR_MIL		(1 << 7)
#define ISL1208_DT		0x03
#define ISL1208_MO		0x04
#define ISL1208_YR		0x05
#define ISL1208_DW		0x06
#define ISL1208_SR		0x07
#define  ISL1208_SR_RTCF	(1 << 0)
#define  ISL1208_SR_WRTC	(1 << 4)

#define ISL1208_NRTC_REGS	7

struct islrtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	struct todr_chip_handle sc_todr;
};

int	islrtc_match(struct device *, void *, void *);
void	islrtc_attach(struct device *, struct device *, void *);

const struct cfattach islrtc_ca = {
	sizeof(struct islrtc_softc), islrtc_match, islrtc_attach
};

struct cfdriver islrtc_cd = {
	NULL, "islrtc", DV_DULL
};

uint8_t	islrtc_reg_read(struct islrtc_softc *, int);
void	islrtc_reg_write(struct islrtc_softc *, int, uint8_t);
int	islrtc_clock_read(struct islrtc_softc *, struct clock_ymdhms *);
int	islrtc_clock_write(struct islrtc_softc *, struct clock_ymdhms *);
int	islrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	islrtc_settime(struct todr_chip_handle *, struct timeval *);

int
islrtc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "isil,isl1208") == 0 ||
	    strcmp(ia->ia_name, "isil,isl1218") == 0)
		return 1;

	return 0;
}

void
islrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct islrtc_softc *sc = (struct islrtc_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = islrtc_gettime;
	sc->sc_todr.todr_settime = islrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
islrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct islrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int error;

	error = islrtc_clock_read(sc, &dt);
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
islrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct islrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	return islrtc_clock_write(sc, &dt);
}

uint8_t
islrtc_reg_read(struct islrtc_softc *sc, int reg)
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
islrtc_reg_write(struct islrtc_softc *sc, int reg, uint8_t val)
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
islrtc_clock_read(struct islrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[ISL1208_NRTC_REGS];
	uint8_t cmd = ISL1208_SC;
	uint8_t status;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, ISL1208_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Convert the ISL1208's register values into something useable.
	 */
	dt->dt_sec = FROMBCD(regs[0]);
	dt->dt_min = FROMBCD(regs[1]);
	if (regs[2] & ISL1208_HR_MIL) {
		dt->dt_hour = FROMBCD(regs[2] & ~ISL1208_HR_MIL);
	} else {
		dt->dt_hour = FROMBCD(regs[2] & ~ISL1208_HR_HR21);
		if (regs[2] & ISL1208_HR_HR21)
			dt->dt_hour += 12;
	}
	dt->dt_day = FROMBCD(regs[3]);
	dt->dt_mon = FROMBCD(regs[4]);
	dt->dt_year = FROMBCD(regs[5]) + 2000;

	/* Consider the time to be invalid if we lost power. */
	status = islrtc_reg_read(sc, ISL1208_SR);
	if (status & ISL1208_SR_RTCF)
		return EINVAL;

	return 0;
}

int
islrtc_clock_write(struct islrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[ISL1208_NRTC_REGS];
	uint8_t cmd = ISL1208_SC;
	uint8_t reg;
	int error;

	/*
	 * Convert our time representation into something the ISL1208
	 * can understand.
	 */
	regs[0] = TOBCD(dt->dt_sec);
	regs[1] = TOBCD(dt->dt_min);
	regs[2] = TOBCD(dt->dt_hour) | ISL1208_HR_MIL;
	regs[3] = TOBCD(dt->dt_day);
	regs[4] = TOBCD(dt->dt_mon);
	regs[5] = TOBCD(dt->dt_year - 2000);
	regs[6] = TOBCD(dt->dt_wday);

	/* Stop RTC such that we can write to it. */
	reg = islrtc_reg_read(sc, ISL1208_SR);
	if (reg == 0xff) {
		error = EIO;
		goto fail;
	}
	islrtc_reg_write(sc, ISL1208_SR, reg | ISL1208_SR_WRTC);

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, ISL1208_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/* Restart RTC. */
	islrtc_reg_write(sc, ISL1208_SR, reg & ~ISL1208_SR_WRTC);

fail:
	if (error) {
		printf("%s: can't write RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	return 0;
}
