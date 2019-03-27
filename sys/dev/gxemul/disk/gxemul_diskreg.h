/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Juli Mallett <jmallett@FreeBSD.org>
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

#ifndef	_DEV_GXEMUL_DISK_GXEMUL_DISK_H_
#define	_DEV_GXEMUL_DISK_GXEMUL_DISK_H_

#define	GXEMUL_DISK_DEV_BASE		(0x13000000)

#define	GXEMUL_DISK_DEV_BLOCKSIZE	(0x0200)

#define	GXEMUL_DISK_DEV_ID_START	(0x0000)
#define	GXEMUL_DISK_DEV_ID_END		(0x0100)

#ifdef _LP64
#define GXEMUL_DISK_DEV_OFFSET		(0x0000)
#else
#define GXEMUL_DISK_DEV_OFFSET_LO       (0x0000)
#define GXEMUL_DISK_DEV_OFFSET_HI       (0x0008)
#endif
#define	GXEMUL_DISK_DEV_DISKID		(0x0010)
#define	GXEMUL_DISK_DEV_START		(0x0020)
#define	GXEMUL_DISK_DEV_STATUS		(0x0030)
#define	GXEMUL_DISK_DEV_BLOCK		(0x4000)

#ifdef _LP64
#define	GXEMUL_DISK_DEV_FUNCTION(f)					\
	(volatile uint64_t *)MIPS_PHYS_TO_DIRECT_UNCACHED(GXEMUL_DISK_DEV_BASE + (f))
#define	GXEMUL_DISK_DEV_READ(f)						\
	(volatile uint64_t)*GXEMUL_DISK_DEV_FUNCTION(f)
#else
#define	GXEMUL_DISK_DEV_FUNCTION(f)					\
	(volatile uint32_t *)MIPS_PHYS_TO_DIRECT_UNCACHED(GXEMUL_DISK_DEV_BASE + (f))
#define	GXEMUL_DISK_DEV_READ(f)						\
	(volatile uint32_t)*GXEMUL_DISK_DEV_FUNCTION(f)
#endif
#define	GXEMUL_DISK_DEV_WRITE(f, v)					\
	*GXEMUL_DISK_DEV_FUNCTION(f) = (v)

#define	GXEMUL_DISK_DEV_START_READ	(0x00)
#define	GXEMUL_DISK_DEV_START_WRITE	(0x01)

#define	GXEMUL_DISK_DEV_STATUS_FAILURE	(0x00)

#endif /* !_DEV_GXEMUL_DISK_GXEMUL_DISK_H_ */
