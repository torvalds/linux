/* $OpenBSD: umcs.h,v 1.7 2025/07/06 01:54:12 jsg Exp $ */
/* $NetBSD: umcs.h,v 1.1 2014/03/16 09:34:45 martin Exp $ */

/*-
 * Copyright (c) 2010 Lev Serebryakov <lev@FreeBSD.org>.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _UMCS_H_
#define	_UMCS_H_

#define	UMCS_MAX_PORTS		4

#define	UMCS_READ_LENGTH	1	/* bytes */

/* Read/Write registers vendor commands */
#define	UMCS_READ		0x0d
#define	UMCS_WRITE		0x0e

#define	UMCS_CONFIG_NO		0
#define	UMCS_IFACE_NO		0

/* Read/Write EEPROM values */
#define	UMCS_EEPROM_RW_WVALUE	0x0900

/*
 * All these registers are documented only in full datasheet, which
 * can be requested from MosChip tech support.
 */
#define	UMCS_SP1		0x00	/* Option bits for UART 1, R/W */
#define	UMCS_CTRL1		0x01	/* Control bits for UART 1, R/W */
#define	UMCS_PINPONGHIGH	0x02	/* High bits of ping-pong reg, R/W */
#define	UMCS_PINPONGLOW		0x03	/* Low bits of ping-pong reg, R/W */


/* DCRx_1 Registers goes here (see below, they are documented) */
#define	UMCS_GPIO		0x07	/* GPIO_0 and GPIO_1 bits, R/W */
#define	UMCS_SP2		0x08	/* Option bits for UART 2, R/W */
#define	UMCS_CTRL2		0x09	/* Control bits for UART 2, R/W */
#define	UMCS_SP3		0x0a	/* Option bits for UART 3, R/W */
#define	UMCS_CTRL3		0x0b	/* Control bits for UART 3, R/W */
#define	UMCS_SP4		0x0c	/* Option bits for UART 4, R/W */
#define	UMCS_CTRL4		0x0d	/* Control bits for UART 4, R/W */
#define	UMCS_PLL_DIV_M		0x0e	/* Pre-divider for PLL, R/W */
#define	UMCS_UNKNOWN1		0x0f	/* NOT MENTIONED AND NOT USED */
#define	UMCS_PLL_DIV_N		0x10	/* Loop divider for PLL, R/W */
#define	UMCS_CLK_MUX		0x12	/* PLL clock & Int. ep ctrl, R/W */
#define	UMCS_UNKNOWN2		0x11	/* NOT MENTIONED AND NOT USED */
#define	UMCS_CLK_SELECT12	0x13	/* Clock source for ports 1 & 2, R/W */
#define	UMCS_CLK_SELECT34	0x14	/* Clock source for ports 3 & 4, R/W */
#define	UMCS_UNKNOWN3		0x15	/* NOT MENTIONED AND NOT USED */


/* DCRx_2-DCRx_4 Registers goes here (see below, they are documented) */
#define	UMCS_UNKNOWN4		0x1f	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWN5		0x20	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWN6		0x21	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWN7		0x22	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWN8		0x23	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWN9		0x24	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWNA		0x25	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWNB		0x26	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWNC		0x27	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWND		0x28	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWNE		0x29	/* NOT MENTIONED AND NOT USED */
#define	UMCS_UNKNOWNF		0x2a	/* NOT MENTIONED AND NOT USED */
#define	UMCS_MODE		0x2b	/* Hardware configuration, R */
#define	UMCS_SP1_ICG		0x2c	/* Inter char gap config, port 1, R/W */
#define	UMCS_SP2_ICG		0x2d	/* Inter char gap config, port 2, R/W */
#define	UMCS_SP3_ICG		0x2e	/* Inter char gap config, port 3, R/W */
#define	UMCS_SP4_ICG		0x2f	/* Inter char gap config, port 4, R/W */
#define	UMCS_RX_SAMPLING12	0x30	/* RX sampling for ports 1 & 2, R/W */
#define	UMCS_RX_SAMPLING34	0x31	/* RX sampling for ports 3 & 4, R/W */
#define	UMCS_BI_FIFO_STAT1	0x32	/* Bulk-In FIFO Stat for Port 1, R */
#define	UMCS_BO_FIFO_STAT1	0x33	/* Bulk-out FIFO Stat for Port 1, R */
#define	UMCS_BI_FIFO_STAT2	0x34	/* Bulk-In FIFO Stat for Port 2, R */
#define	UMCS_BO_FIFO_STAT2	0x35	/* Bulk-out FIFO Stat for Port 2, R */
#define	UMCS_BI_FIFO_STAT3	0x36	/* Bulk-In FIFO Stat for Port 3, R */
#define	UMCS_BO_FIFO_STAT3	0x37	/* Bulk-out FIFO Stat for Port 3, R */
#define	UMCS_BI_FIFO_STAT4	0x38	/* Bulk-In FIFO Stat for Port 4, R */
#define	UMCS_BO_FIFO_STAT4	0x39	/* Bulk-out FIFO Stat for Port 4, R */
#define	UMCS_ZERO_PERIOD1	0x3a	/* Period btw frames for Port 1, R/W */
#define	UMCS_ZERO_PERIOD2	0x3b	/* Period btw frames for Port 2 R/W */
#define	UMCS_ZERO_PERIOD3	0x3c	/* Period btw frames for Port 3, R/W */
#define	UMCS_ZERO_PERIOD4	0x3d	/* Period btw frames for Port 4, R/W */
#define	UMCS_ZERO_ENABLE	0x3e	/* Enable zero-out frames, R/W */

/* Low 8 bits and high 1 bit of threshold values for Bulk-Out ports 1-4 */
#define	UMCS_THR_VAL_LOW1	0x3f
#define	UMCS_THR_VAL_HIGH1	0x40
#define	UMCS_THR_VAL_LOW2	0x41
#define	UMCS_THR_VAL_HIGH2	0x42
#define	UMCS_THR_VAL_LOW3	0x43
#define	UMCS_THR_VAL_HIGH3	0x44
#define	UMCS_THR_VAL_LOW4	0x45
#define	UMCS_THR_VAL_HIGH4	0x46
				    
				    
/* Bits for SPx registers */
#define	UMCS_SPx_LOOP_PIPES	0x01	/* Loop Out FIFO to In FIFO */
#define	UMCS_SPx_SKIP_ERR_DATA	0x02	/* Drop data received with errors */
#define	UMCS_SPx_RESET_OUT_FIFO	0x04	/* Reset Bulk-Out FIFO */
#define	UMCS_SPx_RESET_IN_FIFO	0x08	/* Reset Bulk-In FIFO */
#define	UMCS_SPx_CLK_MASK	0x70	/* Mask to extract Baud CLK source */
#define	UMCS_SPx_CLK_X1		0x00	/* Max speed = 115200 bps, default */
#define	UMCS_SPx_CLK_X2		0x10	/* Max speed = 230400 bps */
#define	UMCS_SPx_CLK_X35	0x20	/* Max speed = 403200 bps */
#define	UMCS_SPx_CLK_X4		0x30	/* Max speed = 460800 bps */
#define	UMCS_SPx_CLK_X7		0x40	/* Max speed = 806400 bps */
#define	UMCS_SPx_CLK_X8		0x50	/* Max speed = 921600 bps */
#define	UMCS_SPx_CLK_24MHZ	0x60	/* Max speed = 1.5Mbps */
#define	UMCS_SPx_CLK_48MHZ	0x70	/* Max speed = 3.0 Mbps */
#define	UMCS_SPx_CLK_SHIFT	4	/* Shift to get clock value */
#define	UMCS_SPx_UART_RESET	0x80	/* Reset UART */


/* Bits for CTRL registers */
#define	UMCS_CTRL_HWFC		0x01	/* Enable hardware flow control */
#define	UMCS_CTRL_UNUSED1	0x02	/* Reserved */
#define	UMCS_CTRL_CTS_ENABLE	0x04	/* CTS changes are translated to MSR */
#define	UMCS_CTRL_UNUSED2	0x08	/* Reserved for ports 2,3,4 */
#define	UMCS_CTRL1_DRIVER_DONE	0x08	/* Memory can be use as FIFO */
#define	UMCS_CTRL_RX_NEGATE	0x10	/* Negate RX input */
#define	UMCS_CTRL_RX_DISABLE	0x20	/* Disable RX logic */
#define	UMCS_CTRL_FSM_CONTROL	0x40	/* Disable RX FSM when TX is active */
#define	UMCS_CTRL_UNUSED3	0x80	/* Reserved */


/*
 * Bits for PINPONGx registers.  These registers control how often two
 * input buffers for Bulk-In FIFOs are swapped. One of buffers is used
 * for USB transfer, other for receiving data from UART.  Exact meaning
 * of 15 bit value in these registers is unknown
 */
#define	UMCS_PINPONGHIGH_MULT	128	/* Only 7 bits in PINPONGLOW register */
#define	UMCS_PINPONGLOW_BITS	7	/* Only 7 bits in PINPONGLOW register */


/*
 * THIS ONE IS UNDOCUMENTED IN FULL DATASHEET, but email from tech support
 * confirms, that it is register for GPIO_0 and GPIO_1 data input/output.
 * Chips has 2 GPIO, but first one (lower bit) MUST be used by device
 * authors as "number of port" indicator, grounded (0) for two-port
 * devices and pulled-up to 1 for 4-port devices.
 */
#define	UMCS_GPIO_4PORTS	0x01	/* Device has 4 ports configured */
#define	UMCS_GPIO_GPIO_0	0x01	/* The same as above */
#define	UMCS_GPIO_GPIO_1	0x02	/* GPIO_1 data */

/*
 * Constants for PLL dividers.  Output frequency of PLL is:
 *   Fout = (N/M) * Fin.
 * Default PLL input frequency Fin is 12Mhz (on-chip).
 */
#define	UMCS_PLL_DIV_M_BITS	6	/* Number of bits for M divider */
#define	UMCS_PLL_DIV_M_MASK	0x3f	/* Mask for M divider */
#define	UMCS_PLL_DIV_M_MIN	1	/* Minimum value for M, (0 forbidden) */
#define	UMCS_PLL_DIV_M_DEF	1	/* Default value for M */
#define	UMCS_PLL_DIV_M_MAX	63	/* Maximum value for M */
#define	UMCS_PLL_DIV_N_BITS	6	/* Number of bits for N divider */
#define	UMCS_PLL_DIV_N_MASK	0x3f	/* Mask for N divider */
#define	UMCS_PLL_DIV_N_MIN	1	/* Minimum value for N, (0 forbidden) */
#define	UMCS_PLL_DIV_N_DEF	8	/* Default value for N */
#define	UMCS_PLL_DIV_N_MAX	63	/* Maximum value for N */


/* Bits for CLK_MUX register */
#define	UMCS_CLK_MUX_INMASK	0x03	/* Mask to extract PLL clock input */
#define	UMCS_CLK_MUX_IN12MHZ	0x00	/* 12Mhz PLL input, default */
#define	UMCS_CLK_MUX_INEXTRN	0x01	/* External PLL input */
#define	UMCS_CLK_MUX_INRSV1	0x02	/* Reserved */
#define	UMCS_CLK_MUX_INRSV2	0x03	/* Reserved */
#define	UMCS_CLK_MUX_PLLHIGH	0x04	/* 20MHz-100MHz or 100MHz-300MHz range*/
#define	UMCS_CLK_MUX_INTRFIFOS	0x08	/* Enable FIFOs status (+8 bytes) */
#define	UMCS_CLK_MUX_RESERVED1	0x10	/* Unused */
#define	UMCS_CLK_MUX_RESERVED2	0x20	/* Unused */
#define	UMCS_CLK_MUX_RESERVED3	0x40	/* Unused */
#define	UMCS_CLK_MUX_RESERVED4	0x80	/* Unused */


/* Bits for CLK_SELECTxx registers */
#define	UMCS_CLK_SELECT1_MASK	0x07	/* Bits for port 1 in CLK_SELECT12 */
#define	UMCS_CLK_SELECT1_SHIFT	0	/* Shift for port 1in CLK_SELECT12 */
#define	UMCS_CLK_SELECT2_MASK	0x38	/* Bits for port 2 in CLK_SELECT12 */
#define	UMCS_CLK_SELECT2_SHIFT	3	/* Shift for port 2 in CLK_SELECT12 */
#define	UMCS_CLK_SELECT3_MASK	0x07	/* Bits for port 3 in CLK_SELECT23 */
#define	UMCS_CLK_SELECT3_SHIFT	0	/* Shift for port 3 in CLK_SELECT23 */
#define	UMCS_CLK_SELECT4_MASK	0x38	/* Bits for port 4 in CLK_SELECT23 */
#define	UMCS_CLK_SELECT4_SHIFT	3	/* Shift for port 4 in CLK_SELECT23 */
#define	UMCS_CLK_SELECT_STD	0x00	/* STANDARD rate derived from 96Mhz */
#define	UMCS_CLK_SELECT_30MHZ	0x01	/* 30Mhz */
#define	UMCS_CLK_SELECT_96MHZ	0x02	/* 96Mhz direct */
#define	UMCS_CLK_SELECT_120MHZ	0x03	/* 120Mhz */
#define	UMCS_CLK_SELECT_PLL	0x04	/* PLL output */
#define	UMCS_CLK_SELECT_EXT	0x05	/* External clock input */
#define	UMCS_CLK_SELECT_RES1	0x06	/* Unused */
#define	UMCS_CLK_SELECT_RES2	0x07	/* Unused */


/* Bits for MODE register */
#define	UMCS_MODE_RESERVED1	0x01	/* Unused */
#define	UMCS_MODE_RESET		0x02	/* RESET = Active High (default) */
#define	UMCS_MODE_SER_PRSNT	0x04	/* Reserved (default) */
#define	UMCS_MODE_PLLBYPASS	0x08	/* PLL output is bypassed */
#define	UMCS_MODE_PORBYPASS	0x10	/* Power-On Reset is bypassed */
#define	UMCS_MODE_SELECT24S	0x20	/* 4 or 2 Serial Ports / IrDA active */
#define	UMCS_MODE_EEPROMWR	0x40	/* EEPROM write is enabled (default) */
#define	UMCS_MODE_IRDA		0x80	/* IrDA mode is activated (default) */

/* All 8 bits is used as number of BAUD clocks of pause */
#define	UMCS_SPx_ICG_DEF	0x24	


/*
 * Bits for RX_SAMPLINGxx registers.  These registers control when
 * bit value will be sampled within the baud period.
 * 0 is very beginning of period, 15 is very end, 7 is the middle.
 */
#define	UMCS_RX_SAMPLING1_MASK	0x0f	/* Bits for port 1 in RX_SAMPLING12 */
#define	UMCS_RX_SAMPLING1_SHIFT	0	/* Shift for port 1in RX_SAMPLING12 */
#define	UMCS_RX_SAMPLING2_MASK	0xf0	/* Bits for port 2 in RX_SAMPLING12 */
#define	UMCS_RX_SAMPLING2_SHIFT	4	/* Shift for port 2 in RX_SAMPLING12 */
#define	UMCS_RX_SAMPLING3_MASK	0x0f	/* Bits for port 3 in RX_SAMPLING23 */
#define	UMCS_RX_SAMPLING3_SHIFT	0	/* Shift for port 3 in RX_SAMPLING23 */
#define	UMCS_RX_SAMPLING4_MASK	0xf0	/* Bits for port 4 in RX_SAMPLING23 */
#define	UMCS_RX_SAMPLING4_SHIFT	4	/* Shift for port 4 in RX_SAMPLING23 */
#define	UMCS_RX_SAMPLINGx_MIN	0	/* Max for any RX Sampling */
#define	UMCS_RX_SAMPLINGx_DEF	7	/* Default for any RX Sampling */
#define	UMCS_RX_SAMPLINGx_MAX	15	/* Min for any RX Sampling */

/* Number of Bulk-in requests before sending zero-sized reply */
#define	UMCS_ZERO_PERIODx_DEF	20


/* Bits to enable sending zero-sized replies, per port, (default is on) */
#define	UMCS_ZERO_ENABLE_PORT1	0x01
#define	UMCS_ZERO_ENABLE_PORT2	0x02
#define	UMCS_ZERO_ENABLE_PORT3	0x04
#define	UMCS_ZERO_ENABLE_PORT4	0x08


/* Bits for THR_VAL_HIx */
#define	UMCS_THR_VAL_HIMASK	0x01	/* Only one bit is used */
#define	UMCS_THR_VAL_HIMUL	256	/* This one bit is means "256" */
#define	UMCS_THR_VAL_HISHIFT	8	/* This one bit is means "256" */
#define	UMCS_THR_VAL_HIENABLE	0x80	/* Enable threshold */

/* These are documented in "public" datasheet */
#define	UMCS_DCR0_1		0x04	/* Device ctrl reg 0 for Port 1, R/W */
#define	UMCS_DCR1_1		0x05	/* Device ctrl reg 1 for Port 1, R/W */
#define	UMCS_DCR2_1		0x06	/* Device ctrl reg 2 for Port 1, R/W */
#define	UMCS_DCR0_2		0x16	/* Device ctrl reg 0 for Port 2, R/W */
#define	UMCS_DCR1_2		0x17	/* Device ctrl reg 1 for Port 2, R/W */
#define	UMCS_DCR2_2		0x18	/* Device ctrl reg 2 for Port 2, R/W */
#define	UMCS_DCR0_3		0x19	/* Device ctrl reg 0 for Port 3, R/W */
#define	UMCS_DCR1_3		0x1a	/* Device ctrl reg 1 for Port 3, R/W */
#define	UMCS_DCR2_3		0x1b	/* Device ctrl reg 2 for Port 3, R/W */
#define	UMCS_DCR0_4		0x1c	/* Device ctrl reg 0 for Port 4, R/W */
#define	UMCS_DCR1_4		0x1d	/* Device ctrl reg 1 for Port 4, R/W */
#define	UMCS_DCR2_4		0x1e	/* Device ctrl reg 2 for Port 4, R/W */


/* Bits of DCR0 registers, documented in datasheet */
#define	UMCS_DCR0_PWRSAVE	0x01	/* Transceiver off when USB Suspended */
#define	UMCS_DCR0_RESERVED1	0x02	/* Unused */
#define	UMCS_DCR0_GPIO_MASK	0x0c	/* GPIO Mode bits */
#define	UMCS_DCR0_GPIO_IN	0x00	/* GPIO Mode - Input (0b00) */
#define	UMCS_DCR0_GPIO_OUT	0x08	/* GPIO Mode - Input (0b10) */ 
#define	UMCS_DCR0_RTS_ACTHI	0x10	/* RTS Active is High, (default low) */
#define	UMCS_DCR0_RTS_AUTO	0x20	/* Control by state TX buffer or MCR */
#define	UMCS_DCR0_IRDA		0x40	/* IrDA mode */
#define	UMCS_DCR0_RESERVED2	0x80	/* Unused */

/* Bits of DCR1 registers, documented in datasheet, work only for port 1. */
#define	UMCS_DCR1_GPIO_CURRENT_MASK	0x03	/* Mask to get GPIO value */
#define	UMCS_DCR1_GPIO_CURRENT_6MA	0x00	/* GPIO output current 6mA */
#define	UMCS_DCR1_GPIO_CURRENT_8MA	0x01	/* GPIO output current 8mA */
#define	UMCS_DCR1_GPIO_CURRENT_10MA	0x02	/* GPIO output current 10mA */
#define	UMCS_DCR1_GPIO_CURRENT_12MA	0x03	/* GPIO output current 12mA */
#define	UMCS_DCR1_UART_CURRENT_MASK	0x0c	/* Mask to get UART value */
#define	UMCS_DCR1_UART_CURRENT_6MA	0x00	/* Output current 6mA */
#define	UMCS_DCR1_UART_CURRENT_8MA	0x04	/* Output current 8mA default */
#define	UMCS_DCR1_UART_CURRENT_10MA	0x08	/* UART output current 10mA */
#define	UMCS_DCR1_UART_CURRENT_12MA	0x0c	/* UART output current 12mA */
#define	UMCS_DCR1_WAKEUP_DISABLE	0x10	/* Disable Remote USB Wakeup */
#define	UMCS_DCR1_PLLPWRDOWN_DISABLE	0x20	/* Disable PLL power down */
#define	UMCS_DCR1_LONG_INTERRUPT	0x40	/* Enable  FIFO statistics */
#define	UMCS_DCR1_RESERVED1		0x80	/* Unused */

/*
 * Bits of DCR2 registers, documented in datasheet
 * Wakeup will work only if DCR0_IRDA = 0 (RS-xxx mode) and
 * DCR1_WAKEUP_DISABLE = 0 (wakeup enabled).
 */
#define	UMCS_DCR2_WAKEUP_CTS	0x01	/* Wakeup on CTS change, default = 0 */
#define	UMCS_DCR2_WAKEUP_DCD	0x02	/* Wakeup on DCD change, default = 0 */
#define	UMCS_DCR2_WAKEUP_RI	0x04	/* Wakeup on RI change, default = 1 */
#define	UMCS_DCR2_WAKEUP_DSR	0x08	/* Wakeup on DSR change, default = 0 */
#define	UMCS_DCR2_WAKEUP_RXD	0x10	/* Wakeup on RX Data change, dflt = 0 */
#define	UMCS_DCR2_WAKEUP_RESUME	0x20	/* Wakeup issues RESUME signal,
					 * DISCONNECT otherwise, default = 1 */
#define	UMCS_DCR2_RESERVED1	0x40	/* Unused */
#define	UMCS_DCR2_SHDN_POLARITY	0x80	/* 0: Pin 12 Active Low, 1: Pin 12
					 * Active High, default = 0 */

/* Documented UART registers (fully compatible with 16550 UART) */
#define	UMCS_REG_THR		0x00	/* Transmitter Holding Register  W */
#define	UMCS_REG_RHR		0x00	/* Receiver Holding Register R */
#define	UMCS_REG_IER		0x01	/* Interrupt enable register - R/W */
#define	UMCS_REG_FCR		0x02	/* FIFO Control register - W */
#define	UMCS_REG_ISR		0x02	/* Interrupt Status Register R */
#define	UMCS_REG_LCR		0x03	/* Line control register R/W */
#define	UMCS_REG_MCR		0x04	/* Modem control register R/W */
#define	UMCS_REG_LSR		0x05	/* Line status register R */
#define	UMCS_REG_MSR		0x06	/* Modem status register R */
#define	UMCS_REG_SCRATCHPAD	0x07	/* Scratch pad register */

#define	UMCS_REG_DLL		0x00	/* Low bits of BAUD divider */
#define	UMCS_REG_DLM		0x01	/* High bits of BAUD divider */

/* IER bits */
#define	UMCS_IER_RXREADY	0x01	/* RX Ready interrupt mask */
#define	UMCS_IER_TXREADY	0x02	/* TX Ready interrupt mask */
#define	UMCS_IER_RXSTAT		0x04	/* RX Status interrupt mask */
#define	UMCS_IER_MODEM		0x08	/* Modem status change interrupt mask */
#define	UMCS_IER_SLEEP		0x10	/* SLEEP enable */

/* FCR bits */
#define	UMCS_FCR_ENABLE		0x01	/* Enable FIFO */
#define	UMCS_FCR_FLUSHRHR	0x02	/* Flush RHR and FIFO */
#define	UMCS_FCR_FLUSHTHR	0x04	/* Flush THR and FIFO */
#define	UMCS_FCR_RTLMASK	0xa0	/* Select RHR Interrupt Trigger level */
#define	UMCS_FCR_RTL_1_1	0x00	/* L1 = 1, L2 = 1 */
#define	UMCS_FCR_RTL_1_4	0x40	/* L1 = 1, L2 = 4 */
#define	UMCS_FCR_RTL_1_8	0x80	/* L1 = 1, L2 = 8 */
#define	UMCS_FCR_RTL_1_14	0xa0	/* L1 = 1, L2 = 14 */

/* ISR bits */
#define	UMCS_ISR_NOPENDING	0x01	/* No interrupt pending */
#define	UMCS_ISR_INTMASK	0x3f	/* Mask to select interrupt source */
#define	UMCS_ISR_RXERR		0x06	/* Receiver error */
#define	UMCS_ISR_RXHASDATA	0x04	/* Receiver has data */
#define	UMCS_ISR_RXTIMEOUT	0x0c	/* Receiver timeout */
#define	UMCS_ISR_TXEMPTY	0x02	/* Transmitter empty */
#define	UMCS_ISR_MSCHANGE	0x00	/* Modem status change */

/* LCR bits */
#define	UMCS_LCR_DATALENMASK	0x03	/* Mask for data length */
#define	UMCS_LCR_DATALEN5	0x00	/* 5 data bits */
#define	UMCS_LCR_DATALEN6	0x01	/* 6 data bits */
#define	UMCS_LCR_DATALEN7	0x02	/* 7 data bits */
#define	UMCS_LCR_DATALEN8	0x03	/* 8 data bits */

#define	UMCS_LCR_STOPBMASK	0x04	/* Mask for stop bits */
#define	UMCS_LCR_STOPB1		0x00	/* 1 stop bit in any case */
#define	UMCS_LCR_STOPB2		0x04	/* 1.5-2 stop bits depends on data len*/

#define	UMCS_LCR_PARITYMASK	0x38	/* Mask for all parity data */
#define	UMCS_LCR_PARITYON	0x08	/* Parity ON/OFF - ON */
#define	UMCS_LCR_PARITYODD	0x00	/* Parity Odd */
#define	UMCS_LCR_PARITYEVEN	0x10	/* Parity Even */
#define	UMCS_LCR_PARITYFORCE	0x20	/* Force parity odd/even */

#define	UMCS_LCR_BREAK		0x40	/* Send BREAK */
#define	UMCS_LCR_DIVISORS	0x80	/* Map DLL/DLM instead of xHR/IER */

/* LSR bits */
#define	UMCS_LSR_RHRAVAIL	0x01	/* Data available for read */
#define	UMCS_LSR_RHROVERRUN	0x02	/* Data FIFO/register overflow */
#define	UMCS_LSR_PARITYERR	0x04	/* Parity error */
#define	UMCS_LSR_FRAMEERR	0x10	/* Framing error */
#define	UMCS_LSR_BREAKERR	0x20	/* BREAK signal received */
#define	UMCS_LSR_THREMPTY	0x40	/* THR register is empty, ready for
					 * transmit */
#define	UMCS_LSR_HASERR		0x80	/* Has error in receiver FIFO */

/* MCR bits */
#define	UMCS_MCR_DTR		0x01	/* Force DTR to be active (low) */
#define	UMCS_MCR_RTS		0x02	/* Force RTS to be active (low) */
#define	UMCS_MCR_IE		0x04	/* Enable interrupts (not documented) */
#define	UMCS_MCR_LOOPBACK	0x10	/* Enable local loopback test mode */
#define	UMCS_MCR_CTSRTS		0x20	/* Enable CTS/RTS in 550 (FIFO) mode */
#define	UMCS_MCR_DTRDSR		0x40	/* Enable DTR/DSR in 550 (FIFO) mode */
#define	UMCS_MCR_DCD		0x80	/* Enable DCD in 550 (FIFO) mode */

/* MSR bits */
#define	UMCS_MSR_DELTACTS	0x01	/* CTS was changed since last read */
#define	UMCS_MSR_DELTADSR	0x02	/* DSR was changed since last read */
#define	UMCS_MSR_DELTARI	0x04	/* RI was changed  since last read */
#define	UMCS_MSR_DELTADCD	0x08	/* DCD was changed since last read */
#define	UMCS_MSR_NEGCTS		0x10	/* Negated CTS signal */
#define	UMCS_MSR_NEGDSR		0x20	/* Negated DSR signal */
#define	UMCS_MSR_NEGRI		0x40	/* Negated RI signal */
#define	UMCS_MSR_NEGDCD		0x80	/* Negated DCD signal */

/* SCRATCHPAD bits */
#define	UMCS_SCRATCHPAD_RS232		0x00	/* RS-485 disabled */
#define	UMCS_SCRATCHPAD_RS485_DTRRX	0x80	/* RS-485 mode, DTR High = RX */
#define	UMCS_SCRATCHPAD_RS485_DTRTX	0xc0	/* RS-485 mode, DTR High = TX */

#endif /* _UMCS_H_ */
