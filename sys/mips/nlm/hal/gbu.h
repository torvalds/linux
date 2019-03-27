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
#ifndef _NLM_HAL_GBU_H__
#define	_NLM_HAL_GBU_H__

/* Global Bus Unit (GBU) for flash Specific registers */

#define	GBU_CS_BASEADDR(cs)	(0x0+cs)
#define	GBU_CS0_BASEADDR	0x0
#define	GBU_CS1_BASEADDR	0x1
#define	GBU_CS2_BASEADDR	0x2
#define	GBU_CS3_BASEADDR	0x3
#define	GBU_CS4_BASEADDR	0x4
#define	GBU_CS5_BASEADDR	0x5
#define	GBU_CS6_BASEADDR	0x6
#define	GBU_CS7_BASEADDR	0x7
#define	GBU_CS_BASELIMIT(cs)	(0x8+cs)
#define	GBU_CS0_BASELIMIT	0x8
#define	GBU_CS1_BASELIMIT	0x9
#define	GBU_CS2_BASELIMIT	0xa
#define	GBU_CS3_BASELIMIT	0xb
#define	GBU_CS4_BASELIMIT	0xc
#define	GBU_CS5_BASELIMIT	0xd
#define	GBU_CS6_BASELIMIT	0xe
#define	GBU_CS7_BASELIMIT	0xf
#define	GBU_CS_DEVPARAM(cs)	(0x10+cs)
#define	GBU_CS0_DEVPARAM	0x10
#define	GBU_CS1_DEVPARAM	0x11
#define	GBU_CS2_DEVPARAM	0x12
#define	GBU_CS3_DEVPARAM	0x13
#define	GBU_CS4_DEVPARAM	0x14
#define	GBU_CS5_DEVPARAM	0x15
#define	GBU_CS6_DEVPARAM	0x16
#define	GBU_CS7_DEVPARAM	0x17
#define	GBU_CS_DEVTIME0(cs)	(0x18+cs)
#define	GBU_CS0_DEVTIME0	0x18
#define	GBU_CS1_DEVTIME0	0x1a
#define	GBU_CS2_DEVTIME0	0x1c
#define	GBU_CS3_DEVTIME0	0x1e
#define	GBU_CS4_DEVTIME0	0x20
#define	GBU_CS5_DEVTIME0	0x22
#define	GBU_CS6_DEVTIME0	0x24
#define	GBU_CS7_DEVTIME0	0x26
#define	GBU_CS_DEVTIME1(cs)	(0x19+cs)
#define	GBU_CS0_DEVTIME1	0x19
#define	GBU_CS1_DEVTIME1	0x1b
#define	GBU_CS2_DEVTIME1	0x1d
#define	GBU_CS3_DEVTIME1	0x1f
#define	GBU_CS4_DEVTIME1	0x21
#define	GBU_CS5_DEVTIME1	0x23
#define	GBU_CS6_DEVTIME1	0x25
#define	GBU_CS7_DEVTIME1	0x27
#define	GBU_SYSCTRL		0x28
#define	GBU_BYTESWAP		0x29
#define	GBU_DI_TIMEOUT_VAL	0x2d
#define	GBU_INTSTAT		0x2e
#define	GBU_INTEN		0x2f
#define	GBU_STATUS		0x30
#define	GBU_ERRLOG0		0x2a
#define	GBU_ERRLOG1		0x2b
#define	GBU_ERRLOG2		0x2c

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define	nlm_read_gbu_reg(b, r)		nlm_read_reg(b, r)
#define	nlm_write_gbu_reg(b, r, v)	nlm_write_reg(b, r, v)
#define	nlm_get_gbu_pcibase(node)	\
				nlm_pcicfg_base(XLP_IO_NOR_OFFSET(node))
#define	nlm_get_gbu_regbase(node)	\
				(nlm_get_gbu_pcibase(node) + XLP_IO_PCI_HDRSZ)

#endif /* !LOCORE && !__ASSEMBLY__ */
#endif /* _NLM_HAL_GBU_H__ */
