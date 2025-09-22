/*	$OpenBSD: pcicreg.h,v 1.1.1.1 2006/10/06 21:02:55 miod Exp $	*/
/*	$NetBSD: pcicreg.h,v 1.2 2005/12/11 12:18:58 christos Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sh/devreg.h>

/*
 * PCI Controller
 */

#define	SH4_PCIC		0xfe200000

#define	SH4_PCIC_IO		0xfe240000
#define	SH4_PCIC_IO_SIZE	0x00040000
#define	SH4_PCIC_IO_MASK	(SH4_PCIC_IO_SIZE-1)
#define	SH4_PCIC_MEM		0xfd000000
#define	SH4_PCIC_MEM_SIZE	0x01000000
#define	SH4_PCIC_MEM_MASK	(SH4_PCIC_MEM_SIZE-1)

#define	SH4_PCICONF	(SH4_PCIC+0x000)	/* 32bit */
#define	SH4_PCICONF0	(SH4_PCICONF+0x00)	/* 32bit */
#define	SH4_PCICONF1	(SH4_PCICONF+0x04)	/* 32bit */
#define	SH4_PCICONF2	(SH4_PCICONF+0x08)	/* 32bit */
#define	SH4_PCICONF3	(SH4_PCICONF+0x0c)	/* 32bit */
#define	SH4_PCICONF4	(SH4_PCICONF+0x10)	/* 32bit */
#define	SH4_PCICONF5	(SH4_PCICONF+0x14)	/* 32bit */
#define	SH4_PCICONF6	(SH4_PCICONF+0x18)	/* 32bit */
#define	SH4_PCICONF7	(SH4_PCICONF+0x1c)	/* 32bit */
#define	SH4_PCICONF8	(SH4_PCICONF+0x20)	/* 32bit */
#define	SH4_PCICONF9	(SH4_PCICONF+0x24)	/* 32bit */
#define	SH4_PCICONF10	(SH4_PCICONF+0x28)	/* 32bit */
#define	SH4_PCICONF11	(SH4_PCICONF+0x2c)	/* 32bit */
#define	SH4_PCICONF12	(SH4_PCICONF+0x30)	/* 32bit */
#define	SH4_PCICONF13	(SH4_PCICONF+0x34)	/* 32bit */
#define	SH4_PCICONF14	(SH4_PCICONF+0x38)	/* 32bit */
#define	SH4_PCICONF15	(SH4_PCICONF+0x3c)	/* 32bit */
#define	SH4_PCICONF16	(SH4_PCICONF+0x40)	/* 32bit */
#define	SH4_PCICONF17	(SH4_PCICONF+0x44)	/* 32bit */
#define	SH4_PCICR	(SH4_PCIC+0x100)	/* 32bit */
#define	SH4_PCILSR0	(SH4_PCIC+0x104)	/* 32bit */
#define	SH4_PCILSR1	(SH4_PCIC+0x108)	/* 32bit */
#define	SH4_PCILAR0	(SH4_PCIC+0x10c)	/* 32bit */
#define	SH4_PCILAR1	(SH4_PCIC+0x110)	/* 32bit */
#define	SH4_PCIINT	(SH4_PCIC+0x114)	/* 32bit */
#define	SH4_PCIINTM	(SH4_PCIC+0x118)	/* 32bit */
#define	SH4_PCIALR	(SH4_PCIC+0x11c)	/* 32bit */
#define	SH4_PCICLR	(SH4_PCIC+0x120)	/* 32bit */
#define	SH4_PCIAINT	(SH4_PCIC+0x130)	/* 32bit */
#define	SH4_PCIAINTM	(SH4_PCIC+0x134)	/* 32bit */
#define	SH4_PCIDMABT	(SH4_PCIC+0x140)	/* 32bit */
#define	SH4_PCIDPA0	(SH4_PCIC+0x180)	/* 32bit */
#define	SH4_PCIDLA0	(SH4_PCIC+0x184)	/* 32bit */
#define	SH4_PCIDTC0	(SH4_PCIC+0x188)	/* 32bit */
#define	SH4_PCIDCR0	(SH4_PCIC+0x18c)	/* 32bit */
#define	SH4_PCIDPA1	(SH4_PCIC+0x190)	/* 32bit */
#define	SH4_PCIDLA1	(SH4_PCIC+0x194)	/* 32bit */
#define	SH4_PCIDTC1	(SH4_PCIC+0x198)	/* 32bit */
#define	SH4_PCIDCR1	(SH4_PCIC+0x19c)	/* 32bit */
#define	SH4_PCIDPA2	(SH4_PCIC+0x1a0)	/* 32bit */
#define	SH4_PCIDLA2	(SH4_PCIC+0x1a4)	/* 32bit */
#define	SH4_PCIDTC2	(SH4_PCIC+0x1a8)	/* 32bit */
#define	SH4_PCIDCR2	(SH4_PCIC+0x1ac)	/* 32bit */
#define	SH4_PCIDPA3	(SH4_PCIC+0x1b0)	/* 32bit */
#define	SH4_PCIDLA3	(SH4_PCIC+0x1b4)	/* 32bit */
#define	SH4_PCIDTC3	(SH4_PCIC+0x1b8)	/* 32bit */
#define	SH4_PCIDCR3	(SH4_PCIC+0x1bc)	/* 32bit */
#define	SH4_PCIPAR	(SH4_PCIC+0x1c0)	/* 32bit */
#define	SH4_PCIMBR	(SH4_PCIC+0x1c4)	/* 32bit */
#define	SH4_PCIIOBR	(SH4_PCIC+0x1c8)	/* 32bit */
#define	SH4_PCIPINT	(SH4_PCIC+0x1cc)	/* 32bit */
#define	SH4_PCIPINTM	(SH4_PCIC+0x1d0)	/* 32bit */
#define	SH4_PCICLKR	(SH4_PCIC+0x1d4)	/* 32bit */
#define	SH4_PCIBCR1	(SH4_PCIC+0x1e0)	/* 32bit */
#define	SH4_PCIBCR2	(SH4_PCIC+0x1e4)	/* 32bit */
#define	SH4_PCIWCR1	(SH4_PCIC+0x1e8)	/* 32bit */
#define	SH4_PCIWCR2	(SH4_PCIC+0x1ec)	/* 32bit */
#define	SH4_PCIWCR3	(SH4_PCIC+0x1f0)	/* 32bit */
#define	SH4_PCIMCR	(SH4_PCIC+0x1f4)	/* 32bit */
#define	SH4_PCIBCR3	(SH4_PCIC+0x1f8)	/* 32bit: SH7751R */
#define	SH4_PCIPCTR	(SH4_PCIC+0x200)	/* 32bit */
#define	SH4_PCIPDTR	(SH4_PCIC+0x204)	/* 32bit */
#define	SH4_PCIPDR	(SH4_PCIC+0x220)	/* 32bit */

#define	PCICR_BASE		0xa5000000
#define	PCICR_TRDSGL		0x00000200
#define	PCICR_BYTESWAP		0x00000100
#define	PCICR_PCIPUP		0x00000080
#define	PCICR_BMABT		0x00000040
#define	PCICR_MD10		0x00000020
#define	PCICR_MD9		0x00000010
#define	PCICR_SERR		0x00000008
#define	PCICR_INTA		0x00000004
#define	PCICR_RSTCTL		0x00000002
#define	PCICR_CFINIT		0x00000001

#define	PCIINT_M_LOCKON		0x00008000
#define	PCIINT_T_TGT_ABORT	0x00004000
#define	PCIINT_TGT_RETRY	0x00000200
#define	PCIINT_MST_DIS		0x00000100
#define	PCIINT_ADRPERR		0x00000080
#define	PCIINT_SERR_DET		0x00000040
#define	PCIINT_T_DPERR_WT	0x00000020
#define	PCIINT_T_PERR_DET	0x00000010
#define	PCIINT_M_TGT_ABORT	0x00000008
#define	PCIINT_M_MST_ABORT	0x00000004
#define	PCIINT_M_DPERR_WT	0x00000002
#define	PCIINT_M_DPERR_RD	0x00000001
#define	PCIINT_ALL		0x0000c3ff
#define	PCIINT_CLEAR_ALL	PCIINT_ALL

#define	PCIINTM_MASK_ALL	0x00000000
#define	PCIINTM_UNMASK_ALL	PCIINT_ALL

#define	PCIMBR_MASK		0xff000000

#define	PCIIOBR_MASK		0xffc00000
