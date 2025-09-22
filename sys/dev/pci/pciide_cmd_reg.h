/*	$OpenBSD: pciide_cmd_reg.h,v 1.12 2024/09/06 10:54:08 jsg Exp $	*/
/*	$NetBSD: pciide_cmd_reg.h,v 1.9 2000/08/02 20:23:46 bouyer Exp $	*/

/*
 * Copyright (c) 1998 Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 */

#ifndef _DEV_PCI_PCIIDE_CMD_REG_H_
#define _DEV_PCI_PCIIDE_CMD_REG_H_

/*
 * Registers definitions for CMD Technologies's PCI 064x IDE controllers.
 */

/* Interesting revision of the 0646 */
#define CMD0646U2_REV 0x05
#define CMD0646U_REV 0x03

/* Configuration (RO) */
#define CMD_CONF 0x50
#define CMD_CONF_REV_MASK	0x03 /* 0640/3/6 only */
#define CMD_CONF_DRV0_INTR	0x04
#define CMD_CONF_DEVID		0x18 /* 0640/3/6 only */
#define CMD_CONF_VESAPRT	0x20 /* 0640/3/6 only */
#define CMD_CONF_DSA1		0x40
#define CMD_CONF_DSA0		0x80 /* 0640/3/6 only */

/* Control register (RW) */
#define CMD_CTRL 0x51
#define CMD_CTRL_HR_FIFO		0x01 /* 0640/3/6 only */
#define CMD_CTRL_HW_FIFO		0x02 /* 0640/3/6 only */
#define CMD_CTRL_DEVSEL			0x04
#define CMD_CTRL_2PORT			0x08
#define CMD_CTRL_PAR			0x10 /* 0640/3/6 only */
#define CMD_CTRL_HW_HLD			0x20 /* 0640/3/6 only */
#define CMD_CTRL_DRV0_RAHEAD		0x40
#define CMD_CTRL_DRV1_RAHEAD		0x80

/*
 * data read/write timing registers . 0640 uses the same for drive 0 and 1
 * on the secondary channel
 */
#define CMD_DATA_TIM(chan, drive) \
	(((chan) == 0) ? \
		((drive) == 0) ? 0x54: 0x56 \
		: \
		((drive) == 0) ? 0x58 : 0x5b)

/* secondary channel status and addr timings */
#define CMD_ARTTIM23	0x57
#define CMD_ARTTIM23_IRQ	0x10
#define CMD_ARTTIM23_RHAEAD(d)	((0x4) << (d))

/* DMA master read mode select */
#define CMD_DMA_MODE 0x71
#define CMD_DMA_MASK		0x03
#define CMD_DMA			0x00
#define CMD_DMA_MULTIPLE	0x01
#define CMD_DMA_LINE		0x03
/* the following bits are only for 0646U/646U2/648/649 */
#define CMD_DMA_IRQ(chan) 	(0x4 << (chan))
#define CMD_DMA_IRQ_DIS(chan) 	(0x10 << (chan))
#define CMD_DMA_RST		0x40

/* the following is only for 0646U/646U2/648/649 */
/* busmaster control/status register */
#define CMD_BICSR	0x79
#define CMD_BICSR_80(chan)	(0x01 << (chan))
/* Ultra/DMA timings reg */
#define CMD_UDMATIM(channel)	(0x73 + (8 * (channel)))
#define CMD_UDMATIM_UDMA(drive)	(0x01 << (drive))
#define CMD_UDMATIM_UDMA33(drive) (0x04 << (drive))
#define CMD_UDMATIM_TIM_MASK	0x3
#define CMD_UDMATIM_TIM_OFF(drive) (4 + ((drive) * 2))
static int8_t cmd0646_9_tim_udma[] = {0x03, 0x02, 0x01, 0x02, 0x01, 0x00};

/*
 * timings values for the 0643/6/8/9
 * for all dma_mode we have to have
 * DMA_timings(dma_mode) >= PIO_timings(dma_mode + 2)
 */
static int8_t cmd0643_9_data_tim_pio[] = {0xA9, 0x57, 0x44, 0x32, 0x3F};
static int8_t cmd0643_9_data_tim_dma[] = {0x87, 0x32, 0x3F};

#endif	/* !_DEV_PCI_PCIIDE_CMD_REG_H_ */
