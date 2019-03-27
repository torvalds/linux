/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/alpha/include/ucontext.h,v 1.3 1999/10/08
 * $FreeBSD$
 */

#ifndef _MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

struct __mcontext {
	__uint64_t mc_global[8];
	__uint64_t mc_out[8];
	__uint64_t mc_local[8];
	__uint64_t mc_in[8];
	__uint32_t mc_fp[64];
} __aligned(64);

typedef struct __mcontext mcontext_t;

#define	_mc_flags	mc_global[0]
#define	_mc_sp		mc_out[6]
#define	_mc_fprs	mc_local[0]
#define	_mc_fsr		mc_local[1]
#define	_mc_gsr		mc_local[2]
#define	_mc_tnpc	mc_in[0]
#define	_mc_tpc		mc_in[1]
#define	_mc_tstate	mc_in[2]
#define	_mc_y		mc_in[4]
#define	_mc_wstate	mc_in[5]

#define	_MC_VERSION_SHIFT	0
#define	_MC_VERSION_BITS	32
#define	_MC_VERSION		1L

#define	_MC_FLAGS_SHIFT		32
#define	_MC_FLAGS_BITS		32
#define	_MC_VOLUNTARY		((1L << 0) << _MC_FLAGS_SHIFT)

#endif /* !_MACHINE_UCONTEXT_H_ */
