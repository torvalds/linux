/*	$OpenBSD: bt463reg.h,v 1.3 2008/06/26 05:42:15 ray Exp $ */
/*	$NetBSD: bt463reg.h,v 1.1 1998/08/18 07:43:09 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register definitions for the Brooktree Bt463 135MHz Monolithic
 * CMOS TrueVu RAMDAC.
 */

/*
 * Directly-accessible registers.  Note the address register is
 * auto-incrementing.
 */
#define	BT463_REG_ADDR_LOW		0x00	/* C1,C0 == 0,0 */
#define	BT463_REG_ADDR_HIGH		0x01	/* C1,C0 == 0,1 */
#define	BT463_REG_IREG_DATA		0x02	/* C1,C0 == 1,0 */
#define	BT463_REG_CMAP_DATA		0x03	/* C1,C0 == 1,1 */

#define	BT463_REG_MAX			BT463_REG_CMAP_DATA

/*
 * All internal register access to the Bt463 is done indirectly via the
 * Address Register (mapped into the host bus in a device-specific
 * fashion).  The following register definitions are in terms of
 * their address register address values.
 */

/* C1,C0 must be 1,0 */
#define	BT463_IREG_CURSOR_COLOR_0	0x0100	/* 3 r/w cycles */
#define	BT463_IREG_CURSOR_COLOR_1	0x0101	/* 3 r/w cycles */
#define	BT463_IREG_ID			0x0200
#define	BT463_IREG_COMMAND_0		0x0201
#define	BT463_IREG_COMMAND_1		0x0202
#define	BT463_IREG_COMMAND_2		0x0203
#define	BT463_IREG_READ_MASK_P0_P7	0x0205
#define	BT463_IREG_READ_MASK_P8_P15	0x0206
#define	BT463_IREG_READ_MASK_P16_P23	0x0207
#define	BT463_IREG_READ_MASK_P24_P27	0x0208
#define	BT463_IREG_BLINK_MASK_P0_P7	0x0209
#define	BT463_IREG_BLINK_MASK_P8_P15	0x020a
#define	BT463_IREG_BLINK_MASK_P16_P23	0x020b
#define	BT463_IREG_BLINK_MASK_P24_P27	0x020c
#define	BT463_IREG_TEST			0x020d
#define	BT463_IREG_INPUT_SIG		0x020e	/* 2 of 3 r/w cycles */
#define	BT463_IREG_OUTPUT_SIG		0x020f	/* 3 r/w cycles */
#define	BT463_IREG_REVISION		0x0220
#define	BT463_IREG_WINDOW_TYPE_TABLE	0x0300	/* 3 r/w cycles */

#define	BT463_NWTYPE_ENTRIES		0x10	/* 16 window type entries */

/* C1,C0 must be 1,1 */
#define	BT463_IREG_CPALETTE_RAM		0x0000	/* 3 r/w cycles */

#define	BT463_NCMAP_ENTRIES		0x210	/* 528 CMAP entries */
