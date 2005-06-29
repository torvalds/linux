/*
 *  linux/include/asm-arm/serial.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   15-10-1996	RMK	Created
 */

#ifndef __ASM_SERIAL_H
#define __ASM_SERIAL_H

#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#if defined(CONFIG_ARCH_A5K)
     /* UART CLK        PORT  IRQ     FLAGS        */

#define SERIAL_PORT_DFNS                                                \
        { 0, BASE_BAUD, 0x3F8, 10, STD_COM_FLAGS },     /* ttyS0 */     \
        { 0, BASE_BAUD, 0x2F8, 10, STD_COM_FLAGS },     /* ttyS1 */

#else

#define SERIAL_PORT_DFNS                                                \
        { 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },     /* ttyS0 */     \
        { 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },     /* ttyS1 */

#endif

#endif
