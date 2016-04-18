/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_CHAIN_H
#define _QED_CHAIN_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/qed/common_hsi.h>

/* dma_addr_t manip */
#define DMA_LO_LE(x)            cpu_to_le32(lower_32_bits(x))
#define DMA_HI_LE(x)            cpu_to_le32(upper_32_bits(x))
#define DMA_REGPAIR_LE(x, val)  do { \
					(x).hi = DMA_HI_LE((val)); \
					(x).lo = DMA_LO_LE((val)); \
				} while (0)

#define HILO_GEN(hi, lo, type)  ((((type)(hi)) << 32) + (lo))
#define HILO_DMA(hi, lo)        HILO_GEN(hi, lo, dma_addr_t)
#define HILO_64(hi, lo) HILO_GEN((le32_to_cpu(hi)), (le32_to_cpu(lo)), u64)
#define HILO_DMA_REGPAIR(regpair)       (HILO_DMA(regpair.hi, regpair.lo))
#define HILO_64_REGPAIR(regpair)        (HILO_64(regpair.hi, regpair.lo))

enum qed_chain_mode {
	/* Each Page contains a next pointer at its end */
	QED_CHAIN_MODE_NEXT_PTR,

	/* Chain is a single page (next ptr) is unrequired */
	QED_CHAIN_MODE_SINGLE,

	/* Page pointers are located in a side list */
	QED_CHAIN_MODE_PBL,
};

enum qed_chain_use_mode {
	QED_CHAIN_USE_TO_PRODUCE,		/* Chain starts empty */
	QED_CHAIN_USE_TO_CONSUME,		/* Chain starts full */
	QED_CHAIN_USE_TO_CONSUME_PRODUCE,	/* Chain starts empty */
};

struct qed_chain_next {
	struct regpair	next_phys;
	void		*next_virt;
};

struct qed_chain_pbl {
	dma_addr_t	p_phys_table;
	void		*p_virt_table;
	u16		prod_page_idx;
	u16		cons_page_idx;
};

struct qed_chain {
	void			*p_virt_addr;
	dma_addr_t		p_phys_addr;
	void			*p_prod_elem;
	void			*p_cons_elem;
	u16			page_cnt;
	enum qed_chain_mode	mode;
	enum qed_chain_use_mode intended_use; /* used to produce/consume */
	u16			capacity; /*< number of _usable_ elements */
	u16			size; /* number of elements */
	u16			prod_idx;
	u16			cons_idx;
	u16			elem_per_page;
	u16			elem_per_page_mask;
	u16			elem_unusable;
	u16			usable_per_page;
	u16			elem_size;
	u16			next_page_mask;
	struct qed_chain_pbl	pbl;
};

#define QED_CHAIN_PBL_ENTRY_SIZE        (8)
#define QED_CHAIN_PAGE_SIZE             (0x1000)
#define ELEMS_PER_PAGE(elem_size)       (QED_CHAIN_PAGE_SIZE / (elem_size))

#define UNUSABLE_ELEMS_PER_PAGE(elem_size, mode)     \
	((mode == QED_CHAIN_MODE_NEXT_PTR) ?	     \
	 (1 + ((sizeof(struct qed_chain_next) - 1) / \
	       (elem_size))) : 0)

#define USABLE_ELEMS_PER_PAGE(elem_size, mode) \
	((u32)(ELEMS_PER_PAGE(elem_size) -     \
	       UNUSABLE_ELEMS_PER_PAGE(elem_size, mode)))

#define QED_CHAIN_PAGE_CNT(elem_cnt, elem_size, mode) \
	DIV_ROUND_UP(elem_cnt, USABLE_ELEMS_PER_PAGE(elem_size, mode))

/* Accessors */
static inline u16 qed_chain_get_prod_idx(struct qed_chain *p_chain)
{
	return p_chain->prod_idx;
}

static inline u16 qed_chain_get_cons_idx(struct qed_chain *p_chain)
{
	return p_chain->cons_idx;
}

static inline u16 qed_chain_get_elem_left(struct qed_chain *p_chain)
{
	u16 used;

	/* we don't need to trancate upon assignmet, as we assign u32->u16 */
	used = ((u32)0x10000u + (u32)(p_chain->prod_idx)) -
		(u32)p_chain->cons_idx;
	if (p_chain->mode == QED_CHAIN_MODE_NEXT_PTR)
		used -= p_chain->prod_idx / p_chain->elem_per_page -
			p_chain->cons_idx / p_chain->elem_per_page;

	return p_chain->capacity - used;
}

static inline u8 qed_chain_is_full(struct qed_chain *p_chain)
{
	return qed_chain_get_elem_left(p_chain) == p_chain->capacity;
}

static inline u8 qed_chain_is_empty(struct qed_chain *p_chain)
{
	return qed_chain_get_elem_left(p_chain) == 0;
}

static inline u16 qed_chain_get_elem_per_page(
	struct qed_chain *p_chain)
{
	return p_chain->elem_per_page;
}

static inline u16 qed_chain_get_usable_per_page(
	struct qed_chain *p_chain)
{
	return p_chain->usable_per_page;
}

static inline u16 qed_chain_get_unusable_per_page(
	struct qed_chain *p_chain)
{
	return p_chain->elem_unusable;
}

static inline u16 qed_chain_get_size(struct qed_chain *p_chain)
{
	return p_chain->size;
}

static inline dma_addr_t
qed_chain_get_pbl_phys(struct qed_chain *p_chain)
{
	return p_chain->pbl.p_phys_table;
}

/**
 * @brief qed_chain_advance_page -
 *
 * Advance the next element accros pages for a linked chain
 *
 * @param p_chain
 * @param p_next_elem
 * @param idx_to_inc
 * @param page_to_inc
 */
static inline void
qed_chain_advance_page(struct qed_chain *p_chain,
		       void **p_next_elem,
		       u16 *idx_to_inc,
		       u16 *page_to_inc)

{
	switch (p_chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
	{
		struct qed_chain_next *p_next = *p_next_elem;
		*p_next_elem = p_next->next_virt;
		*idx_to_inc += p_chain->elem_unusable;
		break;
	}
	case QED_CHAIN_MODE_SINGLE:
		*p_next_elem = p_chain->p_virt_addr;
		break;

	case QED_CHAIN_MODE_PBL:
		/* It is assumed pages are sequential, next element needs
		 * to change only when passing going back to first from last.
		 */
		if (++(*page_to_inc) == p_chain->page_cnt) {
			*page_to_inc = 0;
			*p_next_elem = p_chain->p_virt_addr;
		}
	}
}

#define is_unusable_idx(p, idx)	\
	(((p)->idx & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define is_unusable_next_idx(p, idx) \
	((((p)->idx + 1) & (p)->elem_per_page_mask) == (p)->usable_per_page)

#define test_ans_skip(p, idx)				\
	do {						\
		if (is_unusable_idx(p, idx)) {		\
			(p)->idx += (p)->elem_unusable;	\
		}					\
	} while (0)

/**
 * @brief qed_chain_return_multi_produced -
 *
 * A chain in which the driver "Produces" elements should use this API
 * to indicate previous produced elements are now consumed.
 *
 * @param p_chain
 * @param num
 */
static inline void
qed_chain_return_multi_produced(struct qed_chain *p_chain,
				u16 num)
{
	p_chain->cons_idx += num;
	test_ans_skip(p_chain, cons_idx);
}

/**
 * @brief qed_chain_return_produced -
 *
 * A chain in which the driver "Produces" elements should use this API
 * to indicate previous produced elements are now consumed.
 *
 * @param p_chain
 */
static inline void qed_chain_return_produced(struct qed_chain *p_chain)
{
	p_chain->cons_idx++;
	test_ans_skip(p_chain, cons_idx);
}

/**
 * @brief qed_chain_produce -
 *
 * A chain in which the driver "Produces" elements should use this to get
 * a pointer to the next element which can be "Produced". It's driver
 * responsibility to validate that the chain has room for new element.
 *
 * @param p_chain
 *
 * @return void*, a pointer to next element
 */
static inline void *qed_chain_produce(struct qed_chain *p_chain)
{
	void *ret = NULL;

	if ((p_chain->prod_idx & p_chain->elem_per_page_mask) ==
	    p_chain->next_page_mask) {
		qed_chain_advance_page(p_chain, &p_chain->p_prod_elem,
				       &p_chain->prod_idx,
				       &p_chain->pbl.prod_page_idx);
	}

	ret = p_chain->p_prod_elem;
	p_chain->prod_idx++;
	p_chain->p_prod_elem = (void *)(((u8 *)p_chain->p_prod_elem) +
					p_chain->elem_size);

	return ret;
}

/**
 * @brief qed_chain_get_capacity -
 *
 * Get the maximum number of BDs in chain
 *
 * @param p_chain
 * @param num
 *
 * @return u16, number of unusable BDs
 */
static inline u16 qed_chain_get_capacity(struct qed_chain *p_chain)
{
	return p_chain->capacity;
}

/**
 * @brief qed_chain_recycle_consumed -
 *
 * Returns an element which was previously consumed;
 * Increments producers so they could be written to FW.
 *
 * @param p_chain
 */
static inline void
qed_chain_recycle_consumed(struct qed_chain *p_chain)
{
	test_ans_skip(p_chain, prod_idx);
	p_chain->prod_idx++;
}

/**
 * @brief qed_chain_consume -
 *
 * A Chain in which the driver utilizes data written by a different source
 * (i.e., FW) should use this to access passed buffers.
 *
 * @param p_chain
 *
 * @return void*, a pointer to the next buffer written
 */
static inline void *qed_chain_consume(struct qed_chain *p_chain)
{
	void *ret = NULL;

	if ((p_chain->cons_idx & p_chain->elem_per_page_mask) ==
	    p_chain->next_page_mask) {
		qed_chain_advance_page(p_chain, &p_chain->p_cons_elem,
				       &p_chain->cons_idx,
				       &p_chain->pbl.cons_page_idx);
	}

	ret = p_chain->p_cons_elem;
	p_chain->cons_idx++;
	p_chain->p_cons_elem = (void *)(((u8 *)p_chain->p_cons_elem) +
					p_chain->elem_size);

	return ret;
}

/**
 * @brief qed_chain_reset - Resets the chain to its start state
 *
 * @param p_chain pointer to a previously allocted chain
 */
static inline void qed_chain_reset(struct qed_chain *p_chain)
{
	int i;

	p_chain->prod_idx	= 0;
	p_chain->cons_idx	= 0;
	p_chain->p_cons_elem	= p_chain->p_virt_addr;
	p_chain->p_prod_elem	= p_chain->p_virt_addr;

	if (p_chain->mode == QED_CHAIN_MODE_PBL) {
		p_chain->pbl.prod_page_idx	= p_chain->page_cnt - 1;
		p_chain->pbl.cons_page_idx	= p_chain->page_cnt - 1;
	}

	switch (p_chain->intended_use) {
	case QED_CHAIN_USE_TO_CONSUME_PRODUCE:
	case QED_CHAIN_USE_TO_PRODUCE:
		/* Do nothing */
		break;

	case QED_CHAIN_USE_TO_CONSUME:
		/* produce empty elements */
		for (i = 0; i < p_chain->capacity; i++)
			qed_chain_recycle_consumed(p_chain);
		break;
	}
}

/**
 * @brief qed_chain_init - Initalizes a basic chain struct
 *
 * @param p_chain
 * @param p_virt_addr
 * @param p_phys_addr	physical address of allocated buffer's beginning
 * @param page_cnt	number of pages in the allocated buffer
 * @param elem_size	size of each element in the chain
 * @param intended_use
 * @param mode
 */
static inline void qed_chain_init(struct qed_chain *p_chain,
				  void *p_virt_addr,
				  dma_addr_t p_phys_addr,
				  u16 page_cnt,
				  u8 elem_size,
				  enum qed_chain_use_mode intended_use,
				  enum qed_chain_mode mode)
{
	/* chain fixed parameters */
	p_chain->p_virt_addr	= p_virt_addr;
	p_chain->p_phys_addr	= p_phys_addr;
	p_chain->elem_size	= elem_size;
	p_chain->page_cnt	= page_cnt;
	p_chain->mode		= mode;

	p_chain->intended_use		= intended_use;
	p_chain->elem_per_page		= ELEMS_PER_PAGE(elem_size);
	p_chain->usable_per_page =
		USABLE_ELEMS_PER_PAGE(elem_size, mode);
	p_chain->capacity		= p_chain->usable_per_page * page_cnt;
	p_chain->size			= p_chain->elem_per_page * page_cnt;
	p_chain->elem_per_page_mask	= p_chain->elem_per_page - 1;

	p_chain->elem_unusable = UNUSABLE_ELEMS_PER_PAGE(elem_size, mode);

	p_chain->next_page_mask = (p_chain->usable_per_page &
				   p_chain->elem_per_page_mask);

	if (mode == QED_CHAIN_MODE_NEXT_PTR) {
		struct qed_chain_next	*p_next;
		u16			i;

		for (i = 0; i < page_cnt - 1; i++) {
			/* Increment mem_phy to the next page. */
			p_phys_addr += QED_CHAIN_PAGE_SIZE;

			/* Initialize the physical address of the next page. */
			p_next = (struct qed_chain_next *)((u8 *)p_virt_addr +
							   elem_size *
							   p_chain->
							   usable_per_page);

			p_next->next_phys.lo	= DMA_LO_LE(p_phys_addr);
			p_next->next_phys.hi	= DMA_HI_LE(p_phys_addr);

			/* Initialize the virtual address of the next page. */
			p_next->next_virt = (void *)((u8 *)p_virt_addr +
						     QED_CHAIN_PAGE_SIZE);

			/* Move to the next page. */
			p_virt_addr = p_next->next_virt;
		}

		/* Last page's next should point to beginning of the chain */
		p_next = (struct qed_chain_next *)((u8 *)p_virt_addr +
						   elem_size *
						   p_chain->usable_per_page);

		p_next->next_phys.lo	= DMA_LO_LE(p_chain->p_phys_addr);
		p_next->next_phys.hi	= DMA_HI_LE(p_chain->p_phys_addr);
		p_next->next_virt	= p_chain->p_virt_addr;
	}
	qed_chain_reset(p_chain);
}

/**
 * @brief qed_chain_pbl_init - Initalizes a basic pbl chain
 *        struct
 * @param p_chain
 * @param p_virt_addr	virtual address of allocated buffer's beginning
 * @param p_phys_addr	physical address of allocated buffer's beginning
 * @param page_cnt	number of pages in the allocated buffer
 * @param elem_size	size of each element in the chain
 * @param use_mode
 * @param p_phys_pbl	pointer to a pre-allocated side table
 *                      which will hold physical page addresses.
 * @param p_virt_pbl	pointer to a pre allocated side table
 *                      which will hold virtual page addresses.
 */
static inline void
qed_chain_pbl_init(struct qed_chain *p_chain,
		   void *p_virt_addr,
		   dma_addr_t p_phys_addr,
		   u16 page_cnt,
		   u8 elem_size,
		   enum qed_chain_use_mode use_mode,
		   dma_addr_t p_phys_pbl,
		   dma_addr_t *p_virt_pbl)
{
	dma_addr_t *p_pbl_dma = p_virt_pbl;
	int i;

	qed_chain_init(p_chain, p_virt_addr, p_phys_addr, page_cnt,
		       elem_size, use_mode, QED_CHAIN_MODE_PBL);

	p_chain->pbl.p_phys_table = p_phys_pbl;
	p_chain->pbl.p_virt_table = p_virt_pbl;

	/* Fill the PBL with physical addresses*/
	for (i = 0; i < page_cnt; i++) {
		*p_pbl_dma = p_phys_addr;
		p_phys_addr += QED_CHAIN_PAGE_SIZE;
		p_pbl_dma++;
	}
}

/**
 * @brief qed_chain_set_prod - sets the prod to the given
 *        value
 *
 * @param prod_idx
 * @param p_prod_elem
 */
static inline void qed_chain_set_prod(struct qed_chain *p_chain,
				      u16 prod_idx,
				      void *p_prod_elem)
{
	p_chain->prod_idx	= prod_idx;
	p_chain->p_prod_elem	= p_prod_elem;
}

/**
 * @brief qed_chain_get_elem -
 *
 * get a pointer to an element represented by absolute idx
 *
 * @param p_chain
 * @assumption p_chain->size is a power of 2
 *
 * @return void*, a pointer to next element
 */
static inline void *qed_chain_sge_get_elem(struct qed_chain *p_chain,
					   u16 idx)
{
	void *ret = NULL;

	if (idx >= p_chain->size)
		return NULL;

	ret = (u8 *)p_chain->p_virt_addr + p_chain->elem_size * idx;

	return ret;
}

/**
 * @brief qed_chain_sge_inc_cons_prod
 *
 * for sge chains, producer isn't increased serially, the ring
 * is expected to be full at all times. Once elements are
 * consumed, they are immediately produced.
 *
 * @param p_chain
 * @param cnt
 *
 * @return inline void
 */
static inline void
qed_chain_sge_inc_cons_prod(struct qed_chain *p_chain,
			    u16 cnt)
{
	p_chain->prod_idx += cnt;
	p_chain->cons_idx += cnt;
}

#endif
