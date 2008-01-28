/*
 * linux/include/asm-arm/arch-orion/debug-macro.S
 *
 * Debugging macro include header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

	.macro  addruart,rx
	mov   \rx, #0xf1000000
	orr   \rx, \rx, #0x00012000
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
