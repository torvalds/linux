/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: BSDI: pmap.v9.h,v 1.10.2.6 1999/08/23 22:18:44 cp Exp
 * $FreeBSD$
 */

#ifndef	_MACHINE_TSB_H_
#define	_MACHINE_TSB_H_

#define	TSB_PAGES_SHIFT			(4)
#define	TSB_PAGES			(1 << TSB_PAGES_SHIFT)
#define	TSB_BSHIFT			(TSB_PAGES_SHIFT + PAGE_SHIFT)
#define	TSB_BSIZE			(1 << TSB_BSHIFT)
#define	TSB_BUCKET_SHIFT		(2)
#define	TSB_BUCKET_SIZE			(1 << TSB_BUCKET_SHIFT)
#define	TSB_BUCKET_ADDRESS_BITS \
	(TSB_BSHIFT - TSB_BUCKET_SHIFT - TTE_SHIFT)
#define	TSB_BUCKET_MASK			((1 << TSB_BUCKET_ADDRESS_BITS) - 1)

#ifndef LOCORE

#define	TSB_SIZE			(TSB_BSIZE / sizeof(struct tte))

extern struct tte *tsb_kernel;
extern vm_size_t tsb_kernel_mask;
extern vm_size_t tsb_kernel_size;
extern vm_paddr_t tsb_kernel_phys;

static __inline struct tte *
tsb_vpntobucket(pmap_t pm, vm_offset_t vpn)
{

	return (&pm->pm_tsb[(vpn & TSB_BUCKET_MASK) << TSB_BUCKET_SHIFT]);
}

static __inline struct tte *
tsb_vtobucket(pmap_t pm, u_long sz, vm_offset_t va)
{

	return (tsb_vpntobucket(pm, va >> TTE_PAGE_SHIFT(sz)));
}

static __inline struct tte *
tsb_kvpntotte(vm_offset_t vpn)
{

	return (&tsb_kernel[vpn & tsb_kernel_mask]);
}

static __inline struct tte *
tsb_kvtotte(vm_offset_t va)
{

	return (tsb_kvpntotte(va >> PAGE_SHIFT));
}

typedef int (tsb_callback_t)(struct pmap *, struct pmap *, struct tte *,
	    vm_offset_t);

struct	tte *tsb_tte_lookup(pmap_t pm, vm_offset_t va);
void	tsb_tte_remove(struct tte *stp);
struct	tte *tsb_tte_enter(pmap_t pm, vm_page_t m, vm_offset_t va, u_long sz,
	    u_long data);
void	tsb_tte_local_remove(struct tte *tp);
void	tsb_foreach(pmap_t pm1, pmap_t pm2, vm_offset_t start, vm_offset_t end,
	    tsb_callback_t *callback);

#endif /* !LOCORE */

#endif /* !_MACHINE_TSB_H_ */
