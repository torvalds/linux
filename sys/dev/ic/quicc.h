/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Juniper Networks
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_IC_QUICC_H_
#define	_DEV_IC_QUICC_H_

/*
 * Device parameter RAM
 */
#define	QUICC_PRAM_BASE		0x8000

#define	QUICC_PRAM_REV_NUM	(QUICC_PRAM_BASE + 0xaf0)

/* SCC parameter RAM. */
#define	QUICC_PRAM_SIZE_SCC	256
#define	QUICC_PRAM_BASE_SCC(u)	(QUICC_PRAM_BASE + QUICC_PRAM_SIZE_SCC * (u))

/* SCC parameters that are common for all modes. */
#define	QUICC_PRAM_SCC_RBASE(u)	(QUICC_PRAM_BASE_SCC(u) + 0x00)
#define	QUICC_PRAM_SCC_TBASE(u)	(QUICC_PRAM_BASE_SCC(u) + 0x02)
#define	QUICC_PRAM_SCC_RFCR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x04)
#define	QUICC_PRAM_SCC_TFCR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x05)
#define	QUICC_PRAM_SCC_MRBLR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x06)
#define	QUICC_PRAM_SCC_RBPTR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x10)
#define	QUICC_PRAM_SCC_TBPTR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x20)

/*
 * SCC parameters that are specific to UART/ASYNC mode.
 */
#define	QUICC_PRAM_SIZE_SCC_UART	0x68	/* Rounded up. */

#define	QUICC_PRAM_SCC_UART_MAX_IDL(u)	(QUICC_PRAM_BASE_SCC(u) + 0x38)
#define	QUICC_PRAM_SCC_UART_IDLC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x3a)
#define	QUICC_PRAM_SCC_UART_BRKCR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x3c)
#define	QUICC_PRAM_SCC_UART_PAREC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x3e)
#define	QUICC_PRAM_SCC_UART_FRMEC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x40)
#define	QUICC_PRAM_SCC_UART_NOSEC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x42)
#define	QUICC_PRAM_SCC_UART_BRKEC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x44)
#define	QUICC_PRAM_SCC_UART_BRKLN(u)	(QUICC_PRAM_BASE_SCC(u) + 0x46)
#define	QUICC_PRAM_SCC_UART_UADDR1(u)	(QUICC_PRAM_BASE_SCC(u) + 0x48)
#define	QUICC_PRAM_SCC_UART_UADDR2(u)	(QUICC_PRAM_BASE_SCC(u) + 0x4a)
#define	QUICC_PRAM_SCC_UART_TOSEQ(u)	(QUICC_PRAM_BASE_SCC(u) + 0x4e)
#define	QUICC_PRAM_SCC_UART_CC(u,n)	(QUICC_PRAM_BASE_SCC(u) + 0x50 + (n)*2)
#define	QUICC_PRAM_SCC_UART_RCCM(u)	(QUICC_PRAM_BASE_SCC(u) + 0x60)
#define	QUICC_PRAM_SCC_UART_RCCR(u)	(QUICC_PRAM_BASE_SCC(u) + 0x62)
#define	QUICC_PRAM_SCC_UART_RLBC(u)	(QUICC_PRAM_BASE_SCC(u) + 0x64)

/*
 * Interrupt controller.
 */
#define	QUICC_REG_SICR		0x10c00
#define	QUICC_REG_SIVEC		0x10c04
#define	QUICC_REG_SIPNR_H	0x10c08
#define	QUICC_REG_SIPNR_L	0x10c0c
#define	QUICC_REG_SCPRR_H	0x10c14
#define	QUICC_REG_SCPRR_L	0x10c18
#define	QUICC_REG_SIMR_H	0x10c1c
#define	QUICC_REG_SIMR_L	0x10c20
#define	QUICC_REG_SIEXR		0x10c24

/*
 * System clock control register.
 */
#define	QUICC_REG_SCCR		0x10c80

/*
 * Baudrate generator registers.
 */
#define	QUICC_REG_BRG(u)	(0x119f0 + ((u) & 3) * 4 - ((u) & 4) * 0x100)

/*
 * SCC registers.
 */
#define	QUICC_REG_SIZE_SCC	0x20
#define	QUICC_REG_BASE_SCC(u)	(0x11a00 + QUICC_REG_SIZE_SCC * (u))

#define	QUICC_REG_SCC_GSMR_L(u)	(QUICC_REG_BASE_SCC(u) + 0x00)
#define	QUICC_REG_SCC_GSMR_H(u)	(QUICC_REG_BASE_SCC(u) + 0x04)
#define	QUICC_REG_SCC_PSMR(u)	(QUICC_REG_BASE_SCC(u) + 0x08)
#define	QUICC_REG_SCC_TODR(u)	(QUICC_REG_BASE_SCC(u) + 0x0c)
#define	QUICC_REG_SCC_DSR(u)	(QUICC_REG_BASE_SCC(u) + 0x0e)
#define	QUICC_REG_SCC_SCCE(u)	(QUICC_REG_BASE_SCC(u) + 0x10)
#define	QUICC_REG_SCC_SCCM(u)	(QUICC_REG_BASE_SCC(u) + 0x14)
#define	QUICC_REG_SCC_SCCS(u)	(QUICC_REG_BASE_SCC(u) + 0x17)

#endif /* _DEV_IC_QUICC_H_ */
