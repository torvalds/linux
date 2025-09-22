/*	$OpenBSD: abx80x.c,v 1.8 2022/10/15 18:22:53 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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

#if defined(__HAVE_FDT)
#include <dev/ofw/openfirm.h>
#endif

#define ABX8XX_HTH		0x00
#define ABX8XX_SC		0x01
#define ABX8XX_MN		0x02
#define ABX8XX_HR		0x03
#define ABX8XX_DA		0x04
#define ABX8XX_MO		0x05
#define ABX8XX_YR		0x06
#define ABX8XX_WD		0x07
#define ABX8XX_CTRL		0x10
#define  ABX8XX_CTRL_WRITE		(1 << 0)
#define  ABX8XX_CTRL_ARST		(1 << 2)
#define  ABX8XX_CTRL_12_24		(1 << 6)
#define ABX8XX_CD_TIMER_CTL	0x18
#define  ABX8XX_CD_TIMER_CTL_EN		(1 << 2)
#define ABX8XX_OSS		0x1d
#define ABX8XX_OSS_OF			(1 << 1)
#define ABX8XX_OSS_OMODE		(1 << 4)
#define ABX8XX_CFG_KEY		0x1f
#define  ABX8XX_CFG_KEY_OSC		0xa1
#define  ABX8XX_CFG_KEY_MISC		0x9d
#define ABX8XX_TRICKLE		0x20
#define  ABX8XX_TRICKLE_RESISTOR_0	(0 << 0)
#define  ABX8XX_TRICKLE_RESISTOR_3	(1 << 0)
#define  ABX8XX_TRICKLE_RESISTOR_6	(2 << 0)
#define  ABX8XX_TRICKLE_RESISTOR_11	(3 << 0)
#define  ABX8XX_TRICKLE_DIODE_SCHOTTKY	(1 << 2)
#define  ABX8XX_TRICKLE_DIODE_STANDARD	(2 << 2)
#define  ABX8XX_TRICKLE_ENABLE		(0xa << 4)

#define ABX8XX_NRTC_REGS	8

struct abcrtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	struct todr_chip_handle sc_todr;
};

int	abcrtc_match(struct device *, void *, void *);
void	abcrtc_attach(struct device *, struct device *, void *);

const struct cfattach abcrtc_ca = {
	sizeof(struct abcrtc_softc), abcrtc_match, abcrtc_attach
};

struct cfdriver abcrtc_cd = {
	NULL, "abcrtc", DV_DULL
};

uint8_t	abcrtc_reg_read(struct abcrtc_softc *, int);
void	abcrtc_reg_write(struct abcrtc_softc *, int, uint8_t);
int	abcrtc_clock_read(struct abcrtc_softc *, struct clock_ymdhms *);
int	abcrtc_clock_write(struct abcrtc_softc *, struct clock_ymdhms *);
int	abcrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	abcrtc_settime(struct todr_chip_handle *, struct timeval *);

#if defined(__HAVE_FDT)
void	abcrtc_trickle_charger(struct abcrtc_softc *, int);
#endif

int
abcrtc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "abracon,ab1805") == 0)
		return 1;

	return 0;
}

void
abcrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct abcrtc_softc *sc = (struct abcrtc_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t reg;
#if defined(__HAVE_FDT)
	int node = *(int *)ia->ia_cookie;
#endif

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	reg = abcrtc_reg_read(sc, ABX8XX_CTRL);
	reg &= ~(ABX8XX_CTRL_ARST | ABX8XX_CTRL_12_24);
	reg |= ABX8XX_CTRL_WRITE;
	abcrtc_reg_write(sc, ABX8XX_CTRL, reg);

#if defined(__HAVE_FDT)
	abcrtc_trickle_charger(sc, node);
#endif

	abcrtc_reg_write(sc, ABX8XX_CD_TIMER_CTL, ABX8XX_CD_TIMER_CTL_EN);

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = abcrtc_gettime;
	sc->sc_todr.todr_settime = abcrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
abcrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct abcrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int error;

	error = abcrtc_clock_read(sc, &dt);
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
abcrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct abcrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	return abcrtc_clock_write(sc, &dt);
}

uint8_t
abcrtc_reg_read(struct abcrtc_softc *sc, int reg)
{
	uint8_t cmd = reg;
	uint8_t val[2];
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
		return 0xff;
	}

	return val[1];
}

void
abcrtc_reg_write(struct abcrtc_softc *sc, int reg, uint8_t data)
{
	uint8_t cmd = reg;
	uint8_t val[2];
	int error;

	val[0] = 1;
	val[1] = data;

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
abcrtc_clock_read(struct abcrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[ABX8XX_NRTC_REGS];
	uint8_t cmd = ABX8XX_HTH;
	int error;

	/* Don't trust the RTC if the oscillator failure is set. */
	if ((abcrtc_reg_read(sc, ABX8XX_OSS) & ABX8XX_OSS_OMODE) == 0 &&
	    (abcrtc_reg_read(sc, ABX8XX_OSS) & ABX8XX_OSS_OF) != 0)
		return EIO;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, sizeof(regs), I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Convert the ABX8XX's register values into something useable.
	 */
	dt->dt_sec = FROMBCD(regs[1] & 0x7f);
	dt->dt_min = FROMBCD(regs[2] & 0x7f);
	dt->dt_hour = FROMBCD(regs[3] & 0x3f);
	dt->dt_day = FROMBCD(regs[4] & 0x3f);
	dt->dt_mon = FROMBCD(regs[5] & 0x1f);
	dt->dt_year = FROMBCD(regs[6]) + 2000;
	dt->dt_wday = regs[7] & 0x7;

	return 0;
}

int
abcrtc_clock_write(struct abcrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[ABX8XX_NRTC_REGS];
	uint8_t cmd = ABX8XX_HTH;
	uint8_t reg;
	int error;

	/*
	 * Convert our time representation into something the ABX8XX
	 * can understand.
	 */
	regs[0] = 0;
	regs[1] = TOBCD(dt->dt_sec);
	regs[2] = TOBCD(dt->dt_min);
	regs[3] = TOBCD(dt->dt_hour);
	regs[4] = TOBCD(dt->dt_day);
	regs[5] = TOBCD(dt->dt_mon);
	regs[6] = TOBCD(dt->dt_year - 2000);
	regs[7] = dt->dt_wday;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, sizeof(regs), I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't write RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Clear oscillator failure. */
	reg = abcrtc_reg_read(sc, ABX8XX_OSS);
	reg &= ~ABX8XX_OSS_OF;
	abcrtc_reg_write(sc, ABX8XX_OSS, reg);

	return 0;
}

#if defined(__HAVE_FDT)
void
abcrtc_trickle_charger(struct abcrtc_softc *sc, int node)
{
	char diode[16] = { 0 };
	uint8_t reg = ABX8XX_TRICKLE_ENABLE;

	OF_getprop(node, "abracon,tc-diode", diode, sizeof(diode));
	if (!strcmp(diode, "standard"))
		reg |= ABX8XX_TRICKLE_DIODE_STANDARD;
	else if (!strcmp(diode, "schottky"))
		reg |= ABX8XX_TRICKLE_DIODE_SCHOTTKY;
	else
		return;

	switch (OF_getpropint(node, "abracon,tc-resistor", -1)) {
	case 0:
		reg |= ABX8XX_TRICKLE_RESISTOR_0;
		break;
	case 3:
		reg |= ABX8XX_TRICKLE_RESISTOR_3;
		break;
	case 6:
		reg |= ABX8XX_TRICKLE_RESISTOR_6;
		break;
	case 11:
		reg |= ABX8XX_TRICKLE_RESISTOR_11;
		break;
	default:
		return;
	}

	abcrtc_reg_write(sc, ABX8XX_CFG_KEY, ABX8XX_CFG_KEY_MISC);
	abcrtc_reg_write(sc, ABX8XX_TRICKLE, reg);
}
#endif
