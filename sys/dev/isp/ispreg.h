/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 1997-2009 by Matthew Jacob
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 *  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 * 
 */
/*
 * Machine Independent (well, as best as possible) register
 * definitions for Qlogic ISP SCSI adapters.
 */
#ifndef	_ISPREG_H
#define	_ISPREG_H

/*
 * Hardware definitions for the Qlogic ISP  registers.
 */

/*
 * This defines types of access to various registers.
 *
 *  	R:		Read Only
 *	W:		Write Only
 *	RW:		Read/Write
 *
 *	R*, W*, RW*:	Read Only, Write Only, Read/Write, but only
 *			if RISC processor in ISP is paused.
 */

/*
 * Offsets for various register blocks.
 *
 * Sad but true, different architectures have different offsets.
 *
 * Don't be alarmed if none of this makes sense. The original register
 * layout set some defines in a certain pattern. Everything else has been
 * grafted on since. For example, the ISP1080 manual will state that DMA
 * registers start at 0x80 from the base of the register address space.
 * That's true, but for our purposes, we define DMA_REGS_OFF for the 1080
 * to start at offset 0x60 because the DMA registers are all defined to
 * be DMA_BLOCK+0x20 and so on. Clear?
 */

#define	BIU_REGS_OFF			0x00

#define	PCI_MBOX_REGS_OFF		0x70
#define	PCI_MBOX_REGS2100_OFF		0x10
#define	PCI_MBOX_REGS2300_OFF		0x40
#define	PCI_MBOX_REGS2400_OFF		0x80
#define	SBUS_MBOX_REGS_OFF		0x80

#define	PCI_SXP_REGS_OFF		0x80
#define	SBUS_SXP_REGS_OFF		0x200

#define	PCI_RISC_REGS_OFF		0x80
#define	SBUS_RISC_REGS_OFF		0x400

/* Bless me! Chip designers have putzed it again! */
#define	ISP1080_DMA_REGS_OFF		0x60
#define	DMA_REGS_OFF			0x00	/* same as BIU block */

#define	SBUS_REGSIZE			0x450
#define	PCI_REGSIZE			0x100

/*
 * NB:	The *_BLOCK definitions have no specific hardware meaning.
 *	They serve simply to note to the MD layer which block of
 *	registers offsets are being accessed.
 */
#define	_NREG_BLKS	5
#define	_BLK_REG_SHFT	13
#define	_BLK_REG_MASK	(7 << _BLK_REG_SHFT)
#define	BIU_BLOCK	(0 << _BLK_REG_SHFT)
#define	MBOX_BLOCK	(1 << _BLK_REG_SHFT)
#define	SXP_BLOCK	(2 << _BLK_REG_SHFT)
#define	RISC_BLOCK	(3 << _BLK_REG_SHFT)
#define	DMA_BLOCK	(4 << _BLK_REG_SHFT)

/*
 * Bus Interface Block Register Offsets
 */

#define	BIU_ID_LO	(BIU_BLOCK+0x0)		/* R  : Bus ID, Low */
#define		BIU2100_FLASH_ADDR	(BIU_BLOCK+0x0)
#define	BIU_ID_HI	(BIU_BLOCK+0x2)		/* R  : Bus ID, High */
#define		BIU2100_FLASH_DATA	(BIU_BLOCK+0x2)
#define	BIU_CONF0	(BIU_BLOCK+0x4)		/* R  : Bus Configuration #0 */
#define	BIU_CONF1	(BIU_BLOCK+0x6)		/* R  : Bus Configuration #1 */
#define		BIU2100_CSR		(BIU_BLOCK+0x6)
#define	BIU_ICR		(BIU_BLOCK+0x8)		/* RW : Bus Interface Ctrl */
#define	BIU_ISR		(BIU_BLOCK+0xA)		/* R  : Bus Interface Status */
#define	BIU_SEMA	(BIU_BLOCK+0xC)		/* RW : Bus Semaphore */
#define	BIU_NVRAM	(BIU_BLOCK+0xE)		/* RW : Bus NVRAM */
/*
 * These are specific to the 2300.
 */
#define	BIU_REQINP	(BIU_BLOCK+0x10)	/* Request Queue In */
#define	BIU_REQOUTP	(BIU_BLOCK+0x12)	/* Request Queue Out */
#define	BIU_RSPINP	(BIU_BLOCK+0x14)	/* Response Queue In */
#define	BIU_RSPOUTP	(BIU_BLOCK+0x16)	/* Response Queue Out */

#define	BIU_R2HSTSLO	(BIU_BLOCK+0x18)
#define	BIU_R2HSTSHI	(BIU_BLOCK+0x1A)

#define	BIU_R2HST_INTR		(1 << 15)	/* RISC to Host Interrupt */
#define	BIU_R2HST_PAUSED	(1 <<  8)	/* RISC paused */
#define	BIU_R2HST_ISTAT_MASK	0xff		/* intr information && status */
#define		ISPR2HST_ROM_MBX_OK	0x1	/* ROM mailbox cmd done ok */
#define		ISPR2HST_ROM_MBX_FAIL	0x2	/* ROM mailbox cmd done fail */
#define		ISPR2HST_MBX_OK		0x10	/* mailbox cmd done ok */
#define		ISPR2HST_MBX_FAIL	0x11	/* mailbox cmd done fail */
#define		ISPR2HST_ASYNC_EVENT	0x12	/* Async Event */
#define		ISPR2HST_RSPQ_UPDATE	0x13	/* Response Queue Update */
#define		ISPR2HST_RSPQ_UPDATE2	0x14	/* Response Queue Update */
#define		ISPR2HST_RIO_16		0x15	/* RIO 1-16 */
#define		ISPR2HST_FPOST		0x16	/* Low 16 bits fast post */
#define		ISPR2HST_FPOST_CTIO	0x17	/* Low 16 bits fast post ctio */
#define		ISPR2HST_ATIO_UPDATE	0x1C	/* ATIO Queue Update */
#define		ISPR2HST_ATIO_RSPQ_UPDATE 0x1D	/* ATIO & Request Update */
#define		ISPR2HST_ATIO_UPDATE2	0x1E	/* ATIO Queue Update */

/* fifo command stuff- mostly for SPI */
#define	DFIFO_COMMAND	(BIU_BLOCK+0x60)	/* RW : Command FIFO Port */
#define		RDMA2100_CONTROL	DFIFO_COMMAND
#define	DFIFO_DATA	(BIU_BLOCK+0x62)	/* RW : Data FIFO Port */

/*
 * Putzed DMA register layouts.
 */
#define	CDMA_CONF	(DMA_BLOCK+0x20)	/* RW*: DMA Configuration */
#define		CDMA2100_CONTROL	CDMA_CONF
#define	CDMA_CONTROL	(DMA_BLOCK+0x22)	/* RW*: DMA Control */
#define	CDMA_STATUS 	(DMA_BLOCK+0x24)	/* R  : DMA Status */
#define	CDMA_FIFO_STS	(DMA_BLOCK+0x26)	/* R  : DMA FIFO Status */
#define	CDMA_COUNT	(DMA_BLOCK+0x28)	/* RW*: DMA Transfer Count */
#define	CDMA_ADDR0	(DMA_BLOCK+0x2C)	/* RW*: DMA Address, Word 0 */
#define	CDMA_ADDR1	(DMA_BLOCK+0x2E)	/* RW*: DMA Address, Word 1 */
#define	CDMA_ADDR2	(DMA_BLOCK+0x30)	/* RW*: DMA Address, Word 2 */
#define	CDMA_ADDR3	(DMA_BLOCK+0x32)	/* RW*: DMA Address, Word 3 */

#define	DDMA_CONF	(DMA_BLOCK+0x40)	/* RW*: DMA Configuration */
#define		TDMA2100_CONTROL	DDMA_CONF
#define	DDMA_CONTROL	(DMA_BLOCK+0x42)	/* RW*: DMA Control */
#define	DDMA_STATUS	(DMA_BLOCK+0x44)	/* R  : DMA Status */
#define	DDMA_FIFO_STS	(DMA_BLOCK+0x46)	/* R  : DMA FIFO Status */
#define	DDMA_COUNT_LO	(DMA_BLOCK+0x48)	/* RW*: DMA Xfer Count, Low */
#define	DDMA_COUNT_HI	(DMA_BLOCK+0x4A)	/* RW*: DMA Xfer Count, High */
#define	DDMA_ADDR0	(DMA_BLOCK+0x4C)	/* RW*: DMA Address, Word 0 */
#define	DDMA_ADDR1	(DMA_BLOCK+0x4E)	/* RW*: DMA Address, Word 1 */
/* these are for the 1040A cards */
#define	DDMA_ADDR2	(DMA_BLOCK+0x50)	/* RW*: DMA Address, Word 2 */
#define	DDMA_ADDR3	(DMA_BLOCK+0x52)	/* RW*: DMA Address, Word 3 */


/*
 * Bus Interface Block Register Definitions
 */
/* BUS CONFIGURATION REGISTER #0 */
#define	BIU_CONF0_HW_MASK		0x000F	/* Hardware revision mask */
/* BUS CONFIGURATION REGISTER #1 */

#define	BIU_SBUS_CONF1_PARITY		0x0100 	/* Enable parity checking */
#define	BIU_SBUS_CONF1_FCODE_MASK	0x00F0	/* Fcode cycle mask */

#define	BIU_PCI_CONF1_FIFO_128		0x0040	/* 128 bytes FIFO threshold */
#define	BIU_PCI_CONF1_FIFO_64		0x0030	/* 64 bytes FIFO threshold */
#define	BIU_PCI_CONF1_FIFO_32		0x0020	/* 32 bytes FIFO threshold */
#define	BIU_PCI_CONF1_FIFO_16		0x0010	/* 16 bytes FIFO threshold */
#define	BIU_BURST_ENABLE		0x0004	/* Global enable Bus bursts */
#define	BIU_SBUS_CONF1_FIFO_64		0x0003	/* 64 bytes FIFO threshold */
#define	BIU_SBUS_CONF1_FIFO_32		0x0002	/* 32 bytes FIFO threshold */
#define	BIU_SBUS_CONF1_FIFO_16		0x0001	/* 16 bytes FIFO threshold */
#define	BIU_SBUS_CONF1_FIFO_8		0x0000	/* 8 bytes FIFO threshold */
#define	BIU_SBUS_CONF1_BURST8		0x0008 	/* Enable 8-byte  bursts */
#define	BIU_PCI_CONF1_SXP		0x0008	/* SXP register select */

#define	BIU_PCI1080_CONF1_SXP0		0x0100	/* SXP bank #1 select */
#define	BIU_PCI1080_CONF1_SXP1		0x0200	/* SXP bank #2 select */
#define	BIU_PCI1080_CONF1_DMA		0x0300	/* DMA bank select */

/* ISP2100 Bus Control/Status Register */

#define	BIU2100_ICSR_REGBSEL		0x30	/* RW: register bank select */
#define		BIU2100_RISC_REGS	(0 << 4)	/* RISC Regs */
#define		BIU2100_FB_REGS		(1 << 4)	/* FrameBuffer Regs */
#define		BIU2100_FPM0_REGS	(2 << 4)	/* FPM 0 Regs */
#define		BIU2100_FPM1_REGS	(3 << 4)	/* FPM 1 Regs */
#define	BIU2100_NVRAM_OFFSET		(1 << 14)
#define	BIU2100_FLASH_UPPER_64K		0x04	/* RW: Upper 64K Bank Select */
#define	BIU2100_FLASH_ENABLE		0x02	/* RW: Enable Flash RAM */
#define	BIU2100_SOFT_RESET		0x01
/* SOFT RESET FOR ISP2100 is same bit, but in this register, not ICR */


/* BUS CONTROL REGISTER */
#define	BIU_ICR_ENABLE_DMA_INT		0x0020	/* Enable DMA interrupts */
#define	BIU_ICR_ENABLE_CDMA_INT		0x0010	/* Enable CDMA interrupts */
#define	BIU_ICR_ENABLE_SXP_INT		0x0008	/* Enable SXP interrupts */
#define	BIU_ICR_ENABLE_RISC_INT		0x0004	/* Enable Risc interrupts */
#define	BIU_ICR_ENABLE_ALL_INTS		0x0002	/* Global enable all inter */
#define	BIU_ICR_SOFT_RESET		0x0001	/* Soft Reset of ISP */

#define	BIU_IMASK	(BIU_ICR_ENABLE_RISC_INT|BIU_ICR_ENABLE_ALL_INTS)

#define	BIU2100_ICR_ENABLE_ALL_INTS	0x8000
#define	BIU2100_ICR_ENA_FPM_INT		0x0020
#define	BIU2100_ICR_ENA_FB_INT		0x0010
#define	BIU2100_ICR_ENA_RISC_INT	0x0008
#define	BIU2100_ICR_ENA_CDMA_INT	0x0004
#define	BIU2100_ICR_ENABLE_RXDMA_INT	0x0002
#define	BIU2100_ICR_ENABLE_TXDMA_INT	0x0001
#define	BIU2100_ICR_DISABLE_ALL_INTS	0x0000

#define	BIU2100_IMASK	(BIU2100_ICR_ENA_RISC_INT|BIU2100_ICR_ENABLE_ALL_INTS)

/* BUS STATUS REGISTER */
#define	BIU_ISR_DMA_INT			0x0020	/* DMA interrupt pending */
#define	BIU_ISR_CDMA_INT		0x0010	/* CDMA interrupt pending */
#define	BIU_ISR_SXP_INT			0x0008	/* SXP interrupt pending */
#define	BIU_ISR_RISC_INT		0x0004	/* Risc interrupt pending */
#define	BIU_ISR_IPEND			0x0002	/* Global interrupt pending */

#define	BIU2100_ISR_INT_PENDING		0x8000	/* Global interrupt pending */
#define	BIU2100_ISR_FPM_INT		0x0020	/* FPM interrupt pending */
#define	BIU2100_ISR_FB_INT		0x0010	/* FB interrupt pending */
#define	BIU2100_ISR_RISC_INT		0x0008	/* Risc interrupt pending */
#define	BIU2100_ISR_CDMA_INT		0x0004	/* CDMA interrupt pending */
#define	BIU2100_ISR_RXDMA_INT_PENDING	0x0002	/* Global interrupt pending */
#define	BIU2100_ISR_TXDMA_INT_PENDING	0x0001	/* Global interrupt pending */

#define	INT_PENDING_MASK(isp)	\
 (IS_FC(isp)? (IS_24XX(isp)? BIU2400_ISR_RISC_INT : BIU2100_ISR_RISC_INT) : \
 (BIU_ISR_RISC_INT))

/* BUS SEMAPHORE REGISTER */
#define	BIU_SEMA_STATUS		0x0002	/* Semaphore Status Bit */
#define	BIU_SEMA_LOCK  		0x0001	/* Semaphore Lock Bit */

/* NVRAM SEMAPHORE REGISTER */
#define	BIU_NVRAM_CLOCK		0x0001
#define	BIU_NVRAM_SELECT	0x0002
#define	BIU_NVRAM_DATAOUT	0x0004
#define	BIU_NVRAM_DATAIN	0x0008
#define	BIU_NVRAM_BUSY		0x0080	/* 2322/24xx only */
#define		ISP_NVRAM_READ		6

/* COMNMAND && DATA DMA CONFIGURATION REGISTER */
#define	DMA_ENABLE_SXP_DMA		0x0008	/* Enable SXP to DMA Data */
#define	DMA_ENABLE_INTS			0x0004	/* Enable interrupts to RISC */
#define	DMA_ENABLE_BURST		0x0002	/* Enable Bus burst trans */
#define	DMA_DMA_DIRECTION		0x0001	/*
						 * Set DMA direction:
						 *	0 - DMA FIFO to host
						 *	1 - Host to DMA FIFO
						 */

/* COMMAND && DATA DMA CONTROL REGISTER */
#define	DMA_CNTRL_SUSPEND_CHAN		0x0010	/* Suspend DMA transfer */
#define	DMA_CNTRL_CLEAR_CHAN		0x0008	/*
						 * Clear FIFO and DMA Channel,
						 * reset DMA registers
						 */
#define	DMA_CNTRL_CLEAR_FIFO		0x0004	/* Clear DMA FIFO */
#define	DMA_CNTRL_RESET_INT		0x0002	/* Clear DMA interrupt */
#define	DMA_CNTRL_STROBE		0x0001	/* Start DMA transfer */

/*
 * Variants of same for 2100
 */
#define	DMA_CNTRL2100_CLEAR_CHAN	0x0004
#define	DMA_CNTRL2100_RESET_INT		0x0002



/* DMA STATUS REGISTER */
#define	DMA_SBUS_STATUS_PIPE_MASK	0x00C0	/* DMA Pipeline status mask */
#define	DMA_SBUS_STATUS_CHAN_MASK	0x0030	/* Channel status mask */
#define	DMA_SBUS_STATUS_BUS_PARITY	0x0008	/* Parity Error on bus */
#define	DMA_SBUS_STATUS_BUS_ERR		0x0004	/* Error Detected on bus */
#define	DMA_SBUS_STATUS_TERM_COUNT	0x0002	/* DMA Transfer Completed */
#define	DMA_SBUS_STATUS_INTERRUPT	0x0001	/* Enable DMA channel inter */

#define	DMA_PCI_STATUS_INTERRUPT	0x8000	/* Enable DMA channel inter */
#define	DMA_PCI_STATUS_RETRY_STAT	0x4000	/* Retry status */
#define	DMA_PCI_STATUS_CHAN_MASK	0x3000	/* Channel status mask */
#define	DMA_PCI_STATUS_FIFO_OVR		0x0100	/* DMA FIFO overrun cond */
#define	DMA_PCI_STATUS_FIFO_UDR		0x0080	/* DMA FIFO underrun cond */
#define	DMA_PCI_STATUS_BUS_ERR		0x0040	/* Error Detected on bus */
#define	DMA_PCI_STATUS_BUS_PARITY	0x0020	/* Parity Error on bus */
#define	DMA_PCI_STATUS_CLR_PEND		0x0010	/* DMA clear pending */
#define	DMA_PCI_STATUS_TERM_COUNT	0x0008	/* DMA Transfer Completed */
#define	DMA_PCI_STATUS_DMA_SUSP		0x0004	/* DMA suspended */
#define	DMA_PCI_STATUS_PIPE_MASK	0x0003	/* DMA Pipeline status mask */

/* DMA Status Register, pipeline status bits */
#define	DMA_SBUS_PIPE_FULL		0x00C0	/* Both pipeline stages full */
#define	DMA_SBUS_PIPE_OVERRUN		0x0080	/* Pipeline overrun */
#define	DMA_SBUS_PIPE_STAGE1		0x0040	/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	DMA_PCI_PIPE_FULL		0x0003	/* Both pipeline stages full */
#define	DMA_PCI_PIPE_OVERRUN		0x0002	/* Pipeline overrun */
#define	DMA_PCI_PIPE_STAGE1		0x0001	/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	DMA_PIPE_EMPTY			0x0000	/* All pipeline stages empty */

/* DMA Status Register, channel status bits */
#define	DMA_SBUS_CHAN_SUSPEND	0x0030	/* Channel error or suspended */
#define	DMA_SBUS_CHAN_TRANSFER	0x0020	/* Chan transfer in progress */
#define	DMA_SBUS_CHAN_ACTIVE	0x0010	/* Chan trans to host active */
#define	DMA_PCI_CHAN_TRANSFER	0x3000	/* Chan transfer in progress */
#define	DMA_PCI_CHAN_SUSPEND	0x2000	/* Channel error or suspended */
#define	DMA_PCI_CHAN_ACTIVE	0x1000	/* Chan trans to host active */
#define	ISP_DMA_CHAN_IDLE	0x0000	/* Chan idle (normal comp) */


/* DMA FIFO STATUS REGISTER */
#define	DMA_FIFO_STATUS_OVERRUN		0x0200	/* FIFO Overrun Condition */
#define	DMA_FIFO_STATUS_UNDERRUN	0x0100	/* FIFO Underrun Condition */
#define	DMA_FIFO_SBUS_COUNT_MASK	0x007F	/* FIFO Byte count mask */
#define	DMA_FIFO_PCI_COUNT_MASK		0x00FF	/* FIFO Byte count mask */

/*
 * 2400 Interface Offsets and Register Definitions
 * 
 * The 2400 looks quite different in terms of registers from other QLogic cards.
 * It is getting to be a genuine pain and challenge to keep the same model
 * for all.
 */
#define	BIU2400_FLASH_ADDR	(BIU_BLOCK+0x00)
#define	BIU2400_FLASH_DATA	(BIU_BLOCK+0x04)
#define	BIU2400_CSR		(BIU_BLOCK+0x08)
#define	BIU2400_ICR		(BIU_BLOCK+0x0C)
#define	BIU2400_ISR		(BIU_BLOCK+0x10)

#define	BIU2400_REQINP		(BIU_BLOCK+0x1C) /* Request Queue In */
#define	BIU2400_REQOUTP		(BIU_BLOCK+0x20) /* Request Queue Out */
#define	BIU2400_RSPINP		(BIU_BLOCK+0x24) /* Response Queue In */
#define	BIU2400_RSPOUTP		(BIU_BLOCK+0x28) /* Response Queue Out */

#define	BIU2400_PRI_REQINP 	(BIU_BLOCK+0x2C) /* Priority Request Q In */
#define	BIU2400_PRI_REQOUTP 	(BIU_BLOCK+0x30) /* Priority Request Q Out */

#define	BIU2400_ATIO_RSPINP	(BIU_BLOCK+0x3C) /* ATIO Queue In */
#define	BIU2400_ATIO_RSPOUTP	(BIU_BLOCK+0x40) /* ATIO Queue Out */

#define	BIU2400_R2HSTSLO	(BIU_BLOCK+0x44)
#define	BIU2400_R2HSTSHI	(BIU_BLOCK+0x46)

#define	BIU2400_HCCR		(BIU_BLOCK+0x48)
#define	BIU2400_GPIOD		(BIU_BLOCK+0x4C)
#define	BIU2400_GPIOE		(BIU_BLOCK+0x50)
#define	BIU2400_HSEMA		(BIU_BLOCK+0x58)

/* BIU2400_FLASH_ADDR definitions */
#define	BIU2400_FLASH_DFLAG	(1 << 30)

/* BIU2400_CSR definitions */
#define	BIU2400_NVERR		(1 << 18)
#define	BIU2400_DMA_ACTIVE	(1 << 17)		/* RO */
#define	BIU2400_DMA_STOP	(1 << 16)
#define	BIU2400_FUNCTION	(1 << 15)		/* RO */
#define	BIU2400_PCIX_MODE(x)	(((x) >> 8) & 0xf)	/* RO */
#define	BIU2400_CSR_64BIT	(1 << 2)		/* RO */
#define	BIU2400_FLASH_ENABLE	(1 << 1)
#define	BIU2400_SOFT_RESET	(1 << 0)

/* BIU2400_ICR definitions */
#define	BIU2400_ICR_ENA_RISC_INT	0x8
#define	BIU2400_IMASK			(BIU2400_ICR_ENA_RISC_INT)

/* BIU2400_ISR definitions */
#define	BIU2400_ISR_RISC_INT		0x8

/* BIU2400_HCCR definitions */

#define	HCCR_2400_CMD_NOP		0x00000000
#define	HCCR_2400_CMD_RESET		0x10000000
#define	HCCR_2400_CMD_CLEAR_RESET	0x20000000
#define	HCCR_2400_CMD_PAUSE		0x30000000
#define	HCCR_2400_CMD_RELEASE		0x40000000
#define	HCCR_2400_CMD_SET_HOST_INT	0x50000000
#define	HCCR_2400_CMD_CLEAR_HOST_INT	0x60000000
#define	HCCR_2400_CMD_CLEAR_RISC_INT	0xA0000000

#define	HCCR_2400_RISC_ERR(x)		(((x) >> 12) & 0x7)	/* RO */
#define	HCCR_2400_RISC2HOST_INT		(1 << 6)		/* RO */
#define	HCCR_2400_RISC_RESET		(1 << 5)		/* RO */


/*
 * Mailbox Block Register Offsets
 */

#define	INMAILBOX0	(MBOX_BLOCK+0x0)
#define	INMAILBOX1	(MBOX_BLOCK+0x2)
#define	INMAILBOX2	(MBOX_BLOCK+0x4)
#define	INMAILBOX3	(MBOX_BLOCK+0x6)
#define	INMAILBOX4	(MBOX_BLOCK+0x8)
#define	INMAILBOX5	(MBOX_BLOCK+0xA)
#define	INMAILBOX6	(MBOX_BLOCK+0xC)
#define	INMAILBOX7	(MBOX_BLOCK+0xE)

#define	OUTMAILBOX0	(MBOX_BLOCK+0x0)
#define	OUTMAILBOX1	(MBOX_BLOCK+0x2)
#define	OUTMAILBOX2	(MBOX_BLOCK+0x4)
#define	OUTMAILBOX3	(MBOX_BLOCK+0x6)
#define	OUTMAILBOX4	(MBOX_BLOCK+0x8)
#define	OUTMAILBOX5	(MBOX_BLOCK+0xA)
#define	OUTMAILBOX6	(MBOX_BLOCK+0xC)
#define	OUTMAILBOX7	(MBOX_BLOCK+0xE)

/*
 * Strictly speaking, it's 
 *  SCSI && 2100 : 8 MBOX registers
 *  2200: 24 MBOX registers
 *  2300/2400: 32 MBOX registers
 */
#define	MBOX_OFF(n)	(MBOX_BLOCK + ((n) << 1))
#define	ISP_NMBOX(isp)	((IS_24XX(isp) || IS_23XX(isp))? 32 : (IS_2200(isp) ? 24 : 8))
#define	ISP_NMBOX_BMASK(isp)	\
	((IS_24XX(isp) || IS_23XX(isp))? 0xffffffff : (IS_2200(isp)? 0x00ffffff : 0xff))
#define	MAX_MAILBOX	32
/* if timeout == 0, then default timeout is picked */
#define	MBCMD_DEFAULT_TIMEOUT	100000	/* 100 ms */
typedef struct {
	uint16_t param[MAX_MAILBOX];
	uint32_t ibits;	/* bits to add for register copyin */
	uint32_t obits;	/* bits to add for register copyout */
	uint32_t ibitm;	/* bits to mask for register copyin */
	uint32_t obitm;	/* bits to mask for register copyout */
	uint32_t logval;	/* Bitmask of status codes to log */
	uint32_t timeout;
	uint32_t lineno;
	const char *func;
} mbreg_t;
#define	MBSINIT(mbxp, code, loglev, timo)	\
	ISP_MEMZERO((mbxp), sizeof (mbreg_t));	\
	(mbxp)->ibitm = ~0;			\
	(mbxp)->obitm = ~0;			\
	(mbxp)->param[0] = code;		\
	(mbxp)->lineno = __LINE__;		\
	(mbxp)->func = __func__;		\
	(mbxp)->logval = loglev;		\
	(mbxp)->timeout = timo


/*
 * Fibre Protocol Module and Frame Buffer Register Offsets/Definitions (2X00).
 * NB: The RISC processor must be paused and the appropriate register
 * bank selected via BIU2100_CSR bits.
 */

#define	FPM_DIAG_CONFIG	(BIU_BLOCK + 0x96)
#define		FPM_SOFT_RESET		0x0100

#define	FBM_CMD		(BIU_BLOCK + 0xB8)
#define		FBMCMD_FIFO_RESET_ALL	0xA000


/*
 * SXP Block Register Offsets
 */
#define	SXP_PART_ID	(SXP_BLOCK+0x0)		/* R  : Part ID Code */
#define	SXP_CONFIG1	(SXP_BLOCK+0x2)		/* RW*: Configuration Reg #1 */
#define	SXP_CONFIG2	(SXP_BLOCK+0x4)		/* RW*: Configuration Reg #2 */
#define	SXP_CONFIG3	(SXP_BLOCK+0x6)		/* RW*: Configuration Reg #2 */
#define	SXP_INSTRUCTION	(SXP_BLOCK+0xC)		/* RW*: Instruction Pointer */
#define	SXP_RETURN_ADDR	(SXP_BLOCK+0x10)	/* RW*: Return Address */
#define	SXP_COMMAND	(SXP_BLOCK+0x14)	/* RW*: Command */
#define	SXP_INTERRUPT	(SXP_BLOCK+0x18)	/* R  : Interrupt */
#define	SXP_SEQUENCE	(SXP_BLOCK+0x1C)	/* RW*: Sequence */
#define	SXP_GROSS_ERR	(SXP_BLOCK+0x1E)	/* R  : Gross Error */
#define	SXP_EXCEPTION	(SXP_BLOCK+0x20)	/* RW*: Exception Enable */
#define	SXP_OVERRIDE	(SXP_BLOCK+0x24)	/* RW*: Override */
#define	SXP_LIT_BASE	(SXP_BLOCK+0x28)	/* RW*: Literal Base */
#define	SXP_USER_FLAGS	(SXP_BLOCK+0x2C)	/* RW*: User Flags */
#define	SXP_USER_EXCEPT	(SXP_BLOCK+0x30)	/* RW*: User Exception */
#define	SXP_BREAKPOINT	(SXP_BLOCK+0x34)	/* RW*: Breakpoint */
#define	SXP_SCSI_ID	(SXP_BLOCK+0x40)	/* RW*: SCSI ID */
#define	SXP_DEV_CONFIG1	(SXP_BLOCK+0x42)	/* RW*: Device Config Reg #1 */
#define	SXP_DEV_CONFIG2	(SXP_BLOCK+0x44)	/* RW*: Device Config Reg #2 */
#define	SXP_PHASE_PTR	(SXP_BLOCK+0x48)	/* RW*: SCSI Phase Pointer */
#define	SXP_BUF_PTR	(SXP_BLOCK+0x4C)	/* RW*: SCSI Buffer Pointer */
#define	SXP_BUF_CTR	(SXP_BLOCK+0x50)	/* RW*: SCSI Buffer Counter */
#define	SXP_BUFFER	(SXP_BLOCK+0x52)	/* RW*: SCSI Buffer */
#define	SXP_BUF_BYTE	(SXP_BLOCK+0x54)	/* RW*: SCSI Buffer Byte */
#define	SXP_BUF_WD	(SXP_BLOCK+0x56)	/* RW*: SCSI Buffer Word */
#define	SXP_BUF_WD_TRAN	(SXP_BLOCK+0x58)	/* RW*: SCSI Buffer Wd xlate */
#define	SXP_FIFO	(SXP_BLOCK+0x5A)	/* RW*: SCSI FIFO */
#define	SXP_FIFO_STATUS	(SXP_BLOCK+0x5C)	/* RW*: SCSI FIFO Status */
#define	SXP_FIFO_TOP	(SXP_BLOCK+0x5E)	/* RW*: SCSI FIFO Top Resid */
#define	SXP_FIFO_BOTTOM	(SXP_BLOCK+0x60)	/* RW*: SCSI FIFO Bot Resid */
#define	SXP_TRAN_REG	(SXP_BLOCK+0x64)	/* RW*: SCSI Transferr Reg */
#define	SXP_TRAN_CNT_LO	(SXP_BLOCK+0x68)	/* RW*: SCSI Trans Count */
#define	SXP_TRAN_CNT_HI	(SXP_BLOCK+0x6A)	/* RW*: SCSI Trans Count */
#define	SXP_TRAN_CTR_LO	(SXP_BLOCK+0x6C)	/* RW*: SCSI Trans Counter */
#define	SXP_TRAN_CTR_HI	(SXP_BLOCK+0x6E)	/* RW*: SCSI Trans Counter */
#define	SXP_ARB_DATA	(SXP_BLOCK+0x70)	/* R  : SCSI Arb Data */
#define	SXP_PINS_CTRL	(SXP_BLOCK+0x72)	/* RW*: SCSI Control Pins */
#define	SXP_PINS_DATA	(SXP_BLOCK+0x74)	/* RW*: SCSI Data Pins */
#define	SXP_PINS_DIFF	(SXP_BLOCK+0x76)	/* RW*: SCSI Diff Pins */

/* for 1080/1280/1240 only */
#define	SXP_BANK1_SELECT	0x100


/* SXP CONF1 REGISTER */
#define	SXP_CONF1_ASYNCH_SETUP		0xF000	/* Asynchronous setup time */
#define	SXP_CONF1_SELECTION_UNIT	0x0000	/* Selection time unit */
#define	SXP_CONF1_SELECTION_TIMEOUT	0x0600	/* Selection timeout */
#define	SXP_CONF1_CLOCK_FACTOR		0x00E0	/* Clock factor */
#define	SXP_CONF1_SCSI_ID		0x000F	/* SCSI id */

/* SXP CONF2 REGISTER */
#define	SXP_CONF2_DISABLE_FILTER	0x0040	/* Disable SCSI rec filters */
#define	SXP_CONF2_REQ_ACK_PULLUPS	0x0020	/* Enable req/ack pullups */
#define	SXP_CONF2_DATA_PULLUPS		0x0010	/* Enable data pullups */
#define	SXP_CONF2_CONFIG_AUTOLOAD	0x0008	/* Enable dev conf auto-load */
#define	SXP_CONF2_RESELECT		0x0002	/* Enable reselection */
#define	SXP_CONF2_SELECT		0x0001	/* Enable selection */

/* SXP INTERRUPT REGISTER */
#define	SXP_INT_PARITY_ERR		0x8000	/* Parity error detected */
#define	SXP_INT_GROSS_ERR		0x4000	/* Gross error detected */
#define	SXP_INT_FUNCTION_ABORT		0x2000	/* Last cmd aborted */
#define	SXP_INT_CONDITION_FAILED	0x1000	/* Last cond failed test */
#define	SXP_INT_FIFO_EMPTY		0x0800	/* SCSI FIFO is empty */
#define	SXP_INT_BUF_COUNTER_ZERO	0x0400	/* SCSI buf count == zero */
#define	SXP_INT_XFER_ZERO		0x0200	/* SCSI trans count == zero */
#define	SXP_INT_INT_PENDING		0x0080	/* SXP interrupt pending */
#define	SXP_INT_CMD_RUNNING		0x0040	/* SXP is running a command */
#define	SXP_INT_INT_RETURN_CODE		0x000F	/* Interrupt return code */


/* SXP GROSS ERROR REGISTER */
#define	SXP_GROSS_OFFSET_RESID		0x0040	/* Req/Ack offset not zero */
#define	SXP_GROSS_OFFSET_UNDERFLOW	0x0020	/* Req/Ack offset underflow */
#define	SXP_GROSS_OFFSET_OVERFLOW	0x0010	/* Req/Ack offset overflow */
#define	SXP_GROSS_FIFO_UNDERFLOW	0x0008	/* SCSI FIFO underflow */
#define	SXP_GROSS_FIFO_OVERFLOW		0x0004	/* SCSI FIFO overflow */
#define	SXP_GROSS_WRITE_ERR		0x0002	/* SXP and RISC wrote to reg */
#define	SXP_GROSS_ILLEGAL_INST		0x0001	/* Bad inst loaded into SXP */

/* SXP EXCEPTION REGISTER */
#define	SXP_EXCEPT_USER_0		0x8000	/* Enable user exception #0 */
#define	SXP_EXCEPT_USER_1		0x4000	/* Enable user exception #1 */
#define	PCI_SXP_EXCEPT_SCAM		0x0400	/* SCAM Selection enable */
#define	SXP_EXCEPT_BUS_FREE		0x0200	/* Enable Bus Free det */
#define	SXP_EXCEPT_TARGET_ATN		0x0100	/* Enable TGT mode atten det */
#define	SXP_EXCEPT_RESELECTED		0x0080	/* Enable ReSEL exc handling */
#define	SXP_EXCEPT_SELECTED		0x0040	/* Enable SEL exc handling */
#define	SXP_EXCEPT_ARBITRATION		0x0020	/* Enable ARB exc handling */
#define	SXP_EXCEPT_GROSS_ERR		0x0010	/* Enable gross error except */
#define	SXP_EXCEPT_BUS_RESET		0x0008	/* Enable Bus Reset except */

	/* SXP OVERRIDE REGISTER */
#define	SXP_ORIDE_EXT_TRIGGER		0x8000	/* Enable external trigger */
#define	SXP_ORIDE_STEP			0x4000	/* Enable single step mode */
#define	SXP_ORIDE_BREAKPOINT		0x2000	/* Enable breakpoint reg */
#define	SXP_ORIDE_PIN_WRITE		0x1000	/* Enable write to SCSI pins */
#define	SXP_ORIDE_FORCE_OUTPUTS		0x0800	/* Force SCSI outputs on */
#define	SXP_ORIDE_LOOPBACK		0x0400	/* Enable SCSI loopback mode */
#define	SXP_ORIDE_PARITY_TEST		0x0200	/* Enable parity test mode */
#define	SXP_ORIDE_TRISTATE_ENA_PINS	0x0100	/* Tristate SCSI enable pins */
#define	SXP_ORIDE_TRISTATE_PINS		0x0080	/* Tristate SCSI pins */
#define	SXP_ORIDE_FIFO_RESET		0x0008	/* Reset SCSI FIFO */
#define	SXP_ORIDE_CMD_TERMINATE		0x0004	/* Terminate cur SXP com */
#define	SXP_ORIDE_RESET_REG		0x0002	/* Reset SXP registers */
#define	SXP_ORIDE_RESET_MODULE		0x0001	/* Reset SXP module */

/* SXP COMMANDS */
#define	SXP_RESET_BUS_CMD		0x300b

/* SXP SCSI ID REGISTER */
#define	SXP_SELECTING_ID		0x0F00	/* (Re)Selecting id */
#define	SXP_SELECT_ID			0x000F	/* Select id */

/* SXP DEV CONFIG1 REGISTER */
#define	SXP_DCONF1_SYNC_HOLD		0x7000	/* Synchronous data hold */
#define	SXP_DCONF1_SYNC_SETUP		0x0F00	/* Synchronous data setup */
#define	SXP_DCONF1_SYNC_OFFSET		0x000F	/* Synchronous data offset */


/* SXP DEV CONFIG2 REGISTER */
#define	SXP_DCONF2_FLAGS_MASK		0xF000	/* Device flags */
#define	SXP_DCONF2_WIDE			0x0400	/* Enable wide SCSI */
#define	SXP_DCONF2_PARITY		0x0200	/* Enable parity checking */
#define	SXP_DCONF2_BLOCK_MODE		0x0100	/* Enable blk mode xfr count */
#define	SXP_DCONF2_ASSERTION_MASK	0x0007	/* Assersion period mask */


/* SXP PHASE POINTER REGISTER */
#define	SXP_PHASE_STATUS_PTR		0x1000	/* Status buffer offset */
#define	SXP_PHASE_MSG_IN_PTR		0x0700	/* Msg in buffer offset */
#define	SXP_PHASE_COM_PTR		0x00F0	/* Command buffer offset */
#define	SXP_PHASE_MSG_OUT_PTR		0x0007	/* Msg out buffer offset */


/* SXP FIFO STATUS REGISTER */
#define	SXP_FIFO_TOP_RESID		0x8000	/* Top residue reg full */
#define	SXP_FIFO_ACK_RESID		0x4000	/* Wide transfers odd resid */
#define	SXP_FIFO_COUNT_MASK		0x001C	/* Words in SXP FIFO */
#define	SXP_FIFO_BOTTOM_RESID		0x0001	/* Bottom residue reg full */


/* SXP CONTROL PINS REGISTER */
#define	SXP_PINS_CON_PHASE		0x8000	/* Scsi phase valid */
#define	SXP_PINS_CON_PARITY_HI		0x0400	/* Parity pin */
#define	SXP_PINS_CON_PARITY_LO		0x0200	/* Parity pin */
#define	SXP_PINS_CON_REQ		0x0100	/* SCSI bus REQUEST */
#define	SXP_PINS_CON_ACK		0x0080	/* SCSI bus ACKNOWLEDGE */
#define	SXP_PINS_CON_RST		0x0040	/* SCSI bus RESET */
#define	SXP_PINS_CON_BSY		0x0020	/* SCSI bus BUSY */
#define	SXP_PINS_CON_SEL		0x0010	/* SCSI bus SELECT */
#define	SXP_PINS_CON_ATN		0x0008	/* SCSI bus ATTENTION */
#define	SXP_PINS_CON_MSG		0x0004	/* SCSI bus MESSAGE */
#define	SXP_PINS_CON_CD 		0x0002	/* SCSI bus COMMAND */
#define	SXP_PINS_CON_IO 		0x0001	/* SCSI bus INPUT */

/*
 * Set the hold time for the SCSI Bus Reset to be 250 ms
 */
#define	SXP_SCSI_BUS_RESET_HOLD_TIME	250

/* SXP DIFF PINS REGISTER */
#define	SXP_PINS_DIFF_SENSE		0x0200	/* DIFFSENS sig on SCSI bus */
#define	SXP_PINS_DIFF_MODE		0x0100	/* DIFFM signal */
#define	SXP_PINS_DIFF_ENABLE_OUTPUT	0x0080	/* Enable SXP SCSI data drv */
#define	SXP_PINS_DIFF_PINS_MASK		0x007C	/* Differential control pins */
#define	SXP_PINS_DIFF_TARGET		0x0002	/* Enable SXP target mode */
#define	SXP_PINS_DIFF_INITIATOR		0x0001	/* Enable SXP initiator mode */

/* Ultra2 only */
#define	SXP_PINS_LVD_MODE		0x1000
#define	SXP_PINS_HVD_MODE		0x0800
#define	SXP_PINS_SE_MODE		0x0400
#define	SXP_PINS_MODE_MASK		(SXP_PINS_LVD_MODE|SXP_PINS_HVD_MODE|SXP_PINS_SE_MODE)

/* The above have to be put together with the DIFFM pin to make sense */
#define	ISP1080_LVD_MODE		(SXP_PINS_LVD_MODE)
#define	ISP1080_HVD_MODE		(SXP_PINS_HVD_MODE|SXP_PINS_DIFF_MODE)
#define	ISP1080_SE_MODE			(SXP_PINS_SE_MODE)
#define	ISP1080_MODE_MASK		(SXP_PINS_MODE_MASK|SXP_PINS_DIFF_MODE)

/*
 * RISC and Host Command and Control Block Register Offsets
 */

#define	RISC_ACC	RISC_BLOCK+0x0	/* RW*: Accumulator */
#define	RISC_R1		RISC_BLOCK+0x2	/* RW*: GP Reg R1  */
#define	RISC_R2		RISC_BLOCK+0x4	/* RW*: GP Reg R2  */
#define	RISC_R3		RISC_BLOCK+0x6	/* RW*: GP Reg R3  */
#define	RISC_R4		RISC_BLOCK+0x8	/* RW*: GP Reg R4  */
#define	RISC_R5		RISC_BLOCK+0xA	/* RW*: GP Reg R5  */
#define	RISC_R6		RISC_BLOCK+0xC	/* RW*: GP Reg R6  */
#define	RISC_R7		RISC_BLOCK+0xE	/* RW*: GP Reg R7  */
#define	RISC_R8		RISC_BLOCK+0x10	/* RW*: GP Reg R8  */
#define	RISC_R9		RISC_BLOCK+0x12	/* RW*: GP Reg R9  */
#define	RISC_R10	RISC_BLOCK+0x14	/* RW*: GP Reg R10 */
#define	RISC_R11	RISC_BLOCK+0x16	/* RW*: GP Reg R11 */
#define	RISC_R12	RISC_BLOCK+0x18	/* RW*: GP Reg R12 */
#define	RISC_R13	RISC_BLOCK+0x1a	/* RW*: GP Reg R13 */
#define	RISC_R14	RISC_BLOCK+0x1c	/* RW*: GP Reg R14 */
#define	RISC_R15	RISC_BLOCK+0x1e	/* RW*: GP Reg R15 */
#define	RISC_PSR	RISC_BLOCK+0x20	/* RW*: Processor Status */
#define	RISC_IVR	RISC_BLOCK+0x22	/* RW*: Interrupt Vector */
#define	RISC_PCR	RISC_BLOCK+0x24	/* RW*: Processor Ctrl */
#define	RISC_RAR0	RISC_BLOCK+0x26	/* RW*: Ram Address #0 */
#define	RISC_RAR1	RISC_BLOCK+0x28	/* RW*: Ram Address #1 */
#define	RISC_LCR	RISC_BLOCK+0x2a	/* RW*: Loop Counter */
#define	RISC_PC		RISC_BLOCK+0x2c	/* R  : Program Counter */
#define	RISC_MTR	RISC_BLOCK+0x2e	/* RW*: Memory Timing */
#define		RISC_MTR2100	RISC_BLOCK+0x30

#define	RISC_EMB	RISC_BLOCK+0x30	/* RW*: Ext Mem Boundary */
#define		DUAL_BANK	8
#define	RISC_SP		RISC_BLOCK+0x32	/* RW*: Stack Pointer */
#define	RISC_HRL	RISC_BLOCK+0x3e	/* R *: Hardware Rev Level */
#define	HCCR		RISC_BLOCK+0x40	/* RW : Host Command & Ctrl */
#define	BP0		RISC_BLOCK+0x42	/* RW : Processor Brkpt #0 */
#define	BP1		RISC_BLOCK+0x44	/* RW : Processor Brkpt #1 */
#define	TCR		RISC_BLOCK+0x46	/*  W : Test Control */
#define	TMR		RISC_BLOCK+0x48	/*  W : Test Mode */


/* PROCESSOR STATUS REGISTER */
#define	RISC_PSR_FORCE_TRUE		0x8000
#define	RISC_PSR_LOOP_COUNT_DONE	0x4000
#define	RISC_PSR_RISC_INT		0x2000
#define	RISC_PSR_TIMER_ROLLOVER		0x1000
#define	RISC_PSR_ALU_OVERFLOW		0x0800
#define	RISC_PSR_ALU_MSB		0x0400
#define	RISC_PSR_ALU_CARRY		0x0200
#define	RISC_PSR_ALU_ZERO		0x0100

#define	RISC_PSR_PCI_ULTRA		0x0080
#define	RISC_PSR_SBUS_ULTRA		0x0020

#define	RISC_PSR_DMA_INT		0x0010
#define	RISC_PSR_SXP_INT		0x0008
#define	RISC_PSR_HOST_INT		0x0004
#define	RISC_PSR_INT_PENDING		0x0002
#define	RISC_PSR_FORCE_FALSE  		0x0001


/* Host Command and Control */
#define	HCCR_CMD_NOP			0x0000	/* NOP */
#define	HCCR_CMD_RESET			0x1000	/* Reset RISC */
#define	HCCR_CMD_PAUSE			0x2000	/* Pause RISC */
#define	HCCR_CMD_RELEASE		0x3000	/* Release Paused RISC */
#define	HCCR_CMD_STEP			0x4000	/* Single Step RISC */
#define	HCCR_2X00_DISABLE_PARITY_PAUSE	0x4001	/*
						 * Disable RISC pause on FPM
						 * parity error.
						 */
#define	HCCR_CMD_SET_HOST_INT		0x5000	/* Set Host Interrupt */
#define	HCCR_CMD_CLEAR_HOST_INT		0x6000	/* Clear Host Interrupt */
#define	HCCR_CMD_CLEAR_RISC_INT		0x7000	/* Clear RISC interrupt */
#define	HCCR_CMD_BREAKPOINT		0x8000	/* Change breakpoint enables */
#define	PCI_HCCR_CMD_BIOS		0x9000	/* Write BIOS (disable) */
#define	PCI_HCCR_CMD_PARITY		0xA000	/* Write parity enable */
#define	PCI_HCCR_CMD_PARITY_ERR		0xE000	/* Generate parity error */
#define	HCCR_CMD_TEST_MODE		0xF000	/* Set Test Mode */


#define	ISP2100_HCCR_PARITY_ENABLE_2	0x0400
#define	ISP2100_HCCR_PARITY_ENABLE_1	0x0200
#define	ISP2100_HCCR_PARITY_ENABLE_0	0x0100
#define	ISP2100_HCCR_PARITY		0x0001

#define	PCI_HCCR_PARITY			0x0400	/* Parity error flag */
#define	PCI_HCCR_PARITY_ENABLE_1	0x0200	/* Parity enable bank 1 */
#define	PCI_HCCR_PARITY_ENABLE_0	0x0100	/* Parity enable bank 0 */

#define	HCCR_HOST_INT			0x0080	/* R  : Host interrupt set */
#define	HCCR_RESET			0x0040	/* R  : reset in progress */
#define	HCCR_PAUSE			0x0020	/* R  : RISC paused */

#define	PCI_HCCR_BIOS			0x0001	/*  W : BIOS enable */

/*
 * Defines for Interrupts
 */
#define	ISP_INTS_ENABLED(isp)						\
 ((IS_SCSI(isp))?  							\
  (ISP_READ(isp, BIU_ICR) & BIU_IMASK) :				\
   (IS_24XX(isp)? (ISP_READ(isp, BIU2400_ICR) & BIU2400_IMASK) :	\
   (ISP_READ(isp, BIU_ICR) & BIU2100_IMASK)))

#define	ISP_ENABLE_INTS(isp)						\
 (IS_SCSI(isp) ?  							\
   ISP_WRITE(isp, BIU_ICR, BIU_IMASK) :					\
   (IS_24XX(isp) ?							\
    (ISP_WRITE(isp, BIU2400_ICR, BIU2400_IMASK)) :			\
    (ISP_WRITE(isp, BIU_ICR, BIU2100_IMASK))))

#define	ISP_DISABLE_INTS(isp)						\
 IS_24XX(isp)? ISP_WRITE(isp, BIU2400_ICR, 0) : ISP_WRITE(isp, BIU_ICR, 0)

/*
 * NVRAM Definitions (PCI cards only)
 */

#define	ISPBSMX(c, byte, shift, mask)	\
	(((c)[(byte)] >> (shift)) & (mask))
/*
 * Qlogic 1020/1040 NVRAM is an array of 128 bytes.
 *
 * Some portion of the front of this is for general host adapter properties
 * This is followed by an array of per-target parameters, and is tailed off
 * with a checksum xor byte at offset 127. For non-byte entities data is
 * stored in Little Endian order.
 */

#define	ISP_NVRAM_SIZE	128

#define	ISP_NVRAM_VERSION(c)			(c)[4]
#define	ISP_NVRAM_FIFO_THRESHOLD(c)		ISPBSMX(c, 5, 0, 0x03)
#define	ISP_NVRAM_BIOS_DISABLE(c)		ISPBSMX(c, 5, 2, 0x01)
#define	ISP_NVRAM_HBA_ENABLE(c)			ISPBSMX(c, 5, 3, 0x01)
#define	ISP_NVRAM_INITIATOR_ID(c)		ISPBSMX(c, 5, 4, 0x0f)
#define	ISP_NVRAM_BUS_RESET_DELAY(c)		(c)[6]
#define	ISP_NVRAM_BUS_RETRY_COUNT(c)		(c)[7]
#define	ISP_NVRAM_BUS_RETRY_DELAY(c)		(c)[8]
#define	ISP_NVRAM_ASYNC_DATA_SETUP_TIME(c)	ISPBSMX(c, 9, 0, 0x0f)
#define	ISP_NVRAM_REQ_ACK_ACTIVE_NEGATION(c)	ISPBSMX(c, 9, 4, 0x01)
#define	ISP_NVRAM_DATA_LINE_ACTIVE_NEGATION(c)	ISPBSMX(c, 9, 5, 0x01)
#define	ISP_NVRAM_DATA_DMA_BURST_ENABLE(c)	ISPBSMX(c, 9, 6, 0x01)
#define	ISP_NVRAM_CMD_DMA_BURST_ENABLE(c)	ISPBSMX(c, 9, 7, 0x01)
#define	ISP_NVRAM_TAG_AGE_LIMIT(c)		(c)[10]
#define	ISP_NVRAM_LOWTRM_ENABLE(c)		ISPBSMX(c, 11, 0, 0x01)
#define	ISP_NVRAM_HITRM_ENABLE(c)		ISPBSMX(c, 11, 1, 0x01)
#define	ISP_NVRAM_PCMC_BURST_ENABLE(c)		ISPBSMX(c, 11, 2, 0x01)
#define	ISP_NVRAM_ENABLE_60_MHZ(c)		ISPBSMX(c, 11, 3, 0x01)
#define	ISP_NVRAM_SCSI_RESET_DISABLE(c)		ISPBSMX(c, 11, 4, 0x01)
#define	ISP_NVRAM_ENABLE_AUTO_TERM(c)		ISPBSMX(c, 11, 5, 0x01)
#define	ISP_NVRAM_FIFO_THRESHOLD_128(c)		ISPBSMX(c, 11, 6, 0x01)
#define	ISP_NVRAM_AUTO_TERM_SUPPORT(c)		ISPBSMX(c, 11, 7, 0x01)
#define	ISP_NVRAM_SELECTION_TIMEOUT(c)		(((c)[12]) | ((c)[13] << 8))
#define	ISP_NVRAM_MAX_QUEUE_DEPTH(c)		(((c)[14]) | ((c)[15] << 8))
#define	ISP_NVRAM_SCSI_BUS_SIZE(c)		ISPBSMX(c, 16, 0, 0x01)
#define	ISP_NVRAM_SCSI_BUS_TYPE(c)		ISPBSMX(c, 16, 1, 0x01)
#define	ISP_NVRAM_ADAPTER_CLK_SPEED(c)		ISPBSMX(c, 16, 2, 0x01)
#define	ISP_NVRAM_SOFT_TERM_SUPPORT(c)		ISPBSMX(c, 16, 3, 0x01)
#define	ISP_NVRAM_FLASH_ONBOARD(c)		ISPBSMX(c, 16, 4, 0x01)
#define	ISP_NVRAM_FAST_MTTR_ENABLE(c)		ISPBSMX(c, 22, 0, 0x01)

#define	ISP_NVRAM_TARGOFF			28
#define	ISP_NVRAM_TARGSIZE			6
#define	_IxT(tgt, tidx)			\
	(ISP_NVRAM_TARGOFF + (ISP_NVRAM_TARGSIZE * (tgt)) + (tidx))
#define	ISP_NVRAM_TGT_RENEG(c, t)		ISPBSMX(c, _IxT(t, 0), 0, 0x01)
#define	ISP_NVRAM_TGT_QFRZ(c, t)		ISPBSMX(c, _IxT(t, 0), 1, 0x01)
#define	ISP_NVRAM_TGT_ARQ(c, t)			ISPBSMX(c, _IxT(t, 0), 2, 0x01)
#define	ISP_NVRAM_TGT_TQING(c, t)		ISPBSMX(c, _IxT(t, 0), 3, 0x01)
#define	ISP_NVRAM_TGT_SYNC(c, t)		ISPBSMX(c, _IxT(t, 0), 4, 0x01)
#define	ISP_NVRAM_TGT_WIDE(c, t)		ISPBSMX(c, _IxT(t, 0), 5, 0x01)
#define	ISP_NVRAM_TGT_PARITY(c, t)		ISPBSMX(c, _IxT(t, 0), 6, 0x01)
#define	ISP_NVRAM_TGT_DISC(c, t)		ISPBSMX(c, _IxT(t, 0), 7, 0x01)
#define	ISP_NVRAM_TGT_EXEC_THROTTLE(c, t)	ISPBSMX(c, _IxT(t, 1), 0, 0xff)
#define	ISP_NVRAM_TGT_SYNC_PERIOD(c, t)		ISPBSMX(c, _IxT(t, 2), 0, 0xff)
#define	ISP_NVRAM_TGT_SYNC_OFFSET(c, t)		ISPBSMX(c, _IxT(t, 3), 0, 0x0f)
#define	ISP_NVRAM_TGT_DEVICE_ENABLE(c, t)	ISPBSMX(c, _IxT(t, 3), 4, 0x01)
#define	ISP_NVRAM_TGT_LUN_DISABLE(c, t)		ISPBSMX(c, _IxT(t, 3), 5, 0x01)

/*
 * Qlogic 1080/1240 NVRAM is an array of 256 bytes.
 *
 * Some portion of the front of this is for general host adapter properties
 * This is followed by an array of per-target parameters, and is tailed off
 * with a checksum xor byte at offset 256. For non-byte entities data is
 * stored in Little Endian order.
 */

#define	ISP1080_NVRAM_SIZE	256

#define	ISP1080_NVRAM_VERSION(c)		ISP_NVRAM_VERSION(c)

/* Offset 5 */
/*
	uint8_t bios_configuration_mode     :2;
	uint8_t bios_disable                :1;
	uint8_t selectable_scsi_boot_enable :1;
	uint8_t cd_rom_boot_enable          :1;
	uint8_t disable_loading_risc_code   :1;
	uint8_t enable_64bit_addressing     :1;
	uint8_t unused_7                    :1;
 */

/* Offsets 6, 7 */
/*
        uint8_t boot_lun_number    :5;
        uint8_t scsi_bus_number    :1;
        uint8_t unused_6           :1;
        uint8_t unused_7           :1;
        uint8_t boot_target_number :4;
        uint8_t unused_12          :1;
        uint8_t unused_13          :1;
        uint8_t unused_14          :1;
        uint8_t unused_15          :1;
 */

#define	ISP1080_NVRAM_HBA_ENABLE(c)			ISPBSMX(c, 16, 3, 0x01)

#define	ISP1080_NVRAM_BURST_ENABLE(c)			ISPBSMX(c, 16, 1, 0x01)
#define	ISP1080_NVRAM_FIFO_THRESHOLD(c)			ISPBSMX(c, 16, 4, 0x0f)

#define	ISP1080_NVRAM_AUTO_TERM_SUPPORT(c)		ISPBSMX(c, 17, 7, 0x01)
#define	ISP1080_NVRAM_BUS0_TERM_MODE(c)			ISPBSMX(c, 17, 0, 0x03)
#define	ISP1080_NVRAM_BUS1_TERM_MODE(c)			ISPBSMX(c, 17, 2, 0x03)

#define	ISP1080_ISP_PARAMETER(c)			\
	(((c)[18]) | ((c)[19] << 8))

#define	ISP1080_FAST_POST(c)				ISPBSMX(c, 20, 0, 0x01)
#define	ISP1080_REPORT_LVD_TRANSITION(c)		ISPBSMX(c, 20, 1, 0x01)

#define	ISP1080_BUS1_OFF				112

#define	ISP1080_NVRAM_INITIATOR_ID(c, b)		\
	ISPBSMX(c, ((b == 0)? 0 : ISP1080_BUS1_OFF) + 24, 0, 0x0f)
#define	ISP1080_NVRAM_BUS_RESET_DELAY(c, b)		\
	(c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 25]
#define	ISP1080_NVRAM_BUS_RETRY_COUNT(c, b)		\
	(c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 26]
#define	ISP1080_NVRAM_BUS_RETRY_DELAY(c, b)		\
	(c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 27]

#define	ISP1080_NVRAM_ASYNC_DATA_SETUP_TIME(c, b)	\
	ISPBSMX(c, ((b == 0)? 0 : ISP1080_BUS1_OFF) + 28, 0, 0x0f)
#define	ISP1080_NVRAM_REQ_ACK_ACTIVE_NEGATION(c, b)	\
	ISPBSMX(c, ((b == 0)? 0 : ISP1080_BUS1_OFF) + 28, 4, 0x01)
#define	ISP1080_NVRAM_DATA_LINE_ACTIVE_NEGATION(c, b)	\
	ISPBSMX(c, ((b == 0)? 0 : ISP1080_BUS1_OFF) + 28, 5, 0x01)
#define	ISP1080_NVRAM_SELECTION_TIMEOUT(c, b)		\
	(((c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 30]) | \
	((c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 31] << 8))
#define	ISP1080_NVRAM_MAX_QUEUE_DEPTH(c, b)		\
	(((c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 32]) | \
	((c)[((b == 0)? 0 : ISP1080_BUS1_OFF) + 33] << 8))

#define	ISP1080_NVRAM_TARGOFF(b)		\
	((b == 0)? 40: (40 + ISP1080_BUS1_OFF))
#define	ISP1080_NVRAM_TARGSIZE			6
#define	_IxT8(tgt, tidx, b)			\
	(ISP1080_NVRAM_TARGOFF((b)) + (ISP1080_NVRAM_TARGSIZE * (tgt)) + (tidx))

#define	ISP1080_NVRAM_TGT_RENEG(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 0, (b)), 0, 0x01)
#define	ISP1080_NVRAM_TGT_QFRZ(c, t, b)			\
	ISPBSMX(c, _IxT8(t, 0, (b)), 1, 0x01)
#define	ISP1080_NVRAM_TGT_ARQ(c, t, b)			\
	ISPBSMX(c, _IxT8(t, 0, (b)), 2, 0x01)
#define	ISP1080_NVRAM_TGT_TQING(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 0, (b)), 3, 0x01)
#define	ISP1080_NVRAM_TGT_SYNC(c, t, b)			\
	ISPBSMX(c, _IxT8(t, 0, (b)), 4, 0x01)
#define	ISP1080_NVRAM_TGT_WIDE(c, t, b)			\
	ISPBSMX(c, _IxT8(t, 0, (b)), 5, 0x01)
#define	ISP1080_NVRAM_TGT_PARITY(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 0, (b)), 6, 0x01)
#define	ISP1080_NVRAM_TGT_DISC(c, t, b)			\
	ISPBSMX(c, _IxT8(t, 0, (b)), 7, 0x01)
#define	ISP1080_NVRAM_TGT_EXEC_THROTTLE(c, t, b)	\
	ISPBSMX(c, _IxT8(t, 1, (b)), 0, 0xff)
#define	ISP1080_NVRAM_TGT_SYNC_PERIOD(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 2, (b)), 0, 0xff)
#define	ISP1080_NVRAM_TGT_SYNC_OFFSET(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 3, (b)), 0, 0x0f)
#define	ISP1080_NVRAM_TGT_DEVICE_ENABLE(c, t, b)	\
	ISPBSMX(c, _IxT8(t, 3, (b)), 4, 0x01)
#define	ISP1080_NVRAM_TGT_LUN_DISABLE(c, t, b)		\
	ISPBSMX(c, _IxT8(t, 3, (b)), 5, 0x01)

#define	ISP12160_NVRAM_HBA_ENABLE	ISP1080_NVRAM_HBA_ENABLE
#define	ISP12160_NVRAM_BURST_ENABLE	ISP1080_NVRAM_BURST_ENABLE
#define	ISP12160_NVRAM_FIFO_THRESHOLD	ISP1080_NVRAM_FIFO_THRESHOLD
#define	ISP12160_NVRAM_AUTO_TERM_SUPPORT	ISP1080_NVRAM_AUTO_TERM_SUPPORT
#define	ISP12160_NVRAM_BUS0_TERM_MODE	ISP1080_NVRAM_BUS0_TERM_MODE
#define	ISP12160_NVRAM_BUS1_TERM_MODE	ISP1080_NVRAM_BUS1_TERM_MODE
#define	ISP12160_ISP_PARAMETER		ISP12160_ISP_PARAMETER
#define	ISP12160_FAST_POST		ISP1080_FAST_POST
#define	ISP12160_REPORT_LVD_TRANSITION	ISP1080_REPORT_LVD_TRANSTION

#define	ISP12160_NVRAM_INITIATOR_ID			\
	ISP1080_NVRAM_INITIATOR_ID
#define	ISP12160_NVRAM_BUS_RESET_DELAY			\
	ISP1080_NVRAM_BUS_RESET_DELAY
#define	ISP12160_NVRAM_BUS_RETRY_COUNT			\
	ISP1080_NVRAM_BUS_RETRY_COUNT
#define	ISP12160_NVRAM_BUS_RETRY_DELAY			\
	ISP1080_NVRAM_BUS_RETRY_DELAY
#define	ISP12160_NVRAM_ASYNC_DATA_SETUP_TIME		\
	ISP1080_NVRAM_ASYNC_DATA_SETUP_TIME
#define	ISP12160_NVRAM_REQ_ACK_ACTIVE_NEGATION		\
	ISP1080_NVRAM_REQ_ACK_ACTIVE_NEGATION
#define	ISP12160_NVRAM_DATA_LINE_ACTIVE_NEGATION	\
	ISP1080_NVRAM_DATA_LINE_ACTIVE_NEGATION
#define	ISP12160_NVRAM_SELECTION_TIMEOUT		\
	ISP1080_NVRAM_SELECTION_TIMEOUT
#define	ISP12160_NVRAM_MAX_QUEUE_DEPTH			\
	ISP1080_NVRAM_MAX_QUEUE_DEPTH


#define	ISP12160_BUS0_OFF	24
#define	ISP12160_BUS1_OFF	136

#define	ISP12160_NVRAM_TARGOFF(b)		\
	(((b == 0)? ISP12160_BUS0_OFF : ISP12160_BUS1_OFF) + 16)

#define	ISP12160_NVRAM_TARGSIZE			6
#define	_IxT16(tgt, tidx, b)			\
	(ISP12160_NVRAM_TARGOFF((b))+(ISP12160_NVRAM_TARGSIZE * (tgt))+(tidx))

#define	ISP12160_NVRAM_TGT_RENEG(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 0, 0x01)
#define	ISP12160_NVRAM_TGT_QFRZ(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 1, 0x01)
#define	ISP12160_NVRAM_TGT_ARQ(c, t, b)			\
	ISPBSMX(c, _IxT16(t, 0, (b)), 2, 0x01)
#define	ISP12160_NVRAM_TGT_TQING(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 3, 0x01)
#define	ISP12160_NVRAM_TGT_SYNC(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 4, 0x01)
#define	ISP12160_NVRAM_TGT_WIDE(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 5, 0x01)
#define	ISP12160_NVRAM_TGT_PARITY(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 6, 0x01)
#define	ISP12160_NVRAM_TGT_DISC(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 0, (b)), 7, 0x01)

#define	ISP12160_NVRAM_TGT_EXEC_THROTTLE(c, t, b)	\
	ISPBSMX(c, _IxT16(t, 1, (b)), 0, 0xff)
#define	ISP12160_NVRAM_TGT_SYNC_PERIOD(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 2, (b)), 0, 0xff)

#define	ISP12160_NVRAM_TGT_SYNC_OFFSET(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 3, (b)), 0, 0x1f)
#define	ISP12160_NVRAM_TGT_DEVICE_ENABLE(c, t, b)	\
	ISPBSMX(c, _IxT16(t, 3, (b)), 5, 0x01)

#define	ISP12160_NVRAM_PPR_OPTIONS(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 4, (b)), 0, 0x0f)
#define	ISP12160_NVRAM_PPR_WIDTH(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 4, (b)), 4, 0x03)
#define	ISP12160_NVRAM_PPR_ENABLE(c, t, b)		\
	ISPBSMX(c, _IxT16(t, 4, (b)), 7, 0x01)

/*
 * Qlogic 2100 thru 2300 NVRAM is an array of 256 bytes.
 *
 * Some portion of the front of this is for general RISC engine parameters,
 * mostly reflecting the state of the last INITIALIZE FIRMWARE mailbox command.
 *
 * This is followed by some general host adapter parameters, and ends with
 * a checksum xor byte at offset 255. For non-byte entities data is stored
 * in Little Endian order.
 */
#define	ISP2100_NVRAM_SIZE	256
/* ISP_NVRAM_VERSION is in same overall place */
#define	ISP2100_NVRAM_RISCVER(c)		(c)[6]
#define	ISP2100_NVRAM_OPTIONS(c)		((c)[8] | ((c)[9] << 8))
#define	ISP2100_NVRAM_MAXFRAMELENGTH(c)		(((c)[10]) | ((c)[11] << 8))
#define	ISP2100_NVRAM_MAXIOCBALLOCATION(c)	(((c)[12]) | ((c)[13] << 8))
#define	ISP2100_NVRAM_EXECUTION_THROTTLE(c)	(((c)[14]) | ((c)[15] << 8))
#define	ISP2100_NVRAM_RETRY_COUNT(c)		(c)[16]
#define	ISP2100_NVRAM_RETRY_DELAY(c)		(c)[17]

#define	ISP2100_NVRAM_PORT_NAME(c)	(\
		(((uint64_t)(c)[18]) << 56) | \
		(((uint64_t)(c)[19]) << 48) | \
		(((uint64_t)(c)[20]) << 40) | \
		(((uint64_t)(c)[21]) << 32) | \
		(((uint64_t)(c)[22]) << 24) | \
		(((uint64_t)(c)[23]) << 16) | \
		(((uint64_t)(c)[24]) <<  8) | \
		(((uint64_t)(c)[25]) <<  0))

#define	ISP2100_NVRAM_HARDLOOPID(c)		((c)[26] | ((c)[27] << 8))
#define	ISP2100_NVRAM_TOV(c)			((c)[29])

#define	ISP2100_NVRAM_NODE_NAME(c)	(\
		(((uint64_t)(c)[30]) << 56) | \
		(((uint64_t)(c)[31]) << 48) | \
		(((uint64_t)(c)[32]) << 40) | \
		(((uint64_t)(c)[33]) << 32) | \
		(((uint64_t)(c)[34]) << 24) | \
		(((uint64_t)(c)[35]) << 16) | \
		(((uint64_t)(c)[36]) <<  8) | \
		(((uint64_t)(c)[37]) <<  0))

#define	ISP2100_XFW_OPTIONS(c)			((c)[38] | ((c)[39] << 8))

#define	ISP2100_RACC_TIMER(c)			(c)[40]
#define	ISP2100_IDELAY_TIMER(c)			(c)[41]

#define	ISP2100_ZFW_OPTIONS(c)			((c)[42] | ((c)[43] << 8))

#define	ISP2100_SERIAL_LINK(c)			((c)[68] | ((c)[69] << 8))

#define	ISP2100_NVRAM_HBA_OPTIONS(c)		((c)[70] | ((c)[71] << 8))
#define	ISP2100_NVRAM_HBA_DISABLE(c)		ISPBSMX(c, 70, 0, 0x01)
#define	ISP2100_NVRAM_BIOS_DISABLE(c)		ISPBSMX(c, 70, 1, 0x01)
#define	ISP2100_NVRAM_LUN_DISABLE(c)		ISPBSMX(c, 70, 2, 0x01)
#define	ISP2100_NVRAM_ENABLE_SELECT_BOOT(c)	ISPBSMX(c, 70, 3, 0x01)
#define	ISP2100_NVRAM_DISABLE_CODELOAD(c)	ISPBSMX(c, 70, 4, 0x01)
#define	ISP2100_NVRAM_SET_CACHELINESZ(c)	ISPBSMX(c, 70, 5, 0x01)

#define	ISP2100_NVRAM_BOOT_NODE_NAME(c)	(\
		(((uint64_t)(c)[72]) << 56) | \
		(((uint64_t)(c)[73]) << 48) | \
		(((uint64_t)(c)[74]) << 40) | \
		(((uint64_t)(c)[75]) << 32) | \
		(((uint64_t)(c)[76]) << 24) | \
		(((uint64_t)(c)[77]) << 16) | \
		(((uint64_t)(c)[78]) <<  8) | \
		(((uint64_t)(c)[79]) <<  0))

#define	ISP2100_NVRAM_BOOT_LUN(c)		(c)[80]
#define	ISP2100_RESET_DELAY(c)			(c)[81]

#define	ISP2100_HBA_FEATURES(c)			((c)[232] | ((c)[233] << 8))

/*
 * Qlogic 2400 NVRAM is an array of 512 bytes with a 32 bit checksum.
 */
#define	ISP2400_NVRAM_PORT0_ADDR	0x80
#define	ISP2400_NVRAM_PORT1_ADDR	0x180
#define	ISP2400_NVRAM_SIZE		512

#define	ISP2400_NVRAM_VERSION(c)		((c)[4] | ((c)[5] << 8))
#define	ISP2400_NVRAM_MAXFRAMELENGTH(c)		(((c)[12]) | ((c)[13] << 8))
#define	ISP2400_NVRAM_EXECUTION_THROTTLE(c)	(((c)[14]) | ((c)[15] << 8))
#define	ISP2400_NVRAM_EXCHANGE_COUNT(c)		(((c)[16]) | ((c)[17] << 8))
#define	ISP2400_NVRAM_HARDLOOPID(c)		((c)[18] | ((c)[19] << 8))

#define	ISP2400_NVRAM_PORT_NAME(c)	(\
		(((uint64_t)(c)[20]) << 56) | \
		(((uint64_t)(c)[21]) << 48) | \
		(((uint64_t)(c)[22]) << 40) | \
		(((uint64_t)(c)[23]) << 32) | \
		(((uint64_t)(c)[24]) << 24) | \
		(((uint64_t)(c)[25]) << 16) | \
		(((uint64_t)(c)[26]) <<  8) | \
		(((uint64_t)(c)[27]) <<  0))

#define	ISP2400_NVRAM_NODE_NAME(c)	(\
		(((uint64_t)(c)[28]) << 56) | \
		(((uint64_t)(c)[29]) << 48) | \
		(((uint64_t)(c)[30]) << 40) | \
		(((uint64_t)(c)[31]) << 32) | \
		(((uint64_t)(c)[32]) << 24) | \
		(((uint64_t)(c)[33]) << 16) | \
		(((uint64_t)(c)[34]) <<  8) | \
		(((uint64_t)(c)[35]) <<  0))

#define	ISP2400_NVRAM_LOGIN_RETRY_CNT(c)	((c)[36] | ((c)[37] << 8))
#define	ISP2400_NVRAM_LINK_DOWN_ON_NOS(c)	((c)[38] | ((c)[39] << 8))
#define	ISP2400_NVRAM_INTERRUPT_DELAY(c)	((c)[40] | ((c)[41] << 8))
#define	ISP2400_NVRAM_LOGIN_TIMEOUT(c)		((c)[42] | ((c)[43] << 8))

#define	ISP2400_NVRAM_FIRMWARE_OPTIONS1(c)	\
	((c)[44] | ((c)[45] << 8) | ((c)[46] << 16) | ((c)[47] << 24))
#define	ISP2400_NVRAM_FIRMWARE_OPTIONS2(c)	\
	((c)[48] | ((c)[49] << 8) | ((c)[50] << 16) | ((c)[51] << 24))
#define	ISP2400_NVRAM_FIRMWARE_OPTIONS3(c)	\
	((c)[52] | ((c)[53] << 8) | ((c)[54] << 16) | ((c)[55] << 24))

/*
 * Firmware Crash Dump
 *
 * QLogic needs specific information format when they look at firmware crashes.
 *
 * This is incredibly kernel memory consumptive (to say the least), so this
 * code is only compiled in when needed.
 */

#define	QLA2200_RISC_IMAGE_DUMP_SIZE					\
	(1 * sizeof (uint16_t)) +	/* 'used' flag (also HBA type) */ \
	(352 * sizeof (uint16_t)) +	/* RISC registers */		\
 	(61440 * sizeof (uint16_t))	/* RISC SRAM (offset 0x1000..0xffff) */
#define	QLA2300_RISC_IMAGE_DUMP_SIZE					\
	(1 * sizeof (uint16_t)) +	/* 'used' flag (also HBA type) */ \
	(464 * sizeof (uint16_t)) +	/* RISC registers */		\
 	(63488 * sizeof (uint16_t)) +	/* RISC SRAM (0x0800..0xffff) */ \
	(4096 * sizeof (uint16_t)) +	/* RISC SRAM (0x10000..0x10FFF) */ \
	(61440 * sizeof (uint16_t))	/* RISC SRAM (0x11000..0x1FFFF) */
/* the larger of the two */
#define	ISP_CRASH_IMAGE_SIZE	QLA2300_RISC_IMAGE_DUMP_SIZE
#endif	/* _ISPREG_H */
