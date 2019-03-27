/*
 * Copyright (c) 2017-2018 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_CHAIN_H__
#define __ECORE_CHAIN_H__

#include "common_hsi.h"
#include "ecore_utils.h"

enum ecore_chain_mode
{
	/* Each Page contains a next pointer at its end */
	ECORE_CHAIN_MODE_NEXT_PTR,

	/* Chain is a single page (next ptr) is unrequired */
	ECORE_CHAIN_MODE_SINGLE,

	/* Page pointers are located in a side list */
	ECORE_CHAIN_MODE_PBL,
};

enum ecore_chain_use_mode
{
	ECORE_CHAIN_USE_TO_PRODUCE,		/* Chain starts empty */
	ECORE_CHAIN_USE_TO_CONSUME,		/* Chain starts full */
	ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,	/* Chain starts empty */
};

enum ecore_chain_cnt_type {
	/* The chain's size/prod/cons are kept in 16-bit variables */
	ECORE_CHAIN_CNT_TYPE_U16,

	/* The chain's size/prod/cons are kept in 32-bit variables  */
	ECORE_CHAIN_CNT_TYPE_U32,
};

struct ecore_chain_next
{
	struct regpair	next_phys;
	void		*next_virt;
};

struct ecore_chain_pbl_u16 {
	u16	prod_page_idx;
	u16	cons_page_idx;
};

struct ecore_chain_pbl_u32 {
	u32	prod_page_idx;
	u32	cons_page_idx;
};

struct ecore_chain_ext_pbl
{
	dma_addr_t	p_pbl_phys;
	void		*p_pbl_virt;
};

struct ecore_chain_u16 {
	/* Cyclic index of next element to produce/consme */
	u16	prod_idx;
	u16	cons_idx;
};

struct ecore_chain_u32 {
	/* Cyclic index of next element to produce/consme */
	u32	prod_idx;
	u32	cons_idx;
};

struct ecore_chain
{
	/* fastpath portion of the chain - required for commands such
	 * as produce / consume.
	 */
	/* Point to next element to produce/consume */
	void				*p_prod_elem;
	void				*p_cons_elem;

	/* Fastpath portions of the PBL [if exists] */

	struct {
		/* Table for keeping the virtual addresses of the chain pages,
		 * respectively to the physical addresses in the pbl table.
		 */
		void		**pp_virt_addr_tbl;

		union {
			struct ecore_chain_pbl_u16	pbl_u16;
			struct ecore_chain_pbl_u32	pbl_u32;
		} c;
	} pbl;

	union {
		struct ecore_chain_u16	chain16;
		struct ecore_chain_u32	chain32;
	} u;

	/* Capacity counts only usable elements */
	u32				capacity;
	u32				page_cnt;

	/* A u8 would suffice for mode, but it would save as a lot of headaches
	 * on castings & defaults.
	 */
	enum ecore_chain_mode		mode;

	/* Elements information for fast calculations */
	u16				elem_per_page;
	u16				elem_per_page_mask;
	u16				elem_size;
	u16				next_page_mask;
	u16				usable_per_page;
	u8				elem_unusable;

	u8				cnt_type;

	/* Slowpath of the chain - required for initialization and destruction,
	 * but isn't involved in regular functionality.
	 */

	/* Base address of a pre-allocated buffer for pbl */
	struct {
		dma_addr_t		p_phys_table;
		void			*p_virt_table;
	} pbl_sp;

	/* Address of first page of the chain  - the address is required
	 * for fastpath operation [consume/produce] but only for the the SINGLE
	 * flavour which isn't considered fastpath [== SPQ].
	 */
	void				*p_virt_addr;
	dma_addr_t			p_phys_addr;

	/* Total number of elements [for entire chain] */
	u32				size;

	u8				intended_use;

	/* TBD - do we really need this? Couldn't find usage for it */
	bool				b_external_pbl;

	void				*dp_ctx;
};

#define ECORE_CHAIN_PBL_ENTRY_SIZE	(8)
#define ECORE_CHAIN_PAGE_SIZE		(0x1000)
#define ELEMS_PER_PAGE(elem_size)	(ECORE_CHAIN_PAGE_SIZE/(elem_size))

#define UNUSABLE_ELEMS_PER_PAGE(elem_size, mode)		\
	  ((mode == ECORE_CHAIN_MODE_NEXT_PTR) ? 		\
	   (u8)(1 + ((sizeof(struct ecore_chain_next)-1) /	\
		     (elem_size))) : 0)

#define USABLE_ELEMS_PER_PAGE(elem_size, mode)			\
	  ((u32) (ELEMS_PER_PAGE(elem_size) - 			\
	  	  UNUSABLE_ELEMS_PER_PAGE(elem_size, mode)))

#define ECORE_CHAIN_PAGE_CNT(elem_cnt, elem_size, mode)		\
	DIV_ROUND_UP(elem_cnt, USABLE_ELEMS_PER_PAGE(elem_size, mode))

#define is_chain_u16(p)	((p)->cnt_type == ECORE_CHAIN_CNT_TYPE_U16)
#define is_chain_u32(p)	((p)->cnt_type == ECORE_CHAIN_CNT_TYPE_U32)

/* Accessors */
static OSAL_INLINE u16 ecore_chain_get_prod_idx(struct ecore_chain *p_chain)
{
	OSAL_ASSERT(is_chain_u16(p_chain));
	return p_chain->u.chain16.prod_idx;
}

#ifndef LINUX_REMOVE
static OSAL_INLINE u32 ecore_chain_get_prod_idx_u32(struct ecore_chain *p_chain)
{
	OSAL_ASSERT(is_chain_u32(p_chain));
	return p_chain->u.chain32.prod_idx;
}
#endif

static OSAL_INLINE u16 ecore_chain_get_cons_idx(struct ecore_chain *p_chain)
{
	OSAL_ASSERT(is_chain_u16(p_chain));
	return p_chain->u.chain16.cons_idx;
}

static OSAL_INLINE u32 ecore_chain_get_cons_idx_u32(struct ecore_chain *p_chain)
{
	OSAL_ASSERT(is_chain_u32(p_chain));
	return p_chain->u.chain32.cons_idx;
}

/* FIXME:
 * Should create OSALs for the below definitions.
 * For Linux, replace them with the existing U16_MAX and U32_MAX, and handle
 * kernel versions that lack them.
 */
#define ECORE_U16_MAX	((u16)~0U)
#define ECORE_U32_MAX	((u32)~0U)

static OSAL_INLINE u16 ecore_chain_get_elem_left(struct ecore_chain *p_chain)
{
	u16 used;

	OSAL_ASSERT(is_chain_u16(p_chain));

	used = (u16)(((u32)ECORE_U16_MAX + 1 +
		      (u32)(p_chain->u.chain16.prod_idx)) -
		     (u32)p_chain->u.chain16.cons_idx);
	if (p_chain->mode == ECORE_CHAIN_MODE_NEXT_PTR)
		used -= (((u32)ECORE_U16_MAX + 1) / p_chain->elem_per_page +
			 p_chain->u.chain16.prod_idx / p_chain->elem_per_page -
			 p_chain->u.chain16.cons_idx / p_chain->elem_per_page) %
			p_chain->page_cnt;

	return (u16)(p_chain->capacity - used);
}

static OSAL_INLINE u32
ecore_chain_get_elem_left_u32(struct ecore_chain *p_chain)
{
	u32 used;

	OSAL_ASSERT(is_chain_u32(p_chain));

	used = (u32)(((u64)ECORE_U32_MAX + 1 +
		      (u64)(p_chain->u.chain32.prod_idx)) -
		     (u64)p_chain->u.chain32.cons_idx);
	if (p_chain->mode == ECORE_CHAIN_MODE_NEXT_PTR)
		used -= (((u64)ECORE_U32_MAX + 1) / p_chain->elem_per_page +
			 p_chain->u.chain32.prod_idx / p_chain->elem_per_page -
			 p_chain->u.chain32.cons_idx / p_chain->elem_per_page) %
			p_chain->page_cnt;

	return p_chain->capacity - used;
}

#ifndef LINUX_REMOVE
static OSAL_INLINE u8 ecore_chain_is_full(struct ecore_chain *p_chain)
{
	if (is_chain_u16(p_chain))
		return (ecore_chain_get_elem_left(p_chain) ==
			p_chain->capacity);
	else
		return (ecore_chain_get_elem_left_u32(p_chain) ==
			p_chain->capacity);
}

static OSAL_INLINE u8 ecore_chain_is_empty(struct ecore_chain *p_chain)
{
	if (is_chain_u16(p_chain))
		return (ecore_chain_get_elem_left(p_chain) == 0);
	else
		return (ecore_chain_get_elem_left_u32(p_chain) == 0);
}

static OSAL_INLINE
u16 ecore_chain_get_elem_per_page(struct ecore_chain *p_chain)
{
	return p_chain->elem_per_page;
}
#endif

static OSAL_INLINE
u16 ecore_chain_get_usable_per_page(struct ecore_chain *p_chain)
{
	return p_chain->usable_per_page;
}

static OSAL_INLINE
u8 ecore_chain_get_unusable_per_page(struct ecore_chain *p_chain)
{
	return p_chain->elem_unusable;
}

#ifndef LINUX_REMOVE
static OSAL_INLINE u32 ecore_chain_get_size(struct ecore_chain *p_chain)
{
	return p_chain->size;
}
#endif

static OSAL_INLINE u32 ecore_chain_get_page_cnt(struct ecore_chain *p_chain)
{
	return p_chain->page_cnt;
}

static OSAL_INLINE
dma_addr_t ecore_chain_get_pbl_phys(struct ecore_chain *p_chain)
{
	return p_chain->pbl_sp.p_phys_table;
}

/**
 * @brief ecore_chain_advance_page -
 *
 * Advance the next element accros pages for a linked chain
 *
 * @param p_chain
 * @param p_next_elem
 * @param idx_to_inc
 * @param page_to_inc
 */
static OSAL_INLINE void
ecore_chain_advance_page(struct ecore_chain *p_chain, void **p_next_elem,
			 void *idx_to_inc, void *page_to_inc)
{
	struct ecore_chain_next *p_next = OSAL_NULL;
	u32 page_index = 0;

	switch(p_chain->mode) {
	case ECORE_CHAIN_MODE_NEXT_PTR:
		p_next = (struct ecore_chain_next *)(*p_next_elem);
		*p_next_elem = p_next->next_virt;
		if (is_chain_u16(p_chain))
			*(u16 *)idx_to_inc += (u16)p_chain->elem_unusable;
		else
			*(u32 *)idx_to_inc += (u16)p_chain->elem_unusable;
		break;
	case ECORE_CHAIN_MODE_SINGLE:
		*p_next_elem = p_chain->p_virt_addr;
		break;
	case ECORE_CHAIN_MODE_PBL:
		if (is_chain_u16(p_chain)) {
			if (++(*(u16 *)page_to_inc) == p_chain->page_cnt)
				*(u16 *)page_to_inc = 0;
			page_index = *(u16 *)page_to_inc;
		} else {
			if (++(*(u32 *)page_to_inc) == p_chain->page_cnt)
				*(u32 *)page_to_inc = 0;
			page_index = *(u32 *)page_to_inc;
		}
		*p_next_elem = p_chain->pbl.pp_virt_addr_tbl[page_index];
	}
}

#define is_unusable_idx(p, idx)			\
	(((p)->u.chain16.idx & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define is_unusable_idx_u32(p, idx)		\
	(((p)->u.chain32.idx & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define is_unusable_next_idx(p, idx)		\
	((((p)->u.chain16.idx + 1) & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define is_unusable_next_idx_u32(p, idx)	\
	((((p)->u.chain32.idx + 1) & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define test_and_skip(p, idx)							\
	do {									\
		if (is_chain_u16(p)) {						\
			if (is_unusable_idx(p, idx))				\
				(p)->u.chain16.idx += (p)->elem_unusable;	\
		} else {							\
			if (is_unusable_idx_u32(p, idx))			\
				(p)->u.chain32.idx += (p)->elem_unusable;	\
		}								\
	} while (0)

#ifndef LINUX_REMOVE
/**
 * @brief ecore_chain_return_multi_produced -
 *
 * A chain in which the driver "Produces" elements should use this API
 * to indicate previous produced elements are now consumed.
 *
 * @param p_chain
 * @param num
 */
static OSAL_INLINE
void ecore_chain_return_multi_produced(struct ecore_chain *p_chain, u32 num)
{
	if (is_chain_u16(p_chain))
		p_chain->u.chain16.cons_idx += (u16)num;
	else
		p_chain->u.chain32.cons_idx += num;
	test_and_skip(p_chain, cons_idx);
}
#endif

/**
 * @brief ecore_chain_return_produced -
 *
 * A chain in which the driver "Produces" elements should use this API
 * to indicate previous produced elements are now consumed.
 *
 * @param p_chain
 */
static OSAL_INLINE void ecore_chain_return_produced(struct ecore_chain *p_chain)
{
	if (is_chain_u16(p_chain))
		p_chain->u.chain16.cons_idx++;
	else
		p_chain->u.chain32.cons_idx++;
	test_and_skip(p_chain, cons_idx);
}

/**
 * @brief ecore_chain_produce -
 *
 * A chain in which the driver "Produces" elements should use this to get
 * a pointer to the next element which can be "Produced". It's driver
 * responsibility to validate that the chain has room for new element.
 *
 * @param p_chain
 *
 * @return void*, a pointer to next element
 */
static OSAL_INLINE void *ecore_chain_produce(struct ecore_chain *p_chain)
{
	void *p_ret = OSAL_NULL, *p_prod_idx, *p_prod_page_idx;

	if (is_chain_u16(p_chain)) {
		if ((p_chain->u.chain16.prod_idx &
		     p_chain->elem_per_page_mask) ==
		    p_chain->next_page_mask) {
			p_prod_idx = &p_chain->u.chain16.prod_idx;
			p_prod_page_idx = &p_chain->pbl.c.pbl_u16.prod_page_idx;
			ecore_chain_advance_page(p_chain, &p_chain->p_prod_elem,
						 p_prod_idx, p_prod_page_idx);
		}
		p_chain->u.chain16.prod_idx++;
	} else {
		if ((p_chain->u.chain32.prod_idx &
		     p_chain->elem_per_page_mask) ==
		    p_chain->next_page_mask) {
			p_prod_idx = &p_chain->u.chain32.prod_idx;
			p_prod_page_idx = &p_chain->pbl.c.pbl_u32.prod_page_idx;
			ecore_chain_advance_page(p_chain, &p_chain->p_prod_elem,
						 p_prod_idx, p_prod_page_idx);
		}
		p_chain->u.chain32.prod_idx++;
	}

	p_ret = p_chain->p_prod_elem;
	p_chain->p_prod_elem = (void*)(((u8*)p_chain->p_prod_elem) +
				       p_chain->elem_size);

	return p_ret;
}

/**
 * @brief ecore_chain_get_capacity -
 *
 * Get the maximum number of BDs in chain
 *
 * @param p_chain
 * @param num
 *
 * @return number of unusable BDs
 */
static OSAL_INLINE u32 ecore_chain_get_capacity(struct ecore_chain *p_chain)
{
	return p_chain->capacity;
}

/**
 * @brief ecore_chain_recycle_consumed -
 *
 * Returns an element which was previously consumed;
 * Increments producers so they could be written to FW.
 *
 * @param p_chain
 */
static OSAL_INLINE
void ecore_chain_recycle_consumed(struct ecore_chain *p_chain)
{
	test_and_skip(p_chain, prod_idx);
	if (is_chain_u16(p_chain))
		p_chain->u.chain16.prod_idx++;
	else
		p_chain->u.chain32.prod_idx++;
}

/**
 * @brief ecore_chain_consume -
 *
 * A Chain in which the driver utilizes data written by a different source
 * (i.e., FW) should use this to access passed buffers.
 *
 * @param p_chain
 *
 * @return void*, a pointer to the next buffer written
 */
static OSAL_INLINE void *ecore_chain_consume(struct ecore_chain *p_chain)
{
	void *p_ret = OSAL_NULL, *p_cons_idx, *p_cons_page_idx;

	if (is_chain_u16(p_chain)) {
		if ((p_chain->u.chain16.cons_idx &
		     p_chain->elem_per_page_mask) ==
		    p_chain->next_page_mask) {
			p_cons_idx = &p_chain->u.chain16.cons_idx;
			p_cons_page_idx = &p_chain->pbl.c.pbl_u16.cons_page_idx;
			ecore_chain_advance_page(p_chain, &p_chain->p_cons_elem,
						 p_cons_idx, p_cons_page_idx);
		}
		p_chain->u.chain16.cons_idx++;
	} else {
		if ((p_chain->u.chain32.cons_idx &
		     p_chain->elem_per_page_mask) ==
		    p_chain->next_page_mask) {
			p_cons_idx = &p_chain->u.chain32.cons_idx;
			p_cons_page_idx = &p_chain->pbl.c.pbl_u32.cons_page_idx;
			ecore_chain_advance_page(p_chain, &p_chain->p_cons_elem,
						 p_cons_idx, p_cons_page_idx);
		}
		p_chain->u.chain32.cons_idx++;
	}

	p_ret = p_chain->p_cons_elem;
	p_chain->p_cons_elem = (void*)(((u8*)p_chain->p_cons_elem) +
				       p_chain->elem_size);

	return p_ret;
}

/**
 * @brief ecore_chain_reset -
 *
 * Resets the chain to its start state
 *
 * @param p_chain pointer to a previously allocted chain
 */
static OSAL_INLINE void ecore_chain_reset(struct ecore_chain *p_chain)
{
	u32 i;

	if (is_chain_u16(p_chain)) {
		p_chain->u.chain16.prod_idx = 0;
		p_chain->u.chain16.cons_idx = 0;
	} else {
		p_chain->u.chain32.prod_idx = 0;
		p_chain->u.chain32.cons_idx = 0;
	}
	p_chain->p_cons_elem = p_chain->p_virt_addr;
	p_chain->p_prod_elem = p_chain->p_virt_addr;

	if (p_chain->mode == ECORE_CHAIN_MODE_PBL) {
		/* Use "page_cnt-1" as a reset value for the prod/cons page's
		 * indices, to avoid unnecessary page advancing on the first
		 * call to ecore_chain_produce/consume. Instead, the indices
		 * will be advanced to page_cnt and then will be wrapped to 0.
		 */
		u32 reset_val = p_chain->page_cnt - 1;

		if (is_chain_u16(p_chain)) {
			p_chain->pbl.c.pbl_u16.prod_page_idx = (u16)reset_val;
			p_chain->pbl.c.pbl_u16.cons_page_idx = (u16)reset_val;
		} else {
			p_chain->pbl.c.pbl_u32.prod_page_idx = reset_val;
			p_chain->pbl.c.pbl_u32.cons_page_idx = reset_val;
		}
	}

	switch (p_chain->intended_use) {
	case ECORE_CHAIN_USE_TO_CONSUME:
		/* produce empty elements */
		for (i = 0; i < p_chain->capacity; i++)
			ecore_chain_recycle_consumed(p_chain);
		break;

	case ECORE_CHAIN_USE_TO_CONSUME_PRODUCE:
	case ECORE_CHAIN_USE_TO_PRODUCE:
	default:
		/* Do nothing */
		break;
	}
}

/**
 * @brief ecore_chain_init_params -
 *
 * Initalizes a basic chain struct
 *
 * @param p_chain
 * @param page_cnt	number of pages in the allocated buffer
 * @param elem_size	size of each element in the chain
 * @param intended_use
 * @param mode
 * @param cnt_type
 * @param dp_ctx
 */
static OSAL_INLINE void
ecore_chain_init_params(struct ecore_chain *p_chain, u32 page_cnt, u8 elem_size,
			enum ecore_chain_use_mode intended_use,
			enum ecore_chain_mode mode,
			enum ecore_chain_cnt_type cnt_type, void *dp_ctx)
{
	/* chain fixed parameters */
	p_chain->p_virt_addr = OSAL_NULL;
	p_chain->p_phys_addr = 0;
	p_chain->elem_size = elem_size;
	p_chain->intended_use = (u8)intended_use;
	p_chain->mode = mode;
	p_chain->cnt_type = (u8)cnt_type;

	p_chain->elem_per_page = ELEMS_PER_PAGE(elem_size);
	p_chain->usable_per_page = USABLE_ELEMS_PER_PAGE(elem_size, mode);
	p_chain->elem_per_page_mask = p_chain->elem_per_page - 1;
	p_chain->elem_unusable = UNUSABLE_ELEMS_PER_PAGE(elem_size, mode);
	p_chain->next_page_mask = (p_chain->usable_per_page &
				   p_chain->elem_per_page_mask);

	p_chain->page_cnt = page_cnt;
	p_chain->capacity = p_chain->usable_per_page * page_cnt;
	p_chain->size = p_chain->elem_per_page * page_cnt;
	p_chain->b_external_pbl = false;
	p_chain->pbl_sp.p_phys_table = 0;
	p_chain->pbl_sp.p_virt_table = OSAL_NULL;
	p_chain->pbl.pp_virt_addr_tbl = OSAL_NULL;

	p_chain->dp_ctx = dp_ctx;
}

/**
 * @brief ecore_chain_init_mem -
 *
 * Initalizes a basic chain struct with its chain buffers
 *
 * @param p_chain
 * @param p_virt_addr	virtual address of allocated buffer's beginning
 * @param p_phys_addr	physical address of allocated buffer's beginning
 *
 */
static OSAL_INLINE void ecore_chain_init_mem(struct ecore_chain *p_chain,
					     void *p_virt_addr,
					     dma_addr_t p_phys_addr)
{
	p_chain->p_virt_addr = p_virt_addr;
	p_chain->p_phys_addr = p_phys_addr;
}

/**
 * @brief ecore_chain_init_pbl_mem -
 *
 * Initalizes a basic chain struct with its pbl buffers
 *
 * @param p_chain
 * @param p_virt_pbl	pointer to a pre allocated side table which will hold
 *                      virtual page addresses.
 * @param p_phys_pbl	pointer to a pre-allocated side table which will hold
 *                      physical page addresses.
 * @param pp_virt_addr_tbl
 *                      pointer to a pre-allocated side table which will hold
 *                      the virtual addresses of the chain pages.
 *
 */
static OSAL_INLINE void ecore_chain_init_pbl_mem(struct ecore_chain *p_chain,
						 void *p_virt_pbl,
						 dma_addr_t p_phys_pbl,
						 void **pp_virt_addr_tbl)
{
	p_chain->pbl_sp.p_phys_table = p_phys_pbl;
	p_chain->pbl_sp.p_virt_table = p_virt_pbl;
	p_chain->pbl.pp_virt_addr_tbl = pp_virt_addr_tbl;
}

/**
 * @brief ecore_chain_init_next_ptr_elem -
 *
 * Initalizes a next pointer element
 *
 * @param p_chain
 * @param p_virt_curr	virtual address of a chain page of which the next
 *                      pointer element is initialized
 * @param p_virt_next	virtual address of the next chain page
 * @param p_phys_next	physical address of the next chain page
 *
 */
static OSAL_INLINE void
ecore_chain_init_next_ptr_elem(struct ecore_chain *p_chain, void *p_virt_curr,
			       void *p_virt_next, dma_addr_t p_phys_next)
{
	struct ecore_chain_next *p_next;
	u32 size;

	size = p_chain->elem_size * p_chain->usable_per_page;
	p_next = (struct ecore_chain_next *)((u8 *)p_virt_curr + size);

	DMA_REGPAIR_LE(p_next->next_phys, p_phys_next);

	p_next->next_virt = p_virt_next;
}

/**
 * @brief ecore_chain_get_last_elem -
 *
 * Returns a pointer to the last element of the chain
 *
 * @param p_chain
 *
 * @return void*
 */
static OSAL_INLINE void *ecore_chain_get_last_elem(struct ecore_chain *p_chain)
{
	struct ecore_chain_next *p_next = OSAL_NULL;
	void *p_virt_addr = OSAL_NULL;
	u32 size, last_page_idx;

	if (!p_chain->p_virt_addr)
		goto out;

	switch (p_chain->mode) {
	case ECORE_CHAIN_MODE_NEXT_PTR:
		size = p_chain->elem_size * p_chain->usable_per_page;
		p_virt_addr = p_chain->p_virt_addr;
		p_next = (struct ecore_chain_next *)((u8 *)p_virt_addr + size);
		while (p_next->next_virt != p_chain->p_virt_addr) {
			p_virt_addr = p_next->next_virt;
			p_next = (struct ecore_chain_next *)((u8 *)p_virt_addr +
							     size);
		}
		break;
	case ECORE_CHAIN_MODE_SINGLE:
		p_virt_addr = p_chain->p_virt_addr;
		break;
	case ECORE_CHAIN_MODE_PBL:
		last_page_idx = p_chain->page_cnt - 1;
		p_virt_addr = p_chain->pbl.pp_virt_addr_tbl[last_page_idx];
		break;
	}
	/* p_virt_addr points at this stage to the last page of the chain */
	size = p_chain->elem_size * (p_chain->usable_per_page - 1);
	p_virt_addr = (u8 *)p_virt_addr + size;
out:
	return p_virt_addr;
}

/**
 * @brief ecore_chain_set_prod - sets the prod to the given value
 *
 * @param prod_idx
 * @param p_prod_elem
 */
static OSAL_INLINE void ecore_chain_set_prod(struct ecore_chain *p_chain,
					     u32 prod_idx, void *p_prod_elem)
{
	if (p_chain->mode == ECORE_CHAIN_MODE_PBL) {
		/* Use "prod_idx-1" since ecore_chain_produce() advances the
		 * page index before the producer index when getting to
		 * "next_page_mask".
		 */
		u32 elem_idx =
			(prod_idx - 1 + p_chain->capacity) % p_chain->capacity;
		u32 page_idx = elem_idx / p_chain->elem_per_page;

		if (is_chain_u16(p_chain))
			p_chain->pbl.c.pbl_u16.prod_page_idx = (u16)page_idx;
		else
			p_chain->pbl.c.pbl_u32.prod_page_idx = page_idx;
	}

	if (is_chain_u16(p_chain))
		p_chain->u.chain16.prod_idx = (u16)prod_idx;
	else
		p_chain->u.chain32.prod_idx = prod_idx;
	p_chain->p_prod_elem = p_prod_elem;
}

/**
 * @brief ecore_chain_set_cons - sets the cons to the given value
 *
 * @param cons_idx
 * @param p_cons_elem
 */
static OSAL_INLINE void ecore_chain_set_cons(struct ecore_chain *p_chain,
					     u32 cons_idx, void *p_cons_elem)
{
	if (p_chain->mode == ECORE_CHAIN_MODE_PBL) {
		/* Use "cons_idx-1" since ecore_chain_consume() advances the
		 * page index before the consumer index when getting to
		 * "next_page_mask".
		 */
		u32 elem_idx =
			(cons_idx - 1 + p_chain->capacity) % p_chain->capacity;
		u32 page_idx = elem_idx / p_chain->elem_per_page;

		if (is_chain_u16(p_chain))
			p_chain->pbl.c.pbl_u16.cons_page_idx = (u16)page_idx;
		else
			p_chain->pbl.c.pbl_u32.cons_page_idx = page_idx;
	}

	if (is_chain_u16(p_chain))
		p_chain->u.chain16.cons_idx = (u16)cons_idx;
	else
		p_chain->u.chain32.cons_idx = cons_idx;

	p_chain->p_cons_elem = p_cons_elem;
}

/**
 * @brief ecore_chain_pbl_zero_mem - set chain memory to 0
 *
 * @param p_chain
 */
static OSAL_INLINE void ecore_chain_pbl_zero_mem(struct ecore_chain *p_chain)
{
	u32 i, page_cnt;

	if (p_chain->mode != ECORE_CHAIN_MODE_PBL)
		return;

	page_cnt = ecore_chain_get_page_cnt(p_chain);

	for (i = 0; i < page_cnt; i++)
		OSAL_MEM_ZERO(p_chain->pbl.pp_virt_addr_tbl[i],
			      ECORE_CHAIN_PAGE_SIZE);
}

int ecore_chain_print(struct ecore_chain *p_chain, char *buffer,
		      u32 buffer_size, u32 *element_indx, u32 stop_indx,
		      bool print_metadata,
		      int (*func_ptr_print_element)(struct ecore_chain *p_chain,
						    void *p_element,
						    char *buffer),
		      int (*func_ptr_print_metadata)(struct ecore_chain *p_chain,
						     char *buffer));

#endif /* __ECORE_CHAIN_H__ */
