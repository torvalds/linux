/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * cyclades cyclom-y serial driver
 *	Andrew Herbert <andrew@werple.apana.org.au>, 17 August 1993
 *
 * Copyright (c) 1993 Andrew Herbert.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name Andrew Herbert may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Definitions for Cirrus Logic CD1400 serial/parallel chips.
 */

#define	CD1400_NO_OF_CHANNELS	4	/* 4 serial channels per chip */
#define	CD1400_RX_FIFO_SIZE	12
#define	CD1400_TX_FIFO_SIZE	12

/*
 * Global registers.
 */
#define	CD1400_GFRCR		0x40	/* global firmware revision code */
#define	CD1400_CAR		0x68	/* channel access */
#define	CD1400_CAR_CHAN			(3<<0)	/* channel select */
#define	CD1400_GCR		0x4B	/* global configuration */
#define	CD1400_GCR_PARALLEL		(1<<7)	/* channel 0 is parallel */
#define	CD1400_SVRR		0x67	/* service request */
#define	CD1400_SVRR_MDMCH		(1<<2)
#define	CD1400_SVRR_TXRDY		(1<<1)
#define	CD1400_SVRR_RXRDY		(1<<0)
#define	CD1400_RICR		0x44	/* receive interrupting channel */
#define	CD1400_TICR		0x45	/* transmit interrupting channel */
#define	CD1400_MICR		0x46	/* modem interrupting channel */
#define	CD1400_RIR		0x6B	/* receive interrupt status */
#define	CD1400_RIR_RDIREQ		(1<<7)	/* rx service required */
#define	CD1400_RIR_RBUSY		(1<<6)	/* rx service in progress */
#define	CD1400_RIR_CHAN			(3<<0)	/* channel select */
#define	CD1400_TIR		0x6A	/* transmit interrupt status */
#define	CD1400_TIR_RDIREQ		(1<<7)	/* tx service required */
#define	CD1400_TIR_RBUSY		(1<<6)	/* tx service in progress */
#define	CD1400_TIR_CHAN			(3<<0)	/* channel select */
#define	CD1400_MIR		0x69	/* modem interrupt status */
#define	CD1400_MIR_RDIREQ		(1<<7)	/* modem service required */
#define	CD1400_MIR_RBUSY		(1<<6)	/* modem service in progress */
#define	CD1400_MIR_CHAN			(3<<0)	/* channel select */
#define	CD1400_PPR		0x7E	/* prescaler period */
#define	CD1400_PPR_PRESCALER		512

/*
 * Virtual registers.
 */
#define	CD1400_RIVR		0x43	/* receive interrupt vector */
#define	CD1400_RIVR_EXCEPTION		(1<<2)	/* receive exception bit */
#define	CD1400_TIVR		0x42	/* transmit interrupt vector */
#define	CD1400_MIVR		0x41	/* modem interrupt vector */
#define	CD1400_TDR		0x63	/* transmit data */
#define	CD1400_RDSR		0x62	/* receive data/status */
#define	CD1400_RDSR_TIMEOUT		(1<<7)	/* rx timeout */
#define	CD1400_RDSR_SPECIAL_SHIFT	4	/* rx special char shift */
#define	CD1400_RDSR_SPECIAL		(7<<4)	/* rx special char */
#define	CD1400_RDSR_BREAK		(1<<3)	/* rx break */
#define	CD1400_RDSR_PE			(1<<2)	/* rx parity error */
#define	CD1400_RDSR_FE			(1<<1)	/* rx framing error */
#define	CD1400_RDSR_OE			(1<<0)	/* rx overrun error */
#define	CD1400_MISR		0x4C	/* modem interrupt status */
#define	CD1400_MISR_DSRd		(1<<7)	/* DSR delta */
#define	CD1400_MISR_CTSd		(1<<6)	/* CTS delta */
#define	CD1400_MISR_RId			(1<<5)	/* RI delta */
#define	CD1400_MISR_CDd			(1<<4)	/* CD delta */
#define	CD1400_EOSRR		0x60	/* end of service request */

/*
 * Channel registers.
 */
#define	CD1400_LIVR		0x18	/* local interrupt vector */
#define	CD1400_CCR		0x05	/* channel control */
#define	CD1400_CCR_CMDRESET		(1<<7)	/* enables following: */
#define	CD1400_CCR_FTF				(1<<1)	/* flush tx fifo */
#define	CD1400_CCR_FULLRESET			(1<<0)	/* full reset */
#define	CD1400_CCR_CHANRESET			0	/*  current channel */
#define	CD1400_CCR_CMDCORCHG		(1<<6)	/* enables following: */
#define	CD1400_CCR_COR3				(1<<3)	/* COR3 changed */
#define	CD1400_CCR_COR2				(1<<2)	/* COR2 changed */
#define	CD1400_CCR_COR1				(1<<1)	/* COR1 changed */
#define	CD1400_CCR_CMDSENDSC		(1<<5)	/* enables following: */
#define	CD1400_CCR_SC				(7<<0)	/* special char 1-4 */
#define	CD1400_CCR_CMDCHANCTL		(1<<4)	/* enables following: */
#define	CD1400_CCR_XMTEN			(1<<3)	/* tx enable */
#define	CD1400_CCR_XMTDIS			(1<<2)	/* tx disable */
#define	CD1400_CCR_RCVEN			(1<<1)	/* rx enable */
#define	CD1400_CCR_RCVDIS			(1<<0)	/* rx disable */
#define	CD1400_SRER		0x06	/* service request enable */
#define	CD1400_SRER_MDMCH		(1<<7)	/* modem change */
#define	CD1400_SRER_RXDATA		(1<<4)	/* rx data */
#define	CD1400_SRER_TXRDY		(1<<2)	/* tx fifo empty */
#define	CD1400_SRER_TXMPTY		(1<<1)	/* tx shift reg empty */
#define	CD1400_SRER_NNDT		(1<<0)	/* no new data */
#define	CD1400_COR1		0x08	/* channel option 1 */
#define	CD1400_COR1_PARODD		(1<<7)
#define	CD1400_COR1_PARNORMAL		(2<<5)
#define	CD1400_COR1_PARFORCE		(1<<5)	/* odd/even = force 1/0 */
#define	CD1400_COR1_PARNONE		(0<<5)
#define	CD1400_COR1_NOINPCK		(1<<4)
#define	CD1400_COR1_STOP2		(2<<2)
#define	CD1400_COR1_STOP15		(1<<2)	/* 1.5 stop bits */
#define	CD1400_COR1_STOP1		(0<<2)
#define	CD1400_COR1_CS8			(3<<0)
#define	CD1400_COR1_CS7			(2<<0)
#define	CD1400_COR1_CS6			(1<<0)
#define	CD1400_COR1_CS5			(0<<0)
#define	CD1400_COR2		0x09	/* channel option 2 */
#define	CD1400_COR2_IXANY		(1<<7)	/* implied XON mode */
#define	CD1400_COR2_IXOFF		(1<<6)	/* in-band tx flow control */
#define	CD1400_COR2_ETC			(1<<5)	/* embedded tx command */
#define	CD1400_ETC_CMD				0x00	/* start an ETC */
#define	CD1400_ETC_SENDBREAK			0x81
#define	CD1400_ETC_INSERTDELAY			0x82
#define	CD1400_ETC_STOPBREAK			0x83
#define	CD1400_COR2_LLM			(1<<4)	/* local loopback mode */
#define	CD1400_COR2_RLM			(1<<3)	/* remote loopback mode */
#define	CD1400_COR2_RTSAO		(1<<2)	/* RTS auto output */
#define	CD1400_COR2_CCTS_OFLOW		(1<<1)	/* CTS auto enable */
#define	CD1400_COR2_CDSR_OFLOW		(1<<0)	/* DSR auto enable */
#define	CD1400_COR3		0x0A	/* channel option 3 */
#define	CD1400_COR3_SCDRNG		(1<<7)	/* special char detect range */
#define	CD1400_COR3_SCD34		(1<<6)	/* special char detect 3-4 */
#define	CD1400_COR3_FTC			(1<<5)	/* flow control transparency */
#define	CD1400_COR3_SCD12		(1<<4)	/* special char detect 1-2 */
#define	CD1400_COR3_RXTH		(15<<0)	/* rx fifo threshold */
#define	CD1400_COR4		0x1E	/* channel option 4 */
#define	CD1400_COR4_IGNCR		(1<<7)
#define	CD1400_COR4_ICRNL		(1<<6)
#define	CD1400_COR4_INLCR		(1<<5)
#define	CD1400_COR4_IGNBRK		(1<<4)
#define	CD1400_COR4_NOBRKINT		(1<<3)
#define	CD1400_COR4_PFO_ESC		(4<<0)	/* parity/framing/overrun... */
#define	CD1400_COR4_PFO_NUL		(3<<0)
#define	CD1400_COR4_PFO_DISCARD		(2<<0)
#define	CD1400_COR4_PFO_GOOD		(1<<0)
#define	CD1400_COR4_PFO_EXCEPTION	(0<<0)
#define	CD1400_COR5		0x1F	/* channel option 5 */
#define	CD1400_COR5_ISTRIP		(1<<7)
#define	CD1400_COR5_LNEXT		(1<<6)
#define	CD1400_COR5_CMOE		(1<<5)	/* char matching on error */
#define	CD1400_COR5_EBD			(1<<2)	/* end of break detected */
#define	CD1400_COR5_ONLCR		(1<<1)
#define	CD1400_COR5_OCRNL		(1<<0)
#define	CD1400_CCSR		0x0B	/* channel control status */
#define	CD1400_RDCR		0x0E	/* received data count */
#define	CD1400_SCHR1		0x1A	/* special character 1 */
#define	CD1400_SCHR2		0x1B	/* special character 2 */
#define	CD1400_SCHR3		0x1C	/* special character 3 */
#define	CD1400_SCHR4		0x1D	/* special character 4 */
#define	CD1400_SCRL		0x22	/* special character range, low */
#define	CD1400_SCRH		0x23	/* special character range, high */
#define	CD1400_LNC		0x24	/* lnext character */
#define	CD1400_MCOR1		0x15	/* modem change option 1 */
#define	CD1400_MCOR1_DSRzd		(1<<7)	/* DSR one-to-zero delta */
#define	CD1400_MCOR1_CTSzd		(1<<6)
#define	CD1400_MCOR1_RIzd		(1<<5)
#define	CD1400_MCOR1_CDzd		(1<<4)
#define	CD1400_MCOR1_DTRth		(15<<0)	/* dtrflow threshold */
#define	CD1400_MCOR2		0x16	/* modem change option 2 */
#define	CD1400_MCOR2_DSRod		(1<<7)	/* DSR zero-to-one delta */
#define	CD1400_MCOR2_CTSod		(1<<6)
#define	CD1400_MCOR2_RIod		(1<<5)
#define	CD1400_MCOR2_CDod		(1<<4)
#define	CD1400_RTPR		0x21	/* receive timeout period */
#define	CD1400_MSVR1		0x6C	/* modem signal value 1 */
#define	CD1400_MSVR1_RTS		(1<<0)	/* RTS line (r/w) */
#define	CD1400_MSVR2		0x6D	/* modem signal value 2 */
#define	CD1400_MSVR2_DSR		(1<<7)	/* !DSR line (r) */
#define	CD1400_MSVR2_CTS		(1<<6)	/* !CTS line (r) */
#define	CD1400_MSVR2_RI			(1<<5)	/* !RI line (r) */
#define	CD1400_MSVR2_CD			(1<<4)	/* !CD line (r) */
#define	CD1400_MSVR2_DTR		(1<<1)	/* DTR line (r/w) */
#define	CD1400_PSVR		0x6F	/* printer signal value */
#define	CD1400_RBPR		0x78	/* receive baud rate period */
#define	CD1400_RCOR		0x7C	/* receive clock option */
#define	CD1400_TBPR		0x72	/* transmit baud rate period */
#define	CD1400_TCOR		0x76	/* transmit clock option */
