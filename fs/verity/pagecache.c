// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/fsverity.h>
#include <linux/pagemap.h>

/**
 * generic_read_merkle_tree_page - generic ->read_merkle_tree_page helper
 * @inode:	inode containing the Merkle tree
 * @index:	0-based index of the Merkle tree page in the inode
 * @num_ra_pages: The number of Merkle tree pages that should be prefetched.
 *
 * The caller needs to adjust @index from the Merkle-tree relative index passed
 * to ->read_merkle_tree_page to the actual index where the Merkle tree is
 * stored in the page cache for @inode.
 */
struct page *generic_read_merkle_tree_page(struct inode *inode, pgoff_t index,
					   unsigned long num_ra_pages)
{
	struct folio *folio;

	folio = __filemap_get_folio(inode->i_mapping, index, FGP_ACCESSED, 0);
	if (IS_ERR(folio) || !folio_test_uptodate(folio)) {
		DEFINE_READAHEAD(ractl, NULL, NULL, inode->i_mapping, index);

		if (!IS_ERR(folio))
			folio_put(folio);
		else if (num_ra_pages > 1)
			page_cache_ra_unbounded(&ractl, num_ra_pages, 0);
		folio = read_mapping_folio(inode->i_mapping, index, NULL);
		if (IS_ERR(folio))
			return ERR_CAST(folio);
	}
	return folio_file_page(folio, index);
}
EXPORT_SYMBOL_GPL(generic_read_merkle_tree_page);
