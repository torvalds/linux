/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_PAGE_H_
#define _LINUX_PAGE_H_

#include <linux/types.h>

#include <sys/param.h>
#include <sys/vmmeter.h>

#include <machine/atomic.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

typedef unsigned long linux_pte_t;
typedef unsigned long linux_pmd_t;
typedef unsigned long linux_pgd_t;
typedef unsigned long pgprot_t;

#define page	vm_page

#define	LINUXKPI_PROT_VALID (1 << 3)
#define	LINUXKPI_CACHE_MODE_SHIFT 4

CTASSERT((VM_PROT_ALL & -LINUXKPI_PROT_VALID) == 0);

static inline pgprot_t
cachemode2protval(vm_memattr_t attr)
{
	return ((attr << LINUXKPI_CACHE_MODE_SHIFT) | LINUXKPI_PROT_VALID);
}

static inline vm_memattr_t
pgprot2cachemode(pgprot_t prot)
{
	if (prot & LINUXKPI_PROT_VALID)
		return (prot >> LINUXKPI_CACHE_MODE_SHIFT);
	else
		return (VM_MEMATTR_DEFAULT);
}

#define	virt_to_page(x)		PHYS_TO_VM_PAGE(vtophys(x))
#define	page_to_pfn(pp)		(VM_PAGE_TO_PHYS(pp) >> PAGE_SHIFT)
#define	pfn_to_page(pfn)	(PHYS_TO_VM_PAGE((pfn) << PAGE_SHIFT))
#define	nth_page(page,n)	pfn_to_page(page_to_pfn(page) + (n))

#define	clear_page(page)		memset(page, 0, PAGE_SIZE)
#define	pgprot_noncached(prot)		\
	(((prot) & VM_PROT_ALL) | cachemode2protval(VM_MEMATTR_UNCACHEABLE))
#define	pgprot_writecombine(prot)	\
	(((prot) & VM_PROT_ALL) | cachemode2protval(VM_MEMATTR_WRITE_COMBINING))

#undef	PAGE_MASK
#define	PAGE_MASK	(~(PAGE_SIZE-1))
/*
 * Modifying PAGE_MASK in the above way breaks trunc_page, round_page,
 * and btoc macros. Therefore, redefine them in a way that makes sense
 * so the LinuxKPI consumers don't get totally broken behavior.
 */
#undef	btoc
#define	btoc(x)	(((vm_offset_t)(x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#undef	round_page
#define	round_page(x)	((((uintptr_t)(x)) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#undef	trunc_page
#define	trunc_page(x)	((uintptr_t)(x) & ~(PAGE_SIZE - 1))

#endif	/* _LINUX_PAGE_H_ */
