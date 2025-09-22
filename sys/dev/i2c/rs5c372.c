/*	$OpenBSD: rs5c372.c,v 1.8 2022/10/12 13:39:50 kettenis Exp $	*/
/*	$NetBSD: rs5c372.c,v 1.5 2006/03/29 06:41:24 thorpej Exp $	*/

/*
 * Copyright (c) 2005 Kimihiro Nonaka
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
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/event.h>

#include <dev/clock_subr.h>

#include <dev/i2c/i2cvar.h>

/*
 * RS5C372[AB] Real-Time Clock
 */

#define	RICOHRTC_ADDR		0x32	/* Fixed I2C Slave Address */

#define RICOHRTC_SECONDS	0
#define RICOHRTC_MINUTES	1
#define RICOHRTC_HOURS		2
#define RICOHRTC_DAY		3
#define RICOHRTC_DATE		4
#define RICOHRTC_MONTH		5
#define RICOHRTC_YEAR		6
#define RICOHRTC_CLOCK_CORRECT	7
#define RICOHRTC_ALARMA_MIN	8
#define RICOHRTC_ALARMA_HOUR	9
#define RICOHRTC_ALARMA_DATE	10
#define RICOHRTC_ALARMB_MIN	11
#define RICOHRTC_ALARMB_HOUR	12
#define RICOHRTC_ALARMB_DATE	13
#define RICOHRTC_CONTROL1	14
#define RICOHRTC_CONTROL2	15
#define	RICOHRTC_NREGS		16
#define	RICOHRTC_NRTC_REGS	7

/*
 * Bit definitions.
 */
#define	RICOHRTC_SECONDS_MASK	0x7f
#define	RICOHRTC_MINUTES_MASK	0x7f
#define	RICOHRTC_HOURS_12HRS_PM	(1u << 5)	/* If 12 hr mode, set = PM */
#define	RICOHRTC_HOURS_12MASK	0x1f
#define	RICOHRTC_HOURS_24MASK	0x3f
#define	RICOHRTC_DAY_MASK	0x07
#define	RICOHRTC_DATE_MASK	0x3f
#define	RICOHRTC_MONTH_MASK	0x1f
#define	RICOHRTC_CONTROL2_24HRS	(1u << 5)
#define	RICOHRTC_CONTROL2_XSTP	(1u << 4)	/* read */
#define	RICOHRTC_CONTROL2_ADJ	(1u << 4)	/* write */
#define	RICOHRTC_CONTROL2_NCLEN	(1u << 3)
#define	RICOHRTC_CONTROL2_CTFG	(1u << 2)
#define	RICOHRTC_CONTROL2_AAFG	(1u << 1)
#define	RICOHRTC_CONTROL2_BAFG	(1u << 0)

struct ricohrtc_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	int sc_address;
	struct todr_chip_handle sc_todr;
};

int ricohrtc_match(struct device *, void *, void *);
void ricohrtc_attach(struct device *, struct device *, void *);

const struct cfattach ricohrtc_ca = {
	sizeof(struct ricohrtc_softc), ricohrtc_match, ricohrtc_attach
};

struct cfdriver ricohrtc_cd = {
	NULL, "ricohrtc", DV_DULL
};

void ricohrtc_reg_write(struct ricohrtc_softc *, int, uint8_t);
int ricohrtc_clock_read(struct ricohrtc_softc *, struct clock_ymdhms *);
int ricohrtc_clock_write(struct ricohrtc_softc *, struct clock_ymdhms *);
int ricohrtc_gettime(struct todr_chip_handle *, struct timeval *);
int ricohrtc_settime(struct todr_chip_handle *, struct timeval *);

int
ricohrtc_match(struct device *parent, void *v, void *arg)
{
	struct i2c_attach_args *ia = arg;
#ifdef PARANOID_CHECKS
	u_int8_t data, cmd;
#endif

	if (ia->ia_addr != RICOHRTC_ADDR)
		return (0);

#ifdef PARANOID_CHECKS
	/* Verify that the 'reserved bits in a few registers read 0 */
	if (iic_acquire_bus(ia->ia_tag, I2C_F_POLL)) {
		printf("ricohrtc acquire fail\n");
		return (0);
	}

	cmd = RICOHRTC_SECONDS;
	if (iic_exec(ia->ia_tag, I2C_OP_READ_WITH_STOP, ia->ia_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL)) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc read %d fail\n", cmd);
		return (0);
	}
	if ((data & ~RICOHRTC_SECONDS_MASK) != 0) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc second %d\n",data);
		return (0);
	}

	cmd = RICOHRTC_MINUTES;
	if (iic_exec(ia->ia_tag, I2C_OP_READ_WITH_STOP, ia->ia_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL)) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc read %d fail\n", cmd);
		return (0);
	}

	if ((data & ~RICOHRTC_MINUTES_MASK) != 0) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc minute %d\n",data);
		return (0);
	}

	cmd = RICOHRTC_HOURS;
	if (iic_exec(ia->ia_tag, I2C_OP_READ_WITH_STOP, ia->ia_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL)) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc read %d fail\n", cmd);
		return (0);
	}
	if ((data & ~RICOHRTC_HOURS_24MASK) != 0) {
		iic_release_bus(ia->ia_tag, I2C_F_POLL);
		printf("ricohrtc hour %d\n",data);
		return (0);
	}
	iic_release_bus(ia->ia_tag, I2C_F_POLL);

#endif
	return (1);
}

void
ricohrtc_attach(struct device *parent, struct device *self, void *arg)
{
	struct ricohrtc_softc *sc = (struct ricohrtc_softc *)self;
	struct i2c_attach_args *ia = arg;

	printf(": RICOH RS5C372[AB] Real-time Clock\n");

	sc->sc_tag = ia->ia_tag;
	sc->sc_address = ia->ia_addr;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = ricohrtc_gettime;
	sc->sc_todr.todr_settime = ricohrtc_settime;
	sc->sc_todr.todr_setwen = NULL;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	/* Initialize RTC */
	ricohrtc_reg_write(sc, RICOHRTC_CONTROL2, RICOHRTC_CONTROL2_24HRS);
	ricohrtc_reg_write(sc, RICOHRTC_CONTROL1, 0);
}

int
ricohrtc_gettime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct ricohrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;

	memset(&dt, 0, sizeof(dt));
	if (ricohrtc_clock_read(sc, &dt) == 0)
		return (-1);

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return (0);
}

int
ricohrtc_settime(struct todr_chip_handle *ch, struct timeval *tv)
{
	struct ricohrtc_softc *sc = ch->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	if (ricohrtc_clock_write(sc, &dt) == 0)
		return (-1);
	return (0);
}

void
ricohrtc_reg_write(struct ricohrtc_softc *sc, int reg, uint8_t val)
{
	uint8_t cmd;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	reg &= 0xf;
	cmd = (reg << 4);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_address,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: ricohrtc_reg_write: failed to write reg%d\n",
		    sc->sc_dev.dv_xname, reg);
		return;
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}

int
ricohrtc_clock_read(struct ricohrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t bcd[RICOHRTC_NRTC_REGS];
	uint8_t cmd;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	cmd = (RICOHRTC_SECONDS << 4);
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_address,
	    &cmd, sizeof cmd, bcd, RICOHRTC_NRTC_REGS, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: ricohrtc_clock_read: failed to read rtc\n",
		    sc->sc_dev.dv_xname);
		return (0);
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	/*
	 * Convert the RICOHRTC's register values into something useable
	 */
	dt->dt_sec = FROMBCD(bcd[RICOHRTC_SECONDS] & RICOHRTC_SECONDS_MASK);
	dt->dt_min = FROMBCD(bcd[RICOHRTC_MINUTES] & RICOHRTC_MINUTES_MASK);
	dt->dt_hour = FROMBCD(bcd[RICOHRTC_HOURS] & RICOHRTC_HOURS_24MASK);
	dt->dt_day = FROMBCD(bcd[RICOHRTC_DATE] & RICOHRTC_DATE_MASK);
	dt->dt_mon = FROMBCD(bcd[RICOHRTC_MONTH] & RICOHRTC_MONTH_MASK);
	dt->dt_year = FROMBCD(bcd[RICOHRTC_YEAR]) + POSIX_BASE_YEAR;
	return (1);
}

int
ricohrtc_clock_write(struct ricohrtc_softc *sc, struct clock_ymdhms *dt)
{
	uint8_t bcd[RICOHRTC_NRTC_REGS];
	uint8_t cmd;

	/*
	 * Convert our time representation into something the RICOHRTC
	 * can understand.
	 */
	bcd[RICOHRTC_SECONDS] = TOBCD(dt->dt_sec);
	bcd[RICOHRTC_MINUTES] = TOBCD(dt->dt_min);
	bcd[RICOHRTC_HOURS] = TOBCD(dt->dt_hour);
	bcd[RICOHRTC_DATE] = TOBCD(dt->dt_day);
	bcd[RICOHRTC_DAY] = TOBCD(dt->dt_wday);
	bcd[RICOHRTC_MONTH] = TOBCD(dt->dt_mon);
	bcd[RICOHRTC_YEAR] = TOBCD(dt->dt_year - POSIX_BASE_YEAR);

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	cmd = (RICOHRTC_SECONDS << 4);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_address,
	    &cmd, sizeof cmd, bcd, RICOHRTC_NRTC_REGS, I2C_F_POLL)) {
		iic_release_bus(sc->sc_tag, I2C_F_POLL);
		printf("%s: ricohrtc_clock_write: failed to write rtc\n",
		    sc->sc_dev.dv_xname);
		return (0);
	}
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
	return (1);
}
