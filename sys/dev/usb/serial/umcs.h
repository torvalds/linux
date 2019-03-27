/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#ifndef _UMCS7840_H_
#define	_UMCS7840_H_

#define	UMCS7840_MAX_PORTS	4

#define	UMCS7840_READ_LENGTH	1	/* bytes */
#define	UMCS7840_CTRL_TIMEOUT	500	/* ms */

/* Read/Wrtire registers vendor commands */
#define	MCS7840_RDREQ		0x0d
#define	MCS7840_WRREQ		0x0e

/* Read/Wrtie EEPROM values */
#define	MCS7840_EEPROM_RW_WVALUE	0x0900

/*
 *   All these registers are documented only in full datasheet,
 * which can be requested from MosChip tech support.
 */
#define	MCS7840_DEV_REG_SP1		0x00	/* Options for for UART 1, R/W */
#define	MCS7840_DEV_REG_CONTROL1	0x01	/* Control bits for UART 1,
						 * R/W */
#define	MCS7840_DEV_REG_PINPONGHIGH	0x02	/* High bits of ping-pong
						 * register, R/W */
#define	MCS7840_DEV_REG_PINPONGLOW	0x03	/* Low bits of ping-pong
						 * register, R/W */
/* DCRx_1 Registers goes here (see below, they are documented) */
#define	MCS7840_DEV_REG_GPIO		0x07	/* GPIO_0 and GPIO_1 bits,
						 * undocumented, see notes
						 * below R/W */
#define	MCS7840_DEV_REG_SP2		0x08	/* Options for for UART 2, R/W */
#define	MCS7840_DEV_REG_CONTROL2	0x09	/* Control bits for UART 2,
						 * R/W */
#define	MCS7840_DEV_REG_SP3		0x0a	/* Options for for UART 3, R/W */
#define	MCS7840_DEV_REG_CONTROL3	0x0b	/* Control bits for UART 3,
						 * R/W */
#define	MCS7840_DEV_REG_SP4		0x0c	/* Options for for UART 4, R/W */
#define	MCS7840_DEV_REG_CONTROL4	0x0d	/* Control bits for UART 4,
						 * R/W */
#define	MCS7840_DEV_REG_PLL_DIV_M	0x0e	/* Pre-diviedr for PLL, R/W */
#define	MCS7840_DEV_REG_UNKNOWN1	0x0f	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_PLL_DIV_N	0x10	/* Loop divider for PLL, R/W */
#define	MCS7840_DEV_REG_CLOCK_MUX	0x12	/* PLL input clock & Interrupt
						 * endpoint control, R/W */
#define	MCS7840_DEV_REG_UNKNOWN2	0x11	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_CLOCK_SELECT12	0x13	/* Clock source for ports 1 &
						 * 2, R/W */
#define	MCS7840_DEV_REG_CLOCK_SELECT34	0x14	/* Clock source for ports 3 &
						 * 4, R/W */
#define	MCS7840_DEV_REG_UNKNOWN3	0x15	/* NOT MENTIONED AND NOT USED */
/* DCRx_2-DCRx_4 Registers goes here (see below, they are documented) */
#define	MCS7840_DEV_REG_UNKNOWN4	0x1f	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWN5	0x20	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWN6	0x21	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWN7	0x22	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWN8	0x23	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWN9	0x24	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWNA	0x25	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWNB	0x26	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWNC	0x27	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWND	0x28	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWNE	0x29	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_UNKNOWNF	0x2a	/* NOT MENTIONED AND NOT USED */
#define	MCS7840_DEV_REG_MODE		0x2b	/* Hardware configuration,
						 * R/Only */
#define	MCS7840_DEV_REG_SP1_ICG		0x2c	/* Inter character gap
						 * configuration for Port 1,
						 * R/W */
#define	MCS7840_DEV_REG_SP2_ICG		0x2d	/* Inter character gap
						 * configuration for Port 2,
						 * R/W */
#define	MCS7840_DEV_REG_SP3_ICG		0x2e	/* Inter character gap
						 * configuration for Port 3,
						 * R/W */
#define	MCS7840_DEV_REG_SP4_ICG		0x2f	/* Inter character gap
						 * configuration for Port 4,
						 * R/W */
#define	MCS7840_DEV_REG_RX_SAMPLING12	0x30	/* RX sampling for ports 1 &
						 * 2, R/W */
#define	MCS7840_DEV_REG_RX_SAMPLING34	0x31	/* RX sampling for ports 3 &
						 * 4, R/W */
#define	MCS7840_DEV_REG_BI_FIFO_STAT1	0x32	/* Bulk-In FIFO Stat for Port
						 * 1, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BO_FIFO_STAT1	0x33	/* Bulk-out FIFO Stat for Port
						 * 1, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BI_FIFO_STAT2	0x34	/* Bulk-In FIFO Stat for Port
						 * 2, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BO_FIFO_STAT2	0x35	/* Bulk-out FIFO Stat for Port
						 * 2, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BI_FIFO_STAT3	0x36	/* Bulk-In FIFO Stat for Port
						 * 3, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BO_FIFO_STAT3	0x37	/* Bulk-out FIFO Stat for Port
						 * 3, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BI_FIFO_STAT4	0x38	/* Bulk-In FIFO Stat for Port
						 * 4, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_BO_FIFO_STAT4	0x39	/* Bulk-out FIFO Stat for Port
						 * 4, contains number of
						 * available bytes, R/Only */
#define	MCS7840_DEV_REG_ZERO_PERIOD1	0x3a	/* Period between zero out
						 * frames for Port 1, R/W */
#define	MCS7840_DEV_REG_ZERO_PERIOD2	0x3b	/* Period between zero out
						 * frames for Port 1, R/W */
#define	MCS7840_DEV_REG_ZERO_PERIOD3	0x3c	/* Period between zero out
						 * frames for Port 1, R/W */
#define	MCS7840_DEV_REG_ZERO_PERIOD4	0x3d	/* Period between zero out
						 * frames for Port 1, R/W */
#define	MCS7840_DEV_REG_ZERO_ENABLE	0x3e	/* Enable/disable of zero out
						 * frames, R/W */
#define	MCS7840_DEV_REG_THR_VAL_LOW1	0x3f	/* Low 8 bits of threshold
						 * value for Bulk-Out for Port
						 * 1, R/W */
#define	MCS7840_DEV_REG_THR_VAL_HIGH1	0x40	/* High 1 bit of threshold
						 * value for Bulk-Out and
						 * enable flag for Port 1, R/W */
#define	MCS7840_DEV_REG_THR_VAL_LOW2	0x41	/* Low 8 bits of threshold
						 * value for Bulk-Out for Port
						 * 2, R/W */
#define	MCS7840_DEV_REG_THR_VAL_HIGH2	0x42	/* High 1 bit of threshold
						 * value for Bulk-Out and
						 * enable flag for Port 2, R/W */
#define	MCS7840_DEV_REG_THR_VAL_LOW3	0x43	/* Low 8 bits of threshold
						 * value for Bulk-Out for Port
						 * 3, R/W */
#define	MCS7840_DEV_REG_THR_VAL_HIGH3	0x44	/* High 1 bit of threshold
						 * value for Bulk-Out and
						 * enable flag for Port 3, R/W */
#define	MCS7840_DEV_REG_THR_VAL_LOW4	0x45	/* Low 8 bits of threshold
						 * value for Bulk-Out for Port
						 * 4, R/W */
#define	MCS7840_DEV_REG_THR_VAL_HIGH4	0x46	/* High 1 bit of threshold
						 * value for Bulk-Out and
						 * enable flag for Port 4, R/W */

/* Bits for SPx registers */
#define	MCS7840_DEV_SPx_LOOP_PIPES	0x01	/* Loop Bulk-Out FIFO to the
						 * Bulk-In FIFO, default = 0 */
#define	MCS7840_DEV_SPx_SKIP_ERR_DATA	0x02	/* Drop data bytes from UART,
						 * which were recevied with
						 * errors, default = 0 */
#define	MCS7840_DEV_SPx_RESET_OUT_FIFO	0x04	/* Reset Bulk-Out FIFO */
#define	MCS7840_DEV_SPx_RESET_IN_FIFO	0x08	/* Reset Bulk-In FIFO */
#define	MCS7840_DEV_SPx_CLOCK_MASK	0x70	/* Mask to extract Baud CLK
						 * source */
#define	MCS7840_DEV_SPx_CLOCK_X1	0x00	/* CLK =  1.8432Mhz, max speed
						 * = 115200 bps, default */
#define	MCS7840_DEV_SPx_CLOCK_X2	0x10	/* CLK =  3.6864Mhz, max speed
						 * = 230400 bps */
#define	MCS7840_DEV_SPx_CLOCK_X35	0x20	/* CLK =  6.4512Mhz, max speed
						 * = 403200 bps */
#define	MCS7840_DEV_SPx_CLOCK_X4	0x30	/* CLK =  7.3728Mhz, max speed
						 * = 460800 bps */
#define	MCS7840_DEV_SPx_CLOCK_X7	0x40	/* CLK = 12.9024Mhz, max speed
						 * = 806400 bps */
#define	MCS7840_DEV_SPx_CLOCK_X8	0x50	/* CLK = 14.7456Mhz, max speed
						 * = 921600 bps */
#define	MCS7840_DEV_SPx_CLOCK_24MHZ	0x60	/* CLK = 24.0000Mhz, max speed
						 * = 1.5 Mbps */
#define	MCS7840_DEV_SPx_CLOCK_48MHZ	0x70	/* CLK = 48.0000Mhz, max speed
						 * = 3.0 Mbps */
#define	MCS7840_DEV_SPx_CLOCK_SHIFT	4	/* Value 0..7 can be shifted
						 * to get clock value */
#define	MCS7840_DEV_SPx_UART_RESET	0x80	/* Reset UART */

/* Bits for CONTROLx registers */
#define	MCS7840_DEV_CONTROLx_HWFC		0x01	/* Enable hardware flow
							 * control (when power
							 * down? It is unclear
							 * in documents),
							 * default = 0 */
#define	MCS7840_DEV_CONTROLx_UNUNSED1		0x02	/* Reserved */
#define	MCS7840_DEV_CONTROLx_CTS_ENABLE		0x04	/* CTS changes are
							 * translated to MSR,
							 * default = 0 */
#define	MCS7840_DEV_CONTROLx_UNUSED2		0x08	/* Reserved for ports
							 * 2,3,4 */
#define	MCS7840_DEV_CONTROL1_DRIVER_DONE	0x08	/* USB enumerating is
							 * finished, USB
							 * enumeration memory
							 * can be used as FIFOs */
#define	MCS7840_DEV_CONTROLx_RX_NEGATE		0x10	/* Negate RX input,
							 * works for IrDA mode
							 * only, default = 0 */
#define	MCS7840_DEV_CONTROLx_RX_DISABLE		0x20	/* Disable RX logic,
							 * works only for
							 * RS-232/RS-485 mode,
							 * default = 0 */
#define	MCS7840_DEV_CONTROLx_FSM_CONTROL	0x40	/* Disable RX FSM when
							 * TX is in progress,
							 * works for IrDA mode
							 * only, default = 0 */
#define	MCS7840_DEV_CONTROLx_UNUSED3		0x80	/* Reserved */

/*
 * Bits for PINPONGx registers
 * These registers control how often two input buffers
 * for Bulk-In FIFOs are swapped. One of buffers is used
 * for USB trnasfer, other for receiving data from UART.
 * Exact meaning of 15 bit value in these registers is unknown
 */
#define	MCS7840_DEV_PINPONGHIGH_MULT	128	/* Only 7 bits in PINPONGLOW
						 * register */
#define	MCS7840_DEV_PINPONGLOW_BITS	7	/* Only 7 bits in PINPONGLOW
						 * register */

/*
 *  THIS ONE IS UNDOCUMENTED IN FULL DATASHEET, but e-mail from tech support
 * confirms, that it is register for GPIO_0 and GPIO_1 data input/output.
 *  Chips has 2 GPIO, but first one (lower bit) MUST be used by device
 * authors as "number of port" indicator, grounded (0) for two-port
 * devices and pulled-up to 1 for 4-port devices.
 */
#define	MCS7840_DEV_GPIO_4PORTS		0x01	/* Device has 4 ports
						 * configured */
#define	MCS7840_DEV_GPIO_GPIO_0		0x01	/* The same as above */
#define	MCS7840_DEV_GPIO_GPIO_1		0x02	/* GPIO_1 data */

/*
 * Constants for PLL dividers
 * Ouptut frequency of PLL is:
 *   Fout = (N/M) * Fin.
 * Default PLL input frequency Fin is 12Mhz (on-chip).
 */
#define	MCS7840_DEV_PLL_DIV_M_BITS	6	/* Number of useful bits for M
						 * divider */
#define	MCS7840_DEV_PLL_DIV_M_MASK	0x3f	/* Mask for M divider */
#define	MCS7840_DEV_PLL_DIV_M_MIN	1	/* Minimum value for M, 0 is
						 * forbidden */
#define	MCS7840_DEV_PLL_DIV_M_DEF	1	/* Default value for M */
#define	MCS7840_DEV_PLL_DIV_M_MAX	63	/* Maximum value for M */
#define	MCS7840_DEV_PLL_DIV_N_BITS	6	/* Number of useful bits for N
						 * divider */
#define	MCS7840_DEV_PLL_DIV_N_MASK	0x3f	/* Mask for N divider */
#define	MCS7840_DEV_PLL_DIV_N_MIN	1	/* Minimum value for N, 0 is
						 * forbidden */
#define	MCS7840_DEV_PLL_DIV_N_DEF	8	/* Default value for N */
#define	MCS7840_DEV_PLL_DIV_N_MAX	63	/* Maximum value for N */

/* Bits for CLOCK_MUX register */
#define	MCS7840_DEV_CLOCK_MUX_INPUTMASK	0x03	/* Mask to extract PLL clock
						 * input */
#define	MCS7840_DEV_CLOCK_MUX_IN12MHZ	0x00	/* 12Mhz PLL input, default */
#define	MCS7840_DEV_CLOCK_MUX_INEXTRN	0x01	/* External (device-depended)
						 * PLL input */
#define	MCS7840_DEV_CLOCK_MUX_INRSV1	0x02	/* Reserved */
#define	MCS7840_DEV_CLOCK_MUX_INRSV2	0x03	/* Reserved */
#define	MCS7840_DEV_CLOCK_MUX_PLLHIGH	0x04	/* 0 = PLL Output is
						 * 20MHz-100MHz (default), 1 =
						 * 100MHz-300MHz range */
#define	MCS7840_DEV_CLOCK_MUX_INTRFIFOS	0x08	/* Enable additional 8 bytes
						 * fro Interrupt USB pipe with
						 * USB FIFOs statuses, default
						 * = 0 */
#define	MCS7840_DEV_CLOCK_MUX_RESERVED1	0x10	/* Unused */
#define	MCS7840_DEV_CLOCK_MUX_RESERVED2	0x20	/* Unused */
#define	MCS7840_DEV_CLOCK_MUX_RESERVED3	0x40	/* Unused */
#define	MCS7840_DEV_CLOCK_MUX_RESERVED4	0x80	/* Unused */

/* Bits for CLOCK_SELECTxx registers	*/
#define	MCS7840_DEV_CLOCK_SELECT1_MASK	0x07	/* Bits for port 1 in
						 * CLOCK_SELECT12 */
#define	MCS7840_DEV_CLOCK_SELECT1_SHIFT	0	/* Shift for port 1in
						 * CLOCK_SELECT12 */
#define	MCS7840_DEV_CLOCK_SELECT2_MASK	0x38	/* Bits for port 2 in
						 * CLOCK_SELECT12 */
#define	MCS7840_DEV_CLOCK_SELECT2_SHIFT	3	/* Shift for port 2 in
						 * CLOCK_SELECT12 */
#define	MCS7840_DEV_CLOCK_SELECT3_MASK	0x07	/* Bits for port 3 in
						 * CLOCK_SELECT23 */
#define	MCS7840_DEV_CLOCK_SELECT3_SHIFT	0	/* Shift for port 3 in
						 * CLOCK_SELECT23 */
#define	MCS7840_DEV_CLOCK_SELECT4_MASK	0x38	/* Bits for port 4 in
						 * CLOCK_SELECT23 */
#define	MCS7840_DEV_CLOCK_SELECT4_SHIFT	3	/* Shift for port 4 in
						 * CLOCK_SELECT23 */
#define	MCS7840_DEV_CLOCK_SELECT_STD	0x00	/* STANDARD baudrate derived
						 * from 96Mhz, default for all
						 * ports */
#define	MCS7840_DEV_CLOCK_SELECT_30MHZ	0x01	/* 30Mhz */
#define	MCS7840_DEV_CLOCK_SELECT_96MHZ	0x02	/* 96Mhz direct */
#define	MCS7840_DEV_CLOCK_SELECT_120MHZ	0x03	/* 120Mhz */
#define	MCS7840_DEV_CLOCK_SELECT_PLL	0x04	/* PLL output (see for M and N
						 * dividers) */
#define	MCS7840_DEV_CLOCK_SELECT_EXT	0x05	/* External clock input
						 * (device-dependend) */
#define	MCS7840_DEV_CLOCK_SELECT_RES1	0x06	/* Unused */
#define	MCS7840_DEV_CLOCK_SELECT_RES2	0x07	/* Unused */

/* Bits for MODE register */
#define	MCS7840_DEV_MODE_RESERVED1	0x01	/* Unused */
#define	MCS7840_DEV_MODE_RESET		0x02	/* 0: RESET = Active High
						 * (default), 1: Reserved (?) */
#define	MCS7840_DEV_MODE_SER_PRSNT	0x04	/* 0: Reserved, 1: Do not use
						 * hardocded values (default)
						 * (?) */
#define	MCS7840_DEV_MODE_PLLBYPASS	0x08	/* 1: PLL output is bypassed,
						 * default = 0 */
#define	MCS7840_DEV_MODE_PORBYPASS	0x10	/* 1: Power-On Reset is
						 * bypassed, default = 0 */
#define	MCS7840_DEV_MODE_SELECT24S	0x20	/* 0: 4 Serial Ports / IrDA
						 * active, 1: 2 Serial Ports /
						 * IrDA active */
#define	MCS7840_DEV_MODE_EEPROMWR	0x40	/* EEPROM write is enabled,
						 * default */
#define	MCS7840_DEV_MODE_IRDA		0x80	/* IrDA mode is activated
						 * (could be turned on),
						 * default */

/* Bits for SPx ICG */
#define	MCS7840_DEV_SPx_ICG_DEF		0x24	/* All 8 bits is used as
						 * number of BAUD clocks of
						 * pause */

/*
 * Bits for RX_SAMPLINGxx registers
 * These registers control when bit value will be sampled within
 * the baud period.
 * 0 is very beginning of period, 15 is very end, 7 is the middle.
 */
#define	MCS7840_DEV_RX_SAMPLING1_MASK	0x0f	/* Bits for port 1 in
						 * RX_SAMPLING12 */
#define	MCS7840_DEV_RX_SAMPLING1_SHIFT	0	/* Shift for port 1in
						 * RX_SAMPLING12 */
#define	MCS7840_DEV_RX_SAMPLING2_MASK	0xf0	/* Bits for port 2 in
						 * RX_SAMPLING12 */
#define	MCS7840_DEV_RX_SAMPLING2_SHIFT	4	/* Shift for port 2 in
						 * RX_SAMPLING12 */
#define	MCS7840_DEV_RX_SAMPLING3_MASK	0x0f	/* Bits for port 3 in
						 * RX_SAMPLING23 */
#define	MCS7840_DEV_RX_SAMPLING3_SHIFT	0	/* Shift for port 3 in
						 * RX_SAMPLING23 */
#define	MCS7840_DEV_RX_SAMPLING4_MASK	0xf0	/* Bits for port 4 in
						 * RX_SAMPLING23 */
#define	MCS7840_DEV_RX_SAMPLING4_SHIFT	4	/* Shift for port 4 in
						 * RX_SAMPLING23 */
#define	MCS7840_DEV_RX_SAMPLINGx_MIN	0	/* Max for any RX Sampling */
#define	MCS7840_DEV_RX_SAMPLINGx_DEF	7	/* Default for any RX
						 * Sampling, center of period */
#define	MCS7840_DEV_RX_SAMPLINGx_MAX	15	/* Min for any RX Sampling */

/* Bits for ZERO_PERIODx */
#define	MCS7840_DEV_ZERO_PERIODx_DEF	20	/* Number of Bulk-in requests
						 * befor sending zero-sized
						 * reply */

/* Bits for ZERO_ENABLE */
#define	MCS7840_DEV_ZERO_ENABLE_PORT1	0x01	/* Enable of sending
						 * zero-sized replies for port
						 * 1, default */
#define	MCS7840_DEV_ZERO_ENABLE_PORT2	0x02	/* Enable of sending
						 * zero-sized replies for port
						 * 2, default */
#define	MCS7840_DEV_ZERO_ENABLE_PORT3	0x04	/* Enable of sending
						 * zero-sized replies for port
						 * 3, default */
#define	MCS7840_DEV_ZERO_ENABLE_PORT4	0x08	/* Enable of sending
						 * zero-sized replies for port
						 * 4, default */

/* Bits for THR_VAL_HIGHx */
#define	MCS7840_DEV_THR_VAL_HIGH_MASK	0x01	/* Only one bit is used */
#define	MCS7840_DEV_THR_VAL_HIGH_MUL	256	/* This one bit is means "256" */
#define	MCS7840_DEV_THR_VAL_HIGH_SHIFT	8	/* This one bit is means "256" */
#define	MCS7840_DEV_THR_VAL_HIGH_ENABLE	0x80	/* Enable threshold */

/* These are documented in "public" datasheet */
#define	MCS7840_DEV_REG_DCR0_1	0x04	/* Device contol register 0 for Port
					 * 1, R/W */
#define	MCS7840_DEV_REG_DCR1_1	0x05	/* Device contol register 1 for Port
					 * 1, R/W */
#define	MCS7840_DEV_REG_DCR2_1	0x06	/* Device contol register 2 for Port
					 * 1, R/W */
#define	MCS7840_DEV_REG_DCR0_2	0x16	/* Device contol register 0 for Port
					 * 2, R/W */
#define	MCS7840_DEV_REG_DCR1_2	0x17	/* Device contol register 1 for Port
					 * 2, R/W */
#define	MCS7840_DEV_REG_DCR2_2	0x18	/* Device contol register 2 for Port
					 * 2, R/W */
#define	MCS7840_DEV_REG_DCR0_3	0x19	/* Device contol register 0 for Port
					 * 3, R/W */
#define	MCS7840_DEV_REG_DCR1_3	0x1a	/* Device contol register 1 for Port
					 * 3, R/W */
#define	MCS7840_DEV_REG_DCR2_3	0x1b	/* Device contol register 2 for Port
					 * 3, R/W */
#define	MCS7840_DEV_REG_DCR0_4	0x1c	/* Device contol register 0 for Port
					 * 4, R/W */
#define	MCS7840_DEV_REG_DCR1_4	0x1d	/* Device contol register 1 for Port
					 * 4, R/W */
#define	MCS7840_DEV_REG_DCR2_4	0x1e	/* Device contol register 2 for Port
					 * 4, R/W */

/* Bits of DCR0 registers, documented in datasheet */
#define	MCS7840_DEV_DCR0_PWRSAVE		0x01	/* Shutdown transiver
							 * when USB Suspend is
							 * engaged, default = 1 */
#define	MCS7840_DEV_DCR0_RESERVED1		0x02	/* Unused */
#define	MCS7840_DEV_DCR0_GPIO_MODE_MASK		0x0c	/* GPIO Mode bits, WORKS
							 * ONLY FOR PORT 1 */
#define	MCS7840_DEV_DCR0_GPIO_MODE_IN		0x00	/* GPIO Mode - Input
							 * (0b00), WORKS ONLY
							 * FOR PORT 1 */
#define	MCS7840_DEV_DCR0_GPIO_MODE_OUT		0x08	/* GPIO Mode - Input
							 * (0b10), WORKS ONLY
							 * FOR PORT 1 */
#define	MCS7840_DEV_DCR0_RTS_ACTIVE_HIGH	0x10	/* RTS Active is HIGH,
							 * default = 0 (low) */
#define	MCS7840_DEV_DCR0_RTS_AUTO		0x20	/* RTS is controlled by
							 * state of TX buffer,
							 * default = 0
							 * (controlled by MCR) */
#define	MCS7840_DEV_DCR0_IRDA			0x40	/* IrDA mode */
#define	MCS7840_DEV_DCR0_RESERVED2		0x80	/* Unused */

/* Bits of DCR1 registers, documented in datasheet */
#define	MCS7840_DEV_DCR1_GPIO_CURRENT_MASK	0x03	/* Mask to extract GPIO
							 * current value, WORKS
							 * ONLY FOR PORT 1 */
#define	MCS7840_DEV_DCR1_GPIO_CURRENT_6MA	0x00	/* GPIO output current
							 * 6mA, WORKS ONLY FOR
							 * PORT 1 */
#define	MCS7840_DEV_DCR1_GPIO_CURRENT_8MA	0x01	/* GPIO output current
							 * 8mA, defauilt, WORKS
							 * ONLY FOR PORT 1 */
#define	MCS7840_DEV_DCR1_GPIO_CURRENT_10MA	0x02	/* GPIO output current
							 * 10mA, WORKS ONLY FOR
							 * PORT 1 */
#define	MCS7840_DEV_DCR1_GPIO_CURRENT_12MA	0x03	/* GPIO output current
							 * 12mA, WORKS ONLY FOR
							 * PORT 1 */
#define	MCS7840_DEV_DCR1_UART_CURRENT_MASK	0x0c	/* Mask to extract UART
							 * signals current value */
#define	MCS7840_DEV_DCR1_UART_CURRENT_6MA	0x00	/* UART output current
							 * 6mA */
#define	MCS7840_DEV_DCR1_UART_CURRENT_8MA	0x04	/* UART output current
							 * 8mA, defauilt */
#define	MCS7840_DEV_DCR1_UART_CURRENT_10MA	0x08	/* UART output current
							 * 10mA */
#define	MCS7840_DEV_DCR1_UART_CURRENT_12MA	0x0c	/* UART output current
							 * 12mA */
#define	MCS7840_DEV_DCR1_WAKEUP_DISABLE		0x10	/* Disable Remote USB
							 * Wakeup */
#define	MCS7840_DEV_DCR1_PLLPWRDOWN_DISABLE	0x20	/* Disable PLL power
							 * down when not needed,
							 * WORKS ONLY FOR PORT 1 */
#define	MCS7840_DEV_DCR1_LONG_INTERRUPT		0x40	/* Enable 13 bytes of
							 * interrupt data, with
							 * FIFO statistics,
							 * WORKS ONLY FOR PORT 1 */
#define	MCS7840_DEV_DCR1_RESERVED1		0x80	/* Unused */

/*
 * Bits of DCR2 registers, documented in datasheet
 * Wakeup will work only if DCR0_IRDA = 0 (RS-xxx mode) and
 * DCR1_WAKEUP_DISABLE = 0 (wakeup enabled).
 */
#define	MCS7840_DEV_DCR2_WAKEUP_CTS	0x01	/* Wakeup on CTS change,
						 * default = 0 */
#define	MCS7840_DEV_DCR2_WAKEUP_DCD	0x02	/* Wakeup on DCD change,
						 * default = 0 */
#define	MCS7840_DEV_DCR2_WAKEUP_RI	0x04	/* Wakeup on RI change,
						 * default = 1 */
#define	MCS7840_DEV_DCR2_WAKEUP_DSR	0x08	/* Wakeup on DSR change,
						 * default = 0 */
#define	MCS7840_DEV_DCR2_WAKEUP_RXD	0x10	/* Wakeup on RX Data change,
						 * default = 0 */
#define	MCS7840_DEV_DCR2_WAKEUP_RESUME	0x20	/* Wakeup issues RESUME
						 * signal, DISCONNECT
						 * otherwise, default = 1 */
#define	MCS7840_DEV_DCR2_RESERVED1	0x40	/* Unused */
#define	MCS7840_DEV_DCR2_SHDN_POLARITY	0x80	/* 0: Pin 12 Active Low, 1:
						 * Pin 12 Active High, default
						 * = 0 */

/* Interrupt endpoint bytes & bits */
#define	MCS7840_IEP_FIFO_STATUS_INDEX	5
/*
 * Thesse can be calculated as "1 << portnumber" for Bulk-out and
 * "1 << (portnumber+1)" for Bulk-in
 */
#define	MCS7840_IEP_BO_PORT1_HASDATA	0x01
#define	MCS7840_IEP_BI_PORT1_HASDATA	0x02
#define	MCS7840_IEP_BO_PORT2_HASDATA	0x04
#define	MCS7840_IEP_BI_PORT2_HASDATA	0x08
#define	MCS7840_IEP_BO_PORT3_HASDATA	0x10
#define	MCS7840_IEP_BI_PORT3_HASDATA	0x20
#define	MCS7840_IEP_BO_PORT4_HASDATA	0x40
#define	MCS7840_IEP_BI_PORT4_HASDATA	0x80

/* Documented UART registers (fully compatible with 16550 UART) */
#define	MCS7840_UART_REG_THR		0x00	/* Transmitter Holding
						 * Register W/Only */
#define	MCS7840_UART_REG_RHR		0x00	/* Receiver Holding Register
						 * R/Only */
#define	MCS7840_UART_REG_IER		0x01	/* Interrupt enable register -
						 * R/W */
#define	MCS7840_UART_REG_FCR		0x02	/* FIFO Control register -
						 * W/Only */
#define	MCS7840_UART_REG_ISR		0x02	/* Interrupt Status Registter
						 * R/Only */
#define	MCS7840_UART_REG_LCR		0x03	/* Line control register R/W */
#define	MCS7840_UART_REG_MCR		0x04	/* Modem control register R/W */
#define	MCS7840_UART_REG_LSR		0x05	/* Line status register R/Only */
#define	MCS7840_UART_REG_MSR		0x06	/* Modem status register
						 * R/Only */
#define	MCS7840_UART_REG_SCRATCHPAD	0x07	/* Scratch pad register */

#define	MCS7840_UART_REG_DLL		0x00	/* Low bits of BAUD divider */
#define	MCS7840_UART_REG_DLM		0x01	/* High bits of BAUD divider */

/* IER bits */
#define	MCS7840_UART_IER_RXREADY	0x01	/* RX Ready interrumpt mask */
#define	MCS7840_UART_IER_TXREADY	0x02	/* TX Ready interrumpt mask */
#define	MCS7840_UART_IER_RXSTAT		0x04	/* RX Status interrumpt mask */
#define	MCS7840_UART_IER_MODEM		0x08	/* Modem status change
						 * interrumpt mask */
#define	MCS7840_UART_IER_SLEEP		0x10	/* SLEEP enable */

/* FCR bits */
#define	MCS7840_UART_FCR_ENABLE		0x01	/* Enable FIFO */
#define	MCS7840_UART_FCR_FLUSHRHR	0x02	/* Flush RHR and FIFO */
#define	MCS7840_UART_FCR_FLUSHTHR	0x04	/* Flush THR and FIFO */
#define	MCS7840_UART_FCR_RTLMASK	0xa0	/* Mask to select RHR
						 * Interrupt Trigger level */
#define	MCS7840_UART_FCR_RTL_1_1	0x00	/* L1 = 1, L2 = 1 */
#define	MCS7840_UART_FCR_RTL_1_4	0x40	/* L1 = 1, L2 = 4 */
#define	MCS7840_UART_FCR_RTL_1_8	0x80	/* L1 = 1, L2 = 8 */
#define	MCS7840_UART_FCR_RTL_1_14	0xa0	/* L1 = 1, L2 = 14 */

/* ISR bits */
#define	MCS7840_UART_ISR_NOPENDING	0x01	/* No interrupt pending */
#define	MCS7840_UART_ISR_INTMASK	0x3f	/* Mask to select interrupt
						 * source */
#define	MCS7840_UART_ISR_RXERR		0x06	/* Recevir error */
#define	MCS7840_UART_ISR_RXHASDATA	0x04	/* Recevier has data */
#define	MCS7840_UART_ISR_RXTIMEOUT	0x0c	/* Recevier timeout */
#define	MCS7840_UART_ISR_TXEMPTY	0x02	/* Transmitter empty */
#define	MCS7840_UART_ISR_MSCHANGE	0x00	/* Modem status change */

/* LCR bits */
#define	MCS7840_UART_LCR_DATALENMASK	0x03	/* Mask for data length */
#define	MCS7840_UART_LCR_DATALEN5	0x00	/* 5 data bits */
#define	MCS7840_UART_LCR_DATALEN6	0x01	/* 6 data bits */
#define	MCS7840_UART_LCR_DATALEN7	0x02	/* 7 data bits */
#define	MCS7840_UART_LCR_DATALEN8	0x03	/* 8 data bits */

#define	MCS7840_UART_LCR_STOPBMASK	0x04	/* Mask for stop bits */
#define	MCS7840_UART_LCR_STOPB1		0x00	/* 1 stop bit in any case */
#define	MCS7840_UART_LCR_STOPB2		0x04	/* 1.5-2 stop bits depends on
						 * data length */

#define	MCS7840_UART_LCR_PARITYMASK	0x38	/* Mask for all parity data */
#define	MCS7840_UART_LCR_PARITYON	0x08	/* Parity ON/OFF - ON */
#define	MCS7840_UART_LCR_PARITYODD	0x00	/* Parity Odd */
#define	MCS7840_UART_LCR_PARITYEVEN	0x10	/* Parity Even */
#define	MCS7840_UART_LCR_PARITYODD	0x00	/* Parity Odd */
#define	MCS7840_UART_LCR_PARITYFORCE	0x20	/* Force parity odd/even */

#define	MCS7840_UART_LCR_BREAK		0x40	/* Send BREAK */
#define	MCS7840_UART_LCR_DIVISORS	0x80	/* Map DLL/DLM instead of
						 * xHR/IER */

/* LSR bits */
#define	MCS7840_UART_LSR_RHRAVAIL	0x01	/* Data available for read */
#define	MCS7840_UART_LSR_RHROVERRUN	0x02	/* Data FIFO/register overflow */
#define	MCS7840_UART_LSR_PARITYERR	0x04	/* Parity error */
#define	MCS7840_UART_LSR_FRAMEERR	0x10	/* Framing error */
#define	MCS7840_UART_LSR_BREAKERR	0x20	/* BREAK signal received */
#define	MCS7840_UART_LSR_THREMPTY	0x40	/* THR register is empty,
						 * ready for transmit */
#define	MCS7840_UART_LSR_HASERR		0x80	/* Has error in receiver FIFO */

/* MCR bits */
#define	MCS7840_UART_MCR_DTR		0x01	/* Force DTR to be active
						 * (low) */
#define	MCS7840_UART_MCR_RTS		0x02	/* Force RTS to be active
						 * (low) */
#define	MCS7840_UART_MCR_IE		0x04	/* Enable interrupts (from
						 * code, not documented) */
#define	MCS7840_UART_MCR_LOOPBACK	0x10	/* Enable local loopback test
						 * mode */
#define	MCS7840_UART_MCR_CTSRTS		0x20	/* Enable CTS/RTS flow control
						 * in 550 (FIFO) mode */
#define	MCS7840_UART_MCR_DTRDSR		0x40	/* Enable DTR/DSR flow control
						 * in 550 (FIFO) mode */
#define	MCS7840_UART_MCR_DCD		0x80	/* Enable DCD flow control in
						 * 550 (FIFO) mode */

/* MSR bits */
#define	MCS7840_UART_MSR_DELTACTS	0x01	/* CTS was changed since last
						 * read */
#define	MCS7840_UART_MSR_DELTADSR	0x02	/* DSR was changed since last
						 * read */
#define	MCS7840_UART_MSR_DELTARI	0x04	/* RI was changed from low to
						 * high since last read */
#define	MCS7840_UART_MSR_DELTADCD	0x08	/* DCD was changed since last
						 * read */
#define	MCS7840_UART_MSR_NEGCTS		0x10	/* Negated CTS signal */
#define	MCS7840_UART_MSR_NEGDSR		0x20	/* Negated DSR signal */
#define	MCS7840_UART_MSR_NEGRI		0x40	/* Negated RI signal */
#define	MCS7840_UART_MSR_NEGDCD		0x80	/* Negated DCD signal */

/* SCRATCHPAD bits */
#define	MCS7840_UART_SCRATCHPAD_RS232		0x00	/* RS-485 disabled */
#define	MCS7840_UART_SCRATCHPAD_RS485_DTRRX	0x80	/* RS-485 mode, DTR High
							 * = RX */
#define	MCS7840_UART_SCRATCHPAD_RS485_DTRTX	0xc0	/* RS-485 mode, DTR High
							 * = TX */

#define	MCS7840_CONFIG_INDEX	0
#define	MCS7840_IFACE_INDEX	0

#endif
