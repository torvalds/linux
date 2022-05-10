/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_H
#define _MM_SWAP_H

#ifdef CONFIG_SWAP
#include <linux/blk_types.h> /* for bio_end_io_t */

/* linux/mm/page_io.c */
int sio_pool_init(void);
struct swap_iocb;
int swap_readpage(struct page *page, bool do_poll,
		  struct swap_iocb **plug);
void __swap_read_unplug(struct swap_iocb *plug);
static inline void swap_read_unplug(struct swap_iocb *plug)
{
	if (unlikely(plug))
		__swap_read_unplug(plug);
}
void swap_write_unplug(struct swap_iocb *sio);
int swap_writepage(struct page *page, struct writeback_control *wbc);
void end_swap_bio_write(struct bio *bio);
int __swap_writepage(struct page *page, struct writeback_control *wbc,
		     bio_end_io_t end_write_func);

/* linux/mm/swap_state.c */
/* One swap address space for each 64M swap space */
#define SWAP_ADDRESS_SPACE_SHIFT	14
#define SWAP_ADDRESS_SPACE_PAGES	(1 << SWAP_ADDRESS_SPACE_SHIFT)
extern struct address_space *swapper_spaces[];
#define swap_address_space(entry)			    \
	(&swapper_spaces[swp_type(entry)][swp_offset(entry) \
		>> SWAP_ADDRESS_SPACE_SHIFT])

void show_swap_cache_info(void);
int add_to_swap(struct page *page);
void *get_shadow_from_swap_cache(swp_entry_t entry);
int add_to_swap_cache(struct page *page, swp_entry_t entry,
		      gfp_t gfp, void **shadowp);
void __delete_from_swap_cache(struct page *page,
			      swp_entry_t entry, void *shadow);
void delete_from_swap_cache(struct page *page);
void clear_shadow_from_swap_cache(int type, unsigned long begin,
				  unsigned long end);
void free_swap_cache(struct page *page);
struct page *lookup_swap_cache(swp_entry_t entry,
			       struct vm_area_struct *vma,
			       unsigned long addr);
struct page *find_get_incore_page(struct address_space *mapping, pgoff_t index);

struct page *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
				   struct vm_area_struct *vma,
				   unsigned long addr,
				   bool do_poll,
				   struct swap_iocb **plug);
struct page *__read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
				     struct vm_area_struct *vma,
				     unsigned long addr,
				     bool *new_page_allocated);
struct page *swap_cluster_readahead(swp_entry_t entry, gfp_t flag,
				    struct vm_fault *vmf);
struct page *swapin_readahead(swp_entry_t entry, gfp_t flag,
			      struct vm_fault *vmf);

static inline unsigned int page_swap_flags(struct page *page)
{
	return page_swap_info(page)->flags;
}
#else /* CONFIG_SWAP */
struct swap_iocb;
static inline int swap_readpage(struct page *page, bool do_poll,
				struct swap_iocb **plug)
{
	return 0;
}
static inline void swap_write_unplug(struct swap_iocb *sio)
{
}

static inline struct address_space *swap_address_space(swp_entry_t entry)
{
	return NULL;
}

static inline void free_swap_cache(struct page *page)
{
}

static inline void show_swap_cache_info(void)
{
}

static inline struct page *swap_cluster_readahead(swp_entry_t entry,
				gfp_t gfp_mask, struct vm_fault *vmf)
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

static inline struct page *lookup_swap_cache(swp_entry_t swp,
					     struct vm_area_struct *vma,
					     unsigned long addr)
{
	return NULL;
}

static inline
struct page *find_get_incore_page(struct address_space *mapping, pgoff_t index)
{
	return find_get_page(mapping, index);
}

static inline int add_to_swap(struct page *page)
{
	return 0;
}

static inline void *get_shadow_from_swap_cache(swp_entry_t entry)
{
	return NULL;
}

static inline int add_to_swap_cache(struct page *page, swp_entry_t entry,
					gfp_t gfp_mask, void **shadowp)
{
	return -1;
}

static inline void __delete_from_swap_cache(struct page *page,
					swp_entry_t entry, void *shadow)
{
}

static inline void delete_from_swap_cache(struct page *page)
{
}

static inline void clear_shadow_from_swap_cache(int type, unsigned long begin,
				unsigned long end)
{
}

static inline unsigned int page_swap_flags(struct page *page)
{
	return 0;
}
#endif /* CONFIG_SWAP */
#endif /* _MM_SWAP_H */
