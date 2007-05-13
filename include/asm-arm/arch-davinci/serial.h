/*
 * DaVinci serial device definitions
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <asm/arch/io.h>

#define DAVINCI_UART0_BASE   (IO_PHYS + 0x20000)
#define DAVINCI_UART1_BASE   (IO_PHYS + 0x20400)
#define DAVINCI_UART2_BASE   (IO_PHYS + 0x20800)

#endif /* __ASM_ARCH_SERIAL_H */
