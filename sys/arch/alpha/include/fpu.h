/*	$OpenBSD: fpu.h,v 1.2 2011/03/23 16:54:34 pirofti Exp $	*/
/*	$NetBSD: fpu.h,v 1.4 2001/04/26 03:10:46 ross Exp $	*/

/*-
 * Copyright (c) 2001 Ross Harvey
 * All rights reserved.
 *
 * This software was written for NetBSD.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_FPU_H_
#define _MACHINE_FPU_H_

#define	_FP_C_DEF(n) (1UL << (n))

/*
 * Most of these next definitions were moved from <ieeefp.h>. Apparently the
 * names happen to match those exported by Compaq and Linux from their fpu.h
 * files.
 */

#define	FPCR_SUM	_FP_C_DEF(63)
#define	FPCR_INED	_FP_C_DEF(62)
#define	FPCR_UNFD	_FP_C_DEF(61)
#define	FPCR_UNDZ	_FP_C_DEF(60)
#define	FPCR_DYN(rm)	((unsigned long)(rm) << 58)
#define	FPCR_IOV	_FP_C_DEF(57)
#define	FPCR_INE	_FP_C_DEF(56)
#define	FPCR_UNF	_FP_C_DEF(55)
#define	FPCR_OVF	_FP_C_DEF(54)
#define	FPCR_DZE	_FP_C_DEF(53)
#define	FPCR_INV	_FP_C_DEF(52)
#define	FPCR_OVFD	_FP_C_DEF(51)
#define	FPCR_DZED	_FP_C_DEF(50)
#define	FPCR_INVD	_FP_C_DEF(49)
#define	FPCR_DNZ	_FP_C_DEF(48)
#define	FPCR_DNOD	_FP_C_DEF(47)

#define	FPCR_MIRRORED (FPCR_INE | FPCR_UNF | FPCR_OVF | FPCR_DZE | FPCR_INV)
#define FPCR_MIR_START 52

/*
 * The AARM specifies the bit positions of the software word used for
 * user mode interface to the control and status of the kernel completion
 * routines. Although it largely just redefines the FPCR, it shuffles
 * the bit order. The names of the bits are defined in the AARM, and
 * the definition prefix can easily be determined from public domain
 * programs written to either the Compaq or Linux interfaces, which
 * appear to be identical.
 */

#define IEEE_STATUS_DNO _FP_C_DEF(22)
#define IEEE_STATUS_INE _FP_C_DEF(21)
#define IEEE_STATUS_UNF _FP_C_DEF(20)
#define IEEE_STATUS_OVF _FP_C_DEF(19)
#define IEEE_STATUS_DZE _FP_C_DEF(18)
#define IEEE_STATUS_INV _FP_C_DEF(17)

#define	IEEE_TRAP_ENABLE_DNO _FP_C_DEF(6)
#define	IEEE_TRAP_ENABLE_INE _FP_C_DEF(5)
#define	IEEE_TRAP_ENABLE_UNF _FP_C_DEF(4)
#define	IEEE_TRAP_ENABLE_OVF _FP_C_DEF(3)
#define	IEEE_TRAP_ENABLE_DZE _FP_C_DEF(2)
#define	IEEE_TRAP_ENABLE_INV _FP_C_DEF(1)

#define	IEEE_INHERIT _FP_C_DEF(14)
#define	IEEE_MAP_UMZ _FP_C_DEF(13)
#define	IEEE_MAP_DMZ _FP_C_DEF(12)

#define FP_C_MIRRORED (IEEE_STATUS_INE | IEEE_STATUS_UNF | IEEE_STATUS_OVF\
				| IEEE_STATUS_DZE | IEEE_STATUS_INV)
#define	FP_C_MIR_START 17

#ifdef _KERNEL

#define	FLD_MASK(len) ((1UL << (len)) - 1)
#define FLD_CLEAR(obj, origin, len)	\
		((obj) & ~(FLD_MASK(len) << (origin)))
#define	FLD_INSERT(obj, origin, len, value)	\
		(FLD_CLEAR(obj, origin, len) | (value) << origin)

#define	FP_C_TO_OPENBSD_MASK(fp_c) 	((fp_c) >> 1 & 0x3f)
#define	FP_C_TO_OPENBSD_FLAG(fp_c) 	((fp_c) >> 17 & 0x3f)
#define OPENBSD_MASK_TO_FP_C(m)		(((m) & 0x3f) << 1)
#define OPENBSD_FLAG_TO_FP_C(s)		(((s) & 0x3f) << 17)
#define	CLEAR_FP_C_MASK(fp_c)		((fp_c) & ~(0x3f << 1))
#define	CLEAR_FP_C_FLAG(fp_c)		((fp_c) & ~(0x3f << 17))
#define	SET_FP_C_MASK(fp_c, m) (CLEAR_FP_C_MASK(fp_c) | OPENBSD_MASK_TO_FP_C(m))
#define	SET_FP_C_FLAG(fp_c, m) (CLEAR_FP_C_FLAG(fp_c) | OPENBSD_FLAG_TO_FP_C(m))

#endif

#endif
