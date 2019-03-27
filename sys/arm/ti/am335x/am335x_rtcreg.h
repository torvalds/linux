/*-
 * Copyright 2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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

#ifndef _AM335X_RTCREG_H_
#define _AM335X_RTCREG_H_

#define	RTC_SECONDS		0x00
#define	RTC_MINUTES		0x04
#define	RTC_HOURS		0x08
#define	RTC_DAYS		0x0c
#define	RTC_MONTHS		0x10
#define	RTC_YEARS		0x14
#define	RTC_WEEK		0x18
#define	RTC_CTRL		0x40
#define	RTC_CTRL_DISABLE		(1U << 6)
#define	RTC_CTRL_RUN			(1U << 0)
#define	RTC_STATUS		0x44
#define	RTC_STATUS_ALARM2		(1U << 7)
#define	RTC_STATUS_ALARM		(1U << 6)
#define	RTC_STATUS_1D_EVENT		(1U << 5)
#define	RTC_STATUS_1H_EVENT		(1U << 4)
#define	RTC_STATUS_1M_EVENT		(1U << 3)
#define	RTC_STATUS_1S_EVENT		(1U << 2)
#define	RTC_STATUS_RUN			(1U << 1)
#define	RTC_STATUS_BUSY			(1U << 0)
#define	RTC_INTR		0x48
#define	RTC_INTR_ALARM2			(1U << 4)
#define	RTC_INTR_ALARM			(1U << 3)
#define	RTC_INTR_TIMER			(1U << 2)
#define	RTC_OSC			0x54
#define	RTC_OSC_32KCLK_EN		(1U << 6)
#define	RTC_OSC_OSC32K_GZ		(1U << 4)
#define	RTC_OSC_32KCLK_SEL		(1U << 3)
#define	RTC_OSC_RES_SELECT		(1U << 2)
#define	RTC_OSC_SW2			(1U << 1)
#define	RTC_OSC_SW1			(1U << 0)
#define	RTC_KICK0R		0x6c
#define	RTC_KICK0R_PASS			0x83e70b13
#define	RTC_KICK1R		0x70
#define	RTC_KICK1R_PASS			0x95a4f1e0
#define	RTC_REVISION		0x74
#define	RTC_ALARM2_SECONDS	0x80
#define	RTC_ALARM2_MINUTES	0x84
#define	RTC_ALARM2_HOURS	0x88
#define	RTC_ALARM2_DAYS		0x8c
#define	RTC_ALARM2_MONTHS	0x90
#define	RTC_ALARM2_YEARS	0x94
#define	RTC_PMIC		0x98
#define	PMIC_PWR_ENABLE			(1U << 16)

#endif /* _AM335X_RTCREG_H_ */
