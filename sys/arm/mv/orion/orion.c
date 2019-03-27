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

#if 0
extern const struct obio_pci_irq_map pci_irq_map[];
const struct obio_pci mv_pci_info[] = {
	{ MV_TYPE_PCIE,
		MV_PCIE_BASE,	MV_PCIE_SIZE,
		MV_PCIE_IO_BASE, MV_PCIE_IO_SIZE,	4, 0x51,
		MV_PCIE_MEM_BASE, MV_PCIE_MEM_SIZE,	4, 0x59,
		NULL, MV_INT_PEX0
	},

	{ MV_TYPE_PCI,
		MV_PCI_BASE, MV_PCI_SIZE,
		MV_PCI_IO_BASE, MV_PCI_IO_SIZE,		3, 0x51,
		MV_PCI_MEM_BASE, MV_PCI_MEM_SIZE,	3, 0x59,
		pci_irq_map, -1
	},

	{ 0, 0, 0 }
};
#endif

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

uint32_t
get_tclk(void)
{
	uint32_t sar;

	/*
	 * On Orion TCLK is can be configured to 150 MHz or 166 MHz.
	 * Current setting is read from Sample At Reset register.
	 */
	/* XXX MPP addr should be retrieved from the DT */
	sar = bus_space_read_4(fdtbus_bs_tag, MV_MPP_BASE, SAMPLE_AT_RESET);
	sar = (sar & TCLK_MASK) >> TCLK_SHIFT;
	switch (sar) {
	case 1:
		return (TCLK_150MHZ);
	case 2:
		return (TCLK_166MHZ);
	default:
		panic("Unknown TCLK settings!");
	}
}

uint32_t
get_cpu_freq(void)
{

	return (0);
}
