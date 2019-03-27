/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#ifndef __AM335X_SCM_H__
#define __AM335X_SCM_H__

/* AM335x-specific registers for control module (scm) */
#define	SCM_CTRL_STATUS	0x40
#define	SCM_BGAP_CTRL	0x448
#define	SCM_BGAP_TEMP_MASK	0xff
#define	SCM_BGAP_TEMP_SHIFT	8
#define	SCM_BGAP_BGOFF		(1 << 6)
#define	SCM_BGAP_SOC		(1 << 4)
#define	SCM_BGAP_CLRZ		(1 << 3)
#define	SCM_BGAP_CONTCONV	(1 << 2)
#define	SCM_BGAP_EOCZ		(1 << 1)
#define	SCM_USB_CTRL0	0x620
#define	SCM_USB_STS0	0x624
#define	SCM_USB_CTRL1	0x628
#define	SCM_USB_STS1	0x62C
#define	SCM_MAC_ID0_LO	0x630
#define	SCM_MAC_ID0_HI	0x634
#define	SCM_PWMSS_CTRL	0x664

#endif /* __AM335X_SCM_H__ */
