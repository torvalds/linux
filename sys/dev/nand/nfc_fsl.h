/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Juniper Networks, Inc.
 * Copyright (C) 2009-2012 Semihalf
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

#ifndef _NAND_NFC_FSL_H_
#define	_NAND_NFC_FSL_H_

/* LBC BR/OR Registers layout definitions */
#define BR_V		0x00000001
#define BR_V_SHIFT	0
#define BR_MSEL		0x000000E0
#define BR_MSEL_SHIFT	5
#define BR_DECC_CHECK_MODE	0x00000600
#define BR_DECC_CHECK_GEN	0x00000400

#define OR_FCM_PAGESIZE		0x00000400

/* Options definitions */
#define NAND_OPT_ECC_MODE_HW	1
#define NAND_OPT_ECC_MODE_SOFT	(1 << 1)

/* FMR - Flash Mode Register */
#define FMR_CWTO	0xF000
#define FMR_CWTO_SHIFT	12
#define FMR_BOOT	0x0800
#define FMR_ECCM	0x0100
#define FMR_AL		0x0030
#define FMR_AL_SHIFT	4
#define FMR_OP		0x0003
#define FMR_OP_SHIFT	0

#define FIR_OP_NOP	0x0 /* No operation and end of sequence */
#define FIR_OP_CA	0x1 /* Issue current column address */
#define FIR_OP_PA	0x2 /* Issue current block+page address */
#define FIR_OP_UA	0x3 /* Issue user defined address */
#define	FIR_OP_CM(x)	(4 + (x))	/* Issue command from FCR[CMD(x)] */
#define FIR_OP_WB	0x8 /* Write FBCR bytes from FCM buffer */
#define FIR_OP_WS	0x9 /* Write 1 or 2 bytes from MDR[AS] */
#define FIR_OP_RB	0xA /* Read FBCR bytes to FCM buffer */
#define FIR_OP_RS	0xB /* Read 1 or 2 bytes to MDR[AS] */
#define FIR_OP_CW0	0xC /* Wait then issue FCR[CMD0] */
#define FIR_OP_CW1	0xD /* Wait then issue FCR[CMD1] */
#define FIR_OP_RBW	0xE /* Wait then read FBCR bytes */
#define FIR_OP_RSW	0xF /* Wait then read 1 or 2 bytes */

/* LTESR - Transfer Error Status Register */
#define LTESR_BM	0x80000000
#define LTESR_FCT	0x40000000
#define LTESR_PAR	0x20000000
#define LTESR_WP	0x04000000
#define LTESR_ATMW	0x00800000
#define LTESR_ATMR	0x00400000
#define LTESR_CS	0x00080000
#define LTESR_CC	0x00000001

#define LTESR_NAND_MASK	(LTESR_FCT | LTESR_CC | LTESR_CS)

/* FPAR - Flash Page Address Register */
#define FPAR_SP_PI		0x00007C00
#define FPAR_SP_PI_SHIFT	10
#define FPAR_SP_MS		0x00000200
#define FPAR_SP_CI		0x000001FF
#define FPAR_SP_CI_SHIFT	0
#define FPAR_LP_PI		0x0003F000
#define FPAR_LP_PI_SHIFT	12
#define FPAR_LP_MS		0x00000800
#define FPAR_LP_CI		0x000007FF
#define FPAR_LP_CI_SHIFT	0

#define FSL_FCM_WAIT_TIMEOUT	10

#endif /* _NAND_NFC_FSL_H_ */
