/*	$OpenBSD: pte.h,v 1.17 2024/04/14 19:08:09 miod Exp $	*/
/*	$NetBSD: pte.h,v 1.7 2001/07/31 06:55:46 eeh Exp $ */

/*
 * Copyright (c) 1996-1999 Eduardo Horvath
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef	_MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

/* virtual address to virtual page number */
#define	VA_SUN4U_VPG(va)	(((int)(va) >> 13) & 31)

/* virtual address to offset within page */
#define VA_SUN4U_OFF(va)       	(((int)(va)) & 0x1FFF)

/* When we go to 64-bit VAs we need to handle the hole */
#define VA_VPG(va)	VA_SUN4U_VPG(va)
#define VA_OFF(va)	VA_SUN4U_OFF(va)

#define PG_SHIFT4U	13
#define MMU_PAGE_ALIGN	8192

/* If you know where a tte is in the tsb, how do you find its va? */	
#define TSBVA(i)	((tsb[(i)].tag.f.tag_va<<22)|(((i)<<13)&0x3ff000))

#ifndef _LOCORE
/* 
 *  This is the spitfire TTE.
 */
#if 0 /* We don't use bitfields anyway. */
struct sun4u_tag_fields {
	u_int64_t	tag_g:1,	/* global flag */
		tag_ctxt:15,	/* context for mapping */
		tag_unassigned:6,
		tag_va:42;	/* virtual address bits<64:22> */
};
union sun4u_tag { struct sun4u_tag_fields f; int64_t tag; };
struct sun4u_data_fields {
	u_int64_t	data_v:1,	/* valid bit */
		data_size:2,	/* page size [8K*8**<SIZE>] */
		data_nfo:1,	/* no-fault only */
		data_ie:1,	/* invert endianness [inefficient] */
		data_soft2:2,	/* reserved for S/W */
		data_pa:36,	/* physical address */
		data_accessed:1,/* S/W accessed bit */
		data_modified:1,/* S/W modified bit */
		data_realw:1,	/* S/W real writable bit (to manage modified) */
		data_tsblock:1,	/* S/W TSB locked entry */
		data_exec:1,	/* S/W Executable */
		data_onlyexec:1,/* S/W Executable only */
		data_lock:1,	/* lock into TLB */
		data_cacheable:2,	/* cacheability control */
		data_e:1,	/* explicit accesses only */
		data_priv:1,	/* privileged page */
		data_w:1,	/* writeable */
		data_g:1;	/* same as tag_g */
};
union sun4u_data { struct sun4u_data_fields f; int64_t data; };
struct sun4u_tte {
	union sun4u_tag tag;
	union sun4u_data data;
};
#else
struct sun4u_tte {
	int64_t tag;
	int64_t data;
};
#endif
typedef struct sun4u_tte pte_t;

/* Assembly routine to flush a mapping */
extern void (*sp_tlb_flush_pte)(vaddr_t, uint64_t);
extern void (*sp_tlb_flush_ctx)(uint64_t);

#if defined(MULTIPROCESSOR)
void smp_tlb_flush_pte(vaddr_t, uint64_t);
void smp_tlb_flush_ctx(uint64_t);
#define tlb_flush_pte(va,ctx)	smp_tlb_flush_pte(va, ctx)
#define tlb_flush_ctx(ctx)	smp_tlb_flush_ctx(ctx)
#else
#define tlb_flush_pte(va,ctx)	(*sp_tlb_flush_pte)(va, ctx)
#define tlb_flush_ctx(ctx)	(*sp_tlb_flush_ctx)(ctx)
#endif

#endif /* _LOCORE */

/* TSB tag masks */
#define CTX_MASK		((1<<13)-1)
#define TSB_TAG_CTX_SHIFT	48
#define TSB_TAG_VA_SHIFT	22

#define TSB_TAG_LOCKED		0x0000040000000000LL

#define TSB_TAG_G		0x8000000000000000LL
#define TSB_TAG_CTX(t)		((((int64_t)(t))>>TSB_TAG_CTX_SHIFT)&CTX_MASK)
#define TSB_TAG_VA(t)		((((int64_t)(t))<<TSB_TAG_VA_SHIFT))
#define TSB_TAG(g,ctx,va)	((((u_int64_t)((g)!=0))<<63)|(((u_int64_t)(ctx)&CTX_MASK)<<TSB_TAG_CTX_SHIFT)|(((u_int64_t)va)>>TSB_TAG_VA_SHIFT))

/* Page sizes */
#define	PGSZ_8K			0
#define	PGSZ_64K		1
#define	PGSZ_512K		2
#define	PGSZ_4M			3

#define	SUN4U_PGSZ_SHIFT	61
#define	SUN4U_TLB_SZ(s)		(((uint64_t)(s)) << SUN4U_PGSZ_SHIFT)

/* TLB data masks */
#define SUN4U_TLB_V		0x8000000000000000LL
#define SUN4U_TLB_8K		SUN4U_TLB_SZ(PGSZ_8K)
#define SUN4U_TLB_64K		SUN4U_TLB_SZ(PGSZ_64K)
#define SUN4U_TLB_512K		SUN4U_TLB_SZ(PGSZ_512K)
#define SUN4U_TLB_4M		SUN4U_TLB_SZ(PGSZ_4M)
#define SUN4U_TLB_SZ_MASK	0x6000000000000000LL
#define SUN4U_TLB_NFO		0x1000000000000000LL
#define SUN4U_TLB_IE		0x0800000000000000LL
#define SUN4U_TLB_SOFT2_MASK	0x07fc000000000000LL
#define SUN4U_TLB_RESERVED_MASK	0x0003800000000000LL
#define SUN4U_TLB_PA_MASK	0x00007fffffffe000LL
#define SUN4U_TLB_SOFT_MASK	0x0000000000001f80LL
/* S/W bits */
#define SUN4U_TLB_ACCESS	0x0000000000000200LL
#define SUN4U_TLB_MODIFY	0x0000000000000800LL
#define SUN4U_TLB_REAL_W	0x0000000000000400LL
#define SUN4U_TLB_TSB_LOCK	0x0000000000001000LL
#define SUN4U_TLB_EXEC		0x0000000000000100LL
#define SUN4U_TLB_EXEC_ONLY	0x0000000000000080LL
/* H/W bits */
#define SUN4U_TLB_L		0x0000000000000040LL
#define SUN4U_TLB_CACHE_MASK	0x0000000000000030LL
#define SUN4U_TLB_CP		0x0000000000000020LL
#define SUN4U_TLB_CV		0x0000000000000010LL
#define SUN4U_TLB_E		0x0000000000000008LL
#define SUN4U_TLB_P		0x0000000000000004LL
#define SUN4U_TLB_W		0x0000000000000002LL
#define SUN4U_TLB_G		0x0000000000000001LL

#define SUN4U_TSB_DATA(g,sz,pa,priv,write,cache,aliased,valid,ie) \
(((valid)?SUN4U_TLB_V:0LL)|SUN4U_TLB_SZ(sz)|\
(((u_int64_t)(pa))&SUN4U_TLB_PA_MASK)|\
((cache)?((aliased)?SUN4U_TLB_CP:SUN4U_TLB_CACHE_MASK):SUN4U_TLB_E)|\
((priv)?SUN4U_TLB_P:0LL)|((write)?SUN4U_TLB_W:0LL)|((g)?SUN4U_TLB_G:0LL)|\
((ie)?SUN4U_TLB_IE:0LL))

#define SUN4V_PGSZ_SHIFT	0
#define	SUN4V_TLB_SZ(s)		(((uint64_t)(s))<<SUN4V_PGSZ_SHIFT)

/* TLB data masks */
#define SUN4V_TLB_V		0x8000000000000000LL
#define SUN4V_TLB_8K		SUN4V_TLB_SZ(PGSZ_8K)
#define SUN4V_TLB_64K		SUN4V_TLB_SZ(PGSZ_64K)
#define SUN4V_TLB_512K		SUN4V_TLB_SZ(PGSZ_512K)
#define SUN4V_TLB_4M		SUN4V_TLB_SZ(PGSZ_4M)
#define SUN4V_TLB_SZ_MASK	0x000000000000000fLL
#define SUN4V_TLB_NFO		0x4000000000000000LL
#define SUN4V_TLB_IE		0x0000000000001000LL
#define SUN4V_TLB_SOFT2_MASK	0x3f00000000000000LL
#define SUN4V_TLB_PA_MASK	0x00ffffffffffe000LL
#define SUN4V_TLB_SOFT_MASK	0x0000000000000030LL
/* S/W bits */
#define SUN4V_TLB_ACCESS	0x0000000000000010LL
#define SUN4V_TLB_MODIFY	0x0000000000000020LL
#define SUN4V_TLB_REAL_W	0x2000000000000000LL
#define SUN4V_TLB_TSB_LOCK	0x1000000000000000LL
#define SUN4V_TLB_EXEC		SUN4V_TLB_X
#define SUN4V_TLB_EXEC_ONLY	0x0200000000000000LL
/* H/W bits */
#define SUN4V_TLB_CACHE_MASK	0x0000000000000600LL
#define SUN4V_TLB_CP		0x0000000000000400LL
#define SUN4V_TLB_CV		0x0000000000000200LL
#define SUN4V_TLB_E		0x0000000000000800LL
#define SUN4V_TLB_P		0x0000000000000100LL
#define SUN4V_TLB_X		0x0000000000000080LL
#define SUN4V_TLB_W		0x0000000000000040LL
#define SUN4V_TLB_G		0x0000000000000000LL

#define SUN4V_TSB_DATA(g,sz,pa,priv,write,cache,aliased,valid,ie) \
(((valid)?SUN4V_TLB_V:0LL)|SUN4V_TLB_SZ(sz)|\
(((u_int64_t)(pa))&SUN4V_TLB_PA_MASK)|\
((cache)?((aliased)?SUN4V_TLB_CP:SUN4V_TLB_CACHE_MASK):SUN4V_TLB_E)|\
((priv)?SUN4V_TLB_P:0LL)|((write)?SUN4V_TLB_W:0LL)|((g)?SUN4V_TLB_G:0LL)|\
((ie)?SUN4V_TLB_IE:0LL))

#endif /* _MACHINE_PTE_H_ */
