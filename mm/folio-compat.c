/*
 * Compatibility functions which bloat the callers too much to make inline.
 * All of the callers of these functions should be converted to use folios
 * eventually.
 */

#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include "internal.h"

struct address_space *page_mapping(struct page *page)
{
	return folio_mapping(page_folio(page));
}
EXPORT_SYMBOL(page_mapping);

void unlock_page(struct page *page)
{
	return folio_unlock(page_folio(page));
}
EXPORT_SYMBOL(unlock_page);

void end_page_writeback(struct page *page)
{
	return folio_end_writeback(page_folio(page));
}
EXPORT_SYMBOL(end_page_writeback);

void wait_on_page_writeback(struct page *page)
{
	return folio_wait_writeback(page_folio(page));
}
EXPORT_SYMBOL_GPL(wait_on_page_writeback);

void wait_for_stable_page(struct page *page)
{
	return folio_wait_stable(page_folio(page));
}
EXPORT_SYMBOL_GPL(wait_for_stable_page);

void mark_page_accessed(struct page *page)
{
	folio_mark_accessed(page_folio(page));
}
EXPORT_SYMBOL(mark_page_accessed);

bool set_page_writeback(struct page *page)
{
	return folio_start_writeback(page_folio(page));
}
EXPORT_SYMBOL(set_page_writeback);

bool set_page_dirty(struct page *page)
{
	return folio_mark_dirty(page_folio(page));
}
EXPORT_SYMBOL(set_page_dirty);

int __set_page_dirty_nobuffers(struct page *page)
{
	return filemap_dirty_folio(page_mapping(page), page_folio(page));
}
EXPORT_SYMBOL(__set_page_dirty_nobuffers);

bool clear_page_dirty_for_io(struct page *page)
{
	return folio_clear_dirty_for_io(page_folio(page));
}
EXPORT_SYMBOL(clear_page_dirty_for_io);

bool redirty_page_for_writepage(struct writeback_control *wbc,
		struct page *page)
{
	return folio_redirty_for_writepage(wbc, page_folio(page));
}
EXPORT_SYMBOL(redirty_page_for_writepage);

void lru_cache_add_inactive_or_unevictable(struct page *page,
		struct vm_area_struct *vma)
{
	folio_add_lru_vma(page_folio(page), vma);
}

int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
		pgoff_t index, gfp_t gfp)
{
	return filemap_add_folio(mapping, page_folio(page), index, gfp);
}
EXPORT_SYMBOL(add_to_page_cache_lru);

noinline
struct page *pagecache_get_page(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp)
{
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index, fgp_flags, gfp);
	if (IS_ERR(folio))
		return NULL;
	return folio_file_page(folio, index);
}
EXPORT_SYMBOL(pagecache_get_page);

struct page *grab_cache_page_write_begin(struct address_space *mapping,
					pgoff_t index)
{
	return pagecache_get_page(mapping, index, FGP_WRITEBEGIN,
			mapping_gfp_mask(mapping));
}
EXPORT_SYMBOL(grab_cache_page_write_begin);

bool isolate_lru_page(struct page *page)
{
	if (WARN_RATELIMIT(PageTail(page), "trying to isolate tail page"))
		return false;
	return folio_isolate_lru((struct folio *)page);
}

void putback_lru_page(struct page *page)
{
	folio_putback_lru(page_folio(page));
}

#ifdef CONFIG_MMU
void page_add_new_anon_rmap(struct page *page, struct vm_area_struct *vma,
		unsigned long address)
{
	VM_BUG_ON_PAGE(PageTail(page), page);

	return folio_add_new_anon_rmap((struct folio *)page, vma, address);
}
#endif
