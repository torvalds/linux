/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_CHAIN_H
#define _QED_CHAIN_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/qed/common_hsi.h>

enum qed_chain_mode {
	/* Each Page contains a next pointer at its end */
	QED_CHAIN_MODE_NEXT_PTR,

	/* Chain is a single page (next ptr) is not required */
	QED_CHAIN_MODE_SINGLE,

	/* Page pointers are located in a side list */
	QED_CHAIN_MODE_PBL,
};

enum qed_chain_use_mode {
	QED_CHAIN_USE_TO_PRODUCE,			/* Chain starts empty */
	QED_CHAIN_USE_TO_CONSUME,			/* Chain starts full */
	QED_CHAIN_USE_TO_CONSUME_PRODUCE,		/* Chain starts empty */
};

enum qed_chain_cnt_type {
	/* The chain's size/prod/cons are kept in 16-bit variables */
	QED_CHAIN_CNT_TYPE_U16,

	/* The chain's size/prod/cons are kept in 32-bit variables  */
	QED_CHAIN_CNT_TYPE_U32,
};

struct qed_chain_next {
	struct regpair					next_phys;
	void						*next_virt;
};

struct qed_chain_pbl_u16 {
	u16						prod_page_idx;
	u16						cons_page_idx;
};

struct qed_chain_pbl_u32 {
	u32						prod_page_idx;
	u32						cons_page_idx;
};

struct qed_chain_u16 {
	/* Cyclic index of next element to produce/consume */
	u16						prod_idx;
	u16						cons_idx;
};

struct qed_chain_u32 {
	/* Cyclic index of next element to produce/consume */
	u32						prod_idx;
	u32						cons_idx;
};

struct addr_tbl_entry {
	void						*virt_addr;
	dma_addr_t					dma_map;
};

struct qed_chain {
	/* Fastpath portion of the chain - required for commands such
	 * as produce / consume.
	 */

	/* Point to next element to produce/consume */
	void						*p_prod_elem;
	void						*p_cons_elem;

	/* Fastpath portions of the PBL [if exists] */

	struct {
		/* Table for keeping the virtual and physical addresses of the
		 * chain pages, respectively to the physical addresses
		 * in the pbl table.
		 */
		struct addr_tbl_entry			*pp_addr_tbl;

		union {
			struct qed_chain_pbl_u16	u16;
			struct qed_chain_pbl_u32	u32;
		}					c;
	}						pbl;

	union {
		struct qed_chain_u16			chain16;
		struct qed_chain_u32			chain32;
	}						u;

	/* Capacity counts only usable elements */
	u32						capacity;
	u32						page_cnt;

	enum qed_chain_mode				mode;

	/* Elements information for fast calculations */
	u16						elem_per_page;
	u16						elem_per_page_mask;
	u16						elem_size;
	u16						next_page_mask;
	u16						usable_per_page;
	u8						elem_unusable;

	enum qed_chain_cnt_type				cnt_type;

	/* Slowpath of the chain - required for initialization and destruction,
	 * but isn't involved in regular functionality.
	 */

	u32						page_size;

	/* Base address of a pre-allocated buffer for pbl */
	struct {
		__le64					*table_virt;
		dma_addr_t				table_phys;
		size_t					table_size;
	}						pbl_sp;

	/* Address of first page of the chain - the address is required
	 * for fastpath operation [consume/produce] but only for the SINGLE
	 * flavour which isn't considered fastpath [== SPQ].
	 */
	void						*p_virt_addr;
	dma_addr_t					p_phys_addr;

	/* Total number of elements [for entire chain] */
	u32						size;

	enum qed_chain_use_mode				intended_use;

	bool						b_external_pbl;
};

struct qed_chain_init_params {
	enum qed_chain_mode				mode;
	enum qed_chain_use_mode				intended_use;
	enum qed_chain_cnt_type				cnt_type;

	u32						page_size;
	u32						num_elems;
	size_t						elem_size;

	void						*ext_pbl_virt;
	dma_addr_t					ext_pbl_phys;
};

#define QED_CHAIN_PAGE_SIZE				SZ_4K

#define ELEMS_PER_PAGE(elem_size, page_size)				     \
	((page_size) / (elem_size))

#define UNUSABLE_ELEMS_PER_PAGE(elem_size, mode)			     \
	(((mode) == QED_CHAIN_MODE_NEXT_PTR) ?				     \
	 (u8)(1 + ((sizeof(struct qed_chain_next) - 1) / (elem_size))) :     \
	 0)

#define USABLE_ELEMS_PER_PAGE(elem_size, page_size, mode)		     \
	((u32)(ELEMS_PER_PAGE((elem_size), (page_size)) -		     \
	       UNUSABLE_ELEMS_PER_PAGE((elem_size), (mode))))

#define QED_CHAIN_PAGE_CNT(elem_cnt, elem_size, page_size, mode)	     \
	DIV_ROUND_UP((elem_cnt),					     \
		     USABLE_ELEMS_PER_PAGE((elem_size), (page_size), (mode)))

#define is_chain_u16(p)							     \
	((p)->cnt_type == QED_CHAIN_CNT_TYPE_U16)
#define is_chain_u32(p)							     \
	((p)->cnt_type == QED_CHAIN_CNT_TYPE_U32)

/* Accessors */

static inline u16 qed_chain_get_prod_idx(const struct qed_chain *chain)
{
	return chain->u.chain16.prod_idx;
}

static inline u16 qed_chain_get_cons_idx(const struct qed_chain *chain)
{
	return chain->u.chain16.cons_idx;
}

static inline u32 qed_chain_get_prod_idx_u32(const struct qed_chain *chain)
{
	return chain->u.chain32.prod_idx;
}

static inline u32 qed_chain_get_cons_idx_u32(const struct qed_chain *chain)
{
	return chain->u.chain32.cons_idx;
}

static inline u16 qed_chain_get_elem_used(const struct qed_chain *chain)
{
	u32 prod = qed_chain_get_prod_idx(chain);
	u32 cons = qed_chain_get_cons_idx(chain);
	u16 elem_per_page = chain->elem_per_page;
	u16 used;

	if (prod < cons)
		prod += (u32)U16_MAX + 1;

	used = (u16)(prod - cons);
	if (chain->mode == QED_CHAIN_MODE_NEXT_PTR)
		used -= (u16)(prod / elem_per_page - cons / elem_per_page);

	return used;
}

static inline u16 qed_chain_get_elem_left(const struct qed_chain *chain)
{
	return (u16)(chain->capacity - qed_chain_get_elem_used(chain));
}

static inline u32 qed_chain_get_elem_used_u32(const struct qed_chain *chain)
{
	u64 prod = qed_chain_get_prod_idx_u32(chain);
	u64 cons = qed_chain_get_cons_idx_u32(chain);
	u16 elem_per_page = chain->elem_per_page;
	u32 used;

	if (prod < cons)
		prod += (u64)U32_MAX + 1;

	used = (u32)(prod - cons);
	if (chain->mode == QED_CHAIN_MODE_NEXT_PTR)
		used -= (u32)(prod / elem_per_page - cons / elem_per_page);

	return used;
}

static inline u32 qed_chain_get_elem_left_u32(const struct qed_chain *chain)
{
	return chain->capacity - qed_chain_get_elem_used_u32(chain);
}

static inline u16 qed_chain_get_usable_per_page(const struct qed_chain *chain)
{
	return chain->usable_per_page;
}

static inline u8 qed_chain_get_unusable_per_page(const struct qed_chain *chain)
{
	return chain->elem_unusable;
}

static inline u32 qed_chain_get_page_cnt(const struct qed_chain *chain)
{
	return chain->page_cnt;
}

static inline dma_addr_t qed_chain_get_pbl_phys(const struct qed_chain *chain)
{
	return chain->pbl_sp.table_phys;
}

/**
 * qed_chain_advance_page(): Advance the next element across pages for a
 *                           linked chain.
 *
 * @p_chain: P_chain.
 * @p_next_elem: P_next_elem.
 * @idx_to_inc: Idx_to_inc.
 * @page_to_inc: page_to_inc.
 *
 * Return: Void.
 */
static inline void
qed_chain_advance_page(struct qed_chain *p_chain,
		       void **p_next_elem, void *idx_to_inc, void *page_to_inc)
{
	struct qed_chain_next *p_next = NULL;
	u32 page_index = 0;

	switch (p_chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		p_next = *p_next_elem;
		*p_next_elem = p_next->next_virt;
		if (is_chain_u16(p_chain))
			*(u16 *)idx_to_inc += p_chain->elem_unusable;
		else
			*(u32 *)idx_to_inc += p_chain->elem_unusable;
		break;
	case QED_CHAIN_MODE_SINGLE:
		*p_next_elem = p_chain->p_virt_addr;
		break;

	case QED_CHAIN_MODE_PBL:
		if (is_chain_u16(p_chain)) {
			if (++(*(u16 *)page_to_inc) == p_chain->page_cnt)
				*(u16 *)page_to_inc = 0;
			page_index = *(u16 *)page_to_inc;
		} else {
			if (++(*(u32 *)page_to_inc) == p_chain->page_cnt)
				*(u32 *)page_to_inc = 0;
			page_index = *(u32 *)page_to_inc;
		}
		*p_next_elem = p_chain->pbl.pp_addr_tbl[page_index].virt_addr;
	}
}

#define is_unusable_idx(p, idx)	\
	(((p)->u.chain16.idx & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define is_unusable_idx_u32(p, idx) \
	(((p)->u.chain32.idx & (p)->elem_per_page_mask) == (p)->usable_per_page)
#define is_unusable_next_idx(p, idx)				 \
	((((p)->u.chain16.idx + 1) & (p)->elem_per_page_mask) == \
	 (p)->usable_per_page)

#define is_unusable_next_idx_u32(p, idx)			 \
	((((p)->u.chain32.idx + 1) & (p)->elem_per_page_mask) == \
	 (p)->usable_per_page)

#define test_and_skip(p, idx)						   \
	do {						\
		if (is_chain_u16(p)) {					   \
			if (is_unusable_idx(p, idx))			   \
				(p)->u.chain16.idx += (p)->elem_unusable;  \
		} else {						   \
			if (is_unusable_idx_u32(p, idx))		   \
				(p)->u.chain32.idx += (p)->elem_unusable;  \
		}					\
	} while (0)

/**
 * qed_chain_return_produced(): A chain in which the driver "Produces"
 *                              elements should use this API
 *                              to indicate previous produced elements
 *                              are now consumed.
 *
 * @p_chain: Chain.
 *
 * Return: Void.
 */
static inline void qed_chain_return_produced(struct qed_chain *p_chain)
{
	if (is_chain_u16(p_chain))
		p_chain->u.chain16.cons_idx++;
	else
		p_chain->u.chain32.cons_idx++;
	test_and_skip(p_chain, cons_idx);
}

/**
 * qed_chain_produce(): A chain in which the driver "Produces"
 *                      elements should use this to get a pointer to
 *                      the next element which can be "Produced". It's driver
 *                      responsibility to validate that the chain has room for
 *                      new element.
 *
 * @p_chain: Chain.
 *
 * Return: void*, a pointer to next element.
 */
static inline void *qed_chain_produce(struct qed_chain *p_chain)
{
	void *p_ret = NULL, *p_prod_idx, *p_prod_page_idx;

	if (is_chain_u16(p_chain)) {
		if ((p_chain->u.chain16.prod_idx &
		     p_chain->elem_per_page_mask) == p_chain->next_page_mask) {
			p_prod_idx = &p_chain->u.chain16.prod_idx;
			p_prod_page_idx = &p_chain->pbl.c.u16.prod_page_idx;
			qed_chain_advance_page(p_chain, &p_chain->p_prod_elem,
					       p_prod_idx, p_prod_page_idx);
		}
		p_chain->u.chain16.prod_idx++;
	} else {
		if ((p_chain->u.chain32.prod_idx &
		     p_chain->elem_per_page_mask) == p_chain->next_page_mask) {
			p_prod_idx = &p_chain->u.chain32.prod_idx;
			p_prod_page_idx = &p_chain->pbl.c.u32.prod_page_idx;
			qed_chain_advance_page(p_chain, &p_chain->p_prod_elem,
					       p_prod_idx, p_prod_page_idx);
		}
		p_chain->u.chain32.prod_idx++;
	}

	p_ret = p_chain->p_prod_elem;
	p_chain->p_prod_elem = (void *)(((u8 *)p_chain->p_prod_elem) +
					p_chain->elem_size);

	return p_ret;
}

/**
 * qed_chain_get_capacity(): Get the maximum number of BDs in chain
 *
 * @p_chain: Chain.
 *
 * Return: number of unusable BDs.
 */
static inline u32 qed_chain_get_capacity(struct qed_chain *p_chain)
{
	return p_chain->capacity;
}

/**
 * qed_chain_recycle_consumed(): Returns an element which was
 *                               previously consumed;
 *                               Increments producers so they could
 *                               be written to FW.
 *
 * @p_chain: Chain.
 *
 * Return: Void.
 */
static inline void qed_chain_recycle_consumed(struct qed_chain *p_chain)
{
	test_and_skip(p_chain, prod_idx);
	if (is_chain_u16(p_chain))
		p_chain->u.chain16.prod_idx++;
	else
		p_chain->u.chain32.prod_idx++;
}

/**
 * qed_chain_consume(): A Chain in which the driver utilizes data written
 *                      by a different source (i.e., FW) should use this to
 *                      access passed buffers.
 *
 * @p_chain: Chain.
 *
 * Return: void*, a pointer to the next buffer written.
 */
static inline void *qed_chain_consume(struct qed_chain *p_chain)
{
	void *p_ret = NULL, *p_cons_idx, *p_cons_page_idx;

	if (is_chain_u16(p_chain)) {
		if ((p_chain->u.chain16.cons_idx &
		     p_chain->elem_per_page_mask) == p_chain->next_page_mask) {
			p_cons_idx = &p_chain->u.chain16.cons_idx;
			p_cons_page_idx = &p_chain->pbl.c.u16.cons_page_idx;
			qed_chain_advance_page(p_chain, &p_chain->p_cons_elem,
					       p_cons_idx, p_cons_page_idx);
		}
		p_chain->u.chain16.cons_idx++;
	} else {
		if ((p_chain->u.chain32.cons_idx &
		     p_chain->elem_per_page_mask) == p_chain->next_page_mask) {
			p_cons_idx = &p_chain->u.chain32.cons_idx;
			p_cons_page_idx = &p_chain->pbl.c.u32.cons_page_idx;
			qed_chain_advance_page(p_chain, &p_chain->p_cons_elem,
					       p_cons_idx, p_cons_page_idx);
		}
		p_chain->u.chain32.cons_idx++;
	}

	p_ret = p_chain->p_cons_elem;
	p_chain->p_cons_elem = (void *)(((u8 *)p_chain->p_cons_elem) +
					p_chain->elem_size);

	return p_ret;
}

/**
 * qed_chain_reset(): Resets the chain to its start state.
 *
 * @p_chain: pointer to a previously allocated chain.
 *
 * Return Void.
 */
static inline void qed_chain_reset(struct qed_chain *p_chain)
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

	if (p_chain->mode == QED_CHAIN_MODE_PBL) {
		/* Use (page_cnt - 1) as a reset value for the prod/cons page's
		 * indices, to avoid unnecessary page advancing on the first
		 * call to qed_chain_produce/consume. Instead, the indices
		 * will be advanced to page_cnt and then will be wrapped to 0.
		 */
		u32 reset_val = p_chain->page_cnt - 1;

		if (is_chain_u16(p_chain)) {
			p_chain->pbl.c.u16.prod_page_idx = (u16)reset_val;
			p_chain->pbl.c.u16.cons_page_idx = (u16)reset_val;
		} else {
			p_chain->pbl.c.u32.prod_page_idx = reset_val;
			p_chain->pbl.c.u32.cons_page_idx = reset_val;
		}
	}

	switch (p_chain->intended_use) {
	case QED_CHAIN_USE_TO_CONSUME:
		/* produce empty elements */
		for (i = 0; i < p_chain->capacity; i++)
			qed_chain_recycle_consumed(p_chain);
		break;

	case QED_CHAIN_USE_TO_CONSUME_PRODUCE:
	case QED_CHAIN_USE_TO_PRODUCE:
	default:
		/* Do nothing */
		break;
	}
}

/**
 * qed_chain_get_last_elem(): Returns a pointer to the last element of the
 *                            chain.
 *
 * @p_chain: Chain.
 *
 * Return: void*.
 */
static inline void *qed_chain_get_last_elem(struct qed_chain *p_chain)
{
	struct qed_chain_next *p_next = NULL;
	void *p_virt_addr = NULL;
	u32 size, last_page_idx;

	if (!p_chain->p_virt_addr)
		goto out;

	switch (p_chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		size = p_chain->elem_size * p_chain->usable_per_page;
		p_virt_addr = p_chain->p_virt_addr;
		p_next = (struct qed_chain_next *)((u8 *)p_virt_addr + size);
		while (p_next->next_virt != p_chain->p_virt_addr) {
			p_virt_addr = p_next->next_virt;
			p_next = (struct qed_chain_next *)((u8 *)p_virt_addr +
							   size);
		}
		break;
	case QED_CHAIN_MODE_SINGLE:
		p_virt_addr = p_chain->p_virt_addr;
		break;
	case QED_CHAIN_MODE_PBL:
		last_page_idx = p_chain->page_cnt - 1;
		p_virt_addr = p_chain->pbl.pp_addr_tbl[last_page_idx].virt_addr;
		break;
	}
	/* p_virt_addr points at this stage to the last page of the chain */
	size = p_chain->elem_size * (p_chain->usable_per_page - 1);
	p_virt_addr = (u8 *)p_virt_addr + size;
out:
	return p_virt_addr;
}

/**
 * qed_chain_set_prod(): sets the prod to the given value.
 *
 * @p_chain: Chain.
 * @prod_idx: Prod Idx.
 * @p_prod_elem: Prod elem.
 *
 * Return Void.
 */
static inline void qed_chain_set_prod(struct qed_chain *p_chain,
				      u32 prod_idx, void *p_prod_elem)
{
	if (p_chain->mode == QED_CHAIN_MODE_PBL) {
		u32 cur_prod, page_mask, page_cnt, page_diff;

		cur_prod = is_chain_u16(p_chain) ? p_chain->u.chain16.prod_idx :
			   p_chain->u.chain32.prod_idx;

		/* Assume that number of elements in a page is power of 2 */
		page_mask = ~p_chain->elem_per_page_mask;

		/* Use "cur_prod - 1" and "prod_idx - 1" since producer index
		 * reaches the first element of next page before the page index
		 * is incremented. See qed_chain_produce().
		 * Index wrap around is not a problem because the difference
		 * between current and given producer indices is always
		 * positive and lower than the chain's capacity.
		 */
		page_diff = (((cur_prod - 1) & page_mask) -
			     ((prod_idx - 1) & page_mask)) /
			    p_chain->elem_per_page;

		page_cnt = qed_chain_get_page_cnt(p_chain);
		if (is_chain_u16(p_chain))
			p_chain->pbl.c.u16.prod_page_idx =
				(p_chain->pbl.c.u16.prod_page_idx -
				 page_diff + page_cnt) % page_cnt;
		else
			p_chain->pbl.c.u32.prod_page_idx =
				(p_chain->pbl.c.u32.prod_page_idx -
				 page_diff + page_cnt) % page_cnt;
	}

	if (is_chain_u16(p_chain))
		p_chain->u.chain16.prod_idx = (u16) prod_idx;
	else
		p_chain->u.chain32.prod_idx = prod_idx;
	p_chain->p_prod_elem = p_prod_elem;
}

/**
 * qed_chain_pbl_zero_mem(): set chain memory to 0.
 *
 * @p_chain: Chain.
 *
 * Return: Void.
 */
static inline void qed_chain_pbl_zero_mem(struct qed_chain *p_chain)
{
	u32 i, page_cnt;

	if (p_chain->mode != QED_CHAIN_MODE_PBL)
		return;

	page_cnt = qed_chain_get_page_cnt(p_chain);

	for (i = 0; i < page_cnt; i++)
		memset(p_chain->pbl.pp_addr_tbl[i].virt_addr, 0,
		       p_chain->page_size);
}

#endif
