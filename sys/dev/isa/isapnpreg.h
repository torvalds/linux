/*	$OpenBSD: isapnpreg.h,v 1.4 1997/12/25 09:22:41 downsj Exp $	*/
/*	$NetBSD: isapnpreg.h,v 1.5 1997/08/12 07:34:34 mikel Exp $	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef _DEV_ISA_ISAPNPREG_H_
#define _DEV_ISA_ISAPNPREG_H_

/*
 * ISA Plug and Play register definitions;
 * From Plug and Play ISA Specification V1.0a, May 5 1994
 */

#define ISAPNP_MAX_CARDS		8
#define ISAPNP_MAX_IDENT		32
#define ISAPNP_MAX_DEVCLASS		16
#define ISAPNP_SERIAL_SIZE		9
#define ISAPNP_MAX_TAGSIZE		256

#define ISAPNP_ADDR	0x279	/* Write only */
#define ISAPNP_WRDATA	0xa79	/* Write only */

/* The read port is in range 0x203 to 0x3ff */
#define ISAPNP_RDDATA_MIN	0x203	/* Read only */
#define ISAPNP_RDDATA_MAX	0x3ff

#define ISAPNP_LFSR_INIT	0x6A	/* Initial value of LFSR sequence */
#define ISAPNP_LFSR_LENGTH	32	/* Number of values in LFSR sequence */
				/* Formula to compute the next value */
#define ISAPNP_LFSR_NEXT(v) (((v) >> 1) | (((v) & 1) ^ (((v) & 2) >> 1)) << 7)

#define ISAPNP_SET_RD_PORT				0x00
#define ISAPNP_SERIAL_ISOLATION				0x01
#define ISAPNP_CONFIG_CONTROL				0x02
#define		ISAPNP_CC_RESET				0x01
#define		ISAPNP_CC_WAIT_FOR_KEY			0x02
#define		ISAPNP_CC_RESET_CSN			0x04
#define		ISAPNP_CC_RESET_DRV			0x07
#define ISAPNP_WAKE					0x03
#define ISAPNP_RESOURCE_DATA				0x04
#define ISAPNP_STATUS					0x05
#define ISAPNP_CARD_SELECT_NUM				0x06
#define ISAPNP_LOGICAL_DEV_NUM				0x07

#define ISAPNP_ACTIVATE					0x30
#define ISAPNP_IO_RANGE_CHECK				0x31

#define ISAPNP_NUM_MEM					4
#define ISAPNP_MEM_DESC { 0x40, 0x48, 0x50, 0x58 }
#define		ISAPNP_MEM_BASE_23_16			0x0
#define		ISAPNP_MEM_BASE_15_8			0x1
#define		ISAPNP_MEM_CONTROL			0x2
#define			ISAPNP_MEM_CONTROL_LIMIT	1
#define			ISAPNP_MEM_CONTROL_16		2
#define		ISAPNP_MEM_LRANGE_23_16			0x3
#define		ISAPNP_MEM_LRANGE_15_8			0x4

#define ISAPNP_NUM_IO					8
#define ISAPNP_IO_DESC { 0x60, 0x62, 0x64, 0x68, 0x6a, 0x6c, 0x6e }
#define		ISAPNP_IO_BASE_15_8			0x0
#define		ISAPNP_IO_BASE_7_0			0x1

#define ISAPNP_NUM_IRQ					16
#define ISAPNP_IRQ_DESC { 0x70, 0x72 }
#define		ISAPNP_IRQ_NUMBER			0x0
#define		ISAPNP_IRQ_CONTROL			0x1
#define			ISAPNP_IRQ_LEVEL		1
#define			ISAPNP_IRQ_HIGH			2

#define ISAPNP_NUM_DRQ					8
#define ISAPNP_DRQ_DESC { 0x74, 0x75 }

#define ISAPNP_NUM_MEM32				4
#define ISAPNP_MEM32_DESC { 0x76, 0x80, 0x90, 0xa0 }
#define		ISAPNP_MEM32_BASE_31_24			0x0
#define		ISAPNP_MEM32_BASE_23_16			0x1
#define		ISAPNP_MEM32_BASE_15_8			0x2
#define		ISAPNP_MEM32_BASE_7_0			0x3
#define		ISAPNP_MEM32_CONTROL			0x4
#define			ISAPNP_MEM32_CONTROL_LIMIT	1
#define			ISAPNP_MEM32_CONTROL_16		2
#define			ISAPNP_MEM32_CONTROL_32		6
#define		ISAPNP_MEM32_LRANGE_31_24		0x5
#define		ISAPNP_MEM32_LRANGE_23_16		0x6
#define		ISAPNP_MEM32_LRANGE_15_8		0x7
#define		ISAPNP_MEM32_LRANGE_7_0			0x8

/* Small Tags */
#define ISAPNP_TAG_VERSION_NUM				0x1
#define ISAPNP_TAG_LOGICAL_DEV_ID			0x2
#define ISAPNP_TAG_COMPAT_DEV_ID			0x3
#define ISAPNP_TAG_IRQ_FORMAT				0x4
#define		ISAPNP_IRQTYPE_EDGE_PLUS		1
#define		ISAPNP_IRQTYPE_EDGE_MINUS		2
#define		ISAPNP_IRQTYPE_LEVEL_PLUS		4
#define		ISAPNP_IRQTYPE_LEVEL_MINUS		8
#define ISAPNP_TAG_DMA_FORMAT				0x5
#define		ISAPNP_DMAWIDTH_8			0x00
#define		ISAPNP_DMAWIDTH_8_16			0x01
#define		ISAPNP_DMAWIDTH_16			0x02
#define		ISAPNP_DMAWIDTH_RESERVED		0x03
#define		ISAPNP_DMAWIDTH_MASK			0x03
#define		ISAPNP_DMAATTR_BUS_MASTER		0x04
#define		ISAPNP_DMAATTR_INCR_8			0x08
#define		ISAPNP_DMAATTR_INCR_16			0x10
#define		ISAPNP_DMAATTR_MASK			0x1c
#define		ISAPNP_DMASPEED_COMPAT			0x00
#define		ISAPNP_DMASPEED_A			0x20
#define		ISAPNP_DMASPEED_B			0x40
#define		ISAPNP_DMASPEED_F			0x60
#define		ISAPNP_DMASPEED_MASK			0x60
#define ISAPNP_TAG_DEP_START				0x6
#define		ISAPNP_DEP_PREFERRED			0x0
#define		ISAPNP_DEP_ACCEPTABLE			0x1
#define		ISAPNP_DEP_FUNCTIONAL			0x2
#define		ISAPNP_DEP_RESERVED			0x3
#define		ISAPNP_DEP_MASK				0x3
#define		ISAPNP_DEP_UNSET			0x80	/* Internal */
#define		ISAPNP_DEP_CONFLICTING			0x81	/* Internal */
#define ISAPNP_TAG_DEP_END				0x7
#define ISAPNP_TAG_IO_PORT_DESC				0x8
#define		ISAPNP_IOFLAGS_16			0x1
#define ISAPNP_TAG_FIXED_IO_PORT_DESC			0x9
#define ISAPNP_TAG_RESERVED1				0xa
#define ISAPNP_TAG_RESERVED2				0xb
#define ISAPNP_TAG_RESERVED3				0xc
#define ISAPNP_TAG_RESERVED4				0xd
#define ISAPNP_TAG_VENDOR_DEF				0xe
#define ISAPNP_TAG_END					0xf

/* Large Tags */
#define ISAPNP_LARGE_TAG				0x80
#define ISAPNP_TAG_MEM_RANGE_DESC			0x81
#define		ISAPNP_MEMATTR_WRITEABLE		0x01
#define		ISAPNP_MEMATTR_CACHEABLE		0x02
#define		ISAPNP_MEMATTR_HIGH_ADDR		0x04
#define		ISAPNP_MEMATTR_SHADOWABLE		0x20
#define		ISAPNP_MEMATTR_ROM			0x40
#define		ISAPNP_MEMATTR_MASK			0x67
#define		ISAPNP_MEMWIDTH_8			0x00
#define		ISAPNP_MEMWIDTH_16			0x08
#define		ISAPNP_MEMWIDTH_8_16			0x10
#define		ISAPNP_MEMWIDTH_32			0x18
#define		ISAPNP_MEMWIDTH_MASK			0x18
#define ISAPNP_TAG_ANSI_IDENT_STRING			0x82
#define ISAPNP_TAG_UNICODE_IDENT_STRING			0x83
#define ISAPNP_TAG_VENDOR_DEFINED			0x84
#define ISAPNP_TAG_MEM32_RANGE_DESC			0x85
#define ISAPNP_TAG_FIXED_MEM32_RANGE_DESC		0x86

#endif	/* _DEV_ISA_ISAPNPREG_H_ */
