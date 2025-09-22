/*	$OpenBSD: pcf8563.c,v 1.7 2022/10/12 13:39:50 kettenis Exp $	*/

/*
 * Copyright (c) 2005 Kimihiro Nonaka
 * Copyright (c) 2016, 2017 Mark Kettenis
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
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
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

#include <dev/clock_subr.h>

#include <dev/i2c/i2cvar.h>

/*
 * PCF8563 Real-Time Clock
 */

#define PCF8563_CONTROL1	0x00
#define PCF8563_CONTROL2	0x01
#define PCF8563_SECONDS		0x02
#define PCF8563_MINUTES		0x03
#define PCF8563_HOURS		0x04
#define PCF8563_DAY		0x05
#define PCF8563_WDAY		0x06
#define PCF8563_MONTH		0x07
#define PCF8563_YEAR		0x08

#define PCF8563_NREGS		12
#define PCF8563_NRTC_REGS	7

/*
 * Bit definitions.
 */
#define PCF8563_CONTROL1_TESTC	(1 << 3)
#define PCF8563_CONTROL1_STOP	(1 << 5)
#define PCF8563_CONTROL1_TEST1	(1 << 1)
#define PCF8563_SECONDS_MASK	0x7f
#define PCF8563_SECONDS_VL	(1 << 7)
#define PCF8563_MINUTES_MASK	0x7f
#define PCF8563_HOURS_MASK	0x3f
#define PCF8563_DAY_MASK	0x3f
#define PCF8563_WDAY_MASK	0x07
#define PCF8563_MONTH_MASK	0x1f
#define PCF8563_MONTH_C		(1 << 7)

struct pcxrtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	int sc_address;
	struct todr_chip_handle sc_todr;
};

int pcxrtc_match(struct device *, void *, void *);
void pcxrtc_attach(struct device *, struct device *, void *);

const struct cfattach pcxrtc_ca = {
	sizeof(struct pcxrtc_softc), pcxrtc_match, pcxrtc_attach
};

struct cfdriver pcxrtc_cd = {
	NULL, "pcxrtc", DV_DULL
};

uint8_t pcxrtc_reg_read(struct pcxrtc_softc *, int);
void pcxrtc_reg_write(struct pcxrtc_softc *, int, uint8_t);
int pcxrtc_clock_read(struct pcxrtc_softc *, struct clock_ymdhms *);
int pcxrtc_clock_write(struct pcxrtc_softc *, struct clock_ymdhms *);
int pcxrtc_gettime(struct todr_chip_handle *, struct timeval *);
int pcxrtc_settime(struct todr_chip_handle *, struct timeval *);

int
pcxrtc_match(struct device *parent, void *v, void *arg)
{
	struct i2c_attach_args *ia = arg;

	if (strcmp(ia->ia_name, "nxp,pcf8563") == 0 ||
	    strcmp(ia->ia_name, "haoyu,hym8563") == 0)
		return (1);

	return (0);
}

void
pcxrtc_attach(struct device *parent, struct device *self, void *arg)
{
	struct pcxrtc_softc *sc = (struct pcxrtc_softc *)self;
	struct i2c_attach_args *ia = arg;
	uint8_t reg;

	sc->sc_tag = ia->ia_tag;
	sc->sc_address = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = pcxrtc_gettime;
	sc->sc_todr.todr_settime = pcxrtc_settime;
	sc->sc_todr.todr_setwen = NULL;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	/* Enable. */
	reg = pcxrtc_reg_read(sc, PCF8563_CONTROL1);
	reg &= ~PCF8563_CONTROL1_STOP;
	pcxrtc_reg_write(sc, PCF8563_CONTROL1, reg);

	/* Report battery status. */
	reg = pcxrtc_reg_read(sc, PCF8563_SECONDS);
	printf(": battery %s\n", (reg & PCF8563_SECONDS_VL) ? "low" : "ok");
}

int
pcxrtc_gettime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct pcxrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;

	memset(&dt, 0, sizeof(dt));
	if (pcxrtc_clock_read(sc, &dt) == 0)
		return (-1);

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return (0);
}

int
pcxrtc_settime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct pcxrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	if (pcxrtc_clock_write(sc, &dt) == 0)
		return (-1);
	return (0);
}

uint8_t
pcxrtc_reg_read(struct pcxrtc_softc *sc, int reg)
{
	uint8_t cmd = reg;
	uint8_t val;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_address,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: pcxrtc_reg_read: failed to read reg%d\n",
		    sc->sc_dev.dv_xname, reg);
		return 0;
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
	return val;
}

void
pcxrtc_reg_write(struct pcxrtc_softc *sc, int reg, uint8_t val)
{
	uint8_t cmd = reg;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_address,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: pcxrtc_reg_write: failed to write reg%d\n",
		    sc->sc_dev.dv_xname, reg);
		return;
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}

int
pcxrtc_clock_read(struct pcxrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[PCF8563_NRTC_REGS];
	uint8_t cmd = PCF8563_SECONDS;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_address,
	    &cmd, sizeof(cmd), regs, PCF8563_NRTC_REGS, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: pcxrtc_clock_read: failed to read rtc\n",
		    sc->sc_dev.dv_xname);
		return (0);
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/*
	 * Convert the PCF8563's register values into something useable
	 */
	dt->dt_sec = FROMBCD(regs[0] & PCF8563_SECONDS_MASK);
	dt->dt_min = FROMBCD(regs[1] & PCF8563_MINUTES_MASK);
	dt->dt_hour = FROMBCD(regs[2] & PCF8563_HOURS_MASK);
	dt->dt_day = FROMBCD(regs[3] & PCF8563_DAY_MASK);
	dt->dt_mon = FROMBCD(regs[5] & PCF8563_MONTH_MASK);
	dt->dt_year = FROMBCD(regs[6]) + 2000;

	if ((regs[0] & PCF8563_SECONDS_VL))
		return (0);

	return (1);
}

int
pcxrtc_clock_write(struct pcxrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t regs[PCF8563_NRTC_REGS];
	uint8_t cmd = PCF8563_SECONDS;

	/*
	 * Convert our time representation into something the PCF8563
	 * can understand.
	 */
	regs[0] = TOBCD(dt->dt_sec);
	regs[1] = TOBCD(dt->dt_min);
	regs[2] = TOBCD(dt->dt_hour);
	regs[3] = TOBCD(dt->dt_day);
	regs[4] = TOBCD(dt->dt_wday);
	regs[5] = TOBCD(dt->dt_mon);
	regs[6] = TOBCD(dt->dt_year - 2000);

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_address,
	    &cmd, sizeof(cmd), regs, PCF8563_NRTC_REGS, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: pcxrtc_clock_write: failed to write rtc\n",
		    sc->sc_dev.dv_xname);
		return (0);
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
	return (1);
}
