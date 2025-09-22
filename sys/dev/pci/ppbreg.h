/*	$OpenBSD: ppbreg.h,v 1.6 2020/05/23 07:58:24 patrick Exp $	*/
/*	$NetBSD: ppbreg.h,v 1.3 2001/07/06 18:07:16 mcr Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI-PCI Bridge chip register definitions and macros.
 * Derived from information found in the ``PCI to PCI Bridge
 * Architecture Specification, Revision 1.0, April 5, 1994.''
 *
 * XXX much is missing.
 */

/*
 * PCI Programming Interface register.
 */
#define	PPB_INTERFACE_SUBTRACTIVE	0x01

/*
 * Register offsets
 */
#define	PPB_REG_BASE0		0x10		/* Base Addr Reg. 0 */
#define	PPB_REG_BASE1		0x14		/* Base Addr Reg. 1 */
#define	PPB_REG_BUSINFO		0x18		/* Bus information */
#define	PPB_REG_IOSTATUS	0x1c		/* I/O base+lim & sec stat */
#define	PPB_REG_MEM		0x20		/* Memory base/limit */
#define	PPB_REG_PREFMEM		0x24		/* Pref Mem  base/limit */
#define	PPB_REG_PREFBASE_HI32	0x28		/* Pref Mem base high bits */
#define	PPB_REG_PREFLIM_HI32	0x2c		/* Pref Mem lim high bits */
#define	PPB_REG_IO_HI		0x30		/* I/O base+lim high bits */
#define	PPB_REG_BRIDGECONTROL	0x3c		/* bridge control register */

/*
 * Macros to extract the contents of the "Bus Info" register.
 */
#define	PPB_BUSINFO_PRIMARY(bir)					\
	    ((bir >>  0) & 0xff)
#define	PPB_BUSINFO_SECONDARY(bir)					\
	    ((bir >>  8) & 0xff)
#define	PPB_BUSINFO_SUBORDINATE(bir)					\
	    ((bir >> 16) & 0xff)
#define	PPB_BUSINFO_SECLAT(bir)						\
	    ((bir >> 24) & 0xff)

/*
 * Routine to translate between secondary bus interrupt pin/device number and
 * primary bus interrupt pin number.
 */
#define	PPB_INTERRUPT_SWIZZLE(pin, device)				\
	    ((((pin) + (device) - 1) % 4) + 1)

/*
 * secondary bus I/O base and limits
 */
#define PPB_IOBASE_SHIFT   0
#define PPB_IOLIMIT_SHIFT  8
#define PPB_IO_MASK   0xf000
#define PPB_IO_32BIT  0x0001
#define PPB_IO_SHIFT  8
#define PPB_IO_MIN    4096

/*
 * secondary bus memory base and limits
 */
#define PPB_MEMBASE_SHIFT  0
#define PPB_MEMLIMIT_SHIFT 16
#define PPB_MEM_MASK   0xfff00000
#define PPB_MEM_SHIFT  16
#define PPB_MEM_MIN    0x00100000

/* 
 * bridge control register (see table 3.9 of ppb rev. 1.1)
 *
 * Note these are in the *upper* 16 bits of the Bridge Control
 * Register (the bottom 16 are Interrupt Line and Interrupt Pin).
 */
#define	PPB_BC_BITBASE			   16

#define PPB_BC_PARITYERRORRESPONSE_ENABLE  (1U << (0 + PPB_BC_BITBASE))
#define PPB_BC_SERR_ENABLE                 (1U << (1 + PPB_BC_BITBASE))
#define PPB_BC_ISA_ENABLE                  (1U << (2 + PPB_BC_BITBASE))
#define PPB_BC_VGA_ENABLE                  (1U << (3 + PPB_BC_BITBASE))
#define PPB_BC_MASTER_ABORT_MODE           (1U << (5 + PPB_BC_BITBASE))
#define PPB_BC_SECONDARY_RESET             (1U << (6 + PPB_BC_BITBASE))
#define	PPB_BC_FAST_B2B_ENABLE		   (1U << (7 + PPB_BC_BITBASE))
	/* PCI 2.2 */
#define	PPB_BC_PRIMARY_DISCARD_TIMEOUT	   (1U << (8 + PPB_BC_BITBASE))
#define	PPB_BC_SECONDARY_DISCARD_TIMEOUT   (1U << (9 + PPB_BC_BITBASE))
#define	PPB_BC_DISCARD_TIMER_STATUS	   (1U << (10 + PPB_BC_BITBASE))
#define	PPB_BC_DISCARD_TIMER_SERR_ENABLE   (1U << (11 + PPB_BC_BITBASE))
