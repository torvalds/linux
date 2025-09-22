/*	$OpenBSD: cd1190reg.h,v 1.5 2022/01/09 05:42:38 jsg Exp $	*/

/*-
 * Copyright (c) 1998 Iain Hibbert.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Definitions for Cirrus Logic CD1190 parallel chips.
 */

/* ACK Width Register */
#define CD1190_AWR		0x03

/* Controller Command Register */
#define CD1190_CCR		0x0b
#define CD1190_CCR_CGC		(1<<7)	/* Change Global Config Command */
#define CD1190_CCR_CGC_RESET	 (1<<0) /* CGC Reset Command */
#define CD1190_CCR_PAR		(1<<6)	/* Parallel Command */
#define CD1190_CCR_PAR_DISABLE	 (1<<2) /* PAR Parallel Disable */
#define CD1190_CCR_PAR_FLUSH	 (1<<1) /* PAR Flush FIFO */
#define CD1190_CCR_PAR_ENABLE	 (1<<0) /* PAR Parallel Enable */
#define CD1190_CCR_SIG		(1<<5)	/* Signal Command */
#define CD1190_CCR_SIG_SET_BUSY	 (1<<2) /* SIG Set Busy Output */
#define CD1190_CCR_SIG_ACK	 (1<<1) /* SIG Pulse ACK Output */
#define CD1190_CCR_SIG_CLR_BUSY	 (1<<0) /* SIG Clear Busy Output */
#define CD1190_CCR_TIM		(1<<4)	/* Timer Command */
#define CD1190_CCR_TIM_ENABLE	 (1<<3) /* Timer Enabled */

/* Data Time-0ut Register */
#define CD1190_DTR		0x09

/* End Of Service Request Register */
#define CD1190_ESR		0x10

/* FIFO Count Register */
#define CD1190_FCR		0x0e

/* FIFO Data Register */
#define CD1190_FDR		0x12

/* Firmware Revision Register */
#define CD1190_FRR		0x0f

/* FIFO Threshold Register */
#define CD1190_FTR		0x08

/* Global Config Register */
#define CD1190_GCR		0x0a
#define CD1190_GCR_NOACK	(1<<2) /* NO-ACK handshaking */
#define CD1190_GCR_MODE		(1<<1) /* Peripheral/Controller Mode */
#define CD1190_GCR_DIR		(1<<0) /* Input/Output Direction */

/* defines for variable CD1190_IO */
#define CD1190_CO		0x00
#define CD1190_CI		0x01
#define CD1190_PO		0x02
#define CD1190_PI		0x03

/* Interrupt Config Register */
#define CD1190_ICR		0x01
#define CD1190_ICR_ENABLE	(1<<7) /* Enable Interrupts */
#define CD1190_ICR_ACK		(1<<3) /* Enable: Unsolicited ACK */
#define CD1190_ICR_FIFO		(1<<2) /* Enable: FIFO Thresh/Time */
#define CD1190_ICR_SIGNAL	(1<<1) /* Enable: Signal Status */
#define CD1190_ICR_TIMER	(1<<0) /* Enable: Timer Expired */

/* Interrupt Status Register */
#define CD1190_ISR		0x0c
#define CD1190_ISR_INTERRUPT	(1<<7) /* Interrupt Has Occurred */
#define CD1190_ISR_ACK		(1<<3) /* Unsolicited ACK */
#define CD1190_ISR_FIFO		(1<<2) /* FIFO Thresh/Time */
#define CD1190_ISR_SIGNAL	(1<<1) /* Signal Status */
#define CD1190_ISR_TIMER	(1<<0) /* Timer Expired */

/* Interrupt Vector Register */
#define CD1190_IVR		0x00

/* Parallel Status Register */
#define CD1190_PSR		0x1c
#define CD1190_PSR_ENP		(1<<7) /* Parallel Enabled */
#define CD1190_PSR_BUSY		(1<<6) /* Parallel Busy */
#define CD1190_PSR_ACK		(1<<5) /* Parallel Acknowledge */
#define CD1190_PSR_STATUS	(CD1190_PSR_ENP | CD1190_PSR_BUSY)
#define CD1190_PSR_NORMAL	CD1190_PSR_ENP

/* Signal Control Register */
#define CD1190_SCR		0x1e
#define CD1190_SCR_WRRD		(1<<7) /* Read Only: Write/Read */
#define CD1190_SCR_IP3		(1<<6) /* Read Only: Input Line 3 */
#define CD1190_SCR_IP2		(1<<5) /* Read Only: Input Line 2 */
#define CD1190_SCR_IP1		(1<<4) /* Read Only: Input Line 1 */
#define CD1190_SCR_WR_WRRD	(1<<3) /* Write/Read */
#define CD1190_SCR_OP3		(1<<2) /* Output Line 3 */
#define CD1190_SCR_OP2		(1<<1) /* Output Line 2 */
#define CD1190_SCR_OP1		(1<<0) /* Output Line 1 */

/* Input signals
 *
 *	IP3 	-	*ERROR
 *	IP2 	-	PAPER EMPTY / FAULT
 *	IP1 	-	SELECT
 *	WRRD	-	AFD
 *	OP3 	-	SLIN
 *	OP2 	-	*INIT / *RESET
 *	OP1 	-	N/A
 */
#define CD1190_SCR_NOERROR	CD1190_SCR_IP3 /* Printer Error (active low) */
#define CD1190_SCR_PE		CD1190_SCR_IP2 /* Paper Empty */
#define CD1190_SCR_SELECT	CD1190_SCR_IP1 /* Printer Select */

#define CD1190_SCR_STATUS	(CD1190_SCR_IP3 | CD1190_SCR_IP2 | CD1190_SCR_IP1)

/* Output signals - Active High?
 *
 *	IP3 	-	SLIN
 *	IP2 	-	*INIT / *RESET
 *	IP1 	-	N/A
 *	WRRD	-	AFD
 *	OP3 	-	*ERROR
 *	OP2 	-	SELECT
 *	OP1 	-	PAPER EMPTY / FAULT
 */
#define CD1190_SCR_RESET	CD1190_SCR_OP2
#define CD1190_SCR_SEL_IN	CD1190_SCR_OP3

/* Specification Register ZEROes */
#define CD1190_SR0		0x06
#define CD1190_SR0_WRRD		(1<<7) /* WR/RD 1 to 0 Change */
#define CD1190_SR0_IP3		(1<<6) /* IP3 1 to 0 Change */
#define CD1190_SR0_IP2		(1<<5) /* IP2 1 to 0 Change */
#define CD1190_SR0_IP1		(1<<4) /* IP1 1 to 0 Change */

/* Specification Register ONEs */
#define CD1190_SR1		0x07
#define CD1190_SR1_WRRD		(1<<7) /* WR/RD 0 to 1 Change */
#define CD1190_SR1_IP3		(1<<6) /* IP3 0 to 1 Change */
#define CD1190_SR1_IP2		(1<<5) /* IP2 0 to 1 Change */
#define CD1190_SR1_IP1		(1<<4) /* IP1 0 to 1 Change */

/* Signal Status Register */
#define CD1190_SSR		0x0d
#define CD1190_SSR_WRRD		(1<<7) /* WR/RD Change */
#define CD1190_SSR_IP3		(1<<6) /* IP3 Change */
#define CD1190_SSR_IP2		(1<<5) /* IP2 Change */
#define CD1190_SSR_IP1		(1<<4) /* IP1 Change */

/* Strobe Width Register */
#define CD1190_SWR		0x02

/* Timer Multiplier Register */
#define CD1190_TMR		0x05

/* Timer Prescale Register */
#define CD1190_TPR		0x04
