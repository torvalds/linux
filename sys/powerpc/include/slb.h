/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009 Nathan Whitehorn
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
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_SLB_H_
#define	_MACHINE_SLB_H_

/*
 * Bit definitions for segment lookaside buffer entries.
 *
 * PowerPC Microprocessor Family: The Programming Environments for 64-bit
 * Microprocessors, section 7.4.2.1
 *
 * Note that these bitmasks are relative to the values for one of the two
 * values for slbmte, slbmfee, and slbmfev, not the internal SLB
 * representation.
 */

#define	SLBV_KS		0x0000000000000800UL /* Supervisor-state prot key */
#define	SLBV_KP		0x0000000000000400UL /* User-state prot key */
#define	SLBV_N		0x0000000000000200UL /* No-execute protection */
#define	SLBV_L		0x0000000000000100UL /* Large page selector */
#define	SLBV_CLASS	0x0000000000000080UL /* Class selector */
#define	SLBV_VSID_MASK	0xfffffffffffff000UL /* Virtual segment ID mask */
#define	SLBV_VSID_SHIFT	12

/*
 * Make a predictable 1:1 map from ESIDs to VSIDs for the kernel. Hash table
 * coverage is increased by swizzling the ESID and multiplying by a prime
 * number (0x13bb).
 */
#define	KERNEL_VSID_BIT	0x0000001000000000UL /* Bit set in all kernel VSIDs */
#define KERNEL_VSID(esid) ((((((uint64_t)esid << 8) | ((uint64_t)esid >> 28)) \
				* 0x13bbUL) & (KERNEL_VSID_BIT - 1)) | \
				KERNEL_VSID_BIT)

#define	SLBE_VALID	0x0000000008000000UL /* SLB entry valid */
#define	SLBE_INDEX_MASK	0x0000000000000fffUL /* SLB index mask*/
#define	SLBE_ESID_MASK	0xfffffffff0000000UL /* Effective segment ID mask */
#define	SLBE_ESID_SHIFT	28

/* Virtual real-mode VSID in LPARs */
#define VSID_VRMA	0x1ffffff

/*
 * User segment for copyin/out
 */
#define USER_SLB_SLOT 0
#define USER_SLB_SLBE (((USER_ADDR >> ADDR_SR_SHFT) << SLBE_ESID_SHIFT) | \
			SLBE_VALID | USER_SLB_SLOT)

struct slb {
	uint64_t	slbv;
	uint64_t	slbe;
};

#endif /* !_MACHINE_SLB_H_ */
