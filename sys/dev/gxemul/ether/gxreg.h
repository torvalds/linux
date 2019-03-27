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

#ifndef	_DEV_GXEMUL_ETHER_GXREG_H_
#define	_DEV_GXEMUL_ETHER_GXREG_H_

#define	GXEMUL_ETHER_DEV_BASE		(0x14000000)
#define	GXEMUL_ETHER_DEV_IRQ		(3)

#define	GXEMUL_ETHER_DEV_MTU		(0x4000)

#define	GXEMUL_ETHER_DEV_BUFFER		(0x0000)
#define	GXEMUL_ETHER_DEV_STATUS		(0x4000)
#define	GXEMUL_ETHER_DEV_LENGTH		(0x4010)
#define	GXEMUL_ETHER_DEV_COMMAND	(0x4020)
#define	GXEMUL_ETHER_DEV_MAC		(0x4040)

#ifdef _LP64
#define	GXEMUL_ETHER_DEV_FUNCTION(f)					\
	(volatile uint64_t *)MIPS_PHYS_TO_DIRECT_UNCACHED(GXEMUL_ETHER_DEV_BASE + (f))
#define	GXEMUL_ETHER_DEV_READ(f)					\
	(volatile uint64_t)*GXEMUL_ETHER_DEV_FUNCTION(f)
#else
#define	GXEMUL_ETHER_DEV_FUNCTION(f)					\
	(volatile uint32_t *)MIPS_PHYS_TO_DIRECT_UNCACHED(GXEMUL_ETHER_DEV_BASE + (f))
#define	GXEMUL_ETHER_DEV_READ(f)					\
	(volatile uint32_t)*GXEMUL_ETHER_DEV_FUNCTION(f)
#endif
#define	GXEMUL_ETHER_DEV_WRITE(f, v)					\
	*GXEMUL_ETHER_DEV_FUNCTION(f) = (v)

#define	GXEMUL_ETHER_DEV_STATUS_RX_OK	(0x01)
#define	GXEMUL_ETHER_DEV_STATUS_RX_MORE	(0x02)

#define	GXEMUL_ETHER_DEV_COMMAND_RX	(0x00)
#define	GXEMUL_ETHER_DEV_COMMAND_TX	(0x01)

#endif /* !_DEV_GXEMUL_ETHER_GXREG_H_ */
