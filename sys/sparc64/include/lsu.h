/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_LSU_H_
#define	_MACHINE_LSU_H_

/*
 * Definitions for the Load-Store-Unit Control Register. This is called
 * Data Cache Unit Control Register (DCUCR) for UltraSPARC-III and greater.
 */
#define	LSU_IC		(1UL << 0)
#define	LSU_DC		(1UL << 1)
#define	LSU_IM		(1UL << 2)
#define	LSU_DM		(1UL << 3)

/* Parity control mask, UltraSPARC-I and II series only. */
#define	LSU_FM_SHIFT	4
#define	LSU_FM_BITS	16
#define	LSU_FM_MASK	(((1UL << LSU_FM_BITS) - 1) << LSU_FM_SHIFT)

#define	LSU_VM_SHIFT	25
#define	LSU_VM_BITS	8
#define	LSU_VM_MASK	(((1UL << LSU_VM_BITS) - 1) << LSU_VM_SHIFT)

#define	LSU_PM_SHIFT	33
#define	LSU_PM_BITS	8
#define	LSU_PM_MASK	(((1UL << LSU_PM_BITS) - 1) << LSU_PM_SHIFT)

#define	LSU_VW		(1UL << 21)
#define	LSU_VR		(1UL << 22)
#define	LSU_PW		(1UL << 23)
#define	LSU_PR		(1UL << 24)

/* The following bits are valid for the UltraSPARC-III series only. */
#define	LSU_WE		(1UL << 41)
#define	LSU_SL		(1UL << 42)
#define	LSU_SPE		(1UL << 43)
#define	LSU_HPE		(1UL << 44)
#define	LSU_PE		(1UL << 45)
#define	LSU_RE		(1UL << 46)
#define	LSU_ME		(1UL << 47)
#define	LSU_CV		(1UL << 48)
#define	LSU_CP		(1UL << 49)

/* The following bit is valid for the UltraSPARC-IV only. */
#define	LSU_WIH		(1UL << 4)

/* The following bits are valid for the UltraSPARC-IV+ only. */
#define	LSU_PPS_SHIFT	50
#define	LSU_PPS_BITS	2
#define	LSU_PPS_MASK	(((1UL << LSU_PPS_BITS) - 1) << LSU_PPS_SHIFT)

#define	LSU_IPS_SHIFT	52
#define	LSU_IPS_BITS	2
#define	LSU_IPS_MASK	(((1UL << LSU_IPS_BITS) - 1) << LSU_IPS_SHIFT)

#define	LSU_PCM		(1UL << 54)
#define	LSU_WCE		(1UL << 55)

/* The following bit is valid for the SPARC64 V, VI, VII and VIIIfx only. */
#define	LSU_WEAK_SPCA	(1UL << 41)

#endif	/* _MACHINE_LSU_H_ */
