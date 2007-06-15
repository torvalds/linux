/*
 * include/asm-sh/snapgear.h
 *
 * Modified version of io_se.h for the snapgear-specific functions.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for a SnapGear
 */

#ifndef _ASM_SH_IO_SNAPGEAR_H
#define _ASM_SH_IO_SNAPGEAR_H

#if defined(CONFIG_CPU_SH4)
/*
 * The external interrupt lines, these take up ints 0 - 15 inclusive
 * depending on the priority for the interrupt.  In fact the priority
 * is the interrupt :-)
 */

#define IRL0_IRQ		2
#define IRL0_IPR_POS	3
#define IRL0_PRIORITY	13

#define IRL1_IRQ		5
#define IRL1_IPR_POS	2
#define IRL1_PRIORITY	10

#define IRL2_IRQ		8
#define IRL2_IPR_POS	1
#define IRL2_PRIORITY	7

#define IRL3_IRQ		11
#define IRL3_IPR_POS	0
#define IRL3_PRIORITY	4
#endif

#define __IO_PREFIX	snapgear
#include <asm/io_generic.h>

#ifdef CONFIG_SH_SECUREEDGE5410
/*
 * We need to remember what was written to the ioport as some bits
 * are shared with other functions and you cannot read back what was
 * written :-|
 *
 * Bit        Read                   Write
 * -----------------------------------------------
 * D0         DCD on ttySC1          power
 * D1         Reset Switch           heatbeat
 * D2         ttySC0 CTS (7100)      LAN
 * D3         -                      WAN
 * D4         ttySC0 DCD (7100)      CONSOLE
 * D5         -                      ONLINE
 * D6         -                      VPN
 * D7         -                      DTR on ttySC1
 * D8         -                      ttySC0 RTS (7100)
 * D9         -                      ttySC0 DTR (7100)
 * D10        -                      RTC SCLK
 * D11        RTC DATA               RTC DATA
 * D12        -                      RTS RESET
 */

#define SECUREEDGE_IOPORT_ADDR ((volatile short *) 0xb0000000)
extern unsigned short secureedge5410_ioport;

#define SECUREEDGE_WRITE_IOPORT(val, mask) (*SECUREEDGE_IOPORT_ADDR = \
	 (secureedge5410_ioport = \
			((secureedge5410_ioport & ~(mask)) | ((val) & (mask)))))
#define SECUREEDGE_READ_IOPORT() \
	 ((*SECUREEDGE_IOPORT_ADDR&0x0817) | (secureedge5410_ioport&~0x0817))
#endif

#endif /* _ASM_SH_IO_SNAPGEAR_H */
