/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_H
#define _MM_SWAP_H

struct mempolicy;

#ifdef CONFIG_SWAP
#include <linux/swapops.h> /* for swp_offset */
#include <linux/blk_types.h> /* for bio_end_io_t */

/* linux/mm/page_io.c */
int sio_pool_init(void);
struct swap_iocb;
void swap_read_folio(struct folio *folio, bool do_poll,
		struct swap_iocb **plug);
void __swap_read_unplug(struct swap_iocb *plug);
static inline void swap_read_unplug(struct swap_iocb *plug)
{
	if (unlikely(plug))
		__swap_read_unplug(plug);
}
void swap_write_unplug(struct swap_iocb *sio);
int swap_writepage(struct page *page, struct writeback_control *wbc);
void __swap_writepage(struct folio *folio, struct writeback_control *wbc);

/* linux/mm/swap_state.c */
/* One swap address space for each 64M swap space */
#define SWAP_ADDRESS_SPACE_SHIFT	14
#define SWAP_ADDRESS_SPACE_PAGES	(1 << SWAP_ADDRESS_SPACE_SHIFT)
extern struct address_space *swapper_spaces[];
#define swap_address_space(entry)			    \
	(&swapper_spaces[swp_type(entry)][swp_offset(entry) \
		>> SWAP_ADDRESS_SPACE_SHIFT])

/*
 * Return the swap device position of the swap entry.
 */
static inline loff_t swap_dev_pos(swp_entry_t entry)
{
	return ((loff_t)swp_offset(entry)) << PAGE_SHIFT;
}

void show_swap_cache_info(void);
bool add_to_swap(struct folio *folio);
void *get_shadow_from_swap_cache(swp_entry_t entry);
int add_to_swap_cache(struct folio *folio, swp_entry_t entry,
		      gfp_t gfp, void **shadowp);
void __delete_from_swap_cache(struct folio *folio,
			      swp_entry_t entry, void *shadow);
void delete_from_swap_cache(struct folio *folio);
void clear_shadow_from_swap_cache(int type, unsigned long begin,
				  unsigned long end);
void swapcache_clear(struct swap_info_struct *si, swp_entry_t entry);
struct folio *swap_cache_get_folio(swp_entry_t entry,
		struct vm_area_struct *vma, unsigned long addr);
struct folio *filemap_get_incore_folio(struct address_space *mapping,
		pgoff_t index);

struct folio *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
		struct vm_area_struct *vma, unsigned long addr,
		struct swap_iocb **plug);
struct folio *__read_swap_cache_async(swp_entry_t entry, gfp_t gfp_flags,
		struct mempolicy *mpol, pgoff_t ilx, bool *new_page_allocated,
		bool skip_if_exists);
struct folio *swap_cluster_readahead(swp_entry_t entry, gfp_t flag,
		struct mempolicy *mpol, pgoff_t ilx);
struct page *swapin_readahead(swp_entry_t entry, gfp_t flag,
			      struct vm_fault *vmf);

static inline unsigned int folio_swap_flags(struct folio *folio)
{
	return swp_swap_info(folio->swap)->flags;
}
#else /* CONFIG_SWAP */
struct swap_iocb;
static inline void swap_read_folio(struct folio *folio, bool do_poll,
		struct swap_iocb **plug)
{
}
static inline void swap_write_unplug(struct swap_iocb *sio)
{
}

static inline struct address_space *swap_address_space(swp_entry_t entry)
{
	return NULL;
}

static inline void show_swap_cache_info(void)
{
}

static inline struct folio *swap_cluster_readahead(swp_entry_t entry,
			gfp_t gfp_mask, struct mempolicy *mpol, pgoff_t ilx)
{
	return NULL;
}

static inline struct page *swapin_readahead(swp_entry_t swp, gfp_t gfp_mask,
			struct vm_fault *vmf)
{
	return NULL;
}

static inline int swap_writepage(struct page *p, struct writeback_control *wbc)
{
	return 0;
}

static inline void swapcache_clear(struct swap_info_struct *si, swp_entry_t entry)
{
}

static inline struct folio *swap_cache_get_folio(swp_entry_t entry,
		struct vm_area_struct *vma, unsigned long addr)
{
	return NULL;
}

static inline
struct folio *filemap_get_incore_folio(struct address_space *mapping,
		pgoff_t index)
{
	return filemap_get_folio(mapping, index);
}

static inline bool add_to_swap(struct folio *folio)
{
	return false;
}

static inline void *get_shadow_from_swap_cache(swp_entry_t entry)
{
	return NULL;
}

static inline int add_to_swap_cache(struct folio *folio, swp_entry_t entry,
					gfp_t gfp_mask, void **shadowp)
{
	return -1;
}

static inline void __delete_from_swap_cache(struct folio *folio,
					swp_entry_t entry, void *shadow)
{
}

static inline void delete_from_swap_cache(struct folio *folio)
{
}

static inline void clear_shadow_from_swap_cache(int type, unsigned long begin,
				unsigned long end)
{
}

static inline unsigned int folio_swap_flags(struct folio *folio)
{
	return 0;
}
#endif /* CONFIG_SWAP */
#endif /* _MM_SWAP_H */
