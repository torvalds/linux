/*-
 * Copyright (c) 2014-2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * Maxim DS3231 RTC registers.
 */

#ifndef _DS3231REG_H_
#define _DS3231REG_H_

#define	DS3231_SECS		0x00
#define	DS3231_SECS_MASK		0x7f
#define	DS3231_MINS		0x01
#define	DS3231_MINS_MASK		0x7f
#define	DS3231_HOUR		0x02
#define	DS3231_HOUR_MASK_12HR		0x3f
#define	DS3231_HOUR_MASK_24HR		0x1f
#define	DS3231_HOUR_IS_PM		0x20
#define	DS3231_HOUR_USE_AMPM		0x40
#define	DS3231_WEEKDAY		0x03
#define	DS3231_WEEKDAY_MASK		0x07
#define	DS3231_DATE		0x04
#define	DS3231_DATE_MASK		0x3f
#define	DS3231_MONTH		0x05
#define	DS3231_MONTH_MASK		0x1f
#define	DS3231_C_MASK			0x80
#define	DS3231_YEAR		0x06
#define	DS3231_YEAR_MASK		0xff
#define	DS3231_CONTROL		0x0e
#define	DS3231_CTRL_EOSC		(1 << 7)
#define	DS3231_CTRL_BBSQW		(1 << 6)
#define	DS3231_CTRL_CONV		(1 << 5)
#define	DS3231_CTRL_RS2			(1 << 4)
#define	DS3231_CTRL_RS1			(1 << 3)
#define	DS3231_CTRL_RS_MASK		(DS3231_CTRL_RS2 | DS3231_CTRL_RS1)
#define	DS3231_CTRL_RS_SHIFT		3
#define	DS3231_CTRL_INTCN		(1 << 2)
#define	DS3231_CTRL_A2IE		(1 << 1)
#define	DS3231_CTRL_A1IE		(1 << 0)
#define	DS3231_CTRL_MASK		\
	(DS3231_CTRL_EOSC | DS3231_CTRL_A1IE | DS3231_CTRL_A2IE)
#define	DS3231_STATUS		0x0f
#define	DS3231_STATUS_OSF		(1 << 7)
#define	DS3231_STATUS_EN32KHZ		(1 << 3)
#define	DS3231_STATUS_BUSY		(1 << 2)
#define	DS3231_STATUS_A2F		(1 << 1)
#define	DS3231_STATUS_A1F		(1 << 0)
#define	DS3231_TEMP		0x11
#define	DS3231_TEMP_MASK		0xffc0
#define	DS3231_0500C			0x80
#define	DS3231_0250C			0x40
#define	DS3231_MSB			0x8000
#define	DS3231_NEG_BIT			DS3231_MSB
#define	TZ_ZEROC			2731

#endif	/* _DS3231REG_H_ */
