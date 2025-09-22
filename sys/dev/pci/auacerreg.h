/*	$OpenBSD: auacerreg.h,v 1.2 2022/01/09 05:42:45 jsg Exp $	*/
/*	$NetBSD: auacer.h,v 1.1 2004/10/10 16:37:07 augustss Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_AUACERREG_H_
#define	_DEV_PCI_AUACERREG_H_

#define	ALI_SCR		0x00	/* System Control Register */
#define   ALI_SCR_RESET		(1<<31)	/* master reset */
#define   ALI_SCR_AC97_DBL	(1<<30)
#define   ALI_SCR_CODEC_SPDF	(3<<20)	/* 1=7/8, 2=6/9, 3=10/11 */
#define   ALI_SCR_IN_BITS	(3<<18)
#define   ALI_SCR_OUT_BITS	(3<<16)
#define   ALI_SCR_6CH_CFG	(3<<14)
#define   ALI_SCR_PCM_4		(1<<8)
#define   ALI_SCR_PCM_6		(2<<8)
#define   ALI_SCR_PCM_246_MASK	(ALI_SCR_PCM_4 | ALI_SCR_PCM_6)
#define	ALI_SSR		0x04	/* System Status Register  */
#define   ALI_SSR_SEC_ID	(3<<5)
#define   ALI_SSR_PRI_ID	(3<<3)
#define	ALI_DMACR	0x08	/* DMA Control Register */
#define   ALI_DMACR_PAUSE 16	/* offset for pause bits */
#define	ALI_FIFOCR1	0x0c	/* FIFO Control Register 1 */
#define	ALI_INTERFACECR	0x10	/* Interface Control Register */
#define	ALI_INTERRUPTCR	0x14	/* Interrupt Control Register */
#define	ALI_INTERRUPTSR	0x18	/* Interrupt Status Register */
#define   ALI_INT_MICIN2		(1<<26)
#define   ALI_INT_PCMIN2		(1<<25)
#define   ALI_INT_I2SIN			(1<<24)
#define   ALI_INT_SPDIFOUT		(1<<23)
#define   ALI_INT_SPDIFIN		(1<<22)
#define   ALI_INT_LFEOUT		(1<<21)
#define   ALI_INT_CENTEROUT		(1<<20)
#define   ALI_INT_CODECSPDIFOUT		(1<<19)
#define   ALI_INT_MICIN			(1<<18)
#define   ALI_INT_PCMOUT		(1<<17)
#define   ALI_INT_PCMIN			(1<<16)
#define   ALI_INT_CPRAIS		(1<<7)
#define   ALI_INT_SPRAIS		(1<<5)
#define   ALI_INT_GPIO			(1<<1)
#define	ALI_FIFOCR2	0x1c	/* FIFO Control Register 2 */
#define	ALI_CPR		0x20	/* Command Port Register */
#define   ALI_CPR_ADDR_SECONDARY	0x100
#define   ALI_CPR_ADDR_READ		0x80
#define	ALI_CPR_ADDR	0x22	/* AC97 write addr */
#define	ALI_SPR		0x24	/* Status Port Register */
#define	ALI_SPR_ADDR	0x26	/* AC97 read addr */
#define	ALI_FIFOCR3	0x2c	/* FIFO Control Register 3 */
#define	ALI_TTSR	0x30	/* Transmit Tag Slot Register */
#define	ALI_RTSR	0x34	/* Receive Tag Slot  Register */
#define	ALI_CSPSR	0x38	/* Command/Status Port Status Register */
#define   ALI_CSPSR_CODEC_READY	0x08
#define   ALI_CSPSR_READ_OK	0x02
#define   ALI_CSPSR_WRITE_OK	0x01
#define	ALI_CAS		0x3c	/* Codec Write Semaphore Register */
#define   ALI_CAS_SEM_BUSY	0x80000000
#define ALI_HWVOL	0xf0	/* hardware volume control/status */
#define ALI_I2SCR	0xf4	/* I2S control/status */
#define	ALI_SPDIFCSR	0xf8	/* SPDIF Channel Status Register  */
#define	ALI_SPDIFICS	0xfc	/* SPDIF Interface Control/Status  */


#define ALI_OFF_BDBAR	0x00	/* Buffer Descriptor list Base Address */
#define ALI_OFF_CIV	0x04	/* Current Index Value */
#define ALI_OFF_LVI	0x05	/* Last Valid Index */
#define   ALI_LVI_MASK	0x1f
#define ALI_OFF_SR	0x06	/* Status Register */
#define   ALI_SR_DMA_INT_FIFO	  (1<<4) /* fifo under/over flow */
#define   ALI_SR_DMA_INT_COMPLETE (1<<3) /* buffer read/write complete and ioc set */
#define   ALI_SR_DMA_INT_LVI	  (1<<2) /* last valid done */
#define   ALI_SR_DMA_INT_CELV	  (1<<1) /* last valid is current */
#define   ALI_SR_DMA_INT_DCH	  (1<<0) /* DMA Controller Halted (happens on LVI interrupts) */
#define   ALI_SR_W1TC (ALI_SR_DMA_INT_LVI | ALI_SR_DMA_INT_COMPLETE | ALI_SR_DMA_INT_FIFO | ALI_SR_DMA_INT_CELV)
#define ALI_OFF_PICB	0x08	/* Position In Current Buffer */
#define	ALI_PIV		0x0a	/* 5 bits prefetched index value */
#define ALI_OFF_CR	0x0b	/* Control Register */
#define   ALI_CR_IOCE	0x10	/* Int On Completion Enable */
#define	  ALI_CR_FEIE	0x08	/* Fifo Error Int Enable */
#define	  ALI_CR_LVBIE	0x04	/* Last Valid Buf Int Enable */
#define	  ALI_CR_RR	0x02	/* 1 - Reset Regs */
#define	  ALI_CR_RPBM	0x01	/* 1 - Run, 0 - Pause */

#define ALI_BASE_PI		0x40	/* PCM In */
#define ALI_BASE_PO		0x50	/* PCM Out */
#define ALI_BASE_MC		0x60	/* Mic In */
#define ALI_BASE_CODEC_SPDIFO	0x70	/* Codec SPDIF Out  */
#define ALI_BASE_CENTER		0x80	/* Center out */
#define ALI_BASE_LFE		0x90	/* ? */
#define ALI_BASE_CTL_SPDIFI	0xa0	/* Controller SPDIF In */
#define ALI_BASE_CTL_SPDIFO	0xb0	/* Controller SPDIF Out */

#define ALI_PORT2SLOT(port) (((port) - 0x40) / 0x10)
#define ALI_PORT2INTR(port) (ALI_PORT2SLOT(port) + 16)

#define ALI_IF_AC97SP		(1<<21)
#define ALI_IF_MC		(1<<20)
#define ALI_IF_PI		(1<<19)
#define ALI_IF_MC2		(1<<18)
#define ALI_IF_PI2		(1<<17)
#define ALI_IF_LINE_SRC		(1<<15)	/* 0/1 = slot 3/6 */
#define ALI_IF_MIC_SRC		(1<<14)	/* 0/1 = slot 3/6 */
#define ALI_IF_SPDF_SRC		(3<<12)	/* 00 = PCM, 01 = AC97-in, 10 = spdif-in, 11 = i2s */
#define ALI_IF_AC97_OUT	(3<<8)	/* 00 = PCM, 10 = spdif-in, 11 = i2s */
#define ALI_IF_PO_SPDF	(1<<3)
#define ALI_IF_PO		(1<<1)

#define ALI_INT_MASK		(ALI_INT_SPDIFOUT|ALI_INT_CODECSPDIFOUT|ALI_INT_MICIN|ALI_INT_PCMOUT|ALI_INT_PCMIN)

#define ALI_SAMPLE_SIZE 2


/*
 * according to the dev/audiovar.h AU_RING_SIZE is 2^16, what fits
 * in our limits perfectly, i.e. setting it to higher value
 * in your kernel config would improve performance, still 2^21 is the max
 */
#define	ALI_DMALIST_MAX	32
#define	ALI_DMASEG_MAX	(65536*2)	/* 64k samples, 2x16 bit samples */
struct auacer_dmalist {
	u_int32_t	base;
	u_int32_t	len;
#define	ALI_DMAF_IOC	0x80000000	/* 1-int on complete */
#define	ALI_DMAF_BUP	0x40000000	/* 0-retrans last, 1-transmit 0 */
};

#endif /* _DEV_PCI_AUACERREG_H_ */

