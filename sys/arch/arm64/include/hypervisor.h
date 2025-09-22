/* $OpenBSD: hypervisor.h,v 1.5 2025/02/11 22:27:09 kettenis Exp $ */
/*-
 * Copyright (c) 2013, 2014 Andrew Turner
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
 * $FreeBSD: head/sys/arm64/include/hypervisor.h 281494 2015-04-13 14:43:10Z andrew $
 */

#ifndef _MACHINE_HYPERVISOR_H_
#define	_MACHINE_HYPERVISOR_H_

/*
 * These registers are only useful when in hypervisor context,
 * e.g. specific to EL2, or controlling the hypervisor.
 */

/*
 * Architecture feature trap register
 */
#define	CPTR_RES0	0x7fefc800
#define	CPTR_RES1	0x000032ff
#define	CPTR_TFP	0x00000400
#define	CPTR_TTA	0x00100000
#define	CPTR_TCPAC	0x80000000

/*
 * Hypervisor Config Register
 */

#define	HCR_VM		0x0000000000000001
#define	HCR_SWIO	0x0000000000000002
#define	HCR_PTW		0x0000000000000004
#define	HCR_FMO		0x0000000000000008
#define	HCR_IMO		0x0000000000000010
#define	HCR_AMO		0x0000000000000020
#define	HCR_VF		0x0000000000000040
#define	HCR_VI		0x0000000000000080
#define	HCR_VSE		0x0000000000000100
#define	HCR_FB		0x0000000000000200
#define	HCR_BSU_MASK	0x0000000000000c00
#define	HCR_DC		0x0000000000001000
#define	HCR_TWI		0x0000000000002000
#define	HCR_TWE		0x0000000000004000
#define	HCR_TID0	0x0000000000008000
#define	HCR_TID1	0x0000000000010000
#define	HCR_TID2	0x0000000000020000
#define	HCR_TID3	0x0000000000040000
#define	HCR_TSC		0x0000000000080000
#define	HCR_TIDCP	0x0000000000100000
#define	HCR_TACR	0x0000000000200000
#define	HCR_TSW		0x0000000000400000
#define	HCR_TPC		0x0000000000800000
#define	HCR_TPU		0x0000000001000000
#define	HCR_TTLB	0x0000000002000000
#define	HCR_TVM		0x0000000004000000
#define	HCR_TGE		0x0000000008000000
#define	HCR_TDZ		0x0000000010000000
#define	HCR_HCD		0x0000000020000000
#define	HCR_TRVM	0x0000000040000000
#define	HCR_RW		0x0000000080000000
#define	HCR_CD		0x0000000100000000
#define	HCR_ID		0x0000000200000000
#define	HCR_E2H		0x0000000400000000
#define	HCR_APK		0x0000010000000000
#define	HCR_API		0x0000020000000000

#endif

