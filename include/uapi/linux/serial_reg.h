/*
 * include/linux/serial_reg.h
 *
 * Copyright (C) 1992, 1994 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 * 
 * These are the UART port assignments, expressed as offsets from the base
 * register.  These assignments should hold for any serial port based on
 * a 8250, 16450, or 16550(A).
 */

#ifndef _LINUX_SERIAL_REG_H
#define _LINUX_SERIAL_REG_H

/*
 * DLAB=0
 */
#define UART_RX		0	/* In:  Receive buffer */
#define UART_TX		0	/* Out: Transmit buffer */

#define UART_IER	1	/* Out: Interrupt Enable Register */
#define UART_IER_MSI		0x08 /* Enable Modem status interrupt */
#define UART_IER_RLSI		0x04 /* Enable receiver line status interrupt */
#define UART_IER_THRI		0x02 /* Enable Transmitter holding register int. */
#define UART_IER_RDI		0x01 /* Enable receiver data interrupt */
/*
 * Sleep mode for ST16650 and TI16750.  For the ST16650, EFR[4]=1
 */
#define UART_IERX_SLEEP		0x10 /* Enable sleep mode */

#define UART_IIR	2	/* In:  Interrupt ID Register */
#define UART_IIR_NO_INT		0x01 /* No interrupts pending */
#define UART_IIR_ID		0x0e /* Mask for the interrupt ID */
#define UART_IIR_MSI		0x00 /* Modem status interrupt */
#define UART_IIR_THRI		0x02 /* Transmitter holding register empty */
#define UART_IIR_RDI		0x04 /* Receiver data interrupt */
#define UART_IIR_RLSI		0x06 /* Receiver line status interrupt */

#define UART_IIR_BUSY		0x07 /* DesignWare APB Busy Detect */

#define UART_IIR_RX_TIMEOUT	0x0c /* OMAP RX Timeout interrupt */
#define UART_IIR_XOFF		0x10 /* OMAP XOFF/Special Character */
#define UART_IIR_CTS_RTS_DSR	0x20 /* OMAP CTS/RTS/DSR Change */

#define UART_FCR	2	/* Out: FIFO Control Register */
#define UART_FCR_ENABLE_FIFO	0x01 /* Enable the FIFO */
#define UART_FCR_CLEAR_RCVR	0x02 /* Clear the RCVR FIFO */
#define UART_FCR_CLEAR_XMIT	0x04 /* Clear the XMIT FIFO */
#define UART_FCR_DMA_SELECT	0x08 /* For DMA applications */
/*
 * Note: The FIFO trigger levels are chip specific:
 *	RX:76 = 00  01  10  11	TX:54 = 00  01  10  11
 * PC16550D:	 1   4   8  14		xx  xx  xx  xx
 * TI16C550A:	 1   4   8  14          xx  xx  xx  xx
 * TI16C550C:	 1   4   8  14          xx  xx  xx  xx
 * ST16C550:	 1   4   8  14		xx  xx  xx  xx
 * ST16C650:	 8  16  24  28		16   8  24  30	PORT_16650V2
 * NS16C552:	 1   4   8  14		xx  xx  xx  xx
 * ST16C654:	 8  16  56  60		 8  16  32  56	PORT_16654
 * TI16C750:	 1  16  32  56		xx  xx  xx  xx	PORT_16750
 * TI16C752:	 8  16  56  60		 8  16  32  56
 * Tegra:	 1   4   8  14		16   8   4   1	PORT_TEGRA
 */
#define UART_FCR_R_TRIG_00	0x00
#define UART_FCR_R_TRIG_01	0x40
#define UART_FCR_R_TRIG_10	0x80
#define UART_FCR_R_TRIG_11	0xc0
#define UART_FCR_T_TRIG_00	0x00
#define UART_FCR_T_TRIG_01	0x10
#define UART_FCR_T_TRIG_10	0x20
#define UART_FCR_T_TRIG_11	0x30

#define UART_FCR_TRIGGER_MASK	0xC0 /* Mask for the FIFO trigger range */
#define UART_FCR_TRIGGER_1	0x00 /* Mask for trigger set at 1 */
#define UART_FCR_TRIGGER_4	0x40 /* Mask for trigger set at 4 */
#define UART_FCR_TRIGGER_8	0x80 /* Mask for trigger set at 8 */
#define UART_FCR_TRIGGER_14	0xC0 /* Mask for trigger set at 14 */
/* 16650 definitions */
#define UART_FCR6_R_TRIGGER_8	0x00 /* Mask for receive trigger set at 1 */
#define UART_FCR6_R_TRIGGER_16	0x40 /* Mask for receive trigger set at 4 */
#define UART_FCR6_R_TRIGGER_24  0x80 /* Mask for receive trigger set at 8 */
#define UART_FCR6_R_TRIGGER_28	0xC0 /* Mask for receive trigger set at 14 */
#define UART_FCR6_T_TRIGGER_16	0x00 /* Mask for transmit trigger set at 16 */
#define UART_FCR6_T_TRIGGER_8	0x10 /* Mask for transmit trigger set at 8 */
#define UART_FCR6_T_TRIGGER_24  0x20 /* Mask for transmit trigger set at 24 */
#define UART_FCR6_T_TRIGGER_30	0x30 /* Mask for transmit trigger set at 30 */
#define UART_FCR7_64BYTE	0x20 /* Go into 64 byte mode (TI16C750) */

#define UART_FCR_R_TRIG_SHIFT		6
#define UART_FCR_R_TRIG_BITS(x)		\
	(((x) & UART_FCR_TRIGGER_MASK) >> UART_FCR_R_TRIG_SHIFT)
#define UART_FCR_R_TRIG_MAX_STATE	4

#define UART_LCR	3	/* Out: Line Control Register */
/*
 * Note: if the word length is 5 bits (UART_LCR_WLEN5), then setting 
 * UART_LCR_STOP will select 1.5 stop bits, not 2 stop bits.
 */
#define UART_LCR_DLAB		0x80 /* Divisor latch access bit */
#define UART_LCR_SBC		0x40 /* Set break control */
#define UART_LCR_SPAR		0x20 /* Stick parity (?) */
#define UART_LCR_EPAR		0x10 /* Even parity select */
#define UART_LCR_PARITY		0x08 /* Parity Enable */
#define UART_LCR_STOP		0x04 /* Stop bits: 0=1 bit, 1=2 bits */
#define UART_LCR_WLEN5		0x00 /* Wordlength: 5 bits */
#define UART_LCR_WLEN6		0x01 /* Wordlength: 6 bits */
#define UART_LCR_WLEN7		0x02 /* Wordlength: 7 bits */
#define UART_LCR_WLEN8		0x03 /* Wordlength: 8 bits */

/*
 * Access to some registers depends on register access / configuration
 * mode.
 */
#define UART_LCR_CONF_MODE_A	UART_LCR_DLAB	/* Configutation mode A */
#define UART_LCR_CONF_MODE_B	0xBF		/* Configutation mode B */

#define UART_MCR	4	/* Out: Modem Control Register */
#define UART_MCR_CLKSEL		0x80 /* Divide clock by 4 (TI16C752, EFR[4]=1) */
#define UART_MCR_TCRTLR		0x40 /* Access TCR/TLR (TI16C752, EFR[4]=1) */
#define UART_MCR_XONANY		0x20 /* Enable Xon Any (TI16C752, EFR[4]=1) */
#define UART_MCR_AFE		0x20 /* Enable auto-RTS/CTS (TI16C550C/TI16C750) */
#define UART_MCR_LOOP		0x10 /* Enable loopback test mode */
#define UART_MCR_OUT2		0x08 /* Out2 complement */
#define UART_MCR_OUT1		0x04 /* Out1 complement */
#define UART_MCR_RTS		0x02 /* RTS complement */
#define UART_MCR_DTR		0x01 /* DTR complement */

#define UART_LSR	5	/* In:  Line Status Register */
#define UART_LSR_FIFOE		0x80 /* Fifo error */
#define UART_LSR_TEMT		0x40 /* Transmitter empty */
#define UART_LSR_THRE		0x20 /* Transmit-hold-register empty */
#define UART_LSR_BI		0x10 /* Break interrupt indicator */
#define UART_LSR_FE		0x08 /* Frame error indicator */
#define UART_LSR_PE		0x04 /* Parity error indicator */
#define UART_LSR_OE		0x02 /* Overrun error indicator */
#define UART_LSR_DR		0x01 /* Receiver data ready */
#define UART_LSR_BRK_ERROR_BITS	0x1E /* BI, FE, PE, OE bits */

#define UART_MSR	6	/* In:  Modem Status Register */
#define UART_MSR_DCD		0x80 /* Data Carrier Detect */
#define UART_MSR_RI		0x40 /* Ring Indicator */
#define UART_MSR_DSR		0x20 /* Data Set Ready */
#define UART_MSR_CTS		0x10 /* Clear to Send */
#define UART_MSR_DDCD		0x08 /* Delta DCD */
#define UART_MSR_TERI		0x04 /* Trailing edge ring indicator */
#define UART_MSR_DDSR		0x02 /* Delta DSR */
#define UART_MSR_DCTS		0x01 /* Delta CTS */
#define UART_MSR_ANY_DELTA	0x0F /* Any of the delta bits! */

#define UART_SCR	7	/* I/O: Scratch Register */

/*
 * DLAB=1
 */
#define UART_DLL	0	/* Out: Divisor Latch Low */
#define UART_DLM	1	/* Out: Divisor Latch High */

/*
 * LCR=0xBF (or DLAB=1 for 16C660)
 */
#define UART_EFR	2	/* I/O: Extended Features Register */
#define UART_XR_EFR	9	/* I/O: Extended Features Register (XR17D15x) */
#define UART_EFR_CTS		0x80 /* CTS flow control */
#define UART_EFR_RTS		0x40 /* RTS flow control */
#define UART_EFR_SCD		0x20 /* Special character detect */
#define UART_EFR_ECB		0x10 /* Enhanced control bit */
/*
 * the low four bits control software flow control
 */

/*
 * LCR=0xBF, TI16C752, ST16650, ST16650A, ST16654
 */
#define UART_XON1	4	/* I/O: Xon character 1 */
#define UART_XON2	5	/* I/O: Xon character 2 */
#define UART_XOFF1	6	/* I/O: Xoff character 1 */
#define UART_XOFF2	7	/* I/O: Xoff character 2 */

/*
 * EFR[4]=1 MCR[6]=1, TI16C752
 */
#define UART_TI752_TCR	6	/* I/O: transmission control register */
#define UART_TI752_TLR	7	/* I/O: trigger level register */

/*
 * LCR=0xBF, XR16C85x
 */
#define UART_TRG	0	/* FCTR bit 7 selects Rx or Tx
				 * In: Fifo count
				 * Out: Fifo custom trigger levels */
/*
 * These are the definitions for the Programmable Trigger Register
 */
#define UART_TRG_1		0x01
#define UART_TRG_4		0x04
#define UART_TRG_8		0x08
#define UART_TRG_16		0x10
#define UART_TRG_32		0x20
#define UART_TRG_64		0x40
#define UART_TRG_96		0x60
#define UART_TRG_120		0x78
#define UART_TRG_128		0x80

#define UART_FCTR	1	/* Feature Control Register */
#define UART_FCTR_RTS_NODELAY	0x00  /* RTS flow control delay */
#define UART_FCTR_RTS_4DELAY	0x01
#define UART_FCTR_RTS_6DELAY	0x02
#define UART_FCTR_RTS_8DELAY	0x03
#define UART_FCTR_IRDA		0x04  /* IrDa data encode select */
#define UART_FCTR_TX_INT	0x08  /* Tx interrupt type select */
#define UART_FCTR_TRGA		0x00  /* Tx/Rx 550 trigger table select */
#define UART_FCTR_TRGB		0x10  /* Tx/Rx 650 trigger table select */
#define UART_FCTR_TRGC		0x20  /* Tx/Rx 654 trigger table select */
#define UART_FCTR_TRGD		0x30  /* Tx/Rx 850 programmable trigger select */
#define UART_FCTR_SCR_SWAP	0x40  /* Scratch pad register swap */
#define UART_FCTR_RX		0x00  /* Programmable trigger mode select */
#define UART_FCTR_TX		0x80  /* Programmable trigger mode select */

/*
 * LCR=0xBF, FCTR[6]=1
 */
#define UART_EMSR	7	/* Extended Mode Select Register */
#define UART_EMSR_FIFO_COUNT	0x01  /* Rx/Tx select */
#define UART_EMSR_ALT_COUNT	0x02  /* Alternating count select */

/*
 * The Intel XScale on-chip UARTs define these bits
 */
#define UART_IER_DMAE	0x80	/* DMA Requests Enable */
#define UART_IER_UUE	0x40	/* UART Unit Enable */
#define UART_IER_NRZE	0x20	/* NRZ coding Enable */
#define UART_IER_RTOIE	0x10	/* Receiver Time Out Interrupt Enable */

#define UART_IIR_TOD	0x08	/* Character Timeout Indication Detected */

#define UART_FCR_PXAR1	0x00	/* receive FIFO threshold = 1 */
#define UART_FCR_PXAR8	0x40	/* receive FIFO threshold = 8 */
#define UART_FCR_PXAR16	0x80	/* receive FIFO threshold = 16 */
#define UART_FCR_PXAR32	0xc0	/* receive FIFO threshold = 32 */

/*
 * Intel MID on-chip HSU (High Speed UART) defined bits
 */
#define UART_FCR_HSU_64_1B	0x00	/* receive FIFO treshold = 1 */
#define UART_FCR_HSU_64_16B	0x40	/* receive FIFO treshold = 16 */
#define UART_FCR_HSU_64_32B	0x80	/* receive FIFO treshold = 32 */
#define UART_FCR_HSU_64_56B	0xc0	/* receive FIFO treshold = 56 */

#define UART_FCR_HSU_16_1B	0x00	/* receive FIFO treshold = 1 */
#define UART_FCR_HSU_16_4B	0x40	/* receive FIFO treshold = 4 */
#define UART_FCR_HSU_16_8B	0x80	/* receive FIFO treshold = 8 */
#define UART_FCR_HSU_16_14B	0xc0	/* receive FIFO treshold = 14 */

#define UART_FCR_HSU_64B_FIFO	0x20	/* chose 64 bytes FIFO */
#define UART_FCR_HSU_16B_FIFO	0x00	/* chose 16 bytes FIFO */

#define UART_FCR_HALF_EMPT_TXI	0x00	/* trigger TX_EMPT IRQ for half empty */
#define UART_FCR_FULL_EMPT_TXI	0x08	/* trigger TX_EMPT IRQ for full empty */

/*
 * These register definitions are for the 16C950
 */
#define UART_ASR	0x01	/* Additional Status Register */
#define UART_RFL	0x03	/* Receiver FIFO level */
#define UART_TFL 	0x04	/* Transmitter FIFO level */
#define UART_ICR	0x05	/* Index Control Register */

/* The 16950 ICR registers */
#define UART_ACR	0x00	/* Additional Control Register */
#define UART_CPR	0x01	/* Clock Prescalar Register */
#define UART_TCR	0x02	/* Times Clock Register */
#define UART_CKS	0x03	/* Clock Select Register */
#define UART_TTL	0x04	/* Transmitter Interrupt Trigger Level */
#define UART_RTL	0x05	/* Receiver Interrupt Trigger Level */
#define UART_FCL	0x06	/* Flow Control Level Lower */
#define UART_FCH	0x07	/* Flow Control Level Higher */
#define UART_ID1	0x08	/* ID #1 */
#define UART_ID2	0x09	/* ID #2 */
#define UART_ID3	0x0A	/* ID #3 */
#define UART_REV	0x0B	/* Revision */
#define UART_CSR	0x0C	/* Channel Software Reset */
#define UART_NMR	0x0D	/* Nine-bit Mode Register */
#define UART_CTR	0xFF

/*
 * The 16C950 Additional Control Register
 */
#define UART_ACR_RXDIS	0x01	/* Receiver disable */
#define UART_ACR_TXDIS	0x02	/* Transmitter disable */
#define UART_ACR_DSRFC	0x04	/* DSR Flow Control */
#define UART_ACR_TLENB	0x20	/* 950 trigger levels enable */
#define UART_ACR_ICRRD	0x40	/* ICR Read enable */
#define UART_ACR_ASREN	0x80	/* Additional status enable */



/*
 * These definitions are for the RSA-DV II/S card, from
 *
 * Kiyokazu SUTO <suto@ks-and-ks.ne.jp>
 */

#define UART_RSA_BASE (-8)

#define UART_RSA_MSR ((UART_RSA_BASE) + 0) /* I/O: Mode Select Register */

#define UART_RSA_MSR_SWAP (1 << 0) /* Swap low/high 8 bytes in I/O port addr */
#define UART_RSA_MSR_FIFO (1 << 2) /* Enable the external FIFO */
#define UART_RSA_MSR_FLOW (1 << 3) /* Enable the auto RTS/CTS flow control */
#define UART_RSA_MSR_ITYP (1 << 4) /* Level (1) / Edge triger (0) */

#define UART_RSA_IER ((UART_RSA_BASE) + 1) /* I/O: Interrupt Enable Register */

#define UART_RSA_IER_Rx_FIFO_H (1 << 0) /* Enable Rx FIFO half full int. */
#define UART_RSA_IER_Tx_FIFO_H (1 << 1) /* Enable Tx FIFO half full int. */
#define UART_RSA_IER_Tx_FIFO_E (1 << 2) /* Enable Tx FIFO empty int. */
#define UART_RSA_IER_Rx_TOUT (1 << 3) /* Enable char receive timeout int */
#define UART_RSA_IER_TIMER (1 << 4) /* Enable timer interrupt */

#define UART_RSA_SRR ((UART_RSA_BASE) + 2) /* IN: Status Read Register */

#define UART_RSA_SRR_Tx_FIFO_NEMP (1 << 0) /* Tx FIFO is not empty (1) */
#define UART_RSA_SRR_Tx_FIFO_NHFL (1 << 1) /* Tx FIFO is not half full (1) */
#define UART_RSA_SRR_Tx_FIFO_NFUL (1 << 2) /* Tx FIFO is not full (1) */
#define UART_RSA_SRR_Rx_FIFO_NEMP (1 << 3) /* Rx FIFO is not empty (1) */
#define UART_RSA_SRR_Rx_FIFO_NHFL (1 << 4) /* Rx FIFO is not half full (1) */
#define UART_RSA_SRR_Rx_FIFO_NFUL (1 << 5) /* Rx FIFO is not full (1) */
#define UART_RSA_SRR_Rx_TOUT (1 << 6) /* Character reception timeout occurred (1) */
#define UART_RSA_SRR_TIMER (1 << 7) /* Timer interrupt occurred */

#define UART_RSA_FRR ((UART_RSA_BASE) + 2) /* OUT: FIFO Reset Register */

#define UART_RSA_TIVSR ((UART_RSA_BASE) + 3) /* I/O: Timer Interval Value Set Register */

#define UART_RSA_TCR ((UART_RSA_BASE) + 4) /* OUT: Timer Control Register */

#define UART_RSA_TCR_SWITCH (1 << 0) /* Timer on */

/*
 * The RSA DSV/II board has two fixed clock frequencies.  One is the
 * standard rate, and the other is 8 times faster.
 */
#define SERIAL_RSA_BAUD_BASE (921600)
#define SERIAL_RSA_BAUD_BASE_LO (SERIAL_RSA_BAUD_BASE / 8)

/*
 * Extra serial register definitions for the internal UARTs
 * in TI OMAP processors.
 */
#define UART_OMAP_MDR1		0x08	/* Mode definition register */
#define UART_OMAP_MDR2		0x09	/* Mode definition register 2 */
#define UART_OMAP_SCR		0x10	/* Supplementary control register */
#define UART_OMAP_SSR		0x11	/* Supplementary status register */
#define UART_OMAP_EBLR		0x12	/* BOF length register */
#define UART_OMAP_OSC_12M_SEL	0x13	/* OMAP1510 12MHz osc select */
#define UART_OMAP_MVER		0x14	/* Module version register */
#define UART_OMAP_SYSC		0x15	/* System configuration register */
#define UART_OMAP_SYSS		0x16	/* System status register */
#define UART_OMAP_WER		0x17	/* Wake-up enable register */
#define UART_OMAP_TX_LVL	0x1a	/* TX FIFO level register */

/*
 * These are the definitions for the MDR1 register
 */
#define UART_OMAP_MDR1_16X_MODE		0x00	/* UART 16x mode */
#define UART_OMAP_MDR1_SIR_MODE		0x01	/* SIR mode */
#define UART_OMAP_MDR1_16X_ABAUD_MODE	0x02	/* UART 16x auto-baud */
#define UART_OMAP_MDR1_13X_MODE		0x03	/* UART 13x mode */
#define UART_OMAP_MDR1_MIR_MODE		0x04	/* MIR mode */
#define UART_OMAP_MDR1_FIR_MODE		0x05	/* FIR mode */
#define UART_OMAP_MDR1_CIR_MODE		0x06	/* CIR mode */
#define UART_OMAP_MDR1_DISABLE		0x07	/* Disable (default state) */

/*
 * These are definitions for the Exar XR17V35X and XR17(C|D)15X
 */
#define UART_EXAR_8XMODE	0x88	/* 8X sampling rate select */
#define UART_EXAR_SLEEP		0x8b	/* Sleep mode */
#define UART_EXAR_DVID		0x8d	/* Device identification */

#define UART_EXAR_FCTR		0x08	/* Feature Control Register */
#define UART_FCTR_EXAR_IRDA	0x08	/* IrDa data encode select */
#define UART_FCTR_EXAR_485	0x10	/* Auto 485 half duplex dir ctl */
#define UART_FCTR_EXAR_TRGA	0x00	/* FIFO trigger table A */
#define UART_FCTR_EXAR_TRGB	0x60	/* FIFO trigger table B */
#define UART_FCTR_EXAR_TRGC	0x80	/* FIFO trigger table C */
#define UART_FCTR_EXAR_TRGD	0xc0	/* FIFO trigger table D programmable */

#define UART_EXAR_TXTRG		0x0a	/* Tx FIFO trigger level write-only */
#define UART_EXAR_RXTRG		0x0b	/* Rx FIFO trigger level write-only */

#endif /* _LINUX_SERIAL_REG_H */

