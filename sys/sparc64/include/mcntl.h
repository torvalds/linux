/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Marius Strobl <marius@FreeBSD.org>
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

#ifndef	_MACHINE_MCNTL_H
#define	_MACHINE_MCNTL_H

/*
 * Definitions for the SPARC64 V, VI, VII and VIIIfx Memory Control Register
 */
#define	MCNTL_JPS1_TSBP		(1UL << 8)

#define	MCNTL_RMD_SHIFT		12
#define	MCNTL_RMD_BITS		2
#define	MCNTL_RMD_MASK							\
	(((1UL << MCNTL_RMD_BITS) - 1) << MCNTL_RMD_SHIFT)
#define	MCNTL_RMD_FULL		(0UL << MCNTL_RMD_SHIFT)
#define	MCNTL_RMD_1024		(2UL << MCNTL_RMD_SHIFT)
#define	MCNTL_RMD_512		(3UL << MCNTL_RMD_SHIFT)

#define	MCNTL_FW_FDTLB		(1UL << 14)
#define	MCNTL_FW_FITLB		(1UL << 15)
#define	MCNTL_NC_CACHE		(1UL << 16)

/* The following bits are valid for the SPARC64 VI, VII and VIIIfx only. */
#define	MCNTL_MPG_SDTLB		(1UL << 6)
#define	MCNTL_MPG_SITLB		(1UL << 7)

/* The following bits are valid for the SPARC64 VIIIfx only. */
#define	MCNTL_HPF_SHIFT		18
#define	MCNTL_HPF_BITS		2
#define	MCNTL_HPF_MASK							\
	(((1UL << MCNTL_HPF_BITS) - 1) << MCNTL_HPF_SHIFT)
#define	MCNTL_HPF_STRONG	(0UL << MCNTL_HPF_SHIFT)
#define	MCNTL_HPF_NOT		(1UL << MCNTL_HPF_SHIFT)
#define	MCNTL_HPF_WEAK		(2UL << MCNTL_HPF_SHIFT)

#endif	/* _MACHINE_MCNTL_H */
