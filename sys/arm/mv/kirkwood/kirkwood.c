/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

struct resource_spec mv_gpio_res[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE },
	{ SYS_RES_IRQ,		5,	RF_ACTIVE },
	{ SYS_RES_IRQ,		6,	RF_ACTIVE },
	{ -1, 0 }
};

const struct decode_win xor_win_tbl[] = {
	{ 0 },
};
const struct decode_win *xor_wins = xor_win_tbl;
int xor_wins_no = 0;

uint32_t
get_tclk(void)
{
	uint32_t dev, rev;

	/*
	 * On Kirkwood TCLK is not configurable and depends on silicon
	 * revision:
	 * - A0 and A1 have TCLK hardcoded to 200 MHz.
	 * - Z0 and others have TCLK hardcoded to 166 MHz.
	 */
	soc_id(&dev, &rev);
	if (dev == MV_DEV_88F6281 && (rev == 2 || rev == 3))
		return (TCLK_200MHZ);
	if (dev == MV_DEV_88F6282)
		return (TCLK_200MHZ);

	return (TCLK_166MHZ);
}

uint32_t
get_cpu_freq(void)
{

	return (0);
}
