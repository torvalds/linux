/*-
 * Defines for Cronyx-Sigma adapter, based on Cirrus Logic multiprotocol
 * controller RISC processor CL-CD2400/2401.
 *
 * Copyright (C) 1994-2000 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: cxreg.h,v 1.1.2.1 2003/11/12 17:13:41 rik Exp $
 * $FreeBSD$
 */
#define REVCL_MIN   7		/* CD2400 min. revision number G */
#define REVCL_MAX   13		/* CD2400 max. revision number M */
#define REVCL31_MIN 0x33	/* CD2431 min. revision number C */
#define REVCL31_MAX 0x34	/* CD2431 max. revision number D */

#define BRD_INTR_LEVEL 0x5a	/* interrupt level (arbitrary PILR value) */

#define CS0(p)	((p) | 0x8000)	/* chip select 0 */
#define CS1(p)	((p) | 0xc000)	/* chip select 1 */
#define CS1A(p)	((p) | 0x8010)	/* chip select 1 for agp-compatible models */
#define BSR(p)	(p)		/* board status register, read only */
#define BCR0(p)	(p)		/* board command register 0, write only */
#define BCR1(p)	((p) | 0x2000)	/* board command register 1, write only */

/*
 * For Sigma-800 only.
 */
#define BDET(p)	((p) | 0x2000)	/* board detection register, read only */
#define BCR2(p)	((p) | 0x4000)	/* board command register 2, write only */

/*
 * Chip register address, B is chip base port, R is chip register number.
 */
#define R(b,r) ((b) | (((r)<<6 & 0x3c00) | ((r) & 0xf)))

/*
 * Interrupt acknowledge register, P is board port, L is interrupt level,
 * as prodrammed in PILR.
 */
#define IACK(p,l)   (R(p,l) | 0x4000)

/*
 * Global registers.
 */
#define GFRCR(b)    R(b,0x82)	/* global firmware revision code register */
#define CAR(b)      R(b,0xec)	/* channel access register */

/*
 * Option registers.
 */
#define CMR(b)      R(b,0x18)	/* channel mode register */
#define COR1(b)     R(b,0x13)	/* channel option register 1 */
#define COR2(b)     R(b,0x14)	/* channel option register 2 */
#define COR3(b)     R(b,0x15)	/* channel option register 3 */
#define COR4(b)     R(b,0x16)	/* channel option register 4 */
#define COR5(b)     R(b,0x17)	/* channel option register 5 */
#define COR6(b)     R(b,0x1b)	/* channel option register 6 */
#define COR7(b)     R(b,0x04)	/* channel option register 7 */
#define SCHR1(b)    R(b,0x1c)	/* special character register 1 */
#define SCHR2(b)    R(b,0x1d)	/* special character register 2 */
#define SCHR3(b)    R(b,0x1e)	/* special character register 3 */
#define SCHR4(b)    R(b,0x1f)	/* special character register 4 */
#define SCRL(b)     R(b,0x20)	/* special character range low */
#define SCRH(b)     R(b,0x21)	/* special character range high */
#define LNXT(b)     R(b,0x2d)	/* LNext character */
#define RFAR1(b)    R(b,0x1c)	/* receive frame address register 1 */
#define RFAR2(b)    R(b,0x1d)	/* receive frame address register 2 */
#define RFAR3(b)    R(b,0x1e)	/* receive frame address register 3 */
#define RFAR4(b)    R(b,0x1f)	/* receive frame address register 4 */
#define CPSR(b)     R(b,0xd4)	/* CRC polynomial select register */

/*
 * Bit rate and clock option registers.
 */
#define RBPR(b)     R(b,0xc9)	/* receive baud rate period register */
#define RCOR(b)     R(b,0xca)	/* receive clock option register */
#define TBPR(b)     R(b,0xc1)	/* transmit baud rate period register */
#define TCOR(b)     R(b,0xc2)	/* receive clock option register */

/*
 * Channel command and status registers.
 */
#define CCR(b)      R(b,0x10)	/* channel command register */
#define STCR(b)     R(b,0x11)	/* special transmit command register */
#define CSR(b)      R(b,0x19)	/* channel status register */
#define MSVR(b)     R(b,0xdc)	/* modem signal value register */
#define MSVR_RTS(b) R(b,0xdc)	/* modem RTS setup register */
#define MSVR_DTR(b) R(b,0xdd)	/* modem DTR setup register */

/*
 * Interrupt registers.
 */
#define LIVR(b)     R(b,0x0a)	/* local interrupt vector register */
#define IER(b)      R(b,0x12)	/* interrupt enable register */
#define LICR(b)     R(b,0x25)	/* local interrupting channel register */
#define STK(b)      R(b,0xe0)	/* stack register */

/*
 * Receive interrupt registers.
 */
#define RPILR(b)    R(b,0xe3)	/* receive priority interrupt level register */
#define RIR(b)      R(b,0xef)	/* receive interrupt register */
#define RISR(b)     R(b,0x8a)	/* receive interrupt status register */
#define RISRL(b)    R(b,0x8a)	/* receive interrupt status register low */
#define RISRH(b)    R(b,0x8b)	/* receive interrupt status register high */
#define RFOC(b)     R(b,0x33)	/* receive FIFO output count */
#define RDR(b)      R(b,0xf8)	/* receive data register */
#define REOIR(b)    R(b,0x87)	/* receive end of interrupt register */

/*
 * Transmit interrupt registers.
 */
#define TPILR(b)    R(b,0xe2)	/* transmit priority interrupt level reg */
#define TIR(b)      R(b,0xee)	/* transmit interrupt register */
#define TISR(b)     R(b,0x89)	/* transmit interrupt status register */
#define TFTC(b)     R(b,0x83)	/* transmit FIFO transfer count */
#define TDR(b)      R(b,0xf8)	/* transmit data register */
#define TEOIR(b)    R(b,0x86)	/* transmit end of interrupt register */

/*
 * Modem interrupt registers.
 */
#define MPILR(b)    R(b,0xe1)	/* modem priority interrupt level register */
#define MIR(b)      R(b,0xed)	/* modem interrupt register */
#define MISR(b)     R(b,0x88)	/* modem/timer interrupt status register */
#define MEOIR(b)    R(b,0x85)	/* modem end of interrupt register */

/*
 * DMA registers.
 */
#define DMR(b)      R(b,0xf4)	/* DMA mode register */
#define BERCNT(b)   R(b,0x8d)	/* bus error retry count */
#define DMABSTS(b)  R(b,0x1a)	/* DMA buffer status */

/*
 * DMA receive registers.
 */
#define ARBADRL(b)  R(b,0x40)	/* A receive buffer address lower */
#define ARBADRU(b)  R(b,0x42)	/* A receive buffer address upper */
#define BRBADRL(b)  R(b,0x44)	/* B receive buffer address lower */
#define BRBADRU(b)  R(b,0x46)	/* B receive buffer address upper */
#define ARBCNT(b)   R(b,0x48)	/* A receive buffer byte count */
#define BRBCNT(b)   R(b,0x4a)	/* B receive buffer byte count */
#define ARBSTS(b)   R(b,0x4c)	/* A receive buffer status */
#define BRBSTS(b)   R(b,0x4d)	/* B receive buffer status */
#define RCBADRL(b)  R(b,0x3c)	/* receive current buffer address lower */
#define RCBADRU(b)  R(b,0x3e)	/* receive current buffer address upper */

/*
 * DMA transmit registers.
 */
#define ATBADRL(b)  R(b,0x50)	/* A transmit buffer address lower */
#define ATBADRU(b)  R(b,0x52)	/* A transmit buffer address upper */
#define BTBADRL(b)  R(b,0x54)	/* B transmit buffer address lower */
#define BTBADRU(b)  R(b,0x56)	/* B transmit buffer address upper */
#define ATBCNT(b)   R(b,0x58)	/* A transmit buffer byte count */
#define BTBCNT(b)   R(b,0x5a)	/* B transmit buffer byte count */
#define ATBSTS(b)   R(b,0x5c)	/* A transmit buffer status */
#define BTBSTS(b)   R(b,0x5d)	/* B transmit buffer status */
#define TCBADRL(b)  R(b,0x38)	/* transmit current buffer address lower */
#define TCBADRU(b)  R(b,0x3a)	/* transmit current buffer address upper */

/*
 * Timer registers.
 */
#define TPR(b)      R(b,0xd8)	/* timer period register */
#define RTPR(b)     R(b,0x26)	/* receive timeout period register */
#define RTPRL(b)    R(b,0x26)	/* receive timeout period register low */
#define RTPTH(b)    R(b,0x27)	/* receive timeout period register high */
#define GT1(b)      R(b,0x28)	/* general timer 1 */
#define GT1L(b)     R(b,0x28)	/* general timer 1 low */
#define GT1H(b)     R(b,0x29)	/* general timer 1 high */
#define GT2(b)      R(b,0x2a)	/* general timer 2 */
#define TTR(b)      R(b,0x2a)	/* transmit timer register */

/*
 * Board status register bits, for all models.
 */
#define BSR_NOINTR     0x01	/* no interrupt pending flag */
#define BSR_NOCHAIN    0x80	/* no daisy chained board, all but Sigma-22 */

/*
 * For old Sigmas only.
 */
#define BSR_VAR_MASK   0x66	/* adapter variant mask */
#define BSR_OSC_MASK   0x18	/* oscillator frequency mask */
#define BSR_OSC_20     0x18	/* 20 MHz */
#define BSR_OSC_18432  0x10	/* 18.432 MHz */

#define BSR_NODSR(n)   (0x100 << (n))	/* DSR from channels 0-3, inverted */
#define BSR_NOCD(n)    (0x1000 << (n))	/* CD from channels 0-3, inverted */

/*
 * Board status register bits for Sigma-2x.
 */
#define BSR2X_OSC_33     0x08	/* oscillator 33/20 MHz bit */
#define BSR2X_VAR_MASK   0x30	/* Sigma-2x variant mask */

/*
 * Board status register bits for Sigma-800.
 */
#define BSR800_NU0       0x02	/* no channels 0-3 installed */
#define BSR800_NU1       0x04	/* no channels 4-7 installed */
#define BSR800_LERR      0x08	/* firmware load error */
#define BSR800_MIRQ      0x10	/* modem IRQ active */
#define BSR800_TIRQ      0x20	/* transmit IRQ active */
#define BSR800_RIRQ      0x40	/* receive IRQ active */

#define BDET_IB          0x08	/* identification bit */
#define BDET_IB_NEG      0x80	/* negated identification bit */

/*
 * Sigma-800 control register 2 bits.
 */
#define BCR2_BUS0       0x01	/* bus timing control */
#define BCR2_BUS1       0x02	/* bus timing control */
#define BCR2_TMS  	0x08	/* firmware download signal */
#define BCR2_TDI  	0x80	/* firmware download signal */

/*
 * Board revision mask.
 */
#define BSR_REV_MASK     (BSR_OSC_MASK|BSR_VAR_MASK|BSR_NOCHAIN)
#define BSR2X_REV_MASK   (BSR_OSC_MASK|BSR_VAR_MASK)

/*
 * Sigma-2x variants.
 */
#define CRONYX_22	0x20
#define CRONYX_24	0x00

/*
 * Sigma-XXX variants.
 */
#define CRONYX_100	0x64
#define CRONYX_400	0x62
#define CRONYX_500	0x60
#define CRONYX_410	0x24
#define CRONYX_810	0x20
#define CRONYX_410s	0x04
#define CRONYX_810s	0x00
#define CRONYX_440	0x44
#define CRONYX_840	0x40
#define CRONYX_401	0x26
#define CRONYX_801	0x22
#define CRONYX_401s	0x06
#define CRONYX_801s	0x02
#define CRONYX_404	0x46
#define CRONYX_703	0x42

/*
 * Board control register 0 bits.
 */
#define BCR0_IRQ_DIS  0x00	/* no interrupt generated */
#define BCR0_IRQ_3    0x01	/* select IRQ number 3 */
#define BCR0_IRQ_5    0x02	/* select IRQ number 5 */
#define BCR0_IRQ_7    0x03	/* select IRQ number 7 */
#define BCR0_IRQ_10   0x04	/* select IRQ number 10 */
#define BCR0_IRQ_11   0x05	/* select IRQ number 11 */
#define BCR0_IRQ_12   0x06	/* select IRQ number 12 */
#define BCR0_IRQ_15   0x07	/* select IRQ number 15 */
#define BCR0_IRQ_MASK 0x07	/* irq select mask */

#define BCR0_DMA_DIS  0x00	/* no interrupt generated */
#define BCR0_DMA_5    0x10	/* select DMA channel 5 */
#define BCR0_DMA_6    0x20	/* select DMA channel 6 */
#define BCR0_DMA_7    0x30	/* select DMA channel 7 */
#define BCR0_DMA_MASK 0x30	/* drq select mask */

/* For old Sigmas only. */
#define BCR0_NORESET  0x08	/* CD2400 reset flag (inverted) */

#define BCR0_UM_ASYNC 0x00	/* channel 0 mode - async */
#define BCR0_UM_SYNC  0x80	/* channel 0 mode - sync */
#define BCR0_UI_RS232 0x00	/* channel 0 interface - RS-232 */
#define BCR0_UI_RS449 0x40	/* channel 0 interface - RS-449/V.35 */
#define BCR0_UMASK    0xc0	/* channel 0 interface mask */

/* For Sigma-22 only. */
#define BCR02X_FAST   0x40	/* fast bus timing */
#define BCR02X_LED    0x80	/* LED control */

/* For Sigma-800 only. */
#define BCR0800_TCK   0x80	/* firmware download signal */

/*
 * Board control register 1 bits.
 */
/* For old Sigmas only. */
#define BCR1_DTR(n)    (0x100 << (n))	/* DTR for channels 0-3 sync */

/* For Sigma-800 only. */
#define BCR1800_DTR(n) (1 << ((n) & 7))	/* DTR for channels 0-7 sync */

/*
 * Channel commands (CCR).
 */
#define CCR_CLRCH  0x40		/* clear channel */
#define CCR_INITCH 0x20		/* initialize channel */
#define CCR_RSTALL 0x10		/* reset all channels */
#define CCR_ENTX   0x08		/* enable transmitter */
#define CCR_DISTX  0x04		/* disable transmitter */
#define CCR_ENRX   0x02		/* enable receiver */
#define CCR_DISRX  0x01		/* disable receiver */
#define CCR_CLRT1  0xc0		/* clear timer 1 */
#define CCR_CLRT2  0xa0		/* clear timer 2 */
#define CCR_CLRRCV 0x90		/* clear receiver */
#define CCR_CLRTX  0x88		/* clear transmitter */

/*
 * Interrupt enable register (IER) bits.
 */
#define IER_MDM    0x80		/* modem status changed */
#define IER_RET    0x20		/* receive exception timeout */
#define IER_RXD    0x08		/* data received */
#define IER_TIMER  0x04		/* timer expired */
#define IER_TXMPTY 0x02		/* transmitter empty */
#define IER_TXD    0x01		/* data transmitted */

/*
 * Modem signal values register bits (MSVR).
 */
#define MSV_DSR    0x80		/* state of Data Set Ready input */
#define MSV_CD     0x40		/* state of Carrier Detect input */
#define MSV_CTS    0x20		/* state of Clear to Send input */
#define MSV_TXCOUT 0x10		/* TXCout/DTR pin output flag */
#define MSV_PORTID 0x04		/* device is CL-CD2401 (not 2400) */
#define MSV_DTR    0x02		/* state of Data Terminal Ready output */
#define MSV_RTS    0x01		/* state of Request to Send output */
#define MSV_BITS "\20\1rts\2dtr\3cd2400\5txcout\6cts\7cd\10dsr"

/*
 * DMA buffer status register bits (DMABSTS).
 */
#define DMABSTS_TDALIGN 0x80	/* internal data alignment in transmit FIFO */
#define DMABSTS_RSTAPD  0x40	/* reset append mode */
#define DMABSTS_CRTTBUF 0x20	/* internal current transmit buffer in use */
#define DMABSTS_APPEND  0x10	/* append buffer is in use */
#define DMABSTS_NTBUF   0x08	/* next transmit buffer is B (not A) */
#define DMABSTS_TBUSY   0x04	/* current transmit buffer is in use */
#define DMABSTS_NRBUF   0x02	/* next receive buffer is B (not A) */
#define DMABSTS_RBUSY   0x01	/* current receive buffer is in use */

/*
 * Buffer status register bits ([AB][RT]BSTS).
 */
#define BSTS_BUSERR 0x80	/* bus error */
#define BSTS_EOFR   0x40	/* end of frame */
#define BSTS_EOBUF  0x20	/* end of buffer */
#define BSTS_APPEND 0x08	/* append mode */
#define BSTS_INTR   0x02	/* interrupt required */
#define BSTS_OWN24  0x01	/* buffer is (free to be) used by CD2400 */
#define BSTS_BITS "\20\1own24\2intr\4append\6eobuf\7eofr\10buserr"

/*
 * Receive interrupt status register (RISR) bits.
 */
#define RIS_OVERRUN   0x0008	/* overrun error */
#define RIS_BB        0x0800	/* buffer B status (not A) */
#define RIS_EOBUF     0x2000	/* end of buffer reached */
#define RIS_EOFR      0x4000	/* frame reception complete */
#define RIS_BUSERR    0x8000	/* bus error */

#define RISH_CLRDCT   0x0001	/* X.21 clear detect */
#define RISH_RESIND   0x0004	/* residual indication */
#define RISH_CRCERR   0x0010	/* CRC error */
#define RISH_RXABORT  0x0020	/* abort sequence received */
#define RISH_EOFR     0x0040	/* complete frame received */
#define RISH_BITS "\20\1clrdct\3resind\4overrun\5crcerr\6rxabort\7eofr\14bb\16eobuf\17eofr\20buserr"

#define RISA_BREAK    0x0001	/* break signal detected */
#define RISA_FRERR    0x0002	/* frame error (bad stop bits) */
#define RISA_PARERR   0x0004	/* parity error */
#define RISA_SCMASK   0x0070	/* special character detect mask */
#define RISA_SCHR1    0x0010	/* special character 1 detected */
#define RISA_SCHR2    0x0020	/* special character 2 detected */
#define RISA_SCHR3    0x0030	/* special character 3 detected */
#define RISA_SCHR4    0x0040	/* special character 4 detected */
#define RISA_SCRANGE  0x0070	/* special character in range detected */
#define RISA_TIMEOUT  0x0080	/* receive timeout, no data */
#define RISA_BITS "\20\1break\2frerr\3parerr\4overrun\5schr1\6schr2\7schr4\10timeout\14bb\16eobuf\17eofr\20buserr"

#define RISB_CRCERR   0x0010	/* CRC error */
#define RISB_RXABORT  0x0020	/* abort sequence received */
#define RISB_EOFR     0x0040	/* complete frame received */

#define RISX_LEADCHG  0x0001	/* CTS lead change */
#define RISX_PARERR   0x0004	/* parity error */
#define RISX_SCMASK   0x0070	/* special character detect mask */
#define RISX_SCHR1    0x0010	/* special character 1 detected */
#define RISX_SCHR2    0x0020	/* special character 2 detected */
#define RISX_SCHR3    0x0030	/* special character 3 detected */
#define RISX_ALLZERO  0x0040	/* all 0 condition detected */
#define RISX_ALLONE   0x0050	/* all 1 condition detected */
#define RISX_ALTOZ    0x0060	/* alternating 1 0 condition detected */
#define RISX_SYN      0x0070	/* SYN detected */
#define RISX_LEAD     0x0080	/* leading value */

/*
 * Channel mode register (CMR) bits.
 */
#define CMR_RXDMA     0x80	/* DMA receive transfer mode */
#define CMR_TXDMA     0x40	/* DMA transmit transfer mode */
#define CMR_HDLC      0x00	/* HDLC protocol mode */
#define CMR_BISYNC    0x01	/* BISYNC protocol mode */
#define CMR_ASYNC     0x02	/* ASYNC protocol mode */
#define CMR_X21       0x03	/* X.21 protocol mode */

/*
 * Modem interrupt status register (MISR) bits.
 */
#define MIS_CDSR      0x80	/* DSR changed */
#define MIS_CCD       0x40	/* CD changed */
#define MIS_CCTS      0x20	/* CTS changed */
#define MIS_CGT2      0x02	/* GT2 timer expired */
#define MIS_CGT1      0x01	/* GT1 timer expired */
#define MIS_BITS "\20\1gt1\2gt2\6ccts\7ccd\10cdsr"

/*
 * Transmit interrupt status register (TISR) bits.
 */
#define TIS_BUSERR    0x80	/* Bus error */
#define TIS_EOFR      0x40	/* End of frame */
#define TIS_EOBUF     0x20	/* end of transmit buffer reached */
#define TIS_UNDERRUN  0x10	/* transmit underrun */
#define TIS_BB        0x08	/* buffer B status (not A) */
#define TIS_TXEMPTY   0x02	/* transmitter empty */
#define TIS_TXDATA    0x01	/* transmit data below threshold */
#define TIS_BITS "\20\1txdata\2txempty\4bb\5underrun\6eobuf\7eofr\10buserr"

/*
 * Local interrupt vector register (LIVR) bits.
 */
#define LIV_EXCEP     0
#define LIV_MODEM     1
#define LIV_TXDATA    2
#define LIV_RXDATA    3

/*
 * Transmit end of interrupt registers (TEOIR) bits.
 */
#define TEOI_TERMBUFF  0x80	/* force current buffer to be discarded */
#define TEOI_EOFR      0x40	/* end of frame in interrupt mode */
#define TEOI_SETTM2    0x20	/* set general timer 2 in sync mode */
#define TEOI_SETTM1    0x10	/* set general timer 1 in sync mode */
#define TEOI_NOTRANSF  0x08	/* no transfer of data on this interrupt */

/*
 * Receive end of interrupt registers (REOIR) bits.
 */
#define REOI_TERMBUFF  0x80	/* force current buffer to be terminated */
#define REOI_DISCEXC   0x40	/* discard exception character */
#define REOI_SETTM2    0x20	/* set general timer 2 */
#define REOI_SETTM1    0x10	/* set general timer 1 */
#define REOI_NOTRANSF  0x08	/* no transfer of data */
#define REOI_GAP_MASK  0x07	/* optional gap size to leave in buffer */

/*
 * Special transmit command register (STCR) bits.
 */
#define STC_ABORTTX   0x40	/* abort transmission (HDLC mode) */
#define STC_APPDCMP   0x20	/* append complete (async DMA mode) */
#define STC_SNDSPC    0x08	/* send special characters (async mode) */
#define STC_SSPC_MASK 0x07	/* special character select */
#define STC_SSPC_1    0x01	/* send special character #1 */
#define STC_SSPC_2    0x02	/* send special character #2 */
#define STC_SSPC_3    0x03	/* send special character #3 */
#define STC_SSPC_4    0x04	/* send special character #4 */

/*
 * Channel status register (CSR) bits, asynchronous mode.
 */
#define CSRA_RXEN    0x80       /* receiver enable */
#define CSRA_RXFLOFF 0x40       /* receiver flow off */
#define CSRA_RXFLON  0x20       /* receiver flow on */
#define CSRA_TXEN    0x08       /* transmitter enable */
#define CSRA_TXFLOFF 0x04       /* transmitter flow off */
#define CSRA_TXFLON  0x02       /* transmitter flow on */
#define CSRA_BITS "\20\2txflon\3txfloff\4txen\6rxflon\7rxfloff\10rxen"
