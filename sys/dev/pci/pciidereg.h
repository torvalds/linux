/*	$OpenBSD: pciidereg.h,v 1.10 2013/11/26 20:33:17 deraadt Exp $	*/
/*	$NetBSD: pciidereg.h,v 1.6 2000/11/14 18:42:58 thorpej Exp $	*/

/*
 * Copyright (c) 1998 Christopher G. Demetriou.  All rights reserved.
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

#ifndef _DEV_PCI_PCIIDEREG_H_
#define _DEV_PCI_PCIIDEREG_H_

/*
 * PCI IDE controller register definitions.
 *
 * Author: Christopher G. Demetriou, March 2, 1998.
 *
 * See "PCI IDE Controller Specification, Revision 1.0 3/4/94" and
 * "Programming Interface for Bus Master IDE Controller, Revision 1.0
 * 5/16/94" from the PCI SIG.
 */

/*
 * Number of channels per chip.  MUST NOT CHANGE (macros in pciide.c and
 * this file depend on its value).
 */
#define	PCIIDE_NUM_CHANNELS		2

/*
 * PCI base address register locations (some are per-channel).
 */
#define	PCIIDE_REG_CMD_BASE(chan)	(0x10 + (8 * (chan)))
#define	PCIIDE_REG_CTL_BASE(chan)	(0x14 + (8 * (chan)))
#define	PCIIDE_REG_BUS_MASTER_DMA	0x20

/*
 * Bits in the PCI Programming Interface register (some are per-channel).
 * Bits 6-4 are defined as read-only in PCI 2.1 specification.
 * Microsoft proposed to use these bits for independent channels
 * enable/disable. This feature is enabled based on the value of bit 6.
 */
#define PCIIDE_CHANSTATUS_EN		0x40
#define PCIIDE_CHAN_EN(chan)		(0x20 >> (chan))
#define	PCIIDE_INTERFACE_PCI(chan)	(0x01 << (2 * (chan)))
#define	PCIIDE_INTERFACE_SETTABLE(chan)	(0x02 << (2 * (chan)))
#define	PCIIDE_INTERFACE_BUS_MASTER_DMA	0x80

/*
 * Compatibility address/IRQ definitions (some are per-channel).
 */
#define	PCIIDE_COMPAT_CMD_BASE(chan)	((chan) == 0 ? 0x1f0 : 0x170)
#define	PCIIDE_COMPAT_CMD_SIZE		8
#define	PCIIDE_COMPAT_CTL_BASE(chan)	((chan) == 0 ? 0x3f6 : 0x376)
#define	PCIIDE_COMPAT_CTL_SIZE		1
#define	PCIIDE_COMPAT_IRQ(chan)		((chan) == 0 ? 14 : 15)

#define	PCIIDE_CHANNEL_NAME(chan)	((chan) == 0 ? "channel 0" : "channel 1")

/*
 * definitions for IDE DMA
 * XXX maybe this should go elsewhere
 */

/* secondary channel registers offset */
#define IDEDMA_SCH_OFFSET 0x08
#define IDEDMA_NREGS 8

/* Bus master command register (per channel) */
#define IDEDMA_CMD(chan) (0x00 + IDEDMA_SCH_OFFSET * (chan))
#define IDEDMA_CMD_WRITE 0x08
#define IDEDMA_CMD_START 0x01

/* Bus master status register (per channel) */
#define IDEDMA_CTL(chan) (0x02 + IDEDMA_SCH_OFFSET * (chan))
#define IDEDMA_CTL_DRV_DMA(d)	(0x20 << (d))
#define IDEDMA_CTL_INTR		0x04
#define IDEDMA_CTL_ERR		0x02
#define IDEDMA_CTL_ACT		0x01

/* Bus master table pointer register (per channel) */
#define IDEDMA_TBL(chan) (0x04 + IDEDMA_SCH_OFFSET * (chan))
#define IDEDMA_TBL_MASK 0xfffffffc
#define IDEDMA_TBL_ALIGN 0x00010000

/* bus master table descriptor */
struct idedma_table {
	u_int32_t base_addr; /* physical base addr of memory region */
	u_int32_t byte_count; /* memory region length */
#define IDEDMA_BYTE_COUNT_MASK 0x0000FFFF
#define IDEDMA_BYTE_COUNT_EOT  0x80000000
};

#define IDEDMA_BYTE_COUNT_MAX 0x00010000 /* Max I/O per table */
#define IDEDMA_BYTE_COUNT_ALIGN 0x00010000

/* Number of idedma table needed */
#define NIDEDMA_TABLES (MAXPHYS/PAGE_SIZE + 1)

/* Intel SCH */
#define SCH_D0TIM	0x80
#define SCH_D1TIM	0x84
#define SCH_TIM_UDMA	0x70000
#define SCH_TIM_MDMA	0x00300
#define SCH_TIM_PIO	0x00007
#define SCH_TIM_SYNCDMA	(1U << 31)

#define SCH_TIM_MASK	(SCH_TIM_UDMA | SCH_TIM_MDMA | SCH_TIM_PIO)

#endif	/* !_DEV_PCI_PCIIDEREG_H_ */
