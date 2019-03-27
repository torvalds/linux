/*-
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: FreeBSD: sys/arm/lpc/lpc_machdep.c
 */

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	_ARM32_BUS_DMA_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/reboot.h>
#include <sys/devmap.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/machdep.h>
#include <machine/platform.h> 
#include <machine/cpu.h>

#include <arm/ralink/rt1310reg.h>
#include <arm/ralink/rt1310var.h>

#include <dev/fdt/fdt_common.h>

#ifdef  EARLY_PRINTF
early_putc_t *early_putc;
#endif


uint32_t rt1310_master_clock;

vm_offset_t
platform_lastaddr(void)
{

	return (devmap_lastaddr());
}

void
platform_probe_and_attach(void)
{
}

void
platform_gpio_init(void)
{

	/*
	 * Set initial values of GPIO output ports
	 */
}

void
platform_late_init(void)
{
	bootverbose = 1;
}

/*
 * Add a single static device mapping.
 * The values used were taken from the ranges property of the SoC node in the
 * dts file when this code was converted to arm_devmap_add_entry().
 */
int
platform_devmap_init(void)
{
	devmap_add_entry(0x19C00000, 0xE0000);
	devmap_add_entry(0x1e800000, 0x800000);
	devmap_add_entry(0x1f000000, 0x400000);
	return (0);
}

struct arm32_dma_range *
bus_dma_get_range(void)
{

	return (NULL);
}

int
bus_dma_get_range_nb(void)
{

	return (0);
}

void
cpu_reset(void)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bst = fdtbus_bs_tag;

	/* Enable WDT */
	/* Instant assert of RESETOUT_N with pulse length 1ms */
	bus_space_map(bst, 0x1e8c0000, 0x20000, 0, &bsh);
	bus_space_write_4(bst, bsh, 0, 13000);
	bus_space_write_4(bst, bsh, 8, (1<<3) | (1<<4) | 7);
	bus_space_unmap(bst, bsh, 0x20000);

	for (;;)
		continue;
}

#ifdef RALINK_BOOT_DEBUG
void bootdebug1(int c);
void bootdebug1(int c)
{
	/* direct put uart physical address */
        uint8_t* uart_base_addr=(uint8_t*)0x1e840000;
	*(uart_base_addr) = c;
}

void bootdebug2(int c);
void bootdebug2(int c)
{
#if defined(SOCDEV_PA) && defined(SOCDEV_VA)
	/* direct put uart map address at locore-v4.S */
        uint8_t* uart_base_addr=(uint8_t*)0xce840000;
	*(uart_base_addr) = c;
#endif
}

void bootdebug3(int c);
void bootdebug3(int c)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bst = fdtbus_bs_tag;
	bus_space_map(bst, 0x1e840000, 0x20000, 0, &bsh);
	bus_space_write_1(bst, bsh, 0, c);
	bus_space_unmap(bst, bsh, 0x20000);
}
#endif
