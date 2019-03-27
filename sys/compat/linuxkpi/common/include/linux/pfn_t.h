/*-
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $FreeBSD$
 */

#ifndef _LINUX_PFN_T_H_
#define	_LINUX_PFN_T_H_

#include <linux/mm.h>

CTASSERT(PAGE_SHIFT > 4);

#define	PFN_FLAGS_MASK (((u64)(PAGE_SIZE - 1)) << (64 - PAGE_SHIFT))
#define	PFN_SG_CHAIN (1ULL << (64 - 1))
#define	PFN_SG_LAST (1ULL << (64 - 2))
#define	PFN_DEV (1ULL << (64 - 3))
#define	PFN_MAP (1ULL << (64 - 4))

static inline pfn_t
__pfn_to_pfn_t(unsigned long pfn, u64 flags)
{
	pfn_t pfn_t = { pfn | (flags & PFN_FLAGS_MASK) };

	return (pfn_t);
}

static inline pfn_t
pfn_to_pfn_t(unsigned long pfn)
{
	return (__pfn_to_pfn_t (pfn, 0));
}

#endif					/* _LINUX_PFN_T_H_ */
