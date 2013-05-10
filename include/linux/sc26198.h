/*****************************************************************************/

/*
 *	sc26198.h  -- SC26198 UART hardware info.
 *
 *	Copyright (C) 1995-1998  Stallion Technologies
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
#ifndef	_SC26198_H
#define	_SC26198_H
/*****************************************************************************/

/*
 *	Define the number of async ports per sc26198 uart device.
 */
#define	SC26198_PORTS		8

/*
 *	Baud rate timing clocks. All derived from a master 14.7456 MHz clock.
 */
#define	SC26198_MASTERCLOCK	14745600L
#define	SC26198_DCLK		(SC26198_MASTERCLOCK)
#define	SC26198_CCLK		(SC26198_MASTERCLOCK / 2)
#define	SC26198_BCLK		(SC26198_MASTERCLOCK / 4)

/*
 *	Define internal FIFO sizes for the 26198 ports.
 */
#define	SC26198_TXFIFOSIZE	16
#define	SC26198_RXFIFOSIZE	16

/*****************************************************************************/

/*
 *	Global register definitions. These registers are global to each 26198
 *	device, not specific ports on it.
 */
#define	TSTR		0x0d
#define	GCCR		0x0f
#define	ICR		0x1b
#define	WDTRCR		0x1d
#define	IVR		0x1f
#define	BRGTRUA		0x84
#define	GPOSR		0x87
#define	GPOC		0x8b
#define	UCIR		0x8c
#define	CIR		0x8c
#define	BRGTRUB		0x8d
#define	GRXFIFO		0x8e
#define	GTXFIFO		0x8e
#define	GCCR2		0x8f
#define	BRGTRLA		0x94
#define	GPOR		0x97
#define	GPOD		0x9b
#define	BRGTCR		0x9c
#define	GICR		0x9c
#define	BRGTRLB		0x9d
#define	GIBCR		0x9d
#define	GITR		0x9f

/*
 *	Per port channel registers. These are the register offsets within
 *	the port address space, so need to have the port address (0 to 7)
 *	inserted in bit positions 4:6.
 */
#define	MR0		0x00
#define	MR1		0x01
#define	IOPCR		0x02
#define	BCRBRK		0x03
#define	BCRCOS		0x04
#define	BCRX		0x06
#define	BCRA		0x07
#define	XONCR		0x08
#define	XOFFCR		0x09
#define	ARCR		0x0a
#define	RXCSR		0x0c
#define	TXCSR		0x0e
#define	MR2		0x80
#define	SR		0x81
#define SCCR		0x81
#define	ISR		0x82
#define	IMR		0x82
#define	TXFIFO		0x83
#define	RXFIFO		0x83
#define	IPR		0x84
#define	IOPIOR		0x85
#define	XISR		0x86

/*
 *	For any given port calculate the address to use to access a specified
 *	register. This is only used for unusual access, mostly this is done
 *	through the assembler access routines.
 */
#define	SC26198_PORTREG(port,reg)	((((port) & 0x07) << 4) | (reg))

/*****************************************************************************/

/*
 *	Global configuration control register bit definitions.
 */
#define	GCCR_NOACK		0x00
#define	GCCR_IVRACK		0x02
#define	GCCR_IVRCHANACK		0x04
#define	GCCR_IVRTYPCHANACK	0x06
#define	GCCR_ASYNCCYCLE		0x00
#define	GCCR_SYNCCYCLE		0x40

/*****************************************************************************/

/*
 *	Mode register 0 bit definitions.
 */
#define	MR0_ADDRNONE		0x00
#define	MR0_AUTOWAKE		0x01
#define	MR0_AUTODOZE		0x02
#define	MR0_AUTOWAKEDOZE	0x03
#define	MR0_SWFNONE		0x00
#define	MR0_SWFTX		0x04
#define	MR0_SWFRX		0x08
#define	MR0_SWFRXTX		0x0c
#define	MR0_TXMASK		0x30
#define	MR0_TXEMPTY		0x00
#define	MR0_TXHIGH		0x10
#define	MR0_TXHALF		0x20
#define	MR0_TXRDY		0x00
#define	MR0_ADDRNT		0x00
#define	MR0_ADDRT		0x40
#define	MR0_SWFNT		0x00
#define	MR0_SWFT		0x80

/*
 *	Mode register 1 bit definitions.
 */
#define	MR1_CS5			0x00
#define	MR1_CS6			0x01
#define	MR1_CS7			0x02
#define	MR1_CS8			0x03
#define	MR1_PAREVEN		0x00
#define	MR1_PARODD		0x04
#define	MR1_PARENB		0x00
#define	MR1_PARFORCE		0x08
#define	MR1_PARNONE		0x10
#define	MR1_PARSPECIAL		0x18
#define	MR1_ERRCHAR		0x00
#define	MR1_ERRBLOCK		0x20
#define	MR1_ISRUNMASKED		0x00
#define	MR1_ISRMASKED		0x40
#define	MR1_AUTORTS		0x80

/*
 *	Mode register 2 bit definitions.
 */
#define	MR2_STOP1		0x00
#define	MR2_STOP15		0x01
#define	MR2_STOP2		0x02
#define	MR2_STOP916		0x03
#define	MR2_RXFIFORDY		0x00
#define	MR2_RXFIFOHALF		0x04
#define	MR2_RXFIFOHIGH		0x08
#define	MR2_RXFIFOFULL		0x0c
#define	MR2_AUTOCTS		0x10
#define	MR2_TXRTS		0x20
#define	MR2_MODENORM		0x00
#define	MR2_MODEAUTOECHO	0x40
#define	MR2_MODELOOP		0x80
#define	MR2_MODEREMECHO		0xc0

/*****************************************************************************/

/*
 *	Baud Rate Generator (BRG) selector values.
 */
#define	BRG_50			0x00
#define	BRG_75			0x01
#define	BRG_150			0x02
#define	BRG_200			0x03
#define	BRG_300			0x04
#define	BRG_450			0x05
#define	BRG_600			0x06
#define	BRG_900			0x07
#define	BRG_1200		0x08
#define	BRG_1800		0x09
#define	BRG_2400		0x0a
#define	BRG_3600		0x0b
#define	BRG_4800		0x0c
#define	BRG_7200		0x0d
#define	BRG_9600		0x0e
#define	BRG_14400		0x0f
#define	BRG_19200		0x10
#define	BRG_28200		0x11
#define	BRG_38400		0x12
#define	BRG_57600		0x13
#define	BRG_115200		0x14
#define	BRG_230400		0x15
#define	BRG_GIN0		0x16
#define	BRG_GIN1		0x17
#define	BRG_CT0			0x18
#define	BRG_CT1			0x19
#define	BRG_RX2TX316		0x1b
#define	BRG_RX2TX31		0x1c

#define	SC26198_MAXBAUD		921600

/*****************************************************************************/

/*
 *	Command register command definitions.
 */
#define	CR_NULL			0x04
#define	CR_ADDRNORMAL		0x0c
#define	CR_RXRESET		0x14
#define	CR_TXRESET		0x1c
#define	CR_CLEARRXERR		0x24
#define	CR_BREAKRESET		0x2c
#define	CR_TXSTARTBREAK		0x34
#define	CR_TXSTOPBREAK		0x3c
#define	CR_RTSON		0x44
#define	CR_RTSOFF		0x4c
#define	CR_ADDRINIT		0x5c
#define	CR_RXERRBLOCK		0x6c
#define	CR_TXSENDXON		0x84
#define	CR_TXSENDXOFF		0x8c
#define	CR_GANGXONSET		0x94
#define	CR_GANGXOFFSET		0x9c
#define	CR_GANGXONINIT		0xa4
#define	CR_GANGXOFFINIT		0xac
#define	CR_HOSTXON		0xb4
#define	CR_HOSTXOFF		0xbc
#define	CR_CANCELXOFF		0xc4
#define	CR_ADDRRESET		0xdc
#define	CR_RESETALLPORTS	0xf4
#define	CR_RESETALL		0xfc

#define	CR_RXENABLE		0x01
#define	CR_TXENABLE		0x02

/*****************************************************************************/

/*
 *	Channel status register.
 */
#define	SR_RXRDY		0x01
#define	SR_RXFULL		0x02
#define	SR_TXRDY		0x04
#define	SR_TXEMPTY		0x08
#define	SR_RXOVERRUN		0x10
#define	SR_RXPARITY		0x20
#define	SR_RXFRAMING		0x40
#define	SR_RXBREAK		0x80

#define	SR_RXERRS		(SR_RXPARITY | SR_RXFRAMING | SR_RXOVERRUN)

/*****************************************************************************/

/*
 *	Interrupt status register and interrupt mask register bit definitions.
 */
#define	IR_TXRDY		0x01
#define	IR_RXRDY		0x02
#define	IR_RXBREAK		0x04
#define	IR_XONXOFF		0x10
#define	IR_ADDRRECOG		0x20
#define	IR_RXWATCHDOG		0x40
#define	IR_IOPORT		0x80

/*****************************************************************************/

/*
 *	Interrupt vector register field definitions.
 */
#define	IVR_CHANMASK		0x07
#define	IVR_TYPEMASK		0x18
#define	IVR_CONSTMASK		0xc0

#define	IVR_RXDATA		0x10
#define	IVR_RXBADDATA		0x18
#define	IVR_TXDATA		0x08
#define	IVR_OTHER		0x00

/*****************************************************************************/

/*
 *	BRG timer control register bit definitions.
 */
#define	BRGCTCR_DISABCLK0	0x00
#define	BRGCTCR_ENABCLK0	0x08
#define	BRGCTCR_DISABCLK1	0x00
#define	BRGCTCR_ENABCLK1	0x80

#define	BRGCTCR_0SCLK16		0x00
#define	BRGCTCR_0SCLK32		0x01
#define	BRGCTCR_0SCLK64		0x02
#define	BRGCTCR_0SCLK128	0x03
#define	BRGCTCR_0X1		0x04
#define	BRGCTCR_0X12		0x05
#define	BRGCTCR_0IO1A		0x06
#define	BRGCTCR_0GIN0		0x07

#define	BRGCTCR_1SCLK16		0x00
#define	BRGCTCR_1SCLK32		0x10
#define	BRGCTCR_1SCLK64		0x20
#define	BRGCTCR_1SCLK128	0x30
#define	BRGCTCR_1X1		0x40
#define	BRGCTCR_1X12		0x50
#define	BRGCTCR_1IO1B		0x60
#define	BRGCTCR_1GIN1		0x70

/*****************************************************************************/

/*
 *	Watch dog timer enable register.
 */
#define	WDTRCR_ENABALL		0xff

/*****************************************************************************/

/*
 *	XON/XOFF interrupt status register.
 */
#define	XISR_TXCHARMASK		0x03
#define	XISR_TXCHARNORMAL	0x00
#define	XISR_TXWAIT		0x01
#define	XISR_TXXOFFPEND		0x02
#define	XISR_TXXONPEND		0x03

#define	XISR_TXFLOWMASK		0x0c
#define	XISR_TXNORMAL		0x00
#define	XISR_TXSTOPPEND		0x04
#define	XISR_TXSTARTED		0x08
#define	XISR_TXSTOPPED		0x0c

#define	XISR_RXFLOWMASK		0x30
#define	XISR_RXFLOWNONE		0x00
#define	XISR_RXXONSENT		0x10
#define	XISR_RXXOFFSENT		0x20

#define	XISR_RXXONGOT		0x40
#define	XISR_RXXOFFGOT		0x80

/*****************************************************************************/

/*
 *	Current interrupt register.
 */
#define	CIR_TYPEMASK		0xc0
#define	CIR_TYPEOTHER		0x00
#define	CIR_TYPETX		0x40
#define	CIR_TYPERXGOOD		0x80
#define	CIR_TYPERXBAD		0xc0

#define	CIR_RXDATA		0x80
#define	CIR_RXBADDATA		0x40
#define	CIR_TXDATA		0x40

#define	CIR_CHANMASK		0x07
#define	CIR_CNTMASK		0x38

#define	CIR_SUBTYPEMASK		0x38
#define	CIR_SUBNONE		0x00
#define	CIR_SUBCOS		0x08
#define	CIR_SUBADDR		0x10
#define	CIR_SUBXONXOFF		0x18
#define	CIR_SUBBREAK		0x28

/*****************************************************************************/

/*
 *	Global interrupting channel register.
 */
#define	GICR_CHANMASK		0x07

/*****************************************************************************/

/*
 *	Global interrupting byte count register.
 */
#define	GICR_COUNTMASK		0x0f

/*****************************************************************************/

/*
 *	Global interrupting type register.
 */
#define	GITR_RXMASK		0xc0
#define	GITR_RXNONE		0x00
#define	GITR_RXBADDATA		0x80
#define	GITR_RXGOODDATA		0xc0
#define	GITR_TXDATA		0x20

#define	GITR_SUBTYPEMASK	0x07
#define	GITR_SUBNONE		0x00
#define	GITR_SUBCOS		0x01
#define	GITR_SUBADDR		0x02
#define	GITR_SUBXONXOFF		0x03
#define	GITR_SUBBREAK		0x05

/*****************************************************************************/

/*
 *	Input port change register.
 */
#define	IPR_CTS			0x01
#define	IPR_DTR			0x02
#define	IPR_RTS			0x04
#define	IPR_DCD			0x08
#define	IPR_CTSCHANGE		0x10
#define	IPR_DTRCHANGE		0x20
#define	IPR_RTSCHANGE		0x40
#define	IPR_DCDCHANGE		0x80

#define	IPR_CHANGEMASK		0xf0

/*****************************************************************************/

/*
 *	IO port interrupt and output register.
 */
#define	IOPR_CTS		0x01
#define	IOPR_DTR		0x02
#define	IOPR_RTS		0x04
#define	IOPR_DCD		0x08
#define	IOPR_CTSCOS		0x10
#define	IOPR_DTRCOS		0x20
#define	IOPR_RTSCOS		0x40
#define	IOPR_DCDCOS		0x80

/*****************************************************************************/

/*
 *	IO port configuration register.
 */
#define	IOPCR_SETCTS		0x00
#define	IOPCR_SETDTR		0x04
#define	IOPCR_SETRTS		0x10
#define	IOPCR_SETDCD		0x00

#define	IOPCR_SETSIGS		(IOPCR_SETRTS | IOPCR_SETRTS | IOPCR_SETDTR | IOPCR_SETDCD)

/*****************************************************************************/

/*
 *	General purpose output select register.
 */
#define	GPORS_TXC1XA		0x08
#define	GPORS_TXC16XA		0x09
#define	GPORS_RXC16XA		0x0a
#define	GPORS_TXC16XB		0x0b
#define	GPORS_GPOR3		0x0c
#define	GPORS_GPOR2		0x0d
#define	GPORS_GPOR1		0x0e
#define	GPORS_GPOR0		0x0f

/*****************************************************************************/

/*
 *	General purpose output register.
 */
#define	GPOR_0			0x01
#define	GPOR_1			0x02
#define	GPOR_2			0x04
#define	GPOR_3			0x08

/*****************************************************************************/

/*
 *	General purpose output clock register.
 */
#define	GPORC_0NONE		0x00
#define	GPORC_0GIN0		0x01
#define	GPORC_0GIN1		0x02
#define	GPORC_0IO3A		0x02

#define	GPORC_1NONE		0x00
#define	GPORC_1GIN0		0x04
#define	GPORC_1GIN1		0x08
#define	GPORC_1IO3C		0x0c

#define	GPORC_2NONE		0x00
#define	GPORC_2GIN0		0x10
#define	GPORC_2GIN1		0x20
#define	GPORC_2IO3E		0x20

#define	GPORC_3NONE		0x00
#define	GPORC_3GIN0		0x40
#define	GPORC_3GIN1		0x80
#define	GPORC_3IO3G		0xc0

/*****************************************************************************/

/*
 *	General purpose output data register.
 */
#define	GPOD_0MASK		0x03
#define	GPOD_0SET1		0x00
#define	GPOD_0SET0		0x01
#define	GPOD_0SETR0		0x02
#define	GPOD_0SETIO3B		0x03

#define	GPOD_1MASK		0x0c
#define	GPOD_1SET1		0x00
#define	GPOD_1SET0		0x04
#define	GPOD_1SETR0		0x08
#define	GPOD_1SETIO3D		0x0c

#define	GPOD_2MASK		0x30
#define	GPOD_2SET1		0x00
#define	GPOD_2SET0		0x10
#define	GPOD_2SETR0		0x20
#define	GPOD_2SETIO3F		0x30

#define	GPOD_3MASK		0xc0
#define	GPOD_3SET1		0x00
#define	GPOD_3SET0		0x40
#define	GPOD_3SETR0		0x80
#define	GPOD_3SETIO3H		0xc0

/*****************************************************************************/
#endif
