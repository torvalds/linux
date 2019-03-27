/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2006-2008 Semihalf, Grzegorz Bernacki
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_RTC_DS1553_H_
#define _DEV_RTC_DS1553_H_

/* DS1553 registers */
#define DS1553_NVRAM_SIZE		0x1ff0
#define DS1553_OFF_FLAGS		0x1ff0
#define DS1553_OFF_ALARM_SECONDS	0x1ff2
#define DS1553_OFF_ALARM_MINUTES	0x1ff3
#define DS1553_OFF_ALARM_HOURS		0x1ff4
#define DS1553_OFF_ALARM_DATE		0x1ff5
#define DS1553_OFF_INTERRUPTS		0x1ff6
#define DS1553_OFF_WATCHDOG		0x1ff7
#define DS1553_OFF_CONTROL		0x1ff8
#define DS1553_OFF_SECONDS		0x1ff9
#define DS1553_OFF_MINUTES		0x1ffa
#define DS1553_OFF_HOURS		0x1ffb
#define DS1553_OFF_DAYOFWEEK		0x1ffc
#define DS1553_OFF_DATE			0x1ffd
#define DS1553_OFF_MONTH		0x1ffe
#define DS1553_OFF_YEAR			0x1fff

/* dayofweek register's bits */
#define DS1553_BIT_FREQ_TEST		0x40 /* frequency test bit */

/* seconds register's bit */
#define DS1553_BIT_OSC			0x80 /* oscillator start/stop bit */

/* control register's bits */
#define DS1553_BIT_WRITE		0x80 /* write */
#define DS1553_BIT_READ			0x40 /* read */

/* watchdog register's bits */
#define DS1553_BIT_WATCHDOG		0x80 /* watchdog steering bit */
#define DS1553_BIT_BMB4			0x40 /* watchdog multiplier bit4 */
#define DS1553_BIT_BMB3			0x20 /* watchdog multiplier bit3 */
#define DS1553_BIT_BMB2			0x10 /* watchdog multiplier bit2 */
#define DS1553_BIT_BMB1			0x8  /* watchdog multiplier bit1 */
#define DS1553_BIT_BMB0			0x4  /* watchdog multiplier bit0 */
#define DS1553_BIT_RB1			0x2  /* watchdog resolution bit1 */
#define DS1553_BIT_RB0			0x1  /* watchdog resolution bit0 */

/* alarm seconds/minutes/hours/date register's bit */
#define DS1553_BIT_AM			0x80 /* alarm mask bit */

/* flag register's bits */
#define DS1553_BIT_BLF			0x10 /* battery flag */
#define DS1553_BIT_WF			0x80 /* watchdog flag */

/* register's mask */
#define DS1553_MASK_MONTH		0x1f
#define DS1553_MASK_DATE		0x3f
#define DS1553_MASK_DAYOFWEEK		0x7
#define DS1553_MASK_HOUR		0x3f
#define DS1553_MASK_MINUTES		0x7f
#define DS1553_MASK_SECONDS		0x7f

struct ds1553_softc {

	bus_space_tag_t		sc_bst;	/* bus space tag */
	bus_space_handle_t	sc_bsh;	/* bus space handle */

	int			rid;	/* resource id */
	struct resource		*res;
	struct mtx		sc_mtx;	/* hardware mutex */

	uint32_t		year_offset;
	/* read/write functions */
	uint8_t			(*sc_read)(device_t, bus_size_t);
	void			(*sc_write)(device_t, bus_size_t, uint8_t);
};

/* device interface */
int ds1553_attach(device_t);

/* clock interface */
int ds1553_gettime(device_t, struct timespec *);
int ds1553_settime(device_t, struct timespec *);

#endif /* _DEV_RTC_DS1553_H_ */
