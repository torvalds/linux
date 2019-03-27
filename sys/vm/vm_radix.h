/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 EMC Corp.
 * Copyright (c) 2011 Jeffrey Roberson <jeff@freebsd.org>
 * Copyright (c) 2008 Mayur Shardul <mayur.shardul@gmail.com>
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

#ifndef _VM_RADIX_H_
#define _VM_RADIX_H_

#include <vm/_vm_radix.h>

#ifdef _KERNEL

int		vm_radix_insert(struct vm_radix *rtree, vm_page_t page);
void		vm_radix_wait(void);
boolean_t	vm_radix_is_singleton(struct vm_radix *rtree);
vm_page_t	vm_radix_lookup(struct vm_radix *rtree, vm_pindex_t index);
vm_page_t	vm_radix_lookup_ge(struct vm_radix *rtree, vm_pindex_t index);
vm_page_t	vm_radix_lookup_le(struct vm_radix *rtree, vm_pindex_t index);
void		vm_radix_reclaim_allnodes(struct vm_radix *rtree);
vm_page_t	vm_radix_remove(struct vm_radix *rtree, vm_pindex_t index);
vm_page_t	vm_radix_replace(struct vm_radix *rtree, vm_page_t newpage);
void		vm_radix_zinit(void);

static __inline void
vm_radix_init(struct vm_radix *rtree)
{

	rtree->rt_root = 0;
}

static __inline boolean_t
vm_radix_is_empty(struct vm_radix *rtree)
{

	return (rtree->rt_root == 0);
}

#endif /* _KERNEL */
#endif /* !_VM_RADIX_H_ */
