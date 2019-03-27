/*-
 * Defines for Cronyx-Tau adapter, based on Hitachi HD64570 controller.
 *
 * Copyright (C) 1996 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: ctaureg.h,v 1.1.2.1 2003/11/12 17:16:10 rik Exp $
 * $FreeBSD$
 */

/*
 * Chip register address, B is chip base port, R is chip register number.
 */
#define R(b,r)	((b) | 0x8000 | (((r)<<6 & 0x3c00) | ((r) & 0xf)))

/*
 * Interface board registers, R is register number 0..7.
 */
#define GR(p,r) ((p) | 0x0010 | (r)<<1)

/*------------------------------------------------------------
 * Basic Tau model.
 */
#define BSR0(p) (p)		/* board status register 0, read only */
#define BSR1(p) ((p) | 0x2000)	/* board status register 1, read only */
#define BSR2(p) ((p) | 0x4010)	/* board status register 2, read only */
#define BSR3(p) ((p) | 0x4000)	/* board status register 3, read only */
#define BCR0(p) (p)		/* board command register 0, write only */
#define BCR1(p) ((p) | 0x2000)	/* board command register 1, write only */
#define BCR2(p) ((p) | 0x4010)	/* board command register 2, write only */
#define BCR3(p) ((p) | 0x4000)	/* board command register 3, write only */
#define IACK(p) ((p) | 0x6000)	/* interrupt acknowledge register, ro */

/*
 * Board status register 0 bits.
 */
#define BSR0_INTR	0x01	/* interrupt pending flag */
#define BSR0_HDINT	0x02	/* HD64570 interrupt pending */
#define BSR0_GINT	0x04	/* interface board interrupt pending */
#define BSR0_RDYERR	0x10	/* HD64570 reg.i/o error - not ready */

#define BSR0_TE1	0x02	/* 0 - E1 daughter board installed */
#define BSR0_T703	0x04	/* 0 - G.703 daughter board installed */

/*
 * Board status register 1 bits.
 */
#define BSR1_DSR0	0x01	/* DSR from channel 0 */
#define BSR1_DSR1	0x02	/* DSR from channel 1 */

#define BSR1_CH0_CABLE	0x0c	/* channel 0 cable type mask */
#define BSR1_CH0_V35	0x0c	/* channel 0 is V.35 */
#define BSR1_CH0_RS232	0x08	/* channel 0 is RS-232 or not connected */
#define BSR1_CH0_X21	0x04	/* channel 0 is X.21 */
#define BSR1_CH0_RS530	0x00	/* channel 0 is RS-530 */

#define BSR1_CH1_CABLE	0x30	/* channel 1 cable type mask */
#define BSR1_CH1_SHIFT	2
#define BSR1_CH1_V35	0x0c	/* channel 1 is V.35 */
#define BSR1_CH1_RS232	0x08	/* channel 1 is RS-232 or not connected */
#define BSR1_CH1_X21	0x04	/* channel 1 is X.21 */
#define BSR1_CH1_RS530	0x00	/* channel 1 is RS-530 */

/*
 * Board status register 2 bits.
 */
#define BSR2_GINT0	0x08	/* interface board chan0 interrupt pending */
#define BSR2_GINT1	0x40	/* interface board chan1 interrupt pending */
#define BSR2_LERR	0x80	/* firmware download error signal */

/*
 * Board status register 3 bits.
 */
#define BSR3_IB 	0x08	/* identification bit */
#define BSR3_NSTATUS	0x10	/* firmware download status */
#define BSR3_CONF_DN	0x20	/* firmware download done */
#define BSR3_IB_NEG	0x40	/* negated identification bit */
#define BSR3_ZERO	0x80	/* always zero */

/*
 * Board control register 0 bits.
 */
#define BCR0_IRQ_DIS	0x00	/* no interrupt generated */
#define BCR0_IRQ_3	0x01	/* select IRQ number 3 */
#define BCR0_IRQ_5	0x02	/* select IRQ number 5 */
#define BCR0_IRQ_7	0x03	/* select IRQ number 7 */
#define BCR0_IRQ_10	0x04	/* select IRQ number 10 */
#define BCR0_IRQ_11	0x05	/* select IRQ number 11 */
#define BCR0_IRQ_12	0x06	/* select IRQ number 12 */
#define BCR0_IRQ_15	0x07	/* select IRQ number 15 */
#define BCR0_IRQ_MASK	0x07	/* IRQ mask */

#define BCR0_HDRUN	0x08	/* inverted board reset flag */

#define BCR0_DMA_DIS	0x00	/* no interrupt generated */
#define BCR0_DMA_5	0x10	/* select DMA channel 5 */
#define BCR0_DMA_6	0x20	/* select DMA channel 6 */
#define BCR0_DMA_7	0x30	/* select DMA channel 7 */

#define BCR0_TCK	0x80	/* firmware download TCK signal */

/*
 * Board control register 1 bits.
 */
#define BCR1_DTR0	0x01	/* channel 0 DTR enable */
#define BCR1_DTR1	0x02	/* channel 1 DTR enable */

#define BCR1_TXCOUT0	0x10	/* channel 0 TXCOUT enable */
#define BCR1_TXCOUT1	0x20	/* channel 1 TXCOUT enable */

#define BCR1_TMS	0x08	/* firmware download TMS signal */
#define BCR1_TDI	0x80	/* firmware download TDI signal */

#define BCR1_NCONFIGI	0x08	/* firmware download start */
#define BCR1_DCLK	0x40	/* firmware download clock */
#define BCR1_1KDAT	0x80	/* firmware download data */

/*
 * Board control register 2 bits -- see ctau.h.
 */

#define IMVR(b)       R(b,HD_IMVR)	/* interrupt modified vector reg. */
#define ITCR(b)       R(b,HD_ITCR)	/* interrupt control register */
#define ISR0(b)       R(b,HD_ISR0)	/* interrupt status register 0, ro */
#define ISR1(b)       R(b,HD_ISR1)	/* interrupt status register 1, ro */
#define ISR2(b)       R(b,HD_ISR2)	/* interrupt status register 2, ro */
#define IER0(b)       R(b,HD_IER0)	/* interrupt enable register 0 */
#define IER1(b)       R(b,HD_IER1)	/* interrupt enable register 1 */
#define IER2(b)       R(b,HD_IER2)	/* interrupt enable register 2 */
#define PCR(b)	      R(b,HD_PCR)	/* DMA priority control register */
#define DMER(b)       R(b,HD_DMER)	/* DMA master enable register */
#define WCRL(b)       R(b,HD_WCRL)	/* wait control register L */
#define WCRM(b)       R(b,HD_WCRM)	/* wait control register M */
#define WCRH(b)       R(b,HD_WCRH)	/* wait control register H */

/*------------------------------------------------------------
 * Tau/E1 model.
 */
#define E1CFG(p)	GR(p,0) 	/* control register 0, write only */
#define E1SR(p) 	GR(p,0) 	/* status register, read only */
#define E1CS2(p)	GR(p,1) 	/* chip select 2/IACK, read/write */
#define E1SYN(p)	GR(p,3) 	/* sync mode enable, write only */
#define E1CS0(p)	GR(p,4) 	/* chip select 0, write only */
#define E1CS1(p)	GR(p,5) 	/* chip select 1, write only */
#define E1DAT(p)	GR(p,7) 	/* selected chip read/write */

/*
 * Tau/E1 CS2/IACK register bits.
 */
#define E1CS2_IACK	0x08	/* serial controller interrupt acknowledge */
#define E1CS2_SCC	0x04	/* serial controller select */
#define E1CS2_AB	0x02	/* serial controller A/B signal */
#define E1CS2_DC	0x01	/* serial controller D/C signal */

/*
 * Tau/E1 control register bits.
 */
#define E1CFG_II	 0x00	/* configuration II */
#define E1CFG_K 	 0x01	/* configuration K */
#define E1CFG_HI	 0x02	/* configuration HI */
#define E1CFG_D 	 0x03	/* configuration D */

#define E1CFG_CLK0_INT	 0x00	/* channel E0 transmit clock - internal */
#define E1CFG_CLK0_RCV	 0x04	/* channel E0 transmit clock - RCLK0 */
#define E1CFG_CLK0_RCLK1 0x08	/* channel E0 transmit clock - RCLK1 */

#define E1CFG_CLK1_INT	 0x00	/* channel E1 transmit clock - internal */
#define E1CFG_CLK1_RCLK0 0x10	/* channel E1 transmit clock - RCLK0 */
#define E1CFG_CLK1_RCV	 0x20	/* channel E1 transmit clock - RCLK1 */

#define E1CFG_LED	 0x40	/* LED control */
#define E1CFG_GRUN	 0x80	/* global run flag */

/*
 * Tau/E1 sync control register bits.
 */
#define E1SYN_ENS0	0x01	/* enable channel 0 sync mode */
#define E1SYN_ENS1	0x02	/* enable channel 1 sync mode */

/*
 * Tau/E1 status register bits.
 */
#define E1SR_E0_IRQ0	0x01	/* E0 controller interrupt 0 */
#define E1SR_E0_IRQ1	0x02	/* E0 controller interrupt 1 */
#define E1SR_E1_IRQ0	0x04	/* E1 controller interrupt 0 */
#define E1SR_E1_IRQ1	0x08	/* E1 controller interrupt 1 */
#define E1SR_SCC_IRQ	0x10	/* serial controller interrupt */
#define E1SR_TP0	0x20	/* channel 0 is twisted pair */
#define E1SR_TP1	0x40	/* channel 1 is twisted pair */
#define E1SR_REV	0x80	/* Tau/E1 revision */

/*
 * Tau/E1 serial memory register bits.
 */

/*------------------------------------------------------------
 * Tau/G.703 model.
 */
#define GLCR0(p)	GR(p,3)      /* line control register 0, write only */
#define GMD0(p) 	GR(p,4)      /* mode register 0, write only */
#define GMD1(p) 	GR(p,5)      /* mode register 1, write only */
#define GMD2(p) 	GR(p,6)      /* mode register 2, write only */
#define GLCR1(p)	GR(p,7)      /* line control register 1, write only */
#define GERR(p) 	GR(p,0)      /* error register, read/write */
#define GLQ(p)		GR(p,1)      /* line quality register, read only */
#define GLDR(p) 	GR(p,2)      /* loop detect request, read only */

/*
 * Tau/G.703 mode register 0/1 bits.
 */
#define GMD_2048	0x00	/* 2048 kbit/sec */
#define GMD_1024	0x02	/* 1024 kbit/sec */
#define GMD_512 	0x03	/* 512 kbit/sec */
#define GMD_256 	0x04	/* 256 kbit/sec */
#define GMD_128 	0x05	/* 128 kbit/sec */
#define GMD_64		0x06	/* 64 kbit/sec */

#define GMD_RSYNC	0x08	/* receive synchronization */
#define GMD_PCE_PCM2	0x10	/* precoder enable, mode PCM2 */
#define GMD_PCE_PCM2D	0x20	/* precoder enable, mode PCM2D */

#define GMD0_SDI	0x40	/* serial data input */
#define GMD0_SCLK	0x80	/* serial data clock */

#define GMD1_NCS0	0x40	/* chip select 0 inverted */
#define GMD1_NCS1	0x80	/* chip select 1 inverted */

/*
 * Tau/G.703 mode register 2 bits.
 */
#define GMD2_SERIAL	0x01	/* channel 1 serial interface V.35/RS-232/etc */
#define GMD2_LED	0x02	/* LED control */
#define GMD2_RAW0	0x04	/* channel 0 raw mode (byte-sync) */
#define GMD2_RAW1	0x08	/* channel 1 raw mode (byte-sync) */

/*
 * Tau/G.703 interrupt status register bits.
 */
#define GERR_BPV0	 0x01	 /* channel 0 bipolar violation */
#define GERR_ERR0	 0x02	 /* channel 0 test error */
#define GERR_BPV1	 0x04	 /* channel 1 bipolar violation */
#define GERR_ERR1	 0x08	 /* channel 1 test error */

/*
 * Tau/G.703 line quality register bits.
 */
#define GLQ_MASK     0x03    /* channel 0 mask */
#define GLQ_SHIFT    2	     /* channel 1 shift */

#define GLQ_DB0      0x00    /* channel 0 level 0.0 dB */
#define GLQ_DB95     0x01    /* channel 0 level -9.5 dB */
#define GLQ_DB195    0x02    /* channel 0 level -19.5 dB */
#define GLQ_DB285    0x03    /* channel 0 level -28.5 dB */

/*
 * Tau/G.703 serial data output register bits.
 */
#define GLDR_C0 	0x01	/* chip 0 serial data output */
#define GLDR_LREQ0	0x02	/* channel 0 remote loop request */
#define GLDR_C1 	0x04	/* chip 1 serial data output */
#define GLDR_LREQ1	0x08	/* channel 1 remote loop request */

/*
 * Tau/G.703 line control register 0/1 bits.
 */
#define GLCR_RENABLE	0x00	/* normal mode, auto remote loop enabled */
#define GLCR_RDISABLE	0x01	/* normal mode, auto remote loop disabled */
#define GLCR_RREFUSE	0x02	/* send the remote loop request sequence */
#define GLCR_RREQUEST	0x03	/* send the remote loop refuse sequence */
