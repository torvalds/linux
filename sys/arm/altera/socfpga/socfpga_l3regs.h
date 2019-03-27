/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#define	L3REGS_REMAP		0x0	/* Remap */
#define	 REMAP_LWHPS2FPGA	(1 << 4)
#define	 REMAP_HPS2FPGA		(1 << 3)
#define	 REMAP_MPUZERO		(1 << 0)
#define	L3REGS_L4MAIN		0x8	/* L4 main peripherals security */
#define	L3REGS_L4SP		0xC	/* L4 SP Peripherals Security */
#define	L3REGS_L4MP		0x10	/* L4 MP Peripherals Security */
#define	L3REGS_L4OSC1		0x14	/* L4 OSC1 Peripherals Security */
#define	L3REGS_L4SPIM		0x18	/* L4 SPIM Peripherals Security */
#define	L3REGS_STM		0x1C	/* STM Peripheral Security */
#define	L3REGS_LWHPS2FPGAREGS	0x20	/* LWHPS2FPGA AXI Bridge Security */
#define	L3REGS_USB1		0x28	/* USB1 Peripheral Security */
#define	L3REGS_NANDDATA		0x2C	/* NAND Flash Controller Data Sec */
#define	L3REGS_USB0		0x80	/* USB0 Peripheral Security */
#define	L3REGS_NANDREGS		0x84	/* NAND Flash Controller Security */
#define	L3REGS_QSPIDATA		0x88	/* QSPI Flash Controller Data Sec */
#define	L3REGS_FPGAMGRDATA	0x8C	/* FPGA Manager Data Peripheral Sec */
#define	L3REGS_HPS2FPGAREGS	0x90	/* HPS2FPGA AXI Bridge Perip. Sec */
#define	L3REGS_ACP		0x94	/* MPU ACP Peripheral Security */
#define	L3REGS_ROM		0x98	/* ROM Peripheral Security */
#define	L3REGS_OCRAM		0x9C	/* On-chip RAM Peripheral Security */
#define	L3REGS_SDRDATA		0xA0	/* SDRAM Data Peripheral Security */
