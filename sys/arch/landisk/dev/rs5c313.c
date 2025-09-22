/*	$OpenBSD: rs5c313.c,v 1.5 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: rs5c313.c,v 1.1 2006/09/07 01:12:00 uwe Exp $	*/
/*	$NetBSD: rs5c313_landisk.c,v 1.1 2006/09/07 01:55:03 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * RICOH RS5C313 Real Time Clock
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <dev/clock_subr.h>
#include <sh/clock.h>

#include <sh/devreg.h>
#include <sh/dev/scireg.h>

#include <landisk/dev/rs5c313reg.h>
#include <landisk/landisk/landiskreg.h>

struct rs5c313_softc {
	struct device sc_dev;

	int sc_valid;		/* oscillation halt sensing on init */
};

/* chip access methods */
void rtc_begin(struct rs5c313_softc *);
void rtc_ce(struct rs5c313_softc *, int);
void rtc_dir(struct rs5c313_softc *, int);
void rtc_clk(struct rs5c313_softc *, int);
int  rtc_read(struct rs5c313_softc *);
void rtc_write(struct rs5c313_softc *, int);

int rs5c313_init(struct rs5c313_softc *);
int rs5c313_read_reg(struct rs5c313_softc *, int);
void rs5c313_write_reg(struct rs5c313_softc *, int, int);
void rs5c313_gettime(void *, time_t, struct clock_ymdhms *);
void rs5c313_settime(void *, struct clock_ymdhms *);

int
rs5c313_init(struct rs5c313_softc *sc)
{
	int status = 0;
	int retry;

	rtc_ce(sc, 0);

	rtc_begin(sc);
	rtc_ce(sc, 1);

	if ((rs5c313_read_reg(sc, RS5C313_CTRL) & CTRL_XSTP) == 0) {
		sc->sc_valid = 1;
		goto done;
	}

	sc->sc_valid = 0;
	printf("%s: time not valid\n", sc->sc_dev.dv_xname);

	rs5c313_write_reg(sc, RS5C313_TINT, 0);
	rs5c313_write_reg(sc, RS5C313_CTRL, (CTRL_BASE | CTRL_ADJ));

	for (retry = 1000; retry > 0; --retry) {
		if (rs5c313_read_reg(sc, RS5C313_CTRL) & CTRL_BSY)
			delay(1);
		else
			break;
	}

	if (retry == 0) {
		status = EIO;
		goto done;
	}

	rs5c313_write_reg(sc, RS5C313_CTRL, CTRL_BASE);

done:
	rtc_ce(sc, 0);
	return status;
}

int
rs5c313_read_reg(struct rs5c313_softc *sc, int addr)
{
	int data;

	/* output */
	rtc_dir(sc, 1);

	/* control */
	rtc_write(sc, 1);		/* ignored */
	rtc_write(sc, 1);		/* R/#W = 1(READ) */
	rtc_write(sc, 1);		/* AD = 1 */
	rtc_write(sc, 0);		/* DT = 0 */

	/* address */
	rtc_write(sc, addr & 0x8);	/* A3 */
	rtc_write(sc, addr & 0x4);	/* A2 */
	rtc_write(sc, addr & 0x2);	/* A1 */
	rtc_write(sc, addr & 0x1);	/* A0 */

	/* input */
	rtc_dir(sc, 0);

	/* ignore */
	(void)rtc_read(sc);
	(void)rtc_read(sc);
	(void)rtc_read(sc);
	(void)rtc_read(sc);

	/* data */
	data = rtc_read(sc);	/* D3 */
	data <<= 1;
	data |= rtc_read(sc);	/* D2 */
	data <<= 1;
	data |= rtc_read(sc);	/* D1 */
	data <<= 1;
	data |= rtc_read(sc);	/* D0 */

	return data;
}

void
rs5c313_write_reg(struct rs5c313_softc *sc, int addr, int data)
{
	/* output */
	rtc_dir(sc, 1);

	/* control */
	rtc_write(sc, 1);		/* ignored */
	rtc_write(sc, 0);		/* R/#W = 0 (WRITE) */
	rtc_write(sc, 1);		/* AD = 1 */
	rtc_write(sc, 0);		/* DT = 0 */

	/* address */
	rtc_write(sc, addr & 0x8);	/* A3 */
	rtc_write(sc, addr & 0x4);	/* A2 */
	rtc_write(sc, addr & 0x2);	/* A1 */
	rtc_write(sc, addr & 0x1);	/* A0 */

	/* control */
	rtc_write(sc, 1);		/* ignored */
	rtc_write(sc, 0);		/* R/#W = 0(WRITE) */
	rtc_write(sc, 0);		/* AD = 0 */
	rtc_write(sc, 1);		/* DT = 1 */

	/* data */
	rtc_write(sc, data & 0x8);	/* D3 */
	rtc_write(sc, data & 0x4);	/* D2 */
	rtc_write(sc, data & 0x2);	/* D1 */
	rtc_write(sc, data & 0x1);	/* D0 */
}

void
rs5c313_gettime(void *cookie, time_t base, struct clock_ymdhms *dt)
{
	struct rs5c313_softc *sc = cookie;
	int retry;
	int s;

	/*
	 * If chip had invalid data on init, don't bother reading
	 * bogus values.
	 */
	if (sc->sc_valid == 0)
		return;

	s = splhigh();

	rtc_begin(sc);
	for (retry = 10; retry > 0; --retry) {
		rtc_ce(sc, 1);

		rs5c313_write_reg(sc, RS5C313_CTRL, CTRL_BASE);
		if ((rs5c313_read_reg(sc, RS5C313_CTRL) & CTRL_BSY) == 0)
			break;

		rtc_ce(sc, 0);
		delay(1);
	}

	if (retry == 0) {
		splx(s);
		return;
	}

#define RTCGET(x, y)							\
	do {								\
		int ones = rs5c313_read_reg(sc, RS5C313_ ## y ## 1);	\
		int tens = rs5c313_read_reg(sc, RS5C313_ ## y ## 10);	\
		dt->dt_ ## x = tens * 10 + ones;			\
	} while (/* CONSTCOND */0)

	RTCGET(sec, SEC);
	RTCGET(min, MIN);
	RTCGET(hour, HOUR);
	RTCGET(day, DAY);
	RTCGET(mon, MON);
	RTCGET(year, YEAR);
#undef	RTCGET
	dt->dt_wday = rs5c313_read_reg(sc, RS5C313_WDAY);

	rtc_ce(sc, 0);
	splx(s);

	dt->dt_year = (dt->dt_year % 100) + 1900;
	if (dt->dt_year < 1970) {
		dt->dt_year += 100;
	}
}

void
rs5c313_settime(void *cookie, struct clock_ymdhms *dt)
{
	struct rs5c313_softc *sc = cookie;
	int retry;
	int t;
	int s;

	s = splhigh();

	rtc_begin(sc);
	for (retry = 10; retry > 0; --retry) {
		rtc_ce(sc, 1);

		rs5c313_write_reg(sc, RS5C313_CTRL, CTRL_BASE);
		if ((rs5c313_read_reg(sc, RS5C313_CTRL) & CTRL_BSY) == 0)
			break;

		rtc_ce(sc, 0);
		delay(1);
	}

	if (retry == 0) {
		splx(s);
		return;
	}

#define	RTCSET(x, y)							     \
	do {								     \
		t = TOBCD(dt->dt_ ## y) & 0xff;				     \
		rs5c313_write_reg(sc, RS5C313_ ## x ## 1, t & 0x0f);	     \
		rs5c313_write_reg(sc, RS5C313_ ## x ## 10, (t >> 4) & 0x0f); \
	} while (/* CONSTCOND */0)

	RTCSET(SEC, sec);
	RTCSET(MIN, min);
	RTCSET(HOUR, hour);
	RTCSET(DAY, day);
	RTCSET(MON, mon);

#undef	RTCSET

	t = dt->dt_year % 100;
	t = TOBCD(t);
	rs5c313_write_reg(sc, RS5C313_YEAR1, t & 0x0f);
	rs5c313_write_reg(sc, RS5C313_YEAR10, (t >> 4) & 0x0f);

	rs5c313_write_reg(sc, RS5C313_WDAY, dt->dt_wday);

	rtc_ce(sc, 0);
	splx(s);

	sc->sc_valid = 1;
}

struct rtc_ops rs5c313_ops = {
	NULL,
	NULL,			/* not used */
	rs5c313_gettime,
	rs5c313_settime
};

void
rtc_begin(struct rs5c313_softc *sc)
{
	SHREG_SCSPTR = SCSPTR_SPB1IO | SCSPTR_SPB1DT
		     | SCSPTR_SPB0IO | SCSPTR_SPB0DT;
	delay(100);
}

/*
 * CE pin
 */
void
rtc_ce(struct rs5c313_softc *sc, int onoff)
{
	if (onoff)
		_reg_write_1(LANDISK_PWRMNG, PWRMNG_RTC_CE);
	else
		_reg_write_1(LANDISK_PWRMNG, 0);
	delay(600);
}

/*
 * SCLK pin is connected to SPB0DT.
 * SPB0DT is always in output mode, we set SPB0IO in rtc_begin.
 */
void
rtc_clk(struct rs5c313_softc *sc, int onoff)
{
	uint8_t r = SHREG_SCSPTR;

	if (onoff)
		r |= SCSPTR_SPB0DT;
	else
		r &= ~SCSPTR_SPB0DT;
	SHREG_SCSPTR = r;
}

/*
 * SIO pin is connected to SPB1DT.
 * SPB1DT is output when SPB1IO is set.
 */
void
rtc_dir(struct rs5c313_softc *sc, int output)
{
	uint8_t r = SHREG_SCSPTR;

	if (output)
		r |= SCSPTR_SPB1IO;
	else
		r &= ~SCSPTR_SPB1IO;
	SHREG_SCSPTR = r;
}

/* 
 * Read bit from SPB1DT pin.
 */
int
rtc_read(struct rs5c313_softc *sc)
{
	int bit;

	delay(300);

	bit = (SHREG_SCSPTR & SCSPTR_SPB1DT) ? 1 : 0;

	rtc_clk(sc, 0);
	delay(300);
	rtc_clk(sc, 1);

	return bit;
}

/* 
 * Write bit via SPB1DT pin.
 */
void
rtc_write(struct rs5c313_softc *sc, int bit)
{
	uint8_t r = SHREG_SCSPTR;

	if (bit)
		r |= SCSPTR_SPB1DT;
	else
		r &= ~SCSPTR_SPB1DT;
	SHREG_SCSPTR = r;

	delay(300);

	rtc_clk(sc, 0);
	delay(300);
	rtc_clk(sc, 1);
}

/* autoconf glue */
int rs5c313_landisk_match(struct device *, void *, void *);
void rs5c313_landisk_attach(struct device *, struct device *, void *);

const struct cfattach rsclock_ca = {
	sizeof (struct rs5c313_softc),
	rs5c313_landisk_match, rs5c313_landisk_attach
};

struct cfdriver rsclock_cd = {
	NULL, "rsclock", DV_DULL
};

int
rs5c313_landisk_match(struct device *parent, void *vcf, void *aux)
{
	static int matched = 0;

	if (matched)
		return (0);

	return (matched = 1);
}

void
rs5c313_landisk_attach(struct device *parent, struct device *self, void *aux)
{
	struct rs5c313_softc *sc = (void *)self;

	printf(": RS5C313 real time clock\n");

	if (rs5c313_init(sc) != 0) {
		printf("%s: init failed\n", self->dv_xname);
		return;
	}

	rs5c313_ops._cookie = sc;
	sh_clock_init(SH_CLOCK_NORTC, &rs5c313_ops);
}
