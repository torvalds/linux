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
#include <machine/fdt.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

/*
 * Virtual address space layout:
 * -----------------------------
 * 0x0000_0000 - 0xBFFF_FFFF	: User Process (3 GB)
 * 0xC000_0000 - virtual_avail	: Kernel Reserved (text, data, page tables,
 * 				: stack etc.)
 * virtual-avail - 0xEFFF_FFFF	: KVA (virtual_avail is typically < 0xc0a0_0000)
 * 0xF000_0000 - 0xF0FF_FFFF	: No-Cache allocation area (16 MB)
 * 0xF100_0000 - 0xF10F_FFFF	: SoC Integrated devices registers range (1 MB)
 * 0xF110_0000 - 0xF11F_FFFF	: PCI-Express I/O space (1MB)
 * 0xF120_0000 - 0xF12F_FFFF	: PCI I/O space (1MB)
 * 0xF130_0000 - 0xF52F_FFFF	: PCI-Express memory space (64MB)
 * 0xF530_0000 - 0xF92F_FFFF	: PCI memory space (64MB)
 * 0xF930_0000 - 0xF93F_FFFF	: Device Bus: BOOT (1 MB)
 * 0xF940_0000 - 0xF94F_FFFF	: Device Bus: CS0 (1 MB)
 * 0xF950_0000 - 0xFB4F_FFFF	: Device Bus: CS1 (32 MB)
 * 0xFB50_0000 - 0xFB5F_FFFF	: Device Bus: CS2 (1 MB)
 * 0xFB60_0000 - 0xFFFE_FFFF	: Unused (~74MB)
 * 0xFFFF_0000 - 0xFFFF_0FFF	: 'High' vectors page (4 kB)
 * 0xFFFF_1000 - 0xFFFF_1FFF	: ARM_TP_ADDRESS/RAS page (4 kB)
 * 0xFFFF_2000 - 0xFFFF_FFFF	: Unused (56 kB)
 */


struct resource_spec mv_gpio_res[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE },
	{ -1, 0 }
};

const struct decode_win idma_win_tbl[] = {
	{ 0 },
};
const struct decode_win *idma_wins = idma_win_tbl;
int idma_wins_no = 0;

const struct decode_win xor_win_tbl[] = {
	{ 0 },
};
const struct decode_win *xor_wins = xor_win_tbl;
int xor_wins_no = 0;

uint32_t
get_tclk(void)
{
	uint32_t sar;

	/*
	 * On Discovery TCLK is can be configured to 166 MHz or 200 MHz.
	 * Current setting is read from Sample At Reset register.
	 */
	sar = bus_space_read_4(fdtbus_bs_tag, MV_MPP_BASE, SAMPLE_AT_RESET_HI);
	sar = (sar & TCLK_MASK) >> TCLK_SHIFT;

	switch (sar) {
	case 0:
		return (TCLK_166MHZ);
	case 1:
		return (TCLK_200MHZ);
	default:
		panic("Unknown TCLK settings!");
	}
}

uint32_t
get_cpu_freq(void)
{

	return (0);
}
