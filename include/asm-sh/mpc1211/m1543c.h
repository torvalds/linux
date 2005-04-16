#ifndef __ASM_SH_M1543C_H
#define __ASM_SH_M1543C_H

/*
 * linux/include/asm-sh/m1543c.h
 * Copyright (C) 2001  Nobuhiro Sakawa
 * M1543C:PCI-ISA Bus Bridge with Super IO Chip support
 *
 * from
 *
 * linux/include/asm-sh/smc37c93x.h
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * SMSC 37C93x Super IO Chip support
 */

/* Default base I/O address */
#define FDC_PRIMARY_BASE	0x3f0
#define IDE1_PRIMARY_BASE	0x1f0
#define IDE1_SECONDARY_BASE	0x170
#define PARPORT_PRIMARY_BASE	0x378
#define COM1_PRIMARY_BASE	0x2f8
#define COM2_PRIMARY_BASE	0x3f8
#define COM3_PRIMARY_BASE	0x3e8
#define RTC_PRIMARY_BASE	0x070
#define KBC_PRIMARY_BASE	0x060
#define AUXIO_PRIMARY_BASE	0x000	/* XXX */
#define I8259_M_CR		0x20
#define I8259_M_MR		0x21
#define I8259_S_CR		0xa0
#define I8259_S_MR		0xa1

/* Logical device number */
#define LDN_FDC			0
#define LDN_IDE1		1
#define LDN_IDE2		2
#define LDN_PARPORT		3
#define LDN_COM1		4
#define LDN_COM2		5
#define LDN_COM3		11
#define LDN_RTC			6
#define LDN_KBC			7

/* Configuration port and key */
#define CONFIG_PORT		0x3f0
#define INDEX_PORT		CONFIG_PORT
#define DATA_PORT		0x3f1
#define CONFIG_ENTER1		0x51
#define CONFIG_ENTER2		0x23
#define CONFIG_EXIT		0xbb

/* Configuration index */
#define CURRENT_LDN_INDEX	0x07
#define POWER_CONTROL_INDEX	0x22
#define ACTIVATE_INDEX		0x30
#define IO_BASE_HI_INDEX	0x60
#define IO_BASE_LO_INDEX	0x61
#define IRQ_SELECT_INDEX	0x70
#define PS2_IRQ_INDEX		0x72
#define DMA_SELECT_INDEX	0x74

/* UART stuff. Only for debugging.  */
/* UART Register */

#define UART_RBR	0x0	/* Receiver Buffer Register (Read Only) */
#define UART_THR	0x0	/* Transmitter Holding Register (Write Only) */
#define UART_IER	0x2	/* Interrupt Enable Register */
#define UART_IIR	0x4	/* Interrupt Ident Register (Read Only) */
#define UART_FCR	0x4	/* FIFO Control Register (Write Only) */
#define UART_LCR	0x6	/* Line Control Register */
#define UART_MCR	0x8	/* MODEM Control Register */
#define UART_LSR	0xa	/* Line Status Register */
#define UART_MSR	0xc	/* MODEM Status Register */
#define UART_SCR	0xe	/* Scratch Register */
#define UART_DLL	0x0	/* Divisor Latch (LS) */
#define UART_DLM	0x2	/* Divisor Latch (MS) */

#ifndef __ASSEMBLY__
typedef struct uart_reg {
	volatile __u16 rbr;
	volatile __u16 ier;
	volatile __u16 iir;
	volatile __u16 lcr;
	volatile __u16 mcr;
	volatile __u16 lsr;
	volatile __u16 msr;
	volatile __u16 scr;
} uart_reg;
#endif /* ! __ASSEMBLY__ */

/* Alias for Write Only Register */

#define thr	rbr
#define tcr	iir

/* Alias for Divisor Latch Register */

#define dll	rbr
#define dlm	ier
#define fcr	iir

/* Interrupt Enable Register */

#define IER_ERDAI	0x0100	/* Enable Received Data Available Interrupt */
#define IER_ETHREI	0x0200	/* Enable Transmitter Holding Register Empty Interrupt */
#define IER_ELSI	0x0400	/* Enable Receiver Line Status Interrupt */
#define IER_EMSI	0x0800	/* Enable MODEM Status Interrupt */

/* Interrupt Ident Register */

#define IIR_IP		0x0100	/* "0" if Interrupt Pending */
#define IIR_IIB0	0x0200	/* Interrupt ID Bit 0 */
#define IIR_IIB1	0x0400	/* Interrupt ID Bit 1 */
#define IIR_IIB2	0x0800	/* Interrupt ID Bit 2 */
#define IIR_FIFO	0xc000	/* FIFOs enabled */

/* FIFO Control Register */

#define FCR_FEN		0x0100	/* FIFO enable */
#define FCR_RFRES	0x0200	/* Receiver FIFO reset */
#define FCR_TFRES	0x0400	/* Transmitter FIFO reset */
#define FCR_DMA		0x0800	/* DMA mode select */
#define FCR_RTL		0x4000	/* Receiver triger (LSB) */
#define FCR_RTM		0x8000	/* Receiver triger (MSB) */

/* Line Control Register */

#define LCR_WLS0	0x0100	/* Word Length Select Bit 0 */
#define LCR_WLS1	0x0200	/* Word Length Select Bit 1 */
#define LCR_STB		0x0400	/* Number of Stop Bits */
#define LCR_PEN		0x0800	/* Parity Enable */
#define LCR_EPS		0x1000	/* Even Parity Select */
#define LCR_SP		0x2000	/* Stick Parity */
#define LCR_SB		0x4000	/* Set Break */
#define LCR_DLAB	0x8000	/* Divisor Latch Access Bit */

/* MODEM Control Register */

#define MCR_DTR		0x0100	/* Data Terminal Ready */
#define MCR_RTS		0x0200	/* Request to Send */
#define MCR_OUT1	0x0400	/* Out 1 */
#define MCR_IRQEN	0x0800	/* IRQ Enable */
#define MCR_LOOP	0x1000	/* Loop */

/* Line Status Register */

#define LSR_DR		0x0100	/* Data Ready */
#define LSR_OE		0x0200	/* Overrun Error */
#define LSR_PE		0x0400	/* Parity Error */
#define LSR_FE		0x0800	/* Framing Error */
#define LSR_BI		0x1000	/* Break Interrupt */
#define LSR_THRE	0x2000	/* Transmitter Holding Register Empty */
#define LSR_TEMT	0x4000	/* Transmitter Empty */
#define LSR_FIFOE	0x8000	/* Receiver FIFO error */

/* MODEM Status Register */

#define MSR_DCTS	0x0100	/* Delta Clear to Send */
#define MSR_DDSR	0x0200	/* Delta Data Set Ready */
#define MSR_TERI	0x0400	/* Trailing Edge Ring Indicator */
#define MSR_DDCD	0x0800	/* Delta Data Carrier Detect */
#define MSR_CTS		0x1000	/* Clear to Send */
#define MSR_DSR		0x2000	/* Data Set Ready */
#define MSR_RI		0x4000	/* Ring Indicator */
#define MSR_DCD		0x8000	/* Data Carrier Detect */

/* Baud Rate Divisor */

#define UART_CLK	(1843200)	/* 1.8432 MHz */
#define UART_BAUD(x)	(UART_CLK / (16 * (x)))

/* RTC register definition */
#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9
#define RTC_FREQ_SELECT		10
# define RTC_UIP 0x80
# define RTC_DIV_CTL 0x70
/* This RTC can work under 32.768KHz clock only.  */
# define RTC_OSC_ENABLE 0x20
# define RTC_OSC_DISABLE 0x00
#define RTC_CONTROL     	11
# define RTC_SET 0x80
# define RTC_PIE 0x40
# define RTC_AIE 0x20
# define RTC_UIE 0x10
# define RTC_SQWE 0x08
# define RTC_DM_BINARY 0x04
# define RTC_24H 0x02
# define RTC_DST_EN 0x01

#endif  /* __ASM_SH_M1543C_H */
