/*
 * linux/include/asm-arm/arch-omap/io.h
 *
 * IO definitions for TI OMAP processors and boards
 *
 * Copied from linux/include/asm-arm/arch-sa1100/io.h
 * Copyright (C) 1997-1999 Russell King
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
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)			((void __iomem *)(PCIO_BASE + (a)))
#define __mem_pci(a)		(a)
#define __mem_isa(a)		(a)

/*
 * ----------------------------------------------------------------------------
 * I/O mapping
 * ----------------------------------------------------------------------------
 */
#define IO_PHYS			0xFFFB0000
#define IO_OFFSET		0x01000000	/* Virtual IO = 0xfefb0000 */
#define IO_VIRT			(IO_PHYS - IO_OFFSET)
#define IO_SIZE			0x40000
#define IO_ADDRESS(x)		((x) - IO_OFFSET)

#define PCIO_BASE		0

#define io_p2v(x)               ((x) - IO_OFFSET)
#define io_v2p(x)               ((x) + IO_OFFSET)

#ifndef __ASSEMBLER__

/*
 * Functions to access the OMAP IO region
 *
 * NOTE: - Use omap_read/write[bwl] for physical register addresses
 *	 - Use __raw_read/write[bwl]() for virtual register addresses
 *	 - Use IO_ADDRESS(phys_addr) to convert registers to virtual addresses
 *	 - DO NOT use hardcoded virtual addresses to allow changing the
 *	   IO address space again if needed
 */
#define omap_readb(a)		(*(volatile unsigned char  *)IO_ADDRESS(a))
#define omap_readw(a)		(*(volatile unsigned short *)IO_ADDRESS(a))
#define omap_readl(a)		(*(volatile unsigned int   *)IO_ADDRESS(a))

#define omap_writeb(v,a)	(*(volatile unsigned char  *)IO_ADDRESS(a) = (v))
#define omap_writew(v,a)	(*(volatile unsigned short *)IO_ADDRESS(a) = (v))
#define omap_writel(v,a)	(*(volatile unsigned int   *)IO_ADDRESS(a) = (v))

/* 16 bit uses LDRH/STRH, base +/- offset_8 */
typedef struct { volatile u16 offset[256]; } __regbase16;
#define __REGV16(vaddr)		((__regbase16 *)((vaddr)&~0xff)) \
					->offset[((vaddr)&0xff)>>1]
#define __REG16(paddr)          __REGV16(io_p2v(paddr))

/* 8/32 bit uses LDR/STR, base +/- offset_12 */
typedef struct { volatile u8 offset[4096]; } __regbase8;
#define __REGV8(vaddr)		((__regbase8  *)((vaddr)&~4095)) \
					->offset[((vaddr)&4095)>>0]
#define __REG8(paddr)		__REGV8(io_p2v(paddr))

typedef struct { volatile u32 offset[4096]; } __regbase32;
#define __REGV32(vaddr)		((__regbase32 *)((vaddr)&~4095)) \
					->offset[((vaddr)&4095)>>2]
#define __REG32(paddr)		__REGV32(io_p2v(paddr))

#else

#define __REG8(paddr)		io_p2v(paddr)
#define __REG16(paddr)		io_p2v(paddr)
#define __REG32(paddr)		io_p2v(paddr)

#endif

#endif
