/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NET_BPF_JITTER_H_
#define _NET_BPF_JITTER_H_

#ifdef _KERNEL
MALLOC_DECLARE(M_BPFJIT);
#endif

extern int bpf_jitter_enable;

/*
 * Prototype of a filtering function created by the jitter.
 *
 * The syntax and the meaning of the parameters is analogous to the one of
 * bpf_filter(). Notice that the filter is not among the parameters because
 * it is hardwired in the function.
 */
typedef u_int (*bpf_filter_func)(u_char *, u_int, u_int);

/* Structure describing a native filtering program created by the jitter. */
typedef struct bpf_jit_filter {
	/* The native filtering binary, in the form of a bpf_filter_func. */
	bpf_filter_func	func;
	size_t		size;
} bpf_jit_filter;

/*
 * BPF jitter, builds a machine function from a BPF program.
 *
 * param fp	The BPF pseudo-assembly filter that will be translated
 *		into native code.
 * param nins	Number of instructions of the input filter.
 * return	The bpf_jit_filter structure containing the native filtering
 *		binary.
 *
 * bpf_jitter allocates the buffers for the new native filter and
 * then translates the program pointed by fp calling bpf_jit_compile().
 */
bpf_jit_filter	*bpf_jitter(struct bpf_insn *fp, int nins);

/*
 * Deletes a filtering function that was previously created by bpf_jitter().
 *
 * param filter	The filter to destroy.
 *
 * This function frees the variuos buffers (code, memory, etc.) associated
 * with a filtering function.
 */
void		bpf_destroy_jit_filter(bpf_jit_filter *filter);

/*
 * Declarations for machine-dependent functions.
 */
struct bpf_insn;

bpf_filter_func	bpf_jit_compile(struct bpf_insn *, u_int, size_t *);

#endif	/* _NET_BPF_JITTER_H_ */
