/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007, Juniper Networks, Inc.
 * Copyright (c) 2012-2013, SRI International
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * (FA8750-10-C-0237) ("CTSRD"), as part of the DARPA CRASH research
 * programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef _DEV_CFI_REG_H_
#define	_DEV_CFI_REG_H_

struct cfi_qry {
	u_char		reserved[16];
	u_char		ident[3];	/* "QRY" */
	u_char		pri_vend[2];
	u_char		pri_vend_eqt[2];
	u_char		alt_vend[2];
	u_char		alt_vend_eqt[2];
	/* System Interface Information. */
	u_char		min_vcc;
	u_char		max_vcc;
	u_char		min_vpp;
	u_char		max_vpp;
	u_char		tto_byte_write;		/* 2**n microseconds. */
	u_char		tto_buf_write;		/* 2**n microseconds. */
	u_char		tto_block_erase;	/* 2**n milliseconds. */
	u_char		tto_chip_erase;		/* 2**n milliseconds. */
	u_char		mto_byte_write;		/* 2**n times typical t/o. */
	u_char		mto_buf_write;		/* 2**n times typical t/o. */
	u_char		mto_block_erase;	/* 2**n times typical t/o. */
	u_char		mto_chip_erase;		/* 2**n times typical t/o. */
	/* Device Geometry Definition. */
	u_char		size;			/* 2**n bytes. */
	u_char		iface[2];
	u_char		max_buf_write_size[2];	/* 2**n. */
	u_char		nregions;		/* Number of erase regions. */
	u_char		region[4];		/* Single entry. */
	/* Additional entries follow. */
	/* Primary Vendor-specific Extended Query table follows. */
	/* Alternate Vendor-specific Extended Query table follows. */
};

#define	CFI_QRY_CMD_ADDR	0x55
#define	CFI_QRY_CMD_DATA	0x98

#define	CFI_QRY_IDENT		offsetof(struct cfi_qry, ident)
#define	CFI_QRY_VEND		offsetof(struct cfi_qry, pri_vend)

#define	CFI_QRY_TTO_WRITE	offsetof(struct cfi_qry, tto_byte_write)
#define	CFI_QRY_TTO_BUFWRITE	offsetof(struct cfi_qry, tto_buf_write)
#define	CFI_QRY_TTO_ERASE	offsetof(struct cfi_qry, tto_block_erase)
#define	CFI_QRY_MTO_WRITE	offsetof(struct cfi_qry, mto_byte_write)
#define	CFI_QRY_MTO_BUFWRITE	offsetof(struct cfi_qry, mto_buf_write)
#define	CFI_QRY_MTO_ERASE	offsetof(struct cfi_qry, mto_block_erase)

#define	CFI_QRY_SIZE		offsetof(struct cfi_qry, size)
#define	CFI_QRY_IFACE		offsetof(struct cfi_qry, iface)
#define	CFI_QRY_MAXBUF		offsetof(struct cfi_qry, max_buf_write_size)
#define	CFI_QRY_NREGIONS	offsetof(struct cfi_qry, nregions)
#define	CFI_QRY_REGION0		offsetof(struct cfi_qry, region)
#define	CFI_QRY_REGION(x)	(CFI_QRY_REGION0 + (x) * 4)

#define	CFI_VEND_NONE		0x0000
#define	CFI_VEND_INTEL_ECS	0x0001
#define	CFI_VEND_AMD_SCS	0x0002
#define	CFI_VEND_INTEL_SCS	0x0003
#define	CFI_VEND_AMD_ECS	0x0004
#define	CFI_VEND_MITSUBISHI_SCS	0x0100
#define	CFI_VEND_MITSUBISHI_ECS	0x0101

#define	CFI_IFACE_X8		0x0000
#define	CFI_IFACE_X16		0x0001
#define	CFI_IFACE_X8X16		0x0002
#define	CFI_IFACE_X32		0x0003
#define	CFI_IFACE_X16X32	0x0005

/* Standard Command Set (aka Basic Command Set) */
#define	CFI_BCS_BLOCK_ERASE	0x20
#define	CFI_BCS_PROGRAM		0x40
#define	CFI_BCS_CLEAR_STATUS	0x50
#define	CFI_BCS_READ_STATUS	0x70
#define	CFI_BCS_ERASE_SUSPEND	0xb0
#define	CFI_BCS_ERASE_RESUME	0xd0	/* Equals CONFIRM */
#define	CFI_BCS_CONFIRM		0xd0
#define	CFI_BCS_BUF_PROG_SETUP	0xe8
#define	CFI_BCS_READ_ARRAY	0xff
#define	CFI_BCS_READ_ARRAY2	0xf0

/* Intel commands. */
#define	CFI_INTEL_LB		0x01	/* Lock Block */
#define	CFI_INTEL_LBS		0x60	/* Lock Block Setup */
#define	CFI_INTEL_READ_ID	0x90	/* Read Identifier */
#define	CFI_INTEL_PP_SETUP	0xc0	/* Protection Program Setup */
#define	CFI_INTEL_UB		0xd0	/* Unlock Block */

/* NB: these are addresses for 16-bit accesses */
#define	CFI_INTEL_PLR		0x80	/* Protection Lock Register */
#define	CFI_INTEL_PR(n)		(0x81+(n)) /* Protection Register */

/* Status register definitions */
#define	CFI_INTEL_STATUS_WSMS	0x0080	/* Write Machine Status */
#define	CFI_INTEL_STATUS_ESS	0x0040	/* Erase Suspend Status */
#define	CFI_INTEL_STATUS_ECLBS	0x0020	/* Erase and Clear Lock-Bit Status */
#define	CFI_INTEL_STATUS_PSLBS	0x0010	/* Program and Set Lock-Bit Status */
#define	CFI_INTEL_STATUS_VPENS	0x0008	/* Programming Voltage Status */
#define	CFI_INTEL_STATUS_PSS	0x0004	/* Program Suspend Status */
#define	CFI_INTEL_STATUS_DPS	0x0002	/* Device Protect Status */
#define	CFI_INTEL_STATUS_RSVD	0x0001	/* reserved */

/* eXtended Status register definitions */
#define	CFI_INTEL_XSTATUS_WBS	0x8000	/* Write Buffer Status */
#define	CFI_INTEL_XSTATUS_RSVD	0x7f00	/* reserved */

/* AMD commands. */
#define	CFI_AMD_BLOCK_ERASE	0x30
#define	CFI_AMD_UNLOCK_ACK	0x55
#define	CFI_AMD_ERASE_SECTOR	0x80
#define	CFI_AMD_AUTO_SELECT	0x90
#define	CFI_AMD_PROGRAM		0xa0
#define	CFI_AMD_UNLOCK		0xaa

#define	AMD_ADDR_START		0xaaa
#define	AMD_ADDR_ACK		0x555

#define	CFI_AMD_MAXCHK		0x10000

#endif /* _DEV_CFI_REG_H_ */
