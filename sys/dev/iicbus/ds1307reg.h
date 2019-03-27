/*-
 * Copyright (c) 2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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
 * Maxim DS1307 RTC registers.
 */

#ifndef _DS1307REG_H_
#define _DS1307REG_H_

#define	DS1307_SECS		0x00
#define	DS1307_SECS_MASK		0x7f
#define	DS1307_SECS_CH			0x80
#define	MCP7941X_SECS_ST		0x80
#define	DS1307_MINS		0x01
#define	DS1307_MINS_MASK		0x7f
#define	DS1307_HOUR		0x02
#define	DS1307_HOUR_MASK_12HR		0x1f
#define	DS1307_HOUR_MASK_24HR		0x3f
#define	DS1307_HOUR_IS_PM		0x20
#define	DS1307_HOUR_USE_AMPM		0x40
#define	DS1307_WEEKDAY		0x03
#define	MCP7941X_WEEKDAY_VBATEN		0x08
#define	DS1307_WEEKDAY_MASK		0x07
#define	DS1307_DATE		0x04
#define	DS1307_DATE_MASK		0x3f
#define	DS1307_MONTH		0x05
#define	MCP7941X_MONTH_LPYR		0x20
#define	DS1307_MONTH_MASK		0x1f
#define	DS1307_YEAR		0x06
#define	DS1307_YEAR_MASK		0xff
#define	DS1307_CONTROL		0x07
#define	DS1307_CTRL_OUT			(1 << 7)
#define	MCP7941X_CTRL_SQWE		(1 << 6)
#define	DS1307_CTRL_SQWE		(1 << 4)
#define	DS1307_CTRL_RS1			(1 << 1)
#define	DS1307_CTRL_RS0			(1 << 0)
#define	DS1307_CTRL_RS_MASK		(DS1307_CTRL_RS1 | DS1307_CTRL_RS0)
#define	DS1307_CTRL_MASK		0x93

#endif	/* _DS1307REG_H_ */
