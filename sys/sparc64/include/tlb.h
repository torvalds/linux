/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2008, 2010 Marius Strobl <marius@FreeBSD.org>
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

#ifndef	_MACHINE_TLB_H_
#define	_MACHINE_TLB_H_

#define	TLB_DIRECT_ADDRESS_BITS		(43)
#define	TLB_DIRECT_PAGE_BITS		(PAGE_SHIFT_4M)

#define	TLB_DIRECT_ADDRESS_MASK		((1UL << TLB_DIRECT_ADDRESS_BITS) - 1)
#define	TLB_DIRECT_PAGE_MASK		((1UL << TLB_DIRECT_PAGE_BITS) - 1)

#define	TLB_PHYS_TO_DIRECT(pa)						\
	((pa) | VM_MIN_DIRECT_ADDRESS)
#define	TLB_DIRECT_TO_PHYS(va)						\
	((va) & TLB_DIRECT_ADDRESS_MASK)
#define	TLB_DIRECT_TO_TTE_MASK						\
	(TD_V | TD_4M | (TLB_DIRECT_ADDRESS_MASK - TLB_DIRECT_PAGE_MASK))

#define	TLB_DAR_SLOT_SHIFT		(3)
#define	TLB_DAR_TLB_SHIFT		(16)
#define	TLB_DAR_SLOT(tlb, slot)						\
	((tlb) << TLB_DAR_TLB_SHIFT | (slot) << TLB_DAR_SLOT_SHIFT)
#define	TLB_DAR_T16			(0)	/* US-III{,i,+}, IV{,+} */
#define	TLB_DAR_T32			(0)	/* US-I, II{,e,i} */
#define	TLB_DAR_DT512_0			(2)	/* US-III{,i,+}, IV{,+} */
#define	TLB_DAR_DT512_1			(3)	/* US-III{,i,+}, IV{,+} */
#define	TLB_DAR_IT128			(2)	/* US-III{,i,+}, IV */
#define	TLB_DAR_IT512			(2)	/* US-IV+ */
#define	TLB_DAR_FTLB			(0)	/* SPARC64 V, VI, VII, VIIIfx */
#define	TLB_DAR_STLB			(2)	/* SPARC64 V, VI, VII, VIIIfx */

#define	TAR_VPN_SHIFT			(13)
#define	TAR_CTX_MASK			((1 << TAR_VPN_SHIFT) - 1)

#define	TLB_TAR_VA(va)			((va) & ~TAR_CTX_MASK)
#define	TLB_TAR_CTX(ctx)		((ctx) & TAR_CTX_MASK)

#define	TLB_CXR_CTX_BITS		(13)
#define	TLB_CXR_CTX_MASK						\
	(((1UL << TLB_CXR_CTX_BITS) - 1) << TLB_CXR_CTX_SHIFT)
#define	TLB_CXR_CTX_SHIFT		(0)
#define	TLB_CXR_PGSZ_BITS		(3)
#define	TLB_CXR_PGSZ_MASK		(~TLB_CXR_CTX_MASK)
#define	TLB_PCXR_N_IPGSZ0_SHIFT		(53)	/* SPARC64 VI, VII, VIIIfx */
#define	TLB_PCXR_N_IPGSZ1_SHIFT		(50)	/* SPARC64 VI, VII, VIIIfx */
#define	TLB_PCXR_N_PGSZ0_SHIFT		(61)
#define	TLB_PCXR_N_PGSZ1_SHIFT		(58)
#define	TLB_PCXR_N_PGSZ_I_SHIFT		(55)	/* US-IV+ */
#define	TLB_PCXR_P_IPGSZ0_SHIFT		(24)	/* SPARC64 VI, VII, VIIIfx */
#define	TLB_PCXR_P_IPGSZ1_SHIFT		(27)	/* SPARC64 VI, VII, VIIIfx */
#define	TLB_PCXR_P_PGSZ0_SHIFT		(16)
#define	TLB_PCXR_P_PGSZ1_SHIFT		(19)
/*
 * Note that the US-IV+ documentation appears to have TLB_PCXR_P_PGSZ_I_SHIFT
 * and TLB_PCXR_P_PGSZ0_SHIFT erroneously inverted.
 */
#define	TLB_PCXR_P_PGSZ_I_SHIFT		(22)	/* US-IV+ */
#define	TLB_SCXR_S_PGSZ1_SHIFT		(19)
#define	TLB_SCXR_S_PGSZ0_SHIFT		(16)

#define	TLB_TAE_PGSZ_BITS		(3)
#define	TLB_TAE_PGSZ0_MASK						\
	(((1UL << TLB_TAE_PGSZ_BITS) - 1) << TLB_TAE_PGSZ0_SHIFT)
#define	TLB_TAE_PGSZ1_MASK						\
	(((1UL << TLB_TAE_PGSZ_BITS) - 1) << TLB_TAE_PGSZ1_SHIFT)
#define	TLB_TAE_PGSZ0_SHIFT		(16)
#define	TLB_TAE_PGSZ1_SHIFT		(19)

#define	TLB_DEMAP_ID_SHIFT		(4)
#define	TLB_DEMAP_ID_PRIMARY		(0)
#define	TLB_DEMAP_ID_SECONDARY		(1)
#define	TLB_DEMAP_ID_NUCLEUS		(2)

#define	TLB_DEMAP_TYPE_SHIFT		(6)
#define	TLB_DEMAP_TYPE_PAGE		(0)
#define	TLB_DEMAP_TYPE_CONTEXT		(1)
#define	TLB_DEMAP_TYPE_ALL		(2)	/* US-III and beyond only */

#define	TLB_DEMAP_VA(va)		((va) & ~PAGE_MASK)
#define	TLB_DEMAP_ID(id)		((id) << TLB_DEMAP_ID_SHIFT)
#define	TLB_DEMAP_TYPE(type)		((type) << TLB_DEMAP_TYPE_SHIFT)

#define	TLB_DEMAP_PAGE			(TLB_DEMAP_TYPE(TLB_DEMAP_TYPE_PAGE))
#define	TLB_DEMAP_CONTEXT		(TLB_DEMAP_TYPE(TLB_DEMAP_TYPE_CONTEXT))
#define	TLB_DEMAP_ALL			(TLB_DEMAP_TYPE(TLB_DEMAP_TYPE_ALL))

#define	TLB_DEMAP_PRIMARY		(TLB_DEMAP_ID(TLB_DEMAP_ID_PRIMARY))
#define	TLB_DEMAP_SECONDARY		(TLB_DEMAP_ID(TLB_DEMAP_ID_SECONDARY))
#define	TLB_DEMAP_NUCLEUS		(TLB_DEMAP_ID(TLB_DEMAP_ID_NUCLEUS))

#define	TLB_CTX_KERNEL			(0)
#define	TLB_CTX_USER_MIN		(1)
#define	TLB_CTX_USER_MAX		(8192)

#define	MMU_SFSR_ASI_SHIFT		(16)
#define	MMU_SFSR_FT_SHIFT		(7)
#define	MMU_SFSR_E_SHIFT		(6)
#define	MMU_SFSR_CT_SHIFT		(4)
#define	MMU_SFSR_PR_SHIFT		(3)
#define	MMU_SFSR_W_SHIFT		(2)
#define	MMU_SFSR_OW_SHIFT		(1)
#define	MMU_SFSR_FV_SHIFT		(0)

#define	MMU_SFSR_ASI_SIZE		(8)
#define	MMU_SFSR_FT_SIZE		(6)
#define	MMU_SFSR_CT_SIZE		(2)

#define	MMU_SFSR_GET_ASI(sfsr)						\
	(((sfsr) >> MMU_SFSR_ASI_SHIFT) & ((1UL << MMU_SFSR_ASI_SIZE) - 1))
#define	MMU_SFSR_GET_FT(sfsr)						\
	(((sfsr) >> MMU_SFSR_FT_SHIFT) & ((1UL << MMU_SFSR_FT_SIZE) - 1))
#define	MMU_SFSR_GET_CT(sfsr)						\
	(((sfsr) >> MMU_SFSR_CT_SHIFT) & ((1UL << MMU_SFSR_CT_SIZE) - 1))

#define	MMU_SFSR_E			(1UL << MMU_SFSR_E_SHIFT)
#define	MMU_SFSR_PR			(1UL << MMU_SFSR_PR_SHIFT)
#define	MMU_SFSR_W			(1UL << MMU_SFSR_W_SHIFT)
#define	MMU_SFSR_OW			(1UL << MMU_SFSR_OW_SHIFT)
#define	MMU_SFSR_FV			(1UL << MMU_SFSR_FV_SHIFT)

typedef void tlb_flush_nonlocked_t(void);
typedef void tlb_flush_user_t(void);

struct pmap;
struct tlb_entry;

extern int dtlb_slots;
extern int itlb_slots;
extern int kernel_tlb_slots;
extern struct tlb_entry *kernel_tlbs;

void	tlb_context_demap(struct pmap *pm);
void	tlb_page_demap(struct pmap *pm, vm_offset_t va);
void	tlb_range_demap(struct pmap *pm, vm_offset_t start, vm_offset_t end);

tlb_flush_nonlocked_t cheetah_tlb_flush_nonlocked;
tlb_flush_user_t cheetah_tlb_flush_user;

tlb_flush_nonlocked_t spitfire_tlb_flush_nonlocked;
tlb_flush_user_t spitfire_tlb_flush_user;

tlb_flush_nonlocked_t zeus_tlb_flush_nonlocked;
tlb_flush_user_t zeus_tlb_flush_user;

extern tlb_flush_nonlocked_t *tlb_flush_nonlocked;
extern tlb_flush_user_t *tlb_flush_user;

#endif /* !_MACHINE_TLB_H_ */
