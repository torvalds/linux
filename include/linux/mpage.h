/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/mpage.h
 *
 * Contains declarations related to preparing and submitting BIOS which contain
 * multiple pagecache pages.
 */

/*
 * (And no, it doesn't do the #ifdef __MPAGE_H thing, and it doesn't do
 * nested includes.  Get it right in the .c file).
 */
#ifdef CONFIG_BLOCK

struct writeback_control;
struct readahead_control;

void mpage_readahead(struct readahead_control *, get_block_t get_block);
int mpage_read_folio(struct folio *folio, get_block_t get_block);
int mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc, get_block_t get_block);

#endif
