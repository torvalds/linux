/*
 * Compatibility functions which bloat the callers too much to make inline.
 * All of the callers of these functions should be converted to use folios
 * eventually.
 */

#include <linux/pagemap.h>

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
