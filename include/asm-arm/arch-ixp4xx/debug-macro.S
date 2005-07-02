/* linux/include/asm-arm/arch-ixp4xx/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

                .macro  addruart,rx
                mrc     p15, 0, \rx, c1, c0
                tst     \rx, #1                 @ MMU enabled?
                moveq   \rx, #0xc8000000
                movne   \rx, #0xff000000
		orrne	\rx, \rx, #0x00b00000
                add     \rx,\rx,#3              @ Uart regs are at off set of 3 if
						@ byte writes used - Big Endian.
                .endm

               .macro  senduart,rd,rx
                strb    \rd, [\rx]
                .endm

                .macro  waituart,rd,rx
1002:           ldrb    \rd, [\rx, #0x14]
                and     \rd, \rd, #0x60		@ check THRE and TEMT bits
                teq     \rd, #0x60
                bne     1002b
                .endm

                .macro  busyuart,rd,rx
                .endm
