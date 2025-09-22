/*	$OpenBSD: plx9060reg.h,v 1.2 2005/11/21 21:52:47 miod Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2000 Zembu Labs, Inc.
 * All rights reserved.
 *
 * Author: Jason R. Thorpe <thorpej@zembu.com>
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
 *	This product includes software developed by Zembu Labs, Inc.
 * 4. Neither the name of Zembu Labs nor the names of its employees may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ZEMBU LABS, INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAR-
 * RANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DIS-
 * CLAIMED.  IN NO EVENT SHALL ZEMBU LABS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register description for the PLX 9060-family of PCI bus
 * controllers.
 *
 * In order for this file to be really useful to you, you'll want to
 * have the PLX 9060 datasheet in front of you.
 */

#ifndef _DEV_PCI_PLX9060REG_H_
#define	_DEV_PCI_PLX9060REG_H_

/*
 * PLX 9060 PCI configuration space registers.
 */

#define	PLX_PCI_RUNTIME_MEMADDR	0x10	/* memory mapped 9060 */
#define	PLX_PCI_RUNTIME_IOADDR	0x14	/* i/o mapped 9060 */
#define	PLX_PCI_LOCAL_ADDR0	0x18	/* PCI address of 9060 local bus */

/*
 * PLX 9060 Runtime registers, in PCI space.
 */

/* Local Address Space 0 Range Register */
#define	PLX_LAS0RR		0x00
#define	LASRR_IO		0x00000001
#define	LASRR_MEM_1M		0x00000002
#define	LASRR_MEM_64BIT		0x00000004
#define	LASRR_MEM_PREFETCH	0x00000008
#define	LASRR_MEM_MASK		0xfffffff0
#define	LASRR_IO_MASK		0xfffffffc


/* Local Address Space 0 Local Base Address (remap) Register */
#define	PLX_LAS0BA		0x04
#define	LASBA_ENABLE		0x00000001
#define	LASBA_MEM_MASK		0xfffffff0
#define	LASBA_IO_MASK		0xfffffffc


/* Local Arbitration Register */
#define	PLX_LAR			0x08
#define	LAR_LATTMR		0x000000ff
#define	LAR_PAUSETMR		0x0000ff00
#define	LAR_LATTMR_EN		0x00010000
#define	LAR_BREQ_EN		0x00040000
#define	LAR_DSGIVEUP		0x00200000
#define	LAR_DSLOCK_EN		0x00400000
#define	LAR_PCI21_MODE		0x01000000


/* Big/Little Endian Register */
#define	PLX_ENDIAN		0x0c
#define	ENDIAN_CRBE		0x00000001
#define	ENDIAN_DMBE		0x00000002
#define	ENDIAN_DSAS0BE		0x00000004
#define	ENDIAN_DSAERBE		0x00000008
#define	ENDIAN_BEBL		0x00000010


/* Expansion ROM Range Register */
#define	PLX_EROMRR		0x10
#define	EROMRR_MASK		0xffffffc0


/* Expansion ROM Base Address (remap) Register */
#define	PLX_EROMBA		0x14
#define	EROMBA_BREQ_DC		0x0000000f
#define	EROMBA_BREQ_EN		0x00000010
#define	EROMBA_MASK		0xffffffc0


/* Local Bus Region Descriptor for PCI to Local Access Register */
#define	PLX_LBRD		0x18


/* Local Range for Direct Master to PCI */
#define	PLX_DMRR		0x1c


/* Local Bus Base Address for Direct Master to PCI Memory */
#define	PLX_DMLBAM		0x20


/* Local Bus Base Address for Direct Master to PCI IO/Config */
#define	PLX_DMLBAI		0x24


/* PCI Base Address (remap) for Direct Master to PCI Memory */
#define	PLX_DMBPAM		0x28


/* PCI Base Address (remap) for Direct Master to PCI IO/Config */
#define	PLX_DMPBAI		0x2c


#define	PLX_MAILBOX0		0x40	/* Mailbox register 0 */
#define	PLX_MAILBOX1		0x44	/* Mailbox register 1 */
#define	PLX_MAILBOX2		0x48	/* Mailbox register 2 */
#define	PLX_MAILBOX3		0x4c	/* Mailbox register 3 */
#define	PLX_MAILBOX4		0x50	/* Mailbox register 4 (not 9060ES) */
#define	PLX_MAILBOX5		0x54	/* Mailbox register 5 (not 9060ES) */
#define	PLX_MAILBOX6		0x58	/* Mailbox register 6 (not 9060ES) */
#define	PLX_MAILBOX7		0x5c	/* Mailbox register 7 (not 9060ES) */


#define	PLX_PCI_LOCAL_DOORBELL	0x60	/* PCI -> local doorbell */
#define	PLX_LOCAL_PCI_DOORBELL	0x64	/* local -> PCI doorbell */


/* Interrupt Control/Status */
#define	PLX_INTCSR		0x68
#define	INTCSR_LSERR_TAMA	0x00000001
#define	INTCSR_LSERR_PA		0x00000002
#define	INTCSR_SERR		0x00000004
#define	INTCSR_PCI_EN		0x00000100
#define	INTCSR_PCIDB_EN		0x00000200
#define	INTCSR_PCIAB_EN		0x00000400
#define	INTCSR_PCILOC_EN	0x00000800
#define	INTCSR_RETRYAB_EN	0x00001000
#define	INTCSR_PCIDB_INT	0x00002000
#define	INTCSR_PCIAB_INT	0x00004000
#define	INTCSR_PCILOC_INT	0x00008000
#define	INTCSR_LOCOE_EN		0x00010000
#define	INTCSR_LOCDB_EN		0x00020000
#define	INTCSR_LOCDB_INT	0x00100000
#define	INTCSR_BIST_INT		0x00800000
#define	INTCSR_DMAB_INT		0x01000000
#define	INTCSR_RETRYAB_INT	0x08000000


/* EEPROM Control, PCI Command Codes, User I/O Control, Init Control */
#define	PLX_CONTROL		0x6c
#define	CONTROL_PCIMRC		0x00000f00
#define	CONTROL_PCIMRC_SHIFT	8
#define	CONTROL_PCIMWC		0x0000f000
#define	CONTROL_PCIMWC_SHIFT	12
#define	CONTROL_GPO		0x00010000
#define	CONTROL_GPI		0x00020000
#define	CONTROL_EESK		0x01000000
#define	CONTROL_EECS		0x02000000
#define	CONTROL_EEDO		0x04000000	/* PLX -> EEPROM */
#define	CONTROL_EEDI		0x08000000	/* EEPROM -> PLX */
#define	CONTROL_EEPRESENT	0x10000000
#define	CONTROL_RELOADCFG	0x20000000
#define	CONTROL_SWR		0x40000000
#define	CONTROL_LOCALINIT	0x80000000

	/* EEPROM opcodes */
#define	PLX_EEPROM_OPC_READ(x)	(0x0080 | ((x) & 0x3f))
#define	PLX_EEPROM_OPC_WRITE(x)	(0x0040 | ((x) & 0x3f))
#define	PLX_EEPROM_OPC_WREN	0x0030
#define	PLX_EEPROM_OPC_WRPR	0x0000
#define	PLX_EEPROM_COMMAND(y)	(((y) & 0xff) | 0x100)


/* PCI Configuration ID */
#define	PLX_IDREG		0x70

#endif /* _DEV_PCI_PLX9060REG_H_ */
