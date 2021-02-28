/*
 * Compatibility functions which bloat the callers too much to make inline.
 * All of the callers of these functions should be converted to use folios
 * eventually.
 */

#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

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

bool page_mapped(struct page *page)
{
	return folio_mapped(page_folio(page));
}
EXPORT_SYMBOL(page_mapped);

void mark_page_accessed(struct page *page)
{
	folio_mark_accessed(page_folio(page));
}
EXPORT_SYMBOL(mark_page_accessed);

#ifdef CONFIG_MIGRATION
int migrate_page_move_mapping(struct address_space *mapping,
		struct page *newpage, struct page *page, int extra_count)
{
	return folio_migrate_mapping(mapping, page_folio(newpage),
					page_folio(page), extra_count);
}
EXPORT_SYMBOL(migrate_page_move_mapping);

void migrate_page_states(struct page *newpage, struct page *page)
{
	folio_migrate_flags(page_folio(newpage), page_folio(page));
}
EXPORT_SYMBOL(migrate_page_states);

void migrate_page_copy(struct page *newpage, struct page *page)
{
	folio_migrate_copy(page_folio(newpage), page_folio(page));
}
EXPORT_SYMBOL(migrate_page_copy);
#endif

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
