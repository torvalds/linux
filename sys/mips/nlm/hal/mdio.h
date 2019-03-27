/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __NLM_MDIO_H__
#define	__NLM_MDIO_H__

/**
* @file_name mdio.h
* @author Netlogic Microsystems
* @brief Access functions for XLP MDIO
*/
#define	INT_MDIO_CTRL				0x19
#define	INT_MDIO_CTRL_DATA			0x1A
#define	INT_MDIO_RD_STAT			0x1B
#define	INT_MDIO_LINK_STAT			0x1C
#define	EXT_G0_MDIO_CTRL			0x1D
#define	EXT_G1_MDIO_CTRL			0x21
#define	EXT_G0_MDIO_CTRL_DATA			0x1E
#define	EXT_G1_MDIO_CTRL_DATA			0x22
#define	EXT_G0_MDIO_LINK_STAT			0x20
#define	EXT_G1_MDIO_LINK_STAT			0x24
#define	EXT_G0_MDIO_RD_STAT			0x1F
#define	EXT_G1_MDIO_RD_STAT			0x23

#define	INT_MDIO_CTRL_ST_POS			0
#define	INT_MDIO_CTRL_OP_POS			2
#define	INT_MDIO_CTRL_PHYADDR_POS		4
#define	INT_MDIO_CTRL_DEVTYPE_POS		9
#define	INT_MDIO_CTRL_TA_POS			14
#define	INT_MDIO_CTRL_MIIM_POS			16
#define	INT_MDIO_CTRL_LOAD_POS			19
#define	INT_MDIO_CTRL_XDIV_POS			21
#define	INT_MDIO_CTRL_MCDIV_POS			28
#define	INT_MDIO_CTRL_RST			0x40000000
#define	INT_MDIO_CTRL_SMP			0x00100000
#define	INT_MDIO_CTRL_CMD_LOAD			0x00080000

#define	INT_MDIO_RD_STAT_MASK			0x0000FFFF
#define	INT_MDIO_STAT_LFV			0x00010000
#define	INT_MDIO_STAT_SC			0x00020000
#define	INT_MDIO_STAT_SM			0x00040000
#define	INT_MDIO_STAT_MIILFS			0x00080000
#define	INT_MDIO_STAT_MBSY			0x00100000

#define	EXT_G_MDIO_CLOCK_DIV_4			0
#define	EXT_G_MDIO_CLOCK_DIV_2			1
#define	EXT_G_MDIO_CLOCK_DIV_1			2
#define	EXT_G_MDIO_REGADDR_POS			5
#define	EXT_G_MDIO_PHYADDR_POS			10
#define	EXT_G_MDIO_CMD_SP			0x00008000
#define	EXT_G_MDIO_CMD_PSIA			0x00010000
#define	EXT_G_MDIO_CMD_LCD			0x00020000
#define	EXT_G_MDIO_CMD_RDS			0x00040000
#define	EXT_G_MDIO_CMD_SC			0x00080000
#define	EXT_G_MDIO_MMRST			0x00100000
#define	EXT_G_MDIO_DIV				0x0000001E
#define	EXT_G_MDIO_DIV_WITH_HW_DIV64		0x00000010

#define	EXT_G_MDIO_RD_STAT_MASK			0x0000FFFF
#define	EXT_G_MDIO_STAT_LFV			0x00010000
#define	EXT_G_MDIO_STAT_SC			0x00020000
#define	EXT_G_MDIO_STAT_SM			0x00040000
#define	EXT_G_MDIO_STAT_MIILFS			0x00080000
#define	EXT_G_MDIO_STAT_MBSY			0x80000000
#define	MDIO_OP_CMD_READ			0x10
#define	MDIO_OP_CMD_WRITE			0x01

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

int nlm_int_gmac_mdio_read(uint64_t, int, int, int, int, int);
int nlm_int_gmac_mdio_write(uint64_t, int, int, int, int, int, uint16_t);
int nlm_int_gmac_mdio_reset(uint64_t, int, int, int);
int nlm_gmac_mdio_read(uint64_t, int, int, int, int, int);
int nlm_gmac_mdio_write(uint64_t, int, int, int, int, int, uint16_t);
int nlm_gmac_mdio_reset(uint64_t, int, int, int);
void nlm_mdio_reset_all(uint64_t);

#endif /* !(LOCORE) && !(__ASSEMBLY__) */
#endif
