/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

/* $FreeBSD$ */

#ifndef __PCF2123REG_H__
#define	__PCF2123REG_H__

/* Control and status */
#define	PCF2123_REG_CTRL1		0x0
#define	PCF2123_REG_CTRL2		0x1

/* Time and date */
#define	PCF2123_REG_SECONDS		0x2
#define	PCF2123_REG_MINUTES		0x3
#define	PCF2123_REG_HOURS		0x4
#define	PCF2123_REG_DAYS		0x5
#define	PCF2123_REG_WEEKDAYS		0x6
#define	PCF2123_REG_MONTHS		0x7
#define	PCF2123_REG_YEARS		0x8

/* Alarm registers */
#define	PCF2123_REG_MINUTE_ALARM	0x9
#define	PCF2123_REG_HOUR_ALARM		0xA
#define	PCF2123_REG_DAY_ALARM		0xB
#define	PCF2123_REG_WEEKDAY_ALARM	0xC

/* Offset */
#define	PCF2123_REG_OFFSET		0xD

/* Timer */
#define	PCF2123_REG_TIMER_CLKOUT	0xE
#define	PCF2123_REG_COUNTDOWN_TIMER	0xF

/* Commands */
#define	PCF2123_CMD_READ	(1 << 7)
#define	PCF2123_CMD_WRITE	(0 << 7)

#define	PCF2123_READ(reg)	(PCF2123_CMD_READ | (1 << 4) | (reg))
#define	PCF2123_WRITE(reg)	(PCF2123_CMD_WRITE | (1 << 4) | (reg))

#endif /* __PCF2123REG_H__ */

