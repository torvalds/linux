/*
 *  vrc4173.h, Include file for NEC VRC4173.
 *
 *  Copyright (C) 2000  Michael R. McDonald
 *  Copyright (C) 2001-2003 Montavista Software Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com>
 *  Copyright (C) 2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __NEC_VRC4173_H
#define __NEC_VRC4173_H

#include <linux/config.h>
#include <asm/io.h>

/*
 * Interrupt Number
 */
#define VRC4173_IRQ_BASE	72
#define VRC4173_IRQ(x)		(VRC4173_IRQ_BASE + (x))
#define VRC4173_USB_IRQ		VRC4173_IRQ(0)
#define VRC4173_PCMCIA2_IRQ	VRC4173_IRQ(1)
#define VRC4173_PCMCIA1_IRQ	VRC4173_IRQ(2)
#define VRC4173_PS2CH2_IRQ	VRC4173_IRQ(3)
#define VRC4173_PS2CH1_IRQ	VRC4173_IRQ(4)
#define VRC4173_PIU_IRQ		VRC4173_IRQ(5)
#define VRC4173_AIU_IRQ		VRC4173_IRQ(6)
#define VRC4173_KIU_IRQ		VRC4173_IRQ(7)
#define VRC4173_GIU_IRQ		VRC4173_IRQ(8)
#define VRC4173_AC97_IRQ	VRC4173_IRQ(9)
#define VRC4173_AC97INT1_IRQ	VRC4173_IRQ(10)
/* RFU */
#define VRC4173_DOZEPIU_IRQ	VRC4173_IRQ(13)
#define VRC4173_IRQ_LAST	VRC4173_DOZEPIU_IRQ

/*
 * PCI I/O accesses
 */
#ifdef CONFIG_VRC4173

extern unsigned long vrc4173_io_offset;

#define set_vrc4173_io_offset(offset)	do { vrc4173_io_offset = (offset); } while (0)

#define vrc4173_outb(val,port)		outb((val), vrc4173_io_offset+(port))
#define vrc4173_outw(val,port)		outw((val), vrc4173_io_offset+(port))
#define vrc4173_outl(val,port)		outl((val), vrc4173_io_offset+(port))
#define vrc4173_outb_p(val,port)	outb_p((val), vrc4173_io_offset+(port))
#define vrc4173_outw_p(val,port)	outw_p((val), vrc4173_io_offset+(port))
#define vrc4173_outl_p(val,port)	outl_p((val), vrc4173_io_offset+(port))

#define vrc4173_inb(port)		inb(vrc4173_io_offset+(port))
#define vrc4173_inw(port)		inw(vrc4173_io_offset+(port))
#define vrc4173_inl(port)		inl(vrc4173_io_offset+(port))
#define vrc4173_inb_p(port)		inb_p(vrc4173_io_offset+(port))
#define vrc4173_inw_p(port)		inw_p(vrc4173_io_offset+(port))
#define vrc4173_inl_p(port)		inl_p(vrc4173_io_offset+(port))

#define vrc4173_outsb(port,addr,count)	outsb(vrc4173_io_offset+(port),(addr),(count))
#define vrc4173_outsw(port,addr,count)	outsw(vrc4173_io_offset+(port),(addr),(count))
#define vrc4173_outsl(port,addr,count)	outsl(vrc4173_io_offset+(port),(addr),(count))

#define vrc4173_insb(port,addr,count)	insb(vrc4173_io_offset+(port),(addr),(count))
#define vrc4173_insw(port,addr,count)	insw(vrc4173_io_offset+(port),(addr),(count))
#define vrc4173_insl(port,addr,count)	insl(vrc4173_io_offset+(port),(addr),(count))

#else

#define set_vrc4173_io_offset(offset)	do {} while (0)

#define vrc4173_outb(val,port)		do {} while (0)
#define vrc4173_outw(val,port)		do {} while (0)
#define vrc4173_outl(val,port)		do {} while (0)
#define vrc4173_outb_p(val,port)	do {} while (0)
#define vrc4173_outw_p(val,port)	do {} while (0)
#define vrc4173_outl_p(val,port)	do {} while (0)

#define vrc4173_inb(port)		0
#define vrc4173_inw(port)		0
#define vrc4173_inl(port)		0
#define vrc4173_inb_p(port)		0
#define vrc4173_inw_p(port)		0
#define vrc4173_inl_p(port)		0

#define vrc4173_outsb(port,addr,count)	do {} while (0)
#define vrc4173_outsw(port,addr,count)	do {} while (0)
#define vrc4173_outsl(port,addr,count)	do {} while (0)

#define vrc4173_insb(port,addr,count)	do {} while (0)
#define vrc4173_insw(port,addr,count)	do {} while (0)
#define vrc4173_insl(port,addr,count)	do {} while (0)

#endif

/*
 * Clock Mask Unit
 */
typedef enum vrc4173_clock {
	VRC4173_PIU_CLOCK,
	VRC4173_KIU_CLOCK,
	VRC4173_AIU_CLOCK,
	VRC4173_PS2_CH1_CLOCK,
	VRC4173_PS2_CH2_CLOCK,
	VRC4173_USBU_PCI_CLOCK,
	VRC4173_CARDU1_PCI_CLOCK,
	VRC4173_CARDU2_PCI_CLOCK,
	VRC4173_AC97U_PCI_CLOCK,
	VRC4173_USBU_48MHz_CLOCK,
	VRC4173_EXT_48MHz_CLOCK,
	VRC4173_48MHz_CLOCK,
} vrc4173_clock_t;

#ifdef CONFIG_VRC4173

extern void vrc4173_supply_clock(vrc4173_clock_t clock);
extern void vrc4173_mask_clock(vrc4173_clock_t clock);

#else

static inline void vrc4173_supply_clock(vrc4173_clock_t clock) {}
static inline void vrc4173_mask_clock(vrc4173_clock_t clock) {}

#endif

/*
 * Interupt Control Unit
 */

#define VRC4173_PIUINT_COMMAND		0x0040
#define VRC4173_PIUINT_DATA		0x0020
#define VRC4173_PIUINT_PAGE1		0x0010
#define VRC4173_PIUINT_PAGE0		0x0008
#define VRC4173_PIUINT_DATALOST		0x0004
#define VRC4173_PIUINT_STATUSCHANGE	0x0001

#ifdef CONFIG_VRC4173

extern void vrc4173_enable_piuint(uint16_t mask);
extern void vrc4173_disable_piuint(uint16_t mask);

#else

static inline void vrc4173_enable_piuint(uint16_t mask) {}
static inline void vrc4173_disable_piuint(uint16_t mask) {}

#endif

#define VRC4173_AIUINT_INPUT_DMAEND	0x0800
#define VRC4173_AIUINT_INPUT_DMAHALT	0x0400
#define VRC4173_AIUINT_INPUT_DATALOST	0x0200
#define VRC4173_AIUINT_INPUT_DATA	0x0100
#define VRC4173_AIUINT_OUTPUT_DMAEND	0x0008
#define VRC4173_AIUINT_OUTPUT_DMAHALT	0x0004
#define VRC4173_AIUINT_OUTPUT_NODATA	0x0002

#ifdef CONFIG_VRC4173

extern void vrc4173_enable_aiuint(uint16_t mask);
extern void vrc4173_disable_aiuint(uint16_t mask);

#else

static inline void vrc4173_enable_aiuint(uint16_t mask) {}
static inline void vrc4173_disable_aiuint(uint16_t mask) {}

#endif

#define VRC4173_KIUINT_DATALOST		0x0004
#define VRC4173_KIUINT_DATAREADY	0x0002
#define VRC4173_KIUINT_SCAN		0x0001

#ifdef CONFIG_VRC4173

extern void vrc4173_enable_kiuint(uint16_t mask);
extern void vrc4173_disable_kiuint(uint16_t mask);

#else

static inline void vrc4173_enable_kiuint(uint16_t mask) {}
static inline void vrc4173_disable_kiuint(uint16_t mask) {}

#endif

/*
 * General-Purpose I/O Unit
 */
typedef enum vrc4173_function {
	PS2_CHANNEL1,
	PS2_CHANNEL2,
	TOUCHPANEL,
	KEYBOARD_8SCANLINES,
	KEYBOARD_10SCANLINES,
	KEYBOARD_12SCANLINES,
	GPIO_0_15PINS,
	GPIO_16_20PINS,
} vrc4173_function_t;

#ifdef CONFIG_VRC4173

extern void vrc4173_select_function(vrc4173_function_t function);

#else

static inline void vrc4173_select_function(vrc4173_function_t function) {}

#endif

#endif /* __NEC_VRC4173_H */
