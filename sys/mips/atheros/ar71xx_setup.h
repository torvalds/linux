/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Adrian Chadd
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
 */

/* $FreeBSD$ */

#ifndef	__AR71XX_SETUP_H__
#define	__AR71XX_SETUP_H__

enum ar71xx_soc_type {
	AR71XX_SOC_UNKNOWN,
	AR71XX_SOC_AR7130,
	AR71XX_SOC_AR7141,
	AR71XX_SOC_AR7161,
	AR71XX_SOC_AR7240,
	AR71XX_SOC_AR7241,
	AR71XX_SOC_AR7242,
	AR71XX_SOC_AR9130,
	AR71XX_SOC_AR9132,
	AR71XX_SOC_AR9330,
	AR71XX_SOC_AR9331,
	AR71XX_SOC_AR9341,
	AR71XX_SOC_AR9342,
	AR71XX_SOC_AR9344,
	AR71XX_SOC_QCA9556,
	AR71XX_SOC_QCA9558,
	AR71XX_SOC_QCA9533,
	AR71XX_SOC_QCA9533_V2,
};
extern enum ar71xx_soc_type ar71xx_soc;

extern void ar71xx_detect_sys_type(void);
extern const char *ar71xx_get_system_type(void);

#endif
