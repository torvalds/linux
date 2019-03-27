/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2017 Jung-uk Kim <jkim@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include "opt_bpf.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#else
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/types.h>
#endif

#include <net/bpf.h>
#include <net/bpf_jitter.h>

static u_int	bpf_jit_accept_all(u_char *, u_int, u_int);

#ifdef _KERNEL
MALLOC_DEFINE(M_BPFJIT, "BPF_JIT", "BPF JIT compiler");

SYSCTL_NODE(_net, OID_AUTO, bpf_jitter, CTLFLAG_RW, 0, "BPF JIT compiler");
int bpf_jitter_enable = 1;
SYSCTL_INT(_net_bpf_jitter, OID_AUTO, enable, CTLFLAG_RW,
    &bpf_jitter_enable, 0, "enable BPF JIT compiler");
#endif

bpf_jit_filter *
bpf_jitter(struct bpf_insn *fp, int nins)
{
	bpf_jit_filter *filter;

	/* Allocate the filter structure. */
#ifdef _KERNEL
	filter = (struct bpf_jit_filter *)malloc(sizeof(*filter),
	    M_BPFJIT, M_NOWAIT);
#else
	filter = (struct bpf_jit_filter *)malloc(sizeof(*filter));
#endif
	if (filter == NULL)
		return (NULL);

	/* No filter means accept all. */
	if (fp == NULL || nins == 0) {
		filter->func = bpf_jit_accept_all;
		return (filter);
	}

	/* Create the binary. */
	if ((filter->func = bpf_jit_compile(fp, nins, &filter->size)) == NULL) {
#ifdef _KERNEL
		free(filter, M_BPFJIT);
#else
		free(filter);
#endif
		return (NULL);
	}

	return (filter);
}

void
bpf_destroy_jit_filter(bpf_jit_filter *filter)
{

#ifdef _KERNEL
	if (filter->func != bpf_jit_accept_all)
		free(filter->func, M_BPFJIT);
	free(filter, M_BPFJIT);
#else
	if (filter->func != bpf_jit_accept_all)
		munmap(filter->func, filter->size);
	free(filter);
#endif
}

static u_int
bpf_jit_accept_all(__unused u_char *p, __unused u_int wirelen,
    __unused u_int buflen)
{

	return ((u_int)-1);
}
