/*	$OpenBSD: mcp794xx.c,v 1.3 2022/10/15 18:22:53 kettenis Exp $	*/
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

#define MCP794XX_SC		0x00
#define  MCP794XX_SC_ST			(1 << 7)
#define MCP794XX_MN		0x01
#define MCP794XX_HR		0x02
#define  MCP794XX_HR_PM			(1 << 5)
#define  MCP794XX_HR_12H		(1 << 6)
#define MCP794XX_DW		0x03
#define  MCP794XX_DW_VBATEN		(1 << 3)
#define  MCP794XX_DW_PWRFAIL		(1 << 4)
#define  MCP794XX_DW_OSCRUN		(1 << 5)
#define MCP794XX_DT		0x04
#define MCP794XX_MO		0x05
#define MCP794XX_YR		0x06
#define MCP794XX_CR		0x07
#define  MCP794XX_CR_EXTOSC		(1 << 3)

#define MCP794XX_NRTC_REGS	7

struct mcprtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	int sc_extosc;
	struct todr_chip_handle sc_todr;
};

int	mcprtc_match(struct device *, void *, void *);
void	mcprtc_attach(struct device *, struct device *, void *);

const struct cfattach mcprtc_ca = {
	sizeof(struct mcprtc_softc), mcprtc_match, mcprtc_attach
};

struct cfdriver mcprtc_cd = {
	NULL, "mcprtc", DV_DULL
};

uint8_t	mcprtc_reg_read(struct mcprtc_softc *, int);
void	mcprtc_reg_write(struct mcprtc_softc *, int, uint8_t);
int	mcprtc_clock_read(struct mcprtc_softc *, struct clock_ymdhms *);
int	mcprtc_clock_write(struct mcprtc_softc *, struct clock_ymdhms *);
int	mcprtc_gettime(struct todr_chip_handle *, struct timeval *);
int	mcprtc_settime(struct todr_chip_handle *, struct timeval *);

int
mcprtc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "microchip,mcp7940x") == 0 ||
	    strcmp(ia->ia_name, "microchip,mcp7941x") == 0)
		return 1;

	return 0;
}

void
mcprtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct mcprtc_softc *sc = (struct mcprtc_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = mcprtc_gettime;
	sc->sc_todr.todr_settime = mcprtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
mcprtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct mcprtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int error;

	error = mcprtc_clock_read(sc, &dt);
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
mcprtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct mcprtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	return mcprtc_clock_write(sc, &dt);
}

uint8_t
mcprtc_reg_read(struct mcprtc_softc *sc, int reg)
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
mcprtc_reg_write(struct mcprtc_softc *sc, int reg, uint8_t val)
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
mcprtc_clock_read(struct mcprtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[MCP794XX_NRTC_REGS];
	uint8_t cmd = MCP794XX_SC;
	int error;

	/* Don't trust the RTC if the oscillator is not running. */
	if (!(mcprtc_reg_read(sc, MCP794XX_DW) & MCP794XX_DW_OSCRUN))
		return EIO;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, MCP794XX_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Convert the MCP794XX's register values into something useable.
	 */
	dt->dt_sec = FROMBCD(regs[0] & 0x7f);
	dt->dt_min = FROMBCD(regs[1] & 0x7f);
	dt->dt_hour = FROMBCD(regs[2] & 0x1f);
	if ((regs[2] & MCP794XX_HR_12H) && (regs[2] & MCP794XX_HR_PM))
		dt->dt_hour += 12;
	dt->dt_wday = FROMBCD(regs[3] & 0x7);
	dt->dt_day = FROMBCD(regs[4] & 0x3f);
	dt->dt_mon = FROMBCD(regs[5] & 0x1f);
	dt->dt_year = FROMBCD(regs[6]) + 2000;

	return 0;
}

int
mcprtc_clock_write(struct mcprtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[MCP794XX_NRTC_REGS];
	uint8_t cmd = MCP794XX_SC;
	uint8_t oscoff, oscbit;
	uint8_t reg;
	int error, i;

	/*
	 * Convert our time representation into something the MCP794XX
	 * can understand.
	 */
	regs[0] = TOBCD(dt->dt_sec);
	regs[1] = TOBCD(dt->dt_min);
	regs[2] = TOBCD(dt->dt_hour);
	regs[3] = TOBCD(dt->dt_wday) | MCP794XX_DW_VBATEN;
	regs[4] = TOBCD(dt->dt_day);
	regs[5] = TOBCD(dt->dt_mon);
	regs[6] = TOBCD(dt->dt_year - 2000);

	/* Stop RTC. */
	if (sc->sc_extosc) {
		oscoff = MCP794XX_CR;
		oscbit = MCP794XX_CR_EXTOSC;
	} else {
		oscoff = MCP794XX_SC;
		oscbit = MCP794XX_SC_ST;
	}

	/* Stop RTC such that we can write to it. */
	reg = mcprtc_reg_read(sc, oscoff);
	reg &= ~oscbit;
	mcprtc_reg_write(sc, oscoff, reg);

	for (i = 0; i < 10; i++) {
		reg = mcprtc_reg_read(sc, MCP794XX_DW);
		if ((reg & MCP794XX_DW_OSCRUN) == 0)
			break;
		delay(10);
	}
	if (i == 10) {
		error = EIO;
		goto fail;
	}

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), regs, MCP794XX_NRTC_REGS, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/* Restart RTC. */
	reg = mcprtc_reg_read(sc, oscoff);
	reg |= oscbit;
	mcprtc_reg_write(sc, oscoff, reg);

fail:
	if (error) {
		printf("%s: can't write RTC\n", sc->sc_dev.dv_xname);
		return error;
	}

	return 0;
}
