/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Ingenic JZ4780 SMB Controller
 */

#ifndef __JZ4780_SMB_H__
#define __JZ4780_SMB_H__

#define	SMBCON			0x00
#define	 SMBCON_STPHLD		(1 << 7)
#define	 SMBCON_SLVDIS		(1 << 6)
#define	 SMBCON_REST		(1 << 5)
#define	 SMBCON_MATP		(1 << 4)
#define	 SMBCON_SATP		(1 << 3)
#define	 SMBCON_SPD		(3 << 1)
#define	 SMBCON_SPD_STANDARD	(1 << 1)
#define	 SMBCON_SPD_FAST	(2 << 1)
#define	 SMBCON_MD		(1 << 0)
#define	SMBTAR			0x04
#define	 SMBTAR_MATP		(1 << 12)
#define	 SMBTAR_SPECIAL		(1 << 11)
#define	 SMBTAR_GC_OR_START	(1 << 10)
#define	 SMBTAR_SMBTAR		(0x3ff << 0)
#define	SMBSAR			0x08
#define	SMBDC			0x10
#define	 SMBDC_CMD		(1 << 8)
#define	 SMBDC_DAT		(0xff << 0)
#define	SMBSHCNT		0x14
#define	SMBSLCNT		0x18
#define	SMBFHCNT		0x1c
#define	SMBFLCNT		0x20
#define	SMBINTST		0x2c
#define	SMBINTM			0x30
#define	SMBRXTL			0x38
#define	SMBTXTL			0x3c
#define	SMBCINT			0x40
#define	SMBCRXUF		0x44
#define	SMBCRXOF		0x48
#define	SMBCTXOF		0x4c
#define	SMBCRXREQ		0x50
#define	SMBCTXABT		0x54
#define	SMBCRXDN		0x58
#define	SMBCACT			0x5c
#define	SMBCSTP			0x60
#define	SMBCSTT			0x64
#define	SMBCGC			0x68
#define	SMBENB			0x6c
#define	 SMBENB_SMBENB		(1 << 0)
#define	SMBST			0x70
#define	 SMBST_SLVACT		(1 << 6)
#define	 SMBST_MSTACT		(1 << 5)
#define	 SMBST_RFF		(1 << 4)
#define	 SMBST_RFNE		(1 << 3)
#define	 SMBST_TFE		(1 << 2)
#define	 SMBST_TFNF		(1 << 1)
#define	 SMBST_ACT		(1 << 0)
#define	SMBABTSRC		0x80
#define	SMBDMACR		0x88
#define	SMBDMATDLR		0x8c
#define	SMBDMARDLR		0x90
#define	SMBSDASU		0x94
#define	SMBACKGC		0x98
#define	SMBENBST		0x9c
#define	 SMBENBST_SLVRDLST	(1 << 2)
#define	 SMBENBST_SLVDISB	(1 << 1)
#define	 SMBENBST_SMBEN		(1 << 0)
#define	SMBSDAHD		0xd0		
#define	 SMBSDAHD_HDENB		(1 << 8)
#define	 SMBSDAHD_SDAHD		(0xff << 0)

#endif /* !__JZ4780_SMB_H__ */
