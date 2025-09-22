/*	$OpenBSD: ds1307.c,v 1.8 2022/10/20 10:35:35 mglocker Exp $ */

/*
 * Copyright (c) 2016 Marcus Glocker <mglocker@openbsd.org>
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

#include <dev/clock_subr.h>
#include <dev/i2c/i2cvar.h>

/*
 * Defines.
 */
/* RTC Registers */
#define DS1307_SEC_REG		0x00
#define DS1307_SEC_MASK		0x7f
#define DS1307_SEC_MASK_CH	0x80	/* Clock Halt bit */
#define DS1307_SEC_BIT_CH	7	/* 0 = osc enabled, 1 = osc disabled */
#define DS1307_MIN_REG		0x01
#define DS1307_MIN_MASK		0x7f
#define DS1307_HOUR_REG		0x02
#define DS1307_HOUR_MASK	0x3f
#define DS1307_HOUR_MASK_MODE	0x40	/* Hour Mode bit */
#define DS1307_HOUR_BIT_MODE	6	/* 0 = 24h mode, 1 = 12h mode */
#define DS1307_WDAY_REG		0x03
#define DS1307_WDAY_MASK	0x07
#define DS1307_DATE_REG		0x04
#define DS1307_DATE_MASK	0x3f
#define DS1307_MONTH_REG	0x05
#define DS1307_MONTH_MASK	0x1f
#define DS1307_YEAR_REG		0x06
#define DS1307_YEAR_MASK	0xff
#define DS1307_CTRL_REG		0x07

/* RAM Registers */
#define DS1307_RAM_REG		0x08	/* RAM address space 0x08 - 0x3f */

/*
 * Driver structure.
 */
struct maxrtc_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	int			sc_addr;
	struct todr_chip_handle sc_todr;
};

/*
 * Prototypes.
 */
int	maxrtc_match(struct device *, void *, void *);
void	maxrtc_attach(struct device *, struct device *, void *);
int	maxrtc_read(struct maxrtc_softc *, uint8_t *, uint8_t,
	    uint8_t *, uint8_t);
int	maxrtc_write(struct maxrtc_softc *, uint8_t *, uint8_t);
int	maxrtc_enable_osc(struct maxrtc_softc *);
int	maxrtc_set_24h_mode(struct maxrtc_softc *);
int	maxrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	maxrtc_settime(struct todr_chip_handle *, struct timeval *);

/*
 * Driver glue structures.
 */
const struct cfattach maxrtc_ca = {
	sizeof(struct maxrtc_softc), maxrtc_match, maxrtc_attach
};

struct cfdriver maxrtc_cd = {
	NULL, "maxrtc", DV_DULL
};

/*
 * Functions.
 */
int
maxrtc_match(struct device *parent, void *v, void *arg)
{
	struct i2c_attach_args *ia = arg;

	if (strcmp(ia->ia_name, "dallas,ds1307") == 0 ||
	    strcmp(ia->ia_name, "ds1307") == 0 ||
	    strcmp(ia->ia_name, "dallas,ds1339") == 0)
		return (1);

	return (0);
}

void
maxrtc_attach(struct device *parent, struct device *self, void *arg)
{
	struct maxrtc_softc *sc = (struct maxrtc_softc *)self;
	struct i2c_attach_args *ia = arg;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	if (maxrtc_enable_osc(sc) == -1)
		return;

	if (maxrtc_set_24h_mode(sc) == -1)
		return;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = maxrtc_gettime;
	sc->sc_todr.todr_settime = maxrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);
}

int
maxrtc_read(struct maxrtc_softc *sc, uint8_t *cmd, uint8_t cmd_len,
    uint8_t *data, uint8_t data_len)
{
	int r;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if ((r = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    cmd, cmd_len, data, data_len, I2C_F_POLL)))
		printf("%s: maxrtc_read failed\n", sc->sc_dev.dv_xname);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	return (r);
}

int
maxrtc_write(struct maxrtc_softc *sc, uint8_t *data, uint8_t data_len)
{
	int r;

	/*
	 * On write operation the DS1307 requires the target address to be
	 * stored in the first byte of the data packet.  Therefore we don't
	 * fill up the command packet here.
	 */
	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if ((r = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    NULL, 0, data, data_len, I2C_F_POLL)))
		printf("%s: maxrtc_write failed\n", sc->sc_dev.dv_xname);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	return (r);
}

int
maxrtc_enable_osc(struct maxrtc_softc *sc)
{
	uint8_t cmd;
	uint8_t data_r;
	uint8_t data_w[2];

	cmd = DS1307_SEC_REG;
	data_r = 0;
	if (maxrtc_read(sc, &cmd, sizeof(cmd), &data_r, sizeof(data_r))) {
		printf("%s: maxrtc_enable_osc failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}
	if ((data_r & DS1307_SEC_MASK_CH) == 0) {
		/* oscillator is already enabled */
		printf(": rtc is ok\n");
		return (0);
	}
	printf(": rtc was halted, check battery\n");

	/* enable the oscillator */
	data_r |= 0 << DS1307_SEC_BIT_CH;
	data_w[0] = DS1307_SEC_REG;
	data_w[1] = data_r;
	if (maxrtc_write(sc, data_w, sizeof(data_w))) {
		printf("%s: maxrtc_enable_osc failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	return (0);
}

int
maxrtc_set_24h_mode(struct maxrtc_softc *sc)
{
	uint8_t cmd;
	uint8_t data_r;
	uint8_t data_w[2];

	cmd = DS1307_HOUR_REG;
	data_r = 0;
	if (maxrtc_read(sc, &cmd, sizeof(cmd), &data_r, sizeof(data_r))) {
		printf("%s: maxrtc_set_24h_mode failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}
	if ((data_r & DS1307_HOUR_MASK_MODE) == 0) {
		/* 24h mode is already set */
		return (0);
	}

	/* set 24h mode */
	data_r |= 0 << DS1307_HOUR_BIT_MODE;
	data_w[0] = DS1307_HOUR_REG;
	data_w[1] = data_r;
	if (maxrtc_write(sc, data_w, sizeof(data_w))) {
		printf("%s: maxrtc_set_24h_mode failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	return (0);
}

int
maxrtc_gettime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct maxrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;
	uint8_t cmd;
	uint8_t data[7];

	cmd = DS1307_SEC_REG;
	memset(data, 0, sizeof(data));
	if (maxrtc_read(sc, &cmd, sizeof(cmd), data, sizeof(data))) {
		printf("%s: maxrtc_gettime failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	dt.dt_sec = FROMBCD(data[DS1307_SEC_REG] & DS1307_SEC_MASK);
	dt.dt_min = FROMBCD(data[DS1307_MIN_REG] & DS1307_MIN_MASK);
	dt.dt_hour = FROMBCD(data[DS1307_HOUR_REG] & DS1307_HOUR_MASK);
	dt.dt_wday = FROMBCD(data[DS1307_WDAY_REG] & DS1307_WDAY_MASK);
	dt.dt_day = FROMBCD(data[DS1307_DATE_REG] & DS1307_DATE_MASK);
	dt.dt_mon = FROMBCD(data[DS1307_MONTH_REG] & DS1307_MONTH_MASK);
	dt.dt_year = FROMBCD(data[DS1307_YEAR_REG] & DS1307_YEAR_MASK) + 2000;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;

	return (0);
}

int
maxrtc_settime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct maxrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;
	uint8_t data[8];

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	data[0] = DS1307_SEC_REG;
	data[1] = TOBCD(dt.dt_sec);	/* this will also enable the osc */
	data[2] = TOBCD(dt.dt_min);
	data[3] = TOBCD(dt.dt_hour);	/* this will also set 24h mode */
	data[4] = TOBCD(dt.dt_wday);
	data[5] = TOBCD(dt.dt_day);
	data[6] = TOBCD(dt.dt_mon);
	data[7] = TOBCD(dt.dt_year - 2000);
	if (maxrtc_write(sc, data, sizeof(data))) {
		printf("%s: maxrtc_settime failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	return (0);
}
