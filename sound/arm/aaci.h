/*
 *  linux/sound/arm/aaci.c - ARM PrimeCell AACI PL041 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef AACI_H
#define AACI_H

/*
 * Control and status register offsets
 *  P39.
 */
#define AACI_CSCH1	0x000
#define AACI_CSCH2	0x014
#define AACI_CSCH3	0x028
#define AACI_CSCH4	0x03c

#define AACI_RXCR	0x000	/* 29 bits Control Rx FIFO */
#define AACI_TXCR	0x004	/* 17 bits Control Tx FIFO */
#define AACI_SR		0x008	/* 12 bits Status */
#define AACI_ISR	0x00c	/* 7 bits  Int Status */
#define AACI_IE 	0x010	/* 7 bits  Int Enable */

/*
 * Other registers
 */
#define AACI_SL1RX	0x050
#define AACI_SL1TX	0x054
#define AACI_SL2RX	0x058
#define AACI_SL2TX	0x05c
#define AACI_SL12RX	0x060
#define AACI_SL12TX	0x064
#define AACI_SLFR	0x068	/* slot flags */
#define AACI_SLISTAT	0x06c	/* slot interrupt status */
#define AACI_SLIEN	0x070	/* slot interrupt enable */
#define AACI_INTCLR	0x074	/* interrupt clear */
#define AACI_MAINCR	0x078	/* main control */
#define AACI_RESET	0x07c	/* reset control */
#define AACI_SYNC	0x080	/* sync control */
#define AACI_ALLINTS	0x084	/* all fifo interrupt status */
#define AACI_MAINFR	0x088	/* main flag register */
#define AACI_DR1	0x090	/* data read/written fifo 1 */
#define AACI_DR2	0x0b0	/* data read/written fifo 2 */
#define AACI_DR3	0x0d0	/* data read/written fifo 3 */
#define AACI_DR4	0x0f0	/* data read/written fifo 4 */

/*
 * TX/RX fifo control register (CR). P48
 */
#define CR_FEN		(1 << 16)	/* fifo enable */
#define CR_COMPACT	(1 << 15)	/* compact mode */
#define CR_SZ16		(0 << 13)	/* 16 bits */
#define CR_SZ18		(1 << 13)	/* 18 bits */
#define CR_SZ20		(2 << 13)	/* 20 bits */
#define CR_SZ12		(3 << 13)	/* 12 bits */
#define CR_SL12		(1 << 12)
#define CR_SL11		(1 << 11)
#define CR_SL10		(1 << 10)
#define CR_SL9		(1 << 9)
#define CR_SL8		(1 << 8)
#define CR_SL7		(1 << 7)
#define CR_SL6		(1 << 6)
#define CR_SL5		(1 << 5)
#define CR_SL4		(1 << 4)
#define CR_SL3		(1 << 3)
#define CR_SL2		(1 << 2)
#define CR_SL1		(1 << 1)
#define CR_EN		(1 << 0)	/* transmit enable */

/*
 * status register bits. P49
 */
#define SR_RXTOFE	(1 << 11)	/* rx timeout fifo empty */
#define SR_TXTO		(1 << 10)	/* rx timeout fifo nonempty */
#define SR_TXU		(1 << 9)	/* tx underrun */
#define SR_RXO		(1 << 8)	/* rx overrun */
#define SR_TXB		(1 << 7)	/* tx busy */
#define SR_RXB		(1 << 6)	/* rx busy */
#define SR_TXFF		(1 << 5)	/* tx fifo full */
#define SR_RXFF		(1 << 4)	/* rx fifo full */
#define SR_TXHE		(1 << 3)	/* tx fifo half empty */
#define SR_RXHF		(1 << 2)	/* rx fifo half full */
#define SR_TXFE		(1 << 1)	/* tx fifo empty */
#define SR_RXFE		(1 << 0)	/* rx fifo empty */

/*
 * interrupt status register bits.
 */
#define ISR_RXTOFEINTR	(1 << 6)	/* rx fifo empty */
#define ISR_URINTR	(1 << 5)	/* tx underflow */
#define ISR_ORINTR	(1 << 4)	/* rx overflow */
#define ISR_RXINTR	(1 << 3)	/* rx fifo */
#define ISR_TXINTR	(1 << 2)	/* tx fifo intr */
#define ISR_RXTOINTR	(1 << 1)	/* tx timeout */
#define ISR_TXCINTR	(1 << 0)	/* tx complete */

/*
 * interrupt enable register bits.
 */
#define IE_RXTOIE	(1 << 6)
#define IE_URIE		(1 << 5)
#define IE_ORIE		(1 << 4)
#define IE_RXIE		(1 << 3)
#define IE_TXIE		(1 << 2)
#define IE_RXTIE	(1 << 1)
#define IE_TXCIE	(1 << 0)

/*
 * interrupt status. P51
 */
#define ISR_RXTOFE	(1 << 6)	/* rx timeout fifo empty */
#define ISR_UR		(1 << 5)	/* tx fifo underrun */
#define ISR_OR		(1 << 4)	/* rx fifo overrun */
#define ISR_RX		(1 << 3)	/* rx interrupt status */
#define ISR_TX		(1 << 2)	/* tx interrupt status */
#define ISR_RXTO	(1 << 1)	/* rx timeout */
#define ISR_TXC		(1 << 0)	/* tx complete */

/*
 * interrupt enable. P52
 */
#define IE_RXTOFE	(1 << 6)	/* rx timeout fifo empty */
#define IE_UR		(1 << 5)	/* tx fifo underrun */
#define IE_OR		(1 << 4)	/* rx fifo overrun */
#define IE_RX		(1 << 3)	/* rx interrupt status */
#define IE_TX		(1 << 2)	/* tx interrupt status */
#define IE_RXTO		(1 << 1)	/* rx timeout */
#define IE_TXC		(1 << 0)	/* tx complete */

/*
 * slot flag register bits. P56
 */
#define SLFR_RWIS	(1 << 13)	/* raw wake-up interrupt status */
#define SLFR_RGPIOINTR	(1 << 12)	/* raw gpio interrupt */
#define SLFR_12TXE	(1 << 11)	/* slot 12 tx empty */
#define SLFR_12RXV	(1 << 10)	/* slot 12 rx valid */
#define SLFR_2TXE	(1 << 9)	/* slot 2 tx empty */
#define SLFR_2RXV	(1 << 8)	/* slot 2 rx valid */
#define SLFR_1TXE	(1 << 7)	/* slot 1 tx empty */
#define SLFR_1RXV	(1 << 6)	/* slot 1 rx valid */
#define SLFR_12TXB	(1 << 5)	/* slot 12 tx busy */
#define SLFR_12RXB	(1 << 4)	/* slot 12 rx busy */
#define SLFR_2TXB	(1 << 3)	/* slot 2 tx busy */
#define SLFR_2RXB	(1 << 2)	/* slot 2 rx busy */
#define SLFR_1TXB	(1 << 1)	/* slot 1 tx busy */
#define SLFR_1RXB	(1 << 0)	/* slot 1 rx busy */

/*
 * Interrupt clear register.
 */
#define ICLR_RXTOFEC4	(1 << 12)
#define ICLR_RXTOFEC3	(1 << 11)
#define ICLR_RXTOFEC2	(1 << 10)
#define ICLR_RXTOFEC1	(1 << 9)
#define ICLR_TXUEC4	(1 << 8)
#define ICLR_TXUEC3	(1 << 7)
#define ICLR_TXUEC2	(1 << 6)
#define ICLR_TXUEC1	(1 << 5)
#define ICLR_RXOEC4	(1 << 4)
#define ICLR_RXOEC3	(1 << 3)
#define ICLR_RXOEC2	(1 << 2)
#define ICLR_RXOEC1	(1 << 1)
#define ICLR_WISC	(1 << 0)

/*
 * Main control register bits. P62
 */
#define MAINCR_SCRA(x)	((x) << 10)	/* secondary codec reg access */
#define MAINCR_DMAEN	(1 << 9)	/* dma enable */
#define MAINCR_SL12TXEN	(1 << 8)	/* slot 12 transmit enable */
#define MAINCR_SL12RXEN	(1 << 7)	/* slot 12 receive enable */
#define MAINCR_SL2TXEN	(1 << 6)	/* slot 2 transmit enable */
#define MAINCR_SL2RXEN	(1 << 5)	/* slot 2 receive enable */
#define MAINCR_SL1TXEN	(1 << 4)	/* slot 1 transmit enable */
#define MAINCR_SL1RXEN	(1 << 3)	/* slot 1 receive enable */
#define MAINCR_LPM	(1 << 2)	/* low power mode */
#define MAINCR_LOOPBK	(1 << 1)	/* loopback */
#define MAINCR_IE	(1 << 0)	/* aaci interface enable */

/*
 * Reset register bits. P65
 */
#define RESET_NRST	(1 << 0)

/*
 * Sync register bits. P65
 */
#define SYNC_FORCE	(1 << 0)

/*
 * Main flag register bits. P66
 */
#define MAINFR_TXB	(1 << 1)	/* transmit busy */
#define MAINFR_RXB	(1 << 0)	/* receive busy */



struct aaci_runtime {
	void			__iomem *base;
	void			__iomem *fifo;

	struct ac97_pcm		*pcm;
	int			pcm_open;

	u32			cr;
	struct snd_pcm_substream	*substream;

	/*
	 * PIO support
	 */
	void			*start;
	void			*end;
	void			*ptr;
	int			bytes;
	unsigned int		period;
	unsigned int		fifosz;
};

struct aaci {
	struct amba_device	*dev;
	struct snd_card		*card;
	void			__iomem *base;
	unsigned int		fifosize;

	/* AC'97 */
	struct mutex		ac97_sem;
	struct snd_ac97_bus	*ac97_bus;
	struct snd_ac97		*ac97;

	u32			maincr;
	spinlock_t		lock;

	struct aaci_runtime	playback;
	struct aaci_runtime	capture;

	struct snd_pcm		*pcm;
};

#define ACSTREAM_FRONT		0
#define ACSTREAM_SURROUND	1
#define ACSTREAM_LFE		2

#endif
