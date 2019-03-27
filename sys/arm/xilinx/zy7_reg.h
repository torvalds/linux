/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 Thomas Skibo
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
 *
 * $FreeBSD$
 */

/*
 * Address regions of Zynq-7000.  
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.
 */

#ifndef _ZY7_REG_H_
#define _ZY7_REG_H_

/* PL AXI buses:  General Purpose Port #0, M_AXI_GP0. */
#define ZYNQ7_PLGP0_HWBASE	0x40000000
#define ZYNQ7_PLGP0_SIZE	0x40000000

/* PL AXI buses:  General Purpose Port #1, M_AXI_GP1. */
#define ZYNQ7_PLGP1_HWBASE	0x80000000
#define ZYNQ7_PLGP1_SIZE	0x40000000

/* I/O Peripheral registers. */
#define ZYNQ7_PSIO_HWBASE	0xE0000000
#define ZYNQ7_PSIO_SIZE		0x00300000

/* UART0 and UART1 */
#define ZYNQ7_UART0_HWBASE	(ZYNQ7_PSIO_HWBASE)
#define ZYNQ7_UART0_SIZE	0x1000

#define ZYNQ7_UART1_HWBASE	(ZYNQ7_PSIO_HWBASE+0x1000)
#define ZYNQ7_UART1_SIZE	0x1000


/* SMC Memories not mapped for now. */
#define ZYNQ7_SMC_HWBASE	0xE1000000
#define ZYNQ7_SMC_SIZE		0x05000000

/* SLCR, PS system, and CPU private registers combined in this region. */
#define ZYNQ7_PSCTL_HWBASE	0xF8000000
#define ZYNQ7_PSCTL_SIZE	0x01000000

#define ZYNQ7_SLCR_HWBASE	(ZYNQ7_PSCTL_HWBASE)
#define ZYNQ7_SLCR_SIZE		0x1000

#define ZYNQ7_DEVCFG_HWBASE	(ZYNQ7_PSCTL_HWBASE+0x7000)
#define ZYNQ7_DEVCFG_SIZE	0x1000

#endif /* _ZY7_REG_H_ */
