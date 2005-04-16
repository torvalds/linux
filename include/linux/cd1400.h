/*****************************************************************************/

/*
 *	cd1400.h  -- cd1400 UART hardware info.
 *
 *	Copyright (C) 1996-1998  Stallion Technologies
 *	Copyright (C) 1994-1996  Greg Ungerer.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*****************************************************************************/
#ifndef	_CD1400_H
#define	_CD1400_H
/*****************************************************************************/

/*
 *	Define the number of async ports per cd1400 uart chip.
 */
#define	CD1400_PORTS		4

/*
 *	Define the cd1400 uarts internal FIFO sizes.
 */
#define	CD1400_TXFIFOSIZE	12
#define	CD1400_RXFIFOSIZE	12

/*
 *	Local RX FIFO thresh hold level. Also define the RTS thresh hold
 *	based on the RX thresh hold.
 */
#define	FIFO_RXTHRESHOLD	6
#define	FIFO_RTSTHRESHOLD	7

/*****************************************************************************/

/*
 *	Define the cd1400 register addresses. These are all the valid
 *	registers with the cd1400. Some are global, some virtual, some
 *	per port.
 */
#define	GFRCR		0x40
#define	CAR		0x68
#define	GCR		0x4b
#define	SVRR		0x67
#define	RICR		0x44
#define	TICR		0x45
#define	MICR		0x46
#define	RIR		0x6b
#define	TIR		0x6a
#define	MIR		0x69
#define	PPR		0x7e

#define	RIVR		0x43
#define	TIVR		0x42
#define	MIVR		0x41
#define	TDR		0x63
#define	RDSR		0x62
#define	MISR		0x4c
#define	EOSRR		0x60

#define	LIVR		0x18
#define	CCR		0x05
#define	SRER		0x06
#define	COR1		0x08
#define	COR2		0x09
#define	COR3		0x0a
#define	COR4		0x1e
#define	COR5		0x1f
#define	CCSR		0x0b
#define	RDCR		0x0e
#define	SCHR1		0x1a
#define	SCHR2		0x1b
#define	SCHR3		0x1c
#define	SCHR4		0x1d
#define	SCRL		0x22
#define	SCRH		0x23
#define	LNC		0x24
#define	MCOR1		0x15
#define	MCOR2		0x16
#define	RTPR		0x21
#define	MSVR1		0x6c
#define	MSVR2		0x6d
#define	PSVR		0x6f
#define	RBPR		0x78
#define	RCOR		0x7c
#define	TBPR		0x72
#define	TCOR		0x76

/*****************************************************************************/

/*
 *	Define the set of baud rate clock divisors.
 */
#define	CD1400_CLK0	8
#define	CD1400_CLK1	32
#define	CD1400_CLK2	128
#define	CD1400_CLK3	512
#define	CD1400_CLK4	2048

#define	CD1400_NUMCLKS	5

/*****************************************************************************/

/*
 *	Define the clock pre-scalar value to be a 5 ms clock. This should be
 *	OK for now. It would probably be better to make it 10 ms, but we
 *	can't fit that divisor into 8 bits!
 */
#define	PPR_SCALAR	244

/*****************************************************************************/

/*
 *	Define values used to set character size options.
 */
#define	COR1_CHL5	0x00
#define	COR1_CHL6	0x01
#define	COR1_CHL7	0x02
#define	COR1_CHL8	0x03

/*
 *	Define values used to set the number of stop bits.
 */
#define	COR1_STOP1	0x00
#define	COR1_STOP15	0x04
#define	COR1_STOP2	0x08

/*
 *	Define values used to set the parity scheme in use.
 */
#define	COR1_PARNONE	0x00
#define	COR1_PARFORCE	0x20
#define	COR1_PARENB	0x40
#define	COR1_PARIGNORE	0x10

#define	COR1_PARODD	0x80
#define	COR1_PAREVEN	0x00

#define	COR2_IXM	0x80
#define	COR2_TXIBE	0x40
#define	COR2_ETC	0x20
#define	COR2_LLM	0x10
#define	COR2_RLM	0x08
#define	COR2_RTSAO	0x04
#define	COR2_CTSAE	0x02

#define	COR3_SCDRNG	0x80
#define	COR3_SCD34	0x40
#define	COR3_FCT	0x20
#define	COR3_SCD12	0x10

/*
 *	Define values used by COR4.
 */
#define	COR4_BRKINT	0x08
#define	COR4_IGNBRK	0x18

/*****************************************************************************/

/*
 *	Define the modem control register values.
 *	Note that the actual hardware is a little different to the conventional
 *	pin names on the cd1400.
 */
#define	MSVR1_DTR	0x01
#define	MSVR1_DSR	0x10
#define	MSVR1_RI	0x20
#define	MSVR1_CTS	0x40
#define	MSVR1_DCD	0x80

#define	MSVR2_RTS	0x02
#define	MSVR2_DSR	0x10
#define	MSVR2_RI	0x20
#define	MSVR2_CTS	0x40
#define	MSVR2_DCD	0x80

#define	MCOR1_DCD	0x80
#define	MCOR1_CTS	0x40
#define	MCOR1_RI	0x20
#define	MCOR1_DSR	0x10

#define	MCOR2_DCD	0x80
#define	MCOR2_CTS	0x40
#define	MCOR2_RI	0x20
#define	MCOR2_DSR	0x10

/*****************************************************************************/

/*
 *	Define the bits used with the service (interrupt) enable register.
 */
#define	SRER_NNDT	0x01
#define	SRER_TXEMPTY	0x02
#define	SRER_TXDATA	0x04
#define	SRER_RXDATA	0x10
#define	SRER_MODEM	0x80

/*****************************************************************************/

/*
 *	Define operational commands for the command register.
 */
#define	CCR_RESET	0x80
#define	CCR_CORCHANGE	0x4e
#define	CCR_SENDCH	0x20
#define	CCR_CHANCTRL	0x10

#define	CCR_TXENABLE	(CCR_CHANCTRL | 0x08)
#define	CCR_TXDISABLE	(CCR_CHANCTRL | 0x04)
#define	CCR_RXENABLE	(CCR_CHANCTRL | 0x02)
#define	CCR_RXDISABLE	(CCR_CHANCTRL | 0x01)

#define	CCR_SENDSCHR1	(CCR_SENDCH | 0x01)
#define	CCR_SENDSCHR2	(CCR_SENDCH | 0x02)
#define	CCR_SENDSCHR3	(CCR_SENDCH | 0x03)
#define	CCR_SENDSCHR4	(CCR_SENDCH | 0x04)

#define	CCR_RESETCHAN	(CCR_RESET | 0x00)
#define	CCR_RESETFULL	(CCR_RESET | 0x01)
#define	CCR_TXFLUSHFIFO	(CCR_RESET | 0x02)

#define	CCR_MAXWAIT	10000

/*****************************************************************************/

/*
 *	Define the valid acknowledgement types (for hw ack cycle).
 */
#define	ACK_TYPMASK	0x07
#define	ACK_TYPTX	0x02
#define	ACK_TYPMDM	0x01
#define	ACK_TYPRXGOOD	0x03
#define	ACK_TYPRXBAD	0x07

#define	SVRR_RX		0x01
#define	SVRR_TX		0x02
#define	SVRR_MDM	0x04

#define	ST_OVERRUN	0x01
#define	ST_FRAMING	0x02
#define	ST_PARITY	0x04
#define	ST_BREAK	0x08
#define	ST_SCHAR1	0x10
#define	ST_SCHAR2	0x20
#define	ST_SCHAR3	0x30
#define	ST_SCHAR4	0x40
#define	ST_RANGE	0x70
#define	ST_SCHARMASK	0x70
#define	ST_TIMEOUT	0x80

#define	MISR_DCD	0x80
#define	MISR_CTS	0x40
#define	MISR_RI		0x20
#define	MISR_DSR	0x10

/*****************************************************************************/

/*
 *	Defines for the CCSR status register.
 */
#define	CCSR_RXENABLED	0x80
#define	CCSR_RXFLOWON	0x40
#define	CCSR_RXFLOWOFF	0x20
#define	CCSR_TXENABLED	0x08
#define	CCSR_TXFLOWON	0x04
#define	CCSR_TXFLOWOFF	0x02

/*****************************************************************************/

/*
 *	Define the embedded commands.
 */
#define	ETC_CMD		0x00
#define	ETC_STARTBREAK	0x81
#define	ETC_DELAY	0x82
#define	ETC_STOPBREAK	0x83

/*****************************************************************************/
#endif
