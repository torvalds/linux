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
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/vmparam.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

/*
 * Virtual address space layout:
 * -----------------------------
 * 0x0000_0000 - 0xbfff_ffff	: user process
 *
 * 0xc040_0000 - virtual_avail	: kernel reserved (text, data, page tables
 *				: structures, ARM stacks etc.)
 * virtual_avail - 0xefff_ffff	: KVA (virtual_avail is typically < 0xc0a0_0000)
 * 0xf000_0000 - 0xf0ff_ffff	: no-cache allocation area (16MB)
 * 0xf100_0000 - 0xf10f_ffff	: SoC integrated devices registers range (1MB)
 * 0xf110_0000 - 0xf11f_ffff	: PCI-Express I/O space (1MB)
 * 0xf120_0000 - 0xf12f_ffff	: PCI I/O space (1MB)
 * 0xf130_0000 - 0xf52f_ffff	: PCI-Express memory space (64MB)
 * 0xf530_0000 - 0xf92f_ffff	: PCI memory space (64MB)
 * 0xf930_0000 - 0xfffe_ffff	: unused (~108MB)
 * 0xffff_0000 - 0xffff_0fff	: 'high' vectors page (4KB)
 * 0xffff_1000 - 0xffff_1fff	: ARM_TP_ADDRESS/RAS page (4KB)
 * 0xffff_2000 - 0xffff_ffff	: unused (~55KB)
 */


#if 0
int platform_pci_get_irq(u_int bus, u_int slot, u_int func, u_int pin);

/* Static device mappings. */
const struct devmap_entry db88f5xxx_devmap[] = {
	/*
	 * Map the on-board devices VA == PA so that we can access them
	 * with the MMU on or off.
	 */
	{ /* SoC integrated peripherals registers range */
		MV_BASE,
		MV_PHYS_BASE,
		MV_SIZE,
	},
	{ /* PCIE I/O */
		MV_PCIE_IO_BASE,
		MV_PCIE_IO_PHYS_BASE,
		MV_PCIE_IO_SIZE,
	},
	{ /* PCIE Memory */
		MV_PCIE_MEM_BASE,
		MV_PCIE_MEM_PHYS_BASE,
		MV_PCIE_MEM_SIZE,
	},
	{ /* PCI I/O */
		MV_PCI_IO_BASE,
		MV_PCI_IO_PHYS_BASE,
		MV_PCI_IO_SIZE,
	},
	{ /* PCI Memory */
		MV_PCI_MEM_BASE,
		MV_PCI_MEM_PHYS_BASE,
		MV_PCI_MEM_SIZE,
	},
	{ /* 7-seg LED */
		MV_DEV_CS0_BASE,
		MV_DEV_CS0_PHYS_BASE,
		MV_DEV_CS0_SIZE,
	},
	{ 0, 0, 0, }
};

/*
 * The pci_irq_map table consists of 3 columns:
 * - PCI slot number (less than zero means ANY).
 * - PCI IRQ pin (less than zero means ANY).
 * - PCI IRQ (less than zero marks end of table).
 *
 * IRQ number from the first matching entry is used to configure PCI device
 */

/* PCI IRQ Map for DB-88F5281 */
const struct obio_pci_irq_map pci_irq_map[] = {
	{ 7, -1, GPIO2IRQ(12) },
	{ 8, -1, GPIO2IRQ(13) },
	{ 9, -1, GPIO2IRQ(13) },
	{ -1, -1, -1 }
};

/* PCI IRQ Map for DB-88F5182 */
const struct obio_pci_irq_map pci_irq_map[] = {
	{ 7, -1, GPIO2IRQ(0) },
	{ 8, -1, GPIO2IRQ(1) },
	{ 9, -1, GPIO2IRQ(1) },
	{ -1, -1, -1 }
};
#endif

#if 0
/*
 * mv_gpio_config row structure:
 *	<GPIO number>, <GPIO flags>, <GPIO mode>
 *
 * - GPIO pin number (less than zero marks end of table)
 * - GPIO flags:
 *	MV_GPIO_BLINK
 *	MV_GPIO_POLAR_LOW
 *	MV_GPIO_EDGE
 *	MV_GPIO_LEVEL
 * - GPIO mode:
 *	1	- Output, set to HIGH.
 *	0	- Output, set to LOW.
 *	-1	- Input.
 */

/* GPIO Configuration for DB-88F5281 */
const struct gpio_config mv_gpio_config[] = {
	{ 12, MV_GPIO_POLAR_LOW | MV_GPIO_LEVEL, -1 },
	{ 13, MV_GPIO_POLAR_LOW | MV_GPIO_LEVEL, -1 },
	{ -1, -1, -1 }
};

#if 0
/* GPIO Configuration for DB-88F5182 */
const struct gpio_config mv_gpio_config[] = {
	{ 0, MV_GPIO_POLAR_LOW | MV_GPIO_LEVEL, -1 },
	{ 1, MV_GPIO_POLAR_LOW | MV_GPIO_LEVEL, -1 },
	{ -1, -1, -1 }
};
#endif

#endif
