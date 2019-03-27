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
#ifndef __NLM_ILAKEN_H__
#define	__NLM_ILAKEN_H__

/**
* @file_name interlaken.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP ILAKEN ports
*/

#define	ILK_TX_CONTROL(block)		NAE_REG(block, 5, 0x00)
#define	ILK_TX_RATE_LIMIT(block)	NAE_REG(block, 5, 0x01)
#define	ILK_TX_META_CTRL(block)		NAE_REG(block, 5, 0x02)
#define	ILK_RX_CTRL(block)		NAE_REG(block, 5, 0x03)
#define	ILK_RX_STATUS1(block)		NAE_REG(block, 5, 0x04)
#define	ILK_RX_STATUS2(block)		NAE_REG(block, 5, 0x05)
#define	ILK_GENERAL_CTRL1(block)	NAE_REG(block, 5, 0x06)
#define	ILK_STATUS3(block)		NAE_REG(block, 5, 0x07)
#define	ILK_RX_FC_TMAP0(block)		NAE_REG(block, 5, 0x08)
#define	ILK_RX_FC_TMAP1(block)		NAE_REG(block, 5, 0x09)
#define	ILK_RX_FC_TMAP2(block)		NAE_REG(block, 5, 0x0a)
#define	ILK_RX_FC_TMAP3(block)		NAE_REG(block, 5, 0x0b)
#define	ILK_RX_FC_TMAP4(block)		NAE_REG(block, 5, 0x0c)
#define	ILK_RX_FC_TADDR(block)		NAE_REG(block, 5, 0x0d)
#define	ILK_GENERAL_CTRL2(block)	NAE_REG(block, 5, 0x0e)
#define	ILK_GENERAL_CTRL3(block)	NAE_REG(block, 5, 0x0f)
#define	ILK_SMALL_COUNT0(block)		NAE_REG(block, 5, 0x10)
#define	ILK_SMALL_COUNT1(block)		NAE_REG(block, 5, 0x11)
#define	ILK_SMALL_COUNT2(block)		NAE_REG(block, 5, 0x12)
#define	ILK_SMALL_COUNT3(block)		NAE_REG(block, 5, 0x13)
#define	ILK_SMALL_COUNT4(block)		NAE_REG(block, 5, 0x14)
#define	ILK_SMALL_COUNT5(block)		NAE_REG(block, 5, 0x15)
#define	ILK_SMALL_COUNT6(block)		NAE_REG(block, 5, 0x16)
#define	ILK_SMALL_COUNT7(block)		NAE_REG(block, 5, 0x17)
#define	ILK_MID_COUNT0(block)		NAE_REG(block, 5, 0x18)
#define	ILK_MID_COUNT1(block)		NAE_REG(block, 5, 0x19)
#define	ILK_LARGE_COUNT0(block)		NAE_REG(block, 5, 0x1a)
#define	ILK_LARGE_COUNT1(block)		NAE_REG(block, 5, 0x1b)
#define	ILK_LARGE_COUNT_H0(block)	NAE_REG(block, 5, 0x1c)
#define	ILK_LARGE_COUNT_H1(block)	NAE_REG(block, 5, 0x1d)

#endif
