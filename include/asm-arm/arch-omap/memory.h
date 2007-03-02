/*
 * linux/include/asm-arm/arch-omap/memory.h
 *
 * Memory map for OMAP-1510 and 1610
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This file was derived from linux/include/asm-arm/arch-intergrator/memory.h
 * Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#if defined(CONFIG_ARCH_OMAP1)
#define PHYS_OFFSET		UL(0x10000000)
#elif defined(CONFIG_ARCH_OMAP2)
#define PHYS_OFFSET		UL(0x80000000)
#endif

/*
 * Conversion between SDRAM and fake PCI bus, used by USB
 * NOTE: Physical address must be converted to Local Bus address
 *	 on OMAP-1510 only
 */

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 */
#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

/*
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus. We do the address translation based on the
 * device overriding the defaults used in the dma-mapping API.
 * Note that the is_lbus_device() test is not very efficient on 1510
 * because of the strncmp().
 */
#ifdef CONFIG_ARCH_OMAP15XX

/*
 * OMAP-1510 Local Bus address offset
 */
#define OMAP1510_LB_OFFSET	UL(0x30000000)

#define virt_to_lbus(x)		((x) - PAGE_OFFSET + OMAP1510_LB_OFFSET)
#define lbus_to_virt(x)		((x) - OMAP1510_LB_OFFSET + PAGE_OFFSET)
#define is_lbus_device(dev)	(cpu_is_omap15xx() && dev && (strncmp(dev->bus_id, "ohci", 4) == 0))

#define __arch_page_to_dma(dev, page)	({is_lbus_device(dev) ? \
					(dma_addr_t)virt_to_lbus(page_address(page)) : \
					(dma_addr_t)__virt_to_bus(page_address(page));})

#define __arch_dma_to_virt(dev, addr)	({is_lbus_device(dev) ? \
					lbus_to_virt(addr) : \
					__bus_to_virt(addr);})

#define __arch_virt_to_dma(dev, addr)	({is_lbus_device(dev) ? \
					virt_to_lbus(addr) : \
					__virt_to_bus(addr);})

#endif	/* CONFIG_ARCH_OMAP15XX */

#endif

