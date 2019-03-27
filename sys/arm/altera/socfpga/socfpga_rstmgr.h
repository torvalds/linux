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

#define	RSTMGR_STAT		0x0	/* Status */
#define	RSTMGR_CTRL		0x4	/* Control */
#define	 CTRL_SWWARMRSTREQ	(1 << 1) /* Trigger warm reset */
#define	RSTMGR_COUNTS		0x8	/* Reset Cycles Count */
#define	RSTMGR_MPUMODRST	0x10	/* MPU Module Reset */
#define	 MPUMODRST_CPU1			(1 << 1)
#define	RSTMGR_PERMODRST	0x14	/* Peripheral Module Reset */
#define	RSTMGR_PER2MODRST	0x18	/* Peripheral 2 Module Reset */
#define	RSTMGR_BRGMODRST	0x1C	/* Bridge Module Reset */
#define	 BRGMODRST_FPGA2HPS	(1 << 2)
#define	 BRGMODRST_LWHPS2FPGA	(1 << 1)
#define	 BRGMODRST_HPS2FPGA	(1 << 0)
#define	RSTMGR_MISCMODRST	0x20	/* Miscellaneous Module Reset */

#define	RSTMGR_A10_CTRL		0xC	/* Control */
#define	RSTMGR_A10_MPUMODRST	0x20	/* MPU Module Reset */

int rstmgr_warmreset(uint32_t reg);
