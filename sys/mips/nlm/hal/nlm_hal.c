/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/xlp.h>

uint32_t
xlp_get_cpu_frequency(int node, int core)
{
	uint64_t sysbase = nlm_get_sys_regbase(node);
	unsigned int pll_divf, pll_divr, dfs_div, ext_div;
	unsigned int rstval, dfsval;

	rstval = nlm_read_sys_reg(sysbase, SYS_POWER_ON_RESET_CFG);
	dfsval = nlm_read_sys_reg(sysbase, SYS_CORE_DFS_DIV_VALUE);
	pll_divf = ((rstval >> 10) & 0x7f) + 1;
	pll_divr = ((rstval >> 8)  & 0x3) + 1;
	if (!nlm_is_xlp8xx_ax())
		ext_div = ((rstval >> 30) & 0x3) + 1;
	else
		ext_div = 1;
	dfs_div  = ((dfsval >> (core << 2)) & 0xf) + 1;

	return ((800000000ULL * pll_divf)/(3 * pll_divr * ext_div * dfs_div));
}

static u_int
nlm_get_device_frequency(uint64_t sysbase, int devtype)
{
	uint32_t pllctrl, dfsdiv, spf, spr, div_val;
	int extra_div;

	pllctrl = nlm_read_sys_reg(sysbase, SYS_PLL_CTRL);
	if (devtype <= 7)
		div_val = nlm_read_sys_reg(sysbase, SYS_DFS_DIV_VALUE0);
	else {
		devtype -= 8;
		div_val = nlm_read_sys_reg(sysbase, SYS_DFS_DIV_VALUE1);
	}
	dfsdiv = ((div_val >> (devtype << 2)) & 0xf) + 1;
	spf = (pllctrl >> 3 & 0x7f) + 1;
	spr = (pllctrl >> 1 & 0x03) + 1;
	if (devtype == DFS_DEVICE_NAE && !nlm_is_xlp8xx_ax())
		extra_div = 2;
	else
		extra_div = 1;

	return ((400 * spf) / (3 * extra_div * spr * dfsdiv));
}

int
nlm_set_device_frequency(int node, int devtype, int frequency)
{
	uint64_t sysbase;
	u_int cur_freq;
	int dec_div;

	sysbase = nlm_get_sys_regbase(node);
	cur_freq = nlm_get_device_frequency(sysbase, devtype);
	if (cur_freq < (frequency - 5))
		dec_div = 1;
	else
		dec_div = 0;

	for(;;) {
		if ((cur_freq >= (frequency - 5)) && (cur_freq <= frequency))
			break;
		if (dec_div)
			nlm_write_sys_reg(sysbase, SYS_DFS_DIV_DEC_CTRL,
			    (1 << devtype));
		else
			nlm_write_sys_reg(sysbase, SYS_DFS_DIV_INC_CTRL,
			    (1 << devtype));
		cur_freq = nlm_get_device_frequency(sysbase, devtype);
	}
	return (nlm_get_device_frequency(sysbase, devtype));
}
