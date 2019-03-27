/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_DCR_H_
#define	_MACHINE_DCR_H_

/*
 * Definitions for the UltraSPARC-III Depatch Control Register (ASR 18).
 */
#define	DCR_MS			(1UL << 0)
#define	DCR_IFPOE		(1UL << 1)
#define	DCR_SI			(1UL << 3)
#define	DCR_RPE			(1UL << 4)
#define	DCR_BPE			(1UL << 5)

#define	DCR_OBSDATA_SHIFT	6
#define	DCR_OBSDATA_CT_BITS	8
#define	DCR_OBSDATA_CT_MASK						\
	(((1UL << DCR_OBSDATA_CT_BITS) - 1) << DCR_OBSDATA_SHIFT)

/* The following bits are valid for the UltraSPARC-III++/IV+ only. */
#define	DCR_IPE			(1UL << 2)

#define	DCR_OBSDATA_CTP_BITS	6
#define	DCR_OBSDATA_CTP_MASK						\
	(((1UL << DCR_OBSDATA_CTP_BITS) - 1) << DCR_OBSDATA_SHIFT)

#define	DCR_DPE			(1UL << 12)

/* The following bits are valid for the UltraSPARC-IV+ only. */
#define	DCR_BPM_SHIFT		13
#define	DCR_BPM_BITS		2
#define	DCR_BPM_MASK							\
	(((1UL << DCR_BPM_BITS) - 1) << DCR_BPM_SHIFT)
#define	DCR_BPM_1HIST_GSHARE	(0UL << DCR_BPM_SHIFT)
#define	DCR_BPM_2HIST_GSHARE	(1UL << DCR_BPM_SHIFT)
#define	DCR_BPM_PC		(2UL << DCR_BPM_SHIFT)
#define	DCR_BPM_2HIST_MIXED	(3UL << DCR_BPM_SHIFT)

#define	DCR_JPE			(1UL << 15)
#define	DCR_ITPE		(1UL << 16)
#define	DCR_DTPE		(1UL << 17)
#define	DCR_PPE			(1UL << 18)

#endif	/* _MACHINE_DCR_H_ */
