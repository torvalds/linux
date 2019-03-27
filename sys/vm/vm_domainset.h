/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,	Jeffrey Roberson <jeff@freebsd.org>
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
#ifndef __VM_DOMAINSET_H__
#define __VM_DOMAINSET_H__

struct vm_domainset_iter {
	struct domainset	*di_domain;
	unsigned int		*di_iter;
	vm_pindex_t		di_offset;
	int			di_flags;
	uint16_t		di_policy;
	domainid_t		di_n;
	bool			di_minskip;
};

int	vm_domainset_iter_page(struct vm_domainset_iter *, struct vm_object *,
	    int *);
void	vm_domainset_iter_page_init(struct vm_domainset_iter *,
	    struct vm_object *, vm_pindex_t, int *, int *);
int	vm_domainset_iter_policy(struct vm_domainset_iter *, int *);
void	vm_domainset_iter_policy_init(struct vm_domainset_iter *,
	    struct domainset *, int *, int *);
void	vm_domainset_iter_policy_ref_init(struct vm_domainset_iter *,
	    struct domainset_ref *, int *, int *);

void	vm_wait_doms(const domainset_t *);

#endif  /* __VM_DOMAINSET_H__ */
